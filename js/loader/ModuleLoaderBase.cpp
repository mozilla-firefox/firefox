/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"  // nsAutoMicroTask
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ScriptLoadContext.h"
#include "mozilla/dom/ScriptSettings.h"  // AutoJSAPI
#include "mozilla/dom/ScriptTrace.h"
#include "mozilla/mozalloc_oom.h"  // mozalloc_handle_oom
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"  // mozilla::StaticRefPtr
#include "mozilla/StaticPrefs_dom.h"

#include "GeckoProfiler.h"
#include "LoadedScript.h"
#include "ModuleLoadRequest.h"
#include "nsContentUtils.h"
#include "nsICacheInfoChannel.h"  // nsICacheInfoChannel
#include "nsNetUtil.h"            // NS_NewURI
#include "ScriptLoaderInterface.h"
#include "ScriptLoadRequest.h"
#include "xpcpublic.h"

#include "js/Array.h"         // JS::GetArrayLength
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/CompilationAndEvaluation.h"
#include "js/ContextOptions.h"  // JS::ContextOptionsRef
#include "js/ErrorReport.h"     // JSErrorBase
#include "js/Exception.h"  // JS_IsExceptionPending, JS_IsThrowingOutOfMemory
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Modules.h"  // JS::FinishLoadingImportedModule, JS::{G,S}etModuleResolveHook, JS::Get{ModulePrivate,ModuleScript,RequestedModule{s,Specifier,SourcePos}}, JS::SetModule{Load,Metadata}Hook
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_GetElement
#include "js/SourceText.h"

using mozilla::AutoSlowOperation;
using mozilla::CycleCollectedJSContext;
using mozilla::Err;
using mozilla::MicroTaskRunnable;
using mozilla::Preferences;
using mozilla::UniquePtr;
using mozilla::dom::AutoJSAPI;
using mozilla::dom::ReferrerPolicy;

namespace JS::loader {

mozilla::LazyLogModule ModuleLoaderBase::gCspPRLog("CSP");
mozilla::LazyLogModule ModuleLoaderBase::gModuleLoaderBaseLog(
    "ModuleLoaderBase");

#undef LOG
#define LOG(args)                                                           \
  MOZ_LOG(ModuleLoaderBase::gModuleLoaderBaseLog, mozilla::LogLevel::Debug, \
          args)

#define LOG_ENABLED() \
  MOZ_LOG_TEST(ModuleLoaderBase::gModuleLoaderBaseLog, mozilla::LogLevel::Debug)

//////////////////////////////////////////////////////////////
// ModuleLoaderBase::LoadingRequest
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ModuleLoaderBase::LoadingRequest)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(ModuleLoaderBase::LoadingRequest, mRequest, mWaiting)

NS_IMPL_CYCLE_COLLECTING_ADDREF(ModuleLoaderBase::LoadingRequest)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ModuleLoaderBase::LoadingRequest)

//////////////////////////////////////////////////////////////
// ModuleLoaderBase
//////////////////////////////////////////////////////////////

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ModuleLoaderBase)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(ModuleLoaderBase, mFetchingModules, mFetchedModules,
                         mDynamicImportRequests, mGlobalObject, mOverriddenBy,
                         mLoader)

NS_IMPL_CYCLE_COLLECTING_ADDREF(ModuleLoaderBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ModuleLoaderBase)

// static
void ModuleLoaderBase::EnsureModuleHooksInitialized() {
  AutoJSAPI jsapi;
  jsapi.Init();
  JSRuntime* rt = JS_GetRuntime(jsapi.cx());
  if (GetModuleLoadHook(rt)) {
    return;
  }

  SetModuleLoadHook(rt, HostLoadImportedModule);
  SetModuleMetadataHook(rt, HostPopulateImportMeta);
  SetScriptPrivateReferenceHooks(rt, HostAddRefScriptFetchInfo,
                                 HostReleaseScriptFetchInfo);
}

static bool CreateBadModuleTypeError(JSContext* aCx,
                                     ScriptFetchInfo* aFetchInfo, nsIURI* aURI,
                                     MutableHandle<Value> aErrorOut) {
  Rooted<JSString*> filename(aCx);
  if (aFetchInfo) {
    nsAutoCString url;
    aFetchInfo->BaseURL()->GetAsciiSpec(url);
    filename = JS_NewStringCopyZ(aCx, url.get());
  } else {
    filename = JS_NewStringCopyZ(aCx, "(unknown)");
  }

  if (!filename) {
    return false;
  }

  MOZ_ASSERT(aURI);
  nsAutoCString url;
  aURI->GetSpec(url);

  Rooted<JSString*> uri(aCx, JS_NewStringCopyZ(aCx, url.get()));
  if (!uri) {
    return false;
  }

  Rooted<JSString*> msg(aCx, JS_NewStringCopyZ(aCx, ": invalid module type"));
  if (!msg) {
    return false;
  }

  Rooted<JSString*> errMsg(aCx, JS_ConcatStrings(aCx, uri, msg));
  if (!errMsg) {
    return false;
  }

  return CreateError(aCx, JSEXN_TYPEERR, nullptr, filename, 0,
                     ColumnNumberOneOrigin(), nullptr, errMsg,
                     NothingHandleValue, aErrorOut);
}

// https://html.spec.whatwg.org/#hostloadimportedmodule
// static
bool ModuleLoaderBase::HostLoadImportedModule(
    JSContext* aCx, Handle<Value> aReferrer, Handle<JSObject*> aModuleRequest,
    Handle<Value> aHostDefined, Handle<Value> aPayload, uint32_t aLineNumber,
    JS::ColumnNumberOneOrigin aColumnNumber) {
  Rooted<JSObject*> object(aCx);
  if (aPayload.isObject()) {
    object = &aPayload.toObject();
  }
  bool isDynamicImport = object && IsPromiseObject(object);

  Rooted<JSString*> specifierString(
      aCx, GetModuleRequestSpecifier(aCx, aModuleRequest));
  if (!specifierString) {
    JS_ReportOutOfMemory(aCx);
    return false;
  }

  // Let url be the result of resolving a module specifier given referencing
  // module script and specifier.
  nsAutoJSString string;
  if (!string.init(aCx, specifierString)) {
    JS_ReportOutOfMemory(aCx);
    return false;
  }

  RefPtr<ModuleLoaderBase> loader = GetCurrentModuleLoader(aCx);
  if (!loader) {
    return false;
  }

  if (isDynamicImport && !loader->IsDynamicImportSupported()) {
    JS_ReportErrorNumberASCII(aCx, js::GetErrorMessage, nullptr,
                              JSMSG_DYNAMIC_IMPORT_NOT_SUPPORTED);
    return false;
  }

  RefPtr<ScriptFetchInfo> fetchInfo(GetScriptFetchInfoOrNull(aReferrer));

  // Step 8. Let url be the result of resolving a module specifier given
  //   referencingScript and moduleRequest.[[Specifier]], catching any
  //   exceptions. If they throw an exception, let resolutionError be the
  //   thrown exception.
  auto result = loader->ResolveModuleSpecifier(fetchInfo, string);

  // Step 9. If the previous step threw an exception, then:
  if (result.isErr()) {
    Rooted<Value> error(aCx);
    nsresult rv =
        loader->HandleResolveFailure(aCx, fetchInfo, string, result.unwrapErr(),
                                     aLineNumber, aColumnNumber, &error);
    if (NS_FAILED(rv)) {
      JS_ReportOutOfMemory(aCx);
      return false;
    }

    // Step 2. Perform FinishLoadingImportedModule(referrer, moduleRequest,
    //   payload, ThrowCompletion(resolutionError)).
    FinishLoadingImportedModuleFailed(aCx, aPayload, error);

    // Step 3. Return.
    return true;
  }

  MOZ_ASSERT(result.isOk());
  auto record = result.unwrap();
  nsCOMPtr<nsIURI> uri = record->Result();
  MOZ_ASSERT(uri, "Failed to resolve module specifier");

  if (ImportMap::IsMultipleImportMapsSupported()) {
    // This implements the 'Add module to resolved module set' part defined in
    // 'resolve a module specifier'. This is done here because we need to
    // process the resolved specifiers differently for preloading module
    // scripts.
    //
    // See https://html.spec.whatwg.org/#resolve-a-module-specifier
    loader->AddToResolvedModuleSet(std::move(record), aHostDefined);
  }

  ModuleType moduleType = GetModuleRequestType(aCx, aModuleRequest);
  if (!loader->IsModuleTypeAllowed(moduleType)) {
    LOG(("ModuleLoaderBase::HostLoadImportedModule uri %s, bad module type",
         uri->GetSpecOrDefault().get()));
    Rooted<Value> error(aCx);
    if (!CreateBadModuleTypeError(aCx, fetchInfo, uri, &error)) {
      JS_ReportOutOfMemory(aCx);
      return false;
    }
    JS_SetPendingException(aCx, error);
    return false;
  }

  RefPtr<ScriptFetchOptions> options = nullptr;
  ReferrerPolicy referrerPolicy;
  nsIURI* fetchReferrer = nullptr;
  if (fetchInfo) {
    options = fetchInfo->FetchOptions();
    referrerPolicy = fetchInfo->ReferrerPolicy();
    fetchReferrer = fetchInfo->BaseURL();
  } else {
    options = loader->CreateDefaultScriptFetchOptions();
    referrerPolicy = ReferrerPolicy::_empty;
    fetchReferrer = loader->GetClientReferrerURI();
  }

  mozilla::dom::SRIMetadata sriMetadata;
  loader->GetImportMapSRI(
      uri, fetchReferrer,
      loader->GetScriptLoaderInterface()->GetConsoleReportCollector(),
      &sriMetadata);

  RefPtr<ModuleLoadRequest> request = loader->CreateRequest(
      aCx, uri, aModuleRequest, aHostDefined, aPayload, isDynamicImport,
      options, referrerPolicy, fetchReferrer, sriMetadata);
  if (!request) {
    MOZ_ASSERT(isDynamicImport);
    nsAutoCString url;
    uri->GetSpec(url);
    JS_ReportErrorNumberASCII(aCx, js::GetErrorMessage, nullptr,
                              JSMSG_DYNAMIC_IMPORT_FAILED, url.get());
    return false;
  }

  LOG(
      ("ModuleLoaderBase::HostLoadImportedModule loader (%p) uri %s referrer "
       "%s request (%p)",
       loader.get(), uri->GetSpecOrDefault().get(),
       fetchReferrer ? fetchReferrer->GetSpecOrDefault().get() : "(null)",
       request.get()));

  request->SetImport(aReferrer, aModuleRequest, aPayload);

  if (isDynamicImport) {
    loader->AppendDynamicImport(request);
  }

  nsresult rv = loader->StartModuleLoad(request);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_ASSERT(!request->mModuleScript);
    loader->GetScriptLoaderInterface()->ReportErrorToConsole(request, rv);
    if (isDynamicImport) {
      loader->RemoveDynamicImport(request);

      nsAutoCString url;
      uri->GetSpec(url);
      JS_ReportErrorNumberASCII(aCx, js::GetErrorMessage, nullptr,
                                JSMSG_DYNAMIC_IMPORT_FAILED, url.get());
    } else {
      loader->OnFetchFailed(request);
      return true;
    }

    return false;
  }

  if (isDynamicImport) {
    loader->OnDynamicImportStarted(request);
  }

  return true;
}

