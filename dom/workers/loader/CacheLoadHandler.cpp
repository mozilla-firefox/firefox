/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheLoadHandler.h"
#include "ScriptResponseHeaderProcessor.h"  // ScriptResponseHeaderProcessor
#include "WorkerLoadContext.h"              // WorkerLoadContext

#include "nsIPrincipal.h"

#include "nsIThreadRetargetableRequest.h"
#include "nsIXPConnect.h"

#include "jsapi.h"
#include "nsNetUtil.h"

#include "mozilla/Assertions.h"
#include "mozilla/Encoding.h"
#include "mozilla/dom/CacheBinding.h"
#include "mozilla/dom/cache/CacheTypes.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/ServiceWorkerBinding.h"  // ServiceWorkerState
#include "mozilla/Result.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WorkerScope.h"

#include "mozilla/dom/workerinternals/ScriptLoader.h"  // WorkerScriptLoader

namespace mozilla {
namespace dom {

namespace workerinternals::loader {

NS_IMPL_ISUPPORTS0(CacheCreator)

NS_IMPL_ISUPPORTS(CacheLoadHandler, nsIStreamLoaderObserver)

NS_IMPL_ISUPPORTS0(CachePromiseHandler)

CachePromiseHandler::CachePromiseHandler(
    WorkerScriptLoader* aLoader, ThreadSafeRequestHandle* aRequestHandle)
    : mLoader(aLoader), mRequestHandle(aRequestHandle) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mLoader);
}

void CachePromiseHandler::ResolvedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aValue,
                                           ErrorResult& aRv) {
  AssertIsOnMainThread();
  if (mRequestHandle->IsEmpty()) {
    return;
  }
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  // May already have been canceled by CacheLoadHandler::Fail from
  // CancelMainThread.
  MOZ_ASSERT(loadContext->mCacheStatus == WorkerLoadContext::WritingToCache ||
             loadContext->mCacheStatus == WorkerLoadContext::Cancel);
  MOZ_ASSERT_IF(loadContext->mCacheStatus == WorkerLoadContext::Cancel,
                !loadContext->mCachePromise);

  if (loadContext->mCachePromise) {
    loadContext->mCacheStatus = WorkerLoadContext::Cached;
    loadContext->mCachePromise = nullptr;
    mRequestHandle->MaybeExecuteFinishedScripts();
  }
}

void CachePromiseHandler::RejectedCallback(JSContext* aCx,
                                           JS::Handle<JS::Value> aValue,
                                           ErrorResult& aRv) {
  AssertIsOnMainThread();
  if (mRequestHandle->IsEmpty()) {
    return;
  }
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  // May already have been canceled by CacheLoadHandler::Fail from
  // CancelMainThread.
  MOZ_ASSERT(loadContext->mCacheStatus == WorkerLoadContext::WritingToCache ||
             loadContext->mCacheStatus == WorkerLoadContext::Cancel);
  loadContext->mCacheStatus = WorkerLoadContext::Cancel;

  loadContext->mCachePromise = nullptr;

  // This will delete the cache object and will call LoadingFinished() with an
  // error for each ongoing operation.
  auto* cacheCreator = mRequestHandle->GetCacheCreator();
  if (cacheCreator) {
    cacheCreator->DeleteCache(NS_ERROR_FAILURE);
  }
}

CacheCreator::CacheCreator(WorkerPrivate* aWorkerPrivate)
    : mCacheName(aWorkerPrivate->ServiceWorkerCacheName()),
      mOriginAttributes(aWorkerPrivate->GetOriginAttributes()) {
  MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
}

nsresult CacheCreator::CreateCacheStorage(nsIPrincipal* aPrincipal) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mCacheStorage);
  MOZ_ASSERT(aPrincipal);

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  MOZ_ASSERT(xpc, "This should never be null!");

  AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> sandbox(cx);
  nsresult rv = xpc->CreateSandbox(cx, aPrincipal, sandbox.address());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // The JSContext is not in a realm, so CreateSandbox returned an unwrapped
  // global.
  MOZ_ASSERT(JS_IsGlobalObject(sandbox));

  mSandboxGlobalObject = xpc::NativeGlobal(sandbox);
  if (NS_WARN_IF(!mSandboxGlobalObject)) {
    return NS_ERROR_FAILURE;
  }

  // Create a CacheStorage bypassing its trusted origin checks.  The
  // ServiceWorker has already performed its own checks before getting
  // to this point.
  ErrorResult error;
  mCacheStorage = CacheStorage::CreateOnMainThread(
      mozilla::dom::cache::CHROME_ONLY_NAMESPACE, mSandboxGlobalObject,
      aPrincipal, true /* force trusted origin */, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  return NS_OK;
}

