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

struct ReliableBroadcastId {
  SequenceNumber number;

  bool operator < (ReliableBroadcastId other) const {
    return number < other.number;
  }
};

template<class UnreliableId>
struct UnreliableBroadcastId {
  UnreliableId number;

  bool operator < (UnreliableBroadcastId other) const {
    return number < other.number;
  }
};

struct ForwardId {
  bool operator < (ForwardId other) const {
    // Currently we're not storing this in outgoing messages.
    // In the future it would be nice to store it there so that
    // we'd know when trying to send duplicates.
    assert(0);
  }
};

template<class UnreliableId>
using MessageId = boost::variant< ReliableBroadcastId
                                , UnreliableBroadcastId<UnreliableId>
                                , ForwardId
                                >;


}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_ID_H
