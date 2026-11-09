/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingProtection.h"

#include "BounceTrackingAllowList.h"
#include "BounceTrackingProtectionStorage.h"
#include "BounceTrackingState.h"
#include "BounceTrackingRecord.h"
#include "BounceTrackingMapEntry.h"
#include "ClearDataCallback.h"
#include "ProfileAfterChangeGate.h"

#include "BounceTrackingStateGlobal.h"
#include "ErrorList.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Try.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "nsComponentManagerUtils.h"
#include "nsDebug.h"
#include "nsGlobalWindowInner.h"
#include "nsHashPropertyBag.h"
#include "nsIClearDataService.h"
#include "nsIObserverService.h"
#include "nsIPermissionManager.h"
#include "nsIPrincipal.h"
#include "nsISupports.h"
#include "nsISupportsPrimitives.h"
#include "nsServiceManagerUtils.h"
#include "nscore.h"
#include "prtime.h"
#include "mozilla/dom/BrowsingContext.h"
#include "xpcpublic.h"
#include "mozilla/glean/AntitrackingBouncetrackingprotectionMetrics.h"
#include "mozilla/ContentBlockingLog.h"
#include "mozilla/glean/GleanPings.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIConsoleService.h"
#include "mozilla/intl/Localization.h"
#include "mozilla/browser/NimbusFeatures.h"

#define TEST_OBSERVER_MSG_RECORD_BOUNCES_FINISHED "test-record-bounces-finished"

namespace mozilla {

NS_IMPL_ISUPPORTS(BounceTrackingProtection, nsIObserver,
                  nsISupportsWeakReference, nsIBounceTrackingProtection);

LazyLogModule gBounceTrackingProtectionLog("BounceTrackingProtection");

static StaticRefPtr<BounceTrackingProtection> sBounceTrackingProtection;
static bool sInitFailed = false;

Maybe<uint32_t> BounceTrackingProtection::sLastRecordedModeTelemetry;

static const char kBTPModePref[] = "privacy.bounceTrackingProtection.mode";

// static
already_AddRefed<BounceTrackingProtection>
BounceTrackingProtection::GetSingleton() {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsresult rv = EnsurePastProfileAfterChange();
  if (NS_FAILED(rv)) {
    // We haven't reached "profile-after-change" yet, so we can't initialize
    // BounceTrackingProtection. Return nullptr.
    return nullptr;
  }

  // Init previously failed, don't try again.
  if (sInitFailed) {
    return nullptr;
  }

  RecordModePrefTelemetry();

  // Feature is disabled.
  if (StaticPrefs::privacy_bounceTrackingProtection_mode() ==
      nsIBounceTrackingProtection::MODE_DISABLED) {
    return nullptr;
  }

  // Feature is enabled, lazily create singleton instance.
  if (!sBounceTrackingProtection) {
    sBounceTrackingProtection = new BounceTrackingProtection();
    RunOnShutdown([] {
      if (sBounceTrackingProtection &&
          sBounceTrackingProtection->mRemoteExceptionList) {
        (void)sBounceTrackingProtection->mRemoteExceptionList->Shutdown();
      }
      sBounceTrackingProtection = nullptr;
    });

    nsresult rv = sBounceTrackingProtection->Init();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      sInitFailed = true;
      sBounceTrackingProtection = nullptr;
      return nullptr;
    }
  }

  return do_AddRef(sBounceTrackingProtection);
}

// static
void BounceTrackingProtection::RecordModePrefTelemetry() {
  uint32_t featureMode = StaticPrefs::privacy_bounceTrackingProtection_mode();

  // Record mode telemetry, but only if the mode changed.
  if (sLastRecordedModeTelemetry.isNothing() ||
      sLastRecordedModeTelemetry.value() != featureMode) {
    glean::bounce_tracking_protection::mode.Set(featureMode);
    sLastRecordedModeTelemetry = Some(featureMode);
  }
}

nsresult BounceTrackingProtection::Init() {
  MOZ_ASSERT(StaticPrefs::privacy_bounceTrackingProtection_mode() !=
                 nsIBounceTrackingProtection::MODE_DISABLED,
             "Mode pref must have an enabled state for init to be called.");
  MOZ_LOG_FMT(
      gBounceTrackingProtectionLog, LogLevel::Info,
      "Init BounceTrackingProtection. Config: mode: {}, "
      "bounceTrackingActivationLifetimeSec: {}, bounceTrackingGracePeriodSec: "
      "{}, bounceTrackingPurgeTimerPeriodSec: {}, "
      "clientBounceDetectionTimerPeriodMS: {}, "
      "HasMigratedUserActivationData: {}",
      static_cast<nsIBounceTrackingProtection::Modes>(
          StaticPrefs::privacy_bounceTrackingProtection_mode()),
      StaticPrefs::
          privacy_bounceTrackingProtection_bounceTrackingActivationLifetimeSec(),
      StaticPrefs::
          privacy_bounceTrackingProtection_bounceTrackingGracePeriodSec(),
      StaticPrefs::
          privacy_bounceTrackingProtection_bounceTrackingPurgeTimerPeriodSec(),
      StaticPrefs::
          privacy_bounceTrackingProtection_clientBounceDetectionTimerPeriodMS(),
      StaticPrefs::
          privacy_bounceTrackingProtection_hasMigratedUserActivationData());

  mStorage = new BounceTrackingProtectionStorage();

  nsresult rv = mStorage->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = MaybeMigrateUserInteractionPermissions();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Error,
                "user activation permission migration failed");
  }

  // Register feature pref listener which dynamically enables or disables the
  // feature depending on feature pref state.
  rv = Preferences::RegisterCallback(&BounceTrackingProtection::OnPrefChange,
                                     kBTPModePref);
  NS_ENSURE_SUCCESS(rv, rv);

  // Run the remaining init logic.
  return OnModeChange(true);
}

nsresult BounceTrackingProtection::UpdateBounceTrackingPurgeTimer(
    bool aShouldEnable) {
  // Cancel the existing timer.
  // If disabling: we're done now.
  // If enabling: schedule a new timer so interval changes (as controlled by the
  // pref) are taken into account.
  if (mBounceTrackingPurgeTimer) {
    mBounceTrackingPurgeTimer->Cancel();
    mBounceTrackingPurgeTimer = nullptr;
  }

  if (!aShouldEnable) {
    return NS_OK;
  }

  // Schedule timer for tracker purging. The timer interval is determined by
  // pref.
  uint32_t purgeTimerPeriod = StaticPrefs::
      privacy_bounceTrackingProtection_bounceTrackingPurgeTimerPeriodSec();

  // The pref can be set to 0 to disable interval purging.
  if (purgeTimerPeriod == 0) {
    return NS_OK;
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "Scheduling mBounceTrackingPurgeTimer. Interval: {} seconds.",
              purgeTimerPeriod);

  return NS_NewTimerWithCallback(
      getter_AddRefs(mBounceTrackingPurgeTimer),
      [](auto) {
        if (!sBounceTrackingProtection) {
          return;
        }
        sBounceTrackingProtection->PurgeBounceTrackers()->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [] {
              MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                          "{}: PurgeBounceTrackers finished after timer call.",
                          __FUNCTION__);
            },
            [] { NS_WARNING("RunPurgeBounceTrackers failed"); });
      },
      purgeTimerPeriod * PR_MSEC_PER_SEC, nsITimer::TYPE_REPEATING_SLACK,
      "mBounceTrackingPurgeTimer"_ns);
}

