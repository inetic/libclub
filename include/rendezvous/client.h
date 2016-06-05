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

#ifndef __RENDEZVOUS_CLIENT_H__
#define __RENDEZVOUS_CLIENT_H__

#include <vector>
#include <mutex>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>
#include <binary/encoder.h>
#include <binary/decoder.h>
#include <binary/ip.h>
#include <rendezvous/constants.h>

namespace rendezvous {

class client {
private:
  using Bytes = std::vector<uint8_t>;
  using udp = boost::asio::ip::udp;
  using Error = boost::system::error_code;
  using Handler = std::function<void(Error, udp::socket, udp::endpoint)>;
  using Constant = constants_v1;

  struct State {
    std::mutex mutex;
    udp::socket socket;
    Handler handler;
    bool was_destroyed;
    udp::endpoint rx_endpoint;
    Bytes rx_buffer;
    Bytes tx_buffer;

    State(udp::socket socket, Handler handler)
      : socket(std::move(socket))
      , handler(std::move(handler))
      , was_destroyed(false)
      , rx_buffer(256)
      , tx_buffer(256)
    {}

    void exec(Error error, udp::endpoint ep) {
      using namespace std;
      auto h = move(handler);
      // TODO: use socket directly once switch to c++14 is made.
      auto socket_ptr = make_shared<udp::socket>(move(socket));

      socket.get_io_service().post([h, error, socket_ptr, ep]() {
          h(error, move(*socket_ptr), ep);
          });
    }
  };

  using StatePtr = std::shared_ptr<State>;

public:
  using Service = uint32_t;

  static uint16_t version() { return 1; }

  client( Service service_number
        , udp::socket
        , udp::endpoint server_ep
        , Handler handler);

  boost::asio::io_service& get_io_service() const {
    return _state->socket.get_io_service();
  }

  ~client();

private:
  void start_receiving(StatePtr);
  void start_sending(StatePtr);

  void on_recv(StatePtr, Error, size_t);
  void on_send(StatePtr);

  std::vector<uint8_t> construct_fetch_message() const;
  std::vector<uint8_t> construct_close_message() const;

private:
  Service _service_number;
  boost::asio::steady_timer _resend_timer;
  udp::endpoint _server_ep;
  StatePtr _state;
};

} // rendezvous client

#include <rendezvous/constants.h>

