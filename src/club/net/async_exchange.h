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

#pragma once

#include <boost/optional.hpp>
#include "any_size.h"

// Send and receive concurrently, call handler when
// both operations finish.

namespace club {

template< class Socket
        , class Handler /* void ( const error_code& error
                                , std::vector<char>&& rx_buffer) */
        , class TxBuffers>
void async_exchange( Socket&          socket
                   , const TxBuffers& tx_buffers
                   , unsigned int     timeout_ms
                   , const Handler&   handler)
{
  using namespace std;
  using namespace boost;
  using boost::system::error_code;

  auto rx_error  = make_shared<optional<error_code>>();
  auto tx_error  = make_shared<optional<error_code>>();
  auto rx_buffer = make_shared<vector<char>>();

  send_any_size( socket
               , tx_buffers
               , timeout_ms
               , [=, &socket](const error_code& error) {
                 *tx_error = error;
                 if (*rx_error /* is_set */) {
                   if (**rx_error) {
                     handler(**rx_error, std::move(*rx_buffer));
                   }
                   else {
                     handler(**tx_error, std::move(*rx_buffer));
                   }
                 }
                 else if (**tx_error) {
                   socket.close();
                 }
               });


  recv_any_size( socket
               , *rx_buffer
               , timeout_ms
               , [=, &socket](const error_code& error) {
                 *rx_error = error;
                 if (*tx_error /* is_set */) {
                   if (**tx_error) {
                     handler(**tx_error, std::move(*rx_buffer));
                   }
                   else {
                     handler(**rx_error, std::move(*rx_buffer));
                   }
                 }
                 else if (**rx_error) {
                   socket.close();
                 }
               });
}

} // club namespace