// static
void BounceTrackingProtection::OnPrefChange(const char* aPref, void* aData) {
  MOZ_ASSERT(sBounceTrackingProtection);
  MOZ_ASSERT(strcmp(kBTPModePref, aPref) == 0);

  RecordModePrefTelemetry();

  sBounceTrackingProtection->OnModeChange(false);
}

nsresult BounceTrackingProtection::OnModeChange(bool aIsStartup) {
  // Get feature mode from pref and ensure its within bounds.
  uint8_t modeInt = StaticPrefs::privacy_bounceTrackingProtection_mode();
  NS_ENSURE_TRUE(modeInt <= nsIBounceTrackingProtection::MAX_MODE_VALUE,
                 NS_ERROR_FAILURE);
  nsIBounceTrackingProtection::Modes mode =
      static_cast<nsIBounceTrackingProtection::Modes>(modeInt);

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: mode: {}.",
              __FUNCTION__, mode);
  if (sInitFailed) {
    return NS_ERROR_FAILURE;
  }

  nsresult result = NS_OK;

  if (!aIsStartup) {
    // Clear bounce tracker candidate map for any mode change so it's not leaked
    // into other modes. For example if we switch from dry-run mode into fully
    // enabled we want a clean slate to not purge trackers that we've classified
    // in dry-run mode. User activation data must be kept to avoid false
    // positives.
    MOZ_ASSERT(mStorage);
    result = mStorage->ClearByType(
        BounceTrackingProtectionStorage::EntryType::BounceTracker);
  }

  // On disable
  if (mode == nsIBounceTrackingProtection::MODE_DISABLED ||
      mode == nsIBounceTrackingProtection::MODE_ENABLED_STANDBY) {
    // No further cleanup needed if we're just starting up.
    if (aIsStartup) {
      MOZ_ASSERT(!mBounceTrackingPurgeTimer);
      return result;
    }

    // Stop regular purging.
    nsresult rv = UpdateBounceTrackingPurgeTimer(false);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      result = rv;
      // Even if this step fails try to do more cleanup.
    }

    // Clear all per-tab state.
    BounceTrackingState::DestroyAll();
    return result;
  }

  // On enable
  MOZ_ASSERT(mode == nsIBounceTrackingProtection::MODE_ENABLED ||
             mode == nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN);

  // Schedule regular purging.
  nsresult rv = UpdateBounceTrackingPurgeTimer(true);
  NS_ENSURE_SUCCESS(rv, rv);

  return result;
}

nsresult BounceTrackingProtection::RecordStatefulBounces(
    BounceTrackingState* aBounceTrackingState) {
  NS_ENSURE_ARG_POINTER(aBounceTrackingState);

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: aBounceTrackingState: {}", __FUNCTION__,
              *aBounceTrackingState);

  aBounceTrackingState->DropRetargetedLoadBounces();

  // Assert: navigable’s bounce tracking record is not null.
  BounceTrackingRecord* record =
      aBounceTrackingState->GetBounceTrackingRecord();
  if (!record) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "GetBounceTrackingRecord returned nothing");
    return NS_ERROR_FAILURE;
  }

  // The storage partition the tab's own bounces are written into is keyed by
  // the BounceTrackingState's cached OriginAttributes. Assert it still matches
  // the tab's current top BrowsingContext, so that if a tab's userContextId
  // were ever to change during its lifetime we don't file bounces under a
  // container the tab no longer lives in. A load switching container is
  // retargeted into a new tab instead. The BrowsingContext may be gone when
  // recording on tab close during shutdown; in that case there is nothing to
  // compare against and we skip the check. See Bug 2054941.
#ifdef DEBUG
  if (RefPtr<dom::BrowsingContext> bc =
          aBounceTrackingState->CurrentBrowsingContext()) {
    MOZ_ASSERT(bc->OriginAttributesRef().EqualsIgnoringFPD(
                   aBounceTrackingState->OriginAttributesRef()),
               "BTP: recording bounces under a container that no longer "
               "matches the tab's BrowsingContext (Bug 2054941).");
  }
