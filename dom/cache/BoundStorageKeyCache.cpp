/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BoundStorageKeyCache.h"

#include "mozilla/dom/CacheBinding.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/cache/AutoUtils.h"
#include "mozilla/dom/cache/Cache.h"
#include "mozilla/dom/cache/CacheChild.h"
#include "mozilla/dom/cache/CacheWorkerRef.h"
// #include "mozilla/ErrorResult.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom::cache {

NS_IMPL_ISUPPORTS(BoundStorageKeyCache, nsISupports)

using mozilla::ipc::PBackgroundChild;

BoundStorageKeyCache::BoundStorageKeyCache(nsIGlobalObject* aGlobal,
                                           CacheChild* aActor,
                                           Namespace aNamespace)
    : mGlobal(aGlobal), mActor(aActor), mNamespace(aNamespace) {
  MOZ_DIAGNOSTIC_ASSERT(mGlobal);
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mNamespace != INVALID_NAMESPACE);
  mActor->SetListener(this);
}

// static
bool BoundStorageKeyCache::CachesEnabled(JSContext* aCx, JSObject* aObj) {
  if (!IsSecureContextOrObjectIsFromSecureContext(aCx, aObj)) {
    return StaticPrefs::dom_caches_testing_enabled() ||
           ServiceWorkersEnabled(aCx, aObj);
  }
  return true;
}

