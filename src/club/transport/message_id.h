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

#ifndef CLUB_TRANSPORT_MESSAGE_ID_H
#define CLUB_TRANSPORT_MESSAGE_ID_H

namespace club { namespace transport {

//------------------------------------------------------------------------------
template<typename UnreliableValue>
struct UnreliableMessageId {
  UnreliableValue value;

  bool operator< (const UnreliableMessageId& other) const {
    return value < other.value;
  }
};

//------------------------------------------------------------------------------
struct ReliableMessageId {
  SequenceNumber value;

  bool operator< (const ReliableMessageId& other) const {
    return value < other.value;
  }
};

//------------------------------------------------------------------------------
template<typename UnreliableValue>
using MessageId = boost::variant< ReliableMessageId
                                , UnreliableMessageId<UnreliableValue> >;

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_ID_H
