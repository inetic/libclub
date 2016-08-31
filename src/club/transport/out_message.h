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
  /*
   * Header:
   *
   *  0 1 2 3 4 5 6 7 8 9 0
   * +-+-+-+-+-+-+-+-+-+-+-+
   * |T|  SN   |OS |ST |CS |
   *
   * T:  MessageType
   * SN: SequenceNumber
   * OS: Original size of the message
   * ST: Start position
   * CS: Chunk size
   *
   * This message may contain only a fraction of what the original poster
   * of the message sent. It is due to the message trying to fit into
   * a buffer of size min(MTU size, receiving buffer size).
   *
   * The number of bytes following this header is equal to CS.
   */
  struct Header {
    MessageType type;
    SequenceNumber sequence_number;
    uint16_t original_size;
    uint16_t start_position;
    uint16_t chunk_size;

    void encode(binary::encoder& e) const {
      e.put(uint8_t(type));
      e.put(sequence_number);
      e.put(original_size);
      e.put(start_position);
      e.put(chunk_size);
    }
  };

  static constexpr size_t header_size = 11;

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , bool                   resend_until_acked
            , MessageType            type
            , SequenceNumber         sequence_number
            , std::vector<uint8_t>&& payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , resend_until_acked(resend_until_acked)
    , data(std::move(payload))
    , payload_start(0)
    , header{type, sequence_number, uint16_t(data.size()), 0, uint16_t(data.size())}
    , _is_dirty(false)
  {
  }

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , bool                   resend_until_acked
            , std::vector<uint8_t>&& header_and_payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , resend_until_acked(resend_until_acked)
    , data(std::move(header_and_payload))
    , payload_start(header_size)
    , _is_dirty(false)
  {
    assert(data.size() >= header_size);

    binary::decoder d(data);

    header.type = d.get<MessageType>();
    header.sequence_number = d.get<SequenceNumber>();
    header.original_size = d.get<uint16_t>();
    header.start_position = d.get<uint16_t>();
    header.chunk_size = d.get<uint16_t>();
  }

  void reset_payload(std::vector<uint8_t>&& new_payload) {
    // Only reset the data if no part of the message has already been sent.
    if (_is_dirty) return;

    payload_start = 0;
    data = std::move(new_payload);
  }

  size_t payload_size() const {
    return data.size() - payload_start;
  }

  size_t header_and_payload_size() const {
    return header_size + payload_size();
  }

  // Return the size of the encoded payload.
  uint16_t encode_header_and_payload( binary::encoder& encoder
                                    , uint16_t start) const {
    _is_dirty = true;

    if (encoder.remaining_size() < header_size) {
      encoder.set_error();
      return 0;
    }

    const auto payload_size_ = std::min( data.size() - payload_start - start
                                       , encoder.remaining_size() - header_size);

    Header h(header);
    h.start_position += start;
    h.chunk_size = payload_size_;

    h.encode(encoder);

    encoder.put_raw(data.data() + payload_start + start, payload_size_);

    return payload_size_;
  }

public:
  const uuid source;
  std::set<uuid> targets;
  bool resend_until_acked;
  std::vector<uint8_t> data;
  size_t payload_start;
  Header header;

private:
  // Once this message or a part of it has been sent, we must not change its
  // content (using the `reset_payload` function above). We use this flag
  // for that.
  mutable bool _is_dirty;
};

//------------------------------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const OutMessage& m) {
  using namespace boost::asio;

  return os << "(OutMessage src:" << m.source
            << " targets: " << str(m.targets) << ")";
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_OUT_MESSAGE_H
