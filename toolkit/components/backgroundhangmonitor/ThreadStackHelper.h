/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ThreadStackHelper_h
#define mozilla_ThreadStackHelper_h

#ifdef MOZ_GECKO_PROFILER

#  include "js/ProfilingStack.h"
#  include "GeckoProfiler.h"
#  include "HangDetails.h"
#  include "mozilla/Span.h"
#  include "nsThread.h"

#  include <stddef.h>

#  if defined(XP_LINUX)
#    include <signal.h>
#    include <semaphore.h>
#    include <sys/types.h>
#  elif defined(XP_WIN)
#    include <windows.h>
#  elif defined(XP_MACOSX)
#    include <mach/mach.h>
#  endif

// Support profiling stack and native stack on these platforms.
#  if defined(XP_LINUX) || defined(XP_WIN) || defined(XP_MACOSX)
#    define MOZ_THREADSTACKHELPER_PROFILING_STACK
#    define MOZ_THREADSTACKHELPER_NATIVE_STACK
#  endif

namespace mozilla {

/**
 * ThreadStackHelper is used to retrieve the profiler's "profiling stack" of a
 * thread, as an alternative of using the profiler to take a profile.
 * The target thread first declares an ThreadStackHelper instance;
 * then another thread can call ThreadStackHelper::GetStack to retrieve
 * the profiling stack of the target thread at that instant.
 *
 * Only non-copying labels are included in the stack, which means labels
 * with custom text and markers are not included.
 */
class ThreadStackHelper : public ProfilerStackCollector {
 private:
  HangStack* mStackToFill;
  Array<char, nsThread::kRunnableNameBufSize>* mRunnableNameBuffer;
  size_t mMaxStackSize;
  size_t mMaxBufferSize;
  size_t mDesiredStackSize;
  size_t mDesiredBufferSize;

  bool PrepareStackBuffer(HangStack& aStack);

 public:
  /**
   * Create a ThreadStackHelper instance targeting the current thread.
   */
  ThreadStackHelper();

  /**
   * Retrieve the current interleaved stack of the thread associated with this
   * ThreadStackHelper.
   *
   * @param aStack        HangStack instance to be filled.
   * @param aRunnableName The name of the current runnable on the target thread.
   * @param aStackWalk    If true, native stack frames will be collected
   *                      along with profiling stack frames.
   */
  void GetStack(HangStack& aStack, nsACString& aRunnableName, bool aStackWalk);

  /**
   * Retrieve the thread's profiler thread ID.
   */
  ProfilerThreadId GetThreadId() const { return mThreadId; }

 protected:
  /**
   * ProfilerStackCollector
   */
  virtual void SetIsMainThread() override;
  virtual void CollectNativeLeafAddr(void* aAddr) override;
  virtual void CollectJitReturnAddr(void* aAddr) override;
  virtual void CollectWasmFrame(JS::ProfilingCategoryPair aCategory,
                                const char* aLabel) override;
  virtual void CollectProfilingStackFrame(
      const js::ProfilingStackFrame& aEntry) override;

 private:
  bool MaybeAppendDynamicStackFrame(mozilla::Span<const char> aBuf);
  void TryAppendFrame(mozilla::HangEntry aFrame);

  // The profiler's unique thread identifier for the target thread.
  ProfilerThreadId mThreadId;
};

}  // namespace mozilla

#endif  // MOZ_GECKO_PROFILER

#endif  // mozilla_ThreadStackHelper_h
