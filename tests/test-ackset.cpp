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
#include <transport/ack_set.h>
#include <debug/string_tools.h>

using AckSet = club::transport::AckSet;
using std::vector;
using std::cout;
using std::endl;

//------------------------------------------------------------------------------
vector<uint32_t> acks_to_vector(const AckSet& acks) {
  vector<uint32_t> ret;
  for (auto s : acks) { ret.push_back(s); }
  return ret;
}

//------------------------------------------------------------------------------
namespace std {
std::ostream& operator<<(std::ostream& os, const vector<uint32_t>& v) {
  return os << "[" << str(v) << "]";
}
} // std namespace

//------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(test_ack_set) {
  using Vec = vector<uint32_t>;

  {
    AckSet acks;
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec());
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({10}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE(acks.try_add(11));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({11, 10}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE(acks.try_add(20));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({20, 10}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(0));
    BOOST_REQUIRE(acks.try_add(31));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({31, 0}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(0));
    BOOST_REQUIRE(acks.try_add(32) == false);
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({0}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(0));
    BOOST_REQUIRE(acks.try_add(1));
    BOOST_REQUIRE(acks.try_add(32));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({32, 1}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(0));
    BOOST_REQUIRE(acks.try_add(1));
    BOOST_REQUIRE(acks.try_add(32));
    BOOST_REQUIRE(acks.try_add(2));
    BOOST_REQUIRE(acks.try_add(33));
    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), Vec({33, 32, 2}));
  }

  {
    AckSet acks;
    Vec vec;

    for (auto i = 0; i < 32; ++i) {
      BOOST_REQUIRE(acks.try_add(i));
      vec.push_back(31 - i);
    }

    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), vec);

    for (auto i = 0; i < 32; ++i) {
      BOOST_REQUIRE(acks.try_add(i));
    }

    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), vec);

    vec.clear();
    for (auto i = 32; i < 64; ++i) {
      BOOST_REQUIRE(acks.try_add(i));
      vec.push_back(63 + 32 - i);
    }

    BOOST_REQUIRE_EQUAL(acks_to_vector(acks), vec);
  }
}

BOOST_AUTO_TEST_CASE(test_ack_set_serialize) {
  using Vec = vector<uint32_t>;

  auto encode_decode = [](AckSet acks) {
    vector<uint8_t> data(binary::encoded<AckSet>::size());
    binary::encoder encoder(data);
    encoder.put(acks);
    binary::decoder decoder(data.data(), data.size());
    return decoder.get<AckSet>();
  };

  {
    AckSet acks;
    BOOST_REQUIRE(acks_to_vector(encode_decode(acks)) == Vec());
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE(acks_to_vector(encode_decode(acks)) == Vec({10}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE(acks.try_add(11));
    BOOST_REQUIRE(acks_to_vector(encode_decode(acks)) == Vec({11, 10}));
  }

  {
    AckSet acks;
    BOOST_REQUIRE(acks.try_add(10));
    BOOST_REQUIRE(acks.try_add(11));
    BOOST_REQUIRE(acks.try_add(11+31));
    BOOST_REQUIRE_EQUAL(acks_to_vector(encode_decode(acks)), Vec({11+31, 11}));
  }
}

