/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CCGCScheduler_h
#define mozilla_dom_CCGCScheduler_h

#include "js/SliceBudget.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/IdleTaskRunner.h"
#include "mozilla/MainThreadIdlePeriod.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/ipc/IdleSchedulerChild.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollector.h"
#include "nsJSEnvironment.h"

namespace mozilla {

extern const TimeDuration kOneMinute;

// The amount of time we wait between a request to CC (after GC ran)
// and doing the actual CC.
extern const TimeDuration kCCDelay;

extern const TimeDuration kCCSkippableDelay;

// In case the cycle collector isn't run at all, we don't want forget skippables
// to run too often. So limit the forget skippable cycle to start at earliest 2
// seconds after the end of the previous cycle.
extern const TimeDuration kTimeBetweenForgetSkippableCycles;

// ForgetSkippable is usually fast, so we can use small budgets.
// This isn't a real budget but a hint to IdleTaskRunner whether there
// is enough time to call ForgetSkippable.
extern const TimeDuration kForgetSkippableSliceDuration;

// Maximum amount of time that should elapse between incremental CC slices
extern const TimeDuration kICCIntersliceDelay;

// Time budget for an incremental CC slice when using timer to run it.
extern const TimeDuration kICCSliceBudget;
// Minimum budget for an incremental CC slice when using idle time to run it.
extern const TimeDuration kIdleICCSliceBudget;

// Maximum total duration for an ICC
extern const TimeDuration kMaxICCDuration;

// Force a CC after this long if there's more than NS_CC_FORCED_PURPLE_LIMIT
// objects in the purple buffer.
extern const TimeDuration kCCForced;
constexpr uint32_t kCCForcedPurpleLimit = 10;

// Don't allow an incremental GC to lock out the CC for too long.
extern const TimeDuration kMaxCCLockedoutTime;

// Trigger a CC if the purple buffer exceeds this size when we check it.
constexpr uint32_t kCCPurpleLimit = 200;

// Actions performed by the GCRunner state machine.
enum class GCRunnerAction {
  MinorGC,        // Run a minor GC (nursery collection)
  WaitToMajorGC,  // We want to start a new major GC
  StartMajorGC,   // The parent says we may begin our major GC
  GCSlice,        // Run a single slice of a major GC
  None
};

struct GCRunnerStep {
  GCRunnerAction mAction;
  JS::GCReason mReason;
};

// Actions that are output from the CCRunner state machine.
enum class CCRunnerAction {
  // Do nothing.
  None,

  // We crossed an eager minor GC threshold in the middle of an incremental CC,
  // and we have some idle time.
  MinorGC,

  // Various cleanup actions.
  ForgetSkippable,
  CleanupContentUnbinder,
  CleanupDeferred,

  // Do the actual cycle collection (build the graph etc).
  CycleCollect,

  // All done.
  StopRunning
};

enum CCRunnerYield { Continue, Yield };

enum CCRunnerForgetSkippableRemoveChildless {
  KeepChildless = false,
  RemoveChildless = true
};

struct CCRunnerStep {
  // The action the scheduler is instructing the caller to perform.
  CCRunnerAction mAction;

  // Whether to stop processing actions for this invocation of the timer
  // callback.
  CCRunnerYield mYield;

  union ActionData {
    // If the action is ForgetSkippable, then whether to remove childless nodes
    // or not.
    CCRunnerForgetSkippableRemoveChildless mRemoveChildless;

    // If the action is CycleCollect, the reason for the collection.
    CCReason mCCReason;

    // If the action is MinorGC, the reason for the GC.
    JS::GCReason mReason;

    MOZ_IMPLICIT ActionData(CCRunnerForgetSkippableRemoveChildless v)
        : mRemoveChildless(v) {}
    MOZ_IMPLICIT ActionData(CCReason v) : mCCReason(v) {}
    MOZ_IMPLICIT ActionData(JS::GCReason v) : mReason(v) {}
    ActionData() = default;
  } mParam;
};

class CCGCScheduler {
 public:
  CCGCScheduler()
      : mAskParentBeforeMajorGC(XRE_IsContentProcess()),
        mReadyForMajorGC(!mAskParentBeforeMajorGC),
        mInterruptRequested(false) {}

