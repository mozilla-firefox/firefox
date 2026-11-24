/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerRegistration.h"

#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/NavigationPreloadManager.h"
#include "mozilla/dom/NavigationPreloadManagerBinding.h"
#include "mozilla/dom/Notification.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PushManager.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/dom/ServiceWorker.h"
#include "mozilla/dom/ServiceWorkerRegistrationBinding.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ScopeExit.h"
#include "nsCycleCollectionParticipant.h"
#include "nsPIDOMWindow.h"
#include "ServiceWorkerRegistrationChild.h"

using mozilla::ipc::ResponseRejectReason;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(ServiceWorkerRegistration)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ServiceWorkerRegistration,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInstallingWorker, mWaitingWorker,
                                    mActiveWorker, mNavigationPreloadManager,
                                    mPushManager)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ServiceWorkerRegistration,
                                                DOMEventTargetHelper)
  tmp->DisconnectFromOwner();
  tmp->Shutdown();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInstallingWorker, mWaitingWorker,
                                  mActiveWorker, mNavigationPreloadManager,
                                  mPushManager)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(ServiceWorkerRegistration, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(ServiceWorkerRegistration, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ServiceWorkerRegistration)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(ServiceWorkerRegistration)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

namespace {
const uint64_t kInvalidUpdateFoundId = 0;
}  // anonymous namespace

ServiceWorkerRegistration::ServiceWorkerRegistration(
    nsIGlobalObject* aGlobal,
    const ServiceWorkerRegistrationDescriptor& aDescriptor)
    : DOMEventTargetHelper(aGlobal),
      mDescriptor(aDescriptor),
      mShutdown(false),
      mScheduledUpdateFoundId(kInvalidUpdateFoundId),
      mDispatchedUpdateFoundId(kInvalidUpdateFoundId) {
  ::mozilla::ipc::PBackgroundChild* parentActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!parentActor)) {
    Shutdown();
    return;
  }

  auto actor = ServiceWorkerRegistrationChild::Create();
  if (NS_WARN_IF(!actor)) {
    Shutdown();
    return;
  }

  PServiceWorkerRegistrationChild* sentActor =
      parentActor->SendPServiceWorkerRegistrationConstructor(
          actor, aDescriptor.ToIPC());
  if (NS_WARN_IF(!sentActor)) {
    Shutdown();
    return;
  }
  MOZ_DIAGNOSTIC_ASSERT(sentActor == actor);

  mActor = std::move(actor);
  mActor->SetOwner(this);

  KeepAliveIfHasListenersFor(nsGkAtoms::onupdatefound);
}

ServiceWorkerRegistration::~ServiceWorkerRegistration() { Shutdown(); }

JSObject* ServiceWorkerRegistration::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return ServiceWorkerRegistration_Binding::Wrap(aCx, this, aGivenProto);
}

/* static */
already_AddRefed<ServiceWorkerRegistration>
ServiceWorkerRegistration::CreateForMainThread(
    nsPIDOMWindowInner* aWindow,
    const ServiceWorkerRegistrationDescriptor& aDescriptor) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<ServiceWorkerRegistration> registration =
      new ServiceWorkerRegistration(aWindow->AsGlobal(), aDescriptor);
  // This is not called from within the constructor, as it may call content code
  // which can cause the deletion of the registration, so we need to keep a
  // strong reference while calling it.
  registration->UpdateState(aDescriptor);

  return registration.forget();
}

/* static */
already_AddRefed<ServiceWorkerRegistration>
ServiceWorkerRegistration::CreateForWorker(
    WorkerPrivate* aWorkerPrivate, nsIGlobalObject* aGlobal,
    const ServiceWorkerRegistrationDescriptor& aDescriptor) {
  MOZ_DIAGNOSTIC_ASSERT(aWorkerPrivate);
  MOZ_DIAGNOSTIC_ASSERT(aGlobal);
  aWorkerPrivate->AssertIsOnWorkerThread();

  RefPtr<ServiceWorkerRegistration> registration =
      new ServiceWorkerRegistration(aGlobal, aDescriptor);
  // This is not called from within the constructor, as it may call content code
  // which can cause the deletion of the registration, so we need to keep a
  // strong reference while calling it.
  registration->UpdateState(aDescriptor);

  return registration.forget();
}

void ServiceWorkerRegistration::DisconnectFromOwner() {
  DOMEventTargetHelper::DisconnectFromOwner();
}

