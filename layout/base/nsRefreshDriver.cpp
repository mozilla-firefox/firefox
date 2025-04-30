/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Code to notify things that animate before a refresh, at an appropriate
 * refresh rate.  (Perhaps temporary, until replaced by compositor.)
 *
 * Chrome and each tab have their own RefreshDriver, which in turn
 * hooks into one of a few global timer based on RefreshDriverTimer,
 * defined below.  There are two main global timers -- one for active
 * animations, and one for inactive ones.  These are implemented as
 * subclasses of RefreshDriverTimer; see below for a description of
 * their implementations.  In the future, additional timer types may
 * implement things like blocking on vsync.
 */

#include "nsRefreshDriver.h"
#include "mozilla/DataMutex.h"
#include "nsThreadUtils.h"

#ifdef XP_WIN
#  include <windows.h>
// mmsystem isn't part of WIN32_LEAN_AND_MEAN, so we have
// to manually include it
#  include <mmsystem.h>
#  include "WinUtils.h"
#endif

#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/MediaQueryList.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/Hal.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/PresShell.h"
#include "mozilla/VsyncTaskManager.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "imgRequest.h"
#include "nsComponentManagerUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/HTMLVideoElement.h"
#include "nsIXULRuntime.h"
#include "jsapi.h"
#include "nsContentUtils.h"
#include "nsTextFrame.h"
#include "mozilla/PendingFullscreenEvent.h"
#include "mozilla/dom/PerformanceMainThread.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_idle_period.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_page_load.h"
#include "nsViewManager.h"
#include "GeckoProfiler.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/CallbackDebuggerNotification.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/VsyncMainChild.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/TaskController.h"
#include "imgIContainer.h"
#include "mozilla/dom/ScriptSettings.h"
#include "nsDocShell.h"
#include "nsISimpleEnumerator.h"
#include "nsJSEnvironment.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/glean/LayoutMetrics.h"

#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "VsyncSource.h"
#include "mozilla/VsyncDispatcher.h"
#include "mozilla/Unused.h"
#include "nsAnimationManager.h"
#include "nsDisplayList.h"
#include "nsDOMNavigationTiming.h"
#include "nsTransitionManager.h"

#if defined(MOZ_WIDGET_ANDROID)
#  include "VRManagerChild.h"
#endif  // defined(MOZ_WIDGET_ANDROID)

#include "nsXULPopupManager.h"

#include <numeric>

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::ipc;
using namespace mozilla::dom;
using namespace mozilla::layout;

static mozilla::LazyLogModule sRefreshDriverLog("nsRefreshDriver");
#define LOG(...) \
  MOZ_LOG(sRefreshDriverLog, mozilla::LogLevel::Debug, (__VA_ARGS__))

// after 10 minutes, stop firing off inactive timers
#define DEFAULT_INACTIVE_TIMER_DISABLE_SECONDS 600

// The number of seconds spent skipping frames because we are waiting for the
// compositor before logging.
#if defined(MOZ_ASAN)
#  define REFRESH_WAIT_WARNING 5
#elif defined(DEBUG) && !defined(MOZ_VALGRIND)
#  define REFRESH_WAIT_WARNING 5
#elif defined(DEBUG) && defined(MOZ_VALGRIND)
#  define REFRESH_WAIT_WARNING (RUNNING_ON_VALGRIND ? 20 : 5)
#elif defined(MOZ_VALGRIND)
#  define REFRESH_WAIT_WARNING (RUNNING_ON_VALGRIND ? 10 : 1)
#else
#  define REFRESH_WAIT_WARNING 1
#endif

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsRefreshDriver::TickReasons);

namespace {
// The number outstanding nsRefreshDrivers (that have been created but not
// disconnected). When this reaches zero we will call
// nsRefreshDriver::Shutdown.
static uint32_t sRefreshDriverCount = 0;
}  // namespace

namespace mozilla {

static TimeStamp sMostRecentHighRateVsync;

static TimeDuration sMostRecentHighRate;

/*
 * The base class for all global refresh driver timers.  It takes care
 * of managing the list of refresh drivers attached to them and
 * provides interfaces for querying/setting the rate and actually
 * running a timer 'Tick'.  Subclasses must implement StartTimer(),
 * StopTimer(), and ScheduleNextTick() -- the first two just
 * start/stop whatever timer mechanism is in use, and ScheduleNextTick
 * is called at the start of the Tick() implementation to set a time
 * for the next tick.
 */
class RefreshDriverTimer {
 public:
  RefreshDriverTimer() = default;

  NS_INLINE_DECL_REFCOUNTING(RefreshDriverTimer)

  virtual void AddRefreshDriver(nsRefreshDriver* aDriver) {
    LOG("[%p] AddRefreshDriver %p", this, aDriver);

    bool startTimer =
        mContentRefreshDrivers.IsEmpty() && mRootRefreshDrivers.IsEmpty();
    if (IsRootRefreshDriver(aDriver)) {
      NS_ASSERTION(!mRootRefreshDrivers.Contains(aDriver),
                   "Adding a duplicate root refresh driver!");
      mRootRefreshDrivers.AppendElement(aDriver);
    } else {
      NS_ASSERTION(!mContentRefreshDrivers.Contains(aDriver),
                   "Adding a duplicate content refresh driver!");
      mContentRefreshDrivers.AppendElement(aDriver);
    }

    if (startTimer) {
      StartTimer();
    }
  }

  void RemoveRefreshDriver(nsRefreshDriver* aDriver) {
    LOG("[%p] RemoveRefreshDriver %p", this, aDriver);

    if (IsRootRefreshDriver(aDriver)) {
      NS_ASSERTION(mRootRefreshDrivers.Contains(aDriver),
                   "RemoveRefreshDriver for a refresh driver that's not in the "
                   "root refresh list!");
      mRootRefreshDrivers.RemoveElement(aDriver);
    } else {
      nsPresContext* pc = aDriver->GetPresContext();
      nsPresContext* rootContext = pc ? pc->GetRootPresContext() : nullptr;
      // During PresContext shutdown, we can't accurately detect
      // if a root refresh driver exists or not. Therefore, we have to
      // search and find out which list this driver exists in.
      if (!rootContext) {
        if (mRootRefreshDrivers.Contains(aDriver)) {
          mRootRefreshDrivers.RemoveElement(aDriver);
        } else {
          NS_ASSERTION(mContentRefreshDrivers.Contains(aDriver),
                       "RemoveRefreshDriver without a display root for a "
                       "driver that is not in the content refresh list");
          mContentRefreshDrivers.RemoveElement(aDriver);
        }
      } else {
        NS_ASSERTION(mContentRefreshDrivers.Contains(aDriver),
                     "RemoveRefreshDriver for a driver that is not in the "
                     "content refresh list");
        mContentRefreshDrivers.RemoveElement(aDriver);
      }
    }

    bool stopTimer =
        mContentRefreshDrivers.IsEmpty() && mRootRefreshDrivers.IsEmpty();
    if (stopTimer) {
      StopTimer();
    }
  }

  TimeStamp MostRecentRefresh() const { return mLastFireTime; }
  VsyncId MostRecentRefreshVsyncId() const { return mLastFireId; }
  virtual bool IsBlocked() { return false; }

  virtual TimeDuration GetTimerRate() = 0;

  TimeStamp GetIdleDeadlineHint(TimeStamp aDefault) {
    MOZ_ASSERT(NS_IsMainThread());

    if (!IsTicking() && !gfxPlatform::IsInLayoutAsapMode()) {
      return aDefault;
    }

    TimeStamp mostRecentRefresh = MostRecentRefresh();
    TimeDuration refreshPeriod = GetTimerRate();
    TimeStamp idleEnd = mostRecentRefresh + refreshPeriod;
    double highRateMultiplier = nsRefreshDriver::HighRateMultiplier();

    // If we haven't painted for some time, then guess that we won't paint
    // again for a while, so the refresh driver is not a good way to predict
    // idle time.
    if (highRateMultiplier == 1.0 &&
        (idleEnd +
             refreshPeriod *
                 StaticPrefs::layout_idle_period_required_quiescent_frames() <
         TimeStamp::Now())) {
      return aDefault;
    }

    // End the predicted idle time a little early, the amount controlled by a
    // pref, to prevent overrunning the idle time and delaying a frame.
    // But do that only if we aren't in high rate mode.
    idleEnd = idleEnd - TimeDuration::FromMilliseconds(
                            highRateMultiplier *
                            StaticPrefs::layout_idle_period_time_limit());
    return idleEnd < aDefault ? idleEnd : aDefault;
  }

  Maybe<TimeStamp> GetNextTickHint() {
    MOZ_ASSERT(NS_IsMainThread());
    TimeStamp nextTick = MostRecentRefresh() + GetTimerRate();
    return nextTick < TimeStamp::Now() ? Nothing() : Some(nextTick);
  }

  // Returns null if the RefreshDriverTimer is attached to several
  // RefreshDrivers. That may happen for example when there are
  // several windows open.
  nsPresContext* GetPresContextForOnlyRefreshDriver() {
    if (mRootRefreshDrivers.Length() == 1 && mContentRefreshDrivers.IsEmpty()) {
      return mRootRefreshDrivers[0]->GetPresContext();
    }
    if (mContentRefreshDrivers.Length() == 1 && mRootRefreshDrivers.IsEmpty()) {
      return mContentRefreshDrivers[0]->GetPresContext();
    }
    return nullptr;
  }

  bool IsAnyToplevelContentPageLoading() {
    for (nsTArray<RefPtr<nsRefreshDriver>>* drivers :
         {&mRootRefreshDrivers, &mContentRefreshDrivers}) {
      for (RefPtr<nsRefreshDriver>& driver : *drivers) {
        if (nsPresContext* pc = driver->GetPresContext()) {
          if (pc->Document()->IsTopLevelContentDocument() &&
              pc->Document()->GetReadyStateEnum() <
                  Document::READYSTATE_COMPLETE) {
            return true;
          }
        }
      }
    }

    return false;
  }

 protected:
  virtual ~RefreshDriverTimer() {
    MOZ_ASSERT(
        mContentRefreshDrivers.Length() == 0,
        "Should have removed all content refresh drivers from here by now!");
    MOZ_ASSERT(
        mRootRefreshDrivers.Length() == 0,
        "Should have removed all root refresh drivers from here by now!");
  }

  virtual void StartTimer() = 0;
  virtual void StopTimer() = 0;
  virtual void ScheduleNextTick(TimeStamp aNowTime) = 0;

 public:
  virtual bool IsTicking() const = 0;

 protected:
  bool IsRootRefreshDriver(nsRefreshDriver* aDriver) {
    nsPresContext* pc = aDriver->GetPresContext();
    nsPresContext* rootContext = pc ? pc->GetRootPresContext() : nullptr;
    if (!rootContext) {
      return false;
    }

    return aDriver == rootContext->RefreshDriver();
  }

  /*
   * Actually runs a tick, poking all the attached RefreshDrivers.
   * Grabs the "now" time via TimeStamp::Now().
   */
  void Tick() {
    TimeStamp now = TimeStamp::Now();
    Tick(VsyncId(), now);
  }

  void TickRefreshDrivers(VsyncId aId, TimeStamp aNow,
                          nsTArray<RefPtr<nsRefreshDriver>>& aDrivers) {
    if (aDrivers.IsEmpty()) {
      return;
    }

    for (nsRefreshDriver* driver : aDrivers.Clone()) {
      // don't poke this driver if it's in test mode
      if (driver->IsTestControllingRefreshesEnabled()) {
        continue;
      }

      TickDriver(driver, aId, aNow);
    }
  }

  /*
   * Tick the refresh drivers based on the given timestamp.
   */
  void Tick(VsyncId aId, TimeStamp now) {
    ScheduleNextTick(now);

    mLastFireTime = now;
    mLastFireId = aId;

    LOG("[%p] ticking drivers...", this);

    TickRefreshDrivers(aId, now, mContentRefreshDrivers);
    TickRefreshDrivers(aId, now, mRootRefreshDrivers);

    LOG("[%p] done.", this);
  }

  static void TickDriver(nsRefreshDriver* driver, VsyncId aId, TimeStamp now) {
    driver->Tick(aId, now);
  }

  TimeStamp mLastFireTime;
  VsyncId mLastFireId;
  TimeStamp mTargetTime;

  nsTArray<RefPtr<nsRefreshDriver>> mContentRefreshDrivers;
  nsTArray<RefPtr<nsRefreshDriver>> mRootRefreshDrivers;

  // useful callback for nsITimer-based derived classes, here
  // because of c++ protected shenanigans
  static void TimerTick(nsITimer* aTimer, void* aClosure) {
    RefPtr<RefreshDriverTimer> timer =
        static_cast<RefreshDriverTimer*>(aClosure);
    timer->Tick();
  }
};

/*
 * A RefreshDriverTimer that uses a nsITimer as the underlying timer.  Note that
 * this is a ONE_SHOT timer, not a repeating one!  Subclasses are expected to
 * implement ScheduleNextTick and intelligently calculate the next time to tick,
 * and to reset mTimer.  Using a repeating nsITimer gets us into a lot of pain
 * with its attempt at intelligent slack removal and such, so we don't do it.
 */
class SimpleTimerBasedRefreshDriverTimer : public RefreshDriverTimer {
 public:
  /*
   * aRate -- the delay, in milliseconds, requested between timer firings
   */
  explicit SimpleTimerBasedRefreshDriverTimer(double aRate) {
    SetRate(aRate);
    mTimer = NS_NewTimer();
  }

  virtual ~SimpleTimerBasedRefreshDriverTimer() override { StopTimer(); }

  // will take effect at next timer tick
  virtual void SetRate(double aNewRate) {
    mRateMilliseconds = aNewRate;
    mRateDuration = TimeDuration::FromMilliseconds(mRateMilliseconds);
  }

  double GetRate() const { return mRateMilliseconds; }

  TimeDuration GetTimerRate() override { return mRateDuration; }

