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
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_message) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  s2.receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
    c();
  }));

  s1.send_unreliable(std::vector<uint8_t>{0,1,2,3});

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_big_message) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  vector<uint8_t> big_message(3*Socket::packet_size);

  for (size_t i = 0; i < big_message.size(); i++) {
    big_message[i] = i;
  }

  s2.receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), big_message);
    c();
  }));

  s1.send_unreliable(big_message);

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  s2.receive_unreliable([&](auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s2.receive_unreliable(
        when_all.make_continuation([&](auto c, auto err, auto b) {
          BOOST_REQUIRE(!err);
          BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
          c();
        }));
  });

  s1.send_unreliable(std::vector<uint8_t>{0,1,2,3});
  s1.send_unreliable(std::vector<uint8_t>{4,5,6,7});

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_many_messages) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  const uint8_t N = 64;

  auto on_n_receives = when_all.make_continuation();

  async_loop([&](unsigned int i, auto cont) {
    if (i == N) {
      return on_n_receives();
    }

    s2.receive_unreliable([&, cont, i](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({i}));
      cont();
    });
  });

  for (uint8_t i = 0; i < N; ++i) {
    s1.send_unreliable(std::vector<uint8_t>{i});
  }

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages_causal) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  auto on_all_recv = when_all.make_continuation();

  s2.receive_unreliable([&](auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s1.send_unreliable(std::vector<uint8_t>{4,5,6,7});
    s1.flush(when_all.make_continuation());

    s2.receive_unreliable([&](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
      on_all_recv();
    });
  });

  s1.send_unreliable(std::vector<uint8_t>{0,1,2,3});

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_exchange) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  s1.receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({2,3,4,5}));
    s1.flush(c);
  }));

  s2.receive_unreliable(when_all.make_continuation([&](auto c, auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
    s2.flush(c);
  }));

  s1.send_unreliable(std::vector<uint8_t>{0,1,2,3});
  s2.send_unreliable(std::vector<uint8_t>{2,3,4,5});

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_one_message) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  s2.receive_reliable(when_all.make_continuation([&](auto c, auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
    c();
  }));

  s1.send_reliable(std::vector<uint8_t>{0,1,2,3});

  s1.flush(when_all.make_continuation());
  s2.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  auto when_all_recv = when_all.make_continuation();

  s2.receive_reliable([&](auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s2.receive_reliable([&](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
      s2.flush(when_all_recv);
    });
  });

  s1.send_reliable(std::vector<uint8_t>{0,1,2,3});
  s1.send_reliable(std::vector<uint8_t>{4,5,6,7});

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_big_messages) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  size_t N = 3;

  WhenAll when_all;

  vector<uint8_t> message(2*Socket::packet_size);

  for (size_t i = 0; i < message.size(); ++i) {
    message[i] = i;
  }

  auto when_all_recv = when_all.make_continuation();

  async_loop([&](auto i, auto cont) {
    if (i == N) {
      return s2.flush(when_all_recv);
    }

    s2.receive_reliable([&, cont](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), message);
      cont();
    });
  });

  for (size_t i = 0; i < N; ++i) {
    s1.send_reliable(message);
  }

  s1.flush(when_all.make_continuation());

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages_causal) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  WhenAll when_all;

  auto on_s2_finish = when_all.make_continuation();

  s2.receive_reliable([&](auto err, auto b) {
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s1.send_reliable(std::vector<uint8_t>{4,5,6,7});
    s1.flush(when_all.make_continuation());

    s2.receive_reliable([&](auto err, auto b) {
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
      s2.flush(on_s2_finish);
    });
  });

  s1.send_reliable(std::vector<uint8_t>{0,1,2,3});

  when_all.on_complete([&]() {
      s1.close();
      s2.close();
    });

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_and_reliable) {
  asio::io_service ios;

  std::srand(std::time(0));

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  constexpr uint8_t N = 64;
  size_t count = 0;

  auto flush_then_close = [&]() {
    s1.flush([&] {
        s2.flush([&] {
            s1.close(); s2.close(); }); });
  };

  async_loop([&](auto, auto cont) {
    s2.receive_reliable([&, cont](auto err, auto b) {
      if (err == asio::error::operation_aborted) return;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0, count++}));
      if (count < N) cont();
      else flush_then_close();
    });
  });

  async_loop([&](auto, auto cont) {
    s2.receive_unreliable([&, cont](auto err, auto b) {
      if (err == asio::error::operation_aborted) return;
      BOOST_REQUIRE(!err);
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({1, count++}));
      if (count < N) cont();
      else flush_then_close();
    });
  });

  for (uint8_t i = 0; i < N; ++i) {
    if (std::rand() % 2) {
      s1.send_reliable(std::vector<uint8_t>{0, i});
    }
    else {
      s1.send_unreliable(std::vector<uint8_t>{1, i});
    }
  }

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_timeout) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  unsigned count = 0;

  s1.receive_reliable([&](auto err, auto) {
    ++count;
    BOOST_REQUIRE_EQUAL(err, club::transport::error::timed_out);
  });

  s2.receive_reliable([&](auto err, auto b) {
    ++count;
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s2.receive_reliable([&](auto err, auto b) {
        ++count;
        BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
      });

    s2.receive_unreliable([&](auto err, auto b) {
        ++count;
        BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
      });

    s2.flush([&s2] {
        // Close the udp::socket directly not to give the transport::socket
        // an opportunity to close gracefuly.
        s2.get_socket_impl().close();
      });
  });

  s1.send_reliable(std::vector<uint8_t>{0,1,2,3});

  ios.run();

  BOOST_REQUIRE_EQUAL(count, 4);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_close) {
  asio::io_service ios;

  Socket s1(ios), s2(ios);

  s1.rendezvous_connect(s2.local_endpoint());
  s2.rendezvous_connect(s1.local_endpoint());

  unsigned count = 0;

  s1.receive_reliable([&](auto err, auto) {
    ++count;
    BOOST_REQUIRE_EQUAL(err, boost::asio::error::connection_reset);
  });

  s2.receive_reliable([&](auto err, auto b) {
    ++count;
    BOOST_REQUIRE(!err);
    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

    s2.receive_reliable([&](auto err, auto b) {
        ++count;
        BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
      });

    s2.receive_unreliable([&](auto err, auto b) {
        ++count;
        BOOST_REQUIRE_EQUAL(err, boost::asio::error::operation_aborted);
      });

    s2.flush([&s2] {
        // Close gracefuly.
        s2.close();
      });
  });

  s1.send_reliable(std::vector<uint8_t>{0,1,2,3});

  ios.run();

  BOOST_REQUIRE_EQUAL(count, 4);
}

