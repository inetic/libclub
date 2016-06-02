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

#include "p2p_connect.h"
#include "PL/ResenderSocket.h"

using namespace net;
using namespace std;

using boost::optional;

namespace asio = boost::asio;

typedef PL::ResenderSocket Socket;
typedef Socket::endpoint_type endpoint_type;

typedef std::function<void( const boost::system::error_code&
                          , const endpoint_type&
                          , const endpoint_type&)> Handler;

#if 0
# define LOG(...) log(__VA_ARGS__)
#else
# define LOG(...) do {} while(0)
#endif

struct P2PConnect : enable_shared_from_this<P2PConnect> {
  using error_code = boost::system::error_code;

  optional<error_code> send_error;
  optional<error_code> recv_error;

  unsigned char        send_buffer;
  unsigned char        recv_buffer;

  endpoint_type        retry_endpoint;

  Socket&     socket;
  PL::Channel channel;

  P2PConnect(Socket& socket, PL::Channel channel)
    : socket(socket)
    , channel(channel)
  {}

  template<class Handler>
  void start( unsigned int   timeout_ms
            , bool           is_retry
            , endpoint_type  remote_endpoint
            , const Handler& handler) {

    send_error.reset();

    if (!is_retry) {
      recv_error.reset();
    }

    retry_endpoint = endpoint_type();

    auto self = shared_from_this();

    socket.async_send_to( remote_endpoint
                        , asio::buffer(&send_buffer, 1)
                        , channel
                        , timeout_ms
                        , [this, self, timeout_ms, handler, remote_endpoint]
                          (error_code error) {

                          if (retry_endpoint != endpoint_type()) {
                            return start(timeout_ms, true, retry_endpoint, handler);
                          }

                          if (recv_error) { // Receiving finished
                            if (*recv_error) { // With error
                              handler(remote_endpoint, *recv_error);
                            }
                            else {
                              handler(remote_endpoint, error);
                            }
                          }
                          else {
                            send_error = error;
                            if (error) {
                              socket.cancel_receiving(channel);
                            }
                          }
                        });

    if (is_retry) return;

    socket.async_receive_from( channel
                             , timeout_ms
                             , asio::buffer(&recv_buffer, 1)
                             , [this, self, remote_endpoint
                               , timeout_ms, handler]
                               ( endpoint_type endpoint
                               , error_code    error
                               , size_t) {

                             if (!error && endpoint != remote_endpoint) {
                               recv_error = error;

                               if (send_error) {
                                 start(timeout_ms, true, endpoint, handler);
                               }
                               else {
                                 socket.cancel_sending(channel);
                                 retry_endpoint = endpoint;
                               }
                               return;
                             }

                             if (send_error) {
                               if (*send_error) {
                                 handler(remote_endpoint, *send_error);
                               }
                               else {
                                 handler(remote_endpoint, error);
                               }
                             }
                             else {
                               recv_error = error;
                               if (error) {
                                 socket.close();
                               }
                             }
                             });
  }
};

template<class Handler /* void(error_code) */>
void p2p_connect_channel( Socket&              socket
                        , unsigned int         timeout_ms
                        , PL::Channel          channel
                        , const endpoint_type& remote_endpoint
                        , const Handler&       handler)
{
  auto obj = make_shared<P2PConnect>(socket, channel);
  obj->start( timeout_ms
            , false
            , remote_endpoint
            , handler);
}

void net::p2p_connect( PL::ResenderSocket&  socket
                     , unsigned int         timeout_ms
                     , const endpoint_type& remote_private_endpoint
                     , const endpoint_type& remote_public_endpoint
                     , const Handler&       handler) {

  using error_code = boost::system::error_code;

  bool has_public_ep = !remote_public_endpoint.address().is_unspecified()
                    && remote_public_endpoint.port() != 0;

  bool has_private_ep = !remote_private_endpoint.address().is_unspecified()
                    && remote_private_endpoint.port() != 0;

  if (!has_public_ep && !has_private_ep) {
    socket.get_io_service().post([=]() {
        handler( boost::asio::error::invalid_argument
               , endpoint_type()
               , endpoint_type());
        });
  }

  struct Status {
    optional<error_code> public_error;
    optional<error_code> private_error;

    endpoint_type public_result;
    endpoint_type private_result;
  };

  auto status = make_shared<Status>();

  // If the other peer is behind a Symmetric NAT and we're behind
  // a Full-Cone NAT, then the actual remote endpoint will be different
  // to the one we've requested. The difference between the two
  // can be used for port prediction by the upper layers.
  auto interm_handler = [handler]( error_code    error
                                 , endpoint_type requested_endpoint
                                 , endpoint_type connected_endpoint) {
    handler(error, requested_endpoint, connected_endpoint);
  };

  if (has_public_ep) {
    p2p_connect_channel( socket
                       , timeout_ms
                       , PL::CHANNEL_P2P_CONNECT_PUBLIC()
                       , remote_public_endpoint,
      [=, &socket](endpoint_type endpoint, error_code error) {
        auto& private_error = status->private_error;

        if (!has_private_ep) {
          return interm_handler( error
                               , remote_public_endpoint
                               , endpoint );
        }

        if (private_error) { // Connection to private ep ended first
          interm_handler( *private_error
                        , remote_private_endpoint
                        , status->private_result);
        }
        else {
          status->public_error  = error;
          status->public_result = endpoint;
          if (!error) {
            socket.cancel_sending  (PL::CHANNEL_P2P_CONNECT_PRIVATE());
            socket.cancel_receiving(PL::CHANNEL_P2P_CONNECT_PRIVATE());
          }
        }
      });
  }

  if (has_private_ep) {
    p2p_connect_channel( socket
                       , timeout_ms
                       , PL::CHANNEL_P2P_CONNECT_PRIVATE()
                       , remote_private_endpoint,
      [=, &socket](endpoint_type endpoint, error_code error) {
        auto& public_error = status->public_error;

        if (!has_public_ep) {
          return interm_handler( error
                               , remote_private_endpoint
                               , endpoint );
        }

        if (public_error) {
          // Connection to public ep ended first
          interm_handler( *public_error
                        , remote_public_endpoint
                        , status->public_result);
        }
        else {
          status->private_error  = error;
          status->private_result = endpoint;
          if (!error) {
            socket.cancel_sending  (PL::CHANNEL_P2P_CONNECT_PUBLIC());
            socket.cancel_receiving(PL::CHANNEL_P2P_CONNECT_PUBLIC());
          }
        }
      });
  }
}

////////////////////////////////////////////////////////////////////////////////

