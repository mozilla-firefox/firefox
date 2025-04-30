/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/StaticPrefs_page_load.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/Unused.h"
#include "mozilla/ipc/IdleSchedulerParent.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/NeverDestroyed.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "nsSystemInfo.h"
#include "nsThreadUtils.h"
#include "nsITimer.h"
#include "nsIThread.h"

namespace mozilla::ipc {

// Shared memory for counting how many child processes are running
// tasks. This memory is shared across all the child processes.
// The [0] is used for counting all the processes and
// [childId] is for counting per process activity.
// This way the global activity can be checked in a fast way by just looking
// at [0] value.
// [1] is used for cpu count for child processes.
static SharedMemoryMappingWithHandle& sActiveChildCounter() {
  static NeverDestroyed<SharedMemoryMappingWithHandle> mapping;
  return *mapping;
}

std::bitset<NS_IDLE_SCHEDULER_COUNTER_ARRAY_LENGHT>
    IdleSchedulerParent::sInUseChildCounters;
MOZ_RUNINIT LinkedList<IdleSchedulerParent>
    IdleSchedulerParent::sIdleAndGCRequests;
int32_t IdleSchedulerParent::sMaxConcurrentIdleTasksInChildProcesses = 1;
uint32_t IdleSchedulerParent::sMaxConcurrentGCs = 1;
uint32_t IdleSchedulerParent::sActiveGCs = 0;
uint32_t IdleSchedulerParent::sChildProcessesRunningPrioritizedOperation = 0;
uint32_t IdleSchedulerParent::sChildProcessesAlive = 0;
nsITimer* IdleSchedulerParent::sStarvationPreventer = nullptr;

uint32_t IdleSchedulerParent::sNumCPUs = 0;
uint32_t IdleSchedulerParent::sPrefConcurrentGCsMax = 0;
uint32_t IdleSchedulerParent::sPrefConcurrentGCsCPUDivisor = 0;

IdleSchedulerParent::IdleSchedulerParent() {
  sChildProcessesAlive++;

  uint32_t max_gcs_pref =
      StaticPrefs::javascript_options_concurrent_multiprocess_gcs_max();
  uint32_t cpu_divisor_pref =
      StaticPrefs::javascript_options_concurrent_multiprocess_gcs_cpu_divisor();
  if (!max_gcs_pref) {
    max_gcs_pref = UINT32_MAX;
  }
  if (!cpu_divisor_pref) {
    cpu_divisor_pref = 4;
  }

  if (!sNumCPUs) {
    // While waiting for the real logical core count behave as if there was
    // just one core.
    sNumCPUs = 1;

    // CollectProcessInfo can be an expensive call, so we dispatch it as a
    // background task and avoid to do so during shutdown.
    if (MOZ_LIKELY(!AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown))) {
      nsCOMPtr<nsIThread> thread = do_GetCurrentThread();
      nsCOMPtr<nsIRunnable> runnable =
          NS_NewRunnableFunction("cpucount getter", [thread]() {
            ProcessInfo processInfo = {};
            if (NS_SUCCEEDED(CollectProcessInfo(processInfo))) {
              uint32_t num_cpus = processInfo.cpuCount;
              // We have a new cpu count, Update the number of idle tasks.
              if (MOZ_LIKELY(!AppShutdown::IsInOrBeyond(
                      ShutdownPhase::XPCOMShutdownThreads))) {
                nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
                    "IdleSchedulerParent::CalculateNumIdleTasks", [num_cpus]() {
                      // We're setting this within this lambda because it's run
                      // on the correct thread and avoids a race.
                      sNumCPUs = num_cpus;

                      // This reads the sPrefConcurrentGCsMax and
                      // sPrefConcurrentGCsCPUDivisor values set below, it will
                      // run after the code that sets those.
                      CalculateNumIdleTasks();
                    });

                thread->Dispatch(runnable, NS_DISPATCH_NORMAL);
              }
            }
          });
      NS_DispatchBackgroundTask(runnable.forget(), NS_DISPATCH_EVENT_MAY_BLOCK);
    }
  }

  if (sPrefConcurrentGCsMax != max_gcs_pref ||
      sPrefConcurrentGCsCPUDivisor != cpu_divisor_pref) {
    // We execute this if these preferences have changed. We also want to make
    // sure it executes for the first IdleSchedulerParent, which it does because
    // sPrefConcurrentGCsMax and sPrefConcurrentGCsCPUDivisor are initially
    // zero.
    sPrefConcurrentGCsMax = max_gcs_pref;
    sPrefConcurrentGCsCPUDivisor = cpu_divisor_pref;

    CalculateNumIdleTasks();
  }
}

