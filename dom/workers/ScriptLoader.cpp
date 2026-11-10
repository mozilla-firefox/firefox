/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptLoader.h"

#include <algorithm>

#include "WorkerRunnable.h"
#include "WorkerScope.h"
#include "js/CompilationAndEvaluation.h"
#include "js/Exception.h"
#include "js/SourceText.h"
#include "js/TypeDecls.h"
#include "js/loader/ModuleLoadRequest.h"
#include "jsapi.h"
#include "jsfriendapi.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/Assertions.h"
#include "mozilla/Encoding.h"
#include "mozilla/LoadContext.h"
#include "mozilla/Maybe.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/ClientChannelHelper.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/SerializedStackHolder.h"
#include "mozilla/dom/nsCSPService.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/workerinternals/CacheLoadHandler.h"
#include "mozilla/dom/workerinternals/NetworkLoadHandler.h"
#include "mozilla/dom/workerinternals/ScriptResponseHeaderProcessor.h"
#include "mozilla/dom/workerinternals/WorkerModuleLoader.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsDocShellCID.h"
#include "nsError.h"
#include "nsIChannel.h"
#include "nsIContentPolicy.h"
#include "nsIContentSecurityPolicy.h"
#include "nsICookieJarSettings.h"
#include "nsIDocShell.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIIOService.h"
#include "nsIOService.h"
#include "nsIOutputStream.h"
#include "nsIPipe.h"
#include "nsIPrincipal.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsIStreamListenerTee.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIURI.h"
#include "nsIXPConnect.h"
#include "nsJSEnvironment.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"
#include "xpcpublic.h"

#define MAX_CONCURRENT_SCRIPTS 1000

using JS::loader::ParserMetadata;
using JS::loader::ScriptKind;
using JS::loader::ScriptLoadRequest;
using mozilla::ipc::PrincipalInfo;

namespace mozilla::dom::workerinternals {
namespace {

nsresult ConstructURI(const nsAString& aScriptURL, nsIURI* baseURI,
                      const mozilla::Encoding* aDocumentEncoding,
                      nsIURI** aResult) {
  nsresult rv;
  // Only top level workers' main script use the document charset for the
  // script uri encoding. Otherwise, default encoding (UTF-8) is applied.
  if (aDocumentEncoding) {
    nsAutoCString charset;
    aDocumentEncoding->Name(charset);
    rv = NS_NewURI(aResult, aScriptURL, charset.get(), baseURI);
  } else {
    rv = NS_NewURI(aResult, aScriptURL, nullptr, baseURI);
  }

  if (NS_FAILED(rv)) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }
  return NS_OK;
}

nsresult ChannelFromScriptURL(
    nsIPrincipal* principal, Document* parentDoc, WorkerPrivate* aWorkerPrivate,
    nsILoadGroup* loadGroup, nsIIOService* ios,
    nsIScriptSecurityManager* secMan, nsIURI* aScriptURL,
    const Maybe<ClientInfo>& aClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController, bool aIsMainScript,
    WorkerScriptType aWorkerScriptType, nsContentPolicyType aContentPolicyType,
    nsLoadFlags aLoadFlags, uint32_t aSecFlags,
    nsICookieJarSettings* aCookieJarSettings, nsIReferrerInfo* aReferrerInfo,
    nsIChannel** aChannel) {
  AssertIsOnMainThread();

  nsresult rv;
  nsCOMPtr<nsIURI> uri = aScriptURL;

  // Only use the document when its principal matches the principal of the
  // current request. This means scripts fetched using the Workers' own
  // principal won't inherit properties of the document, in particular the CSP.
  if (parentDoc && parentDoc->NodePrincipal() != principal) {
    parentDoc = nullptr;
  }

  // The main service worker script should never be loaded over the network
  // in this path.  It should always be offlined by ServiceWorkerScriptCache.
  // We assert here since this error should also be caught by the runtime
  // check in CacheLoadHandler.
  //
  // Note, if we ever allow service worker scripts to be loaded from network
  // here we need to configure the channel properly.  For example, it must
  // not allow redirects.
  MOZ_DIAGNOSTIC_ASSERT(aContentPolicyType !=
                        nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER);

  nsCOMPtr<nsIChannel> channel;
  if (parentDoc) {
    // This is the path for top level dedicated worker scripts with a document
    rv = NS_NewChannel(getter_AddRefs(channel), uri, parentDoc, aSecFlags,
                       aContentPolicyType,
                       nullptr,  // aPerformanceStorage
                       loadGroup,
                       nullptr,  // aCallbacks
                       aLoadFlags, ios);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_SECURITY_ERR);
  } else {
    // This branch is used in the following cases:
    //    * Shared and ServiceWorkers (who do not have a doc)
    //    * Static Module Imports
    //    * ImportScripts

    // We must have a loadGroup with a load context for the principal to
    // traverse the channel correctly.

    MOZ_ASSERT(loadGroup);
    MOZ_ASSERT(NS_LoadGroupMatchesPrincipal(loadGroup, principal));

    RefPtr<PerformanceStorage> performanceStorage;
    nsCOMPtr<nsICSPEventListener> cspEventListener;
    if (aWorkerPrivate && !aIsMainScript) {
      performanceStorage = aWorkerPrivate->GetPerformanceStorage();
      cspEventListener = aWorkerPrivate->CSPEventListener();
    }

    if (aClientInfo.isSome()) {
      // If we have an existing clientInfo (true for all modules and
      // importScripts), we will use this branch
      rv = NS_NewChannel(getter_AddRefs(channel), uri, principal,
                         aClientInfo.ref(), aController, aSecFlags,
                         aContentPolicyType, aCookieJarSettings,
                         performanceStorage, loadGroup, nullptr,  // aCallbacks
                         aLoadFlags, ios);
    } else {
      rv = NS_NewChannel(getter_AddRefs(channel), uri, principal, aSecFlags,
                         aContentPolicyType, aCookieJarSettings,
                         performanceStorage, loadGroup, nullptr,  // aCallbacks
                         aLoadFlags, ios);
    }

    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_SECURITY_ERR);

