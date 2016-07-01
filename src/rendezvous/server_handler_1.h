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

#ifndef __RENDEZVOUS_SERVER_HANDLER_1_H__
#define __RENDEZVOUS_SERVER_HANDLER_1_H__

#include <boost/asio/steady_timer.hpp>
#include <boost/optional.hpp>
#include <binary/encoder.h>
#include <binary/serialize/ip.h>
#include <rendezvous/constants.h>
#include <async/alarm.h>
#include "options.h"
#include "reflector.h"

namespace rendezvous {

class server;

class server_handler_1 {
  using service_type = uint32_t;
  using udp = boost::asio::ip::udp;
  using Address = boost::asio::ip::address;

  struct State {
    boost::asio::io_service& ios;
    bool                     was_destroyed;

    State(boost::asio::io_service& ios)
      : ios(ios)
      , was_destroyed(false) {
    }
  };

  using StatePtr = std::shared_ptr<State>;
  using Constant = constants_v1;

  struct Service {
    service_type                  type;
    udp::endpoint                 internal_ep;
    uint16_t                      reflected_port;
    bool                          is_host;
    std::unique_ptr<async::alarm> dead_man_switch;
  };

  struct Entry {
    udp::endpoint            external_endpoint;
    udp::endpoint            internal_endpoint;
    Service&                 service;
    std::set<udp::endpoint>& service_endpoints;
  };

public:
  static const VersionType version = 1;

  server_handler_1(boost::asio::io_service&, const options&);
  ~server_handler_1();

  boost::asio::io_service& get_io_service() const;
  void handle(udp::endpoint from, binary::decoder, server&);

private:
  std::unique_ptr<async::alarm> make_dms(udp::endpoint);

  std::vector<uint8_t> payload( udp::endpoint  reflexive_ep
                              , udp::endpoint  ext_ep
                              , udp::endpoint  int_ep
                              , uint16_t) const;

  void match( server& server
            , udp::endpoint ep1_ext, udp::endpoint ep1_int, uint16_t
            , udp::endpoint ep2_ext, udp::endpoint ep2_int, uint16_t);

  void forget(udp::endpoint from, const char*);

  Entry find_or_create_entry( udp::endpoint ext_ep
                            , udp::endpoint int_ep
                            , uint16_t      reflected_port
                            , bool          is_host
                            , service_type);

  void remove_ep_from_service( udp::endpoint ep
                             , service_type  service);

  void respond_with_reflector(udp::endpoint from, server&);

private:
  StatePtr  _state;
  options   _options;
  Reflector _reflector;

  std::map<udp::endpoint, Service>                ep_to_service;
  std::map<service_type, std::set<udp::endpoint>> service_to_endpoints;
};

} // rendezvous namespace

#include "server.h"

