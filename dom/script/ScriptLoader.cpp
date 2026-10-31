/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptLoader.h"

#include "GeckoProfiler.h"
#include "ModuleLoader.h"
#include "ReferrerInfo.h"
#include "ScriptCompression.h"
#include "ScriptLoadHandler.h"
#include "ScriptTrace.h"
#include "SharedScriptCache.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/CompilationAndEvaluation.h"
#include "js/CompileOptions.h"  // JS::CompileOptions, JS::OwningCompileOptions, JS::DecodeOptions, JS::OwningDecodeOptions, JS::DelazificationOption
#include "js/ContextOptions.h"  // JS::ContextOptionsRef
#include "js/MemoryFunctions.h"
#include "js/Modules.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/Transcoding.h"  // JS::TranscodeRange, JS::TranscodeResult, JS::IsTranscodeFailureResult
#include "js/Utility.h"
#include "js/experimental/CompileScript.h"  // JS::FrontendContext, JS::NewFrontendContext, JS::DestroyFrontendContext, JS::SetNativeStackQuota, JS::ThreadStackQuotaForSize, JS::CompilationStorage, JS::CompileGlobalScriptToStencil, JS::CompileModuleScriptToStencil, JS::DecodeStencil, JS::PrepareForInstantiate, JS::AllowCancellingCompilation, JS::RequestFrontendCompilationCancellation
#include "js/experimental/JSStencil.h"  // JS::Stencil, JS::InstantiationStorage, JS::StartCollectingDelazifications, JS::IsStencilCacheable
#include "js/loader/LoadedScript.h"
#include "js/loader/ModuleLoadRequest.h"
#include "js/loader/ModuleLoaderBase.h"
#include "js/loader/ScriptLoadRequest.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ConsoleReportCollector.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/Encoding.h"
#include "mozilla/EventQueue.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"  // mozilla::Mutex
#include "mozilla/PreloadHashKey.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskController.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/DocumentInlines.h"  // Document::GetPresContext
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/SpeculationRules.h"
#ifdef NIGHTLY_BUILD
#  include "mozilla/dom/IntegrityPolicyWAICT.h"
#endif
#include "mozilla/FlowMarkers.h"
#include "mozilla/dom/JSExecutionUtils.h"  // mozilla::dom::Compile, mozilla::dom::InstantiateStencil, mozilla::dom::EvaluationExceptionToNSResult
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/SRILogHelper.h"
#include "mozilla/dom/ScriptDecoding.h"  // mozilla::dom::ScriptDecoding
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SpeculationRuleSet.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "nsAboutProtocolUtils.h"
#include "nsCRT.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionParticipant.h"
#include "nsError.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIAsyncOutputStream.h"
#include "nsICacheInfoChannel.h"
#include "nsIClassOfService.h"
#include "nsIClassifiedChannel.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIDocShell.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIPrincipal.h"
#include "nsIScriptContext.h"
#include "nsIScriptElement.h"
#include "nsIScriptError.h"
#include "nsIScriptGlobalObject.h"
#include "nsISupportsPriority.h"
#include "nsITimedChannel.h"
#include "nsITimer.h"
#include "nsJSPrincipals.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsPresContext.h"  // nsPresContext
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsThreadUtils.h"
#include "nsUnicharUtils.h"
#include "prsystem.h"
#include "xpcpublic.h"

using namespace JS::loader;

namespace mozilla::dom {

LazyLogModule ScriptLoader::gCspPRLog("CSP");
LazyLogModule ScriptLoader::gScriptLoaderLog("ScriptLoader");

#undef LOG
#define LOG(args) \
  MOZ_LOG(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug, args)

#define LOG_ENABLED() \
  MOZ_LOG_TEST(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug)

// Alternate Data MIME type used by the ScriptLoader to register that we want to
// store the disk cache without reading it.
static constexpr auto kNullMimeType = "javascript/null"_ns;

/////////////////////////////////////////////////////////////
// ShutdownAndMemoryPressureObserver
/////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS(ShutdownAndMemoryPressureObserver, nsIObserver)

void ShutdownAndMemoryPressureObserver::OnShutdown() {
  if (mScriptLoader) {
    mScriptLoader->Destroy();
    MOZ_ASSERT(!mScriptLoader);
  }
}

void ShutdownAndMemoryPressureObserver::OnMemoryPressure() {
  if (mScriptLoader) {
    mScriptLoader->OnMemoryPressure();
  }
}

void ShutdownAndMemoryPressureObserver::Unregister() {
  if (mScriptLoader) {
    mScriptLoader = nullptr;
    nsContentUtils::UnregisterShutdownObserver(this);

    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    if (obsService) {
      obsService->RemoveObserver(this, "memory-pressure");
    }
  }
}

NS_IMETHODIMP
ShutdownAndMemoryPressureObserver::Observe(nsISupports* aSubject,
                                           const char* aTopic,
                                           const char16_t* aData) {
  if (strcmp(aTopic, "xpcom-shutdown") == 0) {
    OnShutdown();
    return NS_OK;
  }

  if (strcmp(aTopic, "memory-pressure") == 0) {
    OnMemoryPressure();
    return NS_OK;
  }

  return NS_OK;
}

//////////////////////////////////////////////////////////////
// ScriptLoader::PreloadInfo
//////////////////////////////////////////////////////////////

inline void ImplCycleCollectionUnlink(ScriptLoader::PreloadInfo& aField) {
  ImplCycleCollectionUnlink(aField.mRequest);
}

inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    ScriptLoader::PreloadInfo& aField, const char* aName, uint32_t aFlags = 0) {
  ImplCycleCollectionTraverse(aCallback, aField.mRequest, aName, aFlags);
}

//////////////////////////////////////////////////////////////
// ScriptLoader
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ScriptLoader)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(ScriptLoader)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ScriptLoader)
  if (tmp->mDocument) {
    tmp->DropDocumentReference();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mNonAsyncExternalScriptInsertedRequests, mLoadingAsyncRequests,
      mLoadedAsyncRequests, mDeferRequests, mXSLTRequests,
      mParserBlockingRequest, mOffThreadCompilingRequests,
      mDiskCacheableDependencyModules, mDiskCacheQueue, mPreloads,
      mPendingChildLoaders, mModuleLoader, mWebExtModuleLoaders)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ScriptLoader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mNonAsyncExternalScriptInsertedRequests, mLoadingAsyncRequests,
      mLoadedAsyncRequests, mDeferRequests, mXSLTRequests,
      mParserBlockingRequest, mOffThreadCompilingRequests,
      mDiskCacheableDependencyModules, mDiskCacheQueue, mPreloads,
      mPendingChildLoaders, mModuleLoader, mWebExtModuleLoaders)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(ScriptLoader)
  for (size_t i = 0; i < tmp->mDelazificationCollectingScripts.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(
        mDelazificationCollectingScripts[i])
  }
  for (size_t i = 0; i < tmp->mDelazificationCollectingModules.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(
        mDelazificationCollectingModules[i])
  }
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ScriptLoader)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ScriptLoader)

ScriptLoader::ScriptLoader(Document* aDocument)
    : mDocument(aDocument), mReporter(new ConsoleReportCollector()) {
  LOG(("ScriptLoader::ScriptLoader %p", this));

  mSpeculativeOMTParsingEnabled = StaticPrefs::
      dom_script_loader_external_scripts_speculative_omt_parse_enabled();

  // NOTE: The loader for the system principal aren't supposed to
  //       load remote contents, and it doesn't have to use the in-memory cache.
  //       A non-system-principal document can also load internal resources,
  //       and those cases should be filtered out by
  //       ScriptLoader::GetCacheBehavior.
  if (!LoaderPrincipal()->IsSystemPrincipal() &&
      StaticPrefs::dom_script_loader_experimental_navigation_cache()) {
    mCache = SharedScriptCache::Get();
    RegisterToCache();
    LOG(("ScriptLoader (%p): Using in-memory cache.", this));
  }

  mObserver = new ShutdownAndMemoryPressureObserver(this);
  nsContentUtils::RegisterShutdownObserver(mObserver);

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->AddObserver(mObserver, "memory-pressure", false);
  }
}

ScriptLoader::~ScriptLoader() {
  LOG(("ScriptLoader::~ScriptLoader %p", this));

  if (!mDelazificationCollectingScripts.IsEmpty() ||
      !mDelazificationCollectingModules.IsEmpty()) {
    mDelazificationCollectingScripts.Clear();
    mDelazificationCollectingModules.Clear();

    mozilla::DropJSObjects(this);
  }

  mObservers.Clear();

  if (mParserBlockingRequest) {
    const RefPtr<ScriptLoadRequest> parserBlockRequest = mParserBlockingRequest;
    FireScriptAvailable(NS_ERROR_ABORT, parserBlockRequest);
  }

  for (RefPtr<ScriptLoadRequest> req = mXSLTRequests.getFirst(); req;
       req = req->getNext()) {
    FireScriptAvailable(NS_ERROR_ABORT, req);
  }

  for (RefPtr<ScriptLoadRequest> req = mDeferRequests.getFirst(); req;
       req = req->getNext()) {
    FireScriptAvailable(NS_ERROR_ABORT, req);
  }

  for (RefPtr<ScriptLoadRequest> req = mLoadingAsyncRequests.getFirst(); req;
       req = req->getNext()) {
    FireScriptAvailable(NS_ERROR_ABORT, req);
  }

  for (RefPtr<ScriptLoadRequest> req = mLoadedAsyncRequests.getFirst(); req;
       req = req->getNext()) {
    FireScriptAvailable(NS_ERROR_ABORT, req);
  }

  for (RefPtr<ScriptLoadRequest> req =
           mNonAsyncExternalScriptInsertedRequests.getFirst();
       req; req = req->getNext()) {
    FireScriptAvailable(NS_ERROR_ABORT, req);
  }

  // Unblock the kids, in case any of them moved to a different document
  // subtree in the meantime and therefore aren't actually going away.
  for (uint32_t j = 0; j < mPendingChildLoaders.Length(); ++j) {
    mPendingChildLoaders[j]->RemoveParserBlockingScriptExecutionBlocker();
  }

  if (mObserver) {
    mObserver->Unregister();
    mObserver = nullptr;
  }

  mModuleLoader = nullptr;

  if (mProcessPendingRequestsAsyncBypassParserBlocking) {
    mProcessPendingRequestsAsyncBypassParserBlocking->Cancel();
  }
}

void ScriptLoader::SetGlobalObject(nsIGlobalObject* aGlobalObject) {
  if (!aGlobalObject) {
    // The document is being detached.
    CancelAndClearScriptLoadRequests();
    return;
  }

  MOZ_ASSERT(!HasPendingRequests());

  if (!mModuleLoader) {
    // The module loader is associated with a global object, so don't create it
    // until we have a global set.
    mModuleLoader = new ModuleLoader(this, aGlobalObject, ModuleLoader::Normal);
  }

  MOZ_ASSERT(mModuleLoader->GetGlobalObject() == aGlobalObject);
  MOZ_ASSERT(aGlobalObject->GetModuleLoader(dom::danger::GetJSContext()) ==
             mModuleLoader);
}

void ScriptLoader::DropDocumentReference() {
  if (mDocument && mCache) {
    DeregisterFromCache();
  }

  mDocument = nullptr;
}

void ScriptLoader::RegisterToCache() {
  if (mCache) {
    MOZ_ASSERT(mDocument);
    mCache->RegisterLoader(*this);
  }
}

void ScriptLoader::DeregisterFromCache() {
  if (mCache) {
    MOZ_ASSERT(mDocument);
    mCache->CancelLoadsForLoader(*this);
    mCache->UnregisterLoader(*this);
  }
}

nsIPrincipal* ScriptLoader::LoaderPrincipal() const {
  return mDocument->NodePrincipal();
}

nsIPrincipal* ScriptLoader::PartitionedPrincipal() const {
  return mDocument->PartitionedPrincipal();
}

bool ScriptLoader::ShouldBypassCache() const {
  return mDocument && nsContentUtils::ShouldBypassSubResourceCache(mDocument);
}

#ifdef NIGHTLY_BUILD
bool ScriptLoader::WAICTHandlesScripts() const {
  if (!mDocument) {
    return false;
  }
  auto* policy =
      PolicyContainer::GetIntegrityPolicyWAICT(mDocument->GetPolicyContainer());
  return policy &&
         policy->ShouldHandle(IntegrityPolicy::DestinationType::Script);
}
#endif

void ScriptLoader::RegisterContentScriptModuleLoader(ModuleLoader* aLoader) {
  MOZ_ASSERT(aLoader);
  MOZ_ASSERT(aLoader->GetScriptLoader() == this);

  mWebExtModuleLoaders.AppendElement(aLoader);
}

// Collect telemtry data about the cache information, and the kind of source
// which are being loaded, and where it is being loaded from.
static void CollectScriptTelemetry(ScriptLoadRequest* aRequest) {
  using namespace mozilla::glean::dom;

  MOZ_ASSERT(aRequest->IsFetching());

  // Skip this function if we are not running telemetry.
  if (!mozilla::Telemetry::CanRecordExtended()) {
    return;
  }

  if (aRequest->IsRetrievedFromMemoryCache()) {
    // This is revived from the cache.
    return;
  }

  // Report the type of source. This is used to monitor the status of the
  // JavaScript Start-up Bytecode Cache, with the expectation of an almost zero
  // source-fallback and alternate-data being roughtly equal to source loads.
  if (aRequest->mFetchSourceOnly) {
    if (aRequest->GetScriptLoadContext()->mIsInline) {
      script_loading_source.EnumGet(ScriptLoadingSourceLabel::eInline).Add();
    } else if (aRequest->IsFetchedAsTextSource()) {
      script_loading_source.EnumGet(ScriptLoadingSourceLabel::eSourcefallback)
          .Add();
    }
  } else {
    if (aRequest->IsFetchedAsTextSource()) {
      script_loading_source.EnumGet(ScriptLoadingSourceLabel::eSource).Add();
    } else if (aRequest->IsRetrievedAsSerializedStencil()) {
      script_loading_source.EnumGet(ScriptLoadingSourceLabel::eAltdata).Add();
    }
  }
}

static void AddMemoryCacheRefCountTelemetry(
    JS::loader::LoadedScript* aLoadedScript) {
  using namespace mozilla::glean::dom;

  // Skip this function if we are not running telemetry.
  if (!mozilla::Telemetry::CanRecordExtended()) {
    return;
  }

  uint16_t count = aLoadedScript->ClampedRefCountForTelemetry();
  script_memory_cache_ref_count.AccumulateSingleSample(count);
}

// Helper method for checking if the script element is an event-handler
// This means that it has both a for-attribute and a event-attribute.
// Also, if the for-attribute has a value that matches "\s*window\s*",
// and the event-attribute matches "\s*onload([ \(].*)?" then it isn't an
// eventhandler. (both matches are case insensitive).
// This is how IE seems to filter out a window's onload handler from a
// <script for=... event=...> element.

static bool IsScriptEventHandler(ScriptKind kind, nsIContent* aScriptElement) {
  if (kind != ScriptKind::eClassic) {
    return false;
  }

  if (!aScriptElement->IsHTMLElement()) {
    return false;
  }

  nsAutoString forAttr, eventAttr;
  if (!aScriptElement->AsElement()->GetAttr(nsGkAtoms::_for, forAttr) ||
      !aScriptElement->AsElement()->GetAttr(nsGkAtoms::event, eventAttr)) {
    return false;
  }

  const nsAString& for_str =
      nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(forAttr);
  if (!for_str.LowerCaseEqualsLiteral("window")) {
    return true;
  }

  // We found for="window", now check for event="onload".
  const nsAString& event_str =
      nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(
          eventAttr);
  if (!event_str.LowerCaseEqualsLiteral("onload") &&
      !event_str.LowerCaseEqualsLiteral("onload()")) {
    return true;
  }

  // If the `for` attribute has the value "window" and the `event` attribute is
  // either "onload" or "onload()", then it isn't an event handler.
  return false;
}

nsContentPolicyType ScriptLoadRequestToContentPolicyType(
    ScriptLoadRequest* aRequest) {
  if (aRequest->GetScriptLoadContext()->IsPreload()) {
    if (aRequest->IsModuleRequest()) {
      switch (aRequest->AsModuleRequest()->mModuleType) {
        case JS::ModuleType::JavaScript:
          return nsIContentPolicy::TYPE_INTERNAL_MODULE_PRELOAD;
        case JS::ModuleType::JSON:
          return nsIContentPolicy::TYPE_INTERNAL_JSON_PRELOAD;
        case JS::ModuleType::CSS:
          return nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD;
        case JS::ModuleType::Text:
          return nsIContentPolicy::TYPE_INTERNAL_TEXT_PRELOAD;
        case JS::ModuleType::Bytes:
        case JS::ModuleType::Unknown:
          MOZ_ASSERT_UNREACHABLE("Unknown module type");
      }
    }

    return nsIContentPolicy::TYPE_INTERNAL_SCRIPT_PRELOAD;
  }

  if (aRequest->IsModuleRequest()) {
    switch (aRequest->AsModuleRequest()->mModuleType) {
      case JS::ModuleType::Unknown:
      case JS::ModuleType::Bytes:
        MOZ_CRASH("Unexpected module type");
      case JS::ModuleType::JavaScript:
        return nsIContentPolicy::TYPE_INTERNAL_MODULE;
      case JS::ModuleType::JSON:
        return nsIContentPolicy::TYPE_JSON;
      case JS::ModuleType::CSS:
        return nsIContentPolicy::TYPE_STYLESHEET;
      case JS::ModuleType::Text:
        return nsIContentPolicy::TYPE_TEXT;
    }
  }

  return nsIContentPolicy::TYPE_INTERNAL_SCRIPT;
}

RequestMode ComputeRequestModeForContentPolicy(
    const ScriptLoadRequest* aRequest, ScriptFetchOptions* aFetchOptions) {
  auto corsMapping =
      aRequest->IsModuleRequest()
          ? nsContentSecurityManager::REQUIRE_CORS_CHECKS
          : nsContentSecurityManager::CORS_NONE_MAPS_TO_DISABLED_CORS_CHECKS;
  return nsContentSecurityManager::SecurityModeToRequestMode(
      nsContentSecurityManager::ComputeSecurityMode(
          nsContentSecurityManager::ComputeSecurityFlags(
              aFetchOptions->mCORSMode, corsMapping)));
}

nsresult ScriptLoader::CheckContentPolicy(nsIScriptElement* aElement,
                                          const nsAString& aNonce,
                                          ScriptLoadRequest* aRequest,
                                          ScriptFetchOptions* aFetchOptions,
                                          nsIURI* aURI) {
  MOZ_ASSERT(aRequest);
  MOZ_ASSERT(aFetchOptions);
  MOZ_ASSERT(aURI);

  nsContentPolicyType contentPolicyType =
      ScriptLoadRequestToContentPolicyType(aRequest);

  nsCOMPtr<nsINode> requestingNode = do_QueryInterface(aElement);
  if (!requestingNode) {
    MOZ_ASSERT(aRequest->IsModuleRequest());
    requestingNode = mDocument;
  }
  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = MOZ_TRY(net::LoadInfo::Create(
      mDocument->NodePrincipal(),  // loading principal
      mDocument->NodePrincipal(),  // triggering principal
      requestingNode, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
      contentPolicyType));
  secCheckLoadInfo->SetParserCreatedScript(aElement &&
                                           aElement->GetParserCreated() !=
                                               mozilla::dom::NOT_FROM_PARSER);
  Maybe<RequestMode> requestMode =
      Some(ComputeRequestModeForContentPolicy(aRequest, aFetchOptions));
  secCheckLoadInfo->SetRequestMode(requestMode);
  // Use nonce of the current element, instead of the preload, because those
  // are allowed to differ.
  secCheckLoadInfo->SetCspNonce(aNonce);
  secCheckLoadInfo->SetIntegrityMetadata(
      aRequest->mIntegrity.GetIntegrityString());

  int16_t shouldLoad = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentLoadPolicy(aURI, secCheckLoadInfo, &shouldLoad,
                                          nsContentUtils::GetContentPolicy());
  if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) {
    if (NS_FAILED(rv) || shouldLoad != nsIContentPolicy::REJECT_TYPE) {
      return NS_ERROR_CONTENT_BLOCKED;
    }
    return NS_ERROR_CONTENT_BLOCKED_SHOW_ALT;
  }

  return NS_OK;
}

/* static */
bool ScriptLoader::IsAboutPageLoadingChromeURI(ScriptLoadRequest* aRequest,
                                               Document* aDocument) {
  // if the uri to be loaded is not of scheme chrome:, there is nothing to do.
  if (!aRequest->URI()->SchemeIs("chrome")) {
    return false;
  }

  // we can either get here with a regular contentPrincipal or with a
  // NullPrincipal in case we are showing an error page in a sandboxed iframe.
  // In either case if the about: page is linkable from content, there is
  // nothing to do.
  uint32_t aboutModuleFlags = 0;
  nsresult rv = NS_OK;

  nsCOMPtr<nsIPrincipal> triggeringPrincipal = aRequest->TriggeringPrincipal();
  if (triggeringPrincipal->GetIsContentPrincipal()) {
    if (!triggeringPrincipal->SchemeIs("about")) {
      return false;
    }
    rv = triggeringPrincipal->GetAboutModuleFlags(&aboutModuleFlags);
    NS_ENSURE_SUCCESS(rv, false);
  } else if (triggeringPrincipal->GetIsNullPrincipal()) {
    nsCOMPtr<nsIURI> docURI = aDocument->GetDocumentURI();
    if (!docURI->SchemeIs("about")) {
      return false;
    }

    nsCOMPtr<nsIAboutModule> aboutModule;
    rv = NS_GetAboutModule(docURI, getter_AddRefs(aboutModule));
    if (NS_FAILED(rv) || !aboutModule) {
      return false;
    }
    rv = aboutModule->GetURIFlags(docURI, &aboutModuleFlags);
    NS_ENSURE_SUCCESS(rv, false);
  } else {
    return false;
  }

  if (aboutModuleFlags & nsIAboutModule::MAKE_LINKABLE) {
    return false;
  }

  // seems like an about page wants to load a chrome URI.
  return true;
}

nsIURI* ScriptLoader::GetBaseURI() const {
  MOZ_ASSERT(mDocument);
  return mDocument->GetDocBaseURI();
}

class ScriptRequestProcessor : public Runnable {
 private:
  MOZ_KNOWN_LIVE const RefPtr<ScriptLoader> mLoader;
  MOZ_KNOWN_LIVE const RefPtr<ScriptLoadRequest> mRequest;

 public:
  ScriptRequestProcessor(ScriptLoader* aLoader, ScriptLoadRequest* aRequest)
      : Runnable("dom::ScriptRequestProcessor"),
        mLoader(aLoader),
        mRequest(aRequest) {}
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    return mLoader->ProcessRequest(mRequest);
  }
};

void ScriptLoader::RunScriptWhenSafe(ScriptLoadRequest* aRequest) {
  auto* runnable = new ScriptRequestProcessor(this, aRequest);
  nsContentUtils::AddScriptRunner(runnable);
}

nsresult ScriptLoader::RestartLoad(ScriptLoadRequest* aRequest) {
  aRequest->getLoadedScript()->DropSRIOrSRIAndSerializedStencil();
  TRACE_FOR_TEST(aRequest, "load:fallback");

  // Notify preload restart so that we can register this preload request again.
  aRequest->GetScriptLoadContext()->NotifyRestart(mDocument);

  // Start a new channel from which we explicitly request to stream the source
  // instead of the serialized stencil.
  aRequest->mFetchSourceOnly = true;
  nsresult rv;
  if (aRequest->IsModuleRequest()) {
    rv = aRequest->AsModuleRequest()->RestartModuleLoad();
  } else {
    rv = StartLoad(aRequest);
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Close the current channel and this ScriptLoadHandler as we created a new
  // one for the same request.
  return NS_BINDING_RETARGETED;
}

nsresult ScriptLoader::StartLoad(ScriptLoadRequest* aRequest) {
  if (aRequest->IsModuleRequest()) {
    return aRequest->AsModuleRequest()->StartModuleLoad();
  }

  return StartClassicLoad(aRequest);
}

static nsSecurityFlags CORSModeToSecurityFlags(CORSMode aCORSMode) {
  nsSecurityFlags securityFlags =
      nsContentSecurityManager::ComputeSecurityFlags(
          aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                         CORS_NONE_MAPS_TO_DISABLED_CORS_CHECKS);

  securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

  return securityFlags;
}

void ScriptLoader::OnDelayedReady(ScriptLoadRequest* aRequest) {
  if (!mDocument) {
    return;
  }

  if (aRequest->IsCanceled()) {
    return;
  }

  MOZ_ASSERT(aRequest->IsRetrievedFromMemoryCache());
  MOZ_ASSERT(aRequest->IsDelayingReady());

  aRequest->SetReady();
  MaybeMoveToLoadedList(aRequest);
  ProcessPendingRequests();
}

nsresult ScriptLoader::StartClassicLoad(ScriptLoadRequest* aRequest) {
  if (aRequest->IsRetrievedFromMemoryCache()) {
    // NOTE: The network event need to be dispatched in the current call stack,
    //       in order to reflect it in the DevTools Network Monitor.
    EmulateNetworkEvents(aRequest);

    nsCOMPtr<nsIRunnable> runnable =
        mozilla::NewRunnableMethod<RefPtr<ScriptLoadRequest>>(
            "ScriptLoader::OnDelayedReady", this, &ScriptLoader::OnDelayedReady,
            aRequest);
    mDocument->Dispatch(runnable.forget());
    return NS_OK;
  }

  MOZ_ASSERT(aRequest->IsFetching());
  NS_ENSURE_TRUE(mDocument, NS_ERROR_NULL_POINTER);
  aRequest->SetUnknownDataType();

  // If this document is sandboxed without 'allow-scripts', abort.
  if (mDocument->HasScriptsBlockedBySandbox()) {
    return NS_OK;
  }

  if (LOG_ENABLED()) {
    nsAutoCString url;
    aRequest->URI()->GetAsciiSpec(url);
    LOG(("ScriptLoadRequest (%p): Start Classic Load (url = %s)", aRequest,
         url.get()));
  }

  nsSecurityFlags securityFlags = CORSModeToSecurityFlags(aRequest->CORSMode());

  nsresult rv = StartLoadInternal(aRequest, securityFlags);

  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

static bool IsWebExtensionRequest(ScriptLoadRequest* aRequest) {
  if (!aRequest->IsModuleRequest()) {
    return false;
  }

  ModuleLoader* loader =
      ModuleLoader::From(aRequest->AsModuleRequest()->mLoader);
  return loader->GetKind() == ModuleLoader::WebExtension;
}

static nsresult CreateChannelForScriptLoading(
    nsIChannel** aOutChannel, Document* aDocument, nsIURI* aURI,
    nsINode* aContext, nsIPrincipal* aTriggeringPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType) {
  nsCOMPtr<nsILoadGroup> loadGroup = aDocument->GetDocumentLoadGroup();
  nsCOMPtr<nsPIDOMWindowOuter> window = aDocument->GetWindow();
  NS_ENSURE_TRUE(window, NS_ERROR_NULL_POINTER);
  nsIDocShell* docshell = window->GetDocShell();
  nsCOMPtr<nsIInterfaceRequestor> prompter(do_QueryInterface(docshell));

  return NS_NewChannelWithTriggeringPrincipal(
      aOutChannel, aURI, aContext, aTriggeringPrincipal, aSecurityFlags,
      aContentPolicyType,
      /* aPerformanceStorage = */ nullptr, loadGroup, prompter);
}

static nsresult CreateChannelForScriptLoading(nsIChannel** aOutChannel,
                                              Document* aDocument,
                                              ScriptLoadRequest* aRequest,
                                              nsSecurityFlags aSecurityFlags) {
  nsContentPolicyType contentPolicyType =
      ScriptLoadRequestToContentPolicyType(aRequest);
  nsCOMPtr<nsINode> context;
  if (aRequest->GetScriptLoadContext()->HasScriptElement()) {
    context = do_QueryInterface(
        aRequest->GetScriptLoadContext()->GetScriptElementForLoadingNode());
  } else {
    context = aDocument;
  }

  return CreateChannelForScriptLoading(aOutChannel, aDocument, aRequest->URI(),
                                       context, aRequest->TriggeringPrincipal(),
                                       aSecurityFlags, contentPolicyType);
}

static void PrepareLoadInfoForScriptLoading(nsIChannel* aChannel,
                                            const ScriptLoadRequest* aRequest) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  loadInfo->SetParserCreatedScript(aRequest->ParserMetadata() ==
                                   ParserMetadata::ParserInserted);
  loadInfo->SetCspNonce(aRequest->Nonce());
  loadInfo->SetIntegrityMetadata(aRequest->mIntegrity.GetIntegrityString());
}

// static
void ScriptLoader::PrepareCacheInfoChannel(nsIChannel* aChannel,
                                           ScriptLoadRequest* aRequest) {
  // To avoid decoding issues, the build-id is part of the disk cache MIME type
  // constant.
  aRequest->getLoadedScript()->DropDiskCacheReference();
  nsCOMPtr<nsICacheInfoChannel> cic(do_QueryInterface(aChannel));
  if (cic && StaticPrefs::dom_script_loader_bytecode_cache_enabled()) {
    MOZ_ASSERT(!IsWebExtensionRequest(aRequest),
               "Web extension scripts are not compatible with the disk cache");
    if (!aRequest->mFetchSourceOnly) {
      // Inform the HTTP cache that we prefer to have information coming from
      // the serialized stencil disk cache instead of the sources, if such entry
      // is already registered.
      LOG(("ScriptLoadRequest (%p): Maybe request the disk cache", aRequest));
      cic->PreferAlternativeDataType(
          ScriptLoader::BytecodeMimeTypeFor(aRequest), ""_ns,
          nsICacheInfoChannel::PreferredAlternativeDataDeliveryType::ASYNC);
    } else {
      // If we are explicitly loading from the sources, such as after a
      // restarted request, we might still want to save to the disk cache after.
      //
      // The following tell the cache to look for an alternative data type which
      // does not exist, such that we can later save the serialized Stencil
      // with a different alternative data type.
      LOG(("ScriptLoadRequest (%p): Request saving to the disk cache later",
           aRequest));
      cic->PreferAlternativeDataType(
          kNullMimeType, ""_ns,
          nsICacheInfoChannel::PreferredAlternativeDataDeliveryType::ASYNC);
    }
  }
}

static void AdjustPriorityAndClassOfServiceForLinkPreloadScripts(
    nsIChannel* aChannel, ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->GetScriptLoadContext()->IsLinkPreloadScript());

  // Put it to the group that is not blocked by leaders and doesn't block
  // follower at the same time.
  // Giving it a much higher priority will make this request be processed
  // ahead of other Unblocked requests, but with the same weight as
  // Leaders. This will make us behave similar way for both http2 and http1.
  ScriptLoadContext::PrioritizeAsPreload(aChannel);

  if (!StaticPrefs::network_fetchpriority_enabled()) {
    return;
  }

  const auto fetchPriority = ToFetchPriority(aRequest->FetchPriority());
  if (nsCOMPtr<nsISupportsPriority> supportsPriority =
          do_QueryInterface(aChannel)) {
    LOG(("Is <link rel=[module]preload"));

    // The spec defines the priority to be set in an implementation defined
    // manner (<https://fetch.spec.whatwg.org/#concept-fetch>, step 15 and
    // <https://html.spec.whatwg.org/#concept-script-fetch-options-fetch-priority>).
    // See corresponding preferences in StaticPrefList.yaml for more context.
    const int32_t supportsPriorityDelta =
        FETCH_PRIORITY_ADJUSTMENT_FOR(link_preload_script, fetchPriority);
    supportsPriority->AdjustPriority(supportsPriorityDelta);
#ifdef DEBUG
    int32_t adjustedPriority;
    supportsPriority->GetPriority(&adjustedPriority);
    LogPriorityMapping(ScriptLoader::gScriptLoaderLog, fetchPriority,
                       adjustedPriority);
#endif
  }

  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    cos->SetFetchPriorityDOM(fetchPriority);
  }
}

