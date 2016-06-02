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

#ifndef __CLUB_DIJKSTRA_H__
#define __CLUB_DIJKSTRA_H__

#include "node.h"

namespace club {

struct Dijkstra {
  std::map<uuid, size_t> distance;
  std::map<uuid, uuid>   predecessor;

  uuid start;
  Graph<uuid> graph;

  Dijkstra(uuid start, Graph<uuid>&& graph)
    : start(start)
    , graph(std::move(graph))
  {}

  void run() {
    using namespace boost::adaptors;
  
    distance.clear();
    predecessor.clear();
  
    std::set<uuid> Q;
  
    // Initialization.
    for (auto& node_id : graph.nodes) {
      distance[node_id] = -1;
      predecessor[node_id] = node_id;
      Q.insert(node_id);
    }
  
    distance[start] = 0;
  
    auto extract_min = [&](std::set<uuid>& Q) {
      auto ret_id   = *Q.begin();
      auto min_dist = distance[ret_id];
      for (auto id : Q) {
        auto d = distance[id];
        if (d < min_dist) {
          ret_id = id;
        }
      }
      Q.erase(ret_id);
      return ret_id;
    };
  
    while (!Q.empty()) {
      auto u = extract_min(Q);
      auto ui = graph.edges.find(u);
  
      for (auto peer_id : ui->second) {
        auto alt = distance[peer_id] + 1 /* length(u,peer) */;
        if (alt < distance[peer_id]) {
          distance[peer_id] = alt;
          predecessor[peer_id] = ui->first;
        }
      }
    }
  }

  uuid first_node_to(uuid target) {
    if (target == start) return start;

    while (true) {
      auto pi = predecessor.find(target);
      ASSERT(pi != predecessor.end());

      if (pi->second == start) {
        return target;
      }
      target = pi->second;
    }
  }
};

} // club namespace

#endif // ifndef __CLUB_DIJKSTRA_H__
