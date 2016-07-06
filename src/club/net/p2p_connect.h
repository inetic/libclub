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

#ifndef CLUB_NET_P2P_CONNECT_H
#define CLUB_NET_P2P_CONNECT_H

// The code implements algorithm as explained here:
// http://www.brynosaurus.com/pub/net/p2pnat/
//
// The article is called (in case the link is dead):
// "Peer-to-Peer Communication Across Network Address Translators"
//
// There is one more twist though, we are connected to the
// internet such that there is no NAT blocking incomming packets
// and the other peer is behind a Symmetric NAT, his address
// will be different from those passed to the function. The
// function will detect it and endpoint passed to the
// handler will be the correct one.

#include <boost/asio/ip/udp.hpp>

namespace club {

class ResenderSocket;

void p2p_connect
    ( ResenderSocket& socket
    , unsigned int timeout_ms
    , const boost::asio::ip::udp::endpoint& remote_private_endpoint
    , const boost::asio::ip::udp::endpoint& remote_public_endpoint
    , const std::function<void ( const boost::system::error_code&
                               // Which of the above private or public
                               // endpoints was used.
                               , const boost::asio::ip::udp::endpoint& requested
                               // If the other side is behind a Symmetric
                               // NAT and we're behind a Full-Cone NAT
                               // then the port may differe from the requested
                               // endpoint.
                               , const boost::asio::ip::udp::endpoint& used
                               )>& handler);

} // club namespace.

#endif // ifndef CLUB_NET_P2P_CONNECT_H
