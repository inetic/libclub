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

#ifndef __CLUB_SERIALIZE_LIST_H__
#define __CLUB_SERIALIZE_LIST_H__

#include <list>
#include "binary/decoder.h"

namespace club {

//------------------------------------------------------------------------------
template<typename Encoder, class T>
inline void encode( Encoder& e
                  , const std::list<T>& list
                  , size_t max_size = std::numeric_limits<uint32_t>::max()) {
  using namespace boost;

  ASSERT(list.size() < max_size);

  e.template put<uint32_t>(list.size());

  for (const auto& item : list) {
    e.template put<T>(item);
  }
}

//------------------------------------------------------------------------------
template<class T>
inline void decode( binary::decoder& d
                  , std::list<T>& list
                  , size_t max_size = std::numeric_limits<uint32_t>::max()) {
  using namespace boost;

  if (d.error()) return;

  auto size = d.get<uint32_t>();

  if (size > max_size) {
    return d.set_error();
  }

  for (uint32_t i = 0; i < size; ++i) {
    list.push_back(d.get<T>());
    if (d.error()) break;
  }
}

//------------------------------------------------------------------------------

} // club namespace

#endif // ifndef __CLUB_SERIALIZE_LIST_H__
