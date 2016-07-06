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

#ifndef CLUB_ERASE_IF_H
#define CLUB_ERASE_IF_H

namespace club {

template<class T, class F> void erase_if(T& collection, const F& f) {
  using I = typename T::iterator;

  I j = collection.begin();
  for (I i = collection.begin(); i != collection.end(); i = j) {
    ++j;
    if (f(*i)) {
      collection.erase(i);
    }
  }
}

} // club namespace

#endif // ifndef CLUB_ERASE_IF_H
