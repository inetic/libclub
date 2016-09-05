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

#ifndef CLUB_LOG_H
#define CLUB_LOG_H

#include <boost/range/adaptor/map.hpp>
#include "log_entry.h"

namespace club {

class Log : public std::map<MessageId, LogEntry> {
private:
  typedef std::map<MessageId, LogEntry> Map;

public:
  MessageId get_predecessor_time(const MessageId&);
  LogEntry* get_predecessor(const MessageId&);

  LogEntry* find_entry(const MessageId&);

  LogEntry* find_highest_fuse_entry();

  void apply_ack(const uuid& op_id, AckData ack);

  void insert_entry(LogEntry&&);

  // TODO: Make private
  MessageId last_fuse_commit;
  MessageId last_committed;
  uuid   last_commit_op;
  std::map<MessageId, std::map<uuid, AckData>> pending_acks;
};

} // club namespace

namespace club {

//------------------------------------------------------------------------------
inline
LogEntry* Log::get_predecessor(const MessageId& supremum) {
  auto i = Map::lower_bound(supremum);

  if (i == Map::begin()) return nullptr;
  --i;

  return &i->second;
}

//------------------------------------------------------------------------------
inline void Log::insert_entry(LogEntry&& entry_) {
  using std::move;
  using std::next;
  using boost::adaptors::map_values;

  auto tc  = message_id(entry_.message);

  if (tc <= last_committed) {
    // TODO: This normally should never happen, but it sometimes _does_
    // when I run either club_fuse_again or club_stress_fuse test. Need to
    // investigate why it is so.
    //assert(0);
    return;
  }

  auto pair = insert(std::make_pair(move(tc), move(entry_)));

  auto entry_i = pair.first;
  auto& entry = entry_i->second;

  //------------------------------------------------------------------
  // Setup predecessors.
  entry.predecessors[last_committed] = last_commit_op;

  auto op_id = entry.original_poster();

  if (entry_i != begin()) {
    auto& prev_entry = std::prev(entry_i)->second.message;
    entry.predecessors[message_id(prev_entry)] = op_id;
  }

  if (next(entry_i) != end()) {
    auto& next_entry = std::next(entry_i)->second;
    next_entry.predecessors[message_id(entry.message)] = op_id;
  }

  //------------------------------------------------------------------
  // Apply any pending acks.
  auto ack_i = pending_acks.find(message_id(entry.message));

  if (ack_i != pending_acks.end()) {
    auto acks  = move(ack_i->second);

    pending_acks.erase(ack_i);

    for (auto& pair : acks) {
      apply_ack(pair.first, move(pair.second));
    }
  }

  //------------------------------------------------------------------
  apply_ack(op_id, std::move(ack_data(entry.message)));
}

//------------------------------------------------------------------------------
inline void Log::apply_ack(const uuid& op_id, AckData ack) {
  auto e = this->find_entry(ack.acked_message_id);

  if (e == nullptr) {
    this->pending_acks[ack.acked_message_id][op_id] = ack;
    return;
  }

  for (auto q : ack.local_quorum) {
    e->quorum.insert(q);
  }

  e->acks[op_id] = ack;

  e->predecessors[ack.prev_message_id] = op_id;
}

//------------------------------------------------------------------------------
inline
MessageId Log::get_predecessor_time(const MessageId& supremum) {
  auto p = get_predecessor(supremum);

  if (p == nullptr) {
    return MessageId();
  }

  return message_id(p->message);
}

//------------------------------------------------------------------------------
inline LogEntry* Log::find_highest_fuse_entry() {
  using boost::adaptors::map_values;

  for (auto i = this->rbegin(); i != this->rend(); ++i) {
    if (i->second.message_type() != fuse) continue;
    return &i->second;
  }

  return nullptr;
}

//------------------------------------------------------------------------------
inline
LogEntry* Log::find_entry(const MessageId& clock) {
  auto i = Map::find(clock);
  if (i == Map::end()) return nullptr;
  return &i->second;
}

//------------------------------------------------------------------------------
} // club namespace

#endif // ifndef CLUB_LOG_H