void IdleSchedulerParent::CalculateNumIdleTasks() {
  MOZ_ASSERT(sNumCPUs);
  MOZ_ASSERT(sPrefConcurrentGCsMax);
  MOZ_ASSERT(sPrefConcurrentGCsCPUDivisor);

  // On one and two processor (or hardware thread) systems this will
  // allow one concurrent idle task.
  sMaxConcurrentIdleTasksInChildProcesses = int32_t(std::max(sNumCPUs, 1u));
  sMaxConcurrentGCs = std::clamp(sNumCPUs / sPrefConcurrentGCsCPUDivisor, 1u,
                                 sPrefConcurrentGCsMax);

  if (sActiveChildCounter()) {
    sActiveChildCounter()
        .DataAsSpan<Atomic<int32_t>>()[NS_IDLE_SCHEDULER_INDEX_OF_CPU_COUNTER] =
        static_cast<int32_t>(sMaxConcurrentIdleTasksInChildProcesses);
  }
  IdleSchedulerParent::Schedule(nullptr);
}

IdleSchedulerParent::~IdleSchedulerParent() {
  // We can't know if an active process just crashed, so we just always expect
  // that is the case.
  if (mChildId) {
    sInUseChildCounters[mChildId] = false;
    if (sActiveChildCounter()) {
      auto counters = sActiveChildCounter().DataAsSpan<Atomic<int32_t>>();
      if (counters[mChildId]) {
        --counters[NS_IDLE_SCHEDULER_INDEX_OF_ACTIVITY_COUNTER];
        counters[mChildId] = 0;
      }
    }
  }

  if (mRunningPrioritizedOperation) {
    --sChildProcessesRunningPrioritizedOperation;
  }

  if (mDoingGC) {
    // Give back our GC token.
    sActiveGCs--;
  }

  if (mRequestingGC) {
    mRequestingGC.value()(false);
    mRequestingGC = Nothing();
  }

  // Remove from the scheduler's queue.
  if (isInList()) {
    remove();
  }

  MOZ_ASSERT(sChildProcessesAlive > 0);
  sChildProcessesAlive--;
  if (sChildProcessesAlive == 0) {
    MOZ_ASSERT(sIdleAndGCRequests.isEmpty());
    sActiveChildCounter() = nullptr;

    if (sStarvationPreventer) {
      sStarvationPreventer->Cancel();
      NS_RELEASE(sStarvationPreventer);
    }
  }

  Schedule(nullptr);
}

