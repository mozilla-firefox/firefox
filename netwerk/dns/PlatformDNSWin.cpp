/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GetAddrInfo.h"
#include "mozilla/Maybe.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"

#ifdef DNSQUERY_AVAILABLE
// There is a bug in windns.h where the type of parameter ppQueryResultsSet for
// DnsQuery_A is dependent on UNICODE being set. It should *always* be
// PDNS_RECORDA, but if UNICODE is set it is PDNS_RECORDW. To get around this
// we make sure that UNICODE is unset.
#  undef UNICODE
#  include <ws2tcpip.h>
#  undef GetAddrInfo
#  include <iphlpapi.h>
#  include <windns.h>
#endif  // DNSQUERY_AVAILABLE

namespace mozilla::net {

#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))

// Captures every unencrypted resolver configured on the system by enumerating
// the DNS servers of all operational network adapters. Used for RFC 9462 DDR
// verification: any of these resolvers may have answered the query, so
// verification accepts a designated resolver whose certificate covers any one
// of them.
static void CaptureResolverAddrs(nsTArray<NetAddr>& aOut) {
  ULONG bufLen = 15000;
  UniquePtr<uint8_t[]> buf;
  ULONG ret = ERROR_BUFFER_OVERFLOW;
  for (int attempts = 0; attempts < 3 && ret == ERROR_BUFFER_OVERFLOW;
       attempts++) {
    buf = std::make_unique<uint8_t[]>(bufLen);
    ret = GetAdaptersAddresses(
        AF_UNSPEC,
        GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_FRIENDLY_NAME,
        nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get()), &bufLen);
  }
  if (ret != ERROR_SUCCESS) {
    LOG("GetAdaptersAddresses failed: %lu", ret);
    return;
  }

  for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get());
       adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp) {
      continue;
    }
    for (auto* dns = adapter->FirstDnsServerAddress; dns; dns = dns->Next) {
      NetAddr addr;
      if (SockaddrToNetAddr(dns->Address.lpSockaddr, addr)) {
        AppendResolverAddr(aOut, addr);
      }
    }
  }
}

nsresult ParseHTTPSRecordList(const DNS_RECORDA* aRecords,
                              const nsACString& aHost,
                              const Maybe<nsCString>& aOriginHost,
                              TypeRecordResultType& aResult, uint32_t& aTTL,
                              HTTPSAliasTarget& aAlias, uint16_t aType) {
  nsAutoCString host(aHost);
  nsAutoCString cname;
  // True when |cname| came from an HTTPS AliasMode record rather than a CNAME.
  bool cnameIsAlias = false;
  aTTL = UINT32_MAX;

  auto CheckRecords = [&aResult, &cname, &cnameIsAlias, &aTTL, &aOriginHost,
                       aType](const DNS_RECORDA* result,
                              const nsCString& aHost) -> nsresult {
    for (const DNS_RECORDA* current = result; current;
         current = current->pNext) {
      if (!current->pName ||
          !DNSPacket::IsSameName(nsDependentCString(current->pName), aHost)) {
        continue;
      }
      if (current->wType == aType) {
        const unsigned char* ptr = (const unsigned char*)&(current->Data);
        struct SVCB parsed;
        nsresult rv = DNSPacket::ParseHTTPS(current->wDataLength, parsed, 0,
                                            ptr, current->wDataLength,
                                            aOriginHost ? *aOriginHost : aHost);
        if (NS_FAILED(rv)) {
          return rv;
        }

        if (parsed.mSvcDomainName.IsEmpty() && parsed.mSvcFieldPriority == 0) {
          // For AliasMode SVCB RRs, a TargetName of "." indicates that the
          // service is not available or does not exist.
          continue;
        }

        if (parsed.mSvcFieldPriority == 0) {
          // Alias form SvcDomainName must not have the "." value (empty)
          if (parsed.mSvcDomainName.IsEmpty()) {
            return NS_ERROR_UNEXPECTED;
          }
          cname = parsed.mSvcDomainName;
          cnameIsAlias = true;
          ToLowerCase(cname);
          break;
        }

        if (!aResult.is<TypeRecordHTTPSSVC>()) {
          aResult = mozilla::AsVariant(CopyableTArray<SVCB>());
        }
        auto& results = aResult.as<TypeRecordHTTPSSVC>();
        results.AppendElement(parsed);
        aTTL = std::min<uint32_t>(aTTL, current->dwTtl);
      } else if (current->wType == DNS_TYPE_CNAME) {
        cname = current->Data.Cname.pNameHost;
        cnameIsAlias = false;
        ToLowerCase(cname);
        aTTL = std::min<uint32_t>(aTTL, current->dwTtl);
        break;
      }
    }
    return NS_OK;
  };

  int32_t loopCount = 64;
  while (loopCount > 0 && aResult.is<Nothing>()) {
    loopCount--;

    nsresult rv = CheckRecords(aRecords, host);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (aResult.is<Nothing>() && !cname.IsEmpty()) {
      // AliasMode/CNAME target. Its records may be chained within this same
      // response; otherwise the caller re-queries aAlias.mName.
      aAlias.mFromAliasMode = aAlias.mFromAliasMode || cnameIsAlias;
      aAlias.mName = cname;
      host = cname;
      cname.Truncate();
      continue;
    }

    if (aResult.is<Nothing>()) {
      break;
    }
  }

  // CNAME loop
  if (loopCount == 0) {
    // Don't leave a stale alias target behind for the caller to follow.
    aAlias = HTTPSAliasTarget{};
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (aResult.is<Nothing>()) {
    if (!aAlias.mName.IsEmpty()) {
      // We resolved to an alias but its target wasn't in this response; the
      // caller will issue a fresh lookup for it.
      return NS_OK;
    }
    // The call succeeded, but no HTTPS records were found.
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (aTTL == UINT32_MAX) {
    aTTL = 60;  // Defaults to 60 seconds
  }
  return NS_OK;
}

nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias, uint16_t aType,
                                nsTArray<NetAddr>* aResolverAddrs) {
  nsAutoCString host(aHost);
  PDNS_RECORD result = nullptr;

  if (xpc::IsInAutomation() &&
      !StaticPrefs::network_dns_native_https_query_in_automation()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  TimeStamp startTime = TimeStamp::Now();

  DNS_STATUS status = DnsQuery_A(host.get(), aType, DNS_QUERY_STANDARD, nullptr,
                                 &result, nullptr);

  if (aType == nsIDNSService::RESOLVE_TYPE_HTTPSSVC) {
    mozilla::glean::networking::dns_native_https_call_time
        .AccumulateRawDuration(TimeStamp::Now() - startTime);
  }

  if (status != ERROR_SUCCESS) {
    LOG("DnsQuery_A failed with error: %ld\n", status);
    return NS_ERROR_UNKNOWN_HOST;
  }

  auto freeDnsRecord =
      MakeScopeExit([&]() { DnsRecordListFree(result, DnsFreeRecordList); });

  if (aResolverAddrs) {
    CaptureResolverAddrs(*aResolverAddrs);
  }

  return ParseHTTPSRecordList(result, host, aOriginHost, aResult, aTTL, aAlias,
                              aType);
}

void DNSThreadShutdown() {}

}  // namespace mozilla::net
