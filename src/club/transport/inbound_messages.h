#ifndef CLUB_TRANSPORT_INBOUND_MESSAGES_H
#define CLUB_TRANSPORT_INBOUND_MESSAGES_H

namespace club { namespace transport {

template<typename UnreliableId>
class InboundMessages {
  using OnReceive = std::function<void(boost::asio::const_buffer)>;
  using Transport = transport::Transport<UnreliableId>;

public:
  InboundMessages(OnReceive on_recv);

private:
  friend class transport::Transport<UnreliableId>;

  void register_transport(Transport*);
  void deregister_transport(Transport*);

  void on_receive( const boost::system::error_code&
                 , boost::asio::const_buffer);

private:
  OnReceive _on_recv;
  std::set<Transport*> _transports;
};

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
template<class Id>
InboundMessages<Id>::InboundMessages(OnReceive on_recv)
  : _on_recv(std::move(on_recv))
{}

//------------------------------------------------------------------------------
template<class Id> void InboundMessages<Id>::register_transport(Transport* t)
{
  _transports.insert(t);
}

//------------------------------------------------------------------------------
template<class Id> void InboundMessages<Id>::deregister_transport(Transport* t)
{
  _transports.erase(t);
}

//------------------------------------------------------------------------------
template<class Id>
void InboundMessages<Id>::on_receive( const boost::system::error_code&
                                    , boost::asio::const_buffer buffer) {
  _on_recv(buffer);
}

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_INBOUND_MESSAGES_H
