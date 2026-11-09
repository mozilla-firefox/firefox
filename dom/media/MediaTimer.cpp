/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaTimer.h"

#include <type_traits>

#include "mozilla/AwakeTimeStamp.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SharedThreadPool.h"
#include "nsComponentManagerUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {

template <typename T>
MediaTimer<T>::MediaTimer(bool aFuzzy)
    : mMonitor("MediaTimer Monitor"),
      mCreationTimeStamp(T::Now()),
      mUpdateScheduled(false),
      mFuzzy(aFuzzy) {
  TIMER_LOG("MediaTimer::MediaTimer");

  // Use the SharedThreadPool to create an nsIThreadPool with a maximum of one
  // thread, which is equivalent to an nsIThread for our purposes.
  RefPtr<SharedThreadPool> threadPool(SharedThreadPool::Get("MediaTimer", 1));
  mThread = threadPool.get();
  mTimer = NS_NewTimer(mThread);
}

template <typename T>
void MediaTimer<T>::DispatchDestroy() {
  // Hold a strong reference to the thread so that it doesn't get deleted in
  // Destroy(), which may run completely before the stack if Dispatch() begins
  // to unwind.
  nsCOMPtr<nsIEventTarget> thread = mThread;
  nsresult rv =
      thread->Dispatch(NewNonOwningRunnableMethod("MediaTimer::Destroy", this,
                                                  &MediaTimer::Destroy),
                       NS_DISPATCH_NORMAL);
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
  (void)rv;
}

// Runs on the timer thread.
template <typename T>
void MediaTimer<T>::Destroy() {
  MOZ_ASSERT(OnMediaTimerThread());
  TIMER_LOG("MediaTimer::Destroy");

  // Reject any outstanding entries.
  {
    MonitorAutoLock lock(mMonitor);
    Reject();

    // Cancel the timer if necessary.
    CancelTimerIfArmed();
  }

  delete this;
}

template <typename T>
bool MediaTimer<T>::OnMediaTimerThread() {
  bool rv = false;
  mThread->IsOnCurrentThread(&rv);
  return rv;
}

template <typename T>
RefPtr<MediaTimerPromise> MediaTimer<T>::WaitFor(
    const typename T::DurationType& aDuration, StaticString aCallSite) {
  return WaitUntil(T::Now() + aDuration, aCallSite);
}

template <typename T>
RefPtr<MediaTimerPromise> MediaTimer<T>::WaitUntil(const T& aTimeStamp,
                                                   StaticString aCallSite) {
  MonitorAutoLock mon(mMonitor);
  TIMER_LOG("MediaTimer::WaitUntil {}", RelativeMicroseconds(aTimeStamp));
  Entry e(aTimeStamp, aCallSite);
  RefPtr<MediaTimerPromise> p = e.mPromise.get();
  mEntries.push(e);
  ScheduleUpdate();
  return p;
}

template <typename T>
void MediaTimer<T>::Cancel() {
  MonitorAutoLock mon(mMonitor);
  TIMER_LOG("MediaTimer::Cancel");
  Reject();
}

template <typename T>
void MediaTimer<T>::ScheduleUpdate() {
  mMonitor.AssertCurrentThreadOwns();
  if (mUpdateScheduled) {
    return;
  }
  mUpdateScheduled = true;

  nsresult rv = mThread->Dispatch(
      NewRunnableMethod("MediaTimer::Update", this, &MediaTimer::Update),
      NS_DISPATCH_NORMAL);
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
  (void)rv;
}

template <typename T>
void MediaTimer<T>::Update() {
  MonitorAutoLock mon(mMonitor);
  UpdateLocked();
}

