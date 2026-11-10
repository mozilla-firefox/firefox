/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "nsHttpHandler.h"

#include <bitset>

#include "ASpdySession.h"
#include "EventTokenBucket.h"
#include "HttpLog.h"
#include "LoadContextInfo.h"
#include "SerializedLoadContext.h"
#include "TRRServiceChannel.h"
#include "Tickler.h"
#include "mozilla/AntiTrackingRedirectHeuristic.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Base64.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/DynamicFpiRedirectHeuristic.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/LazyIdleThread.h"
#include "mozilla/OriginAttributesHashKey.h"
#include "mozilla/Printf.h"
#include "mozilla/RandomNum.h"
#include "mozilla/SHA1.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/network/Connection.h"
#include "mozilla/glean/GleanPings.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/RequestContextService.h"
#include "mozilla/net/rust_helper.h"
#include "nsAsyncRedirectVerifyHelper.h"
#include "nsCOMPtr.h"
#include "nsCORSListenerProxy.h"
#include "nsCRT.h"
#include "nsCategoryManagerUtils.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsError.h"
#include "nsHTTPCompressConv.h"
#include "nsHttp.h"
#include "nsHttpAuthCache.h"
#include "nsHttpChannel.h"
#include "nsHttpChannelAuthProvider.h"
#include "nsHttpConnectionMgr.h"
#include "nsICookieService.h"
#include "nsIHttpActivityObserver.h"
#include "nsINetworkLinkService.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsIParentalControlsService.h"
#include "nsISiteIntegrityService.h"
#include "nsISiteSecurityService.h"
#include "nsISocketProvider.h"
#include "nsIStreamConverterService.h"
#include "nsISupportsPrimitives.h"
#include "nsIXULAppInfo.h"
#include "nsIXULRuntime.h"
#include "nsNSSComponent.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsRFPService.h"
#include "nsServiceManagerUtils.h"
#include "nsSocketProviderService.h"
#include "nsSocketTransportService2.h"
#include "nsStandardURL.h"
#include "prsystem.h"

#if defined(XP_UNIX)
#  include <sys/utsname.h>
#endif

#if defined(MOZ_WIDGET_GTK)
#  include "mozilla/WidgetUtilsGtk.h"
#endif

#if defined(XP_WIN)
#  include <windows.h>

#  include "mozilla/WindowsVersion.h"
#endif

#if defined(XP_MACOSX)
#  include <CoreServices/CoreServices.h>
#endif

#if defined(XP_MACOSX) || defined(XP_IOS)
#  include "mozilla/net/AppleFastDatapathProbe.h"
#endif

//-----------------------------------------------------------------------------
#include "mozilla/net/HttpChannelChild.h"

#define UA_PREF_PREFIX "general.useragent."
#ifdef XP_WIN
#  define UA_SPARE_PLATFORM
#endif

#define HTTP_PREF_PREFIX "network.http."
#define INTL_ACCEPT_LANGUAGES "intl.accept_languages"
#define BROWSER_PREF_PREFIX "browser.cache."
#define H2MANDATORY_SUITE "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256"
#define SAFE_HINT_HEADER_VALUE "safeHint.enabled"
#define SECURITY_PREFIX "security."
#define DOM_SECURITY_PREFIX "dom.security"

#define ACCEPT_HEADER_STYLE "text/css,*/*;q=0.1"
#define ACCEPT_HEADER_JSON "application/json,*/*;q=0.5"
#define ACCEPT_HEADER_TEXT "text/plain,*/*;q=0.5"
#define ACCEPT_HEADER_ALL "*/*"

#define UA_PREF(_pref) UA_PREF_PREFIX _pref
#define HTTP_PREF(_pref) HTTP_PREF_PREFIX _pref
#define BROWSER_PREF(_pref) BROWSER_PREF_PREFIX _pref

#define NS_HTTP_PROTOCOL_FLAGS \
  (URI_STD | ALLOWS_PROXY | ALLOWS_PROXY_HTTP | URI_LOADABLE_BY_ANYONE)

//-----------------------------------------------------------------------------

using mozilla::dom::Promise;

namespace mozilla::net {

LazyLogModule gHttpLog("nsHttp");
LazyLogModule gHttpIOLog("HttpIO");

#ifdef ANDROID
static nsCString GetDeviceModelId() {
  // Assumed to be running on the main thread
  // We need the device property in either case
  nsAutoCString deviceModelId;
  nsCOMPtr<nsIPropertyBag2> infoService;
  infoService = mozilla::components::SystemInfo::Service();
  MOZ_ASSERT(infoService, "Could not find a system info service");
  nsAutoString androidDevice;
  nsresult rv = infoService->GetPropertyAsAString(u"device"_ns, androidDevice);
  if (NS_SUCCEEDED(rv)) {
    deviceModelId = NS_LossyConvertUTF16toASCII(androidDevice);
  }
  nsAutoCString deviceString;
  rv = Preferences::GetCString(UA_PREF("device_string"), deviceString);
  if (NS_SUCCEEDED(rv)) {
    deviceString.Trim(" ", true, true);
    deviceString.ReplaceSubstring("%DEVICEID%"_ns, deviceModelId);
    return std::move(deviceString);
  }
  return std::move(deviceModelId);
}
#endif

#ifdef XP_UNIX
static bool IsRunningUnderUbuntuSnap() {
#  if defined(MOZ_WIDGET_GTK)
  if (!widget::IsRunningUnderSnap()) {
    return false;
  }

  char version[100];
  if (PR_GetSystemInfo(PR_SI_RELEASE_BUILD, version, sizeof(version)) ==
      PR_SUCCESS) {
    if (strstr(version, "Ubuntu")) {
      return true;
    }
  }
#  endif
  return false;
}
#endif

//-----------------------------------------------------------------------------
// nsHttpHandler <public>
//-----------------------------------------------------------------------------

StaticRefPtr<nsHttpHandler> gHttpHandler;

/* static */
already_AddRefed<nsHttpHandler> nsHttpHandler::GetInstance() {
  if (!gHttpHandler) {
    gHttpHandler = new nsHttpHandler();
    DebugOnly<nsresult> rv = gHttpHandler->Init();
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    // There is code that may be executed during the final cycle collection
    // shutdown and still referencing gHttpHandler. Not earlier than that:
    // resolving an atom once the table is gone asserts.
    RunOnShutdown(
        [] {
          gHttpHandler->Shutdown();
          gHttpHandler = nullptr;
        },
        ShutdownPhase::CCPostLastCycleCollection);
  }
  RefPtr<nsHttpHandler> httpHandler = gHttpHandler;
  return httpHandler.forget();
}

/// Derive the HTTP Accept header for image requests based on the enabled prefs
/// for non-universal image types. This may be overridden in its entirety by
/// the image.http.accept pref.
static nsCString ImageAcceptHeader() {
  nsCString mimeTypes;

  mimeTypes.Append("image/avif,");

#ifdef MOZ_JXL
  if (mozilla::StaticPrefs::image_jxl_enabled()) {
    mimeTypes.Append("image/jxl,");
  }
#endif

  mimeTypes.Append("image/webp,");

  // Default value as specified by fetch standard
  // https://fetch.spec.whatwg.org/commit-snapshots/8dd73dbecfefdbef8f432164fb3a5b9785f7f520/#ref-for-header-list-contains%E2%91%A7
  mimeTypes.Append("image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5");

  return mimeTypes;
}

static nsCString DocumentAcceptHeader() {
  // https://fetch.spec.whatwg.org/#document-accept-header-value
  // The value specified by the fetch standard is
  // `text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8`
  nsCString mimeTypes("text/html,application/xhtml+xml,application/xml;q=0.9,");

  // we also insert all of the image formats before */* when the pref is set
  if (mozilla::StaticPrefs::network_http_accept_include_images()) {
    mimeTypes.Append("image/avif,");

#ifdef MOZ_JXL
    if (mozilla::StaticPrefs::image_jxl_enabled()) {
      mimeTypes.Append("image/jxl,");
    }
#endif

    mimeTypes.Append("image/webp,image/png,image/svg+xml,");
  }

  mimeTypes.Append("*/*;q=0.8");

  return mimeTypes;
}

Atomic<bool, Relaxed> nsHttpHandler::sParentalControlsEnabled(false);

nsHttpHandler::nsHttpHandler()
    : mAuthCache(new nsHttpAuthCache()),
      mPrivateAuthCache(new nsHttpAuthCache()),
      mIdleTimeout(PR_SecondsToInterval(10)),
      mSpdyTimeout(
          PR_SecondsToInterval(StaticPrefs::network_http_http2_timeout())),
      mResponseTimeout(PR_SecondsToInterval(300)),
      mImageAcceptHeader(ImageAcceptHeader()),
      mDocumentAcceptHeader(DocumentAcceptHeader()),
      mLastUniqueID(NowInSeconds()),
      mIdempotencyKeySeed(mozilla::RandomUint64OrDie()),
      mPrivateBrowsingIdempotencyKeySeed(mozilla::RandomUint64OrDie()),
      mDebugObservations(false),
      mEnableAltSvc(false),
      mSpdyPingThreshold(PR_SecondsToInterval(
          StaticPrefs::network_http_http2_ping_threshold())),
      mSpdyPingTimeout(PR_SecondsToInterval(
          StaticPrefs::network_http_http2_ping_timeout())) {
  LOG(("Creating nsHttpHandler [this=%p].\n", this));

  mAuthCache->Init();
  mPrivateAuthCache->Init();

  mUserAgentOverride.SetIsVoid(true);

  MOZ_ASSERT(!gHttpHandler, "HTTP handler already created!");

  nsCOMPtr<nsIXULRuntime> runtime;
  runtime = mozilla::components::XULRuntime::Service();
  if (runtime) {
    runtime->GetProcessID(&mProcessId);
    runtime->GetUniqueProcessID(&mUniqueProcessId);
  }
}

nsHttpHandler::~nsHttpHandler() {
  LOG(("Deleting nsHttpHandler [this=%p]\n", this));

  // Note: don't call NeckoChild::DestroyNeckoChild() here, as it's too late
  // and it'll segfault.  NeckoChild will get cleaned up by process exit.

  Shutdown();

  mConnMgr = nullptr;
}

void nsHttpHandler::Shutdown() {
  if (mShutdownCalled.exchange(true)) {
    return;
  }

  LOG(("nsHttpHandler::Shutdown [this=%p]\n", this));

  ShutdownConnectionManager();

  nsHttp::DestroyAtomTable();
}

static const char* gCallbackPrefs[] = {
    HTTP_PREF_PREFIX,
    UA_PREF_PREFIX,
    INTL_ACCEPT_LANGUAGES,
    BROWSER_PREF("disk_cache_ssl"),
    H2MANDATORY_SUITE,
    HTTP_PREF("tcp_keepalive.short_lived_connections"),
    HTTP_PREF("tcp_keepalive.long_lived_connections"),
    SAFE_HINT_HEADER_VALUE,
    SECURITY_PREFIX,
    DOM_SECURITY_PREFIX,
    "image.http.accept",
    "image.jxl.enabled",
    nullptr,
};

nsresult nsHttpHandler::Init() {
  nsresult rv;

  LOG(("nsHttpHandler::Init\n"));
  MOZ_ASSERT(NS_IsMainThread());

  // We should not create nsHttpHandler during shutdown, but we have some
  // xpcshell tests doing this.
  if (MOZ_UNLIKELY(AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdown) &&
                   !PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR"))) {
    MOZ_DIAGNOSTIC_CRASH("Try to init HttpHandler after shutdown");
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  rv = nsHttp::CreateAtomTable();
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIIOService> service;
  service = mozilla::components::IO::Service(&rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("unable to continue without io service");
    return rv;
  }
  mIOService = new nsMainThreadPtrHolder<nsIIOService>(
      "nsHttpHandler::mIOService", service);

#if defined(XP_MACOSX) || defined(XP_IOS)
  if (XRE_IsParentProcess()) {
    (void)NS_DispatchToMainThreadQueue(
        NS_NewRunnableFunction("net::StartAppleFastDatapathProbe",
                               &StartAppleFastDatapathProbe),
        EventQueuePriority::Idle);
  }
#endif

  if (IsNeckoChild()) NeckoChild::InitNeckoChild();

  InitUserAgentComponents();

  // This preference is only used in parent process.
  if (!IsNeckoChild()) {
    if (XRE_IsParentProcess()) {
      mDictionaryCache = DictionaryCache::GetInstance();
      // mDictionaryCache can be null if shutdown has occurred

      std::bitset<3> usageOfHTTPSRRPrefs;
      usageOfHTTPSRRPrefs[0] = StaticPrefs::network_dns_upgrade_with_https_rr();
      usageOfHTTPSRRPrefs[1] =
          StaticPrefs::network_dns_use_https_rr_as_altsvc();
      usageOfHTTPSRRPrefs[2] = StaticPrefs::network_dns_echconfig_enabled();
      glean::networking::https_rr_prefs_usage.Set(
          static_cast<uint32_t>(usageOfHTTPSRRPrefs.to_ulong()));
      glean::networking::http3_enabled.Set(
          StaticPrefs::network_http_http3_enable());
    }

    mActivityDistributor = components::HttpActivityDistributor::Service();

    auto initQLogDir = [&]() {
      if (!StaticPrefs::network_http_http3_enable_qlog()) {
        return EmptyCString();
      }

      nsCOMPtr<nsIFile> qlogDir;
      nsresult rv =
          NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(qlogDir));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return EmptyCString();
      }

      nsAutoCString dirName("qlog_");
      dirName.AppendInt(mProcessId);
      rv = qlogDir->AppendNative(dirName);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return EmptyCString();
      }

      return qlogDir->HumanReadablePath();
    };
    mHttp3QlogDir = initQLogDir();

