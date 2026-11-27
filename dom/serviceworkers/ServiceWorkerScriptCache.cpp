/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerScriptCache.h"

#include "ServiceWorkerManager.h"
#include "js/Array.h"               // JS::GetArrayLength
#include "js/PropertyAndElement.h"  // JS_GetElement
#include "js/Utility.h"             // JS::FreePolicy
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CacheBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseWorkerProxy.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/cache/Cache.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsContentUtils.h"
#include "nsICacheInfoChannel.h"
#include "nsIHttpChannel.h"
#include "nsIInputStreamPump.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsIStreamLoader.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIUUIDGenerator.h"
#include "nsIXPConnect.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsStringStream.h"

using mozilla::dom::cache::Cache;
using mozilla::dom::cache::CacheStorage;
using mozilla::ipc::PrincipalInfo;

namespace mozilla::dom::serviceWorkerScriptCache {

namespace {

already_AddRefed<CacheStorage> CreateCacheStorage(JSContext* aCx,
                                                  nsIPrincipal* aPrincipal,
                                                  ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);

  nsIXPConnect* xpc = nsContentUtils::XPConnect();
  MOZ_ASSERT(xpc, "This should never be null!");
  JS::Rooted<JSObject*> sandbox(aCx);
  aRv = xpc->CreateSandbox(aCx, aPrincipal, sandbox.address());
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // This is called when the JSContext is not in a realm, so CreateSandbox
  // returned an unwrapped global.
  MOZ_ASSERT(JS_IsGlobalObject(sandbox));

  nsCOMPtr<nsIGlobalObject> sandboxGlobalObject = xpc::NativeGlobal(sandbox);
  if (!sandboxGlobalObject) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // We assume private browsing is not enabled here.  The ScriptLoader
  // explicitly fails for private browsing so there should never be
  // a service worker running in private browsing mode.  Therefore if
  // we are purging scripts or running a comparison algorithm we cannot
  // be in private browsing.
  //
  // Also, bypass the CacheStorage trusted origin checks.  The ServiceWorker
  // has validated the origin prior to this point.  All the information
  // to revalidate is not available now.
  return CacheStorage::CreateOnMainThread(cache::CHROME_ONLY_NAMESPACE,
                                          sandboxGlobalObject, aPrincipal,
                                          true /* force trusted origin */, aRv);
}

class CompareManager;
class CompareCache;

// This class downloads a URL from the network, compare the downloaded script
// with an existing cache if provided, and report to CompareManager via calling
// ComparisonFinished().
class CompareNetwork final : public nsIStreamLoaderObserver,
                             public nsIRequestObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISTREAMLOADEROBSERVER
  NS_DECL_NSIREQUESTOBSERVER

  CompareNetwork(CompareManager* aManager,
                 ServiceWorkerRegistrationInfo* aRegistration,
                 bool aIsMainScript)
      : mManager(aManager),
        mRegistration(aRegistration),
        mInternalHeaders(new InternalHeaders()),
        mLoadFlags(nsIChannel::LOAD_BYPASS_SERVICE_WORKER),
        mState(WaitingForInitialization),
        mNetworkResult(NS_OK),
        mCacheResult(NS_OK),
        mIsMainScript(aIsMainScript),
        mIsFromCache(false) {
    MOZ_ASSERT(aManager);
    MOZ_ASSERT(NS_IsMainThread());
  }

  nsresult Initialize(nsIPrincipal* aPrincipal, const nsACString& aURL,
                      Cache* const aCache);

  void Abort();

  void NetworkFinish(nsresult aRv);

  void CacheFinish(nsresult aRv);

  const nsCString& URL() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mURL;
  }

  const nsString& Buffer() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mBuffer;
  }

  const ChannelInfo& GetChannelInfo() const { return mChannelInfo; }

  already_AddRefed<InternalHeaders> GetInternalHeaders() const {
    RefPtr<InternalHeaders> internalHeaders = mInternalHeaders;
    return internalHeaders.forget();
  }

  UniquePtr<PrincipalInfo> TakePrincipalInfo() {
    return std::move(mPrincipalInfo);
  }

  bool Succeeded() const { return NS_SUCCEEDED(mNetworkResult); }

  const nsTArray<NotNull<RefPtr<nsIURI>>>& URLList() const { return mURLList; }

 private:
  ~CompareNetwork() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!mCC);
  }

  void Finish();

  nsresult SetPrincipalInfo(nsIChannel* aChannel);

  RefPtr<CompareManager> mManager;
  RefPtr<CompareCache> mCC;
  RefPtr<ServiceWorkerRegistrationInfo> mRegistration;

  nsCOMPtr<nsIChannel> mChannel;
  nsString mBuffer;
  nsCString mURL;
  ChannelInfo mChannelInfo;
  RefPtr<InternalHeaders> mInternalHeaders;
  UniquePtr<PrincipalInfo> mPrincipalInfo;
  nsTArray<NotNull<RefPtr<nsIURI>>> mURLList;

  nsCString mMaxScope;
  nsLoadFlags mLoadFlags;

  enum {
    WaitingForInitialization,
    WaitingForBothFinished,
    WaitingForNetworkFinished,
    WaitingForCacheFinished,
    Finished
  } mState;

  nsresult mNetworkResult;
  nsresult mCacheResult;

  const bool mIsMainScript;
  bool mIsFromCache;
};

NS_IMPL_ISUPPORTS(CompareNetwork, nsIStreamLoaderObserver, nsIRequestObserver)

