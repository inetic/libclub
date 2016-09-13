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

#ifndef CLUB_TRANSPORT_ERROR_H
#define CLUB_TRANSPORT_ERROR_H

#include <boost/asio/error.hpp>

namespace club { namespace transport {

enum class error {
  parse_error = 1,
  timed_out,
};

inline std::ostream& operator<<(std::ostream& os, error e) {
  return os << static_cast<int>(e);
}

class socket_error_category_impl : public boost::system::error_category {
public:
  const char* name() const noexcept override { return "transport::error"; }
  std::string message(int e) const {
    switch (static_cast<error>(e)) {
      case error::parse_error: return "Parse error";
      case error::timed_out: return "Timed out";
    }
    return "Unknown error";
  }
};

const boost::system::error_category& get_socket_error_category();
static const boost::system::error_category& socket_error_category
    = get_socket_error_category();

inline boost::system::error_code make_error_code(error e)
{
  return boost::system::error_code(
      static_cast<int>(e), socket_error_category);
}

}} // namespaces

namespace boost { namespace system {
  template<> struct is_error_code_enum<club::transport::error>
  {
    BOOST_STATIC_CONSTANT(bool, value = true);
  };
}} // namespaces

#endif // ifndef CLUB_TRANSPORT_ERROR_H

