/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/CaptivePortalService.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsIObserverService.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"
#include "xpcprivate.h"
#include "xpcpublic.h"

static constexpr auto kInterfaceName = u"captive-portal-inteface"_ns;

static const char kOpenCaptivePortalLoginEvent[] = "captive-portal-login";
static const char kAbortCaptivePortalLoginEvent[] =
    "captive-portal-login-abort";
static const char kCaptivePortalLoginSuccessEvent[] =
    "captive-portal-login-success";

namespace mozilla {
namespace net {

static LazyLogModule gCaptivePortalLog("CaptivePortalService");
#undef LOG
#define LOG(args) MOZ_LOG(gCaptivePortalLog, mozilla::LogLevel::Debug, args)

NS_IMPL_ISUPPORTS(CaptivePortalService, nsICaptivePortalService, nsIObserver,
                  nsISupportsWeakReference, nsITimerCallback,
                  nsICaptivePortalCallback, nsINamed)

static StaticRefPtr<CaptivePortalService> gCPService;

// static
already_AddRefed<nsICaptivePortalService> CaptivePortalService::GetSingleton() {
  if (gCPService) {
    return do_AddRef(gCPService);
  }

  gCPService = new CaptivePortalService();
  ClearOnShutdown(&gCPService);
  return do_AddRef(gCPService);
}

CaptivePortalService::CaptivePortalService() {
  mLastChecked = TimeStamp::Now();
}

CaptivePortalService::~CaptivePortalService() {
  LOG(("CaptivePortalService::~CaptivePortalService isParentProcess:%d\n",
       XRE_GetProcessType() == GeckoProcessType_Default));
}

bool CaptivePortalService::IsActive() const {
  return mStarted && StaticPrefs::network_captive_portal_service_enabled();
}

nsresult CaptivePortalService::PerformCheck() {
  LOG(
      ("CaptivePortalService::PerformCheck mRequestInProgress:%d "
       "mInitialized:%d mStarted:%d\n",
       mRequestInProgress, mInitialized, mStarted));
  // Don't issue another request if last one didn't complete
  if (mRequestInProgress || !mInitialized || !IsActive()) {
    return NS_OK;
  }
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }
  // Instantiating CaptiveDetect.sys.mjs before the JS engine is ready will
  // lead to a crash (see bug 1800603)
  // We can remove this restriction when we rewrite the detector in
  // C++ or rust (bug 1809886).
  if (!XPCJSRuntime::Get()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  nsresult rv;
  if (!mCaptivePortalDetector) {
    mCaptivePortalDetector =
        do_CreateInstance("@mozilla.org/toolkit/captive-detector;1", &rv);
    if (NS_FAILED(rv)) {
      LOG(("Unable to get a captive portal detector\n"));
      return rv;
    }
  }

  LOG(("CaptivePortalService::PerformCheck - Calling CheckCaptivePortal\n"));
  mRequestInProgress = true;
  mCaptivePortalDetector->CheckCaptivePortal(kInterfaceName, this);
  return NS_OK;
}

nsresult CaptivePortalService::RearmTimer() {
  LOG(("CaptivePortalService::RearmTimer\n"));
  // Start a timer to recheck
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  if (mTimer) {
    mTimer->Cancel();
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    mTimer = nullptr;
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!IsActive()) {
    return NS_OK;
  }

  // If we have successfully determined the state, and we have never detected
  // a captive portal, we don't need to keep polling, but will rely on events
  // to trigger detection.
  if (mState == NOT_CAPTIVE) {
    return NS_OK;
  }

  if (!mTimer) {
    mTimer = NS_NewTimer();
  }

  if (mTimer && mDelay > 0) {
    LOG(("CaptivePortalService - Reloading timer with delay %u\n", mDelay));
    return mTimer->InitWithCallback(this, mDelay, nsITimer::TYPE_ONE_SHOT);
  }

  return NS_OK;
}

nsresult CaptivePortalService::Initialize() {
  if (mInitialized) {
    return NS_OK;
  }
  mInitialized = true;

  // Only the main process service should actually do anything. The service in
  // the content process only mirrors the CP state in the main process.
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    return NS_OK;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(this, kOpenCaptivePortalLoginEvent, true);
    observerService->AddObserver(this, kAbortCaptivePortalLoginEvent, true);
    observerService->AddObserver(this, kCaptivePortalLoginSuccessEvent, true);
    observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
  }

