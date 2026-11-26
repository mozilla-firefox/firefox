/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PlatformDNSMac.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>

#include "GetAddrInfo.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSPacket.h"
#include "nsIDNSService.h"

namespace mozilla::net {

#define LOG(msg, ...) \
  MOZ_LOG(gGetAddrInfoLog, LogLevel::Debug, ("[DNS]: " msg, ##__VA_ARGS__))

// Callback for DNSServiceQueryRecord
void QueryCallback(DNSServiceRef aSDRef, DNSServiceFlags aFlags,
                   uint32_t aInterfaceIndex, DNSServiceErrorType aErrorCode,
                   const char* aFullname, uint16_t aRRType, uint16_t aRRClass,
                   uint16_t aRDLen, const void* aRdata, uint32_t aTtl,
                   void* aContext) {
  struct DNSContext* context = (struct DNSContext*)aContext;

  LOG("DNS response name: %s type: %u rdlen %u class %u ttl %u", aFullname,
      aRRType, aRDLen, aRRClass, aTtl);

  if (aErrorCode != kDNSServiceErr_NoError) {
    LOG("Error resolving record: %d\n", aErrorCode);
    context->mRv = NS_ERROR_UNKNOWN_HOST;
    return;
  }

  if (NS_FAILED(context->mRv)) {
    LOG("Parsing already failed for a previous record");
    return;
  }

  // Skip intermediate records (e.g. CNAMEs) returned when
  // kDNSServiceFlagsReturnIntermediates is set.
  if (aRRType != TRRTYPE_HTTPSSVC) {
    return;
  }

  context->mDone = !(aFlags & kDNSServiceFlagsMoreComing);

  // Process the rdata for HTTPS records (type 65)
  if (aRDLen == 0) {
    context->mRv = NS_ERROR_UNKNOWN_HOST;
    return;
  }

  auto fullname = Substring(nsDependentCString(aFullname), 0, -1);
  if (fullname.Length() && fullname.Last() == '.') {
    // The fullname argument is always FQDN
    fullname.Rebind(aFullname, fullname.Length() - 1);
  }

  const nsACString& origin =
      context->mOriginHost
          ? static_cast<const nsACString&>(*context->mOriginHost)
          : static_cast<const nsACString&>(fullname);

  struct SVCB parsed;
  nsresult rv = DNSPacket::ParseHTTPS(
      aRDLen, parsed, 0, (const unsigned char*)aRdata, aRDLen, origin);
  if (NS_FAILED(rv)) {
    LOG("ParseHTTPS failed\n");
    context->mRv = rv;
    return;
  }

  if (parsed.mSvcDomainName.IsEmpty() && parsed.mSvcFieldPriority == 0) {
    // For AliasMode SVCB RRs, a TargetName of "." indicates that the
    // service is not available or does not exist.
    return;
  }

  if (parsed.mSvcFieldPriority == 0) {
    // Alias form SvcDomainName must not have the "." value (empty)
    if (parsed.mSvcDomainName.IsEmpty()) {
      context->mRv = NS_ERROR_UNEXPECTED;
      return;
    }
    LOG("alias mode %s -> %s", context->mHost.get(),
        parsed.mSvcDomainName.get());
    context->mAliasName = parsed.mSvcDomainName;
    ToLowerCase(context->mAliasName);
    return;
  }

  if (!context->mResult->is<TypeRecordHTTPSSVC>()) {
    *context->mResult = mozilla::AsVariant(CopyableTArray<SVCB>());
  }
  auto& results = context->mResult->as<TypeRecordHTTPSSVC>();
  results.AppendElement(parsed);
  *context->mTTL = std::min<uint32_t>(*context->mTTL, aTtl);
}

nsresult ResolveHTTPSRecordImpl(const nsACString& aHost,
                                const Maybe<nsCString>& aOriginHost,
                                nsIDNSService::DNSFlags aFlags,
                                TypeRecordResultType& aResult, uint32_t& aTTL,
                                HTTPSAliasTarget& aAlias) {
  nsAutoCString host(aHost);
  nsAutoCString cname;

  if (xpc::IsInAutomation() &&
      !StaticPrefs::network_dns_native_https_query_in_automation()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  LOG("resolving %s\n", host.get());
  TimeStamp startTime = TimeStamp::Now();

  struct DNSContext context{
      .mResult = &aResult,
      .mHost = host,
      .mOriginHost = aOriginHost,
      .mTTL = &aTTL,
  };

  DNSServiceRef sdRef;
  DNSServiceErrorType err;

  err = DNSServiceQueryRecord(&sdRef, kDNSServiceFlagsReturnIntermediates,
                              0,  // All interfaces
                              host.get(), TRRTYPE_HTTPSSVC, kDNSServiceClass_IN,
                              QueryCallback, &context);

  if (err != kDNSServiceErr_NoError) {
    LOG("DNSServiceQueryRecord failed: %d\n", err);
    return NS_ERROR_UNKNOWN_HOST;
  }

  struct pollfd pfd = {
      .fd = DNSServiceRefSockFD(sdRef),
      .events = POLLIN,
  };

  // If the domain queried results in NXDOMAIN, then QueryCallback will
  // never get called, and poll will hang forever. We need to use a
  // timeout so that poll() eventually returns.
  // DNSServiceProcessResult only handles the replies that have already
  // arrived, and intermediate CNAMEs come before the HTTPS records, so keep
  // processing until the HTTPS RRSet has been delivered.
  TimeStamp deadline =
      startTime + TimeDuration::FromMilliseconds(
                      StaticPrefs::network_dns_native_https_timeout_mac_msec());
  while (!context.IsComplete()) {
    int timeout =
        static_cast<int>((deadline - TimeStamp::Now()).ToMilliseconds());
    if (timeout <= 0) {
      LOG("poll timed out");
      break;
    }
    int result = poll(&pfd, 1, timeout);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG("poll() failed");
      break;
    }
    if (result == 0) {
      LOG("poll timed out");
      break;
    }
    if (!(pfd.revents & POLLIN)) {
      LOG("poll() returned revents %d", pfd.revents);
      break;
    }
    err = DNSServiceProcessResult(sdRef);
    if (err != kDNSServiceErr_NoError) {
      LOG("DNSServiceProcessResult failed: %d", err);
      break;
    }
  }

  // Cleanup
  DNSServiceRefDeallocate(sdRef);
  mozilla::glean::networking::dns_native_https_call_time.AccumulateRawDuration(
      TimeStamp::Now() - startTime);

  LOG("resolving %s done %x ttl=%u", host.get(), context.mRv, aTTL);
  if (NS_FAILED(context.mRv)) {
    return context.mRv;
  }
  if (aResult.is<Nothing>()) {
    if (!context.mAliasName.IsEmpty()) {
      // The response was an HTTPS AliasMode record (intermediate CNAMEs are
      // ignored by QueryCallback); hand the target back so the caller can
      // issue a fresh lookup for it.
      aAlias.mName = context.mAliasName;
      aAlias.mFromAliasMode = true;
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

void DNSThreadShutdown() {}

}  // namespace mozilla::net
