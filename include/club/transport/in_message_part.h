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

#ifndef CLUB_TRANSPORT_IN_MESSAGE_PART_H
#define CLUB_TRANSPORT_IN_MESSAGE_PART_H

#include <set>
#include <club/uuid.h>
#include <club/transport/sequence_number.h>
#include <club/transport/message_type.h>
#include <club/transport/in_message_full.h>

#include <club/debug/ostream_uuid.h>
#include <club/debug/string_tools.h>

namespace club { namespace transport {

//------------------------------------------------------------------------------
struct InMessagePart {
  MessageType               type;
  SequenceNumber            sequence_number;
  size_t                    original_size;
  size_t                    chunk_start;
  size_t                    chunk_size;
  boost::asio::const_buffer payload;
  boost::asio::const_buffer header_and_payload;

  bool is_complete() const;
  boost::optional<InMessageFull> get_complete_message() const;

  InMessagePart() {};

  InMessagePart( MessageType               type
               , SequenceNumber            sequence_number
               , size_t                    original_size
               , size_t                    chunk_start
               , size_t                    chunk_size
               , boost::asio::const_buffer payload
               , boost::asio::const_buffer header_and_payload)
    : type(type)
    , sequence_number(sequence_number)
    , original_size(original_size)
    , chunk_start(chunk_start)
    , chunk_size(chunk_size)
    , payload(payload)
    , header_and_payload(header_and_payload)
  {}
};

//------------------------------------------------------------------------------
inline
bool InMessagePart::is_complete() const {
  return chunk_start == 0 && original_size == chunk_size;
}

//------------------------------------------------------------------------------
inline
boost::optional<InMessageFull> InMessagePart::get_complete_message() const {
  if (!is_complete()) return boost::none;
  return InMessageFull(type, sequence_number, chunk_size, payload);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, InMessagePart& m) {
  if (d.error()) return;

  auto type_start = d.current();

  m.type            = d.get<MessageType>();
  m.sequence_number = d.get<SequenceNumber>();
  m.original_size   = d.get<uint16_t>();
  m.chunk_start     = d.get<uint16_t>();
  m.chunk_size      = d.get<uint16_t>();

  if (d.error()) return;

  if (m.chunk_size > d.size()) {
    return d.set_error();
  }

  using boost::asio::const_buffer;

  auto header_size = d.current() - type_start;

  m.payload = const_buffer(d.current(), m.chunk_size);
  m.header_and_payload = const_buffer(type_start, m.chunk_size + header_size);

  d.skip(m.chunk_size);
}

//------------------------------------------------------------------------------
inline std::ostream& operator<<(std::ostream& os, const InMessagePart& m) {
  using namespace boost::asio;

  os << "(InMessagePart"
     << " type:" << m.type
     << " sn:" << m.sequence_number
     << " orig_size:" << m.original_size
     << " chunk_start:" << m.chunk_start
     << " chunk_size:" << m.chunk_size;

  const_buffer b( buffer_cast<const uint8_t*>(m.payload)
                , std::min<size_t>(10, buffer_size(m.payload)));

  os << " " << str(b) << "x" << buffer_size(m.payload)
     << ")";

  return os;
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_IN_MESSAGE_PART_H
