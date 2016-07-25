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

  bool read_one();

  const uuid&               source()       const { return _source; }
  const std::set<uuid>&     targets()      const { return _targets; }
        std::set<uuid>&     targets()            { return _targets; }
  MessageId                 message_id()   const { return _message_id; }
  boost::asio::const_buffer payload()           const { return _payload; }
  boost::asio::const_buffer payload_with_type() const { return _payload_with_type; }

private:
  binary::decoder                 _decoder;
  uuid                            _source;
  std::set<uuid>                  _targets;
  boost::asio::const_buffer       _payload;
  boost::asio::const_buffer       _payload_with_type;
  MessageId                       _message_id;
  boost::optional<SequenceNumber> _sequence_number;
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
bool MessageReader<Id>::read_one() {
  // TODO: See if the number of octets can be reduced.

  if (_decoder.error()) return false;

  _source = _decoder.get<uuid>();

  if (_decoder.error()) return false;

  auto target_count = _decoder.get<uint8_t>();

  if (_decoder.error()) return false;

  _targets.clear();

  for (auto i = 0; i < target_count; ++i) {
    _targets.insert(_decoder.get<uuid>());
    if (_decoder.error()) return false;
  }

  auto type_start = _decoder.current();

  auto is_reliable = _decoder.get<uint8_t>();

  if (is_reliable) {
    _message_id = ReliableMessageId{_decoder.get<SequenceNumber>()};
  }
  else {
    _message_id = UnreliableMessageId{_decoder.get<Id>()};
  }

  if (_decoder.error()) return false;

  auto message_size = _decoder.get<uint16_t>();

  if (_decoder.error()) return false;

  if (message_size > _decoder.size()) {
    return false;
  }

  using boost::asio::const_buffer;

  _payload = const_buffer(_decoder.current(), message_size);
  _payload_with_type = const_buffer(type_start, message_size + (_decoder.current() - type_start));

  return true;
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_READER_H
