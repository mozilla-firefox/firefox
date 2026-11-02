/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EventTokenBucket.h"

#include "mozilla/Components.h"
#include "nsICancelable.h"
#include "nsIIOService.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsSocketTransportService2.h"
#include "nsWeakReference.h"

#ifdef DEBUG
#  include "MainThreadUtils.h"
#endif

#ifdef XP_WIN
#  include <mmsystem.h>
#  include <windows.h>
#endif

namespace mozilla {
namespace net {

////////////////////////////////////////////
// EventTokenBucketCancelable
////////////////////////////////////////////

class TokenBucketCancelable : public nsICancelable,
                              public nsSupportsWeakReference {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICANCELABLE

  explicit TokenBucketCancelable(class ATokenBucketEvent* event);
  void Fire();

 private:
  virtual ~TokenBucketCancelable() = default;

  friend class EventTokenBucket;
  // Strong, so a queued event cannot be destroyed before it is fired or
  // canceled.
  RefPtr<ATokenBucketEvent> mEvent;
};

NS_IMPL_ISUPPORTS(TokenBucketCancelable, nsICancelable,
                  nsISupportsWeakReference)

TokenBucketCancelable::TokenBucketCancelable(ATokenBucketEvent* event)
    : mEvent(event) {}

NS_IMETHODIMP
TokenBucketCancelable::Cancel(nsresult reason) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mEvent = nullptr;
  return NS_OK;
}

void TokenBucketCancelable::Fire() {
  if (!mEvent) return;

  RefPtr<ATokenBucketEvent> event = std::move(mEvent);
  event->OnTokenBucketAdmitted();
}

////////////////////////////////////////////
// EventTokenBucket
////////////////////////////////////////////

NS_IMPL_ISUPPORTS(EventTokenBucket, nsITimerCallback, nsINamed)

// by default 1hz with no burst
EventTokenBucket::EventTokenBucket(uint32_t eventsPerSecond, uint32_t burstSize)
    : mUnitCost(kUsecPerSec),
      mMaxCredit(kUsecPerSec),
      mCredit(kUsecPerSec),
      mPaused(false),
      mStopped(false),
      mTimerArmed(false)
#ifdef XP_WIN
      ,
      mFineGrainTimerInUse(false),
      mFineGrainResetTimerArmed(false)
#endif
{
  mLastUpdate = TimeStamp::Now();

  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;
  nsCOMPtr<nsIEventTarget> sts;
  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  if (NS_SUCCEEDED(rv)) {
    sts = mozilla::components::SocketTransport::Service(&rv);
  }
  if (NS_SUCCEEDED(rv)) mTimer = NS_NewTimer(sts);
  SetRate(eventsPerSecond, burstSize);
}

EventTokenBucket::~EventTokenBucket() {
  SOCKET_LOG(
      ("EventTokenBucket::dtor %p events=%zu\n", this, mEvents.GetSize()));

  CleanupTimers();

  // Complete any queued events to prevent hangs
  while (mEvents.GetSize()) {
    RefPtr<TokenBucketCancelable> cancelable = mEvents.PopFront();
    cancelable->Fire();
  }
}

void EventTokenBucket::CleanupTimers() {
  if (mTimer && mTimerArmed) {
    mTimer->Cancel();
  }
  mTimer = nullptr;
  mTimerArmed = false;

#ifdef XP_WIN
  NormalTimers();
  if (mFineGrainResetTimer && mFineGrainResetTimerArmed) {
    mFineGrainResetTimer->Cancel();
  }
  mFineGrainResetTimer = nullptr;
  mFineGrainResetTimerArmed = false;
#endif
}

void EventTokenBucket::SetRate(uint32_t eventsPerSecond, uint32_t burstSize) {
  SOCKET_LOG(("EventTokenBucket::SetRate %p %u %u\n", this, eventsPerSecond,
              burstSize));

  if (eventsPerSecond > kMaxHz) {
    eventsPerSecond = kMaxHz;
    SOCKET_LOG(("  eventsPerSecond out of range\n"));
  }

  if (!eventsPerSecond) {
    eventsPerSecond = 1;
    SOCKET_LOG(("  eventsPerSecond out of range\n"));
  }

  mUnitCost = kUsecPerSec / eventsPerSecond;
  mMaxCredit = mUnitCost * burstSize;
  if (mMaxCredit > kUsecPerSec * 60 * 15) {
    SOCKET_LOG(("  burstSize out of range\n"));
    mMaxCredit = kUsecPerSec * 60 * 15;
  }
  mCredit = mMaxCredit;
  mLastUpdate = TimeStamp::Now();
}

void EventTokenBucket::ClearCredits() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::ClearCredits %p\n", this));
  mCredit = 0;
}

