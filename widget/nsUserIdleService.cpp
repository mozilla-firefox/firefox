/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsUserIdleService.h"

#include <algorithm>

#include "mozilla/AppShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/glean/WidgetMetrics.h"
#include "nsCOMArray.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIAsyncShutdown.h"
#include "nsIObserverService.h"
#include "nsString.h"
#include "nsXULAppAPI.h"
#include "prinrval.h"
#include "prtime.h"

#ifdef MOZ_WIDGET_ANDROID
#  include <android/log.h>
#endif

using namespace mozilla;

// After the twenty four hour period expires for an idle daily, this is the
// amount of idle time we wait for before actually firing the idle-daily
// event.
#define DAILY_SIGNIFICANT_IDLE_SERVICE_SEC (3 * 60)

// In cases where it's been longer than twenty four hours since the last
// idle-daily, this is the shortend amount of idle time we wait for before
// firing the idle-daily event.
#define DAILY_SHORTENED_IDLE_SERVICE_SEC 60

// Pref for last time (seconds since epoch) daily notification was sent.
#define PREF_LAST_DAILY "idle.lastDailyNotification"

// Number of seconds in a day.
#define SECONDS_PER_DAY 86400

static LazyLogModule sLog("idleService");

#define LOG_TAG "GeckoIdleService"
#define LOG_LEVEL ANDROID_LOG_DEBUG

// Use this to find previously added observers in our array:
class IdleListenerComparator {
 public:
  bool Equals(IdleListener a, IdleListener b) const {
    return (a.observer == b.observer) && (a.reqIdleTime == b.reqIdleTime);
  }
};

////////////////////////////////////////////////////////////////////////////////
//// nsUserIdleServiceDaily

NS_IMPL_ISUPPORTS(nsUserIdleServiceDaily, nsIObserver, nsISupportsWeakReference)

NS_IMETHODIMP
nsUserIdleServiceDaily::Observe(nsISupports*, const char* aTopic,
                                const char16_t*) {
  auto shutdownInProgress =
      AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed);
  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: Observe '%s' (%d)", aTopic,
           shutdownInProgress));

  if (shutdownInProgress || strcmp(aTopic, OBSERVER_TOPIC_ACTIVE) == 0) {
    return NS_OK;
  }
  MOZ_ASSERT(strcmp(aTopic, OBSERVER_TOPIC_IDLE) == 0);

  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: Notifying idle-daily observers"));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG, "Notifying idle-daily observers");
#endif

  // Send the idle-daily observer event
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  NS_ENSURE_STATE(observerService);
  (void)observerService->NotifyObservers(nullptr, OBSERVER_TOPIC_IDLE_DAILY,
                                         nullptr);

  // Notify the category observers.
  nsCOMArray<nsIObserver> entries;
  mCategoryObservers.GetEntries(entries);
  for (int32_t i = 0; i < entries.Count(); ++i) {
    (void)entries[i]->Observe(nullptr, OBSERVER_TOPIC_IDLE_DAILY, nullptr);
  }

  // Stop observing idle for today.
  (void)mIdleService->RemoveIdleObserver(this, mIdleDailyTriggerWait);

  // Set the last idle-daily time pref.
  int32_t nowSec = static_cast<int32_t>(PR_Now() / PR_USEC_PER_SEC);
  Preferences::SetInt(PREF_LAST_DAILY, nowSec);

  // Force that to be stored so we don't retrigger twice a day under
  // any circumstances.
  nsIPrefService* prefs = Preferences::GetService();
  if (prefs) {
    prefs->SavePrefFile(nullptr);
  }

  MOZ_LOG(
      sLog, LogLevel::Debug,
      ("nsUserIdleServiceDaily: Storing last idle time as %d sec.", nowSec));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG, "Storing last idle time as %d",
                      nowSec);
