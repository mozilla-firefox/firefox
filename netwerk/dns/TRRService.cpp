/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TRRService.h"

#include "DNSServiceBase.h"
#include "TRR.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/glean/NetwerkDnsMetrics.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/TRRServiceChild.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceUtils.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsICaptivePortalService.h"
#include "nsIFile.h"
#include "nsINetworkLinkService.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsNetUtil.h"
#include "nsSocketTransportService2.h"
#include "nsStandardURL.h"
// Put DNSLogging.h at the end to avoid LOG being overwritten by other headers.
#include "DNSLogging.h"

static const char kOpenCaptivePortalLoginEvent[] = "captive-portal-login";
static const char kClearPrivateData[] = "clear-private-data";
static const char kPurge[] = "browser:purge-session-history";

#define TRR_PREF_PREFIX "network.trr."
#define TRR_PREF(x) TRR_PREF_PREFIX x

namespace mozilla::net {

StaticRefPtr<nsIThread> sTRRBackgroundThread;
static Atomic<TRRService*> sTRRServicePtr;

static Atomic<size_t, Relaxed> sDomainIndex(0);
static Atomic<size_t, Relaxed> sCurrentTRRModeIndex(0);

constexpr nsLiteralCString kTRRDomains[3][7] = {
    // clang-format off
    {
    // When mode is 0, the provider key has no postfix.
    "(other)"_ns,
    "mozilla.cloudflare-dns.com"_ns,
    "firefox.dns.nextdns.io"_ns,
    "private.canadianshield.cira.ca"_ns,
    "doh.xfinity.com"_ns,  // Steered clients
    "dns.shaw.ca"_ns, // Steered clients
    "dooh.cloudflare-dns.com"_ns, // DNS over Oblivious HTTP
    },
    {
    "(other)_2"_ns,
    "mozilla.cloudflare-dns.com_2"_ns,
    "firefox.dns.nextdns.io_2"_ns,
    "private.canadianshield.cira.ca_2"_ns,
    "doh.xfinity.com_2"_ns,  // Steered clients
    "dns.shaw.ca_2"_ns, // Steered clients
    "dooh.cloudflare-dns.com_2"_ns, // DNS over Oblivious HTTP
    },
    {
    "(other)_3"_ns,
    "mozilla.cloudflare-dns.com_3"_ns,
    "firefox.dns.nextdns.io_3"_ns,
    "private.canadianshield.cira.ca_3"_ns,
    "doh.xfinity.com_3"_ns,  // Steered clients
    "dns.shaw.ca_3"_ns, // Steered clients
    "dooh.cloudflare-dns.com_3"_ns, // DNS over Oblivious HTTP
    },
    // clang-format on
};

// static
void TRRService::SetCurrentTRRMode(nsIDNSService::ResolverMode aMode) {
  // A table to map ResolverMode to the row of kTRRDomains.
  // When the aMode is 2, we use kTRRDomains[1] as provider keys. When aMode is
  // 3, we use kTRRDomains[2]. Otherwise, we kTRRDomains[0] is used.
  static const uint32_t index[] = {0, 0, 1, 2, 0, 0};
  if (aMode > nsIDNSService::MODE_TRROFF) {
    aMode = nsIDNSService::MODE_TRROFF;
  }
  sCurrentTRRModeIndex = index[static_cast<size_t>(aMode)];
}

// static
void TRRService::SetProviderDomain(const nsACString& aTRRDomain) {
  sDomainIndex = 0;
  for (size_t i = 1; i < std::size(kTRRDomains[0]); i++) {
    if (aTRRDomain.Equals(kTRRDomains[0][i])) {
      sDomainIndex = i;
      break;
    }
  }
}

const nsCString& TRRProviderKey() { return TRRService::ProviderKey(); }

// static
const nsCString& TRRService::ProviderKey() {
  return kTRRDomains[sCurrentTRRModeIndex][sDomainIndex];
}

NS_IMPL_ISUPPORTS_INHERITED(TRRService, TRRServiceBase, nsIObserver,
                            nsISupportsWeakReference)

NS_IMPL_ADDREF_USING_AGGREGATOR(TRRService::ConfirmationContext, OwningObject())
NS_IMPL_RELEASE_USING_AGGREGATOR(TRRService::ConfirmationContext,
                                 OwningObject())
NS_IMPL_QUERY_INTERFACE(TRRService::ConfirmationContext, nsITimerCallback,
                        nsINamed)

TRRService::TRRService() { MOZ_ASSERT(NS_IsMainThread(), "wrong thread"); }

// static
TRRService* TRRService::Get() { return sTRRServicePtr; }

// static
void TRRService::AddObserver(nsIObserver* aObserver,
                             nsIObserverService* aObserverService) {
  nsCOMPtr<nsIObserverService> observerService;
  if (aObserverService) {
    observerService = aObserverService;
  } else {
    observerService = mozilla::services::GetObserverService();
  }

  if (observerService) {
    observerService->AddObserver(aObserver, NS_CAPTIVE_PORTAL_CONNECTIVITY,
                                 true);
    observerService->AddObserver(aObserver, kOpenCaptivePortalLoginEvent, true);
    observerService->AddObserver(aObserver, kClearPrivateData, true);
    observerService->AddObserver(aObserver, kPurge, true);
    observerService->AddObserver(aObserver, NS_NETWORK_LINK_TOPIC, true);
    observerService->AddObserver(aObserver, NS_DNS_SUFFIX_LIST_UPDATED_TOPIC,
                                 true);
    observerService->AddObserver(aObserver, "xpcom-shutdown-threads", true);
    observerService->AddObserver(aObserver, "application-foreground", true);
  }
}

// static
bool TRRService::CheckCaptivePortalIsPassed() {
  bool result = false;
  nsCOMPtr<nsICaptivePortalService> captivePortalService =
      do_GetService(NS_CAPTIVEPORTAL_CID);
  if (captivePortalService) {
    int32_t captiveState;
    MOZ_ALWAYS_SUCCEEDS(captivePortalService->GetState(&captiveState));

    if ((captiveState == nsICaptivePortalService::UNLOCKED_PORTAL) ||
        (captiveState == nsICaptivePortalService::NOT_CAPTIVE)) {
      result = true;
    }
    LOG(("TRRService::Init mCaptiveState=%d mCaptiveIsPassed=%d\n",
         captiveState, (int)result));
  }

  return result;
}

nsresult TRRService::Init(bool aNativeHTTPSQueryEnabled) {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  if (mInitialized) {
    return NS_OK;
  }
  mInitialized = true;

  AddObserver(this);

  nsCOMPtr<nsIPrefBranch> prefBranch;
  GetPrefBranch(getter_AddRefs(prefBranch));
  if (prefBranch) {
    prefBranch->AddObserver(TRR_PREF_PREFIX, this, true);
    prefBranch->AddObserver(kRolloutURIPref, this, true);
    prefBranch->AddObserver(kRolloutModePref, this, true);
  }

  sTRRServicePtr = this;

  mNativeHTTPSQueryEnabled = aNativeHTTPSQueryEnabled;
  ReadPrefs(nullptr);
  mConfirmation.HandleEvent(ConfirmationEvent::Init);

  if (XRE_IsParentProcess()) {
    mCaptiveIsPassed = CheckCaptivePortalIsPassed();

    mParentalControlEnabled = GetParentalControlsEnabledInternal();

    mLinkService = do_GetService(NS_NETWORK_LINK_SERVICE_CONTRACTID);
    if (mLinkService) {
      nsTArray<nsCString> suffixList;
      mLinkService->GetDnsSuffixList(suffixList);
      RebuildSuffixList(std::move(suffixList));
    }

    if (!StaticPrefs::network_trr_parse_on_socket_thread()) {
      nsCOMPtr<nsIThread> thread;
      if (NS_FAILED(
              NS_NewNamedThread("TRR Background", getter_AddRefs(thread)))) {
        NS_WARNING("NS_NewNamedThread failed!");
        return NS_ERROR_FAILURE;
      }

      sTRRBackgroundThread = thread;
    }
  }

  LOG(("Initialized TRRService\n"));
  return NS_OK;
}

// static
bool TRRService::GetParentalControlsEnabledInternal() {
  return nsHttpHandler::GetParentalControlsEnabled();
}

// static, for testing purposes only
bool TRRService::ReloadParentalControlsEnabled() {
  nsHttpHandler::UpdateParentalControlsEnabled(true /* wait for completion */);
  return nsHttpHandler::GetParentalControlsEnabled();
}

void TRRService::SetDetectedTrrURI(const nsACString& aURI) {
  LOG(("SetDetectedTrrURI(%s", nsPromiseFlatCString(aURI).get()));
  // If the user has set a custom URI then we don't want to override that.
  // If the URI is set via doh-rollout.uri, mURIPref will be empty
  // (see TRRServiceBase::OnTRRURIChange)
  if (!mURIPref.IsEmpty()) {
    LOG(("Already has user value. Not setting URI"));
    return;
  }

  if (StaticPrefs::network_trr_use_ohttp()) {
    LOG(("No autodetection when using OHTTP"));
    return;
  }

  mURISetByDetection = MaybeSetPrivateURI(aURI);
}

bool TRRService::Enabled(nsIRequest::TRRMode aRequestMode) {
  if (mMode == nsIDNSService::MODE_TRROFF ||
      aRequestMode == nsIRequest::TRR_DISABLED_MODE) {
    LOG(("TRR service not enabled - off or disabled"));
    return false;
  }

  // If already confirmed, service is enabled.
  if (mConfirmation.State() == CONFIRM_OK ||
      aRequestMode == nsIRequest::TRR_ONLY_MODE) {
    LOG(("TRR service enabled - confirmed or trr_only request"));
    return true;
  }

  // If this is a TRR_FIRST request but the resolver has a different mode,
  // just go ahead and let it try to use TRR.
  if (aRequestMode == nsIRequest::TRR_FIRST_MODE &&
      mMode != nsIDNSService::MODE_TRRFIRST) {
    LOG(("TRR service enabled - trr_first request"));
    return true;
  }

  // In TRR_ONLY_MODE / confirmationNS == "skip" we don't try to confirm.
  if (mConfirmation.State() == CONFIRM_DISABLED) {
    LOG(("TRRService service enabled - confirmation is disabled"));
    return true;
  }

  LOG(("TRRService::Enabled mConfirmation.mState=%d mCaptiveIsPassed=%d\n",
       mConfirmation.State(), (int)mCaptiveIsPassed));

  if (StaticPrefs::network_trr_wait_for_confirmation()) {
    return mConfirmation.State() == CONFIRM_OK;
  }

  if (StaticPrefs::network_trr_attempt_when_retrying_confirmation()) {
    return mConfirmation.State() == CONFIRM_OK ||
           mConfirmation.State() == CONFIRM_TRYING_OK ||
           mConfirmation.State() == CONFIRM_TRYING_FAILED;
  }

  return mConfirmation.State() == CONFIRM_OK ||
         mConfirmation.State() == CONFIRM_TRYING_OK;
}

void TRRService::GetPrefBranch(nsIPrefBranch** result) {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  *result = nullptr;
  CallGetService(NS_PREFSERVICE_CONTRACTID, result);
}

bool TRRService::MaybeSetPrivateURI(const nsACString& aURI) {
  bool clearCache = false;
  nsAutoCString newURI(aURI);
  LOG(("MaybeSetPrivateURI(%s)", newURI.get()));

  ProcessURITemplate(newURI);
  {
    MutexAutoLock lock(mLock);
    if (mPrivateURI.Equals(newURI)) {
      return false;
    }

    if (!mPrivateURI.IsEmpty()) {
      LOG(("TRRService clearing blocklist because of change in uri service\n"));
      auto bl = mTRRBLStorage.Lock();
      bl->Clear();
      clearCache = true;
    }

    nsAutoCString host;

    nsCOMPtr<nsIURI> url;
    if (NS_SUCCEEDED(NS_NewURI(getter_AddRefs(url), newURI))) {
      url->GetHost(host);
    }

    SetProviderDomain(host);

    mPrivateURI = newURI;

    // Notify the content processes of the new TRR
    for (auto* cp :
         dom::ContentParent::AllProcesses(dom::ContentParent::eLive)) {
      PNeckoParent* neckoParent =
          SingleManagedOrNull(cp->ManagedPNeckoParent());
      if (!neckoParent) {
        continue;
      }
      (void)neckoParent->SendSetTRRDomain(host);
    }

    // The URI has changed. We should trigger a new confirmation immediately.
    // We must do this here because the URI could also change because of
    // steering.
    mConfirmationTriggered =
        mConfirmation.HandleEvent(ConfirmationEvent::URIChange, lock);
  }

  // Clear the cache because we changed the URI
  if (clearCache) {
    ClearEntireCache();
  }

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, NS_NETWORK_TRR_URI_CHANGED_TOPIC, nullptr);
  }

  // Call this without lock to avoid deadlock.
  AsyncCreateTRRConnectionInfo(newURI);

  return true;
}