// static
bool ModuleLoaderBase::FinishLoadingImportedModule(
    JSContext* aCx, ModuleLoadRequest* aRequest) {
  // The request should been removed from mDynamicImportRequests.
  MOZ_ASSERT_IF(aRequest->IsDynamicImport(),
                !aRequest->mLoader->HasDynamicImport(aRequest));

  // This is the normal completion path, so the imports must have been loaded.
  // Otherwise the promise of a dynamic import has already been rejected in
  // OnLoadRequestedModulesRejected.
  MOZ_ASSERT(!aRequest->IsErroredLoadingImports());

  Rooted<JSObject*> module(aCx);
  {
    ModuleScript* moduleScript = aRequest->mModuleScript;
    MOZ_ASSERT(moduleScript);
    MOZ_ASSERT(moduleScript->ModuleRecord());
    module.set(moduleScript->ModuleRecord());
  }
  MOZ_ASSERT(module);

  Rooted<Value> referrer(aCx, aRequest->mReferrerValue);
  Rooted<JSObject*> moduleReqObj(aCx, aRequest->mModuleRequestObj);
  Rooted<Value> payload(aCx, aRequest->mPayload);

  LOG(("ScriptLoadRequest (%p): FinishLoadingImportedModule module (%p)",
       aRequest, module.get()));
  bool usePromise = aRequest->HasScriptLoadContext();
  bool ok = JS::FinishLoadingImportedModule(aCx, referrer, moduleReqObj,
                                            payload, module, usePromise);
  // FinishLoadingImportedModule returns false when OOM or
  // RejectPromiseWithPendingError returns false, which means there is no
  // exception pending or the exception is uncatchable.
  if (!ok && JS_IsThrowingOutOfMemory(aCx)) {
    mozalloc_handle_oom(0);
  }

  // RejectPromiseWithPendingError has already settled the promise so there is
  // nothing left to do.
  MOZ_ASSERT(!JS_IsExceptionPending(aCx));
  aRequest->ClearImport();

  return ok;
}

// static
bool ModuleLoaderBase::ImportMetaResolve(JSContext* cx, unsigned argc,
                                         Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedValue moduleValue(
      cx, js::GetFunctionNativeReserved(
              &args.callee(),
              static_cast<size_t>(ImportMetaSlots::ModuleRecordSlot)));
  MOZ_ASSERT(!moduleValue.isUndefined());
  RootedObject moduleRecord(cx, &moduleValue.toObject());
  RootedValue modulePrivate(cx, GetModulePrivate(moduleRecord));
  MOZ_ASSERT(!modulePrivate.isUndefined());

  // https://html.spec.whatwg.org/#hostgetimportmetaproperties
  // Step 4.1. Set specifier to ? ToString(specifier).
  //
  // https://tc39.es/ecma262/#sec-tostring
  RootedValue v(cx, args.get(ImportMetaResolveSpecifierArg));
  RootedString specifier(cx, ToString(cx, v));
  if (!specifier) {
    return false;
  }

  // Step 4.2, 4.3 are implemented in ImportMetaResolveImpl.
  RootedString url(cx, ImportMetaResolveImpl(cx, modulePrivate, specifier));
  if (!url) {
    return false;
  }

  // Step 4.4. Return the serialization of url.
  args.rval().setString(url);
  return true;
}

// static
JSString* ModuleLoaderBase::ImportMetaResolveImpl(
    JSContext* aCx, Handle<Value> aReferencingPrivate,
    Handle<JSString*> aSpecifier) {
  RootedString urlString(aCx);

  // The destructor of RefPtr local variables can GC, and they should be
  // performed before returning the raw JSString* pointer.
  {
    RefPtr<ScriptFetchInfo> fetchInfo =
        static_cast<ScriptFetchInfo*>(aReferencingPrivate.toPrivate());
    MOZ_ASSERT(fetchInfo->IsForModuleScript());

    RefPtr<ModuleLoaderBase> loader = GetCurrentModuleLoader(aCx);
    if (!loader) {
      return nullptr;
    }

    nsAutoJSString specifier;
    if (!specifier.init(aCx, aSpecifier)) {
      return nullptr;
    }

    auto result = loader->ResolveModuleSpecifier(fetchInfo, specifier);
    if (result.isErr()) {
      Rooted<Value> error(aCx);
      nsresult rv = loader->HandleResolveFailure(
          aCx, fetchInfo, specifier, result.unwrapErr(), 0,
          ColumnNumberOneOrigin(), &error);
      if (NS_FAILED(rv)) {
        JS_ReportOutOfMemory(aCx);
        return nullptr;
      }

      JS_SetPendingException(aCx, error);

      return nullptr;
    }

    MOZ_ASSERT(result.isOk());
    auto record = result.unwrap();

    nsCOMPtr<nsIURI> uri = record->Result();
    if (ImportMap::IsMultipleImportMapsSupported()) {
      // This implements the 'Add module to resolved module set' part defined in
      // 'resolve a module specifier'.
      loader->AddToResolvedModuleSet(std::move(record));
    }

    nsAutoCString url;
    MOZ_ALWAYS_SUCCEEDS(uri->GetAsciiSpec(url));

    urlString.set(JS_NewStringCopyZ(aCx, url.get()));
  }

  return urlString;
}

// static
bool ModuleLoaderBase::HostPopulateImportMeta(JSContext* aCx,
                                              Handle<JSObject*> aModuleRecord,
                                              Handle<JSObject*> aMetaObject) {
  RefPtr<ScriptFetchInfo> fetchInfo = static_cast<ScriptFetchInfo*>(
      JS::GetModulePrivate(aModuleRecord).toPrivate());
  MOZ_ASSERT(fetchInfo->IsForModuleScript());

  nsAutoCString url;
  MOZ_DIAGNOSTIC_ASSERT(fetchInfo->BaseURL());
  MOZ_ALWAYS_SUCCEEDS(fetchInfo->BaseURL()->GetAsciiSpec(url));

  Rooted<JSString*> urlString(aCx, JS_NewStringCopyZ(aCx, url.get()));
  if (!urlString) {
    JS_ReportOutOfMemory(aCx);
    return false;
  }

  // https://html.spec.whatwg.org/#import-meta-url
  if (!JS_DefineProperty(aCx, aMetaObject, "url", urlString,
                         JSPROP_ENUMERATE)) {
    return false;
  }

  // https://html.spec.whatwg.org/#import-meta-resolve
  // Define 'resolve' function on the import.meta object.
  JSFunction* resolveFunc = js::DefineFunctionWithReserved(
      aCx, aMetaObject, "resolve", ImportMetaResolve, ImportMetaResolveNumArgs,
      JSPROP_ENUMERATE);
  if (!resolveFunc) {
    return false;
  }

  // Store the 'active script' of the meta object into the function slot.
  // See https://html.spec.whatwg.org/#active-script
  //
  // Note: Hold a reference to the module record which in turn keeps the
  // ModuleScript alive when import.resolve is called.
  RootedObject resolveFuncObj(aCx, JS_GetFunctionObject(resolveFunc));
  js::SetFunctionNativeReserved(
      resolveFuncObj, static_cast<size_t>(ImportMetaSlots::ModuleRecordSlot),
      JS::ObjectValue(*aModuleRecord));

  return true;
}

AutoOverrideModuleLoader::AutoOverrideModuleLoader(ModuleLoaderBase* aTarget,
                                                   ModuleLoaderBase* aLoader)
    : mTarget(aTarget) {
  mTarget->SetOverride(aLoader);
}

AutoOverrideModuleLoader::~AutoOverrideModuleLoader() {
  mTarget->ResetOverride();
}

void ModuleLoaderBase::SetOverride(ModuleLoaderBase* aLoader) {
  MOZ_ASSERT(!mOverriddenBy);
  MOZ_ASSERT(!aLoader->mOverriddenBy);
  MOZ_ASSERT(mGlobalObject == aLoader->mGlobalObject);
  mOverriddenBy = aLoader;
}

