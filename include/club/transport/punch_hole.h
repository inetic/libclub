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

#ifndef CLUB_TRANSPORT_PUNCH_HOLE_H
#define CLUB_TRANSPORT_PUNCH_HOLE_H

#include <club/generic/move_exec.h>
#include <boost/asio/ip/udp.hpp>

namespace club { namespace transport {

class PunchHole {
  using udp = boost::asio::ip::udp;
  using error_code = boost::system::error_code;
  using Handler = std::function<void( error_code
                                    , boost::asio::ip::udp::endpoint)>;

public:
  PunchHole( udp::socket&
           , const udp::endpoint& remote_endpoint
           , std::vector<uint8_t> syn_packet);

  template<class H>
  void start(H&& handler);

private:
  void start_sending(unsigned int);
  void start_receiving();
  void handle_common(error_code);

private:
  udp::socket& _socket;
  boost::asio::steady_timer _timer;
  udp::endpoint _remote_endpoint;
  std::vector<uint8_t> _syn_packet;
  udp::endpoint _rx_endpoint;
  std::vector<uint8_t> _rx_buffer;
  const unsigned int _retry_count = 10;
  Handler _on_punch;
  boost::optional<error_code> _result_error;
};

//--------------------------------------------------------------------
inline
PunchHole::PunchHole( udp::socket& socket
                    , const udp::endpoint& remote_endpoint
                    , std::vector<uint8_t> syn_packet)
  : _socket(socket)
  , _timer(socket.get_io_service())
  , _remote_endpoint(remote_endpoint)
  , _syn_packet(std::move(syn_packet))
  , _rx_buffer(2*_syn_packet.size())
{
}

//--------------------------------------------------------------------
template<class H>
inline
void PunchHole::start(H&& handler)
{
  _on_punch = std::move(handler);
  start_receiving();
  start_sending(0);
}

//--------------------------------------------------------------------
inline void PunchHole::handle_common(error_code error) {
  if (_result_error) {
    // We only have two async action that call this function and the
    // first one has already been executed, so we can finish now.
    return move_exec(_on_punch, *_result_error, _remote_endpoint);
  }

  _result_error = error;
  _timer.cancel();
  if (error && _socket.is_open()) _socket.close();
}

//--------------------------------------------------------------------
inline
void PunchHole::start_receiving()
{
  auto on_receive = [=](error_code error, size_t size) {
    if (error || _result_error) return handle_common(error);

    if (_rx_endpoint == _remote_endpoint) {
      return handle_common(error_code()); // success
    }

    if (_rx_endpoint.address() != _remote_endpoint.address()) {
      return start_receiving();
    }

    // He's behind a symmetric NAT and thus his port is different.
    // TODO: Unless it's an adversary! Would be nice if they could
    //       exchange some kind of PIN.
    _remote_endpoint = _rx_endpoint;
    return handle_common(error_code());
  };

  _socket.async_receive_from( boost::asio::buffer(_rx_buffer)
                            , _rx_endpoint
                            , std::move(on_receive));
}

//--------------------------------------------------------------------
inline
void PunchHole::start_sending(unsigned int attempt_number)
{
  auto on_send = [=](error_code error, size_t size) {
    if (error || _result_error) {
      return handle_common(error);
    }

    if (attempt_number == _retry_count) {
      return handle_common(boost::asio::error::host_unreachable);
    }

    _timer.expires_from_now(std::chrono::milliseconds(200));
    _timer.async_wait([=](error_code) { start_sending(attempt_number + 1); });
  };

  _socket.async_send_to( boost::asio::buffer(_syn_packet)
                       , _remote_endpoint
                       , std::move(on_send));
}

//--------------------------------------------------------------------
template<class Handler>
void punch_hole( boost::asio::ip::udp::socket& socket
               , boost::asio::ip::udp::endpoint& remote_endpoint
               , std::vector<uint8_t> syn_packet
               , Handler handler) {
  auto puncher = std::make_shared<PunchHole>( socket
                                            , remote_endpoint
                                            , std::move(syn_packet));

  // TODO: Handler can't be moved into std::function<...>.
  auto handler_ptr = std::make_shared<Handler>(std::move(handler));

  auto on_punch = [ handler_ptr = std::move(handler_ptr)
                  , puncher]( boost::system::error_code error
                            , boost::asio::ip::udp::endpoint remote_endpoint) {
    (*handler_ptr)(error, remote_endpoint);
  };

  puncher->start(std::move(on_punch));
}

}} // namespaces

#endif // ifndef CLUB_TRANSPORT_PUNCH_HOLE_H
