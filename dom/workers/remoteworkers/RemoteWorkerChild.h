/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_RemoteWorkerChild_h
#define mozilla_dom_RemoteWorkerChild_h

#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

#include "mozilla/DataMutex.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThreadBound.h"
#include "mozilla/dom/PRemoteWorkerChild.h"
#include "mozilla/dom/RemoteWorkerOp.h"
#include "mozilla/dom/PRemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/ServiceWorkerOpArgs.h"
#include "mozilla/dom/SharedWorkerOpArgs.h"

class nsISerialEventTarget;
class nsIConsoleReportCollector;

namespace mozilla::dom {

using remoteworker::RemoteWorkerState;

class ErrorValue;
class FetchEventOpProxyChild;
class RemoteWorkerData;
class RemoteWorkerServiceKeepAlive;
class ServiceWorkerOp;
class SharedWorkerOp;
class UniqueMessagePortId;
class WeakWorkerRef;
class WorkerErrorReport;
class WorkerPrivate;

/**
 * Background-managed "Worker Launcher"-thread-resident created via the
 * RemoteWorkerManager to actually spawn the worker. Currently, the worker will
 * be spawned from the main thread due to nsIPrincipal not being able to be
 * created on background threads and other ownership invariants, most of which
 * can be relaxed in the future.
 */
class RemoteWorkerChild final : public PRemoteWorkerChild {
  friend class FetchEventOpProxyChild;
  friend class PRemoteWorkerChild;
  friend class ServiceWorkerOp;
  friend class SharedWorkerOp;

  ~RemoteWorkerChild();

 public:
  // Note that all IPC-using methods must only be invoked on the
  // RemoteWorkerService thread which the inherited
  // IProtocol::GetActorEventTarget() will return for us.
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RemoteWorkerChild, final)

  explicit RemoteWorkerChild(const RemoteWorkerData& aData);

  void ExecWorker(
      const RemoteWorkerData& aData,
      mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
          aChildEp);

  void ErrorPropagationOnMainThread(const WorkerErrorReport* aReport,
                                    bool aIsErrorEvent);

  void CSPViolationPropagationOnMainThread(const nsAString& aJSON);

  void NotifyLock(bool aCreated);

  void NotifyWebTransport(bool aCreated);

  void FlushReportsOnMainThread(nsIConsoleReportCollector* aReporter);

  RefPtr<GenericNonExclusivePromise> GetTerminationPromise();

  RefPtr<GenericPromise> MaybeSendSetServiceWorkerSkipWaitingFlag();

  const nsTArray<uint64_t>& WindowIDs() const { return mWindowIDs; }

  void SetIsThawing(const bool aIsThawing) { mIsThawing = aIsThawing; }
  bool IsThawing() const { return mIsThawing; }
  void PendRemoteWorkerOp(RefPtr<RemoteWorkerOp> aOp);
  void RunAllPendingOpsOnMainThread();

 private:
  class InitializeWorkerRunnable;

  // The state of the WorkerPrivate as perceived by the owner on the main
  // thread.  All state transitions now happen on the main thread, but the
  // Worker Launcher thread will consult the state and will directly append ops
  // to the Pending queue
  DataMutex<RemoteWorkerState> mState;

  const RefPtr<RemoteWorkerServiceKeepAlive> mServiceKeepAlive;

  void ActorDestroy(ActorDestroyReason) override;

  mozilla::ipc::IPCResult RecvExecOp(SharedWorkerOpArgs&& aOpArgs);

  mozilla::ipc::IPCResult RecvExecServiceWorkerOp(
      ServiceWorkerOpArgs&& aArgs, ExecServiceWorkerOpResolver&& aResolve);

  already_AddRefed<PFetchEventOpProxyChild> AllocPFetchEventOpProxyChild(
      const ParentToChildServiceWorkerFetchEventOpArgs& aArgs);

  mozilla::ipc::IPCResult RecvPFetchEventOpProxyConstructor(
      PFetchEventOpProxyChild* aActor,
      const ParentToChildServiceWorkerFetchEventOpArgs& aArgs) override;

  nsresult ExecWorkerOnMainThread(
      RemoteWorkerData&& aData,
      mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
          aChildEp);

  void ExceptionalErrorTransitionDuringExecWorker();

  void RequestWorkerCancellation();

  void InitializeOnWorker();

  void CreationSucceededOnAnyThread();

  void CreationFailedOnAnyThread();

  void CreationSucceededOrFailedOnAnyThread(bool aDidCreationSucceed);

  // Cancels the worker if it has been started and ensures that we transition
  // to the Terminated state once the worker has been terminated or we have
  // ensured that it will never start.
  void CloseWorkerOnMainThread();

  void ErrorPropagation(const ErrorValue& aValue);

  void ErrorPropagationDispatch(nsresult aError);

  // When the WorkerPrivate Cancellation lambda is invoked, it's possible that
  // we have not yet advanced to running from pending, so we could be in either
  // state.  This method is expected to be called by the Workers' cancellation
  // lambda and will obtain the lock and call the
  // TransitionStateFromPendingToCanceled if appropriate.  Otherwise it will
  // directly move from the running state to the canceled state which does not
  // require additional cleanup.
  void OnWorkerCancellationTransitionStateFromPendingOrRunningToCanceled();
  // A helper used by the above method by the worker cancellation lambda if the
  // the worker hasn't started running, or in exceptional cases where we bail
  // out of the ExecWorker method early.  The caller must be holding the lock
  // (in order to pass in the state).
  void TransitionStateFromPendingToCanceled(RemoteWorkerState& aState);
  void TransitionStateFromCanceledToKilled();

  void TransitionStateToRunning();

  void TransitionStateToTerminated();

  void TransitionStateToTerminated(RemoteWorkerState& aState);

  void CancelAllPendingOps(RemoteWorkerState& aState);

  void MaybeStartOp(RefPtr<RemoteWorkerOp>&& aOp);

  const bool mIsServiceWorker;

  // Touched on main-thread only.
  nsTArray<uint64_t> mWindowIDs;

  struct LauncherBoundData {
    MozPromiseHolder<GenericNonExclusivePromise> mTerminationPromise;
    // Flag to ensure we report creation at most once.  This could be cleaned up
    // further.
    bool mDidSendCreated = false;
  };

  ThreadBound<LauncherBoundData> mLauncherData;

  // Thaw operation holds mState.lock. It means other operations will be blocked
  // until mState.lock is released. However, Thaw operation is blocked by
  // RemoteWorkerDebugger registration that needs WorkerLauncher thread to send
  // IPC to continue the registration on the parent process. If a RemoteWorkerOp
  // is received on WorkerLauncher thread when the RemoteWorker is thawing, a
  // deadlock could be happen between WorkerLauncher thread and RemoteWorker's
  // parent thread. So mIsThawing and mPendingOps are introduced to avoid the
  // deadlock by pending the operations when RemoteWorker is thawing.
  //
  // Note that these could be removed once RemoteWorkerChild off-main-thread
  // done since the RemoteWorker's parent thread will be WorkerLauncher thread.
  // And it means when executing WorkerPrivate::Thaw on WorkerLauncher thread,
  // it is impossible to handle the IPC callback on WorkerLauncher thread at the
  // same time.
  Atomic<bool> mIsThawing{false};
  DataMutex<nsTArray<RefPtr<RemoteWorkerOp>>> mPendingOps;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_RemoteWorkerChild_h