#endif

  // Note the moment we expect to get the next timer callback
  mExpectedTriggerTime =
      PR_Now() + ((PRTime)SECONDS_PER_DAY * (PRTime)PR_USEC_PER_SEC);

  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: Restarting daily timer"));

  // Start timer for the next check in one day.
  (void)mTimer->InitWithNamedFuncCallback(
      DailyCallback, this, SECONDS_PER_DAY * PR_MSEC_PER_SEC,
      nsITimer::TYPE_ONE_SHOT, "nsUserIdleServiceDaily::Observe"_ns);

  return NS_OK;
}

nsUserIdleServiceDaily::nsUserIdleServiceDaily(nsIUserIdleService* aIdleService)
    : mIdleService(aIdleService),
      mTimer(NS_NewTimer()),
      mCategoryObservers(OBSERVER_TOPIC_IDLE_DAILY),
      mExpectedTriggerTime(0),
      mIdleDailyTriggerWait(DAILY_SIGNIFICANT_IDLE_SERVICE_SEC) {}

void nsUserIdleServiceDaily::Init() {
  // First check the time of the last idle-daily event notification. If it
  // has been 24 hours or higher, or if we have never sent an idle-daily,
  // get ready to send an idle-daily event. Otherwise set a timer targeted
  // at 24 hours past the last idle-daily we sent.

  int32_t lastDaily = Preferences::GetInt(PREF_LAST_DAILY, 0);
  // Setting the pref to -1 allows to disable idle-daily, and it's particularly
  // useful in tests. Normally there should be no need for the user to set
  // this value.
  if (lastDaily == -1) {
    MOZ_LOG(sLog, LogLevel::Debug,
            ("nsUserIdleServiceDaily: Init: disabled idle-daily"));
    return;
  }

  int32_t nowSec = static_cast<int32_t>(PR_Now() / PR_USEC_PER_SEC);
  if (lastDaily < 0 || lastDaily > nowSec) {
    // The time is bogus, use default.
    lastDaily = 0;
  }
  int32_t secondsSinceLastDaily = nowSec - lastDaily;

  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: Init: seconds since last daily: %d",
           secondsSinceLastDaily));

  // If it has been twenty four hours or more or if we have never sent an
  // idle-daily event get ready to send it during the next idle period.
  if (secondsSinceLastDaily > SECONDS_PER_DAY) {
    // Check for a "long wait", e.g. 48-hours or more.
    bool hasBeenLongWait =
        (lastDaily && (secondsSinceLastDaily > (SECONDS_PER_DAY * 2)));

    MOZ_LOG(
        sLog, LogLevel::Debug,
        ("nsUserIdleServiceDaily: has been long wait? %d", hasBeenLongWait));

    // StageIdleDaily sets up a wait for the user to become idle and then
    // sends the idle-daily event.
    StageIdleDaily(hasBeenLongWait);
  } else {
    MOZ_LOG(sLog, LogLevel::Debug,
            ("nsUserIdleServiceDaily: Setting timer a day from now"));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG, "Setting timer a day from now");
#endif

    // According to our last idle-daily pref, the last idle-daily was fired
    // less then 24 hours ago. Set a wait for the amount of time remaining.
    int32_t milliSecLeftUntilDaily =
        (SECONDS_PER_DAY - secondsSinceLastDaily) * PR_MSEC_PER_SEC;

    MOZ_LOG(sLog, LogLevel::Debug,
            ("nsUserIdleServiceDaily: Seconds till next timeout: %d",
             (SECONDS_PER_DAY - secondsSinceLastDaily)));

    // Mark the time at which we expect this to fire. On systems with faulty
    // timers, we need to be able to cross check that the timer fired at the
    // expected time.
    mExpectedTriggerTime =
        PR_Now() + (milliSecLeftUntilDaily * PR_USEC_PER_MSEC);

    (void)mTimer->InitWithNamedFuncCallback(
        DailyCallback, this, milliSecLeftUntilDaily, nsITimer::TYPE_ONE_SHOT,
        "nsUserIdleServiceDaily::Init"_ns);
  }
}