  bool CCRunnerFired(TimeStamp aDeadline);

  // Parameter setting

  void SetActiveIntersliceGCBudget(TimeDuration aDuration) {
    mActiveIntersliceGCBudget = aDuration;
  }

  // State retrieval

  TimeDuration GetCCBlockedTime(TimeStamp aNow) const {
    MOZ_ASSERT(mInIncrementalGC);
    MOZ_ASSERT(!mCCBlockStart.IsNull());
    return aNow - mCCBlockStart;
  }

  bool InIncrementalGC() const { return mInIncrementalGC; }

  TimeStamp GetLastCCEndTime() const { return mLastCCEndTime; }

  bool IsEarlyForgetSkippable(uint32_t aN = kMajorForgetSkippableCalls) const {
    return mCleanupsSinceLastGC < aN;
  }

  bool NeedsFullGC() const { return mNeedsFullGC; }

  // Requests
  void PokeGC(JS::GCReason aReason, JSObject* aObj, TimeDuration aDelay = {});
  void PokeShrinkingGC();
  void PokeFullGC();
  void MaybePokeCC(TimeStamp aNow, uint32_t aSuspectedCCObjects);
  void PokeMinorGC(JS::GCReason aReason);

  void UserIsInactive();
  void UserIsActive();
  bool IsUserActive() const { return mUserIsActive; }

  void KillShrinkingGCTimer();
  void KillFullGCTimer();
  void KillGCRunner();
  void KillCCRunner();
  void KillAllTimersAndRunners();

  JS::SliceBudget CreateGCSliceBudget(mozilla::TimeDuration aDuration,
                                      bool isIdle, bool isExtended) {
    mInterruptRequested = false;
    // Don't try to interrupt if we are in a mode where idle time is rare.
    auto budget = JS::SliceBudget(
        aDuration, mPreferFasterCollection ? nullptr : &mInterruptRequested);
    budget.idle = isIdle;
    budget.extended = isExtended;
    return budget;
  }

  /*
   * aDelay is the delay before the first time the idle task runner runs.
   * Then it runs every
   * StaticPrefs::javascript_options_gc_delay_interslice()
   */
  void EnsureGCRunner(TimeDuration aDelay = {});

  // If GCRunner isn't active, this calls EnsureGCRunner(). Otherwise the timer
  // is reset.
  void EnsureOrResetGCRunner();

  void EnsureCCRunner(TimeDuration aDelay, TimeDuration aBudget);

  // State modification

  void SetNeedsFullGC(bool aNeedGC = true) { mNeedsFullGC = aNeedGC; }

  void SetWantMajorGC(JS::GCReason aReason) {
    MOZ_ASSERT(aReason != JS::GCReason::NO_REASON);

    // If the GC being requested is not a shrinking GC set this flag.
    // If/when the shrinking GC timer fires but the user is active we check
    // this flag before canceling the GC, so as not to cancel the
    // non-shrinking GC being requested here.
    if (aReason != JS::GCReason::USER_INACTIVE) {
      mWantAtLeastRegularGC = true;
    }

    // Force full GCs when called from reftests so that we collect dead zones
    // that have not been scheduled for collection.
    if (aReason == JS::GCReason::DOM_WINDOW_UTILS) {
      SetNeedsFullGC();
    }

    // USER_INACTIVE trumps everything,
    // FULL_GC_TIMER trumps everything except USER_INACTIVE,
    // all other reasons just use the latest reason.
    switch (aReason) {
      case JS::GCReason::USER_INACTIVE:
        mMajorGCReason = aReason;
        break;
      case JS::GCReason::FULL_GC_TIMER:
        if (mMajorGCReason != JS::GCReason::USER_INACTIVE) {
          mMajorGCReason = aReason;
        }
        break;
      default:
        if (mMajorGCReason != JS::GCReason::USER_INACTIVE &&
            mMajorGCReason != JS::GCReason::FULL_GC_TIMER) {
          mMajorGCReason = aReason;
        }
        break;
    }
  }

