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

#ifndef CLUB_LOG_ENTRY_H
#define CLUB_LOG_ENTRY_H

#include <boost/range/adaptor/map.hpp>
#include <boost/variant.hpp>
#include <club/uuid.h>
#include "message.h"

namespace club {

struct LogEntry {
  //----------------------------------------------------------------------------
  // This entry may be committed only after these conditions are met:
  //   * quorum == (acks | map_keys)
  //   * predecessors is empty or predecessors.back() is the last message
  //     we've committed.
  std::map<MessageId, uuid> predecessors;
  LogMessage                message;

  std::set<uuid> quorum;
  std::map<uuid, AckData> acks;

  //----------------------------------------------------------------------------
  template<class M>
  LogEntry(M message)
    : message(std::move(message))
  {
    quorum.insert(::club::original_poster(message));
  }

  LogEntry(LogEntry&&) = default;
  LogEntry& operator=(LogEntry&&) = default;

  LogEntry(const LogEntry&) = delete;
  LogEntry& operator=(const LogEntry&) = delete;

  bool acked_by_quorum(const std::set<uuid>&) const;
  bool acked_by_quorum() const;

  MessageType message_type() const {
    return ::club::message_type(message);
  }

  uuid original_poster() const {
    return ::club::original_poster(message);
  }

  MessageId message_id() const {
    return ::club::message_id(message);
  }
};

inline
std::ostream& operator<<(std::ostream& os, const LogEntry& e) {
  using boost::adaptors::map_keys;

  if (e.predecessors.empty()) {
    return os << "(Entry: " << e.message << " Acks:[" << str(e.acks | map_keys)
              << "] Quorum:[" << str(e.quorum) << "], {})";
  }
  else {
    return os << "(Entry: " << e.message << " Acks:[" << str(e.acks | map_keys)
              << "] Quorum:[" << str(e.quorum) << "], " << str(e.predecessors) << ")";
  }
}

} // club namespace

namespace club {

//------------------------------------------------------------------------------
inline
bool LogEntry::acked_by_quorum() const {
  // TODO: Can be done more efficiently.
  for (auto q : quorum) {
    if (acks.count(q) == 0) {
      return false;
    }
  }
  return true;
}

inline
bool LogEntry::acked_by_quorum(const std::set<uuid>& alive) const {
  // TODO: Can be done more efficiently.
  for (auto q : quorum) {
    if (acks.count(q) == 0 && alive.count(q) != 0) {
      return false;
    }
  }
  return true;
}

} // club namespace

#endif // ifndef CLUB_LOG_ENTRY_H
