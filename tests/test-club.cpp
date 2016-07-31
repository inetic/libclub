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
#include <boost/range/adaptor/indirected.hpp>

#include <iostream>
#include <set>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <club/hub.h>
#include <club/graph.h>
#include "when_all.h"
#include "async_loop.h"
#include "make_connected_sockets.h"
#include "binary/dynamic_encoder.h"
#include "binary/decoder.h"
#include "debug/string_tools.h"

using SocketPtr = std::shared_ptr<club::Socket>;
using std::cout;
using std::endl;
using std::make_shared;
using std::make_pair;
using std::unique_ptr;
using std::shared_ptr;
using std::set;
using std::pair;
using std::move;
using std::vector;
using boost::system::error_code;
using boost::asio::io_service;
using HubPtr = unique_ptr<club::hub>;
using club::uuid;
using boost::adaptors::indirected;
using std::function;
namespace asio = boost::asio;

// -------------------------------------------------------------------
class Debugger {
public:
  Debugger() : next_map_id(0) {}

  void map(const HubPtr& hub) {
    cout << "Map(" << hub->id() << ")-><" << next_map_id++ << ">" << endl;
  }

  template<class... Rs>
  void map(const HubPtr& hub, const Rs&... hubs) {
    map(hub);
    map(hubs...);
  }

  void map(const std::vector<HubPtr>& hubs) {
    for (const auto& hub : hubs) {
      map(hub);
    }
  }

private:
  size_t next_map_id;
};

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_one_fusion) {
  io_service ios;

  unique_ptr<club::hub> r1(new club::hub(ios));
  unique_ptr<club::hub> r2(new club::hub(ios));

  //Debugger d;
  //d.map(r1);
  //d.map(r2);

  bool hubs_fused = false;

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      r1->fuse(move(*s1), when_all.make_continuation(
            [=](auto c, error_code e, uuid) {
              BOOST_REQUIRE(!e);
              c();
            }));

      r2->fuse(move(*s2), when_all.make_continuation(
          [=](auto c, error_code e, uuid) {
            BOOST_REQUIRE(!e);
            c();
          }));

      when_all.on_complete([&]() {
          hubs_fused = true;
          r1.reset();
          r2.reset();
        });
    });

  ios.run();

  BOOST_CHECK(hubs_fused);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_leave_and_remove) {
  io_service ios;

  HubPtr r1(new club::hub(ios));
  HubPtr r2(new club::hub(ios));

  bool r1_removed = false;
  bool hubs_fused = false;

  //Debugger d;
  //d.map(r1, r2);

  make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
      WhenAll when_all;

      for (auto pair : {make_pair(&r1, s1), make_pair(&r2, s2) }) {
        auto& r = *pair.first;
        auto& s = pair.second;

        r->on_insert.connect(when_all.make_continuation(
              [](auto c, set<club::hub::node>) {
                c();
              }));

        r->fuse(move(*s), when_all.make_continuation(
            [](auto c, error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              c();
            }));
      }

      when_all.on_complete([&]() {
          hubs_fused = true;

          r1.reset();
        });

      r2->on_remove.connect([&](set<club::hub::node>) {
          r1_removed = true;

          r1.reset();
          r2.reset();
          });

    });

  ios.run();

  BOOST_CHECK(hubs_fused);
  BOOST_CHECK(r1_removed);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_two_consecutive_fusions) {
  io_service ios;

  unique_ptr<club::hub> r1(new club::hub(ios));
  unique_ptr<club::hub> r2(new club::hub(ios));
  unique_ptr<club::hub> r3(new club::hub(ios));

  WhenAll when_all1;
  WhenAll when_all2;

  bool hubs_fused = false;

  make_connected_sockets(ios, [&](SocketPtr s11, SocketPtr s12) {
    make_connected_sockets(ios, [&, s11, s12](SocketPtr s21, SocketPtr s22) {

      r1->fuse(move(*s11), when_all1.make_continuation(
            [](auto c, error_code e, uuid) {
              BOOST_CHECK(!e);
              c();
            }));

      r2->fuse(move(*s12), when_all1.make_continuation(
            [](auto c, error_code e, uuid) {
              BOOST_CHECK(!e);
              c();
            }));

      when_all1.on_complete([&, s21, s22]() mutable {
          r1->fuse(move(*s21), when_all2.make_continuation(
                [](auto c, error_code e, uuid) {
                  BOOST_CHECK(!e);
                  c();
                }));

          r3->fuse(move(*s22), when_all2.make_continuation(
                [](auto c, error_code e, uuid) {
                  BOOST_CHECK(!e);
                  c();
                }));

          when_all2.on_complete([&]() {
                  hubs_fused = true;
                  r1.reset();
                  r2.reset();
                  r3.reset();
                });
        });
    });
  });

  ios.run();

  BOOST_CHECK(hubs_fused);
}

