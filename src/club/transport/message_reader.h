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

#include "ack_entry.h"

namespace club { namespace transport {

class MessageReader {
public:
  MessageReader();

  void set_data(const uint8_t* data, size_t);

  boost::optional<InMessagePart> read_one_message();
  boost::optional<AckEntry>      read_one_ack_entry();

private:
  binary::decoder _decoder;
  binary::decoder _ack_decoder;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
MessageReader::MessageReader()
{}

//------------------------------------------------------------------------------
void MessageReader::set_data(const uint8_t* data, size_t size) {
  if (size < 1) {
    _decoder.set_error();
    _ack_decoder.set_error();
    assert(0);
    return;
  }

  auto ack_set_count = *data;

  size_t acks_data_size = binary::encoded<AckEntry>::size()
                        * ack_set_count;

  if (size - 1 < acks_data_size) {
    _decoder.set_error();
    _ack_decoder.set_error();
    return;
  }

  _ack_decoder.reset(data + 1, acks_data_size);
  _decoder.reset(data + 1 + acks_data_size, size - acks_data_size - 1);
}

//------------------------------------------------------------------------------
boost::optional<AckEntry> MessageReader::read_one_ack_entry() {
  if (_ack_decoder.size() == 0) {
    return boost::none;
  }

  auto entry = _ack_decoder.get<AckEntry>();

  assert(entry.acks.type() != AckSet::Type::unset);

  if (entry.acks.type() == AckSet::Type::unset) {
    _ack_decoder.set_error();
  }

  if (_ack_decoder.error()) return boost::none;

  assert(entry.acks.type() != AckSet::Type::unset);
  return std::move(entry);
}

//------------------------------------------------------------------------------
boost::optional<InMessagePart> MessageReader::read_one_message() {
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

  auto type_start      = _decoder.current();

  auto message_type      = _decoder.get<MessageType>();
  auto sequence_number   = _decoder.get<SequenceNumber>();
  auto orig_message_size = _decoder.get<uint16_t>();
  auto chunk_start       = _decoder.get<uint16_t>();
  auto chunk_size        = _decoder.get<uint16_t>();

  if (_decoder.error()) return boost::none;

  if (chunk_size > _decoder.size()) {
    return boost::none;
  }

  using boost::asio::const_buffer;

  auto header_size = _decoder.current() - type_start;

  auto payload = const_buffer(_decoder.current(), chunk_size);
  auto payload_with_type = const_buffer(type_start, chunk_size + header_size);

  _decoder.skip(chunk_size);

  return InMessagePart( move(source)
                      , move(targets)
                      , message_type
                      , sequence_number
                      , orig_message_size
                      , chunk_start
                      , chunk_size
                      , payload
                      , payload_with_type );
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_READER_H
