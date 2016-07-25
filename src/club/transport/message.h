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
#include "sequence_number.h"

namespace club { namespace transport {

struct ReliableMessage {
  uuid                 source;
  std::set<uuid>       targets;
  SequenceNumber       sequence_number;
  std::vector<uint8_t> bytes;

  ReliableMessage( uuid source
                 , std::set<uuid>&& targets
                 , SequenceNumber   sn
                 , std::vector<uint8_t>&& bytes)
    : source(std::move(source))
    , targets(std::move(targets))
    , sequence_number(sn)
    , bytes(std::move(bytes))
  {}
};

template<typename UnreliableId>
struct UnreliableMessageT {
  uuid                 source;
  std::set<uuid>       targets;
  UnreliableId         id;
  std::vector<uint8_t> bytes;

  UnreliableMessageT( uuid                   source
                    , std::set<uuid>&&       targets
                    , UnreliableId&&         id
                    , std::vector<uint8_t>&& bytes)
    : source(std::move(source))
    , targets(std::move(targets))
    , id(std::move(id))
    , bytes(std::move(bytes))
  {}
};

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_H
