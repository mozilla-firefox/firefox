/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_SHARED_DATAGRAMQUEUECHARGE_H_
#define DOM_WEBTRANSPORT_SHARED_DATAGRAMQUEUECHARGE_H_

#include <cstdint>

namespace mozilla::dom {

// What neqo charges a queued datagram against the session's byte budget, on
// top of its payload: mirrors PER_DATAGRAM_OVERHEAD in neqo-transport's
// datagram_queue.rs, which does not export it.  Both sides of the datagram
// credit grant count bytes this way, so the credit content holds never
// exceeds what neqo's queue can take without evicting.
// TODO(bug 2079448): take this from neqo once it exposes it.
static constexpr uint64_t kDatagramQueueOverhead = 64;

// neqo queues the encoded HTTP/3 datagram, so its charge also covers the
// session's prefix: the quarter stream ID varint that neqo-http3's
// Session::send_datagram writes ahead of the payload.
constexpr uint64_t DatagramPrefixBytes(uint64_t aSessionId) {
  uint64_t quarterStreamId = aSessionId / 4;
  return quarterStreamId < (1ull << 6)    ? 1
         : quarterStreamId < (1ull << 14) ? 2
         : quarterStreamId < (1ull << 30) ? 4
                                          : 8;
}

constexpr uint64_t DatagramQueueCharge(uint64_t aPayloadBytes,
                                       uint64_t aPrefixBytes) {
  return aPayloadBytes + aPrefixBytes + kDatagramQueueOverhead;
}

}  // namespace mozilla::dom

#endif  // DOM_WEBTRANSPORT_SHARED_DATAGRAMQUEUECHARGE_H_
