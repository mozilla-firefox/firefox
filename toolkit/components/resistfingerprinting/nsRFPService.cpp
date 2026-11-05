/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "nsRFPService.h"

#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <utility>
#include <mutex>

#include "MainThreadUtils.h"
#include "ScopedNSSTypes.h"

#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/ArrayIterator.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Components.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/glean/ResistfingerprintingMetrics.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/Likely.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Services.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SSE.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanvasRenderingContextHelper.h"
#include "mozilla/dom/CanvasRenderingContext2D.h"
#include "mozilla/dom/CanvasUtils.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/MediaDeviceInfoBinding.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/XorShift128PlusRNG.h"

#include "nsAboutProtocolUtils.h"
#include "nsBaseHashtable.h"
#include "nsComponentManagerUtils.h"
#include "nsCRT.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsCoord.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsHashKeys.h"
#include "nsJSUtils.h"
#include "nsLiteralString.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsStringFlags.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"
#include "nsTPromiseFlatString.h"
#include "nsTStringRepr.h"
#include "nsUserCharacteristics.h"
#include "nsXPCOM.h"
#include "nsRFPTargetSetIDL.h"

#include "nsICookieJarSettings.h"
#include "nsICryptoHash.h"
#include "nsIEffectiveTLDService.h"
#include "nsIGlobalObject.h"
#include "nsILoadInfo.h"
#include "nsIObserverService.h"
#include "nsIRandomGenerator.h"
#include "nsIScriptSecurityManager.h"
#include "nsIUserIdleService.h"
#include "nsIWebProgressListener.h"
#include "nsIXULAppInfo.h"

#include "nscore.h"
#include "prenv.h"
#include "prtime.h"
#include "xpcpublic.h"

#include "js/Date.h"

using namespace mozilla;

static mozilla::LazyLogModule gResistFingerprintingLog(
    "nsResistFingerprinting");

static mozilla::LazyLogModule gFingerprinterDetection("FingerprinterDetection");

static mozilla::LazyLogModule gTimestamps("Timestamps");

#define RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_BASE_PREF \
  "privacy.baselineFingerprintingProtection.overrides"
#define RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_PREF \
  "privacy.fingerprintingProtection.overrides"
#define GLEAN_DATA_SUBMISSION_PREF "datareporting.healthreport.uploadEnabled"
#define USER_CHARACTERISTICS_UUID_PREF \
  "toolkit.telemetry.user_characteristics_ping.uuid"
#define INTL_ACCEPT_LANGUAGES_PREF "intl.accept_languages"

#define RFP_TIMER_UNCONDITIONAL_VALUE 20
#define LAST_PB_SESSION_EXITED_TOPIC "last-pb-context-exited"
#define IDLE_TOPIC "browser-idle-startup-tasks-finished"
#define COMPOSITOR_CREATED "compositor:created"
#define FONT_LIST_INITIALIZED "font-list-initialized"
#define USER_CHARACTERISTICS_TEST_REQUEST \
  "user-characteristics-testing-please-populate-data"
#define MOBILE_TELEMETRY_PREF_CHANGE_TOPIC "mobile-telemetry-pref-changed"

static constexpr uint32_t kVideoFramesPerSec = 30;
static constexpr uint32_t kVideoDroppedRatio = 1;

#define RFP_DEFAULT_SPOOFING_KEYBOARD_LANG KeyboardLang::EN
#define RFP_DEFAULT_SPOOFING_KEYBOARD_REGION KeyboardRegion::US

#define FP_OVERRIDES_DOMAIN_KEY_DELIMITER ','

// Fingerprinting protections that are enabled by default. This can be
// overridden using the privacy.fingerprintingProtection.overrides pref.
// NOLINTBEGIN(bugprone-macro-parentheses)
#if defined(MOZ_WIDGET_ANDROID)
#  define ANDROID_DEFAULT(name) RFPTarget::name,
#  define DESKTOP_DEFAULT(name)
#else
#  define ANDROID_DEFAULT(name)
#  define DESKTOP_DEFAULT(name) RFPTarget::name,
#endif

#if defined(MOZ_WIDGET_ANDROID) && !defined(NIGHTLY_BUILD)
constinit
#else
MOZ_RUNINIT
#endif
    const RFPTargetSet kDefaultFingerprintingProtectionsBase = {
#include "RFPTargetsDefaultBaseline.inc"
};

MOZ_RUNINIT const RFPTargetSet kDefaultFingerprintingProtections = {
#include "RFPTargetsDefault.inc"
};

#undef ANDROID_DEFAULT
#undef DESKTOP_DEFAULT
// NOLINTEND(bugprone-macro-parentheses)

// ============================================================================
// ============================================================================
// ============================================================================
// Structural Stuff & Pref Observing

NS_IMPL_ISUPPORTS(nsRFPService, nsIObserver, nsIRFPService)

static StaticRefPtr<nsRFPService> sRFPService;
static bool sInitialized = false;
static inline StaticAutoPtr<nsTArray<nsCString>> sAllowedFonts;

// Actually enabled fingerprinting protections.
static StaticMutex sEnabledFingerprintingProtectionsMutex;
constinit static RFPTargetSet sEnabledFingerprintingProtectionsBase
    MOZ_GUARDED_BY(sEnabledFingerprintingProtectionsMutex);
constinit static RFPTargetSet sEnabledFingerprintingProtections
    MOZ_GUARDED_BY(sEnabledFingerprintingProtectionsMutex);

/* static */
already_AddRefed<nsRFPService> nsRFPService::GetOrCreate() {
  if (!sInitialized) {
    sRFPService = new nsRFPService();
    nsresult rv = sRFPService->Init();

    if (NS_FAILED(rv)) {
      sRFPService = nullptr;
      return nullptr;
    }

    ClearOnShutdown(&sRFPService);
    sInitialized = true;
  }

  return do_AddRef(sRFPService);
}

static const char* gCallbackPrefs[] = {
    RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_BASE_PREF,
    RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_PREF,
    GLEAN_DATA_SUBMISSION_PREF,
    nullptr,
};

nsresult nsRFPService::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  NS_ENSURE_TRUE(obs, NS_ERROR_NOT_AVAILABLE);

  rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  NS_ENSURE_SUCCESS(rv, rv);

  if (XRE_IsParentProcess()) {
    rv = obs->AddObserver(this, LAST_PB_SESSION_EXITED_TOPIC, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, OBSERVER_TOPIC_IDLE_DAILY, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, IDLE_TOPIC, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, COMPOSITOR_CREATED, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, FONT_LIST_INITIALIZED, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, USER_CHARACTERISTICS_TEST_REQUEST, false);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = obs->AddObserver(this, MOBILE_TELEMETRY_PREF_CHANGE_TOPIC, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  Preferences::RegisterCallbacks(nsRFPService::PrefChanged, gCallbackPrefs,
                                 this);

  JS::SetReduceMicrosecondTimePrecisionCallback(
      nsRFPService::ReduceTimePrecisionAsUSecsWrapper);

  // Called from here to get the initial list of enabled fingerprinting
  // protections.
  UpdateFPPOverrideList();

  return rv;
}

// Very simple helper functions to improve readability.

/* static */
bool nsRFPService::IsRFPPrefEnabled(bool aIsPrivateMode) {
  return StaticPrefs::privacy_resistFingerprinting_DoNotUseDirectly() ||
         (aIsPrivateMode &&
          StaticPrefs::privacy_resistFingerprinting_pbmode_DoNotUseDirectly());
}

/* static */
bool IsBaselineFPPEnabled() {
  return StaticPrefs::
      privacy_baselineFingerprintingProtection_DoNotUseDirectly();
}

/* static */
bool IsFPPEnabled(bool aIsPrivateMode) {
  return StaticPrefs::privacy_fingerprintingProtection_DoNotUseDirectly() ||
         (aIsPrivateMode &&
          StaticPrefs::
              privacy_fingerprintingProtection_pbmode_DoNotUseDirectly());
}

nsRFPService::FingerprintingProtectionType
nsRFPService::GetFingerprintingProtectionType(bool aIsPrivateMode) {
  if (nsRFPService::IsRFPPrefEnabled(aIsPrivateMode)) {
    return FingerprintingProtectionType::RFP;
  }

  if (IsFPPEnabled(aIsPrivateMode)) {
    return FingerprintingProtectionType::FPP;
  }

  if (IsBaselineFPPEnabled()) {
    return FingerprintingProtectionType::Baseline;
  }

  return FingerprintingProtectionType::None;
}

Maybe<bool> nsRFPService::HandleExceptionalRFPTargets(
    RFPTarget aTarget, bool aIsPrivateMode,
    FingerprintingProtectionType aMode) {
  MOZ_ASSERT(GetFingerprintingProtectionType(aIsPrivateMode) !=
             FingerprintingProtectionType::None);

  // IsAlwaysEnabledForPrecompute is used to enable fingerprinting protections.
  // It isn't included in the RFPTargetSet.
  if (aTarget == RFPTarget::IsAlwaysEnabledForPrecompute) {
    return Some(true);
  }

  // We only spoof language if the user has explicitly agreed to the
  // prompt we show when privacy.spoof_english is set to 0.
  if (aTarget == RFPTarget::JSLocale) {
    return Some(IsTargetActiveForMode(aTarget, aMode) &&
                StaticPrefs::privacy_spoof_english_DoNotUseDirectly() == 2);
  }

  return Nothing();
}

bool nsRFPService::IsTargetActiveForMode(RFPTarget aTarget,
                                         FingerprintingProtectionType aMode) {
  StaticMutexAutoLock lock(sEnabledFingerprintingProtectionsMutex);
  switch (aMode) {
    case FingerprintingProtectionType::FPP:
      return sEnabledFingerprintingProtections.contains(aTarget);
    case FingerprintingProtectionType::Baseline:
      return sEnabledFingerprintingProtectionsBase.contains(aTarget);
    case FingerprintingProtectionType::RFP:
      return true;
    default:
      MOZ_CRASH("Unexpected FingerprintingProtectionType");
      return false;
  }
}

/* static */
bool nsRFPService::IsRFPEnabledFor(
    bool aIsPrivateMode, RFPTarget aTarget,
    const Maybe<RFPTargetSet>& aOverriddenFingerprintingSettings) {
  MOZ_ASSERT(aTarget != RFPTarget::AllTargets);

  FingerprintingProtectionType mode =
      GetFingerprintingProtectionType(aIsPrivateMode);
  if (mode == FingerprintingProtectionType::None) {
    return false;
  }

  if (Maybe<bool> result =
          HandleExceptionalRFPTargets(aTarget, aIsPrivateMode, mode)) {
    return *result;
  }

  if (mode == FingerprintingProtectionType::RFP) {
    return true;
  }

  if (aOverriddenFingerprintingSettings) {
    return aOverriddenFingerprintingSettings.ref().contains(aTarget);
  }

  return IsTargetActiveForMode(aTarget, mode);
}

void nsRFPService::UpdateFPPOverrideList() {
  StaticMutexAutoLock lock(sEnabledFingerprintingProtectionsMutex);
  std::tuple<const char*, RFPTargetSet&, const RFPTargetSet&> prefs[] = {
      {RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_PREF,
       sEnabledFingerprintingProtections, kDefaultFingerprintingProtections},
      {RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_BASE_PREF,
       sEnabledFingerprintingProtectionsBase,
       kDefaultFingerprintingProtectionsBase},
  };

  for (const auto& [pref, targetSet, defaultSet] : prefs) {
    nsAutoString targetOverrides;
    nsresult rv = Preferences::GetString(pref, targetOverrides);
    if (NS_FAILED(rv)) {
      MOZ_LOG(gResistFingerprintingLog, LogLevel::Warning,
              ("Could not get fingerprinting override pref (%s) value", pref));
      continue;
    }

    targetSet = CreateOverridesFromText(targetOverrides, defaultSet);
  }
}

/* static */
Maybe<RFPTarget> nsRFPService::TextToRFPTarget(const nsAString& aText) {
#define ITEM_VALUE(name, value)     \
  if (aText.EqualsLiteral(#name)) { \
    return Some(RFPTarget::name);   \
  }

#include "RFPTargets.inc"
#undef ITEM_VALUE

  return Nothing();
}

void nsRFPService::StartShutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();

  if (obs) {
    obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    if (XRE_IsParentProcess()) {
      obs->RemoveObserver(this, LAST_PB_SESSION_EXITED_TOPIC);
      obs->RemoveObserver(this, OBSERVER_TOPIC_IDLE_DAILY);
      obs->RemoveObserver(this, IDLE_TOPIC);
      obs->RemoveObserver(this, COMPOSITOR_CREATED);
      obs->RemoveObserver(this, FONT_LIST_INITIALIZED);
      obs->RemoveObserver(this, USER_CHARACTERISTICS_TEST_REQUEST);
      obs->RemoveObserver(this, MOBILE_TELEMETRY_PREF_CHANGE_TOPIC);
    }
  }

  if (mWebCompatService) {
    mWebCompatService->Shutdown();
  }

  Preferences::UnregisterCallbacks(nsRFPService::PrefChanged, gCallbackPrefs,
                                   this);
}

// static
void nsRFPService::PrefChanged(const char* aPref, void* aSelf) {
  static_cast<nsRFPService*>(aSelf)->PrefChanged(aPref);
}

void nsRFPService::PrefChanged(const char* aPref) {
  MOZ_LOG(gResistFingerprintingLog, LogLevel::Info,
          ("Pref Changed: %s", aPref));
  nsDependentCString pref(aPref);

  if (pref.EqualsLiteral(RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_PREF) ||
      pref.EqualsLiteral(RESIST_FINGERPRINTINGPROTECTION_OVERRIDE_BASE_PREF)) {
    UpdateFPPOverrideList();

    // Granular overrides are applied on top of the global set of enabled
    // targets, which just changed. Ask the WebCompat service to send them
    // again so they are recomputed against the new global set. This is done
    // here, rather than by having the service observe the pref itself, to
    // guarantee that the global set is updated first.
    if (XRE_IsParentProcess() && mWebCompatService) {
      DebugOnly<nsresult> rv = mWebCompatService->RefreshOverrides();
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "Failed to refresh the fingerprinting overrides");
    }
  } else if (pref.EqualsLiteral(GLEAN_DATA_SUBMISSION_PREF)) {
    if (XRE_IsParentProcess() &&
        !Preferences::GetBool(GLEAN_DATA_SUBMISSION_PREF, false)) {
      MOZ_LOG(gResistFingerprintingLog, LogLevel::Info, ("Clearing UUID"));
      // If the user has unset the telemetry pref, wipe out the UUID pref value
      // (The data will also be erased server-side via the "deletion-request"
      // ping)
      Preferences::SetCString(USER_CHARACTERISTICS_UUID_PREF, ""_ns);
    }
  }
}