void ServiceWorkerRegistration::RegistrationCleared() {
  // Its possible that the registration will fail to install and be
  // immediately removed.  In that case we may never receive the
  // UpdateState() call if the actor was too slow to connect, etc.
  // Ensure that we force all our known actors to redundant so that
  // the appropriate statechange events are fired.  If we got the
  // UpdateState() already then this will be a no-op.
  UpdateStateInternal(Maybe<ServiceWorkerDescriptor>(),
                      Maybe<ServiceWorkerDescriptor>(),
                      Maybe<ServiceWorkerDescriptor>());

  // Our underlying registration was removed from SWM, so we
  // will never get an updatefound event again.  We can let
  // the object GC if content is not holding it alive.
  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onupdatefound);
}

already_AddRefed<ServiceWorker> ServiceWorkerRegistration::GetInstalling()
    const {
  RefPtr<ServiceWorker> ref = mInstallingWorker;
  return ref.forget();
}

already_AddRefed<ServiceWorker> ServiceWorkerRegistration::GetWaiting() const {
  RefPtr<ServiceWorker> ref = mWaitingWorker;
  return ref.forget();
}

already_AddRefed<ServiceWorker> ServiceWorkerRegistration::GetActive() const {
  RefPtr<ServiceWorker> ref = mActiveWorker;
  return ref.forget();
}

already_AddRefed<NavigationPreloadManager>
ServiceWorkerRegistration::NavigationPreload() {
  RefPtr<ServiceWorkerRegistration> reg = this;
  if (!mNavigationPreloadManager) {
    mNavigationPreloadManager = MakeRefPtr<NavigationPreloadManager>(reg);
  }
  RefPtr<NavigationPreloadManager> ref = mNavigationPreloadManager;
  return ref.forget();
}

void ServiceWorkerRegistration::UpdateState(
    const ServiceWorkerRegistrationDescriptor& aDescriptor) {
  MOZ_DIAGNOSTIC_ASSERT(MatchesDescriptor(aDescriptor));

  mDescriptor = aDescriptor;

  UpdateStateInternal(aDescriptor.GetInstalling(), aDescriptor.GetWaiting(),
                      aDescriptor.GetActive());

  nsTArray<UniquePtr<VersionCallback>> callbackList =
      std::move(mVersionCallbackList);
  for (auto& cb : callbackList) {
    if (cb->mVersion > mDescriptor.Version()) {
      mVersionCallbackList.AppendElement(std::move(cb));
      continue;
    }

    cb->mFunc(cb->mVersion == mDescriptor.Version());
  }
}

bool ServiceWorkerRegistration::MatchesDescriptor(
    const ServiceWorkerRegistrationDescriptor& aDescriptor) const {
  return aDescriptor.Id() == mDescriptor.Id() &&
         aDescriptor.PrincipalInfo() == mDescriptor.PrincipalInfo() &&
         aDescriptor.Scope() == mDescriptor.Scope();
}

void ServiceWorkerRegistration::GetScope(nsAString& aScope) const {
  CopyUTF8toUTF16(mDescriptor.Scope(), aScope);
}

ServiceWorkerUpdateViaCache ServiceWorkerRegistration::GetUpdateViaCache(
    ErrorResult& aRv) const {
  return mDescriptor.UpdateViaCache();
}

