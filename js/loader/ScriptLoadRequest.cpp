/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptLoadRequest.h"

#include "mozilla/dom/Document.h"
#include "mozilla/dom/ScriptLoadContext.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WorkerLoadContext.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit

#include "GeckoProfiler.h"
#include "ModuleLoadRequest.h"
#include "nsContentUtils.h"
#include "nsIClassOfService.h"
#include "nsISupportsPriority.h"

#include "js/SourceText.h"

using JS::SourceText;

namespace JS::loader {

//////////////////////////////////////////////////////////////
// ScriptFetchOptions
//////////////////////////////////////////////////////////////

ScriptFetchOptions::ScriptFetchOptions(
    mozilla::CORSMode aCORSMode, const nsAString& aNonce,
    mozilla::dom::RequestPriority aFetchPriority,
    const ParserMetadata aParserMetadata, nsIPrincipal* aTriggeringPrincipal)
    : mCORSMode(aCORSMode),
      mFetchPriority(aFetchPriority),
      mParserMetadata(aParserMetadata),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mNonce(aNonce) {}

void ScriptFetchOptions::SetTriggeringPrincipal(
    nsIPrincipal* aTriggeringPrincipal) {
  MOZ_ASSERT(!mTriggeringPrincipal);
  mTriggeringPrincipal = aTriggeringPrincipal;
}

// static
already_AddRefed<ScriptFetchOptions> ScriptFetchOptions::CreateDefault() {
  RefPtr<ScriptFetchOptions> options = new ScriptFetchOptions(
      mozilla::CORS_NONE, /* aNonce = */ u""_ns,
      mozilla::dom::RequestPriority::Auto, ParserMetadata::NotParserInserted);
  return options.forget();
}

ScriptFetchOptions::~ScriptFetchOptions() = default;

//////////////////////////////////////////////////////////////
// ScriptLoadRequest
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ScriptLoadRequest)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ScriptLoadRequest)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ScriptLoadRequest)

// ScriptLoadRequest can be accessed from multiple threads.
//
// For instance, worker script loader passes the ScriptLoadRequest to
// the main thread to perform the actual load.
// Even while it's handled by the main thread, the ScriptLoadRequest is
// the target of the worker thread's cycle collector.
//
// Fields that can be modified by the main thread shouldn't be touched by
// the cycle collection.
//
// NOTE: LoadedScript, nsIURI, and nsIPrincipal don't have to be touched here
//       because they cannot be a part of cycle.
NS_IMPL_CYCLE_COLLECTION(ScriptLoadRequest, mLoadContext)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(ScriptLoadRequest)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

ScriptLoadRequest::ScriptLoadRequest(
    ScriptKind aKind, const SRIMetadata& aIntegrity, nsIURI* aReferrer,
    LoadContextBase* aContext,
    const mozilla::Encoding* aClassicScriptHintEncoding)
    : mKind(aKind),
      mState(State::CheckingCache),
      mFetchSourceOnly(false),
      mHasSourceMapURL_(false),
      mHasDirtyCache_(false),
      mHadPostponed_(false),
      mDiskCachingPlan(CachingPlan::Uninitialized),
      mMemoryCachingPlan(CachingPlan::Uninitialized),
      mIsRetrievedFromMemoryCache(false),
      mHadRedirected_(false),
      mIntegrity(aIntegrity),
      mReferrer(aReferrer),
      mLoadContext(aContext),
      mEarlyHintPreloaderId(0),
      mClassicScriptHintEncoding(aClassicScriptHintEncoding) {
  if (mLoadContext) {
    mLoadContext->SetRequest(this);
  }
}

ScriptLoadRequest::~ScriptLoadRequest() {
  PROFILER_MARKER("~ScriptLoadRequest", JS, {}, TerminatingFlowMarker,
                  Flow::FromPointer(this));
}

void ScriptLoadRequest::SetReady() {
  MOZ_ASSERT(!IsFinished());
  mState = State::Ready;
}

void ScriptLoadRequest::Cancel() {
  mState = State::Canceled;
  if (HasScriptLoadContext()) {
    GetScriptLoadContext()->MaybeCancelOffThreadScript();
    GetScriptLoadContext()->MaybeUnblockOnload();
  }
}

bool ScriptLoadRequest::HasScriptLoadContext() const {
  return HasLoadContext() && mLoadContext->IsWindowContext();
}

bool ScriptLoadRequest::HasWorkerLoadContext() const {
  return HasLoadContext() && mLoadContext->IsWorkerContext();
}

