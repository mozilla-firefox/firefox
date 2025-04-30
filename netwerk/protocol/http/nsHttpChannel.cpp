/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set expandtab ts=4 sw=2 sts=2 cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include <inttypes.h>

#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/NavigatorLogin.h"
#include "mozilla/glean/AntitrackingMetrics.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/StoragePrincipalHelper.h"

#include "nsCOMPtr.h"
#include "nsContentSecurityUtils.h"
#include "nsHttp.h"
#include "nsHttpChannel.h"
#include "nsHttpChannelAuthProvider.h"
#include "nsHttpHandler.h"
#include "nsIStreamConverter.h"
#include "nsString.h"
#include "nsICacheStorageService.h"
#include "nsICacheStorage.h"
#include "nsICacheEntry.h"
#include "nsICryptoHash.h"
#include "nsIEffectiveTLDService.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsINetworkInterceptController.h"
#include "nsIStringBundle.h"
#include "nsIStreamListenerTee.h"
#include "nsISeekableStream.h"
#include "nsIProtocolProxyService2.h"
#include "nsIURLQueryStringStripper.h"
#include "nsIWebTransport.h"
#include "nsCRT.h"
#include "nsMimeTypes.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsIStreamTransportService.h"
#include "prnetdb.h"
#include "nsEscape.h"
#include "nsComponentManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsIOService.h"
#include "nsDNSPrefetch.h"
#include "nsChannelClassifier.h"
#include "nsIRedirectResultListener.h"
#include "mozilla/TimeStamp.h"
#include "nsError.h"
#include "nsPrintfCString.h"
#include "nsQueryObject.h"
#include "nsThreadUtils.h"
#include "nsIConsoleService.h"
#include "nsINetworkErrorLogging.h"
#include "mozilla/AntiTrackingRedirectHeuristic.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/PerfStats.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPrefs_security.h"
#include "sslt.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsContentSecurityManager.h"
#include "nsIClassOfService.h"
#include "CookieService.h"
#include "nsIPrincipal.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsITransportSecurityInfo.h"
#include "nsIWebProgressListener.h"
#include "LoadContextInfo.h"
#include "netCore.h"
#include "nsHttpTransaction.h"
#include "nsICancelable.h"
#include "nsIHttpChannelInternal.h"
#include "nsIPrompt.h"
#include "nsInputStreamPump.h"
#include "nsURLHelper.h"
#include "nsISocketTransport.h"
#include "nsIStreamConverterService.h"
#include "nsISiteSecurityService.h"
#include "nsIURIMutator.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/glean/DomSecurityMetrics.h"
#include "mozilla/Telemetry.h"
#include "AlternateServices.h"
#include "NetworkMarker.h"
#include "nsIDNSRecord.h"
#include "mozilla/dom/Document.h"
#include "nsICompressConvStats.h"
#include "nsCORSListenerProxy.h"
#include "nsISocketProvider.h"
#include "mozilla/extensions/StreamFilterParent.h"
#include "mozilla/net/Predictor.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/NullPrincipal.h"
#include "CacheControlParser.h"
#include "nsMixedContentBlocker.h"
#include "CacheStorageService.h"
#include "HttpChannelParent.h"
#include "HttpTransactionParent.h"
#include "ThirdPartyUtil.h"
#include "InterceptedHttpChannel.h"
#include "../../cache2/CacheFileUtils.h"
#include "nsINetworkLinkService.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/nsHTTPSOnlyStreamListener.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/net/AsyncUrlChannelClassifier.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/net/OpaqueResponseUtils.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "HttpTrafficAnalyzer.h"
#include "mozilla/net/SocketProcessParent.h"
#include "mozilla/dom/SecFetch.h"
#include "mozilla/net/TRRService.h"
#include "nsUnknownDecoder.h"
#ifdef XP_WIN
#  include "HttpWinUtils.h"
#endif
#ifdef XP_MACOSX
#  include "MicrosoftEntraSSOUtils.h"
#endif
#ifdef FUZZING
#  include "mozilla/StaticPrefs_fuzzing.h"
#endif

namespace mozilla {

using namespace dom;

namespace net {

namespace {

// True if the local cache should be bypassed when processing a request.
#define BYPASS_LOCAL_CACHE(loadFlags, isPreferCacheLoadOverBypass) \
  ((loadFlags) & (nsIRequest::LOAD_BYPASS_CACHE |                  \
                  nsICachingChannel::LOAD_BYPASS_LOCAL_CACHE) &&   \
   !(((loadFlags) & nsIRequest::LOAD_FROM_CACHE) &&                \
     (isPreferCacheLoadOverBypass)))

#define RECOVER_FROM_CACHE_FILE_ERROR(result) \
  ((result) == NS_ERROR_FILE_NOT_FOUND ||     \
   (result) == NS_ERROR_FILE_CORRUPTED || (result) == NS_ERROR_OUT_OF_MEMORY)

#define WRONG_RACING_RESPONSE_SOURCE(req)               \
  (mRaceCacheWithNetwork &&                             \
   (((mFirstResponseSource == RESPONSE_FROM_CACHE) &&   \
     ((req) != mCachePump)) ||                          \
    ((mFirstResponseSource == RESPONSE_FROM_NETWORK) && \
     ((req) != mTransactionPump))))

static NS_DEFINE_CID(kStreamListenerTeeCID, NS_STREAMLISTENERTEE_CID);

enum ChannelDisposition {
  kHttpCanceled = 0,
  kHttpDisk = 1,
  kHttpNetOK = 2,
  kHttpNetEarlyFail = 3,
  kHttpNetLateFail = 4,
  kHttpsCanceled = 8,
  kHttpsDisk = 9,
  kHttpsNetOK = 10,
  kHttpsNetEarlyFail = 11,
  kHttpsNetLateFail = 12
};

void AccumulateCacheHitTelemetry(CacheDisposition hitOrMiss,
                                 nsIChannel* aChannel) {
  nsCString key("UNKNOWN");

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  nsAutoCString contentType;
  if (NS_SUCCEEDED(aChannel->GetContentType(contentType))) {
    if (nsContentUtils::IsJavascriptMIMEType(
            NS_ConvertUTF8toUTF16(contentType))) {
      key.AssignLiteral("JAVASCRIPT");
    } else if (StringBeginsWith(contentType, "text/css"_ns) ||
               (loadInfo && loadInfo->GetExternalContentPolicyType() ==
                                ExtContentPolicy::TYPE_STYLESHEET)) {
      key.AssignLiteral("STYLESHEET");
    } else if (StringBeginsWith(contentType, "application/wasm"_ns)) {
      key.AssignLiteral("WASM");
    } else if (StringBeginsWith(contentType, "image/"_ns)) {
      key.AssignLiteral("IMAGE");
    } else if (StringBeginsWith(contentType, "video/"_ns)) {
      key.AssignLiteral("MEDIA");
    } else if (StringBeginsWith(contentType, "audio/"_ns)) {
      key.AssignLiteral("MEDIA");
    } else if (!StringBeginsWith(contentType,
                                 nsLiteralCString(UNKNOWN_CONTENT_TYPE))) {
      key.AssignLiteral("OTHER");
    }
  }

  Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3 label =
      Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::Unresolved;
  switch (hitOrMiss) {
    case kCacheUnresolved:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::Unresolved;
      break;
    case kCacheHit:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::Hit;
      break;
    case kCacheHitViaReval:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::HitViaReval;
      break;
    case kCacheMissedViaReval:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::MissedViaReval;
      break;
    case kCacheMissed:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::Missed;
      break;
    case kCacheUnknown:
      label = Telemetry::LABELS_HTTP_CACHE_DISPOSITION_3::Unknown;
      break;
  }

  Telemetry::AccumulateCategoricalKeyed(key, label);
  Telemetry::AccumulateCategoricalKeyed("ALL"_ns, label);
}

// Computes and returns a SHA1 hash of the input buffer. The input buffer
// must be a null-terminated string.
nsresult Hash(const char* buf, nsACString& hash) {
  nsresult rv;

  nsCOMPtr<nsICryptoHash> hasher =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = hasher->Init(nsICryptoHash::SHA1);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = hasher->Update(reinterpret_cast<unsigned const char*>(buf), strlen(buf));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = hasher->Finish(true, hash);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

class CookieVisitor final {
 public:
  explicit CookieVisitor(nsHttpResponseHead* aResponseHead) {
    nsAutoCString cookieHeader;
    if (NS_SUCCEEDED(
            aResponseHead->GetHeader(nsHttp::Set_Cookie, cookieHeader))) {
      for (const auto& cookie : cookieHeader.Split('\n')) {
        mCookieHeaders.AppendElement(cookie);
      }
    }
  }

  ~CookieVisitor() = default;

  const nsTArray<nsCString>& CookieHeaders() const { return mCookieHeaders; }

 private:
  nsTArray<nsCString> mCookieHeaders;
};
}  // unnamed namespace

// We only treat 3xx responses as redirects if they have a Location header and
// the status code is in a whitelist.
bool nsHttpChannel::WillRedirect(const nsHttpResponseHead& response) {
  return IsRedirectStatus(response.Status()) &&
         response.HasHeader(nsHttp::Location);
}

nsresult StoreAuthorizationMetaData(nsICacheEntry* entry,
                                    nsHttpRequestHead* requestHead);

class MOZ_STACK_CLASS AutoRedirectVetoNotifier {
 public:
  explicit AutoRedirectVetoNotifier(nsHttpChannel* channel, nsresult& aRv)
      : mChannel(channel), mRv(aRv) {
    if (mChannel->LoadHasAutoRedirectVetoNotifier()) {
      MOZ_CRASH("Nested AutoRedirectVetoNotifier on the stack");
      mChannel = nullptr;
      return;
    }

    mChannel->StoreHasAutoRedirectVetoNotifier(true);
  }
  ~AutoRedirectVetoNotifier() { ReportRedirectResult(mRv); }
  void RedirectSucceeded() { ReportRedirectResult(NS_OK); }

 private:
  nsHttpChannel* mChannel;
  bool mCalledReport = false;
  nsresult& mRv;
  void ReportRedirectResult(nsresult aRv);
};

void AutoRedirectVetoNotifier::ReportRedirectResult(nsresult aRv) {
  if (!mChannel) return;

  if (mCalledReport) {
    return;
  }
  mCalledReport = true;

  mChannel->mRedirectChannel = nullptr;

  if (NS_SUCCEEDED(aRv)) {
    mChannel->RemoveAsNonTailRequest();
  }

  nsCOMPtr<nsIRedirectResultListener> vetoHook;
  NS_QueryNotificationCallbacks(mChannel, NS_GET_IID(nsIRedirectResultListener),
                                getter_AddRefs(vetoHook));

  nsHttpChannel* channel = mChannel;
  mChannel = nullptr;

  if (vetoHook) vetoHook->OnRedirectResult(aRv);

  // Drop after the notification
  channel->StoreHasAutoRedirectVetoNotifier(false);
}

//-----------------------------------------------------------------------------
// nsHttpChannel <public>
//-----------------------------------------------------------------------------

nsHttpChannel::nsHttpChannel() : HttpAsyncAborter<nsHttpChannel>(this) {
  LOG(("Creating nsHttpChannel [this=%p, nsIChannel=%p]\n", this,
       static_cast<nsIChannel*>(this)));
  mChannelCreationTime = PR_Now();
  mChannelCreationTimestamp = TimeStamp::Now();
}

nsHttpChannel::~nsHttpChannel() {
  PROFILER_MARKER("~nsHttpChannel", NETWORK, {}, TerminatingFlowMarker,
                  Flow::FromPointer(this));
  LOG(("Destroying nsHttpChannel [this=%p, nsIChannel=%p]\n", this,
       static_cast<nsIChannel*>(this)));

  if (LOG_ENABLED()) {
    nsCString webExtension;
    this->GetPropertyAsACString(u"cancelledByExtension"_ns, webExtension);
    if (!webExtension.IsEmpty()) {
      LOG(("channel [%p] cancelled by extension [id=%s]", this,
           webExtension.get()));
    }
  }

  if (mAuthProvider) {
    DebugOnly<nsresult> rv = mAuthProvider->Disconnect(NS_ERROR_ABORT);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  ReleaseMainThreadOnlyReferences();
  if (gHttpHandler) {
    gHttpHandler->RemoveHttpChannel(mChannelId);
  }
}

void nsHttpChannel::ReleaseMainThreadOnlyReferences() {
  if (NS_IsMainThread()) {
    // Already on main thread, let dtor to
    // take care of releasing references
    return;
  }

  nsTArray<nsCOMPtr<nsISupports>> arrayToRelease;
  arrayToRelease.AppendElement(mAuthProvider.forget());
  arrayToRelease.AppendElement(mRedirectChannel.forget());
  arrayToRelease.AppendElement(mPreflightChannel.forget());
  arrayToRelease.AppendElement(mDNSPrefetch.forget());

  MOZ_DIAGNOSTIC_ASSERT(
      !mEarlyHintObserver,
      "Early hint observer should have been released in ReleaseListeners()");
  arrayToRelease.AppendElement(mEarlyHintObserver.forget());
  MOZ_DIAGNOSTIC_ASSERT(
      !mChannelClassifier,
      "Channel classifier should have been released in ReleaseListeners()");
  arrayToRelease.AppendElement(
      mChannelClassifier.forget().downcast<nsIURIClassifierCallback>());
  MOZ_DIAGNOSTIC_ASSERT(
      !mWarningReporter,
      "Warning reporter should have been released in ReleaseListeners()");
  arrayToRelease.AppendElement(mWarningReporter.forget());

  NS_DispatchToMainThread(new ProxyReleaseRunnable(std::move(arrayToRelease)));
}

nsresult nsHttpChannel::Init(nsIURI* uri, uint32_t caps, nsProxyInfo* proxyInfo,
                             uint32_t proxyResolveFlags, nsIURI* proxyURI,
                             uint64_t channelId,
                             ExtContentPolicyType aContentPolicyType,
                             nsILoadInfo* aLoadInfo) {
  nsresult rv =
      HttpBaseChannel::Init(uri, caps, proxyInfo, proxyResolveFlags, proxyURI,
                            channelId, aContentPolicyType, aLoadInfo);
  if (NS_FAILED(rv)) return rv;

  LOG1(("nsHttpChannel::Init [this=%p]\n", this));

  return rv;
}

nsresult nsHttpChannel::AddSecurityMessage(const nsAString& aMessageTag,
                                           const nsAString& aMessageCategory) {
  if (mWarningReporter) {
    return mWarningReporter->ReportSecurityMessage(aMessageTag,
                                                   aMessageCategory);
  }
  return HttpBaseChannel::AddSecurityMessage(aMessageTag, aMessageCategory);
}

NS_IMETHODIMP
nsHttpChannel::LogBlockedCORSRequest(const nsAString& aMessage,
                                     const nsACString& aCategory,
                                     bool aIsWarning) {
  if (mWarningReporter) {
    return mWarningReporter->LogBlockedCORSRequest(aMessage, aCategory,
                                                   aIsWarning);
  }
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsHttpChannel::LogMimeTypeMismatch(const nsACString& aMessageName,
                                   bool aWarning, const nsAString& aURL,
                                   const nsAString& aContentType) {
  if (mWarningReporter) {
    return mWarningReporter->LogMimeTypeMismatch(aMessageName, aWarning, aURL,
                                                 aContentType);
  }
  return NS_ERROR_UNEXPECTED;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <private>
//-----------------------------------------------------------------------------

nsresult nsHttpChannel::PrepareToConnect() {
  LOG(("nsHttpChannel::PrepareToConnect [this=%p]\n", this));

  // notify "http-on-modify-request-before-cookies" observers
  gHttpHandler->OnModifyRequestBeforeCookies(this);

  AddCookiesToRequest();

#if defined(XP_WIN) || defined(XP_MACOSX)

  auto prefEnabledForCurrentContainer = [&]() {
    uint32_t containerId = mLoadInfo->GetOriginAttributes().mUserContextId;
    // Make sure that the default container ID is 0
    static_assert(nsIScriptSecurityManager::DEFAULT_USER_CONTEXT_ID == 0);

    nsAutoCString prefName;
#  ifdef XP_WIN
    prefName = nsPrintfCString("network.http.windows-sso.container-enabled.%u",
                               containerId);
#  endif

#  ifdef XP_MACOSX
    prefName = nsPrintfCString(
        "network.http.microsoft-entra-sso.container-enabled.%u", containerId);
#  endif

    bool enabled = false;
    Preferences::GetBool(prefName.get(), &enabled);

    LOG(("Pref for %s is %d\n", prefName.get(), enabled));

    return enabled;
  };

#endif  // defined(XP_WIN) || defined(XP_MACOSX)

#ifdef XP_WIN

  // If Windows 10 SSO is enabled, we potentially add auth
  // information to secure top level loads (DOCUMENTs) and iframes
  // (SUBDOCUMENTs) that aren't anonymous or private browsing.
  if (StaticPrefs::network_http_windows_sso_enabled() &&
      mURI->SchemeIs("https") && !(mLoadFlags & LOAD_ANONYMOUS) &&
      !mPrivateBrowsing) {
    ExtContentPolicyType type = mLoadInfo->GetExternalContentPolicyType();
    if ((type == ExtContentPolicy::TYPE_DOCUMENT ||
         type == ExtContentPolicy::TYPE_SUBDOCUMENT) &&
        prefEnabledForCurrentContainer()) {
      AddWindowsSSO(this);
    }
  }
#endif

#ifdef XP_MACOSX

  auto isUriMSAuthority = [&]() {
    nsAutoCString endPoint;
    nsresult rv = mURI->GetHost(endPoint);
    if (!NS_SUCCEEDED(rv)) {
      return false;
    }
    LOG(("endPoint is %s\n", endPoint.get()));

    return gHttpHandler->IsHostMSAuthority(endPoint);
  };

  // If macOS SSO is enabled, we potentially add auth
  // information to secure top level loads (DOCUMENTs) and iframes
  // (SUBDOCUMENTs) that aren't anonymous or private browsing.
  if (StaticPrefs::network_http_microsoft_entra_sso_enabled() &&
      mURI->SchemeIs("https") && !(mLoadFlags & LOAD_ANONYMOUS) &&
      !mPrivateBrowsing) {
    ExtContentPolicyType type = mLoadInfo->GetExternalContentPolicyType();
    if ((type == ExtContentPolicy::TYPE_DOCUMENT ||
         type == ExtContentPolicy::TYPE_SUBDOCUMENT) &&
        prefEnabledForCurrentContainer() && isUriMSAuthority()) {
      nsMainThreadPtrHandle<nsHttpChannel> self(
          new nsMainThreadPtrHolder<nsHttpChannel>(
              "nsHttpChannel::PrepareToConnect::self", this));
      auto resultCallback = [self(self)]() {
        MOZ_ASSERT(NS_IsMainThread());
        nsresult rv = self->ContinuePrepareToConnect();
        if (NS_FAILED(rv)) {
          self->CloseCacheEntry(false);
          Unused << self->AsyncAbort(rv);
        }
      };

      nsresult rv = AddMicrosoftEntraSSO(this, std::move(resultCallback));

      // Returns NS_OK if performRequests is called in MicrosoftEntraSSOUtils
      // This temporarily stops the channel setup for the delegate.
      if (NS_SUCCEEDED(rv)) {
        return rv;
      }
    }
  }

#endif

  return ContinuePrepareToConnect();
}

nsresult nsHttpChannel::ContinuePrepareToConnect() {
  // notify "http-on-modify-request" observers
  CallOnModifyRequestObservers();

  return CallOrWaitForResume(
      [](auto* self) { return self->OnBeforeConnect(); });
}

void nsHttpChannel::HandleContinueCancellingByURLClassifier(
    nsresult aErrorCode) {
  MOZ_ASSERT(
      UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(aErrorCode));
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(
        ("Waiting until resume HandleContinueCancellingByURLClassifier "
         "[this=%p]\n",
         this));
    mCallOnResume = [aErrorCode](nsHttpChannel* self) {
      self->HandleContinueCancellingByURLClassifier(aErrorCode);
      return NS_OK;
    };
    return;
  }

  LOG(("nsHttpChannel::HandleContinueCancellingByURLClassifier [this=%p]\n",
       this));
  ContinueCancellingByURLClassifier(aErrorCode);
}

void nsHttpChannel::SetPriorityHeader() {
  nsAutoCString userSetPriority;
  Unused << GetRequestHeader("Priority"_ns, userSetPriority);
  if (!userSetPriority.IsEmpty()) {
    // If the Priority header is set by the user, do not override it.
    return;
  }

  uint8_t urgency =
      nsHttpHandler::UrgencyFromCoSFlags(mClassOfService.Flags(), mPriority);
  bool incremental = mClassOfService.Incremental();

  nsPrintfCString value(
      "%s", urgency != 3 ? nsPrintfCString("u=%d", urgency).get() : "");

  if (incremental) {
    if (!value.IsEmpty()) {
      value.Append(", ");
    }
    value.Append("i");
  }

  if (!value.IsEmpty()) {
    SetRequestHeader("Priority"_ns, value, false);
  }
}

nsresult nsHttpChannel::OnBeforeConnect() {
  nsresult rv = NS_OK;

  // Check if request was cancelled during suspend AFTER on-modify-request
  if (mCanceled) {
    return mStatus;
  }

  // Check to see if we should redirect this channel elsewhere by
  // nsIHttpChannel.redirectTo API request
  if (mAPIRedirectTo) {
    return AsyncCall(&nsHttpChannel::HandleAsyncAPIRedirect);
  }

  // Note that we are only setting the "Upgrade-Insecure-Requests" request
  // header for *all* navigational requests instead of all requests as
  // defined in the spec, see:
  // https://www.w3.org/TR/upgrade-insecure-requests/#preference
  ExtContentPolicyType type = mLoadInfo->GetExternalContentPolicyType();

  if (type == ExtContentPolicy::TYPE_DOCUMENT ||
      type == ExtContentPolicy::TYPE_SUBDOCUMENT) {
    rv = SetRequestHeader("Upgrade-Insecure-Requests"_ns, "1"_ns, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (LoadAuthRedirectedChannel()) {
    // This channel is a result of a redirect due to auth retry
    // We have already checked for HSTS upgarde in the redirecting channel.
    // We can safely skip those checks
    return ContinueOnBeforeConnect(false, rv);
  }

  SecFetch::AddSecFetchHeader(this);

  // Check to see if we should redirect this channel to the unstripped URI. To
  // revert the query stripping if the loading channel is in the content
  // blocking allow list.
  if (ContentBlockingAllowList::Check(this)) {
    nsCOMPtr<nsIURI> unstrippedURI;
    mLoadInfo->GetUnstrippedURI(getter_AddRefs(unstrippedURI));

    if (unstrippedURI) {
      return AsyncCall(&nsHttpChannel::HandleAsyncRedirectToUnstrippedURI);
    }
  }

  nsCOMPtr<nsIPrincipal> resultPrincipal;
  if (!mURI->SchemeIs("https")) {
    nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
        this, getter_AddRefs(resultPrincipal));
  }

  // Check if we already know about the HSTS status of the host
  nsISiteSecurityService* sss = gHttpHandler->GetSSService();
  NS_ENSURE_TRUE(sss, NS_ERROR_OUT_OF_MEMORY);
  bool isSecureURI;
  OriginAttributes originAttributes;
  if (!StoragePrincipalHelper::GetOriginAttributesForHSTS(this,
                                                          originAttributes)) {
    return NS_ERROR_FAILURE;
  }
  rv = sss->IsSecureURI(mURI, originAttributes, &isSecureURI);
  NS_ENSURE_SUCCESS(rv, rv);
  // Save that on the loadInfo so it can later be consumed by
  // SecurityInfo.sys.mjs
  mLoadInfo->SetHstsStatus(isSecureURI);

  RefPtr<mozilla::dom::BrowsingContext> bc;
  mLoadInfo->GetBrowsingContext(getter_AddRefs(bc));
  // If bypassing the cache and we're forced offline
  // we can just return the error here.
  if (bc && bc->Top()->GetForceOffline() &&
      BYPASS_LOCAL_CACHE(mLoadFlags, LoadPreferCacheLoadOverBypass())) {
    return NS_ERROR_OFFLINE;
  }

  // At this point it is no longer possible to call
  // HttpBaseChannel::UpgradeToSecure.
  StoreUpgradableToSecure(false);
  bool shouldUpgrade = LoadUpgradeToSecure();
  if (mURI->SchemeIs("http")) {
    OriginAttributes originAttributes;
    if (!StoragePrincipalHelper::GetOriginAttributesForHSTS(this,
                                                            originAttributes)) {
      return NS_ERROR_FAILURE;
    }

    if (!shouldUpgrade) {
      // Make sure http channel is released on main thread.
      // See bug 1539148 for details.
      nsMainThreadPtrHandle<nsHttpChannel> self(
          new nsMainThreadPtrHolder<nsHttpChannel>(
              "nsHttpChannel::OnBeforeConnect::self", this));
      auto resultCallback = [self(self)](bool aResult, nsresult aStatus) {
        MOZ_ASSERT(NS_IsMainThread());

        nsresult rv = self->MaybeUseHTTPSRRForUpgrade(aResult, aStatus);
        if (NS_FAILED(rv)) {
          self->CloseCacheEntry(false);
          Unused << self->AsyncAbort(rv);
        }
      };

      bool willCallback = false;
      rv = NS_ShouldSecureUpgrade(
          mURI, mLoadInfo, resultPrincipal, LoadAllowSTS(), originAttributes,
          shouldUpgrade, std::move(resultCallback), willCallback);
      // If the request gets upgraded because of the HTTPS-Only mode, but no
      // event listener has been registered so far, we want to do that here.
      uint32_t httpOnlyStatus = mLoadInfo->GetHttpsOnlyStatus();
      if (httpOnlyStatus &
          nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_NOT_REGISTERED) {
        RefPtr<nsHTTPSOnlyStreamListener> httpsOnlyListener =
            new nsHTTPSOnlyStreamListener(mListener, mLoadInfo);
        mListener = httpsOnlyListener;

        httpOnlyStatus ^=
            nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_NOT_REGISTERED;
        httpOnlyStatus |= nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_REGISTERED;
        mLoadInfo->SetHttpsOnlyStatus(httpOnlyStatus);
      }
      LOG(
          ("nsHttpChannel::OnBeforeConnect "
           "[this=%p willCallback=%d rv=%" PRIx32 "]\n",
           this, willCallback, static_cast<uint32_t>(rv)));

      if (NS_FAILED(rv) || MOZ_UNLIKELY(willCallback)) {
        return rv;
      }
    }
  }

  return MaybeUseHTTPSRRForUpgrade(shouldUpgrade, NS_OK);
}

// Returns true if the network connectivity checker indicated
// that HTTPS records can be resolved on this network - false otherwise.
// When TRR is enabled, we always return true, as resolving HTTPS
// records don't depend on the network.
static bool canUseHTTPSRRonNetwork(bool& aTRREnabled) {
  // Respect the pref.
  if (StaticPrefs::network_dns_force_use_https_rr()) {
    aTRREnabled = true;
    return true;
  }

  aTRREnabled = false;

  if (nsCOMPtr<nsIDNSService> dns = mozilla::components::DNS::Service()) {
    nsIDNSService::ResolverMode mode;
    // If the browser is currently using TRR/DoH, then it can
    // definitely resolve HTTPS records.
    if (NS_SUCCEEDED(dns->GetCurrentTrrMode(&mode))) {
      if (mode == nsIDNSService::MODE_TRRFIRST) {
        RefPtr<TRRService> trr = TRRService::Get();
        if (trr && trr->IsConfirmed()) {
          aTRREnabled = true;
        }
      } else if (mode == nsIDNSService::MODE_TRRONLY) {
        aTRREnabled = true;
      }
      if (aTRREnabled) {
        return true;
      }
    }
  }

  if (RefPtr<NetworkConnectivityService> ncs =
          NetworkConnectivityService::GetSingleton()) {
    nsINetworkConnectivityService::ConnectivityState state;
    if (NS_SUCCEEDED(ncs->GetDNS_HTTPS(&state)) &&
        state == nsINetworkConnectivityService::NOT_AVAILABLE) {
      return false;
    }
  }
  return true;
}

nsresult nsHttpChannel::MaybeUseHTTPSRRForUpgrade(bool aShouldUpgrade,
                                                  nsresult aStatus) {
  if (NS_FAILED(aStatus)) {
    return aStatus;
  }

  RefPtr<mozilla::dom::BrowsingContext> bc;
  mLoadInfo->GetBrowsingContext(getter_AddRefs(bc));
  bool forceOffline = bc && bc->Top()->GetForceOffline();

  if (mURI->SchemeIs("https") || aShouldUpgrade || !LoadUseHTTPSSVC() ||
      forceOffline) {
    return ContinueOnBeforeConnect(aShouldUpgrade, aStatus);
  }

  auto shouldSkipUpgradeWithHTTPSRR = [&]() -> bool {
    if (mCaps & NS_HTTP_DISALLOW_HTTPS_RR) {
      return true;
    }

    // Skip using HTTPS RR to upgrade when this is not a top-level load and the
    // loading principal is http.
    if ((mLoadInfo->GetExternalContentPolicyType() !=
         ExtContentPolicy::TYPE_DOCUMENT) &&
        (mLoadInfo->GetLoadingPrincipal() &&
         mLoadInfo->GetLoadingPrincipal()->SchemeIs("http"))) {
      return true;
    }

    // If the network connectivity checker indicates the network is
    // blocking HTTPS requests, then we should skip them so we don't
    // needlessly wait for a timeout.
    bool trrEnabled = false;
    if (!canUseHTTPSRRonNetwork(trrEnabled)) {
      return true;
    }

    // Don't block the channel when TRR is not used.
    if (!trrEnabled) {
      return true;
    }

    auto dnsStrategy = GetProxyDNSStrategy();
    if (dnsStrategy != ProxyDNSStrategy::ORIGIN) {
      return true;
    }

    nsAutoCString uriHost;
    mURI->GetAsciiHost(uriHost);

    return gHttpHandler->IsHostExcludedForHTTPSRR(uriHost);
  };

  if (shouldSkipUpgradeWithHTTPSRR()) {
    StoreUseHTTPSSVC(false);
    // If the website does not want to use HTTPS RR, we should set
    // NS_HTTP_DISALLOW_HTTPS_RR. This is for avoiding HTTPS RR being used by
    // the transaction.
    DisallowHTTPSRR(mCaps);
    return ContinueOnBeforeConnect(aShouldUpgrade, aStatus);
  }

  if (mHTTPSSVCRecord.isSome()) {
    LOG((
        "nsHttpChannel::MaybeUseHTTPSRRForUpgrade [%p] mHTTPSSVCRecord is some",
        this));
    StoreWaitHTTPSSVCRecord(false);
    bool hasHTTPSRR = (mHTTPSSVCRecord.ref() != nullptr);
    return ContinueOnBeforeConnect(hasHTTPSRR, aStatus, hasHTTPSRR);
  }

  LOG(("nsHttpChannel::MaybeUseHTTPSRRForUpgrade [%p] wait for HTTPS RR",
       this));

  OriginAttributes originAttributes;
  StoragePrincipalHelper::GetOriginAttributesForHTTPSRR(this, originAttributes);

  RefPtr<nsDNSPrefetch> resolver =
      new nsDNSPrefetch(mURI, originAttributes, nsIRequest::GetTRRMode());
  nsWeakPtr weakPtrThis(
      do_GetWeakReference(static_cast<nsIHttpChannel*>(this)));
  nsresult rv = resolver->FetchHTTPSSVC(
      mCaps & NS_HTTP_REFRESH_DNS, !LoadUseHTTPSSVC(),
      [weakPtrThis](nsIDNSHTTPSSVCRecord* aRecord) {
        nsCOMPtr<nsIHttpChannel> channel = do_QueryReferent(weakPtrThis);
        RefPtr<nsHttpChannel> httpChannelImpl = do_QueryObject(channel);
        if (httpChannelImpl) {
          httpChannelImpl->OnHTTPSRRAvailable(aRecord);
        }
      });
  if (NS_FAILED(rv)) {
    LOG(("  FetchHTTPSSVC failed with 0x%08" PRIx32,
         static_cast<uint32_t>(rv)));
    return ContinueOnBeforeConnect(aShouldUpgrade, aStatus);
  }

  StoreWaitHTTPSSVCRecord(true);
  return NS_OK;
}

nsresult nsHttpChannel::ContinueOnBeforeConnect(bool aShouldUpgrade,
                                                nsresult aStatus,
                                                bool aUpgradeWithHTTPSRR) {
  LOG(
      ("nsHttpChannel::ContinueOnBeforeConnect "
       "[this=%p aShouldUpgrade=%d rv=%" PRIx32 "]\n",
       this, aShouldUpgrade, static_cast<uint32_t>(aStatus)));

  MOZ_ASSERT(!LoadWaitHTTPSSVCRecord());

  if (NS_FAILED(aStatus)) {
    return aStatus;
  }

  if (aShouldUpgrade && !mURI->SchemeIs("https")) {
    // only set HTTPS_RR to be responsbile for the upgrade in the loadinfo
    // if it actually was responsible, otherwise the correct flag is
    // already present in the loadinfo.
    if (aUpgradeWithHTTPSRR) {
      mLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::HTTPS_RR);
    }
    return AsyncCall(&nsHttpChannel::HandleAsyncRedirectChannelToHttps);
  }

  // ensure that we are using a valid hostname
  if (!net_IsValidDNSHost(nsDependentCString(mConnectionInfo->Origin()))) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (mUpgradeProtocolCallback) {
    // Websockets can run over HTTP/2, but other upgrades can't.
    if (mUpgradeProtocol.EqualsLiteral("websocket") &&
        StaticPrefs::network_http_http2_websockets()) {
      // Need to tell the conn manager that we're ok with http/2 even with
      // the allow keepalive bit not set. That bit needs to stay off,
      // though, in case we end up having to fallback to http/1.1 (where
      // we absolutely do want to disable keepalive).
      mCaps |= NS_HTTP_ALLOW_SPDY_WITHOUT_KEEPALIVE;
    } else {
      mCaps |= NS_HTTP_DISALLOW_SPDY;
    }
    // Upgrades cannot use HTTP/3.
    mCaps |= NS_HTTP_DISALLOW_HTTP3;
    // Because NS_HTTP_STICKY_CONNECTION breaks HTTPS RR fallabck mecnahism, we
    // can not use HTTPS RR for upgrade requests.
    DisallowHTTPSRR(mCaps);
  }

  if (LoadIsTRRServiceChannel()) {
    mCaps |= NS_HTTP_LARGE_KEEPALIVE;
    DisallowHTTPSRR(mCaps);
  }

  if (mTransactionSticky) {
    MOZ_ASSERT(LoadAuthRedirectedChannel());
    // this means this is a redirected channel channel due to auth retry and a
    // connection based auth scheme was used
    // we have a reference to the old-transaction with sticky connection which
    // we need to use
    mCaps |= NS_HTTP_STICKY_CONNECTION;
  }

  mCaps |= NS_HTTP_TRR_FLAGS_FROM_MODE(nsIRequest::GetTRRMode());

  // Finalize ConnectionInfo flags before SpeculativeConnect
  mConnectionInfo->SetAnonymous((mLoadFlags & LOAD_ANONYMOUS) != 0);
  mConnectionInfo->SetPrivate(mPrivateBrowsing);
  mConnectionInfo->SetNoSpdy(mCaps & NS_HTTP_DISALLOW_SPDY);
  mConnectionInfo->SetBeConservative((mCaps & NS_HTTP_BE_CONSERVATIVE) ||
                                     LoadBeConservative());
  mConnectionInfo->SetTlsFlags(mTlsFlags);
  mConnectionInfo->SetIsTrrServiceChannel(LoadIsTRRServiceChannel());
  mConnectionInfo->SetTRRMode(nsIRequest::GetTRRMode());
  mConnectionInfo->SetIPv4Disabled(mCaps & NS_HTTP_DISABLE_IPV4);
  mConnectionInfo->SetIPv6Disabled(mCaps & NS_HTTP_DISABLE_IPV6);
  mConnectionInfo->SetAnonymousAllowClientCert(
      (mLoadFlags & LOAD_ANONYMOUS_ALLOW_CLIENT_CERT) != 0);

  if (mWebTransportSessionEventListener) {
    nsTArray<RefPtr<nsIWebTransportHash>> aServerCertHashes;
    nsresult rv;
    nsCOMPtr<WebTransportConnectionSettings> wtconSettings =
        do_QueryInterface(mWebTransportSessionEventListener, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    wtconSettings->GetServerCertificateHashes(aServerCertHashes);
    gHttpHandler->ConnMgr()->StoreServerCertHashes(
        mConnectionInfo, gHttpHandler->IsHttp2Excluded(mConnectionInfo),
        !Http3Allowed(), std::move(aServerCertHashes));
  }

  if (ShouldIntercept()) {
    return RedirectToInterceptedChannel();
  }

  // notify "http-on-before-connect" observers
  gHttpHandler->OnBeforeConnect(this);

  return CallOrWaitForResume([](auto* self) { return self->Connect(); });
}

class MOZ_STACK_CLASS AddResponseHeadersToResponseHead final
    : public nsIHttpHeaderVisitor {
 public:
  explicit AddResponseHeadersToResponseHead(nsHttpResponseHead* aResponseHead)
      : mResponseHead(aResponseHead) {}

  NS_IMETHOD VisitHeader(const nsACString& aHeader,
                         const nsACString& aValue) override {
    nsAutoCString headerLine = aHeader + ": "_ns + aValue;
    DebugOnly<nsresult> rv = mResponseHead->ParseHeaderLine(headerLine);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    return NS_OK;
  }

  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;

  // Stub AddRef/Release since this is a stack class.
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override {
    return ++mRefCnt;
  }

  NS_IMETHOD_(MozExternalRefCountType) Release(void) override {
    return --mRefCnt;
  }

  virtual ~AddResponseHeadersToResponseHead() {
    MOZ_DIAGNOSTIC_ASSERT(mRefCnt == 0);
  }

 private:
  nsHttpResponseHead* mResponseHead;

  nsrefcnt mRefCnt = 0;
};

NS_IMPL_QUERY_INTERFACE(AddResponseHeadersToResponseHead, nsIHttpHeaderVisitor)

nsresult nsHttpChannel::HandleOverrideResponse() {
  // Start building a response with the data from mOverrideResponse.
  mResponseHead = MakeUnique<nsHttpResponseHead>();

  // Apply override response status code and status text.
  uint32_t statusCode;
  nsresult rv = mOverrideResponse->GetResponseStatus(&statusCode);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString statusText;
  rv = mOverrideResponse->GetResponseStatusText(statusText);
  NS_ENSURE_SUCCESS(rv, rv);

  // Hardcoding protocol HTTP/1.1
  nsPrintfCString line("HTTP/1.1 %u %s", statusCode, statusText.get());
  rv = mResponseHead->ParseStatusLine(line);
  NS_ENSURE_SUCCESS(rv, rv);

  // Apply override response headers.
  AddResponseHeadersToResponseHead visitor(mResponseHead.get());
  rv = mOverrideResponse->VisitResponseHeaders(&visitor);
  NS_ENSURE_SUCCESS(rv, rv);

  if (WillRedirect(*mResponseHead)) {
    // TODO: Bug 759040 - We should call HandleAsyncRedirect directly here,
    // to avoid event dispatching latency.
    LOG(("Skipping read of overridden response redirect entity\n"));
    return AsyncCall(&nsHttpChannel::HandleAsyncRedirect);
  }

  // Handle Set-Cookie headers as if the response was from networking.
  CookieVisitor cookieVisitor(mResponseHead.get());
  SetCookieHeaders(cookieVisitor.CookieHeaders());
  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(this, parentChannel);
  if (RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel)) {
    httpParent->SetCookieHeaders(cookieVisitor.CookieHeaders());
  }

  rv = ProcessSecurityHeaders();
  if (NS_FAILED(rv)) {
    NS_WARNING("ProcessSecurityHeaders failed, continuing load.");
  }

  if ((statusCode < 500) && (statusCode != 421)) {
    ProcessAltService();
  }

  nsAutoCString body;
  rv = mOverrideResponse->GetResponseBody(body);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> stringStream;
  rv = NS_NewCStringInputStream(getter_AddRefs(stringStream), body);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = nsInputStreamPump::Create(getter_AddRefs(mCachePump), stringStream, 0, 0,
                                 true);
  if (NS_FAILED(rv)) {
    stringStream->Close();
    return rv;
  }

  rv = mCachePump->AsyncRead(this);
  if (NS_FAILED(rv)) return rv;

  return NS_OK;
}

nsresult nsHttpChannel::Connect() {
  LOG(("nsHttpChannel::Connect [this=%p]\n", this));

  if (mAPIRedirectTo) {
    LOG(("nsHttpChannel::Connect [transparent=%d]\n",
         mAPIRedirectTo->second()));

    nsresult rv = StartRedirectChannelToURI(
        mAPIRedirectTo->first(),
        mAPIRedirectTo->second() ? nsIChannelEventSink::REDIRECT_PERMANENT |
                                       nsIChannelEventSink::REDIRECT_TRANSPARENT
                                 : nsIChannelEventSink::REDIRECT_PERMANENT);
    mAPIRedirectTo = Nothing();
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }
    return NS_ERROR_FAILURE;
  }

  // If mOverrideResponse is set, bypass the rest of the connection and reply
  // immediately with a response built using the data from mOverrideResponse.
  if (mOverrideResponse) {
    return HandleOverrideResponse();
  }

  // Don't allow resuming when cache must be used
  if (LoadResuming() && (mLoadFlags & LOAD_ONLY_FROM_CACHE)) {
    LOG(("Resuming from cache is not supported yet"));
    return NS_ERROR_DOCUMENT_NOT_CACHED;
  }

  // Step 8.18 of HTTP-network-or-cache fetch
  // https://fetch.spec.whatwg.org/#http-network-or-cache-fetch
  nsAutoCString rangeVal;
  if (NS_SUCCEEDED(GetRequestHeader("Range"_ns, rangeVal))) {
    SetRequestHeader("Accept-Encoding"_ns, "identity"_ns, true);
  }

  if (mRequestHead.IsPost() || mRequestHead.IsPatch()) {
    // If the post id is already set then this is an attempt to replay
    // a post/patch transaction via the cache.  Otherwise, we need a unique
    // post id for this transaction.
    if (mPostID == 0) {
      mPostID = gHttpHandler->GenerateUniqueID();
    }

    if (StaticPrefs::network_http_idempotencyKey_enabled() &&
        !mRequestHead.HasHeader(nsHttp::Idempotency_Key)) {
      // check if we need to add
      // idempotency-key header
      // See Bug 1830022 for more details
      nsAutoCString key;
      gHttpHandler->GenerateIdempotencyKeyForPost(mPostID, mLoadInfo, key);
      MOZ_ALWAYS_SUCCEEDS(
          mRequestHead.SetHeader(nsHttp::Idempotency_Key, key, false));
    }
  }

#ifdef MOZ_WIDGET_ANDROID
  bool val = false;
  if (nsIOService::ShouldAddAdditionalSearchHeaders(mURI, &val)) {
    SetRequestHeader("X-Search-Subdivision"_ns, val ? "1"_ns : "0"_ns, false);
  }
#endif

  bool isTrackingResource = IsThirdPartyTrackingResource();
  LOG(("nsHttpChannel %p tracking resource=%d, cos=%lu, inc=%d", this,
       isTrackingResource, mClassOfService.Flags(),
       mClassOfService.Incremental()));

  if (isTrackingResource) {
    AddClassFlags(nsIClassOfService::Tail);
  }

  if (WaitingForTailUnblock()) {
    MOZ_DIAGNOSTIC_ASSERT(!mOnTailUnblock);
    mOnTailUnblock = &nsHttpChannel::ConnectOnTailUnblock;
    return NS_OK;
  }

  return ConnectOnTailUnblock();
}

nsresult nsHttpChannel::ConnectOnTailUnblock() {
  nsresult rv;

  LOG(("nsHttpChannel::ConnectOnTailUnblock [this=%p]\n", this));
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::ConnectOnTailUnblock", NETWORK,
                            Flow::FromPointer(this));

  // Consider opening a TCP connection right away.
  SpeculativeConnect();

  // open a cache entry for this channel...
  rv = OpenCacheEntry(mURI->SchemeIs("https"));

  // do not continue if asyncOpenCacheEntry is in progress
  if (AwaitingCacheCallbacks()) {
    LOG(("nsHttpChannel::Connect %p AwaitingCacheCallbacks forces async\n",
         this));
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Unexpected state");

    if (mNetworkTriggered && mWaitingForProxy) {
      // Someone has called TriggerNetwork(), meaning we are racing the
      // network with the cache.
      mWaitingForProxy = false;
      return ContinueConnect();
    }

    return NS_OK;
  }

  if (NS_FAILED(rv)) {
    LOG(("OpenCacheEntry failed [rv=%" PRIx32 "]\n",
         static_cast<uint32_t>(rv)));
    // if this channel is only allowed to pull from the cache, then
    // we must fail if we were unable to open a cache entry.
    if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
      return NS_ERROR_DOCUMENT_NOT_CACHED;
    }
    // otherwise, let's just proceed without using the cache.
  }

  if (mRaceCacheWithNetwork && ((mCacheEntry && !CachedContentIsValid() &&
                                 (mDidReval || LoadCachedContentIsPartial())) ||
                                mIgnoreCacheEntry)) {
    // We won't send the conditional request because the unconditional
    // request was already sent (see bug 1377223).
    glean::network::race_cache_validation
        .EnumGet(glean::network::RaceCacheValidationLabel::eNotsent)
        .Add();
  }

  // When racing, if OnCacheEntryAvailable is called before AsyncOpenURI
  // returns, then we may not have started reading from the cache.
  // If the content is valid, we should attempt to do so, as technically the
  // cache has won the race.
  if (mRaceCacheWithNetwork && CachedContentIsValid()) {
    Unused << ReadFromCache();
  }

  return TriggerNetwork();
}

nsresult nsHttpChannel::ContinueConnect() {
  // If we need to start a CORS preflight, do it now!
  // Note that it is important to do this before the early returns below.
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::ContinueConnect", NETWORK,
                            Flow::FromPointer(this));
  if (!LoadIsCorsPreflightDone() && LoadRequireCORSPreflight()) {
    MOZ_ASSERT(!mPreflightChannel);
    nsresult rv = nsCORSListenerProxy::StartCORSPreflight(
        this, this, mUnsafeHeaders, getter_AddRefs(mPreflightChannel));
    return rv;
  }

  MOZ_RELEASE_ASSERT(!LoadRequireCORSPreflight() || LoadIsCorsPreflightDone(),
                     "CORS preflight must have been finished by the time we "
                     "do the rest of ContinueConnect");

  RefPtr<mozilla::dom::BrowsingContext> bc;
  mLoadInfo->GetBrowsingContext(getter_AddRefs(bc));

  // we may or may not have a cache entry at this point
  if (mCacheEntry) {
    // read straight from the cache if possible...
    if (CachedContentIsValid()) {
      // If we're forced offline, and set to bypass the cache, return offline.
      if (bc && bc->Top()->GetForceOffline() &&
          BYPASS_LOCAL_CACHE(mLoadFlags, LoadPreferCacheLoadOverBypass())) {
        return NS_ERROR_OFFLINE;
      }

      nsRunnableMethod<nsHttpChannel>* event = nullptr;
      nsresult rv;
      if (!LoadCachedContentIsPartial()) {
        rv = AsyncCall(&nsHttpChannel::AsyncOnExamineCachedResponse, &event);
        if (NS_FAILED(rv)) {
          LOG(("  AsyncCall failed (%08x)", static_cast<uint32_t>(rv)));
        }
      }
      rv = ReadFromCache();
      if (NS_FAILED(rv) && event) {
        event->Revoke();
      }

      AccumulateCacheHitTelemetry(kCacheHit, this);
      mCacheDisposition = kCacheHit;

      return rv;
    }
    if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
      // the cache contains the requested resource, but it must be
      // validated before we can reuse it.  since we are not allowed
      // to hit the net, there's nothing more to do.  the document
      // is effectively not in the cache.
      LOG(("  !CachedContentIsValid() && mLoadFlags & LOAD_ONLY_FROM_CACHE"));
      return NS_ERROR_DOCUMENT_NOT_CACHED;
    }
  } else if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
    LOG(("  !mCacheEntry && mLoadFlags & LOAD_ONLY_FROM_CACHE"));
    return NS_ERROR_DOCUMENT_NOT_CACHED;
  }

  if (mLoadFlags & LOAD_NO_NETWORK_IO) {
    LOG(("  mLoadFlags & LOAD_NO_NETWORK_IO"));
    return NS_ERROR_DOCUMENT_NOT_CACHED;
  }

  // We're about to hit the network. Don't if we're forced offline.
  if (bc && bc->Top()->GetForceOffline()) {
    return NS_ERROR_OFFLINE;
  }

  // hit the net...
  nsresult rv = DoConnect(mTransactionSticky);
  mTransactionSticky = nullptr;
  return rv;
}

nsresult nsHttpChannel::DoConnect(HttpTransactionShell* aTransWithStickyConn) {
  LOG(("nsHttpChannel::DoConnect [this=%p]\n", this));

  if (!mDNSBlockingPromise.IsEmpty()) {
    LOG(("  waiting for DNS prefetch"));

    // Transaction is passed only from auth retry for which we will definitely
    // not block on DNS to alter the origin server name for IP; it has already
    // been done.
    MOZ_ASSERT(!aTransWithStickyConn);
    MOZ_ASSERT(mDNSBlockingThenable);

    nsCOMPtr<nsISerialEventTarget> target(do_GetMainThread());
    RefPtr<nsHttpChannel> self(this);
    mDNSBlockingThenable->Then(
        target, __func__,
        [self](const nsCOMPtr<nsIDNSRecord>& aRec) {
          nsresult rv = self->DoConnectActual(nullptr);
          if (NS_FAILED(rv)) {
            self->CloseCacheEntry(false);
            Unused << self->AsyncAbort(rv);
          }
        },
        [self](nsresult err) {
          self->CloseCacheEntry(false);
          Unused << self->AsyncAbort(err);
        });

    // The connection will continue when the promise is resolved in
    // OnLookupComplete.
    return NS_OK;
  }

  return DoConnectActual(aTransWithStickyConn);
}

nsresult nsHttpChannel::DoConnectActual(
    HttpTransactionShell* aTransWithStickyConn) {
  LOG(("nsHttpChannel::DoConnectActual [this=%p, aTransWithStickyConn=%p]\n",
       this, aTransWithStickyConn));

  nsresult rv = SetupChannelForTransaction();
  if (NS_FAILED(rv)) {
    return rv;
  }

  return DispatchTransaction(aTransWithStickyConn);
}

nsresult nsHttpChannel::DispatchTransaction(
    HttpTransactionShell* aTransWithStickyConn) {
  LOG(("nsHttpChannel::DispatchTransaction [this=%p, aTransWithStickyConn=%p]",
       this, aTransWithStickyConn));
  nsresult rv = InitTransaction();
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (aTransWithStickyConn) {
    rv = gHttpHandler->InitiateTransactionWithStickyConn(
        mTransaction, mPriority, aTransWithStickyConn);
  } else {
    rv = gHttpHandler->InitiateTransaction(mTransaction, mPriority);
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = mTransaction->AsyncRead(this, getter_AddRefs(mTransactionPump));
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint32_t suspendCount = mSuspendCount;
  if (LoadAsyncResumePending()) {
    LOG(
        ("  Suspend()'ing transaction pump once because of async resume pending"
         ", sc=%u, pump=%p, this=%p",
         suspendCount, mTransactionPump.get(), this));
    ++suspendCount;
  }
  while (suspendCount--) {
    mTransactionPump->Suspend();
  }

  return NS_OK;
}

void nsHttpChannel::SpeculativeConnect() {
  // Before we take the latency hit of dealing with the cache, try and
  // get the TCP (and SSL) handshakes going so they can overlap.

  // don't speculate if we are offline, when doing http upgrade (i.e.
  // websockets bootstrap), or if we can't do keep-alive (because then we
  // couldn't reuse the speculative connection anyhow).
  RefPtr<mozilla::dom::BrowsingContext> bc;
  mLoadInfo->GetBrowsingContext(getter_AddRefs(bc));

  if (gIOService->IsOffline() || mUpgradeProtocolCallback ||
      !(mCaps & NS_HTTP_ALLOW_KEEPALIVE) ||
      (bc && bc->Top()->GetForceOffline())) {
    return;
  }

  // LOAD_ONLY_FROM_CACHE and LOAD_NO_NETWORK_IO must not hit network.
  // LOAD_FROM_CACHE is unlikely to hit network, so skip preconnects for it.
  if (mLoadFlags &
      (LOAD_ONLY_FROM_CACHE | LOAD_FROM_CACHE | LOAD_NO_NETWORK_IO)) {
    return;
  }

  if (LoadAllowStaleCacheContent()) {
    return;
  }

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  NS_NewNotificationCallbacksAggregation(mCallbacks, mLoadGroup,
                                         getter_AddRefs(callbacks));
  if (!callbacks) return;
  bool httpsRRAllowed = !(mCaps & NS_HTTP_DISALLOW_HTTPS_RR);
  Unused << gHttpHandler->MaybeSpeculativeConnectWithHTTPSRR(
      mConnectionInfo, callbacks,
      mCaps & (NS_HTTP_DISALLOW_SPDY | NS_HTTP_TRR_MODE_MASK |
               NS_HTTP_DISABLE_IPV4 | NS_HTTP_DISABLE_IPV6 |
               NS_HTTP_DISALLOW_HTTP3 | NS_HTTP_REFRESH_DNS),
      nsHttpHandler::EchConfigEnabled() && httpsRRAllowed);
}

void nsHttpChannel::DoNotifyListenerCleanup() {
  // We don't need this info anymore
  CleanRedirectCacheChainIfNecessary();
}

void nsHttpChannel::ReleaseListeners() {
  HttpBaseChannel::ReleaseListeners();
  mChannelClassifier = nullptr;
  mWarningReporter = nullptr;
  mEarlyHintObserver = nullptr;
  mWebTransportSessionEventListener = nullptr;

  for (StreamFilterRequest& request : mStreamFilterRequests) {
    request.mPromise->Reject(false, __func__);
  }
  mStreamFilterRequests.Clear();
}

void nsHttpChannel::DoAsyncAbort(nsresult aStatus) {
  Unused << AsyncAbort(aStatus);
}

void nsHttpChannel::HandleAsyncRedirect() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(("Waiting until resume to do async redirect [this=%p]\n", this));
    mCallOnResume = [](nsHttpChannel* self) {
      self->HandleAsyncRedirect();
      return NS_OK;
    };
    return;
  }

  nsresult rv = NS_OK;

  LOG(("nsHttpChannel::HandleAsyncRedirect [this=%p]\n", this));

  // since this event is handled asynchronously, it is possible that this
  // channel could have been canceled, in which case there would be no point
  // in processing the redirect.
  if (NS_SUCCEEDED(mStatus)) {
    PushRedirectAsyncFunc(&nsHttpChannel::ContinueHandleAsyncRedirect);
    rv = AsyncProcessRedirection(mResponseHead->Status());
    if (NS_FAILED(rv)) {
      PopRedirectAsyncFunc(&nsHttpChannel::ContinueHandleAsyncRedirect);
      // TODO: if !DoNotRender3xxBody(), render redirect body instead.
      // But first we need to cache 3xx bodies (bug 748510)
      rv = ContinueHandleAsyncRedirect(rv);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  } else {
    rv = ContinueHandleAsyncRedirect(mStatus);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

nsresult nsHttpChannel::ContinueHandleAsyncRedirect(nsresult rv) {
  if (NS_FAILED(rv)) {
    // If AsyncProcessRedirection fails, then we have to send out the
    // OnStart/OnStop notifications.
    LOG(("ContinueHandleAsyncRedirect got failure result [rv=%" PRIx32 "]\n",
         static_cast<uint32_t>(rv)));

    bool redirectsEnabled = !mLoadInfo->GetDontFollowRedirects();

    if (redirectsEnabled) {
      // TODO: stop failing original channel if redirect vetoed?
      mStatus = rv;

      DoNotifyListener();

      // Blow away cache entry if we couldn't process the redirect
      // for some reason (the cache entry might be corrupt).
      if (mCacheEntry) {
        mCacheEntry->AsyncDoom(nullptr);
      }
    } else {
      DoNotifyListener();
    }
  }

  CloseCacheEntry(true);

  StoreIsPending(false);

  if (mLoadGroup) mLoadGroup->RemoveRequest(this, nullptr, mStatus);

  return NS_OK;
}

void nsHttpChannel::HandleAsyncNotModified() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(("Waiting until resume to do async not-modified [this=%p]\n", this));
    mCallOnResume = [](nsHttpChannel* self) {
      self->HandleAsyncNotModified();
      return NS_OK;
    };
    return;
  }

  LOG(("nsHttpChannel::HandleAsyncNotModified [this=%p]\n", this));

  DoNotifyListener();

  CloseCacheEntry(false);

  StoreIsPending(false);

  if (mLoadGroup) mLoadGroup->RemoveRequest(this, nullptr, mStatus);
}

nsresult nsHttpChannel::SetupChannelForTransaction() {
  LOG((
      "nsHttpChannel::SetupChannelForTransaction [this=%p, cos=%lu, inc=%d "
      "prio=%d]\n",
      this, mClassOfService.Flags(), mClassOfService.Incremental(), mPriority));

  NS_ENSURE_TRUE(!mTransaction, NS_ERROR_ALREADY_INITIALIZED);

  nsresult rv;

  mozilla::MutexAutoLock lock(mRCWNLock);

  if (StaticPrefs::network_http_priority_header_enabled()) {
    SetPriorityHeader();
  }

  // If we're racing cache with network, conditional or byte range header
  // could be added in OnCacheEntryCheck. We cannot send conditional request
  // without having the entry, so we need to remove the headers here and
  // ignore the cache entry in OnCacheEntryAvailable.
  if (mRaceCacheWithNetwork && AwaitingCacheCallbacks()) {
    if (mDidReval) {
      LOG(("  Removing conditional request headers"));
      UntieValidationRequest();
      mDidReval = false;
      mIgnoreCacheEntry = true;
    }

    if (LoadCachedContentIsPartial()) {
      LOG(("  Removing byte range request headers"));
      UntieByteRangeRequest();
      StoreCachedContentIsPartial(false);
      mIgnoreCacheEntry = true;
    }

    if (mIgnoreCacheEntry) {
      mAvailableCachedAltDataType.Truncate();
      StoreDeliveringAltData(false);
      mAltDataLength = -1;
      mCacheInputStream.CloseAndRelease();
    }
  }

  StoreUsedNetwork(1);

  if (!LoadAllowSpdy()) {
    mCaps |= NS_HTTP_DISALLOW_SPDY;
  }
  if (!LoadAllowHttp3()) {
    mCaps |= NS_HTTP_DISALLOW_HTTP3;
  }
  if (LoadBeConservative()) {
    mCaps |= NS_HTTP_BE_CONSERVATIVE;
  }

  if (mLoadFlags & LOAD_ANONYMOUS_ALLOW_CLIENT_CERT) {
    mCaps |= NS_HTTP_LOAD_ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT;
  }

  if (nsContentUtils::ShouldResistFingerprinting(this,
                                                 RFPTarget::HttpUserAgent)) {
    mCaps |= NS_HTTP_USE_RFP;
  }

  // Use the URI path if not proxying (transparent proxying such as proxy
  // CONNECT does not count here). Also figure out what HTTP version to use.
  nsAutoCString buf, path;
  nsCString* requestURI;

  // This is the normal e2e H1 path syntax "/index.html"
  rv = mURI->GetPathQueryRef(path);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // path may contain UTF-8 characters, so ensure that they're escaped.
  if (NS_EscapeURL(path.get(), path.Length(), esc_OnlyNonASCII | esc_Spaces,
                   buf)) {
    requestURI = &buf;
  } else {
    requestURI = &path;
  }

  // trim off the #ref portion if any...
  int32_t ref1 = requestURI->FindChar('#');
  if (ref1 != kNotFound) {
    requestURI->SetLength(ref1);
  }

  if (mConnectionInfo->UsingConnect() || !mConnectionInfo->UsingHttpProxy()) {
    mRequestHead.SetVersion(gHttpHandler->HttpVersion());
  } else {
    mRequestHead.SetPath(*requestURI);

    // RequestURI should be the absolute uri H1 proxy syntax
    // "http://foo/index.html" so we will overwrite the relative version in
    // requestURI
    rv = mURI->GetUserPass(buf);
    if (NS_FAILED(rv)) return rv;
    if (!buf.IsEmpty() && ((strncmp(mSpec.get(), "http:", 5) == 0) ||
                           strncmp(mSpec.get(), "https:", 6) == 0)) {
      nsCOMPtr<nsIURI> tempURI = nsIOService::CreateExposableURI(mURI);
      rv = tempURI->GetAsciiSpec(path);
      if (NS_FAILED(rv)) return rv;
      requestURI = &path;
    } else {
      requestURI = &mSpec;
    }

    // trim off the #ref portion if any...
    int32_t ref2 = requestURI->FindChar('#');
    if (ref2 != kNotFound) {
      requestURI->SetLength(ref2);
    }

    mRequestHead.SetVersion(gHttpHandler->ProxyHttpVersion());
  }

  mRequestHead.SetRequestURI(*requestURI);

  // set the request time for cache expiration calculations
  mRequestTime = NowInSeconds();
  StoreRequestTimeInitialized(true);

  // if doing a reload, force end-to-end
  if (mLoadFlags & LOAD_BYPASS_CACHE) {
    // We need to send 'Pragma:no-cache' to inhibit proxy caching even if
    // no proxy is configured since we might be talking with a transparent
    // proxy, i.e. one that operates at the network level.  See bug #14772.
    // But we should not touch Pragma if Cache-Control is already set
    // (https://fetch.spec.whatwg.org/#ref-for-concept-request-cache-mode%E2%91%A3)
    if (!mRequestHead.HasHeader(nsHttp::Pragma)) {
      rv = mRequestHead.SetHeaderOnce(nsHttp::Pragma, "no-cache", true);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
    // If we're configured to speak HTTP/1.1 then also send 'Cache-control:
    // no-cache'. But likewise don't touch Cache-Control if it's already set.
    if (mRequestHead.Version() >= HttpVersion::v1_1 &&
        !mRequestHead.HasHeader(nsHttp::Cache_Control)) {
      rv = mRequestHead.SetHeaderOnce(nsHttp::Cache_Control, "no-cache", true);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  } else if (mLoadFlags & VALIDATE_ALWAYS) {
    // We need to send 'Cache-Control: max-age=0' to force each cache along
    // the path to the origin server to revalidate its own entry, if any,
    // with the next cache or server.  See bug #84847.
    //
    // If we're configured to speak HTTP/1.0 then just send 'Pragma: no-cache'
    //
    // But don't send the headers if they're already set:
    // https://fetch.spec.whatwg.org/#ref-for-concept-request-cache-mode%E2%91%A2
    if (mRequestHead.Version() >= HttpVersion::v1_1) {
      if (!mRequestHead.HasHeader(nsHttp::Cache_Control)) {
        rv = mRequestHead.SetHeaderOnce(nsHttp::Cache_Control, "max-age=0",
                                        true);
      }
    } else {
      if (!mRequestHead.HasHeader(nsHttp::Pragma)) {
        rv = mRequestHead.SetHeaderOnce(nsHttp::Pragma, "no-cache", true);
      }
    }
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  if (LoadResuming()) {
    char byteRange[32];
    SprintfLiteral(byteRange, "bytes=%" PRIu64 "-", mStartPos);
    rv = mRequestHead.SetHeader(nsHttp::Range, nsDependentCString(byteRange));
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    if (!mEntityID.IsEmpty()) {
      // Also, we want an error if this resource changed in the meantime
      // Format of the entity id is: escaped_etag/size/lastmod
      nsCString::const_iterator start, end, slash;
      mEntityID.BeginReading(start);
      mEntityID.EndReading(end);
      mEntityID.BeginReading(slash);

      if (FindCharInReadable('/', slash, end)) {
        nsAutoCString ifMatch;
        rv = mRequestHead.SetHeader(
            nsHttp::If_Match,
            NS_UnescapeURL(Substring(start, slash), 0, ifMatch));
        MOZ_ASSERT(NS_SUCCEEDED(rv));

        ++slash;  // Incrementing, so that searching for '/' won't find
                  // the same slash again
      }

      if (FindCharInReadable('/', slash, end)) {
        rv = mRequestHead.SetHeader(nsHttp::If_Unmodified_Since,
                                    Substring(++slash, end));
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      }
    }
  }

  // See bug #466080. Transfer LOAD_ANONYMOUS flag to socket-layer.
  if (mLoadFlags & LOAD_ANONYMOUS) mCaps |= NS_HTTP_LOAD_ANONYMOUS;

  if (mUpgradeProtocolCallback) {
    rv = mRequestHead.SetHeader(nsHttp::Upgrade, mUpgradeProtocol, false);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = mRequestHead.SetHeaderOnce(nsHttp::Connection, nsHttp::Upgrade.get(),
                                    true);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    mCaps |= NS_HTTP_STICKY_CONNECTION;
    mCaps &= ~NS_HTTP_ALLOW_KEEPALIVE;
  }

  if (mWebTransportSessionEventListener) {
    mCaps |= NS_HTTP_STICKY_CONNECTION;
  }

  return NS_OK;
}

nsresult nsHttpChannel::InitTransaction() {
  nsresult rv;
  // create wrapper for this channel's notification callbacks
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  NS_NewNotificationCallbacksAggregation(mCallbacks, mLoadGroup,
                                         getter_AddRefs(callbacks));

  // create the transaction object
  if (nsIOService::UseSocketProcess()) {
    if (NS_WARN_IF(!gIOService->SocketProcessReady())) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    RefPtr<SocketProcessParent> socketProcess =
        SocketProcessParent::GetSingleton();
    if (!socketProcess->CanSend()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    nsCOMPtr<nsIParentChannel> parentChannel;
    NS_QueryNotificationCallbacks(this, parentChannel);
    RefPtr<DocumentLoadListener> documentChannelParent =
        do_QueryObject(parentChannel);
    // See HttpTransactionChild::CanSendODAToContentProcessDirectly() and
    // nsHttpChannel::CallOnStartRequest() for the reason why we need to know if
    // this is a document load. We only send ODA directly to child process for
    // non document loads.
    RefPtr<HttpTransactionParent> transParent =
        new HttpTransactionParent(!!documentChannelParent);
    LOG1(("nsHttpChannel %p created HttpTransactionParent %p\n", this,
          transParent.get()));

    // Since OnStopRequest could be sent to child process from socket process
    // directly, we need to store these two values in HttpTransactionChild and
    // forward to child process until HttpTransactionChild::OnStopRequest is
    // called.
    transParent->SetRedirectTimestamp(mRedirectStartTimeStamp,
                                      mRedirectEndTimeStamp);

    if (socketProcess) {
      MOZ_ALWAYS_TRUE(
          socketProcess->SendPHttpTransactionConstructor(transParent));
    }
    mTransaction = transParent;
  } else {
    mTransaction = new nsHttpTransaction();
    LOG1(("nsHttpChannel %p created nsHttpTransaction %p\n", this,
          mTransaction.get()));
  }

  // Save the mapping of channel id and the channel. We need this mapping for
  // nsIHttpActivityObserver.
  gHttpHandler->AddHttpChannel(mChannelId, ToSupports(this));

  EnsureBrowserId();
  EnsureRequestContext();

  HttpTrafficCategory category = CreateTrafficCategory();
  std::function<void(TransactionObserverResult&&)> observer;
  if (mTransactionObserver) {
    observer = [transactionObserver{std::move(mTransactionObserver)}](
                   TransactionObserverResult&& aResult) {
      transactionObserver->Complete(aResult.versionOk(), aResult.authOk(),
                                    aResult.closeReason());
    };
  }
  mTransaction->SetIsForWebTransport(!!mWebTransportSessionEventListener);
  rv = mTransaction->Init(
      mCaps, mConnectionInfo, &mRequestHead, mUploadStream, mReqContentLength,
      LoadUploadStreamHasHeaders(), GetCurrentSerialEventTarget(), callbacks,
      this, mBrowserId, category, mRequestContext, mClassOfService,
      mInitialRwin, LoadResponseTimeoutEnabled(), mChannelId,
      std::move(observer), nullptr, nullptr, 0);
  if (NS_FAILED(rv)) {
    mTransaction = nullptr;
    return rv;
  }

  return rv;
}

HttpTrafficCategory nsHttpChannel::CreateTrafficCategory() {
  MOZ_ASSERT(!mFirstPartyClassificationFlags ||
             !mThirdPartyClassificationFlags);

  if (!StaticPrefs::network_traffic_analyzer_enabled()) {
    return HttpTrafficCategory::eInvalid;
  }

  HttpTrafficAnalyzer::ClassOfService cos;
  {
    if ((mClassOfService.Flags() & nsIClassOfService::Leader) &&
        mLoadInfo->GetExternalContentPolicyType() ==
            ExtContentPolicy::TYPE_SCRIPT) {
      cos = HttpTrafficAnalyzer::ClassOfService::eLeader;
    } else if (mLoadFlags & nsIRequest::LOAD_BACKGROUND) {
      cos = HttpTrafficAnalyzer::ClassOfService::eBackground;
    } else {
      cos = HttpTrafficAnalyzer::ClassOfService::eOther;
    }
  }

  bool isThirdParty = AntiTrackingUtils::IsThirdPartyChannel(this);

  HttpTrafficAnalyzer::TrackingClassification tc;
  {
    uint32_t flags = isThirdParty ? mThirdPartyClassificationFlags
                                  : mFirstPartyClassificationFlags;

    using CF = nsIClassifiedChannel::ClassificationFlags;
    using TC = HttpTrafficAnalyzer::TrackingClassification;

    if (flags & CF::CLASSIFIED_TRACKING_CONTENT) {
      tc = TC::eContent;
    } else if (flags & CF::CLASSIFIED_FINGERPRINTING_CONTENT) {
      tc = TC::eFingerprinting;
    } else if (flags & CF::CLASSIFIED_ANY_BASIC_TRACKING) {
      tc = TC::eBasic;
    } else {
      tc = TC::eNone;
    }
  }

  bool isSystemPrincipal =
      mLoadInfo->GetLoadingPrincipal() &&
      mLoadInfo->GetLoadingPrincipal()->IsSystemPrincipal();
  return HttpTrafficAnalyzer::CreateTrafficCategory(
      NS_UsePrivateBrowsing(this), isSystemPrincipal, isThirdParty, cos, tc);
}

void nsHttpChannel::SetCachedContentType() {
  if (!mResponseHead) {
    return;
  }

  nsAutoCString contentTypeStr;
  mResponseHead->ContentType(contentTypeStr);

  uint8_t contentType = nsICacheEntry::CONTENT_TYPE_OTHER;
  if (nsContentUtils::IsJavascriptMIMEType(
          NS_ConvertUTF8toUTF16(contentTypeStr))) {
    contentType = nsICacheEntry::CONTENT_TYPE_JAVASCRIPT;
  } else if (StringBeginsWith(contentTypeStr, "text/css"_ns) ||
             (mLoadInfo->GetExternalContentPolicyType() ==
              ExtContentPolicy::TYPE_STYLESHEET)) {
    contentType = nsICacheEntry::CONTENT_TYPE_STYLESHEET;
  } else if (StringBeginsWith(contentTypeStr, "application/wasm"_ns)) {
    contentType = nsICacheEntry::CONTENT_TYPE_WASM;
  } else if (StringBeginsWith(contentTypeStr, "image/"_ns)) {
    contentType = nsICacheEntry::CONTENT_TYPE_IMAGE;
  } else if (StringBeginsWith(contentTypeStr, "video/"_ns)) {
    contentType = nsICacheEntry::CONTENT_TYPE_MEDIA;
  } else if (StringBeginsWith(contentTypeStr, "audio/"_ns)) {
    contentType = nsICacheEntry::CONTENT_TYPE_MEDIA;
  }

  mCacheEntry->SetContentType(contentType);
}

nsresult nsHttpChannel::CallOnStartRequest() {
  LOG(("nsHttpChannel::CallOnStartRequest [this=%p]", this));

  MOZ_RELEASE_ASSERT(!LoadRequireCORSPreflight() || LoadIsCorsPreflightDone(),
                     "CORS preflight must have been finished by the time we "
                     "call OnStartRequest");

  MOZ_RELEASE_ASSERT(mCanceled || LoadProcessCrossOriginSecurityHeadersCalled(),
                     "Security headers need to have been processed before "
                     "calling CallOnStartRequest");

  mEarlyHintObserver = nullptr;

  if (StaticPrefs::network_http_network_error_logging_enabled() &&
      mResponseHead && mResponseHead->HasHeader(nsHttp::NEL) &&
      LoadUsedNetwork()) {
    // If the policy gets cleared, but this response is still in the cache,
    // we probably don't want to reinstall the policy when the entry is
    // reloaded. That means it's important to only call RegisterPolicy when
    // LoadUsedNetwork() is true.
    if (nsCOMPtr<nsINetworkErrorLogging> nel =
            components::NetworkErrorLogging::Service()) {
      nel->RegisterPolicy(this);
    }
  }

  if (LoadOnStartRequestCalled()) {
    // This can only happen when a range request loading rest of the data
    // after interrupted concurrent cache read asynchronously failed, e.g.
    // the response range bytes are not as expected or this channel has
    // been externally canceled.
    //
    // It's legal to bypass CallOnStartRequest for that case since we've
    // already called OnStartRequest on our listener and also added all
    // content converters before.
    MOZ_ASSERT(LoadConcurrentCacheAccess());
    LOG(("CallOnStartRequest already invoked before"));
    return mStatus;
  }

  // Ensure mListener->OnStartRequest will be invoked before exiting
  // this function.
  auto onStartGuard = MakeScopeExit([&] {
    LOG(
        ("  calling mListener->OnStartRequest by ScopeExit [this=%p, "
         "listener=%p]\n",
         this, mListener.get()));
    MOZ_ASSERT(!LoadOnStartRequestCalled());

    if (mListener) {
      nsCOMPtr<nsIStreamListener> deleteProtector(mListener);
      StoreOnStartRequestCalled(true);
      deleteProtector->OnStartRequest(this);
    }
    StoreOnStartRequestCalled(true);
  });

  nsresult rv = ValidateMIMEType();
  // Since ODA and OnStopRequest could be sent from socket process directly, we
  // need to update the channel status before calling mListener->OnStartRequest.
  // This is the only way to let child process discard the already received ODA
  // messages.
  if (NS_FAILED(rv)) {
    mStatus = rv;
    return mStatus;
  }

  // EnsureOpaqueResponseIsAllowed and EnsureOpauqeResponseIsAllowedAfterSniff
  // are the checks for Opaque Response Blocking to ensure that we block as many
  // cross-origin responses with CORS headers as possible that are not either
  // Javascript or media to avoid leaking their contents through side channels.
  OpaqueResponse opaqueResponse =
      PerformOpaqueResponseSafelistCheckBeforeSniff();
  if (opaqueResponse == OpaqueResponse::Block) {
    SetChannelBlockedByOpaqueResponse();
    CancelWithReason(NS_BINDING_ABORTED,
                     "OpaqueResponseBlocker::BlockResponse"_ns);
    return NS_BINDING_ABORTED;
  }

  // Allow consumers to override our content type
  if (mLoadFlags & LOAD_CALL_CONTENT_SNIFFERS) {
    // NOTE: We can have both a txn pump and a cache pump when the cache
    // content is partial. In that case, we need to read from the cache,
    // because that's the one that has the initial contents. If that fails
    // then give the transaction pump a shot.

    nsIChannel* thisChannel = static_cast<nsIChannel*>(this);

    bool typeSniffersCalled = false;
    if (mCachePump) {
      typeSniffersCalled =
          NS_SUCCEEDED(mCachePump->PeekStream(CallTypeSniffers, thisChannel));
    }

    if (!typeSniffersCalled && mTransactionPump) {
      RefPtr<nsInputStreamPump> pump = do_QueryObject(mTransactionPump);
      if (pump) {
        pump->PeekStream(CallTypeSniffers, thisChannel);
      } else {
        MOZ_ASSERT(nsIOService::UseSocketProcess());
        RefPtr<HttpTransactionParent> trans = do_QueryObject(mTransactionPump);
        MOZ_ASSERT(trans);
        trans->SetSniffedTypeToChannel(CallTypeSniffers, thisChannel);
      }
    }
  }

  // Note that the code below should be synced with the code in
  // HttpTransactionChild::CanSendODAToContentProcessDirectly(). We MUST make
  // sure HttpTransactionChild::CanSendODAToContentProcessDirectly() returns
  // false when a stream converter is applied.
  bool unknownDecoderStarted = false;
  if (mResponseHead && !mResponseHead->HasContentType()) {
    MOZ_ASSERT(mConnectionInfo, "Should have connection info here");
    if (!mContentTypeHint.IsEmpty()) {
      mResponseHead->SetContentType(mContentTypeHint);
    } else if (mResponseHead->Version() == HttpVersion::v0_9 &&
               mConnectionInfo->OriginPort() !=
                   mConnectionInfo->DefaultPort()) {
      mResponseHead->SetContentType(nsLiteralCString(TEXT_PLAIN));
    } else {
      // Uh-oh.  We had better find out what type we are!
      mListener = new nsUnknownDecoder(mListener);
      unknownDecoderStarted = true;
    }
  }

  // If unknownDecoder is not going to be launched, call
  // EnsureOpaqueResponseIsAllowedAfterSniff immediately.
  if (!unknownDecoderStarted) {
    if (opaqueResponse == OpaqueResponse::SniffCompressed) {
      mListener = new nsCompressedAudioVideoImageDetector(
          mListener, &HttpBaseChannel::CallTypeSniffers);
    } else if (opaqueResponse == OpaqueResponse::Sniff) {
      MOZ_DIAGNOSTIC_ASSERT(mORB);
      nsresult rv = mORB->EnsureOpaqueResponseIsAllowedAfterSniff(this);

      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  // If the content is multipart/x-mixed-replace, we'll insert a MIME decoder
  // in the pipeline to handle the content and pass it along to our
  // original listener. nsUnknownDecoder doesn't support detecting this type,
  // so we only need to insert this using the response header's mime type.
  //
  // We only do this for unwrapped document loads, since we might want to send
  // parts to the external protocol handler without leaving the parent process.
  bool mustRunStreamFilterInParent = false;
  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(this, parentChannel);
  RefPtr<DocumentLoadListener> docListener = do_QueryObject(parentChannel);
  if (mResponseHead && docListener && docListener->GetChannel() == this) {
    nsAutoCString contentType;
    mResponseHead->ContentType(contentType);

    if (contentType.Equals("multipart/x-mixed-replace"_ns)) {
      nsCOMPtr<nsIStreamConverterService> convServ(
          mozilla::components::StreamConverter::Service(&rv));
      if (NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsIStreamListener> toListener(mListener);
        nsCOMPtr<nsIStreamListener> fromListener;

        rv = convServ->AsyncConvertData("multipart/x-mixed-replace", "*/*",
                                        toListener, nullptr,
                                        getter_AddRefs(fromListener));
        if (NS_SUCCEEDED(rv)) {
          mListener = fromListener;
          mustRunStreamFilterInParent = true;
        }
      }
    }
  }

  // If we installed a multipart converter, then we need to add StreamFilter
  // object before it, so that extensions see the un-parsed original stream.
  // We may want to add an option for extensions to opt-in to proper multipart
  // handling.
  // If not, then pass the StreamFilter promise on to DocumentLoadListener,
  // where it'll be added in the content process.
  for (StreamFilterRequest& request : mStreamFilterRequests) {
    if (mustRunStreamFilterInParent) {
      mozilla::ipc::Endpoint<extensions::PStreamFilterParent> parent;
      mozilla::ipc::Endpoint<extensions::PStreamFilterChild> child;
      nsresult rv = extensions::PStreamFilter::CreateEndpoints(&parent, &child);
      if (NS_FAILED(rv)) {
        request.mPromise->Reject(false, __func__);
      } else {
        extensions::StreamFilterParent::Attach(this, std::move(parent));
        request.mPromise->Resolve(std::move(child), __func__);
      }
    } else {
      if (docListener) {
        docListener->AttachStreamFilter()->ChainTo(request.mPromise.forget(),
                                                   __func__);
      } else {
        request.mPromise->Reject(false, __func__);
      }
    }
    request.mPromise = nullptr;
  }
  mStreamFilterRequests.Clear();
  StoreTracingEnabled(false);

  if (mResponseHead && !mResponseHead->HasContentCharset()) {
    mResponseHead->SetContentCharset(mContentCharsetHint);
  }

  if (mCacheEntry && LoadCacheEntryIsWriteOnly()) {
    SetCachedContentType();
  }

  LOG(("  calling mListener->OnStartRequest [this=%p, listener=%p]\n", this,
       mListener.get()));

  // About to call OnStartRequest, dismiss the guard object.
  onStartGuard.release();

  if (mListener) {
    MOZ_ASSERT(!LoadOnStartRequestCalled(),
               "We should not call OsStartRequest twice");
    nsCOMPtr<nsIStreamListener> deleteProtector(mListener);
    StoreOnStartRequestCalled(true);
    rv = deleteProtector->OnStartRequest(this);
    if (NS_FAILED(rv)) return rv;
  } else {
    NS_WARNING("OnStartRequest skipped because of null listener");
    StoreOnStartRequestCalled(true);
  }

  // Install stream converter if required.
  // Normally, we expect the listener to disable content conversion during
  // OnStartRequest if it wants to handle it itself (which is common case with
  // HttpChannelParent, disabling so that it can be done in the content
  // process). If we've installed an nsUnknownDecoder, then we won't yet have
  // called OnStartRequest on the final listener (that happens after we send
  // OnDataAvailable to the nsUnknownDecoder), so it can't yet have disabled
  // content conversion.
  // In that case, assume that the listener will disable content conversion,
  // unless it's specifically told us that it won't.
  if (!unknownDecoderStarted || LoadListenerRequiresContentConversion()) {
    nsCOMPtr<nsIStreamListener> listener;
    rv =
        DoApplyContentConversions(mListener, getter_AddRefs(listener), nullptr);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (listener) {
      MOZ_ASSERT(!LoadDataSentToChildProcess(),
                 "DataSentToChildProcess being true means ODAs are sent to "
                 "the child process directly. We MUST NOT apply content "
                 "converter in this case.");
      mListener = listener;
      mCompressListener = listener;

      StoreHasAppliedConversion(true);
    }
  }

  // if this channel is for a download, close off access to the cache.
  if (mCacheEntry && LoadChannelIsForDownload()) {
    mCacheEntry->AsyncDoom(nullptr);

    // We must keep the cache entry in case of partial request.
    // Concurrent access is the same, we need the entry in
    // OnStopRequest.
    // We also need the cache entry when racing cache with network to find
    // out what is the source of the data.
    if (!LoadCachedContentIsPartial() && !LoadConcurrentCacheAccess() &&
        !(mRaceCacheWithNetwork &&
          mFirstResponseSource == RESPONSE_FROM_CACHE)) {
      CloseCacheEntry(false);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::GetHttpProxyConnectResponseCode(
    int32_t* aResponseCode) {
  NS_ENSURE_ARG_POINTER(aResponseCode);

  if (mConnectionInfo && mConnectionInfo->UsingConnect()) {
    *aResponseCode = mProxyConnectResponseCode;
  } else {
    *aResponseCode = -1;
  }
  return NS_OK;
}

nsresult nsHttpChannel::ProcessFailedProxyConnect(uint32_t httpStatus) {
  // Failure to set up a proxy tunnel via CONNECT means one of the following:
  // 1) Proxy wants authorization, or forbids.
  // 2) DNS at proxy couldn't resolve target URL.
  // 3) Proxy connection to target failed or timed out.
  // 4) Eve intercepted our CONNECT, and is replying with malicious HTML.
  //
  // Our current architecture would parse the proxy's response content with
  // the permission of the target URL.  Given #4, we must avoid rendering the
  // body of the reply, and instead give the user a (hopefully helpful)
  // boilerplate error page, based on just the HTTP status of the reply.

  MOZ_ASSERT(mConnectionInfo->UsingConnect(),
             "proxy connect failed but not using CONNECT?");
  nsresult rv = HttpProxyResponseToErrorCode(httpStatus);
  LOG(("Cancelling failed proxy CONNECT [this=%p httpStatus=%u]\n", this,
       httpStatus));

  // Make sure the connection is thrown away as it can be in a bad state
  // and the proxy may just hang on the next request.
  MOZ_ASSERT(mTransaction);
  mTransaction->DontReuseConnection();

  Cancel(rv);
  {
    nsresult rv = CallOnStartRequest();
    if (NS_FAILED(rv)) {
      LOG(("CallOnStartRequest failed [this=%p httpStatus=%u rv=%08x]\n", this,
           httpStatus, static_cast<uint32_t>(rv)));
    }
  }
  return rv;
}

static void GetSTSConsoleErrorTag(uint32_t failureResult,
                                  nsAString& consoleErrorTag) {
  switch (failureResult) {
    case nsISiteSecurityService::ERROR_COULD_NOT_PARSE_HEADER:
      consoleErrorTag = u"STSCouldNotParseHeader"_ns;
      break;
    case nsISiteSecurityService::ERROR_NO_MAX_AGE:
      consoleErrorTag = u"STSNoMaxAge"_ns;
      break;
    case nsISiteSecurityService::ERROR_MULTIPLE_MAX_AGES:
      consoleErrorTag = u"STSMultipleMaxAges"_ns;
      break;
    case nsISiteSecurityService::ERROR_INVALID_MAX_AGE:
      consoleErrorTag = u"STSInvalidMaxAge"_ns;
      break;
    case nsISiteSecurityService::ERROR_MULTIPLE_INCLUDE_SUBDOMAINS:
      consoleErrorTag = u"STSMultipleIncludeSubdomains"_ns;
      break;
    case nsISiteSecurityService::ERROR_INVALID_INCLUDE_SUBDOMAINS:
      consoleErrorTag = u"STSInvalidIncludeSubdomains"_ns;
      break;
    case nsISiteSecurityService::ERROR_COULD_NOT_SAVE_STATE:
      consoleErrorTag = u"STSCouldNotSaveState"_ns;
      break;
    default:
      consoleErrorTag = u"STSUnknownError"_ns;
      break;
  }
}

/**
 * Process an HTTP Strict Transport Security (HSTS) header.
 */
nsresult nsHttpChannel::ProcessHSTSHeader(nsITransportSecurityInfo* aSecInfo) {
  nsHttpAtom atom(nsHttp::ResolveAtom("Strict-Transport-Security"_ns));

  nsAutoCString securityHeader;
  nsresult rv = mResponseHead->GetHeader(atom, securityHeader);
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    LOG(("nsHttpChannel: No %s header, continuing load.\n", atom.get()));
    return NS_OK;
  }
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!aSecInfo) {
    LOG(("nsHttpChannel::ProcessHSTSHeader: no securityInfo?"));
    return NS_ERROR_INVALID_ARG;
  }
  nsITransportSecurityInfo::OverridableErrorCategory overridableErrorCategory;
  rv = aSecInfo->GetOverridableErrorCategory(&overridableErrorCategory);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (overridableErrorCategory !=
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
    LOG(
        ("nsHttpChannel::ProcessHSTSHeader: untrustworthy connection - not "
         "processing header"));
    return NS_ERROR_FAILURE;
  }

  nsISiteSecurityService* sss = gHttpHandler->GetSSService();
  NS_ENSURE_TRUE(sss, NS_ERROR_OUT_OF_MEMORY);

  OriginAttributes originAttributes;
  if (NS_WARN_IF(!StoragePrincipalHelper::GetOriginAttributesForHSTS(
          this, originAttributes))) {
    return NS_ERROR_FAILURE;
  }

  uint32_t failureResult;
  rv = sss->ProcessHeader(mURI, securityHeader, originAttributes, nullptr,
                          nullptr, &failureResult);
  if (NS_FAILED(rv)) {
    nsAutoString consoleErrorCategory(u"Invalid HSTS Headers"_ns);
    nsAutoString consoleErrorTag;
    GetSTSConsoleErrorTag(failureResult, consoleErrorTag);
    Unused << AddSecurityMessage(consoleErrorTag, consoleErrorCategory);
    LOG(("nsHttpChannel: Failed to parse %s header, continuing load.\n",
         atom.get()));
  }
  return NS_OK;
}

/**
 * Decide whether or not to remember Strict-Transport-Security, and whether
 * or not to enforce channel integrity.
 *
 * @return NS_ERROR_FAILURE if there's security information missing even though
 *             it's an HTTPS connection.
 */
nsresult nsHttpChannel::ProcessSecurityHeaders() {
  // If this channel is not loading securely, STS or PKP doesn't do anything.
  // In the case of HSTS, the upgrade to HTTPS takes place earlier in the
  // channel load process.
  if (!mURI->SchemeIs("https")) {
    return NS_OK;
  }

  if (IsBrowsingContextDiscarded()) {
    return NS_OK;
  }

  nsAutoCString asciiHost;
  nsresult rv = mURI->GetAsciiHost(asciiHost);
  NS_ENSURE_SUCCESS(rv, NS_OK);

  // If the channel is not a hostname, but rather an IP, do not process STS
  // or PKP headers
  if (HostIsIPLiteral(asciiHost)) {
    return NS_OK;
  }

  // mSecurityInfo may not always be present, and if it's not then it is okay
  // to just disregard any security headers since we know nothing about the
  // security of the connection.
  NS_ENSURE_TRUE(mSecurityInfo, NS_OK);

  // Only process HSTS headers for first-party loads. This prevents a
  // proliferation of useless HSTS state for partitioned third parties.
  if (!mLoadInfo->GetIsThirdPartyContextToTopWindow()) {
    rv = ProcessHSTSHeader(mSecurityInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

bool nsHttpChannel::IsHTTPS() { return mURI->SchemeIs("https"); }

void nsHttpChannel::ProcessSSLInformation() {
  // If this is HTTPS, record any use of RSA so that Key Exchange Algorithm
  // can be whitelisted for TLS False Start in future sessions. We could
  // do the same for DH but its rarity doesn't justify the lookup.

  if (mCanceled || NS_FAILED(mStatus) || !mSecurityInfo || !IsHTTPS() ||
      mPrivateBrowsing) {
    return;
  }

  if (!mSecurityInfo) {
    return;
  }

  uint32_t state;
  if (NS_SUCCEEDED(mSecurityInfo->GetSecurityState(&state)) &&
      (state & nsIWebProgressListener::STATE_IS_BROKEN)) {
    // Send weak crypto warnings to the web console
    if (state & nsIWebProgressListener::STATE_USES_WEAK_CRYPTO) {
      nsString consoleErrorTag = u"WeakCipherSuiteWarning"_ns;
      nsString consoleErrorCategory = u"SSL"_ns;
      Unused << AddSecurityMessage(consoleErrorTag, consoleErrorCategory);
    }
  }

  uint16_t tlsVersion;
  nsresult rv = mSecurityInfo->GetProtocolVersion(&tlsVersion);
  if (NS_SUCCEEDED(rv) &&
      tlsVersion != nsITransportSecurityInfo::TLS_VERSION_1_2 &&
      tlsVersion != nsITransportSecurityInfo::TLS_VERSION_1_3) {
    nsString consoleErrorTag = u"DeprecatedTLSVersion2"_ns;
    nsString consoleErrorCategory = u"TLS"_ns;
    Unused << AddSecurityMessage(consoleErrorTag, consoleErrorCategory);
  }
}

void nsHttpChannel::ProcessAltService(nsHttpConnectionInfo* aTransConnInfo) {
  // e.g. Alt-Svc: h2=":443"; ma=60
  // e.g. Alt-Svc: h2="otherhost:443"
  // Alt-Svc       = 1#( alternative *( OWS ";" OWS parameter ) )
  // alternative   = protocol-id "=" alt-authority
  // protocol-id   = token ; percent-encoded ALPN protocol identifier
  // alt-authority = quoted-string ;  containing [ uri-host ] ":" port

  if (!LoadAllowAltSvc()) {  // per channel opt out
    return;
  }

  if (mWebTransportSessionEventListener) {
    return;
  }

  if (!gHttpHandler->AllowAltSvc() || (mCaps & NS_HTTP_DISALLOW_SPDY)) {
    return;
  }

  if (IsBrowsingContextDiscarded()) {
    return;
  }

  nsAutoCString scheme;
  mURI->GetScheme(scheme);
  bool isHttp = scheme.EqualsLiteral("http");
  if (!isHttp && !scheme.EqualsLiteral("https")) {
    return;
  }

  nsAutoCString altSvc;
  Unused << mResponseHead->GetHeader(nsHttp::Alternate_Service, altSvc);
  if (altSvc.IsEmpty()) {
    return;
  }

  if (!nsHttp::IsReasonableHeaderValue(altSvc)) {
    LOG(("Alt-Svc Response Header seems unreasonable - skipping\n"));
    return;
  }

  nsAutoCString originHost;
  int32_t originPort = 80;
  mURI->GetPort(&originPort);
  if (NS_FAILED(mURI->GetAsciiHost(originHost))) {
    return;
  }

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  nsCOMPtr<nsProxyInfo> proxyInfo;
  NS_NewNotificationCallbacksAggregation(mCallbacks, mLoadGroup,
                                         getter_AddRefs(callbacks));

  if (mProxyInfo) {
    proxyInfo = do_QueryInterface(mProxyInfo);
  }

  OriginAttributes originAttributes;
  // Regular principal in case we have a proxy.
  if (proxyInfo &&
      !StaticPrefs::privacy_partition_network_state_connection_with_proxy()) {
    StoragePrincipalHelper::GetOriginAttributes(
        this, originAttributes, StoragePrincipalHelper::eRegularPrincipal);
  } else {
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(
        this, originAttributes);
  }

  AltSvcMapping::ProcessHeader(altSvc, scheme, originHost, originPort,
                               mUsername, mPrivateBrowsing, callbacks,
                               proxyInfo, mCaps & NS_HTTP_DISALLOW_SPDY,
                               originAttributes, aTransConnInfo);
}

nsresult nsHttpChannel::ProcessResponse(nsHttpConnectionInfo* aConnInfo) {
  uint32_t httpStatus = mResponseHead->Status();

  LOG(("nsHttpChannel::ProcessResponse [this=%p httpStatus=%u]\n", this,
       httpStatus));

  // Gather data on whether the transaction and page (if this is
  // the initial page load) is being loaded with SSL.
  glean::http::transaction_is_ssl
      .EnumGet(static_cast<glean::http::TransactionIsSslLabel>(
          mConnectionInfo->EndToEndSSL()))
      .Add();
  if (mLoadFlags & LOAD_INITIAL_DOCUMENT_URI) {
    glean::http::pageload_is_ssl
        .EnumGet(static_cast<glean::http::PageloadIsSslLabel>(
            mConnectionInfo->EndToEndSSL()))
        .Add();
  }

  if (Telemetry::CanRecordPrereleaseData()) {
    // how often do we see something like Alt-Svc: "443:quic,p=1"
    // and Alt-Svc: "h3-****"
    nsAutoCString alt_service;
    Unused << mResponseHead->GetHeader(nsHttp::Alternate_Service, alt_service);
    uint32_t saw_quic = 0;
    if (!alt_service.IsEmpty()) {
      if (strstr(alt_service.get(), "h3-")) {
        saw_quic = 1;
      } else if (strstr(alt_service.get(), "quic")) {
        saw_quic = 2;
      }
    }
    glean::http::saw_quic_alt_protocol.AccumulateSingleSample(saw_quic);

    // Gather data on various response status to monitor any increased frequency
    // of auth failures due to Bug 1896350
    switch (httpStatus) {
      case 200:
        mozilla::glean::networking::http_response_status_code.Get("200_ok"_ns)
            .Add(1);
        break;
      case 301:
        mozilla::glean::networking::http_response_status_code
            .Get("301_moved_permanently"_ns)
            .Add(1);
        break;
      case 302:
        mozilla::glean::networking::http_response_status_code
            .Get("302_found"_ns)
            .Add(1);
        break;
      case 304:
        mozilla::glean::networking::http_response_status_code
            .Get("304_not_modified"_ns)
            .Add(1);
        break;
      case 307:
        mozilla::glean::networking::http_response_status_code
            .Get("307_temporary_redirect"_ns)
            .Add(1);
        break;
      case 308:
        mozilla::glean::networking::http_response_status_code
            .Get("308_permanent_redirect"_ns)
            .Add(1);
        break;
      case 400:
        mozilla::glean::networking::http_response_status_code
            .Get("400_bad_request"_ns)
            .Add(1);
        break;
      case 401:
        mozilla::glean::networking::http_response_status_code
            .Get("401_unauthorized"_ns)
            .Add(1);
        break;
      case 403:
        mozilla::glean::networking::http_response_status_code
            .Get("403_forbidden"_ns)
            .Add(1);
        break;
      case 404:
        mozilla::glean::networking::http_response_status_code
            .Get("404_not_found"_ns)
            .Add(1);
        break;
      case 421:
        mozilla::glean::networking::http_response_status_code
            .Get("421_misdirected_request"_ns)
            .Add(1);
        break;
      case 425:
        mozilla::glean::networking::http_response_status_code
            .Get("425_too_early"_ns)
            .Add(1);
        break;
      case 429:
        mozilla::glean::networking::http_response_status_code
            .Get("429_too_many_requests"_ns)
            .Add(1);
        break;
      case 500:
        mozilla::glean::networking::http_response_status_code
            .Get("other_5xx"_ns)
            .Add(1);
        break;
      default:
        if (httpStatus >= 400 && httpStatus < 500) {
          mozilla::glean::networking::http_response_status_code
              .Get("other_4xx"_ns)
              .Add(1);
        } else if (httpStatus > 500) {
          mozilla::glean::networking::http_response_status_code
              .Get("other_5xx"_ns)
              .Add(1);
        } else {
          mozilla::glean::networking::http_response_status_code.Get("other"_ns)
              .Add(1);
        }
        break;
    }
  }

  // Let the predictor know whether this was a cacheable response or not so
  // that it knows whether or not to possibly prefetch this resource in the
  // future.
  // We use GetReferringPage because mReferrerInfo may not be set at all(this is
  // especially useful in xpcshell tests, where we don't have an actual pageload
  // to get a referrer from).
  nsCOMPtr<nsIURI> referrer = GetReferringPage();
  if (!referrer && mReferrerInfo) {
    referrer = mReferrerInfo->GetOriginalReferrer();
  }

  if (referrer) {
    nsCOMPtr<nsILoadContextInfo> lci = GetLoadContextInfo(this);
    mozilla::net::Predictor::UpdateCacheability(
        referrer, mURI, httpStatus, mRequestHead, mResponseHead.get(), lci,
        IsThirdPartyTrackingResource());
  }

  // Only allow 407 (authentication required) to continue
  if (mTransaction && mTransaction->ProxyConnectFailed() && httpStatus != 407) {
    return ProcessFailedProxyConnect(httpStatus);
  }

  MOZ_ASSERT(!CachedContentIsValid() || mRaceCacheWithNetwork,
             "We should not be hitting the network if we have valid cached "
             "content unless we are racing the network and cache");

  ProcessSSLInformation();

  // notify "http-on-examine-response" observers
  gHttpHandler->OnExamineResponse(this);

  return ContinueProcessResponse1(aConnInfo);
}

void nsHttpChannel::AsyncContinueProcessResponse(
    nsHttpConnectionInfo* aConnInfo) {
  nsresult rv;
  rv = ContinueProcessResponse1(aConnInfo);
  if (NS_FAILED(rv)) {
    // A synchronous failure here would normally be passed as the return
    // value from OnStartRequest, which would in turn cancel the request.
    // If we're continuing asynchronously, we need to cancel the request
    // ourselves.
    Unused << Cancel(rv);
  }
}

nsresult nsHttpChannel::ContinueProcessResponse1(
    nsHttpConnectionInfo* aConnInfo) {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");
  nsresult rv = NS_OK;

  if (mSuspendCount) {
    LOG(("Waiting until resume to finish processing response [this=%p]\n",
         this));
    mCallOnResume = [connInfo = RefPtr{aConnInfo}](nsHttpChannel* self) {
      self->AsyncContinueProcessResponse(connInfo);
      return NS_OK;
    };
    return NS_OK;
  }

  // Check if request was cancelled during http-on-examine-response.
  if (mCanceled) {
    return CallOnStartRequest();
  }

  uint32_t httpStatus = mResponseHead->Status();

  // STS, Cookies and Alt-Service should not be handled on proxy failure.
  // If proxy CONNECT response needs to complete, wait to process connection
  // for Strict-Transport-Security.
  if (!(mTransaction && mTransaction->ProxyConnectFailed()) &&
      (httpStatus != 407)) {
    CookieVisitor cookieVisitor(mResponseHead.get());
    SetCookieHeaders(cookieVisitor.CookieHeaders());
    nsCOMPtr<nsIParentChannel> parentChannel;
    NS_QueryNotificationCallbacks(this, parentChannel);
    if (RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel)) {
      httpParent->SetCookieHeaders(cookieVisitor.CookieHeaders());
    }

    // Given a successful connection, process any STS or PKP data that's
    // relevant.
    nsresult rv = ProcessSecurityHeaders();
    if (NS_FAILED(rv)) {
      NS_WARNING("ProcessSTSHeader failed, continuing load.");
    }

    if ((httpStatus < 500) && (httpStatus != 421)) {
      ProcessAltService(aConnInfo);
    }
  }

  if (LoadConcurrentCacheAccess() && LoadCachedContentIsPartial() &&
      httpStatus != 206) {
    LOG(
        ("  only expecting 206 when doing partial request during "
         "interrupted cache concurrent read"));
    return NS_ERROR_CORRUPTED_CONTENT;
  }

  // handle unused username and password in url (see bug 232567)
  if (httpStatus != 401 && httpStatus != 407) {
    if (!mAuthRetryPending) {
      MOZ_DIAGNOSTIC_ASSERT(mAuthProvider);
      rv = mAuthProvider ? mAuthProvider->CheckForSuperfluousAuth()
                         : NS_ERROR_UNEXPECTED;
      if (NS_FAILED(rv)) {
        mStatus = rv;
        LOG(("  CheckForSuperfluousAuth failed (%08x)",
             static_cast<uint32_t>(rv)));
      }
    }
    if (mCanceled) return CallOnStartRequest();

    // reset the authentication's current continuation state because ourvr
    // last authentication attempt has been completed successfully
    MOZ_DIAGNOSTIC_ASSERT(mAuthProvider);
    rv = mAuthProvider ? mAuthProvider->Disconnect(NS_ERROR_ABORT)
                       : NS_ERROR_UNEXPECTED;
    if (NS_FAILED(rv)) {
      LOG(("  Disconnect failed (%08x)", static_cast<uint32_t>(rv)));
    }
    mAuthProvider = nullptr;
    LOG(("  continuation state has been reset"));
  }

  gHttpHandler->OnAfterExamineResponse(this);

  // No process switch needed, continue as normal.
  return ContinueProcessResponse2(rv);
}

nsresult nsHttpChannel::ContinueProcessResponse2(nsresult rv) {
  if (mSuspendCount) {
    LOG(("Waiting until resume to finish processing response [this=%p]\n",
         this));
    mCallOnResume = [rv](nsHttpChannel* self) {
      Unused << self->ContinueProcessResponse2(rv);
      return NS_OK;
    };
    return NS_OK;
  }

  if (NS_FAILED(rv) && !mCanceled) {
    // The process switch failed, cancel this channel.
    Cancel(rv);
    return CallOnStartRequest();
  }

  if (mAPIRedirectTo && !mCanceled) {
    MOZ_ASSERT(!LoadOnStartRequestCalled());

    PushRedirectAsyncFunc(&nsHttpChannel::ContinueProcessResponse3);
    rv = StartRedirectChannelToURI(
        mAPIRedirectTo->first(),
        mAPIRedirectTo->second() ? nsIChannelEventSink::REDIRECT_TEMPORARY |
                                       nsIChannelEventSink::REDIRECT_TRANSPARENT
                                 : nsIChannelEventSink::REDIRECT_TEMPORARY);
    mAPIRedirectTo = Nothing();
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }
    PopRedirectAsyncFunc(&nsHttpChannel::ContinueProcessResponse3);
  }

  // Hack: ContinueProcessResponse3 uses NS_OK to detect successful
  // redirects, so we distinguish this codepath (a non-redirect that's
  // processing normally) by passing in a bogus error code.
  return ContinueProcessResponse3(NS_BINDING_FAILED);
}

nsresult nsHttpChannel::ContinueProcessResponse3(nsresult rv) {
  LOG(("nsHttpChannel::ContinueProcessResponse3 [this=%p, rv=%" PRIx32 "]",
       this, static_cast<uint32_t>(rv)));

  if (NS_SUCCEEDED(rv)) {
    // redirectTo() has passed through, we don't want to go on with
    // this channel.  It will now be canceled by the redirect handling
    // code that called this function.
    return NS_OK;
  }

  rv = NS_OK;

  uint32_t httpStatus = mResponseHead->Status();
  bool transactionRestarted = mTransaction->TakeRestartedState();

  // handle different server response categories.  Note that we handle
  // caching or not caching of error pages in
  // nsHttpResponseHead::MustValidate; if you change this switch, update that
  // one
  switch (httpStatus) {
    case 200:
    case 203:
      // Per RFC 2616, 14.35.2, "A server MAY ignore the Range header".
      // So if a server does that and sends 200 instead of 206 that we
      // expect, notify our caller.
      // However, if we wanted to start from the beginning, let it go through
      if (LoadResuming() && mStartPos != 0) {
        LOG(("Server ignored our Range header, cancelling [this=%p]\n", this));
        Cancel(NS_ERROR_NOT_RESUMABLE);
        rv = CallOnStartRequest();
        break;
      }
      // these can normally be cached
      rv = ProcessNormal();
      MaybeInvalidateCacheEntryForSubsequentGet();
      break;
    case 206:
      if (LoadCachedContentIsPartial()) {  // an internal byte range request...
        auto func = [](auto* self, nsresult aRv) {
          return self->ContinueProcessResponseAfterPartialContent(aRv);
        };
        rv = ProcessPartialContent(func);
        // Directly call ContinueProcessResponseAfterPartialContent if channel
        // is not suspended or ProcessPartialContent throws.
        if (!mSuspendCount || NS_FAILED(rv)) {
          return ContinueProcessResponseAfterPartialContent(rv);
        }
        return NS_OK;
      } else {
        mCacheInputStream.CloseAndRelease();
        rv = ProcessNormal();
      }
      break;
    case 301:
    case 302:
    case 307:
    case 308:
    case 303:
#if 0
    case 305: // disabled as a security measure (see bug 187996).
#endif
      // don't store the response body for redirects
      MaybeInvalidateCacheEntryForSubsequentGet();
      PushRedirectAsyncFunc(&nsHttpChannel::ContinueProcessResponse4);
      rv = AsyncProcessRedirection(httpStatus);
      if (NS_FAILED(rv)) {
        PopRedirectAsyncFunc(&nsHttpChannel::ContinueProcessResponse4);
        LOG(("AsyncProcessRedirection failed [rv=%" PRIx32 "]\n",
             static_cast<uint32_t>(rv)));
        // don't cache failed redirect responses.
        if (mCacheEntry) mCacheEntry->AsyncDoom(nullptr);
        if (DoNotRender3xxBody(rv)) {
          mStatus = rv;
          DoNotifyListener();
        } else {
          rv = ContinueProcessResponse4(rv);
        }
      }
      break;
    case 304:
      if (!ShouldBypassProcessNotModified()) {
        auto func = [](auto* self, nsresult aRv) {
          return self->ContinueProcessResponseAfterNotModified(aRv);
        };
        rv = ProcessNotModified(func);
        // Directly call ContinueProcessResponseAfterNotModified if channel
        // is not suspended or ProcessNotModified throws.
        if (!mSuspendCount || NS_FAILED(rv)) {
          return ContinueProcessResponseAfterNotModified(rv);
        }
        return NS_OK;
      }

      // Don't cache uninformative 304
      if (LoadCustomConditionalRequest()) {
        CloseCacheEntry(false);
      }

      if (ShouldBypassProcessNotModified() || NS_FAILED(rv)) {
        rv = ProcessNormal();
      }
      break;
    case 401:
    case 407:
      if (MOZ_UNLIKELY(httpStatus == 407 && transactionRestarted)) {
        // The transaction has been internally restarted.  We want to
        // authenticate to the proxy again, so reuse either cached credentials
        // or use default credentials for NTLM/Negotiate.  This prevents
        // considering the previously used credentials as invalid.
        MOZ_DIAGNOSTIC_ASSERT(mAuthProvider);
        if (!mAuthProvider) {
          mStatus = NS_ERROR_UNEXPECTED;
          return ProcessNormal();
        }
        mAuthProvider->ClearProxyIdent();
      }
      if (!LoadAuthRedirectedChannel() &&
          MOZ_UNLIKELY(LoadCustomAuthHeader()) && httpStatus == 401) {
        // When a custom auth header fails, we don't want to try
        // any cached credentials, nor we want to ask the user.
        // It's up to the consumer to re-try w/o setting a custom
        // auth header if cached credentials should be attempted.
        rv = NS_ERROR_FAILURE;
      } else if (httpStatus == 401 &&
                 StaticPrefs::
                     network_auth_supress_auth_prompt_for_XFO_failures() &&
                 !nsContentSecurityUtils::CheckCSPFrameAncestorAndXFO(this)) {
        // CSP Frame Ancestor and X-Frame-Options check has failed
        // Do not prompt http auth - Bug 1629307
        rv = NS_ERROR_FAILURE;
      } else {
        MOZ_DIAGNOSTIC_ASSERT(mAuthProvider);
        rv = mAuthProvider
                 ? mAuthProvider->ProcessAuthentication(
                       httpStatus, mConnectionInfo->EndToEndSSL() &&
                                       mTransaction &&
                                       mTransaction->ProxyConnectFailed())
                 : NS_ERROR_UNEXPECTED;
      }
      if (rv == NS_ERROR_IN_PROGRESS) {
        // authentication prompt has been invoked and result
        // is expected asynchronously
        mIsAuthChannel = true;
        mAuthRetryPending = true;
        if (httpStatus == 407 ||
            (mTransaction && mTransaction->ProxyConnectFailed())) {
          StoreProxyAuthPending(true);
        }

        // suspend the transaction pump to stop receiving the
        // unauthenticated content data. We will throw that data
        // away when user provides credentials or resume the pump
        // when user refuses to authenticate.
        LOG(
            ("Suspending the transaction, asynchronously prompting for "
             "credentials"));
        mTransactionPump->Suspend();

#ifdef DEBUG
        // This is for test purposes only. See bug 1683176 for details.
        gHttpHandler->OnTransactionSuspendedDueToAuthentication(this);
#endif
        rv = NS_OK;
      } else if (NS_FAILED(rv)) {
        LOG(("ProcessAuthentication failed [rv=%" PRIx32 "]\n",
             static_cast<uint32_t>(rv)));
        if (mTransaction && mTransaction->ProxyConnectFailed()) {
          return ProcessFailedProxyConnect(httpStatus);
        }
        if (rv == NS_ERROR_BASIC_HTTP_AUTH_DISABLED) {
          mStatus = rv;
        } else if (!mAuthRetryPending) {
          MOZ_DIAGNOSTIC_ASSERT(mAuthProvider);
          rv = mAuthProvider ? mAuthProvider->CheckForSuperfluousAuth()
                             : NS_ERROR_UNEXPECTED;
          if (NS_FAILED(rv)) {
            mStatus = rv;
            LOG(("CheckForSuperfluousAuth failed [rv=%x]\n",
                 static_cast<uint32_t>(rv)));
          }
        }
        rv = ProcessNormal();
      } else {
        mIsAuthChannel = true;
        mAuthRetryPending = true;
        if (StaticPrefs::network_auth_use_redirect_for_retries()) {
          if (NS_SUCCEEDED(RedirectToNewChannelForAuthRetry())) {
            return NS_OK;
          }
          mAuthRetryPending = false;
          rv = ProcessNormal();
        }
      }
      break;

    case 408:
    case 425:
    case 429:
      // Do not cache 408, 425 and 429.
      CloseCacheEntry(false);
      [[fallthrough]];  // process normally
    default:
      rv = ProcessNormal();
      MaybeInvalidateCacheEntryForSubsequentGet();
      break;
  }

  UpdateCacheDisposition(false, false);
  return rv;
}

nsresult nsHttpChannel::ContinueProcessResponseAfterPartialContent(
    nsresult aRv) {
  LOG(
      ("nsHttpChannel::ContinueProcessResponseAfterPartialContent "
       "[this=%p, rv=%" PRIx32 "]",
       this, static_cast<uint32_t>(aRv)));

  UpdateCacheDisposition(false, NS_SUCCEEDED(aRv));
  return aRv;
}

nsresult nsHttpChannel::ContinueProcessResponseAfterNotModified(nsresult aRv) {
  LOG(
      ("nsHttpChannel::ContinueProcessResponseAfterNotModified "
       "[this=%p, rv=%" PRIx32 "]",
       this, static_cast<uint32_t>(aRv)));

  if (NS_SUCCEEDED(aRv)) {
    StoreTransactionReplaced(true);
    UpdateCacheDisposition(true, false);
    return NS_OK;
  }

  LOG(("ProcessNotModified failed [rv=%" PRIx32 "]\n",
       static_cast<uint32_t>(aRv)));

  // We cannot read from the cache entry, it might be in an
  // incosistent state.  Doom it and redirect the channel
  // to the same URI to reload from the network.
  mCacheInputStream.CloseAndRelease();
  if (mCacheEntry) {
    mCacheEntry->AsyncDoom(nullptr);
    mCacheEntry = nullptr;
  }

  nsresult rv =
      StartRedirectChannelToURI(mURI, nsIChannelEventSink::REDIRECT_INTERNAL);
  if (NS_SUCCEEDED(rv)) {
    return NS_OK;
  }

  // Don't cache uninformative 304
  if (LoadCustomConditionalRequest()) {
    CloseCacheEntry(false);
  }

  if (ShouldBypassProcessNotModified() || NS_FAILED(rv)) {
    rv = ProcessNormal();
  }

  UpdateCacheDisposition(false, false);
  return rv;
}

static void ReportHttpResponseVersion(HttpVersion version) {
  if (Telemetry::CanRecordPrereleaseData()) {
    glean::http::response_version.AccumulateSingleSample(
        static_cast<uint32_t>(version));
  }
  mozilla::glean::networking::http_response_version
      .Get(HttpVersionToTelemetryLabel(version))
      .Add(1);
}

void nsHttpChannel::UpdateCacheDisposition(bool aSuccessfulReval,
                                           bool aPartialContentUsed) {
  if (mRaceDelay && !mRaceCacheWithNetwork &&
      (LoadCachedContentIsPartial() || mDidReval)) {
    if (aSuccessfulReval || aPartialContentUsed) {
      glean::network::race_cache_validation
          .EnumGet(glean::network::RaceCacheValidationLabel::eCachedcontentused)
          .Add();
    } else {
      glean::network::race_cache_validation
          .EnumGet(
              glean::network::RaceCacheValidationLabel::eCachedcontentnotused)
          .Add();
    }
  }

  PROFILER_MARKER_TEXT(
      "CacheDisposition", NETWORK, {},
      nsPrintfCString(
          !mDidReval ? "Missed"
                     : (aSuccessfulReval ? "HitViaReval" : "MissedViaReval")));
  if (Telemetry::CanRecordPrereleaseData()) {
    CacheDisposition cacheDisposition;
    if (!mDidReval) {
      cacheDisposition = kCacheMissed;
    } else if (aSuccessfulReval) {
      cacheDisposition = kCacheHitViaReval;
    } else {
      cacheDisposition = kCacheMissedViaReval;
    }
    AccumulateCacheHitTelemetry(cacheDisposition, this);
    mCacheDisposition = cacheDisposition;
  }

  ReportHttpResponseVersion(mResponseHead->Version());
}

nsresult nsHttpChannel::ContinueProcessResponse4(nsresult rv) {
  bool doNotRender = DoNotRender3xxBody(rv);

  if (rv == NS_ERROR_DOM_BAD_URI && mRedirectURI &&
      !net::SchemeIsHttpOrHttps(mRedirectURI)) {
    // This was a blocked attempt to redirect and subvert the system by
    // redirecting to another protocol (perhaps javascript:)
    // In that case we want to throw an error instead of displaying the
    // non-redirected response body.
    LOG(("ContinueProcessResponse4 detected rejected Non-HTTP Redirection"));
    doNotRender = true;
    rv = NS_ERROR_CORRUPTED_CONTENT;
  }

  if (doNotRender) {
    Cancel(rv);
    DoNotifyListener();
    return rv;
  }

  if (NS_SUCCEEDED(rv)) {
    UpdateInhibitPersistentCachingFlag();

    MaybeCreateCacheEntryWhenRCWN();

    rv = InitCacheEntry();
    if (NS_FAILED(rv)) {
      LOG(
          ("ContinueProcessResponse4 "
           "failed to init cache entry [rv=%x]\n",
           static_cast<uint32_t>(rv)));
    }
    CloseCacheEntry(false);
    return NS_OK;
  }

  LOG(("ContinueProcessResponse4 got failure result [rv=%" PRIx32 "]\n",
       static_cast<uint32_t>(rv)));
  if (mTransaction && mTransaction->ProxyConnectFailed()) {
    return ProcessFailedProxyConnect(mRedirectType);
  }
  return ProcessNormal();
}

nsresult nsHttpChannel::ProcessNormal() {
  LOG(("nsHttpChannel::ProcessNormal [this=%p]\n", this));

  return ContinueProcessNormal(NS_OK);
}

nsresult nsHttpChannel::ContinueProcessNormal(nsresult rv) {
  LOG(("nsHttpChannel::ContinueProcessNormal [this=%p]", this));

  if (NS_FAILED(rv)) {
    // Fill the failure status here, we have failed to fall back, thus we
    // have to report our status as failed.
    mStatus = rv;
    DoNotifyListener();
    return rv;
  }

  rv = ProcessCrossOriginSecurityHeaders();
  if (NS_FAILED(rv)) {
    mStatus = rv;
    HandleAsyncAbort();
    return rv;
  }

  // if we're here, then any byte-range requests failed to result in a partial
  // response.  we must clear this flag to prevent BufferPartialContent from
  // being called inside our OnDataAvailable (see bug 136678).
  StoreCachedContentIsPartial(false);

  UpdateInhibitPersistentCachingFlag();

  MaybeCreateCacheEntryWhenRCWN();

  // this must be called before firing OnStartRequest, since http clients,
  // such as imagelib, expect our cache entry to already have the correct
  // expiration time (bug 87710).
  if (mCacheEntry) {
    rv = InitCacheEntry();
    if (NS_FAILED(rv)) CloseCacheEntry(true);
  }

  // Check that the server sent us what we were asking for
  if (LoadResuming()) {
    // Create an entity id from the response
    nsAutoCString id;
    rv = GetEntityID(id);
    if (NS_FAILED(rv)) {
      // If creating an entity id is not possible -> error
      Cancel(NS_ERROR_NOT_RESUMABLE);
    } else if (mResponseHead->Status() != 206 &&
               mResponseHead->Status() != 200) {
      // Probably 404 Not Found, 412 Precondition Failed or
      // 416 Invalid Range -> error
      LOG(("Unexpected response status while resuming, aborting [this=%p]\n",
           this));
      Cancel(NS_ERROR_ENTITY_CHANGED);
    }
    // If we were passed an entity id, verify it's equal to the server's
    else if (!mEntityID.IsEmpty()) {
      if (!mEntityID.Equals(id)) {
        LOG(("Entity mismatch, expected '%s', got '%s', aborting [this=%p]",
             mEntityID.get(), id.get(), this));
        Cancel(NS_ERROR_ENTITY_CHANGED);
      }
    }
  }

  rv = CallOnStartRequest();
  if (NS_FAILED(rv)) return rv;

  // install cache listener if we still have a cache entry open
  if (mCacheEntry && !LoadCacheEntryIsReadOnly()) {
    rv = InstallCacheListener();
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

nsresult nsHttpChannel::PromptTempRedirect() {
  if (!gHttpHandler->PromptTempRedirect()) {
    return NS_OK;
  }
  nsresult rv;
  nsCOMPtr<nsIStringBundleService> bundleService;
  bundleService = mozilla::components::StringBundle::Service(&rv);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIStringBundle> stringBundle;
  rv =
      bundleService->CreateBundle(NECKO_MSGS_URL, getter_AddRefs(stringBundle));
  if (NS_FAILED(rv)) return rv;

  nsAutoString messageString;
  rv = stringBundle->GetStringFromName("RepostFormData", messageString);
  if (NS_SUCCEEDED(rv)) {
    bool repost = false;

    nsCOMPtr<nsIPrompt> prompt;
    GetCallback(prompt);
    if (!prompt) return NS_ERROR_NO_INTERFACE;

    prompt->Confirm(nullptr, messageString.get(), &repost);
    if (!repost) return NS_ERROR_FAILURE;
  }

  return rv;
}

nsresult nsHttpChannel::ProxyFailover() {
  LOG(("nsHttpChannel::ProxyFailover [this=%p]\n", this));

  nsresult rv;

  nsCOMPtr<nsIProtocolProxyService> pps;
  pps = mozilla::components::ProtocolProxy::Service(&rv);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIProxyInfo> pi;
  rv = pps->GetFailoverForProxy(mConnectionInfo->ProxyInfo(), mURI, mStatus,
                                getter_AddRefs(pi));
#ifdef MOZ_PROXY_DIRECT_FAILOVER
  if (NS_FAILED(rv)) {
    if (!StaticPrefs::network_proxy_failover_direct()) {
      return rv;
    }
    // If this request used a failed proxy and there is no failover available,
    // fallback to DIRECT connections for conservative requests.
    if (LoadBeConservative()) {
      rv = pps->NewProxyInfo("direct"_ns, ""_ns, 0, ""_ns, ""_ns, 0, UINT32_MAX,
                             nullptr, getter_AddRefs(pi));
    }
#endif
    if (NS_FAILED(rv)) {
      return rv;
    }
#ifdef MOZ_PROXY_DIRECT_FAILOVER
  }
#endif

  // XXXbz so where does this codepath remove us from the loadgroup,
  // exactly?
  return AsyncDoReplaceWithProxy(pi);
}

void nsHttpChannel::SetHTTPSSVCRecord(
    already_AddRefed<nsIDNSHTTPSSVCRecord>&& aRecord) {
  LOG(("nsHttpChannel::SetHTTPSSVCRecord [this=%p]\n", this));
  nsCOMPtr<nsIDNSHTTPSSVCRecord> record = aRecord;
  MOZ_ASSERT(!mHTTPSSVCRecord);
  mHTTPSSVCRecord.emplace(std::move(record));
}

void nsHttpChannel::HandleAsyncRedirectChannelToHttps() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(("Waiting until resume to do async redirect to https [this=%p]\n",
         this));
    mCallOnResume = [](nsHttpChannel* self) {
      self->HandleAsyncRedirectChannelToHttps();
      return NS_OK;
    };
    return;
  }

  nsresult rv = StartRedirectChannelToHttps();
  if (NS_FAILED(rv)) {
    rv = ContinueAsyncRedirectChannelToURI(rv);
    if (NS_FAILED(rv)) {
      LOG(("ContinueAsyncRedirectChannelToURI failed (%08x) [this=%p]\n",
           static_cast<uint32_t>(rv), this));
    }
  }
}

nsresult nsHttpChannel::StartRedirectChannelToHttps() {
  LOG(("nsHttpChannel::HandleAsyncRedirectChannelToHttps() [STS]\n"));

  nsCOMPtr<nsIURI> upgradedURI;
  nsresult rv = NS_GetSecureUpgradedURI(mURI, getter_AddRefs(upgradedURI));
  NS_ENSURE_SUCCESS(rv, rv);

  return StartRedirectChannelToURI(
      upgradedURI, nsIChannelEventSink::REDIRECT_PERMANENT |
                       nsIChannelEventSink::REDIRECT_STS_UPGRADE);
}

void nsHttpChannel::HandleAsyncAPIRedirect() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");
  MOZ_ASSERT(mAPIRedirectTo, "How did that happen?");
  MOZ_ASSERT(mAPIRedirectTo->first(), "How did that happen?");

  if (mSuspendCount) {
    LOG(("Waiting until resume to do async API redirect [this=%p]\n", this));
    mCallOnResume = [](nsHttpChannel* self) {
      self->HandleAsyncAPIRedirect();
      return NS_OK;
    };
    return;
  }

  nsresult rv = StartRedirectChannelToURI(
      mAPIRedirectTo->first(),
      mAPIRedirectTo->second() ? nsIChannelEventSink::REDIRECT_PERMANENT |
                                     nsIChannelEventSink::REDIRECT_TRANSPARENT
                               : nsIChannelEventSink::REDIRECT_PERMANENT);
  if (NS_FAILED(rv)) {
    rv = ContinueAsyncRedirectChannelToURI(rv);
    if (NS_FAILED(rv)) {
      LOG(("ContinueAsyncRedirectChannelToURI failed (%08x) [this=%p]\n",
           static_cast<uint32_t>(rv), this));
    }
  }
}

void nsHttpChannel::HandleAsyncRedirectToUnstrippedURI() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(
        ("Waiting until resume to do async redirect to unstripped URI "
         "[this=%p]\n",
         this));
    mCallOnResume = [](nsHttpChannel* self) {
      self->HandleAsyncRedirectToUnstrippedURI();
      return NS_OK;
    };
    return;
  }

  nsCOMPtr<nsIURI> unstrippedURI;
  mLoadInfo->GetUnstrippedURI(getter_AddRefs(unstrippedURI));

  // Clear the unstripped URI from the loadInfo before starting redirect in case
  // endless redirect.
  mLoadInfo->SetUnstrippedURI(nullptr);

  nsresult rv = StartRedirectChannelToURI(
      unstrippedURI, nsIChannelEventSink::REDIRECT_PERMANENT);

  if (NS_FAILED(rv)) {
    rv = ContinueAsyncRedirectChannelToURI(rv);
    if (NS_FAILED(rv)) {
      LOG(("ContinueAsyncRedirectChannelToURI failed (%08x) [this=%p]\n",
           static_cast<uint32_t>(rv), this));
    }
  }
}
nsresult nsHttpChannel::RedirectToNewChannelForAuthRetry() {
  LOG(("nsHttpChannel::RedirectToNewChannelForAuthRetry %p", this));
  nsresult rv = NS_OK;

  nsCOMPtr<nsILoadInfo> redirectLoadInfo = CloneLoadInfoForRedirect(
      mURI, nsIChannelEventSink::REDIRECT_INTERNAL |
                nsIChannelEventSink::REDIRECT_AUTH_RETRY);

  nsCOMPtr<nsIIOService> ioService;

  rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> newChannel;
  rv = gHttpHandler->NewProxiedChannel(mURI, mProxyInfo, mProxyResolveFlags,
                                       mProxyURI, mLoadInfo,
                                       getter_AddRefs(newChannel));

  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetupReplacementChannel(mURI, newChannel, true,
                               nsIChannelEventSink::REDIRECT_INTERNAL |
                                   nsIChannelEventSink::REDIRECT_AUTH_RETRY);
  NS_ENSURE_SUCCESS(rv, rv);

  // rewind the upload stream
  if (mUploadStream) {
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mUploadStream);
    nsresult rv = NS_ERROR_NO_INTERFACE;
    if (seekable) {
      rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    }

    // This should not normally happen, but it's possible that big memory
    // blobs originating in the other process can't be rewinded.
    // In that case we just fail the request, otherwise the content length
    // will not match and this load will never complete.
    NS_ENSURE_SUCCESS(rv, rv);
  }

  RefPtr<nsHttpChannel> httpChannelImpl = do_QueryObject(newChannel);

  MOZ_ASSERT(mAuthProvider);
  httpChannelImpl->mAuthProvider = std::move(mAuthProvider);

  httpChannelImpl->mProxyInfo = mProxyInfo;

  if ((mCaps & NS_HTTP_STICKY_CONNECTION) ||
      mTransaction->HasStickyConnection()) {
    mConnectionInfo = mTransaction->GetConnInfo();

    httpChannelImpl->mTransactionSticky = mTransaction;

    if (mTransaction->Http2Disabled()) {
      httpChannelImpl->mCaps |= NS_HTTP_DISALLOW_SPDY;
    }
    if (mTransaction->Http3Disabled()) {
      httpChannelImpl->mCaps |= NS_HTTP_DISALLOW_HTTP3;
    }
  }
  httpChannelImpl->mCaps |= NS_HTTP_STICKY_CONNECTION;
  if (LoadAuthConnectionRestartable()) {
    httpChannelImpl->mCaps |= NS_HTTP_CONNECTION_RESTARTABLE;
  } else {
    httpChannelImpl->mCaps &= ~NS_HTTP_CONNECTION_RESTARTABLE;
  }

  MOZ_ASSERT(mConnectionInfo);
  httpChannelImpl->mConnectionInfo = mConnectionInfo->Clone();

  // we need to store the state to skip unnecessary checks in the new channel
  httpChannelImpl->StoreAuthRedirectedChannel(true);

  // We must copy proxy and auth header to the new channel.
  // Although the new channel can populate auth headers from auth cache, we
  // would still like to use the auth headers generated in this channel. The
  // main reason for doing this is that certain connection-based/stateful auth
  // schemes like NTLM will fail when we try generate the credentials more than
  // the number of times the server has presented us the challenge due to the
  // usage of nonce in generating the credentials Copying the auth header will
  // bypass generation of the credentials
  nsAutoCString authVal;
  if (NS_SUCCEEDED(GetRequestHeader("Proxy-Authorization"_ns, authVal))) {
    httpChannelImpl->SetRequestHeader("Proxy-Authorization"_ns, authVal, false);
  }
  if (NS_SUCCEEDED(GetRequestHeader("Authorization"_ns, authVal))) {
    httpChannelImpl->SetRequestHeader("Authorization"_ns, authVal, false);
  }

  httpChannelImpl->SetBlockAuthPrompt(LoadBlockAuthPrompt());
  mRedirectChannel = newChannel;

  rv = gHttpHandler->AsyncOnChannelRedirect(
      this, newChannel,
      nsIChannelEventSink::REDIRECT_INTERNAL |
          nsIChannelEventSink::REDIRECT_AUTH_RETRY);

  if (NS_SUCCEEDED(rv)) rv = WaitForRedirectCallback();

  // redirected channel will be opened after we receive the OnStopRequest

  if (NS_FAILED(rv)) {
    AutoRedirectVetoNotifier notifier(this, rv);
    mRedirectChannel = nullptr;
  }

  return rv;
}
nsresult nsHttpChannel::StartRedirectChannelToURI(nsIURI* upgradedURI,
                                                  uint32_t flags) {
  nsresult rv = NS_OK;
  LOG(("nsHttpChannel::StartRedirectChannelToURI()\n"));

  nsCOMPtr<nsIChannel> newChannel;
  nsCOMPtr<nsILoadInfo> redirectLoadInfo =
      CloneLoadInfoForRedirect(upgradedURI, flags);

  nsCOMPtr<nsIIOService> ioService;
  rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_NewChannelInternal(getter_AddRefs(newChannel), upgradedURI,
                             redirectLoadInfo,
                             nullptr,  // PerformanceStorage
                             nullptr,  // aLoadGroup
                             nullptr,  // aCallbacks
                             nsIRequest::LOAD_NORMAL, ioService);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetupReplacementChannel(upgradedURI, newChannel, true, flags);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mHTTPSSVCRecord) {
    RefPtr<nsHttpChannel> httpChan = do_QueryObject(newChannel);
    nsCOMPtr<nsIDNSHTTPSSVCRecord> rec = mHTTPSSVCRecord.ref();
    if (httpChan && rec) {
      httpChan->SetHTTPSSVCRecord(rec.forget());
    }
  }

  // Inform consumers about this fake redirect
  mRedirectChannel = newChannel;

  PushRedirectAsyncFunc(&nsHttpChannel::ContinueAsyncRedirectChannelToURI);
  rv = gHttpHandler->AsyncOnChannelRedirect(this, newChannel, flags);

  if (NS_SUCCEEDED(rv)) rv = WaitForRedirectCallback();

  if (NS_FAILED(rv)) {
    AutoRedirectVetoNotifier notifier(this, rv);

    /* Remove the async call to ContinueAsyncRedirectChannelToURI().
     * It is called directly by our callers upon return (to clean up
     * the failed redirect). */
    PopRedirectAsyncFunc(&nsHttpChannel::ContinueAsyncRedirectChannelToURI);
  }

  return rv;
}

nsresult nsHttpChannel::ContinueAsyncRedirectChannelToURI(nsresult rv) {
  LOG(("nsHttpChannel::ContinueAsyncRedirectChannelToURI [this=%p]", this));

  // Since we handle mAPIRedirectTo uri also after on-examine-response handler
  // rather drop it here to avoid any redirect loops, even just hypothetical.
  mAPIRedirectTo = Nothing();

  if (NS_SUCCEEDED(rv)) {
    rv = OpenRedirectChannel(rv);
  }

  if (NS_FAILED(rv)) {
    // Cancel the channel here, the update to https had been vetoed
    // but from the security reasons we have to discard the whole channel
    // load.
    Cancel(rv);
  }

  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, mStatus);
  }

  if (NS_FAILED(rv) && !mCachePump && !mTransactionPump) {
    // We have to manually notify the listener because there is not any pump
    // that would call our OnStart/StopRequest after resume from waiting for
    // the redirect callback.
    DoNotifyListener();
  }

  return rv;
}

nsresult nsHttpChannel::OpenRedirectChannel(nsresult rv) {
  AutoRedirectVetoNotifier notifier(this, rv);

  if (NS_FAILED(rv)) return rv;

  if (!mRedirectChannel) {
    LOG((
        "nsHttpChannel::OpenRedirectChannel unexpected null redirect channel"));
    return NS_ERROR_FAILURE;
  }

  // Make sure to do this after we received redirect veto answer,
  // i.e. after all sinks had been notified
  mRedirectChannel->SetOriginalURI(mOriginalURI);

  // open new channel
  rv = mRedirectChannel->AsyncOpen(mListener);

  NS_ENSURE_SUCCESS(rv, rv);

  mStatus = NS_BINDING_REDIRECTED;

  notifier.RedirectSucceeded();

  ReleaseListeners();

  return NS_OK;
}

nsresult nsHttpChannel::AsyncDoReplaceWithProxy(nsIProxyInfo* pi) {
  LOG(("nsHttpChannel::AsyncDoReplaceWithProxy [this=%p pi=%p]", this, pi));
  nsresult rv;

  nsCOMPtr<nsIChannel> newChannel;
  rv = gHttpHandler->NewProxiedChannel(mURI, pi, mProxyResolveFlags, mProxyURI,
                                       mLoadInfo, getter_AddRefs(newChannel));
  if (NS_FAILED(rv)) return rv;

  uint32_t flags = nsIChannelEventSink::REDIRECT_INTERNAL;

  rv = SetupReplacementChannel(mURI, newChannel, true, flags);
  if (NS_FAILED(rv)) return rv;

  // Inform consumers about this fake redirect
  mRedirectChannel = newChannel;

  PushRedirectAsyncFunc(&nsHttpChannel::OpenRedirectChannel);
  rv = gHttpHandler->AsyncOnChannelRedirect(this, newChannel, flags);

  if (NS_SUCCEEDED(rv)) rv = WaitForRedirectCallback();

  if (NS_FAILED(rv)) {
    AutoRedirectVetoNotifier notifier(this, rv);
    PopRedirectAsyncFunc(&nsHttpChannel::OpenRedirectChannel);
  }

  return rv;
}

nsresult nsHttpChannel::ResolveProxy() {
  LOG(("nsHttpChannel::ResolveProxy [this=%p]\n", this));

  nsresult rv;

  nsCOMPtr<nsIProtocolProxyService> pps;
  pps = mozilla::components::ProtocolProxy::Service(&rv);
  if (NS_FAILED(rv)) return rv;

  // using the nsIProtocolProxyService2 allows a minor performance
  // optimization, but if an add-on has only provided the original interface
  // then it is ok to use that version.
  nsCOMPtr<nsIProtocolProxyService2> pps2 = do_QueryInterface(pps);
  if (pps2) {
    rv = pps2->AsyncResolve2(this, mProxyResolveFlags, this, nullptr,
                             getter_AddRefs(mProxyRequest));
  } else {
    rv = pps->AsyncResolve(static_cast<nsIChannel*>(this), mProxyResolveFlags,
                           this, nullptr, getter_AddRefs(mProxyRequest));
  }

  return rv;
}

bool nsHttpChannel::ResponseWouldVary(nsICacheEntry* entry) {
  nsresult rv;
  nsAutoCString buf, metaKey;
  Unused << mCachedResponseHead->GetHeader(nsHttp::Vary, buf);

  constexpr auto prefix = "request-"_ns;

  // enumerate the elements of the Vary header...
  for (const nsACString& token :
       nsCCharSeparatedTokenizer(buf, NS_HTTP_HEADER_SEP).ToRange()) {
    LOG(
        ("nsHttpChannel::ResponseWouldVary [channel=%p] "
         "processing %s\n",
         this, nsPromiseFlatCString(token).get()));
    //
    // if "*", then assume response would vary.  technically speaking,
    // "Vary: header, *" is not permitted, but we allow it anyways.
    //
    // We hash values of cookie-headers for the following reasons:
    //
    //   1- cookies can be very large in size
    //
    //   2- cookies may contain sensitive information.  (for parity with
    //      out policy of not storing Set-cookie headers in the cache
    //      meta data, we likewise do not want to store cookie headers
    //      here.)
    //
    if (token.EqualsLiteral("*")) {
      return true;  // if we encounter this, just get out of here
    }

    // build cache meta data key...
    metaKey = prefix + token;

    // check the last value of the given request header to see if it has
    // since changed.  if so, then indeed the cached response is invalid.
    nsCString lastVal;
    entry->GetMetaDataElement(metaKey.get(), getter_Copies(lastVal));
    LOG(
        ("nsHttpChannel::ResponseWouldVary [channel=%p] "
         "stored value = \"%s\"\n",
         this, lastVal.get()));

    // Look for value of "Cookie" in the request headers
    nsHttpAtom atom = nsHttp::ResolveAtom(token);
    nsAutoCString newVal;
    bool hasHeader = NS_SUCCEEDED(mRequestHead.GetHeader(atom, newVal));
    if (!lastVal.IsEmpty()) {
      // value for this header in cache, but no value in request
      if (!hasHeader) {
        return true;  // yes - response would vary
      }

      // If this is a cookie-header, stored metadata is not
      // the value itself but the hash. So we also hash the
      // outgoing value here in order to compare the hashes
      nsAutoCString hash;
      if (atom == nsHttp::Cookie) {
        rv = Hash(newVal.get(), hash);
        // If hash failed, be conservative (the cached hash
        // exists at this point) and claim response would vary
        if (NS_FAILED(rv)) return true;
        newVal = hash;

        LOG(
            ("nsHttpChannel::ResponseWouldVary [this=%p] "
             "set-cookie value hashed to %s\n",
             this, newVal.get()));
      }

      if (!newVal.Equals(lastVal)) {
        return true;  // yes, response would vary
      }

    } else if (hasHeader) {  // old value is empty, but newVal is set
      return true;
    }
  }

  return false;
}

// We need to have an implementation of this function just so that we can keep
// all references to mCallOnResume of type nsHttpChannel:  it's not OK in C++
// to set a member function ptr to  a base class function.
void nsHttpChannel::HandleAsyncAbort() {
  HttpAsyncAborter<nsHttpChannel>::HandleAsyncAbort();
}

//-----------------------------------------------------------------------------
// nsHttpChannel <byte-range>
//-----------------------------------------------------------------------------

bool nsHttpChannel::IsResumable(int64_t partialLen, int64_t contentLength,
                                bool ignoreMissingPartialLen) const {
  bool hasContentEncoding =
      mCachedResponseHead->HasHeader(nsHttp::Content_Encoding);

  nsAutoCString etag;
  Unused << mCachedResponseHead->GetHeader(nsHttp::ETag, etag);
  bool hasWeakEtag = !etag.IsEmpty() && StringBeginsWith(etag, "W/"_ns);

  return (partialLen < contentLength) &&
         (partialLen > 0 || ignoreMissingPartialLen) && !hasContentEncoding &&
         !hasWeakEtag && mCachedResponseHead->IsResumable() &&
         !LoadCustomConditionalRequest() && !mCachedResponseHead->NoStore();
}

nsresult nsHttpChannel::MaybeSetupByteRangeRequest(
    int64_t partialLen, int64_t contentLength, bool ignoreMissingPartialLen) {
  // Be pesimistic
  StoreIsPartialRequest(false);

  if (!IsResumable(partialLen, contentLength, ignoreMissingPartialLen)) {
    return NS_ERROR_NOT_RESUMABLE;
  }

  // looks like a partial entry we can reuse; add If-Range
  // and Range headers.
  nsresult rv = SetupByteRangeRequest(partialLen);
  if (NS_FAILED(rv)) {
    // Make the request unconditional again.
    UntieByteRangeRequest();
  }

  return rv;
}

nsresult nsHttpChannel::SetupByteRangeRequest(int64_t partialLen) {
  // cached content has been found to be partial, add necessary request
  // headers to complete cache entry.

  // use strongest validator available...
  nsAutoCString val;
  Unused << mCachedResponseHead->GetHeader(nsHttp::ETag, val);
  if (val.IsEmpty()) {
    Unused << mCachedResponseHead->GetHeader(nsHttp::Last_Modified, val);
  }
  if (val.IsEmpty()) {
    // if we hit this code it means mCachedResponseHead->IsResumable() is
    // either broken or not being called.
    MOZ_ASSERT_UNREACHABLE("no cache validator");
    StoreIsPartialRequest(false);
    return NS_ERROR_FAILURE;
  }

  char buf[64];
  SprintfLiteral(buf, "bytes=%" PRId64 "-", partialLen);

  DebugOnly<nsresult> rv{};
  rv = mRequestHead.SetHeader(nsHttp::Range, nsDependentCString(buf));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = mRequestHead.SetHeader(nsHttp::If_Range, val);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  StoreIsPartialRequest(true);

  return NS_OK;
}

void nsHttpChannel::UntieByteRangeRequest() {
  DebugOnly<nsresult> rv{};
  rv = mRequestHead.ClearHeader(nsHttp::Range);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = mRequestHead.ClearHeader(nsHttp::If_Range);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

nsresult nsHttpChannel::ProcessPartialContent(
    const std::function<nsresult(nsHttpChannel*, nsresult)>&
        aContinueProcessResponseFunc) {
  // ok, we've just received a 206
  //
  // we need to stream whatever data is in the cache out first, and then
  // pick up whatever data is on the wire, writing it into the cache.

  LOG(("nsHttpChannel::ProcessPartialContent [this=%p]\n", this));

  NS_ENSURE_TRUE(mCachedResponseHead, NS_ERROR_NOT_INITIALIZED);
  NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_NOT_INITIALIZED);

  // Check if the content-encoding we now got is different from the one we
  // got before
  nsAutoCString contentEncoding, cachedContentEncoding;
  // It is possible that there is not such headers
  Unused << mResponseHead->GetHeader(nsHttp::Content_Encoding, contentEncoding);
  Unused << mCachedResponseHead->GetHeader(nsHttp::Content_Encoding,
                                           cachedContentEncoding);
  if (nsCRT::strcasecmp(contentEncoding.get(), cachedContentEncoding.get()) !=
      0) {
    Cancel(NS_ERROR_INVALID_CONTENT_ENCODING);
    return CallOnStartRequest();
  }

  nsresult rv;

  int64_t cachedContentLength = mCachedResponseHead->ContentLength();
  int64_t entitySize = mResponseHead->TotalEntitySize();

  nsAutoCString contentRange;
  Unused << mResponseHead->GetHeader(nsHttp::Content_Range, contentRange);
  LOG(
      ("nsHttpChannel::ProcessPartialContent [this=%p trans=%p] "
       "original content-length %" PRId64 ", entity-size %" PRId64
       ", content-range %s\n",
       this, mTransaction.get(), cachedContentLength, entitySize,
       contentRange.get()));

  if ((entitySize >= 0) && (cachedContentLength >= 0) &&
      (entitySize != cachedContentLength)) {
    LOG(
        ("nsHttpChannel::ProcessPartialContent [this=%p] "
         "206 has different total entity size than the content length "
         "of the original partially cached entity.\n",
         this));

    mCacheEntry->AsyncDoom(nullptr);
    Cancel(NS_ERROR_CORRUPTED_CONTENT);
    return CallOnStartRequest();
  }

  if (LoadConcurrentCacheAccess()) {
    // We started to read cached data sooner than its write has been done.
    // But the concurrent write has not finished completely, so we had to
    // do a range request.  Now let the content coming from the network
    // be presented to consumers and also stored to the cache entry.

    rv = InstallCacheListener(mLogicalOffset);
    if (NS_FAILED(rv)) return rv;
  } else {
    // suspend the current transaction
    rv = mTransactionPump->Suspend();
    if (NS_FAILED(rv)) return rv;
  }

  // merge any new headers with the cached response headers
  mCachedResponseHead->UpdateHeaders(mResponseHead.get());

  // update the cached response head
  nsAutoCString head;
  mCachedResponseHead->Flatten(head, true);
  rv = mCacheEntry->SetMetaDataElement("response-head", head.get());
  if (NS_FAILED(rv)) return rv;

  // make the cached response be the current response
  mResponseHead = std::move(mCachedResponseHead);

  UpdateInhibitPersistentCachingFlag();

  rv = UpdateExpirationTime();
  if (NS_FAILED(rv)) return rv;

  // notify observers interested in looking at a response that has been
  // merged with any cached headers (http-on-examine-merged-response).
  gHttpHandler->OnExamineMergedResponse(this);

  if (LoadConcurrentCacheAccess()) {
    StoreCachedContentIsPartial(false);
    // Leave the ConcurrentCacheAccess flag set, we want to use it
    // to prevent duplicate OnStartRequest call on the target listener
    // in case this channel is canceled before it gets its OnStartRequest
    // from the http transaction.
    return rv;
  }

  // Now we continue reading the network response.
  // the cached content is valid, although incomplete.
  StoreCachedContentIsValid(CachedContentValidity::Valid);
  return CallOrWaitForResume([aContinueProcessResponseFunc](auto* self) {
    nsresult rv = self->ReadFromCache();
    return aContinueProcessResponseFunc(self, rv);
  });
}

nsresult nsHttpChannel::OnDoneReadingPartialCacheEntry(bool* streamDone) {
  nsresult rv;

  LOG(("nsHttpChannel::OnDoneReadingPartialCacheEntry [this=%p]", this));

  // by default, assume we would have streamed all data or failed...
  *streamDone = true;

  // setup cache listener to append to cache entry
  int64_t size;
  rv = mCacheEntry->GetDataSize(&size);
  if (NS_FAILED(rv)) return rv;

  rv = InstallCacheListener(size);
  if (NS_FAILED(rv)) return rv;

  // Entry is valid, do it now, after the output stream has been opened,
  // otherwise when done earlier, pending readers would consider the cache
  // entry still as partial (CacheEntry::GetDataSize would return the partial
  // data size) and consumers would do the conditional request again.
  rv = mCacheEntry->SetValid();
  if (NS_FAILED(rv)) return rv;

  // need to track the logical offset of the data being sent to our listener
  mLogicalOffset = size;

  // we're now completing the cached content, so we can clear this flag.
  // this puts us in the state of a regular download.
  StoreCachedContentIsPartial(false);
  // The cache input stream pump is finished, we do not need it any more.
  // (see bug 1313923)
  mCachePump = nullptr;

  // resume the transaction if it exists, otherwise the pipe contained the
  // remaining part of the document and we've now streamed all of the data.
  if (mTransactionPump) {
    rv = mTransactionPump->Resume();
    if (NS_SUCCEEDED(rv)) *streamDone = false;
  } else {
    MOZ_ASSERT_UNREACHABLE("no transaction");
  }
  return rv;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <cache>
//-----------------------------------------------------------------------------

bool nsHttpChannel::ShouldBypassProcessNotModified() {
  if (LoadCustomConditionalRequest()) {
    LOG(("Bypassing ProcessNotModified due to custom conditional headers"));
    return true;
  }

  if (!mDidReval) {
    LOG(
        ("Server returned a 304 response even though we did not send a "
         "conditional request"));
    return true;
  }

  return false;
}

nsresult nsHttpChannel::ProcessNotModified(
    const std::function<nsresult(nsHttpChannel*, nsresult)>&
        aContinueProcessResponseFunc) {
  nsresult rv;

  LOG(("nsHttpChannel::ProcessNotModified [this=%p]\n", this));

  // Assert ShouldBypassProcessNotModified() has been checked before call to
  // ProcessNotModified().
  MOZ_ASSERT(!ShouldBypassProcessNotModified());

  MOZ_ASSERT(mCachedResponseHead);
  MOZ_ASSERT(mCacheEntry);
  NS_ENSURE_TRUE(mCachedResponseHead && mCacheEntry, NS_ERROR_UNEXPECTED);

  // If the 304 response contains a Last-Modified different than the
  // one in our cache that is pretty suspicious and is, in at least the
  // case of bug 716840, a sign of the server having previously corrupted
  // our cache with a bad response. Take the minor step here of just dooming
  // that cache entry so there is a fighting chance of getting things on the
  // right track.

  nsAutoCString lastModifiedCached;
  nsAutoCString lastModified304;

  rv =
      mCachedResponseHead->GetHeader(nsHttp::Last_Modified, lastModifiedCached);
  if (NS_SUCCEEDED(rv)) {
    rv = mResponseHead->GetHeader(nsHttp::Last_Modified, lastModified304);
  }

  if (NS_SUCCEEDED(rv) && !lastModified304.Equals(lastModifiedCached)) {
    LOG(
        ("Cache Entry and 304 Last-Modified Headers Do Not Match "
         "[%s] and [%s]\n",
         lastModifiedCached.get(), lastModified304.get()));

    mCacheEntry->AsyncDoom(nullptr);
    glean::http::cache_lm_inconsistent
        .EnumGet(glean::http::CacheLmInconsistentLabel::eTrue)
        .Add();
  }

  // merge any new headers with the cached response headers
  mCachedResponseHead->UpdateHeaders(mResponseHead.get());

  // update the cached response head
  nsAutoCString head;
  mCachedResponseHead->Flatten(head, true);
  rv = mCacheEntry->SetMetaDataElement("response-head", head.get());
  if (NS_FAILED(rv)) return rv;

  if (StaticPrefs::network_http_network_error_logging_enabled() &&
      LoadUsedNetwork() && !mReportedNEL) {
    if (nsCOMPtr<nsINetworkErrorLogging> nel =
            components::NetworkErrorLogging::Service()) {
      nel->GenerateNELReport(this);
    }
    mReportedNEL = true;
  }

  // make the cached response be the current response
  mResponseHead = std::move(mCachedResponseHead);

  UpdateInhibitPersistentCachingFlag();

  rv = UpdateExpirationTime();
  if (NS_FAILED(rv)) return rv;

  rv = AddCacheEntryHeaders(mCacheEntry);
  if (NS_FAILED(rv)) return rv;

  // notify observers interested in looking at a reponse that has been
  // merged with any cached headers
  gHttpHandler->OnExamineMergedResponse(this);

  StoreCachedContentIsValid(CachedContentValidity::Valid);

  // Tell other consumers the entry is OK to use
  rv = mCacheEntry->SetValid();
  if (NS_FAILED(rv)) return rv;

  return CallOrWaitForResume([aContinueProcessResponseFunc](auto* self) {
    nsresult rv = self->ReadFromCache();
    return aContinueProcessResponseFunc(self, rv);
  });
}

// Determines if a request is a byte range request for a subrange,
// i.e. is a byte range request, but not a 0- byte range request.
static bool IsSubRangeRequest(nsHttpRequestHead& aRequestHead) {
  nsAutoCString byteRange;
  if (NS_FAILED(aRequestHead.GetHeader(nsHttp::Range, byteRange))) {
    return false;
  }

  if (byteRange.EqualsLiteral("bytes=0-")) {
#ifndef ANDROID
    glean::network::byte_range_request.Get("cacheable"_ns).Add(1);
#endif
    return false;
  }
#ifndef ANDROID
  glean::network::byte_range_request.Get("not_cacheable"_ns).Add(1);
#endif
  return true;
}

nsresult nsHttpChannel::OpenCacheEntry(bool isHttps) {
  // Drop this flag here
  StoreConcurrentCacheAccess(0);

  LOG(("nsHttpChannel::OpenCacheEntry [this=%p]", this));

  // make sure we're not abusing this function
  MOZ_ASSERT(!mCacheEntry, "cache entry already open");
  if (!mRequestHead.IsGet() && !mRequestHead.IsHead() &&
      !mRequestHead.IsPost() && !mRequestHead.IsPatch()) {
    // don't use the cache for other types of requests
    return NS_OK;
  }

  MOZ_ASSERT_IF(mRequestHead.IsPost() || mRequestHead.IsPatch(), mPostID > 0);

  return OpenCacheEntryInternal(isHttps);
}

nsresult nsHttpChannel::OpenCacheEntryInternal(bool isHttps) {
  nsresult rv;

  if (LoadResuming()) {
    // We don't support caching for requests initiated
    // via nsIResumableChannel.
    return NS_OK;
  }

  // Don't cache byte range requests which are subranges, only cache 0-
  // byte range requests.
  if (IsSubRangeRequest(mRequestHead)) {
    return NS_OK;
  }

  // Handle correctly WaitForCacheEntry
  AutoCacheWaitFlags waitFlags(this);

  nsAutoCString cacheKey;

  nsCOMPtr<nsICacheStorageService> cacheStorageService(
      components::CacheStorage::Service());
  if (!cacheStorageService) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsICacheStorage> cacheStorage;
  mCacheEntryURI = mURI;

  RefPtr<LoadContextInfo> info = GetLoadContextInfo(this);
  if (!info) {
    return NS_ERROR_FAILURE;
  }

  uint32_t cacheEntryOpenFlags;
  bool offline = gIOService->IsOffline();

  RefPtr<mozilla::dom::BrowsingContext> bc;
  mLoadInfo->GetBrowsingContext(getter_AddRefs(bc));

  bool maybeRCWN = false;

  nsAutoCString cacheControlRequestHeader;
  Unused << mRequestHead.GetHeader(nsHttp::Cache_Control,
                                   cacheControlRequestHeader);
  CacheControlParser cacheControlRequest(cacheControlRequestHeader);
  if (cacheControlRequest.NoStore()) {
    return NS_OK;
  }

  bool forceOffline = bc && bc->Top()->GetForceOffline();
  if (offline || (mLoadFlags & INHIBIT_CACHING) || forceOffline) {
    if (BYPASS_LOCAL_CACHE(mLoadFlags, LoadPreferCacheLoadOverBypass()) &&
        !offline && !forceOffline) {
      return NS_OK;
    }
    cacheEntryOpenFlags = nsICacheStorage::OPEN_READONLY;
    StoreCacheEntryIsReadOnly(true);
  } else if (BYPASS_LOCAL_CACHE(mLoadFlags, LoadPreferCacheLoadOverBypass())) {
    cacheEntryOpenFlags = nsICacheStorage::OPEN_TRUNCATE;
  } else {
    cacheEntryOpenFlags =
        nsICacheStorage::OPEN_NORMALLY | nsICacheStorage::CHECK_MULTITHREADED;
  }

  // Remember the request is a custom conditional request so that we can
  // process any 304 response correctly.
  StoreCustomConditionalRequest(
      mRequestHead.HasHeader(nsHttp::If_Modified_Since) ||
      mRequestHead.HasHeader(nsHttp::If_None_Match) ||
      mRequestHead.HasHeader(nsHttp::If_Unmodified_Since) ||
      mRequestHead.HasHeader(nsHttp::If_Match) ||
      mRequestHead.HasHeader(nsHttp::If_Range));

  if (mLoadFlags & INHIBIT_PERSISTENT_CACHING) {
    rv = cacheStorageService->MemoryCacheStorage(
        info,  // ? choose app cache as well...
        getter_AddRefs(cacheStorage));
  } else if (LoadPinCacheContent()) {
    rv = cacheStorageService->PinningCacheStorage(info,
                                                  getter_AddRefs(cacheStorage));
  } else {
    // Try to race only if we use disk cache storage
    maybeRCWN = mRequestHead.IsSafeMethod();
    rv = cacheStorageService->DiskCacheStorage(info,
                                               getter_AddRefs(cacheStorage));
  }
  NS_ENSURE_SUCCESS(rv, rv);

  if ((mClassOfService.Flags() & nsIClassOfService::Leader) ||
      (mLoadFlags & LOAD_INITIAL_DOCUMENT_URI)) {
    cacheEntryOpenFlags |= nsICacheStorage::OPEN_PRIORITY;
  }

  // Only for backward compatibility with the old cache back end.
  // When removed, remove the flags and related code snippets.
  if (mLoadFlags & LOAD_BYPASS_LOCAL_CACHE_IF_BUSY) {
    cacheEntryOpenFlags |= nsICacheStorage::OPEN_BYPASS_IF_BUSY;
  }

  if (mPostID) {
    mCacheIdExtension.Append(nsPrintfCString("%d", mPostID));
  }
  if (LoadIsTRRServiceChannel()) {
    mCacheIdExtension.Append("TRR");
  }
  if (mRequestHead.IsHead()) {
    mCacheIdExtension.Append("HEAD");
  }
  bool isThirdParty = false;
  if (StaticPrefs::network_fetch_cache_partition_cross_origin() &&
      (NS_FAILED(mLoadInfo->TriggeringPrincipal()->IsThirdPartyChannel(
           this, &isThirdParty)) ||
       isThirdParty) &&
      (mLoadInfo->InternalContentPolicyType() == nsIContentPolicy::TYPE_FETCH ||
       mLoadInfo->InternalContentPolicyType() ==
           nsIContentPolicy::TYPE_XMLHTTPREQUEST ||
       mLoadInfo->InternalContentPolicyType() ==
           nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_ASYNC ||
       mLoadInfo->InternalContentPolicyType() ==
           nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_SYNC)) {
    mCacheIdExtension.Append("FETCH");
  }

  mCacheOpenWithPriority = cacheEntryOpenFlags & nsICacheStorage::OPEN_PRIORITY;
  mCacheQueueSizeWhenOpen =
      CacheStorageService::CacheQueueSize(mCacheOpenWithPriority);

  if ((mNetworkTriggerDelay || StaticPrefs::network_http_rcwn_enabled()) &&
      maybeRCWN && mAllowRCWN) {
    bool hasAltData = false;
    uint32_t sizeInKb = 0;
    rv = cacheStorage->GetCacheIndexEntryAttrs(
        mCacheEntryURI, mCacheIdExtension, &hasAltData, &sizeInKb);

    // We will attempt to race the network vs the cache if we've found
    // this entry in the cache index, and it has appropriate attributes
    // (doesn't have alt-data, and has a small size)
    if (NS_SUCCEEDED(rv) && !hasAltData &&
        sizeInKb < StaticPrefs::network_http_rcwn_small_resource_size_kb()) {
      MaybeRaceCacheWithNetwork();
    }
  }

  if (!mCacheOpenDelay) {
    MOZ_ASSERT(NS_IsMainThread(), "Should be called on the main thread");
    if (mNetworkTriggered) {
      mRaceCacheWithNetwork = StaticPrefs::network_http_rcwn_enabled();
    }
    rv = cacheStorage->AsyncOpenURI(mCacheEntryURI, mCacheIdExtension,
                                    cacheEntryOpenFlags, this);
  } else {
    // We pass `this` explicitly as a parameter due to the raw pointer
    // to refcounted object in lambda analysis.
    mCacheOpenFunc = [cacheEntryOpenFlags,
                      cacheStorage](nsHttpChannel* self) -> void {
      MOZ_ASSERT(NS_IsMainThread(), "Should be called on the main thread");
      cacheStorage->AsyncOpenURI(self->mCacheEntryURI, self->mCacheIdExtension,
                                 cacheEntryOpenFlags, self);
    };

    // calls nsHttpChannel::Notify after `mCacheOpenDelay` milliseconds
    auto callback = MakeRefPtr<TimerCallback>(this);
    NS_NewTimerWithCallback(getter_AddRefs(mCacheOpenTimer), callback,
                            mCacheOpenDelay, nsITimer::TYPE_ONE_SHOT);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  waitFlags.Keep(WAIT_FOR_CACHE_ENTRY);

  return NS_OK;
}

nsresult nsHttpChannel::CheckPartial(nsICacheEntry* aEntry, int64_t* aSize,
                                     int64_t* aContentLength) {
  return nsHttp::CheckPartial(
      aEntry, aSize, aContentLength,
      mCachedResponseHead ? mCachedResponseHead.get() : mResponseHead.get());
}

void nsHttpChannel::UntieValidationRequest() {
  DebugOnly<nsresult> rv{};
  // Make the request unconditional again.
  rv = mRequestHead.ClearHeader(nsHttp::If_Modified_Since);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = mRequestHead.ClearHeader(nsHttp::If_None_Match);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = mRequestHead.ClearHeader(nsHttp::ETag);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

NS_IMETHODIMP
nsHttpChannel::OnCacheEntryCheck(nsICacheEntry* entry, uint32_t* aResult) {
  nsresult rv = NS_OK;

  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnCacheEntryCheck", NETWORK,
                            Flow::FromPointer(this));
  LOG(("nsHttpChannel::OnCacheEntryCheck enter [channel=%p entry=%p]", this,
       entry));

  mozilla::MutexAutoLock lock(mRCWNLock);

  if (mRaceCacheWithNetwork && mFirstResponseSource == RESPONSE_FROM_NETWORK) {
    LOG(
        ("Not using cached response because we've already got one from the "
         "network %p",
         this));
    *aResult = ENTRY_NOT_WANTED;

    // Net-win indicates that mOnStartRequestTimestamp is from net.
    TimeDuration savedTime = (TimeStamp::Now() - mOnStartRequestTimestamp);
    glean::network::race_cache_with_network_saved_time.AccumulateRawDuration(
        savedTime);
    PROFILER_MARKER_TEXT("RCWN", NETWORK, {},
                         nsPrintfCString("Network won by %" PRId64 "ms",
                                         int64_t(savedTime.ToMilliseconds())));
    return NS_OK;
  }
  if (mRaceCacheWithNetwork && mFirstResponseSource == RESPONSE_PENDING) {
    mOnCacheEntryCheckTimestamp = TimeStamp::Now();
  }

  nsAutoCString cacheControlRequestHeader;
  Unused << mRequestHead.GetHeader(nsHttp::Cache_Control,
                                   cacheControlRequestHeader);
  CacheControlParser cacheControlRequest(cacheControlRequestHeader);

  if (cacheControlRequest.NoStore()) {
    LOG(
        ("Not using cached response based on no-store request cache "
         "directive\n"));
    *aResult = ENTRY_NOT_WANTED;
    return NS_OK;
  }

  // Be pessimistic: assume the cache entry has no useful data.
  *aResult = ENTRY_WANTED;
  StoreCachedContentIsValid(CachedContentValidity::Invalid);

  nsCString buf;

  // Get the method that was used to generate the cached response
  rv = entry->GetMetaDataElement("request-method", getter_Copies(buf));
  NS_ENSURE_SUCCESS(rv, rv);

  bool methodWasHead = buf.EqualsLiteral("HEAD");
  bool methodWasGet = buf.EqualsLiteral("GET");

  if (methodWasHead) {
    // The cached response does not contain an entity.  We can only reuse
    // the response if the current request is also HEAD.
    if (!mRequestHead.IsHead()) {
      *aResult = ENTRY_NOT_WANTED;
      return NS_OK;
    }
  }
  buf.Adopt(nullptr);

  // We'll need this value in later computations...
  uint32_t lastModifiedTime;
  rv = entry->GetLastModified(&lastModifiedTime);
  NS_ENSURE_SUCCESS(rv, rv);

  // Determine if this is the first time that this cache entry
  // has been accessed during this session.
  bool fromPreviousSession =
      (gHttpHandler->SessionStartTime() > lastModifiedTime);

  // Get the cached HTTP response headers
  mCachedResponseHead = MakeUnique<nsHttpResponseHead>();

  rv = nsHttp::GetHttpResponseHeadFromCacheEntry(entry,
                                                 mCachedResponseHead.get());
  NS_ENSURE_SUCCESS(rv, rv);

  bool isCachedRedirect = WillRedirect(*mCachedResponseHead);

  // Do not return 304 responses from the cache, and also do not return
  // any other non-redirect 3xx responses from the cache (see bug 759043).
  NS_ENSURE_TRUE((mCachedResponseHead->Status() / 100 != 3) || isCachedRedirect,
                 NS_ERROR_ABORT);

  if (mCachedResponseHead->NoStore() && LoadCacheEntryIsReadOnly()) {
    // This prevents loading no-store responses when navigating back
    // while the browser is set to work offline.
    LOG(("  entry loading as read-only but is no-store, set INHIBIT_CACHING"));
    mLoadFlags |= nsIRequest::INHIBIT_CACHING;
  }

  // Don't bother to validate items that are read-only,
  // unless they are read-only because of INHIBIT_CACHING
  if ((LoadCacheEntryIsReadOnly() &&
       !(mLoadFlags & nsIRequest::INHIBIT_CACHING))) {
    int64_t size, contentLength;
    rv = CheckPartial(entry, &size, &contentLength);
    NS_ENSURE_SUCCESS(rv, rv);

    if (contentLength != int64_t(-1) && contentLength != size) {
      *aResult = ENTRY_NOT_WANTED;
      return NS_OK;
    }

    rv = OpenCacheInputStream(entry, true);
    if (NS_SUCCEEDED(rv)) {
      StoreCachedContentIsValid(CachedContentValidity::Valid);
    }
    return rv;
  }

  bool wantCompleteEntry = false;

  if (!methodWasHead && !isCachedRedirect) {
    // If the cached content-length is set and it does not match the data
    // size of the cached content, then the cached response is partial...
    // either we need to issue a byte range request or we need to refetch
    // the entire document.
    //
    // We exclude redirects from this check because we (usually) strip the
    // entity when we store the cache entry, and even if we didn't, we
    // always ignore a cached redirect's entity anyway. See bug 759043.
    int64_t size, contentLength;
    rv = CheckPartial(entry, &size, &contentLength);
    NS_ENSURE_SUCCESS(rv, rv);

    if (size == int64_t(-1)) {
      LOG(("  write is in progress"));
      if (mLoadFlags & LOAD_BYPASS_LOCAL_CACHE_IF_BUSY) {
        LOG(
            ("  not interested in the entry, "
             "LOAD_BYPASS_LOCAL_CACHE_IF_BUSY specified"));

        *aResult = ENTRY_NOT_WANTED;
        return NS_OK;
      }

      // Ignore !(size > 0) from the resumability condition
      if (!IsResumable(size, contentLength, true)) {
        if (IsNavigation()) {
          LOG(
              ("  bypassing wait for the entry, "
               "this is a navigational load"));
          *aResult = ENTRY_NOT_WANTED;
          return NS_OK;
        }

        LOG(
            ("  wait for entry completion, "
             "response is not resumable"));

        wantCompleteEntry = true;
      } else {
        StoreConcurrentCacheAccess(1);
      }
    } else if (contentLength != int64_t(-1) && contentLength != size) {
      LOG(
          ("Cached data size does not match the Content-Length header "
           "[content-length=%" PRId64 " size=%" PRId64 "]\n",
           contentLength, size));

      rv = MaybeSetupByteRangeRequest(size, contentLength);
      StoreCachedContentIsPartial(NS_SUCCEEDED(rv) && LoadIsPartialRequest());
      if (LoadCachedContentIsPartial()) {
        rv = OpenCacheInputStream(entry, false);
        if (NS_FAILED(rv)) {
          UntieByteRangeRequest();
          return rv;
        }

        *aResult = ENTRY_NEEDS_REVALIDATION;
        return NS_OK;
      }

      if (size == 0 && LoadCacheOnlyMetadata()) {
        // Don't break cache entry load when the entry's data size
        // is 0 and CacheOnlyMetadata flag is set. In that case we
        // want to proceed since the LOAD_ONLY_IF_MODIFIED flag is
        // also set.
        MOZ_ASSERT(mLoadFlags & LOAD_ONLY_IF_MODIFIED);
      } else {
        return rv;
      }
    }
  }

  bool isHttps = mURI->SchemeIs("https");

  bool doValidation = false;
  bool doBackgroundValidation = false;
  bool canAddImsHeader = true;

  bool isForcedValid = false;
  entry->GetIsForcedValid(&isForcedValid);
  auto prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eUsed;

  bool weaklyFramed, isImmutable;
  nsHttp::DetermineFramingAndImmutability(entry, mCachedResponseHead.get(),
                                          isHttps, &weaklyFramed, &isImmutable);

  // Cached entry is not the entity we request (see bug #633743)
  if (ResponseWouldVary(entry)) {
    LOG(("Validating based on Vary headers returning TRUE\n"));
    canAddImsHeader = false;
    doValidation = true;
    prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eWouldvary;
  } else {
    if (mCachedResponseHead->ExpiresInPast() ||
        mCachedResponseHead->MustValidateIfExpired()) {
      prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eExpired;
    }
    doValidation = nsHttp::ValidationRequired(
        isForcedValid, mCachedResponseHead.get(), mLoadFlags,
        LoadAllowStaleCacheContent(), LoadForceValidateCacheContent(),
        isImmutable, LoadCustomConditionalRequest(), mRequestHead, entry,
        cacheControlRequest, fromPreviousSession, &doBackgroundValidation);
  }

  nsAutoCString requestedETag;
  if (!doValidation &&
      NS_SUCCEEDED(mRequestHead.GetHeader(nsHttp::If_Match, requestedETag)) &&
      (methodWasGet || methodWasHead)) {
    nsAutoCString cachedETag;
    Unused << mCachedResponseHead->GetHeader(nsHttp::ETag, cachedETag);
    if (!cachedETag.IsEmpty() && (StringBeginsWith(cachedETag, "W/"_ns) ||
                                  !requestedETag.Equals(cachedETag))) {
      // User has defined If-Match header, if the cached entry is not
      // matching the provided header value or the cached ETag is weak,
      // force validation.
      doValidation = true;
    }
  }

  // Previous error should not be propagated.
  rv = NS_OK;

  if (!doValidation) {
    //
    // Check the authorization headers used to generate the cache entry.
    // We must validate the cache entry if:
    //
    // 1) the cache entry was generated prior to this session w/
    //    credentials (see bug 103402).
    // 2) the cache entry was generated w/o credentials, but would now
    //    require credentials (see bug 96705).
    //
    // NOTE: this does not apply to proxy authentication.
    //
    entry->GetMetaDataElement("auth", getter_Copies(buf));
    doValidation =
        (fromPreviousSession && !buf.IsEmpty()) ||
        (buf.IsEmpty() && mRequestHead.HasHeader(nsHttp::Authorization));
    if (doValidation) {
      prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eAuth;
    }
  }

  // Bug #561276: We maintain a chain of cache-keys which returns cached
  // 3xx-responses (redirects) in order to detect cycles. If a cycle is
  // found, ignore the cached response and hit the net. Otherwise, use
  // the cached response and add the cache-key to the chain. Note that
  // a limited number of redirects (cached or not) is allowed and is
  // enforced independently of this mechanism
  if (!doValidation && isCachedRedirect) {
    nsAutoCString cacheKey;
    rv = GenerateCacheKey(mPostID, cacheKey);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    auto redirectedCachekeys = mRedirectedCachekeys.Lock();
    auto& ref = redirectedCachekeys.ref();
    if (!ref) {
      ref = MakeUnique<nsTArray<nsCString>>();
    } else if (ref->Contains(cacheKey)) {
      doValidation = true;
    }

    LOG(("Redirection-chain %s key %s\n",
         doValidation ? "contains" : "does not contain", cacheKey.get()));

    // Append cacheKey if not in the chain already
    if (!doValidation) {
      ref->AppendElement(cacheKey);
    } else {
      prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eRedirect;
    }
  }

  StoreCachedContentIsValid(!doValidation ? CachedContentValidity::Valid
                                          : CachedContentValidity::Invalid);

  if (isForcedValid) {
    // Telemetry value is only useful if this was a prefetched item
    if (!doValidation) {
      // Could have gotten to a funky state with some of the if chain above
      // and in nsHttp::ValidationRequired. Make sure we get it right here.
      prefetchStatus = glean::predictor::PrefetchUseStatusLabel::eUsed;

      entry->MarkForcedValidUse();
    }
    glean::predictor::prefetch_use_status.EnumGet(prefetchStatus).Add();
  }

  if (doValidation) {
    //
    // now, we are definitely going to issue a HTTP request to the server.
    // make it conditional if possible.
    //
    // do not attempt to validate no-store content, since servers will not
    // expect it to be cached.  (we only keep it in our cache for the
    // purposes of back/forward, etc.)
    //
    // the request method MUST be either GET or HEAD (see bug 175641) and
    // the cached response code must be < 400
    //
    // the cached content must not be weakly framed
    //
    // do not override conditional headers when consumer has defined its own
    if (!mCachedResponseHead->NoStore() &&
        (mRequestHead.IsGet() || mRequestHead.IsHead()) &&
        !LoadCustomConditionalRequest() && !weaklyFramed &&
        (mCachedResponseHead->Status() < 400)) {
      if (LoadConcurrentCacheAccess()) {
        // In case of concurrent read and also validation request we
        // must wait for the current writer to close the output stream
        // first.  Otherwise, when the writer's job would have been interrupted
        // before all the data were downloaded, we'd have to do a range request
        // which would be a second request in line during this channel's
        // life-time.  nsHttpChannel is not designed to do that, so rather
        // turn off concurrent read and wait for entry's completion.
        // Then only re-validation or range-re-validation request will go out.
        StoreConcurrentCacheAccess(0);
        // This will cause that OnCacheEntryCheck is called again with the same
        // entry after the writer is done.
        wantCompleteEntry = true;
      } else {
        nsAutoCString val;
        // Add If-Modified-Since header if a Last-Modified was given
        // and we are allowed to do this (see bugs 510359 and 269303)
        if (canAddImsHeader) {
          Unused << mCachedResponseHead->GetHeader(nsHttp::Last_Modified, val);
          if (!val.IsEmpty()) {
            rv = mRequestHead.SetHeader(nsHttp::If_Modified_Since, val);
            MOZ_ASSERT(NS_SUCCEEDED(rv));
          }
        }
        // Add If-None-Match header if an ETag was given in the response
        Unused << mCachedResponseHead->GetHeader(nsHttp::ETag, val);
        if (!val.IsEmpty()) {
          rv = mRequestHead.SetHeader(nsHttp::If_None_Match, val);
          MOZ_ASSERT(NS_SUCCEEDED(rv));
        }
        mDidReval = true;
      }
    }
  }

  bool valid = CachedContentIsValid();
  if (valid || mDidReval) {
    rv = OpenCacheInputStream(entry, valid);
    if (NS_FAILED(rv)) {
      // If we can't get the entity then we have to act as though we
      // don't have the cache entry.
      if (mDidReval) {
        UntieValidationRequest();
        mDidReval = false;
      }
      StoreCachedContentIsValid(CachedContentValidity::Invalid);
    }
  }

  if (mDidReval) {
    *aResult = ENTRY_NEEDS_REVALIDATION;
  } else if (wantCompleteEntry) {
    *aResult = RECHECK_AFTER_WRITE_FINISHED;
  } else {
    *aResult = ENTRY_WANTED;

    if (doBackgroundValidation) {
      PerformBackgroundCacheRevalidation();
    }
  }

  LOG(
      ("nsHTTPChannel::OnCacheEntryCheck exit [this=%p doValidation=%d "
       "result=%d]\n",
       this, doValidation, *aResult));
  return rv;
}

NS_IMETHODIMP
nsHttpChannel::OnCacheEntryAvailable(nsICacheEntry* entry, bool aNew,
                                     nsresult status) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnCacheEntryAvailable", NETWORK,
                            Flow::FromPointer(this));
  LOG(
      ("nsHttpChannel::OnCacheEntryAvailable [this=%p entry=%p "
       "new=%d status=%" PRIx32 "] for %s",
       this, entry, aNew, static_cast<uint32_t>(status), mSpec.get()));

  // if the channel's already fired onStopRequest, then we should ignore
  // this event.
  if (!LoadIsPending()) {
    mCacheInputStream.CloseAndRelease();
    return NS_OK;
  }

  rv = OnCacheEntryAvailableInternal(entry, aNew, status);
  if (NS_FAILED(rv)) {
    CloseCacheEntry(false);
    if (mRaceCacheWithNetwork && mNetworkTriggered &&
        mFirstResponseSource != RESPONSE_FROM_CACHE) {
      // Ignore the error if we're racing cache with network and the cache
      // didn't win, The network part will handle cancelation or any other
      // error. Otherwise we could end up calling the listener twice, see
      // bug 1397593.
      LOG(
          ("  not calling AsyncAbort() because we're racing cache with "
           "network"));
    } else {
      Unused << AsyncAbort(rv);
    }
  }

  return NS_OK;
}

nsresult nsHttpChannel::OnCacheEntryAvailableInternal(nsICacheEntry* entry,
                                                      bool aNew,
                                                      nsresult status) {
  nsresult rv;

  if (mCanceled) {
    LOG(("channel was canceled [this=%p status=%" PRIx32 "]\n", this,
         static_cast<uint32_t>(static_cast<nsresult>(mStatus))));
    return mStatus;
  }

  if (mIgnoreCacheEntry) {
    if (!entry || aNew) {
      // We use this flag later to decide whether to report
      // LABELS_NETWORK_RACE_CACHE_VALIDATION::NotSent. We didn't have
      // an usable entry, so drop the flag.
      mIgnoreCacheEntry = false;
    }
    entry = nullptr;
    status = NS_ERROR_NOT_AVAILABLE;
  }

  rv = OnNormalCacheEntryAvailable(entry, aNew, status);

  if (NS_FAILED(rv) && (mLoadFlags & LOAD_ONLY_FROM_CACHE)) {
    return NS_ERROR_DOCUMENT_NOT_CACHED;
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  // We may be waiting for more callbacks...
  if (AwaitingCacheCallbacks()) {
    return NS_OK;
  }

  bool valid = CachedContentIsValid();
  if (mRaceCacheWithNetwork &&
      ((mCacheEntry && !valid && (mDidReval || LoadCachedContentIsPartial())) ||
       mIgnoreCacheEntry)) {
    // We won't send the conditional request because the unconditional
    // request was already sent (see bug 1377223).
    glean::network::race_cache_validation
        .EnumGet(glean::network::RaceCacheValidationLabel::eNotsent)
        .Add();
  }

  if (mRaceCacheWithNetwork && valid) {
    Unused << ReadFromCache();
  }

  return TriggerNetwork();
}

nsresult nsHttpChannel::OnNormalCacheEntryAvailable(nsICacheEntry* aEntry,
                                                    bool aNew,
                                                    nsresult aEntryStatus) {
  StoreWaitForCacheEntry(LoadWaitForCacheEntry() & ~WAIT_FOR_CACHE_ENTRY);

  if (NS_FAILED(aEntryStatus) || aNew) {
    // Make sure this flag is dropped.  It may happen the entry is doomed
    // between OnCacheEntryCheck and OnCacheEntryAvailable.
    StoreCachedContentIsValid(CachedContentValidity::Invalid);

    // From the same reason remove any conditional headers added
    // in OnCacheEntryCheck.
    if (mDidReval) {
      LOG(("  Removing conditional request headers"));
      UntieValidationRequest();
      mDidReval = false;
    }

    if (LoadCachedContentIsPartial()) {
      LOG(("  Removing byte range request headers"));
      UntieByteRangeRequest();
      StoreCachedContentIsPartial(false);
    }

    if (mLoadFlags & LOAD_ONLY_FROM_CACHE) {
      // if this channel is only allowed to pull from the cache, then
      // we must fail if we were unable to open a cache entry for read.
      return NS_ERROR_DOCUMENT_NOT_CACHED;
    }
  }

  if (NS_SUCCEEDED(aEntryStatus)) {
    mCacheEntry = aEntry;
    StoreCacheEntryIsWriteOnly(aNew);

    if (!aNew && !mAsyncOpenTime.IsNull()) {
      // We use microseconds for IO operations. For consistency let's use
      // microseconds here too.
      uint32_t duration = (TimeStamp::Now() - mAsyncOpenTime).ToMicroseconds();
      bool isSlow = false;
      if ((mCacheOpenWithPriority &&
           mCacheQueueSizeWhenOpen >=
               StaticPrefs::
                   network_http_rcwn_cache_queue_priority_threshold()) ||
          (!mCacheOpenWithPriority &&
           mCacheQueueSizeWhenOpen >=
               StaticPrefs::network_http_rcwn_cache_queue_normal_threshold())) {
        isSlow = true;
      }
      CacheFileUtils::CachePerfStats::AddValue(
          CacheFileUtils::CachePerfStats::ENTRY_OPEN, duration, isSlow);
    }
  }

  return NS_OK;
}

// Generates the proper cache-key for this instance of nsHttpChannel
nsresult nsHttpChannel::GenerateCacheKey(uint32_t postID,
                                         nsACString& cacheKey) {
  AssembleCacheKey(mSpec.get(), postID, cacheKey);
  return NS_OK;
}

// Assembles a cache-key from the given pieces of information and |mLoadFlags|
void nsHttpChannel::AssembleCacheKey(const char* spec, uint32_t postID,
                                     nsACString& cacheKey) {
  cacheKey.Truncate();

  if (mLoadFlags & LOAD_ANONYMOUS) {
    cacheKey.AssignLiteral("anon&");
  }

  if (postID) {
    char buf[32];
    SprintfLiteral(buf, "id=%x&", postID);
    cacheKey.Append(buf);
  }

  if (!cacheKey.IsEmpty()) {
    cacheKey.AppendLiteral("uri=");
  }

  // Strip any trailing #ref from the URL before using it as the key
  const char* p = strchr(spec, '#');
  if (p) {
    cacheKey.Append(spec, p - spec);
  } else {
    cacheKey.Append(spec);
  }
}

nsresult DoUpdateExpirationTime(nsHttpChannel* aSelf,
                                nsICacheEntry* aCacheEntry,
                                nsHttpResponseHead* aResponseHead,
                                uint32_t& aExpirationTime) {
  MOZ_ASSERT(aExpirationTime == 0);
  NS_ENSURE_TRUE(aResponseHead, NS_ERROR_FAILURE);

  nsresult rv;

  if (!aResponseHead->MustValidate()) {
    // For stale-while-revalidate we use expiration time as the absolute base
    // for calculation of the stale window absolute end time.  Hence, when the
    // entry may be served w/o revalidation, we need a non-zero value for the
    // expiration time.  Let's set it to |now|, which basicly means "expired",
    // same as when set to 0.
    uint32_t now = NowInSeconds();
    aExpirationTime = now;

    uint32_t freshnessLifetime = 0;

    rv = aResponseHead->ComputeFreshnessLifetime(&freshnessLifetime);
    if (NS_FAILED(rv)) return rv;

    if (freshnessLifetime > 0) {
      uint32_t currentAge = 0;

      rv = aResponseHead->ComputeCurrentAge(now, aSelf->GetRequestTime(),
                                            &currentAge);
      if (NS_FAILED(rv)) return rv;

      LOG(("freshnessLifetime = %u, currentAge = %u\n", freshnessLifetime,
           currentAge));

      if (freshnessLifetime > currentAge) {
        uint32_t timeRemaining = freshnessLifetime - currentAge;
        // be careful... now + timeRemaining may overflow
        if (now + timeRemaining < now) {
          aExpirationTime = uint32_t(-1);
        } else {
          aExpirationTime = now + timeRemaining;
        }
      }
    }
  }

  rv = aCacheEntry->SetExpirationTime(aExpirationTime);
  NS_ENSURE_SUCCESS(rv, rv);

  return rv;
}

// UpdateExpirationTime is called when a new response comes in from the server.
// It updates the stored response-time and sets the expiration time on the
// cache entry.
//
// From section 13.2.4 of RFC2616, we compute expiration time as follows:
//
//    timeRemaining = freshnessLifetime - currentAge
//    expirationTime = now + timeRemaining
//
nsresult nsHttpChannel::UpdateExpirationTime() {
  uint32_t expirationTime = 0;
  nsresult rv = DoUpdateExpirationTime(this, mCacheEntry, mResponseHead.get(),
                                       expirationTime);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult nsHttpChannel::OpenCacheInputStream(nsICacheEntry* cacheEntry,
                                             bool startBuffering) {
  nsresult rv;

  if (mURI->SchemeIs("https")) {
    rv = cacheEntry->GetSecurityInfo(getter_AddRefs(mCachedSecurityInfo));
    if (NS_FAILED(rv)) {
      LOG(("failed to parse security-info [channel=%p, entry=%p]", this,
           cacheEntry));
      NS_WARNING("failed to parse security-info");
      cacheEntry->AsyncDoom(nullptr);
      return rv;
    }

    MOZ_ASSERT(mCachedSecurityInfo);
    if (!mCachedSecurityInfo) {
      LOG(
          ("mCacheEntry->GetSecurityInfo returned success but did not "
           "return the security info [channel=%p, entry=%p]",
           this, cacheEntry));
      cacheEntry->AsyncDoom(nullptr);
      return NS_ERROR_UNEXPECTED;  // XXX error code
    }
  }

  // Keep the conditions below in sync with the conditions in ReadFromCache.

  rv = NS_OK;

  if (WillRedirect(*mCachedResponseHead)) {
    // Do not even try to read the entity for a redirect because we do not
    // return an entity to the application when we process redirects.
    LOG(("Will skip read of cached redirect entity\n"));
    return NS_OK;
  }

  if ((mLoadFlags & nsICachingChannel::LOAD_ONLY_IF_MODIFIED) &&
      !LoadCachedContentIsPartial()) {
    // For LOAD_ONLY_IF_MODIFIED, we usually don't have to deal with the
    // cached entity.
    LOG(
        ("Will skip read from cache based on LOAD_ONLY_IF_MODIFIED "
         "load flag\n"));
    return NS_OK;
  }

  // Open an input stream for the entity, so that the call to OpenInputStream
  // happens off the main thread.
  nsCOMPtr<nsIInputStream> stream;

  // If an alternate representation was requested, try to open the alt
  // input stream.
  // If the entry has a "is-from-child" metadata, then only open the altdata
  // stream if the consumer is also from child.
  bool altDataFromChild = false;
  {
    nsCString value;
    rv = cacheEntry->GetMetaDataElement("alt-data-from-child",
                                        getter_Copies(value));
    altDataFromChild = !value.IsEmpty();
  }

  nsAutoCString altDataType;
  Unused << cacheEntry->GetAltDataType(altDataType);

  nsAutoCString contentType;
  mCachedResponseHead->ContentType(contentType);

  bool foundAltData = false;
  bool deliverAltData = true;
  if (!LoadDisableAltDataCache() && !altDataType.IsEmpty() &&
      !mPreferredCachedAltDataTypes.IsEmpty() &&
      altDataFromChild == LoadAltDataForChild()) {
    for (auto& pref : mPreferredCachedAltDataTypes) {
      if (pref.type() == altDataType &&
          (pref.contentType().IsEmpty() || pref.contentType() == contentType)) {
        foundAltData = true;
        deliverAltData =
            pref.deliverAltData() ==
            nsICacheInfoChannel::PreferredAlternativeDataDeliveryType::ASYNC;
        break;
      }
    }
  }

  nsCOMPtr<nsIInputStream> altData;
  int64_t altDataSize = -1;
  if (foundAltData) {
    rv = cacheEntry->OpenAlternativeInputStream(altDataType,
                                                getter_AddRefs(altData));
    if (NS_SUCCEEDED(rv)) {
      // We have succeeded.
      mAvailableCachedAltDataType = altDataType;
      StoreDeliveringAltData(deliverAltData);

      // Set the correct data size on the channel.
      Unused << cacheEntry->GetAltDataSize(&altDataSize);
      mAltDataLength = altDataSize;

      LOG(("Opened alt-data input stream [type=%s, size=%" PRId64
           ", deliverAltData=%d]",
           altDataType.get(), mAltDataLength, deliverAltData));

      if (deliverAltData) {
        stream = altData;
      }
    }
  }

  if (!stream) {
    rv = cacheEntry->OpenInputStream(0, getter_AddRefs(stream));
  }

  if (NS_FAILED(rv)) {
    LOG(
        ("Failed to open cache input stream [channel=%p, "
         "mCacheEntry=%p]",
         this, cacheEntry));
    return rv;
  }

  if (startBuffering) {
    bool nonBlocking;
    rv = stream->IsNonBlocking(&nonBlocking);
    if (NS_SUCCEEDED(rv) && nonBlocking) startBuffering = false;
  }

  if (!startBuffering) {
    // Bypass wrapping the input stream for the new cache back-end since
    // nsIStreamTransportService expects a blocking stream.  Preloading of
    // the data must be done on the level of the cache backend, internally.
    //
    // We do not connect the stream to the stream transport service if we
    // have to validate the entry with the server. If we did, we would get
    // into a race condition between the stream transport service reading
    // the existing contents and the opening of the cache entry's output
    // stream to write the new contents in the case where we get a non-304
    // response.
    LOG(
        ("Opened cache input stream without buffering [channel=%p, "
         "mCacheEntry=%p, stream=%p]",
         this, cacheEntry, stream.get()));
    mCacheInputStream.takeOver(stream);
    return rv;
  }

  // Have the stream transport service start reading the entity on one of its
  // background threads.

  nsCOMPtr<nsITransport> transport;
  nsCOMPtr<nsIInputStream> wrapper;

  nsCOMPtr<nsIStreamTransportService> sts(
      components::StreamTransport::Service());
  rv = sts ? NS_OK : NS_ERROR_NOT_AVAILABLE;
  if (NS_SUCCEEDED(rv)) {
    rv = sts->CreateInputTransport(stream, true, getter_AddRefs(transport));
  }
  if (NS_SUCCEEDED(rv)) {
    rv = transport->OpenInputStream(0, 0, 0, getter_AddRefs(wrapper));
  }
  if (NS_SUCCEEDED(rv)) {
    LOG(
        ("Opened cache input stream [channel=%p, wrapper=%p, "
         "transport=%p, stream=%p]",
         this, wrapper.get(), transport.get(), stream.get()));
  } else {
    LOG(
        ("Failed to open cache input stream [channel=%p, "
         "wrapper=%p, transport=%p, stream=%p]",
         this, wrapper.get(), transport.get(), stream.get()));

    stream->Close();
    return rv;
  }

  mCacheInputStream.takeOver(wrapper);

  return NS_OK;
}

// Actually process the cached response that we started to handle in CheckCache
// and/or StartBufferingCachedEntity.
nsresult nsHttpChannel::ReadFromCache(void) {
  NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(CachedContentIsValid(), NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(!mCachePump, NS_OK);  // already opened

  LOG(
      ("nsHttpChannel::ReadFromCache [this=%p] "
       "Using cached copy of: %s\n",
       this, mSpec.get()));

  // When racing the cache with the network with a timer, and we get data from
  // the cache, we should prevent the timer from triggering a network request.
  if (mNetworkTriggerTimer) {
    mNetworkTriggerTimer->Cancel();
    mNetworkTriggerTimer = nullptr;
  }

  if (mRaceCacheWithNetwork) {
    MOZ_ASSERT(mFirstResponseSource != RESPONSE_FROM_CACHE);
    if (mFirstResponseSource == RESPONSE_PENDING) {
      LOG(("First response from cache"));
      PROFILER_MARKER_TEXT(
          "RCWN", NETWORK, {},
          nsPrintfCString("Cache won for %s (%p)", mSpec.get(), this));
      mFirstResponseSource = RESPONSE_FROM_CACHE;

      // Cancel the transaction because we will serve the request from the cache
      CancelNetworkRequest(NS_BINDING_ABORTED);
      if (mTransactionPump && mSuspendCount) {
        uint32_t suspendCount = mSuspendCount;
        while (suspendCount--) {
          mTransactionPump->Resume();
        }
      }
      mTransaction = nullptr;
      mTransactionPump = nullptr;
    } else {
      MOZ_ASSERT(mFirstResponseSource == RESPONSE_FROM_NETWORK);
      LOG(
          ("Skipping read from cache because first response was from "
           "network\n"));

      if (!mOnCacheEntryCheckTimestamp.IsNull()) {
        TimeStamp currentTime = TimeStamp::Now();
        TimeDuration savedTime = currentTime - mOnStartRequestTimestamp;
        glean::network::race_cache_with_network_saved_time
            .AccumulateRawDuration(savedTime);

        PROFILER_MARKER_TEXT(
            "RCWN", NETWORK, {},
            nsPrintfCString("Network won by %" PRId64 "ms for %s",
                            int64_t(savedTime.ToMilliseconds()), mSpec.get()));
        TimeDuration diffTime = currentTime - mOnCacheEntryCheckTimestamp;
        glean::network::race_cache_with_network_ocec_on_start_diff
            .AccumulateRawDuration(diffTime);
      }
      return NS_OK;
    }
  }

  if (mCachedResponseHead) mResponseHead = std::move(mCachedResponseHead);

  UpdateInhibitPersistentCachingFlag();

  // if we don't already have security info, try to get it from the cache
  // entry. there are two cases to consider here: 1) we are just reading
  // from the cache, or 2) this may be due to a 304 not modified response,
  // in which case we could have security info from a socket transport.
  if (!mSecurityInfo) mSecurityInfo = mCachedSecurityInfo;

  nsresult rv;

  // Keep the conditions below in sync with the conditions in
  // StartBufferingCachedEntity.

  if (WillRedirect(*mResponseHead)) {
    // TODO: Bug 759040 - We should call HandleAsyncRedirect directly here,
    // to avoid event dispatching latency.
    MOZ_ASSERT(!mCacheInputStream);
    LOG(("Skipping skip read of cached redirect entity\n"));
    return AsyncCall(&nsHttpChannel::HandleAsyncRedirect);
  }

  if ((mLoadFlags & LOAD_ONLY_IF_MODIFIED) && !LoadCachedContentIsPartial()) {
    LOG(
        ("Skipping read from cache based on LOAD_ONLY_IF_MODIFIED "
         "load flag\n"));
    MOZ_ASSERT(!mCacheInputStream);
    // TODO: Bug 759040 - We should call HandleAsyncNotModified directly
    // here, to avoid event dispatching latency.
    return AsyncCall(&nsHttpChannel::HandleAsyncNotModified);
  }

  MOZ_ASSERT(mCacheInputStream);
  if (!mCacheInputStream) {
    NS_ERROR(
        "mCacheInputStream is null but we're expecting to "
        "be able to read from it.");
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIInputStream> inputStream = mCacheInputStream.forget();

  rv = nsInputStreamPump::Create(getter_AddRefs(mCachePump), inputStream, 0, 0,
                                 true);
  if (NS_FAILED(rv)) {
    inputStream->Close();
    return rv;
  }

  rv = mCachePump->AsyncRead(this);
  if (NS_FAILED(rv)) return rv;

  uint32_t suspendCount = mSuspendCount;
  if (LoadAsyncResumePending()) {
    LOG(
        ("  Suspend()'ing cache pump once because of async resume pending"
         ", sc=%u, pump=%p, this=%p",
         suspendCount, mCachePump.get(), this));
    ++suspendCount;
  }
  while (suspendCount--) {
    mCachePump->Suspend();
  }

  return NS_OK;
}

void nsHttpChannel::CloseCacheEntry(bool doomOnFailure) {
  mCacheInputStream.CloseAndRelease();

  if (!mCacheEntry) return;

  LOG(("nsHttpChannel::CloseCacheEntry [this=%p] mStatus=%" PRIx32
       " CacheEntryIsWriteOnly=%x",
       this, static_cast<uint32_t>(static_cast<nsresult>(mStatus)),
       LoadCacheEntryIsWriteOnly()));

  // If we have begun to create or replace a cache entry, and that cache
  // entry is not complete and not resumable, then it needs to be doomed.
  // Otherwise, CheckCache will make the mistake of thinking that the
  // partial cache entry is complete.

  bool doom = false;
  if (LoadInitedCacheEntry()) {
    MOZ_ASSERT(mResponseHead, "oops");
    if (NS_FAILED(mStatus) && doomOnFailure && LoadCacheEntryIsWriteOnly() &&
        !mResponseHead->IsResumable()) {
      doom = true;
    }
  } else if (LoadCacheEntryIsWriteOnly()) {
    doom = true;
  }

  if (doom) {
    LOG(("  dooming cache entry!!"));
    mCacheEntry->AsyncDoom(nullptr);
  } else {
    // Store updated security info, makes cached EV status race less likely
    // (see bug 1040086)
    if (mSecurityInfo) {
      mCacheEntry->SetSecurityInfo(mSecurityInfo);
    }
  }

  mCachedResponseHead = nullptr;

  mCachePump = nullptr;
  // This releases the entry for other consumers to use.
  // We call Dismiss() in case someone still keeps a reference
  // to this entry handle.
  mCacheEntry->Dismiss();
  mCacheEntry = nullptr;
  StoreCacheEntryIsWriteOnly(false);
  StoreInitedCacheEntry(false);
}

void nsHttpChannel::MaybeCreateCacheEntryWhenRCWN() {
  mozilla::MutexAutoLock lock(mRCWNLock);

  // Create cache entry for writing only when we're racing cache with network
  // and we don't have the entry because network won.
  if (mCacheEntry || !mRaceCacheWithNetwork ||
      mFirstResponseSource != RESPONSE_FROM_NETWORK ||
      LoadCacheEntryIsReadOnly()) {
    return;
  }

  LOG(("nsHttpChannel::MaybeCreateCacheEntryWhenRCWN [this=%p]", this));

  nsCOMPtr<nsICacheStorageService> cacheStorageService(
      components::CacheStorage::Service());
  if (!cacheStorageService) {
    return;
  }

  nsCOMPtr<nsICacheStorage> cacheStorage;
  RefPtr<LoadContextInfo> info = GetLoadContextInfo(this);
  Unused << cacheStorageService->DiskCacheStorage(info,
                                                  getter_AddRefs(cacheStorage));
  if (!cacheStorage) {
    return;
  }

  Unused << cacheStorage->OpenTruncate(mCacheEntryURI, mCacheIdExtension,
                                       getter_AddRefs(mCacheEntry));

  LOG(("  created entry %p", mCacheEntry.get()));

  if (AwaitingCacheCallbacks()) {
    // Setting mIgnoreCacheEntry to true ensures that we won't close this
    // write-only entry in OnCacheEntryAvailable() if this method was called
    // after OnCacheEntryCheck().
    mIgnoreCacheEntry = true;
  }

  mAvailableCachedAltDataType.Truncate();
  StoreDeliveringAltData(false);
  mAltDataLength = -1;
  mCacheInputStream.CloseAndRelease();
  StoreCachedContentIsValid(CachedContentValidity::Invalid);
}

// Initialize the cache entry for writing.
//  - finalize storage policy
//  - store security info
//  - update expiration time
//  - store headers and other meta data
nsresult nsHttpChannel::InitCacheEntry() {
  nsresult rv;

  NS_ENSURE_TRUE(mCacheEntry, NS_ERROR_UNEXPECTED);
  // if only reading, nothing to be done here.
  if (LoadCacheEntryIsReadOnly()) return NS_OK;

  // Don't cache the response again if already cached...
  if (CachedContentIsValid()) return NS_OK;

  LOG(("nsHttpChannel::InitCacheEntry [this=%p entry=%p]\n", this,
       mCacheEntry.get()));

  bool recreate = !LoadCacheEntryIsWriteOnly();
  bool dontPersist = mLoadFlags & INHIBIT_PERSISTENT_CACHING;

  if (!recreate && dontPersist) {
    // If the current entry is persistent but we inhibit peristence
    // then force recreation of the entry as memory/only.
    rv = mCacheEntry->GetPersistent(&recreate);
    if (NS_FAILED(rv)) return rv;
  }

  if (recreate) {
    LOG(
        ("  we have a ready entry, but reading it again from the server -> "
         "recreating cache entry\n"));
    // clean the altData cache and reset this to avoid wrong content length
    mAvailableCachedAltDataType.Truncate();
    StoreDeliveringAltData(false);

    nsCOMPtr<nsICacheEntry> currentEntry;
    currentEntry.swap(mCacheEntry);
    rv = currentEntry->Recreate(dontPersist, getter_AddRefs(mCacheEntry));
    if (NS_FAILED(rv)) {
      LOG(("  recreation failed, the response will not be cached"));
      return NS_OK;
    }

    StoreCacheEntryIsWriteOnly(true);
  }

  // Set the expiration time for this cache entry
  rv = UpdateExpirationTime();
  if (NS_FAILED(rv)) return rv;

  // mark this weakly framed until a response body is seen
  mCacheEntry->SetMetaDataElement("strongly-framed", "0");

  rv = AddCacheEntryHeaders(mCacheEntry);
  if (NS_FAILED(rv)) return rv;

  StoreInitedCacheEntry(true);

  // Don't perform the check when writing (doesn't make sense)
  StoreConcurrentCacheAccess(0);

  return NS_OK;
}

void nsHttpChannel::UpdateInhibitPersistentCachingFlag() {
  // The no-store directive within the 'Cache-Control:' header indicates
  // that we must not store the response in a persistent cache.
  if (mResponseHead->NoStore()) {
    mLoadFlags |= INHIBIT_PERSISTENT_CACHING;
    return;
  }

  if (!StaticPrefs::network_cache_persist_permanent_redirects_http() &&
      mURI->SchemeIs("http") &&
      nsHttp::IsPermanentRedirect(mResponseHead->Status())) {
    mLoadFlags |= INHIBIT_PERSISTENT_CACHING;
    return;
  }

  // Only cache SSL content on disk if the pref is set
  if (!gHttpHandler->IsPersistentHttpsCachingEnabled() &&
      mURI->SchemeIs("https")) {
    mLoadFlags |= INHIBIT_PERSISTENT_CACHING;
  }
}

nsresult DoAddCacheEntryHeaders(nsHttpChannel* self, nsICacheEntry* entry,
                                nsHttpRequestHead* requestHead,
                                nsHttpResponseHead* responseHead,
                                nsITransportSecurityInfo* securityInfo) {
  nsresult rv;

  LOG(("nsHttpChannel::AddCacheEntryHeaders [this=%p] begin", self));
  // Store secure data in memory only
  if (securityInfo) {
    entry->SetSecurityInfo(securityInfo);
  }

  // Store the HTTP request method with the cache entry so we can distinguish
  // for example GET and HEAD responses.
  nsAutoCString method;
  requestHead->Method(method);
  rv = entry->SetMetaDataElement("request-method", method.get());
  if (NS_FAILED(rv)) return rv;

  // Store the HTTP authorization scheme used if any...
  rv = StoreAuthorizationMetaData(entry, requestHead);
  if (NS_FAILED(rv)) return rv;

  // Iterate over the headers listed in the Vary response header, and
  // store the value of the corresponding request header so we can verify
  // that it has not varied when we try to re-use the cached response at
  // a later time.  Take care to store "Cookie" headers only as hashes
  // due to security considerations and the fact that they can be pretty
  // large (bug 468426). We take care of "Vary: cookie" in ResponseWouldVary.
  //
  // NOTE: if "Vary: accept, cookie", then we will store the "accept" header
  // in the cache.  we could try to avoid needlessly storing the "accept"
  // header in this case, but it doesn't seem worth the extra code to perform
  // the check.
  {
    nsAutoCString buf, metaKey;
    Unused << responseHead->GetHeader(nsHttp::Vary, buf);

    constexpr auto prefix = "request-"_ns;

    for (const nsACString& token :
         nsCCharSeparatedTokenizer(buf, NS_HTTP_HEADER_SEP).ToRange()) {
      LOG(
          ("nsHttpChannel::AddCacheEntryHeaders [this=%p] "
           "processing %s",
           self, nsPromiseFlatCString(token).get()));
      if (!token.EqualsLiteral("*")) {
        nsHttpAtom atom = nsHttp::ResolveAtom(token);
        nsAutoCString val;
        nsAutoCString hash;
        if (NS_SUCCEEDED(requestHead->GetHeader(atom, val))) {
          // If cookie-header, store a hash of the value
          if (atom == nsHttp::Cookie) {
            LOG(
                ("nsHttpChannel::AddCacheEntryHeaders [this=%p] "
                 "cookie-value %s",
                 self, val.get()));
            rv = Hash(val.get(), hash);
            // If hash failed, store a string not very likely
            // to be the result of subsequent hashes
            if (NS_FAILED(rv)) {
              val = "<hash failed>"_ns;
            } else {
              val = hash;
            }

            LOG(("   hashed to %s\n", val.get()));
          }

          // build cache meta data key and set meta data element...
          metaKey = prefix + token;
          entry->SetMetaDataElement(metaKey.get(), val.get());
        } else {
          LOG(
              ("nsHttpChannel::AddCacheEntryHeaders [this=%p] "
               "clearing metadata for %s",
               self, nsPromiseFlatCString(token).get()));
          metaKey = prefix + token;
          entry->SetMetaDataElement(metaKey.get(), nullptr);
        }
      }
    }
  }

  // Store the received HTTP head with the cache entry as an element of
  // the meta data.
  nsAutoCString head;
  responseHead->Flatten(head, true);
  rv = entry->SetMetaDataElement("response-head", head.get());
  if (NS_FAILED(rv)) return rv;
  head.Truncate();
  responseHead->FlattenNetworkOriginalHeaders(head);
  rv = entry->SetMetaDataElement("original-response-headers", head.get());
  if (NS_FAILED(rv)) return rv;

  // Indicate we have successfully finished setting metadata on the cache entry.
  rv = entry->MetaDataReady();

  return rv;
}

nsresult nsHttpChannel::AddCacheEntryHeaders(nsICacheEntry* entry) {
  return DoAddCacheEntryHeaders(this, entry, &mRequestHead, mResponseHead.get(),
                                mSecurityInfo);
}

inline void GetAuthType(const char* challenge, nsCString& authType) {
  const char* p;

  // get the challenge type
  if ((p = strchr(challenge, ' ')) != nullptr) {
    authType.Assign(challenge, p - challenge);
  } else {
    authType.Assign(challenge);
  }
}

nsresult StoreAuthorizationMetaData(nsICacheEntry* entry,
                                    nsHttpRequestHead* requestHead) {
  // Not applicable to proxy authorization...
  nsAutoCString val;
  if (NS_FAILED(requestHead->GetHeader(nsHttp::Authorization, val))) {
    return NS_OK;
  }

  // eg. [Basic realm="wally world"]
  nsAutoCString buf;
  GetAuthType(val.get(), buf);
  return entry->SetMetaDataElement("auth", buf.get());
}

// Finalize the cache entry
//  - may need to rewrite response headers if any headers changed
//  - may need to recalculate the expiration time if any headers changed
//  - called only for freshly written cache entries
nsresult nsHttpChannel::FinalizeCacheEntry() {
  LOG(("nsHttpChannel::FinalizeCacheEntry [this=%p]\n", this));

  // Don't update this meta-data on 304
  if (LoadStronglyFramed() && !CachedContentIsValid() && mCacheEntry) {
    LOG(("nsHttpChannel::FinalizeCacheEntry [this=%p] Is Strongly Framed\n",
         this));
    mCacheEntry->SetMetaDataElement("strongly-framed", "1");
  }

  if (mResponseHead && LoadResponseHeadersModified()) {
    // Set the expiration time for this cache entry
    nsresult rv = UpdateExpirationTime();
    if (NS_FAILED(rv)) return rv;
  }
  return NS_OK;
}

// Open an output stream to the cache entry and insert a listener tee into
// the chain of response listeners.
nsresult nsHttpChannel::InstallCacheListener(int64_t offset) {
  nsresult rv;

  LOG(("Preparing to write data into the cache [uri=%s]\n", mSpec.get()));

  MOZ_ASSERT(mCacheEntry);
  MOZ_ASSERT(LoadCacheEntryIsWriteOnly() || LoadCachedContentIsPartial() ||
             mRaceCacheWithNetwork);
  MOZ_ASSERT(mListener);

  nsAutoCString contentEncoding, contentType;
  Unused << mResponseHead->GetHeader(nsHttp::Content_Encoding, contentEncoding);
  mResponseHead->ContentType(contentType);
  // If the content is compressible and the server has not compressed it,
  // mark the cache entry for compression.
  if (contentEncoding.IsEmpty() &&
      (contentType.EqualsLiteral(TEXT_HTML) ||
       contentType.EqualsLiteral(TEXT_PLAIN) ||
       contentType.EqualsLiteral(TEXT_CSS) ||
       contentType.EqualsLiteral(TEXT_JAVASCRIPT) ||
       contentType.EqualsLiteral(TEXT_ECMASCRIPT) ||
       contentType.EqualsLiteral(TEXT_XML) ||
       contentType.EqualsLiteral(APPLICATION_JAVASCRIPT) ||
       contentType.EqualsLiteral(APPLICATION_ECMASCRIPT) ||
       contentType.EqualsLiteral(APPLICATION_XJAVASCRIPT) ||
       contentType.EqualsLiteral(APPLICATION_XHTML_XML))) {
    rv = mCacheEntry->SetMetaDataElement("uncompressed-len", "0");
    if (NS_FAILED(rv)) {
      LOG(("unable to mark cache entry for compression"));
    }
  }

  LOG(("Trading cache input stream for output stream [channel=%p]", this));

  // We must close the input stream first because cache entries do not
  // correctly handle having an output stream and input streams open at
  // the same time.
  mCacheInputStream.CloseAndRelease();

  int64_t predictedSize = mResponseHead->TotalEntitySize();
  if (predictedSize != -1) {
    predictedSize -= offset;
  }

  nsCOMPtr<nsIOutputStream> out;
  rv =
      mCacheEntry->OpenOutputStream(offset, predictedSize, getter_AddRefs(out));
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    LOG(("  entry doomed, not writing it [channel=%p]", this));
    // Entry is already doomed.
    // This may happen when expiration time is set to past and the entry
    // has been removed by the background eviction logic.
    return NS_OK;
  }
  if (rv == NS_ERROR_FILE_TOO_BIG) {
    LOG(("  entry would exceed max allowed size, not writing it [channel=%p]",
         this));
    mCacheEntry->AsyncDoom(nullptr);
    return NS_OK;
  }
  if (NS_FAILED(rv)) return rv;

  if (LoadCacheOnlyMetadata()) {
    LOG(("Not storing content, cacheOnlyMetadata set"));
    // We must open and then close the output stream of the cache entry.
    // This way we indicate the content has been written (despite with zero
    // length) and the entry is now in the ready state with "having data".

    out->Close();
    return NS_OK;
  }

  // XXX disk cache does not support overlapped i/o yet
#if 0
    // Mark entry valid inorder to allow simultaneous reading...
    rv = mCacheEntry->MarkValid();
    if (NS_FAILED(rv)) return rv;
#endif

  nsCOMPtr<nsIStreamListenerTee> tee =
      do_CreateInstance(kStreamListenerTeeCID, &rv);
  if (NS_FAILED(rv)) return rv;

  LOG(("nsHttpChannel::InstallCacheListener sync tee %p rv=%" PRIx32, tee.get(),
       static_cast<uint32_t>(rv)));
  rv = tee->Init(mListener, out, nullptr);
  if (NS_FAILED(rv)) return rv;

  mListener = tee;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <redirect>
//-----------------------------------------------------------------------------

nsresult nsHttpChannel::SetupReplacementChannel(nsIURI* newURI,
                                                nsIChannel* newChannel,
                                                bool preserveMethod,
                                                uint32_t redirectFlags) {
  LOG(
      ("nsHttpChannel::SetupReplacementChannel "
       "[this=%p newChannel=%p preserveMethod=%d]",
       this, newChannel, preserveMethod));

  if (!mEndMarkerAdded && profiler_thread_is_being_profiled_for_markers()) {
    mEndMarkerAdded = true;

    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    int32_t priority = PRIORITY_NORMAL;
    GetPriority(&priority);

    TimingStruct timings;
    if (mTransaction) {
      timings = mTransaction->Timings();
    }

    uint64_t size = 0;
    GetEncodedBodySize(&size);

    nsAutoCString contentType;
    mozilla::Maybe<mozilla::net::HttpVersion> httpVersion = Nothing();
    mozilla::Maybe<uint32_t> responseStatus = Nothing();
    if (mResponseHead) {
      mResponseHead->ContentType(contentType);
      httpVersion = Some(mResponseHead->Version());
      responseStatus = Some(mResponseHead->Status());
    }

    RefPtr<nsIIdentChannel> newIdentChannel = do_QueryObject(newChannel);
    uint64_t channelId = 0;
    if (newIdentChannel) {
      channelId = newIdentChannel->ChannelId();
    }
    profiler_add_network_marker(
        mURI, requestMethod, priority, mChannelId,
        NetworkLoadType::LOAD_REDIRECT, mLastStatusReported, TimeStamp::Now(),
        size, mCacheDisposition, mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(),
        mClassOfService.Flags(), mStatus, &timings, std::move(mSource),
        httpVersion, responseStatus,
        Some(nsDependentCString(contentType.get())), newURI, redirectFlags,
        channelId);
  }

  nsresult rv = HttpBaseChannel::SetupReplacementChannel(
      newURI, newChannel, preserveMethod, redirectFlags);
  if (NS_FAILED(rv)) return rv;

  nsAutoCString uriHost;
  mURI->GetAsciiHost(uriHost);
  // disable https-rr when encountering a downgrade from https to http.
  // If the host would have https-rr dns-entries, it would be misconfigured
  // due to giving us mixed signals:
  //   1. the signal to upgrade all http requests to https,
  //   2. but also downgrading to http on https via redirects.
  // Add to exclude list for that reason
  if (!gHttpHandler->IsHostExcludedForHTTPSRR(uriHost) &&
      nsHTTPSOnlyUtils::IsUpgradeDowngradeEndlessLoop(
          mURI, newURI, mLoadInfo,
          {nsHTTPSOnlyUtils::UpgradeDowngradeEndlessLoopOptions::
               EnforceForHTTPSRR})) {
    // Add the host to a excluded list because:
    // 1. We don't need to do the same check again.
    // 2. Other subresources in the same host will be also excluded.
    gHttpHandler->ExcludeHTTPSRRHost(uriHost);
    LOG(("[%p] skip HTTPS upgrade for host [%s]", this, uriHost.get()));
  }

  rv = CheckRedirectLimit(newURI, redirectFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // pass on the early hint observer to be able to process `103 Early Hints`
  // responses after cross origin redirects
  if (mEarlyHintObserver) {
    if (RefPtr<nsHttpChannel> httpChannelImpl = do_QueryObject(newChannel)) {
      httpChannelImpl->SetEarlyHintObserver(mEarlyHintObserver);
    }
    mEarlyHintObserver = nullptr;
  }

  // We don't support redirection for WebTransport for now.
  mWebTransportSessionEventListener = nullptr;

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(newChannel);
  if (!httpChannel) return NS_OK;  // no other options to set

  // convey the ApplyConversion flag (bug 91862)
  nsCOMPtr<nsIEncodedChannel> encodedChannel = do_QueryInterface(httpChannel);
  if (encodedChannel) encodedChannel->SetApplyConversion(LoadApplyConversion());

  // transfer the resume information
  if (LoadResuming()) {
    nsCOMPtr<nsIResumableChannel> resumableChannel(
        do_QueryInterface(newChannel));
    if (!resumableChannel) {
      NS_WARNING(
          "Got asked to resume, but redirected to non-resumable channel!");
      return NS_ERROR_NOT_RESUMABLE;
    }
    resumableChannel->ResumeAt(mStartPos, mEntityID);
  }

  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
      do_QueryInterface(newChannel, &rv);
  if (NS_SUCCEEDED(rv)) {
    TimeStamp timestamp;
    rv = GetNavigationStartTimeStamp(&timestamp);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    if (timestamp) {
      Unused << internalChannel->SetNavigationStartTimeStamp(timestamp);
    }
  }

  return NS_OK;
}

nsresult nsHttpChannel::AsyncProcessRedirection(uint32_t redirectType) {
  LOG(("nsHttpChannel::AsyncProcessRedirection [this=%p type=%u]\n", this,
       redirectType));

  nsresult rv = ProcessCrossOriginSecurityHeaders();
  if (NS_FAILED(rv)) {
    mStatus = rv;
    HandleAsyncAbort();
    return rv;
  }

  nsAutoCString location;

  // if a location header was not given, then we can't perform the redirect,
  // so just carry on as though this were a normal response.
  if (NS_FAILED(mResponseHead->GetHeader(nsHttp::Location, location))) {
    return NS_ERROR_FAILURE;
  }

  // If we were told to not follow redirects automatically, then again
  // carry on as though this were a normal response.
  if (mLoadInfo->GetDontFollowRedirects()) {
    return NS_ERROR_FAILURE;
  }

  // make sure non-ASCII characters in the location header are escaped.
  nsAutoCString locationBuf;
  if (NS_EscapeURL(location.get(), -1, esc_OnlyNonASCII | esc_Spaces,
                   locationBuf)) {
    location = locationBuf;
  }

  mRedirectType = redirectType;

  LOG(("redirecting to: %s [redirection-limit=%u]\n", location.get(),
       uint32_t(mRedirectionLimit)));

  rv = CreateNewURI(location.get(), getter_AddRefs(mRedirectURI));

  if (NS_FAILED(rv)) {
    LOG(("Invalid URI for redirect: Location: %s\n", location.get()));
    return NS_ERROR_CORRUPTED_CONTENT;
  }

  if (!StaticPrefs::network_allow_redirect_to_data() &&
      !mLoadInfo->GetAllowInsecureRedirectToDataURI() &&
      mRedirectURI->SchemeIs("data")) {
    LOG(("Invalid data URI for redirect!"));
    nsContentSecurityManager::ReportBlockedDataURI(mRedirectURI, mLoadInfo,
                                                   true);
    return NS_ERROR_DOM_BAD_URI;
  }

  // Perform the URL query string stripping for redirects. We will only strip
  // the query string if it is redirecting to a third-party URI in the top
  // level.
  if (StaticPrefs::privacy_query_stripping_redirect()) {
    ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
    bool isThirdPartyRedirectURI = true;
    thirdPartyUtil->IsThirdPartyURI(mURI, mRedirectURI,
                                    &isThirdPartyRedirectURI);
    if (isThirdPartyRedirectURI && mLoadInfo->GetExternalContentPolicyType() ==
                                       ExtContentPolicy::TYPE_DOCUMENT) {
      glean::contentblocking::query_stripping_count
          .EnumGet(glean::contentblocking::QueryStrippingCountLabel::eRedirect)
          .Add();

      nsCOMPtr<nsIPrincipal> prin;
      ContentBlockingAllowList::RecomputePrincipal(
          mRedirectURI, mLoadInfo->GetOriginAttributes(), getter_AddRefs(prin));

      bool isRedirectURIInAllowList = false;
      if (prin) {
        ContentBlockingAllowList::Check(prin, mPrivateBrowsing,
                                        isRedirectURIInAllowList);
      }

      if (!isRedirectURIInAllowList) {
        nsCOMPtr<nsIURI> strippedURI;

        nsCOMPtr<nsIURLQueryStringStripper> queryStripper;
        queryStripper =
            mozilla::components::URLQueryStringStripper::Service(&rv);
        NS_ENSURE_SUCCESS(rv, rv);

        uint32_t numStripped;

        rv = queryStripper->Strip(mRedirectURI, mPrivateBrowsing,
                                  getter_AddRefs(strippedURI), &numStripped);
        NS_ENSURE_SUCCESS(rv, rv);

        if (numStripped) {
          mUnstrippedRedirectURI = mRedirectURI;
          mRedirectURI = strippedURI;

          // Record telemetry, but only if we stripped any query params.
          glean::contentblocking::query_stripping_count
              .EnumGet(glean::contentblocking::QueryStrippingCountLabel::
                           eStripforredirect)
              .Add();
          glean::contentblocking::query_stripping_param_count
              .AccumulateSingleSample(numStripped);
        }
      }
    }
  }

  // if we have a Set-Login header, we should try to handle it here
  nsAutoCString setLogin;
  if (NS_SUCCEEDED(mResponseHead->GetHeader(nsHttp::Set_Login, setLogin))) {
    bool isDocument = mLoadInfo->GetExternalContentPolicyType() ==
                      ExtContentPolicy::TYPE_DOCUMENT;
    if (isDocument) {
      auto ssm = nsContentUtils::GetSecurityManager();
      if (ssm) {
        nsCOMPtr<nsIPrincipal> documentPrincipal;
        nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
            this, getter_AddRefs(documentPrincipal));
        dom::NavigatorLogin::SetLoginStatus(documentPrincipal, setLogin);
      }
    } else {
      bool inThirdPartyContext = mLoadInfo->GetIsInThirdPartyContext();
      nsIPrincipal* loadingPrincipal = mLoadInfo->GetLoadingPrincipal();
      if (loadingPrincipal) {
        bool isSameOriginToLoadingPrincipal =
            loadingPrincipal->IsSameOrigin(mURI);
        if (!inThirdPartyContext && isSameOriginToLoadingPrincipal) {
          dom::NavigatorLogin::SetLoginStatus(loadingPrincipal, setLogin);
        }
      }
    }
  }

  if (NS_WARN_IF(!mRedirectURI)) {
    LOG(("Invalid redirect URI after performaing query string stripping"));
    return NS_ERROR_FAILURE;
  }

  return ContinueProcessRedirectionAfterFallback(NS_OK);
}

nsresult nsHttpChannel::ContinueProcessRedirectionAfterFallback(nsresult rv) {
  // Kill the current cache entry if we are redirecting
  // back to ourself.
  bool redirectingBackToSameURI = false;
  if (mCacheEntry && LoadCacheEntryIsWriteOnly() &&
      NS_SUCCEEDED(mURI->Equals(mRedirectURI, &redirectingBackToSameURI)) &&
      redirectingBackToSameURI) {
    mCacheEntry->AsyncDoom(nullptr);
  }

  // move the reference of the old location to the new one if the new
  // one has none.
  PropagateReferenceIfNeeded(mURI, mRedirectURI);

  bool rewriteToGET =
      ShouldRewriteRedirectToGET(mRedirectType, mRequestHead.ParsedMethod());

  // prompt if the method is not safe (such as POST, PUT, DELETE, ...)
  if (!rewriteToGET && !mRequestHead.IsSafeMethod()) {
    rv = PromptTempRedirect();
    if (NS_FAILED(rv)) return rv;
  }

  uint32_t redirectFlags;
  if (nsHttp::IsPermanentRedirect(mRedirectType)) {
    redirectFlags = nsIChannelEventSink::REDIRECT_PERMANENT;
  } else {
    redirectFlags = nsIChannelEventSink::REDIRECT_TEMPORARY;
  }

  nsCOMPtr<nsIIOService> ioService;
  rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIChannel> newChannel;
  nsCOMPtr<nsILoadInfo> redirectLoadInfo =
      CloneLoadInfoForRedirect(mRedirectURI, redirectFlags);

  // Propagate the unstripped redirect URI.
  redirectLoadInfo->SetUnstrippedURI(mUnstrippedRedirectURI);

  rv = NS_NewChannelInternal(getter_AddRefs(newChannel), mRedirectURI,
                             redirectLoadInfo,
                             nullptr,  // PerformanceStorage
                             nullptr,  // aLoadGroup
                             nullptr,  // aCallbacks
                             nsIRequest::LOAD_NORMAL, ioService);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetupReplacementChannel(mRedirectURI, newChannel, !rewriteToGET,
                               redirectFlags);
  if (NS_FAILED(rv)) return rv;

  // verify that this is a legal redirect
  mRedirectChannel = newChannel;

  PushRedirectAsyncFunc(&nsHttpChannel::ContinueProcessRedirection);
  rv = gHttpHandler->AsyncOnChannelRedirect(this, newChannel, redirectFlags);

  if (NS_SUCCEEDED(rv)) rv = WaitForRedirectCallback();

  if (NS_FAILED(rv)) {
    AutoRedirectVetoNotifier notifier(this, rv);
    PopRedirectAsyncFunc(&nsHttpChannel::ContinueProcessRedirection);
  }

  return rv;
}

nsresult nsHttpChannel::ContinueProcessRedirection(nsresult rv) {
  AutoRedirectVetoNotifier notifier(this, rv);

  LOG(("nsHttpChannel::ContinueProcessRedirection [rv=%" PRIx32 ",this=%p]\n",
       static_cast<uint32_t>(rv), this));
  if (NS_FAILED(rv)) return rv;

  MOZ_ASSERT(mRedirectChannel, "No redirect channel?");

  // Make sure to do this after we received redirect veto answer,
  // i.e. after all sinks had been notified
  mRedirectChannel->SetOriginalURI(mOriginalURI);

  // XXX we used to talk directly with the script security manager, but that
  // should really be handled by the event sink implementation.

  // begin loading the new channel
  rv = mRedirectChannel->AsyncOpen(mListener);
  LOG(("  new channel AsyncOpen returned %" PRIX32, static_cast<uint32_t>(rv)));
  NS_ENSURE_SUCCESS(rv, rv);

  // close down this channel
  Cancel(NS_BINDING_REDIRECTED);

  notifier.RedirectSucceeded();

  ReleaseListeners();

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel <auth>
//-----------------------------------------------------------------------------

NS_IMETHODIMP nsHttpChannel::OnAuthAvailable() {
  LOG(("nsHttpChannel::OnAuthAvailable [this=%p]", this));

  // setting mAuthRetryPending flag and resuming the transaction
  // triggers process of throwing away the unauthenticated data already
  // coming from the network
  mIsAuthChannel = true;
  mAuthRetryPending = true;
  StoreProxyAuthPending(false);
  LOG(("Resuming the transaction, we got credentials from user"));
  if (mTransactionPump) {
    mTransactionPump->Resume();
  }

  if (StaticPrefs::network_auth_use_redirect_for_retries()) {
    return CallOrWaitForResume(
        [](auto* self) { return self->RedirectToNewChannelForAuthRetry(); });
  }

  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::OnAuthCancelled(bool userCancel) {
  LOG(("nsHttpChannel::OnAuthCancelled [this=%p]", this));
  MOZ_ASSERT(mAuthRetryPending, "OnAuthCancelled should not be called twice");

  if (mTransactionPump) {
    // If the channel is trying to authenticate to a proxy and
    // that was canceled we cannot show the http response body
    // from the 40x as that might mislead the user into thinking
    // it was a end host response instead of a proxy reponse.
    // This must check explicitly whether a proxy auth was being done
    // because we do want to show the content if this is an error from
    // the origin server.
    if (LoadProxyAuthPending()) Cancel(NS_ERROR_PROXY_CONNECTION_REFUSED);

    // Make sure to process security headers before calling CallOnStartRequest.
    nsresult rv = ProcessCrossOriginSecurityHeaders();
    if (NS_FAILED(rv)) {
      mStatus = rv;
      HandleAsyncAbort();
      return rv;
    }

    // ensure call of OnStartRequest of the current listener here,
    // it would not be called otherwise at all
    rv = CallOnStartRequest();

    // drop mAuthRetryPending flag and resume the transaction
    // this resumes load of the unauthenticated content data (which
    // may have been canceled if we don't want to show it)
    mAuthRetryPending = false;
    LOG(("Resuming the transaction, user cancelled the auth dialog"));
    mTransactionPump->Resume();

    if (NS_FAILED(rv)) mTransactionPump->Cancel(rv);
  }

  StoreProxyAuthPending(false);
  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::CloseStickyConnection() {
  LOG(("nsHttpChannel::CloseStickyConnection this=%p", this));

  // Require we are between OnStartRequest and OnStopRequest, because
  // what we do here takes effect in OnStopRequest (not reusing the
  // connection for next authentication round).
  if (!LoadIsPending()) {
    LOG(("  channel not pending"));
    NS_ERROR(
        "CloseStickyConnection not called before OnStopRequest, won't have any "
        "effect");
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(mTransaction);
  if (!mTransaction) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!(mCaps & NS_HTTP_STICKY_CONNECTION ||
        mTransaction->HasStickyConnection())) {
    LOG(("  not sticky"));
    return NS_OK;
  }

  mTransaction->DontReuseConnection();
  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::ConnectionRestartable(bool aRestartable) {
  LOG(("nsHttpChannel::ConnectionRestartable this=%p, restartable=%d", this,
       aRestartable));
  StoreAuthConnectionRestartable(aRestartable);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF_INHERITED(nsHttpChannel, HttpBaseChannel)
NS_IMPL_RELEASE_INHERITED(nsHttpChannel, HttpBaseChannel)

NS_INTERFACE_MAP_BEGIN(nsHttpChannel)
  NS_INTERFACE_MAP_ENTRY(nsIRequest)
  NS_INTERFACE_MAP_ENTRY(nsIChannel)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIHttpChannel)
  NS_INTERFACE_MAP_ENTRY(nsICacheInfoChannel)
  NS_INTERFACE_MAP_ENTRY(nsICachingChannel)
  NS_INTERFACE_MAP_ENTRY(nsIClassOfService)
  NS_INTERFACE_MAP_ENTRY(nsIUploadChannel)
  NS_INTERFACE_MAP_ENTRY(nsIFormPOSTActionChannel)
  NS_INTERFACE_MAP_ENTRY(nsIUploadChannel2)
  NS_INTERFACE_MAP_ENTRY(nsICacheEntryOpenCallback)
  NS_INTERFACE_MAP_ENTRY(nsIHttpChannelInternal)
  NS_INTERFACE_MAP_ENTRY(nsIResumableChannel)
  NS_INTERFACE_MAP_ENTRY(nsITransportEventSink)
  NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
  NS_INTERFACE_MAP_ENTRY(nsIProtocolProxyCallback)
  NS_INTERFACE_MAP_ENTRY(nsIProxiedChannel)
  NS_INTERFACE_MAP_ENTRY(nsIHttpAuthenticableChannel)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncVerifyRedirectCallback)
  NS_INTERFACE_MAP_ENTRY(nsIThreadRetargetableRequest)
  NS_INTERFACE_MAP_ENTRY(nsIThreadRetargetableStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIDNSListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsICorsPreflightCallback)
  NS_INTERFACE_MAP_ENTRY(nsIRaceCacheWithNetwork)
  NS_INTERFACE_MAP_ENTRY(nsIRequestTailUnblockCallback)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsHttpChannel)
  NS_INTERFACE_MAP_ENTRY(nsIEarlyHintObserver)
NS_INTERFACE_MAP_END_INHERITING(HttpBaseChannel)

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIRequest
//-----------------------------------------------------------------------------

NS_IMETHODIMP nsHttpChannel::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsHttpChannel::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP
nsHttpChannel::CancelWithReason(nsresult aStatus, const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
nsHttpChannel::Cancel(nsresult status) {
  MOZ_ASSERT(NS_IsMainThread());
  // We should never have a pump open while a CORS preflight is in progress.
  MOZ_ASSERT_IF(mPreflightChannel, !mCachePump);
#ifdef DEBUG
  // We want to perform this check only when the chanel is being cancelled the
  // first time with a URL classifier blocking error code.  If mStatus is
  // already set to such an error code then Cancel() may be called for some
  // other reason, for example because we've received notification about our
  // parent process side channel being canceled, in which case we cannot expect
  // that CancelByURLClassifier() would have handled this case.
  if (UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(status) &&
      !UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(mStatus)) {
    MOZ_CRASH_UNSAFE_PRINTF("Blocking classifier error %" PRIx32
                            " need to be handled by CancelByURLClassifier()",
                            static_cast<uint32_t>(status));
  }
#endif

  LOG(("nsHttpChannel::Cancel [this=%p status=%" PRIx32 ", reason=%s]\n", this,
       static_cast<uint32_t>(status), mCanceledReason.get()));
  MOZ_ASSERT_IF(!(mConnectionInfo && mConnectionInfo->UsingConnect()) &&
                    NS_SUCCEEDED(mStatus),
                !AllowedErrorForHTTPSRRFallback(status));

  mEarlyHintObserver = nullptr;
  mWebTransportSessionEventListener = nullptr;

  if (mCanceled) {
    LOG(("  ignoring; already canceled\n"));
    return NS_OK;
  }

  LogCallingScriptLocation(this);

  if (LoadWaitingForRedirectCallback()) {
    LOG(("channel canceled during wait for redirect callback"));
  }

  return CancelInternal(status);
}

NS_IMETHODIMP
nsHttpChannel::CancelByURLClassifier(nsresult aErrorCode) {
  MOZ_ASSERT(
      UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(aErrorCode));
  MOZ_ASSERT(NS_IsMainThread());
  // We should never have a pump open while a CORS preflight is in progress.
  MOZ_ASSERT_IF(mPreflightChannel, !mCachePump);

  LOG(("nsHttpChannel::CancelByURLClassifier [this=%p]\n", this));

  if (mCanceled) {
    LOG(("  ignoring; already canceled\n"));
    return NS_OK;
  }

  // We are being canceled by the channel classifier because of tracking
  // protection, but we haven't yet had a chance to dispatch the
  // "http-on-modify-request" notifications yet (this would normally be
  // done in PrepareToConnect()).  So do that now, before proceeding to
  // cancel.
  //
  // Note that running these observers can itself result in the channel
  // being canceled.  In that case, we accept that cancelation code as
  // the cause of the cancelation, as if the classification of the channel
  // would have occurred past this point!

  // notify "http-on-modify-request" observers
  CallOnModifyRequestObservers();

  // Check if request was cancelled during on-modify-request
  if (mCanceled) {
    return mStatus;
  }

  if (mSuspendCount) {
    LOG(("Waiting until resume in Cancel [this=%p]\n", this));
    MOZ_ASSERT(!mCallOnResume);
    StoreChannelClassifierCancellationPending(1);
    mCallOnResume = [aErrorCode](nsHttpChannel* self) {
      self->HandleContinueCancellingByURLClassifier(aErrorCode);
      return NS_OK;
    };
    return NS_OK;
  }

  // Check to see if we should redirect this channel elsewhere by
  // nsIHttpChannel.redirectTo API request
  if (mAPIRedirectTo) {
    StoreChannelClassifierCancellationPending(1);
    return AsyncCall(&nsHttpChannel::HandleAsyncAPIRedirect);
  }

  return CancelInternal(aErrorCode);
}

void nsHttpChannel::ContinueCancellingByURLClassifier(nsresult aErrorCode) {
  MOZ_ASSERT(
      UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(aErrorCode));
  MOZ_ASSERT(NS_IsMainThread());
  // We should never have a pump open while a CORS preflight is in progress.
  MOZ_ASSERT_IF(mPreflightChannel, !mCachePump);

  LOG(("nsHttpChannel::ContinueCancellingByURLClassifier [this=%p]\n", this));
  if (mCanceled) {
    LOG(("  ignoring; already canceled\n"));
    return;
  }

  // Check to see if we should redirect this channel elsewhere by
  // nsIHttpChannel.redirectTo API request
  if (mAPIRedirectTo) {
    Unused << AsyncCall(&nsHttpChannel::HandleAsyncAPIRedirect);
    return;
  }

  Unused << CancelInternal(aErrorCode);
}

nsresult nsHttpChannel::CancelInternal(nsresult status) {
  LOG(("nsHttpChannel::CancelInternal [this=%p]\n", this));
  bool channelClassifierCancellationPending =
      !!LoadChannelClassifierCancellationPending();
  if (UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(status)) {
    StoreChannelClassifierCancellationPending(0);
  }

  mEarlyHintObserver = nullptr;
  mWebTransportSessionEventListener = nullptr;
  mCanceled = true;
  mStatus = NS_FAILED(status) ? status : NS_ERROR_ABORT;

  if (StaticPrefs::network_http_network_error_logging_enabled() &&
      LoadUsedNetwork() && !mReportedNEL) {
    if (nsCOMPtr<nsINetworkErrorLogging> nel =
            components::NetworkErrorLogging::Service()) {
      nel->GenerateNELReport(this);
    }
    mReportedNEL = true;
  }

  // We don't want the content process to see any header values
  // when the request is blocked by ORB
  if (mChannelBlockedByOpaqueResponse && mCachedOpaqueResponseBlockingPref) {
    mResponseHead->ClearHeaders();
  }

  if (mLastStatusReported && !mEndMarkerAdded &&
      profiler_thread_is_being_profiled_for_markers()) {
    // These do allocations/frees/etc; avoid if not active
    // mLastStatusReported can be null if Cancel is called before we added the
    // start marker.
    mEndMarkerAdded = true;

    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    int32_t priority = PRIORITY_NORMAL;
    GetPriority(&priority);

    uint64_t size = 0;
    GetEncodedBodySize(&size);

    profiler_add_network_marker(
        mURI, requestMethod, priority, mChannelId, NetworkLoadType::LOAD_CANCEL,
        mLastStatusReported, TimeStamp::Now(), size, mCacheDisposition,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(),
        mClassOfService.Flags(), mStatus, &mTransactionTimings,
        std::move(mSource));
  }

  // If we don't have mTransactionPump and mCachePump, we need to call
  // AsyncAbort to make sure this channel's listener got notified.
  bool needAsyncAbort = !mTransactionPump && !mCachePump;

  if (mProxyRequest) mProxyRequest->Cancel(status);
  CancelNetworkRequest(status);
  mCacheInputStream.CloseAndRelease();
  if (mCachePump) mCachePump->Cancel(status);
  if (mAuthProvider) mAuthProvider->Cancel(status);
  if (mPreflightChannel) mPreflightChannel->Cancel(status);
  if (mRequestContext && mOnTailUnblock) {
    mOnTailUnblock = nullptr;
    mRequestContext->CancelTailedRequest(this);
    CloseCacheEntry(false);
    needAsyncAbort = false;
    Unused << AsyncAbort(status);
  } else if (channelClassifierCancellationPending) {
    // If mCallOnResume is not null here, it's set in
    // nsHttpChannel::CancelByURLClassifier. We can override mCallOnResume since
    // mCanceled is true and nsHttpChannel::ContinueCancellingByURLClassifier
    // does nothing.
    if (mCallOnResume) {
      mCallOnResume = nullptr;
    }
    // If we're coming from an asynchronous path when canceling a channel due
    // to safe-browsing protection, we need to AsyncAbort the channel now.
    needAsyncAbort = false;
    Unused << AsyncAbort(status);
  }

  // If we already have mCallOnResume, AsyncAbort will be called in
  // ResumeInternal.
  if (needAsyncAbort && !mCallOnResume && !mSuspendCount) {
    LOG(("nsHttpChannel::CancelInternal do AsyncAbort [this=%p]\n", this));
    CloseCacheEntry(false);
    Unused << AsyncAbort(status);
  }
  return NS_OK;
}

void nsHttpChannel::CancelNetworkRequest(nsresult aStatus) {
  if (mTransaction) {
    nsresult rv = gHttpHandler->CancelTransaction(mTransaction, aStatus);
    if (NS_FAILED(rv)) {
      LOG(("failed to cancel the transaction\n"));
    }
  }
  if (mTransactionPump) mTransactionPump->Cancel(aStatus);

  mEarlyHintObserver = nullptr;
  mWebTransportSessionEventListener = nullptr;
}

NS_IMETHODIMP
nsHttpChannel::Suspend() {
  NS_ENSURE_TRUE(LoadIsPending(), NS_ERROR_NOT_AVAILABLE);

  LOG(("nsHttpChannel::SuspendInternal [this=%p]\n", this));
  LogCallingScriptLocation(this);

  ++mSuspendCount;

  if (mSuspendCount == 1) {
    mSuspendTimestamp = TimeStamp::NowLoRes();
  }

  nsresult rvTransaction = NS_OK;
  if (mTransactionPump) {
    rvTransaction = mTransactionPump->Suspend();
  }
  nsresult rvCache = NS_OK;
  if (mCachePump) {
    rvCache = mCachePump->Suspend();
  }

  return NS_FAILED(rvTransaction) ? rvTransaction : rvCache;
}

NS_IMETHODIMP
nsHttpChannel::Resume() {
  NS_ENSURE_TRUE(mSuspendCount > 0, NS_ERROR_UNEXPECTED);

  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::Resume", NETWORK,
                            Flow::FromPointer(this));
  LOG(("nsHttpChannel::ResumeInternal [this=%p]\n", this));
  LogCallingScriptLocation(this);

  if (--mSuspendCount == 0) {
    mSuspendTotalTime += TimeStamp::NowLoRes() - mSuspendTimestamp;

    if (mCallOnResume) {
      // Resume the interrupted procedure first, then resume
      // the pump to continue process the input stream.
      // Any newly created pump MUST be suspended to prevent calling
      // its OnStartRequest before OnStopRequest of any pre-existing
      // pump.  AsyncResumePending ensures that.
      MOZ_ASSERT(!LoadAsyncResumePending());
      StoreAsyncResumePending(1);

      std::function<nsresult(nsHttpChannel*)> callOnResume = nullptr;
      std::swap(callOnResume, mCallOnResume);

      RefPtr<nsHttpChannel> self(this);
      nsCOMPtr<nsIRequest> transactionPump = mTransactionPump;
      RefPtr<nsInputStreamPump> cachePump = mCachePump;

      nsresult rv = NS_DispatchToCurrentThread(NS_NewRunnableFunction(
          "nsHttpChannel::CallOnResume",
          [callOnResume{std::move(callOnResume)}, self{std::move(self)},
           transactionPump{std::move(transactionPump)},
           cachePump{std::move(cachePump)}]() {
            MOZ_ASSERT(self->LoadAsyncResumePending());
            nsresult rv = self->CallOrWaitForResume(callOnResume);
            if (NS_FAILED(rv)) {
              self->CloseCacheEntry(false);
              Unused << self->AsyncAbort(rv);
            }
            MOZ_ASSERT(self->LoadAsyncResumePending());

            self->StoreAsyncResumePending(0);

            // And now actually resume the previously existing pumps.
            if (transactionPump) {
              LOG(
                  ("nsHttpChannel::CallOnResume resuming previous transaction "
                   "pump %p, this=%p",
                   transactionPump.get(), self.get()));
              transactionPump->Resume();
            }
            if (cachePump) {
              LOG(
                  ("nsHttpChannel::CallOnResume resuming previous cache pump "
                   "%p, this=%p",
                   cachePump.get(), self.get()));
              cachePump->Resume();
            }

            // Any newly created pumps were suspended once because of
            // AsyncResumePending. Problem is that the stream listener
            // notification is already pending in the queue right now, because
            // AsyncRead doesn't (regardless if called after Suspend) respect
            // the suspend coutner and the right order would not be preserved.
            // Hence, we do another dispatch round to actually Resume after
            // the notification from the original pump.
            if (transactionPump != self->mTransactionPump &&
                self->mTransactionPump) {
              LOG(
                  ("nsHttpChannel::CallOnResume async-resuming new "
                   "transaction "
                   "pump %p, this=%p",
                   self->mTransactionPump.get(), self.get()));

              nsCOMPtr<nsIRequest> pump = self->mTransactionPump;
              NS_DispatchToCurrentThread(NS_NewRunnableFunction(
                  "nsHttpChannel::CallOnResume new transaction",
                  [pump{std::move(pump)}]() { pump->Resume(); }));
            }
            if (cachePump != self->mCachePump && self->mCachePump) {
              LOG(
                  ("nsHttpChannel::CallOnResume async-resuming new cache pump "
                   "%p, this=%p",
                   self->mCachePump.get(), self.get()));

              RefPtr<nsInputStreamPump> pump = self->mCachePump;
              NS_DispatchToCurrentThread(NS_NewRunnableFunction(
                  "nsHttpChannel::CallOnResume new pump",
                  [pump{std::move(pump)}]() { pump->Resume(); }));
            }
          }));
      NS_ENSURE_SUCCESS(rv, rv);
      return rv;
    }
  }

  nsresult rvTransaction = NS_OK;
  if (mTransactionPump) {
    rvTransaction = mTransactionPump->Resume();
  }

  nsresult rvCache = NS_OK;
  if (mCachePump) {
    rvCache = mCachePump->Resume();
  }

  return NS_FAILED(rvTransaction) ? rvTransaction : rvCache;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetSecurityInfo(nsITransportSecurityInfo** securityInfo) {
  NS_ENSURE_ARG_POINTER(securityInfo);
  *securityInfo = do_AddRef(mSecurityInfo).take();
  return NS_OK;
}

// If any of the functions that AsyncOpen calls returns immediately an error
// AsyncAbort(which calls onStart/onStopRequest) does not need to be call.
// To be sure that they are not call ReleaseListeners() is called.
// If AsyncOpen returns NS_OK, after that point AsyncAbort must be called on
// any error.
NS_IMETHODIMP
nsHttpChannel::AsyncOpen(nsIStreamListener* aListener) {
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::AsyncOpen", NETWORK,
                            Flow::FromPointer(this));
  nsCOMPtr<nsIStreamListener> listener = aListener;
  nsresult rv =
      nsContentSecurityManager::doContentSecurityCheck(this, listener);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    ReleaseListeners();
    return rv;
  }

  MOZ_ASSERT(
      mLoadInfo->GetSecurityMode() == 0 ||
          mLoadInfo->GetInitialSecurityCheckDone() ||
          (mLoadInfo->GetSecurityMode() ==
               nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL &&
           mLoadInfo->GetLoadingPrincipal() &&
           mLoadInfo->GetLoadingPrincipal()->IsSystemPrincipal()),
      "security flags in loadInfo but doContentSecurityCheck() not called");

  LOG(("nsHttpChannel::AsyncOpen [this=%p]\n", this));
  mOpenerCallingScriptLocation = CallingScriptLocationString();
  LogCallingScriptLocation(this, mOpenerCallingScriptLocation);
  NS_CompareLoadInfoAndLoadContext(this);

#ifdef DEBUG
  AssertPrivateBrowsingId();
#endif

  NS_ENSURE_ARG_POINTER(listener);
  NS_ENSURE_TRUE(!LoadIsPending(), NS_ERROR_IN_PROGRESS);
  NS_ENSURE_TRUE(!LoadWasOpened(), NS_ERROR_ALREADY_OPENED);

  if (mCanceled) {
    ReleaseListeners();
    return NS_FAILED(mStatus) ? mStatus : NS_ERROR_FAILURE;
  }

  if (MaybeWaitForUploadStreamNormalization(listener, nullptr)) {
    return NS_OK;
  }

  MOZ_ASSERT(NS_IsMainThread());

  if (!gHttpHandler->Active()) {
    LOG(("  after HTTP shutdown..."));
    ReleaseListeners();
    return NS_ERROR_NOT_AVAILABLE;
  }

  rv = NS_CheckPortSafety(mURI);
  if (NS_FAILED(rv)) {
    ReleaseListeners();
    return rv;
  }

  // If no one called SetLoadGroup or SetNotificationCallbacks, the private
  // state has not been updated on PrivateBrowsingChannel (which we derive
  // from) Same if the loadinfo has changed since the creation of the channel.
  // Hence, we have to call UpdatePrivateBrowsing() here
  UpdatePrivateBrowsing();

  AntiTrackingUtils::UpdateAntiTrackingInfoForChannel(this);

  // Recalculate the default userAgent header after the AntiTrackingInfo gets
  // updated because we can only know whether the site is exempted from
  // fingerprinting protection after we have the AntiTracking Info.
  //
  // Note that we don't recalculate the header if it has been modified since the
  // channel was created because we want to preserve the modified header.
  if (!LoadIsUserAgentHeaderModified()) {
    rv = mRequestHead.SetHeader(
        nsHttp::User_Agent,
        gHttpHandler->UserAgent(nsContentUtils::ShouldResistFingerprinting(
            this, RFPTarget::HttpUserAgent)),
        false, nsHttpHeaderArray::eVarietyRequestEnforceDefault);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  if (WaitingForTailUnblock()) {
    // This channel is marked as Tail and is part of a request context
    // that has positive number of non-tailed requestst, hence this channel
    // has been put to a queue.
    // When tail is unblocked, OnTailUnblock on this channel will be called
    // to continue AsyncOpen.
    mListener = listener;
    MOZ_DIAGNOSTIC_ASSERT(!mOnTailUnblock);
    mOnTailUnblock = &nsHttpChannel::AsyncOpenOnTailUnblock;

    LOG(("  put on hold until tail is unblocked"));
    return NS_OK;
  }

  // Remember the cookie header that was set, if any
  nsAutoCString cookieHeader;
  if (NS_SUCCEEDED(mRequestHead.GetHeader(nsHttp::Cookie, cookieHeader))) {
    mUserSetCookieHeader = cookieHeader;
  }

  // Set user agent override, do so before OnOpeningRequest notification
  // since we want to allow consumers of that notification change or remove
  // the User-Agent request header.
  HttpBaseChannel::SetDocshellUserAgentOverride();

  // After we notify any observers (on-opening-request, loadGroup, etc) we
  // must return NS_OK and return any errors asynchronously via
  // OnStart/OnStopRequest.  Observers may add a reference to the channel
  // and expect to get OnStopRequest so they know when to drop the reference,
  // etc.

  // notify "http-on-opening-request" observers, but not if this is a redirect
  if (!(mLoadFlags & LOAD_REPLACE)) {
    gHttpHandler->OnOpeningRequest(this);
  }

  StoreIsPending(true);
  StoreWasOpened(true);

  mListener = listener;

  if (nsIOService::UseSocketProcess() &&
      !gIOService->IsSocketProcessLaunchComplete()) {
    RefPtr<nsHttpChannel> self = this;
    gIOService->CallOrWaitForSocketProcess(
        [self]() { self->AsyncOpenFinal(TimeStamp::Now()); });
    return NS_OK;
  }

  AsyncOpenFinal(TimeStamp::Now());

  return NS_OK;
}

void nsHttpChannel::AsyncOpenFinal(TimeStamp aTimeStamp) {
  // We save this timestamp from outside of the if block in case we enable the
  // profiler after AsyncOpen().
  mLastStatusReported = TimeStamp::Now();
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::AsyncOpenFinal", NETWORK,
                            Flow::FromPointer(this));
  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    profiler_add_network_marker(
        mURI, requestMethod, mPriority, mChannelId, NetworkLoadType::LOAD_START,
        mChannelCreationTimestamp, mLastStatusReported, 0, mCacheDisposition,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(),
        mClassOfService.Flags(), mStatus);
  }

  // Added due to PauseTask/DelayHttpChannel
  if (mLoadGroup) mLoadGroup->AddRequest(this, nullptr);

  // record asyncopen time unconditionally and clear it if we
  // don't want it after OnModifyRequest() weighs in. But waiting for
  // that to complete would mean we don't include proxy resolution in the
  // timing.
  if (!LoadAsyncOpenTimeOverriden()) {
    mAsyncOpenTime = aTimeStamp;
  }

  // Remember we have Authorization header set here.  We need to check on it
  // just once and early, AsyncOpen is the best place.
  StoreCustomAuthHeader(mRequestHead.HasHeader(nsHttp::Authorization));

  bool willCallback = false;
  // We are about to do an async lookup to check if the URI is a tracker. If
  // yes, this channel will be canceled by channel classifier.  Chances are the
  // lookup is not needed so CheckIsTrackerWithLocalTable() will return an
  // error and then we can MaybeResolveProxyAndBeginConnect() right away.
  // We skip the check in case this is an internal redirected channel
  if (!LoadAuthRedirectedChannel() &&
      NS_ShouldClassifyChannel(this, ClassifyType::ETP)) {
    RefPtr<nsHttpChannel> self = this;
    willCallback = NS_SUCCEEDED(
        AsyncUrlChannelClassifier::CheckChannel(this, [self]() -> void {
          nsCOMPtr<nsIURI> uri;
          self->GetURI(getter_AddRefs(uri));
          MOZ_ASSERT(uri);

          // Finish the AntiTracking Heuristic before
          // MaybeResolveProxyAndBeginConnect().
          FinishAntiTrackingRedirectHeuristic(self, uri);

          self->MaybeResolveProxyAndBeginConnect();
        }));
  }

  if (!willCallback) {
    // We can do MaybeResolveProxyAndBeginConnect immediately if
    // CheckIsTrackerWithLocalTable is failed. Note that we don't need to
    // handle the failure because BeginConnect() will return synchronously and
    // the caller will be responsible for handling it.
    MaybeResolveProxyAndBeginConnect();
  }
}

void nsHttpChannel::MaybeResolveProxyAndBeginConnect() {
  nsresult rv;

  // The common case for HTTP channels is to begin proxy resolution and return
  // at this point. The only time we know mProxyInfo already is if we're
  // proxying a non-http protocol like ftp. We don't need to discover proxy
  // settings if we are never going to make a network connection.
  if (!mProxyInfo &&
      !(mLoadFlags & (LOAD_ONLY_FROM_CACHE | LOAD_NO_NETWORK_IO)) &&
      !BypassProxy() && NS_SUCCEEDED(ResolveProxy())) {
    return;
  }

  if (!gHttpHandler->Active()) {
    LOG(
        ("nsHttpChannel::MaybeResolveProxyAndBeginConnect [this=%p] "
         "Handler no longer active.\n",
         this));
    rv = NS_ERROR_NOT_AVAILABLE;
  } else {
    rv = BeginConnect();
  }
  if (NS_FAILED(rv)) {
    CloseCacheEntry(false);
    Unused << AsyncAbort(rv);
  }
}

nsresult nsHttpChannel::AsyncOpenOnTailUnblock() {
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::AsyncOpenOnTailUnblock", NETWORK,
                            Flow::FromPointer(this));
  return AsyncOpen(mListener);
}

already_AddRefed<nsChannelClassifier>
nsHttpChannel::GetOrCreateChannelClassifier() {
  if (!mChannelClassifier) {
    mChannelClassifier = new nsChannelClassifier(this);
    LOG(("nsHttpChannel [%p] created nsChannelClassifier [%p]\n", this,
         mChannelClassifier.get()));
  }

  RefPtr<nsChannelClassifier> classifier = mChannelClassifier;
  return classifier.forget();
}

ProxyDNSStrategy nsHttpChannel::GetProxyDNSStrategy() {
  // When network_dns_force_use_https_rr is true, return DNS_PREFETCH_ORIGIN.
  // This ensures that we always perform HTTPS RR query.
  nsCOMPtr<nsProxyInfo> proxyInfo(static_cast<nsProxyInfo*>(mProxyInfo.get()));
  if (!proxyInfo || StaticPrefs::network_dns_force_use_https_rr()) {
    return ProxyDNSStrategy::ORIGIN;
  }

  // If the proxy is not to perform name resolution itself.
  return GetProxyDNSStrategyHelper(proxyInfo->Type(), proxyInfo->Flags());
}

// BeginConnect() SHOULD NOT call AsyncAbort(). AsyncAbort will be called by
// functions that called BeginConnect if needed. Only
// MaybeResolveProxyAndBeginConnect and OnProxyAvailable ever call
// BeginConnect.
nsresult nsHttpChannel::BeginConnect() {
  LOG(("nsHttpChannel::BeginConnect [this=%p]\n", this));
  nsresult rv;

  // It is the caller's responsibility to not call us late in shutdown.
  MOZ_ASSERT(gHttpHandler->Active());

  // Construct connection info object
  nsAutoCString host;
  nsAutoCString scheme;
  int32_t port = -1;
  bool isHttps = mURI->SchemeIs("https");

  rv = mURI->GetScheme(scheme);
  if (NS_SUCCEEDED(rv)) rv = mURI->GetAsciiHost(host);
  if (NS_SUCCEEDED(rv)) rv = mURI->GetPort(&port);
  if (NS_SUCCEEDED(rv)) rv = mURI->GetAsciiSpec(mSpec);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Just a warning here because some nsIURIs do not implement this method.
  Unused << NS_WARN_IF(NS_FAILED(mURI->GetUsername(mUsername)));

  // Reject the URL if it doesn't specify a host
  if (host.IsEmpty()) {
    rv = NS_ERROR_MALFORMED_URI;
    return rv;
  }
  LOG(("host=%s port=%d\n", host.get(), port));
  LOG(("uri=%s\n", mSpec.get()));

  nsCOMPtr<nsProxyInfo> proxyInfo;
  if (mProxyInfo) proxyInfo = do_QueryInterface(mProxyInfo);

  if (mCaps & NS_HTTP_CONNECT_ONLY) {
    if (!proxyInfo) {
      LOG(("return failure: no proxy for connect-only channel\n"));
      return NS_ERROR_FAILURE;
    }

    if (!proxyInfo->IsHTTP() && !proxyInfo->IsHTTPS()) {
      LOG(("return failure: non-http proxy for connect-only channel\n"));
      return NS_ERROR_FAILURE;
    }
  }

  mRequestHead.SetHTTPS(isHttps);
  mRequestHead.SetOrigin(scheme, host, port);

  SetOriginHeader();
  SetDoNotTrack();
  SetGlobalPrivacyControl();

  OriginAttributes originAttributes;
  // Regular principal in case we have a proxy.
  if (proxyInfo &&
      !StaticPrefs::privacy_partition_network_state_connection_with_proxy()) {
    StoragePrincipalHelper::GetOriginAttributes(
        this, originAttributes, StoragePrincipalHelper::eRegularPrincipal);
  } else {
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(
        this, originAttributes);
  }

  // Adjust mCaps according to our request headers:
  //  - If "Connection: close" is set as a request header, then do not bother
  //    trying to establish a keep-alive connection.
  if (mRequestHead.HasHeaderValue(nsHttp::Connection, "close")) {
    mCaps &= ~(NS_HTTP_ALLOW_KEEPALIVE);
    StoreAllowHttp3(false);
  }

  gHttpHandler->MaybeAddAltSvcForTesting(mURI, mUsername, mPrivateBrowsing,
                                         mCallbacks, originAttributes);

  RefPtr<nsHttpConnectionInfo> connInfo;
#ifdef FUZZING
  if (StaticPrefs::fuzzing_necko_http3()) {
    connInfo =
        new nsHttpConnectionInfo(host, port, "h3"_ns, mUsername, proxyInfo,
                                 originAttributes, host, port, true);
  } else {
#endif
    if (mWebTransportSessionEventListener) {
      connInfo =
          new nsHttpConnectionInfo(host, port, "h3"_ns, mUsername, proxyInfo,
                                   originAttributes, isHttps, true, true);
      bool dedicated = true;
      nsresult rv;
      nsCOMPtr<WebTransportConnectionSettings> wtconSettings =
          do_QueryInterface(mWebTransportSessionEventListener, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      nsIWebTransport::HTTPVersion httpVersion;
      Unused << wtconSettings->GetHttpVersion(&httpVersion);
      if (httpVersion == nsIWebTransport::HTTPVersion::h2) {
        connInfo =
            new nsHttpConnectionInfo(host, port, "h2"_ns, mUsername, proxyInfo,
                                     originAttributes, isHttps, false, true);
      } else {
        connInfo =
            new nsHttpConnectionInfo(host, port, "h3"_ns, mUsername, proxyInfo,
                                     originAttributes, isHttps, true, true);
      }
      wtconSettings->GetDedicated(&dedicated);
      if (dedicated) {
        connInfo->SetWebTransportId(
            nsHttpConnectionInfo::GenerateNewWebTransportId());
      }
    } else {
      connInfo = new nsHttpConnectionInfo(host, port, ""_ns, mUsername,
                                          proxyInfo, originAttributes, isHttps);
    }
#ifdef FUZZING
  }
#endif

  bool http2Allowed = !gHttpHandler->IsHttp2Excluded(connInfo);

  bool http3Allowed = Http3Allowed();
  if (!http3Allowed) {
    mCaps |= NS_HTTP_DISALLOW_HTTP3;
  }

  RefPtr<AltSvcMapping> mapping;
  if (!mConnectionInfo && LoadAllowAltSvc() &&  // per channel
      !mWebTransportSessionEventListener && (http2Allowed || http3Allowed) &&
      !(mLoadFlags & LOAD_FRESH_CONNECTION) &&
      AltSvcMapping::AcceptableProxy(proxyInfo) &&
      (scheme.EqualsLiteral("http") || scheme.EqualsLiteral("https")) &&
      (mapping = gHttpHandler->GetAltServiceMapping(
           scheme, host, port, mPrivateBrowsing, originAttributes, http2Allowed,
           http3Allowed))) {
    LOG(("nsHttpChannel %p Alt Service Mapping Found %s://%s:%d [%s]\n", this,
         scheme.get(), mapping->AlternateHost().get(), mapping->AlternatePort(),
         mapping->HashKey().get()));

    if (!(mLoadFlags & LOAD_ANONYMOUS) && !mPrivateBrowsing) {
      nsAutoCString altUsedLine(mapping->AlternateHost());
      bool defaultPort =
          mapping->AlternatePort() ==
          (isHttps ? NS_HTTPS_DEFAULT_PORT : NS_HTTP_DEFAULT_PORT);
      if (!defaultPort) {
        altUsedLine.AppendLiteral(":");
        altUsedLine.AppendInt(mapping->AlternatePort());
      }
      // Like what we did for 'Authorization' header, we need to do the same for
      // 'Alt-Used' for avoiding this header being shown in the ServiceWorker
      // FetchEvent.
      Unused << mRequestHead.ClearHeader(nsHttp::Alternate_Service_Used);
      rv = mRequestHead.SetHeader(nsHttp::Alternate_Service_Used, altUsedLine,
                                  false,
                                  nsHttpHeaderArray::eVarietyRequestDefault);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    nsCOMPtr<nsIConsoleService> consoleService;
    consoleService = mozilla::components::Console::Service();
    if (consoleService && !host.Equals(mapping->AlternateHost())) {
      nsAutoString message(u"Alternate Service Mapping found: "_ns);
      AppendASCIItoUTF16(scheme, message);
      message.AppendLiteral(u"://");
      AppendASCIItoUTF16(host, message);
      message.AppendLiteral(u":");
      message.AppendInt(port);
      message.AppendLiteral(u" to ");
      AppendASCIItoUTF16(scheme, message);
      message.AppendLiteral(u"://");
      AppendASCIItoUTF16(mapping->AlternateHost(), message);
      message.AppendLiteral(u":");
      message.AppendInt(mapping->AlternatePort());
      consoleService->LogStringMessage(message.get());
    }

    LOG(("nsHttpChannel %p Using connection info from altsvc mapping", this));
    mapping->GetConnectionInfo(getter_AddRefs(mConnectionInfo), proxyInfo,
                               originAttributes);
    glean::http::transaction_use_altsvc
        .EnumGet(glean::http::TransactionUseAltsvcLabel::eTrue)
        .Add();
    if (mConnectionInfo->IsHttp3() &&
        StaticPrefs::
            network_http_http3_force_use_alt_svc_mapping_for_testing()) {
      mCaps |= NS_HTTP_DISALLOW_SPDY;
    }
  } else if (mConnectionInfo) {
    LOG(("nsHttpChannel %p Using channel supplied connection info", this));
    glean::http::transaction_use_altsvc
        .EnumGet(glean::http::TransactionUseAltsvcLabel::eFalse)
        .Add();
  } else {
    LOG(("nsHttpChannel %p Using default connection info", this));

    mConnectionInfo = connInfo;
    glean::http::transaction_use_altsvc
        .EnumGet(glean::http::TransactionUseAltsvcLabel::eFalse)
        .Add();
  }

  bool trrEnabled = false;
  auto dnsStrategy = GetProxyDNSStrategy();
  bool httpsRRAllowed =
      !LoadBeConservative() && !(mCaps & NS_HTTP_BE_CONSERVATIVE) &&
      !(mLoadInfo->TriggeringPrincipal()->IsSystemPrincipal() &&
        mLoadInfo->GetExternalContentPolicyType() !=
            ExtContentPolicy::TYPE_DOCUMENT) &&
      dnsStrategy == ProxyDNSStrategy::ORIGIN &&
      !mConnectionInfo->UsingConnect() && canUseHTTPSRRonNetwork(trrEnabled) &&
      StaticPrefs::network_dns_use_https_rr_as_altsvc();
  if (!httpsRRAllowed) {
    DisallowHTTPSRR(mCaps);
  } else if (trrEnabled) {
    if (nsIRequest::GetTRRMode() != nsIRequest::TRR_DISABLED_MODE) {
      mCaps |= NS_HTTP_FORCE_WAIT_HTTP_RR;
    }
  }
  // No need to lookup HTTPSSVC record if mHTTPSSVCRecord already contains a
  // value.
  StoreUseHTTPSSVC(StaticPrefs::network_dns_upgrade_with_https_rr() &&
                   httpsRRAllowed && mHTTPSSVCRecord.isNothing());

  // Need to re-ask the handler, since mConnectionInfo may not be the connInfo
  // we used earlier
  if (!mConnectionInfo->IsHttp3() &&
      gHttpHandler->IsHttp2Excluded(mConnectionInfo)) {
    StoreAllowSpdy(0);
    mCaps |= NS_HTTP_DISALLOW_SPDY;
    mConnectionInfo->SetNoSpdy(true);
  }

  // We can be passed with the auth provider if this channel was
  // a result of redirect due to auth retry
  if (!mAuthProvider) {
    mAuthProvider = new nsHttpChannelAuthProvider();
  }

  rv = mAuthProvider->Init(this);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // check to see if authorization headers should be included
  // CustomAuthHeader is set in AsyncOpen if we find Authorization header
  rv = mAuthProvider->AddAuthorizationHeaders(LoadCustomAuthHeader());
  if (NS_FAILED(rv)) {
    LOG(("nsHttpChannel %p AddAuthorizationHeaders failed (%08x)", this,
         static_cast<uint32_t>(rv)));
  }

  // if this somehow fails we can go on without it
  Unused << gHttpHandler->AddConnectionHeader(&mRequestHead, mCaps);

  if (!LoadIsTRRServiceChannel() &&
      ((mLoadFlags & LOAD_FRESH_CONNECTION) ||
       (!StaticPrefs::network_dns_only_refresh_on_fresh_connection() &&
        (mLoadFlags & VALIDATE_ALWAYS ||
         BYPASS_LOCAL_CACHE(mLoadFlags, LoadPreferCacheLoadOverBypass()))))) {
    mCaps |= NS_HTTP_REFRESH_DNS;
  }

  if (gHttpHandler->CriticalRequestPrioritization()) {
    if (mClassOfService.Flags() & nsIClassOfService::Leader) {
      mCaps |= NS_HTTP_LOAD_AS_BLOCKING;
    }
    if (mClassOfService.Flags() & nsIClassOfService::Unblocked) {
      mCaps |= NS_HTTP_LOAD_UNBLOCKED;
    }
    if (mClassOfService.Flags() & nsIClassOfService::UrgentStart &&
        gHttpHandler->IsUrgentStartEnabled()) {
      mCaps |= NS_HTTP_URGENT_START;
      SetPriority(nsISupportsPriority::PRIORITY_HIGHEST);
    }
  }

  // Force-Reload should reset the persistent connection pool for this host
  if (mLoadFlags & LOAD_FRESH_CONNECTION) {
    // just the initial document resets the whole pool
    if (mLoadFlags & LOAD_INITIAL_DOCUMENT_URI) {
      gHttpHandler->AltServiceCache()->ClearAltServiceMappings();
      rv = gHttpHandler->DoShiftReloadConnectionCleanupWithConnInfo(
          mConnectionInfo);
      if (NS_FAILED(rv)) {
        LOG((
            "nsHttpChannel::BeginConnect "
            "DoShiftReloadConnectionCleanupWithConnInfo failed: %08x [this=%p]",
            static_cast<uint32_t>(rv), this));
      }
    }
  }

  // We may have been cancelled already, either by on-modify-request
  // listeners or load group observers; in that case, we should not send the
  // request to the server
  if (mCanceled) {
    return mStatus;
  }
  // skip classifier checks if this channel was the result of internal auth
  // redirect
  bool shouldBeClassifiedForTracker =
      !LoadAuthRedirectedChannel() &&
      NS_ShouldClassifyChannel(this, ClassifyType::ETP);

  if (shouldBeClassifiedForTracker) {
    if (LoadChannelClassifierCancellationPending()) {
      LOG(
          ("Waiting for safe-browsing protection cancellation in BeginConnect "
           "[this=%p]\n",
           this));
      return NS_OK;
    }

    ReEvaluateReferrerAfterTrackingStatusIsKnown();
  }

  MaybeStartDNSPrefetch();

  // Update whether the channel is on the third-party cookie blocking exception
  // list.
  CookieService::Update3PCBExceptionInfo(this);

  rv = CallOrWaitForResume(
      [](nsHttpChannel* self) { return self->PrepareToConnect(); });
  if (NS_FAILED(rv)) {
    return rv;
  }

  bool shouldBeClassifiedForSafeBrowsing =
      NS_ShouldClassifyChannel(this, ClassifyType::SafeBrowsing);

  if (shouldBeClassifiedForSafeBrowsing) {
    // Start nsChannelClassifier to catch phishing and malware URIs.
    RefPtr<nsChannelClassifier> channelClassifier =
        GetOrCreateChannelClassifier();
    LOG(("nsHttpChannel::Starting nsChannelClassifier %p [this=%p]",
         channelClassifier.get(), this));
    channelClassifier->Start();
  }

  return NS_OK;
}

void nsHttpChannel::MaybeStartDNSPrefetch() {
  // Start a DNS lookup very early in case the real open is queued the DNS can
  // happen in parallel. Do not do so in the presence of an HTTP proxy as
  // all lookups other than for the proxy itself are done by the proxy.
  // Also we don't do a lookup if the LOAD_NO_NETWORK_IO or
  // LOAD_ONLY_FROM_CACHE flags are set.
  //
  // We keep the DNS prefetch object around so that we can retrieve
  // timing information from it. There is no guarantee that we actually
  // use the DNS prefetch data for the real connection, but as we keep
  // this data around for 3 minutes by default, this should almost always
  // be correct, and even when it isn't, the timing still represents _a_
  // valid DNS lookup timing for the site, even if it is not _the_
  // timing we used.
  if ((mLoadFlags & (LOAD_NO_NETWORK_IO | LOAD_ONLY_FROM_CACHE)) ||
      LoadAuthRedirectedChannel()) {
    return;
  }

  auto dnsStrategy = GetProxyDNSStrategy();

  LOG(
      ("nsHttpChannel::MaybeStartDNSPrefetch [this=%p, strategy=%u] "
       "prefetching%s\n",
       this, static_cast<uint32_t>(dnsStrategy),
       mCaps & NS_HTTP_REFRESH_DNS ? ", refresh requested" : ""));

  if (dnsStrategy == ProxyDNSStrategy::ORIGIN) {
    OriginAttributes originAttributes;
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(
        this, originAttributes);

    mDNSPrefetch = new nsDNSPrefetch(mURI, originAttributes,
                                     nsIRequest::GetTRRMode(), this, true);
    nsIDNSService::DNSFlags dnsFlags = nsIDNSService::RESOLVE_DEFAULT_FLAGS;
    if (mCaps & NS_HTTP_REFRESH_DNS) {
      dnsFlags |= nsIDNSService::RESOLVE_BYPASS_CACHE;
    }

    Unused << mDNSPrefetch->PrefetchHigh(dnsFlags);

    bool unused;
    if (StaticPrefs::network_dns_use_https_rr_as_altsvc() && !mHTTPSSVCRecord &&
        !(mCaps & NS_HTTP_DISALLOW_HTTPS_RR) &&
        canUseHTTPSRRonNetwork(unused)) {
      MOZ_ASSERT(!mHTTPSSVCRecord);

      OriginAttributes originAttributes;
      StoragePrincipalHelper::GetOriginAttributesForHTTPSRR(this,
                                                            originAttributes);

      RefPtr<nsDNSPrefetch> resolver =
          new nsDNSPrefetch(mURI, originAttributes, nsIRequest::GetTRRMode());
      Unused << resolver->FetchHTTPSSVC(mCaps & NS_HTTP_REFRESH_DNS, true,
                                        [](nsIDNSHTTPSSVCRecord*) {
                                          // Do nothing. This is a DNS prefetch.
                                        });
    }
  }
}

NS_IMETHODIMP
nsHttpChannel::GetEncodedBodySize(uint64_t* aEncodedBodySize) {
  if (mCacheEntry && !LoadCacheEntryIsWriteOnly()) {
    int64_t dataSize = 0;
    mCacheEntry->GetDataSize(&dataSize);
    *aEncodedBodySize = dataSize;
  } else {
    *aEncodedBodySize = mLogicalOffset;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIHttpChannelInternal
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetIsAuthChannel(bool* aIsAuthChannel) {
  *aIsAuthChannel = mIsAuthChannel;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetChannelIsForDownload(bool aChannelIsForDownload) {
  if (aChannelIsForDownload) {
    AddClassFlags(nsIClassOfService::Throttleable);
  } else {
    ClearClassFlags(nsIClassOfService::Throttleable);
  }

  return HttpBaseChannel::SetChannelIsForDownload(aChannelIsForDownload);
}

base::ProcessId nsHttpChannel::ProcessId() {
  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(this, parentChannel);
  if (RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel)) {
    return httpParent->OtherPid();
  }
  if (RefPtr<DocumentLoadListener> docParent = do_QueryObject(parentChannel)) {
    return docParent->OtherPid();
  }
  return base::GetCurrentProcId();
}

auto nsHttpChannel::AttachStreamFilter() -> RefPtr<ChildEndpointPromise> {
  LOG(("nsHttpChannel::AttachStreamFilter [this=%p]", this));
  MOZ_ASSERT(!LoadOnStartRequestCalled());

  if (!ProcessId()) {
    return ChildEndpointPromise::CreateAndReject(false, __func__);
  }

  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(this, parentChannel);

  // If our listener is a DocumentLoadListener, then we might handle
  // multi-part responses here in the parent process. The current extension
  // API doesn't understand the parsed multipart format, so we defer responding
  // here until CallOnStartRequest, and attach the StreamFilter before the
  // multipart handler (in the parent process!) if applicable.
  if (RefPtr<DocumentLoadListener> docParent = do_QueryObject(parentChannel)) {
    StreamFilterRequest* request = mStreamFilterRequests.AppendElement();
    request->mPromise = new ChildEndpointPromise::Private(__func__);
    return request->mPromise;
  }

  mozilla::ipc::Endpoint<extensions::PStreamFilterParent> parent;
  mozilla::ipc::Endpoint<extensions::PStreamFilterChild> child;
  nsresult rv = extensions::PStreamFilter::CreateEndpoints(&parent, &child);
  if (NS_FAILED(rv)) {
    return ChildEndpointPromise::CreateAndReject(false, __func__);
  }

  if (RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel)) {
    return httpParent->AttachStreamFilter(std::move(parent), std::move(child));
  }

  extensions::StreamFilterParent::Attach(this, std::move(parent));
  return ChildEndpointPromise::CreateAndResolve(std::move(child), __func__);
}

NS_IMETHODIMP
nsHttpChannel::GetNavigationStartTimeStamp(TimeStamp* aTimeStamp) {
  LOG(("nsHttpChannel::GetNavigationStartTimeStamp [this=%p]", this));
  MOZ_ASSERT(aTimeStamp);
  *aTimeStamp = mNavigationStartTimeStamp;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetNavigationStartTimeStamp(TimeStamp aTimeStamp) {
  LOG(("nsHttpChannel::SetNavigationStartTimeStamp [this=%p]", this));
  mNavigationStartTimeStamp = aTimeStamp;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsISupportsPriority
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::SetPriority(int32_t value) {
  int16_t newValue = std::clamp<int32_t>(value, INT16_MIN, INT16_MAX);
  if (mPriority == newValue) return NS_OK;

  LOG(("nsHttpChannel::SetPriority %p p=%d", this, newValue));

  mPriority = newValue;
  if (mTransaction) {
    nsresult rv = gHttpHandler->RescheduleTransaction(mTransaction, mPriority);
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpChannel::SetPriority [this=%p] "
           "RescheduleTransaction failed (%08x)",
           this, static_cast<uint32_t>(rv)));
    }
  }

  // If this channel is the real channel for an e10s channel, notify the
  // child side about the priority change as well.
  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(this, parentChannel);
  RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel);
  if (httpParent) {
    httpParent->DoSendSetPriority(newValue);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannel::nsIClassOfService
//-----------------------------------------------------------------------------

void nsHttpChannel::OnClassOfServiceUpdated() {
  LOG(("nsHttpChannel::OnClassOfServiceUpdated this=%p, cos=%lu, inc=%d", this,
       mClassOfService.Flags(), mClassOfService.Incremental()));

  if (mTransaction) {
    gHttpHandler->UpdateClassOfServiceOnTransaction(mTransaction,
                                                    mClassOfService);
  }
  if (EligibleForTailing()) {
    RemoveAsNonTailRequest();
  } else {
    AddAsNonTailRequest();
  }
}

NS_IMETHODIMP
nsHttpChannel::SetClassFlags(uint32_t inFlags) {
  uint32_t previous = mClassOfService.Flags();
  mClassOfService.SetFlags(inFlags);
  if (previous != mClassOfService.Flags()) {
    OnClassOfServiceUpdated();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::AddClassFlags(uint32_t inFlags) {
  uint32_t previous = mClassOfService.Flags();
  mClassOfService.SetFlags(inFlags | mClassOfService.Flags());
  if (previous != mClassOfService.Flags()) {
    OnClassOfServiceUpdated();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::ClearClassFlags(uint32_t inFlags) {
  uint32_t previous = mClassOfService.Flags();
  mClassOfService.SetFlags(~inFlags & mClassOfService.Flags());
  if (previous != mClassOfService.Flags()) {
    OnClassOfServiceUpdated();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetClassOfService(ClassOfService cos) {
  ClassOfService previous = mClassOfService;
  mClassOfService = cos;
  if (previous != mClassOfService) {
    OnClassOfServiceUpdated();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetIncremental(bool incremental) {
  bool previous = mClassOfService.Incremental();
  mClassOfService.SetIncremental(incremental);
  if (previous != mClassOfService.Incremental()) {
    OnClassOfServiceUpdated();
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIProtocolProxyCallback
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnProxyAvailable(nsICancelable* request, nsIChannel* channel,
                                nsIProxyInfo* pi, nsresult status) {
  LOG(("nsHttpChannel::OnProxyAvailable [this=%p pi=%p status=%" PRIx32
       " mStatus=%" PRIx32 "]\n",
       this, pi, static_cast<uint32_t>(status),
       static_cast<uint32_t>(static_cast<nsresult>(mStatus))));
  mProxyRequest = nullptr;

  nsresult rv;

  // If status is a failure code, then it means that we failed to resolve
  // proxy info.  That is a non-fatal error assuming it wasn't because the
  // request was canceled.  We just failover to DIRECT when proxy resolution
  // fails (failure can mean that the PAC URL could not be loaded).

  if (NS_SUCCEEDED(status)) {
    mProxyInfo = pi;

    if (mProxyInfo) {
      nsAutoCStringN<8> type;
      mProxyInfo->GetType(type);
      uint32_t flags = 0;
      mProxyInfo->GetFlags(&flags);

      if (type.EqualsLiteral("socks")) {
        if (flags & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST) {
          glean::networking::proxy_info_type
              .EnumGet(glean::networking::ProxyInfoTypeLabel::eSocks5h)
              .Add(1);
        } else {
          glean::networking::proxy_info_type
              .EnumGet(glean::networking::ProxyInfoTypeLabel::eSocks5)
              .Add(1);
        }
      } else if (type.EqualsLiteral("socks4")) {
        if (flags & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST) {
          glean::networking::proxy_info_type
              .EnumGet(glean::networking::ProxyInfoTypeLabel::eSocks4a)
              .Add(1);
        } else {
          glean::networking::proxy_info_type
              .EnumGet(glean::networking::ProxyInfoTypeLabel::eSocks4)
              .Add(1);
        }
      } else if (type.EqualsLiteral("http")) {
        glean::networking::proxy_info_type
            .EnumGet(glean::networking::ProxyInfoTypeLabel::eHttp)
            .Add(1);
      } else if (type.EqualsLiteral("https")) {
        glean::networking::proxy_info_type
            .EnumGet(glean::networking::ProxyInfoTypeLabel::eHttps)
            .Add(1);
      } else if (type.EqualsLiteral("direct")) {
        glean::networking::proxy_info_type
            .EnumGet(glean::networking::ProxyInfoTypeLabel::eDirect)
            .Add(1);
      } else {
        glean::networking::proxy_info_type
            .EnumGet(glean::networking::ProxyInfoTypeLabel::eUnknown)
            .Add(1);
      }
    }
  }

  if (!gHttpHandler->Active()) {
    LOG(
        ("nsHttpChannel::OnProxyAvailable [this=%p] "
         "Handler no longer active.\n",
         this));
    rv = NS_ERROR_NOT_AVAILABLE;
  } else {
    rv = BeginConnect();
  }

  if (NS_FAILED(rv)) {
    CloseCacheEntry(false);
    Unused << AsyncAbort(rv);
  }
  return rv;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIProxiedChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetProxyInfo(nsIProxyInfo** result) {
  if (!mConnectionInfo) {
    *result = do_AddRef(mProxyInfo).take();
  } else {
    *result = do_AddRef(mConnectionInfo->ProxyInfo()).take();
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsITimedChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetDomainLookupStart(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetDomainLookupStart();
  } else {
    *_retval = mTransactionTimings.domainLookupStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetDomainLookupEnd(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetDomainLookupEnd();
  } else {
    *_retval = mTransactionTimings.domainLookupEnd;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetConnectStart(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetConnectStart();
  } else {
    *_retval = mTransactionTimings.connectStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetTcpConnectEnd(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetTcpConnectEnd();
  } else {
    *_retval = mTransactionTimings.tcpConnectEnd;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetSecureConnectionStart(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetSecureConnectionStart();
  } else {
    *_retval = mTransactionTimings.secureConnectionStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetConnectEnd(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetConnectEnd();
  } else {
    *_retval = mTransactionTimings.connectEnd;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetRequestStart(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetRequestStart();
  } else {
    *_retval = mTransactionTimings.requestStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseStart(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetResponseStart();
  } else {
    *_retval = mTransactionTimings.responseStart;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetResponseEnd(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetResponseEnd();
  } else {
    *_retval = mTransactionTimings.responseEnd;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetTransactionPending(TimeStamp* _retval) {
  if (mTransaction) {
    *_retval = mTransaction->GetPendingTime();
  } else {
    *_retval = mTransactionTimings.transactionPending;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIHttpAuthenticableChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetIsSSL(bool* aIsSSL) {
  // this attribute is really misnamed - it wants to know if
  // https:// is being used. SSL might be used to cover http://
  // in some circumstances (proxies, http/2, etc..)
  return mURI->SchemeIs("https", aIsSSL);
}

NS_IMETHODIMP
nsHttpChannel::GetProxyMethodIsConnect(bool* aProxyMethodIsConnect) {
  *aProxyMethodIsConnect = mConnectionInfo->UsingConnect();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetServerResponseHeader(nsACString& value) {
  if (!mResponseHead) return NS_ERROR_NOT_AVAILABLE;
  return mResponseHead->GetHeader(nsHttp::Server, value);
}

NS_IMETHODIMP
nsHttpChannel::GetProxyChallenges(nsACString& value) {
  if (!mResponseHead) return NS_ERROR_UNEXPECTED;
  return mResponseHead->GetHeader(nsHttp::Proxy_Authenticate, value);
}

NS_IMETHODIMP
nsHttpChannel::GetWWWChallenges(nsACString& value) {
  if (!mResponseHead) return NS_ERROR_UNEXPECTED;
  return mResponseHead->GetHeader(nsHttp::WWW_Authenticate, value);
}

NS_IMETHODIMP
nsHttpChannel::SetProxyCredentials(const nsACString& value) {
  return mRequestHead.SetHeader(nsHttp::Proxy_Authorization, value);
}

NS_IMETHODIMP
nsHttpChannel::SetWWWCredentials(const nsACString& value) {
  // This method is called when various browser initiated authorization
  // code sets the credentials.  We need to flag this header as the
  // "browser default" so it does not show up in the ServiceWorker
  // FetchEvent.  This may actually get called more than once, though,
  // so we clear the header first since "default" headers are not
  // allowed to overwrite normally.
  Unused << mRequestHead.ClearHeader(nsHttp::Authorization);
  return mRequestHead.SetHeader(nsHttp::Authorization, value, false,
                                nsHttpHeaderArray::eVarietyRequestDefault);
}

//-----------------------------------------------------------------------------
// Methods that nsIHttpAuthenticableChannel dupes from other IDLs, which we
// get from HttpBaseChannel, must be explicitly forwarded, because C++ sucks.
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  return HttpBaseChannel::GetLoadFlags(aLoadFlags);
}

NS_IMETHODIMP
nsHttpChannel::GetURI(nsIURI** aURI) { return HttpBaseChannel::GetURI(aURI); }

NS_IMETHODIMP
nsHttpChannel::GetNotificationCallbacks(nsIInterfaceRequestor** aCallbacks) {
  return HttpBaseChannel::GetNotificationCallbacks(aCallbacks);
}

NS_IMETHODIMP
nsHttpChannel::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  return HttpBaseChannel::GetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsHttpChannel::GetRequestMethod(nsACString& aMethod) {
  return HttpBaseChannel::GetRequestMethod(aMethod);
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIRequestObserver
//-----------------------------------------------------------------------------

void nsHttpChannel::RecordOnStartTelemetry(nsresult aStatus,
                                           bool aIsNavigation) {
  glean::http::channel_onstart_success
      .EnumGet(static_cast<glean::http::ChannelOnstartSuccessLabel>(
          NS_SUCCEEDED(aStatus)))
      .Add();

  mozilla::glean::networking::http_channel_onstart_status
      .Get(NS_SUCCEEDED(aStatus) ? "successful"_ns : "fail"_ns)
      .Add(1);

  if (mTransaction) {
    Telemetry::Accumulate(
        Telemetry::HTTP3_CHANNEL_ONSTART_SUCCESS,
        (mTransaction->IsHttp3Used()) ? "http3"_ns : "no_http3"_ns,
        NS_SUCCEEDED(aStatus));
  }

  enum class HttpOnStartState : uint32_t {
    Success = 0,
    DNSError = 1,
    Others = 2,
  };

  if (TRRService::Get() && TRRService::Get()->IsConfirmed()) {
    // Note this telemetry probe is not working when DNS resolution is done in
    // the socket process.
    HttpOnStartState state = HttpOnStartState::Others;
    if (NS_SUCCEEDED(aStatus)) {
      state = HttpOnStartState::Success;
    } else if (aStatus == NS_ERROR_UNKNOWN_HOST ||
               aStatus == NS_ERROR_UNKNOWN_PROXY_HOST) {
      state = HttpOnStartState::DNSError;
    }

    if (aIsNavigation) {
      glean::http::channel_page_onstart_success_trr
          .Get(TRRService::ProviderKey())
          .AccumulateSingleSample(static_cast<uint32_t>(state));
    } else {
      glean::http::channel_sub_onstart_success_trr
          .Get(TRRService::ProviderKey())
          .AccumulateSingleSample(static_cast<uint32_t>(state));
    }
  }

  if (nsIOService::UseSocketProcess() && mTransaction) {
    const TimeStamp now = TimeStamp::Now();
    TimeStamp responseEnd = mTransaction->GetResponseEnd();
    if (!responseEnd.IsNull()) {
      PerfStats::RecordMeasurement(PerfStats::Metric::ResponseEndSocketToParent,
                                   now - responseEnd);
    }

    mOnStartRequestStartTime = mTransaction->GetOnStartRequestStartTime();
    if (!mOnStartRequestStartTime.IsNull()) {
      PerfStats::RecordMeasurement(
          PerfStats::Metric::OnStartRequestSocketToParent,
          now - mOnStartRequestStartTime);
    }
  } else {
    mOnStartRequestStartTime = TimeStamp::Now();
  }
}

static bool hasConnectivity() {
  if (RefPtr<NetworkConnectivityService> ncs =
          NetworkConnectivityService::GetSingleton()) {
    nsINetworkConnectivityService::ConnectivityState state;
    if (NS_SUCCEEDED(ncs->GetIPv4(&state)) &&
        state == nsINetworkConnectivityService::OK) {
      return true;
    }
    // We should also check for IPv6 connectivity here, but since
    // quite a few mozilla domains don't have IPv6 records yet,
    // we might incorrectly fallback to a backup domain on
    // IPv6 only networks.
    // When bug 1665605 gets fixed we can also return true when only IPv6 is OK.
  }

  return false;
};

static already_AddRefed<nsIURI> GetFallbackURI(nsIURI* aURI) {
  nsresult rv;
  nsAutoCString host;
  aURI->GetHost(host);
  nsCOMPtr<nsIURI> backupURI;

  nsAutoCString fallbackDomain;
  if (!gIOService->GetFallbackDomain(host, fallbackDomain)) {
    return nullptr;
  }

  rv = NS_MutateURI(aURI).SetHost(fallbackDomain).Finalize(backupURI);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return backupURI.forget();
}

NS_IMETHODIMP
nsHttpChannel::OnStartRequest(nsIRequest* request) {
  nsresult rv;

  MOZ_ASSERT(LoadRequestObserversCalled());

  AUTO_PROFILER_LABEL("nsHttpChannel::OnStartRequest", NETWORK);
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnStartRequest", NETWORK,
                            Flow::FromPointer(this));

  if (!(mCanceled || NS_FAILED(mStatus)) &&
      !WRONG_RACING_RESPONSE_SOURCE(request)) {
    // capture the request's status, so our consumers will know ASAP of any
    // connection failures, etc - bug 93581
    nsresult status;
    request->GetStatus(&status);
    mStatus = status;
  }

  if (mStatus == NS_ERROR_NON_LOCAL_CONNECTION_REFUSED) {
    MOZ_CRASH_UNSAFE(nsPrintfCString("Attempting to connect to non-local "
                                     "address! opener is [%s], uri is "
                                     "[%s]",
                                     mOpenerCallingScriptLocation
                                         ? mOpenerCallingScriptLocation->get()
                                         : "unknown",
                                     mURI->GetSpecOrDefault().get())
                         .get());
  }

  LOG(("nsHttpChannel::OnStartRequest [this=%p request=%p status=%" PRIx32
       "]\n",
       this, request, static_cast<uint32_t>(static_cast<nsresult>(mStatus))));

  RecordOnStartTelemetry(mStatus, IsNavigation());

  if (mRaceCacheWithNetwork) {
    LOG(
        ("  racingNetAndCache - mFirstResponseSource:%d fromCache:%d "
         "fromNet:%d\n",
         static_cast<int32_t>(mFirstResponseSource), request == mCachePump,
         request == mTransactionPump));
    if (mFirstResponseSource == RESPONSE_PENDING) {
      // When the cache wins mFirstResponseSource is set to
      // RESPONSE_FROM_CACHE earlier in ReadFromCache, so this must be a
      // response from the network.
      MOZ_ASSERT(request == mTransactionPump);
      LOG(("  First response from network\n"));
      {
        // Race condition with OnCacheEntryCheck, which is not limited
        // to main thread.
        mozilla::MutexAutoLock lock(mRCWNLock);
        mFirstResponseSource = RESPONSE_FROM_NETWORK;
        // If we haven't gotten any response from the cache, we've won the
        // race.  Any response from the case means it's a cache 'win', even if
        // the response was "we don't have an entry" or "we have an entry but
        // it's expired/invalid".
        if (LoadCachedContentIsValid() == CachedContentValidity::Unset) {
          StoreNetworkWonRace(1);
        }
        mOnStartRequestTimestamp = TimeStamp::Now();
        PROFILER_MARKER_TEXT(
            "RCWN", NETWORK, {},
            nsPrintfCString("Network won on StartRequest valid=%d for %s - %p",
                            LoadCachedContentIsValid(), mSpec.get(), this));

        // Conditional or byte range header could be added in
        // OnCacheEntryCheck. We need to remove them because the
        // request might be sent again due to auth retry and we must
        // not send these headers without having the entry.
        if (mDidReval) {
          LOG(("  Removing conditional request headers"));
          UntieValidationRequest();
          mDidReval = false;
        }
        if (LoadCachedContentIsPartial()) {
          LOG(("  Removing byte range request headers"));
          UntieByteRangeRequest();
          StoreCachedContentIsPartial(false);
        }
      }
      mAvailableCachedAltDataType.Truncate();
      StoreDeliveringAltData(false);
    } else if (WRONG_RACING_RESPONSE_SOURCE(request)) {
      LOG(("  Early return when racing. This response not needed."));
      return NS_OK;
    } else {
      PROFILER_MARKER_TEXT(
          "RCWN", NETWORK, {},
          nsPrintfCString("Cache won on StartRequest valid=%d for %s - %p",
                          LoadCachedContentIsValid(), mSpec.get(), this));
    }
  }

  // Make sure things are what we expect them to be...
  MOZ_ASSERT(request == mCachePump || request == mTransactionPump,
             "Unexpected request");

  MOZ_ASSERT(mRaceCacheWithNetwork || !(mTransactionPump && mCachePump) ||
                 LoadCachedContentIsPartial() || LoadTransactionReplaced(),
             "If we have both pumps, we're racing cache with network, the cache"
             " content is partial, or the cache entry was revalidated and "
             "OnStopRequest was not called yet for the transaction pump.");

  StoreAfterOnStartRequestBegun(true);
  if (mOnStartRequestTimestamp.IsNull()) {
    mOnStartRequestTimestamp = TimeStamp::Now();
  }

  mozilla::glean::networking::http_onstart_suspend_total_time
      .AccumulateRawDuration(mSuspendTotalTime);

  if (mTransaction) {
    mProxyConnectResponseCode = mTransaction->GetProxyConnectResponseCode();
    if (request == mTransactionPump) {
      StoreDataSentToChildProcess(mTransaction->DataSentToChildProcess());
    }

    if (!mSecurityInfo && !mCachePump) {
      // grab the security info from the connection object; the transaction
      // is guaranteed to own a reference to the connection.
      mSecurityInfo = mTransaction->SecurityInfo();
    }

    uint32_t stage = mTransaction->HTTPSSVCReceivedStage();
    if (!LoadHTTPSSVCTelemetryReported() && stage != HTTPSSVC_NOT_USED) {
      glean::http::dns_httpssvc_record_receiving_stage.AccumulateSingleSample(
          stage);
    }

    if (HTTPS_RR_IS_USED(stage)) {
      nsAutoCString suffix(LoadEchConfigUsed() ? "_ech_used" : "");
      // Determine the result string based on the status.
      nsAutoCString result(NS_SUCCEEDED(mStatus) ? "success" : "failure");
      result.Append(suffix);

      mozilla::glean::networking::http_channel_onstart_success_https_rr
          .Get(result)
          .Add(1);
      StoreHasHTTPSRR(true);
    }

    StoreLoadedBySocketProcess(mTransaction->AsHttpTransactionParent() !=
                               nullptr);

    bool isTrr;
    bool echConfigUsed;
    mTransaction->GetNetworkAddresses(mSelfAddr, mPeerAddr, isTrr,
                                      mEffectiveTRRMode, mTRRSkipReason,
                                      echConfigUsed);
    StoreResolvedByTRR(isTrr);
    StoreEchConfigUsed(echConfigUsed);
  }

  // don't enter this block if we're reading from the cache...
  if (NS_SUCCEEDED(mStatus) && !mCachePump && mTransaction) {
    // mTransactionPump doesn't hit OnInputStreamReady and call this until
    // all of the response headers have been acquired, so we can take
    // ownership of them from the transaction.
    RefPtr<nsHttpConnectionInfo> connInfo;
    mResponseHead =
        mTransaction->TakeResponseHeadAndConnInfo(getter_AddRefs(connInfo));
    mSupportsHTTP3 = mTransaction->GetSupportsHTTP3();
    // the response head may be null if the transaction was cancelled.  in
    // which case we just need to call OnStartRequest/OnStopRequest.
    if (mResponseHead) {
      return ProcessResponse(connInfo);
    }

    NS_WARNING("No response head in OnStartRequest");
  }

  // cache file could be deleted on our behalf, it could contain errors or
  // it failed to allocate memory, reload from network here.
  if (mCacheEntry && mCachePump && RECOVER_FROM_CACHE_FILE_ERROR(mStatus)) {
    LOG(("  cache file error, reloading from server"));
    mCacheEntry->AsyncDoom(nullptr);
    rv =
        StartRedirectChannelToURI(mURI, nsIChannelEventSink::REDIRECT_INTERNAL);
    if (NS_SUCCEEDED(rv)) return NS_OK;
  }

  // If this is a system principal request to an essential domain and we
  // currently have connectivity, then check if there's a fallback domain we can
  // use to retry. If so we redirect to the fallback domain.
  if (StaticPrefs::network_essential_domains_fallback() && NS_FAILED(mStatus) &&
      !mCanceled && mLoadInfo->TriggeringPrincipal()->IsSystemPrincipal() &&
      hasConnectivity()) {
    if (nsCOMPtr<nsIURI> fallbackURI = GetFallbackURI(mURI)) {
      rv = StartRedirectChannelToURI(
          fallbackURI, nsIChannelEventSink::REDIRECT_INTERNAL |
                           nsIChannelEventSink::REDIRECT_TRANSPARENT);
      if (NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsIObserverService> obsService =
            services::GetObserverService();
        if (obsService)
          obsService->NotifyObservers(static_cast<nsIHttpChannel*>(this),
                                      "httpchannel-fallback", nullptr);
        return NS_OK;
      }
    }
  }

  // avoid crashing if mListener happens to be null...
  if (!mListener) {
    MOZ_ASSERT_UNREACHABLE("mListener is null");
    return NS_OK;
  }

  rv = ProcessCrossOriginSecurityHeaders();
  if (NS_FAILED(rv)) {
    mStatus = rv;
    HandleAsyncAbort();
    return rv;
  }

  // No process change is needed, so continue on to ContinueOnStartRequest1.
  return ContinueOnStartRequest1(rv);
}

nsresult nsHttpChannel::ContinueOnStartRequest1(nsresult result) {
  nsresult rv;

  // if process selection failed, cancel this load.
  if (NS_FAILED(result) && !mCanceled) {
    Cancel(result);
    return CallOnStartRequest();
  }

  // before we start any content load, check for redirectTo being called
  // this code is executed mainly before we start load from the cache
  if (mAPIRedirectTo && !mCanceled) {
    nsAutoCString redirectToSpec;
    mAPIRedirectTo->first()->GetAsciiSpec(redirectToSpec);
    LOG(("  redirectTo called with uri=%s", redirectToSpec.BeginReading()));

    MOZ_ASSERT(!LoadOnStartRequestCalled());
    PushRedirectAsyncFunc(&nsHttpChannel::ContinueOnStartRequest2);
    rv = StartRedirectChannelToURI(
        mAPIRedirectTo->first(),
        mAPIRedirectTo->second() ? nsIChannelEventSink::REDIRECT_TEMPORARY |
                                       nsIChannelEventSink::REDIRECT_TRANSPARENT
                                 : nsIChannelEventSink::REDIRECT_TEMPORARY);
    mAPIRedirectTo = Nothing();
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }
    PopRedirectAsyncFunc(&nsHttpChannel::ContinueOnStartRequest2);
  }

  // Hack: ContinueOnStartRequest2 uses NS_OK to detect successful redirects,
  // so we distinguish this codepath (a non-redirect that's processing
  // normally) by passing in a bogus error code.
  return ContinueOnStartRequest2(NS_BINDING_FAILED);
}

nsresult nsHttpChannel::ContinueOnStartRequest2(nsresult result) {
  if (NS_SUCCEEDED(result)) {
    // Redirect has passed through, we don't want to go on with this
    // channel.  It will now be canceled by the redirect handling code
    // that called this function.
    return NS_OK;
  }

  // on proxy errors, try to failover
  if (mConnectionInfo->ProxyInfo() &&
      (mStatus == NS_ERROR_PROXY_CONNECTION_REFUSED ||
       mStatus == NS_ERROR_UNKNOWN_PROXY_HOST ||
       mStatus == NS_ERROR_NET_TIMEOUT || mStatus == NS_ERROR_NET_RESET)) {
    PushRedirectAsyncFunc(&nsHttpChannel::ContinueOnStartRequest3);
    if (NS_SUCCEEDED(ProxyFailover())) {
      mProxyConnectResponseCode = 0;
      return NS_OK;
    }
    PopRedirectAsyncFunc(&nsHttpChannel::ContinueOnStartRequest3);
  }

  // Hack: ContinueOnStartRequest3 uses NS_OK to detect successful redirects,
  // so we distinguish this codepath (a non-redirect that's processing
  // normally) by passing in a bogus error code.
  return ContinueOnStartRequest3(NS_BINDING_FAILED);
}

nsresult nsHttpChannel::ContinueOnStartRequest3(nsresult result) {
  if (NS_SUCCEEDED(result)) {
    // Redirect has passed through, we don't want to go on with this
    // channel.  It will now be canceled by the redirect handling code
    // that called this function.
    return NS_OK;
  }

  return ContinueOnStartRequest4(NS_OK);
}

nsresult nsHttpChannel::ContinueOnStartRequest4(nsresult result) {
  LOG(("nsHttpChannel::ContinueOnStartRequest4 [this=%p]", this));

  if (NS_SUCCEEDED(mStatus) && mResponseHead && mAuthProvider) {
    uint32_t httpStatus = mResponseHead->Status();
    if (httpStatus != 401 && httpStatus != 407) {
      nsresult rv = mAuthProvider->CheckForSuperfluousAuth();
      if (NS_FAILED(rv)) {
        mStatus = rv;
        LOG(("  CheckForSuperfluousAuth failed (%08x)",
             static_cast<uint32_t>(rv)));
      }
    }
  }

  return CallOnStartRequest();
}

static void ReportHTTPSRRTelemetry(
    const Maybe<nsCOMPtr<nsIDNSHTTPSSVCRecord>>& aMaybeRecord) {
  bool hasHTTPSRR = aMaybeRecord && (aMaybeRecord.ref() != nullptr);
  if (!hasHTTPSRR) {
    mozilla::glean::networking::https_rr_presented.Get("none"_ns).Add(1);
    return;
  }

  const nsCOMPtr<nsIDNSHTTPSSVCRecord>& record = aMaybeRecord.ref();
  nsCOMPtr<nsISVCBRecord> svcbRecord;
  if (NS_SUCCEEDED(record->GetServiceModeRecord(false, false,
                                                getter_AddRefs(svcbRecord)))) {
    MOZ_ASSERT(svcbRecord);

    Maybe<std::tuple<nsCString, SupportedAlpnRank>> alpn =
        svcbRecord->GetAlpn();
    bool isHttp3 = alpn ? IsHttp3(std::get<1>(*alpn)) : false;
    mozilla::glean::networking::https_rr_presented
        .Get(isHttp3 ? "presented_with_http3"_ns : "presented"_ns)
        .Add(1);
  }
}

static void RecordHttpChanDispositionGlean(ChannelDisposition chanDisposition) {
  switch (chanDisposition) {
    case kHttpCanceled:
      mozilla::glean::networking::http_channel_disposition
          .Get("http_cancelled"_ns)
          .Add(1);
      break;
    case kHttpDisk:
      mozilla::glean::networking::http_channel_disposition.Get("http_disk"_ns)
          .Add(1);
      break;
    case kHttpNetOK:
      mozilla::glean::networking::http_channel_disposition.Get("http_net_ok"_ns)
          .Add(1);
      break;
    case kHttpNetEarlyFail:
      mozilla::glean::networking::http_channel_disposition
          .Get("http_net_early_fail"_ns)
          .Add(1);
      break;
    case kHttpNetLateFail:
      mozilla::glean::networking::http_channel_disposition
          .Get("http_net_late_fail"_ns)
          .Add(1);
      break;
    case kHttpsCanceled:
      mozilla::glean::networking::http_channel_disposition
          .Get("https_cancelled"_ns)
          .Add(1);
      break;
    case kHttpsDisk:
      mozilla::glean::networking::http_channel_disposition.Get("http_disk"_ns)
          .Add(1);
      break;
    case kHttpsNetOK:
      mozilla::glean::networking::http_channel_disposition
          .Get("https_net_ok"_ns)
          .Add(1);
      break;
    case kHttpsNetEarlyFail:
      mozilla::glean::networking::http_channel_disposition
          .Get("https_net_early_fail"_ns)
          .Add(1);
      break;
    case kHttpsNetLateFail:
      mozilla::glean::networking::http_channel_disposition
          .Get("https_net_late_fail"_ns)
          .Add(1);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown value for chanDisposition");
  }
}

static nsLiteralCString HttpChanDispositionToTelemetryLabel(
    Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE upgradeChanDisposition) {
  if (upgradeChanDisposition ==
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::cancel) {
    return "cancel"_ns;
  }
  if (upgradeChanDisposition ==
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::disk) {
    return "disk"_ns;
  }
  if (upgradeChanDisposition ==
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netOk) {
    return "net_ok"_ns;
  }
  if (upgradeChanDisposition ==
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netEarlyFail) {
    return "net_early_fail"_ns;
  }
  if (upgradeChanDisposition ==
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netLateFail) {
    return "net_late_fail"_ns;
  }

  MOZ_ASSERT_UNREACHABLE("Unknown value for upgradeChanDecomposition");
  return "other"_ns;
}

nsresult nsHttpChannel::LogConsoleError(const char* aTag) {
  nsCOMPtr<nsIConsoleService> console(mozilla::components::Console::Service());
  NS_ENSURE_TRUE(console, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<nsILoadInfo> loadInfo = LoadInfo();
  NS_ENSURE_TRUE(console, NS_ERROR_OUT_OF_MEMORY);
  uint64_t innerWindowID = loadInfo->GetInnerWindowID();

  nsAutoString errorText;
  nsresult rv = nsContentUtils::GetLocalizedString(
      nsContentUtils::eNECKO_PROPERTIES, aTag, errorText);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIScriptError> error(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));
  NS_ENSURE_TRUE(error, NS_ERROR_OUT_OF_MEMORY);

  rv =
      error->InitWithSourceURI(errorText, mURI, 0, 0, nsIScriptError::errorFlag,
                               "Invalid HTTP Status Lines"_ns, innerWindowID);
  NS_ENSURE_SUCCESS(rv, rv);
  console->LogMessage(error);
  return NS_OK;
}

static void RecordHTTPSUpgradeTelemetry(nsIURI* aURI, nsILoadInfo* aLoadInfo) {
  // we record https telemetry only for top-level loads
  if (aLoadInfo->GetExternalContentPolicyType() !=
      ExtContentPolicy::TYPE_DOCUMENT) {
    return;
  }

  // exempt loopback addresses because we only want to record telemetry
  // for actual web requests
  if (nsMixedContentBlocker::IsPotentiallyTrustworthyLoopbackURL(aURI)) {
    return;
  }

  // todo: for now we don't record form submissions, only
  // top-level document loads. Once Bug 1720500 is fixed, we can
  // consider recording form submissions too.
  if (aLoadInfo->GetIsFormSubmission()) {
    return;
  }

  bool isHTTPS = aURI->SchemeIs("https");

  nsILoadInfo::HTTPSUpgradeTelemetryType httpsTelemetry =
      aLoadInfo->GetHttpsUpgradeTelemetry();
  switch (httpsTelemetry) {
    case nsILoadInfo::NOT_INITIALIZED: {
      if (isHTTPS) {
        // Bug 1912222: We should never encounter NOT_INITIALIZED values,
        // though we still want to know whether those loads are HTTPS
        // or not. Eventually we'll want to remove this if-clause
        // again and only report "not_initialized".
        mozilla::glean::networking::http_to_https_upgrade_reason
            .Get("not_initialized_https"_ns)
            .Add(1);
        return;
      }
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("not_initialized"_ns)
          .Add(1);
      break;
    }
    case nsILoadInfo::NO_UPGRADE: {
      if (isHTTPS) {
        // Bug 1912222: We should rearely encounter NO_UPGRADE values, though
        // we still want to ensure those are not HTTPS. Eventually we'll want
        // to remove this if-clause again and only report "no_upgrade".
        mozilla::glean::networking::http_to_https_upgrade_reason
            .Get("no_upgrade_https"_ns)
            .Add(1);
        return;
      }
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("no_upgrade"_ns)
          .Add(1);
      break;
    }
    case nsILoadInfo::ALREADY_HTTPS:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("already_https"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HSTS:
      mozilla::glean::networking::http_to_https_upgrade_reason.Get("hsts"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_ONLY_UPGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_only_upgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_ONLY_UPGRADE_DOWNGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_only_upgrade_downgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_FIRST_UPGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_first_upgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_FIRST_UPGRADE_DOWNGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_first_upgrade_downgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_FIRST_SCHEMELESS_UPGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_first_schemeless_upgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_FIRST_SCHEMELESS_UPGRADE_DOWNGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_first_schemeless_upgrade_downgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::CSP_UIR:
      mozilla::glean::networking::http_to_https_upgrade_reason.Get("csp_uir"_ns)
          .Add(1);
      break;
    case nsILoadInfo::HTTPS_RR:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("https_rr"_ns)
          .Add(1);
      break;
    case nsILoadInfo::WEB_EXTENSION_UPGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("web_extension_upgrade"_ns)
          .Add(1);
      break;
    case nsILoadInfo::UPGRADE_EXCEPTION:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("upgrade_exception"_ns)
          .Add(1);
      break;
    case nsILoadInfo::SKIP_HTTPS_UPGRADE:
      mozilla::glean::networking::http_to_https_upgrade_reason
          .Get("skip_upgrade"_ns)
          .Add(1);
      break;
    default:
      MOZ_ASSERT(false, "what telemetry flag is set to end up here?");
  }
}

static void RecordIPAddressSpaceTelemetry(bool aLoadSuccess, nsIURI* aURI,
                                          nsILoadInfo* aLoadInfo,
                                          NetAddr& aPeerAddr) {
  bool isIpAddrAny = false;

  if (NS_SUCCEEDED(gIOService->HostnameIsIPAddressAny(aURI, &isIpAddrAny)) &&
      isIpAddrAny) {
    if (aLoadSuccess) {
      mozilla::glean::networking::http_ip_addr_any_hostnames.Get("sucess"_ns)
          .Add(1);
    } else {
      mozilla::glean::networking::http_ip_addr_any_hostnames.Get("failure"_ns)
          .Add(1);
    }
  }

  // if the load was not successful, then there is nothing to record here
  if (!aLoadSuccess) {
    return;
  }

  // we record https telemetry only for top-level loads
  if (aLoadInfo->GetExternalContentPolicyType() !=
      ExtContentPolicy::TYPE_DOCUMENT) {
    return;
  }

  if (aURI->SchemeIs("https")) {
    mozilla::glean::networking::https_http_or_local.Get("load_is_https"_ns)
        .Add(1);
    return;
  }

  if (aURI->SchemeIs("http")) {
    if (aPeerAddr.IsIPAddrLocal() || aPeerAddr.IsLoopbackAddr()) {
      mozilla::glean::networking::https_http_or_local
          .Get("load_is_http_for_local_domain"_ns)
          .Add(1);
    } else {
      mozilla::glean::networking::https_http_or_local.Get("load_is_http"_ns)
          .Add(1);
    }
    return;
  }
}

NS_IMETHODIMP
nsHttpChannel::OnStopRequest(nsIRequest* request, nsresult status) {
  MOZ_ASSERT(!mAsyncOpenTime.IsNull());
  AUTO_PROFILER_LABEL("nsHttpChannel::OnStopRequest", NETWORK);
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnStopRequest", NETWORK,
                            Flow::FromPointer(this));

  LOG(("nsHttpChannel::OnStopRequest [this=%p request=%p status=%" PRIx32 "]\n",
       this, request, static_cast<uint32_t>(status)));

  LOG(("OnStopRequest %p requestFromCache: %d mFirstResponseSource: %d\n", this,
       request == mCachePump, static_cast<int32_t>(mFirstResponseSource)));

  MOZ_ASSERT(NS_IsMainThread(),
             "OnStopRequest should only be called from the main thread");

  if (mStatus == NS_ERROR_PARSING_HTTP_STATUS_LINE) {
    Unused << LogConsoleError("InvalidHTTPResponseStatusLine");
  }

  if (WRONG_RACING_RESPONSE_SOURCE(request)) {
    return NS_OK;
  }

  // It's possible that LoadUseHTTPSSVC() is false, but we already have
  // mHTTPSSVCRecord.
  if (LoadUseHTTPSSVC() || mHTTPSSVCRecord) {
    ReportHTTPSRRTelemetry(mHTTPSSVCRecord);
  }

  // If this load failed because of a security error, it may be because we
  // are in a captive portal - trigger an async check to make sure.
  int32_t nsprError = -1 * NS_ERROR_GET_CODE(status);
  if (mozilla::psm::IsNSSErrorCode(nsprError) && IsHTTPS()) {
    gIOService->RecheckCaptivePortal();
  }

  if (request == mCachePump) {
    mCacheReadEnd = TimeStamp::Now();
  }

  // allow content to be cached if it was loaded successfully (bug #482935)
  bool contentComplete = NS_SUCCEEDED(status);

  // honor the cancelation status even if the underlying transaction
  // completed.
  if (mCanceled || NS_FAILED(mStatus)) status = mStatus;

  if (mLoadInfo->TriggeringPrincipal()->IsSystemPrincipal()) {
    ReportSystemChannelTelemetry(status);
  }

  if (LoadCachedContentIsPartial()) {
    if (NS_SUCCEEDED(status)) {
      // mTransactionPump should be suspended
      MOZ_ASSERT(request != mTransactionPump,
                 "byte-range transaction finished prematurely");

      if (request == mCachePump) {
        bool streamDone;
        status = OnDoneReadingPartialCacheEntry(&streamDone);
        if (NS_SUCCEEDED(status) && !streamDone) return status;
        // otherwise, fall through and fire OnStopRequest...
      } else if (request == mTransactionPump) {
        MOZ_ASSERT(LoadConcurrentCacheAccess());
      } else {
        MOZ_ASSERT_UNREACHABLE("unexpected request");
      }
    }
    // Do not to leave the transaction in a suspended state in error cases.
    if (NS_FAILED(status) && mTransaction) {
      nsresult rv = gHttpHandler->CancelTransaction(mTransaction, status);
      if (NS_FAILED(rv)) {
        LOG(("  CancelTransaction failed (%08x)", static_cast<uint32_t>(rv)));
      }
    }
  }

  nsCOMPtr<nsICompressConvStats> conv = do_QueryInterface(mCompressListener);
  if (conv) {
    conv->GetDecodedDataLength(&mDecodedBodySize);
  }

  bool isFromNet = request == mTransactionPump;

  if (mTransaction) {
    // determine if we should call DoAuthRetry
    bool authRetry = (mAuthRetryPending && NS_SUCCEEDED(status) &&
                      // we should only auth retry in this channel if are not
                      // redirecting a new channel for authentication retries
                      !StaticPrefs::network_auth_use_redirect_for_retries());

    StoreStronglyFramed(mTransaction->ResponseIsComplete());
    LOG(("nsHttpChannel %p has a strongly framed transaction: %d", this,
         LoadStronglyFramed()));

    // Save the reference of |mTransaction| to |transactionWithStickyConn|
    // when it has a sticky connection.
    // In the case we need to retry an authentication request, we need to
    // reuse the connection of |transactionWithStickyConn|.
    RefPtr<HttpTransactionShell> transactionWithStickyConn;
    if (mCaps & NS_HTTP_STICKY_CONNECTION ||
        mTransaction->HasStickyConnection()) {
      transactionWithStickyConn = mTransaction;
      // Make sure we use the updated caps and connection info from transaction.
      // We read these values when the transaction is already closed, so there
      // should be no race.
      if (mTransaction->Http2Disabled()) {
        mCaps |= NS_HTTP_DISALLOW_SPDY;
      }
      if (mTransaction->Http3Disabled()) {
        mCaps |= NS_HTTP_DISALLOW_HTTP3;
      }
      mConnectionInfo = mTransaction->GetConnInfo();
      LOG(("  transaction %p has sticky connection",
           transactionWithStickyConn.get()));
    }

    // this code relies on the code in nsHttpTransaction::Close, which
    // tests for NS_HTTP_STICKY_CONNECTION to determine whether or not to
    // keep the connection around after the transaction is finished.
    //
    LOG(("  mAuthRetryPending=%d, status=%" PRIx32 ", sticky conn cap=%d",
         static_cast<bool>(mAuthRetryPending), static_cast<uint32_t>(status),
         mCaps & NS_HTTP_STICKY_CONNECTION));
    // We must check caps for stickinness also on the transaction because it
    // might have been updated by the transaction itself during inspection of
    // the reposnse headers yet on the socket thread (found connection based
    // auth schema).

    if ((NS_FAILED(status)) && transactionWithStickyConn) {
      // Close (don't reuse) the sticky connection if this channel has been
      // cancelled. There are proxy servers known to get confused when we send
      // a new request over such a half-stated connection.
      if (!LoadAuthConnectionRestartable()) {
        LOG(("  not reusing a half-authenticated sticky connection"));
        transactionWithStickyConn->DontReuseConnection();
      }
    }

    if (mCaps & NS_HTTP_STICKY_CONNECTION) {
      mTransaction->SetH2WSConnRefTaken();
    }

    mTransferSize = mTransaction->GetTransferSize();
    mRequestSize = mTransaction->GetRequestSize();

    RecordHTTPSUpgradeTelemetry(mURI, mLoadInfo);

    RecordIPAddressSpaceTelemetry(NS_SUCCEEDED(mStatus), mURI, mLoadInfo,
                                  mPeerAddr);

    // If we are using the transaction to serve content, we also save the
    // time since async open in the cache entry so we can compare telemetry
    // between cache and net response.
    // Do not store the time of conditional requests because even if we
    // fetch the data from the server, the time includes loading of the old
    // cache entry which would skew the network load time.
    if (request == mTransactionPump && mCacheEntry && !mDidReval &&
        !LoadCustomConditionalRequest() && !mAsyncOpenTime.IsNull() &&
        !mOnStartRequestTimestamp.IsNull()) {
      uint64_t onStartTime =
          (mOnStartRequestTimestamp - mAsyncOpenTime).ToMilliseconds();
      uint64_t onStopTime =
          (TimeStamp::Now() - mAsyncOpenTime).ToMilliseconds();
      Unused << mCacheEntry->SetNetworkTimes(onStartTime, onStopTime);
    }

    mResponseTrailers = mTransaction->TakeResponseTrailers();

    if (nsIOService::UseSocketProcess() && mTransaction) {
      mOnStopRequestStartTime = mTransaction->GetOnStopRequestStartTime();
      if (!mOnStopRequestStartTime.IsNull()) {
        PerfStats::RecordMeasurement(
            PerfStats::Metric::OnStopRequestSocketToParent,
            TimeStamp::Now() - mOnStopRequestStartTime);
      }
    } else {
      mOnStopRequestStartTime = TimeStamp::Now();
    }

    // at this point, we're done with the transaction
    mTransactionTimings = mTransaction->Timings();
    mTransaction = nullptr;
    mTransactionPump = nullptr;

    // We no longer need the dns prefetch object
    if (mDNSPrefetch && mDNSPrefetch->TimingsValid() &&
        !mTransactionTimings.requestStart.IsNull() &&
        !mTransactionTimings.connectStart.IsNull() &&
        mDNSPrefetch->EndTimestamp() <= mTransactionTimings.connectStart) {
      // We only need the domainLookup timestamps when not using a
      // persistent connection, meaning if the endTimestamp < connectStart
      mTransactionTimings.domainLookupStart = mDNSPrefetch->StartTimestamp();
      mTransactionTimings.domainLookupEnd = mDNSPrefetch->EndTimestamp();
    }
    mDNSPrefetch = nullptr;

    // handle auth retry...
    if (authRetry) {
      mAuthRetryPending = false;
      auto continueOSR = [authRetry, isFromNet, contentComplete,
                          transactionWithStickyConn](auto* self,
                                                     nsresult aStatus) {
        return self->ContinueOnStopRequestAfterAuthRetry(
            aStatus, authRetry, isFromNet, contentComplete,
            transactionWithStickyConn);
      };
      status = DoAuthRetry(transactionWithStickyConn, continueOSR);
      if (NS_SUCCEEDED(status)) {
        return NS_OK;
      }
    }
    return ContinueOnStopRequestAfterAuthRetry(status, authRetry, isFromNet,
                                               contentComplete,
                                               transactionWithStickyConn);
  }

  return ContinueOnStopRequest(status, isFromNet, contentComplete);
}

nsresult nsHttpChannel::ContinueOnStopRequestAfterAuthRetry(
    nsresult aStatus, bool aAuthRetry, bool aIsFromNet, bool aContentComplete,
    HttpTransactionShell* aTransWithStickyConn) {
  LOG(
      ("nsHttpChannel::ContinueOnStopRequestAfterAuthRetry "
       "[this=%p, aStatus=%" PRIx32
       " aAuthRetry=%d, aIsFromNet=%d, aTransWithStickyConn=%p]\n",
       this, static_cast<uint32_t>(aStatus), aAuthRetry, aIsFromNet,
       aTransWithStickyConn));

  if (aAuthRetry && NS_SUCCEEDED(aStatus)) {
    return NS_OK;
  }

  // If DoAuthRetry failed, or if we have been cancelled since showing
  // the auth. dialog, then we need to send OnStartRequest now
  if (aAuthRetry || (mAuthRetryPending && NS_FAILED(aStatus))) {
    MOZ_ASSERT(NS_FAILED(aStatus), "should have a failure code here");
    // NOTE: since we have a failure status, we can ignore the return
    // value from onStartRequest.
    LOG(("  calling mListener->OnStartRequest [this=%p, listener=%p]\n", this,
         mListener.get()));
    if (mListener) {
      MOZ_ASSERT(!LoadOnStartRequestCalled(),
                 "We should not call OnStartRequest twice.");
      if (!LoadOnStartRequestCalled()) {
        nsCOMPtr<nsIStreamListener> listener(mListener);
        StoreOnStartRequestCalled(true);
        listener->OnStartRequest(this);
      }
    } else {
      StoreOnStartRequestCalled(true);
      NS_WARNING("OnStartRequest skipped because of null listener");
    }
    mAuthRetryPending = false;
  }

  // if this transaction has been replaced, then bail.
  if (LoadTransactionReplaced()) {
    LOG(("Transaction replaced\n"));
    // This was just the network check for a 304 response.
    mFirstResponseSource = RESPONSE_PENDING;
    return NS_OK;
  }

  bool upgradeWebsocket = mUpgradeProtocolCallback && aTransWithStickyConn &&
                          mResponseHead &&
                          ((mResponseHead->Status() == 101 &&
                            mResponseHead->Version() == HttpVersion::v1_1) ||
                           (mResponseHead->Status() == 200 &&
                            mResponseHead->Version() == HttpVersion::v2_0));

  bool upgradeConnect = mUpgradeProtocolCallback && aTransWithStickyConn &&
                        (mCaps & NS_HTTP_CONNECT_ONLY) && mResponseHead &&
                        mResponseHead->Status() == 200;

  if (upgradeWebsocket || upgradeConnect) {
    if (nsIOService::UseSocketProcess() && upgradeConnect) {
      // TODO: Support connection upgrade for socket process in bug 1632809.
      Unused << mUpgradeProtocolCallback->OnUpgradeFailed(
          NS_ERROR_NOT_IMPLEMENTED);
      return ContinueOnStopRequest(aStatus, aIsFromNet, aContentComplete);
    }

    nsresult rv = gHttpHandler->CompleteUpgrade(aTransWithStickyConn,
                                                mUpgradeProtocolCallback);
    mUpgradeProtocolCallback = nullptr;
    if (NS_FAILED(rv)) {
      LOG(("  CompleteUpgrade failed with %" PRIx32,
           static_cast<uint32_t>(rv)));

      // This ensures that WebSocketChannel::OnStopRequest will be
      // called with an error so the session is properly aborted.
      aStatus = rv;
    }
  }

  return ContinueOnStopRequest(aStatus, aIsFromNet, aContentComplete);
}

nsresult nsHttpChannel::ContinueOnStopRequest(nsresult aStatus, bool aIsFromNet,
                                              bool aContentComplete) {
  LOG(
      ("nsHttpChannel::ContinueOnStopRequest "
       "[this=%p aStatus=%" PRIx32 ", aIsFromNet=%d]\n",
       this, static_cast<uint32_t>(aStatus), aIsFromNet));

  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::ContinueOnStopRequest", NETWORK,
                            Flow::FromPointer(this));
  // HTTP_CHANNEL_DISPOSITION TELEMETRY
  ChannelDisposition chanDisposition = kHttpCanceled;
  // HTTP_CHANNEL_DISPOSITION_UPGRADE TELEMETRY
  Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE upgradeChanDisposition =
      Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::cancel;

  // HTTP 0.9 is more likely to be an error than really 0.9, so count it that
  // way
  if (mCanceled) {
    chanDisposition = kHttpCanceled;
    upgradeChanDisposition =
        Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::cancel;
  } else if (!LoadUsedNetwork() ||
             (mRaceCacheWithNetwork &&
              mFirstResponseSource == RESPONSE_FROM_CACHE)) {
    chanDisposition = kHttpDisk;
    upgradeChanDisposition =
        Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::disk;
  } else if (NS_SUCCEEDED(aStatus) && mResponseHead &&
             mResponseHead->Version() != HttpVersion::v0_9) {
    chanDisposition = kHttpNetOK;
    upgradeChanDisposition =
        Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netOk;
  } else if (!mTransferSize) {
    chanDisposition = kHttpNetEarlyFail;
    upgradeChanDisposition =
        Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netEarlyFail;
  } else {
    chanDisposition = kHttpNetLateFail;
    upgradeChanDisposition =
        Telemetry::LABELS_HTTP_CHANNEL_DISPOSITION_UPGRADE::netLateFail;
  }
  // Browser upgrading only happens on HTTPS pages for mixed passive content
  // when upgrading is enabled.
  nsCString upgradeKey;
  nsLiteralCString upgradeChanDispositionLabel =
      HttpChanDispositionToTelemetryLabel(upgradeChanDisposition);
  if (IsHTTPS()) {
    // Browser upgrading is disabled and the content is already HTTPS
    upgradeKey = "disabledNoReason"_ns;
    // Checks "security.mixed_content.upgrade_display_content" is true
    if (StaticPrefs::security_mixed_content_upgrade_display_content()) {
      if (mLoadInfo->GetBrowserUpgradeInsecureRequests()) {
        // HTTP content the browser has upgraded to HTTPS
        mozilla::glean::networking::http_channel_disposition_enabled_upgrade
            .Get(upgradeChanDispositionLabel)
            .Add(1);
        upgradeKey = "enabledUpgrade"_ns;
      } else {
        // Content wasn't upgraded but is already HTTPS
        mozilla::glean::networking::http_channel_disposition_enabled_no_reason
            .Get(upgradeChanDispositionLabel)
            .Add(1);
        upgradeKey = "enabledNoReason"_ns;
      }
    } else {
      mozilla::glean::networking::http_channel_disposition_disabled_no_reason
          .Get(upgradeChanDispositionLabel)
          .Add(1);
    }
    // shift http to https disposition enums
    chanDisposition =
        static_cast<ChannelDisposition>(chanDisposition + kHttpsCanceled);
  } else if (mLoadInfo->GetBrowserWouldUpgradeInsecureRequests()) {
    // HTTP content the browser would upgrade to HTTPS if upgrading was
    // enabled
    mozilla::glean::networking::http_channel_disposition_disabled_upgrade
        .Get(upgradeChanDispositionLabel)
        .Add(1);
    upgradeKey = "disabledUpgrade"_ns;
  } else if (StaticPrefs::security_mixed_content_upgrade_display_content()) {
    // HTTP content that wouldn't upgrade
    mozilla::glean::networking::http_channel_disposition_enabled_wont
        .Get(upgradeChanDispositionLabel)
        .Add(1);
    upgradeKey = "enabledWont"_ns;
  } else {
    mozilla::glean::networking::http_channel_disposition_disabled_wont
        .Get(upgradeChanDispositionLabel)
        .Add(1);
    upgradeKey = "disabledWont"_ns;
  }

  Telemetry::AccumulateCategoricalKeyed(upgradeKey, upgradeChanDisposition);

  LOG(("  nsHttpChannel::OnStopRequest ChannelDisposition %d\n",
       chanDisposition));
  glean::http::channel_disposition.AccumulateSingleSample(chanDisposition);
  RecordHttpChanDispositionGlean(chanDisposition);

  // Collect specific telemetry for measuring image, video, audio
  // success/failure rates in regular browsing mode and when auto upgrading of
  // subresources is enabled. Note that we only evaluate actual image types, not
  // favicons.
  nsContentPolicyType internalLoadType;
  mLoadInfo->GetInternalContentPolicyType(&internalLoadType);
  bool statusIsSuccess = NS_SUCCEEDED(aStatus);
  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_IMAGE ||
      internalLoadType == nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD) {
    if (mLoadInfo->GetBrowserDidUpgradeInsecureRequests()) {
      glean::mixed_content::images
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::ImagesLabel::eImgupsuccess
                       : glean::mixed_content::ImagesLabel::eImgupfailure)
          .Add();
    } else {
      glean::mixed_content::images
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::ImagesLabel::eImgnoupsuccess
                       : glean::mixed_content::ImagesLabel::eImgnoupfailure)
          .Add();
    }
  }
  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_VIDEO) {
    if (mLoadInfo->GetBrowserDidUpgradeInsecureRequests()) {
      glean::mixed_content::video
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::VideoLabel::eVideoupsuccess
                       : glean::mixed_content::VideoLabel::eVideoupfailure)
          .Add();
    } else {
      glean::mixed_content::video
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::VideoLabel::eVideonoupsuccess
                       : glean::mixed_content::VideoLabel::eVideonoupfailure)
          .Add();
    }
  }
  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_AUDIO) {
    if (mLoadInfo->GetBrowserDidUpgradeInsecureRequests()) {
      glean::mixed_content::audio
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::AudioLabel::eAudioupsuccess
                       : glean::mixed_content::AudioLabel::eAudioupfailure)
          .Add();
    } else {
      glean::mixed_content::audio
          .EnumGet(statusIsSuccess
                       ? glean::mixed_content::AudioLabel::eAudionoupsuccess
                       : glean::mixed_content::AudioLabel::eAudionoupfailure)
          .Add();
    }
  }

  // if needed, check cache entry has all data we expect
  if (mCacheEntry && mCachePump && LoadConcurrentCacheAccess() &&
      aContentComplete) {
    int64_t size, contentLength;
    nsresult rv = CheckPartial(mCacheEntry, &size, &contentLength);
    if (NS_SUCCEEDED(rv)) {
      if (size == int64_t(-1)) {
        // mayhemer TODO - we have to restart read from cache here at the size
        // offset
        MOZ_ASSERT(false);
        LOG(
            ("  cache entry write is still in progress, but we just "
             "finished reading the cache entry"));
      } else if (contentLength != int64_t(-1) && contentLength != size) {
        LOG(("  concurrent cache entry write has been interrupted"));
        mCachedResponseHead = std::move(mResponseHead);
        // Ignore zero partial length because we also want to resume when
        // no data at all has been read from the cache.
        rv = MaybeSetupByteRangeRequest(size, contentLength, true);
        if (NS_SUCCEEDED(rv) && LoadIsPartialRequest()) {
          // Prevent read from cache again
          StoreCachedContentIsValid(CachedContentValidity::Invalid);
          StoreCachedContentIsPartial(1);

          // We are about to perform a different network request.
          // We must set mRaceCacheWithNetwork to false because otherwise
          // we would ignore the network response thinking we didn't need it.
          mRaceCacheWithNetwork = false;

          // Perform the range request
          rv = ContinueConnect();
          if (NS_SUCCEEDED(rv)) {
            LOG(("  performing range request"));
            mCachePump = nullptr;
            return NS_OK;
          }
          LOG(("  but range request perform failed 0x%08" PRIx32,
               static_cast<uint32_t>(rv)));
          aStatus = NS_ERROR_NET_INTERRUPT;
        } else {
          LOG(("  but range request setup failed rv=0x%08" PRIx32
               ", failing load",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  StoreIsPending(false);
  mStatus = aStatus;

  // perform any final cache operations before we close the cache entry.
  if (mCacheEntry && LoadRequestTimeInitialized()) {
    // New implementation just returns value of the !LoadCacheEntryIsReadOnly()
    // flag passed in. Old implementation checks on nsICache::ACCESS_WRITE
    // flag.

    // Assume that write access is granted
    if (!LoadCacheEntryIsReadOnly()) {
      nsresult rv = FinalizeCacheEntry();
      if (NS_FAILED(rv)) {
        LOG(("FinalizeCacheEntry failed (%08x)", static_cast<uint32_t>(rv)));
      }
    }
  }

  ReportRcwnStats(aIsFromNet);

  // Register entry to the PerformanceStorage resource timing
  MaybeReportTimingData();

  MaybeFlushConsoleReports();

  if (!mEndMarkerAdded && profiler_thread_is_being_profiled_for_markers()) {
    // These do allocations/frees/etc; avoid if not active
    mEndMarkerAdded = true;

    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    int32_t priority = PRIORITY_NORMAL;
    GetPriority(&priority);

    uint64_t size = 0;
    GetEncodedBodySize(&size);

    nsAutoCString contentType;
    mozilla::Maybe<mozilla::net::HttpVersion> httpVersion = Nothing();
    mozilla::Maybe<uint32_t> responseStatus = Nothing();
    if (mResponseHead) {
      mResponseHead->ContentType(contentType);
      httpVersion = Some(mResponseHead->Version());
      responseStatus = Some(mResponseHead->Status());
    }

    profiler_add_network_marker(
        mURI, requestMethod, priority, mChannelId, NetworkLoadType::LOAD_STOP,
        mLastStatusReported, TimeStamp::Now(), size, mCacheDisposition,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(),
        mClassOfService.Flags(), mStatus, &mTransactionTimings,
        std::move(mSource), httpVersion, responseStatus,
        Some(nsDependentCString(contentType.get())));
  }

  if (mAuthRetryPending &&
      StaticPrefs::network_auth_use_redirect_for_retries()) {
    nsresult rv = OpenRedirectChannel(aStatus);
    LOG(("Opening redirect channel for auth retry %x",
         static_cast<uint32_t>(rv)));
    if (NS_FAILED(rv)) {
      if (mListener) {
        MOZ_ASSERT(!LoadOnStartRequestCalled(),
                   "We should not call OnStartRequest twice.");
        if (!LoadOnStartRequestCalled()) {
          nsCOMPtr<nsIStreamListener> listener(mListener);
          StoreOnStartRequestCalled(true);
          listener->OnStartRequest(this);
        }
      } else {
        StoreOnStartRequestCalled(true);
        NS_WARNING("OnStartRequest skipped because of null listener");
      }
    }
    mAuthRetryPending = false;
  }

  if (StaticPrefs::network_http_network_error_logging_enabled() &&
      LoadUsedNetwork() && !mReportedNEL) {
    if (nsCOMPtr<nsINetworkErrorLogging> nel =
            components::NetworkErrorLogging::Service()) {
      nel->GenerateNELReport(this);
    }
    mReportedNEL = true;
  }

  // notify "http-on-before-stop-request" observers
  gHttpHandler->OnBeforeStopRequest(this);

  if (mListener) {
    LOG(("nsHttpChannel %p calling OnStopRequest\n", this));
    MOZ_ASSERT(LoadOnStartRequestCalled(),
               "OnStartRequest should be called before OnStopRequest");
    MOZ_ASSERT(!LoadOnStopRequestCalled(),
               "We should not call OnStopRequest twice");
    StoreOnStopRequestCalled(true);
    mListener->OnStopRequest(this, aStatus);
  }
  StoreOnStopRequestCalled(true);

  // The prefetch needs to be released on the main thread
  mDNSPrefetch = nullptr;

  mRedirectChannel = nullptr;

  // notify "http-on-stop-request" observers
  gHttpHandler->OnStopRequest(this);

  RemoveAsNonTailRequest();

  if (mChannelBlockedByOpaqueResponse && mCachedOpaqueResponseBlockingPref) {
    mResponseHead->ClearHeaders();
  }
  // If a preferred alt-data type was set, this signals the consumer is
  // interested in reading and/or writing the alt-data representation.
  // We need to hold a reference to the cache entry in case the listener calls
  // openAlternativeOutputStream() after CloseCacheEntry() clears mCacheEntry.
  if (!mPreferredCachedAltDataTypes.IsEmpty()) {
    mAltDataCacheEntry = mCacheEntry;
  }

  CloseCacheEntry(!aContentComplete);

  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, aStatus);
  }

  // We don't need this info anymore
  CleanRedirectCacheChainIfNecessary();

  ReleaseListeners();

  // Release mUploadStream to free some memory sooner.
  // We release this in background thread to avoid blocking I/O operations on
  // main thread See Bug 1940224
  Unused << NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "release HttpBaseChannel::mUploadStream",
      [uploadStream = std::move(mUploadStream)]() { Unused << uploadStream; }));

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIStreamListener
//-----------------------------------------------------------------------------

class OnTransportStatusAsyncEvent : public Runnable {
 public:
  OnTransportStatusAsyncEvent(nsITransportEventSink* aEventSink,
                              nsresult aTransportStatus, int64_t aProgress,
                              int64_t aProgressMax)
      : Runnable("net::OnTransportStatusAsyncEvent"),
        mEventSink(aEventSink),
        mTransportStatus(aTransportStatus),
        mProgress(aProgress),
        mProgressMax(aProgressMax) {
    MOZ_ASSERT(!NS_IsMainThread(), "Shouldn't be created on main thread");
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "Should run on main thread");
    if (mEventSink) {
      mEventSink->OnTransportStatus(nullptr, mTransportStatus, mProgress,
                                    mProgressMax);
    }
    return NS_OK;
  }

 private:
  nsCOMPtr<nsITransportEventSink> mEventSink;
  nsresult mTransportStatus;
  int64_t mProgress;
  int64_t mProgressMax;
};

NS_IMETHODIMP
nsHttpChannel::OnDataAvailable(nsIRequest* request, nsIInputStream* input,
                               uint64_t offset, uint32_t count) {
  nsresult rv;
  AUTO_PROFILER_LABEL("nsHttpChannel::OnDataAvailable", NETWORK);
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnDataAvailable", NETWORK,
                            Flow::FromPointer(this));

  LOG(("nsHttpChannel::OnDataAvailable [this=%p request=%p offset=%" PRIu64
       " count=%" PRIu32 "]\n",
       this, request, offset, count));

  LOG(("  requestFromCache: %d mFirstResponseSource: %d\n",
       request == mCachePump, static_cast<int32_t>(mFirstResponseSource)));

  // don't send out OnDataAvailable notifications if we've been canceled.
  if (mCanceled) return mStatus;

  if (mAuthRetryPending || WRONG_RACING_RESPONSE_SOURCE(request) ||
      (request == mTransactionPump && LoadTransactionReplaced())) {
    uint32_t n;
    return input->ReadSegments(NS_DiscardSegment, nullptr, count, &n);
  }

  MOZ_ASSERT(mResponseHead, "No response head in ODA!!");

  MOZ_ASSERT(!(LoadCachedContentIsPartial() && (request == mTransactionPump)),
             "transaction pump not suspended");

  mIsReadingFromCache = (request == mCachePump);

  if (mListener) {
    //
    // synthesize transport progress event.  we do this here since we want
    // to delay OnProgress events until we start streaming data.  this is
    // crucially important since it impacts the lock icon (see bug 240053).
    //
    nsresult transportStatus;
    if (request == mCachePump) {
      transportStatus = NS_NET_STATUS_READING;
    } else {
      transportStatus = NS_NET_STATUS_RECEIVING_FROM;
    }

    // mResponseHead may reference new or cached headers, but either way it
    // holds our best estimate of the total content length.  Even in the case
    // of a byte range request, the content length stored in the cached
    // response headers is what we want to use here.

    int64_t progressMax = -1;
    rv = GetContentLength(&progressMax);
    if (NS_FAILED(rv)) {
      NS_WARNING("GetContentLength failed");
    }
    int64_t progress = mLogicalOffset + count;

    if ((progress > progressMax) && (progressMax != -1)) {
      NS_WARNING(
          "unexpected progress values - "
          "is server exceeding content length?");
    }

    // make sure params are in range for js
    if (!InScriptableRange(progressMax)) {
      progressMax = -1;
    }

    if (!InScriptableRange(progress)) {
      progress = -1;
    }

    if (NS_IsMainThread()) {
      OnTransportStatus(nullptr, transportStatus, progress, progressMax);
    } else {
      rv = NS_DispatchToMainThread(new OnTransportStatusAsyncEvent(
          this, transportStatus, progress, progressMax));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    //
    // we have to manually keep the logical offset of the stream up-to-date.
    // we cannot depend solely on the offset provided, since we may have
    // already streamed some data from another source (see, for example,
    // OnDoneReadingPartialCacheEntry).
    //
    int64_t offsetBefore = 0;
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(input);
    if (seekable && NS_FAILED(seekable->Tell(&offsetBefore))) {
      seekable = nullptr;
    }

    if (nsIOService::UseSocketProcess() && mTransaction) {
      mOnDataAvailableStartTime = mTransaction->GetDataAvailableStartTime();
      if (!mOnDataAvailableStartTime.IsNull()) {
        PerfStats::RecordMeasurement(
            PerfStats::Metric::OnDataAvailableSocketToParent,
            TimeStamp::Now() - mOnDataAvailableStartTime);
      }
    } else {
      mOnDataAvailableStartTime = TimeStamp::Now();
    }
    nsresult rv =
        mListener->OnDataAvailable(this, input, mLogicalOffset, count);
    if (NS_SUCCEEDED(rv)) {
      // by contract mListener must read all of "count" bytes, but
      // nsInputStreamPump is tolerant to seekable streams that violate that
      // and it will redeliver incompletely read data. So we need to do
      // the same thing when updating the progress counter to stay in sync.
      int64_t offsetAfter, delta;
      if (seekable && NS_SUCCEEDED(seekable->Tell(&offsetAfter))) {
        delta = offsetAfter - offsetBefore;
        if (delta != count) {
          count = delta;

          NS_WARNING("Listener OnDataAvailable contract violation");
          nsCOMPtr<nsIConsoleService> consoleService;
          consoleService = mozilla::components::Console::Service();
          nsAutoString message(nsLiteralString(
              u"http channel Listener OnDataAvailable contract violation"));
          if (consoleService) {
            consoleService->LogStringMessage(message.get());
          }
        }
      }
      mLogicalOffset += count;
    }

    return rv;
  }

  return NS_ERROR_ABORT;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIThreadRetargetableRequest
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::RetargetDeliveryTo(nsISerialEventTarget* aNewTarget) {
  MOZ_ASSERT(NS_IsMainThread(), "Should be called on main thread only");

  NS_ENSURE_ARG(aNewTarget);
  if (aNewTarget->IsOnCurrentThread()) {
    NS_WARNING("Retargeting delivery to same thread");
    return NS_OK;
  }
  if (!mTransactionPump && !mCachePump) {
    LOG(("nsHttpChannel::RetargetDeliveryTo %p %p no pump available\n", this,
         aNewTarget));
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = NS_OK;
  // If both cache pump and transaction pump exist, we're probably dealing
  // with partially cached content. So, we must be able to retarget both.
  nsCOMPtr<nsIThreadRetargetableRequest> retargetableCachePump;
  nsCOMPtr<nsIThreadRetargetableRequest> retargetableTransactionPump;
  if (mCachePump) {
    retargetableCachePump = do_QueryObject(mCachePump);
    // nsInputStreamPump should implement this interface.
    MOZ_ASSERT(retargetableCachePump);
    rv = retargetableCachePump->RetargetDeliveryTo(aNewTarget);
  }
  if (NS_SUCCEEDED(rv) && mTransactionPump) {
    retargetableTransactionPump = do_QueryObject(mTransactionPump);
    // nsInputStreamPump should implement this interface.
    MOZ_ASSERT(retargetableTransactionPump);
    rv = retargetableTransactionPump->RetargetDeliveryTo(aNewTarget);

    // If retarget fails for transaction pump, we must restore mCachePump.
    if (NS_FAILED(rv) && retargetableCachePump) {
      nsCOMPtr<nsISerialEventTarget> main = GetMainThreadSerialEventTarget();
      NS_ENSURE_TRUE(main, NS_ERROR_UNEXPECTED);
      rv = retargetableCachePump->RetargetDeliveryTo(main);
    }
  }
  return rv;
}

NS_IMETHODIMP
nsHttpChannel::GetDeliveryTarget(nsISerialEventTarget** aEventTarget) {
  if (mCachePump) {
    return mCachePump->GetDeliveryTarget(aEventTarget);
  }
  if (mTransactionPump) {
    nsCOMPtr<nsIThreadRetargetableRequest> request =
        do_QueryInterface(mTransactionPump);
    return request->GetDeliveryTarget(aEventTarget);
  }
  return NS_ERROR_NOT_AVAILABLE;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsThreadRetargetableStreamListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::CheckListenerChain() {
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread!");
  nsresult rv = NS_OK;
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mListener, &rv);
  if (retargetableListener) {
    rv = retargetableListener->CheckListenerChain();
  }
  return rv;
}

NS_IMETHODIMP
nsHttpChannel::OnDataFinished(nsresult aStatus) {
  nsCOMPtr<nsIThreadRetargetableStreamListener> listener =
      do_QueryInterface(mListener);

  if (listener) {
    return listener->OnDataFinished(aStatus);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsITransportEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnTransportStatus(nsITransport* trans, nsresult status,
                                 int64_t progress, int64_t progressMax) {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread only");
  // cache the progress sink so we don't have to query for it each time.
  if (!mProgressSink) GetCallback(mProgressSink);

  mLastTransportStatus = status;
  if (status == NS_NET_STATUS_CONNECTED_TO ||
      status == NS_NET_STATUS_WAITING_FOR) {
    bool isTrr = false;
    bool echConfigUsed = false;
    if (mTransaction) {
      mTransaction->GetNetworkAddresses(mSelfAddr, mPeerAddr, isTrr,
                                        mEffectiveTRRMode, mTRRSkipReason,
                                        echConfigUsed);

    } else {
      nsCOMPtr<nsISocketTransport> socketTransport = do_QueryInterface(trans);
      if (socketTransport) {
        socketTransport->GetPeerAddr(&mPeerAddr);
        socketTransport->GetSelfAddr(&mSelfAddr);
        socketTransport->ResolvedByTRR(&isTrr);
        socketTransport->GetEffectiveTRRMode(&mEffectiveTRRMode);
        socketTransport->GetEchConfigUsed(&echConfigUsed);
      }
    }

    StoreResolvedByTRR(isTrr);
    StoreEchConfigUsed(echConfigUsed);
  }

  // block socket status event after Cancel or OnStopRequest has been called.
  if (mProgressSink && NS_SUCCEEDED(mStatus) && LoadIsPending()) {
    LOG(("sending progress%s notification [this=%p status=%" PRIx32
         " progress=%" PRId64 "/%" PRId64 "]\n",
         (mLoadFlags & LOAD_BACKGROUND) ? "" : " and status", this,
         static_cast<uint32_t>(status), progress, progressMax));

    nsAutoCString host;
    mURI->GetHost(host);
    if (!(mLoadFlags & LOAD_BACKGROUND)) {
      mProgressSink->OnStatus(this, status, NS_ConvertUTF8toUTF16(host).get());
    } else {
      nsCOMPtr<nsIParentChannel> parentChannel;
      NS_QueryNotificationCallbacks(this, parentChannel);
      // If the event sink is |HttpChannelParent|, we have to send status
      // events to it even if LOAD_BACKGROUND is set. |HttpChannelParent|
      // needs to be aware of whether the status is
      // |NS_NET_STATUS_RECEIVING_FROM| or |NS_NET_STATUS_READING|.
      // LOAD_BACKGROUND is checked again in |HttpChannelChild|, so the final
      // consumer won't get this event.
      if (SameCOMIdentity(parentChannel, mProgressSink)) {
        mProgressSink->OnStatus(this, status,
                                NS_ConvertUTF8toUTF16(host).get());
      }
    }

    if (progress > 0) {
      if ((progress > progressMax) && (progressMax != -1)) {
        NS_WARNING("unexpected progress values");
      }

      // Try to get mProgressSink if it was nulled out during OnStatus.
      if (!mProgressSink) {
        GetCallback(mProgressSink);
      }
      if (mProgressSink) {
        mProgressSink->OnProgress(this, progress, progressMax);
      }
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsICacheInfoChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::IsFromCache(bool* value) {
  if (!LoadIsPending()) return NS_ERROR_NOT_AVAILABLE;

  if (!mRaceCacheWithNetwork) {
    // return false if reading a partial cache entry; the data isn't
    // entirely from the cache!
    *value = (mCachePump || (mLoadFlags & LOAD_ONLY_IF_MODIFIED)) &&
             CachedContentIsValid() && !LoadCachedContentIsPartial();
    return NS_OK;
  }

  // If we are racing network and cache (or skipping the cache)
  // we just return the first response source.
  *value = mFirstResponseSource == RESPONSE_FROM_CACHE;

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheEntryId(uint64_t* aCacheEntryId) {
  bool fromCache = false;
  if (NS_FAILED(IsFromCache(&fromCache)) || !fromCache || !mCacheEntry ||
      NS_FAILED(mCacheEntry->GetCacheEntryId(aCacheEntryId))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheTokenFetchCount(uint32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  nsCOMPtr<nsICacheEntry> cacheEntry =
      mCacheEntry ? mCacheEntry : mAltDataCacheEntry;
  if (!cacheEntry) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return cacheEntry->GetFetchCount(_retval);
}

NS_IMETHODIMP
nsHttpChannel::GetCacheTokenExpirationTime(uint32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  if (!mCacheEntry) return NS_ERROR_NOT_AVAILABLE;

  return mCacheEntry->GetExpirationTime(_retval);
}

NS_IMETHODIMP
nsHttpChannel::SetAllowStaleCacheContent(bool aAllowStaleCacheContent) {
  LOG(("nsHttpChannel::SetAllowStaleCacheContent [this=%p, allow=%d]", this,
       aAllowStaleCacheContent));
  StoreAllowStaleCacheContent(aAllowStaleCacheContent);
  return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::GetAllowStaleCacheContent(bool* aAllowStaleCacheContent) {
  NS_ENSURE_ARG(aAllowStaleCacheContent);
  *aAllowStaleCacheContent = LoadAllowStaleCacheContent();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetForceValidateCacheContent(bool aForceValidateCacheContent) {
  LOG(("nsHttpChannel::SetForceValidateCacheContent [this=%p, allow=%d]", this,
       aForceValidateCacheContent));
  StoreForceValidateCacheContent(aForceValidateCacheContent);
  return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::GetForceValidateCacheContent(bool* aForceValidateCacheContent) {
  NS_ENSURE_ARG(aForceValidateCacheContent);
  *aForceValidateCacheContent = LoadForceValidateCacheContent();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetPreferCacheLoadOverBypass(bool aPreferCacheLoadOverBypass) {
  StorePreferCacheLoadOverBypass(aPreferCacheLoadOverBypass);
  return NS_OK;
}
NS_IMETHODIMP
nsHttpChannel::GetPreferCacheLoadOverBypass(bool* aPreferCacheLoadOverBypass) {
  NS_ENSURE_ARG(aPreferCacheLoadOverBypass);
  *aPreferCacheLoadOverBypass = LoadPreferCacheLoadOverBypass();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::PreferAlternativeDataType(
    const nsACString& aType, const nsACString& aContentType,
    PreferredAlternativeDataDeliveryType aDeliverAltData) {
  ENSURE_CALLED_BEFORE_ASYNC_OPEN();
  mPreferredCachedAltDataTypes.AppendElement(PreferredAlternativeDataTypeParams(
      nsCString(aType), nsCString(aContentType), aDeliverAltData));
  return NS_OK;
}

const nsTArray<PreferredAlternativeDataTypeParams>&
nsHttpChannel::PreferredAlternativeDataTypes() {
  return mPreferredCachedAltDataTypes;
}

NS_IMETHODIMP
nsHttpChannel::GetAlternativeDataType(nsACString& aType) {
  // must be called during or after OnStartRequest
  if (!LoadAfterOnStartRequestBegun()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aType = mAvailableCachedAltDataType;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::OpenAlternativeOutputStream(const nsACString& type,
                                           int64_t predictedSize,
                                           nsIAsyncOutputStream** _retval) {
  // OnStopRequest will clear mCacheEntry, but we may use mAltDataCacheEntry
  // if the consumer called PreferAlternativeDataType()
  nsCOMPtr<nsICacheEntry> cacheEntry =
      mCacheEntry ? mCacheEntry : mAltDataCacheEntry;
  if (!cacheEntry) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv =
      cacheEntry->OpenAlternativeOutputStream(type, predictedSize, _retval);
  if (NS_SUCCEEDED(rv)) {
    // Clear this metadata flag in case it exists.
    // The caller of this method may set it again.
    cacheEntry->SetMetaDataElement("alt-data-from-child", nullptr);
  }
  return rv;
}

NS_IMETHODIMP
nsHttpChannel::GetOriginalInputStream(nsIInputStreamReceiver* aReceiver) {
  if (aReceiver == nullptr) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIInputStream> inputStream;

  nsCOMPtr<nsICacheEntry> cacheEntry =
      mCacheEntry ? mCacheEntry : mAltDataCacheEntry;
  if (cacheEntry) {
    cacheEntry->OpenInputStream(0, getter_AddRefs(inputStream));
  }
  aReceiver->OnInputStreamReady(inputStream);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetAlternativeDataInputStream(nsIInputStream** aInputStream) {
  NS_ENSURE_ARG_POINTER(aInputStream);

  *aInputStream = nullptr;

  nsCOMPtr<nsICacheEntry> cacheEntry =
      mCacheEntry ? mCacheEntry : mAltDataCacheEntry;
  if (!mAvailableCachedAltDataType.IsEmpty() && cacheEntry) {
    nsresult rv = cacheEntry->OpenAlternativeInputStream(
        mAvailableCachedAltDataType, aInputStream);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsICachingChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::IsRacing(bool* aIsRacing) {
  if (!LoadAfterOnStartRequestBegun()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aIsRacing = mRaceCacheWithNetwork;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheToken(nsISupports** token) {
  NS_ENSURE_ARG_POINTER(token);
  if (!mCacheEntry) return NS_ERROR_NOT_AVAILABLE;
  return CallQueryInterface(mCacheEntry, token);
}

NS_IMETHODIMP
nsHttpChannel::SetCacheToken(nsISupports* token) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheKey(uint32_t* key) {
  NS_ENSURE_ARG_POINTER(key);

  LOG(("nsHttpChannel::GetCacheKey [this=%p]\n", this));

  *key = mPostID;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetCacheKey(uint32_t key) {
  LOG(("nsHttpChannel::SetCacheKey [this=%p key=%u]\n", this, key));

  ENSURE_CALLED_BEFORE_CONNECT();

  mPostID = key;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetCacheOnlyMetadata(bool* aOnlyMetadata) {
  NS_ENSURE_ARG(aOnlyMetadata);
  *aOnlyMetadata = LoadCacheOnlyMetadata();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetCacheOnlyMetadata(bool aOnlyMetadata) {
  LOG(("nsHttpChannel::SetCacheOnlyMetadata [this=%p only-metadata=%d]\n", this,
       aOnlyMetadata));

  ENSURE_CALLED_BEFORE_ASYNC_OPEN();

  StoreCacheOnlyMetadata(aOnlyMetadata);
  if (aOnlyMetadata) {
    mLoadFlags |= LOAD_ONLY_IF_MODIFIED;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::GetPin(bool* aPin) {
  NS_ENSURE_ARG(aPin);
  *aPin = LoadPinCacheContent();
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetPin(bool aPin) {
  LOG(("nsHttpChannel::SetPin [this=%p pin=%d]\n", this, aPin));

  ENSURE_CALLED_BEFORE_CONNECT();

  StorePinCacheContent(aPin);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::ForceCacheEntryValidFor(uint32_t aSecondsToTheFuture) {
  if (!mCacheEntry) {
    LOG(
        ("nsHttpChannel::ForceCacheEntryValidFor found no cache entry "
         "for this channel [this=%p].",
         this));
  } else {
    mCacheEntry->ForceValidFor(aSecondsToTheFuture);

    nsAutoCString key;
    mCacheEntry->GetKey(key);

    LOG(
        ("nsHttpChannel::ForceCacheEntryValidFor successfully forced valid "
         "entry with key %s for %d seconds. [this=%p]",
         key.get(), aSecondsToTheFuture, this));
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIResumableChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::ResumeAt(uint64_t aStartPos, const nsACString& aEntityID) {
  LOG(("nsHttpChannel::ResumeAt [this=%p startPos=%" PRIu64 " id='%s']\n", this,
       aStartPos, PromiseFlatCString(aEntityID).get()));
  mEntityID = aEntityID;
  mStartPos = aStartPos;
  StoreResuming(true);
  return NS_OK;
}

nsresult nsHttpChannel::DoAuthRetry(
    HttpTransactionShell* aTransWithStickyConn,
    const std::function<nsresult(nsHttpChannel*, nsresult)>&
        aContinueOnStopRequestFunc) {
  LOG(("nsHttpChannel::DoAuthRetry [this=%p, aTransWithStickyConn=%p]\n", this,
       aTransWithStickyConn));

  MOZ_ASSERT(!mTransaction, "should not have a transaction");

  // Note that we don't have to toggle |IsPending| anymore. See the reasons
  // below.
  // 1. We can't suspend the channel during "http-on-modify-request"
  // when |IsPending| is false.
  // 2. We don't check |IsPending| in SetRequestHeader now.

  // Reset RequestObserversCalled because we've probably called the request
  // observers once already.
  StoreRequestObserversCalled(false);

  // fetch cookies, and add them to the request header.
  // the server response could have included cookies that must be sent with
  // this authentication attempt (bug 84794).
  // TODO: save cookies from auth response and send them here (bug 572151).
  AddCookiesToRequest();

  // notify "http-on-modify-request" observers
  CallOnModifyRequestObservers();

  RefPtr<HttpTransactionShell> trans(aTransWithStickyConn);
  return CallOrWaitForResume(
      [trans{std::move(trans)}, aContinueOnStopRequestFunc](auto* self) {
        return self->ContinueDoAuthRetry(trans, aContinueOnStopRequestFunc);
      });
}

nsresult nsHttpChannel::ContinueDoAuthRetry(
    HttpTransactionShell* aTransWithStickyConn,
    const std::function<nsresult(nsHttpChannel*, nsresult)>&
        aContinueOnStopRequestFunc) {
  LOG(("nsHttpChannel::ContinueDoAuthRetry [this=%p]\n", this));
  StoreIsPending(true);

  // get rid of the old response headers
  mResponseHead = nullptr;

  // rewind the upload stream
  if (mUploadStream) {
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mUploadStream);
    nsresult rv = NS_ERROR_NO_INTERFACE;
    if (seekable) {
      rv = seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    }

    // This should not normally happen, but it's possible that big memory
    // blobs originating in the other process can't be rewinded.
    // In that case we just fail the request, otherwise the content length
    // will not match and this load will never complete.
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // always set sticky connection flag
  mCaps |= NS_HTTP_STICKY_CONNECTION;
  // and when needed, allow restart regardless the sticky flag
  if (LoadAuthConnectionRestartable()) {
    LOG(("  connection made restartable"));
    mCaps |= NS_HTTP_CONNECTION_RESTARTABLE;
    StoreAuthConnectionRestartable(false);
  } else {
    LOG(("  connection made non-restartable"));
    mCaps &= ~NS_HTTP_CONNECTION_RESTARTABLE;
  }

  // notify "http-on-before-connect" observers
  gHttpHandler->OnBeforeConnect(this);

  RefPtr<HttpTransactionShell> trans(aTransWithStickyConn);
  return CallOrWaitForResume(
      [trans{std::move(trans)}, aContinueOnStopRequestFunc](auto* self) {
        nsresult rv = self->DoConnect(trans);
        return aContinueOnStopRequestFunc(self, rv);
      });
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIAsyncVerifyRedirectCallback
//-----------------------------------------------------------------------------

nsresult nsHttpChannel::WaitForRedirectCallback() {
  nsresult rv;
  LOG(("nsHttpChannel::WaitForRedirectCallback [this=%p]\n", this));

  if (mTransactionPump) {
    rv = mTransactionPump->Suspend();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (mCachePump) {
    rv = mCachePump->Suspend();
    if (NS_FAILED(rv) && mTransactionPump) {
#ifdef DEBUG
      nsresult resume =
#endif
          mTransactionPump->Resume();
      MOZ_ASSERT(NS_SUCCEEDED(resume), "Failed to resume transaction pump");
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  StoreWaitingForRedirectCallback(true);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::OnRedirectVerifyCallback(nsresult result) {
  LOG(
      ("nsHttpChannel::OnRedirectVerifyCallback [this=%p] "
       "result=%" PRIx32 " stack=%zu WaitingForRedirectCallback=%u\n",
       this, static_cast<uint32_t>(result), mRedirectFuncStack.Length(),
       LoadWaitingForRedirectCallback()));
  MOZ_ASSERT(LoadWaitingForRedirectCallback(),
             "Someone forgot to call WaitForRedirectCallback() ?!");
  StoreWaitingForRedirectCallback(false);

  if (mCanceled && NS_SUCCEEDED(result)) result = NS_BINDING_ABORTED;

  for (uint32_t i = mRedirectFuncStack.Length(); i > 0;) {
    --i;
    // Pop the last function pushed to the stack
    nsContinueRedirectionFunc func = mRedirectFuncStack.PopLastElement();

    // Call it with the result we got from the callback or the deeper
    // function call.
    result = (this->*func)(result);

    // If a new function has been pushed to the stack and placed us in the
    // waiting state, we need to break the chain and wait for the callback
    // again.
    if (LoadWaitingForRedirectCallback()) break;
  }

  if (NS_FAILED(result) && !mCanceled) {
    // First, cancel this channel if we are in failure state to set mStatus
    // and let it be propagated to pumps.
    Cancel(result);
  }

  if (!LoadWaitingForRedirectCallback()) {
    // We are not waiting for the callback. At this moment we must release
    // reference to the redirect target channel, otherwise we may leak.
    // However, if we are redirecting due to auth retries, we open the
    // redirected channel after OnStopRequest. In that case we should not
    // release the reference
    if (!StaticPrefs::network_auth_use_redirect_for_retries() ||
        !mAuthRetryPending) {
      mRedirectChannel = nullptr;
    }
  }

  // We always resume the pumps here. If all functions on stack have been
  // called we need OnStopRequest to be triggered, and if we broke out of the
  // loop above (and are thus waiting for a new callback) the suspension
  // count must be balanced in the pumps.
  if (mTransactionPump) mTransactionPump->Resume();
  if (mCachePump) mCachePump->Resume();

  return result;
}

void nsHttpChannel::PushRedirectAsyncFunc(nsContinueRedirectionFunc func) {
  mRedirectFuncStack.AppendElement(func);
}

void nsHttpChannel::PopRedirectAsyncFunc(nsContinueRedirectionFunc func) {
  MOZ_ASSERT(func == mRedirectFuncStack.LastElement(),
             "Trying to pop wrong method from redirect async stack!");

  mRedirectFuncStack.RemoveLastElement();
}

//-----------------------------------------------------------------------------
// nsIDNSListener functions
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpChannel::OnLookupComplete(nsICancelable* request, nsIDNSRecord* rec,
                                nsresult status) {
  MOZ_ASSERT(NS_IsMainThread(), "Expecting DNS callback on main thread.");

  LOG(
      ("nsHttpChannel::OnLookupComplete [this=%p] prefetch complete%s: "
       "%s status[0x%" PRIx32 "]\n",
       this, mCaps & NS_HTTP_REFRESH_DNS ? ", refresh requested" : "",
       NS_SUCCEEDED(status) ? "success" : "failure",
       static_cast<uint32_t>(status)));

  // Unset DNS cache refresh if it was requested,
  if (mCaps & NS_HTTP_REFRESH_DNS) {
    mCaps &= ~NS_HTTP_REFRESH_DNS;
    if (mTransaction) {
      mTransaction->SetDNSWasRefreshed();
    }
  }

  if (!mDNSBlockingPromise.IsEmpty()) {
    if (NS_SUCCEEDED(status)) {
      nsCOMPtr<nsIDNSRecord> record(rec);
      mDNSBlockingPromise.Resolve(record, __func__);
    } else {
      mDNSBlockingPromise.Reject(status, __func__);
    }
  }

  return NS_OK;
}

void nsHttpChannel::OnHTTPSRRAvailable(nsIDNSHTTPSSVCRecord* aRecord) {
  MOZ_ASSERT(NS_IsMainThread(), "Expecting DNS callback on main thread.");

  LOG(("nsHttpChannel::OnHTTPSRRAvailable [this=%p, aRecord=%p]\n", this,
       aRecord));

  if (mHTTPSSVCRecord) {
    MOZ_ASSERT(false, "OnHTTPSRRAvailable called twice!");
    return;
  }

  nsCOMPtr<nsIDNSHTTPSSVCRecord> record = aRecord;
  mHTTPSSVCRecord.emplace(std::move(record));
  const nsCOMPtr<nsIDNSHTTPSSVCRecord>& httprr = mHTTPSSVCRecord.ref();

  if (LoadWaitHTTPSSVCRecord()) {
    MOZ_ASSERT(mURI->SchemeIs("http"));

    StoreWaitHTTPSSVCRecord(false);
    nsresult rv = ContinueOnBeforeConnect(!!httprr, mStatus, !!httprr);
    if (NS_FAILED(rv)) {
      CloseCacheEntry(false);
      Unused << AsyncAbort(rv);
    }
  } else {
    // This channel is not canceled and the transaction is not created.
    if (httprr && NS_SUCCEEDED(mStatus) && !mTransaction &&
        (mFirstResponseSource != RESPONSE_FROM_CACHE)) {
      bool hasIPAddress = false;
      Unused << httprr->GetHasIPAddresses(&hasIPAddress);
      glean::http::dns_httpssvc_record_receiving_stage.AccumulateSingleSample(
          hasIPAddress ? HTTPSSVC_WITH_IPHINT_RECEIVED_STAGE_0
                       : HTTPSSVC_WITHOUT_IPHINT_RECEIVED_STAGE_0);
      StoreHTTPSSVCTelemetryReported(true);
    }
  }
}

//-----------------------------------------------------------------------------
// nsHttpChannel internal functions
//-----------------------------------------------------------------------------

// Creates an URI to the given location using current URI for base and charset
nsresult nsHttpChannel::CreateNewURI(const char* loc, nsIURI** newURI) {
  nsCOMPtr<nsIIOService> ioService;
  nsresult rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
  if (NS_FAILED(rv)) return rv;

  return ioService->NewURI(nsDependentCString(loc), nullptr, mURI, newURI);
}

void nsHttpChannel::MaybeInvalidateCacheEntryForSubsequentGet() {
  // See RFC 2616 section 5.1.1. These are considered valid
  // methods which DO NOT invalidate cache-entries for the
  // referred resource. POST, PUT and DELETE as well as any
  // other method not listed here will potentially invalidate
  // any cached copy of the resource
  if (mRequestHead.IsGet() || mRequestHead.IsOptions() ||
      mRequestHead.IsHead() || mRequestHead.IsTrace() ||
      mRequestHead.IsConnect()) {
    return;
  }

  // Invalidate the request-uri.
  if (LOG_ENABLED()) {
    nsAutoCString key;
    mURI->GetAsciiSpec(key);
    LOG(("MaybeInvalidateCacheEntryForSubsequentGet [this=%p uri=%s]\n", this,
         key.get()));
  }

  DoInvalidateCacheEntry(mURI);

  // Invalidate Location-header if set
  nsAutoCString location;
  Unused << mResponseHead->GetHeader(nsHttp::Location, location);
  if (!location.IsEmpty()) {
    LOG(("  Location-header=%s\n", location.get()));
    InvalidateCacheEntryForLocation(location.get());
  }

  // Invalidate Content-Location-header if set
  Unused << mResponseHead->GetHeader(nsHttp::Content_Location, location);
  if (!location.IsEmpty()) {
    LOG(("  Content-Location-header=%s\n", location.get()));
    InvalidateCacheEntryForLocation(location.get());
  }
}

void nsHttpChannel::InvalidateCacheEntryForLocation(const char* location) {
  nsAutoCString tmpCacheKey, tmpSpec;
  nsCOMPtr<nsIURI> resultingURI;
  nsresult rv = CreateNewURI(location, getter_AddRefs(resultingURI));
  if (NS_SUCCEEDED(rv) && HostPartIsTheSame(resultingURI)) {
    DoInvalidateCacheEntry(resultingURI);
  } else {
    LOG(("  hosts not matching\n"));
  }
}

void nsHttpChannel::DoInvalidateCacheEntry(nsIURI* aURI) {
  // NOTE:
  // Following comments 24,32 and 33 in bug #327765, we only care about
  // the cache in the protocol-handler.
  // The logic below deviates from the original logic in OpenCacheEntry on
  // one point by using only READ_ONLY access-policy. I think this is safe.

  nsresult rv;

  nsAutoCString key;
  if (LOG_ENABLED()) {
    aURI->GetAsciiSpec(key);
  }

  LOG(("DoInvalidateCacheEntry [channel=%p key=%s]", this, key.get()));

  nsCOMPtr<nsICacheStorageService> cacheStorageService(
      components::CacheStorage::Service());
  rv = cacheStorageService ? NS_OK : NS_ERROR_FAILURE;

  nsCOMPtr<nsICacheStorage> cacheStorage;
  if (NS_SUCCEEDED(rv)) {
    RefPtr<LoadContextInfo> info = GetLoadContextInfo(this);
    rv = cacheStorageService->DiskCacheStorage(info,
                                               getter_AddRefs(cacheStorage));
  }

  if (NS_SUCCEEDED(rv)) {
    rv = cacheStorage->AsyncDoomURI(aURI, ""_ns, nullptr);
  }

  LOG(("DoInvalidateCacheEntry [channel=%p key=%s rv=%d]", this, key.get(),
       int(rv)));
}

void nsHttpChannel::AsyncOnExamineCachedResponse() {
  gHttpHandler->OnExamineCachedResponse(this);
}

void nsHttpChannel::UpdateAggregateCallbacks() {
  if (!mTransaction) {
    return;
  }
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  NS_NewNotificationCallbacksAggregation(mCallbacks, mLoadGroup,
                                         GetCurrentSerialEventTarget(),
                                         getter_AddRefs(callbacks));
  mTransaction->SetSecurityCallbacks(callbacks);
}

NS_IMETHODIMP
nsHttpChannel::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread.");

  nsresult rv = HttpBaseChannel::SetLoadGroup(aLoadGroup);
  if (NS_SUCCEEDED(rv)) {
    UpdateAggregateCallbacks();
  }
  return rv;
}

NS_IMETHODIMP
nsHttpChannel::SetNotificationCallbacks(nsIInterfaceRequestor* aCallbacks) {
  MOZ_ASSERT(NS_IsMainThread(), "Wrong thread.");

  nsresult rv = HttpBaseChannel::SetNotificationCallbacks(aCallbacks);
  if (NS_SUCCEEDED(rv)) {
    UpdateAggregateCallbacks();
  }
  return rv;
}

bool nsHttpChannel::AwaitingCacheCallbacks() {
  return LoadWaitForCacheEntry() != 0;
}

// static
bool nsHttpChannel::IsRedirectStatus(uint32_t status) {
  // 305 disabled as a security measure (see bug 187996).
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

void nsHttpChannel::SetCouldBeSynthesized() {
  MOZ_ASSERT(!BypassServiceWorker());
  StoreResponseCouldBeSynthesized(true);
}

NS_IMETHODIMP
nsHttpChannel::OnPreflightSucceeded() {
  MOZ_ASSERT(LoadRequireCORSPreflight(), "Why did a preflight happen?");
  StoreIsCorsPreflightDone(1);
  mPreflightChannel = nullptr;

  return ContinueConnect();
}

NS_IMETHODIMP
nsHttpChannel::OnPreflightFailed(nsresult aError) {
  MOZ_ASSERT(LoadRequireCORSPreflight(), "Why did a preflight happen?");
  StoreIsCorsPreflightDone(1);
  mPreflightChannel = nullptr;

  CloseCacheEntry(false);
  Unused << AsyncAbort(aError);
  return NS_OK;
}

nsresult nsHttpChannel::CallOrWaitForResume(
    const std::function<nsresult(nsHttpChannel*)>& aFunc) {
  if (mCanceled) {
    MOZ_ASSERT(NS_FAILED(mStatus));
    return mStatus;
  }

  if (mSuspendCount) {
    LOG(("Waiting until resume [this=%p]\n", this));
    MOZ_ASSERT(!mCallOnResume);
    mCallOnResume = aFunc;
    return NS_OK;
  }

  return aFunc(this);
}

// This is loosely based on:
// https://fetch.spec.whatwg.org/#serializing-a-request-origin
static bool HasNullRequestOrigin(nsHttpChannel* aChannel, nsIURI* aURI,
                                 bool isAddonRequest) {
  // Step 1. If request has a redirect-tainted origin, then return "null".
  if (aChannel->HasRedirectTaintedOrigin()) {
    if (StaticPrefs::network_http_origin_redirectTainted()) {
      return true;
    }
  }

  // Non-standard: Only allow HTTP and HTTPS origins.
  if (!ReferrerInfo::IsReferrerSchemeAllowed(aURI)) {
    // And moz-extension: for add-on initiated requests.
    if (!aURI->SchemeIs("moz-extension") || !isAddonRequest) {
      return true;
    }
  }

  // Non-standard: Hide onion URLs.
  if (StaticPrefs::network_http_referer_hideOnionSource()) {
    nsAutoCString host;
    if (NS_SUCCEEDED(aURI->GetAsciiHost(host)) &&
        StringEndsWith(host, ".onion"_ns)) {
      return ReferrerInfo::IsCrossOriginRequest(aChannel);
    }
  }

  // Step 2. Return request’s origin, serialized.
  return false;
}

// Step 8.12. of HTTP-network-or-cache fetch
//
// https://fetch.spec.whatwg.org/#append-a-request-origin-header
void nsHttpChannel::SetOriginHeader() {
  auto* triggeringPrincipal =
      BasePrincipal::Cast(mLoadInfo->TriggeringPrincipal());

  if (triggeringPrincipal->IsSystemPrincipal()) {
    // We can't infer an Origin header from the system principal,
    // this means system requests use whatever Origin header was specified.
    return;
  }
  bool isAddonRequest = triggeringPrincipal->AddonPolicy() ||
                        triggeringPrincipal->ContentScriptAddonPolicy();

  // Non-standard: Handle already existing Origin header.
  nsAutoCString existingHeader;
  Unused << mRequestHead.GetHeader(nsHttp::Origin, existingHeader);
  if (!existingHeader.IsEmpty()) {
    LOG(("nsHttpChannel::SetOriginHeader Origin header already present"));
    auto const shouldNullifyOriginHeader =
        [&existingHeader, isAddonRequest](nsHttpChannel* aChannel) {
          nsCOMPtr<nsIURI> uri;
          nsresult rv = NS_NewURI(getter_AddRefs(uri), existingHeader);
          if (NS_FAILED(rv)) {
            return false;
          }

          if (HasNullRequestOrigin(aChannel, uri, isAddonRequest)) {
            return true;
          }

          nsCOMPtr<nsILoadInfo> info = aChannel->LoadInfo();
          if (info->GetTainting() == mozilla::LoadTainting::CORS) {
            return false;
          }

          return ReferrerInfo::ShouldSetNullOriginHeader(aChannel, uri);
        };

    if (!existingHeader.EqualsLiteral("null") &&
        shouldNullifyOriginHeader(this)) {
      LOG(("nsHttpChannel::SetOriginHeader null Origin by Referrer-Policy"));
      MOZ_ALWAYS_SUCCEEDS(
          mRequestHead.SetHeader(nsHttp::Origin, "null"_ns, false /* merge */));
    }
    return;
  }

  if (StaticPrefs::network_http_sendOriginHeader() == 0) {
    // Custom user setting: 0 means never send Origin header.
    return;
  }

  // Step 1. Let serializedOrigin be the result of byte-serializing a request
  // origin with request.
  nsAutoCString serializedOrigin;
  nsCOMPtr<nsIURI> uri;
  {
    if (NS_FAILED(triggeringPrincipal->GetURI(getter_AddRefs(uri)))) {
      return;
    }

    if (!uri) {
      if (isAddonRequest) {
        // For add-on compatibility prefer sending no header at all
        // instead of `Origin: null`.
        return;
      }

      // Otherwise use "null" when the triggeringPrincipal's URI is nullptr.
      serializedOrigin.AssignLiteral("null");
    } else if (HasNullRequestOrigin(this, uri, isAddonRequest)) {
      serializedOrigin.AssignLiteral("null");
    } else {
      nsContentUtils::GetWebExposedOriginSerialization(uri, serializedOrigin);
    }
  }

  // Step 2. If request’s response tainting is "cors" or request’s mode is
  // "websocket", then append (`Origin`, serializedOrigin) to request’s header
  // list.
  //
  // Note: We don't handle "websocket" here (yet?).
  if (mLoadInfo->GetTainting() == mozilla::LoadTainting::CORS) {
    MOZ_ALWAYS_SUCCEEDS(mRequestHead.SetHeader(nsHttp::Origin, serializedOrigin,
                                               false /* merge */));
    return;
  }

  // Step 3. Otherwise, if request’s method is neither `GET` nor `HEAD`, then:
  if (mRequestHead.IsGet() || mRequestHead.IsHead()) {
    return;
  }

  if (!serializedOrigin.EqualsLiteral("null")) {
    // Step 3.1. (Implemented by ReferrerInfo::ShouldSetNullOriginHeader)
    if (ReferrerInfo::ShouldSetNullOriginHeader(this, uri)) {
      serializedOrigin.AssignLiteral("null");
    } else if (StaticPrefs::network_http_sendOriginHeader() == 1) {
      // Non-standard: Restrict Origin to same-origin loads if requested by user
      nsAutoCString currentOrigin;
      nsContentUtils::GetWebExposedOriginSerialization(mURI, currentOrigin);
      if (!serializedOrigin.EqualsIgnoreCase(currentOrigin.get())) {
        // Origin header suppressed by user setting.
        serializedOrigin.AssignLiteral("null");
      }
    }
  }

  // Step 3.2. Append (`Origin`, serializedOrigin) to request’s header list.
  MOZ_ALWAYS_SUCCEEDS(mRequestHead.SetHeader(nsHttp::Origin, serializedOrigin,
                                             false /* merge */));
}

void nsHttpChannel::SetDoNotTrack() {
  /**
   * 'DoNotTrack' header should be added if 'privacy.donottrackheader.enabled'
   * is true or tracking protection is enabled. See bug 1258033.
   */
  nsCOMPtr<nsILoadContext> loadContext;
  NS_QueryNotificationCallbacks(this, loadContext);

  if ((loadContext && loadContext->UseTrackingProtection()) ||
      StaticPrefs::privacy_donottrackheader_enabled()) {
    DebugOnly<nsresult> rv =
        mRequestHead.SetHeader(nsHttp::DoNotTrack, "1"_ns, false);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

void nsHttpChannel::SetGlobalPrivacyControl() {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");

  if (StaticPrefs::privacy_globalprivacycontrol_functionality_enabled() &&
      (StaticPrefs::privacy_globalprivacycontrol_enabled() ||
       (StaticPrefs::privacy_globalprivacycontrol_pbmode_enabled() &&
        NS_UsePrivateBrowsing(this)))) {
    // Send the header with a value of 1 to indicate opting-out
    DebugOnly<nsresult> rv =
        mRequestHead.SetHeader(nsHttp::GlobalPrivacyControl, "1"_ns, false);
  }
}

void nsHttpChannel::ReportRcwnStats(bool isFromNet) {
  if (!StaticPrefs::network_http_rcwn_enabled()) {
    return;
  }

  if (isFromNet) {
    // Race was won by the network if:
    // * we hadn't gotten a response from the cache yet when we started pumping
    //   data from the network, whether or not the cache entry would have
    //   been valid or not.
    // * If we did get a response from the cache first, but it wasn't valid or
    // just
    //   indicated there was no entry, then it's not a network RCWN win
    // * Note that we consider it a Network win even if an invalid/expired cache
    //   response comes in after the network response.

    if (mRaceCacheWithNetwork && LoadNetworkWonRace()) {
      PROFILER_MARKER_TEXT(
          "RCWN", NETWORK, {},
          nsPrintfCString("Network won valid = %d, channel %p, URI %s",
                          LoadCachedContentIsValid(), this, mSpec.get()));
      gIOService->IncrementNetWonRequestNumber();
      glean::network::race_cache_bandwidth_race_network_win.Accumulate(
          mTransferSize);
      if (mRaceDelay) {
        glean::network::race_cache_with_network_usage
            .EnumGet(glean::network::RaceCacheWithNetworkUsageLabel::
                         eNetworkdelayedrace)
            .Add();
      } else {
        glean::network::race_cache_with_network_usage
            .EnumGet(
                glean::network::RaceCacheWithNetworkUsageLabel::eNetworkrace)
            .Add();
      }
    } else {
      PROFILER_MARKER_TEXT(
          "RCWN", NETWORK, {},
          nsPrintfCString(
              "Cache won or was replaced, valid = %d, channel %p, URI %s",
              LoadCachedContentIsValid(), this, mSpec.get()));
      glean::network::race_cache_bandwidth_not_race.Accumulate(mTransferSize);
      glean::network::race_cache_with_network_usage
          .EnumGet(
              glean::network::RaceCacheWithNetworkUsageLabel::eNetworknorace)
          .Add();
    }
  } else {
    if (mRaceCacheWithNetwork || mRaceDelay) {
      PROFILER_MARKER_TEXT(
          "RCWN", NETWORK, {},
          nsPrintfCString("Cache won valid=%d, channel %p, URI %s",
                          LoadCachedContentIsValid(), this, mSpec.get()));
      gIOService->IncrementCacheWonRequestNumber();
      glean::network::race_cache_bandwidth_race_cache_win.Accumulate(
          mTransferSize);
      if (mRaceDelay) {
        glean::network::race_cache_with_network_usage
            .EnumGet(glean::network::RaceCacheWithNetworkUsageLabel::
                         eCachedelayedrace)
            .Add();
      } else {
        glean::network::race_cache_with_network_usage
            .EnumGet(glean::network::RaceCacheWithNetworkUsageLabel::eCacherace)
            .Add();
      }
    } else {
      glean::network::race_cache_bandwidth_not_race.Accumulate(mTransferSize);
      glean::network::race_cache_with_network_usage
          .EnumGet(glean::network::RaceCacheWithNetworkUsageLabel::eCachenorace)
          .Add();
    }
  }

  gIOService->IncrementRequestNumber();
}

void nsHttpChannel::ReportSystemChannelTelemetry(nsresult status) {
  // Use status and httpStatus to determine
  // if it was successful, and if we had connectivity / offline in this time
  nsAutoCString domain;
  mOriginalURI->GetHost(domain);

  if (!LoadUsedNetwork()) {
    // We're not really interested in any cached requests.
    return;
  }

  if (!StringEndsWith(domain, ".mozilla.org"_ns) &&
      !StringEndsWith(domain, ".mozilla.com"_ns)) {
    return;
  }

  nsAutoCString label("ok"_ns);
  if (NS_FAILED(status)) {
    if (mCanceled) {
      // The request was cancelled.
      label = "cancel"_ns;
    } else if (NS_IsOffline()) {
      // The error occured while all interfaces are offline
      label = "offline"_ns;
    } else if (!hasConnectivity()) {
      // The error occured while the browser didn't have connectivity
      label = "connectivity"_ns;
    } else if (status == NS_ERROR_UNKNOWN_HOST) {
      // The failure was a DNS error
      label = "dns"_ns;
    } else if (NS_ERROR_GET_MODULE(status) == NS_ERROR_MODULE_SECURITY) {
      // The error was due to TLS
      label = "tls_fail"_ns;
    } else if (status == NS_ERROR_NET_RESET) {
      label = "reset"_ns;
    } else if (status == NS_ERROR_NET_TIMEOUT) {
      label = "timeout"_ns;
    } else if (status == NS_ERROR_CONNECTION_REFUSED) {
      label = "refused"_ns;
    } else if (status == NS_ERROR_NET_PARTIAL_TRANSFER) {
      label = "partial"_ns;
    } else {
      // Unspecified error. If this bucket is too big we might add other labels.
      label = "other"_ns;
    }
  } else if (mResponseHead && mResponseHead->Status() / 100 != 2) {
    // There was no channel error, but the server responded with a non-2XX
    // status code.
    label = "http_status";
  }

  if (StringEndsWith(domain, ".addons.mozilla.org"_ns)) {
    mozilla::glean::network::system_channel_addonversion_status.Get(label).Add(
        1);
    return;
  }

  if (domain == "addons.mozilla.org") {
    mozilla::glean::network::system_channel_addon_status.Get(label).Add(1);
    return;
  }

  if (domain == "aus5.mozilla.org"_ns) {
    mozilla::glean::network::system_channel_update_status.Get(label).Add(1);
    return;
  }

  if (domain == "firefox.settings.services.mozilla.com"_ns) {
    mozilla::glean::network::system_channel_remote_settings_status.Get(label)
        .Add(1);
    return;
  }

  if (domain == "incoming.telemetry.mozilla.com"_ns) {
    mozilla::glean::network::system_channel_telemetry_status.Get(label).Add(1);
    return;
  }

  // Not one of the probes we recorded earlier.
  mozilla::glean::network::system_channel_other_status.Get(label).Add(1);
}

NS_IMETHODIMP
nsHttpChannel::GetAllowRacing(bool* aAllowRacing) {
  *aAllowRacing = mAllowRCWN;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::SetAllowRacing(bool aAllowRacing) {
  mAllowRCWN = aAllowRacing;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::Test_delayCacheEntryOpeningBy(int32_t aTimeout) {
  LOG(("nsHttpChannel::Test_delayCacheEntryOpeningBy this=%p timeout=%d", this,
       aTimeout));
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");
  mRaceCacheWithNetwork = true;
  mCacheOpenDelay = aTimeout;
  if (mCacheOpenTimer) {
    mCacheOpenTimer->SetDelay(aTimeout);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::Test_triggerDelayedOpenCacheEntry() {
  LOG(("nsHttpChannel::Test_triggerDelayedOpenCacheEntry this=%p", this));
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");
  nsresult rv;
  if (!mCacheOpenDelay) {
    // No delay was set.
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (!mCacheOpenFunc) {
    // There should be a runnable.
    return NS_ERROR_FAILURE;
  }
  if (mCacheOpenTimer) {
    rv = mCacheOpenTimer->Cancel();
    if (NS_FAILED(rv)) {
      return rv;
    }
    mCacheOpenTimer = nullptr;
  }
  mCacheOpenDelay = 0;
  // Avoid re-entrancy issues by nulling our mCacheOpenFunc before calling it.
  std::function<void(nsHttpChannel*)> cacheOpenFunc = nullptr;
  std::swap(cacheOpenFunc, mCacheOpenFunc);
  cacheOpenFunc(this);

  return NS_OK;
}

nsresult nsHttpChannel::TriggerNetworkWithDelay(uint32_t aDelay) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");

  LOG(("nsHttpChannel::TriggerNetworkWithDelay [this=%p, delay=%u]\n", this,
       aDelay));

  if (mCanceled) {
    LOG(("  channel was canceled.\n"));
    return mStatus;
  }

  // If a network request has already gone out, there is no point in
  // doing this again.
  if (mNetworkTriggered) {
    LOG(("  network already triggered. Returning.\n"));
    return NS_OK;
  }

  if (mNetworkTriggerDelay) {
    aDelay = mNetworkTriggerDelay;
  }

  if (!aDelay) {
    // We cannot call TriggerNetwork() directly here, because it would
    // cause performance regression in tp6 tests, see bug 1398847.
    return NS_DispatchToMainThread(
        NewRunnableMethod("net::nsHttpChannel::TriggerNetworkWithDelay", this,
                          &nsHttpChannel::TriggerNetwork),
        NS_DISPATCH_NORMAL);
  }

  MOZ_ASSERT(!mNetworkTriggerTimer);
  mNetworkTriggerTimer = NS_NewTimer();
  auto callback = MakeRefPtr<TimerCallback>(this);
  LOG(("Creating new networkTriggertimer for delay"));
  mNetworkTriggerTimer->InitWithCallback(callback, aDelay,
                                         nsITimer::TYPE_ONE_SHOT);
  return NS_OK;
}

nsresult nsHttpChannel::TriggerNetwork() {
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");

  LOG(("nsHttpChannel::TriggerNetwork [this=%p]\n", this));

  if (mCanceled) {
    LOG(("  channel was canceled.\n"));
    return mStatus;
  }

  // If a network request has already gone out, there is no point in
  // doing this again.
  if (mNetworkTriggered) {
    LOG(("  network already triggered. Returning.\n"));
    return NS_OK;
  }

  mNetworkTriggered = true;
  if (mNetworkTriggerTimer) {
    mNetworkTriggerTimer->Cancel();
    mNetworkTriggerTimer = nullptr;
  }

  // If we are waiting for a proxy request, that means we can't trigger
  // the next step just yet. We need for mConnectionInfo to be non-null
  // before we call ContinueConnect. OnProxyAvailable will trigger
  // BeginConnect, and Connect will call ContinueConnect even if it's
  // for the cache callbacks.
  if (mProxyRequest) {
    LOG(("  proxy request in progress. Delaying network trigger.\n"));
    mWaitingForProxy = true;
    return NS_OK;
  }

  // If |mCacheOpenFunc| is assigned, we're delaying opening the entry to
  // simulate racing. Although cache entry opening hasn't started yet, we're
  // actually racing, so we must set mRaceCacheWithNetwork to true now.
  mRaceCacheWithNetwork =
      AwaitingCacheCallbacks() &&
      (mCacheOpenFunc || StaticPrefs::network_http_rcwn_enabled());

  LOG(("  triggering network rcwn=%d\n", bool(mRaceCacheWithNetwork)));
  return ContinueConnect();
}

void nsHttpChannel::MaybeRaceCacheWithNetwork() {
  nsresult rv;

  nsCOMPtr<nsINetworkLinkService> netLinkSvc;
  netLinkSvc = do_GetService(NS_NETWORK_LINK_SERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  uint32_t linkType;
  rv = netLinkSvc->GetLinkType(&linkType);
  if (NS_FAILED(rv)) {
    return;
  }

  if (!(linkType == nsINetworkLinkService::LINK_TYPE_ETHERNET ||
#ifndef MOZ_WIDGET_ANDROID
        // On Android we don't assume an unknown link type is unmetered
        linkType == nsINetworkLinkService::LINK_TYPE_UNKNOWN ||
#endif
        linkType == nsINetworkLinkService::LINK_TYPE_USB ||
        linkType == nsINetworkLinkService::LINK_TYPE_WIFI)) {
    return;
  }

  // Don't trigger the network if the load flags say so.
  if (mLoadFlags & (LOAD_ONLY_FROM_CACHE | LOAD_NO_NETWORK_IO)) {
    return;
  }

  // We must not race if the channel has a failure status code.
  if (NS_FAILED(mStatus)) {
    return;
  }

  // If a CORS Preflight is required we must not race.
  if (LoadRequireCORSPreflight() && !LoadIsCorsPreflightDone()) {
    return;
  }

  if (CacheFileUtils::CachePerfStats::IsCacheSlow()) {
    // If the cache is slow, trigger the network request immediately.
    mRaceDelay = 0;
  } else {
    // Give cache a headstart of 3 times the average cache entry open time.
    mRaceDelay = CacheFileUtils::CachePerfStats::GetAverage(
                     CacheFileUtils::CachePerfStats::ENTRY_OPEN, true) *
                 3;
    // We use microseconds in CachePerfStats but we need milliseconds
    // for TriggerNetwork.
    mRaceDelay /= 1000;
  }

  mRaceDelay = std::clamp<uint32_t>(
      mRaceDelay, StaticPrefs::network_http_rcwn_min_wait_before_racing_ms(),
      StaticPrefs::network_http_rcwn_max_wait_before_racing_ms());

  MOZ_ASSERT(StaticPrefs::network_http_rcwn_enabled() || mNetworkTriggerDelay,
             "The pref must be turned on.");
  LOG(("nsHttpChannel::MaybeRaceCacheWithNetwork [this=%p, delay=%u]\n", this,
       mRaceDelay));

  TriggerNetworkWithDelay(mRaceDelay);
}

NS_IMETHODIMP
nsHttpChannel::Test_triggerNetwork(int32_t aTimeout) {
  LOG(("nsHttpChannel::Test_triggerNetwork this=%p timeout=%d", this,
       aTimeout));
  MOZ_ASSERT(NS_IsMainThread(), "Must be called on the main thread");

  // We set the trigger delay to the specified timeout.
  mRaceCacheWithNetwork = true;
  mNetworkTriggerDelay = aTimeout;

  // If we already have a timer, set the delay/
  if (mNetworkTriggerTimer) {
    // If the timeout is 0 and there is a timer, we can trigger
    // the network immediately.
    MOZ_ASSERT(LoadWasOpened(), "Must have been opened before");
    if (!aTimeout) {
      return TriggerNetwork();
    }
    mNetworkTriggerTimer->SetDelay(aTimeout);
  }
  return NS_OK;
}

nsHttpChannel::TimerCallback::TimerCallback(nsHttpChannel* aChannel)
    : mChannel(aChannel) {}

NS_IMPL_ISUPPORTS(nsHttpChannel::TimerCallback, nsITimerCallback, nsINamed)

NS_IMETHODIMP
nsHttpChannel::TimerCallback::GetName(nsACString& aName) {
  aName.AssignLiteral("nsHttpChannel");
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::TimerCallback::Notify(nsITimer* aTimer) {
  if (aTimer == mChannel->mCacheOpenTimer) {
    return mChannel->Test_triggerDelayedOpenCacheEntry();
  }
  if (aTimer == mChannel->mNetworkTriggerTimer) {
    return mChannel->TriggerNetwork();
  }
  MOZ_CRASH("Unknown timer");

  return NS_OK;
}

bool nsHttpChannel::EligibleForTailing() {
  if (!(mClassOfService.Flags() & nsIClassOfService::Tail)) {
    return false;
  }

  if (mClassOfService.Flags() &
      (nsIClassOfService::UrgentStart | nsIClassOfService::Leader |
       nsIClassOfService::TailForbidden)) {
    return false;
  }

  if (mClassOfService.Flags() & nsIClassOfService::Unblocked &&
      !(mClassOfService.Flags() & nsIClassOfService::TailAllowed)) {
    return false;
  }

  if (IsNavigation()) {
    return false;
  }

  return true;
}

bool nsHttpChannel::WaitingForTailUnblock() {
  nsresult rv;

  if (!gHttpHandler->IsTailBlockingEnabled()) {
    LOG(("nsHttpChannel %p tail-blocking disabled", this));
    return false;
  }

  if (!EligibleForTailing()) {
    LOG(("nsHttpChannel %p not eligible for tail-blocking", this));
    AddAsNonTailRequest();
    return false;
  }

  if (!EnsureRequestContext()) {
    LOG(("nsHttpChannel %p no request context", this));
    return false;
  }

  LOG(("nsHttpChannel::WaitingForTailUnblock this=%p, rc=%p", this,
       mRequestContext.get()));

  bool blocked;
  rv = mRequestContext->IsContextTailBlocked(this, &blocked);
  if (NS_FAILED(rv)) {
    return false;
  }

  LOG(("  blocked=%d", blocked));

  return blocked;
}

//-----------------------------------------------------------------------------
// nsHttpChannel::nsIRequestTailUnblockCallback
//-----------------------------------------------------------------------------

// Must be implemented in the leaf class because we don't have
// AsyncAbort in HttpBaseChannel.
NS_IMETHODIMP
nsHttpChannel::OnTailUnblock(nsresult rv) {
  LOG(("nsHttpChannel::OnTailUnblock this=%p rv=%" PRIx32 " rc=%p", this,
       static_cast<uint32_t>(rv), mRequestContext.get()));
  AUTO_PROFILER_FLOW_MARKER("nsHttpChannel::OnTailUnblock", NETWORK,
                            Flow::FromPointer(this));
  MOZ_RELEASE_ASSERT(mOnTailUnblock);

  if (NS_FAILED(mStatus)) {
    rv = mStatus;
  }

  if (NS_SUCCEEDED(rv)) {
    auto callback = mOnTailUnblock;
    mOnTailUnblock = nullptr;
    rv = (this->*callback)();
  }

  if (NS_FAILED(rv)) {
    CloseCacheEntry(false);
    return AsyncAbort(rv);
  }

  return NS_OK;
}

void nsHttpChannel::SetWarningReporter(
    HttpChannelSecurityWarningReporter* aReporter) {
  LOG(("nsHttpChannel [this=%p] SetWarningReporter [%p]", this, aReporter));
  mWarningReporter = aReporter;
}

HttpChannelSecurityWarningReporter* nsHttpChannel::GetWarningReporter() {
  LOG(("nsHttpChannel [this=%p] GetWarningReporter [%p]", this,
       mWarningReporter.get()));
  return mWarningReporter.get();
}

// The specification for ORB is currently being written:
// https://whatpr.org/fetch/1442.html#orb-algorithm
// The `opaque-response-safelist check` is implemented in:
// * `HttpBaseChannel::PerformOpaqueResponseSafelistCheckBeforeSniff`
// * `nsHttpChannel::DisableIsOpaqueResponseAllowedAfterSniffCheck`
// * `HttpBaseChannel::PerformOpaqueResponseSafelistCheckAfterSniff`
// * `OpaqueResponseBlocker::ValidateJavaScript`
//
// Should only be called by nsMediaSniffer::GetMIMETypeFromContent and
// imageLoader::GetMIMETypeFromContent when the content type can be
// recognized by these sniffers.
void nsHttpChannel::DisableIsOpaqueResponseAllowedAfterSniffCheck(
    SnifferType aType) {
  // https://whatpr.org/fetch/1442.html#orb-algorithm
  // This method covers steps, 8 and 10.
  MOZ_ASSERT(XRE_IsParentProcess());

  if (NeedOpaqueResponseAllowedCheckAfterSniff()) {
    MOZ_ASSERT(mCachedOpaqueResponseBlockingPref);

    // If the sniffer type is media and the request comes from a media element,
    // we would like to check:
    // - Whether the information provided by the media element shows it's an
    // initial request.
    // - Whether the response's status is either 200 or 206.
    //
    // If any of the results is false, then we set
    // mBlockOpaqueResponseAfterSniff to true and block the response later.
    if (aType == SnifferType::Media) {
      // Step 8
      MOZ_ASSERT(mLoadInfo);

      bool isMediaRequest;
      mLoadInfo->GetIsMediaRequest(&isMediaRequest);
      if (isMediaRequest) {
        bool isInitialRequest;
        mLoadInfo->GetIsMediaInitialRequest(&isInitialRequest);
        MOZ_ASSERT(isInitialRequest);

        if (!isInitialRequest) {
          // Step 8.1
          BlockOpaqueResponseAfterSniff(
              u"media request after sniffing, but not initial request"_ns,
              OpaqueResponseBlockedTelemetryReason::eMediaNotInitial);
          return;
        }

        if (mResponseHead->Status() != 200 && mResponseHead->Status() != 206) {
          // Step 8.2
          BlockOpaqueResponseAfterSniff(
              u"media request's response status is neither 200 nor 206"_ns,
              OpaqueResponseBlockedTelemetryReason::eMediaIncorrectResp);
          return;
        }
      }
    }

    // Step 8.3 if `aType == SnifferType::Media`
    // Step 9 can be skipped, only `HTMLMediaElement` ever sets isMediaRequest.
    // Step 10 if `aType == SnifferType::Image`
    AllowOpaqueResponseAfterSniff();
  }
}

namespace {

class CopyNonDefaultHeaderVisitor final : public nsIHttpHeaderVisitor {
  nsCOMPtr<nsIHttpChannel> mTarget;

  ~CopyNonDefaultHeaderVisitor() = default;

  NS_IMETHOD
  VisitHeader(const nsACString& aHeader, const nsACString& aValue) override {
    if (aValue.IsEmpty()) {
      return mTarget->SetEmptyRequestHeader(aHeader);
    }
    return mTarget->SetRequestHeader(aHeader, aValue, false /* merge */);
  }

 public:
  explicit CopyNonDefaultHeaderVisitor(nsIHttpChannel* aTarget)
      : mTarget(aTarget) {
    MOZ_DIAGNOSTIC_ASSERT(mTarget);
  }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(CopyNonDefaultHeaderVisitor, nsIHttpHeaderVisitor)

}  // anonymous namespace

nsresult nsHttpChannel::RedirectToInterceptedChannel() {
  nsCOMPtr<nsINetworkInterceptController> controller;
  GetCallback(controller);

  RefPtr<InterceptedHttpChannel> intercepted =
      InterceptedHttpChannel::CreateForInterception(
          mChannelCreationTime, mChannelCreationTimestamp, mAsyncOpenTime);

  ExtContentPolicyType type = mLoadInfo->GetExternalContentPolicyType();

  nsCOMPtr<nsILoadInfo> redirectLoadInfo =
      CloneLoadInfoForRedirect(mURI, nsIChannelEventSink::REDIRECT_INTERNAL);

  nsresult rv = intercepted->Init(
      mURI, mCaps, static_cast<nsProxyInfo*>(mProxyInfo.get()),
      mProxyResolveFlags, mProxyURI, mChannelId, type, redirectLoadInfo);

  rv = SetupReplacementChannel(mURI, intercepted, true,
                               nsIChannelEventSink::REDIRECT_INTERNAL);
  NS_ENSURE_SUCCESS(rv, rv);

  // Some APIs, like fetch(), allow content to set non-standard headers.
  // Normally these APIs are responsible for copying these headers across
  // redirects.  In the e10s parent-side intercept case, though, we currently
  // "hide" the internal redirect to the InterceptedHttpChannel.  So the
  // fetch() API does not have the opportunity to move headers over.
  // Therefore, we do it automatically here.
  //
  // Once child-side interception is removed and the internal redirect no
  // longer needs to be "hidden", then this header copying code can be
  // removed.
  nsCOMPtr<nsIHttpHeaderVisitor> visitor =
      new CopyNonDefaultHeaderVisitor(intercepted);
  rv = VisitNonDefaultRequestHeaders(visitor);
  NS_ENSURE_SUCCESS(rv, rv);

  mRedirectChannel = intercepted;

  PushRedirectAsyncFunc(&nsHttpChannel::ContinueAsyncRedirectChannelToURI);

  rv = gHttpHandler->AsyncOnChannelRedirect(
      this, intercepted, nsIChannelEventSink::REDIRECT_INTERNAL);

  if (NS_SUCCEEDED(rv)) {
    rv = WaitForRedirectCallback();
  }

  if (NS_FAILED(rv)) {
    AutoRedirectVetoNotifier notifier(this, rv);

    PopRedirectAsyncFunc(&nsHttpChannel::ContinueAsyncRedirectChannelToURI);
  }

  return rv;
}

void nsHttpChannel::ReEvaluateReferrerAfterTrackingStatusIsKnown() {
  nsCOMPtr<nsICookieJarSettings> cjs;
  if (mLoadInfo) {
    Unused << mLoadInfo->GetCookieJarSettings(getter_AddRefs(cjs));
  }
  if (!cjs) {
    cjs = net::CookieJarSettings::Create(mLoadInfo->GetLoadingPrincipal());
  }
  if (cjs->GetRejectThirdPartyContexts()) {
    bool isPrivate = mLoadInfo->GetOriginAttributes().IsPrivateBrowsing();
    // If our referrer has been set before, and our referrer policy is unset
    // (default policy) if we thought the channel wasn't a third-party
    // tracking channel, we may need to set our referrer with referrer policy
    // once again to ensure our defaults properly take effect now.
    if (mReferrerInfo) {
      ReferrerInfo* referrerInfo =
          static_cast<ReferrerInfo*>(mReferrerInfo.get());

      if (referrerInfo->IsPolicyOverrided() &&
          referrerInfo->ReferrerPolicy() ==
              ReferrerInfo::GetDefaultReferrerPolicy(nullptr, nullptr,
                                                     isPrivate)) {
        nsCOMPtr<nsIReferrerInfo> newReferrerInfo =
            referrerInfo->CloneWithNewPolicy(
                ReferrerInfo::GetDefaultReferrerPolicy(this, mURI, isPrivate));
        // The arguments passed to SetReferrerInfoInternal here should mirror
        // the arguments passed in
        // HttpChannelChild::RecvOverrideReferrerInfoDuringBeginConnect().
        SetReferrerInfoInternal(newReferrerInfo, false, true, true);

        nsCOMPtr<nsIParentChannel> parentChannel;
        NS_QueryNotificationCallbacks(this, parentChannel);
        RefPtr<HttpChannelParent> httpParent = do_QueryObject(parentChannel);
        if (httpParent) {
          httpParent->OverrideReferrerInfoDuringBeginConnect(newReferrerInfo);
        }
      }
    }
  }
}

namespace {

class BackgroundRevalidatingListener : public nsIStreamListener {
  NS_DECL_ISUPPORTS

  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER

 private:
  virtual ~BackgroundRevalidatingListener() = default;
};

NS_IMETHODIMP
BackgroundRevalidatingListener::OnStartRequest(nsIRequest* request) {
  return NS_OK;
}

NS_IMETHODIMP
BackgroundRevalidatingListener::OnDataAvailable(nsIRequest* request,
                                                nsIInputStream* input,
                                                uint64_t offset,
                                                uint32_t count) {
  uint32_t bytesRead = 0;
  return input->ReadSegments(NS_DiscardSegment, nullptr, count, &bytesRead);
}

NS_IMETHODIMP
BackgroundRevalidatingListener::OnStopRequest(nsIRequest* request,
                                              nsresult status) {
  if (NS_FAILED(status)) {
    return status;
  }

  nsCOMPtr<nsIHttpChannel> channel(do_QueryInterface(request));
  if (gHttpHandler) {
    gHttpHandler->OnBackgroundRevalidation(channel);
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS(BackgroundRevalidatingListener, nsIStreamListener,
                  nsIRequestObserver)

}  // namespace

void nsHttpChannel::PerformBackgroundCacheRevalidation() {
  if (!StaticPrefs::network_http_stale_while_revalidate_enabled()) {
    return;
  }

  // This is a channel doing a revalidation. It shouldn't do it again.
  if (mStaleRevalidation) {
    return;
  }

  LOG(("nsHttpChannel::PerformBackgroundCacheRevalidation %p", this));

  Unused << NS_DispatchToMainThreadQueue(
      NewIdleRunnableMethod(
          "nsHttpChannel::PerformBackgroundCacheRevalidation", this,
          &nsHttpChannel::PerformBackgroundCacheRevalidationNow),
      EventQueuePriority::Idle);
}

void nsHttpChannel::PerformBackgroundCacheRevalidationNow() {
  LOG(("nsHttpChannel::PerformBackgroundCacheRevalidationNow %p", this));

  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  nsLoadFlags loadFlags = mLoadFlags | LOAD_ONLY_IF_MODIFIED | VALIDATE_ALWAYS |
                          LOAD_BACKGROUND | LOAD_BYPASS_SERVICE_WORKER;

  nsCOMPtr<nsIChannel> validatingChannel;
  rv = NS_NewChannelInternal(getter_AddRefs(validatingChannel), mURI, mLoadInfo,
                             nullptr /* performance storage */, mLoadGroup,
                             mCallbacks, loadFlags);
  if (NS_FAILED(rv)) {
    LOG(("  failed to created the channel, rv=0x%08x",
         static_cast<uint32_t>(rv)));
    return;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(validatingChannel));
  MOZ_ASSERT(httpChannel);
  nsCOMPtr<nsIHttpHeaderVisitor> visitor =
      new CopyNonDefaultHeaderVisitor(httpChannel);
  rv = VisitNonDefaultRequestHeaders(visitor);
  if (NS_FAILED(rv)) {
    LOG(("failed to copy headers to the validating channel, rv=0x%08x",
         static_cast<uint32_t>(rv)));
    return;
  }

  nsCOMPtr<nsISupportsPriority> priority(do_QueryInterface(validatingChannel));
  if (priority) {
    priority->SetPriority(nsISupportsPriority::PRIORITY_LOWEST);
  }

  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(validatingChannel));
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::Tail);
  }

  RefPtr<nsHttpChannel> httpChan = do_QueryObject(validatingChannel);
  if (httpChan) {
    httpChan->mStaleRevalidation = true;
  }

  RefPtr<BackgroundRevalidatingListener> listener =
      new BackgroundRevalidatingListener();
  rv = validatingChannel->AsyncOpen(listener);
  if (NS_FAILED(rv)) {
    LOG(("  failed to open the channel, rv=0x%08x", static_cast<uint32_t>(rv)));
    return;
  }

  LOG(("  %p is re-validating with a new channel %p", this,
       validatingChannel.get()));
}

NS_IMETHODIMP
nsHttpChannel::SetEarlyHintObserver(nsIEarlyHintObserver* aObserver) {
  mEarlyHintObserver = aObserver;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpChannel::EarlyHint(const nsACString& aLinkHeader,
                         const nsACString& aReferrerPolicy,
                         const nsACString& aCspHeader) {
  LOG(("nsHttpChannel::EarlyHint.\n"));

  if (mEarlyHintObserver && nsContentUtils::ComputeIsSecureContext(this)) {
    LOG(("nsHttpChannel::EarlyHint propagated.\n"));
    mEarlyHintObserver->EarlyHint(aLinkHeader, aReferrerPolicy, aCspHeader);
  }
  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::SetResponseOverride(
    nsIReplacedHttpResponse* aReplacedHttpResponse) {
  mOverrideResponse = new nsMainThreadPtrHolder<nsIReplacedHttpResponse>(
      "nsIReplacedHttpResponse", aReplacedHttpResponse);
  return NS_OK;
}

NS_IMETHODIMP nsHttpChannel::SetResponseStatus(uint32_t aStatus,
                                               const nsACString& aStatusText) {
  if (!mResponseHead) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsAutoCString statusText(aStatusText);
  nsAutoCString protocolVersion(
      nsHttp::GetProtocolVersion(mResponseHead->Version()));
  ToUpperCase(protocolVersion);

  nsPrintfCString line("%s %u %s", protocolVersion.get(), aStatus,
                       statusText.get());

  return mResponseHead->ParseStatusLine(line);
}

NS_IMETHODIMP nsHttpChannel::SetWebTransportSessionEventListener(
    WebTransportSessionEventListener* aListener) {
  mWebTransportSessionEventListener = aListener;
  return NS_OK;
}

already_AddRefed<WebTransportSessionEventListener>
nsHttpChannel::GetWebTransportSessionEventListener() {
  RefPtr<WebTransportSessionEventListener> wt =
      mWebTransportSessionEventListener;
  return wt.forget();
}

NS_IMETHODIMP nsHttpChannel::GetLastTransportStatus(
    nsresult* aLastTransportStatus) {
  *aLastTransportStatus = mLastTransportStatus;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
