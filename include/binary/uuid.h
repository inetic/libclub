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

#ifndef __BINARY_SERIALIZE_UUID_H__
#define __BINARY_SERIALIZE_UUID_H__

#include <boost/uuid/uuid.hpp>
#include <binary/decoder.h>

namespace boost { namespace uuids {

template<typename Encoder>
inline void encode(Encoder& e, boost::uuids::uuid uuid) {
  for (auto i : uuid) { e.template put(i); }
}

inline void decode(binary::decoder& d, boost::uuids::uuid& uuid) {
  for (auto& i : uuid) {
    i = d.get<std::remove_reference<decltype(i)>::type>();
  }
}

}} // boost::uuids namespace

#endif // ifndef __BINARY_SERIALIZE_UUID_H__
