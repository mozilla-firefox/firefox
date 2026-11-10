// Copyright (c) 2006-2011 The Chromium Authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//  * Neither the name of Google, Inc. nor the names of its contributors
//    may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

#include <windows.h>
#include <mmsystem.h>
#include <process.h>

#include <type_traits>

static void PopulateRegsFromContext(Registers& aRegs, CONTEXT* aContext) {
#if defined(GP_ARCH_amd64)
  aRegs.mPC = reinterpret_cast<Address>(aContext->Rip);
  aRegs.mSP = reinterpret_cast<Address>(aContext->Rsp);
  aRegs.mFP = reinterpret_cast<Address>(aContext->Rbp);
  aRegs.mR10 = reinterpret_cast<Address>(aContext->R10);
  aRegs.mR12 = reinterpret_cast<Address>(aContext->R12);
#elif defined(GP_ARCH_x86)
  aRegs.mPC = reinterpret_cast<Address>(aContext->Eip);
  aRegs.mSP = reinterpret_cast<Address>(aContext->Esp);
  aRegs.mFP = reinterpret_cast<Address>(aContext->Ebp);
  aRegs.mEcx = reinterpret_cast<Address>(aContext->Ecx);
  aRegs.mEdx = reinterpret_cast<Address>(aContext->Edx);
#elif defined(GP_ARCH_arm64)
  aRegs.mPC = reinterpret_cast<Address>(aContext->Pc);
  aRegs.mSP = reinterpret_cast<Address>(aContext->Sp);
  aRegs.mFP = reinterpret_cast<Address>(aContext->Fp);
  aRegs.mLR = reinterpret_cast<Address>(aContext->Lr);
  aRegs.mR11 = reinterpret_cast<Address>(aContext->X11);
#else
#  error "bad arch"
#endif
}

// Gets a real (i.e. not pseudo) handle for the current thread, with the
// permissions needed for profiling.
// @return a real HANDLE for the current thread.
static HANDLE GetRealCurrentThreadHandleForProfiling() {
  HANDLE realCurrentThreadHandle;
  if (!::DuplicateHandle(
          ::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
          &realCurrentThreadHandle,
          THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
          FALSE, 0)) {
    return nullptr;
  }

  return realCurrentThreadHandle;
}

static_assert(
    std::is_same_v<mozilla::profiler::PlatformData::WindowsHandle, HANDLE>);

mozilla::profiler::PlatformData::PlatformData(ProfilerThreadId aThreadId)
    : mProfiledThread(GetRealCurrentThreadHandleForProfiling()) {
  MOZ_ASSERT(aThreadId == ProfilerThreadId::FromNumber(::GetCurrentThreadId()));
}

mozilla::profiler::PlatformData::~PlatformData() {
  if (mProfiledThread) {
    CloseHandle(mProfiledThread);
    mProfiledThread = nullptr;
  }
}

static const HANDLE kNoThread = INVALID_HANDLE_VALUE;

////////////////////////////////////////////////////////////////////////
// BEGIN Sampler target specifics

Sampler::Sampler(PSLockRef aLock) {}

void Sampler::Disable(PSLockRef aLock) {}

static void StreamMetaPlatformSampleUnits(PSLockRef aLock,
                                          SpliceableJSONWriter& aWriter) {
  static const Span<const char> units =
      (GetCycleTimeFrequencyMHz() != 0) ? MakeStringSpan("ns")
                                        : MakeStringSpan("variable CPU cycles");
  aWriter.StringProperty("threadCPUDelta", units);
}

/* static */
uint64_t RunningTimes::ConvertRawToJson(uint64_t aRawValue) {
  static const uint64_t cycleTimeFrequencyMHz = GetCycleTimeFrequencyMHz();
  if (cycleTimeFrequencyMHz == 0u) {
    return aRawValue;
  }

  constexpr uint64_t GHZ_PER_MHZ = 1'000u;
  // To get ns, we need to divide cycles by a frequency in GHz, i.e.:
  // cycles / (f_MHz / GHZ_PER_MHZ). To avoid losing the integer precision of
  // f_MHz, this is computed as (cycles * GHZ_PER_MHZ) / f_MHz.
  // Adding GHZ_PER_MHZ/2 to (cycles * GHZ_PER_MHZ) will round to nearest when
  // the result of the division is truncated.
  return (aRawValue * GHZ_PER_MHZ + (GHZ_PER_MHZ / 2u)) / cycleTimeFrequencyMHz;
}

static inline uint64_t ToNanoSeconds(const FILETIME& aFileTime) {
  // FILETIME values are 100-nanoseconds units, converting
  ULARGE_INTEGER usec = {{aFileTime.dwLowDateTime, aFileTime.dwHighDateTime}};
  return usec.QuadPart * 100;
}