// -------------------------------------------------------------------
void consecutive_fusions(const size_t N) {
  auto seed = std::time(0);
  std::srand(seed);

  io_service ios;

  bool hubs_fused = false;
  size_t insert_count = 0;
  size_t dc_count = 0;

  const size_t exp_insert_count = N*(N-1);
  const size_t exp_dc_count     = N*(N-1) - 2*(N-1);

  bool wait_for_dc_count = false;

  //cout << "N = " << N << endl;
  //cout << "expected dc count " << exp_dc_count << endl;
  //cout << "expected insert count " << exp_insert_count << endl;

  vector<HubPtr> hubs;

  auto on_finish = [&]() {
    if (insert_count == exp_insert_count) {
      if (!wait_for_dc_count || dc_count == exp_dc_count) {
        for(auto& hub : hubs) {
          hub.reset();
        }
      }
    }
  };

  for (size_t i = 0; i < N; ++i) {
    hubs.emplace_back(new club::hub(ios));

    hubs[i]->on_insert.connect([&, i](set<club::hub::node> nodes){
          insert_count += nodes.size();
          on_finish();
        });

    hubs[i]->on_direct_connect.connect([&](club::hub::node){
          ++dc_count;
          on_finish();
        });
  }

  //Debugger d;
  //d.map(hubs);

  async_loop([&](unsigned int i, Cont cont) {
    if (i == 0) return cont();

    if (i == N) {
      hubs_fused = true;
      return;
    }

    make_connected_sockets(ios, [&, i, cont]
                                (SocketPtr s1, SocketPtr s2) {

        WhenAll when_all;

        size_t j = std::rand() % i;

        hubs[j]->fuse(move(*s1), when_all.make_continuation(
          [](auto c, error_code e, uuid) {
            BOOST_CHECK_MESSAGE(!e, e.message());
            c();
          }));

        hubs[i]->fuse(move(*s2), when_all.make_continuation(
          [](auto c, error_code e, uuid) {
            BOOST_CHECK_MESSAGE(!e, e.message());
            c();
          }));

        when_all.on_complete(cont);
      });
    });

  ios.run();

  BOOST_CHECK(hubs_fused);
  BOOST_CHECK_EQUAL(insert_count, N*(N-1));
}

BOOST_AUTO_TEST_CASE(club_consecutive_fusions_2) { consecutive_fusions(2); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_3) { consecutive_fusions(3); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_4) { consecutive_fusions(4); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_5) { consecutive_fusions(5); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_6) { consecutive_fusions(6); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_7) { consecutive_fusions(7); }
BOOST_AUTO_TEST_CASE(club_consecutive_fusions_8) { consecutive_fusions(8); }

// -------------------------------------------------------------------
template<class Handler /* void() */>
void fuse_n_hubs( boost::asio::io_service& ios
                , std::vector<HubPtr>&     hubs
                , bool                     wait_for_direct_connections
                , Handler                  handler) {
  const size_t N         = hubs.size();
  auto remaining_inserts = make_shared<uint32_t>(N * (N - 1));
  auto remaining_dcs     = make_shared<uint32_t>(N * (N - 1) - 2*(N-1));

  auto cs = make_shared<vector<boost::signals2::connection>>();

  if (N == 0 || N == 1) {
    return ios.post(handler);
  }

  auto on_complete = [ cs
                     , wait_for_direct_connections
                     , remaining_inserts
                     , remaining_dcs
                     , handler]() {
    if (*remaining_inserts == 0) {
      if (!wait_for_direct_connections || *remaining_dcs == 0) {
        for (auto& c : *cs) {
          c.disconnect();
        }
      }
      handler();
    }
  };

  for (size_t i = 0; i < N; ++i) {
    auto c1 = hubs[i]->on_insert.connect(
          [ remaining_inserts
          , on_complete](set<club::hub::node> nodes){
        (*remaining_inserts) -= nodes.size();
        on_complete();
      });

    auto c2 = hubs[i]->on_direct_connect.connect(
          [ remaining_dcs
          , on_complete](club::hub::node) {
        --(*remaining_dcs);
        on_complete();
      });

    cs->push_back(c1);
    cs->push_back(c2);
  }

  async_loop([&ios, &hubs, N](unsigned int i, Cont cont) {
    if (i == 0) return cont();
    if (i == N) return;

    make_connected_sockets(ios, [i, cont, &hubs] (SocketPtr s1, SocketPtr s2) {

        WhenAll when_all;

        hubs[0]->fuse(move(*s1), when_all.make_continuation(
            [](auto c, error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              c();
            }));

        hubs[i]->fuse(move(*s2), when_all.make_continuation(
            [](auto c, error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              c();
            }));

        when_all.on_complete(cont);
      });
    });
}

