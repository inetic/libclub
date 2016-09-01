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

#ifndef CLUB_TRANSPORT_OUT_RELAY_MESSAGE_H
#define CLUB_TRANSPORT_OUT_RELAY_MESSAGE_H

#include "out_message.h"

namespace club { namespace transport {

struct OutRelayMessage {
  uuid source;
  std::set<uuid> targets;
  OutMessage out_message;

  OutRelayMessage( uuid source
                 , std::set<uuid>&& targets
                 , OutMessage&& out_message)
    : source(std::move(source))
    , targets(std::move(targets))
    , out_message(std::move(out_message))
  {}

  bool resend_until_acked() const {
    return out_message.resend_until_acked;
  }

  void reset_payload(std::vector<uint8_t>&& new_payload) {
    out_message.reset_payload(std::move(new_payload));
  }

  size_t payload_size() const {
    return out_message.payload_size();
  }
};

}}

#endif // ifndef CLUB_TRANSPORT_OUT_RELAY_MESSAGE_H