 protected:
  void StartTimer() override {
    // pretend we just fired, and we schedule the next tick normally
    mLastFireTime = TimeStamp::Now();
    mLastFireId = VsyncId();

    mTargetTime = mLastFireTime + mRateDuration;

    uint32_t delay = static_cast<uint32_t>(mRateMilliseconds);
    mTimer->InitWithNamedFuncCallback(
        TimerTick, this, delay, nsITimer::TYPE_ONE_SHOT,
        "SimpleTimerBasedRefreshDriverTimer::StartTimer");
  }

  void StopTimer() override { mTimer->Cancel(); }

  double mRateMilliseconds;
  TimeDuration mRateDuration;
  RefPtr<nsITimer> mTimer;
};

/*
 * A refresh driver that listens to vsync events and ticks the refresh driver
 * on vsync intervals. We throttle the refresh driver if we get too many
 * vsync events and wait to catch up again.
 */
class VsyncRefreshDriverTimer : public RefreshDriverTimer {
 public:
  // This is used in the parent process for all platforms except Linux Wayland.
  static RefPtr<VsyncRefreshDriverTimer>
  CreateForParentProcessWithGlobalVsync() {
    MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<VsyncDispatcher> vsyncDispatcher =
        gfxPlatform::GetPlatform()->GetGlobalVsyncDispatcher();
    RefPtr<VsyncRefreshDriverTimer> timer =
        new VsyncRefreshDriverTimer(std::move(vsyncDispatcher), nullptr);
    return timer.forget();
  }

  // This is used in the parent process for Linux Wayland only, where we have a
  // per-widget VsyncSource which is independent from the gfxPlatform's global
  // VsyncSource.
  static RefPtr<VsyncRefreshDriverTimer>
  CreateForParentProcessWithLocalVsyncDispatcher(
      RefPtr<VsyncDispatcher>&& aVsyncDispatcher) {
    MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<VsyncRefreshDriverTimer> timer =
        new VsyncRefreshDriverTimer(std::move(aVsyncDispatcher), nullptr);
    return timer.forget();
  }

  // This is used in the content process.
  static RefPtr<VsyncRefreshDriverTimer> CreateForContentProcess(
      RefPtr<VsyncMainChild>&& aVsyncChild) {
    MOZ_RELEASE_ASSERT(XRE_IsContentProcess());
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<VsyncRefreshDriverTimer> timer =
        new VsyncRefreshDriverTimer(nullptr, std::move(aVsyncChild));
    return timer.forget();
  }

  TimeDuration GetTimerRate() override {
    if (mVsyncDispatcher) {
      mVsyncRate = mVsyncDispatcher->GetVsyncRate();
    } else if (mVsyncChild) {
      mVsyncRate = mVsyncChild->GetVsyncRate();
    }

    // If hardware queries fail / are unsupported, we have to just guess.
    return mVsyncRate != TimeDuration::Forever()
               ? mVsyncRate
               : TimeDuration::FromMilliseconds(1000.0 / 60.0);
  }

  bool IsBlocked() override {
    return !mSuspendVsyncPriorityTicksUntil.IsNull() &&
           mSuspendVsyncPriorityTicksUntil > TimeStamp::Now() &&
           ShouldGiveNonVsyncTasksMoreTime();
  }

 private:
  // RefreshDriverVsyncObserver redirects vsync notifications to the main thread
  // and calls VsyncRefreshDriverTimer::NotifyVsyncOnMainThread on it. It also
  // acts as a weak reference to the refresh driver timer, dropping its
  // reference when RefreshDriverVsyncObserver::Shutdown is called from the
  // timer's destructor.
  //
  // RefreshDriverVsyncObserver::NotifyVsync is called from different places
  // depending on the process type.
  //
  // Parent process:
  // NotifyVsync is called by RefreshDriverVsyncDispatcher, on a background
  // thread. RefreshDriverVsyncDispatcher keeps strong references to its
  // VsyncObservers, both in its array of observers and while calling
  // NotifyVsync. So it might drop its last reference to the observer on a
  // background thread. This means that the VsyncRefreshDriverTimer itself can't
  // be the observer (because its destructor would potentially be run on a
  // background thread), and it's why we use this separate class.
  //
  // Child process:
  // NotifyVsync is called by VsyncMainChild, on the main thread.
  // VsyncMainChild keeps raw pointers to its observers.
  class RefreshDriverVsyncObserver final : public VsyncObserver {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(
        VsyncRefreshDriverTimer::RefreshDriverVsyncObserver, override)

   public:
    explicit RefreshDriverVsyncObserver(
        VsyncRefreshDriverTimer* aVsyncRefreshDriverTimer)
        : mVsyncRefreshDriverTimer(aVsyncRefreshDriverTimer),
          mLastPendingVsyncNotification(
              "RefreshDriverVsyncObserver::mLastPendingVsyncNotification") {
      MOZ_ASSERT(NS_IsMainThread());
    }

    void NotifyVsync(const VsyncEvent& aVsync) override {
      // Compress vsync notifications such that only 1 may run at a time
      // This is so that we don't flood the refresh driver with vsync messages
      // if the main thread is blocked for long periods of time
      {  // scope lock
        auto pendingVsync = mLastPendingVsyncNotification.Lock();
        bool hadPendingVsync = pendingVsync->isSome();
        *pendingVsync = Some(aVsync);
        if (hadPendingVsync) {
          return;
        }
      }

      if (XRE_IsContentProcess()) {
        // In the content process, NotifyVsync is called by VsyncMainChild on
        // the main thread. No need to use a runnable, just call
        // NotifyVsyncTimerOnMainThread() directly.
        NotifyVsyncTimerOnMainThread();
        return;
      }

      // In the parent process, NotifyVsync is called on the vsync thread, which
      // on most platforms is different from the main thread, so we need to
      // dispatch a runnable for running NotifyVsyncTimerOnMainThread on the
      // main thread.
      // TODO: On Linux Wayland, the vsync thread is currently the main thread,
      // and yet we still dispatch the runnable. Do we need to?
      bool useVsyncPriority = mozilla::BrowserTabsRemoteAutostart();
      nsCOMPtr<nsIRunnable> vsyncEvent = new PrioritizableRunnable(
          NS_NewRunnableFunction(
              "RefreshDriverVsyncObserver::NotifyVsyncTimerOnMainThread",
              [self = RefPtr{this}]() {
                self->NotifyVsyncTimerOnMainThread();
              }),
          useVsyncPriority ? nsIRunnablePriority::PRIORITY_VSYNC
                           : nsIRunnablePriority::PRIORITY_NORMAL);
      NS_DispatchToMainThread(vsyncEvent);
    }

    void NotifyVsyncTimerOnMainThread() {
      MOZ_ASSERT(NS_IsMainThread());

      if (!mVsyncRefreshDriverTimer) {
        // Ignore calls after Shutdown.
        return;
      }

      VsyncEvent vsyncEvent;
      {
        // Get the last of the queued-up vsync notifications.
        auto pendingVsync = mLastPendingVsyncNotification.Lock();
        MOZ_RELEASE_ASSERT(
            pendingVsync->isSome(),
            "We should always have a pending vsync notification here.");
        vsyncEvent = pendingVsync->extract();
      }

      // Call VsyncRefreshDriverTimer::NotifyVsyncOnMainThread, and keep a
      // strong reference to it while calling the method.
      RefPtr<VsyncRefreshDriverTimer> timer = mVsyncRefreshDriverTimer;
      timer->NotifyVsyncOnMainThread(vsyncEvent);
    }

    void Shutdown() {
      MOZ_ASSERT(NS_IsMainThread());
      mVsyncRefreshDriverTimer = nullptr;
    }

   private:
    ~RefreshDriverVsyncObserver() = default;

    // VsyncRefreshDriverTimer holds this RefreshDriverVsyncObserver and it will
    // be always available before Shutdown(). We can just use the raw pointer
    // here.
    // Only accessed on the main thread.
    VsyncRefreshDriverTimer* mVsyncRefreshDriverTimer;

    // Non-empty between a call to NotifyVsync and a call to
    // NotifyVsyncOnMainThread. When multiple vsync notifications have been
    // received between those two calls, this contains the last of the pending
    // notifications. This is used both in the parent process and in the child
    // process, but it only does something useful in the parent process. In the
    // child process, both calls happen on the main thread right after one
    // another, so there's only one notification to keep track of; vsync
    // notification coalescing for child processes happens at the IPC level
    // instead.
    DataMutex<Maybe<VsyncEvent>> mLastPendingVsyncNotification;

  };  // RefreshDriverVsyncObserver

  VsyncRefreshDriverTimer(RefPtr<VsyncDispatcher>&& aVsyncDispatcher,
                          RefPtr<VsyncMainChild>&& aVsyncChild)
      : mVsyncDispatcher(aVsyncDispatcher),
        mVsyncChild(aVsyncChild),
        mVsyncRate(TimeDuration::Forever()),
        mRecentVsync(TimeStamp::Now()),
        mLastTickStart(TimeStamp::Now()),
        mLastIdleTaskCount(0),
        mLastRunOutOfMTTasksCount(0),
        mProcessedVsync(true),
        mHasPendingLowPrioTask(false) {
    mVsyncObserver = new RefreshDriverVsyncObserver(this);
  }

  ~VsyncRefreshDriverTimer() override {
    if (mVsyncDispatcher) {
      mVsyncDispatcher->RemoveVsyncObserver(mVsyncObserver);
      mVsyncDispatcher = nullptr;
    } else if (mVsyncChild) {
      mVsyncChild->RemoveChildRefreshTimer(mVsyncObserver);
      mVsyncChild = nullptr;
    }

    // Detach current vsync timer from this VsyncObserver. The observer will no
    // longer tick this timer.
    mVsyncObserver->Shutdown();
    mVsyncObserver = nullptr;
  }

  bool ShouldGiveNonVsyncTasksMoreTime(bool aCheckOnlyNewPendingTasks = false) {
    TaskController* taskController = TaskController::Get();
    IdleTaskManager* idleTaskManager = taskController->GetIdleTaskManager();
    VsyncTaskManager* vsyncTaskManager = VsyncTaskManager::Get();

    // Note, pendingTaskCount includes also all the pending idle and vsync
    // tasks.
    uint64_t pendingTaskCount =
        taskController->PendingMainthreadTaskCountIncludingSuspended();
    uint64_t pendingIdleTaskCount = idleTaskManager->PendingTaskCount();
    uint64_t pendingVsyncTaskCount = vsyncTaskManager->PendingTaskCount();
    if (!(pendingTaskCount > (pendingIdleTaskCount + pendingVsyncTaskCount))) {
      return false;
    }
    if (aCheckOnlyNewPendingTasks) {
      return true;
    }

    uint64_t idleTaskCount = idleTaskManager->ProcessedTaskCount();

    // If we haven't processed new idle tasks and we have pending
    // non-idle tasks, give those non-idle tasks more time,
    // but only if the main thread wasn't totally empty at some point.
    // In the parent process RunOutOfMTTasksCount() is less meaningful
    // because some of the tasks run through AppShell.
    return mLastIdleTaskCount == idleTaskCount &&
           (taskController->RunOutOfMTTasksCount() ==
                mLastRunOutOfMTTasksCount ||
            XRE_IsParentProcess());
  }

  void NotifyVsyncOnMainThread(const VsyncEvent& aVsyncEvent) {
    MOZ_ASSERT(NS_IsMainThread());

    mRecentVsync = aVsyncEvent.mTime;
    mRecentVsyncId = aVsyncEvent.mId;
    if (!mSuspendVsyncPriorityTicksUntil.IsNull() &&
        mSuspendVsyncPriorityTicksUntil > TimeStamp::Now()) {
      if (ShouldGiveNonVsyncTasksMoreTime()) {
        if (!IsAnyToplevelContentPageLoading()) {
          // If pages aren't loading and there aren't other tasks to run,
          // trigger the pending vsync notification.
          mPendingVsync = mRecentVsync;
          mPendingVsyncId = mRecentVsyncId;
          if (!mHasPendingLowPrioTask) {
            mHasPendingLowPrioTask = true;
            NS_DispatchToMainThreadQueue(
                NS_NewRunnableFunction(
                    "NotifyVsyncOnMainThread[low priority]",
                    [self = RefPtr{this}]() {
                      self->mHasPendingLowPrioTask = false;
                      if (self->mRecentVsync == self->mPendingVsync &&
                          self->mRecentVsyncId == self->mPendingVsyncId &&
                          !self->ShouldGiveNonVsyncTasksMoreTime()) {
                        self->mSuspendVsyncPriorityTicksUntil = TimeStamp();
                        self->NotifyVsyncOnMainThread({self->mPendingVsyncId,
                                                       self->mPendingVsync,
                                                       /* unused */
                                                       TimeStamp()});
                      }
                    }),
                EventQueuePriority::Low);
          }
        }
        return;
      }

      // Clear the value since we aren't blocking anymore because there aren't
      // any non-idle tasks to process.
      mSuspendVsyncPriorityTicksUntil = TimeStamp();
    }

    if (StaticPrefs::layout_lower_priority_refresh_driver_during_load() &&
        ShouldGiveNonVsyncTasksMoreTime()) {
      nsPresContext* pctx = GetPresContextForOnlyRefreshDriver();
      if (pctx && pctx->HadFirstContentfulPaint() && pctx->Document() &&
          pctx->Document()->GetReadyStateEnum() <
              Document::READYSTATE_COMPLETE) {
        nsPIDOMWindowInner* win = pctx->Document()->GetInnerWindow();
        uint32_t frameRateMultiplier = pctx->GetNextFrameRateMultiplier();
        if (!frameRateMultiplier) {
          pctx->DidUseFrameRateMultiplier();
        }
        if (win && frameRateMultiplier) {
          dom::Performance* perf = win->GetPerformance();
          // Limit slower refresh rate to 5 seconds between the
          // first contentful paint and page load.
          if (perf &&
              perf->Now() < StaticPrefs::page_load_deprioritization_period()) {
            if (mProcessedVsync) {
              mProcessedVsync = false;
              TimeDuration rate = GetTimerRate();
              uint32_t slowRate = static_cast<uint32_t>(rate.ToMilliseconds() *
                                                        frameRateMultiplier);
              pctx->DidUseFrameRateMultiplier();
              nsCOMPtr<nsIRunnable> vsyncEvent = NewRunnableMethod<>(
                  "VsyncRefreshDriverTimer::IdlePriorityNotify", this,
                  &VsyncRefreshDriverTimer::IdlePriorityNotify);
              NS_DispatchToCurrentThreadQueue(vsyncEvent.forget(), slowRate,
                                              EventQueuePriority::Idle);
            }
            return;
          }
        }
      }
    }

    TickRefreshDriver(aVsyncEvent.mId, aVsyncEvent.mTime);
  }

