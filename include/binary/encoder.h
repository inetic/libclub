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

#ifndef __BINARY_ENCODER_H__
#define __BINARY_ENCODER_H__

namespace binary {

class encoder {
public:
  using iterator = std::uint8_t*;

  template<typename RandomAccessSequence>
  encoder(RandomAccessSequence&);

  template<typename RandomAccessIterator>
  encoder(RandomAccessIterator begin, RandomAccessIterator end);

  template<typename RandomAccessIterator>
  encoder(RandomAccessIterator begin, std::size_t);

  std::size_t written() const;

  bool error() const { return _was_error; }

  template<class T> void put(T&& value);
  template<class Iterator> void put_raw(const Iterator*, std::size_t);

private:
// TODO
public:

  struct {
    iterator start;
    iterator begin;
    iterator end;
  } _current;

  bool _was_error;
};

} // binary namespace

namespace binary {

template<typename RandomAccessSequence>
encoder::encoder(RandomAccessSequence& sequence)
: _was_error(false) {
  _current.start = reinterpret_cast<iterator>(sequence.begin());
  _current.begin = _current.start;
  _current.end   = reinterpret_cast<iterator>(sequence.end());
}

template<typename RandomAccessIterator>
encoder::encoder(RandomAccessIterator begin, RandomAccessIterator end)
: _was_error(false) {
  _current.start = begin;
  _current.begin = begin;
  _current.end   = end;
}

template<typename RandomAccessIterator>
encoder::encoder(RandomAccessIterator begin, std::size_t size)
: _was_error(false) {
  _current.start = begin;
  _current.begin = begin;
  _current.end   = begin + size;
}

inline
std::size_t encoder::written() const {
  return _current.begin - _current.start;
}

inline void encode(encoder& e, uint8_t value) {
  if (e._current.begin >= e._current.end) e._was_error = true;
  if (e._was_error) return;

  *(e._current.begin++) = value;
}

inline void encode(encoder& e, int8_t value) {
  if (e._current.begin >= e._current.end) e._was_error = true;
  if (e._was_error) return;

  *(e._current.begin++) = value;
}

inline void encode(encoder& e, uint16_t value) {
  if (e._current.begin + sizeof(value) > e._current.end) e._was_error = true;
  if (e._was_error) return;

  *(e._current.begin++) = (value >> 8) & 0xff;
  *(e._current.begin++) = value & 0xff;
}

inline void encode(encoder& e, uint32_t value) {
  if (e._current.begin + sizeof(value) > e._current.end) e._was_error = true;
  if (e._was_error) return;

  *(e._current.begin++) = (value >> 24) & 0xff;
  *(e._current.begin++) = (value >> 16) & 0xff;
  *(e._current.begin++) = (value >> 8)  & 0xff;
  *(e._current.begin++) = value         & 0xff;
}

template<typename T>
inline
void encoder::put(T&& value) {
  if (_was_error) return;
  //encodable< encoder
  //         , typename std::decay<T>::type
  //         >::encode(*this, std::forward<T>(value));
  encode(*this, std::forward<T>(value));
}

template<class Iterator>
inline
void encoder::put_raw(const Iterator* iter, std::size_t size) {
  static_assert(sizeof(*iter) == 1, "");

  if (_current.begin + size > _current.end) _was_error = true;
  if (_was_error) return;

  for (std::size_t i = 0; i != size; ++i) {
    *(_current.begin++) = *(iter++);
  }
}

} // binary namespace

#endif // ifndef __BINARY_ENCODER_H__
