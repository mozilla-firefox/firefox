/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingProtection.h"
#include "BounceTrackingProtectionStorage.h"
#include "BounceTrackingState.h"
#include "BounceTrackingRecord.h"
#include "ProfileAfterChangeGate.h"

#include "ErrorList.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/Try.h"
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
  CancelClientBounceDetectionTimeout();
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

  MOZ_ASSERT(browsingContext->OriginAttributesRef().mPartitionKey.IsEmpty(),
             "Top level BCs must not have a partition key.");

  // Create a copy of the BC's OriginAttributes so we can use it later without
  // having to hold a reference to the BC. We don't need first party domain.
  // Many things in BTP are keyed by OriginAttributes and we don't want to
  // create unnecessary partitions.
  mOriginAttributes =
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          browsingContext->OriginAttributesRef());

  // Add a listener for window load. See BounceTrackingState::OnStateChange for
  // the listener code.
  return aWebProgress->AddProgressListener(this,
                                           nsIWebProgress::NOTIFY_STATE_WINDOW);
}

void BounceTrackingState::ResetBounceTrackingRecord() {
  mBounceTrackingRecord = nullptr;
  mRetargetedOriginAttributes.reset();
}

void BounceTrackingState::DropRetargetedLoadBounces() {
  if (!mRetargetedOriginAttributes) {
    return;
  }
  if (mBounceTrackingRecord) {
    mBounceTrackingRecord->RemoveBounceSites(*mRetargetedOriginAttributes);
  }
  mRetargetedOriginAttributes.reset();
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

  Maybe<OriginAttributes> originAttributes;
  if (aOriginAttributes) {
    originAttributes.emplace(
        BounceTrackingProtectionStorage::NormalizeOriginAttributes(
            *aOriginAttributes));
  }
  auto matches = [&](const OriginAttributes& aCandidate) {
    if (originAttributes) {
      return *originAttributes == aCandidate;
    }
    return !aPattern || aPattern->Matches(aCandidate);
  };

  for (const WeakPtr<BounceTrackingState>& btsWeak :
       sBounceTrackingStates->Values()) {
    if (!btsWeak) {
      continue;
    }
    RefPtr<BounceTrackingState> bounceTrackingState(btsWeak);

    // A record which followed a load retargeted from another container also
    // holds sites which ran in that container. Drop it whole rather than
    // leaving a partial record behind.
    if (!matches(bounceTrackingState->OriginAttributesRef()) &&
        !(bounceTrackingState->mBounceTrackingRecord &&
          bounceTrackingState->mBounceTrackingRecord
              ->HasOriginAttributesMatching(matches))) {
      continue;
    }
    bounceTrackingState->CancelClientBounceDetectionTimeout();
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
    const BounceTrackingSite& aSite, bool& aResult) {
  aResult = false;
  NS_ENSURE_TRUE(!aSite.IsNull(), NS_ERROR_FAILURE);

  if (!sBounceTrackingStates) {
    return NS_OK;
  }

  // Iterate over all browsing contexts which have a bounce tracking state. Use
  // the content principal base domain field to determine whether a BC has an
  // active site that matches aSite.
  for (const WeakPtr<BounceTrackingState>& btsWeak :
       sBounceTrackingStates->Values()) {
    if (!btsWeak) {
      continue;
    }
    RefPtr<BounceTrackingState> state(btsWeak);

    // Skip BTS of unrelated OA. These are not relevant for the caller as their
    // state is isolated.
    if (state->mOriginAttributes != aSite.OriginAttributesRef()) {
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
    Result<BounceTrackingSite, nsresult> site =
        BounceTrackingSite::FromPrincipal(principal);
    if (NS_WARN_IF(site.isErr())) {
      continue;
    }

    if (site.inspect() == aSite) {
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
  // retargeted into a new tab in the container its site is bound to. Each hop
  // is recorded with the OriginAttributes it ran in, and the record follows the
  // load into the new tab, see OnLoadRetargeted.
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
  // not the null site.
  bool siteListIsEmpty = true;

  // Collect uri list including any redirects. Record each hop with the
  // container it ran in: the tab's, unless a redirect to a site bound to
  // another container switched the rest of the load into it.
  nsTArray<BounceTrackingSite> siteList;

  for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectHistoryEntry :
       loadInfo->RedirectChain()) {
    nsCOMPtr<nsIPrincipal> principal;
    rv = redirectHistoryEntry->GetPrincipal(getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    BounceTrackingSite site =
        MOZ_TRY(BounceTrackingSite::FromPrincipal(principal));
    siteListIsEmpty &= site.IsNull();
    siteList.AppendElement(std::move(site));
  }

  // Add site via the current URI which is the end of the chain.
  nsCOMPtr<nsIURI> channelURI;
  rv = aChannel->GetURI(getter_AddRefs(channelURI));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mozilla::net::SchemeIsHttpOrHttps(channelURI)) {
    BounceTrackingSite site = MOZ_TRY(BounceTrackingSite::FromURI(
        channelURI, loadInfo->GetOriginAttributes()));

    if (site.IsNull()) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Failed to get site host from channelURI: {}",
                  __FUNCTION__, channelURI->GetSpecOrDefault());
    }
    siteListIsEmpty &= site.IsNull();
    siteList.AppendElement(std::move(site));
  }

  // Do not record empty site lists. This can happen if none of the principals
  // are suitable for tracking. It includes when OnDocumentStartRequest is
  // called for the initial about:blank.
  if (siteListIsEmpty) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: skip empty site list.", __FUNCTION__);
    return NS_OK;
  }

  // A load which switched container is retargeted into a new tab.
  OriginAttributes loadOriginAttributes =
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          loadInfo->GetOriginAttributes());
  if (loadOriginAttributes != mOriginAttributes) {
    mRetargetedOriginAttributes = Some(loadOriginAttributes);
  } else {
    mRetargetedOriginAttributes.reset();
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

  BounceTrackingSite site =
      MOZ_TRY(BounceTrackingSite::FromPrincipal(documentPrincipal));
  if (!site.IsNull()) {
    nsresult rv =
        OnResponseReceived(AutoTArray<BounceTrackingSite, 1>{std::move(site)});
    NS_ENSURE_SUCCESS(rv, rv);
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

// Site of aWindowContext's top level document. The null site if that document
// is no longer the current one or is not a principal we track.
static BounceTrackingSite GetTopLevelSite(dom::WindowContext* aWindowContext) {
  if (!aWindowContext) {
    return {};
  }
  dom::WindowContext* topWindowContext = aWindowContext->TopWindowContext();
  if (!topWindowContext || !topWindowContext->IsCurrent()) {
    return {};
  }

  nsIPrincipal* principal = topWindowContext->Canonical()->DocumentPrincipal();
  if (!principal) {
    return {};
  }

  Result<BounceTrackingSite, nsresult> site =
      BounceTrackingSite::FromPrincipal(principal);
  if (NS_WARN_IF(site.isErr())) {
    return {};
  }
  return site.unwrap();
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
  DropRetargetedLoadBounces();

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
  CancelClientBounceDetectionTimeout();

  // The initiator's (schemeless) site, for the user activation set. The null
  // site for opaque origins. Strictly speaking we only need to check
  // IsNullPrincipal, but we're generally only interested in content principals
  // with http/s scheme. Other principal types or schemes are not considered to
  // be trackers.
  Result<BounceTrackingSite, nsresult> initiatorSite =
      BounceTrackingSite::FromPrincipal(aTriggeringPrincipal);
  NS_WARNING_ASSERTION(initiatorSite.isOk(), "No site for the initiator.");
  BounceTrackingSite site = initiatorSite.unwrapOr(BounceTrackingSite());

  // The site this context is leaving, not the initiator's, which can be a frame
  // or another tab. Read at commit time. Null if nothing is committed yet.
  BounceTrackingSite departingSite;
  // RecordStatefulBounces exempts the initial site, so a frame which navigates
  // the top level must not name itself. A context with no committed document
  // was opened by this navigation; use its opener, not the initiator, which can
  // resolve back into it and name the frame again.
  BounceTrackingSite initialSite;
  if (RefPtr<dom::BrowsingContext> browsingContext = CurrentBrowsingContext()) {
    if (browsingContext->GetHasLoadedNonInitialDocument()) {
      departingSite =
          GetTopLevelSite(browsingContext->GetCurrentWindowContext());
      initialSite = departingSite;
    } else if (RefPtr<dom::BrowsingContext> opener =
                   browsingContext->GetOpener()) {
      initialSite = GetTopLevelSite(opener->GetCurrentWindowContext());
    }
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: site: {}, departingSite: {}, initialSite: {}", __FUNCTION__,
              site, departingSite, initialSite);

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
    mBounceTrackingRecord->SetInitialSite(initialSite);
    if (hasUserActivation) {
      mBounceTrackingRecord->AddUserActivationSite(site);
    }

    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: new BounceTrackingRecord(): {}", __FUNCTION__,
                mBounceTrackingRecord);

    return NS_OK;
  }

  // If sourceSnapshotParams’s has transient activation is true: The user
  // activation ends the extended navigation. Process the bounce candidates.
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: site: {}, hasUserActivation? {}", __FUNCTION__, site,
              hasUserActivation);
  if (hasUserActivation) {
    nsresult rv = mBounceTrackingProtection->RecordStatefulBounces(this);
    NS_ENSURE_SUCCESS(rv, rv);

    MOZ_ASSERT(!mBounceTrackingRecord);
    mBounceTrackingRecord = MakeRefPtr<BounceTrackingRecord>();
    mBounceTrackingRecord->SetInitialSite(initialSite);
    // Not initialSite: the user activation set feeds
    // DynamicFpiNavigationHeuristic, which wants the site interacted with.
    mBounceTrackingRecord->AddUserActivationSite(site);

    return NS_OK;
  }

  // There is no transient user activation. Add the site being left as a bounce
  // candidate. Not initialSite: its opener fallback would name the initiating
  // tab again.
  mBounceTrackingRecord->AddBounceSite(departingSite);

  return NS_OK;
}

