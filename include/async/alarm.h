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

#ifndef __ASYNC_ALARM_H__
#define __ASYNC_ALARM_H__

#include <mutex>
#include <boost/asio/steady_timer.hpp>

// Alarm is similar to the boost::steady_timer but
// it doesn't call its on_expire handler if it was
// stopped or destroyed.
namespace async {

class alarm {
private:
  enum State { idle, running, canceling, canceling_for_restart };

  struct Storage {
    bool was_destroyed;
    std::mutex mutex;

    Storage() : was_destroyed(false) {}
  };

  using StoragePtr = std::shared_ptr<Storage>;
  using error_code = boost::system::error_code;

public:
  using duration = boost::asio::steady_timer::duration;

public:

  template<class H>
  alarm(boost::asio::io_service& ios, const H& on_expire)
    : _storage(std::make_shared<Storage>())
    , _state(idle)
    , _timer(ios)
    , _on_expire(on_expire)
  { }

  alarm(alarm&) = delete;
  alarm& operator=(alarm&) = delete;
  alarm(alarm&& other) = delete;

  void start(duration timeout) {
    std::lock_guard<std::mutex> lock(_storage->mutex);
    start_locked(timeout);
  }

  void stop() {
    std::lock_guard<std::mutex> lock(_storage->mutex);

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
    {
      std::lock_guard<std::mutex> lock(_storage->mutex);
      _storage->was_destroyed = true;
    }
    stop();
  }

private:
  void on_timeout(error_code) {
    std::function<void()> on_expire;

    {
      std::lock_guard<std::mutex> lock(_storage->mutex);

      switch (_state) {
        case idle:
          assert(0 && "This state shouldn't be possible here");
          break;
        case running:
          _state = idle;
          on_expire = std::move(_on_expire);
          break;
        case canceling:
          _state = idle;
          break;
        case canceling_for_restart:
          _state = idle;
          start_locked(_timeout);
          break;
      }
    }

    if (on_expire) on_expire();
  }

  void start_locked(duration timeout) {
    switch (_state) {
      case idle:
        _state = running;
        _timer.expires_from_now(timeout);
        {
          auto storage = _storage;
          _timer.async_wait(
              [this, storage](error_code e) {
                if (storage->was_destroyed) return;
                on_timeout(e);
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

private:
  StoragePtr                _storage;
  State                     _state;
  boost::asio::steady_timer _timer;
  std::function<void ()>    _on_expire;
  duration                  _timeout;
};

} // async namespace

#endif // ifndef __ASYNC_ALARM_H__