bool ModuleLoaderBase::IsOverridden() { return !!mOverriddenBy; }

bool ModuleLoaderBase::IsOverriddenBy(ModuleLoaderBase* aLoader) {
  return mOverriddenBy == aLoader;
}

void ModuleLoaderBase::ResetOverride() {
  MOZ_ASSERT(mOverriddenBy);
  mOverriddenBy = nullptr;
}

// static
ModuleLoaderBase* ModuleLoaderBase::GetCurrentModuleLoader(JSContext* aCx) {
  auto reportError = mozilla::MakeScopeExit([aCx]() {
    JS_ReportErrorASCII(aCx, "No ScriptLoader found for the current context");
  });

  Rooted<JSObject*> object(aCx, CurrentGlobalOrNull(aCx));
  if (!object) {
    return nullptr;
  }

  nsIGlobalObject* global = xpc::NativeGlobal(object);
  if (!global) {
    return nullptr;
  }

  ModuleLoaderBase* loader = global->GetModuleLoader(aCx);
  if (!loader) {
    return nullptr;
  }

  MOZ_ASSERT(loader->mGlobalObject == global);

  reportError.release();

  if (loader->mOverriddenBy) {
    MOZ_ASSERT(loader->mOverriddenBy->mGlobalObject == global);
    return loader->mOverriddenBy;
  }
  return loader;
}

// static
ScriptFetchInfo* ModuleLoaderBase::GetScriptFetchInfoOrNull(
    Handle<Value> aReferrer) {
  Value value = JS::GetReferrerPrivate(aReferrer);
  if (value.isUndefined()) {
    return nullptr;
  }

  return static_cast<ScriptFetchInfo*>(value.toPrivate());
}

nsresult ModuleLoaderBase::StartModuleLoad(ModuleLoadRequest* aRequest) {
  return StartOrRestartModuleLoad(aRequest, RestartRequest::No);
}

nsresult ModuleLoaderBase::RestartModuleLoad(ModuleLoadRequest* aRequest) {
  return StartOrRestartModuleLoad(aRequest, RestartRequest::Yes);
}

nsresult ModuleLoaderBase::StartOrRestartModuleLoad(ModuleLoadRequest* aRequest,
                                                    RestartRequest aRestart) {
  MOZ_ASSERT(aRequest->mLoader == this);
  MOZ_ASSERT(aRequest->IsFetching());

  MOZ_ASSERT_IF(aRequest->IsRetrievedFromMemoryCache(),
                aRestart == RestartRequest::No);

  if (!aRequest->IsRetrievedFromMemoryCache()) {
    aRequest->SetUnknownDataType();
  }

  if (LOG_ENABLED()) {
    nsAutoCString url;
    aRequest->URI()->GetAsciiSpec(url);
    LOG(("ScriptLoadRequest (%p): Start module load %s", aRequest, url.get()));
  }

  // If we're restarting the request, the module should already be in the
  // "fetching" map.
  MOZ_ASSERT_IF(
      aRestart == RestartRequest::Yes,
      IsModuleFetching(ModuleMapKey(aRequest->URI(), aRequest->mModuleType)));

  // Check with the derived class whether we should load this module.
  nsresult rv = NS_OK;
  if (!CanStartLoad(aRequest, &rv)) {
    return rv;
  }

  // https://html.spec.whatwg.org/#fetch-a-single-module-script
  // Steps 5-6. If the entry already exists (a module script or null, or a list
  // of waiting callbacks), WaitForModuleFetch runs or appends onComplete as
  // appropriate rather than starting a new fetch.
  if (aRestart == RestartRequest::No &&
      ModuleMapContainsURL(
          ModuleMapKey(aRequest->URI(), aRequest->mModuleType))) {
    LOG(("ScriptLoadRequest (%p): Waiting for module fetch", aRequest));
    WaitForModuleFetch(aRequest);
    return NS_OK;
  }

  rv = StartFetch(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aRequest->IsRetrievedFromMemoryCache()) {
    MOZ_ASSERT(
        IsModuleFetched(ModuleMapKey(aRequest->URI(), aRequest->mModuleType)));
    return NS_OK;
  }

  // We successfully started fetching a module so put its URL in the module
  // map and mark it as fetching.
  if (aRestart == RestartRequest::No) {
    SetModuleFetchStarted(aRequest);
  }

  return NS_OK;
}

bool ModuleLoaderBase::ModuleMapContainsURL(const ModuleMapKey& key) const {
  return IsModuleFetching(key) || IsModuleFetched(key);
}

bool ModuleLoaderBase::IsModuleFetching(const ModuleMapKey& key) const {
  return mFetchingModules.Contains(key);
}

bool ModuleLoaderBase::IsModuleFetched(const ModuleMapKey& key) const {
  return mFetchedModules.Contains(key);
}

nsresult ModuleLoaderBase::GetFetchedModuleURLs(nsTArray<nsCString>& aURLs) {
  for (const auto& entry : mFetchedModules) {
    nsAutoCString spec;
    nsresult rv = entry.mUri->GetSpec(spec);
    NS_ENSURE_SUCCESS(rv, rv);

    aURLs.AppendElement(spec);
  }

  return NS_OK;
}

void ModuleLoaderBase::SetModuleFetchStarted(ModuleLoadRequest* aRequest) {
  // Update the module map to indicate that a module is currently being fetched.

  ModuleMapKey moduleMapKey(aRequest->URI(), aRequest->mModuleType);

  MOZ_ASSERT(aRequest->IsFetching());
  MOZ_ASSERT(!ModuleMapContainsURL(moduleMapKey));

  // Step 7. Set moduleMap[(url, moduleType)] to « onComplete ».
  //
  // The list of waiting callbacks is the LoadingRequest's mWaiting.
  RefPtr<LoadingRequest> loadingRequest = new LoadingRequest();
  loadingRequest->mRequest = aRequest;
  mFetchingModules.InsertOrUpdate(moduleMapKey, loadingRequest);
}

already_AddRefed<ModuleLoaderBase::LoadingRequest>
ModuleLoaderBase::SetModuleFetchFinishedAndGetWaitingRequests(
    ModuleLoadRequest* aRequest, nsresult aResult) {
  // Update module map with the result of fetching a single module script.
  //
  // If any requests for the same URL are waiting on this one to complete, they
  // are returned so they can be resumed or failed as appropriate.

  MOZ_ASSERT(aRequest->mLoader == this);

  LOG(
      ("ScriptLoadRequest (%p): Module fetch finished (script == %p, result == "
       "%u)",
       aRequest, aRequest->mModuleScript.get(), unsigned(aResult)));

  ModuleMapKey moduleMapKey(aRequest->URI(), aRequest->mModuleType);

  // The entry may be absent for an inline module, because we already finished
  // fetching it, or because it was canceled and dropped by
  // CancelFetchingModules. There is nothing to complete in those cases.
  auto entry = mFetchingModules.Lookup(moduleMapKey);
  if (!entry) {
    LOG(
        ("ScriptLoadRequest (%p): Key not found in mFetchingModules, "
         "assuming we have an inline module or have finished fetching already",
         aRequest));
    return nullptr;
  }

  // It's possible for a request to be cancelled and removed from the fetching
  // modules map and a new request started for the same URI and added to the
  // map. In this case we don't want the first cancelled request to complete the
  // later request (which will cause it to fail) so we ignore it.
  RefPtr<LoadingRequest> loadingRequest = entry.Data();
  if (loadingRequest->mRequest != aRequest) {
    MOZ_ASSERT(aRequest->IsCanceled());
    LOG(
        ("ScriptLoadRequest (%p): Ignoring completion of cancelled request "
         "that was removed from the map",
         aRequest));
    return nullptr;
  }

  MOZ_ALWAYS_TRUE(mFetchingModules.Remove(moduleMapKey));

  RefPtr<ModuleScript> moduleScript(aRequest->mModuleScript);
  MOZ_ASSERT(NS_FAILED(aResult) == !moduleScript);

  if (moduleScript) {
    mFetchedModules.InsertOrUpdate(moduleMapKey, RefPtr{moduleScript});
  }

  return loadingRequest.forget();
}

void ModuleLoaderBase::ResumeWaitingRequests(LoadingRequest* aLoadingRequest,
                                             bool aSuccess) {
  for (ModuleLoadRequest* request : aLoadingRequest->mWaiting) {
    ResumeWaitingRequest(request, aSuccess);
    request->NotifyModuleWaitFinished();
  }
}

void ModuleLoaderBase::ResumeWaitingRequest(ModuleLoadRequest* aRequest,
                                            bool aSuccess) {
  if (aSuccess) {
    aRequest->ModuleLoaded();
  }

  if (!aRequest->IsErrored()) {
    OnFetchSucceeded(aRequest);
  } else {
    OnFetchFailed(aRequest);
  }
}

void ModuleLoaderBase::WaitForModuleFetch(ModuleLoadRequest* aRequest) {
  ModuleMapKey moduleMapKey(aRequest->URI(), aRequest->mModuleType);
  MOZ_ASSERT(ModuleMapContainsURL(moduleMapKey));

  // Step 6. If moduleMap[(url, moduleType)] is a list, append onComplete to
  // moduleMap[(url, moduleType)], and return.
  if (auto entry = mFetchingModules.Lookup(moduleMapKey)) {
    const RefPtr<LoadingRequest>& loadingRequest = entry.Data();
    loadingRequest->mWaiting.AppendElement(aRequest);
    return;
  }

  // Step 5. If moduleMap[(url, moduleType)] is a module script or null, run
  // onComplete given moduleMap[(url, moduleType)], and return.
  //
  // Failed fetches are no longer cached, so the entry is always a module
  // script, never null.
  RefPtr<ModuleScript> ms;
  MOZ_ALWAYS_TRUE(mFetchedModules.Get(moduleMapKey, getter_AddRefs(ms)));
  MOZ_ASSERT(ms);

  ResumeWaitingRequest(aRequest, true);
}