  void RecordTelemetryProbes(TimeStamp aVsyncTimestamp) {
    MOZ_ASSERT(NS_IsMainThread());
#ifndef ANDROID /* bug 1142079 */
    if (XRE_IsParentProcess()) {
      TimeDuration vsyncLatency = TimeStamp::Now() - aVsyncTimestamp;
      glean::layout::refresh_driver_chrome_frame_delay.AccumulateRawDuration(
          vsyncLatency);
    } else if (mVsyncRate != TimeDuration::Forever()) {
      TimeDuration contentDelay =
          (TimeStamp::Now() - mLastTickStart) - mVsyncRate;
      if (contentDelay.ToMilliseconds() < 0) {
        // Vsyncs are noisy and some can come at a rate quicker than
        // the reported hardware rate. In those cases, consider that we have 0
        // delay.
        contentDelay = TimeDuration::FromMilliseconds(0);
      }
      glean::layout::refresh_driver_content_frame_delay.AccumulateRawDuration(
          contentDelay);
    } else {
      // Request the vsync rate which VsyncChild stored the last time it got a
      // vsync notification.
      mVsyncRate = mVsyncChild->GetVsyncRate();
    }
#endif
  }

  void OnTimerStart() {
    mLastTickStart = TimeStamp::Now();
    mLastTickEnd = TimeStamp();
    mLastIdleTaskCount = 0;
  }

  void IdlePriorityNotify() {
    if (mLastProcessedTick.IsNull() || mRecentVsync > mLastProcessedTick) {
      // mSuspendVsyncPriorityTicksUntil is for high priority vsync
      // notifications only.
      mSuspendVsyncPriorityTicksUntil = TimeStamp();
      TickRefreshDriver(mRecentVsyncId, mRecentVsync);
    }

    mProcessedVsync = true;
  }

  hal::PerformanceHintSession* GetPerformanceHintSession() {
    // The ContentChild creates/destroys the PerformanceHintSession in response
    // to the process' priority being foregrounded/backgrounded. We can only use
    // this session when using a single vsync source for the process, otherwise
    // these threads may be performing work for multiple
    // VsyncRefreshDriverTimers and we will misreport the work duration.
    const ContentChild* contentChild = ContentChild::GetSingleton();
    if (contentChild && mVsyncChild) {
      return contentChild->PerformanceHintSession();
    }

    return nullptr;
  }

  void TickRefreshDriver(VsyncId aId, TimeStamp aVsyncTimestamp) {
    MOZ_ASSERT(NS_IsMainThread());

    RecordTelemetryProbes(aVsyncTimestamp);

    TimeStamp tickStart = TimeStamp::Now();

    const TimeDuration previousRate = mVsyncRate;
    const TimeDuration rate = GetTimerRate();

    if (rate != previousRate) {
      if (auto* const performanceHintSession = GetPerformanceHintSession()) {
        performanceHintSession->UpdateTargetWorkDuration(
            ContentChild::GetPerformanceHintTarget(rate));
      }
    }

    if (TimeDuration::FromMilliseconds(nsRefreshDriver::DefaultInterval()) >
        rate) {
      sMostRecentHighRateVsync = tickStart;
      sMostRecentHighRate = rate;
    }

#ifdef DEBUG
    // On 32-bit Windows we sometimes get times where TimeStamp::Now() is not
    // monotonic because the underlying system apis produce non-monontonic
    // results; see bug 1306896.
    // On Wayland, vsync timestamp might not precisely match system time; see
    // bug 1958043.
#  if defined(_WIN32) || defined(MOZ_WAYLAND)
    Unused << NS_WARN_IF(aVsyncTimestamp > tickStart);
#  else
    MOZ_ASSERT(aVsyncTimestamp <= tickStart);
#  endif
#endif

    bool shouldGiveNonVSyncTasksMoreTime = ShouldGiveNonVsyncTasksMoreTime();

    // Set these variables before calling RunRefreshDrivers so that they are
    // visible to any nested ticks.
    mLastTickStart = tickStart;
    mLastProcessedTick = aVsyncTimestamp;

    RunRefreshDrivers(aId, aVsyncTimestamp);

    TimeStamp tickEnd = TimeStamp::Now();

    if (auto* const performanceHintSession = GetPerformanceHintSession()) {
      performanceHintSession->ReportActualWorkDuration(tickEnd - tickStart);
    }

    // Re-read mLastTickStart in case there was a nested tick inside this
    // tick.
    TimeStamp mostRecentTickStart = mLastTickStart;

    // Let also non-RefreshDriver code to run at least for awhile if we have
    // a mVsyncRefreshDriverTimer.
    // Always give a tiny bit, 5% of the vsync interval, time outside the
    // tick
    // In case there are both normal tasks and RefreshDrivers are doing
    // work, mSuspendVsyncPriorityTicksUntil will be set to a timestamp in the
    // future where the period between the previous tick start
    // (mostRecentTickStart) and the next tick needs to be at least the amount
    // of work normal tasks and RefreshDrivers did together (minus short grace
    // period).
    TimeDuration gracePeriod = rate / int64_t(20);

    if (shouldGiveNonVSyncTasksMoreTime && !mLastTickEnd.IsNull() &&
        XRE_IsContentProcess() &&
        // For RefreshDriver scheduling during page load there is currently
        // idle priority based setup.
        // XXX Consider to remove the page load specific code paths.
        !IsAnyToplevelContentPageLoading()) {
      // In case normal tasks are doing lots of work, we still want to paint
      // every now and then, so only at maximum 4 * rate of work is counted
      // here.
      // If we're giving extra time for tasks outside a tick, try to
      // ensure the next vsync after that period is handled, so subtract
      // a grace period.
      TimeDuration timeForOutsideTick = std::clamp(
          tickStart - mLastTickEnd - gracePeriod, gracePeriod, rate * 4);
      mSuspendVsyncPriorityTicksUntil = tickEnd + timeForOutsideTick;
    } else if (ShouldGiveNonVsyncTasksMoreTime(true)) {
      // We've got some new tasks, give them some extra time.
      // This handles also the case when mLastTickEnd.IsNull() above and we
      // should give some more time for non-vsync tasks.
      mSuspendVsyncPriorityTicksUntil = tickEnd + gracePeriod;
    } else {
      mSuspendVsyncPriorityTicksUntil = mostRecentTickStart + gracePeriod;
    }

    mLastIdleTaskCount =
        TaskController::Get()->GetIdleTaskManager()->ProcessedTaskCount();
    mLastRunOutOfMTTasksCount = TaskController::Get()->RunOutOfMTTasksCount();
    mLastTickEnd = tickEnd;
  }

  void StartTimer() override {
    MOZ_ASSERT(NS_IsMainThread());

    mLastFireTime = TimeStamp::Now();
    mLastFireId = VsyncId();

    if (mVsyncDispatcher) {
      mVsyncDispatcher->AddVsyncObserver(mVsyncObserver);
    } else if (mVsyncChild) {
      mVsyncChild->AddChildRefreshTimer(mVsyncObserver);
      OnTimerStart();
    }
    mIsTicking = true;
  }

  void StopTimer() override {
    MOZ_ASSERT(NS_IsMainThread());

    if (mVsyncDispatcher) {
      mVsyncDispatcher->RemoveVsyncObserver(mVsyncObserver);
    } else if (mVsyncChild) {
      mVsyncChild->RemoveChildRefreshTimer(mVsyncObserver);
    }
    mIsTicking = false;
  }

 public:
  bool IsTicking() const override { return mIsTicking; }

 protected:
  void ScheduleNextTick(TimeStamp aNowTime) override {
    // Do nothing since we just wait for the next vsync from
    // RefreshDriverVsyncObserver.
  }

  void RunRefreshDrivers(VsyncId aId, TimeStamp aTimeStamp) {
    Tick(aId, aTimeStamp);
    for (auto& driver : mContentRefreshDrivers) {
      driver->FinishedVsyncTick();
    }
    for (auto& driver : mRootRefreshDrivers) {
      driver->FinishedVsyncTick();
    }
  }

  // Always non-null. Has a weak pointer to us and notifies us of vsync.
  RefPtr<RefreshDriverVsyncObserver> mVsyncObserver;

  // Used in the parent process. We register mVsyncObserver with it for the
  // duration during which we want to receive vsync notifications. We also
  // use it to query the current vsync rate.
  RefPtr<VsyncDispatcher> mVsyncDispatcher;
  // Used it the content process. We register mVsyncObserver with it for the
  // duration during which we want to receive vsync notifications. The
  // mVsyncChild will be always available before VsyncChild::ActorDestroy().
  // After ActorDestroy(), StartTimer() and StopTimer() calls will be non-op.
  RefPtr<VsyncMainChild> mVsyncChild;

  TimeDuration mVsyncRate;
  bool mIsTicking = false;

  TimeStamp mRecentVsync;
  VsyncId mRecentVsyncId;
  // The local start time when RefreshDrivers' Tick was called last time.
  TimeStamp mLastTickStart;
  // The local end time of the last RefreshDrivers' tick.
  TimeStamp mLastTickEnd;
  // The number of idle tasks the main thread has processed. It is updated
  // right after RefreshDrivers' tick.
  uint64_t mLastIdleTaskCount;
  // If there were no idle tasks, we need to check if the main event queue
  // was totally empty at times.
  uint64_t mLastRunOutOfMTTasksCount;
  // Note, mLastProcessedTick stores the vsync timestamp, which may be coming
  // from a different process.
  TimeStamp mLastProcessedTick;
  // mSuspendVsyncPriorityTicksUntil is used to block too high refresh rate in
  // case the main thread has also other non-idle tasks to process.
  // The timestamp is effectively mLastTickEnd + some duration.
  TimeStamp mSuspendVsyncPriorityTicksUntil;
  bool mProcessedVsync;

  TimeStamp mPendingVsync;
  VsyncId mPendingVsyncId;
  bool mHasPendingLowPrioTask;
};  // VsyncRefreshDriverTimer

/**
 * Since the content process takes some time to setup
 * the vsync IPC connection, this timer is used
 * during the intial startup process.
 * During initial startup, the refresh drivers
 * are ticked off this timer, and are swapped out once content
 * vsync IPC connection is established.
 */
class StartupRefreshDriverTimer : public SimpleTimerBasedRefreshDriverTimer {
 public:
  explicit StartupRefreshDriverTimer(double aRate)
      : SimpleTimerBasedRefreshDriverTimer(aRate) {}

 protected:
  void ScheduleNextTick(TimeStamp aNowTime) override {
    // Since this is only used for startup, it isn't super critical
    // that we tick at consistent intervals.
    TimeStamp newTarget = aNowTime + mRateDuration;
    uint32_t delay =
        static_cast<uint32_t>((newTarget - aNowTime).ToMilliseconds());
    mTimer->InitWithNamedFuncCallback(
        TimerTick, this, delay, nsITimer::TYPE_ONE_SHOT,
        "StartupRefreshDriverTimer::ScheduleNextTick");
    mTargetTime = newTarget;
  }

 public:
  bool IsTicking() const override { return true; }
};

/*
 * A RefreshDriverTimer for inactive documents.  When a new refresh driver is
 * added, the rate is reset to the base (normally 1s/1fps).  Every time
 * it ticks, a single refresh driver is poked.  Once they have all been poked,
 * the duration between ticks doubles, up to mDisableAfterMilliseconds.  At that
 * point, the timer is quiet and doesn't tick (until something is added to it
 * again).
 *
 * When a timer is removed, there is a possibility of another timer
 * being skipped for one cycle.  We could avoid this by adjusting
 * mNextDriverIndex in RemoveRefreshDriver, but there's little need to
 * add that complexity.  All we want is for inactive drivers to tick
 * at some point, but we don't care too much about how often.
 */
class InactiveRefreshDriverTimer final
    : public SimpleTimerBasedRefreshDriverTimer {
 public:
  explicit InactiveRefreshDriverTimer(double aRate)
      : SimpleTimerBasedRefreshDriverTimer(aRate),
        mNextTickDuration(aRate),
        mDisableAfterMilliseconds(-1.0),
        mNextDriverIndex(0) {}

  InactiveRefreshDriverTimer(double aRate, double aDisableAfterMilliseconds)
      : SimpleTimerBasedRefreshDriverTimer(aRate),
        mNextTickDuration(aRate),
        mDisableAfterMilliseconds(aDisableAfterMilliseconds),
        mNextDriverIndex(0) {}

  void AddRefreshDriver(nsRefreshDriver* aDriver) override {
    RefreshDriverTimer::AddRefreshDriver(aDriver);

    LOG("[%p] inactive timer got new refresh driver %p, resetting rate", this,
        aDriver);

    // reset the timer, and start with the newly added one next time.
    mNextTickDuration = mRateMilliseconds;

    // we don't really have to start with the newly added one, but we may as
    // well not tick the old ones at the fastest rate any more than we need to.
    mNextDriverIndex = GetRefreshDriverCount() - 1;

    StopTimer();
    StartTimer();
  }

  TimeDuration GetTimerRate() override {
    return TimeDuration::FromMilliseconds(mNextTickDuration);
  }

 protected:
  uint32_t GetRefreshDriverCount() {
    return mContentRefreshDrivers.Length() + mRootRefreshDrivers.Length();
  }

  void StartTimer() override {
    mLastFireTime = TimeStamp::Now();
    mLastFireId = VsyncId();

    mTargetTime = mLastFireTime + mRateDuration;

    uint32_t delay = static_cast<uint32_t>(mRateMilliseconds);
    mTimer->InitWithNamedFuncCallback(TimerTickOne, this, delay,
                                      nsITimer::TYPE_ONE_SHOT,
                                      "InactiveRefreshDriverTimer::StartTimer");
    mIsTicking = true;
  }

  void StopTimer() override {
    mTimer->Cancel();
    mIsTicking = false;
  }

  void ScheduleNextTick(TimeStamp aNowTime) override {
    if (mDisableAfterMilliseconds > 0.0 &&
        mNextTickDuration > mDisableAfterMilliseconds) {
      // We hit the time after which we should disable
      // inactive window refreshes; don't schedule anything
      // until we get kicked by an AddRefreshDriver call.
      return;
    }

    // double the next tick time if we've already gone through all of them once
    if (mNextDriverIndex >= GetRefreshDriverCount()) {
      mNextTickDuration *= 2.0;
      mNextDriverIndex = 0;
    }

    // this doesn't need to be precise; do a simple schedule
    uint32_t delay = static_cast<uint32_t>(mNextTickDuration);
    mTimer->InitWithNamedFuncCallback(
        TimerTickOne, this, delay, nsITimer::TYPE_ONE_SHOT,
        "InactiveRefreshDriverTimer::ScheduleNextTick");

    LOG("[%p] inactive timer next tick in %f ms [index %d/%d]", this,
        mNextTickDuration, mNextDriverIndex, GetRefreshDriverCount());
  }

 public:
  bool IsTicking() const override { return mIsTicking; }

 protected:
  /* Runs just one driver's tick. */
  void TickOne() {
    TimeStamp now = TimeStamp::Now();

    ScheduleNextTick(now);

    mLastFireTime = now;
    mLastFireId = VsyncId();

    nsTArray<RefPtr<nsRefreshDriver>> drivers(mContentRefreshDrivers.Clone());
    drivers.AppendElements(mRootRefreshDrivers);
    size_t index = mNextDriverIndex;

    if (index < drivers.Length() &&
        !drivers[index]->IsTestControllingRefreshesEnabled()) {
      TickDriver(drivers[index], VsyncId(), now);
    }

    mNextDriverIndex++;
  }

  static void TimerTickOne(nsITimer* aTimer, void* aClosure) {
    RefPtr<InactiveRefreshDriverTimer> timer =
        static_cast<InactiveRefreshDriverTimer*>(aClosure);
    timer->TickOne();
  }

  double mNextTickDuration;
  double mDisableAfterMilliseconds;
  uint32_t mNextDriverIndex;
  bool mIsTicking = false;
};

}  // namespace mozilla

