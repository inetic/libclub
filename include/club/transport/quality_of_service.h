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

#ifndef CLUB_TRANSPORT_QUALITY_OF_SERVICE_H
#define CLUB_TRANSPORT_QUALITY_OF_SERVICE_H

#include <club/debug/log.h>
#include <club/transport/ack_set.h>

namespace club { namespace transport {

// Nomenclature partly used from here LEBDAT specification:
// https://tools.ietf.org/html/rfc6817

class QualityOfService {
  using udp = boost::asio::ip::udp;
  using clock = std::chrono::steady_clock;

public:
  // Sender Maximum Segment Size
  static constexpr int32_t MSS() { return 1452; }
  static constexpr int32_t MIN_CWND() { return 2; }

public:
  size_t next_packet_max_size() const;

  boost::asio::steady_timer::duration
    next_sleep_duration(udp::endpoint);

  size_t payload_header_size() const;

  // Acks are being encoded/decoded separately from the header because
  // header always contains and increments the sequence number. But
  // if only acks are sent then the sequence number must not be
  // incremented (because acks are not acked).
  size_t encoded_acks_size() const;
  void encode_acks(binary::encoder&);
  void decode_acks(binary::decoder&);

  void encode_header(binary::encoder&);
  void decode_header(binary::decoder&);

  void encode_payload_header(binary::encoder&, size_t packet_size);
  void decode_payload_header(binary::decoder&);

  const std::set<uint32_t>& acks() const { return _acks; }

  int32_t bytes_in_flight() const;
  void clear_in_flight_info() { _in_flight.clear(); }

  size_t cwnd() const { return _cwnd; }

  clock::duration rtt() const { return _rtt; }

private:
  uint64_t time_since_start_mks() const;
  void update_rtt(clock::duration last_rtt);

private:
  friend std::ostream& operator<<(std::ostream&, const QualityOfService&);

  static constexpr int64_t invalid_ts = std::numeric_limits<int64_t>::max();

  const clock::time_point _time_started = clock::now();
  int64_t _last_recv_packet_time = invalid_ts;
  int64_t _base_delay = invalid_ts;
  int32_t _next_seq_nr = 0;
  int32_t _ack_nr = 0;
  int32_t _cwnd = MIN_CWND() * MSS();
  float _bytes_newly_acked = 0;
  int64_t _bytes_sent_total = 0;
  boost::optional<uint32_t> _last_received_ack;

  clock::duration _rtt = std::chrono::milliseconds(500);

  struct PacketInfo {
    uint32_t size;
    clock::time_point send_time;
  };

