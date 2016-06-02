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

#ifndef __NET_PL_CONNECTED_SOCKET_H__
#define __NET_PL_CONNECTED_SOCKET_H__

#include <boost/asio/ip/udp.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include "Channel.h"

namespace net { namespace PL {

class ResenderSocket;

class ConnectedSocket {
  typedef boost::asio::ip::udp               udp;

public:
  typedef std::chrono::high_resolution_clock             Clock;

  typedef udp::endpoint                                  endpoint_type;
  typedef boost::system::error_code                      error_code;
  typedef std::vector<char>                              Bytes;
  typedef ResenderSocket                                 Delegate;

  typedef std::function<void(const error_code&)>         TXHandler;

  typedef std::function<void(const error_code&, size_t)> RXHandler;

  typedef std::function<void(const error_code&)>         ConnectHandler;

  typedef std::function<void( const error_code&
                            , const endpoint_type&
                            , const endpoint_type&)>     P2PConnectHandler;

public:

  //////////////////////////////////////////////////////////////////////////////
  // To have the system assing a random port to this socket, use the third
  // constructor and have port set to zero. (TODO: I'm not sure if the
  // first constructor is being used).
  ConnectedSocket(boost::asio::io_service& io_service);
  ConnectedSocket(udp::socket&&);
  ConnectedSocket(ConnectedSocket&&);
  ConnectedSocket(boost::asio::io_service& io_service, const endpoint_type& ep);
  ConnectedSocket(boost::asio::io_service& io_service, unsigned short port);

  // Delegated stuff.
  boost::asio::io_service& get_io_service();
  unsigned int id() const;
  void move_counters_from(ResenderSocket& socket);
  void open ();
  size_t buffer_size() const;

  //////////////////////////////////////////////////////////////////////////////
  // Determine our local endpoint that shall be used if we're to connect
  // to remote_endpoint (Note, depending on the remote_endpoint the system
  // will chose which device to use: lo, eth0, wlan0, ... and thus the
  // local ip will differ).
  endpoint_type local_endpoint_to(const endpoint_type remote_endpoint);

  endpoint_type local_endpoint();

  //////////////////////////////////////////////////////////////////////////////

  boost::optional<endpoint_type> remote_endpoint() const;

  ~ConnectedSocket();

  bool is_open() const;

  void close();

  bool is_connected() const;

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  void async_send( Channel                          channel
                 , const boost::asio::const_buffer& buffer
                 , unsigned int                     timeout_ms
                 , const TXHandler&                 handler);

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  void async_send( const boost::asio::const_buffer&  buffer
                 , unsigned int   timeout_ms
                 , const TXHandler& handler);

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&, size_t)
  void async_receive( const boost::asio::mutable_buffer&  buffer
                    , unsigned int   timeout_ms
                    , const RXHandler& handler);

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&, size_t)
  void async_receive( Channel                            channel
                    , const boost::asio::mutable_buffer& buffer
                    , unsigned int                       timeout_ms
                    , const RXHandler& handler);

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  void async_connect( unsigned int          timeout_ms
                    , const endpoint_type&  remote_endpoint
                    , const ConnectHandler& handler);

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  void async_p2p_connect( unsigned int             timeout_ms
                        , const endpoint_type&     remote_private_endpoint
                        , const endpoint_type&     remote_public_endpoint
                        , const P2PConnectHandler& handler );

  //////////////////////////////////////////////////////////////////////////////
  void debug(bool b);

  //////////////////////////////////////////////////////////////////////////////
  bool remote_known_to_have_symmetric_nat() const {
    return _remote_known_to_have_symmetric_nat;
  }

  void remote_known_to_have_symmetric_nat(bool b) {
    _remote_known_to_have_symmetric_nat = b;
  }

  //////////////////////////////////////////////////////////////////////////////
  void bind(const endpoint_type& endpoint);

  //////////////////////////////////////////////////////////////////////////////
private:

  void on_private_endpoint_sent( const Clock::time_point       end_time
                               , const std::shared_ptr<Bytes>& bytes
                               , const ConnectHandler&         handler
                               , const error_code&             error);

  Clock::duration::rep milliseconds_left(const Clock::time_point& end) const;

  template<class Handler>
  void on_private_endpoint_recv( const Clock::time_point       end_time
                               , const std::shared_ptr<Bytes>& bytes
                               , const Handler&                handler
                               , const endpoint_type&          rx_endpoint
                               , const error_code&             error);

  //////////////////////////////////////////////////////////////////////////////
  void start_receiving_close();

  void on_recv_close( const endpoint_type& endpoint
                    , const error_code&    error
                    , size_t               size);

  void on_keep_alive_timeout(const error_code& error);

  //////////////////////////////////////////////////////////////////////////////
  friend class Acceptor;

private:
  std::shared_ptr<Delegate> _socket;
  bool                      _closed;
  bool                      _received_close;
  bool                      _remote_known_to_have_symmetric_nat;
};

}} // net::PL namespace

#endif // ifndef __NET_PL_CONNECTED_SOCKET_H__

