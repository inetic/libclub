#ifndef CLUB_TRANSPORT_TRANSPORT_H
#define CLUB_TRANSPORT_TRANSPORT_H

#include <iostream>
#include <boost/asio/steady_timer.hpp>
#include <transport/transmit_queue.h>
#include <transport/inbound_messages.h>
#include <transport/message_reader.h>

namespace club { namespace transport {

template<typename UnreliableId>
class Transport {
private:
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
      , rx_buffer(65536)
      , tx_buffer(65536)
    {}
  };

public:
  using TransmitQueue    = ::club::transport::TransmitQueue<UnreliableId>;
  using OutboundMessages = ::club::transport::OutboundMessages<UnreliableId>;
  using InboundMessages  = ::club::transport::InboundMessages<UnreliableId>;
  using Message = typename TransmitQueue::Message;

public:
  Transport( uuid                              id
           , udp::socket                       socket
           , udp::endpoint                     remote_endpoint
           , std::shared_ptr<OutboundMessages> outbound
           , std::shared_ptr<InboundMessages>  inbound);

  void add_target(const uuid&);

  ~Transport();

private:
  friend class ::club::transport::OutboundMessages<UnreliableId>;

  void insert_message(std::shared_ptr<Message> m);

  void start_receiving(std::shared_ptr<SocketState>);

  void on_receive( boost::system::error_code
                 , std::size_t
                 , std::shared_ptr<SocketState>);

  void start_sending(std::shared_ptr<SocketState>);

  void on_send( const boost::system::error_code&
              , std::shared_ptr<SocketState>);

private:
  uuid                             _id;
  bool                             _is_sending;
  udp::socket                      _socket;
  udp::endpoint                    _remote_endpoint;
  TransmitQueue                    _transmit_queue;
  std::shared_ptr<InboundMessages> _inbound;
  MessageReader                    _message_reader;
  boost::asio::steady_timer        _timer;
  std::shared_ptr<SocketState>     _socket_state;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<typename UnreliableId>
Transport<UnreliableId>
::Transport( uuid                              id
           , udp::socket                       socket
           , udp::endpoint                     remote_endpoint
           , std::shared_ptr<OutboundMessages> outbound
           , std::shared_ptr<InboundMessages>  inbound)
  : _id(std::move(id))
  , _is_sending(false)
  , _socket(std::move(socket))
  , _remote_endpoint(std::move(remote_endpoint))
  , _transmit_queue(std::move(outbound))
  , _inbound(std::move(inbound))
  , _timer(_socket.get_io_service())
  , _socket_state(std::make_shared<SocketState>())
{
  _inbound->register_transport(this);
  _transmit_queue.outbound_messages().register_transport(this);
  start_receiving(_socket_state);
}

//------------------------------------------------------------------------------
template<typename UnreliableId>
Transport<UnreliableId>::~Transport() {
  _inbound->deregister_transport(this);
  _transmit_queue.outbound_messages().deregister_transport(this);
  _socket_state->was_destroyed = true;
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::add_target(const uuid& id)
{
  _transmit_queue.add_target(id);
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
    return _inbound->on_receive(error, asio::const_buffer(0, 0));
  }

  // Ignore packets from unknown sources.
  if (!_remote_endpoint.address().is_unspecified()) {
    if (state->rx_endpoint != _remote_endpoint) {
      return start_receiving(move(state));
    }
  }

  _message_reader.set_data(state->rx_buffer.data(), size);

  while (_message_reader.read_one()) {
    _inbound->on_receive(error, _message_reader.message_data());

    if (state->was_destroyed) {
      return;
    }
  }

  start_receiving(move(state));
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::start_sending(std::shared_ptr<SocketState> state) {
  using boost::system::error_code;
  using std::move;

  binary::encoder encoder(state->tx_buffer.data(), state->tx_buffer.size());

  auto count = _transmit_queue.encode_few(encoder);

  if (count == 0) {
    _is_sending = false;
    return;
  }

  auto s = state.get();

  _socket.async_send_to( boost::asio::buffer( s->tx_buffer.data()
                                            , encoder.written())
                       , _remote_endpoint
                       , [this, state = move(state)]
                         (const error_code& error, std::size_t) {
                           on_send(error, move(state));
                         });
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::on_send( const boost::system::error_code& error
                           , std::shared_ptr<SocketState>     state)
{
  using std::move;
  using boost::system::error_code;

  if (state->was_destroyed) return;

  if (error) {
    if (error == boost::asio::error::operation_aborted) {
      return;
    }
    assert(0);
  }

  _timer.expires_from_now(std::chrono::milliseconds(100));
  _timer.async_wait([this, state = move(state)]
                    (const error_code error) {
                      if (state->was_destroyed) return;
                      if (error) return;
                      start_sending(move(state));
                    });
}

//------------------------------------------------------------------------------
template<class Id>
void Transport<Id>::insert_message(std::shared_ptr<Message> m) {
  _transmit_queue.insert_message(std::move(m));

  if (!_is_sending) {
    _is_sending = true;
    start_sending(_socket_state);
  }
}

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_TRANSPORT_H