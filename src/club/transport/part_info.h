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

#ifndef CLUB_TRANSPORT_PART_INFO_H
#define CLUB_TRANSPORT_PART_INFO_H

#include <map>

namespace club { namespace transport {

class PartInfo {
  using Info = std::map<size_t, size_t>;

public:
  using const_iterator = Info::const_iterator;

public:
  bool empty() const { return _info.empty(); }

  void add_part(size_t start, size_t size);

  const_iterator begin() const { return _info.begin(); }
  const_iterator end()   const { return _info.end(); }

private:
  void sanitize(Info::iterator);

private:
  friend std::ostream& operator<<(std::ostream&, const PartInfo&);

  Info _info;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
inline void PartInfo::add_part(size_t start, size_t end) {
  if (end <= start) return;

  if (_info.empty()) {
    _info.emplace(start, end);
    return;
  }

  auto i = _info.lower_bound(start); // lower_bound: greater or equal

  if (i == _info.end()) i = --_info.end();
  else if (i != _info.begin()) --i;

  for (;i != _info.end(); ++i) {
    auto& i_begin = i->first;
    auto& i_end   = i->second;

    if (i_begin <= start && start <= i_end) {
      i_end = std::max(end, i_end);
      return sanitize(i);
    }
    if (start <= i_begin && i_begin <= end) {
      auto new_begin = start;
      auto new_end   = std::max(end, i_end);
      _info.erase(i);
      auto new_i = _info.emplace(new_begin, new_end).first;
      return sanitize(new_i);
    }
  }

  // No existing interval was modified.
  _info.emplace(start, end);
}

//------------------------------------------------------------------------------
inline void PartInfo::sanitize(Info::iterator i) {
  for (auto j = std::next(i); j != _info.end();) {
    if (i->second < j->first) break;
    i->second = std::max(i->second, j->second);
    j = _info.erase(j);
  }
}

//------------------------------------------------------------------------------
inline std::ostream& operator<<(std::ostream& os, const PartInfo& info) {
  if (info._info.empty()) {
    return os << "(PartInfo)";
  }

  os << "(PartInfo ";
  for (auto i = info._info.begin(); i != info._info.end(); ++i) {
    os << "(" << i->first << ", " << i->second << ")";
    if (i != --info._info.end()) {
      os << ", ";
    }
  }
  return os << ")";
}

//------------------------------------------------------------------------------
}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_PART_INFO_H