auto BoundStorageKeyCache::Match(JSContext* aCx,
                                 const RequestOrUTF8String& aRequest,
                                 const CacheQueryOptions& aOptions,
                                 ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  SafeRefPtr<InternalRequest> ir =
      ToInternalRequest(aCx, aRequest, IgnoreBody, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  CacheQueryParams params;
  ToCacheQueryParams(params, aOptions);

  AutoChildOpArgs args(
      this, CacheMatchArgs(CacheRequest(), params, GetOpenMode()), 1);

  args.Add(*ir, IgnoreBody, IgnoreInvalidScheme, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<CachePromise> promise = new MatchResultPromise::Private(__func__);
  ExecuteOp(args, promise, aRv);
  return promise.forget();
}

auto BoundStorageKeyCache::MatchAll(
    JSContext* aCx, const Optional<RequestOrUTF8String>& aRequest,
    const CacheQueryOptions& aOptions, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  CacheQueryParams params;
  ToCacheQueryParams(params, aOptions);

  AutoChildOpArgs args(this,
                       CacheMatchAllArgs(Nothing(), params, GetOpenMode()), 1);

  if (aRequest.WasPassed()) {
    SafeRefPtr<InternalRequest> ir =
        ToInternalRequest(aCx, aRequest.Value(), IgnoreBody, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }

    args.Add(*ir, IgnoreBody, IgnoreInvalidScheme, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  RefPtr<CachePromise> promise{new MatchAllResultPromise::Private(__func__)};
  ExecuteOp(args, promise, aRv);

  return promise.forget();
}

auto BoundStorageKeyCache::Add(JSContext* aContext,
                               const RequestOrUTF8String& aRequest,
                               CallerType aCallerType, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  if (!IsValidPutRequestMethod(aRequest, aRv)) {
    return nullptr;
  }

  GlobalObject global(aContext, mGlobal->GetGlobalJSObject());
  MOZ_DIAGNOSTIC_ASSERT(!global.Failed());

  nsTArray<SafeRefPtr<Request>> requestList(1);
  RootedDictionary<RequestInit> requestInit(aContext);
  SafeRefPtr<Request> request =
      Request::Constructor(global, aRequest, requestInit, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  SafeRefPtr<InternalRequest> ireq = request->GetInternalRequest();
  if (NS_WARN_IF(!IsValidPutRequestURL(ireq->GetURLWithoutFragment(), aRv))) {
    return nullptr;
  }

  requestList.AppendElement(std::move(request));
  return AddAll(global, std::move(requestList), aCallerType, aRv);
}

auto BoundStorageKeyCache::AddAll(
    JSContext* aContext,
    const Sequence<OwningRequestOrUTF8String>& aRequestList,
    CallerType aCallerType, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  GlobalObject global(aContext, mGlobal->GetGlobalJSObject());
  MOZ_DIAGNOSTIC_ASSERT(!global.Failed());

  nsTArray<SafeRefPtr<Request>> requestList(aRequestList.Length());
  for (uint32_t i = 0; i < aRequestList.Length(); ++i) {
    RequestOrUTF8String requestOrString;

    if (aRequestList[i].IsRequest()) {
      requestOrString.SetAsRequest() = aRequestList[i].GetAsRequest();
      if (NS_WARN_IF(
              !IsValidPutRequestMethod(requestOrString.GetAsRequest(), aRv))) {
        return nullptr;
      }
    } else {
      requestOrString.SetAsUTF8String() = aRequestList[i].GetAsUTF8String();
    }

    RootedDictionary<RequestInit> requestInit(aContext);
    SafeRefPtr<Request> request =
        Request::Constructor(global, requestOrString, requestInit, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    SafeRefPtr<InternalRequest> ireq = request->GetInternalRequest();
    if (NS_WARN_IF(!IsValidPutRequestURL(ireq->GetURLWithoutFragment(), aRv))) {
      return nullptr;
    }

    requestList.AppendElement(std::move(request));
  }

  return AddAll(global, std::move(requestList), aCallerType, aRv);
}

auto BoundStorageKeyCache::Put(JSContext* aCx,
                               const RequestOrUTF8String& aRequest,
                               Response& aResponse, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  if (NS_WARN_IF(!IsValidPutRequestMethod(aRequest, aRv))) {
    return nullptr;
  }

  if (!IsValidPutResponseStatus(aResponse, PutStatusPolicy::Default, aRv)) {
    return nullptr;
  }

  if (NS_WARN_IF(aResponse.GetPrincipalInfo() &&
                 aResponse.GetPrincipalInfo()->type() ==
                     mozilla::ipc::PrincipalInfo::TExpandedPrincipalInfo)) {
    // WebExtensions Content Scripts can currently run fetch from their global
    // which will end up to have an expanded principal, but we require that the
    // contents of Cache storage for the content origin to be same-origin, and
    // never an expanded principal (See Bug 1753810).
    aRv.ThrowSecurityError("Disallowed on WebExtension ContentScript Request");
    return nullptr;
  }

  SafeRefPtr<InternalRequest> ir =
      ToInternalRequest(aCx, aRequest, ReadBody, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  AutoChildOpArgs args(this, CachePutAllArgs(), 1);

  args.Add(aCx, *ir, ReadBody, TypeErrorOnInvalidScheme, aResponse, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<CachePromise> promise{new PutResultPromise::Private(__func__)};
  ExecuteOp(args, promise, aRv);

  return promise.forget();
}

auto BoundStorageKeyCache::Delete(JSContext* aCx,
                                  const RequestOrUTF8String& aRequest,
                                  const CacheQueryOptions& aOptions,
                                  ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  SafeRefPtr<InternalRequest> ir =
      ToInternalRequest(aCx, aRequest, IgnoreBody, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  CacheQueryParams params;
  ToCacheQueryParams(params, aOptions);

  AutoChildOpArgs args(this, CacheDeleteArgs(CacheRequest(), params), 1);

  args.Add(*ir, IgnoreBody, IgnoreInvalidScheme, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<CachePromise> promise{new DeleteResultPromise::Private(__func__)};
  ExecuteOp(args, promise, aRv);

  return promise.forget();
}

auto BoundStorageKeyCache::Keys(JSContext* aCx,
                                const Optional<RequestOrUTF8String>& aRequest,
                                const CacheQueryOptions& aOptions,
                                ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  CacheQueryParams params;
  ToCacheQueryParams(params, aOptions);

  AutoChildOpArgs args(this, CacheKeysArgs(Nothing(), params, GetOpenMode()),
                       1);

  if (aRequest.WasPassed()) {
    SafeRefPtr<InternalRequest> ir =
        ToInternalRequest(aCx, aRequest.Value(), IgnoreBody, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    args.Add(*ir, IgnoreBody, IgnoreInvalidScheme, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  RefPtr<CachePromise> promise{new KeysResultPromise::Private(__func__)};
  ExecuteOp(args, promise, aRv);

  return promise.forget();
}

void BoundStorageKeyCache::OnActorDestroy(CacheChild* aActor) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mActor == aActor);

  mActor->ClearListener();
  mActor = nullptr;
}

nsIGlobalObject* BoundStorageKeyCache::GetGlobalObject() const {
  return mGlobal;
}

#ifdef DEBUG
void BoundStorageKeyCache::AssertOwningThread() const {
  NS_ASSERT_OWNINGTHREAD(Cache);
}
#endif

BoundStorageKeyCache::~BoundStorageKeyCache() {
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyCache);
  if (mActor) {
    mActor->StartDestroyFromListener();
    // OnActorDestroy() is called synchronously by StartDestroyFromListener().
    // So we should have already cleared the mActor.
    MOZ_DIAGNOSTIC_ASSERT(!mActor);
  }
}

void BoundStorageKeyCache::ExecuteOp(AutoChildOpArgs& aOpArgs,
                                     RefPtr<CachePromise>& aPromise,
                                     ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  mActor->ExecuteOp(mGlobal, aPromise, this, aOpArgs.SendAsOpArgs());
}

auto BoundStorageKeyCache::AddAll(const GlobalObject& aGlobal,
                                  nsTArray<SafeRefPtr<Request>>&& aRequestList,
                                  CallerType aCallerType, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  MOZ_DIAGNOSTIC_ASSERT(mActor);

  // Fetch doesn't work on non-main threads yet.
  RefPtr<CachePromise> promise = AddAllResultPromise::CreateAndReject(
      ErrorResult(NS_ERROR_FAILURE), __func__);
  return promise.forget();
}

auto BoundStorageKeyCache::PutAll(
    JSContext* aCx, const nsTArray<SafeRefPtr<Request>>& aRequestList,
    const nsTArray<RefPtr<Response>>& aResponseList, ErrorResult& aRv)
    -> already_AddRefed<CachePromise> {
  MOZ_DIAGNOSTIC_ASSERT(aRequestList.Length() == aResponseList.Length());

  if (NS_WARN_IF(!mActor)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  CacheChild::AutoLock actorLock(*mActor);

  AutoChildOpArgs args(this, CachePutAllArgs(), aRequestList.Length());

  for (uint32_t i = 0; i < aRequestList.Length(); ++i) {
    SafeRefPtr<InternalRequest> ir = aRequestList[i]->GetInternalRequest();
    args.Add(aCx, *ir, ReadBody, TypeErrorOnInvalidScheme, *aResponseList[i],
             aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  RefPtr<CachePromise> promise{new PutResultPromise::Private(__func__)};
  ExecuteOp(args, promise, aRv);

  return promise.forget();
}

OpenMode BoundStorageKeyCache::GetOpenMode() const {
  return mNamespace == CHROME_ONLY_NAMESPACE ? OpenMode::Eager : OpenMode::Lazy;
}

}  // namespace mozilla::dom::cache
