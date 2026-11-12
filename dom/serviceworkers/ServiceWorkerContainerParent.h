/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_serviceworkercontainerparent_h_
#define mozilla_dom_serviceworkercontainerparent_h_

#include "mozilla/dom/PServiceWorkerContainerParent.h"

namespace mozilla::dom {

class IPCServiceWorkerDescriptor;
class ServiceWorkerContainerProxy;

class ServiceWorkerContainerParent final
    : public PServiceWorkerContainerParent {
  RefPtr<ServiceWorkerContainerProxy> mProxy;

  ~ServiceWorkerContainerParent();

  // PServiceWorkerContainerParent
  void ActorDestroy(ActorDestroyReason aReason) override;

  mozilla::ipc::IPCResult RecvRegister(
      Tainted<IPCClientInfo>&& aClientInfo, const nsACString& aScopeURL,
      const WorkerType& aType, const nsACString& aScriptURL,
      const ServiceWorkerUpdateViaCache& aUpdateViaCache,
      RegisterResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvGetRegistration(
      Tainted<IPCClientInfo>&& aClientInfo, const nsACString& aURL,
      GetRegistrationResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvGetRegistrations(
      Tainted<IPCClientInfo>&& aClientInfo,
      GetRegistrationsResolver&& aResolver) override;

  mozilla::ipc::IPCResult RecvGetReady(Tainted<IPCClientInfo>&& aClientInfo,
                                       GetReadyResolver&& aResolver) override;

 public:
  NS_INLINE_DECL_REFCOUNTING(ServiceWorkerContainerParent, override);

  ServiceWorkerContainerParent();

  void Init();
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_serviceworkercontainerparent_h_