void AdjustPriorityForNonLinkPreloadScripts(nsIChannel* aChannel,
                                            ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->IsLinkPreloadScript());

  if (!StaticPrefs::network_fetchpriority_enabled()) {
    return;
  }

  const auto fetchPriority = ToFetchPriority(aRequest->FetchPriority());
  if (nsCOMPtr<nsISupportsPriority> supportsPriority =
          do_QueryInterface(aChannel)) {
    LOG(("Is not <link rel=[module]preload"));

    // The spec defines the priority to be set in an implementation defined
    // manner (<https://fetch.spec.whatwg.org/#concept-fetch>, step 15 and
    // <https://html.spec.whatwg.org/#concept-script-fetch-options-fetch-priority>).
    // See corresponding preferences in StaticPrefList.yaml for more context.
    const int32_t supportsPriorityDelta = [&]() {
      const ScriptLoadContext* scriptLoadContext =
          aRequest->GetScriptLoadContext();
      if (aRequest->IsModuleRequest()) {
        return FETCH_PRIORITY_ADJUSTMENT_FOR(module_script, fetchPriority);
      }

      if (scriptLoadContext->IsAsyncScript() ||
          scriptLoadContext->IsDeferredScript()) {
        return FETCH_PRIORITY_ADJUSTMENT_FOR(async_or_defer_script,
                                             fetchPriority);
      }

      if (scriptLoadContext->mScriptFromHead) {
        return FETCH_PRIORITY_ADJUSTMENT_FOR(script_in_head, fetchPriority);
      }

      return FETCH_PRIORITY_ADJUSTMENT_FOR(other_script, fetchPriority);
    }();

    if (supportsPriorityDelta) {
      supportsPriority->AdjustPriority(supportsPriorityDelta);
#ifdef DEBUG
      int32_t adjustedPriority;
      supportsPriority->GetPriority(&adjustedPriority);
      LogPriorityMapping(ScriptLoader::gScriptLoaderLog, fetchPriority,
                         adjustedPriority);
#endif
    }
  }
  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    cos->SetFetchPriorityDOM(fetchPriority);
  }
}

// static
void ScriptLoader::PrepareRequestPriorityAndRequestDependencies(
    nsIChannel* aChannel, ScriptLoadRequest* aRequest) {
  if (aRequest->GetScriptLoadContext()->IsLinkPreloadScript()) {
    // This is <link rel="preload" as="script"> or <link rel="modulepreload">
    // initiated speculative load
    // (https://developer.mozilla.org/en-US/docs/Web/Performance/Speculative_loading).
    AdjustPriorityAndClassOfServiceForLinkPreloadScripts(aChannel, aRequest);
    ScriptLoadContext::AddLoadBackgroundFlag(aChannel);
  } else if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    AdjustPriorityForNonLinkPreloadScripts(aChannel, aRequest);

    if (aRequest->GetScriptLoadContext()->mScriptFromHead &&
        aRequest->GetScriptLoadContext()->IsBlockingScript()) {
      // synchronous head scripts block loading of most other non js/css
      // content such as images, Leader implicitely disallows tailing
      cos->AddClassFlags(nsIClassOfService::Leader);
    } else if (aRequest->GetScriptLoadContext()->IsDeferredScript() &&
               !StaticPrefs::network_http_tailing_enabled()) {
      // Bug 1395525 and the !StaticPrefs::network_http_tailing_enabled() bit:
      // We want to make sure that turing tailing off by the pref makes the
      // browser behave exactly the same way as before landing the tailing
      // patch.

      // head/body deferred scripts are blocked by leaders but are not
      // allowed tailing because they block DOMContentLoaded
      cos->AddClassFlags(nsIClassOfService::TailForbidden);
    } else {
      // other scripts (=body sync or head/body async) are neither blocked
      // nor prioritized
      cos->AddClassFlags(nsIClassOfService::Unblocked);

      if (aRequest->GetScriptLoadContext()->IsAsyncScript()) {
        // async scripts are allowed tailing, since those and only those
        // don't block DOMContentLoaded; this flag doesn't enforce tailing,
        // just overweights the Unblocked flag when the channel is found
        // to be a thrird-party tracker and thus set the Tail flag to engage
        // tailing.
        cos->AddClassFlags(nsIClassOfService::TailAllowed);
      }
    }
  }
}

inline nsLiteralString GetInitiatorType(ScriptLoadRequest* aRequest) {
  if (aRequest->mEarlyHintPreloaderId) {
    return u"early-hints"_ns;
  }

  if (aRequest->GetScriptLoadContext()->IsLinkPreloadScript()) {
    return u"link"_ns;
  }

  return u"script"_ns;
}

// static
nsresult ScriptLoader::PrepareHttpRequestAndInitiatorType(
    nsIChannel* aChannel, ScriptLoadRequest* aRequest) {
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aChannel));
  nsresult rv = NS_OK;

  if (httpChannel) {
    // The 'Accept' HTTP header should be set in
    // nsHttpHandler::AddStandardRequestHeaders.

    nsCOMPtr<nsIReferrerInfo> referrerInfo =
        new ReferrerInfo(aRequest->mReferrer, aRequest->ReferrerPolicy());
    rv = httpChannel->SetReferrerInfoWithoutClone(referrerInfo);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    nsAutoString hintCharset;
    if (!aRequest->GetScriptLoadContext()->IsPreload()) {
      if (aRequest->GetScriptLoadContext()->HasScriptElement()) {
        aRequest->GetScriptLoadContext()->GetHintCharset(hintCharset);
      }
    } else if (aRequest->mClassicScriptHintEncoding) {
      nsAutoCString charset;
      aRequest->mClassicScriptHintEncoding->Name(charset);
      hintCharset = NS_ConvertUTF8toUTF16(charset);
    }

    rv = httpChannel->SetClassicScriptHintCharset(hintCharset);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set the initiator type
  nsCOMPtr<nsITimedChannel> timedChannel(do_QueryInterface(httpChannel));
  if (timedChannel) {
    timedChannel->SetInitiatorType(GetInitiatorType(aRequest));
  }

  return rv;
}

nsresult ScriptLoader::PrepareIncrementalStreamLoader(
    nsIIncrementalStreamLoader** aOutLoader, nsIChannel* aChannel,
    ScriptLoadRequest* aRequest) {
  UniquePtr<mozilla::dom::SRICheckDataVerifier> sriDataVerifier;
  if (!aRequest->mIntegrity.IsEmpty()) {
    sriDataVerifier = std::make_unique<SRICheckDataVerifier>(
        aRequest->mIntegrity, aChannel, mReporter);
  }

  RefPtr<ScriptLoadHandler> handler =
      new ScriptLoadHandler(this, aRequest, std::move(sriDataVerifier));

  aChannel->SetNotificationCallbacks(handler);

  nsresult rv = NS_NewIncrementalStreamLoader(aOutLoader, handler);
  NS_ENSURE_SUCCESS(rv, rv);
  return rv;
}

void ScriptLoader::NotifyPreloadCoalescing(ModuleLoadRequest* aRequest) {
  ScriptLoadContext* context = aRequest->GetScriptLoadContext();
  MOZ_ASSERT(aRequest->IsTopLevel());
  MOZ_ASSERT(context->IsLinkPreloadScript());
  MOZ_ASSERT(!context->Channel());

  auto key = PreloadHashKey::CreateAsScript(
      aRequest->URI(), aRequest->CORSMode(), JS::loader::ScriptKind::eModule);
  context->SetIsCoalescedModulePreload();
  context->NotifyOpen(key, mDocument, /* aIsPreload = */ true,
                      /* aIsModule = */ true);
  context->NotifyPreloadCoalescingResult();
}

