/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieStoreManager.h"

#include "CookieStoreChild.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsGlobalWindowInner.h"
#include "nsIGlobalObject.h"
#include "nsIPrincipal.h"

using mozilla::ipc::PrincipalInfo;

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(CookieStoreManager)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(CookieStoreManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobalObject)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->Shutdown();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(CookieStoreManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobalObject)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

CookieStoreManager::CookieStoreManager(
    nsIGlobalObject* aGlobalObject,
    const nsACString& aServiceWorkerRegistrationScopeURL)
    : mGlobalObject(aGlobalObject),
      mScopeURL(aServiceWorkerRegistrationScopeURL) {
  MOZ_ASSERT(aGlobalObject);
}

namespace {

nsIPrincipal* RetrievePrincipal(nsIGlobalObject* aGlobalObject) {
  if (NS_IsMainThread()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobalObject);
    if (NS_WARN_IF(!window)) {
      return nullptr;
    }

    return nsGlobalWindowInner::Cast(window)->GetClientPrincipal();
  }

  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(worker);
  worker->AssertIsOnWorkerThread();

  return worker->GetPrincipal();
}

}  // namespace

CookieStoreManager::~CookieStoreManager() { Shutdown(); }

JSObject* CookieStoreManager::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return CookieStoreManager_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<Promise> CookieStoreManager::Subscribe(
    const CopyableTArray<CookieStoreGetOptions>& aSubscriptions,
    ErrorResult& aRv) {
  return SubscribeOrUnsubscribe(Action::eSubscribe, aSubscriptions, aRv);
}

already_AddRefed<Promise> CookieStoreManager::GetSubscriptions(
    ErrorResult& aRv) {
  nsCOMPtr<nsIPrincipal> principal = RetrievePrincipal(mGlobalObject);
  if (NS_WARN_IF(!principal)) {
    aRv.ThrowInvalidStateError("Invalid context");
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(mGlobalObject, aRv);
  if (NS_WARN_IF(!promise)) {
    return nullptr;
  }

  // ServiceWorkers only have one principal: it's either partitioned or
  // unpartitioned. If the "context" is partitioned, the window or the
  // WorkerPrivate will return a partitioned principal.

  // Let's dispatch a runnable to implement the "Run the following steps in
  // parallel".
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      __func__, [self = RefPtr(this), principal = RefPtr(principal.get()),
                 promise = RefPtr(promise)]() {
        if (promise->GetParentObject()->IsDying()) {
          return;
        }

        if (!self->MaybeCreateActor()) {
          promise->MaybeRejectWithNotAllowedError("Permission denied");
          return;
        }

        PrincipalInfo principalInfo;
        nsresult rv = PrincipalToPrincipalInfo(principal, &principalInfo);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          promise->MaybeResolve(nsTArray<CookieStoreGetOptions>());
          return;
        }

        RefPtr<CookieStoreChild::GetSubscriptionsRequestPromise> ipcPromise =
            self->mActor->SendGetSubscriptionsRequest(principalInfo,
                                                      self->mScopeURL);
        if (NS_WARN_IF(!ipcPromise)) {
          promise->MaybeResolve(nsTArray<CookieStoreGetOptions>());
          return;
        }

        ipcPromise->Then(
            NS_GetCurrentThread(), __func__,
            [promise = RefPtr(promise)](
                const CookieStoreChild::GetSubscriptionsRequestPromise::
                    ResolveOrRejectValue& aResult) {
              if (promise->GetParentObject()->IsDying()) {
                return;
              }

              if (aResult.IsReject()) {
                promise->MaybeResolve(nsTArray<CookieStoreGetOptions>());
                return;
              }

              nsTArray<CookieStoreGetOptions> results;
              for (const auto& subscription : aResult.ResolveValue()) {
                CookieStoreGetOptions* result = results.AppendElement();

                if (subscription.name().isSome()) {
                  result->mName.Construct();
                  result->mName.Value() = subscription.name().value();
                }

                result->mUrl.Construct();
                result->mUrl.Value() = subscription.url();
              }

              promise->MaybeResolve(results);
            });
      }));

  return promise.forget();
}

already_AddRefed<Promise> CookieStoreManager::Unsubscribe(
    const CopyableTArray<CookieStoreGetOptions>& aSubscriptions,
    ErrorResult& aRv) {
  return SubscribeOrUnsubscribe(Action::eUnsubscribe, aSubscriptions, aRv);
}