nsresult TRRService::ReadPrefs(const char* name) {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");

  // Whenever a pref change occurs that would cause us to clear the cache
  // we set this to true then do it at the end of the method.
  bool clearEntireCache = false;

  if (!name || !strcmp(name, TRR_PREF("mode")) ||
      !strcmp(name, kRolloutModePref)) {
    nsIDNSService::ResolverMode prevMode = Mode();

    OnTRRModeChange();
    // When the TRR service gets disabled we should purge the TRR cache to
    // make sure we don't use any of the cached entries on a network where
    // they are invalid - for example after turning on a VPN.
    if (TRR_DISABLED(Mode()) && !TRR_DISABLED(prevMode)) {
      clearEntireCache = true;
    }
  }
  if (!name || !strcmp(name, TRR_PREF("uri")) ||
      !strcmp(name, TRR_PREF("default_provider_uri")) ||
      !strcmp(name, kRolloutURIPref) || !strcmp(name, TRR_PREF("ohttp.uri")) ||
      !strcmp(name, TRR_PREF("use_ohttp"))) {
    OnTRRURIChange();
  }
  if (!name || !strcmp(name, TRR_PREF("credentials"))) {
    MutexAutoLock lock(mLock);
    Preferences::GetCString(TRR_PREF("credentials"), mPrivateCred);
  }
  if (!name || !strcmp(name, TRR_PREF("confirmationNS"))) {
    MutexAutoLock lock(mLock);
    Preferences::GetCString(TRR_PREF("confirmationNS"), mConfirmationNS);
    LOG(("confirmationNS = %s", mConfirmationNS.get()));
  }
  if (!name || !strcmp(name, TRR_PREF("bootstrapAddr"))) {
    MutexAutoLock lock(mLock);
    Preferences::GetCString(TRR_PREF("bootstrapAddr"), mBootstrapAddr);
    clearEntireCache = true;
  }
  if (!name || !strcmp(name, TRR_PREF("excluded-domains")) ||
      !strcmp(name, TRR_PREF("builtin-excluded-domains"))) {
    MutexAutoLock lock(mLock);

    mExcludedDomains.Clear();

    auto parseExcludedDomains = [this](const char* aPrefName) MOZ_REQUIRES(
                                    mLock) {
      nsAutoCString excludedDomains;
      Preferences::GetCString(aPrefName, excludedDomains);
      if (excludedDomains.IsEmpty()) {
        return;
      }

      for (const nsACString& tokenSubstring :
           nsCCharSeparatedTokenizerTemplate<
               NS_IsAsciiWhitespace, nsTokenizerFlags::SeparatorOptional>(
               excludedDomains, ',')
               .ToRange()) {
        nsCString token{tokenSubstring};
        LOG(("TRRService::ReadPrefs %s host:[%s]\n", aPrefName, token.get()));
        mExcludedDomains.Insert(token);
      }
    };

    parseExcludedDomains(TRR_PREF("excluded-domains"));
    parseExcludedDomains(TRR_PREF("builtin-excluded-domains"));
    clearEntireCache = true;
  }
  if (!name || !strcmp(name, TRR_PREF("force_http3_first"))) {
    nsAutoCString uri;
    GetURI(uri);
    AsyncCreateTRRConnectionInfo(uri);
  }

  // if name is null, then we're just now initializing. In that case we don't
  // need to clear the cache.
  if (name && clearEntireCache) {
    ClearEntireCache();
  }

  return NS_OK;
}

