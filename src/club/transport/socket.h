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

#ifndef CLUB_TRANSPORT_SOCKET_H
#define CLUB_TRANSPORT_SOCKET_H

#include <iostream>
#include <array>
#include <boost/asio/steady_timer.hpp>
#include <transport/transmit_queue.h>
#include <club/debug/ostream_uuid.h>
#include <transport/out_message.h>
#include <async/alarm.h>
#include "error.h"

namespace club { namespace transport {

class SocketImpl {
private:
  enum class SendState { sending, waiting, pending };

public:
  static const size_t packet_size = 1452;

  using OnReceive = std::function<void( const boost::system::error_code&
                                      , boost::asio::const_buffer )>;
  using OnFlush = std::function<void()>;

private:
  using udp = boost::asio::ip::udp;

  struct SocketState {
    bool                 was_destroyed;
    udp::endpoint        rx_endpoint;

    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;

    SocketState()
      : was_destroyed(false)
      , rx_buffer(packet_size)
      , tx_buffer(packet_size)
    {}
  };

  using SocketStatePtr = std::shared_ptr<SocketState>;

public:
  using TransmitQueue = transport::TransmitQueue<OutMessage>;

public:
  SocketImpl(boost::asio::io_service&);

  SocketImpl( udp::socket   socket
            , udp::endpoint remote_endpoint);

  SocketImpl(SocketImpl&&) = delete;
  SocketImpl& operator=(SocketImpl&&) = delete;

  bool is_sending() const {
    return _send_state == SendState::sending
        || _send_state == SendState::waiting;
  }

  ~SocketImpl();

  udp::endpoint local_endpoint() const;
  void rendezvous_connect(udp::endpoint);
  void receive_unreliable(OnReceive);
  void receive_reliable(OnReceive);
  void send_unreliable(std::vector<uint8_t>);
  void send_reliable(std::vector<uint8_t>);
  void flush(OnFlush);
  void close();

private:
  void handle_error(const boost::system::error_code&);

  void start_receiving(SocketStatePtr);

  void on_receive( boost::system::error_code
                 , std::size_t
                 , SocketStatePtr);

  void start_sending(SocketStatePtr);

  void on_send( const boost::system::error_code&
              , size_t
              , SocketStatePtr);

  void handle_acks(AckSet);
  void handle_message(SocketStatePtr&, InMessagePart);

  bool try_encode(binary::encoder&, OutMessage&) const;

  void encode(binary::encoder&, OutMessage&) const;

  void handle_sync_message(const InMessagePart&);
  void handle_unreliable_message(SocketStatePtr&, const InMessagePart&);
  void handle_reliable_message(SocketStatePtr&, const InMessagePart&);

  void replay_pending_messages(SocketStatePtr&);

  bool user_handle_reliable_msg(SocketStatePtr&, InMessageFull&);

  template<typename ...Ts>
  void add_message(Ts&&... params) {
    _transmit_queue.emplace(std::forward<Ts>(params)...);
  }

  void on_recv_timeout_alarm();
  void on_send_keepalive_alarm(SocketStatePtr);

  async::alarm::duration recv_timeout_duration() const {
    return keepalive_period() * 5;
  }

  async::alarm::duration keepalive_period() const {
    return std::chrono::milliseconds(200);
  }

private:
  using PendingMessages = std::map<SequenceNumber, PendingMessage>;

  struct Sync {
    SequenceNumber last_used_reliable_sn;
    SequenceNumber last_used_unreliable_sn;
  };

  SendState                        _send_state;
  udp::socket                      _socket;
  udp::endpoint                    _remote_endpoint;
  TransmitQueue                    _transmit_queue;
  boost::asio::steady_timer        _timer;
  SocketStatePtr                   _socket_state;
  // If this is nonet, then we haven't yet received sync
  boost::optional<Sync>            _sync;
  PendingMessages                  _pending_reliable_messages;
  boost::optional<PendingMessage>  _pending_unreliable_message;
  bool                             _schedule_sending_acks = false;
  AckSet _received_message_ids_by_peer;
  AckSet _received_message_ids;

  SequenceNumber _next_reliable_sn   = 0;
  SequenceNumber _next_unreliable_sn = 1;

  OnReceive _on_receive_reliable;
  OnReceive _on_receive_unreliable;

  OnFlush _on_flush;