nsresult ScriptLoader::StartLoadInternal(ScriptLoadRequest* aRequest,
                                         nsSecurityFlags securityFlags) {
  nsCOMPtr<nsIChannel> channel;
  nsresult rv = CreateChannelForScriptLoading(
      getter_AddRefs(channel), mDocument, aRequest, securityFlags);

  NS_ENSURE_SUCCESS(rv, rv);

  if (aRequest->mEarlyHintPreloaderId) {
    nsCOMPtr<nsIHttpChannelInternal> channelInternal =
        do_QueryInterface(channel);
    NS_ENSURE_TRUE(channelInternal != nullptr, NS_ERROR_FAILURE);

    rv = channelInternal->SetEarlyHintPreloaderId(
        aRequest->mEarlyHintPreloaderId);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  PrepareLoadInfoForScriptLoading(channel, aRequest);

  nsCOMPtr<nsIScriptGlobalObject> scriptGlobal = GetScriptGlobalObject();
  if (!scriptGlobal) {
    return NS_ERROR_FAILURE;
  }

#ifdef NIGHTLY_BUILD
  if (WAICTHandlesScripts()) {
    aRequest->mFetchSourceOnly = true;
  }
#endif

  ScriptLoader::PrepareCacheInfoChannel(channel, aRequest);

  LOG(("ScriptLoadRequest (%p): mode=%u tracking=%d", aRequest,
       unsigned(aRequest->GetScriptLoadContext()->mScriptMode),
       net::ChannelClassifierUtils::IsTrackingClassificationFlag(
           aRequest->GetScriptLoadContext()
               ->GetClassificationFlags()
               .thirdPartyFlags,
           NS_UsePrivateBrowsing(channel))));

  PrepareRequestPriorityAndRequestDependencies(channel, aRequest);

  rv = PrepareHttpRequestAndInitiatorType(channel, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIIncrementalStreamLoader> loader;
  rv =
      PrepareIncrementalStreamLoader(getter_AddRefs(loader), channel, aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  auto key = PreloadHashKey::CreateAsScript(
      aRequest->URI(), aRequest->CORSMode(), aRequest->mKind);
  aRequest->GetScriptLoadContext()->NotifyOpen(
      key, channel, mDocument,
      aRequest->GetScriptLoadContext()->IsLinkPreloadScript(),
      aRequest->IsModuleRequest());

  rv = channel->AsyncOpen(loader);

  if (NS_FAILED(rv)) {
    // Make sure to inform any <link preload> tags about failure to load the
    // resource.
    aRequest->GetScriptLoadContext()->NotifyStart(channel);
    aRequest->GetScriptLoadContext()->NotifyStop(rv);
    // If this was a preload that failed to start, deregister it so a
    // subsequent attempt to load the same URL can succeed.
    if (aRequest->GetScriptLoadContext()->IsPreload()) {
      mDocument->Preloads().DeregisterPreload(key);
    }
  }

  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

bool ScriptLoader::PreloadURIComparator::Equals(const PreloadInfo& aPi,
                                                nsIURI* const& aURI) const {
  bool same;
  return NS_SUCCEEDED(aPi.mRequest->URI()->Equals(aURI, &same)) && same;
}

static bool CSPAllowsInlineScript(nsIScriptElement* aElement,
                                  const nsAString& aSourceText,
                                  const nsAString& aNonce,
                                  Document* aDocument) {
  nsCOMPtr<nsIContentSecurityPolicy> csp =
      PolicyContainer::GetCSP(aDocument->GetPolicyContainer());
  if (!csp) {
    // no CSP --> allow
    return true;
  }

  bool parserCreated =
      aElement->GetParserCreated() != mozilla::dom::NOT_FROM_PARSER;
  nsCOMPtr<Element> element = do_QueryInterface(aElement);

  bool allowInlineScript = false;
  nsresult rv = csp->GetAllowsInline(
      nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE,
      false /* aHasUnsafeHash */, aNonce, parserCreated, element,
      nullptr /* nsICSPEventListener */, aSourceText,
      aElement->GetScriptLineNumber(),
      aElement->GetScriptColumnNumber().oneOriginValue(), &allowInlineScript);
  return NS_SUCCEEDED(rv) && allowInlineScript;
}

namespace {
RequestPriority FetchPriorityToRequestPriority(
    const FetchPriority aFetchPriority) {
  switch (aFetchPriority) {
    case FetchPriority::High:
      return RequestPriority::High;
    case FetchPriority::Low:
      return RequestPriority::Low;
    case FetchPriority::Auto:
      return RequestPriority::Auto;
  }

  MOZ_ASSERT_UNREACHABLE();
  return RequestPriority::Auto;
}
}  // namespace

void ScriptLoader::NotifyObserversForCachedScript(ScriptLoadRequest* aRequest) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();

  if (!obsService->HasObservers("http-on-resource-cache-response")) {
    return;
  }

  nsIScriptElement* element = aRequest->GetScriptLoadContext()->mScriptElement;

  nsCOMPtr<nsINode> context;
  if (element) {
    context = do_QueryInterface(element);
  } else {
    context = mDocument;
  }

  nsCOMPtr<nsIChannel> channel;
  nsresult rv = CreateChannelForScriptLoading(
      getter_AddRefs(channel), mDocument, aRequest->URI(), context,
      aRequest->FetchOptions()->mTriggeringPrincipal,
      CORSModeToSecurityFlags(aRequest->FetchOptions()->mCORSMode),
      nsIContentPolicy::TYPE_INTERNAL_SCRIPT);
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<net::HttpBaseChannel> httpBaseChannel = do_QueryObject(channel);
  if (httpBaseChannel) {
    const net::nsHttpResponseHead* responseHead = nullptr;
    if (aRequest->mNetworkMetadata) {
      responseHead = aRequest->mNetworkMetadata->GetResponseHead();
    }
    httpBaseChannel->SetDummyChannelForCachedResource(responseHead);
  }

  // Populate channel and loadInfo fields, in the same way as
  // StartLoadInternal, but skip unnecessary things.

  PrepareLoadInfoForScriptLoading(channel, aRequest);

  rv = PrepareHttpRequestAndInitiatorType(channel, aRequest);
  if (NS_FAILED(rv)) {
    return;
  }

  ScriptHashKey key(this, aRequest, aRequest->ReferrerPolicy(),
                    aRequest->FetchOptions(),
                    aRequest->getLoadedScript()->GetURI());
  nsAutoCString keyStr;
  key.ToStringForLookup(keyStr);

  obsService->NotifyObservers(channel, "http-on-resource-cache-response",
                              NS_ConvertUTF8toUTF16(keyStr).get());
}

already_AddRefed<ScriptLoadRequest> ScriptLoader::CreateLoadRequest(
    ScriptKind aKind, nsIURI* aURI, nsIScriptElement* aElement,
    const nsAString& aScriptContent, nsIPrincipal* aTriggeringPrincipal,
    CORSMode aCORSMode, const nsAString& aNonce,
    RequestPriority aRequestPriority, const SRIMetadata& aIntegrity,
    ReferrerPolicy aReferrerPolicy, ParserMetadata aParserMetadata,
    ScriptLoadRequestType aRequestType,
    const Encoding* aClassicScriptPreloadHintEncoding) {
  nsIURI* referrer = mDocument->GetDocumentURIAsReferrer();
  RefPtr<ScriptFetchOptions> fetchOptions =
      new ScriptFetchOptions(aCORSMode, aNonce, aRequestPriority,
                             aParserMetadata, aTriggeringPrincipal);
  RefPtr<ScriptLoadContext> context =
      new ScriptLoadContext(aElement, aScriptContent);

  if (aKind == ScriptKind::eModule) {
    RefPtr<ModuleLoadRequest> request = mModuleLoader->CreateTopLevel(
        aURI, aElement, aReferrerPolicy, fetchOptions, aIntegrity, referrer,
        context, aRequestType);

    return request.forget();
  }

  MOZ_ASSERT(aKind == ScriptKind::eClassic || aKind == ScriptKind::eImportMap ||
             (StaticPrefs::dom_speculation_rules_enabled() &&
              aKind == ScriptKind::eSpeculationRules));

  const Encoding* classicScriptHintEncoding = nullptr;
  if (aKind == ScriptKind::eClassic) {
    if (aRequestType == ScriptLoadRequestType::Preload) {
      classicScriptHintEncoding = aClassicScriptPreloadHintEncoding;
    } else {
      MOZ_ASSERT(aClassicScriptPreloadHintEncoding == nullptr);

      nsAutoString classicScriptHintCharset;
      aElement->GetScriptCharset(classicScriptHintCharset);
      if (!classicScriptHintCharset.IsEmpty()) {
        classicScriptHintEncoding =
            Encoding::ForLabel(classicScriptHintCharset);
      }
    }
  } else {
    MOZ_ASSERT(aClassicScriptPreloadHintEncoding == nullptr);
  }

  RefPtr<ScriptLoadRequest> request = new ScriptLoadRequest(
      aKind, aIntegrity, referrer, context, classicScriptHintEncoding);

  TryUseCache(aReferrerPolicy, fetchOptions, aURI, request, aElement, aNonce,
              aRequestType);

  return request.forget();
}

void ScriptLoader::TryUseCache(ReferrerPolicy aReferrerPolicy,
                               ScriptFetchOptions* aFetchOptions, nsIURI* aURI,
                               ScriptLoadRequest* aRequest,
                               nsIScriptElement* aElement,
                               const nsAString& aNonce,
                               ScriptLoadRequestType aRequestType) {
  if (aRequestType == ScriptLoadRequestType::Inline) {
    aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
    LOG(
        ("ScriptLoader (%p): Created LoadedScript (%p) for "
         "ScriptLoadRequest(%p) because inline %s.",
         this, aRequest->getLoadedScript(), aRequest,
         aRequest->URI()->GetSpecOrDefault().get()));
    return;
  }

  if (!mCache) {
    aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
    LOG(
        ("ScriptLoader (%p): Created LoadedScript (%p) for "
         "ScriptLoadRequest(%p) %s.",
         this, aRequest->getLoadedScript(), aRequest,
         aRequest->URI()->GetSpecOrDefault().get()));
    return;
  }

  // NOTE: Some ScriptLoadRequest fields aren't yet accessible until
  //       either NoCacheEntryFound or CacheEntryFound is called,
  //       which constructs ScriptFetchInfo and LoadedScript.
  //       aRequest->FetchOptions() and aRequest->ReferrerPolicy() are
  //       backed by ScriptFetchInfo, and aRequest->URI() is backed by
  //       LoadedScript, and we cannot use them here.
  ScriptHashKey key(this, aRequest, aReferrerPolicy, aFetchOptions, aURI);
  auto cacheResult = mCache->Lookup(*this, key, /* aSyncLoad = */ true);
  MOZ_ASSERT_IF(cacheResult.mState == CachedSubResourceState::Complete,
                cacheResult.mCompleteValue->IsCachedStencil() ||
                    cacheResult.mCompleteValue->IsInvalidatedCachedStencil());
  if (cacheResult.mState != CachedSubResourceState::Complete ||
      !cacheResult.mCompleteValue->IsCachedStencil()) {
    aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
    LOG(
        ("ScriptLoader (%p): Created LoadedScript (%p) for "
         "ScriptLoadRequest(%p) because cache is not found %s.",
         this, aRequest->getLoadedScript(), aRequest,
         aRequest->URI()->GetSpecOrDefault().get()));
    return;
  }

  if (aRequest->IsClassicScript() &&
      cacheResult.mCompleteValue->DependsOnClassicScriptHintEncoding() &&
      cacheResult.mCompleteValue->ClassicScriptEncoding() !=
          GetClassicScriptFallbackEncoding(aRequest)) {
    // See ScriptLoadHandler::TrySetDecoder for the
    // DependsOnClassicScriptHintEncoding flag handling.
    //
    // If this branch is taken, there are multiple cases:
    //   (a) If the request has hint charset, the hint charset should be used
    //       for decoding the script.  The cache entry is decoded with different
    //       encoding, and thus this cache entry shouldn't be used.
    //   (b) If the request has no hint charset, either the document's charset
    //       or the fallback windows-1252 should be used.
    //     (b-1) For script preloads performed during the speculative loading,
    //           the document's charset might not have been reflecting the
    //           actual document's encoding yet.  In that case, tentatively
    //           use the in-memory cache for the preload, and let the actual
    //           script load decide whether to use the preload or not.
    //           NOTE: Do the same for the case where the the encoding
    //                 accidentally match due to the uninitialized encoding
    //                 (UTF-8).
    //     (b-2) For regular loads, the cache entry is decoded with the
    //           different encoding, and thus this cache entry shouldn't be
    //           used.
    if (aRequestType == ScriptLoadRequestType::Preload &&
        !aRequest->mClassicScriptHintEncoding && mDocument &&
        mDocument->GetDocumentCharacterSetSource() == 0) {
      // This is the (b-1) case above.
      LOG(
          ("ScriptLoader (%p): Using in-memory cache with different encoding "
           "for "
           "ScriptLoadRequest(%p) for preload without hint charset, performed "
           "while the document encoding is uninitialized.",
           this, aRequest));
    } else {
      // This is the (a) and (b-2) case above.
      aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
      LOG(
          ("ScriptLoader (%p): Created LoadedScript (%p) for "
           "ScriptLoadRequest(%p) because cache has different encoding %s.",
           this, aRequest->getLoadedScript(), aRequest,
           aRequest->URI()->GetSpecOrDefault().get()));
      return;
    }
  }

  if (!cacheResult.mCompleteValue->IsSRIMetadataReusableBy(
          aRequest->mIntegrity)) {
    mCache->Evict(key);
    aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
    LOG(
        ("ScriptLoader (%p): Created LoadedScript (%p) for "
         "ScriptLoadRequest(%p) because of SRI metadata mismatch %s",
         this, aRequest->getLoadedScript(), aRequest,
         aRequest->URI()->GetSpecOrDefault().get()));
    return;
  }

  if (cacheResult.mCompleteValue->IsDirty()) {
    // The cache entry needs revalidation.
    // Fetch from necko and validate in ScriptLoader::OnStreamComplete.
    TRACE_FOR_TEST(aRequest, "memorycache:dirty:hit");
    aRequest->SetHasDirtyCache();
    aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
    LOG(
        ("ScriptLoader (%p): Created LoadedScript (%p) for "
         "ScriptLoadRequest(%p) because of dirty flag %s.",
         this, aRequest->getLoadedScript(), aRequest,
         aRequest->URI()->GetSpecOrDefault().get()));
    return;
  }

  if (aRequestType == ScriptLoadRequestType::External) {
    // NOTE: The preload case checks the same after the
    //       LookupPreloadRequest call.
    if (NS_FAILED(CheckContentPolicy(aElement, aNonce, aRequest, aFetchOptions,
                                     aURI))) {
      aRequest->NoCacheEntryFound(aReferrerPolicy, aFetchOptions, aURI);
      LOG(
          ("ScriptLoader (%p): Created LoadedScript (%p) for "
           "ScriptLoadRequest(%p) because content policy violation %s.",
           this, aRequest->getLoadedScript(), aRequest,
           aRequest->URI()->GetSpecOrDefault().get()));
      return;
    }
  }

  aRequest->mNetworkMetadata = cacheResult.mNetworkMetadata;

  MOZ_ASSERT(cacheResult.mCompleteValue->CachedReferrerPolicy() ==
             aReferrerPolicy);

  mMemoryCacheUsed++;
  if (!cacheResult.mCompleteValue->IsEverHitFromMemoryCache()) {
    cacheResult.mCompleteValue->SetIsEverHitFromMemoryCache();
    mCache->OnEntryEverHit();
  }
  AddMemoryCacheRefCountTelemetry(cacheResult.mCompleteValue);

  aRequest->CacheEntryFound(cacheResult.mCompleteValue, aFetchOptions);
  MOZ_ASSERT_IF(!aRequest->IsModuleRequest(), aRequest->IsDelayingReady());
  MOZ_ASSERT_IF(aRequest->IsModuleRequest(), aRequest->IsFetching());
  LOG(
      ("ScriptLoader (%p): Found in-memory cache LoadedScript (%p) for "
       "ScriptLoadRequest(%p) %s.",
       this, aRequest->getLoadedScript(), aRequest,
       aRequest->URI()->GetSpecOrDefault().get()));
  TRACE_FOR_TEST(aRequest, "load:memorycache");

  cacheResult.mCompleteValue->AddFetchCount();
  return;
}

void ScriptLoader::EmulateNetworkEvents(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsRetrievedFromMemoryCache());
  MOZ_ASSERT(aRequest->mNetworkMetadata);
  MOZ_ASSERT(!aRequest->IsWasmBytes());

  NotifyObserversForCachedScript(aRequest);

  {
    nsAutoCString name;
    nsString entryName;
    aRequest->URI()->GetSpec(name);
    CopyUTF8toUTF16(name, entryName);

    auto now = TimeStamp::Now();

    SharedSubResourceCacheUtils::AddPerformanceEntryForCache(
        entryName, GetInitiatorType(aRequest), aRequest->mNetworkMetadata, now,
        now, mDocument);
  }
}

bool ScriptLoader::ProcessScriptElement(nsIScriptElement* aElement,
                                        const nsAString& aSourceText) {
  // We need a document to evaluate scripts.
  NS_ENSURE_TRUE(mDocument, false);

  // Check to see if scripts has been turned off.
  if (!mEnabled || !mDocument->IsScriptEnabled()) {
    return false;
  }

  NS_ASSERTION(!aElement->IsMalformed(), "Executing malformed script");

  nsCOMPtr<nsIContent> scriptContent = do_QueryInterface(aElement);

  ScriptKind scriptKind;
  if (aElement->GetScriptIsModule()) {
    scriptKind = ScriptKind::eModule;
  } else if (aElement->GetScriptIsImportMap()) {
    scriptKind = ScriptKind::eImportMap;
  } else if (aElement->GetScriptIsSpeculationRules()) {
    scriptKind = ScriptKind::eSpeculationRules;
  } else {
    scriptKind = ScriptKind::eClassic;
  }

  // Step 13. Check that the script is not an eventhandler
  if (IsScriptEventHandler(scriptKind, scriptContent)) {
    return false;
  }

  // "In modern user agents that support module scripts, the script element with
  // the nomodule attribute will be ignored".
  // "The nomodule attribute must not be specified on module scripts (and will
  // be ignored if it is)."
  if (scriptKind == ScriptKind::eClassic && scriptContent->IsHTMLElement() &&
      scriptContent->AsElement()->HasAttr(nsGkAtoms::nomodule)) {
    return false;
  }

  // Step 15. and later in the HTML5 spec
  if (aElement->GetScriptExternal()) {
    return ProcessExternalScript(aElement, scriptKind, scriptContent);
  }

  return ProcessInlineScript(aElement, scriptKind, aSourceText);
}

static ParserMetadata GetParserMetadata(nsIScriptElement* aElement) {
  return aElement->GetParserCreated() == mozilla::dom::NOT_FROM_PARSER
             ? ParserMetadata::NotParserInserted
             : ParserMetadata::ParserInserted;
}

bool ScriptLoader::ProcessExternalScript(nsIScriptElement* aElement,
                                         ScriptKind aScriptKind,
                                         nsIContent* aScriptContent) {
  LOG(("ScriptLoader (%p): Process external script for element %p", this,
       aElement));

  // https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element
  // Step 33.1. If el's type is "importmap" or "speculationrules", then queue an
  // element task on the DOM manipulation task source given el to fire an event
  // named error at el, and return.
  if (aScriptKind == ScriptKind::eImportMap ||
      aScriptKind == ScriptKind::eSpeculationRules) {
    NS_DispatchToCurrentThread(
        NewRunnableMethod("nsIScriptElement::FireErrorEvent", aElement,
                          &nsIScriptElement::FireErrorEvent));
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Script Loader"_ns, mDocument,
        PropertiesFile::DOM_PROPERTIES,
        aScriptKind == ScriptKind::eImportMap
            ? "ImportMapExternalNotSupported"
            : "SpeculationRulesExternalNotSupported");
    return false;
  }

  nsCOMPtr<nsIURI> scriptURI = aElement->GetScriptURI();
  if (!scriptURI) {
    // Asynchronously report the failure to create a URI object
    NS_DispatchToCurrentThread(
        NewRunnableMethod("nsIScriptElement::FireErrorEvent", aElement,
                          &nsIScriptElement::FireErrorEvent));
    return false;
  }

  // https://html.spec.whatwg.org/#prepare-the-script-element
  // Step 33.7. If el is potentially render-blocking, then block rendering on
  // el.
  mDocument->MaybeBlockRendering(*aScriptContent->AsElement());

  nsString nonce = nsContentSecurityUtils::GetIsElementNonceableNonce(
      *aScriptContent->AsElement());
  SRIMetadata sriMetadata;
  {
    // https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element
    // Step 31.11.
    // - module: If el does not have an integrity attribute, then set options's
    // integrity metadata to the result of resolving a module integrity metadata
    // with url and settings object.
    nsAutoString integrity;
    if (aScriptContent->AsElement()->GetAttr(nsGkAtoms::integrity, integrity)) {
      GetSRIMetadata(integrity, &sriMetadata);
    } else if (aScriptKind == ScriptKind::eModule) {
      mModuleLoader->GetImportMapSRI(scriptURI,
                                     mDocument->GetDocumentURIAsReferrer(),
                                     mReporter, &sriMetadata);
    }
  }

  RefPtr<ScriptLoadRequest> request =
      LookupPreloadRequest(aElement, aScriptKind, sriMetadata);
  if (request) {
    PROFILER_MARKER("ScriptLoader::ProcessExternalScript PreloadRequest", JS,
                    {mozilla::MarkerStack::Capture()}, FlowMarker,
                    Flow::FromPointer(request.get()));

    if (NS_FAILED(CheckContentPolicy(aElement, nonce, request,
                                     request->FetchOptions(),
                                     request->URI()))) {
      LOG(("ScriptLoader (%p): content policy check failed for preload", this));

      // Probably plans have changed; even though the preload was allowed seems
      // like the actual load is not; let's cancel the preload request.
      request->Cancel();
      // The script never loads, so nothing else unblocks rendering for it.
      mDocument->UnblockRendering(*aScriptContent->AsElement());
      return false;
    }

    // Use the preload request.

    LOG(("ScriptLoadRequest (%p): Using preload request", request.get()));

    // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-module-script-tree
    // Step 1. Disallow further import maps given settings object.
    if (request->IsModuleRequest() &&
        !StaticPrefs::dom_multiple_import_maps_enabled()) {
      LOG(("ScriptLoadRequest (%p): Disallow further import maps.",
           request.get()));
      mModuleLoader->DisallowImportMaps();
    }

    // It's possible these attributes changed since we started the preload so
    // update them here.
    request->GetScriptLoadContext()->SetScriptMode(
        aElement->GetScriptDeferred(), aElement->GetScriptAsync(), false);

    // The request will be added to another list or set as
    // mParserBlockingRequest below.
    if (request->GetScriptLoadContext()->mInCompilingList) {
      mOffThreadCompilingRequests.Remove(request);
      request->GetScriptLoadContext()->mInCompilingList = false;
    }
  } else {
    // No usable preload found.

    nsCOMPtr<nsIPrincipal> principal =
        aElement->GetScriptURITriggeringPrincipal();
    if (!principal) {
      principal = aScriptContent->NodePrincipal();
    }

    CORSMode ourCORSMode = aElement->GetCORSMode();
    const FetchPriority fetchPriority = aElement->GetFetchPriority();
    ReferrerPolicy referrerPolicy = GetReferrerPolicy(aElement);
    ParserMetadata parserMetadata = GetParserMetadata(aElement);

    request = CreateLoadRequest(aScriptKind, scriptURI, aElement, VoidString(),
                                principal, ourCORSMode, nonce,
                                FetchPriorityToRequestPriority(fetchPriority),
                                sriMetadata, referrerPolicy, parserMetadata,
                                ScriptLoadRequestType::External, nullptr);

    PROFILER_MARKER("ScriptLoader::ProcessExternalScript CreateLoadRequest", JS,
                    {mozilla::MarkerStack::Capture()}, FlowMarker,
                    Flow::FromPointer(request.get()));

    request->GetScriptLoadContext()->mIsInline = false;
    request->GetScriptLoadContext()->SetScriptMode(
        aElement->GetScriptDeferred(), aElement->GetScriptAsync(), false);
    // keep request->GetScriptLoadContext()->mScriptFromHead to false so we
    // don't treat non preloaded scripts as blockers for full page load. See bug
    // 792438.

    LOG(("ScriptLoadRequest (%p): Created request for external script",
         request.get()));

    nsresult rv = StartLoad(request);
    if (NS_FAILED(rv)) {
      ReportErrorToConsole(request, rv);
      // The script never loads, so nothing else unblocks rendering for it.
      mDocument->UnblockRendering(*aScriptContent->AsElement());

      // If this is a script element that with an https URL scheme would block
      // the parser, we need to block the parser.
      bool block = !(request->GetScriptLoadContext()->IsAsyncScript() ||
                     !aElement->GetParserCreated() ||
                     request->GetScriptLoadContext()->IsDeferredScript());

      // Asynchronously report the load failure
      nsCOMPtr<nsIRunnable> runnable;
      if (block) {
        mParserBlockingRequest = request;
        runnable = NewRunnableMethod<RefPtr<ScriptLoadRequest>, nsresult>(
            "ScriptLoader::HandleLoadErrorAndProcessPendingRequests", this,
            &ScriptLoader::HandleLoadErrorAndProcessPendingRequests, request,
            rv);
      } else {
        runnable =
            NewRunnableMethod("nsIScriptElement::FireErrorEvent", aElement,
                              &nsIScriptElement::FireErrorEvent);
      }

      if (mDocument) {
        mDocument->Dispatch(runnable.forget());
      } else {
        NS_DispatchToCurrentThread(runnable.forget());
      }
      return block;
    }

    if (request->IsRetrievedFromMemoryCache()) {
      // https://html.spec.whatwg.org/#prepare-the-script-element
      //
      // Step 33. If el's type is "classic" and el has a src attribute, or el's
      //          type is "module":
      // ...
      // Step 33.2. If el has an async attribute or el's force async is true:
      // Step 33.2.1. Let scripts be el's preparation-time document's set of
      //              scripts that will execute as soon as possible.
      // Step 33.2.2. Append el to scripts.
      // ...
      // Step 33.3. Otherwise, if el is not parser-inserted:
      // Step 33.3.1. Let scripts be el's preparation-time document's list of
      //              scripts that will execute in order as soon as possible.
      // Step 33.3.2. Append el to scripts.
      // ...
      //
      // https://html.spec.whatwg.org/#the-end
      //
      // Step 7. Spin the event loop until the set of scripts that will execute
      //         as soon as possible and the list of scripts that will execute
      //         in order as soon as possible are empty.
      //
      // For scripts that creates the actual necko channel, the request is
      // associated with the document's load group, and the load group manages
      // the script set and the script list above implicitly, and the above
      // "spin the event loop" is handled by IsBusy() check inside
      // nsDocLoader::DocLoaderIsEmpty.
      //
      // https://searchfox.org/mozilla-central/rev/e85232b4b28ecc970240d39203e417d1c320623c/uriloader/base/nsDocLoader.cpp#704
      //
      // For in-memory-cached scripts, no channel is created, and those scripts
      // should explicitly block the step 7 above.
      //
      // NOTE: IsAsyncScript represents both "async" and "force async".
      if (request->GetScriptLoadContext()->IsAsyncScript() ||
          parserMetadata == ParserMetadata::NotParserInserted) {
        request->GetScriptLoadContext()->BlockOnload(mDocument);
      }
    }
  }

  // We should still be in loading stage of script unless we're loading a
  // module or speculatively off-main-thread parsing a script.
  NS_ASSERTION(SpeculativeOMTParsingEnabled() ||
                   !request->GetScriptLoadContext()->CompileStarted() ||
                   request->IsModuleRequest(),
               "Request should not yet be in compiling stage.");

  if (request->GetScriptLoadContext()->IsAsyncScript()) {
    AddAsyncRequest(request);
    if (request->IsFinished()) {
      // The script is available already. Run it ASAP when the event
      // loop gets a chance to spin.

      // KVKV TODO: Instead of processing immediately, try off-thread-parsing
      // it and only schedule a pending ProcessRequest if that fails.
      ProcessPendingRequestsAsync();
    }
    return false;
  }
  if (!aElement->GetParserCreated()) {
    // Violate the HTML5 spec in order to make LABjs and the "order" plug-in
    // for RequireJS work with their Gecko-sniffed code path. See
    // http://lists.w3.org/Archives/Public/public-html/2010Oct/0088.html
    request->GetScriptLoadContext()->mIsNonAsyncScriptInserted = true;
    mNonAsyncExternalScriptInsertedRequests.AppendElement(request);
    if (request->IsFinished()) {
      // The script is available already. Run it ASAP when the event
      // loop gets a chance to spin.
      ProcessPendingRequestsAsync();
    }
    return false;
  }
  // we now have a parser-inserted request that may or may not be still
  // loading
  if (request->GetScriptLoadContext()->IsDeferredScript()) {
    // We don't want to run this yet.
    // If we come here, the script is a parser-created script and it has
    // the defer attribute but not the async attribute OR it is a module
    // script without the async attribute. Since a
    // a parser-inserted script is being run, we came here by the parser
    // running the script, which means the parser is still alive and the
    // parse is ongoing.
    NS_ASSERTION(mDocument->GetCurrentContentSink() ||
                     aElement->GetParserCreated() == FROM_PARSER_XSLT,
                 "Non-XSLT Defer script on a document without an active "
                 "parser; bug 592366.");
    AddDeferRequest(request);
    return false;
  }

  if (aElement->GetParserCreated() == FROM_PARSER_XSLT) {
    // Need to maintain order for XSLT-inserted scripts
    NS_ASSERTION(!mParserBlockingRequest,
                 "Parser-blocking scripts and XSLT scripts in the same doc!");
    request->GetScriptLoadContext()->mIsXSLT = true;
    mXSLTRequests.AppendElement(request);
    if (request->IsFinished()) {
      // The script is available already. Run it ASAP when the event
      // loop gets a chance to spin.
      ProcessPendingRequestsAsync();
    }
    return true;
  }

  if (request->IsFinished() && ReadyToExecuteParserBlockingScripts()) {
    // The request has already been loaded and there are no pending style
    // sheets. If the script comes from the network stream, cheat for
    // performance reasons and avoid a trip through the event loop.
    if (aElement->GetParserCreated() == FROM_PARSER_NETWORK) {
      return ProcessRequest(request) == NS_ERROR_HTMLPARSER_BLOCK;
    }
    // Otherwise, we've got a document.written script, make a trip through
    // the event loop to hide the preload effects from the scripts on the
    // Web page.
    NS_ASSERTION(!mParserBlockingRequest,
                 "There can be only one parser-blocking script at a time");
    NS_ASSERTION(mXSLTRequests.isEmpty(),
                 "Parser-blocking scripts and XSLT scripts in the same doc!");
    mParserBlockingRequest = request;
    ProcessPendingRequestsAsync();
    return true;
  }

  // The script hasn't loaded yet or there's a style sheet blocking it.
  // The script will be run when it loads or the style sheet loads.
  NS_ASSERTION(!mParserBlockingRequest,
               "There can be only one parser-blocking script at a time");
  NS_ASSERTION(mXSLTRequests.isEmpty(),
               "Parser-blocking scripts and XSLT scripts in the same doc!");
  mParserBlockingRequest = request;
  return true;
}

bool ScriptLoader::ProcessInlineScript(nsIScriptElement* aElement,
                                       ScriptKind aScriptKind,
                                       const nsAString& aSourceText) {
  // Is this document sandboxed without 'allow-scripts'?
  if (mDocument->HasScriptsBlockedBySandbox()) {
    return false;
  }

  nsCOMPtr<Element> element = do_QueryInterface(aElement);
  nsString nonce = nsContentSecurityUtils::GetIsElementNonceableNonce(*element);

  // Does CSP allow this inline script to run?
  if (!CSPAllowsInlineScript(aElement, aSourceText, nonce, mDocument)) {
    return false;
  }

  // Check if adding an import map script is allowed. If not, we bail out
  // early to prevent creating a load request.
  if (aScriptKind == ScriptKind::eImportMap) {
    // https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element
    // Step 31.2 type is "importmap":
    //   Step 1. If el's relevant global object's import maps allowed is false,
    //   then queue an element task on the DOM manipulation task source given el
    //   to fire an event named error at el, and return.
    if (!mModuleLoader->IsImportMapAllowed()) {
      NS_WARNING("ScriptLoader: import maps allowed is false.");
      const char* msg = mModuleLoader->HasImportMapRegistered()
                            ? "ImportMapNotAllowedMultiple"
                            : "ImportMapNotAllowedAfterModuleLoad";
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                      "Script Loader"_ns, mDocument,
                                      PropertiesFile::DOM_PROPERTIES, msg);
      NS_DispatchToCurrentThread(
          NewRunnableMethod("nsIScriptElement::FireErrorEvent", aElement,
                            &nsIScriptElement::FireErrorEvent));
      return false;
    }
  }

  // https://html.spec.whatwg.org/#prepare-the-script-element
  // Step 34.2, the "module" case, step 2: if el is potentially
  // render-blocking, block rendering on el.
  if (aScriptKind == ScriptKind::eModule) {
    mDocument->MaybeBlockRendering(*element);
  }

  // Inline classic scripts ignore their CORS mode and are always CORS_NONE.
  CORSMode corsMode = CORS_NONE;
  if (aScriptKind == ScriptKind::eModule) {
    corsMode = aElement->GetCORSMode();
  }
  // <https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element>
  // step 29 specifies to use the fetch priority. Presumably it has no effect
  // for inline scripts.
  const auto fetchPriority = aElement->GetFetchPriority();

  ReferrerPolicy referrerPolicy = GetReferrerPolicy(aElement);
  ParserMetadata parserMetadata = GetParserMetadata(aElement);

  // NOTE: The `nonce` as specified here is significant, because it's inherited
  // by other scripts (e.g. modules created via dynamic imports).
  RefPtr<ScriptLoadRequest> request = CreateLoadRequest(
      aScriptKind, mDocument->GetDocumentURI(), aElement, aSourceText,
      mDocument->NodePrincipal(), corsMode, nonce,
      FetchPriorityToRequestPriority(fetchPriority),
      SRIMetadata(),  // SRI doesn't apply
      referrerPolicy, parserMetadata, ScriptLoadRequestType::Inline, nullptr);
  request->GetScriptLoadContext()->mIsInline = true;
  request->GetScriptLoadContext()->mLineNo = aElement->GetScriptLineNumber();
  request->GetScriptLoadContext()->mColumnNo =
      aElement->GetScriptColumnNumber();
  request->mFetchSourceOnly = true;
  request->SetTextSource(request->mLoadContext.get());
  TRACE_FOR_TEST(request, "load:source");
  CollectScriptTelemetry(request);

  // Only the 'async' attribute is heeded on an inline module script and
  // inline classic scripts ignore both these attributes.
  MOZ_ASSERT(!aElement->GetScriptDeferred());
  MOZ_ASSERT_IF(!request->IsModuleRequest(), !aElement->GetScriptAsync());
  request->GetScriptLoadContext()->SetScriptMode(
      false, aElement->GetScriptAsync(), false);

  LOG(("ScriptLoadRequest (%p): Created request for inline script",
       request.get()));

  request->SetBaseURL(mDocument->GetDocBaseURI());

  const bool multiImportMapsEnabled =
      StaticPrefs::dom_multiple_import_maps_enabled();
  if (request->IsModuleRequest()) {
    if (!multiImportMapsEnabled) {
      // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-an-inline-module-script-graph
      // Step 1. Disallow further import maps given settings object.
      mModuleLoader->DisallowImportMaps();
    }

    ModuleLoadRequest* const modReq = request->AsModuleRequest();
    if (aElement->GetParserCreated() != NOT_FROM_PARSER) {
      if (aElement->GetScriptAsync()) {
        AddAsyncRequest(modReq);
      } else {
        AddDeferRequest(modReq);
      }
    }

    // This calls OnFetchComplete directly since there's no need to start
    // fetching an inline script.
    modReq->OnFetchComplete(NS_OK);
    return false;
  }

  if (request->IsImportMapRequest()) {
    if (!multiImportMapsEnabled) {
      // https://html.spec.whatwg.org/multipage/scripting.html#prepare-the-script-element
      // Step 31.2 type is "importmap":
      //   Impl note: Step 1 is done above before creating a ScriptLoadRequest.
      MOZ_ASSERT(mModuleLoader->IsImportMapAllowed());

      //   Step 2. Set el's relevant global object's import maps allowed to
      //   false.
      mModuleLoader->DisallowImportMaps();
    }

    //   Step 3. Let result be the result of creating an import map parse result
    //   given source text and base URL.
    UniquePtr<ImportMap> importMap = mModuleLoader->ParseImportMap(request);
    if (!importMap) {
      // If parsing import maps fails, the exception will be reported in
      // ModuleLoaderBase::ParseImportMap, and the registration of the import
      // map will bail out early.
      return false;
    }

    // Remove any module preloads. Module specifier resolution is invalidated by
    // adding an import map, and incorrect dependencies may have been loaded.
    mPreloads.RemoveElementsBy(
        [this, multiImportMapsEnabled](const PreloadInfo& info) {
          if (!info.mRequest->IsModuleRequest()) {
            return false;
          }

          info.mRequest->Cancel();
          if (multiImportMapsEnabled) {
            mModuleLoader->ClearPreloadedModuleGraph(
                info.mRequest->AsModuleRequest());
          }

          return true;
        });

    // TODO: Bug 1781758: Move RegisterImportMap into EvaluateScriptElement.
    //
    // https://html.spec.whatwg.org/multipage/scripting.html#execute-the-script-element
    // The spec defines 'register an import map' should be done in
    // 'execute the script element', because inside 'execute the script element'
    // it will perform a 'preparation-time document check'.
    // However, as import maps could be only inline scripts by now, the
    // 'preparation-time document check' will never fail for import maps.
    // So we simply call 'register an import map' here.
    mModuleLoader->RegisterImportMap(std::move(importMap), request);
    return false;
  }

  if (request->IsSpeculationRulesRequest()) {
    MOZ_ASSERT(StaticPrefs::dom_speculation_rules_enabled());
    mDocument->SetUseCounter(eUseCounter_custom_SpeculationRulesScriptTag);

    // https://html.spec.whatwg.org/#prepare-the-script-element
    // Step 34.2. Switch on el's type:
    //  ...
    //  "speculationrules":
    //    1. Let result be the result of creating a speculation rules parse
    //    result given source text and el's node document.
    nsAutoString source;
    aElement->GetScriptText(source);
    auto speculationRuleSetResult = SpeculationRuleSet::Parse(
        NS_ConvertUTF16toUTF8(source), request->BaseURL(), request->BaseURL());

    // Like in the import map case above, we register the speculation rules here
    // instead of later in EvaluateScriptElement.

    // https://html.spec.whatwg.org/#execute-the-script-element
    // Step 6. Switch on el's type:
    //  ...
    //  "speculationrules":
    //    1. Register speculation rules given el's relevant global object and
    //    el's result.
    if (speculationRuleSetResult.isErr()) {
      // https://html.spec.whatwg.org/#register-speculation-rules
      // Step 1.2.
      nsCOMPtr<nsIScriptGlobalObject> global = GetScriptGlobalObject();
      if (!global) {
        return false;
      }
      SpeculationRuleSet::ReportParseError(
          global, speculationRuleSetResult.unwrapErr());
      return false;
    }

    mDocument->SpeculationRules().RegisterFromScript(
        aElement, speculationRuleSetResult.unwrap());
    return false;
  }

  request->mState = ScriptLoadRequest::State::Ready;
  if (aElement->GetParserCreated() == FROM_PARSER_XSLT &&
      (!ReadyToExecuteParserBlockingScripts() || !mXSLTRequests.isEmpty())) {
    // Need to maintain order for XSLT-inserted scripts
    NS_ASSERTION(!mParserBlockingRequest,
                 "Parser-blocking scripts and XSLT scripts in the same doc!");
    mXSLTRequests.AppendElement(request);
    return true;
  }
  if (aElement->GetParserCreated() == NOT_FROM_PARSER) {
    RunScriptWhenSafe(request);
    return false;
  }
  if (aElement->GetParserCreated() == FROM_PARSER_NETWORK &&
      !ReadyToExecuteParserBlockingScripts()) {
    NS_ASSERTION(!mParserBlockingRequest,
                 "There can be only one parser-blocking script at a time");
    mParserBlockingRequest = request;
    NS_ASSERTION(mXSLTRequests.isEmpty(),
                 "Parser-blocking scripts and XSLT scripts in the same doc!");
    return true;
  }
  // We now have a document.written inline script or we have an inline script
  // from the network but there is no style sheet that is blocking scripts.
  // Don't check for style sheets blocking scripts in the document.write
  // case to avoid style sheet network activity affecting when
  // document.write returns. It's not really necessary to do this if
  // there's no document.write currently on the call stack. However,
  // this way matches IE more closely than checking if document.write
  // is on the call stack.
  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(),
               "Not safe to run a parser-inserted script?");
  return ProcessRequest(request) == NS_ERROR_HTMLPARSER_BLOCK;
}

ScriptLoadRequest* ScriptLoader::LookupPreloadRequest(
    nsIScriptElement* aElement, ScriptKind aScriptKind,
    const SRIMetadata& aSRIMetadata) {
  MOZ_ASSERT(aElement);

  nsTArray<PreloadInfo>::index_type i =
      mPreloads.IndexOf(aElement->GetScriptURI(), 0, PreloadURIComparator());
  if (i == nsTArray<PreloadInfo>::NoIndex) {
    return nullptr;
  }
  RefPtr<ScriptLoadRequest> request = mPreloads[i].mRequest;
  if (aScriptKind != request->mKind) {
    return nullptr;
  }

#ifdef NIGHTLY_BUILD
  if (WAICTHandlesScripts()) {
    return nullptr;
  }
#endif

  // Found preloaded request. Note that a script-inserted script can steal a
  // preload!
  request->GetScriptLoadContext()->SetIsLoadRequest(aElement);

  if (request->GetScriptLoadContext()->mWasCompiledOMT &&
      !request->IsModuleRequest()) {
    request->SetReady();
  }

  mPreloads.RemoveElementAt(i);

  // Double-check that the charset the preload used is the same as the charset
  // we have now.
  nsAutoString elementCharset;
  aElement->GetScriptCharset(elementCharset);
  const Encoding* elementEncoding = Encoding::ForLabel(elementCharset);

  // Bug 1832361: charset and crossorigin attributes shouldn't affect matching
  // of module scripts and modulepreload
  if (request->IsClassicScript() &&
      (request->mClassicScriptHintEncoding != elementEncoding ||
       aElement->GetCORSMode() != request->CORSMode())) {
    // Drop the preload.
    request->Cancel();
    return nullptr;
  }

  // At the point of script preload, the document's charset might be different
  // in the following situation:
  //   1. Document's charset is set to "uninitialied" UTF-8
  //   2. Speculative load performs a script preload
  //   3. Script preload uses the "uninitialied" charset as a fallback encoding
  //      when looking up an in-memory cache entry
  //   4. An in-memory cache entry is found for the preload
  //   5. Document's charset is set to different one, by auto-detection etc
  //   6. Actual script load is performed
  //
  // In this case, we should re-validate the fallback encoding.
  if (request->IsRetrievedFromMemoryCache() &&
      request->getLoadedScript()->DependsOnClassicScriptHintEncoding() &&
      request->getLoadedScript()->ClassicScriptEncoding() !=
          GetClassicScriptFallbackEncoding(elementEncoding)) {
    // Drop the preload.
    request->Cancel();
    return nullptr;
  }

  if (!aSRIMetadata.CanTrustBeDelegatedTo(request->mIntegrity)) {
    // Don't cancel link preload requests, we want to deliver onload according
    // the result of the load, cancellation would unexpectedly lead to error
    // notification.
    if (!request->GetScriptLoadContext()->IsLinkPreloadScript()) {
      request->Cancel();
    }
    return nullptr;
  }

  if (StaticPrefs::dom_multiple_import_maps_enabled()) {
    // During preload, the resolved module specifiers are stored in the module
    // script. Now the preload request is reused, so adding the resolved
    // specifiers into the global's resolved module set.
    if (request->IsModuleRequest()) {
      mModuleLoader->MovePreloadedSetToResolvedSet(request->AsModuleRequest());
    }
  }

  // Report any errors that we skipped while preloading.
  ReportPreloadErrorsToConsole(request);

  // This makes sure the pending preload (if exists) for this resource is
  // properly marked as used and thus not notified in the console as unused.
  request->GetScriptLoadContext()->NotifyUsage(mDocument);
  // A used preload must no longer be found in the Document's hash table.  Any
  // <link preload> tag after the <script> tag will start a new request, that
  // can be satisfied from a different cache, but not from the preload cache.
  request->GetScriptLoadContext()->RemoveSelf(mDocument);

  return request;
}

