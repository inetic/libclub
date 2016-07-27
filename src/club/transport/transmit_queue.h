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
#include "binary/serialize/uuid.h"
#include "transport/message.h"
#include "transport/outbound_messages.h"
#include "transport/ack_set_serialize.h"

namespace club { namespace transport {

template<class> class OutboundMessages;
template<class> class Transport;

template<class UnreliableId>
struct TransmitQueue {
private:
  using OutboundMessages = transport::OutboundMessages<UnreliableId>;
  using uuid = boost::uuids::uuid;

  using Message = transport::OutMessage;
  using MessagePtr = std::shared_ptr<Message>;

  struct Entry {
    boost::optional<UnreliableId> unreliable_id;
    MessagePtr                    message;
  };

  using Messages = std::list<Entry>;

public:
  TransmitQueue(std::shared_ptr<OutboundMessages>);

  size_t encode_few(binary::encoder&);

  void add_target(const uuid&);

  void insert_message( boost::optional<UnreliableId>
                     , MessagePtr);

  OutboundMessages& outbound_messages() { return *_outbound_messages; }

private:
  void circular_increment(typename Messages::iterator& i);

  static void set_intersection( const std::set<uuid>&
                              , const std::set<uuid>&
                              , std::vector<uuid>&);

  static void set_difference( const std::vector<uuid>&
                            , const std::set<uuid>&
                            , std::vector<uuid>&);

  void erase(typename Messages::iterator i);

  bool try_encode( binary::encoder&
                 , const std::vector<uuid>&
                 , const Message&) const;

  void encode( binary::encoder&
             , const std::vector<uuid>&
             , const Message&) const;

  void encode_targets(binary::encoder&, const std::vector<uuid>&) const;

  size_t encoded_size( const std::vector<uuid>& targets
                     , const Message& msg) const;

private:
  std::shared_ptr<OutboundMessages> _outbound_messages;
  std::set<uuid>                    _possible_targets;

  // Invariant that must hold: _messages.empty() <=> _next == _messages.end()
  Messages                     _messages;
  typename Messages::iterator  _next;

  // A cache vector so we don't have to reallocate it each time.
  std::vector<uuid> _target_intersection;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
TransmitQueue<Id>::TransmitQueue(std::shared_ptr<OutboundMessages> outbound)
  : _outbound_messages(std::move(outbound))
  , _next(_messages.end())
{}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::add_target(const uuid& id) {
  _possible_targets.insert(id);
}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::insert_message( boost::optional<Id> unreliable_id
                                      , MessagePtr message) {
  _messages.insert(_next, Entry{ std::move(unreliable_id)
                               , std::move(message)
                               });

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
TransmitQueue<Id>::erase(typename Messages::iterator i) {
  using std::shared_ptr;

  //Tell the _outbound_messages object that we're no longer using this message.
  _outbound_messages->release( std::move(i->unreliable_id)
                             , std::move(i->message));

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
size_t
TransmitQueue<Id>::encode_few(binary::encoder& encoder) {
  size_t count = 0;

  if (_messages.empty()) return count;

  auto last = _next;
  if (last == _messages.begin()) { last = --_messages.end(); }
  else --last;

  bool is_last = false;

  while (!is_last) {
    auto current = _next;

    circular_increment(_next);

    is_last = current == last;

    set_intersection( current->message->targets
                    , _possible_targets
                    , _target_intersection);

    if (_target_intersection.empty()) {
      erase(current);
      if (_messages.empty()) break;
      continue;
    }

    if (!try_encode(encoder, _target_intersection, *current->message)) {
      _next = current;
      break;
    }

    ++count;

    // Unreliable entries are sent only once to each target.
    // TODO: Also erase the message if _target_intersection is empty.
    if (!current->message->is_reliable) {
      auto& m = *current->message;

      for (const auto& target : _target_intersection) {
        m.targets.erase(target);
      }

      if (m.targets.empty()) {
        erase(current);
        if (_messages.empty()) break;
      }
    }
  }

  return count;
}

//------------------------------------------------------------------------------
template<class Id>
bool
TransmitQueue<Id>::try_encode( binary::encoder& encoder
                             , const std::vector<uuid>& targets
                             , const Message& msg) const {

  if (encoded_size(targets, msg) > encoder.remaining_size()) {
    return false;
  }

  encode(encoder, targets, msg);

  assert(!encoder.error());

  return true;
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::encode( binary::encoder& encoder
                         , const std::vector<uuid>& targets
                         , const Message& msg) const {
  encoder.put(msg.source);
  encode_targets(encoder, targets);
  encoder.put_raw(msg.bytes.data(), msg.bytes.size());
}

//------------------------------------------------------------------------------
template<class Id>
size_t
TransmitQueue<Id>::encoded_size( const std::vector<uuid>& targets
                               , const Message& msg) const {
  size_t sizeof_uuid = binary::encoded<uuid>::size();

  return sizeof_uuid // msg.source
       + sizeof(uint8_t) // number of targets
       + targets.size() * sizeof_uuid
       + msg.bytes.size();
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
void TransmitQueue<Id>::set_intersection( const std::set<uuid>& set1
                                        , const std::set<uuid>& set2
                                        , std::vector<uuid>& result) {
  result.resize(0);

  std::set_intersection( set1.begin(), set1.end()
                       , set2.begin(), set2.end()
                       , std::back_inserter(result));
}

//------------------------------------------------------------------------------
template<class Id>
void TransmitQueue<Id>::set_difference( const std::vector<uuid>& set1
                                      , const std::set<uuid>&    set2
                                      , std::vector<uuid>&       result) {
  result.resize(0);

  std::set_difference( set1.begin(), set1.end()
                     , set2.begin(), set2.end()
                     , std::back_inserter(result));
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSMIT_QUEUE_H
