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

#ifndef CLUB_TRANSPORT_ACK_ENTRY_H
#define CLUB_TRANSPORT_ACK_ENTRY_H

#include "ack_set.h"
#include "ack_set_serialize.h"

namespace club { namespace transport {

struct AckEntry {
  uuid   to;
  uuid   from;
  AckSet acks;
};

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, const AckEntry& ack_entry) {
  assert(ack_entry.acks.type() != AckSet::Type::unset);
  e.put(ack_entry.to);
  e.put(ack_entry.from);
  e.put(ack_entry.acks);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, AckEntry& ack_entry) {
  ack_entry.to   = d.get<uuid>();
  ack_entry.from = d.get<uuid>();
  ack_entry.acks = d.get<AckSet>();

  if (ack_entry.acks.type() == AckSet::Type::unset) {
    d.set_error();
  }

  assert(!d.error());
}

//------------------------------------------------------------------------------
inline
std::ostream& operator<<(std::ostream& os, const AckEntry& e) {
  return os << "(AckEntry to:" << e.to << " from:" << e.from
            << " " << e.acks << ")";
}

}} // club::transport namespace

//------------------------------------------------------------------------------
namespace binary {

template<>
struct encoded<::club::transport::AckEntry> {
  static size_t size() {
    return encoded<::club::uuid>::size()
         + encoded<::club::uuid>::size()
         + encoded<::club::transport::AckSet>::size();
  }
};

} // binary namespace

#endif // ifndef CLUB_TRANSPORT_ACK_ENTRY_H
