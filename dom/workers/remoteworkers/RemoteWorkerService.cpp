/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerService.h"

#include "mozilla/dom/PRemoteWorkerParent.h"
#include "mozilla/dom/PRemoteWorkerDebuggerParent.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsIObserverService.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"
#include "nsXPCOMPrivate.h"
#include "RemoteWorkerController.h"
#include "RemoteWorkerDebuggerManagerChild.h"
#include "RemoteWorkerDebuggerManagerParent.h"
#include "RemoteWorkerServiceChild.h"
#include "RemoteWorkerServiceParent.h"

namespace mozilla {

using namespace ipc;

namespace dom {

namespace {

StaticMutex sRemoteWorkerServiceMutex;
StaticRefPtr<RemoteWorkerService> sRemoteWorkerService;

}  // namespace

/**
 * Block shutdown until the RemoteWorkers have shutdown so that we do not try
 * and shutdown the RemoteWorkerService "Worker Launcher" thread until they have
 * cleanly shutdown.
 *
 * Note that this shutdown blocker is not used to initiate shutdown of any of
 * the workers directly; their shutdown is initiated from PBackground in the
 * parent process.  The shutdown blocker just exists to avoid races around
 * shutting down the worker launcher thread after all of the workers have
 * shutdown and torn down their actors.
 *
 * Currently, it should be the case that the ContentParent should want to keep
 * the content processes alive until the RemoteWorkers have all reported their
 * shutdown over IPC (on the "Worker Launcher" thread).  So for an orderly
 * content process shutdown that is waiting for there to no longer be a reason
 * to keep the content process alive, this blocker should only hang around for
 * a brief period of time, helping smooth out lifecycle edge cases.
 *
 * In the event the content process is trying to shutdown while the
 * RemoteWorkers think they should still be alive, it's possible that this
 * blocker could expose the relevant logic error in the parent process if no
 * attempt is made to shutdown the RemoteWorker.
 *
 * ## Major Implementation Note: This is not actually an nsIAsyncShutdownClient
 *
 * Until https://bugzilla.mozilla.org/show_bug.cgi?id=1760855 provides us with a
 * non-JS implementation of nsIAsyncShutdownService, this implementation
 * actually uses event loop spinning.  The patch on
 * https://bugzilla.mozilla.org/show_bug.cgi?id=1775784 that changed us to use
 * this hack can be reverted when the time is right.
 *
 * Event loop spinning is handled by `RemoteWorkerService::Observe` and it calls
 * our exposed `ShouldBlockShutdown()` to know when to stop spinning.
 */
class RemoteWorkerServiceShutdownBlocker final {
  ~RemoteWorkerServiceShutdownBlocker() = default;

 public:
  explicit RemoteWorkerServiceShutdownBlocker(RemoteWorkerService* aService)
      : mService(aService), mBlockShutdown(true) {}

  void RemoteWorkersAllGoneAllowShutdown() {
    mService->FinishShutdown();
    mService = nullptr;

    mBlockShutdown = false;
  }

  bool ShouldBlockShutdown() { return mBlockShutdown; }

  NS_INLINE_DECL_REFCOUNTING(RemoteWorkerServiceShutdownBlocker);

  RefPtr<RemoteWorkerService> mService;
  bool mBlockShutdown;
};

RemoteWorkerServiceKeepAlive::RemoteWorkerServiceKeepAlive(
    RemoteWorkerServiceShutdownBlocker* aBlocker)
    : mBlocker(aBlocker) {
  MOZ_ASSERT(NS_IsMainThread());
}

RemoteWorkerServiceKeepAlive::~RemoteWorkerServiceKeepAlive() {
  // Dispatch a runnable to the main thread to tell the Shutdown Blocker to
  // remove itself and notify the RemoteWorkerService it can finish its
  // shutdown.  We dispatch this to the main thread even if we are already on
  // the main thread.
  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction(__func__, [blocker = std::move(mBlocker)] {
        blocker->RemoteWorkersAllGoneAllowShutdown();
      });
  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));
}

/* static */
void RemoteWorkerService::InitializeParent() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
  MOZ_ASSERT(!sRemoteWorkerService);

  RefPtr<RemoteWorkerService> service = new RemoteWorkerService();

  // ## Parent Process Initialization Case
  //
  // Otherwise we are in the parent process and were invoked by
  // nsLayoutStatics::Initialize.  We wait until profile-after-change to kick
  // off the Worker Launcher thread and have it connect to PBackground.  This is
  // an appropriate time for remote worker APIs to come online, especially
  // because the PRemoteWorkerService mechanism needs processes to eagerly
  // register themselves with PBackground since the design explicitly intends to
  // avoid blocking on the main threads.  (Disclaimer: Currently, things block
  // on the main thread.)

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return;
  }

  nsresult rv = obs->AddObserver(service, "profile-after-change", false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  sRemoteWorkerService = service;
}

