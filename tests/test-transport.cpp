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
#include <transport/transport.h>
#include <debug/string_tools.h>

//------------------------------------------------------------------------------
// The transport tests that have the prefix 'test_transport_reliable*' should
// be tested with packet dropping enabled (needs root permissions):
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

using uuid             = club::uuid;
using UnreliableId     = uint32_t;
using TransmitQueue    = club::transport::TransmitQueue<UnreliableId>;
using OutboundMessages = club::transport::OutboundMessages<UnreliableId>;
using InboundMessages  = club::transport::InboundMessages<UnreliableId>;
using Transport        = club::transport::Transport<UnreliableId>;
using udp              = boost::asio::ip::udp;

using TransportPtr = std::unique_ptr<Transport>;

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
struct Node {
  uuid                                    id;
  std::map<uuid, TransportPtr>            transports;
  shared_ptr<OutboundMessages>            outbound;
  shared_ptr<InboundMessages>             inbound;
  std::function<void(uuid, const_buffer)> on_recv;

  void add_transport(uuid other_id, udp::socket s, udp::endpoint e) {
    auto t = std::make_unique<Transport>(id, move(s), e, outbound, inbound);
    transports.emplace(std::make_pair(other_id, move(t)));
    transports[other_id]->add_target(other_id);
  }

  void send_unreliable(vector<uint8_t> data, set<uuid> targets) {
    auto data_id = boost::hash_value(data);

    outbound->send_unreliable( data_id
                             , move(data)
                             , move(targets));
  }

  void send_reliable(vector<uint8_t> data, set<uuid> targets) {
    outbound->send_reliable(move(data), move(targets));
  }

  Node()
    : id(boost::uuids::random_generator()())
    , outbound(make_shared<OutboundMessages>(id))
    , inbound(make_shared<InboundMessages>
        ([this](auto s, auto b) { this->on_recv(s, b); }))
  {}
};

//------------------------------------------------------------------------------
void connect_nodes(asio::io_service& ios, Node& n1, Node& n2) {
  udp::socket s1(ios, udp::endpoint(udp::v4(), 0));
  udp::socket s2(ios, udp::endpoint(udp::v4(), 0));

  auto ep1 = s1.local_endpoint();
  auto ep2 = s2.local_endpoint();

  n1.add_transport(n2.id, move(s1), move(ep2));
  n2.add_transport(n1.id, move(s2), move(ep1));
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_message) {
  asio::io_service ios;

  Node n1, n2;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));
    n1.transports.clear();
    n2.transports.clear();
  };

  connect_nodes(ios, n1, n2);

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages) {
  asio::io_service ios;

  Node n1, n2;

  size_t counter = 0;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);

    if (counter++ == 0) {
      BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));
    }
    else {
      BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({4,5,6,7}));
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  connect_nodes(ios, n1, n2);

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});
  n1.send_unreliable(std::vector<uint8_t>{4,5,6,7}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_messages_causal) {
  asio::io_service ios;

  Node n1, n2;

  size_t counter = 0;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);

    if (counter++ == 0) {
      BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));
      n1.send_unreliable(std::vector<uint8_t>{4,5,6,7}, set<uuid>{n2.id});
    }
    else {
      BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({4,5,6,7}));
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  connect_nodes(ios, n1, n2);

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_exchange) {
  asio::io_service ios;

  Node n1, n2;

  connect_nodes(ios, n1, n2);

  size_t counter = 0;

  n1.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n2.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({2,3,4,5}));

    if (counter++ == 1) {
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));

    if (counter++ == 1) {
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});
  n2.send_unreliable(std::vector<uint8_t>{2,3,4,5}, set<uuid>{n1.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_forward_one_hop) {
  asio::io_service ios;

  Node n1, n2, n3;

  connect_nodes(ios, n1, n2);
  connect_nodes(ios, n2, n3);

  // Tell n1 that it can reach n3 through n2.
  n1.transports[n2.id]->add_target(n3.id);

  n3.on_recv = [&](auto, auto b) {
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));

    n1.transports.clear();
    n2.transports.clear();
    n3.transports.clear();
  };

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n3.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_forward_two_hops) {
  asio::io_service ios;

  Node n1, n2, n3, n4;

  connect_nodes(ios, n1, n2);
  connect_nodes(ios, n2, n3);
  connect_nodes(ios, n3, n4);

  // Setup routing tables
  n1.transports[n2.id]->add_target(n3.id);
  n1.transports[n2.id]->add_target(n4.id);
  n2.transports[n3.id]->add_target(n4.id);

  n4.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));

    n1.transports.clear();
    n2.transports.clear();
    n3.transports.clear();
    n4.transports.clear();
  };

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n4.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_two_targets) {
  asio::io_service ios;

  Node n1, n2, n3;

  connect_nodes(ios, n1, n2);
  connect_nodes(ios, n1, n3);

  size_t counter = 0;

  auto on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));

    if (++counter == 2) {
      n1.transports.clear();
      n2.transports.clear();
      n3.transports.clear();
    }
  };

  n2.on_recv = on_recv;
  n3.on_recv = on_recv;

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id, n3.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_unreliable_one_hop_two_targets) {
  asio::io_service ios;

  Node n1, n2, n3, n4;

  connect_nodes(ios, n1, n2);
  connect_nodes(ios, n2, n3);
  connect_nodes(ios, n2, n4);

  // Setup routing tables
  n1.transports[n2.id]->add_target(n3.id);
  n1.transports[n2.id]->add_target(n4.id);

  size_t counter = 0;

  auto on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));

    if (++counter == 2) {
      n1.transports.clear();
      n2.transports.clear();
      n3.transports.clear();
      n4.transports.clear();
    }
  };

  n3.on_recv = on_recv;
  n4.on_recv = on_recv;

  n1.send_unreliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n3.id, n4.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_one_message) {
  asio::io_service ios;

  Node n1, n2;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));
    n1.transports.clear();
    n2.transports.clear();
  };

  connect_nodes(ios, n1, n2);

  n1.send_reliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages) {
  asio::io_service ios;

  Node n1, n2;

  size_t counter = 0;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);

    if (counter++ == 0) {
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
    }
    else {
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  connect_nodes(ios, n1, n2);

  n1.send_reliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});
  n1.send_reliable(std::vector<uint8_t>{4,5,6,7}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_two_messages_causal) {
  asio::io_service ios;

  Node n1, n2;

  size_t counter = 0;

  n2.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);

    if (counter++ == 0) {
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));

      n1.send_reliable(std::vector<uint8_t>{4,5,6,7}, set<uuid>{n2.id});
    }
    else {
      BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({4,5,6,7}));
      n1.transports.clear();
      n2.transports.clear();
    }
  };

  connect_nodes(ios, n1, n2);

  n1.send_reliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_reliable_one_hop) {
  asio::io_service ios;

  Node n1, n2, n3;

  n3.on_recv = [&](auto s, auto b) {
    BOOST_REQUIRE(s == n1.id);

    BOOST_REQUIRE_EQUAL(buf_to_vector(b), vector<uint8_t>({0,1,2,3}));
    n1.transports.clear();
    n2.transports.clear();
    n3.transports.clear();
  };

  connect_nodes(ios, n1, n2);
  connect_nodes(ios, n2, n3);

  n1.transports[n2.id]->add_target(n3.id);

  n1.send_reliable(std::vector<uint8_t>{0,1,2,3}, set<uuid>{n3.id});

  ios.run();
}

//------------------------------------------------------------------------------