    if (const char* origin = PR_GetEnv("MOZ_FORCE_QUIC_ON")) {
      nsCCharSeparatedTokenizer tokens(nsDependentCString(origin), ':');
      nsAutoCString host;
      int32_t port = 443;
      if (tokens.hasMoreTokens()) {
        host = tokens.nextToken();
        if (tokens.hasMoreTokens()) {
          nsresult res;
          int32_t tmp = tokens.nextToken().ToInteger(&res);
          if (NS_SUCCEEDED(res)) {
            port = tmp;
          }
        }
        auto map = mAltSvcMappingTemptativeMap.Lock();
        map->InsertOrUpdate(
            host, std::make_unique<nsCString>(nsPrintfCString("h3=:%d", port)));
      }
    }
  }

  // monitor some preference changes
  Preferences::RegisterPrefixCallbacks(nsHttpHandler::PrefsChanged,
                                       gCallbackPrefs, this);
  PrefsChanged(nullptr);

  mCompatFirefox.AssignLiteral("Firefox/" MOZILLA_UAVERSION);

  nsCOMPtr<nsIXULAppInfo> appInfo;
  appInfo = mozilla::components::XULRuntime::Service();

  mAppName.AssignLiteral(MOZ_APP_UA_NAME);
  if (mAppName.Length() == 0 && appInfo) {
    // Try to get the UA name from appInfo, falling back to the name
    appInfo->GetUAName(mAppName);
    if (mAppName.Length() == 0) {
      appInfo->GetName(mAppName);
    }
    appInfo->GetVersion(mAppVersion);
    mAppName.StripChars(R"( ()<>@,;:\"/[]?={})");
  } else {
    mAppVersion.AssignLiteral(MOZ_APP_UA_VERSION);
  }

  mMisc.AssignLiteral("rv:" MOZILLA_UAVERSION);

  // Generate the spoofed User Agent for fingerprinting resistance.
  nsRFPService::GetSpoofedUserAgent(mSpoofedUserAgent);

  mSessionStartTime = NowInSeconds();
  mHandlerActive = true;

  rv = InitConnectionMgr();
  if (NS_FAILED(rv)) return rv;

  mAltSvcCache = std::make_unique<AltSvcCache>();

  mRequestContextService = RequestContextService::GetOrCreate();

#if defined(ANDROID) || defined(XP_IOS)
  mProductSub.AssignLiteral(MOZILLA_UAVERSION);
#else
  mProductSub.AssignLiteral(LEGACY_UA_GECKO_TRAIL);
#endif

#if DEBUG
  // dump user agent prefs
  LOG(("> legacy-app-name = %s\n", mLegacyAppName.get()));
  LOG(("> legacy-app-version = %s\n", mLegacyAppVersion.get()));
  LOG(("> platform = %s\n", mPlatform.get()));
  LOG(("> oscpu = %s\n", mOscpu.get()));
  LOG(("> misc = %s\n", mMisc.get()));
  LOG(("> product = %s\n", mProduct.get()));
  LOG(("> product-sub = %s\n", mProductSub.get()));
  LOG(("> app-name = %s\n", mAppName.get()));
  LOG(("> app-version = %s\n", mAppVersion.get()));
  LOG(("> compat-firefox = %s\n", mCompatFirefox.get()));
  LOG(("> user-agent = %s\n", UserAgent(false).get()));
#endif

  // Startup the http category
  // Bring alive the objects in the http-protocol-startup category
  NS_CreateServicesFromCategory(
      NS_HTTP_STARTUP_CATEGORY,
      static_cast<nsISupports*>(static_cast<void*>(this)),
      NS_HTTP_STARTUP_TOPIC);

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    // register the handler object as a weak callback as we don't need to worry
    // about shutdown ordering.
    obsService->AddObserver(this, "profile-change-net-teardown", true);
    obsService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
    obsService->AddObserver(this, "net:clear-active-logins", true);
    obsService->AddObserver(this, "net:prune-dead-connections", true);
    // Sent by the TorButton add-on in the Tor Browser
    obsService->AddObserver(this, "net:prune-all-connections", true);
    obsService->AddObserver(this, "net:cancel-all-connections", true);
    obsService->AddObserver(this, "last-pb-context-exited", true);
    obsService->AddObserver(this, "browser:purge-session-history", true);
    obsService->AddObserver(this, NS_NETWORK_LINK_TOPIC, true);
    obsService->AddObserver(this, "application-background", true);
    obsService->AddObserver(this, "psm:user-certificate-added", true);
    obsService->AddObserver(this, "psm:user-certificate-deleted", true);
    obsService->AddObserver(this, "intl:app-locales-changed", true);
    obsService->AddObserver(this, "browser-delayed-startup-finished", true);
    obsService->AddObserver(this, "network:reset-http3-excluded-list", true);
    obsService->AddObserver(this, "network:reset_third_party_roots_check",
                            true);
    obsService->AddObserver(this, "idle-daily", true);

    if (!IsNeckoChild()) {
      obsService->AddObserver(this, "net:current-browser-id", true);
    }

    // disabled as its a nop right now
    // obsService->AddObserver(this, "net:failed-to-process-uri-content", true);
  }

  MakeNewRequestTokenBucket();
  mWifiTickler = new Tickler();
  if (NS_FAILED(mWifiTickler->Init())) {
    mWifiTickler = nullptr;
  }

  UpdateParentalControlsEnabled(false /* wait for completion */);
  return NS_OK;
}

void nsHttpHandler::UpdateParentalControlsEnabled(bool waitForCompletion) {
  // Child process does not have privileges to read parentals control state
  if (!XRE_IsParentProcess()) {
    return;
  }

  auto getParentalControlsTask = []() {
    nsCOMPtr<nsIParentalControlsService> pc =
        do_CreateInstance("@mozilla.org/parental-controls-service;1");
    if (pc) {
      bool localEnabled = false;
      pc->GetParentalControlsEnabled(&localEnabled);
      sParentalControlsEnabled = localEnabled;

      // Cache the state of parental controls via preference
      if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
        Preferences::SetBool(
            StaticPrefs::GetPrefName_network_parental_controls_cached_state(),
            localEnabled);
      }
    }
  };

  if (waitForCompletion) {
    getParentalControlsTask();
  } else {
    // To avoid blocking on determining parental controls state, used the cached
    // pref until the runnable completes
    sParentalControlsEnabled =
        mozilla::StaticPrefs::network_parental_controls_cached_state();
    (void)NS_DispatchToMainThreadQueue(
        NS_NewRunnableFunction("GetParentalControlsEnabled",
                               std::move(getParentalControlsTask)),
        mozilla::EventQueuePriority::Idle);
  }
}

void nsHttpHandler::GenerateIdempotencyKeyForPost(const uint32_t aPostId,
                                                  nsILoadInfo* aLoadInfo,
                                                  nsACString& aOutKey) {
  MOZ_ASSERT(aLoadInfo);
  OriginAttributes attrs = aLoadInfo->GetOriginAttributes();

  // Create a SHA1 string using the origin attributes, session seed and the post
  // id.
  nsAutoCString sha1Input;
  attrs.CreateSuffix(sha1Input);
  sha1Input.AppendInt(aPostId);
  sha1Input.AppendInt(attrs.IsPrivateBrowsing()
                          ? mPrivateBrowsingIdempotencyKeySeed
                          : mIdempotencyKeySeed);
  SHA1Sum sha1;
  SHA1Sum::Hash hash;
  sha1.update((sha1Input.get()), sha1Input.Length());
  sha1.finish(hash);
  uint64_t hashValue = BigEndian::readUint64(&hash);

  aOutKey.Append("\"");
  aOutKey.AppendInt(hashValue);
  aOutKey.Append("\"");
}

const nsCString& nsHttpHandler::Http3QlogDir() {
  if (StaticPrefs::network_http_http3_enable_qlog()) {
    return mHttp3QlogDir;
  }

  return EmptyCString();
}

void nsHttpHandler::MakeNewRequestTokenBucket() {
  LOG(("nsHttpHandler::MakeNewRequestTokenBucket this=%p child=%d\n", this,
       IsNeckoChild()));
  if (!mConnMgr || IsNeckoChild()) {
    return;
  }
  RefPtr<EventTokenBucket> tokenBucket =
      new EventTokenBucket(RequestTokenBucketHz(), RequestTokenBucketBurst());
  // NOTE The thread or socket may be gone already.
  nsresult rv = mConnMgr->UpdateRequestTokenBucket(tokenBucket);
  if (NS_FAILED(rv)) {
    LOG(("    failed to update request token bucket\n"));
  }
}

nsresult nsHttpHandler::InitConnectionMgr() {
  // Init ConnectionManager only on parent!
  if (IsNeckoChild()) {
    return NS_OK;
  }

  if (mConnMgr) {
    return NS_OK;
  }

  mConnMgr = new nsHttpConnectionMgr();

  return mConnMgr->Init(mMaxUrgentExcessiveConns, mMaxConnections,
                        mMaxPersistentConnectionsPerServer,
                        mMaxPersistentConnectionsPerProxy, mMaxRequestDelay,
                        mThrottleEnabled, mThrottleSuspendFor,
                        mThrottleResumeFor, mThrottleHoldTime, mThrottleMaxTime,
                        mBeConservativeForProxy);
}

// We're using RequestOverride because this can get called when these are
// set by Fetch from the old request. We need to pass a function pointer to
// let GetDictionaryFor suspend the channel before starting the async
// dictionary load.
nsresult nsHttpHandler::AddAcceptAndDictionaryHeaders(
    nsIURI* aURI, ExtContentPolicyType aType, nsHttpRequestHead* aRequest,
    bool aSecure, nsHttpChannel* aChan, void (*aSuspend)(nsHttpChannel*),
    const std::function<bool(bool, DictionaryCacheEntry*)>& aCallback) {
  LOG(("Adding Dictionary headers"));
  auto guard = MakeScopeExit([&]() { (aCallback)(false, nullptr); });

  nsresult rv = NS_OK;
  // Add the "Accept-Encoding" header and possibly Dictionary headers
  if (aSecure) {
    // The dictionary info may require us to check the cache.
    if (StaticPrefs::network_http_dictionaries_enable() && mDictionaryCache) {
      // Note: this is async; the lambda can happen later
      // aCallback will now be owned by GetDictionaryFor
      guard.release();
      RefPtr<LoadContextInfo> lci = GetLoadContextInfo(aChan);
      mDictionaryCache->GetDictionaryFor(
          aURI, aType, lci, aChan, aSuspend,
          [self = RefPtr(this), aRequest, aCallback](
              bool aNeedsResume, DictionaryCacheEntry* aDict) {
            if (!aDict) {
              // Accept-Encoding was already set in AddStandardHeaders
              (aCallback)(aNeedsResume, nullptr);
              return NS_OK;
            }

            nsAutoCStringN<64> encodedHash = ":"_ns + aDict->GetHash() + ":"_ns;

            // We want to make sure that the cache entry doesn't disappear out
            // from under us if we set the header, so do the callback to
            // Prefetch() the entry before adding the headers (so we don't
            // have to remove the headers if Prefetch() fails).  It might fail
            // if something is asynchronously Dooming the entry but the
            // DictionaryCache hasn't been updated to remove the entry yet (or
            // any other thing that were to desynchronize the DictionaryCache
            // with the actual cache.
            if ((aCallback)(aNeedsResume, aDict)) {
              LOG_DICTIONARIES(
                  ("Setting Available-Dictionary: %s", encodedHash.get()));
              // Need to retain access to the dictionary until the request
              // completes. Note that this includes if the dictionary we offered
              // gets replaced by another request while we're waiting for a
              // response; in that case we need to read in a copy of the
              // dictionary into memory before overwriting it and store in dict
              // temporarily.
              aRequest->SetDictionary(aDict);

              nsresult rv = aRequest->SetHeader(
                  nsHttp::Available_Dictionary, encodedHash, false,
                  nsHttpHeaderArray::eVarietyRequestOverride);
              if (NS_FAILED(rv)) {
                return rv;
              }
              if (!aDict->GetId().IsEmpty()) {
                nsPrintfCString id("\"%s\"", aDict->GetId().get());
                LOG_DICTIONARIES(("Setting Dictionary-Id: %s", id.get()));
                rv = aRequest->SetHeader(
                    nsHttp::Dictionary_Id, id, false,
                    nsHttpHeaderArray::eVarietyRequestOverride);
                if (NS_FAILED(rv)) {
                  return rv;
                }
              }
              return aRequest->SetHeader(
                  nsHttp::Accept_Encoding, self->mDictionaryAcceptEncodings,
                  false, nsHttpHeaderArray::eVarietyRequestOverride);
            }  // else probably Prefetch failed
            return NS_OK;
          });
    }
  }
  // guard may call aCallback here
  return rv;
}

