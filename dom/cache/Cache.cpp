/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/Cache.h"

#include "js/Array.h"               // JS::GetArrayLength, JS::IsArrayObject
#include "js/PropertyAndElement.h"  // JS_GetElement
#include "mozilla/ErrorResult.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/CacheBinding.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/cache/AutoUtils.h"
#include "mozilla/dom/cache/CacheChild.h"
#include "mozilla/dom/cache/CacheCommon.h"
#include "mozilla/dom/cache/CacheWorkerRef.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom::cache {

using mozilla::ipc::PBackgroundChild;

bool IsValidPutRequestURL(nsIURI* aUrl, ErrorResult& aRv) {
  if (!TypeUtils::URLHasValidScheme(aUrl)) {
    aRv.ThrowTypeError<MSG_INVALID_URL_SCHEME>("Request",
                                               aUrl->GetSpecOrDefault());
    return false;
  }

  return true;
}

bool IsValidPutRequestMethod(const Request& aRequest, ErrorResult& aRv) {
  nsAutoCString method;
  aRequest.GetMethod(method);
  if (!method.LowerCaseEqualsLiteral("get")) {
    aRv.ThrowTypeError<MSG_INVALID_REQUEST_METHOD>(method);
    return false;
  }

  return true;
}

bool IsValidPutRequestMethod(const RequestOrUTF8String& aRequest,
                             ErrorResult& aRv) {
  // If the provided request is a string URL, then it will default to
  // a valid http method automatically.
  if (!aRequest.IsRequest()) {
    return true;
  }
  return IsValidPutRequestMethod(aRequest.GetAsRequest(), aRv);
}

bool IsValidPutResponseStatus(Response& aResponse, PutStatusPolicy aPolicy,
                              ErrorResult& aRv) {
  if ((aPolicy == PutStatusPolicy::RequireOK && !aResponse.Ok()) ||
      aResponse.Status() == 206) {
    nsAutoCString url;
    aResponse.GetUrl(url);
    aRv.ThrowTypeError<MSG_CACHE_ADD_FAILED_RESPONSE>(
        GetEnumString(aResponse.Type()), IntToCString(aResponse.Status()), url);
    return false;
  }

  return true;
}

// Helper class to wait for Add()/AddAll() fetch requests to complete and
// then perform a PutAll() with the responses.  This class holds a WorkerRef
// to keep the Worker thread alive.  This is mainly to ensure that Add/AddAll
// act the same as other Cache operations that directly create a CacheOpChild
// actor.
class Cache::FetchHandler final : public PromiseNativeHandler {
 public:
  FetchHandler(SafeRefPtr<CacheWorkerRef> aWorkerRef, Cache* aCache,
               nsTArray<SafeRefPtr<Request>>&& aRequestList, Promise* aPromise)
      : mWorkerRef(std::move(aWorkerRef)),
        mCache(aCache),
        mRequestList(std::move(aRequestList)),
        mPromise(aPromise) {
    MOZ_ASSERT_IF(!NS_IsMainThread(), mWorkerRef);
    MOZ_DIAGNOSTIC_ASSERT(mCache);
    MOZ_DIAGNOSTIC_ASSERT(mPromise);
  }

  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override {
    NS_ASSERT_OWNINGTHREAD(FetchHandler);

    // Stop holding the worker alive when we leave this method.
    const SafeRefPtr<CacheWorkerRef> workerRef = std::move(mWorkerRef);

    // Promise::All() passed an array of fetch() Promises should give us
    // an Array of Response objects.  The following code unwraps these
    // JS values back to an nsTArray<RefPtr<Response>>.

    AutoTArray<RefPtr<Response>, 256> responseList;
    responseList.SetCapacity(mRequestList.Length());

    const auto failOnErr = [this](const auto) { Fail(); };

    bool isArray;
    QM_TRY(OkIf(JS::IsArrayObject(aCx, aValue, &isArray)), QM_VOID, failOnErr);
    QM_TRY(OkIf(isArray), QM_VOID, failOnErr);

    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());

    uint32_t length;
    QM_TRY(OkIf(JS::GetArrayLength(aCx, obj, &length)), QM_VOID, failOnErr);

