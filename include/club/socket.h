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

#ifndef CLUB_SOCKET_H
#define CLUB_SOCKET_H

#include <iostream>
#include <array>
#include <queue>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <club/transport/transmit_queue.h>
#include <club/debug/ostream_uuid.h>
#include <club/transport/out_message.h>
#include <async/alarm.h>
#include <club/transport/error.h>
#include <club/transport/punch_hole.h>
#include <club/transport/quality_of_service.h>
#include <club/transport/packet.h>
#include <club/debug/log.h>

namespace club {

class SocketImpl : public std::enable_shared_from_this<SocketImpl> {
private:
  enum class SendState { sending, pending };

public:
  static constexpr size_t packet_size = transport::QualityOfService::MSS();

  using OnReceive = std::function<void( const boost::system::error_code&
                                      , boost::asio::const_buffer )>;
  using OnSend = std::function<void(const boost::system::error_code&)>;
  using OnFlush = std::function<void()>;

private:
  using clock = std::chrono::steady_clock;
  using udp = boost::asio::ip::udp;

  using Self = std::shared_ptr<SocketImpl>;

public:
  using TransmitQueue = transport::TransmitQueue;
  using OutMessage = transport::OutMessage;
  using InMessagePart = transport::InMessagePart;
  using InMessageFull = transport::InMessageFull;
  using PendingMessage = transport::PendingMessage;
  using SequenceNumber = transport::SequenceNumber;
  using AckSet = transport::AckSet;
  using MessageType = transport::MessageType;
  using error = transport::error;

public:
  SocketImpl(boost::asio::io_service&);
  SocketImpl(udp::socket);

  SocketImpl( udp::socket   socket
            , udp::endpoint remote_endpoint);

  SocketImpl(SocketImpl&&) = delete;
  SocketImpl& operator=(SocketImpl&&) = delete;

  ~SocketImpl();

  udp::endpoint local_endpoint() const;
  boost::optional<udp::endpoint> remote_endpoint() const;

  template<class OnConnect>
  void rendezvous_connect(udp::endpoint, OnConnect);

  void receive_unreliable(OnReceive);
  void receive_reliable(OnReceive);
  void send_unreliable(std::vector<uint8_t>, OnSend);
  void send_reliable(std::vector<uint8_t>, OnSend);
  void flush(OnFlush);
  void close();

  udp::socket& get_socket_impl() {
    return _socket;
  }

  // If we don't receive any packet during this duration, the socket
  // close shall be called and handler shall execute with timed_out
  // error.
  async::alarm::duration recv_timeout_duration() const {
    return keepalive_period() * 45;
  }

  boost::asio::io_service& get_io_service() {
    return _socket.get_io_service();
  }

  async::alarm::duration keepalive_period() const {
    return _keepalive_period;
  }

  void keepalive_period(async::alarm::duration d) {
    _keepalive_period = d;
    _send_keepalive_alarm.start(_keepalive_period);
  }

private:
  void handle_error(const boost::system::error_code&);

  void start_receiving();

  void on_receive( boost::system::error_code
                 , std::size_t);

  void start_sending();

  void on_send(const boost::system::error_code&, size_t);

  void handle_message(InMessagePart);

  bool try_encode(binary::encoder&, OutMessage&) const;

  void encode(binary::encoder&, OutMessage&) const;

  void handle_sync_message(const InMessagePart&);
  void handle_close_message();
  void handle_unreliable_message(const InMessagePart&);
  void handle_reliable_message(const InMessagePart&);

  void replay_pending_messages();

  bool user_handle_reliable_msg(InMessageFull&);

  template<typename ...Ts>
  void add_message(Ts&&... params) {
    _transmit_queue.insert(OutMessage(std::forward<Ts>(params)...));
  }

  void on_recv_timeout_alarm();
  void on_send_keepalive_alarm();

  void sync_send_close_message();

  std::vector<uint8_t>
  construct_packet_with_one_message(OutMessage& m);

  static udp::endpoint sanitize_address(udp::endpoint);

  uint64_t time() const {
    using namespace std::chrono;
    auto t = clock::now().time_since_epoch();
    return duration_cast<milliseconds>(t).count();
  }

  void exec_on_send_handlers(boost::system::error_code);
  bool can_exec_on_send_handlers() const;

  template<class... Ts> void print(Ts&&...) const;

private:
  using PendingMessages = std::map<SequenceNumber, PendingMessage>;

