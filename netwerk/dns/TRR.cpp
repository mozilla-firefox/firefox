/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DNS.h"
#include "DNSUtils.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsHttpHandler.h"
#include "nsHttpChannel.h"
#include "nsHostResolver.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIIOService.h"
#include "nsIInputStream.h"
#include "nsIObliviousHttp.h"
#include "nsIOService.h"
#include "nsISupports.h"
#include "nsISupportsUtils.h"
#include "nsITimedChannel.h"
#include "nsIUploadChannel2.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "ObliviousHttpChannel.h"
#include "TRR.h"
#include "TRRService.h"
#include "TRRServiceChannel.h"
#include "TRRLoadInfo.h"

#include "mozilla/Base64.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkDnsMetrics.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/UniquePtr.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"
#include "mozilla/glean/NetwerkMetrics.h"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS_INHERITED(TRR, Runnable, nsIStreamListener, nsITimerCallback,
                            nsIRunnablePriority)

NS_IMETHODIMP
TRR::GetPriority(uint32_t* aPriority) {
  if (StaticPrefs::network_trr_high_priority_events()) {
    *aPriority = nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
  } else {
    *aPriority = nsIRunnablePriority::PRIORITY_NORMAL;
  }
  return NS_OK;
}

// static
already_AddRefed<nsIEventTarget> TRR::DefaultTarget() {
  if (!TRRService::Get()) {
    return nullptr;
  }
  return TRRService::Get()->MainThreadOrTRRTarget();
}

// when firing off a normal A or AAAA query
TRR::TRR(AHostResolver* aResolver, nsHostRecord* aRec, enum TrrType aType)
    : mozilla::Runnable("TRR"),
      mRec(aRec),
      mHostResolver(aResolver),
      mType(aType),
      mOriginSuffix(aRec->originSuffix),
      mTarget(DefaultTarget()) {
  mHost = aRec->host;
  mPB = aRec->pb;
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess() || XRE_IsSocketProcess(),
                        "TRR must be in parent or socket process");
}

// when following CNAMEs
TRR::TRR(AHostResolver* aResolver, nsHostRecord* aRec, nsCString& aHost,
         enum TrrType& aType, unsigned int aLoopCount, bool aPB)
    : mozilla::Runnable("TRR"),
      mHost(aHost),
      mRec(aRec),
      mHostResolver(aResolver),
      mType(aType),
      mPB(aPB),
      mCnameLoop(aLoopCount),
      mOriginSuffix(aRec ? aRec->originSuffix : ""_ns),
      mTarget(DefaultTarget()) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess() || XRE_IsSocketProcess(),
                        "TRR must be in parent or socket process");
}

// to verify a domain
TRR::TRR(AHostResolver* aResolver, nsACString& aHost, enum TrrType aType,
         const nsACString& aOriginSuffix, bool aPB, bool aUseFreshConnection,
         nsIEventTarget* aTarget)
    : mozilla::Runnable("TRR"),
      mHost(aHost),
      mRec(nullptr),
      mHostResolver(aResolver),
      mType(aType),
      mPB(aPB),
      mOriginSuffix(aOriginSuffix),
      mUseFreshConnection(aUseFreshConnection),
      mTarget(aTarget) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess() || XRE_IsSocketProcess(),
                        "TRR must be in parent or socket process");
}

void TRR::HandleTimeout() {
  mTimeout = nullptr;
  RecordReason(TRRSkippedReason::TRR_TIMEOUT);
  Cancel(NS_ERROR_NET_TIMEOUT_EXTERNAL);
}

NS_IMETHODIMP
TRR::Notify(nsITimer* aTimer) {
  if (aTimer == mTimeout) {
    HandleTimeout();
  } else {
    MOZ_CRASH("Unknown timer");
  }

  return NS_OK;
}

NS_IMETHODIMP
TRR::Run() {
  MOZ_ASSERT_IF(XRE_IsParentProcess() && TRRService::Get(),
                NS_IsMainThread() || TRRService::Get()->IsOnTRRThread());
  MOZ_ASSERT_IF(XRE_IsSocketProcess(), NS_IsMainThread());

  if ((TRRService::Get() == nullptr) || NS_FAILED(SendHTTPRequest())) {
    RecordReason(TRRSkippedReason::TRR_SEND_FAILED);
    FailData(NS_ERROR_FAILURE);
    // The dtor will now be run
  }
  return NS_OK;
}

DNSPacket* TRR::GetOrCreateDNSPacket() {
  if (!mPacket) {
    mPacket = MakeUnique<DNSPacket>();
  }

  return mPacket.get();
}

