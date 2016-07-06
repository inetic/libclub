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

#ifndef BINARY_SERIALIZE_OPTIONAL_H
#define BINARY_SERIALIZE_OPTIONAL_H

#include <boost/optional.hpp>
#include "binary/decoder.h"

namespace boost {

//------------------------------------------------------------------------------
template<typename Encoder, class T>
inline void encode( Encoder& e
                  , const boost::optional<T>& opt) {
  using namespace boost;

  if (opt) {
    e.template put<uint8_t>(1);
    e.template put<T>((typename std::remove_reference<T>::type)(*opt));
  }
  else {
    e.template put<uint8_t>(0);
  }
}

//------------------------------------------------------------------------------
template<class T>
inline void decode( binary::decoder& d
                  , boost::optional<T>& opt) {
  using namespace boost;

  if (d.error()) return;

  if (d.get<uint8_t>()) {
    if (d.error()) return;
    opt = d.get<typename std::remove_reference<T>::type>();
  }
  else {
    opt = boost::none;
  }
}

//------------------------------------------------------------------------------

} // boost namespace

#endif // ifndef BINARY_SERIALIZE_OPTIONAL_H
