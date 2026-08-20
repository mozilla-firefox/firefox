/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerServiceChild.h"

#include "RemoteWorkerChild.h"
#include "RemoteWorkerController.h"

namespace mozilla::dom {

RemoteWorkerServiceChild::RemoteWorkerServiceChild() = default;

RemoteWorkerServiceChild::~RemoteWorkerServiceChild() = default;

already_AddRefed<PRemoteWorkerChild>
RemoteWorkerServiceChild::AllocPRemoteWorkerChild(
    const RemoteWorkerData& aData,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&
        aChildEp) {
  return MakeAndAddRef<RemoteWorkerChild>(aData);
}

mozilla::ipc::IPCResult RemoteWorkerServiceChild::RecvPRemoteWorkerConstructor(
    PRemoteWorkerChild* aActor, const RemoteWorkerData& aData,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
        aChildEp) {
  RemoteWorkerChild* actor = mozilla::ipc::ActorCast<RemoteWorkerChild>(aActor);
  actor->ExecWorker(aData, std::move(aChildEp));
  return IPC_OK();
}

}  // namespace mozilla::dom
