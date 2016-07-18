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

#ifndef CLUB_NET_HEADER_H
#define CLUB_NET_HEADER_H

#include <array>
#include <ostream>
#include "club/channel.h"
#include "debug/ASSERT.h"

namespace club {

struct Header
{
  typedef uint32_t IDType;

  // The type used to represent a Header as an array of bytes.
  typedef std::array<char, 8> bytes_type;

  Header()
    : _id(0), _is_ack(false), _needs_ack(false) {}

  Header(Channel channel, uint32_t id, bool is_ack, bool needs_ack)
    : _channel        (channel)
    , _id             (id)
    , _is_ack         (is_ack)
    , _needs_ack      (needs_ack)
  {
    ASSERT(!(_is_ack && _needs_ack));
  }

  bytes_type to_bytes() const {
    bytes_type bytes;

    bytes[0] = _channel.type();
    bytes[1] = _channel.is_internal();
    bytes[2] = _is_ack;
    bytes[3] = _needs_ack;
    *((uint32_t*) &bytes[4]) = _id;

    return bytes;
  }

  void from_bytes(const bytes_type& bytes) {
    _channel.type(bytes[0]);
    _channel.is_internal(bytes[1]);
    _is_ack          = bytes[2];
    _needs_ack       = bytes[3];
    _id              = *((uint32_t*) &bytes[4]);
  }

  const Channel& channel() const { return _channel; }
  void channel(const Channel& channel) { _channel = channel; }

  IDType id() const { return _id; }

  bool is_ack() const { return _is_ack; }
  bool needs_ack() const { return _needs_ack; }

  //----------------------------------------------------------------------------
  private:

  friend std::ostream& operator<<(std::ostream&, const Header&);

  Channel    _channel;
  IDType     _id;
  bool       _is_ack;
  bool       _needs_ack;
};

inline std::ostream& operator<<(std::ostream& os, const Header& h)
{
  return os << "(Header " << (h.is_ack() ? "ACK|" : "")
            << h._channel << " " << h.id() << ")";
}

} // club namespace

#endif // ifndef CLUB_NET_HEADER_H