nsresult CacheCreator::Load(nsIPrincipal* aPrincipal) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mLoaders.IsEmpty());

  nsresult rv = CreateCacheStorage(aPrincipal);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  ErrorResult error;
  MOZ_ASSERT(!mCacheName.IsEmpty());
  RefPtr<Promise> promise = mCacheStorage->Open(mCacheName, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  promise->AppendNativeHandler(this);
  return NS_OK;
}

void CacheCreator::FailLoaders(nsresult aRv) {
  AssertIsOnMainThread();

  // Fail() can call LoadingFinished() which may call ExecuteFinishedScripts()
  // which sets mCacheCreator to null, so hold a ref.
  RefPtr<CacheCreator> kungfuDeathGrip = this;

  for (uint32_t i = 0, len = mLoaders.Length(); i < len; ++i) {
    mLoaders[i]->Fail(aRv);
  }

  mLoaders.Clear();
}

void CacheCreator::RejectedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  AssertIsOnMainThread();
  FailLoaders(NS_ERROR_FAILURE);
}

void CacheCreator::ResolvedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  AssertIsOnMainThread();
  if (!aValue.isObject()) {
    FailLoaders(NS_ERROR_FAILURE);
    return;
  }

  JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
  Cache* cache = nullptr;
  nsresult rv = UNWRAP_OBJECT(Cache, &obj, cache);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailLoaders(NS_ERROR_FAILURE);
    return;
  }

  mCache = cache;
  MOZ_DIAGNOSTIC_ASSERT(mCache);

  // If the worker is canceled, CancelMainThread() will have cleared the
  // loaders via DeleteCache().
  for (uint32_t i = 0, len = mLoaders.Length(); i < len; ++i) {
    mLoaders[i]->Load(cache);
  }
}

void CacheCreator::DeleteCache(nsresult aReason) {
  AssertIsOnMainThread();

  // This is called when the load is canceled which can occur before
  // mCacheStorage is initialized.
  if (mCacheStorage) {
    // It's safe to do this while Cache::Match() and Cache::Put() calls are
    // running.
    RefPtr<Promise> promise = mCacheStorage->Delete(mCacheName, IgnoreErrors());

    // We don't care to know the result of the promise object.
  }

  // Always call this here to ensure the loaders array is cleared.
  FailLoaders(NS_ERROR_FAILURE);
}

CacheLoadHandler::CacheLoadHandler(ThreadSafeWorkerRef* aWorkerRef,
                                   ThreadSafeRequestHandle* aRequestHandle,
                                   bool aIsWorkerScript,
                                   bool aOnlyExistingCachedResourcesAllowed,
                                   WorkerScriptLoader* aLoader)
    : mRequestHandle(aRequestHandle),
      mLoader(aLoader),
      mWorkerRef(aWorkerRef),
      mIsWorkerScript(aIsWorkerScript),
      mFailed(false),
      mOnlyExistingCachedResourcesAllowed(aOnlyExistingCachedResourcesAllowed) {
  MOZ_ASSERT(aWorkerRef);
  MOZ_ASSERT(aWorkerRef->Private()->IsServiceWorker());
  mMainThreadEventTarget = aWorkerRef->Private()->MainThreadEventTarget();
  MOZ_ASSERT(mMainThreadEventTarget);
  mBaseURI = mLoader->GetBaseURI();
  AssertIsOnMainThread();

  // Worker scripts are always decoded as UTF-8 per spec.
  mDecoder = MakeUnique<ScriptDecoder>(UTF_8_ENCODING,
                                       ScriptDecoder::BOMHandling::Remove);
}

