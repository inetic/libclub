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
#include "server.h"

namespace ip = boost::asio::ip;

using udp = boost::asio::ip::udp;
using std::cout;
using std::cerr;
using std::endl;
using std::move;
using Error = boost::system::error_code;
using std::unique_ptr;

int main(int argc, const char* argv[]) {
  rendezvous::options options;

  options.parse_command_line(argc, argv);

  boost::asio::io_service ios;
  unique_ptr<rendezvous::server> server;

  try {
    server.reset(new rendezvous::server(ios, options));
    if (options.verbosity() > 0) {
      cout << "Listening on port " << server->local_endpoint().port() << endl;
    }
  }
  catch (boost::system::system_error exception) {
    cerr << exception.what() << endl;
    return 1;
  }

  ios.run();

  if (options.verbosity() > 0) {
    cout << "Finish" << endl;
  }
}

