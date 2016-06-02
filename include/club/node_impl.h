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

#ifndef __NODE_IMPL_H__
#define __NODE_IMPL_H__

namespace club {

//------------------------------------------------------------------------------
class node_impl {
  public:
    uuid id() const;

    bool operator < (const node_impl&) const;

  private:
    friend class hub;
    node_impl(uuid);

  private:
    uuid _id;
};

//------------------------------------------------------------------------------
inline
node_impl::node_impl(uuid id) : _id(id) {}

//------------------------------------------------------------------------------
inline
bool node_impl::operator < (const node_impl& other) const {
  return _id < other._id;
}

//------------------------------------------------------------------------------
inline
uuid node_impl::id() const {
  return _id;
}

//------------------------------------------------------------------------------

} // club namespace


#endif // ifndef __NODE_IMPL_H__
