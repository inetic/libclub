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

// This is really an app to test congestion control of the
// club::transport::Socket.

#include <iostream>
#include <boost/asio.hpp>
#include <club/socket.h>
#include <boost/program_options.hpp>

using udp = boost::asio::ip::udp;
namespace asio = boost::asio;
namespace po = boost::program_options;
using std::move;
using std::vector;
using std::cout;
using std::endl;
using std::string;
using ClubSocket = club::Socket;
using boost::system::error_code;
using std::shared_ptr;
using std::unique_ptr;

static constexpr uint16_t DEFAULT_SERVER_PORT = 6379;
static constexpr int NUMBER_OF_ROUNDS = 10;

//------------------------------------------------------------------------------
static udp::endpoint resolve(asio::io_service& ios, const string& str) {
  string addr;
  string port;

  auto colon_pos = str.find(':');
  if (colon_pos == string::npos) {
    addr = str;
    port = std::to_string(DEFAULT_SERVER_PORT);
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
struct Stopable {
  virtual void stop() = 0;
  virtual ~Stopable() {}
};

//------------------------------------------------------------------------------
struct Server : public Stopable {
  asio::io_service& ios;
  vector<uint8_t> contact_data;
  udp::endpoint remote_endpoint;
  udp::socket listening_socket;
  std::shared_ptr<ClubSocket> socket;

  Server(asio::io_service& ios)
    : ios(ios)
    , listening_socket(ios, udp::endpoint(udp::v4(), DEFAULT_SERVER_PORT))
  {
    listening_socket.async_receive_from( asio::buffer(contact_data)
                                       , remote_endpoint
                                       , [=](auto error, auto size) {
                                           this->on_contact_received(error, size);
                                       });
  }

  void on_contact_received(error_code error, size_t size) {
    if (error) {
      cout << "Error receiving contact data " << error.message() << endl;
      return stop();
    }

    cout << "Contact received " << remote_endpoint << endl;
    socket = std::make_shared<ClubSocket>(std::move(listening_socket));
    socket->rendezvous_connect(remote_endpoint, [=](auto error) {
        if (error) {
          cout << "Error connecting to " << remote_endpoint << " " << error.message() << endl;
          return this->stop();
        }
        cout << "Connected rendezvous" << endl;
        this->on_connect();
      });
  }

  void on_connect() {
    start_round(0);
  }

  void start_round(int round_number) {
    cout << "-------------- round " << round_number << " ---------------" << endl;
    using clock = std::chrono::steady_clock;

    size_t size = 65535;
    vector<uint8_t> data(size);

    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = std::rand();
    }

    cout << "Sending reliable" << endl;
    socket->send_reliable(move(data), [=](auto error) {
        });

    auto start = clock::now();

    socket->receive_reliable([=](auto error, auto buffer) {
        if (error) {
          cout << "Error receiving recv ack: " << error.message() << endl;
          return this->stop();
        }
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(clock::now() - start).count();
        cout << "Time elapsed: " << ms << "ms" << endl;
        cout << "Average speed: " << (size / (float(ms) / 1000)) << "Bps" << endl;

        if (round_number < NUMBER_OF_ROUNDS) {
          this->start_round(round_number+1);
        }
        else {
          return this->stop();
        }
      });
  }

  void stop() override {
    if (listening_socket.is_open()) listening_socket.close();
    if (socket) socket->close();
    socket.reset();
  }
};

//------------------------------------------------------------------------------
struct Client : public Stopable {
  udp::socket udp_socket;
  shared_ptr<ClubSocket> socket;
  udp::endpoint server_ep;

  Client(asio::io_service& ios, udp::endpoint server_ep)
    : udp_socket(ios, udp::endpoint(udp::v4(), 0))
    , server_ep(server_ep)
  {
    static vector<uint8_t> dummy_data({0,1,2,3});
    udp_socket.async_send_to( asio::buffer(dummy_data)
                            , server_ep
                            , [=](auto error, size_t size) {
                              if (error) {
                                cout << "Error sending contact to " << server_ep
                                     << ": " << error.message() << endl;
                                return this->stop();
                              }
                              cout << "Contact sent" << endl;
                              this->on_contact_sent();
                            });
  } 

  void on_contact_sent() {
    socket = std::make_shared<ClubSocket>(std::move(udp_socket));

    socket->rendezvous_connect(server_ep, [=](auto error) {
        if (error) {
          cout << "Error rendezvous connect " << error.message() << endl;
          return this->stop();
        }
        this->on_connect();
      });
  }

  void on_connect() {
    cout << "Rendezvous connect success to " << server_ep << endl;
    start_round(0);
  }

  void start_round(int round_number) {
    cout << "-------------- round " << round_number << " ---------------" << endl;
    cout << "Receiving" << endl;
    socket->receive_reliable([=](auto error, auto buffer) {
        if (error) {
          cout << "Error receiving data from server: " << error.message() << endl;
          return this->stop();
        }
        cout << "Success receiving " << asio::buffer_size(buffer) << " bytes of data " << " " << error.message() << endl;

        socket->send_reliable({1}, [=](auto error) {
            if (error) {
              cout << "Error sending ack: " << error.message() << endl;
              return this->stop();
            }
            socket->flush([=]() {
                if (round_number < NUMBER_OF_ROUNDS) {
                  this->start_round(round_number+1);
                }
                else {
                  this->stop();
                }
              });
          });

      });
  }

  void stop() override {
    std::cout << "!!!!!!!!!!! stop" << std::endl;
    if (udp_socket.is_open()) udp_socket.close();
    if (socket) socket->close();
    socket.reset();
  }
};

//------------------------------------------------------------------------------
int main(int argc, const char* argv[]) {
  // Parse command line options
  po::options_description desc("Options");

  desc.add_options()
    ("help,h", "output this help")
    ("connect,c", po::value<string>(), "endpoint of the server (if not set, we're the server)");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    cout << desc << endl;
    return 0;
  }

  boost::asio::io_service ios;

  std::function<void()> stop;

  unique_ptr<Stopable> stopable;

  if (!vm.count("connect")) {
    cout << "The 'connect' option was not set. We're the server then."
         << endl;
    stopable.reset(new Server(ios));
  }
  else {
    auto server_addr = vm["connect"].as<string>();
    auto server_ep = resolve(ios, server_addr);
    stopable.reset(new Client(ios, server_ep));
  }

  ios.run();

  cout << "Finished" << endl;
}

//------------------------------------------------------------------------------