already_AddRefed<Promise> ServiceWorkerRegistration::Update(ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  if (!global) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  RefPtr<Promise> outer = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // `ServiceWorker` objects are not exposed on worker threads yet, so calling
  // `ServiceWorkerRegistration::Get{Installing,Waiting,Active}` won't work.
  const Maybe<ServiceWorkerDescriptor> newestWorkerDescriptor =
      mDescriptor.Newest();

  // "If newestWorker is null, return a promise rejected with an
  // "InvalidStateError" DOMException and abort these steps."
  if (newestWorkerDescriptor.isNothing()) {
    outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return outer.forget();
  }

  // "If the context object’s relevant settings object’s global object
  // globalObject is a ServiceWorkerGlobalScope object, and globalObject’s
  // associated service worker's state is "installing", return a promise
  // rejected with an "InvalidStateError" DOMException and abort these steps."
  if (!NS_IsMainThread()) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    if (workerPrivate->IsServiceWorker() &&
        (workerPrivate->GetServiceWorkerDescriptor().State() ==
         ServiceWorkerState::Installing)) {
      outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
      return outer.forget();
    }
  }

  RefPtr<ServiceWorkerRegistration> self = this;

  if (!mActor) {
    outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return outer.forget();
  }

  mActor->SendUpdate(
      newestWorkerDescriptor.ref().ScriptURL(),
      [outer,
       self](const IPCServiceWorkerRegistrationDescriptorOrCopyableErrorResult&
                 aResult) {
        if (aResult.type() ==
            IPCServiceWorkerRegistrationDescriptorOrCopyableErrorResult::
                TCopyableErrorResult) {
          // application layer error
          const auto& rv = aResult.get_CopyableErrorResult();
          MOZ_DIAGNOSTIC_ASSERT(rv.Failed());
          outer->MaybeReject(CopyableErrorResult(rv));
          return;
        }
        // success
        const auto& ipcDesc =
            aResult.get_IPCServiceWorkerRegistrationDescriptor();
        nsIGlobalObject* global = self->GetParentObject();
        // It's possible this binding was detached from the global.  In cases
        // where we use IPC with Promise callbacks, we use
        // DOMMozPromiseRequestHolder in order to auto-disconnect the promise
        // that would hold these callbacks.  However in bug 1466681 we changed
        // this call to use (synchronous) callbacks because the use of
        // MozPromise introduced an additional runnable scheduling which made
        // it very difficult to maintain ordering required by the standard.
        //
        // If we were to delete this actor at the time of DETH detaching, we
        // would not need to do this check because the IPC callback of the
        // RemoteServiceWorkerRegistrationImpl lambdas would never occur.
        // However, its actors currently depend on asking the parent to delete
        // the actor for us.  Given relaxations in the IPC lifecyle, we could
        // potentially issue a direct termination, but that requires additional
        // evaluation.
        if (!global) {
          outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
          return;
        }
        RefPtr<ServiceWorkerRegistration> ref =
            global->GetOrCreateServiceWorkerRegistration(
                ServiceWorkerRegistrationDescriptor(ipcDesc));
        if (!ref) {
          outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
          return;
        }
        outer->MaybeResolve(ref);
      },
      [outer](ResponseRejectReason&& aReason) {
        // IPC layer error
        outer->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
      });

  return outer.forget();
}

already_AddRefed<Promise> ServiceWorkerRegistration::Unregister(
    ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  RefPtr<Promise> outer = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (!mActor) {
    outer->MaybeResolve(false);
    return outer.forget();
  }

  mActor->SendUnregister(
      [outer](std::tuple<bool, CopyableErrorResult>&& aResult) {
        if (std::get<1>(aResult).Failed()) {
          // application layer error
          // register() should be resilient and resolve false instead of
          // rejecting in most cases.
          std::get<1>(aResult).SuppressException();
          outer->MaybeResolve(false);
          return;
        }
        // success
        outer->MaybeResolve(std::get<0>(aResult));
      },
      [outer](ResponseRejectReason&& aReason) {
        // IPC layer error
        outer->MaybeResolve(false);
      });

  return outer.forget();
}

already_AddRefed<PushManager> ServiceWorkerRegistration::GetPushManager(
    JSContext* aCx, ErrorResult& aRv) {
  if (!mPushManager) {
    nsCOMPtr<nsIGlobalObject> globalObject = GetParentObject();

    if (!globalObject) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }

    GlobalObject global(aCx, globalObject->GetGlobalJSObject());
    mPushManager = PushManager::Constructor(
        global, NS_ConvertUTF8toUTF16(mDescriptor.Scope()), aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  RefPtr<PushManager> ret = mPushManager;
  return ret.forget();
}

already_AddRefed<Promise> ServiceWorkerRegistration::ShowNotification(
    JSContext* aCx, const nsAString& aTitle,
    const NotificationOptions& aOptions, ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  if (!global) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  // Until we ship ServiceWorker objects on worker threads the active
  // worker will always be nullptr.  So limit this check to main
  // thread for now.
  if (mDescriptor.GetActive().isNothing() && NS_IsMainThread()) {
    aRv.ThrowTypeError<MSG_NO_ACTIVE_WORKER>(mDescriptor.Scope());
    return nullptr;
  }

  NS_ConvertUTF8toUTF16 scope(mDescriptor.Scope());

  RefPtr<Promise> p = Notification::ShowPersistentNotification(
      aCx, global, scope, aTitle, aOptions, mDescriptor, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return p.forget();
}

already_AddRefed<Promise> ServiceWorkerRegistration::GetNotifications(
    const GetNotificationOptions& aOptions, ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  if (!global) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  NS_ConvertUTF8toUTF16 scope(mDescriptor.Scope());

  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(global);
    if (NS_WARN_IF(!window)) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }
    return Notification::Get(window, aOptions, scope, aRv);
  }

  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  worker->AssertIsOnWorkerThread();
  return Notification::WorkerGet(worker, aOptions, scope, aRv);
}

