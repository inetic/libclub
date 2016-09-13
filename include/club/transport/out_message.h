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
#include <binary/encoder.h>
#include <club/generic/variant_tools.h>
#include <club/transport/sequence_number.h>
#include <club/transport/message_type.h>
#include <club/transport/ack_set.h>
#include <club/transport/in_message_part.h>
#include <club/transport/pending_message.h>

#include <club/debug/ostream_uuid.h>
#include <club/debug/string_tools.h>

namespace club { namespace transport {

//------------------------------------------------------------------------------
struct OutMessage {
private:
  /*
   * This message may contain only a fraction of what the original poster
   * of the message sent. It is due to the message trying to fit into
   * a buffer of size min(MTU size, receiving buffer size).
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

public:
  static constexpr size_t header_size = 11;

  OutMessage( bool                   resend_until_acked
            , MessageType            type
            , SequenceNumber         sequence_number
            , std::vector<uint8_t>&& payload)
    : resend_until_acked(resend_until_acked)
    , _payload_start(0)
    , _header{type, sequence_number, uint16_t(payload.size()), 0, uint16_t(payload.size())}
    , _data(std::move(payload))
    , _is_dirty(false)
  {
    // Currently limited to size 65535 because of Header::original_size
    assert(_data.size() <= std::numeric_limits<uint16_t>::max());
  }

  OutMessage(OutMessage&&) = default;
  OutMessage& operator=(OutMessage&&) = default;
  OutMessage(const OutMessage&) = delete;

  SequenceNumber sequence_number() const { return _header.sequence_number; }

  void reset_payload(std::vector<uint8_t>&& new_payload) {
    // Only reset the _data if no part of the message has already been sent.
    if (_is_dirty) return;

    _payload_start = 0;
    _data = std::move(new_payload);
  }

  size_t payload_size() const {
    return _data.size() - _payload_start;
  }

  // Return the size of the encoded payload.
  uint16_t encode_header_and_payload( binary::encoder& encoder
                                    , uint16_t start) const {
    _is_dirty = true;

    if (encoder.remaining_size() < header_size) {
      encoder.set_error();
      return 0;
    }

    auto s = _payload_start + start;

    const auto payload_size_ = std::min( _data.size() - s
                                       , encoder.remaining_size() - header_size);

    Header h = _header;
    h.start_position += start;
    h.chunk_size = payload_size_;

    h.encode(encoder);

    encoder.put_raw(_data.data() + s, payload_size_);

    return payload_size_;
  }

  const Header& header() const { return _header; }

public:
  const bool resend_until_acked;
  size_t bytes_already_sent = 0;

private:
  size_t _payload_start;
  Header _header;
  std::vector<uint8_t> _data;
  // Once this message or a part of it has been sent, we must not change its
  // content (using the `reset_payload` function above). We use this flag
  // for that.
  mutable bool _is_dirty;
};

//------------------------------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const OutMessage& m) {
  using namespace boost::asio;

  return os << "(OutMessage type:" << m.header().type
            << " sn:" << m.sequence_number() << ")";
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_OUT_MESSAGE_H
