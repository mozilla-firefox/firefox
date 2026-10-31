/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ModuleLoader.h"

#include <type_traits>

#include "GeckoProfiler.h"
#include "ScriptLoader.h"
#include "ScriptTrace.h"        // TRACE_FOR_TEST
#include "js/CompileOptions.h"  // JS::CompileOptions, JS::InstantiateOptions
#include "js/ContextOptions.h"  // JS::ContextOptionsRef
#include "js/MemoryFunctions.h"
#include "js/Modules.h"  // JS::FinishDynamicModuleImport, JS::{G,S}etModuleResolveHook, JS::Get{ModulePrivate,ModuleScript,RequestedModule{s,Specifier,SourcePos}}, JS::SetModule{DynamicImport,Metadata}Hook
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/Realm.h"
#include "js/SourceText.h"
#include "js/experimental/JSStencil.h"  // JS::Stencil, JS::CompileModuleScriptToStencil, JS::InstantiateModuleStencil
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/loader/LoadedScript.h"
#include "js/loader/ModuleLoadRequest.h"
#include "js/loader/ModuleLoaderBase.h"
#include "js/loader/ScriptLoadRequest.h"
#include "jsapi.h"
#include "mozilla/Assertions.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/RequestBinding.h"
#include "nsContentSecurityManager.h"
#include "nsContentSecurityUtils.h"
#include "nsError.h"
#include "nsIContent.h"
#include "nsIPrincipal.h"
#include "nsJSUtils.h"
#include "xpcpublic.h"

using JS::SourceText;
using namespace JS::loader;

namespace mozilla::dom {

#undef LOG
#define LOG(args) \
  MOZ_LOG(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug, args)

#define LOG_ENABLED() \
  MOZ_LOG_TEST(ScriptLoader::gScriptLoaderLog, mozilla::LogLevel::Debug)

//////////////////////////////////////////////////////////////
// DOM module loader
//////////////////////////////////////////////////////////////

ModuleLoader::ModuleLoader(ScriptLoader* aLoader,
                           nsIGlobalObject* aGlobalObject, Kind aKind)
    : ModuleLoaderBase(aLoader, aGlobalObject), mKind(aKind) {}

ScriptLoader* ModuleLoader::GetScriptLoader() {
  return static_cast<ScriptLoader*>(mLoader.get());
}

bool ModuleLoader::CanStartLoad(ModuleLoadRequest* aRequest, nsresult* aRvOut) {
  if (!GetScriptLoader()->GetDocument()) {
    *aRvOut = NS_ERROR_NULL_POINTER;
    return false;
  }

  nsCOMPtr<nsIPrincipal> principal = aRequest->TriggeringPrincipal();
  if (BasePrincipal::Cast(principal)->ContentScriptAddonPolicy()) {
    // To prevent dynamic code execution, content scripts can only
    // load moz-extension URLs.
    if (!aRequest->URI()->SchemeIs("moz-extension")) {
      *aRvOut = NS_ERROR_DOM_WEBEXT_CONTENT_SCRIPT_URI;
      return false;
    }
  } else {
    // If this document is sandboxed without 'allow-scripts', abort.
    if (GetScriptLoader()->GetDocument()->HasScriptsBlockedBySandbox()) {
      *aRvOut = NS_ERROR_CONTENT_BLOCKED;
      return false;
    }
  }

  if (LOG_ENABLED()) {
    nsAutoCString url;
    aRequest->URI()->GetAsciiSpec(url);
    LOG(("ScriptLoadRequest (%p): Start Module Load (url = %s)", aRequest,
         url.get()));
  }

  return true;
}

void ModuleLoader::DisallowImportMapsForModuleFetch(
    ModuleLoadRequest* aRequest) {
  if (!aRequest->GetScriptLoadContext()->IsPreload() &&
      !StaticPrefs::dom_multiple_import_maps_enabled()) {
    LOG(("ScriptLoadRequest (%p): Disallow further import maps.", aRequest));
    DisallowImportMaps();
  }
}

// Skip module CORS checks for resource: principals loading trusted schemes.
// Other URI security checks still apply.
static bool IsResourceDocumentLoadingTrustedURI(ModuleLoadRequest* aRequest) {
  nsIPrincipal* triggeringPrincipal = aRequest->TriggeringPrincipal();
  if (!triggeringPrincipal->GetIsContentPrincipal() ||
      !triggeringPrincipal->SchemeIs("resource")) {
    return false;
  }

  return nsContentSecurityUtils::IsTrustedScheme(aRequest->URI());
}

nsresult ModuleLoader::StartFetch(ModuleLoadRequest* aRequest) {
  if (aRequest->IsRetrievedFromMemoryCache()) {
    DisallowImportMapsForModuleFetch(aRequest);
    GetScriptLoader()->EmulateNetworkEvents(aRequest);
    SetModuleFetchStarted(aRequest);
    aRequest->OnFetchComplete(NS_OK);
    return NS_OK;
  }

  // Module scripts normally require CORS. Disable it for non-linkable about:
  // pages loading chrome: URLs and resource: principals loading trusted
  // schemes.
  bool skipCORSChecks = ScriptLoader::IsAboutPageLoadingChromeURI(
                            aRequest, GetScriptLoader()->GetDocument()) ||
                        IsResourceDocumentLoadingTrustedURI(aRequest);

  nsContentSecurityManager::CORSSecurityMapping corsMapping =
      skipCORSChecks
          ? nsContentSecurityManager::CORSSecurityMapping::DISABLE_CORS_CHECKS
          : nsContentSecurityManager::CORSSecurityMapping::REQUIRE_CORS_CHECKS;

  nsSecurityFlags securityFlags =
      nsContentSecurityManager::ComputeSecurityFlags(aRequest->CORSMode(),
                                                     corsMapping);

  securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

  // Delegate Shared Behavior to base ScriptLoader
  nsresult rv = GetScriptLoader()->StartLoadInternal(aRequest, securityFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  DisallowImportMapsForModuleFetch(aRequest);

  LOG(("ScriptLoadRequest (%p): Start fetching module", aRequest));

  return NS_OK;
}

void ModuleLoader::AsyncExecuteInlineModule(ModuleLoadRequest* aRequest) {
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
      mozilla::NewRunnableMethod<RefPtr<ModuleLoadRequest>>(
          "ModuleLoader::ExecuteInlineModule", this,
          &ModuleLoader::ExecuteInlineModule, aRequest)));
}

