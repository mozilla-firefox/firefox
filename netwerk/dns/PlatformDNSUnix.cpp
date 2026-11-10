/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <netinet/in.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "GetAddrInfo.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"

namespace mozilla::net {

#if defined(HAVE_RES_NINIT)
MOZ_THREAD_LOCAL(struct __res_state*) sThreadRes;
mozilla::StaticMutex sMutex MOZ_ANNOTATED;
#endif

#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))

nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias) {
  DNSPacket packet;
  nsAutoCString host(aHost);
  nsAutoCString cname;
  nsresult rv;

  if (xpc::IsInAutomation() &&
      !StaticPrefs::network_dns_native_https_query_in_automation()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

#if defined(HAVE_RES_NINIT)
  if (!sThreadRes.get()) {
    auto resState = std::make_unique<struct __res_state>();
    memset(resState.get(), 0, sizeof(struct __res_state));
    {
      StaticMutexAutoLock lock(sMutex);
      if (int ret = res_ninit(resState.get())) {
        LOG("res_ninit failed: %d", ret);
        return NS_ERROR_UNKNOWN_HOST;
      }
    }
    sThreadRes.set(resState.release());
  }
#endif

  LOG("resolving %s\n", host.get());
  // Perform the query
  rv = packet.FillBuffer(
      [&](unsigned char response[DNSPacket::MAX_SIZE]) -> int {
        int len = 0;
        TimeStamp startTime = TimeStamp::Now();
#if defined(HAVE_RES_NINIT)
        len = res_nquery(sThreadRes.get(), host.get(), ns_c_in,
                         nsIDNSService::RESOLVE_TYPE_HTTPSSVC, response,
                         DNSPacket::MAX_SIZE);
#else
        len =
            res_query(host.get(), ns_c_in, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
                      response, DNSPacket::MAX_SIZE);
#endif

        mozilla::glean::networking::dns_native_https_call_time
            .AccumulateRawDuration(TimeStamp::Now() - startTime);
        if (len < 0) {
          LOG("DNS query failed");
        }
        return len;
      });
  if (NS_FAILED(rv)) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  packet.SetOriginHost(aOriginHost);
  return ParseHTTPSRecord(host, packet, aResult, aTTL, aAlias);
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
    res_nclose(res);
  }
  free(res);
#endif
}

}  // namespace mozilla::net