    if (cspEventListener) {
      nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
      rv = loadInfo->SetCspEventListener(cspEventListener);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Bug 2048884: copy the owning BrowsingContext id so requests initiated by
    // the worker report the correct frameId. This does not apply to shared or
    // service workers, which have no reference frame.
    if (aWorkerPrivate) {
      uint64_t bcID = aWorkerPrivate->AssociatedBrowsingContextID();
      if (bcID) {
        nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
        rv = loadInfo->SetAssociatedBrowsingContextID(bcID);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  if (aReferrerInfo) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel);
    if (httpChannel) {
      rv = httpChannel->SetReferrerInfo(aReferrerInfo);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  channel.forget(aChannel);
  return rv;
}

void LoadAllScripts(WorkerPrivate* aWorkerPrivate,
                    UniquePtr<SerializedStackHolder> aOriginStack,
                    const nsTArray<nsString>& aScriptURLs, bool aIsMainScript,
                    WorkerScriptType aWorkerScriptType, ErrorResult& aRv,
                    const mozilla::Encoding* aDocumentEncoding = nullptr) {
  aWorkerPrivate->AssertIsOnWorkerThread();
  NS_ASSERTION(!aScriptURLs.IsEmpty(), "Bad arguments!");

  AutoSyncLoopHolder syncLoop(aWorkerPrivate, Canceling);
  nsCOMPtr<nsISerialEventTarget> syncLoopTarget =
      syncLoop.GetSerialEventTarget();
  if (!syncLoopTarget) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  RefPtr<loader::WorkerScriptLoader> loader =
      loader::WorkerScriptLoader::Create(aWorkerPrivate,
                                         std::move(aOriginStack),
                                         syncLoopTarget, aWorkerScriptType);

  if (NS_WARN_IF(!loader)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Move any WorkerScriptLoader error back to the caller
  auto takeErrorResult =
      MakeScopeExit([&] { aRv = loader->TakeErrorResult(); });

  bool ok = loader->CreateScriptRequests(aScriptURLs, aDocumentEncoding,
                                         aIsMainScript);

  if (!ok) {
    return;
  }
  // Bug 1817259 - For now, we force loading the debugger script as Classic,
  // even if the debugged worker is a Module.
  if (aWorkerPrivate->WorkerType() == WorkerType::Module &&
      aWorkerScriptType != DebuggerScript) {
    MOZ_ASSERT(aIsMainScript);
    // Module Load
    RefPtr<JS::loader::ScriptLoadRequest> mainScript = loader->GetMainScript();
    if (mainScript && mainScript->IsModuleRequest()) {
      if (NS_FAILED(mainScript->AsModuleRequest()->StartModuleLoad())) {
        return;
      }
      syncLoop.Run();
      return;
    }
  }

  if (loader->DispatchLoadScripts()) {
    syncLoop.Run();
  }
}

class ChannelGetterRunnable final : public WorkerMainThreadRunnable {
  const nsAString& mScriptURL;
  const WorkerType& mWorkerType;
  const RequestCredentials& mCredentials;
  const ClientInfo mClientInfo;
  WorkerLoadInfo& mLoadInfo;
  nsresult mResult;

 public:
  ChannelGetterRunnable(WorkerPrivate* aParentWorker,
                        const nsAString& aScriptURL,
                        const WorkerType& aWorkerType,
                        const RequestCredentials& aCredentials,
                        WorkerLoadInfo& aLoadInfo)
      : WorkerMainThreadRunnable(aParentWorker,
                                 "ScriptLoader :: ChannelGetter"_ns),
        mScriptURL(aScriptURL)
        // ClientInfo should always be present since this should not be called
        // if parent's status is greater than Running.
        ,
        mWorkerType(aWorkerType),
        mCredentials(aCredentials),
        mClientInfo(aParentWorker->GlobalScope()->GetClientInfo().ref()),
        mLoadInfo(aLoadInfo),
        mResult(NS_ERROR_FAILURE) {
    MOZ_ASSERT(aParentWorker);
    aParentWorker->AssertIsOnWorkerThread();
  }

  virtual bool MainThreadRun() override {
    AssertIsOnMainThread();
    MOZ_ASSERT(mWorkerRef);

    WorkerPrivate* workerPrivate = mWorkerRef->Private();

    // Initialize the WorkerLoadInfo principal to our triggering principal
    // before doing anything else.  Normally we do this in the WorkerPrivate
    // Constructor, but we can't do so off the main thread when creating
    // a nested worker.  So do it here instead.
    mLoadInfo.mLoadingPrincipal = workerPrivate->GetPrincipal();
    MOZ_DIAGNOSTIC_ASSERT(mLoadInfo.mLoadingPrincipal);

    mLoadInfo.mPrincipal = mLoadInfo.mLoadingPrincipal;

    // Figure out our base URI.
    nsCOMPtr<nsIURI> baseURI = workerPrivate->GetBaseURI();
    MOZ_ASSERT(baseURI);

    // May be null.
    nsCOMPtr<Document> parentDoc = workerPrivate->GetDocument();

    mLoadInfo.mLoadGroup = workerPrivate->GetLoadGroup();
    mLoadInfo.mCookieJarSettings = workerPrivate->CookieJarSettings();

    // Nested workers use default uri encoding.
    nsCOMPtr<nsIURI> url;
    mResult = ConstructURI(mScriptURL, baseURI, nullptr, getter_AddRefs(url));
    NS_ENSURE_SUCCESS(mResult, true);

    Maybe<ClientInfo> clientInfo;
    clientInfo.emplace(mClientInfo);

    nsCOMPtr<nsIChannel> channel;
    nsCOMPtr<nsIReferrerInfo> referrerInfo =
        ReferrerInfo::CreateForFetch(mLoadInfo.mLoadingPrincipal, nullptr);
    mLoadInfo.mReferrerInfo =
        static_cast<ReferrerInfo*>(referrerInfo.get())
            ->CloneWithNewPolicy(workerPrivate->GetReferrerPolicy());

    mResult = workerinternals::ChannelFromScriptURLMainThread(
        mLoadInfo.mLoadingPrincipal, parentDoc, mLoadInfo.mLoadGroup, url,
        mWorkerType, mCredentials, clientInfo,
        // Nested workers are always dedicated.
        nsIContentPolicy::TYPE_INTERNAL_WORKER, mLoadInfo.mCookieJarSettings,
        mLoadInfo.mReferrerInfo, getter_AddRefs(channel));
    NS_ENSURE_SUCCESS(mResult, true);

    mResult = mLoadInfo.SetPrincipalsAndPolicyContainerFromChannel(channel);
    NS_ENSURE_SUCCESS(mResult, true);

    mLoadInfo.mChannel = std::move(channel);
    return true;
  }

  nsresult GetResult() const { return mResult; }

 private:
  virtual ~ChannelGetterRunnable() = default;
};

nsresult GetCommonSecFlags(bool aIsMainScript, nsIURI* uri,
                           nsIPrincipal* principal,
                           WorkerScriptType aWorkerScriptType,
                           uint32_t& secFlags) {
  bool inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
      principal, uri, true /* aInheritForAboutBlank */,
      false /* aForceInherit */);

  bool isData = uri->SchemeIs("data");
  if (inheritAttrs && !isData) {
    secFlags |= nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  if (aWorkerScriptType == DebuggerScript) {
    // A DebuggerScript needs to be a chrome script resource like chrome: or
    // resource:. We restrict it to those trusted schemes rather than the broad
    // URI_IS_UI_RESOURCE flag, which image/UI data protocols (page-icon:,
    // moz-icon:, ...) also carry and must never be loaded as worker scripts.
    if (!nsContentSecurityUtils::IsTrustedScheme(uri)) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    secFlags |= nsILoadInfo::SEC_ALLOW_CHROME;
  }

  // Note: this is for backwards compatibility and goes against spec.
  // We should find a better solution.
  if (aIsMainScript && isData) {
    secFlags = nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL;
  }

  return NS_OK;
}

nsresult GetModuleSecFlags(bool aIsTopLevel, nsIPrincipal* principal,
                           WorkerScriptType aWorkerScriptType, nsIURI* aURI,
                           RequestCredentials aCredentials,
                           uint32_t& secFlags) {
  // Implements "To fetch a single module script,"
  // Step 9. If destination is "worker", "sharedworker", or "serviceworker",
  //         and the top-level module fetch flag is set, then set request's
  //         mode to "same-origin".

  // Step 8. Let request be a new request whose [...] mode is "cors" [...]
  secFlags = aIsTopLevel ? nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED
                         : nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;

  // This implements the same Cookie settings as  nsContentSecurityManager's
  // ComputeSecurityFlags. The main difference is the line above, Step 9,
  // setting to same origin.

  if (aCredentials == RequestCredentials::Include) {
    secFlags |= nsILoadInfo::nsILoadInfo::SEC_COOKIES_INCLUDE;
  } else if (aCredentials == RequestCredentials::Same_origin) {
    secFlags |= nsILoadInfo::nsILoadInfo::SEC_COOKIES_SAME_ORIGIN;
  } else if (aCredentials == RequestCredentials::Omit) {
    secFlags |= nsILoadInfo::nsILoadInfo::SEC_COOKIES_OMIT;
  }

  return GetCommonSecFlags(aIsTopLevel, aURI, principal, aWorkerScriptType,
                           secFlags);
}

nsresult GetClassicSecFlags(bool aIsMainScript, nsIURI* uri,
                            nsIPrincipal* principal,
                            WorkerScriptType aWorkerScriptType,
                            uint32_t& secFlags) {
  secFlags = aIsMainScript
                 ? nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED
                 : nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;

  return GetCommonSecFlags(aIsMainScript, uri, principal, aWorkerScriptType,
                           secFlags);
}

}  //  anonymous namespace

namespace loader {

class ScriptExecutorRunnable final : public MainThreadWorkerSyncRunnable {
  RefPtr<WorkerScriptLoader> mScriptLoader;
  const Span<RefPtr<ThreadSafeRequestHandle>> mLoadedRequests;

 public:
  ScriptExecutorRunnable(WorkerScriptLoader* aScriptLoader,
                         WorkerPrivate* aWorkerPrivate,
                         nsISerialEventTarget* aSyncLoopTarget,
                         Span<RefPtr<ThreadSafeRequestHandle>> aLoadedRequests);

 private:
  ~ScriptExecutorRunnable() = default;

  virtual bool IsDebuggerRunnable() const override;

  virtual bool PreRun(WorkerPrivate* aWorkerPrivate) override;

  bool ProcessModuleScript(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  bool ProcessClassicScripts(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override;

  nsresult Cancel() override;
};

WorkerScriptLoader::WorkerScriptLoader(
    UniquePtr<SerializedStackHolder> aOriginStack,
    nsISerialEventTarget* aSyncLoopTarget, WorkerScriptType aWorkerScriptType)
    : mOriginStack(std::move(aOriginStack)),
      mSyncLoopTarget(aSyncLoopTarget),
      mWorkerScriptType(aWorkerScriptType),
      mLoadingModuleRequestCount(0),
      mCleanedUp(false),
      mCleanUpLock("cleanUpLock") {}

WorkerScriptLoader::~WorkerScriptLoader() { mRv.SuppressException(); }

ErrorResult WorkerScriptLoader::TakeErrorResult() { return std::move(mRv); }

already_AddRefed<WorkerScriptLoader> WorkerScriptLoader::Create(
    WorkerPrivate* aWorkerPrivate,
    UniquePtr<SerializedStackHolder> aOriginStack,
    nsISerialEventTarget* aSyncLoopTarget, WorkerScriptType aWorkerScriptType) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  RefPtr<WorkerScriptLoader> self = new WorkerScriptLoader(
      std::move(aOriginStack), aSyncLoopTarget, aWorkerScriptType);

  RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
      aWorkerPrivate, "WorkerScriptLoader::Create", [self]() {
        // Requests that are in flight are covered by the worker references
        // in DispatchLoadScript(s), so we do not need to do additional
        // cleanup, but just in case we are ready/aborted we can try to
        // shutdown here, too.
        self->TryShutdown();
      });

  if (!workerRef) {
    return nullptr;
  }
  self->mWorkerRef = new ThreadSafeWorkerRef(workerRef);

  nsIGlobalObject* global = self->GetGlobal();
  self->mController = global->GetController();

  // Set up the module loader, if it has not been initialzied yet.
  self->InitModuleLoader();

  return self.forget();
}

ScriptLoadRequest* WorkerScriptLoader::GetMainScript() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  ScriptLoadRequest* request = mLoadingRequests.getFirst();
  if (request->GetWorkerLoadContext()->IsTopLevel()) {
    return request;
  }
  return nullptr;
}

void WorkerScriptLoader::InitModuleLoader() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  if (GetGlobal()->GetModuleLoader(nullptr)) {
    return;
  }
  RefPtr<WorkerModuleLoader> moduleLoader =
      new WorkerModuleLoader(this, GetGlobal());
  if (mWorkerScriptType == WorkerScript) {
    mWorkerRef->Private()->GlobalScope()->InitModuleLoader(moduleLoader);
    return;
  }
  mWorkerRef->Private()->DebuggerGlobalScope()->InitModuleLoader(moduleLoader);
}

bool WorkerScriptLoader::CreateScriptRequests(
    const nsTArray<nsString>& aScriptURLs,
    const mozilla::Encoding* aDocumentEncoding, bool aIsMainScript) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  // If a worker has been loaded as a module worker, ImportScripts calls are
  // disallowed -- then the operation is invalid.
  //
  // 10.3.1 Importing scripts and libraries.
  // Step 1. If worker global scope's type is "module", throw a TypeError
  //         exception.
  //
  // Also, for now, the debugger script is always loaded as Classic,
  // even if the debugged worker is a Module. We still want to allow
  // it to use importScripts.
  if (mWorkerRef->Private()->WorkerType() == WorkerType::Module &&
      !aIsMainScript && !IsDebuggerScript()) {
    // This should only run for non-main scripts, as only these are
    // importScripts
    mRv.ThrowTypeError(
        "Using `ImportScripts` inside a Module Worker is "
        "disallowed.");
    return false;
  }
  for (const nsString& scriptURL : aScriptURLs) {
    nsresult rv = NS_OK;
    RefPtr<ScriptLoadRequest> request = CreateScriptLoadRequest(
        scriptURL, aDocumentEncoding, aIsMainScript, &rv);
    if (!request) {
      mLoadingRequests.CancelRequestsAndClear();
      workerinternals::ReportLoadError(mRv, rv, scriptURL);
      return false;
    }
    mLoadingRequests.AppendElement(request);
  }

  return true;
}

nsTArray<RefPtr<ThreadSafeRequestHandle>> WorkerScriptLoader::GetLoadingList() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  nsTArray<RefPtr<ThreadSafeRequestHandle>> list;
  for (ScriptLoadRequest* req = mLoadingRequests.getFirst(); req;
       req = req->getNext()) {
    RefPtr<ThreadSafeRequestHandle> handle =
        new ThreadSafeRequestHandle(req, mSyncLoopTarget.get());
    list.AppendElement(handle.forget());
  }
  return list;
}