#endif

  nsTArray<nsCString> classifiedHosts;
  RefPtr<BounceTrackingStateGlobal> globalState;

  // Each bounce is classified in the storage partition it ran in, which can
  // differ from the tab's if the load was retargeted into another container.
  // For each site in navigable’s bounce tracking record's bounce set:
  for (const BounceTrackingSite& site : record->BounceSites()) {
    // Skip null sites, they are only used for logging purposes.
    if (site.IsNull()) {
      continue;
    }

    // If site equals navigable’s bounce tracking record's initial site,
    // continue.
    if (site == record->InitialSite()) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip site == initialSite: {}", __FUNCTION__, site);
      continue;
    }
    // If site equals navigable’s bounce tracking record's final site,
    // continue.
    if (site == record->FinalSite()) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip site == finalSite: {}", __FUNCTION__, site);
      continue;
    }

    // The bounce tracker map and the user activation map of the site's
    // partition. Created only for sites left to check, so null sites don't
    // create a state global. Consecutive sites usually share a partition.
    if (!globalState ||
        globalState->OriginAttributesRef() != site.OriginAttributesRef()) {
      globalState =
          mStorage->GetOrCreateStateGlobal(site.OriginAttributesRef());
      MOZ_ASSERT(globalState);
    }

    // If user activation map contains site, continue.
    if (globalState->HasUserActivation(site)) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip site with recent user activation: {}", __FUNCTION__,
                  site);
      continue;
    }

    // If stateful bounce tracking map contains site, continue.
    if (globalState->HasBounceTracker(site)) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip already existing site: {}", __FUNCTION__, site);
      continue;
    }

    // Set stateful bounce tracking map[site] to topDocument’s relevant
    // settings object’s current wall time.
    PRTime now = PR_Now();
    MOZ_ASSERT(!globalState->HasBounceTracker(site));
    nsresult rv = globalState->RecordBounceTracker(site, now, false, record);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    // A site can be classified in more than one container.
    if (!classifiedHosts.Contains(site.SiteHost())) {
      classifiedHosts.AppendElement(site.SiteHost());
    }

    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Info,
                "{}: Added bounce tracker candidate. site: {}, "
                "aBounceTrackingState: {}",
                __FUNCTION__, site, *aBounceTrackingState);
  }

  // Set navigable’s bounce tracking record to null.
  aBounceTrackingState->ResetBounceTrackingRecord();
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Done, reset aBounceTrackingState: {}", __FUNCTION__,
              *aBounceTrackingState);

  // Log a message to the web console for each classified host.
  nsresult rv = LogBounceTrackersClassifiedToWebConsole(aBounceTrackingState,
                                                        classifiedHosts);
  NS_ENSURE_SUCCESS(rv, rv);

  // If running in test automation, dispatch an observer message indicating
  // we're finished recording bounces.
  if (StaticPrefs::privacy_bounceTrackingProtection_enableTestMode()) {
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    NS_ENSURE_TRUE(obsSvc, NS_ERROR_FAILURE);

    RefPtr<nsHashPropertyBag> props = new nsHashPropertyBag();

    nsresult rv = props->SetPropertyAsUint64(
        u"browserId"_ns, aBounceTrackingState->GetBrowserId());
    NS_ENSURE_SUCCESS(rv, rv);

    // Number of hosts classified as bounce trackers in this call. Tests use
    // this to distinguish a finalization that classified trackers from one that
    // finalized an empty or fully exempt record.
    rv = props->SetPropertyAsUint32(u"bounceTrackerCandidateCount"_ns,
                                    classifiedHosts.Length());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obsSvc->NotifyObservers(
        ToSupports(props), TEST_OBSERVER_MSG_RECORD_BOUNCES_FINISHED, nullptr);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult BounceTrackingProtection::RecordUserActivation(
    nsIPrincipal* aPrincipal, Maybe<PRTime> aActivationTime,
    dom::CanonicalBrowsingContext* aTopBrowsingContext) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_TRUE(!aTopBrowsingContext || aTopBrowsingContext->IsTop(),
                 NS_ERROR_INVALID_ARG);

  RefPtr<BounceTrackingProtection> btp = GetSingleton();
  // May be nullptr if feature is disabled.
  if (!btp) {
    return NS_OK;
  }

  BounceTrackingSite site =
      MOZ_TRY(BounceTrackingSite::FromPrincipal(aPrincipal));
  if (site.IsNull()) {
    return NS_OK;
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: site: {}",
              __FUNCTION__, site);

  RefPtr<BounceTrackingStateGlobal> globalState =
      btp->mStorage->GetOrCreateStateGlobal(aPrincipal);
  MOZ_ASSERT(globalState);

  // aActivationTime defaults to current time if no value is provided.
  nsresult rv = globalState->RecordUserActivation(
      site, aActivationTime.valueOr(PR_Now()));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aTopBrowsingContext) {
    MOZ_ASSERT(aTopBrowsingContext->IsTop());
    dom::BrowsingContextWebProgress* webProgress =
        aTopBrowsingContext->GetWebProgress();
    NS_ENSURE_TRUE(webProgress, NS_ERROR_FAILURE);

    RefPtr<BounceTrackingState> bounceTrackingState =
        webProgress->GetBounceTrackingState();
    // We may not always get a BounceTrackingState, e.g the feature is disabled,
    // if the user has opted into all cookies, no cookies, or some other case.
    // If we don't have it, just return OK here.
    NS_ENSURE_TRUE(bounceTrackingState, NS_OK);

    return bounceTrackingState->OnUserActivation(site);
  }
  return NS_OK;
}

