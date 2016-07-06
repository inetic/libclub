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

#ifndef CLUB_SERIALIZE_MESSAGE_ID_H
#define CLUB_SERIALIZE_MESSAGE_ID_H

#include <binary/decoder.h>
#include "message_id.h"

namespace club {

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode(Encoder& e, const MessageId& mid) {
  e.template put(mid.timestamp);
  e.template put(mid.original_poster);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, MessageId& mid) {
  if (d.error()) return;
  mid.timestamp  = d.get<decltype(mid.timestamp)>();

  if (d.error()) return;
  mid.original_poster = d.get<decltype(mid.original_poster)>();
}

//------------------------------------------------------------------------------

} // club namespace

#endif // ifndef CLUB_SERIALIZE_MESSAGE_ID_H
