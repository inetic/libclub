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

#ifndef CLUB_CYCLIC_QUEUE_H
#define CLUB_CYCLIC_QUEUE_H

#include <list>
#include <boost/variant.hpp>

namespace club {

template<class Message>
class CyclicQueue {
  // TODO: Use std::deque
  using Messages = std::list<Message>;
public:

  class Cycle;

  using Delegate = boost::variant< typename Messages::iterator
                                 , typename Messages::iterator* >;

  //----------------------------------------------------------------------------
  class iterator {
  public:
    iterator& operator++(); // Prefix

    bool operator==(const iterator&) const;
    bool operator!=(const iterator&) const;

    Message& operator*();
    Message* operator->();

    // This shall become std::next(*this)
    void erase();

  private:
    friend class Cycle;
    template<class I> iterator(I, Cycle&);

  private:
    Delegate delegate;
    Cycle& _cycle;
  };

  //----------------------------------------------------------------------------
  class Cycle {
  public:
    Cycle(CyclicQueue&);

    iterator begin();
    iterator end();
  private:
    friend class iterator;
    typename Messages::iterator _end;
    CyclicQueue& _queue;
  };

  //----------------------------------------------------------------------------
  Cycle cycle();

  void insert(Message m);
  template<typename... Ts> void emplace(Ts&&... params);
  bool empty() const;
  size_t size() const;

  const Messages& messages() const { return _messages; }

private:
  Messages _messages;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class M>
CyclicQueue<M>::Cycle::Cycle(CyclicQueue<M>& tq)
  : _end(tq._messages.end())
  , _queue(tq)
{ }

template<class M>
typename CyclicQueue<M>::iterator
CyclicQueue<M>::Cycle::begin() {
  return iterator(_queue._messages.begin(), *this);
}

template<class M>
typename CyclicQueue<M>::iterator
CyclicQueue<M>::Cycle::end() {
  return iterator(&_end, *this);
}

//------------------------------------------------------------------------------
template<class M>
template<class I>
CyclicQueue<M>::iterator::
iterator(I delegate , typename CyclicQueue<M>::Cycle& cycle)
  : delegate(delegate)
  , _cycle(cycle)
{ }

template<class M>
typename CyclicQueue<M>::iterator&
CyclicQueue<M>::iterator::operator++() {
  auto& list = _cycle._queue._messages;

  if (auto dp = boost::get<typename std::list<M>::iterator>(&delegate)) {
    auto old = *dp;
    ++(*dp);
    if (*dp == list.end()) return *this;

    list.splice(list.end(), list, old);

    if (_cycle._end == list.end()) {
      _cycle._end = old;
    }
  }

  return *this;
}

template<class M>
M& CyclicQueue<M>::iterator::operator*() {
  auto dp = boost::get<typename Messages::iterator>(&delegate);
  assert(dp);
  return **dp;
}

template<class M>
M* CyclicQueue<M>::iterator::operator->() {
  auto dp = boost::get<typename Messages::iterator>(&delegate);
  assert(dp);
  return &**dp;
}

template<class M>
bool
CyclicQueue<M>::iterator::operator==(const iterator& other) const {
  using I = typename CyclicQueue<M>::Messages::iterator;

  if (auto dp = boost::get<I>(&delegate)) {
    if (auto other_dp = boost::get<I>(&other.delegate)) {
      return *dp == *other_dp;
    }
    return *dp == **boost::get<I*>(&other.delegate);
  }

  auto dp = *boost::get<I*>(&delegate);
  
  if (auto other_dp = boost::get<I>(&other.delegate)) {
    return *dp == *other_dp;
  }

  return *dp == **boost::get<I*>(&other.delegate);
}

template<class M>
bool
CyclicQueue<M>::iterator::operator!=(const iterator& other) const {
  return !(*this == other);
}

template<class M>
void CyclicQueue<M>::iterator::erase() {
  using I = typename CyclicQueue<M>::Messages::iterator;
  auto i = *boost::get<I>(&delegate);
  delegate = _cycle._queue._messages.erase(i);
}

//------------------------------------------------------------------------------
template<class M>
typename CyclicQueue<M>::Cycle CyclicQueue<M>::cycle() {
  return Cycle(*this);
}

template<class M>
void CyclicQueue<M>::insert(M m) {
  _messages.push_back(std::move(m));
}

//------------------------------------------------------------------------------
template<class M>
template<typename... Ts>
void CyclicQueue<M>::emplace(Ts&&... params) {
  insert(M(std::forward<Ts>(params)...));
}

//------------------------------------------------------------------------------
template<class M>
bool CyclicQueue<M>::empty() const {
  return _messages.empty();
}

template<class M>
size_t CyclicQueue<M>::size() const {
  return _messages.size();
}

//------------------------------------------------------------------------------

}

#endif // ifndef CLUB_CYCLIC_QUEUE_H