void TRRService::ClearEntireCache() {
  if (!StaticPrefs::network_trr_clear_cache_on_pref_change()) {
    return;
  }
  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID);
  if (!dns) {
    return;
  }
  dns->ClearCache(true);
}

void TRRService::AddEtcHosts(const nsTArray<nsCString>& aArray) {
  MutexAutoLock lock(mLock);
  for (const auto& item : aArray) {
    LOG(("Adding %s from /etc/hosts to excluded domains", item.get()));
    mEtcHostsDomains.Insert(item);
  }
}

void TRRService::ReadEtcHostsFile() {
  if (!XRE_IsParentProcess()) {
    return;
  }

  DNSServiceBase::DoReadEtcHostsFile(
      [](const nsTArray<nsCString>* aArray) -> bool {
        RefPtr<TRRService> service(sTRRServicePtr);
        if (service && aArray) {
          service->AddEtcHosts(*aArray);
        }
        return !!service;
      });
}

void TRRService::GetURI(nsACString& result) {
  MutexAutoLock lock(mLock);
  result = mPrivateURI;
}

nsresult TRRService::GetCredentials(nsCString& result) {
  MutexAutoLock lock(mLock);
  result = mPrivateCred;
  return NS_OK;
}

uint32_t TRRService::GetRequestTimeout() {
  if (mMode == nsIDNSService::MODE_TRRONLY) {
    return StaticPrefs::network_trr_request_timeout_mode_trronly_ms();
  }

  if (StaticPrefs::network_trr_strict_native_fallback()) {
    return StaticPrefs::network_trr_strict_fallback_request_timeout_ms();
  }

#if defined(ANDROID)
  // On Android, the DoH connection is more likely to be broken due to network
  // changes, etc. We use a slightly lower timeout for doh-rollout
  // but users in mode 2 or 3 still get the regular timeout.
  if (StaticPrefs::network_trr_mode() == nsIDNSService::MODE_NATIVEONLY &&
      mMode == nsIDNSService::MODE_TRRFIRST) {
    return StaticPrefs::network_android_doh_rollout_request_timeout_ms();
  }
#endif

  return StaticPrefs::network_trr_request_timeout_ms();
}

nsresult TRRService::Start() {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  if (!mInitialized) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  return NS_OK;
}

TRRService::~TRRService() {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  LOG(("Exiting TRRService\n"));
}

nsresult TRRService::DispatchTRRRequest(TRR* aTrrRequest) {
  return DispatchTRRRequestInternal(aTrrRequest, true);
}

nsresult TRRService::DispatchTRRRequestInternal(TRR* aTrrRequest,
                                                bool aWithLock) {
  NS_ENSURE_ARG_POINTER(aTrrRequest);

  nsCOMPtr<nsIThread> thread = MainThreadOrTRRThread(aWithLock);
  if (!thread) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<TRR> trr = aTrrRequest;
  return thread->Dispatch(trr.forget());
}

already_AddRefed<nsIThread> TRRService::MainThreadOrTRRThread(bool aWithLock) {
  if (XRE_IsSocketProcess() || mDontUseTRRThread) {
    return do_GetMainThread();
  }

  nsCOMPtr<nsIThread> thread = aWithLock ? TRRThread() : TRRThread_locked();
  return thread.forget();
}

already_AddRefed<nsIThread> TRRService::TRRThread() {
  MutexAutoLock lock(mLock);
  return TRRThread_locked();
}

already_AddRefed<nsIThread> TRRService::TRRThread_locked() {
  if (StaticPrefs::network_trr_parse_on_socket_thread()) {
    if (!gSocketTransportService) {
      return nullptr;
    }

    return gSocketTransportService->GetSocketThread();
  }

  RefPtr<nsIThread> thread = sTRRBackgroundThread;
  return thread.forget();
}

bool TRRService::IsOnTRRThread() {
  if (StaticPrefs::network_trr_parse_on_socket_thread()) {
    if (!gSocketTransportService) {
      return false;
    }

    return OnSocketThread();
  }

  nsCOMPtr<nsIThread> thread;
  {
    MutexAutoLock lock(mLock);
    thread = sTRRBackgroundThread;
  }
  if (!thread) {
    return false;
  }

  return thread->IsOnCurrentThread();
}

