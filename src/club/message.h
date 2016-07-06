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

#ifndef CLUB_MESSAGE_H
#define CLUB_MESSAGE_H

#include <iostream>
#include <boost/uuid/uuid.hpp>
#include <binary/encoder.h>
#include <binary/decoder.h>

#include "binary/serialize/uuid.h"
#include "binary/serialize/vector.h"
#include "binary/serialize/set.h"
#include "binary/serialize/flat_set.h"
#include "binary/serialize/string.h"
#include "binary/serialize/pair.h"
#include "club/uuid.h"
#include "club/message_id.h"
#include "serialize/net.h"
#include "serialize/message_id.h"

#include "generic/variant_tools.h"

#include "debug/string_tools.h"
#include "club/debug/ostream_uuid.h"

namespace club {

// These are sanitization maximas, real values shall be much smaller.
static const size_t MAX_NODE_COUNT    = 1024;
static const size_t MAX_DATAGRAM_SIZE = 5*1024*1024;

enum MessageType
      { fuse
      , sync
      , port_offer
      , user_data
      , ack // NOTE: Must be last for the below decoding to work.
      };

template<typename Encoder>
inline void encode(Encoder& e, club::MessageType t) {
  e.put(static_cast<uint8_t>(t));
}

inline void decode(binary::decoder& d, club::MessageType& t) {
  auto c = d.get<uint8_t>(); 
  t = static_cast<club::MessageType>(c);
  if (t > club::ack) d.set_error();
}

inline
std::ostream& operator<<(std::ostream& os, MessageType mt) {
  switch (mt) {
    case fuse:            os << "fuse";             break;
    case sync:            os << "sync";             break;
    case port_offer:      os << "port_offer";       break;
    case user_data:       os << "user_data";        break;
    case ack:             os << "ack";              break;
  }
  return os;
}

//------------------------------------------------------------------------------
struct Header {
  uuid                             original_poster;
  TimeStamp                        time_stamp;
  MessageId                        config_id;
  boost::container::flat_set<uuid> visited;
};

template<typename Encoder>
inline void encode(Encoder& e, const club::Header& msg) {
    e.template put(msg.original_poster);
    e.template put(msg.time_stamp);
    e.template put(msg.config_id);
    e.template put(msg.visited);
}

inline void decode(binary::decoder& d, club::Header& msg) {
  msg.original_poster   = d.get<uuid>();
  msg.time_stamp        = d.get<decltype(msg.time_stamp)>();
  msg.config_id         = d.get<decltype(msg.config_id)>();
  msg.visited           = d.get<decltype(msg.visited)>(MAX_NODE_COUNT);
}

inline std::ostream& operator<<(std::ostream& os, const Header& h) {
  os << "OP:" << h.original_poster << ":" << h.time_stamp
     << " C:" << h.config_id;
  return os;
}

//------------------------------------------------------------------------------
struct AckData {
  MessageId                        acked_message_id;
  MessageId                        prev_message_id;
  boost::container::flat_set<uuid> local_quorum;
};

template<typename Encoder>
inline void encode(Encoder& e, const AckData& msg) {
  e.template put(msg.acked_message_id);
  e.template put(msg.prev_message_id);
  e.template put(msg.local_quorum);
}

inline void decode(binary::decoder& d, AckData& msg) {
  msg.acked_message_id = d.get<MessageId>();
  msg.prev_message_id  = d.get<MessageId>();
  msg.local_quorum     = d.get<decltype(msg.local_quorum)>(MAX_NODE_COUNT);
}

//------------------------------------------------------------------------------
struct Sync {
  static MessageType type() { return sync; }
  static bool always_ack()   { return false; }

  Header header;
  uuid   with; // The other node with whom the original_poster is syncing.

