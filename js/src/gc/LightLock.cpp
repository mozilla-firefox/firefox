/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/LightLock.h"

#include "mozilla/Atomics.h"
#include "mozilla/TimeStamp.h"

#include <thread>

#include "gc/GCRuntime.h"
#include "threading/LockGuard.h"
#include "util/WindowsWrapper.h"
#include "vm/MutexIDs.h"
#include "vm/Runtime.h"

using namespace js;

js::LightLockRuntime::LightLockRuntime() : mutex(mutexid::GCLightLock) {}

/* static */
LightLockRuntime* js::LightLockRuntime::from(JSRuntime* runtime) {
  return &runtime->gc.lightLockRuntime;
}

MOZ_NEVER_INLINE void js::LightLock::lockSlow(JSRuntime* runtime) {
  uint32_t spinCounter = 0;

  for (;;) {
    uint32_t currentState = state;

    // No one else can be waiting yet.
    MOZ_ASSERT(!(currentState & HasWaiter));

    // If the mutex is unlocked, attempt to lock it.
    if (currentState == UnlockedState &&
        state.compareExchange(UnlockedState, LockedState)) {
      break;
    }

    // It's locked. Try spinning a few times.
    if (spin(spinCounter)) {
      continue;
    }

    // Otherwise wait on the underlying condition variable.
    if (tryBlockUntilWoken(runtime)) {
      break;
    }

    // If the mutex was unlocked in the meantime, restart.
    spinCounter = 0;
  }

  MOZ_ASSERT(isLocked());
}

bool js::LightLock::tryBlockUntilWoken(JSRuntime* runtime) {
  LightLockRuntime* llrt = LightLockRuntime::from(runtime);
  LockGuard<Mutex> lock(llrt->mutex);

  if (!state.compareExchange(LockedState, LockedWithWaiterState)) {
    return false;  // State changed while we waited for the mutex.
  }

  bool waiting = true;
  MOZ_ASSERT(!llrt->waitingPtr);
  llrt->waitingPtr = &waiting;

  auto wasWoken = [&]() {
    // Check whether wakeOtherThread cleared |waitingPtr|.
    return !waiting;
  };

#ifdef DEBUG
  mozilla::TimeDuration duration = mozilla::TimeDuration::FromSeconds(10);
  if (!llrt->condVar.wait_for(lock, duration, wasWoken)) {
    MOZ_CRASH_UNSAFE_PRINTF("Timeout waiting on LightLock in state %u\n",
                            unsigned(state));
  }
#else
  llrt->condVar.wait(lock, wasWoken);
#endif

  return true;
}

MOZ_NEVER_INLINE void js::LightLock::unlockSlow(JSRuntime* runtime) {
  MOZ_ASSERT(hasWaiter());
  LightLockRuntime* llrt = LightLockRuntime::from(runtime);
  LockGuard<Mutex> lock(llrt->mutex);
  // Hand over the lock to the waiting thread: clear the waiting state and wake
  // it up.
  MOZ_ALWAYS_TRUE(state.compareExchange(LockedWithWaiterState, LockedState));
  wakeOtherThread(runtime, lock);
}

void js::LightLock::wakeOtherThread(JSRuntime* runtime,
                                    const LockGuard<Mutex>& lock) {
  LightLockRuntime* llrt = LightLockRuntime::from(runtime);
  MOZ_ASSERT(llrt->waitingPtr);
  MOZ_ASSERT(*llrt->waitingPtr);
  *llrt->waitingPtr = false;
  llrt->waitingPtr = nullptr;
  llrt->condVar.notify_one();
}

bool js::LightLock::spin(uint32_t& counter) {
  if (counter >= 10) {
    return false;
  }

  counter++;

  if (counter <= 3) {
    mozilla::cpu_pause();
  } else {
    std::this_thread::yield();
  }

  return true;
}
