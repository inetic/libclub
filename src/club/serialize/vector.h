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

#pragma once

#include <vector>
#include <debug/ASSERT.h>
#include "binary/decoder.h"

namespace std {

//------------------------------------------------------------------------------
template<typename Encoder, class T>
  inline void encode( Encoder& e
                    , const std::vector<T>& vector
                    , size_t max_size = std::numeric_limits<uint32_t>::max()) {
    using namespace boost;

    ASSERT(vector.size() < max_size);

    e.template put<uint32_t>(vector.size());

    for (const auto& item : vector) {
      e.template put<typename std::decay<T>::type>(item);
    }
  }

//------------------------------------------------------------------------------
// TODO: Why doesn't the above use cover this special case?
template<typename Encoder>
  inline void encode( Encoder& e
                    , const std::vector<char>& vector
                    , size_t max_size = std::numeric_limits<uint32_t>::max()) {
    using namespace boost;

    ASSERT(vector.size() < max_size);

    e.template put<uint32_t>(vector.size());

    for (auto item : vector) {
      e.template put(item);
    }
  }

//------------------------------------------------------------------------------
template<class T>
inline void decode( binary::decoder& d
                  , std::vector<T>& vector
                  , size_t max_size = std::numeric_limits<uint32_t>::max()) {
  using namespace boost;

  if (d.error()) return;

  auto size = d.get<uint32_t>();

  if (size > max_size) {
    return d.set_error();
  }

  vector.clear();
  vector.reserve(size);

  for (uint32_t i = 0; i < size; ++i) {
    vector.push_back(d.get<T>());
    if (d.error()) break;
  }
}

//------------------------------------------------------------------------------

} // binary namespace