  LOG(("Initialized CaptivePortalService\n"));
  return NS_OK;
}

nsresult CaptivePortalService::Start() {
  if (!mInitialized) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (!StaticPrefs::network_captive_portal_service_enabled()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (xpc::AreNonLocalConnectionsDisabled() &&
      !Preferences::GetBool("network.captive-portal-service.testMode", false)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    // Doesn't do anything if called in the content process.
    return NS_OK;
  }

  if (mStarted) {
    return NS_OK;
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  MOZ_ASSERT(mState == UNKNOWN, "Initial state should be UNKNOWN");
  mStarted = true;
  mEverBeenCaptive = false;

  mMinInterval = StaticPrefs::network_captive_portal_service_minInterval();
  mMaxInterval = StaticPrefs::network_captive_portal_service_maxInterval();
  mBackoffFactor = StaticPrefs::network_captive_portal_service_backoffFactor();

  LOG(("CaptivePortalService::Start min:%u max:%u backoff:%.2f\n", mMinInterval,
       mMaxInterval, mBackoffFactor));

  mSlackCount = 0;
  mDelay = mMinInterval;

  // When Start is called, perform a check immediately
  PerformCheck();
  RearmTimer();
  return NS_OK;
}

nsresult CaptivePortalService::Stop() {
  LOG(("CaptivePortalService::Stop\n"));

  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    // Doesn't do anything when called in the content process.
    return NS_OK;
  }

  if (!mStarted) {
    return NS_OK;
  }

  if (mTimer) {
    mTimer->Cancel();
  }
  mTimer = nullptr;
  mRequestInProgress = false;
  mStarted = false;
  mEverBeenCaptive = false;
  if (mCaptivePortalDetector) {
    mCaptivePortalDetector->Abort(kInterfaceName);
  }
  mCaptivePortalDetector = nullptr;

  // Clear the state in case anyone queries the state while detection is off.
  mState = UNKNOWN;
  return NS_OK;
}

void CaptivePortalService::SetStateInChild(int32_t aState) {
  // This should only be called in the content process, from ContentChild.cpp
  // in order to mirror the captive portal state set in the chrome process.
  MOZ_ASSERT(XRE_GetProcessType() != GeckoProcessType_Default);

  mState = aState;
  mLastChecked = TimeStamp::Now();
}

//-----------------------------------------------------------------------------
// CaptivePortalService::nsICaptivePortalService
//-----------------------------------------------------------------------------

NS_IMETHODIMP
CaptivePortalService::GetState(int32_t* aState) {
  *aState = mState;
  return NS_OK;
}

NS_IMETHODIMP
CaptivePortalService::RecheckCaptivePortal() {
  LOG(("CaptivePortalService::RecheckCaptivePortal\n"));

  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    // Doesn't do anything if called in the content process.
    return NS_OK;
  }

  if (!IsActive()) {
    return NS_OK;
  }

  // This is called for user activity. We need to reset the slack count,
  // so the checks continue to be quite frequent.
  mSlackCount = 0;
  mDelay = mMinInterval;

  PerformCheck();
  RearmTimer();
  return NS_OK;
}

NS_IMETHODIMP
CaptivePortalService::GetLastChecked(uint64_t* aLastChecked) {
  double duration = (TimeStamp::Now() - mLastChecked).ToMilliseconds();
  *aLastChecked = static_cast<uint64_t>(duration);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// CaptivePortalService::nsITimer
// This callback gets called every mDelay miliseconds
// It issues a checkCaptivePortal operation if one isn't already in progress
//-----------------------------------------------------------------------------
NS_IMETHODIMP
CaptivePortalService::Notify(nsITimer* aTimer) {
  LOG(("CaptivePortalService::Notify\n"));
  MOZ_ASSERT(aTimer == mTimer);
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  if (!IsActive()) {
    return NS_OK;
  }

  PerformCheck();

  // This is needed because we don't want to always make requests very often.
  // Every 10 checks, we the delay is increased mBackoffFactor times
  // to a maximum delay of mMaxInterval
  mSlackCount++;
  if (mSlackCount % 10 == 0) {
    mDelay = mDelay * mBackoffFactor;
  }
  if (mDelay > mMaxInterval) {
    mDelay = mMaxInterval;
  }

  // Note - if mDelay is 0, the timer will not be rearmed.
  RearmTimer();

  return NS_OK;
}

//-----------------------------------------------------------------------------
// CaptivePortalService::nsINamed
//-----------------------------------------------------------------------------

NS_IMETHODIMP
CaptivePortalService::GetName(nsACString& aName) {
  aName.AssignLiteral("CaptivePortalService");
  return NS_OK;
}

//-----------------------------------------------------------------------------
// CaptivePortalService::nsIObserver
//-----------------------------------------------------------------------------
NS_IMETHODIMP
CaptivePortalService::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData) {
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    // Doesn't do anything if called in the content process.
    return NS_OK;
  }

