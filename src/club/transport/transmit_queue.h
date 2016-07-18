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

#ifndef CLUB_TRANSMIT_QUEUE_H
#define CLUB_TRANSMIT_QUEUE_H

#include <list>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include "binary/encoder.h"
#include "transport/message.h"
#include "transport/outbound_messages.h"

namespace club { namespace transport {

template<class> class OutboundMessages;
template<class> class Transport;

template<class UnreliableId>
struct TransmitQueue {
private:
  friend class ::club::transport::Transport<UnreliableId>;

  using OutboundMessages = ::club::transport::OutboundMessages<UnreliableId>;
  using uuid = boost::uuids::uuid;
  using UnreliableMessage = UnreliableMessageT<UnreliableId>;

  using Message
    = boost::variant< std::shared_ptr<ReliableMessage>
                    , std::shared_ptr<UnreliableMessage>
                    >;

  using Messages = std::list<Message>;

public:
  TransmitQueue(std::shared_ptr<OutboundMessages>);

  uint16_t encode_few(binary::encoder&);

private:
  void add_target(const uuid&);

  void insert_message(Message);

  void circular_increment(typename Messages::iterator& i);

  static std::set<uuid>& targets_of(Message&);

  static void set_intersection( const std::set<uuid>&
                              , const std::set<uuid>&
                              , std::vector<uuid>&);

  void erase(typename Messages::iterator i);
  void release(Message& m);

  bool try_encode( binary::encoder&
                 , const std::vector<uuid>&
                 , const Message&) const;

  void encode( binary::encoder&
             , const std::vector<uuid>&
             , const Message&) const;

  void encode_targets(binary::encoder&, const std::vector<uuid>&) const;

  OutboundMessages& outbound_messages() { return *_outbound_messages; }

private:
  std::shared_ptr<OutboundMessages> _outbound_messages;
  std::set<uuid>                    _targets;

  // Invariant that must hold: _messages.empty() <=> _next != _messages.end()
  Messages                     _messages;
  typename Messages::iterator  _next;

  // A cache vector so we don't have to reallocate it each time.
  std::vector<uuid> _target_intersection;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
TransmitQueue<Id>::TransmitQueue(std::shared_ptr<OutboundMessages> transmit_set)
  : _outbound_messages(std::move(transmit_set))
  , _next(_messages.end())
{}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::add_target(const uuid& id) {
  _targets.insert(id);
}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::insert_message(Message message) {
  _messages.insert(_next, std::move(message));
  if (_next == _messages.end()) _next = _messages.begin();
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::circular_increment(typename Messages::iterator& i) {
  assert(!_messages.empty() && i != _messages.end());
  if (++i == _messages.end()) i = _messages.begin();
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::release(Message& m) {
  using std::shared_ptr;

  if (auto pp = boost::get<shared_ptr<ReliableMessage>>(&m)) {
    SequenceNumber sn = (*pp)->sequence_number;
    pp->reset(); // Decrement counter
    _outbound_messages->release(sn);
  }
  else if (auto pp = boost::get<shared_ptr<UnreliableMessage>>(&m)) {
    Id id = std::move((*pp)->id);
    pp->reset(); // Decrement counter
    _outbound_messages->release(id);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::erase(typename Messages::iterator i) {
  release(*i);

  if (i == _next) {
    _next = _messages.erase(i);
    if (_next == _messages.end()) _next = _messages.begin();
  }
  else {
    _messages.erase(i);
  }
}

//------------------------------------------------------------------------------
template<class Id>
uint16_t
TransmitQueue<Id>::encode_few(binary::encoder& encoder) {
  uint16_t count = 0;
  if (_messages.empty()) return 0;

  auto last = _next;
  if (last == _messages.begin()) { last = --_messages.end(); }
  else --last;

  bool is_last = false;

  while (true) {
    auto current = _next;

    circular_increment(_next);

    is_last = current == last;

    set_intersection(targets_of(*current), _targets, _target_intersection);

    if (_target_intersection.empty()) {
      erase(current);
      if (_messages.empty()) break;
      continue;
    }

    if (!try_encode(encoder, _target_intersection, *current)) {
      _next = current;
      break;
    }

    ++count;

    // Unreliable entries are sent only once to each target.
    if (auto pp = boost::get<std::shared_ptr<UnreliableMessage>>(&*current)) {
      auto& m = **pp;

      for (const auto& target : _targets) {
        m.targets.erase(target);
      }

      if (m.targets.empty()) {
        erase(current);
        if (_messages.empty()) break;
      }
    }

    if (is_last) break;
  }

  return count;
}

//------------------------------------------------------------------------------
template<class Id>
bool
TransmitQueue<Id>::try_encode( binary::encoder& encoder
                             , const std::vector<uuid>& targets
                             , const Message& msg) const {
  auto prev_begin = encoder._current.begin;
  auto prev_error = encoder._was_error;
  
  encode(encoder, targets, msg);
  
  if (encoder.error()) {
    encoder._current.begin = prev_begin;
    encoder._was_error     = prev_error;
    return false;
  }

  return true;
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::encode( binary::encoder& encoder
                         , const std::vector<uuid>& targets
                         , const Message& msg) const {
  using boost::get;
  using std::shared_ptr;

  std::vector<uint8_t>* bytes = nullptr;

  if (auto pp1 = get<shared_ptr<UnreliableMessage>>(&msg)) {
    assert(*pp1);
    encoder.put((uint8_t) 0);
    encode_targets(encoder, targets);
    bytes = &(**pp1).bytes;
  }
  else {
    auto pp2 = get<shared_ptr<ReliableMessage>>(&msg);
    assert(pp2 && *pp2);
    auto& msg = **pp2;

    encoder.put((uint8_t) 1);
    encoder.put(msg.sequence_number);
    encode_targets(encoder, targets);
    bytes = &msg.bytes;
  }

  const auto size = bytes->size();

  encoder.put((uint16_t) size);
  encoder.put_raw(bytes->data(), size);
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::encode_targets( binary::encoder& encoder
                                 , const std::vector<uuid>& targets) const {
  if (targets.size() > std::numeric_limits<uint8_t>::max()) {
    assert(0);
    return encoder.set_error();
  }

  encoder.put((uint8_t) targets.size());

  for (const auto& id : targets) {
    encoder.put(id);
  }
}

//------------------------------------------------------------------------------
template<class Id>
std::set<typename TransmitQueue<Id>::uuid>&
TransmitQueue<Id>::targets_of(Message& e)
{
  using boost::get;
  using std::shared_ptr;

  if (auto pp1 = get<shared_ptr<UnreliableMessage>>(&e)) {
    assert(*pp1);
    return (*pp1)->targets;
  }
  else {
    auto pp2 = get<shared_ptr<ReliableMessage>>(&e);
    assert(pp2 && *pp2);
    return (*pp2)->targets;
  }
}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::set_intersection( const std::set<uuid>& set1
                                        , const std::set<uuid>& set2
                                        , std::vector<uuid>& result) {
  result.resize(0);

  std::set_intersection( set1.begin(), set1.end()
                       , set2.begin(), set2.end()
                       , std::back_inserter(result));
}

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSMIT_QUEUE_H