NS_IMETHODIMP
nsRFPService::Observe(nsISupports* aObject, const char* aTopic,
                      const char16_t* aMessage) {
  static uint8_t bitsetForUserCharacteristics = 0;

  if (strcmp(NS_XPCOM_SHUTDOWN_OBSERVER_ID, aTopic) == 0) {
    StartShutdown();
  }

  if (strcmp(LAST_PB_SESSION_EXITED_TOPIC, aTopic) == 0) {
    // Clear the private session key when the private session ends so that we
    // can generate a new key for the new private session.
    OriginAttributesPattern pattern;
    pattern.mPrivateBrowsingId.Construct(1);
    ClearBrowsingSessionKey(pattern);
  }

  if (!strcmp(IDLE_TOPIC, aTopic)) {
    bitsetForUserCharacteristics |= 1 << 0;
  } else if (!strcmp(COMPOSITOR_CREATED, aTopic)) {
    bitsetForUserCharacteristics |= 1 << 1;
  } else if (!strcmp(FONT_LIST_INITIALIZED, aTopic)) {
    bitsetForUserCharacteristics |= 1 << 2;
  }

  if (bitsetForUserCharacteristics == 0b111) {
    nsUserCharacteristics::MaybeSubmitPing();
    // Set 4th bit to 1 to make sure bitset is never 0b111 again.
    // This is to ensure that we only submit the ping once.
    bitsetForUserCharacteristics |= 1 << 3;
  }

  if (!strcmp(USER_CHARACTERISTICS_TEST_REQUEST, aTopic) &&
      xpc::IsInAutomation()) {
    nsUserCharacteristics::PopulateDataAndEventuallySubmit(
        /* aUpdatePref = */ false, /* aTesting = */ true);
  }

  if (!strcmp(OBSERVER_TOPIC_IDLE_DAILY, aTopic)) {
    if (StaticPrefs::
            privacy_resistFingerprinting_randomization_daily_reset_enabled()) {
      OriginAttributesPattern pattern;
      pattern.mPrivateBrowsingId.Construct(
          nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID);
      ClearBrowsingSessionKey(pattern);
    }

    if (StaticPrefs::
            privacy_resistFingerprinting_randomization_daily_reset_private_enabled()) {
      OriginAttributesPattern pattern;
      pattern.mPrivateBrowsingId.Construct(1);
      ClearBrowsingSessionKey(pattern);
    }
  }

  if (nsCRT::strcmp(aTopic, "profile-after-change") == 0 &&
      XRE_IsParentProcess()) {
    // Get the singleton of the remote override service if we are in the parent
    // process.
    nsresult rv;
    mWebCompatService =
        do_GetService(NS_FINGERPRINTINGWEBCOMPATSERVICE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mWebCompatService->Init();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!strcmp(MOBILE_TELEMETRY_PREF_CHANGE_TOPIC, aTopic) &&
      nsDependentString(aMessage).EqualsLiteral(u"disabled")) {
    // If the user has unset the telemetry pref, wipe out the UUID pref value
    // (The data will also be erased server-side via the "deletion-request"
    // ping)
    Preferences::SetCString(USER_CHARACTERISTICS_UUID_PREF, ""_ns);
  }

  return NS_OK;
}

// ============================================================================
// ============================================================================
// ============================================================================
// Reduce Timer Precision Stuff

constexpr double RFP_TIME_ATOM_MS = 16.667;  // 60Hz, 1000/60 but rounded.
/*
In RFP RAF always runs at 60Hz, so we're ~0.02% off of 1000/60 here.
```js
extra_frames_per_frame = 16.667 / (1000/60) - 1 // 0.00028
sec_per_extra_frame = 1 / (extra_frames_per_frame * 60) // 833.33
min_per_extra_frame = sec_per_extra_frame / 60 // 13.89
```
We expect an extra frame every ~14 minutes, which is enough to be smooth.
16.67 would be ~1.4 minutes, which is OK, but is more noticeable.
Put another way, if this is the only unacceptable hitch you have across 14
minutes, I'm impressed, and we might revisit this.
*/

/* static */
double nsRFPService::TimerResolution(RTPCallerType aRTPCallerType) {
  double prefValue = StaticPrefs::
      privacy_resistFingerprinting_reduceTimerPrecision_microseconds();
  if (aRTPCallerType == RTPCallerType::ResistFingerprinting) {
    return std::max(RFP_TIME_ATOM_MS * 1000.0, prefValue);
  }
  return prefValue;
}

/**
 * The purpose of this function is to deterministicly generate a random midpoint
 * between a lower clamped value and an upper clamped value. Assuming a clamping
 * resolution of 100, here is an example:
 *
 * |---------------------------------------|--------------------------|
 * lower clamped value (e.g. 300)          |           upper clamped value (400)
 *                              random midpoint (e.g. 360)
 *
 * If our actual timestamp (e.g. 325) is below the midpoint, we keep it clamped
 * downwards. If it were equal to or above the midpoint (e.g. 365) we would
 * round it upwards to the largest clamped value (in this example: 400).
 *
 * The question is: does time go backwards?
 *
 * The midpoint is deterministicly random and generated from three components:
 * a secret seed, a per-timeline (context) 'mix-in', and a clamped time.
 *
 * When comparing times across different seed values: time may go backwards.
 * For a clamped time of 300, one seed may generate a midpoint of 305 and
 * another 395. So comparing an (actual) timestamp of 325 and 351 could see the
 * 325 clamped up to 400 and the 351 clamped down to 300. The seed is
 * per-process, so this case occurs when one can compare timestamps
 * cross-process. This is uncommon (because we don't have site isolation.) The
 * circumstances this could occur are BroadcastChannel, Storage Notification,
 * and in theory (but not yet implemented) SharedWorker. This should be an
 * exhaustive list (at time of comment writing!).
 *
 * Aside from cross-process communication, derived timestamps across different
 * time origins may go backwards. (Specifically, derived means adding two
 * timestamps together to get an (approximate) absolute time.)
 * Assume a page and a worker. If one calls performance.now() in the page and
 * then triggers a call to performance.now() in the worker, the following
 * invariant should hold true:
 *             page.performance.timeOrigin + page.performance.now() <
 *                      worker.performance.timeOrigin + worker.performance.now()
 *
 * We break this invariant.
 *
 * The 'Context Mix-in' is a securely generated random seed that is unique for
 * each timeline that starts over at zero. It is needed to ensure that the
 * sequence of midpoints (as calculated by the secret seed and clamped time)
 * does not repeat. In RelativeTimeline.h, we define a 'RelativeTimeline' class
 * that can be inherited by any object that has a relative timeline. The most
 * obvious examples are Documents and Workers. An attacker could let time go
 * forward and observe (roughly) where the random midpoints fall. Then they
 * create a new object, time starts back over at zero, and they know
 * (approximately) where the random midpoints are.
 *
 * When the timestamp given is a non-relative timestamp (e.g. it is relative to
 * the unix epoch) it is not possible to replay a sequence of random values.
 * Thus, providing a zero context pointer is an indicator that the timestamp
 * given is absolute and does not need any additional randomness.
 *
 * @param aClampedTimeUSec [in]  The clamped input time in microseconds.
 * @param aResolutionUSec  [in]  The current resolution for clamping in
 *                               microseconds.
 * @param aMidpointOut     [out] The midpoint, in microseconds, between [0,
 *                               aResolutionUSec].
 * @param aContextMixin    [in]  An opaque random value for relative
 *                               timestamps. 0 for absolute timestamps
 * @param aSecretSeed      [in]  TESTING ONLY. When provided, the current seed
 *                               will be replaced with this value.
 * @return                 A nsresult indicating success of failure. If the
 *                         function failed, nothing is written to aMidpointOut
 */

/* static */
nsresult nsRFPService::RandomMidpoint(long long aClampedTimeUSec,
                                      long long aResolutionUSec,
                                      int64_t aContextMixin,
                                      long long* aMidpointOut,
                                      uint8_t* aSecretSeed /* = nullptr */) {
  nsresult rv;
  const int kSeedSize = 16;
  static Atomic<uint8_t*> sSecretMidpointSeed;

  if (MOZ_UNLIKELY(!aMidpointOut)) {
    return NS_ERROR_INVALID_ARG;
  }

  /*
   * Below, we will use three different values to seed a fairly simple random
   * number generator. On the first run we initiate the secret seed, which
   * is mixed in with the time epoch and the context mix in to seed the RNG.
   *
   * This isn't the most secure method of generating a random midpoint but is
   * reasonably performant and should be sufficient for our purposes.
   */

  // If we don't have a seed, we need to get one.
  if (MOZ_UNLIKELY(!sSecretMidpointSeed)) {
    nsCOMPtr<nsIRandomGenerator> randomGenerator =
        do_GetService("@mozilla.org/security/random-generator;1", &rv);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    uint8_t* temp = nullptr;
    rv = randomGenerator->GenerateRandomBytes(kSeedSize, &temp);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    if (MOZ_UNLIKELY(!sSecretMidpointSeed.compareExchange(nullptr, temp))) {
      // Some other thread initted this first, never mind!
      free(temp);
    }
  }

  // sSecretMidpointSeed is now set, and invariant. The contents of the buffer
  // it points to is also invariant, _unless_ this function is called with a
  // non-null |aSecretSeed|.
  uint8_t* seed = sSecretMidpointSeed;
  MOZ_RELEASE_ASSERT(seed);

  // If someone has passed in the testing-only parameter, replace our seed with
  // it. We do _not_ re-allocate the buffer, since that can lead to UAF below.
  // The math could still be racy if the caller supplies a new secret seed while
  // some other thread is calling this function, but since this is arcane
  // test-only functionality that is used in only one test-case presently, we
  // put the burden of using this particular footgun properly on the test code.
  if (MOZ_UNLIKELY(aSecretSeed != nullptr)) {
    memcpy(seed, aSecretSeed, kSeedSize);
  }

  // Seed and create our random number generator.
  non_crypto::XorShift128PlusRNG rng(aContextMixin ^ *(uint64_t*)(seed),
                                     aClampedTimeUSec ^ *(uint64_t*)(seed + 8));

  // Retrieve the output midpoint value.
  if (MOZ_UNLIKELY(aResolutionUSec <= 0)) {  // ??? Bug 1718066
    return NS_ERROR_FAILURE;
  }
  *aMidpointOut = rng.next() % aResolutionUSec;

  return NS_OK;
}

/**
 * Given a precision value, this function will reduce a given input time to the
 * nearest multiple of that precision.
 *
 * It will check if it is appropriate to clamp the input time according to the
 * values of the given TimerPrecisionType.  Note that if one desires a minimum
 * precision for Resist Fingerprinting, it is the caller's responsibility to
 * provide the correct value. This means you should pass TimerResolution(),
 * which enforces a minimum value on the precision based on preferences.
 *
 * It ensures the given precision value is greater than zero, if it is not it
 * returns the input time.
 *
 * While the correct thing to pass is TimerResolution() we expose it as an
 * argument for testing purposes only.
 *
 * @param aTime           [in] The input time to be clamped.
 * @param aTimeScale      [in] The units the input time is in (Seconds,
 *                             Milliseconds, or Microseconds).
 * @param aResolutionUSec [in] The precision (in microseconds) to clamp to.
 * @param aContextMixin   [in] An opaque random value for relative timestamps.
 *                             0 for absolute timestamps
 * @return                 If clamping is appropriate, the clamped value of the
 *                         input, otherwise the input.
 */
/* static */
double nsRFPService::ReduceTimePrecisionImpl(double aTime, TimeScale aTimeScale,
                                             double aResolutionUSec,
                                             int64_t aContextMixin,
                                             TimerPrecisionType aType) {
  if (aType == TimerPrecisionType::DangerouslyNone) {
    return aTime;
  }

  // This boolean will serve as a flag indicating we are clamping the time
  // unconditionally. We do this when timer reduction preference is off; but we
  // still want to apply 20us clamping to al timestamps to avoid leaking
  // nano-second precision.
  bool unconditionalClamping = false;
  if (aType == UnconditionalAKAHighRes || aResolutionUSec <= 0) {
    unconditionalClamping = true;
    aResolutionUSec = RFP_TIMER_UNCONDITIONAL_VALUE;  // 20 microseconds
    aContextMixin = 0;  // Just clarifies our logging statement at the end,
                        // otherwise unused
  }

  // Increase the time as needed until it is in microseconds.
  // Note that a double can hold up to 2**53 with integer precision. This gives
  // us only until June 5, 2255 in time-since-the-epoch with integer precision.
  // So we will be losing microseconds precision after that date.
  // We think this is okay, and we codify it in some tests.
  double timeScaled = aTime * (1000000 / aTimeScale);
  // Cut off anything less than a microsecond.
  long long timeAsInt = timeScaled;

  // If we have a blank context mixin, this indicates we (should) have an
  // absolute timestamp. We check the time, and if it less than a unix timestamp
  // about 10 years in the past, we output to the log and, in debug builds,
  // assert. This is an error case we want to understand and fix: we must have
  // given a relative timestamp with a mixin of 0 which is incorrect. Anyone
  // running a debug build _probably_ has an accurate clock, and if they don't,
  // they'll hopefully find this message and understand why things are crashing.
  const long long kFeb282008 = 1204233985000;
  if (aContextMixin == 0 && timeAsInt < kFeb282008 && !unconditionalClamping &&
      aType != TimerPrecisionType::RFP) {
    nsAutoCString type;
    TypeToText(aType, type);
    MOZ_LOG(
        gTimestamps, LogLevel::Error,
        ("About to assert. aTime=%lli<%lli aContextMixin=%" PRId64 " aType=%s",
         timeAsInt, kFeb282008, aContextMixin, type.get()));
    MOZ_ASSERT(false,
               "ReduceTimePrecisionImpl was given a relative time "
               "with an empty context mix-in (or your clock is 10+ years off.) "
               "Run this with MOZ_LOG=Timestamps:1 to get more details.");
  }

  // Cast the resolution (in microseconds) to an int.
  long long resolutionAsInt = aResolutionUSec;
  // Perform the clamping.
  // We do a cast back to double to perform the division with doubles, then
  // floor the result and the rest occurs with integer precision. This is
  // because it gives consistency above and below zero. Above zero, performing
  // the division in integers truncates decimals, taking the result closer to
  // zero (a floor). Below zero, performing the division in integers truncates
  // decimals, taking the result closer to zero (a ceil). The impact of this is
  // that comparing two clamped values that should be related by a constant
  // (e.g. 10s) that are across the zero barrier will no longer work. We need to
  // round consistently towards positive infinity or negative infinity (we chose
  // negative.) This can't be done with a truncation, it must be done with
  // floor.
  long long clamped =
      floor(double(timeAsInt) / resolutionAsInt) * resolutionAsInt;

  long long midpoint = 0;
  long long clampedAndJittered = clamped;
  if (!unconditionalClamping &&
      StaticPrefs::privacy_resistFingerprinting_reduceTimerPrecision_jitter()) {
    if (!NS_FAILED(RandomMidpoint(clamped, resolutionAsInt, aContextMixin,
                                  &midpoint)) &&
        timeAsInt >= clamped + midpoint) {
      clampedAndJittered += resolutionAsInt;
    }
  }

  // Cast it back to a double and reduce it to the correct units.
  double ret = double(clampedAndJittered) / (1000000.0 / double(aTimeScale));

  MOZ_LOG(
      gTimestamps, LogLevel::Verbose,
      ("Given: (%.*f, Scaled: %.*f, Converted: %lli), Rounding %s with (%lli, "
       "Originally %.*f), "
       "Intermediate: (%lli), Clamped: (%lli) Jitter: (%i Context: %" PRId64
       " Midpoint: %lli) "
       "Final: (%lli Converted: %.*f)",
       DBL_DIG - 1, aTime, DBL_DIG - 1, timeScaled, timeAsInt,
       (unconditionalClamping ? "unconditionally" : "normally"),
       resolutionAsInt, DBL_DIG - 1, aResolutionUSec,
       (long long)floor(double(timeAsInt) / resolutionAsInt), clamped,
       StaticPrefs::privacy_resistFingerprinting_reduceTimerPrecision_jitter(),
       aContextMixin, midpoint, clampedAndJittered, DBL_DIG - 1, ret));

  return ret;
}

/* static */
double nsRFPService::ReduceTimePrecisionAsUSecs(double aTime,
                                                int64_t aContextMixin,
                                                RTPCallerType aRTPCallerType) {
  const auto type = GetTimerPrecisionType(aRTPCallerType);
  return nsRFPService::ReduceTimePrecisionImpl(aTime, MicroSeconds,
                                               TimerResolution(aRTPCallerType),
                                               aContextMixin, type);
}

/* static */
double nsRFPService::ReduceTimePrecisionAsMSecs(double aTime,
                                                int64_t aContextMixin,
                                                RTPCallerType aRTPCallerType) {
  const auto type = GetTimerPrecisionType(aRTPCallerType);
  return nsRFPService::ReduceTimePrecisionImpl(aTime, MilliSeconds,
                                               TimerResolution(aRTPCallerType),
                                               aContextMixin, type);
}

/* static */
double nsRFPService::ReduceTimePrecisionAsMSecsRFPOnly(
    double aTime, int64_t aContextMixin, RTPCallerType aRTPCallerType) {
  return nsRFPService::ReduceTimePrecisionImpl(
      aTime, MilliSeconds, TimerResolution(aRTPCallerType), aContextMixin,
      GetTimerPrecisionTypeRFPOnly(aRTPCallerType));
}

/* static */
double nsRFPService::ReduceTimePrecisionAsSecs(double aTime,
                                               int64_t aContextMixin,
                                               RTPCallerType aRTPCallerType) {
  const auto type = GetTimerPrecisionType(aRTPCallerType);
  return nsRFPService::ReduceTimePrecisionImpl(
      aTime, Seconds, TimerResolution(aRTPCallerType), aContextMixin, type);
}

/* static */
double nsRFPService::ReduceTimePrecisionAsSecsRFPOnly(
    double aTime, int64_t aContextMixin, RTPCallerType aRTPCallerType) {
  return nsRFPService::ReduceTimePrecisionImpl(
      aTime, Seconds, TimerResolution(aRTPCallerType), aContextMixin,
      GetTimerPrecisionTypeRFPOnly(aRTPCallerType));
}

/* static */
double nsRFPService::ReduceTimePrecisionAsUSecsWrapper(
    double aTime, JS::RTPCallerTypeToken aCallerType, JSContext* aCx) {
  MOZ_ASSERT(aCx);

#ifdef DEBUG
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  MOZ_ASSERT(global->GetRTPCallerType() == RTPCallerTypeFromToken(aCallerType));
#endif

  RTPCallerType callerType = RTPCallerTypeFromToken(aCallerType);
  return nsRFPService::ReduceTimePrecisionImpl(
      aTime, MicroSeconds, TimerResolution(callerType),
      0, /* For absolute timestamps (all the JS engine does), supply zero
            context mixin */
      GetTimerPrecisionType(callerType));
}

/* static */
TimerPrecisionType nsRFPService::GetTimerPrecisionType(
    RTPCallerType aRTPCallerType) {
  if (aRTPCallerType == RTPCallerType::SystemPrincipal) {
    return DangerouslyNone;
  }

  if (aRTPCallerType == RTPCallerType::ResistFingerprinting) {
    return TimerPrecisionType::RFP;
  }

  if (StaticPrefs::privacy_reduceTimerPrecision() &&
      aRTPCallerType == RTPCallerType::CrossOriginIsolated) {
    return UnconditionalAKAHighRes;
  }

  if (StaticPrefs::privacy_reduceTimerPrecision()) {
    return Normal;
  }

  if (StaticPrefs::privacy_reduceTimerPrecision_unconditional()) {
    return UnconditionalAKAHighRes;
  }

  return DangerouslyNone;
}

/* static */
TimerPrecisionType nsRFPService::GetTimerPrecisionTypeRFPOnly(
    RTPCallerType aRTPCallerType) {
  if (aRTPCallerType == RTPCallerType::ResistFingerprinting) {
    return TimerPrecisionType::RFP;
  }

  if (StaticPrefs::privacy_reduceTimerPrecision_unconditional() &&
      aRTPCallerType != RTPCallerType::SystemPrincipal) {
    return UnconditionalAKAHighRes;
  }

  return DangerouslyNone;
}

/* static */
void nsRFPService::TypeToText(TimerPrecisionType aType, nsACString& aText) {
  switch (aType) {
    case TimerPrecisionType::DangerouslyNone:
      aText.AssignLiteral("DangerouslyNone");
      return;
    case TimerPrecisionType::Normal:
      aText.AssignLiteral("Normal");
      return;
    case TimerPrecisionType::RFP:
      aText.AssignLiteral("RFP");
      return;
    case TimerPrecisionType::UnconditionalAKAHighRes:
      aText.AssignLiteral("UnconditionalAKAHighRes");
      return;
    default:
      MOZ_ASSERT(false, "Shouldn't go here");
      aText.AssignLiteral("Unknown Enum Value");
      return;
  }
}

// ============================================================================
// ============================================================================
// ============================================================================
// Video Statistics Spoofing

/* static */
uint32_t nsRFPService::CalculateTargetVideoResolution(uint32_t aVideoQuality) {
  return aVideoQuality * NSToIntCeil(aVideoQuality * 16 / 9.0);
}

/* static */
uint32_t nsRFPService::GetSpoofedTotalFrames(double aTime) {
  double precision =
      TimerResolution(RTPCallerType::ResistFingerprinting) / 1000 / 1000;
  double time = floor(aTime / precision) * precision;

  return NSToIntFloor(time * kVideoFramesPerSec);
}

/* static */
uint32_t nsRFPService::GetSpoofedDroppedFrames(double aTime, uint32_t aWidth,
                                               uint32_t aHeight) {
  uint32_t targetRes = CalculateTargetVideoResolution(
      StaticPrefs::privacy_resistFingerprinting_target_video_res());

  // The video resolution is less than or equal to the target resolution, we
  // report a zero dropped rate for this case.
  if (targetRes >= aWidth * aHeight) {
    return 0;
  }

  double precision =
      TimerResolution(RTPCallerType::ResistFingerprinting) / 1000 / 1000;
  double time = floor(aTime / precision) * precision;
  // Bound the dropped ratio from 0 to 100.
  uint32_t boundedDroppedRatio = std::min(kVideoDroppedRatio, 100U);

  return NSToIntFloor(time * kVideoFramesPerSec *
                      (boundedDroppedRatio / 100.0));
}

/* static */
uint32_t nsRFPService::GetSpoofedPresentedFrames(double aTime, uint32_t aWidth,
                                                 uint32_t aHeight) {
  uint32_t targetRes = CalculateTargetVideoResolution(
      StaticPrefs::privacy_resistFingerprinting_target_video_res());

  // The target resolution is greater than the current resolution. For this
  // case, there will be no dropped frames, so we report total frames directly.
  if (targetRes >= aWidth * aHeight) {
    return GetSpoofedTotalFrames(aTime);
  }

  double precision =
      TimerResolution(RTPCallerType::ResistFingerprinting) / 1000 / 1000;
  double time = floor(aTime / precision) * precision;
  // Bound the dropped ratio from 0 to 100.
  uint32_t boundedDroppedRatio = std::min(kVideoDroppedRatio, 100U);

  return NSToIntFloor(time * kVideoFramesPerSec *
                      ((100 - boundedDroppedRatio) / 100.0));
}

// ============================================================================
// ============================================================================
// ============================================================================
// User-Agent/Version Stuff

/* static */
void nsRFPService::GetSpoofedUserAgent(nsACString& userAgent,
                                       bool aAndroidDesktopMode /* = false */) {
  // This function generates the spoofed value of User Agent.
  // We spoof the values of the platform and Firefox version, which could be
  // used as fingerprinting sources to identify individuals.
  // Reference of the format of User Agent:
  // https://developer.mozilla.org/en-US/docs/Web/API/NavigatorID/userAgent
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/User-Agent

  // These magic numbers are the lengths of the UA string literals below.
  // Assume three-digit Firefox version numbers so we have room to grow.
  size_t preallocatedLength = 13 + std::size(SPOOFED_UA_OS) - 1 + 5 + 3 + 10 +
                              std::size(LEGACY_UA_GECKO_TRAIL) - 1 + 9 + 3 + 2;
  userAgent.SetCapacity(preallocatedLength);

  // "Mozilla/5.0 (%s; rv:%d.0) Gecko/%d Firefox/%d.0"
  userAgent.AssignLiteral("Mozilla/5.0 (");
  if (aAndroidDesktopMode) {
    userAgent.AppendLiteral(SPOOFED_UA_OS_OTHER);
  } else {
    userAgent.AppendLiteral(SPOOFED_UA_OS);
  }
  userAgent.AppendLiteral("; rv:" MOZILLA_UAVERSION ") Gecko/");
#if defined(ANDROID)
  if (aAndroidDesktopMode) {
    userAgent.AppendLiteral(LEGACY_UA_GECKO_TRAIL);
  } else {
    userAgent.AppendLiteral(MOZILLA_UAVERSION);
  }
#else
  userAgent.AppendLiteral(LEGACY_UA_GECKO_TRAIL);
#endif
  userAgent.AppendLiteral(" Firefox/" MOZILLA_UAVERSION);

  MOZ_ASSERT(userAgent.Length() <= preallocatedLength);
}

NS_IMETHODIMP nsRFPService::GetSpoofedUserAgentService(bool aDesktopMode,
                                                       nsACString& aUserAgent) {
  nsRFPService::GetSpoofedUserAgent(aUserAgent, aDesktopMode);
  return NS_OK;
}

/* static */
nsCString nsRFPService::GetSpoofedJSLocale() { return "en-US"_ns; }

/* static */
nsCString nsRFPService::GetSpoofedJSTimeZone() {
  return "Atlantic/Reykjavik"_ns;
}

// ============================================================================
// ============================================================================
// ============================================================================
// Keyboard Spoofing Stuff

nsTHashMap<KeyboardHashKey, const SpoofingKeyboardCode*>*
    nsRFPService::sSpoofingKeyboardCodes = nullptr;

KeyboardHashKey::KeyboardHashKey(const KeyboardLangs aLang,
                                 const KeyboardRegions aRegion,
                                 const KeyNameIndex aKeyIdx,
                                 const nsAString& aKey)
    : mLang(aLang), mRegion(aRegion), mKeyIdx(aKeyIdx), mKey(aKey) {}

KeyboardHashKey::KeyboardHashKey(KeyTypePointer aOther)
    : mLang(aOther->mLang),
      mRegion(aOther->mRegion),
      mKeyIdx(aOther->mKeyIdx),
      mKey(aOther->mKey) {}

KeyboardHashKey::KeyboardHashKey(KeyboardHashKey&& aOther) noexcept
    : PLDHashEntryHdr(std::move(aOther)),
      mLang(std::move(aOther.mLang)),
      mRegion(std::move(aOther.mRegion)),
      mKeyIdx(std::move(aOther.mKeyIdx)),
      mKey(std::move(aOther.mKey)) {}

KeyboardHashKey::~KeyboardHashKey() = default;

bool KeyboardHashKey::KeyEquals(KeyTypePointer aOther) const {
  return mLang == aOther->mLang && mRegion == aOther->mRegion &&
         mKeyIdx == aOther->mKeyIdx && mKey == aOther->mKey;
}

KeyboardHashKey::KeyTypePointer KeyboardHashKey::KeyToPointer(KeyType aKey) {
  return &aKey;
}

PLDHashNumber KeyboardHashKey::HashKey(KeyTypePointer aKey) {
  PLDHashNumber hash = mozilla::HashString(aKey->mKey);
  return mozilla::AddToHash(hash, aKey->mRegion, aKey->mKeyIdx, aKey->mLang);
}

/* static */
void nsRFPService::MaybeCreateSpoofingKeyCodes(const KeyboardLangs aLang,
                                               const KeyboardRegions aRegion) {
  if (sSpoofingKeyboardCodes == nullptr) {
    sSpoofingKeyboardCodes =
        new nsTHashMap<KeyboardHashKey, const SpoofingKeyboardCode*>();
  }

  if (KeyboardLang::EN == aLang) {
    switch (aRegion) {
      case KeyboardRegion::US:
        MaybeCreateSpoofingKeyCodesForEnUS();
        break;
    }
  }
}

/* static */
void nsRFPService::MaybeCreateSpoofingKeyCodesForEnUS() {
  MOZ_ASSERT(sSpoofingKeyboardCodes);

  static bool sInitialized = false;
  const KeyboardLangs lang = KeyboardLang::EN;
  const KeyboardRegions reg = KeyboardRegion::US;

  if (sInitialized) {
    return;
  }

  static const SpoofingKeyboardInfo spoofingKeyboardInfoTable[] = {
#define KEY(key_, _codeNameIdx, _keyCode, _modifier) \
  {NS_LITERAL_STRING_FROM_CSTRING(key_),             \
   KEY_NAME_INDEX_USE_STRING,                        \
   {CODE_NAME_INDEX_##_codeNameIdx, _keyCode, _modifier}},
#define CONTROL(keyNameIdx_, _codeNameIdx, _keyCode) \
  {u""_ns,                                           \
   KEY_NAME_INDEX_##keyNameIdx_,                     \
   {CODE_NAME_INDEX_##_codeNameIdx, _keyCode, MODIFIER_NONE}},
#include "KeyCodeConsensus_En_US.inc"
#undef CONTROL
#undef KEY
  };

  for (const auto& keyboardInfo : spoofingKeyboardInfoTable) {
    KeyboardHashKey key(lang, reg, keyboardInfo.mKeyIdx, keyboardInfo.mKey);
    MOZ_ASSERT(!sSpoofingKeyboardCodes->Contains(key),
               "Double-defining key code; fix your KeyCodeConsensus file");
    sSpoofingKeyboardCodes->InsertOrUpdate(key, &keyboardInfo.mSpoofingCode);
  }

  sInitialized = true;
}

/* static */
void nsRFPService::GetKeyboardLangAndRegion(const nsAString& aLanguage,
                                            KeyboardLangs& aLocale,
                                            KeyboardRegions& aRegion) {
  nsAutoString langStr;
  nsAutoString regionStr;
  uint32_t partNum = 0;

  for (const nsAString& part : aLanguage.Split('-')) {
    if (partNum == 0) {
      langStr = part;
    } else {
      regionStr = part;
      break;
    }

    partNum++;
  }

  // We test each language here as well as the region. There are some cases that
  // only the language is given, we will use the default region code when this
  // happens. The default region should depend on the given language.
  if (langStr.EqualsLiteral(RFP_KEYBOARD_LANG_STRING_EN)) {
    aLocale = KeyboardLang::EN;
    // Give default values first.
    aRegion = KeyboardRegion::US;

    if (regionStr.EqualsLiteral(RFP_KEYBOARD_REGION_STRING_US)) {
      aRegion = KeyboardRegion::US;
    }
  } else {
    // There is no spoofed keyboard locale for the given language. We use the
    // default one in this case.
    aLocale = RFP_DEFAULT_SPOOFING_KEYBOARD_LANG;
    aRegion = RFP_DEFAULT_SPOOFING_KEYBOARD_REGION;
  }
}

/* static */
bool nsRFPService::GetSpoofedKeyCodeInfo(
    const dom::Document* aDoc, const WidgetKeyboardEvent* aKeyboardEvent,
    SpoofingKeyboardCode& aOut) {
  MOZ_ASSERT(aKeyboardEvent);

  KeyboardLangs keyboardLang = RFP_DEFAULT_SPOOFING_KEYBOARD_LANG;
  KeyboardRegions keyboardRegion = RFP_DEFAULT_SPOOFING_KEYBOARD_REGION;
  // If the document is given, we use the content language which is get from the
  // document. Otherwise, we use the default one.
  if (aDoc) {
    nsAtom* lang = aDoc->GetContentLanguage();

    // If the content-langauge is not given, we try to get langauge from the
    // HTML lang attribute.
    if (!lang) {
      if (dom::Element* elm = aDoc->GetHtmlElement()) {
        lang = elm->GetLang();
      }
    }

    // If two or more languages are given, per HTML5 spec, we should consider
    // it as 'unknown'. So we use the default one.
    if (lang) {
      nsDependentAtomString langStr(lang);
      if (!langStr.Contains(char16_t(','))) {
        langStr.StripWhitespace();
        GetKeyboardLangAndRegion(langStr, keyboardLang, keyboardRegion);
      }
    }
  }

  MaybeCreateSpoofingKeyCodes(keyboardLang, keyboardRegion);

  KeyNameIndex keyIdx = aKeyboardEvent->mKeyNameIndex;
  nsAutoString keyName;

  if (keyIdx == KEY_NAME_INDEX_USE_STRING) {
    keyName = aKeyboardEvent->mKeyValue;
  }

  KeyboardHashKey key(keyboardLang, keyboardRegion, keyIdx, keyName);
  const SpoofingKeyboardCode* keyboardCode = sSpoofingKeyboardCodes->Get(key);

  if (keyboardCode != nullptr) {
    aOut = *keyboardCode;
    return true;
  }

  return false;
}

/* static */
bool nsRFPService::GetSpoofedModifierStates(
    const dom::Document* aDoc, const WidgetKeyboardEvent* aKeyboardEvent,
    const Modifiers aModifier, bool& aOut) {
  MOZ_ASSERT(aKeyboardEvent);

  // For modifier or control keys, we don't need to hide its modifier states.
  if (aKeyboardEvent->mKeyNameIndex != KEY_NAME_INDEX_USE_STRING) {
    return false;
  }

  // We will spoof the modifer state for Alt, Shift, and AltGraph.
  // We don't spoof the Control key, because it is often used
  // for command key combinations in web apps.
  if ((aModifier & (MODIFIER_ALT | MODIFIER_SHIFT | MODIFIER_ALTGRAPH)) != 0) {
    SpoofingKeyboardCode keyCodeInfo;

    if (GetSpoofedKeyCodeInfo(aDoc, aKeyboardEvent, keyCodeInfo)) {
      aOut = ((keyCodeInfo.mModifierStates & aModifier) != 0);
      return true;
    }
  }

  return false;
}

/* static */
bool nsRFPService::GetSpoofedCode(const dom::Document* aDoc,
                                  const WidgetKeyboardEvent* aKeyboardEvent,
                                  nsAString& aOut) {
  MOZ_ASSERT(aKeyboardEvent);

  SpoofingKeyboardCode keyCodeInfo;

  if (!GetSpoofedKeyCodeInfo(aDoc, aKeyboardEvent, keyCodeInfo)) {
    return false;
  }

  WidgetKeyboardEvent::GetDOMCodeName(keyCodeInfo.mCode, aOut);

  // We need to change the 'Left' with 'Right' if the location indicates
  // it's a right key.
  if (aKeyboardEvent->mLocation ==
          dom::KeyboardEvent_Binding::DOM_KEY_LOCATION_RIGHT &&
      StringEndsWith(aOut, u"Left"_ns)) {
    aOut.ReplaceLiteral(aOut.Length() - 4, 4, u"Right");
  }

  return true;
}

/* static */
bool nsRFPService::GetSpoofedKeyCode(const dom::Document* aDoc,
                                     const WidgetKeyboardEvent* aKeyboardEvent,
                                     uint32_t& aOut) {
  MOZ_ASSERT(aKeyboardEvent);

  SpoofingKeyboardCode keyCodeInfo;

  if (GetSpoofedKeyCodeInfo(aDoc, aKeyboardEvent, keyCodeInfo)) {
    aOut = keyCodeInfo.mKeyCode;
    return true;
  }

  return false;
}

// ============================================================================
// ============================================================================
// ============================================================================
// Randomization Stuff
nsresult nsRFPService::GetBrowsingSessionKey(
    const OriginAttributes& aOriginAttributes, nsID& aBrowsingSessionKey) {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsAutoCString oaSuffix;
  aOriginAttributes.CreateSuffix(oaSuffix);

  MOZ_LOG(gResistFingerprintingLog, LogLevel::Info,
          ("Get the browsing session key for the originAttributes: %s\n",
           oaSuffix.get()));

  // If any fingerprinting randomization protection is enabled, we generate the
  // browsing session key.
  // Note that there is only canvas randomization protection currently.
  if (!nsContentUtils::ShouldResistFingerprinting(
          "Checking the target activation globally without local context",
          RFPTarget::CanvasRandomization) &&
      !nsContentUtils::ShouldResistFingerprinting(
          "Checking the target activation globally without local context",
          RFPTarget::WebGLRandomization) &&
      !nsContentUtils::ShouldResistFingerprinting(
          "Checking the target activation globally without local context",
          RFPTarget::EfficientCanvasRandomization)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  Maybe<nsID> sessionKey = mBrowsingSessionKeys.MaybeGet(oaSuffix);

  // The key has been generated, bail out earlier.
  if (sessionKey) {
    MOZ_LOG(gResistFingerprintingLog, LogLevel::Info,
            ("The browsing session key exists: %s\n",
             sessionKey.ref().ToString().get()));
    aBrowsingSessionKey = sessionKey.ref();
    return NS_OK;
  }

  nsID& newKey =
      mBrowsingSessionKeys.InsertOrUpdate(oaSuffix, nsID::GenerateUUID());

  MOZ_LOG(gResistFingerprintingLog, LogLevel::Debug,
          ("Generated browsing session key: %s\n", newKey.ToString().get()));
  aBrowsingSessionKey = newKey;

  return NS_OK;
}

void nsRFPService::ClearBrowsingSessionKey(
    const OriginAttributesPattern& aPattern) {
  MOZ_ASSERT(XRE_IsParentProcess());

  for (auto iter = mBrowsingSessionKeys.Iter(); !iter.Done(); iter.Next()) {
    nsAutoCString key(iter.Key());
    OriginAttributes attrs;
    (void)attrs.PopulateFromSuffix(key);

    // Remove the entry if the origin attributes pattern matches
    if (aPattern.Matches(attrs)) {
      iter.Remove();
    }
  }
}

void nsRFPService::ClearBrowsingSessionKey(
    const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsAutoCString key;
  aOriginAttributes.CreateSuffix(key);

  mBrowsingSessionKeys.Remove(key);
}

// static
Maybe<nsTArray<uint8_t>> nsRFPService::GenerateKey(nsIChannel* aChannel) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aChannel);

#ifdef DEBUG
  // Ensure we only compute random key for top-level loads.
  {
    nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
    MOZ_ASSERT(loadInfo->GetExternalContentPolicyType() ==
               ExtContentPolicy::TYPE_DOCUMENT);
  }
#endif

  nsCOMPtr<nsIURI> topLevelURI;
  (void)aChannel->GetURI(getter_AddRefs(topLevelURI));

  MOZ_LOG(gResistFingerprintingLog, LogLevel::Debug,
          ("Generating the randomization key for top-level URI: %s\n",
           topLevelURI->GetSpecOrDefault().get()));

  RefPtr<nsRFPService> service = GetOrCreate();

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  OriginAttributes attrs = loadInfo->GetOriginAttributes();

  // Set the partitionKey using the top level URI to ensure that the key is
  // specific to the top level site.
  bool foreignByAncestorContext =
      AntiTrackingUtils::IsThirdPartyChannel(aChannel) &&
      loadInfo->GetIsThirdPartyContextToTopWindow();
  attrs.SetPartitionKey(topLevelURI, foreignByAncestorContext);

  nsAutoCString oaSuffix;
  attrs.CreateSuffix(oaSuffix);

  MOZ_LOG(gResistFingerprintingLog, LogLevel::Debug,
          ("Get the key using OriginAttributes: %s\n", oaSuffix.get()));

  nsID sessionKey = {};
  if (NS_FAILED(service->GetBrowsingSessionKey(attrs, sessionKey))) {
    return Nothing();
  }

  // Return nothing if fingerprinting randomization is disabled for the given
  // channel.
  //
  // Note that canvas randomization is the only fingerprinting randomization
  // protection currently.
  if (!nsContentUtils::ShouldResistFingerprinting(
          aChannel, RFPTarget::CanvasRandomization) &&
      !nsContentUtils::ShouldResistFingerprinting(
          aChannel, RFPTarget::WebGLRandomization) &&
      !nsContentUtils::ShouldResistFingerprinting(
          aChannel, RFPTarget::EfficientCanvasRandomization)) {
    return Nothing();
  }
  auto sessionKeyStr = sessionKey.ToString();

  // Generate the key by using the hMAC. The key is based on the session key and
  // the partitionKey, i.e. top-level site.
  HMAC hmac;

  nsresult rv = hmac.Begin(
      SEC_OID_SHA256,
      Span(reinterpret_cast<const uint8_t*>(sessionKeyStr.get()), NSID_LENGTH));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  // Using the OriginAttributes to get the top level site. The site is composed
  // of scheme, host, and port.
  NS_ConvertUTF16toUTF8 topLevelSite(attrs.mPartitionKey);
  rv = hmac.Update(reinterpret_cast<const uint8_t*>(topLevelSite.get()),
                   topLevelSite.Length());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  Maybe<nsTArray<uint8_t>> key;
  key.emplace();

  rv = hmac.End(key.ref());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  return key;
}

// static
Maybe<nsTArray<uint8_t>> nsRFPService::GenerateKeyForServiceWorker(
    nsIURI* aFirstPartyURI, nsIPrincipal* aPrincipal,
    bool aForeignByAncestorContext) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aFirstPartyURI);

  RefPtr<nsRFPService> service = GetOrCreate();

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();
  attrs.SetPartitionKey(aFirstPartyURI, aForeignByAncestorContext);

  nsID sessionKey = {};
  if (NS_FAILED(service->GetBrowsingSessionKey(attrs, sessionKey))) {
    return Nothing();
  }
  auto sessionKeyStr = sessionKey.ToString();

  // Generate the key by using the hMAC. The key is based on the session key and
  // the partitionKey, i.e. top-level site.
  HMAC hmac;

  nsresult rv = hmac.Begin(
      SEC_OID_SHA256,
      Span(reinterpret_cast<const uint8_t*>(sessionKeyStr.get()), NSID_LENGTH));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  // Using the OriginAttributes to get the top level site. The site is composed
  // of scheme, host, and port.
  NS_ConvertUTF16toUTF8 topLevelSite(attrs.mPartitionKey);
  rv = hmac.Update(reinterpret_cast<const uint8_t*>(topLevelSite.get()),
                   topLevelSite.Length());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  Maybe<nsTArray<uint8_t>> key;
  key.emplace();

  rv = hmac.End(key.ref());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }

  return key;
}

NS_IMETHODIMP
nsRFPService::CleanAllRandomKeys() {
  MOZ_ASSERT(XRE_IsParentProcess());
  mBrowsingSessionKeys.Clear();
  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::CleanRandomKeyByPrincipal(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_TRUE(aPrincipal->GetIsContentPrincipal(), NS_ERROR_FAILURE);

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();
  nsCOMPtr<nsIURI> uri = aPrincipal->GetURI();

  attrs.SetPartitionKey(uri, false);
  ClearBrowsingSessionKey(attrs);

  // We must also include the cross-site embeds of this principal that end up
  // re-embedded back into the same principal's top level, otherwise state will
  // persist for this target
  attrs.SetPartitionKey(uri, true);
  ClearBrowsingSessionKey(attrs);
  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::CleanRandomKeyBySite(
    const nsACString& aSchemelessSite,
    JS::Handle<JS::Value> aOriginAttributesPattern, JSContext* aCx) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aCx);

  OriginAttributesPattern pattern;
  if (!aOriginAttributesPattern.isObject() ||
      !pattern.Init(aCx, aOriginAttributesPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!pattern.mPartitionKeyPattern.WasPassed()) {
    pattern.mPartitionKeyPattern.Construct();
  }
  pattern.mPartitionKeyPattern.Value().mBaseDomain.Construct(
      NS_ConvertUTF8toUTF16(aSchemelessSite));

  ClearBrowsingSessionKey(pattern);

  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::CleanRandomKeyByHost(const nsACString& aHost,
                                   const nsAString& aPattern) {
  MOZ_ASSERT(XRE_IsParentProcess());

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  // Get http URI from the host.
  nsCOMPtr<nsIURI> httpURI;
  nsresult rv = NS_NewURI(getter_AddRefs(httpURI), "http://"_ns + aHost);
  NS_ENSURE_SUCCESS(rv, rv);

  // Use the originAttributes to get the partitionKey.
  OriginAttributes attrs;
  attrs.SetPartitionKey(httpURI, false);

  // Set the partitionKey to the pattern.
  pattern.mPartitionKey.Reset();
  pattern.mPartitionKey.Construct(attrs.mPartitionKey);

  ClearBrowsingSessionKey(pattern);

  // We must also include the cross-site embeds of this principal that end up
  // re-embedded back into the same principal's top level, otherwise state will
  // persist for this target
  attrs.SetPartitionKey(httpURI, true);
  pattern.mPartitionKey.Reset();
  pattern.mPartitionKey.Construct(attrs.mPartitionKey);
  ClearBrowsingSessionKey(pattern);

  // Get https URI from the host.
  nsCOMPtr<nsIURI> httpsURI;
  rv = NS_NewURI(getter_AddRefs(httpsURI), "https://"_ns + aHost);
  NS_ENSURE_SUCCESS(rv, rv);

  // Use the originAttributes to get the partitionKey and set to the pattern.
  attrs.SetPartitionKey(httpsURI, false);
  pattern.mPartitionKey.Reset();
  pattern.mPartitionKey.Construct(attrs.mPartitionKey);
  ClearBrowsingSessionKey(pattern);

  // We must also include the cross-site embeds of this principal that end up
  // re-embedded back into the same principal's top level, otherwise state will
  // persist for this target
  attrs.SetPartitionKey(httpsURI, true);
  pattern.mPartitionKey.Reset();
  pattern.mPartitionKey.Construct(attrs.mPartitionKey);
  ClearBrowsingSessionKey(pattern);
  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::CleanRandomKeyByOriginAttributesPattern(
    const nsAString& aPattern) {
  MOZ_ASSERT(XRE_IsParentProcess());

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  ClearBrowsingSessionKey(pattern);
  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::TestGenerateRandomKey(nsIChannel* aChannel,
                                    nsTArray<uint8_t>& aKey) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aChannel);

  Maybe<nsTArray<uint8_t>> key = GenerateKey(aChannel);

  if (!key) {
    return NS_OK;
  }

  aKey = key.ref().Clone();
  return NS_OK;
}

// static
nsresult nsRFPService::GenerateCanvasKeyFromImageData(
    nsICookieJarSettings* aCookieJarSettings, uint8_t* aImageData,
    uint32_t aSize, nsTArray<uint8_t>& aCanvasKey) {
  NS_ENSURE_ARG_POINTER(aCookieJarSettings);
  AUTO_PROFILER_MARKER_TEXT("nsRFPService", OTHER, {},
                            "nsRFPService::GenerateCanvasKeyFromImageData"_ns);

  nsTArray<uint8_t> randomKey;
  nsresult rv =
      aCookieJarSettings->GetFingerprintingRandomizationKey(randomKey);

  // There is no random key for this cookieJarSettings. This means that the
  // randomization is disabled. So, we can bail out from here without doing
  // anything.
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  if ((aSize < 2500 || !mozilla::supports_sha()) ||
      StaticPrefs::
          privacy_resistFingerprinting_randomization_canvas_use_siphash()) {
    // Hash the canvas data to generate the image data hash.
    mozilla::HashNumber imageHashData = mozilla::HashBytes(aImageData, aSize);

    // Then, we use the SipHash seeded by the first half of the random key to
    // generate a hash result using the image hash data. Our sipHash is
    // implemented in the mozilla::HashCodeScrambler.
    uint64_t k0 = *reinterpret_cast<uint64_t*>(randomKey.Elements());
    uint64_t k1 = *reinterpret_cast<uint64_t*>(randomKey.Elements() + 8);
    mozilla::HashCodeScrambler hcs(k0, k1);
    mozilla::HashNumber hashResult = hcs.scramble(imageHashData);

    aCanvasKey.SetLength(32);
    aCanvasKey.ClearAndRetainStorage();

    // Finally, we use the hash result, image hash data, and the second half of
    // the random key to generate the 256 bits canvas key.
    uint64_t digest = static_cast<uint64_t>(hashResult) << 32 | imageHashData;
    non_crypto::XorShift128PlusRNG rng(
        digest, *reinterpret_cast<uint64_t*>(randomKey.Elements() + 16));

    for (size_t i = 0; i < 4; ++i) {
      uint64_t val = rng.next();
      for (size_t j = 0; j < 8; ++j) {
        uint8_t data = static_cast<uint8_t>((val >> (j * 8)) & 0xFF);
        aCanvasKey.InsertElementAt((i * 8) + j, data);
      }
    }
  } else {
    // Generate the key for randomizing the canvas data using hMAC. The key is
    // based on the random key of the document and the canvas data itself. So,
    // different canvas would have different keys.
    HMAC hmac;

    rv = hmac.Begin(SEC_OID_SHA256, Span(randomKey));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = hmac.Update(aImageData, aSize);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = hmac.End(aCanvasKey);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// static
void nsRFPService::PotentiallyDumpImage(nsIPrincipal* aPrincipal,
                                        gfx::DataSourceSurface* aSurface) {
  // Only dump from the content process to avoid unintended file writes from
  // privileged or background processes.
  if (XRE_GetProcessType() != GeckoProcessType_Content) {
    return;
  }
  if (MOZ_LOG_TEST(gFingerprinterDetection, LogLevel::Debug)) {
    int32_t format = 0;
    UniquePtr<uint8_t[]> imageBuffer =
        gfxUtils::GetImageBuffer(aSurface, true, &format);
    nsRFPService::PotentiallyDumpImage(
        aPrincipal, imageBuffer.get(), aSurface->GetSize().width,
        aSurface->GetSize().height,
        aSurface->GetSize().width * aSurface->GetSize().height * 4);
  }
}

void nsRFPService::PotentiallyDumpImage(nsIPrincipal* aPrincipal,
                                        uint8_t* aData, uint32_t aWidth,
                                        uint32_t aHeight, uint32_t aSize) {
  // Only dump from the content process to avoid unintended file writes from
  // privileged or background processes.
  if (XRE_GetProcessType() != GeckoProcessType_Content) {
    return;
  }
  nsAutoCString safeSite;

  if (MOZ_LOG_TEST(gFingerprinterDetection, LogLevel::Debug)) {
    nsAutoCString site;
    if (aPrincipal) {
      nsCOMPtr<nsIURI> uri = aPrincipal->GetURI();
      if (uri) {
        site.Assign(uri->GetSpecOrDefault());
      }
    }
    if (site.IsEmpty()) {
      site.AssignLiteral("unknown");
    }

    safeSite.SetCapacity(site.Length());
    // Build a sanitized site string from the principal's URI for filename.
    // Allow alnum, '.', '_', '-', replace others with '_'.
    for (uint32_t i = 0; i < site.Length() && safeSite.Length() < 80; ++i) {
      char c = site[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
        safeSite.Append(c);
      } else {
        safeSite.Append('_');
      }
    }

    MOZ_LOG(gFingerprinterDetection, LogLevel::Debug,
            ("Would dump a canvas image from %s width: %i height: %i bytes: %i",
             site.get(), aWidth, aHeight, aSize));
  }

  if (MOZ_LOG_TEST(gFingerprinterDetection, LogLevel::Verbose)) {
    // Requires sandbox level 0
    // For debugging purposes you can dump the image with this code
    // then convert it with the image-magick command
    // convert -size WxH -depth 8 rgba:$i $i.png
    // Depending on surface format, the alpha and color channels might be mixed
    // up...
    static int calls = 0;
    char filename[256];
    SprintfLiteral(filename, "rendered_image_%s__%dx%d_%d", safeSite.get(),
                   aWidth, aHeight, calls);

    const char* logEnv = PR_GetEnv("MOZ_LOG_FILE");
#ifdef XP_WIN
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    const char* dirEnd = nullptr;
    if (logEnv) {
      for (const char* it = logEnv; *it; ++it) {
        if (*it == sep) {
          dirEnd = it;
        }
      }
    }

    char outPath[512];
    if (dirEnd) {
      int dirLen = int(dirEnd - logEnv + 1);
      SprintfLiteral(outPath, "%.*s%s", dirLen, logEnv, filename);
    } else {
      SprintfLiteral(outPath, "%s", filename);
    }

    FILE* outputFile = fopen(outPath, "wb");
    if (outputFile) {
      fwrite(aData, 1, aSize, outputFile);
      fclose(outputFile);
      calls++;
    }
  }
}

// static
nsresult nsRFPService::RandomizePixels(nsICookieJarSettings* aCookieJarSettings,
                                       nsIPrincipal* aPrincipal, uint8_t* aData,
                                       uint32_t aWidth, uint32_t aHeight,
                                       uint32_t aSize,
                                       gfx::SurfaceFormat aSurfaceFormat) {
  constexpr uint8_t bytesPerChannel = 1;
  constexpr uint8_t bytesPerPixel = 4 * bytesPerChannel;

  uint8_t offset = 0;
  switch (aSurfaceFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
      offset = 0;
      break;
    case gfx::SurfaceFormat::A8R8G8B8:
      offset = 1;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Unsupported surface format for pixel randomization");
      return NS_ERROR_INVALID_ARG;
  }
  return RandomizeElements(aCookieJarSettings, aPrincipal, aData, aSize,
                           bytesPerPixel, bytesPerChannel, offset, true);
}

// static
nsresult nsRFPService::RandomizeElements(
    nsICookieJarSettings* aCookieJarSettings, nsIPrincipal* aPrincipal,
    uint8_t* aData, uint32_t aSizeInBytes, uint8_t aElementsPerGroup,
    uint8_t aBytesPerElement, uint8_t aElementOffset, bool aSkipLastElement) {
  NS_ENSURE_ARG_POINTER(aData);
  MOZ_ASSERT(aElementsPerGroup >= 1);
  MOZ_ASSERT(aBytesPerElement >= 1);
  MOZ_ASSERT(aElementOffset < aElementsPerGroup);

  if (!aCookieJarSettings) {
    return NS_OK;
  }

  uint32_t groupSize = aElementsPerGroup * aBytesPerElement;
  uint32_t groupCount = aSizeInBytes / groupSize;

  if (groupCount <= 1) {
    return NS_OK;
  }

  if (aPrincipal && CanvasUtils::GetCanvasExtractDataPermission(aPrincipal) ==
                        nsIPermissionManager::ALLOW_ACTION) {
    return NS_OK;
  }

  // Don't randomize if all groups are uniform.
  const bool allGroupsMatch = [&]() {
    auto itr = RangedPtr<const uint8_t>(aData, aSizeInBytes);
    const auto itrEnd = itr + (groupCount * groupSize);
    for (; itr != itrEnd; itr += groupSize) {
      if (memcmp(itr.get(), aData, groupSize) != 0) {
        return false;
      }
    }
    return true;
  }();
  if (allGroupsMatch) {
    return NS_OK;
  }

  auto timerId =
      glean::fingerprinting_protection::canvas_noise_calculate_time_2.Start();

  nsTArray<uint8_t> canvasKey;
  nsresult rv = GenerateCanvasKeyFromImageData(aCookieJarSettings, aData,
                                               aSizeInBytes, canvasKey);

  if (NS_FAILED(rv)) {
    glean::fingerprinting_protection::canvas_noise_calculate_time_2.Cancel(
        std::move(timerId));
    return rv;
  }

  // Generate random values that will decide the RGB channel and the pixel
  // position that we are going to introduce the noises. The channel and
  // position are predictable to ensure we have a consistant result with the
  // same canvas in the same browsing session.

  // Seed and create the first random number generator which will be used to
  // select RGB channel and the pixel position. The seed is the first half of
  // the canvas key.
  non_crypto::XorShift128PlusRNG rng1(
      *reinterpret_cast<uint64_t*>(canvasKey.Elements()),
      *reinterpret_cast<uint64_t*>(canvasKey.Elements() + 8));

  // Use the last 8 bits as the number of noises.
  uint8_t rnd3 = canvasKey.LastElement();

  // Clear the last 8 bits.
  canvasKey.ReplaceElementAt(canvasKey.Length() - 1, 0);

  // Use the remaining 120 bits to seed and create the second random number
  // generator. The random number will be used to decided the noise bit that
  // will be added to the lowest order bit of the channel of the pixel.
  non_crypto::XorShift128PlusRNG rng2(
      *reinterpret_cast<uint64_t*>(canvasKey.Elements() + 16),
      *reinterpret_cast<uint64_t*>(canvasKey.Elements() + 24));

  // Ensure at least 20 random changes may occur.
  uint8_t numNoises = std::clamp<uint8_t>(rnd3, 20, 255);

  // (This is for pixel randomization)
  // For canvas 2d randomization, we are always dealing with 4 channels.
  // For WebGL, we are dealing with 1-4 channels. Thankfully, WebGL always
  // puts the alpha channel at the end, so we can always use the last
  // element as the alpha channel (if it exists).
  // To choose which channel to add a noise, we use the modulo operator
  // to select the channel. We want to avoid the alpha channel
  // (if it exists). So, in cases where the number of elements per group
  // is greater than 1, we subtract 1 from the modulo divisor.
  // This way, we can avoid the alpha channel by skipping it in the
  // selection process.
  // We set aSkipLastElement to false for WebGL formats without the alpha
  // channel.
  uint8_t moduloDivisor = aElementsPerGroup;
  if (moduloDivisor > 1 && aSkipLastElement) {
    moduloDivisor -= 1;
  }

  while (numNoises--) {
    // Choose which element to add a noise.
    // We could have an element that we want to avoid (for ex. alpha channel),
    // so we use the offset param to skip the elements we don't want to change.
    uint8_t element = rng1.next() % moduloDivisor + aElementOffset;
    MOZ_ASSERT(element < aElementsPerGroup,
               "Element index should be less than elements per group");

    // When randomizing elements with aBytesPerElement > 1, we end up
    // randomizing the first byte of each element.
    // This if actually desired, as it will correspond to the least significant
    // bits of the entire element.
    // For floats for example, the first 8 bits are still under the fraction
    // part of the float representation. For float 32, we are affecting even
    // smaller fraction part.
    // This is what we want, as we want to introduce very small
    // changes to the elements.

    uint32_t idx =
        groupSize * (rng1.next() % groupCount) + element * aBytesPerElement;
    MOZ_ASSERT(idx < aSizeInBytes,
               "Index should be less than the size of the data");
    uint8_t bit = rng2.next();

    // 50% chance to XOR a 0x2 or 0x1 into the existing byte
    aData[idx] = aData[idx] ^ (0x2 >> (bit & 0x1));
  }

  glean::fingerprinting_protection::canvas_noise_calculate_time_2
      .StopAndAccumulate(std::move(timerId));

  return NS_OK;
}

static const char* CanvasFingerprinterToString(
    CanvasFingerprinterAlias aFingerprinter) {
  switch (aFingerprinter) {
    case CanvasFingerprinterAlias::eNoneIdentified:
      return "(None Identified)";
    case CanvasFingerprinterAlias::eFingerprintJS:
      return "FingerprintJS";
    case CanvasFingerprinterAlias::eAkamai:
      return "Akamai";
    case CanvasFingerprinterAlias::eOzoki:
      return "Ozoki";
    case CanvasFingerprinterAlias::ePerimeterX:
      return "PerimeterX";
    case CanvasFingerprinterAlias::eClientGear:
      return "ClientGear";
    case CanvasFingerprinterAlias::eSignifyd:
      return "Signifyd";
    case CanvasFingerprinterAlias::eClaydar:
      return "Claydar";
    case CanvasFingerprinterAlias::eImperva:
      return "Imperva";
    case CanvasFingerprinterAlias::eForter:
      return "Forter";
    case CanvasFingerprinterAlias::eVariant1:
      return "Variant1";
    case CanvasFingerprinterAlias::eVariant2:
      return "Variant2";
    case CanvasFingerprinterAlias::eVariant3:
      return "Variant3";
    case CanvasFingerprinterAlias::eVariant4:
      return "Variant4";
    case CanvasFingerprinterAlias::eVariant5:
      return "Variant5";
    case CanvasFingerprinterAlias::eVariant6:
      return "Variant6";
    case CanvasFingerprinterAlias::eVariant7:
      return "Variant7";
    case CanvasFingerprinterAlias::eVariant8:
      return "Variant8";
  }
  MOZ_ASSERT(false, "Unhandled CanvasFingerprinterAlias enum value");
  return "<error>";
}

namespace mozilla {
nsCString CanvasUsageSourceToString(CanvasUsageSource aSource) {
  if (aSource == CanvasUsageSource::Unknown) {
    return "None"_ns;
  }

  nsCString accumulated;

  auto append = [&](const char* name) {
    if (!accumulated.IsEmpty()) {
      accumulated.AppendLiteral(", ");
    }
    accumulated.Append(name);
  };

#define APPEND_IF_SET(flag, name)     \
  if ((aSource & (flag)) == (flag)) { \
    append(name);                     \
  }

  APPEND_IF_SET(Impossible, "Impossible");

  APPEND_IF_SET(MainThread_Canvas_ImageBitmap_toDataURL,
                "MainThread_Canvas_ImageBitmap_toDataURL");
  APPEND_IF_SET(MainThread_Canvas_ImageBitmap_toBlob,
                "MainThread_Canvas_ImageBitmap_toBlob");
  APPEND_IF_SET(MainThread_Canvas_ImageBitmap_getImageData,
                "MainThread_Canvas_ImageBitmap_getImageData");

  APPEND_IF_SET(MainThread_Canvas_Canvas2D_toDataURL,
                "MainThread_Canvas_Canvas2D_toDataURL");
  APPEND_IF_SET(MainThread_Canvas_Canvas2D_toBlob,
                "MainThread_Canvas_Canvas2D_toBlob");
  APPEND_IF_SET(MainThread_Canvas_Canvas2D_getImageData,
                "MainThread_Canvas_Canvas2D_getImageData");

  APPEND_IF_SET(MainThread_Canvas_WebGL_toDataURL,
                "MainThread_Canvas_WebGL_toDataURL");
  APPEND_IF_SET(MainThread_Canvas_WebGL_toBlob,
                "MainThread_Canvas_WebGL_toBlob");
  APPEND_IF_SET(MainThread_Canvas_WebGL_getImageData,
                "MainThread_Canvas_WebGL_getImageData");
  APPEND_IF_SET(MainThread_Canvas_WebGL_readPixels,
                "MainThread_Canvas_WebGL_readPixels");

  APPEND_IF_SET(MainThread_Canvas_WebGPU_toDataURL,
                "MainThread_Canvas_WebGPU_toDataURL");
  APPEND_IF_SET(MainThread_Canvas_WebGPU_toBlob,
                "MainThread_Canvas_WebGPU_toBlob");
  APPEND_IF_SET(MainThread_Canvas_WebGPU_getImageData,
                "MainThread_Canvas_WebGPU_getImageData");

  APPEND_IF_SET(MainThread_OffscreenCanvas_ImageBitmap_toDataURL,
                "MainThread_OffscreenCanvas_ImageBitmap_toDataURL");
  APPEND_IF_SET(MainThread_OffscreenCanvas_ImageBitmap_toBlob,
                "MainThread_OffscreenCanvas_ImageBitmap_toBlob");
  APPEND_IF_SET(MainThread_OffscreenCanvas_ImageBitmap_getImageData,
                "MainThread_OffscreenCanvas_ImageBitmap_getImageData");

  APPEND_IF_SET(MainThread_OffscreenCanvas_Canvas2D_toDataURL,
                "MainThread_OffscreenCanvas_Canvas2D_toDataURL");
  APPEND_IF_SET(MainThread_OffscreenCanvas_Canvas2D_toBlob,
                "MainThread_OffscreenCanvas_Canvas2D_toBlob");
  APPEND_IF_SET(MainThread_OffscreenCanvas_Canvas2D_getImageData,
                "MainThread_OffscreenCanvas_Canvas2D_getImageData");

  APPEND_IF_SET(Worker_OffscreenCanvas_Canvas2D_toBlob,
                "Worker_OffscreenCanvas_Canvas2D_toBlob");
  APPEND_IF_SET(Worker_OffscreenCanvas_Canvas2D_getImageData,
                "Worker_OffscreenCanvas_Canvas2D_getImageData");

  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGL_toDataURL,
                "MainThread_OffscreenCanvas_WebGL_toDataURL");
  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGL_toBlob,
                "MainThread_OffscreenCanvas_WebGL_toBlob");
  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGL_getImageData,
                "MainThread_OffscreenCanvas_WebGL_getImageData");
  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGL_readPixels,
                "MainThread_OffscreenCanvas_WebGL_readPixels");

  APPEND_IF_SET(Worker_OffscreenCanvas_ImageBitmap_toBlob,
                "Worker_OffscreenCanvas_ImageBitmap_toBlob");
  APPEND_IF_SET(Worker_OffscreenCanvas_ImageBitmap_getImageData,
                "Worker_OffscreenCanvas_ImageBitmap_getImageData");

  APPEND_IF_SET(Worker_OffscreenCanvas_WebGL_toBlob,
                "Worker_OffscreenCanvas_WebGL_toBlob");
  APPEND_IF_SET(Worker_OffscreenCanvas_WebGL_getImageData,
                "Worker_OffscreenCanvas_WebGL_getImageData");
  APPEND_IF_SET(Worker_OffscreenCanvas_WebGL_readPixels,
                "Worker_OffscreenCanvas_WebGL_readPixels");

  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGPU_toDataURL,
                "MainThread_OffscreenCanvas_WebGPU_toDataURL");
  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGPU_toBlob,
                "MainThread_OffscreenCanvas_WebGPU_toBlob");
  APPEND_IF_SET(MainThread_OffscreenCanvas_WebGPU_getImageData,
                "MainThread_OffscreenCanvas_WebGPU_getImageData");

  APPEND_IF_SET(Worker_OffscreenCanvas_WebGPU_toBlob,
                "Worker_OffscreenCanvas_WebGPU_toBlob");
  APPEND_IF_SET(Worker_OffscreenCanvas_WebGPU_getImageData,
                "Worker_OffscreenCanvas_WebGPU_getImageData");

  APPEND_IF_SET(Worker_OffscreenCanvasCanvas2D_Canvas2D_getImageData,
                "Worker_OffscreenCanvasCanvas2D_Canvas2D_getImageData");
  APPEND_IF_SET(Worker_OffscreenCanvasCanvas2D_Canvas2D_toBlob,
                "Worker_OffscreenCanvasCanvas2D_Canvas2D_toBlob");

#undef APPEND_IF_SET

  if (accumulated.IsEmpty()) {
    accumulated.AssignLiteral("<error>");
  }

  return accumulated;
}
}  // namespace mozilla