// This class gets a cached Response from the CacheStorage and then it calls
// CacheFinish() in the CompareNetwork.
class CompareCache final : public PromiseNativeHandler,
                           public nsIStreamLoaderObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISTREAMLOADEROBSERVER

  explicit CompareCache(CompareNetwork* aCN)
      : mCN(aCN), mState(WaitingForInitialization), mInCache(false) {
    MOZ_ASSERT(aCN);
    MOZ_ASSERT(NS_IsMainThread());
  }

  nsresult Initialize(Cache* const aCache, const nsACString& aURL);

  void Finish(nsresult aStatus, bool aInCache);

  void Abort();

  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override;

  const nsString& Buffer() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mBuffer;
  }

  bool InCache() { return mInCache; }

 private:
  ~CompareCache() { MOZ_ASSERT(NS_IsMainThread()); }

  void ManageValueResult(JSContext* aCx, JS::Handle<JS::Value> aValue);

  RefPtr<CompareNetwork> mCN;
  nsCOMPtr<nsIInputStreamPump> mPump;

  nsCString mURL;
  nsString mBuffer;

  enum {
    WaitingForInitialization,
    WaitingForScript,
    Finished,
  } mState;

  bool mInCache;
};

NS_IMPL_ISUPPORTS(CompareCache, nsIStreamLoaderObserver)

