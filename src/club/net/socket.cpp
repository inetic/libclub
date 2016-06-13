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

#include "club/socket.h"
#include "ResenderSocket.h"
#include "p2p_connect.h"
//#include "serialize/binary_stream_list.h"
//#include "serialize/binary_stream_net.h"
#include "binary/dynamic_encoder.h"
#include "serialize/net.h"
#include "debug/log.h"

using namespace club;
using boost::optional;
namespace asio   = boost::asio;

typedef ResenderSocket::endpoint_type      endpoint_type;
typedef Socket::Clock::duration   duration;
typedef Socket::Clock::time_point time_point;

////////////////////////////////////////////////////////////////////////////////
Socket::Socket(asio::io_service& io_service)
  : _socket(std::make_shared<Delegate>(io_service))
  , _closed(false)
  , _received_close(false)
  , _remote_known_to_have_symmetric_nat(false)
{
}

Socket::Socket(udp::socket&& s)
  : _socket(std::make_shared<Delegate>(std::move(s)))
  , _closed(false)
  , _received_close(false)
  , _remote_known_to_have_symmetric_nat(false)
{
}

Socket::Socket(Socket&& s)
  : _socket(std::move(s._socket))
  , _closed(s._closed)
  , _received_close(s._received_close)
  , _remote_known_to_have_symmetric_nat(s._remote_known_to_have_symmetric_nat)
{
  s._closed = true;
  s._received_close = false;
  s._remote_known_to_have_symmetric_nat = false;
}

Socket::Socket( asio::io_service&    io_service
                                , const endpoint_type& ep)
  : _socket(std::make_shared<Delegate>(io_service, ep))
  , _closed(false)
  , _received_close(false)
  , _remote_known_to_have_symmetric_nat(false)
{
}

Socket::Socket( asio::io_service& io_service
                                , unsigned short    port)
  : _socket(std::make_shared<Delegate>( io_service
                                      , udp::endpoint(udp::v4(), port)))
  , _closed(false)
  , _received_close(false)
  , _remote_known_to_have_symmetric_nat(false)
{
}

////////////////////////////////////////////////////////////////////////////////
asio::io_service& Socket::get_io_service() {
  return _socket->get_io_service();
}

////////////////////////////////////////////////////////////////////////////////
unsigned int Socket::id() const { return _socket->id(); }