void ModuleLoader::ExecuteInlineModule(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsFinished());
  MOZ_ASSERT(aRequest->IsTopLevel());
  MOZ_ASSERT(aRequest->GetScriptLoadContext()->mIsInline);

  if (aRequest->GetScriptLoadContext()->GetParserCreated() == NOT_FROM_PARSER) {
    GetScriptLoader()->RunScriptWhenSafe(aRequest);
  } else {
    const RefPtr<ScriptLoader> scriptLoader = GetScriptLoader();
    scriptLoader->MaybeMoveToLoadedList(aRequest);
    scriptLoader->ProcessPendingRequests();
  }

  aRequest->GetScriptLoadContext()->MaybeUnblockOnload();
}

void ModuleLoader::OnModuleLoadComplete(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsFinished());

  if (aRequest->IsTopLevel() || aRequest->IsDynamicImport()) {
    if (aRequest->GetScriptLoadContext()->mIsInline &&
        aRequest->GetScriptLoadContext()->GetParserCreated() ==
            NOT_FROM_PARSER) {
      // https://html.spec.whatwg.org/#prepare-the-script-element
      // Step 32.2.
      //    type: "module":
      //    3.1. Queue an element task on the networking task source given
      //         el to perform the following steps:
      //        1. Mark as ready el given result.
      //
      // Step 33. If ... el's type is "module":
      //    ...
      //    3. Otherwise, if el is not parser-inserted:
      //      3. Set el's steps to run when the result is ready to the
      //         following:
      //        ...
      //        2.1. Execute the script element scripts[0].
      AsyncExecuteInlineModule(aRequest);
      return;
    } else if (aRequest->GetScriptLoadContext()->mIsInline &&
               aRequest->GetScriptLoadContext()->GetParserCreated() !=
                   NOT_FROM_PARSER &&
               !nsContentUtils::IsSafeToRunScript()) {
      // Avoid giving inline async module scripts that don't have
      // external dependencies a guaranteed execution time relative
      // to the HTML parse. That is, deliberately avoid guaranteeing
      // that the script would always observe a DOM shape where the
      // parser has not added further elements to the DOM.
      // (If `nsContentUtils::IsSafeToRunScript()` returns `true`,
      // we come here synchronously from the parser. If it returns
      // `false` we come here from an external dependency completing
      // its fetch, in which case we already are at an unspecific
      // point relative to the parse.)
      AsyncExecuteInlineModule(aRequest);
      return;
    } else {
      GetScriptLoader()->MaybeMoveToLoadedList(aRequest);
      GetScriptLoader()->ProcessPendingRequestsAsync();
    }
  }

  aRequest->GetScriptLoadContext()->MaybeUnblockOnload();
}