class CompareManager final : public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  explicit CompareManager(ServiceWorkerRegistrationInfo* aRegistration,
                          CompareCallback* aCallback)
      : mRegistration(aRegistration),
        mCallback(aCallback),
        mLoadFlags(nsIChannel::LOAD_BYPASS_SERVICE_WORKER),
        mState(WaitingForInitialization),
        mPendingCount(0),
        mOnFailure(OnFailure::DoNothing),
        mAreScriptsEqual(true) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aRegistration);
  }

  nsresult Initialize(nsIPrincipal* aPrincipal, const nsACString& aURL,
                      const nsAString& aCacheName);

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override;

  CacheStorage* CacheStorage_() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCacheStorage);
    return mCacheStorage;
  }

  void ComparisonFinished(nsresult aStatus, bool aIsMainScript, bool aIsEqual,
                          const nsACString& aMaxScope, nsLoadFlags aLoadFlags) {
    MOZ_ASSERT(NS_IsMainThread());
    if (mState == Finished) {
      return;
    }

    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForScriptOrComparisonResult);

    if (NS_WARN_IF(NS_FAILED(aStatus))) {
      Fail(aStatus);
      return;
    }

    mAreScriptsEqual = mAreScriptsEqual && aIsEqual;

    if (aIsMainScript) {
      mMaxScope = aMaxScope;
      mLoadFlags = aLoadFlags;
    }

    // Check whether all CompareNetworks finished their jobs.
    MOZ_DIAGNOSTIC_ASSERT(mPendingCount > 0);
    if (--mPendingCount) {
      return;
    }

    if (mAreScriptsEqual) {
      MOZ_ASSERT(mCallback);
      mCallback->ComparisonResult(aStatus, true /* aSameScripts */, mOnFailure,
                                  u""_ns, mMaxScope, mLoadFlags);
      Cleanup();
      return;
    }

    // Write to Cache so ScriptLoader reads succeed.
    WriteNetworkBufferToNewCache();
  }

 private:
  ~CompareManager() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCNList.Length() == 0);
  }

  void Fail(nsresult aStatus);

  void Cleanup();

  nsresult FetchScript(const nsACString& aURL, bool aIsMainScript,
                       Cache* const aCache = nullptr) {
    MOZ_ASSERT(NS_IsMainThread());

    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForInitialization ||
                          mState == WaitingForScriptOrComparisonResult);

    RefPtr<CompareNetwork> cn =
        new CompareNetwork(this, mRegistration, aIsMainScript);
    mCNList.AppendElement(cn);
    mPendingCount += 1;

    nsresult rv = cn->Initialize(mPrincipal, aURL, aCache);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    return NS_OK;
  }

  void ManageOldCache(JSContext* aCx, JS::Handle<JS::Value> aValue) {
    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForExistingOpen);

    // RAII Cleanup when fails.
    nsresult rv = NS_ERROR_FAILURE;
    auto guard = MakeScopeExit([&] { Fail(rv); });

    if (NS_WARN_IF(!aValue.isObject())) {
      return;
    }

    MOZ_ASSERT(!mOldCache);
    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
    if (NS_WARN_IF(!obj) ||
        NS_WARN_IF(NS_FAILED(UNWRAP_OBJECT(Cache, obj, mOldCache)))) {
      return;
    }

    Optional<RequestOrUTF8String> request;
    CacheQueryOptions options;
    ErrorResult error;
    RefPtr<Promise> promise = mOldCache->Keys(aCx, request, options, error);
    if (NS_WARN_IF(error.Failed())) {
      // No exception here because there are no ReadableStreams involved here.
      MOZ_ASSERT(!error.IsJSException());
      rv = error.StealNSResult();
      return;
    }

    mState = WaitingForExistingKeys;
    promise->AppendNativeHandler(this);
    guard.release();
  }

  void ManageOldKeys(JSContext* aCx, JS::Handle<JS::Value> aValue) {
    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForExistingKeys);

    // RAII Cleanup when fails.
    nsresult rv = NS_ERROR_FAILURE;
    auto guard = MakeScopeExit([&] { Fail(rv); });

    if (NS_WARN_IF(!aValue.isObject())) {
      return;
    }

    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
    if (NS_WARN_IF(!obj)) {
      return;
    }

    uint32_t len = 0;
    if (!JS::GetArrayLength(aCx, obj, &len)) {
      return;
    }

    // Fetch and compare the source scripts.
    MOZ_ASSERT(mPendingCount == 0);

    mState = WaitingForScriptOrComparisonResult;

    bool hasMainScript = false;
    AutoTArray<nsCString, 8> urlList;

    // Extract the list of URLs in the old cache.
    for (uint32_t i = 0; i < len; ++i) {
      JS::Rooted<JS::Value> val(aCx);
      if (NS_WARN_IF(!JS_GetElement(aCx, obj, i, &val)) ||
          NS_WARN_IF(!val.isObject())) {
        return;
      }

      Request* request;
      JS::Rooted<JSObject*> requestObj(aCx, &val.toObject());
      if (NS_WARN_IF(NS_FAILED(UNWRAP_OBJECT(Request, &requestObj, request)))) {
        return;
      };

      nsCString url;
      request->GetUrl(url);

      if (!hasMainScript && url == mURL) {
        hasMainScript = true;
      }

      urlList.AppendElement(std::move(url));
    }

    // If the main script is missing, then something has gone wrong.  We
    // will try to continue with the update process to trigger a new
    // installation.  If that fails, however, then uninstall the registration
    // because it is broken in a way that cannot be fixed.
    if (!hasMainScript) {
      mOnFailure = OnFailure::Uninstall;
    }

    // Always make sure to fetch the main script.  If the old cache has
    // no entries or the main script entry is missing, then the loop below
    // may not trigger it.  This should not really happen, but we handle it
    // gracefully if it does occur.  Its possible the bad cache state is due
    // to a crash or shutdown during an update, etc.
    rv = FetchScript(mURL, true /* aIsMainScript */, mOldCache);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    for (const auto& url : urlList) {
      // We explicitly start the fetch for the main script above.
      if (mURL == url) {
        continue;
      }

      rv = FetchScript(url, false /* aIsMainScript */, mOldCache);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }
    }

    guard.release();
  }

  void ManageNewCache(JSContext* aCx, JS::Handle<JS::Value> aValue) {
    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForOpen);

    // RAII Cleanup when fails.
    nsresult rv = NS_ERROR_FAILURE;
    auto guard = MakeScopeExit([&] { Fail(rv); });

    if (NS_WARN_IF(!aValue.isObject())) {
      return;
    }

    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
    if (NS_WARN_IF(!obj)) {
      return;
    }

    Cache* cache = nullptr;
    rv = UNWRAP_OBJECT(Cache, &obj, cache);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    // Just to be safe.
    RefPtr<Cache> kungfuDeathGrip = cache;

    MOZ_ASSERT(mPendingCount == 0);
    for (uint32_t i = 0; i < mCNList.Length(); ++i) {
      // We bail out immediately when something goes wrong.
      rv = WriteToCache(aCx, cache, mCNList[i]);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }
    }

    mState = WaitingForPut;
    guard.release();
  }

  void WriteNetworkBufferToNewCache() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCNList.Length() != 0);
    MOZ_ASSERT(mCacheStorage);
    MOZ_ASSERT(mNewCacheName.IsEmpty());

    ErrorResult result;
    result = serviceWorkerScriptCache::GenerateCacheName(mNewCacheName);
    if (NS_WARN_IF(result.Failed())) {
      MOZ_ASSERT(!result.IsErrorWithMessage());
      Fail(result.StealNSResult());
      return;
    }

    RefPtr<Promise> cacheOpenPromise =
        mCacheStorage->Open(mNewCacheName, result);
    if (NS_WARN_IF(result.Failed())) {
      MOZ_ASSERT(!result.IsErrorWithMessage());
      Fail(result.StealNSResult());
      return;
    }

    mState = WaitingForOpen;
    cacheOpenPromise->AppendNativeHandler(this);
  }

  nsresult WriteToCache(JSContext* aCx, Cache* aCache, CompareNetwork* aCN) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aCache);
    MOZ_ASSERT(aCN);
    MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForOpen);

    // We don't have to save any information from a failed CompareNetwork.
    if (!aCN->Succeeded()) {
      return NS_OK;
    }

    // Scripts can be several megabytes, so the UTF-16 to UTF-8 conversion is
    // done fallibly here.  Failing the update is much better than aborting the
    // parent process with an OOM crash.
    nsCString converted;
    if (NS_WARN_IF(!CopyUTF16toUTF8(aCN->Buffer(), converted, fallible))) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Note that this is the length of the UTF-8 body we just produced, not the
    // length of the UTF-16 buffer it was converted from.
    const int64_t bodyLength = converted.Length();

    nsCOMPtr<nsIInputStream> body;
    nsresult rv =
        NS_NewCStringInputStream(getter_AddRefs(body), std::move(converted));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    SafeRefPtr<InternalResponse> ir =
        MakeSafeRefPtr<InternalResponse>(200, "OK"_ns);
    ir->SetBody(body, bodyLength);
    ir->SetURLList(aCN->URLList());

    ir->InitChannelInfo(aCN->GetChannelInfo());
    UniquePtr<PrincipalInfo> principalInfo = aCN->TakePrincipalInfo();
    if (principalInfo) {
      ir->SetPrincipalInfo(std::move(principalInfo));
    }

    RefPtr<InternalHeaders> internalHeaders = aCN->GetInternalHeaders();
    ir->Headers()->Fill(*(internalHeaders.get()), IgnoreErrors());

    RefPtr<Response> response =
        new Response(aCache->GetGlobalObject(), std::move(ir), nullptr);

    RequestOrUTF8String request;
    request.SetAsUTF8String() = aCN->URL();

    // For now we have to wait until the Put Promise is fulfilled before we can
    // continue since Cache does not yet support starting a read that is being
    // written to.
    ErrorResult result;
    RefPtr<Promise> cachePromise = aCache->Put(aCx, request, *response, result);
    result.WouldReportJSException();
    if (NS_WARN_IF(result.Failed())) {
      // No exception here because there are no ReadableStreams involved here.
      MOZ_ASSERT(!result.IsJSException());
      MOZ_ASSERT(!result.IsErrorWithMessage());
      return result.StealNSResult();
    }

    mPendingCount += 1;
    cachePromise->AppendNativeHandler(this);
    return NS_OK;
  }

  RefPtr<ServiceWorkerRegistrationInfo> mRegistration;
  RefPtr<CompareCallback> mCallback;
  RefPtr<CacheStorage> mCacheStorage;

  nsTArray<RefPtr<CompareNetwork>> mCNList;

  nsCString mURL;
  RefPtr<nsIPrincipal> mPrincipal;

  // Used for the old cache where saves the old source scripts.
  RefPtr<Cache> mOldCache;

  // Only used if the network script has changed and needs to be cached.
  nsString mNewCacheName;

  nsCString mMaxScope;
  nsLoadFlags mLoadFlags;

  enum {
    WaitingForInitialization,
    WaitingForExistingOpen,
    WaitingForExistingKeys,
    WaitingForScriptOrComparisonResult,
    WaitingForOpen,
    WaitingForPut,
    Finished
  } mState;

  uint32_t mPendingCount;
  OnFailure mOnFailure;
  bool mAreScriptsEqual;
};