  void SetWantEagerMinorGC(JS::GCReason aReason) {
    if (mEagerMinorGCReason == JS::GCReason::NO_REASON) {
      mEagerMinorGCReason = aReason;
    }
  }

  // Ensure that the current runner does a cycle collection, and trigger a GC
  // after it finishes.
  void EnsureCCThenGC(CCReason aReason) {
    MOZ_ASSERT(mCCRunnerState != CCRunnerState::Inactive);
    MOZ_ASSERT(aReason != CCReason::NO_REASON);
    mNeedsFullCC = aReason;
    mNeedsGCAfterCC = true;
  }

  // Returns false if we started and finished a major GC while waiting for a
  // response.
  [[nodiscard]] bool NoteReadyForMajorGC() {
    if (mMajorGCReason == JS::GCReason::NO_REASON || InIncrementalGC()) {
      return false;
    }
    mReadyForMajorGC = true;
    return true;
  }

  // Starting a major GC (incremental or non-incremental).
  void NoteGCBegin(JS::GCReason aReason);

  // Major GC completed.
  void NoteGCEnd();

  // A timer fired, but then decided not to run a GC.
  void NoteWontGC();

  void NoteMinorGCEnd() { mEagerMinorGCReason = JS::GCReason::NO_REASON; }

  // This is invoked when we reach the actual cycle collection portion of the
  // overall cycle collection.
  void NoteCCBegin();

  // This is invoked when the whole process of collection is done -- i.e., CC
  // preparation (eg ForgetSkippables) in addition to the CC itself. There
  // really ought to be a separate name for the overall CC as opposed to the
  // actual cycle collection portion.
  void NoteCCEnd(const CycleCollectorResults& aResults, TimeStamp aWhen);

  // A single slice has completed.
  void NoteGCSliceEnd(TimeStamp aStart, TimeStamp aEnd);

  bool GCRunnerFired(TimeStamp aDeadline);
  bool GCRunnerFiredDoGC(TimeStamp aDeadline, const GCRunnerStep& aStep);

  using MayGCPromise =
      MozPromise<bool, mozilla::ipc::ResponseRejectReason, true>;

  // Returns null if we shouldn't GC now (eg a GC is already running).
  static RefPtr<MayGCPromise> MayGCNow(JS::GCReason reason);

  // Check all of the various collector timers/runners and see if they are
  // waiting to fire. This does not check the Full GC Timer, as that's a
  // more expensive collection we run on a long timer.
  void RunNextCollectorTimer(JS::GCReason aReason,
                             mozilla::TimeStamp aDeadline);

  // When we decide to do a cycle collection but we're in the middle of an
  // incremental GC, the CC is "locked out" until the GC completes -- unless
  // the wait is too long, and we decide to finish the incremental GC early.
  void BlockCC(TimeStamp aNow) {
    MOZ_ASSERT(mInIncrementalGC);
    MOZ_ASSERT(mCCBlockStart.IsNull());
    mCCBlockStart = aNow;
  }

  void UnblockCC() { mCCBlockStart = TimeStamp(); }

  // Returns the number of purple buffer items that were processed and removed.
  void NoteForgetSkippableComplete(TimeStamp aNow,
                                   uint32_t aSuspectedCCObjects) {
    mLastForgetSkippableEndTime = aNow;
    mPreviousSuspectedCount = aSuspectedCCObjects;
    mCleanupsSinceLastGC++;
  }

  // Test if we are in the NoteCCBegin .. NoteCCEnd interval.
  bool IsCollectingCycles() const { return mIsCollectingCycles; }

  // The CC was abandoned without running a slice, so we only did forget
  // skippables. Prevent running another cycle soon.
  void NoteForgetSkippableOnlyCycle(TimeStamp aNow) {
    mLastForgetSkippableCycleEndTime = aNow;
  }

  void Shutdown() {
    mDidShutdown = true;
    KillAllTimersAndRunners();
  }

  // Scheduling