nsUserIdleServiceDaily::~nsUserIdleServiceDaily() {
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void nsUserIdleServiceDaily::StageIdleDaily(bool aHasBeenLongWait) {
  NS_ASSERTION(mIdleService, "No idle service available?");
  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: Registering Idle observer callback "
           "(short wait requested? %d)",
           aHasBeenLongWait));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG, "Registering Idle observer callback");
#endif
  mIdleDailyTriggerWait =
      (aHasBeenLongWait ? DAILY_SHORTENED_IDLE_SERVICE_SEC
                        : DAILY_SIGNIFICANT_IDLE_SERVICE_SEC);
  (void)mIdleService->AddIdleObserver(this, mIdleDailyTriggerWait);
}

// static
void nsUserIdleServiceDaily::DailyCallback(nsITimer* aTimer, void* aClosure) {
  MOZ_LOG(sLog, LogLevel::Debug,
          ("nsUserIdleServiceDaily: DailyCallback running"));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG, "DailyCallback running");
#endif

  nsUserIdleServiceDaily* self = static_cast<nsUserIdleServiceDaily*>(aClosure);

  // Check to be sure the timer didn't fire early. This currently only
  // happens on android.
  PRTime now = PR_Now();
  if (self->mExpectedTriggerTime && now < self->mExpectedTriggerTime) {
    // Timer returned early, reschedule to the appropriate time.
    PRTime delayTime = self->mExpectedTriggerTime - now;

    // Add 10 ms to ensure we don't undershoot, and never get a "0" timer.
    delayTime += 10 * PR_USEC_PER_MSEC;

    MOZ_LOG(sLog, LogLevel::Debug,
            ("nsUserIdleServiceDaily: DailyCallback resetting timer to %" PRId64
             " msec",
             delayTime / PR_USEC_PER_MSEC));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "DailyCallback resetting timer to %" PRId64 " msec",
                        delayTime / PR_USEC_PER_MSEC);
#endif

    (void)self->mTimer->InitWithNamedFuncCallback(
        DailyCallback, self, delayTime / PR_USEC_PER_MSEC,
        nsITimer::TYPE_ONE_SHOT, "nsUserIdleServiceDaily::DailyCallback"_ns);
    return;
  }

  // Register for a short term wait for idle event. When this fires we fire
  // our idle-daily event.
  self->StageIdleDaily(false);
}

/**
 * The idle services goal is to notify subscribers when a certain time has
 * passed since the last user interaction with the system.
 *
 * On some platforms this is defined as the last time user events reached this
 * application, on other platforms it is a system wide thing - the preferred
 * implementation is to use the system idle time, rather than the application
 * idle time, as the things depending on the idle service are likely to use
 * significant resources (network, disk, memory, cpu, etc.).
 *
 * When the idle service needs to use the system wide idle timer, it typically
 * needs to poll the idle time value by the means of a timer.  It needs to
 * poll fast when it is in active idle mode (when it has a listener in the idle
 * mode) as it needs to detect if the user is active in other applications.
 *
 * When the service is waiting for the first listener to become idle, or when
 * it is only monitoring application idle time, it only needs to have the timer
 * expire at the time the next listener goes idle.
 *
 * The core state of the service is determined by:
 *
 * - A list of listeners.
 *
 * - A boolean that tells if any listeners are in idle mode.
 *
 * - A delta value that indicates when, measured from the last non-idle time,
 *   the next listener should switch to idle mode.
 *
 * - An absolute time of the last time idle mode was detected (this is used to
 *   judge if we have been out of idle mode since the last invocation of the
 *   service.
 *
 * There are four entry points into the system:
 *
 * - A new listener is registered.
 *
 * - An existing listener is deregistered.
 *
 * - User interaction is detected.
 *
 * - The timer expires.
 *
 * When a new listener is added its idle timeout, is compared with the next idle
 * timeout, and if lower, that time is stored as the new timeout, and the timer
 * is reconfigured to ensure a timeout around the time the new listener should
 * timeout.
 *
 * If the next idle time is above the idle time requested by the new listener
 * it won't be informed until the timer expires, this is to avoid recursive
 * behavior and to simplify the code.  In this case the timer will be set to
 * about 10 ms.
 *
 * When an existing listener is deregistered, it is just removed from the list
 * of active listeners, we don't stop the timer, we just let it expire.
 *
 * When user interaction is detected, either because it was directly detected or
 * because we polled the system timer and found it to be unexpected low, then we
 * check the flag that tells us if any listeners are in idle mode, if there are
 * they are removed from idle mode and told so, and we reset our state
 * caculating the next timeout and restart the timer if needed.
 *
 * ---- Build in logic
 *
 * In order to avoid restarting the timer endlessly, the timer function has
 * logic that will only restart the timer, if the requested timeout is before
 * the current timeout.
 *
 */