NS_IMPL_ISUPPORTS0(CompareManager)

nsresult CompareNetwork::Initialize(nsIPrincipal* aPrincipal,
                                    const nsACString& aURL,
                                    Cache* const aCache) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mURL = aURL;
  mURLList.AppendElement(WrapNotNull(uri.get()));

  nsCOMPtr<nsILoadGroup> loadGroup;
  rv = NS_NewLoadGroup(getter_AddRefs(loadGroup), aPrincipal);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Update LoadFlags for propagating to ServiceWorkerInfo.
  mLoadFlags = nsIChannel::LOAD_BYPASS_SERVICE_WORKER;

  ServiceWorkerUpdateViaCache uvc = mRegistration->GetUpdateViaCache();
  if (uvc == ServiceWorkerUpdateViaCache::None ||
      (uvc == ServiceWorkerUpdateViaCache::Imports && mIsMainScript)) {
    mLoadFlags |= nsIRequest::VALIDATE_ALWAYS;
  }

  if (mRegistration->IsLastUpdateCheckTimeOverOneDay()) {
    mLoadFlags |= nsIRequest::LOAD_BYPASS_CACHE;
  }

  // Different settings are needed for fetching imported scripts, since they
  // might be cross-origin scripts.
  uint32_t secFlags =
      mIsMainScript ? nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED
                    : nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;

  nsContentPolicyType contentPolicyType =
      mIsMainScript ? nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER
                    : nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS;

  // Create a new cookieJarSettings.
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      mozilla::net::CookieJarSettings::Create(aPrincipal);

  // Populate the partitionKey by using the given prinicpal. The ServiceWorkers
  // are using the foreign partitioned principal, so we can get the partitionKey
  // from it and the partitionKey will only exist if it's in the third-party
  // context. In first-party context, we can still use the uri to set the
  // partitionKey.
  if (!aPrincipal->OriginAttributesRef().mPartitionKey.IsEmpty()) {
    net::CookieJarSettings::Cast(cookieJarSettings)
        ->SetPartitionKey(aPrincipal->OriginAttributesRef().mPartitionKey);
  } else {
    net::CookieJarSettings::Cast(cookieJarSettings)->SetPartitionKey(uri);
  }

  // Note that because there is no "serviceworker" RequestContext type, we can
  // use the TYPE_INTERNAL_SCRIPT content policy types when loading a service
  // worker.
  rv = NS_NewChannel(getter_AddRefs(mChannel), uri, aPrincipal, secFlags,
                     contentPolicyType, cookieJarSettings,
                     nullptr /* aPerformanceStorage */, loadGroup,
                     nullptr /* aCallbacks */, mLoadFlags);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Set the IsInThirdPartyContext for the channel's loadInfo according to the
  // partitionKey of the principal. The worker is foreign if it's using
  // partitioned principal, i.e. the partitionKey is not empty. In this case,
  // we need to set the bit to the channel's loadInfo.
  if (!aPrincipal->OriginAttributesRef().mPartitionKey.IsEmpty()) {
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
    rv = loadInfo->SetIsInThirdPartyContext(true);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (httpChannel) {
    // Spec says no redirects allowed for top-level SW scripts.
    if (mIsMainScript) {
      rv = httpChannel->SetRedirectionLimit(0);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }

    rv = httpChannel->SetRequestHeader("Service-Worker"_ns, "script"_ns,
                                       /* merge */ false);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  nsCOMPtr<nsIStreamLoader> loader;
  rv = NS_NewStreamLoader(getter_AddRefs(loader), this, this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = mChannel->AsyncOpen(loader);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // If we do have an existing cache to compare with.
  if (aCache) {
    mCC = new CompareCache(this);
    rv = mCC->Initialize(aCache, aURL);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Abort();
      return rv;
    }

    mState = WaitingForBothFinished;
    return NS_OK;
  }

  mState = WaitingForNetworkFinished;
  return NS_OK;
}

void CompareNetwork::Finish() {
  if (mState == Finished) {
    return;
  }

  bool same = true;
  nsresult rv = NS_OK;

  // mNetworkResult is prior to mCacheResult, since it's needed for reporting
  // various errors to web content.
  if (NS_FAILED(mNetworkResult)) {
    // An imported script could become offline, since it might no longer be
    // needed by the new importing script. In that case, the importing script
    // must be different, and thus, it's okay to report same script found here.
    rv = mIsMainScript ? mNetworkResult : NS_OK;
    same = true;
  } else if (mCC && NS_FAILED(mCacheResult)) {
    rv = mCacheResult;
  } else {  // Both passed.
    same = mCC && mCC->InCache() && mCC->Buffer().Equals(mBuffer);
  }

  mManager->ComparisonFinished(rv, mIsMainScript, same, mMaxScope, mLoadFlags);

  // We have done with the CompareCache.
  mCC = nullptr;
}

void CompareNetwork::NetworkFinish(nsresult aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForBothFinished ||
                        mState == WaitingForNetworkFinished);

  mNetworkResult = aRv;

  if (mState == WaitingForBothFinished) {
    mState = WaitingForCacheFinished;
    return;
  }

  if (mState == WaitingForNetworkFinished) {
    Finish();
    return;
  }
}