nsresult BounceTrackingProtection::RecordUserActivation(
    dom::WindowContext* aWindowContext) {
  NS_ENSURE_ARG_POINTER(aWindowContext);

  if (XRE_IsContentProcess()) {
    dom::WindowGlobalChild* wgc = aWindowContext->GetWindowGlobalChild();
    NS_ENSURE_TRUE(wgc, NS_ERROR_FAILURE);
    NS_ENSURE_TRUE(wgc->SendRecordUserActivationForBTP(), NS_ERROR_FAILURE);
    return NS_OK;
  }
  MOZ_ASSERT(XRE_IsParentProcess());

  dom::WindowGlobalParent* wgp = aWindowContext->Canonical();
  MOZ_ASSERT(wgp);

  NS_ENSURE_TRUE(wgp->RecvRecordUserActivationForBTP(), NS_ERROR_FAILURE);

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::Observe(nsISupports* aSubject, const char* aTopic,
                                  const char16_t* aData) {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: aTopic: {}",
              __FUNCTION__, aTopic);

  if (!strcmp(aTopic, "idle-daily")) {
    // Submit custom telemetry ping.
    glean_pings::BounceTrackingProtection.Submit();
  }
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestGetBounceTrackerCandidateHosts(
    JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    nsTArray<RefPtr<nsIBounceTrackingMapEntry>>& aCandidates) {
  MOZ_ASSERT(aCx);

  OriginAttributes oa;
  if (!aOriginAttributes.isObject() || !oa.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<BounceTrackingStateGlobal> globalState = mStorage->GetStateGlobal(oa);
  if (!globalState) {
    return NS_OK;
  }

  for (auto iter = globalState->BounceTrackersMapRef().ConstIter();
       !iter.Done(); iter.Next()) {
    RefPtr<nsIBounceTrackingMapEntry> candidate = new BounceTrackingMapEntry(
        globalState->SiteFor(iter.Key()), iter.Data().mBounceTime);
    aCandidates.AppendElement(candidate);
  }

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestGetUserActivationHosts(
    JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    nsTArray<RefPtr<nsIBounceTrackingMapEntry>>& aHosts) {
  MOZ_ASSERT(aCx);

  OriginAttributes oa;
  if (!aOriginAttributes.isObject() || !oa.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<BounceTrackingStateGlobal> globalState = mStorage->GetStateGlobal(oa);
  if (!globalState) {
    return NS_OK;
  }

  for (auto iter = globalState->UserActivationMapRef().ConstIter();
       !iter.Done(); iter.Next()) {
    RefPtr<nsIBounceTrackingMapEntry> candidate = new BounceTrackingMapEntry(
        globalState->SiteFor(iter.Key()), iter.Data());
    aHosts.AppendElement(candidate);
  }

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestGetRecentlyPurgedTrackers(
    JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx,
    nsTArray<RefPtr<nsIBounceTrackingPurgeEntry>>& aPurgedTrackers) {
  MOZ_ASSERT(aCx);

  OriginAttributes oa;
  if (!aOriginAttributes.isObject() || !oa.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<BounceTrackingStateGlobal> globalState = mStorage->GetStateGlobal(oa);
  if (!globalState) {
    return NS_OK;
  }

  nsTArray<RefPtr<BounceTrackingPurgeEntry>> purgeEntriesSorted;
  for (auto iter = globalState->RecentPurgesMapRef().ConstIter(); !iter.Done();
       iter.Next()) {
    for (const auto& entry : iter.Data()) {
      purgeEntriesSorted.InsertElementSorted(entry, PurgeEntryTimeComparator{});
    }
  }

  aPurgedTrackers.AppendElements(purgeEntriesSorted);

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::ClearAll() {
  BounceTrackingState::ResetAll();
  return mStorage->Clear();
}

NS_IMETHODIMP
BounceTrackingProtection::ClearBySiteHostAndOriginAttributes(
    const nsACString& aSiteHost, JS::Handle<JS::Value> aOriginAttributes,
    JSContext* aCx) {
  NS_ENSURE_ARG_POINTER(aCx);

  OriginAttributes originAttributes;
  if (!aOriginAttributes.isObject() ||
      !originAttributes.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Bounce tracking state is only kept for top level sites, which a partitioned
  // principal never is.
  if (!originAttributes.mPartitionKey.IsEmpty()) {
    return NS_OK;
  }

  // Reset per tab state for tabs matching the given OriginAttributes.
  BounceTrackingState::ResetAllForOriginAttributes(originAttributes);

  return mStorage->ClearBySiteHost(aSiteHost, &originAttributes);
}

NS_IMETHODIMP
BounceTrackingProtection::ClearBySiteHostAndOriginAttributesPattern(
    const nsACString& aSiteHost, JS::Handle<JS::Value> aOriginAttributesPattern,
    JSContext* aCx) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_TRUE(!aSiteHost.IsEmpty(), NS_ERROR_INVALID_ARG);

  OriginAttributesPattern pattern;
  if (!aOriginAttributesPattern.isObject() ||
      !pattern.Init(aCx, aOriginAttributesPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Clear per-tab state.
  BounceTrackingState::ResetAllForOriginAttributesPattern(pattern);

  // Clear global state including on-disk state.
  return mStorage->ClearByOriginAttributesPattern(pattern,
                                                  Some(nsCString(aSiteHost)));
}

NS_IMETHODIMP
BounceTrackingProtection::ClearByTimeRange(PRTime aFrom, PRTime aTo) {
  NS_ENSURE_TRUE(aFrom >= 0, NS_ERROR_INVALID_ARG);
  NS_ENSURE_TRUE(aFrom < aTo, NS_ERROR_INVALID_ARG);

  // Clear all BounceTrackingState, we don't keep track of time ranges.
  BounceTrackingState::ResetAll();

  return mStorage->ClearByTimeRange(aFrom, aTo);
}

NS_IMETHODIMP
BounceTrackingProtection::ClearByOriginAttributesPattern(
    const nsAString& aPattern) {
  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Reset all per-tab state matching the given OriginAttributesPattern.
  BounceTrackingState::ResetAllForOriginAttributesPattern(pattern);

  return mStorage->ClearByOriginAttributesPattern(pattern);
}

NS_IMETHODIMP
BounceTrackingProtection::AddSiteHostExceptions(
    const nsTArray<nsCString>& aSiteHosts) {
  for (const auto& host : aSiteHosts) {
    mRemoteSiteHostExceptions.Insert(host);
  }

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::RemoveSiteHostExceptions(
    const nsTArray<nsCString>& aSiteHosts) {
  for (const auto& host : aSiteHosts) {
    mRemoteSiteHostExceptions.Remove(host);
  }

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::HasRecentlyPurgedSite(const nsACString& aSiteHost,
                                                bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = false;
  NS_ENSURE_TRUE(!aSiteHost.IsEmpty(), NS_ERROR_INVALID_ARG);

  // Look for a purge log entry matching the site and return greedily if we find
  // one. We look through all state globals because we want to search across all
  // OriginAttributes.
  for (const auto& entry : mStorage->StateGlobalMapRef()) {
    RefPtr<BounceTrackingStateGlobal> stateGlobal = entry.GetData();
    MOZ_ASSERT(stateGlobal);

    if (stateGlobal->RecentPurgesMapRef().Contains(aSiteHost)) {
      *aResult = true;
      return NS_OK;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::GetRecentPurgedChainEntriesForSite(
    const nsACString& aSiteHost,
    nsTArray<RefPtr<nsIBounceTrackingPurgeEntry>>& aEntries) {
  NS_ENSURE_TRUE(!aSiteHost.IsEmpty(), NS_ERROR_INVALID_ARG);

  nsTArray<RefPtr<BounceTrackingPurgeEntry>> matchingEntries;

  for (const auto& globalEntry : mStorage->StateGlobalMapRef()) {
    RefPtr<BounceTrackingStateGlobal> stateGlobal = globalEntry.GetData();
    MOZ_ASSERT(stateGlobal);

    for (auto iter = stateGlobal->RecentPurgesMapRef().ConstIter();
         !iter.Done(); iter.Next()) {
      for (const auto& entry : iter.Data()) {
        bool matches = iter.Key() == aSiteHost;
        if (!matches) {
          BounceTrackingRecord* record = entry->GetBounceChainRecord();
          if (record) {
            matches = record->InitialSite().SiteHost() == aSiteHost ||
                      record->FinalSite().SiteHost() == aSiteHost ||
                      record->HasBounceSiteHost(aSiteHost);
          }
        }
        if (matches) {
          matchingEntries.InsertElementSorted(entry,
                                              PurgeEntryTimeComparator{});
        }
      }
    }
  }

  aEntries.AppendElements(matchingEntries);
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestGetSiteHostExceptions(
    nsTArray<nsCString>& aSiteHostExceptions) {
  aSiteHostExceptions.Clear();

  for (const auto& host : mRemoteSiteHostExceptions) {
    aSiteHostExceptions.AppendElement(host);
  }

  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestRunPurgeBounceTrackers(
    JSContext* aCx, mozilla::dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aPromise);

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (!globalObject) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(globalObject, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  // PurgeBounceTrackers returns a MozPromise, wrap it in a dom::Promise
  // required for XPCOM.
  PurgeBounceTrackers()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise](const PurgeBounceTrackersMozPromise::ResolveValueType&
                    purgedEntries) {
        // Convert array of BounceTrackingMapEntry to array of site host
        // strings for the JS caller. We can't pass an XPCOM type as the
        // resolver value of a JS Promise.
        nsTArray<nsCString> purgedSitesHosts;
        for (const auto& entry : purgedEntries) {
          purgedSitesHosts.AppendElement(entry->SiteRef().SiteHost());
        }
        promise->MaybeResolve(purgedSitesHosts);
      },
      [promise](const PurgeBounceTrackersMozPromise::RejectValueType& error) {
        promise->MaybeReject(error);
      });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestFlushStorage(JSContext* aCx,
                                           mozilla::dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aPromise);

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (!globalObject) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(globalObject, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  mStorage->TestFlush()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [promise](const GenericPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsResolve()) {
          promise->MaybeResolveWithUndefined();
        } else {
          promise->MaybeReject(aValue.RejectValue());
        }
      });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
BounceTrackingProtection::TestClearExpiredUserActivations() {
  return ClearExpiredUserInteractions();
}

NS_IMETHODIMP
BounceTrackingProtection::TestAddBounceTrackerCandidate(
    JS::Handle<JS::Value> aOriginAttributes, const nsACString& aHost,
    const PRTime aBounceTime, JSContext* aCx) {
  MOZ_ASSERT(aCx);

  OriginAttributes oa;
  if (!aOriginAttributes.isObject() || !oa.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<BounceTrackingStateGlobal> stateGlobal =
      mStorage->GetOrCreateStateGlobal(oa);

  // Ensure aHost is lowercase to match nsIURI and nsIPrincipal.
  nsAutoCString host(aHost);
  ToLowerCase(host);

  // Can not have a host in both maps.
  BounceTrackingSite site = stateGlobal->SiteFor(host);
  nsresult rv = stateGlobal->TestRemoveUserActivation(site);
  NS_ENSURE_SUCCESS(rv, rv);
  return stateGlobal->RecordBounceTracker(site, aBounceTime);
}

NS_IMETHODIMP
BounceTrackingProtection::TestAddUserActivation(
    JS::Handle<JS::Value> aOriginAttributes, const nsACString& aHost,
    const PRTime aActivationTime, JSContext* aCx) {
  MOZ_ASSERT(aCx);

  OriginAttributes oa;
  if (!aOriginAttributes.isObject() || !oa.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<BounceTrackingStateGlobal> stateGlobal =
      mStorage->GetOrCreateStateGlobal(oa);
  MOZ_ASSERT(stateGlobal);

  // Ensure aHost is lowercase to match nsIURI and nsIPrincipal.
  nsAutoCString host(aHost);
  ToLowerCase(host);

  return stateGlobal->RecordUserActivation(stateGlobal->SiteFor(host),
                                           aActivationTime);
}

NS_IMETHODIMP
BounceTrackingProtection::TestMaybeMigrateUserInteractionPermissions() {
  return MaybeMigrateUserInteractionPermissions();
}

// static
nsresult BounceTrackingProtection::LogBounceTrackersClassifiedToWebConsole(
    BounceTrackingState* aBounceTrackingState,
    const nsTArray<nsCString>& aSiteHosts) {
  NS_ENSURE_ARG(aBounceTrackingState);

  // Nothing to log.
  if (aSiteHosts.IsEmpty()) {
    return NS_OK;
  }

  RefPtr<dom::BrowsingContext> browsingContext =
      aBounceTrackingState->CurrentBrowsingContext();
  if (!browsingContext) {
    return NS_OK;
  }

  // Get the localized copy from antiTracking.ftl and insert the variables.
  nsTArray<nsCString> resourceIDs = {"toolkit/global/antiTracking.ftl"_ns};
  RefPtr<intl::Localization> l10n =
      intl::Localization::Create(resourceIDs, true);

  for (const nsACString& siteHost : aSiteHosts) {
    auto l10nArgs = dom::Optional<intl::L10nArgs>();
    l10nArgs.Construct();

    auto siteHostArg = l10nArgs.Value().Entries().AppendElement();
    siteHostArg->mKey = "siteHost";
    siteHostArg->mValue.SetValue().SetAsUTF8String().Assign(siteHost);

    auto gracePeriodArg = l10nArgs.Value().Entries().AppendElement();
    gracePeriodArg->mKey = "gracePeriodSeconds";
    gracePeriodArg->mValue.SetValue().SetAsDouble() = StaticPrefs::
        privacy_bounceTrackingProtection_bounceTrackingGracePeriodSec();

    // Construct the localized string.
    nsAutoCString message;
    ErrorResult errorResult;
    l10n->FormatValueSync("btp-warning-tracker-classified"_ns, l10nArgs,
                          message, errorResult);
    if (NS_WARN_IF(errorResult.Failed())) {
      return errorResult.StealNSResult();
    }

    // Log to the console via nsIScriptError object.
    nsresult rv = NS_OK;
    nsCOMPtr<nsIScriptError> error =
        do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = error->InitWithWindowID(
        NS_ConvertUTF8toUTF16(message), ""_ns, 0, 0,
        nsIScriptError::warningFlag, "bounceTrackingProtection",
        browsingContext->GetCurrentInnerWindowId(), true);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIConsoleService> consoleService =
        do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // The actual log call.
    rv = consoleService->LogMessage(error);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

RefPtr<GenericNonExclusivePromise>
BounceTrackingProtection::EnsureRemoteExceptionListService() {
  // mRemoteExceptionList already initialized or currently initializing.
  if (mRemoteExceptionListInitPromise) {
    return mRemoteExceptionListInitPromise;
  }

  // Create the service instance.
  nsresult rv;
  mRemoteExceptionList =
      do_GetService(NS_NSIBTPEXCEPTIONLISTSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mRemoteExceptionListInitPromise =
        GenericNonExclusivePromise::CreateAndReject(rv, __func__);
    return mRemoteExceptionListInitPromise;
  }

  // Call the init method and get the Promise. It resolves once the allow-list
  // entries have been imported.
  RefPtr<dom::Promise> jsPromise;
  rv = mRemoteExceptionList->Init(this, getter_AddRefs(jsPromise));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mRemoteExceptionListInitPromise =
        GenericNonExclusivePromise::CreateAndReject(rv, __func__);
    return mRemoteExceptionListInitPromise;
  }
  MOZ_ASSERT(jsPromise);

  // Convert to MozPromise so it can be handled from C++ side. Also store the
  // promise so that subsequent calls to this method can wait for init too.
  RefPtr<GenericNonExclusivePromise::Private> resultPromise =
      new GenericNonExclusivePromise::Private(__func__);
  mRemoteExceptionListInitPromise = resultPromise;

  jsPromise->AddCallbacksWithCycleCollectedArgs(
      [resultPromise](JSContext*, JS::Handle<JS::Value>, ErrorResult&) {
        resultPromise->Resolve(true, __func__);
      },
      [resultPromise](JSContext*, JS::Handle<JS::Value>, ErrorResult&) {
        resultPromise->Reject(NS_ERROR_FAILURE, __func__);
      });
  jsPromise->AppendNativeHandler(
      new dom::MozPromiseRejectOnDestruction{resultPromise, __func__});

  return mRemoteExceptionListInitPromise;
}

RefPtr<BounceTrackingProtection::PurgeBounceTrackersMozPromise>
BounceTrackingProtection::PurgeBounceTrackers() {
  // Only purge when the feature is actually enabled.
  if (StaticPrefs::privacy_bounceTrackingProtection_mode() !=
          nsIBounceTrackingProtection::MODE_ENABLED &&
      StaticPrefs::privacy_bounceTrackingProtection_mode() !=
          nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: Skip: Purging disabled via mode pref.", __FUNCTION__);
    return PurgeBounceTrackersMozPromise::CreateAndReject(
        nsresult::NS_ERROR_NOT_AVAILABLE, __func__);
  }

  // Prevent multiple purge operations from running at the same time.
  if (mPurgeInProgress) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: Skip: Purge already in progress.", __FUNCTION__);
    return PurgeBounceTrackersMozPromise::CreateAndReject(
        nsresult::NS_ERROR_NOT_AVAILABLE, __func__);
  }
  mPurgeInProgress = true;

  RefPtr<PurgeBounceTrackersMozPromise::Private> resultPromise =
      new PurgeBounceTrackersMozPromise::Private(__func__);

  RefPtr<BounceTrackingProtection> self = this;

  // Wait for the remote exception list service to be ready before purging.
  EnsureRemoteExceptionListService()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self, resultPromise](
          const GenericNonExclusivePromise::ResolveOrRejectValue& aResult) {
        if (aResult.IsReject()) {
          nsresult rv = aResult.RejectValue();
          self->mPurgeInProgress = false;
          resultPromise->Reject(rv, __func__);
          return;
        }
        // Remote exception list is ready.

        // Obtain a cache of allow-list permissions so we only need to
        // fetch permissions once even when we do multiple base domain lookups.
        BounceTrackingAllowList bounceTrackingAllowList;

        // Collect promises for all clearing operations to later await on.
        nsTArray<RefPtr<ClearDataMozPromise>> clearPromises;

        // Run the purging algorithm for all global state objects.
        for (const auto& entry : self->mStorage->StateGlobalMapRef()) {
          const OriginAttributes& originAttributes = entry.GetKey();
          BounceTrackingStateGlobal* stateGlobal = entry.GetData();
          MOZ_ASSERT(stateGlobal);

          if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
            nsAutoCString oaSuffix;
            originAttributes.CreateSuffix(oaSuffix);
            MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                        "{}: Running purge algorithm for OA: '{}'",
                        __FUNCTION__, oaSuffix);
          }

          nsresult rv = self->PurgeBounceTrackersForStateGlobal(
              stateGlobal, bounceTrackingAllowList, clearPromises);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            self->mPurgeInProgress = false;
            resultPromise->Reject(rv, __func__);
            return;
          }
        }

        // Wait for all data clearing operations to complete. mClearPromises
        // contains one promise per host / clear task.
        ClearDataMozPromise::AllSettled(GetCurrentSerialEventTarget(),
                                        clearPromises)
            ->Then(
                GetCurrentSerialEventTarget(), __func__,
                [resultPromise,
                 self](ClearDataMozPromise::AllSettledPromiseType::
                           ResolveOrRejectValue&& aResults) {
                  MOZ_ASSERT(aResults.IsResolve(), "AllSettled never rejects");

                  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                              "{}: Done. Cleared {} hosts.", __FUNCTION__,
                              aResults.ResolveValue().Length());

                  if (!aResults.ResolveValue().IsEmpty()) {
                    glean::bounce_tracking_protection::num_hosts_per_purge_run
                        .AccumulateSingleSample(
                            aResults.ResolveValue().Length());
                  }

                  // Check if any clear call failed.
                  bool anyFailed = false;

                  nsTArray<RefPtr<BounceTrackingPurgeEntry>> purgedSites;

                  // If any clear call failed reject.
                  for (auto& result : aResults.ResolveValue()) {
                    if (result.IsReject()) {
                      anyFailed = true;
                    } else {
                      purgedSites.AppendElement(result.ResolveValue());
                    }
                  }

                  // Record exposure of the feature for Nimbus
                  // experimentation.
                  // The error result returned by this method isn't very
                  // useful, so we ignore it. Thee method will also return
                  // errors if the client is not enrolled in an experiment
                  // involving BTP which we don't consider a failure state.
                  //
                  // We record exposure for MODE_ENABLED_DRY_RUN in addition to
                  // MODE_ENABLED so we know in Nimbus when a client would have
                  // been exposed to BTP had it been enabled. This enables us to
                  // compare the control and treatment branches with exposure.
                  (void)NimbusFeatures::RecordExposureEvent(
                      "bounceTrackingProtection"_ns, false);

                  if (StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                      nsIBounceTrackingProtection::MODE_ENABLED) {
                    // Log successful purges.
                    for (const auto& entry : purgedSites) {
                      RefPtr<BounceTrackingStateGlobal> stateGlobal =
                          self->mStorage->GetOrCreateStateGlobal(
                              entry->SiteRef().OriginAttributesRef());
                      MOZ_ASSERT(stateGlobal);
                      DebugOnly<nsresult> rv =
                          stateGlobal->RecordPurgedTracker(entry);
                      NS_WARNING_ASSERTION(
                          NS_SUCCEEDED(rv),
                          "Failed to record purged tracker in log.");
                    }

                    if (purgedSites.Length() > 0) {
                      // Record successful purges via nsITrackingDBService for
                      // tracker stats.
                      ReportPurgedTrackersToAntiTrackingDB(purgedSites);
                    }
                  }

                  self->mPurgeInProgress = false;

                  // If any clear call failed reject the promise.
                  if (anyFailed) {
                    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
                    return;
                  }
                  resultPromise->Resolve(std::move(purgedSites), __func__);
                });
      });
  return resultPromise.forget();
}

// static
void BounceTrackingProtection::ReportPurgedTrackersToAntiTrackingDB(
    const nsTArray<RefPtr<BounceTrackingPurgeEntry>>& aPurgedSiteHosts) {
  MOZ_ASSERT(!aPurgedSiteHosts.IsEmpty());
  MOZ_ASSERT(StaticPrefs::privacy_bounceTrackingProtection_mode() ==
             nsIBounceTrackingProtection::MODE_ENABLED);

  ContentBlockingLog log;
  for (const RefPtr<BounceTrackingPurgeEntry>& entry : aPurgedSiteHosts) {
    nsAutoCString origin("https://");
    origin.Append(entry->SiteRef().SiteHost());

    log.RecordLogParent(
        origin, nsIWebProgressListener::STATE_PURGED_BOUNCETRACKER, true);
  }
  log.ReportLog();
}

nsresult BounceTrackingProtection::PurgeBounceTrackersForStateGlobal(
    BounceTrackingStateGlobal* aStateGlobal,
    BounceTrackingAllowList& aBounceTrackingAllowList,
    nsTArray<RefPtr<ClearDataMozPromise>>& aClearPromises) {
  MOZ_ASSERT(aStateGlobal);
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: {}",
              __FUNCTION__, *aStateGlobal);

  // Ensure we only purge when pref configuration allows it.
  if (StaticPrefs::privacy_bounceTrackingProtection_mode() !=
          nsIBounceTrackingProtection::MODE_ENABLED &&
      StaticPrefs::privacy_bounceTrackingProtection_mode() !=
          nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  const PRTime now = PR_Now();

  // 1. Remove hosts from the user activation map whose user activation flag has
  // expired.
  nsresult rv = ClearExpiredUserInteractions(aStateGlobal);
  NS_ENSURE_SUCCESS(rv, rv);

  // 2. Go over bounce tracker candidate map and purge state.

  // Collect hosts to remove from the bounce trackers map. We can not remove
  // them while iterating over the map.
  nsTArray<BounceTrackingSite> bounceTrackerCandidatesToRemove;

  for (auto hostIter = aStateGlobal->BounceTrackersMapRef().ConstIter();
       !hostIter.Done(); hostIter.Next()) {
    BounceTrackingSite site = aStateGlobal->SiteFor(hostIter.Key());
    const nsACString& host = site.SiteHost();
    const auto& candidate = hostIter.Data();
    const PRTime bounceTime = candidate.mBounceTime;

    // If bounceTime + bounce tracking grace period is after now, then continue.
    // The host is still within the grace period and must not be purged.
    if (bounceTime +
            StaticPrefs::
                    privacy_bounceTrackingProtection_bounceTrackingGracePeriodSec() *
                PR_USEC_PER_SEC >
        now) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip host within bounce tracking grace period {}",
                  __FUNCTION__, host);

      continue;
    }

    // If there is a top-level traversable whose active document's origin's
    // site's host equals host, then continue.
    // TODO: Bug 1842047: Implement a more accurate check that calls into the
    // browser implementations to determine whether the site is currently open
    // on the top level.
    bool hostIsActive;
    rv = BounceTrackingState::HasBounceTrackingStateForSite(site, hostIsActive);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      hostIsActive = false;
    }
    if (hostIsActive) {
      MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                  "{}: Skip host which is active {}", __FUNCTION__, host);
      continue;
    }

    // Gecko specific: If the host is on the content blocking allow-list or
    // allow-listed via RemoteSettings continue.
    bool isAllowListed = mRemoteSiteHostExceptions.Contains(host);
    // If remote settings doesn't allowlist also check the content blocking
    // allow-list.
    if (!isAllowListed) {
      rv = aBounceTrackingAllowList.CheckForBaseDomain(
          host, aStateGlobal->OriginAttributesRef(), isAllowListed);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }
    }
    if (isAllowListed) {
      if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
        nsAutoCString originAttributeSuffix;
        aStateGlobal->OriginAttributesRef().CreateSuffix(originAttributeSuffix);
        MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                    "{}: Skip allow-listed: host: {}, originAttributes: {}",
                    __FUNCTION__, host, originAttributeSuffix);
      }
      // Remove allow-listed host so we don't need to check in again next purge
      // run. If it gets classified again and the allow-list entry gets removed
      // it will be purged in the next run.
      bounceTrackerCandidatesToRemove.AppendElement(site);
      continue;
    }

    // No exception above applies, clear state for the given host.
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Info,
                "{}: Purging bounce tracker. siteHost: {}, bounceTime: {} "
                "aStateGlobal: {}",
                __FUNCTION__, host, bounceTime, *aStateGlobal);

    // Remove it from the bounce trackers map, it's about to be purged. If the
    // clear call fails still remove it. We want to avoid an ever growing list
    // of hosts in case of repeated failures.
    bounceTrackerCandidatesToRemove.AppendElement(site);

    RefPtr<ClearDataMozPromise> clearDataPromise;
    rv = PurgeStateForSite(site, bounceTime, candidate.mRecord,
                           getter_AddRefs(clearDataPromise));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }
    MOZ_ASSERT(clearDataPromise);

    aClearPromises.AppendElement(clearDataPromise);
  }

  // Remove hosts from the bounce trackers map which we executed purge calls
  // for.
  return aStateGlobal->RemoveBounceTrackers(bounceTrackerCandidatesToRemove);
}

nsresult BounceTrackingProtection::PurgeStateForSite(
    const BounceTrackingSite& aSite, PRTime bounceTime,
    BounceTrackingRecord* aChainRecord, ClearDataMozPromise** aClearPromise) {
  MOZ_ASSERT(!aSite.IsNull());
  MOZ_ASSERT(aClearPromise);

  RefPtr<ClearDataMozPromise::Private> clearPromise =
      new ClearDataMozPromise::Private(__func__);
  RefPtr<ClearDataCallback> cb =
      new ClearDataCallback(clearPromise, aSite, bounceTime, aChainRecord);

  if (StaticPrefs::privacy_bounceTrackingProtection_mode() ==
      nsIBounceTrackingProtection::MODE_ENABLED_DRY_RUN) {
    // In dry-run mode, we don't actually clear the data, but we still want to
    // resolve the promise to indicate that the data would have been cleared.
    cb->OnDataDeleted(0);

    clearPromise.forget(aClearPromise);
    return NS_OK;
  }

  nsresult rv = NS_OK;
  nsCOMPtr<nsIClearDataService> clearDataService =
      do_GetService("@mozilla.org/clear-data-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // When clearing data for a specific site host we need to ensure that we
  // only clear for matching OriginAttributes. For example if the current
  // state global is private browsing only we must not clear normal browsing
  // data. To restrict clearing we pass in a (stringified)
  // OriginAttributesPattern which matches the state global's
  // OriginAttributes.
  nsAutoString oaPatternString;
  OriginAttributesPattern pattern;

  // partitionKey and firstPartyDomain are omitted making them wildcards. We
  // want to clear across partitions since BTP should clear all data for a
  // given top level.
  const OriginAttributes& originAttributes = aSite.OriginAttributesRef();
  pattern.mUserContextId.Construct(originAttributes.mUserContextId);
  pattern.mPrivateBrowsingId.Construct(originAttributes.mPrivateBrowsingId);
  pattern.mGeckoViewSessionContextId.Construct(
      originAttributes.mGeckoViewSessionContextId);

  NS_ENSURE_TRUE(pattern.ToJSON(oaPatternString), NS_ERROR_FAILURE);

  // XPIDL consts can't use & or ~, so CLEAR_STATE_FOR_TRACKER_PURGING lists
  // its flags explicitly. Catch it drifting from CLEAR_COOKIES_AND_SITE_DATA.
  static_assert(
      nsIClearDataService::CLEAR_STATE_FOR_TRACKER_PURGING ==
          ((nsIClearDataService::CLEAR_ALL_CACHES |
            nsIClearDataService::CLEAR_COOKIES_AND_SITE_DATA |
            nsIClearDataService::CLEAR_CLIENT_AUTH_REMEMBER_SERVICE |
            nsIClearDataService::CLEAR_MEDIA_DEVICES |
            nsIClearDataService::CLEAR_STORAGE_ACCESS) &
           ~nsIClearDataService::CLEAR_BOUNCE_TRACKING_PROTECTION_STATE),
      "CLEAR_STATE_FOR_TRACKER_PURGING is out of sync with "
      "CLEAR_COOKIES_AND_SITE_DATA");

  rv = clearDataService->DeleteDataFromSiteAndOriginAttributesPatternString(
      aSite.SiteHost(), oaPatternString, false,
      // CLEAR_STATE_FOR_TRACKER_PURGING excludes our own state. We already
      // update it ourselves and a nested call to the
      // BounceTrackingProtectionCleaner while iterating over the candidate set
      // may lead to crashes.
      nsIClearDataService::CLEAR_STATE_FOR_TRACKER_PURGING, cb);
  NS_ENSURE_SUCCESS(rv, rv);

  clearPromise.forget(aClearPromise);

  return NS_OK;
}

nsresult BounceTrackingProtection::ClearExpiredUserInteractions(
    BounceTrackingStateGlobal* aStateGlobal) {
  if (!aStateGlobal && mStorage->StateGlobalMapRef().IsEmpty()) {
    // Nothing to clear.
    return NS_OK;
  }

  const PRTime now = PR_Now();

  // Convert the user activation lifetime into microseconds for calculation with
  // PRTime values. The pref is a 32-bit value. Cast into 64-bit before
  // multiplying so we get the correct result.
  int64_t activationLifetimeUsec =
      static_cast<int64_t>(
          StaticPrefs::
              privacy_bounceTrackingProtection_bounceTrackingActivationLifetimeSec()) *
      PR_USEC_PER_SEC;

  // Clear user activation for the given state global.
  if (aStateGlobal) {
    return aStateGlobal->ClearUserActivationBefore(now -
                                                   activationLifetimeUsec);
  }

  // aStateGlobal not passed, clear user activation for all state globals.
  for (const auto& entry : mStorage->StateGlobalMapRef()) {
    const RefPtr<BounceTrackingStateGlobal>& stateGlobal = entry.GetData();
    MOZ_ASSERT(stateGlobal);

    nsresult rv =
        stateGlobal->ClearUserActivationBefore(now - activationLifetimeUsec);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

void BounceTrackingProtection::MaybeLogPurgedWarningForSite(
    const BounceTrackingSite& aSite,
    BounceTrackingState* aBounceTrackingState) {
  NS_ENSURE_TRUE_VOID(!aSite.IsNull());
  NS_ENSURE_TRUE_VOID(aBounceTrackingState);

  // Ensure that the BTS is connected to a BrowsingContext. We will later use
  // this BC to get the DevTools console to log to.
  RefPtr<dom::BrowsingContext> browsingContext =
      aBounceTrackingState->CurrentBrowsingContext();
  if (!browsingContext) {
    return;
  }

  // Check if the site is in the recently purged log. If it is we should log a
  // console warning.
  RefPtr<BounceTrackingStateGlobal> stateGlobal =
      mStorage->GetStateGlobal(aSite.OriginAttributesRef());
  if (!stateGlobal) {
    // No state global which means we don't have any purge data for the given
    // OriginAttributes on record.
    return;
  }

  if (!stateGlobal->RecentPurgesMapRef().Contains(aSite.SiteHost())) {
    // No recent purge found for the given site.
    return;
  }

  // Recently purged the site. Log a warning.

  // Get the localized copy from antiTracking.ftl and insert the variables.
  nsTArray<nsCString> resourceIDs = {"toolkit/global/antiTracking.ftl"_ns};
  RefPtr<intl::Localization> l10n =
      intl::Localization::Create(resourceIDs, true);

  auto l10nArgs = dom::Optional<intl::L10nArgs>();
  l10nArgs.Construct();

  auto siteHostArg = l10nArgs.Value().Entries().AppendElement();
  siteHostArg->mKey = "siteHost";
  siteHostArg->mValue.SetValue().SetAsUTF8String().Assign(aSite.SiteHost());

  // Construct the localized string.
  nsAutoCString message;
  ErrorResult errorResult;
  l10n->FormatValueSync("btp-warning-tracker-purged"_ns, l10nArgs, message,
                        errorResult);
  if (NS_WARN_IF(errorResult.Failed())) {
    return;
  }

  nsresult rv = nsContentUtils::ReportToConsoleByWindowID(
      NS_ConvertUTF8toUTF16(message), nsIScriptError::warningFlag,
      "bounceTrackingProtection"_ns,
      browsingContext->GetCurrentInnerWindowId());

  NS_ENSURE_SUCCESS_VOID(rv);
}

nsresult BounceTrackingProtection::MaybeMigrateUserInteractionPermissions() {
  // Only run the migration once.
  if (StaticPrefs::
          privacy_bounceTrackingProtection_hasMigratedUserActivationData()) {
    return NS_OK;
  }

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Importing user activation data from permissions",
              __FUNCTION__);

  // Get all user activation permissions that are within our user activation
  // lifetime. We don't care about the rest since they are considered expired
  // for BTP.

  nsresult rv = NS_OK;
  nsCOMPtr<nsIPermissionManager> permManager =
      do_GetService(NS_PERMISSIONMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(permManager, NS_ERROR_FAILURE);

  // Construct the since time param. The permission manager expects epoch in
  // miliseconds.
  int64_t nowMS = PR_Now() / PR_USEC_PER_MSEC;
  int64_t activationLifetimeMS =
      static_cast<int64_t>(
          StaticPrefs::
              privacy_bounceTrackingProtection_bounceTrackingActivationLifetimeSec()) *
      PR_MSEC_PER_SEC;
  int64_t since = nowMS - activationLifetimeMS;
  MOZ_ASSERT(since > 0);

  // Get all user activation permissions last modified between "since" and now.
  nsTArray<RefPtr<nsIPermission>> userActivationPermissions;
  rv = permManager->GetAllByTypeSince("storageAccessAPI"_ns, since,
                                      userActivationPermissions);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Found {} (non-expired) user activation permissions",
              __FUNCTION__, userActivationPermissions.Length());

  for (const auto& perm : userActivationPermissions) {
    nsCOMPtr<nsIPrincipal> permPrincipal;

    rv = perm->GetPrincipal(getter_AddRefs(permPrincipal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }
    MOZ_ASSERT(permPrincipal);

    // The time the permission was last modified is the time of last user
    // activation.
    int64_t modificationTimeMS;
    rv = perm->GetModificationTime(&modificationTimeMS);
    NS_ENSURE_SUCCESS(rv, rv);
    MOZ_ASSERT(modificationTimeMS >= since,
               "Unexpected permission modification time");

    // We may end up with duplicates here since user activation permissions are
    // tracked by origin, while BTP tracks user activation by site host.
    // RecordUserActivation is responsible for only keeping the most recent user
    // activation flag for a given site host and needs to make sure existing
    // activation flags are not overwritten by older timestamps.
    // RecordUserActivation expects epoch in microseconds.
    rv = RecordUserActivation(permPrincipal,
                              Some(modificationTimeMS * PR_USEC_PER_MSEC));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }
  }

  // Migration successful, set the pref to indicate that we have migrated.
  return mozilla::Preferences::SetBool(
      "privacy.bounceTrackingProtection.hasMigratedUserActivationData", true);
}

}  // namespace mozilla