  // Return a budget along with a boolean saying whether to prefer to run short
  // slices and stop rather than continuing to the next phase of cycle
  // collection.
  JS::SliceBudget ComputeCCSliceBudget(TimeStamp aDeadline,
                                       TimeStamp aCCBeginTime,
                                       TimeStamp aPrevSliceEndTime,
                                       TimeStamp aNow,
                                       bool* aPreferShorterSlices) const;

  JS::SliceBudget ComputeInterSliceGCBudget(TimeStamp aDeadline,
                                            TimeStamp aNow);

  TimeDuration ComputeMinimumBudgetForRunner(TimeDuration aBaseValue);

  bool ShouldForgetSkippable(uint32_t aSuspectedCCObjects) const {
    // Only do a forget skippable if there are more than a few new objects
    // or we're doing the initial forget skippables.
    return ((mPreviousSuspectedCount + 100) <= aSuspectedCCObjects) ||
           mCleanupsSinceLastGC < kMajorForgetSkippableCalls;
  }

  // There is reason to suspect that there may be a significant amount of
  // garbage to cycle collect: either we just finished a GC, or the purple
  // buffer is getting really big, or it's getting somewhat big and it has been
  // too long since the last CC.
  CCReason IsCCNeeded(TimeStamp aNow, uint32_t aSuspectedCCObjects) const {
    if (mNeedsFullCC != CCReason::NO_REASON) {
      return mNeedsFullCC;
    }
    if (aSuspectedCCObjects > kCCPurpleLimit) {
      return CCReason::MANY_SUSPECTED;
    }
    if (aSuspectedCCObjects > kCCForcedPurpleLimit && mLastCCEndTime &&
        aNow - mLastCCEndTime > kCCForced) {
      return CCReason::TIMED;
    }
    return CCReason::NO_REASON;
  }

  mozilla::CCReason ShouldScheduleCC(TimeStamp aNow,
                                     uint32_t aSuspectedCCObjects) const;

  // If we collected a substantial amount of cycles, poke the GC since more
  // objects might be unreachable now.
  bool NeedsGCAfterCC() const {
    return mCCollectedWaitingForGC > 250 || mCCollectedZonesWaitingForGC > 0 ||
           mLikelyShortLivingObjectsNeedingGC > 2500 || mNeedsGCAfterCC;
  }

  bool IsLastEarlyCCTimer(int32_t aCurrentFireCount) const {
    int32_t numEarlyTimerFires =
        std::max(int32_t(mCCDelay / kCCSkippableDelay) - 2, 1);

    return aCurrentFireCount >= numEarlyTimerFires;
  }

  enum class CCRunnerState {
    Inactive,
    ReducePurple,
    CleanupChildless,
    CleanupContentUnbinder,
    CleanupDeferred,
    StartCycleCollection,
    CycleCollecting,
    Canceled,
    NumStates
  };

  void InitCCRunnerStateMachine(CCRunnerState initialState, CCReason aReason) {
    if (mCCRunner) {
      return;
    }

    MOZ_ASSERT(mCCReason == CCReason::NO_REASON);
    mCCReason = aReason;

    // The state machine should always have been deactivated after the previous
    // collection, however far that collection may have gone.
    MOZ_ASSERT(mCCRunnerState == CCRunnerState::Inactive,
               "DeactivateCCRunner should have been called");
    mCCRunnerState = initialState;

    // Currently, there are only two entry points to the non-Inactive part of
    // the state machine.
    if (initialState == CCRunnerState::ReducePurple) {
      mCCDelay = kCCDelay;
      mCCRunnerEarlyFireCount = 0;
    } else if (initialState == CCRunnerState::CycleCollecting) {
      // Nothing needed.
    } else {
      MOZ_CRASH("Invalid initial state");
    }
  }

  void DeactivateCCRunner() {
    mCCRunnerState = CCRunnerState::Inactive;
    mCCReason = CCReason::NO_REASON;
  }

  bool HasMoreIdleGCRunnerWork() const {
    return mMajorGCReason != JS::GCReason::NO_REASON ||
           mEagerMajorGCReason != JS::GCReason::NO_REASON ||
           mEagerMinorGCReason != JS::GCReason::NO_REASON;
  }

