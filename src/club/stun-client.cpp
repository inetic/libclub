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

#include <iostream>
#include <bitset>
#include <boost/asio/steady_timer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/range/adaptor/map.hpp>
#include "stun-client.h"
#include "debug/ASSERT.h"
#include "binary/serialize/ip.h"

using Timer = boost::asio::steady_timer;
using namespace club;
using std::make_shared;
using std::move;
using std::cout;
using std::endl;
using std::bitset;
using udp = boost::asio::ip::udp;
using boost::endian::native_to_big;
using boost::adaptors::map_values;
using LockGuard = std::lock_guard<std::mutex>;
using binary::decoder;
namespace error = boost::asio::error;

static const bitset<2> SUCCESS_CLASS("10");
static const uint16_t BIND_METHOD = 0x001;
static const uint32_t COOKIE      = 0x2112A442;
static const size_t   HEADER_SIZE = 20; // Bytes
static const uint16_t ATTRIB_UNKNOWN            = 0x0000;
static const uint16_t ATTRIB_MAPPED_ADDRESS     = 0x0001;
static const uint16_t ATTRIB_XOR_MAPPED_ADDRESS = 0x0020;

struct Attribute {
  uint16_t type;
  uint16_t length;
  const uint8_t* data;
};

// Result is in host byte order.
static uint16_t decode_method(uint16_t data) {
  // Bits #7 and 11 (from left, and #4 and 8 from right)
  // belong to the class, so need to be extracted.
  // Also last two bits are always zero.
  // Bits #0 and #1 (from left, and #14 and #15 from right)
  // are reserved for multiplexing.
  uint16_t result = (data & (1 << 0))
                  | (data & (1 << 1))
                  | (data & (1 << 2))
                  | (data & (1 << 3))
                  //| (data & (1 << 4))
                  | ((data & (1 << 5)) >> 1)
                  | ((data & (1 << 6)) >> 1)
                  | ((data & (1 << 7)) >> 1)
                  //| (data & (1 << 8))
                  | ((data & (1 << 9))  >> 2)
                  | ((data & (1 << 10)) >> 2)
                  | ((data & (1 << 11)) >> 1)
                  | ((data & (1 << 12)) >> 2)
                  | ((data & (1 << 13)) >> 2);

  return result;
}

static bitset<2> decode_class(uint16_t data) {
  bitset<2> result;
  result[0] = (data & (1 << 4)) != 0;
  result[1] = (data & (1 << 8)) != 0;
  return result;
}

static bitset<2> decode_plex(uint16_t data) {
  bitset<2> result;
  result[0] = (data & (1 << 14)) != 0;
  result[1] = (data & (1 << 15)) != 0;
  return result;
}

static Attribute decode_attribute(decoder& d) {
  Attribute a;
  a.type   = d.get<uint16_t>();
  a.length = d.get<uint16_t>();
  if (d.error()) {
    a.type   = ATTRIB_UNKNOWN;
    a.length = 0;
    return a;
  }
  a.data = d.current();
  return a;
}

static udp::endpoint decode_mapped_address(decoder& d) {
  namespace ip = boost::asio::ip;

  d.skip(1);
  auto family = d.get<uint8_t>();
  auto port   = d.get<uint16_t>();

  if (d.error()) return udp::endpoint();

  static const uint8_t IPV4 = 0x01;
  static const uint8_t IPV6 = 0x02;

  if (family == IPV4) {
    ip::address_v4 addr;
    binary::decode(d, addr);
    return udp::endpoint(addr, port);
  }
  else if (family == IPV6) {
    ip::address_v6 addr;
    binary::decode(d, addr);
    return udp::endpoint(addr, port);
  }
  else {
    d.set_error();
    return udp::endpoint();
  }
}

