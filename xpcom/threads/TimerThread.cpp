/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTimerImpl.h"
#include "TimerThread.h"

#include "GeckoProfiler.h"
#include "nsThreadUtils.h"

#include "nsIObserverService.h"
#include "nsIPropertyBag2.h"
#include "mozilla/Services.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/ArenaAllocator.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/OperatorNewExtensions.h"
#include "mozilla/StaticPrefs_timer.h"

#include "mozilla/glean/XpcomMetrics.h"

#include <math.h>

using namespace mozilla;

#ifdef XP_WIN
// Include Windows header required for enabling high-precision timers.
#  include <windows.h>
#  include <mmsystem.h>

static constexpr UINT kTimerPeriodHiRes = 1;
static constexpr UINT kTimerPeriodLowRes = 16;

// Helper functions to determine what Windows timer resolution to target.
static constexpr UINT GetDesiredTimerPeriod(const bool aOnBatteryPower,
                                            const bool aLowProcessPriority) {
  const bool useLowResTimer = aOnBatteryPower || aLowProcessPriority;
  return useLowResTimer ? kTimerPeriodLowRes : kTimerPeriodHiRes;
}

static_assert(GetDesiredTimerPeriod(true, false) == kTimerPeriodLowRes);
static_assert(GetDesiredTimerPeriod(false, true) == kTimerPeriodLowRes);
static_assert(GetDesiredTimerPeriod(true, true) == kTimerPeriodLowRes);
static_assert(GetDesiredTimerPeriod(false, false) == kTimerPeriodHiRes);

UINT TimerThread::ComputeDesiredTimerPeriod() const {
  const bool lowPriorityProcess =
      mCachedPriority.load(std::memory_order_relaxed) <
      hal::PROCESS_PRIORITY_FOREGROUND;

  // NOTE: Using short-circuiting here to avoid call to GetSystemPowerStatus()
  // when we know that that result will not affect the final result. (As
  // confirmed by the static_assert's above, onBatteryPower does not affect the
  // result when the lowPriorityProcess is true.)
  SYSTEM_POWER_STATUS status;
  const bool onBatteryPower = !lowPriorityProcess &&
                              GetSystemPowerStatus(&status) &&
                              (status.ACLineStatus == 0);

  return GetDesiredTimerPeriod(onBatteryPower, lowPriorityProcess);
}
#endif

// Uncomment the following line to enable runtime stats during development.
// #define TIMERS_RUNTIME_STATS

#ifdef TIMERS_RUNTIME_STATS
// This class gathers durations and displays some basic stats when destroyed.
// It is intended to be used as a static variable (see `AUTO_TIMERS_STATS`
// below), to display stats at the end of the program.
class StaticTimersStats {
 public:
  explicit StaticTimersStats(const char* aName) : mName(aName) {}

  ~StaticTimersStats() {
    // Using unsigned long long for computations and printfs.
    using ULL = unsigned long long;
    ULL n = static_cast<ULL>(mCount);
    if (n == 0) {
      printf("[%d] Timers stats `%s`: (nothing)\n",
             int(profiler_current_process_id().ToNumber()), mName);
    } else if (ULL sumNs = static_cast<ULL>(mSumDurationsNs); sumNs == 0) {
      printf("[%d] Timers stats `%s`: %llu\n",
             int(profiler_current_process_id().ToNumber()), mName, n);
    } else {
      printf("[%d] Timers stats `%s`: %llu ns / %llu = %llu ns, max %llu ns\n",
             int(profiler_current_process_id().ToNumber()), mName, sumNs, n,
             sumNs / n, static_cast<ULL>(mLongestDurationNs));
    }
  }

  void AddDurationFrom(TimeStamp aStart) {
    // Duration between aStart and now, rounded to the nearest nanosecond.
    DurationNs duration = static_cast<DurationNs>(
        (TimeStamp::Now() - aStart).ToMicroseconds() * 1000 + 0.5);
    mSumDurationsNs += duration;
    ++mCount;
    // Update mLongestDurationNs if this one is longer.
    for (;;) {
      DurationNs longest = mLongestDurationNs;
      if (MOZ_LIKELY(longest >= duration)) {
        // This duration is not the longest, nothing to do.
        break;
      }
      if (MOZ_LIKELY(mLongestDurationNs.compareExchange(longest, duration))) {
        // Successfully updated `mLongestDurationNs` with the new value.
        break;
      }
      // Otherwise someone else just updated `mLongestDurationNs`, we need to
      // try again by looping.
    }
  }

  void AddCount() {
    MOZ_ASSERT(mSumDurationsNs == 0, "Don't mix counts and durations");
    ++mCount;
  }

 private:
  using DurationNs = uint64_t;
  using Count = uint32_t;

  Atomic<DurationNs> mSumDurationsNs{0};
  Atomic<DurationNs> mLongestDurationNs{0};
  Atomic<Count> mCount{0};
  const char* mName;
};

// RAII object that measures its scoped lifetime duration and reports it to a
// `StaticTimersStats`.
class MOZ_RAII AutoTimersStats {
 public:
  explicit AutoTimersStats(StaticTimersStats& aStats)
      : mStats(aStats), mStart(TimeStamp::Now()) {}

  ~AutoTimersStats() { mStats.AddDurationFrom(mStart); }

 private:
  StaticTimersStats& mStats;
  TimeStamp mStart;
};

// Macro that should be used to collect basic statistics from measurements of
// block durations, from where this macro is, until the end of its enclosing
// scope. The name is used in the static variable name and when displaying stats
// at the end of the program; Another location could use the same name but their
// stats will not be combined, so use different name if these locations should
// be distinguished.
#  define AUTO_TIMERS_STATS(name)                  \
    static ::StaticTimersStats sStat##name(#name); \
    ::AutoTimersStats autoStat##name(sStat##name);

// This macro only counts the number of times it's used, not durations.
// Don't mix with AUTO_TIMERS_STATS!
#  define COUNT_TIMERS_STATS(name)                 \
    static ::StaticTimersStats sStat##name(#name); \
    sStat##name.AddCount();

#else  // TIMERS_RUNTIME_STATS

#  define AUTO_TIMERS_STATS(name)
#  define COUNT_TIMERS_STATS(name)

#endif  // TIMERS_RUNTIME_STATS else

NS_IMPL_ISUPPORTS_INHERITED(TimerThread, Runnable, nsIObserver)

TimerThread::TimerThread()
    : Runnable("TimerThread"),
      mInitialized(false),
      mMonitor("TimerThread.mMonitor"),
      mShutdown(false),
      mWaiting(false),
      mNotified(false),
      mSleeping(false),
      mAllowedEarlyFiringMicroseconds(0) {}

TimerThread::~TimerThread() {
  mThread = nullptr;

  NS_ASSERTION(mTimers.IsEmpty(), "Timers remain in TimerThread::~TimerThread");

#if TIMER_THREAD_STATISTICS
  {
    MonitorAutoLock lock(mMonitor);
    PrintStatistics();
  }
#endif
}

namespace {

class TimerObserverRunnable : public Runnable {
 public:
  explicit TimerObserverRunnable(nsIObserver* aObserver)
      : mozilla::Runnable("TimerObserverRunnable"), mObserver(aObserver) {}

  NS_DECL_NSIRUNNABLE

