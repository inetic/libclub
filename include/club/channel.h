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

#ifndef CLUB_CHANNEL_H
#define CLUB_CHANNEL_H

namespace club {

class Channel {
  public:
  using Type = unsigned char;

  static const Type UNSET               = 0;
  static const Type DAT                 = 1;   // Data
  static const Type UNR                 = 2;   // Unreliable data, needs no ACK
  static const Type CLOSE               = 3;   // Socket is being closed.
  static const Type P2P_CONNECT_PRIVATE = 4;
  static const Type P2P_CONNECT_PUBLIC  = 5;
  static const Type RECONNECT           = 6;
  static const Type PUNCH               = 7;
  static const Type KEEP_ALIVE          = 8;
  static const Type PORT_ECHO_REQUEST   = 9;
  static const Type PORT_ECHO_RESPONSE  = 10;

  public:
  Channel()
    : _type(UNSET)
    , _is_internal(true) {}

  Channel(unsigned char type)
    : _type(type)
    , _is_internal(false) {}

  Channel(unsigned char type, bool is_internal)
    : _type(type)
    , _is_internal(is_internal) {}

  bool operator<(const Channel& c) const {
    return _is_internal < c._is_internal
        || (!(_is_internal > c._is_internal) && (_type < c._type));
  }

  bool operator==(const Channel& c) const {
    return _is_internal == c._is_internal && _type == c._type;
  }

  bool operator!=(const Channel& c) const { return !(*this == c); }

  unsigned char type() const { return _type; }
  void          type(unsigned char type) { _type = type; }

  bool is_internal() const { return _is_internal; }
  void is_internal(bool b) { _is_internal = b; }

  static std::string internal_type_to_string(unsigned char type) {
    switch (type) {
      case UNSET:               return "UNSET";
      case DAT:                 return "DAT";
      case UNR:                 return "UNR";
      case CLOSE:               return "CLOSE";
      case P2P_CONNECT_PRIVATE: return "P2P_CONNECT_PRIVATE";
      case P2P_CONNECT_PUBLIC:  return "P2P_CONNECT_PUBLIC";
      case RECONNECT:           return "RECONNECT";
      case PUNCH:               return "PUNCH";
      case KEEP_ALIVE:          return "KEEP_ALIVE";
      case PORT_ECHO_REQUEST:   return "KEEP_PORT_ECHO_REQUEST";
      case PORT_ECHO_RESPONSE:  return "KEEP_PORT_ECHO_RESPONSE";
      default: { std::stringstream ss;
                 ss << "?" << (unsigned int) type << "?";
                 return ss.str(); }
    }
  }

  private:
  unsigned char _type;
  bool          _is_internal; // Is channel for internal purposes of sockets.
};

inline std::ostream& operator<<(std::ostream& os, const Channel& c)
{
  if (c.is_internal()) {
    return os << "Channel " << Channel::internal_type_to_string(c.type());
  }
  else {
    return os << "Channel EXT:" << (unsigned int) c.type();
  }
}

static inline Channel CHANNEL_DAT()                 { return Channel(Channel::DAT, true); }
static inline Channel CHANNEL_UNR()                 { return Channel(Channel::UNR, true); }
static inline Channel CHANNEL_CLOSE()               { return Channel(Channel::CLOSE, true); }
static inline Channel CHANNEL_P2P_CONNECT_PRIVATE() { return Channel(Channel::P2P_CONNECT_PRIVATE, true); }
static inline Channel CHANNEL_P2P_CONNECT_PUBLIC()  { return Channel(Channel::P2P_CONNECT_PUBLIC, true); }
static inline Channel CHANNEL_RECONNECT()           { return Channel(Channel::RECONNECT, true); }
static inline Channel CHANNEL_PUNCH()               { return Channel(Channel::PUNCH, true); }
static inline Channel CHANNEL_KEEP_ALIVE()          { return Channel(Channel::KEEP_ALIVE, true); }

} // club namespace

#endif // ifndef CLUB_CHANNEL_H
