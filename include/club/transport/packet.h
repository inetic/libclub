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

#ifndef CLUB_TRANSPORT_PACKET_H
#define CLUB_TRANSPORT_PACKET_H

namespace club { namespace transport {

//------------------------------------------------------------------------------
class PacketDecoder {
private:
  QualityOfService& _qos;
  binary::decoder _decoder;
  uint16_t _next_message_i = 0;
  boost::optional<uint16_t> _message_count;

public:
  bool error() const { return _decoder.error(); }

  void decode_header() {
    _qos.decode_header(_decoder);

    bool has_acks = _decoder.get<uint8_t>() != 0;

    if (has_acks) {
      _qos.decode_acks(_decoder);
      auto ack_set = _decoder.get<AckSet>();
      _qos._received_message_ids_by_peer = ack_set;
    }

    _message_count = _decoder.get<uint16_t>();

    if (*_message_count) {
      _qos.decode_payload_header(_decoder);
    }
  }

  boost::optional<InMessagePart> decode_message() {
    if (_next_message_i == *_message_count) return boost::none;
    ++_next_message_i;
    return _decoder.get<InMessagePart>();
  }

  PacketDecoder(QualityOfService& qos, const std::vector<uint8_t>& data)
    : _qos(qos)
    , _decoder(data)
  {
  }
};

//------------------------------------------------------------------------------
inline
boost::optional<size_t> encode_packet( QualityOfService& qos
                                     , TransmitQueue& transmit_queue
                                     , AckSet received_message_ids
                                     , std::vector<uint8_t>& out_packet) {
  size_t minimum_size = qos.encoded_acks_size()
                      + 2*8 /* qos.encode_header */
                      // Additional bytes added by encode_acks()
                      + 3 + binary::encoded<AckSet>::size();

  size_t next_packet_size = std::max( minimum_size
                                    , qos.next_packet_max_size());

  out_packet.resize(next_packet_size);
  binary::encoder encoder(out_packet);

  qos.encode_header(encoder);

  bool has_acks = false;
  {
    if (qos.acks().empty()) {
      encoder.put<uint8_t>(0);
    }
    else {
      encoder.put<uint8_t>(1);
      qos.encode_acks(encoder);
      encoder.put(received_message_ids);
      has_acks = true;
    }
  }

  //club::log("err? ", encoder.error() ? "yes" : "no", " ", encoder.written());
  auto count_encoder = encoder;
  encoder.skip(sizeof(uint16_t));

  if (encoder.error()) {
    assert(0);
    return boost::none;
  }

  // We can only encode qos header after we've known the
  // total size of this packet, so we do it below.
  binary::encoder qos_encoder = encoder;
  encoder.skip(qos.payload_header_size());

  auto count = transmit_queue.encode_payload( encoder
                                            , qos._received_message_ids_by_peer
                                            , qos.rtt() * 2);

  count_encoder.put<uint16_t>(count);

  if (count) {
    qos.encode_payload_header(qos_encoder, encoder.written());
  }

  if (count == 0 && !has_acks) {
    return boost::none;
  }

  return encoder.written();
}

//------------------------------------------------------------------------------
inline
boost::optional<size_t> encode_packet_with_one_message
    ( QualityOfService& qos
    , OutMessage& m
    , AckSet received_message_ids
    , std::vector<uint8_t>& out_packet) {
  binary::encoder encoder(out_packet);

  qos.encode_header(encoder);

  {
    if (qos.acks().empty()) {
      encoder.put<uint8_t>(0);
    }
    else {
      encoder.put<uint8_t>(1);
      qos.encode_acks(encoder);
      encoder.put(received_message_ids);
    }
  }

  assert(!encoder.error());
  encoder.put<uint16_t>(1); // We're sending just one message.

  // We can only encode qos header after we've known the
  // total size of this packet, so we do it below.
  binary::encoder qos_encoder = encoder;
  encoder.skip(qos.payload_header_size());

  encoder.put(m);

  if (encoder.error()) {
    assert(0 && "Shouldn't happen");
    return boost::none;
  }

  qos.encode_payload_header(qos_encoder, encoder.written());
  out_packet.resize(encoder.written());

  return encoder.written();
}

//------------------------------------------------------------------------------
}} // namespaces

#endif // ifndef CLUB_TRANSPORT_PACKET_H

