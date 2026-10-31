/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkletModuleLoader.h"

#include "js/CompileOptions.h"  // JS::InstantiateOptions
#include "js/Modules.h"
#include "js/experimental/JSStencil.h"  // JS::CompileModuleScriptToStencil, JS::InstantiateModuleStencil
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/loader/ModuleLoadRequest.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/dom/StructuredCloneHolder.h"
#include "mozilla/dom/Worklet.h"
#include "mozilla/dom/WorkletFetchHandler.h"
#include "nsStringBundle.h"

using JS::loader::ModuleLoadRequest;
using JS::loader::ResolveError;

namespace mozilla::dom::loader {

//////////////////////////////////////////////////////////////
// WorkletScriptLoader
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkletScriptLoader)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(WorkletScriptLoader)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WorkletScriptLoader)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WorkletScriptLoader)

nsresult WorkletScriptLoader::FillCompileOptionsForRequest(
    JSContext* cx, ScriptLoadRequest* aRequest, JS::CompileOptions* aOptions,
    JS::MutableHandle<JSScript*> aIntroductionScript) {
  aOptions->setIntroductionType("Worklet");
  aOptions->setFileAndLine(aRequest->mURL.get(), 1);
  aOptions->setIsRunOnce(true);
  aOptions->setNoScriptRval(true);
  return NS_OK;
}

//////////////////////////////////////////////////////////////
// WorkletModuleLoader
//////////////////////////////////////////////////////////////

NS_IMPL_ADDREF_INHERITED(WorkletModuleLoader, ModuleLoaderBase)
NS_IMPL_RELEASE_INHERITED(WorkletModuleLoader, ModuleLoaderBase)

NS_IMPL_CYCLE_COLLECTION_INHERITED(WorkletModuleLoader, ModuleLoaderBase,
                                   mFetchingRequests)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WorkletModuleLoader)
NS_INTERFACE_MAP_END_INHERITING(ModuleLoaderBase)

WorkletModuleLoader::WorkletModuleLoader(WorkletScriptLoader* aScriptLoader,
                                         nsIGlobalObject* aGlobalObject)
    : ModuleLoaderBase(aScriptLoader, aGlobalObject) {
  // This should be constructed on a worklet thread.
  MOZ_ASSERT(!NS_IsMainThread());
}

already_AddRefed<ModuleLoadRequest> WorkletModuleLoader::CreateRequest(
    JSContext* aCx, nsIURI* aURI, JS::Handle<JSObject*> aModuleRequest,
    JS::Handle<JS::Value> aHostDefined, JS::Handle<JS::Value> aPayload,
    bool aIsDynamicImport, ScriptFetchOptions* aOptions,
    dom::ReferrerPolicy aReferrerPolicy, nsIURI* aBaseURL,
    const dom::SRIMetadata& aSriMetadata) {
  JS::ModuleType moduleType = GetModuleRequestType(aCx, aModuleRequest);
  ModuleLoadRequest* root = nullptr;
  MOZ_ASSERT(!aHostDefined.isUndefined());
  root = static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
  MOZ_ASSERT(root);
  WorkletLoadContext* context = root->mLoadContext->AsWorkletContext();
  const nsMainThreadPtrHandle<WorkletFetchHandler>& handlerRef =
      context->GetHandlerRef();
  RefPtr<WorkletLoadContext> loadContext = new WorkletLoadContext(handlerRef);
  RefPtr<ModuleLoadRequest> request =
      new ModuleLoadRequest(moduleType, SRIMetadata(), aBaseURL, loadContext,
                            ModuleLoadRequest::Kind::StaticImport, this, root);

  request->mURL = aURI->GetSpecOrDefault();
  request->NoCacheEntryFound(aReferrerPolicy, aOptions, aURI);
  return request.forget();
}

bool WorkletModuleLoader::CanStartLoad(ModuleLoadRequest* aRequest,
                                       nsresult* aRvOut) {
  return true;
}

nsresult WorkletModuleLoader::StartFetch(ModuleLoadRequest* aRequest) {
  InsertRequest(aRequest->URI(), aRequest);

  RefPtr<StartFetchRunnable> runnable =
      new StartFetchRunnable(aRequest->GetWorkletLoadContext()->GetHandlerRef(),
                             aRequest->URI(), aRequest->mReferrer);
  NS_DispatchToMainThread(runnable.forget());
  return NS_OK;
}

nsresult WorkletModuleLoader::CompileFetchedModule(
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
      MOZ_CRASH("CSS modules are not supported in worklets");
    case JS::ModuleType::Text:
      return CreateTextModule(aCx, aOptions, aRequest, aModuleScript);
  }

  MOZ_CRASH("Unhandled module type");
}

nsresult WorkletModuleLoader::CompileJavaScriptOrWasmModule(
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
  return aModuleScript ? NS_OK : NS_ERROR_FAILURE;
}

nsresult WorkletModuleLoader::CompileJsonModule(
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

nsresult WorkletModuleLoader::CreateTextModule(
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

// AddModuleResultRunnable is a Runnable which will notify the result of
// Worklet::AddModule on the main thread.
class AddModuleResultRunnable final : public Runnable {
 public:
  explicit AddModuleResultRunnable(
      const nsMainThreadPtrHandle<WorkletFetchHandler>& aHandlerRef,
      bool aSucceeded)
      : Runnable("Worklet::AddModuleResultRunnable"),
        mHandlerRef(aHandlerRef),
        mSucceeded(aSucceeded) {
    MOZ_ASSERT(!NS_IsMainThread());
  }

  ~AddModuleResultRunnable() = default;

  NS_IMETHOD
  Run() override;

 private:
  nsMainThreadPtrHandle<WorkletFetchHandler> mHandlerRef;
  bool mSucceeded;
};

NS_IMETHODIMP
AddModuleResultRunnable::Run() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mSucceeded) {
    mHandlerRef->ExecutionSucceeded();
  } else {
    mHandlerRef->ExecutionFailed();
  }

  return NS_OK;
}

