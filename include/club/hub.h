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

#ifndef CLUB_HUB_H
#define CLUB_HUB_H

#include <map>
#include <boost/signals2.hpp>
#include <boost/container/flat_map.hpp>
#include <binary/decoder.h>

#include "club/graph.h"
#include "club/uuid.h"
#include "club/node_impl.h"

#include <club/socket.h>
#include <club/detail/time_stamp.h>
#include "log.h"

namespace club {

struct Node;
class GetExternalPort;
class BroadcastRoutingTable;
struct Fuse;
struct Sync;
struct PortOffer;
struct UserData;
struct Ack;
struct LogEntry;
struct MessageId;
class Log;
class SeenMessages;

namespace transport {
  template<typename> class Core;
}

class hub {
private:
  template<class T> using Signal = boost::signals2::signal<T>;
  template<class T> using shared_ptr = std::shared_ptr<T>;

  using ID = club::uuid;
  using Bytes = std::vector<char>;
  using Address = boost::asio::ip::address;
  using TransportCore = transport::Core<uint64_t>;

  typedef boost::asio::io_service::work    Work;
  typedef boost::container::flat_map<uuid, std::unique_ptr<Node>> Nodes;

  typedef std::function<void(const boost::system::error_code&, uuid)> OnFused;

public:

  using node = node_impl;

public:

  hub(boost::asio::io_service&);

  Signal<void(std::set<node>)>                  on_insert;
  Signal<void(std::set<node>)>                  on_remove;
  Signal<void(node, const Bytes&)>              on_receive;
  Signal<void(node, boost::asio::const_buffer)> on_receive_unreliable;
  Signal<void(node)>                            on_direct_connect;

  void fuse(Socket&&, const OnFused&);

  void total_order_broadcast(Bytes);
  void unreliable_broadcast(Bytes, std::function<void()>);

  boost::asio::io_service& get_io_service() { return _io_service; }
  uuid                     id()    const    { return _id; }

  ~hub();

  size_t size() const { return _nodes.size(); }

  // TODO: Check why this is public.
  void add_connection(Node& from, uuid to, boost::asio::ip::address);

private:
  friend struct Node;

  template<class Message> void broadcast(const Message&);

  void on_recv_raw(Node&, const Bytes&);
  void node_received_unreliable_broadcast(const Bytes& bytes);

  template<class Message> void on_recv(Node&, Message);
  template<class Message> void parse_message(Node&, binary::decoder&);

  void process(Node&, Fuse);
  void process(Node&, Sync);
  void process(Node&, PortOffer);
  void process(Node&, UserData);
  void process(Node&, Ack);

  void commit_what_was_seen_by_everyone();

  void on_peer_connected(const Node&);
  void on_peer_disconnected(const Node&, std::string reason);

  template<class Message>
  void add_log_entry(Message);

  void on_commit_fuse(LogEntry);

  template<class Message, class... Args> Message construct(Args&&...);
  template<class Message, class... Args> Message construct_ackable(Args&&... args);
  Ack construct_ack(const MessageId&);

  void broadcast_port_offer_to(Node&, Address addr);

  template<class F> bool destroys_this(F);

  Address find_address_to(uuid) const;

  Node& this_node();

  Node& insert_node(uuid id);
  Node& insert_node(uuid id, std::shared_ptr<Socket>);

  Node*       find_node(uuid id);
  const Node* find_node(uuid id) const;

  std::set<uuid> remove_connection(uuid from, uuid to);

  boost::container::flat_set<uuid> local_quorum() const;

  void commit(LogEntry&& entry);
  void commit_user_data(uuid op, std::vector<char>&&);
  void commit_fuse(LogEntry&&);

private:
  boost::asio::io_service&               _io_service;
  std::set<uuid>                         _last_quorum;
  std::unique_ptr<Work>                  _work;
  uuid                                   _id;
  Nodes                                  _nodes;
  std::unique_ptr<Log>                   _log;
  TimeStamp                              _time_stamp;
  std::unique_ptr<BroadcastRoutingTable> _broadcast_routing_table;
  std::shared_ptr<bool>                  _was_destroyed;
  std::shared_ptr<TransportCore>         _transport_core;

  // TODO: This must be refactored, otherwise the memory will grow indefinitely.
  //       Luckily reconfiguration doesn't happen too often, so for apps that
  //       are expected to run for only a couple of hours this shouldn't be a
  //       problem.
  std::set<MessageId> _configs;
  std::unique_ptr<SeenMessages> _seen;

  std::list<std::unique_ptr<GetExternalPort>> _stun_requests;
};

} // club namespace

#endif // ifndef CLUB_HUB_H
