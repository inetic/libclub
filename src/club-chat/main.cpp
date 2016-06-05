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

#include <iostream>
#include <string>
#include <thread>
#include <boost/program_options.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/signal_set.hpp>
#include <net/PL/ConnectedSocket.h>
#include <rendezvous/client.h>
#include <club/hub.h>

using std::move;
using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::make_shared;
using std::unique_ptr;
using std::make_unique;
using udp = boost::asio::ip::udp;
using Error = boost::system::error_code;
using net::PL::ConnectedSocket;

namespace po = boost::program_options;
namespace ip = boost::asio::ip;
namespace asio = boost::asio;

//------------------------------------------------------------------------------
struct Chat {
  std::set<club::uuid> members;
  std::unique_ptr<rendezvous::client> rendezvous_client;
  std::unique_ptr<ConnectedSocket> socket_ptr;
} chat;

//------------------------------------------------------------------------------
static const uint32_t CHAT_SERVICE_NUMBER = 9124016;

//------------------------------------------------------------------------------
static udp::endpoint resolve(asio::io_service& ios, const string& str) {
  string addr;
  string port;

  auto colon_pos = str.find(':');
  if (colon_pos == string::npos) {
    addr = str;
    port = "6378";
  }
  else {
    addr = string(str.begin(), str.begin() + colon_pos);
    port = string(str.begin() + colon_pos + 1, str.end());
  }

  udp::resolver resolver(ios);
  udp::resolver::query query(addr, port);
  udp::resolver::iterator iter = resolver.resolve(query);

  if (iter == udp::resolver::iterator()) {
    return udp::endpoint();
  }

  return *iter;
}

//------------------------------------------------------------------------------

void start_reading_input(club::hub& hub) {
  std::thread([&hub]() {
      for (string line; std::getline(std::cin, line);) {
        hub.get_io_service().post([&hub, line]() {
            hub.total_order_broadcast(vector<char>(line.begin(), line.end()));
            });
      }
      }).detach();
}

//------------------------------------------------------------------------------
bool is_max_id(club::uuid id, const std::set<club::uuid>& members) {
  for (auto& n_id : members) {
    if (id < n_id) {
      return false;
    }
  }
  return true;
}

//------------------------------------------------------------------------------
udp::socket create_socket(asio::io_service& ios, uint16_t preferred_port) {
  try {
    return udp::socket(ios, udp::endpoint(udp::v4(), preferred_port));
  }
  catch(...) {
    return udp::socket(ios, udp::endpoint(udp::v4(), 0));
  }
}

//------------------------------------------------------------------------------

void start_fetching_peers( unique_ptr<club::hub>& hub
                         , uint16_t local_port
                         , udp::endpoint server_ep) {
  if (chat.rendezvous_client) return;
  if (!is_max_id(hub->id(), chat.members)) return;

  cout << "start fetching peers" << endl;

  udp::socket socket = create_socket(hub->get_io_service(), local_port);

  chat.rendezvous_client = make_unique<rendezvous::client>
    ( CHAT_SERVICE_NUMBER // Service number
    , move(socket)
    , server_ep
    , [&, server_ep, local_port]( Error error
                                , udp::socket socket
                                , udp::endpoint remote_ep) {
      chat.rendezvous_client.reset();

      if (error) {
        if (error != boost::asio::error::operation_aborted) {
          cout << "Rendezvous error: " << error.message() << endl;
        }
        return hub.reset();
      }

      chat.socket_ptr.reset(new ConnectedSocket(move(socket)));

      chat.socket_ptr->async_p2p_connect
        ( 5000
        , udp::endpoint()
        , remote_ep
        , [&, server_ep, local_port]( Error error
                                    , udp::endpoint
                                    , udp::endpoint) {
          if (error) {
            cout << "Connect error: " << error.message() << endl;
            return hub.reset();
          }

          hub->fuse( move(*chat.socket_ptr)
                   , [&hub, server_ep, local_port](Error error, club::uuid id) {
              if (error) {
                cout << "Fuse error: " << error.message() << endl;
                return hub.reset();
              }
              chat.members.insert(id);
              start_fetching_peers(hub, local_port, server_ep);
              });
          });
      });
}

//------------------------------------------------------------------------------
int main(int argc, const char* argv[]) {
  po::options_description desc("Options");

  desc.add_options()
    ("help,h", "output this help")
    ("port,p", po::value<uint16_t>(), "port of the local socket")
    ("rendezvous,r", po::value<string>(), "endpoint to the rendezvous server");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    cout << desc << endl;
    return 0;
  }

  if (!vm.count("rendezvous")) {
    cout << "The 'rendezvous' option must be set" << endl;
    return 1;
  }

  uint16_t local_port = 0;

  if (vm.count("port")) {
    local_port = vm["port"].as<uint16_t>();
  }

  asio::io_service ios;

  auto rendezvous_server_ep = resolve(ios, vm["rendezvous"].as<string>());

  auto hub = make_unique<club::hub>(ios);

  hub->on_receive.connect([](club::hub::node node, const std::vector<char>& data) {
      cout << node.id() << ": " << string(data.begin(), data.end()) << endl;
    });

  hub->on_insert.connect([](std::set<club::hub::node> nodes) {
      for (auto node : nodes) {
        chat.members.insert(node.id());
        cout << node.id() << " joined the club" << endl;
      }
    });

  hub->on_remove.connect([=, &hub](std::set<club::hub::node> nodes) {
      bool lost_max_id = false;
      for (auto node : nodes) {
        chat.members.erase(node.id());
        if (is_max_id(node.id(), chat.members)) {
          lost_max_id = true;
        }
      }
      if (lost_max_id && is_max_id(hub->id(), chat.members)) {
        start_fetching_peers(hub, local_port, rendezvous_server_ep);
      }
    });

  start_reading_input(*hub);

  start_fetching_peers(hub, local_port, rendezvous_server_ep);

  boost::asio::signal_set signals(ios, SIGINT, SIGTERM);

  signals.async_wait([&hub](Error error, int signal_number) {
      hub.reset();
      chat.rendezvous_client.reset();
      chat.socket_ptr.reset();
    });

  ios.run();
}

//------------------------------------------------------------------------------

