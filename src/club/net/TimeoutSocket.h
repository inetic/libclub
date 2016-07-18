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

#ifndef CLUB_NET_TIMEOUT_SOCKET_H
#define CLUB_NET_TIMEOUT_SOCKET_H

#include <boost/optional.hpp>

#include "Buffer.h"

// Design decisions:
// * Should the socket get closed if no async read/write
//   action is assigned to it after a callback gets called?
//   => No, it would cause problems in this kind of situations:
//      s1 << recv << recv
//      s2 << send << wait << send
//
// * If socket is to be destroyed after io_service runs out
//   of work, explicit close needs to be called on the socket while
//   the service is still running. This is because if the last action
//   assigned to it timed out, we might still have a waiting job on
//   the socket (because cancelling is not portable).
//
// * If socket is to be destroyed before io_service runs out
//   of work, it will be closed implicitly in the destructor.

namespace club {

//////////////////////////////////////////////////////////////////////
// A socket that defines timeouts on async read/write operations.
//////////////////////////////////////////////////////////////////////

class TimeoutSocket {
private:
  typedef boost::asio::ip::udp                      udp;
  typedef boost::system::error_code                 error_code;
  typedef udp::endpoint                             endpoint_type;
  typedef boost::asio::ip::udp::socket              Delegate;
  typedef std::function<void(const error_code&)>    TXHandler;
  typedef std::function<void( const endpoint_type&
                            , const error_code&
                            , size_t) >             RXHandler;

  static error_code operation_aborted() {
    return boost::asio::error::operation_aborted;
  }

  class TimedReceiver {

    struct HandlerArgs {
      endpoint_type endpoint;
      error_code    error;
      size_t        size;
      HandlerArgs( const endpoint_type& endpoint
                 , const error_code&    error
                 , size_t               size)
        : endpoint(endpoint), error(error), size(size) {}
    };

    public:
    TimedReceiver( Delegate& socket, Channel debug_type)
      : _socket(socket)
      , _timer(new boost::asio::deadline_timer(socket.get_io_service()))
      , _debug_type(debug_type)
    {
    }

    ~TimedReceiver() {
    }

    template< class Handler
            , class MutableBufferSequence>
    void start_receiving( unsigned int                   timeout_ms
                        , const MutableBufferSequence&   buffers
                        , std::weak_ptr<TimedReceiver>   weak_self
                        , const Handler&                 handler )
    {
      ASSERT(!_handler);
      ASSERT(!_handler_args);

      _buffers.resize(std::distance(buffers.begin(), buffers.end()));
      std::copy(buffers.begin(), buffers.end(), _buffers.begin());

      _handler = handler;

      _timer->expires_from_now(boost::posix_time::milliseconds(timeout_ms));

      _timer->async_wait([this, weak_self](const error_code& e) {
        if (auto self = weak_self.lock()) {
          on_timeout(_timer, e);
        }
      });
    }

    template<class MutableBufferSequence>
    void receive( const endpoint_type&         endpoint
                , const Header&                header
                , const MutableBufferSequence& buffers
                , size_t                       size) {

      if (header.is_ack()) {
        // In case of acknowledgement, _last_acked_id represents
        // the expected id.
        if (_last_acked_id && header.id() != *_last_acked_id)
          return;
        if (!is_receiving())
          return;
      }
      else {
        if (header.needs_ack()) {

          if (_last_acked_id && header.id() <= *_last_acked_id) {
            // Already acked this one.
            send_ack(endpoint, header.channel(), header.id());
            return;
          }

          if (!is_receiving())
            return;

          send_ack(endpoint, header.channel(), header.id());
          _last_acked_id = header.id();
        }
        else {
          if (!is_receiving())
            return;
        }

        boost::asio::buffer_copy(_buffers, buffers, size);
      }

      _timer->cancel();
      // Don't overwrite args if receive had been cancelled.
      if (!_handler_args)
        _handler_args = HandlerArgs(endpoint, error_code(), size);
    }

    void cancel(const error_code& error) {
      ASSERT(_handler);
      if (_handler_args) {
        return;
      }
      _timer->cancel();
      _handler_args = HandlerArgs(endpoint_type(), error, 0);
    }

    bool is_active() const { return (bool) _handler; }
    bool is_receiving() const { return (bool) _handler && !_handler_args; }

    void reset_last_acked_id() {
      _last_acked_id.reset();
    }
    void reset_last_acked_id(uint32_t v) {
      _last_acked_id.reset(v);
    }
    boost::optional<uint32_t> get_last_acked_id() const {
      return _last_acked_id;
    }

