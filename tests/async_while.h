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

#ifndef __ASYNC_WHILE_H__
#define __ASYNC_WHILE_H__

using Cont = std::function<void()>;

template<class Handler> void async_while_(unsigned int i, Handler h) {
  auto j = i + 1;
  h(i, [h, j]() { async_while_(j, std::move(h)); });
}

template<class Handler> void async_while(Handler h) {
  async_while_(0, std::move(h));
}

#endif // ifndef __ASYNC_WHILE_H__
