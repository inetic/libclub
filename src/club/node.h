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

#ifndef CLUB_NODE_H
#define CLUB_NODE_H

#include <map>
#include <club/uuid.h>
#include <club/socket.h>
#include <club/hub.h>
#include "message.h"
#include <club/debug/string_tools.h>

#if 0
#  include "debug/log.h"
#  define NODE_LOG(...) log("NODE: ", _debug_hub_id, " ", __VA_ARGS__)
#else
#  include "debug/log.h"
#  define NODE_LOG(...) do {} while(0)
#  define NODE_LOG_(...) log("NODE: ", _debug_hub_id, " ", __VA_ARGS__)
#endif

namespace club {

struct Node {
  using Error         = boost::system::error_code;
  using Address       = boost::asio::ip::address;
  using Bytes         = std::vector<uint8_t>;
  using SocketPtr     = std::shared_ptr<Socket>;

  struct SharedState {
    bool                              was_destroyed;
    // Our sockets don't like being destroyed while waiting
    // on callbacks.
    std::shared_ptr<Socket>           socket;

    SharedState()
      : was_destroyed(false)
    { }
  };

  enum class ConnectState { not_connected
                          , connecting
                          , connected
                          , disconnected };

  struct Peer {
    Address address;
  };

  Node(club::hub* hub, uuid id)
    : id(id)
    , user_notified(hub->id() == id)
    , connect_state(ConnectState::not_connected)
    , _remote_port({0, 0})
    , _hub(hub)
    , _debug_hub_id(_hub->id())
    , _contact_sent(false)
    , _shared_state(std::make_shared<SharedState>())
  {
  }

  Node(club::hub* hub, uuid id, SocketPtr&& socket)
    : id(id)
    , user_notified(hub->id() == id)
    , connect_state(ConnectState::connected)
    , _remote_port({0, 0})
    , _hub(hub)
    , _debug_hub_id(_hub->id())
    , _contact_sent(false)
    , _shared_state(std::make_shared<SharedState>())
  {
    _shared_state->socket = move(socket);
    start_recv_loops();
  }

  void assign_socket(SocketPtr socket) {
    ASSERT(_shared_state);
    ASSERT(!_shared_state->socket);
    set_state(ConnectState::connected);
    _shared_state->socket = move(socket);
    start_recv_loops();
  }

  void set_remote_port( uint16_t internal_port
                      , uint16_t external_port) {
    if (is(ConnectState::not_connected) == false) return;
    _remote_port.internal = internal_port;
    _remote_port.external = external_port;
    connect();
  }

  void set_remote_address(std::shared_ptr<Socket> socket, Address addr) {
    if (is(ConnectState::not_connected) == false) return;
    _shared_state->socket = move(socket);
    _remote_address = addr;
    connect();
  }

  void send(std::shared_ptr<Bytes> data) {
    if (is(ConnectState::disconnected)) return;
    // TODO: Socket should take asio::const_buffer as an argument
    // and a callback to preserve it's lifetime
    auto state = _shared_state;
    _shared_state->socket->send_reliable(*data, [=](auto error) {
        if (state->was_destroyed) return;
        if (error) {
          return this->on_socket_error("unreliable recv", error);
        }
      });
  }

  template<class Handler>
  void send_unreliable(boost::asio::const_buffer b, Handler handler) {
    if (!is(ConnectState::connected)) {
      return _shared_state->socket->get_io_service().post([handler]() {
          handler(boost::asio::error::broken_pipe);
        });
    }

    auto state = _shared_state;

    // TODO: Don't copy data
    auto begin = boost::asio::buffer_cast<const uint8_t*>(b);
    std::vector<uint8_t> data( begin
                             , begin + boost::asio::buffer_size(b));

    _shared_state->socket->send_unreliable(std::move(data), [=](auto error) {
        if (state->was_destroyed) return;
        handler(error);
      });
  }

  Address address() const {
    if (!is(ConnectState::connected)) {
      return Address();
    }

    ASSERT(_shared_state->socket);
    auto oep = _shared_state->socket->remote_endpoint();
    if (oep && oep->address().is_v4()) {
      return oep->address().to_v4();
    }

    return Address();
  }

  bool is_connected() const { return is(ConnectState::connected); }
  bool is_connecting() const { return is(ConnectState::connecting); }

  ~Node() {
    NODE_LOG("~Node(", id,")");
    _shared_state->was_destroyed = true;
    if (_shared_state->socket) _shared_state->socket->close();
  }

