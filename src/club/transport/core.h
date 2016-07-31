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

#ifndef CLUB_TRANSPORT_CORE_H
#define CLUB_TRANSPORT_CORE_H

#include <map>
#include "transport/message.h"
#include "transport/ack_set.h"
#include "transport/ack_entry.h"

#include <club/debug/ostream_uuid.h>
#include "debug/string_tools.h"

namespace club { namespace transport {

template<typename> class TransmitQueue;
template<typename> class Transport;

template<typename UnreliableId>
class Core {
private:
  using OnReceive          = std::function<void(uuid, boost::asio::const_buffer)>;
  using OnFlush            = std::function<void()>;
  using Queue              = TransmitQueue<UnreliableId>;
  using Message            = transport::OutMessage;
  using Transport          = transport::Transport<UnreliableId>;

  using ReliableMessages   = std::map<SequenceNumber, std::weak_ptr<Message>>;
  using UnreliableMessages = std::map<UnreliableId, std::weak_ptr<Message>>;

public:
  Core(uuid our_id, OnReceive);

  void send_reliable( std::vector<uint8_t>&& data
                    , std::set<uuid>         targets);

  void send_unreliable( UnreliableId           id
                      , std::vector<uint8_t>&& data
                      , std::set<uuid>         targets);

  const uuid& id() const { return _our_id; }

  void flush(OnFlush);

  ~Core();

private:
  friend class ::club::transport::Transport<UnreliableId>;
  friend class ::club::transport::TransmitQueue<UnreliableId>;

  void release(boost::optional<UnreliableId>, std::shared_ptr<Message>&&);

  void register_transport(Transport*);
  void unregister_transport(Transport*);
  void forward_message(InMessage&&);

  void add_ack_entry(AckEntry);
  uint8_t encode_acks(binary::encoder& encoder, const std::set<uuid>& targets);

  void add_target(uuid);

  void on_receive(InMessage);

  void on_receive_acks(const uuid&, AckSet);
  void acknowledge(const uuid&, SequenceNumber);

  void try_flush();

private:
  struct PendingEntry {
    InMessage            message;
    std::vector<uint8_t> data;

    PendingEntry(PendingEntry&&)                 = default;
    PendingEntry(const PendingEntry&)            = delete;
    PendingEntry& operator=(const PendingEntry&) = delete;

    PendingEntry(InMessage m)
      : message(std::move(m))
      , data( boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
            , boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
              + boost::asio::buffer_size(message.type_and_payload) )
    {
      using boost::asio::const_buffer;
      using boost::asio::buffer_size;

      size_t type_size = buffer_size(message.type_and_payload)
                       - buffer_size(message.payload);

      message.type_and_payload = const_buffer(data.data(), data.size());
      message.payload          = const_buffer( data.data() + type_size
                                             , data.size() - type_size);
    }
  };

  using PendingMessages = std::map<SequenceNumber, PendingEntry>;

  void recursively_apply(SequenceNumber, PendingMessages&);

  struct Inbound {
    SequenceNumber  last_executed_message;
    AckSet          acks;
    PendingMessages pending;

    Inbound() {}

    Inbound(SequenceNumber first, AckSet acks)
      : last_executed_message(first)
      , acks(acks)
    {}
  };

private:
  uuid                   _our_id;
  OnReceive              _on_recv;
  SequenceNumber         _next_reliable_broadcast_number;
  // This number should be unique for each packet sent, i.e. even
  // a particular message - if sent multiple times - should always
  // have this number incremented.
  // TODO: The above currently doesn't hold.
  SequenceNumber         _next_message_number;
  std::set<Transport*>   _transports;
  ReliableMessages       _reliable_messages;
  UnreliableMessages     _unreliable_messages;

  std::set<uuid>         _all_targets;
  OnFlush                _on_flush;

  std::map<uuid, Inbound>                _inbound;
  std::map<uuid, std::map<uuid, AckSet>> _outbound_acks;
  //        |              |
  //        |              +-> from
  //        +-> to

