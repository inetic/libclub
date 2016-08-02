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

#ifndef CLUB_TRANSPORT_OUT_MESSAGE_H
#define CLUB_TRANSPORT_OUT_MESSAGE_H

#include <set>
#include <club/uuid.h>
#include "generic/variant_tools.h"
#include "transport/sequence_number.h"
#include "transport/message_type.h"

#include <club/debug/ostream_uuid.h>
#include "debug/string_tools.h"

namespace club { namespace transport {

//------------------------------------------------------------------------------
struct OutMessage {
  static const size_t header_size = 7;

  // When we receive payload from a user, we need to prepend the header
  // data to it (it would be inefficient to concatenate the two).
  struct HeaderAndPayload {
    std::array<uint8_t, header_size> header;
    std::vector<uint8_t>             payload;

    void reset_payload(std::vector<uint8_t>&& new_payload) {
      assert(new_payload.size() < std::numeric_limits<uint16_t>::max());

      // Payload size starts at byte #5
      binary::encoder e( header.data() + 5
                       , header.data() + sizeof(uint16_t));

      e.put((uint16_t) new_payload.size());

      payload = std::move(new_payload);
    }
  };

  // When we receive a message from the network and need to forward it,
  // the message data contains the header and the payload.
  struct HeaderAndPayloadCombined {
    std::vector<uint8_t> header_and_payload;
  };

  using Data = boost::variant< HeaderAndPayload
                             , HeaderAndPayloadCombined >;

  const uuid           source;
        std::set<uuid> targets;
        Data           data;

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , MessageType            type
            , SequenceNumber         sequence_number
            , std::vector<uint8_t>&& payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , data(HeaderAndPayload())
  {
    auto& data_ = *boost::get<HeaderAndPayload>(&data);

    assert(data_.payload.size() <= std::numeric_limits<uint16_t>()::max());

    binary::encoder e( data_.header.data()
                     , data_.header.data() + data_.header.size());

    e.put(type);
    e.put(sequence_number);
    e.put((uint16_t) payload.size());

    assert(!e.error() && e.written() == header_size);

    data_.payload = std::move(payload);
  }

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , std::vector<uint8_t>&& header_and_payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , data(HeaderAndPayloadCombined{std::move(header_and_payload)})
  { }

  void reset_payload(std::vector<uint8_t>&& new_payload) {
    // TODO: Once we start supporting message splitting, only reset
    // data if no part of the message has already been sent.
    return match(data
                , [&](const HeaderAndPayload& data) {
                    const_cast<HeaderAndPayload&>(data)
                      .reset_payload(std::move(new_payload));
                  }
                , [](const HeaderAndPayloadCombined& data) {
                    assert(0 && "Can't reset forwarded data");
                  });
  }

  size_t header_and_payload_size() const {
    return match(data
                , [=](const HeaderAndPayload& data) {
                    return data.header.size() + data.payload.size();
                  }
                , [=](const HeaderAndPayloadCombined& data) {
                    return data.header_and_payload.size();
                  });
  }

  void encode_header_and_payload(binary::encoder& encoder) const {
    match(data
         , [&](const HeaderAndPayload& data) {
             encoder.put_raw(data.header.data(),  data.header.size());
             encoder.put_raw(data.payload.data(), data.payload.size());
           }
         , [&](const HeaderAndPayloadCombined& data) {
             encoder.put_raw( data.header_and_payload.data()
                            , data.header_and_payload.size());
           });
  }
};

//------------------------------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const OutMessage& m) {
  os << "(OutMessage src:" << m.source
     << " targets: " << str(m.targets) << " ";

  match(m.data, [&os](const OutMessage::HeaderAndPayload& data) {
                  os << str(data.header) << " " << str(data.payload);
                }
              , [&os](const OutMessage::HeaderAndPayloadCombined& data) {
                  os << str(data.header_and_payload);
                });

  return os << ")";
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_OUT_MESSAGE_H