void ScriptLoader::GetSRIMetadata(const nsAString& aIntegrityAttr,
                                  SRIMetadata* aMetadataOut) {
  MOZ_ASSERT(aMetadataOut->IsEmpty());

  if (aIntegrityAttr.IsEmpty()) {
    return;
  }

  MOZ_LOG(SRILogHelper::GetSriLog(), mozilla::LogLevel::Debug,
          ("ScriptLoader::GetSRIMetadata, integrity=%s",
           NS_ConvertUTF16toUTF8(aIntegrityAttr).get()));

  nsAutoCString sourceUri;
  if (mDocument->GetDocumentURI()) {
    mDocument->GetDocumentURI()->GetAsciiSpec(sourceUri);
  }
  SRICheck::IntegrityMetadata(aIntegrityAttr, sourceUri, mReporter,
                              aMetadataOut);
}

ReferrerPolicy ScriptLoader::GetReferrerPolicy(nsIScriptElement* aElement) {
  ReferrerPolicy scriptReferrerPolicy = aElement->GetReferrerPolicy();
  if (scriptReferrerPolicy != ReferrerPolicy::_empty) {
    return scriptReferrerPolicy;
  }
  return mDocument->GetReferrerPolicy();
}

void ScriptLoader::CancelAndClearScriptLoadRequests() {
  // Cancel all requests that have not been executed and remove them.

  if (mParserBlockingRequest) {
    mParserBlockingRequest->Cancel();
    mParserBlockingRequest = nullptr;
  }

  mDeferRequests.CancelRequestsAndClear();
  mLoadingAsyncRequests.CancelRequestsAndClear();
  mLoadedAsyncRequests.CancelRequestsAndClear();
  mNonAsyncExternalScriptInsertedRequests.CancelRequestsAndClear();
  mXSLTRequests.CancelRequestsAndClear();
  mOffThreadCompilingRequests.CancelRequestsAndClear();

  if (mModuleLoader) {
    mModuleLoader->CancelFetchingModules();
    mModuleLoader->CancelAndClearDynamicImports();
  }

  for (ModuleLoader* loader : mWebExtModuleLoaders) {
    loader->CancelAndClearDynamicImports();
  }

  for (size_t i = 0; i < mPreloads.Length(); i++) {
    mPreloads[i].mRequest->Cancel();
  }
  mPreloads.Clear();
}

nsresult ScriptLoader::CompileOffThreadOrProcessRequest(
    ScriptLoadRequest* aRequest) {
  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(),
               "Processing requests when running scripts is unsafe.");

  if (!aRequest->IsRetrievedFromMemoryCache() &&
      !aRequest->GetScriptLoadContext()->mCompileOrDecodeTask &&
      !aRequest->GetScriptLoadContext()->CompileStarted()) {
    bool couldCompile = false;
    nsresult rv = AttemptOffThreadScriptCompile(aRequest, &couldCompile);
    if (NS_FAILED(rv)) {
      HandleLoadError(aRequest, rv);
      return rv;
    }

    if (couldCompile) {
      return NS_OK;
    }
  }

  return ProcessRequest(aRequest);
}

namespace {

class OffThreadCompilationCompleteTask : public Task {
 public:
  OffThreadCompilationCompleteTask(ScriptLoadRequest* aRequest,
                                   ScriptLoader* aLoader)
      : Task(Kind::MainThreadOnly, EventQueuePriority::Normal),
        mRequest(aRequest),
        mLoader(aLoader) {
    MOZ_ASSERT(NS_IsMainThread());
  }

  void RecordStartTime() { mStartTime = TimeStamp::Now(); }
  void RecordStopTime() { mStopTime = TimeStamp::Now(); }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("dom::OffThreadCompilationCompleteTask");
    return true;
  }
#endif

  MOZ_CAN_RUN_SCRIPT_BOUNDARY TaskResult Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    RefPtr<ScriptLoadContext> context = mRequest->GetScriptLoadContext();

    if (!context->mCompileOrDecodeTask) {
      // Request has been cancelled by MaybeCancelOffThreadScript, which nulls
      // the task. This task depends on that cancelled one, so it has finished
      // by now and is only kept alive by the list of cancelled tasks, along
      // with its FrontendContext and its result.
      CompileOrDecodeTask::ForgetFinishedCancelledTasks();
      return TaskResult::Complete;
    }

    RecordStopTime();

    if (profiler_is_active()) {
      ProfilerString8View scriptSourceString;
      if (mRequest->IsFetchedAsTextSource()) {
        scriptSourceString = "ScriptCompileOffThread";
      } else if (mRequest->IsWasmBytes()) {
        scriptSourceString = "WasmCompileOffThread";
      } else {
        MOZ_ASSERT(mRequest->IsRetrievedAsSerializedStencil());
        scriptSourceString = "DecodeStencilOffThread";
      }

      nsAutoCString profilerLabelString;
      mRequest->GetScriptLoadContext()->GetProfilerLabel(profilerLabelString);
      PROFILER_MARKER_TEXT(scriptSourceString, JS,
                           MarkerTiming::Interval(mStartTime, mStopTime),
                           profilerLabelString);
    }

    const RefPtr<ScriptLoadRequest> request = std::move(mRequest);
    const RefPtr<ScriptLoader> loader = std::move(mLoader);
    (void)loader->ProcessOffThreadRequest(request);
    return TaskResult::Complete;
  }

 private:
  // NOTE:
  // These fields are main-thread only, and this task shouldn't be freed off
  // main thread.
  //
  // This is guaranteed by not having off-thread tasks which depends on this
  // task, because otherwise the off-thread task's mDependencies can be the
  // last reference, which results in freeing this task off main thread.
  //
  // If such task is added, these fields must be moved to separate storage.
  RefPtr<ScriptLoadRequest> mRequest;
  RefPtr<ScriptLoader> mLoader;

  TimeStamp mStartTime;
  TimeStamp mStopTime;
};

} /* anonymous namespace */

// TODO: This uses the same heuristics and the same threshold as the
//       JS::CanCompileOffThread / JS::CanDecodeOffThread APIs, but the
//       heuristics needs to be updated to reflect the change regarding the
//       Stencil API, and also the thread management on the consumer side
//       (bug 1846160).
static constexpr size_t OffThreadMinimumTextLength = 5 * 1000;
static constexpr size_t OffThreadMinimumSerializedStencilLength = 5 * 1000;
static constexpr size_t OffThreadMinimumWasmLength = 16 * 1024;

nsresult ScriptLoader::AttemptOffThreadScriptCompile(
    ScriptLoadRequest* aRequest, bool* aCouldCompileOut) {
  // If speculative parsing is enabled, the request may not be ready to run if
  // the element is not yet available.
  MOZ_ASSERT_IF(!SpeculativeOMTParsingEnabled() && !aRequest->IsModuleRequest(),
                aRequest->IsFinished());
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mWasCompiledOMT);
  MOZ_ASSERT(aCouldCompileOut && !*aCouldCompileOut);

  // Don't off-thread compile inline scripts.
  if (aRequest->GetScriptLoadContext()->mIsInline) {
    return NS_OK;
  }

  if (aRequest->IsRetrievedFromMemoryCache()) {
    // This is a revived cache.
    return NS_OK;
  }

  // Don't off-thread compile JSON or CSS modules.
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1912112 (JSON)
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1987143 (CSS)
  if (aRequest->IsModuleRequest() &&
      (aRequest->AsModuleRequest()->mModuleType == JS::ModuleType::JSON ||
       aRequest->AsModuleRequest()->mModuleType == JS::ModuleType::CSS)) {
    return NS_OK;
  }

  nsCOMPtr<nsIGlobalObject> globalObject = GetGlobalForRequest(aRequest);
  if (!globalObject) {
    return NS_ERROR_FAILURE;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(globalObject)) {
    return NS_ERROR_FAILURE;
  }

  JSContext* cx = jsapi.cx();
  JS::CompileOptions options(cx);

  // Introduction script will actually be computed and set when the script is
  // collected from offthread
  JS::Rooted<JSScript*> dummyIntroductionScript(cx);
  nsresult rv = FillCompileOptionsForRequest(cx, aRequest, &options,
                                             &dummyIntroductionScript);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aRequest->IsFetchedAsTextSource()) {
    if (!StaticPrefs::javascript_options_parallel_parsing() ||
        aRequest->ScriptTextLength() < OffThreadMinimumTextLength) {
      TRACE_FOR_TEST(aRequest, "compile:main thread");
      return NS_OK;
    }
  } else if (aRequest->IsWasmBytes()) {
    if (!StaticPrefs::javascript_options_parallel_parsing() ||
        aRequest->WasmBytes().length() < OffThreadMinimumWasmLength) {
      TRACE_FOR_TEST(aRequest, "compile:main thread");
      return NS_OK;
    }

    // Only source phase modules are compiled off-thread. Compilation of
    // evaluation phase modules is not yet implemented (Bug 2030454).
    if (!aRequest->AsModuleRequest()->IsSourcePhaseRequest()) {
      TRACE_FOR_TEST(aRequest, "compile:main thread");
      return NS_OK;
    }
  } else {
    MOZ_ASSERT(aRequest->IsRetrievedAsSerializedStencil());

    JS::TranscodeRange range = aRequest->SerializedStencil();
    if (!StaticPrefs::javascript_options_parallel_parsing() ||
        range.length() < OffThreadMinimumSerializedStencilLength) {
      return NS_OK;
    }
  }

  RefPtr<CompileOrDecodeTask> compileOrDecodeTask;
  rv = CreateOffThreadTask(cx, aRequest, options,
                           getter_AddRefs(compileOrDecodeTask));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<OffThreadCompilationCompleteTask> completeTask =
      new OffThreadCompilationCompleteTask(aRequest, this);

  completeTask->RecordStartTime();

  aRequest->GetScriptLoadContext()->mCompileOrDecodeTask = compileOrDecodeTask;
  completeTask->AddDependency(compileOrDecodeTask);

  TaskController::Get()->AddTask(compileOrDecodeTask.forget());
  TaskController::Get()->AddTask(completeTask.forget());

  TRACE_FOR_TEST(aRequest, "compile:off thread");

  aRequest->GetScriptLoadContext()->BlockOnload(mDocument);

  // Once the compilation is finished, the completeTask will be run on
  // the main thread to call ScriptLoader::ProcessOffThreadRequest for the
  // request.
  aRequest->mState = ScriptLoadRequest::State::Compiling;

  // Requests that are not tracked elsewhere are added to a list while they are
  // being compiled off-thread, so we can cancel the compilation later if
  // necessary.
  //
  // Non-top-level modules not tracked because these are cancelled from their
  // importing module.
  if (aRequest->IsTopLevel() && !aRequest->isInList()) {
    mOffThreadCompilingRequests.AppendElement(aRequest);
    aRequest->GetScriptLoadContext()->mInCompilingList = true;
  }

  *aCouldCompileOut = true;

  return NS_OK;
}

CompileOrDecodeTask::CompileOrDecodeTask(Type aType)
    : Task(Kind::OffMainThreadOnly, EventQueuePriority::Normal),
      mMutex("CompileOrDecodeTask"),
      mType(aType) {}

Task::TaskResult CompileOrDecodeTask::Run() {
  MutexAutoLock lock(mMutex);

  if (mIsCancelled) {
    mMayStillRun = false;
    return TaskResult::Complete;
  }

  TaskResult result = RunTask();
  // An interrupted task runs again later, so it is still worth waiting for.
  mMayStillRun = result != TaskResult::Complete;
  return result;
}

void CompileOrDecodeTask::Cancel() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mIsCancelled);

  mIsCancelled = true;
  CancelTask();
  TrackCancelled();
}

void CompileOrDecodeTask::WaitForRunningTask() {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
}

// Cancelled tasks that may still be running. A running one is waited for at
// shutdown, so it cannot outlive the frontend state that JS_ShutDown frees.
static StaticAutoPtr<nsTArray<RefPtr<CompileOrDecodeTask>>> sCancelledTasks;

void CompileOrDecodeTask::ForgetFinishedCancelledTasks() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!sCancelledTasks) {
    return;
  }

  sCancelledTasks->RemoveElementsBy(
      [](const RefPtr<CompileOrDecodeTask>& aTask) {
        return !aTask->MayStillRun();
      });
}

void CompileOrDecodeTask::EnsureCancelledTasksList() {
  MOZ_ASSERT(NS_IsMainThread());

  if (sCancelledTasks) {
    return;
  }

  sCancelledTasks = new nsTArray<RefPtr<CompileOrDecodeTask>>();

  RunOnShutdown(
      [] {
        for (const RefPtr<CompileOrDecodeTask>& task : *sCancelledTasks) {
          task->WaitForRunningTask();
        }
        sCancelledTasks = nullptr;
      },
      ShutdownPhase::XPCOMShutdownThreads);
}

void CompileOrDecodeTask::TrackCancelled() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mIsCancelled, "Only Cancel tracks a task");

  if (!MayStillRun()) {
    return;
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads)) {
    WaitForRunningTask();
    return;
  }

  EnsureCancelledTasksList();
  ForgetFinishedCancelledTasks();
  sCancelledTasks->AppendElement(this);
}

StencilCompileOrDecodeTask::StencilCompileOrDecodeTask(Type aType)
    : CompileOrDecodeTask(aType),
      mOptions(JS::OwningCompileOptions::ForFrontendContext()) {}

StencilCompileOrDecodeTask::~StencilCompileOrDecodeTask() {
  if (mFrontendContext) {
    JS::DestroyFrontendContext(mFrontendContext);
    mFrontendContext = nullptr;
  }
}

void StencilCompileOrDecodeTask::CancelTask() {
  // Decode has no poll points, so only compilation actually aborts.
  if (mFrontendContext) {
    JS::RequestFrontendCompilationCancellation(mFrontendContext);
  }
}

nsresult StencilCompileOrDecodeTask::InitFrontendContext() {
  mFrontendContext =
      JS::NewFrontendContext(JS::AllowCancellingCompilation::Yes);
  if (!mFrontendContext) {
    mIsCancelled = true;
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

void StencilCompileOrDecodeTask::DidRunTask(RefPtr<JS::Stencil>&& aStencil) {
  if (aStencil) {
    if (!JS::PrepareForInstantiate(mFrontendContext, *aStencil,
                                   mInstantiationStorage)) {
      aStencil = nullptr;
    }
  }

  mStencil = std::move(aStencil);
}

already_AddRefed<JS::Stencil> StencilCompileOrDecodeTask::StealResult(
    JSContext* aCx, JS::InstantiationStorage* aInstantiationStorage) {
  JS::FrontendContext* fc = mFrontendContext;
  mFrontendContext = nullptr;
  auto destroyFrontendContext =
      mozilla::MakeScopeExit([&]() { JS::DestroyFrontendContext(fc); });

  MOZ_ASSERT(fc);

  if (JS::HadFrontendErrors(fc)) {
    (void)JS::ConvertFrontendErrorsToRuntimeErrors(aCx, fc, mOptions);
    return nullptr;
  }

  if (!mStencil && JS::IsTranscodeFailureResult(mResult)) {
    // Decode failure with bad content isn't reported as error.
    JS_ReportErrorASCII(aCx, "failed to decode cache");
    return nullptr;
  }

  // Report warnings.
  if (!JS::ConvertFrontendErrorsToRuntimeErrors(aCx, fc, mOptions)) {
    return nullptr;
  }

  MOZ_ASSERT(mStencil,
             "If this task is cancelled, StealResult shouldn't be called");

  // This task is started and finished successfully.
  *aInstantiationStorage = std::move(mInstantiationStorage);

  return mStencil.forget();
}

enum class CompilationTarget { Script, Module };

template <CompilationTarget target>
class ScriptOrModuleCompileTask final : public StencilCompileOrDecodeTask {
 public:
  explicit ScriptOrModuleCompileTask(
      ScriptLoader::MaybeSourceText&& aMaybeSource)
      : StencilCompileOrDecodeTask(Type::Compile),
        mMaybeSource(std::move(aMaybeSource)) {}

  nsresult Init(JS::CompileOptions& aOptions) {
    nsresult rv = InitFrontendContext();
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mOptions.copy(mFrontendContext, aOptions)) {
      mIsCancelled = true;
      return NS_ERROR_OUT_OF_MEMORY;
    }

    return NS_OK;
  }

  TaskResult RunTask() override MOZ_REQUIRES(mMutex) {
    RefPtr<JS::Stencil> stencil = Compile();

    DidRunTask(std::move(stencil));
    return TaskResult::Complete;
  }

 private:
  already_AddRefed<JS::Stencil> Compile() {
    size_t stackSize = TaskController::GetThreadStackSize();
    JS::SetNativeStackQuota(mFrontendContext,
                            JS::ThreadStackQuotaForSize(stackSize));

    auto compile = [&](auto& source) {
      if constexpr (target == CompilationTarget::Script) {
        return JS::CompileGlobalScriptToStencil(mFrontendContext, mOptions,
                                                source);
      }
      return JS::CompileModuleScriptToStencil(mFrontendContext, mOptions,
                                              source);
    };
    return mMaybeSource.mapNonEmpty(compile);
  }

 public:
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    if constexpr (target == CompilationTarget::Script) {
      aName.AssignLiteral("ScriptCompileTask");
    } else {
      aName.AssignLiteral("ModuleCompileTask");
    }
    return true;
  }
#endif

 private:
  ScriptLoader::MaybeSourceText mMaybeSource;
};

using ScriptCompileTask =
    class ScriptOrModuleCompileTask<CompilationTarget::Script>;
using ModuleCompileTask =
    class ScriptOrModuleCompileTask<CompilationTarget::Module>;

class ScriptDecodeTask final : public StencilCompileOrDecodeTask {
 public:
  ScriptDecodeTask(JS::TranscodeBuffer&& aSRIAndSerializedStencil,
                   size_t aSerializedStencilOffset)
      : StencilCompileOrDecodeTask(Type::Decode),
        mSRIAndSerializedStencil(std::move(aSRIAndSerializedStencil)),
        mSerializedStencilOffset(aSerializedStencilOffset) {}

  JS::TranscodeBuffer TakeBuffer() {
    return std::move(mSRIAndSerializedStencil);
  }

  nsresult Init(JS::DecodeOptions& aOptions) {
    nsresult rv = InitFrontendContext();
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mDecodeOptions.copy(mFrontendContext, aOptions)) {
      mIsCancelled = true;
      return NS_ERROR_OUT_OF_MEMORY;
    }

    return NS_OK;
  }

  TaskResult RunTask() override MOZ_REQUIRES(mMutex) {
    RefPtr<JS::Stencil> stencil = Decode();

    mOptions.steal(std::move(mDecodeOptions));

    DidRunTask(std::move(stencil));
    return TaskResult::Complete;
  }

 private:
  // The stencil, after the SRI which precedes it in the buffer.
  JS::TranscodeRange Range() const {
    return JS::TranscodeRange(
        mSRIAndSerializedStencil.begin() + mSerializedStencilOffset,
        mSRIAndSerializedStencil.length() - mSerializedStencilOffset);
  }

  already_AddRefed<JS::Stencil> Decode() {
    // NOTE: JS::DecodeStencil doesn't need the stack quota.

    RefPtr<JS::Stencil> stencil;
    mResult = JS::DecodeStencil(mFrontendContext, mDecodeOptions, Range(),
                                getter_AddRefs(stencil));
    return stencil.forget();
  }

 public:
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    aName.AssignLiteral("ScriptDecodeTask");
    return true;
  }
#endif

 private:
  JS::OwningDecodeOptions mDecodeOptions;

  // A cancelled task drops these with its result.
  JS::TranscodeBuffer mSRIAndSerializedStencil;

  const size_t mSerializedStencilOffset;
};

ScriptDecodeTask* CompileOrDecodeTask::AsScriptDecodeTask() {
  MOZ_ASSERT(IsDecodeTask());
  return static_cast<ScriptDecodeTask*>(this);
}

JS::TranscodeBuffer StencilCompileOrDecodeTask::TakeSRIAndSerializedStencil() {
  return AsScriptDecodeTask()->TakeBuffer();
}

nsresult WasmCompileTask::Init(JSContext* aCx, JS::CompileOptions& aOptions) {
  mCompileArgs = JS::BuildCompileArgsForESM(aCx, aOptions);
  if (!mCompileArgs) {
    mIsCancelled = true;
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

Task::TaskResult WasmCompileTask::RunTask() {
  mCompileResult =
      JS::CompileForESM(*mCompileArgs, mBytes.begin(), mBytes.length());

  return TaskResult::Complete;
}

bool WasmCompileTask::StealResult(JSContext* aCx,
                                  JS::MutableHandle<JSObject*> aModuleOut) {
  JS::Rooted<JSObject*> wasmModuleObject(aCx);

  MutexAutoLock lock(mMutex);
  if (!JS::FinishCompileForESM(aCx, *mCompileArgs, mCompileResult,
                               &wasmModuleObject)) {
    return false;
  }

  aModuleOut.set(JS::CreateWasmSourcePhaseModule(aCx, wasmModuleObject));
  return !!aModuleOut;
}

nsresult ScriptLoader::CreateOffThreadTask(
    JSContext* aCx, ScriptLoadRequest* aRequest, JS::CompileOptions& aOptions,
    CompileOrDecodeTask** aCompileOrDecodeTask) {
  if (aRequest->IsWasmBytes()) {
    RefPtr<WasmCompileTask> compileTask =
        new WasmCompileTask(std::move(aRequest->WasmBytes()));
    nsresult rv = compileTask->Init(aCx, aOptions);
    NS_ENSURE_SUCCESS(rv, rv);
    compileTask.forget(aCompileOrDecodeTask);
    return NS_OK;
  }

  if (aRequest->IsRetrievedAsSerializedStencil()) {
    JS::DecodeOptions decodeOptions(aOptions);
    RefPtr<ScriptDecodeTask> decodeTask =
        new ScriptDecodeTask(aRequest->TakeSRIAndSerializedStencil(),
                             aRequest->GetSerializedStencilOffset());
    nsresult rv = decodeTask->Init(decodeOptions);
    if (NS_FAILED(rv)) {
      aRequest->RestoreSRIAndSerializedStencil(
          decodeTask->TakeSRIAndSerializedStencil());
      return rv;
    }
    decodeTask.forget(aCompileOrDecodeTask);
    return NS_OK;
  }

  MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                          aRequest->mLoadContext.get());
  NS_ENSURE_SUCCESS(rv, rv);

  if (ShouldApplyDelazifyStrategy(aRequest)) {
    ApplyDelazifyStrategy(&aOptions);
    mTotalFullParseSize +=
        aRequest->ScriptTextLength() > 0
            ? static_cast<uint32_t>(aRequest->ScriptTextLength())
            : 0;

    LOG(
        ("ScriptLoadRequest (%p): non-on-demand-only (omt) Parsing Enabled "
         "for url=%s mTotalFullParseSize=%u",
         aRequest, aRequest->URI()->GetSpecOrDefault().get(),
         mTotalFullParseSize));
  }

  if (aRequest->IsModuleRequest()) {
    RefPtr<ModuleCompileTask> compileTask =
        new ModuleCompileTask(std::move(maybeSource));
    rv = compileTask->Init(aOptions);
    NS_ENSURE_SUCCESS(rv, rv);
    compileTask.forget(aCompileOrDecodeTask);
    return NS_OK;
  }
  MOZ_ASSERT(!aRequest->IsWasmBytes());

  if (StaticPrefs::dom_expose_test_interfaces()) {
    switch (aOptions.eagerDelazificationStrategy()) {
      case JS::DelazificationOption::OnDemandOnly:
        TRACE_FOR_TEST(aRequest, "delazification:OnDemandOnly");
        break;
      case JS::DelazificationOption::CheckConcurrentWithOnDemand:
      case JS::DelazificationOption::ConcurrentDepthFirst:
        TRACE_FOR_TEST(aRequest, "delazification:ConcurrentDepthFirst");
        break;
      case JS::DelazificationOption::ConcurrentLargeFirst:
        TRACE_FOR_TEST(aRequest, "delazification:ConcurrentLargeFirst");
        break;
      case JS::DelazificationOption::ParseEverythingEagerly:
        TRACE_FOR_TEST(aRequest, "delazification:ParseEverythingEagerly");
        break;
    }
  }

  RefPtr<ScriptCompileTask> compileTask =
      new ScriptCompileTask(std::move(maybeSource));
  rv = compileTask->Init(aOptions);
  NS_ENSURE_SUCCESS(rv, rv);
  compileTask.forget(aCompileOrDecodeTask);
  return NS_OK;
}

nsresult ScriptLoader::ProcessOffThreadRequest(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsCompiling());
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mWasCompiledOMT);

  PROFILER_MARKER("ScriptLoader::ProcessOffThreadRequest", JS,
                  {mozilla::MarkerStack::Capture()}, FlowMarker,
                  Flow::FromPointer(aRequest));

  if (aRequest->IsCanceled()) {
    return NS_OK;
  }

  aRequest->GetScriptLoadContext()->mWasCompiledOMT = true;

  if (aRequest->GetScriptLoadContext()->mInCompilingList) {
    mOffThreadCompilingRequests.Remove(aRequest);
    aRequest->GetScriptLoadContext()->mInCompilingList = false;
  }

  if (aRequest->IsModuleRequest()) {
    MOZ_ASSERT(aRequest->GetScriptLoadContext()->mCompileOrDecodeTask);
    ModuleLoadRequest* request = aRequest->AsModuleRequest();
    request->OnFetchComplete(NS_OK);
    return NS_OK;
  }

  // Element may not be ready yet if speculatively compiling, so process the
  // request in ProcessPendingRequests when it is available.
  MOZ_ASSERT_IF(!SpeculativeOMTParsingEnabled(),
                aRequest->GetScriptLoadContext()->HasScriptElement());
  if (!aRequest->GetScriptLoadContext()->HasScriptElement()) {
    // Unblock onload here in case this request never gets executed.
    aRequest->GetScriptLoadContext()->MaybeUnblockOnload();
    return NS_OK;
  }

  aRequest->SetReady();

  // Move async scripts to mLoadedAsyncRequests and process them by calling
  // ProcessPendingRequests.
  if (aRequest != mParserBlockingRequest &&
      (aRequest->GetScriptLoadContext()->IsAsyncScript() ||
       aRequest->GetScriptLoadContext()->IsBlockingScript()) &&
      !aRequest->isInList()) {
    if (aRequest->GetScriptLoadContext()->IsAsyncScript()) {
      // We're adding the request back to async list so that it can be executed
      // later.
      aRequest->GetScriptLoadContext()->mInAsyncList = false;
      AddAsyncRequest(aRequest);
    } else {
      MOZ_ASSERT(
          false,
          "This should not run ever with the current default prefs. The "
          "request should not run synchronously but added to some queue.");
      return ProcessRequest(aRequest);
    }
  } else if (aRequest->GetScriptLoadContext()->mInAsyncList) {
    // This async request may already be in mLoadingAsyncRequests while its
    // off-thread compilation finishes. Now that it is ready, move it to
    // mLoadedAsyncRequests so ProcessPendingRequests can execute it.
    MaybeMoveToLoadedList(aRequest);
  }

  // Process other scripts in the proper order.
  ProcessPendingRequests();
  return NS_OK;
}