/* static */
void RemoteWorkerService::InitializeChild(
    mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint,
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>
        aDebuggerChildEp) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!XRE_IsParentProcess());

  StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
  MOZ_ASSERT(!sRemoteWorkerService);

  RefPtr<RemoteWorkerService> service = new RemoteWorkerService();

  // ## Content Process Initialization Case
  //
  // We are being told to initialize now that we know what our remote type is.
  // Now is a fine time to call InitializeOnMainThread.

  nsresult rv = service->InitializeOnMainThread(std::move(aEndpoint),
                                                std::move(aDebuggerChildEp));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  sRemoteWorkerService = service;
}

/* static */
nsIThread* RemoteWorkerService::Thread() {
  StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
  MOZ_ASSERT(sRemoteWorkerService);
  MOZ_ASSERT(sRemoteWorkerService->mThread);
  return sRemoteWorkerService->mThread;
}

/* static */
void RemoteWorkerService::RegisterRemoteDebugger(
    RemoteWorkerDebuggerInfo aDebuggerInfo,
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent> aDebuggerParentEp) {
  StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
  MOZ_ASSERT(sRemoteWorkerService);
  MOZ_ASSERT(sRemoteWorkerService->mThread);

  // If we are on WorkerLauncher thread, direcly call
  // RemoteWorkerDebuggerManager::SendRegister.
  if (sRemoteWorkerService->mThread->IsOnCurrentThread()) {
    MOZ_ASSERT(sRemoteWorkerService->mDebuggerManagerChild);
    Unused << sRemoteWorkerService->mDebuggerManagerChild->SendRegister(
        std::move(aDebuggerInfo), std::move(aDebuggerParentEp));
    return;
  }

  // For top-level workers in parent process, directly call RecvRegister().
  if (XRE_IsParentProcess() && NS_IsMainThread()) {
    MOZ_ASSERT(sRemoteWorkerService->mDebuggerManagerParent);
    Unused << sRemoteWorkerService->mDebuggerManagerParent->RecvRegister(
        std::move(aDebuggerInfo), std::move(aDebuggerParentEp));
    return;
  }

  // We are on other thread in the case of this is a Child worker. Dispatch this
  // method to WorkerLauncher thread.
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "RemoteWorkerService::RegisterRemoteDebugger",
      [debuggerInfo = std::move(aDebuggerInfo),
       debuggerParentEp = std::move(aDebuggerParentEp)]() mutable {
        RemoteWorkerService::RegisterRemoteDebugger(
            std::move(debuggerInfo), std::move(debuggerParentEp));
      });
  Unused << NS_WARN_IF(
      NS_FAILED(sRemoteWorkerService->mThread->Dispatch(r.forget())));
}

/* static */
already_AddRefed<RemoteWorkerServiceKeepAlive>
RemoteWorkerService::MaybeGetKeepAlive() {
  StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
  // In normal operation no one should be calling this without a service
  // existing, so assert, but we'll also handle this being null as it is a
  // plausible shutdown race.
  MOZ_ASSERT(sRemoteWorkerService);
  if (!sRemoteWorkerService) {
    return nullptr;
  }

  // Note that this value can be null, but this all handles that.
  auto lockedKeepAlive = sRemoteWorkerService->mKeepAlive.Lock();
  RefPtr<RemoteWorkerServiceKeepAlive> extraRef = *lockedKeepAlive;
  return extraRef.forget();
}

nsresult RemoteWorkerService::InitializeOnMainThread(
    mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint,
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>
        aDebuggerChildEp) {
  // I would like to call this thread "DOM Remote Worker Launcher", but the max
  // length is 16 chars.
  nsresult rv = NS_NewNamedThread("Worker Launcher", getter_AddRefs(mThread));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return NS_ERROR_FAILURE;
  }

  rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mShutdownBlocker = new RemoteWorkerServiceShutdownBlocker(this);

  {
    RefPtr<RemoteWorkerServiceKeepAlive> keepAlive =
        new RemoteWorkerServiceKeepAlive(mShutdownBlocker);

    auto lockedKeepAlive = mKeepAlive.Lock();
    *lockedKeepAlive = std::move(keepAlive);
  }

  RefPtr<RemoteWorkerService> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "InitializeThread",
      [self, endpoint = std::move(aEndpoint),
       debuggerChildEp = std::move(aDebuggerChildEp)]() mutable {
        self->InitializeOnTargetThread(std::move(endpoint));

        self->mDebuggerManagerChild =
            MakeRefPtr<RemoteWorkerDebuggerManagerChild>();
        debuggerChildEp.Bind(self->mDebuggerManagerChild);
      });

  rv = mThread->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

