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
#include <transport/transport.h>

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
using UnreliableId     = uint64_t;
using TransmitQueue    = club::transport::TransmitQueue<UnreliableId>;
using OutboundMessages = club::transport::OutboundMessages<UnreliableId>;
using InboundMessages  = club::transport::InboundMessages<UnreliableId>;
using Transport        = club::transport::Transport<UnreliableId>;
using udp              = boost::asio::ip::udp;

namespace asio = boost::asio;

//------------------------------------------------------------------------------
vector<uint8_t> buf_to_vector(const_buffer buf) {
  auto p = boost::asio::buffer_cast<const uint8_t*>(buf);
  auto s = boost::asio::buffer_size(buf);
  return vector<uint8_t>(p, p + s);
}

//------------------------------------------------------------------------------
struct Node {
  uuid                              id;
  std::list<Transport>              transports;
  shared_ptr<OutboundMessages>      outbound;
  shared_ptr<InboundMessages>       inbound;
  std::function<void(const_buffer)> on_recv;

  void add_transport(udp::socket s, udp::endpoint e) {
    transports.emplace_back(id, move(s), e, outbound, inbound);
  }

  Node()
    : id(boost::uuids::random_generator()())
    , outbound(make_shared<OutboundMessages>(id))
    , inbound(make_shared<InboundMessages>
        ([this](auto b) { this->on_recv(b); }))
  {}
};

//------------------------------------------------------------------------------
void connect_nodes(asio::io_service& ios, Node& n1, Node& n2) {
  udp::socket s1(ios, udp::endpoint(udp::v4(), 0));
  udp::socket s2(ios, udp::endpoint(udp::v4(), 0));

  auto ep1 = s1.local_endpoint();
  auto ep2 = s2.local_endpoint();

  n1.add_transport(move(s1), move(ep2));

  n1.transports.back().add_target(n2.id);

  n2.add_transport(move(s2), move(ep1));

  n2.transports.back().add_target(n1.id);
}

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_transport_one_unreliable_message) {
  asio::io_service ios;

  Node n1, n2; 

  n2.on_recv = [&](auto b) {
    BOOST_REQUIRE(buf_to_vector(b) == vector<uint8_t>({0,1,2,3}));
    n1.transports.clear();
    n2.transports.clear();
  };

  connect_nodes(ios, n1, n2);

  n1.outbound->send_unreliable( 0
                              , vector<uint8_t>{0,1,2,3}
                              , set<uuid>{n2.id});

  ios.run();
}

//------------------------------------------------------------------------------
