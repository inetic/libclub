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

#ifndef CLUB_NET_RESENDER_SOCKET_H
#define CLUB_NET_RESENDER_SOCKET_H

#include <boost/chrono.hpp>
#include "TimeoutSocket.h"
#include "KeepAlive.h"

#define DEFAULT_TIMEOUT 1000

namespace club {

//////////////////////////////////////////////////////////////////////
// Socket that tries to resend a packet until it receives an ack
// or until a specified timeout value (in milliseconds) is reached.
//////////////////////////////////////////////////////////////////////

class ResenderSocket : public TimeoutSocket {
private:
  typedef boost::asio::ip::udp      udp;
  typedef boost::system::error_code error_code;
  typedef std::chrono::system_clock system_clock;

  typedef TimeoutSocket               TSocket;

public:
  typedef udp::endpoint               endpoint_type;

public:

  //////////////////////////////////////////////////////////////////////////////
  ResenderSocket(boost::asio::io_service& io_service)
    : TSocket        (io_service)
    , _next_packet_id(0)
    , _keep_alive    (*this)
    , _retry_time_ms (200)
  {
  }

  ResenderSocket(udp::socket&& socket)
    : TSocket        (std::move(socket))
    , _next_packet_id(0)
    , _keep_alive    (*this)
    , _retry_time_ms (200)
  {
  }

  ResenderSocket( boost::asio::io_service&              io_service
                , const boost::asio::ip::udp::endpoint& ep)
    : TSocket        (io_service, ep)
    , _next_packet_id(0)
    , _keep_alive    (*this)
    , _retry_time_ms (200)
  {
  }

  ResenderSocket( boost::asio::io_service& io_service
                , unsigned short           port)
    : TSocket        (io_service, udp::endpoint(udp::v4(), port))
    , _next_packet_id(0)
    , _keep_alive    (*this)
    , _retry_time_ms (200)
  {
  }

  //////////////////////////////////////////////////////////////////////////////
  KeepAlive& get_keep_alive() { return _keep_alive; }
  TimeoutSocket& timeout_socket() { return *this; }

  //////////////////////////////////////////////////////////////////////////////
  void set_next_packet_id(uint32_t next_packet_id) {
    _next_packet_id = next_packet_id;
  }

  uint32_t get_next_packet_id() const {
    return _next_packet_id;
  }