NS_IMETHODIMP
TRRService::Observe(nsISupports* aSubject, const char* aTopic,
                    const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread(), "wrong thread");
  LOG(("TRR::Observe() topic=%s\n", aTopic));
  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    // Reset the state of whether a confirmation is triggered, so we can check
    // if we create a new one after ReadPrefs().
    mConfirmationTriggered = false;
    ReadPrefs(NS_ConvertUTF16toUTF8(aData).get());
    {
      MutexAutoLock lock(mLock);
      mConfirmation.RecordEvent("pref-change", lock);
    }

    // We should only trigger a new confirmation if reading the prefs didn't
    // already trigger one.
    if (!mConfirmationTriggered) {
      mConfirmation.HandleEvent(ConfirmationEvent::PrefChange);
    }
  } else if (!strcmp(aTopic, kOpenCaptivePortalLoginEvent)) {
    // We are in a captive portal
    LOG(("TRRservice in captive portal\n"));
    mCaptiveIsPassed = false;
    mConfirmation.SetCaptivePortalStatus(
        nsICaptivePortalService::LOCKED_PORTAL);
  } else if (!strcmp(aTopic, NS_CAPTIVE_PORTAL_CONNECTIVITY)) {
    nsAutoCString data = NS_ConvertUTF16toUTF8(aData);
    LOG(("TRRservice captive portal was %s\n", data.get()));
    nsCOMPtr<nsICaptivePortalService> cps = do_QueryInterface(aSubject);
    if (cps) {
      mConfirmation.SetCaptivePortalStatus(cps->State());
    }

    // If we were previously in a captive portal, this event means we will
    // need to trigger confirmation again. Otherwise it's just a periodical
    // captive-portal check that completed and we don't need to react to it.
    if (!mCaptiveIsPassed) {
      mConfirmation.HandleEvent(ConfirmationEvent::CaptivePortalConnectivity);
    }

    mCaptiveIsPassed = true;
  } else if (!strcmp(aTopic, kClearPrivateData) || !strcmp(aTopic, kPurge)) {
    // flush the TRR blocklist
    auto bl = mTRRBLStorage.Lock();
    bl->Clear();
  } else if (!strcmp(aTopic, NS_DNS_SUFFIX_LIST_UPDATED_TOPIC) ||
             !strcmp(aTopic, NS_NETWORK_LINK_TOPIC)) {
    // nsINetworkLinkService is only available on parent process.
    if (XRE_IsParentProcess()) {
      nsCOMPtr<nsINetworkLinkService> link = do_QueryInterface(aSubject);
      // The network link service notification normally passes itself as the
      // subject, but some unit tests will sometimes pass a null subject.
      if (link) {
        nsTArray<nsCString> suffixList;
        link->GetDnsSuffixList(suffixList);
        RebuildSuffixList(std::move(suffixList));
      }
    }

    if (!strcmp(aTopic, NS_NETWORK_LINK_TOPIC)) {
      nsAutoCString converted = NS_ConvertUTF16toUTF8(aData);
      if (converted.EqualsLiteral(NS_NETWORK_LINK_DATA_DOWN)) {
        MutexAutoLock lock(mLock);
        mConfirmation.RecordEvent("network-down", lock);
      } else if (converted.EqualsLiteral(NS_NETWORK_LINK_DATA_CHANGED)) {
        MutexAutoLock lock(mLock);
        mConfirmation.RecordEvent("network-change", lock);
      }

      if (mURISetByDetection) {
        // If the URI was set via SetDetectedTrrURI we need to restore it to the
        // default pref when a network link change occurs.
        CheckURIPrefs();
      }

      if (converted.EqualsLiteral(NS_NETWORK_LINK_DATA_UP)) {
        mConfirmation.HandleEvent(ConfirmationEvent::NetworkUp);
      }
    }
  } else if (!strcmp(aTopic, "application-foreground")) {
    MaybeSpeculativeConnectToTRR();
  } else if (!strcmp(aTopic, "xpcom-shutdown-threads")) {
    mShutdown = true;
    // If a confirmation is still in progress we record the event.
    // Since there should be no more confirmations after this, the shutdown
    // reason would not really be recorded in telemetry.
    {
      MutexAutoLock lock(mLock);
      mConfirmation.RecordEvent("shutdown", lock);
    }

    if (sTRRBackgroundThread) {
      nsCOMPtr<nsIThread> thread;
      thread = sTRRBackgroundThread.get();
      sTRRBackgroundThread = nullptr;
      MOZ_ALWAYS_SUCCEEDS(thread->Shutdown());
    }
    sTRRServicePtr = nullptr;
  }
  return NS_OK;
}

void TRRService::RebuildSuffixList(nsTArray<nsCString>&& aSuffixList) {
  if (!StaticPrefs::network_trr_split_horizon_mitigations() || mShutdown) {
    return;
  }

  MutexAutoLock lock(mLock);
  mDNSSuffixDomains.Clear();
  for (const auto& item : aSuffixList) {
    LOG(("TRRService adding %s to suffix list", item.get()));
    mDNSSuffixDomains.Insert(item);
  }
}

void TRRService::MaybeSpeculativeConnectToTRR() {
  if (!StaticPrefs::network_trr_preconnect_on_foreground() || !Enabled()) {
    return;
  }

  RefPtr<nsHttpConnectionInfo> ci = TRRConnectionInfo();
  if (!ci) {
    return;
  }

  (void)gHttpHandler->SpeculativeConnect(ci, nullptr, 0, nullptr);
}

void TRRService::ConfirmationContext::SetState(
    enum ConfirmationState aNewState) {
  LOG(("ConfirmationContext::SetState %u", uint32_t(aNewState)));
  mState = aNewState;

  enum ConfirmationState state = mState;
  if (XRE_IsParentProcess()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "TRRService::ConfirmationContextNotify", [state] {
          if (nsCOMPtr<nsIObserverService> obs =
                  mozilla::services::GetObserverService()) {
            auto stateString =
                [](enum ConfirmationState aState) -> const char16_t* {
              switch (aState) {
                case CONFIRM_OFF:
                  return u"CONFIRM_OFF";
                case CONFIRM_TRYING_OK:
                  return u"CONFIRM_TRYING_OK";
                case CONFIRM_OK:
                  return u"CONFIRM_OK";
                case CONFIRM_FAILED:
                  return u"CONFIRM_FAILED";
                case CONFIRM_TRYING_FAILED:
                  return u"CONFIRM_TRYING_FAILED";
                case CONFIRM_DISABLED:
                  return u"CONFIRM_DISABLED";
              }
              MOZ_ASSERT_UNREACHABLE();
              return u"";
            };

            obs->NotifyObservers(nullptr, "network:trr-confirmation",
                                 stateString(state));
          }
        }));
  }

  if (XRE_IsParentProcess()) {
    return;
  }

  MOZ_ASSERT(XRE_IsSocketProcess());
  MOZ_ASSERT(NS_IsMainThread());

  TRRServiceChild* child = TRRServiceChild::GetSingleton();
  if (child && child->CanSend()) {
    LOG(("TRRService::SendSetConfirmationState"));
    (void)child->SendSetConfirmationState(mState);
  }
}