static StaticRefPtr<RefreshDriverTimer> sRegularRateTimer;
static StaticAutoPtr<nsTArray<RefreshDriverTimer*>> sRegularRateTimerList;
static StaticRefPtr<InactiveRefreshDriverTimer> sThrottledRateTimer;

static bool IsPresentingInVR() {
#ifdef MOZ_WIDGET_ANDROID
  return gfx::VRManagerChild::IsPresenting();
#else
  return false;
#endif
}

void nsRefreshDriver::CreateVsyncRefreshTimer() {
  MOZ_ASSERT(NS_IsMainThread());

  if (gfxPlatform::IsInLayoutAsapMode()) {
    return;
  }

  if (!mOwnTimer) {
    // If available, we fetch the widget-specific vsync source.
    nsPresContext* pc = GetPresContext();
    nsCOMPtr<nsIWidget> widget = pc->GetRootWidget();
    if (widget) {
      if (RefPtr<VsyncDispatcher> vsyncDispatcher =
              widget->GetVsyncDispatcher()) {
        mOwnTimer = VsyncRefreshDriverTimer::
            CreateForParentProcessWithLocalVsyncDispatcher(
                std::move(vsyncDispatcher));
        sRegularRateTimerList->AppendElement(mOwnTimer.get());
        return;
      }
      if (BrowserChild* browserChild = widget->GetOwningBrowserChild()) {
        if (RefPtr<VsyncMainChild> vsyncChildViaPBrowser =
                browserChild->GetVsyncChild()) {
          mOwnTimer = VsyncRefreshDriverTimer::CreateForContentProcess(
              std::move(vsyncChildViaPBrowser));
          sRegularRateTimerList->AppendElement(mOwnTimer.get());
          return;
        }
      }
    }
  }
  if (!sRegularRateTimer) {
    if (XRE_IsParentProcess()) {
      // Make sure all vsync systems are ready.
      gfxPlatform::GetPlatform();
      // In parent process, we can create the VsyncRefreshDriverTimer directly.
      sRegularRateTimer =
          VsyncRefreshDriverTimer::CreateForParentProcessWithGlobalVsync();
    } else {
      PBackgroundChild* actorChild =
          BackgroundChild::GetOrCreateForCurrentThread();
      if (NS_WARN_IF(!actorChild)) {
        return;
      }

      auto vsyncChildViaPBackground = MakeRefPtr<dom::VsyncMainChild>();
      dom::PVsyncChild* actor =
          actorChild->SendPVsyncConstructor(vsyncChildViaPBackground);
      if (NS_WARN_IF(!actor)) {
        return;
      }

      RefPtr<RefreshDriverTimer> vsyncRefreshDriverTimer =
          VsyncRefreshDriverTimer::CreateForContentProcess(
              std::move(vsyncChildViaPBackground));

      sRegularRateTimer = std::move(vsyncRefreshDriverTimer);
    }
  }
}

static uint32_t GetFirstFrameDelay(imgIRequest* req) {
  nsCOMPtr<imgIContainer> container;
  if (NS_FAILED(req->GetImage(getter_AddRefs(container))) || !container) {
    return 0;
  }

  // If this image isn't animated, there isn't a first frame delay.
  int32_t delay = container->GetFirstFrameDelay();
  if (delay < 0) {
    return 0;
  }

  return static_cast<uint32_t>(delay);
}

static constexpr nsLiteralCString sRenderingPhaseNames[] = {
    "Flush autofocus candidates"_ns,                 // FlushAutoFocusCandidates
    "Resize steps"_ns,                               // ResizeSteps
    "Scroll steps"_ns,                               // ScrollSteps
    "Evaluate media queries and report changes"_ns,  // EvaluateMediaQueriesAndReportChanges
    "Update animations and send events"_ns,    // UpdateAnimationsAndSendEvents
    "Fullscreen steps"_ns,                     // FullscreenSteps
    "Animation and video frame callbacks"_ns,  // AnimationFrameCallbacks
    "Layout, content-visibility and resize observers"_ns,  // Layout
    "View transition operations"_ns,        // ViewTransitionOperations
    "Update intersection observations"_ns,  // UpdateIntersectionObservations
    "Paint"_ns,                             // Paint
};

static_assert(std::size(sRenderingPhaseNames) == size_t(RenderingPhase::Count),
              "Unexpected rendering phase?");

template <typename Callback>
void nsRefreshDriver::RunRenderingPhaseLegacy(RenderingPhase aPhase,
                                              Callback&& aCallback) {
  if (!mRenderingPhasesNeeded.contains(aPhase)) {
    return;
  }
  mRenderingPhasesNeeded -= aPhase;

  AUTO_PROFILER_LABEL_DYNAMIC_CSTR_RELEVANT_FOR_JS(
      "Update the rendering", LAYOUT,
      sRenderingPhaseNames[size_t(aPhase)].get());
  aCallback();
}

template <typename Callback>
void nsRefreshDriver::RunRenderingPhase(RenderingPhase aPhase,
                                        Callback&& aCallback,
                                        DocFilter aExtraFilter) {
  RunRenderingPhaseLegacy(aPhase, [&] {
    if (MOZ_UNLIKELY(!mPresContext)) {
      return;
    }
    // https://html.spec.whatwg.org/#update-the-rendering step 3
    //
    //     Remove from docs any Document object doc for which any of the
    //     following are true.
    //
    // TODO(emilio): Per spec we should collect all these upfront, once.
    AutoTArray<RefPtr<Document>, 32> documents;
    auto ShouldCollect = [aExtraFilter](const Document* aDocument) {
      return !aDocument->IsRenderingSuppressed() &&
             (!aExtraFilter || aExtraFilter(*aDocument));
    };
    if (ShouldCollect(mPresContext->Document())) {
      documents.AppendElement(mPresContext->Document());
    }
    mPresContext->Document()->CollectDescendantDocuments(
        documents, Document::IncludeSubResources::Yes, ShouldCollect);
    for (auto& doc : documents) {
      aCallback(*doc);
    }
  });
}

/* static */
void nsRefreshDriver::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  // clean up our timers
  sRegularRateTimer = nullptr;
  sRegularRateTimerList = nullptr;
  sThrottledRateTimer = nullptr;
}

/* static */
int32_t nsRefreshDriver::DefaultInterval() {
  return NSToIntRound(1000.0 / gfxPlatform::GetDefaultFrameRate());
}

/* static */
double nsRefreshDriver::HighRateMultiplier() {
  // We're in high rate mode if we've gotten a fast rate during the last
  // DefaultInterval().
  bool inHighRateMode =
      !gfxPlatform::IsInLayoutAsapMode() &&
      StaticPrefs::layout_expose_high_rate_mode_from_refreshdriver() &&
      !sMostRecentHighRateVsync.IsNull() &&
      (sMostRecentHighRateVsync +
       TimeDuration::FromMilliseconds(DefaultInterval())) > TimeStamp::Now();
  if (!inHighRateMode) {
    // Clear the timestamp so that the next call is faster.
    sMostRecentHighRateVsync = TimeStamp();
    sMostRecentHighRate = TimeDuration();
    return 1.0;
  }

  return sMostRecentHighRate.ToMilliseconds() / DefaultInterval();
}

// Compute the interval to use for the refresh driver timer, in milliseconds.
// outIsDefault indicates that rate was not explicitly set by the user
// so we might choose other, more appropriate rates (e.g. vsync, etc)
// layout.frame_rate=0 indicates "ASAP mode".
// In ASAP mode rendering is iterated as fast as possible (typically for stress
// testing). A target rate of 10k is used internally instead of special-handling
// 0. Backends which block on swap/present/etc should try to not block when
// layout.frame_rate=0 - to comply with "ASAP" as much as possible.
double nsRefreshDriver::GetRegularTimerInterval() const {
  int32_t rate = Preferences::GetInt("layout.frame_rate", -1);
  if (rate < 0) {
    rate = gfxPlatform::GetDefaultFrameRate();
  } else if (rate == 0) {
    rate = 10000;
  }

  return 1000.0 / rate;
}

/* static */
double nsRefreshDriver::GetThrottledTimerInterval() {
  uint32_t rate = StaticPrefs::layout_throttled_frame_rate();
  return 1000.0 / rate;
}

/* static */
TimeDuration nsRefreshDriver::GetMinRecomputeVisibilityInterval() {
  return TimeDuration::FromMilliseconds(
      StaticPrefs::layout_visibility_min_recompute_interval_ms());
}

RefreshDriverTimer* nsRefreshDriver::ChooseTimer() {
  if (mThrottled) {
    if (!sThrottledRateTimer) {
      sThrottledRateTimer = new InactiveRefreshDriverTimer(
          GetThrottledTimerInterval(),
          DEFAULT_INACTIVE_TIMER_DISABLE_SECONDS * 1000.0);
    }
    return sThrottledRateTimer;
  }

  if (!mOwnTimer) {
    CreateVsyncRefreshTimer();
  }

  if (mOwnTimer) {
    return mOwnTimer.get();
  }

  if (!sRegularRateTimer) {
    double rate = GetRegularTimerInterval();
    sRegularRateTimer = new StartupRefreshDriverTimer(rate);
  }

  return sRegularRateTimer;
}

static nsDocShell* GetDocShell(nsPresContext* aPresContext) {
  if (!aPresContext) {
    return nullptr;
  }
  return static_cast<nsDocShell*>(aPresContext->GetDocShell());
}

nsRefreshDriver::nsRefreshDriver(nsPresContext* aPresContext)
    : mActiveTimer(nullptr),
      mOwnTimer(nullptr),
      mPresContext(aPresContext),
      mRootRefresh(nullptr),
      mNextTransactionId{0},
      mFreezeCount(0),
      mThrottledFrameRequestInterval(
          TimeDuration::FromMilliseconds(GetThrottledTimerInterval())),
      mMinRecomputeVisibilityInterval(GetMinRecomputeVisibilityInterval()),
      mThrottled(false),
      mNeedToRecomputeVisibility(false),
      mTestControllingRefreshes(false),
      mInRefresh(false),
      mWaitingForTransaction(false),
      mSkippedPaints(false),
      mResizeSuppressed(false),
      mInNormalTick(false),
      mAttemptedExtraTickSinceLastVsync(false),
      mHasExceededAfterLoadTickPeriod(false),
      mHasStartedTimerAtLeastOnce(false) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mPresContext,
             "Need a pres context to tell us to call Disconnect() later "
             "and decrement sRefreshDriverCount.");
  mMostRecentRefresh = TimeStamp::Now();
  mNextThrottledFrameRequestTick = mMostRecentRefresh;
  mNextRecomputeVisibilityTick = mMostRecentRefresh;

  if (!sRegularRateTimerList) {
    sRegularRateTimerList = new nsTArray<RefreshDriverTimer*>();
  }
  ++sRefreshDriverCount;
}

nsRefreshDriver::~nsRefreshDriver() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(ObserverCount() == mEarlyRunners.Length(),
             "observers, except pending selection scrolls, "
             "should have been unregistered");
  MOZ_ASSERT(!mActiveTimer, "timer should be gone");
  MOZ_ASSERT(!mPresContext,
             "Should have called Disconnect() and decremented "
             "sRefreshDriverCount!");

  if (mRootRefresh) {
    mRootRefresh->RemoveRefreshObserver(this, FlushType::Style);
    mRootRefresh = nullptr;
  }
  if (mOwnTimer && sRegularRateTimerList) {
    sRegularRateTimerList->RemoveElement(mOwnTimer.get());
  }
}

// Method for testing.  See nsIDOMWindowUtils.advanceTimeAndRefresh
// for description.
void nsRefreshDriver::AdvanceTimeAndRefresh(int64_t aMilliseconds) {
  // ensure that we're removed from our driver
  StopTimer();

  if (!mTestControllingRefreshes) {
    mMostRecentRefresh = TimeStamp::Now();

    mTestControllingRefreshes = true;
    if (mWaitingForTransaction) {
      // Disable any refresh driver throttling when entering test mode
      mWaitingForTransaction = false;
      mSkippedPaints = false;
    }
  }

  mMostRecentRefresh += TimeDuration::FromMilliseconds((double)aMilliseconds);

  mozilla::dom::AutoNoJSAPI nojsapi;
  DoTick();
}