uint32_t EventTokenBucket::BurstEventsAvailable() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return static_cast<uint32_t>(mCredit / mUnitCost);
}

uint32_t EventTokenBucket::QueuedEvents() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return mEvents.GetSize();
}

void EventTokenBucket::Pause() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::Pause %p\n", this));
  if (mPaused || mStopped) return;

  mPaused = true;
  if (mTimerArmed) {
    mTimer->Cancel();
    mTimerArmed = false;
  }
}

void EventTokenBucket::UnPause() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::UnPause %p\n", this));
  if (!mPaused || mStopped) return;

  mPaused = false;
  DispatchEvents();
  UpdateTimer();
}

void EventTokenBucket::Stop() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::Stop %p armed=%d\n", this, mTimerArmed));
  mStopped = true;
  CleanupTimers();

  // Complete any queued events to prevent hangs
  while (mEvents.GetSize()) {
    RefPtr<TokenBucketCancelable> cancelable = mEvents.PopFront();
    cancelable->Fire();
  }
}

nsresult EventTokenBucket::SubmitEvent(ATokenBucketEvent* event,
                                       nsICancelable** cancelable) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::SubmitEvent %p\n", this));

  if (mStopped || !mTimer) return NS_ERROR_FAILURE;

  UpdateCredits();

  RefPtr<TokenBucketCancelable> cancelEvent = new TokenBucketCancelable(event);
  // When this function exits the cancelEvent needs 2 references, one for the
  // mEvents queue and one for the caller of SubmitEvent()

  *cancelable = do_AddRef(cancelEvent).take();

  if (mPaused || !TryImmediateDispatch(cancelEvent.get())) {
    // queue it
    SOCKET_LOG(("   queued\n"));
    mEvents.Push(cancelEvent.forget());
    UpdateTimer();
  } else {
    SOCKET_LOG(("   dispatched synchronously\n"));
  }

  return NS_OK;
}

bool EventTokenBucket::TryImmediateDispatch(TokenBucketCancelable* cancelable) {
  if (mCredit < mUnitCost) return false;

  mCredit -= mUnitCost;
  cancelable->Fire();
  return true;
}

void EventTokenBucket::DispatchEvents() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  SOCKET_LOG(("EventTokenBucket::DispatchEvents %p %d\n", this, mPaused));
  if (mPaused || mStopped) return;

  while (mEvents.GetSize() && mUnitCost <= mCredit) {
    RefPtr<TokenBucketCancelable> cancelable = mEvents.PopFront();
    if (cancelable->mEvent) {
      SOCKET_LOG(
          ("EventTokenBucket::DispachEvents [%p] "
           "Dispatching queue token bucket event cost=%" PRIu64
           " credit=%" PRIu64 "\n",
           this, mUnitCost, mCredit));
      mCredit -= mUnitCost;
      cancelable->Fire();
    }
  }

#ifdef XP_WIN
  if (!mEvents.GetSize()) WantNormalTimers();
#endif
}

void EventTokenBucket::UpdateTimer() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mTimerArmed || mPaused || mStopped || !mEvents.GetSize() || !mTimer) {
    return;
  }

  if (mCredit >= mUnitCost) return;

  // determine the time needed to wait to accumulate enough credits to admit
  // one more event and set the timer for that point. Always round it
  // up because firing early doesn't help.
  //
  uint64_t deficit = mUnitCost - mCredit;
  uint64_t msecWait = (deficit + (kUsecPerMsec - 1)) / kUsecPerMsec;

  if (msecWait < 4) {  // minimum wait
    msecWait = 4;
  } else if (msecWait > 60000) {  // maximum wait
    msecWait = 60000;
  }

#ifdef XP_WIN
  FineGrainTimers();
