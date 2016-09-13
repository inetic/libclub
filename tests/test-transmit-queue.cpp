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

#include <club/debug/string_tools.h>
#include <iostream>
#include <club/transport/transmit_queue.h>

using std::cout;
using std::endl;
using std::move;

using TQ  = club::transport::TransmitQueue<int>;

BOOST_AUTO_TEST_CASE(test_transmit_queue) {
  {
    TQ tq;

    int d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, tq.size());
  }

  {
    TQ tq;

    tq.insert(0);

    int d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, tq.size());
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);

    int d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, tq.size());
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);

    int d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, 5);
    d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, 5);
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);

    int d = 0;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
      if (d == 2) break;
    }
    --d;
    for (auto m : tq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++ % 5);
    }

    BOOST_REQUIRE_EQUAL(d, 6);
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);
    tq.insert(5);

    auto cycle = tq.cycle();

    for (auto i = cycle.begin(); i != cycle.end();) {
      i.erase();
    }

    BOOST_REQUIRE(tq.empty());
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);
    tq.insert(5);

    auto cycle = tq.cycle();

    size_t k = 0;
    for (auto i = cycle.begin(); i != cycle.end();) {
      if (k++ % 2 == 0) {
        i.erase();
      }
      else {
        ++i;
      }
    }

    std::vector<int> result;

    for (auto m : tq.cycle()) {
      result.push_back(m);
    }

    BOOST_REQUIRE(result == std::vector<int>({1,3,5}));
  }

  {
    TQ tq;

    tq.insert(0);
    tq.insert(1);
    tq.insert(2);
    tq.insert(3);
    tq.insert(4);
    tq.insert(5);

    auto cycle = tq.cycle();

    size_t k = 0;
    for (auto i = cycle.begin(); i != cycle.end();) {
      if (++k % 2 == 0) {
        i.erase();
      }
      else {
        ++i;
      }
    }

    std::vector<int> result;

    for (auto m : tq.cycle()) {
      result.push_back(m);
    }

    BOOST_REQUIRE(result == std::vector<int>({0,2,4}));
  }
}