  struct Sync {
    SequenceNumber last_used_reliable_sn;
    SequenceNumber last_used_unreliable_sn;
  };

  boost::asio::strand              _strand;
  async::alarm::duration           _keepalive_period = std::chrono::milliseconds(500);
  SendState                        _send_state;
  udp::socket                      _socket;
  udp::endpoint                    _remote_endpoint;
  TransmitQueue                    _transmit_queue;

  udp::endpoint        rx_endpoint;
  std::vector<uint8_t> rx_buffer = std::vector<uint8_t>(packet_size);
  std::vector<uint8_t> tx_buffer = std::vector<uint8_t>(packet_size);

  // If this is not set, then we haven't yet received sync
  boost::optional<Sync>            _sync;
  PendingMessages                  _pending_reliable_messages;
  boost::optional<PendingMessage>  _pending_unreliable_message;
  AckSet _received_message_ids;

  SequenceNumber _next_reliable_sn   = 0;
  SequenceNumber _next_unreliable_sn = 1;

  OnReceive _on_receive_reliable;
  OnReceive _on_receive_unreliable;
  std::queue<OnSend> _on_send;

  OnFlush _on_flush;

  async::alarm                     _recv_timeout_alarm;
  async::alarm                     _send_keepalive_alarm;

  transport::QualityOfService _qos;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline
SocketImpl::SocketImpl(boost::asio::io_service& ios)
  : _strand(ios)
  , _send_state(SendState::pending)
  , _socket(ios, udp::endpoint(udp::v4(), 0))
  , _recv_timeout_alarm(_socket.get_io_service(), [this]() { on_recv_timeout_alarm(); })
  , _send_keepalive_alarm(_socket.get_io_service(), [=]() { on_send_keepalive_alarm(); })
{
}

inline
SocketImpl::SocketImpl(udp::socket udp_socket)
  : _strand(udp_socket.get_io_service())
  , _send_state(SendState::pending)
  , _socket(std::move(udp_socket))
  , _recv_timeout_alarm(_socket.get_io_service(), [this]() { on_recv_timeout_alarm(); })
  , _send_keepalive_alarm(_socket.get_io_service(), [=]() { on_send_keepalive_alarm(); })
{
}

//------------------------------------------------------------------------------
template<class OnConnect>
inline
void SocketImpl::rendezvous_connect(udp::endpoint remote_ep, OnConnect on_connect) {
  using std::move;
  using boost::system::error_code;

  namespace asio = boost::asio;

  remote_ep = sanitize_address(remote_ep);

  auto syn_message = OutMessage( true
                               , MessageType::sync
                               , _next_reliable_sn++
                               , std::vector<uint8_t>());

  auto packet = construct_packet_with_one_message(syn_message);

  // TODO: Optimization: When hole punch receives a package from the remote
  //       we should add it's sequence number into _received_message_ids_by_peer
  //       so that we can acknowledge it asap.
  auto on_punch = [ this
                  , on_connect = move(on_connect)
                  , self = shared_from_this()
                  , syn_message = move(syn_message)]
                  (error_code error, udp::endpoint remote_ep) mutable {
    if (error) return on_connect(error);
    _remote_endpoint = remote_ep;
    _transmit_queue.insert(std::move(syn_message));
    start_sending();
    start_receiving();
    _send_keepalive_alarm.start(_keepalive_period);
    return on_connect(error);
  };

  transport::punch_hole(_socket, remote_ep, std::move(packet), std::move(on_punch));
}

//------------------------------------------------------------------------------
inline
boost::asio::ip::udp::endpoint SocketImpl::local_endpoint() const {
  return _socket.local_endpoint();
}

//------------------------------------------------------------------------------
inline
boost::optional<boost::asio::ip::udp::endpoint>
SocketImpl::remote_endpoint() const {
  return _remote_endpoint;
}

//------------------------------------------------------------------------------
inline
SocketImpl::~SocketImpl() {
  close();
}

//------------------------------------------------------------------------------
inline
void SocketImpl::receive_unreliable(OnReceive on_recv) {
  _on_receive_unreliable = std::move(on_recv);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::receive_reliable(OnReceive on_recv) {
  _on_receive_reliable = std::move(on_recv);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::send_unreliable(std::vector<uint8_t> data, OnSend on_send) {
  _on_send.push(std::move(on_send));
  add_message(false, MessageType::unreliable, _next_unreliable_sn++, std::move(data));
  start_sending();
}

//------------------------------------------------------------------------------
inline
void SocketImpl::send_reliable(std::vector<uint8_t> data, OnSend on_send) {
  _on_send.push(std::move(on_send));
  add_message(true, MessageType::reliable, _next_reliable_sn++, std::move(data));
  start_sending();
}

//------------------------------------------------------------------------------
inline
void SocketImpl::start_receiving()
{
  using boost::system::error_code;
  using std::move;

  _recv_timeout_alarm.start(recv_timeout_duration());

  _socket.async_receive_from
      ( boost::asio::buffer(rx_buffer)
      , rx_endpoint
      , _strand.wrap([self = shared_from_this()]
                     (const error_code& e, std::size_t size) {
                       self->on_receive(e, size);
                     })
      );
}

//------------------------------------------------------------------------------
inline void SocketImpl::flush(OnFlush on_flush) {
  assert(!_on_flush);
  _on_flush = std::move(on_flush);

  if (_transmit_queue.empty() && _send_state != SendState::sending) {
    _strand.dispatch([this, self = shared_from_this()]() {
        // If we're currently sending, then the on_send handler
        // shall flush.
        if (_send_state == SendState::sending) return;

        if (_transmit_queue.empty() && _on_flush) {
          move_exec(_on_flush);
        }
      });
  }
}

//------------------------------------------------------------------------------
inline void SocketImpl::close() {
  if (_socket.is_open()) {
    sync_send_close_message();
    _socket.close();
  }
  _recv_timeout_alarm.stop();
  _send_keepalive_alarm.stop();
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
inline
void SocketImpl::handle_error(const boost::system::error_code& err) {
  close();

  auto r1 = std::move(_on_receive_unreliable);
  auto r2 = std::move(_on_receive_reliable);
  if (r1) r1(err, boost::asio::const_buffer());
  if (r2) r2(err, boost::asio::const_buffer());

  // TODO: Should probably execute send handler as well.
}

//------------------------------------------------------------------------------
inline
void SocketImpl::on_receive( boost::system::error_code error
                           , std::size_t               size)
{
  using namespace std;
  namespace asio = boost::asio;

  _recv_timeout_alarm.stop();

  if (error) {
    return handle_error(error);
  }

  // Ignore packets from unknown sources.
  if (!_remote_endpoint.address().is_unspecified()) {
    if (rx_endpoint != _remote_endpoint) {
      return start_receiving();
    }
  }

  transport::PacketDecoder decoder(_qos, rx_buffer);

  decoder.decode_header();

  if (decoder.error()) return handle_error(transport::error::parse_error);

  while (auto m = decoder.decode_message()) {
    if (decoder.error()) {
      return handle_error(transport::error::parse_error);
    }
    handle_message(std::move(*m));
    if (!_socket.is_open()) return;
  }

  if (can_exec_on_send_handlers()) {
    exec_on_send_handlers(error);
  }

  start_receiving();
  start_sending();
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_message(InMessagePart msg) {
  switch (msg.type) {
    case MessageType::sync:       handle_sync_message(msg); break;
    case MessageType::keep_alive: break;
    case MessageType::unreliable: handle_unreliable_message(msg); break;
    case MessageType::reliable:   handle_reliable_message(msg); break;
    case MessageType::close:      handle_close_message(); break;
    default: return handle_error(error::parse_error);
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_close_message() {
  _socket.close();
  handle_error(boost::asio::error::connection_reset);
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_sync_message(const InMessagePart& msg) {
  if (!_sync) {
    _received_message_ids.try_add(msg.sequence_number);
    _sync = Sync{msg.sequence_number, msg.sequence_number};
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_reliable_message(const InMessagePart& msg) {
  if (!_sync) return;
  if (!_received_message_ids.can_add(msg.sequence_number)) return;

  if (msg.sequence_number == _sync->last_used_reliable_sn + 1) {
    if (auto full_msg = msg.get_complete_message()) {
      if (!user_handle_reliable_msg(*full_msg)) return;
      return replay_pending_messages();
    }
  }

  auto i = _pending_reliable_messages.find(msg.sequence_number);

  if (i == _pending_reliable_messages.end()) {
    i = _pending_reliable_messages.emplace(msg.sequence_number, msg).first;
  }
  else {
    i->second.update_payload(msg.chunk_start, msg.payload);
    replay_pending_messages();
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::replay_pending_messages() {
  auto& pms = _pending_reliable_messages;

  for (auto i = pms.begin(); i != pms.end();) {
    auto& pm = i->second;

    if (pm.sequence_number == _sync->last_used_reliable_sn + 1) {
      auto full_message = pm.get_complete_message();
      if (!full_message) return;
      if (!user_handle_reliable_msg(*full_message)) return;
      i = pms.erase(i);
    }
    else {
      ++i;
    }
  }
}

//------------------------------------------------------------------------------
inline
bool SocketImpl::user_handle_reliable_msg(InMessageFull& msg) {
  if (!_on_receive_reliable) return false;
  // The callback may hold a shared_ptr to this, so I placed the scope
  // here so that 'f' would get destroyed and thus state->was_destroyed
  // would be relevant in the line below.
  {
    auto f = std::move(_on_receive_reliable);
    f(boost::system::error_code(), msg.payload);
  }
  _received_message_ids.try_add(msg.sequence_number);
  _sync->last_used_reliable_sn = msg.sequence_number;
  return true;
}

//------------------------------------------------------------------------------
inline
void SocketImpl::handle_unreliable_message(const InMessagePart& msg) {
  if (!_on_receive_unreliable) return;
  if (!_sync) return;
  if (msg.sequence_number <= _sync->last_used_unreliable_sn) return;

  auto& opm = _pending_unreliable_message;

  if (msg.is_complete()) {
    auto r = std::move(_on_receive_unreliable);
    r(boost::system::error_code(), msg.payload);
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

  if (pm.is_complete()) {
    auto r = std::move(_on_receive_unreliable);
    r(boost::system::error_code(), pm.payload);
    _sync->last_used_unreliable_sn = msg.sequence_number;
    opm = boost::none;
  }
}

//------------------------------------------------------------------------------
inline
void SocketImpl::start_sending() {
  using boost::system::error_code;
  using boost::asio::buffer;

  if (!_socket.is_open()) return;
  if (_send_state != SendState::pending) return;

  auto opt_encoded_size = encode_packet( _qos
                                       , _transmit_queue
                                       , _received_message_ids
                                       , tx_buffer);

  if (!opt_encoded_size) {
    _send_keepalive_alarm.start(_keepalive_period);

    return _strand.dispatch([this, self = shared_from_this()]() {
        if (_send_state != SendState::pending) return;

        if (can_exec_on_send_handlers()) {
          exec_on_send_handlers(error_code());
        }
        if (_transmit_queue.empty() && _on_flush) {
          move_exec(_on_flush);
        }
      });
  }

  _send_state = SendState::sending;

  _socket.async_send_to
      ( buffer(tx_buffer.data(), *opt_encoded_size)
      , _remote_endpoint
      , _strand.wrap([self = shared_from_this()]
                     (const error_code& error, std::size_t size) {
                       self->on_send(error, size);
                     }));
}

//------------------------------------------------------------------------------
inline
void SocketImpl::on_send( const boost::system::error_code& error
                        , size_t                           size)
{
  using std::move;
  using boost::system::error_code;

  //club::log(">>> ", time(), " ", _socket.local_endpoint().port(),
  //  " -> ", _remote_endpoint.port(), " ", _qos, " ", _on_send.size());

  _send_state = SendState::pending;

  if (error) {
    assert(error == boost::asio::error::operation_aborted);
    return exec_on_send_handlers(error);
  }

  if (can_exec_on_send_handlers()) {
    exec_on_send_handlers(error_code());
  }

  // If no payload was encoded and there is no need to
  // re-send acks, then flush end return.
  if (_transmit_queue.empty() && _on_flush) {
    move_exec(_on_flush);
    if (!_socket.is_open()) return;
  }

  start_sending();
}

//------------------------------------------------------------------------------
inline bool SocketImpl::can_exec_on_send_handlers() const {
  return _transmit_queue.size_in_bytes() < _qos.cwnd();
}

//------------------------------------------------------------------------------
inline void SocketImpl::exec_on_send_handlers(boost::system::error_code error) {
  auto fs = move(_on_send);
  while (!fs.empty()) {
    auto f = std::move(fs.front());
    f(error);
    fs.pop();
  }
}

//------------------------------------------------------------------------------
inline
std::vector<uint8_t>
SocketImpl::construct_packet_with_one_message(OutMessage& m) {
  std::vector<uint8_t> data(packet_size);

  auto opt_encoded_size = encode_packet_with_one_message( _qos
                                                        , m
                                                        , _received_message_ids
                                                        , data);

  return data;
}

//------------------------------------------------------------------------------
inline
void SocketImpl::sync_send_close_message() {
  OutMessage m( false
              , MessageType::close
              , 0
              , std::vector<uint8_t>());

  auto data = construct_packet_with_one_message(m);

  _socket.send_to(boost::asio::buffer(data), _remote_endpoint);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
inline void SocketImpl::on_recv_timeout_alarm() {
  handle_error(error::timed_out);
}

inline void SocketImpl::on_send_keepalive_alarm() {
  if (_transmit_queue.empty()) {
    add_message(false, MessageType::keep_alive, 0, std::vector<uint8_t>());
  }
  else {
    // There are data in the transmit queue that has not been acknowledged
    // for a signifficant time. We clear the in_flight info of _qos to
    // indicate we consider the data in flight to be lost and that they need to
    // be resent.
    // TODO: Should probably also reset cwnd to its default value.
    _qos.clear_in_flight_info();
  }

  start_sending();
}

//------------------------------------------------------------------------------
inline
boost::asio::ip::udp::endpoint
SocketImpl::sanitize_address(udp::endpoint ep) {
  namespace ip = boost::asio::ip;

  if (ep.address().is_unspecified()) {
    if (ep.address().is_v4()) {
      ep = udp::endpoint(ip::address_v4::loopback(), ep.port());
    }
    else {
      ep = udp::endpoint(ip::address_v6::loopback(), ep.port());
    }
  }

  return ep;
}

//------------------------------------------------------------------------------
template<class... Ts> void SocketImpl::print(Ts&&... args) const {
  //std::cout << this << " " << time() << " ";
  //socket_print_(std::forward<Ts>(args)...);
  club::log(this, " ", time(), " ", std::forward<Ts>(args)...);
}

//------------------------------------------------------------------------------
// Socket
//------------------------------------------------------------------------------
class Socket {
private:
  using udp = boost::asio::ip::udp;

  // Impl is shared_ptr because we use shared_from_this to maintain its
  // lifetime. It is however not to allow Socket to be copyable.
  std::shared_ptr<SocketImpl> _impl;

public:
  static const size_t packet_size = SocketImpl::packet_size;

  using OnReceive = SocketImpl::OnReceive;
  using OnFlush = SocketImpl::OnFlush;

public:
  Socket(boost::asio::io_service& ios)
    : _impl(std::make_shared<SocketImpl>(ios))
  {}

  Socket(udp::socket udp_socket)
    : _impl(std::make_shared<SocketImpl>(std::move(udp_socket)))
  {}

  Socket(Socket&& other)
    : _impl(std::move(other._impl)) {}

  Socket& operator = (Socket&& other) {
    _impl = std::move(other._impl);
    return *this;
  }

  Socket(const Socket&) = delete;
  Socket& operator = (const Socket&) = delete;

  ~Socket() {
    // _impl may have been moved from.
    if (_impl) _impl->close();
  }

  udp::endpoint local_endpoint() const {
    return _impl->local_endpoint();
  }

  boost::optional<udp::endpoint> remote_endpoint() const {
    return _impl->remote_endpoint();
  }

  template<class OnConnect>
  void rendezvous_connect(udp::endpoint remote_ep, OnConnect on_connect) {
    _impl->rendezvous_connect(std::move(remote_ep), std::move(on_connect));
  }

  void receive_unreliable(OnReceive _1) {
    _impl->receive_unreliable(std::move(_1));
  }

  void receive_reliable(OnReceive _1) {
    _impl->receive_reliable(std::move(_1));
  }

  template<class OnSend>
  void send_unreliable(std::vector<uint8_t> _1, OnSend&& on_send) {
    _impl->send_unreliable(std::move(_1), std::forward<OnSend>(on_send));
  }

  template<class OnSend>
  void send_reliable(std::vector<uint8_t> _1, OnSend&& on_send) {
    _impl->send_reliable(std::move(_1), std::forward<OnSend>(on_send));
  }

  void flush(OnFlush _1) {
    _impl->flush(std::move(_1));
  }

  void close() {
    _impl->close();
  }

  udp::socket& get_socket_impl() {
    return _impl->get_socket_impl();
  }

  async::alarm::duration recv_timeout_duration() const {
    return _impl->recv_timeout_duration();
  }

  boost::asio::io_service& get_io_service() {
    return _impl->get_io_service();
  }

  async::alarm::duration keepalive_period() const {
    return _impl->keepalive_period();
  }

  void keepalive_period(async::alarm::duration d) {
    _impl->keepalive_period(d);
  }

};

} // namespace

#endif // ifndef CLUB_SOCKET_H
