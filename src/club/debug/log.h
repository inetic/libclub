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

#ifndef __LOG_H__
#define __LOG_H__

// Logging.
// #ifndef NDEBUG
#include "log_output.h"
#include "string_tools.h"

template<typename... Ts>
void log(const Ts&... args) {
  log(str(args...));
}

// #else
// template<typename... Ts>
// void log(const Ts&... args) { }
// #endif // ifndef NDEBUG

#endif // __LOG_H__
