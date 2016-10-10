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
#include <set>
#include <club/debug/string_tools.h>
#include <set_operations.h>

using std::move;
using std::vector;
using std::set;
using std::cout;
using std::endl;
using club::str;

//------------------------------------------------------------------------------
template<class Range>
vector<int> vec(const Range& r) {
  vector<int> retval;
  for (auto v : r) retval.push_back(v);
  return retval;
}

//------------------------------------------------------------------------------
void s_(std::set<int>&) {}

template<class... Args>
void s_(std::set<int>& set, int arg, Args... args) {
  set.insert(arg);
  s_(set, args...);
}

template<class... Args>
set<int> s(Args... args) {
  set<int> retval;
  s_(retval, args...);
  return retval;
}

//------------------------------------------------------------------------------
namespace std {
std::ostream& operator<<(std::ostream& os, const vector<int>& v) {
  return os << str(v);
}
std::ostream& operator<<(std::ostream& os, const set<int>& v) {
  return os << "{" << str(v) << "}";
}
template<class S1, class S2>
std::ostream& operator<<(std::ostream& os, const seto::intersection_t<S1, S2>& r) {
  for (auto i : r) {
    os << i << ',';
  }
  return os;
}
} // std namespace

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_set_operations) {
  BOOST_REQUIRE_EQUAL(seto::intersection(s(1,2,3), s(4,5,6)), s());
  BOOST_REQUIRE_EQUAL(seto::intersection(s(4,5,6), s(1,2,3)), s());

  BOOST_REQUIRE_EQUAL(seto::intersection(s(1,2,3), s(2,3,4)), s(2,3));
  BOOST_REQUIRE_EQUAL(seto::intersection(s(2,3,4), s(1,2,3)), s(2,3));

  BOOST_REQUIRE_EQUAL(seto::intersection(s(1,2,3), s(2)), s(2));
  BOOST_REQUIRE_EQUAL(seto::intersection(s(2), s(1,2,3)), s(2));

  auto intr = [](auto a, auto b) { return seto::intersection(move(a), move(b)); };

  BOOST_REQUIRE_EQUAL( intr( intr(s(1,2,3,4,5), s(2,3,4,5,6))
                           , s(1,2,3,4,5,6))
                     , s(2,3,4,5));

  BOOST_REQUIRE_EQUAL( intr( s(1,2,3,4,5)
                           , intr(s(2,3,4,5,6), s(1,2,3,4,5,6)))
                     , s(2,3,4,5));

  BOOST_REQUIRE_EQUAL( seto::intersection(s(1,2,3,4,5), s(2,3,4,5,6), s(1,2,3,4,5,6))
                     , s(2,3,4,5));

  BOOST_REQUIRE_EQUAL(seto::intersection(s(1,2,3,4,5), s(2,4,6)), s(2,4));
  BOOST_REQUIRE_EQUAL(seto::intersection(s(2,4,6), s(1,2,3,4,5)), s(2,4));

  {
    using std::ref;
    auto s1 = s(1,2,3);
    auto s2 = s(2,3,4);
    BOOST_REQUIRE_EQUAL(seto::intersection(ref(s1), ref(s2)), s(2,3));
    BOOST_REQUIRE_EQUAL(seto::intersection(ref(s2), ref(s1)), s(2,3));
  }
}