nsresult ModuleLoader::CompileFetchedModule(
    JSContext* aCx, JS::Handle<JSObject*> aGlobal, JS::CompileOptions& aOptions,
    ModuleLoadRequest* aRequest, JS::MutableHandle<JSObject*> aModuleOut) {
  switch (aRequest->mModuleType) {
    case JS::ModuleType::Unknown:
      MOZ_CRASH("Unexpected module type");
    case JS::ModuleType::JavaScriptOrWasm:
      return CompileJavaScriptOrWasmModule(aCx, aGlobal, aOptions, aRequest,
                                           aModuleOut);
    case JS::ModuleType::JSON:
      return CompileJsonModule(aCx, aOptions, aRequest, aModuleOut);
    case JS::ModuleType::CSS:
      return CompileCssModule(aCx, aOptions, aRequest, aModuleOut);
    case JS::ModuleType::Text:
      return CreateTextModule(aCx, aOptions, aRequest, aModuleOut);
    case JS::ModuleType::Bytes:
      MOZ_CRASH("Unexpected module type");
  }

  MOZ_CRASH("Unhandled module type");
}

// https://html.spec.whatwg.org/#creating-a-javascript-module-script
// Step 1: If scripting is disabled, set source to empty string
nsresult ModuleLoader::CompileEmptyJavaScriptModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.init(aCx, u"", 0, JS::SourceOwnership::Borrowed)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  RefPtr<JS::Stencil> stencil =
      JS::CompileModuleScriptToStencil(aCx, aOptions, srcBuf);
  if (!stencil) {
    return NS_ERROR_FAILURE;
  }
  aRequest->SetStencil(stencil);
  JS::InstantiateOptions instantiateOptions(aOptions);
  aModuleOut.set(
      JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
  return aModuleOut ? NS_OK : NS_ERROR_FAILURE;
}

#ifdef NIGHTLY_BUILD
nsresult ModuleLoader::CompileWasmModuleBytes(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    WasmBytesBuffer& aBytes, JS::MutableHandle<JSObject*> aModuleOut) {
  JSObject* wasmModule = JS::CompileWasmModule(aCx, aOptions, aBytes);
  if (!wasmModule) {
    return NS_ERROR_FAILURE;
  }

  aModuleOut.set(wasmModule);
  return NS_OK;
}

// https://html.spec.whatwg.org/#creating-a-webassembly-module-script
nsresult ModuleLoader::CompileEmptyWasmModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  TRACE_FOR_TEST(aRequest, "compile:wasm empty");

  // Step 1: If scripting is disabled, set bodyBytes to the byte sequence
  // 0x00 0x61 0x73 0x6D 0x01 0x00 0x00 0x00
  static constexpr uint8_t kEmptyWasmModule[] = {0x00, 0x61, 0x73, 0x6D,
                                                 0x01, 0x00, 0x00, 0x00};

  WasmBytesBuffer bytes;
  if (!bytes.append(kEmptyWasmModule, sizeof(kEmptyWasmModule))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return CompileWasmModuleBytes(aCx, aOptions, aRequest, bytes, aModuleOut);
}
#endif