 private:
  nsCOMPtr<nsIObserver> mObserver;
};

NS_IMETHODIMP
TimerObserverRunnable::Run() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(mObserver, "sleep_notification", false);
    observerService->AddObserver(mObserver, "wake_notification", false);
    observerService->AddObserver(mObserver, "suspend_process_notification",
                                 false);
    observerService->AddObserver(mObserver, "resume_process_notification",
                                 false);
    observerService->AddObserver(mObserver, "ipc:process-priority-changed",
                                 false);
  }
  return NS_OK;
}

}  // namespace

namespace {

// TimerEventAllocator is a thread-safe allocator used only for nsTimerEvents.
// It's needed to avoid contention over the default allocator lock when
// firing timer events (see bug 733277).  The thread-safety is required because
// nsTimerEvent objects are allocated on the timer thread, and freed on another
// thread.  Because TimerEventAllocator has its own lock, contention over that
// lock is limited to the allocation and deallocation of nsTimerEvent objects.
//
// Because this is layered over ArenaAllocator, it never shrinks -- even
// "freed" nsTimerEvents aren't truly freed, they're just put onto a free-list
// for later recycling.  So the amount of memory consumed will always be equal
// to the high-water mark consumption.  But nsTimerEvents are small and it's
// unusual to have more than a few hundred of them, so this shouldn't be a
// problem in practice.

class TimerEventAllocator {
 private:
  struct FreeEntry {
    FreeEntry* mNext;
  };

  ArenaAllocator<4096> mPool MOZ_GUARDED_BY(mMonitor);
  FreeEntry* mFirstFree MOZ_GUARDED_BY(mMonitor);
  mozilla::Monitor mMonitor;

 public:
  TimerEventAllocator()
      : mFirstFree(nullptr), mMonitor("TimerEventAllocator") {}

  ~TimerEventAllocator() = default;

  void* Alloc(size_t aSize);
  void Free(void* aPtr);
};

}  // namespace

// This is a nsICancelableRunnable because we can dispatch it to Workers and
// those can be shut down at any time, and in these cases, Cancel() is called
// instead of Run().
class nsTimerEvent final : public CancelableRunnable {
 public:
  NS_IMETHOD Run() override;

  nsresult Cancel() override {
    mTimer->Cancel();
    return NS_OK;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_IMETHOD GetName(nsACString& aName) override;
#endif

  explicit nsTimerEvent(already_AddRefed<nsTimerImpl> aTimer,
                        ProfilerThreadId aTimerThreadId)
      : mozilla::CancelableRunnable("nsTimerEvent"),
        mTimer(aTimer),
        mGeneration(mTimer->GetGeneration()),
        mTimerThreadId(aTimerThreadId) {
    // Note: We override operator new for this class, and the override is
    // fallible!

    AddAllocatorRef();

    if (MOZ_LOG_TEST(GetTimerLog(), LogLevel::Debug) ||
        profiler_thread_is_being_profiled_for_markers(mTimerThreadId)) {
      mInitTime = TimeStamp::Now();
    }
  }

  static void Init();
  static void Shutdown();

  static void* operator new(size_t aSize) noexcept(true) {
    return sAllocator->Alloc(aSize);
  }
  void operator delete(void* aPtr) {
    sAllocator->Free(aPtr);
    ReleaseAllocatorRef();
  }

  already_AddRefed<nsTimerImpl> ForgetTimer() { return mTimer.forget(); }

 private:
  nsTimerEvent(const nsTimerEvent&) = delete;
  nsTimerEvent& operator=(const nsTimerEvent&) = delete;
  nsTimerEvent& operator=(const nsTimerEvent&&) = delete;

  ~nsTimerEvent() = default;

  static void AddAllocatorRef() { ++sAllocatorRefs; }
  static void ReleaseAllocatorRef() {
    nsrefcnt count = --sAllocatorRefs;
    if (count == 0) {
      delete sAllocator;
      sAllocator = nullptr;
    }
  }

  TimeStamp mInitTime;
  RefPtr<nsTimerImpl> mTimer;
  const int32_t mGeneration;
  ProfilerThreadId mTimerThreadId;

  static TimerEventAllocator* sAllocator;
  static ThreadSafeAutoRefCnt sAllocatorRefs;
};

TimerEventAllocator* nsTimerEvent::sAllocator = nullptr;
ThreadSafeAutoRefCnt nsTimerEvent::sAllocatorRefs;

namespace {

void* TimerEventAllocator::Alloc(size_t aSize) {
  MOZ_ASSERT(aSize == sizeof(nsTimerEvent));

  mozilla::MonitorAutoLock lock(mMonitor);

  void* p;
  if (mFirstFree) {
    p = mFirstFree;
    mFirstFree = mFirstFree->mNext;
  } else {
    p = mPool.Allocate(aSize, fallible);
  }

  return p;
}

void TimerEventAllocator::Free(void* aPtr) {
  mozilla::MonitorAutoLock lock(mMonitor);

  FreeEntry* entry = reinterpret_cast<FreeEntry*>(aPtr);

  entry->mNext = mFirstFree;
  mFirstFree = entry;
}

}  // namespace

struct TimerMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("Timer");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   uint32_t aDelay, uint8_t aType,
                                   MarkerThreadId aThreadId, bool aCanceled) {
    aWriter.IntProperty("delay", aDelay);
    if (!aThreadId.IsUnspecified()) {
      // Tech note: If `ToNumber()` returns a uint64_t, the conversion to
      // int64_t is "implementation-defined" before C++20. This is
      // acceptable here, because this is a one-way conversion to a unique
      // identifier that's used to visually separate data by thread on the
      // front-end.
      aWriter.IntProperty(
          "threadId", static_cast<int64_t>(aThreadId.ThreadId().ToNumber()));
    }
    if (aCanceled) {
      aWriter.BoolProperty("canceled", true);
      // Show a red 'X' as a prefix on the marker chart for canceled timers.
      aWriter.StringProperty("prefix", "❌");
    }

    // The string property for the timer type is not written when the type is
    // one shot, as that's the type used almost all the time, and that would
    // consume space in the profiler buffer and then in the profile JSON,
    // getting in the way of capturing long power profiles.
    // Bug 1815677 might make this cheap to capture.
    if (aType != nsITimer::TYPE_ONE_SHOT) {
      if (aType == nsITimer::TYPE_REPEATING_SLACK) {
        aWriter.StringProperty("ttype", "repeating slack");
      } else if (aType == nsITimer::TYPE_REPEATING_PRECISE) {
        aWriter.StringProperty("ttype", "repeating precise");
      } else if (aType == nsITimer::TYPE_REPEATING_PRECISE_CAN_SKIP) {
        aWriter.StringProperty("ttype", "repeating precise can skip");
      } else if (aType == nsITimer::TYPE_REPEATING_SLACK_LOW_PRIORITY) {
        aWriter.StringProperty("ttype", "repeating slack low priority");
      } else if (aType == nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY) {
        aWriter.StringProperty("ttype", "low priority");
      }
    }
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyLabelFormat("delay", "Delay", MS::Format::Milliseconds);
    schema.AddKeyLabelFormat("ttype", "Timer Type", MS::Format::String);
    schema.AddKeyLabelFormat("canceled", "Canceled", MS::Format::String);
    schema.SetChartLabel("{marker.data.prefix} {marker.data.delay}");
    schema.SetTableLabel(
        "{marker.name} - {marker.data.prefix} {marker.data.delay}");
    return schema;
  }
};