    private:

    void on_timeout( const std::shared_ptr<boost::asio::deadline_timer>&
                   , const error_code& e) {
      using namespace boost::asio;

      if (_handler_args) {
        HandlerArgs a = *_handler_args;
        _handler_args.reset();
        fire_handler(a.endpoint, a.error, a.size);
        return;
      }

      fire_handler( endpoint_type()
                  , e ? e : error::timed_out
                  , 0);
    }

    void send_ack(const endpoint_type& endpoint, Channel type, uint32_t id) {
      auto bytes = std::make_shared<Header::bytes_type>
                     (Header(type, id, true, false).to_bytes());

      _socket.async_send_to( boost::asio::buffer(*bytes)
                           , endpoint
                           , [bytes](error_code, size_t){});
    }

    void fire_handler( const endpoint_type& endpoint
                     , const error_code&    error
                     , size_t               size )
    {
      ASSERT(_handler);
      RXHandler h = _handler;
      _handler = RXHandler();
      h(endpoint, error, size);
    }

    private:
    Delegate&                                    _socket;
    std::shared_ptr<boost::asio::deadline_timer> _timer;
    std::vector<boost::asio::mutable_buffer>     _buffers;
    RXHandler                                    _handler;
    boost::optional<uint32_t>                    _last_acked_id;

    boost::optional<HandlerArgs> _handler_args;

    public:
    Channel _debug_type;
  };

  typedef std::shared_ptr<TimedReceiver>           TimedReceiverPtr;
  typedef std::pair<Channel, bool /* is_ack */>    ReceiversKey;
  typedef std::map<ReceiversKey, TimedReceiverPtr> Receivers;

public:

  TimeoutSocket(boost::asio::io_service& io_service)
    : _delegate                (io_service)
    , _on_recv_loop_is_running (false)
    , _rx_data                 (1024)
    , _was_destroyed           (std::make_shared<bool>(false))
  {
    _rx_buffer.buffer(boost::asio::buffer(_rx_data));
  }

  TimeoutSocket(udp::socket&& socket)
    : _delegate                (std::move(socket))
    , _on_recv_loop_is_running (false)
    , _rx_data                 (1024)
    , _was_destroyed           (std::make_shared<bool>(false))
  {
    _rx_buffer.buffer(boost::asio::buffer(_rx_data));
  }

  TimeoutSocket( boost::asio::io_service& io_service
               , const udp::endpoint&     ep)
    : _delegate                (io_service, ep)
    , _on_recv_loop_is_running (false)
    , _rx_data                 (1024)
    , _was_destroyed           (std::make_shared<bool>(false))
  {
    _rx_buffer.buffer(boost::asio::buffer(_rx_data));
  }

  TimeoutSocket( boost::asio::io_service& io_service
               , unsigned short           port)
    : _delegate                (io_service, udp::endpoint(udp::v4(), port))
    , _on_recv_loop_is_running (false)
    , _rx_data                 (1024)
    , _was_destroyed           (std::make_shared<bool>(false))
  {
    _rx_buffer.buffer(boost::asio::buffer(_rx_data));
  }

  ~TimeoutSocket() {
    *_was_destroyed = true;
  }

  //////////////////////////////////////////////////////////////////////////////
  void close() { _delegate.close(); }

  void move_counters_from(TimeoutSocket& socket) {
    for (auto& pair : socket._receivers) {
      TimedReceiver& r = *pair.second;

      boost::optional<uint32_t> last_acked = r.get_last_acked_id();

      if (last_acked) {
        ReceiversKey key = pair.first;
        TimedReceiverPtr& p = _receivers[key];

        if (!p) {
          p.reset(new TimedReceiver(_delegate, key.first));
        }

        p->reset_last_acked_id(*last_acked);
        r.reset_last_acked_id();
      }
    }
  }

  // Does the same as the bind_remote function but aplies the filter
  // in this class, not using system functions. This is because I couldn't
  // figure out how to undo bind_remote yet. See this question:
  // http://stackoverflow.com/questions/12551402/p2p-using-boost-asio-udp-socket?rq=1
  void bind_remote(const endpoint_type& endpoint) {
    _remote_filter = endpoint;
  }

  void unbind_remote() {
    _remote_filter.reset();
    _receivers.clear();
  }