void ServiceWorkerRegistration::SetNavigationPreloadEnabled(
    bool aEnabled, ServiceWorkerBoolCallback&& aSuccessCB,
    ServiceWorkerFailureCallback&& aFailureCB) {
  if (!mActor) {
    aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return;
  }

  mActor->SendSetNavigationPreloadEnabled(
      aEnabled,
      [successCB = std::move(aSuccessCB), aFailureCB](bool aResult) {
        if (!aResult) {
          aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
          return;
        }
        successCB(aResult);
      },
      [aFailureCB](ResponseRejectReason&& aReason) {
        aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
      });
}

void ServiceWorkerRegistration::SetNavigationPreloadHeader(
    const nsCString& aHeader, ServiceWorkerBoolCallback&& aSuccessCB,
    ServiceWorkerFailureCallback&& aFailureCB) {
  if (!mActor) {
    aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return;
  }

  mActor->SendSetNavigationPreloadHeader(
      aHeader,
      [successCB = std::move(aSuccessCB), aFailureCB](bool aResult) {
        if (!aResult) {
          aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
          return;
        }
        successCB(aResult);
      },
      [aFailureCB](ResponseRejectReason&& aReason) {
        aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
      });
}

void ServiceWorkerRegistration::GetNavigationPreloadState(
    NavigationPreloadGetStateCallback&& aSuccessCB,
    ServiceWorkerFailureCallback&& aFailureCB) {
  if (!mActor) {
    aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
    return;
  }

  mActor->SendGetNavigationPreloadState(
      [successCB = std::move(aSuccessCB),
       aFailureCB](Maybe<IPCNavigationPreloadState>&& aState) {
        if (NS_WARN_IF(!aState)) {
          aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
          return;
        }

        NavigationPreloadState state;
        state.mEnabled = aState.ref().enabled();
        state.mHeaderValue.Construct(std::move(aState.ref().headerValue()));
        successCB(std::move(state));
      },
      [aFailureCB](ResponseRejectReason&& aReason) {
        aFailureCB(CopyableErrorResult(NS_ERROR_DOM_INVALID_STATE_ERR));
      });
}

const ServiceWorkerRegistrationDescriptor&
ServiceWorkerRegistration::Descriptor() const {
  return mDescriptor;
}

void ServiceWorkerRegistration::WhenVersionReached(
    uint64_t aVersion, ServiceWorkerBoolCallback&& aCallback) {
  if (aVersion <= mDescriptor.Version()) {
    aCallback(aVersion == mDescriptor.Version());
    return;
  }

  mVersionCallbackList.AppendElement(
      MakeUnique<VersionCallback>(aVersion, std::move(aCallback)));
}

void ServiceWorkerRegistration::MaybeScheduleUpdateFound(
    const Maybe<ServiceWorkerDescriptor>& aInstallingDescriptor) {
  // This function sets mScheduledUpdateFoundId to note when we were told about
  // a new installing worker. We rely on a call to
  // MaybeDispatchUpdateFoundRunnable (called indirectly from UpdateJobCallback)
  // to actually fire the event.
  uint64_t newId = aInstallingDescriptor.isSome()
                       ? aInstallingDescriptor.ref().Id()
                       : kInvalidUpdateFoundId;

  if (mScheduledUpdateFoundId != kInvalidUpdateFoundId) {
    if (mScheduledUpdateFoundId == newId) {
      return;
    }
    MaybeDispatchUpdateFound();
    MOZ_DIAGNOSTIC_ASSERT(mScheduledUpdateFoundId == kInvalidUpdateFoundId);
  }

  bool updateFound =
      newId != kInvalidUpdateFoundId && mDispatchedUpdateFoundId != newId;

  if (!updateFound) {
    return;
  }

  mScheduledUpdateFoundId = newId;
}