struct AddRemoveTimerMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("AddRemoveTimer");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aTimerName,
                                   uint32_t aDelay, MarkerThreadId aThreadId) {
    aWriter.StringProperty("name", aTimerName);
    aWriter.IntProperty("delay", aDelay);
    if (!aThreadId.IsUnspecified()) {
      // Tech note: If `ToNumber()` returns a uint64_t, the conversion to
      // int64_t is "implementation-defined" before C++20. This is
      // acceptable here, because this is a one-way conversion to a unique
      // identifier that's used to visually separate data by thread on the
      // front-end.
      aWriter.IntProperty(
          "threadId", static_cast<int64_t>(aThreadId.ThreadId().ToNumber()));
    }
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyLabelFormatSearchable("name", "Name", MS::Format::String,
                                       MS::Searchable::Searchable);
    schema.AddKeyLabelFormat("delay", "Delay", MS::Format::Milliseconds);
    schema.SetTableLabel(
        "{marker.name} - {marker.data.name} - {marker.data.delay}");
    return schema;
  }
};

void nsTimerEvent::Init() {
  sAllocator = new TimerEventAllocator();
  AddAllocatorRef();  // Freed in Shutdown
}

void nsTimerEvent::Shutdown() {
  ReleaseAllocatorRef();  // Taken in Init
}

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
NS_IMETHODIMP
nsTimerEvent::GetName(nsACString& aName) {
  bool current;
  MOZ_RELEASE_ASSERT(
      NS_SUCCEEDED(mTimer->mEventTarget->IsOnCurrentThread(&current)) &&
      current);

  mTimer->GetName(aName);
  return NS_OK;
}
#endif

NS_IMETHODIMP
nsTimerEvent::Run() {
  if (MOZ_LOG_TEST(GetTimerLog(), LogLevel::Debug)) {
    TimeStamp now = TimeStamp::Now();
    MOZ_LOG(GetTimerLog(), LogLevel::Debug,
            ("[this=%p] time between PostTimerEvent() and Fire(): %fms\n", this,
             (now - mInitTime).ToMilliseconds()));
  }

  if (profiler_thread_is_being_profiled_for_markers(mTimerThreadId)) {
    MutexAutoLock lock(mTimer->mMutex);
    nsAutoCString name;
    mTimer->GetName(name, lock);
    // This adds a marker with the timer name as the marker name, to make it
    // obvious which timers are being used. This marker will be useful to
    // understand which timers might be added and firing excessively often.
    profiler_add_marker(
        name, geckoprofiler::category::TIMER,
        MarkerOptions(MOZ_LIKELY(mInitTime)
                          ? MarkerTiming::Interval(
                                mTimer->mTimeout - mTimer->mDelay, mInitTime)
                          : MarkerTiming::IntervalUntilNowFrom(
                                mTimer->mTimeout - mTimer->mDelay),
                      MarkerThreadId(mTimerThreadId)),
        TimerMarker{}, mTimer->mDelay.ToMilliseconds(), mTimer->mType,
        MarkerThreadId::CurrentThread(), false);
    // This marker is meant to help understand the behavior of the timer thread.
    profiler_add_marker(
        "PostTimerEvent", geckoprofiler::category::OTHER,
        MarkerOptions(MOZ_LIKELY(mInitTime)
                          ? MarkerTiming::IntervalUntilNowFrom(mInitTime)
                          : MarkerTiming::InstantNow(),
                      MarkerThreadId(mTimerThreadId)),
        AddRemoveTimerMarker{}, name, mTimer->mDelay.ToMilliseconds(),
        MarkerThreadId::CurrentThread());
  }

  mTimer->Fire(mGeneration);

  return NS_OK;
}

