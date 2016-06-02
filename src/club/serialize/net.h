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

#ifndef __CLUB_BINARY_SERIALIZE_NET_H__
#define __CLUB_BINARY_SERIALIZE_NET_H__

#include "binary/decoder.h"
#include "binary/ip.h"
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>

namespace binary {

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, boost::asio::ip::address addr) {
  using namespace boost;

  ASSERT(addr.is_v6() || addr.is_v4());

  std::uint8_t addr_version = addr.is_v6() ? 6 : 4;

  e.template put((uint8_t) addr_version);

  if (addr_version == 4) {
    e.template put(addr.to_v4());
  }
  else {
    e.template put(addr.to_v6());
  }
}

//------------------------------------------------------------------------------
inline void decode(decoder& d, boost::asio::ip::address& addr) {
  using namespace boost;
  namespace ip = boost::asio::ip;

  auto addr_version = d.get<uint8_t>();

  if (d.error()) return;

  switch (addr_version) {
    case 4:  addr = d.get<ip::address_v4>();
             break;
    case 6:  addr = d.get<ip::address_v6>();
             break;
    default: d.set_error();
  }
}

//------------------------------------------------------------------------------
template<typename Encoder>
  inline void encode(Encoder& e, boost::asio::ip::udp::endpoint ep) {
    using namespace boost;

    e.template put((uint16_t)ep.port());
    e.template put(ep.address());
  }

//------------------------------------------------------------------------------
inline void decode(decoder& d, boost::asio::ip::udp::endpoint& ep) {
  namespace ip = boost::asio::ip;

  auto port = d.get<std::uint16_t>();
  auto addr = d.get<ip::address>();

  ep = ip::udp::endpoint(addr, port);
}

} // binary namespace

#endif // ifndef __CLUB_BINARY_SERIALIZE_NET_H__