void ServiceWorkerRegistration::MaybeDispatchUpdateFoundRunnable() {
  if (mScheduledUpdateFoundId == kInvalidUpdateFoundId) {
    return;
  }

  nsIGlobalObject* global = GetParentObject();
  NS_ENSURE_TRUE_VOID(global);

  nsCOMPtr<nsIRunnable> r = NewCancelableRunnableMethod(
      "ServiceWorkerRegistration::MaybeDispatchUpdateFound", this,
      &ServiceWorkerRegistration::MaybeDispatchUpdateFound);

  Unused << global->EventTargetFor(TaskCategory::Other)
                ->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void ServiceWorkerRegistration::MaybeDispatchUpdateFound() {
  uint64_t scheduledId = mScheduledUpdateFoundId;
  mScheduledUpdateFoundId = kInvalidUpdateFoundId;

  if (scheduledId == kInvalidUpdateFoundId ||
      scheduledId == mDispatchedUpdateFoundId) {
    return;
  }

  mDispatchedUpdateFoundId = scheduledId;
  DispatchTrustedEvent(u"updatefound"_ns);
}

void ServiceWorkerRegistration::UpdateStateInternal(
    const Maybe<ServiceWorkerDescriptor>& aInstalling,
    const Maybe<ServiceWorkerDescriptor>& aWaiting,
    const Maybe<ServiceWorkerDescriptor>& aActive) {
  // Do this immediately as it may flush an already pending updatefound
  // event.  In that case we want to fire the pending event before
  // modifying any of the registration properties.
  MaybeScheduleUpdateFound(aInstalling);

  // Move the currently exposed workers into a separate list
  // of "old" workers.  We will then potentially add them
  // back to the registration properties below based on the
  // given descriptor.  Any that are not restored will need
  // to be moved to the redundant state.
  AutoTArray<RefPtr<ServiceWorker>, 3> oldWorkerList({
      std::move(mInstallingWorker),
      std::move(mWaitingWorker),
      std::move(mActiveWorker),
  });

  // Its important that all state changes are actually applied before
  // dispatching any statechange events.  Each ServiceWorker object
  // should be in the correct state and the ServiceWorkerRegistration
  // properties need to be set correctly as well.  To accomplish this
  // we use a ScopeExit to dispatch any statechange events.
  auto scopeExit = MakeScopeExit([&] {
    // Check to see if any of the "old" workers was completely discarded.
    // Set these workers to the redundant state.
    for (auto& oldWorker : oldWorkerList) {
      if (!oldWorker || oldWorker == mInstallingWorker ||
          oldWorker == mWaitingWorker || oldWorker == mActiveWorker) {
        continue;
      }

      oldWorker->SetState(ServiceWorkerState::Redundant);
    }

    // Check each worker to see if it needs a statechange event dispatched.
    if (mInstallingWorker) {
      mInstallingWorker->MaybeDispatchStateChangeEvent();
    }
    if (mWaitingWorker) {
      mWaitingWorker->MaybeDispatchStateChangeEvent();
    }
    if (mActiveWorker) {
      mActiveWorker->MaybeDispatchStateChangeEvent();
    }

    // We also check the "old" workers to see if they need a statechange
    // event as well.  Note, these may overlap with the known worker properties
    // above, but MaybeDispatchStateChangeEvent() will ignore duplicated calls.
    for (auto& oldWorker : oldWorkerList) {
      if (!oldWorker) {
        continue;
      }

      oldWorker->MaybeDispatchStateChangeEvent();
    }
  });

  // Clear all workers if the registration has been detached from the global.
  // Also, we cannot expose ServiceWorker objects on worker threads yet, so
  // do the same on when off-main-thread.  This main thread check should be
  // removed as part of bug 1113522.
  nsCOMPtr<nsIGlobalObject> global = GetParentObject();
  if (!global || !NS_IsMainThread()) {
    return;
  }

  if (aActive.isSome()) {
    if ((mActiveWorker = global->GetOrCreateServiceWorker(aActive.ref()))) {
      mActiveWorker->SetState(aActive.ref().State());
    }
  } else {
    mActiveWorker = nullptr;
  }

  if (aWaiting.isSome()) {
    if ((mWaitingWorker = global->GetOrCreateServiceWorker(aWaiting.ref()))) {
      mWaitingWorker->SetState(aWaiting.ref().State());
    }
  } else {
    mWaitingWorker = nullptr;
  }

  if (aInstalling.isSome()) {
    if ((mInstallingWorker =
             global->GetOrCreateServiceWorker(aInstalling.ref()))) {
      mInstallingWorker->SetState(aInstalling.ref().State());
    }
  } else {
    mInstallingWorker = nullptr;
  }
}

void ServiceWorkerRegistration::RevokeActor(
    ServiceWorkerRegistrationChild* aActor) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mActor == aActor);
  mActor->RevokeOwner(this);
  mActor = nullptr;

  mShutdown = true;

  RegistrationCleared();
}

void ServiceWorkerRegistration::Shutdown() {
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