ModuleScript* ModuleLoaderBase::GetFetchedModule(
    const ModuleMapKey& moduleMapKey) const {
  if (LOG_ENABLED()) {
    nsAutoCString url;
    moduleMapKey.mUri->GetAsciiSpec(url);
    LOG(("GetFetchedModule %s", url.get()));
  }

  bool found;
  ModuleScript* ms = mFetchedModules.GetWeak(moduleMapKey, &found);
  MOZ_ASSERT(found);
  return ms;
}

void ModuleLoaderBase::OnFetchComplete(ModuleLoadRequest* aRequest,
                                       nsresult aRv) {
  LOG(("ScriptLoadRequest (%p): OnFetchComplete result %x", aRequest,
       (unsigned)aRv));
  MOZ_ASSERT(aRequest->mLoader == this);
  MOZ_ASSERT(!aRequest->mModuleScript);

  nsresult rv = aRv;
  if (NS_SUCCEEDED(rv)) {
    rv = CreateModuleScript(aRequest);

#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
    // If a module script was created, it should either have a module record
    // object or a parse error.
    if (ModuleScript* ms = aRequest->mModuleScript) {
      MOZ_DIAGNOSTIC_ASSERT(bool(ms->ModuleRecord()) != ms->HasParseError());
    }
#endif

    if (aRequest->getLoadedScript()->IsTextSource()) {
      aRequest->getLoadedScript()->ClearScriptText();
    }

    if (NS_FAILED(rv)) {
      // Failing to create a module script leaves the request errored (its
      // module script is null), which the shared error path below handles the
      // same way as a failed fetch. A failed fetch is reported to the console
      // by the caller, so report this failure here.
      mLoader->ReportErrorToConsole(aRequest, rv);
    }
  }

  RefPtr<LoadingRequest> waitingRequests =
      SetModuleFetchFinishedAndGetWaitingRequests(aRequest, rv);
  MOZ_ASSERT_IF(waitingRequests, waitingRequests->mRequest == aRequest);

  bool success = bool(aRequest->mModuleScript);
  MOZ_ASSERT(NS_SUCCEEDED(rv) == success);

  // TODO: https://github.com/whatwg/html/issues/11755
  // Should we check if the module script has an error to rethrow as well?
  //
  // https://html.spec.whatwg.org/#hostloadimportedmodule:fetch-a-single-imported-module-script
  // Step 14. onSingleFetchComplete: step 2, and step 3
  //   return ThrowCompletion if moduleScript is null or its parse error is not
  //   null:
  //   Step 4. Otherwise, set completion to NormalCompletion(moduleScript's
  //     record).
  if (!aRequest->IsErrored()) {
    OnFetchSucceeded(aRequest);
  } else {
    OnFetchFailed(aRequest);
  }

  if (!waitingRequests) {
    return;
  }

  ResumeWaitingRequests(waitingRequests, success);
}

void ModuleLoaderBase::OnFetchSucceeded(ModuleLoadRequest* aRequest) {
  // TODO, Bug 1990416: Align the loading descendants behavior of dynamic import
  // According to the spec, dynamic import should trigger fetching of
  // descendant modules in the JS engine. However, when the dynamic import’s
  // module graph is loaded, ScriptLoaders move the request to a Loaded event
  // queue and invoke ProcessDynamicImport. To account for this behavior,
  // we perform the fetching of submodules in the host layer instead.
  if (aRequest->IsTopLevel() || aRequest->IsDynamicImport()) {
    if (aRequest->IsSourcePhaseRequest()) {
      if (!aRequest->IsCanceled()) {
        aRequest->SetReady();
        aRequest->LoadFinished();
      }
    } else {
      StartFetchingModuleDependencies(aRequest);
    }
  } else {
    MOZ_ASSERT(!aRequest->IsDynamicImport());
    AutoJSAPI jsapi;
    if (!jsapi.Init(mGlobalObject)) {
      return;
    }
    JSContext* cx = jsapi.cx();
    if (!FinishLoadingImportedModule(cx, aRequest)) {
      // The graph load was abandoned by an uncatchable error, e.g. the script
      // being terminated, so the parent request is never going to complete.
      aRequest->Cancel();
      return;
    }

    aRequest->SetReady();
    aRequest->LoadFinished();
  }
}

void ModuleLoaderBase::OnFetchFailed(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsErrored());
  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobalObject)) {
    return;
  }
  JSContext* cx = jsapi.cx();

  if (aRequest->IsTopLevel()) {
    // https://html.spec.whatwg.org/#fetch-the-descendants-of-and-link-a-module-script
    // Step 2. If record is null, then:
    // Step 2.1. Set moduleScript's error to rethrow to moduleScript's parse
    //           error.
    if (aRequest->mModuleScript && !aRequest->mModuleScript->ModuleRecord()) {
      MOZ_ASSERT(aRequest->mModuleScript->HasParseError());
      Value parseError = aRequest->mModuleScript->ParseError();
      LOG(("ScriptLoadRequest (%p): found parse error", aRequest));
      aRequest->mModuleScript->SetErrorToRethrow(parseError);
    }
    DispatchModuleErrored(aRequest);

    return;
  }

  // The remaining case is static/dynamic import.
  MOZ_ASSERT(aRequest->IsStaticImport() || aRequest->IsDynamicImport());
  MOZ_ASSERT(!aRequest->mPayload.isUndefined());
  Rooted<Value> payload(cx, aRequest->mPayload);

  // https://html.spec.whatwg.org/#hostloadimportedmodule
  //
  // Step 14.2. If moduleScript is null, then set completion to Completion
  //            Record { [[Type]]: throw, [[Value]]: a new TypeError,
  //            [[Target]]: empty }.
  if (!aRequest->mModuleScript) {
    LOG(
        ("ScriptLoadRequest (%p): FinishLoadingImportedModule: module script "
         "is null",
         aRequest));

    // Impl note:
    // For dynamic import, the TypeError will be pass to the rejected handler of
    // the LoadRequestedModules from the dynamic import.
    //
    // For static import/top-level import, the TypeError will be ignore in the
    // rejected handler of LoadRequestedModules (the state.[[ErrorToRethrow]]
    // isn't set). So we don't actually create a TypeError for the these two
    // cases.
    if (aRequest->GetRootModule()->IsDynamicImport()) {
      nsAutoCString url;
      aRequest->URI()->GetSpec(url);
      JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                JSMSG_DYNAMIC_IMPORT_FAILED, url.get());
      FinishLoadingImportedModuleFailedWithPendingException(cx, payload);
    } else {
      Rooted<Value> error(cx, UndefinedValue());
      FinishLoadingImportedModuleFailed(cx, payload, error);
    }

    aRequest->ModuleErrored();
    aRequest->ClearImport();
    return;
  }

  // Step 14.3. Otherwise, if moduleScript's parse error is not null, then:
  //   1. Let parseError be moduleScript's parse error.
  //   2. Set completion to Completion Record { [[Type]]: throw,
  //      [[Value]]: parseError, [[Target]]: empty }.
  //   3. If loadState is not undefined and loadState.[[ErrorToRethrow]]
  //      is null, set loadState.[[ErrorToRethrow]] to parseError.
  MOZ_ASSERT(aRequest->mModuleScript->HasParseError());
  Rooted<Value> parseError(cx, aRequest->mModuleScript->ParseError());
  LOG(
      ("ScriptLoadRequest (%p): FinishLoadingImportedModule: found parse "
       "error",
       aRequest));
  // Step 14.5. Perform FinishLoadingImportedModule(referrer, moduleRequest,
  //            payload, completion).
  FinishLoadingImportedModuleFailed(cx, payload, parseError);
  aRequest->ModuleErrored();
  aRequest->ClearImport();
}

void ModuleLoaderBase::Cancel(ModuleLoadRequest* aRequest) {
  if (aRequest->IsFinished()) {
    return;
  }

  aRequest->ScriptLoadRequest::Cancel();
  aRequest->mModuleScript = nullptr;

  if (aRequest->mPayload.isUndefined()) {
    return;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobalObject)) {
    return;
  }
  JSContext* cx = jsapi.cx();

  Rooted<Value> payload(cx, aRequest->mPayload);
  Rooted<Value> error(cx, UndefinedValue());

  LOG(
      ("ScriptLoadRequest (%p): Canceled, calling "
       "FinishLoadingImportedModuleFailed",
       aRequest));

  FinishLoadingImportedModuleFailed(cx, payload, error);
  aRequest->ModuleErrored();
  aRequest->ClearImport();
}

class ModuleErroredRunnable : public MicroTaskRunnable {
 public:
  explicit ModuleErroredRunnable(ModuleLoadRequest* aRequest)
      : mRequest(aRequest) {}

  virtual void Run(AutoSlowOperation& aAso) override {
    mRequest->ModuleErrored();
  }

 private:
  RefPtr<ModuleLoadRequest> mRequest;
};