  std::shared_ptr<bool> _was_destroyed;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id> Core<Id>::Core(uuid our_id, OnReceive on_recv)
  : _our_id(std::move(our_id))
  , _on_recv(std::move(on_recv))
  , _next_reliable_broadcast_number(0) // TODO: Should this be initialized to a random number?
  , _next_message_number(0)
  , _was_destroyed(std::make_shared<bool>(false))
{
}

//------------------------------------------------------------------------------
template<class Id> Core<Id>::~Core() {
  *_was_destroyed = true;
}
//------------------------------------------------------------------------------
template<class Id> void Core<Id>::register_transport(Transport* t)
{
  _transports.insert(t);
}

//------------------------------------------------------------------------------
template<class Id> void Core<Id>::unregister_transport(Transport* t)
{
  _transports.erase(t);
}

//------------------------------------------------------------------------------
template<class Id>
uint8_t Core<Id>::encode_acks( binary::encoder& encoder
                             , const std::set<uuid>& targets) {
  // Need to write at least the size.
  assert(encoder.remaining_size() > 0);

  auto count_encoder = encoder;

  uint8_t count = 0;
  encoder.put(count); // Shall be rewritten later in this func.

  for (auto i = _outbound_acks.begin(); i != _outbound_acks.end();) {
    auto& froms = i->second;

    // TODO: Optimize this set intersection of targets with _outbound_acks.
    if (targets.count(i->first) == 0) {
      ++i;
      continue;
    }

    for (auto j = froms.begin(); j != froms.end();) {
      auto tmp_encoder = encoder;

      auto ack_entry = AckEntry{i->first, j->first, j->second};
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
      i = _outbound_acks.erase(i);
    }
    else {
      ++i;
    }
  }

  count_encoder.put(count);
  return count;
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::add_ack_entry(AckEntry entry) {
  _outbound_acks[entry.to][entry.from] = entry.acks;
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::acknowledge(const uuid& from, SequenceNumber sn) {
  _outbound_acks[from][_our_id].try_add(sn);
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::on_receive_acks(const uuid& target, AckSet acks) {
  bool acked_some = false;

  for (auto sn : acks) {
    auto i = _reliable_messages.find(sn);

    if (i != _reliable_messages.end()) {
      if (auto message = i->second.lock()) {
        message->targets.erase(target);

        if (message->targets.empty()) {
          _reliable_messages.erase(i);
        }

        acked_some = true;
      }
    }
  }

  if (acked_some) {
    try_flush();
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
Core<Id>::send_reliable( std::vector<uint8_t>&& data
                       , std::set<uuid>         targets) {
  using namespace std;

  auto sn = _next_reliable_broadcast_number++;

  // TODO: It is inefficient to *copy* the data into the new vector just to
  //       prepend a small header (same below).
  std::vector<uint8_t> data_( binary::encoded<MessageType>::size()
                            + sizeof(SequenceNumber)
                            + sizeof(uint16_t) // Size of data
                            + data.size()
                            );

  binary::encoder encoder(data_.data(), data_.size());

  assert(data.size() <= std::numeric_limits<uint16_t>::max());

  encoder.put(MessageType::reliable);
  encoder.put(sn);
  encoder.put((uint16_t) data.size());
  encoder.put_raw(data.data(), data.size());

  auto message = make_shared<Message>( _our_id
                                     , move(targets)
                                     , MessageType::reliable
                                     , sn
                                     , move(data_)
                                     );

  _reliable_messages.emplace(sn, message);

  for (auto t : _transports) {
    t->insert_message(boost::none, message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::add_target(uuid new_target) {
  using namespace std;

  auto inserted = _all_targets.insert(std::move(new_target)).second;

  if (inserted) {
    auto sn = _next_reliable_broadcast_number++;

    std::vector<uint8_t> data( binary::encoded<MessageType>::size()
                             + sizeof(SequenceNumber)
                             + sizeof(uint16_t) // zero
                             );

    binary::encoder encoder(data);

    encoder.put(MessageType::syn);
    encoder.put(sn);
    encoder.put((uint16_t) 0);

    auto message = make_shared<Message>( _our_id
                                       , set<uuid>(_all_targets)
                                       , MessageType::syn
                                       , sn
                                       , move(data));

    _reliable_messages.emplace(sn, message);

    for (auto t : _transports) {
      t->insert_message(boost::none, message);
    }
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::send_unreliable( Id                     id
                              , std::vector<uint8_t>&& data
                              , std::set<uuid>         targets) {
  using namespace std;

  auto i = _unreliable_messages.find(id);

  if (i != _unreliable_messages.end()) {
    if (auto p = i->second.lock()) {
      p->bytes = std::move(data);
    }
    // else { it was there but was already sent, so noop }
  }
  else {
    // TODO: Same as above, it is inefficient to *copy* the data
    //       just to prepend a small header.
    std::vector<uint8_t> data_( binary::encoded<MessageType>::size()
                              + sizeof(Id)
                              + sizeof(uint16_t) // Size of data
                              + data.size()
                              );

    binary::encoder encoder(data_.data(), data_.size());

    auto sn = _next_message_number++;

    encoder.put(MessageType::unreliable);
    encoder.put(sn);
    encoder.put((uint16_t) data.size());
    encoder.put_raw(data.data(), data.size());

    auto message = make_shared<Message>( _our_id
                                       , move(targets)
                                       , MessageType::unreliable
                                       , sn
                                       , move(data_)
                                       );

    _unreliable_messages.emplace(id, move(message));

    for (auto t : _transports) {
      t->insert_message(id, message);
    }
  }

}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::forward_message(InMessage&& msg) {
  using namespace std;

  auto begin = boost::asio::buffer_cast<const uint8_t*>(msg.type_and_payload);
  auto size  = boost::asio::buffer_size(msg.type_and_payload);

  std::vector<uint8_t> data(begin, begin + size);

  auto message = make_shared<Message>( move(msg.source)
                                     , move(msg.targets)
                                     , MessageType::unreliable
                                     , msg.sequence_number
                                     , move(data) );

  // TODO: Same as with unreliable messages, store the message in a
  // std::map so that we don't put identical messages to message queues
  // (through transports).

  for (auto t : _transports) {
    t->insert_message(boost::none, message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
Core<Id>::recursively_apply(SequenceNumber next_sn, PendingMessages& pending) {
  if (pending.empty()) return;
  auto i = pending.begin();
  if (i->first != next_sn) return;

  // Move the entry to this local variable to extend its lifetime beyond the
  // following erase.
  auto pending_entry = std::move(i->second);
  i = pending.erase(i);

  on_receive(std::move(pending_entry.message));
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::on_receive(InMessage msg) {
  using std::move;

  if (msg.type == MessageType::reliable) {
    auto i = _inbound.find(msg.source);

    // If there is no AckSet for this source we haven't received Syn packet
    // yet.
    if (i != _inbound.end()) {
      // If the remote peer is sending too quickly we refuse to receive
      // and acknowledge the message.
      auto& inbound = i->second;

      if (inbound.acks.try_add(msg.sequence_number)) {
        acknowledge(msg.source, msg.sequence_number);

        if (msg.sequence_number == inbound.last_executed_message + 1) {
          auto was_destroyed = _was_destroyed;

          auto sn = msg.sequence_number;

          // TODO: Is OK for invokation of _on_recv to destroy _on_recv?
          inbound.last_executed_message = sn;
          _on_recv(msg.source, msg.payload);
          if (*was_destroyed) return;

          // This should be the last thing this function does (or you need
          // to check the was_destroyed flag again).
          recursively_apply(++sn, inbound.pending);
        }
        else if (msg.sequence_number > inbound.last_executed_message + 1) {
          inbound.pending.emplace(msg.sequence_number, PendingEntry(move(msg)));
        }
      }
    }
  }
  else if (msg.type == MessageType::unreliable) {
    _on_recv(msg.source, msg.payload);
  }
  else if (msg.type == MessageType::syn) {
    acknowledge(msg.source, msg.sequence_number);

    auto i = _inbound.find(msg.source);

    if (i == _inbound.end()) {
      AckSet acks;
      acks.try_add(msg.sequence_number);
      _inbound[msg.source] = Inbound(msg.sequence_number, acks);
    }
    else {
      auto& inbound = i->second;

      inbound.acks.try_add(msg.sequence_number);

      if (msg.sequence_number == inbound.last_executed_message + 1) {
        inbound.last_executed_message = msg.sequence_number;
        recursively_apply(msg.sequence_number + 1, inbound.pending);
      }
      else if (msg.sequence_number > inbound.last_executed_message + 1) {
        inbound.pending.emplace(msg.sequence_number, PendingEntry(move(msg)));
      }
    }
  }
  else {
    // TODO: Disconnect from the sender.
    assert(0);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::release( boost::optional<Id> unreliable_id
                      , std::shared_ptr<Message>&& m) {
  // TODO: Split these in two functions.

  // For reliable messages, we only treat as reliable those that originated
  // here. Also, we don't store unreliable messages that did not originate
  // here in _unreliable_messages because we don't want this user to change
  // them anyway.
  if (m->source != _our_id) return;

  if (!unreliable_id) {
    assert(m->source == _our_id);
    auto i = _reliable_messages.find(m->sequence_number);
    if (i == _reliable_messages.end()) return;
    if (m.use_count() > 1) return; // Someone else still uses this message.
    // TODO: If targets of the message is not empty, we must store it to some
    // other variable (could be called `_orphans`) and remove it from there
    // when we're notified that a node was removed from the network.
    _reliable_messages.erase(i);
  } else {
    auto i = _unreliable_messages.find(*unreliable_id);
    if (i == _unreliable_messages.end()) return;
    if (m.use_count() > 1) return; // Someone else still uses this message.
    _unreliable_messages.erase(i);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::flush(OnFlush on_flush) {
  _on_flush = std::move(on_flush);
}

//------------------------------------------------------------------------------
template<class Id> void Core<Id>::try_flush() {
  if (!_on_flush) return;

  // TODO: We should probably also check that all acks have been sent.

  if (!_reliable_messages.empty() || !_unreliable_messages.empty()) {
    return;
  }

  // TODO: Transports could increment and decrement some counter when sending
  // and finishing sending so that we wouldn't have to iterate here through
  // the transports.
  for (auto t : _transports) {
    if (t->is_sending()) return;
  }

  auto on_flush = std::move(_on_flush);
  on_flush();
}

//------------------------------------------------------------------------------
}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_CORE_H