// -------------------------------------------------------------------
std::vector<HubPtr> make_hubs(io_service& ios, size_t count) {
  std::vector<HubPtr> ret;

  for (size_t i = 0; i < count; ++i) {
    ret.emplace_back(unique_ptr<club::hub>(new club::hub(ios)));
  }

  std::sort(ret.begin(), ret.end(), [](const HubPtr& r1, const HubPtr& r2) {
      return r1->id() < r2->id();
      });

  return ret;
}

// -------------------------------------------------------------------
std::set<uuid> hub_ids(const std::vector<HubPtr>& hubs) {
  std::set<uuid> ret;
  for (const auto& r: hubs) { ret.insert(r->id()); }
  return ret;
}

// -------------------------------------------------------------------
template<class Handler>
void construct_network( io_service& ios
                      , club::Graph<size_t> graph
                      , Handler h) {
  using G = club::Graph<size_t>;

  struct State {
    // Poor man's iterator
    struct I {
      I(G::Edges& edges)
        : edges(edges)
        , first_i(edges.begin())
      {
        if (first_i != edges.end()) {
          second_i = first_i->second.begin();
        }
      }

      G::Edges&          edges;
      G::Edges::iterator first_i;
      G::IDs::iterator   second_i;

      I& operator++() { // pre-increment
        if (first_i == edges.end()) return *this;
        ++second_i;
        if (second_i == first_i->second.end()) {
          ++first_i;
          if (first_i == edges.end()) return *this;
          second_i = first_i->second.begin();
        }
        return *this;
      }

      bool is_end() const {
        return first_i == edges.end();
      }

      size_t from() const { return first_i->first; }
      size_t to()   const { return *second_i; }
    };

    State(io_service& ios, G&& graph, size_t node_count)
      : hubs(make_hubs(ios, node_count))
      , insert_countdown(graph.nodes.size() * (graph.nodes.size() - 1))
      , fuse_countdown(graph.edge_count() * 2)
      , graph(move(graph))
      , edge_i(this->graph.edges)
    { }

    vector<HubPtr> hubs;
    size_t         insert_countdown;
    size_t         fuse_countdown;
    G              graph;
    I              edge_i;

    vector<boost::signals2::scoped_connection> connections;
  };

  auto state = make_shared<State>(ios, move(graph), graph.nodes.size());

  //Debugger d;
  //d.map(state->hubs);

  auto on_event = [=]() {
    if (state->insert_countdown == 0 && state->fuse_countdown == 0) {
      state->connections.clear();
      h(move(state->hubs));
    }
  };

  for (auto& hub : state->hubs) {
    auto id = hub->id();
    auto c = hub->on_insert.connect([id, on_event, state] (set<club::hub::node> nodes) {
        set<uuid> ids;
        for (auto n : nodes) ids.insert(n.id());
        state->insert_countdown -= nodes.size();
        on_event();
      });

    state->connections.push_back(move(c));
  }

  async_loop([=, &ios](unsigned int i, Cont cont) {
      if (state->edge_i.is_end()) {
        return on_event();
      }

      make_connected_sockets(ios, [=](SocketPtr s1, SocketPtr s2) {
          WhenAll when_all;

          auto k = state->edge_i.from();
          auto l = state->edge_i.to();

          ASSERT(k < state->hubs.size());
          ASSERT(l < state->hubs.size());

          state->hubs[k]->fuse(move(*s1), when_all.make_continuation(
              [state](auto c, error_code e, uuid) {
                BOOST_CHECK_MESSAGE(!e, e.message());
                --state->fuse_countdown;
                c();
            }));

          state->hubs[l]->fuse(move(*s2), when_all.make_continuation(
              [state](auto c, error_code e, uuid) {
                BOOST_CHECK_MESSAGE(!e, e.message());
                --state->fuse_countdown;
                c();
            }));

          when_all.on_complete([=]() {
              ++state->edge_i;
              cont();
            });
        });
    });
}

