#ifndef TRANSPORT_CLUB_DIJKSTRA_H
#define TRANSPORT_CLUB_DIJKSTRA_H

#include <club/graph.h>

namespace club { namespace transport {

struct Dijkstra {
  uuid start;

  std::map<uuid, size_t> distance;
  std::map<uuid, uuid>   predecessor;

  Dijkstra(uuid start, const Graph<uuid>& graph)
    : start(start)
  {
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
  
      if (distance[u] == ((size_t) -1)) break;

      for (auto peer_id : ui->second) {
        auto alt = distance[u] + 1 /* length(u,peer) */;
        if (alt < distance[peer_id]) {
          distance[peer_id] = alt;
          predecessor[peer_id] = ui->first;
        }
      }
    }
  }

  boost::optional<uuid> first_node_to(uuid target) {
    if (target == start) return start;

    while (true) {
      auto pi = predecessor.find(target);
      
      if (pi == predecessor.end() || pi->second == target) {
        return boost::none;
      }

      if (pi->second == start) {
        return target;
      }

      target = pi->second;
    }
  }
};

}} // club::transport namespace

#endif // ifndef TRANSPORT_CLUB_DIJKSTRA_H
