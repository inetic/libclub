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

#ifndef CLUB_TRANSPORT_MESSAGE_H
#define CLUB_TRANSPORT_MESSAGE_H

#include <set>
#include <club/uuid.h>
#include "transport/sequence_number.h"
#include "transport/message_type.h"

#include <club/debug/ostream_uuid.h>
#include "debug/string_tools.h"

namespace club { namespace transport {

//------------------------------------------------------------------------------
struct InMessage {
  const uuid                      source;
        std::set<uuid>            targets;
  const MessageType               type;
  const SequenceNumber            sequence_number;
  const boost::asio::const_buffer payload;
  const boost::asio::const_buffer type_and_payload;

  InMessage( uuid                      source
           , std::set<uuid>&&          targets
           , MessageType               type
           , SequenceNumber            sequence_number
           , boost::asio::const_buffer payload
           , boost::asio::const_buffer type_and_payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , type(type)
    , sequence_number(sequence_number)
    , payload(payload)
    , type_and_payload(type_and_payload)
  {}
};

//------------------------------------------------------------------------------
struct OutMessage {
  const uuid                          source;
        std::set<uuid>                targets;
  const MessageType                   type;
  const SequenceNumber                sequence_number;
        std::vector<uint8_t>          bytes;

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , MessageType            type
            , SequenceNumber         sequence_number
            , std::vector<uint8_t>&& bytes)
    : source(std::move(source))
    , targets(std::move(targets))
    , type(type)
    , sequence_number(sequence_number)
    , bytes(std::move(bytes))
  {}
};

//------------------------------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const OutMessage& m) {
  return os << "(OutMessage src:" << m.source
            << " targets: " << str(m.targets)
            << " " << str(m.bytes)
            << ")";
}

inline std::ostream& operator<<(std::ostream& os, const InMessage& m) {
  return os << "(InMessage src:" << m.source
            << " targets: " << str(m.targets)
            << " " << str(m.type_and_payload)
            << ")";
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_H