#endif

  SOCKET_LOG(
      ("EventTokenBucket::UpdateTimer %p for %" PRIu64 "ms\n", this, msecWait));
  nsresult rv = mTimer->InitWithCallback(this, static_cast<uint32_t>(msecWait),
                                         nsITimer::TYPE_ONE_SHOT);
  mTimerArmed = NS_SUCCEEDED(rv);
}

NS_IMETHODIMP
EventTokenBucket::Notify(nsITimer* timer) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

#ifdef XP_WIN
  if (timer == mFineGrainResetTimer) {
    FineGrainResetTimerNotify();
    return NS_OK;
  }
#endif

  SOCKET_LOG(("EventTokenBucket::Notify() %p\n", this));
  mTimerArmed = false;
  if (mStopped) return NS_OK;

  UpdateCredits();
  DispatchEvents();
  UpdateTimer();

  return NS_OK;
}

NS_IMETHODIMP
EventTokenBucket::GetName(nsACString& aName) {
  aName.AssignLiteral("EventTokenBucket");
  return NS_OK;
}

void EventTokenBucket::UpdateCredits() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  TimeStamp now = TimeStamp::Now();
  TimeDuration elapsed = now - mLastUpdate;
  mLastUpdate = now;

  mCredit += static_cast<uint64_t>(elapsed.ToMicroseconds());
  if (mCredit > mMaxCredit) mCredit = mMaxCredit;
  SOCKET_LOG(("EventTokenBucket::UpdateCredits %p to %" PRIu64 " (%" PRIu64
              " each.. %3.2f)\n",
              this, mCredit, mUnitCost, (double)mCredit / mUnitCost));
}

#ifdef XP_WIN
void EventTokenBucket::FineGrainTimers() {
  SOCKET_LOG(("EventTokenBucket::FineGrainTimers %p mFineGrainTimerInUse=%d\n",
              this, mFineGrainTimerInUse));

  mLastFineGrainTimerUse = TimeStamp::Now();

  if (mFineGrainTimerInUse) return;

  if (mUnitCost > kCostFineGrainThreshold) return;

  SOCKET_LOG(
      ("EventTokenBucket::FineGrainTimers %p timeBeginPeriod()\n", this));

  mFineGrainTimerInUse = true;
  timeBeginPeriod(1);
}

void EventTokenBucket::NormalTimers() {
  if (!mFineGrainTimerInUse) return;
  mFineGrainTimerInUse = false;

  SOCKET_LOG(("EventTokenBucket::NormalTimers %p timeEndPeriod()\n", this));
  timeEndPeriod(1);
}

void EventTokenBucket::WantNormalTimers() {
  if (!mFineGrainTimerInUse) return;
  if (mFineGrainResetTimerArmed) return;

  TimeDuration elapsed(TimeStamp::Now() - mLastFineGrainTimerUse);
  static const TimeDuration fiveSeconds = TimeDuration::FromSeconds(5);

  if (elapsed >= fiveSeconds) {
    NormalTimers();
    return;
  }

  if (!mFineGrainResetTimer) mFineGrainResetTimer = NS_NewTimer();

  // if we can't delay the reset, just do it now
  if (!mFineGrainResetTimer) {
    NormalTimers();
    return;
  }

  // pad the callback out 100ms to avoid having to round trip this again if the
  // timer calls back just a tad early.
  SOCKET_LOG(
      ("EventTokenBucket::WantNormalTimers %p "
       "Will reset timer granularity after delay",
       this));

  mFineGrainResetTimer->InitWithCallback(
      this,
      static_cast<uint32_t>((fiveSeconds - elapsed).ToMilliseconds()) + 100,
      nsITimer::TYPE_ONE_SHOT);
  mFineGrainResetTimerArmed = true;
}

void EventTokenBucket::FineGrainResetTimerNotify() {
  SOCKET_LOG(("EventTokenBucket::FineGrainResetTimerNotify(%p) events = %zd\n",
              this, mEvents.GetSize()));
  mFineGrainResetTimerArmed = false;

  // If we are currently processing events then wait for the queue to drain
  // before trying to reset back to normal timers again
  if (!mEvents.GetSize()) WantNormalTimers();
}

#endif

}  // namespace net
}  // namespace mozilla
