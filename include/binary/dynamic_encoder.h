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

#ifndef __BINARY_DYNAMIC_ENCODER_H__
#define __BINARY_DYNAMIC_ENCODER_H__

namespace binary {

template<typename ByteType>
class dynamic_encoder {
public:
  using iterator = ByteType*;

  std::size_t written() const;

  dynamic_encoder();
  dynamic_encoder(size_t reserve);

  template<class T> void put(T&& value);

  template<class Iterator> void put_raw(const Iterator*, std::size_t);

  std::vector<ByteType> move_data() {
    current = 0;
    return std::move(data);
  }

private:
// TODO: not public
public:

  std::vector<ByteType> data;
  size_t current;

  void grow_to_fit(size_t size);
};

} // binary namespace

namespace binary {

template<typename ByteType>
inline dynamic_encoder<ByteType>::dynamic_encoder()
  : current(0) {
}

template<typename ByteType>
inline dynamic_encoder<ByteType>::dynamic_encoder(size_t reserve)
  : current(0) {
  data.reserve(reserve);
}

template<typename ByteType>
inline
std::size_t dynamic_encoder<ByteType>::written() const {
  return data.size();
}

template<typename ByteType>
inline
void dynamic_encoder<ByteType>::grow_to_fit(size_t size) {
  if (current + size > data.size()) {
    data.resize(current + size);
  }
}

template<typename B>
inline
void encode(dynamic_encoder<B>& e, uint8_t value) {
  e.grow_to_fit(sizeof(value));

  e.data[e.current++] = value;
}

template<typename B>
inline
void encode(dynamic_encoder<B>& e, char value) {
  e.grow_to_fit(sizeof(value));

  e.data[e.current++] = value;
}

template<typename B>
inline
void encode(dynamic_encoder<B>& e, uint16_t value) {
  e.grow_to_fit(sizeof(value));

  e.data[e.current++] = (value >> 8) & 0xff;
  e.data[e.current++] = value & 0xff;
}

template<typename B>
inline
void encode(dynamic_encoder<B>& e, uint32_t value) {
  e.grow_to_fit(sizeof(value));

  e.data[e.current++] = (value >> 24) & 0xff;
  e.data[e.current++] = (value >> 16) & 0xff;
  e.data[e.current++] = (value >> 8)  & 0xff;
  e.data[e.current++] = value         & 0xff;
}

template<typename B>
inline
void encode(dynamic_encoder<B>& e, int32_t value) {
  e.grow_to_fit(sizeof(value));

  e.data[e.current++] = (value >> 24) & 0xff;
  e.data[e.current++] = (value >> 16) & 0xff;
  e.data[e.current++] = (value >> 8)  & 0xff;
  e.data[e.current++] = value         & 0xff;
}

template<class B>
template<class Iterator>
inline
void dynamic_encoder<B>::put_raw(const Iterator* iter, std::size_t size) {
  static_assert(sizeof(*iter) == 1, "");
  grow_to_fit(size);

  for (std::size_t i = 0; i != size; ++i) {
    data[current++] = *(iter++);
  }
}

} // binary namespace

template<typename B>
template<typename T>
inline
void binary::dynamic_encoder<B>::put(T&& value) {
  using binary::encode;

  encode(*this, std::forward<T>(value));
}

#endif // ifndef __BINARY_DYNAMIC_ENCODER_H__
