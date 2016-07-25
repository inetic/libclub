#ifndef CLUB_TRANSPORT_MESSAGE_ID_H
#define CLUB_TRANSPORT_MESSAGE_ID_H

namespace club { namespace transport {

//------------------------------------------------------------------------------
template<typename UnreliableValue>
struct UnreliableMessageId {
  UnreliableValue value;

  bool operator< (const UnreliableMessageId& other) const {
    return value < other.value;
  }
};

//------------------------------------------------------------------------------
struct ReliableMessageId {
  SequenceNumber value;

  bool operator< (const ReliableMessageId& other) const {
    return value < other.value;
  }
};

//------------------------------------------------------------------------------
template<typename UnreliableValue>
struct MessageId
  : public boost::variant< ReliableMessageId
                         , UnreliableMessageId<UnreliableValue>
                         >
{
  using Base = boost::variant< ReliableMessageId
                             , UnreliableMessageId<UnreliableValue>
                             >;
  using Base::Base;
};

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_ID_H