////////////////////////////////////////////////////////////////////////////////
//// nsUserIdleService

namespace {
nsUserIdleService* gIdleService;
}  // namespace

already_AddRefed<nsUserIdleService> nsUserIdleService::GetInstance() {
  RefPtr<nsUserIdleService> instance(gIdleService);
  return instance.forget();
}

class UserIdleBlocker final : public nsIAsyncShutdownBlocker {
  ~UserIdleBlocker() = default;

 public:
  explicit UserIdleBlocker() = default;

  NS_IMETHOD
  GetName(nsAString& aNameOut) override {
    aNameOut = nsLiteralString(u"UserIdleBlocker");
    return NS_OK;
  }

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aClient) override {
    if (gIdleService) {
      gIdleService->SetDisabledForShutdown();
    }
    aClient->RemoveBlocker(this);
    return NS_OK;
  }

  NS_IMETHOD
  GetState(nsIPropertyBag**) override { return NS_OK; }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(UserIdleBlocker, nsIAsyncShutdownBlocker)

nsUserIdleService::nsUserIdleService()
    : mIdleObserverCount(0),
      mDeltaToNextIdleSwitchInS(UINT32_MAX),
      mLastUserInteraction(TimeStamp::Now()) {
  MOZ_ASSERT(!gIdleService);
  gIdleService = this;
  if (XRE_IsParentProcess()) {
    mDailyIdle = new nsUserIdleServiceDaily(this);
    mDailyIdle->Init();
  }
  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  MOZ_ASSERT(svc);
  nsCOMPtr<nsIAsyncShutdownClient> client;
  auto rv = svc->GetAppShutdownConfirmed(getter_AddRefs(client));
  if (NS_FAILED(rv)) {
    // appShutdownConfirmed can be undefined in some environments.
    rv = svc->GetXpcomWillShutdown(getter_AddRefs(client));
  }
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  client->AddBlocker(new UserIdleBlocker(),
                     NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__,
                     u""_ns);
}

nsUserIdleService::~nsUserIdleService() {
  if (mTimer) {
    mTimer->Cancel();
  }

  MOZ_ASSERT(gIdleService == this);
  gIdleService = nullptr;
}

NS_IMPL_ISUPPORTS(nsUserIdleService, nsIUserIdleService,
                  nsIUserIdleServiceInternal)

void nsUserIdleService::SetDisabledForShutdown() {
  SetDisabled(true);
  mShutdown = true;
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
  mArrayListeners.Clear();
  mIdleObserverCount = 0;
}

NS_IMETHODIMP
nsUserIdleService::AddIdleObserver(nsIObserver* aObserver,
                                   uint32_t aIdleTimeInS) {
  NS_ENSURE_ARG_POINTER(aObserver);
  // We don't accept idle time at 0, and we can't handle idle time that are too
  // high either - no more than ~136 years.
  NS_ENSURE_ARG_RANGE(aIdleTimeInS, 1, (UINT32_MAX / 10) - 1);

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString timeCStr;
    timeCStr.AppendInt(aIdleTimeInS);
    PROFILER_MARKER_TEXT("UserIdle::AddObserver", OTHER, MarkerStack::Capture(),
                         timeCStr);
  }

  if (XRE_IsContentProcess()) {
    dom::ContentChild* cpc = dom::ContentChild::GetSingleton();
    cpc->AddIdleObserver(aObserver, aIdleTimeInS);
    return NS_OK;
  }

  if (mShutdown) {
    return NS_OK;
  }

  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: Register idle observer %p for %d seconds", aObserver,
           aIdleTimeInS));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG,
                      "Register idle observer %p for %d seconds", aObserver,
                      aIdleTimeInS);
