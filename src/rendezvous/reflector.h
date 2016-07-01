#ifndef RENDEZVOUS_V1_REFLECTOR_H
#define RENDEZVOUS_V1_REFLECTOR_H

#include <binary/serialize/ip.h>
#include "rendezvous/constants.h"
#include "v1/header.h"

namespace rendezvous {

class Reflector {
  using udp = boost::asio::ip::udp;

  struct State {
    bool                  was_destroyed;
    udp::endpoint         remote_endpoint;
    std::vector<uint8_t>  rx_buffer;
    std::vector<uint8_t>  tx_buffer;
  };

public:
  Reflector(boost::asio::io_service&);

  uint16_t get_port() const;

  ~Reflector();
private:
  void start_receiving();
  void start_sending();

private:
  udp::socket            _socket;
  std::shared_ptr<State> _state;
};

//------------------------------------------------------------------------------
Reflector::Reflector(boost::asio::io_service& ios)
  : _socket(ios, udp::endpoint(udp::v4(), 0 /* random port */))
  , _state(std::make_shared<State>(State{ false
                                        , udp::endpoint()
                                        , std::vector<uint8_t>(512)
                                        , std::vector<uint8_t>(512) }))
{
  start_receiving();
}

uint16_t Reflector::get_port() const {
  return _socket.local_endpoint().port();
}

void Reflector::start_receiving() {
  auto state = _state;

  _socket.async_receive_from
      ( boost::asio::buffer(state->rx_buffer)
      , state->remote_endpoint
      , [this, state]
        (boost::system::error_code err, size_t size)
        {
          if (state->was_destroyed) return;

          if (err) {
            if (err == boost::asio::error::operation_aborted) {
              return;
            }
            return start_receiving();
          }
          
          start_sending();
        });
                           
}

void Reflector::start_sending() {
  auto state = _state;

  binary::encoder e(state->tx_buffer.data(), state->tx_buffer.size());

  uint16_t payload_size = 1                // method
                        + 1                // reserved
                        + sizeof(uint16_t) // port
                        ;

  state->tx_buffer.resize(HEADER_SIZE + payload_size);

  write_header(e, payload_size);

  e.put(METHOD_REFLECTED);
  e.put((uint8_t) 0 /* reserved */);
  e.put(state->remote_endpoint.port());

  assert(!e.error());

  _socket.async_send_to
      ( boost::asio::buffer(state->tx_buffer)
      , state->remote_endpoint
      , [this, state] (boost::system::error_code err, size_t size) {
          if (state->was_destroyed) return;
          if (err == boost::asio::error::operation_aborted) {
            return;
          }
          return start_receiving();
        });
}

Reflector::~Reflector() {
  _state->was_destroyed = true;
}

} // rendezvous namespace

#endif // ifndef RENDEZVOUS_V1_REFLECTOR_H