nsresult ScriptLoader::ProcessRequest(ScriptLoadRequest* aRequest) {
  LOG(("ScriptLoadRequest (%p): Process request", aRequest));

  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(),
               "Processing requests when running scripts is unsafe.");
  NS_ASSERTION(aRequest->IsFinished(),
               "Processing a request that is not ready to run.");

  NS_ENSURE_ARG(aRequest);

  auto unblockOnload = MakeScopeExit(
      [&] { aRequest->GetScriptLoadContext()->MaybeUnblockOnload(); });

  if (aRequest->IsModuleRequest()) {
    ModuleLoadRequest* request = aRequest->AsModuleRequest();
    if (request->IsDynamicImport()) {
      request->ProcessDynamicImport();
      return NS_OK;
    }

    if (request->mModuleScript &&
        !request->mModuleScript->HasErrorToRethrow()) {
      if (!request->InstantiateModuleGraph()) {
        request->mModuleScript = nullptr;
      }
    }

    if (!request->mModuleScript) {
      // There was an error fetching a module script.  Nothing to do here.
      LOG(("ScriptLoadRequest (%p):   Error loading request, firing error",
           aRequest));
      FireScriptAvailable(NS_ERROR_FAILURE, aRequest);
      return NS_OK;
    }
  }

  nsCOMPtr<nsIScriptElement> oldParserInsertedScript;
  uint32_t parserCreated = aRequest->GetScriptLoadContext()->GetParserCreated();
  if (parserCreated) {
    oldParserInsertedScript = mCurrentParserInsertedScript;
    mCurrentParserInsertedScript =
        aRequest->GetScriptLoadContext()
            ->GetScriptElementForCurrentParserInsertedScript();
  }

  aRequest->GetScriptLoadContext()->BeginEvaluatingTopLevel();

  FireScriptAvailable(NS_OK, aRequest);

  {
    // Try to perform a microtask checkpoint
    nsAutoMicroTask mt;
  }

  nsresult rv = EvaluateScriptElement(aRequest);

  FireScriptEvaluated(rv, aRequest);

  aRequest->GetScriptLoadContext()->EndEvaluatingTopLevel();

  if (parserCreated) {
    mCurrentParserInsertedScript = oldParserInsertedScript;
  }

  if (aRequest->GetScriptLoadContext()->mCompileOrDecodeTask) {
    // The request was parsed off-main-thread, but the result of the off
    // thread parse was not actually needed to process the request
    // (disappearing window, some other error, ...). Finish the
    // request to avoid leaks.
    MOZ_ASSERT(!aRequest->IsModuleRequest());
    aRequest->GetScriptLoadContext()->MaybeCancelOffThreadScript();
  }

  // If the script is in-memory cached, it should already be converted to
  // CachedStencil in ScriptLoader::TryCacheRequest.
  //
  // If the navigation cache is not enabled and the script is going to be
  // saved to disk, it's also converted to CachedStencil in
  // ScriptLoader::RegisterForDiskCache.
  //
  // If the LoadedScript is still eTextSource or eSerializedStencil at this
  // point, they're not going to be cached, and the received data is
  // no longer necessary.
  if (aRequest->getLoadedScript()->IsTextSource()) {
    aRequest->getLoadedScript()->ClearScriptText();
    aRequest->getLoadedScript()->DropSRI();
  } else if (aRequest->getLoadedScript()->IsSerializedStencil()) {
    MOZ_ASSERT(!aRequest->HasStencil());
    aRequest->getLoadedScript()->DropSRIOrSRIAndSerializedStencil();
  }

  return rv;
}

void ScriptLoader::FireScriptAvailable(nsresult aResult,
                                       ScriptLoadRequest* aRequest) {
  const nsCOMPtr<nsIScriptElement> scriptElement =
      aRequest->GetScriptLoadContext()->GetScriptElementForObserver();
  const nsCOMPtr<nsIURI> uri = aRequest->URI();
  for (int32_t i = 0; i < mObservers.Count(); i++) {
    nsCOMPtr<nsIScriptLoaderObserver> obs = mObservers[i];
    obs->ScriptAvailable(aResult, scriptElement,
                         aRequest->GetScriptLoadContext()->mIsInline, uri,
                         aRequest->GetScriptLoadContext()->mLineNo);
  }

  const bool isInlineClassicScript =
      aRequest->GetScriptLoadContext()->mIsInline &&
      !aRequest->IsModuleRequest();
  scriptElement->ScriptAvailable(aResult, scriptElement, isInlineClassicScript,
                                 uri,
                                 aRequest->GetScriptLoadContext()->mLineNo);
}

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
MOZ_CAN_RUN_SCRIPT_BOUNDARY void ScriptLoader::FireScriptEvaluated(
    nsresult aResult, ScriptLoadRequest* aRequest) {
  for (int32_t i = 0; i < mObservers.Count(); i++) {
    nsCOMPtr<nsIScriptLoaderObserver> obs = mObservers[i];
    RefPtr<nsIScriptElement> scriptElement =
        aRequest->GetScriptLoadContext()->GetScriptElementForObserver();
    obs->ScriptEvaluated(aResult, scriptElement,
                         aRequest->GetScriptLoadContext()->mIsInline);
  }

  RefPtr<nsIScriptElement> scriptElement =
      aRequest->GetScriptLoadContext()->GetScriptElementForObserver();
  scriptElement->ScriptEvaluated(aResult, scriptElement,
                                 aRequest->GetScriptLoadContext()->mIsInline);
}

already_AddRefed<nsIGlobalObject> ScriptLoader::GetGlobalForRequest(
    ScriptLoadRequest* aRequest) {
  if (aRequest->IsModuleRequest()) {
    ModuleLoader* loader =
        ModuleLoader::From(aRequest->AsModuleRequest()->mLoader);
    nsCOMPtr<nsIGlobalObject> global = loader->GetGlobalObject();
    return global.forget();
  }

  return GetScriptGlobalObject();
}

already_AddRefed<nsIScriptGlobalObject> ScriptLoader::GetScriptGlobalObject() {
  if (!mDocument) {
    return nullptr;
  }

  nsPIDOMWindowInner* pwin = mDocument->GetInnerWindow();
  if (!pwin) {
    return nullptr;
  }

  nsCOMPtr<nsIScriptGlobalObject> globalObject = do_QueryInterface(pwin);
  NS_ASSERTION(globalObject, "windows must be global objects");

  // and make sure we are setup for this type of script.
  nsresult rv = globalObject->EnsureScriptEnvironment();
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return globalObject.forget();
}

static void ApplyEagerBaselineStrategy(JS::CompileOptions* aOptions) {
  uint32_t strategyIndex = StaticPrefs::
      javascript_options_baselinejit_offthread_compilation_strategy();

  JS::EagerBaselineOption strategy;
  switch (strategyIndex) {
    // Values of 2 and 3 indicate to eagerly baseline compile, but only
    // if JitHints are available.
    case 2:
    case 3:
      strategy = JS::EagerBaselineOption::JitHints;
      break;
    case 4:
      strategy = JS::EagerBaselineOption::Aggressive;
      break;
    default:
      // Value of 0 indicates omt baseline compilation should be disabled.
      // Value of 1 indicates omt baseline compilation should be on demand only,
      // so set the eager baseline strategy to None.
      strategy = JS::EagerBaselineOption::None;
      break;
  }

  aOptions->setEagerBaselineStrategy(strategy);
}

nsresult ScriptLoader::FillCompileOptionsForRequest(
    JSContext* aCx, ScriptLoadRequest* aRequest, JS::CompileOptions* aOptions,
    JS::MutableHandle<JSScript*> aIntroductionScript) {
  // It's very important to use aRequest->URI(), not the final URI of the
  // channel aRequest ended up getting script data from, as the script filename.
  nsresult rv = aRequest->URI()->GetSpec(aRequest->mURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mDocument) {
    mDocument->NoteScriptTrackingStatus(
        aRequest->mURL,
        aRequest->GetScriptLoadContext()->GetClassificationFlags());
  }

  const char* introductionType;
  if (aRequest->IsModuleRequest() &&
      !aRequest->AsModuleRequest()->IsTopLevel()) {
    introductionType = "importedModule";
  } else if (!aRequest->GetScriptLoadContext()->mIsInline) {
    introductionType = "srcScript";
  } else if (aRequest->GetScriptLoadContext()->GetParserCreated() ==
             FROM_PARSER_NETWORK) {
    introductionType = "inlineScript";
  } else {
    introductionType = "injectedScript";
  }
  aOptions->setIntroductionInfoToCaller(aCx, introductionType,
                                        aIntroductionScript);
  aOptions->setFileAndLine(aRequest->mURL.get(),
                           aRequest->GetScriptLoadContext()->mLineNo);
  // The column is only relevant for inline scripts in order for SpiderMonkey to
  // properly compute offsets relatively to the script position within the HTML
  // file. injectedScript are not concerned and are always considered to start
  // at column 0.
  if (aRequest->GetScriptLoadContext()->mIsInline &&
      aRequest->GetScriptLoadContext()->GetParserCreated() ==
          FROM_PARSER_NETWORK) {
    aOptions->setColumn(aRequest->GetScriptLoadContext()->mColumnNo);
  }
  aOptions->setIsRunOnce(true);
  aOptions->setNoScriptRval(true);
  if (aRequest->HasSourceMapURL()) {
    aOptions->setSourceMapURL(aRequest->GetSourceMapURL().get());
  }
  if (aRequest->mOriginPrincipal) {
    nsCOMPtr<nsIGlobalObject> globalObject = GetGlobalForRequest(aRequest);
    nsIPrincipal* scriptPrin = globalObject->PrincipalOrNull();
    MOZ_ASSERT(scriptPrin);
    bool subsumes = scriptPrin->Subsumes(aRequest->mOriginPrincipal);
    aOptions->setMutedErrors(!subsumes);
  }

  aOptions->setDeferDebugMetadata(true);

  if (!mCache) {
    aOptions->borrowBuffer = true;
  }

  ApplyEagerBaselineStrategy(aOptions);

  return NS_OK;
}

/* static */
ScriptLoader::DiskCacheStrategy ScriptLoader::GetDiskCacheStrategy() {
  int32_t strategyPref =
      StaticPrefs::dom_script_loader_bytecode_cache_strategy();
  LOG(("Bytecode-cache: disk cache strategy = %d.", strategyPref));

  DiskCacheStrategy strategy;
  switch (strategyPref) {
    case -2: {
      strategy.mIsDisabled = true;
      break;
    }
    case -1: {
      // Eager mode, skip heuristics!
      strategy.mHasSourceLengthMin = false;
      strategy.mHasFetchCountMin = false;
      break;
    }
    case 1: {
      strategy.mHasSourceLengthMin = true;
      strategy.mHasFetchCountMin = true;
      strategy.mSourceLengthMin = 1024;
      // fetchCountMin is optimized for speed in exchange for additional
      // memory and cache use.
      strategy.mFetchCountMin = 2;
      break;
    }
    default:
    case 0: {
      strategy.mHasSourceLengthMin = true;
      strategy.mHasFetchCountMin = true;
      strategy.mSourceLengthMin = 1024;
      // If we were to optimize only for speed, without considering the impact
      // on memory, we should set this threshold to 2. (Bug 900784 comment 120)
      strategy.mFetchCountMin = 4;
      break;
    }
  }

  return strategy;
}

// https://html.spec.whatwg.org/#creating-a-javascript-module-script
// Step 1: If scripting is disabled, the module script is created from the
// empty source instead of the fetched source.
static bool IsScriptingDisabled(ModuleLoadRequest* aRequest) {
  // ModuleLoaderBase::CreateModuleScript fails before compiling anything if
  // AutoJSAPI cannot be initialized with the global, so it is alive here.
  nsIGlobalObject* global = aRequest->mLoader->GetGlobalObject();
  MOZ_ASSERT(global && global->GetGlobalJSObject());

  return !xpc::Scriptability::AllowedIfExists(global->GetGlobalJSObject());
}

void ScriptLoader::CalculateCacheFlag(ScriptLoadRequest* aRequest) {
  using mozilla::TimeDuration;
  using mozilla::TimeStamp;

#ifdef NIGHTLY_BUILD
  // For WAICT we always need to be able to hash the script source,
  // which might not be possible when caching only the stencil.
  if (WAICTHandlesScripts()) {
    aRequest->MarkNotCacheable();
    return;
  }
#endif

  if (aRequest->GetScriptLoadContext()->mIsInline) {
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: Inline script",
         aRequest));
    aRequest->MarkNotCacheable();
    MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
    // NOTE: An inline script tag can have an SRI, but we don't calculate it
    //       for this case.
    MOZ_ASSERT(aRequest->HasNoSRIOrSRIAndSerializedStencil());
    return;
  }

  if (!net::SchemeIsHttpOrHttps(aRequest->URI())) {
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: Unsupported scheme",
         aRequest));
    // Internal resources can be exposed to the web content, but they don't
    // have to be cached.
    aRequest->MarkNotCacheable();
    MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
    MOZ_ASSERT(aRequest->HasNoSRIOrSRIAndSerializedStencil());
    return;
  }

  if (aRequest->IsModuleRequest()) {
    ModuleLoadRequest* moduleLoadRequest = aRequest->AsModuleRequest();
    if (moduleLoadRequest->mModuleType == JS::ModuleType::JavaScriptOrWasm) {
#ifdef NIGHTLY_BUILD
      // See https://bugzilla.mozilla.org/show_bug.cgi?id=1998240
      // For now, we don't support caching wasm modules.
      if (moduleLoadRequest->HasWasmMimeTypeEssence()) {
        MOZ_ASSERT(aRequest->IsWasmBytes());
        LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: wasm module",
             aRequest));
        aRequest->MarkNotCacheable();
        // The disk reference is cleared when we do the mime essense check
        // in PrepareLoadedRequest.
        MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
        return;
      }
#endif

      if (IsScriptingDisabled(moduleLoadRequest)) {
        LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: empty module",
             aRequest));
        aRequest->MarkNotCacheable();
        // Without the in-memory cache, the LoadedScript is used only by this
        // request, so drop its disk cache reference and the SRI data here.
        if (!UsesMemoryCache()) {
          aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
        }
        return;
      }
    } else {
      LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: synthetic module",
           aRequest));
      aRequest->MarkNotCacheable();
      MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
      MOZ_ASSERT_IF(aRequest->IsFetchedAsTextSource(),
                    aRequest->HasNoSRIOrSRIAndSerializedStencil());
      return;
    }
  }

  MOZ_ASSERT(!aRequest->IsWasmBytes());

  if (!aRequest->IsRetrievedFromMemoryCache() &&
      aRequest->ExpirationTime().IsExpired()) {
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: Expired",
         aRequest));
    // NOTE: The expiration for in-memory-cached case should be handled by
    //       SharedScriptCache.
    aRequest->MarkSkippedAllCaching();
    aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
    return;
  }

  if (strcmp(aRequest->URI()->GetSpecOrDefault().get(),
             "https://snap.licdn.com/li.lms-analytics/insight.min.js") == 0) {
    // See bug 2042605 and
    // https://github.com/linkedin/linkedin-gtm-community-template/commit/d6d31ee6f8880ffd8037e4aea7146ccd7cbba27b
    //
    // There are outdated copies of the above template, which reloads the same
    // script infinitely and recursively in the onload event handler for the
    // same script.
    //
    // Applying cache reduces the turnaround time between the next load and the
    // onload handler, and especially the in-memory cache makes it immediate
    // recursion. As a workaround for this issue, we do not apply any cache
    // for this script.
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip all: bug 2042605",
         aRequest));
    aRequest->MarkNotCacheable();
    aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
    return;
  }

  if (mCache) {
    if (mCache->IsLowMemory()) {
      // During the low-memory situation, we avoid creating another cache,
      // with the following rationale.
      //
      // If there are multiple tabs that share single cache entry, the existence
      // of the cache effectively reduces the memory consumption, but the
      // most common use case is with a single tab, and in that case the cache
      // does not reduce the memory consumption but only reduces the cost of
      // the calculation across navigation.
      LOG(
          ("ScriptLoadRequest (%p): Bytecode-cache: Skip in-memory: memory "
           "pressure",
           aRequest));
      aRequest->MarkSkippedMemoryCaching();
    } else {
      LOG(("ScriptLoadRequest (%p): Bytecode-cache: Mark in-memory: Stencil",
           aRequest));
      aRequest->MarkPassedConditionForMemoryCache();
    }

    // Disk cache is handled by SharedScriptCache.
    return;
  }

  aRequest->MarkSkippedMemoryCaching();

  // The following conditions apply only to the disk cache.

  if (aRequest->IsRetrievedAsSerializedStencil()) {
    LOG(
        ("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: "
         "IsRetrievedAsSerializedStencil",
         aRequest));
    aRequest->MarkSkippedDiskCaching();
    MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
    return;
  }

  // We need the nsICacheInfoChannel to exist to be able to open the alternate
  // data output stream.
  if (!aRequest->getLoadedScript()->HasDiskCacheReference()) {
    LOG(
        ("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: "
         "!LoadedScript::HasDiskCacheReference",
         aRequest));
    aRequest->MarkSkippedDiskCaching();
    MOZ_ASSERT_IF(aRequest->IsFetchedAsTextSource(),
                  aRequest->HasNoSRIOrSRIAndSerializedStencil());
    return;
  }

  auto strategy = GetDiskCacheStrategy();

  if (strategy.mIsDisabled) {
    // Reader mode, keep requesting alternate data but no longer save it.
    LOG(
        ("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: Disabled by "
         "pref.",
         aRequest));
    aRequest->MarkSkippedDiskCaching();

    aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
    return;
  }

  // If the script is too small/large, do not attempt at creating a disk
  // cache for this script, as the overhead of parsing it might not be worth the
  // effort.
  size_t sourceLength;
  if (aRequest->IsRetrievedFromMemoryCache()) {
    sourceLength = JS::GetScriptSourceLength(aRequest->GetStencil());
  } else {
    MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
    sourceLength = aRequest->ReceivedScriptTextLength();
  }
  if (strategy.mHasSourceLengthMin) {
    if (sourceLength < strategy.mSourceLengthMin) {
      LOG(
          ("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: Script is too "
           "small.",
           aRequest));
      aRequest->MarkSkippedDiskCaching();
      aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
      return;
    }
  }

  // The disk cache size is limited by the pref, and also the disk capacity.
  // Assuming the disk capacity is sufficient, we use the pref to limit the
  // maximum size, to avoid processing the too large cache, which will
  // ultimately be rejected when saving the cache.
  //
  // The actual disk cache is the concatenation of the main data and the
  // alternate data.
  //
  // The main data is the JavaScript source transferred over the network,
  // which can be compressed, but it's at most sourceLength bytes.
  //
  // The alternate data is the serialized Stencil, which also contains the
  // raw uncompressed JavaScript source in addition to the compiled data.
  //
  // The serialized Stencil takes ~3.8x size of the source length.
  // (gathered from scripts used in the top 50 websites)
  size_t expectedDiskCacheSize = sourceLength * 5;
  int32_t diskCacheMaxSizeInKb =
      StaticPrefs::browser_cache_disk_max_entry_size();
  // The pref being -1 means "no limit".
  if (diskCacheMaxSizeInKb > 0) {
    if (expectedDiskCacheSize > size_t(diskCacheMaxSizeInKb) * 1024) {
      LOG(
          ("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: Script is too "
           "large.",
           aRequest));
      aRequest->MarkSkippedDiskCaching();
      aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
      return;
    }
  }

  // Check that we loaded the cache entry a few times before attempting any
  // disk cache optimization, such that we do not waste time on entry which
  // are going to be dropped soon.
  if (strategy.mHasFetchCountMin) {
    uint8_t fetchCount = aRequest->mLoadedScript->mFetchCount;
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: fetchCount = %d.", aRequest,
         fetchCount));
    if (fetchCount < strategy.mFetchCountMin) {
      LOG(("ScriptLoadRequest (%p): Bytecode-cache: Skip disk: fetchCount",
           aRequest));
      aRequest->MarkSkippedDiskCaching();

      if (!mCache) {
        // If in-memory cache is not enabled, the disk cache reference
        // and the SRI data is necessary only when the current request
        // reaches the minimum fetch count.  And they can be discarded here
        // if the fetch count is less than the minimum.
        //
        // If in-memory cache is enabled, the disk cache reference and the
        // SRI data is cached with the LoadedScript, and the LoadedScript
        // is reused by the subsequent requests, and the fetch count
        // can reach the minimum later.  We need to keep the disk cache
        // reference and the SRI data until then.
        aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
      }
      return;
    }
  }

  LOG(("ScriptLoadRequest (%p): Bytecode-cache: Mark disk: Passed condition",
       aRequest));
  aRequest->MarkPassedConditionForDiskCache();

  if (aRequest->IsModuleRequest() &&
      aRequest->AsModuleRequest()->IsStaticImport()) {
    MOZ_ASSERT(!aRequest->isInList());
    mDiskCacheableDependencyModules.AppendElement(aRequest);
  }
}

class MOZ_RAII AutoSetProcessingScriptTag {
  nsCOMPtr<nsIScriptContext> mContext;
  bool mOldTag;

 public:
  explicit AutoSetProcessingScriptTag(nsIScriptContext* aContext)
      : mContext(aContext), mOldTag(mContext->GetProcessingScriptTag()) {
    mContext->SetProcessingScriptTag(true);
  }

  ~AutoSetProcessingScriptTag() { mContext->SetProcessingScriptTag(mOldTag); }
};

static void ExecuteCompiledScript(JSContext* aCx, JS::Handle<JSScript*> aScript,
                                  ErrorResult& aRv) {
  if (!aScript) {
    // Compilation succeeds without producing a script if scripting is
    // disabled for the global.
    return;
  }

  if (!JS_ExecuteScript(aCx, aScript)) {
    aRv.NoteJSContextException(aCx);
  }
}

// https://html.spec.whatwg.org/#execute-the-script-element
nsresult ScriptLoader::EvaluateScriptElement(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsFinished());
  MOZ_ASSERT(mDocument);

  // The window may have gone away by this point, in which case there's no point
  // in trying to run the script.
  if (!mDocument->GetInnerWindow()) {
    return NS_OK;
  }

  // 2. If el's preparation-time document is not equal to document, then return.
  Document* ownerDoc =
      aRequest->GetScriptLoadContext()->GetScriptOwnerDocument();
  if (ownerDoc != mDocument) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIGlobalObject> globalObject;
  nsCOMPtr<nsIScriptContext> context;
  if (!IsWebExtensionRequest(aRequest)) {
    // Otherwise we have to ensure that there is a nsIScriptContext.
    nsCOMPtr<nsIScriptGlobalObject> scriptGlobal = GetScriptGlobalObject();
    if (!scriptGlobal) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ASSERT_IF(
        aRequest->IsModuleRequest(),
        aRequest->AsModuleRequest()->GetGlobalObject() == scriptGlobal);

    // Make sure context is a strong reference since we access it after
    // we've executed a script, which may cause all other references to
    // the context to go away.
    context = scriptGlobal->GetScriptContext();
    if (!context) {
      return NS_ERROR_FAILURE;
    }

    globalObject = scriptGlobal;
  }

  // 5. If el's from an external file is true, or el's type is "module", then
  // increment document's ignore-destructive-writes counter.
  const bool ignoreDestructiveWrites =
      !aRequest->GetScriptLoadContext()->mIsInline ||
      aRequest->IsModuleRequest();
  if (ignoreDestructiveWrites) {
    if (mDocument) {
      mDocument->IncrementIgnoreDestructiveWritesCounter();
    }
  }

  auto afterScript = MakeScopeExit([&] {
    if (mContinueParsingDocumentAfterCurrentScript) {
      // This mechanism is currently only used when the parser returns
      // early due to this script loader having a current script. However,
      // now that we have this, we could migrate continuing after a
      // parser-blocking script to this same mechanism. Not doing it right
      // away to reduce risk of introducing bugs.
      mContinueParsingDocumentAfterCurrentScript = false;
      if (mDocument) {
        nsCOMPtr<nsIParser> parser = mDocument->CreatorParserOrNull();
        if (parser) {
          parser->ContinueInterruptedParsingAsync();
        }
      }
    }
    // 7. Decrement the ignore-destructive-writes counter of document, if it was
    // incremented in the earlier step.
    if (ignoreDestructiveWrites) {
      if (mDocument) {
        mDocument->DecrementIgnoreDestructiveWritesCounter();
      }
    }
  });

  // Update our current script.
  // This must be destroyed after destroying nsAutoMicroTask, see:
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1620505#c4
  nsIScriptElement* currentScript =
      aRequest->IsModuleRequest() ? nullptr
                                  : aRequest->GetScriptLoadContext()
                                        ->GetScriptElementForCurrentScript();
  AutoCurrentScriptUpdater scriptUpdater(this, currentScript);

  Maybe<AutoSetProcessingScriptTag> setProcessingScriptTag;
  if (context) {
    setProcessingScriptTag.emplace(context);
  }

  // https://wicg.github.io/import-maps/#integration-script-type
  // Switch on the script's type for scriptElement:
  // "importmap"
  //    Assert: Never reached.
  MOZ_ASSERT(!aRequest->IsImportMapRequest());

  auto start = TimeStamp::Now();

  nsresult rv;
  if (aRequest->IsModuleRequest()) {
    rv = aRequest->AsModuleRequest()->EvaluateModule();
  } else {
    MOZ_ASSERT(!aRequest->IsWasmBytes());
    rv = EvaluateScript(globalObject, aRequest);
  }

  auto end = TimeStamp::Now();
  auto duration = (end - start).ToMilliseconds();

  static constexpr double LongScriptThresholdInMilliseconds = 1.0;
  if (duration > LongScriptThresholdInMilliseconds) {
    aRequest->SetTookLongInPreviousRuns();
  }

  return rv;
}

// Decode a script contained in a buffer.
void ScriptLoader::Decode(JSContext* aCx, JS::CompileOptions& aCompileOptions,
                          const JS::TranscodeRange& aRange,
                          RefPtr<JS::Stencil>& aStencil, ErrorResult& aRv) {
  JS::DecodeOptions decodeOptions(aCompileOptions);
  if (!mCache) {
    decodeOptions.borrowBuffer = true;
  }

  MOZ_ASSERT(aCompileOptions.noScriptRval);
  JS::TranscodeResult tr =
      JS::DecodeStencil(aCx, decodeOptions, aRange, getter_AddRefs(aStencil));
  // These errors are external parameters which should be handled before the
  // decoding phase, and which are the only reasons why you might want to
  // fallback on decoding failures.
  MOZ_ASSERT(tr != JS::TranscodeResult::Failure_BadBuildId);
  if (tr != JS::TranscodeResult::Ok) {
    aRv = NS_ERROR_DOM_JS_DECODING_ERROR;
    return;
  }
}