// Private

nsresult BounceTrackingState::OnResponseReceived(
    const nsTArray<BounceTrackingSite>& aSiteList) {
  MOZ_ASSERT(!aSiteList.IsEmpty(), "siteList should not be empty.");

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: #{} siteList: {}", __FUNCTION__, aSiteList.Length(),
              fmt::join(aSiteList, ", "));

  // Record should exist by now. It gets created in OnStartNavigation which
  // runs for every top level document load before the response is received.
  if (NS_WARN_IF(!mBounceTrackingRecord)) {
    return NS_ERROR_FAILURE;
  }

  // Check if there is still an active timeout. This shouldn't happen since
  // OnStartNavigation already cancels it.
  CancelClientBounceDetectionTimeout();

  // Run steps after a timeout: queue a global task on the networking task
  // source with global to record stateful bounces for bounce.
  nsresult rv = StartClientBounceDetectionTimeout();
  NS_ENSURE_SUCCESS(rv, rv);

  // For each URL in URLs: Insert host to the navigable’s bounce tracking
  // record's bounce set.
  for (const BounceTrackingSite& site : aSiteList) {
    mBounceTrackingRecord->AddBounceSite(site);
  }

  return NS_OK;
}

nsresult BounceTrackingState::StartClientBounceDetectionTimeout() {
  MOZ_ASSERT(!mClientBounceDetectionTimeout);
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

  return NS_OK;
}