bool TRRService::ConfirmationContext::HandleEvent(ConfirmationEvent aEvent) {
  MutexAutoLock lock(OwningObject()->mLock);
  return HandleEvent(aEvent, lock);
}

// We're protected by service->mLock
bool TRRService::ConfirmationContext::HandleEvent(ConfirmationEvent aEvent,
                                                  const MutexAutoLock&) {
  auto prevAddr = TaskAddr();
  TRRService* service = OwningObject();
  service->mLock.AssertCurrentThreadOwns();
  nsIDNSService::ResolverMode mode = service->Mode();

  auto resetConfirmation = [&]() {
    service->mLock.AssertCurrentThreadOwns();
    mTask = nullptr;
    nsCOMPtr<nsITimer> timer = std::move(mTimer);
    if (timer) {
      timer->Cancel();
    }

    mRetryInterval = StaticPrefs::network_trr_retry_timeout_ms();
    mTRRFailures = 0;

    if (TRR_DISABLED(mode)) {
      LOG(("TRR is disabled. mConfirmation.mState -> CONFIRM_OFF"));
      SetState(CONFIRM_OFF);
      return;
    }

    if (mode == nsIDNSService::MODE_TRRONLY) {
      LOG(("TRR_ONLY_MODE. mConfirmation.mState -> CONFIRM_DISABLED"));
      SetState(CONFIRM_DISABLED);
      return;
    }

    if (service->mConfirmationNS.Equals("skip"_ns)) {
      LOG((
          "mConfirmationNS == skip. mConfirmation.mState -> CONFIRM_DISABLED"));
      SetState(CONFIRM_DISABLED);
      return;
    }

    if (StaticPrefs::network_trr_start_confirmation_in_failed_state()) {
      // Don't optimistically assume TRR is usable. Starting in CONFIRM_FAILED
      // makes lookups fall back to native DNS until the first confirmation
      // succeeds, instead of blocking on the TRR connection being established.
      // The next call to maybeConfirm will transition to CONFIRM_TRYING_FAILED.
      LOG(("mConfirmation.mState -> CONFIRM_FAILED"));
      SetState(CONFIRM_FAILED);
      return;
    }

    // The next call to maybeConfirm will transition to CONFIRM_TRYING_OK
    LOG(("mConfirmation.mState -> CONFIRM_OK"));
    SetState(CONFIRM_OK);
  };

  auto maybeConfirm = [&](const char* aReason) {
    service->mLock.AssertCurrentThreadOwns();
    if (TRR_DISABLED(mode) || mState == CONFIRM_DISABLED || mTask) {
      LOG(
          ("TRRService:MaybeConfirm(%s) mode=%d, mTask=%p "
           "mState=%d\n",
           aReason, (int)mode, (void*)mTask, (int)mState));
      return;
    }

    MOZ_ASSERT(mode != nsIDNSService::MODE_TRRONLY,
               "Confirmation should be disabled");
    MOZ_ASSERT(!service->mConfirmationNS.Equals("skip"),
               "Confirmation should be disabled");

    LOG(("maybeConfirm(%s) starting confirmation test %s %s\n", aReason,
         service->mPrivateURI.get(), service->mConfirmationNS.get()));

    MOZ_ASSERT(mState == CONFIRM_OK || mState == CONFIRM_FAILED);

    if (mState == CONFIRM_FAILED) {
      LOG(("mConfirmation.mState -> CONFIRM_TRYING_FAILED"));
      SetState(CONFIRM_TRYING_FAILED);
    } else {
      LOG(("mConfirmation.mState -> CONFIRM_TRYING_OK"));
      SetState(CONFIRM_TRYING_OK);
    }

    nsCOMPtr<nsITimer> timer = std::move(mTimer);
    if (timer) {
      timer->Cancel();
    }

    MOZ_ASSERT(mode == nsIDNSService::MODE_TRRFIRST,
               "Should only confirm in TRR first mode");
    // Set aUseFreshConnection if TRR lookups are retried
    // or if confirmation already failed.
    mTask = new TRR(service, service->mConfirmationNS, TRRTYPE_NS, ""_ns, false,
                    mState == CONFIRM_TRYING_FAILED ||
                        StaticPrefs::network_trr_retry_on_recoverable_errors());
    mTask->SetTimeout(StaticPrefs::network_trr_confirmation_timeout_ms());
    mTask->SetPurpose(TRR::Confirmation);

    if (service->mLinkService) {
      service->mLinkService->GetNetworkID(mNetworkId);
    }

    if (mFirstRequestTime.IsNull()) {
      mFirstRequestTime = TimeStamp::Now();
    }
    if (mTrigger.IsEmpty()) {
      mTrigger.Assign(aReason);
    }

    LOG(("Dispatching confirmation task: %p", mTask.get()));
    service->DispatchTRRRequestInternal(mTask, false);
  };

  switch (aEvent) {
    case ConfirmationEvent::Init:
      resetConfirmation();
      maybeConfirm("context-init");
      break;
    case ConfirmationEvent::PrefChange:
      resetConfirmation();
      maybeConfirm("pref-change");
      break;
    case ConfirmationEvent::ConfirmationRetry:
      MOZ_ASSERT(mState == CONFIRM_FAILED);
      if (mState == CONFIRM_FAILED) {
        maybeConfirm("confirmation-retry");
      }
      break;
    case ConfirmationEvent::FailedLookups:
      MOZ_ASSERT(mState == CONFIRM_OK);
      mTrigger.Assign("failed-lookups");
      mFailedLookups = nsDependentCSubstring(
          mFailureReasons, mTRRFailures % ConfirmationContext::RESULTS_SIZE);
      maybeConfirm("failed-lookups");
      break;
    case ConfirmationEvent::RetryTRR:
      MOZ_ASSERT(mState == CONFIRM_OK);
      maybeConfirm("retry-trr");
      break;
    case ConfirmationEvent::URIChange:
      resetConfirmation();
      maybeConfirm("uri-change");
      break;
    case ConfirmationEvent::CaptivePortalConnectivity:
      // If we area already confirmed then we're fine.
      // If there is a confirmation in progress, likely it started before
      // we had full connectivity, so it may be hanging. We reset and try again.
      if (mState == CONFIRM_FAILED || mState == CONFIRM_TRYING_FAILED ||
          mState == CONFIRM_TRYING_OK) {
        resetConfirmation();
        maybeConfirm("cp-connectivity");
      }
      break;
    case ConfirmationEvent::NetworkUp:
      if (mState != CONFIRM_OK) {
        resetConfirmation();
        maybeConfirm("network-up");
      }
      break;
    case ConfirmationEvent::ConfirmOK:
      // Reset confirmation retry timeout to default
      mRetryInterval = StaticPrefs::network_trr_retry_timeout_ms();
      SetState(CONFIRM_OK);
      mTask = nullptr;
      break;
    case ConfirmationEvent::ConfirmFail:
      MOZ_ASSERT(mState == CONFIRM_TRYING_OK ||
                 mState == CONFIRM_TRYING_FAILED);
      SetState(CONFIRM_FAILED);
      mTask = nullptr;
      // retry failed NS confirmation
      LOG(("Setting timer to reconfirm %u", uint32_t(mRetryInterval)));
      NS_NewTimerWithCallback(getter_AddRefs(mTimer), this, mRetryInterval,
                              nsITimer::TYPE_ONE_SHOT);
      // double the interval up to this point
      mRetryInterval *= 2;
      if (mRetryInterval > StaticPrefs::network_trr_max_retry_timeout_ms()) {
        mRetryInterval = StaticPrefs::network_trr_max_retry_timeout_ms();
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected ConfirmationEvent");
  }

  return prevAddr != TaskAddr();
}

bool TRRService::MaybeBootstrap(const nsACString& aPossible,
                                nsACString& aResult) {
  MutexAutoLock lock(mLock);
  if (mMode == nsIDNSService::MODE_TRROFF || mBootstrapAddr.IsEmpty()) {
    return false;
  }

  nsCOMPtr<nsIURI> url;
  nsresult rv =
      NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
          .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_STANDARD,
                 443, mPrivateURI, nullptr, nullptr, nullptr)
          .Finalize(url);
  if (NS_FAILED(rv)) {
    LOG(("TRRService::MaybeBootstrap failed to create URI!\n"));
    return false;
  }

  nsAutoCString host;
  url->GetHost(host);
  if (!aPossible.Equals(host)) {
    return false;
  }
  LOG(("TRRService::MaybeBootstrap: use %s instead of %s\n",
       mBootstrapAddr.get(), host.get()));
  aResult = mBootstrapAddr;
  return true;
}