  endpoint_type remote_endpoint() const {
    if (_remote_filter) {
      return *_remote_filter;
    }
    using namespace boost;
    throw system::system_error(asio::error::not_connected);
  }

  bool loop_is_running() const { return _on_recv_loop_is_running; }
  size_t buffer_size() const   { return _rx_data.size(); }

  bool has_active_receiver(Channel channel) const {
    ReceiversKey key(channel, false);
    Receivers::const_iterator i = _receivers.find(key);
    if (i == _receivers.end()) return false;
    return i->second->is_active();
  }

  void reset_last_acked_id() {
    for (Receivers::iterator i = _receivers.begin()
        ; i != _receivers.end(); ++i ) {
      TimedReceiver& r = *i->second;
      r.reset_last_acked_id();
    }
  }

  boost::asio::io_service& get_io_service() { return _delegate.get_io_service(); }

  //////////////////////////////////////////////////////////////////////////////
  // Cancel timed receive.

  size_t cancel
      ( Channel channel
      , bool       is_ack
      , const error_code& error = operation_aborted())
  {
    Receivers::iterator i = _receivers.find(ReceiversKey(channel, is_ack));
    if (i == _receivers.end()) return 0;
    TimedReceiver& r = *i->second;
    if (!r.is_active()) return 0;
    r.cancel(error);
    return 1;
  }

  size_t cancel_sending( Channel channel
                       , const error_code& error = operation_aborted()) {
    return cancel(channel, true, error);
  }

  size_t cancel_receiving( Channel channel
                         , const error_code& error = operation_aborted()) {
    return cancel(channel, false, error);
  }

  size_t cancel_all(const error_code& error = operation_aborted())
  {
    size_t ret = 0;
    for (auto& rp : _receivers) {
      TimedReceiver& r = *rp.second;

      if (r.is_active()) {
        ++ret;
        r.cancel(error);
      }
    }
    return ret;
  }

  //////////////////////////////////////////////////////////////////////////////
  void debug(bool b) { _debug = b; }

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  template<class Handler, class ConstBufferSequence>
  void async_send( uint32_t                   packet_id
                 , Channel                 channel
                 , const ConstBufferSequence& buffer
                 , unsigned int               timeout_ms
                 , const Handler&             handler)
  {
    async_send_to( packet_id
                 , channel
                 , remote_endpoint()
                 , buffer
                 , timeout_ms
                 , handler);
  }

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const error_code&)
  template<class Handler, class ConstBufferSequence>
  void async_send_to( uint32_t                   packet_id
                    , Channel                    channel
                    , const udp::endpoint&       tx_endpoint
                    , const ConstBufferSequence& buffer
                    , unsigned int               timeout_ms
                    , const Handler&             handler)
  {
    bool needs_ack = timeout_ms != 0;

    _tx_buffer.set_header(Header(channel, packet_id, false, needs_ack));
    _tx_buffer.buffer(buffer);

    auto was_destroyed = _was_destroyed;

    if (needs_ack) {
      // Start receiving ACK.
      TimedReceiverPtr& p = _receivers[ReceiversKey(channel, true)];

      if (!p) {
        p.reset(new TimedReceiver(_delegate, channel));
      }

      p->start_receiving( timeout_ms
                        , boost::asio::null_buffers()
                        , p
                        , [this, handler]
                          ( const endpoint_type& /* endpoint */
                          , const error_code&    error
                          , size_t               /* size */)
                          {
                            handler(error);
                          }
                        );

      // In case of ACKs, the last_acket_id represents an expected
      // packet ID.
      p->reset_last_acked_id(packet_id);

      start_on_recv_loop("async_send");
    }

    _delegate.async_send_to( _tx_buffer, tx_endpoint
                           , [=](error_code error, size_t) {
      if (timeout_ms == 0) {
        // Not waiting for ack.
        if (error) {
          handler(error);
        }
        else {
          handler(boost::asio::error::timed_out);
        }
        return;
      }
      else {
        // Waiting for ack.
        if (!error && *was_destroyed) {
          error = boost::asio::error::broken_pipe;
        }

        if (error) {
          cancel(channel, true, error);
        }
      }
    });
  }

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const endpoint_type&, const error_code&, size_t)
  template<class Handler, class MutableBufferSequence>
  void async_receive_from( Channel                      channel
                         , unsigned int                 timeout_ms
                         , const MutableBufferSequence& buffer
                         , const Handler&               handler)
  {
    TimedReceiverPtr& p = _receivers[ReceiversKey(channel, false)];

    if (!p) {
      p.reset(new TimedReceiver(_delegate, channel));
    }

    p->start_receiving(timeout_ms, buffer, p, handler);

    start_on_recv_loop("async_receive_from");
  }

  //////////////////////////////////////////////////////////////////////////////
  // handler :: void (const endpoint_type&, const error_code&, size_t)
  template<class Handler, class MutableBufferSequence>
  void async_receive_from( unsigned int                 timeout_ms
                         , const MutableBufferSequence& buffer
                         , const Handler&               handler)
  {
    async_receive_from(CHANNEL_DAT(), timeout_ms, buffer, handler);
  }

  uintptr_t id() const { return reinterpret_cast<uintptr_t>(this); }

  void open () {
    using namespace boost::asio;
    _delegate.open(ip::udp(ip::udp::v4()));
  }

  const std::shared_ptr<bool>& was_destroyed_ptr() const {
    return _was_destroyed;
  }

  endpoint_type local_endpoint() const { return _delegate.local_endpoint(); }

  bool is_open() const { return _delegate.is_open(); }

