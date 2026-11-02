/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerDebuggerManagerParent.h"

#include "RemoteWorkerDebuggerParent.h"
#include "mozilla/dom/WorkerDebuggerManager.h"

namespace mozilla::dom {

/* static */
RefPtr<RemoteWorkerDebuggerManagerParent>
RemoteWorkerDebuggerManagerParent::CreateForProcess(
    mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerChild>* aChildEp) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess() && NS_IsMainThread());

  mozilla::ipc::Endpoint<PRemoteWorkerDebuggerManagerParent> parentEp;
  nsresult rv =
      PRemoteWorkerDebuggerManager::CreateEndpoints(&parentEp, aChildEp);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  RefPtr<RemoteWorkerDebuggerManagerParent> actor =
      MakeRefPtr<RemoteWorkerDebuggerManagerParent>();
  parentEp.Bind(actor);

  return actor;
}

RemoteWorkerDebuggerManagerParent::RemoteWorkerDebuggerManagerParent() {
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

  RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::GetOrCreate();
  MOZ_ASSERT_DEBUG_OR_FUZZING(manager);

  RefPtr<RemoteWorkerDebuggerParent> debugger =
      MakeRefPtr<RemoteWorkerDebuggerParent>(aDebuggerInfo,
                                             std::move(aParentEp));

  manager->RegisterDebugger(debugger);

  MOZ_ASSERT(debugger->CanSend());
  (void)debugger->SendRegisterDone();

  return IPC_OK();
}

}  // end of namespace mozilla::dom
