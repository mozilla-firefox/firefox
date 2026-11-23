/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecodePool.h"

#include <algorithm>

#include "Decoder.h"
#include "IDecodingTask.h"
#include "RasterImage.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Monitor.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/TaskController.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/Logging.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsIObserverService.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"
#include "nsXPCOMCIDInternal.h"
#include "prsystem.h"

#if defined(XP_WIN)
#  include <objbase.h>

#  include "mozilla/WindowsProcessMitigations.h"
#endif

using std::max;
using std::min;

namespace mozilla {
namespace image {

///////////////////////////////////////////////////////////////////////////////
// DecodePool implementation.
///////////////////////////////////////////////////////////////////////////////

/* static */
StaticRefPtr<DecodePool> DecodePool::sSingleton;
/* static */
uint32_t DecodePool::sNumCores = 0;

NS_IMPL_ISUPPORTS(DecodePool, nsIObserver)

/* static */
void DecodePool::Initialize() {
  MOZ_ASSERT(NS_IsMainThread());
  sNumCores = max<int32_t>(PR_GetNumberOfProcessors(), 1);
  DecodePool::Singleton();
}

/* static */
DecodePool* DecodePool::Singleton() {
  if (!sSingleton) {
    MOZ_ASSERT(NS_IsMainThread());
    sSingleton = new DecodePool();
    ClearOnShutdown(&sSingleton);
  }

  return sSingleton;
}

/* static */
uint32_t DecodePool::NumberOfCores() { return sNumCores; }

#if defined(XP_WIN)
class IOThreadIniter final : public Runnable {
 public:
  explicit IOThreadIniter() : Runnable("image::IOThreadIniter") {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(!NS_IsMainThread());

    CoInitialize(nullptr);

    return NS_OK;
  }
};
#endif

DecodePool::DecodePool() : mMutex("image::IOThread") {
  // Initialize the I/O thread.
#if defined(XP_WIN)
  // On Windows we use the io thread to get icons from the system. Any thread
  // that makes system calls needs to call CoInitialize. And these system calls
  // (SHGetFileInfo) should only be called from one thread at a time, in case
  // we ever create more than one io thread. If win32k is locked down, we can't
  // call SHGetFileInfo anyway, so we don't need the initializer.
  nsCOMPtr<nsIRunnable> initer =
      IsWin32kLockedDown() ? nullptr : new IOThreadIniter();
  nsresult rv = NS_NewNamedThread("ImageIO", getter_AddRefs(mIOThread), initer);
#else
  nsresult rv = NS_NewNamedThread("ImageIO", getter_AddRefs(mIOThread));
#endif
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv) && mIOThread,
                     "Should successfully create image I/O thread");

  nsCOMPtr<nsIObserverService> obsSvc = services::GetObserverService();
  if (obsSvc) {
    obsSvc->AddObserver(this, "xpcom-shutdown-threads", false);
  }
}

DecodePool::~DecodePool() {
  MOZ_ASSERT(NS_IsMainThread(), "Must shut down DecodePool on main thread!");
}

NS_IMETHODIMP
DecodePool::Observe(nsISupports*, const char* aTopic, const char16_t*) {
  MOZ_ASSERT(strcmp(aTopic, "xpcom-shutdown-threads") == 0, "Unexpected topic");

  mShuttingDown = true;

  nsCOMPtr<nsIThread> ioThread;

  {
    MutexAutoLock lock(mMutex);
    ioThread.swap(mIOThread);
#ifdef XP_WIN
    if (mShellCallStart) {
      gfxCriticalNote << "ImageIO shell call in flight for "
                      << (AwakeTimeStamp::Now() - *mShellCallStart).ToSeconds()
                      << "s"
                      << (mShellCallStartedInShutdown ? ", started in shutdown"
                                                      : "");
    }
#endif
  }

  if (ioThread) {
    ioThread->Shutdown();
  }

  return NS_OK;
}

/* static */ bool DecodePool::IsShuttingDown() {
  if (MOZ_UNLIKELY(!sSingleton)) {
    return AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads);
  }

  return sSingleton->mShuttingDown;
}

class DecodingTask final : public Task {
 public:
  explicit DecodingTask(RefPtr<IDecodingTask>&& aTask)
      : Task(Kind::OffMainThreadOnly, aTask->Priority() == TaskPriority::eLow
                                          ? EventQueuePriority::Normal
                                          : EventQueuePriority::RenderBlocking),
        mTask(aTask) {}

  TaskResult Run() override {
    if (MOZ_LIKELY(!DecodePool::IsShuttingDown())) {
      mTask->Run();
    }
    return TaskResult::Complete;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("ImageDecodingTask");
    return true;
  }
#endif

 private:
  RefPtr<IDecodingTask> mTask;
};

void DecodePool::AsyncRun(IDecodingTask* aTask) {
  MOZ_ASSERT(aTask);

  TaskController::Get()->AddTask(
      MakeAndAddRef<DecodingTask>((RefPtr<IDecodingTask>(aTask))));
}

bool DecodePool::SyncRunIfPreferred(IDecodingTask* aTask, nsIURI* aURI) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTask);

  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING(
      "DecodePool::SyncRunIfPreferred", GRAPHICS,
      nsContentUtils::TruncatedURLForDisplay(aURI, 1024));

  if (aTask->ShouldPreferSyncRun()) {
    aTask->Run();
    return true;
  }

  AsyncRun(aTask);
  return false;
}

void DecodePool::SyncRunIfPossible(IDecodingTask* aTask, nsIURI* aURI) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTask);

  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING(
      "DecodePool::SyncRunIfPossible", GRAPHICS,
      nsContentUtils::TruncatedURLForDisplay(aURI, 1024));

  aTask->Run();
}

already_AddRefed<nsISerialEventTarget> DecodePool::GetIOEventTarget() {
  MutexAutoLock threadPoolLock(mMutex);
  nsCOMPtr<nsISerialEventTarget> target = mIOThread;
  return target.forget();
}

#ifdef XP_WIN
/* static */ void DecodePool::NoteShellCallStarted() {
  if (!sSingleton) {
    return;
  }
  MutexAutoLock lock(sSingleton->mMutex);
  sSingleton->mShellCallStart = Some(AwakeTimeStamp::Now());
  sSingleton->mShellCallStartedInShutdown =
      AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed);
}

/* static */ void DecodePool::NoteShellCallFinished() {
  if (!sSingleton) {
    return;
  }
  MutexAutoLock lock(sSingleton->mMutex);
  sSingleton->mShellCallStart.reset();
  sSingleton->mShellCallStartedInShutdown = false;
}
#endif

}  // namespace image
}  // namespace mozilla
