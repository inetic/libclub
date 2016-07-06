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

#ifndef CLUB_VARIANT_TOOLS_H
#define CLUB_VARIANT_TOOLS_H

#include <boost/variant.hpp>
#include "overload.h"

////////////////////////////////////////////////////////////////////////////////
// This is apply_visitor from boost::variant, but this one accepts the
// variant object via const reference. This seems to be missing in the
// boost::variant library.
template<typename Visitor, typename Visitable>
typename Visitor::result_type apply_visitor(
  Visitor&          visitor,
  const Visitable&  visitable)
{
  return visitable.apply_visitor(visitor);
}

template<typename Visitor, typename Visitable>
typename Visitor::result_type apply_visitor(
  const Visitor&    visitor,
  const Visitable&  visitable)
{
  return visitable.apply_visitor(visitor);
}

////////////////////////////////////////////////////////////////////////////////
namespace detail {

// This struct wraps an Overloader and defines a result_type type that
// boost::apply_visitor needs.
template<typename V, typename... F>
struct MatchWrapper {
  typedef typename boost::mpl::front<typename V::types>::type
          first_variant_type;

  typedef Overloader<F...> function_type;

  typedef decltype(std::declval<function_type>()(std::declval<first_variant_type>()))
          result_type;

  function_type fun;

  MatchWrapper(F... f) : fun(f...) {}

  template<typename... T>
  result_type operator () (T&&... args) const {
    return fun(std::forward<T...>(args...));
  }
};

} // namespace detail

////////////////////////////////////////////////////////////////////////////////
// More conveniet variant visitation:
//
// variant<bool, int, float> x = 13;
//
// match(x, [](bool  b) { ... }
//        , [](int   i) { ... }
//        , [](float f) { ... });
//
// Note: all the lambdas must have the same return type.
//
template<typename V, typename... F>
typename detail::MatchWrapper<V, F...>::result_type
match(const V& variant, F... f) {
  using boost::apply_visitor;
  return apply_visitor(detail::MatchWrapper<V, F...>(f...), variant);
}

template<typename V, typename... F>
typename detail::MatchWrapper<V, F...>::result_type
match(V& variant, F... f) {
  using boost::apply_visitor;
  return apply_visitor(detail::MatchWrapper<V, F...>(f...), variant);
}


#endif // CLUB_VARIANT_TOOLS_H