class AddModuleThrowErrorRunnable final : public Runnable,
                                          public StructuredCloneHolder {
 public:
  explicit AddModuleThrowErrorRunnable(
      const nsMainThreadPtrHandle<WorkletFetchHandler>& aHandlerRef)
      : Runnable("Worklet::AddModuleThrowErrorRunnable"),
        StructuredCloneHolder(CloningSupported, TransferringNotSupported,
                              StructuredCloneScope::SameProcess),
        mHandlerRef(aHandlerRef) {
    MOZ_ASSERT(!NS_IsMainThread());
  }

  ~AddModuleThrowErrorRunnable() = default;

  NS_IMETHOD
  Run() override;

 private:
  nsMainThreadPtrHandle<WorkletFetchHandler> mHandlerRef;
};

NS_IMETHODIMP
AddModuleThrowErrorRunnable::Run() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIGlobalObject> global =
      do_QueryInterface(mHandlerRef->mWorklet->GetParentObject());
  MOZ_ASSERT(global);

  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(global))) {
    mHandlerRef->ExecutionFailed();
    return NS_ERROR_FAILURE;
  }

  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> error(cx);
  IgnoredErrorResult result;
  Read(cx, &error, result);
  (void)NS_WARN_IF(result.Failed());
  mHandlerRef->ExecutionFailed(error);

  return NS_OK;
}

void WorkletModuleLoader::OnModuleLoadComplete(ModuleLoadRequest* aRequest) {
  if (aRequest->IsStaticImport()) {
    return;
  }

  const nsMainThreadPtrHandle<WorkletFetchHandler>& handlerRef =
      aRequest->GetWorkletLoadContext()->GetHandlerRef();

  auto addModuleFailed = MakeScopeExit([&] {
    RefPtr<AddModuleResultRunnable> runnable =
        new AddModuleResultRunnable(handlerRef, false);
    NS_DispatchToMainThread(runnable.forget());
  });

  if (!aRequest->mModuleScript) {
    return;
  }

  bool hasParseError = aRequest->mModuleScript->HasParseError();
  bool hasError = aRequest->mModuleScript->HasErrorToRethrow();

  if (!hasParseError && !hasError) {
    if (!aRequest->InstantiateModuleGraph()) {
      return;
    }

    nsresult rv = aRequest->EvaluateModule();
    if (NS_FAILED(rv)) {
      return;
    }

    hasError = aRequest->mModuleScript->HasErrorToRethrow();
  }

  if (hasParseError || hasError) {
    AutoJSAPI jsapi;
    if (NS_WARN_IF(!jsapi.Init(GetGlobalObject()))) {
      return;
    }

    JSContext* cx = jsapi.cx();
    JS::Rooted<JS::Value> error(cx);
    if (hasParseError) {
      error = aRequest->mModuleScript->ParseError();
    } else {
      error = aRequest->mModuleScript->ErrorToRethrow();
    }

    RefPtr<AddModuleThrowErrorRunnable> runnable =
        new AddModuleThrowErrorRunnable(handlerRef);

    bool writeOk = [&] {
      IgnoredErrorResult result;
      runnable->Write(cx, error, result);
      return !result.Failed();
    }();

    JS_SetPendingException(cx, error);
    if (!writeOk) {
      return;
    }

    addModuleFailed.release();
    NS_DispatchToMainThread(runnable.forget());
    return;
  }

  addModuleFailed.release();
  RefPtr<AddModuleResultRunnable> runnable =
      new AddModuleResultRunnable(handlerRef, true);
  NS_DispatchToMainThread(runnable.forget());
}

// TODO: Bug 1808301: Call FormatLocalizedString from a worklet thread.
nsresult WorkletModuleLoader::GetResolveFailureMessage(
    ResolveError aError, const nsAString& aSpecifier, nsAString& aResult) {
  uint8_t index = static_cast<uint8_t>(aError);
  MOZ_ASSERT(index < static_cast<uint8_t>(ResolveError::Length));
  MOZ_ASSERT(HasSetLocalizedStrings());
  if (NS_WARN_IF(mLocalizedStrs.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  const nsString& localizedStr = mLocalizedStrs.ElementAt(index);

  AutoTArray<nsString, 1> params;
  params.AppendElement(aSpecifier);

  nsStringBundleBase::FormatString(localizedStr.get(), params, aResult);
  return NS_OK;
}

void WorkletModuleLoader::InsertRequest(nsIURI* aURI,
                                        ModuleLoadRequest* aRequest) {
  mFetchingRequests.InsertOrUpdate(aURI, aRequest);
}

void WorkletModuleLoader::RemoveRequest(nsIURI* aURI) {
  MOZ_ASSERT(mFetchingRequests.Remove(aURI));
}

ModuleLoadRequest* WorkletModuleLoader::GetRequest(nsIURI* aURI) const {
  RefPtr<ModuleLoadRequest> req;
  MOZ_ALWAYS_TRUE(mFetchingRequests.Get(aURI, getter_AddRefs(req)));
  return req;
}

}  // namespace mozilla::dom::loader
