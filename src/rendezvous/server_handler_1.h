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
#include <binary/ip.h>
#include <rendezvous/constants.h>
#include <async/alarm.h>
#include "options.h"

namespace rendezvous {

class server;

class server_handler_1 {
  using service_type = uint32_t;
  using udp = boost::asio::ip::udp;
  using Address = boost::asio::ip::address;

  struct State {
    boost::asio::io_service& ios;
    bool was_destroyed;

    State(boost::asio::io_service& ios)
      : ios(ios)
      , was_destroyed(false) {
    }
  };

  using StatePtr = std::shared_ptr<State>;
  using Constant = constants_v1;

  struct DMS { // Dead Man's Switch
    udp::endpoint    requester_external_ep;
    udp::endpoint    requester_internal_ep;
    std::unique_ptr<async::alarm> alarm;
  };

public:
  static const VersionType version = 1;

  server_handler_1(boost::asio::io_service&, const options&);
  ~server_handler_1();

  boost::asio::io_service& get_io_service() const;
  void handle(udp::endpoint from, binary::decoder, server&);

private:
  void on_alarm();
  DMS make_dms( service_type service
              , udp::endpoint
              , udp::endpoint);

  std::vector<uint8_t> payload( udp::endpoint  reflexive_ep
                              , udp::endpoint  ext_ep
                              , udp::endpoint  int_ep) const;

  void match( server& server
            , udp::endpoint ep1_ext, udp::endpoint ep1_int
            , udp::endpoint ep2_ext, udp::endpoint ep2_int);

  void forget(udp::endpoint from);

private:
  StatePtr  _state;
  options   _options;

  std::map<udp::endpoint, service_type> ep_to_service;
  std::map<service_type, DMS> service_to_dms;
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
{
}

//------------------------------------------------------------------------------
inline
void server_handler_1::forget(udp::endpoint from) {
  auto i = ep_to_service.find(from);
  if (i != ep_to_service.end()) {
    service_to_dms.erase(i->second);
  }
  ep_to_service.erase(i);
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

  if (_options.verbosity() > 2) {
    cout << "handler_1::handle(" << from << ")" << endl;
  }

  auto client_method = d.get<uint8_t>();

  if (d.error()) {
    if (_options.verbosity() > 0) {
      cout << "Error parsing message from " << from << endl;
    }
    return;
  }

  switch (client_method) {
    case CLIENT_METHOD_CLOSE: return forget(from);
    case CLIENT_METHOD_FETCH: break; // Handled in the rest of this function.
    default: {
               if (_options.verbosity() > 0) {
                 cout << "Invalid client method from " << from << endl;
               }
               return forget(from);
             }
  }

  auto service = d.get<service_type>();
  auto his_internal_port = d.get<uint16_t>();

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
    default: return forget(from);
  }

  if (d.error()) {
    if (_options.verbosity() > 0) {
      cout << "Error parsing message from " << from << endl;
    }
    return forget(from);
  }

  auto ep_i = ep_to_service.find(from);
  auto service_i = service_to_dms.find(service);

  if (ep_i == ep_to_service.end()) {
    if (service_i == service_to_dms.end()) {
      if (_options.verbosity() > 1) {
        cout << "Registering " << from << " on service " << service << endl;
      }
      ep_to_service.emplace(from, service);

      service_to_dms.emplace(service, make_dms( service
                                              , from
                                              , his_internal_ep));
    }
    else {
      auto ext_ep   = service_i->second.requester_external_ep;
      auto int_ep   = service_i->second.requester_internal_ep;

      if (_options.verbosity() > 1) {
        cout << "Match " << ext_ep << ", " << from << endl;
      }
      match(server, ext_ep, int_ep
                  , from, his_internal_ep);
      ep_to_service.erase(ext_ep);
      service_to_dms.erase(service_i);
    }
  }
  else {
    auto old_service = ep_i->second;

    if (old_service == service) {
      if (_options.verbosity() > 2) {
        cout << "Refresh " << from << " service " << service << endl;
      }
      assert(service_i != service_to_dms.end());
      service_i->second.alarm->start(Constant::keepalive_duration());
    }
    else {
      service_to_dms.erase(old_service);

      if (service_i == service_to_dms.end()) {
        if (_options.verbosity() > 1) {
          cout << "Reregister " << from
               << " service " << service
               << " (old service: " << old_service << ")" << endl;
        }
        service_to_dms.emplace(service, make_dms(service
                                                , from
                                                , his_internal_ep));
      }
      else {
        auto ext_ep   = service_i->second.requester_external_ep;
        auto int_ep   = service_i->second.requester_internal_ep;

        if (_options.verbosity() > 1) {
          cout << "ReRegister match: " << from << " " << ext_ep << endl;
        }
        match(server, ext_ep, int_ep
                    , from, his_internal_ep);
        ep_to_service.erase(ep_i);
        service_to_dms.erase(service_i);
      }
    }
  }
}

//------------------------------------------------------------------------------
inline
server_handler_1::DMS server_handler_1::make_dms( service_type  service
                                                , udp::endpoint requester_ext
                                                , udp::endpoint requester_int) {
  using std::cout;
  using std::endl;
  using std::move;

  return DMS({ requester_ext
             , requester_int
             , std::make_unique<async::alarm>
                ( get_io_service()
                , [=]() {
                    if (_options.verbosity() > 1) {
                      cout << "Deregister " << requester_ext
                           << " on service " << service
                           << " (timeout) " << endl;
                    }
                    ep_to_service.erase(requester_ext);
                    service_to_dms.erase(service);
                  })});
}

//------------------------------------------------------------------------------
inline
void server_handler_1::match( server& server
                            , udp::endpoint ep1_ext
                            , udp::endpoint ep1_int
                            , udp::endpoint ep2_ext
                            , udp::endpoint ep2_int) {
  using std::move;
  server.send_to(ep1_ext, payload(ep1_ext, ep2_ext, ep2_int));
  server.send_to(ep2_ext, payload(ep2_ext, ep1_ext, ep1_int));
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
                       , udp::endpoint int_ep ) const {

  namespace ip = boost::asio::ip;

  // TODO Padding
  uint16_t payload_size
            = 1 /* method */
            + 3 /* port+ipv */ + (reflexive_ep.address().is_v4() ? 4 : 16)
            + 3 /* port+ipv */ + (ext_ep      .address().is_v4() ? 4 : 16)
            + 3 /* port+ipv */ + (int_ep      .address().is_v4() ? 4 : 16);

  std::vector<uint8_t> ret(HEADER_SIZE + payload_size);

  binary::encoder encoder(ret.data(), ret.size());

  static const uint16_t plex = 1 << 15;
  encoder.put((uint16_t) (plex | version));
  encoder.put((uint16_t) payload_size);
  encoder.put((uint32_t) COOKIE);
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
