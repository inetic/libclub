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

#ifndef CLUB_DEBUG_OSTREAM_UUID_H
#define CLUB_DEBUG_OSTREAM_UUID_H

#include <boost/uuid/uuid.hpp>
#include <boost/format.hpp>

namespace boost { namespace uuids {

inline std::ostream& operator<<(std::ostream& os, uuid id) {
  using namespace std;
  int truncated_size = 2;
  os << "[";
  for (auto a : id) {
    os << boost::format("%02x") % (int) a;
    if (--truncated_size == 0) {
      break;
    }
  }
  return os << "]";
}

}} // boost::uuids namespace

#endif // ifndef CLUB_DEBUG_OSTREAM_UUID_H