void CacheLoadHandler::Fail(nsresult aRv) {
  AssertIsOnMainThread();
  MOZ_ASSERT(NS_FAILED(aRv));

  if (mFailed) {
    return;
  }

  mFailed = true;

  if (mPump) {
    MOZ_ASSERT_IF(!mRequestHandle->IsEmpty(),
                  mRequestHandle->GetContext()->mCacheStatus ==
                      WorkerLoadContext::ReadingFromCache);
    mPump->Cancel(aRv);
    mPump = nullptr;
  }
  if (mRequestHandle->IsEmpty()) {
    return;
  }

  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  loadContext->mCacheStatus = WorkerLoadContext::Cancel;

  if (loadContext->mCachePromise) {
    loadContext->mCachePromise->MaybeReject(aRv);
  }

  loadContext->mCachePromise = nullptr;

  mRequestHandle->LoadingFinished(aRv);
}

void CacheLoadHandler::Load(Cache* aCache) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aCache);
  MOZ_ASSERT(!mRequestHandle->IsEmpty());
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), loadContext->mRequest->mURL,
                          nullptr, mBaseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  MOZ_ASSERT(loadContext->mFullURL.IsEmpty());
  rv = uri->GetSpec(loadContext->mFullURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  mozilla::dom::RequestOrUTF8String request;
  request.SetAsUTF8String().ShareOrDependUpon(loadContext->mFullURL);

  mozilla::dom::CacheQueryOptions params;

  // This JSContext will not end up executing JS code because here there are
  // no ReadableStreams involved.
  AutoJSAPI jsapi;
  jsapi.Init();

  ErrorResult error;
  RefPtr<Promise> promise = aCache->Match(jsapi.cx(), request, params, error);
  if (NS_WARN_IF(error.Failed())) {
    Fail(error.StealNSResult());
    return;
  }

  promise->AppendNativeHandler(this);
}

void CacheLoadHandler::RejectedCallback(JSContext* aCx,
                                        JS::Handle<JS::Value> aValue,
                                        ErrorResult& aRv) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mRequestHandle->IsEmpty());

  MOZ_ASSERT(mRequestHandle->GetContext()->mCacheStatus ==
             WorkerLoadContext::Uncached);
  Fail(NS_ERROR_FAILURE);
}

void CacheLoadHandler::ResolvedCallback(JSContext* aCx,
                                        JS::Handle<JS::Value> aValue,
                                        ErrorResult& aRv) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mRequestHandle->IsEmpty());
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  // If we have already called 'Fail', we should not proceed. If we cancelled,
  // we should similarily not proceed.
  if (mFailed) {
    return;
  }

  MOZ_ASSERT(loadContext->mCacheStatus == WorkerLoadContext::Uncached);

  nsresult rv;

  // The ServiceWorkerScriptCache will store data for any scripts it
  // it knows about.  This is always at least the top level script.
  // Depending on if a previous version of the service worker has
  // been installed or not it may also know about importScripts().  We
  // must handle loading and offlining new importScripts() here, however.
  if (aValue.isUndefined()) {
    // If this is the main script or we're not loading a new service worker
    // then this is an error.  This can happen for internal reasons, like
    // storage was probably wiped without removing the service worker
    // registration.  It can also happen for exposed reasons like the
    // service worker script calling importScripts() after install.
    if (NS_WARN_IF(mIsWorkerScript || mOnlyExistingCachedResourcesAllowed)) {
      Fail(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }

    loadContext->mCacheStatus = WorkerLoadContext::ToBeCached;
    rv = mLoader->LoadScript(mRequestHandle);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Fail(rv);
    }
    return;
  }

  MOZ_ASSERT(aValue.isObject());

  JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
  mozilla::dom::Response* response = nullptr;
  rv = UNWRAP_OBJECT(Response, &obj, response);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  InternalHeaders* headers = response->GetInternalHeaders();

  headers->Get("content-security-policy"_ns, mCSPHeaderValue, IgnoreErrors());
  headers->Get("content-security-policy-report-only"_ns,
               mCSPReportOnlyHeaderValue, IgnoreErrors());
  headers->Get("referrer-policy"_ns, mReferrerPolicyHeaderValue,
               IgnoreErrors());

  nsAutoCString coepHeader;
  headers->Get("cross-origin-embedder-policy"_ns, coepHeader, IgnoreErrors());

  nsILoadInfo::CrossOriginEmbedderPolicy coep =
      NS_GetCrossOriginEmbedderPolicyFromHeader(
          coepHeader, mWorkerRef->Private()->Trials().IsEnabled(
                          OriginTrial::CoepCredentialless));

  rv = ScriptResponseHeaderProcessor::ProcessCrossOriginEmbedderPolicyHeader(
      mWorkerRef->Private(), coep, loadContext->IsTopLevel());

  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  nsCOMPtr<nsIInputStream> inputStream;
  response->GetBody(getter_AddRefs(inputStream));
  mChannelInfo = response->GetChannelInfo();
  const UniquePtr<PrincipalInfo>& pInfo = response->GetPrincipalInfo();
  if (pInfo) {
    mPrincipalInfo = mozilla::MakeUnique<PrincipalInfo>(*pInfo);
  }

  if (!inputStream) {
    loadContext->mCacheStatus = WorkerLoadContext::Cached;

    if (mRequestHandle->IsCancelled()) {
      auto* cacheCreator = mRequestHandle->GetCacheCreator();
      if (cacheCreator) {
        cacheCreator->DeleteCache(mRequestHandle->GetCancelResult());
      }
      return;
    }

    nsresult rv = DataReceivedFromCache(
        (uint8_t*)"", 0, mChannelInfo, std::move(mPrincipalInfo),
        mCSPHeaderValue, mCSPReportOnlyHeaderValue, mReferrerPolicyHeaderValue);

    mRequestHandle->OnStreamComplete(rv);
    return;
  }

  MOZ_ASSERT(!mPump);
  rv = NS_NewInputStreamPump(getter_AddRefs(mPump), inputStream.forget(),
                             0,     /* default segsize */
                             0,     /* default segcount */
                             false, /* default closeWhenDone */
                             mMainThreadEventTarget);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  nsCOMPtr<nsIStreamLoader> loader;
  rv = NS_NewStreamLoader(getter_AddRefs(loader), this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Fail(rv);
    return;
  }

  rv = mPump->AsyncRead(loader);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mPump = nullptr;
    Fail(rv);
    return;
  }

  nsCOMPtr<nsIThreadRetargetableRequest> rr = do_QueryInterface(mPump);
  if (rr) {
    nsCOMPtr<nsIEventTarget> sts =
        do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
    RefPtr<TaskQueue> queue =
        TaskQueue::Create(sts.forget(), "CacheLoadHandler STS Delivery Queue");
    rv = rr->RetargetDeliveryTo(queue);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch the nsIInputStreamPump to a IO thread.");
    }
  }

  loadContext->mCacheStatus = WorkerLoadContext::ReadingFromCache;
}

