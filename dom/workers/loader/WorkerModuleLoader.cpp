/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkerModuleLoader.h"

#include "js/Modules.h"
#include "js/experimental/JSStencil.h"  // JS::Stencil, JS::CompileModuleScriptToStencil, JS::InstantiateModuleStencil
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/loader/ModuleLoadRequest.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/WorkerLoadContext.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/workerinternals/ScriptLoader.h"
#include "nsISupportsImpl.h"

namespace mozilla::dom::workerinternals::loader {

//////////////////////////////////////////////////////////////
// WorkerModuleLoader
//////////////////////////////////////////////////////////////

NS_IMPL_ADDREF_INHERITED(WorkerModuleLoader, JS::loader::ModuleLoaderBase)
NS_IMPL_RELEASE_INHERITED(WorkerModuleLoader, JS::loader::ModuleLoaderBase)

NS_IMPL_CYCLE_COLLECTION_INHERITED(WorkerModuleLoader,
                                   JS::loader::ModuleLoaderBase)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkerModuleLoader)
NS_INTERFACE_MAP_END_INHERITING(JS::loader::ModuleLoaderBase)

WorkerModuleLoader::WorkerModuleLoader(WorkerScriptLoader* aScriptLoader,
                                       nsIGlobalObject* aGlobalObject)
    : ModuleLoaderBase(aScriptLoader, aGlobalObject) {}

nsIURI* WorkerModuleLoader::GetBaseURI() const {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  return workerPrivate->GetBaseURI();
}

nsIURI* WorkerModuleLoader::GetClientReferrerURI() {
  // https://w3c.github.io/webappsec-referrer-policy/#determine-requests-referrer
  // Step 3. "client":
  //   2. let referrerSource be environment’s creation URL.
  //
  // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-creation-url
  // https://html.spec.whatwg.org/multipage/workers.html#set-up-a-worker-environment-settings-object
  return GetBaseURI();
}

already_AddRefed<JS::loader::ScriptFetchOptions>
WorkerModuleLoader::CreateDefaultScriptFetchOptions() {
  RefPtr<ScriptFetchOptions> options = ScriptFetchOptions::CreateDefault();
  return options.forget();
}

already_AddRefed<ModuleLoadRequest> WorkerModuleLoader::CreateRequest(
    JSContext* aCx, nsIURI* aURI, JS::Handle<JSObject*> aModuleRequest,
    JS::Handle<JS::Value> aHostDefined, JS::Handle<JS::Value> aPayload,
    bool aIsDynamicImport, ScriptFetchOptions* aOptions,
    mozilla::dom::ReferrerPolicy aReferrerPolicy, nsIURI* aBaseURL,
    const mozilla::dom::SRIMetadata& aSriMetadata) {
  Maybe<ClientInfo> clientInfo = GetGlobalObject()->GetClientInfo();

  ModuleLoadRequest::Kind kind;
  RefPtr<WorkerLoadContext> loadContext;
  ModuleLoadRequest* root = nullptr;
  if (aIsDynamicImport) {
    if (!CreateDynamicImportLoader()) {
      return nullptr;
    }

    loadContext = new WorkerLoadContext(
        WorkerLoadContext::Kind::DynamicImport, clientInfo,
        GetCurrentScriptLoader(),
        // When dynamic import is supported in ServiceWorkers,
        // the current plan in onlyExistingCachedResourcesAllowed
        // is that only existing cached resources will be
        // allowed.  (`import()` will not be used for caching
        // side effects, but instead a specific method will be
        // used during installation.)
        true);

    kind = ModuleLoadRequest::Kind::DynamicImport;
  } else {
    MOZ_ASSERT(!aHostDefined.isUndefined());
    root = static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
    MOZ_ASSERT(root);
    WorkerLoadContext* context = root->mLoadContext->AsWorkerContext();
    loadContext = new WorkerLoadContext(
        WorkerLoadContext::Kind::StaticImport, clientInfo,
        context->mScriptLoader, context->mOnlyExistingCachedResourcesAllowed);
    kind = ModuleLoadRequest::Kind::StaticImport;
  }

  JS::ModuleType moduleType = JS::GetModuleRequestType(aCx, aModuleRequest);
  RefPtr<ModuleLoadRequest> request = new ModuleLoadRequest(
      moduleType, SRIMetadata(), aBaseURL, loadContext, kind, this, root);

  request->mURL = aURI->GetSpecOrDefault();
  request->NoCacheEntryFound(aReferrerPolicy, aOptions, aURI);
  return request.forget();
}

bool WorkerModuleLoader::CreateDynamicImportLoader() {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  workerPrivate->AssertIsOnWorkerThread();

  RefPtr<WorkerScriptLoader> loader = loader::WorkerScriptLoader::Create(
      workerPrivate, nullptr, nullptr,
      GetCurrentScriptLoader()->GetWorkerScriptType());
  if (NS_WARN_IF(!loader)) {
    return false;
  }

  SetScriptLoader(loader);
  return true;
}

bool WorkerModuleLoader::IsDynamicImportSupported() {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  // Not supported for Service Workers.
  // https://github.com/w3c/ServiceWorker/issues/1585 covers existing discussion
  // about potentially supporting use of import().
  return !workerPrivate->IsServiceWorker();
}

bool WorkerModuleLoader::IsForServiceWorker() const {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  return workerPrivate && workerPrivate->IsServiceWorker();
}

