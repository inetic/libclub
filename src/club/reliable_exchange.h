#ifndef CLUB_RELIABLE_EXCHANGE_H
#define CLUB_RELIABLE_EXCHANGE_H

namespace club {

template<class Handler>
void reliable_exchange( std::vector<uint8_t> data
                      , club::Socket& socket
                      , Handler handler) {
  using boost::system::error_code;
  using club::Socket;

  struct State {
    Socket& socket;
    boost::asio::const_buffer buffer;
    boost::optional<error_code> first_error;

    State(Socket& socket) : socket(socket) {}
  };

  auto state = std::make_shared<State>(socket);

  state->socket.send_reliable(move(data), [=](auto error) {
      if (state->first_error) {
        handler(*state->first_error, state->buffer);
      }
      else {
        state->first_error = error;
        if (error) state->socket.close();
      }
    });

  state->socket.receive_reliable([=](auto error, auto buffer) {
      state->buffer = buffer;

      if (state->first_error) {
        handler(*state->first_error, state->buffer);
      }
      else {
        state->first_error = error;
        if (error) state->socket.close();
      }
    });
}


} // namespace

#endif // ifndef CLUB_RELIABLE_EXCHANGE_H
