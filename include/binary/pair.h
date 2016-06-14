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

#ifndef __CLUB_BINARY_PAIR_SERIALIZATION_H__
#define __CLUB_BINARY_PAIR_SERIALIZATION_H__

#include <binary/decoder.h>

namespace std {

//------------------------------------------------------------------------------
template<typename Encoder, class T1, class T2>
  inline void encode(Encoder& e, const std::pair<T1, T2>& pair) {
    e.template put(pair.first);
    e.template put(pair.second);
  }

//------------------------------------------------------------------------------
template<class T1, class T2>
inline void decode(binary::decoder& d, std::pair<T1, T2>& pair) {
  if (d.error()) return;
  pair.first  = d.get<T1>();

  if (d.error()) return;
  pair.second = d.get<T2>();
}

//------------------------------------------------------------------------------

} // std namespace

#endif // ifndef __CLUB_BINARY_PAIR_SERIALIZATION_H__
