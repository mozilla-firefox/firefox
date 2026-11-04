/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingProtection.h"
#include "BounceTrackingState.h"
#include "BounceTrackingRecord.h"
#include "ProfileAfterChangeGate.h"

#include "ErrorList.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/BrowsingContextWebProgress.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIBrowser.h"
#include "nsIChannel.h"
#include "nsIEffectiveTLDService.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsIURI.h"
#include "nsIWebProgressListener.h"
#include "nsIPrincipal.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/ClearOnShutdown.h"
#include "nsTHashMap.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/WindowContext.h"

namespace mozilla {

// Global map: browserId -> BounceTrackingState
static StaticAutoPtr<nsTHashMap<uint64_t, WeakPtr<BounceTrackingState>>>
    sBounceTrackingStates;

NS_IMPL_ISUPPORTS(BounceTrackingState, nsIWebProgressListener,
                  nsISupportsWeakReference);

BounceTrackingState::BounceTrackingState() {
  MOZ_ASSERT(StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                 nsIBounceTrackingProtection::MODE_ENABLED ||
             StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                 nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN);
  mBounceTrackingProtection = BounceTrackingProtection::GetSingleton();
};

BounceTrackingState::~BounceTrackingState() {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Verbose,
              "BounceTrackingState destructor");

  if (sBounceTrackingStates) {
    sBounceTrackingStates->Remove(mBrowserId);
  }
  if (mClientBounceDetectionTimeout) {
    mClientBounceDetectionTimeout->Cancel();
    mClientBounceDetectionTimeout = nullptr;
  }
}

// static
already_AddRefed<BounceTrackingState> BounceTrackingState::GetOrCreate(
    dom::BrowsingContextWebProgress* aWebProgress, nsresult& aRv) {
  aRv = NS_OK;

  // Do not init before profile-after-change.
  nsresult rv = EnsurePastProfileAfterChange();
  if (NS_FAILED(rv)) {
    aRv = rv;
    return nullptr;
  }

  if (!aWebProgress) {
    aRv = NS_ERROR_INVALID_ARG;
    return nullptr;
  }

  // Check if we should even apply BTP for this environment. This depends on the
  // feature prefs and the web progress itself.
  if (!ShouldCreateBounceTrackingStateForWebProgress(aWebProgress)) {
    // The call below ensures we record the disabled state in telemetry. In
    // MODE_DISABLED we may never hit BounceTrackingProtection::GetSingleton and
    // thus would skip on recording telemetry.
    if (StaticPrefs::privacy_bounceTrackingProtection_mode() ==
        nsIBounceTrackingProtection::MODE_DISABLED) {
      BounceTrackingProtection::RecordModePrefTelemetry();
    }
    return nullptr;
  }

  dom::BrowsingContext* browsingContext = aWebProgress->GetBrowsingContext();
  if (!browsingContext) {
    return nullptr;
  }
  uint64_t browserId = browsingContext->BrowserId();

  // Check if there is already a BounceTrackingState for the given browser.
  if (sBounceTrackingStates) {
    WeakPtr<BounceTrackingState> existingBTS =
        sBounceTrackingStates->Get(browserId);
    if (existingBTS) {
      // Return a strong reference.
      return do_AddRef(existingBTS.get());
    }
  }

  // Create a new BounceTracking state, initialize it and insert it into the
  // map, then return it to the caller.
  RefPtr<BounceTrackingState> newBTS = new BounceTrackingState();

  aRv = newBTS->Init(aWebProgress);
  if (NS_FAILED(aRv)) {
    NS_WARNING("Failed to initialize BounceTrackingState.");
    return nullptr;
  }

  // Now that we know that we'll keep this BounceTrackingState lazily initialize
  // the global map
  if (!sBounceTrackingStates) {
    sBounceTrackingStates =
        new nsTHashMap<nsUint64HashKey, WeakPtr<BounceTrackingState>>();
    ClearOnShutdown(&sBounceTrackingStates);
  }
  sBounceTrackingStates->InsertOrUpdate(browserId, newBTS);

  return newBTS.forget();
};

// static
void BounceTrackingState::ResetAll() { Reset(nullptr, nullptr); }

