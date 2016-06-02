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

#include <vector>
#include <algorithm>
#include "connection_graph.h"

using namespace club;
using namespace std;

typedef ConnectionGraph CG;
typedef boost::asio::ip::address Address;
using ID = CG::ID;

//------------------------------------------------------------------------------
CG::ConnectionGraph() {}

//------------------------------------------------------------------------------
static bool first_is_less_public(Address a1, Address a2) {
  if (a1 == a2) return false;
  if (a1.is_unspecified()) return true;
  if (a2.is_unspecified()) return false;
  if (a1.is_loopback() && a2.is_loopback()) return false;
  if (a1.is_loopback()) return true;
  if (a2.is_loopback()) return false;

  if (a1.is_v4()) {
    // TODO: This one probably isn't right.
    if (a2.is_v6()) return true;

    auto a1_ = a1.to_v4();
    auto a2_ = a2.to_v4();

    if (a1_.is_class_c() && a2_.is_class_c()) return false;
    if (a1_.is_class_c()) return true;
    if (a1_.is_class_b() && a2_.is_class_b()) return false;
    if (a1_.is_class_b()) return true;
    return false;
  }
  else { // a1 is ipv6
    // TODO: This one probably isn't right.
    if (!a2.is_v6()) return false;

    auto a1_ = a1.to_v6();
    auto a2_ = a2.to_v6();

    if (a1_.is_link_local() && a2_.is_link_local()) return false;
    if (a1_.is_link_local()) return true;
    return false;
  }
}

//------------------------------------------------------------------------------
void CG::add_connection(ID from, ID to, Address address) {
  _connections[from][to] = address;
}

//------------------------------------------------------------------------------
Address CG::find_address(ID from, ID to) {
  set<Address> addresses = find_addresses(from, to);

  if (addresses.empty()) {
    return Address();
  }

  auto result_i = addresses.begin();

  // Pick the one which is most private.
  for (auto i = ++addresses.begin(); i != addresses.end(); ++i) {
    if (first_is_less_public(*i, *result_i)) {
      result_i = i;
    }
  }

  return *result_i;
}

//------------------------------------------------------------------------------
set<Address> CG::find_addresses(ID from, ID to) {
  set<ID>      visited;
  set<Address> result;
  find_addresses(from, to, Address(), visited, result);
  return result;
}

//------------------------------------------------------------------------------
static vector<pair<ID, Address>>
get_sorted_neighbors(const map<ID, Address>& neighbors) {
  // Sort neighbors, by publicness. The reasoning is that
  // those which are more private shall be closer.
  vector<pair<ID, Address>> result(neighbors.size());
  copy(neighbors.begin(), neighbors.end(), result.begin());
  sort(result.begin(), result.end(),
      []( const pair<ID, Address>& pair1
        , const pair<ID, Address>& pair2) {
        auto a1 = pair1.second;
        auto a2 = pair2.second;
        return first_is_less_public(a1, a2);
      });
  return result;
}

//------------------------------------------------------------------------------
void CG::find_addresses( ID            from
                       , ID            to
                       , Address       most_public
                       , set<ID>&      visited
                       , set<Address>& result) {

  if (visited.count(from) != 0) { return; }
  visited.insert(from);

  auto neighbors_i = _connections.find(from);
  if (neighbors_i == _connections.end()) { return; }

  const auto& neighbors = neighbors_i->second;

  auto to_ep_i = neighbors.find(to);

  if (to_ep_i != neighbors.end()) { 
    // One of the neighbors is the one we're looking for.
    auto ep = to_ep_i->second;

    result.insert(first_is_less_public(ep, most_public)
                  ? most_public
                  : ep);
    return;
  }

  vector<pair<ID, Address>> sorted_neighbors = get_sorted_neighbors(neighbors);

  for (auto id_ep : sorted_neighbors) {
    auto id = id_ep.first;
    auto ep = id_ep.second;

    find_addresses( id
                  , to
                  , first_is_less_public(ep, most_public) ? most_public : ep
                  , visited
                  , result);
  }
}

//------------------------------------------------------------------------------