bool TRRService::IsDomainBlocked(const nsACString& aHost,
                                 const nsACString& aOriginSuffix,
                                 bool aPrivateBrowsing) {
  auto bl = mTRRBLStorage.Lock();
  if (bl->IsEmpty()) {
    return false;
  }

  // use a unified casing for the hashkey
  nsAutoCString hashkey(aHost + aOriginSuffix);
  if (auto val = bl->Lookup(hashkey)) {
    int32_t until =
        *val + int32_t(StaticPrefs::network_trr_temp_blocklist_duration_sec());
    int32_t expire = NowInSeconds();
    if (until > expire) {
      LOG(("Host [%s] is TRR blocklisted\n", PromiseFlatCString(aHost).get()));
      return true;
    }

    // the blocklisted entry has expired
    val.Remove();
  }
  return false;
}

// When running in TRR-only mode, the blocklist is not used and it will also
// try resolving the localhost / .local names.
bool TRRService::IsTemporarilyBlocked(const nsACString& aHost,
                                      const nsACString& aOriginSuffix,
                                      bool aPrivateBrowsing,
                                      bool aParentsToo)  // false if domain
{
  if (!StaticPrefs::network_trr_temp_blocklist()) {
    LOG(("TRRService::IsTemporarilyBlocked temp blocklist disabled by pref"));
    return false;
  }

  if (mMode == nsIDNSService::MODE_TRRONLY) {
    return false;  // might as well try
  }

  LOG(("Checking if host [%s] is blocklisted",
       nsPromiseFlatCString(aHost).get()));

  int32_t dot = aHost.FindChar('.');
  if ((dot == kNotFound) && aParentsToo) {
    // Only if a full host name. Domains can be dotless to be able to
    // blocklist entire TLDs
    return true;
  }

  if (IsDomainBlocked(aHost, aOriginSuffix, aPrivateBrowsing)) {
    return true;
  }

  nsDependentCSubstring domain = Substring(aHost, 0);
  while (dot != kNotFound) {
    dot++;
    domain.Rebind(domain, dot, domain.Length() - dot);

    if (IsDomainBlocked(domain, aOriginSuffix, aPrivateBrowsing)) {
      return true;
    }

    dot = domain.FindChar('.');
  }

  return false;
}

bool TRRService::IsExcludedFromTRR(const nsACString& aHost,
                                   nsIRequest::TRRMode aRequestMode) {
  // This method may be called off the main thread. We need to lock so
  // mExcludedDomains and mDNSSuffixDomains don't change while this code
  // is running.
  MutexAutoLock lock(mLock);

  return IsExcludedFromTRR_unlocked(aHost, aRequestMode);
}

bool TRRService::IsExcludedFromTRR_unlocked(const nsACString& aHost,
                                            nsIRequest::TRRMode aRequestMode) {
  // The effective resolver mode for this lookup is TRR-only when either the
  // request explicitly asked for TRR_ONLY_MODE, or the request doesn't
  // override the mode and the global mode is MODE_TRRONLY.
  const bool trrOnly = aRequestMode == nsIRequest::TRR_ONLY_MODE ||
                       (aRequestMode == nsIRequest::TRR_DEFAULT_MODE &&
                        mMode == nsIDNSService::MODE_TRRONLY);
  const bool checkDNSSuffix =
      !trrOnly || StaticPrefs::network_trr_exclude_dns_suffix_in_mode_trronly();

  int32_t dot = 0;
  // iteratively check the sub-domain of |aHost|
  while (dot < static_cast<int32_t>(aHost.Length())) {
    nsDependentCSubstring subdomain =
        Substring(aHost, dot, aHost.Length() - dot);

    if (mExcludedDomains.Contains(subdomain)) {
      LOG(("Subdomain [%s] of host [%s] Is Excluded From TRR via pref\n",
           nsPromiseFlatCString(subdomain).get(),
           nsPromiseFlatCString(aHost).get()));
      return true;
    }
    if (checkDNSSuffix && mDNSSuffixDomains.Contains(subdomain)) {
      LOG(
          ("Subdomain [%s] of host [%s] Is Excluded From TRR via DNSSuffix "
           "domains\n",
           nsPromiseFlatCString(subdomain).get(),
           nsPromiseFlatCString(aHost).get()));
      return true;
    }
    if (mEtcHostsDomains.Contains(subdomain)) {
      LOG(("Subdomain [%s] of host [%s] Is Excluded From TRR by /etc/hosts\n",
           nsPromiseFlatCString(subdomain).get(),
           nsPromiseFlatCString(aHost).get()));
      return true;
    }

    dot = aHost.FindChar('.', dot + 1);
    if (dot == kNotFound) {
      break;
    }
    dot++;
  }

  return false;
}