void nsRefreshDriver::RestoreNormalRefresh() {
  mTestControllingRefreshes = false;
  EnsureTimerStarted(eAllowTimeToGoBackwards);
  mPendingTransactions.Clear();
}

void nsRefreshDriver::AddRefreshObserver(nsARefreshObserver* aObserver,
                                         FlushType aFlushType,
                                         const char* aObserverDescription) {
  ObserverArray& array = ArrayFor(aFlushType);
  MOZ_ASSERT(!array.Contains(aObserver),
             "We don't want to redundantly register the same observer");
  array.AppendElement(
      ObserverData{aObserver, aObserverDescription, TimeStamp::Now(),
                   MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext)),
                   profiler_capture_backtrace(), aFlushType});
#ifdef DEBUG
  MOZ_ASSERT(aObserver->mRegistrationCount >= 0,
             "Registration count shouldn't be able to go negative");
  aObserver->mRegistrationCount++;
#endif
  EnsureTimerStarted();
}

bool nsRefreshDriver::RemoveRefreshObserver(nsARefreshObserver* aObserver,
                                            FlushType aFlushType) {
  ObserverArray& array = ArrayFor(aFlushType);
  auto index = array.IndexOf(aObserver);
  if (index == ObserverArray::array_type::NoIndex) {
    return false;
  }

  if (profiler_thread_is_being_profiled_for_markers()) {
    auto& data = array.ElementAt(index);
    nsPrintfCString str("%s [%s]", data.mDescription,
                        kFlushTypeNames[aFlushType]);
    PROFILER_MARKER_TEXT(
        "RefreshObserver", GRAPHICS,
        MarkerOptions(MarkerStack::TakeBacktrace(std::move(data.mCause)),
                      MarkerTiming::IntervalUntilNowFrom(data.mRegisterTime),
                      std::move(data.mInnerWindowId)),
        str);
  }

  array.RemoveElementAt(index);
#ifdef DEBUG
  aObserver->mRegistrationCount--;
  MOZ_ASSERT(aObserver->mRegistrationCount >= 0,
             "Registration count shouldn't be able to go negative");
#endif
  return true;
}

void nsRefreshDriver::AddPostRefreshObserver(
    nsAPostRefreshObserver* aObserver) {
  MOZ_ASSERT(!mPostRefreshObservers.Contains(aObserver));
  mPostRefreshObservers.AppendElement(aObserver);
}

void nsRefreshDriver::RemovePostRefreshObserver(
    nsAPostRefreshObserver* aObserver) {
  bool removed = mPostRefreshObservers.RemoveElement(aObserver);
  MOZ_DIAGNOSTIC_ASSERT(removed);
  Unused << removed;
}

void nsRefreshDriver::AddImageRequest(imgIRequest* aRequest) {
  uint32_t delay = GetFirstFrameDelay(aRequest);
  if (delay == 0) {
    mRequests.Insert(aRequest);
  } else {
    auto* const start = mStartTable.GetOrInsertNew(delay);
    start->mEntries.Insert(aRequest);
  }

  EnsureTimerStarted();

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsCOMPtr<nsIURI> uri = aRequest->GetURI();

    PROFILER_MARKER_TEXT("Image Animation", GRAPHICS,
                         MarkerOptions(MarkerTiming::IntervalStart(),
                                       MarkerInnerWindowIdFromDocShell(
                                           GetDocShell(mPresContext))),
                         nsContentUtils::TruncatedURLForDisplay(uri));
  }
}

void nsRefreshDriver::RemoveImageRequest(imgIRequest* aRequest) {
  // Try to remove from both places, just in case.
  bool removed = mRequests.EnsureRemoved(aRequest);
  uint32_t delay = GetFirstFrameDelay(aRequest);
  if (delay != 0) {
    ImageStartData* start = mStartTable.Get(delay);
    if (start) {
      removed = removed | start->mEntries.EnsureRemoved(aRequest);
    }
  }

  if (removed && profiler_thread_is_being_profiled_for_markers()) {
    nsCOMPtr<nsIURI> uri = aRequest->GetURI();

    PROFILER_MARKER_TEXT("Image Animation", GRAPHICS,
                         MarkerOptions(MarkerTiming::IntervalEnd(),
                                       MarkerInnerWindowIdFromDocShell(
                                           GetDocShell(mPresContext))),
                         nsContentUtils::TruncatedURLForDisplay(uri));
  }
}

void nsRefreshDriver::RegisterCompositionPayload(
    const mozilla::layers::CompositionPayload& aPayload) {
  mCompositionPayloads.AppendElement(aPayload);
}

void nsRefreshDriver::AddForceNotifyContentfulPaintPresContext(
    nsPresContext* aPresContext) {
  mForceNotifyContentfulPaintPresContexts.AppendElement(aPresContext);
}

void nsRefreshDriver::FlushForceNotifyContentfulPaintPresContext() {
  while (!mForceNotifyContentfulPaintPresContexts.IsEmpty()) {
    WeakPtr<nsPresContext> presContext =
        mForceNotifyContentfulPaintPresContexts.PopLastElement();
    if (presContext) {
      presContext->NotifyContentfulPaint();
    }
  }
}

bool nsRefreshDriver::CanDoCatchUpTick() {
  if (mTestControllingRefreshes || !mActiveTimer) {
    return false;
  }

  // If we've already ticked for the current timer refresh (or more recently
  // than that), then we don't need to do any catching up.
  if (mMostRecentRefresh >= mActiveTimer->MostRecentRefresh()) {
    return false;
  }

  if (mActiveTimer->IsBlocked()) {
    return false;
  }

  if (mTickVsyncTime.IsNull()) {
    // Don't try to run a catch-up tick before there has been at least one
    // normal tick. The catch-up tick could negatively affect page load
    // performance.
    return false;
  }

  if (mPresContext && mPresContext->Document()->GetReadyStateEnum() <
                          Document::READYSTATE_COMPLETE) {
    // Don't try to run a catch-up tick before the page has finished loading.
    // The catch-up tick could negatively affect page load performance.
    return false;
  }

  return true;
}

bool nsRefreshDriver::CanDoExtraTick() {
  // Only allow one extra tick per normal vsync tick.
  if (mAttemptedExtraTickSinceLastVsync) {
    return false;
  }

  // If we don't have a timer, or we didn't tick on the timer's
  // refresh then we can't do an 'extra' tick (but we may still
  // do a catch up tick).
  if (!mActiveTimer ||
      mActiveTimer->MostRecentRefresh() != mMostRecentRefresh) {
    return false;
  }

  // Grab the current timestamp before checking the tick hint to be sure
  // sure that it's equal or smaller than the value used within checking
  // the tick hint.
  TimeStamp now = TimeStamp::Now();
  Maybe<TimeStamp> nextTick = mActiveTimer->GetNextTickHint();
  int32_t minimumRequiredTime = StaticPrefs::layout_extra_tick_minimum_ms();
  // If there's less than 4 milliseconds until the next tick, it's probably
  // not worth trying to catch up.
  if (minimumRequiredTime < 0 || !nextTick ||
      (*nextTick - now) < TimeDuration::FromMilliseconds(minimumRequiredTime)) {
    return false;
  }

  return true;
}

void nsRefreshDriver::EnsureTimerStarted(EnsureTimerStartedFlags aFlags) {
  // FIXME: Bug 1346065: We should also assert the case where we have no
  // stylo-threads.
  MOZ_ASSERT(!ServoStyleSet::IsInServoTraversal() || NS_IsMainThread(),
             "EnsureTimerStarted should be called only when we are not "
             "in servo traversal or on the main-thread");

  if (mTestControllingRefreshes) {
    return;
  }

  if (!mRefreshTimerStartedCause) {
    mRefreshTimerStartedCause = profiler_capture_backtrace();
  }

  // will it already fire, and no other changes needed?
  if (mActiveTimer && !(aFlags & eForceAdjustTimer)) {
    // If we're being called from within a user input handler, and we think
    // there's time to rush an extra tick immediately, then schedule a runnable
    // to run the extra tick.
    if (mUserInputProcessingCount && CanDoExtraTick()) {
      RefPtr<nsRefreshDriver> self = this;
      NS_DispatchToCurrentThreadQueue(
          NS_NewRunnableFunction(
              "RefreshDriver::EnsureTimerStarted::extra",
              [self]() -> void {
                // Re-check if we can still do an extra tick, in case anything
                // changed while the runnable was pending.
                if (self->CanDoExtraTick()) {
                  PROFILER_MARKER_UNTYPED("ExtraRefreshDriverTick", GRAPHICS);
                  LOG("[%p] Doing extra tick for user input", self.get());
                  self->mAttemptedExtraTickSinceLastVsync = true;
                  self->Tick(self->mActiveTimer->MostRecentRefreshVsyncId(),
                             self->mActiveTimer->MostRecentRefresh(),
                             IsExtraTick::Yes);
                }
              }),
          EventQueuePriority::Vsync);
    }
    return;
  }

  if (IsFrozen() || !mPresContext) {
    // If we don't want to start it now, or we've been disconnected.
    StopTimer();
    return;
  }

  if (mPresContext->Document()->IsBeingUsedAsImage()) {
    // Image documents receive ticks from clients' refresh drivers.
    // XXXdholbert Exclude SVG-in-opentype fonts from this optimization, until
    // they receive refresh-driver ticks from their client docs (bug 1107252).
    if (!mPresContext->Document()->IsSVGGlyphsDocument()) {
      MOZ_ASSERT(!mActiveTimer,
                 "image doc refresh driver should never have its own timer");
      return;
    }
  }

  // We got here because we're either adjusting the time *or* we're
  // starting it for the first time.  Add to the right timer,
  // prehaps removing it from a previously-set one.
  RefreshDriverTimer* newTimer = ChooseTimer();
  if (newTimer != mActiveTimer) {
    if (mActiveTimer) {
      mActiveTimer->RemoveRefreshDriver(this);
    }
    mActiveTimer = newTimer;
    mActiveTimer->AddRefreshDriver(this);

    if (!mHasStartedTimerAtLeastOnce) {
      mHasStartedTimerAtLeastOnce = true;
      if (profiler_thread_is_being_profiled_for_markers()) {
        nsCString text = "initial timer start "_ns;
        if (mPresContext->Document()->GetDocumentURI()) {
          text.Append(nsContentUtils::TruncatedURLForDisplay(
              mPresContext->Document()->GetDocumentURI()));
        }

        PROFILER_MARKER_TEXT("nsRefreshDriver", LAYOUT,
                             MarkerOptions(MarkerInnerWindowIdFromDocShell(
                                 GetDocShell(mPresContext))),
                             text);
      }
    }

    // If the timer has ticked since we last ticked, consider doing a 'catch-up'
    // tick immediately.
    if (CanDoCatchUpTick()) {
      RefPtr<nsRefreshDriver> self = this;
      NS_DispatchToCurrentThreadQueue(
          NS_NewRunnableFunction(
              "RefreshDriver::EnsureTimerStarted::catch-up",
              [self]() -> void {
                // Re-check if we can still do a catch-up, in case anything
                // changed while the runnable was pending.
                if (self->CanDoCatchUpTick()) {
                  LOG("[%p] Doing catch up tick", self.get());
                  self->Tick(self->mActiveTimer->MostRecentRefreshVsyncId(),
                             self->mActiveTimer->MostRecentRefresh());
                }
              }),
          EventQueuePriority::Vsync);
    }
  }

  // Since the different timers are sampled at different rates, when switching
  // timers, the most recent refresh of the new timer may be *before* the
  // most recent refresh of the old timer.
  // If we are restoring the refresh driver from test control, the time is
  // expected to go backwards (see bug 1043078), otherwise we just keep the most
  // recent tick of this driver (which may be older than the most recent tick of
  // the timer).
  if (!(aFlags & eAllowTimeToGoBackwards)) {
    return;
  }

  if (mMostRecentRefresh != mActiveTimer->MostRecentRefresh()) {
    mMostRecentRefresh = mActiveTimer->MostRecentRefresh();
  }
}

void nsRefreshDriver::StopTimer() {
  if (!mActiveTimer) {
    return;
  }

  mActiveTimer->RemoveRefreshDriver(this);
  mActiveTimer = nullptr;
  mRefreshTimerStartedCause = nullptr;
}

uint32_t nsRefreshDriver::ObserverCount() const {
  uint32_t sum = 0;
  for (const ObserverArray& array : mObservers) {
    sum += array.Length();
  }
  sum += mEarlyRunners.Length();
  return sum;
}

bool nsRefreshDriver::HasObservers() const {
  for (const ObserverArray& array : mObservers) {
    if (!array.IsEmpty()) {
      return true;
    }
  }

  return !mEarlyRunners.IsEmpty();
}

void nsRefreshDriver::AppendObserverDescriptionsToString(
    nsACString& aStr) const {
  for (const ObserverArray& array : mObservers) {
    for (const auto& observer : array.EndLimitedRange()) {
      aStr.AppendPrintf("%s [%s], ", observer.mDescription,
                        kFlushTypeNames[observer.mFlushType]);
    }
  }
  if (!mEarlyRunners.IsEmpty()) {
    aStr.AppendPrintf("%zux Early runner, ", mEarlyRunners.Length());
  }
  // Remove last ", "
  aStr.Truncate(aStr.Length() - 2);
}

bool nsRefreshDriver::HasImageRequests() const {
  for (const auto& data : mStartTable.Values()) {
    if (!data->mEntries.IsEmpty()) {
      return true;
    }
  }

  return !mRequests.IsEmpty();
}

