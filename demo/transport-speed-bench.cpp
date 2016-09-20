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
enum Action : uint8_t {
  data = 0, stop
};

//------------------------------------------------------------------------------
struct Server : public Stopable {
  using clock = std::chrono::steady_clock;

  asio::io_service& ios;
  vector<uint8_t> contact_data;
  udp::endpoint remote_endpoint;
  udp::socket listening_socket;
  std::shared_ptr<ClubSocket> socket;
  clock::time_point start;
  size_t to_send = 1048576 * 3;
  size_t remaining = to_send;

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
        cout << "Sending" << endl;
        this->start = clock::now();
        this->start_sending();
      });
  }

  void start_sending() {
    using namespace std::chrono;
    float duration = duration_cast<milliseconds>(clock::now() - start).count()
                   / 1000.f;
       
    size_t sent = to_send - remaining;

    cout << "Remaining: " << remaining << " Bytes; "
         << "Sent: " << sent << " Bytes; "
         << "Speed: " << (sent / duration) << " Bps"
         << "          \r" << std::flush;

    if (remaining == 0) {
      cout << "\nFlushing" << endl;
      std::vector<uint8_t> data{Action::stop};
      socket->send_reliable(data, [=](auto error) {
            socket->flush([=]() { this->stop(); });
          });
      return;
    }

    size_t size = 128 + std::rand() % (ClubSocket::packet_size * 2 - 128);
    size = std::min(size, remaining);
    vector<uint8_t> data(size);

    data[0] = Action::data;
    for (size_t i = 1; i < data.size(); ++i) {
      data[i] = std::rand();
    }

    socket->send_reliable(move(data), [=](auto error) {
          this->remaining -= size;
          this->start_sending();
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
  size_t received = 0;

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
        cout << "Rendezvous connect success to " << server_ep << endl;
        cout << "Receiving" << endl;
        this->start_receiving();
      });
  }

  void start_receiving() {
    socket->receive_reliable([=](auto error, auto buffer) {
        if (error) {
          cout << "\nError receiving data from server: " << error.message() << endl;
          return this->stop();
        }
        auto buf_size = asio::buffer_size(buffer);

        if (buf_size < 1) {
          assert(0);
          return;
        }

        received += buf_size;
        const char* b = asio::buffer_cast<const char*>(buffer);

        if (b[0] == Action::data) {
          cout << "Received " << buf_size << " bytes; "
               << "Total " << received
               << "        \r" << std::flush;
          this->start_receiving();
        }
        else {
          cout << "\nReceived done" << endl;
          this->stop();
        }
      });
  }

  void stop() override {
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