nsresult nsHttpHandler::AddStandardRequestHeaders(
    nsHttpRequestHead* request, nsIURI* aURI, bool aIsHTTPS,
    ExtContentPolicyType aContentPolicyType, bool aShouldResistFingerprinting,
    const nsCString& aLanguageOverride) {
  nsresult rv;

  // Add the "User-Agent" header
  rv = request->SetHeader(nsHttp::User_Agent,
                          UserAgent(aShouldResistFingerprinting), false,
                          nsHttpHeaderArray::eVarietyRequestDefault);
  if (NS_FAILED(rv)) return rv;

  // MIME based content negotiation lives!
  // Add the "Accept" header.  Note, this is set as an override because the
  // service worker expects to see it.  The other "default" headers are
  // hidden from service worker interception.
  nsAutoCString accept;
  if (aContentPolicyType == ExtContentPolicy::TYPE_DOCUMENT ||
      aContentPolicyType == ExtContentPolicy::TYPE_SUBDOCUMENT) {
    accept.Assign(mDocumentAcceptHeader);
  } else if (aContentPolicyType == ExtContentPolicy::TYPE_IMAGE ||
             aContentPolicyType == ExtContentPolicy::TYPE_IMAGESET) {
    accept.Assign(mImageAcceptHeader);
  } else if (aContentPolicyType == ExtContentPolicy::TYPE_STYLESHEET) {
    accept.Assign(ACCEPT_HEADER_STYLE);
  } else if (aContentPolicyType == ExtContentPolicy::TYPE_JSON) {
    accept.Assign(ACCEPT_HEADER_JSON);
  } else if (aContentPolicyType == ExtContentPolicy::TYPE_TEXT) {
    accept.Assign(ACCEPT_HEADER_TEXT);
  } else {
    accept.Assign(ACCEPT_HEADER_ALL);
  }

  rv = request->SetHeader(nsHttp::Accept, accept, false,
                          nsHttpHeaderArray::eVarietyRequestOverride);
  if (NS_FAILED(rv)) return rv;

  if (!aLanguageOverride.IsEmpty()) {
    nsAutoCString acceptLanguage;
    acceptLanguage.Assign(aLanguageOverride.get());
    rv = request->SetHeader(nsHttp::Accept_Language, acceptLanguage, false,
                            nsHttpHeaderArray::eVarietyRequestOverride);
    if (NS_FAILED(rv)) return rv;
  } else {
    // Add the "Accept-Language" header.  This header is also exposed to the
    // service worker.
    if (mAcceptLanguagesIsDirty) {
      rv = SetAcceptLanguages();
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    // Add the "Accept-Language" header
    if (!mAcceptLanguages.IsEmpty()) {
      rv = request->SetHeader(nsHttp::Accept_Language, mAcceptLanguages, false,
                              nsHttpHeaderArray::eVarietyRequestOverride);
      if (NS_FAILED(rv)) return rv;
    }
  }

  // add the "Send Hint" header
  if (mSafeHintEnabled || sParentalControlsEnabled) {
    rv = request->SetHeader(nsHttp::Prefer, "safe"_ns, false,
                            nsHttpHeaderArray::eVarietyRequestDefault);
    if (NS_FAILED(rv)) return rv;
  }

  if (aIsHTTPS) {
    rv = request->SetHeader(nsHttp::Accept_Encoding, mHttpsAcceptEncodings,
                            false, nsHttpHeaderArray::eVarietyRequestDefault);
  } else {
    rv = request->SetHeader(nsHttp::Accept_Encoding, mHttpAcceptEncodings,
                            false, nsHttpHeaderArray::eVarietyRequestDefault);
  }
  return NS_OK;
}

nsresult nsHttpHandler::AddConnectionHeader(nsHttpRequestHead* request,
                                            uint32_t caps) {
  // RFC2616 section 19.6.2 states that the "Connection: keep-alive"
  // and "Keep-alive" request headers should not be sent by HTTP/1.1
  // user-agents.  But this is not a problem in practice, and the
  // alternative proxy-connection is worse. see 570283

  constexpr auto close = "close"_ns;
  constexpr auto keepAlive = "keep-alive"_ns;

  const nsLiteralCString* connectionType = &close;
  if (caps & NS_HTTP_ALLOW_KEEPALIVE) {
    connectionType = &keepAlive;
  }

  return request->SetHeader(nsHttp::Connection, *connectionType);
}

bool nsHttpHandler::IsAcceptableEncoding(const char* enc, bool isSecure) {
  if (!enc) return false;

  // we used to accept x-foo anytime foo was acceptable, but that's just
  // continuing bad behavior.. so limit it to known x-* patterns
  bool rv;
  if (isSecure) {
    // Should be a superset of mAcceptEncodings (unless someone messes with
    // prefs)
    rv = nsHttp::FindToken(mDictionaryAcceptEncodings.get(), enc,
                           HTTP_LWS ",") != nullptr;
  } else {
    rv = nsHttp::FindToken(mHttpAcceptEncodings.get(), enc, HTTP_LWS ",") !=
         nullptr;
  }
  // gzip and deflate are inherently acceptable in modern HTTP - always
  // process them if a stream converter can also be found.
  if (!rv &&
      (!nsCRT::strcasecmp(enc, "gzip") || !nsCRT::strcasecmp(enc, "deflate") ||
       !nsCRT::strcasecmp(enc, "x-gzip") ||
       !nsCRT::strcasecmp(enc, "x-deflate"))) {
    rv = true;
  }
  LOG(("nsHttpHandler::IsAceptableEncoding %s https=%d %d\n", enc, isSecure,
       rv));
  return rv;
}

nsISiteIntegrityService* nsHttpHandler::GetSiteIntegrityService() {
  if (!mSiteIntegrityService) {
    nsCOMPtr<nsISiteIntegrityService> service;
    service = mozilla::components::SiteIntegrity::Service();
    mSiteIntegrityService = new nsMainThreadPtrHolder<nsISiteIntegrityService>(
        "nsHttpHandler::mSiteIntegrityService", service);
  }
  return mSiteIntegrityService;
}

nsISiteSecurityService* nsHttpHandler::GetSSService() {
  if (!mSSService) {
    nsCOMPtr<nsISiteSecurityService> service;
    service = mozilla::components::SiteSecurity::Service();
    mSSService = new nsMainThreadPtrHolder<nsISiteSecurityService>(
        "nsHttpHandler::mSSService", service);
  }
  return mSSService;
}

nsICookieService* nsHttpHandler::GetCookieService() {
  if (!mCookieService) {
    nsCOMPtr<nsICookieService> service =
        do_GetService(NS_COOKIESERVICE_CONTRACTID);
    mCookieService = new nsMainThreadPtrHolder<nsICookieService>(
        "nsHttpHandler::mCookieService", service);
  }
  return mCookieService;
}

nsresult nsHttpHandler::GetIOService(nsIIOService** result) {
  NS_ENSURE_ARG_POINTER(result);

  *result = do_AddRef(mIOService.get()).take();
  return NS_OK;
}

void nsHttpHandler::NotifyObservers(nsIChannel* chan, const char* event) {
  LOG(("nsHttpHandler::NotifyObservers [this=%p chan=%p event=\"%s\"]\n", this,
       chan, event));
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) obsService->NotifyObservers(chan, event, nullptr);
}

nsresult nsHttpHandler::AsyncOnChannelRedirect(
    nsIChannel* oldChan, nsIChannel* newChan, uint32_t flags,
    nsIEventTarget* mainThreadEventTarget) {
  MOZ_ASSERT(NS_IsMainThread() && (oldChan && newChan));

  nsCOMPtr<nsIURI> oldURI;
  oldChan->GetURI(getter_AddRefs(oldURI));
  MOZ_ASSERT(oldURI);

  nsCOMPtr<nsIURI> newURI;
  newChan->GetURI(getter_AddRefs(newURI));
  MOZ_ASSERT(newURI);

  PrepareForAntiTrackingRedirectHeuristic(oldChan, oldURI, newChan, newURI);

  DynamicFpiRedirectHeuristic(oldChan, oldURI, newChan, newURI);

  // TODO E10S This helper has to be initialized on the other process
  RefPtr<nsAsyncRedirectVerifyHelper> redirectCallbackHelper =
      new nsAsyncRedirectVerifyHelper();

  return redirectCallbackHelper->Init(oldChan, newChan, flags,
                                      mainThreadEventTarget);
}

/* static */
nsresult nsHttpHandler::GenerateHostPort(const nsCString& host, int32_t port,
                                         nsACString& hostLine) {
  return NS_GenerateHostPort(host, port, hostLine);
}

// static
uint8_t nsHttpHandler::UrgencyFromCoSFlags(uint32_t cos,
                                           int32_t aSupportsPriority) {
  uint8_t urgency;
  if (cos & nsIClassOfService::UrgentStart) {
    // coming from an user interaction => response should be the highest
    // priority
    urgency = 1;
  } else if (cos & nsIClassOfService::Leader) {
    // main html document normal priority
    urgency = 2;
  } else if (cos & nsIClassOfService::Unblocked) {
    urgency = 3;
  } else if (cos & nsIClassOfService::Follower) {
    urgency = 4;
  } else if (cos & nsIClassOfService::Speculative) {
    urgency = 6;
  } else if (cos & nsIClassOfService::Background) {
    // background tasks can be deprioritzed to the lowest priority
    urgency = 6;
  } else if (cos & nsIClassOfService::Tail) {
    urgency = mozilla::StaticPrefs::network_http_tailing_urgency();
  } else {
    // all others get a lower priority than the main html document
    urgency = 4;
  }

  int8_t adjustment = 0;
  if (mozilla::StaticPrefs::network_fetchpriority_adjust_urgency()) {
    if (aSupportsPriority <= nsISupportsPriority::PRIORITY_HIGHEST) {
      adjustment = -2;
    } else if (aSupportsPriority <= nsISupportsPriority::PRIORITY_HIGH) {
      adjustment = -1;
    } else if (aSupportsPriority >= nsISupportsPriority::PRIORITY_LOWEST) {
      adjustment = 2;
    } else if (aSupportsPriority >= nsISupportsPriority::PRIORITY_LOW) {
      adjustment = 1;
    }
  }

  auto adjustUrgency = [](uint8_t u, int8_t a) -> uint8_t {
    int16_t result = static_cast<int16_t>(u) + a;
    if (result <= 0) {
      return 0;
    }
    if (result >= 6) {
      return 6;
    }
    return result;
  };

  return adjustUrgency(urgency, adjustment);
}

//-----------------------------------------------------------------------------
// nsHttpHandler <private>
//-----------------------------------------------------------------------------

const nsCString& nsHttpHandler::UserAgent(bool aShouldResistFingerprinting) {
  if (aShouldResistFingerprinting && !mSpoofedUserAgent.IsEmpty()) {
    LOG(("using spoofed userAgent : %s\n", mSpoofedUserAgent.get()));
    return mSpoofedUserAgent;
  }

  if (!mUserAgentOverride.IsVoid()) {
    LOG(("using general.useragent.override : %s\n", mUserAgentOverride.get()));
    return mUserAgentOverride;
  }

  if (mUserAgentIsDirty) {
    BuildUserAgent();
    mUserAgentIsDirty = false;
  }

  return mUserAgent;
}

void nsHttpHandler::BuildUserAgent() {
  LOG(("nsHttpHandler::BuildUserAgent\n"));

  MOZ_ASSERT(!mLegacyAppName.IsEmpty() && !mLegacyAppVersion.IsEmpty(),
             "HTTP cannot send practical requests without this much");

  // preallocate to worst-case size, which should always be better
  // than if we didn't preallocate at all.
  mUserAgent.SetCapacity(mLegacyAppName.Length() + mLegacyAppVersion.Length() +
#ifndef UA_SPARE_PLATFORM
                         mPlatform.Length() +
#endif
                         mOscpu.Length() + mMisc.Length() + mProduct.Length() +
                         mProductSub.Length() + mAppName.Length() +
                         mAppVersion.Length() + mCompatFirefox.Length() +
                         mCompatDevice.Length() + mDeviceModelId.Length() + 13);

  // Application portion
  mUserAgent.Assign(mLegacyAppName);
  mUserAgent += '/';
  mUserAgent += mLegacyAppVersion;
  mUserAgent += ' ';

  // Application comment
  mUserAgent += '(';
#ifndef UA_SPARE_PLATFORM
  if (!mPlatform.IsEmpty()) {
    mUserAgent += mPlatform;
    mUserAgent.AppendLiteral("; ");
  }
#endif
  if (!mCompatDevice.IsEmpty()) {
    mUserAgent += mCompatDevice;
    mUserAgent.AppendLiteral("; ");
  } else if (!mOscpu.IsEmpty()) {
    mUserAgent += mOscpu;
    mUserAgent.AppendLiteral("; ");
  }
  if (!mDeviceModelId.IsEmpty()) {
    mUserAgent += mDeviceModelId;
    mUserAgent.AppendLiteral("; ");
  }
  mUserAgent += mMisc;
  mUserAgent += ')';

  // Product portion
  mUserAgent += ' ';
  mUserAgent += mProduct;
  mUserAgent += '/';
  mUserAgent += mProductSub;

  bool isFirefox = mAppName.EqualsLiteral("Firefox");
  if (isFirefox || mCompatFirefoxEnabled) {
    // "Firefox/x.y" (compatibility) app token
    mUserAgent += ' ';
    mUserAgent += mCompatFirefox;
  }
  if (!isFirefox) {
    // App portion
    mUserAgent += ' ';
    mUserAgent += mAppName;
    mUserAgent += '/';
    mUserAgent += mAppVersion;
  }
}

#ifdef XP_WIN
// Hardcode the reported Windows version to 10.0. This way, Microsoft doesn't
// get to change Web compat-sensitive values without our veto. The compat-
// sensitivity keeps going up as 10.0 stays as the current value for longer
// and longer. If the system-reported version ever changes, we'll be able to
// take our time to evaluate the Web compat impact instead of having to
// scramble to react like happened with macOS changing from 10.x to 11.x.
#  define OSCPU_WINDOWS "Windows NT 10.0"
#  define OSCPU_WIN64 OSCPU_WINDOWS "; Win64; x64"
#endif