  Sync() {}
  Sync(Header header, uuid with)
    : header(std::move(header))
    , with(with)
  {}
};

template<typename Encoder>
inline void encode(Encoder& e, const club::Sync& msg) {
    e.template put(msg.header);
    e.template put(msg.with);
}

inline void decode(binary::decoder& d, club::Sync& msg) {
  msg.header = d.get<Header>();
  msg.with   = d.get<uuid>();
}

inline std::ostream& operator<<(std::ostream& os, const Sync& msg) {
  os << "(Sync " << msg.header
     << " With:" << msg.with
     << ")";
  return os;
}

//------------------------------------------------------------------------------
struct Fuse {
  static MessageType type() { return fuse; }
  static bool always_ack()  { return false; }

  Header header;
  AckData ack_data;
  uuid   with; // The other node with whom the original_poster is fusing.

  Fuse(const Fuse&) = delete;
  Fuse(Fuse&&)      = default;
  Fuse& operator=(const Fuse&) = delete;
  Fuse& operator=(Fuse&&)      = default;

  Fuse() {}
  Fuse(Header header, AckData ack_data, uuid with)
    : header(std::move(header))
    , ack_data(std::move(ack_data))
    , with(with)
  {}
};

template<typename Encoder>
inline void encode(Encoder& e, const club::Fuse& msg) {
    e.template put(msg.header);
    e.template put(msg.ack_data);
    e.template put(msg.with);
}

inline void decode(binary::decoder& d, club::Fuse& msg) {
  msg.header   = d.get<Header>();
  msg.ack_data = d.get<decltype(msg.ack_data)>();
  msg.with     = d.get<uuid>();
}

inline std::ostream& operator<<(std::ostream& os, const Fuse& msg) {
  os << "(Fuse " << msg.header
     << " With:" << msg.with
     << " | Ack " << msg.ack_data.acked_message_id
           << " " << msg.ack_data.prev_message_id
     << ")";
  return os;
}

//------------------------------------------------------------------------------
struct PortOffer {
  Header            header;
  uuid              addressor;
  unsigned short    internal_port;
  unsigned short    external_port;

  static MessageType type()      { return port_offer; }
  static bool        always_ack() { return true; }

  PortOffer() {}

  PortOffer( Header header
           , uuid addressor
           , unsigned short internal_port
           , unsigned short external_port)
    : header(std::move(header))
    , addressor(addressor)
    , internal_port(internal_port)
    , external_port(external_port)
  {}
};

template<typename Encoder>
static void encode(Encoder& e, const club::PortOffer& msg) {
  e.template put(msg.header);
  e.template put(msg.addressor);
  e.template put(msg.internal_port);
  e.template put(msg.external_port);
}

inline void decode(binary::decoder& d, club::PortOffer& msg) {
  msg.header        = d.get<Header>();
  msg.addressor     = d.get<uuid>();
  msg.internal_port = d.get<unsigned short>();
  msg.external_port = d.get<unsigned short>();
}

inline std::ostream& operator<<(std::ostream& os, const PortOffer& msg) {
  return os << "(PortOffer " << msg.header << " -> "
            << msg.addressor << " " << msg.internal_port
            << "/" << msg.external_port
            << ")";
}

//------------------------------------------------------------------------------
struct UserData {
  Header            header;
  AckData           ack_data;
  std::vector<char> data;

  static MessageType type()      { return user_data; }
  static bool        always_ack() { return true; }

  UserData(const UserData&) = delete;
  UserData(UserData&&)      = default;
  UserData& operator=(const UserData&) = delete;
  UserData& operator=(UserData&&)      = default;

  UserData() {}

