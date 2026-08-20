/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FetchEventOpProxyChild.h"

#include <utility>

#include "mozilla/Assertions.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/RemoteWorkerService.h"
#include "mozilla/dom/ServiceWorkerOp.h"
#include "mozilla/dom/ServiceWorkerOpPromise.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsThreadUtils.h"

namespace mozilla {

using namespace ipc;

namespace dom {

namespace {

nsresult GetIPCSynthesizeResponseArgs(
    ChildToParentSynthesizeResponseArgs* aIPCArgs,
    SynthesizeResponseArgs&& aArgs) {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
  auto [internalResponse, ipcArgs] = std::move(aArgs);

  *aIPCArgs = std::move(ipcArgs);

  internalResponse->SerializeChildToParentInternalResponseBody(
      &aIPCArgs->internalResponse());
  return NS_OK;
}

}  // anonymous namespace

void FetchEventOpProxyChild::Initialize(
    const ParentToChildServiceWorkerFetchEventOpArgs& aArgs) {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
  MOZ_ASSERT(!mOp);

  mInternalRequest =
      MakeSafeRefPtr<InternalRequest>(aArgs.common().internalRequest());

  if (aArgs.common().preloadNavigation()) {
    // We use synchronous task dispatch here to make sure that if the preload
    // response arrived before we dispatch the fetch event, then the JS preload
    // response promise will get resolved immediately.
    mPreloadResponseAvailablePromise =
        MakeRefPtr<FetchEventPreloadResponseAvailablePromise::Private>(
            __func__);
    mPreloadResponseAvailablePromise->UseDirectTaskDispatch(__func__);
    if (aArgs.preloadResponse().isSome()) {
      mPreloadResponseAvailablePromiseResolved = true;
      mPreloadResponseAvailablePromise->Resolve(
          InternalResponse::FromIPC(aArgs.preloadResponse().ref()), __func__);
    }

    mPreloadResponseTimingPromise =
        MakeRefPtr<FetchEventPreloadResponseTimingPromise::Private>(__func__);
    mPreloadResponseTimingPromise->UseDirectTaskDispatch(__func__);
    if (aArgs.preloadResponseTiming().isSome()) {
      mPreloadResponseTimingPromise->Resolve(
          aArgs.preloadResponseTiming().ref(), __func__);
    }

    mPreloadResponseEndPromise =
        MakeRefPtr<FetchEventPreloadResponseEndPromise::Private>(__func__);
    mPreloadResponseEndPromise->UseDirectTaskDispatch(__func__);
    if (aArgs.preloadResponseEndArgs().isSome()) {
      mPreloadResponseEndPromiseResolved = true;
      mPreloadResponseEndPromise->Resolve(aArgs.preloadResponseEndArgs().ref(),
                                          __func__);
    }
  }

  RemoteWorkerChild* manager =
      mozilla::ipc::ActorCast<RemoteWorkerChild>(Manager());
  MOZ_ASSERT(manager);

  RefPtr<FetchEventOpProxyChild> self = this;

  auto callback = [self](const ServiceWorkerOpResult& aResult) {
    // FetchEventOp could finish before NavigationPreload fetch finishes.
    // If NavigationPreload is available in FetchEvent, caching FetchEventOp
    // result until RecvPreloadResponseEnd is called, such that the preload
    // response could be completed.
    if (self->mPreloadResponseEndPromise &&
        !self->mPreloadResponseEndPromiseResolved &&
        self->mPreloadResponseAvailablePromiseResolved) {
      self->mCachedOpResult = Some(aResult);
      return;
    }
    if (!self->CanSend()) {
      return;
    }

    if (NS_WARN_IF(aResult.type() == ServiceWorkerOpResult::Tnsresult)) {
      (void)self->Send__delete__(self, aResult.get_nsresult());
      return;
    }

    MOZ_ASSERT(aResult.type() ==
               ServiceWorkerOpResult::TServiceWorkerFetchEventOpResult);

    (void)self->Send__delete__(self, aResult);
  };

  RefPtr<FetchEventOp> op = ServiceWorkerOp::Create(aArgs, std::move(callback))
                                .template downcast<FetchEventOp>();

  MOZ_ASSERT(op);

  op->SetActor(this);
  mOp = op;

  op->GetRespondWithPromise()
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = std::move(self)](
                 FetchEventRespondWithPromise::ResolveOrRejectValue&& aResult) {
               self->mRespondWithPromiseRequestHolder.Complete();

               if (NS_WARN_IF(aResult.IsReject())) {
                 MOZ_ASSERT(NS_FAILED(aResult.RejectValue().status()));

                 (void)self->SendRespondWith(aResult.RejectValue());
                 return;
               }

               auto& result = aResult.ResolveValue();

               if (result.is<SynthesizeResponseArgs>()) {
                 ChildToParentSynthesizeResponseArgs ipcArgs;
                 nsresult rv = GetIPCSynthesizeResponseArgs(
                     &ipcArgs, result.extract<SynthesizeResponseArgs>());

                 if (NS_WARN_IF(NS_FAILED(rv))) {
                   (void)self->SendRespondWith(
                       CancelInterceptionArgs(rv, ipcArgs.timeStamps()));
                   return;
                 }

                 (void)self->SendRespondWith(ipcArgs);
               } else if (result.is<ResetInterceptionArgs>()) {
                 (void)self->SendRespondWith(
                     result.extract<ResetInterceptionArgs>());
               } else {
                 (void)self->SendRespondWith(
                     result.extract<CancelInterceptionArgs>());
               }
             })
      ->Track(mRespondWithPromiseRequestHolder);

