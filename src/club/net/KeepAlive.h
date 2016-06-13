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

#pragma once

#include <chrono>
#include "TimeoutSocket.h"

namespace club {

class KeepAlive {
public:
  typedef boost::system::error_code              error_code;
  typedef boost::asio::ip::udp::endpoint         endpoint_type;
  typedef std::function<void(const error_code&)> Handler;
  typedef std::chrono::high_resolution_clock     Clock;
  typedef TimeoutSocket                          Socket;
  typedef std::shared_ptr<bool>                  BoolPtr;

  KeepAlive(Socket& socket)
    : _socket(socket)
    , _loop_is_running(false)
    , _consecutive_timeouts(0)
    , _was_destroyed(new bool(false))
  {}

  void start(const Handler& on_disconnected) {
    ASSERT(!_loop_is_running);
    _on_disconnected = on_disconnected;
    _consecutive_timeouts = 0;

    if (_loop_is_running) return;
    _loop_is_running = true;
    receive();
  }

  void stop() {
    if (!_loop_is_running) return;
    _loop_is_running = false;
    _consecutive_timeouts = 0;
  }

  void received_something() { _consecutive_timeouts = 0; }
  void sent_something()     { _time_last_sent = Clock::now(); }

  bool is_running() const { return _loop_is_running; }

  unsigned int recv_timeout_ms() const { return 1500; }
  unsigned int allowed_timeout_count() const { return 6; }

  ~KeepAlive() { *_was_destroyed = true; }

private:
  void receive() {
    using std::bind;
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    _socket.async_receive_from(
        CHANNEL_KEEP_ALIVE()
      , recv_timeout_ms()
      , boost::asio::null_buffers()
      , bind(&KeepAlive::on_recv, this, _was_destroyed, _1, _2, _3)
      );
  }

  void on_recv( const BoolPtr& was_destroyed
              , const endpoint_type&
              , const error_code& error
              , size_t)
  {
    namespace asio = boost::asio;

    if (*was_destroyed) return;

    if (!_loop_is_running) {
      fire_handler(asio::error::operation_aborted);
      return;
    }

    if (error == asio::error::timed_out) {
      _consecutive_timeouts++;

      if (_consecutive_timeouts >= allowed_timeout_count()) {
        fire_handler(asio::error::timed_out);
        return;
      }
    }
    else if (error) {
      fire_handler(error);
      return;
    }
    else {
      _consecutive_timeouts = 0;
    }

    Clock::time_point now = Clock::now();

    if (now - _time_last_sent >= std::chrono::milliseconds(recv_timeout_ms()))
    {
      _time_last_sent = now;
      send();
    }
    else {
      receive();
    }
  }

  void send() {
    using std::bind;
    using std::placeholders::_1;

    _socket.async_send( 0
                      , CHANNEL_KEEP_ALIVE()
                      , boost::asio::null_buffers()
                      , 0
                      , bind(&KeepAlive::on_sent, this, _was_destroyed, _1)
                      );
  }

  void on_sent( const BoolPtr& was_destroyed
              , const error_code& /*error*/) {
    using std::bind;
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    if (*was_destroyed) return;

    if (!_loop_is_running) {
      fire_handler(boost::asio::error::operation_aborted);
      return;
    }

    receive();
  }

  void fire_handler(const error_code& error) {
    ASSERT(_on_disconnected);
    Handler h = std::move(_on_disconnected);
    _on_disconnected = Handler();
    h(error);
  }

private:
  Socket&                     _socket;
  bool                        _loop_is_running;
  Handler                     _on_disconnected;
  unsigned int                _consecutive_timeouts;
  Clock::time_point           _time_last_sent;
  std::shared_ptr<bool>       _was_destroyed;
};

} // club namespace