nsresult ModuleLoader::CompileJavaScriptOrWasmModule(
    JSContext* aCx, JS::Handle<JSObject*> aGlobal, JS::CompileOptions& aOptions,
    ModuleLoadRequest* aRequest, JS::MutableHandle<JSObject*> aModuleOut) {
  GetScriptLoader()->CalculateCacheFlag(aRequest);

  if (!nsJSUtils::IsScriptable(aGlobal)) {
    aRequest->GetScriptLoadContext()->MaybeCancelOffThreadScript();

#ifdef NIGHTLY_BUILD
    if (aRequest->HasWasmMimeTypeEssence()) {
      MOZ_ASSERT(aRequest->IsWasmBytes());
      return CompileEmptyWasmModule(aCx, aOptions, aRequest, aModuleOut);
    }
#endif

    return CompileEmptyJavaScriptModule(aCx, aOptions, aRequest, aModuleOut);
  }

#ifdef NIGHTLY_BUILD
  if (aRequest->HasWasmMimeTypeEssence()) {
    MOZ_ASSERT(aRequest->IsWasmBytes());
    // The request's bytes were moved into the WasmCompileTask, so the result
    // has to be taken from the task rather than recompiled.
    if (aRequest->GetScriptLoadContext()->mWasCompiledOMT) {
      if (!aRequest->GetScriptLoadContext()->StealOffThreadWasmResult(
              aCx, aModuleOut)) {
        return NS_ERROR_FAILURE;
      }
      return NS_OK;
    }

    return CompileWasmModuleBytes(aCx, aOptions, aRequest,
                                  aRequest->WasmBytes(), aModuleOut);
  }
#endif
  MOZ_ASSERT(!aRequest->IsWasmBytes());

  if (aRequest->IsRetrievedFromMemoryCache()) {
    JS::InstantiateOptions instantiateOptions(aOptions);
    RefPtr<JS::Stencil> stencil = aRequest->GetStencil();
    aModuleOut.set(
        JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
    if (!aModuleOut) {
      return NS_ERROR_FAILURE;
    }

    if (!GetScriptLoader()->StartCollectingDelazifications(aCx, aModuleOut,
                                                           stencil)) {
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  if (aRequest->GetScriptLoadContext()->mWasCompiledOMT) {
    JS::InstantiationStorage storage;
    RefPtr<JS::Stencil> stencil =
        aRequest->GetScriptLoadContext()->StealOffThreadResult(aCx, &storage);
    if (!stencil) {
      return NS_ERROR_FAILURE;
    }

    aRequest->SetStencil(stencil);

    JS::InstantiateOptions instantiateOptions(aOptions);
    aModuleOut.set(JS::InstantiateModuleStencil(aCx, instantiateOptions,
                                                stencil, &storage));
    if (!aModuleOut) {
      return NS_ERROR_FAILURE;
    }

    if (aRequest->PassedConditionForEitherCache()) {
      if (!GetScriptLoader()->StartCollectingDelazifications(aCx, aModuleOut,
                                                             stencil)) {
        return NS_ERROR_FAILURE;
      }
    }

    GetScriptLoader()->TryCacheRequest(aRequest);

    return NS_OK;
  }

  RefPtr<JS::Stencil> stencil;
  if (aRequest->IsFetchedAsTextSource()) {
    MaybeSourceText maybeSource;
    nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                            aRequest->mLoadContext.get());
    NS_ENSURE_SUCCESS(rv, rv);

    auto compile = [&](auto& source) {
      return JS::CompileModuleScriptToStencil(aCx, aOptions, source);
    };
    stencil = maybeSource.mapNonEmpty(compile);
  } else {
    MOZ_ASSERT(aRequest->IsRetrievedAsSerializedStencil());
    JS::DecodeOptions decodeOptions(aOptions);
    if (!GetScriptLoader()->UsesMemoryCache()) {
      decodeOptions.borrowBuffer = true;
    }

    JS::TranscodeRange range = aRequest->SerializedStencil();
    JS::TranscodeResult tr =
        JS::DecodeStencil(aCx, decodeOptions, range, getter_AddRefs(stencil));
    if (tr != JS::TranscodeResult::Ok) {
      return NS_ERROR_DOM_JS_DECODING_ERROR;
    }
  }

  if (!stencil) {
    return NS_ERROR_FAILURE;
  }

  aRequest->SetStencil(stencil);

  JS::InstantiateOptions instantiateOptions(aOptions);
  aModuleOut.set(
      JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
  if (!aModuleOut) {
    return NS_ERROR_FAILURE;
  }

  if (aRequest->PassedConditionForEitherCache()) {
    if (!GetScriptLoader()->StartCollectingDelazifications(aCx, aModuleOut,
                                                           stencil)) {
      return NS_ERROR_FAILURE;
    }
  }

  GetScriptLoader()->TryCacheRequest(aRequest);

  return NS_OK;
}

nsresult ModuleLoader::CompileJsonModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mWasCompiledOMT);

  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  ModuleLoader::MaybeSourceText maybeSource;
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

  aModuleOut.set(jsonModule);
  return NS_OK;
}

nsresult CreateCssModule(JSContext* aCx, nsIGlobalObject* aGlobal,
                         const nsACString& aSource, nsIURI* aBaseURI,
                         JS::MutableHandle<JSObject*> aModuleOut) {
  // https://html.spec.whatwg.org/#creating-a-css-module-script
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
  if (!window) {
    JS_ReportErrorASCII(aCx,
                        "CSS module scripts not supported when there is no "
                        "window");
    return NS_ERROR_FAILURE;
  }

  Document* constructorDocument = window->GetExtantDoc();
  if (!constructorDocument) {
    JS_ReportErrorASCII(aCx,
                        "CSS module scripts not supported when there is no "
                        "document");
    return NS_ERROR_FAILURE;
  }

  // 5. Let sheet be the result of running the steps to create a constructed
  // CSSStyleSheet with an empty dictionary as the argument.
  // Note that according to the specification, the baseURL should be the
  // baseURL of the document, but that doesn't seem correct (see
  // https://github.com/whatwg/html/issues/11629).
  ErrorResult error;
  CSSStyleSheetInit options;
  RefPtr<StyleSheet> sheet = StyleSheet::CreateConstructedSheet(
      *constructorDocument, aBaseURI, options, error);
  if (error.Failed()) {
    MOZ_ALWAYS_TRUE(error.MaybeSetPendingException(aCx));
    return NS_ERROR_FAILURE;
  }

  // 6. Run the steps to synchronously replace the rules of a CSSStyleSheet on
  // sheet given source. Ideally we wouldn't run this on the main thread for
  // large scripts, see https://bugzilla.mozilla.org/show_bug.cgi?id=1987143.
  sheet->ReplaceSync(aSource, error);
  if (error.Failed()) {
    MOZ_ALWAYS_TRUE(error.MaybeSetPendingException(aCx));
    return NS_ERROR_FAILURE;
  }

  JS::Rooted<JS::Value> val(aCx, JS::NullValue());
  if (!GetOrCreateDOMReflector(aCx, sheet, &val) || !val.isObject()) {
    if (!JS_IsExceptionPending(aCx)) {
      JS_ReportErrorASCII(aCx, "Internal error");
    }
    return NS_ERROR_FAILURE;
  }

  // Steps 1 - 4 (re-ordered), 7, 8
  JSObject* cssModule = JS::CreateDefaultExportSyntheticModule(aCx, val);
  if (!cssModule) {
    return NS_ERROR_FAILURE;
  }

  aModuleOut.set(cssModule);
  return NS_OK;
}

nsresult ModuleLoader::CompileCssModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mWasCompiledOMT);
  MOZ_ASSERT(mozilla::StaticPrefs::layout_css_module_scripts_enabled());

  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  ModuleLoader::MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(aCx, &maybeSource,
                                          aRequest->mLoadContext.get());
  NS_ENSURE_SUCCESS(rv, rv);

  JS::Rooted<JSObject*> cssModule(aCx, nullptr);
  ErrorResult error;
  auto compile = [&](auto& source) {
    using T = decltype(source);
    static_assert(std::is_same_v<T, JS::SourceText<char16_t>&> ||
                  std::is_same_v<T, JS::SourceText<Utf8Unit>&>);

    nsCString text;
    if constexpr (std::is_same_v<T, JS::SourceText<mozilla::Utf8Unit>&>) {
      text.Assign(source.get(), source.length());
    } else {
      CopyUTF16toUTF8(nsDependentSubstring(source.get(), source.length()),
                      text);
    }

    nsresult rv2 = CreateCssModule(aCx, aRequest->GetGlobalObject(), text,
                                   aRequest->BaseURL(), &cssModule);
    if (NS_FAILED(rv2) && !JS_IsExceptionPending(aCx)) {
      error.ThrowUnknownError("Internal error");
    }
  };

  maybeSource.mapNonEmpty(compile);
  if (!cssModule) {
    if (error.Failed()) {
      MOZ_ALWAYS_TRUE(error.MaybeSetPendingException(aCx));
    }
    return NS_ERROR_FAILURE;
  }

  aModuleOut.set(cssModule);
  return NS_OK;
}