bool WorkerScriptLoader::IsDynamicImport(ScriptLoadRequest* aRequest) {
  return aRequest->IsModuleRequest() &&
         aRequest->AsModuleRequest()->IsDynamicImport();
}

nsContentPolicyType WorkerScriptLoader::GetContentPolicyType(
    ScriptLoadRequest* aRequest) {
  if (aRequest->GetWorkerLoadContext()->IsTopLevel()) {
    // Implements https://html.spec.whatwg.org/#worker-processing-model
    // Step 13: Let destination be "sharedworker" if is shared is true, and
    // "worker" otherwise.
    return mWorkerRef->Private()->ContentPolicyType();
  }
  if (aRequest->IsModuleRequest()) {
    if (aRequest->AsModuleRequest()->mModuleType == JS::ModuleType::Text) {
      return nsIContentPolicy::TYPE_TEXT;
    }

    if (aRequest->AsModuleRequest()->IsDynamicImport()) {
      if (aRequest->AsModuleRequest()->mModuleType ==
          JS::ModuleType::JavaScript) {
        return nsIContentPolicy::TYPE_INTERNAL_MODULE;
      } else {
        MOZ_ASSERT(aRequest->AsModuleRequest()->mModuleType ==
                   JS::ModuleType::JSON);
        return nsIContentPolicy::TYPE_JSON;
      }
    }

    // Implements the destination for Step 14 in
    // https://html.spec.whatwg.org/#worker-processing-model
    //
    // We need a special subresource type in order to correctly implement
    // the graph fetch, where the destination is set to "worker" or
    // "sharedworker".
    return nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE;
  }
  // For script imported in worker's importScripts().
  return nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS;
}

already_AddRefed<ScriptLoadRequest> WorkerScriptLoader::CreateScriptLoadRequest(
    const nsString& aScriptURL, const mozilla::Encoding* aDocumentEncoding,
    bool aIsMainScript, nsresult* aRv) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  WorkerLoadContext::Kind kind =
      WorkerLoadContext::GetKind(aIsMainScript, IsDebuggerScript());

  Maybe<ClientInfo> clientInfo = GetGlobal()->GetClientInfo();

  // (For non-serviceworkers, this variable does not matter, but false best
  // captures their behavior.)
  bool onlyExistingCachedResourcesAllowed = false;
  if (mWorkerRef->Private()->IsServiceWorker()) {
    // https://w3c.github.io/ServiceWorker/#importscripts step 4:
    // > 4. If serviceWorker’s state is not "parsed" or "installing":
    // >    1. Return map[url] if it exists and a network error otherwise.
    //
    // So if our state is beyond installing, it's too late to make a request
    // that would perform a new fetch which would be cached.
    onlyExistingCachedResourcesAllowed =
        mWorkerRef->Private()->GetServiceWorkerDescriptor().State() >
        ServiceWorkerState::Installing;
  }
  RefPtr<WorkerLoadContext> loadContext = new WorkerLoadContext(
      kind, clientInfo, this, onlyExistingCachedResourcesAllowed);

  // Create ScriptLoadRequests for this WorkerScriptLoader
  ReferrerPolicy referrerPolicy = mWorkerRef->Private()->GetReferrerPolicy();

  // Only top level workers' main script use the document charset for the
  // script uri encoding. Otherwise, default encoding (UTF-8) is applied.
  MOZ_ASSERT_IF(bool(aDocumentEncoding),
                aIsMainScript && !mWorkerRef->Private()->GetParent());
  nsCOMPtr<nsIURI> baseURI = aIsMainScript ? GetInitialBaseURI() : GetBaseURI();
  nsCOMPtr<nsIURI> uri;
  nsresult rv =
      ConstructURI(aScriptURL, baseURI, aDocumentEncoding, getter_AddRefs(uri));
  // If we failed to construct the URI, handle it in the LoadContext so it is
  // thrown in the right order.
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // This function is used by the following:
    //   * Worker constructor
    //   * WorkerGlobalScope.importScripts
    //   * WorkerDebuggerGlobalScope.loadSubScript
    //
    // Worker constructor validates the URL in WorkerPrivate::Constructor,
    // and this branch shouldn't taken.
    //
    // importScripts is available only to classic scripts.
    //
    // loadSubScript is available only to privileged scripts, and we don't
    // care any invalid URLs.
    //
    // Module imports should use WorkerModuleLoader instead.
    MOZ_ASSERT(mWorkerRef->Private()->WorkerType() == WorkerType::Classic);

    *aRv = rv;
    return nullptr;
  }

  // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-classic-worker-script
  // Step 2.5. Let script be the result [...] and the default classic script
  // fetch options.
  //
  // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-worklet/module-worker-script-graph
  // Step 1. Let options be a script fetch options whose cryptographic nonce is
  // the empty string, integrity metadata is the empty string, parser metadata
  // is "not-parser-inserted", credentials mode is credentials mode, referrer
  // policy is the empty string, and fetch priority is "auto".
  RefPtr<ScriptFetchOptions> fetchOptions = new ScriptFetchOptions(
      CORSMode::CORS_NONE, /* aNonce = */ u""_ns, RequestPriority::Auto,
      ParserMetadata::NotParserInserted, nullptr);

  RefPtr<ScriptLoadRequest> request = nullptr;
  // Bug 1817259 - For now the debugger scripts are always loaded a Classic.
  if (mWorkerRef->Private()->WorkerType() == WorkerType::Classic ||
      IsDebuggerScript()) {
    request = new ScriptLoadRequest(ScriptKind::eClassic, SRIMetadata(),
                                    /* aReferrer = */ nullptr, loadContext,
                                    /* aClassicScriptHintEncoding = */ nullptr);
  } else {
    // Implements part of "To fetch a worklet/module worker script graph"
    // including, setting up the request with a credentials mode,
    // destination.

    // Step 1. Let options be a script fetch options.
    // We currently don't track credentials in our ScriptFetchOptions
    // implementation, so we are defaulting the fetchOptions object defined
    // above. This behavior is handled fully in GetModuleSecFlags.

    RefPtr<WorkerModuleLoader::ModuleLoaderBase> moduleLoader =
        GetGlobal()->GetModuleLoader(nullptr);

    // Implements the referrer for "To fetch a single module script"
    // Our implementation does not have a "client" as a referrer.
    // However, when client is resolved (per 8.3. Determine request’s
    // Referrer in
    // https://w3c.github.io/webappsec-referrer-policy/#determine-requests-referrer)
    // This should result in the referrer source being the creation URL.
    //
    // In subresource modules, the referrer is the importing script.
    nsCOMPtr<nsIURI> referrer =
        mWorkerRef->Private()->GetReferrerInfo()->GetOriginalReferrer();

    // Part of Step 2. This sets the Top-level flag to true
    request = new ModuleLoadRequest(
        JS::ModuleType::JavaScript, SRIMetadata(), referrer, loadContext,
        ModuleLoadRequest::Kind::TopLevel, moduleLoader, nullptr);
  }

  // Set the mURL, it will be used for error handling and debugging.
  request->mURL = NS_ConvertUTF16toUTF8(aScriptURL);

  request->NoCacheEntryFound(referrerPolicy, fetchOptions, uri);

  return request.forget();
}

