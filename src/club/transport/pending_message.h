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

namespace club { namespace transport {

struct PendingMessage {
  const uuid                      source;
  const MessageType               type;
  const SequenceNumber            sequence_number;
        size_t                    size;
        boost::asio::const_buffer payload;
        boost::asio::const_buffer type_and_payload;

  std::vector<uint8_t>     data;
  std::map<size_t, size_t> part_info;

  PendingMessage(PendingMessage&&)                 = default;
  PendingMessage(const PendingMessage&)            = delete;
  PendingMessage& operator=(const PendingMessage&) = delete;

  PendingMessage(InMessagePart m);
  PendingMessage(InMessageFull m);

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
  , data( boost::asio::buffer_cast<const uint8_t*>(m.type_and_payload)
        , boost::asio::buffer_cast<const uint8_t*>(m.type_and_payload)
          + boost::asio::buffer_size(m.type_and_payload) )
{
  using boost::asio::const_buffer;
  using boost::asio::buffer_size;

  size_t type_size = buffer_size(m.type_and_payload)
                   - buffer_size(m.payload);

  type_and_payload = const_buffer(data.data(), data.size());
  payload          = const_buffer( data.data() + type_size
                                 , data.size() - type_size);

  part_info.emplace(m.chunk_start, m.chunk_size);
}

//------------------------------------------------------------------------------
inline
PendingMessage::PendingMessage(InMessageFull m)
  : source(std::move(m.source))
  , type(m.type)
  , sequence_number(m.sequence_number)
  , size(m.size)
  , data( boost::asio::buffer_cast<const uint8_t*>(m.type_and_payload)
        , boost::asio::buffer_cast<const uint8_t*>(m.type_and_payload)
          + boost::asio::buffer_size(m.type_and_payload) )
{
  using boost::asio::const_buffer;
  using boost::asio::buffer_size;

  size_t type_size = buffer_size(m.type_and_payload)
                   - buffer_size(m.payload);

  m.type_and_payload = const_buffer(data.data(), data.size());
  m.payload          = const_buffer( data.data() + type_size
                                   , data.size() - type_size);
}

//------------------------------------------------------------------------------
inline
bool PendingMessage::is_full() const {
  if (part_info.empty()) return true;
  return part_info.begin()->first == 0 && part_info.begin()->second == size;
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