template <typename T>
bool MediaTimer<T>::IsExpired(const T& aTarget, const T& aNow) {
  MOZ_ASSERT(OnMediaTimerThread());
  mMonitor.AssertCurrentThreadOwns();
  // Treat this timer as expired in fuzzy mode even if it is fired
  // slightly (< 1ms) before the schedule target. So we don't need to schedule
  // a timer with very small timeout again when the client doesn't need a
  // high-res timer.
  T t = mFuzzy ? aTarget - T::DurationType::FromMilliseconds(1) : aTarget;
  return t <= aNow;
}

template <typename T>
void MediaTimer<T>::UpdateLocked() {
  MOZ_ASSERT(OnMediaTimerThread());
  mMonitor.AssertCurrentThreadOwns();
  mUpdateScheduled = false;

  TIMER_LOG("MediaTimer::UpdateLocked");

  // Resolve all the promises whose time is up.
  T now = T::Now();
  while (!mEntries.empty() && IsExpired(mEntries.top().mTimeStamp, now)) {
    mEntries.top().mPromise->Resolve(true, __func__);
    DebugOnly<T> poppedTimeStamp = mEntries.top().mTimeStamp;
    mEntries.pop();
    MOZ_ASSERT_IF(!mEntries.empty(),
                  *&poppedTimeStamp <= mEntries.top().mTimeStamp);
  }

  // If we've got no more entries, cancel any pending timer and bail out.
  if (mEntries.empty()) {
    CancelTimerIfArmed();
    return;
  }

  // We've got more entries - (re)arm the timer for the soonest one.
  if (!TimerIsArmed() ||
      mEntries.top().mTimeStamp < mCurrentTimerTarget.value()) {
    CancelTimerIfArmed();
    ArmTimer(mEntries.top().mTimeStamp, now);
  }
}

template <typename T>
void MediaTimer<T>::Reject() {
  mMonitor.AssertCurrentThreadOwns();
  while (!mEntries.empty()) {
    mEntries.top().mPromise->Reject(false, __func__);
    mEntries.pop();
  }
}

template <typename T>
/* static */ void MediaTimer<T>::TimerCallback(nsITimer* aTimer,
                                               void* aClosure) {
  static_cast<MediaTimer<T>*>(aClosure)->TimerFired();
}

template <typename T>
void MediaTimer<T>::TimerFired() {
  MonitorAutoLock mon(mMonitor);
  MOZ_ASSERT(OnMediaTimerThread());
  mCurrentTimerTarget = Nothing();
  UpdateLocked();
}

template <typename T>
void MediaTimer<T>::ArmTimer(const T& aTarget, const T& aNow) {
  MOZ_DIAGNOSTIC_ASSERT(!TimerIsArmed());
  MOZ_DIAGNOSTIC_ASSERT(aTarget > aNow);

  const typename T::DurationType delay = aTarget - aNow;
  TIMER_LOG("MediaTimer::ArmTimer delay={:.3f}ms", delay.ToMilliseconds());
  mCurrentTimerTarget.emplace(aTarget);
  TimeDuration duration;
  if constexpr (std::is_same_v<typename T::DurationType, TimeDuration>) {
    duration = delay;
  } else {
    duration = TimeDuration::FromMicroseconds(delay.ToMicroseconds());
  }
  MOZ_ALWAYS_SUCCEEDS(mTimer->InitHighResolutionWithNamedFuncCallback(
      &TimerCallback, this, duration, nsITimer::TYPE_ONE_SHOT,
      "MediaTimer::TimerCallback"_ns));
}

template <typename T>
bool MediaTimer<T>::TimerIsArmed() {
  return mCurrentTimerTarget.isSome();
}

template <typename T>
void MediaTimer<T>::CancelTimerIfArmed() {
  MOZ_ASSERT(OnMediaTimerThread());
  if (TimerIsArmed()) {
    TIMER_LOG("MediaTimer::CancelTimerIfArmed canceling timer");
    mTimer->Cancel();
    mCurrentTimerTarget = Nothing();
  }
}

template class MediaTimer<AwakeTimeStamp>;
template class MediaTimer<TimeStamp>;

}  // namespace mozilla