////////////////////////////////////////////////////////////////////////////////
void Socket::move_counters_from(ResenderSocket& rsocket) {
  _socket->move_counters_from(rsocket);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::open () { _socket->open(); }

////////////////////////////////////////////////////////////////////////////////
size_t Socket::buffer_size() const {
  return _socket->buffer_size();
}

////////////////////////////////////////////////////////////////////////////////
endpoint_type
Socket::local_endpoint_to(const endpoint_type remote_endpoint)
{
  udp::socket tmp(get_io_service());
  tmp.connect(remote_endpoint);

  return endpoint_type( tmp.local_endpoint().address()
                      , _socket->local_endpoint().port());
}

////////////////////////////////////////////////////////////////////////////////
endpoint_type Socket::local_endpoint() {
  try {
    unsigned short local_port = _socket->local_endpoint().port();
    endpoint_type remote      = _socket->remote_endpoint();
    return endpoint_type(local_endpoint_to(remote).address(), local_port);
  } catch(const std::exception&) {
    return _socket->local_endpoint();
  }
}

////////////////////////////////////////////////////////////////////////////////
optional<endpoint_type> Socket::remote_endpoint() const {
  if (_received_close)
    return optional<endpoint_type>();
  try {
    return _socket->remote_endpoint();
  } catch (const boost::system::system_error&) {
    return optional<endpoint_type>();
  }
}

////////////////////////////////////////////////////////////////////////////////
Socket::~Socket() {
  close();
}

////////////////////////////////////////////////////////////////////////////////
bool Socket::is_open() const { return !_closed && _socket->is_open(); }

////////////////////////////////////////////////////////////////////////////////
void Socket::close() {
  if (_closed) return;

  _closed = true;

  if (!_socket->is_open()) return;

  if (!remote_endpoint()) {
    _socket->close();
    return;
  }

  _socket->cancel_all(asio::error::operation_aborted);
  auto s = _socket;

  _socket->async_send( asio::null_buffers()
                     , CHANNEL_CLOSE()
                     , 200
                     , [s](const error_code&) { s->close(); });

  _socket->get_keep_alive().stop();
}

////////////////////////////////////////////////////////////////////////////////
bool Socket::is_connected() const {
  return !_closed && remote_endpoint();
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_send( Channel                   channel
                       , const asio::const_buffer& buffer
                       , unsigned int              timeout_ms
                       , const TXHandler&          handler)
{
  ASSERT(!*_socket->_was_destroyed);

  if (!is_connected()) {
    handler(asio::error::not_connected);
    return;
  }

  auto was_destroyed = _socket->_was_destroyed;
  auto buffers = asio::const_buffers_1(buffer);

  _socket->async_send( buffers, channel, timeout_ms
                     , [was_destroyed, handler](const error_code& e) {
                         ASSERT(!*was_destroyed);
                         handler(e);
                       });

  start_receiving_close();
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_send( const asio::const_buffer&  buffer
                       , unsigned int   timeout_ms
                       , const TXHandler& handler)
{
  async_send(CHANNEL_DAT(), buffer, timeout_ms, handler);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_receive( const asio::mutable_buffer&  buffer
                          , unsigned int   timeout_ms
                          , const RXHandler& handler)
{
  async_receive(CHANNEL_DAT(), buffer, timeout_ms, handler);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_receive( Channel                     channel
                          , const asio::mutable_buffer& buffer
                          , unsigned int                timeout_ms
                          , const RXHandler&            handler)
{
  ASSERT(!*_socket->_was_destroyed);

  if (!is_connected()) {
    get_io_service().post([handler](){
        handler(asio::error::not_connected, 0); });
    return;
  }

  _socket->async_receive_from
    ( channel
    , timeout_ms
    , asio::mutable_buffers_1(buffer)
    , [=](const endpoint_type&, const error_code& error, size_t size)
      {
        if (error && error != asio::error::timed_out) {
          _closed = true;
          //_socket->close();
        }

        handler(error, size);
      });

  start_receiving_close();
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_connect( unsigned int          timeout_ms
                          , const endpoint_type&  remote_endpoint
                          , const ConnectHandler& handler)
{
  binary::dynamic_encoder<char> encoder;
  encoder.put(local_endpoint_to(remote_endpoint));

  _socket->reset_counters();

  using namespace std::chrono;

  std::shared_ptr<Bytes> buffer(new Bytes(encoder.move_data()));

  Clock::time_point end_time = Clock::now() + milliseconds(timeout_ms);

  _socket->async_send_to
      ( remote_endpoint
      , asio::buffer(*buffer)
      , CHANNEL_DAT()
      , timeout_ms

      , [=](const error_code& error) {
          on_private_endpoint_sent( end_time
                                  , buffer
                                  , handler
                                  , error);
        });
}

////////////////////////////////////////////////////////////////////////////////
void Socket::bind(const endpoint_type& endpoint) {
  _socket->bind_remote(endpoint);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::async_p2p_connect
    ( unsigned int             timeout_ms
    , const endpoint_type&     remote_private_endpoint
    , const endpoint_type&     remote_public_endpoint
    , const P2PConnectHandler& handler )
{
  if (is_connected()) {
    _socket->unbind_remote();
  }

  p2p_connect( *_socket
             , timeout_ms
             , remote_private_endpoint
             , remote_public_endpoint
             , [=]( const error_code&    error
                  , const endpoint_type& requested_endpoint
                  , const endpoint_type& connected_endpoint)
               {
                 if (!error) {
                   _socket->bind_remote(connected_endpoint);

                   if (requested_endpoint != connected_endpoint) {
                     _remote_known_to_have_symmetric_nat = true;
                   }
                 }

                 handler(error, requested_endpoint, connected_endpoint);
               });
}

////////////////////////////////////////////////////////////////////////////////
void Socket::on_private_endpoint_sent
    ( const time_point              end_time
    , const std::shared_ptr<Bytes>& bytes
    , const ConnectHandler&         handler
    , const error_code&             error)
{
  if (error) {
    handler(error);
    return;
  }

  int time_left_ms = milliseconds_left(end_time);

  if (time_left_ms <= 0) {
    handler(asio::error::timed_out);
    return;
  }

  _socket->async_receive_from
    ( time_left_ms
    , asio::buffer(*bytes)
    , [=]( const endpoint_type& endpoint
         , const error_code& error
         , size_t size)
      {
        bytes->resize(std::min(size, bytes->size()));

        on_private_endpoint_recv( end_time
                                , bytes
                                , handler
                                , endpoint
                                , error);
      });
}

////////////////////////////////////////////////////////////////////////////////
duration::rep
Socket::milliseconds_left(const time_point& end) const {
  using namespace std::chrono;
  milliseconds timeout_left_ms
    = duration_cast<milliseconds>(end - Clock::now());

  return timeout_left_ms.count();
}

////////////////////////////////////////////////////////////////////////////////
template<class Handler>
void Socket::on_private_endpoint_recv
    ( const time_point              end_time
    , const std::shared_ptr<Bytes>& bytes
    , const Handler&                handler
    , const endpoint_type&          rx_endpoint
    , const error_code&             error)
{
  if (error) {
    handler(error);
    return;
  }

  int time_left_ms = milliseconds_left(end_time);

  if (time_left_ms <= 0) {
    handler(error);
    return;
  }

  binary::decoder decoder(bytes->data(), bytes->size());
  endpoint_type remote_private_endpoint = decoder.get<endpoint_type>();
  endpoint_type remote_public_endpoint( rx_endpoint.address()
                                      , remote_private_endpoint.port());

  if (decoder.error()) {
    return handler(boost::asio::error::connection_aborted);
  }

  using namespace std::chrono;

  async_p2p_connect( time_left_ms
                   , remote_private_endpoint
                   , remote_public_endpoint
                   , [handler]( error_code error
                              , endpoint_type
                              , endpoint_type) { handler(error); });
}

////////////////////////////////////////////////////////////////////////////////
void Socket::start_receiving_close() {
  KeepAlive& keep_alive = _socket->get_keep_alive();

  auto was_destroyed = _socket->_was_destroyed;

  if (!keep_alive.is_running()) {
    keep_alive.start([=](const error_code& error) {
        if (*was_destroyed) return;
        on_keep_alive_timeout(error);
        });
  }

  if (_socket->has_active_receiver(CHANNEL_CLOSE()))
    return;

  _socket->async_receive_from
    ( CHANNEL_CLOSE(), -1, asio::null_buffers()
    , [=]( const endpoint_type& endpoint
         , const error_code&    error
         , size_t               size) {
           if (*was_destroyed) return;
           on_recv_close(endpoint, error, size);
         });
}

////////////////////////////////////////////////////////////////////////////////
void Socket::on_recv_close( const endpoint_type&
                          , const error_code&    error
                          , size_t               /*size*/)
{
  if (error) return;

  _received_close = true;
  _socket->cancel_all(asio::error::connection_reset);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::debug(bool b) {
  _socket->debug(b);
}

////////////////////////////////////////////////////////////////////////////////
void Socket::on_keep_alive_timeout(const error_code& error) {
  if (error == asio::error::operation_aborted) {
    return;
  }

  _socket->cancel_all(asio::error::timed_out);
  _socket->close();
}

////////////////////////////////////////////////////////////////////////////////

