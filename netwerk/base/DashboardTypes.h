/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DashboardTypes_h_
#define mozilla_net_DashboardTypes_h_

#include "ipc/IPCMessageUtils.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "nsHttp.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {
namespace net {

struct SocketInfo {
  nsCString host;
  uint64_t sent;
  uint64_t received;
  uint16_t port;
  bool active;
  nsCString type;
};

inline bool operator==(const SocketInfo& a, const SocketInfo& b) {
  return a.host == b.host && a.sent == b.sent && a.received == b.received &&
         a.port == b.port && a.active == b.active && a.type == b.type;
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

}  // namespace net
}  // namespace mozilla

namespace IPC {

template <>
struct ParamTraits<mozilla::net::SocketInfo> {
  typedef mozilla::net::SocketInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.host);
    WriteParam(aWriter, aParam.sent);
    WriteParam(aWriter, aParam.received);
    WriteParam(aWriter, aParam.port);
    WriteParam(aWriter, aParam.active);
    WriteParam(aWriter, aParam.type);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->host) &&
           ReadParam(aReader, &aResult->sent) &&
           ReadParam(aReader, &aResult->received) &&
           ReadParam(aReader, &aResult->port) &&
           ReadParam(aReader, &aResult->active) &&
           ReadParam(aReader, &aResult->type);
  }
};

template <>
struct ParamTraits<mozilla::net::DNSCacheEntries> {
  typedef mozilla::net::DNSCacheEntries paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.hostname);
    WriteParam(aWriter, aParam.hostaddr);
    WriteParam(aWriter, aParam.family);
    WriteParam(aWriter, aParam.expiration);
    WriteParam(aWriter, aParam.TRR);
    WriteParam(aWriter, aParam.originAttributesSuffix);
    WriteParam(aWriter, aParam.flags);
    WriteParam(aWriter, aParam.resolveType);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->hostname) &&
           ReadParam(aReader, &aResult->hostaddr) &&
           ReadParam(aReader, &aResult->family) &&
           ReadParam(aReader, &aResult->expiration) &&
           ReadParam(aReader, &aResult->TRR) &&
           ReadParam(aReader, &aResult->originAttributesSuffix) &&
           ReadParam(aReader, &aResult->flags) &&
           ReadParam(aReader, &aResult->resolveType);
  }
};

template <>
struct ParamTraits<mozilla::net::DnsAndConnectSockets> {
  typedef mozilla::net::DnsAndConnectSockets paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.speculative);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->speculative);
  }
};

template <>
struct ParamTraits<mozilla::net::HttpConnInfo> {
  typedef mozilla::net::HttpConnInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.ttl);
    WriteParam(aWriter, aParam.rtt);
    WriteParam(aWriter, aParam.protocolVersion);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->ttl) &&
           ReadParam(aReader, &aResult->rtt) &&
           ReadParam(aReader, &aResult->protocolVersion);
  }
};

template <>
struct ParamTraits<mozilla::net::HttpRetParams> {
  typedef mozilla::net::HttpRetParams paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.host);
    WriteParam(aWriter, aParam.active);
    WriteParam(aWriter, aParam.idle);
    WriteParam(aWriter, aParam.dnsAndSocks);
    WriteParam(aWriter, aParam.counter);
    WriteParam(aWriter, aParam.port);
    WriteParam(aWriter, aParam.httpVersion);
    WriteParam(aWriter, aParam.ssl);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->host) &&
           ReadParam(aReader, &aResult->active) &&
           ReadParam(aReader, &aResult->idle) &&
           ReadParam(aReader, &aResult->dnsAndSocks) &&
           ReadParam(aReader, &aResult->counter) &&
           ReadParam(aReader, &aResult->port) &&
           ReadParam(aReader, &aResult->httpVersion) &&
           ReadParam(aReader, &aResult->ssl);
  }
};

template <>
struct ParamTraits<mozilla::net::Http3ConnStats> {
  typedef mozilla::net::Http3ConnStats paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.packetsRx);
    WriteParam(aWriter, aParam.dupsRx);
    WriteParam(aWriter, aParam.droppedRx);
    WriteParam(aWriter, aParam.savedDatagrams);
    WriteParam(aWriter, aParam.packetsTx);
    WriteParam(aWriter, aParam.lost);
    WriteParam(aWriter, aParam.lateAck);
    WriteParam(aWriter, aParam.ptoAck);
    WriteParam(aWriter, aParam.ptoCounts);
    WriteParam(aWriter, aParam.wouldBlockRx);
    WriteParam(aWriter, aParam.wouldBlockTx);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->packetsRx) &&
           ReadParam(aReader, &aResult->dupsRx) &&
           ReadParam(aReader, &aResult->droppedRx) &&
           ReadParam(aReader, &aResult->savedDatagrams) &&
           ReadParam(aReader, &aResult->packetsTx) &&
           ReadParam(aReader, &aResult->lost) &&
           ReadParam(aReader, &aResult->lateAck) &&
           ReadParam(aReader, &aResult->ptoAck) &&
           ReadParam(aReader, &aResult->ptoCounts) &&
           ReadParam(aReader, &aResult->wouldBlockRx) &&
           ReadParam(aReader, &aResult->wouldBlockTx);
  }
};

template <>
struct ParamTraits<mozilla::net::Http3ConnectionStatsParams> {
  typedef mozilla::net::Http3ConnectionStatsParams paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.host);
    WriteParam(aWriter, aParam.port);
    WriteParam(aWriter, aParam.stats);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->host) &&
           ReadParam(aReader, &aResult->port) &&
           ReadParam(aReader, &aResult->stats);
  }
};

}  // namespace IPC

#endif  // mozilla_net_DashboardTypes_h_
