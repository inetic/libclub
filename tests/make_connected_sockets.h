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

#ifndef __MAKE_CONNECTED_SOCKETS_H__
#define __MAKE_CONNECTED_SOCKETS_H__

#include <boost/asio.hpp>
#include "net/ConnectedSocket.h"
#include "when_all.h"

// -------------------------------------------------------------------
inline boost::asio::ip::udp::endpoint
unspecified_to_loopback(boost::asio::ip::udp::endpoint e) {
  namespace ip = boost::asio::ip;

  if (e.address().is_unspecified()) {
    return ip::udp::endpoint(ip::address_v4::loopback(), e.port());
  }

  return e;
}

// -------------------------------------------------------------------
template<class H> void make_connected_sockets( boost::asio::io_service& ios
                                             , H handler) {
  using namespace net;
  using udp = boost::asio::ip::udp;
  using boost::system::error_code;
  using std::make_shared;
  using std::move;

  auto s1 = make_shared<ConnectedSocket>(ios, 0);
  auto s2 = make_shared<ConnectedSocket>(ios, 0);

  WhenAll on_connect;

  auto on_connect1 = on_connect.make_continuation();
  s1->async_p2p_connect( 1000
                       , unspecified_to_loopback(s2->local_endpoint())
                       , s2->local_endpoint()
                       , [=](error_code e, udp::endpoint, udp::endpoint) {
                         BOOST_CHECK_MESSAGE(!e, e.message());
                         on_connect1();
                       });

  auto on_connect2 = on_connect.make_continuation();
  s2->async_p2p_connect( 1000
                       , unspecified_to_loopback(s1->local_endpoint())
                       , s1->local_endpoint()
                       , [=](error_code e, udp::endpoint, udp::endpoint) {
                         BOOST_CHECK_MESSAGE(!e, e.message());
                         on_connect2();
                       });

  on_connect.on_complete([=]() { handler(move(s1), move(s2)); });
}

// -------------------------------------------------------------------
template<class H> void make_n_connected_socket_pairs
                         ( boost::asio::io_service& ios
                         , size_t n
                         , H handler) {
  using namespace net;
  using SocketPtr = std::shared_ptr<ConnectedSocket>;
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

// -------------------------------------------------------------------
template<class H> void make_network( boost::asio::io_service& ios
                                   , size_t node_count
                                   , H handler) {
  using namespace net;
  using SocketPtr = std::shared_ptr<ConnectedSocket>;
  using udp = boost::asio::ip::udp;
  using boost::system::error_code;
  using std::make_shared;
  using std::vector;

  auto network = make_shared<vector<vector<SocketPtr>>>(node_count);

  for (size_t i = 0; i < node_count; ++i) {
    for (size_t j = 0; j < node_count - 1; ++j) {
      (*network)[i].push_back(make_shared<ConnectedSocket>(ios, 0));
    }
  }

  WhenAll when_all;

  // If node_count == 2    If node_count == 4
  //
  //    j                    j
  //  0 | 1,0 0,0          2 | 3,0 3,1 3,2 2,2
  //    +--------- i       1 | 2,0 2,1 1,1 1,2
  //      0   1            0 | 1,0 0,0 0,1 0,2
  //                         +---------------- i
  //                           0   1   2   3

  for (size_t i = 0; i < node_count; ++i) {
    for (size_t j = 0; j < node_count - 1; ++j) {
      size_t shift_i = i > j ? 0 : 1;
      size_t shift_j = i > j ? 1 : 0;

      auto on_connect = when_all.make_continuation();

      auto& my_socket    = (*network)[i]          [j];
      auto& other_socket = (*network)[j + shift_i][i - shift_j];

      my_socket->async_p2p_connect
          ( 5000
          , unspecified_to_loopback(other_socket->local_endpoint())
          , other_socket->local_endpoint()
          , [=](error_code e, udp::endpoint, udp::endpoint) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              on_connect();
          });
    }
  }

  when_all.on_complete([=]() { handler(std::move(*network)); });
}

// -------------------------------------------------------------------

#endif // ifndef __MAKE_CONNECTED_SOCKETS_H__
