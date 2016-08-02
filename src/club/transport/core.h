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
#include "transport/in_message.h"
#include "transport/out_message.h"
#include "transport/ack_set.h"
#include "transport/ack_entry.h"
#include "transport/outbound_acks.h"

#include <club/debug/ostream_uuid.h>
#include "debug/string_tools.h"
#include "message_id.h"

namespace club { namespace transport {

template<typename> class TransmitQueue;
template<typename> class Transport;

template<typename UnreliableId>
class Core {
private:
  using OnReceive  = std::function<void(uuid, boost::asio::const_buffer)>;
  using OnFlush    = std::function<void()>;
  using Queue      = TransmitQueue<UnreliableId>;
  using OutMessage = transport::OutMessage;
  using Transport  = transport::Transport<UnreliableId>;
  using MessageId  = transport::MessageId<UnreliableId>;
  using Messages   = std::map<MessageId, std::weak_ptr<OutMessage>>;

  using UnreliableBroadcastId = transport::UnreliableBroadcastId<UnreliableId>;


public:
  Core(uuid our_id, OnReceive);

  void broadcast_reliable(std::vector<uint8_t>&& data);

  void broadcast_unreliable( UnreliableId           id
                           , std::vector<uint8_t>&& data
                           , std::set<uuid>         targets);

  void broadcast_unreliable( UnreliableId           id
                           , std::vector<uint8_t>&& data);

  const uuid& id() const { return _our_id; }

  void flush(OnFlush);

  ~Core();

private:
  friend class ::club::transport::Transport<UnreliableId>;
  friend class ::club::transport::TransmitQueue<UnreliableId>;

  void release(MessageId, std::shared_ptr<OutMessage>&&);

  void register_transport(Transport*);
  void unregister_transport(Transport*);
  void forward_message(const InMessage&);

  void add_ack_entry(AckEntry);
  uint8_t encode_acks(binary::encoder& encoder, const std::set<uuid>& targets);

  void add_target(uuid);

  void on_receive(InMessage);

  void on_receive_acks(const uuid&, AckSet);
  void acknowledge(const uuid&, AckSet::Type, SequenceNumber);

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

  struct NodeData {
    struct Sync {
      SequenceNumber last_executed_message;
      AckSet         acks;
    };

    boost::optional<Sync> sync;
    PendingMessages       pending;
  };

  template<class Key, class Value>
  static
  std::set<Key> keys(const std::map<Key, Value>& map) {
    std::set<Key> retval;
    for (const auto& pair : map)
      retval.insert(retval.end(), pair.first);
    return retval;
  }

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
  Messages               _messages;
  OnFlush                _on_flush;