IPCResult IdleSchedulerParent::RecvInitForIdleUse(
    InitForIdleUseResolver&& aResolve) {
  // This must already be non-zero, if it is zero then the cleanup code for the
  // shared memory (initialised below) will never run.  The invariant is that if
  // the shared memory is initialsed, then this is non-zero.
  MOZ_ASSERT(sChildProcessesAlive > 0);

  MOZ_ASSERT(IsNotDoingIdleTask());

  // Create a shared memory object which is shared across all the relevant
  // processes.
  if (!sActiveChildCounter()) {
    size_t shmemSize = NS_IDLE_SCHEDULER_COUNTER_ARRAY_LENGHT * sizeof(int32_t);
    sActiveChildCounter() = shared_memory::Create(shmemSize).MapWithHandle();
    if (sActiveChildCounter()) {
      memset(sActiveChildCounter().Address(), 0, shmemSize);
      sInUseChildCounters[NS_IDLE_SCHEDULER_INDEX_OF_ACTIVITY_COUNTER] = true;
      sInUseChildCounters[NS_IDLE_SCHEDULER_INDEX_OF_CPU_COUNTER] = true;
      sActiveChildCounter().DataAsSpan<Atomic<int32_t>>()
          [NS_IDLE_SCHEDULER_INDEX_OF_CPU_COUNTER] =
          static_cast<int32_t>(sMaxConcurrentIdleTasksInChildProcesses);
    } else {
      sActiveChildCounter() = nullptr;
    }
  }
  MutableSharedMemoryHandle activeCounter =
      sActiveChildCounter() ? sActiveChildCounter().Handle().Clone() : nullptr;

  uint32_t unusedId = 0;
  for (uint32_t i = 0; i < NS_IDLE_SCHEDULER_COUNTER_ARRAY_LENGHT; ++i) {
    if (!sInUseChildCounters[i]) {
      sInUseChildCounters[i] = true;
      unusedId = i;
      break;
    }
  }

  // If there wasn't an empty item, we'll fallback to 0.
  mChildId = unusedId;

  aResolve(
      std::tuple<mozilla::Maybe<MutableSharedMemoryHandle>&&, const uint32_t&>(
          Some(std::move(activeCounter)), mChildId));
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvRequestIdleTime(uint64_t aId,
                                                   TimeDuration aBudget) {
  MOZ_ASSERT(aBudget);
  MOZ_ASSERT(IsNotDoingIdleTask());

  mCurrentRequestId = aId;
  mRequestedIdleBudget = aBudget;

  if (!isInList()) {
    sIdleAndGCRequests.insertBack(this);
  }

  Schedule(this);
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvIdleTimeUsed(uint64_t aId) {
  // The client can either signal that they've used the idle time or they're
  // canceling the request.  We cannot use a seperate cancel message because it
  // could arrive after the parent has granted the request.
  MOZ_ASSERT(IsWaitingForIdle() || IsDoingIdleTask());

  // The parent process will always know the ID of the current request (since
  // the IPC channel is reliable).  The IDs are provided so that the client can
  // check them (it's possible for the client to race ahead of the server).
  MOZ_ASSERT(mCurrentRequestId == aId);

  if (IsWaitingForIdle() && !mRequestingGC) {
    remove();
  }
  mRequestedIdleBudget = TimeDuration();
  Schedule(nullptr);
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvSchedule() {
  Schedule(nullptr);
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvRunningPrioritizedOperation() {
  ++mRunningPrioritizedOperation;
  if (mRunningPrioritizedOperation == 1) {
    ++sChildProcessesRunningPrioritizedOperation;
  }
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvPrioritizedOperationDone() {
  MOZ_ASSERT(mRunningPrioritizedOperation);

  --mRunningPrioritizedOperation;
  if (mRunningPrioritizedOperation == 0) {
    --sChildProcessesRunningPrioritizedOperation;
    Schedule(nullptr);
  }
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvRequestGC(RequestGCResolver&& aResolver) {
  MOZ_ASSERT(!mDoingGC);
  MOZ_ASSERT(!mRequestingGC);

  mRequestingGC = Some(aResolver);
  if (!isInList()) {
    sIdleAndGCRequests.insertBack(this);
  }

  Schedule(nullptr);
  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvStartedGC() {
  if (mDoingGC) {
    return IPC_OK();
  }

  mDoingGC = true;
  sActiveGCs++;

  if (mRequestingGC) {
    // We have to respond to the request before dropping it, even though the
    // content process is already doing the GC.
    mRequestingGC.value()(true);
    mRequestingGC = Nothing();
    if (!IsWaitingForIdle()) {
      remove();
    }
  }

  return IPC_OK();
}

IPCResult IdleSchedulerParent::RecvDoneGC() {
  MOZ_ASSERT(mDoingGC);
  sActiveGCs--;
  mDoingGC = false;
  Schedule(nullptr);
  return IPC_OK();
}

int32_t IdleSchedulerParent::ActiveCount() {
  if (sActiveChildCounter()) {
    return sActiveChildCounter().DataAsSpan<Atomic<int32_t>>()
        [NS_IDLE_SCHEDULER_INDEX_OF_ACTIVITY_COUNTER];
  }
  return 0;
}

bool IdleSchedulerParent::HasSpareCycles(int32_t aActiveCount) {
  // We can run a new task if we have a spare core.  If we're running a
  // prioritised operation we halve the number of regular spare cores.
  //
  // sMaxConcurrentIdleTasksInChildProcesses will always be >0 so on 1 and 2
  // core systems this will allow 1 idle tasks (0 if running a prioritized
  // operation).
  MOZ_ASSERT(sMaxConcurrentIdleTasksInChildProcesses > 0);
  return sChildProcessesRunningPrioritizedOperation
             ? sMaxConcurrentIdleTasksInChildProcesses / 2 > aActiveCount
             : sMaxConcurrentIdleTasksInChildProcesses > aActiveCount;
}

bool IdleSchedulerParent::HasSpareGCCycles() {
  return sMaxConcurrentGCs > sActiveGCs;
}

void IdleSchedulerParent::SendIdleTime() {
  // We would assert that IsWaitingForIdle() except after potentially removing
  // the task from it's list this will return false.  Instead check
  // mRequestedIdleBudget.
  MOZ_ASSERT(mRequestedIdleBudget);
  Unused << SendIdleTime(mCurrentRequestId, mRequestedIdleBudget);
}

void IdleSchedulerParent::SendMayGC() {
  MOZ_ASSERT(mRequestingGC);
  mRequestingGC.value()(true);
  mRequestingGC = Nothing();
  mDoingGC = true;
  sActiveGCs++;
}

void IdleSchedulerParent::Schedule(IdleSchedulerParent* aRequester) {
  // Tasks won't update the active count until after they receive their message
  // and start to run, so make a copy of it here and increment it for every task
  // we schedule. It will become an estimate of how many tasks will be active
  // shortly.
  int32_t activeCount = ActiveCount();

  if (aRequester && aRequester->mRunningPrioritizedOperation) {
    // Prioritised operations are requested only for idle time requests, so this
    // must be an idle time request.
    MOZ_ASSERT(aRequester->IsWaitingForIdle());

    // If the requester is prioritized, just let it run itself.
    if (aRequester->isInList() && !aRequester->mRequestingGC) {
      aRequester->remove();
    }
    aRequester->SendIdleTime();
    activeCount++;
  }

  RefPtr<IdleSchedulerParent> idleRequester = sIdleAndGCRequests.getFirst();

  bool has_spare_cycles = HasSpareCycles(activeCount);
  bool has_spare_gc_cycles = HasSpareGCCycles();

  while (idleRequester && (has_spare_cycles || has_spare_gc_cycles)) {
    // Get the next element before potentially removing the current one from the
    // list.
    RefPtr<IdleSchedulerParent> next = idleRequester->getNext();

    if (has_spare_cycles && idleRequester->IsWaitingForIdle()) {
      // We can run an idle task.
      activeCount++;
      if (!idleRequester->mRequestingGC) {
        idleRequester->remove();
      }
      idleRequester->SendIdleTime();
      has_spare_cycles = HasSpareCycles(activeCount);
    }

    if (has_spare_gc_cycles && idleRequester->mRequestingGC) {
      if (!idleRequester->IsWaitingForIdle()) {
        idleRequester->remove();
      }
      idleRequester->SendMayGC();
      has_spare_gc_cycles = HasSpareGCCycles();
    }

    idleRequester = next;
  }

  if (!sIdleAndGCRequests.isEmpty() && HasSpareCycles(activeCount)) {
    EnsureStarvationTimer();
  }
}

void IdleSchedulerParent::EnsureStarvationTimer() {
  // Even though idle runnables aren't really guaranteed to get run ever (which
  // is why most of them have the timer fallback), try to not let any child
  // process' idle handling to starve forever in case other processes are busy
  if (!sStarvationPreventer) {
    // Reuse StaticPrefs::page_load_deprioritization_period(), since that
    // is used on child side when deciding the minimum idle period.
    NS_NewTimerWithFuncCallback(
        &sStarvationPreventer, StarvationCallback, nullptr,
        StaticPrefs::page_load_deprioritization_period(),
        nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY, "StarvationCallback");
  }
}

void IdleSchedulerParent::StarvationCallback(nsITimer* aTimer, void* aData) {
  RefPtr<IdleSchedulerParent> idleRequester = sIdleAndGCRequests.getFirst();
  while (idleRequester) {
    if (idleRequester->IsWaitingForIdle()) {
      // Treat the first process waiting for idle time as running prioritized
      // operation so that it gets run.
      ++idleRequester->mRunningPrioritizedOperation;
      ++sChildProcessesRunningPrioritizedOperation;
      Schedule(idleRequester);
      --idleRequester->mRunningPrioritizedOperation;
      --sChildProcessesRunningPrioritizedOperation;
      break;
    }

    idleRequester = idleRequester->getNext();
  }
  NS_RELEASE(sStarvationPreventer);
}

}  // namespace mozilla::ipc