namespace rendezvous {

inline
client::client( Service service_number
              , udp::socket socket
              , udp::endpoint server_ep
              , Handler handler)
  : _service_number(service_number)
  , _resend_timer(socket.get_io_service())
  , _server_ep(server_ep)
  , _state(std::make_shared<State>(std::move(socket), std::move(handler)))
{
  _state->tx_buffer = construct_fetch_message();

  start_sending(_state);
  start_receiving(_state);
}

inline
void client::start_receiving(StatePtr state) {
  state->socket.async_receive_from( boost::asio::buffer(state->rx_buffer)
                                  , state->rx_endpoint
                                  , [this, state](Error error, size_t size) {
                                    on_recv(std::move(state), error, size);
                                  });
}

inline
void client::on_recv(StatePtr state, Error error, size_t size) {
  using std::move;
  namespace ip = boost::asio::ip;

  std::lock_guard<std::mutex> guard(state->mutex);

  if (state->was_destroyed) {
    return state->exec(error, udp::endpoint());
  }

  if (error) {
    _resend_timer.cancel();
    return state->exec(error, udp::endpoint());
  }

  if (state->rx_endpoint != _server_ep) {
    return start_receiving(move(state));
  }

  binary::decoder d( state->rx_buffer.data()
                   , std::min( state->rx_buffer.size()
                             , size));

  auto plex_and_version = d.get<uint16_t>();
  auto length           = d.get<uint16_t>();
  auto cookie           = d.get<uint32_t>();
  auto method           = d.get<uint8_t>();

  if (d.error()) return start_receiving(move(state));

  d.shrink(length);

  std::bitset<2> plex;
  plex[0] = (plex_and_version & (1 << 14)) != 0;
  plex[1] = (plex_and_version & (1 << 15)) != 0;

  // TODO: Check version.
  if (plex != PLEX)           return start_receiving(move(state));
  if (cookie != COOKIE)       return start_receiving(move(state));
  if (method != METHOD_MATCH) return start_receiving(move(state));

  udp::endpoint reflexive_ep, ext_ep, int_ep;

  for (auto ep : {&reflexive_ep, &ext_ep, &int_ep}) {
    auto ipv  = d.get<uint8_t>();
    auto port = d.get<uint16_t>();

    if (ipv == IPV4_TAG) {
      *ep = udp::endpoint(d.get<ip::address_v4>(), port);
    }
    else if (ipv == IPV6_TAG) {
      *ep = udp::endpoint(d.get<ip::address_v6>(), port);
    }
    else {
      d.set_error();
      break;
    }
  }

  if (d.error()) return start_receiving(move(state));

  _resend_timer.cancel();

  if (reflexive_ep.address() == ext_ep.address()) {
    state->exec(error, int_ep);
  }
  else {
    state->exec(error, ext_ep);
  }
}

inline
void client::start_sending(StatePtr state) {
  _state->socket.async_send_to( boost::asio::buffer(state->tx_buffer)
                              , _server_ep
                              , [this, state](Error, size_t) {
                                on_send(std::move(state));
                              });
}

inline
void client::on_send(StatePtr state) {
  std::lock_guard<std::mutex> guard(state->mutex);

  if (state->was_destroyed || !state->handler) {
    return;
  }

  _resend_timer.expires_from_now(Constant::keepalive_duration() / 3);
  _resend_timer.async_wait([this, state](Error) {
      if (state->was_destroyed || !state->handler) return;
      start_sending(std::move(state));
    });
}

inline
client::~client() {
  std::lock_guard<std::mutex> guard(_state->mutex);

  _state->was_destroyed = true;

  if (_state->socket.is_open()) {
    // Attempt to close gracefuly.
    _state->socket.send_to( boost::asio::buffer(construct_close_message())
                          , _server_ep);

    _state->socket.close();
  }
}

inline
std::vector<uint8_t> client::construct_fetch_message() const {
  namespace ip = boost::asio::ip;
  ip::address internal_addr;

  {
    udp::socket tmp(get_io_service());
    tmp.connect(_server_ep);
    internal_addr = tmp.local_endpoint().address();
  }

  auto internal_port = _state->socket.local_endpoint().port();

  uint16_t payload_size = 1 /* method */
                        + sizeof(Service)
                        + 2 /* port */
                        + 1 /* ipv */
                        + (internal_addr.is_v4() ? 4 : 16);

  Bytes bytes(HEADER_SIZE + payload_size);
  binary::encoder e(bytes.data(), bytes.size());

  // Header
  uint16_t plex = 1 << 15;

  e.put((uint16_t) (plex | version()));
  e.put((uint16_t) payload_size);
  e.put((uint32_t) COOKIE);

  assert(e.written() == HEADER_SIZE);

  // Payload
  e.put((uint8_t) CLIENT_METHOD_FETCH);
  e.put((Service) _service_number); // Service
  e.put((uint16_t) internal_port);

  if (internal_addr.is_v4()) {
    e.put((uint8_t) IPV4_TAG);
    e.put(internal_addr.to_v4());
  }
  else {
    e.put((uint8_t) IPV6_TAG);
    e.put(internal_addr.to_v6());
  }

  assert(!e.error());
  assert(e.written() == bytes.size());

  return bytes;
}

inline
std::vector<uint8_t> client::construct_close_message() const {
  namespace ip = boost::asio::ip;

  uint16_t payload_size = 1 /* method */;

  Bytes bytes(HEADER_SIZE + payload_size);
  binary::encoder e(bytes.data(), bytes.size());

  // Header
  uint16_t plex = 1 << 15;

  e.put((uint16_t) (plex | version()));
  e.put((uint16_t) payload_size);
  e.put((uint32_t) COOKIE);

  assert(e.written() == HEADER_SIZE);

  // Payload
  e.put((uint8_t) CLIENT_METHOD_CLOSE);

  assert(!e.error());
  assert(e.written() == bytes.size());

  return bytes;
}

} // rendezvous client

#endif // ifndef __RENDEZVOUS_CLIENT_H__