auto nsRefreshDriver::GetReasonsToTick() const -> TickReasons {
  TickReasons reasons = TickReasons::None;
  if (HasObservers()) {
    reasons |= TickReasons::HasObservers;
  }
  if (HasImageRequests() && !mThrottled) {
    reasons |= TickReasons::HasImageRequests;
  }
  if (!mRenderingPhasesNeeded.isEmpty()) {
    reasons |= TickReasons::HasPendingRenderingSteps;
  }
  if (mPresContext && mPresContext->IsRoot() &&
      mPresContext->NeedsMoreTicksForUserInput()) {
    reasons |= TickReasons::RootNeedsMoreTicksForUserInput;
  }
  return reasons;
}

void nsRefreshDriver::AppendTickReasonsToString(TickReasons aReasons,
                                                nsACString& aStr) const {
  if (aReasons == TickReasons::None) {
    aStr.AppendLiteral(" <none>");
    return;
  }

  if (aReasons & TickReasons::HasObservers) {
    aStr.AppendLiteral(" HasObservers (");
    AppendObserverDescriptionsToString(aStr);
    aStr.AppendLiteral(")");
  }
  if (aReasons & TickReasons::HasImageRequests) {
    aStr.AppendLiteral(" HasImageAnimations");
  }
  if (aReasons & TickReasons::HasPendingRenderingSteps) {
    aStr.AppendLiteral(" HasPendingRenderingSteps(");
    bool first = true;
    for (auto phase : mRenderingPhasesNeeded) {
      if (!first) {
        aStr.AppendLiteral(", ");
      }
      first = false;
      aStr.Append(sRenderingPhaseNames[size_t(phase)]);
    }
    aStr.AppendLiteral(")");
  }
  if (aReasons & TickReasons::RootNeedsMoreTicksForUserInput) {
    aStr.AppendLiteral(" RootNeedsMoreTicksForUserInput");
  }
}

bool nsRefreshDriver::
    ShouldKeepTimerRunningWhileWaitingForFirstContentfulPaint() {
  // On top level content pages keep the timer running initially so that we
  // paint the page soon enough.
  if (mThrottled || mTestControllingRefreshes || !XRE_IsContentProcess() ||
      !mPresContext->Document()->IsTopLevelContentDocument() ||
      mPresContext->Document()->IsInitialDocument() ||
      gfxPlatform::IsInLayoutAsapMode() ||
      mPresContext->HadFirstContentfulPaint() ||
      mPresContext->Document()->GetReadyStateEnum() ==
          Document::READYSTATE_COMPLETE) {
    return false;
  }
  if (mBeforeFirstContentfulPaintTimerRunningLimit.IsNull()) {
    // Don't let the timer to run forever, so limit to 4s for now.
    mBeforeFirstContentfulPaintTimerRunningLimit =
        TimeStamp::Now() + TimeDuration::FromSeconds(4.0f);
  }

  return TimeStamp::Now() <= mBeforeFirstContentfulPaintTimerRunningLimit;
}

bool nsRefreshDriver::ShouldKeepTimerRunningAfterPageLoad() {
  if (mHasExceededAfterLoadTickPeriod ||
      !StaticPrefs::layout_keep_ticking_after_load_ms() || mThrottled ||
      mTestControllingRefreshes || !XRE_IsContentProcess() ||
      !mPresContext->Document()->IsTopLevelContentDocument() ||
      TaskController::Get()->PendingMainthreadTaskCountIncludingSuspended() ==
          0 ||
      gfxPlatform::IsInLayoutAsapMode()) {
    // Make the next check faster.
    mHasExceededAfterLoadTickPeriod = true;
    return false;
  }

  nsPIDOMWindowInner* innerWindow = mPresContext->Document()->GetInnerWindow();
  if (!innerWindow) {
    return false;
  }
  auto* perf =
      static_cast<PerformanceMainThread*>(innerWindow->GetPerformance());
  if (!perf) {
    return false;
  }
  nsDOMNavigationTiming* timing = perf->GetDOMTiming();
  if (!timing) {
    return false;
  }
  TimeStamp loadend = timing->LoadEventEnd();
  if (!loadend) {
    return false;
  }
  // Keep ticking after the page load for some time.
  const bool retval =
      (loadend + TimeDuration::FromMilliseconds(
                     StaticPrefs::layout_keep_ticking_after_load_ms())) >
      TimeStamp::Now();
  if (!retval) {
    mHasExceededAfterLoadTickPeriod = true;
  }
  return retval;
}

nsRefreshDriver::ObserverArray& nsRefreshDriver::ArrayFor(
    FlushType aFlushType) {
  switch (aFlushType) {
    case FlushType::Event:
      return mObservers[0];
    case FlushType::Style:
      return mObservers[1];
    case FlushType::Display:
      return mObservers[2];
    default:
      MOZ_CRASH("We don't track refresh observers for this flush type");
  }
}

/*
 * nsITimerCallback implementation
 */

void nsRefreshDriver::DoTick() {
  MOZ_ASSERT(!IsFrozen(), "Why are we notified while frozen?");
  MOZ_ASSERT(mPresContext, "Why are we notified after disconnection?");
  MOZ_ASSERT(!nsContentUtils::GetCurrentJSContext(),
             "Shouldn't have a JSContext on the stack");

  if (mTestControllingRefreshes) {
    Tick(VsyncId(), mMostRecentRefresh);
  } else {
    Tick(VsyncId(), TimeStamp::Now());
  }
}

void nsRefreshDriver::MaybeIncreaseMeasuredTicksSinceLoading() {
  if (mPresContext && mPresContext->IsRoot()) {
    mPresContext->MaybeIncreaseMeasuredTicksSinceLoading();
  }
}

void nsRefreshDriver::UpdateRemoteFrameEffects() {
  mPresContext->Document()->UpdateRemoteFrameEffects();
}

static void UpdateAndReduceAnimations(Document& aDocument) {
  for (DocumentTimeline* tl :
       ToTArray<AutoTArray<RefPtr<DocumentTimeline>, 32>>(
           aDocument.Timelines())) {
    tl->WillRefresh();
  }

  if (nsPresContext* pc = aDocument.GetPresContext()) {
    if (pc->EffectCompositor()->NeedsReducing()) {
      pc->EffectCompositor()->ReduceAnimations();
    }
  }
}

void nsRefreshDriver::RunVideoFrameCallbacks(
    const nsTArray<RefPtr<Document>>& aDocs, TimeStamp aNowTime) {
  // For each fully active Document in docs, for each associated video element
  // for that Document, run the video frame request callbacks passing now as the
  // timestamp.
  Maybe<TimeStamp> nextTickHint;
  for (Document* doc : aDocs) {
    nsTArray<RefPtr<HTMLVideoElement>> videoElms;
    doc->TakeVideoFrameRequestCallbacks(videoElms);
    if (videoElms.IsEmpty()) {
      continue;
    }

    DOMHighResTimeStamp timeStamp = 0;
    DOMHighResTimeStamp nextTickTimeStamp = 0;
    if (auto* innerWindow = doc->GetInnerWindow()) {
      if (Performance* perf = innerWindow->GetPerformance()) {
        if (!nextTickHint) {
          nextTickHint = GetNextTickHint();
        }
        timeStamp = perf->TimeStampToDOMHighResForRendering(aNowTime);
        nextTickTimeStamp =
            nextTickHint
                ? perf->TimeStampToDOMHighResForRendering(*nextTickHint)
                : timeStamp;
      }
      // else window is partially torn down already
    }

    AUTO_PROFILER_TRACING_MARKER_INNERWINDOWID(
        "Paint", "requestVideoFrame callbacks", GRAPHICS, doc->InnerWindowID());
    for (const auto& videoElm : videoElms) {
      nsTArray<VideoFrameRequest> callbacks;
      VideoFrameCallbackMetadata metadata;

      // Presentation time is our best estimate of when the video frame was
      // submitted for compositing. Given that we decode frames in advance,
      // this can be most closely estimated as the vsync time (aNowTime), as
      // that is when the compositor samples the ImageHost to get the next
      // frame to present.
      metadata.mPresentationTime = timeStamp;

      // Expected display time is our best estimate of when the video frame we
      // are submitting for compositing this cycle is shown to the user's eye.
      // This will generally be when the next vsync triggers, assuming we do
      // not fall behind on compositing.
      metadata.mExpectedDisplayTime = nextTickTimeStamp;

      // TakeVideoFrameRequestCallbacks is responsible for populating the rest
      // of the metadata fields. If it is not ready, or there has been no
      // change, it will not populate metadata nor yield any callbacks.
      videoElm->TakeVideoFrameRequestCallbacks(aNowTime, nextTickHint, metadata,
                                               callbacks);

      for (auto& callback : callbacks) {
        if (videoElm->IsVideoFrameCallbackCancelled(callback.mHandle)) {
          continue;
        }

        // MOZ_KnownLive is OK, because the stack array frameRequestCallbacks
        // keeps callback alive and the mCallback strong reference can't be
        // mutated by the call.
        LogVideoFrameRequestCallback::Run run(callback.mCallback);
        MOZ_KnownLive(callback.mCallback)->Call(timeStamp, metadata);
      }
    }
  }
}

void nsRefreshDriver::RunFrameRequestCallbacks(
    const nsTArray<RefPtr<Document>>& aDocs, TimeStamp aNowTime) {
  for (Document* doc : aDocs) {
    nsTArray<FrameRequest> callbacks;
    doc->TakeFrameRequestCallbacks(callbacks);
    if (callbacks.IsEmpty()) {
      continue;
    }

    DOMHighResTimeStamp timeStamp = 0;
    RefPtr innerWindow = nsGlobalWindowInner::Cast(doc->GetInnerWindow());
    if (innerWindow) {
      if (Performance* perf = innerWindow->GetPerformance()) {
        timeStamp = perf->TimeStampToDOMHighResForRendering(aNowTime);
      }
      // else window is partially torn down already
    }

    AUTO_PROFILER_TRACING_MARKER_INNERWINDOWID(
        "Paint", "requestAnimationFrame callbacks", GRAPHICS,
        doc->InnerWindowID());
    for (const auto& callback : callbacks) {
      if (doc->IsCanceledFrameRequestCallback(callback.mHandle)) {
        continue;
      }

      CallbackDebuggerNotificationGuard guard(
          innerWindow, DebuggerNotificationType::RequestAnimationFrameCallback);

      // MOZ_KnownLive is OK, because the stack array frameRequestCallbacks
      // keeps callback alive and the mCallback strong reference can't be
      // mutated by the call.
      LogFrameRequestCallback::Run run(callback.mCallback);
      MOZ_KnownLive(callback.mCallback)->Call(timeStamp);
    }
  }
}

void nsRefreshDriver::RunVideoAndFrameRequestCallbacks(TimeStamp aNowTime) {
  const bool tickThrottledFrameRequests = [&] {
    if (mThrottled) {
      // We always tick throttled frame requests if the entire refresh driver is
      // throttled, because in that situation throttled frame requests tick at
      // the same frequency as non-throttled frame requests.
      return true;
    }
    if (aNowTime >= mNextThrottledFrameRequestTick) {
      mNextThrottledFrameRequestTick =
          aNowTime + mThrottledFrameRequestInterval;
      return true;
    }
    return false;
  }();

  if (NS_WARN_IF(!mPresContext)) {
    return;
  }
  bool skippedAnyThrottledDoc = false;
  // Grab all of our documents that can fire frame request callbacks up front.
  AutoTArray<RefPtr<Document>, 8> docs;
  auto ShouldCollect = [&](const Document* aDoc) {
    if (aDoc->IsRenderingSuppressed()) {
      return false;
    }
    if (!aDoc->HasFrameRequestCallbacks()) {
      // TODO(emilio): Consider removing this check to deal with callbacks
      // posted from other documents more per spec... If we do that we also need
      // to tweak the throttling code to not set mRenderingPhasesNeeded below.
      // Check what other engines do too.
      return false;
    }
    if (!tickThrottledFrameRequests && aDoc->ShouldThrottleFrameRequests()) {
      // Skip throttled docs if it's not time to un-throttle them yet.
      skippedAnyThrottledDoc = true;
      return false;
    }
    return true;
  };
  if (ShouldCollect(mPresContext->Document())) {
    docs.AppendElement(mPresContext->Document());
  }
  mPresContext->Document()->CollectDescendantDocuments(
      docs, Document::IncludeSubResources::Yes, ShouldCollect);
  if (skippedAnyThrottledDoc) {
    // FIXME(emilio): It's a bit subtle to just set this here, but matches
    // pre-existing behavior for throttled docs. It seems at least we should
    // EnsureTimerStarted too? But that kinda defeats the throttling, a little
    // bit? For now, preserve behavior.
    mRenderingPhasesNeeded += RenderingPhase::AnimationFrameCallbacks;
  }

  if (docs.IsEmpty()) {
    return;
  }

  RunVideoFrameCallbacks(docs, aNowTime);
  RunFrameRequestCallbacks(docs, aNowTime);
}

static StaticAutoPtr<AutoTArray<RefPtr<Task>, 8>> sPendingIdleTasks;

void nsRefreshDriver::DispatchIdleTaskAfterTickUnlessExists(Task* aTask) {
  if (!sPendingIdleTasks) {
    sPendingIdleTasks = new AutoTArray<RefPtr<Task>, 8>();
  } else {
    if (sPendingIdleTasks->Contains(aTask)) {
      return;
    }
  }

  sPendingIdleTasks->AppendElement(aTask);
}

void nsRefreshDriver::CancelIdleTask(Task* aTask) {
  if (!sPendingIdleTasks) {
    return;
  }

  sPendingIdleTasks->RemoveElement(aTask);

  if (sPendingIdleTasks->IsEmpty()) {
    sPendingIdleTasks = nullptr;
  }
}

bool nsRefreshDriver::TickObserverArray(uint32_t aIdx, TimeStamp aNowTime) {
  MOZ_ASSERT(aIdx < std::size(mObservers));
  for (RefPtr<nsARefreshObserver> obs : mObservers[aIdx].EndLimitedRange()) {
    obs->WillRefresh(aNowTime);

    if (!mPresContext || !mPresContext->GetPresShell()) {
      return false;
    }
  }
  return true;
}