  async::alarm                     _recv_timeout_alarm;
  async::alarm                     _send_keepalive_alarm;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline
SocketImpl::SocketImpl( udp::socket   socket
                      , udp::endpoint remote_endpoint)
  : _send_state(SendState::pending)
  , _socket(std::move(socket))
  , _timer(_socket.get_io_service())
  , _socket_state(std::make_shared<SocketState>())
  , _recv_timeout_alarm(_socket.get_io_service(), [this]() { on_recv_timeout_alarm(); })
  , _send_keepalive_alarm(_socket.get_io_service(), [=]() { on_send_keepalive_alarm(_socket_state); })
{
  rendezvous_connect(remote_endpoint);
}

//------------------------------------------------------------------------------
inline
SocketImpl::SocketImpl(boost::asio::io_service& ios)
  : _send_state(SendState::pending)
  , _socket(ios, udp::endpoint(udp::v4(), 0))
  , _timer(_socket.get_io_service())
  , _socket_state(std::make_shared<SocketState>())
  , _recv_timeout_alarm(_socket.get_io_service(), [this]() { on_recv_timeout_alarm(); })
  , _send_keepalive_alarm(_socket.get_io_service(), [=]() { on_send_keepalive_alarm(_socket_state); })
{
}

//------------------------------------------------------------------------------
inline
void SocketImpl::rendezvous_connect(udp::endpoint remote_ep) {
  _remote_endpoint = std::move(remote_ep);
  add_message(true, MessageType::sync, _next_reliable_sn++, std::vector<uint8_t>());
  start_sending(_socket_state);
  start_receiving(_socket_state);
}

//------------------------------------------------------------------------------
inline
boost::asio::ip::udp::endpoint SocketImpl::local_endpoint() const {
  return _socket.local_endpoint();
}

//------------------------------------------------------------------------------
inline
SocketImpl::~SocketImpl() {
  _socket_state->was_destroyed = true;
}

//------------------------------------------------------------------------------
inline
void SocketImpl::receive_unreliable(OnReceive on_receive) {
  _on_receive_unreliable = std::move(on_receive);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::receive_reliable(OnReceive on_receive) {
  _on_receive_reliable = std::move(on_receive);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::send_unreliable(std::vector<uint8_t> data) {
  add_message(false, MessageType::unreliable, _next_unreliable_sn++, std::move(data));
  start_sending(_socket_state);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::send_reliable(std::vector<uint8_t> data) {
  add_message(true, MessageType::reliable, _next_reliable_sn++, std::move(data));
  start_sending(_socket_state);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::start_receiving(SocketStatePtr state)
{
  using boost::system::error_code;
  using std::move;

  auto s = state.get();

  _recv_timeout_alarm.start(recv_timeout_duration());

  _socket.async_receive_from( boost::asio::buffer(s->rx_buffer)
                            , s->rx_endpoint
                            , [this, state = move(state)]
                              (const error_code& e, std::size_t size) {
                                on_receive(e, size, move(state));
                              }
                            );
}

//------------------------------------------------------------------------------
inline void SocketImpl::flush(OnFlush on_flush) {
  _on_flush = on_flush;
}

//------------------------------------------------------------------------------
inline void SocketImpl::close() {
  _timer.cancel();
  _socket.close();
  _recv_timeout_alarm.stop();
  _send_keepalive_alarm.stop();
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_error(const boost::system::error_code& err) {
  auto state = _socket_state;

  close();

  auto r1 = std::move(_on_receive_unreliable);
  auto r2 = std::move(_on_receive_reliable);
  if (r1) r1(err, boost::asio::const_buffer());
  if (r2) r2(err, boost::asio::const_buffer());
}

//------------------------------------------------------------------------------
inline
void SocketImpl::on_receive( boost::system::error_code error
                           , std::size_t               size
                           , SocketStatePtr            state)
{
  using namespace std;
  namespace asio = boost::asio;

  if (state->was_destroyed) return;

  _recv_timeout_alarm.stop();

  if (error) {
    auto r1 = std::move(_on_receive_unreliable);
    auto r2 = std::move(_on_receive_reliable);
    if (r1) r1(error, asio::const_buffer());
    if (r2) r2(error, asio::const_buffer());
    return;
  }

  // Ignore packets from unknown sources.
  if (!_remote_endpoint.address().is_unspecified()) {
    if (state->rx_endpoint != _remote_endpoint) {
      return start_receiving(move(state));
    }
  }

  binary::decoder decoder(state->rx_buffer);

  auto ack_set = decoder.get<AckSet>();

  if (decoder.error()) { return handle_error(transport::error::parse_error); }

  handle_acks(ack_set);

  auto message_count = decoder.get<uint16_t>();
  assert(!decoder.error());

  for (uint16_t i = 0; i < message_count; ++i) {
    auto m = decoder.get<InMessagePart>();
    if (decoder.error()) {
      return handle_error(transport::error::parse_error);
    }
    handle_message(state, std::move(m));
    if (state->was_destroyed) return;
  }

  start_receiving(move(state));
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_acks(AckSet acks) {
  // TODO: If we receive an older packet than we've already received, this
  // is going to reduce our information.
  _received_message_ids_by_peer = acks;
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_message(SocketStatePtr& state, InMessagePart msg) {
  switch (msg.type) {
    case MessageType::sync:       handle_sync_message(msg); break;
    case MessageType::keep_alive: break;
    case MessageType::unreliable: handle_unreliable_message(state, msg); break;
    case MessageType::reliable:   handle_reliable_message(state, msg); break;
    default: return handle_error(error::parse_error);
  }

  if (!state->was_destroyed) {
    start_sending(_socket_state);
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_sync_message(const InMessagePart& msg) {
  _schedule_sending_acks = true;
  if (!_sync) {
    _received_message_ids.try_add(msg.sequence_number);
    _sync = Sync{msg.sequence_number, msg.sequence_number};
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_reliable_message( SocketStatePtr& state
                                        , const InMessagePart& msg) {
  _schedule_sending_acks = true;
  if (!_sync) return;
  if (!_received_message_ids.can_add(msg.sequence_number)) return;

  if (msg.sequence_number == _sync->last_used_reliable_sn + 1) {
    if (auto full_msg = msg.get_full_message()) {
      if (!user_handle_reliable_msg(state, *full_msg)) return;
      return replay_pending_messages(state);
    }
  }

  auto i = _pending_reliable_messages.find(msg.sequence_number);

  if (i == _pending_reliable_messages.end()) {
    i = _pending_reliable_messages.emplace(msg.sequence_number, msg).first;
  }
  else {
    i->second.update_payload(msg.chunk_start, msg.payload);
    replay_pending_messages(state);
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::replay_pending_messages(SocketStatePtr& state) {
  auto& pms = _pending_reliable_messages;

  for (auto i = pms.begin(); i != pms.end();) {
    auto& pm = i->second;

    if (pm.sequence_number == _sync->last_used_reliable_sn + 1) {
      auto full_message = pm.get_full_message();
      if (!full_message) return;
      if (!user_handle_reliable_msg(state, *full_message)) return;
      i = pms.erase(i);
    }
    else {
      ++i;
    }
  }
}

//------------------------------------------------------------------------------
inline
bool SocketImpl::user_handle_reliable_msg( SocketStatePtr& state
                                         , InMessageFull& msg) {
  auto f = std::move(_on_receive_reliable);
  f(boost::system::error_code(), msg.payload);
  if (state->was_destroyed) return false;
  _received_message_ids.try_add(msg.sequence_number);
  _sync->last_used_reliable_sn = msg.sequence_number;
  return true;
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_unreliable_message( SocketStatePtr& state
                                          , const InMessagePart& msg) {
  if (!_on_receive_unreliable) return;
  if (!_sync) return;
  if (msg.sequence_number <= _sync->last_used_unreliable_sn) return;

  auto& opm = _pending_unreliable_message;

  if (msg.is_full()) {
    auto r = std::move(_on_receive_unreliable);
    r(boost::system::error_code(), msg.payload);
    if (state->was_destroyed) return;
    _sync->last_used_unreliable_sn = msg.sequence_number;
    opm = boost::none;
    return;
  }

  if (!opm || opm->sequence_number < msg.sequence_number) {
    opm.emplace(msg);
    return;
  }

  auto& pm = *_pending_unreliable_message;

  if (pm.sequence_number > msg.sequence_number) {
    return;
  }

  assert(pm.sequence_number == msg.sequence_number);

  pm.update_payload(msg.chunk_start, msg.payload);

  if (pm.is_full()) {
    auto r = std::move(_on_receive_unreliable);
    r(boost::system::error_code(), pm.payload);
    if (state->was_destroyed) return;
    _sync->last_used_unreliable_sn = msg.sequence_number;
    opm = boost::none;
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::start_sending(SocketStatePtr state) {
  using boost::system::error_code;
  using boost::asio::buffer;

  if (!_socket.is_open()) return;
  if (_send_state != SendState::pending) return;

  binary::encoder encoder(state->tx_buffer);

  // Encode acks
  encoder.put(_received_message_ids);

  assert(!encoder.error());

  // Encode payload
  auto cycle = _transmit_queue.cycle();

  size_t count = 0;
  auto count_encoder = encoder;
  encoder.put<uint16_t>(0);

  for (auto mi = cycle.begin(); mi != cycle.end();) {
    if (_received_message_ids_by_peer.is_in(mi->sequence_number())) {
      mi.erase();
      continue;
    }

    if (!try_encode(encoder, *mi)) {
      break;
    }

    ++count;

    if (mi->bytes_already_sent != mi->payload_size()) {
      // It means we've exhausted the buffer in encoder.
      break;
    }

    // Unreliable entries are sent only once.
    if (!mi->resend_until_acked) {
      mi.erase();
      continue;
    }
    
    ++mi;
  }

  if (count == 0 && !_schedule_sending_acks) {
    if (_on_flush) {
      auto f = std::move(_on_flush);
      f();
      if (state->was_destroyed) return;
      if (!_socket.is_open()) return;
    }
    _send_keepalive_alarm.start(keepalive_period());
    return;
  }

  count_encoder.put<uint16_t>(count);

  _schedule_sending_acks = false;

  assert(encoder.written());

  _send_state = SendState::sending;

  // Get the pointer here because `state` is moved from in arguments below
  // (and order of argument evaluation is undefined).
  auto s = state.get();

  _socket.async_send_to( buffer(s->tx_buffer.data(), encoder.written())
                       , _remote_endpoint
                       , [this, state = std::move(state)]
                         (const error_code& error, std::size_t size) {
                           on_send(error, size, std::move(state));
                         });
}

//------------------------------------------------------------------------------
inline
void SocketImpl::on_send( const boost::system::error_code& error
                        , size_t                           size
                        , SocketStatePtr                   state)
{
  using std::move;
  using boost::system::error_code;

  if (state->was_destroyed) return;

  _send_state = SendState::pending;

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
   *   delay_s  = size / (40000/8)
   *   delay_us = 1000000 * size / (40000/8)
   *   delay_us = 200 * size
   *
   * TODO: Proper congestion control
   */
  if (_remote_endpoint.address().is_unspecified()) {
    // No need to wait when we're on the same PC. Would have been nicer
    // if we didn't use timer at all in this case, but this is gonna
    // have to be refactored due to proper congestion control.
    _timer.expires_from_now(std::chrono::microseconds(0));
  }
  else {
    _timer.expires_from_now(std::chrono::microseconds(200*size));
  }

  _timer.async_wait([this, state = move(state)]
                    (const error_code error) {
                      if (state->was_destroyed) return;
                      _send_state = SendState::pending;
                      if (error) return;
                      start_sending(move(state));
                    });
}

//------------------------------------------------------------------------------
inline
bool
SocketImpl::try_encode(binary::encoder& encoder, OutMessage& message) const {

  auto minimal_encoded_size =
      OutMessage::header_size
      // We'd want to send at least one byte of the payload,
      // otherwise what's the point.
      + std::min<size_t>(1, message.payload_size()) ;

  if (minimal_encoded_size > encoder.remaining_size()) {
    return false;
  }

  encode(encoder, message);

  assert(!encoder.error());

  return true;
}

//------------------------------------------------------------------------------
inline
void
SocketImpl::encode( binary::encoder& encoder, OutMessage& message) const {
  auto& m = message;

  if (m.bytes_already_sent == m.payload_size()) {
    m.bytes_already_sent = 0;
  }

  uint16_t payload_size = m.encode_header_and_payload( encoder
                                                     , m.bytes_already_sent);

  if (encoder.error()) {
    assert(0);
    return;
  }

  m.bytes_already_sent += payload_size;
}

inline void SocketImpl::on_recv_timeout_alarm() {
  handle_error(error::timed_out);
}

inline void SocketImpl::on_send_keepalive_alarm(SocketStatePtr state) {
  add_message(false, MessageType::keep_alive, 0, std::vector<uint8_t>());
  start_sending(std::move(state));
}

//------------------------------------------------------------------------------
// Socket
//------------------------------------------------------------------------------
class Socket {
private:
  using udp = boost::asio::ip::udp;

  std::unique_ptr<SocketImpl> _impl;

public:
  static const size_t packet_size = SocketImpl::packet_size;

  using OnReceive = SocketImpl::OnReceive;
  using OnFlush = SocketImpl::OnFlush;

public:
  Socket(boost::asio::io_service& ios)
    : _impl(std::make_unique<SocketImpl>(ios))
  {}

  Socket( udp::socket   socket
        , udp::endpoint remote_endpoint)
    : _impl(std::make_unique<SocketImpl>( std::move(socket)
                                        , std::move(remote_endpoint)))
  {}

  Socket(Socket&& other)
    : _impl(std::move(other._impl)) {}

  Socket& operator = (Socket&& other) {
    _impl = std::move(other._impl);
    return *this;
  }

  udp::endpoint local_endpoint() const {
    return _impl->local_endpoint();
  }

  void rendezvous_connect(udp::endpoint remote_ep) {
    _impl->rendezvous_connect(std::move(remote_ep));
  }

  void receive_unreliable(OnReceive _1) {
    _impl->receive_unreliable(std::move(_1));
  }

  void receive_reliable(OnReceive _1) {
    _impl->receive_reliable(std::move(_1));
  }

  void send_unreliable(std::vector<uint8_t> _1) {
    _impl->send_unreliable(std::move(_1));
  }

  void send_reliable(std::vector<uint8_t> _1) {
    _impl->send_reliable(std::move(_1));
  }

  void flush(OnFlush _1) {
    _impl->flush(std::move(_1));
  }

  void close() {
    _impl->close();
  }
};

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_SOCKET_H
