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

#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include <iostream>
#include "club/graph.h"
#include "debug/string_tools.h"

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(graph) {
  using ID = unsigned int;
  using G = club::Graph<ID>;
  using IDs = std::set<ID>;

  {
    G g;

    g.nodes.insert(0);
    g.nodes.insert(1);

    auto removed = g.remove_unreachable_nodes(0);

    BOOST_CHECK(removed == IDs({1}));
    BOOST_CHECK(g.nodes == IDs({0}));
  }

  {
    G g;

    g.nodes.insert(0);
    g.nodes.insert(1);

    g.add_edge(0, 1);

    auto removed = g.remove_unreachable_nodes(0);

    BOOST_CHECK(removed == IDs({}));
    BOOST_CHECK(g.nodes == IDs({0, 1}));
  }

  {
    //       1      4
    //       |\    /|
    //       | 0--3 |
    //       |/    \|
    //       2      5
    G g;

    g.nodes.insert(0);
    g.nodes.insert(1);
    g.nodes.insert(2);
    g.nodes.insert(3);
    g.nodes.insert(4);
    g.nodes.insert(5);

    g.add_edge(0, 1);
    g.add_edge(1, 2);
    g.add_edge(2, 0);

    g.add_edge(3, 4);
    g.add_edge(4, 5);
    g.add_edge(5, 3);

    g.add_edge(1, 3);

    {
      auto removed = g.remove_unreachable_nodes(1);

      BOOST_CHECK(removed == IDs({}));
      BOOST_CHECK(g.nodes == IDs({0, 1, 2, 3, 4, 5}));
    }

    g.remove_edge(1, 3);

    {
      auto removed = g.remove_unreachable_nodes(1);

      BOOST_CHECK(removed == IDs({3, 4, 5}));
      BOOST_CHECK(g.nodes == IDs({0, 1, 2}));
    }
  }
}