void nsRefreshDriver::Tick(VsyncId aId, TimeStamp aNowTime,
                           IsExtraTick aIsExtraTick /* = No */) {
  MOZ_ASSERT(!nsContentUtils::GetCurrentJSContext(),
             "Shouldn't have a JSContext on the stack");

  // We're either frozen or we were disconnected (likely in the middle
  // of a tick iteration).  Just do nothing here, since our
  // prescontext went away.
  if (IsFrozen() || !mPresContext) {
    return;
  }

  // We can have a race condition where the vsync timestamp
  // is before the most recent refresh due to a forced refresh.
  // The underlying assumption is that the refresh driver tick can only
  // go forward in time, not backwards. To prevent the refresh
  // driver from going back in time, just skip this tick and
  // wait until the next tick.
  // If this is an 'extra' tick, then we expect it to be using the same
  // vsync id and timestamp as the original tick, so also allow those.
  if ((aNowTime <= mMostRecentRefresh) && !mTestControllingRefreshes &&
      aIsExtraTick == IsExtraTick::No) {
    return;
  }
  auto cleanupInExtraTick = MakeScopeExit([&] { mInNormalTick = false; });
  mInNormalTick = aIsExtraTick != IsExtraTick::Yes;

  if (!IsPresentingInVR() && IsWaitingForPaint(aNowTime)) {
    // In immersive VR mode, we do not get notifications when frames are
    // presented, so we do not wait for the compositor in that mode.

    // We're currently suspended waiting for earlier Tick's to
    // be completed (on the Compositor). Mark that we missed the paint
    // and keep waiting.
    PROFILER_MARKER_UNTYPED(
        "RefreshDriverTick waiting for paint", GRAPHICS,
        MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext)));
    return;
  }

  const TimeStamp previousRefresh = mMostRecentRefresh;
  mMostRecentRefresh = aNowTime;

  if (mRootRefresh) {
    mRootRefresh->RemoveRefreshObserver(this, FlushType::Style);
    mRootRefresh = nullptr;
  }
  mSkippedPaints = false;

  RefPtr<PresShell> presShell = mPresContext->GetPresShell();
  if (!presShell) {
    StopTimer();
    return;
  }

  TickReasons tickReasons = GetReasonsToTick();
  if (tickReasons == TickReasons::None) {
    // We no longer have any observers.
    // Discard composition payloads because there is no paint.
    mCompositionPayloads.Clear();

    // We don't want to stop the timer when observers are initially
    // removed, because sometimes observers can be added and removed
    // often depending on what other things are going on and in that
    // situation we don't want to thrash our timer.  So instead we
    // wait until we get a Notify() call when we have no observers
    // before stopping the timer.
    // On top level content pages keep the timer running initially so that we
    // paint the page soon enough.
    if (ShouldKeepTimerRunningWhileWaitingForFirstContentfulPaint()) {
      PROFILER_MARKER(
          "RefreshDriverTick waiting for first contentful paint", GRAPHICS,
          MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext)), Tracing,
          "Paint");
    } else if (ShouldKeepTimerRunningAfterPageLoad()) {
      PROFILER_MARKER(
          "RefreshDriverTick after page load", GRAPHICS,
          MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext)), Tracing,
          "Paint");
    } else {
      StopTimer();
    }
    return;
  }

  AUTO_PROFILER_LABEL_RELEVANT_FOR_JS("RefreshDriver tick", LAYOUT);

  nsAutoCString profilerStr;
  if (profiler_thread_is_being_profiled_for_markers()) {
    profilerStr.AppendLiteral("Tick reasons:");
    AppendTickReasonsToString(tickReasons, profilerStr);
  }
  AUTO_PROFILER_MARKER_TEXT(
      "RefreshDriverTick", GRAPHICS,
      MarkerOptions(
          MarkerStack::TakeBacktrace(std::move(mRefreshTimerStartedCause)),
          MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext))),
      profilerStr);

  mResizeSuppressed = false;

  bool oldInRefresh = mInRefresh;
  auto restoreInRefresh = MakeScopeExit([&] { mInRefresh = oldInRefresh; });
  mInRefresh = true;

  AutoRestore<TimeStamp> restoreTickStart(mTickStart);
  mTickStart = TimeStamp::Now();
  mTickVsyncId = aId;
  mTickVsyncTime = aNowTime;

  gfxPlatform::GetPlatform()->SchedulePaintIfDeviceReset();

  FlushForceNotifyContentfulPaintPresContext();

  AutoTArray<nsCOMPtr<nsIRunnable>, 16> earlyRunners = std::move(mEarlyRunners);
  for (auto& runner : earlyRunners) {
    runner->Run();
    // Early runners might destroy this pres context.
    if (!mPresContext || !mPresContext->GetPresShell()) {
      return StopTimer();
    }
  }

  // Dispatch coalesced input events.
  if (!TickObserverArray(0, aNowTime)) {
    return StopTimer();
  }

  // Notify style flush observers.
  if (!TickObserverArray(1, aNowTime)) {
    return StopTimer();
  }

  // Check if running the microtask checkpoint above caused the pres context to
  // be destroyed.
  if (!mPresContext || !mPresContext->GetPresShell()) {
    return StopTimer();
  }

  // Step 7. For each doc of docs, flush autofocus candidates for doc if its
  // node navigable is a top-level traversable.
  // NOTE(emilio): Docs with autofocus candidates must be the top-level.
  RunRenderingPhase(
      RenderingPhase::FlushAutoFocusCandidates,
      [](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        MOZ_KnownLive(aDoc).FlushAutoFocusCandidates();
      },
      [](const Document& aDoc) { return aDoc.HasAutoFocusCandidates(); });

  // Step 8. For each doc of docs, run the resize steps for doc.
  RunRenderingPhase(RenderingPhase::ResizeSteps,
                    [](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                      if (RefPtr<PresShell> ps = aDoc.GetPresShell()) {
                        ps->RunResizeSteps();
                      }
                    });

  // Step 9. For each doc of docs, run the scroll steps for doc.
  RunRenderingPhase(RenderingPhase::ScrollSteps,
                    [](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                      if (RefPtr<PresShell> ps = aDoc.GetPresShell()) {
                        ps->RunScrollSteps();
                      }
                    });

  // Step 10. For each doc of docs, evaluate media queries and report changes
  // for doc.
  // https://drafts.csswg.org/cssom-view/#evaluate-media-queries-and-report-changes
  RunRenderingPhase(
      RenderingPhase::EvaluateMediaQueriesAndReportChanges,
      [&](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        MOZ_KnownLive(aDoc).EvaluateMediaQueriesAndReportChanges();
      });

  // Step 11. For each doc of docs, update animations and send events for doc.
  RunRenderingPhase(RenderingPhase::UpdateAnimationsAndSendEvents,
                    [&](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                      if (aDoc.HasAnimationController()) {
                        RefPtr controller = aDoc.GetAnimationController();
                        controller->WillRefresh(aNowTime);
                      }

                      {
                        // Animation updates may queue Promise resolution
                        // microtasks. We shouldn't run these, however, until we
                        // have fully updated the animation state. As per the
                        // "update animations and send events" procedure[1], we
                        // should remove replaced animations and then run these
                        // microtasks before dispatching the corresponding
                        // animation events.
                        //
                        // [1]:
                        // https://drafts.csswg.org/web-animations-1/#update-animations-and-send-events
                        nsAutoMicroTask mt;
                        UpdateAndReduceAnimations(aDoc);
                      }
                      if (RefPtr pc = aDoc.GetPresContext()) {
                        RefPtr dispatcher = pc->AnimationEventDispatcher();
                        dispatcher->DispatchEvents();
                      }
                    });

  // Step 12. For each doc of docs, run the fullscreen steps for doc.
  RunRenderingPhase(RenderingPhase::FullscreenSteps,
                    [&](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                      MOZ_KnownLive(aDoc).RunFullscreenSteps();
                    });

  // TODO: Step 13. For each doc of docs, if the user agent detects that the
  // backing storage associated with a CanvasRenderingContext2D or an
  // OffscreenCanvasRenderingContext2D, context, has been lost, then it must run
  // the context lost steps for each such context.

  // Step 13.5. (https://wicg.github.io/video-rvfc/#video-rvfc-procedures):
  //
  //   For each fully active Document in docs, for each associated video element
  //   for that Document, run the video frame request callbacks passing now as
  //   the timestamp.
  //
  // Step 14. For each doc of docs, run the animation frame callbacks for doc,
  // passing in the relative high resolution time given frameTimestamp and doc's
  // relevant global object as the timestamp.
  RunRenderingPhaseLegacy(RenderingPhase::AnimationFrameCallbacks,
                          [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                            RunVideoAndFrameRequestCallbacks(aNowTime);
                          });

  MaybeIncreaseMeasuredTicksSinceLoading();

  if (!mPresContext || !mPresContext->GetPresShell()) {
    return StopTimer();
  }

  if (mRenderingPhasesNeeded.contains(RenderingPhase::Layout)) {
    mNeedToRecomputeVisibility = true;
    // Layout changes can cause intersection observers to need updates.
    mRenderingPhasesNeeded += RenderingPhase::UpdateIntersectionObservations;
  }

  // Steps 16 and 17.
  RunRenderingPhase(
      RenderingPhase::Layout,
      [](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        MOZ_KnownLive(aDoc)
            .DetermineProximityToViewportAndNotifyResizeObservers();
      });
  if (MOZ_UNLIKELY(!mPresContext || !mPresContext->GetPresShell())) {
    return StopTimer();
  }

  // Update any popups that may need to be moved or hidden due to their
  // anchor changing.
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    pm->UpdatePopupPositions(this);
  }

  // Recompute approximate frame visibility if it's necessary and enough time
  // has passed since the last time we did it.
  if (mNeedToRecomputeVisibility && !mThrottled &&
      aNowTime >= mNextRecomputeVisibilityTick &&
      !presShell->IsPaintingSuppressed()) {
    mNextRecomputeVisibilityTick = aNowTime + mMinRecomputeVisibilityInterval;
    mNeedToRecomputeVisibility = false;
    presShell->ScheduleApproximateFrameVisibilityUpdateNow();
  }

  // Step 18: For each doc of docs, perform pending transition operations for
  // doc.
  RunRenderingPhase(
      RenderingPhase::ViewTransitionOperations,
      [](Document& aDoc) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        MOZ_KnownLive(aDoc).PerformPendingViewTransitionOperations();
      });

  // Step 19. For each doc of docs, run the update intersection observations
  // steps for doc.
  RunRenderingPhase(
      RenderingPhase::UpdateIntersectionObservations,
      [&](Document& aDoc) { aDoc.UpdateIntersections(aNowTime); });

  // Notify display flush observers (like a11y).
  if (!TickObserverArray(2, aNowTime)) {
    return StopTimer();
  }

  UpdateAnimatedImages(previousRefresh, aNowTime);

  bool painted = false;
  RunRenderingPhaseLegacy(
      RenderingPhase::Paint,
      [&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA { painted = PaintIfNeeded(); });

  if (!painted) {
    // No paint happened, discard composition payloads.
    mCompositionPayloads.Clear();
    mPaintCause = nullptr;
  }

  if (MOZ_UNLIKELY(!mPresContext || !mPresContext->GetPresShell())) {
    return StopTimer();
  }

  // This needs to happen after DL building since we rely on the raster scales
  // being stored in nsSubDocumentFrame.
  UpdateRemoteFrameEffects();

#ifndef ANDROID /* bug 1142079 */
  mozilla::glean::layout::refresh_driver_tick.AccumulateRawDuration(
      TimeStamp::Now() - mTickStart);
#endif

  for (nsAPostRefreshObserver* observer :
       mPostRefreshObservers.ForwardRange()) {
    observer->DidRefresh();
  }

  NS_ASSERTION(mInRefresh, "Still in refresh");

  if (mPresContext->IsRoot() && XRE_IsContentProcess() &&
      StaticPrefs::gfx_content_always_paint()) {
    SchedulePaint();
  }

  if (painted && sPendingIdleTasks) {
    UniquePtr<AutoTArray<RefPtr<Task>, 8>> tasks(sPendingIdleTasks.forget());
    for (RefPtr<Task>& taskWithDelay : *tasks) {
      TaskController::Get()->AddTask(taskWithDelay.forget());
    }
  }
}

bool nsRefreshDriver::PaintIfNeeded() {
  if (mThrottled) {
    return false;
  }
  if (IsPresentingInVR()) {
    // Skip the paint in immersive VR mode because whatever we paint here will
    // not end up on the screen. The screen is displaying WebGL content from a
    // single canvas in that mode.
    return false;
  }
  if (mPresContext->Document()->IsRenderingSuppressed()) {
    // If the top level document is suppressed, skip painting altogether.
    // TODO(emilio): Deal with this properly for subdocuments.
    return false;
  }
  nsCString transactionId;
  if (profiler_thread_is_being_profiled_for_markers()) {
    transactionId.AppendLiteral("Transaction ID: ");
    transactionId.AppendInt((uint64_t)mNextTransactionId);
  }
  AUTO_PROFILER_MARKER_TEXT(
      "ViewManagerFlush", GRAPHICS,
      MarkerOptions(MarkerInnerWindowIdFromDocShell(GetDocShell(mPresContext)),
                    MarkerStack::TakeBacktrace(std::move(mPaintCause))),
      transactionId);

  // Forward our composition payloads to the layer manager.
  if (!mCompositionPayloads.IsEmpty()) {
    nsCOMPtr<nsIWidget> widget = mPresContext->GetRootWidget();
    WindowRenderer* renderer = widget ? widget->GetWindowRenderer() : nullptr;
    if (renderer && renderer->AsWebRender()) {
      renderer->AsWebRender()->RegisterPayloads(
          std::move(mCompositionPayloads));
    }
    mCompositionPayloads.Clear();
  }
  RefPtr<nsViewManager> vm = mPresContext->PresShell()->GetViewManager();
  {
    PaintTelemetry::AutoRecordPaint record;
    vm->ProcessPendingUpdates();
  }
  return true;
}

