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

#ifndef RENDEZVOUS_REFLECTOR_H
#define RENDEZVOUS_REFLECTOR_H

#include <binary/serialize/ip.h>
#include "rendezvous/constants.h"
#include "header.h"

namespace rendezvous {

class Reflector {
  using udp = boost::asio::ip::udp;

  struct State {
    bool                  was_destroyed;
    udp::endpoint         remote_endpoint;
    std::vector<uint8_t>  rx_buffer;
    std::vector<uint8_t>  tx_buffer;
  };

public:
  Reflector(boost::asio::io_service&, VersionType);

  uint16_t get_port() const;

  ~Reflector();
private:
  void start_receiving();
  void start_sending();

private:
  udp::socket            _socket;
  std::shared_ptr<State> _state;
  const VersionType      _version;
};

//------------------------------------------------------------------------------
Reflector::Reflector(boost::asio::io_service& ios, VersionType version)
  : _socket(ios, udp::endpoint(udp::v4(), 0 /* random port */))
  , _state(std::make_shared<State>(State{ false
                                        , udp::endpoint()
                                        , std::vector<uint8_t>(512)
                                        , std::vector<uint8_t>(512) }))
  , _version(version)
{
  start_receiving();
}

uint16_t Reflector::get_port() const {
  return _socket.local_endpoint().port();
}

void Reflector::start_receiving() {
  auto state = _state;

  _socket.async_receive_from
      ( boost::asio::buffer(state->rx_buffer)
      , state->remote_endpoint
      , [this, state]
        (boost::system::error_code err, size_t size)
        {
          if (state->was_destroyed) return;

          if (err) {
            if (err == boost::asio::error::operation_aborted) {
              return;
            }
            return start_receiving();
          }
          
          start_sending();
        });
                           
}

void Reflector::start_sending() {
  auto state = _state;

  binary::encoder e(state->tx_buffer.data(), state->tx_buffer.size());

  uint16_t payload_size = 1                // method
                        + 1                // reserved
                        + sizeof(uint16_t) // port
                        ;

  state->tx_buffer.resize(HEADER_SIZE + payload_size);

  write_header(e, _version, payload_size);

  e.put(METHOD_REFLECTED);
  e.put((uint8_t) 0 /* reserved */);
  e.put(state->remote_endpoint.port());

  assert(!e.error());

  _socket.async_send_to
      ( boost::asio::buffer(state->tx_buffer)
      , state->remote_endpoint
      , [this, state] (boost::system::error_code err, size_t size) {
          if (state->was_destroyed) return;
          if (err == boost::asio::error::operation_aborted) {
            return;
          }
          return start_receiving();
        });
}

Reflector::~Reflector() {
  _state->was_destroyed = true;
}

} // rendezvous namespace

#endif // ifndef RENDEZVOUS_REFLECTOR_H
