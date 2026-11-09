/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/CCGCScheduler.h"

// This is a test for mozilla::CCGCScheduler.

using namespace mozilla;

MOZ_RUNINIT static TimeDuration kOneSecond = TimeDuration::FromSeconds(1);
MOZ_RUNINIT static TimeDuration kTenthSecond = TimeDuration::FromSeconds(0.1);
MOZ_RUNINIT static TimeDuration kFrameDuration =
    TimeDuration::FromSeconds(1.0 / 60.0);

MOZ_RUNINIT static mozilla::TimeStamp sNow = TimeStamp::Now();

static mozilla::TimeStamp AdvanceTime(TimeDuration aDuration) {
  sNow += aDuration;
  return sNow;
}

static TimeStamp Now() { return sNow; }

static uint32_t sSuspected = 0;

static uint32_t SuspectedCCObjects() { return sSuspected; }
static void SetNumSuspected(uint32_t n) { sSuspected = n; }
static void SuspectMore(uint32_t n) { sSuspected += n; }

using CCRunnerState = mozilla::CCGCScheduler::CCRunnerState;

class TestGC {
 protected:
  CCGCScheduler& mScheduler;

 public:
  explicit TestGC(CCGCScheduler& aScheduler) : mScheduler(aScheduler) {}
  void Run(int aNumSlices);
};

void TestGC::Run(int aNumSlices) {
  // Make the purple buffer nearly empty so it is itself not an adequate reason
  // for wanting a CC.
  static_assert(3 < mozilla::kCCPurpleLimit);
  SetNumSuspected(3);

  // Running the GC should not influence whether a CC is currently seen as
  // needed. But the first time we run GC, it will be false; later, we will
  // have run a GC and set it to true.
  CCReason neededCCAtStartOfGC =
      mScheduler.IsCCNeeded(Now(), SuspectedCCObjects());

  mScheduler.NoteGCBegin(JS::GCReason::API);

  for (int slice = 0; slice < aNumSlices; slice++) {
    EXPECT_TRUE(mScheduler.InIncrementalGC());
    TimeStamp idleDeadline = Now() + kTenthSecond;
    JS::SliceBudget budget =
        mScheduler.ComputeInterSliceGCBudget(idleDeadline, Now());
    TimeDuration budgetDuration = budget.timeBudget();
    EXPECT_NEAR(budgetDuration.ToSeconds(), 0.1, 1.e-6);
    // Pretend the GC took exactly the budget.
    AdvanceTime(budgetDuration);

    EXPECT_EQ(mScheduler.IsCCNeeded(Now(), SuspectedCCObjects()),
              neededCCAtStartOfGC);

    // Mutator runs for 1 second.
    AdvanceTime(kOneSecond);
  }

  mScheduler.NoteGCEnd();
  mScheduler.SetNeedsFullGC(false);
}

class TestCC {
 protected:
  CCGCScheduler& mScheduler;

 public:
  explicit TestCC(CCGCScheduler& aScheduler) : mScheduler(aScheduler) {}

  void Run(int aNumSlices) {
    Prepare();
    MaybePokeCC();
    TimerFires(aNumSlices);
    EndCycleCollectionCallback();
    KillCCRunner();
  }

  virtual void Prepare() = 0;
  virtual void MaybePokeCC();
  virtual void TimerFires(int aNumSlices);
  virtual void RunSlices(int aNumSlices);
  virtual void RunSlice(TimeStamp aCCStartTime, TimeStamp aPrevSliceEnd,
                        int aSliceNum, int aNumSlices) = 0;
  virtual void ForgetSkippable();
  virtual void EndCycleCollectionCallback();
  virtual void KillCCRunner();
};

void TestCC::MaybePokeCC() {
  // nsJSContext::MaybePokeCC

  // In all tests so far, we will be running this just after a GC.
  CCReason reason = mScheduler.ShouldScheduleCC(Now(), SuspectedCCObjects());
  EXPECT_EQ(reason, CCReason::GC_FINISHED);

  mScheduler.InitCCRunnerStateMachine(CCRunnerState::ReducePurple, reason);
  EXPECT_TRUE(mScheduler.IsEarlyForgetSkippable());
}