#endif

  // Put the time + observer in a struct we can keep:
  IdleListener listener(aObserver, aIdleTimeInS);

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  mArrayListeners.AppendElement(listener);

  // Create our timer callback if it's not there already.
  if (!mTimer) {
    mTimer = NS_NewTimer();
    NS_ENSURE_TRUE(mTimer, NS_ERROR_OUT_OF_MEMORY);
  }

  // Check if the newly added observer has a smaller wait time than what we
  // are waiting for now.
  if (mDeltaToNextIdleSwitchInS > aIdleTimeInS) {
    // If it is, then this is the next to move to idle (at this point we
    // don't care if it should have switched already).
    MOZ_LOG(
        sLog, LogLevel::Debug,
        ("idleService: Register: adjusting next switch from %d to %d seconds",
         mDeltaToNextIdleSwitchInS, aIdleTimeInS));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "Register: adjusting next switch from %d to %d seconds",
                        mDeltaToNextIdleSwitchInS, aIdleTimeInS);
#endif

    mDeltaToNextIdleSwitchInS = aIdleTimeInS;
    ReconfigureTimer();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsUserIdleService::RemoveIdleObserver(nsIObserver* aObserver,
                                      uint32_t aTimeInS) {
  NS_ENSURE_ARG_POINTER(aObserver);
  NS_ENSURE_ARG(aTimeInS);

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString timeCStr;
    timeCStr.AppendInt(aTimeInS);
    PROFILER_MARKER_TEXT("UserIdle::RemoveObserver", OTHER,
                         MarkerStack::Capture(), timeCStr);
  }

  if (XRE_IsContentProcess()) {
    dom::ContentChild* cpc = dom::ContentChild::GetSingleton();
    cpc->RemoveIdleObserver(aObserver, aTimeInS);
    return NS_OK;
  }

  if (mShutdown) {
    return NS_OK;
  }

  IdleListener listener(aObserver, aTimeInS);

  // Find the entry and remove it, if it was the last entry, we just let the
  // existing timer run to completion (there might be a new registration in a
  // little while.
  IdleListenerComparator c;
  nsTArray<IdleListener>::index_type listenerIndex =
      mArrayListeners.IndexOf(listener, 0, c);
  if (listenerIndex != mArrayListeners.NoIndex) {
    if (mArrayListeners.ElementAt(listenerIndex).isIdle) mIdleObserverCount--;
    mArrayListeners.RemoveElementAt(listenerIndex);
    MOZ_LOG(sLog, LogLevel::Debug,
            ("idleService: Remove observer %p (%d seconds), %d remain idle",
             aObserver, aTimeInS, mIdleObserverCount));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "Remove observer %p (%d seconds), %d remain idle",
                        aObserver, aTimeInS, mIdleObserverCount);
#endif
    return NS_OK;
  }

  // If we get here, we haven't removed anything:
  MOZ_LOG(sLog, LogLevel::Warning,
          ("idleService: Failed to remove idle observer %p (%d seconds)",
           aObserver, aTimeInS));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG,
                      "Failed to remove idle observer %p (%d seconds)",
                      aObserver, aTimeInS);
