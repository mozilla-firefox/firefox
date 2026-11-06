/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_CacheLoadHandler_h_
#define mozilla_dom_workers_CacheLoadHandler_h_

#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/CacheBinding.h"
#include "mozilla/dom/ChannelInfo.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/ScriptLoadHandler.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/cache/Cache.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/dom/workerinternals/ScriptLoader.h"
#include "nsIContentPolicy.h"
#include "nsIInputStreamPump.h"
#include "nsIStreamLoader.h"
#include "nsStreamUtils.h"
#include "nsStringFwd.h"

using mozilla::dom::cache::Cache;
using mozilla::dom::cache::CacheStorage;
using mozilla::ipc::PrincipalInfo;

namespace mozilla::dom {

class WorkerLoadContext;

namespace workerinternals::loader {

/*
 * [DOMDOC] CacheLoadHandler for Workers
 *
 * A LoadHandler is a ScriptLoader helper class that reacts to an
 * nsIStreamLoader's events for loading JS scripts. It is primarily responsible
 * for decoding the stream into UTF8 or UTF16. Additionally, it takes care of
 * any work that needs to follow the completion of a stream. Every LoadHandler
 * also manages additional tasks for the type of load that it is doing.
 *
 * CacheLoadHandler is a specialized LoadHandler used by ServiceWorkers to
 * implement the installation model used by ServiceWorkers to support running
 * offline. When a ServiceWorker is installed, its main script is evaluated and
 * all script resources that are loaded are saved. The spec does not specify the
 * storage mechanism for this, but we chose to reuse the Cache API[1] mechanism
 * that we expose to content to also store the script and its dependencies. We
 * store the script resources in a special chrome namespace CacheStorage that is
 * not visible to content. Each distinct ServiceWorker installation gets its own
 * Cache keyed by a randomly-generated UUID.
 *
 * In terms of specification, this class implements step 4 of
 * https://w3c.github.io/ServiceWorker/#importscripts
 *
 * Relationship to NetworkLoadHandler
 *
 * During ServiceWorker installation, the CacheLoadHandler falls back on the
 * NetworkLoadHandler by calling `mLoader->LoadScript(...)`. If a script has not
 * been seen before, then we will fall back on loading from the network.
 * However, if the ServiceWorker is already installed, an error will be
 * generated and the ServiceWorker will fail to load, per spec.
 *
 * CacheLoadHandler does not persist some pieces of information, such as the
 * sourceMapUrl. Also, the DOM Cache API storage does not yet support alternate
 * data streams for JS Bytecode or WASM caching; this is tracked by Bug 1336199.
 *
 * [1]: https://developer.mozilla.org/en-US/docs/Web/API/caches
 *
 */

class CacheLoadHandler final : public PromiseNativeHandler,
                               public nsIStreamLoaderObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISTREAMLOADEROBSERVER

  CacheLoadHandler(ThreadSafeWorkerRef* aWorkerRef,
                   ThreadSafeRequestHandle* aRequestHandle,
                   bool aIsWorkerScript,
                   bool aOnlyExistingCachedResourcesAllowed,
                   WorkerScriptLoader* aLoader);

  void Fail(nsresult aRv);

  void Load(Cache* aCache);

  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

 private:
  ~CacheLoadHandler() { AssertIsOnMainThread(); }

  nsresult DataReceivedFromCache(
      const uint8_t* aString, uint32_t aStringLen,
      const mozilla::dom::ChannelInfo& aChannelInfo,
      UniquePtr<PrincipalInfo> aPrincipalInfo,
      const PolicyContainer::Headers& aPolicyContainerHeaders,
      const nsACString& aReferrerPolicyHeaderValue,
      const nsACString& aReportingEndpointsHeaderValue);
  nsresult DataReceived();

  RefPtr<ThreadSafeRequestHandle> mRequestHandle;
  const RefPtr<WorkerScriptLoader> mLoader;
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;
  const bool mIsWorkerScript;
  bool mFailed;
  bool mOnlyExistingCachedResourcesAllowed;
  nsCOMPtr<nsIInputStreamPump> mPump;
  nsCOMPtr<nsIURI> mBaseURI;
  mozilla::dom::ChannelInfo mChannelInfo;
  UniquePtr<PrincipalInfo> mPrincipalInfo;
  UniquePtr<ScriptDecoder> mDecoder;
  PolicyContainer::Headers mPolicyContainerHeaders;
  nsCString mReferrerPolicyHeaderValue;
  nsCString mReportingEndpointsHeaderValue;
  nsCOMPtr<nsISerialEventTarget> mMainThreadEventTarget;
};

/*
 * CacheCreator
 *
 * The CacheCreator is responsible for maintaining a CacheStorage for the
 * purposes of caching ServiceWorkers (see comment on CacheLoadHandler). In
 * addition, it tracks all CacheLoadHandlers and is used for cleanup once
 * loading has finished.
 *
 */

class CacheCreator final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit CacheCreator(WorkerPrivate* aWorkerPrivate);

  void AddLoader(MovingNotNull<RefPtr<CacheLoadHandler>> aLoader) {
    AssertIsOnMainThread();
    MOZ_ASSERT(!mCacheStorage);
    mLoaders.AppendElement(std::move(aLoader));
  }

  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  // Try to load from cache with aPrincipal used for cache access.
  nsresult Load(nsIPrincipal* aPrincipal);

  Cache* Cache_() const {
    AssertIsOnMainThread();
    MOZ_ASSERT(mCache);
    return mCache;
  }

  nsIGlobalObject* Global() const {
    AssertIsOnMainThread();
    MOZ_ASSERT(mSandboxGlobalObject);
    return mSandboxGlobalObject;
  }

  void DeleteCache(nsresult aReason);

 private:
  ~CacheCreator() = default;

  nsresult CreateCacheStorage(nsIPrincipal* aPrincipal);

  void FailLoaders(nsresult aRv);

  RefPtr<Cache> mCache;
  RefPtr<CacheStorage> mCacheStorage;
  nsCOMPtr<nsIGlobalObject> mSandboxGlobalObject;
  nsTArray<NotNull<RefPtr<CacheLoadHandler>>> mLoaders;

  nsString mCacheName;
  OriginAttributes mOriginAttributes;
};

/*
 * CachePromiseHandler
 *
 * This promise handler is used to track if a ServiceWorker has been written to
 * Cache. It is responsible for tracking the state of the ServiceWorker being
 * cached. It also handles cancelling caching of a ServiceWorker if loading is
 * interrupted. It is initialized by the NetworkLoadHandler as part of the first
 * load of a ServiceWorker.
 *
 */
class CachePromiseHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  CachePromiseHandler(WorkerScriptLoader* aLoader,
                      ThreadSafeRequestHandle* aRequestHandle);

  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

 private:
  ~CachePromiseHandler() { AssertIsOnMainThread(); }

  RefPtr<WorkerScriptLoader> mLoader;
  RefPtr<ThreadSafeRequestHandle> mRequestHandle;
};

}  // namespace workerinternals::loader
}  // namespace mozilla::dom

#endif /* mozilla_dom_workers_CacheLoadHandler_h_ */