namespace mozilla::profiler {
bool GetCpuTimeSinceThreadStartInNs(
    uint64_t* aResult, const mozilla::profiler::PlatformData& aPlatformData) {
  const HANDLE profiledThread = aPlatformData.ProfiledThread();
  int frequencyInMHz = GetCycleTimeFrequencyMHz();
  if (frequencyInMHz) {
    uint64_t cpuCycleCount;
    if (!QueryThreadCycleTime(profiledThread, &cpuCycleCount)) {
      return false;
    }

    constexpr uint64_t USEC_PER_NSEC = 1000L;
    *aResult = cpuCycleCount * USEC_PER_NSEC / frequencyInMHz;
    return true;
  }

  FILETIME createTime, exitTime, kernelTime, userTime;
  if (!GetThreadTimes(profiledThread, &createTime, &exitTime, &kernelTime,
                      &userTime)) {
    return false;
  }

  *aResult = ToNanoSeconds(kernelTime) + ToNanoSeconds(userTime);
  return true;
}
}  // namespace mozilla::profiler

static RunningTimes GetProcessRunningTimesDiff(
    PSLockRef aLock, RunningTimes& aPreviousRunningTimesToBeUpdated) {
  AUTO_PROFILER_STATS(GetProcessRunningTimes);

  static const HANDLE processHandle = GetCurrentProcess();

  RunningTimes newRunningTimes;
  {
    AUTO_PROFILER_STATS(GetProcessRunningTimes_QueryProcessCycleTime);
    if (ULONG64 cycles; QueryProcessCycleTime(processHandle, &cycles) != 0) {
      newRunningTimes.SetThreadCPUDelta(cycles);
    }
    newRunningTimes.SetPostMeasurementTimeStamp(TimeStamp::Now());
  };

  const RunningTimes diff = newRunningTimes - aPreviousRunningTimesToBeUpdated;
  aPreviousRunningTimesToBeUpdated = newRunningTimes;
  return diff;
}

static RunningTimes GetThreadRunningTimesDiff(
    PSLockRef aLock,
    ThreadRegistration::UnlockedRWForLockedProfiler& aThreadData) {
  AUTO_PROFILER_STATS(GetThreadRunningTimes);

  const mozilla::profiler::PlatformData& platformData =
      aThreadData.PlatformDataCRef();
  const HANDLE profiledThread = platformData.ProfiledThread();

  const RunningTimes newRunningTimes = GetRunningTimesWithTightTimestamp(
      [profiledThread](RunningTimes& aRunningTimes) {
        AUTO_PROFILER_STATS(GetThreadRunningTimes_QueryThreadCycleTime);
        if (ULONG64 cycles;
            QueryThreadCycleTime(profiledThread, &cycles) != 0) {
          aRunningTimes.ResetThreadCPUDelta(cycles);
        } else {
          aRunningTimes.ClearThreadCPUDelta();
        }
      });

  ProfiledThreadData* profiledThreadData =
      aThreadData.GetProfiledThreadData(aLock);
  MOZ_ASSERT(profiledThreadData);
  RunningTimes& previousRunningTimes =
      profiledThreadData->PreviousThreadRunningTimesRef();
  const RunningTimes diff = newRunningTimes - previousRunningTimes;
  previousRunningTimes = newRunningTimes;
  return diff;
}

static void DiscardSuspendedThreadRunningTimes(
    PSLockRef aLock,
    ThreadRegistration::UnlockedRWForLockedProfiler& aThreadData) {
  AUTO_PROFILER_STATS(DiscardSuspendedThreadRunningTimes);

  // On Windows, suspending a thread makes that thread work a little bit. So we
  // want to discard any added running time since the call to
  // GetThreadRunningTimesDiff, which is done by overwriting the thread's
  // PreviousThreadRunningTimesRef() with the current running time now.

  const mozilla::profiler::PlatformData& platformData =
      aThreadData.PlatformDataCRef();
  const HANDLE profiledThread = platformData.ProfiledThread();

  ProfiledThreadData* profiledThreadData =
      aThreadData.GetProfiledThreadData(aLock);
  MOZ_ASSERT(profiledThreadData);
  RunningTimes& previousRunningTimes =
      profiledThreadData->PreviousThreadRunningTimesRef();

  if (ULONG64 cycles; QueryThreadCycleTime(profiledThread, &cycles) != 0) {
    previousRunningTimes.ResetThreadCPUDelta(cycles);
  } else {
    previousRunningTimes.ClearThreadCPUDelta();
  }
}

