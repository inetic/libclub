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
#include "transport/core.h"
#include "transport/ack_set_serialize.h"
#include "debug/string_tools.h"

namespace club { namespace transport {

template<class> class Core;
template<class> class Transport;

template<class UnreliableId>
struct TransmitQueue {
private:
  using Core = transport::Core<UnreliableId>;
  using uuid = boost::uuids::uuid;

  using Message = transport::OutMessage;
  using MessagePtr = std::shared_ptr<Message>;

  struct Entry {
    boost::optional<UnreliableId> unreliable_id;
    MessagePtr                    message;
  };

  using Messages = std::list<Entry>;

public:
  TransmitQueue(std::shared_ptr<Core>);

  size_t encode_few(binary::encoder&);

  void add_target(const uuid&);

  void insert_message( boost::optional<UnreliableId>
                     , MessagePtr);

  Core& core() { return *_core; }

private:
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
  std::shared_ptr<Core> _core;
  std::set<uuid>        _possible_targets;

  Messages                     _messages;
  typename Messages::iterator  _next;

  // A cache vector so we don't have to reallocate it each time.
  std::vector<uuid> _target_intersection;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
TransmitQueue<Id>::TransmitQueue(std::shared_ptr<Core> core)
  : _core(std::move(core))
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
  if (_next != _messages.end() || _messages.empty()) {
    _messages.insert(_next, Entry{ std::move(unreliable_id)
                          , std::move(message)
                          });
  }
  else {
    // If we're at the end of the queue and it isn't empty, that means we've
    // just sent everything in it. So the new messages shall be
    // sent next.
    _messages.insert(_messages.begin(), Entry{ std::move(unreliable_id)
                                      , std::move(message)
                                      });
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
TransmitQueue<Id>::erase(typename Messages::iterator i) {
  using std::shared_ptr;

  assert(i != _messages.end());

  // Tell the _core object that we're no longer using this message.
  _core->release( std::move(i->unreliable_id)
                , std::move(i->message));

  if (i == _next) {
    _next = _messages.erase(i);
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

  using namespace std;

  if (_messages.empty()) return count;

  auto current = _next;

  if (current == _messages.end()) {
    current = _messages.begin();
  }

  auto last = current;
  if (last == _messages.begin()) { last = --_messages.end(); }
  else --last;

  while (true) {
    _next = std::next(current);

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
    if (current->message->type == MessageType::unreliable) {
      auto& m = *current->message;

      for (const auto& target : _target_intersection) {
        m.targets.erase(target);
      }

      if (m.targets.empty()) {
        erase(current);
        if (_messages.empty()) break;
      }
    }

    if (current == last) break;

    current = _next;
    if (current == _messages.end()) current = _messages.begin();
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