static udp::endpoint decode_xor_mapped_address(decoder& d) {
  d.skip(1);
  auto family = d.get<uint8_t>();
  auto port   = d.get<uint16_t>();

  if (d.error()) return udp::endpoint();

  static const uint8_t IPV4 = 0x01;
  static const uint8_t IPV6 = 0x02;

  if (family == IPV4) {
    auto addr = d.get<uint32_t>();
    port = port ^ (COOKIE >> 16);
    addr = addr ^ COOKIE;
    return udp::endpoint(boost::asio::ip::address_v4(addr), port);
  }
  else if (family == IPV6) {
    ASSERT(0 && "TODO");
    d.set_error();
    return udp::endpoint();
  }
  else {
    d.set_error();
    return udp::endpoint();
  }
}

//------------------------------------------------------------------------------
struct StunClient::Request {
  boost::asio::io_service& ios;
  RequestID id;
  Handler handler;
  std::vector<uint8_t> tx_buffer;
  Endpoint tx_endpoint;
  Timer timer;
  size_t resend_count;

  Request( boost::asio::io_service& ios
         , RequestID id
         , Endpoint tx_endpoint
         , Handler handler)
    : ios(ios)
    , id(id)
    , handler(handler)
    , tx_buffer(512)
    , tx_endpoint(tx_endpoint)
    , timer(ios)
    , resend_count(0)
  { }

  void exec(Error error, Endpoint reflected_endpoint) {
    timer.cancel();
    auto h = move(handler);
    ios.post([=]() { h(error, reflected_endpoint); });
  }
};

//------------------------------------------------------------------------------
struct StunClient::State {
  std::mutex mutex;
  boost::asio::io_service& ios;
  std::vector<uint8_t> rx_buffer;
  Endpoint rx_endpoint;
  Requests requests;
  bool was_destroyed;

  State(boost::asio::io_service& ios)
    : ios(ios)
    , rx_buffer(512)
    , was_destroyed(false) {}

  void destroy_requests(Error error) {
    for (auto request : requests | map_values) {
      if (!request->handler) continue;
      request->exec(error, Endpoint());
    }
    requests.clear();
  }

};

//------------------------------------------------------------------------------
StunClient::StunClient(udp::socket& socket)
  : _socket(socket)
  , _state(make_shared<State>(socket.get_io_service()))
  , _request_count(0)
{
  LockGuard guard(_state->mutex);

#ifdef NDEBUG
  std::random_device rd;
  _rand.seed(rd());
#else
  // Using random_device makes valgrind panic.
  // https://bugs.launchpad.net/ubuntu/+source/valgrind/+bug/1501545
  _rand.seed(std::time(0));
#endif
}

//------------------------------------------------------------------------------
StunClient::~StunClient() {
  LockGuard guard(_state->mutex);

  _state->was_destroyed = true;

  if (_request_count) {
    udp::socket s(_socket.get_io_service(), udp::endpoint(udp::v4(), 0));
    static uint32_t buf = 0xffff;
    s.send_to( boost::asio::buffer(&buf, sizeof(buf))
             , _socket.local_endpoint());
  }
}

//------------------------------------------------------------------------------
void StunClient::reflect(Endpoint server_endpoint, Handler handler) {
  LockGuard guard(_state->mutex);

  RequestID id;

  for (auto& i : id) i = rand();

  auto request = make_shared<Request>( _socket.get_io_service()
                                     , id
                                     , server_endpoint
                                     , move(handler));

  auto ri = _state->requests.find(server_endpoint);

  if (ri != _state->requests.end()) {
    return execute(ri, error::operation_aborted);
  }

  _state->requests[server_endpoint] = request;

  start_sending(request);

  if (_request_count++ == 0) {
    start_receiving(_state);
  }
}

//------------------------------------------------------------------------------
void StunClient::start_sending(RequestPtr request) {
  static const uint16_t size = 0;

  auto& buf = request->tx_buffer;
  buf.resize(HEADER_SIZE);

  binary::encoder e(buf.data(), buf.size());

  e.put((uint16_t) BIND_METHOD);
  e.put((uint16_t) size);
  e.put((uint32_t) COOKIE);
  e.put_raw(request->id.data(), request->id.size());

  ASSERT(!e.error() && e.written() == HEADER_SIZE);

  auto state = _state;
  _socket.async_send_to( boost::asio::buffer(buf)
                       , request->tx_endpoint
                       , [this, state, request](Error error, size_t) {
                         LockGuard guard(state->mutex);
                         on_send(error, move(request));
                       });
}

