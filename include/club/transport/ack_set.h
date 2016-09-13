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

#ifndef CLUB_TRANSPORT_ACK_SET_H
#define CLUB_TRANSPORT_ACK_SET_H

#include <bitset>
#include <binary/encoder.h>
#include <binary/decoder.h>
#include <binary/encoded.h>

#include "sequence_number.h"
#include "club/debug/ostream_uuid.h"

namespace binary { class decoder; }

namespace club { namespace transport {

class AckSet {
public:
  AckSet();
  AckSet(SequenceNumber);

  bool try_add(SequenceNumber new_sn);
  bool can_add(SequenceNumber new_sn) const;
  bool is_in(SequenceNumber sn) const;
  bool empty() const { return bitmask == 0; }

  struct iterator {
    iterator(const AckSet& acks, int _pos)
      : acks(acks), pos(_pos) {
      for (;pos != -1; --pos) {
        if (acks.get_bit(pos) == 1) {
          break;
        }
      }
    }

    SequenceNumber operator*() const {
      assert(!acks.empty());
      return acks.highest_sequence_number - pos;
    }

    iterator& operator++() {// prefix
      --pos;
      for (;pos >= 0; --pos) {
        if (acks.get_bit(pos) == 1) {
          break;
        }
      }
      return *this;
    }

    iterator operator++(int) {// postfix
      auto ret = *this;
      ++(*this);
      return ret;
    }

    bool operator==(const iterator& other) const {
      return &acks == &other.acks && pos == other.pos;
    }

    bool operator!=(const iterator& other) const {
      return !(*this == other);
    }

    const AckSet& acks;
    int pos;
  };

  iterator begin() const {
    if (empty()) return end();
    return iterator(*this, 31);
  }

  iterator end() const { return iterator(*this, -1); }

  void set_bit(int p) {
    assert(0 <= p && p < 32);
    bitmask |= 1 << p;
  }

  bool get_bit(int p) const {
    assert(0 <= p && p < 32);
    return bitmask & (1 << p);
  }

private:
  friend std::ostream& operator<<(std::ostream&, const AckSet&);
  friend void decode(binary::decoder&, AckSet&);
  template<typename Encoder> friend void encode(Encoder&, const AckSet&);

  SequenceNumber highest_sequence_number;
  SequenceNumber lowest_sequence_number;

  // i'th bit is set <=> (highest_sequence_number - i) belongs to the set
  uint32_t bitmask;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline AckSet::AckSet()
  : highest_sequence_number(0)
  , lowest_sequence_number(0)
  , bitmask(0)
{}

inline AckSet::AckSet(SequenceNumber sn)
  : highest_sequence_number(sn)
  , lowest_sequence_number(sn)
  , bitmask(0)
{ }

inline bool AckSet::try_add(SequenceNumber new_sn) {
  if (bitmask == 0) {
    highest_sequence_number = new_sn;
    lowest_sequence_number = new_sn;
    set_bit(0);
    return true;
  }
  else {
    auto hsn = highest_sequence_number;

    if (new_sn == hsn) return true;
    if (new_sn < hsn) {
      if (new_sn < hsn - 31) return true;
      set_bit(hsn - new_sn);
      return true;
    }
    else {
      if (new_sn > hsn + 31) {
        return false;
      }

      auto shift = new_sn - hsn;

      for (decltype(shift) i = 31; i > 31 - shift; --i) {
        if (get_bit(i) == 1 || (hsn <= lowest_sequence_number + i)) {
          continue;
        }
        return false;
      }

      bitmask <<= shift;
      set_bit(0);
      highest_sequence_number = new_sn;
      return true;
    }
  }
}

inline bool AckSet::is_in(SequenceNumber sn) const {
  if (empty()) return false;
  if (sn > highest_sequence_number) return false;
  if (sn < highest_sequence_number - 31) return true;
  if (sn < lowest_sequence_number) return true;
  return get_bit(highest_sequence_number - sn);
}

inline bool AckSet::can_add(SequenceNumber new_sn) const {
  if (bitmask == 0) {
    return true;
  }
  else {
    auto hsn = highest_sequence_number;

    if (new_sn == hsn) return true;
    if (new_sn < hsn) {
      if (new_sn < hsn - 31) return true;
      return true;
    }
    else {
      if (new_sn > hsn + 31) {
        return false;
      }

      auto shift = new_sn - hsn;

      for (decltype(shift) i = 31; i > 31 - shift; --i) {
        if (get_bit(i) == 1 || (hsn <= lowest_sequence_number + i)) {
          continue;
        }
        return false;
      }

      return true;
    }
  }
}

inline
std::ostream& operator<<(std::ostream& os, const AckSet& acks) {
  os << "(AckSet ";

  if (!acks.empty()) {
    os << "<" << acks.highest_sequence_number << ","
       << acks.lowest_sequence_number << "> "
       << std::bitset<32>(acks.bitmask) << " ";
  }
  else {
    os << "empty ";
  }

  //os << "{";
  //auto cnt = 0;
  //for (auto i = acks.begin(); i != acks.end(); ++i) {
  //  if (cnt++) { os << ", "; }
  //  os << *i;
  //}
  //os << "}";

  return os << ")";
}

}} // club::transport namespace

//------------------------------------------------------------------------------
namespace binary {
  template<> struct encoded<::club::transport::AckSet> {
    static size_t size() {
      return sizeof(::club::transport::SequenceNumber)
           + sizeof(uint32_t);
    }
  };
} // binary namespace

namespace club { namespace transport {

//------------------------------------------------------------------------------
template<typename Encoder>
inline void encode( Encoder& e, const AckSet& ack_set) {
  e.put((SequenceNumber) ack_set.highest_sequence_number);
  e.put(ack_set.bitmask);
}

//------------------------------------------------------------------------------
inline void decode(binary::decoder& d, AckSet& ack_set) {
  if (d.error()) return;

  ack_set.highest_sequence_number = d.get<SequenceNumber>();
  ack_set.bitmask = d.get<uint32_t>();
}

//------------------------------------------------------------------------------

}} // club::transport namespace
#endif // ifndef CLUB_TRANSPORT_ACK_SET_H