already_AddRefed<Promise> CookieStoreManager::SubscribeOrUnsubscribe(
    Action aAction, const CopyableTArray<CookieStoreGetOptions>& aSubscriptions,
    ErrorResult& aRv) {
  nsCOMPtr<nsIPrincipal> principal = RetrievePrincipal(mGlobalObject);
  if (NS_WARN_IF(!principal)) {
    aRv.ThrowInvalidStateError("Invalid context");
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(mGlobalObject, aRv);
  if (NS_WARN_IF(!promise)) {
    return nullptr;
  }

  // ServiceWorkers only have one principal: it's either partitioned or
  // unpartitioned. If the "context" is partitioned, the window or the
  // WorkerPrivate will return a partitioned principal.

  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      __func__,
      [self = RefPtr(this), promise = RefPtr(promise),
       principal = RefPtr(principal.get()), aSubscriptions, aAction]() {
        if (promise->GetParentObject()->IsDying()) {
          return;
        }

        nsCOMPtr<nsIURI> baseURI;
        nsresult rv = NS_NewURI(getter_AddRefs(baseURI), self->mScopeURL,
                                nullptr, nullptr);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          promise->MaybeRejectWithTypeError<MSG_INVALID_URL>(self->mScopeURL);
          return;
        }

        if (NS_WARN_IF(!baseURI)) {
          promise->MaybeRejectWithSecurityError(
              "Couldn't acquire the base URI of this context");
          return;
        }

        nsTArray<CookieSubscription> subscriptions;

        for (const CookieStoreGetOptions& subscription : aSubscriptions) {
          nsString subscriptionURL;
          if (subscription.mUrl.WasPassed()) {
            subscriptionURL.Assign(subscription.mUrl.Value());
          }

          nsCOMPtr<nsIURI> uri;
          rv =
              NS_NewURI(getter_AddRefs(uri), subscriptionURL, nullptr, baseURI);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            promise->MaybeRejectWithTypeError<MSG_INVALID_URL>(
                NS_ConvertUTF16toUTF8(subscriptionURL));
            return;
          }

          nsAutoCString subscriptionURI;
          rv = uri->GetSpec(subscriptionURI);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            promise->MaybeRejectWithTypeError<MSG_INVALID_URL>(
                NS_ConvertUTF16toUTF8(subscriptionURL));
            return;
          }

          if (!StringBeginsWith(subscriptionURI, self->mScopeURL)) {
            promise->MaybeRejectWithTypeError<MSG_INVALID_URL>(
                NS_ConvertUTF16toUTF8(subscriptionURL));
            return;
          }

          subscriptions.AppendElement(CookieSubscription{
              subscription.mName.WasPassed() ? Some(subscription.mName.Value())
                                             : Nothing(),
              NS_ConvertUTF8toUTF16(subscriptionURI)});
        }

        if (!self->MaybeCreateActor()) {
          promise->MaybeRejectWithNotAllowedError("Permission denied");
          return;
        }

        PrincipalInfo principalInfo;
        rv = PrincipalToPrincipalInfo(principal, &principalInfo);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          promise->MaybeResolveWithUndefined();
          return;
        }

        RefPtr<CookieStoreChild::SubscribeOrUnsubscribeRequestPromise>
            ipcPromise = self->mActor->SendSubscribeOrUnsubscribeRequest(
                principalInfo, self->mScopeURL, subscriptions,
                (aAction == Action::eSubscribe));
        if (NS_WARN_IF(!ipcPromise)) {
          promise->MaybeResolveWithUndefined();
          return;
        }

        ipcPromise->Then(
            NS_GetCurrentThread(), __func__,
            [promise = RefPtr(promise)](
                const CookieStoreChild::SubscribeOrUnsubscribeRequestPromise::
                    ResolveOrRejectValue& aResult) {
              // TODO We don't really want to expose internal errors.
              promise->MaybeResolveWithUndefined();
            });
      }));

  return promise.forget();
}

bool CookieStoreManager::MaybeCreateActor() {
  if (mActor) {
    return mActor->CanSend();
  }

  mozilla::ipc::PBackgroundChild* actorChild =
      mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    // The process is probably shutting down. Let's return a 'generic' error.
    return false;
  }

  PCookieStoreChild* actor = actorChild->SendPCookieStoreConstructor();
  if (!actor) {
    return false;
  }

  mActor = static_cast<CookieStoreChild*>(actor);

  return true;
}

void CookieStoreManager::Shutdown() {
  if (mActor) {
    mActor->Close();
    mActor = nullptr;
  }
}

}  // namespace mozilla::dom
