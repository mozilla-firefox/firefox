/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerDebuggerManagerParent.h"

#include "RemoteWorkerDebuggerParent.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/WorkerDebuggerManager.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "nsContentUtils.h"

namespace mozilla::dom {

/* static */
RefPtr<RemoteWorkerDebuggerManagerParent>
RemoteWorkerDebuggerManagerParent::CreateForProcess(
    ContentParent* aProcess,
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>* aChildEp) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());

  mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerParent> parentEp;
  nsresult rv =
      PRemoteWorkerDebuggerManager::CreateEndpoints(&parentEp, aChildEp);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  RefPtr<RemoteWorkerDebuggerManagerParent> actor =
      MakeRefPtr<RemoteWorkerDebuggerManagerParent>(
          aProcess ? aProcess->ThreadsafeHandle() : nullptr);
  parentEp.Bind(actor);

  return actor;
}

RemoteWorkerDebuggerManagerParent::RemoteWorkerDebuggerManagerParent(
    ThreadsafeContentParentHandle* aProcess)
    : mProcess(aProcess) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
}

RemoteWorkerDebuggerManagerParent::~RemoteWorkerDebuggerManagerParent() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());
}

mozilla::ipc::IPCResult RemoteWorkerDebuggerManagerParent::RecvRegister(
    const RemoteWorkerDebuggerInfo& aDebuggerInfo,
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent>&& aParentEp) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());

  if (!aParentEp.IsValid()) {
    return IPC_FAIL(
        this, "Invalid Parent Endpoint for RemoteWorkerDebuggerParent...");
  }

  if (mProcess) {
    // A dedicated worker registers before its script loads, with a principal
    // derived from the requested script URL, which may be cross-origin and is
    // rejected later by the load. Only the principals of shared and service
    // workers, which the parent chose the process for, can be validated here.
    if (aDebuggerInfo.type() != WorkerKindDedicated &&
        !mProcess->ValidatePrincipal(
            aDebuggerInfo.principal(),
            {ValidatePrincipalOptions::AllowNotLoadedOrigin,
             ValidatePrincipalOptions::AlwaysAllowSystem})) {
      return ContentParent::PrincipalValidationIpcFail(
          aDebuggerInfo.principal(), this, __func__);
    }
    if (!ValidateDebuggerInfo(aDebuggerInfo)) {
      return IPC_FAIL(this, "Invalid RemoteWorkerDebuggerInfo");
    }
  }

  RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::GetOrCreate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(manager);

  RefPtr<RemoteWorkerDebuggerParent> debugger =
      MakeRefPtr<RemoteWorkerDebuggerParent>(aDebuggerInfo,
                                             std::move(aParentEp), mProcess);

  manager->RegisterDebugger(debugger);

  MOZ_ASSERT(debugger->CanSend());
  (void)debugger->SendRegisterDone();

  return IPC_OK();
}

bool RemoteWorkerDebuggerManagerParent::ValidateDebuggerInfo(
    const RemoteWorkerDebuggerInfo& aDebuggerInfo) {
  MOZ_ASSERT(mProcess);

  if (aDebuggerInfo.type() > WorkerKindService) {
    return false;
  }

  if (aDebuggerInfo.isChrome() &&
      !aDebuggerInfo.principal()->IsSystemPrincipal()) {
    return false;
  }

  // A dedicated worker runs in the same process as its owner window, and inner
  // window ids encode the process that minted them. UINT64_MAX means the worker
  // has no owner window.
  const uint64_t windowID = aDebuggerInfo.windowID();
  if (aDebuggerInfo.type() == WorkerKindDedicated && windowID &&
      windowID != UINT64_MAX &&
      !nsContentUtils::IsProcessSpecificIdFrom(windowID, mProcess->ChildID())) {
    return false;
  }

  if (aDebuggerInfo.Id().IsEmpty()) {
    return false;
  }

  // A process may not claim the id of a worker from another process, neither as
  // its own id nor as its parent's.
  RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::GetOrCreate();
  if (!IsIdUsableByThisProcess(manager, aDebuggerInfo.Id())) {
    return false;
  }
  if (!aDebuggerInfo.parentId().IsEmpty() &&
      !IsIdUsableByThisProcess(manager, aDebuggerInfo.parentId())) {
    return false;
  }

  return true;
}

bool RemoteWorkerDebuggerManagerParent::IsIdUsableByThisProcess(
    WorkerDebuggerManager* aManager, const nsString& aId) {
  nsCOMPtr<nsIWorkerDebugger> existing = aManager->GetDebuggerById(aId);
  // GetDebuggerById() only returns RemoteWorkerDebuggerParent instances.
  return !existing ||
         static_cast<RemoteWorkerDebuggerParent*>(existing.get())->Process() ==
             mProcess;
}

}  // end of namespace mozilla::dom
