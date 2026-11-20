/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorker.h"

#include "ServiceWorkerChild.h"
#include "ServiceWorkerManager.h"
#include "ServiceWorkerPrivate.h"
#include "ServiceWorkerRegistration.h"
#include "ServiceWorkerUtils.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientState.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ServiceWorkerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsGlobalWindowInner.h"
#include "nsPIDOMWindow.h"

#ifdef XP_WIN
#  undef PostMessage
#endif

using mozilla::ipc::BackgroundChild;
using mozilla::ipc::PBackgroundChild;

namespace mozilla::dom {

// static
already_AddRefed<ServiceWorker> ServiceWorker::Create(
    nsIGlobalObject* aGlobal, const ServiceWorkerDescriptor& aDescriptor) {
  RefPtr<ServiceWorker> ref = new ServiceWorker(aGlobal, aDescriptor);
  return ref.forget();
}

ServiceWorker::ServiceWorker(nsIGlobalObject* aGlobal,
                             const ServiceWorkerDescriptor& aDescriptor)
    : DOMEventTargetHelper(aGlobal),
      mDescriptor(aDescriptor),
      mShutdown(false),
      mLastNotifiedState(ServiceWorkerState::Installing) {
  MOZ_DIAGNOSTIC_ASSERT(aGlobal);

  if (NS_WARN_IF(!BackgroundChild::ValidatePrincipalInfo(
          aDescriptor.PrincipalInfo(), {}))) {
    Shutdown();
    return;
  }

  PBackgroundChild* parentActor =
      BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!parentActor)) {
    Shutdown();
    return;
  }

  RefPtr<ServiceWorkerChild> actor = ServiceWorkerChild::Create();
  if (NS_WARN_IF(!actor)) {
    Shutdown();
    return;
  }

  PServiceWorkerChild* sentActor =
      parentActor->SendPServiceWorkerConstructor(actor, aDescriptor.ToIPC());
  if (NS_WARN_IF(!sentActor)) {
    Shutdown();
    return;
  }
  MOZ_DIAGNOSTIC_ASSERT(sentActor == actor);

  mActor = std::move(actor);
  mActor->SetOwner(this);

  KeepAliveIfHasListenersFor(nsGkAtoms::onstatechange);

  // The error event handler is required by the spec currently, but is not used
  // anywhere.  Don't keep the object alive in that case.

  // Attempt to get an existing binding object for the registration
  // associated with this ServiceWorker.
  RefPtr<ServiceWorkerRegistration> reg =
      aGlobal->GetServiceWorkerRegistration(ServiceWorkerRegistrationDescriptor(
          mDescriptor.RegistrationId(), mDescriptor.RegistrationVersion(),
          mDescriptor.PrincipalInfo(), mDescriptor.Scope(), mDescriptor.Type(),
          ServiceWorkerUpdateViaCache::Imports));

  if (reg) {
    MaybeAttachToRegistration(reg);
    // Following codes are commented since GetRegistration has no
    // implementation. If we can not get an existing binding object, probably
    // need to create one to associate to it.
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1769652
    /*
    } else {

      RefPtr<ServiceWorker> self = this;
      GetRegistration(
          [self = std::move(self)](
              const ServiceWorkerRegistrationDescriptor& aDescriptor) {
            nsIGlobalObject* global = self->GetParentObject();
            NS_ENSURE_TRUE_VOID(global);
            RefPtr<ServiceWorkerRegistration> reg =
                global->GetOrCreateServiceWorkerRegistration(aDescriptor);
            self->MaybeAttachToRegistration(reg);
          },
          [](ErrorResult&& aRv) {
            // do nothing
            aRv.SuppressException();
          });
    */
  }
}

ServiceWorker::~ServiceWorker() { Shutdown(); }

NS_IMPL_CYCLE_COLLECTION_CLASS(ServiceWorker)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ServiceWorker,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRegistration)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ServiceWorker,
                                                DOMEventTargetHelper)
  tmp->DisconnectFromOwner();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRegistration)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(ServiceWorker, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(ServiceWorker, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ServiceWorker)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(ServiceWorker)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

JSObject* ServiceWorker::WrapObject(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return ServiceWorker_Binding::Wrap(aCx, this, aGivenProto);
}

ServiceWorkerState ServiceWorker::State() const { return mDescriptor.State(); }

void ServiceWorker::SetState(ServiceWorkerState aState) {
  NS_ENSURE_TRUE_VOID(aState >= mDescriptor.State());
  mDescriptor.SetState(aState);
}

void ServiceWorker::MaybeDispatchStateChangeEvent() {
  if (mDescriptor.State() <= mLastNotifiedState || !GetParentObject()) {
    return;
  }
  mLastNotifiedState = mDescriptor.State();

  DOMEventTargetHelper::DispatchTrustedEvent(u"statechange"_ns);

  // Once we have transitioned to the redundant state then no
  // more statechange events will occur.  We can allow the DOM
  // object to GC if script is not holding it alive.
  if (mLastNotifiedState == ServiceWorkerState::Redundant) {
    IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onstatechange);
  }
}

void ServiceWorker::GetScriptURL(nsString& aURL) const {
  CopyUTF8toUTF16(mDescriptor.ScriptURL(), aURL);
}

