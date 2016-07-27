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

#ifndef CLUB_TRANSPORT_ACK_SET_SERIALIZE_H
#define CLUB_TRANSPORT_ACK_SET_SERIALIZE_H

#include <binary/encoder.h>
#include <binary/decoder.h>
#include "ack_set.h"

//------------------------------------------------------------------------------
namespace binary {
  template<> struct encoded<::club::transport::AckSet> {
    static size_t size() {
      return sizeof(::club::transport::SequenceNumber)
           + sizeof(uint32_t);
    }
  };
} // binary namespace

namespace club { namespace transport {

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode( Encoder& e, const AckSet& ack_set) {
  e.put((SequenceNumber) ack_set.highest_sequence_number);

  uint32_t mixed = ack_set.predecessors
                 | (ack_set.is_empty << 31);

  e.put(mixed);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, AckSet& ack_set) {
  if (d.error()) return;

  ack_set.highest_sequence_number = d.get<SequenceNumber>();

  auto mixed = d.get<uint32_t>();

  ack_set.predecessors = mixed;
  ack_set.is_empty     = mixed >> 31;
}


}} // club::transport namespace

#endif // define CLUB_TRANSPORT_ACK_SET_SERIALIZE_H