CanvasUsage CanvasUsage::CreateUsage(
    bool aIsOffscreen, dom::CanvasContextType aContextType,
    CanvasExtractionAPI aApi, CSSIntSize aSize,
    const nsICanvasRenderingContextInternal* aContext) {
  CanvasFeatureUsage featureUsage = CanvasFeatureUsage::None;

  // Only 2D contexts currently expose feature usage metrics.
  if (aContext && (aContextType == dom::CanvasContextType::Canvas2D ||
                   aContextType == dom::CanvasContextType::OffscreenCanvas2D)) {
    // Downcast is safe because 2D contexts derive from
    // nsICanvasRenderingContextInternal and we gated on the supplied type. The
    // header is included so the relationship is known to the compiler.
    auto* ctx2D = static_cast<const dom::CanvasRenderingContext2D*>(aContext);
    featureUsage = ctx2D->FeatureUsage();
  }

  CanvasUsageSource usageSource =
      CanvasUsage::GetCanvasUsageSource(aIsOffscreen, aContextType, aApi);
  return CanvasUsage(aSize, aContextType, usageSource, featureUsage);
}
CanvasUsageSource CanvasUsage::GetCanvasUsageSource(
    bool isOffscreen, dom::CanvasContextType contextType,
    CanvasExtractionAPI api) {
  const bool isMainThread = NS_IsMainThread();

  auto logImpossible = [&](const char* aComment = "") {
    MOZ_LOG(gFingerprinterDetection, LogLevel::Error,
            ("CanvasUsageSource impossible: comment=%s isOffscreen=%d "
             "contextType=%d api=%d isMainThread=%d",
             aComment, isOffscreen, static_cast<int>(contextType),
             static_cast<int>(api), isMainThread));
  };

  // Non-offscreen (HTMLCanvasElement) path (always main thread).
  if (!isOffscreen) {
    if (!isMainThread) {
      logImpossible("Non-offscreen canvas accessed off main thread");
      return CanvasUsageSource::Impossible;
    }
    switch (contextType) {
      case dom::CanvasContextType::Canvas2D:
        switch (api) {
          case CanvasExtractionAPI::ToDataURL:
            return MainThread_Canvas_Canvas2D_toDataURL;
          case CanvasExtractionAPI::ToBlob:
            return MainThread_Canvas_Canvas2D_toBlob;
          case CanvasExtractionAPI::GetImageData:
            return MainThread_Canvas_Canvas2D_getImageData;
          case CanvasExtractionAPI::ReadPixels:
            logImpossible("ReadPixels invalid for Canvas2D");
            return CanvasUsageSource::Impossible;
          default:
            logImpossible("Unknown API for Canvas2D");
            return CanvasUsageSource::Impossible;
        }
      case dom::CanvasContextType::OffscreenCanvas2D:
        // Very confused about how we get an OffscreenCanvas2D context without
        // an OffscreenCanvas but it does happen in the wild...
        switch (api) {
          case CanvasExtractionAPI::GetImageData:
            return CanvasUsageSource::
                MainThread_Canvas_OffscreenCanvas2D_getImageData;
          case CanvasExtractionAPI::ToBlob:
            return CanvasUsageSource::
                MainThread_Canvas_OffscreenCanvas2D_toBlob;
          default:
            logImpossible("Unsupported API for OffscreenCanvas2D");
            return CanvasUsageSource::Impossible;
        }
      case dom::CanvasContextType::WebGL1:
      case dom::CanvasContextType::WebGL2:
        switch (api) {
          case CanvasExtractionAPI::ToDataURL:
            return MainThread_Canvas_WebGL_toDataURL;
          case CanvasExtractionAPI::ToBlob:
            return MainThread_Canvas_WebGL_toBlob;
          case CanvasExtractionAPI::GetImageData:
            return MainThread_Canvas_WebGL_getImageData;
          case CanvasExtractionAPI::ReadPixels:
            return MainThread_Canvas_WebGL_readPixels;
          default:
            logImpossible("Unknown API for WebGL");
            return CanvasUsageSource::Impossible;
        }
      case dom::CanvasContextType::WebGPU:
        switch (api) {
          case CanvasExtractionAPI::ToDataURL:
            return MainThread_Canvas_WebGPU_toDataURL;
          case CanvasExtractionAPI::ToBlob:
            return MainThread_Canvas_WebGPU_toBlob;
          case CanvasExtractionAPI::GetImageData:
            return MainThread_Canvas_WebGPU_getImageData;
          case CanvasExtractionAPI::ReadPixels:
            logImpossible("ReadPixels invalid for WebGPU");
            return CanvasUsageSource::Impossible;
          default:
            logImpossible("Unknown API for WebGPU");
            return CanvasUsageSource::Impossible;
        }
      case dom::CanvasContextType::ImageBitmap:
        switch (api) {
          case CanvasExtractionAPI::ToDataURL:
            return MainThread_Canvas_ImageBitmap_toDataURL;
          case CanvasExtractionAPI::ToBlob:
            return MainThread_Canvas_ImageBitmap_toBlob;
          case CanvasExtractionAPI::GetImageData:
            return MainThread_Canvas_ImageBitmap_getImageData;
          case CanvasExtractionAPI::ReadPixels:
            logImpossible("ReadPixels invalid for ImageBitmap");
            return CanvasUsageSource::Impossible;
          default:
            logImpossible("Unknown API for ImageBitmap");
            return CanvasUsageSource::Impossible;
        }
      default:
        logImpossible("Unknown context type (main thread, non-offscreen)");
        return CanvasUsageSource::Impossible;
    }
  }

  // OffscreenCanvas path.
  switch (contextType) {
    case dom::CanvasContextType::Canvas2D:
      switch (api) {
        case CanvasExtractionAPI::ToDataURL:
          if (isMainThread) {
            return MainThread_OffscreenCanvas_Canvas2D_toDataURL;
          }
          logImpossible("ToDataURL invalid for Offscreen Canvas2D on worker");
          return CanvasUsageSource::Impossible;  // not supported
        case CanvasExtractionAPI::ToBlob:
          return isMainThread ? MainThread_OffscreenCanvas_Canvas2D_toBlob
                              : Worker_OffscreenCanvas_Canvas2D_toBlob;
        case CanvasExtractionAPI::GetImageData:
          return isMainThread ? MainThread_OffscreenCanvas_Canvas2D_getImageData
                              : Worker_OffscreenCanvas_Canvas2D_getImageData;
        case CanvasExtractionAPI::ReadPixels:
          logImpossible("ReadPixels invalid for Offscreen 2D");
          return CanvasUsageSource::Impossible;
        default:
          logImpossible("Unknown API for Offscreen Canvas2D");
          return CanvasUsageSource::Impossible;
      }
    case dom::CanvasContextType::OffscreenCanvas2D:
      switch (api) {
        case CanvasExtractionAPI::GetImageData:
          return Worker_OffscreenCanvasCanvas2D_Canvas2D_getImageData;
        case CanvasExtractionAPI::ToBlob:
          return Worker_OffscreenCanvasCanvas2D_Canvas2D_toBlob;
        default:
          logImpossible("Unsupported API for OffscreenCanvas2D");
          return CanvasUsageSource::Impossible;
      }
    case dom::CanvasContextType::WebGL1:
    case dom::CanvasContextType::WebGL2:
      switch (api) {
        case CanvasExtractionAPI::ToDataURL:
          if (!isMainThread) {
            logImpossible("ToDataURL invalid for Offscreen WebGL on worker");
            return CanvasUsageSource::Impossible;  // not supported
          }
          return MainThread_OffscreenCanvas_WebGL_toDataURL;
        case CanvasExtractionAPI::ToBlob:
          return isMainThread ? MainThread_OffscreenCanvas_WebGL_toBlob
                              : Worker_OffscreenCanvas_WebGL_toBlob;
        case CanvasExtractionAPI::GetImageData:
          return isMainThread ? MainThread_OffscreenCanvas_WebGL_getImageData
                              : Worker_OffscreenCanvas_WebGL_getImageData;
        case CanvasExtractionAPI::ReadPixels:
          return isMainThread ? MainThread_OffscreenCanvas_WebGL_readPixels
                              : Worker_OffscreenCanvas_WebGL_readPixels;
        default:
          logImpossible("Unknown API for Offscreen WebGL");
          return CanvasUsageSource::Impossible;
      }
    case dom::CanvasContextType::WebGPU:
      switch (api) {
        case CanvasExtractionAPI::ToDataURL:
          if (!isMainThread) {
            logImpossible("ToDataURL invalid for Offscreen WebGPU on worker");
            return CanvasUsageSource::Impossible;  // not supported
          }
          return MainThread_OffscreenCanvas_WebGPU_toDataURL;
        case CanvasExtractionAPI::ToBlob:
          return isMainThread ? MainThread_OffscreenCanvas_WebGPU_toBlob
                              : Worker_OffscreenCanvas_WebGPU_toBlob;
        case CanvasExtractionAPI::GetImageData:
          return isMainThread ? MainThread_OffscreenCanvas_WebGPU_getImageData
                              : Worker_OffscreenCanvas_WebGPU_getImageData;
        case CanvasExtractionAPI::ReadPixels:
          logImpossible("ReadPixels invalid for Offscreen WebGPU");
          return CanvasUsageSource::Impossible;
        default:
          logImpossible("Unknown API for Offscreen WebGPU");
          return CanvasUsageSource::Impossible;
      }
    case dom::CanvasContextType::ImageBitmap:
      switch (api) {
        case CanvasExtractionAPI::ToDataURL:
          if (!isMainThread) {
            logImpossible(
                "ToDataURL invalid for Offscreen ImageBitmap on worker");
            return CanvasUsageSource::Impossible;  // not supported worker
          }
          return MainThread_OffscreenCanvas_ImageBitmap_toDataURL;
        case CanvasExtractionAPI::ToBlob:
          return isMainThread ? MainThread_OffscreenCanvas_ImageBitmap_toBlob
                              : Worker_OffscreenCanvas_ImageBitmap_toBlob;
        case CanvasExtractionAPI::GetImageData:
          return isMainThread
                     ? MainThread_OffscreenCanvas_ImageBitmap_getImageData
                     : Worker_OffscreenCanvas_ImageBitmap_getImageData;
        case CanvasExtractionAPI::ReadPixels:
          logImpossible("ReadPixels invalid for Offscreen ImageBitmap");
          return CanvasUsageSource::Impossible;
        default:
          logImpossible("Unknown API for Offscreen ImageBitmap");
          return CanvasUsageSource::Impossible;
      }
    default:
      logImpossible("Unknown context type (offscreen)");
      return CanvasUsageSource::Impossible;
  }
}