bool WorkerScriptLoader::DispatchLoadScript(ScriptLoadRequest* aRequest) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();

  IncreaseLoadingModuleRequestCount();

  nsTArray<RefPtr<ThreadSafeRequestHandle>> scriptLoadList;
  RefPtr<ThreadSafeRequestHandle> handle =
      new ThreadSafeRequestHandle(aRequest, mSyncLoopTarget.get());
  scriptLoadList.AppendElement(handle.forget());

  return DispatchLoadScripts(std::move(scriptLoadList));
}

bool WorkerScriptLoader::DispatchLoadScripts(
    nsTArray<RefPtr<ThreadSafeRequestHandle>>&& aLoadingList) {
  MOZ_ASSERT(mWorkerRef->Private()->IsOnWorkerThread());

  nsTArray<RefPtr<ThreadSafeRequestHandle>> scriptLoadList =
      std::move(aLoadingList);
  if (!scriptLoadList.Length()) {
    // Try to get a loading list if we were not passed one explcitly.
    scriptLoadList = GetLoadingList();
  }

  RefPtr<ScriptLoaderRunnable> runnable =
      new ScriptLoaderRunnable(this, std::move(scriptLoadList));

  RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
      mWorkerRef->Private(), "WorkerScriptLoader::DispatchLoadScripts",
      [runnable]() {
        NS_DispatchToMainThread(NewRunnableMethod(
            "ScriptLoaderRunnable::CancelMainThreadWithBindingAborted",
            runnable,
            &ScriptLoaderRunnable::CancelMainThreadWithBindingAborted));
      });

  if (NS_FAILED(NS_DispatchToMainThread(runnable))) {
    NS_ERROR("Failed to dispatch!");
    mRv.Throw(NS_ERROR_FAILURE);
    return false;
  }
  return true;
}

nsIURI* WorkerScriptLoader::GetInitialBaseURI() {
  MOZ_ASSERT(mWorkerRef->Private());
  nsIURI* baseURI;
  WorkerPrivate* parentWorker = mWorkerRef->Private()->GetParent();
  if (parentWorker) {
    baseURI = parentWorker->GetBaseURI();
  } else {
    // May be null.
    baseURI = mWorkerRef->Private()->GetBaseURI();
  }

  return baseURI;
}

nsIURI* WorkerScriptLoader::GetBaseURI() const {
  MOZ_ASSERT(mWorkerRef);
  nsIURI* baseURI;
  baseURI = mWorkerRef->Private()->GetBaseURI();
  NS_ASSERTION(baseURI, "Should have been set already!");

  return baseURI;
}

nsIGlobalObject* WorkerScriptLoader::GetGlobal() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  return mWorkerScriptType == WorkerScript
             ? static_cast<nsIGlobalObject*>(
                   mWorkerRef->Private()->GlobalScope())
             : mWorkerRef->Private()->DebuggerGlobalScope();
}

void WorkerScriptLoader::MaybeMoveToLoadedList(ScriptLoadRequest* aRequest) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  // Only set to ready for regular scripts. Module loader will set the script to
  // ready if it is a Module Request.
  if (!aRequest->IsModuleRequest()) {
    aRequest->SetReady();
  }

  // If the request is not in a list, we are in an illegal state.
  MOZ_RELEASE_ASSERT(aRequest->isInList());

  while (!mLoadingRequests.isEmpty()) {
    ScriptLoadRequest* request = mLoadingRequests.getFirst();
    // We need to move requests in post order. If prior requests have not
    // completed, delay execution.
    if (!request->IsFinished()) {
      break;
    }

    RefPtr<ScriptLoadRequest> req = mLoadingRequests.Steal(request);
    mLoadedRequests.AppendElement(req);
  }
}

bool WorkerScriptLoader::StorePolicyContainerArgs() {
  // We must be on the same worker as we started on.
  mWorkerRef->Private()->AssertIsOnWorkerThread();

  if (!mWorkerRef->Private()->GetJSContext()) {
    return false;
  }

  MOZ_ASSERT(!mRv.Failed());

  // Store the policy container args (CSP, IP address space, etc.) from
  // WorkerLoadInfo into the worker's ClientSource.
  mWorkerRef->Private()->StorePolicyContainerArgsOnClient();
  return true;
}

bool WorkerScriptLoader::ProcessPendingRequests(JSContext* aCx) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  // Don't run if something else has already failed.
  if (mExecutionAborted) {
    mLoadedRequests.CancelRequestsAndClear();
    TryShutdown();
    return true;
  }

  // If nothing else has failed, our ErrorResult better not be a failure
  // either.
  MOZ_ASSERT(!mRv.Failed(), "Who failed it and why?");

  // Slightly icky action at a distance, but there's no better place to stash
  // this value, really.
  JS::Rooted<JSObject*> global(aCx, JS::CurrentGlobalOrNull(aCx));
  MOZ_ASSERT(global);

  while (!mLoadedRequests.isEmpty()) {
    RefPtr<ScriptLoadRequest> req = mLoadedRequests.StealFirst();
    // We don't have a ProcessRequest method (like we do on the DOM), as there
    // isn't much processing that we need to do per request that isn't related
    // to evaluation (the processsing done for the DOM is handled in
    // DataRecievedFrom{Cache,Network} for workers.
    // So, this inner loop calls EvaluateScript directly. This will change
    // once modules are introduced as we will have some extra work to do.
    if (!EvaluateScript(aCx, req)) {
      req->Cancel();
      mExecutionAborted = true;
      WorkerLoadContext* loadContext = req->GetWorkerLoadContext();
      mMutedErrorFlag = loadContext->mMutedErrorFlag.valueOr(true);
      mLoadedRequests.CancelRequestsAndClear();
      break;
    }
  }

  TryShutdown();
  return true;
}

