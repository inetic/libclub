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

#ifndef CLUB_TRANSPORT_INBOUND_MESSAGES_H
#define CLUB_TRANSPORT_INBOUND_MESSAGES_H

#include "ack_set.h"
#include "ack_entry.h"

namespace club { namespace transport {

template<typename UnreliableId>
class InboundMessages {
  using OnReceive = std::function<void(uuid, boost::asio::const_buffer)>;
  using Transport = transport::Transport<UnreliableId>;

public:
  InboundMessages(OnReceive on_recv);

private:
  friend class transport::Transport<UnreliableId>;
  friend class transport::TransmitQueue<UnreliableId>;

  void register_transport(Transport*);
  void deregister_transport(Transport*);

  void on_receive( const boost::system::error_code&
                 , const InMessage*);

private:
  OnReceive _on_recv;
  std::set<Transport*> _transports;
  std::map<uuid, AckSet> _ack_map;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
InboundMessages<Id>::InboundMessages(OnReceive on_recv)
  : _on_recv(std::move(on_recv))
{}

//------------------------------------------------------------------------------
template<class Id> void InboundMessages<Id>::register_transport(Transport* t)
{
  _transports.insert(t);
}

//------------------------------------------------------------------------------
template<class Id> void InboundMessages<Id>::deregister_transport(Transport* t)
{
  _transports.erase(t);
}

//------------------------------------------------------------------------------
template<class Id>
void InboundMessages<Id>::on_receive( const boost::system::error_code& error
                                    , const InMessage* msg) {
  if (error) {
    assert(0 && "TODO: Handle error");
    return;
  }

  assert(msg);

  if (msg->is_reliable) {
    // If the remote peer is sending too fast we refuse to receive
    // and acknowledge the message.
    //
    // TODO: Operator map::operator[] creates an entry in the map,
    //       we should only create the entry once a Syn packet is
    //       exchanged (sockets connect).
    if (_ack_map[msg->source].try_add(msg->sequence_number)) {
      _on_recv(msg->source, msg->payload);
    }
  }
  else {
    _on_recv(msg->source, msg->payload);
  }
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_INBOUND_MESSAGES_H