void TRRService::AddToBlocklist(const nsACString& aHost,
                                const nsACString& aOriginSuffix,
                                bool privateBrowsing, bool aParentsToo) {
  if (!StaticPrefs::network_trr_temp_blocklist()) {
    LOG(("TRRService::AddToBlocklist temp blocklist disabled by pref"));
    return;
  }

  LOG(("TRR blocklist %s\n", PromiseFlatCString(aHost).get()));
  nsAutoCString hashkey(aHost + aOriginSuffix);

  // this overwrites any existing entry
  {
    auto bl = mTRRBLStorage.Lock();
    bl->InsertOrUpdate(hashkey, NowInSeconds());
  }

  // See bug 1700405. Some test expects 15 trr consecutive failures, but the NS
  // check against the base domain is successful. So, we skip this NS check when
  // the pref said so in order to pass the test reliably.
  if (aParentsToo && !StaticPrefs::network_trr_skip_check_for_blocked_host()) {
    // when given a full host name, verify its domain as well
    int32_t dot = aHost.FindChar('.');
    if (dot != kNotFound) {
      // this has a domain to be checked
      dot++;
      nsDependentCSubstring domain =
          Substring(aHost, dot, aHost.Length() - dot);
      nsAutoCString check(domain);
      if (IsTemporarilyBlocked(check, aOriginSuffix, privateBrowsing, false)) {
        // the domain part is already blocklisted, no need to add this entry
        return;
      }
      // verify 'check' over TRR
      LOG(("TRR: verify if '%s' resolves as NS\n", check.get()));

      // check if there's an NS entry for this name
      RefPtr<TRR> trr = new TRR(this, check, TRRTYPE_NS, aOriginSuffix,
                                privateBrowsing, false);
      trr->SetPurpose(TRR::Blocklist);
      DispatchTRRRequest(trr);
    }
  }
}

NS_IMETHODIMP
TRRService::ConfirmationContext::Notify(nsITimer* aTimer) {
  MutexAutoLock lock(OwningObject()->mLock);
  if (aTimer == mTimer) {
    HandleEvent(ConfirmationEvent::ConfirmationRetry, lock);
  }

  return NS_OK;
}

NS_IMETHODIMP
TRRService::ConfirmationContext::GetName(nsACString& aName) {
  aName.AssignLiteral("TRRService::ConfirmationContext");
  return NS_OK;
}

static char StatusToChar(nsresult aLookupStatus, nsresult aChannelStatus) {
  // If the resolution fails in the TRR channel then we'll have a failed
  // aChannelStatus. Otherwise, we parse the response - if it's not a valid DNS
  // packet or doesn't contain the correct responses aLookupStatus will be a
  // failure code.
  if (aChannelStatus == NS_OK) {
    // Return + if confirmation was OK, or - if confirmation failed
    return aLookupStatus == NS_OK ? '+' : '-';
  }

  if (nsCOMPtr<nsIIOService> ios = do_GetIOService()) {
    bool hasConnectiviy = true;
    ios->GetConnectivity(&hasConnectiviy);
    if (!hasConnectiviy) {
      // Browser has no active network interfaces = is offline.
      return 'o';
    }
  }

  switch (aChannelStatus) {
    case NS_ERROR_NET_TIMEOUT_EXTERNAL:
      // TRR timeout expired
      return 't';
    case NS_ERROR_UNKNOWN_HOST:
      // TRRServiceChannel failed to due to unresolved host
      return 'd';
    default:
      break;
  }

  // The error is a network error
  if (NS_ERROR_GET_MODULE(aChannelStatus) == NS_ERROR_MODULE_NETWORK) {
    return 'n';
  }

  // Some other kind of failure.
  return '?';
}

void TRRService::RetryTRRConfirm() {
  if (mConfirmation.State() == CONFIRM_OK) {
    LOG(("TRRService::RetryTRRConfirm triggering confirmation"));
    mConfirmation.HandleEvent(ConfirmationEvent::RetryTRR);
  }
}

void TRRService::RecordTRRStatus(TRR* aTrrRequest) {
  MOZ_ASSERT_IF(XRE_IsParentProcess(), NS_IsMainThread() || IsOnTRRThread());
  MOZ_ASSERT_IF(XRE_IsSocketProcess(), NS_IsMainThread());

  nsresult channelStatus = aTrrRequest->ChannelStatus();

  glean::dns::trr_success
      .Get(ProviderKey(),
           NS_SUCCEEDED(channelStatus)
               ? "Fine"_ns
               : (channelStatus == NS_ERROR_NET_TIMEOUT_EXTERNAL ? "Timeout"_ns
                                                                 : "Bad"_ns))
      .Add();
  mConfirmation.RecordTRRStatus(aTrrRequest);
}

void TRRService::ConfirmationContext::RecordTRRStatus(TRR* aTrrRequest) {
  nsresult channelStatus = aTrrRequest->ChannelStatus();

  if (OwningObject()->Mode() == nsIDNSService::MODE_TRRONLY) {
    mLastConfirmationSkipReason = aTrrRequest->SkipReason();
    mLastConfirmationStatus = channelStatus;
  }

  if (NS_SUCCEEDED(channelStatus)) {
    LOG(("TRRService::RecordTRRStatus channel success"));
    mTRRFailures = 0;
    return;
  }

  if (OwningObject()->Mode() != nsIDNSService::MODE_TRRFIRST) {
    return;
  }

  // only count failures while in OK state
  if (State() != CONFIRM_OK) {
    return;
  }

  // When TRR retry is enabled, nsHostResolver will trigger Confirmation
  // immediately upon a lookup failure, so nothing to be done here.
  // nsHostResolver can assess the success of the lookup considering all the
  // involved results (A, AAAA) so we let it tell us when to re-Confirm.
  if (StaticPrefs::network_trr_retry_on_recoverable_errors()) {
    LOG(("TRRService not counting failures when retry is enabled"));
    return;
  }

  mFailureReasons[mTRRFailures % ConfirmationContext::RESULTS_SIZE] =
      StatusToChar(NS_OK, channelStatus);
  uint32_t fails = ++mTRRFailures;
  LOG(("TRRService::RecordTRRStatus fails=%u", fails));

  if (fails >= StaticPrefs::network_trr_max_fails()) {
    LOG(("TRRService had %u failures in a row\n", fails));
    // When several failures occur we trigger a confirmation causing
    // us to transition into the CONFIRM_TRYING_OK state.
    // Only after the confirmation fails do we finally go into CONFIRM_FAILED
    // and start skipping TRR.

    // Trigger a confirmation immediately.
    // If it fails, it will fire off a timer to start retrying again.
    HandleEvent(ConfirmationEvent::FailedLookups);
  }
}