void ModuleLoaderBase::DispatchModuleErrored(ModuleLoadRequest* aRequest) {
  if (aRequest->HasScriptLoadContext() &&
      aRequest->GetScriptLoadContext()->mIsInline) {
    // https://html.spec.whatwg.org/#prepare-the-script-element
    // Step 32. If el does not have a src content attribute:
    //   2. "module".3.1
    //   Queue an element task on the networking task source given el to
    //   perform the following steps:
    CycleCollectedJSContext* context = CycleCollectedJSContext::Get();
    RefPtr<ModuleErroredRunnable> runnable =
        new ModuleErroredRunnable(aRequest);
    context->DispatchToMicroTask(runnable.forget());
  } else {
    aRequest->ModuleErrored();
  }
}

nsresult ModuleLoaderBase::CreateModuleScript(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(!aRequest->mModuleScript);
  MOZ_ASSERT(aRequest->BaseURL());

  LOG(("ScriptLoadRequest (%p): Create module script", aRequest));

  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobalObject)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  {
    JSContext* cx = jsapi.cx();
    Rooted<JSObject*> module(cx);

    CompileOptions options(cx);
    RootedScript introductionScript(cx);
    rv = mLoader->FillCompileOptionsForRequest(cx, aRequest, &options,
                                               &introductionScript);

    if (NS_SUCCEEDED(rv)) {
      Rooted<JSObject*> global(cx, mGlobalObject->GetGlobalJSObject());
      rv = CompileFetchedModule(cx, global, options, aRequest, &module);
    }

    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv) == (module != nullptr));

    if (module) {
      RootedScript moduleScript(cx, GetModuleScript(module));
      if (moduleScript) {
        RootedValue privateValue(cx);
        InstantiateOptions instantiateOptions(options);
        if (!UpdateDebugMetadata(cx, moduleScript, instantiateOptions,
                                 privateValue, nullptr, introductionScript,
                                 nullptr)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
    }

    MOZ_ASSERT(aRequest->mLoadedScript->IsModuleScript());

    RefPtr<ModuleScript> moduleScript = new ModuleScript(aRequest->FetchInfo());

    aRequest->mModuleScript = moduleScript;

    moduleScript->SetForPreload(aRequest->mLoadContext->IsPreload());
    moduleScript->SetHadImportMap(HasImportMapRegistered());

    if (!module) {
      LOG(("ScriptLoadRequest (%p):   compilation failed (%d)", aRequest,
           unsigned(rv)));

      Rooted<Value> error(cx);
      if (!jsapi.HasException() || !jsapi.StealException(&error) ||
          error.isUndefined()) {
        aRequest->mModuleScript = nullptr;
        return NS_ERROR_FAILURE;
      }

      moduleScript->SetParseError(error);
      return NS_OK;
    }

    moduleScript->SetModuleRecord(module);

    if (IsCyclicModule(module)) {
      aRequest->FetchInfo()->AssociateWithModule(module);
    }
  }

  LOG(("ScriptLoadRequest (%p):   module script == %p ForPreload %d", aRequest,
       aRequest->mModuleScript.get(), aRequest->mModuleScript->ForPreload()));

  return rv;
}