void CompareNetwork::CacheFinish(nsresult aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForBothFinished ||
                        mState == WaitingForCacheFinished);

  mCacheResult = aRv;

  if (mState == WaitingForBothFinished) {
    mState = WaitingForNetworkFinished;
    return;
  }

  if (mState == WaitingForCacheFinished) {
    Finish();
    return;
  }
}

void CompareNetwork::Abort() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState != Finished) {
    mState = Finished;

    MOZ_ASSERT(mChannel);
    mChannel->CancelWithReason(NS_BINDING_ABORTED, "CompareNetwork::Abort"_ns);
    mChannel = nullptr;

    if (mCC) {
      mCC->Abort();
      mCC = nullptr;
    }
  }
}

NS_IMETHODIMP
CompareNetwork::OnStartRequest(nsIRequest* aRequest) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState == Finished) {
    return NS_OK;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  MOZ_ASSERT_IF(mIsMainScript, channel == mChannel);
  mChannel = channel;

  MOZ_ASSERT(!mChannelInfo.IsInitialized());
  mChannelInfo.InitFromChannel(mChannel);

  nsresult rv = SetPrincipalInfo(mChannel);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mInternalHeaders->FillResponseHeaders(mChannel);

  nsCOMPtr<nsICacheInfoChannel> cacheChannel(do_QueryInterface(channel));
  if (cacheChannel) {
    cacheChannel->IsFromCache(&mIsFromCache);
  }

  return NS_OK;
}

