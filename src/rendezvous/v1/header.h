#ifndef RENDEZVOUS_V1_HEADER_H
#define RENDEZVOUS_V1_HEADER_H

#include <rendezvous/constants.h>

namespace rendezvous {

inline void write_header( binary::encoder& encoder
                        , uint16_t payload_size) {
  static const uint16_t plex = 1 << 15;
  static const VersionType version = 1;
  encoder.put((uint16_t) (plex | version));
  encoder.put((uint16_t) payload_size);
  encoder.put((uint32_t) COOKIE);
}

} // rendezvous namespace

#endif // ifndef RENDEZVOUS_V1_HEADER_H
