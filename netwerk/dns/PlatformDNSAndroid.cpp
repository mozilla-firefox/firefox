/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <android/multinetwork.h>
#include <netinet/in.h>
#include <poll.h>
#include <resolv.h>

#include "GetAddrInfo.h"
#include "mozilla/Atomics.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"

namespace mozilla::net {

// The first call to ResolveHTTPSRecordImpl will load the library
// and function pointers.
static Atomic<bool> sLibLoading{false};

// https://developer.android.com/ndk/reference/group/networking#android_res_nquery
// The function android_res_nquery is defined in <android/multinetwork.h>
typedef int (*android_res_nquery_ptr)(net_handle_t network, const char* dname,
                                      int ns_class, int ns_type,
                                      uint32_t flags);
static Atomic<android_res_nquery_ptr> sAndroidResNQuery;

// https://developer.android.com/ndk/reference/group/networking#android_res_nresult
// The function android_res_nresult is defined in <android/multinetwork.h>
typedef int (*android_res_nresult_ptr)(int fd, int* rcode, uint8_t* answer,
                                       size_t anslen);
static Atomic<android_res_nresult_ptr> sAndroidResNResult;

#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))

nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias) {
  DNSPacket packet;
  nsAutoCString host(aHost);
  nsresult rv;

  if (xpc::IsInAutomation() &&
      !StaticPrefs::network_dns_native_https_query_in_automation()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (!sLibLoading.exchange(true)) {
    // We're the first call here, load the library and symbols.
    if (__builtin_available(android 29, *)) {
      sAndroidResNQuery = android_res_nquery;    // API 29
      sAndroidResNResult = android_res_nresult;  // API 29
    } else {
      LOG("No android_res_nquery symbol");
    }
  }

  if (!sAndroidResNQuery || !sAndroidResNResult) {
    LOG("nquery not loaded");
    // The library hasn't been loaded yet.
    return NS_ERROR_UNKNOWN_HOST;
  }

  LOG("resolving %s\n", host.get());
  TimeStamp startTime = TimeStamp::Now();
  // Perform the query
  rv = packet.FillBuffer(
      [&](unsigned char response[DNSPacket::MAX_SIZE]) -> int {
        int fd = 0;
        auto closeSocket = MakeScopeExit([&] {
          if (fd > 0) {
            close(fd);
          }
        });
        uint32_t flags = 0;
        if (aFlags & nsIDNSService::RESOLVE_BYPASS_CACHE) {
          flags = ANDROID_RESOLV_NO_CACHE_LOOKUP;
        }
        fd = sAndroidResNQuery(0, host.get(), ns_c_in,
                               nsIDNSService::RESOLVE_TYPE_HTTPSSVC, flags);

        if (fd < 0) {
          LOG("DNS query failed");
          return fd;
        }

        struct pollfd fds;
        fds.fd = fd;
        fds.events = POLLIN;  // Wait for read events

        // Wait for an event on the file descriptor
        int ret = poll(&fds, 1,
                       StaticPrefs::network_dns_native_https_timeout_android());
        if (ret <= 0) {
          LOG("poll failed %d", ret);
          return -1;
        }

        // android_res_nresult reads the DNS answer and closes the fd, so
        // disarm the scope exit to avoid closing it a second time.
        int rcode = 0;
        int len =
            sAndroidResNResult(fd, &rcode, response, DNSPacket::MAX_SIZE - 1);
        fd = -1;
        if (len < 0) {
          LOG("android_res_nresult failed %d", len);
          return len;
        }

        return len;
      });
  mozilla::glean::networking::dns_native_https_call_time.AccumulateRawDuration(
      TimeStamp::Now() - startTime);
  if (NS_FAILED(rv)) {
    LOG("failed rv");
    return rv;
  }

  packet.SetOriginHost(aOriginHost);
  return ParseHTTPSRecord(host, packet, aResult, aTTL, aAlias);
}

void DNSThreadShutdown() {}

}  // namespace mozilla::net
