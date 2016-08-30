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

#ifndef CLUB_TRANSPORT_OUTBOUND_ACKS_H
#define CLUB_TRANSPORT_OUTBOUND_ACKS_H

namespace club { namespace transport {

class OutboundAcks {
private:
  struct AckSetId {
    AckEntry::Type ack_type;
    uuid           source;

    bool operator < (const AckSetId& other) const {
      return std::tie(ack_type, source)
           < std::tie(other.ack_type, other.source);
    }
  };

public:
  OutboundAcks(uuid our_id);

  size_t encode_few(binary::encoder&, const std::set<uuid>& targets);
  void add_ack_entry(AckEntry);
  void acknowledge(const uuid& from, AckEntry::Type, SequenceNumber);

private:
  uuid _our_id;
  std::map<uuid, std::map<AckSetId, AckSet>> _storage;
  //        |              |
  //        |              +-> (type, from)
  //        +-> to
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline
OutboundAcks::OutboundAcks(uuid our_id)
  : _our_id(std::move(our_id))
{}

//------------------------------------------------------------------------------
inline
size_t OutboundAcks::encode_few( binary::encoder& encoder
                               , const std::set<uuid>& targets) {
  // Need to write at least the size.
  assert(encoder.remaining_size() > 0);

  if (encoder.remaining_size() == 0) {
    encoder.set_error();
    return 0;
  }

  auto count_encoder = encoder;

  uint8_t count = 0;
  encoder.put(count); // Shall be rewritten later in this func.

  for (auto i = _storage.begin(); i != _storage.end();) {
    auto& froms = i->second;

    // TODO: Optimize this set intersection of targets with _storage.
    if (targets.count(i->first) == 0) {
      ++i;
      continue;
    }

    for (auto j = froms.begin(); j != froms.end();) {
      auto tmp_encoder = encoder;

      auto ack_entry = AckEntry{j->first.ack_type, i->first, j->first.source, j->second};
      encoder.put(ack_entry);

      if (encoder.error()) {
        count_encoder.put(count);
        encoder = tmp_encoder;
        return count;
      }

      if (count == std::numeric_limits<decltype(count)>::max()) {
        count_encoder.put(count);
        return count;
      }

      ++count;

      j = froms.erase(j);
    }

    if (i->second.empty()) {
      i = _storage.erase(i);
    }
    else {
      ++i;
    }
  }

  count_encoder.put(count);
  return count;
}

//------------------------------------------------------------------------------
inline
void OutboundAcks::add_ack_entry(AckEntry entry) {
  // TODO: Union with the existing AckSet.
  AckSetId ack_set_id{entry.type, entry.from};
  _storage[entry.to][ack_set_id] = entry.acks;
}

//------------------------------------------------------------------------------
inline
void OutboundAcks::acknowledge( const uuid& from
                              , AckEntry::Type type
                              , SequenceNumber sn) {
  AckSetId ack_set_id{type, _our_id};

  auto i = _storage.find(from);

  if (i == _storage.end()) {
    _storage[from].emplace(ack_set_id, AckSet(sn));
  }
  else {
    auto j = i->second.find(ack_set_id);
    if (j == i->second.end()) {
      _storage[from].emplace(ack_set_id, AckSet(sn));
    }
    else {
      j->second.try_add(sn);
    }
  }
}
//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_OUTBOUND_ACKS_H
