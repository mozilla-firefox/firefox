/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DashboardTypes_h_
#define mozilla_net_DashboardTypes_h_

#include "ipc/IPCMessageUtils.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/Maybe.h"
#include "nsHttp.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

namespace mozilla {
namespace net {

struct SocketInfo {
  nsCString host;
  uint64_t sent;
  uint64_t received;
  uint16_t port;
  bool active;
  nsCString type;
  nsCString originAttributesSuffix;
};

inline bool operator==(const SocketInfo& a, const SocketInfo& b) {
  return a.host == b.host && a.sent == b.sent && a.received == b.received &&
         a.port == b.port && a.active == b.active && a.type == b.type &&
         a.originAttributesSuffix == b.originAttributesSuffix;
}

struct DnsAndConnectSockets {
  bool speculative;
};

struct DNSCacheEntries {
  nsCString hostname;
  nsTArray<nsCString> hostaddr;
  uint16_t family{0};
  int64_t expiration{0};
  bool TRR{false};
  nsCString originAttributesSuffix;
  nsCString flags;
  uint16_t resolveType{0};
};

struct HttpConnInfo {
  uint32_t ttl;
  uint32_t rtt;
  nsString protocolVersion;

  void SetHTTPProtocolVersion(HttpVersion pv);
};

struct HttpRetParams {
  nsCString host;
  CopyableTArray<HttpConnInfo> active;
  CopyableTArray<HttpConnInfo> idle;
  CopyableTArray<DnsAndConnectSockets> dnsAndSocks;
  uint32_t counter;
  uint16_t port;
  nsCString httpVersion;
  bool ssl;
  nsCString originAttributesSuffix;
};

struct Http3ConnStats {
  // Total packets received, including all the bad ones.
  uint64_t packetsRx;
  // Duplicate packets received.
  uint64_t dupsRx;
  // Dropped packets or dropped garbage.
  uint64_t droppedRx;
  // The number of packet that were saved for later processing.
  uint64_t savedDatagrams;
  // Total packets sent.
  uint64_t packetsTx;
  // Total number of packets that are declared lost.
  uint64_t lost;
  // Late acknowledgments, for packets that were declared lost already.
  uint64_t lateAck;
  // Acknowledgments for packets that contained data that was marked
  // for retransmission when the PTO timer popped.
  uint64_t ptoAck;
  // Count PTOs. Single PTOs, 2 PTOs in a row, 3 PTOs in row, etc. are counted
  // separately.
  CopyableTArray<uint64_t> ptoCounts;
  // The count of WouldBlock errors encountered during receive operations.
  uint64_t wouldBlockRx;
  // The count of WouldBlock errors encountered during transmit operations.
  uint64_t wouldBlockTx;
};

struct Http3ConnectionStatsParams {
  nsCString host;
  uint16_t port;
  CopyableTArray<Http3ConnStats> stats;
};

using SSLTokensCacheCertHash = CopyableTArray<uint8_t>;  // SHA-256
// Maps a cert's SHA-256 fingerprint to its DER.
using SSLTokensCacheCertDERByFingerprint =
    nsTHashMap<nsCStringHashKey, nsTArray<uint8_t>>;

struct SSLTokensCacheRecordInfo {
  nsCString key;
  int64_t expirationTime;
  uint8_t overridableError;
  bool restored;
  uint64_t id;
  Maybe<CopyableTArray<SSLTokensCacheCertHash>> succeededHashes;
  Maybe<CopyableTArray<SSLTokensCacheCertHash>> handshakeHashes;
  CopyableTArray<uint8_t> compressedPayload;
};

// L, I, R: leaf, intermediate and root certs.
// SSLTokensCacheSnapshot
// ├─ records: [RecordInfo(a.com #1), RecordInfo(a.com #2), RecordInfo(b.com)]
// │    each: key, expiration, …,
// │          succeededHashes = [hash(L), hash(I), hash(R)],
// │          handshakeHashes = [hash(L), hash(I)],
// │          compressedPayload = token + ev, ct, overridable, builtin_root
// └─ certs: {hash(L) → DER(L), hash(I) → DER(I), hash(R) → DER(R)}
// TODO: Measure whether Brotli compression of records still pays off now that
// the certificates are stored separately.
struct SSLTokensCacheSnapshot {
  CopyableTArray<SSLTokensCacheRecordInfo> records;
  SSLTokensCacheCertDERByFingerprint certs;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_DashboardTypes_h_
