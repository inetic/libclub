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
#include "vector_clock.h"

using std::cout;
using club::VClockT;

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(vclock) {
  using Clock = VClockT<uint32_t>;

  {
    Clock vc1;
    Clock vc2;

    BOOST_CHECK(!(vc1 >> vc2));
    BOOST_CHECK(!(vc2 >> vc1));
    BOOST_CHECK(vc1 >>= vc2);
    BOOST_CHECK(vc2 >>= vc1);
  }

  {
    Clock vc1;
    Clock vc2;

    vc1[0] = 1;

    vc2[0] = 1;

    BOOST_CHECK(!(vc1 >> vc2));
    BOOST_CHECK(!(vc2 >> vc1));
    BOOST_CHECK(vc1 >>= vc2);
    BOOST_CHECK(vc2 >>= vc1);
  }

  {
    Clock vc1;
    Clock vc2;

    vc1[0] = 1;

    vc2[0] = 1;
    vc2[1] = 2;

    BOOST_CHECK(vc1 >> vc2);
    BOOST_CHECK(vc1 >>= vc2);

    BOOST_CHECK(!(vc2 >> vc1));
    BOOST_CHECK(!(vc2 >>= vc1));
  }
}

// -------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(vclock_ord) {
  using Clock = VClockT<uint32_t>;

  {
    Clock c1;
    Clock c2;

    c1[0] = 5; c2[0] = 8;

    BOOST_CHECK(c1 < c2);
  }

  {
    Clock c1;
    Clock c2;

    c1[0] = 8; c2[0] = 5;

    BOOST_CHECK(!(c1 < c2));
  }

  {
    Clock c1;
    Clock c2;

    c1[0] = 5; c2[0] = 8;
    c1[1] = 7; c2[1] = 6;

    BOOST_CHECK(c2.is_smaller_numericaly(c1));
    BOOST_CHECK(c2 < c1);
  }

  {
    Clock c1;
    Clock c2;

    c1[0] = 5; c2[0] = 8;
    c1[1] = 7; c2[1] = 6;
               c2[2] = 4;

    BOOST_CHECK(c1 < c2);
  }
}

// -------------------------------------------------------------------