  //////////////////////////////////////////////////////////////////////////////
  void move_counters_from(ResenderSocket& socket) {
    _next_packet_id = socket._next_packet_id;
    socket._next_packet_id = 0;
    TSocket::move_counters_from(socket);
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // max_retry_ms >  0
  //   => Will try to resend message till ACK arives back or
  //      time (now + max_retry_ms) is reached.
  //
  // max_retry_ms == 0
  //   => Won't try to repost, handler will always be
  //      called timed_out.
  //
  // handler :: void (const error_code&)
  template<class Handler, class ConstBufferSequence>
  void async_send( const ConstBufferSequence& buffer
                 , const Channel&             channel
                 , unsigned int               max_retry_ms
                 , const Handler&             handler)
  {
    async_send_to( remote_endpoint()
                 , buffer
                 , channel
                 , max_retry_ms
                 , handler);
  }

  //////////////////////////////////////////////////////////////////////////////
  //
  // max_retry_ms >  0
  //   => Will try to resend message till ACK arives back or
  //      time (now + max_retry_ms) is reached.
  //
  // max_retry_ms == 0
  //   => Won't try to repost, handler will always be
  //      called timed_out.
  //
  // handler :: void (const error_code&)
  template<class Handler, class ConstBufferSequence>
  void async_send_to( const udp::endpoint&       tx_endpoint
                    , const ConstBufferSequence& buffer
                    , Channel                    channel
                    , unsigned int               max_retry_ms
                    , const Handler&             handler)
  {
    _keep_alive.sent_something();

    ASSERT(!(channel == CHANNEL_UNR() && max_retry_ms != 0));

    uint32_t packet_id = _next_packet_id;

    if (channel != CHANNEL_UNR()) {
      ++_next_packet_id;
    }

    if (max_retry_ms == 0)
    {
      TSocket::async_send_to( packet_id
                            , channel
                            , tx_endpoint
                            , buffer, 0, handler);
      return;
    }

    using namespace std::chrono;

    system_clock::time_point time_max = system_clock::now()
                                      + milliseconds(max_retry_ms);

    unsigned int retry_in_ms =
      std::min<unsigned int>(_retry_time_ms, max_retry_ms);

    auto was_destroyed = _was_destroyed;

    TSocket::async_send_to( packet_id
                          , channel
                          , tx_endpoint
                          , buffer
                          , retry_in_ms

                          , [ this
                            , channel
                            , packet_id
                            , handler
                            , time_max
                            , tx_endpoint
                            , buffer
                            , was_destroyed]
                            (const error_code& error)
                            {
                              ASSERT(!*was_destroyed);
                              on_send_to( channel
                                        , packet_id
                                        , handler
                                        , time_max
                                        , tx_endpoint
                                        , buffer
                                        , error);
                            }
                           );
  }

  // handler :: void (const endpoint_type&, const error_code&, size_t)
  template<class Handler, class MutableBufferSequence>
  void async_receive_from( unsigned int timeout_ms
                         , const MutableBufferSequence& buffer
                         , const Handler& handler)
  {
    auto was_destroyed = _was_destroyed;

    TSocket::async_receive_from
        ( timeout_ms
        , buffer
        , [this, handler, was_destroyed]( const endpoint_type& endpoint
                         , const error_code&    error
                         , size_t               size)
          {
            on_receive(was_destroyed, handler, endpoint, error, size);
          }
        );
  }

  // handler :: void (const endpoint_type&, const error_code&, size_t)
  template<class Handler, class MutableBufferSequence>
  void async_receive_from( Channel    channel
                         , unsigned int timeout_ms
                         , const MutableBufferSequence& buffer
                         , const Handler& handler)
  {
    auto was_destroyed = _was_destroyed;
    TSocket::async_receive_from
        ( channel
        , timeout_ms
        , buffer
        , [this, handler, was_destroyed]( const endpoint_type& endpoint
                         , const error_code&    error
                         , size_t               size)
          {
            on_receive(was_destroyed, handler, endpoint, error, size);
          }
        );
  }

  void unbind_remote() {
    TimeoutSocket::unbind_remote();
    _keep_alive.stop();
  }

private:

  template<class Handler>
  void on_receive( const std::shared_ptr<bool>& was_destroyed
                 , const Handler&               handler
                 , const endpoint_type&         endpoint
                 , const error_code&            error
                 , size_t                       size) {
    if (!error && !*was_destroyed) {
      _keep_alive.received_something();
    }

    handler(endpoint, error, size);
  }

  template<class Handler, class Buffer>
  void on_send_to( Channel                         channel
                 , uint32_t                        packet_id
                 , const Handler&                  handler
                 , const system_clock::time_point& time_max
                 , const endpoint_type&            tx_endpoint
                 , const Buffer&                   buffer
                 , const error_code&               error)
  {
    using namespace std::chrono;

    system_clock::time_point now = system_clock::now();

    update_retry_time(error == boost::asio::error::timed_out);

    if (error == boost::asio::error::timed_out) {
      if (now >= time_max) {
        handler(error);
        return;
      }

      milliseconds time_left = duration_cast<milliseconds>(time_max - now);

      typedef unsigned int uint;

      uint retry_in_ms = std::min<uint>(time_left.count(), _retry_time_ms);

      auto was_destroyed = _was_destroyed;

      TSocket::async_send_to(
          packet_id
        , channel
        , tx_endpoint
        , buffer
        , retry_in_ms

        , [=](const error_code& error) {
            if (*was_destroyed) {
              throw std::runtime_error("Socket destroyed too soon.");
            }

            on_send_to( channel
                      , packet_id
                      , handler
                      , time_max
                      , tx_endpoint
                      , buffer
                      , error);
          }

        );

      return;
    }

    handler(error);
  }

  void update_retry_time(bool dropped_packet) {
    float f = _retry_time_ms;
    if (dropped_packet) {
      f = f * 1.5f;
      _retry_time_ms = std::min<int>(f, 1000);
    }
    else {
      _retry_time_ms = std::max<int>(_retry_time_ms - 20, 100);
    }
  }

  friend class Acceptor;
  friend class Socket;

  void reset_counters() {
    _next_packet_id = 0;
    TSocket::reset_last_acked_id();
  }

private:
  uint32_t     _next_packet_id;
  KeepAlive    _keep_alive;
  unsigned int _retry_time_ms;
};


} // club namespace

#endif // ifndef CLUB_NET_RESENDER_SOCKET_H
