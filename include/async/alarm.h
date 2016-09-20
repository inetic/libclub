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

#ifndef ASYNC_ALARM_H
#define ASYNC_ALARM_H

#include <boost/asio/steady_timer.hpp>

// Alarm is similar to the boost::steady_timer but
// it doesn't call its on_expire handler if it was
// stopped or destroyed.
namespace async {

class alarm {
private:
  using clock = std::chrono::steady_clock;

  enum State { idle, running, canceling, canceling_for_restart };

  struct Storage {
    bool was_destroyed = false;
    std::function<void ()> on_expire;

    Storage(std::function<void()> on_exp)
      : on_expire(std::move(on_exp))
    {}
  };

  using StoragePtr = std::shared_ptr<Storage>;
  using error_code = boost::system::error_code;

public:
  using duration = boost::asio::steady_timer::duration;

public:

  uint32_t time() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(clock::now() - s).count();
  }

  template<class H>
  alarm(boost::asio::io_service& ios, H&& on_expire)
    : _storage(std::make_shared<Storage>(std::forward<H>(on_expire)))
    , _state(idle)
    , _timer(ios)
  { }

  alarm(alarm&) = delete;
  alarm& operator=(alarm&) = delete;
  alarm(alarm&& other) = delete;

  void start(duration timeout) {
    //std::cout << "    " << time() << " " << this << " alarm::start_locked " << state_to_str(_state) << " " << std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count() << std::endl;
    switch (_state) {
      case idle:
        _state = running;
        {
          auto expiration_time = clock::now() + timeout;
          _timer.expires_at(expiration_time);
          _timer.async_wait(
              [this, s = _storage, expiration_time](error_code e) {
                if (s->was_destroyed) return;
                on_timeout(e, expiration_time);
              });
        }
        break;
      case running:
        _state = canceling_for_restart;
        _timer.cancel();
        _timeout = timeout;
        break;
      case canceling:
        _state = canceling_for_restart;
        _timeout = timeout;
        break;
      case canceling_for_restart:
        _timeout = timeout;
        break;
    }
  }

  void stop() {
    //std::cout << "    " << time() << " " << this << " alarm::stop " << state_to_str(_state) << std::endl;
    switch (_state) {
      case idle:                  break;
      case running:               _state = canceling;
                                  _timer.cancel();
                                  break;
      case canceling:             break;
      case canceling_for_restart: _state = canceling; break;
    }
  }

  ~alarm() {
    _storage->was_destroyed = true;
    stop();
  }

private:
  void on_timeout(error_code e, clock::time_point expiration_time) {
    //std::cout << "    " << time() << " " << this << " alarm::on_timeout " << state_to_str(_state) << " " << e.message() << std::endl;
    switch (_state) {
      case idle:
        assert(0 && "This state shouldn't be possible here");
        break;
      case running:
        _state = idle;
        {
          auto now = clock::now();
          if (now < expiration_time) {
            // Very rarely it happens that the timer fires before the
            // expiration time, seems like a bug in asio::steady_timer.
            using namespace std::chrono;
            std::cout << "timer fired " << duration_cast<milliseconds>(expiration_time - now).count() << " ms too soon" << std::endl;
            assert(0);
            return start(expiration_time - now);
          }
          auto s = _storage;
          s->on_expire();
        }
        break;
      case canceling:
        _state = idle;
        break;
      case canceling_for_restart:
        _state = idle;
        start(_timeout);
        break;
    }
  }

  std::string state_to_str(State s) {
    switch (s) {
      case idle: return "idle";
      case running: return "running";
      case canceling: return "canceling";
      case canceling_for_restart: return "canceling_for_restart";
    }
    return "???";
  }

private:
  StoragePtr                _storage;
  State                     _state;
  boost::asio::steady_timer _timer;
  duration                  _timeout;

  clock::time_point s = clock::now();
};

} // async namespace

#endif // ifndef ASYNC_ALARM_H