// static
void BounceTrackingState::DestroyAll() {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}",
              __FUNCTION__);
  if (!sBounceTrackingStates) {
    return;
  }

  // Fully reset all BounceTrackingStates, so even if some don't get destroyed
  // straight away things like running timers are stopped.
  BounceTrackingState::Reset(nullptr, nullptr);

  // Destroy all BounceTrackingState objects.
  for (auto iter = sBounceTrackingStates->Iter(); !iter.Done(); iter.Next()) {
    WeakPtr<BounceTrackingState> bts = iter.Data();
    // Need to remove the element from the map prior to calling Destroy()
    // because the destructor also updates the map and we can't iterate and
    // externally modify the map at the same time. This way the Remove() call of
    // the destructor is a no-op.
    iter.Remove();
    if (!bts) {
      continue;
    }
    // Destroy the BounceTrackingState by dropping references to it. This is
    // best effort. If something still holds a reference it still stay alive
    // longer.
    // Tell the web progress to drop the BTS reference.
    RefPtr<dom::BrowsingContext> browsingContext =
        bts->CurrentBrowsingContext();
    if (!browsingContext) {
      continue;
    }
    dom::BrowsingContextWebProgress* webProgress =
        browsingContext->Canonical()->GetWebProgress();
    if (!webProgress) {
      continue;
    }
    webProgress->DropBounceTrackingState();
  }

  // Clean up the map.
  sBounceTrackingStates = nullptr;
}

// static
void BounceTrackingState::ResetAllForOriginAttributes(
    const OriginAttributes& aOriginAttributes) {
  Reset(&aOriginAttributes, nullptr);
}

// static
void BounceTrackingState::ResetAllForOriginAttributesPattern(
    const OriginAttributesPattern& aPattern) {
  Reset(nullptr, &aPattern);
}

nsresult BounceTrackingState::Init(
    dom::BrowsingContextWebProgress* aWebProgress) {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "BounceTrackingState::{}", __FUNCTION__);

  MOZ_ASSERT(!mIsInitialized,
             "BounceTrackingState must not be initialized twice.");
  mIsInitialized = true;

  NS_ENSURE_ARG_POINTER(aWebProgress);
  NS_ENSURE_TRUE(StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                         nsIBounceTrackingProtection::MODE_ENABLED ||
                     StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                         nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN,
                 NS_ERROR_NOT_AVAILABLE);
  NS_ENSURE_TRUE(mBounceTrackingProtection, NS_ERROR_FAILURE);

  // Store the browser ID so we can get the associated BC later without having
  // to hold a reference to aWebProgress.
  dom::BrowsingContext* browsingContext = aWebProgress->GetBrowsingContext();
  NS_ENSURE_TRUE(browsingContext, NS_ERROR_FAILURE);
  mBrowserId = browsingContext->BrowserId();

  // Create a copy of the BC's OriginAttributes so we can use it later without
  // having to hold a reference to the BC.
  mOriginAttributes = browsingContext->OriginAttributesRef();
  // We don't need first party domain. Many things in BTP are keyed by
  // OriginAttributes and we don't want to create unecessary partitions.
  mOriginAttributes.mFirstPartyDomain.Truncate();

  MOZ_ASSERT(mOriginAttributes.mPartitionKey.IsEmpty(),
             "Top level BCs mus not have a partition key.");

  // Add a listener for window load. See BounceTrackingState::OnStateChange for
  // the listener code.
  return aWebProgress->AddProgressListener(this,
                                           nsIWebProgress::NOTIFY_STATE_WINDOW);
}

void BounceTrackingState::ResetBounceTrackingRecord() {
  mBounceTrackingRecord = nullptr;
}

void BounceTrackingState::OnBrowsingContextDiscarded() {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}",
              __FUNCTION__);
  // We are about to be destroyed because the tab closed. This marks the end of
  // the extended navigation (if any). Record stateful bounces.

  // No ongoing extended navigation, nothing to record.
  if (!mBounceTrackingRecord) {
    return;
  }

  MOZ_ASSERT(mBounceTrackingProtection);
  nsresult rv = mBounceTrackingProtection->RecordStatefulBounces(this);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to record stateful bounces on BrowsingContext discard.");
  }
}

BounceTrackingRecord* BounceTrackingState::GetBounceTrackingRecord() {
  return mBounceTrackingRecord;
}

