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
// A made up magic number, every service (chat, VoIP, particular game,...)
// should choose a unique number.
static const uint32_t CHAT_SERVICE_NUMBER = 9124016;

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
struct Options {
  uint16_t      local_port;
  udp::endpoint server_endpoint;
};

//------------------------------------------------------------------------------
struct Chat {
  asio::io_service&                   io_service;
  Options                             options;
  unique_ptr<club::hub>               hub;
  std::set<club::uuid>                members;
  std::unique_ptr<rendezvous::client> rendezvous_client;
  std::unique_ptr<ConnectedSocket>    socket_ptr;

  Chat(asio::io_service& ios, const Options& options)
    : io_service(ios)
    , options(options)
    , hub(make_unique<club::hub>(ios))
  {
    init_callbacks();
    start_fetching_peers();
  }

  void init_callbacks() {
    hub->on_receive.connect([](club::hub::node node, const vector<char>& data) {
        cout << node.id() << ": " << string(data.begin(), data.end()) << endl;
      });

    hub->on_insert.connect([this](std::set<club::hub::node> nodes) {
        for (auto node : nodes) {
          members.insert(node.id());
          cout << node.id() << " joined the club" << endl;
        }
      });

    hub->on_remove.connect([=](std::set<club::hub::node> nodes) {
        bool lost_leader = false;
        for (auto node : nodes) {
          cout << node.id() << " left" << endl;

          members.erase(node.id());

          if (is_leader(node.id())) lost_leader = true;
        }
        if (lost_leader && is_leader(hub->id())) {
          start_fetching_peers();
        }
      });
  }

  void start_fetching_peers() {
    if (rendezvous_client) return;
    if (!is_leader(hub->id())) return;

    cout << "start fetching peers" << endl;

    udp::socket socket = create_socket( hub->get_io_service()
                                      , options.local_port);

    rendezvous_client = make_unique<rendezvous::client>
      ( CHAT_SERVICE_NUMBER
      , move(socket)
      , options.server_endpoint
      , [=](Error error, udp::socket socket, udp::endpoint remote_ep) {
        rendezvous_client.reset();

        if (error) {
          if (error != boost::asio::error::operation_aborted) {
            cout << "Rendezvous error: " << error.message() << endl;
          }
          return stop();
        }

        socket_ptr.reset(new ConnectedSocket(move(socket)));

        socket_ptr->async_p2p_connect
          ( 5000 // Timeout in milliseconds
          , udp::endpoint()
          , remote_ep
          , [&](Error error, udp::endpoint, udp::endpoint) {
            if (error) {
              if (error != asio::error::operation_aborted) {
                cout << "Connect error: " << error.message() << endl;
                return start_fetching_peers();
              }
              return stop();
            }

            hub->fuse( move(*socket_ptr)
                     , [&](Error error, club::uuid id) {
                         if (error) {
                           cout << "Fuse error: " << error.message() << endl;
                           return stop();
                         }
                         members.insert(id);
                         start_fetching_peers();
                       });
            });
        });
  }

  void stop() {
    hub.reset();
    rendezvous_client.reset();
    socket_ptr.reset();
  }

  bool is_leader(club::uuid id) {
    if (members.empty()) return true;
    return *members.rbegin() <= id;
  }
};

//------------------------------------------------------------------------------
void start_reading_input(Chat& chat) {
  std::thread([&chat]() {
      for (string line; std::getline(std::cin, line);) {
        chat.io_service.post([&chat, line]() {
            if (auto& hub = chat.hub) {
              hub->total_order_broadcast(vector<char>(line.begin(), line.end()));
            }
          });
      }
    }).detach();
}

//------------------------------------------------------------------------------
int main(int argc, const char* argv[]) {
  // Parse options
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

  udp::endpoint rendezvous_server_ep;

  asio::io_service ios;

  try {
    rendezvous_server_ep = resolve(ios, vm["rendezvous"].as<string>());
  }
  catch(const std::exception& e) {
    cout << "Error: " << e.what() << endl;
    return 2;
  }

  // Start the chat service.
  Options options{local_port, rendezvous_server_ep};

  Chat chat(ios, options);

  start_reading_input(chat);

  // Capture these signals so that we can disconnect gracefuly.
  boost::asio::signal_set signals(ios, SIGINT, SIGTERM);

  signals.async_wait([&](Error error, int signal_number) {
      chat.stop();
    });

  ios.run();
}

//------------------------------------------------------------------------------