nsresult WorkerScriptLoader::LoadScript(
    ThreadSafeRequestHandle* aRequestHandle) {
  AssertIsOnMainThread();

  WorkerLoadContext* loadContext = aRequestHandle->GetContext();
  ScriptLoadRequest* request = aRequestHandle->GetRequest();
  MOZ_ASSERT_IF(loadContext->IsTopLevel(), !IsDebuggerScript());

  // The URL passed to us for loading was invalid, stop loading at this point.
  if (loadContext->mLoadResult != NS_ERROR_NOT_INITIALIZED) {
    return loadContext->mLoadResult;
  }

  WorkerPrivate* parentWorker = mWorkerRef->Private()->GetParent();

  // For JavaScript debugging, the devtools server must run on the same
  // thread as the debuggee, indicating the worker uses content principal.
  // However, in Bug 863246, web content will no longer be able to load
  // resource:// URIs by default, so we need system principal to load
  // debugger scripts.
  nsIPrincipal* principal = (IsDebuggerScript())
                                ? nsContentUtils::GetSystemPrincipal()
                                : mWorkerRef->Private()->GetPrincipal();

  nsCOMPtr<nsILoadGroup> loadGroup = mWorkerRef->Private()->GetLoadGroup();
  MOZ_DIAGNOSTIC_ASSERT(principal);

  NS_ENSURE_TRUE(NS_LoadGroupMatchesPrincipal(loadGroup, principal),
                 NS_ERROR_FAILURE);

  // May be null.
  nsCOMPtr<Document> parentDoc = mWorkerRef->Private()->GetDocument();

  nsCOMPtr<nsIChannel> channel;
  if (loadContext->IsTopLevel()) {
    // May be null.
    channel = mWorkerRef->Private()->ForgetWorkerChannel();
  }

  nsCOMPtr<nsIIOService> ios(do_GetIOService());

  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  NS_ASSERTION(secMan, "This should never be null!");

  nsresult& rv = loadContext->mLoadResult;

  nsLoadFlags loadFlags = mWorkerRef->Private()->GetLoadFlags();

  // Get the top-level worker.
  WorkerPrivate* topWorkerPrivate = mWorkerRef->Private();
  WorkerPrivate* parent = topWorkerPrivate->GetParent();
  while (parent) {
    topWorkerPrivate = parent;
    parent = topWorkerPrivate->GetParent();
  }

  // If the top-level worker is a dedicated worker and has a window, and the
  // window has a docshell, the caching behavior of this worker should match
  // that of that docshell.
  if (topWorkerPrivate->IsDedicatedWorker()) {
    if (nsCOMPtr<nsPIDOMWindowInner> window = topWorkerPrivate->GetWindow()) {
      if (nsCOMPtr<nsIDocShell> docShell = window->GetDocShell()) {
        loadFlags = docShell->GetDefaultLoadFlags();
      }
    }
  }

  if (!channel) {
    nsCOMPtr<nsIReferrerInfo> referrerInfo;
    uint32_t secFlags;
    if (request->IsModuleRequest()) {
      // https://fetch.spec.whatwg.org/#concept-main-fetch
      // Step 8. If request’s referrer policy is the empty string, then set
      //         request’s referrer policy to request’s policy container’s
      //         referrer policy.
      ReferrerPolicy policy =
          request->ReferrerPolicy() == ReferrerPolicy::_empty
              ? mWorkerRef->Private()->GetReferrerPolicy()
              : request->ReferrerPolicy();

      referrerInfo = new ReferrerInfo(request->mReferrer, policy);

      // https://html.spec.whatwg.org/multipage/webappapis.html#default-classic-script-fetch-options
      // The default classic script fetch options are a script fetch options
      // whose ... credentials mode is "same-origin", ....
      RequestCredentials credentials =
          mWorkerRef->Private()->WorkerType() == WorkerType::Classic
              ? RequestCredentials::Same_origin
              : mWorkerRef->Private()->WorkerCredentials();

      rv = GetModuleSecFlags(loadContext->IsTopLevel(), principal,
                             mWorkerScriptType, request->URI(), credentials,
                             secFlags);
    } else {
      referrerInfo = ReferrerInfo::CreateForFetch(principal, nullptr);
      if (parentWorker && !loadContext->IsTopLevel()) {
        referrerInfo =
            static_cast<ReferrerInfo*>(referrerInfo.get())
                ->CloneWithNewPolicy(parentWorker->GetReferrerPolicy());
      }
      rv = GetClassicSecFlags(loadContext->IsTopLevel(), request->URI(),
                              principal, mWorkerScriptType, secFlags);
    }

    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsContentPolicyType contentPolicyType = GetContentPolicyType(request);

    rv = ChannelFromScriptURL(
        principal, parentDoc, mWorkerRef->Private(), loadGroup, ios, secMan,
        request->URI(), loadContext->mClientInfo, mController,
        loadContext->IsTopLevel(), mWorkerScriptType, contentPolicyType,
        loadFlags, secFlags, mWorkerRef->Private()->CookieJarSettings(),
        referrerInfo, getter_AddRefs(channel));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Set the IsInThirdPartyContext for the channel's loadInfo according to the
    // partitionKey of the principal. The worker is foreign if it's using
    // partitioned principal, i.e. the partitionKey is not empty. In this case,
    // we need to set the bit to the channel's loadInfo.
    if (!principal->OriginAttributesRef().mPartitionKey.IsEmpty()) {
      nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
      rv = loadInfo->SetIsInThirdPartyContext(true);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  // Associate any originating stack with the channel.
  if (!mOriginStackJSON.IsEmpty()) {
    NotifyNetworkMonitorAlternateStack(channel, mOriginStackJSON);
  }

  // We need to know which index we're on in OnStreamComplete so we know
  // where to put the result.
  RefPtr<NetworkLoadHandler> listener =
      new NetworkLoadHandler(this, aRequestHandle);

  RefPtr<ScriptResponseHeaderProcessor> headerProcessor = nullptr;

  // For each debugger script, a non-debugger script load of the same script
  // should have occured prior that processed the headers.
  if (!IsDebuggerScript()) {
    JS::ModuleType moduleType = request->IsModuleRequest()
                                    ? request->AsModuleRequest()->mModuleType
                                    : JS::ModuleType::JavaScript;

    bool requiresStrictMimeCheck =
        GetContentPolicyType(request) ==
            nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS ||
        request->IsModuleRequest();

    headerProcessor = MakeRefPtr<ScriptResponseHeaderProcessor>(
        mWorkerRef, loadContext->IsTopLevel() && !IsDynamicImport(request),
        requiresStrictMimeCheck, moduleType);
  }

  nsCOMPtr<nsIStreamLoader> loader;
  rv = NS_NewStreamLoader(getter_AddRefs(loader), listener, headerProcessor);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (loadContext->IsTopLevel()) {
    MOZ_DIAGNOSTIC_ASSERT(loadContext->mClientInfo.isSome());

    // In order to get the correct foreign partitioned prinicpal, we need to
    // set the `IsThirdPartyContextToTopWindow` to the channel's loadInfo.
    // This flag reflects the fact that if the worker is created under a
    // third-party context.
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    loadInfo->SetIsInThirdPartyContext(
        mWorkerRef->Private()->IsThirdPartyContext());

    Maybe<ClientInfo> clientInfo;
    clientInfo.emplace(loadContext->mClientInfo.ref());
    rv = AddClientChannelHelper(channel, std::move(clientInfo),
                                Maybe<ClientInfo>(),
                                mWorkerRef->Private()->HybridEventTarget());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  if (StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    nsILoadInfo::CrossOriginEmbedderPolicy respectedCOEP =
        mWorkerRef->Private()->GetEmbedderPolicy();
    if (mWorkerRef->Private()->IsDedicatedWorker() &&
        respectedCOEP == nsILoadInfo::EMBEDDER_POLICY_NULL) {
      respectedCOEP = mWorkerRef->Private()->GetOwnerEmbedderPolicy();
    }

    nsCOMPtr<nsILoadInfo> channelLoadInfo = channel->LoadInfo();
    channelLoadInfo->SetLoadingEmbedderPolicy(respectedCOEP);
  }

  if (loadContext->mCacheStatus != WorkerLoadContext::ToBeCached) {
    rv = channel->AsyncOpen(loader);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  } else {
    nsCOMPtr<nsIOutputStream> writer;

    // In case we return early.
    loadContext->mCacheStatus = WorkerLoadContext::Cancel;

    NS_NewPipe(getter_AddRefs(loadContext->mCacheReadStream),
               getter_AddRefs(writer), 0,
               UINT32_MAX,    // unlimited size to avoid writer WOULD_BLOCK case
               true, false);  // non-blocking reader, blocking writer

    nsCOMPtr<nsIStreamListenerTee> tee =
        do_CreateInstance(NS_STREAMLISTENERTEE_CONTRACTID);
    rv = tee->Init(loader, writer, listener);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsresult rv = channel->AsyncOpen(tee);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  loadContext->mChannel.swap(channel);

  return NS_OK;
}

nsresult WorkerScriptLoader::FillCompileOptionsForRequest(
    JSContext* cx, ScriptLoadRequest* aRequest, JS::CompileOptions* aOptions,
    JS::MutableHandle<JSScript*> aIntroductionScript) {
  // The full URL shouldn't be exposed to the debugger. See Bug 1634872
  aOptions->setFileAndLine(aRequest->mURL.get(), 1);
  aOptions->setNoScriptRval(true);

  aOptions->setMutedErrors(
      aRequest->GetWorkerLoadContext()->mMutedErrorFlag.value());

  if (aRequest->HasSourceMapURL()) {
    aOptions->setSourceMapURL(aRequest->GetSourceMapURL().get());
  }

  // disable top-level await for sw module scripts
  const auto* workerPrivate = GetCurrentThreadWorkerPrivate();
  if (workerPrivate && workerPrivate->IsServiceWorker() &&
      aRequest->IsModuleRequest()) {
    aOptions->topLevelAwait = false;
  }

  return NS_OK;
}

bool WorkerScriptLoader::EvaluateScript(JSContext* aCx,
                                        ScriptLoadRequest* aRequest) {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  MOZ_ASSERT(!IsDynamicImport(aRequest));

  // Make sure TryShutdown() bails while evaluation is in progress.
  // ProcessPendingRequests() calls TryShutdown() once we return.
  ++mEvaluatingScriptCount;
  auto decrementEvaluatingScriptCount = MakeScopeExit([&] {
    MOZ_ASSERT(mEvaluatingScriptCount > 0);
    --mEvaluatingScriptCount;
  });

  WorkerLoadContext* loadContext = aRequest->GetWorkerLoadContext();

  NS_ASSERTION(!loadContext->mChannel, "Should no longer have a channel!");
  NS_ASSERTION(aRequest->IsFinished(), "Should be scheduled!");

  MOZ_ASSERT(!mRv.Failed(), "Who failed it and why?");
  mRv.MightThrowJSException();
  if (NS_FAILED(loadContext->mLoadResult)) {
    ReportErrorToConsole(aRequest, loadContext->mLoadResult);
    return false;
  }

  // If this is a top level script that succeeded, then mark the
  // Client execution ready and possible controlled by a service worker.
  if (loadContext->IsTopLevel()) {
    if (mController.isSome()) {
      MOZ_ASSERT(mWorkerScriptType == WorkerScript,
                 "Debugger clients can't be controlled.");
      mWorkerRef->Private()->GlobalScope()->Control(mController.ref());
    }
    mWorkerRef->Private()->ExecutionReady();
  }

  if (aRequest->IsModuleRequest()) {
    // Only the top level module of the module graph will be executed from here,
    // the rest will be executed from SpiderMonkey as part of the execution of
    // the module graph.
    MOZ_ASSERT(aRequest->IsTopLevel());
    ModuleLoadRequest* request = aRequest->AsModuleRequest();
    if (!request->mModuleScript) {
      return false;
    }

    // https://html.spec.whatwg.org/#run-a-worker
    // if script's error to rethrow is non-null, then:
    //    Queue a global task on the DOM manipulation task source given worker's
    //    relevant global object to fire an event named error at worker.
    //
    // The event will be dispatched in CompileScriptRunnable.
    if (request->mModuleScript->HasParseError() ||
        request->mModuleScript->HasErrorToRethrow()) {
      // Here we assign an error code that is not a JS Exception, so
      // CompileRunnable can dispatch the event.
      mRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }

    // Implements To fetch a worklet/module worker script graph
    // Step 5. Fetch the descendants of and link result.
    if (!request->InstantiateModuleGraph()) {
      return false;
    }

    if (request->mModuleScript->HasErrorToRethrow()) {
      // See the comments when we check HasParseError() above.
      mRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
      return false;
    }

    nsresult rv = request->EvaluateModule();
    return NS_SUCCEEDED(rv);
  }

  JS::CompileOptions options(aCx);
  // The introduction script is used by the DOM script loader as a way
  // to fill the Debugger Metadata for the JS Execution context. We don't use
  // the JS Execution context as we are not making use of async compilation
  // (delegation to another worker to produce bytecode or compile a string to a
  // JSScript), so it is not used in this context.
  JS::Rooted<JSScript*> unusedIntroductionScript(aCx);
  nsresult rv = FillCompileOptionsForRequest(aCx, aRequest, &options,
                                             &unusedIntroductionScript);

  MOZ_ASSERT(NS_SUCCEEDED(rv), "Filling compile options should not fail");

  // Our ErrorResult still shouldn't be a failure.
  MOZ_ASSERT(!mRv.Failed(), "Who failed it and why?");

  // Get the source text.
  ScriptLoadRequest::MaybeSourceText maybeSource;
  rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                 aRequest->mLoadContext.get());
  if (NS_FAILED(rv)) {
    mRv.StealExceptionFromJSContext(aCx);
    return false;
  }

  if (!mWorkerRef->Private()->IsServiceWorker()) {
    // We need a LoadedScript to be associated with the JSScript in order to
    // correctly resolve the referencing private for dynamic imports. In turn
    // this allows us to correctly resolve the BaseURL.
    //
    // Dynamic import is disallowed on service workers.  Additionally, causes
    // crashes because the life cycle isn't completed for service workers.  To
    // keep things simple, we don't create a classic script for ServiceWorkers.
    // If this changes then we will need to ensure that the reference that is
    // held is released appropriately.
    nsCOMPtr<nsIURI> requestBaseURI;
    if (loadContext->mMutedErrorFlag.valueOr(false)) {
      NS_NewURI(getter_AddRefs(requestBaseURI), "about:blank"_ns);
    } else {
      requestBaseURI = aRequest->BaseURL();
    }
    MOZ_ASSERT(aRequest->mLoadedScript->IsClassicScript());
    aRequest->SetBaseURL(requestBaseURI);
  }

  JS::Rooted<JSScript*> script(aCx);
  script = aRequest->IsUTF8Text()
               ? JS::Compile(aCx, options,
                             maybeSource.ref<JS::SourceText<Utf8Unit>>())
               : JS::Compile(aCx, options,
                             maybeSource.ref<JS::SourceText<char16_t>>());
  if (!script) {
    if (loadContext->IsTopLevel()) {
      // This is a top-level worker script,
      //
      // https://html.spec.whatwg.org/#run-a-worker
      // If script is null or if script's error to rethrow is non-null, then:
      //   Queue a global task on the DOM manipulation task source given
      //   worker's relevant global object to fire an event named error at
      //   worker.
      JS_ClearPendingException(aCx);
      mRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    } else {
      // This is a script which is loaded by importScripts().
      //
      // https://html.spec.whatwg.org/#import-scripts-into-worker-global-scope
      // For each url in the resulting URL records:
      //   Fetch a classic worker-imported script given url and settings object,
      //   passing along performFetch if provided. If this succeeds, let script
      //   be the result. Otherwise, rethrow the exception.
      mRv.StealExceptionFromJSContext(aCx);
    }

    return false;
  }

  if (!mWorkerRef->Private()->IsServiceWorker()) {
    aRequest->FetchInfo()->AssociateWithScript(script);
  }

  JS::Rooted<JS::Value> unused(aCx);
  bool successfullyEvaluated = JS_ExecuteScript(aCx, script, &unused);
  if (aRequest->IsCanceled()) {
    return false;
  }
  if (!successfullyEvaluated) {
    mRv.StealExceptionFromJSContext(aCx);
    return false;
  }
  // steal the loadContext so that the cycle is broken and cycle collector can
  // collect the scriptLoadRequest.
  return true;
}

void WorkerScriptLoader::TryShutdown() {
  {
    MutexAutoLock lock(CleanUpLock());
    if (CleanedUp()) {
      return;
    }
  }

  if (AllScriptsExecuted() && AllModuleRequestsLoaded()) {
    ShutdownScriptLoader(!mExecutionAborted, mMutedErrorFlag);
  }
}

void WorkerScriptLoader::ShutdownScriptLoader(bool aResult, bool aMutedError) {
  MOZ_ASSERT(AllScriptsExecuted());
  MOZ_ASSERT(AllModuleRequestsLoaded());
  mWorkerRef->Private()->AssertIsOnWorkerThread();

  if (!aResult) {
    // At this point there are two possibilities:
    //
    // 1) mRv.Failed().  In that case we just want to leave it
    //    as-is, except if it has a JS exception and we need to mute JS
    //    exceptions.  In that case, we log the exception without firing any
    //    events and then replace it on the ErrorResult with a NetworkError,
    //    per spec.
    //
    // 2) mRv succeeded.  As far as I can tell, this can only
    //    happen when loading the main worker script and
    //    GetOrCreateGlobalScope() fails or if ScriptExecutorRunnable::Cancel
    //    got called.  Does it matter what we throw in this case?  I'm not
    //    sure...
    if (mRv.Failed()) {
      if (aMutedError && mRv.IsJSException()) {
        LogExceptionToConsole(mWorkerRef->Private()->GetJSContext(),
                              mWorkerRef->Private());
        mRv.Throw(NS_ERROR_DOM_NETWORK_ERR);
      }
    } else {
      mRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    }
  }

  // Lock, shutdown, and cleanup state. After this the Loader is closed.
  {
    MutexAutoLock lock(CleanUpLock());

    if (CleanedUp()) {
      return;
    }

    mWorkerRef->Private()->AssertIsOnWorkerThread();
    // Module loader doesn't use sync loop for dynamic import
    if (mSyncLoopTarget) {
      mWorkerRef->Private()->MaybeStopSyncLoop(
          mSyncLoopTarget, aResult ? NS_OK : NS_ERROR_FAILURE);
      mSyncLoopTarget = nullptr;
    }

    // Signal cleanup
    mCleanedUp = true;

    // Allow worker shutdown.
    mWorkerRef = nullptr;
  }
}

void WorkerScriptLoader::ReportErrorToConsole(ScriptLoadRequest* aRequest,
                                              nsresult aResult) const {
  nsAutoString url = NS_ConvertUTF8toUTF16(aRequest->mURL);
  workerinternals::ReportLoadError(const_cast<ErrorResult&>(mRv), aResult, url);
}

void WorkerScriptLoader::LogExceptionToConsole(JSContext* aCx,
                                               WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  MOZ_ASSERT(mRv.IsJSException());

  JS::Rooted<JS::Value> exn(aCx);
  if (!ToJSValue(aCx, std::move(mRv), &exn)) {
    return;
  }

  // Now the exception state should all be in exn.
  MOZ_ASSERT(!JS_IsExceptionPending(aCx));
  MOZ_ASSERT(!mRv.Failed());

  JS::ExceptionStack exnStack(aCx, exn, nullptr);
  JS::ErrorReportBuilder report(aCx);
  if (!report.init(aCx, exnStack, JS::ErrorReportBuilder::WithSideEffects)) {
    JS_ClearPendingException(aCx);
    return;
  }

  RefPtr<xpc::ErrorReport> xpcReport = new xpc::ErrorReport();
  xpcReport->Init(report.report(), report.toStringResult().c_str(),
                  aWorkerPrivate->IsChromeWorker(), aWorkerPrivate->WindowID());

  RefPtr<AsyncErrorReporter> r = new AsyncErrorReporter(xpcReport);
  NS_DispatchToMainThread(r);
}

bool WorkerScriptLoader::AllModuleRequestsLoaded() const {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  return mLoadingModuleRequestCount == 0;
}

void WorkerScriptLoader::IncreaseLoadingModuleRequestCount() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  ++mLoadingModuleRequestCount;
}

void WorkerScriptLoader::DecreaseLoadingModuleRequestCount() {
  mWorkerRef->Private()->AssertIsOnWorkerThread();
  --mLoadingModuleRequestCount;
}

NS_IMPL_ISUPPORTS(ScriptLoaderRunnable, nsIRunnable, nsINamed)

NS_IMPL_ISUPPORTS(WorkerScriptLoader, nsINamed)

ScriptLoaderRunnable::ScriptLoaderRunnable(
    WorkerScriptLoader* aScriptLoader,
    nsTArray<RefPtr<ThreadSafeRequestHandle>> aLoadingRequests)
    : mScriptLoader(aScriptLoader),
      mWorkerRef(aScriptLoader->mWorkerRef),
      mLoadingRequests(std::move(aLoadingRequests)),
      mCancelMainThread(Nothing()) {
  MOZ_ASSERT(aScriptLoader);
}

nsresult ScriptLoaderRunnable::Run() {
  AssertIsOnMainThread();

  // Convert the origin stack to JSON (which must be done on the main
  // thread) explicitly, so that we can use the stack to notify the net
  // monitor about every script we load. We do this, rather than pass
  // the stack directly to the netmonitor, in order to be able to use this
  // for all subsequent scripts.
  if (mScriptLoader->mOriginStack &&
      mScriptLoader->mOriginStackJSON.IsEmpty()) {
    ConvertSerializedStackToJSON(std::move(mScriptLoader->mOriginStack),
                                 mScriptLoader->mOriginStackJSON);
  }

  if (!mWorkerRef->Private()->IsServiceWorker() ||
      mScriptLoader->IsDebuggerScript()) {
    for (ThreadSafeRequestHandle* handle : mLoadingRequests) {
      handle->SetRunnable(this);
    }

    for (ThreadSafeRequestHandle* handle : mLoadingRequests) {
      nsresult rv = mScriptLoader->LoadScript(handle);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        LoadingFinished(handle, rv);
        CancelMainThread(rv);
        return rv;
      }
    }

    return NS_OK;
  }

  MOZ_ASSERT(!mCacheCreator);
  mCacheCreator = new CacheCreator(mWorkerRef->Private());

  for (ThreadSafeRequestHandle* handle : mLoadingRequests) {
    handle->SetRunnable(this);
    WorkerLoadContext* loadContext = handle->GetContext();
    mCacheCreator->AddLoader(MakeNotNull<RefPtr<CacheLoadHandler>>(
        mWorkerRef, handle, loadContext->IsTopLevel(),
        loadContext->mOnlyExistingCachedResourcesAllowed, mScriptLoader));
  }

  // The worker may have a null principal on first load, but in that case its
  // parent definitely will have one.
  nsIPrincipal* principal = mWorkerRef->Private()->GetPrincipal();
  if (!principal) {
    WorkerPrivate* parentWorker = mWorkerRef->Private()->GetParent();
    MOZ_ASSERT(parentWorker, "Must have a parent!");
    principal = parentWorker->GetPrincipal();
  }

  nsresult rv = mCacheCreator->Load(principal);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CancelMainThread(rv);
    return rv;
  }

  return NS_OK;
}

