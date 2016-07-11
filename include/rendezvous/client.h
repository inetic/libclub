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

#ifndef RENDEZVOUS_CLIENT_H
#define RENDEZVOUS_CLIENT_H

#include <vector>
#include <mutex>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/optional.hpp>
#include <binary/encoder.h>
#include <binary/decoder.h>
#include <binary/serialize/ip.h>
#include <rendezvous/constants.h>

namespace rendezvous {

class client {
private:
  using Bytes = std::vector<uint8_t>;
  using udp = boost::asio::ip::udp;
  using Error = boost::system::error_code;
  using Handler = std::function<void(Error, udp::socket, udp::endpoint)>;
  using Constant = constants_v1;

  struct State {
    std::mutex mutex;
    udp::socket socket;
    Handler handler;
    bool was_destroyed;
    udp::endpoint rx_endpoint;
    Bytes rx_buffer;
    Bytes tx_buffer;
    boost::asio::steady_timer timer;

    State(udp::socket socket, Handler handler)
      : socket(std::move(socket))
      , handler(std::move(handler))
      , was_destroyed(false)
      , rx_buffer(256)
      , tx_buffer(256)
      , timer(socket.get_io_service())
    {}

    void exec(Error error, udp::endpoint ep, bool is_sym) {
      using namespace std;
      auto h = move(handler);
      // TODO: use socket directly once switch to c++14 is made.
      auto socket_ptr = make_shared<udp::socket>(move(socket));

      if (!is_sym) {
        socket.get_io_service().post([h, error, socket_ptr, ep]() {
            h(error, move(*socket_ptr), ep);
          });
      }
      else {
        // If the other end is behind a symmetric NAT we need to
        // wait a little to alow him to punch a hole in his NAT.
        // Otherwise if his NAT found out our incoming packet
        // first, the port prediction (that the rendezvous
        // server did) wouldn't work.
        timer.expires_from_now(std::chrono::milliseconds(500));
        timer.async_wait([h, error, socket_ptr, ep](Error e2) {
            h(error ? error : e2, move(*socket_ptr), ep);
          });
      }
    }
  };

  using StatePtr = std::shared_ptr<State>;

public:
  using Service = uint32_t;

  static uint16_t version() { return 1; }

  client( Service service_number
        , udp::socket
        , udp::endpoint server_ep
        , bool is_host
        , Handler handler);

  boost::asio::io_service& get_io_service() const {
    return _state->socket.get_io_service();
  }

  ~client();

private:
  void start_receiving(StatePtr);
  void start_sending(StatePtr);

  void on_recv(StatePtr, Error, size_t);
  void on_send(StatePtr);

  void write_header(binary::encoder&, uint16_t payload_size) const;;
  std::vector<uint8_t> construct_fetch_message(uint16_t reflected_port) const;
  std::vector<uint8_t> construct_close_message() const;
  std::vector<uint8_t> construct_reflect_message() const;
  std::vector<uint8_t> construct_get_reflector_message() const;

  void handle_reflector_message(binary::decoder&);
  void handle_reflected_port(binary::decoder&);

  void send(StatePtr, Bytes payload, const udp::endpoint& target);

  bool is_valid_sender(const udp::endpoint&) const;

private:
  Service _service_number;
  boost::asio::steady_timer _resend_timer;
  udp::endpoint _server_ep;
  StatePtr _state;
  boost::optional<uint16_t>      _reflected_port;
  boost::optional<udp::endpoint> _reflector_endpoint;
  bool _is_host;
};

} // rendezvous client

#include <rendezvous/constants.h>