nsresult ModuleLoader::CreateTextModule(
    JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  MOZ_ASSERT(!aRequest->GetScriptLoadContext()->mWasCompiledOMT);

  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  ModuleLoader::MaybeSourceText maybeSource;
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

  aModuleOut.set(textModule);
  return NS_OK;
}

already_AddRefed<ModuleLoadRequest> ModuleLoader::CreateTopLevel(
    nsIURI* aURI, nsIScriptElement* aElement, ReferrerPolicy aReferrerPolicy,
    ScriptFetchOptions* aFetchOptions, const SRIMetadata& aIntegrity,
    nsIURI* aReferrer, ScriptLoadContext* aContext,
    ScriptLoadRequestType aRequestType) {
  RefPtr<ModuleLoadRequest> request = new ModuleLoadRequest(
      JS::ModuleType::JavaScript, aIntegrity, aReferrer, aContext,
      ModuleLoadRequest::Kind::TopLevel, this, nullptr);

  GetScriptLoader()->TryUseCache(aReferrerPolicy, aFetchOptions, aURI, request,
                                 aElement, aFetchOptions->mNonce, aRequestType);

  return request.forget();
}

already_AddRefed<ModuleLoadRequest> ModuleLoader::CreateRequest(
    JSContext* aCx, nsIURI* aURI, JS::Handle<JSObject*> aModuleRequest,
    JS::Handle<JS::Value> aHostDefined, JS::Handle<JS::Value> aPayload,
    bool aIsDynamicImport, ScriptFetchOptions* aOptions,
    ReferrerPolicy aReferrerPolicy, nsIURI* aBaseURL,
    const SRIMetadata& aSriMetadata) {
  RefPtr<ScriptLoadContext> context = new ScriptLoadContext();
  context->mIsInline = false;
  ModuleLoadRequest::Kind kind;
  ModuleLoadRequest* root = nullptr;
  if (aIsDynamicImport) {
    context->mScriptMode = ScriptLoadContext::ScriptMode::eAsync;
    kind = ModuleLoadRequest::Kind::DynamicImport;
  } else {
    MOZ_ASSERT(!aHostDefined.isUndefined());
    root = static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
    MOZ_ASSERT(root);
    LoadContextBase* loadContext = root->mLoadContext;
    context->mScriptMode = loadContext->AsWindowContext()->mScriptMode;
    if (loadContext->AsWindowContext()->mIsPreload) {
      context->mIsPreload = true;
    }
    kind = ModuleLoadRequest::Kind::StaticImport;
  }

  JS::ModuleType moduleType = GetModuleRequestType(aCx, aModuleRequest);
  RefPtr<ModuleLoadRequest> request = new ModuleLoadRequest(
      moduleType, aSriMetadata, aBaseURL, context, kind, this, root);

  GetScriptLoader()->TryUseCache(aReferrerPolicy, aOptions, aURI, request);

  return request.forget();
}

already_AddRefed<ScriptFetchOptions>
ModuleLoader::CreateDefaultScriptFetchOptions() {
  RefPtr<ScriptFetchOptions> options = ScriptFetchOptions::CreateDefault();
  nsCOMPtr<nsIPrincipal> principal = GetGlobalObject()->PrincipalOrNull();
  options->SetTriggeringPrincipal(principal);
  return options.forget();
}

nsIURI* ModuleLoader::GetClientReferrerURI() {
  Document* document = GetScriptLoader()->GetDocument();
#ifdef DEBUG
  nsCOMPtr<nsIPrincipal> principal = GetGlobalObject()->PrincipalOrNull();
#endif  // DEBUG
  MOZ_ASSERT_IF(GetKind() == WebExtension,
                BasePrincipal::Cast(principal)->ContentScriptAddonPolicy());
  MOZ_ASSERT_IF(GetKind() == Normal, principal == document->NodePrincipal());

  return document->GetDocBaseURI();
}

ModuleLoader::~ModuleLoader() {
  LOG(("ModuleLoader::~ModuleLoader %p", this));
  mLoader = nullptr;
}

#undef LOG
#undef LOG_ENABLED

}  // namespace mozilla::dom