// static
void BounceTrackingState::Reset(const OriginAttributes* aOriginAttributes,
                                const OriginAttributesPattern* aPattern) {
  if (aOriginAttributes || aPattern) {
    MOZ_ASSERT((aOriginAttributes != nullptr) != (aPattern != nullptr),
               "Must not pass both aOriginAttributes and aPattern.");
  }

  if (!sBounceTrackingStates) {
    return;
  }
  for (const WeakPtr<BounceTrackingState>& btsWeak :
       sBounceTrackingStates->Values()) {
    if (!btsWeak) {
      continue;
    }
    RefPtr<BounceTrackingState> bounceTrackingState(btsWeak);

    if ((aOriginAttributes &&
         *aOriginAttributes != bounceTrackingState->OriginAttributesRef()) ||
        (aPattern &&
         !aPattern->Matches(bounceTrackingState->OriginAttributesRef()))) {
      continue;
    }
    if (bounceTrackingState->mClientBounceDetectionTimeout) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: mClientBounceDetectionTimeout->Cancel()", __FUNCTION__);
      bounceTrackingState->mClientBounceDetectionTimeout->Cancel();
      bounceTrackingState->mClientBounceDetectionTimeout = nullptr;
    }
    bounceTrackingState->ResetBounceTrackingRecord();
  }
}

// static
bool BounceTrackingState::ShouldCreateBounceTrackingStateForWebProgress(
    dom::BrowsingContextWebProgress* aWebProgress) {
  NS_ENSURE_TRUE(aWebProgress, false);

  uint8_t mode = StaticPrefs::privacy_bounceTrackingProtection_mode();
  // Classification / purging is disabled.
  if (mode != nsIBounceTrackingProtection::MODE_ENABLED &&
      mode != nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN) {
    return false;
  }

  // Only keep track of top level content browsing contexts.
  dom::BrowsingContext* browsingContext = aWebProgress->GetBrowsingContext();
  if (!browsingContext || !browsingContext->IsTopContent()) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Verbose,
                "{}: Skip non top-content.", __FUNCTION__);
    return false;
  }

  bool isPrivate = browsingContext->UsePrivateBrowsing();
  uint32_t cookieBehavior = nsICookieManager::GetCookieBehavior(isPrivate);
  if (cookieBehavior == nsICookieService::BEHAVIOR_ACCEPT ||
      cookieBehavior == nsICookieService::BEHAVIOR_REJECT) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Verbose,
                "{}: Skip on cookie behavior {}", __FUNCTION__, cookieBehavior);
    return false;
  }

  return true;
}

// static
bool BounceTrackingState::ShouldTrackPrincipal(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  // Only track content principals.
  if (!aPrincipal->GetIsContentPrincipal()) {
    return false;
  }

  // Skip non http(s) schemes.
  if (!net::SchemeIsHttpOrHttps(aPrincipal)) {
    return false;
  }

  return true;
}

// static
nsresult BounceTrackingState::HasBounceTrackingStateForSite(
    const nsACString& aSiteHost, const OriginAttributes& aOriginAttributes,
    bool& aResult) {
  aResult = false;
  NS_ENSURE_TRUE(aSiteHost.Length(), NS_ERROR_FAILURE);

  if (!sBounceTrackingStates) {
    return NS_OK;
  }

  // Iterate over all browsing contexts which have a bounce tracking state. Use
  // the content principal base domain field to determine whether a BC has an
  // active site that matches aSiteHost.
  for (const WeakPtr<BounceTrackingState>& btsWeak :
       sBounceTrackingStates->Values()) {
    if (!btsWeak) {
      continue;
    }
    RefPtr<BounceTrackingState> state(btsWeak);

    // Skip BTS of unrelated OA. These are not relevant for the caller as their
    // state is isolated.
    if (state->mOriginAttributes != aOriginAttributes) {
      continue;
    }

    // For each active BTS get the current window's document principal.
    RefPtr<dom::BrowsingContext> browsingContext =
        state->CurrentBrowsingContext();

    if (!browsingContext || browsingContext->IsDiscarded() ||
        browsingContext->IsInBFCache()) {
      continue;
    }

    RefPtr<dom::WindowGlobalParent> currentWindow =
        browsingContext->Canonical()->GetCurrentWindowGlobal();
    if (!currentWindow) {
      continue;
    }

    nsCOMPtr<nsIPrincipal> principal = currentWindow->DocumentPrincipal();
    if (NS_WARN_IF(!principal)) {
      continue;
    }

    // This active-site purge guard filters by the cached OriginAttributes but
    // reads the live document principal. For track-worthy documents the two
    // must agree, otherwise the guard would protect (or fail to protect) the
    // wrong container's data. They only diverge if a tab's userContextId
    // changes during its lifetime. Documents BTP does not track are excluded:
    // the system principal is a process-wide singleton whose OriginAttributes
    // report the default (non-private) values regardless of the window, and
    // non-http(s) documents (e.g. about:, moz-extension:) are not partitioned
    // like the http(s) content BTP records bounces for. See Bug 2054941.
    MOZ_ASSERT(
        !ShouldTrackPrincipal(principal) ||
            principal->OriginAttributesRef().EqualsIgnoringFPD(
                state->mOriginAttributes),
        "BTP: active-site purge guard sees a live document whose container "
        "differs from the cached BounceTrackingState OriginAttributes (Bug "
        "2054941).");

    // Lastly, check if the site matches.
    nsAutoCString baseDomain;
    nsresult rv = principal->GetBaseDomain(baseDomain);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    if (aSiteHost.Equals(baseDomain)) {
      aResult = true;
      return NS_OK;
    }
  }

  return NS_OK;
}