namespace rendezvous {

//------------------------------------------------------------------------------
inline
server_handler_1::server_handler_1( boost::asio::io_service& ios
                                  , const options& opts)
  : _state(std::make_shared<State>(ios))
  , _options(opts)
  , _reflector(ios)
{
}

//------------------------------------------------------------------------------
inline
void server_handler_1::forget(udp::endpoint from, const char* msg) {
  using std::cout;
  using std::endl;

  auto i = ep_to_service.find(from);
  if (i != ep_to_service.end()) {

    if (_options.verbosity() > 1) {
      cout << "Forgetting: " << from << " (" << msg << ")" << endl;
    }

    auto j = service_to_endpoints.find(i->second.type);
    if (j != service_to_endpoints.end()) {
      j->second.erase(from);
      if (j->second.empty()) {
        service_to_endpoints.erase(i->second.type);
      }
    }
    ep_to_service.erase(i);
  }
}

//------------------------------------------------------------------------------
inline
void
server_handler_1::remove_ep_from_service( udp::endpoint ep
                                        , service_type  service) {
  auto eps_i = service_to_endpoints.find(service);

  if (eps_i == service_to_endpoints.end()) {
    return;
  }

  eps_i->second.erase(ep);

  if (eps_i->second.empty()) {
    service_to_endpoints.erase(service);
  }
}

//------------------------------------------------------------------------------
inline
server_handler_1::Entry
server_handler_1::find_or_create_entry( udp::endpoint ext_ep
                                      , udp::endpoint int_ep
                                      , uint16_t      reflected_port
                                      , bool          is_host
                                      , service_type  service_t) {
  auto ep_i = ep_to_service.find(ext_ep);

  if (ep_i == ep_to_service.end()) {
    // We've not heart from this endpoint yet.
    auto pair = ep_to_service.emplace(ext_ep, Service{ service_t
                                                     , int_ep
                                                     , reflected_port
                                                     , is_host
                                                     , make_dms(ext_ep)});

    auto& endpoints = service_to_endpoints[service_t];
    endpoints.insert(ext_ep);
    return Entry{ ext_ep
                , int_ep
                , pair.first->second
                , endpoints };
  }
  else {
    // We already know him.
    auto& service = ep_i->second;

    service.dead_man_switch->start(Constant::keepalive_duration());

    service.is_host = is_host;

    if (service.type != service_t) {
      // He changed to another service.
      remove_ep_from_service(ext_ep, service.type);
      service.type = service_t;
    }

    auto& endpoints = service_to_endpoints[service_t];
    endpoints.insert(ext_ep);

    return Entry{ ext_ep
                , int_ep
                , ep_i->second
                , endpoints };
  }
}

//------------------------------------------------------------------------------
inline
void
server_handler_1::respond_with_reflector(udp::endpoint from, server& server) {
  std::vector<uint8_t> payload(HEADER_SIZE + 4);

  binary::encoder e(payload.data(), payload.size());

  write_header(e, payload.size());

  e.put((uint8_t)  METHOD_REFLECTOR);
  e.put((uint8_t)  0); // Reserved.
  e.put((uint16_t) _reflector.get_port());

  assert(!e.error());

  server.send_to(from, std::move(payload));
}

//------------------------------------------------------------------------------
inline
void server_handler_1::handle( udp::endpoint from
                             , binary::decoder d
                             , server& server) {

  using std::cout;
  using std::endl;
  using std::move;

  namespace ip = boost::asio::ip;

  auto client_method = d.get<uint8_t>();

  if (d.error()) {
    if (_options.verbosity() > 0) {
      cout << "Error parsing message from " << from << endl;
    }
    return;
  }

  if (_options.verbosity() > 2) {
    cout << "handler_1::handle(" << from << ", "
         << ((int) client_method) << ")" << endl;
  }

  bool is_host;

  switch (client_method) {
    case CLIENT_METHOD_CLOSE: return forget(from, "close");
    case CLIENT_METHOD_FETCH: is_host = false;
                              break; // Handled in the rest of this function.
    case CLIENT_METHOD_FETCH_AS_HOST:
                              is_host = true;
                              break; // Handled in the rest of this function.
    case CLIENT_METHOD_GET_REFLECTOR:
                              return respond_with_reflector(from, server);
    default: {
               if (_options.verbosity() > 0) {
                 cout << "Invalid client method (" << ((int) client_method)
                      << ") from " << from << endl;
               }
               return forget(from, "invalid message");
             }
  }

  auto service = d.get<service_type>();
  auto his_reflected_port = d.get<uint16_t>();
  auto his_internal_port  = d.get<uint16_t>();

  udp::endpoint his_internal_ep;

  auto ipv = d.get<uint8_t>();

  switch (ipv) {
    case IPV4_TAG: {
                     ip::address_v4 addr;
                     binary::decode(d, addr);
                     his_internal_ep = udp::endpoint(addr, his_internal_port);
                     break;
                   }
    case IPV6_TAG: {
                     ip::address_v6 addr;
                     binary::decode(d, addr);
                     his_internal_ep = udp::endpoint(addr, his_internal_port);
                     break;
                   }
    default:
                   return forget(from, "invalid ip version");
  }

  if (d.error()) {
    if (_options.verbosity() > 0) {
      cout << "Error parsing message from " << from << endl;
    }
    return forget(from, "error parsing message");
  }

  auto entry = find_or_create_entry( from
                                   , his_internal_ep
                                   , his_reflected_port
                                   , is_host
                                   , service);

  for (auto other_ext_ep : entry.service_endpoints) {
    if (other_ext_ep == from) {
      continue;
    }
    auto other_i = ep_to_service.find(other_ext_ep);

    assert(other_i != ep_to_service.end());
    assert(other_i->second.type == service);

    auto& other = other_i->second;

    if (!is_host || !other.is_host) {
      match(server, other_ext_ep
                  , other.internal_ep
                  , other.reflected_port
                  , from
                  , his_internal_ep
                  , his_reflected_port);

      forget(from, "match 1");
      forget(other_ext_ep, "match 2");

      return;
    }
  }
}

//------------------------------------------------------------------------------
inline
std::unique_ptr<async::alarm>
server_handler_1::make_dms(udp::endpoint requester_ep) {
  using std::cout;
  using std::endl;
  using std::move;

  auto dms = std::make_unique<async::alarm>
               ( get_io_service()
               , [=]() { forget(requester_ep, "timeout"); });

  dms->start(Constant::keepalive_duration());

  return move(dms);
}

//------------------------------------------------------------------------------
inline
void server_handler_1::match( server& server
                            , udp::endpoint ep1_ext
                            , udp::endpoint ep1_int
                            , uint16_t      reflected_port1
                            , udp::endpoint ep2_ext
                            , udp::endpoint ep2_int
                            , uint16_t      reflected_port2) {
  server.send_to(ep1_ext, payload(ep1_ext, ep2_ext, ep2_int, reflected_port2));
  server.send_to(ep2_ext, payload(ep2_ext, ep1_ext, ep1_int, reflected_port1));
}

//------------------------------------------------------------------------------
inline
server_handler_1::~server_handler_1() {
  _state->was_destroyed = true;
}

//------------------------------------------------------------------------------
inline
std::vector<uint8_t> server_handler_1::payload
                       ( udp::endpoint reflexive_ep
                       , udp::endpoint ext_ep
                       , udp::endpoint int_ep
                       , uint16_t      reflected_port) const {

  namespace ip = boost::asio::ip;

  bool is_sym = false;

  if (reflected_port != 0) {
    auto diff = reflected_port - ext_ep.port();
    is_sym = diff != 0;
    ext_ep.port(reflected_port + diff);
  }

  // TODO Padding
  uint16_t payload_size
            = 1 /* method */
            + 3 /* port+ipv */ + (reflexive_ep.address().is_v4() ? 4 : 16)
            + 3 /* port+ipv */ + (ext_ep      .address().is_v4() ? 4 : 16)
            + 3 /* port+ipv */ + (int_ep      .address().is_v4() ? 4 : 16)
            + 1 /* (bool) other is symmetric */
            ;

  std::vector<uint8_t> ret(HEADER_SIZE + payload_size);

  binary::encoder encoder(ret.data(), ret.size());

  write_header(encoder, payload_size);

  encoder.put((uint8_t) METHOD_MATCH);

  for (auto ep : {reflexive_ep, ext_ep, int_ep}) {
    if (ep.address().is_v4()) {
      encoder.put((uint8_t)  IPV4_TAG);
      encoder.put((uint16_t) ep.port());
      encoder.put<ip::address_v4>(ep.address().to_v4());
    }
    else {
      encoder.put((uint8_t)  IPV6_TAG);
      encoder.put((uint16_t) ep.port());
      encoder.put(ep.address().to_v6());
    }
  }

  encoder.put((uint8_t) is_sym);

  assert(!encoder.error());
  ret.resize(encoder.written());

  return ret;
}

//------------------------------------------------------------------------------
inline
boost::asio::io_service& server_handler_1::get_io_service() const {
  return _state->ios;
}

//------------------------------------------------------------------------------

} // rendezvous namespace

#endif // ifndef __RENDEZVOUS_SERVER_HANDLER_1_H__