nsresult TRR::CreateQueryURI(nsIURI** aOutURI) {
  nsAutoCString uri;
  nsCOMPtr<nsIURI> dnsURI;
  if (UseDefaultServer()) {
    TRRService::Get()->GetURI(uri);
  } else {
    uri = mRec->mTrrServer;
  }

  nsresult rv = NS_NewURI(getter_AddRefs(dnsURI), uri);
  if (NS_FAILED(rv)) {
    RecordReason(TRRSkippedReason::TRR_BAD_URL);
    return rv;
  }

  dnsURI.forget(aOutURI);
  return NS_OK;
}

bool TRR::MaybeBlockRequest() {
  if (((mType == TRRTYPE_A) || (mType == TRRTYPE_AAAA)) &&
      mRec->mEffectiveTRRMode != nsIRequest::TRR_ONLY_MODE) {
    // let NS resolves skip the blocklist check
    // we also don't check the blocklist for TRR only requests
    MOZ_ASSERT(mRec);

    // If TRRService isn't enabled anymore for the req, don't do TRR.
    if (!TRRService::Get()->Enabled(mRec->mEffectiveTRRMode)) {
      RecordReason(TRRSkippedReason::TRR_MODE_NOT_ENABLED);
      return true;
    }

    if (!StaticPrefs::network_trr_strict_native_fallback() &&
        UseDefaultServer() &&
        TRRService::Get()->IsTemporarilyBlocked(mHost, mOriginSuffix, mPB,
                                                true)) {
      if (mType == TRRTYPE_A) {
        // count only blocklist for A records to avoid double counts
        glean::dns::trr_blacklisted.Get(TRRService::ProviderKey(), "true"_ns)
            .Add();
      }

      RecordReason(TRRSkippedReason::TRR_HOST_BLOCKED_TEMPORARY);
      // not really an error but no TRR is issued
      return true;
    }

    if (TRRService::Get()->IsExcludedFromTRR(mHost, mRec->mEffectiveTRRMode)) {
      RecordReason(TRRSkippedReason::TRR_EXCLUDED);
      return true;
    }

    if (UseDefaultServer() && (mType == TRRTYPE_A)) {
      glean::dns::trr_blacklisted.Get(TRRService::ProviderKey(), "false"_ns)
          .Add();
    }
  }

  return false;
}

