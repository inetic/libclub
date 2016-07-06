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

#ifndef CLUB_OVERLOAD_H
#define CLUB_OVERLOAD_H

// The overload function can be used to take several function objects with the
// same return type and turn them into single polymorphic function object.
//
// Example:
//
// auto f = overload( [](bool  b) { return "bool";  }
//                  , [](int   i) { return "int";   }
//                  , [](float f) { return "float"; });
//
// f(true) // returns "bool"
// f(1)    // returns "int"
// f(1.0f) // returns "float"
//

namespace detail {

template<typename...>
struct Overloader;

template<typename F>
struct Overloader<F> : public F {
  Overloader(const F& f) : F(f) {}

  using F::operator ();
};

template<typename F0, typename... Fs>
struct Overloader<F0, Fs...> : public F0, public Overloader<Fs...>
{
  Overloader(const F0& f0, const Fs&... fs) 
    : F0(f0)
    , Overloader<Fs...>(fs...) 
  {}

  using Overloader<Fs...>::operator ();
  using F0::operator ();
};

} // namespace detail

template<typename... F>
detail::Overloader<F...> overload(const F&... f) {
  return detail::Overloader<F...>(f...);
}

#endif // CLUB_OVERLOAD_H