template <typename Func>
void Sampler::SuspendAndSampleAndResumeThread(
    PSLockRef aLock,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const TimeStamp& aNow, const Func& aProcessRegs) {
  HANDLE profiled_thread = aThreadData.PlatformDataCRef().ProfiledThread();
  if (profiled_thread == nullptr) {
    return;
  }

  // Context used for sampling the register state of the profiled thread.
  CONTEXT context;
  memset(&context, 0, sizeof(context));

  //----------------------------------------------------------------//
  // Suspend the samplee thread and get its context.

  static const DWORD kSuspendFailed = static_cast<DWORD>(-1);
  if (SuspendThread(profiled_thread) == kSuspendFailed) {
    return;
  }

  // SuspendThread is asynchronous, so the thread may still be running.
  // Call GetThreadContext first to ensure the thread is really suspended.
  // See https://blogs.msdn.microsoft.com/oldnewthing/20150205-00/?p=44743.

  // Using only CONTEXT_CONTROL is faster but on 64-bit it causes crashes in
  // RtlVirtualUnwind (see bug 1120126) so we set all the flags.
#if defined(GP_ARCH_amd64)
  context.ContextFlags = CONTEXT_FULL;
#else
  context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
#endif
  if (!GetThreadContext(profiled_thread, &context)) {
    ResumeThread(profiled_thread);
    return;
  }

  //----------------------------------------------------------------//
  // Sample the target thread.

  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
  //
  // The profiler's "critical section" begins here.  We must be very careful
  // what we do here, or risk deadlock.  See the corresponding comment in
  // platform-linux-android.cpp for details.

  Registers regs;
  PopulateRegsFromContext(regs, &context);
  aProcessRegs(regs, aNow);

  //----------------------------------------------------------------//
  // Resume the target thread.

  ResumeThread(profiled_thread);

  // The profiler's critical section ends here.
  //
  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
}

// END Sampler target specifics
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// BEGIN SamplerThread target specifics

static unsigned int __stdcall ThreadEntry(void* aArg) {
  auto thread = static_cast<SamplerThread*>(aArg);
  thread->Run();
  return 0;
}

static unsigned int __stdcall UnregisteredThreadSpyEntry(void* aArg) {
  auto thread = static_cast<SamplerThread*>(aArg);
  thread->RunUnregisteredThreadSpy();
  return 0;
}

SamplerThread::SamplerThread(PSLockRef aLock, uint32_t aActivityGeneration,
                             double aIntervalMilliseconds, uint32_t aFeatures)
    : mSampler(aLock),
      mActivityGeneration(aActivityGeneration),
      mIntervalMicroseconds(
          std::max(1, int(floor(aIntervalMilliseconds * 1000 + 0.5)))),
      mWinSystemTickSampling(
          ProfilerFeature::HasWinSystemTickSampling(aFeatures)),
      mHiResTimer(
          mWinSystemTickSampling
              ? nullptr
              : ::CreateWaitableTimerExA(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS)) {
  if (!mHiResTimer) {
    if ((!mWinSystemTickSampling) && (mIntervalMicroseconds < 10 * 1000)) {
      // By default the timer resolution (which tends to be 1/64Hz, around 16ms)
      // is not changed. However, if the requested interval is sufficiently low,
      // the resolution will be adjusted to match. Note that this affects all
      // timers in Firefox, and could therefore hide issues while profiling.
      // This change may be prevented with the "winsystemticksampling"
      // feature.
      ::timeBeginPeriod(mIntervalMicroseconds / 1000);
    }
  }

  if (ProfilerFeature::HasUnregisteredThreads(aFeatures)) {
    // Sampler&spy threads are not running yet, so it's safe to modify
    // mSpyingState without locking the monitor.
    mSpyingState = SpyingState::Spy_Initializing;
    mUnregisteredThreadSpyThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr,
                       /* stack_size */ 0, UnregisteredThreadSpyEntry, this,
                       /* initflag */ 0, nullptr));
    if (mUnregisteredThreadSpyThread == 0) {
      MOZ_CRASH("_beginthreadex failed");
    }
  }

  // Create a new thread. It is important to use _beginthreadex() instead of
  // the Win32 function CreateThread(), because the CreateThread() does not
  // initialize thread-specific structures in the C runtime library.
  mThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr,
                                                    /* stack_size */ 0,
                                                    ThreadEntry, this,
                                                    /* initflag */ 0, nullptr));
  if (mThread == 0) {
    MOZ_CRASH("_beginthreadex failed");
  }
}