void TestCC::TimerFires(int aNumSlices) {
  // Series of CCRunner timer fires.
  CCRunnerStep step;

  while (true) {
    SuspectMore(1000);
    TimeStamp idleDeadline = Now() + kOneSecond;
    step =
        mScheduler.AdvanceCCRunner(idleDeadline, Now(), SuspectedCCObjects());
    // Should first see a series of ForgetSkippable actions.
    if (step.mAction != CCRunnerAction::ForgetSkippable ||
        step.mParam.mRemoveChildless != KeepChildless) {
      break;
    }
    EXPECT_EQ(step.mYield, Yield);
    ForgetSkippable();
  }

  while (step.mYield == Continue) {
    TimeStamp idleDeadline = Now() + kOneSecond;
    step =
        mScheduler.AdvanceCCRunner(idleDeadline, Now(), SuspectedCCObjects());
  }
  EXPECT_EQ(step.mAction, CCRunnerAction::ForgetSkippable);
  EXPECT_EQ(step.mParam.mRemoveChildless, RemoveChildless);
  ForgetSkippable();

  TimeStamp idleDeadline = Now() + kOneSecond;
  step = mScheduler.AdvanceCCRunner(idleDeadline, Now(), SuspectedCCObjects());
  EXPECT_EQ(step.mAction, CCRunnerAction::CleanupContentUnbinder);
  step = mScheduler.AdvanceCCRunner(idleDeadline, Now(), SuspectedCCObjects());
  EXPECT_EQ(step.mAction, CCRunnerAction::CleanupDeferred);

  mScheduler.NoteCCBegin();
  RunSlices(aNumSlices);
}

void TestCC::ForgetSkippable() {
  // ...ForgetSkippable would happen here...
  JS::SliceBudget budget =
      mScheduler.ComputeForgetSkippableBudget(Now(), Now() + kTenthSecond);
  EXPECT_NEAR(budget.timeBudget().ToMilliseconds(),
              kTenthSecond.ToMilliseconds(), 1);
  AdvanceTime(kTenthSecond);
  mScheduler.NoteForgetSkippableComplete(Now(), SuspectedCCObjects());
}

void TestCC::RunSlices(int aNumSlices) {
  TimeStamp ccStartTime = Now();
  TimeStamp prevSliceEnd = ccStartTime;
  for (int ccslice = 0; ccslice < aNumSlices; ccslice++) {
    RunSlice(ccStartTime, prevSliceEnd, ccslice, aNumSlices);
    prevSliceEnd = Now();
  }

  SetNumSuspected(0);
}

void TestCC::EndCycleCollectionCallback() {
  // nsJSContext::EndCycleCollectionCallback
  CycleCollectorResults results;
  results.mFreedGCed = 10;
  results.mFreedJSZones = 2;
  mScheduler.NoteCCEnd(results, Now());

  // Because > 0 zones were freed.
  EXPECT_TRUE(mScheduler.NeedsGCAfterCC());
}

void TestCC::KillCCRunner() {
  // nsJSContext::KillCCRunner
  mScheduler.KillCCRunner();
}

class TestIdleCC : public TestCC {
 public:
  explicit TestIdleCC(CCGCScheduler& aScheduler) : TestCC(aScheduler) {}

  void Prepare() override;
  void RunSlice(TimeStamp aCCStartTime, TimeStamp aPrevSliceEnd, int aSliceNum,
                int aNumSlices) override;
};

void TestIdleCC::Prepare() { EXPECT_TRUE(!mScheduler.InIncrementalGC()); }

void TestIdleCC::RunSlice(TimeStamp aCCStartTime, TimeStamp aPrevSliceEnd,
                          int aSliceNum, int aNumSlices) {
  CCRunnerStep step;
  TimeStamp idleDeadline = Now() + kTenthSecond;

  // The scheduler should request a CycleCollect slice.
  step = mScheduler.AdvanceCCRunner(idleDeadline, Now(), SuspectedCCObjects());
  EXPECT_EQ(step.mAction, CCRunnerAction::CycleCollect);

  // nsJSContext::RunCycleCollectorSlice

  EXPECT_FALSE(mScheduler.InIncrementalGC());
  bool preferShorter;
  JS::SliceBudget budget = mScheduler.ComputeCCSliceBudget(
      idleDeadline, aCCStartTime, aPrevSliceEnd, Now(), &preferShorter);
  // The scheduler will set the budget to our deadline (0.1sec in the future).
  EXPECT_NEAR(budget.timeBudget().ToMilliseconds(),
              kTenthSecond.ToMilliseconds(), 1);
  EXPECT_FALSE(preferShorter);

  AdvanceTime(kTenthSecond);
}

class TestNonIdleCC : public TestCC {
 public:
  explicit TestNonIdleCC(CCGCScheduler& aScheduler) : TestCC(aScheduler) {}

  void Prepare() override;
  void RunSlice(TimeStamp aCCStartTime, TimeStamp aPrevSliceEnd, int aSliceNum,
                int aNumSlices) override;
};

void TestNonIdleCC::Prepare() {
  EXPECT_TRUE(!mScheduler.InIncrementalGC());

  // Advance time by an hour to give time for a user event in the past.
  AdvanceTime(TimeDuration::FromSeconds(3600));
}