NS_IMETHODIMP
CacheLoadHandler::OnStreamComplete(nsIStreamLoader* aLoader,
                                   nsISupports* aContext, nsresult aStatus,
                                   uint32_t aStringLen,
                                   const uint8_t* aString) {
  AssertIsOnMainThread();
  if (mRequestHandle->IsEmpty()) {
    return NS_OK;
  }
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  mPump = nullptr;

  if (NS_FAILED(aStatus)) {
    MOZ_ASSERT(loadContext->mCacheStatus ==
                   WorkerLoadContext::ReadingFromCache ||
               loadContext->mCacheStatus == WorkerLoadContext::Cancel);
    Fail(aStatus);
    return NS_OK;
  }

  MOZ_ASSERT(loadContext->mCacheStatus == WorkerLoadContext::ReadingFromCache);
  loadContext->mCacheStatus = WorkerLoadContext::Cached;

  MOZ_ASSERT(mPrincipalInfo);

  nsresult rv = DataReceivedFromCache(
      aString, aStringLen, mChannelInfo, std::move(mPrincipalInfo),
      mCSPHeaderValue, mCSPReportOnlyHeaderValue, mReferrerPolicyHeaderValue);
  return mRequestHandle->OnStreamComplete(rv);
}

nsresult CacheLoadHandler::DataReceivedFromCache(
    const uint8_t* aString, uint32_t aStringLen,
    const mozilla::dom::ChannelInfo& aChannelInfo,
    UniquePtr<PrincipalInfo> aPrincipalInfo, const nsACString& aCSPHeaderValue,
    const nsACString& aCSPReportOnlyHeaderValue,
    const nsACString& aReferrerPolicyHeaderValue) {
  AssertIsOnMainThread();
  if (mRequestHandle->IsEmpty()) {
    return NS_OK;
  }
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  MOZ_ASSERT(loadContext->mCacheStatus == WorkerLoadContext::Cached);
  MOZ_ASSERT(loadContext->mRequest);

  auto responsePrincipalOrErr = PrincipalInfoToPrincipal(*aPrincipalInfo);
  MOZ_DIAGNOSTIC_ASSERT(responsePrincipalOrErr.isOk());

  nsIPrincipal* principal = mWorkerRef->Private()->GetPrincipal();
  if (!principal) {
    WorkerPrivate* parentWorker = mWorkerRef->Private()->GetParent();
    MOZ_ASSERT(parentWorker, "Must have a parent!");
    principal = parentWorker->GetPrincipal();
  }

  nsCOMPtr<nsIPrincipal> responsePrincipal = responsePrincipalOrErr.unwrap();

  loadContext->mMutedErrorFlag.emplace(!principal->Subsumes(responsePrincipal));

  // May be null.
  Document* parentDoc = mWorkerRef->Private()->GetDocument();

  // Use the regular ScriptDecoder Decoder for this grunt work! Should be just
  // fine because we're running on the main thread.
  nsresult rv;

  // Set the Source type to "text" for decoding.
  loadContext->mRequest->SetTextSource(loadContext);

  rv = mDecoder->DecodeRawData(loadContext->mRequest, aString, aStringLen,
                               /* aEndOfStream = */ true);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!loadContext->mRequest->ScriptTextLength()) {
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                    parentDoc, nsContentUtils::eDOM_PROPERTIES,
                                    "EmptyWorkerSourceWarning");
  }

  nsCOMPtr<nsIURI> finalURI;
  rv = NS_NewURI(getter_AddRefs(finalURI), loadContext->mFullURL);
  if (!loadContext->mRequest->mBaseURL) {
    loadContext->mRequest->mBaseURL = finalURI;
  }
  if (loadContext->IsTopLevel()) {
    if (NS_SUCCEEDED(rv)) {
      mWorkerRef->Private()->SetBaseURI(finalURI);
    }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    nsIPrincipal* principal = mWorkerRef->Private()->GetPrincipal();
    MOZ_DIAGNOSTIC_ASSERT(principal);

    bool equal = false;
    MOZ_ALWAYS_SUCCEEDS(responsePrincipal->Equals(principal, &equal));
    MOZ_DIAGNOSTIC_ASSERT(equal);

    nsCOMPtr<nsIContentSecurityPolicy> csp;
    if (parentDoc) {
      csp = parentDoc->GetCsp();
    }
    MOZ_DIAGNOSTIC_ASSERT(!csp);
#endif

    mWorkerRef->Private()->InitChannelInfo(aChannelInfo);

    nsILoadGroup* loadGroup = mWorkerRef->Private()->GetLoadGroup();
    MOZ_DIAGNOSTIC_ASSERT(loadGroup);

    // Override the principal on the WorkerPrivate.  This is only necessary
    // in order to get a principal with exactly the correct URL.  The fetch
    // referrer logic depends on the WorkerPrivate principal having a URL
    // that matches the worker script URL.  If bug 1340694 is ever fixed
    // this can be removed.
    // XXX: force the partitionedPrincipal to be equal to the response one.
    // This is OK for now because we don't want to expose partitionedPrincipal
    // functionality in ServiceWorkers yet.
    rv = mWorkerRef->Private()->SetPrincipalsAndCSPOnMainThread(
        responsePrincipal, responsePrincipal, loadGroup, nullptr);
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));

    rv = mWorkerRef->Private()->SetCSPFromHeaderValues(
        aCSPHeaderValue, aCSPReportOnlyHeaderValue);
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));

    mWorkerRef->Private()->UpdateReferrerInfoFromHeader(
        aReferrerPolicyHeaderValue);
  }

  if (NS_SUCCEEDED(rv)) {
    return DataReceived();
  }

  return rv;
}

nsresult CacheLoadHandler::DataReceived() {
  MOZ_ASSERT(!mRequestHandle->IsEmpty());
  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  if (loadContext->IsTopLevel()) {
    WorkerPrivate* parent = mWorkerRef->Private()->GetParent();

    if (parent) {
      // XHR Params Allowed
      mWorkerRef->Private()->SetXHRParamsAllowed(parent->XHRParamsAllowed());

      // Set ContentSecurityPolicy
      nsresult rv = mWorkerRef->Private()->SetCsp(parent->GetCsp());
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

}  // namespace workerinternals::loader

}  // namespace dom
}  // namespace mozilla