    for (uint32_t i = 0; i < length; ++i) {
      JS::Rooted<JS::Value> value(aCx);

      QM_TRY(OkIf(JS_GetElement(aCx, obj, i, &value)), QM_VOID, failOnErr);

      QM_TRY(OkIf(value.isObject()), QM_VOID, failOnErr);

      JS::Rooted<JSObject*> responseObj(aCx, &value.toObject());

      RefPtr<Response> response;
      QM_TRY(MOZ_TO_RESULT(UNWRAP_OBJECT(Response, responseObj, response)),
             QM_VOID, failOnErr);

      QM_TRY(OkIf(response->Type() != ResponseType::Error), QM_VOID, failOnErr);

      // Do not allow the convenience methods .add()/.addAll() to store failed
      // or invalid responses.  A consequence of this is that these methods
      // cannot be used to store opaque or opaqueredirect responses since they
      // always expose a 0 status value.
      ErrorResult errorResult;
      if (!IsValidPutResponseStatus(*response, PutStatusPolicy::RequireOK,
                                    errorResult)) {
        // TODO: abort the fetch requests we have running (bug 1157434)
        mPromise->MaybeReject(std::move(errorResult));
        return;
      }

      responseList.AppendElement(std::move(response));
    }

    MOZ_DIAGNOSTIC_ASSERT(mRequestList.Length() == responseList.Length());

    // Now store the unwrapped Response list in the Cache.
    ErrorResult result;
    // TODO: Here we use the JSContext as received by the ResolvedCallback, and
    // its state could be the wrong one. The spec doesn't say anything
    // about it, yet (bug 1384006)
    RefPtr<Promise> put =
        mCache->PutAll(aCx, mRequestList, responseList, result);
    result.WouldReportJSException();
    if (NS_WARN_IF(result.Failed())) {
      // TODO: abort the fetch requests we have running (bug 1157434)
      mPromise->MaybeReject(std::move(result));
      return;
    }

    // Chain the Cache::Put() promise to the original promise returned to
    // the content script.
    mPromise->MaybeResolve(put);
  }

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override {
    NS_ASSERT_OWNINGTHREAD(FetchHandler);
    Fail();
  }

 private:
  ~FetchHandler() = default;

  void Fail() { mPromise->MaybeRejectWithTypeError<MSG_FETCH_FAILED>(); }

  SafeRefPtr<CacheWorkerRef> mWorkerRef;
  RefPtr<Cache> mCache;
  nsTArray<SafeRefPtr<Request>> mRequestList;
  RefPtr<Promise> mPromise;

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS0(Cache::FetchHandler)

NS_IMPL_CYCLE_COLLECTING_ADDREF(mozilla::dom::cache::Cache);
NS_IMPL_CYCLE_COLLECTING_RELEASE(mozilla::dom::cache::Cache);
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(mozilla::dom::cache::Cache, mGlobal);

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Cache)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

Cache::Cache(nsIGlobalObject* aGlobal, CacheChild* aActor, Namespace aNamespace)
    : mGlobal(aGlobal), mActor(aActor), mNamespace(aNamespace) {
  MOZ_DIAGNOSTIC_ASSERT(mGlobal);
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mNamespace != INVALID_NAMESPACE);
  mActor->SetListener(this);
}

// static
bool Cache::CachesEnabled(JSContext* aCx, JSObject* aObj) {
  if (!IsSecureContextOrObjectIsFromSecureContext(aCx, aObj)) {
    return StaticPrefs::dom_caches_testing_enabled() ||
           ServiceWorkersEnabled(aCx, aObj);
  }
  return true;
}