void TestNonIdleCC::RunSlice(TimeStamp aCCStartTime, TimeStamp aPrevSliceEnd,
                             int aSliceNum, int aNumSlices) {
  CCRunnerStep step;
  TimeStamp nullDeadline;

  // The scheduler should tell us to run a slice of cycle collection.
  step = mScheduler.AdvanceCCRunner(nullDeadline, Now(), SuspectedCCObjects());
  EXPECT_EQ(step.mAction, CCRunnerAction::CycleCollect);

  // nsJSContext::RunCycleCollectorSlice

  EXPECT_FALSE(mScheduler.InIncrementalGC());

  bool preferShorter;
  JS::SliceBudget budget = mScheduler.ComputeCCSliceBudget(
      nullDeadline, aCCStartTime, aPrevSliceEnd, Now(), &preferShorter);
  if (aSliceNum == 0) {
    // First slice of the CC, so always use the baseBudget which is
    // kICCSliceBudget (3ms) for a non-idle slice.
    EXPECT_NEAR(budget.timeBudget().ToMilliseconds(),
                kICCSliceBudget.ToMilliseconds(), 0.1);
  } else if (aSliceNum == 1) {
    // Second slice still uses the baseBudget, since not much time has passed
    // so none of the lengthening mechanisms have kicked in yet.
    EXPECT_NEAR(budget.timeBudget().ToMilliseconds(),
                kICCSliceBudget.ToMilliseconds(), 0.1);
  } else if (aSliceNum == 2) {
    // We're not overrunning kMaxICCDuration, so we don't go unlimited.
    EXPECT_FALSE(budget.isUnlimited());
    // This slice is delayed, slice time should be increased.
    EXPECT_NEAR(budget.timeBudget().ToMilliseconds(),
                MainThreadIdlePeriod::GetLongIdlePeriod() / 2.0, 0.5);
  } else {
    // We're not overrunning kMaxICCDuration, so we don't go unlimited.
    EXPECT_FALSE(budget.isUnlimited());

    // These slices are not delayed, but enough time has passed that the
    // dominating factor is now the linear ramp up to max slice time at the
    // halfway point to kMaxICCDuration.
    EXPECT_TRUE(budget.timeBudget().ToMilliseconds() >
                kICCSliceBudget.ToMilliseconds());
    EXPECT_TRUE(budget.timeBudget().ToMilliseconds() <=
                MainThreadIdlePeriod::GetLongIdlePeriod());
  }
  EXPECT_TRUE(preferShorter);  // Non-idle prefers shorter slices

  AdvanceTime(budget.timeBudget());
  if (aSliceNum == 1) {
    // Delay the third slice (only).
    AdvanceTime(kICCIntersliceDelay * 2);
  }
}

// Do a GC then CC then GC.
static bool BasicScenario(CCGCScheduler& aScheduler, TestGC* aTestGC,
                          TestCC* aTestCC) {
  // Run a 10-slice incremental GC.
  aTestGC->Run(10);

  // After a GC, the scheduler should decide to do a full CC regardless of the
  // number of purple buffer entries.
  SetNumSuspected(3);
  EXPECT_EQ(aScheduler.IsCCNeeded(Now(), SuspectedCCObjects()),
            CCReason::GC_FINISHED);

  // Now we should want to CC.
  EXPECT_EQ(aScheduler.ShouldScheduleCC(Now(), SuspectedCCObjects()),
            CCReason::GC_FINISHED);

  // Do a 5-slice CC.
  aTestCC->Run(5);

  // Not enough suspected objects to deserve a CC.
  EXPECT_EQ(aScheduler.IsCCNeeded(Now(), SuspectedCCObjects()),
            CCReason::NO_REASON);
  EXPECT_EQ(aScheduler.ShouldScheduleCC(Now(), SuspectedCCObjects()),
            CCReason::NO_REASON);
  SetNumSuspected(10000);

  // We shouldn't want to CC again yet, it's too soon.
  EXPECT_EQ(aScheduler.ShouldScheduleCC(Now(), SuspectedCCObjects()),
            CCReason::NO_REASON);
  AdvanceTime(mozilla::kCCDelay);

  // *Now* it's time for another CC.
  EXPECT_EQ(aScheduler.ShouldScheduleCC(Now(), SuspectedCCObjects()),
            CCReason::MANY_SUSPECTED);

  // Run a 3-slice incremental GC.
  EXPECT_TRUE(!aScheduler.InIncrementalGC());
  aTestGC->Run(3);

  return true;
}

MOZ_RUNINIT static CCGCScheduler scheduler;
MOZ_RUNINIT static TestGC gc(scheduler);
MOZ_RUNINIT static TestIdleCC ccIdle(scheduler);
MOZ_RUNINIT static TestNonIdleCC ccNonIdle(scheduler);

TEST(TestScheduler, Idle)
{
  // Cannot CC until we GC once.
  EXPECT_EQ(scheduler.ShouldScheduleCC(Now(), SuspectedCCObjects()),
            CCReason::NO_REASON);

  EXPECT_TRUE(BasicScenario(scheduler, &gc, &ccIdle));
}

TEST(TestScheduler, NonIdle)
{
  EXPECT_TRUE(BasicScenario(scheduler, &gc, &ccNonIdle));
}