nsresult ModuleLoaderBase::GetResolveFailureMessage(ResolveError aError,
                                                    const nsAString& aSpecifier,
                                                    nsAString& aResult) {
  AutoTArray<nsString, 1> errorParams;
  errorParams.AppendElement(aSpecifier);

  nsresult rv = nsContentUtils::FormatLocalizedString(
      PropertiesFile::DOM_PROPERTIES, ResolveErrorInfo::GetString(aError),
      errorParams, aResult);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult ModuleLoaderBase::HandleResolveFailure(
    JSContext* aCx, ScriptFetchInfo* aFetchInfo, const nsAString& aSpecifier,
    ResolveError aError, uint32_t aLineNumber,
    ColumnNumberOneOrigin aColumnNumber, MutableHandle<Value> aErrorOut) {
  Rooted<JSString*> filename(aCx);
  if (aFetchInfo) {
    nsAutoCString url;
    aFetchInfo->BaseURL()->GetAsciiSpec(url);
    filename = JS_NewStringCopyZ(aCx, url.get());
  } else {
    filename = JS_NewStringCopyZ(aCx, "(unknown)");
  }

  if (!filename) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsAutoString errorText;
  nsresult rv = GetResolveFailureMessage(aError, aSpecifier, errorText);
  NS_ENSURE_SUCCESS(rv, rv);

  Rooted<JSString*> string(aCx, JS_NewUCStringCopyZ(aCx, errorText.get()));
  if (!string) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!CreateError(aCx, JSEXN_TYPEERR, nullptr, filename, aLineNumber,
                   aColumnNumber, nullptr, string, NothingHandleValue,
                   aErrorOut)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

ResolveResult ModuleLoaderBase::ResolveModuleSpecifier(
    ScriptFetchInfo* aFetchInfo, const nsAString& aSpecifier) {
  // Import Maps are not supported on workers/worklets.
  // See https://github.com/WICG/import-maps/issues/2
  MOZ_ASSERT_IF(!NS_IsMainThread(), mImportMap == nullptr);

  // Forward to the updated 'Resolve a module specifier' algorithm defined in
  // the Import Maps spec.
  return ImportMap::ResolveModuleSpecifier(mImportMap.get(), mLoader,
                                           aFetchInfo, aSpecifier);
}

ResolvedModuleSet* ModuleLoaderBase::GetResolvedModuleSet() {
  MOZ_ASSERT(ImportMap::IsMultipleImportMapsSupported());
  if (!mResolvedModuleSet) {
    mResolvedModuleSet = std::make_unique<ResolvedModuleSet>();
  }

  return mResolvedModuleSet.get();
}

static void AddToResolvedSet(ResolvedModuleSet* aSet,
                             UniquePtr<SpecifierResolutionRecord> aRecord) {
  MOZ_ASSERT(ImportMap::IsMultipleImportMapsSupported());
  auto ptr = aSet->lookupForAdd(aRecord);
  if (ptr) {
    return;
  }

  MOZ_ALWAYS_TRUE(aSet->add(ptr, std::move(aRecord)));
}

void ModuleLoaderBase::AddToPreloadedResolvedSet(
    ModuleLoadRequest* aRootRequest,
    UniquePtr<SpecifierResolutionRecord> aRecord) {
  MOZ_ASSERT(aRootRequest);
  MOZ_ASSERT(aRootRequest->mModuleScript);

  ResolvedModuleSet* set =
      aRootRequest->mModuleScript->GetPreloadedResolvedSet();
  MOZ_ASSERT(set);
  AddToResolvedSet(set, std::move(aRecord));
}

void ModuleLoaderBase::AddToGlobalResolvedSet(
    UniquePtr<SpecifierResolutionRecord> aRecord) {
  AddToResolvedSet(GetResolvedModuleSet(), std::move(aRecord));
}

static ModuleLoadRequest* GetPreloadRootModuleRequest(
    JS::Handle<JS::Value> aHostDefined) {
  MOZ_ASSERT(!aHostDefined.isUndefined());
  ModuleLoadRequest* parent =
      static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
  MOZ_ASSERT(parent);
  return parent->GetRootModule();
}

void ModuleLoaderBase::AddToResolvedModuleSet(
    UniquePtr<SpecifierResolutionRecord> aRecord,
    Handle<Value> aHostDefined /* = UndefinedHandleValue */) {
  // 2. If global does not implement Window, then return.
  if (!mLoader->IsImportMapSupported()) {
    return;
  }

  // aHostDefined is undefined only for dynamic imports, which are never part of
  // a preload. Otherwise it is for static imports, whose root request tracks
  // whether the graph is still being preloaded.
  if (!aHostDefined.isUndefined()) {
    RefPtr<ModuleLoadRequest> root = GetPreloadRootModuleRequest(aHostDefined);
    if (root->mLoadContext->IsPreload()) {
      AddToPreloadedResolvedSet(root, std::move(aRecord));
      return;
    }
  }

  // release the mResult from the record as it is not needed.
  nsCOMPtr<nsIURI> _ = aRecord->TakeResult();
  AddToGlobalResolvedSet(std::move(aRecord));
}

// This just flip the preload flag in the module script and ModuleObject.
void ModuleLoaderBase::ResetPreloadFlag(nsIURI* aURI) {
  MOZ_ASSERT(aURI);
  ModuleMapKey key(aURI, ModuleType::JavaScript);
  if (!IsModuleFetched(key)) {
    return;
  }

  RefPtr<ModuleScript> ms;
  MOZ_ALWAYS_TRUE(mFetchedModules.Get(key, getter_AddRefs(ms)));

  // This function is called from a preloaded module graph, but it may have a
  // module script from another preloaded module graph which has been turned
  // into a normal load, so we don't assert the module script is a preloaded
  // one.
  ms->SetForPreload(false);
}

// This is called when a preloaded module request is reused, so we need to move
// the resolved set into the global's set.
// Also, we need to flip the preload flag in the module script.
void ModuleLoaderBase::MovePreloadedSetToResolvedSet(
    ModuleLoadRequest* aRootRequest) {
  LOG(("ScriptLoadRequest (%p): MovePreloadedSetToResolvedSet", aRootRequest));
  MOZ_ASSERT(ImportMap::IsMultipleImportMapsSupported());
  MOZ_ASSERT(aRootRequest);

  // ScriptLoader has flipped the preload flag to false.
  MOZ_ASSERT(!aRootRequest->mLoadContext->IsPreload());

  ModuleScript* ms = aRootRequest->mModuleScript;
  if (!ms) {
    return;
  }

  ms->SetForPreload(false);
  if (!ms->HasPreloadedResolvedSet()) {
    return;
  }

  ResolvedModuleSet* set = ms->GetPreloadedResolvedSet();
  for (auto iter = set->modIter(); !iter.done(); iter.next()) {
    auto record = std::move(iter.getMutable());
    nsCOMPtr<nsIURI> uri = record->TakeResult();
    ResetPreloadFlag(uri);
    AddToGlobalResolvedSet(std::move(record));
  }

  set->clear();
  ms->ReleasePreloadedResolvedSet();
}

// If the module script is preloaded, reset the [[LoadedModules]]
//
void ModuleLoaderBase::ResetPreloadedModule(nsIURI* aURI) {
  MOZ_ASSERT(aURI);
  ModuleMapKey key(aURI, ModuleType::JavaScript);
  if (!IsModuleFetched(key)) {
    return;
  }

  RefPtr<ModuleScript> ms;
  MOZ_ALWAYS_TRUE(mFetchedModules.Get(key, getter_AddRefs(ms)));
  if (!ms->ForPreload()) {
    return;
  }

  LOG(
      ("ModuleLoaderBase::ResetPreloadedModule: module script (%p) reset "
       "preloaded info",
       ms.get()));
  ms->ResetPreload();
}

// This is called before registering an import map, so we clear the remaing
// mPreloads from the ScriptLoader
void ModuleLoaderBase::ClearPreloadedModuleGraph(
    ModuleLoadRequest* aRootRequest) {
  MOZ_ASSERT(ImportMap::IsMultipleImportMapsSupported());
  MOZ_ASSERT(aRootRequest);
  MOZ_ASSERT(aRootRequest->mLoadContext->IsPreload());
  LOG(("ScriptLoadRequest (%p): ClearPreloadedModuleGraph", aRootRequest));

  ModuleScript* ms = aRootRequest->mModuleScript;
  if (!ms) {
    return;
  }

  // If the preloaded request has a module script which has been used previously
  // in another preloaed module graph, which has been reused and is a normal
  // load now, we don't need to reset its data anymore.
  if (!ms->ForPreload()) {
    return;
  }

  ms->ResetPreload();

  if (!ms->HasPreloadedResolvedSet()) {
    return;
  }

  ResolvedModuleSet* set = ms->GetPreloadedResolvedSet();
  for (auto iter = set->iter(); !iter.done(); iter.next()) {
    nsCOMPtr<nsIURI> uri = iter.get()->TakeResult();
    MOZ_ASSERT(uri);
    ResetPreloadedModule(uri);
  }
  set->clear();
  ms->ReleasePreloadedResolvedSet();
}

void ModuleLoaderBase::StartFetchingModuleDependencies(
    ModuleLoadRequest* aRequest) {
  if (aRequest->IsCanceled()) {
    return;
  }

  MOZ_ASSERT(aRequest->mModuleScript);
  MOZ_ASSERT(!aRequest->mModuleScript->HasParseError());
  ModuleScript* moduleScript = aRequest->mModuleScript;
  MOZ_ASSERT(moduleScript->ModuleRecord());
  MOZ_ASSERT(aRequest->IsFetching() || aRequest->IsCompiling());
  MOZ_ASSERT(aRequest->IsTopLevel() || aRequest->IsDynamicImport());

  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(mGlobalObject))) {
    return;
  }
  JSContext* cx = jsapi.cx();

  Rooted<JSObject*> module(cx, moduleScript->ModuleRecord());

  LOG(
      ("ScriptLoadRequest (%p): module record (%p) Start fetching module "
       "dependencies",
       aRequest, module.get()));

  // Wrap the request into a JS::Value, and AddRef() it.
  // The Release() will be called in the resolved/rejected handlers.
  Rooted<Value> hostDefinedVal(cx, PrivateValue(aRequest));
  aRequest->AddRef();

  bool result = false;

  // A microtask job is dropped if the global is being destroyed or if scripting
  // is disabled. As a result, the promise returned by LoadRequestedModules
  // never settles: module loading stalls, document loading is blocked, and the
  // ModuleLoadRequest above is never released.
  //
  // Use the synchronous variant of LoadRequestedModules in those cases. The
  // scheme checks cover chrome pages, whose global can be torn down after
  // this point, while the dependency is still fetching.
  Rooted<JSObject*> global(cx, mGlobalObject->GetGlobalJSObject());
  bool isSync = aRequest->URI()->SchemeIs("chrome") ||
                aRequest->URI()->SchemeIs("resource") ||
                !mGlobalObject->CanRunJSMicroTask(global);

  // TODO: Bug1973660: Use Promise version of LoadRequestedModules on Workers.
  if (aRequest->HasScriptLoadContext() && !isSync) {
    Rooted<JSFunction*> onResolved(
        cx, js::NewFunctionWithReserved(cx, OnLoadRequestedModulesResolved,
                                        OnLoadRequestedModulesResolvedNumArgs,
                                        0, "resolved"));
    if (!onResolved) {
      JS_ReportOutOfMemory(cx);
      return;
    }

    RootedFunction onRejected(
        cx, js::NewFunctionWithReserved(cx, OnLoadRequestedModulesRejected,
                                        OnLoadRequestedModulesRejectedNumArgs,
                                        0, "rejected"));
    if (!onRejected) {
      JS_ReportOutOfMemory(cx);
      return;
    }

    RootedObject resolveFuncObj(cx, JS_GetFunctionObject(onResolved));
    js::SetFunctionNativeReserved(resolveFuncObj, LoadReactionHostDefinedSlot,
                                  hostDefinedVal);

    RootedObject rejectFuncObj(cx, JS_GetFunctionObject(onRejected));
    js::SetFunctionNativeReserved(rejectFuncObj, LoadReactionHostDefinedSlot,
                                  hostDefinedVal);

    Rooted<JSObject*> loadPromise(cx);
    result = LoadRequestedModules(cx, module, hostDefinedVal, &loadPromise);
    if (result) {
      AddPromiseReactions(cx, loadPromise, resolveFuncObj, rejectFuncObj);
    }
  } else {
    result = LoadRequestedModules(cx, module, hostDefinedVal,
                                  OnLoadRequestedModulesResolved,
                                  OnLoadRequestedModulesRejected);
  }

  if (!result) {
    LOG(("ScriptLoadRequest (%p): LoadRequestedModules failed", aRequest));
    OnLoadRequestedModulesRejected(cx, aRequest, UndefinedHandleValue);
  }
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesResolved(JSContext* aCx,
                                                      unsigned aArgc,
                                                      Value* aVp) {
  CallArgs args = CallArgsFromVp(aArgc, aVp);
  Rooted<Value> hostDefined(aCx);
  hostDefined = js::GetFunctionNativeReserved(&args.callee(),
                                              LoadReactionHostDefinedSlot);
  return OnLoadRequestedModulesResolved(aCx, hostDefined);
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesResolved(
    JSContext* aCx, Handle<Value> aHostDefined) {
  auto* request = static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
  MOZ_ASSERT(request);
  return OnLoadRequestedModulesResolved(request);
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesResolved(
    ModuleLoadRequest* aRequest) {
  LOG(("ScriptLoadRequest (%p): LoadRequestedModules resolved", aRequest));
  if (!aRequest->IsCanceled()) {
    aRequest->SetReady();
    aRequest->LoadFinished();
  }

  // Decrease the reference 'AddRef'ed when converting the hostDefined.
  aRequest->Release();
  return true;
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesRejected(JSContext* aCx,
                                                      unsigned aArgc,
                                                      Value* aVp) {
  CallArgs args = CallArgsFromVp(aArgc, aVp);
  Rooted<Value> error(aCx, args.get(OnLoadRequestedModulesRejectedErrorArg));
  Rooted<Value> hostDefined(aCx);
  hostDefined = js::GetFunctionNativeReserved(&args.callee(),
                                              LoadReactionHostDefinedSlot);
  return OnLoadRequestedModulesRejected(aCx, hostDefined, error);
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesRejected(
    JSContext* aCx, Handle<Value> aHostDefined, Handle<Value> aError) {
  auto* request = static_cast<ModuleLoadRequest*>(aHostDefined.toPrivate());
  MOZ_ASSERT(request);
  return OnLoadRequestedModulesRejected(aCx, request, aError);
}

// static
bool ModuleLoaderBase::OnLoadRequestedModulesRejected(
    JSContext* aCx, ModuleLoadRequest* aRequest, Handle<Value> error) {
  ModuleScript* moduleScript = aRequest->mModuleScript;

  if (aRequest->IsCanceled()) {
    aRequest->Release();
    return true;
  }

  // TODO, Bug 1990416: Align the loading descendants behavior of dynamic import
  if (aRequest->IsDynamicImport()) {
    LOG(
        ("ScriptLoadRequest (%p): LoadRequestedModules rejected for dynamic "
         "import",
         aRequest));
    // https://tc39.es/ecma262/#sec-ContinueDynamicImport
    // Step 4.a. Perform ! Call(promiseCapability.[[Reject]], undefined,
    //   « reason »).
    Rooted<Value> payload(aCx, aRequest->mPayload);
    if (!error.isUndefined()) {
      FinishLoadingImportedModuleFailed(aCx, payload, error);
    } else {
      nsAutoCString url;
      aRequest->URI()->GetSpec(url);
      JS_ReportErrorNumberASCII(aCx, js::GetErrorMessage, nullptr,
                                JSMSG_DYNAMIC_IMPORT_FAILED, url.get());
      FinishLoadingImportedModuleFailedWithPendingException(aCx, payload);
    }
    aRequest->SetErroredLoadingImports();

    // The promise has been settled, so the import is done. The other error
    // paths clear the import as well, see OnFetchFailed and Cancel.
    aRequest->ClearImport();
  } else if (moduleScript && !error.isUndefined()) {
    LOG(
        ("ScriptLoadRequest (%p): LoadRequestedModules rejected: set error to "
         "rethrow",
         aRequest));
    // https://html.spec.whatwg.org/#fetch-the-descendants-of-and-link-a-module-script
    // Step 7. Upon rejection of loadingPromise, run the following
    //         steps:
    // Step 7.1. If state.[[ErrorToRethrow]] is not null, set moduleScript's
    //           error to rethrow to state.[[ErrorToRethrow]] and run
    //           onComplete given moduleScript.
    moduleScript->SetErrorToRethrow(error);
  } else {
    LOG(
        ("ScriptLoadRequest (%p): LoadRequestedModules rejected: set module "
         "script to null",
         aRequest));
    // Step 7.2. Otherwise, run onComplete given null.
    aRequest->mModuleScript = nullptr;
  }

  aRequest->ModuleErrored();

  // Decrease the reference 'AddRef'ed when converting the hostDefined.
  aRequest->Release();
  return true;
}

bool ModuleLoaderBase::GetImportMapSRI(
    nsIURI* aURI, nsIURI* aSourceURI, nsIConsoleReportCollector* aReporter,
    mozilla::dom::SRIMetadata* aMetadataOut) {
  MOZ_ASSERT(aMetadataOut->IsEmpty());
  MOZ_ASSERT(aURI);

  if (!HasImportMapRegistered()) {
    return false;
  }

  mozilla::Maybe<nsString> entry =
      ImportMap::LookupIntegrity(mImportMap.get(), aURI);
  if (entry.isNothing()) {
    return false;
  }

  mozilla::dom::SRICheck::IntegrityMetadata(
      *entry, aSourceURI->GetSpecOrDefault(), aReporter, aMetadataOut);
  return true;
}

void ModuleLoaderBase::AppendDynamicImport(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->mLoader == this);
  mDynamicImportRequests.AppendElement(aRequest);
}

ModuleLoaderBase::ModuleLoaderBase(ScriptLoaderInterface* aLoader,
                                   nsIGlobalObject* aGlobalObject)
    : mGlobalObject(aGlobalObject), mLoader(aLoader) {
  MOZ_ASSERT(mGlobalObject);
  MOZ_ASSERT(mLoader);

  EnsureModuleHooksInitialized();
}

ModuleLoaderBase::~ModuleLoaderBase() {
  mDynamicImportRequests.CancelRequestsAndClear();

  LOG(("ModuleLoaderBase::~ModuleLoaderBase %p", this));
}

nsIURI* ModuleLoaderBase::GetBaseURI() const { return mLoader->GetBaseURI(); }

void ModuleLoaderBase::CancelFetchingModules() {
  for (const auto& entry : mFetchingModules) {
    RefPtr<LoadingRequest> loadingRequest = entry.GetData();
    loadingRequest->mRequest->Cancel();

    for (const auto& request : loadingRequest->mWaiting) {
      request->Cancel();
      request->NotifyModuleWaitFinished();
    }
  }

  // Drop the canceled requests rather than leaving HasFetchingModules() true
  // forever. A request's fetch may still complete (unsuccessfully) afterwards;
  // OnFetchComplete tolerates the entry no longer being in mFetchingModules.
  mFetchingModules.Clear();
}

void ModuleLoaderBase::Shutdown() {
  // Resume the waiting requests before cancelling the dynamic imports. A
  // dynamic import waiting on another request's fetch is in both lists, and
  // cancelling it first completes it and clears its import, which leaves
  // ResumeWaitingRequest() calling OnFetchFailed() with no payload. Resuming
  // first errors it through OnFetchFailed(), which rejects its promise and
  // removes it from mDynamicImportRequests.
  for (const auto& entry : mFetchingModules) {
    RefPtr<LoadingRequest> loadingRequest(entry.GetData());
    if (loadingRequest) {
      ResumeWaitingRequests(loadingRequest, false);
    }
  }

  CancelAndClearDynamicImports();

  for (const auto& entry : mFetchedModules) {
    if (entry.GetData()) {
      entry.GetData()->Shutdown();
    }
  }

  mFetchingModules.Clear();
  mFetchedModules.Clear();
  mGlobalObject = nullptr;
  mLoader = nullptr;
}

bool ModuleLoaderBase::HasFetchingModules() const {
  return !mFetchingModules.IsEmpty();
}

bool ModuleLoaderBase::HasPendingDynamicImports() const {
  return !mDynamicImportRequests.isEmpty();
}

void ModuleLoaderBase::CancelDynamicImport(ModuleLoadRequest* aRequest,
                                           nsresult aResult) {
  // aRequest may have already been unlinked by CC.
  MOZ_ASSERT(aRequest->mLoader == this || !aRequest->mLoader);

  RefPtr<ScriptLoadRequest> req = mDynamicImportRequests.Steal(aRequest);
  if (!aRequest->IsCanceled()) {
    // If the ClearDynamicImport() has been called, then it should have been
    // removed from mDynamicImportRequests as well.
    MOZ_ASSERT(!aRequest->mPayload.isUndefined());
    aRequest->Cancel();
  }
}

void ModuleLoaderBase::RemoveDynamicImport(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsDynamicImport());
  mDynamicImportRequests.Remove(aRequest);
}

#ifdef DEBUG
bool ModuleLoaderBase::HasDynamicImport(
    const ModuleLoadRequest* aRequest) const {
  MOZ_ASSERT(aRequest->mLoader == this);
  return mDynamicImportRequests.Contains(
      const_cast<ModuleLoadRequest*>(aRequest));
}
#endif

bool ModuleLoaderBase::InstantiateModuleGraph(ModuleLoadRequest* aRequest) {
  // Instantiate a top-level module and record any error.

  MOZ_ASSERT(aRequest);
  MOZ_ASSERT(aRequest->mLoader == this);
  MOZ_ASSERT(aRequest->IsTopLevel());

  LOG(("ScriptLoadRequest (%p): Instantiate module graph", aRequest));

  AUTO_PROFILER_LABEL("ModuleLoaderBase::InstantiateModuleGraph", JS);

  ModuleScript* moduleScript = aRequest->mModuleScript;
  MOZ_ASSERT(moduleScript);

  MOZ_ASSERT(!moduleScript->HasParseError());
  MOZ_ASSERT(moduleScript->ModuleRecord());

  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(mGlobalObject))) {
    return false;
  }

  JSContext* cx = jsapi.cx();
  Rooted<JSObject*> module(cx, moduleScript->ModuleRecord());
  if (!xpc::Scriptability::AllowedIfExists(module)) {
    return true;
  }

  if (!ModuleLink(jsapi.cx(), module)) {
    LOG(("ScriptLoadRequest (%p): Instantiate failed", aRequest));
    MOZ_ASSERT(jsapi.HasException());
    RootedValue exception(jsapi.cx());
    if (!jsapi.StealException(&exception)) {
      return false;
    }
    MOZ_ASSERT(!exception.isUndefined());
    moduleScript->SetErrorToRethrow(exception);
  }

  return true;
}

void ModuleLoaderBase::ProcessDynamicImport(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->IsDynamicImport());

  // A request that failed to fetch or compile is processed in OnFetchFailed,
  // and a request whose imports failed to load is processed in
  // OnLoadRequestedModulesRejected. Both reject the promise of the dynamic
  // import and clear the import, so there is nothing left to do here.
  if (aRequest->IsErrored() || aRequest->IsErroredLoadingImports()) {
    LOG(("ScriptLoadRequest (%p): ProcessDynamicImport, request has an error",
         aRequest));
    MOZ_ASSERT_IF(aRequest->IsErroredLoadingImports(),
                  aRequest->mPayload.isUndefined());
    return;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(GetGlobalObject())) {
    return;
  }
  JSContext* cx = jsapi.cx();

  LOG(("ScriptLoadRequest (%p): ProcessDynamicImport", aRequest));
  if (!FinishLoadingImportedModule(cx, aRequest)) {
    // The import was abandoned by an uncatchable error, e.g. the script being
    // terminated, so don't record it as a successful execution.
    return;
  }

  // TODO: Implement caching for wasm modules (Bug 1998240).
  if (!aRequest->IsWasmBytes()) {
    (void)mLoader->MaybePrepareModuleForDiskCacheAfterExecute(aRequest, NS_OK);
  }

  mLoader->MaybeUpdateDiskCache();
}