// static
void BounceTrackingState::OnLoadRetargeted(
    dom::CanonicalBrowsingContext* aSource,
    dom::CanonicalBrowsingContext* aTarget, uint64_t aLoadId) {
  MOZ_ASSERT(aSource);
  MOZ_ASSERT(aTarget);

  // The load never starts in the target, so this is what creates its state,
  // which must exist even if the record stays: the purge spares the sites open
  // in tabs with a BounceTrackingState.
  RefPtr<BounceTrackingState> target = aTarget->GetBounceTrackingState();
  RefPtr<BounceTrackingState> source = aSource->GetBounceTrackingState();
  if (!target || !source || target == source ||
      !source->mBounceTrackingRecord) {
    return;
  }
  // The new tab is created asynchronously. If the source started another
  // navigation meanwhile, the record belongs to that one and stays.
  // The record has dropped this load's hops already, see
  // DropRetargetedLoadBounces.
  if (source->mLastStartedLoadId != Some(aLoadId)) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: source moved on to another load, keeping its record",
                __FUNCTION__);
    return;
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: source: {}, target: {}", __FUNCTION__, *source, *target);

  if (NS_WARN_IF(target->mBounceTrackingRecord)) {
    MOZ_ASSERT_UNREACHABLE("The target was created for this load.");
    return;
  }
  target->CancelClientBounceDetectionTimeout();

  target->mBounceTrackingRecord = std::move(source->mBounceTrackingRecord);
  target->mLastStartedLoadId = source->mLastStartedLoadId;
  source->mRetargetedOriginAttributes.reset();
  // User activation in another container must not feed the storage access
  // heuristic of this one, see DynamicFpiNavigationHeuristic.
  target->mBounceTrackingRecord->ClearUserActivationHosts();

  // Restarted from zero, so the client bounce window grows by the time it took
  // to create the new tab.
  if (source->mClientBounceDetectionTimeout) {
    source->CancelClientBounceDetectionTimeout();
    DebugOnly<nsresult> rv = target->StartClientBounceDetectionTimeout();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "StartClientBounceDetectionTimeout failed");
  }
}

void BounceTrackingState::CancelClientBounceDetectionTimeout() {
  if (!mClientBounceDetectionTimeout) {
    return;
  }
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: mClientBounceDetectionTimeout->Cancel()", __FUNCTION__);
  mClientBounceDetectionTimeout->Cancel();
  mClientBounceDetectionTimeout = nullptr;
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

  BounceTrackingSite finalSite =
      MOZ_TRY(BounceTrackingSite::FromPrincipal(aDocumentPrincipal));

  // Set the navigable’s bounce tracking record's final host to the host of
  // finalSite.
  mBounceTrackingRecord->SetFinalSite(finalSite);

  return NS_OK;
}

nsresult BounceTrackingState::OnUserActivation(
    const BounceTrackingSite& aSite) {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: aSite: {}, mBounceTrackingRecord: {}", __FUNCTION__, aSite,
              mBounceTrackingRecord);

  if (mBounceTrackingRecord) {
    mBounceTrackingRecord->AddUserActivationSite(aSite);
  }

  return NS_OK;
}

}  // namespace mozilla