// -------------------------------------------------------------------
club::Graph<size_t> clique_graph(size_t node_count) {
  club::Graph<size_t> graph;

  if (node_count == 0) return graph;

  if (node_count == 1) {
    graph.nodes.insert(0);
  }

  for (size_t i = 0; i < node_count; ++i) {
    for (size_t j = i + 1; j < node_count; ++j) {
      graph.add_edge(i,j);
    }
  }

  return graph;
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_manual_clique) {
  std::srand(std::time(0));

  io_service ios;

  size_t node_count = 3 + std::rand() % 5;

  auto on_finish = [&](vector<HubPtr> hubs) {
    BOOST_REQUIRE(hubs.size() == node_count);
    hubs.clear();
  };

  construct_network(ios, clique_graph(node_count), on_finish);

  ios.run();

  // TODO: Explicit check that each hub really has a clique.
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_fuse_populated_hubs) {
  std::srand(std::time(0));

  io_service ios;

  vector<HubPtr> hubs1 = make_hubs(ios, 1 + std::rand() % 5);
  vector<HubPtr> hubs2 = make_hubs(ios, 1 + std::rand() % 5);

  //Debugger d;
  //d.map(hubs1);
  //d.map(hubs2);

  set<uuid> hubs1_ids = hub_ids(hubs1);
  set<uuid> hubs2_ids = hub_ids(hubs2);

  WhenAll when_all;

  auto on_fuse_hubs1 = when_all.make_continuation();
  auto on_fuse_hubs2 = when_all.make_continuation();

  bool wait_for_dcs = false;

  fuse_n_hubs(ios, hubs1, wait_for_dcs, on_fuse_hubs1);
  fuse_n_hubs(ios, hubs2, wait_for_dcs, on_fuse_hubs2);

  auto close_hubs = [&]() {
    for (auto* hubs : {&hubs1, &hubs2}) {
      for (auto& hub : *hubs) {
        hub.reset();
      }
    }
  };

  //cout << "Hubs1 size = " << hubs1.size() << endl;
  //cout << "Hubs2 size = " << hubs2.size() << endl;

  auto N1 = hubs1.size();
  auto N2 = hubs2.size();
  auto remaining_dc_count     = 2 * N1 * N2 - 2;
  auto remaining_insert_count = 2 * N1 * N2;

  //cout << "expected dc count = " << remaining_dc_count << endl;
  //cout << "expected insert count = " << remaining_insert_count << endl;

  vector<vector<uuid>> insert_records1(N1);
  vector<vector<uuid>> insert_records2(N2);

  auto hub_sets    = {&hubs1, &hubs2};
  auto record_sets = {&insert_records1, &insert_records2};

  when_all.on_complete([&]() {

      for (size_t i = 0; i < 2; ++i) {
        auto& hubs           = *hub_sets.begin()[i];
        auto& insert_records = *record_sets.begin()[i];

        for (size_t j = 0; j < hubs.size(); ++j) {

          hubs[j]->on_insert.connect([&, j](set<club::hub::node> nodes) {
              for (auto node : nodes) {
                insert_records[j].push_back(node.id());
                --remaining_insert_count;
              }

              if (remaining_insert_count == 0) {
                if (!wait_for_dcs || remaining_dc_count == 0) {
                  close_hubs();
                }
              }
            });

          hubs[j]->on_direct_connect.connect([&](club::hub::node) {
              --remaining_dc_count;
              if (remaining_insert_count == 0) {
                if (!wait_for_dcs || remaining_dc_count == 0) {
                  close_hubs();
                }
              }
            });
        }
      }

      make_connected_sockets(ios, [&](SocketPtr s1, SocketPtr s2) {
          auto i1 = std::rand() % hubs1.size();
          auto i2 = std::rand() % hubs2.size();

          //cout << "**************************************************" << endl;
          //cout << "Merging " << i1 << " with " << (hubs1.size() + i2) << endl;
          hubs1[i1]->fuse(move(*s1), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });

          hubs2[i2]->fuse(move(*s2), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });
          });

    });

  ios.run();

  for (auto records : record_sets | indirected) {
    for (size_t i = 1; i < records.size(); ++i) {
      BOOST_CHECK(records[0] == records[i]);
    }
  }

  BOOST_CHECK_EQUAL(remaining_insert_count, 0);

  if (wait_for_dcs) {
    BOOST_CHECK_EQUAL(remaining_dc_count, 0);
  }
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_parallel_fusion) {
  std::srand(std::time(0));

  io_service ios;

  vector<HubPtr> hubs1 = make_hubs(ios, 2 + std::rand() % 5);
  vector<HubPtr> hubs2 = make_hubs(ios, 2 + std::rand() % 5);

  //Debugger d;
  //d.map(hubs1);
  //d.map(hubs2);

  set<uuid> hubs1_ids = hub_ids(hubs1);
  set<uuid> hubs2_ids = hub_ids(hubs2);

  WhenAll when_all;

  auto on_fuse_hubs1 = when_all.make_continuation();
  auto on_fuse_hubs2 = when_all.make_continuation();

  fuse_n_hubs(ios, hubs1, false, on_fuse_hubs1);
  fuse_n_hubs(ios, hubs2, false, on_fuse_hubs2);

  auto close_hubs = [&]() {
    for (auto* hubs : {&hubs1, &hubs2}) {
      for (auto& hub : *hubs) {
        hub.reset();
      }
    }
  };

  //cout << "Hubs1 size = " << hubs1.size() << endl;
  //cout << "Hubs2 size = " << hubs2.size() << endl;

  auto N1 = hubs1.size();
  auto N2 = hubs2.size();
  auto remaining_insert_count = 2 * N1 * N2;

  //cout << "expected insert count = " << remaining_insert_count << endl;

  vector<vector<uuid>> insert_records1(N1);
  vector<vector<uuid>> insert_records2(N2);

  auto record_sets = {&insert_records1, &insert_records2};

  when_all.on_complete([&]() {
      size_t j = 0;
      for (auto hubs : {&hubs1, &hubs2}) {
        size_t i = 0;
        for (auto& hub : *hubs) {
          hub->on_insert.connect([ i, j
                                 , &record_sets
                                 , &remaining_insert_count
                                 , close_hubs] (set<club::hub::node> nodes) {
              auto& insert_records = *record_sets.begin()[j];

              for (auto node : nodes) {
                insert_records[i].push_back(node.id());
                --remaining_insert_count;
              }

              if (remaining_insert_count == 0) {
                close_hubs();
              }
            });
          ++i;
        }
        ++j;
      }

      make_connected_sockets(ios, [&](SocketPtr s11, SocketPtr s12) {
        make_connected_sockets(ios, [&,s11,s12](SocketPtr s21, SocketPtr s22) {

          // TODO: Randomize fusing nodes.
          hubs1[0]->fuse(move(*s11), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });

          hubs2[0]->fuse(move(*s12), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });

          hubs1[1]->fuse(move(*s21), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });

          hubs2[1]->fuse(move(*s22), [](error_code e, uuid) {
              BOOST_CHECK_MESSAGE(!e, e.message());
              });
        });
      });
    });

  ios.run();

  for (auto records : record_sets | indirected) {
    for (size_t i = 1; i < records.size(); ++i) {
      BOOST_CHECK(records[0] == records[i]);
    }
  }

  BOOST_CHECK_EQUAL(remaining_insert_count, 0);
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_commit_order) {
  std::srand(std::time(0));

  io_service ios;

  vector<HubPtr> hubs = make_hubs(ios, 2 + std::rand() % 8);

  //Debugger d;
  //d.map(hubs);

  vector<vector<uint64_t>> received(hubs.size());

  //cout << "size=" << hubs.size() << endl;

  fuse_n_hubs(ios, hubs, false, [&]() {
      WhenAll when_all;

      for (uint64_t i = 0; i < hubs.size(); ++i) {
        auto received_all = when_all.make_continuation();

        hubs[i]->on_receive.connect([i, &received, &hubs, received_all]
                                     ( club::hub::node
                                     , const std::vector<char>& data) {
            binary::decoder d(data.data(), data.size());
            auto j = d.get<uint32_t>();
            received[i].push_back(j);

            // Have we received from everyone?
            if (received[i].size() == hubs.size()) {
              received_all();
            }
          });

        auto timer  = make_shared<asio::steady_timer>(ios);
        auto millis = std::chrono::milliseconds(std::rand() % 20);

        timer->expires_from_now(millis);
        timer->async_wait([&, i, timer](error_code) {
            binary::dynamic_encoder<char> e;
            e.put<uint32_t>(i);
            hubs[i]->total_order_broadcast(e.move_data());
          });
      }

      when_all.on_complete([&hubs]() {
          for (auto& r : hubs) r.reset();
          });
  });

  ios.run();

  BOOST_CHECK(received.size() > 1);
  BOOST_CHECK_EQUAL(received.size(), hubs.size());

  bool equal_order = true;

  for (size_t i = 1; i < received.size(); ++i) {
    BOOST_CHECK_EQUAL(received[i].size(), hubs.size());
    // Test that messages arrived in the same order for
    // everyone.
    equal_order = equal_order && (received[i] == received[0]);
  }

  if (!equal_order) {
    BOOST_CHECK_MESSAGE(false, "Failure received[i] != received[0]");
    for (size_t i = 0; i < received.size(); ++i) {
      BOOST_WARN_MESSAGE(false, "received[" << i << "] = " << str(received[i]));
    }
  }
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_commit_remove_order) {
  auto seed = std::time(0);
  std::srand(seed);

  io_service ios;

  vector<HubPtr> hubs = make_hubs(ios, 3 + std::rand() % 8);

  const size_t size = hubs.size();
  const size_t split = (size % 2 == 0) ? (size / 2)
                                       : (size / 2 + 1);

  vector<vector<uint64_t>> received(split);

  std::map<uuid, size_t> id_to_i_map;

  for (size_t i = 0; i < hubs.size(); ++i) {
    id_to_i_map[hubs[i]->id()] = i;
  }

  auto id_to_i = [id_to_i_map](const uuid& id) -> size_t {
    auto i = id_to_i_map.find(id);
    if (i == id_to_i_map.end()) return -1;
    return i->second;
  };

  //Debugger d;
  //d.map(hubs);

  fuse_n_hubs(ios, hubs, false, [&]() {
      WhenAll when_all;

      //cout << "---------------------------------------" << endl;
      //cout << "nodes: " << hubs.size() << endl;
      //cout << "split: " << split << endl;

      for (uint64_t i = 0; i < hubs.size(); ++i) {
        if (i < split) {
          auto received_all = when_all.make_continuation();

          hubs[i]->on_receive.connect([i, &received, &hubs, received_all]
                                       ( club::hub::node
                                       , const std::vector<char>& data) {
              binary::decoder d(data.data(), data.size());
              uint64_t j = d.get<uint32_t>();
              received[i].push_back(j);

              // Have we received from everyone?
              if (received[i].size() == hubs.size()) {
                received_all();
              }
            });

          hubs[i]->on_remove.connect([ i
                                     , &received
                                     , &hubs
                                     , received_all
                                     , id_to_i]
                                     (set<club::hub::node> nodes) {
              set<uuid> ids;
              for (auto n : nodes) { ids.insert(n.id()); }
              ASSERT(!nodes.empty());
              for (auto node : nodes) {
                received[i].push_back(hubs.size() + id_to_i(node.id()));
              }

              // Have we received from everyone?
              if (received[i].size() == hubs.size()) {
                received_all();
              }
              });
        }

        auto timer  = make_shared<asio::steady_timer>(ios);
        auto millis = std::chrono::milliseconds(std::rand() % 20);

        timer->expires_from_now(millis);
        timer->async_wait([&, i, timer](error_code) {
            if (i < split) {
              binary::dynamic_encoder<char> e;
              e.put<uint32_t>(i);
              hubs[i]->total_order_broadcast(e.move_data());
            }
            else {
              hubs[i].reset();
            }
          });
      }

      when_all.on_complete([&hubs]() {
          hubs.clear();
          });
  });

  ios.run();

  BOOST_CHECK(received.size() > 1);
  BOOST_CHECK_EQUAL(received.size(), split);

  bool equal_order = true;

  for (size_t i = 1; i < received.size(); ++i) {
    BOOST_CHECK_EQUAL(received[i].size(), size);
    // Test that messages arrived in the same order for
    // everyone.
    equal_order = equal_order && (received[i] == received[0]);
  }

  if (!equal_order) {
    BOOST_CHECK_MESSAGE(false, "Failure received[i] != received[0]");
    for (size_t i = 0; i < received.size(); ++i) {
      BOOST_WARN_MESSAGE(false, "received[" << i << "] = " << str(received[i]));
    }
  }
}

