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
#include <club/socket.h>
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
using club::Socket;

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
  // When testing, it may be useful to set local_port to a precise value,
  // but in normal circumstances it is OK to set this to 0 and the system will
  // pick a random and unused port.
  uint16_t      local_port;

  // Endpoint of the rendezvous server, it is the server we contact in order
  // to learn about IP endpoints of other chatters.
  udp::endpoint server_endpoint;
};

//------------------------------------------------------------------------------
struct Chat {
  asio::io_service&                   io_service;
  Options                             options;
  unique_ptr<club::hub>               hub;
  std::set<club::uuid>                members;
  std::unique_ptr<rendezvous::client> rendezvous_client;
  std::unique_ptr<Socket>             socket_ptr;

  Chat(asio::io_service& ios, const Options& options)
    : io_service(ios)
    , options(options)
    , hub(make_unique<club::hub>(ios))
  {
    init_callbacks();

    // Contact the rendezvous server to find other chatters.
    start_fetching_peers();
  }

  void init_callbacks() {
    // Every time one of the nodes already present in Club sends data using the
    // `total_order_broadcast` function, it shall be received here (even by the
    // sender). Additionally, even if multiple nodes sent data concurrently,
    // Club makes sure every node receives the data in the same order.
    hub->on_receive([](club::hub::node node, const vector<char>& data) {
        cout << node.id() << ": " << string(data.begin(), data.end()) << endl;
      });

    // This is called when nodes are added to the network. Every node shall see
    // the same sequence on inserts.
    hub->on_insert([this](std::set<club::hub::node> nodes) {
        for (auto node : nodes) {
          members.insert(node.id());
          cout << node.id() << " joined the club" << endl;
        }
      });

    // When nodes are removed from the network. Again, each node (in a remaining
    // connected component) shall see the same sequence of removals.
    hub->on_remove([=](std::set<club::hub::node> nodes) {
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

    // We let only one node of the already established network to request for
    // new nodes. Note that it wouldn't be a problem if multiple nodes tried to
    // expand the network at once, but in the context of this application it
    // would be wasteful.
    if (!is_leader(hub->id())) return;

    cout << "start fetching peers" << endl;

    // Try to create an UDP socket and bind it to port `options.local_port`. If
    // it fails (e.g. if the port is already bound to another socket), it will
    // bind to a random port.
    udp::socket socket = create_socket( hub->get_io_service()
                                      , options.local_port);

    // Contact the rendezvous server and stay connected until another chatter
    // also contacts it. Once the server knows of two nodes, it sends UDP
    // endpoints of one to the other. Once `rendezvous::client` receives such
    // endpoint, it calls the handler with it.
    rendezvous_client = make_unique<rendezvous::client>
      ( CHAT_SERVICE_NUMBER
      , move(socket)
      , options.server_endpoint
      , false // connect as non-host
      , [=](Error error, udp::socket socket, udp::endpoint remote_ep) {
        rendezvous_client.reset();

        if (error) {
          if (error != boost::asio::error::operation_aborted) {
            cout << "Rendezvous error: " << error.message() << endl;
          }
          return stop();
        }

        socket_ptr.reset(new Socket(move(socket)));

        // Now we know the endpoint of the other chatter, we try to connect to
        // it directly (rendezvous). This takes care of the NAT hole punching
        // as well.
        socket_ptr->rendezvous_connect(remote_ep, [&](Error error) {
            if (error) {
              if (error != asio::error::operation_aborted) {
                cout << "Connect error: " << error.message() << endl;
                return start_fetching_peers();
              }
              return stop();
            }

            // We're directly connected to the new node, now we tell Club about
            // our new connection.
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
// Spawn a new thread and start reading user input, each input line shall
// be broadcast through Club reliably and totally ordered.
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
  // Parse command line options
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

  string rendezvous_server_addr = "asyncwired.com";

  if (!vm.count("rendezvous")) {
    cout << "The 'rendezvous' option was not set. Using "
         << rendezvous_server_addr << " to find chat-club members."
         << endl;
  }
  else {
    rendezvous_server_addr = vm["rendezvous"].as<string>();
  }

  uint16_t local_port = 0;

  if (vm.count("port")) {
    local_port = vm["port"].as<uint16_t>();
  }

  udp::endpoint rendezvous_server_ep;

  asio::io_service ios;

  try {
    rendezvous_server_ep = resolve(ios, rendezvous_server_addr);
  }
  catch(const std::exception& e) {
    cout << "Error: " << e.what() << endl;
    return 2;
  }

  // Start the chat service.
  Chat chat(ios, Options{local_port, rendezvous_server_ep});

  start_reading_input(chat);

  // Capture these signals so that we can disconnect gracefully.
  boost::asio::signal_set signals(ios, SIGINT, SIGTERM);

  signals.async_wait([&](Error error, int signal_number) {
      chat.stop();
    });

  ios.run();
}

//------------------------------------------------------------------------------