#endif
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsUserIdleService::ResetIdleTimeOut(uint32_t idleDeltaInMS) {
  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: Reset idle timeout (last interaction %u msec)",
           idleDeltaInMS));

  if (mShutdown) {
    return NS_OK;
  }

  // Store the time
  mLastUserInteraction =
      TimeStamp::Now() - TimeDuration::FromMilliseconds(idleDeltaInMS);

  // If no one is idle, then we are done, any existing timers can keep running.
  if (mIdleObserverCount == 0) {
    MOZ_LOG(sLog, LogLevel::Debug,
            ("idleService: Reset idle timeout: no idle observers"));
    return NS_OK;
  }

  // Mark all idle services as non-idle, and calculate the next idle timeout.
  nsCOMArray<nsIObserver> notifyList;
  mDeltaToNextIdleSwitchInS = UINT32_MAX;

  // Loop through all listeners, and find any that have detected idle.
  for (uint32_t i = 0; i < mArrayListeners.Length(); i++) {
    IdleListener& curListener = mArrayListeners.ElementAt(i);

    // If the listener was idle, then he shouldn't be any longer.
    if (curListener.isIdle) {
      notifyList.AppendObject(curListener.observer);
      curListener.isIdle = false;
    }

    // Check if the listener is the next one to timeout.
    mDeltaToNextIdleSwitchInS =
        std::min(mDeltaToNextIdleSwitchInS, curListener.reqIdleTime);
  }

  // When we are done, then we wont have anyone idle.
  mIdleObserverCount = 0;

  // Restart the idle timer, and do so before anyone can delay us.
  ReconfigureTimer();

  int32_t numberOfPendingNotifications = notifyList.Count();

  // Bail if nothing to do.
  if (!numberOfPendingNotifications) {
    return NS_OK;
  }

  // Now send "active" events to all, if any should have timed out already,
  // then they will be reawaken by the timer that is already running.

  // We need a text string to send with any state change events.
  nsAutoString timeStr;

  timeStr.AppendInt((int32_t)(idleDeltaInMS / PR_MSEC_PER_SEC));

  // Send the "non-idle" events.
  while (numberOfPendingNotifications--) {
    MOZ_LOG(sLog, LogLevel::Debug,
            ("idleService: Reset idle timeout: tell observer %p user is back",
             notifyList[numberOfPendingNotifications]));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "Reset idle timeout: tell observer %p user is back",
                        notifyList[numberOfPendingNotifications]);
#endif
    notifyList[numberOfPendingNotifications]->Observe(
        this, OBSERVER_TOPIC_ACTIVE, timeStr.get());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsUserIdleService::GetIdleTime(uint32_t* idleTime) {
  // Check sanity of in parameter.
  if (!idleTime) {
    return NS_ERROR_NULL_POINTER;
  }

  // Polled idle time in ms.
  uint32_t polledIdleTimeMS;

  bool polledIdleTimeIsValid = PollIdleTime(&polledIdleTimeMS);

  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: Get idle time: polled %u msec, valid = %d",
           polledIdleTimeMS, polledIdleTimeIsValid));

  // timeSinceReset is in milliseconds.
  TimeDuration timeSinceReset = TimeStamp::Now() - mLastUserInteraction;
  uint32_t timeSinceResetInMS = timeSinceReset.ToMilliseconds();

  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: Get idle time: time since reset %u msec",
           timeSinceResetInMS));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG,
                      "Get idle time: time since reset %u msec",
                      timeSinceResetInMS);
#endif

  // If we did't get pulled data, return the time since last idle reset.
  if (!polledIdleTimeIsValid) {
    // We need to convert to ms before returning the time.
    *idleTime = timeSinceResetInMS;
    return NS_OK;
  }

  // Otherwise return the shortest time detected (in ms).
  *idleTime = std::min(timeSinceResetInMS, polledIdleTimeMS);

  return NS_OK;
}

bool nsUserIdleService::PollIdleTime(uint32_t* /*aIdleTime*/) {
  // Default behavior is not to have the ability to poll an idle time.
  return false;
}

nsresult nsUserIdleService::GetDisabled(bool* aResult) {
  *aResult = mDisabled;
  return NS_OK;
}

nsresult nsUserIdleService::SetDisabled(bool aDisabled) {
  mDisabled = aDisabled;
  return NS_OK;
}

void nsUserIdleService::StaticIdleTimerCallback(nsITimer* aTimer,
                                                void* aClosure) {
  static_cast<nsUserIdleService*>(aClosure)->IdleTimerCallback();
}