already_AddRefed<dom::BrowsingContext>
BounceTrackingState::CurrentBrowsingContext() {
  MOZ_ASSERT(mBrowserId != 0);
  return dom::BrowsingContext::GetCurrentTopByBrowserId(mBrowserId);
}

const OriginAttributes& BounceTrackingState::OriginAttributesRef() {
  return mOriginAttributes;
}

nsresult BounceTrackingState::OnDocumentStartRequest(nsIChannel* aChannel) {
  NS_ENSURE_ARG_POINTER(aChannel);
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}",
              __FUNCTION__);

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = aChannel->GetLoadInfo(getter_AddRefs(loadInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  // For http(s) loads the channel we record bounces for must live in the same
  // storage partition as the BounceTrackingState which owns the per-tab record.
  // Non-http(s) channels (e.g. the initial about:blank) are excluded because
  // they do not necessarily carry the tab's OriginAttributes. See Bug 2054941.
  //
  // The container is left out of the comparison because a top level load can be
  // retargeted into a new tab in the container its site is bound to, splitting
  // one extended navigation across two tabs.
  //
  // TODO: Bug 2058145: BTP does not account for those transitions yet: hops
  // which ran in another container are recorded under this tab's, causing false
  // negatives and, more problematic, false positives. Bug 2059768 covers the
  // known cases with todo() expectations.
#ifdef DEBUG
  if (nsCOMPtr<nsIURI> channelURIForAssert;
      NS_SUCCEEDED(aChannel->GetURI(getter_AddRefs(channelURIForAssert))) &&
      channelURIForAssert &&
      mozilla::net::SchemeIsHttpOrHttps(channelURIForAssert)) {
    constexpr uint32_t kIgnoredForAssert =
        OriginAttributes::STRIP_FIRST_PARTY_DOMAIN |
        OriginAttributes::STRIP_PARTITION_KEY |
        OriginAttributes::STRIP_USER_CONTEXT_ID;

    OriginAttributes channelAttrsForAssert = loadInfo->GetOriginAttributes();
    channelAttrsForAssert.StripAttributes(kIgnoredForAssert);

    OriginAttributes stateAttrsForAssert = mOriginAttributes;
    stateAttrsForAssert.StripAttributes(kIgnoredForAssert);

    MOZ_ASSERT(channelAttrsForAssert == stateAttrsForAssert,
               "BTP: channel OriginAttributes (PBM) diverged from the cached "
               "BounceTrackingState OriginAttributes (Bug 2054941).");
  }
#endif

  // Used to keep track of whether we added entries to the site list that are
  // not "null".
  bool siteListIsEmpty = true;

  // Collect uri list including any redirects.
  nsTArray<nsCString> siteList;

  for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectHistoryEntry :
       loadInfo->RedirectChain()) {
    nsCOMPtr<nsIPrincipal> principal;
    rv = redirectHistoryEntry->GetPrincipal(getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!BounceTrackingState::ShouldTrackPrincipal(principal)) {
      siteList.AppendElement("null"_ns);
      continue;
    }

    nsAutoCString baseDomain;
    rv = principal->GetBaseDomain(baseDomain);
    NS_ENSURE_SUCCESS(rv, rv);

    if (NS_WARN_IF(baseDomain.IsEmpty())) {
      siteList.AppendElement("null");
    } else {
      siteList.AppendElement(baseDomain);
      siteListIsEmpty = false;
    }
  }

  // Add site via the current URI which is the end of the chain.
  nsCOMPtr<nsIURI> channelURI;
  rv = aChannel->GetURI(getter_AddRefs(channelURI));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mozilla::net::SchemeIsHttpOrHttps(channelURI)) {
    nsCOMPtr<nsIEffectiveTLDService> tldService =
        do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString siteHost;
    rv = tldService->GetSchemelessSite(channelURI, siteHost);

    // Skip URIs where we can't get a site host.
    if (NS_FAILED(rv)) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Failed to get site host from channelURI: {}",
                  __FUNCTION__, channelURI->GetSpecOrDefault());
      siteList.AppendElement("null"_ns);
    } else {
      MOZ_ASSERT(!siteHost.IsEmpty(), "siteHost should not be empty.");
      siteList.AppendElement(siteHost);
      siteListIsEmpty = false;
    }
  }

  // Do not record empty site lists. This can happen if none of the principals
  // are suitable for tracking. It includes when OnDocumentStartRequest is
  // called for the initial about:blank.
  if (siteListIsEmpty) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: skip empty site list.", __FUNCTION__);
    return NS_OK;
  }

  return OnResponseReceived(siteList);
}

