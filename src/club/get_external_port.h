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

#ifndef CLUB_GET_EXTERNAL_PORT_H
#define CLUB_GET_EXTERNAL_PORT_H

#include <mutex>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include "stun_client.h"

namespace club {

class GetExternalPort {
  using udp = boost::asio::ip::udp;
  using Duration = boost::asio::steady_timer::duration;
  using Error = boost::system::error_code;
  using Handler = std::function<void(Error, udp::socket, udp::endpoint)>;

  struct State {
    boost::asio::io_service& ios;
    bool was_destroyed;
    Handler handler;
    udp::socket socket;
    bool timed_out;
    std::mutex mutex;

    State(boost::asio::io_service& ios, Handler handler)
      : ios(ios)
      , was_destroyed(false)
      , handler(std::move(handler))
      , socket(ios, udp::endpoint(udp::v4(), 0))
      , timed_out(false)
    {}

    void exec(Error error, udp::endpoint external_ep) {
      auto h = std::move(handler);
      // TODO: move the socket directly once switched to c++14
      auto socket_ptr = std::make_shared<udp::socket>(std::move(socket));
      ios.post([h, error, socket_ptr, external_ep]() {
            h(error, std::move(*socket_ptr), external_ep);
          });
    }
  };

public:
  GetExternalPort(boost::asio::io_service&, Duration, Handler);
  ~GetExternalPort();

private:
  std::shared_ptr<State> _state;
  std::unique_ptr<StunClient> _stun_client;
  udp::resolver _resolver;
  boost::asio::steady_timer _timer;
};

} // club namespace

namespace club {

inline
GetExternalPort::GetExternalPort( boost::asio::io_service& ios
                                , Duration max_duration
                                , Handler h)
  // TODO: v6 also
  : _state(std::make_shared<State>(ios, std::move(h)))
  , _stun_client(new StunClient(_state->socket))
  , _resolver(ios)
  , _timer(ios)
{
  namespace error = boost::asio::error;

  struct Stun {
    std::string url;
    std::string port;
  };

  std::lock_guard<std::mutex> guard(_state->mutex);

  // TODO: Create a StunDatabase object from where we'll
  // take these, also prefer to use our own stun server.
  static const std::vector<Stun> stuns(
    { {"stun.l.google.com",   "19302", }
    , {"stun1.l.google.com",  "19302", }
    , {"stun2.l.google.com",  "19302", }
    , {"stun3.l.google.com",  "19302", }
    , {"stun4.l.google.com",  "19302", } });
  //static const std::vector<Stun> stuns(
  //  { {"s1.taraba.net",   "3478", } });

  auto rand = std::bind(std::uniform_int_distribution<int>(0, stuns.size() - 1)
                       , std::mt19937(std::time(0)));

  auto stun_i = rand();
  udp::resolver::query q(stuns[stun_i].url, stuns[stun_i].port);

  auto state = _state;

  _timer.expires_from_now(max_duration);
  _timer.async_wait([=](Error error) {
      std::lock_guard<std::mutex> guard(state->mutex);

      if (state->was_destroyed) return;

      if (!error) {
        state->timed_out = true;
        _resolver.cancel();
        _stun_client.reset();
      }
    });

  _resolver.async_resolve(q, [=]( Error error
                                , udp::resolver::iterator iterator) {
      std::lock_guard<std::mutex> guard(state->mutex);

      if (state->was_destroyed) {
        error = error::operation_aborted;
      }

      if (state->timed_out) {
        error = error::timed_out;
      }

      if (error) {
        return state->exec(error, udp::endpoint());
      }

      _stun_client->reflect(*iterator
                          , [=](Error error, udp::endpoint ep) {
            std::lock_guard<std::mutex> guard(state->mutex);

            if (state->was_destroyed) {
              return state->exec(error::operation_aborted, ep);
            }

            if (error && state->timed_out) {
              error = error::timed_out;
            }

            _timer.cancel();
            return state->exec(error, ep);
          });
    });
}

inline
GetExternalPort::~GetExternalPort() {
  std::lock_guard<std::mutex> guard(_state->mutex);
  _state->was_destroyed = true;
}

} // club namespace

#endif // ifndef CLUB_GET_EXTERNAL_PORT_H