  UserData( Header header
          , AckData ack_data
          , const std::vector<char>& data)
    : header(std::move(header))
    , ack_data(std::move(ack_data))
    , data(data)
  {}
};

template<typename Encoder>
inline void encode(Encoder& e, const club::UserData& msg) {
  e.template put(msg.header);
  e.template put(msg.ack_data);
  e.template put(msg.data);
}

inline void decode(binary::decoder& d, club::UserData& msg) {
  msg.header   = d.get<Header>();
  msg.ack_data = d.get<decltype(msg.ack_data)>();
  msg.data     = d.get<std::vector<char>>(MAX_DATAGRAM_SIZE);
}

inline std::ostream& operator<<(std::ostream& os, const UserData& msg) {
  return os << "(UserData " << msg.header
            << " |data| = " << msg.data.size()
            << ")";
}

//------------------------------------------------------------------------------
struct Ack {
  Header         header;
  AckData        ack_data;

  static MessageType type()       { return ack; }
  static bool        always_ack() { return false; }

  Ack(const Ack&) = delete;
  Ack(Ack&&)      = default;
  Ack& operator=(const Ack&) = delete;
  Ack& operator=(Ack&&)      = default;

  Ack() {}

  Ack( Header                             header
     , MessageId                          acked_message_id
     , MessageId                          prev_message_id
     , boost::container::flat_set<uuid>&& local_quorum)
    : header(std::move(header))
    , ack_data{ std::move(acked_message_id)
              , std::move(prev_message_id)
              , std::move(local_quorum) }
  {
    ASSERT(this->ack_data.prev_message_id < this->ack_data.acked_message_id);
  }
};

template<typename Encoder>
inline void encode(Encoder& e, const club::Ack& msg) {
  ASSERT(msg.ack_data.prev_message_id < msg.ack_data.acked_message_id);

  e.template put(msg.header);
  e.template put(msg.ack_data);
}

inline void decode(binary::decoder& d, club::Ack& msg) {
  msg.header   = d.get<Header>();
  msg.ack_data = d.get<AckData>();

  if (!(msg.ack_data.prev_message_id < msg.ack_data.acked_message_id)) {
    ASSERT(0);
    d.set_error();
  }
}

inline std::ostream& operator<<(std::ostream& os, const Ack& msg) {
  return os << "(Ack " << msg.header
            << " Of:" << msg.ack_data.acked_message_id
            << " Prev:" << msg.ack_data.prev_message_id << ")"
            << " LQ:{" << str_from_range(msg.ack_data.local_quorum) << "}"
            << ")";
}

//------------------------------------------------------------------------------
template<class M> const uuid& original_poster(const M& msg) {
  return msg.header.original_poster;
}

template<class M> MessageId message_id(const M& msg) {
  return MessageId{msg.header.time_stamp, msg.header.original_poster};
}

template<class M> const MessageId& config_id(const M& msg) {
  return msg.header.config_id;
}

//------------------------------------------------------------------------------
using LogMessage = boost::variant< Fuse
                                 , UserData >;

inline
MessageType message_type(const LogMessage& message) {
  return match(message, [](const Fuse&)           { return fuse; }
                      , [](const UserData&)       { return user_data; });
}

inline
uuid original_poster(const LogMessage& message) {
  return match( message
              , [](const Fuse& m)           { return original_poster(m); }
              , [](const UserData& m)       { return original_poster(m); });
}

inline
MessageId message_id(const LogMessage& message) {
  MessageId ret;
  match( message
       , [&](const Fuse& m)           { ret = message_id(m); }
       , [&](const UserData& m)       { ret = message_id(m); });
  return ret;
}

inline
MessageId config_id(const LogMessage& message) {
  MessageId ret;
  match( message
       , [&](const Fuse& m)           { ret = config_id(m); }
       , [&](const UserData& m)       { ret = config_id(m); });
  return ret;
}

inline
AckData& ack_data(LogMessage& message) {
  AckData* ret = nullptr;
  match( message
       , [&](const Fuse& m)      { ret = &const_cast<AckData&>(m.ack_data); }
       , [&](const UserData& m)  { ret = &const_cast<AckData&>(m.ack_data); });
  return *ret;
}

//------------------------------------------------------------------------------

} // club namespace

#endif // ifndef CLUB_MESSAGE_H
