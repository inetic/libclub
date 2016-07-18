#ifndef CLUB_TRANSPORT_MESSAGE_READER_H
#define CLUB_TRANSPORT_MESSAGE_READER_H

#include <binary/decoder.h>
#include <binary/serialize/uuid.h>

namespace club { namespace transport {

class MessageReader {
public:
  MessageReader();

  void set_data(const uint8_t* data, size_t);

  bool read_one();

  const std::vector<uuid>&  targets()      const { return _targets; }
  boost::asio::const_buffer message_data() const { return _message; }

  boost::optional<SequenceNumber>
    sequence_number() const { return _sequence_number; }

private:
  binary::decoder                 _decoder;
  std::vector<uuid>               _targets;
  boost::asio::const_buffer       _message;
  boost::optional<SequenceNumber> _sequence_number;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
MessageReader::MessageReader()
{}

void MessageReader::set_data(const uint8_t* data, size_t size) {
  _decoder.reset(data, size);
}

bool MessageReader::read_one() {
  // TODO: See if the number of octets can be reduced.

  if (_decoder.error()) return false;

  auto has_sequence_number = _decoder.get<uint8_t>();

  if (has_sequence_number) {
    _sequence_number = _decoder.get<SequenceNumber>();
  }
  else {
    _sequence_number = boost::none;
  }

  auto target_count = _decoder.get<uint8_t>();

  if (_decoder.error()) return false;

  _targets.resize(target_count);

  for (auto i = 0; i < target_count; ++i) {
    _targets[i] = _decoder.get<uuid>();
  }

  if (_decoder.error()) return false;

  auto message_size = _decoder.get<uint16_t>();

  if (_decoder.error()) return false;

  if (message_size > _decoder.size()) {
    return false;
  }

  _message = boost::asio::const_buffer(_decoder.current(), message_size);

  return true;
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_READER_H
