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
#include <transport/part_info.h>
#include <debug/string_tools.h>

using PartInfo = club::transport::PartInfo;
using std::vector;
using std::pair;
using std::cout;
using std::endl;

using PartV = std::vector<pair<size_t, size_t>>;

//------------------------------------------------------------------------------
PartV parts_to_vector(const PartInfo& parts) {
  PartV ret;
  for (auto p : parts) { ret.push_back(p); }
  return ret;
}

//------------------------------------------------------------------------------
namespace std {
template<class T>
std::ostream& operator<<(std::ostream& os, const vector<T>& v) {
  return os << "[" << str(v) << "]";
}
} // std namespace

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_part_info) {

  {
    PartInfo pi;
    BOOST_REQUIRE(pi.empty());
  }

  {
    PartInfo pi;
    pi.add_part(0, 0);
    pi.add_part(1, 1);
    BOOST_REQUIRE(pi.empty());
  }

  {
    PartInfo pi;
    pi.add_part(0, 1);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 1} }));
  }

  {
    //     0123
    // ----<-)----
    // -----<-)---
    PartInfo pi;
    pi.add_part(0, 1);
    pi.add_part(1, 2);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 2} }));
  }

  {
    //     0123
    // -----<-)---
    // ----<-)----
    PartInfo pi;
    pi.add_part(1, 2);
    pi.add_part(0, 1);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 2} }));
  }

  {
    //     0  3  6 8
    // -------<--)------
    // -----<------)----
    PartInfo pi;
    pi.add_part(3, 6);
    pi.add_part(1, 8);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {1, 8} }));
  }

  {
    //     0  3  6 8
    // -----<------)----
    // -------<--)------
    PartInfo pi;
    pi.add_part(1, 8);
    pi.add_part(3, 6);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {1, 8} }));
  }

  {
    //     0  3  6 8
    // -----<------)----
    // -------<--)------
    PartInfo pi;
    pi.add_part(0, 1);
    pi.add_part(2, 3);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 1}, {2, 3} }));
  }

  {
    //     01234567
    // ----<)--------
    // -----<)-------
    // ----<------)--
    PartInfo pi;
    pi.add_part(0, 1);
    pi.add_part(2, 3);
    pi.add_part(0, 7);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 7} }));
  }

  {
    PartInfo pi;
    pi.add_part(3, 7);
    pi.add_part(10, 20);
    pi.add_part(0, 30);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 30} }));
  }

  {
    PartInfo pi;
    pi.add_part(3, 7);
    pi.add_part(10, 20);
    pi.add_part(7, 30);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {3, 30} }));
  }

  {
    PartInfo pi;
    pi.add_part(0, 1000);
    pi.add_part(0, 91);
    BOOST_REQUIRE(!pi.empty());
    BOOST_REQUIRE_EQUAL(parts_to_vector(pi), PartV({ {0, 1000} }));
  }
}