// nsIWebProgressListener

NS_IMETHODIMP
BounceTrackingState::OnStateChange(nsIWebProgress* aWebProgress,
                                   nsIRequest* aRequest, uint32_t aStateFlags,
                                   nsresult aStatus) {
  NS_ENSURE_ARG_POINTER(aWebProgress);
  NS_ENSURE_ARG_POINTER(aRequest);

  bool isTopLevel = false;
  nsresult rv = aWebProgress->GetIsTopLevel(&isTopLevel);
  NS_ENSURE_SUCCESS(rv, rv);

  // Filter for top level loads.
  if (!isTopLevel) {
    return NS_OK;
  }

  // Filter for window loads.
  if (!(aStateFlags & nsIWebProgressListener::STATE_STOP) ||
      !(aStateFlags & nsIWebProgressListener::STATE_IS_WINDOW)) {
    return NS_OK;
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Verbose,
              "{}: Top level window load: aStateFlags: {}, aStatus: {:#x}",
              __PRETTY_FUNCTION__, aStateFlags, static_cast<uint32_t>(aStatus));

  // Discard failed loads. Those are not valid destinations.
  if (NS_FAILED(aStatus)) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Verbose,
                "{}: Discarding failed load. aStatus: {:#x}",
                __PRETTY_FUNCTION__, static_cast<uint32_t>(aStatus));
    return NS_OK;
  }

  // Get the document principal via the current window global.
  dom::BrowsingContext* browsingContext = aWebProgress->GetBrowsingContext();
  NS_ENSURE_TRUE(browsingContext, NS_ERROR_FAILURE);

  dom::WindowGlobalParent* windowGlobalParent =
      browsingContext->Canonical()->GetCurrentWindowGlobal();
  NS_ENSURE_TRUE(windowGlobalParent, NS_ERROR_FAILURE);

  nsCOMPtr<nsIPrincipal> documentPrincipal =
      windowGlobalParent->DocumentPrincipal();

  // For track-worthy documents the committed document must live in the same
  // container as the BounceTrackingState, i.e. the tab's userContextId has not
  // changed during its lifetime. This also catches a container change on paths
  // without a channel hook (e.g. bfcache / session restore). Documents BTP does
  // not track are excluded: the system principal is a process-wide singleton
  // whose OriginAttributes report the default (non-private) values regardless
  // of the window, and non-http(s) documents (e.g. about:, moz-extension:) are
  // not partitioned like the http(s) content BTP records bounces for. See the
  // note in OnDocumentStartRequest and Bug 2054941.
  MOZ_ASSERT(
      !documentPrincipal || !ShouldTrackPrincipal(documentPrincipal) ||
          documentPrincipal->OriginAttributesRef().EqualsIgnoringFPD(
              mOriginAttributes),
      "BTP: committed document OriginAttributes (userContextId/PBM) diverged "
      "from the cached BounceTrackingState OriginAttributes (Bug 2054941).");

  // OnBFCacheRestored already handled the restore synchronously with the swap.
  // This notification arrives later, possibly after the restored page started
  // another navigation, whose record it must not overwrite the final host of.
  if (aStateFlags & nsIWebProgressListener::STATE_RESTORING) {
    return NS_OK;
  }

  return OnDocumentLoaded(documentPrincipal);
}