void nsRefreshDriver::UpdateAnimatedImages(TimeStamp aPreviousRefresh,
                                           TimeStamp aNowTime) {
  if (mThrottled) {
    // Don't do this when throttled, as the compositor might be paused and we
    // don't want to queue a lot of paints, see bug 1828587.
    return;
  }
  // Perform notification to imgIRequests subscribed to listen for refresh
  // events.
  for (const auto& entry : mStartTable) {
    const uint32_t& delay = entry.GetKey();
    ImageStartData* data = entry.GetWeak();

    if (data->mEntries.IsEmpty()) {
      continue;
    }

    if (data->mStartTime) {
      TimeStamp& start = *data->mStartTime;

      if (aPreviousRefresh >= start && aNowTime >= start) {
        TimeDuration prev = aPreviousRefresh - start;
        TimeDuration curr = aNowTime - start;
        uint32_t prevMultiple = uint32_t(prev.ToMilliseconds()) / delay;

        // We want to trigger images' refresh if we've just crossed over a
        // multiple of the first image's start time. If so, set the animation
        // start time to the nearest multiple of the delay and move all the
        // images in this table to the main requests table.
        if (prevMultiple != uint32_t(curr.ToMilliseconds()) / delay) {
          mozilla::TimeStamp desired =
              start + TimeDuration::FromMilliseconds(prevMultiple * delay);
          BeginRefreshingImages(data->mEntries, desired);
        }
      } else {
        // Sometimes the start time can be in the future if we spin a nested
        // event loop and re-entrantly tick. In that case, setting the
        // animation start time to the start time seems like the least bad
        // thing we can do.
        mozilla::TimeStamp desired = start;
        BeginRefreshingImages(data->mEntries, desired);
      }
    } else {
      // This is the very first time we've drawn images with this time delay.
      // Set the animation start time to "now" and move all the images in this
      // table to the main requests table.
      mozilla::TimeStamp desired = aNowTime;
      BeginRefreshingImages(data->mEntries, desired);
      data->mStartTime.emplace(aNowTime);
    }
  }

  if (!mRequests.IsEmpty()) {
    // RequestRefresh may run scripts, so it's not safe to directly call it
    // while using a hashtable enumerator to enumerate mRequests in case
    // script modifies the hashtable. Instead, we build a (local) array of
    // images to refresh, and then we refresh each image in that array.
    nsTArray<nsCOMPtr<imgIContainer>> imagesToRefresh(mRequests.Count());

    for (const auto& req : mRequests) {
      nsCOMPtr<imgIContainer> image;
      if (NS_SUCCEEDED(req->GetImage(getter_AddRefs(image)))) {
        imagesToRefresh.AppendElement(image.forget());
      }
    }

    for (const auto& image : imagesToRefresh) {
      image->RequestRefresh(aNowTime);
    }
  }
}

void nsRefreshDriver::BeginRefreshingImages(RequestTable& aEntries,
                                            mozilla::TimeStamp aDesired) {
  for (const auto& req : aEntries) {
    mRequests.Insert(req);

    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(req->GetImage(getter_AddRefs(image)))) {
      image->SetAnimationStartTime(aDesired);
    }
  }
  aEntries.Clear();
}

void nsRefreshDriver::Freeze() {
  StopTimer();
  mFreezeCount++;
}

void nsRefreshDriver::Thaw() {
  NS_ASSERTION(mFreezeCount > 0, "Thaw() called on an unfrozen refresh driver");

  if (mFreezeCount > 0) {
    mFreezeCount--;
  }

  if (mFreezeCount == 0 && HasReasonsToTick()) {
    // FIXME: This isn't quite right, since our EnsureTimerStarted call
    // updates our mMostRecentRefresh, but the DoRefresh call won't run
    // and notify our observers until we get back to the event loop.
    // Thus MostRecentRefresh() will lie between now and the DoRefresh.
    RefPtr<nsRunnableMethod<nsRefreshDriver>> event = NewRunnableMethod(
        "nsRefreshDriver::DoRefresh", this, &nsRefreshDriver::DoRefresh);
    if (nsPresContext* pc = GetPresContext()) {
      pc->Document()->Dispatch(event.forget());
      EnsureTimerStarted();
    } else {
      NS_ERROR("Thawing while document is being destroyed");
    }
  }
}

void nsRefreshDriver::FinishedWaitingForTransaction() {
  if (mSkippedPaints && !IsInRefresh() && HasReasonsToTick() &&
      CanDoCatchUpTick()) {
    NS_DispatchToCurrentThreadQueue(
        NS_NewRunnableFunction(
            "nsRefreshDriver::FinishedWaitingForTransaction",
            [self = RefPtr{this}]() {
              if (self->CanDoCatchUpTick()) {
                self->Tick(self->mActiveTimer->MostRecentRefreshVsyncId(),
                           self->mActiveTimer->MostRecentRefresh());
              }
            }),
        EventQueuePriority::Vsync);
  }
  mWaitingForTransaction = false;
  mSkippedPaints = false;
}

mozilla::layers::TransactionId nsRefreshDriver::GetTransactionId(
    bool aThrottle) {
  mNextTransactionId = mNextTransactionId.Next();
  LOG("[%p] Allocating transaction id %" PRIu64, this, mNextTransactionId.mId);

  // If this a paint from within a normal tick, and the caller hasn't explicitly
  // asked for it to skip being throttled, then record this transaction as
  // pending and maybe disable painting until some transactions are processed.
  if (aThrottle && mInNormalTick) {
    mPendingTransactions.AppendElement(mNextTransactionId);
    if (TooManyPendingTransactions() && !mWaitingForTransaction &&
        !mTestControllingRefreshes) {
      LOG("[%p] Hit max pending transaction limit, entering wait mode", this);
      mWaitingForTransaction = true;
      mSkippedPaints = false;
    }
  }

  return mNextTransactionId;
}

mozilla::layers::TransactionId nsRefreshDriver::LastTransactionId() const {
  return mNextTransactionId;
}

void nsRefreshDriver::RevokeTransactionId(
    mozilla::layers::TransactionId aTransactionId) {
  MOZ_ASSERT(aTransactionId == mNextTransactionId);
  LOG("[%p] Revoking transaction id %" PRIu64, this, aTransactionId.mId);
  if (AtPendingTransactionLimit() &&
      mPendingTransactions.Contains(aTransactionId) && mWaitingForTransaction) {
    LOG("[%p] No longer over pending transaction limit, leaving wait state",
        this);
    MOZ_ASSERT(!mSkippedPaints,
               "How did we skip a paint when we're in the middle of one?");
    FinishedWaitingForTransaction();
  }

  // Notify the pres context so that it can deliver MozAfterPaint for this
  // id if any caller was expecting it.
  nsPresContext* pc = GetPresContext();
  if (pc) {
    pc->NotifyRevokingDidPaint(aTransactionId);
  }
  // Remove aTransactionId from the set of outstanding transactions since we're
  // no longer waiting on it to be completed, but don't revert
  // mNextTransactionId since we can't use the id again.
  mPendingTransactions.RemoveElement(aTransactionId);
}

void nsRefreshDriver::ClearPendingTransactions() {
  LOG("[%p] ClearPendingTransactions", this);
  mPendingTransactions.Clear();
  mWaitingForTransaction = false;
}

void nsRefreshDriver::ResetInitialTransactionId(
    mozilla::layers::TransactionId aTransactionId) {
  mNextTransactionId = aTransactionId;
}

mozilla::TimeStamp nsRefreshDriver::GetTransactionStart() { return mTickStart; }

VsyncId nsRefreshDriver::GetVsyncId() { return mTickVsyncId; }

mozilla::TimeStamp nsRefreshDriver::GetVsyncStart() { return mTickVsyncTime; }

void nsRefreshDriver::NotifyTransactionCompleted(
    mozilla::layers::TransactionId aTransactionId) {
  LOG("[%p] Completed transaction id %" PRIu64, this, aTransactionId.mId);
  mPendingTransactions.RemoveElement(aTransactionId);
  if (mWaitingForTransaction && !TooManyPendingTransactions()) {
    LOG("[%p] No longer over pending transaction limit, leaving wait state",
        this);
    FinishedWaitingForTransaction();
  }
}

void nsRefreshDriver::WillRefresh(mozilla::TimeStamp aTime) {
  mRootRefresh->RemoveRefreshObserver(this, FlushType::Style);
  mRootRefresh = nullptr;
  if (mSkippedPaints) {
    DoRefresh();
  }
}

bool nsRefreshDriver::IsWaitingForPaint(mozilla::TimeStamp aTime) {
  if (mTestControllingRefreshes) {
    return false;
  }

  if (mWaitingForTransaction) {
    LOG("[%p] Over max pending transaction limit when trying to paint, "
        "skipping",
        this);
    mSkippedPaints = true;
    return true;
  }

  // Try find the 'root' refresh driver for the current window and check
  // if that is waiting for a paint.
  nsPresContext* pc = GetPresContext();
  nsPresContext* rootContext = pc ? pc->GetRootPresContext() : nullptr;
  if (rootContext) {
    nsRefreshDriver* rootRefresh = rootContext->RefreshDriver();
    if (rootRefresh && rootRefresh != this) {
      if (rootRefresh->IsWaitingForPaint(aTime)) {
        if (mRootRefresh != rootRefresh) {
          if (mRootRefresh) {
            mRootRefresh->RemoveRefreshObserver(this, FlushType::Style);
          }
          rootRefresh->AddRefreshObserver(this, FlushType::Style,
                                          "Waiting for paint");
          mRootRefresh = rootRefresh;
        }
        mSkippedPaints = true;
        return true;
      }
    }
  }
  return false;
}

void nsRefreshDriver::SetActivity(bool aIsActive) {
  const bool shouldThrottle = !aIsActive;
  if (mThrottled == shouldThrottle) {
    return;
  }
  mThrottled = shouldThrottle;
  if (mActiveTimer || GetReasonsToTick() != TickReasons::None) {
    // We want to switch our timer type here, so just stop and restart the
    // timer.
    EnsureTimerStarted(eForceAdjustTimer);
  }
}

nsPresContext* nsRefreshDriver::GetPresContext() const { return mPresContext; }

void nsRefreshDriver::DoRefresh() {
  // Don't do a refresh unless we're in a state where we should be refreshing.
  if (!IsFrozen() && mPresContext && mActiveTimer) {
    DoTick();
  }
}

#ifdef DEBUG
bool nsRefreshDriver::IsRefreshObserver(nsARefreshObserver* aObserver,
                                        FlushType aFlushType) {
  ObserverArray& array = ArrayFor(aFlushType);
  return array.Contains(aObserver);
}
#endif

void nsRefreshDriver::SchedulePaint() {
  NS_ASSERTION(mPresContext && mPresContext->IsRoot(),
               "Should only schedule view manager flush on root prescontexts");
  if (!mPaintCause) {
    mPaintCause = profiler_capture_backtrace();
  }
  ScheduleRenderingPhase(RenderingPhase::Paint);
  EnsureTimerStarted();
}

/* static */
TimeStamp nsRefreshDriver::GetIdleDeadlineHint(TimeStamp aDefault,
                                               IdleCheck aCheckType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aDefault.IsNull());

  // For computing idleness of refresh drivers we only care about
  // sRegularRateTimerList, since we consider refresh drivers attached to
  // sThrottledRateTimer to be inactive. This implies that tasks
  // resulting from a tick on the sRegularRateTimer counts as being
  // busy but tasks resulting from a tick on sThrottledRateTimer
  // counts as being idle.
  if (sRegularRateTimer) {
    TimeStamp retVal = sRegularRateTimer->GetIdleDeadlineHint(aDefault);
    if (retVal != aDefault) {
      return retVal;
    }
  }

  TimeStamp hint = TimeStamp();
  if (sRegularRateTimerList) {
    for (RefreshDriverTimer* timer : *sRegularRateTimerList) {
      TimeStamp newHint = timer->GetIdleDeadlineHint(aDefault);
      if (newHint < aDefault && (hint.IsNull() || newHint < hint)) {
        hint = newHint;
      }
    }
  }

  if (!hint.IsNull()) {
    return hint;
  }

  if (aCheckType == IdleCheck::AllVsyncListeners && XRE_IsParentProcess()) {
    Maybe<TimeDuration> maybeRate =
        mozilla::gfx::VsyncSource::GetFastestVsyncRate();
    if (maybeRate.isSome()) {
      TimeDuration minIdlePeriod =
          TimeDuration::FromMilliseconds(StaticPrefs::idle_period_min());
      TimeDuration layoutIdleLimit = TimeDuration::FromMilliseconds(
          StaticPrefs::layout_idle_period_time_limit());
      TimeDuration rate = *maybeRate - layoutIdleLimit;

      // If the rate is very short, don't let it affect idle processing in the
      // parent process too much.
      rate = std::max(rate, minIdlePeriod + minIdlePeriod);

      TimeStamp newHint = TimeStamp::Now() + rate;
      if (newHint < aDefault) {
        return newHint;
      }
    }
  }

  return aDefault;
}

/* static */
Maybe<TimeStamp> nsRefreshDriver::GetNextTickHint() {
  MOZ_ASSERT(NS_IsMainThread());

  if (sRegularRateTimer) {
    return sRegularRateTimer->GetNextTickHint();
  }

  Maybe<TimeStamp> hint = Nothing();
  if (sRegularRateTimerList) {
    for (RefreshDriverTimer* timer : *sRegularRateTimerList) {
      if (Maybe<TimeStamp> newHint = timer->GetNextTickHint()) {
        if (!hint || newHint.value() < hint.value()) {
          hint = newHint;
        }
      }
    }
  }
  return hint;
}

/* static */
bool nsRefreshDriver::IsRegularRateTimerTicking() {
  MOZ_ASSERT(NS_IsMainThread());

  if (sRegularRateTimer) {
    if (sRegularRateTimer->IsTicking()) {
      return true;
    }
  }

  if (sRegularRateTimerList) {
    for (RefreshDriverTimer* timer : *sRegularRateTimerList) {
      if (timer->IsTicking()) {
        return true;
      }
    }
  }

  return false;
}

void nsRefreshDriver::Disconnect() {
  MOZ_ASSERT(NS_IsMainThread());

  StopTimer();

  mEarlyRunners.Clear();

  if (mPresContext) {
    mPresContext = nullptr;
    if (--sRefreshDriverCount == 0) {
      Shutdown();
    }
  }
}

#undef LOG