  GCRunnerStep GetNextGCRunnerAction(TimeStamp aDeadline) const;

  CCRunnerStep AdvanceCCRunner(TimeStamp aDeadline, TimeStamp aNow,
                               uint32_t aSuspectedCCObjects);

  // aStartTimeStamp : when the ForgetSkippable timer fired. This may be some
  // time ago, if an incremental GC needed to be finished.
  JS::SliceBudget ComputeForgetSkippableBudget(TimeStamp aStartTimeStamp,
                                               TimeStamp aDeadline);

  bool PreferFasterCollection() const { return mPreferFasterCollection; }

 private:
  // State

  // An incremental GC is in progress, which blocks the CC from running for its
  // duration (or until it goes too long and is finished synchronously.)
  bool mInIncrementalGC = false;

  // Whether to ask the parent process if now is a good time to GC (false for
  // the parent process.)
  const bool mAskParentBeforeMajorGC;

  // We've asked the parent process if now is a good time to GC (do not ask
  // again).
  bool mHaveAskedParent = false;

  // The parent process is ready for us to do a major GC.
  bool mReadyForMajorGC;

  // Set when the IdleTaskRunner requests the current task be interrupted.
  // Cleared when the GC slice budget has detected the interrupt request.
  JS::SliceBudget::InterruptRequestFlag mInterruptRequested;

  // When a shrinking GC has been requested but we back-out, if this is true
  // we run a non-shrinking GC.
  bool mWantAtLeastRegularGC = false;

  // When the CC started actually waiting for the GC to finish. This will be
  // set to non-null at a later time than mCCLockedOut.
  TimeStamp mCCBlockStart;

  bool mDidShutdown = false;

  TimeStamp mLastForgetSkippableEndTime;
  uint32_t mForgetSkippableCounter = 0;
  TimeStamp mForgetSkippableFrequencyStartTime;
  TimeStamp mLastCCEndTime;
  TimeStamp mLastForgetSkippableCycleEndTime;

  CCRunnerState mCCRunnerState = CCRunnerState::Inactive;
  int32_t mCCRunnerEarlyFireCount = 0;
  TimeDuration mCCDelay = kCCDelay;

  // Prevent the very first CC from running before we have GC'd and set the
  // gray bits.
  bool mHasRunGC = false;

  mozilla::CCReason mNeedsFullCC = CCReason::NO_REASON;
  bool mNeedsFullGC = true;
  bool mNeedsGCAfterCC = false;
  uint32_t mPreviousSuspectedCount = 0;

  uint32_t mCleanupsSinceLastGC = UINT32_MAX;

  // If the GC runner triggers a GC slice, this will be set to the idle deadline
  // or the null timestamp if non-idle. It will be Nothing at the end of an
  // internally-triggered slice.
  mozilla::Maybe<TimeStamp> mTriggeredGCDeadline;

  RefPtr<IdleTaskRunner> mGCRunner;
  RefPtr<IdleTaskRunner> mCCRunner;
  nsITimer* mShrinkingGCTimer = nullptr;
  nsITimer* mFullGCTimer = nullptr;

  mozilla::CCReason mCCReason = mozilla::CCReason::NO_REASON;
  JS::GCReason mMajorGCReason = JS::GCReason::NO_REASON;
  JS::GCReason mEagerMajorGCReason = JS::GCReason::NO_REASON;
  JS::GCReason mEagerMinorGCReason = JS::GCReason::NO_REASON;

  bool mIsCompactingOnUserInactive = false;
  bool mIsCollectingCycles = false;
  bool mUserIsActive = true;

  bool mCurrentCollectionHasSeenNonIdle = false;
  bool mPreferFasterCollection = false;

 public:
  uint32_t mCCollectedWaitingForGC = 0;
  uint32_t mCCollectedZonesWaitingForGC = 0;
  uint32_t mLikelyShortLivingObjectsNeedingGC = 0;

  // Configuration parameters

  TimeDuration mActiveIntersliceGCBudget = TimeDuration::FromMilliseconds(5);
};

}  // namespace mozilla

#endif  // mozilla_dom_CCGCScheduler_h