static void MaybeCurrentCaller(nsACString& aFilename, uint32_t& aLineNum,
                               uint32_t& aColumnNum) {
  aFilename.AssignLiteral("<unknown>");

  JSContext* cx = nsContentUtils::GetCurrentJSContext();
  if (!cx) {
    return;
  }

  JS::AutoFilename scriptFilename;
  JS::ColumnNumberOneOrigin columnNum;
  if (JS::DescribeScriptedCaller(&scriptFilename, cx, &aLineNum, &columnNum)) {
    if (const char* file = scriptFilename.get()) {
      aFilename = nsDependentCString(file);
    }
  }
  aColumnNum = columnNum.oneOriginValue();
}

/* static */ void nsRFPService::MaybeReportCanvasFingerprinter(
    nsTArray<CanvasUsage>& aUses, nsIChannel* aChannel, nsIURI* aURI,
    const nsACString& aOriginNoSuffix) {
  if (!aChannel) {
    return;
  }

  nsAutoCString scheme;
  (void)aURI->GetScheme(scheme);
  // We exclude reporting for chrome and resource URIs.
  if (scheme.EqualsLiteral("chrome") || scheme.EqualsLiteral("resource")) {
    return;
  }

  bool extractedWebGL = false;
  bool seenExtractedWebGL_300x150 = false;
  bool seenExtractedWebGL_2000x200 = false;

  uint32_t extracted2D = 0;
  bool seenExtracted2D_122x110 = false;
  bool seenExtracted2D_220x30 = false;
  bool seenExtracted2D_240x60 = false;
  bool seenExtracted2D_250x80 = false;
  bool seenExtracted2D_300x100 = false;
  bool seenExtracted2D_650x12 = false;
  bool seenExtracted2D_860x6 = false;

  CanvasFeatureUsage accumulatedFeatureUsage = CanvasFeatureUsage::None;
  CanvasUsageSource accumulatedUsageSource = CanvasUsageSource::Unknown;

  MOZ_LOG(
      gFingerprinterDetection, LogLevel::Debug,
      ("MaybeReportCanvasFingerprinter: examining %zu uses", aUses.Length()));

  for (const auto& usage : aUses) {
    int32_t width = usage.mSize.width;
    int32_t height = usage.mSize.height;

    if (width > 2500 || height > 1000) {
      // Canvases used for fingerprinting are usually relatively small.
      continue;
    }

    accumulatedFeatureUsage |= usage.mFeatureUsage;
    accumulatedUsageSource |= usage.mUsageSource;

    if (usage.mType == dom::CanvasContextType::Canvas2D ||
        usage.mType == dom::CanvasContextType::OffscreenCanvas2D) {
      accumulatedFeatureUsage |= usage.mFeatureUsage;
      extracted2D++;
      if (width == 122 && height == 110) {
        seenExtracted2D_122x110 = true;
      } else if (width == 220 && height == 30) {
        seenExtracted2D_220x30 = true;
      } else if (width == 240 && height == 60) {
        seenExtracted2D_240x60 = true;
      } else if (width == 250 && height == 80) {
        seenExtracted2D_250x80 = true;
      } else if (width == 300 && height == 100) {
        seenExtracted2D_300x100 = true;
      } else if (width == 650 && height == 12) {
        seenExtracted2D_650x12 = true;
      } else if (width == 860 && height == 6) {
        seenExtracted2D_860x6 = true;
      }
    } else if (usage.mType == dom::CanvasContextType::WebGL1) {
      extractedWebGL = true;
      if (width == 300 && height == 150) {
        seenExtractedWebGL_300x150 = true;
      } else if (width == 2000 && height == 200) {
        seenExtractedWebGL_2000x200 = true;
      }
    }
  }

  CanvasFingerprinterAlias fingerprinter = eNoneIdentified;
  uint32_t knownTextBitmask = static_cast<uint32_t>(
      static_cast<uint64_t>(accumulatedFeatureUsage) & 0xFFFFFFFFu);

  if (seenExtractedWebGL_300x150 && seenExtracted2D_240x60 &&
      seenExtracted2D_122x110) {
    fingerprinter = CanvasFingerprinterAlias::eFingerprintJS;
  } else if (seenExtractedWebGL_300x150 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_12 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_13) {
    fingerprinter = CanvasFingerprinterAlias::eAkamai;
  } else if (accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_9 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_10 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_11) {
    fingerprinter = CanvasFingerprinterAlias::eOzoki;
  } else if (seenExtractedWebGL_2000x200 && knownTextBitmask == 0) {
    fingerprinter = CanvasFingerprinterAlias::ePerimeterX;
  } else if (seenExtracted2D_220x30 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_7) {
    fingerprinter = CanvasFingerprinterAlias::eSignifyd;
  } else if (seenExtracted2D_300x100 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_8) {
    fingerprinter = CanvasFingerprinterAlias::eSignifyd;
  } else if (seenExtracted2D_240x60 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_4) {
    fingerprinter = CanvasFingerprinterAlias::eClaydar;
  } else if (accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_23) {
    fingerprinter = CanvasFingerprinterAlias::eForter;
  } else if (accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_2) {
    fingerprinter = CanvasFingerprinterAlias::eImperva;
  } else if (accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_26) {
    fingerprinter = CanvasFingerprinterAlias::eClientGear;
  } else if (seenExtracted2D_250x80 &&
             accumulatedFeatureUsage & CanvasFeatureUsage::KnownText_6) {
    fingerprinter = CanvasFingerprinterAlias::eVariant5;
  } else if (seenExtracted2D_650x12) {
    fingerprinter = CanvasFingerprinterAlias::eVariant6;
  } else if (seenExtracted2D_860x6) {
    fingerprinter = CanvasFingerprinterAlias::eVariant7;
  } else if (seenExtractedWebGL_300x150 && extracted2D > 0 &&
             (accumulatedFeatureUsage & CanvasFeatureUsage::SetFont)) {
    fingerprinter = CanvasFingerprinterAlias::eVariant1;
  } else if (extractedWebGL > 0 && extracted2D > 1 && seenExtracted2D_860x6) {
    fingerprinter = CanvasFingerprinterAlias::eVariant2;
  }

  nsAutoCString uri;
  (void)aURI->GetSpec(uri);
  nsAutoCString origin(aOriginNoSuffix);
  nsAutoCString filename;
  if (MOZ_LOG_TEST(gFingerprinterDetection, LogLevel::Info)) {
    uint32_t lineNum = 0;
    uint32_t columnNum = 0;
    MaybeCurrentCaller(filename, lineNum, columnNum);
  }

  if (knownTextBitmask == 0 && fingerprinter == eNoneIdentified) {
    MOZ_LOG(gFingerprinterDetection, LogLevel::Debug,
            ("Found no potential canvas fingerprinter on %s on %s in script %s",
             origin.get(), uri.get(), filename.get()));
    return;
  }

  auto event = CanvasFingerprintingEvent(fingerprinter, knownTextBitmask,
                                         accumulatedUsageSource);

  MOZ_LOG(gFingerprinterDetection, LogLevel::Info,
          ("Detected a potential canvas fingerprinter on %s on %s in script %s "
           "(KnownFingerprintTextBitmask: %u, CanvasFingerprinterAlias: %s, "
           "AccumulatedCanvasUsageSource: %s)",
           origin.get(), uri.get(), filename.get(), knownTextBitmask,
           CanvasFingerprinterToString(fingerprinter),
           CanvasUsageSourceToString(accumulatedUsageSource).get()));

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsRFPService::MaybeReportCanvasFingerprinter::NotifyEvent",
      [channel = nsCOMPtr{aChannel}, origin = nsCString(aOriginNoSuffix),
       event = event]() {
        ContentBlockingNotifier::OnEvent(
            channel, false,
            nsIWebProgressListener::STATE_ALLOWED_CANVAS_FINGERPRINTING, origin,
            Nothing(), Some(event));
      }));
}