  //          +--> seq_num
  //          |
  //          |
  std::map<uint32_t, PacketInfo> _in_flight;
  std::set<uint32_t> _acks;

public:
  // TODO: Not too happy that this variable is here (and that it is
  // public). Should it not be in TransmitQueue instead?
  AckSet _received_message_ids_by_peer;
};

//--------------------------------------------------------------------
// Implementation
//--------------------------------------------------------------------
inline void QualityOfService::update_rtt(clock::duration last_rtt) {
  using namespace std::chrono;
  _rtt = duration_cast<clock::duration>(0.75 * _rtt + 0.25 * last_rtt);
}

//--------------------------------------------------------------------
inline uint64_t
QualityOfService::time_since_start_mks() const {
  using namespace std::chrono;
  return duration_cast<microseconds>(clock::now() - _time_started).count();
}

//--------------------------------------------------------------------
inline int32_t
QualityOfService::bytes_in_flight() const {
  uint32_t retval = 0;
  for (auto p : _in_flight) retval += p.second.size;
  return retval;
}

//--------------------------------------------------------------------
inline size_t QualityOfService::encoded_acks_size() const {
  return 2 + _acks.size() * 4;
}

//--------------------------------------------------------------------
inline void QualityOfService::encode_acks(binary::encoder& e) {
  e.put(uint16_t(_acks.size()));
  for (auto i = _acks.begin(); i != _acks.end();) {
    e.put(*i);
    i = _acks.erase(i);
  }
}

//--------------------------------------------------------------------
inline void QualityOfService::decode_acks(binary::decoder& d) {
  _bytes_newly_acked = 0;

  bool data_loss_detected = false;

  auto now = clock::now();
  auto count = d.get<uint16_t>();
  for (uint16_t _i = 0; _i < count; ++_i) {
    auto sn = d.get<uint32_t>();
    assert(!d.error());

    if (_last_received_ack) {
      if (sn != *_last_received_ack + 1) {
        data_loss_detected = true;

        for (auto j = _in_flight.begin(); j != _in_flight.end();) {
          if (j->first >= sn) break;
          j = _in_flight.erase(j);
        }
      }
    }
    _last_received_ack = sn;

    auto i = _in_flight.find(sn);
    if (i != _in_flight.end()) {
      _bytes_newly_acked += i->second.size;
      update_rtt(now - i->second.send_time);
      _in_flight.erase(i);
    }
  }

  //std::cout << "-----------------------" << std::endl;
  if (data_loss_detected) {
    _cwnd = std::min(_cwnd, std::max(_cwnd/2, MIN_CWND() * MSS()));
  }
}

//--------------------------------------------------------------------
inline size_t
QualityOfService::payload_header_size() const {
  return sizeof(_next_seq_nr);
}

//--------------------------------------------------------------------
inline size_t
QualityOfService::next_packet_max_size() const {
  auto diff = _cwnd - bytes_in_flight();
  if (diff <= 0) return 0;
  auto ret = std::min(MSS(), diff);
  return ret;
}

//--------------------------------------------------------------------
inline
void QualityOfService::encode_payload_header(binary::encoder& e, size_t packet_size) {
  _bytes_sent_total += packet_size;
  auto seq_nr = _next_seq_nr++;
  _in_flight[seq_nr] = PacketInfo{ uint32_t(packet_size)
                                 , clock::now() };
  e.put(seq_nr);
}

//--------------------------------------------------------------------
inline
void QualityOfService::decode_payload_header(binary::decoder& d) {
  auto seq_nr = d.get<uint32_t>();
  if (d.error()) { assert(0); return; }
  _acks.insert(seq_nr);
}

//--------------------------------------------------------------------
inline
void QualityOfService::encode_header(binary::encoder& e) {
  using namespace std::chrono;

  auto now = time_since_start_mks();

  int64_t timestamp_difference_mks
    = _last_recv_packet_time != invalid_ts ? now - _last_recv_packet_time
                                           : invalid_ts;

  e.put(now);
  e.put(timestamp_difference_mks);
}

//--------------------------------------------------------------------
inline
void QualityOfService::decode_header(binary::decoder& d) {
  using namespace std::chrono;

  _last_recv_packet_time = d.get<int64_t>();
  auto timestamp_difference_mks = d.get<int64_t>();

  assert(!d.error());

  if (timestamp_difference_mks == invalid_ts) {
    return;
  }

  if (timestamp_difference_mks < _base_delay) {
    _base_delay = timestamp_difference_mks;
  }

  float our_delay
    = microseconds(timestamp_difference_mks - _base_delay).count()
    / 1'000'000.f;

  constexpr float TARGET = 0.01; // 10ms
  constexpr float GAIN = 1;
  constexpr float ALLOWED_INCREASE = 1;

  float off_target = (TARGET - our_delay) / TARGET;
  float window_factor = _bytes_newly_acked * MSS() / _cwnd;
  auto flight_size = bytes_in_flight();

  auto scaled_gain = GAIN * off_target * window_factor;
  auto max_allowed_cwnd = flight_size + ALLOWED_INCREASE * MSS();

  //club::log(">>> our_delay: ", our_delay);
  _cwnd = std::min(_cwnd + scaled_gain, max_allowed_cwnd);
  _cwnd = std::max<decltype(_cwnd)>(_cwnd, MIN_CWND() * MSS());
}

//--------------------------------------------------------------------
inline std::ostream& operator<<(std::ostream& os, const QualityOfService& qos) {
  os << "(in_flight: " << qos.bytes_in_flight()
                       << " cwnd:" << qos._cwnd
                       << " in_flight.size:" << qos._in_flight.size()
                       << ")";
  return os;
}

}} // namespaces

#endif // ifndef CLUB_TRANSPORT_QUALITY_OF_SERVICE_H