nsresult BounceTrackingState::OnBFCacheRestored() {
  RefPtr<dom::BrowsingContext> browsingContext = CurrentBrowsingContext();
  NS_ENSURE_TRUE(browsingContext, NS_ERROR_FAILURE);

  dom::WindowGlobalParent* windowGlobalParent =
      browsingContext->Canonical()->GetCurrentWindowGlobal();
  NS_ENSURE_TRUE(windowGlobalParent, NS_ERROR_FAILURE);

  nsCOMPtr<nsIPrincipal> documentPrincipal =
      windowGlobalParent->DocumentPrincipal();
  NS_ENSURE_TRUE(documentPrincipal, NS_ERROR_FAILURE);

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: this: {}",
              __FUNCTION__, *this);

  if (ShouldTrackPrincipal(documentPrincipal)) {
    nsAutoCString siteHost;
    nsresult rv = documentPrincipal->GetBaseDomain(siteHost);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!siteHost.IsEmpty()) {
      rv = OnResponseReceived(AutoTArray<nsCString, 1>{std::move(siteHost)});
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return OnDocumentLoaded(documentPrincipal);
}

NS_IMETHODIMP
BounceTrackingState::OnProgressChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* aRequest,
                                      int32_t aCurSelfProgress,
                                      int32_t aMaxSelfProgress,
                                      int32_t aCurTotalProgress,
                                      int32_t aMaxTotalProgress) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingState::OnLocationChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* aRequest, nsIURI* aLocation,
                                      uint32_t aFlags) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingState::OnStatusChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest, nsresult aStatus,
                                    const char16_t* aMessage) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingState::OnSecurityChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* aRequest, uint32_t aState) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingState::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                            nsIRequest* aRequest,
                                            uint32_t aEvent) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

// Site host of aWindowContext's top level document. False if that document is
// no longer the current one or is not a principal we track.
static bool GetTopLevelSiteHost(dom::WindowContext* aWindowContext,
                                nsACString& aSiteHost) {
  if (!aWindowContext) {
    return false;
  }
  dom::WindowContext* topWindowContext = aWindowContext->TopWindowContext();
  if (!topWindowContext || !topWindowContext->IsCurrent()) {
    return false;
  }

  nsIPrincipal* principal = topWindowContext->Canonical()->DocumentPrincipal();
  if (!principal || !BounceTrackingState::ShouldTrackPrincipal(principal)) {
    return false;
  }

  nsAutoCString siteHost;
  if (NS_WARN_IF(NS_FAILED(principal->GetBaseDomain(siteHost)))) {
    return false;
  }

  aSiteHost = siteHost;
  return true;
}