void nsHttpHandler::InitUserAgentComponents() {
  // Gather platform.
  mPlatform.AssignLiteral(
#if defined(ANDROID)
      "Android"
#elif defined(XP_WIN)
      "Windows"
#elif defined(XP_MACOSX)
      "Macintosh"
#elif defined(XP_IOS)
      "iPhone"
#elif defined(XP_UNIX)
      // We historically have always had X11 here,
      // and there seems little a webpage can sensibly do
      // based on it being something else, so use X11 for
      // backwards compatibility in all cases.
      "X11"
#endif
  );

#ifdef XP_UNIX
  if (IsRunningUnderUbuntuSnap()) {
    mPlatform.AppendLiteral("; Ubuntu");
  }
#endif

#ifdef ANDROID
  nsCOMPtr<nsIPropertyBag2> infoService;
  infoService = mozilla::components::SystemInfo::Service();
  MOZ_ASSERT(infoService, "Could not find a system info service");
  nsresult rv;

  // Add the Android version number to the Fennec platform identifier.
  nsAutoString androidVersion;
  rv = infoService->GetPropertyAsAString(u"release_version"_ns, androidVersion);
  MOZ_ASSERT_IF(
      NS_SUCCEEDED(rv),
      // Like version "9"
      (androidVersion.Length() == 1 && std::isdigit(androidVersion[0])) ||
          // Or like version "8.1", "10", or "12.1"
          (androidVersion.Length() >= 2 && std::isdigit(androidVersion[0]) &&
           (androidVersion[1] == u'.' || std::isdigit(androidVersion[1]))));

  // Normalize: strip any minor-version suffix (everything from the first '.'
  // onward, including the '.'). Some OEM firmwares report Build.VERSION.RELEASE
  // as e.g. "14.0" while stock AOSP reports "14"; that variance splits the
  // population into fingerprintable subsets without conveying any useful
  // information about the OS. Bug 2043395.
  int32_t dotIdx = androidVersion.FindChar(u'.');
  if (dotIdx >= 0) {
    androidVersion.Truncate(dotIdx);
  }

  // Spoof version "Android 10" for Android OS versions < 10 to reduce their
  // fingerprintable user information. For Android OS versions >= 10, report
  // the real OS version because some enterprise websites only want to permit
  // clients with recent OS version (like bug 1876742). After the truncation
  // above, all versions are bare integers; two digits means the version is
  // >= 10.
  mPlatform += " ";
  if (NS_SUCCEEDED(rv) && androidVersion.Length() >= 2 &&
      std::isdigit(androidVersion[0]) && std::isdigit(androidVersion[1])) {
    mPlatform += NS_LossyConvertUTF16toASCII(androidVersion);
  } else {
    mPlatform.AppendLiteral("10");
  }

  // Add the `Mobile` or `TV` token when running on device.
  bool isTV;
  rv = infoService->GetPropertyAsBool(u"tv"_ns, &isTV);
  if (NS_SUCCEEDED(rv) && isTV) {
    mCompatDevice.AssignLiteral("TV");
  } else {
    mCompatDevice.AssignLiteral("Mobile");
  }

  if (Preferences::GetBool(UA_PREF("use_device"), false)) {
    mDeviceModelId = mozilla::net::GetDeviceModelId();
  }
#endif  // ANDROID

#if defined(XP_IOS)
  // Freeze the iOS version to 18.0, use an underscore separator to avoid
  // detection as macOS.
  mCompatDevice.AssignLiteral("CPU iPhone OS 18_0 like Mac OS X");
#endif

  // Gather OS/CPU.
#if defined(XP_WIN)

#  if defined _M_X64 || defined _M_AMD64
  mOscpu.AssignLiteral(OSCPU_WIN64);
#  elif defined(_ARM64_)
  // Report ARM64 Windows 11+ as x86_64 and Windows 10 as x86. Windows 11+
  // supports x86_64 emulation, but Windows 10 only supports x86 emulation.
  if (IsWin11OrLater()) {
    mOscpu.AssignLiteral(OSCPU_WIN64);
  } else {
    mOscpu.AssignLiteral(OSCPU_WINDOWS);
  }
#  else
  BOOL isWow64 = FALSE;
  if (!IsWow64Process(GetCurrentProcess(), &isWow64)) {
    isWow64 = FALSE;
  }
  if (isWow64) {
    mOscpu.AssignLiteral(OSCPU_WIN64);
  } else {
    mOscpu.AssignLiteral(OSCPU_WINDOWS);
  }
#  endif

#elif defined(XP_MACOSX)
  mOscpu.AssignLiteral("Intel Mac OS X 10.15");
#elif defined(ANDROID)
  mOscpu.AssignLiteral("Linux armv81");
#elif defined(XP_IOS)
  mOscpu.AssignLiteral("iPhone");
#else
  mOscpu.AssignLiteral("Linux x86_64");
#endif

  mUserAgentIsDirty = true;
}

#ifdef XP_MACOSX
void nsHttpHandler::InitMSAuthorities() {
  if (!StaticPrefs::network_http_microsoft_entra_sso_enabled()) {
    return;
  }

  nsAutoCString authorityList;

  if (NS_FAILED(Preferences::GetCString("network.microsoft-sso-authority-list",
                                        authorityList))) {
    return;
  }
  mMSAuthorities.Clear();

  // Normalize the MS authority list
  nsCCharSeparatedTokenizer tokenizer(authorityList, ',');
  while (tokenizer.hasMoreTokens()) {
    const nsDependentCSubstring& token = tokenizer.nextToken();
    mMSAuthorities.Insert(token);
  }
}
#endif

uint32_t nsHttpHandler::MaxSocketCount() {
  PR_CallOnce(&nsSocketTransportService::gMaxCountInitOnce,
              nsSocketTransportService::DiscoverMaxCount);
  // Don't use the full max count because sockets can be held in
  // the persistent connection pool for a long time and that could
  // starve other users.

  uint32_t maxCount = nsSocketTransportService::gMaxCount;
  if (maxCount <= 8) {
    maxCount = 1;
  } else {
    maxCount -= 8;
  }

  return maxCount;
}

// static
void nsHttpHandler::PrefsChanged(const char* pref, void* self) {
  static_cast<nsHttpHandler*>(self)->PrefsChanged(pref);
}

