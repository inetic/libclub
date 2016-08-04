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

#ifndef CLUB_TRANSPORT_PENDING_MESSAGE_H
#define CLUB_TRANSPORT_PENDING_MESSAGE_H

#include "part_info.h"

namespace club { namespace transport {

struct PendingMessage {
  const uuid                      source;
  const MessageType               type;
  const SequenceNumber            sequence_number;
        size_t                    size;
        boost::asio::const_buffer payload;
        boost::asio::const_buffer type_and_payload;

  std::vector<uint8_t> data;
  PartInfo             part_info;

  PendingMessage(PendingMessage&&)                 = default;
  PendingMessage(const PendingMessage&)            = delete;
  PendingMessage& operator=(const PendingMessage&) = delete;

  PendingMessage(InMessagePart m);
  PendingMessage(InMessageFull m);

  void update_payload(size_t start, boost::asio::const_buffer);

  bool is_full() const;

  boost::optional<InMessageFull> get_full_message() const;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline
PendingMessage::PendingMessage(InMessagePart m)
  : source(std::move(m.source))
  , type(m.type)
  , sequence_number(m.sequence_number)
  , size(m.original_size)
  , data(m.original_size)
{
  update_payload(m.chunk_start, m.payload);
}

//------------------------------------------------------------------------------
inline
PendingMessage::PendingMessage(InMessageFull m)
  : source(std::move(m.source))
  , type(m.type)
  , sequence_number(m.sequence_number)
  , size(m.size)
  , data( boost::asio::buffer_cast<const uint8_t*>(m.payload)
        , boost::asio::buffer_cast<const uint8_t*>(m.payload)
          + boost::asio::buffer_size(m.payload) )
{
  m.payload = boost::asio::const_buffer(data.data() , data.size());
}

//------------------------------------------------------------------------------
inline
void PendingMessage::update_payload(size_t start, boost::asio::const_buffer b) {
  namespace asio = boost::asio;

  asio::mutable_buffer target( data.data() + start
                             , data.size() - start );

  size_t copied = asio::buffer_copy(target, b);

  assert(copied == asio::buffer_size(b));

  part_info.add_part(start, start + copied);
}

//------------------------------------------------------------------------------
inline
bool PendingMessage::is_full() const {
  if (part_info.empty()) return true;
  auto start = part_info.begin()->first;
  return start == 0
      && part_info.begin()->second == size;
}

//------------------------------------------------------------------------------
inline
boost::optional<InMessageFull> PendingMessage::get_full_message() const {
  assert(is_full() && "TODO");

  return InMessageFull( source
                      , type
                      , sequence_number
                      , size
                      , payload
                      , type_and_payload);
}

//------------------------------------------------------------------------------
}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_PENDING_MESSAGE_H

