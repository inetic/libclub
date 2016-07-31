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

#ifndef CLUB_TRANSPORT_TRANSPORT_H
#define CLUB_TRANSPORT_TRANSPORT_H

#include <iostream>
#include <array>
#include <boost/asio/steady_timer.hpp>
#include <transport/transmit_queue.h>
#include <transport/message_reader.h>
#include <club/debug/ostream_uuid.h>

namespace club { namespace transport {

template<typename UnreliableId>
class Transport {
private:
  enum class SendState { sending, waiting, pending };

  static const size_t max_message_size = 65536;

  using udp = boost::asio::ip::udp;
  using OnReceive = std::function<void( const boost::system::error_code&
                                      , boost::asio::const_buffer )>;

  struct SocketState {
    bool                 was_destroyed;
    udp::endpoint        rx_endpoint;

    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;

    SocketState()
      : was_destroyed(false)
      , rx_buffer(max_message_size)
      , tx_buffer(max_message_size)
    {}
  };

public:
  using TransmitQueue = transport::TransmitQueue<UnreliableId>;
  using Core          = transport::Core<UnreliableId>;

public:
  Transport( uuid                  id
           , udp::socket           socket
           , udp::endpoint         remote_endpoint
           , std::shared_ptr<Core> core);


  Transport(Transport&&) = delete;
  Transport& operator=(Transport&&) = delete;

  const uuid& id() const { return core()._our_id; }

  void add_target(const uuid&);

  bool has_message(SequenceNumber sn) {
    return _transmit_queue.has_message(sn);
  }

  bool is_sending() const {
    return _send_state == SendState::sending
        || _send_state == SendState::waiting;
  }

  ~Transport();

private:
  friend class ::club::transport::Core<UnreliableId>;

  void insert_message( boost::optional<UnreliableId>
                     , std::shared_ptr<OutMessage> m);

  void start_receiving(std::shared_ptr<SocketState>);

  void on_receive( boost::system::error_code
                 , std::size_t
                 , std::shared_ptr<SocketState>);

  void start_sending(std::shared_ptr<SocketState>);

  void on_send( const boost::system::error_code&
              , size_t
              , std::shared_ptr<SocketState>);

  Core& core() { return _transmit_queue.core(); }
  const Core& core() const { return _transmit_queue.core(); }

