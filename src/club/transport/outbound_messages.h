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

#ifndef CLUB_OUTBOUND_MESSAGES_H
#define CLUB_OUTBOUND_MESSAGES_H

#include <map>
#include "transport/message.h"

namespace club { namespace transport {

template<typename> class TransmitQueue;
template<typename> class Transport;

template<typename UnreliableId>
class OutboundMessages {
private:
  using Queue             = TransmitQueue<UnreliableId>;
  using Message           = transport::Message<UnreliableId>;
  using Transport         = ::club::transport::Transport<UnreliableId>;

  using ReliableMessages   = std::map<SequenceNumber, std::weak_ptr<Message>>;
  using UnreliableMessages = std::map<UnreliableId,   std::weak_ptr<Message>>;

public:
  OutboundMessages(uuid our_id);

  void send_reliable( std::vector<uint8_t>&& data
                    , std::set<uuid>         targets);

  void send_unreliable( UnreliableId           id
                      , std::vector<uint8_t>&& data
                      , std::set<uuid>         targets);

  void acknowledge(const uuid&, SequenceNumber);

private:
  friend class ::club::transport::Transport<UnreliableId>;
  friend class ::club::transport::TransmitQueue<UnreliableId>;

  void release(std::shared_ptr<Message>&&);

  void register_transport(Transport*);
  void deregister_transport(Transport*);

  void forward_message( uuid                      source
                      , std::set<uuid>            targets
                      , typename Message::Id      message_id
                      , boost::asio::const_buffer buffer);

private:
  uuid                 _our_id;
  SequenceNumber       _next_sequence_number;
  std::set<Transport*> _transports;
  ReliableMessages     _reliable_messages;
  UnreliableMessages   _unreliable_messages;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id> OutboundMessages<Id>::OutboundMessages(uuid our_id)
  : _our_id(std::move(our_id))
  , _next_sequence_number(0) // TODO: Should this be initialized to a random number?
{
}

//------------------------------------------------------------------------------
template<class Id> void OutboundMessages<Id>::register_transport(Transport* t)
{
  _transports.insert(t);
}

//------------------------------------------------------------------------------
template<class Id> void OutboundMessages<Id>::deregister_transport(Transport* t)
{
  _transports.erase(t);
}

//------------------------------------------------------------------------------
template<class Id>
void OutboundMessages<Id>::acknowledge(const uuid& target, SequenceNumber sn) {
  auto i = _reliable_messages.find(sn);

  if (i != _reliable_messages.end()) {
    if (auto message = i->second.lock()) {
      message->targets.erase(target);
    }
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
OutboundMessages<Id>::send_reliable( std::vector<uint8_t>&& data
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
                                     , sn
                                     , move(data_)
                                     );

  _reliable_messages.emplace(sn, message);

  for (auto t : _transports) {
    t->insert_message(message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
OutboundMessages<Id>::send_unreliable( Id                     id
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

    encoder.put((uint8_t) 0);
    encoder.put(id);
    encoder.put((uint16_t) data.size());
    encoder.put_raw(data.data(), data.size());

    auto message = make_shared<Message>( _our_id
                                       , move(targets)
                                       , typename Message::Unreliable{move(id)}
                                       , move(data_)
                                       );

    _unreliable_messages.emplace(move(id), move(message));

    for (auto t : _transports) {
      t->insert_message(message);
    }
  }

}

//------------------------------------------------------------------------------
template<class Id>
void OutboundMessages<Id>::forward_message( uuid                      source
                                          , std::set<uuid>            targets
                                          , typename Message::Id      message_id
                                          , boost::asio::const_buffer buffer) {
  using namespace std;

  auto begin = boost::asio::buffer_cast<const uint8_t*>(buffer);

  std::vector<uint8_t> data( begin
                           , begin + boost::asio::buffer_size(buffer));

  auto message = make_shared<Message>( move(source)
                                     , move(targets)
                                     , std::move(message_id)
                                     , move(data) );

  // TODO: Same as with unreliable messages, store the message in a
  // std::map so that we don't put identical messages to message queues
  // (through transports).

  for (auto t : _transports) {
    t->insert_message(message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void OutboundMessages<Id>::release(std::shared_ptr<Message>&& m) {
  // TODO: Split these in two functions.

  if (auto p = boost::get<typename Message::Reliable>(&m->id)) {
    auto i = _reliable_messages.find(p->sequence_number);
    if (i == _reliable_messages.end()) return;
    if (m.use_count() > 1) return; // Someone else still uses this message.
    // TODO: If targets of the message is not empty, we must store it to some
    // other variable (could be called `_orphans`) and remove it from there
    // when we're notified that a node was removed from the network.
    _reliable_messages.erase(i);
  } else
  if (auto p = boost::get<typename Message::Unreliable>(&m->id)) {
    auto i = _unreliable_messages.find(p->identifier);
    if (i == _unreliable_messages.end()) return;
    if (m.use_count() > 1) return; // Someone else still uses this message.
    _unreliable_messages.erase(i);
  }
  else { assert(0); }
}

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_OUTBOUND_MESSAGES_H
