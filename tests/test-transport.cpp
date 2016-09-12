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

#include <boost/asio.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/functional/hash.hpp>
#include <transport/socket.h>
#include <debug/string_tools.h>
#include "when_all.h"
#include "async_loop.h"
#include "util/socket.h"

//------------------------------------------------------------------------------
// The transport tests that have the prefix 'test_transport_reliable*' should
// also be tested with packet dropping enabled (needs root permissions):
//
// Insert a packet dropping rule:
// iptables -I INPUT 1 -m statistic -p udp --mode random --probability 0.5 -j DROP
//
// Delete the rule:
// iptables -D INPUT 1
//
// List rules:
// iptables -L INPUT
//------------------------------------------------------------------------------

using std::cout;
using std::endl;
using std::move;
using std::shared_ptr;
using std::make_shared;
using std::set;
using std::vector;
using boost::system::error_code;
using boost::asio::const_buffer;

using uuid   = club::uuid;
using Socket = club::transport::Socket;
using SocketPtr = std::shared_ptr<Socket>;
using udp    = boost::asio::ip::udp;

namespace asio = boost::asio;

//------------------------------------------------------------------------------
namespace std {
std::ostream& operator<<(std::ostream& os, const vector<uint8_t>& v) {
  return os << str(v);
}
} // std namespace