nsresult CompareNetwork::SetPrincipalInfo(nsIChannel* aChannel) {
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  if (!ssm) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> channelPrincipal;
  nsresult rv = ssm->GetChannelResultPrincipal(
      aChannel, getter_AddRefs(channelPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  auto principalInfo = std::make_unique<PrincipalInfo>();
  rv = PrincipalToPrincipalInfo(channelPrincipal, principalInfo.get());

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mPrincipalInfo = std::move(principalInfo);
  return NS_OK;
}

NS_IMETHODIMP
CompareNetwork::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  // Nothing to do here!
  return NS_OK;
}

NS_IMETHODIMP
CompareNetwork::OnStreamComplete(nsIStreamLoader* aLoader,
                                 nsISupports* aContext, nsresult aStatus,
                                 uint32_t aLen, const uint8_t* aString) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState == Finished) {
    return NS_OK;
  }

  nsresult rv = NS_ERROR_FAILURE;
  auto guard = MakeScopeExit([&] { NetworkFinish(rv); });

  if (aLen > GetWorkerScriptMaxSizeInBytes()) {
    rv = NS_ERROR_DOM_ABORT_ERR;  // This will make sure an exception gets
                                  // thrown to the global.
    return NS_OK;
  }

  if (NS_WARN_IF(NS_FAILED(aStatus))) {
    rv = (aStatus == NS_ERROR_REDIRECT_LOOP) ? NS_ERROR_DOM_SECURITY_ERR
                                             : aStatus;
    return NS_OK;
  }

  nsCOMPtr<nsIRequest> request;
  rv = aLoader->GetRequest(getter_AddRefs(request));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_OK;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  MOZ_ASSERT(channel, "How come we don't have any channel?");

  nsCOMPtr<nsIURI> uri;
  channel->GetOriginalURI(getter_AddRefs(uri));
  bool isExtension = uri->SchemeIs("moz-extension");

  if (isExtension &&
      !StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup()) {
    // Return earlier with error is the worker script is a moz-extension url
    // but the feature isn't enabled by prefs.
    return NS_ERROR_FAILURE;
  }

  if (isExtension) {
    // NOTE: trying to register any moz-extension use that doesn't ends
    // with .js//.mjs seems to be already completing with an error
    // in aStatus and they never reach this point.

    // TODO: look into avoid duplicated parts that could be shared with the HTTP
    // channel scenario.
    nsCOMPtr<nsIURI> channelURL;
    rv = channel->GetURI(getter_AddRefs(channelURL));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Append the final URL (which for an extension worker script is going to
    // be a file or jar url).
    MOZ_DIAGNOSTIC_ASSERT(!mURLList.IsEmpty());

    bool equals = false;
    if (NS_FAILED(channelURL->Equals(mURLList[0], &equals)) || !equals) {
      mURLList.AppendElement(WrapNotNull(channelURL.get()));
    }

    UniquePtr<char16_t[], JS::FreePolicy> buffer;
    size_t len = 0;

    rv = ScriptLoader::ConvertToUTF16(channel, aString, aLen, u"UTF-8"_ns,
                                      nullptr, buffer, len);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mBuffer.Adopt(buffer.release(), len);

    rv = NS_OK;
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(request);

  // Main scripts cannot be redirected successfully, however extensions
  // may successfuly redirect imported scripts to a moz-extension url
  // (if listed in the web_accessible_resources manifest property).
  //
  // When the service worker is initially registered the imported scripts
  // will be loaded from the child process (see dom/workers/ScriptLoader.cpp)
  // and in that case this method will only be called for the main script.
  //
  // When a registered worker is loaded again (e.g. when the webpage calls
  // the ServiceWorkerRegistration's update method):
  //
  // - both the main and imported scripts are loaded by the
  //   CompareManager::FetchScript
  // - the update requests for the imported scripts will also be calling this
  //   method and we should expect scripts redirected to an extension script
  //   to have a null httpChannel.
  //
  // The request that triggers this method is:
  //
  // - the one that is coming from the network (which may be intercepted by
  //   WebRequest listeners in extensions and redirected to a web_accessible
  //   moz-extension url)
  // - it will then be compared with a previous response that we may have
  //   in the cache
  //
  // When the next service worker update occurs, if the request (for an imported
  // script) is not redirected by an extension the cache entry is invalidated
  // and a network request is triggered for the import.
  if (!httpChannel) {
    // Redirecting a service worker main script should fail before reaching this
    // method.
    // If a main script is somehow redirected, the diagnostic assert will crash
    // in non-release builds.  Release builds will return an explicit error.
    MOZ_DIAGNOSTIC_ASSERT(!mIsMainScript,
                          "Unexpected ServiceWorker main script redirected");
    if (mIsMainScript) {
      return NS_ERROR_UNEXPECTED;
    }

    nsCOMPtr<nsIPrincipal> channelPrincipal;

    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    if (!ssm) {
      return NS_ERROR_UNEXPECTED;
    }

    nsresult rv = ssm->GetChannelResultPrincipal(
        channel, getter_AddRefs(channelPrincipal));

    // An extension did redirect a non-MainScript request to a moz-extension url
    // (in that case the originalURL is the resolved jar URI and so we have to
    // look to the channel principal instead).
    if (channelPrincipal->SchemeIs("moz-extension")) {
      UniquePtr<char16_t[], JS::FreePolicy> buffer;
      size_t len = 0;

      rv = ScriptLoader::ConvertToUTF16(channel, aString, aLen, u"UTF-8"_ns,
                                        nullptr, buffer, len);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mBuffer.Adopt(buffer.release(), len);

      return NS_OK;
    }

    // Make non-release and debug builds to crash if this happens and fail
    // explicitly on release builds.
    MOZ_DIAGNOSTIC_CRASH(
        "ServiceWorker imported script redirected to an url "
        "with an unexpected scheme");
    return NS_ERROR_UNEXPECTED;
  }

  bool requestSucceeded;
  rv = httpChannel->GetRequestSucceeded(&requestSucceeded);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_OK;
  }

  if (NS_WARN_IF(!requestSucceeded)) {
    // Get the stringified numeric status code, not statusText which could be
    // something misleading like OK for a 404.
    uint32_t status = 0;
    (void)httpChannel->GetResponseStatus(
        &status);  // don't care if this fails, use 0.
    nsAutoString statusAsText;
    statusAsText.AppendInt(status);

    ServiceWorkerManager::LocalizeAndReportToAllClients(
        mRegistration->Scope(), "ServiceWorkerRegisterNetworkError",
        nsTArray<nsString>{NS_ConvertUTF8toUTF16(mRegistration->Scope()),
                           std::move(statusAsText),
                           NS_ConvertUTF8toUTF16(mURL)});

    rv = NS_ERROR_FAILURE;
    return NS_OK;
  }

  // Note: we explicitly don't check for the return value here, because the
  // absence of the header is not an error condition.
  (void)httpChannel->GetResponseHeader("Service-Worker-Allowed"_ns, mMaxScope);

  // [9.2 Update]4.13, If response's cache state is not "local",
  // set registration's last update check time to the current time
  if (!mIsFromCache) {
    mRegistration->RefreshLastUpdateCheckTime();
  }

  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  if (!JS::Prefs::experimental_import_text() ||
      (loadInfo->GetExternalContentPolicyType() !=
       ExtContentPolicyType::TYPE_TEXT)) {
    nsAutoCString mimeType;
    rv = httpChannel->GetContentType(mimeType);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      // We should only end up here if !mResponseHead in the channel.  If
      // headers were received but no content type was specified, we'll be given
      // UNKNOWN_CONTENT_TYPE "application/x-unknown-content-type" and so fall
      // into the next case with its better error message.
      rv = NS_ERROR_DOM_SECURITY_ERR;
      return rv;
    }

    auto mimeTypeUTF16 = NS_ConvertUTF8toUTF16(mimeType);
    // The top-level service worker script must be served with a JavaScript
    // MIME type. JSON is only permitted for non-top-level (imported) modules,
    // such as `import data from "./x.json" with { type: "json" }`.
    if (mimeTypeUTF16.IsEmpty() ||
        !(nsContentUtils::IsJavascriptMIMEType(mimeTypeUTF16) ||
          (!mIsMainScript && nsContentUtils::IsJsonMimeType(mimeTypeUTF16)))) {
      ServiceWorkerManager::LocalizeAndReportToAllClients(
          mRegistration->Scope(), "ServiceWorkerRegisterMimeTypeError2",
          nsTArray<nsString>{NS_ConvertUTF8toUTF16(mRegistration->Scope()),
                             std::move(mimeTypeUTF16),
                             NS_ConvertUTF8toUTF16(mURL)});
      rv = NS_ERROR_DOM_SECURITY_ERR;
      return rv;
    }
  }

  nsCOMPtr<nsIURI> channelURL;
  rv = httpChannel->GetURI(getter_AddRefs(channelURL));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Append the final URL if its different from the original
  // request URL.  This lets us note that a redirect occurred
  // even though we don't track every redirect URL here.
  MOZ_DIAGNOSTIC_ASSERT(!mURLList.IsEmpty());

  bool equals = false;
  if (NS_FAILED(channelURL->Equals(mURLList[0], &equals)) || !equals) {
    mURLList.AppendElement(WrapNotNull(channelURL.get()));
  }

  UniquePtr<char16_t[], JS::FreePolicy> buffer;
  size_t len = 0;

  rv = ScriptLoader::ConvertToUTF16(httpChannel, aString, aLen, u"UTF-8"_ns,
                                    nullptr, buffer, len);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mBuffer.Adopt(buffer.release(), len);

  rv = NS_OK;
  return NS_OK;
}