nsresult ScriptLoaderRunnable::OnStreamComplete(
    ThreadSafeRequestHandle* aRequestHandle, nsresult aStatus) {
  AssertIsOnMainThread();

  LoadingFinished(aRequestHandle, aStatus);
  return NS_OK;
}

void ScriptLoaderRunnable::LoadingFinished(
    ThreadSafeRequestHandle* aRequestHandle, nsresult aRv) {
  AssertIsOnMainThread();

  WorkerLoadContext* loadContext = aRequestHandle->GetContext();

  loadContext->mLoadResult = aRv;
  MOZ_ASSERT(!loadContext->mLoadingFinished);
  loadContext->mLoadingFinished = true;

  if (loadContext->IsTopLevel() && NS_SUCCEEDED(aRv)) {
    MOZ_DIAGNOSTIC_ASSERT(
        mWorkerRef->Private()->PrincipalURIMatchesScriptURL());
  }

  MaybeExecuteFinishedScripts(aRequestHandle);
}

void ScriptLoaderRunnable::MaybeExecuteFinishedScripts(
    ThreadSafeRequestHandle* aRequestHandle) {
  AssertIsOnMainThread();

  // We execute the last step if we don't have a pending operation with the
  // cache and the loading is completed.
  WorkerLoadContext* loadContext = aRequestHandle->GetContext();
  if (!loadContext->IsAwaitingPromise()) {
    if (aRequestHandle->GetContext()->IsTopLevel()) {
      mWorkerRef->Private()->WorkerScriptLoaded();
    }
    DispatchProcessPendingRequests();
  }
}

