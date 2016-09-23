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

#ifndef CLUB_TRANSMIT_QUEUE_H
#define CLUB_TRANSMIT_QUEUE_H

#include <list>
#include <boost/variant.hpp>
#include <club/generic/cyclic_queue.h>
#include <club/transport/out_message.h>

namespace club { namespace transport {

class TransmitQueue {
  using clock = std::chrono::steady_clock;

  struct Entry {
    Entry(OutMessage m) : message(std::move(m)) {}

    boost::optional<clock::time_point> last_sent_time;
    OutMessage message;
  };

  using Queue = CyclicQueue<Entry>;
public:
  //----------------------------------------------------------------------------
  void insert(OutMessage m) {
    _bytes_in += m.header().original_size;
    _queue.insert(Entry(std::move(m)));
  }

  bool empty() const { return _queue.empty(); }
  size_t size() const { return _queue.size(); }
  size_t size_in_bytes() const { return _bytes_in; }

  size_t encode_payload(binary::encoder&, AckSet acked, clock::duration);

private:
  bool try_encode(binary::encoder&, Entry&) const;

private:
  size_t _bytes_in = 0;
  Queue _queue;
};

//------------------------------------------------------------------------------
inline size_t TransmitQueue::encode_payload( binary::encoder& encoder
                                           , AckSet acked
                                           , clock::duration duration_threshold) {
  size_t count = 0;

  auto cycle = _queue.cycle();

  using namespace std::chrono_literals;
  auto now = clock::now();
  clock::time_point time_threshold = now - duration_threshold;

  for (auto mi = cycle.begin(); mi != cycle.end();) {
    auto& m = mi->message;
    if (m.resend_until_acked && acked.is_in(m.sequence_number())) {
      _bytes_in -= m.payload_size();
      mi.erase();
      continue;
    }

    if (mi->last_sent_time && *mi->last_sent_time > time_threshold) {
      ++mi;
      continue;
    }

    if (!try_encode(encoder, *mi)) {
      break;
    }

    if (m.fully_sent()) {
      mi->last_sent_time = now;
    }

    ++count;

    if (m.bytes_already_sent != m.payload_size()) {
      // It means we've exhausted the buffer in encoder.
      break;
    }

    // Unreliable entries are sent only once.
    if (!m.resend_until_acked) {
      _bytes_in -= m.payload_size();
      mi.erase();
      continue;
    }
    
    ++mi;
  }

  return count;
}

//------------------------------------------------------------------------------
inline
bool
TransmitQueue::try_encode(binary::encoder& encoder, Entry& entry) const {

  auto minimal_encoded_size
      = OutMessage::header_size
      // We'd want to send at least one byte of the payload,
      // otherwise what's the point.
      + std::min<size_t>(1, entry.message.payload_size()) ;

  if (minimal_encoded_size > encoder.remaining_size()) {
    return false;
  }

  encoder.put(entry.message);

  assert(!encoder.error());

  return true;
}

//------------------------------------------------------------------------------

}}

#endif // ifndef CLUB_TRANSMIT_QUEUE_H