bool ScriptLoader::StartCollectingDelazifications(JSContext* aCx,
                                                  JS::Handle<JSScript*> aScript,
                                                  JS::Stencil* aStencil) {
  JS::CollectDelazificationsResult result;
  if (!JS::StartCollectingDelazifications(aCx, aScript, aStencil, result)) {
    return false;
  }
  if (result == JS::CollectDelazificationsResult::NewlyStarted) {
    AppendDelazificationCollection(aScript);
  }
  return true;
}

bool ScriptLoader::StartCollectingDelazifications(JSContext* aCx,
                                                  JS::Handle<JSObject*> aModule,
                                                  JS::Stencil* aStencil) {
  JS::CollectDelazificationsResult result;
  if (!JS::StartCollectingDelazifications(aCx, aModule, aStencil, result)) {
    return false;
  }
  if (result == JS::CollectDelazificationsResult::NewlyStarted) {
    AppendDelazificationCollection(aModule);
  }
  return true;
}

void ScriptLoader::AppendDelazificationCollection(
    JS::Handle<JSScript*> aScript) {
  if (mDelazificationCollectingScripts.IsEmpty() &&
      mDelazificationCollectingModules.IsEmpty()) {
    mozilla::HoldJSObjects(this);
  }
  mDelazificationCollectingScripts.AppendElement(aScript);
}

void ScriptLoader::AppendDelazificationCollection(
    JS::Handle<JSObject*> aModule) {
  if (mDelazificationCollectingScripts.IsEmpty() &&
      mDelazificationCollectingModules.IsEmpty()) {
    mozilla::HoldJSObjects(this);
  }
  mDelazificationCollectingModules.AppendElement(aModule);
}

// Instantiate (on main-thread) a JS::Stencil generated by off-thread or
// main-thread parsing or decoding.
void ScriptLoader::InstantiateStencil(
    JSContext* aCx, JS::CompileOptions& aCompileOptions, JS::Stencil* aStencil,
    JS::MutableHandle<JSScript*> aScript,
    JS::Handle<JSScript*> aDebuggerIntroductionScript, ErrorResult& aRv,
    const nsAutoCString& aProfilerLabelString,
    JS::InstantiationStorage* aStorage /* = nullptr */,
    CollectDelazifications aCollectDelazifications
    /* = CollectDelazifications::No */) {
  AUTO_PROFILER_MARKER_TEXT("ScriptInstantiation", JS,
                            MarkerInnerWindowIdFromJSContext(aCx),
                            aProfilerLabelString);

  JS::InstantiateOptions instantiateOptions(aCompileOptions);
  JS::Rooted<JSScript*> script(
      aCx, JS::InstantiateGlobalStencil(aCx, instantiateOptions, aStencil,
                                        aStorage));
  if (!script) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  if (aCollectDelazifications == CollectDelazifications::Yes) {
    if (!StartCollectingDelazifications(aCx, script, aStencil)) {
      aRv.NoteJSContextException(aCx);
      return;
    }
  }

  aScript.set(script);

  if (instantiateOptions.deferDebugMetadata) {
    // The private value is updated by the
    // ScriptFetchInfo::AssociateWithScript call done after
    // InstantiateClassicScriptFromAny in ScriptLoader::EvaluateScript.
    JS::Rooted<JS::Value> unused(aCx);
    if (!JS::UpdateDebugMetadata(aCx, aScript, instantiateOptions, unused,
                                 nullptr, aDebuggerIntroductionScript,
                                 nullptr)) {
      aRv = NS_ERROR_OUT_OF_MEMORY;
    }
  }
}

void ScriptLoader::InstantiateClassicScriptFromMaybeEncodedSource(
    JSContext* aCx, JS::CompileOptions& aCompileOptions,
    ScriptLoadRequest* aRequest, JS::MutableHandle<JSScript*> aScript,
    JS::Handle<JSScript*> aDebuggerIntroductionScript, ErrorResult& aRv) {
  MOZ_ASSERT(!aRequest->IsWasmBytes());
  nsAutoCString profilerLabelString;
  aRequest->GetScriptLoadContext()->GetProfilerLabel(profilerLabelString);

  CalculateCacheFlag(aRequest);

  if (aRequest->IsRetrievedAsSerializedStencil()) {
    if (aRequest->GetScriptLoadContext()->mCompileOrDecodeTask) {
      LOG(("ScriptLoadRequest (%p): Decode & instantiate and Execute",
           aRequest));
      RefPtr<JS::Stencil> stencil;
      JS::InstantiationStorage storage;
      MOZ_ASSERT(aCompileOptions.noScriptRval);
      stencil =
          aRequest->GetScriptLoadContext()->StealOffThreadResult(aCx, &storage);
      if (!stencil) {
        aRv.NoteJSContextException(aCx);
        return;
      }

      aRequest->SetStencil(stencil);

      InstantiateStencil(aCx, aCompileOptions, stencil, aScript,
                         aDebuggerIntroductionScript, aRv, profilerLabelString,
                         &storage);
    } else {
      LOG(("ScriptLoadRequest (%p): Decode and Execute", aRequest));

      RefPtr<JS::Stencil> stencil;
      {
        AUTO_PROFILER_MARKER_TEXT("DecodeStencilMainThread", JS,
                                  MarkerInnerWindowIdFromJSContext(aCx),
                                  profilerLabelString);
        Decode(aCx, aCompileOptions, aRequest->SerializedStencil(), stencil,
               aRv);
      }

      if (stencil) {
        aRequest->SetStencil(stencil);

        InstantiateStencil(aCx, aCompileOptions, stencil, aScript,
                           aDebuggerIntroductionScript, aRv,
                           profilerLabelString);
      }
    }

    // We do not expect to be saving anything when we already have some
    // serialized Stencil.
    MOZ_ASSERT(!aRequest->getLoadedScript()->HasDiskCacheReference());
    return;
  }

  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  CollectDelazifications collectDelazifications =
      aRequest->PassedConditionForEitherCache() ? CollectDelazifications::Yes
                                                : CollectDelazifications::No;

  if (aRequest->GetScriptLoadContext()->mCompileOrDecodeTask) {
    // Off-main-thread parsing.
    LOG(
        ("ScriptLoadRequest (%p): instantiate off-thread result and "
         "Execute",
         aRequest));
    MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
    RefPtr<JS::Stencil> stencil;
    JS::InstantiationStorage storage;
    MOZ_ASSERT(aCompileOptions.noScriptRval);
    stencil =
        aRequest->GetScriptLoadContext()->StealOffThreadResult(aCx, &storage);
    if (!stencil) {
      aRv.NoteJSContextException(aCx);
      return;
    }

    aRequest->SetStencil(stencil);

    InstantiateStencil(aCx, aCompileOptions, stencil, aScript,
                       aDebuggerIntroductionScript, aRv, profilerLabelString,
                       &storage, collectDelazifications);
  } else {
    // Main thread parsing (inline and small scripts)
    LOG(("ScriptLoadRequest (%p): Compile And Exec", aRequest));
    MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
    MaybeSourceText maybeSource;
    aRv = aRequest->GetScriptSource(aCx, &maybeSource,
                                    aRequest->mLoadContext.get());
    if (!aRv.Failed()) {
      RefPtr<JS::Stencil> stencil;
      ErrorResult erv;
      auto compile = [&](auto& source) {
        AUTO_PROFILER_MARKER_TEXT("ScriptCompileMainThread", JS,
                                  MarkerInnerWindowIdFromJSContext(aCx),
                                  profilerLabelString);

        stencil = CompileGlobalScriptToStencil(aCx, aCompileOptions, source);
        if (!stencil) {
          erv.NoteJSContextException(aCx);
        }
      };

      MOZ_ASSERT(!maybeSource.empty());
      maybeSource.mapNonEmpty(compile);

      if (stencil) {
        aRequest->SetStencil(stencil);

        InstantiateStencil(aCx, aCompileOptions, stencil, aScript,
                           aDebuggerIntroductionScript, erv,
                           profilerLabelString, /* aStorage = */ nullptr,
                           collectDelazifications);
      }

      aRv = std::move(erv);
    }
  }
}

void ScriptLoader::InstantiateClassicScriptFromCachedStencil(
    JSContext* aCx, JS::CompileOptions& aCompileOptions,
    ScriptLoadRequest* aRequest, JS::Stencil* aStencil,
    JS::MutableHandle<JSScript*> aScript,
    JS::Handle<JSScript*> aDebuggerIntroductionScript, ErrorResult& aRv) {
  MOZ_ASSERT(!aRequest->IsWasmBytes());
  nsAutoCString profilerLabelString;
  aRequest->GetScriptLoadContext()->GetProfilerLabel(profilerLabelString);

  CalculateCacheFlag(aRequest);

  MOZ_ASSERT(aRequest->PassedConditionForMemoryCache());

  // For cached stencils, there can be already ongoing work for the in-memory
  // cache and the disk cache.
  //
  // For collecting delazifications, it's detected by
  // JS::StartCollectingDelazifications API and it's not a problem.
  //
  // For disk cache, ScriptLoader::UpdateDiskCache checks the
  // HasDiskCacheReference condition, and that filters out any loaded scripts
  // queued multiple times.
  InstantiateStencil(aCx, aCompileOptions, aStencil, aScript,
                     aDebuggerIntroductionScript, aRv, profilerLabelString,
                     /* aStorage = */ nullptr, CollectDelazifications::Yes);
}

void ScriptLoader::InstantiateClassicScriptFromAny(
    JSContext* aCx, JS::CompileOptions& aCompileOptions,
    ScriptLoadRequest* aRequest, JS::MutableHandle<JSScript*> aScript,
    JS::Handle<JSScript*> aDebuggerIntroductionScript, ErrorResult& aRv) {
  MOZ_ASSERT(!aRequest->IsWasmBytes());
  if (aRequest->IsRetrievedFromMemoryCache()) {
    RefPtr<JS::Stencil> stencil = aRequest->GetStencil();
    InstantiateClassicScriptFromCachedStencil(aCx, aCompileOptions, aRequest,
                                              stencil, aScript,
                                              aDebuggerIntroductionScript, aRv);
    return;
  }

  InstantiateClassicScriptFromMaybeEncodedSource(
      aCx, aCompileOptions, aRequest, aScript, aDebuggerIntroductionScript,
      aRv);
  if (aRv.Failed()) {
    return;
  }

  TryCacheRequest(aRequest);
}

ScriptLoader::CacheBehavior ScriptLoader::GetCacheBehavior(
    ScriptLoadRequest* aRequest) {
  if (!mCache) {
    return CacheBehavior::DoNothingDisabled;
  }

  if (aRequest->ExpirationTime().IsExpired()) {
    return CacheBehavior::Evict;
  }

  // NOTE: A new response may arrive even if the exiting cache is still valid,
  // for example when the request is performed with bypassing the cache.
  //
  // If the response is cacheable, it should overwrite the existing cache
  // if any.  If the response is not cacheable, that should just evict the
  // existing cache if any, so that the next request will also reach the
  // server.
  if (ShouldBypassCache()) {
    // If the request bypasses the cache, the response should always
    // overwrite the cache, regardless of the content.
    return CacheBehavior::Insert;
  }

  ScriptHashKey key(this, aRequest, aRequest->ReferrerPolicy(),
                    aRequest->FetchOptions(),
                    aRequest->getLoadedScript()->GetURI());
  auto cacheResult = mCache->Lookup(*this, key,
                                    /* aSyncLoad = */ true);
  MOZ_ASSERT_IF(cacheResult.mState == CachedSubResourceState::Complete,
                cacheResult.mCompleteValue->IsCachedStencil() ||
                    cacheResult.mCompleteValue->IsInvalidatedCachedStencil());
  if (cacheResult.mState == CachedSubResourceState::Complete &&
      cacheResult.mCompleteValue->IsCachedStencil()) {
    if (!cacheResult.mCompleteValue->IsSRIMetadataReusableBy(
            aRequest->mIntegrity)) {
      mCache->Evict(key);
      return CacheBehavior::Insert;
    }

    return CacheBehavior::DoNothingExisting;
  }

  return CacheBehavior::Insert;
}

void ScriptLoader::TryCacheRequest(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->HasStencil());
  MOZ_ASSERT(!aRequest->IsWasmBytes());

  if (aRequest->IsMarkedNotCacheable()) {
    aRequest->ClearStencil();
    return;
  }

  CacheBehavior cacheBehavior = GetCacheBehavior(aRequest);

  if (cacheBehavior == CacheBehavior::DoNothingDisabled) {
    // If the in-memory cache is not enabled, the disk cache is handled by
    // ScriptLoader, and it needs the stencil if this script is going to be
    // saved to the disk cache.
    if (!aRequest->PassedConditionForDiskCache()) {
      aRequest->ClearStencil();
    }
    return;
  }

  if (cacheBehavior == CacheBehavior::DoNothingExisting) {
    // If there's a compatible cache entry, we'll keep the existing entry.
    aRequest->ClearStencil();
    return;
  }

  MOZ_ASSERT(mCache);

  if (mCache->IsLowMemory()) {
    // If we're in the low-memory situation, do not create new cache entries.
    TRACE_FOR_TEST(aRequest, "memorycache:memorypressure");
    aRequest->ClearStencil();
    return;
  }

  if (!JS::IsStencilCacheable(aRequest->GetStencil())) {
    // If the stencil is not compatible with the cache, this should also evict
    // any existing cache.
    cacheBehavior = CacheBehavior::Evict;
  }

  if (cacheBehavior == CacheBehavior::Insert) {
    LoadedScript* loadedScript = aRequest->getLoadedScript();
    CacheExpirationTime expirationTime = aRequest->ExpirationTime();
    loadedScript->ConvertToCachedStencil(aRequest->GetStencil(),
                                         aRequest->ReferrerPolicy(),
                                         aRequest->BaseURL());
    if (loadedScript->mFetchCount == 0) {
      loadedScript->mFetchCount = 1;
    }
    loadedScript->SetSRIMetadata(aRequest->mIntegrity);
    auto loadData = MakeRefPtr<ScriptLoadData>(this, aRequest, expirationTime,
                                               loadedScript);
    mCache->Insert(*loadData);
    mCache->OnEntryInserted();
    LOG(("ScriptLoader (%p): Inserting in-memory cache for %s.", this,
         aRequest->URI()->GetSpecOrDefault().get()));
    TRACE_FOR_TEST(aRequest, "memorycache:saved");
  } else {
    // The server response is not cacheable.
    // Evict any existing cache.

    MOZ_ASSERT(cacheBehavior == CacheBehavior::Evict);
    ScriptHashKey key(this, aRequest, aRequest->ReferrerPolicy(),
                      aRequest->FetchOptions(), aRequest->URI());
    mCache->Evict(key);
    LOG(("ScriptLoader (%p): Evicting in-memory cache for %s.", this,
         aRequest->URI()->GetSpecOrDefault().get()));

    TRACE_FOR_TEST(aRequest, "memorycache:evict");
  }

  aRequest->ClearStencil();
}

/* static */
nsCString& ScriptLoader::BytecodeMimeTypeFor(
    const ScriptLoadRequest* aRequest) {
  if (aRequest->IsModuleRequest()) {
    return nsContentUtils::JSModuleBytecodeMimeType();
  }
  return nsContentUtils::JSScriptBytecodeMimeType();
}

/* static */
nsCString& ScriptLoader::BytecodeMimeTypeFor(
    const JS::loader::LoadedScript* aLoadedScript) {
  if (aLoadedScript->IsModuleScript()) {
    return nsContentUtils::JSModuleBytecodeMimeType();
  }
  return nsContentUtils::JSScriptBytecodeMimeType();
}

const Encoding* ScriptLoader::GetClassicScriptFallbackEncoding(
    const ScriptLoadRequest* aRequest) {
  return GetClassicScriptFallbackEncoding(aRequest->mClassicScriptHintEncoding);
}

const Encoding* ScriptLoader::GetClassicScriptFallbackEncoding(
    const Encoding* aClassicScriptHintEncoding) {
  if (aClassicScriptHintEncoding) {
    return aClassicScriptHintEncoding;
  }

  // Get the charset from the charset of the document.
  if (mDocument) {
    return mDocument->GetDocumentCharacterSet();
  }

  // Curiously, there are various callers that don't pass aDocument. The
  // fallback in the old code was ISO-8859-1, which behaved like
  // windows-1252.
  return WINDOWS_1252_ENCODING;
}

nsresult ScriptLoader::MaybePrepareForDiskCacheAfterExecute(
    ScriptLoadRequest* aRequest, nsresult aRv) {
  MOZ_ASSERT(!aRequest->IsWasmBytes());
  if (mCache) {
    // Disk cache is handled by SharedScriptCache.
    return NS_OK;
  }

  if (!aRequest->PassedConditionForDiskCache() || !aRequest->HasStencil()) {
    LOG(("ScriptLoadRequest (%p): Bytecode-cache: disabled (rv = %X)", aRequest,
         unsigned(aRv)));
    TRACE_FOR_TEST(aRequest, "diskcache:disabled");

    // For in-memory cached requests, the disk cache references are necessary
    // for later load.
    if (aRequest->HasStencil()) {
      MOZ_ASSERT_IF(!aRequest->PassedConditionForMemoryCache(),
                    !aRequest->getLoadedScript()->HasDiskCacheReference());
    } else {
      // This hits compile error.
      aRequest->getLoadedScript()->DropDiskCacheReferenceAndSRI();
    }

    return aRv;
  }

  TRACE_FOR_TEST(aRequest, "diskcache:register");
  MOZ_ASSERT(aRequest->GetSerializedStencilOffset() ==
             aRequest->SRI().length() + LoadedScript::EncodingHeaderSize);
  RegisterForDiskCache(aRequest);

  return aRv;
}

nsresult ScriptLoader::MaybePrepareModuleForDiskCacheAfterExecute(
    ModuleLoadRequest* aRequest, nsresult aRv) {
  MOZ_ASSERT(aRequest->IsTopLevel() || aRequest->IsDynamicImport());
  MOZ_ASSERT(!aRequest->IsWasmBytes());

  if (mCache) {
    // Disk cache is handled by SharedScriptCache.
    return NS_OK;
  }

  // NOTE: If a module is passed to this multiple times, it can be
  //       enqueued multiple times.
  //       This is okay because ScriptLoader::UpdateDiskCache filters out
  //       any script without the disk cache reference.

  aRv = MaybePrepareForDiskCacheAfterExecute(aRequest, aRv);

  for (auto* r = mDiskCacheableDependencyModules.getFirst(); r;) {
    auto* dep = r->AsModuleRequest();
    MOZ_ASSERT(dep->PassedConditionForDiskCache());

    r = r->getNext();

    if (dep->GetRootModule() != aRequest) {
      continue;
    }

    mDiskCacheableDependencyModules.Remove(dep);

    aRv = MaybePrepareForDiskCacheAfterExecute(dep, aRv);
  }

  return aRv;
}

nsresult ScriptLoader::EvaluateScript(nsIGlobalObject* aGlobalObject,
                                      ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(!aRequest->IsWasmBytes());
  nsAutoMicroTask mt;
  AutoEntryScript aes(aGlobalObject, "EvaluateScript", true);
  JSContext* cx = aes.cx();

  nsAutoCString profilerLabelString;
  aRequest->GetScriptLoadContext()->GetProfilerLabel(profilerLabelString);

  // Create a ClassicScript object and associate it with the JSScript.
  MOZ_ASSERT(aRequest->mLoadedScript->IsClassicScript());

  JS::CompileOptions options(cx);
  JS::Rooted<JSScript*> introductionScript(cx);
  nsresult rv =
      FillCompileOptionsForRequest(cx, aRequest, &options, &introductionScript);

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Apply the delazify strategy if the script is small.
  if (!aRequest->IsRetrievedFromMemoryCache() &&
      aRequest->IsFetchedAsTextSource() &&
      aRequest->ScriptTextLength() < OffThreadMinimumTextLength &&
      ShouldApplyDelazifyStrategy(aRequest)) {
    ApplyDelazifyStrategy(&options);
    mTotalFullParseSize +=
        aRequest->ScriptTextLength() > 0
            ? static_cast<uint32_t>(aRequest->ScriptTextLength())
            : 0;

    LOG(
        ("ScriptLoadRequest (%p): non-on-demand-only (non-omt) Parsing Enabled "
         "for url=%s mTotalFullParseSize=%u",
         aRequest, aRequest->URI()->GetSpecOrDefault().get(),
         mTotalFullParseSize));
  }

  JS::Rooted<JSObject*> global(cx, aGlobalObject->GetGlobalJSObject());
  if (MOZ_UNLIKELY(!xpc::Scriptability::Get(global).Allowed())) {
    return NS_OK;
  }
  ErrorResult erv;
  mozilla::AutoProfilerLabel autoProfilerLabel("JSExecutionContext",
                                               /* dynamicStr */ nullptr,
                                               JS::ProfilingCategoryPair::JS);
  JSAutoRealm autoRealm(cx, global);
  JS::Rooted<JSScript*> script(cx);
  InstantiateClassicScriptFromAny(cx, options, aRequest, &script,
                                  introductionScript, erv);

  if (!erv.Failed()) {
    LOG(("ScriptLoadRequest (%p): Evaluate Script", aRequest));
    AUTO_PROFILER_FLOW_MARKER_TEXT(
        "ScriptExecution", JS, MarkerInnerWindowIdFromJSContext(cx),
        profilerLabelString, Flow::FromPointer(aRequest));

    MOZ_ASSERT(options.noScriptRval);
    TRACE_FOR_TEST(aRequest, "evaluate:classic");

    if (script && JS::GetScriptPrivate(script).isUndefined()) {
      aRequest->FetchInfo()->AssociateWithScript(script);
    }

    ExecuteCompiledScript(cx, script, erv);
  }
  rv = EvaluationExceptionToNSResult(erv);

  if (NS_FAILED(rv)) {
    return rv;
  }

  // This must be called also for compilation failure case, in order to
  // dispatch test-only event.
  rv = MaybePrepareForDiskCacheAfterExecute(aRequest, rv);

  // Even if we are not saving the current script to the disk cache, we have
  // to trigger the disk cache encoding, as the current script can be blocking
  // the other encoding, or the current script can delazify more functions
  // which we are recording the disk cache.
  LOG(("ScriptLoadRequest (%p): ScriptLoader = %p", aRequest, this));
  MaybeUpdateDiskCache();

  return rv;
}

/* static */
ScriptFetchInfo* ScriptLoader::GetActiveScriptFetchInfo(JSContext* aCx) {
  JS::Value value = JS::GetScriptedCallerPrivate(aCx);
  if (value.isUndefined()) {
    return nullptr;
  }

  return static_cast<ScriptFetchInfo*>(value.toPrivate());
}

// static
void ScriptLoader::AssociateFetchInfoWithSystemClassicScript(
    JS::Handle<JSScript*> aScript, nsIURI* aURI) {
  RefPtr<ScriptFetchOptions> fetchOptions = ScriptFetchOptions::CreateDefault();
  fetchOptions->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
  RefPtr<ScriptFetchInfo> fetchInfo = new ScriptFetchInfo(
      ScriptKind::eClassic, ReferrerPolicy::_empty, fetchOptions, aURI);
  fetchInfo->AssociateWithScript(aScript);
}

void ScriptLoader::RegisterForDiskCache(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(!mCache);
  MOZ_ASSERT(aRequest->PassedConditionForDiskCache());
  MOZ_ASSERT(aRequest->HasStencil());
  MOZ_ASSERT(aRequest->getLoadedScript()->HasDiskCacheReference());
  MOZ_DIAGNOSTIC_ASSERT(!aRequest->isInList());
  MOZ_ASSERT(!IsWebExtensionRequest(aRequest),
             "Web extension scripts are not compatible with the disk cache");

  LoadedScript* loadedScript = aRequest->getLoadedScript();
  MOZ_ASSERT(loadedScript->IsTextSource(),
             "Serialized stencil case shouldn't be saved again");
  loadedScript->ConvertToCachedStencil(
      aRequest->GetStencil(), aRequest->ReferrerPolicy(), aRequest->BaseURL());
  mDiskCacheQueue.AppendElement(loadedScript);

  aRequest->ClearStencil();
}

void ScriptLoader::LoadEventFired() {
  mLoadEventFired = true;
  MaybeUpdateDiskCache();
}

void ScriptLoader::Destroy() {
  if (mCache) {
    mCache->UpdateEverHitTelemetry();
  }

  if (mObserver) {
    mObserver->Unregister();
    mObserver = nullptr;
  }

  CancelAndClearScriptLoadRequests();
  GiveUpDiskCaching();
  StopCollectingDelazifications();
}

void ScriptLoader::OnMemoryPressure() {
  // When memory pressure is detected, release the stencils held by the
  // ongoing delazification collection.
  // SharedScriptCache will also clear the cache, and then those stencils
  // will no longer held by anything live long and will be collected
  // shortly.

  StopCollectingDelazifications();
}

void ScriptLoader::StopCollectingDelazifications() {
  // NOTE: There's no difference between JS::FinishCollectingDelazifications
  //       and JS::AbortCollectingDelazifications, except for whether
  //       to return the stencil or not.
  //       The stencil pointer is held by LoadedScript, and thus we call
  //       JS::AbortCollectingDelazifications here, which is more light-weight.

  for (size_t i = 0; i < mDelazificationCollectingScripts.Length(); ++i) {
    JSScript* script = mDelazificationCollectingScripts[i];
    JS::AbortCollectingDelazifications(script);
  }
  mDelazificationCollectingScripts.Clear();

  for (size_t i = 0; i < mDelazificationCollectingModules.Length(); ++i) {
    JSObject* module = mDelazificationCollectingModules[i];
    JS::AbortCollectingDelazifications(module);
  }
  mDelazificationCollectingModules.Clear();

  mozilla::DropJSObjects(this);
}

void ScriptLoader::MaybeUpdateDiskCache() {
  // We wait for the load event to be fired before saving any script to the
  // disk cache. It is quite common to have load event listeners trigger more
  // JavaScript execution, that we want to save as part of disk cache, to
  // improve the load time in subsequent loads.
  if (!mLoadEventFired) {
    LOG(("ScriptLoader (%p): Wait for the load-end event to fire.", this));
    return;
  }

  // Wait until all scripts are loaded before saving to the disk cache, such
  // that we capture most of the intialization of the page.
  if (HasPendingRequests()) {
    LOG(("ScriptLoader (%p): Wait for other pending request to finish.", this));
    return;
  }

  if (mCache) {
    if (!mCache->MaybeScheduleUpdateDiskCache()) {
      TRACE_FOR_TEST_0("diskcache:noschedule");
    }

    StopCollectingDelazifications();
    return;
  }

  // If we already gave up, ensure that we are not going to enqueue any script,
  // and that we finalize them properly.
  if (mGiveUpDiskCaching) {
    LOG(("ScriptLoader (%p): Keep giving-up saving to the disk cache.", this));
    GiveUpDiskCaching();
    return;
  }

  // No need to fire any event if there is no script to be saved.
  if (mDiskCacheQueue.IsEmpty()) {
    LOG(("ScriptLoader (%p): No script in queue to be saved to the disk.",
         this));
    return;
  }

  // Create a new runnable dedicated to encoding all enqueued scripts when the
  // document is idle. In case of failure, we give-up on saving the disk cache.
  nsCOMPtr<nsIRunnable> encoder = NewRunnableMethod(
      "ScriptLoader::UpdateCache", this, &ScriptLoader::UpdateDiskCache);
  if (NS_FAILED(NS_DispatchToCurrentThreadQueue(encoder.forget(),
                                                EventQueuePriority::Idle))) {
    GiveUpDiskCaching();
    return;
  }

  StopCollectingDelazifications();

  LOG(("ScriptLoader (%p): Schedule the disk cache encoding.", this));
}

