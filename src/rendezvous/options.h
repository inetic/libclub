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

#ifndef __RENDEZVOUS_OPTIONS_H__
#define __RENDEZVOUS_OPTIONS_H__

#include <boost/program_options.hpp>

namespace rendezvous {

struct options {
public:
  static uint16_t default_port()      { return 6378; }
  static uint16_t default_verbosity() { return 1; }

  void port(uint16_t);
  uint16_t port() const;
  uint16_t verbosity() const;

  options();

  void parse_command_line(int argc, const char* argv[]);

private:
  uint16_t _port;
  uint16_t _verbosity;
};

} // rendezvous namespace

namespace rendezvous {

inline options::options()
  : _port(default_port())
  , _verbosity(default_verbosity())
{
}

inline void options::port(uint16_t p) { _port = p; }
inline uint16_t options::port() const { return _port; }
inline uint16_t options::verbosity() const { return _verbosity; }

inline
void options::parse_command_line(int argc, const char* argv[]) {
  namespace po = boost::program_options;

  po::options_description desc("Options");

  desc.add_options()
    ("help,h", "output this help message")
    ("port,p", po::value<uint16_t>()->default_value(default_port())
             , "listening UDP port number")
    ("verbosity,v", po::value<uint16_t>()->default_value(default_verbosity())
                  , "level of output from the server");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return;
  }

  _port = vm["port"].as<uint16_t>();
  _verbosity = vm["verbosity"].as<uint16_t>();
}

} // rendezvous namespace

#endif //ifndef __RENDEZVOUS_OPTIONS_H__