  manager->MaybeStartOp(std::move(op));
}

SafeRefPtr<InternalRequest> FetchEventOpProxyChild::ExtractInternalRequest() {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(mInternalRequest);

  return std::move(mInternalRequest);
}

RefPtr<FetchEventPreloadResponseAvailablePromise>
FetchEventOpProxyChild::GetPreloadResponseAvailablePromise() {
  return mPreloadResponseAvailablePromise;
}

RefPtr<FetchEventPreloadResponseTimingPromise>
FetchEventOpProxyChild::GetPreloadResponseTimingPromise() {
  return mPreloadResponseTimingPromise;
}

RefPtr<FetchEventPreloadResponseEndPromise>
FetchEventOpProxyChild::GetPreloadResponseEndPromise() {
  return mPreloadResponseEndPromise;
}

mozilla::ipc::IPCResult FetchEventOpProxyChild::RecvPreloadResponse(
    ParentToChildInternalResponse&& aResponse) {
  // Receiving this message implies that navigation preload is enabled, so
  // Initialize() should have created this promise.
  MOZ_ASSERT(mPreloadResponseAvailablePromise);

  mPreloadResponseAvailablePromiseResolved = true;
  mPreloadResponseAvailablePromise->Resolve(
      InternalResponse::FromIPC(aResponse), __func__);

  return IPC_OK();
}

mozilla::ipc::IPCResult FetchEventOpProxyChild::RecvPreloadResponseTiming(
    ResponseTiming&& aTiming) {
  // Receiving this message implies that navigation preload is enabled, so
  // Initialize() should have created this promise.
  MOZ_ASSERT(mPreloadResponseTimingPromise);

  mPreloadResponseTimingPromise->Resolve(std::move(aTiming), __func__);
  return IPC_OK();
}

mozilla::ipc::IPCResult FetchEventOpProxyChild::RecvPreloadResponseEnd(
    ResponseEndArgs&& aArgs) {
  // Receiving this message implies that navigation preload is enabled, so
  // Initialize() should have created this promise.
  MOZ_ASSERT(mPreloadResponseEndPromise);

  mPreloadResponseEndPromiseResolved = true;
  mPreloadResponseEndPromise->Resolve(std::move(aArgs), __func__);
  // If mCachedOpResult is not nothing, it means FetchEventOp had already done
  // and the operation result is cached. Continue closing IPC here.
  if (mCachedOpResult.isNothing()) {
    return IPC_OK();
  }

  if (!CanSend()) {
    return IPC_OK();
  }

  if (NS_WARN_IF(mCachedOpResult.ref().type() ==
                 ServiceWorkerOpResult::Tnsresult)) {
    (void)Send__delete__(this, mCachedOpResult.ref().get_nsresult());
    return IPC_OK();
  }

  MOZ_ASSERT(mCachedOpResult.ref().type() ==
             ServiceWorkerOpResult::TServiceWorkerFetchEventOpResult);

  (void)Send__delete__(this, mCachedOpResult.ref());

  return IPC_OK();
}

void FetchEventOpProxyChild::ActorDestroy(ActorDestroyReason) {
  (void)NS_WARN_IF(mRespondWithPromiseRequestHolder.Exists());
  mRespondWithPromiseRequestHolder.DisconnectIfExists();

  // If mPreloadResponseAvailablePromise exists, navigation preloading response
  // will not be valid anymore since it is too late to respond to the
  // FetchEvent. Resolve the preload response promise with
  // NS_ERROR_DOM_ABORT_ERR.
  if (mPreloadResponseAvailablePromise) {
    mPreloadResponseAvailablePromiseResolved = true;
    mPreloadResponseAvailablePromise->Resolve(
        InternalResponse::NetworkError(NS_ERROR_DOM_ABORT_ERR), __func__);
  }

  if (mPreloadResponseTimingPromise) {
    mPreloadResponseTimingPromise->Resolve(ResponseTiming(), __func__);
  }

  if (mPreloadResponseEndPromise) {
    mPreloadResponseEndPromiseResolved = true;
    ResponseEndArgs args(FetchDriverObserver::eAborted);
    mPreloadResponseEndPromise->Resolve(args, __func__);
  }

  mOp->RevokeActor(this);
  mOp = nullptr;
}

}  // namespace dom
}  // namespace mozilla