nsresult TRR::SendHTTPRequest() {
  // This is essentially the "run" method - created from nsHostResolver
  if (mCancelled) {
    return NS_ERROR_FAILURE;
  }

  if ((mType != TRRTYPE_A) && (mType != TRRTYPE_AAAA) &&
      (mType != TRRTYPE_NS) && (mType != TRRTYPE_TXT) &&
      (mType != TRRTYPE_HTTPSSVC)) {
    // limit the calling interface because nsHostResolver has explicit slots for
    // these types
    return NS_ERROR_FAILURE;
  }

  if (MaybeBlockRequest()) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  LOG(("TRR::SendHTTPRequest resolve %s type %u\n", mHost.get(), mType));

  nsAutoCString body;
  bool disableECS = StaticPrefs::network_trr_disable_ECS();
  nsresult rv =
      GetOrCreateDNSPacket()->EncodeRequest(body, mHost, mType, disableECS);
  if (NS_FAILED(rv)) {
    HandleEncodeError(rv);
    return rv;
  }

  bool useGet = StaticPrefs::network_trr_useGET();
  nsCOMPtr<nsIURI> dnsURI;
  rv = CreateQueryURI(getter_AddRefs(dnsURI));
  if (NS_FAILED(rv)) {
    LOG(("TRR:SendHTTPRequest: NewURI failed!\n"));
    return rv;
  }

  if (useGet) {
    /* For GET requests, the outgoing packet needs to be Base64url-encoded and
       then appended to the end of the URI. */
    nsAutoCString encoded;
    rv = Base64URLEncode(body.Length(),
                         reinterpret_cast<const unsigned char*>(body.get()),
                         Base64URLEncodePaddingPolicy::Omit, encoded);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString query;
    rv = dnsURI->GetQuery(query);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (query.IsEmpty()) {
      query.Assign("?dns="_ns);
    } else {
      query.Append("&dns="_ns);
    }
    query.Append(encoded);

    rv = NS_MutateURI(dnsURI).SetQuery(query).Finalize(dnsURI);
    LOG(("TRR::SendHTTPRequest GET dns=%s\n", body.get()));
  }

  nsCOMPtr<nsIChannel> channel;
  bool useOHTTP = StaticPrefs::network_trr_use_ohttp();
  if (useOHTTP) {
    nsCOMPtr<nsIObliviousHttpService> ohttpService(
        do_GetService("@mozilla.org/network/oblivious-http-service;1"));
    if (!ohttpService) {
      return NS_ERROR_FAILURE;
    }
    nsCOMPtr<nsIURI> relayURI;
    nsTArray<uint8_t> encodedConfig;
    rv = ohttpService->GetTRRSettings(getter_AddRefs(relayURI), encodedConfig);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!relayURI) {
      return NS_ERROR_FAILURE;
    }
    rv = ohttpService->NewChannel(relayURI, dnsURI, encodedConfig,
                                  getter_AddRefs(channel));
  } else {
    rv = DNSUtils::CreateChannelHelper(dnsURI, getter_AddRefs(channel));
  }
  if (NS_FAILED(rv) || !channel) {
    LOG(("TRR:SendHTTPRequest: NewChannel failed!\n"));
    return rv;
  }

  auto loadFlags = nsIRequest::LOAD_ANONYMOUS | nsIRequest::INHIBIT_CACHING |
                   nsIRequest::LOAD_BYPASS_CACHE |
                   nsIChannel::LOAD_BYPASS_URL_CLASSIFIER;
  if (mUseFreshConnection) {
    // Causes TRRServiceChannel to tell the connection manager
    // to clear out any connection with the current conn info.
    loadFlags |= nsIRequest::LOAD_FRESH_CONNECTION;
  }
  channel->SetLoadFlags(loadFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel);
  if (!httpChannel) {
    return NS_ERROR_UNEXPECTED;
  }

  // This connection should not use TRR
  rv = httpChannel->SetTRRMode(nsIRequest::TRR_DISABLED_MODE);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString contentType(ContentType());
  rv = httpChannel->SetRequestHeader("Accept"_ns, contentType, false);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString cred;
  if (UseDefaultServer()) {
    TRRService::Get()->GetCredentials(cred);
  }
  if (!cred.IsEmpty()) {
    rv = httpChannel->SetRequestHeader("Authorization"_ns, cred, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIHttpChannelInternal> internalChannel = do_QueryInterface(channel);
  if (!internalChannel) {
    return NS_ERROR_UNEXPECTED;
  }

  // setting a small stream window means the h2 stack won't pipeline a window
  // update with each HEADERS or reply to a DATA with a WINDOW UPDATE
  rv = internalChannel->SetInitialRwin(127 * 1024);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = internalChannel->SetIsTRRServiceChannel(true);
  NS_ENSURE_SUCCESS(rv, rv);

  // When using OHTTP, the we can't use cached connection info, since we
  // need to connect to the relay, not the TRR server.
  if (UseDefaultServer() && !useOHTTP &&
      StaticPrefs::network_trr_async_connInfo()) {
    RefPtr<nsHttpConnectionInfo> trrConnInfo =
        TRRService::Get()->TRRConnectionInfo();
    if (trrConnInfo) {
      nsAutoCString host;
      dnsURI->GetHost(host);
      if (host.Equals(trrConnInfo->GetOrigin())) {
        internalChannel->SetConnectionInfo(trrConnInfo);
        LOG(("TRR::SendHTTPRequest use conn info:%s\n",
             trrConnInfo->HashKey().get()));
      } else {
        // The connection info is inconsistent. Avoid using it and generate a
        // new one.
        TRRService::Get()->SetDefaultTRRConnectionInfo(nullptr);
        TRRService::Get()->InitTRRConnectionInfo(true);
      }
    } else {
      TRRService::Get()->InitTRRConnectionInfo();
    }
  }

  if (useGet) {
    rv = httpChannel->SetRequestMethod("GET"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(httpChannel);
    if (!uploadChannel) {
      return NS_ERROR_UNEXPECTED;
    }
    uint32_t streamLength = body.Length();
    nsCOMPtr<nsIInputStream> uploadStream;
    rv =
        NS_NewCStringInputStream(getter_AddRefs(uploadStream), std::move(body));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = uploadChannel->ExplicitSetUploadStream(uploadStream, contentType,
                                                streamLength, "POST"_ns, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = SetupTRRServiceChannelInternal(httpChannel, useGet, contentType);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = httpChannel->AsyncOpen(this);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // If the asyncOpen succeeded we can say that we actually attempted to
  // use the TRR connection.
  if (mRec) {
    mRec->mResolverType = ResolverType();
  }

  NS_NewTimerWithCallback(
      getter_AddRefs(mTimeout), this,
      mTimeoutMs ? mTimeoutMs : TRRService::Get()->GetRequestTimeout(),
      nsITimer::TYPE_ONE_SHOT);

  mChannel = std::move(channel);
  return NS_OK;
}

// static
nsresult TRR::SetupTRRServiceChannelInternal(nsIHttpChannel* aChannel,
                                             bool aUseGet,
                                             const nsACString& aContentType) {
  nsCOMPtr<nsIHttpChannel> httpChannel = aChannel;
  MOZ_ASSERT(httpChannel);

  nsresult rv = NS_OK;
  if (!aUseGet) {
    rv =
        httpChannel->SetRequestHeader("Cache-Control"_ns, "no-store"_ns, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Sanitize the request by removing the Accept-Language header so we minimize
  // the amount of fingerprintable information we send to the server.
  if (!StaticPrefs::network_trr_send_accept_language_headers()) {
    rv = httpChannel->SetRequestHeader("Accept-Language"_ns, ""_ns, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Sanitize the request by removing the User-Agent
  if (!StaticPrefs::network_trr_send_user_agent_headers()) {
    rv = httpChannel->SetRequestHeader("User-Agent"_ns, ""_ns, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (StaticPrefs::network_trr_send_empty_accept_encoding_headers()) {
    rv = httpChannel->SetEmptyRequestHeader("Accept-Encoding"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // set the *default* response content type
  if (NS_FAILED(httpChannel->SetContentType(aContentType))) {
    LOG(("TRR::SetupTRRServiceChannelInternal: couldn't set content-type!\n"));
  }

  return NS_OK;
}

NS_IMETHODIMP
TRR::OnStartRequest(nsIRequest* aRequest) {
  LOG(("TRR::OnStartRequest %p %s %d\n", this, mHost.get(), mType));

  nsresult status = NS_OK;
  aRequest->GetStatus(&status);

  if (NS_FAILED(status)) {
    if (gIOService->InSleepMode()) {
      RecordReason(TRRSkippedReason::TRR_SYSTEM_SLEEP_MODE);
    } else if (NS_IsOffline()) {
      RecordReason(TRRSkippedReason::TRR_BROWSER_IS_OFFLINE);
    }

    switch (status) {
      case NS_ERROR_UNKNOWN_HOST:
        RecordReason(TRRSkippedReason::TRR_CHANNEL_DNS_FAIL);
        break;
      case NS_ERROR_OFFLINE:
        RecordReason(TRRSkippedReason::TRR_BROWSER_IS_OFFLINE);
        break;
      case NS_ERROR_NET_RESET:
        RecordReason(TRRSkippedReason::TRR_NET_RESET);
        break;
      case NS_ERROR_NET_TIMEOUT:
      case NS_ERROR_NET_TIMEOUT_EXTERNAL:
        RecordReason(TRRSkippedReason::TRR_NET_TIMEOUT);
        break;
      case NS_ERROR_PROXY_CONNECTION_REFUSED:
        RecordReason(TRRSkippedReason::TRR_NET_REFUSED);
        break;
      case NS_ERROR_NET_INTERRUPT:
        RecordReason(TRRSkippedReason::TRR_NET_INTERRUPT);
        break;
      case NS_ERROR_NET_INADEQUATE_SECURITY:
        RecordReason(TRRSkippedReason::TRR_NET_INADEQ_SEQURITY);
        break;
      default:
        RecordReason(TRRSkippedReason::TRR_UNKNOWN_CHANNEL_FAILURE);
    }
  }

  return NS_OK;
}

void TRR::SaveAdditionalRecords(
    const nsClassHashtable<nsCStringHashKey, DOHresp>& aRecords) {
  if (!mRec) {
    return;
  }
  nsresult rv;
  for (const auto& recordEntry : aRecords) {
    if (!recordEntry.GetData() || recordEntry.GetData()->mAddresses.IsEmpty()) {
      // no point in adding empty records.
      continue;
    }
    // If IPv6 is disabled don't add anything else than IPv4.
    if (StaticPrefs::network_dns_disableIPv6() &&
        std::find_if(recordEntry.GetData()->mAddresses.begin(),
                     recordEntry.GetData()->mAddresses.end(),
                     [](const NetAddr& addr) { return !addr.IsIPAddrV4(); }) !=
            recordEntry.GetData()->mAddresses.end()) {
      continue;
    }
    RefPtr<nsHostRecord> hostRecord;
    rv = mHostResolver->GetHostRecord(
        recordEntry.GetKey(), EmptyCString(),
        nsIDNSService::RESOLVE_TYPE_DEFAULT, mRec->flags, AF_UNSPEC, mRec->pb,
        mRec->originSuffix, getter_AddRefs(hostRecord));
    if (NS_FAILED(rv)) {
      LOG(("Failed to get host record for additional record %s",
           PromiseFlatCString(recordEntry.GetKey()).get()));
      continue;
    }
    RefPtr<AddrInfo> ai(
        new AddrInfo(recordEntry.GetKey(), ResolverType(), TRRTYPE_A,
                     std::move(recordEntry.GetData()->mAddresses),
                     recordEntry.GetData()->mTtl));
    mHostResolver->MaybeRenewHostRecord(hostRecord);

    // Since we're not actually calling NameLookup for this record, we need
    // to set these fields to avoid assertions in CompleteLookup.
    // This is quite hacky, and should be fixed.
    hostRecord->Reset();
    hostRecord->mResolving++;
    hostRecord->mEffectiveTRRMode =
        static_cast<nsIRequest::TRRMode>(mRec->mEffectiveTRRMode);
    LOG(("Completing lookup for additional: %s",
         PromiseFlatCString(recordEntry.GetKey()).get()));
    (void)mHostResolver->CompleteLookup(hostRecord, NS_OK, ai, mPB,
                                        mOriginSuffix, TRRSkippedReason::TRR_OK,
                                        this);
  }
}

void TRR::StoreIPHintAsDNSRecord(const struct SVCB& aSVCBRecord) {
  LOG(("TRR::StoreIPHintAsDNSRecord [%p] [%s]", this,
       aSVCBRecord.mSvcDomainName.get()));
  CopyableTArray<NetAddr> addresses;
  aSVCBRecord.GetIPHints(addresses);

  if (StaticPrefs::network_dns_disableIPv6()) {
    addresses.RemoveElementsBy(
        [](const NetAddr& addr) { return !addr.IsIPAddrV4(); });
  }

  if (addresses.IsEmpty()) {
    return;
  }

  RefPtr<nsHostRecord> hostRecord;
  nsresult rv = mHostResolver->GetHostRecord(
      aSVCBRecord.mSvcDomainName, EmptyCString(),
      nsIDNSService::RESOLVE_TYPE_DEFAULT,
      mRec->flags | nsIDNSService::RESOLVE_IP_HINT, AF_UNSPEC, mRec->pb,
      mRec->originSuffix, getter_AddRefs(hostRecord));
  if (NS_FAILED(rv)) {
    LOG(("Failed to get host record"));
    return;
  }

  mHostResolver->MaybeRenewHostRecord(hostRecord);

  RefPtr<AddrInfo> ai(new AddrInfo(aSVCBRecord.mSvcDomainName, ResolverType(),
                                   TRRTYPE_A, std::move(addresses), mTTL));

  // Since we're not actually calling NameLookup for this record, we need
  // to set these fields to avoid assertions in CompleteLookup.
  // This is quite hacky, and should be fixed.
  hostRecord->mResolving++;
  hostRecord->mEffectiveTRRMode =
      static_cast<nsIRequest::TRRMode>(mRec->mEffectiveTRRMode);
  (void)mHostResolver->CompleteLookup(hostRecord, NS_OK, ai, mPB, mOriginSuffix,
                                      TRRSkippedReason::TRR_OK, this);
}

nsresult TRR::ReturnData(nsIChannel* aChannel) {
  Maybe<TimeDuration> trrFetchDuration;
  Maybe<TimeDuration> trrFetchDurationNetworkOnly;
  // Set timings.
  nsCOMPtr<nsITimedChannel> timedChan = do_QueryInterface(aChannel);
  if (timedChan) {
    TimeStamp asyncOpen, start, end;
    if (NS_SUCCEEDED(timedChan->GetAsyncOpen(&asyncOpen)) &&
        !asyncOpen.IsNull()) {
      trrFetchDuration = Some(TimeStamp::Now() - asyncOpen);
    }
    if (NS_SUCCEEDED(timedChan->GetRequestStart(&start)) &&
        NS_SUCCEEDED(timedChan->GetResponseEnd(&end)) && !start.IsNull() &&
        !end.IsNull()) {
      trrFetchDurationNetworkOnly = Some(end - start);
    }
  }

  if (mType != TRRTYPE_TXT && mType != TRRTYPE_HTTPSSVC) {
    // create and populate an AddrInfo instance to pass on
    RefPtr<AddrInfo> ai(new AddrInfo(mHost, ResolverType(), mType,
                                     nsTArray<NetAddr>(), mDNS.mTtl));
    auto builder = ai->Build();
    builder.SetAddresses(std::move(mDNS.mAddresses));
    builder.SetCanonicalHostname(mCname);
    if (trrFetchDuration) {
      builder.SetTrrFetchDuration((*trrFetchDuration).ToMilliseconds());
    }
    if (trrFetchDurationNetworkOnly) {
      builder.SetTrrFetchDurationNetworkOnly(
          (*trrFetchDurationNetworkOnly).ToMilliseconds());
    }
    ai = builder.Finish();

    if (!mHostResolver) {
      return NS_ERROR_FAILURE;
    }
    RecordReason(TRRSkippedReason::TRR_OK);
    (void)mHostResolver->CompleteLookup(mRec, NS_OK, ai, mPB, mOriginSuffix,
                                        mTRRSkippedReason, this);
    mHostResolver = nullptr;
    mRec = nullptr;
  } else {
    RecordReason(TRRSkippedReason::TRR_OK);
    (void)mHostResolver->CompleteLookupByType(mRec, NS_OK, mResult,
                                              mTRRSkippedReason, mTTL, mPB);
  }

  return NS_OK;
}

nsresult TRR::FailData(nsresult error) {
  if (!mHostResolver) {
    return NS_ERROR_FAILURE;
  }

  // If we didn't record a reason until now, record a default one.
  RecordReason(TRRSkippedReason::TRR_FAILED);

  if (mType == TRRTYPE_TXT || mType == TRRTYPE_HTTPSSVC) {
    TypeRecordResultType empty(Nothing{});
    (void)mHostResolver->CompleteLookupByType(mRec, error, empty,
                                              mTRRSkippedReason, 0, mPB);
  } else {
    // create and populate an TRR AddrInfo instance to pass on to signal that
    // this comes from TRR
    nsTArray<NetAddr> noAddresses;
    RefPtr<AddrInfo> ai =
        new AddrInfo(mHost, ResolverType(), mType, std::move(noAddresses));

    (void)mHostResolver->CompleteLookup(mRec, error, ai, mPB, mOriginSuffix,
                                        mTRRSkippedReason, this);
  }

  mHostResolver = nullptr;
  mRec = nullptr;
  return NS_OK;
}

void TRR::HandleDecodeError(nsresult aStatusCode) {
  auto rcode = mPacket->GetRCode();
  if (rcode.isOk() && rcode.unwrap() != 0) {
    if (rcode.unwrap() == 0x03) {
      RecordReason(TRRSkippedReason::TRR_NXDOMAIN);
    } else {
      RecordReason(TRRSkippedReason::TRR_RCODE_FAIL);
    }
  } else if (aStatusCode == NS_ERROR_UNKNOWN_HOST ||
             aStatusCode == NS_ERROR_DEFINITIVE_UNKNOWN_HOST) {
    RecordReason(TRRSkippedReason::TRR_NO_ANSWERS);
  } else {
    RecordReason(TRRSkippedReason::TRR_DECODE_FAILED);
  }
}

bool TRR::HasUsableResponse() {
  if (mType == TRRTYPE_A || mType == TRRTYPE_AAAA) {
    return !mDNS.mAddresses.IsEmpty();
  }
  if (mType == TRRTYPE_TXT) {
    return mResult.is<TypeRecordTxt>();
  }
  if (mType == TRRTYPE_HTTPSSVC) {
    return mResult.is<TypeRecordHTTPSSVC>();
  }
  return false;
}

nsresult TRR::FollowCname(nsIChannel* aChannel) {
  nsresult rv = NS_OK;
  nsAutoCString cname;
  while (NS_SUCCEEDED(rv) && mDNS.mAddresses.IsEmpty() && !mCname.IsEmpty() &&
         mCnameLoop > 0) {
    mCnameLoop--;
    LOG(("TRR::On200Response CNAME %s => %s (%u)\n", mHost.get(), mCname.get(),
         mCnameLoop));
    cname = mCname;
    mCname.Truncate();

    LOG(("TRR: check for CNAME record for %s within previous response\n",
         cname.get()));
    nsClassHashtable<nsCStringHashKey, DOHresp> additionalRecords;
    rv = GetOrCreateDNSPacket()->Decode(
        cname, mType, mCname, StaticPrefs::network_trr_allow_rfc1918(), mDNS,
        mResult, additionalRecords, mTTL);
    if (NS_FAILED(rv)) {
      LOG(("TRR::FollowCname DohDecode %x\n", (int)rv));
      HandleDecodeError(rv);
    }
  }

  // restore mCname as DohDecode() change it
  mCname = cname;
  if (NS_SUCCEEDED(rv) && HasUsableResponse()) {
    ReturnData(aChannel);
    return NS_OK;
  }

  bool ra = mPacket && mPacket->RecursionAvailable().unwrapOr(false);
  LOG(("ra = %d", ra));
  if (rv == NS_ERROR_UNKNOWN_HOST && ra) {
    // If recursion is available, but no addresses have been returned,
    // we can just return a failure here.
    LOG(("TRR::FollowCname not sending another request as RA flag is set."));
    FailData(NS_ERROR_UNKNOWN_HOST);
    return NS_OK;
  }

  if (!mCnameLoop) {
    LOG(("TRR::On200Response CNAME loop, eject!\n"));
    return NS_ERROR_REDIRECT_LOOP;
  }

  LOG(("TRR::On200Response CNAME %s => %s (%u)\n", mHost.get(), mCname.get(),
       mCnameLoop));
  RefPtr<TRR> trr =
      new TRR(mHostResolver, mRec, mCname, mType, mCnameLoop, mPB);
  trr->SetPurpose(mPurpose);
  if (!TRRService::Get()) {
    return NS_ERROR_FAILURE;
  }
  return TRRService::Get()->DispatchTRRRequest(trr);
}

nsresult TRR::On200Response(nsIChannel* aChannel) {
  // decode body and create an AddrInfo struct for the response
  nsClassHashtable<nsCStringHashKey, DOHresp> additionalRecords;
  if (RefPtr<TypeHostRecord> typeRec = do_QueryObject(mRec)) {
    MutexAutoLock lock(typeRec->mResultsLock);
    if (typeRec->mOriginHost) {
      GetOrCreateDNSPacket()->SetOriginHost(typeRec->mOriginHost);
    }
  }
  nsresult rv = GetOrCreateDNSPacket()->Decode(
      mHost, mType, mCname, StaticPrefs::network_trr_allow_rfc1918(), mDNS,
      mResult, additionalRecords, mTTL);
  if (NS_FAILED(rv)) {
    LOG(("TRR::On200Response DohDecode %x\n", (int)rv));
    HandleDecodeError(rv);
    return rv;
  }
  if (StaticPrefs::network_trr_add_additional_records()) {
    SaveAdditionalRecords(additionalRecords);
  }

  if (mResult.is<TypeRecordHTTPSSVC>()) {
    auto& results = mResult.as<TypeRecordHTTPSSVC>();
    for (const auto& rec : results) {
      StoreIPHintAsDNSRecord(rec);
    }
  }

  if (!mDNS.mAddresses.IsEmpty() || mType == TRRTYPE_TXT || mCname.IsEmpty()) {
    // pass back the response data
    ReturnData(aChannel);
    return NS_OK;
  }

  LOG(("TRR::On200Response trying CNAME %s", mCname.get()));
  return FollowCname(aChannel);
}

void TRR::RecordProcessingTime(nsIChannel* aChannel) {
  // This method records the time it took from the last received byte of the
  // DoH response until we've notified the consumer with a host record.
  nsCOMPtr<nsITimedChannel> timedChan = do_QueryInterface(aChannel);
  if (!timedChan) {
    return;
  }
  TimeStamp end;
  if (NS_FAILED(timedChan->GetResponseEnd(&end))) {
    return;
  }

  if (end.IsNull()) {
    return;
  }

  glean::dns::trr_processing_time.AccumulateRawDuration(TimeStamp::Now() - end);

  LOG(("Processing DoH response took %f ms",
       (TimeStamp::Now() - end).ToMilliseconds()));
}

void TRR::ReportStatus(nsresult aStatusCode) {
  // If the TRR was cancelled by nsHostResolver, then we don't need to report
  // it as failed; otherwise it can cause the confirmation to fail.
  if (UseDefaultServer() && aStatusCode != NS_ERROR_ABORT) {
    // Bad content is still considered "okay" if the HTTP response is okay
    TRRService::Get()->RecordTRRStatus(this);
  }
}

static void RecordHttpVersion(nsIHttpChannel* aHttpChannel) {
  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
      do_QueryInterface(aHttpChannel);
  if (!internalChannel) {
    LOG(("RecordHttpVersion: Failed to QI nsIHttpChannelInternal"));
    return;
  }

  uint32_t major, minor;
  if (NS_FAILED(internalChannel->GetResponseVersion(&major, &minor))) {
    LOG(("RecordHttpVersion: Failed to get protocol version"));
    return;
  }

  if (major == 2) {
    glean::dns::trr_http_version.Get(TRRService::ProviderKey(), "h_2"_ns).Add();
  } else if (major == 3) {
    glean::dns::trr_http_version.Get(TRRService::ProviderKey(), "h_3"_ns).Add();
  } else {
    glean::dns::trr_http_version.Get(TRRService::ProviderKey(), "h_1"_ns).Add();
  }

  LOG(("RecordHttpVersion: Provider responded using HTTP version: %d", major));
}

NS_IMETHODIMP
TRR::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  // The dtor will be run after the function returns
  LOG(("TRR:OnStopRequest %p %s %d failed=%d code=%X\n", this, mHost.get(),
       mType, mFailed, (unsigned int)aStatusCode));
  nsCOMPtr<nsIChannel> channel;
  channel.swap(mChannel);

  mChannelStatus = aStatusCode;
  if (NS_SUCCEEDED(aStatusCode)) {
    nsCString label = "regular"_ns;
    if (mPB) {
      label = "private"_ns;
    }
    mozilla::glean::networking::trr_request_count.Get(label).Add(1);
  }

  {
    // Cancel the timer since we don't need it anymore.
    nsCOMPtr<nsITimer> timer;
    mTimeout.swap(timer);
    if (timer) {
      timer->Cancel();
    }
  }

  auto scopeExit = MakeScopeExit([&] { ReportStatus(aStatusCode); });

  nsresult rv = NS_OK;
  // if status was "fine", parse the response and pass on the answer
  if (!mFailed && NS_SUCCEEDED(aStatusCode)) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
    if (!httpChannel) {
      return NS_ERROR_UNEXPECTED;
    }
    nsAutoCString contentType;
    httpChannel->GetContentType(contentType);
    if (contentType.Length() &&
        !contentType.LowerCaseEqualsASCII(ContentType())) {
      LOG(("TRR:OnStopRequest %p %s %d wrong content type %s\n", this,
           mHost.get(), mType, contentType.get()));
      FailData(NS_ERROR_UNEXPECTED);
      return NS_OK;
    }

    uint32_t httpStatus;
    rv = httpChannel->GetResponseStatus(&httpStatus);
    if (NS_SUCCEEDED(rv) && httpStatus == 200) {
      rv = On200Response(channel);
      if (NS_SUCCEEDED(rv) && UseDefaultServer()) {
        RecordReason(TRRSkippedReason::TRR_OK);
        RecordProcessingTime(channel);
        RecordHttpVersion(httpChannel);
        return rv;
      }
    } else {
      RecordReason(TRRSkippedReason::TRR_SERVER_RESPONSE_ERR);
      LOG(("TRR:OnStopRequest:%d %p rv %x httpStatus %d\n", __LINE__, this,
           (int)rv, httpStatus));
    }
  }

  LOG(("TRR:OnStopRequest %p status %x mFailed %d\n", this, (int)aStatusCode,
       mFailed));
  FailData(NS_SUCCEEDED(rv) ? NS_ERROR_UNKNOWN_HOST : rv);
  return NS_OK;
}

NS_IMETHODIMP
TRR::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInputStream,
                     uint64_t aOffset, const uint32_t aCount) {
  LOG(("TRR:OnDataAvailable %p %s %d failed=%d aCount=%u\n", this, mHost.get(),
       mType, mFailed, (unsigned int)aCount));
  // receive DNS response into the local buffer
  if (mFailed) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = GetOrCreateDNSPacket()->OnDataAvailable(aRequest, aInputStream,
                                                        aOffset, aCount);
  if (NS_FAILED(rv)) {
    LOG(("TRR::OnDataAvailable:%d fail\n", __LINE__));
    mFailed = true;
    return rv;
  }
  return NS_OK;
}

void TRR::Cancel(nsresult aStatus) {
  // mChannel is only accessed on mTarget, so it must not be touched before we
  // get there.
  if (mTarget && !mTarget->IsOnCurrentThread()) {
    mTarget->Dispatch(NS_NewRunnableFunction(
        "TRR::Cancel",
        [self = RefPtr(this), aStatus]() { self->Cancel(aStatus); }));
    return;
  }

  if (mCancelled) {
    return;
  }
  mCancelled = true;

  if (mChannel) {
    RecordReason(TRRSkippedReason::TRR_REQ_CANCELLED);
    LOG(("TRR: %p canceling Channel %p %s %d status=%" PRIx32 "\n", this,
         mChannel.get(), mHost.get(), mType, static_cast<uint32_t>(aStatus)));
    mChannel->Cancel(aStatus);
  }
}

bool TRR::UseDefaultServer() { return !mRec || mRec->mTrrServer.IsEmpty(); }

}  // namespace net
}  // namespace mozilla