  bool contact_sent() const { return _contact_sent; }
  void contact_sent(bool v) { _contact_sent = v; }

  void disconnect() {
    NODE_LOG("disconnect()");
    if (is(ConnectState::disconnected)) return;
    set_state(ConnectState::disconnected);
    if (auto& s = _shared_state->socket) s.reset();
  }

private:
  void set_state(ConnectState s) {
    connect_state = s;
  }

  bool is(ConnectState s) const {
    return connect_state == s;
  }

  bool has_endpoint() const {
    return _remote_port.internal && !_remote_address.is_unspecified();
  }

  void connect() {
    assert(0 && "TODO");
    //using namespace boost::asio;
    //typedef boost::asio::ip::udp udp;

    //if (!is(ConnectState::not_connected) || !_shared_state->socket || !has_endpoint()) {
    //  return;
    //}

    //NODE_LOG( "Connect ", id
    //        , " socket:", ((bool)_shared_state->socket)
    //        , " address:", _remote_address
    //        , " remote_port:", _remote_port.internal
    //        , "/", _remote_port.external);

    //set_state(ConnectState::connecting);

    //udp::endpoint internal_ep(_remote_address, _remote_port.internal);
    //udp::endpoint external_ep(_remote_address, _remote_port.external);

    //auto state = _shared_state;
    //state->socket->async_p2p_connect(30000, internal_ep, external_ep,
    //    [this, state]( Error error
    //                 , const udp::endpoint&
    //                 , const udp::endpoint&) {
    //      if (state->was_destroyed) return;
    //      if (is(ConnectState::disconnected)) return;

    //      NODE_LOG("OnConnect to ", id, " (error=", error.message(), ")");

    //      if (error) {
    //        ASSERT(is(ConnectState::connecting));
    //        set_state(ConnectState::not_connected);
    //        // TODO: Set up timer after which we try to reconnect.
    //        return;
    //      }

    //      if (is(ConnectState::connecting)) {
    //        set_state(ConnectState::connected);
    //      }

    //      start_recv_loops();
    //      send_front();

    //      _hub->on_peer_connected(*this);
    //    });
  }

  void start_recv_loops() {
    start_reliable_recv_loop();
    start_unreliable_recv_loop();
  }

  void start_reliable_recv_loop() {
    auto state = _shared_state;
    _shared_state->socket->receive_reliable(
      [this, state](auto err, auto buffer) {
        if (state->was_destroyed) return;
        if (this->is(ConnectState::disconnected)) return;

        if (err) {
          auto debug_msg = std::string("reliable_recv") + err.message();
          return this->on_socket_error(std::move(debug_msg), err);
        }

        _hub->on_recv_raw(*this, buffer);
        if (state->was_destroyed) return;
        this->start_reliable_recv_loop();
      });
  }

  void start_unreliable_recv_loop() {
    auto state = _shared_state;

    state->socket->receive_unreliable
        ([this, state](const Error& error, boost::asio::const_buffer buffer) {
            if (state->was_destroyed) return;
            if (is(ConnectState::disconnected)) return;

            if (error) {
              return on_socket_error("unreliable recv", error);
            }

            _hub->node_received_unreliable_broadcast(buffer);

            if (!state->was_destroyed) {
              start_unreliable_recv_loop();
            }
          });
  }

  void on_socket_error(std::string debug_str, Error e) {
    NODE_LOG("on socket error ", e.message());
    if (is(ConnectState::disconnected)) return;
    set_state(ConnectState::disconnected);
    _shared_state->socket.reset();
    _hub->on_peer_disconnected(*this, std::move(debug_str));
  }

public:
  const uuid id;

  std::map<uuid, Peer> peers;

  bool user_notified;

private:
  ConnectState connect_state;

  struct {
    uint16_t internal;
    uint16_t external;
  } _remote_port;

  Address _remote_address;

  club::hub* _hub;
  uuid _debug_hub_id;
  bool _contact_sent;

  std::shared_ptr<SharedState> _shared_state;
};

inline
std::ostream& operator<<(std::ostream& os, Node::ConnectState s) {
  using S = Node::ConnectState;

  switch (s) {
    case S::not_connected: return os << "not_connected";
    case S::connecting:    return os << "connecting";
    case S::connected:     return os << "connected";
    case S::disconnected:  return os << "disconnected";
  }
}

} // club namespace

#endif // ifndef CLUB_NODE_H