mozilla::dom::ScriptLoadContext* ScriptLoadRequest::GetScriptLoadContext() {
  MOZ_ASSERT(mLoadContext);
  return mLoadContext->AsWindowContext();
}

const mozilla::dom::ScriptLoadContext* ScriptLoadRequest::GetScriptLoadContext()
    const {
  MOZ_ASSERT(mLoadContext);
  return mLoadContext->AsWindowContext();
}

mozilla::loader::SyncLoadContext* ScriptLoadRequest::GetSyncLoadContext() {
  MOZ_ASSERT(mLoadContext);
  return mLoadContext->AsSyncContext();
}

mozilla::dom::WorkerLoadContext* ScriptLoadRequest::GetWorkerLoadContext() {
  MOZ_ASSERT(mLoadContext);
  return mLoadContext->AsWorkerContext();
}

mozilla::dom::WorkletLoadContext* ScriptLoadRequest::GetWorkletLoadContext() {
  MOZ_ASSERT(mLoadContext);
  return mLoadContext->AsWorkletContext();
}

ModuleLoadRequest* ScriptLoadRequest::AsModuleRequest() {
  MOZ_ASSERT(IsModuleRequest());
  return static_cast<ModuleLoadRequest*>(this);
}

const ModuleLoadRequest* ScriptLoadRequest::AsModuleRequest() const {
  MOZ_ASSERT(IsModuleRequest());
  return static_cast<const ModuleLoadRequest*>(this);
}

void ScriptLoadRequest::CacheEntryFound(LoadedScript* aLoadedScript,
                                        ScriptFetchOptions* aFetchOptions) {
  MOZ_ASSERT(IsCheckingCache());

  SetCacheEntry(aLoadedScript, aFetchOptions);
}

void ScriptLoadRequest::CacheEntryRevived(LoadedScript* aLoadedScript) {
  MOZ_ASSERT(IsFetching());

  SetCacheEntry(aLoadedScript, FetchOptions());

  // NOTE: The caller should keep using the "fetching" path, with the
  //       cached stencil, and skip the compilation.
  mState = State::Fetching;
}

void ScriptLoadRequest::SetCacheEntry(LoadedScript* aLoadedScript,
                                      ScriptFetchOptions* aFetchOptions) {
  SetStencil(aLoadedScript->GetCachedStencil());

  mFetchInfo =
      new ScriptFetchInfo(mKind, aLoadedScript->CachedReferrerPolicy(),
                          aFetchOptions, aLoadedScript->CachedBaseURL());

  MOZ_ASSERT(!IsRetrievedFromMemoryCache());
  mIsRetrievedFromMemoryCache = true;

  switch (mKind) {
    case ScriptKind::eClassic:
      MOZ_ASSERT(aLoadedScript->IsClassicScript());

      mLoadedScript = aLoadedScript;

      mState = State::DelayingReady;
      break;
    case ScriptKind::eImportMap:
      MOZ_ASSERT(aLoadedScript->IsImportMapScript());

      mLoadedScript = aLoadedScript;

      mState = State::DelayingReady;
      break;
    case ScriptKind::eSpeculationRules:
      MOZ_ASSERT(aLoadedScript->IsSpeculationRulesScript());

      mLoadedScript = aLoadedScript;

      mState = State::Ready;
      break;
    case ScriptKind::eModule:
      MOZ_ASSERT(aLoadedScript->IsModuleScript());

      mLoadedScript = aLoadedScript;

      // Modules need to wait for fetching dependencies before setting to
      // Ready.
      mState = State::Fetching;
      break;
    case ScriptKind::eEvent:
      MOZ_ASSERT_UNREACHABLE("eEvent is only for ScriptFetchInfo");
      break;
  }
}

void ScriptLoadRequest::NoCacheEntryFound(
    mozilla::dom::ReferrerPolicy aReferrerPolicy,
    ScriptFetchOptions* aFetchOptions, nsIURI* aURI) {
  MOZ_ASSERT(IsCheckingCache());
  MOZ_ASSERT(mKind != ScriptKind::eEvent, "eEvent is only for ScriptFetchInfo");
  MOZ_ASSERT(!IsRetrievedFromMemoryCache());

  mFetchInfo = new ScriptFetchInfo(mKind, aReferrerPolicy, aFetchOptions, aURI);
  mLoadedScript = new LoadedScript(mKind, aURI);
  mState = State::Fetching;
}

}  // namespace JS::loader