already_AddRefed<Promise> Cache::Match(JSContext* aCx,
                                       const RequestOrUTF8String& aRequest,
                                       const CacheQueryOptions& aOptions,
                                       ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

already_AddRefed<Promise> Cache::MatchAll(
    JSContext* aCx, const Optional<RequestOrUTF8String>& aRequest,
    const CacheQueryOptions& aOptions, ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

already_AddRefed<Promise> Cache::Add(JSContext* aContext,
                                     const RequestOrUTF8String& aRequest,
                                     CallerType aCallerType, ErrorResult& aRv) {
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

already_AddRefed<Promise> Cache::AddAll(
    JSContext* aContext,
    const Sequence<OwningRequestOrUTF8String>& aRequestList,
    CallerType aCallerType, ErrorResult& aRv) {
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

already_AddRefed<Promise> Cache::Put(JSContext* aCx,
                                     const RequestOrUTF8String& aRequest,
                                     Response& aResponse, ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

already_AddRefed<Promise> Cache::Delete(JSContext* aCx,
                                        const RequestOrUTF8String& aRequest,
                                        const CacheQueryOptions& aOptions,
                                        ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

already_AddRefed<Promise> Cache::Keys(
    JSContext* aCx, const Optional<RequestOrUTF8String>& aRequest,
    const CacheQueryOptions& aOptions, ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

nsISupports* Cache::GetParentObject() const { return mGlobal; }

JSObject* Cache::WrapObject(JSContext* aContext,
                            JS::Handle<JSObject*> aGivenProto) {
  return Cache_Binding::Wrap(aContext, this, aGivenProto);
}

void Cache::OnActorDestroy(CacheChild* aActor) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);
  MOZ_DIAGNOSTIC_ASSERT(mActor == aActor);
  mActor->ClearListener();
  mActor = nullptr;
}

nsIGlobalObject* Cache::GetGlobalObject() const { return mGlobal; }

#ifdef DEBUG
void Cache::AssertOwningThread() const { NS_ASSERT_OWNINGTHREAD(Cache); }
#endif

Cache::~Cache() {
  NS_ASSERT_OWNINGTHREAD(Cache);
  if (mActor) {
    mActor->StartDestroyFromListener();
    // OnActorDestroy() is called synchronously by StartDestroyFromListener().
    // So we should have already cleared the mActor.
    MOZ_DIAGNOSTIC_ASSERT(!mActor);
  }
}

already_AddRefed<Promise> Cache::ExecuteOp(AutoChildOpArgs& aOpArgs,
                                           ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);

  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (NS_WARN_IF(!promise)) {
    return nullptr;
  }

  mActor->ExecuteOp(mGlobal, promise, this, aOpArgs.SendAsOpArgs());
  return promise.forget();
}

already_AddRefed<Promise> Cache::AddAll(
    const GlobalObject& aGlobal, nsTArray<SafeRefPtr<Request>>&& aRequestList,
    CallerType aCallerType, ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mActor);

  // If there is no work to do, then resolve immediately
  if (aRequestList.IsEmpty()) {
    RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
    if (NS_WARN_IF(!promise)) {
      return nullptr;
    }

    promise->MaybeResolveWithUndefined();
    return promise.forget();
  }

  AutoTArray<RefPtr<Promise>, 256> fetchList;
  fetchList.SetCapacity(aRequestList.Length());

  // Begin fetching each request in parallel.  For now, if an error occurs just
  // abandon our previous fetch calls.  In theory we could cancel them in the
  // future once fetch supports it.

  for (uint32_t i = 0; i < aRequestList.Length(); ++i) {
    RequestOrUTF8String requestOrString;
    requestOrString.SetAsRequest() = aRequestList[i].unsafeGetRawPtr();
    RootedDictionary<RequestInit> requestInit(aGlobal.Context());
    RefPtr<Promise> fetch =
        FetchRequest(mGlobal, requestOrString, requestInit, aCallerType, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    fetchList.AppendElement(std::move(fetch));
  }

  RefPtr<Promise> promise = Promise::Create(mGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<FetchHandler> handler =
      new FetchHandler(mActor->GetWorkerRefPtr().clonePtr(), this,
                       std::move(aRequestList), promise);

  RefPtr<Promise> fetchPromise =
      Promise::All(aGlobal.Context(), fetchList, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  fetchPromise->AppendNativeHandler(handler);

  return promise.forget();
}

already_AddRefed<Promise> Cache::PutAll(
    JSContext* aCx, const nsTArray<SafeRefPtr<Request>>& aRequestList,
    const nsTArray<RefPtr<Response>>& aResponseList, ErrorResult& aRv) {
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

  return ExecuteOp(args, aRv);
}

OpenMode Cache::GetOpenMode() const {
  return mNamespace == CHROME_ONLY_NAMESPACE ? OpenMode::Eager : OpenMode::Lazy;
}

}  // namespace mozilla::dom::cache