// -------------------------------------------------------------------
// Given two sets of hubs where each set is expected to be connected
// already, fuse the sets together.
void fuse_hub_sets( io_service& ios
                  , std::vector<HubPtr>&& set1
                  , std::vector<HubPtr>&& set2
                  , function<void (std::vector<HubPtr>&&)> on_join) {
  using SignalConnections = std::list<boost::signals2::connection>;

  ASSERT(set1.size());
  ASSERT(set2.size());

  auto size1 = set1.size();
  auto size2 = set2.size();

  auto new_set = make_shared<vector<HubPtr>>(move(set1));

  std::move( set2.begin()
           , set2.end()
           , std::back_inserter(*new_set));

  make_connected_sockets(ios, [=](SocketPtr s1, SocketPtr s2) {
      size_t m1 = std::rand() % size1;
      size_t m2 = std::rand() % size2 + size1;

      (*new_set)[m1]->fuse(move(*s1), [](error_code error, uuid) {
          BOOST_CHECK_MESSAGE(!error, error.message());
        });

      (*new_set)[m2]->fuse(move(*s2), [](error_code error, uuid) {
          BOOST_CHECK_MESSAGE(!error, error.message());
        });

      auto countdown = make_shared<size_t>(2 * size1 * size2);
      auto connections = make_shared<SignalConnections>();

      for (auto& hub : *new_set | indirected) {
        auto c = hub.on_insert.connect([=](set<club::hub::node> nodes) {
            *countdown -= nodes.size();

            if (*countdown == 0) {
              connections->clear();
              on_join(move(*new_set));
            }
          });

        connections->push_back(c);
      }
    });
}