void ServiceWorker::PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                                const Sequence<JSObject*>& aTransferable,
                                ErrorResult& aRv) {
  // Step 6.1 of
  // https://w3c.github.io/ServiceWorker/#service-worker-postmessage-options
  // invokes
  // https://w3c.github.io/ServiceWorker/#run-service-worker
  // which returns failure in step 3 if the ServiceWorker state is redundant.
  // This will result in the "in parallel" step 6.1 of postMessage itself early
  // returning without starting the ServiceWorker and without throwing an error.
  if (State() == ServiceWorkerState::Redundant) {
    return;
  }

  nsIGlobalObject* global = GetRelevantGlobal();
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (!ServiceWorkersStorageAllowedForGlobal(global)) {
    ServiceWorkerManager::LocalizeAndReportToAllClients(
        mDescriptor.Scope(), "ServiceWorkerPostMessageStorageError",
        nsTArray<nsString>{NS_ConvertUTF8toUTF16(mDescriptor.Scope())});
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  JS::Rooted<JS::Value> transferable(aCx, JS::UndefinedValue());
  aRv = nsContentUtils::CreateJSValueFromSequenceOfObject(aCx, aTransferable,
                                                          &transferable);
  if (aRv.Failed()) {
    return;
  }

  // Window-to-SW messages do not allow memory sharing since they are not in the
  // same agent cluster group, but we do not want to throw an error during the
  // serialization. Because of this, we will propagate an error message data if
  // the SameProcess serialization is required. So that the receiver (service
  // worker) knows that it needs to throw while deserialization and sharing
  // memory objects are not propagated to the other process.
  JS::CloneDataPolicy clonePolicy;
  if (global->IsSharedMemoryAllowed()) {
    clonePolicy.allowSharedMemoryObjects();
  }

  auto data = MakeRefPtr<ipc::StructuredCloneData>(
      JS::StructuredCloneScope::UnknownDestination,
      StructuredCloneHolder::TransferringSupported);
  data->Write(aCx, aMessage, transferable, clonePolicy, aRv);
  if (aRv.Failed()) {
    return;
  }

  // If StructuredCloneData::Write() ended up deciding on a scope of SameProcess
  // then we must convert this to an error on deserialization.  This is because
  // such payloads fundamentally can't be sent cross-process (they involve
  // pointers / local resources).  However, this will also correlate with the
  // spec for situations like SharedArrayBuffer which are limited to being sent
  // within the same agent cluster and where ServiceWorkers are always spawned
  // in their own agent cluster.
  if (data->CloneScope() != JS::StructuredCloneScope::DifferentProcess) {
    data = nullptr;
  }

  if (!mActor) {
    return;
  }

  // ServiceWorkersStorageAllowedForGlobal will have already validated these as
  // both being isSome().
  Maybe<ClientInfo> clientInfo = global->GetClientInfo();
  Maybe<ClientState> clientState = global->GetClientState();

  // If this global is a ServiceWorker, we need this global's
  // ServiceWorkerDescriptor.  While we normally try and normalize things
  // through nsIGlobalObject, this is fairly one-off right now, so starting from
  // worker-specific logic.
  PostMessageSource source;
  if (WorkerPrivate* wp = GetCurrentThreadWorkerPrivate()) {
    if (wp->IsServiceWorker()) {
      source = wp->GetServiceWorkerDescriptor().ToIPC();
    } else {
      source = ClientInfoAndState(clientInfo.ref().ToIPC(),
                                  clientState.ref().ToIPC());
    }
  } else {
    source =
        ClientInfoAndState(clientInfo.ref().ToIPC(), clientState.ref().ToIPC());
  }

  mActor->SendPostMessage(data, source);
}

void ServiceWorker::PostMessage(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                                const StructuredSerializeOptions& aOptions,
                                ErrorResult& aRv) {
  PostMessage(aCx, aMessage, aOptions.mTransfer, aRv);
}

const ServiceWorkerDescriptor& ServiceWorker::Descriptor() const {
  return mDescriptor;
}

void ServiceWorker::DisconnectFromOwner() {
  DOMEventTargetHelper::DisconnectFromOwner();
  Shutdown();
}

void ServiceWorker::RevokeActor(ServiceWorkerChild* aActor) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mActor == aActor);
  mActor->RevokeOwner(this);
  mActor = nullptr;

  mShutdown = true;
}

void ServiceWorker::MaybeAttachToRegistration(
    ServiceWorkerRegistration* aRegistration) {
  MOZ_DIAGNOSTIC_ASSERT(aRegistration);
  MOZ_DIAGNOSTIC_ASSERT(!mRegistration);

  // If the registration no longer actually references this ServiceWorker
  // then we must be in the redundant state.
  if (!aRegistration->Descriptor().HasWorker(mDescriptor)) {
    SetState(ServiceWorkerState::Redundant);
    MaybeDispatchStateChangeEvent();
    return;
  }

  mRegistration = aRegistration;
}

void ServiceWorker::Shutdown() {
  if (mShutdown) {
    return;
  }
  mShutdown = true;

  if (mActor) {
    mActor->RevokeOwner(this);
    mActor->MaybeStartTeardown();
    mActor = nullptr;
  }
}

}  // namespace mozilla::dom
