// Copyright 2016 Peter Jankuliak
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CLUB_NET_ACCEPTOR_H
#define CLUB_NET_ACCEPTOR_H

#include <map>
#include "ConnectedSocket.h"
#include "ResenderSocket.h"
#include "../../Benchmark.h"
#include "serialize/net.h"

namespace club {

class Acceptor {

  typedef boost::asio::ip::address           Address;
  typedef boost::asio::ip::udp               udp;
  typedef boost::asio::ip::udp::endpoint     endpoint_type;
  typedef boost::system::error_code          error_code;
  typedef std::vector<char>                  Bytes;
  typedef std::chrono::high_resolution_clock Clock;

  // LRU style map to store how the other side changed ports
  // if it is behind a Symmetric NAT.
  class PortPredictor {
  public:
    void add_sample( const udp::endpoint& requested
                   , const udp::endpoint& mapped) {
      if (requested.address() != mapped.address()) {
        // I haven't thought yet through whether this case
        // could be predicted, so currently just ignore it.
        return;
      }
      if (requested.port() == mapped.port()) {
        erase(requested.address());
        return;
      }

      insert_port_shift(requested.address(), mapped.port() - requested.port());
    }

    udp::endpoint predict(const udp::endpoint& from) const {
      auto map_i = _map.find(from.address());
      if (map_i == _map.end()) {
        return from;
      }
      return udp::endpoint(from.address(), from.port() + map_i->second->second);
    }

  private:
    void erase(const boost::asio::ip::address& addr) {
      auto i = _map.find(addr);
      if (i == _map.end()) return;
      _list.erase(i->second);
      _map.erase(i);
    }

    void insert_port_shift(const boost::asio::ip::address& addr, int port_shift)
    {
      auto i = _map.find(addr);

      if (i != _map.end()) {
        i->second->second = port_shift;
        // Move to the end of the list (as most recent).
        _list.splice(_list.end(), _list, i->second);
        return;
      }

      if (_map.size() >= max_size()) {
        auto oldest_list_i = _list.begin();
        _map.erase(oldest_list_i->first);
        _list.erase(oldest_list_i);
      }

      _list.push_back(std::make_pair(addr, port_shift));
      _map[addr] = --_list.end();
    }

    static size_t max_size() { return 64; }

  private:
    using Entry = std::pair<Address, int>;

    std::list<Entry> _list;
    std::map<Address, std::list<Entry>::iterator> _map;
  };

public:

  Acceptor( boost::asio::io_service& io_service
          , const endpoint_type&     endpoint)
    : _socket(io_service, endpoint)
  { }

  Acceptor( boost::asio::io_service& io_service
          , unsigned short           port)
    : _socket(io_service, udp::endpoint(udp::v4(), port))
  { }

  endpoint_type local_endpoint() const { return _socket.local_endpoint(); }
  void close() { _socket.close(); }

  boost::asio::io_service& get_io_service() {
    return _socket.get_io_service();
  }

  // handler :: void (const error_code&)
  template<class Handler>
  void async_accept( unsigned int     accept_timeout_ms
                   , unsigned int     connect_timeout_ms
                   , ConnectedSocket& socket
                   , const Handler&   handler)
  {
    namespace asio = boost::asio;

    if (socket.is_connected()) {
      handler(asio::error::already_connected);
      return;
    }

    _socket.unbind_remote();

    _socket.reset_counters();

    // Will hold clients endpoint as he sees himself.
    // (also known as client's-private-endpoint)
    std::shared_ptr<Bytes> bytes(new Bytes(128));

    _socket.async_receive_from
      ( accept_timeout_ms
      , asio::buffer(*bytes)
      , [this, connect_timeout_ms, bytes, handler, &socket]
        (const endpoint_type& endpoint, const error_code& error, size_t size)
        {
          on_recv_private_endpoint( connect_timeout_ms
                                  , bytes
                                  , handler
                                  , socket
                                  , endpoint
                                  , error
                                  , size);
        }
      );
  }

