#include <club/message_id.h>
#include "debug/ostream_uuid.h"

namespace club {

std::ostream& operator<<(std::ostream& os, const MessageId& o) {
  return os << "(" << o.original_poster << ":" << o.timestamp << ")";
}

} // club namespace