  void handle_ack_entry(AckEntry);
  void handle_message(std::shared_ptr<SocketState>&, InMessage);

private:
  uuid                             _id;
  SendState                        _send_state;
  std::set<uuid>                   _targets;
  udp::socket                      _socket;
  udp::endpoint                    _remote_endpoint;
  TransmitQueue                    _transmit_queue;
  MessageReader                    _message_reader;
  boost::asio::steady_timer        _timer;
  std::shared_ptr<SocketState>     _socket_state;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<typename UnreliableId>
Transport<UnreliableId>
::Transport( uuid                  id
           , udp::socket           socket
           , udp::endpoint         remote_endpoint
           , std::shared_ptr<Core> core)
  : _id(std::move(id))
  , _send_state(SendState::pending)
  , _socket(std::move(socket))
  , _remote_endpoint(std::move(remote_endpoint))
  , _transmit_queue(std::move(core))
  , _timer(_socket.get_io_service())
  , _socket_state(std::make_shared<SocketState>())
{
  this->core().register_transport(this);

  start_receiving(_socket_state);
}

//------------------------------------------------------------------------------
template<typename UnreliableId>
Transport<UnreliableId>::~Transport() {
  core().unregister_transport(this);
  _socket_state->was_destroyed = true;
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::add_target(const uuid& id)
{
  if (_targets.insert(id).second) {
    core().add_target(id);
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::start_receiving(std::shared_ptr<SocketState> state)
{
  using boost::system::error_code;
  using std::move;

  auto s = state.get();

  _socket.async_receive_from( boost::asio::buffer(s->rx_buffer)
                            , s->rx_endpoint
                            , [this, state = move(state)]
                              (const error_code& e, std::size_t size) {
                                on_receive(e, size, move(state));
                              }
                            );
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::on_receive( boost::system::error_code    error
                              , std::size_t                  size
                              , std::shared_ptr<SocketState> state)
{
  using namespace std;
  namespace asio = boost::asio;

  if (state->was_destroyed) return;

  if (error) {
    //core().handle_send_error(error);
    assert(0 && "TODO: Handle send error");
  }

  // Ignore packets from unknown sources.
  if (!_remote_endpoint.address().is_unspecified()) {
    if (state->rx_endpoint != _remote_endpoint) {
      return start_receiving(move(state));
    }
  }

  _message_reader.set_data(state->rx_buffer.data(), size);

  // Parse Acks
  while (auto opt_ack_entry = _message_reader.read_one_ack_entry()) {
    assert(opt_ack_entry->from != id());
    if (opt_ack_entry->from == id()) continue;
    handle_ack_entry(std::move(*opt_ack_entry));
    if (state->was_destroyed) return;
  }

  // Parse messages
  while (auto opt_msg = _message_reader.read_one_message()) {
    handle_message(state, std::move(*opt_msg));
    if (state->was_destroyed) return;
  }

  start_receiving(move(state));
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::handle_ack_entry(AckEntry entry) {
  if (entry.to == _id) {
    assert(entry.from != _id);
    core().on_receive_acks(entry.from, entry.acks);
  }
  else {
    core().add_ack_entry(std::move(entry));
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::handle_message( std::shared_ptr<SocketState>& state
                                  , InMessage msg) {
  if (msg.source == _id) {
    assert(0 && "Our message was returned back");
    return;
  }

  // Notify user only if we're one of the targets.
  if (msg.targets.count(_id)) {
    msg.targets.erase(_id);

    core().on_receive(std::move(msg));

    if (state->was_destroyed) return;

    start_sending(_socket_state);
  }

  if (!msg.targets.empty()) {
    core().forward_message(std::move(msg));
  }
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::start_sending(std::shared_ptr<SocketState> state) {
  using boost::system::error_code;
  using std::move;
  using boost::asio::buffer;

  if (_send_state != SendState::pending) return;

  binary::encoder encoder(state->tx_buffer);

  size_t count = 0;

  // TODO: Should we limit the number of acks we encode here to guarantee
  //       some space for messages?
  count += core().encode_acks(encoder, _targets);
  count += _transmit_queue.encode_few(encoder, _targets);

  if (count == 0) {
    core().try_flush();
    return;
  }

  _send_state = SendState::sending;

  // Get the pointer here because `state` is moved from in arguments below
  // (and order of argument evaluation is undefined).
  auto s = state.get();

  _socket.async_send_to( buffer(s->tx_buffer.data(), encoder.written())
                       , _remote_endpoint
                       , [this, state = move(state)]
                         (const error_code& error, std::size_t size) {
                           on_send(error, size, move(state));
                         });
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::on_send( const boost::system::error_code& error
                           , size_t                           size
                           , std::shared_ptr<SocketState>     state)
{
  using std::move;
  using boost::system::error_code;

  if (state->was_destroyed) return;

  _send_state = SendState::pending;

  core().try_flush();

  if (error) {
    if (error == boost::asio::error::operation_aborted) {
      return;
    }
    assert(0);
  }

  _send_state = SendState::waiting;

  /*
   * Wikipedia says [1] that in practice 2G/GPRS capacity is 40kbit/s.
   * [1] https://en.wikipedia.org/wiki/2G
   *
   * We calculate delay:
   *   delay_s  = size / (400000/8)
   *   delay_us = 1000000 * size / (400000/8)
   *   delay_us = 20 * size
   *
   * TODO: Proper congestion control
   */
  _timer.expires_from_now(std::chrono::microseconds(20*size));
  _timer.async_wait([this, state = move(state)]
                    (const error_code error) {
                      if (state->was_destroyed) return;
                      _send_state = SendState::pending;
                      if (error) return;
                      start_sending(move(state));
                    });
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::insert_message( boost::optional<Id> unreliable_id
                                  , std::shared_ptr<OutMessage> m) {
  _transmit_queue.insert_message(std::move(unreliable_id), std::move(m));
  start_sending(_socket_state);
}

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_TRANSPORT_H
