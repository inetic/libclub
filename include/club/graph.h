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

#ifndef CLUB_GRAPH_H
#define CLUB_GRAPH_H

#include <map>
#include <set>

namespace club {

template<class ID> struct Graph {
  using IDs = std::set<ID>;
  using Edges = std::map<ID, IDs>;

  IDs   nodes;
  Edges edges;

  void add_edge(ID from, ID to) {
    nodes.insert(from);
    nodes.insert(to);
    edges[from].insert(to);
  }

  size_t edge_count() const {
    size_t ret = 0;
    for (const auto& e : edges) ret += e.second.size();
    return ret;
  }

  void remove_edge(ID from, ID to) {
    auto from_i = edges.find(from);
    if (from_i == edges.end()) { return; }

    auto to_i = from_i->second.find(to);
    if (to_i == from_i->second.end()) { return; }

    from_i->second.erase(to_i);
    if (from_i->second.empty()) {
      edges.erase(from_i);
    }
  }

  IDs remove_unreachable_nodes(ID pivot) {
    IDs not_visited = std::move(nodes);
    IDs to_visit;
    Edges edges = std::move(this->edges);

    auto erased = not_visited.erase(pivot);

    if (!erased) { return not_visited; }

    nodes.insert(pivot);
    to_visit.insert(pivot);

    while (!to_visit.empty()) {
      auto to_visit_ = std::move(to_visit);

      for (const auto& id : to_visit_) {
        not_visited.erase(id);
        nodes.insert(id);

        auto edges_from_id_i = edges.find(id);

        if (edges_from_id_i == edges.end()) {
          continue;
        }

        for (auto peer : edges_from_id_i->second) {
          this->edges[id].insert(peer);

          if (nodes.count(peer) == 0) {
            to_visit.insert(peer);
          }
        }
      }
    }

    return not_visited;
  }

  bool operator==(const Graph<ID>& g) const {
    return nodes == g.nodes && edges == g.edges;
  }
};

template<class T>
std::ostream& operator<<(std::ostream& os, const Graph<T>& g) {
  os << "([";
  for (auto n_i = g.nodes.begin(); n_i != g.nodes.end();) {
    os << *n_i;
    if (++n_i != g.nodes.end()) { os << ", "; }
  }
  os << "], [";
  for (auto e1_i = g.edges.begin(); e1_i != g.edges.end();) {
    const auto& e2s = e1_i->second;

    for (auto e2_i = e2s.begin(); e2_i != e2s.end();) {
      os << "(" << e1_i->first << ", " << *e2_i << ")";
      if (++e2_i != e2s.end()) { os << ", "; }
    }
    if (++e1_i != g.edges.end()) { os << ", "; }
  }
  return os << "])";
}

} // club namespace

#endif // ifndef CLUB_GRAPH_H
