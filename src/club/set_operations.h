#ifndef SETO_SET_OPERATIONS_
#define SETO_SET_OPERATIONS_

#include <iterator>

namespace seto {

namespace detail {
  template<class S> struct get_value_type
  { using type = typename S::value_type; };

  template<class S> struct get_const_iterator
  { using type = typename S::const_iterator; };

  template<class S> typename S::const_iterator
    begin(const S& s) { return s.begin(); }

  template<class S> typename S::const_iterator
    end(const S& s) { return s.end(); }

  // Specializations of the above for std::reference_wrapper.
  template<class S> struct get_value_type<std::reference_wrapper<S>>
  { using type = typename S::value_type; };

  template<class S> struct get_const_iterator<std::reference_wrapper<S>>
  { using type = typename S::const_iterator; };

  template<class S> typename S::const_iterator
    begin(std::reference_wrapper<S> s) { return s.get().begin(); }

  template<class S> typename S::const_iterator
    end(std::reference_wrapper<S> s) { return s.get().end(); }
} // detail namespace

template<class S1, class S2>
class intersection_t {
public:
  using value_type           = typename detail::get_value_type<S1>::type;
  using const_iterator_type1 = typename detail::get_const_iterator<S1>::type;
  using const_iterator_type2 = typename detail::get_const_iterator<S2>::type;

public:
  class const_iterator : public std::iterator
                           < std::input_iterator_tag
                           , value_type> {
    public:
      const_iterator( const_iterator_type1 first1_
                    , const_iterator_type1 last1_
                    , const_iterator_type2 first2_
                    , const_iterator_type2 last2_)
        : first1(first1_), last1(last1_)
        , first2(first2_), last2(last2_)
      {
        search();
      }

      bool operator==(const_iterator other) const {
        if (is_end()) {
          return other.is_end();
        }
        else if (other.is_end()) {
          return false;
        }

        return first1 == other.first1 && first2 == other.first2;
      }

      bool operator!=(const_iterator other) const {
        return !(*this == other);
      }

      const value_type& operator*() const {
        assert(!is_end());
        return *first1;
      }

      const value_type* operator->() const {
        assert(!is_end());
        return &(*first1);
      }

      const_iterator& operator++() { // Prefix
        assert(!is_end());
        ++first1;
        ++first2;
        search();
        return *this;
      }

      const_iterator operator++(int) { // Postfix
        assert(!is_end());
        const_iterator out(*this);
        ++(*this);
        return out;
      }

    private:
      bool is_end() const { return first1 == last1 || first2 == last2; }

      void search() {
        while (!is_end()) {
          if (*first1 < *first2) {
            ++first1;
          } else  {
            if (!(*first2 < *first1)) {
              return;
            }
            ++first2;
          }
        }
      }

    private:
      const_iterator_type1 first1, last1;
      const_iterator_type2 first2, last2;
  };

public:
  template<class T1, class T2>
  intersection_t(T1&& s1, T2&& s2)
    : s1(std::forward<T1>(s1)), s2(std::forward<T2>(s2)) {}

  const_iterator begin() const {
    return const_iterator( detail::begin(s1), detail::end(s1)
                         , detail::begin(s2), detail::end(s2));
  }

  const_iterator end() const {
    return const_iterator( detail::end(s1), detail::end(s1)
                         , detail::end(s2), detail::end(s2));
  }

  template<class S3>
  bool operator == (const S3& s) const {
    return std::equal(begin(), end(), s.begin(), s.end());
  }

private:
  S1 s1;
  S2 s2;
};

// -----------------------------------------------------------------------------
// Detail namespace
namespace detail {

template<class...> struct intersections;

template<class S> struct intersections<S> {
  using type = S;
};

template<class First, class Second>
struct intersections<First, Second> {
  using type = intersection_t<First, Second>;
};

template<class First, class... Rest>
struct intersections<First, Rest...> {
  using type = intersection_t< First
                             , typename intersections<Rest...>::type>;
};

template<class... Ts>
using intersections_t = typename intersections<Ts...>::type;

} // detail namespace
// -----------------------------------------------------------------------------

template<class S>
inline S&& intersection(S&& s) {
  return std::forward<S>(s);
}

/// Make an intersection of sets passed as arguments to this function.
/// The only requirement of the set is that it implements an ordered
/// sequence.
/// The result is again an ordered sequence.
///
/// Example:
///
/// std::map<int, std::string> my_map{ std::make_pair(0, "zero")
///                                  , std::make_pair(2, "two")
///                                  , std::make_pair(5, "five") };
///
/// auto result = intersection( std::set<int>{1,2,3}
///                           , std::vector<int>{2,3}
///                           , my_map | boost::adaptors::map_keys);
///
/// auto expected = std::list<int>{2};
///
/// assert(std::equal( result.begin(), result.end()
///                  , expected.begin(), expected.end()));
template<class S1, class... Ss>
inline
intersection_t<std::decay_t<S1>, detail::intersections_t<std::decay_t<Ss>...>>
intersection(S1&& s1, Ss&&... ss) {
  return intersection_t< std::decay_t<S1>
                       , detail::intersections_t<std::decay_t<Ss>...>>
                          ( std::forward<S1>(s1)
                          , intersection(std::forward<Ss>(ss)...));
}

}

#endif // ifndef SETO_SET_OPERATIONS_