void ScriptLoader::UpdateDiskCache() {
  MOZ_ASSERT(!mCache);
  LOG(("ScriptLoader (%p): Start the disk cache encoding.", this));

  // If any script got added in the previous loop cycle, wait until all
  // remaining script executions are completed, such that we capture most of
  // the initialization.
  if (HasPendingRequests()) {
    return;
  }

  JS::FrontendContext* fc = JS::NewFrontendContext();
  if (!fc) {
    LOG(
        ("ScriptLoader (%p): Cannot create FrontendContext for the disk cache "
         "encoding.",
         this));
    return;
  }

  int32_t diskCacheMaxSizeInKb =
      StaticPrefs::browser_cache_disk_max_entry_size();

  for (auto& loadedScript : mDiskCacheQueue) {
    // The encoding is performed only when there was no disk cache stored in
    // the necko cache.
    if (!loadedScript->HasDiskCacheReference()) {
      continue;
    }

    if (loadedScript->IsInvalidatedCachedStencil()) {
      continue;
    }

    RefPtr<JS::Stencil> stencil = loadedScript->GetCachedStencil();

    Vector<uint8_t> compressed;
    if (!EncodeAndCompress(fc, loadedScript, stencil, loadedScript->SRI(),
                           compressed)) {
      loadedScript->DropDiskCacheReference();
      loadedScript->DropSRIOrSRIAndSerializedStencil();
      TRACE_FOR_TEST(loadedScript, "diskcache:failed");
      continue;
    }

    // The pref being -1 means "no limit".
    if (diskCacheMaxSizeInKb > 0) {
      size_t sourceLength = JS::GetScriptSourceLength(stencil);
      size_t expectedDiskCacheSize = sourceLength + compressed.length();
      if (expectedDiskCacheSize > size_t(diskCacheMaxSizeInKb) * 1024) {
        loadedScript->DropDiskCacheReference();
        loadedScript->DropSRIOrSRIAndSerializedStencil();
        TRACE_FOR_TEST(loadedScript, "diskcache:toolarge");
        continue;
      }
    }

    if (!SaveToDiskCache(loadedScript, compressed)) {
      loadedScript->DropDiskCacheReference();
      loadedScript->DropSRIOrSRIAndSerializedStencil();
      TRACE_FOR_TEST(loadedScript, "diskcache:failed");
      continue;
    }

    loadedScript->DropDiskCacheReference();
    loadedScript->DropSRIOrSRIAndSerializedStencil();
    TRACE_FOR_TEST(loadedScript, "diskcache:saved");
  }
  mDiskCacheQueue.Clear();

  JS::DestroyFrontendContext(fc);
}

/* static */
bool ScriptLoader::EncodeAndCompress(
    JS::FrontendContext* aFc, const JS::loader::LoadedScript* aLoadedScript,
    JS::Stencil* aStencil, const JS::TranscodeBuffer& aSRI,
    Vector<uint8_t>& aCompressed) {
  size_t alignedSRILength = aSRI.length();
  MOZ_ASSERT(JS::IsTranscodingBytecodeOffsetAligned(alignedSRILength));

  JS::TranscodeBuffer SRIAndSerializedStencil;
  if (!SRIAndSerializedStencil.appendAll(aSRI)) {
    LOG(("LoadedScript (%p): Cannot allocate buffer", aLoadedScript));
    return false;
  }

  MOZ_ASSERT(
      JS::IsTranscodingBytecodeOffsetAligned(LoadedScript::EncodingHeaderSize));

  if (!SRIAndSerializedStencil.growBy(LoadedScript::EncodingHeaderSize)) {
    LOG(("LoadedScript (%p): Cannot allocate buffer", aLoadedScript));
    return false;
  }

  LoadedScript::EncodingHeader header;
  aLoadedScript->WriteIntoEncodingHeader(&header);
  memcpy(SRIAndSerializedStencil.begin() + alignedSRILength, &header,
         LoadedScript::EncodingHeaderSize);

  JS::TranscodeResult result =
      JS::EncodeStencil(aFc, aStencil, SRIAndSerializedStencil);

  if (result != JS::TranscodeResult::Ok) {
    // Encoding can be aborted for any internal error.
    // We don't care the error and just give up encoding.
    JS::ClearFrontendErrors(aFc);

    LOG(("LoadedScript (%p): Cannot encode stencil", aLoadedScript));
    return false;
  }

  // TODO probably need to move this to a helper thread
  if (!ScriptBytecodeCompress(
          SRIAndSerializedStencil,
          alignedSRILength + LoadedScript::EncodingHeaderSize, aCompressed)) {
    return false;
  }

  if (aCompressed.length() >= UINT32_MAX) {
    LOG(
        ("LoadedScript (%p): Serialized stencil is too large to be decoded "
         "correctly.",
         aLoadedScript));
    return false;
  }

  return true;
}

/* static */
bool ScriptLoader::SaveToDiskCache(
    const JS::loader::LoadedScript* aLoadedScript,
    const Vector<uint8_t>& aCompressed) {
  MOZ_ASSERT(NS_IsMainThread());

  // Open the output stream to the cache entry alternate data storage. This
  // might fail if the stream is already open by another request, in which
  // case, we just ignore the current one.
  //
  // OpenAlternativeOutputStream doesn't immediately report errors on the
  // parent process, but instead it sets the error state and asynchronously
  // send it over IPC to report it as Write/Close result.  If all the
  // operations finish before the error arrives, no error will be reported.
  //
  // We don't wait for the parent process here because there's nothing we can
  // do for the error case.
  nsCOMPtr<nsIAsyncOutputStream> output;
  nsresult rv = aLoadedScript->mCacheEntry->OpenAlternativeOutputStream(
      BytecodeMimeTypeFor(aLoadedScript),
      static_cast<int64_t>(aCompressed.length()), getter_AddRefs(output));
  if (NS_FAILED(rv)) {
    LOG(
        ("LoadedScript (%p): Cannot open the disk cache (rv = %X, output "
         "= %p)",
         aLoadedScript, unsigned(rv), output.get()));
    return false;
  }
  MOZ_ASSERT(output);

  auto closeOutStream = mozilla::MakeScopeExit([&]() {
    rv = output->CloseWithStatus(rv);
    LOG(("LoadedScript (%p): Closing (rv = %X)", aLoadedScript, unsigned(rv)));
  });

  uint32_t n;
  rv = output->Write(reinterpret_cast<const char*>(aCompressed.begin()),
                     aCompressed.length(), &n);
  LOG(
      ("LoadedScript (%p): Write the disk cache (rv = %X, length = %u, "
       "written = %u)",
       aLoadedScript, unsigned(rv), unsigned(aCompressed.length()), n));
  if (NS_FAILED(rv)) {
    return false;
  }

  MOZ_RELEASE_ASSERT(aCompressed.length() == n);
  return true;
}

void ScriptLoader::GiveUpDiskCaching() {
  if (mCache) {
    // Disk cache is handled by SharedScriptCache.
    MOZ_ASSERT(mDiskCacheQueue.IsEmpty());
    MOZ_ASSERT(mDiskCacheableDependencyModules.isEmpty());
    return;
  }

  // If the document went away prematurely, we still want to set this, in order
  // to avoid queuing more scripts.
  mGiveUpDiskCaching = true;

  for (auto& loadedScript : mDiskCacheQueue) {
    LOG(("LoadedScript (%p): Giving up encoding the disk cache",
         loadedScript.get()));
    TRACE_FOR_TEST(loadedScript, "diskcache:giveup");

    loadedScript->DropDiskCacheReference();
    loadedScript->DropSRIOrSRIAndSerializedStencil();
  }
  mDiskCacheQueue.Clear();

  while (!mDiskCacheableDependencyModules.isEmpty()) {
    RefPtr<ScriptLoadRequest> request =
        mDiskCacheableDependencyModules.StealFirst();
  }
}

bool ScriptLoader::HasPendingRequests() const {
  return mParserBlockingRequest || !mXSLTRequests.isEmpty() ||
         !mLoadedAsyncRequests.isEmpty() ||
         !mNonAsyncExternalScriptInsertedRequests.isEmpty() ||
         !mDeferRequests.isEmpty() || HasPendingDynamicImports() ||
         !mPendingChildLoaders.IsEmpty();
  // mOffThreadCompilingRequests are already being processed.
}

bool ScriptLoader::HasPendingDynamicImports() const {
  if (mModuleLoader && mModuleLoader->HasPendingDynamicImports()) {
    return true;
  }

  for (ModuleLoader* loader : mWebExtModuleLoaders) {
    if (loader->HasPendingDynamicImports()) {
      return true;
    }
  }

  return false;
}

void ScriptLoader::ProcessPendingRequestsAsync() {
  if (HasPendingRequests()) {
    nsCOMPtr<nsIRunnable> task = NewRunnableMethod<bool>(
        "dom::ScriptLoader::ProcessPendingRequests", this,
        &ScriptLoader::ProcessPendingRequests, false);
    if (mDocument) {
      mDocument->Dispatch(task.forget());
    } else {
      NS_DispatchToCurrentThread(task.forget());
    }
  }
}

void ProcessPendingRequestsCallback(nsITimer* aTimer, void* aClosure)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  RefPtr<ScriptLoader> sl = static_cast<ScriptLoader*>(aClosure);
  sl->ProcessPendingRequests(true);
}

void ScriptLoader::ProcessPendingRequestsAsyncBypassParserBlocking() {
  MOZ_ASSERT(HasPendingRequests());

  if (!mProcessPendingRequestsAsyncBypassParserBlocking) {
    mProcessPendingRequestsAsyncBypassParserBlocking = NS_NewTimer();
  }

  // test_bug503481b.html tests the unlikely edge case where loading parser
  // blocking script depends on async script to be executed. So don't block
  // async scripts forever.
  mProcessPendingRequestsAsyncBypassParserBlocking->InitWithNamedFuncCallback(
      ProcessPendingRequestsCallback, this, 2500, nsITimer::TYPE_ONE_SHOT,
      "ProcessPendingRequestsAsyncBypassParserBlocking"_ns);
}

void ScriptLoader::ProcessPendingRequests(bool aAllowBypassingParserBlocking) {
  RefPtr<ScriptLoadRequest> request;

  if (mProcessPendingRequestsAsyncBypassParserBlocking) {
    mProcessPendingRequestsAsyncBypassParserBlocking->Cancel();
  }

  if (mParserBlockingRequest) {
    if (mParserBlockingRequest->IsFinished() &&
        ReadyToExecuteParserBlockingScripts()) {
      request.swap(mParserBlockingRequest);
      UnblockParser(request);
      ProcessRequest(request);
      ContinueParserAsync(request);
      ProcessPendingRequestsAsync();
      return;
    }

    if (!aAllowBypassingParserBlocking) {
      ProcessPendingRequestsAsyncBypassParserBlocking();
      return;
    }
  }

  while (ReadyToExecuteParserBlockingScripts() && !mXSLTRequests.isEmpty() &&
         mXSLTRequests.getFirst()->IsFinished()) {
    request = mXSLTRequests.StealFirst();
    ProcessRequest(request);
  }

  while (ReadyToExecuteScripts() && !mLoadedAsyncRequests.isEmpty()) {
    if (mLoadedAsyncRequests.getFirst()->TookLongInPreviousRuns() &&
        !mLoadedAsyncRequests.getFirst()->HadPostponed() && IsBeforeFCP()) {
      mLoadedAsyncRequests.getFirst()->SetHadPostponed();
      ProcessPendingRequestsAsync();
      return;
    }

    request = mLoadedAsyncRequests.StealFirst();
    if (request->IsModuleRequest()) {
      ProcessRequest(request);
    } else {
      CompileOffThreadOrProcessRequest(request);
    }
  }

  while (ReadyToExecuteScripts() &&
         !mNonAsyncExternalScriptInsertedRequests.isEmpty() &&
         mNonAsyncExternalScriptInsertedRequests.getFirst()->IsFinished()) {
    // Violate the HTML5 spec and execute these in the insertion order in
    // order to make LABjs and the "order" plug-in for RequireJS work with
    // their Gecko-sniffed code path. See
    // http://lists.w3.org/Archives/Public/public-html/2010Oct/0088.html
    request = mNonAsyncExternalScriptInsertedRequests.StealFirst();
    ProcessRequest(request);
  }

  if (mDeferCheckpointReached && mXSLTRequests.isEmpty()) {
    while (ReadyToExecuteScripts() && !mDeferRequests.isEmpty() &&
           mDeferRequests.getFirst()->IsFinished()) {
      if (mDeferRequests.getFirst()->TookLongInPreviousRuns() &&
          !mDeferRequests.getFirst()->HadPostponed() && IsBeforeFCP()) {
        mDeferRequests.getFirst()->SetHadPostponed();
        ProcessPendingRequestsAsync();
        return;
      }

      request = mDeferRequests.StealFirst();
      ProcessRequest(request);
    }
  }

  while (!mPendingChildLoaders.IsEmpty() &&
         ReadyToExecuteParserBlockingScripts()) {
    RefPtr<ScriptLoader> child = mPendingChildLoaders[0];
    mPendingChildLoaders.RemoveElementAt(0);
    child->RemoveParserBlockingScriptExecutionBlocker();
  }

  if (mDeferCheckpointReached && mDocument && !mParserBlockingRequest &&
      mNonAsyncExternalScriptInsertedRequests.isEmpty() &&
      mXSLTRequests.isEmpty() && mDeferRequests.isEmpty()) {
    const RefPtr<ScriptLoader> self = this;
    if (self->MaybeRemovedDeferRequests()) {
      return self->ProcessPendingRequests();
    }
  }

  if (mDeferCheckpointReached && mDocument && !mParserBlockingRequest &&
      mLoadingAsyncRequests.isEmpty() && mLoadedAsyncRequests.isEmpty() &&
      mNonAsyncExternalScriptInsertedRequests.isEmpty() &&
      mXSLTRequests.isEmpty() && mDeferRequests.isEmpty()) {
    // No more pending scripts; time to unblock onload.
    // OK to unblock onload synchronously here, since callers must be
    // prepared for the world changing anyway.
    mDeferCheckpointReached = false;
    mDocument->UnblockOnload(true);
  }
}

bool ScriptLoader::IsBeforeFCP() {
  if (mHadFCPDoNotUseDirectly) {
    return false;
  }

  if (mLoadEventFired) {
    return false;
  }

  if (!mDocument) {
    return false;
  }

  nsPresContext* context = mDocument->GetPresContext();
  if (!context) {
    return false;
  }

  if (context->HadFirstContentfulPaint()) {
    mHadFCPDoNotUseDirectly = true;
    return false;
  }

  return true;
}

bool ScriptLoader::ReadyToExecuteParserBlockingScripts() {
  // Make sure the SelfReadyToExecuteParserBlockingScripts check is first, so
  // that we don't block twice on an ancestor.
  if (!SelfReadyToExecuteParserBlockingScripts()) {
    return false;
  }

  if (mDocument && mDocument->GetWindowContext()) {
    for (WindowContext* wc =
             mDocument->GetWindowContext()->GetParentWindowContext();
         wc; wc = wc->GetParentWindowContext()) {
      if (Document* doc = wc->GetDocument()) {
        ScriptLoader* ancestor = doc->GetScriptLoader();
        if (ancestor && !ancestor->SelfReadyToExecuteParserBlockingScripts() &&
            ancestor->AddPendingChildLoader(this)) {
          AddParserBlockingScriptExecutionBlocker();
          return false;
        }
      }
    }
  }

  return true;
}

