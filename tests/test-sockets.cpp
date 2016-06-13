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

#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include <iostream>
#include "club/hub.h"
#include "when_all.h"
#include "async_loop.h"
#include "make_connected_sockets.h"

using namespace net;
namespace asio = boost::asio;
using std::cout;
using std::make_shared;
using std::unique_ptr;
using std::shared_ptr;
using boost::system::error_code;
using boost::asio::io_service;
using udp = boost::asio::ip::udp;

// -------------------------------------------------------------------
std::vector<uint8_t> generate_buffer(unsigned int seed, size_t size) {
  std::vector<uint8_t> v(size);
  for (size_t i = 0; i < v.size(); ++i) { v[i] = i + seed; }
  return v;
}

// -------------------------------------------------------------------
// Mostly test that the ios.run() call terminates when sockets
// are explicitly closed.
BOOST_AUTO_TEST_CASE(sockets_explicit_close) {
  using SocketPtr = shared_ptr<ConnectedSocket>;

  io_service ios;

  bool did_connect = false;

  SocketPtr s1;
  SocketPtr s2;

  make_connected_sockets(ios, [&](SocketPtr s1_, SocketPtr s2_) {
    did_connect = true;
    s1 = s1_;
    s2 = s2_;
    s1->close();
    s2->close();
  });

  ios.run();

  BOOST_REQUIRE(did_connect);
}

// -------------------------------------------------------------------
// Mostly test that the ios.run() call terminates when sockets
// are explicitly closed.
BOOST_AUTO_TEST_CASE(sockets_destruct) {
  using SocketPtr = shared_ptr<ConnectedSocket>;

  io_service ios;

  bool did_connect = false;

  make_connected_sockets(ios, [&](SocketPtr, SocketPtr) {
    did_connect = true;
    // Sockets get destructed here implicitly.
  });

  ios.run();

  BOOST_REQUIRE(did_connect);
}

