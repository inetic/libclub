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

namespace club { namespace transport {

// Nomenclature partly used from here LEBDAT specification:
// https://tools.ietf.org/html/rfc6817

class QualityOfService {
  using udp = boost::asio::ip::udp;
  using clock = std::chrono::steady_clock;

public:
  // Sender Maximum Segment Size
  static constexpr size_t MSS() { return 1452; }
  static constexpr size_t MIN_CWND() { return 2; }

public:
  size_t next_packet_max_size() const;

  boost::asio::steady_timer::duration
    next_sleep_duration(udp::endpoint);

  size_t header_size() const;

  // Acks are being encoded/decoded separately from the header because
  // header always contains and increments the sequence number. But
  // if only acks are sent then the sequence number must not be
  // incremented (because acks are not acked).
  void encode_acks(binary::encoder&);
  void decode_acks(binary::decoder&);

  void encode_header(binary::encoder&, size_t packet_size);
  void decode_header(binary::decoder&);

  const std::set<uint32_t>& acks() const { return _acks; }

  uint32_t bytes_in_flight() const;

private:
  uint64_t time_since_start_mks() const;

private:
  static constexpr int64_t invalid_ts = std::numeric_limits<int64_t>::max();

  const clock::time_point _time_started = clock::now();
  int64_t _last_recv_packet_time = invalid_ts;
  int64_t _base_delay = invalid_ts;
  int32_t _next_seq_nr = 0;
  int32_t _ack_nr = 0;
  size_t _cwnd = MIN_CWND() * MSS();
  size_t _size_last_sent;
  float _bytes_newly_acked = 0;

  //          +--> seq_num
  //          |         +--> size
  //          |         |
  std::map<uint32_t, uint32_t> _in_flight;
  std::set<uint32_t> _acks;
};

//--------------------------------------------------------------------
// Implementation
//--------------------------------------------------------------------
inline uint64_t
QualityOfService::time_since_start_mks() const {
  using namespace std::chrono;
  return duration_cast<microseconds>(clock::now() - _time_started).count();
}

//--------------------------------------------------------------------
inline uint32_t
QualityOfService::bytes_in_flight() const {
  uint32_t retval = 0;
  for (auto p : _in_flight) retval += p.second;
  return retval;
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

  auto count = d.get<uint16_t>();
  for (uint16_t _i = 0; _i < count; ++_i) {
    auto sn = d.get<uint32_t>();
    assert(!d.error());
    auto i = _in_flight.find(sn);
    if (i != _in_flight.end()) {
      _bytes_newly_acked += i->second;
      _in_flight.erase(i);
    }
  }
}

//--------------------------------------------------------------------
inline size_t
QualityOfService::header_size() const {
  return 8 // timestamp
       + 8 // timestamp_difference_mks
       + 4 // sequence number
       ;
}

//--------------------------------------------------------------------
inline size_t
QualityOfService::next_packet_max_size() const {
  auto diff = _cwnd - bytes_in_flight();
  auto ret = std::min(MSS(), diff);
  return ret;
}

//--------------------------------------------------------------------
inline
void QualityOfService::encode_header(binary::encoder& e, size_t packet_size) {
  using namespace std::chrono;

  auto seq_nr = _next_seq_nr++;

  _size_last_sent = packet_size;

  auto now = time_since_start_mks();

  int64_t timestamp_difference_mks
    = _last_recv_packet_time != invalid_ts ? now - _last_recv_packet_time
                                           : invalid_ts;

  _in_flight[seq_nr] = _size_last_sent;

  e.put(now);
  e.put(timestamp_difference_mks);
  e.put(seq_nr);
}

//--------------------------------------------------------------------
inline
void QualityOfService::decode_header(binary::decoder& d) {
  using namespace std::chrono;

  _last_recv_packet_time = d.get<int64_t>();
  auto timestamp_difference_mks = d.get<int64_t>();
  auto seq_nr = d.get<uint32_t>();

  assert(!d.error());

  _acks.insert(seq_nr);

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

  _cwnd = std::min(_cwnd + scaled_gain, max_allowed_cwnd);
  _cwnd = std::max<decltype(_cwnd)>(_cwnd, MIN_CWND() * MSS());
}

//--------------------------------------------------------------------

}} // namespaces

#endif // ifndef CLUB_TRANSPORT_QUALITY_OF_SERVICE_H

