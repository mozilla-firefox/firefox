/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientManagerParent.h"

#include "ClientHandleParent.h"
#include "ClientManagerOpParent.h"
#include "ClientManagerService.h"
#include "ClientSourceParent.h"
#include "ClientValidation.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/PClientNavigateOpParent.h"
#include "mozilla/ipc/BackgroundParent.h"

namespace mozilla::dom {

using mozilla::ipc::BackgroundParent;
using mozilla::ipc::IPCResult;

IPCResult ClientManagerParent::RecvTeardown() {
  (void)Send__delete__(this);
  return IPC_OK();
}

void ClientManagerParent::ActorDestroy(ActorDestroyReason aReason) {
  mService->RemoveManager(this);
}

already_AddRefed<PClientHandleParent>
ClientManagerParent::AllocPClientHandleParent(
    Tainted<IPCClientInfo>& aClientInfo) {
  return MakeAndAddRef<ClientHandleParent>();
}

IPCResult ClientManagerParent::RecvPClientHandleConstructor(
    PClientHandleParent* aActor, Tainted<IPCClientInfo>&& aClientInfo) {
  Maybe<IPCClientInfo&> clientInfo = ValidateClientInfo(
      aClientInfo, BackgroundParent::GetLoadedOrigins(Manager()));
  if (!clientInfo) {
    return IPC_FAIL(this, "Invalid PrincipalInfo!");
  }

  ClientHandleParent* actor = static_cast<ClientHandleParent*>(aActor);
  return actor->Init(*clientInfo);
}

PClientManagerOpParent* ClientManagerParent::AllocPClientManagerOpParent(
    const ClientOpConstructorArgs& aArgs) {
  return new ClientManagerOpParent(mService);
}

bool ClientManagerParent::DeallocPClientManagerOpParent(
    PClientManagerOpParent* aActor) {
  delete aActor;
  return true;
}

IPCResult ClientManagerParent::RecvPClientManagerOpConstructor(
    PClientManagerOpParent* aActor, const ClientOpConstructorArgs& aArgs) {
  ClientManagerOpParent* actor = static_cast<ClientManagerOpParent*>(aActor);
  RefPtr<ThreadsafeContentParentHandle> contentParentHandle =
      ::mozilla::ipc::BackgroundParent::GetContentParentHandle(Manager());
  return actor->Init(aArgs, contentParentHandle);
}

PClientNavigateOpParent* ClientManagerParent::AllocPClientNavigateOpParent(
    const ClientNavigateOpConstructorArgs& aArgs) {
  MOZ_ASSERT_UNREACHABLE(
      "ClientNavigateOpParent should be explicitly constructed.");
  return nullptr;
}

bool ClientManagerParent::DeallocPClientNavigateOpParent(
    PClientNavigateOpParent* aActor) {
  delete aActor;
  return true;
}

already_AddRefed<PClientSourceParent>
ClientManagerParent::AllocPClientSourceParent(
    const ClientSourceConstructorArgs& aArgs) {
  RefPtr<ThreadsafeContentParentHandle> contentParentHandle =
      ::mozilla::ipc::BackgroundParent::GetContentParentHandle(Manager());

  return MakeAndAddRef<ClientSourceParent>(aArgs, contentParentHandle);
}

IPCResult ClientManagerParent::RecvPClientSourceConstructor(
    PClientSourceParent* aActor, const ClientSourceConstructorArgs& aArgs) {
  ClientSourceParent* actor = static_cast<ClientSourceParent*>(aActor);

  IPCResult result = actor->Init();
  if (!result) {
    return result;
  }

  return IPC_OK();
}

ClientManagerParent::ClientManagerParent()
    : mService(ClientManagerService::GetOrCreateInstance()) {}

ClientManagerParent::~ClientManagerParent() = default;

void ClientManagerParent::Init() { mService->AddManager(this); }

IPCResult ClientManagerParent::RecvExpectFutureClientSource(
    Tainted<IPCClientInfo>&& aClientInfo) {
  if (BackgroundParent::IsOtherProcessActor(Manager())) {
    return IPC_FAIL(
        this,
        "ExpectFutureClientSource can only be used by the parent process");
  }

  RefPtr<ClientManagerService> cms =
      ClientManagerService::GetOrCreateInstance();
  (void)NS_WARN_IF(!cms->ExpectFutureSource(
      MOZ_NO_VALIDATE(aClientInfo, "Only sent by the parent process")));
  return IPC_OK();
}

IPCResult ClientManagerParent::RecvForgetFutureClientSource(
    Tainted<IPCClientInfo>&& aClientInfo) {
  if (BackgroundParent::IsOtherProcessActor(Manager())) {
    return IPC_FAIL(
        this,
        "ForgetFutureClientSource can only be used by the parent process");
  }

  RefPtr<ClientManagerService> cms = ClientManagerService::GetInstance();
  cms->ForgetFutureSource(
      MOZ_NO_VALIDATE(aClientInfo, "Only sent by the parent process"));
  return IPC_OK();
}

}  // namespace mozilla::dom
