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

#ifndef WHEN_ALL_H
#define WHEN_ALL_H

class WhenAll {
  using Handler = std::function<void()>;

public:
  template<class H> WhenAll(H handler)
    : _instance_count(new size_t(0))
    , _handler(std::make_shared<Handler>(std::move(handler)))
  {}

  WhenAll()
    : _instance_count(new size_t(0))
    , _handler(std::make_shared<Handler>())
  {}

  WhenAll(const WhenAll&) = delete;
  void operator=(const WhenAll&) = delete;

  std::function<void()> make_continuation() {
    auto handler = _handler;
    auto count   = _instance_count;

    ++*count;

    return [handler, count]() {
      if (--*count == 0) {
        (*handler)();
      }
    };
  }

  template<class F> auto make_continuation(F f) {
    using std::move;
    auto c = make_continuation();
    return [c = move(c), f = move(f)](auto ...args) {
             f(c, std::forward<decltype(args)>(args)...);
           };
  }

  template<class F> void on_complete(F on_complete) {
    *_handler = std::move(on_complete);
  }

private:
  std::shared_ptr<size_t>  _instance_count;
  std::shared_ptr<Handler> _handler;
};

#endif // ifndef WHEN_ALL_H