nsresult BounceTrackingState::OnStartNavigation(
    nsIPrincipal* aTriggeringPrincipal,
    const bool aHasValidUserGestureActivation, uint64_t aLoadId) {
  NS_ENSURE_ARG_POINTER(aTriggeringPrincipal);

  // A single logical navigation can reach this method more than once: it may go
  // through a speculative parent load, switch process, or get re-opened, and
  // each of these carries the same load id. Only advance the state machine for
  // the first call of a given load id, otherwise we would double count and
  // corrupt the extended navigation.
  if (aLoadId != 0 && mLastStartedLoadId == Some(aLoadId)) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: skipping duplicate call for load id {}", __FUNCTION__,
                aLoadId);
    return NS_OK;
  }
  mLastStartedLoadId = Some(aLoadId);

  // Logging
  if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
    nsAutoCString origin;
    nsresult rv = aTriggeringPrincipal->GetOrigin(origin);
    if (NS_FAILED(rv)) {
      origin = "err";
    }
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: origin: {}, mBounceTrackingRecord: {}", __FUNCTION__,
                origin, mBounceTrackingRecord);
  }

  // Remove any queued global tasks to record stateful bounces for bounce
  // tracking from the networking task source.
  if (mClientBounceDetectionTimeout) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: mClientBounceDetectionTimeout->Cancel()", __FUNCTION__);
    mClientBounceDetectionTimeout->Cancel();
    mClientBounceDetectionTimeout = nullptr;
  }

  // The initiator's (schemeless) site, for the user activation set.
  nsAutoCString siteHost;

  // If origin is an opaque origin, set siteHost to empty host. Strictly
  // speaking we only need to check IsNullPrincipal, but we're generally only
  // interested in content principals with http/s scheme. Other principal types
  // or schemes are not considered to be trackers.
  if (!BounceTrackingState::ShouldTrackPrincipal(aTriggeringPrincipal)) {
    siteHost = "";
  } else {
    // obtain site
    nsresult rv = aTriggeringPrincipal->GetBaseDomain(siteHost);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      siteHost = "";
    }
  }

  // The site this context is leaving, not the initiator's, which can be a frame
  // or another tab. Read at commit time. Empty if nothing is committed yet.
  nsAutoCString departingSiteHost;
  // RecordStatefulBounces exempts the initial host, so a frame which navigates
  // the top level must not name itself. A context with no committed document
  // was opened by this navigation; use its opener, not the initiator, which can
  // resolve back into it and name the frame again.
  nsAutoCString initialSiteHost;
  if (RefPtr<dom::BrowsingContext> browsingContext = CurrentBrowsingContext()) {
    if (browsingContext->GetHasLoadedNonInitialDocument()) {
      GetTopLevelSiteHost(browsingContext->GetCurrentWindowContext(),
                          departingSiteHost);
      initialSiteHost = departingSiteHost;
    } else if (RefPtr<dom::BrowsingContext> opener =
                   browsingContext->GetOpener()) {
      GetTopLevelSiteHost(opener->GetCurrentWindowContext(), initialSiteHost);
    }
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: siteHost: {}, departingSiteHost: {}, initialSiteHost: {}",
              __FUNCTION__, siteHost, departingSiteHost, initialSiteHost);

  // If sourceSnapshotParams’s has transient activation is true,
  // we initialize a new bounce tracking record with the initialHost
  // having been activated. Also treat system principal navigation as
  // having user interaction.
  bool hasUserActivation = aHasValidUserGestureActivation ||
                           aTriggeringPrincipal->IsSystemPrincipal();

  // If navigable’s bounce tracking record is null: Set navigable’s bounce
  // tracking record to a new bounce tracking record with initial host set to
  // initialHost.
  if (!mBounceTrackingRecord) {
    mBounceTrackingRecord = MakeRefPtr<BounceTrackingRecord>();
    mBounceTrackingRecord->SetInitialHost(initialSiteHost);
    if (hasUserActivation) {
      mBounceTrackingRecord->AddUserActivationHost(siteHost);
    }

    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: new BounceTrackingRecord(): {}", __FUNCTION__,
                mBounceTrackingRecord);

    return NS_OK;
  }

  // If sourceSnapshotParams’s has transient activation is true: The user
  // activation ends the extended navigation. Process the bounce candidates.
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: site: {}, hasUserActivation? {}", __FUNCTION__, siteHost,
              hasUserActivation);
  if (hasUserActivation) {
    nsresult rv = mBounceTrackingProtection->RecordStatefulBounces(this);
    NS_ENSURE_SUCCESS(rv, rv);

    MOZ_ASSERT(!mBounceTrackingRecord);
    mBounceTrackingRecord = MakeRefPtr<BounceTrackingRecord>();
    mBounceTrackingRecord->SetInitialHost(initialSiteHost);
    // Not initialSiteHost: the user activation set feeds
    // DynamicFpiNavigationHeuristic, which wants the site interacted with.
    mBounceTrackingRecord->AddUserActivationHost(siteHost);

    return NS_OK;
  }

  // There is no transient user activation. Add the site being left as a bounce
  // candidate. Not initialSiteHost: its opener fallback would name the
  // initiating tab again.
  if (departingSiteHost.IsEmpty()) {
    mBounceTrackingRecord->AddBounceHost("null"_ns);
  } else {
    mBounceTrackingRecord->AddBounceHost(departingSiteHost);
  }

  return NS_OK;
}

// Private

