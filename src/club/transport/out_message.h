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
   * |T|  SN   |OS |ST |EN |
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
  static const size_t header_size = 11;
  static const size_t pos_OS      = 5;
  static const size_t pos_ST      = 7;
  static const size_t pos_CS      = 9;

  // When we receive payload from a user, we need to prepend the header
  // data to it (it would be inefficient to concatenate the two).
  struct HeaderAndPayloadSeparate {
    std::array<uint8_t, header_size> header;
    std::vector<uint8_t>             payload;

    void reset_payload(std::vector<uint8_t>&& new_payload) {
      assert(new_payload.size() < std::numeric_limits<uint16_t>::max());

      binary::encoder e( header.data() + pos_OS
                       , header.data() + sizeof(uint16_t));

      e.put((uint16_t) new_payload.size());

      payload = std::move(new_payload);
    }

    uint16_t encode(binary::encoder& e, uint16_t start) {
      // A bit of machinery because we need to encode correct ST and EN
      binary::decoder header_d(header);
      binary::encoder header_e(e);

      header_d.skip(pos_ST);
      auto cur_start = header_d.get<uint16_t>();
      auto cur_size  = header_d.get<uint16_t>();

      assert(cur_size == payload.size());
      assert(cur_size >= start);

      auto size = std::min<uint16_t>( cur_size - start
                                    , e.remaining_size() - header.size());

      e.put_raw(header.data(),  header.size());
      e.put_raw(payload.data() + start, size);

      // Correct the header.
      header_e.skip(pos_ST);
      header_e.put((uint16_t) (cur_start + start));
      header_e.put((uint16_t) size);

      return size;
    }
  };

  // When we receive a message from the network and need to forward it,
  // the message data contains the header and the payload.
  struct HeaderAndPayloadCombined {
    std::vector<uint8_t> header_and_payload;

    uint16_t encode(binary::encoder& e, uint16_t start) {
      // A bit of machinery because we need to encode correct ST and EN
      binary::decoder header_d(header_and_payload);
      binary::encoder header_e(e);

      header_d.skip(pos_ST);
      auto cur_start = header_d.get<uint16_t>();
      auto cur_size  = header_d.get<uint16_t>();

      assert(cur_size == header_and_payload.size() - header_size);

      auto size = std::min<uint16_t>(cur_size - start, e.remaining_size());

      e.put_raw(header_and_payload.data(), header_and_payload.size());

      // Correct the header.
      header_e.skip(pos_ST);
      header_e.put((uint16_t) (cur_start + start));
      header_e.put((uint16_t) size);

      return size;
    }
  };

  using HeaderAndPayload = boost::variant< HeaderAndPayloadSeparate
                                         , HeaderAndPayloadCombined >;

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , bool                   resend_until_acked
            , MessageType            type
            , SequenceNumber         sequence_number
            , std::vector<uint8_t>&& payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , resend_until_acked(resend_until_acked)
    , data(HeaderAndPayloadSeparate())
    , _is_dirty(false)
  {
    auto& data_ = *boost::get<HeaderAndPayloadSeparate>(&data);

    assert(data_.payload.size() <= std::numeric_limits<uint16_t>::max());

    binary::encoder e( data_.header.data()
                     , data_.header.data() + data_.header.size());

    e.put(type);
    e.put(sequence_number);
    e.put((uint16_t) payload.size());
    e.put((uint16_t) 0);
    e.put((uint16_t) payload.size());

    assert(!e.error() && e.written() == header_size);

    data_.payload = std::move(payload);
  }

  OutMessage( uuid                   source
            , std::set<uuid>&&       targets
            , bool                   resend_until_acked
            , std::vector<uint8_t>&& header_and_payload)
    : source(std::move(source))
    , targets(std::move(targets))
    , resend_until_acked(resend_until_acked)
    , data(HeaderAndPayloadCombined{std::move(header_and_payload)})
    , _is_dirty(false)
  { }

  void reset_payload(std::vector<uint8_t>&& new_payload) {
    // Only reset the data if no part of the message has already been sent.
    if (_is_dirty) return;

    return match(data
                , [&](const HeaderAndPayloadSeparate& data) {
                    const_cast<HeaderAndPayloadSeparate&>(data)
                      .reset_payload(std::move(new_payload));
                  }
                , [](const HeaderAndPayloadCombined& data) {
                    assert(0 && "Can't reset forwarded data");
                  });
  }

  size_t payload_size() const {
    return match(data
                , [=](const HeaderAndPayloadSeparate& data) {
                    return data.payload.size();
                  }
                , [=](const HeaderAndPayloadCombined& data) {
                    return data.header_and_payload.size() - header_size;
                  });
  }

  size_t header_and_payload_size() const {
    return match(data
                , [=](const HeaderAndPayloadSeparate& data) {
                    return data.header.size() + data.payload.size();
                  }
                , [=](const HeaderAndPayloadCombined& data) {
                    return data.header_and_payload.size();
                  });
  }

  // Return the size of the encoded payload.
  uint16_t encode_header_and_payload( binary::encoder& encoder
                                    , uint16_t start) const {
    _is_dirty = true;

    return match(data
                , [&](const HeaderAndPayloadSeparate& data) {
                    auto& data_ = const_cast<HeaderAndPayloadSeparate&>(data);
                    return data_.encode(encoder, start);
                  }
                , [&](const HeaderAndPayloadCombined& data) {
                    auto& data_ = const_cast<HeaderAndPayloadCombined&>(data);
                    return data_.encode(encoder, start);
                  });
  }

public:
  // TODO: This started as a simple structure so it was OK to have these public
  //       but now it's quite more complex so try to make them private.
  const uuid             source;
        std::set<uuid>   targets;
        bool             resend_until_acked;
        HeaderAndPayload data;

private:
  // Once this message or a part of it has been sent, we must not change its
  // content (using the `reset_payload` function above). We use this flag
  // for that.
  mutable bool _is_dirty;
};

//------------------------------------------------------------------------------

inline std::ostream& operator<<(std::ostream& os, const OutMessage& m) {
  using namespace boost::asio;

  os << "(OutMessage src:" << m.source
     << " targets: " << str(m.targets) << " ";

  match(m.data, [&os](const OutMessage::HeaderAndPayloadSeparate& data) {
                  os << str(data.header) << " ";

                  if (data.payload.size() < 10) {
                    os << str(data.payload);
                  }
                  else {
                    auto p = data.payload.data();

                    os << str(const_buffer(p, 10))
                       << "...x" << data.payload.size();
                  }
                }
              , [&os](const OutMessage::HeaderAndPayloadCombined& data) {
                  os << str(data.header_and_payload);
                });

  return os << ")";
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_OUT_MESSAGE_H