bool WorkerModuleLoader::CanStartLoad(ModuleLoadRequest* aRequest,
                                      nsresult* aRvOut) {
  return true;
}

nsresult WorkerModuleLoader::StartFetch(ModuleLoadRequest* aRequest) {
  if (!GetScriptLoaderFor(aRequest)->DispatchLoadScript(aRequest)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult WorkerModuleLoader::CompileFetchedModule(
    JSContext* aCx, JS::Handle<JSObject*> aGlobal, JS::CompileOptions& aOptions,
    ModuleLoadRequest* aRequest, JS::MutableHandle<JSObject*> aModuleScript) {
  switch (aRequest->mModuleType) {
    case JS::ModuleType::Unknown:
    case JS::ModuleType::Bytes:
      MOZ_CRASH("Unexpected module type");
    case JS::ModuleType::JavaScriptOrWasm:
      return CompileJavaScriptOrWasmModule(aCx, aOptions, aRequest,
                                           aModuleScript);
    case JS::ModuleType::JSON:
      return CompileJsonModule(aCx, aOptions, aRequest, aModuleScript);
    case JS::ModuleType::CSS:
      MOZ_CRASH("CSS modules are not supported in workers");
    case JS::ModuleType::Text:
      return CreateTextModule(aCx, aOptions, aRequest, aModuleScript);
  }

  MOZ_CRASH("Unhandled module type");
}

nsresult WorkerModuleLoader::CompileJavaScriptOrWasmModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleScript) {
#ifdef NIGHTLY_BUILD
  if (aRequest->HasWasmMimeTypeEssence()) {
    MOZ_ASSERT(aRequest->IsWasmBytes());
    JSObject* wasmModule =
        JS::CompileWasmModule(aCx, aOptions, aRequest->WasmBytes());
    if (!wasmModule) {
      return NS_ERROR_FAILURE;
    }

    aModuleScript.set(wasmModule);
    return NS_OK;
  }
#endif
  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                          aRequest->mLoadContext.get());
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<JS::Stencil> stencil;

  auto compile = [&](auto& source) {
    return JS::CompileModuleScriptToStencil(aCx, aOptions, source);
  };
  stencil = maybeSource.mapNonEmpty(compile);

  if (!stencil) {
    return NS_ERROR_FAILURE;
  }

  JS::InstantiateOptions instantiateOptions(aOptions);
  aModuleScript.set(
      JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
  if (!aModuleScript) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult WorkerModuleLoader::CompileJsonModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleScript) {
  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                          aRequest->mLoadContext.get());
  NS_ENSURE_SUCCESS(rv, rv);

  auto compile = [&](auto& source) {
    return JS::CompileJsonModule(aCx, aOptions, source);
  };

  auto* jsonModule = maybeSource.mapNonEmpty(compile);
  if (!jsonModule) {
    return NS_ERROR_FAILURE;
  }

  aModuleScript.set(jsonModule);
  return NS_OK;
}

nsresult WorkerModuleLoader::CreateTextModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleScript) {
  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());

  MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                          aRequest->mLoadContext.get());
  NS_ENSURE_SUCCESS(rv, rv);

  auto compile = [&](auto& source) -> JSObject* {
    using T = decltype(source);
    static_assert(std::is_same_v<T, JS::SourceText<char16_t>&> ||
                  std::is_same_v<T, JS::SourceText<Utf8Unit>&>);

    JSString* str;
    if constexpr (std::is_same_v<T, JS::SourceText<Utf8Unit>&>) {
      str = JS_NewStringCopyUTF8N(aCx,
                                  JS::UTF8Chars(source.get(), source.length()));
    } else {
      str = JS_NewUCStringCopyN(aCx, source.get(), source.length());
    }
    if (!str) {
      return nullptr;
    }

    JS::Rooted<JS::Value> defaultExport(aCx, JS::StringValue(str));
    return JS::CreateDefaultExportSyntheticModule(aCx, defaultExport);
  };

  auto* textModule = maybeSource.mapNonEmpty(compile);
  if (!textModule) {
    return NS_ERROR_FAILURE;
  }

  aModuleScript.set(textModule);
  return NS_OK;
}

WorkerScriptLoader* WorkerModuleLoader::GetCurrentScriptLoader() {
  return static_cast<WorkerScriptLoader*>(mLoader.get());
}

WorkerScriptLoader* WorkerModuleLoader::GetScriptLoaderFor(
    ModuleLoadRequest* aRequest) {
  return aRequest->GetWorkerLoadContext()->mScriptLoader;
}

void WorkerModuleLoader::OnModuleLoadComplete(ModuleLoadRequest* aRequest) {
  if (aRequest->IsStaticImport()) {
    return;
  }

  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(GetGlobalObject()))) {
    return;
  }
  RefPtr<WorkerScriptLoader> requestScriptLoader = GetScriptLoaderFor(aRequest);
  if (aRequest->IsDynamicImport()) {
    aRequest->ProcessDynamicImport();
    requestScriptLoader->TryShutdown();
  } else {
    requestScriptLoader->MaybeMoveToLoadedList(aRequest);
    requestScriptLoader->ProcessPendingRequests(jsapi.cx());
  }
}

bool WorkerModuleLoader::IsModuleEvaluationAborted(
    ModuleLoadRequest* aRequest) {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  return !workerPrivate || !workerPrivate->GlobalScope() ||
         workerPrivate->GlobalScope()->IsDying();
}

}  // namespace mozilla::dom::workerinternals::loader