void nsUserIdleService::IdleTimerCallback(void) {
  // Remember that we no longer have a timer running.
  mCurrentlySetToTimeoutAt = TimeStamp();

  // Find the last detected idle time.
  uint32_t lastIdleTimeInMS = static_cast<uint32_t>(
      (TimeStamp::Now() - mLastUserInteraction).ToMilliseconds());
  // Get the current idle time.
  uint32_t currentIdleTimeInMS;

  if (NS_FAILED(GetIdleTime(&currentIdleTimeInMS))) {
    MOZ_LOG(sLog, LogLevel::Info,
            ("idleService: Idle timer callback: failed to get idle time"));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "Idle timer callback: failed to get idle time");
#endif
    return;
  }

  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: Idle timer callback: current idle time %u msec",
           currentIdleTimeInMS));
#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG,
                      "Idle timer callback: current idle time %u msec",
                      currentIdleTimeInMS);
#endif

  // Check if we have had some user interaction we didn't handle previously
  // we do the calculation in ms to lessen the chance for rounding errors to
  // trigger wrong results.
  if (lastIdleTimeInMS > currentIdleTimeInMS) {
    // We had user activity, so handle that part first (to ensure the listeners
    // don't risk getting an non-idle after they get a new idle indication.
    ResetIdleTimeOut(currentIdleTimeInMS);

    // NOTE: We can't bail here, as we might have something already timed out.
  }

  // Find the idle time in S.
  uint32_t currentIdleTimeInS = currentIdleTimeInMS / PR_MSEC_PER_SEC;

  // Restart timer and bail if no-one are expected to be in idle
  if (mDeltaToNextIdleSwitchInS > currentIdleTimeInS) {
    // If we didn't expect anyone to be idle, then just re-start the timer.
    ReconfigureTimer();
    return;
  }

  if (mDisabled) {
    MOZ_LOG(sLog, LogLevel::Info,
            ("idleService: Skipping idle callback while disabled"));

    ReconfigureTimer();
    return;
  }

  // Tell expired listeners they are expired,and find the next timeout
  auto timer = glean::widget::notify_idle.Measure();

  // We need to initialise the time to the next idle switch.
  mDeltaToNextIdleSwitchInS = UINT32_MAX;

  // Create list of observers that should be notified.
  nsCOMArray<nsIObserver> notifyList;

  for (uint32_t i = 0; i < mArrayListeners.Length(); i++) {
    IdleListener& curListener = mArrayListeners.ElementAt(i);

    // We are only interested in items, that are not in the idle state.
    if (!curListener.isIdle) {
      // If they have an idle time smaller than the actual idle time.
      if (curListener.reqIdleTime <= currentIdleTimeInS) {
        // Then add the listener to the list of listeners that should be
        // notified.
        notifyList.AppendObject(curListener.observer);
        // This listener is now idle.
        curListener.isIdle = true;
        // Remember we have someone idle.
        mIdleObserverCount++;
      } else {
        // Listeners that are not timed out yet are candidates for timing out.
        mDeltaToNextIdleSwitchInS =
            std::min(mDeltaToNextIdleSwitchInS, curListener.reqIdleTime);
      }
    }
  }

  // Restart the timer before any notifications that could slow us down are
  // done.
  ReconfigureTimer();

  int32_t numberOfPendingNotifications = notifyList.Count();

  // Bail if nothing to do.
  if (!numberOfPendingNotifications) {
    MOZ_LOG(
        sLog, LogLevel::Debug,
        ("idleService: **** Idle timer callback: no observers to message."));
    return;
  }

  // We need a text string to send with any state change events.
  nsAutoString timeStr;
  timeStr.AppendInt(currentIdleTimeInS);

  // Notify all listeners that just timed out.
  while (numberOfPendingNotifications--) {
    MOZ_LOG(
        sLog, LogLevel::Debug,
        ("idleService: **** Idle timer callback: tell observer %p user is idle",
         notifyList[numberOfPendingNotifications]));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "Idle timer callback: tell observer %p user is idle",
                        notifyList[numberOfPendingNotifications]);