/* static */ void nsRFPService::MaybeReportFontFingerprinter(
    nsIChannel* aChannel, nsIURI* aURI, const nsACString& aOriginNoSuffix) {
  if (!aChannel) {
    return;
  }

  nsAutoCString scheme;
  (void)aURI->GetScheme(scheme);
  // We exclude reporting for chrome and resource URIs.
  if (scheme.EqualsLiteral("chrome") || scheme.EqualsLiteral("resource")) {
    return;
  }

  // The logging of the event will access nsLoadGroup which is main-thread only.
  // So we need to dispatch the task to the main thread if we are reporting
  // the event off-main-thread.
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsRFPService::MaybeReportFontFingerprinter",
        [channel = nsCOMPtr{aChannel},
         originNoSuffix = nsCString(aOriginNoSuffix), uri = nsCOMPtr{aURI}]() {
          nsRFPService::MaybeReportFontFingerprinter(channel, uri,
                                                     originNoSuffix);
        }));

    return;
  }

  nsAutoCString uri;
  (void)aURI->GetSpec(uri);
  nsAutoCString origin(aOriginNoSuffix);

  if (MOZ_LOG_TEST(gFingerprinterDetection, LogLevel::Info)) {
    nsAutoCString filename;
    uint32_t lineNum = 0;
    uint32_t columnNum = 0;
    MaybeCurrentCaller(filename, lineNum, columnNum);

    MOZ_LOG(gFingerprinterDetection, LogLevel::Info,
            ("Detected a potential font fingerprinter on %s on %s in script "
             "%s:%d:%d",
             origin.get(), uri.get(), filename.get(), lineNum, columnNum));
  }

  ContentBlockingNotifier::OnEvent(
      aChannel, false,
      nsIWebProgressListener::STATE_ALLOWED_FONT_FINGERPRINTING, origin);
}