//------------------------------------------------------------------------------
vector<uint8_t> buf_to_vector(const_buffer buf) {
  auto p = boost::asio::buffer_cast<const uint8_t*>(buf);
  auto s = boost::asio::buffer_size(buf);
  return vector<uint8_t>(p, p + s);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_message) {
  asio::io_service ios;

  make_connected_sockets(ios, [](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      s2->receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
        c();
      }));

      s1->send_unreliable(std::vector<uint8_t>{0,1,2,3});

      s1->flush(when_all.make_continuation());

      when_all.on_complete([s1, s2]() {
          s1->close();
          s2->close();
        });
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_big_message) {
  asio::io_service ios;

  vector<uint8_t> big_message(3*Socket::packet_size);

  for (size_t i = 0; i < big_message.size(); i++) {
    big_message[i] = i;
  }

  int counter = 0;

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      s2->receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
        ++counter;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), big_message);
        c();
      }));

      s1->send_unreliable(big_message);

      s1->flush(when_all.make_continuation());

      when_all.on_complete([&, s1, s2]() {
          ++counter;
          s1->close();
          s2->close();
        });
    });

  ios.run();

  BOOST_REQUIRE_EQUAL(counter, 2);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages) {
  asio::io_service ios;

  int test_count = 0;
  WhenAll when_all;

  make_connected_sockets(ios, [&test_count, &when_all](SocketPtr s1, SocketPtr s2) {
    auto c = when_all.make_continuation();

    s2->receive_unreliable([&, c, s2](auto err, auto b) {
      ++test_count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s2->receive_unreliable([&, c, s2](auto err, auto b) {
            ++test_count;
            BOOST_REQUIRE(!err);
            BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
            c();
          });
    });

    s1->send_unreliable(std::vector<uint8_t>{0,1,2,3});
    s1->send_unreliable(std::vector<uint8_t>{4,5,6,7});

    s1->flush(when_all.make_continuation());

    when_all.on_complete([s1, s2, &test_count]() {
        ++test_count;
        s1->close();
        s2->close();
      });
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 3);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_many_messages) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {
      const uint8_t N = 64;

      WhenAll when_all;

      auto on_n_receives = when_all.make_continuation();

      async_loop([=, &test_count](unsigned int i, auto cont) {
        if (i == N) {
          return on_n_receives();
        }

        s2->receive_unreliable([&, cont, i, s1, s2](auto err, auto b) {
          ++test_count;
          BOOST_REQUIRE(!err);
          BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({i}));
          cont();
        });
      });

      for (uint8_t i = 0; i < N; ++i) {
        s1->send_unreliable(std::vector<uint8_t>{i});
      }

      s1->flush(when_all.make_continuation());

      when_all.on_complete([s1, s2, &test_count]() {
          ++test_count;
          s1->close();
          s2->close();
        });
    });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 65);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages_causal) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      auto on_all_recv = when_all.make_continuation();
      auto on_flush = when_all.make_continuation();

      s2->receive_unreliable([=, &test_count](auto err, auto b) {
        ++test_count;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

        s1->send_unreliable(std::vector<uint8_t>{4,5,6,7});
        s1->flush(on_flush);

        s2->receive_unreliable([&, on_all_recv](auto err, auto b) {
          ++test_count;
          BOOST_REQUIRE(!err);
          BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
          on_all_recv();
        });
      });

      s1->send_unreliable(std::vector<uint8_t>{0,1,2,3});

      when_all.on_complete([s1, s2, &test_count]() {
          ++test_count;
          s1->close();
          s2->close();
        });
    });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 3);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_exchange) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {
    WhenAll when_all;

    s1->receive_unreliable(when_all.make_continuation([&, s1](auto c, auto err, auto b) {
      ++test_count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({2,3,4,5}));
      s1->flush(c);
    }));

    s2->receive_unreliable(when_all.make_continuation([&, s2](auto c, auto err, auto b) {
      ++test_count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
      s2->flush(c);
    }));

    s1->send_unreliable(std::vector<uint8_t>{0,1,2,3});
    s2->send_unreliable(std::vector<uint8_t>{2,3,4,5});

    when_all.on_complete([s1, s2, &test_count]() {
        ++test_count;
        s1->close();
        s2->close();
      });
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 3);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_one_message) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      s2->receive_reliable(when_all.make_continuation(
          [s2, &test_count]
          (auto c, auto err, auto b) {
            ++test_count;
            BOOST_REQUIRE(!err);
            BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
            c();
          }));

      s1->send_reliable(std::vector<uint8_t>{0,1,2,3});

      s1->flush(when_all.make_continuation());
      s2->flush(when_all.make_continuation());

      when_all.on_complete([s1, s2, &test_count]() {
          ++test_count;
          s1->close();
          s2->close();
        });
    });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 2);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {
    WhenAll when_all;

    auto when_all_recv = when_all.make_continuation();

    s2->receive_reliable([&test_count, when_all_recv, s2](auto err, auto b) {
      ++test_count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s2->receive_reliable([&test_count, s2, when_all_recv](auto err, auto b) {
        ++test_count;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
        s2->flush(when_all_recv);
      });
    });

    s1->send_reliable(std::vector<uint8_t>{0,1,2,3});
    s1->send_reliable(std::vector<uint8_t>{4,5,6,7});

    s1->flush(when_all.make_continuation());

    when_all.on_complete([s1, s2, &test_count]() {
        ++test_count;
        s1->close();
        s2->close();
      });
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(test_count, 3);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_big_messages) {
  asio::io_service ios;

  size_t N = 3;

  int test_count = 0;

  vector<uint8_t> message(2*Socket::packet_size);

  for (size_t i = 0; i < message.size(); ++i) {
    message[i] = i;
  }

  make_connected_sockets(ios, [&test_count, N, &message](SocketPtr s1, SocketPtr s2) {
    WhenAll when_all;

    auto when_all_recv = when_all.make_continuation();

    async_loop([&test_count, s2, N, &message, when_all_recv](auto i, auto cont) {
      ++test_count;
      if (i == N) {
        return s2->flush(when_all_recv);
      }

      s2->receive_reliable([&, cont, s2](auto err, auto b) {
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), message);
        cont();
      });
    });

    for (size_t i = 0; i < N; ++i) {
      s1->send_reliable(message);
    }

    s1->flush(when_all.make_continuation());

    when_all.on_complete([s1, s2, &test_count]() {
        ++test_count;
        s1->close();
        s2->close();
      });
  });

  ios.run();
  BOOST_REQUIRE_EQUAL(test_count, N + 2);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages_causal) {
  asio::io_service ios;

  int test_count = 0;

  make_connected_sockets(ios, [&test_count](SocketPtr s1, SocketPtr s2) {

    WhenAll when_all;

    auto on_s2_finish = when_all.make_continuation();
    auto on_s1_flush = when_all.make_continuation();

    s2->receive_reliable([=, &test_count](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s1->send_reliable(std::vector<uint8_t>{4,5,6,7});
      s1->flush(on_s1_flush);

      s2->receive_reliable([=, &test_count](auto err, auto b) {
        ++test_count;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
        s2->flush(on_s2_finish);
      });
    });

    s1->send_reliable(std::vector<uint8_t>{0,1,2,3});

    when_all.on_complete([=, &test_count]() {
        ++test_count;
        s1->close();
        s2->close();
      });
  });

  ios.run();
  BOOST_REQUIRE_EQUAL(test_count, 2);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_and_reliable) {
  asio::io_service ios;

  std::srand(std::time(0));

  constexpr uint8_t N = 64;
  size_t count = 0;

  make_connected_sockets(ios, [&count](SocketPtr s1, SocketPtr s2) {
    auto flush_then_close = [=]() {
      s1->flush([=] {
          s2->flush([s1,s2] {
              s1->close(); s2->close(); }); });
    };

    async_loop([=, &count](auto, auto cont) {
      s2->receive_reliable([cont, &count, s2, flush_then_close](auto err, auto b) {
        if (err == asio::error::operation_aborted) return;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0, count++}));
        if (count < N) cont();
        else flush_then_close();
      });
    });

    async_loop([=, &count](auto, auto cont) {
      s2->receive_unreliable([cont, &count, s1, flush_then_close](auto err, auto b) {
        if (err == asio::error::operation_aborted) return;
        BOOST_REQUIRE(!err);
        BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({1, count++}));
        if (count < N) cont();
        else flush_then_close();
      });
    });

    for (uint8_t i = 0; i < N; ++i) {
      if (std::rand() % 2) {
        s1->send_reliable(std::vector<uint8_t>{0, i});
      }
      else {
        s1->send_unreliable(std::vector<uint8_t>{1, i});
      }
    }
  });

  ios.run();
  BOOST_REQUIRE_EQUAL(count, 64);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_timeout) {
  asio::io_service ios;

  unsigned count = 0;

  make_connected_sockets(ios, [&count](SocketPtr s1, SocketPtr s2) {

    s1->receive_reliable([&, s1](auto err, auto) {
      ++count;
      BOOST_REQUIRE_EQUAL(err, club::transport::error::timed_out);
    });

    s2->receive_reliable([&, s2](auto err, auto b) {
      ++count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s2->receive_reliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
        });

      s2->receive_unreliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
        });

      s2->flush([s2] {
          // Close the udp::socket directly not to give the transport::socket
          // an opportunity to close gracefuly.
          s2->get_socket_impl().close();
        });
    });

    s1->send_reliable(std::vector<uint8_t>{0,1,2,3});
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(count, 4);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_close) {
  asio::io_service ios;

  unsigned count = 0;

  make_connected_sockets(ios, [&count](SocketPtr s1, SocketPtr s2) {
    s1->receive_reliable([&, s1](auto err, auto) {
      ++count;
      BOOST_REQUIRE_EQUAL(err, boost::asio::error::connection_reset);
    });

    s2->receive_reliable([&, s2](auto err, auto b) {
      ++count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s2->receive_reliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
        });

      s2->receive_unreliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
        });

      s2->flush([s2] {
          // Close gracefuly.
          s2->close();
        });
    });

    s1->send_reliable(std::vector<uint8_t>{0,1,2,3});
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(count, 4);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_keepalive) {
  asio::io_service ios;

  unsigned count = 0;
  asio::steady_timer timer(ios);

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
    s1->receive_reliable([&, s1](auto err, auto) {
      ++count;
      BOOST_REQUIRE_EQUAL(err, asio::error::operation_aborted);
    });

    s2->receive_reliable([&, s1, s2](auto err, auto b) {
      ++count;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      s2->receive_reliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, asio::error::operation_aborted);
        });

      s2->receive_unreliable([&, s2](auto err, auto b) {
          ++count;
          BOOST_REQUIRE_EQUAL(err, asio::error::operation_aborted);
        });

      s2->flush([s1, s2, &timer] {
          auto dmax = std::max( s1->recv_timeout_duration()
                              , s2->recv_timeout_duration());

          timer.expires_from_now(2*dmax);
          timer.async_wait([s1, s2](auto /*err*/) {
              s1->close();
              s2->close();
            });
        });
    });

    s1->send_reliable(std::vector<uint8_t>{0,1,2,3});
  });

  ios.run();

  BOOST_REQUIRE_EQUAL(count, 4);
}