  std::map<uuid, NodeData>  _nodes;
  OutboundAcks              _outbound_acks;

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
  , _outbound_acks(_our_id)
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
  return _outbound_acks.encode_few(encoder, targets);
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::add_ack_entry(AckEntry entry) {
  _outbound_acks.add_ack_entry(std::move(entry));
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::acknowledge(const uuid& from
                          , AckSet::Type type
                          , SequenceNumber sn) {
  _outbound_acks.acknowledge(from, type, sn);
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::on_receive_acks(const uuid& target, AckSet acks) {
  bool acked_some = false;

  for (auto sn : acks) {
    MessageId mid;

    switch (acks.type()) {
      case AckSet::Type::directed:  mid = ReliableDirectedId{target, sn}; break;
      case AckSet::Type::broadcast: mid = ReliableBroadcastId{sn};        break;
      default: assert(0); return;
    }

    auto i = _messages.find(mid);

    if (i != _messages.end()) {
      if (auto message = i->second.lock()) {
        message->targets.erase(target);

        if (message->targets.empty()) {
          _messages.erase(i);
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
Core<Id>::broadcast_reliable(std::vector<uint8_t>&& data) {
  using namespace std;

  auto sn = _next_reliable_broadcast_number++;

  auto type = MessageType::reliable_broadcast;

  auto message = make_shared<OutMessage>( _our_id
                                        , keys(_nodes)
                                        , type
                                        , sn
                                        , move(data)
                                        );

  ReliableBroadcastId id{sn};

  _messages.emplace(id, message);

  for (auto t : _transports) {
    t->insert_message(id, true, message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::add_target(uuid new_target) {
  using namespace std;

  auto inserted = _nodes.emplace(new_target, NodeData()).second;

  if (inserted) {
    auto sn = _next_reliable_broadcast_number;

    auto message = make_shared<OutMessage>( _our_id
                                          , set<uuid>{new_target}
                                          , MessageType::syn
                                          , sn
                                          , std::vector<uint8_t>());

    ReliableDirectedId id{std::move(new_target), sn};

    _messages.emplace(id, message);

    for (auto t : _transports) {
      t->insert_message(id, true, message);
    }
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::broadcast_unreliable( Id                     id
                                   , std::vector<uint8_t>&& data) {
  using namespace std;

  auto i = _messages.find(UnreliableBroadcastId{id});

  if (i != _messages.end()) {
    if (auto p = i->second.lock()) {
      p->reset_payload(std::move(data));
    }
    // else { it was there but was already sent, so noop }
  }
  else {
    auto sn   = _next_message_number++;
    auto type = MessageType::unreliable_broadcast;

    auto message = make_shared<OutMessage>( _our_id
                                          , keys(_nodes)
                                          , type
                                          , sn
                                          , move(data)
                                          );

    UnreliableBroadcastId mid{id};

    _messages.emplace(mid, move(message));

    for (auto t : _transports) {
      t->insert_message(mid, false, message);
    }
  }

}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::forward_message(const InMessage& msg) {
  using namespace std;

  auto begin = boost::asio::buffer_cast<const uint8_t*>(msg.type_and_payload);
  auto size  = boost::asio::buffer_size(msg.type_and_payload);

  std::vector<uint8_t> data(begin, begin + size);

  auto message = make_shared<OutMessage>( msg.source
                                        , set<uuid>(msg.targets)
                                        //, msg.type
                                        //, msg.sequence_number
                                        , move(data) );

  // TODO: Same as with unreliable messages, store the message in a
  // std::map so that we don't put identical messages to message queues
  // more than once.

  ForwardId id;

  for (auto t : _transports) {
    t->insert_message(id, false, message);
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

  auto i = _nodes.find(msg.source);

  // If there is no AckSet for this source we attempted to establish
  // connection with it.
  if (i == _nodes.end()) {
    return;
  }

  auto &node = i->second;

  if (msg.type == MessageType::reliable_broadcast) {
    // Have we received syn packet yet?
    if (!node.sync) {
      return;
    }

    // If the remote peer is sending too quickly we refuse to receive
    // and acknowledge the message.
    if (node.sync->acks.try_add(msg.sequence_number)) {
      acknowledge(msg.source, AckSet::Type::broadcast, msg.sequence_number);

      if (msg.sequence_number == node.sync->last_executed_message + 1) {
        auto was_destroyed = _was_destroyed;

        auto sn = msg.sequence_number;

        // TODO: Is OK for invokation of _on_recv to destroy _on_recv?
        node.sync->last_executed_message = sn;
        _on_recv(msg.source, msg.payload);
        if (*was_destroyed) return;

        // This should be the last thing this function does (or you need
        // to check the was_destroyed flag again).
        recursively_apply(++sn, node.pending);
      }
      else if (msg.sequence_number > node.sync->last_executed_message + 1) {
        node.pending.emplace(msg.sequence_number, PendingEntry(move(msg)));
      }
    }
  }
  else if (msg.type == MessageType::unreliable_broadcast) {
    _on_recv(msg.source, msg.payload);
  }
  else if (msg.type == MessageType::syn) {
    acknowledge(msg.source, AckSet::Type::directed, msg.sequence_number);

    if (!node.sync) {
      node.sync = typename NodeData::Sync{ msg.sequence_number-1
                                         , AckSet( AckSet::Type::broadcast
                                                 , msg.sequence_number-1)};
    }
  }
  else {
    // TODO: Disconnect from the sender.
    assert(0);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::release( MessageId message_id
                      , std::shared_ptr<OutMessage>&& m) {
  // For reliable messages, we only treat as reliable those that originated
  // here. Also, we don't store unreliable messages that did not originate
  // here in _messages because we don't want this user to change
  // them anyway.
  if (m->source != _our_id) return;

  auto i = _messages.find(message_id);
  if (i == _messages.end()) return;
  if (m.use_count() > 1) return; // Someone else still uses this message.

  // TODO: In case of reliable messages, if the `targets` variable of the
  // message is not empty, we must store it to some other collection (could be
  // called `_orphans`) and remove it from there when we're notified that a
  // node was removed from the network.

  _messages.erase(i);
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

  if (!_messages.empty()) {
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