// -------------------------------------------------------------------
// Io ios.run() should unblock when there is no more work,
// even if the sockets were not destroyed nor closed.
#if 0 // Currently halts
BOOST_AUTO_TEST_CASE(sockets_no_work) {
  using SocketPtr = shared_ptr<ConnectedSocket>;

  io_service ios;

  bool did_connect = false;

  SocketPtr s1;
  SocketPtr s2;

  make_connected_sockets(ios, [&](SocketPtr s1_, SocketPtr s2_) {
    did_connect = true;
    s1 = s1_;
    s2 = s2_;
  });

  ios.run();

  BOOST_REQUIRE(did_connect);
}
#endif

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sockets_send_receive) {
  using SocketPtr = shared_ptr<ConnectedSocket>;

  io_service ios;

  bool did_connect = false;

  std::vector<uint8_t> tx_bytes = generate_buffer(0, 1024);
  std::vector<uint8_t> rx_bytes(tx_bytes.size());

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      auto on_send = when_all.make_continuation();
      s1->async_send( asio::buffer(tx_bytes)
                    , 1000
                    , [s1, on_send](error_code e) {
                      BOOST_CHECK_MESSAGE(!e, e.message());
                      on_send();
                    });

      auto on_receive = when_all.make_continuation();
      s2->async_receive( asio::buffer(rx_bytes)
                       , 1000
                       , [s2, on_receive, &rx_bytes, &tx_bytes]
                         (error_code e, size_t) {
                           BOOST_CHECK_MESSAGE(!e, e.message());
                           BOOST_CHECK(rx_bytes == tx_bytes);
                           on_receive();
                       });

      when_all.on_complete([&did_connect]() {
          did_connect = true;
          });
  });

  ios.run();

  BOOST_REQUIRE(did_connect);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sockets_send_receive_N_times) {
  using SocketPtr = shared_ptr<ConnectedSocket>;

  io_service ios;

  bool did_connect = false;

  const size_t buf_size = 1024;

  std::vector<uint8_t> tx_bytes(buf_size);
  std::vector<uint8_t> rx_bytes(buf_size);

  const unsigned int N = 100; // Made up number

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      auto on_send = when_all.make_continuation();
      async_loop([&tx_bytes, s1, on_send] (unsigned int i, Cont cont) {
          if (i == N) { return on_send(); }

          tx_bytes = generate_buffer(i, buf_size);

          s1->async_send( asio::buffer(tx_bytes)
                        , 1000
                        , [s1, cont](error_code e) {
                          BOOST_CHECK_MESSAGE(!e, e.message());
                          cont();
                        });
          });

      auto on_receive = when_all.make_continuation();
      async_loop([&rx_bytes, &tx_bytes, s2, on_receive]
                 (unsigned int i, Cont cont) {
          if (i == N) { return on_receive(); }

          s2->async_receive( asio::buffer(rx_bytes)
                           , 1000
                           , [i, s2, cont, &rx_bytes, &tx_bytes]
                             (error_code e, size_t size) {
                               BOOST_CHECK_EQUAL(buf_size, size);
                               BOOST_CHECK_MESSAGE(!e, e.message());
                               auto expected = generate_buffer(i, buf_size);
                               BOOST_CHECK(rx_bytes == expected);
                               cont();
                           });
          });

      when_all.on_complete([&did_connect]() {
          did_connect = true;
          });
  });

  ios.run();

  BOOST_REQUIRE(did_connect);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sockets_exchange) {
  using SocketPtr = shared_ptr<ConnectedSocket>;
  using Bytes = std::vector<uint8_t>;

  io_service ios;

  bool did_exchange = false;


  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      for (auto& s : {s1, s2}) {
        const auto tx_bytes = make_shared<Bytes>(generate_buffer(0, 1024));
        auto       rx_bytes = make_shared<Bytes>(tx_bytes->size());

        auto on_send = when_all.make_continuation();
        s->async_send( asio::buffer(*tx_bytes)
                     , 1000
                     , [s, on_send, tx_bytes](error_code e) {
                       BOOST_CHECK_MESSAGE(!e, e.message());
                       on_send();
                     });

        auto on_receive = when_all.make_continuation();
        s->async_receive( asio::buffer(*rx_bytes)
                        , 1000
                        , [s, on_receive, rx_bytes, tx_bytes]
                          (error_code e, size_t size) {
                            BOOST_CHECK_EQUAL(size, tx_bytes->size());
                            BOOST_CHECK_MESSAGE(!e, e.message());
                            BOOST_CHECK(*rx_bytes == *tx_bytes);
                            on_receive();
                        });
      }

      // Capturing s1 and s2 to prevent sending Close packets
      // before everything finishes.
      when_all.on_complete([&did_exchange, s1, s2]() {
          did_exchange = true;
          });
  });

  ios.run();

  BOOST_REQUIRE(did_exchange);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sockets_exchange_N_times) {
  using SocketPtr = shared_ptr<ConnectedSocket>;
  using std::vector;
  using Bytes = vector<uint8_t>;

  io_service ios;

  bool did_exchange = false;

  const size_t buf_size = 1024;

  const unsigned int N = 100; // Made up number

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      for (auto& s : {s1, s2}) {
        auto tx_bytes = make_shared<Bytes>(buf_size);
        auto rx_bytes = make_shared<Bytes>(buf_size);

        auto on_send = when_all.make_continuation();
        async_loop([s, on_send, tx_bytes](unsigned int i, Cont cont) {
            if (i == N) return on_send();

            *tx_bytes = generate_buffer(i, buf_size);

            s->async_send( asio::buffer(*tx_bytes)
                         , 1000
                         , [s, tx_bytes, cont](error_code e) {
                           BOOST_CHECK_MESSAGE(!e, e.message());
                           cont();
                         });
          });

        auto on_receive = when_all.make_continuation();
        async_loop([s, on_receive, rx_bytes]
                   (unsigned int i, Cont cont) {
            if (i == N) return on_receive();

            s->async_receive( asio::buffer(*rx_bytes)
                            , 1000
                            , [i, s, cont, rx_bytes]
                              (error_code e, size_t size) {
                                auto tx_bytes = generate_buffer(i, buf_size);
                                BOOST_CHECK_EQUAL(size, buf_size);
                                BOOST_CHECK_MESSAGE(!e, e.message());
                                BOOST_CHECK(*rx_bytes == tx_bytes);
                                cont();
                            });
          });
      }

      // Sockets s1 and s2 are captured here to prevent
      // sending Close packet before Ack is sent.
      when_all.on_complete([&did_exchange, s1, s2]() {
          did_exchange = true;
          });
  });

  ios.run();

  BOOST_REQUIRE(did_exchange);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sockets_make_network) {
  using SocketPtr = shared_ptr<ConnectedSocket>;
  using std::vector;

  size_t sizes[] = {2, 3, 5, 9, 21, 32};

  for (auto size: sizes) {
    io_service ios;

    bool finished = false;

    make_network(ios, size, [&finished](vector<vector<SocketPtr>>) {
        finished = true;
    });

    ios.run();

    BOOST_REQUIRE(finished);
  }
}

// -------------------------------------------------------------------
// Same as before, but run the ios.run() function only onces. The
// networks are created one after another.
#if 0 // Currently fails with "Too many open files"
BOOST_AUTO_TEST_CASE(sockets_make_networks) {
  using SocketPtr = shared_ptr<ConnectedSocket>;
  using std::vector;

  io_service ios;

  bool finished = false;

  size_t min = 2;
  size_t max = 20;

  async_loop([&](unsigned int i, Cont cont) {
      if (i+min == max) {
        finished = true;
        return;
      }

      make_network(ios, i+min, [=](vector<vector<SocketPtr>> network) {
          cont();
      });
    });

  ios.run();

  BOOST_REQUIRE(finished);
}
#endif

// -------------------------------------------------------------------
