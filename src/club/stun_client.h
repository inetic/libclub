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

#ifndef CLUB_STUN_CLIENT_H
#define CLUB_STUN_CLIENT_H

#include <map>
#include <random>
#include <mutex>
#include <boost/asio/ip/udp.hpp>

// Implementation of the STUN client according to RFC5389
// https://tools.ietf.org/html/rfc5389

namespace club {

class StunClient {
  struct State;
  struct Request;
  using udp = boost::asio::ip::udp;
  using Error = boost::system::error_code;
  using Endpoint = udp::endpoint;
  using Handler = std::function<void(Error, Endpoint)>;
  using StatePtr = std::shared_ptr<State>;
  using RequestPtr = std::shared_ptr<Request>;
  using RequestID = std::array<char, 12>;
  using Bytes = std::vector<uint8_t>;
  using Requests = std::map<Endpoint, RequestPtr>;

public:
  StunClient(udp::socket&);
  void reflect(Endpoint server_endpoint, Handler);
  ~StunClient();

private:
  void start_sending(RequestPtr);
  void start_receiving(StatePtr);
  void on_send(Error, RequestPtr);
  void on_recv(Error, size_t, StatePtr);
  void execute(Requests::iterator, Error, Endpoint e = Endpoint());

private:
  std::mt19937 _rand;
  udp::socket& _socket;
  StatePtr     _state;
  size_t       _request_count;
};

} // club namespace

#endif // ifndef CLUB_STUN_CLIENT_H
