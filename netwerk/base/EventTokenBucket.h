/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NetEventTokenBucket_h_
#define NetEventTokenBucket_h_

#include "ARefBase.h"
#include "mozilla/TimeStamp.h"
#include "nsCOMPtr.h"
#include "nsDeque.h"
#include "nsINamed.h"
#include "nsITimer.h"

class nsICancelable;

namespace mozilla {
namespace net {

/* A token bucket is used to govern the maximum rate a series of events
   can be executed at. For instance if your event was "eat a piece of cake"
   then a token bucket configured to allow "1 piece per day" would spread
   the eating of a 8 piece cake over 8 days even if you tried to eat the
   whole thing up front. In a practical sense it 'costs' 1 token to execute
   an event and tokens are 'earned' at a particular rate as time goes by.

   The token bucket can be perfectly smooth or allow a configurable amount of
   burstiness. A bursty token bucket allows you to save up unused credits, while
   a perfectly smooth one would not. A smooth "1 per day" cake token bucket
   would require 9 days to eat that cake if you skipped a slice on day 4
   (use the token or lose it), while a token bucket configured with a burst
   of 2 would just let you eat 2 slices on day 5 (the credits for day 4 and day
   5) and finish the cake in the usual 8 days.

   EventTokenBucket(hz=20, burst=5) creates a token bucket with the following
   properties:

  + events from an infinite stream will be admitted 20 times per second (i.e.
    hz=20 means 1 event per 50 ms). Timers will be used to space things evenly
    down to 5ms gaps (i.e. up to 200hz). Token buckets with rates greater than
    200hz will admit multiple events with 5ms gaps between them. 10000hz is the
    maximum rate and 1hz is the minimum rate.

  + The burst size controls the limit of 'credits' that a token bucket can
    accumulate when idle. For our (20,5) example each event requires 50ms of
    credit (again, 20hz = 50ms per event). a burst size of 5 means that the
    token bucket can accumulate a maximum of 250ms (5 * 50ms) for this bucket.
    If no events have been admitted for the last full second the bucket can
    still only accumulate 250ms of credit - but that credit means that 5 events
    can be admitted without delay. A burst size of 1 is the minimum.  The
    EventTokenBucket is created with maximum credits already applied, but they
    can be cleared with the ClearCredits() method. The maximum burst size is 15
    minutes worth of events.

  + An event is submitted to the token bucket asynchronously through
    SubmitEvent().  The OnTokenBucketAdmitted() method of the submitted event
    is used as a callback when the event is ready to run. A cancelable event is
    returned to the SubmitEvent() caller for use in the case they do not wish
    to wait for the callback.
*/

class EventTokenBucket;

class ATokenBucketEvent {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING
  virtual void OnTokenBucketAdmitted() = 0;
};

class TokenBucketCancelable;

class EventTokenBucket : public nsITimerCallback,
                         public nsINamed,
                         public ARefBase {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  // This should be constructed on the main thread
  EventTokenBucket(uint32_t eventsPerSecond, uint32_t burstSize);

  // These public methods are all meant to be called from the socket thread
  void ClearCredits();
  uint32_t BurstEventsAvailable();
  uint32_t QueuedEvents();

  // a paused token bucket will not process any events, but it will accumulate
  // credits. ClearCredits can be used before unpausing if desired.
  void Pause();
  void UnPause();
  void Stop();

  // The returned cancelable event can only be canceled from the socket thread
  nsresult SubmitEvent(ATokenBucketEvent* event, nsICancelable** cancelable);

 private:
  virtual ~EventTokenBucket();
  void CleanupTimers();

  friend class RunNotifyEvent;
  friend class SetTimerEvent;

  bool TryImmediateDispatch(TokenBucketCancelable* cancelable);
  void SetRate(uint32_t eventsPerSecond, uint32_t burstSize);

  void DispatchEvents();
  void UpdateTimer();
  void UpdateCredits();

  const static uint64_t kUsecPerSec = 1000000;
  const static uint64_t kUsecPerMsec = 1000;
  const static uint64_t kMaxHz = 10000;

  uint64_t
      mUnitCost;  // usec of credit needed for 1 event (from eventsPerSecond)
  uint64_t mMaxCredit;  // usec mCredit limit (from busrtSize)
  uint64_t mCredit;     // usec of accumulated credit.

  bool mPaused;
  bool mStopped;
  nsRefPtrDeque<TokenBucketCancelable> mEvents;
  bool mTimerArmed;
  TimeStamp mLastUpdate;

  // The timer is created on the main thread, but is armed and executes Notify()
  // callbacks on the socket thread in order to maintain low latency of event
  // delivery.
  nsCOMPtr<nsITimer> mTimer;

#ifdef XP_WIN
  // Windows timers are 15ms granularity by default. When we have active events
  // that need to be dispatched at 50ms  or less granularity we change the OS
  // granularity to 1ms. 90 seconds after that need has elapsed we will change
  // it back
  const static uint64_t kCostFineGrainThreshold = 50 * kUsecPerMsec;

  void FineGrainTimers();   // get 1ms granularity
  void NormalTimers();      // reset to default granularity
  void WantNormalTimers();  // reset after 90 seconds if not needed in interim
  void FineGrainResetTimerNotify();  // delayed callback to reset

  TimeStamp mLastFineGrainTimerUse;
  bool mFineGrainTimerInUse;
  bool mFineGrainResetTimerArmed;
  nsCOMPtr<nsITimer> mFineGrainResetTimer;
#endif
};

}  // namespace net
}  // namespace mozilla

#endif
