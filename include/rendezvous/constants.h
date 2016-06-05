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

#ifndef __RENDEZVOUS_CONSTANTS_H__
#define __RENDEZVOUS_CONSTANTS_H__

#include <boost/asio/steady_timer.hpp>
#include <bitset>

namespace rendezvous {

using VersionType = uint16_t;

static const std::bitset<2> PLEX("10");
static const size_t         HEADER_SIZE      = 8;
static const size_t         MAX_PAYLOAD_SIZE = 1024 - HEADER_SIZE;
static const uint32_t       COOKIE           = 0x3223B553;

static const uint8_t METHOD_PING  = 0x00;
static const uint8_t METHOD_MATCH = 0x01;

static const uint8_t CLIENT_METHOD_FETCH = 0x00;
static const uint8_t CLIENT_METHOD_CLOSE = 0x01;

// Same as in the STUN RFC.
static const uint8_t IPV4_TAG = 0x01;
static const uint8_t IPV6_TAG = 0x02;

struct constants_v1 {
  static boost::asio::steady_timer::duration keepalive_duration() {
    return std::chrono::seconds(5);
  }
};

} // rendezvous namespace

#endif // ifndef __RENDEZVOUS_CONSTANTS_H__