  udp::endpoint predict_port(const udp::endpoint& orig) const {
    return _port_predictor.predict(orig);
  }

private:

  Clock::duration::rep time_left_to(const Clock::time_point end) const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(end - Clock::now()).count();
  }

  template<class Handler> void on_recv_private_endpoint
        ( unsigned int                  connect_timeout_ms
        , const std::shared_ptr<Bytes>& bytes
        , const Handler&                handler
        , ConnectedSocket&              socket
        , const endpoint_type&          client_public_endpoint
        , const error_code&             error
        , size_t                        size)
  {
    using namespace std::chrono;

    // We need to ignore all the other requests till we finish
    // the connection procedure.
    _socket.bind_remote(client_public_endpoint);

    if (error) {
      handler(error);
      return;
    }

    if (!socket.is_open())
    {
      ASSERT(0);
      socket.open();
      _socket.bind_remote(udp::endpoint(udp::v4(), 0));
    }

    Clock::time_point end_time = Clock::now()
                               + milliseconds(connect_timeout_ms);

    bytes->resize(std::min(bytes->size(), size));
    binary::decoder decoder(bytes->data(), bytes->size());

    auto client_private_endpoint = decoder.get<endpoint_type>();


    udp::socket tmp_socket(socket.get_io_service());
    tmp_socket.connect(client_public_endpoint);

    endpoint_type our_private_endpoint( tmp_socket.local_endpoint().address()
                                      , socket.local_endpoint().port());

    binary::dynamic_encoder<char> encoder;
    encoder.put(our_private_endpoint);

    *bytes = bs.move_data();

    _socket.async_send_to
      ( client_public_endpoint
      , boost::asio::buffer(*bytes)
      , CHANNEL_DAT()
      , connect_timeout_ms
      , [ this
        , end_time
        , bytes
        , handler
        , &socket
        , client_public_endpoint
        , client_private_endpoint]
        (const error_code& error)
        {
          on_private_endpoint_sent( end_time
                                  , bytes
                                  , handler
                                  , socket
                                  , client_public_endpoint
                                  , client_private_endpoint
                                  , error);
        }
      );
  }

  template<class Handler>
  void on_private_endpoint_sent( const Clock::time_point& end_time
                               , const std::shared_ptr<Bytes>& bytes
                               , const Handler&       handler
                               , ConnectedSocket&     socket
                               , const endpoint_type& client_public_endpoint
                               , const endpoint_type& client_private_endpoint
                               , const error_code&    error)
  {
    if (error) {
      handler(error);
      return;
    }

    Clock::duration::rep time_left_ms = time_left_to(end_time);

    if (time_left_ms <= 0) {
      handler(boost::asio::error::timed_out);
      return;
    }

    auto predicted_pub_ep = predict_port(client_public_endpoint);

    bool is_predicted = predicted_pub_ep != client_public_endpoint;

    socket.move_counters_from(_socket);

    socket.async_p2p_connect
      ( time_left_ms
      , predicted_pub_ep
      , client_private_endpoint
      , [=, &socket]( const error_code& error
                    , endpoint_type requested_ep
                    , endpoint_type connected_ep) {
          if (!error) {
            if (is_predicted || requested_ep != connected_ep) {
              socket.remote_known_to_have_symmetric_nat(true);
            }

            // We want to add sample iff (A or B)
            // A: we did not predict change, but change was made (to ports).
            // B: we did predict chane, but no change was made.
            bool A = !is_predicted && requested_ep != connected_ep;
            bool B = is_predicted && requested_ep == connected_ep;

            if (A || B) {
              _port_predictor.add_sample(requested_ep, connected_ep);
            }
          }
          handler(error);
        });
  }

private:

  ResenderSocket _socket;
  endpoint_type  _rx_endpoint;
  PortPredictor  _port_predictor;
};

} // club namespace

#endif // ifndef CLUB_NET_ACCEPTOR_H
