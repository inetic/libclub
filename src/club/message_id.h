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

#ifndef CLUB_MESSAGE_ID_H
#define CLUB_MESSAGE_ID_H

#include <club/uuid.h>
#include <club/detail/time_stamp.h>
#include <boost/uuid/nil_generator.hpp>
#include <club/debug/ostream_uuid.h>

namespace club {

struct MessageId {
  TimeStamp timestamp;
  uuid      original_poster;

  MessageId()
    : timestamp(0)
    , original_poster(boost::uuids::nil_uuid())
  {}

  MessageId(TimeStamp ts, uuid op)
    : timestamp(ts)
    , original_poster(std::move(op))
  {}

  bool operator < (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
         < std::tie(other.timestamp, other.original_poster);
  }

  bool operator <= (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
        <= std::tie(other.timestamp, other.original_poster);
  }

  bool operator > (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
         > std::tie(other.timestamp, other.original_poster);
  }

  bool operator >= (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
        >= std::tie(other.timestamp, other.original_poster);
  }

  bool operator == (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
        == std::tie(other.timestamp, other.original_poster);
  }

  bool operator != (const MessageId& other) const {
    return std::tie(timestamp, original_poster)
        != std::tie(other.timestamp, other.original_poster);
  }
};

inline
std::ostream& operator<<(std::ostream& os, const MessageId& o) {
  return os << "(MId " << o.original_poster << ":" << o.timestamp << ")";
}

} // club namespace

#endif // ifndef CLUB_MESSAGE_ID_H