RemoteWorkerService::RemoteWorkerService()
    : mKeepAlive(nullptr, "RemoteWorkerService::mKeepAlive") {
  MOZ_ASSERT(NS_IsMainThread());
}

RemoteWorkerService::~RemoteWorkerService() = default;

void RemoteWorkerService::InitializeOnTargetThread(
    mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint) {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  RefPtr<RemoteWorkerServiceChild> serviceActor =
      MakeAndAddRef<RemoteWorkerServiceChild>();
  if (NS_WARN_IF(!aEndpoint.Bind(serviceActor))) {
    return;
  }

  // Now we are ready!
  mActor = serviceActor;
}

void RemoteWorkerService::CloseActorOnTargetThread() {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  // If mActor is nullptr it means that initialization failed.
  if (mActor) {
    // Here we need to shutdown the IPC protocol.
    mActor->Close();
    mActor = nullptr;
  }
}

NS_IMETHODIMP
RemoteWorkerService::Observe(nsISupports* aSubject, const char* aTopic,
                             const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    MOZ_ASSERT(mThread);

    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    }

    // Note that nsObserverList::NotifyObservers will hold a strong reference to
    // our instance throughout the entire duration of this call, so it is not
    // necessary for us to hold a kungFuDeathGrip here.

    // Drop our keep-alive.  This could immediately result in our blocker saying
    // it's okay for us to shutdown.  SpinEventLoopUntil checks the predicate
    // before spinning, so in the ideal case we will not spin the loop at all.
    BeginShutdown();

    MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
        "RemoteWorkerService::Observe"_ns,
        [&]() { return !mShutdownBlocker->ShouldBlockShutdown(); }));

    mShutdownBlocker = nullptr;

    return NS_OK;
  }

  MOZ_ASSERT(!strcmp(aTopic, "profile-after-change"));
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->RemoveObserver(this, "profile-after-change");
  }

  Endpoint<PRemoteWorkerServiceChild> childEp;
  RefPtr<RemoteWorkerServiceParent> parentActor =
      RemoteWorkerServiceParent::CreateForProcess(nullptr, &childEp);
  NS_ENSURE_TRUE(parentActor, NS_ERROR_FAILURE);

  Endpoint<PRemoteWorkerDebuggerManagerChild> debuggerChildEp;
  mDebuggerManagerParent =
      RemoteWorkerDebuggerManagerParent::CreateForProcess(&debuggerChildEp);
  NS_ENSURE_TRUE(mDebuggerManagerParent, NS_ERROR_FAILURE);

  return InitializeOnMainThread(std::move(childEp), std::move(debuggerChildEp));
}

void RemoteWorkerService::BeginShutdown() {
  // Drop our keepalive reference which may allow near-immediate removal of the
  // blocker.
  auto lockedKeepAlive = mKeepAlive.Lock();
  *lockedKeepAlive = nullptr;
}

void RemoteWorkerService::FinishShutdown() {
  // Clear the singleton before spinning the event loop when shutting down the
  // thread so that MaybeGetKeepAlive() can assert if there are any late calls
  // and to better reflect the actual state.
  //
  // Our caller, the RemoteWorkerServiceShutdownBlocker, will continue to hold a
  // strong reference to us until we return from this call, so there are no
  // lifecycle implications to dropping this reference.
  {
    StaticMutexAutoLock lock(sRemoteWorkerServiceMutex);
    sRemoteWorkerService = nullptr;
  }

  RefPtr<RemoteWorkerService> self = this;
  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction("RemoteWorkerService::CloseActorOnTargetThread",
                             [self]() { self->CloseActorOnTargetThread(); });

  mThread->Dispatch(r.forget(), NS_DISPATCH_NORMAL);

  // We've posted a shutdown message; now shutdown the thread.  This will spin
  // a nested event loop waiting for the thread to process all pending events
  // (including the just dispatched CloseActorOnTargetThread which will close
  // the actor), ensuring to block main thread shutdown long enough to avoid
  // races.
  mThread->Shutdown();
  mThread = nullptr;
}

NS_IMPL_ISUPPORTS(RemoteWorkerService, nsIObserver)

}  // namespace dom
}  // namespace mozilla