private:

  //////////////////////////////////////////////////////////////////////////////
  void on_receive_loop( std::shared_ptr<bool> was_destroyed
                      , const  error_code&    error
                      , size_t                recv_size)
  {
    if (*was_destroyed) {
      return;
    }

    // TODO: Would be nice if we would increment the buffer size here
    //       and start waiting again (or even better, respond with
    //       some kind of eagain error).
    ASSERT(recv_size <= _rx_data.size() + sizeof(Header::bytes_type));

    _on_recv_loop_is_running = false;

    if (error == boost::asio::error::connection_refused) {
      start_on_recv_loop("on_receive_loop 0");
      return;
    }

    if (error) {
      cancel_all(error);
      return;
    }

    if (_remote_filter) {
      if (_rx_endpoint != *_remote_filter) {
        start_on_recv_loop("on_receive_loop 0.5");
        return;
      }
    }

    Header rx_header;
    _rx_buffer.get_header(rx_header);

    ReceiversKey key(rx_header.channel(), rx_header.is_ack());

    Receivers::iterator ri = _receivers.find(key);

    if (ri == _receivers.end()) {
      start_on_recv_loop("on_receive_loop 1");
      return;
    }

    TimedReceiver& r = *ri->second;

    r.receive(_rx_endpoint
             , rx_header
             , _rx_buffer.get_data_buffers()
             , recv_size - sizeof(Header::bytes_type));

    // TODO: This ASSERT is here to test whether the
    // below condition is still needed. Or the bigger
    // question: can a socket be destroyed inside a handler?
    // Should it be possible?
    ASSERT(!*was_destroyed);

    if (!*was_destroyed) {
      start_on_recv_loop("on_receive_loop 2");
      return;
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  void start_on_recv_loop(const char* /*debug_label = ""*/)
  {
    if (_on_recv_loop_is_running) {
      return;
    }

    // Don't do this: if the other end did not receive our ack, it will
    // resend the packet, but if we have stopped receiving due to this
    // condition, we will not resend the ack.
    //if (!receiving_receiver_count()) {
    //  return;
    //}

    _on_recv_loop_is_running = true;

    // Have to make local variable from this, so it can be captured by the
    // lambda below.
    auto was_destroyed = _was_destroyed;

    _delegate.async_receive_from
        ( _rx_buffer
        , _rx_endpoint
        , [this, was_destroyed](const error_code& error, size_t size) {
            on_receive_loop(was_destroyed, error, size);
          }
        );
  }

  //////////////////////////////////////////////////////////////////////////////
  size_t receiving_receiver_count() const {
    size_t ret = 0;
    for ( Receivers::const_iterator i = _receivers.begin()
        ; i != _receivers.end(); ++i )
    {
      if (i->second->is_receiving()) ++ret;
    }
    return ret;
  }

private:
  Delegate                       _delegate;
  bool                           _on_recv_loop_is_running;

  Receivers                      _receivers;

  ConstBuffer                    _tx_buffer;

  std::vector<char>              _rx_data;
  MutableBuffer                  _rx_buffer;
  udp::endpoint                  _rx_endpoint;
  boost::optional<udp::endpoint> _remote_filter;

  bool                           _debug;

protected:
  std::shared_ptr<bool>          _was_destroyed;
};

} // club namespace

#endif // ifndef CLUB_NET_TIMEOUT_SOCKET_H
