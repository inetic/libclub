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
#define BOOST_TEST_MODULE rendezvous_server_test
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <rendezvous/client.h>
#include "server.h"

namespace ip = boost::asio::ip;
using udp = boost::asio::ip::udp;
using std::cout;
using std::cerr;
using std::endl;
using std::move;
using Error = boost::system::error_code;
using std::unique_ptr;

BOOST_AUTO_TEST_CASE(two_agents) {
  boost::asio::io_service ios;

  uint32_t service_number = 0;

  rendezvous::options options;
  options.port(0);
  unique_ptr<rendezvous::server> server(new rendezvous::server(ios, options));

  udp::endpoint server_ep( ip::address_v4::from_string("127.0.0.1")
                         , server->local_endpoint().port());

  size_t count = 2;

  udp::socket socket1(ios, udp::endpoint(udp::v4(), 0));
  udp::socket socket2(ios, udp::endpoint(udp::v4(), 0));

  auto port1 = socket1.local_endpoint().port();
  auto port2 = socket2.local_endpoint().port();

  rendezvous::client client1( service_number
                            , move(socket1)
                            , server_ep
                            , [&](Error er, udp::socket s, udp::endpoint ep) {
                              BOOST_CHECK(!er);
                              BOOST_CHECK_EQUAL(ep.port(), port2);
                              if (--count == 0) {
                                server.reset();
                              }
                            });

  rendezvous::client client2( service_number
                            , move(socket2)
                            , server_ep
                            , [&](Error er, udp::socket s, udp::endpoint ep) {
                              BOOST_CHECK(!er);
                              BOOST_CHECK_EQUAL(ep.port(), port1);
                              if (--count == 0) {
                                server.reset();
                              }
                            });

  ios.run();

  BOOST_CHECK_EQUAL(count, 0);
}