nsresult TimerThread::Init() {
  mMonitor.AssertCurrentThreadOwns();
  MOZ_LOG(GetTimerLog(), LogLevel::Debug,
          ("TimerThread::Init [%d]\n", mInitialized));

  if (!mInitialized) {
    nsTimerEvent::Init();

    // We hold on to mThread to keep the thread alive.
    nsresult rv =
        NS_NewNamedThread("Timer", getter_AddRefs(mThread), this,
                          {.stackSize = nsIThreadManager::DEFAULT_STACK_SIZE,
                           .blockDispatch = true});
    if (NS_FAILED(rv)) {
      mThread = nullptr;
    } else {
      RefPtr<TimerObserverRunnable> r = new TimerObserverRunnable(this);
      if (NS_IsMainThread()) {
        r->Run();
      } else {
        NS_DispatchToMainThread(r);
      }
    }

    mInitialized = true;
  }

  if (!mThread) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult TimerThread::Shutdown() {
  MOZ_LOG(GetTimerLog(), LogLevel::Debug, ("TimerThread::Shutdown begin\n"));

  if (!mThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsTArray<RefPtr<nsTimerImpl>> timers;
  {
    // lock scope
    MonitorAutoLock lock(mMonitor);

    mShutdown = true;

    // notify the cond var so that Run() can return
    if (mWaiting) {
      mNotified = true;
      mMonitor.Notify();
    }

    // Need to copy content of mTimers array to a local array
    // because call to timers' Cancel() (and release its self)
    // must not be done under the lock. Destructor of a callback
    // might potentially call some code reentering the same lock
    // that leads to unexpected behavior or deadlock.
    // See bug 422472.
    timers.SetCapacity(mTimers.Length());
    for (Entry& entry : mTimers) {
      if (entry.Value()) {
        timers.AppendElement(entry.Take());
      }
    }

    mTimers.Clear();
  }

  for (const RefPtr<nsTimerImpl>& timer : timers) {
    MOZ_ASSERT(timer);
    timer->Cancel();
  }

  mThread->Shutdown();  // wait for the thread to die

  nsTimerEvent::Shutdown();

  MOZ_LOG(GetTimerLog(), LogLevel::Debug, ("TimerThread::Shutdown end\n"));
  return NS_OK;
}

namespace {

struct MicrosecondsToInterval {
  PRIntervalTime operator[](size_t aMs) const {
    return PR_MicrosecondsToInterval(aMs);
  }
};

struct IntervalComparator {
  int operator()(PRIntervalTime aInterval) const {
    return (0 < aInterval) ? -1 : 1;
  }
};

}  // namespace

#ifdef DEBUG
void TimerThread::VerifyTimerListConsistency() const {
  mMonitor.AssertCurrentThreadOwns();

  // Find the first non-canceled timer (and check its cached timeout if we find
  // it).
  const size_t timerCount = mTimers.Length();
  size_t lastNonCanceledTimerIndex = 0;
  while (lastNonCanceledTimerIndex < timerCount &&
         !mTimers[lastNonCanceledTimerIndex].Value()) {
    ++lastNonCanceledTimerIndex;
  }
  MOZ_ASSERT(lastNonCanceledTimerIndex == timerCount ||
             mTimers[lastNonCanceledTimerIndex].Value());
  MOZ_ASSERT(lastNonCanceledTimerIndex == timerCount ||
             mTimers[lastNonCanceledTimerIndex].Value()->mTimeout ==
                 mTimers[lastNonCanceledTimerIndex].Timeout());

  // Verify that mTimers is sorted and the cached timeouts are consistent.
  for (size_t timerIndex = lastNonCanceledTimerIndex + 1;
       timerIndex < timerCount; ++timerIndex) {
    if (mTimers[timerIndex].Value()) {
      MOZ_ASSERT(mTimers[timerIndex].Timeout() ==
                 mTimers[timerIndex].Value()->mTimeout);
      MOZ_ASSERT(mTimers[timerIndex].Timeout() >=
                 mTimers[lastNonCanceledTimerIndex].Timeout());
      lastNonCanceledTimerIndex = timerIndex;
    }
  }
}
#endif

size_t TimerThread::ComputeTimerInsertionIndex(const TimeStamp& timeout) const {
  mMonitor.AssertCurrentThreadOwns();

  const size_t timerCount = mTimers.Length();

  size_t firstGtIndex = 0;
  while (firstGtIndex < timerCount &&
         (!mTimers[firstGtIndex].Value() ||
          mTimers[firstGtIndex].Timeout() <= timeout)) {
    ++firstGtIndex;
  }

  return firstGtIndex;
}

TimeStamp TimerThread::ComputeWakeupTimeFromTimers() const {
  mMonitor.AssertCurrentThreadOwns();

  // Timer list should be non-empty and first timer should always be
  // non-canceled at this point and we rely on that here.
  MOZ_ASSERT(!mTimers.IsEmpty());
  MOZ_ASSERT(mTimers[0].Value());

  // Overview: Find the last timer in the list that can be "bundled" together in
  // the same wake-up with mTimers[0] and use its timeout as our target wake-up
  // time.

  // bundleWakeup is when we should wake up in order to be able to fire all of
  // the timers in our selected bundle. It will always be the timeout of the
  // last timer in the bundle.
  TimeStamp bundleWakeup = mTimers[0].Timeout();

  // cutoffTime is the latest that we can wake up for the timers currently
  // accepted into the bundle. These needs to be updated as we go through the
  // list because later timers may have more strict delay tolerances.
  const TimeDuration minTimerDelay = TimeDuration::FromMilliseconds(
      StaticPrefs::timer_minimum_firing_delay_tolerance_ms());
  const TimeDuration maxTimerDelay = TimeDuration::FromMilliseconds(
      StaticPrefs::timer_maximum_firing_delay_tolerance_ms());
  TimeStamp cutoffTime =
      bundleWakeup + ComputeAcceptableFiringDelay(mTimers[0].Delay(),
                                                  minTimerDelay, maxTimerDelay);

  const size_t timerCount = mTimers.Length();
  for (size_t entryIndex = 1; entryIndex < timerCount; ++entryIndex) {
    const Entry& curEntry = mTimers[entryIndex];
    const nsTimerImpl* curTimer = curEntry.Value();
    if (!curTimer) {
      // Canceled timer - skip it
      continue;
    }

    const TimeStamp curTimerDue = curEntry.Timeout();
    if (curTimerDue > cutoffTime) {
      // Can't include this timer in the bundle - it fires too late.
      break;
    }

    // This timer can be included in the bundle. Update bundleWakeup and
    // cutoffTime.
    bundleWakeup = curTimerDue;
    cutoffTime = std::min(
        curTimerDue + ComputeAcceptableFiringDelay(
                          curEntry.Delay(), minTimerDelay, maxTimerDelay),
        cutoffTime);
    MOZ_ASSERT(bundleWakeup <= cutoffTime);
  }

#if !defined(XP_WIN)
  // Due to the fact that, on Windows, each TimeStamp object holds two distinct
  // "values", this assert is not valid there. See bug 1829983 for the details.
  MOZ_ASSERT(bundleWakeup - mTimers[0].Timeout() <=
             ComputeAcceptableFiringDelay(mTimers[0].Delay(), minTimerDelay,
                                          maxTimerDelay));
#endif

  return bundleWakeup;
}

TimeDuration TimerThread::ComputeAcceptableFiringDelay(
    TimeDuration timerDuration, TimeDuration minDelay,
    TimeDuration maxDelay) const {
  // Use the timer's duration divided by this value as a base for how much
  // firing delay a timer can accept. 8 was chosen specifically because it is a
  // power of two which means that this division turns nicely into a shift.
  constexpr int64_t timerDurationDivider = 8;
  static_assert(IsPowerOfTwo(static_cast<uint64_t>(timerDurationDivider)));
  const TimeDuration tmp = timerDuration / timerDurationDivider;
  return std::clamp(tmp, minDelay, maxDelay);
}

NS_IMETHODIMP
TimerThread::Run() {
  MonitorAutoLock lock(mMonitor);

  mProfilerThreadId = profiler_current_thread_id();

  // TODO: Make mAllowedEarlyFiringMicroseconds const and initialize it in the
  // constructor.
  mAllowedEarlyFiringMicroseconds = 250;
  const TimeDuration normalAllowedEarlyFiring =
      TimeDuration::FromMicroseconds(mAllowedEarlyFiringMicroseconds);

  // Queue for tracking of how many timers are fired on each wake-up. We need to
  // buffer these locally and only send off to glean occasionally to avoid
  // performance hit.
  static constexpr size_t kMaxQueuedTimerFired = 128;
  size_t queuedTimerFiredCount = 0;
  AutoTArray<uint64_t, kMaxQueuedTimerFired> queuedTimersFiredPerWakeup;
  queuedTimersFiredPerWakeup.SetLengthAndRetainStorage(kMaxQueuedTimerFired);

#ifdef XP_WIN
  // kTimerPeriodEvalIntervalSec is the minimum amount of time that must pass
  // before we will consider changing the timer period again.
  static constexpr float kTimerPeriodEvalIntervalSec = 2.0f;
  const TimeDuration timerPeriodEvalInterval =
      TimeDuration::FromSeconds(kTimerPeriodEvalIntervalSec);
  TimeStamp nextTimerPeriodEval = TimeStamp::Now() + timerPeriodEvalInterval;

  // If this is false, we will perform all of the logic but will stop short of
  // actually changing the timer period.
  const bool adjustTimerPeriod =
      StaticPrefs::timer_auto_increase_timer_resolution();
  UINT lastTimePeriodSet = ComputeDesiredTimerPeriod();

  if (adjustTimerPeriod) {
    timeBeginPeriod(lastTimePeriodSet);
  }
#endif

  uint64_t timersFiredThisWakeup = 0;
  while (!mShutdown) {
    const bool chaosModeActive =
        ChaosMode::isActive(ChaosFeature::TimerScheduling);

    // Have to use PRIntervalTime here, since PR_WaitCondVar takes it
    TimeDuration waitFor;

#ifdef DEBUG
    VerifyTimerListConsistency();
#endif

    if (mSleeping) {
      // Sleep for 0.1 seconds while not firing timers.
      uint32_t milliseconds = 100;
      if (chaosModeActive) {
        milliseconds = ChaosMode::randomUint32LessThan(200);
      }
      waitFor = TimeDuration::FromMilliseconds(milliseconds);
    } else {
      // Determine how early we are going to allow timers to fire. In chaos mode
      // we mess with this a little bit.
      const TimeDuration allowedEarlyFiring =
          !chaosModeActive
              ? normalAllowedEarlyFiring
              : TimeDuration::FromMicroseconds(ChaosMode::randomUint32LessThan(
                    4 * mAllowedEarlyFiringMicroseconds));

      waitFor = TimeDuration::Forever();
      TimeStamp now = TimeStamp::Now();

#ifdef XP_WIN
      if (now >= nextTimerPeriodEval) {
        const UINT newTimePeriod = ComputeDesiredTimerPeriod();
        if (newTimePeriod != lastTimePeriodSet) {
          if (adjustTimerPeriod) {
            timeEndPeriod(lastTimePeriodSet);
            timeBeginPeriod(newTimePeriod);
          }
          lastTimePeriodSet = newTimePeriod;
        }
        nextTimerPeriodEval = now + timerPeriodEvalInterval;
      }
#endif

#if TIMER_THREAD_STATISTICS
      if (!mNotified && !mIntendedWakeupTime.IsNull() &&
          now < mIntendedWakeupTime) {
        ++mEarlyWakeups;
        const double earlinessms = (mIntendedWakeupTime - now).ToMilliseconds();
        mTotalEarlyWakeupTime += earlinessms;
      }
#endif

      RemoveLeadingCanceledTimersInternal();

      if (!mTimers.IsEmpty()) {
        if (now + allowedEarlyFiring >= mTimers[0].Value()->mTimeout) {
        next:
          // NB: AddRef before the Release under RemoveTimerInternal to avoid
          // mRefCnt passing through zero, in case all other refs than the one
          // from mTimers have gone away (the last non-mTimers[i]-ref's Release
          // must be racing with us, blocked in gThread->RemoveTimer waiting
          // for TimerThread::mMonitor, under nsTimerImpl::Release.

          RefPtr<nsTimerImpl> timerRef(mTimers[0].Take());
          RemoveFirstTimerInternal();
          MOZ_LOG(GetTimerLog(), LogLevel::Debug,
                  ("Timer thread woke up %fms from when it was supposed to\n",
                   fabs((now - timerRef->mTimeout).ToMilliseconds())));

          // We are going to let the call to PostTimerEvent here handle the
          // release of the timer so that we don't end up releasing the timer
          // on the TimerThread instead of on the thread it targets.
          {
            ++timersFiredThisWakeup;
            LogTimerEvent::Run run(timerRef.get());
            PostTimerEvent(timerRef.forget());
          }

          if (mShutdown) {
            break;
          }

          // Update now, as PostTimerEvent plus the locking may have taken a
          // tick or two, and we may goto next below.
          now = TimeStamp::Now();
        }
      }

      RemoveLeadingCanceledTimersInternal();

      if (!mTimers.IsEmpty()) {
        TimeStamp timeout = mTimers[0].Value()->mTimeout;

        // Don't wait at all (even for PR_INTERVAL_NO_WAIT) if the next timer
        // is due now or overdue.
        //
        // Note that we can only sleep for integer values of a certain
        // resolution. We use mAllowedEarlyFiringMicroseconds, calculated
        // before, to do the optimal rounding (i.e., of how to decide what
        // interval is so small we should not wait at all).
        const TimeDuration timeToNextTimer = timeout - now;

        if (timeToNextTimer < allowedEarlyFiring) {
          goto next;  // round down; execute event now
        }

        // TECHNICAL NOTE: Determining waitFor (by subtracting |now| from our
        // desired wake-up time) at this point is not ideal. For one thing, the
        // |now| that we have at this point is somewhat old. Secondly, there is
        // quite a bit of code between here and where we actually use waitFor to
        // request sleep. If I am thinking about this correctly, both of these
        // will contribute to us requesting more sleep than is actually needed
        // to wake up at our desired time. We could avoid this problem by only
        // determining our desired wake-up time here and then calculating the
        // wait time when we're actually about to sleep.
        const TimeStamp wakeupTime = ComputeWakeupTimeFromTimers();
        waitFor = wakeupTime - now;

        // If this were to fail that would mean that we had more timers that we
        // should have fired.
        MOZ_ASSERT(!waitFor.IsZero());

        // If chaos mode is active then we will add a random amount to the
        // calculated wait (sleep) time to simulate early/late wake-ups.
        const TimeDuration chaosWaitDelay =
            !chaosModeActive
                ? TimeDuration::Zero()
                : TimeDuration::FromMicroseconds(
                      ChaosMode::randomInt32InRange(-10000, 10000));
        waitFor = std::max(TimeDuration::Zero(), waitFor + chaosWaitDelay);

        mIntendedWakeupTime = wakeupTime;
      } else {
        mIntendedWakeupTime = TimeStamp{};
      }

      if (MOZ_LOG_TEST(GetTimerLog(), LogLevel::Debug)) {
        if (waitFor == TimeDuration::Forever())
          MOZ_LOG(GetTimerLog(), LogLevel::Debug, ("waiting forever\n"));
        else
          MOZ_LOG(GetTimerLog(), LogLevel::Debug,
                  ("waiting for %f\n", waitFor.ToMilliseconds()));
      }
    }

    {
      // About to sleep - let's make note of how many timers we processed and
      // see if we should send out a new batch of telemetry.
      queuedTimersFiredPerWakeup[queuedTimerFiredCount] = timersFiredThisWakeup;
      ++queuedTimerFiredCount;
      if (queuedTimerFiredCount == kMaxQueuedTimerFired) {
        glean::timer_thread::timers_fired_per_wakeup.AccumulateSamples(
            queuedTimersFiredPerWakeup);
        queuedTimerFiredCount = 0;
      }
    }

#if TIMER_THREAD_STATISTICS
    {
      size_t bucketIndex = 0;
      while (bucketIndex < sTimersFiredPerWakeupBucketCount - 1 &&
             timersFiredThisWakeup >
                 sTimersFiredPerWakeupThresholds[bucketIndex]) {
        ++bucketIndex;
      }
      MOZ_ASSERT(bucketIndex < sTimersFiredPerWakeupBucketCount);
      ++mTimersFiredPerWakeup[bucketIndex];

      ++mTotalWakeupCount;
      if (mNotified) {
        ++mTimersFiredPerNotifiedWakeup[bucketIndex];
        ++mTotalNotifiedWakeupCount;
      } else {
        ++mTimersFiredPerUnnotifiedWakeup[bucketIndex];
        ++mTotalUnnotifiedWakeupCount;
      }
    }
#endif

    timersFiredThisWakeup = 0;

    mWaiting = true;
    mNotified = false;

    {
      AUTO_PROFILER_TRACING_MARKER("TimerThread", "Wait", OTHER);
      mMonitor.Wait(waitFor);
    }
    mWaiting = false;
  }

  // About to shut down - let's send out the final batch of timers fired counts.
  if (queuedTimerFiredCount != 0) {
    queuedTimersFiredPerWakeup.SetLengthAndRetainStorage(queuedTimerFiredCount);
    glean::timer_thread::timers_fired_per_wakeup.AccumulateSamples(
        queuedTimersFiredPerWakeup);
  }

#ifdef XP_WIN
  // About to shut down - let's finish off the last time period that we set.
  if (adjustTimerPeriod) {
    timeEndPeriod(lastTimePeriodSet);
  }
#endif

  return NS_OK;
}

nsresult TimerThread::AddTimer(nsTimerImpl* aTimer,
                               const MutexAutoLock& aProofOfLock) {
  MonitorAutoLock lock(mMonitor);
  AUTO_TIMERS_STATS(TimerThread_AddTimer);

  if (!aTimer->mEventTarget) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = Init();
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Awaken the timer thread if:
  // - This timer needs to fire *before* the Timer Thread is scheduled to wake
  //   up.
  // AND/OR
  // - The delay is 0, which is usually meant to be run as soon as possible.
  //   Note: Even if the thread is scheduled to wake up now/soon, on some
  //   systems there could be a significant delay compared to notifying, which
  //   is almost immediate; and some users of 0-delay depend on it being this
  //   fast!
  const TimeDuration minTimerDelay = TimeDuration::FromMilliseconds(
      StaticPrefs::timer_minimum_firing_delay_tolerance_ms());
  const TimeDuration maxTimerDelay = TimeDuration::FromMilliseconds(
      StaticPrefs::timer_maximum_firing_delay_tolerance_ms());
  const TimeDuration firingDelay = ComputeAcceptableFiringDelay(
      aTimer->mDelay, minTimerDelay, maxTimerDelay);
  const bool firingBeforeNextWakeup =
      mIntendedWakeupTime.IsNull() ||
      (aTimer->mTimeout + firingDelay < mIntendedWakeupTime);
  const bool wakeUpTimerThread =
      mWaiting && (firingBeforeNextWakeup || aTimer->mDelay.IsZero());

#if TIMER_THREAD_STATISTICS
  if (mTotalTimersAdded == 0) {
    mFirstTimerAdded = TimeStamp::Now();
  }
  ++mTotalTimersAdded;
#endif

  // Add the timer to our list.
  if (!AddTimerInternal(*aTimer)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (wakeUpTimerThread) {
    mNotified = true;
    mMonitor.Notify();
  }

  if (profiler_thread_is_being_profiled_for_markers(mProfilerThreadId)) {
    nsAutoCString name;
    aTimer->GetName(name, aProofOfLock);

    nsLiteralCString prefix("Anonymous_");
    profiler_add_marker(
        "AddTimer", geckoprofiler::category::OTHER,
        MarkerOptions(MarkerThreadId(mProfilerThreadId),
                      MarkerStack::MaybeCapture(
                          name.Equals("nonfunction:JS") ||
                          StringHead(name, prefix.Length()) == prefix)),
        AddRemoveTimerMarker{}, name, aTimer->mDelay.ToMilliseconds(),
        MarkerThreadId::CurrentThread());
  }

  return NS_OK;
}

nsresult TimerThread::RemoveTimer(nsTimerImpl* aTimer,
                                  const MutexAutoLock& aProofOfLock) {
  MonitorAutoLock lock(mMonitor);
  AUTO_TIMERS_STATS(TimerThread_RemoveTimer);

  // Remove the timer from our array.  Tell callers that aTimer was not found
  // by returning NS_ERROR_NOT_AVAILABLE.

  if (!RemoveTimerInternal(*aTimer)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

#if TIMER_THREAD_STATISTICS
  ++mTotalTimersRemoved;
#endif

  // Note: The timer thread is *not* awoken.
  // The removed-timer entry is just left null, and will be reused (by a new or
  // re-set timer) or discarded (when the timer thread logic handles non-null
  // timers around it).
  // If this was the front timer, and in the unlikely case that its entry is not
  // soon reused by a re-set timer, the timer thread will wake up at the
  // previously-scheduled time, but will quickly notice that there is no actual
  // pending timer, and will restart its wait until the following real timeout.

  if (profiler_thread_is_being_profiled_for_markers(mProfilerThreadId)) {
    nsAutoCString name;
    aTimer->GetName(name, aProofOfLock);

    nsLiteralCString prefix("Anonymous_");
    // This marker is meant to help understand the behavior of the timer thread.
    profiler_add_marker(
        "RemoveTimer", geckoprofiler::category::OTHER,
        MarkerOptions(MarkerThreadId(mProfilerThreadId),
                      MarkerStack::MaybeCapture(
                          name.Equals("nonfunction:JS") ||
                          StringHead(name, prefix.Length()) == prefix)),
        AddRemoveTimerMarker{}, name, aTimer->mDelay.ToMilliseconds(),
        MarkerThreadId::CurrentThread());
    // This adds a marker with the timer name as the marker name, to make it
    // obvious which timers are being used. This marker will be useful to
    // understand which timers might be added and removed excessively often.
    profiler_add_marker(name, geckoprofiler::category::TIMER,
                        MarkerOptions(MarkerTiming::IntervalUntilNowFrom(
                                          aTimer->mTimeout - aTimer->mDelay),
                                      MarkerThreadId(mProfilerThreadId)),
                        TimerMarker{}, aTimer->mDelay.ToMilliseconds(),
                        aTimer->mType, MarkerThreadId::CurrentThread(), true);
  }

  return NS_OK;
}

TimeStamp TimerThread::FindNextFireTimeForCurrentThread(TimeStamp aDefault,
                                                        uint32_t aSearchBound) {
  MonitorAutoLock lock(mMonitor);
  AUTO_TIMERS_STATS(TimerThread_FindNextFireTimeForCurrentThread);

  for (const Entry& entry : mTimers) {
    const nsTimerImpl* timer = entry.Value();
    if (timer) {
      if (entry.Timeout() > aDefault) {
        return aDefault;
      }

      // Don't yield to timers created with the *_LOW_PRIORITY type.
      if (!timer->IsLowPriority()) {
        bool isOnCurrentThread = false;
        nsresult rv =
            timer->mEventTarget->IsOnCurrentThread(&isOnCurrentThread);
        if (NS_SUCCEEDED(rv) && isOnCurrentThread) {
          return entry.Timeout();
        }
      }

      if (aSearchBound == 0) {
        // Couldn't find any non-low priority timers for the current thread.
        // Return a compromise between a very short and a long idle time.
        TimeStamp fallbackDeadline =
            TimeStamp::Now() + TimeDuration::FromMilliseconds(16);
        return fallbackDeadline < aDefault ? fallbackDeadline : aDefault;
      }

      --aSearchBound;
    }
  }

  // No timers for this thread, return the default.
  return aDefault;
}

// This function must be called from within a lock
// Also: we hold the mutex for the nsTimerImpl.
bool TimerThread::AddTimerInternal(nsTimerImpl& aTimer) {
  mMonitor.AssertCurrentThreadOwns();
  aTimer.mMutex.AssertCurrentThreadOwns();
  AUTO_TIMERS_STATS(TimerThread_AddTimerInternal);
  if (mShutdown) {
    return false;
  }

  LogTimerEvent::LogDispatch(&aTimer);

  const TimeStamp& timeout = aTimer.mTimeout;
  const size_t insertionIndex = ComputeTimerInsertionIndex(timeout);

  if (insertionIndex != 0 && !mTimers[insertionIndex - 1].Value()) {
    // Very common scenario in practice: The timer just before the insertion
    // point is canceled, overwrite it.
    AUTO_TIMERS_STATS(TimerThread_AddTimerInternal_overwrite_before);
    mTimers[insertionIndex - 1] = Entry{aTimer};
    return true;
  }

  const size_t length = mTimers.Length();
  if (insertionIndex == length) {
    // We're at the end (including it's the very first insertion), add new timer
    // at the end.
    AUTO_TIMERS_STATS(TimerThread_AddTimerInternal_append);
    return mTimers.AppendElement(Entry{aTimer}, mozilla::fallible);
  }

  if (!mTimers[insertionIndex].Value()) {
    // The timer at the insertion point is canceled, overwrite it.
    AUTO_TIMERS_STATS(TimerThread_AddTimerInternal_overwrite);
    mTimers[insertionIndex] = Entry{aTimer};
    return true;
  }

  // The new timer has to be inserted.
  AUTO_TIMERS_STATS(TimerThread_AddTimerInternal_insert);
  // The capacity should be checked first, because if it needs to be increased
  // and the memory allocation fails, only the new timer should be lost.
  if (length == mTimers.Capacity() && mTimers[length - 1].Value()) {
    // We have reached capacity, and the last entry is not canceled, so we
    // really want to increase the capacity in case the extra slot is required.
    // To force-expand the array, append a canceled-timer entry with a timestamp
    // far in the future.
    // This empty Entry may be used below to receive the moved-from previous
    // entry. If not, it may be used in a later call if we need to append a new
    // timer at the end.
    AUTO_TIMERS_STATS(TimerThread_AddTimerInternal_insert_expand);
    if (!mTimers.AppendElement(
            Entry{mTimers[length - 1].Timeout() +
                  TimeDuration::FromSeconds(365.0 * 24.0 * 60.0 * 60.0)},
            mozilla::fallible)) {
      return false;
    }
  }

  // Extract the timer at the insertion point, and put the new timer in its
  // place.
  Entry extractedEntry = std::exchange(mTimers[insertionIndex], Entry{aTimer});
  // Following entries can be pushed until we hit a canceled timer or the end.
  for (size_t i = insertionIndex + 1; i < length; ++i) {
    Entry& entryRef = mTimers[i];
    if (!entryRef.Value()) {
      // Canceled entry, overwrite it with the extracted entry from before.
      COUNT_TIMERS_STATS(TimerThread_AddTimerInternal_insert_overwrite);
      entryRef = std::move(extractedEntry);
      return true;
    }
    // Write extracted entry from before, and extract current entry.
    COUNT_TIMERS_STATS(TimerThread_AddTimerInternal_insert_shifts);
    std::swap(entryRef, extractedEntry);
  }
  // We've reached the end of the list, with still one extracted entry to
  // re-insert. We've checked the capacity above, this cannot fail.
  COUNT_TIMERS_STATS(TimerThread_AddTimerInternal_insert_append);
  mTimers.AppendElement(std::move(extractedEntry));
  return true;
}

// This function must be called from within a lock
// Also: we hold the mutex for the nsTimerImpl.
bool TimerThread::RemoveTimerInternal(nsTimerImpl& aTimer) {
  mMonitor.AssertCurrentThreadOwns();
  aTimer.mMutex.AssertCurrentThreadOwns();
  AUTO_TIMERS_STATS(TimerThread_RemoveTimerInternal);
  if (!aTimer.IsInTimerThread()) {
    COUNT_TIMERS_STATS(TimerThread_RemoveTimerInternal_not_in_list);
    return false;
  }
  AUTO_TIMERS_STATS(TimerThread_RemoveTimerInternal_in_list);
  for (auto& entry : mTimers) {
    if (entry.Value() == &aTimer) {
      entry.Forget();
      return true;
    }
  }
  MOZ_ASSERT(!aTimer.IsInTimerThread(),
             "Not found in the list but it should be!?");
  return false;
}

void TimerThread::RemoveLeadingCanceledTimersInternal() {
  mMonitor.AssertCurrentThreadOwns();
  AUTO_TIMERS_STATS(TimerThread_RemoveLeadingCanceledTimersInternal);

  size_t toRemove = 0;
  while (toRemove < mTimers.Length() && !mTimers[toRemove].Value()) {
    ++toRemove;
  }
  mTimers.RemoveElementsAt(0, toRemove);
}

void TimerThread::RemoveFirstTimerInternal() {
  mMonitor.AssertCurrentThreadOwns();
  AUTO_TIMERS_STATS(TimerThread_RemoveFirstTimerInternal);
  MOZ_ASSERT(!mTimers.IsEmpty());
  mTimers.RemoveElementAt(0);
}

void TimerThread::PostTimerEvent(already_AddRefed<nsTimerImpl> aTimerRef) {
  mMonitor.AssertCurrentThreadOwns();
  AUTO_TIMERS_STATS(TimerThread_PostTimerEvent);

  RefPtr<nsTimerImpl> timer(aTimerRef);

#if TIMER_THREAD_STATISTICS
  const double actualFiringDelay =
      std::max((TimeStamp::Now() - timer->mTimeout).ToMilliseconds(), 0.0);
  if (mNotified) {
    ++mTotalTimersFiredNotified;
    mTotalActualTimerFiringDelayNotified += actualFiringDelay;
  } else {
    ++mTotalTimersFiredUnnotified;
    mTotalActualTimerFiringDelayUnnotified += actualFiringDelay;
  }
#endif

  if (!timer->mEventTarget) {
    NS_ERROR("Attempt to post timer event to NULL event target");
    return;
  }

  // XXX we may want to reuse this nsTimerEvent in the case of repeating timers.

  // Since we already addref'd 'timer', we don't need to addref here.
  // We will release either in ~nsTimerEvent(), or pass the reference back to
  // the caller. We need to copy the generation number from this timer into the
  // event, so we can avoid firing a timer that was re-initialized after being
  // canceled.

  nsCOMPtr<nsIEventTarget> target = timer->mEventTarget;

  void* p = nsTimerEvent::operator new(sizeof(nsTimerEvent));
  if (!p) {
    return;
  }
  RefPtr<nsTimerEvent> event =
      ::new (KnownNotNull, p) nsTimerEvent(timer.forget(), mProfilerThreadId);

  nsresult rv;
  {
    // We release mMonitor around the Dispatch because if the Dispatch interacts
    // with the timer API we'll deadlock.
    MonitorAutoUnlock unlock(mMonitor);
    rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
    if (NS_FAILED(rv)) {
      timer = event->ForgetTimer();
      // We do this to avoid possible deadlock by taking the two locks in a
      // different order than is used in RemoveTimer().  RemoveTimer() has
      // aTimer->mMutex first.   We use timer.get() to keep static analysis
      // happy
      // NOTE: I'm not sure that any of the below is actually necessary. It
      // seems to me that the timer that we're trying to fire will have already
      // been removed prior to this.
      MutexAutoLock lock1(timer.get()->mMutex);
      MonitorAutoLock lock2(mMonitor);
      RemoveTimerInternal(*timer);
    }
  }
}

void TimerThread::DoBeforeSleep() {
  // Mainthread
  MonitorAutoLock lock(mMonitor);
  mSleeping = true;
}

// Note: wake may be notified without preceding sleep notification
void TimerThread::DoAfterSleep() {
  // Mainthread
  MonitorAutoLock lock(mMonitor);
  mSleeping = false;

  // Wake up the timer thread to re-process the array to ensure the sleep delay
  // is correct, and fire any expired timers (perhaps quite a few)
  mNotified = true;
  PROFILER_MARKER_UNTYPED("AfterSleep", OTHER,
                          MarkerThreadId(mProfilerThreadId));
  mMonitor.Notify();
}

NS_IMETHODIMP
TimerThread::Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) {
  if (strcmp(aTopic, "ipc:process-priority-changed") == 0) {
    nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(aSubject);
    MOZ_ASSERT(props != nullptr);

    int32_t priority = static_cast<int32_t>(hal::PROCESS_PRIORITY_UNKNOWN);
    props->GetPropertyAsInt32(u"priority"_ns, &priority);
    mCachedPriority.store(static_cast<hal::ProcessPriority>(priority),
                          std::memory_order_relaxed);
  }

  if (StaticPrefs::timer_ignore_sleep_wake_notifications()) {
    return NS_OK;
  }

  if (strcmp(aTopic, "sleep_notification") == 0 ||
      strcmp(aTopic, "suspend_process_notification") == 0) {
    DoBeforeSleep();
  } else if (strcmp(aTopic, "wake_notification") == 0 ||
             strcmp(aTopic, "resume_process_notification") == 0) {
    DoAfterSleep();
  }

  return NS_OK;
}

uint32_t TimerThread::AllowedEarlyFiringMicroseconds() {
  MonitorAutoLock lock(mMonitor);
  return mAllowedEarlyFiringMicroseconds;
}

#if TIMER_THREAD_STATISTICS
void TimerThread::PrintStatistics() const {
  mMonitor.AssertCurrentThreadOwns();

  const TimeStamp freshNow = TimeStamp::Now();
  const double timeElapsed = mFirstTimerAdded.IsNull()
                                 ? 0.0
                                 : (freshNow - mFirstTimerAdded).ToSeconds();
  printf_stderr("TimerThread Stats (Total time %8.2fs)\n", timeElapsed);

  printf_stderr("Added: %6llu Removed: %6llu Fired: %6llu\n", mTotalTimersAdded,
                mTotalTimersRemoved,
                mTotalTimersFiredNotified + mTotalTimersFiredUnnotified);

  auto PrintTimersFiredBucket =
      [](const AutoTArray<size_t, sTimersFiredPerWakeupBucketCount>& buckets,
         const size_t wakeupCount, const size_t timersFiredCount,
         const double totalTimerDelay, const char* label) {
        printf_stderr("%s : [", label);
        for (size_t bucketVal : buckets) {
          printf_stderr(" %5llu", bucketVal);
        }
        printf_stderr(
            " ] Wake-ups/timer %6llu / %6llu (%7.4f) Avg Timer Delay %7.4f\n",
            wakeupCount, timersFiredCount,
            static_cast<double>(wakeupCount) / timersFiredCount,
            totalTimerDelay / timersFiredCount);
      };

  printf_stderr("Wake-ups:\n");
  PrintTimersFiredBucket(
      mTimersFiredPerWakeup, mTotalWakeupCount,
      mTotalTimersFiredNotified + mTotalTimersFiredUnnotified,
      mTotalActualTimerFiringDelayNotified +
          mTotalActualTimerFiringDelayUnnotified,
      "Total      ");
  PrintTimersFiredBucket(mTimersFiredPerNotifiedWakeup,
                         mTotalNotifiedWakeupCount, mTotalTimersFiredNotified,
                         mTotalActualTimerFiringDelayNotified, "Notified   ");
  PrintTimersFiredBucket(mTimersFiredPerUnnotifiedWakeup,
                         mTotalUnnotifiedWakeupCount,
                         mTotalTimersFiredUnnotified,
                         mTotalActualTimerFiringDelayUnnotified, "Unnotified ");

  printf_stderr("Early Wake-ups: %6llu Avg: %7.4fms\n", mEarlyWakeups,
                mTotalEarlyWakeupTime / mEarlyWakeups);
}
#endif

/* This nsReadOnlyTimer class is used for the values returned by the
 * TimerThread::GetTimers method.
 * It is not possible to return a strong reference to the nsTimerImpl
 * instance (that could extend the lifetime of the timer and cause it to fire
 * a callback pointing to already freed memory) or a weak reference
 * (nsSupportsWeakReference doesn't support freeing the referee on a thread
 * that isn't the thread that owns the weak reference), so instead the timer
 * name, delay and type are copied to a new object. */
class nsReadOnlyTimer final : public nsITimer {
 public:
  explicit nsReadOnlyTimer(const nsACString& aName, uint32_t aDelay,
                           uint32_t aType)
      : mName(aName), mDelay(aDelay), mType(aType) {}
  NS_DECL_ISUPPORTS

  NS_IMETHOD Init(nsIObserver* aObserver, uint32_t aDelayInMs,
                  uint32_t aType) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD InitWithCallback(nsITimerCallback* aCallback, uint32_t aDelayInMs,
                              uint32_t aType) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD InitHighResolutionWithCallback(nsITimerCallback* aCallback,
                                            const mozilla::TimeDuration& aDelay,
                                            uint32_t aType) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD Cancel(void) override { return NS_ERROR_NOT_IMPLEMENTED; }
  NS_IMETHOD InitWithNamedFuncCallback(nsTimerCallbackFunc aCallback,
                                       void* aClosure, uint32_t aDelay,
                                       uint32_t aType,
                                       const char* aName) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD InitHighResolutionWithNamedFuncCallback(
      nsTimerCallbackFunc aCallback, void* aClosure,
      const mozilla::TimeDuration& aDelay, uint32_t aType,
      const char* aName) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD GetName(nsACString& aName) override {
    aName = mName;
    return NS_OK;
  }
  NS_IMETHOD GetDelay(uint32_t* aDelay) override {
    *aDelay = mDelay;
    return NS_OK;
  }
  NS_IMETHOD SetDelay(uint32_t aDelay) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetType(uint32_t* aType) override {
    *aType = mType;
    return NS_OK;
  }
  NS_IMETHOD SetType(uint32_t aType) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetClosure(void** aClosure) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetCallback(nsITimerCallback** aCallback) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetTarget(nsIEventTarget** aTarget) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD SetTarget(nsIEventTarget* aTarget) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  NS_IMETHOD GetAllowedEarlyFiringMicroseconds(
      uint32_t* aAllowedEarlyFiringMicroseconds) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) override {
    return sizeof(*this);
  }

 private:
  nsCString mName;
  uint32_t mDelay;
  uint32_t mType;
  ~nsReadOnlyTimer() = default;
};

NS_IMPL_ISUPPORTS(nsReadOnlyTimer, nsITimer)

nsresult TimerThread::GetTimers(nsTArray<RefPtr<nsITimer>>& aRetVal) {
  nsTArray<RefPtr<nsTimerImpl>> timers;
  {
    MonitorAutoLock lock(mMonitor);
    for (const auto& entry : mTimers) {
      nsTimerImpl* timer = entry.Value();
      if (!timer) {
        continue;
      }
      timers.AppendElement(timer);
    }
  }

  for (nsTimerImpl* timer : timers) {
    nsAutoCString name;
    timer->GetName(name);

    uint32_t delay;
    timer->GetDelay(&delay);

    uint32_t type;
    timer->GetType(&type);

    aRetVal.AppendElement(new nsReadOnlyTimer(name, delay, type));
  }

  return NS_OK;
}