//------------------------------------------------------------------------------
void StunClient::on_send(Error, RequestPtr request) {
  if (!request->handler) {
    return;
  }

  uint64_t timeout = 250 * (2 << request->resend_count++);
  request->timer.expires_from_now(std::chrono::milliseconds(timeout));

  auto state = _state;
  request->timer.async_wait([this, state, request](Error) {
      LockGuard guard(state->mutex);
      if (!request->handler) return;
      start_sending(move(request));
      });
}

//------------------------------------------------------------------------------
void StunClient::start_receiving(StatePtr state) {
  _socket.async_receive_from( boost::asio::buffer(state->rx_buffer)
                            , state->rx_endpoint
                            , [this, state](Error error, size_t size) {
                              LockGuard guard(state->mutex);
                              on_recv(error, size, move(state));
                            });
}

//------------------------------------------------------------------------------
void StunClient::on_recv(Error error, size_t size, StatePtr state) {
  if (state->was_destroyed) {
    return state->destroy_requests(error::operation_aborted);
  }

  auto& rx_buf = state->rx_buffer;

  if (error) {
    return state->destroy_requests(error);
  }

  auto request_i = _state->requests.find(state->rx_endpoint);

  if (request_i == _state->requests.end()) {
    return start_receiving(move(state));
  }

  if (size < HEADER_SIZE) {
    if (--_request_count) start_receiving(move(state));
    return execute(request_i, error::fault);
  }

  decoder header_d(rx_buf.data(), size);

  auto method_and_class = header_d.get<uint16_t>();
  auto plex   = decode_plex(method_and_class);
  auto method = decode_method(method_and_class);
  auto class_ = decode_class(method_and_class);
  auto length = header_d.get<uint16_t>();
  auto cookie = header_d.get<uint32_t>();
  header_d.skip(12);

  if (header_d.error()) {
    if (--_request_count) start_receiving(move(state));
    // TODO: Better error mapping.
    return execute(request_i, error::operation_not_supported);
  }

  if (plex != bitset<2>("00")) {
    return start_receiving(move(state));
  }

  if (cookie != COOKIE) {
    if (--_request_count) start_receiving(move(state));
    // TODO: Better error mapping.
    return execute(request_i, error::fault);
  }

  if (method != BIND_METHOD) {
    if (--_request_count) start_receiving(move(state));
    return execute(request_i, error::operation_not_supported);
  }

  if (class_ != SUCCESS_CLASS) {
    if (--_request_count) start_receiving(move(state));
    // TODO: Better error mapping.
    return execute(request_i, error::fault);
  }

  decoder d(header_d.current(), length);

  while (!d.error()) {
    Attribute a = decode_attribute(d);

    if (d.error()) break;

    decoder dd(a.data, a.length);

    if (a.type == ATTRIB_MAPPED_ADDRESS) {
      Endpoint ep = decode_mapped_address(dd);
      if (dd.error()) break;
      execute(request_i, error, ep);
      break;
    }
    else if (a.type == ATTRIB_XOR_MAPPED_ADDRESS) {
      Endpoint ep = decode_xor_mapped_address(dd);
      if (dd.error()) break;
      execute(request_i, error, ep);
      break;
    }
  }

  if (state->was_destroyed) {
    return state->destroy_requests(error::operation_aborted);
  }

  if (--_request_count) {
    start_receiving(move(state));
  }

  return;
}

//------------------------------------------------------------------------------
void StunClient::execute(Requests::iterator ri, Error error, Endpoint endpoint) {
  ASSERT(!error || endpoint == Endpoint());
  auto r = ri->second;
  _state->requests.erase(ri);
  r->exec(error, endpoint);
}

//------------------------------------------------------------------------------
