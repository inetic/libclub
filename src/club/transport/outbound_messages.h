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
  using UnreliableMessage = UnreliableMessageT<UnreliableId>;
  using Transport         = ::club::transport::Transport<UnreliableId>;

  using ReliableMessages   = std::map<SequenceNumber, std::weak_ptr<ReliableMessage>>;
  using UnreliableMessages = std::map<UnreliableId,   std::weak_ptr<UnreliableMessage>>;

public:
  OutboundMessages();

  void add_reliable_message( uuid                   source
                           , std::vector<uint8_t>&& data
                           , std::set<uuid>         targets);

  void add_unreliable_message( uuid                   source
                             , UnreliableId           id
                             , std::vector<uint8_t>&& data
                             , std::set<uuid>         targets);

  void acknowledge(const uuid&, SequenceNumber);

private:
  friend class ::club::transport::Transport<UnreliableId>;
  friend class ::club::transport::TransmitQueue<UnreliableId>;

  UnreliableMessage* find_unreliable(const UnreliableId&);

  void release(std::shared_ptr<ReliableMessage>&&);
  void release(std::shared_ptr<UnreliableMessage>&&);

  void register_transport(Transport*);
  void deregister_transport(Transport*);

private:
  SequenceNumber       _next_sequence_number;
  std::set<Transport*> _transports;
  ReliableMessages     _reliable_messages;
  UnreliableMessages   _unreliable_messages;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id> OutboundMessages<Id>::OutboundMessages()
  : _next_sequence_number(0) // TODO: Should this be initialized to a random number?
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
OutboundMessages<Id>::add_reliable_message( uuid                   source
                                          , std::vector<uint8_t>&& data
                                          , std::set<uuid>         targets) {
  using namespace std;

  auto sn = _next_sequence_number++;

  auto message = make_shared<ReliableMessage>( move(source)
                                             , move(targets)
                                             , sn
                                             , move(data)
                                             );

  _reliable_messages.emplace(sn, message);

  for (auto t : _transports) {
    t->insert_message(message);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void
OutboundMessages<Id>::add_unreliable_message( uuid                   source
                                            , Id                     id
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
    auto message = make_shared<UnreliableMessage>( move(source)
                                                 , move(targets)
                                                 , move(id)
                                                 , move(data)
                                                 );

    _unreliable_messages.emplace(move(id), move(message));

    for (auto t : _transports) {
      t->insert_message(message);
    }
  }

}

//------------------------------------------------------------------------------
template<class Id>
void OutboundMessages<Id>::release(std::shared_ptr<ReliableMessage>&& m) {
  auto i = _reliable_messages.find(m->sequence_number);
  if (i == _reliable_messages.end()) return;
  if (m.use_count() > 1) return; // Someone else still uses this message.
  // TODO: If targets of the message is not empty, we must store it to some
  // other variable (could be called `_orphans`) and remove it from there
  // when we're notified that a node was removed from the network.
  _reliable_messages.erase(i);
}

//------------------------------------------------------------------------------
template<class Id>
void OutboundMessages<Id>::release(std::shared_ptr<UnreliableMessage>&& m) {
  auto i = _unreliable_messages.find(m->id);
  if (i == _unreliable_messages.end()) return;
  if (m.use_count() > 1) return; // Someone else still uses this message.
  _unreliable_messages.erase(i);
}

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_OUTBOUND_MESSAGES_H