  LOG(("CaptivePortalService::Observe() topic=%s\n", aTopic));
  if (!strcmp(aTopic, kOpenCaptivePortalLoginEvent)) {
    // A redirect or altered content has been detected.
    // The user needs to log in. We are in a captive portal.
    StateTransition(LOCKED_PORTAL);
    mLastChecked = TimeStamp::Now();
    mEverBeenCaptive = true;
  } else if (!strcmp(aTopic, kCaptivePortalLoginSuccessEvent)) {
    // The user has successfully logged in. We have connectivity.
    StateTransition(UNLOCKED_PORTAL);
    mLastChecked = TimeStamp::Now();
    mSlackCount = 0;
    mDelay = mMinInterval;

    RearmTimer();
  } else if (!strcmp(aTopic, kAbortCaptivePortalLoginEvent)) {
    // The login has been aborted
    StateTransition(UNKNOWN);
    mLastChecked = TimeStamp::Now();
    mSlackCount = 0;
  } else if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Stop();
    return NS_OK;
  }

  // Send notification so that the captive portal state is mirrored in the
  // content process.
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    nsCOMPtr<nsICaptivePortalService> cps(this);
    observerService->NotifyObservers(cps, NS_IPC_CAPTIVE_PORTAL_SET_STATE,
                                     nullptr);
  }

  return NS_OK;
}

void CaptivePortalService::NotifyConnectivityAvailable(bool aCaptive) {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    nsCOMPtr<nsICaptivePortalService> cps(this);
    observerService->NotifyObservers(cps, NS_CAPTIVE_PORTAL_CONNECTIVITY,
                                     aCaptive ? u"captive" : u"clear");
  }
}

//-----------------------------------------------------------------------------
// CaptivePortalService::nsICaptivePortalCallback
//-----------------------------------------------------------------------------
NS_IMETHODIMP
CaptivePortalService::Prepare() {
  LOG(("CaptivePortalService::Prepare\n"));
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed) ||
      !IsActive()) {
    return NS_OK;
  }
  // XXX: Finish preparation shouldn't be called until dns and routing is
  // available.
  if (mCaptivePortalDetector) {
    mCaptivePortalDetector->FinishPreparation(kInterfaceName);
  }
  return NS_OK;
}

NS_IMETHODIMP
CaptivePortalService::Complete(bool success) {
  LOG(("CaptivePortalService::Complete(success=%d) mState=%d\n", success,
       mState));
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);
  if (!IsActive()) {
    return NS_OK;
  }
  mLastChecked = TimeStamp::Now();

  // Note: this callback gets called when:
  // 1. the request is completed, and content is valid (success == true)
  // 2. when the request is aborted or times out (success == false)

  if (success) {
    if (mEverBeenCaptive) {
      StateTransition(UNLOCKED_PORTAL);
      NotifyConnectivityAvailable(true);
    } else {
      StateTransition(NOT_CAPTIVE);
      NotifyConnectivityAvailable(false);
    }
  }

  mRequestInProgress = false;
  return NS_OK;
}

void CaptivePortalService::StateTransition(int32_t aNewState) {
  int32_t oldState = mState;
  mState = aNewState;

  if ((oldState == UNKNOWN && mState == NOT_CAPTIVE) ||
      (oldState == LOCKED_PORTAL && mState == UNLOCKED_PORTAL)) {
    nsCOMPtr<nsIObserverService> observerService =
        services::GetObserverService();
    if (observerService) {
      nsCOMPtr<nsICaptivePortalService> cps(this);
      observerService->NotifyObservers(
          cps, NS_CAPTIVE_PORTAL_CONNECTIVITY_CHANGED, nullptr);
    }
  }
}

}  // namespace net
}  // namespace mozilla
