#ifndef CLUB_SEEN_MESSAGES_H
#define CLUB_SEEN_MESSAGES_H

#include <club/message_id.h>

namespace club {

class SeenMessages {
  using uuid = boost::uuids::uuid;

  struct TimeStamps : std::set<TimeStamp> {
    // Everything <= to the `bottom` variable is considered
    // to be inside the set.
    boost::optional<TimeStamp> bottom;
  };

public:
  void insert(MessageId);
  bool is_in(const MessageId&);
  void seen_everything_up_to(const MessageId&);
  void forget_messages_from_user(const uuid&);

private:
  std::map<uuid, TimeStamps> _messages;
};

//--------------------------------------------------------------------
inline
void SeenMessages::insert(MessageId mid) {
  auto& tss = _messages[mid.original_poster];

  if (tss.bottom && mid.timestamp <= *tss.bottom) {
    return;
  }

  tss.insert(mid.timestamp);
}

//--------------------------------------------------------------------
inline
bool SeenMessages::is_in(const MessageId& mid) {
  auto i = _messages.find(mid.original_poster);

  if (i == _messages.end()) return false;

  auto& tss = i->second;

  if (tss.bottom && mid.timestamp <= *tss.bottom) {
    return true;
  }

  return tss.count(mid.timestamp) != 0;
}

//--------------------------------------------------------------------
inline
void SeenMessages::seen_everything_up_to(const MessageId& mid) {
  for (auto& pair : _messages) {
    auto& tss = pair.second;

    if (tss.bottom && mid.timestamp <= *tss.bottom) {
      continue;
    }

    tss.bottom = mid.timestamp;

    tss.erase( tss.begin()
             , tss.upper_bound(mid.timestamp));
  }
}

//--------------------------------------------------------------------
inline
void SeenMessages::forget_messages_from_user(const uuid& uid) {
  _messages.erase(uid);
}

//--------------------------------------------------------------------
} // club namespace

#endif // ifndef CLUB_SEEN_MESSAGES_H