void fuse_hub_sets_into_clique( io_service& ios
                              , std::vector<HubPtr>&& set1
                              , std::vector<HubPtr>&& set2
                              , function<void (std::vector<HubPtr>&&)> on_join) {
  using SignalConnections = std::list<boost::signals2::connection>;
  using SocketPairs = vector<pair<SocketPtr, SocketPtr>>;

  ASSERT(set1.size());
  ASSERT(set2.size());

  auto size1 = set1.size();
  auto size2 = set2.size();

  auto new_set = make_shared<vector<HubPtr>>(move(set1));

  std::move( set2.begin()
           , set2.end()
           , std::back_inserter(*new_set));

  make_n_connected_socket_pairs( ios
                               , size1 * size2
                               , [=](SocketPairs socket_pairs) {
      for (size_t i = 0; i < size1; i++) {
        for (size_t j = 0; j < size2; j++) {
          auto& socket_pair = socket_pairs[i * size2 + j];

          auto s1 = move(socket_pair.first);
          auto s2 = move(socket_pair.second);

          (*new_set)[i]->fuse(move(*s1), [](error_code error, uuid) {
              BOOST_CHECK_MESSAGE(!error, error.message());
            });

          (*new_set)[size1 + j]->fuse(move(*s2), [](error_code error, uuid) {
              BOOST_CHECK_MESSAGE(!error, error.message());
            });
        }
      }

      auto countdown = make_shared<size_t>(2 * size1 * size2);
      auto connections = make_shared<SignalConnections>();

      for (auto& hub : *new_set | indirected) {
        auto c = hub.on_insert.connect([=](set<club::hub::node> nodes) {
            *countdown -= nodes.size();

            if (*countdown == 0) {
              connections->clear();
              on_join(move(*new_set));
            }
          });

        connections->push_back(c);
      }
    });
}

