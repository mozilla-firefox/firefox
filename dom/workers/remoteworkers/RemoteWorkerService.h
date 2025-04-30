/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerService_h
#define mozilla_dom_RemoteWorkerService_h

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/DataMutex.h"
#include "mozilla/ipc/Endpoint.h"
#include "nsCOMPtr.h"
#include "nsIObserver.h"
#include "nsISupportsImpl.h"
#include "mozilla/dom/RemoteWorkerTypes.h"

class nsIThread;

namespace mozilla::dom {

class RemoteWorkerDebuggerManagerChild;
class RemoteWorkerDebuggerManagerParent;
class RemoteWorkerService;
class RemoteWorkerServiceChild;
class RemoteWorkerServiceShutdownBlocker;
class PRemoteWorkerDebuggerManagerChild;
class PRemoteWorkerDebuggerParent;
class PRemoteWorkerServiceChild;

/**
 * Refcounted lifecycle helper; when its refcount goes to zero its destructor
 * will call RemoteWorkerService::Shutdown() which will remove the shutdown
 * blocker and shutdown the "Worker Launcher" thread.
 *
 * The RemoteWorkerService itself will hold a reference to this singleton which
 * it will use to hand out additional refcounts to RemoteWorkerChild instances.
 * When the shutdown blocker is notified that it's time to shutdown, the
 * RemoteWorkerService's reference will be dropped.
 */
class RemoteWorkerServiceKeepAlive {
 public:
  explicit RemoteWorkerServiceKeepAlive(
      RemoteWorkerServiceShutdownBlocker* aBlocker);

 private:
  ~RemoteWorkerServiceKeepAlive();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerServiceKeepAlive);

  RefPtr<RemoteWorkerServiceShutdownBlocker> mBlocker;
};

/**
 * Every process has a RemoteWorkerService which does the actual spawning of
 * RemoteWorkerChild instances. The RemoteWorkerService creates a "Worker
 * Launcher" thread at initialization on which it creates a
 * RemoteWorkerServiceChild to service spawn requests. The thread is exposed as
 * RemoteWorkerService::Thread(). A new/distinct thread is used because we
 * (eventually) don't want to deal with main-thread contention, content
 * processes have no equivalent of a PBackground thread, and actors are bound to
 * specific threads.
 *
 * (Disclaimer: currently most RemoteWorkerOps need to happen on the main thread
 * because the main-thread ends up as the owner of the worker and all
 * manipulation of the worker must happen from the owning thread.)
 */
class RemoteWorkerService final : public nsIObserver {
  friend class RemoteWorkerServiceShutdownBlocker;
  friend class RemoteWorkerServiceKeepAlive;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  // To be called when a process is initialized on main-thread.
  static void InitializeParent();
  static void InitializeChild(
      mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>
          aDebuggerChildEp);

  static nsIThread* Thread();
  static void RegisterRemoteDebugger(
      RemoteWorkerDebuggerInfo aDebuggerInfo,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent> aDebuggerParentEp);

  // Called by RemoteWorkerChild instances on the "Worker Launcher" thread at
  // their creation to assist in tracking when it's safe to shutdown the
  // RemoteWorkerService and "Worker Launcher" thread.  This method will return
  // a null pointer if the RemoteWorkerService has already begun shutdown.
  //
  // This is somewhat awkwardly a static method because the RemoteWorkerChild
  // instances are not managed by RemoteWorkerServiceChild, but instead are
  // managed by PBackground(Child).  So we either need to find the
  // RemoteWorkerService via the hidden singleton or by having the
  // RemoteWorkerChild use PBackgroundChild::ManagedPRemoteWorkerServiceChild()
  // to locate the instance.  We are choosing to use the singleton because we
  // already need to acquire a mutex in the call regardless and the upcoming
  // refactorings may want to start using new toplevel protocols and this will
  // avoid requiring a change when that happens.
  static already_AddRefed<RemoteWorkerServiceKeepAlive> MaybeGetKeepAlive();

 private:
  RemoteWorkerService();
  ~RemoteWorkerService();

  nsresult InitializeOnMainThread(
      mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint,
      mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>
          aDebuggerChildEp);

  void InitializeOnTargetThread(
      mozilla::ipc::Endpoint<PRemoteWorkerServiceChild> aEndpoint);

  void CloseActorOnTargetThread();

  // Called by RemoteWorkerServiceShutdownBlocker when it's time to drop the
  // RemoteWorkerServiceKeepAlive reference.
  void BeginShutdown();

  // Called by RemoteWorkerServiceShutdownBlocker when the blocker has been
  // removed and it's safe to shutdown the "Worker Launcher" thread.
  void FinishShutdown();

  nsCOMPtr<nsIThread> mThread;
  RefPtr<RemoteWorkerServiceChild> mActor;
  RefPtr<RemoteWorkerDebuggerManagerChild> mDebuggerManagerChild;
  RefPtr<RemoteWorkerDebuggerManagerParent> mDebuggerManagerParent;
  // The keep-alive is set and cleared on the main thread but we will hand out
  // additional references to it from the "Worker Launcher" thread, so it's
  // appropriate to use a mutex.  (Alternately we could have used a ThreadBound
  // and set and cleared on the "Worker Launcher" thread, but that would
  // involve more moving parts and could have complicated edge cases.)
  DataMutex<RefPtr<RemoteWorkerServiceKeepAlive>> mKeepAlive;
  // In order to poll the blocker to know when we can stop spinning the event
  // loop at shutdown, we retain a reference to the blocker.
  RefPtr<RemoteWorkerServiceShutdownBlocker> mShutdownBlocker;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_RemoteWorkerService_h
