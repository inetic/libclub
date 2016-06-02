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

#ifndef __RENDEZVOUS_SERVER_H__
#define __RENDEZVOUS_SERVER_H__

#include <queue>
#include <boost/asio/ip/udp.hpp>
#include <binary/decoder.h>
#include <rendezvous/constants.h>
#include "options.h"

namespace rendezvous {

class server_handler_1;

// Header:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |1 0|         Version           |        Message Length         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |                          Magic Cookie                         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

class server {
  using Error = boost::system::error_code;
  using udp = boost::asio::ip::udp;
  using Bytes = std::vector<uint8_t>;

  struct SendEntry {
    udp::endpoint target;
    Bytes payload;
  };

  struct State {
    bool was_destroyed;
    Bytes rx_buffer;
    std::queue<SendEntry> tx_queue;
    udp::endpoint sender_endpoint;

    State()
      : was_destroyed(false)
      , rx_buffer(HEADER_SIZE + MAX_PAYLOAD_SIZE)
    { }
  };

  using StatePtr = std::shared_ptr<State>;

public:
  server(boost::asio::io_service& ios, options);

  void send_to(udp::endpoint target, Bytes payload) {
    bool was_empty = _state->tx_queue.empty();
    _state->tx_queue.emplace(SendEntry({target, payload}));

    if (was_empty) {
      start_sending(_state);
    }
  }

  udp::endpoint local_endpoint() const;

  ~server() {
    _state->was_destroyed = true;
  }

private:
  void start_sending(StatePtr state);
  void start_receiving(StatePtr state);

  void on_receive(StatePtr state, Error error, size_t size);

  void handle_payload( VersionType version
                     , udp::endpoint sender
                     , binary::decoder d);

private:
  boost::asio::io_service& _ios;
  udp::socket _socket;
  StatePtr _state;

  std::unique_ptr<server_handler_1> _handler_1;
};

} // rendezvous namespace

#include "server_handler_1.h"

namespace rendezvous {

inline
server::server(boost::asio::io_service& ios, options opt)
  : _ios(ios)
  , _socket(ios, udp::endpoint(udp::v4(), opt.port()))
  , _state(std::make_shared<State>())
  , _handler_1(new server_handler_1(ios, opt))
{
  start_receiving(_state);
}

inline
boost::asio::ip::udp::endpoint server::local_endpoint() const {
  return _socket.local_endpoint();
}

inline
void server::start_sending(StatePtr state) {
  auto& entry = state->tx_queue.front();
  _socket.async_send_to( boost::asio::buffer(entry.payload)
                       , entry.target
                       , [this, state](Error, size_t) {
                         if (state->was_destroyed) return;
                         state->tx_queue.pop();
                         if (state->tx_queue.empty()) return;
                         start_sending(move(state));
                       });
}

inline
void server::start_receiving(StatePtr state) {
  _socket.async_receive_from( boost::asio::buffer(state->rx_buffer)
                            , state->sender_endpoint
                            , [this, state](Error error, size_t size) {
                              if (state->was_destroyed) return;
                              on_receive(move(state), error, size);
                            });
}

inline
void server::on_receive(StatePtr state, Error error, size_t size) {
  binary::decoder d( state->rx_buffer.data()
                   , std::min( size
                             , state->rx_buffer.size()));

  auto plex_and_version = d.get<uint16_t>();
  auto length           = d.get<uint16_t>();
  auto cookie           = d.get<uint32_t>();

  if (d.error()) {
    return start_receiving(move(state));
  }

  if (length + HEADER_SIZE > size) {
    return start_receiving(move(state));
  }

  std::bitset<2> plex;
  plex[0] = (plex_and_version >> 14) & 1;
  plex[1] = (plex_and_version >> 15) & 1;

  if (plex != PLEX || cookie != COOKIE) {
    return start_receiving(move(state));
  }

  uint16_t version = plex_and_version & 0b0011111111111111;

  handle_payload( version
                , state->sender_endpoint
                , binary::decoder(d.current(), length));

  start_receiving(move(state));
}

inline
void server::handle_payload( VersionType version
                           , udp::endpoint sender
                           , binary::decoder d) {
  switch (version) {
    case server_handler_1::version: _handler_1->handle(sender, d, *this);
                                    break;
  }
}

} // rendezvous namespace

#endif // ifndef __RENDEZVOUS_SERVER_H__