namespace rendezvous {

inline
client::client( Service service_number
              , udp::socket socket
              , udp::endpoint server_ep
              , bool is_host
              , Handler handler)
  : _service_number(service_number)
  , _resend_timer(socket.get_io_service())
  , _server_ep(server_ep)
  , _state(std::make_shared<State>(std::move(socket), std::move(handler)))
  , _is_host(is_host)
{
  start_sending(_state);
  start_receiving(_state);
}

inline
void client::start_receiving(StatePtr state) {
  state->socket.async_receive_from( boost::asio::buffer(state->rx_buffer)
                                  , state->rx_endpoint
                                  , [this, state](Error error, size_t size) {
                                    on_recv(std::move(state), error, size);
                                  });
}

inline bool client::is_valid_sender(const udp::endpoint& sender) const {
  return sender == _server_ep
      || (_reflector_endpoint && *_reflector_endpoint == sender);
}

inline
void client::on_recv(StatePtr state, Error error, size_t size) {
  using std::move;
  namespace ip = boost::asio::ip;

  std::lock_guard<std::mutex> guard(state->mutex);

  if (state->was_destroyed) {
    return state->exec(error, udp::endpoint(), false);
  }

  if (error) {
    _resend_timer.cancel();
    return state->exec(error, udp::endpoint(), false);
  }

  if (!is_valid_sender(state->rx_endpoint)) {
    return start_receiving(move(state));
  }

  binary::decoder d( state->rx_buffer.data()
                   , std::min( state->rx_buffer.size()
                             , size));

  auto plex_and_version = d.get<uint16_t>();
  auto length           = d.get<uint16_t>();
  auto cookie           = d.get<uint32_t>();

  if (d.error()) return start_receiving(move(state));

  d.shrink(length);

  std::bitset<2> plex;
  plex[0] = (plex_and_version & (1 << 14)) != 0;
  plex[1] = (plex_and_version & (1 << 15)) != 0;

  // TODO: Check version.
  if (plex != PLEX)           return start_receiving(move(state));
  if (cookie != COOKIE)       return start_receiving(move(state));

  auto version = plex_and_version & 0x3fff;

  if (version == 0) {
    // Unsupported version by the server.
    return state->exec( boost::asio::error::no_protocol_option
                      , udp::endpoint()
                      , false);
  }

  auto method = d.get<uint8_t>();

  switch (method) {
    case METHOD_MATCH:     break; // Handled in the rest of this function.
    case METHOD_REFLECTOR: handle_reflector_message(d);
                           return start_receiving(move(state));
    case METHOD_REFLECTED: handle_reflected_port(d);
                           return start_receiving(move(state));
    default:               return start_receiving(move(state));
  }

  udp::endpoint reflexive_ep, ext_ep, int_ep;

  for (auto ep : {&reflexive_ep, &ext_ep, &int_ep}) {
    auto ipv  = d.get<uint8_t>();
    auto port = d.get<uint16_t>();

    if (ipv == IPV4_TAG) {
      *ep = udp::endpoint(d.get<ip::address_v4>(), port);
    }
    else if (ipv == IPV6_TAG) {
      *ep = udp::endpoint(d.get<ip::address_v6>(), port);
    }
    else {
      d.set_error();
      break;
    }
  }

  bool is_sym = d.get<uint8_t>();

  if (d.error()) return start_receiving(move(state));

  _resend_timer.cancel();

  if (reflexive_ep.address() == ext_ep.address()) {
    state->exec(error, int_ep, is_sym);
  }
  else {
    state->exec(error, ext_ep, is_sym);
  }
}

inline void client::handle_reflector_message(binary::decoder& d) {
  auto reserved       = d.get<uint8_t>();
  auto reflector_port = d.get<uint16_t>();


  if (d.error() || reserved != 0) {
    assert(0);
    return;
  }

  _reflector_endpoint = udp::endpoint(_server_ep.address(), reflector_port);
  _resend_timer.cancel();
}

inline void client::handle_reflected_port(binary::decoder& d) {
  auto reserved       = d.get<uint8_t>();
  auto reflected_port = d.get<uint16_t>();

  assert(!d.error());
  assert(reserved == 0);

  if (d.error() || reserved != 0) {
    return;
  }

  _reflected_port = reflected_port;
  _resend_timer.cancel();
}

inline
void client::start_sending(StatePtr state) {
  using std::move;

  if (_reflected_port) {
    send( move(state)
        , construct_fetch_message(*_reflected_port)
        , _server_ep);
  }
  else if (_reflector_endpoint) {
    send( move(state)
        , construct_reflect_message()
        , *_reflector_endpoint);
  }
  else {
    send( move(state)
        , construct_get_reflector_message()
        , _server_ep);
  }
}

inline void client::send( StatePtr             state
                        , Bytes                payload
                        , const udp::endpoint& target) {
  state->tx_buffer = std::move(payload);

  _state->socket.async_send_to( boost::asio::buffer(state->tx_buffer)
                              , target
                              , [this, state](Error, size_t) {
                                on_send(std::move(state));
                              });
}

inline
void client::on_send(StatePtr state) {
  std::lock_guard<std::mutex> guard(state->mutex);

  if (state->was_destroyed || !state->handler) {
    return;
  }

  _resend_timer.expires_from_now(Constant::keepalive_duration() / 3);
  _resend_timer.async_wait([this, state](Error) {
      if (state->was_destroyed || !state->handler) return;
      start_sending(std::move(state));
    });
}

inline
client::~client() {
  std::lock_guard<std::mutex> guard(_state->mutex);

  _state->was_destroyed = true;

  if (_state->socket.is_open()) {
    // Attempt to close gracefuly.
    _state->socket.send_to( boost::asio::buffer(construct_close_message())
                          , _server_ep);

    _state->socket.close();
  }
}

inline
void
client::write_header(binary::encoder& encoder, uint16_t payload_size) const {
  // Header
  uint16_t plex = 1 << 15;

  encoder.put((uint16_t) (plex | version()));
  encoder.put((uint16_t) payload_size);
  encoder.put((uint32_t) COOKIE);

  assert(encoder.written() == HEADER_SIZE);
}

inline
std::vector<uint8_t> client::construct_get_reflector_message() const {
  uint16_t payload_size = 1;

  Bytes bytes(HEADER_SIZE + payload_size);

  binary::encoder e(bytes.data(), bytes.size());
  write_header(e, payload_size);

  // Payload
  e.put((uint8_t) CLIENT_METHOD_GET_REFLECTOR);

  return bytes;
}

inline
std::vector<uint8_t> client::construct_reflect_message() const {
  uint16_t payload_size = 1;

  Bytes bytes(HEADER_SIZE + payload_size);

  binary::encoder e(bytes.data(), bytes.size());
  write_header(e, payload_size);

  // Payload
  e.put((uint8_t) CLIENT_METHOD_REFLECT);

  return bytes;
}

inline
std::vector<uint8_t>
client::construct_fetch_message(uint16_t reflected_port) const {
  namespace ip = boost::asio::ip;
  ip::address internal_addr;

  {
    udp::socket tmp(get_io_service());
    tmp.connect(_server_ep);
    internal_addr = tmp.local_endpoint().address();
  }

  auto internal_port = _state->socket.local_endpoint().port();

  uint16_t payload_size = 1 /* method */
                        + sizeof(Service)
                        + 2 /* internal port */
                        + 2 /* reflected port */
                        + 1 /* ipv */
                        + (internal_addr.is_v4() ? 4 : 16);

  Bytes bytes(HEADER_SIZE + payload_size);
  binary::encoder e(bytes.data(), bytes.size());

  // Header
  write_header(e, payload_size);

  // Payload
  if (_is_host) {
    e.put((uint8_t) CLIENT_METHOD_FETCH_AS_HOST);
  }
  else {
    e.put((uint8_t) CLIENT_METHOD_FETCH);
  }

  e.put((Service) _service_number); // Service
  e.put((uint16_t) reflected_port);
  e.put((uint16_t) internal_port);

  if (internal_addr.is_v4()) {
    e.put((uint8_t) IPV4_TAG);
    e.put(internal_addr.to_v4());
  }
  else {
    e.put((uint8_t) IPV6_TAG);
    e.put(internal_addr.to_v6());
  }

  assert(!e.error());
  assert(e.written() == bytes.size());

  return bytes;
}

inline
std::vector<uint8_t> client::construct_close_message() const {
  namespace ip = boost::asio::ip;

  uint16_t payload_size = 1 /* method */;

  Bytes bytes(HEADER_SIZE + payload_size);
  binary::encoder e(bytes.data(), bytes.size());

  // Header
  uint16_t plex = 1 << 15;

  e.put((uint16_t) (plex | version()));
  e.put((uint16_t) payload_size);
  e.put((uint32_t) COOKIE);

  assert(e.written() == HEADER_SIZE);

  // Payload
  e.put((uint8_t) CLIENT_METHOD_CLOSE);

  assert(!e.error());
  assert(e.written() == bytes.size());

  return bytes;
}

} // rendezvous client

#endif // ifndef RENDEZVOUS_CLIENT_H