void ScriptLoaderRunnable::CancelMainThreadWithBindingAborted() {
  AssertIsOnMainThread();
  CancelMainThread(NS_BINDING_ABORTED);
}

void ScriptLoaderRunnable::CancelMainThread(nsresult aCancelResult) {
  AssertIsOnMainThread();
  if (IsCancelled()) {
    return;
  }

  {
    MutexAutoLock lock(mScriptLoader->CleanUpLock());

    // Check if we have already cancelled, or if the worker has been killed
    // before we cancel.
    if (mScriptLoader->CleanedUp()) {
      return;
    }

    mCancelMainThread = Some(aCancelResult);

    for (ThreadSafeRequestHandle* handle : mLoadingRequests) {
      // Finishing an earlier request can have scheduled execution of this one,
      // handing it over to the worker thread.
      if (handle->ExecutionScheduled()) {
        continue;
      }

      bool callLoadingFinished = true;

      WorkerLoadContext* loadContext = handle->GetContext();
      if (!loadContext) {
        continue;
      }

      if (loadContext->IsAwaitingPromise()) {
        MOZ_ASSERT(mWorkerRef->Private()->IsServiceWorker());
        loadContext->mCachePromise->MaybeReject(NS_BINDING_ABORTED);
        loadContext->mCachePromise = nullptr;
        callLoadingFinished = false;
      }
      if (loadContext->mChannel) {
        if (NS_SUCCEEDED(loadContext->mChannel->Cancel(aCancelResult))) {
          callLoadingFinished = false;
        } else {
          NS_WARNING("Failed to cancel channel!");
        }
      }
      if (callLoadingFinished && !loadContext->mLoadingFinished) {
        LoadingFinished(handle, aCancelResult);
      }
    }
    DispatchProcessPendingRequests();
  }
}

void ScriptLoaderRunnable::DispatchProcessPendingRequests() {
  AssertIsOnMainThread();

  const auto begin = mLoadingRequests.begin();
  const auto end = mLoadingRequests.end();
  using Iterator = decltype(begin);
  const auto maybeRangeToExecute =
      [begin, end]() -> Maybe<std::pair<Iterator, Iterator>> {
    // firstItToExecute is the first loadInfo where mExecutionScheduled is
    // unset.
    auto firstItToExecute = std::find_if(
        begin, end, [](const RefPtr<ThreadSafeRequestHandle>& requestHandle) {
          return !requestHandle->ExecutionScheduled();
        });

    if (firstItToExecute == end) {
      return Nothing();
    }

    // firstItUnexecutable is the first loadInfo that is not yet finished.
    // Update mExecutionScheduled on the ones we're about to schedule for
    // execution.
    const auto firstItUnexecutable =
        std::find_if(firstItToExecute, end,
                     [](RefPtr<ThreadSafeRequestHandle>& requestHandle) {
                       MOZ_ASSERT(!requestHandle->ExecutionScheduled());
                       if (!requestHandle->Finished()) {
                         return true;
                       }

                       // We can execute this one.
                       requestHandle->SetExecutionScheduled();

                       return false;
                     });

    return firstItUnexecutable == firstItToExecute
               ? Nothing()
               : Some(std::pair(firstItToExecute, firstItUnexecutable));
  }();

  // If there are no unexecutable load infos, we can unuse things before the
  // execution of the scripts and the stopping of the sync loop.
  if (maybeRangeToExecute) {
    if (maybeRangeToExecute->second == end) {
      mCacheCreator = nullptr;
    }

    RefPtr<ScriptExecutorRunnable> runnable = new ScriptExecutorRunnable(
        mScriptLoader, mWorkerRef->Private(), mScriptLoader->mSyncLoopTarget,
        Span<RefPtr<ThreadSafeRequestHandle>>{maybeRangeToExecute->first,
                                              maybeRangeToExecute->second});

    if (!runnable->Dispatch(mWorkerRef->Private()) &&
        mScriptLoader->mSyncLoopTarget) {
      MOZ_ASSERT(false, "This should never fail!");
    }
  }
}

ScriptExecutorRunnable::ScriptExecutorRunnable(
    WorkerScriptLoader* aScriptLoader, WorkerPrivate* aWorkerPrivate,
    nsISerialEventTarget* aSyncLoopTarget,
    Span<RefPtr<ThreadSafeRequestHandle>> aLoadedRequests)
    : MainThreadWorkerSyncRunnable(aSyncLoopTarget, "ScriptExecutorRunnable"),
      mScriptLoader(aScriptLoader),
      mLoadedRequests(aLoadedRequests) {}

bool ScriptExecutorRunnable::IsDebuggerRunnable() const {
  // ScriptExecutorRunnable is used to execute both worker and debugger scripts.
  // In the latter case, the runnable needs to be dispatched to the debugger
  // queue.
  return mScriptLoader->IsDebuggerScript();
}

bool ScriptExecutorRunnable::PreRun(WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  // We must be on the same worker as we started on.
  MOZ_ASSERT(
      mScriptLoader->mSyncLoopTarget == mSyncLoopTarget,
      "Unexpected SyncLoopTarget. Check if the sync loop was closed early");

  {
    // There is a possibility that we cleaned up while this task was waiting to
    // run. If this has happened, return and exit.
    MutexAutoLock lock(mScriptLoader->CleanUpLock());
    if (mScriptLoader->CleanedUp()) {
      return true;
    }

    const auto& requestHandle = mLoadedRequests[0];
    // Check if the request is still valid.
    if (requestHandle->IsEmpty() ||
        !requestHandle->GetContext()->IsTopLevel()) {
      return true;
    }
  }

  return mScriptLoader->StorePolicyContainerArgs();
}

