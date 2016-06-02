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

#ifndef __NET_ANY_SIZE_H__
#define __NET_ANY_SIZE_H__

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

namespace net {

namespace __detail {
  static unsigned short HEADER_SIZE = 4;

  struct Part {
    Part(size_t buffer_size)
      : buffer_size(buffer_size)
      , data(buffer_size) {}

    void set_remaining(uint32_t remaining)
    {
      using namespace boost::asio;
      uint32_t remaining_le = boost::endian::native_to_big(remaining);
      buffer_copy( buffer(&data[0], HEADER_SIZE)
                 , buffer(&remaining_le, HEADER_SIZE));
    }

    uint32_t remaining() const {
      using namespace boost::asio;
      uint32_t remaining_le;
      buffer_copy( buffer(&remaining_le, HEADER_SIZE)
                 , buffer(&data[0], HEADER_SIZE));

      return boost::endian::big_to_native(remaining_le);
    }

    uint32_t max_data_size() const { return buffer_size - HEADER_SIZE; }

    uint32_t set_data(const char* begin, const char* end) {
      using namespace boost::asio;

      uint32_t size = std::min<uint32_t>(max_data_size(), end - begin);

      data.resize(HEADER_SIZE + size);

      buffer_copy(buffer(&data[HEADER_SIZE], size), buffer(begin, size));

      return size;
    }

    uint32_t buffer_size;
    std::vector<char> data;
  };

  template< class Socket
          , class Handler>
  void send_parts( Socket& socket
                 , const std::shared_ptr<std::vector<char>> & bytes
                 , const std::shared_ptr<Part>& part
                 , unsigned int timeout_ms
                 , uint32_t part_i
                 , uint32_t part_count
                 , const Handler& handler) {

    using boost::system::error_code;

    if (part_i >= part_count) {
      handler(error_code());
      return;
    }

    const char* begin = &(*bytes)[part_i * part->max_data_size()];
    const char* end   = &(*bytes)[bytes->size()];

    part->set_remaining(part_count - part_i - 1);
    part->set_data(begin, end);

    socket.async_send( boost::asio::buffer(part->data)
                     , timeout_ms
                     , [&socket
                       , bytes
                       , timeout_ms
                       , part_i
                       , part_count
                       , part
                       , handler]
                       (error_code error) {

                     if (error) {
                       handler(error);
                       return;
                     }

                     send_parts( socket
                               , bytes
                               , part
                               , timeout_ms
                               , part_i + 1
                               , part_count
                               , handler);
                     });
  }

  template< class Socket
          , class Handler>
  void receive_parts( Socket& socket
                    , std::vector<char>& rx_buffer
                    , const std::shared_ptr<Part>& part
                    , unsigned int timeout_ms
                    , const Handler& handler) {

    using namespace boost::asio;
    using boost::system::error_code;
    using namespace __detail;

    socket.async_receive( buffer(part->data)
                        , timeout_ms
                        , [&socket, &rx_buffer, part, timeout_ms, handler]
                          (error_code error, size_t size) {

                          if (error) {
                            handler(error);
                            return;
                          }

                          uint32_t remaining = part->remaining();

                          auto begin = part->data.begin() + HEADER_SIZE;
                          auto end   = part->data.begin() + size;

                          rx_buffer.insert(rx_buffer.end(), begin, end);

                          if (!remaining) {
                            handler(error_code());
                            return;
                          }

                          receive_parts( socket
                                       , rx_buffer
                                       , part
                                       , timeout_ms
                                       , handler);
                        });
  }
} // __detail namespace

template< class Socket
        , class TxBuffers
        , class Handler
        >
void send_any_size( Socket&          socket
                  , const TxBuffers& tx_buffers
                  , unsigned int     timeout_ms
                  , const Handler&   handler) {

  using namespace std;
  using namespace __detail;
  namespace asio = boost::asio;
  typedef std::vector<char> Bytes;

  size_t max_size  = socket.buffer_size() - HEADER_SIZE;
  size_t buf_size  = asio::buffer_size(tx_buffers);
  size_t num_parts = buf_size / max_size + 1;

  std::shared_ptr<Bytes> bytes = make_shared<Bytes>(buf_size);
  asio::buffer_copy(asio::buffer(*bytes), tx_buffers);

  auto part = std::make_shared<Part>(socket.buffer_size());

  __detail::send_parts( socket
                      , bytes
                      , part
                      , timeout_ms
                      , 0
                      , num_parts
                      , handler);
}

template< class Socket
        , class Handler // void (error_code)
        >
void recv_any_size( Socket&            socket
                  , std::vector<char>& rx_data
                  , unsigned int       timeout_ms
                  , const Handler&     handler) {

  using namespace std;
  namespace asio = boost::asio;
  using namespace __detail;

  rx_data.resize(0);
  auto part = std::make_shared<Part>(socket.buffer_size());

  receive_parts( socket
               , rx_data
               , part
               , timeout_ms
               , handler);
}


} // net namespace

#endif // ifndef __NET_ANY_SIZE_H__
