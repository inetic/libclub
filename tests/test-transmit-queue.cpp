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
#include <club/generic/cyclic_queue.h>

using std::cout;
using std::endl;
using std::move;

using CQ  = club::CyclicQueue<int>;

BOOST_AUTO_TEST_CASE(test_transmit_queue) {
  {
    CQ cq;

    int d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, cq.size());
  }

  {
    CQ cq;

    cq.insert(0);

    int d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, cq.size());
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);

    int d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, cq.size());
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);

    int d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, 5);
    d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
    }
    BOOST_REQUIRE_EQUAL(d, 5);
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);

    int d = 0;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++);
      if (d == 2) break;
    }
    --d;
    for (auto m : cq.cycle()) {
      BOOST_REQUIRE_EQUAL(m, d++ % 5);
    }

    BOOST_REQUIRE_EQUAL(d, 6);
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);
    cq.insert(5);

    auto cycle = cq.cycle();

    for (auto i = cycle.begin(); i != cycle.end();) {
      i.erase();
    }

    BOOST_REQUIRE(cq.empty());
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);
    cq.insert(5);

    auto cycle = cq.cycle();

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

    for (auto m : cq.cycle()) {
      result.push_back(m);
    }

    BOOST_REQUIRE(result == std::vector<int>({1,3,5}));
  }

  {
    CQ cq;

    cq.insert(0);
    cq.insert(1);
    cq.insert(2);
    cq.insert(3);
    cq.insert(4);
    cq.insert(5);

    auto cycle = cq.cycle();

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

    for (auto m : cq.cycle()) {
      result.push_back(m);
    }

    BOOST_REQUIRE(result == std::vector<int>({0,2,4}));
  }
}
