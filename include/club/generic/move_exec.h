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

#ifndef CLUB_MOVE_EXEC_H
#define CLUB_MOVE_EXEC_H

namespace club {

template<class Func, class... Params>
void move_exec(Func& func, Params&&... params) {
  auto f = std::move(func);
  return f(std::forward<Params>(params)...);
}

} // namespace

#endif // ifndef CLUB_MOVE_EXEC_H

