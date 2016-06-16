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

#ifndef __BINARY_IP_H__
#define __BINARY_IP_H__

#include <boost/asio/ip/udp.hpp>
#include <binary/encoder.h>
#include <binary/decoder.h>

namespace binary {

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, boost::asio::ip::address_v4 addr) {
  e.template put<uint32_t>(addr.to_ulong());
}

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, boost::asio::ip::address_v6 addr) {
  boost::asio::ip::address_v6::bytes_type bytes = addr.to_bytes();
  e.put_raw(bytes.data(), bytes.size());
}

//------------------------------------------------------------------------------
inline void decode(decoder& d, boost::asio::ip::address_v4& addr) {
  addr = boost::asio::ip::address_v4(d.get<std::uint32_t>());
}

//------------------------------------------------------------------------------
inline void decode(decoder& d, boost::asio::ip::address_v6& addr) {
  using boost::asio::ip::address_v6;
  address_v6::bytes_type bytes;
  d.get_raw(bytes.data(), bytes.size());
  addr = address_v6(bytes);
}

//------------------------------------------------------------------------------

} // binary namespace


#endif // ifndef __BINARY_IP_H__