// -------------------------------------------------------------------
void drop_random_hubs( io_service& ios
                     , size_t drop_count
                     , std::vector<HubPtr>&& hubs
                     , std::function<void (std::vector<HubPtr>&&)> on_drop) {
  using SignalConnections = std::list<boost::signals2::connection>;

  if (drop_count == 0) {
    auto hubs_ptr = make_shared<vector<HubPtr>>(move(hubs));
    return ios.post([=]() { on_drop(move(*hubs_ptr)); });
  }

  // TODO: Special case when drop_count == hubs.size()
  ASSERT(drop_count < hubs.size());

  std::random_shuffle(hubs.begin(), hubs.end());

  {
    std::vector<uuid> dropped;
    for (size_t i = 0; i < drop_count; ++i) {
      dropped.push_back(hubs[hubs.size() - i - 1]->id());
    }
  }

  hubs.resize(hubs.size() - drop_count);

  auto countdown   = make_shared<size_t>(hubs.size() * drop_count);
  auto hubs_ptr    = make_shared<vector<HubPtr>>(move(hubs));
  auto connections = make_shared<SignalConnections>();

  for (auto& hub : *hubs_ptr | indirected) {
    auto c = hub.on_remove.connect([=](std::set<club::hub::node> nodes) {
        *countdown -= nodes.size();

        if (*countdown == 0) {
          connections->clear();
          on_drop(move(*hubs_ptr));
        }
      });

    connections->push_back(c);
  }
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_remove_from_manual_clique) {
  auto seed = std::time(0);
  std::srand(seed);

  io_service ios;

  size_t hub_size = 3 + std::rand() % 3;

  vector<HubPtr> hubs;

  construct_network(ios, clique_graph(hub_size), [&](vector<HubPtr> hs) {
      hubs = move(hs);

      size_t drop_count = 1 + std::rand() % (hubs.size() - 1);

      //std::cout << "--------------------------------------------" << endl;

      drop_random_hubs( ios
                      , drop_count
                      , move(hubs)
                      , [&](vector<HubPtr>&& hubs) {
                        ASSERT(!hubs.empty());
                        hubs.clear();
                      });
    });

  ios.run();
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_fuse_again) {
  std::srand(std::time(0));

  io_service ios;

  // Minimum size is 2, so we can drop one.
  size_t hub_size_1 = 2 + std::rand() % 5;
  // Minimum size is 1.
  size_t hub_size_2 = 1 + std::rand() % 5;

  vector<HubPtr> hubs1;
  vector<HubPtr> hubs2;

  //cout << "hub_size_1 = " << hub_size_1 << endl;
  //cout << "hub_size_2 = " << hub_size_2 << endl;

  WhenAll when_all;

  auto on_fuse1 = when_all.make_continuation();
  auto on_fuse2 = when_all.make_continuation();

  construct_network(ios, clique_graph(hub_size_1), [&](vector<HubPtr> hs) {
      hubs1 = move(hs);
      on_fuse1();
    });

  construct_network(ios, clique_graph(hub_size_2), [&](vector<HubPtr> hs) {
      hubs2 = move(hs);
      on_fuse2();
    });

  when_all.on_complete([&]() {
      //Debugger d;
      //d.map(hubs1);
      //d.map(hubs2);

      size_t drop_count = 1 + std::rand() % (hubs1.size() - 1);

      drop_random_hubs
        ( ios
        , drop_count
        , move(hubs1)
        , [&, drop_count](vector<HubPtr>&& hubs) {
          ASSERT(!hubs.empty());
          BOOST_REQUIRE_EQUAL(hubs.size(), hub_size_1 - drop_count);

          //cout << "fusing: " << hubs.size() << " " << hubs2.size() << endl;
          fuse_hub_sets_into_clique
              ( ios
              , move(hubs)
              , move(hubs2)
              , [&](vector<HubPtr>&& hubs) {
                   size_t exp_size = hub_size_1 - drop_count + hub_size_2;
                   BOOST_REQUIRE_EQUAL(hubs.size(), exp_size);
                   //cout << "DONE" << endl;
                   hubs.clear();
                 });
        });
    });

  ios.run();
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(club_stress_fuse) {
  std::srand(std::time(0));

  io_service ios;

  vector<HubPtr> hubs1;
  vector<HubPtr> hubs2;

  // Minimum size is 2, so we can drop at least one.
  size_t hub_size_1 = 2 + std::rand() % 5;

  const size_t N = 2 + std::rand() % 8;

  construct_network(ios, clique_graph(hub_size_1), [&](vector<HubPtr> hs) {
      hubs1 = move(hs);

      async_loop([&](unsigned int i, Cont cont) {
          if (i == N) {
            hubs1.clear();
            hubs2.clear();
            return; 
          }

          // Minimum size is 1.
          size_t hub_size_2 = 1 + std::rand() % 5;

          WhenAll when_all;

          size_t drop_count = 1 + std::rand() % (hubs1.size() - 1);
          size_t exp_size = hubs1.size() - drop_count;

          drop_random_hubs
            ( ios
            , drop_count
            , move(hubs1)
            , when_all.make_continuation(
              [&hubs1, exp_size](auto c, vector<HubPtr>&& hubs) {
                BOOST_REQUIRE_EQUAL(hubs.size(), exp_size);
                hubs1 = move(hubs);
                c();
              }));

          construct_network( ios
                           , clique_graph(hub_size_2)
                           , when_all.make_continuation(
                             [&](auto c, vector<HubPtr> hs) {
                               hubs2 = move(hs);
                               c();
                             }));

          when_all.on_complete([&, cont]() {
              size_t exp_size = hubs1.size() + hubs2.size();

              // TODO: Why does the below ASSERT(hubs2.empty()) fail
              //       when this temporary isn't used?
              auto hubs2_ = move(hubs2);

              fuse_hub_sets_into_clique
                  ( ios
                  , move(hubs1)
                  , move(hubs2_)
                  , [&, exp_size, cont](vector<HubPtr>&& fused_hubs) {
                       BOOST_REQUIRE_EQUAL(fused_hubs.size(), exp_size);
                       ASSERT(hubs1.empty());
                       ASSERT(hubs2.empty());
                       hubs1 = move(fused_hubs);
                       cont();
                     });
            });
        });
     });

  ios.run();
}

// -------------------------------------------------------------------
// This test assumes that no UDP packets are dropped on this PC. If
// packet dropping is enabled (e.g. through netem), this test will
// probably fail.
BOOST_AUTO_TEST_CASE(club_unreliable_broadcast) {
  using boost::asio::const_buffer;

  std::srand(std::time(0));

  io_service ios;

  club::Graph<size_t> graph;

  //TODO: Make random connected graph to test on.

  //----------------------------------------------
  // Test case #1
  //cout << " 3 - 2 " << endl;
  //cout << " |   | " << endl;
  //cout << " 0 - 1 " << endl;

  //graph.add_edge(0, 1);
  //graph.add_edge(1, 2);
  //graph.add_edge(2, 3);
  //graph.add_edge(3, 0);

  //----------------------------------------------
  // Test case #2
  //cout << "         5 - 6     " << endl;
  //cout << "         |   |     " << endl;
  //cout << " 0 - 1 - 2 - 3 - 4 " << endl;
  //cout << "     |             " << endl;
  //cout << "     7 - 8         " << endl;

  graph.add_edge(0, 1);
  graph.add_edge(1, 2);
  graph.add_edge(2, 3);
  graph.add_edge(3, 4);
  graph.add_edge(2, 5);
  graph.add_edge(5, 6);
  graph.add_edge(6, 3);
  graph.add_edge(1, 7);
  graph.add_edge(7, 8);

  vector<HubPtr> hubs;

  std::map<uuid, size_t> receivers;

  construct_network(ios, move(graph), [&](vector<HubPtr> hs) {
      hubs = move(hs);

      // Prepare receivers
      for (auto& hub : hubs) {
        auto id = hub->id();
        hub->on_receive_unreliable.connect(
          [&, id](club::hub::node, const_buffer b) {
            auto pair = receivers.insert(std::make_pair(id, 0));

            ++pair.first->second;

            // The one that broadcasts it doesn't receive it.
            if (receivers.size() == hubs.size() - 1) {
              hubs.clear();
            }
          });
      }

      // One of them shall broadcast.
      size_t sender = rand() % hubs.size();

      binary::dynamic_encoder<char> e;
      e.put<uint32_t>(sender);

      hubs[sender]->unreliable_broadcast(e.move_data(), [](){});
    });

  ios.run();

  // Test that we're not sending more than we need.
  for (const auto& pair : receivers) {
    BOOST_REQUIRE_EQUAL(pair.second, 1);
  }
}

// -------------------------------------------------------------------
