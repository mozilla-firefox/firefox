/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_ScriptLoadRequest_h
#define js_loader_ScriptLoadRequest_h

#include "mozilla/Assertions.h"
#include "mozilla/dom/CacheExpirationTime.h"
#include "mozilla/dom/SRIMetadata.h"
#include "mozilla/Encoding.h"
#include "mozilla/LinkedList.h"
#include "mozilla/PreloaderBase.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SharedSubResourceCache.h"  // mozilla::SubResourceNetworkMetadataHolder
#include "mozilla/StaticPrefs_dom.h"

#include "LoadedScript.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIGlobalObject.h"
#include "ScriptFetchOptions.h"
#include "ScriptKind.h"

#include "js/experimental/JSStencil.h"
#include "js/RootingAPI.h"
#include "js/SourceText.h"
#include "js/TypeDecls.h"

namespace mozilla::dom {

class ScriptLoadContext;
class WorkerLoadContext;
class WorkletLoadContext;
enum class RequestPriority : uint8_t;

}  // namespace mozilla::dom

namespace mozilla::loader {
class SyncLoadContext;
}  // namespace mozilla::loader

namespace JS::loader {

class LoadContextBase;
class ModuleLoadRequest;
class ScriptLoadRequestList;

/*
 * ScriptLoadRequest
 *
 * ScriptLoadRequest is a generic representation of a request/response for
 * JavaScript file that will be loaded by a Script/Module loader. This
 * representation is used by the following:
 *   - DOM ScriptLoader / ModuleLoader
 *   - worker ScriptLoader / ModuleLoader
 *   - worklet ScriptLoader
 *   - SyncModuleLoader
 *
 * The ScriptLoadRequest contains information specific to the current request,
 * such as the kind of script (classic, module, etc), and the reference to the
 * LoadedScript which contains the information independent of the current
 * request, such as the URI, the ScriptFetchOptions, etc.
 *
 * Relationship to ScriptLoadContext:
 *
 * ScriptLoadRequest and ScriptLoadContexts have a circular pointer.  A
 * ScriptLoadContext augments the loading of a ScriptLoadRequest by providing
 * additional information regarding the loading and evaluation behavior (see
 * the ScriptLoadContext class for details).  In terms of responsibility,
 * the ScriptLoadRequest represents "What" is being loaded, and the
 * ScriptLoadContext represents "How".
 *
 * TODO: see if we can use it in the jsshell script loader. We need to either
 * remove ISUPPORTS or find a way to encorporate that in the jsshell. We would
 * then only have one implementation of the script loader, and it would be
 * tested whenever jsshell tests are run. This would mean finding another way to
 * create ScriptLoadRequest lists.
 *
 */

class ScriptLoadRequest : public nsISupports,
                          private mozilla::LinkedListElement<ScriptLoadRequest>,
                          public LoadedScriptDelegate<ScriptLoadRequest> {
  using super = LinkedListElement<ScriptLoadRequest>;

  // Allow LinkedListElement<ScriptLoadRequest> to cast us to itself as needed.
  friend class mozilla::LinkedListElement<ScriptLoadRequest>;
  friend class ScriptLoadRequestList;

 protected:
  virtual ~ScriptLoadRequest();

 public:
  using SRIMetadata = mozilla::dom::SRIMetadata;
  ScriptLoadRequest(ScriptKind aKind, const SRIMetadata& aIntegrity,
                    nsIURI* aReferrer, LoadContextBase* aContext,
                    const mozilla::Encoding* aClassicScriptHintEncoding);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(ScriptLoadRequest)

  using super::getNext;
  using super::isInList;

  template <typename T, typename D = DeletePolicy<T>>
  using UniquePtr = mozilla::UniquePtr<T, D>;

  bool IsClassicScript() const { return mKind == ScriptKind::eClassic; }
  bool IsModuleRequest() const { return mKind == ScriptKind::eModule; }
  bool IsImportMapRequest() const { return mKind == ScriptKind::eImportMap; }
  bool IsSpeculationRulesRequest() const {
    return mKind == ScriptKind::eSpeculationRules;
  }

  ModuleLoadRequest* AsModuleRequest();
  const ModuleLoadRequest* AsModuleRequest() const;

  CacheExpirationTime ExpirationTime() const {
    // The request's expiration time is used only when it's received from
    // necko.  For in-memory cached, case, the
    // SharedSubResourceCache::CompleteSubResource::mExpirationTime field is
    // used instead.
    MOZ_ASSERT(!IsRetrievedFromMemoryCache());
    return mExpirationTime;
  }

  void SetMinimumExpirationTime(const CacheExpirationTime& aExpirationTime) {
    mExpirationTime.SetMinimum(aExpirationTime);
  }

  virtual bool IsTopLevel() const { return true; };

  virtual void Cancel();

  virtual void SetReady();

  enum class State : uint8_t {
    // The initial state before checking the in-memory cache.
    CheckingCache,

    // One of the following:
    //   * Fetching the script text from the network
    //   * Fetching the script text from the necko cache
    //   * Fetching the serialized stencil from the necko cache
    Fetching,

    // Retrieved the already-compiled script from the in-memory cache, but not
    // yet transitioned to Ready state, in order to avoid processing the script
    // in the same timing as the script is inserted.
    DelayingReady,

    // Fetched the script text either from the network or the necko cache,
    // and performing off-thread compilation.
    //
    // NOTE: This state is not used when the compilation is done on the
    //       main thread.
    Compiling,

    // One of the following:
    //   * Fetched the script text from the network or the necko cache,
    //     and the compilation is done
    //   * Fetched the serialized stencil from the necko cache, and the
    //     decoding is done
    //   * Retrieved from the stencil in-memory cache
    // The script can be executed, or is already executed.
    Ready,

    // The request is cancelled, and the script is no longer used.
    Canceled
  };

  // Before any attempt at fetching resources from the cache we should first
  // make sure that the resource does not yet exists in the cache. In which case
  // we might simply alias its LoadedScript. Otherwise a new one would be
  // created.
  bool IsCheckingCache() const { return mState == State::CheckingCache; }

  // Setup and load resources, to fill the LoadedScript and make it usable by
  // the JavaScript engine.
  bool IsFetching() const { return mState == State::Fetching; }
  bool IsDelayingReady() const { return mState == State::DelayingReady; }
  bool IsCompiling() const { return mState == State::Compiling; }
  bool IsCanceled() const { return mState == State::Canceled; }

  // Return whether the request has been completed, either successfully or
  // otherwise.
  bool IsFinished() const {
    return mState == State::Ready || mState == State::Canceled;
  }

  mozilla::dom::ReferrerPolicy ReferrerPolicy() const {
    return FetchInfo()->ReferrerPolicy();
  }

  nsIURI* BaseURL() const { return FetchInfo()->BaseURL(); }
  void SetBaseURL(nsIURI* aBaseURL) { FetchInfo()->SetBaseURL(aBaseURL); }
  void SetBaseURLFromChannelAndOriginalURI(nsIChannel* aChannel,
                                           nsIURI* aOriginalURI) {
    FetchInfo()->SetBaseURLFromChannelAndOriginalURI(aChannel, aOriginalURI);
  }

  ScriptFetchOptions* FetchOptions() const {
    return FetchInfo()->FetchOptions();
  }

  mozilla::dom::RequestPriority FetchPriority() const {
    return FetchOptions()->mFetchPriority;
  }

  enum ParserMetadata ParserMetadata() const {
    return FetchOptions()->mParserMetadata;
  }

  const nsString& Nonce() const { return FetchOptions()->mNonce; }

  nsIPrincipal* TriggeringPrincipal() const {
    return FetchOptions()->mTriggeringPrincipal;
  }

  // Convert a CheckingCache ScriptLoadRequest into a DelayingReady
  // (for classic script and import map) or Fetching (for module script),
  // by populating the script data from the cached script.
  //
  // The DelayingReady state should be converted into Ready state, by calling
  // SetReady in the next event tick.
  //
  // aFetchOptions is the current request's fetch option, which can have
  // different nonce value.
  void CacheEntryFound(LoadedScript* aLoadedScript,
                       ScriptFetchOptions* aFetchOptions);

  // Use the cached entry, reviving from the dirty state.
  // This should be called when the request is getting fetched from necko,
  // and this keeps the request in the Fetching state.
  void CacheEntryRevived(LoadedScript* aLoadedScript);

  // Convert a CheckingCache ScriptLoadRequest into a Fetching one, by creating
  // a new LoadedScript which is matching the ScriptKind provided when
  // constructing this ScriptLoadRequest.
  void NoCacheEntryFound(mozilla::dom::ReferrerPolicy aReferrerPolicy,
                         ScriptFetchOptions* aFetchOptions, nsIURI* aURI);

 private:
  void SetCacheEntry(LoadedScript* aLoadedScript,
                     ScriptFetchOptions* aFetchOptions);

 public:
  bool PassedConditionForDiskCache() const {
    return mDiskCachingPlan == CachingPlan::PassedCondition;
  }

  bool PassedConditionForMemoryCache() const {
    return mMemoryCachingPlan == CachingPlan::PassedCondition;
  }

  bool PassedConditionForEitherCache() const {
    return PassedConditionForDiskCache() || PassedConditionForMemoryCache();
  }

  void MarkNotCacheable() {
    mDiskCachingPlan = CachingPlan::NotCacheable;
    mMemoryCachingPlan = CachingPlan::NotCacheable;
  }

  bool IsMarkedNotCacheable() const {
    MOZ_ASSERT_IF(mDiskCachingPlan == CachingPlan::NotCacheable,
                  mMemoryCachingPlan == CachingPlan::NotCacheable);
    MOZ_ASSERT_IF(mDiskCachingPlan != CachingPlan::NotCacheable,
                  mMemoryCachingPlan != CachingPlan::NotCacheable);
    return mDiskCachingPlan == CachingPlan::NotCacheable;
  }

  void MarkSkippedDiskCaching() {
    MOZ_ASSERT(mDiskCachingPlan == CachingPlan::Uninitialized ||
               mDiskCachingPlan == CachingPlan::PassedCondition);
    mDiskCachingPlan = CachingPlan::Skipped;
  }

  void MarkSkippedMemoryCaching() {
    MOZ_ASSERT(mMemoryCachingPlan == CachingPlan::Uninitialized ||
               mMemoryCachingPlan == CachingPlan::PassedCondition);
    mMemoryCachingPlan = CachingPlan::Skipped;
  }

  void MarkSkippedAllCaching() {
    MarkSkippedDiskCaching();
    MarkSkippedMemoryCaching();
  }

  void MarkPassedConditionForDiskCache() {
    MOZ_ASSERT(mDiskCachingPlan == CachingPlan::Uninitialized);
    mDiskCachingPlan = CachingPlan::PassedCondition;
  }

  void MarkPassedConditionForMemoryCache() {
    MOZ_ASSERT(mMemoryCachingPlan == CachingPlan::Uninitialized);
    mMemoryCachingPlan = CachingPlan::PassedCondition;
  }

  mozilla::CORSMode CORSMode() const { return FetchOptions()->mCORSMode; }

  bool HasLoadContext() const { return mLoadContext; }
  bool HasScriptLoadContext() const;
  bool HasWorkerLoadContext() const;

  mozilla::dom::ScriptLoadContext* GetScriptLoadContext();
  const mozilla::dom::ScriptLoadContext* GetScriptLoadContext() const;

  mozilla::loader::SyncLoadContext* GetSyncLoadContext();

  mozilla::dom::WorkerLoadContext* GetWorkerLoadContext();

  mozilla::dom::WorkletLoadContext* GetWorkletLoadContext();

  const LoadedScript* getLoadedScript() const { return mLoadedScript.get(); }
  LoadedScript* getLoadedScript() { return mLoadedScript.get(); }

  bool HasStencil() const { return !!mStencil; }
  JS::Stencil* GetStencil() const { return mStencil; }
  void SetStencil(JS::Stencil* aStencil) { mStencil = aStencil; }
  void ClearStencil() { mStencil = nullptr; }

  bool HasSourceMapURL() const { return mHasSourceMapURL_; }
  const nsString& GetSourceMapURL() const {
    MOZ_ASSERT(mHasSourceMapURL_);
    return mMaybeSourceMapURL_;
  }
  void SetSourceMapURL(const nsString& aSourceMapURL) {
    MOZ_ASSERT(!mHasSourceMapURL_);
    mMaybeSourceMapURL_ = aSourceMapURL;
    mHasSourceMapURL_ = true;
  }

  bool HasDirtyCache() const { return mHasDirtyCache_; }
  void SetHasDirtyCache() { mHasDirtyCache_ = true; }

  bool HadPostponed() const { return mHadPostponed_; }
  void SetHadPostponed() { mHadPostponed_ = true; }

  bool HadRedirected() const { return mHadRedirected_; }
  void SetHadRedirected() { mHadRedirected_ = true; }

  const ScriptFetchInfo* FetchInfo() const { return mFetchInfo; }
  ScriptFetchInfo* FetchInfo() { return mFetchInfo; }

  // Becomes true if the actual script data is retrieved from the
  // SharedScriptCache.
  // This becomes true in the following two situations:
  //   * A valid cache is found when starting the request
  //   * A dirty cache is found when starting the request, and the
  //     cache is revived (becomes true only after revived)
  //
  // This is different than LoadedScript::IsCachedStencil, given that
  // LoadedScript::IsCachedStencil can become true also when the
  // script data is retrieved as text or serialized stencil and then
  // converted to the cached stencil.
  bool IsRetrievedFromMemoryCache() const {
    return mIsRetrievedFromMemoryCache;
  }

  // Given that the LoadedScript::mDataType field used by
  // LoadedScript::IsTextSource and LoadedScript::IsSerializedStencil be set to
  // eCachedStencil or eInvalidatedCachedStencil after setting to
  // others, the following accessors can be called only if one of the following
  // conditions is met:
  //   * while receiving the response from necko,
  //     which means the cached case never reaches the code path
  //   * before converting to cached stencil,
  //     which means only text or serialized stencil cases can reach the code
  //     path
  //   * after filtering out the cached stencil cases
  bool IsFetchedAsTextSource() const {
    MOZ_ASSERT(!IsRetrievedFromMemoryCache());
    MOZ_ASSERT(!getLoadedScript()->IsCachedStencil());
    MOZ_ASSERT(!getLoadedScript()->IsInvalidatedCachedStencil());
    return getLoadedScript()->IsTextSource();
  }
  bool IsRetrievedAsSerializedStencil() const {
    MOZ_ASSERT(!IsRetrievedFromMemoryCache());
    MOZ_ASSERT(!getLoadedScript()->IsCachedStencil());
    MOZ_ASSERT(!getLoadedScript()->IsInvalidatedCachedStencil());
    return getLoadedScript()->IsSerializedStencil();
  }

 public:
  // Fields.

  // Whether this is a classic script, a module script, an import map, or a
  // speculation rule set.
  const ScriptKind mKind;

  // Are we still waiting for a load to complete?
  State mState;

  // Request source, not cached serialized Stencil.
  bool mFetchSourceOnly : 1;

  // Becomes true if this has source map url.
  //
  // Do not access directly.
  // Use HasSourceMapURL(), SetSourceMapURL(), and GetSourceMapURL().
  bool mHasSourceMapURL_ : 1;

  // Set to true if this response is found in the in-memory cache, but the
  // cache is marked as dirty, and needs validation.
  //
  // This request should go to necko, and when the response is received,
  // the cache should be either revived or evicted.
  bool mHasDirtyCache_ : 1;

  // Set to true if this script had already been postponed in the scheduling.
  bool mHadPostponed_ : 1;

  enum class CachingPlan : uint8_t {
    // This is not yet considered for caching.
    Uninitialized,

    // This request is not cacheable (e.g. inline script, JSON module).
    NotCacheable,

    // This request is cacheable, but is marked for skipping due to
    // not passing conditions.
    Skipped,

    // This fits the condition for the caching (e.g. file size, fetch count).
    PassedCondition,
  };
  CachingPlan mDiskCachingPlan : 2;
  CachingPlan mMemoryCachingPlan : 2;

  bool mIsRetrievedFromMemoryCache : 1;

  bool mHadRedirected_ : 1;

  CacheExpirationTime mExpirationTime = CacheExpirationTime::Never();

  RefPtr<mozilla::SubResourceNetworkMetadataHolder> mNetworkMetadata;
  const SRIMetadata mIntegrity;
  const nsCOMPtr<nsIURI> mReferrer;

  // Holds source map url for loaded scripts.
  //
  // Do not access directly.
  // Use HasSourceMapURL(), SetSourceMapURL(), and GetSourceMapURL().
  nsString mMaybeSourceMapURL_;

  nsCOMPtr<nsIPrincipal> mOriginPrincipal;

  // Keep the URI's filename alive during off thread parsing.
  // Also used by workers to report on errors while loading, and used by
  // worklets as the file name in compile options.
  nsAutoCString mURL;

  // The loaded script holds the data which can be shared among similar requests
  RefPtr<LoadedScript> mLoadedScript;

  // Either the result of compilation/decode, or the stencil retrieved from
  // cache.
  // Once this field is populated, it's is guaranteed to be valid until the
  // stencil is instantiated.
  RefPtr<JS::Stencil> mStencil;

  RefPtr<ScriptFetchInfo> mFetchInfo;

  // LoadContext for augmenting the load depending on the loading
  // context (DOM, Worker, etc.)
  RefPtr<LoadContextBase> mLoadContext;

  // EarlyHintRegistrar id to connect the http channel back to the preload, with
  // a default of value of 0 indicating that this request is not an early hints
  // preload.
  uint64_t mEarlyHintPreloaderId;

  // The charset attribute of the script element or the link element.
  // This can be nullptr if there's no charset attribute, or the charset
  // attribute has no effect (modules etc).
  //
  // The actual encoding used for decoding the received script source can be
  // different than this.
  // See ScriptLoadHandler::TrySetDecoder for more details.
  const mozilla::Encoding* mClassicScriptHintEncoding = nullptr;
};

}  // namespace JS::loader

#endif  // js_loader_ScriptLoadRequest_h
