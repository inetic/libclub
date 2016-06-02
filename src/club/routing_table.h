#pragma once

namespace club {

//------------------------------------------------------------------------------
class RoutingTable
{
public:
  RoutingTable(const uuid& my_id);

  void recalculate(const Graph<uuid>&);

private:
  uuid _my_id;
  boost::container::flat_map<uuid, uuid> _map;
};


//------------------------------------------------------------------------------
RoutingTable::RoutingTable(const uuid& my_id)
  : _my_id(my_id)
{}

//------------------------------------------------------------------------------
RoutingTable::recalculate(const Graph<uuid>& graph)
{
}

//------------------------------------------------------------------------------

} // club namespace
