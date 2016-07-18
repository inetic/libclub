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

#ifndef CLUB_SEEN_MESSAGES_H
#define CLUB_SEEN_MESSAGES_H

#include "message_id.h"

namespace club {

class SeenMessages {
  using uuid = boost::uuids::uuid;

  struct TimeStamps : std::set<TimeStamp> {
    // Everything <= to the `bottom` variable is considered
    // to be inside the set.
    boost::optional<TimeStamp> bottom;
  };

public:
  void insert(MessageId);
  bool is_in(const MessageId&);
  void seen_everything_up_to(const MessageId&);
  void forget_messages_from_user(const uuid&);

private:
  std::map<uuid, TimeStamps> _messages;
};

//--------------------------------------------------------------------
inline
void SeenMessages::insert(MessageId mid) {
  auto& tss = _messages[mid.original_poster];

  if (tss.bottom && mid.timestamp <= *tss.bottom) {
    return;
  }

  tss.insert(mid.timestamp);
}

//--------------------------------------------------------------------
inline
bool SeenMessages::is_in(const MessageId& mid) {
  auto i = _messages.find(mid.original_poster);

  if (i == _messages.end()) return false;

  auto& tss = i->second;

  if (tss.bottom && mid.timestamp <= *tss.bottom) {
    return true;
  }

  return tss.count(mid.timestamp) != 0;
}

//--------------------------------------------------------------------
inline
void SeenMessages::seen_everything_up_to(const MessageId& mid) {
  for (auto& pair : _messages) {
    auto& tss = pair.second;

    if (tss.bottom && mid.timestamp <= *tss.bottom) {
      continue;
    }

    tss.bottom = mid.timestamp;

    tss.erase( tss.begin()
             , tss.upper_bound(mid.timestamp));
  }
}

//--------------------------------------------------------------------
inline
void SeenMessages::forget_messages_from_user(const uuid& uid) {
  _messages.erase(uid);
}

//--------------------------------------------------------------------
} // club namespace

#endif // ifndef CLUB_SEEN_MESSAGES_H