nsresult ModuleLoaderBase::EvaluateModule(ModuleLoadRequest* aRequest) {
  MOZ_ASSERT(aRequest->mLoader == this);

  mozilla::nsAutoMicroTask mt;
  mozilla::dom::AutoEntryScript aes(mGlobalObject, "EvaluateModule",
                                    NS_IsMainThread());

  // We would like to handle any evaluation errors synchronously for service
  // workers immediately such that if we are performing service worker
  // registration we can fail it right away rather than having it fail later on
  // an event loop turn which might be too late and the registration process
  // would have succeeded by then.
  return EvaluateModuleInContext(
      aes.cx(), aRequest,
      IsForServiceWorker() ? ThrowModuleErrorsSync : ReportModuleErrorsAsync);
}

nsresult ModuleLoaderBase::EvaluateModuleInContext(
    JSContext* aCx, ModuleLoadRequest* aRequest,
    ModuleErrorBehaviour errorBehaviour) {
  MOZ_ASSERT(aRequest->mLoader == this);
  MOZ_ASSERT_IF(!mGlobalObject->GetModuleLoader(aCx)->IsOverridden(),
                mGlobalObject->GetModuleLoader(aCx) == this);
  MOZ_ASSERT_IF(mGlobalObject->GetModuleLoader(aCx)->IsOverridden(),
                mGlobalObject->GetModuleLoader(aCx)->IsOverriddenBy(this));
  MOZ_ASSERT(!aRequest->IsDynamicImport());

  AUTO_PROFILER_LABEL("ModuleLoaderBase::EvaluateModule", JS);

  nsAutoCString profilerLabelString;
  if (aRequest->HasScriptLoadContext()) {
    aRequest->GetScriptLoadContext()->GetProfilerLabel(profilerLabelString);
  }

  LOG(("ScriptLoadRequest (%p): Evaluate Module", aRequest));
  AUTO_PROFILER_MARKER_TEXT("ModuleEvaluation", JS,
                            MarkerInnerWindowIdFromJSContext(aCx),
                            profilerLabelString);

  MOZ_ASSERT(aRequest->mModuleScript);
  MOZ_ASSERT_IF(aRequest->HasScriptLoadContext(),
                !aRequest->GetScriptLoadContext()->mCompileOrDecodeTask);

  ModuleScript* moduleScript = aRequest->mModuleScript;
  if (moduleScript->HasErrorToRethrow()) {
    LOG(("ScriptLoadRequest (%p):   module has error to rethrow", aRequest));
    Rooted<Value> error(aCx, moduleScript->ErrorToRethrow());
    JS_SetPendingException(aCx, error);
    return NS_OK;
  }

  Rooted<JSObject*> module(aCx, moduleScript->ModuleRecord());
  MOZ_ASSERT(module);
  MOZ_ASSERT(CurrentGlobalOrNull(aCx) == GetNonCCWObjectGlobal(module));

  if (!xpc::Scriptability::AllowedIfExists(module)) {
    return NS_OK;
  }

  if (aRequest->HasScriptLoadContext()) {
    TRACE_FOR_TEST(aRequest, "evaluate:module");
  }

  Rooted<Value> rval(aCx);

  bool ok = ModuleEvaluate(aCx, module, &rval);

  // ModuleEvaluate will usually set a pending exception if it returns false,
  // unless the user cancels execution.
  MOZ_ASSERT_IF(ok, !JS_IsExceptionPending(aCx));

  nsresult rv = NS_OK;
  if (!ok || IsModuleEvaluationAborted(aRequest)) {
    LOG(("ScriptLoadRequest (%p):   evaluation failed", aRequest));
    // For a dynamic import, the promise is rejected. Otherwise an error is
    // reported by AutoEntryScript.
    rv = NS_ERROR_ABORT;
  }

  // ModuleEvaluate returns a promise unless the user cancels the execution in
  // which case rval will be undefined. We should treat it as a failed
  // evaluation, and reject appropriately.
  Rooted<JSObject*> evaluationPromise(aCx);
  if (rval.isObject()) {
    evaluationPromise.set(&rval.toObject());
  }

  // If the promise is rejected, the value is unwrapped from the promise value.
  if (!ThrowOnModuleEvaluationFailure(aCx, evaluationPromise, errorBehaviour)) {
    LOG(("ScriptLoadRequest (%p):   evaluation failed on throw", aRequest));

    if (IsForServiceWorker()) {
      // If this module eval is being done for service workers, then we would
      // like to throw/return right from here, such that if we are in
      // registration phase, we can fail it synchronously right away.
      return NS_ERROR_ABORT;
    }
  }

  // TODO: Implement caching for wasm modules (Bug 1998240).
  if (!aRequest->IsWasmBytes()) {
    rv = mLoader->MaybePrepareModuleForDiskCacheAfterExecute(aRequest, NS_OK);
  }

  mLoader->MaybeUpdateDiskCache();

  return rv;
}

