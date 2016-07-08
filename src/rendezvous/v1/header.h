#ifndef RENDEZVOUS_V1_HEADER_H
#define RENDEZVOUS_V1_HEADER_H

#include <rendezvous/constants.h>

namespace rendezvous {

/*
 * Version 0 means the requester requested an unsupported version
 */
inline void write_header( binary::encoder& encoder
                        , VersionType      version
                        , uint16_t         payload_size) {
  static const uint16_t plex = 1 << 15;
  encoder.put((uint16_t) (plex | version));
  encoder.put((uint16_t) payload_size);
  encoder.put((uint32_t) COOKIE);
}

} // rendezvous namespace

#endif // ifndef RENDEZVOUS_V1_HEADER_H
