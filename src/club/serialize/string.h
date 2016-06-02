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

#ifndef __CLUB_SERIALIZE_STRING_H__
#define __CLUB_SERIALIZE_STRING_H__

#include <string>
#include <debug/ASSERT.h>
#include "binary/decoder.h"

namespace std {

//------------------------------------------------------------------------------
template<typename Encoder>
  inline void encode( Encoder& e
                    , const std::string& str
                    , size_t max_size = std::numeric_limits<uint32_t>::max()) {
    using namespace boost;

    ASSERT(str.size() < max_size);

    e.template put<uint32_t>(str.size());

    for (const auto& item : str) {
      e.template put(item);
    }
  }

//------------------------------------------------------------------------------
inline void decode( binary::decoder& d
                  , std::string& str
                  , size_t max_size = std::numeric_limits<uint32_t>::max()) {
  using namespace boost;

  if (d.error()) return;

  auto size = d.get<uint32_t>();

  if (size > max_size) {
    return d.set_error();
  }

  for (uint32_t i = 0; i < size; ++i) {
    str.push_back(d.get<uint8_t>());
    if (d.error()) break;
  }
}

//------------------------------------------------------------------------------

} // std namespace

#endif // ifndef __CLUB_SERIALIZE_STRING_H__
