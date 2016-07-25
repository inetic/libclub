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

#ifndef CLUB_TRANSPORT_MESSAGE_READER_H
#define CLUB_TRANSPORT_MESSAGE_READER_H

#include <binary/decoder.h>
#include <binary/serialize/uuid.h>

namespace club { namespace transport {

template<class UnreliableId>
class MessageReader {
  using Message             = transport::OutMessage<UnreliableId>;
  using MessageId           = transport::MessageId<UnreliableId>;
  using UnreliableMessageId = transport::UnreliableMessageId<UnreliableId>;

public:
  MessageReader();

  void set_data(const uint8_t* data, size_t);

  boost::optional<InMessage<UnreliableId>> read_one();

private:
  binary::decoder                 _decoder;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
MessageReader<Id>::MessageReader()
{}

template<class Id>
void MessageReader<Id>::set_data(const uint8_t* data, size_t size) {
  _decoder.reset(data, size);
}

template<class Id>
boost::optional<InMessage<Id>> MessageReader<Id>::read_one() {
  using std::move;

  // TODO: See if the number of octets can be reduced.

  if (_decoder.error()) return boost::none;

  auto source = _decoder.get<uuid>();

  if (_decoder.error()) return boost::none;

  auto target_count = _decoder.get<uint8_t>();

  if (_decoder.error()) return boost::none;

  std::set<uuid> targets;

  for (auto i = 0; i < target_count; ++i) {
    targets.insert(_decoder.get<uuid>());
    if (_decoder.error()) return boost::none;
  }

  auto type_start = _decoder.current();

  auto is_reliable = _decoder.get<uint8_t>();

  MessageId message_id;

  if (is_reliable) {
    message_id = ReliableMessageId{_decoder.get<SequenceNumber>()};
  }
  else {
    message_id = UnreliableMessageId{_decoder.get<Id>()};
  }

  if (_decoder.error()) return boost::none;

  auto message_size = _decoder.get<uint16_t>();

  if (_decoder.error()) return boost::none;

  if (message_size > _decoder.size()) {
    return boost::none;
  }

  using boost::asio::const_buffer;

  auto header_size = _decoder.current() - type_start;

  auto payload = const_buffer(_decoder.current(), message_size);
  auto payload_with_type = const_buffer(type_start, message_size + header_size);

  return InMessage<Id>( move(source)
                      , move(targets)
                      , std::move(message_id)
                      , payload
                      , payload_with_type );
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_READER_H
