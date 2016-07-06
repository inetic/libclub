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

#ifndef CLUB_NET_BUFFER_H
#define CLUB_NET_BUFFER_H

#include <boost/asio.hpp>
#include "net/Header.h"

namespace club {

/*
 * This Buffer structure is used to tell boost::asio
 * that we are actualy using two buffers, one for the header
 * (which is always present) and then one provided by the user
 * of this library.
 */
template<class T> class Buffer
{
public:
  typedef typename std::vector<T>::const_iterator const_iterator;

  struct Range {
    typedef typename std::vector<T>::const_iterator const_iterator;

    const_iterator _begin, _end;
    Range(const_iterator b, const_iterator e)
      : _begin(b)
      , _end  (e) {}
    const_iterator begin() const { return _begin; }
    const_iterator end  () const { return _end;   }
  };

  Buffer()
    : _header_buffer(new Header::bytes_type)
    , _buffers(new std::vector<T>(1))
  {
    (*_buffers)[0] = boost::asio::buffer(*_header_buffer);
  }

  template<class B> void buffer(const B& buffer_sequence)
  {
    _buffers->resize(std::distance( buffer_sequence.begin()
                                  , buffer_sequence.end())
                    + 1);

    unsigned int j = 1;

    for ( typename B::const_iterator i = buffer_sequence.begin()
        ; i != buffer_sequence.end(); ++i, ++j)
    {
      (*_buffers)[j] = *i;
    }
  }

  Range get_data_buffers() {
    ASSERT(_buffers->size() > 1);
    return Range(++(_buffers->begin()), _buffers->end());
  }

  void reset_buffer() { _buffers->resize(1); }


  const_iterator begin() const { return _buffers->begin(); }
  const_iterator end() const   { return _buffers->end(); }

  void get_header(Header& h) const { h.from_bytes(*_header_buffer); }
  void set_header(const Header& h) { h.to_bytes(*_header_buffer);   }

private:
  // This class shall be copyed a lot, that's why _buffers is a pointer.
  // _header is a pointer because it's address should not change (The
  // address is contained in the first element of the *_buffers array).
  std::shared_ptr<Header::bytes_type> _header_buffer;
  std::shared_ptr<std::vector<T> >    _buffers;
};

typedef Buffer<boost::asio::mutable_buffer> MutableBuffer;
typedef Buffer<boost::asio::const_buffer>   ConstBuffer;


} // club namespace

#endif // ifndef CLUB_NET_BUFFER_H
