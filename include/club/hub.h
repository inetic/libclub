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
#include <list>
#include <boost/asio.hpp>
#include <boost/container/flat_map.hpp>
#include <binary/decoder.h>

#include "club/graph.h"
#include "club/uuid.h"

#include <club/detail/time_stamp.h>
#include "log.h"

namespace club {

class Socket;
struct Node;
class GetExternalPort;
class BroadcastRoutingTable;
struct Fuse;
struct PortOffer;
struct UserData;
struct Ack;
struct LogEntry;
struct MessageId;
class Log;
class SeenMessages;

class hub {
private:
  using ID = club::uuid;
  using Bytes = std::vector<char>; // TODO: This should be a vector of uint8_t
  using Address = boost::asio::ip::address;

  typedef boost::asio::io_service::work    Work;
  typedef boost::container::flat_map<uuid, std::unique_ptr<Node>> Nodes;

  typedef std::function<void(const boost::system::error_code&, uuid)> OnFused;

public:
  using OnInsert = std::function<void(std::set<uuid>)>;
  using OnRemove = std::function<void(std::set<uuid>)>;
  using OnReceive = std::function<void(uuid, const Bytes&)>;
  using OnReceiveUnreliable = std::function<void(uuid, boost::asio::const_buffer)>;
  using OnDirectConnect = std::function<void(uuid)>;

public:

  hub(boost::asio::io_service&);

  /// Set the callback to be executed when new nodes join the network
  /// we're in. The callback shall be used multiple times until on_insert
  /// function is invoked again with a different argument.
  ///
  /// \param f a std::function object with the signature void(std::set<uuid>).
  ///          'f' can be set to nullptr in which case no callback shall
  ///          be executed on the given event.
  void on_insert(OnInsert f);

  /// Set the callback to be executed when nodes leave the network
  /// we're in. The callback shall be used multiple times until on_remove
  /// function is invoked again with a different argument.
  ///
  /// \param f a std::function object with the signature void(std::set<uuid>).
  ///          'f' can be set to nullptr in which case no callback shall
  ///          be executed on the given event.
  void on_remove(OnRemove f);

  /// Set the callback to be executed when a reliable broadcast message
  /// has been received and totally ordered. The callback shall be used
  /// multiple times until on_receive function is invoked again with a
  /// different argument.
  ///
  /// \param f a std::function object with the signature
  ///          void(uuid source, const std::vector<char>& data).
  ///          'f' can be set to nullptr in which case no callback shall
  ///          be executed on the given event.
  void on_receive(OnReceive f);

  /// Set the callback to be executed when an unreliable broadcast message
  /// has been received. The callback shall be used multiple times until
  /// on_receive_unreliable function is invoked again with a different argument.
  ///
  /// \param f a std::function object with the signature
  ///          void(uuid source, boost::asio::const_buffer data).
  ///          'f' can be set to nullptr in which case no callback shall
  ///          be executed on the given event.
  void on_receive_unreliable(OnReceiveUnreliable f);

  // TODO: Currently unused.
  void on_direct_connect(OnDirectConnect f);

  /// Merge the network we're in with another network.
  ///
  /// \param sock a socket with a direct connection to a node from another
  ///             network.
  /// \on_fused a std::function object of with the signature
  ///             void(const boost::system::error_code&, boostd::uuids::uuid)
  ///             is executed once a basic handshake information is exchanged
  ///             between the two nodes and before any of the nodes from
  ///             the other network are 'inserted' (see `on_insert`).
  void fuse(Socket&& sock, const OnFused& on_fused);

  /// Broadcast a message reliably to the network and let the network assign
  /// a total order to it. That is, the following properties hold:
  ///
  /// 1. For any node A, if A receives message M1 before message M2, then
  ///    any node that receives both of the messages shall receive
  ///    them in the same order.
  /// 2. For any node A, if A sends message M1 before message M2, then
  ///    any node that receives both of the messages shall receive
  ///    M1 before M2.
  ///
  /// The receiver of the message is every node in the network including
  /// the sender.
  void total_order_broadcast(Bytes);

  /// Broadcast a message unreliably to the network. Unlike with the
  /// totally ordered broadcast, the targets of this message are
  /// all nodes of the network excluding the sender.
  ///
  /// There is no guarantee that every target shall receive a message
  /// sent.
  ///
  /// The \c on_broadcast callback shall be executed to indicate
  /// that another call to `unreliable_broadcast` function can be made.
  void unreliable_broadcast(Bytes, std::function<void()> on_broadcast);

  boost::asio::io_service& get_io_service() { return _io_service; }
  uuid                     id()    const    { return _id; }

  ~hub();

  size_t size() const { return _nodes.size(); }

private:
  friend struct Node;

  void add_connection(Node& from, uuid to, boost::asio::ip::address);

  template<class Message> void broadcast(const Message&);

  void on_recv_raw(Node&, boost::asio::const_buffer&);
  void node_received_unreliable_broadcast(boost::asio::const_buffer);

  template<class Message> void on_recv(Node&, Message);
  template<class Message> void parse_message(Node&, binary::decoder&);

  void process(Node&, Fuse);
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
  struct Callbacks;
  std::shared_ptr<Callbacks>             _callbacks;
  boost::asio::io_service&               _io_service;
  std::set<uuid>                         _last_quorum;
  std::unique_ptr<Work>                  _work;
  uuid                                   _id;
  Nodes                                  _nodes;
  std::unique_ptr<Log>                   _log;
  TimeStamp                              _time_stamp;
  std::unique_ptr<BroadcastRoutingTable> _broadcast_routing_table;
  std::shared_ptr<bool>                  _was_destroyed;

  // TODO: This must be refactored, otherwise the memory will grow indefinitely.
  //       Luckily reconfiguration doesn't happen too often, so for apps that
  //       are expected to run for only a couple of hours this shouldn't be a
  //       problem.
  std::set<MessageId> _configs;
  std::unique_ptr<SeenMessages> _seen;

  std::list<std::unique_ptr<GetExternalPort>> _stun_requests;

  template<class... Ts> void debug(Ts&&...);
  std::list<std::string> debug_log;
};

} // club namespace

#endif // ifndef CLUB_HUB_H
