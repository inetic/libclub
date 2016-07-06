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

#ifndef CLUB_ITERATOR_TOOLS_H
#define CLUB_ITERATOR_TOOLS_H

#include <iterator>

// Dereference pair of iterators.
template<typename I0, typename I1>
std::pair<typename std::iterator_traits<I0>::value_type,
          typename std::iterator_traits<I1>::value_type>
dereference(const std::pair<I0, I1>& p) {
  return std::make_pair(*p.first, *p.second);
}

////////////////////////////////////////////////////////////////////////////////
// Test if I is const iterator.
template<typename I>
constexpr bool is_const_iterator() {
  using D = decltype(*(I()));
  using T = typename std::remove_reference<D>::type;
  return std::is_const<T>::value;
}

////////////////////////////////////////////////////////////////////////////////
namespace detail {

template<typename I, typename Enable = void> struct map_iterator;


template<typename I>
struct map_iterator<I, typename std::enable_if<is_const_iterator<I>()>::type> {
  typedef typename I::value_type::second_type value_type;
  typedef const value_type*                   pointer;
  typedef const value_type&                   reference;
};

template<typename I>
struct map_iterator<I, typename std::enable_if<!is_const_iterator<I>()>::type> {
  typedef typename I::value_type::second_type value_type;
  typedef value_type*                         pointer;
  typedef value_type&                         reference;
};

}

////////////////////////////////////////////////////////////////////////////////
// Iterator adapter that adapts a std::map iterator to an iterator of its
// values.
template<typename I>
class MapValueIterator :
  public std::iterator< typename I::iterator_category
                      , typename detail::map_iterator<I>::value_type
                      , typename I::difference_type
                      , typename detail::map_iterator<I>::pointer
                      , typename detail::map_iterator<I>::reference>
{
public:
  MapValueIterator(I i)
    : _delegate(i)
  {}

  typename MapValueIterator<I>::reference operator * () const {
    return _delegate->second;
  }

  typename MapValueIterator<I>::pointer operator -> () const {
    return &_delegate->second;
  }

  MapValueIterator<I>& operator ++ () {
    ++_delegate;
    return *this;
  }

  MapValueIterator<I> operator ++ (int) {
    auto i = *this;
    ++(*this);
    return i;
  }

  bool operator == (MapValueIterator<I> i) const {
    return _delegate == i._delegate;
  }

  bool operator != (MapValueIterator<I> i) const {
    return _delegate != i._delegate;
  }

private:

  I _delegate;
};

////////////////////////////////////////////////////////////////////////////////
// Adapts an iterator whose value_type is a pointer (or a smart pointer, etc...)
// to an interator whose value_type is that pointer dereferenced.
template<typename I>
class DereferenceIterator :
  public std::iterator< typename I::iterator_category
                      , typename std::remove_reference<decltype(**std::declval<I>())>::type
                      , typename I::difference_type
                      , typename I::value_type>
{
public:
  DereferenceIterator(I i)
    : _delegate(i)
  {}

  typename DereferenceIterator<I>::reference operator * () const {
    return **_delegate;
  }

  typename DereferenceIterator<I>::pointer operator -> () const {
    return *_delegate;
  }

  DereferenceIterator<I>& operator ++ () {
    ++_delegate;
    return *this;
  }

  DereferenceIterator<I> operator ++ (int) {
    auto i = *this;
    ++(*this);
    return i;
  }

  bool operator == (DereferenceIterator<I> i) const {
    return _delegate == i._delegate;
  }

  bool operator != (DereferenceIterator<I> i) const {
    return _delegate != i._delegate;
  }

private:

  I _delegate;
};

#endif // CLUB_ITERATOR_TOOLS_H
