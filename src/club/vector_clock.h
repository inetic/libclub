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

#ifndef __NET_VCLOCK_H__
#define __NET_VCLOCK_H__

#include <boost/container/flat_map.hpp>
#include <boost/format.hpp>
#include <boost/uuid/uuid.hpp>
#include <binary/encoder.h>
#include <binary/decoder.h>

#include "serialize/flat_map.h"

namespace club {

template<class ID> class VClockT {
public:
  typedef uint32_t           Version;

public:
  Version get_version(ID id) const {
    auto i = _versions.find(id);
    if (i == _versions.end()) {
      return 0;
    }
    return i->second;
  }

  bool operator>>(const VClockT<ID>& other) const {
    bool has_greater_value = false;

    auto i = begin();
    auto j = other.begin();
    auto i_end = end();
    auto j_end = other.end();

    while (true) {
      if (i == i_end) {
        if (j == j_end) return has_greater_value;
        else            return true;
      }

      if (j == j_end) {
        if (i == i_end) return has_greater_value;
        else            return false; // TODO: unless i and everything after is zero
      }

      if (i->first == j->first) {
        if (i->second > j->second) {
          return false;
        }
        if (i->second < j->second) {
          has_greater_value = true;
        }
        ++i; ++j;
      }
      else if (i->first > j->first) {
        if (j->second != 0) {
          has_greater_value = true;
        }
        ++j;
      }
      else {
        if (i->second != 0) {
          return false;
        }
        ++i;
      }
    }
  }

  bool operator>>=(const VClockT<ID>& other) const {
    auto i = begin();
    auto j = other.begin();
    auto i_end = end();
    auto j_end = other.end();

    while (true) {
      if (i == i_end) return true;

      if (j == j_end) {
        if (i == i_end) return true;
        else            return false; // TODO: unless i and everything after is zero
      }

      if (i->first == j->first) {
        if (i->second > j->second) {
          return false;
        }
        ++i; ++j;
      }
      else if (i->first > j->first) {
        ++j;
      }
      else {
        if (i->second != 0) {
          return false;
        }
        ++i;
      }
    }
  }

  bool is_concurrent(const VClockT<ID>& other) const {
    // TODO: Can be done more efficiently.
    return !(*this >>= other) && !(other >>= *this);
  }

  // Clock union.
  VClockT<ID> operator|(const VClockT<ID>& other) const {
    VClockT<ID> result = other;

    for (const auto& id_version_pair : _versions) {
      auto id      = id_version_pair.first;
      auto version = id_version_pair.second;

      result._versions[id] = std::max(version, other.get_version(id));
    }

    return result;
  }

  void operator|=(const VClockT<ID>& other) {
    *this = *this | other;
  }

  // Clock intersection.
  VClockT<ID> operator&(const VClockT<ID>& other) const {
    VClockT<ID> result;

    for (const auto& id_version_pair : _versions) {
      auto id      = id_version_pair.first;
      auto version = id_version_pair.second;

      auto other_is_version_pair_i = other.find(id);

      if (other_is_version_pair_i == other.end()) {
        continue;
      }

      auto other_version = other_is_version_pair_i->second;

      result._versions[id] = std::max(version, other_version);
    }

    return result;
  }

  void operator&=(const VClockT<ID>& other) {
    *this = *this & other;
  }

  Version& operator[](const ID& id) {
    auto iter = _versions.find(id);
    if (iter != _versions.end()) {
      return iter->second;
    }
    Version& ret = _versions[id];
    ret = 0;
    return ret;
  }

  Version increment(const ID& id) {
    auto iter = _versions.find(id);
    if (iter != _versions.end()) {
      return ++(iter->second);
    }
    _versions[id] = 1;
    return 1;
  }

  bool operator < (const VClockT<ID>& other) const {
    if (*this >> other) {
      return true;
    }
    if (other >> *this) {
      return false;
    }
    // They are concurrent.
    return is_smaller_numericaly(other);
  }

  bool operator > (const VClockT<ID>& other) const {
    return other < *this;
  }

  bool operator <= (const VClockT<ID>& other) const {
    return !(*this > other);
  }

  bool operator >= (const VClockT<ID>& other) const {
    return !(*this < other);
  }

  bool is_smaller_numericaly(const VClockT<ID>& other) const {
    auto i = _versions.rbegin();
    auto j = other._versions.rbegin();

    auto iend = _versions.rend();
    auto jend = other._versions.rend();

    while (i != iend || j != jend) {
      if (i == iend) {
        return true;
      }
      if (j == jend) {
        return false;
      }
      if (i->first < j->first) {
        return true;
      }
      if (i->first > j->first) {
        return false;
      }
      if (i->second < j->second) {
        return true;
      }
      if (i->second > j->second) {
        return false;
      }
      ++i; ++j;
    }

    return false;
  }

  typedef boost::container::flat_map<ID, Version> ContainerT;

  typedef typename ContainerT::value_type     value_type;
  typedef typename ContainerT::iterator       iterator;
  typedef typename ContainerT::const_iterator const_iterator;

  const_iterator begin() const { return _versions.begin(); }
  const_iterator end()   const { return _versions.end(); }

  iterator begin() { return _versions.begin(); }
  iterator end()   { return _versions.end(); }

  size_t erase(ID id) {
    return _versions.erase(id);
  }

  const_iterator erase(const_iterator i) {
    return _versions.erase(i);
  }

  const_iterator find(ID id) const {
    return _versions.find(id);
  }

  iterator find(ID id) {
    return _versions.find(id);
  }

  bool empty() const { return _versions.empty(); }

  bool operator == (const VClockT<ID>& other) const {
    auto i_end = end();
    auto j_end = other.end();
    auto i = begin();
    auto j = other.begin();

    while (i != end() || j != other.end()) {
      if (i == i_end) {
        if (j->second != 0) return false;
        ++j; continue;
      }

      if (j == j_end) {
        if (i->second != 0) return false;
        ++i; continue;
      }

      if (i->first == j->first) {
        if (i->second != j->second) return false;
        ++i; ++j; continue;
      }

      if (i->first < j->first) {
        if (i->second != 0) return false;
        ++i; continue;
      }

      if (j->first < i->first) {
        if (j->second != 0) return false;
        ++j; continue;
      }
    }

    return true;
  }

  bool operator != (const VClockT<ID>& other) const {
    return !(*this == other);
  }

private:
  template<class E, class T>
  friend void encode(E&, const VClockT<T>&);
  template<class T>
  friend void decode(binary::decoder&, VClockT<T>&, size_t);

private:
  boost::container::flat_map<ID, Version> _versions;
};

template<class ID>
std::ostream& operator<<(std::ostream& os
                        , const VClockT<ID>& vclock) {
  os << "{";
  for (auto i = vclock.begin(); i != vclock.end(); ++i) {
    os << i->first << ":" << i->second;
    if (i != --vclock.end()) {
      os << " ";
    }
  }
  return os << "}";
}

template<>
inline
std::ostream& operator<<(std::ostream& os
                        , const VClockT<boost::uuids::uuid>& vclock) {
  auto print_id = [&os](boost::uuids::uuid id) {
    int truncated_size = 2;
    os << "[";
    for (auto a : id) {
      os << boost::format("%02x") % (int) a;
      if (--truncated_size == 0) {
        break;
      }
    }
    os << "]";
  };

  os << "{";
  for (auto i = vclock.begin(); i != vclock.end(); ++i) {
    print_id(i->first);
    os << ":" << i->second;
    if (i != --vclock.end()) {
      os << " ";
    }
  }
  return os << "}";
}

typedef VClockT<boost::uuids::uuid> VClock;

//------------------------------------------------------------------------------
template<typename Encoder, typename ID>
inline void encode(Encoder& e, const club::VClockT<ID>& clock) {
  e.template put(clock._versions);
}

//------------------------------------------------------------------------------
template<typename ID>
inline void decode(binary::decoder& d
                  , club::VClockT<ID>& clock
                  , size_t max_element_count) {
  clock._versions = d.get<decltype(clock._versions)>(max_element_count);
}

//------------------------------------------------------------------------------
} // club namespace

#endif // ifndef __NET_VCLOCK_H__