nsresult CompareCache::Initialize(Cache* const aCache, const nsACString& aURL) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCache);
  MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForInitialization);

  // This JSContext will not end up executing JS code because here there are
  // no ReadableStreams involved.
  AutoJSAPI jsapi;
  jsapi.Init();

  RequestOrUTF8String request;
  request.SetAsUTF8String() = aURL;
  ErrorResult error;
  CacheQueryOptions params;
  RefPtr<Promise> promise = aCache->Match(jsapi.cx(), request, params, error);
  if (NS_WARN_IF(error.Failed())) {
    // No exception here because there are no ReadableStreams involved here.
    MOZ_ASSERT(!error.IsJSException());
    mState = Finished;
    return error.StealNSResult();
  }

  // Retrieve the script from aCache.
  mState = WaitingForScript;
  promise->AppendNativeHandler(this);
  return NS_OK;
}

void CompareCache::Finish(nsresult aStatus, bool aInCache) {
  if (mState != Finished) {
    mState = Finished;
    mInCache = aInCache;
    mCN->CacheFinish(aStatus);
  }
}

void CompareCache::Abort() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState != Finished) {
    mState = Finished;

    if (mPump) {
      mPump->CancelWithReason(NS_BINDING_ABORTED, "CompareCache::Abort"_ns);
      mPump = nullptr;
    }
  }
}

NS_IMETHODIMP
CompareCache::OnStreamComplete(nsIStreamLoader* aLoader, nsISupports* aContext,
                               nsresult aStatus, uint32_t aLen,
                               const uint8_t* aString) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState == Finished) {
    return aStatus;
  }

  if (NS_WARN_IF(NS_FAILED(aStatus))) {
    Finish(aStatus, false);
    return aStatus;
  }

  UniquePtr<char16_t[], JS::FreePolicy> buffer;
  size_t len = 0;

  nsresult rv = ScriptLoader::ConvertToUTF16(nullptr, aString, aLen,
                                             u"UTF-8"_ns, nullptr, buffer, len);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Finish(rv, false);
    return rv;
  }

  mBuffer.Adopt(buffer.release(), len);

  Finish(NS_OK, true);
  return NS_OK;
}

void CompareCache::ResolvedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());

  switch (mState) {
    case Finished:
      return;
    case WaitingForScript:
      ManageValueResult(aCx, aValue);
      return;
    default:
      MOZ_CRASH("Unacceptable state.");
  }
}

void CompareCache::RejectedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState != Finished) {
    Finish(NS_ERROR_FAILURE, false);
    return;
  }
}

void CompareCache::ManageValueResult(JSContext* aCx,
                                     JS::Handle<JS::Value> aValue) {
  MOZ_ASSERT(NS_IsMainThread());

  // The cache returns undefined if the object is not stored.
  if (aValue.isUndefined()) {
    Finish(NS_OK, false);
    return;
  }

  MOZ_ASSERT(aValue.isObject());

  JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
  if (NS_WARN_IF(!obj)) {
    Finish(NS_ERROR_FAILURE, false);
    return;
  }

  Response* response = nullptr;
  nsresult rv = UNWRAP_OBJECT(Response, &obj, response);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Finish(rv, false);
    return;
  }

  MOZ_ASSERT(response->Ok());

  nsCOMPtr<nsIInputStream> inputStream;
  response->GetBody(getter_AddRefs(inputStream));
  MOZ_ASSERT(inputStream);

  MOZ_ASSERT(!mPump);
  rv = NS_NewInputStreamPump(getter_AddRefs(mPump), inputStream.forget(),
                             0,     /* default segsize */
                             0,     /* default segcount */
                             false, /* default closeWhenDone */
                             GetMainThreadSerialEventTarget());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Finish(rv, false);
    return;
  }

  nsCOMPtr<nsIStreamLoader> loader;
  rv = NS_NewStreamLoader(getter_AddRefs(loader), this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    Finish(rv, false);
    return;
  }

  rv = mPump->AsyncRead(loader);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mPump = nullptr;
    Finish(rv, false);
    return;
  }

  nsCOMPtr<nsIThreadRetargetableRequest> rr = do_QueryInterface(mPump);
  if (rr) {
    nsCOMPtr<nsIEventTarget> sts =
        do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
    RefPtr<TaskQueue> queue =
        TaskQueue::Create(sts.forget(), "CompareCache STS Delivery Queue");
    rv = rr->RetargetDeliveryTo(queue);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mPump = nullptr;
      Finish(rv, false);
      return;
    }
  }
}

