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

#ifndef CLUB_STRING_TOOLS_H
#define CLUB_STRING_TOOLS_H

#include <array>
#include <set>
#include <sstream>
#include <vector>
#include <map>
#include <boost/asio/buffer.hpp>
#include <boost/optional.hpp>

// Convert (almost) anything to string.
template<typename T>
std::string str(const T& a) {
  std::ostringstream s;
  s << a;

  return s.str();
}

template<>
inline std::string str(const std::string& a) { return a; }

// Convert std::pair to string
template<typename T0, typename T1>
std::string str(const std::pair<T0, T1>& a) {
  std::string s;
  s += "(";
  s += str(a.first);
  s += ", ";
  s += str(a.second);
  s += ")";

  return s;
}

// Join multiple arguments into single string
template<typename T0, typename... Ts>
std::string str(const T0& a, const Ts&... as) {
  return str(a) + str(as...);
}

// Helper
template<typename R>
std::string str_from_range(const R& range) {
  std::ostringstream s;

  if (range.begin() == range.end()) return "";

  auto last = range.end();
  --last;

  for (typename R::const_iterator i = range.begin(); i != range.end(); ++i) {
    s << str(*i);
    if (i != last) {
      s << ", ";
    }
  }

  return s.str();
}

// Convert std::array to string
template<typename T, size_t N>
std::string str(const std::array<T, N>& a) { return str_from_range(a); }

// Convert std::vector<uint8_t> to string
inline
std::string str(const std::vector<uint8_t>& a) {
  std::ostringstream s;
  s << "[";
  for (auto i = a.begin(); i != a.end(); ++i) {
    s << (unsigned int) *i;
    if (i != --a.end()) s << ", ";
  }
  s << "]";
  return s.str();
}

// Convert std::vector<uint8_t> to string
inline
std::string str(boost::asio::const_buffer b) {
  std::ostringstream s;

  auto ptr    = boost::asio::buffer_cast<const uint8_t*>(b);
  size_t size = boost::asio::buffer_size(b);

  s << "[";
  for (size_t i = 0; i < size; ++i) {
    s << (unsigned int) ptr[i];
    if (i != size - 1) s << ", ";
  }
  s << "]";

  return s.str();
}

// Convert std::vector to string
template<typename T>
std::string str(const std::vector<T>& a) { return "[" + str_from_range(a) + "]"; }

// Convert std::set to string
template<typename T>
std::string str(const std::set<T>& a) { return "{" + str_from_range(a) + "}"; }

// Convert std::map to string
template<typename T1, typename T2>
std::string str(const std::map<T1,T2>& a) { return "{" + str_from_range(a) + "}"; }

// Convert boost::optional to string
template<typename T>
std::string str(const boost::optional<T>& a) {
  if (a) {
    return str(*a);
  } else {
    return "none";
  }
}

#endif // CLUB_STRING_TOOLS_H