void nsHttpHandler::PrefsChanged(const char* pref) {
  nsresult rv = NS_OK;
  int32_t val;

  LOG(("nsHttpHandler::PrefsChanged [pref=%s]\n", pref));

#define PREF_CHANGED(p) ((pref == nullptr) || !strcmp(pref, p))
#define MULTI_PREF_CHANGED(p) \
  ((pref == nullptr) || !strncmp(pref, p, sizeof(p) - 1))

  // If a security pref changed, lets clear our connection pool reuse
  if (MULTI_PREF_CHANGED(SECURITY_PREFIX)) {
    LOG(("nsHttpHandler::PrefsChanged Security Pref Changed %s\n", pref));
    if (mConnMgr) {
      rv = mConnMgr->DoShiftReloadConnectionCleanup();
      if (NS_FAILED(rv)) {
        LOG(
            ("nsHttpHandler::PrefsChanged "
             "DoShiftReloadConnectionCleanup failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
      rv = mConnMgr->PruneDeadConnections();
      if (NS_FAILED(rv)) {
        LOG(
            ("nsHttpHandler::PrefsChanged "
             "PruneDeadConnections failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
    }
  }

  //
  // UA components
  //

  bool cVar = false;

  if (PREF_CHANGED(UA_PREF("compatMode.firefox"))) {
    rv = Preferences::GetBool(UA_PREF("compatMode.firefox"), &cVar);
    mCompatFirefoxEnabled = (NS_SUCCEEDED(rv) && cVar);
    mUserAgentIsDirty = true;
  }

  // general.useragent.override
  if (PREF_CHANGED(UA_PREF("override"))) {
    Preferences::GetCString(UA_PREF("override"), mUserAgentOverride);
    mUserAgentIsDirty = true;
  }

#ifdef ANDROID
  // general.useragent.use_device
  if (PREF_CHANGED(UA_PREF("use_device"))) {
    if (Preferences::GetBool(UA_PREF("use_device"), false)) {
      mDeviceModelId = mozilla::net::GetDeviceModelId();
    } else {
      mDeviceModelId.Truncate();
    }
    mUserAgentIsDirty = true;
  }
#endif

  //
  // HTTP options
  //

  if (PREF_CHANGED(HTTP_PREF("keep-alive.timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("keep-alive.timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      mIdleTimeout = PR_SecondsToInterval(std::clamp(val, 1, 0xffff));
    }
  }

  if (PREF_CHANGED(HTTP_PREF("request.max-attempts"))) {
    rv = Preferences::GetInt(HTTP_PREF("request.max-attempts"), &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxRequestAttempts = (uint16_t)std::clamp(val, 1, 0xffff);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("request.max-start-delay"))) {
    rv = Preferences::GetInt(HTTP_PREF("request.max-start-delay"), &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxRequestDelay = (uint16_t)std::clamp(val, 0, 0xffff);
      if (mConnMgr) {
        rv = mConnMgr->UpdateParam(nsHttpConnectionMgr::MAX_REQUEST_DELAY,
                                   mMaxRequestDelay);
        if (NS_FAILED(rv)) {
          LOG(
              ("nsHttpHandler::PrefsChanged (request.max-start-delay)"
               "UpdateParam failed (%08x)\n",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("response.timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("response.timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      mResponseTimeout = PR_SecondsToInterval(std::clamp(val, 0, 0xffff));
    }
  }

  if (PREF_CHANGED(HTTP_PREF("network-changed.timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("network-changed.timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      mNetworkChangedTimeout = std::clamp(val, 1, 600) * 1000;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("max-connections"))) {
    rv = Preferences::GetInt(HTTP_PREF("max-connections"), &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxConnections =
          (uint16_t)std::clamp((uint32_t)val, (uint32_t)1, MaxSocketCount());

      if (mConnMgr) {
        rv = mConnMgr->UpdateParam(nsHttpConnectionMgr::MAX_CONNECTIONS,
                                   mMaxConnections);
        if (NS_FAILED(rv)) {
          LOG(
              ("nsHttpHandler::PrefsChanged (max-connections)"
               "UpdateParam failed (%08x)\n",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  if (PREF_CHANGED(
          HTTP_PREF("max-urgent-start-excessive-connections-per-host"))) {
    rv = Preferences::GetInt(
        HTTP_PREF("max-urgent-start-excessive-connections-per-host"), &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxUrgentExcessiveConns = (uint8_t)std::clamp(val, 1, 0xff);
      if (mConnMgr) {
        rv = mConnMgr->UpdateParam(nsHttpConnectionMgr::MAX_URGENT_START_Q,
                                   mMaxUrgentExcessiveConns);
        if (NS_FAILED(rv)) {
          LOG(
              ("nsHttpHandler::PrefsChanged "
               "(max-urgent-start-excessive-connections-per-host)"
               "UpdateParam failed (%08x)\n",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("max-persistent-connections-per-server"))) {
    rv = Preferences::GetInt(HTTP_PREF("max-persistent-connections-per-server"),
                             &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxPersistentConnectionsPerServer = (uint8_t)std::clamp(val, 1, 0xff);
      if (mConnMgr) {
        rv = mConnMgr->UpdateParam(
            nsHttpConnectionMgr::MAX_PERSISTENT_CONNECTIONS_PER_HOST,
            mMaxPersistentConnectionsPerServer);
        if (NS_FAILED(rv)) {
          LOG(
              ("nsHttpHandler::PrefsChanged "
               "(max-persistent-connections-per-server)"
               "UpdateParam failed (%08x)\n",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("max-persistent-connections-per-proxy"))) {
    rv = Preferences::GetInt(HTTP_PREF("max-persistent-connections-per-proxy"),
                             &val);
    if (NS_SUCCEEDED(rv)) {
      mMaxPersistentConnectionsPerProxy = (uint8_t)std::clamp(val, 1, 0xff);
      if (mConnMgr) {
        rv = mConnMgr->UpdateParam(
            nsHttpConnectionMgr::MAX_PERSISTENT_CONNECTIONS_PER_PROXY,
            mMaxPersistentConnectionsPerProxy);
        if (NS_FAILED(rv)) {
          LOG(
              ("nsHttpHandler::PrefsChanged "
               "(max-persistent-connections-per-proxy)"
               "UpdateParam failed (%08x)\n",
               static_cast<uint32_t>(rv)));
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("redirection-limit"))) {
    rv = Preferences::GetInt(HTTP_PREF("redirection-limit"), &val);
    if (NS_SUCCEEDED(rv)) mRedirectionLimit = (uint8_t)std::clamp(val, 0, 0xff);
  }

  if (PREF_CHANGED(HTTP_PREF("connection-retry-timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("connection-retry-timeout"), &val);
    if (NS_SUCCEEDED(rv)) mIdleSynTimeout = (uint16_t)std::clamp(val, 0, 3000);
  }

  if (PREF_CHANGED(HTTP_PREF("fast-fallback-to-IPv4"))) {
    rv = Preferences::GetBool(HTTP_PREF("fast-fallback-to-IPv4"), &cVar);
    if (NS_SUCCEEDED(rv)) mFastFallbackToIPv4 = cVar;
  }

  if (PREF_CHANGED(HTTP_PREF("fallback-connection-timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("fallback-connection-timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      mFallbackSynTimeout = (uint16_t)std::clamp(val, 0, 10 * 60);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("version"))) {
    nsAutoCString httpVersion;
    Preferences::GetCString(HTTP_PREF("version"), httpVersion);
    if (!httpVersion.IsVoid()) {
      if (httpVersion.EqualsLiteral("1.1")) {
        mHttpVersion = HttpVersion::v1_1;
      } else if (httpVersion.EqualsLiteral("0.9")) {
        mHttpVersion = HttpVersion::v0_9;
      } else {
        mHttpVersion = HttpVersion::v1_0;
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("proxy.version"))) {
    nsAutoCString httpVersion;
    Preferences::GetCString(HTTP_PREF("proxy.version"), httpVersion);
    if (!httpVersion.IsVoid()) {
      if (httpVersion.EqualsLiteral("1.1")) {
        mProxyHttpVersion = HttpVersion::v1_1;
      } else {
        mProxyHttpVersion = HttpVersion::v1_0;
      }
      // it does not make sense to issue a HTTP/0.9 request to a proxy server
    }
  }

  if (PREF_CHANGED(HTTP_PREF("proxy.respect-be-conservative"))) {
    rv =
        Preferences::GetBool(HTTP_PREF("proxy.respect-be-conservative"), &cVar);
    if (NS_SUCCEEDED(rv)) {
      mBeConservativeForProxy = cVar;
      if (mConnMgr) {
        (void)mConnMgr->UpdateParam(
            nsHttpConnectionMgr::PROXY_BE_CONSERVATIVE,
            static_cast<int32_t>(mBeConservativeForProxy));
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("qos"))) {
    rv = Preferences::GetInt(HTTP_PREF("qos"), &val);
    if (NS_SUCCEEDED(rv)) mQoSBits = (uint8_t)std::clamp(val, 0, 0xff);
  }

  if (PREF_CHANGED(HTTP_PREF("accept-encoding"))) {
    nsAutoCString acceptEncodings;
    rv = Preferences::GetCString(HTTP_PREF("accept-encoding"), acceptEncodings);
    if (NS_SUCCEEDED(rv)) {
      rv = SetAcceptEncodings(acceptEncodings.get(), false, false);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }

  if (PREF_CHANGED(HTTP_PREF("accept-encoding.secure")) ||
      PREF_CHANGED(HTTP_PREF("accept-encoding.dictionary"))) {
    nsAutoCString acceptEncodings;
    rv = Preferences::GetCString(HTTP_PREF("accept-encoding.secure"),
                                 acceptEncodings);
    if (NS_SUCCEEDED(rv)) {
      rv = SetAcceptEncodings(acceptEncodings.get(), true, false);

      // Since dictionary encodings are dependent on both accept-encoding.secure
      // and accept-encoding.dictionary, update both if either changes (which is
      // quite rare, so there's no real perf hit)
      nsAutoCString acceptDictionaryEncodings;
      nsresult rvDic = Preferences::GetCString(
          HTTP_PREF("accept-encoding.dictionary"), acceptDictionaryEncodings);
      if (NS_SUCCEEDED(rvDic) && !acceptDictionaryEncodings.IsEmpty()) {
        acceptEncodings.Append(", "_ns);
        acceptEncodings.Append(acceptDictionaryEncodings);
        rv = SetAcceptEncodings(acceptEncodings.get(), true, true);
      }
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }

  if (PREF_CHANGED(HTTP_PREF("default-socket-type"))) {
    nsAutoCString sval;
    rv = Preferences::GetCString(HTTP_PREF("default-socket-type"), sval);
    if (NS_SUCCEEDED(rv)) {
      if (sval.IsEmpty()) {
        mDefaultSocketType.SetIsVoid(true);
      } else {
        // verify that this socket type is actually valid
        nsCOMPtr<nsISocketProviderService> sps =
            nsSocketProviderService::GetOrCreate();
        if (sps) {
          nsCOMPtr<nsISocketProvider> sp;
          rv = sps->GetSocketProvider(sval.get(), getter_AddRefs(sp));
          if (NS_SUCCEEDED(rv)) {
            // OK, this looks like a valid socket provider.
            mDefaultSocketType.Assign(sval);
          }
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("prompt-temp-redirect"))) {
    rv = Preferences::GetBool(HTTP_PREF("prompt-temp-redirect"), &cVar);
    if (NS_SUCCEEDED(rv)) {
      mPromptTempRedirect = cVar;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("assoc-req.enforce"))) {
    cVar = false;
    rv = Preferences::GetBool(HTTP_PREF("assoc-req.enforce"), &cVar);
    if (NS_SUCCEEDED(rv)) mEnforceAssocReq = cVar;
  }

  // enable Persistent caching for HTTPS - bug#205921
  if (PREF_CHANGED(BROWSER_PREF("disk_cache_ssl"))) {
    cVar = false;
    rv = Preferences::GetBool(BROWSER_PREF("disk_cache_ssl"), &cVar);
    if (NS_SUCCEEDED(rv)) mEnablePersistentHttpsCaching = cVar;
  }

  if (PREF_CHANGED(HTTP_PREF("http2.timeout"))) {
    mSpdyTimeout = PR_SecondsToInterval(
        std::clamp(StaticPrefs::network_http_http2_timeout(), 1, 0xffff));
  }

  if (PREF_CHANGED(HTTP_PREF("http2.chunk-size"))) {
    // keep this within http/2 ranges of 1 to 2^24-1
    mSpdySendingChunkSize = (uint32_t)std::clamp(
        StaticPrefs::network_http_http2_chunk_size(), 1, 0xffffff);
  }

  // The amount of idle seconds on a http2 connection before initiating a
  // server ping. 0 will disable.
  if (PREF_CHANGED(HTTP_PREF("http2.ping-threshold"))) {
    mSpdyPingThreshold = PR_SecondsToInterval((uint16_t)std::clamp(
        StaticPrefs::network_http_http2_ping_threshold(), 0, 0x7fffffff));
  }

  // The amount of seconds to wait for a http2 ping response before
  // closing the session.
  if (PREF_CHANGED(HTTP_PREF("http2.ping-timeout"))) {
    mSpdyPingTimeout = PR_SecondsToInterval((uint16_t)std::clamp(
        StaticPrefs::network_http_http2_ping_timeout(), 0, 0x7fffffff));
  }

  if (PREF_CHANGED(HTTP_PREF("altsvc.enabled"))) {
    rv = Preferences::GetBool(HTTP_PREF("altsvc.enabled"), &cVar);
    if (NS_SUCCEEDED(rv)) mEnableAltSvc = cVar;
  }

  if (PREF_CHANGED(HTTP_PREF("http2.push-allowance"))) {
    mSpdyPushAllowance = static_cast<uint32_t>(
        std::clamp(StaticPrefs::network_http_http2_push_allowance(), 1024,
                   static_cast<int32_t>(ASpdySession::kInitialRwin)));
  }

  if (PREF_CHANGED(HTTP_PREF("http2.pull-allowance"))) {
    mSpdyPullAllowance = static_cast<uint32_t>(std::clamp(
        StaticPrefs::network_http_http2_pull_allowance(), 1024, 0x7fffffff));
  }

  if (PREF_CHANGED(HTTP_PREF("http2.default-concurrent"))) {
    mDefaultSpdyConcurrent = static_cast<uint32_t>(std::max<int32_t>(
        std::min<int32_t>(StaticPrefs::network_http_http2_default_concurrent(),
                          9999),
        1));
  }

  // If http2.send-buffer-size is non-zero, the size to set the TCP
  //  sendbuffer to once the stream has surpassed this number of bytes uploaded
  if (PREF_CHANGED(HTTP_PREF("http2.send-buffer-size"))) {
    mSpdySendBufferSize = (uint32_t)std::clamp(
        StaticPrefs::network_http_http2_send_buffer_size(), 1500, 0x7fffffff);
  }

  // The maximum amount of time to wait for socket transport to be
  // established
  if (PREF_CHANGED(HTTP_PREF("connection-timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("connection-timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      // the pref is in seconds, but the variable is in milliseconds
      mConnectTimeout = std::clamp(val, 1, 0xffff) * PR_MSEC_PER_SEC;
    }
  }

  // The maximum amount of time to wait for a tls handshake to finish.
  if (PREF_CHANGED(HTTP_PREF("tls-handshake-timeout"))) {
    rv = Preferences::GetInt(HTTP_PREF("tls-handshake-timeout"), &val);
    if (NS_SUCCEEDED(rv)) {
      // the pref is in seconds, but the variable is in milliseconds
      mTLSHandshakeTimeout = std::clamp(val, 1, 0xffff) * PR_MSEC_PER_SEC;
    }
  }

  // The maximum number of current global half open sockets allowable
  // for starting a new speculative connection.
  if (PREF_CHANGED(HTTP_PREF("speculative-parallel-limit"))) {
    rv = Preferences::GetInt(HTTP_PREF("speculative-parallel-limit"), &val);
    if (NS_SUCCEEDED(rv)) {
      mParallelSpeculativeConnectLimit = (uint32_t)std::clamp(val, 0, 1024);
    }
  }

  // Whether or not to block requests for non head js/css items (e.g. media)
  // while those elements load.
  if (PREF_CHANGED(HTTP_PREF("rendering-critical-requests-prioritization"))) {
    rv = Preferences::GetBool(
        HTTP_PREF("rendering-critical-requests-prioritization"), &cVar);
    if (NS_SUCCEEDED(rv)) mCriticalRequestPrioritization = cVar;
  }

  // on transition of network.http.diagnostics to true print
  // a bunch of information to the console
  if (pref && PREF_CHANGED(HTTP_PREF("diagnostics"))) {
    rv = Preferences::GetBool(HTTP_PREF("diagnostics"), &cVar);
    if (NS_SUCCEEDED(rv) && cVar) {
      if (mConnMgr) mConnMgr->PrintDiagnostics();
    }
  }

  if (PREF_CHANGED(HTTP_PREF("throttle.enable"))) {
    rv = Preferences::GetBool(HTTP_PREF("throttle.enable"), &mThrottleEnabled);
    if (NS_SUCCEEDED(rv) && mConnMgr) {
      (void)mConnMgr->UpdateParam(nsHttpConnectionMgr::THROTTLING_ENABLED,
                                  static_cast<int32_t>(mThrottleEnabled));
    }
  }

  if (PREF_CHANGED(HTTP_PREF("throttle.suspend-for"))) {
    rv = Preferences::GetInt(HTTP_PREF("throttle.suspend-for"), &val);
    mThrottleSuspendFor = (uint32_t)std::clamp(val, 0, 120000);
    if (NS_SUCCEEDED(rv) && mConnMgr) {
      (void)mConnMgr->UpdateParam(nsHttpConnectionMgr::THROTTLING_SUSPEND_FOR,
                                  mThrottleSuspendFor);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("throttle.resume-for"))) {
    rv = Preferences::GetInt(HTTP_PREF("throttle.resume-for"), &val);
    mThrottleResumeFor = (uint32_t)std::clamp(val, 0, 120000);
    if (NS_SUCCEEDED(rv) && mConnMgr) {
      (void)mConnMgr->UpdateParam(nsHttpConnectionMgr::THROTTLING_RESUME_FOR,
                                  mThrottleResumeFor);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("throttle.hold-time-ms"))) {
    rv = Preferences::GetInt(HTTP_PREF("throttle.hold-time-ms"), &val);
    mThrottleHoldTime = (uint32_t)std::clamp(val, 0, 120000);
    if (NS_SUCCEEDED(rv) && mConnMgr) {
      (void)mConnMgr->UpdateParam(nsHttpConnectionMgr::THROTTLING_HOLD_TIME,
                                  mThrottleHoldTime);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("throttle.max-time-ms"))) {
    rv = Preferences::GetInt(HTTP_PREF("throttle.max-time-ms"), &val);
    mThrottleMaxTime = (uint32_t)std::clamp(val, 0, 120000);
    if (NS_SUCCEEDED(rv) && mConnMgr) {
      (void)mConnMgr->UpdateParam(nsHttpConnectionMgr::THROTTLING_MAX_TIME,
                                  mThrottleMaxTime);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("send_window_size"))) {
    (void)Preferences::GetInt(HTTP_PREF("send_window_size"), &val);
    mSendWindowSize = val >= 0 ? val : 0;
  }

  if (PREF_CHANGED(HTTP_PREF("on_click_priority"))) {
    (void)Preferences::GetBool(HTTP_PREF("on_click_priority"),
                               &mUrgentStartEnabled);
  }

  if (PREF_CHANGED(HTTP_PREF("tailing.enabled"))) {
    (void)Preferences::GetBool(HTTP_PREF("tailing.enabled"),
                               &mTailBlockingEnabled);
  }
  if (PREF_CHANGED(HTTP_PREF("tailing.delay-quantum"))) {
    val = StaticPrefs::network_http_tailing_delay_quantum();
    mTailDelayQuantum = (uint32_t)std::clamp(val, 0, 60000);
  }
  if (PREF_CHANGED(HTTP_PREF("tailing.delay-quantum-after-domcontentloaded"))) {
    val = StaticPrefs::
        network_http_tailing_delay_quantum_after_domcontentloaded();
    mTailDelayQuantumAfterDCL = (uint32_t)std::clamp(val, 0, 60000);
  }
  if (PREF_CHANGED(HTTP_PREF("tailing.delay-max"))) {
    val = StaticPrefs::network_http_tailing_delay_max();
    mTailDelayMax = (uint32_t)std::clamp(val, 0, 60000);
  }
  if (PREF_CHANGED(HTTP_PREF("tailing.total-max"))) {
    val = StaticPrefs::network_http_tailing_total_max();
    mTailTotalMax = (uint32_t)std::clamp(val, 0, 60000);
  }

  if (PREF_CHANGED(HTTP_PREF("focused_window_transaction_ratio"))) {
    float ratio = 0;
    rv = Preferences::GetFloat(HTTP_PREF("focused_window_transaction_ratio"),
                               &ratio);
    if (NS_SUCCEEDED(rv)) {
      if (ratio > 0 && ratio < 1) {
        auto lockedRatio = mFocusedWindowTransactionRatio.Lock();
        *lockedRatio = ratio;
      } else {
        NS_WARNING("Wrong value for focused_window_transaction_ratio");
      }
    }
  }

  //
  // INTL options
  //

  if (PREF_CHANGED(INTL_ACCEPT_LANGUAGES)) {
    // We don't want to set the new accept languages here since
    // this pref is a complex type and it may be racy with flushing
    // string resources.
    mAcceptLanguagesIsDirty = true;
  }

  //
  // Tracking options
  //

  // Hint option
  if (PREF_CHANGED(SAFE_HINT_HEADER_VALUE)) {
    cVar = false;
    rv = Preferences::GetBool(SAFE_HINT_HEADER_VALUE, &cVar);
    if (NS_SUCCEEDED(rv)) {
      mSafeHintEnabled = cVar;
    }
  }

  // toggle to true anytime a token bucket related pref is changed.. that
  // includes telemetry and allow-experiments because of the abtest profile
  bool requestTokenBucketUpdated = false;

  // "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256" is the required h2 interop
  // suite.

  if (PREF_CHANGED(H2MANDATORY_SUITE)) {
    cVar = false;
    rv = Preferences::GetBool(H2MANDATORY_SUITE, &cVar);
    if (NS_SUCCEEDED(rv)) {
      mH2MandatorySuiteEnabled = cVar;
    }
  }

  // network.http.debug-observations
  if (PREF_CHANGED("network.http.debug-observations")) {
    cVar = false;
    rv = Preferences::GetBool("network.http.debug-observations", &cVar);
    if (NS_SUCCEEDED(rv)) {
      mDebugObservations = cVar;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("pacing.requests.enabled"))) {
    rv = Preferences::GetBool(HTTP_PREF("pacing.requests.enabled"), &cVar);
    if (NS_SUCCEEDED(rv)) {
      mRequestTokenBucketEnabled = cVar;
      requestTokenBucketUpdated = true;
    }
  }
  if (PREF_CHANGED(HTTP_PREF("pacing.requests.min-parallelism"))) {
    rv =
        Preferences::GetInt(HTTP_PREF("pacing.requests.min-parallelism"), &val);
    if (NS_SUCCEEDED(rv)) {
      mRequestTokenBucketMinParallelism =
          static_cast<uint16_t>(std::clamp(val, 1, 1024));
      requestTokenBucketUpdated = true;
    }
  }
  if (PREF_CHANGED(HTTP_PREF("pacing.requests.hz"))) {
    rv = Preferences::GetInt(HTTP_PREF("pacing.requests.hz"), &val);
    if (NS_SUCCEEDED(rv)) {
      mRequestTokenBucketHz = static_cast<uint32_t>(std::clamp(val, 1, 10000));
      requestTokenBucketUpdated = true;
    }
  }
  if (PREF_CHANGED(HTTP_PREF("pacing.requests.burst"))) {
    rv = Preferences::GetInt(HTTP_PREF("pacing.requests.burst"), &val);
    if (NS_SUCCEEDED(rv)) {
      mRequestTokenBucketBurst = val ? val : 1;
      requestTokenBucketUpdated = true;
    }
  }
  if (requestTokenBucketUpdated) {
    MakeNewRequestTokenBucket();
  }

  // Keepalive values for initial and idle connections.
  if (PREF_CHANGED(HTTP_PREF("tcp_keepalive.short_lived_connections"))) {
    rv = Preferences::GetBool(
        HTTP_PREF("tcp_keepalive.short_lived_connections"), &cVar);
    if (NS_SUCCEEDED(rv) && cVar != mTCPKeepaliveShortLivedEnabled) {
      mTCPKeepaliveShortLivedEnabled = cVar;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("tcp_keepalive.short_lived_time"))) {
    rv = Preferences::GetInt(HTTP_PREF("tcp_keepalive.short_lived_time"), &val);
    if (NS_SUCCEEDED(rv) && val > 0) {
      mTCPKeepaliveShortLivedTimeS = std::clamp(val, 1, 300);  // Max 5 mins.
    }
  }

  if (PREF_CHANGED(HTTP_PREF("tcp_keepalive.short_lived_idle_time"))) {
    rv = Preferences::GetInt(HTTP_PREF("tcp_keepalive.short_lived_idle_time"),
                             &val);
    if (NS_SUCCEEDED(rv) && val > 0) {
      mTCPKeepaliveShortLivedIdleTimeS = std::clamp(val, 1, kMaxTCPKeepIdle);
    }
  }

  // Keepalive values for Long-lived Connections.
  if (PREF_CHANGED(HTTP_PREF("tcp_keepalive.long_lived_connections"))) {
    rv = Preferences::GetBool(HTTP_PREF("tcp_keepalive.long_lived_connections"),
                              &cVar);
    if (NS_SUCCEEDED(rv) && cVar != mTCPKeepaliveLongLivedEnabled) {
      mTCPKeepaliveLongLivedEnabled = cVar;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("tcp_keepalive.long_lived_idle_time"))) {
    rv = Preferences::GetInt(HTTP_PREF("tcp_keepalive.long_lived_idle_time"),
                             &val);
    if (NS_SUCCEEDED(rv) && val > 0) {
      mTCPKeepaliveLongLivedIdleTimeS = std::clamp(val, 1, kMaxTCPKeepIdle);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("enforce-framing.http1")) ||
      PREF_CHANGED(HTTP_PREF("enforce-framing.soft")) ||
      PREF_CHANGED(HTTP_PREF("enforce-framing.strict_chunked_encoding"))) {
    rv = Preferences::GetBool(HTTP_PREF("enforce-framing.http1"), &cVar);
    if (NS_SUCCEEDED(rv) && cVar) {
      mEnforceH1Framing = FRAMECHECK_STRICT;
    } else {
      rv = Preferences::GetBool(
          HTTP_PREF("enforce-framing.strict_chunked_encoding"), &cVar);
      if (NS_SUCCEEDED(rv) && cVar) {
        mEnforceH1Framing = FRAMECHECK_STRICT_CHUNKED;
      } else {
        rv = Preferences::GetBool(HTTP_PREF("enforce-framing.soft"), &cVar);
        if (NS_SUCCEEDED(rv) && cVar) {
          mEnforceH1Framing = FRAMECHECK_BARELY;
        } else {
          mEnforceH1Framing = FRAMECHECK_LAX;
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("http2.default-hpack-buffer"))) {
    mDefaultHpackBuffer =
        StaticPrefs::network_http_http2_default_hpack_buffer();
  }

  if (PREF_CHANGED(HTTP_PREF("http3.default-qpack-table-size"))) {
    rv = Preferences::GetInt(HTTP_PREF("http3.default-qpack-table-size"), &val);
    if (NS_SUCCEEDED(rv)) {
      mQpackTableSize = val;
    }
  }

  if (PREF_CHANGED(HTTP_PREF("http3.default-max-stream-blocked"))) {
    rv = Preferences::GetInt(HTTP_PREF("http3.default-max-stream-blocked"),
                             &val);
    if (NS_SUCCEEDED(rv)) {
      mHttp3MaxBlockedStreams = std::clamp(val, 0, 0xffff);
    }
  }

  const bool imageAcceptPrefChanged =
      PREF_CHANGED("image.http.accept") || PREF_CHANGED("image.jxl.enabled");

  if (imageAcceptPrefChanged) {
    nsAutoCString userSetImageAcceptHeader;

    if (Preferences::HasUserValue("image.http.accept")) {
      rv = Preferences::GetCString("image.http.accept",
                                   userSetImageAcceptHeader);
      if (NS_FAILED(rv)) {
        userSetImageAcceptHeader.Truncate();
      }
    }

    if (userSetImageAcceptHeader.IsEmpty()) {
      mImageAcceptHeader.Assign(ImageAcceptHeader());
    } else {
      mImageAcceptHeader.Assign(userSetImageAcceptHeader);
    }
  }

  if (PREF_CHANGED("network.http.accept") || imageAcceptPrefChanged) {
    nsAutoCString userSetDocumentAcceptHeader;

    if (Preferences::HasUserValue("network.http.accept")) {
      rv = Preferences::GetCString("network.http.accept",
                                   userSetDocumentAcceptHeader);
      if (NS_FAILED(rv)) {
        userSetDocumentAcceptHeader.Truncate();
      }
    }

    if (userSetDocumentAcceptHeader.IsEmpty()) {
      mDocumentAcceptHeader.Assign(DocumentAcceptHeader());
    } else {
      mDocumentAcceptHeader.Assign(userSetDocumentAcceptHeader);
    }
  }

  if (PREF_CHANGED(HTTP_PREF("http3.alt-svc-mapping-for-testing"))) {
    nsAutoCString altSvcMappings;
    rv = Preferences::GetCString(HTTP_PREF("http3.alt-svc-mapping-for-testing"),
                                 altSvcMappings);
    if (NS_SUCCEEDED(rv)) {
      auto map = mAltSvcMappingTemptativeMap.Lock();
      if (altSvcMappings.IsEmpty()) {
        map->Clear();
      } else {
        for (const nsACString& tokenSubstring :
             nsCCharSeparatedTokenizer(altSvcMappings, ',').ToRange()) {
          nsAutoCString token{tokenSubstring};
          int32_t index = token.Find(";");
          if (index != kNotFound) {
            map->InsertOrUpdate(
                Substring(token, 0, index),
                std::make_unique<nsCString>(Substring(token, index + 1)));
          }
        }
      }
    }
  }

  if (PREF_CHANGED(HTTP_PREF("http3.enable_qlog"))) {
    // Initialize the directory.
    nsCOMPtr<nsIFile> qlogDir;
    if (Preferences::GetBool(HTTP_PREF("http3.enable_qlog")) &&
        !mHttp3QlogDir.IsEmpty() &&
        NS_SUCCEEDED(
            NS_NewNativeLocalFile(mHttp3QlogDir, getter_AddRefs(qlogDir)))) {
      // Here we do main thread IO, but since this only happens
      // when enabling a developer feature it's not a problem for users.
      rv = qlogDir->Create(nsIFile::DIRECTORY_TYPE, 0755);
      if (NS_FAILED(rv) && rv != NS_ERROR_FILE_ALREADY_EXISTS) {
        NS_WARNING("Creating qlog dir failed");
      }
    }
  }

#ifdef XP_MACOSX
  if (XRE_IsParentProcess()) {
    if (PREF_CHANGED(HTTP_PREF("microsoft-entra-sso.enabled"))) {
      rv =
          Preferences::GetBool(HTTP_PREF("microsoft-entra-sso.enabled"), &cVar);
      if (NS_SUCCEEDED(rv) && cVar) {
        InitMSAuthorities();
      }
    }
  }
#endif

  // Enable HTTP response timeout if TCP Keepalives are disabled.
  mResponseTimeoutEnabled =
      !mTCPKeepaliveShortLivedEnabled && !mTCPKeepaliveLongLivedEnabled;

#undef PREF_CHANGED
#undef MULTI_PREF_CHANGED
}

/**
 *  Allocates a C string into that contains a ISO 639 language list
 *  notated with HTTP "q" values for output with a HTTP Accept-Language
 *  header. Previous q values will be stripped because the order of
 *  the langs imply the q value. The q values are calculated by dividing
 *  1.0 amongst the number of languages present.
 *
 *  Ex: passing: "en, ja"
 *      returns: "en,ja;q=0.5"
 *
 *      passing: "en, ja, fr_CA"
 *      returns: "en,ja;q=0.7,fr_CA;q=0.3"
 */
static nsresult PrepareAcceptLanguages(const char* i_AcceptLanguages,
                                       nsACString& o_AcceptLanguages) {
  if (!i_AcceptLanguages) return NS_OK;

  const nsAutoCString ns_accept_languages(i_AcceptLanguages);
  return rust_prepare_accept_languages(&ns_accept_languages,
                                       &o_AcceptLanguages);
}

// Ensure that we've fetched the AcceptLanguages setting
/* static */
void nsHttpHandler::PresetAcceptLanguages() {
  if (!gHttpHandler) {
    RefPtr<nsHttpHandler> handler = nsHttpHandler::GetInstance();
    (void)handler.get();
  }
  [[maybe_unused]] nsresult rv = gHttpHandler->SetAcceptLanguages();
}

nsresult nsHttpHandler::SetAcceptLanguages() {
  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIThread> mainThread;
    nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Forward to the main thread synchronously.
    SyncRunnable::DispatchToThread(
        mainThread,
        NS_NewRunnableFunction("nsHttpHandler::SetAcceptLanguages", [&rv]() {
          rv = gHttpHandler->SetAcceptLanguages();
        }));
    return rv;
  }

  MOZ_ASSERT(NS_IsMainThread());

  mAcceptLanguagesIsDirty = false;

  nsAutoCString acceptLanguages;
  intl::LocaleService::GetInstance()->GetAcceptLanguages(acceptLanguages);

  nsAutoCString buf;
  nsresult rv = PrepareAcceptLanguages(acceptLanguages.get(), buf);
  if (NS_SUCCEEDED(rv)) {
    mAcceptLanguages.Assign(buf);
  }
  return rv;
}

nsresult nsHttpHandler::SetAcceptEncodings(const char* aAcceptEncodings,
                                           bool isSecure, bool isDictionary) {
  if (isDictionary) {
    mDictionaryAcceptEncodings = aAcceptEncodings;
  } else if (isSecure) {
    mHttpsAcceptEncodings = aAcceptEncodings;
  } else {
    // use legacy list if a secure override is not specified
    mHttpAcceptEncodings = aAcceptEncodings;
    if (mHttpsAcceptEncodings.IsEmpty()) {
      mHttpsAcceptEncodings = aAcceptEncodings;
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpHandler::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsHttpHandler, nsIHttpProtocolHandler,
                  nsIProxiedProtocolHandler, nsIProtocolHandler, nsIObserver,
                  nsISupportsWeakReference, nsISpeculativeConnect)

//-----------------------------------------------------------------------------
// nsHttpHandler::nsIProtocolHandler
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpHandler::GetScheme(nsACString& aScheme) {
  aScheme.AssignLiteral("http");
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                          nsIChannel** result) {
  LOG(("nsHttpHandler::NewChannel\n"));

  NS_ENSURE_ARG_POINTER(uri);
  NS_ENSURE_ARG_POINTER(result);

  // Verify that we have been given a valid scheme
  if (!net::SchemeIsHttpOrHttps(uri)) {
    NS_WARNING("Invalid URI scheme");
    return NS_ERROR_UNEXPECTED;
  }

  return NewProxiedChannel(uri, nullptr, 0, nullptr, aLoadInfo, result);
}

NS_IMETHODIMP
nsHttpHandler::AllowPort(int32_t port, const char* scheme, bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpHandler::nsIProxiedProtocolHandler
//-----------------------------------------------------------------------------

nsresult nsHttpHandler::SetupChannelInternal(
    HttpBaseChannel* aChannel, nsIURI* uri, nsIProxyInfo* givenProxyInfo,
    uint32_t proxyResolveFlags, nsIURI* proxyURI, nsILoadInfo* aLoadInfo,
    nsIChannel** result) {
  RefPtr<HttpBaseChannel> httpChannel = aChannel;

  nsCOMPtr<nsProxyInfo> proxyInfo;
  if (givenProxyInfo) {
    proxyInfo = do_QueryInterface(givenProxyInfo);
    NS_ENSURE_ARG(proxyInfo);
  }

  uint32_t caps = mCapabilities;

  uint64_t channelId = NewChannelId();
  nsresult rv = httpChannel->Init(uri, caps, proxyInfo, proxyResolveFlags,
                                  proxyURI, channelId, aLoadInfo);
  if (NS_FAILED(rv)) return rv;

  httpChannel.forget(result);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::NewProxiedChannel(nsIURI* uri, nsIProxyInfo* givenProxyInfo,
                                 uint32_t proxyResolveFlags, nsIURI* proxyURI,
                                 nsILoadInfo* aLoadInfo, nsIChannel** result) {
  HttpBaseChannel* httpChannel;

  LOG(("nsHttpHandler::NewProxiedChannel [proxyInfo=%p]\n", givenProxyInfo));

  if (IsNeckoChild()) {
    httpChannel = new HttpChannelChild();
  } else {
    // HACK: make sure PSM gets initialized on the main thread.
    net_EnsurePSMInit();
    httpChannel = new nsHttpChannel();
  }

  return SetupChannelInternal(httpChannel, uri, givenProxyInfo,
                              proxyResolveFlags, proxyURI, aLoadInfo, result);
}

nsresult nsHttpHandler::CreateTRRServiceChannel(
    nsIURI* uri, nsIProxyInfo* givenProxyInfo, uint32_t proxyResolveFlags,
    nsIURI* proxyURI, nsILoadInfo* aLoadInfo, nsIChannel** result) {
  HttpBaseChannel* httpChannel = new TRRServiceChannel();

  LOG(("nsHttpHandler::CreateTRRServiceChannel [proxyInfo=%p]\n",
       givenProxyInfo));

  return SetupChannelInternal(httpChannel, uri, givenProxyInfo,
                              proxyResolveFlags, proxyURI, aLoadInfo, result);
}

//-----------------------------------------------------------------------------
// nsHttpHandler::nsIHttpProtocolHandler
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpHandler::GetUserAgent(nsACString& value) {
  value = UserAgent(false);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetRfpUserAgent(nsACString& value) {
  value = UserAgent(true);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetDocumentAcceptHeader(nsACString& value) {
  value = mDocumentAcceptHeader;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetAppName(nsACString& value) {
  value = mLegacyAppName;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetAppVersion(nsACString& value) {
  value = mLegacyAppVersion;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetPlatform(nsACString& value) {
  value = mPlatform;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetOscpu(nsACString& value) {
  value = mOscpu;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetMisc(nsACString& value) {
  value = mMisc;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::GetAltSvcCacheKeys(nsTArray<nsCString>& value) {
  return mAltSvcCache->GetAltSvcCacheKeys(value);
}

NS_IMETHODIMP
nsHttpHandler::GetAuthCacheKeys(nsTArray<nsCString>& aValues) {
  mAuthCache->CollectKeys(aValues);
  mPrivateAuthCache->CollectKeys(aValues);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpHandler::nsIObserver
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpHandler::Observe(nsISupports* subject, const char* topic,
                       const char16_t* data) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("nsHttpHandler::Observe [topic=\"%s\"]\n", topic));

  nsresult rv;
  if (!strcmp(topic, "profile-change-net-teardown") ||
      !strcmp(topic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    mHandlerActive = false;

    // clear cache of all authentication credentials.
    mAuthCache->ClearAll();
    mPrivateAuthCache->ClearAll();
    if (mWifiTickler) mWifiTickler->Cancel();

    // Inform nsIOService that network is tearing down.
    gIOService->SetHttpHandlerAlreadyShutingDown();

    ShutdownConnectionManager();

    // need to reset the session start time since cache validation may
    // depend on this value.
    mSessionStartTime = NowInSeconds();

    if (XRE_IsParentProcess()) {
      if (!StaticPrefs::privacy_donottrackheader_enabled()) {
        glean::http::dnt_usage.AccumulateSingleSample(2);
      } else {
        glean::http::dnt_usage.AccumulateSingleSample(1);
      }
    }

    mActivityDistributor = nullptr;
  } else if (!strcmp(topic, "net:clear-active-logins")) {
    mAuthCache->ClearAll();
    mPrivateAuthCache->ClearAll();
  } else if (!strcmp(topic, "net:cancel-all-connections")) {
    if (mConnMgr) {
      mConnMgr->AbortAndCloseAllConnections(0, nullptr);
    }
  } else if (!strcmp(topic, "net:prune-dead-connections")) {
    if (mConnMgr) {
      rv = mConnMgr->PruneDeadConnections();
      if (NS_FAILED(rv)) {
        LOG(("    PruneDeadConnections failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
    }
  } else if (!strcmp(topic, "net:prune-all-connections")) {
    if (mConnMgr) {
      rv = mConnMgr->DoShiftReloadConnectionCleanup();
      if (NS_FAILED(rv)) {
        LOG(("    DoShiftReloadConnectionCleanup failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
      rv = mConnMgr->PruneDeadConnections();
      if (NS_FAILED(rv)) {
        LOG(("    PruneDeadConnections failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
    }
#if 0
    } else if (!strcmp(topic, "net:failed-to-process-uri-content")) {
         // nop right now - we used to cancel h1 pipelines based on this,
         // but those are no longer implemented
         nsCOMPtr<nsIURI> uri = do_QueryInterface(subject);
#endif
  } else if (!strcmp(topic, "last-pb-context-exited")) {
    mPrivateAuthCache->ClearAll();
    nsCORSListenerProxy::ClearPrivateBrowsingCache();
  } else if (!strcmp(topic, "browser:purge-session-history")) {
    if (mConnMgr) {
      (void)mConnMgr->ClearConnectionHistory();
    }
    if (mAltSvcCache) {
      mAltSvcCache->ClearAltServiceMappings();
    }
  } else if (!strcmp(topic, NS_NETWORK_LINK_TOPIC)) {
    if (mConnMgr) {
      rv = mConnMgr->PruneDeadConnections();
      if (NS_FAILED(rv)) {
        LOG(("    PruneDeadConnections failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
      rv = mConnMgr->VerifyTraffic();
      if (NS_FAILED(rv)) {
        LOG(("    VerifyTraffic failed (%08x)\n", static_cast<uint32_t>(rv)));
      }
      // We exclude HTTP/3 for an origin when any error occurs during
      // connection establishment. A common error is caused by UDP being blocked
      // by the network. When the network changes, it’s likely that UDP is no
      // longer blocked, so we should reset the exclusion list to give HTTP/3
      // another chance.
      MutexAutoLock lock(mHttpExclusionLock);
      mExcludedHttp3Origins.Clear();
    }
  } else if (!strcmp(topic, "application-background")) {
    // going to the background on android means we should close
    // down idle connections for power conservation
    if (mConnMgr) {
      rv = StaticPrefs::network_http_close_connections_on_background()
               ? mConnMgr->DoShiftReloadConnectionCleanup()
               : mConnMgr->CloseAllIdleConnections();
      if (NS_FAILED(rv)) {
        LOG(("    background connection cleanup failed (%08x)\n",
             static_cast<uint32_t>(rv)));
      }
    }
  } else if (!strcmp(topic, "net:current-browser-id")) {
    if (XRE_IsParentProcess()) {
      nsCOMPtr<nsISupportsPRUint64> wrapper = do_QueryInterface(subject);
      MOZ_RELEASE_ASSERT(wrapper);

      uint64_t id = 0;
      wrapper->GetData(&id);
      MOZ_ASSERT(id);

      static uint64_t sCurrentBrowserId = 0;
      if (sCurrentBrowserId != id) {
        sCurrentBrowserId = id;
        if (mConnMgr) {
          mConnMgr->UpdateCurrentBrowserId(sCurrentBrowserId);
        }
      }
    }
  } else if (!strcmp(topic, "intl:app-locales-changed")) {
    // If the locale changed, there's a chance the accept language did too
    mAcceptLanguagesIsDirty = true;
  } else if (!strcmp(topic, "network:reset-http3-excluded-list")) {
    MutexAutoLock lock(mHttpExclusionLock);
    mExcludedHttp3Origins.Clear();
  } else if (!strcmp(topic, "idle-daily")) {
    // Submit the local-network-access ping once per day
    if (XRE_IsParentProcess()) {
      glean_pings::LocalNetworkAccess.Submit();
    }
  }
  return NS_OK;
}

// nsISpeculativeConnect

nsresult nsHttpHandler::SpeculativeConnectInternal(
    nsIURI* aURI, nsIPrincipal* aPrincipal,
    Maybe<OriginAttributes>&& aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool anonymous) {
  if (IsNeckoChild()) {
    nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(aCallbacks);

    gNeckoChild->SendSpeculativeConnect(
        nullptr, IPC::SerializedLoadContext(loadContext), aURI, aPrincipal,
        std::move(aOriginAttributes), anonymous);
    return NS_OK;
  }

  if (!mHandlerActive) return NS_OK;

  MOZ_ASSERT(NS_IsMainThread());

  nsISiteSecurityService* sss = gHttpHandler->GetSSService();
  bool isStsHost = false;
  if (!sss) return NS_OK;

  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(aCallbacks);
  OriginAttributes originAttributes;
  // If the principal is given, we use the originAttributes from this
  // principal. Otherwise, we use the originAttributes from the loadContext.
  if (aOriginAttributes) {
    originAttributes = std::move(aOriginAttributes.ref());
  } else if (aPrincipal) {
    originAttributes = aPrincipal->OriginAttributesRef();
  } else if (loadContext) {
    loadContext->GetOriginAttributes(originAttributes);
  }

  nsCOMPtr<nsIURI> clone;
  if (NS_SUCCEEDED(sss->IsSecureURI(aURI, originAttributes, &isStsHost)) &&
      isStsHost) {
    if (NS_SUCCEEDED(NS_GetSecureUpgradedURI(aURI, getter_AddRefs(clone)))) {
      aURI = clone.get();
      // (NOTE: We better make sure |clone| stays alive until the end
      // of the function now, since our aURI arg now points to it!)
    }
  }

  if (!aOriginAttributes) {
    // We must update the originAttributes with the network state first party
    // domain **after** we upgrade aURI to https.
    // Otherwise the speculative connection will be keyed by a http URL
    // and end up not being used.
    StoragePrincipalHelper::UpdateOriginAttributesForNetworkState(
        aURI, originAttributes);
  }

  nsAutoCString scheme;
  nsresult rv = aURI->GetScheme(scheme);
  if (NS_FAILED(rv)) return rv;

  // If this is HTTPS, make sure PSM is initialized as the channel
  // creation path may have been bypassed
  if (scheme.EqualsLiteral("https")) {
    if (!IsNeckoChild()) {
      // make sure PSM gets initialized on the main thread.
      net_EnsurePSMInit();
    }
  }
  // Ensure that this is HTTP or HTTPS, otherwise we don't do preconnect here
  else if (!scheme.EqualsLiteral("http")) {
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoCString host;
  rv = aURI->GetAsciiHost(host);
  if (NS_FAILED(rv)) return rv;

  int32_t port = -1;
  rv = aURI->GetPort(&port);
  if (NS_FAILED(rv)) return rv;

  nsAutoCString username;
  aURI->GetUsername(username);

  RefPtr<AltSvcMapping> mapping;
  if (username.IsEmpty()) {
    mapping = gHttpHandler->GetAltServiceMapping(
        scheme, host, port, originAttributes.IsPrivateBrowsing(),
        originAttributes, true, true);
  }

  RefPtr<nsHttpConnectionInfo> ci;
  if (mapping) {
    mapping->GetConnectionInfo(getter_AddRefs(ci), nullptr, originAttributes);
  } else {
    ci = new nsHttpConnectionInfo(host, port, ""_ns, username, nullptr,
                                  originAttributes, aURI->SchemeIs("https"));
  }
  ci = ci->Mutate()
           .SetAnonymous(anonymous)
           .SetPrivate(originAttributes.IsPrivateBrowsing())
           .Finalize();

  if (mDebugObservations) {
    // this is basically used for test coverage of an otherwise 'hintable'
    // feature

    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    if (obsService) {
      // This is used to test if the speculative connection has the right
      // connection info.
      nsPrintfCString debugHashKey("%s", ci->HashKey().get());
      obsService->NotifyObservers(nullptr, "speculative-connect-request",
                                  NS_ConvertUTF8toUTF16(debugHashKey).get());
      for (auto* cp :
           dom::ContentParent::AllProcesses(dom::ContentParent::eLive)) {
        PNeckoParent* neckoParent =
            SingleManagedOrNull(cp->ManagedPNeckoParent());
        if (!neckoParent) {
          continue;
        }
        (void)neckoParent->SendSpeculativeConnectRequest();
      }
    }
  }

  bool fetchHTTPSRR = EchConfigEnabled();
  if (StaticPrefs::network_http_happy_eyeballs_enabled()) {
    ci = ci->Mutate().SetHappyEyeballsEnabled(true).Finalize();
    // When HE is enabled, HTTPS RR lookups are handled by
    // HappyEyeballsConnectionAttempt.
    fetchHTTPSRR = false;
  }

  LOG(("MaybeSpeculativeConnectWithHTTPSRR for ci=%s", ci->HashKey().get()));
  // When ech is enabled, always do speculative connect with HTTPS RR.
  return MaybeSpeculativeConnectWithHTTPSRR(ci, aCallbacks, 0, fetchHTTPSRR);
}

nsresult nsHttpHandler::SpeculativeConnect(nsHttpConnectionInfo* ci,
                                           nsIInterfaceRequestor* callbacks,
                                           uint32_t caps,
                                           SpeculativeTransaction* aTrans) {
  if (mDebugObservations) {
    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    if (obsService) {
      nsPrintfCString debugHashKey("%s", ci->HashKey().get());
      obsService->NotifyObservers(nullptr, "speculative-connect-request",
                                  NS_ConvertUTF8toUTF16(debugHashKey).get());
    }
  }
  return mConnMgr->SpeculativeConnect(ci, callbacks, caps, aTrans);
}

NS_IMETHODIMP
nsHttpHandler::SpeculativeConnect(nsIURI* aURI, nsIPrincipal* aPrincipal,
                                  nsIInterfaceRequestor* aCallbacks,
                                  bool aAnonymous) {
  return SpeculativeConnectInternal(aURI, aPrincipal, Nothing(), aCallbacks,
                                    aAnonymous);
}

NS_IMETHODIMP nsHttpHandler::SpeculativeConnectWithOriginAttributes(
    nsIURI* aURI, JS::Handle<JS::Value> aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool aAnonymous, JSContext* aCx) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  SpeculativeConnectWithOriginAttributesNative(aURI, std::move(attrs),
                                               aCallbacks, aAnonymous);
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsHttpHandler::SpeculativeConnectWithOriginAttributesNative(
    nsIURI* aURI, OriginAttributes&& aOriginAttributes,
    nsIInterfaceRequestor* aCallbacks, bool aAnonymous) {
  Maybe<OriginAttributes> originAttributes;
  originAttributes.emplace(aOriginAttributes);
  (void)SpeculativeConnectInternal(aURI, nullptr, std::move(originAttributes),
                                   aCallbacks, aAnonymous);
}

void nsHttpHandler::TickleWifi(nsIInterfaceRequestor* cb) {
  if (!cb || !mWifiTickler) return;

  // If B2G requires a similar mechanism nsINetworkManager, currently only avail
  // on B2G, contains the necessary information on wifi and gateway

  nsCOMPtr<nsIDOMWindow> domWindow = do_GetInterface(cb);
  nsCOMPtr<nsPIDOMWindowOuter> piWindow = do_QueryInterface(domWindow);
  if (!piWindow) return;

  RefPtr<dom::Navigator> navigator = piWindow->GetNavigator();
  if (!navigator) return;

  RefPtr<dom::network::Connection> networkProperties =
      navigator->GetConnection(IgnoreErrors());
  if (!networkProperties) return;

  uint32_t gwAddress = networkProperties->GetDhcpGateway();
  bool isWifi = networkProperties->GetIsWifi();
  if (!gwAddress || !isWifi) return;

  mWifiTickler->SetIPV4Address(gwAddress);
  mWifiTickler->Tickle();
}

//-----------------------------------------------------------------------------
// nsHttpsHandler implementation
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsHttpsHandler, nsIHttpProtocolHandler,
                  nsIProxiedProtocolHandler, nsIProtocolHandler,
                  nsISupportsWeakReference, nsISpeculativeConnect)

nsresult nsHttpsHandler::Init() {
  nsCOMPtr<nsIProtocolHandler> httpHandler(
      do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http"));
  MOZ_ASSERT(httpHandler.get() != nullptr);
  return NS_OK;
}

NS_IMETHODIMP
nsHttpsHandler::GetScheme(nsACString& aScheme) {
  aScheme.AssignLiteral("https");
  return NS_OK;
}

NS_IMETHODIMP
nsHttpsHandler::NewChannel(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                           nsIChannel** _retval) {
  MOZ_ASSERT(gHttpHandler);
  if (!gHttpHandler) return NS_ERROR_UNEXPECTED;
  return gHttpHandler->NewChannel(aURI, aLoadInfo, _retval);
}

NS_IMETHODIMP
nsHttpsHandler::AllowPort(int32_t aPort, const char* aScheme, bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
nsHttpHandler::EnsureHSTSDataReadyNative(
    RefPtr<mozilla::net::HSTSDataCallbackWrapper> aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), "http://example.com");
  NS_ENSURE_SUCCESS(rv, rv);

  bool shouldUpgrade = false;
  bool willCallback = false;
  OriginAttributes originAttributes;
  auto func = [callback(aCallback)](bool aResult, nsresult aStatus) {
    callback->DoCallback(aResult);
  };
  rv = NS_ShouldSecureUpgrade(uri, nullptr, nullptr, nullptr, false,
                              originAttributes, shouldUpgrade, std::move(func),
                              willCallback);
  if (NS_FAILED(rv) || !willCallback) {
    aCallback->DoCallback(false);
    return rv;
  }

  return rv;
}

NS_IMETHODIMP
nsHttpHandler::EnsureHSTSDataReady(JSContext* aCx, Promise** aPromise) {
  if (NS_WARN_IF(!aCx)) {
    return NS_ERROR_FAILURE;
  }

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  if (IsNeckoChild()) {
    gNeckoChild->SendEnsureHSTSData()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [promise(promise)](
            NeckoChild::EnsureHSTSDataPromise::ResolveOrRejectValue&& aResult) {
          if (aResult.IsResolve()) {
            promise->MaybeResolve(aResult.ResolveValue());
          } else {
            promise->MaybeReject(NS_ERROR_FAILURE);
          }
        });
    promise.forget(aPromise);
    return NS_OK;
  }

  auto callback = [promise(promise)](bool aResult) {
    promise->MaybeResolve(aResult);
  };

  RefPtr<HSTSDataCallbackWrapper> wrapper =
      new HSTSDataCallbackWrapper(std::move(callback));
  promise.forget(aPromise);
  return EnsureHSTSDataReadyNative(wrapper);
}

NS_IMETHODIMP
nsHttpHandler::ClearCORSPreflightCache() {
  nsCORSListenerProxy::ClearCache();
  return NS_OK;
}

void nsHttpHandler::ShutdownConnectionManager() {
  // ensure connection manager is shutdown
  if (mConnMgr) {
    nsresult rv = mConnMgr->Shutdown();
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpHandler::ShutdownConnectionManager\n"
           "    failed to shutdown connection manager\n"));
    }
  }
}

uint64_t nsHttpHandler::NewChannelId() {
  // channelId is sometimes passed to JavaScript code (e.g. devtools),
  // values should not exceed safe JavaScript integer range (2^53 – 1).
  // Since the uniqueProcessId starts at 0, this should be safe to use
  // unless we create more than 2^22 processes.
  return ((mUniqueProcessId << 31) & 0xFFFFFFFF80000000LL) | mNextChannelId++;
}

void nsHttpHandler::NotifyActiveTabLoadOptimization() {
  SetLastActiveTabLoadOptimizationHit(TimeStamp::Now());
}

TimeStamp nsHttpHandler::GetLastActiveTabLoadOptimizationHit() {
  auto lastTimestamp = mLastActiveTabLoadOptimizationHit.Lock();
  return *lastTimestamp;
}

void nsHttpHandler::SetLastActiveTabLoadOptimizationHit(TimeStamp const& when) {
  if (when.IsNull()) {
    return;
  }
  auto lastTimestamp = mLastActiveTabLoadOptimizationHit.Lock();
  if (lastTimestamp->IsNull() || *lastTimestamp < when) {
    *lastTimestamp = when;
  }
}

bool nsHttpHandler::IsBeforeLastActiveTabLoadOptimization(
    TimeStamp const& when) {
  auto lastTimestamp = mLastActiveTabLoadOptimizationHit.Lock();
  return !lastTimestamp->IsNull() && when <= *lastTimestamp;
}

void nsHttpHandler::ExcludeHttp2OrHttp3Internal(nsHttpConnectionInfo* ci) {
  if (ci->GetHappyEyeballsEnabled()) {
    return;
  }
  LOG(("nsHttpHandler::ExcludeHttp2OrHttp3Internal ci=%s",
       ci->HashKey().get()));
  MOZ_ASSERT(OnSocketThread());

  if (ci->IsHttp3()) {
    {
      MutexAutoLock lock(mHttpExclusionLock);
      mExcludedHttp3Origins.Insert(ci->GetRoutedHost());
    }
    mConnMgr->ExcludeHttp3(ci);
  } else {
    {
      MutexAutoLock lock(mHttpExclusionLock);
      mExcludedHttp2Origins.Insert(ci->GetOrigin());
    }
    mConnMgr->ExcludeHttp2(ci);
  }
}

void nsHttpHandler::ExcludeHttp2(nsHttpConnectionInfo* ci) {
  ExcludeHttp2OrHttp3Internal(ci);
}

bool nsHttpHandler::IsHttp2Excluded(const nsHttpConnectionInfo* ci) {
  MutexAutoLock lock(mHttpExclusionLock);
  return mExcludedHttp2Origins.Contains(ci->GetOrigin());
}

void nsHttpHandler::ExcludeHttp3(nsHttpConnectionInfo* ci) {
  // TODO: exclude HTTP/3 for proxy connection properly.
  if (ci->IsHttp3ProxyConnection()) {
    return;
  }
  MOZ_ASSERT(ci->IsHttp3());
  ExcludeHttp2OrHttp3Internal(ci);
}

bool nsHttpHandler::IsHttp3Excluded(const nsACString& aRoutedHost) {
  MutexAutoLock lock(mHttpExclusionLock);
  return mExcludedHttp3Origins.Contains(aRoutedHost);
}

HttpTrafficAnalyzer* nsHttpHandler::GetHttpTrafficAnalyzer() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!StaticPrefs::network_traffic_analyzer_enabled()) {
    return nullptr;
  }

  return &mHttpTrafficAnalyzer;
}

bool nsHttpHandler::IsHttp3Enabled() {
  static const uint32_t TLS3_PREF_VALUE = 4;

  return StaticPrefs::network_http_http3_enable() &&
         (StaticPrefs::security_tls_version_max() >= TLS3_PREF_VALUE);
}

bool nsHttpHandler::IsHttp3VersionSupported(const nsACString& version) {
  return (StaticPrefs::network_http_http3_support_version1() &&
          version.EqualsLiteral("h3"));
}

bool nsHttpHandler::IsHttp3SupportedByServer(
    nsHttpResponseHead* aResponseHead) {
  if ((aResponseHead->Version() != HttpVersion::v2_0) ||
      (aResponseHead->Status() >= 500) || (aResponseHead->Status() == 421)) {
    return false;
  }

  nsAutoCString altSvc;
  (void)aResponseHead->GetHeader(nsHttp::Alternate_Service, altSvc);
  if (altSvc.IsEmpty() || !nsHttp::IsReasonableHeaderValue(altSvc)) {
    return false;
  }

  return altSvc.Find("h3=") != -1;
}

nsresult nsHttpHandler::InitiateTransaction(HttpTransactionShell* aTrans,
                                            int32_t aPriority) {
  return mConnMgr->AddTransaction(aTrans, aPriority);
}
nsresult nsHttpHandler::InitiateTransactionWithStickyConn(
    HttpTransactionShell* aTrans, int32_t aPriority,
    HttpTransactionShell* aTransWithStickyConn) {
  return mConnMgr->AddTransactionWithStickyConn(aTrans, aPriority,
                                                aTransWithStickyConn);
}

nsresult nsHttpHandler::RescheduleTransaction(HttpTransactionShell* trans,
                                              int32_t priority) {
  return mConnMgr->RescheduleTransaction(trans, priority);
}

void nsHttpHandler::UpdateClassOfServiceOnTransaction(
    HttpTransactionShell* trans, const ClassOfService& classOfService) {
  mConnMgr->UpdateClassOfServiceOnTransaction(trans, classOfService);
}

nsresult nsHttpHandler::CancelTransaction(HttpTransactionShell* trans,
                                          nsresult reason) {
  return mConnMgr->CancelTransaction(trans, reason);
}

void nsHttpHandler::AddHttpChannel(uint64_t aId, nsISupports* aChannel) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  nsWeakPtr channel(do_GetWeakReference(aChannel));
  mIDToHttpChannelMap.InsertOrUpdate(aId, std::move(channel));
}

void nsHttpHandler::RemoveHttpChannel(uint64_t aId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIRunnable> idleRunnable(NewCancelableRunnableMethod<uint64_t>(
        "nsHttpHandler::RemoveHttpChannel", this,
        &nsHttpHandler::RemoveHttpChannel, aId));

    NS_DispatchToMainThreadQueue(do_AddRef(idleRunnable),
                                 EventQueuePriority::Idle);
    return;
  }

  auto entry = mIDToHttpChannelMap.Lookup(aId);
  if (entry) {
    entry.Remove();
  }
}

nsWeakPtr nsHttpHandler::GetWeakHttpChannel(uint64_t aId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  return mIDToHttpChannelMap.Get(aId);
}

nsresult nsHttpHandler::CompleteUpgrade(
    HttpTransactionShell* aTrans, nsIHttpUpgradeListener* aUpgradeListener) {
  return mConnMgr->CompleteUpgrade(aTrans, aUpgradeListener);
}

nsresult nsHttpHandler::DoShiftReloadConnectionCleanupWithConnInfo(
    nsHttpConnectionInfo* aCi) {
  MOZ_ASSERT(aCi);
  return mConnMgr->DoShiftReloadConnectionCleanupWithConnInfo(aCi);
}

void nsHttpHandler::ClearHostMapping(nsHttpConnectionInfo* aConnInfo) {
  AltServiceCache()->ClearHostMapping(aConnInfo);
}

void nsHttpHandler::MaybeAddAltSvcForTesting(
    nsIURI* aUri, const nsACString& aUsername, bool aPrivateBrowsing,
    nsIInterfaceRequestor* aCallbacks,
    const OriginAttributes& aOriginAttributes) {
  if (!IsHttp3Enabled()) {
    return;
  }

  if (!aUri->SchemeIs("https")) {
    // Only set for HTTPS.
    return;
  }

  nsAutoCString originHost;
  if (NS_FAILED(aUri->GetAsciiHost(originHost))) {
    return;
  }

  nsCString map;
  {
    auto mappings = mAltSvcMappingTemptativeMap.Lock();
    nsCString* found = mappings->Get(originHost);
    if (!found) {
      return;
    }
    map = *found;
  }

  int32_t originPort = 80;
  aUri->GetPort(&originPort);
  LOG(("nsHttpHandler::MaybeAddAltSvcForTesting for %s map: %s",
       originHost.get(), map.get()));
  AltSvcMapping::ProcessHeader(map, "https"_ns, originHost, originPort,
                               aUsername, aPrivateBrowsing, aCallbacks, nullptr,
                               0, aOriginAttributes, nullptr, true);
}

bool nsHttpHandler::EchConfigEnabled(bool aIsHttp3) {
  if (sParentalControlsEnabled) {
    return false;
  }

  if (!aIsHttp3) {
    return StaticPrefs::network_dns_echconfig_enabled();
  }

  return StaticPrefs::network_dns_http3_echconfig_enabled();
}

bool nsHttpHandler::FallbackToOriginIfConfigsAreECHAndAllFailed() const {
  return StaticPrefs::
      network_dns_echconfig_fallback_to_origin_when_all_failed();
}

void nsHttpHandler::ExcludeHTTPSRRHost(const nsACString& aHost) {
  MOZ_ASSERT(NS_IsMainThread());

  mExcludedHostsForHTTPSRRUpgrade.Insert(aHost);
}

bool nsHttpHandler::IsHostExcludedForHTTPSRR(const nsACString& aHost) {
  MOZ_ASSERT(NS_IsMainThread());

  return mExcludedHostsForHTTPSRRUpgrade.Contains(aHost);
}

#ifdef XP_MACOSX
bool nsHttpHandler::IsHostMSAuthority(const nsACString& aHost) {
  MOZ_ASSERT(NS_IsMainThread());

  return mMSAuthorities.Contains(aHost);
}
#endif

void nsHttpHandler::Exclude0RttTcp(const nsHttpConnectionInfo* ci) {
  MOZ_ASSERT(OnSocketThread());

  if (!StaticPrefs::network_http_early_data_disable_on_error() ||
      (mExcluded0RttTcpOrigins.Count() >=
       StaticPrefs::network_http_early_data_max_error())) {
    return;
  }

  mExcluded0RttTcpOrigins.Insert(ci->GetOrigin());
}

bool nsHttpHandler::Is0RttTcpExcluded(const nsHttpConnectionInfo* ci) {
  MOZ_ASSERT(OnSocketThread());
  if (!StaticPrefs::network_http_early_data_disable_on_error()) {
    return false;
  }

  if (mExcluded0RttTcpOrigins.Count() >=
      StaticPrefs::network_http_early_data_max_error()) {
    return true;
  }

  return mExcluded0RttTcpOrigins.Contains(ci->GetOrigin());
}

bool nsHttpHandler::HttpActivityDistributorActivated() {
  if (!mActivityDistributor) {
    return false;
  }

  return mActivityDistributor->Activated();
}

void nsHttpHandler::ObserveHttpActivityWithArgs(
    const HttpActivityArgs& aArgs, uint32_t aActivityType,
    uint32_t aActivitySubtype, PRTime aTimestamp, uint64_t aExtraSizeData,
    const nsACString& aExtraStringData) {
  if (!HttpActivityDistributorActivated()) {
    return;
  }

  if (aActivitySubtype == NS_HTTP_ACTIVITY_SUBTYPE_PROXY_RESPONSE_HEADER &&
      !mActivityDistributor->ObserveProxyResponseEnabled()) {
    return;
  }

  if (aActivityType == NS_ACTIVITY_TYPE_HTTP_CONNECTION &&
      !mActivityDistributor->ObserveConnectionEnabled()) {
    return;
  }

  (void)mActivityDistributor->ObserveActivityWithArgs(
      aArgs, aActivityType, aActivitySubtype, aTimestamp, aExtraSizeData,
      aExtraStringData);
}

}  // namespace mozilla::net
