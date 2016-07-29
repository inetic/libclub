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

namespace club { namespace transport {

template<typename> class TransmitQueue;
template<typename> class Transport;

template<typename UnreliableId>
class Core {
private:
  using OnReceive          = std::function<void(uuid, boost::asio::const_buffer)>;
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

  void on_receive_acks(const uuid&, AckSet);
  void acknowledge(const uuid&, SequenceNumber);

private:
  friend class ::club::transport::Transport<UnreliableId>;
  friend class ::club::transport::TransmitQueue<UnreliableId>;

  void release(boost::optional<UnreliableId>, std::shared_ptr<Message>&&);

  void register_transport(Transport*);
  void unregister_transport(Transport*);
  void forward_message(InMessage&&);

  void add_ack_entry(AckEntry);
  uint8_t encode_acks(binary::encoder& encoder);

  void add_target(std::set<uuid>);

  void on_receive( const boost::system::error_code&
                 , const InMessage*);

private:
  uuid                   _our_id;
  OnReceive              _on_recv;
  SequenceNumber         _next_sequence_number;
  std::set<Transport*>   _transports;
  ReliableMessages       _reliable_messages;
  UnreliableMessages     _unreliable_messages;

  std::map<uuid, AckSet>                 _inbound_acks;
  std::map<uuid, std::map<uuid, AckSet>> _outbound_acks;
  //        |              |
  //        |              +-> from
  //        +-> to
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id> Core<Id>::Core(uuid our_id, OnReceive on_recv)
  : _our_id(std::move(our_id))
  , _on_recv(std::move(on_recv))
  , _next_sequence_number(0) // TODO: Should this be initialized to a random number?
{
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
// TODO: This function needs as an argument targets where these acks
//       are meant to go.
template<class Id>
uint8_t Core<Id>::encode_acks(binary::encoder& encoder) {
  // Need to write at least the size.
  assert(encoder.remaining_size() > 0);

  auto count_encoder = encoder;

  uint8_t count = 0;
  encoder.put(count); // Shall be rewritten later in this func.

  for (auto i = _outbound_acks.begin(); i != _outbound_acks.end();) {
    auto& froms = i->second;

    for (auto j = froms.begin(); j != froms.end();) {
      auto tmp_encoder = encoder;

      encoder.put(AckEntry{i->first, j->first, j->second});

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
  for (auto sn : acks) {

    auto i = _reliable_messages.find(sn);

    if (i != _reliable_messages.end()) {
      if (auto message = i->second.lock()) {
        message->targets.erase(target);
      }
    }
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
Core<Id>::send_reliable( std::vector<uint8_t>&& data
                       , std::set<uuid>         targets) {
  using namespace std;

  auto sn = _next_sequence_number++;

  // TODO: It is inefficient to *copy* the data into the new vector just to
  //       prepend a small header (same below).
  std::vector<uint8_t> data_( 1 // Is reliable flag
                            + sizeof(SequenceNumber)
                            + sizeof(uint16_t) // Size of data
                            + data.size()
                            );

  binary::encoder encoder(data_.data(), data_.size());

  assert(data.size() <= std::numeric_limits<uint16_t>::max());

  encoder.put((uint8_t) 1);
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
    std::vector<uint8_t> data_( 1 // Is unreliable flag
                              + sizeof(Id)
                              + sizeof(uint16_t) // Size of data
                              + data.size()
                              );

    binary::encoder encoder(data_.data(), data_.size());

    auto sn = _next_sequence_number++;

    encoder.put((uint8_t) 0);
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
                                     , msg.type
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
void Core<Id>::on_receive( const boost::system::error_code& error
                         , const InMessage* msg) {
  if (error) {
    assert(0 && "TODO: Handle error");
    return;
  }

  assert(msg);

  if (msg->type == MessageType::reliable) {
    // If the remote peer is sending too fast we refuse to receive
    // and acknowledge the message.
    //
    // TODO: Operator map::operator[] creates an entry in the map,
    //       we should only create the entry once a Syn packet is
    //       exchanged (sockets connect).
    if (_inbound_acks[msg->source].try_add(msg->sequence_number)) {
      _on_recv(msg->source, msg->payload);
    }
  }
  else if (msg->type == MessageType::unreliable) {
    _on_recv(msg->source, msg->payload);
  }
  else {
    assert(0 && "TODO: More msg types will come");
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Core<Id>::release( boost::optional<Id> unreliable_id
                      , std::shared_ptr<Message>&& m) {
  // TODO: Split these in two functions.

  if (!unreliable_id) {
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

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_CORE_H
