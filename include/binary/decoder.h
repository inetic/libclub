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

#ifndef BINARY_DECODER_H
#define BINARY_DECODER_H

#include <boost/detail/endian.hpp>

#ifndef BOOST_LITTLE_ENDIAN
# error Big endian architectures not supported yet
#endif

namespace binary {

class decoder {
public:
  using const_iterator = const std::uint8_t*;

  decoder(const decoder&);
  decoder(decoder&);

  template<typename RandomAccessSequence>
  decoder(const RandomAccessSequence&);

  template<typename RandomAccessIterator>
  decoder(RandomAccessIterator begin, RandomAccessIterator end);

  template<typename RandomAccessIterator>
  decoder(RandomAccessIterator begin, std::size_t);

  decoder();

  template<typename RandomAccessIterator>
  void reset(RandomAccessIterator begin, std::size_t);

  void skip(std::size_t size);
  bool empty() const;
  std::size_t size() const;

  template <typename T, typename... Args> T get(Args&&...);
  void get_raw(std::uint8_t*, std::size_t);

  const_iterator current() const;

  bool error() const { return _was_error; }
  void set_error() { _was_error = true; }

  void shrink(size_t);

private:
  struct {
    const_iterator begin;
    const_iterator end;
  } _current;

  bool _was_error;
};

} // binary namespace

namespace binary {

template <typename RandomAccessIterator>
decoder::decoder(RandomAccessIterator begin,
                 RandomAccessIterator end)
  : _was_error(false) {
  _current.begin = reinterpret_cast<const_iterator>(begin);
  _current.end = reinterpret_cast<const_iterator>(end);
}

template <typename RandomAccessIterator>
decoder::decoder(RandomAccessIterator begin, std::size_t size)
  : _was_error(false) {
  _current.begin = reinterpret_cast<const_iterator>(begin);
  _current.end = _current.begin + size;
}

inline
decoder::decoder()
  : _was_error(true) {
  _current.begin = nullptr;
  _current.end = nullptr;
}

inline
decoder::decoder(const decoder& d)
  : _was_error(d._was_error) {
  _current.begin = d._current.begin;
  _current.end = d._current.end;
}

inline
decoder::decoder(decoder& d)
  : _was_error(d._was_error) {
  _current.begin = d._current.begin;
  _current.end = d._current.end;
}

template <typename RandomAccessSequence>
decoder::decoder(const RandomAccessSequence& sequence)
  : _was_error(false) {
  _current.begin = sequence.data();
  auto value_size = sizeof(typename RandomAccessSequence::value_type);
  _current.end = sequence.data() + sequence.size() * value_size;
}

template <typename RandomAccessIterator>
void decoder::reset(RandomAccessIterator begin, std::size_t size) {
  _was_error = false;
  _current.begin = reinterpret_cast<const_iterator>(begin);
  _current.end = _current.begin + size;
}

inline void decoder::shrink(size_t new_size) {
  if (_was_error) return;

  size_t remaining = _current.end - _current.begin;

  if (new_size < remaining) {
    _current.end = _current.begin + new_size;
  }
}

inline void decoder::skip(std::size_t size) {
  if (size > this->size()) _was_error = true;
  if (_was_error) return;
  _current.begin += size;
}

inline decoder::const_iterator decoder::current() const {
  return _current.begin;
}

inline bool decoder::empty() const {
  return _current.begin == _current.end;
}

inline std::size_t decoder::size() const {
  return _current.end - _current.begin;
}

template <>
inline std::uint8_t decoder::get<std::uint8_t>() {
  if (empty()) _was_error = true;
  if (_was_error) return 0;
  return *(_current.begin++);
}

template <>
inline std::int8_t decoder::get<std::int8_t>() {
  if (empty()) _was_error = true;
  if (_was_error) return 0;
  return *(_current.begin++);
}

template <>
inline char decoder::get<char>() {
  if (empty()) _was_error = true;
  if (_was_error) return 0;
  return *(_current.begin++);
}

template <>
inline std::uint16_t decoder::get<std::uint16_t>() {
  std::uint16_t result;
  if (size() < sizeof(result)) _was_error = true;
  if (_was_error) return 0;

  result = _current.begin[0] << 8
         | _current.begin[1];

  _current.begin += sizeof(result);
  return result;
}

template <>
inline std::int16_t decoder::get<std::int16_t>() {
  std::int16_t result;
  if (size() < sizeof(result)) _was_error = true;
  if (_was_error) return 0;

  result = _current.begin[0] << 8
         | _current.begin[1];

  _current.begin += sizeof(result);
  return result;
}

template <>
inline std::uint32_t decoder::get<std::uint32_t>() {
  std::uint32_t result;
  if (size() < sizeof(result)) _was_error = true;
  if (_was_error) return 0;

  result = _current.begin[0] << 24
         | _current.begin[1] << 16
         | _current.begin[2] << 8
         | _current.begin[3];

  _current.begin += sizeof(result);
  return result;
}

template <>
inline std::int32_t decoder::get<std::int32_t>() {
  std::int32_t result;
  if (size() < sizeof(result)) _was_error = true;
  if (_was_error) return 0;

  result = _current.begin[0] << 24
         | _current.begin[1] << 16
         | _current.begin[2] << 8
         | _current.begin[3];

  _current.begin += sizeof(result);
  return result;
}

inline void decoder::get_raw(std::uint8_t* iter, std::size_t size) {
  if (this->size() < size) _was_error = true;
  if (_was_error) return;

  for (std::size_t i = 0; i < size; ++i) {
    *(iter++) = *(_current.begin++);
  }
}

template<class T, typename... Args> void decode(decoder&, Args&&...);

} // binary namespace

template <typename T, typename... Args>
inline T binary::decoder::get(Args&&... args) {
  using binary::decode;

  if (_was_error) return T();

  T retval;
  decode(*this, retval, args...);

  return retval;
}

#endif // ifndef BINARY_DECODER_H
