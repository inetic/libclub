#ifndef CLUB_BROADCAST_ROUTING_TABLE_H
#define CLUB_BROADCAST_ROUTING_TABLE_H

namespace club {

//------------------------------------------------------------------------------
class BroadcastRoutingTable
{
public:
  template<class K, class V> using Map = boost::container::flat_map<K, V>;
  template<class T>          using Set = boost::container::flat_set<T>;

public:
  BroadcastRoutingTable(const uuid& my_id);

  void recalculate(const Graph<uuid>&);

  const Set<uuid>& get_targets(const uuid& source) const;

private:
  Set<uuid> recalculate(const Graph<uuid>&, const uuid& source) const;

private:
  uuid                 _my_id;
  Map<uuid, Set<uuid>> _map;
};


//------------------------------------------------------------------------------
BroadcastRoutingTable::BroadcastRoutingTable(const uuid& my_id)
  : _my_id(my_id)
{}

//------------------------------------------------------------------------------
const BroadcastRoutingTable::Set<uuid>&
BroadcastRoutingTable::get_targets(const uuid& source) const
{
  auto targets_i = _map.find(source);

  if (targets_i == _map.end())
  {
    static const Set<uuid> empty_set;
    return empty_set;
  }

  return targets_i->second;
}

//------------------------------------------------------------------------------
void BroadcastRoutingTable::recalculate(const Graph<uuid>& graph)
{
  _map.clear();
  _map.reserve(graph.nodes.size());

  for (auto node : graph.nodes)
  {
    _map[node] = recalculate(graph, node);
  }
}

//------------------------------------------------------------------------------
BroadcastRoutingTable::Set<uuid>
BroadcastRoutingTable::recalculate( const Graph<uuid>& graph
                                  , const uuid&        source) const
{
  // Start from the 'source' then do a breath-fist search until _my_id is
  // found, return all peers of _my_id which have not been visited.
  using std::set;

  bool has_source = graph.nodes.count(source);
  bool has_my_id  = graph.nodes.count(_my_id);

  if (!has_source || !has_my_id) {
    ASSERT(0);
    return Set<uuid>();
  }

  set<uuid> visited;
  set<uuid> to_visit{source};

  while (!to_visit.empty()) {
    set<uuid> new_to_visit;

    for (auto id : to_visit) {
      visited.insert(id);

      if (id == _my_id) {
        visited.insert(new_to_visit.begin(), new_to_visit.end());
        goto finish;
      }

      auto peers_i = graph.edges.find(id);

      if (peers_i == graph.edges.end()) continue;
      for (const auto& peer_id : peers_i->second) {
        if (visited.count(peer_id)) continue;
        new_to_visit.insert(peer_id);
      }
    }

    to_visit = std::move(new_to_visit);
  }

finish:
  auto my_peers_i = graph.edges.find(_my_id);

  Set<uuid> retval;
  if (my_peers_i == graph.edges.end()) {
    return retval;
  }

  for (const auto& peer : my_peers_i->second) {
    if (visited.count(peer) == 0) {
      retval.insert(peer);
    }
  }

  //std::cout << "source " << source
  //          << ": " << _my_id << " -> {" << str_from_range(retval) << "}"
  //          << std::endl;

  return retval;
}

//------------------------------------------------------------------------------

} // club namespace

#endif // ifndef CLUB_BROADCAST_ROUTING_TABLE_H
