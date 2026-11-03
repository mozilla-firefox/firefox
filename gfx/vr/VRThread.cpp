/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRThread.h"

#include "nsDebug.h"
#include "nsThread.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"

namespace mozilla {

namespace gfx {

static const uint32_t kDefaultThreadLifeTime = 60;  // in 60 seconds.
static const uint32_t kDelayPostTaskTime = 20000;   // in 20000 ms.

VRThread::VRThread(const nsCString& aName)
    : mThread(nullptr), mLifeTime(kDefaultThreadLifeTime), mStarted(false) {
  mName = aName;
}

VRThread::~VRThread() { Shutdown(); }

void VRThread::Start() {
  if (!mThread) {
    nsresult rv = NS_NewNamedThread(mName, getter_AddRefs(mThread));
    MOZ_RELEASE_ASSERT(mThread);

    if (NS_FAILED(rv)) {
      MOZ_ASSERT(false, "Failed to create a vr thread.");
    }
    RefPtr<Runnable> runnable =
        NewRunnableMethod<TimeStamp>("gfx::VRThread::CheckLife", this,
                                     &VRThread::CheckLife, TimeStamp::Now());
    // Post it to the main thread for tracking the lifetime.
    nsCOMPtr<nsIThread> mainThread;
    rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      NS_WARNING("VRThread::Start() could not get Main thread");
      return;
    }
    mainThread->DelayedDispatch(runnable.forget(), kDelayPostTaskTime);
  }
  mStarted = true;
  mLastActiveTime = TimeStamp::Now();
}

void VRThread::Shutdown() {
  if (mThread) {
    if (nsThreadManager::get().IsNSThread()) {
      mThread->Shutdown();
    } else {
      NS_WARNING(
          "VRThread::Shutdown() may only be called from an XPCOM thread");
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "VRThread::Shutdown", [thread = mThread]() { thread->Shutdown(); }));
    }
    mThread = nullptr;
  }
  mStarted = false;
}

const nsCOMPtr<nsIThread> VRThread::GetThread() const { return mThread; }

void VRThread::PostTask(already_AddRefed<Runnable> aTask) {
  PostDelayedTask(std::move(aTask), 0);
}

void VRThread::PostDelayedTask(already_AddRefed<Runnable> aTask,
                               uint32_t aTime) {
  MOZ_RELEASE_ASSERT(mStarted, "Must call Start() before posting tasks.");
  MOZ_RELEASE_ASSERT(mThread);
  mLastActiveTime = TimeStamp::Now();

  if (!aTime) {
    mThread->Dispatch(std::move(aTask), NS_DISPATCH_NORMAL);
  } else {
    mThread->DelayedDispatch(std::move(aTask), aTime);
  }
}

void VRThread::CheckLife(TimeStamp aCheckTimestamp) {
  // VR system is going to shutdown.
  if (!mStarted) {
    Shutdown();
    return;
  }

  const TimeDuration timeout = TimeDuration::FromSeconds(mLifeTime);
  if ((aCheckTimestamp - mLastActiveTime) > timeout) {
    Shutdown();
  } else {
    RefPtr<Runnable> runnable =
        NewRunnableMethod<TimeStamp>("gfx::VRThread::CheckLife", this,
                                     &VRThread::CheckLife, TimeStamp::Now());
    // Post it to the main thread for tracking the lifetime.
    nsCOMPtr<nsIThread> mainThread;
    nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      NS_WARNING("VRThread::CheckLife() could not get Main thread");
      return;
    }
    mainThread->DelayedDispatch(runnable.forget(), kDelayPostTaskTime);
  }
}

void VRThread::SetLifeTime(uint32_t aLifeTime) { mLifeTime = aLifeTime; }

uint32_t VRThread::GetLifeTime() { return mLifeTime; }

bool VRThread::IsActive() { return !!mThread; }

}  // namespace gfx
}  // namespace mozilla
