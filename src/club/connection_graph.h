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

#ifndef __CLUB_CONNECTION_GRAPH_H__
#define __CLUB_CONNECTION_GRAPH_H__

#include <boost/asio/ip/address.hpp>
#include <map>
#include <set>
#include "club/uuid.h"

namespace club {

// The internet is divided into private networks, so when
// we want to know an address of some node (identified by ID)
// we can't just use an address some other node is using.
// But we can use the information while traveling the
// connection graph to determine that address and that is what
// this class does.

class ConnectionGraph {
public:
  using Address = boost::asio::ip::address;
  using ID      = club::uuid;

public:
  ConnectionGraph();

  void add_connection(ID from, ID to, Address address);

  Address find_address(ID from, ID to);

private:
  std::set<Address> find_addresses(ID from, ID to);

  void find_addresses( ID from
                     , ID to
                     , Address most_public
                     , std::set<ID>& visited
                     , std::set<Address>& result);


private:
  std::map<ID, std::map<ID, Address>> _connections;
};

} // club namespace

#endif // ifndef __CLUB_CONNECTION_GRAPH_H__
