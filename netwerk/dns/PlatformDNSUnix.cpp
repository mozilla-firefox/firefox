/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GetAddrInfo.h"
#include "mozilla/Mutex.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"
#include "nsReadableUtils.h"

namespace mozilla::net {

#if defined(HAVE_RES_NINIT)
MOZ_THREAD_LOCAL(struct __res_state*) sThreadRes;
// The DNSNetworkGeneration() value when sThreadRes was initialized.
MOZ_THREAD_LOCAL(uint32_t) sThreadResGeneration;
mozilla::StaticMutex sMutex MOZ_ANNOTATED;
#endif

#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))

#if defined(HAVE_RES_NINIT)
static void CloseResState(struct __res_state* aRes) {
#  if defined(__FreeBSD__)
  // res_nclose only closes the sockets here, the rest is freed by res_ndestroy.
  res_ndestroy(aRes);
#  else
  res_nclose(aRes);
#  endif
}

struct __res_state* GetThreadResState() {
  // Read before res_ninit, so a network change that happens while the state
  // is being initialized causes another reinitialization on the next call.
  uint32_t generation = DNSNetworkGeneration();
  struct __res_state* res = sThreadRes.get();
  if (res && sThreadResGeneration.get() == generation) {
    return res;
  }

  StaticMutexAutoLock lock(sMutex);
  if (res) {
    LOG("network changed, reinitializing resolver state");
    CloseResState(res);
  } else {
    res = new struct __res_state;
    sThreadRes.set(res);
  }
  memset(res, 0, sizeof(struct __res_state));
  if (int ret = res_ninit(res)) {
    LOG("res_ninit failed: %d", ret);
    sThreadRes.set(nullptr);
    delete res;
    return nullptr;
  }
  sThreadResGeneration.set(generation);
  return res;
}
#endif

// Whether aAddr is a local stub resolver (e.g. the systemd-resolved stub at
// 127.0.0.53, or dnsmasq at 127.0.0.1): the whole 127.0.0.0/8 range for IPv4,
// or the IPv6 loopback.
static bool IsLocalStubResolver(const NetAddr& aAddr) {
  if (aAddr.raw.family == AF_INET) {
    return (ntohl(aAddr.inet.ip) & 0xFF000000) == 0x7F000000;
  }
  return aAddr.IsLoopbackAddr();
}

// systemd-resolved (and similar) point /etc/resolv.conf at a loopback stub and
// publish the real upstream servers in /run/systemd/resolve/resolv.conf. Parse
// that file and append its non-loopback nameservers so DDR verification can
// match the actual resolver's certificate.
static void AppendSystemdUpstreamResolvers(nsTArray<NetAddr>& aOut) {
  FILE* f = fopen("/run/systemd/resolve/resolv.conf", "r");
  if (!f) {
    return;
  }
  auto closeFile = MakeScopeExit([f] { fclose(f); });

  char buf[512];
  while (fgets(buf, sizeof(buf), f)) {
    nsAutoCString line(buf);
    line.CompressWhitespace(true, true);
    if (!StringBeginsWith(line, "nameserver "_ns)) {
      continue;
    }

    nsAutoCString ip(Substring(line, sizeof("nameserver ") - 1));
    // Keep only the address token (line was already whitespace-compressed).
    int32_t space = ip.FindChar(' ');
    if (space != kNotFound) {
      ip.Truncate(space);
    }
    // getaddrinfo-style scope suffix (e.g. fe80::1%eth0) can't be parsed.
    int32_t percent = ip.FindChar('%');
    if (percent != kNotFound) {
      ip.Truncate(percent);
    }

    NetAddr addr;
    if (NS_SUCCEEDED(addr.InitFromString(ip)) && !IsLocalStubResolver(addr)) {
      AppendResolverAddr(aOut, addr);
    }
  }
}

// Captures every unencrypted resolver configured on the system (all IPv4 and
// IPv6 nameservers). Used for RFC 9462 DDR verification: any of these resolvers
// may have answered the query, so verification accepts a designated resolver
// whose certificate covers any one of them.
static void CaptureResolverAddrs(struct __res_state* statp,
                                 nsTArray<NetAddr>& aOut) {
  if (!statp) {
    return;
  }
  size_t start = aOut.Length();
  for (int i = 0; i < statp->nscount && i < MAXNS; i++) {
    const auto* sa =
        reinterpret_cast<const struct sockaddr*>(&statp->nsaddr_list[i]);
#if defined(__GLIBC__)
    // glibc stores IPv6 servers in _u._ext.nsaddrs at the same index as their
    // nsaddr_list entry, whose family is then left unset.
    if (sa->sa_family != AF_INET && statp->_u._ext.nsaddrs[i]) {
      sa = reinterpret_cast<const struct sockaddr*>(statp->_u._ext.nsaddrs[i]);
    }
#endif
    NetAddr addr;
    if (SockaddrToNetAddr(sa, addr)) {
      AppendResolverAddr(aOut, addr);
    }
  }

  // If the configured resolvers are all local stubs (e.g. systemd-resolved's
  // 127.0.0.53), res_ninit can't see the real upstream servers. Look them up
  // from systemd-resolved's uplink resolv.conf instead.
  bool allStubs = aOut.Length() > start;
  for (size_t i = start; i < aOut.Length(); i++) {
    if (!IsLocalStubResolver(aOut[i])) {
      allStubs = false;
      break;
    }
  }
  if (allStubs) {
    AppendSystemdUpstreamResolvers(aOut);
  }
}

nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias, uint16_t aType,
                                nsTArray<NetAddr>* aResolverAddrs) {
  DNSPacket packet;
  nsAutoCString host(aHost);
  nsAutoCString cname;
  nsresult rv;

  if (xpc::IsInAutomation() &&
      !StaticPrefs::network_dns_native_https_query_in_automation()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

#if defined(HAVE_RES_NINIT)
  struct __res_state* res = GetThreadResState();
  if (!res) {
    return NS_ERROR_UNKNOWN_HOST;
  }
#endif

  LOG("resolving %s\n", host.get());
  // Perform the query
  rv = packet.FillBuffer(
      [&](unsigned char response[DNSPacket::MAX_SIZE]) -> int {
        int len = 0;
        TimeStamp startTime = TimeStamp::Now();
#if defined(HAVE_RES_NINIT)
        len = res_nquery(res, host.get(), ns_c_in, aType, response,
                         DNSPacket::MAX_SIZE);
#else
        len = res_query(host.get(), ns_c_in, aType, response,
                        DNSPacket::MAX_SIZE);
#endif

        if (aType == nsIDNSService::RESOLVE_TYPE_HTTPSSVC) {
          mozilla::glean::networking::dns_native_https_call_time
              .AccumulateRawDuration(TimeStamp::Now() - startTime);
        }
        if (len < 0) {
          LOG("DNS query failed");
        }
        return len;
      });
  if (NS_FAILED(rv)) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (aResolverAddrs) {
#if defined(HAVE_RES_NINIT)
    CaptureResolverAddrs(res, *aResolverAddrs);
#else
    CaptureResolverAddrs(&_res, *aResolverAddrs);
#endif
  }

  packet.SetOriginHost(aOriginHost);
  return ParseHTTPSRecord(host, packet, aResult, aTTL, aAlias, aType);
}

void DNSThreadShutdown() {
#if defined(HAVE_RES_NINIT)
  auto* res = sThreadRes.get();
  if (!res) {
    return;
  }

  sThreadRes.set(nullptr);
  {
    StaticMutexAutoLock lock(sMutex);
    CloseResState(res);
  }
  delete res;
#endif
}

}  // namespace mozilla::net