template <typename Unit>
static nsresult ConvertToUnicode(nsIChannel* aChannel, const uint8_t* aData,
                                 uint32_t aLength,
                                 const nsAString& aHintCharset,
                                 Document* aDocument, Unit*& aBufOut,
                                 size_t& aLengthOut) {
  if (!aLength) {
    aBufOut = nullptr;
    aLengthOut = 0;
    return NS_OK;
  }

  auto data = Span(aData, aLength);

  // The encoding info precedence is as follows from high to low:
  // The BOM
  // HTTP Content-Type (if name recognized)
  // charset attribute (if name recognized)
  // The encoding of the document

  UniquePtr<Decoder> unicodeDecoder;

  const Encoding* encoding;
  std::tie(encoding, std::ignore) = Encoding::ForBOM(data);
  if (encoding) {
    unicodeDecoder = encoding->NewDecoderWithBOMRemoval();
  }

  if (!unicodeDecoder && aChannel) {
    nsAutoCString label;
    if (NS_SUCCEEDED(aChannel->GetContentCharset(label)) &&
        (encoding = Encoding::ForLabel(label))) {
      unicodeDecoder = encoding->NewDecoderWithoutBOMHandling();
    }
  }

  if (!unicodeDecoder && (encoding = Encoding::ForLabel(aHintCharset))) {
    unicodeDecoder = encoding->NewDecoderWithoutBOMHandling();
  }

  if (!unicodeDecoder && aDocument) {
    unicodeDecoder =
        aDocument->GetDocumentCharacterSet()->NewDecoderWithoutBOMHandling();
  }

  if (!unicodeDecoder) {
    // Curiously, there are various callers that don't pass aDocument. The
    // fallback in the old code was ISO-8859-1, which behaved like
    // windows-1252.
    unicodeDecoder = WINDOWS_1252_ENCODING->NewDecoderWithoutBOMHandling();
  }

  auto signalOOM = mozilla::MakeScopeExit([&aBufOut, &aLengthOut]() {
    aBufOut = nullptr;
    aLengthOut = 0;
  });

  CheckedInt<size_t> bufferLength =
      ScriptDecoding<Unit>::MaxBufferLength(unicodeDecoder, aLength);
  if (!bufferLength.isValid()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  CheckedInt<size_t> bufferByteSize = bufferLength * sizeof(Unit);
  if (!bufferByteSize.isValid()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aBufOut = static_cast<Unit*>(js_malloc(bufferByteSize.value()));
  if (!aBufOut) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  signalOOM.release();
  aLengthOut = ScriptDecoding<Unit>::DecodeInto(
      unicodeDecoder, data, Span(aBufOut, bufferLength.value()),
      /* aEndOfSource = */ true);
  return NS_OK;
}

/* static */
nsresult ScriptLoader::ConvertToUTF16(
    nsIChannel* aChannel, const uint8_t* aData, uint32_t aLength,
    const nsAString& aHintCharset, Document* aDocument,
    UniquePtr<char16_t[], JS::FreePolicy>& aBufOut, size_t& aLengthOut) {
  char16_t* bufOut;
  nsresult rv = ConvertToUnicode(aChannel, aData, aLength, aHintCharset,
                                 aDocument, bufOut, aLengthOut);
  if (NS_SUCCEEDED(rv)) {
    aBufOut.reset(bufOut);
  }
  return rv;
}

/* static */
nsresult ScriptLoader::ConvertToUTF8(
    nsIChannel* aChannel, const uint8_t* aData, uint32_t aLength,
    const nsAString& aHintCharset, Document* aDocument,
    UniquePtr<Utf8Unit[], JS::FreePolicy>& aBufOut, size_t& aLengthOut) {
  Utf8Unit* bufOut;
  nsresult rv = ConvertToUnicode(aChannel, aData, aLength, aHintCharset,
                                 aDocument, bufOut, aLengthOut);
  if (NS_SUCCEEDED(rv)) {
    aBufOut.reset(bufOut);
  }
  return rv;
}

nsresult ScriptLoader::OnStreamComplete(
    nsIChannel* aChannel, ScriptLoadRequest* aRequest, nsresult aChannelStatus,
    nsresult aSRIStatus, SRICheckDataVerifier* aSRIDataVerifier) {
  NS_ASSERTION(aRequest, "null request in stream complete handler");
  NS_ENSURE_TRUE(aRequest, NS_ERROR_FAILURE);

  if (aRequest->IsCanceled()) {
    return NS_BINDING_ABORTED;
  }

  nsresult rv = VerifySRI(aRequest, aChannel, aSRIStatus, aSRIDataVerifier);

  if (NS_SUCCEEDED(rv)) {
    // These flags can be modified by the CacheEntryRevived.
    bool IsFetchedAsTextSource = aRequest->IsFetchedAsTextSource();
    bool IsRetrievedAsSerializedStencil =
        aRequest->IsRetrievedAsSerializedStencil();

    nsCOMPtr<nsICacheInfoChannel> cacheInfo = do_QueryInterface(aChannel);
    nsCOMPtr<nsICacheEntryWriteHandle> cacheEntry;
    if (cacheInfo && NS_SUCCEEDED(cacheInfo->GetCacheEntryWriteHandle(
                         getter_AddRefs(cacheEntry)))) {
      uint64_t id;
      nsresult rv = cacheInfo->GetCacheEntryId(&id);
      if (NS_SUCCEEDED(rv)) {
        LOG(("ScriptLoadRequest (%p): cacheEntryId = %zx", aRequest,
             size_t(id)));

        if (aRequest->HasDirtyCache()) {
          // This request found a dirty cache.
          // Validate the cache with the response's cache ID.
          ScriptHashKey key(this, aRequest, aRequest->ReferrerPolicy(),
                            aRequest->FetchOptions(), aRequest->URI());
          auto cacheResult = mCache->Lookup(*this, key, /* aSyncLoad = */ true);
          MOZ_ASSERT_IF(
              cacheResult.mState == CachedSubResourceState::Complete,
              cacheResult.mCompleteValue->IsCachedStencil() ||
                  cacheResult.mCompleteValue->IsInvalidatedCachedStencil());
          if (cacheResult.mState == CachedSubResourceState::Complete &&
              cacheResult.mCompleteValue->IsCachedStencil() &&
              cacheResult.mCompleteValue->IsSRIMetadataReusableBy(
                  aRequest->mIntegrity) &&
              cacheResult.mCompleteValue->CacheEntryId() == id) {
            cacheResult.mCompleteValue->UnsetDirty();
            // This keeps the request as "fetching" state.
            // PrepareLoadedRequest below will set it to "ready" state.
            //
            // Off-thread compilation is skipped for the revived cache.
            // See AttemptOffThreadScriptCompile.
            //
            // Main thread compilation is skipped in the same way as
            // non-dirty cache.
            aRequest->CacheEntryRevived(cacheResult.mCompleteValue);

            MOZ_ASSERT(aRequest->IsFetching());

            cacheResult.mCompleteValue->AddFetchCount();

            TRACE_FOR_TEST(aRequest, "memorycache:dirty:revived");
            mMemoryCacheRevived++;
          } else {
            mCache->Evict(key);
            TRACE_FOR_TEST(aRequest, "memorycache:dirty:evicted");
            mMemoryCacheEvictedDirty++;
          }
        }

        aRequest->getLoadedScript()->SetCacheEntryId(id);
      }

      // If we are loading from source, store the cache info channel and
      // save the computed SRI hash or a dummy SRI hash in case we are going to
      // save the this script in the disk cache.
      if (!aRequest->IsRetrievedFromMemoryCache() && IsFetchedAsTextSource &&
          StaticPrefs::dom_script_loader_bytecode_cache_enabled()) {
        uint32_t fetchCount;
        if (NS_SUCCEEDED(cacheInfo->GetCacheTokenFetchCount(&fetchCount))) {
          if (fetchCount < UINT8_MAX) {
            aRequest->getLoadedScript()->mFetchCount = fetchCount;
          } else {
            aRequest->getLoadedScript()->mFetchCount = UINT8_MAX;
          }
        }

        aRequest->getLoadedScript()->mCacheEntry = cacheEntry;
        LOG(("ScriptLoadRequest (%p): nsICacheEntryWriteHandle = %p", aRequest,
             (void*)cacheEntry));

        rv = SaveSRIHash(aRequest, aSRIDataVerifier);
      }
    }

    if (IsFetchedAsTextSource) {
      mLoadedFromNeckoAsText++;
    } else if (IsRetrievedAsSerializedStencil) {
      mLoadedFromNeckoAsSerializedStencil++;
    }
    // NOTE: IsCachedStencil() and IsInvalidatedCachedStencil() cases are
    //       handled above by incrementing mMemoryCacheRevived.

    if (NS_SUCCEEDED(rv)) {
      rv = PrepareLoadedRequest(aRequest, aChannel, aChannelStatus);
    }

    if (NS_FAILED(rv)) {
      aRequest->getLoadedScript()->DropDiskCacheReference();
      ReportErrorToConsole(aRequest, rv);
    }
  }

  if (NS_FAILED(rv)) {
    // When loading the disk cache, we verify the SRI hash. If it does not match
    // the one from the document we restart the load, forcing us to load the
    // source instead. If this happens do not remove the current request from
    // script loader's data structures or fire any events.
    if (aChannelStatus != NS_BINDING_RETARGETED) {
      HandleLoadError(aRequest, rv);
    }
  }

  // Process our request and/or any pending ones
  ProcessPendingRequests();

  return rv;
}

nsresult ScriptLoader::VerifySRI(ScriptLoadRequest* aRequest,
                                 nsIChannel* aChannel, nsresult aSRIStatus,
                                 SRICheckDataVerifier* aSRIDataVerifier) const {
  nsresult rv = NS_OK;

  if (!aRequest->mIntegrity.IsEmpty() && NS_SUCCEEDED((rv = aSRIStatus))) {
    MOZ_ASSERT(aSRIDataVerifier);
    MOZ_ASSERT(mReporter);
    rv = aSRIDataVerifier->Verify(aRequest->mIntegrity, aChannel, mReporter);

    mReporter->FlushReportsToConsole(
        nsContentUtils::GetInnerWindowID(aChannel));

    if (NS_FAILED(rv)) {
      rv = NS_ERROR_SRI_CORRUPT;
      TRACE_FOR_TEST(aRequest, "sri:corrupt");
    }
  }

  return rv;
}

nsresult ScriptLoader::SaveSRIHash(
    ScriptLoadRequest* aRequest, SRICheckDataVerifier* aSRIDataVerifier) const {
  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  JS::TranscodeBuffer& sri = aRequest->SRI();
  MOZ_ASSERT(sri.empty());

  uint32_t len = 0;

  // If the integrity metadata does not correspond to a valid hash function,
  // IsComplete would be false.
  if (!aRequest->mIntegrity.IsEmpty() && aSRIDataVerifier->IsComplete()) {
    MOZ_ASSERT(sri.length() == 0);

    // Encode the SRI computed hash.
    len = aSRIDataVerifier->DataSummaryLength();

    if (!sri.resize(len)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    DebugOnly<nsresult> res =
        aSRIDataVerifier->ExportDataSummary(len, sri.begin());
    MOZ_ASSERT(NS_SUCCEEDED(res));
  } else {
    MOZ_ASSERT(sri.length() == 0);

    // Encode a dummy SRI hash.
    len = SRICheckDataVerifier::EmptyDataSummaryLength();

    if (!sri.resize(len)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    DebugOnly<nsresult> res =
        SRICheckDataVerifier::ExportEmptyDataSummary(len, sri.begin());
    MOZ_ASSERT(NS_SUCCEEDED(res));
  }

  // Verify that the exported and predicted length correspond.
  DebugOnly<uint32_t> srilen{};
  MOZ_ASSERT(NS_SUCCEEDED(
      SRICheckDataVerifier::DataSummaryLength(len, sri.begin(), &srilen)));
  MOZ_ASSERT(srilen == len);

  MOZ_ASSERT(sri.length() == len);

  size_t alignedSRILength = JS::AlignTranscodingBytecodeOffset(len);
  if (alignedSRILength != len) {
    if (!sri.resize(alignedSRILength)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  aRequest->SetAlignedSRILength(alignedSRILength);

  return NS_OK;
}

void ScriptLoader::ReportErrorToConsole(ScriptLoadRequest* aRequest,
                                        nsresult aResult) const {
  MOZ_ASSERT(aRequest);

  if (aRequest->GetScriptLoadContext()->IsPreload()) {
    // Skip reporting errors in preload requests. If the request is actually
    // used then we will report the error in ReportPreloadErrorsToConsole below.
    aRequest->GetScriptLoadContext()->mUnreportedPreloadError = aResult;
    return;
  }

  if (!mDocument) {
    return;
  }

  bool isScript = !aRequest->IsModuleRequest();
  const char* message;
  if (aResult == NS_ERROR_MALFORMED_URI) {
    message = isScript ? "ScriptSourceMalformed" : "ModuleSourceMalformed";
  } else if (aResult == NS_ERROR_DOM_BAD_URI) {
    message = isScript ? "ScriptSourceNotAllowed" : "ModuleSourceNotAllowed";
  } else if (aResult == NS_ERROR_DOM_WEBEXT_CONTENT_SCRIPT_URI) {
    MOZ_ASSERT(!isScript);
    message = "WebExtContentScriptModuleSourceNotAllowed";
  } else if (net::ChannelClassifierUtils::IsClassifierBlockingErrorCode(
                 aResult)) {
    // Blocking classifier error codes already show their own console messages.
    return;
  } else {
    message = isScript ? "ScriptSourceLoadFailed" : "ModuleSourceLoadFailed";
  }

  AutoTArray<nsString, 1> params;
  CopyUTF8toUTF16(aRequest->URI()->GetSpecOrDefault(), *params.AppendElement());

  Maybe<SourceLocation> loc;
  if (!isScript && !aRequest->IsTopLevel()) {
    MOZ_ASSERT(aRequest->mReferrer);
    loc.emplace(aRequest->mReferrer.get());
  } else {
    uint32_t lineNo = aRequest->GetScriptLoadContext()->GetScriptLineNumber();
    JS::ColumnNumberOneOrigin columnNo =
        aRequest->GetScriptLoadContext()->GetScriptColumnNumber();
    loc.emplace(mDocument->GetDocumentURI(), lineNo, columnNo.oneOriginValue());
  }

  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Script Loader"_ns, mDocument,
      PropertiesFile::DOM_PROPERTIES, message, params, loc.ref());
}

void ScriptLoader::ReportWarningToConsole(
    ScriptLoadRequest* aRequest, const char* aMessageName,
    const nsTArray<nsString>& aParams) const {
  if (!mDocument) {
    return;
  }
  uint32_t lineNo = aRequest->GetScriptLoadContext()->GetScriptLineNumber();
  JS::ColumnNumberOneOrigin columnNo =
      aRequest->GetScriptLoadContext()->GetScriptColumnNumber();
  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Script Loader"_ns, mDocument,
      PropertiesFile::DOM_PROPERTIES, aMessageName, aParams,
      SourceLocation{mDocument->GetDocumentURI(), lineNo,
                     columnNo.oneOriginValue()});
}

void ScriptLoader::ReportPreloadErrorsToConsole(ScriptLoadRequest* aRequest) {
  if (NS_FAILED(aRequest->GetScriptLoadContext()->mUnreportedPreloadError)) {
    ReportErrorToConsole(
        aRequest, aRequest->GetScriptLoadContext()->mUnreportedPreloadError);
    aRequest->GetScriptLoadContext()->mUnreportedPreloadError = NS_OK;
  }

  // TODO:
  // Bug 1973466, check the child request's error that happened during
  // preload is reported.
}

void ScriptLoader::HandleLoadError(ScriptLoadRequest* aRequest,
                                   nsresult aResult) {
  /*
   * Handle script not loading error because source was an tracking URL (or
   * fingerprinting, cryptomining, etc).
   * We make a note of this script node by including it in a dedicated
   * array of blocked tracking nodes under its parent document.
   */
  if (net::ChannelClassifierUtils::IsClassifierBlockingErrorCode(aResult)) {
    nsCOMPtr<nsIContent> cont = do_QueryInterface(
        aRequest->GetScriptLoadContext()->GetScriptElementForUrlClassifier());
    mDocument->AddBlockedNodeByClassifier(cont);
  }

  bool wasHandled = false;

  // A ModuleLoadRequest will be stored either in mDeferRequests or
  // mLoadingAsyncRequests, but the onerror handler should be triggered later in
  // ProcessRequests, so we handle ModuleLoadRequest before mDeferRequestrs and
  // mLoadingAsyncRequests.
  if (aRequest->IsModuleRequest()) {
    MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mIsInline);
    wasHandled = true;

    ModuleLoadRequest* modReq = aRequest->AsModuleRequest();
    modReq->OnFetchComplete(aResult);

    MOZ_ASSERT(modReq->IsErrored());
  } else if (aRequest->GetScriptLoadContext()->mInDeferList) {
    wasHandled = true;
    if (aRequest->isInList()) {
      RefPtr<ScriptLoadRequest> req = mDeferRequests.Steal(aRequest);
      FireScriptAvailable(aResult, req);
    }
  } else if (aRequest->GetScriptLoadContext()->mInAsyncList) {
    wasHandled = true;
    if (aRequest->isInList()) {
      RefPtr<ScriptLoadRequest> req = mLoadingAsyncRequests.Steal(aRequest);
      FireScriptAvailable(aResult, req);
    }
  }

  if (aRequest->GetScriptLoadContext()->mIsNonAsyncScriptInserted) {
    if (aRequest->isInList()) {
      RefPtr<ScriptLoadRequest> req =
          mNonAsyncExternalScriptInsertedRequests.Steal(aRequest);
      FireScriptAvailable(aResult, req);
    }
  } else if (aRequest->GetScriptLoadContext()->mIsXSLT) {
    if (aRequest->isInList()) {
      RefPtr<ScriptLoadRequest> req = mXSLTRequests.Steal(aRequest);
      FireScriptAvailable(aResult, req);
    }
  } else if (aRequest->GetScriptLoadContext()->IsPreload()) {
    if (aRequest->IsTopLevel()) {
      // Request may already have been removed by
      // CancelAndClearScriptLoadRequests.
      mPreloads.RemoveElement(aRequest, PreloadRequestComparator());
    }
    MOZ_ASSERT(!aRequest->isInList());
  } else if (mParserBlockingRequest == aRequest) {
    MOZ_ASSERT(!aRequest->isInList());
    mParserBlockingRequest = nullptr;
    UnblockParser(aRequest);

    // Ensure that we treat the script as our current parser-inserted script
    // while firing onerror on it.
    MOZ_ASSERT(aRequest->GetScriptLoadContext()->GetParserCreated());
    nsCOMPtr<nsIScriptElement> oldParserInsertedScript =
        mCurrentParserInsertedScript;
    mCurrentParserInsertedScript =
        aRequest->GetScriptLoadContext()
            ->GetScriptElementForCurrentParserInsertedScript();
    FireScriptAvailable(aResult, aRequest);
    ContinueParserAsync(aRequest);
    mCurrentParserInsertedScript = std::move(oldParserInsertedScript);
  } else if (!wasHandled) {
    // This happens for blocking requests cancelled by ParsingComplete().
    // Ignore cancellation status for link-preload requests, as cancellation can
    // be omitted for them when SRI is stronger on consumer tags.
    MOZ_ASSERT(aRequest->IsCanceled() ||
               aRequest->GetScriptLoadContext()->IsLinkPreloadScript());
    MOZ_ASSERT(!aRequest->isInList());
  }
}

void ScriptLoader::HandleLoadErrorAndProcessPendingRequests(
    ScriptLoadRequest* aRequest, nsresult aResult) {
  HandleLoadError(aRequest, aResult);
  // Process in case some other requests have finished meanwhile.
  ProcessPendingRequests();
}

void ScriptLoader::UnblockParser(ScriptLoadRequest* aParserBlockingRequest) {
  aParserBlockingRequest->GetScriptLoadContext()->UnblockParser();
}

void ScriptLoader::ContinueParserAsync(
    ScriptLoadRequest* aParserBlockingRequest) {
  aParserBlockingRequest->GetScriptLoadContext()->ContinueParserAsync();
}

uint32_t ScriptLoader::NumberOfProcessors() {
  if (mNumberOfProcessors > 0) {
    return mNumberOfProcessors;
  }

  int32_t numProcs = PR_GetNumberOfProcessors();
  if (numProcs > 0) {
    mNumberOfProcessors = numProcs;
  }
  return mNumberOfProcessors;
}

int32_t ScriptLoader::PhysicalSizeOfMemoryInGB() {
  // 0 is a valid result from PR_GetPhysicalMemorySize() which
  // means a failure occured.
  if (mPhysicalSizeOfMemory >= 0) {
    return mPhysicalSizeOfMemory;
  }

  // Save the size in GB.
  mPhysicalSizeOfMemory =
      static_cast<int32_t>(PR_GetPhysicalMemorySize() >> 30);
  return mPhysicalSizeOfMemory;
}

bool ScriptLoader::ShouldApplyDelazifyStrategy(ScriptLoadRequest* aRequest) {
  // Full parse everything if negative.
  if (StaticPrefs::dom_script_loader_delazification_max_size() < 0) {
    return true;
  }

  // Be conservative on machines with 2GB or less of memory.
  if (PhysicalSizeOfMemoryInGB() <=
      StaticPrefs::dom_script_loader_delazification_min_mem()) {
    return false;
  }

  uint32_t max_size = static_cast<uint32_t>(
      StaticPrefs::dom_script_loader_delazification_max_size());
  uint32_t script_size =
      aRequest->ScriptTextLength() > 0
          ? static_cast<uint32_t>(aRequest->ScriptTextLength())
          : 0;

  if (mTotalFullParseSize + script_size < max_size) {
    return true;
  }

  if (LOG_ENABLED()) {
    nsCString url = aRequest->URI()->GetSpecOrDefault();
    LOG(
        ("ScriptLoadRequest (%p): non-on-demand-only Parsing Disabled for (%s) "
         "with size=%u because mTotalFullParseSize=%u would exceed max_size=%u",
         aRequest, url.get(), script_size, mTotalFullParseSize, max_size));
  }

  return false;
}

void ScriptLoader::ApplyDelazifyStrategy(JS::CompileOptions* aOptions) {
  JS::DelazificationOption strategy =
      JS::DelazificationOption::ParseEverythingEagerly;
  uint32_t strategyIndex =
      StaticPrefs::dom_script_loader_delazification_strategy();

  // Assert that all enumerated values of DelazificationOption are dense between
  // OnDemandOnly and ParseEverythingEagerly.
#ifdef DEBUG
  uint32_t count = 0;
  uint32_t mask = 0;
#  define _COUNT_ENTRIES(Name) count++;
#  define _MASK_ENTRIES(Name) \
    mask |= 1 << uint32_t(JS::DelazificationOption::Name);

  FOREACH_DELAZIFICATION_STRATEGY(_COUNT_ENTRIES);
  MOZ_ASSERT(count == uint32_t(strategy) + 1);
  FOREACH_DELAZIFICATION_STRATEGY(_MASK_ENTRIES);
  MOZ_ASSERT(((mask + 1) & mask) == 0);
#  undef _COUNT_ENTRIES
#  undef _MASK_ENTRIES
#endif

  // Any strategy index larger than ParseEverythingEagerly would default to
  // ParseEverythingEagerly.
  if (strategyIndex <= uint32_t(strategy)) {
    strategy = JS::DelazificationOption(uint8_t(strategyIndex));
  }

  aOptions->setEagerDelazificationStrategy(strategy);
}

bool ScriptLoader::ShouldCompileOffThread(ScriptLoadRequest* aRequest) {
  if (NumberOfProcessors() <= 1) {
    return false;
  }
  if (aRequest == mParserBlockingRequest) {
    return true;
  }
  if (SpeculativeOMTParsingEnabled()) {
    // Processing non async inserted scripts too early can potentially delay the
    // load event from firing so focus on other scripts instead.
    if (aRequest->GetScriptLoadContext()->mIsNonAsyncScriptInserted &&
        !StaticPrefs::
            dom_script_loader_external_scripts_speculate_non_parser_inserted_enabled()) {
      return false;
    }

    // Async and link preload scripts do not need to be parsed right away.
    if (aRequest->GetScriptLoadContext()->IsAsyncScript() &&
        !StaticPrefs::
            dom_script_loader_external_scripts_speculate_async_enabled()) {
      return false;
    }

    if (aRequest->GetScriptLoadContext()->IsLinkPreloadScript() &&
        !StaticPrefs::
            dom_script_loader_external_scripts_speculate_link_preload_enabled()) {
      return false;
    }

    return true;
  }
  return false;
}

static bool MimeTypeMatchesExpectedModuleType(
    nsIChannel* aChannel, JS::ModuleType expectedModuleType) {
  nsAutoCString mimeType;
  aChannel->GetContentType(mimeType);
  NS_ConvertUTF8toUTF16 typeString(mimeType);

  switch (expectedModuleType) {
    case JS::ModuleType::JavaScriptOrWasm:
#ifdef NIGHTLY_BUILD
      if (StaticPrefs::javascript_options_experimental_wasm_esm_integration()) {
        return nsContentUtils::IsJavascriptMIMEType(typeString) ||
               nsContentUtils::HasWasmMimeTypeEssence(typeString);
      }
#endif
      return nsContentUtils::IsJavascriptMIMEType(typeString);
    case JS::ModuleType::JSON:
      return nsContentUtils::IsJsonMimeType(typeString);
    case JS::ModuleType::CSS:
      return nsContentUtils::HasCssMimeTypeEssence(typeString);
    case JS::ModuleType::Text:
      return true;
    case JS::ModuleType::Unknown:
    case JS::ModuleType::Bytes:
      break;
  }

  return false;
}

nsresult ScriptLoader::PrepareLoadedRequest(ScriptLoadRequest* aRequest,
                                            nsIChannel* aChannel,
                                            nsresult aStatus) {
  PROFILER_MARKER("ScriptLoader::PrepareLoadedRequest", JS,
                  {mozilla::MarkerStack::Capture()}, FlowMarker,
                  Flow::FromPointer(aRequest));

  if (NS_FAILED(aStatus)) {
    return aStatus;
  }

  MOZ_ASSERT(aRequest->IsFetching());
  CollectScriptTelemetry(aRequest);

  // If we don't have a document, then we need to abort further
  // evaluation.
  if (!mDocument) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // If the load returned an error page, then we need to abort
  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    bool requestSucceeded;
    if (NS_SUCCEEDED(httpChannel->GetRequestSucceeded(&requestSucceeded)) &&
        !requestSucceeded) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    if (aRequest->IsModuleRequest()) {
      // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-single-module-script
      // Update script's referrer-policy if there's a Referrer-Policy header in
      // the HTTP response.
      ReferrerPolicy policy =
          nsContentUtils::GetReferrerPolicyFromChannel(httpChannel);
      if (policy != ReferrerPolicy::_empty) {
        aRequest->AsModuleRequest()->UpdateReferrerPolicy(policy);
      }
    }

    nsAutoCString sourceMapURL;
    if (nsContentUtils::GetSourceMapURL(httpChannel, sourceMapURL)) {
      aRequest->SetSourceMapURL(NS_ConvertUTF8toUTF16(sourceMapURL));
    }

    nsCOMPtr<nsIClassifiedChannel> classifiedChannel =
        do_QueryInterface(aChannel);
    MOZ_ASSERT(classifiedChannel);
    if (classifiedChannel &&
        classifiedChannel->IsThirdPartyTrackingResource()) {
      net::ClassificationFlags flags{
          classifiedChannel->GetFirstPartyClassificationFlags(),
          classifiedChannel->GetThirdPartyClassificationFlags()};
      aRequest->GetScriptLoadContext()->SetClassificationFlags(flags);
    }
  }

  // If this load was subject to a CORS check, don't flag it with a separate
  // origin principal, so that it will treat our document's principal as the
  // origin principal.  Module loads always use CORS.
  if (!aRequest->IsModuleRequest() && aRequest->CORSMode() == CORS_NONE) {
    MOZ_TRY(nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
        aChannel, getter_AddRefs(aRequest->mOriginPrincipal)));
  }

  // This assertion could fire errorously if we ran out of memory when
  // inserting the request in the array. However it's an unlikely case
  // so if you see this assertion it is likely something else that is
  // wrong, especially if you see it more than once.
  NS_ASSERTION(mDeferRequests.Contains(aRequest) ||
                   mLoadingAsyncRequests.Contains(aRequest) ||
                   mNonAsyncExternalScriptInsertedRequests.Contains(aRequest) ||
                   mXSLTRequests.Contains(aRequest) ||
                   (aRequest->IsModuleRequest() &&
                    (aRequest->AsModuleRequest()->IsRegisteredDynamicImport() ||
                     !aRequest->AsModuleRequest()->IsTopLevel())) ||
                   mPreloads.Contains(aRequest, PreloadRequestComparator()) ||
                   mParserBlockingRequest == aRequest,
               "aRequest should be pending!");

  nsCOMPtr<nsIURI> uri;
  MOZ_TRY(aChannel->GetOriginalURI(getter_AddRefs(uri)));

  aRequest->SetBaseURLFromChannelAndOriginalURI(aChannel, uri);

  if (aRequest->IsModuleRequest()) {
    ModuleLoadRequest* request = aRequest->AsModuleRequest();

    // When loading a module, only responses with an expected MIME type are
    // acceptable.
    if (!MimeTypeMatchesExpectedModuleType(aChannel, request->mModuleType)) {
      if (LOG_ENABLED()) {
        nsAutoCString spec;
        uri->GetAsciiSpec(spec);
        LOG(
            ("ScriptLoadRequest (%p): MimeTypeMatchesExpectedModuleType check "
             "failed for: %s",
             aRequest, spec.get()));
      }
      return NS_ERROR_FAILURE;
    }

    // Attempt to compile off main thread.
    bool couldCompile = false;
    MOZ_TRY(AttemptOffThreadScriptCompile(request, &couldCompile));
    if (couldCompile) {
      return NS_OK;
    }

    // Otherwise compile it right away and start fetching descendents.
    request->OnFetchComplete(NS_OK);
    return NS_OK;
  }

  // The script is now loaded and ready to run.
  aRequest->SetReady();

  // If speculative parsing is enabled attempt to compile all
  // external scripts off-main-thread.  Otherwise, only omt compile scripts
  // blocking the parser.
  if (ShouldCompileOffThread(aRequest)) {
    MOZ_ASSERT(!aRequest->IsModuleRequest());
    bool couldCompile = false;
    MOZ_TRY(AttemptOffThreadScriptCompile(aRequest, &couldCompile));
    if (couldCompile) {
      MOZ_ASSERT(aRequest->mState == ScriptLoadRequest::State::Compiling,
                 "Request should be off-thread compiling now.");
      return NS_OK;
    }

    // If off-thread compile was rejected, continue with regular processing.
  }

  MaybeMoveToLoadedList(aRequest);

  return NS_OK;
}

void ScriptLoader::DeferCheckpointReached() {
  if (mDeferEnabled) {
    // Have to check because we apparently get ParsingComplete
    // without BeginDeferringScripts in some cases
    mDeferCheckpointReached = true;
  }

  mDeferEnabled = false;
  ProcessPendingRequests();
}

void ScriptLoader::ParsingComplete(bool aTerminated) {
  if (aTerminated) {
    CancelAndClearScriptLoadRequests();

    // Have to call this even if aTerminated so we'll correctly unblock onload.
    DeferCheckpointReached();
  }
}

void ScriptLoader::PreloadURI(
    nsIURI* aURI, const nsAString& aCharset, const nsAString& aType,
    const nsAString& aCrossOrigin, const nsAString& aNonce,
    const nsAString& aFetchPriority, const nsAString& aIntegrity,
    bool aScriptFromHead, bool aAsync, bool aDefer, bool aLinkPreload,
    const ReferrerPolicy aReferrerPolicy, uint64_t aEarlyHintPreloaderId) {
  NS_ENSURE_TRUE_VOID(mDocument);
  // Check to see if scripts has been turned off.
  if (!mEnabled || !mDocument->IsScriptEnabled()) {
    return;
  }

  ScriptKind scriptKind = ScriptKind::eClassic;

  static const char kASCIIWhitespace[] = "\t\n\f\r ";

  nsAutoString type(aType);
  type.Trim(kASCIIWhitespace);
  if (type.LowerCaseEqualsASCII("module")) {
    scriptKind = ScriptKind::eModule;
  }

  if (scriptKind == ScriptKind::eClassic && !aType.IsEmpty() &&
      !nsContentUtils::IsJavascriptMIMEType(aType)) {
    // Unknown type.  Don't load it.
    return;
  }

  SRIMetadata sriMetadata;
  GetSRIMetadata(aIntegrity, &sriMetadata);
  if (aIntegrity.IsVoid() && scriptKind == ScriptKind::eModule) {
    mModuleLoader->GetImportMapSRI(aURI, mDocument->GetDocumentURIAsReferrer(),
                                   mReporter, &sriMetadata);
  }

  const auto requestPriority = FetchPriorityToRequestPriority(
      nsGenericHTMLElement::ToFetchPriority(aFetchPriority));

  const Encoding* classicScriptHintEncoding = nullptr;
  if (scriptKind == ScriptKind::eClassic) {
    classicScriptHintEncoding = Encoding::ForLabel(aCharset);
  }

  // For link type "modulepreload":
  // https://html.spec.whatwg.org/multipage/links.html#link-type-modulepreload
  // Step 11. Let options be a script fetch options whose cryptographic nonce is
  // cryptographic nonce, integrity metadata is integrity metadata, parser
  // metadata is "not-parser-inserted", credentials mode is credentials mode,
  // referrer policy is referrer policy, and fetch priority is fetch priority.
  //
  // We treat speculative <script> loads as parser-inserted, because they
  // come from a parser. This will also match how they should be treated
  // as a normal load.
  RefPtr<ScriptLoadRequest> request = CreateLoadRequest(
      scriptKind, aURI, nullptr, VoidString(), mDocument->NodePrincipal(),
      Element::StringToCORSMode(aCrossOrigin), aNonce, requestPriority,
      sriMetadata, aReferrerPolicy,
      aLinkPreload ? ParserMetadata::NotParserInserted
                   : ParserMetadata::ParserInserted,
      ScriptLoadRequestType::Preload, classicScriptHintEncoding);
  request->GetScriptLoadContext()->mIsInline = false;
  request->GetScriptLoadContext()->mScriptFromHead = aScriptFromHead;
  request->GetScriptLoadContext()->SetScriptMode(aDefer, aAsync, aLinkPreload);
  request->GetScriptLoadContext()->SetIsPreloadRequest();
  request->mEarlyHintPreloaderId = aEarlyHintPreloaderId;

  if (LOG_ENABLED()) {
    nsAutoCString url;
    aURI->GetAsciiSpec(url);
    LOG(("ScriptLoadRequest (%p): Created preload request for %s",
         request.get(), url.get()));
  }

  nsresult rv = StartLoad(request);
  if (NS_FAILED(rv)) {
    return;
  }

  // For a <link rel="modulepreload">, if the module is already fetching,
  // fetched, or in the in-memory navigation cache, no channel is created for a
  // network request, and so StartLoadInternal has not registered the request
  // with the PreloadService. We use context->Channel() to identify these cases,
  // and have to register before we return: PreloadService::PreloadOrCoalesce
  // looks the preload up by key right after this and would otherwise find
  // nothing and fire a spurious error event.
  if (request->IsModuleRequest()) {
    ModuleLoadRequest* moduleRequest = request->AsModuleRequest();
    ScriptLoadContext* context = moduleRequest->GetScriptLoadContext();
    MOZ_ASSERT_IF(moduleRequest->IsRetrievedFromMemoryCache(),
                  !context->Channel());
    if (context->IsLinkPreloadScript() && !context->Channel()) {
      NotifyPreloadCoalescing(moduleRequest);
    }
  }

  PreloadInfo* pi = mPreloads.AppendElement();
  pi->mRequest = request;
}

void ScriptLoader::AddDeferRequest(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->GetScriptLoadContext()->IsDeferredScript());
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mInDeferList &&
             !aRequest->GetScriptLoadContext()->mInAsyncList);
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mInCompilingList);

  aRequest->GetScriptLoadContext()->mInDeferList = true;
  mDeferRequests.AppendElement(aRequest);
  if (mDeferEnabled && aRequest == mDeferRequests.getFirst() && mDocument &&
      !mBlockingDOMContentLoaded) {
    MOZ_ASSERT(mDocument->GetReadyStateEnum() == Document::READYSTATE_LOADING);
    mBlockingDOMContentLoaded = true;
    mDocument->BlockDOMContentLoaded();
  }
}

void ScriptLoader::AddAsyncRequest(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->GetScriptLoadContext()->IsAsyncScript());
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mInDeferList &&
             !aRequest->GetScriptLoadContext()->mInAsyncList);
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mInCompilingList);

  aRequest->GetScriptLoadContext()->mInAsyncList = true;
  if (aRequest->IsFinished()) {
    mLoadedAsyncRequests.AppendElement(aRequest);
  } else {
    mLoadingAsyncRequests.AppendElement(aRequest);
  }
}

void ScriptLoader::MaybeMoveToLoadedList(ScriptLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsFinished());

  bool isDynamicImport = false;
  if (aRequest->IsModuleRequest()) {
    ModuleLoadRequest* modReq = aRequest->AsModuleRequest();
    isDynamicImport = modReq->IsDynamicImport();
  }

  MOZ_ASSERT(aRequest->IsTopLevel() || isDynamicImport);

  // If it's async, move it to the loaded list.
  // aRequest->GetScriptLoadContext()->mInAsyncList really _should_ be in a
  // list, but the consequences if it's not are bad enough we want to avoid
  // trying to move it if it's not.
  if (aRequest->GetScriptLoadContext()->mInAsyncList) {
    MOZ_ASSERT(aRequest->isInList());
    if (aRequest->isInList()) {
      RefPtr<ScriptLoadRequest> req = mLoadingAsyncRequests.Steal(aRequest);
      mLoadedAsyncRequests.AppendElement(req);
    }
  } else if (isDynamicImport) {
    // Process dynamic imports with async scripts.
    MOZ_ASSERT(!aRequest->isInList());
    mLoadedAsyncRequests.AppendElement(aRequest);
  }
}

bool ScriptLoader::MaybeRemovedDeferRequests() {
  if (mDeferRequests.isEmpty() && mDocument && mBlockingDOMContentLoaded) {
    mBlockingDOMContentLoaded = false;
    mDocument->UnblockDOMContentLoaded(/* aFireSync = */ true);
    return true;
  }
  return false;
}

DocGroup* ScriptLoader::GetDocGroup() const { return mDocument->GetDocGroup(); }

void ScriptLoader::BeginDeferringScripts() {
  if (mDeferEnabled || mDeferCheckpointReached) {
    // We already started loading. Now, document.open() happened and we're doing
    // a new parse.
    // If mDeferEnabled, we haven't reached the defer checkpoint and if
    // mDeferCheckpointReached, we did but still have pending scripts. Either
    // way, the load event is still blocked, so we shouldn't block again.
    // If set, reset mDeferCheckpointReached. It'll get set again when the
    // DeferCheckpointReached call corresponding to this BeginDeferringScripts
    // call happens (on document.close()), since we will set mDeferEnabled.
    mDeferCheckpointReached = false;
  } else if (mDocument) {
    mDocument->BlockOnload();
  }
  mDeferEnabled = true;
}

nsAutoScriptLoaderDisabler::nsAutoScriptLoaderDisabler(Document* aDoc) {
  mLoader = aDoc->GetScriptLoader();
  mWasEnabled = mLoader && mLoader->GetEnabled();
  if (mWasEnabled) {
    mLoader->SetEnabled(false);
  }
}

nsAutoScriptLoaderDisabler::~nsAutoScriptLoaderDisabler() {
  if (mWasEnabled) {
    MOZ_ASSERT(mLoader, "mWasEnabled can be true only if we have a loader");
    mLoader->SetEnabled(true);
  }
}

#undef LOG

}  // namespace mozilla::dom