SamplerThread::~SamplerThread() {
  if (mUnregisteredThreadSpyThread) {
    {
      // Make sure the spying thread is not actively working, because the win32
      // function it's using could deadlock with WaitForSingleObject below.
      MonitorAutoLock spyingStateLock{mSpyingStateMonitor};
      while (mSpyingState != SpyingState::Spy_Waiting &&
             mSpyingState != SpyingState::SamplerToSpy_Start) {
        spyingStateLock.Wait();
      }

      mSpyingState = SpyingState::MainToSpy_Shutdown;
      spyingStateLock.NotifyAll();

      do {
        spyingStateLock.Wait();
      } while (mSpyingState != SpyingState::SpyToMain_ShuttingDown);
    }

    WaitForSingleObject(mUnregisteredThreadSpyThread, INFINITE);

    // Close our own handle for the thread.
    if (mUnregisteredThreadSpyThread != kNoThread) {
      CloseHandle(mUnregisteredThreadSpyThread);
    }
  }

  WaitForSingleObject(mThread, INFINITE);

  // Close our own handle for the thread.
  if (mThread != kNoThread) {
    CloseHandle(mThread);
  }

  if (mHiResTimer) {
    CloseHandle(mHiResTimer);
  }

  // Just in the unlikely case some callbacks were added between the end of the
  // thread and now.
  InvokePostSamplingCallbacks(std::move(mPostSamplingCallbackList),
                              SamplingState::JustStopped);
}

void SamplerThread::RunUnregisteredThreadSpy() {
  // TODO: Consider registering this thread.
  // Pros: Remove from list of unregistered threads; Not useful to profiling
  //       Firefox itself.
  // Cons: Doesn't appear in the profile, so users may miss the expensive CPU
  //       cost of this work on Windows.
  PR_SetCurrentThreadName("UnregisteredThreadSpy");

  while (true) {
    {
      MonitorAutoLock spyingStateLock{mSpyingStateMonitor};
      // Either this is the first loop, or we're looping after working.
      MOZ_ASSERT(mSpyingState == SpyingState::Spy_Initializing ||
                 mSpyingState == SpyingState::Spy_Working);

      // Let everyone know we're waiting, and then wait.
      mSpyingState = SpyingState::Spy_Waiting;
      mSpyingStateMonitor.NotifyAll();
      do {
        spyingStateLock.Wait();
      } while (mSpyingState == SpyingState::Spy_Waiting);

      if (mSpyingState == SpyingState::MainToSpy_Shutdown) {
        mSpyingState = SpyingState::SpyToMain_ShuttingDown;
        mSpyingStateMonitor.NotifyAll();
        break;
      }

      MOZ_ASSERT(mSpyingState == SpyingState::SamplerToSpy_Start);
      mSpyingState = SpyingState::Spy_Working;
    }

    // Do the work without lock, so other threads can read the current state.
    SpyOnUnregisteredThreads();
  }
}

void SamplerThread::SleepMicro(uint32_t aMicroseconds) {
  if (mHiResTimer) {
    // duration needs to be in "hundreds of nanoseconds", negative indicates
    // value is relative rather than absolute
    const LARGE_INTEGER duration{
        .QuadPart = static_cast<int64_t>(aMicroseconds) * -10LL};
    const bool b = ::SetWaitableTimerEx(mHiResTimer, &duration, 0, nullptr,
                                        nullptr, nullptr, 0);
    MOZ_RELEASE_ASSERT(b);
    const DWORD r = ::WaitForSingleObject(mHiResTimer, INFINITE);
    MOZ_RELEASE_ASSERT(r == WAIT_OBJECT_0);
  } else {
    // For now, keep the old behaviour of minimum Sleep(1), even for
    // smaller-than-usual sleeps after an overshoot, unless the user has
    // explicitly opted into a sub-millisecond profiler interval.
    if (mIntervalMicroseconds >= 1000) {
      ::Sleep(std::max(1u, aMicroseconds / 1000));
    } else {
      TimeStamp start = TimeStamp::Now();
      TimeStamp end = start + TimeDuration::FromMicroseconds(aMicroseconds);

      // First, sleep for as many whole milliseconds as possible.
      if (aMicroseconds >= 1000) {
        ::Sleep(aMicroseconds / 1000);
      }

      // Then, spin until enough time has passed.
      while (TimeStamp::Now() < end) {
        YieldProcessor();
      }
    }
  }
}

void SamplerThread::Stop(PSLockRef aLock) {
  if (!mHiResTimer) {
    if ((!mWinSystemTickSampling) && (mIntervalMicroseconds < 10 * 1000)) {
      // Disable any timer resolution changes we've made. Do it now while
      // gPSMutex is locked, i.e. before any other SamplerThread can be created
      // and call ::timeBeginPeriod().
      //
      // It's safe to do this now even though this SamplerThread is still
      // alive, because the next time the main loop of Run() iterates it won't
      // get past the mActivityGeneration check, and so it won't make any more
      // ::Sleep() calls.
      ::timeEndPeriod(mIntervalMicroseconds / 1000);
    }
  }

  mSampler.Disable(aLock);
}

// END SamplerThread target specifics
////////////////////////////////////////////////////////////////////////

static void PlatformInit(PSLockRef aLock) {}

#if defined(HAVE_NATIVE_UNWIND)
#  define REGISTERS_SYNC_POPULATE(regs) \
    CONTEXT context;                    \
    RtlCaptureContext(&context);        \
    PopulateRegsFromContext(regs, &context);
#endif
