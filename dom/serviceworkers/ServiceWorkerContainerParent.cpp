/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerContainerParent.h"

#include "ServiceWorkerContainerProxy.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientValidation.h"
#include "mozilla/ipc/BackgroundParent.h"

namespace mozilla::dom {

using mozilla::ipc::BackgroundParent;
using mozilla::ipc::IPCResult;

void ServiceWorkerContainerParent::ActorDestroy(ActorDestroyReason aReason) {
  if (mProxy) {
    mProxy->RevokeActor(this);
    mProxy = nullptr;
  }
}

IPCResult ServiceWorkerContainerParent::RecvRegister(
    Tainted<IPCClientInfo>&& aClientInfo, const nsACString& aScopeURL,
    const WorkerType& aType, const nsACString& aScriptURL,
    const ServiceWorkerUpdateViaCache& aUpdateViaCache,
    RegisterResolver&& aResolver) {
  if (!mProxy) {
    aResolver(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return IPC_OK();
  }

  Maybe<IPCClientInfo&> clientInfo = ValidateClientInfo(
      aClientInfo, BackgroundParent::GetLoadedOrigins(Manager()));
  if (!clientInfo) {
    return IPC_FAIL(this,
                    "Register ClientInfo principal not valid for remote type");
  }

  mProxy
      ->Register(ClientInfo(*clientInfo), aScopeURL, aType, aScriptURL,
                 aUpdateViaCache)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const ServiceWorkerRegistrationDescriptor& aDescriptor) {
            aResolver(aDescriptor.ToIPC());
          },
          [aResolver](const CopyableErrorResult& aResult) {
            aResolver(aResult);
          });

  return IPC_OK();
}

IPCResult ServiceWorkerContainerParent::RecvGetRegistration(
    Tainted<IPCClientInfo>&& aClientInfo, const nsACString& aURL,
    GetRegistrationResolver&& aResolver) {
  if (!mProxy) {
    aResolver(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return IPC_OK();
  }

  Maybe<IPCClientInfo&> clientInfo = ValidateClientInfo(
      aClientInfo, BackgroundParent::GetLoadedOrigins(Manager()));
  if (!clientInfo) {
    return IPC_FAIL(
        this, "GetRegistration ClientInfo principal not valid for remote type");
  }

  mProxy->GetRegistration(ClientInfo(*clientInfo), aURL)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const ServiceWorkerRegistrationDescriptor& aDescriptor) {
            aResolver(aDescriptor.ToIPC());
          },
          [aResolver](const CopyableErrorResult& aResult) {
            aResolver(aResult);
          });

  return IPC_OK();
}

IPCResult ServiceWorkerContainerParent::RecvGetRegistrations(
    Tainted<IPCClientInfo>&& aClientInfo,
    GetRegistrationsResolver&& aResolver) {
  if (!mProxy) {
    aResolver(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return IPC_OK();
  }

  Maybe<IPCClientInfo&> clientInfo = ValidateClientInfo(
      aClientInfo, BackgroundParent::GetLoadedOrigins(Manager()));
  if (!clientInfo) {
    return IPC_FAIL(
        this,
        "GetRegistrations ClientInfo principal not valid for remote type");
  }

  mProxy->GetRegistrations(ClientInfo(*clientInfo))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](
              const nsTArray<ServiceWorkerRegistrationDescriptor>& aList) {
            IPCServiceWorkerRegistrationDescriptorList ipcList;
            for (auto& desc : aList) {
              ipcList.values().AppendElement(desc.ToIPC());
            }
            aResolver(std::move(ipcList));
          },
          [aResolver](const CopyableErrorResult& aResult) {
            aResolver(aResult);
          });

  return IPC_OK();
}

IPCResult ServiceWorkerContainerParent::RecvGetReady(
    Tainted<IPCClientInfo>&& aClientInfo, GetReadyResolver&& aResolver) {
  if (!mProxy) {
    aResolver(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return IPC_OK();
  }

  Maybe<IPCClientInfo&> clientInfo = ValidateClientInfo(
      aClientInfo, BackgroundParent::GetLoadedOrigins(Manager()));
  if (!clientInfo) {
    return IPC_FAIL(this,
                    "GetReady ClientInfo principal not valid for remote type");
  }

  mProxy->GetReady(ClientInfo(*clientInfo))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const ServiceWorkerRegistrationDescriptor& aDescriptor) {
            aResolver(aDescriptor.ToIPC());
          },
          [aResolver](const CopyableErrorResult& aResult) {
            aResolver(aResult);
          });

  return IPC_OK();
}

ServiceWorkerContainerParent::ServiceWorkerContainerParent() = default;

ServiceWorkerContainerParent::~ServiceWorkerContainerParent() {
  MOZ_DIAGNOSTIC_ASSERT(!mProxy);
}

void ServiceWorkerContainerParent::Init() {
  mProxy = new ServiceWorkerContainerProxy(this);
}

}  // namespace mozilla::dom