#endif
    nsAutoCString timeCStr;
    timeCStr.AppendInt(currentIdleTimeInS);
    AUTO_PROFILER_MARKER_TEXT("UserIdle::IdleCallback", OTHER, {}, timeCStr);
    notifyList[numberOfPendingNotifications]->Observe(this, OBSERVER_TOPIC_IDLE,
                                                      timeStr.get());
  }
}

void nsUserIdleService::SetTimerExpiryIfBefore(TimeStamp aNextTimeout) {
  TimeDuration nextTimeoutDuration = aNextTimeout - TimeStamp::Now();

  MOZ_LOG(
      sLog, LogLevel::Debug,
      ("idleService: SetTimerExpiryIfBefore: next timeout %0.f msec from now",
       nextTimeoutDuration.ToMilliseconds()));

#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG,
                      "SetTimerExpiryIfBefore: next timeout %0.f msec from now",
                      nextTimeoutDuration.ToMilliseconds());
#endif

  // Bail if we don't have a timer service.
  if (!mTimer) {
    return;
  }

  // If the new timeout is before the old one or we don't have a timer running,
  // then restart the timer.
  if (mCurrentlySetToTimeoutAt.IsNull() ||
      mCurrentlySetToTimeoutAt > aNextTimeout) {
    mCurrentlySetToTimeoutAt = aNextTimeout;

    // Stop the current timer (it's ok to try'n stop it, even it isn't running).
    mTimer->Cancel();

    // Check that the timeout is actually in the future, otherwise make it so.
    TimeStamp currentTime = TimeStamp::Now();
    if (currentTime > mCurrentlySetToTimeoutAt) {
      mCurrentlySetToTimeoutAt = currentTime;
    }

    // Add 10 ms to ensure we don't undershoot, and never get a "0" timer.
    mCurrentlySetToTimeoutAt += TimeDuration::FromMilliseconds(10);

    TimeDuration deltaTime = mCurrentlySetToTimeoutAt - currentTime;
    MOZ_LOG(
        sLog, LogLevel::Debug,
        ("idleService: IdleService reset timer expiry to %0.f msec from now",
         deltaTime.ToMilliseconds()));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "reset timer expiry to %0.f msec from now",
                        deltaTime.ToMilliseconds());
#endif

    // Start the timer
    mTimer->InitWithNamedFuncCallback(
        StaticIdleTimerCallback, this, deltaTime.ToMilliseconds(),
        nsITimer::TYPE_ONE_SHOT,
        "nsUserIdleService::SetTimerExpiryIfBefore"_ns);
  }
}

void nsUserIdleService::ReconfigureTimer(void) {
  // Check if either someone is idle, or someone will become idle.
  if ((mIdleObserverCount == 0) && UINT32_MAX == mDeltaToNextIdleSwitchInS) {
    // If not, just let any existing timers run to completion
    // And bail out.
    MOZ_LOG(sLog, LogLevel::Debug,
            ("idleService: ReconfigureTimer: no idle or waiting observers"));
#ifdef MOZ_WIDGET_ANDROID
    __android_log_print(LOG_LEVEL, LOG_TAG,
                        "ReconfigureTimer: no idle or waiting observers");
#endif
    return;
  }

  // Find the next timeout value, assuming we are not polling.

  // We need to store the current time, so we don't get artifacts from the time
  // ticking while we are processing.
  TimeStamp curTime = TimeStamp::Now();

  TimeStamp nextTimeoutAt =
      mLastUserInteraction +
      TimeDuration::FromSeconds(mDeltaToNextIdleSwitchInS);

  TimeDuration nextTimeoutDuration = nextTimeoutAt - curTime;

  MOZ_LOG(sLog, LogLevel::Debug,
          ("idleService: next timeout %0.f msec from now",
           nextTimeoutDuration.ToMilliseconds()));

#ifdef MOZ_WIDGET_ANDROID
  __android_log_print(LOG_LEVEL, LOG_TAG, "next timeout %0.f msec from now",
                      nextTimeoutDuration.ToMilliseconds());
#endif

  SetTimerExpiryIfBefore(nextTimeoutAt);
}