nsresult BounceTrackingState::OnResponseReceived(
    const nsTArray<nsCString>& aSiteList) {
#ifdef DEBUG
  MOZ_ASSERT(!aSiteList.IsEmpty(), "siteList should not be empty.");
  for (const nsCString& site : aSiteList) {
    MOZ_ASSERT(!site.IsEmpty(), "site should not be an empty string.");
  }
#endif

  // Logging
  if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
    nsAutoCString siteListStr;

    for (const nsACString& site : aSiteList) {
      siteListStr.Append(site);
      siteListStr.AppendLiteral(", ");
    }

    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: #{} siteList: {}", __FUNCTION__, siteListStr.Length(),
                siteListStr);
  }

  // Record should exist by now. It gets created in OnStartNavigation which
  // runs for every top level document load before the response is received.
  if (NS_WARN_IF(!mBounceTrackingRecord)) {
    return NS_ERROR_FAILURE;
  }

  // Check if there is still an active timeout. This shouldn't happen since
  // OnStartNavigation already cancels it.
  if (mClientBounceDetectionTimeout) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: mClientBounceDetectionTimeout->Cancel()", __FUNCTION__);
    mClientBounceDetectionTimeout->Cancel();
    mClientBounceDetectionTimeout = nullptr;
  }

  // Run steps after a timeout: queue a global task on the networking task
  // source with global to record stateful bounces for bounce.
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Scheduling mClientBounceDetectionTimeout", __FUNCTION__);

  // Use a weak reference to this to avoid keeping the object alive if the tab
  // is closed during the timeout.
  WeakPtr<BounceTrackingState> thisWeak = this;
  nsresult rv = NS_NewTimerWithCallback(
      getter_AddRefs(mClientBounceDetectionTimeout),
      [thisWeak](auto) {
        if (!thisWeak) {
          MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                      "{}: !thisWeak", __FUNCTION__);
          return;
        }
        MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                    "{}: Calling RecordStatefulBounces after timeout.",
                    __FUNCTION__);

        BounceTrackingState* bounceTrackingState = thisWeak;
        DebugOnly<nsresult> rv =
            bounceTrackingState->mBounceTrackingProtection
                ->RecordStatefulBounces(bounceTrackingState);
#ifdef DEBUG
        if (NS_FAILED(rv)) {
          MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                      "Running RecordStatefulBounces after a timeout failed.");
        }
#endif

        bounceTrackingState->mClientBounceDetectionTimeout = nullptr;
      },
      StaticPrefs::
          privacy_bounceTrackingProtection_clientBounceDetectionTimerPeriodMS(),
      nsITimer::TYPE_ONE_SHOT, "mClientBounceDetectionTimeout"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  // For each URL in URLs: Insert host to the navigable’s bounce tracking
  // record's bounce set.
  for (const nsACString& site : aSiteList) {
    mBounceTrackingRecord->AddBounceHost(site);
  }

  return NS_OK;
}

nsresult BounceTrackingState::OnDocumentLoaded(
    nsIPrincipal* aDocumentPrincipal) {
  NS_ENSURE_ARG_POINTER(aDocumentPrincipal);

  // Logging
  if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
    nsAutoCString origin;
    nsresult rv = aDocumentPrincipal->GetOrigin(origin);
    if (NS_FAILED(rv)) {
      origin = "err";
    }
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: origin: {}, this: {}", __FUNCTION__, origin, *this);
  }

  bool shouldTrackPrincipal =
      BounceTrackingState::ShouldTrackPrincipal(aDocumentPrincipal);

  // Check if we need to log a warning to the DevTools console because we have
  // previously purged this site. This is only relevant to check if we actually
  // monitor this principal for bounce tracking.
  if (shouldTrackPrincipal) {
    mBounceTrackingProtection->MaybeLogPurgedWarningForSite(aDocumentPrincipal,
                                                            this);
  }

  // Assert: navigable’s bounce tracking record is not null. It gets created in
  // OnStartNavigation which runs for every top level navigation before the
  // document loads.
  if (NS_WARN_IF(!mBounceTrackingRecord)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString siteHost;
  if (!shouldTrackPrincipal) {
    siteHost = "";
  } else {
    nsresult rv = aDocumentPrincipal->GetBaseDomain(siteHost);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set the navigable’s bounce tracking record's final host to the host of
  // finalSite.
  mBounceTrackingRecord->SetFinalHost(siteHost);

  return NS_OK;
}

nsresult BounceTrackingState::OnUserActivation(const nsACString& aSiteHost) {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: aSiteHost: {}, mBounceTrackingRecord: {}", __FUNCTION__,
              aSiteHost, mBounceTrackingRecord);

  if (mBounceTrackingRecord) {
    mBounceTrackingRecord->AddUserActivationHost(aSiteHost);
  }

  return NS_OK;
}

}  // namespace mozilla
