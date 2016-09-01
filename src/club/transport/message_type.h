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

#ifndef CLUB_TRANSPORT_MESSAGE_TYPE_H
#define CLUB_TRANSPORT_MESSAGE_TYPE_H

#include <binary/decoder.h>
#include <binary/encoded.h>

namespace club { namespace transport {

enum class MessageType { sync       = 0
                       , unreliable = 1
                       , reliable   = 2
                       };

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, const MessageType& t) {
  e.put((uint8_t) t);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, MessageType& t) {
  t = static_cast<MessageType>(d.get<uint8_t>());

  if (t < MessageType::sync || t > MessageType::reliable) {
    assert(0);
    d.set_error();
  }
}

//------------------------------------------------------------------------------
inline std::ostream& operator<<(std::ostream& os, MessageType t) {
  switch (t) {
    case MessageType::sync: return os << "sync";
    case MessageType::unreliable: return os << "unreliable";
    case MessageType::reliable: return os << "reliable";
  }
  return os << "unknown";
}

//------------------------------------------------------------------------------
}} // club::transport namespace

//------------------------------------------------------------------------------
namespace binary {
  template<> struct encoded<::club::transport::MessageType> {
    static size_t size() {
      return sizeof(uint8_t);
    }
  };
} // binary namespace

//------------------------------------------------------------------------------

#endif // ifndef CLUB_TRANSPORT_MESSAGE_TYPE_H