void TRRService::ConfirmationContext::RecordEvent(const char* aReason,
                                                  const MutexAutoLock&) {
  // Reset the confirmation context attributes
  // Only resets the attributes that we keep for telemetry purposes.
  auto reset = [&]() {
    mAttemptCount = 0;
    mNetworkId.Truncate();
    mFirstRequestTime = TimeStamp();
    mContextChangeReason.Assign(aReason);
    mTrigger.Truncate();
    mFailedLookups.Truncate();

    mRetryInterval = StaticPrefs::network_trr_retry_timeout_ms();
  };

  if (mAttemptCount == 0) {
    // XXX: resetting everything might not be the best thing here, even if the
    // context changes, because there might still be a confirmation pending.
    // But cancelling and retrying that confirmation might just make the whole
    // confirmation longer for no reason.
    reset();
    return;
  }

  nsAutoCString results;
  static_assert(RESULTS_SIZE < 64);

  // mResults is a circular buffer ending at mAttemptCount
  if (mAttemptCount <= RESULTS_SIZE) {
    // We have fewer attempts than the size of the buffer, so all of the
    // results are in the buffer.
    results.Append(nsDependentCSubstring(mResults, mAttemptCount));
  } else {
    // More attempts than the buffer size.
    // That means past RESULTS_SIZE attempts in order are
    // [posInResults .. end-of-buffer) + [start-of-buffer .. posInResults)
    uint32_t posInResults = mAttemptCount % RESULTS_SIZE;

    results.Append(nsDependentCSubstring(mResults + posInResults,
                                         RESULTS_SIZE - posInResults));
    results.Append(nsDependentCSubstring(mResults, posInResults));
  }

  glean::network_dns::TrrConfirmationContextExtra extra = {
      .attemptcount = Some(mAttemptCount),
      .captiveportal = Some(nsPrintfCString("%i", mCaptivePortalStatus)),
      .contextreason = Some(mContextChangeReason),
      .failedlookups = mTrigger.Equals("failed-lookups"_ns)
                           ? Some(mFailedLookups)
                           : Nothing(),
      .networkid = Some(mNetworkId),
      .results = Some(results),
      .time = Some(nsPrintfCString(
          "%f", !mFirstRequestTime.IsNull()
                    ? (TimeStamp::Now() - mFirstRequestTime).ToMilliseconds()
                    : 0.0)),
      .trigger = Some(mTrigger),
      .value = Some(mState),
  };
  glean::network_dns::trr_confirmation_context.Record(Some(extra));

  reset();
}

void TRRService::ConfirmationContext::RequestCompleted(
    nsresult aLookupStatus, nsresult aChannelStatus) {
  mResults[mAttemptCount % RESULTS_SIZE] =
      StatusToChar(aLookupStatus, aChannelStatus);
  mAttemptCount++;
}

void TRRService::ConfirmationContext::CompleteConfirmation(nsresult aStatus,
                                                           TRR* aTRRRequest) {
  bool confirmOK;
  {
    MutexAutoLock lock(OwningObject()->mLock);
    // Ignore confirmations that dont match the pending task.
    if (mTask != aTRRRequest) {
      return;
    }
    MOZ_ASSERT(State() == CONFIRM_TRYING_OK ||
               State() == CONFIRM_TRYING_FAILED);
    if (State() != CONFIRM_TRYING_OK && State() != CONFIRM_TRYING_FAILED) {
      return;
    }

    RequestCompleted(aStatus, aTRRRequest->ChannelStatus());
    mLastConfirmationSkipReason = aTRRRequest->SkipReason();
    mLastConfirmationStatus = aTRRRequest->ChannelStatus();

    MOZ_ASSERT(mTask);
    if (NS_SUCCEEDED(aStatus)) {
      profiler_add_marker("TRR Confirmation Success",
                          geckoprofiler::category::NETWORK);
      HandleEvent(ConfirmationEvent::ConfirmOK, lock);
    } else {
      profiler_add_marker("TRR Confirmation Failure",
                          geckoprofiler::category::NETWORK);
      HandleEvent(ConfirmationEvent::ConfirmFail, lock);
    }

    // Capture the final state while still holding the lock. The retry timer
    // set by ConfirmFail can fire as soon as the lock is released and advance
    // the state from CONFIRM_FAILED to CONFIRM_TRYING_FAILED, so we must not
    // read State() after the lock drops.
    confirmOK = (State() == CONFIRM_OK);
    if (confirmOK) {
      // Record event and start new confirmation context
      RecordEvent("success", lock);
    }
    LOG(("TRRService finishing confirmation test %s %d %X\n",
         OwningObject()->mPrivateURI.get(), State(), (unsigned int)aStatus));
  }

  if (confirmOK) {
    // A fresh confirmation means previous blocked entries might not
    // be valid anymore.
    auto bl = OwningObject()->mTRRBLStorage.Lock();
    bl->Clear();
  }

  glean::dns::trr_ns_verfified
      .Get(TRRService::ProviderKey(), confirmOK ? "true"_ns : "false"_ns)
      .Add();
}

AHostResolver::LookupStatus TRRService::CompleteLookup(
    nsHostRecord* rec, nsresult status, AddrInfo* aNewRRSet, bool pb,
    const nsACString& aOriginSuffix, TRRSkippedReason aReason,
    TRR* aTRRRequest) {
  // this is an NS check for the TRR blocklist or confirmationNS check

  MOZ_ASSERT_IF(XRE_IsParentProcess(), NS_IsMainThread() || IsOnTRRThread());
  MOZ_ASSERT_IF(XRE_IsSocketProcess(), NS_IsMainThread());
  MOZ_ASSERT(!rec);

  RefPtr<AddrInfo> newRRSet(aNewRRSet);
  MOZ_ASSERT(newRRSet && newRRSet->TRRType() == TRRTYPE_NS);

  if (aTRRRequest->Purpose() == TRR::Confirmation) {
    mConfirmation.CompleteConfirmation(status, aTRRRequest);
    return LOOKUP_OK;
  }

  if (aTRRRequest->Purpose() == TRR::Blocklist) {
    if (NS_SUCCEEDED(status)) {
      LOG(("TRR verified %s to be fine!\n", newRRSet->Hostname().get()));
    } else {
      LOG(("TRR says %s doesn't resolve as NS!\n", newRRSet->Hostname().get()));
      AddToBlocklist(newRRSet->Hostname(), aOriginSuffix, pb, false);
    }
    return LOOKUP_OK;
  }

  MOZ_ASSERT_UNREACHABLE(
      "TRRService::CompleteLookup called for unexpected request");
  return LOOKUP_OK;
}

AHostResolver::LookupStatus TRRService::CompleteLookupByType(
    nsHostRecord*, nsresult, mozilla::net::TypeRecordResultType& aResult,
    mozilla::net::TRRSkippedReason aReason, uint32_t aTtl, bool aPb) {
  return LOOKUP_OK;
}

NS_IMETHODIMP TRRService::OnProxyConfigChanged() {
  LOG(("TRRService::OnProxyConfigChanged"));

  nsAutoCString uri;
  GetURI(uri);
  AsyncCreateTRRConnectionInfo(uri);

  return NS_OK;
}

void TRRService::InitTRRConnectionInfo(bool aForceReinit) {
  if (XRE_IsParentProcess()) {
    TRRServiceBase::InitTRRConnectionInfo(aForceReinit);
    return;
  }

  MOZ_ASSERT(XRE_IsSocketProcess());
  MOZ_ASSERT(NS_IsMainThread());

  TRRServiceChild* child = TRRServiceChild::GetSingleton();
  if (child && child->CanSend()) {
    LOG(("TRRService::SendInitTRRConnectionInfo"));
    (void)child->SendInitTRRConnectionInfo(aForceReinit);
  }
}

}  // namespace mozilla::net