/* static */
bool nsRFPService::IsSystemPrincipalOrAboutFingerprintingProtection(
    JSContext* aCx, JSObject* aObj) {
  if (!NS_IsMainThread()) {
    return false;
  }

  nsIPrincipal* principal = nsContentUtils::SubjectPrincipal(aCx);
  if (principal->IsSystemPrincipal()) {
    return true;
  }

  return principal->Equals(
      nsContentUtils::GetFingerprintingProtectionPrincipal());
}

/* static */
nsresult nsRFPService::CreateOverrideDomainKey(
    nsIFingerprintingOverride* aOverride, nsACString& aDomainKey) {
  MOZ_ASSERT(aOverride);

  aDomainKey.Truncate();

  nsAutoCString firstPartyDomain;
  nsresult rv = aOverride->GetFirstPartyDomain(firstPartyDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  bool isBaseline = false;
  rv = aOverride->GetIsBaseline(&isBaseline);
  NS_ENSURE_SUCCESS(rv, rv);

  // The first party domain shouldn't be empty. And it shouldn't contain a comma
  // because we use a comma as a delimiter.
  if (firstPartyDomain.IsEmpty() ||
      firstPartyDomain.Contains(FP_OVERRIDES_DOMAIN_KEY_DELIMITER)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString thirdPartyDomain;
  rv = aOverride->GetThirdPartyDomain(thirdPartyDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  // We don't accept both domains are wildcards.
  if (firstPartyDomain.EqualsLiteral("*") &&
      thirdPartyDomain.EqualsLiteral("*")) {
    return NS_ERROR_FAILURE;
  }

  if (thirdPartyDomain.IsEmpty()) {
    aDomainKey.Assign(firstPartyDomain);
  } else {
    // Ensure the third-party domain doesn't contain a delimiter.
    if (thirdPartyDomain.Contains(FP_OVERRIDES_DOMAIN_KEY_DELIMITER)) {
      return NS_ERROR_FAILURE;
    }

    aDomainKey.Assign(firstPartyDomain);
    aDomainKey.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
    aDomainKey.Append(thirdPartyDomain);
  }

  aDomainKey.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
  aDomainKey.Append(isBaseline ? "1" : "0");

  return NS_OK;
}

/* static */
RFPTargetSet nsRFPService::CreateOverridesFromText(
    const nsString& aOverridesText, RFPTargetSet aBaseOverrides) {
  RFPTargetSet result = aBaseOverrides;

  for (const nsAString& token : aOverridesText.Split(',')) {
    // Tolerate whitespace around each entry, e.g. "+A, -B".
    nsAutoString each(token);
    each.Trim(" \t\r\n");
    if (each.IsEmpty()) {
      continue;
    }

    Maybe<RFPTarget> mappedValue =
        nsRFPService::TextToRFPTarget(Substring(each, 1, each.Length() - 1));
    if (mappedValue.isNothing()) {
      MOZ_LOG(gResistFingerprintingLog, LogLevel::Warning,
              ("Could not map the value %s to an RFPTarget Enum",
               NS_ConvertUTF16toUTF8(each).get()));
      continue;
    }
    RFPTarget target = mappedValue.value();
    RFPTargetSet targetSet = RFPTargetSet(target);
    if (target == RFPTarget::AllTargets) {
      std::bitset<128> allTargets;
      allTargets.set();
      targetSet = RFPTargetSet(allTargets);
    }
    if (target == RFPTarget::IsAlwaysEnabledForPrecompute) {
      MOZ_LOG(gResistFingerprintingLog, LogLevel::Warning,
              ("RFPTarget::%s is not a valid value",
               NS_ConvertUTF16toUTF8(each).get()));
    } else if (each[0] == '+') {
      result += targetSet;
      MOZ_LOG(
          gResistFingerprintingLog, LogLevel::Warning,
          ("Mapped value %s (0x%" PRIx64 "), to an addition, now we have %s",
           NS_ConvertUTF16toUTF8(each).get(), static_cast<uint64_t>(target),
           result.serialize().to_string().c_str()));
    } else if (each[0] == '-') {
      result -= targetSet;
      MOZ_LOG(
          gResistFingerprintingLog, LogLevel::Warning,
          ("Mapped value %s (0x%" PRIx64 ") to a subtraction, now we have %s",
           NS_ConvertUTF16toUTF8(each).get(), static_cast<uint64_t>(target),
           result.serialize().to_string().c_str()));
    } else {
      MOZ_LOG(
          gResistFingerprintingLog, LogLevel::Warning,
          ("Mapped value %s (0x%" PRIx64
           ") to an RFPTarget Enum, but the first "
           "character wasn't + or -",
           NS_ConvertUTF16toUTF8(each).get(), static_cast<uint64_t>(target)));
    }
  }

  return result;
}

NS_IMETHODIMP
nsRFPService::SetFingerprintingOverrides(
    const nsTArray<RefPtr<nsIFingerprintingOverride>>& aOverrides) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Clear all overrides before importing.
  CleanAllOverrides();

  // Read the overrides out of the (JS-implemented) override objects before
  // taking the lock, so that we don't call into JS while holding it.
  struct ParsedOverride {
    nsCString mDomainKey;
    nsCString mOverridesText;
    bool mIsBaseline = false;
  };
  nsTArray<ParsedOverride> parsedOverrides(aOverrides.Length());

  for (const auto& fpOverride : aOverrides) {
    ParsedOverride parsed;

    nsresult rv =
        nsRFPService::CreateOverrideDomainKey(fpOverride, parsed.mDomainKey);
    // Skip the current overrides if we fail to create the domain key.
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    rv = fpOverride->GetOverrides(parsed.mOverridesText);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = fpOverride->GetIsBaseline(&parsed.mIsBaseline);
    NS_ENSURE_SUCCESS(rv, rv);

    parsedOverrides.AppendElement(std::move(parsed));
  }

  {
    StaticMutexAutoLock lock(sEnabledFingerprintingProtectionsMutex);
    for (const auto& parsed : parsedOverrides) {
      RFPTargetSet baseOverrides = parsed.mIsBaseline
                                       ? sEnabledFingerprintingProtectionsBase
                                       : sEnabledFingerprintingProtections;
      RFPTargetSet targets = nsRFPService::CreateOverridesFromText(
          NS_ConvertUTF8toUTF16(parsed.mOverridesText), baseOverrides);

      // The newly added one will replace the existing one for the given
      // domain key.
      mFingerprintingOverrides.InsertOrUpdate(parsed.mDomainKey, targets);
    }
  }

  if (Preferences::GetBool(
          "privacy.fingerprintingProtection.remoteOverrides.testing", false)) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    NS_ENSURE_TRUE(obs, NS_ERROR_NOT_AVAILABLE);

    obs->NotifyObservers(nullptr, "fpp-test:set-overrides-finishes", nullptr);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::GetEnabledFingerprintingProtectionsBaseline(
    nsIRFPTargetSetIDL** aProtections) {
  StaticMutexAutoLock lock(sEnabledFingerprintingProtectionsMutex);
  RFPTargetSet enabled = sEnabledFingerprintingProtectionsBase;

  nsCOMPtr<nsIRFPTargetSetIDL> protections = new nsRFPTargetSetIDL(enabled);
  protections.forget(aProtections);

  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::GetEnabledFingerprintingProtections(
    nsIRFPTargetSetIDL** aProtections) {
  StaticMutexAutoLock lock(sEnabledFingerprintingProtectionsMutex);
  RFPTargetSet enabled = sEnabledFingerprintingProtections;

  nsCOMPtr<nsIRFPTargetSetIDL> protections = new nsRFPTargetSetIDL(enabled);
  protections.forget(aProtections);

  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::GetFingerprintingOverrides(const nsACString& aDomainKey,
                                         nsIRFPTargetSetIDL** aOverrides) {
  MOZ_ASSERT(XRE_IsParentProcess());

  Maybe<RFPTargetSet> overrides = mFingerprintingOverrides.MaybeGet(aDomainKey);

  if (!overrides) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIRFPTargetSetIDL> protections =
      new nsRFPTargetSetIDL(overrides.ref());
  protections.forget(aOverrides);

  return NS_OK;
}

NS_IMETHODIMP
nsRFPService::CleanAllOverrides() {
  MOZ_ASSERT(XRE_IsParentProcess());
  mFingerprintingOverrides.Clear();
  return NS_OK;
}

/* static */
Maybe<RFPTargetSet> nsRFPService::GetOverriddenFingerprintingSettingsForChannel(
    nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsIURI> uri;
  (void)aChannel->GetURI(getter_AddRefs(uri));

  if (uri->SchemeIs("about") && !NS_IsContentAccessibleAboutURI(uri)) {
    return Nothing();
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  MOZ_ASSERT(loadInfo);

  RefPtr<dom::BrowsingContext> bc;
  loadInfo->GetTargetBrowsingContext(getter_AddRefs(bc));
  if (!bc || !bc->IsContent()) {
    return Nothing();
  }

  bool isPrivate = loadInfo->GetOriginAttributes().IsPrivateBrowsing();

  // The channel is for the first-party load.
  if (!AntiTrackingUtils::IsThirdPartyChannel(aChannel)) {
    return GetOverriddenFingerprintingSettingsForURI(uri, nullptr, isPrivate);
  }

  // The channel is for the third-party load. We get the first-party URI from
  // the top-level window global parent.
  RefPtr<dom::CanonicalBrowsingContext> topBC = bc->Top()->Canonical();
  RefPtr<dom::WindowGlobalParent> topWGP = topBC->GetCurrentWindowGlobal();

  if (NS_WARN_IF(!topWGP)) {
    return Nothing();
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  DebugOnly<nsresult> rv =
      loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  MOZ_ASSERT(cookieJarSettings);

  uint64_t topWindowContextIdFromCJS =
      net::CookieJarSettings::Cast(cookieJarSettings)
          ->GetTopLevelWindowContextId();

  // The top-level window could be navigated away when we get the fingerprinting
  // override here. For example, the beacon requests. In this case, the
  // top-level windowContext id won't match the inner window id of the top-level
  // windowGlobalParent. So, we cannot rely on the URI from the top-level
  // windowGlobalParent because it could be different from the one that creates
  // the channel. Instead, we fallback to use the partitionKey in the
  // cookieJarSettings to get the top-level URI.
  if (topWGP->InnerWindowId() != topWindowContextIdFromCJS) {
    nsAutoString partitionKey;
    rv = cookieJarSettings->GetPartitionKey(partitionKey);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // Bail out early if the partitionKey is empty.
    if (partitionKey.IsEmpty()) {
      return Nothing();
    }

    nsAutoString scheme;
    nsAutoString domain;
    int32_t unused;
    bool unused2;
    if (!OriginAttributes::ParsePartitionKey(partitionKey, scheme, domain,
                                             unused, unused2)) {
      // A null-principal (e.g. data: URL) top-level page stores its partition
      // key as "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.mozilla" (UUID format),
      // which ParsePartitionKey cannot handle.  Detect this via string matching
      // and bail out silently; any other unparseable key is unexpected.
      MOZ_ASSERT(partitionKey.Length() == 44 &&
                     StringEndsWith(partitionKey, u".mozilla"_ns) &&
                     partitionKey[8] == u'-' && partitionKey[13] == u'-' &&
                     partitionKey[18] == u'-' && partitionKey[23] == u'-',
                 "Failed to parse partitionKey from cookieJarSettings");
      return Nothing();
    }

    nsCOMPtr<nsIURI> topURI;
    rv = NS_NewURI(getter_AddRefs(topURI), scheme + u"://"_ns + domain);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    return GetOverriddenFingerprintingSettingsForURI(topURI, uri, isPrivate);
  }

  nsCOMPtr<nsIPrincipal> topPrincipal = topWGP->DocumentPrincipal();
  if (NS_WARN_IF(!topPrincipal)) {
    return Nothing();
  }

  // Only apply the override if the top is content. In testing, the top level
  // document could be a null principal. We don't need to apply override in this
  // case.
  if (!topPrincipal->GetIsContentPrincipal()) {
    return Nothing();
  }

  nsCOMPtr<nsIURI> topURI = topWGP->GetDocumentURI();
  if (NS_WARN_IF(!topURI)) {
    return Nothing();
  }

  // The top-level page could be navigated to an error page. We cannot get
  // the correct override in this case. So, we return nothing from here.
  if (nsContentUtils::IsErrorPage(topURI)) {
    return Nothing();
  }

#ifdef DEBUG
  // Verify if the top URI matches the partitionKey of the channel.
  nsAutoString partitionKey;
  cookieJarSettings->GetPartitionKey(partitionKey);

  nsAutoCString topPrincipalOriginNoSuffix;
  rv = topPrincipal->GetOriginNoSuffix(topPrincipalOriginNoSuffix);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsCOMPtr<nsIURI> topPrincipalURI;
  rv = NS_NewURI(getter_AddRefs(topPrincipalURI), topPrincipalOriginNoSuffix);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  OriginAttributes attrs;
  attrs.SetPartitionKey(topPrincipalURI, false);

  OriginAttributes attrsForeignByAncestor;
  attrsForeignByAncestor.SetPartitionKey(topPrincipalURI, true);

  // The partitionKey of the channel could haven't been set here if the loading
  // channel is top-level.
  MOZ_ASSERT_IF(!partitionKey.IsEmpty(),
                attrs.mPartitionKey.Equals(partitionKey) ||
                    attrsForeignByAncestor.mPartitionKey.Equals(partitionKey));
#endif

  return GetOverriddenFingerprintingSettingsForURI(topURI, uri, isPrivate);
}

/* static */
Maybe<RFPTargetSet> nsRFPService::GetOverriddenFingerprintingSettingsForURI(
    nsIURI* aFirstPartyURI, nsIURI* aThirdPartyURI, bool aIsPrivate) {
  MOZ_ASSERT(aFirstPartyURI);
  MOZ_ASSERT(XRE_IsParentProcess());

  RefPtr<nsRFPService> service = GetOrCreate();
  if (NS_WARN_IF(!service)) {
    return Nothing();
  }

  // The fingerprinting overrides with a specific scope will replace the
  // overrides with a more general scope. For example, the {first-party domain}
  // will take over {first-party domain, *} because the latter one has a smaller
  // scope.

  bool isBaseline = !IsFPPEnabled(aIsPrivate);
  auto addIsBaseline = [](nsAutoCString& aKey, bool aIsBaseline) {
    aKey.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
    aKey.Append(aIsBaseline ? "1" : "0");
  };

  // First, we get the overrides that applies to every context.
  nsAutoCString key;
  key.Assign("*"_ns);
  addIsBaseline(key, isBaseline);
  Maybe<RFPTargetSet> result = service->mFingerprintingOverrides.MaybeGet(key);

  nsCOMPtr<nsIEffectiveTLDService> eTLDService =
      mozilla::components::EffectiveTLD::Service();
  if (NS_WARN_IF(!eTLDService)) {
    return Nothing();
  }

  nsAutoCString firstPartyDomain;
  nsresult rv = eTLDService->GetBaseDomain(aFirstPartyURI, 0, firstPartyDomain);
  if (NS_FAILED(rv)) {
    return Nothing();
  }

  // The check is for a first-party load. A first-party load can be a
  // top-level load or a first-party subresource/iframe load. The first-party
  // load can match the following two scopes.
  //   1. {first-party domain, *}: Every context that is under the given
  //   first-party domain, including itself.
  //   2. {first-party domain}: First-party contexts that load the given
  //   first-party domain.
  if (!aThirdPartyURI) {
    // Test the {first-party domain, *} scope.
    key.Assign(firstPartyDomain);
    key.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
    key.Append("*"_ns);
    addIsBaseline(key, isBaseline);
    Maybe<RFPTargetSet> fpOverrides =
        service->mFingerprintingOverrides.MaybeGet(key);
    if (fpOverrides) {
      result = fpOverrides;
    }

    // Test the {first-party domain} scope.
    key.Assign(firstPartyDomain);
    addIsBaseline(key, isBaseline);
    fpOverrides = service->mFingerprintingOverrides.MaybeGet(key);
    if (fpOverrides) {
      result = std::move(fpOverrides);
    }

    return result;
  }

  // The check is for a third-party load. The third-party load can match the
  // following three scopes.
  //   1. {first-party domain, *}: Every context that is under the given
  //   first-party domain.
  //   2. {*, third-party domain}: Every third-party context that loads the
  //   given third-party domain.
  //   3. {first-party domain, third-party domain}: The third-party context that
  //   is under the given first-party domain.

  nsAutoCString thirdPartyDomain;
  rv = eTLDService->GetBaseDomain(aThirdPartyURI, 0, thirdPartyDomain);
  if (NS_FAILED(rv)) {
    return Nothing();
  }

  // Test {first-party domain, *} scope.
  key.Assign(firstPartyDomain);
  key.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
  key.Append("*"_ns);
  addIsBaseline(key, isBaseline);
  Maybe<RFPTargetSet> fpOverrides =
      service->mFingerprintingOverrides.MaybeGet(key);
  if (fpOverrides) {
    result = fpOverrides;
  }

  // Test {*, third-party domain} scope.
  key.Assign("*");
  key.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
  key.Append(thirdPartyDomain);
  addIsBaseline(key, isBaseline);
  fpOverrides = service->mFingerprintingOverrides.MaybeGet(key);
  if (fpOverrides) {
    result = fpOverrides;
  }

  // Test {first-party domain, third-party domain} scope.
  key.Assign(firstPartyDomain);
  key.Append(FP_OVERRIDES_DOMAIN_KEY_DELIMITER);
  key.Append(thirdPartyDomain);
  addIsBaseline(key, isBaseline);
  fpOverrides = service->mFingerprintingOverrides.MaybeGet(key);
  if (fpOverrides) {
    result = std::move(fpOverrides);
  }

  return result;
}

/* static */
void nsRFPService::GetMediaDeviceName(nsString& aName,
                                      dom::MediaDeviceKind aKind) {
  switch (aKind) {
    case dom::MediaDeviceKind::Audioinput:
      aName.Assign(u"Internal Microphone"_ns);
      break;
    case dom::MediaDeviceKind::Videoinput:
      aName = u"Internal Camera"_ns;
      break;
    case dom::MediaDeviceKind::Audiooutput:
      aName = u"Internal Speaker"_ns;
      break;
  }
}

/* static */
void nsRFPService::GetMediaDeviceGroup(nsString& aGroup,
                                       dom::MediaDeviceKind aKind) {
  switch (aKind) {
    case dom::MediaDeviceKind::Audioinput:
    case dom::MediaDeviceKind::Audiooutput:
      aGroup.Assign(u"Audio Device Group"_ns);
      break;
    case dom::MediaDeviceKind::Videoinput:
      aGroup = u"Video Device Group"_ns;
      break;
  }
}

/* static */
uint16_t nsRFPService::ViewportSizeToAngle(int32_t aWidth, int32_t aHeight) {
  // Note that, if screen is square, we return portrait-primary.
  // That's why we use > on non-android and >= on Android.
#ifdef MOZ_WIDGET_ANDROID
  bool neutral = aHeight >= aWidth;
#else
  bool neutral = aWidth > aHeight;
#endif
  if (neutral) {
    return 0;
  }
  return 90;
}

/* static */
dom::OrientationType nsRFPService::ViewportSizeToOrientationType(
    int32_t aWidth, int32_t aHeight) {
  if (aWidth > aHeight) {
    return dom::OrientationType::Landscape_primary;
  }
  return dom::OrientationType::Portrait_primary;
}

/* static */
dom::OrientationType nsRFPService::GetDefaultOrientationType() {
#ifdef MOZ_WIDGET_ANDROID
  return dom::OrientationType::Portrait_primary;
#else
  return dom::OrientationType::Landscape_primary;
#endif
}

/* static */
float nsRFPService::GetDefaultPixelDensity() { return 2.0f; }

/* static */
double nsRFPService::GetDevicePixelRatioAtZoom(float aZoom) {
  // nsPresContext includes system zoom settings into its zoom factor.
  // Even at 100% zoom shown in the UI, aZoom is not 1.0f, it is 1.0f *
  // SystemZoom See
  // https://searchfox.org/mozilla-central/rev/5bea6ede57be43d450ecc24af7a535288c9a9f7d/layout/base/nsPresContext.cpp#994
  aZoom /= LookAndFeel::SystemZoomSettings().mFullZoom;

  // Use the same logic as UpdateAppUnitsForFullZoom and ApplyFullZoom from
  // nsDeviceContext.cpp.
  int32_t unzoomedAppUnits =
      NS_lround(AppUnitsPerCSSPixel() / GetDefaultPixelDensity());
  int32_t appUnitsPerDevPixel =
      aZoom == 1.0f
          ? unzoomedAppUnits
          : std::max(1, NSToIntRound(float(unzoomedAppUnits) / aZoom));
  return double(AppUnitsPerCSSPixel()) / double(appUnitsPerDevPixel);
}

nsCString* nsRFPService::sExemptedDomainsLowercase = nullptr;

/* static */
void nsRFPService::GetExemptedDomainsLowercase(nsCString& aExemptedDomains) {
#define EXEMPTED_DOMAINS_PREF_NAME \
  "privacy.resistFingerprinting.exemptedDomains"

  // Callers compare this against a lower-cased host, and
  // nsContentUtils::IsURIInList() asserts that the list is lower-case.
  static bool sInited = false;
  if (!sInited) {
    sInited = true;
    sExemptedDomainsLowercase = new nsCString();
    ClearOnShutdown(sExemptedDomainsLowercase);
    Preferences::GetCString(EXEMPTED_DOMAINS_PREF_NAME,
                            *sExemptedDomainsLowercase);
    ToLowerCase(*sExemptedDomainsLowercase);
    Preferences::RegisterCallback(
        [](const char* aPref, void* aData) {
          Preferences::GetCString(EXEMPTED_DOMAINS_PREF_NAME,
                                  *sExemptedDomainsLowercase);
          ToLowerCase(*sExemptedDomainsLowercase);
        },
        EXEMPTED_DOMAINS_PREF_NAME);
  }

  aExemptedDomains = *sExemptedDomainsLowercase;

#undef EXEMPTED_DOMAINS_PREF_NAME
}

/* static */
CSSIntRect nsRFPService::GetSpoofedScreenAvailSize(const nsRect& aRect,
                                                   float aScale,
                                                   bool aIsFullscreen) {
  int spoofedHeightOffset = aIsFullscreen ? 0 :
#ifdef XP_WIN
                                          48;
#elif defined(XP_MACOSX)
                                          76;
#else
                                          // Linux, Android and other platforms
                                0;
#endif
  spoofedHeightOffset =
      NS_lround(float(spoofedHeightOffset) / aScale * AppUnitsPerCSSPixel());

  int spoofedHeightStart = aIsFullscreen ? 0 :
#ifdef XP_MACOSX
                                         25;
#else
                                         0;
#endif
  spoofedHeightStart =
      NS_lround(float(spoofedHeightStart) / aScale * AppUnitsPerCSSPixel());

  return CSSIntRect::FromAppUnitsRounded(nsRect{
      0, spoofedHeightStart, aRect.width, aRect.height - spoofedHeightOffset});
}

/* static */
int64_t nsRFPService::GetSpoofedStorageLimit() {
  int64_t limit = 50LL * 1024LL * 1024LL * 1024LL;  // 50 GiB
  MOZ_ASSERT(limit / 5 ==
             dom::quota::QuotaManager::GetGroupLimitForLimit(limit));

  return limit;
}

/* static */
bool nsRFPService::ExposeWebCodecsAPI(JSContext* aCx, JSObject* aObj) {
  if (!StaticPrefs::dom_media_webcodecs_enabled()) {
    return false;
  }

  return !IsWebCodecsRFPTargetEnabled(aCx);
}

/* static */
bool nsRFPService::ExposeWebCodecsAPIImageDecoder(JSContext* aCx,
                                                  JSObject* aObj) {
  if (!StaticPrefs::dom_media_webcodecs_image_decoder_enabled()) {
    return false;
  }

  return !IsWebCodecsRFPTargetEnabled(aCx);
}

/* static */
bool nsRFPService::IsWebCodecsRFPTargetEnabled(JSContext* aCx) {
  if (!nsContentUtils::ShouldResistFingerprinting("Efficiency check",
                                                  RFPTarget::WebCodecs)) {
    return false;
  }

  // We know that the RFPTarget::WebCodecs is enabled, check if principal
  // is exempted.

  if (NS_WARN_IF(!aCx)) {
    MOZ_LOG(gResistFingerprintingLog, LogLevel::Warning,
            ("nsRFPService::IsWebCodecsRFPTargetEnabled called with null "
             "JSContext"));
    return true;
  }

  // Once bug 1973966 is resolved, we can replace this section with just
  // nsIPrincipal* principal = nsContentUtils::SubjectPrincipal(aCx);
  JS::Realm* realm = js::GetContextRealm(aCx);
  MOZ_ASSERT(realm);
  JSPrincipals* principals = JS::GetRealmPrincipals(realm);
  nsIPrincipal* principal = nsJSPrincipals::get(principals);

  return nsContentUtils::ShouldResistFingerprinting_dangerous(
      principal, "Principal is the best context we have", RFPTarget::WebCodecs);
}

uint32_t nsRFPService::CollapseMaxTouchPoints(uint32_t aMaxTouchPoints) {
  if (aMaxTouchPoints <= 1) {
    return aMaxTouchPoints;
  }
  // Android reports 5 as the max value, if a device supports multi-touch at all
  // collapse them all to 5. A minority of devices (mostly desktop devices)
  // will report uncommon values like 2, 3, 8, etc
  return 5;
}

/* static */
void nsRFPService::GetFingerprintingRandomizationKeyAsString(
    nsICookieJarSettings* aCookieJarSettings,
    nsACString& aRandomizationKeyStr) {
  NS_ENSURE_TRUE_VOID(aCookieJarSettings);

  nsTArray<uint8_t> randomizationKey(32);
  nsresult rv =
      aCookieJarSettings->GetFingerprintingRandomizationKey(randomizationKey);
  NS_ENSURE_SUCCESS_VOID(rv);

  aRandomizationKeyStr.Assign(
      reinterpret_cast<const char*>(randomizationKey.Elements()),
      randomizationKey.Length());
}

/* static */
nsresult nsRFPService::GenerateRandomizationKeyFromHash(
    const nsACString& aRandomizationKeyStr, uint32_t aContentHash,
    nsACString& aHex) {
  MOZ_ASSERT(aHex.IsEmpty(), "aHex should be empty");

  if (aRandomizationKeyStr.IsEmpty()) {
    return NS_ERROR_INVALID_ARG;
  }

  uint64_t k0 = *reinterpret_cast<const uint64_t*>(aRandomizationKeyStr.Data());
  uint64_t k1 =
      *reinterpret_cast<const uint64_t*>(aRandomizationKeyStr.Data() + 8);
  mozilla::HashCodeScrambler hcs(k0, k1);
  mozilla::HashNumber hashResult = hcs.scramble(aContentHash);

  nsTArray<uint8_t> bufferKey(32);

  uint64_t digest = static_cast<uint64_t>(hashResult) << 32 | aContentHash;
  non_crypto::XorShift128PlusRNG rng(
      digest,
      *reinterpret_cast<const uint64_t*>(aRandomizationKeyStr.Data() + 16));

  for (size_t i = 0; i < 4; ++i) {
    uint64_t val = rng.next();
    for (size_t j = 0; j < 8; ++j) {
      uint8_t data = static_cast<uint8_t>((val >> (j * 8)) & 0xFF);
      bufferKey.InsertElementAt((i * 8) + j, data);
    }
  }

  non_crypto::XorShift128PlusRNG rng1(
      *reinterpret_cast<uint64_t*>(bufferKey.Elements()),
      *reinterpret_cast<uint64_t*>(bufferKey.Elements() + 8));

  uint64_t rand = rng1.next();

  aHex.AppendPrintf("%016" PRIX64, rand);
  MOZ_ASSERT(aHex.Length() == 16, "Expected 16 hex characters");

  return NS_OK;
}

/* static */
void nsRFPService::CalculateFontLocaleAllowlist() {
  static bool sAcceptLanguagesIsDirty = true;

  enum MatchSetting : uint8_t { StartsWith, EndsWith, Exact };

  struct LocaleMatchingRule {
    const char* lang;
    MatchSetting matchSetting;
  };

  struct FontInclusionRule {
    const char* fontName;
    const LocaleMatchingRule* langs;
  };

#define FONT_RULE(font, ...)                          \
  []() -> FontInclusionRule {                         \
    static const LocaleMatchingRule _langs[] = {      \
        __VA_ARGS__, {nullptr, MatchSetting::Exact}}; \
    return FontInclusionRule{font, _langs};           \
  }(),

  static const FontInclusionRule fontInclusionRules[] = {
#define FontInclusionByLocaleRules

#ifdef XP_WIN
#  include "../../gfx/thebes/StandardFonts-win10.inc"
#elif defined(XP_MACOSX)
#  include "../../gfx/thebes/StandardFonts-macos.inc"
#elif defined(XP_LINUX)
#  if defined(ANDROID)
#    include "../../gfx/thebes/StandardFonts-android.inc"
#  else
#    include "../../gfx/thebes/StandardFonts-linux.inc"
#  endif
#endif

#undef FontInclusionByLocaleRules
  };

#undef FONT_RULE

  static std::once_flag sOnce;
  std::call_once(sOnce, []() {
    Preferences::RegisterCallback(
        [](const char*, void*) { sAcceptLanguagesIsDirty = true; },
        INTL_ACCEPT_LANGUAGES_PREF);
  });

  MOZ_ASSERT(NS_IsMainThread());

  if (sAcceptLanguagesIsDirty) {
    if (!sAllowedFonts) {
      sAllowedFonts = new nsTArray<nsCString>();
      ClearOnShutdown(&sAllowedFonts);
    } else {
      sAllowedFonts->ClearAndRetainStorage();
    }

    nsAutoCString acceptLang;
    nsresult rv =
        intl::LocaleService::GetInstance()->GetAcceptLanguages(acceptLang);
    NS_ENSURE_SUCCESS_VOID(rv);

    ToLowerCase(acceptLang);

    for (const nsDependentCSubstring& locale :
         nsCCharSeparatedTokenizer(acceptLang, ',').ToRange()) {
      for (const FontInclusionRule& fontRules : fontInclusionRules) {
        for (const LocaleMatchingRule* localeRule = fontRules.langs;
             localeRule->lang != nullptr; localeRule++) {
          bool matched = false;
          switch (localeRule->matchSetting) {
            case MatchSetting::Exact: {
              if (locale.Equals(localeRule->lang)) {
                matched = true;
              }
              break;
            }
            case MatchSetting::StartsWith: {
              if (StringBeginsWith(locale,
                                   nsDependentCString(localeRule->lang))) {
                matched = true;
              }
              break;
            }
            case MatchSetting::EndsWith: {
              if (StringEndsWith(locale,
                                 nsDependentCString(localeRule->lang))) {
                matched = true;
              }
              break;
            }
            default: {
              MOZ_ASSERT_UNREACHABLE("Unknown match setting");
              break;
            }
          }
          if (matched) {
            sAllowedFonts->AppendElement(fontRules.fontName);
            break;
          }
        }
      }
    }

    sAcceptLanguagesIsDirty = false;
  }
}

/* static */
bool nsRFPService::FontIsAllowedByLocale(const nsACString& aName) {
  if (NS_IsMainThread()) {
    CalculateFontLocaleAllowlist();
  } else {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("CalculateFontLocaleAllowlist",
                               &nsRFPService::CalculateFontLocaleAllowlist));
  }

  if (!sAllowedFonts || sAllowedFonts->IsEmpty() || aName.IsEmpty()) {
    return false;
  }

  // Check if the font is in the allowed list.
  for (const nsCString& font : *sAllowedFonts) {
    if (aName.Equals(font, nsCaseInsensitiveCStringComparator)) {
      return true;
    }
  }

  return false;
}