void ModuleLoaderBase::CancelAndClearDynamicImports() {
  while (ScriptLoadRequest* req = mDynamicImportRequests.getFirst()) {
    // This also removes the request from the list.
    CancelDynamicImport(req->AsModuleRequest(), NS_ERROR_ABORT);
  }
}

UniquePtr<ImportMap> ModuleLoaderBase::ParseImportMap(
    ScriptLoadRequest* aRequest) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(GetGlobalObject())) {
    return nullptr;
  }

  MOZ_ASSERT(aRequest->IsFetchedAsTextSource());
  MaybeSourceText maybeSource;
  nsresult rv = aRequest->GetScriptSource(jsapi.cx(), &maybeSource,
                                          aRequest->mLoadContext.get());
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  SourceText<char16_t>& text = maybeSource.ref<SourceText<char16_t>>();
  ReportWarningHelper warning{mLoader, aRequest};

  // https://html.spec.whatwg.org/multipage/webappapis.html#create-an-import-map-parse-result
  // Step 2. Parse an import map string given input and baseURL, catching any
  // exceptions. If this threw an exception, then set result's error to rethrow
  // to that exception. Otherwise, set result's import map to the return value.
  //
  // https://html.spec.whatwg.org/multipage/webappapis.html#register-an-import-map
  // Step 1. If result's error to rethrow is not null, then report the exception
  // given by result's error to rethrow and return.
  //
  // Impl note: We didn't implement 'Import map parse result' from the spec,
  // https://html.spec.whatwg.org/multipage/webappapis.html#import-map-parse-result
  // As the struct has another item called 'error to rethow' to store the
  // exception thrown during parsing import-maps, and report that exception
  // while registering an import map. Currently only inline import-maps are
  // supported, therefore parsing and registering import-maps will be executed
  // consecutively. To simplify the implementation, we didn't create the 'error
  // to rethow' item and report the exception immediately(done in ~AutoJSAPI).
  return ImportMap::ParseString(jsapi.cx(), text, aRequest->BaseURL(), warning);
}

void ModuleLoaderBase::RegisterImportMap(UniquePtr<ImportMap> aImportMap,
                                         ScriptLoadRequest* aRequest) {
  LOG(("RegisterImportMap"));

  // Check for aImportMap is done in ScriptLoader.
  MOZ_ASSERT(aImportMap);

  // https://html.spec.whatwg.org/multipage/webappapis.html#register-an-import-map
  // The step 1(report the exception if there's an error) is done in
  // ParseImportMap.

  bool multiImportMapsEnabled = ImportMap::IsMultipleImportMapsSupported();
  if (!multiImportMapsEnabled) {
    MOZ_ASSERT(!mImportMap);
    mImportMap = std::move(aImportMap);
  } else {
    ReportWarningHelper warning{mLoader, aRequest};

    // Step 2. Merge existing and new import maps, given global and result's
    //         import map.
    ImportMap::Merge(this, std::move(aImportMap), warning);
    MOZ_ASSERT(mImportMap);
  }

  // If speculative preloading is currently fetching any modules then
  // cancel their requests and remove them from the map.
  //
  // The cancelled requests will still complete later so we have to check this
  // in SetModuleFetchFinishedAndGetWaitingRequests.
  mFetchingModules.RemoveIf([](auto& iter) {
    LoadingRequest* loadingRequest = iter.Data();
    bool isPreload = loadingRequest->mRequest->mLoadContext->IsPreload();
    if (!isPreload) {
      return false;
    }

    loadingRequest->mRequest->Cancel();
    for (const auto& request : loadingRequest->mWaiting) {
      MOZ_DIAGNOSTIC_ASSERT(request->mLoadContext->IsPreload());
      request->Cancel();
      request->NotifyModuleWaitFinished();
    }
    return true;
  });

  // When the pref of multiple import maps is enabled, for preloaded and fetched
  // module scripts, the module graphs have been cleared in
  // ClearPreloadedModuleGraph.
  if (!multiImportMapsEnabled) {
    // If speculative preloading has added modules to the module map, remove
    // them.
    for (const auto& entry : mFetchedModules) {
      ModuleScript* script = entry.GetData();
      if (script) {
        MOZ_DIAGNOSTIC_ASSERT(
            script->ForPreload(),
            "Non-preload module loads should block import maps");
        MOZ_DIAGNOSTIC_ASSERT(!script->HadImportMap(),
                              "Only one import map can be registered");
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
        if (JSObject* module = script->ModuleRecord()) {
          MOZ_DIAGNOSTIC_ASSERT(!ModuleIsLinked(module));
        }
#endif
        script->Shutdown();
      }
    }
    mFetchedModules.Clear();
  }
}

void ModuleLoaderBase::CopyModulesTo(ModuleLoaderBase* aDest) {
  MOZ_ASSERT(aDest->mFetchingModules.IsEmpty());
  MOZ_ASSERT(aDest->mFetchedModules.IsEmpty());
  MOZ_ASSERT(mFetchingModules.IsEmpty());

  for (const auto& entry : mFetchedModules) {
    RefPtr<ModuleScript> moduleScript = entry.GetData();
    aDest->mFetchedModules.InsertOrUpdate(entry, moduleScript);
  }
}

void ModuleLoaderBase::MoveModulesTo(ModuleLoaderBase* aDest) {
  MOZ_ASSERT(mFetchingModules.IsEmpty());
  MOZ_ASSERT(aDest->mFetchingModules.IsEmpty());

  for (const auto& entry : mFetchedModules) {
    RefPtr<ModuleScript> moduleScript = entry.GetData();

#ifdef DEBUG
    if (auto existingEntry = aDest->mFetchedModules.Lookup(entry)) {
      MOZ_ASSERT(moduleScript == existingEntry.Data());
    }
#endif

    aDest->mFetchedModules.InsertOrUpdate(entry, moduleScript);
  }

  mFetchedModules.Clear();
}

#undef LOG
#undef LOG_ENABLED

}  // namespace JS::loader
