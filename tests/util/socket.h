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

#ifndef TEST_UTIL_SOCKET_H
#define TEST_UTIL_SOCKET_H

#include <club/socket.h>
#include "../when_all.h"

// -------------------------------------------------------------------
template<class Handler>
void make_connected_sockets(boost::asio::io_service& ios, Handler handler) {
  using std::move;
  using Socket = club::Socket;

  auto s1 = std::make_shared<Socket>(ios);
  auto s2 = std::make_shared<Socket>(ios);

  WhenAll when_all;

  s1->rendezvous_connect(s2->local_endpoint(), when_all.make_continuation(
      [](auto c, auto error) {
        BOOST_REQUIRE(!error);
        c();
      }));

  s2->rendezvous_connect(s1->local_endpoint(), when_all.make_continuation(
      [](auto c, auto error) {
        BOOST_REQUIRE(!error);
        c();
      }));

  when_all.on_complete([ handler = move(handler)
                       , s1 = move(s1)
                       , s2 = move(s2)]() {
      handler(move(s1), move(s2));
    });
}

// -------------------------------------------------------------------
template<class H> void make_n_connected_socket_pairs
                         ( boost::asio::io_service& ios
                         , size_t n
                         , H handler) {
  using Socket = club::Socket;
  using SocketPtr = std::shared_ptr<Socket>;
  using std::vector;
  using std::pair;
  using std::make_pair;
  using std::move;
  using std::make_shared;

  auto pairs = make_shared<vector<pair<SocketPtr, SocketPtr>>>();

  async_loop([=, &ios](unsigned int i, Cont cont) {
      if (i == n) return handler(move(*pairs));

      make_connected_sockets(ios, [=](SocketPtr s1, SocketPtr s2) {
          pairs->push_back(make_pair(move(s1), move(s2)));
          cont();
        });
  });
}

#endif // ifndef TEST_UTIL_SOCKET_H

