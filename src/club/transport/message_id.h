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
using MessageId = boost::variant< ReliableMessageId
                                , UnreliableMessageId<UnreliableValue> >;

//------------------------------------------------------------------------------

}} // club::transport namespace

#endif // ifndef CLUB_TRANSPORT_MESSAGE_ID_H
