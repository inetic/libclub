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
  InMessagePart        message;
  std::vector<uint8_t> data;

  PendingMessage(PendingMessage&&)                 = default;
  PendingMessage(const PendingMessage&)            = delete;
  PendingMessage& operator=(const PendingMessage&) = delete;

  PendingMessage(InMessagePart m);
  PendingMessage(InMessageFull m);

  boost::optional<InMessageFull> get_full_message() const;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline
PendingMessage::PendingMessage(InMessagePart m)
  : message(std::move(m))
  , data( boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
        , boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
          + boost::asio::buffer_size(message.type_and_payload) )
{
  using boost::asio::const_buffer;
  using boost::asio::buffer_size;

  size_t type_size = buffer_size(message.type_and_payload)
                   - buffer_size(message.payload);

  message.type_and_payload = const_buffer(data.data(), data.size());
  message.payload          = const_buffer( data.data() + type_size
                                         , data.size() - type_size);
}

//------------------------------------------------------------------------------
inline
PendingMessage::PendingMessage(InMessageFull m)
  : message( m.source
           , std::set<uuid>()
           , m.type
           , m.sequence_number
           , m.size
           , 0
           , m.size
           , m.payload
           , m.type_and_payload)
  , data( boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
        , boost::asio::buffer_cast<const uint8_t*>(message.type_and_payload)
          + boost::asio::buffer_size(message.type_and_payload) )
{
  using boost::asio::const_buffer;
  using boost::asio::buffer_size;

  size_t type_size = buffer_size(message.type_and_payload)
                   - buffer_size(message.payload);

  message.type_and_payload = const_buffer(data.data(), data.size());
  message.payload          = const_buffer( data.data() + type_size
                                         , data.size() - type_size);
}

//------------------------------------------------------------------------------
inline
boost::optional<InMessageFull> PendingMessage::get_full_message() const {
  assert(message.is_full());

  return InMessageFull( message.source
                      , message.type
                      , message.sequence_number
                      , message.original_size
                      , message.payload
                      , message.type_and_payload);
}

//------------------------------------------------------------------------------
}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_PENDING_MESSAGE_H