bool ScriptExecutorRunnable::ProcessModuleScript(
    JSContext* aCx, WorkerPrivate* aWorkerPrivate) {
  // We should only ever have one script when processing modules
  MOZ_ASSERT(mLoadedRequests.Length() == 1);
  RefPtr<ScriptLoadRequest> request;
  {
    // There is a possibility that we cleaned up while this task was waiting to
    // run. If this has happened, return and exit.
    MutexAutoLock lock(mScriptLoader->CleanUpLock());
    if (mScriptLoader->CleanedUp()) {
      return true;
    }

    MOZ_ASSERT(mLoadedRequests.Length() == 1);
    const auto& requestHandle = mLoadedRequests[0];
    // The request must be valid.
    MOZ_ASSERT(!requestHandle->IsEmpty());

    // Release the request to the worker. From this point on, the Request Handle
    // is empty.
    request = requestHandle->ReleaseRequest();

    // release lock. We will need it later if we cleanup.
  }

  MOZ_ASSERT(request->IsModuleRequest());

  WorkerLoadContext* loadContext = request->GetWorkerLoadContext();
  ModuleLoadRequest* moduleRequest = request->AsModuleRequest();
  if (aWorkerPrivate->GetReferrerPolicy() != ReferrerPolicy::_empty) {
    moduleRequest->UpdateReferrerPolicy(aWorkerPrivate->GetReferrerPolicy());
  }

  // DecreaseLoadingModuleRequestCount must be called before OnFetchComplete.
  // OnFetchComplete will call ProcessPendingRequests, and in
  // ProcessPendingRequests it will try to shutdown if
  // AllModuleRequestsLoaded() returns true.
  mScriptLoader->DecreaseLoadingModuleRequestCount();
  moduleRequest->OnFetchComplete(loadContext->mLoadResult);

  if (NS_FAILED(loadContext->mLoadResult)) {
    if (moduleRequest->IsDynamicImport() || !moduleRequest->IsTopLevel()) {
      mScriptLoader->TryShutdown();
    }
  }
  return true;
}

bool ScriptExecutorRunnable::ProcessClassicScripts(
    JSContext* aCx, WorkerPrivate* aWorkerPrivate) {
  // There is a possibility that we cleaned up while this task was waiting to
  // run. If this has happened, return and exit.
  {
    MutexAutoLock lock(mScriptLoader->CleanUpLock());
    if (mScriptLoader->CleanedUp()) {
      return true;
    }

    for (const auto& requestHandle : mLoadedRequests) {
      // The request must be valid.
      MOZ_ASSERT(!requestHandle->IsEmpty());

      // Release the request to the worker. From this point on, the Request
      // Handle is empty.
      RefPtr<ScriptLoadRequest> request = requestHandle->ReleaseRequest();
      mScriptLoader->MaybeMoveToLoadedList(request);
    }
  }
  return mScriptLoader->ProcessPendingRequests(aCx);
}

bool ScriptExecutorRunnable::WorkerRun(JSContext* aCx,
                                       WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  // We must be on the same worker as we started on.
  MOZ_ASSERT(
      mScriptLoader->mSyncLoopTarget == mSyncLoopTarget,
      "Unexpected SyncLoopTarget. Check if the sync loop was closed early");

  if (mLoadedRequests.begin()->get()->GetContext()->IsTopLevel()) {
    aWorkerPrivate->InitializeGlobalReportingEndpoints();
  }

  if (mLoadedRequests.begin()->get()->GetRequest()->IsModuleRequest()) {
    return ProcessModuleScript(aCx, aWorkerPrivate);
  }

  return ProcessClassicScripts(aCx, aWorkerPrivate);
}

nsresult ScriptExecutorRunnable::Cancel() {
  if (mScriptLoader->AllScriptsExecuted() &&
      mScriptLoader->AllModuleRequestsLoaded()) {
    mScriptLoader->ShutdownScriptLoader(false, false);
  }
  return NS_OK;
}

} /* namespace loader */

nsresult ChannelFromScriptURLMainThread(
    nsIPrincipal* aPrincipal, Document* aParentDoc, nsILoadGroup* aLoadGroup,
    nsIURI* aScriptURL, const WorkerType& aWorkerType,
    const RequestCredentials& aCredentials,
    const Maybe<ClientInfo>& aClientInfo,
    nsContentPolicyType aMainScriptContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings, nsIReferrerInfo* aReferrerInfo,
    nsIChannel** aChannel) {
  AssertIsOnMainThread();

  nsCOMPtr<nsIIOService> ios(do_GetIOService());

  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  NS_ASSERTION(secMan, "This should never be null!");

  uint32_t secFlags;
  nsresult rv;
  if (aWorkerType == WorkerType::Module) {
    rv = GetModuleSecFlags(true, aPrincipal, WorkerScript, aScriptURL,
                           aCredentials, secFlags);
  } else {
    rv = GetClassicSecFlags(true, aScriptURL, aPrincipal, WorkerScript,
                            secFlags);
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  return ChannelFromScriptURL(
      aPrincipal, aParentDoc, nullptr, aLoadGroup, ios, secMan, aScriptURL,
      aClientInfo, Maybe<ServiceWorkerDescriptor>(), true, WorkerScript,
      aMainScriptContentPolicyType, nsIRequest::LOAD_NORMAL, secFlags,
      aCookieJarSettings, aReferrerInfo, aChannel);
}

nsresult ChannelFromScriptURLWorkerThread(
    JSContext* aCx, WorkerPrivate* aParent, const nsAString& aScriptURL,
    const WorkerType& aWorkerType, const RequestCredentials& aCredentials,
    WorkerLoadInfo& aLoadInfo) {
  aParent->AssertIsOnWorkerThread();

  RefPtr<ChannelGetterRunnable> getter = new ChannelGetterRunnable(
      aParent, aScriptURL, aWorkerType, aCredentials, aLoadInfo);

  ErrorResult rv;
  getter->Dispatch(aParent, Canceling, rv);
  if (rv.Failed()) {
    NS_ERROR("Failed to dispatch!");
    return rv.StealNSResult();
  }

  return getter->GetResult();
}

void ReportLoadError(ErrorResult& aRv, nsresult aLoadResult,
                     const nsAString& aScriptURL) {
  MOZ_ASSERT(!aRv.Failed());

  nsPrintfCString err("Failed to load worker script at \"%s\"",
                      NS_ConvertUTF16toUTF8(aScriptURL).get());

  switch (aLoadResult) {
    case NS_ERROR_MALFORMED_URI:
    case NS_ERROR_DOM_SYNTAX_ERR:
      aRv.ThrowSyntaxError(err);
      break;

    case NS_BINDING_ABORTED:
      // Note: we used to pretend like we didn't set an exception for
      // NS_BINDING_ABORTED, but then ShutdownScriptLoader did it anyway.  The
      // other callsite, in WorkerPrivate::Constructor, never passed in
      // NS_BINDING_ABORTED.  So just throw it directly here.  Consumers will
      // deal as needed.  But note that we do NOT want to use one of the
      // Throw*Error() methods on ErrorResult for this case, because that will
      // make it impossible for consumers to realize that our error was
      // NS_BINDING_ABORTED.
      aRv.Throw(aLoadResult);
      break;

    case NS_ERROR_DOM_BAD_URI:
      // This is actually a security error.
    case NS_ERROR_DOM_SECURITY_ERR:
      aRv.ThrowSecurityError(err);
      break;

    case NS_ERROR_FILE_NOT_FOUND:
    case NS_ERROR_NOT_AVAILABLE:
    case NS_ERROR_CORRUPTED_CONTENT:
    case NS_ERROR_DOM_NETWORK_ERR:
    // For lack of anything better, go ahead and throw a NetworkError here.
    // We don't want to throw a JS exception, because for toplevel script
    // loads that would get squelched.
    default:
      aRv.Throw(NS_ERROR_DOM_NETWORK_ERR);
      break;
  }
}

void LoadMainScript(WorkerPrivate* aWorkerPrivate,
                    UniquePtr<SerializedStackHolder> aOriginStack,
                    const nsAString& aScriptURL,
                    WorkerScriptType aWorkerScriptType, ErrorResult& aRv,
                    const mozilla::Encoding* aDocumentEncoding) {
  nsTArray<nsString> scriptURLs;

  scriptURLs.AppendElement(aScriptURL);

  LoadAllScripts(aWorkerPrivate, std::move(aOriginStack), scriptURLs, true,
                 aWorkerScriptType, aRv, aDocumentEncoding);
}

void Load(WorkerPrivate* aWorkerPrivate,
          UniquePtr<SerializedStackHolder> aOriginStack,
          const nsTArray<nsString>& aScriptURLs,
          WorkerScriptType aWorkerScriptType, ErrorResult& aRv) {
  const uint32_t urlCount = aScriptURLs.Length();

  if (!urlCount) {
    return;
  }

  if (urlCount > MAX_CONCURRENT_SCRIPTS) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  LoadAllScripts(aWorkerPrivate, std::move(aOriginStack), aScriptURLs, false,
                 aWorkerScriptType, aRv);
}

}  // namespace mozilla::dom::workerinternals