nsresult CompareManager::Initialize(nsIPrincipal* aPrincipal,
                                    const nsACString& aURL,
                                    const nsAString& aCacheName) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(mPendingCount == 0);
  MOZ_DIAGNOSTIC_ASSERT(mState == WaitingForInitialization);

  // RAII Cleanup when fails.
  auto guard = MakeScopeExit([&] { Cleanup(); });

  mURL = aURL;
  mPrincipal = aPrincipal;

  // Always create a CacheStorage since we want to write the network entry to
  // the cache even if there isn't an existing one.
  AutoJSAPI jsapi;
  jsapi.Init();
  ErrorResult result;
  mCacheStorage = CreateCacheStorage(jsapi.cx(), aPrincipal, result);
  if (NS_WARN_IF(result.Failed())) {
    MOZ_ASSERT(!result.IsErrorWithMessage());
    return result.StealNSResult();
  }

  // If there is no existing cache, proceed to fetch the script directly.
  if (aCacheName.IsEmpty()) {
    mState = WaitingForScriptOrComparisonResult;
    nsresult rv = FetchScript(aURL, true /* aIsMainScript */);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    guard.release();
    return NS_OK;
  }

  // Open the cache saving the old source scripts.
  RefPtr<Promise> promise = mCacheStorage->Open(aCacheName, result);
  if (NS_WARN_IF(result.Failed())) {
    MOZ_ASSERT(!result.IsErrorWithMessage());
    return result.StealNSResult();
  }

  mState = WaitingForExistingOpen;
  promise->AppendNativeHandler(this);

  guard.release();
  return NS_OK;
}

// This class manages 4 promises if needed:
// 1. Retrieve the Cache object by a given CacheName of OldCache.
// 2. Retrieve the URLs saved in OldCache.
// 3. Retrieve the Cache object of the NewCache for the newly created SW.
// 4. Put the value in the cache.
// For this reason we have mState to know what callback we are handling.
void CompareManager::ResolvedCallback(JSContext* aCx,
                                      JS::Handle<JS::Value> aValue,
                                      ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mCallback);

  switch (mState) {
    case Finished:
      return;
    case WaitingForExistingOpen:
      ManageOldCache(aCx, aValue);
      return;
    case WaitingForExistingKeys:
      ManageOldKeys(aCx, aValue);
      return;
    case WaitingForOpen:
      ManageNewCache(aCx, aValue);
      return;
    case WaitingForPut:
      MOZ_DIAGNOSTIC_ASSERT(mPendingCount > 0);
      if (--mPendingCount == 0) {
        mCallback->ComparisonResult(NS_OK, false /* aIsEqual */, mOnFailure,
                                    mNewCacheName, mMaxScope, mLoadFlags);
        Cleanup();
      }
      return;
    default:
      MOZ_DIAGNOSTIC_CRASH("Missing case in CompareManager::ResolvedCallback");
  }
}

void CompareManager::RejectedCallback(JSContext* aCx,
                                      JS::Handle<JS::Value> aValue,
                                      ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  switch (mState) {
    case Finished:
      return;
    case WaitingForExistingOpen:
      NS_WARNING("Could not open the existing cache.");
      break;
    case WaitingForExistingKeys:
      NS_WARNING("Could not get the existing URLs.");
      break;
    case WaitingForOpen:
      NS_WARNING("Could not open cache.");
      break;
    case WaitingForPut:
      NS_WARNING("Could not write to cache.");
      break;
    default:
      MOZ_DIAGNOSTIC_CRASH("Missing case in CompareManager::RejectedCallback");
  }

  Fail(NS_ERROR_FAILURE);
}

void CompareManager::Fail(nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());
  mCallback->ComparisonResult(aStatus, false /* aIsEqual */, mOnFailure, u""_ns,
                              ""_ns, mLoadFlags);
  Cleanup();
}

void CompareManager::Cleanup() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mState != Finished) {
    mState = Finished;

    MOZ_ASSERT(mCallback);
    mCallback = nullptr;

    // Abort and release CompareNetworks.
    for (uint32_t i = 0; i < mCNList.Length(); ++i) {
      mCNList[i]->Abort();
    }
    mCNList.Clear();
  }
}

}  // namespace

nsresult PurgeCache(nsIPrincipal* aPrincipal, const nsAString& aCacheName) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);

  if (aCacheName.IsEmpty()) {
    return NS_OK;
  }

  AutoJSAPI jsapi;
  jsapi.Init();
  ErrorResult rv;
  RefPtr<CacheStorage> cacheStorage =
      CreateCacheStorage(jsapi.cx(), aPrincipal, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return rv.StealNSResult();
  }

  // We use the ServiceWorker scope as key for the cacheStorage.
  RefPtr<Promise> promise = cacheStorage->Delete(aCacheName, rv);
  if (NS_WARN_IF(rv.Failed())) {
    return rv.StealNSResult();
  }

  // Set [[PromiseIsHandled]] to ensure that if this promise gets rejected,
  // we don't end up reporting a rejected promise to the console.
  MOZ_ALWAYS_TRUE(promise->SetAnyPromiseIsHandled());

  // We don't actually care about the result of the delete operation.
  return NS_OK;
}

nsresult GenerateCacheName(nsAString& aName) {
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidGenerator =
      do_GetService("@mozilla.org/uuid-generator;1", &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsID id;
  rv = uuidGenerator->GenerateUUIDInPlace(&id);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  char chars[NSID_LENGTH];
  id.ToProvidedString(chars);

  // NSID_LENGTH counts the null terminator.
  aName.AssignASCII(chars, NSID_LENGTH - 1);

  return NS_OK;
}

nsresult Compare(ServiceWorkerRegistrationInfo* aRegistration,
                 nsIPrincipal* aPrincipal, const nsAString& aCacheName,
                 const nsACString& aURL, CompareCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRegistration);
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aURL.IsEmpty());
  MOZ_ASSERT(aCallback);

  RefPtr<CompareManager> cm = new CompareManager(aRegistration, aCallback);

  nsresult rv = cm->Initialize(aPrincipal, aURL, aCacheName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

}  // namespace mozilla::dom::serviceWorkerScriptCache
