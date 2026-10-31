/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JavaScript modules (as in, the syntactic construct) implementation. */

#include "vm/Modules.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/ScopeExit.h"
#include "mozilla/Utf8.h"  // mozilla::Utf8Unit

#include <stdint.h>  // uint32_t

#include "jstypes.h"  // JS_PUBLIC_API

#include "builtin/JSON.h"  // js::ParseJSONWithReviver
#include "builtin/ModuleObject.h"
#include "builtin/Number.h"  // js::Int32ToAtom
#include "builtin/Promise.h"  // js::CreatePromiseObjectForAsync, js::AsyncFunctionReturned
#include "ds/Sort.h"
#include "frontend/BytecodeCompiler.h"  // js::frontend::CompileModule
#include "frontend/FrontendContext.h"   // js::AutoReportFrontendContext
#include "js/ColumnNumber.h"            // JS::ColumnNumberOneOrigin
#include "js/Context.h"                 // js::AssertHeapIsIdle
#include "js/ErrorReport.h"             // JSErrorBase
#include "js/friend/StackLimits.h"      // js::AutoCheckRecursionLimit
#include "js/Promise.h"     // JS::PromiseState, JS::SetSettledPromiseIsHandled
#include "js/RootingAPI.h"  // JS::MutableHandle
#include "js/Value.h"       // JS::Value
#include "js/WasmModule.h"  // JS::WasmModule
#include "vm/EnvironmentObject.h"  // js::ModuleEnvironmentObject
#include "vm/JSAtomUtils.h"        // AtomizeString
#include "vm/JSContext.h"          // CHECK_THREAD, JSContext
#include "vm/JSObject.h"           // JSObject
#include "vm/JSONParser.h"         // JSONParser
#include "vm/JSScript.h"           // js::ScriptSourceObject
#include "vm/List.h"               // ListObject
#include "vm/Runtime.h"            // JSRuntime
#include "wasm/WasmCompile.h"
#include "wasm/WasmFeatures.h"
#include "wasm/WasmJS.h"

#include "builtin/HandlerFunction-inl.h"  // js::ExtraValueFromHandler, js::NewHandler{,WithExtraValue}, js::TargetFromHandler
#include "vm/JSAtomUtils-inl.h"           // AtomToId
#include "vm/JSContext-inl.h"             // JSContext::{c,releaseC}heck
#include "vm/JSObject-inl.h"
#include "vm/List-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

using mozilla::Utf8Unit;

class DynamicImportContextObject;

using ModuleSet =
    GCHashSet<JSObject*, StableCellHasher<JSObject*>, SystemAllocPolicy>;

static bool ModuleLink(JSContext* cx, Handle<ModuleObject*> module);
static bool ModuleEvaluate(JSContext* cx, Handle<ModuleObject*> module,
                           MutableHandle<Value> rval);
static bool SyntheticModuleEvaluate(JSContext* cx, Handle<ModuleObject*> module,
                                    MutableHandle<Value> rval);
static bool ContinueModuleLoading(JSContext* cx,
                                  Handle<GraphLoadingStateRecordObject*> state,
                                  Handle<ModuleObject*> moduleCompletion,
                                  ImportPhase phase, Handle<Value> error);
static bool TryStartDynamicModuleImport(JSContext* cx, HandleScript script,
                                        HandleValue specifierArg,
                                        HandleValue optionsArg,
                                        HandleObject promise,
                                        ImportPhase phase);
static bool ContinueDynamicImport(JSContext* cx,
                                  Handle<PromiseObject*> promiseCapability,
                                  Handle<ModuleObject*> module,
                                  ImportPhase phase, bool usePromise);
static bool LinkAndEvaluateDynamicImport(JSContext* cx, unsigned argc,
                                         Value* vp);
static bool LinkAndEvaluateDynamicImport(
    JSContext* cx, Handle<DynamicImportContextObject*> context);

static bool DynamicImportResolved(JSContext* cx,
                                  Handle<DynamicImportContextObject*> context,
                                  Handle<PromiseObject*> promise);
static bool DynamicImportResolved(JSContext* cx, unsigned argc, Value* vp);
static bool DynamicImportRejected(JSContext* cx, unsigned argc, Value* vp);

static bool GatherAsynchronousTransitiveDependencies(
    JSContext* cx, Handle<ModuleObject*> module, MutableHandle<ModuleSet> seen,
    MutableHandle<ModuleVector> result);

////////////////////////////////////////////////////////////////////////////////
// Public API

JS_PUBLIC_API JS::ModuleLoadHook JS::GetModuleLoadHook(JSRuntime* rt) {
  AssertHeapIsIdle();

  return rt->moduleLoadHook;
}

JS_PUBLIC_API void JS::SetModuleLoadHook(JSRuntime* rt, ModuleLoadHook func) {
  AssertHeapIsIdle();

  rt->moduleLoadHook = func;
}

JS_PUBLIC_API JS::ModuleMetadataHook JS::GetModuleMetadataHook(JSRuntime* rt) {
  AssertHeapIsIdle();

  return rt->moduleMetadataHook;
}

JS_PUBLIC_API void JS::SetModuleMetadataHook(JSRuntime* rt,
                                             ModuleMetadataHook func) {
  AssertHeapIsIdle();

  rt->moduleMetadataHook = func;
}

// Get the module object of the enclosing scopes of a dynamic import.
static ModuleObject* GetEnclosingModule(JSScript* script) {
  // Only valid while |script| is running: bodyScope() reads the script's
  // immutable data, which relazification discards.
  MOZ_ASSERT(script->hasBytecode());

  for (Scope* scope = script->bodyScope(); scope; scope = scope->enclosing()) {
    if (scope->is<ModuleScope>()) {
      return scope->as<ModuleScope>().module();
    }
  }

  return nullptr;
}

// Get the module record from a |referrer| value built by
// ReferrerValueForScript, or nullptr if the referrer is not a module.
static ModuleObject* ReferrerModuleOrNull(Handle<Value> referrer) {
  if (!referrer.isObject()) {
    return nullptr;
  }

  JSObject* object = &referrer.toObject();
  if (!object->is<ModuleObject>()) {
    return nullptr;
  }

  return &object->as<ModuleObject>();
}

// Build the referrer value from |script|, and the returned JS::Value is passed
// to HostLoadImportedModule hook.
// |script| could be a function script inside a module script, in which case
// script.isModule() is false. To determine if |script| is from a module script,
// we walk out to the enclosing module scope to check that, if it is, then the
// ObjectValue of the ModuleObject is returned. Otherwise the original private
// value of the |script| is returned.
static Value ReferrerValueForScript(JSScript* script) {
  MOZ_ASSERT(script, "a dynamic import call always has a running script");

  if (ModuleObject* module = GetEnclosingModule(script)) {
    return ObjectValue(*module);
  }

  return script->sourceObject()->getPrivate();
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
JS_PUBLIC_API bool JS::FinishLoadingImportedModule(
    JSContext* cx, Handle<Value> referrer, Handle<JSObject*> moduleRequest,
    Handle<Value> payload, Handle<JSObject*> result, bool usePromise) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(referrer, moduleRequest, payload, result);

  MOZ_ASSERT(moduleRequest->is<ModuleRequestObject>());
  MOZ_ASSERT(moduleRequest->as<ModuleRequestObject>().moduleType() !=
             JS::ModuleType::Unknown);
  MOZ_ASSERT(result);
  Rooted<ModuleObject*> module(cx, &result->as<ModuleObject>());

  // TODO: Until we support evaluation phase imports of wasm modules, we need to
  // guard against first importing a wasm module as source, and then
  // subsequently as evaluation phase. The module will be retrieved from the
  // module map, and then we'll attempt to link it, which isn't currently
  // supported. See Bug 2030454.
  if (moduleRequest->as<ModuleRequestObject>().phase() ==
          ImportPhase::Evaluation &&
      module->moduleSource()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_WASM_ESM_EVAL_NOT_SUPPORTED);
    return FinishLoadingImportedModuleFailedWithPendingException(cx, payload);
  }

  // |loadedModules| is only required to be stored on modules.
  Rooted<ModuleObject*> referrerModule(cx, ReferrerModuleOrNull(referrer));
  if (referrerModule) {
    // Step 1. If result is a normal completion, then
    // Step 1.a. If referrer.[[LoadedModules]] contains a Record whose
    //           [[Specifier]] is specifier, then
    LoadedModuleMap& loadedModules = referrerModule->loadedModules();
    if (auto record = loadedModules.lookup(moduleRequest)) {
      //  Step 1.a.i. Assert: That Record's [[Module]] is result.[[Value]].
      MOZ_ASSERT(record->value() == module);
    } else {
      // Step 1.b. Else, append the Record { moduleRequest.[[Specifer]],
      //           [[Attributes]]: moduleRequest.[[Attributes]],
      //           [[Module]]: result.[[Value]] } to referrer.[[LoadedModules]].
      if (!loadedModules.putNew(moduleRequest, module)) {
        ReportOutOfMemory(cx);
        return FinishLoadingImportedModuleFailedWithPendingException(cx,
                                                                     payload);
      }
    }
  }

  // Step 2. If payload is a GraphLoadingState Record, then
  // Step 2.a. Perform ContinueModuleLoading(payload, result).
  JSObject* object = &payload.toObject();
  if (object->is<GraphLoadingStateRecordObject>()) {
    Rooted<GraphLoadingStateRecordObject*> state(cx);
    state = &object->as<GraphLoadingStateRecordObject>();
    return ContinueModuleLoading(
        cx, state, module, moduleRequest->as<ModuleRequestObject>().phase(),
        UndefinedHandleValue);
  }

  // Step 3. Else,
  // Step 3.a. Perform ContinueDynamicImport(payload, result).
  MOZ_ASSERT(object->is<PromiseObject>());
  Rooted<PromiseObject*> promise(cx, &object->as<PromiseObject>());
  return ContinueDynamicImport(cx, promise, module,
                               moduleRequest->as<ModuleRequestObject>().phase(),
                               usePromise);
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
// Failure path where |result| is a throw completion, supplied as |error|.
JS_PUBLIC_API bool JS::FinishLoadingImportedModuleFailed(
    JSContext* cx, Handle<Value> payloadArg, Handle<Value> error) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(payloadArg, error);
  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  // Step 2. If payload is a GraphLoadingState Record, then
  // Step 2.a. Perform ContinueModuleLoading(payload, result).
  JSObject* payload = &payloadArg.toObject();
  if (payload->is<GraphLoadingStateRecordObject>()) {
    Rooted<GraphLoadingStateRecordObject*> state(cx);
    state = &payload->as<GraphLoadingStateRecordObject>();
    return ContinueModuleLoading(cx, state, nullptr, ImportPhase::Evaluation,
                                 error);
  }

  // Step 3. Else,
  // Step 3.a. Perform ContinueDynamicImport(payload, result).
  // ContinueDynamicImport:
  // Step 1. If moduleCompletion is an abrupt completion, then
  // Step 1. a. Perform ! Call(promiseCapability.[[Reject]], undefined,
  //            moduleCompletion.[[Value]]).
  Rooted<PromiseObject*> promise(cx, &payload->as<PromiseObject>());
  return PromiseObject::reject(cx, promise, error);
}

// https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
// Failure path where |result| is a throw completion, set as the pending
// exception.
JS_PUBLIC_API bool JS::FinishLoadingImportedModuleFailedWithPendingException(
    JSContext* cx, Handle<Value> payload) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(payload);
  MOZ_ASSERT(JS_IsExceptionPending(cx));

  RootedValue error(cx);
  if (!cx->getPendingException(&error)) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    MOZ_ALWAYS_TRUE(cx->getPendingException(&error));
  }
  cx->clearPendingException();

  return FinishLoadingImportedModuleFailed(cx, payload, error);
}

template <typename Unit>
static JSObject* CompileModuleHelper(JSContext* cx,
                                     const JS::ReadOnlyCompileOptions& options,
                                     JS::SourceText<Unit>& srcBuf) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  JS::Rooted<JSObject*> mod(cx);
  {
    AutoReportFrontendContext fc(cx);
    mod = frontend::CompileModule(cx, &fc, options, srcBuf);
  }
  return mod;
}

JS_PUBLIC_API JSObject* JS::CompileModule(JSContext* cx,
                                          const ReadOnlyCompileOptions& options,
                                          SourceText<char16_t>& srcBuf) {
  return CompileModuleHelper(cx, options, srcBuf);
}

JS_PUBLIC_API JSObject* JS::CompileModule(JSContext* cx,
                                          const ReadOnlyCompileOptions& options,
                                          SourceText<Utf8Unit>& srcBuf) {
  return CompileModuleHelper(cx, options, srcBuf);
}

JS_PUBLIC_API JSObject* JS::CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf) {
  size_t length = srcBuf.length();
  auto chars =
      UniqueTwoByteChars(UTF8CharsToNewTwoByteCharsZ(
                             cx, JS::UTF8Chars(srcBuf.get(), srcBuf.length()),
                             &length, js::MallocArena)
                             .get());
  if (!chars) {
    return nullptr;
  }

  JS::SourceText<char16_t> source;
  if (!source.init(cx, std::move(chars), length)) {
    return nullptr;
  }

  return CompileJsonModule(cx, options, source);
}

JS_PUBLIC_API JSObject* JS::CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  auto charRange =
      mozilla::Range<const char16_t>(srcBuf.get(), srcBuf.length());
  Rooted<JSONParser<char16_t>> parser(
      cx, cx, charRange, JSONParser<char16_t>::ParseType::JSONParse);

  parser.reportLineNumbersFromParsedData(true);
  parser.setFilename(options.filename());

  JS::RootedValue jsonValue(cx);
  if (!parser.parse(&jsonValue)) {
    return nullptr;
  }

  return CreateDefaultExportSyntheticModule(cx, jsonValue);
}

JS_PUBLIC_API JSObject* JS::CreateDefaultExportSyntheticModule(
    JSContext* cx, Handle<Value> defaultExport) {
  CHECK_THREAD(cx);
  cx->check(defaultExport);

  Rooted<ExportNameVector> exportNames(cx);
  if (!exportNames.append(cx->names().default_)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<ModuleObject*> moduleObject(
      cx, ModuleObject::createSynthetic(cx, &exportNames));
  if (!moduleObject) {
    return nullptr;
  }

  RootedVector<Value> exportValues(cx);
  if (!exportValues.append(defaultExport)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  if (!ModuleObject::createSyntheticEnvironment(cx, moduleObject,
                                                exportValues)) {
    return nullptr;
  }

  return moduleObject;
}

// https://webassembly.github.io/esm-integration/js-api/index.html#esm-integration
JS_PUBLIC_API JSObject* JS::CompileWasmModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    js::Vector<uint8_t, 0, js::MallocAllocPolicy>& srcBuf) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  if (!wasm::HasSupport(cx)) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                             JSMSG_WASM_COMPILE_ERROR,
                             "WebAssembly is not supported");
    return nullptr;
  }

  SharedWasmCompileArgs compileArgs = BuildCompileArgsForESM(cx, options);
  if (!compileArgs) {
    return nullptr;
  }

  ESMCompileResult compileResult =
      CompileForESM(*compileArgs, srcBuf.begin(), srcBuf.length());

  RootedObject wasmModuleObject(cx);
  if (!FinishCompileForESM(cx, *compileArgs, compileResult,
                           &wasmModuleObject)) {
    return nullptr;
  }

  return CreateWasmModule(cx, wasmModuleObject);
}

static bool GetWasmExportNameAtoms(
    JSContext* cx, Handle<JSObject*> wasmModuleObject,
    MutableHandle<ExportNameVector> exportNames) {
  MOZ_ASSERT(exportNames.empty());

  JS::RootedVector<JSString*> exportNameStrings(cx);
  if (!wasm::GetModuleExportNames(cx, wasmModuleObject, &exportNameStrings)) {
    return false;
  }

  if (!exportNames.reserve(exportNameStrings.length())) {
    ReportOutOfMemory(cx);
    return false;
  }

  Rooted<JSAtom*> exportName(cx);
  for (JSString* exportNameString : exportNameStrings) {
    exportName = AtomizeString(cx, exportNameString);
    if (!exportName) {
      return false;
    }

    exportNames.infallibleAppend(exportName);
  }

  return true;
}

JS_PUBLIC_API JSObject* JS::CreateWasmModule(
    JSContext* cx, Handle<JSObject*> wasmModuleObject) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);

  Rooted<ModuleObject*> moduleObject(cx, ModuleObject::create(cx));
  if (!moduleObject) {
    return nullptr;
  }

  moduleObject->initModuleSourceSlot(wasmModuleObject);

  Rooted<ScriptSourceObject*> sso(cx,
                                  ScriptSourceObject::createForWasmModule(cx));
  if (!sso) {
    return nullptr;
  }
  moduleObject->initScriptSourceObject(sso);

  // https://webassembly.github.io/esm-integration/js-api/index.html#module-declaration-environment-setup
  // Step 1. Let record be this WebAssembly Module Record.
  // Step 2. Let env be NewModuleEnvironment(null).
  // Step 3. Set record.[[Environment]] to env.
  // Step 4. For each name in the export name list of record,
  // Step 4.1. Perform ! env.CreateImmutableBinding(name, true).
  //
  // The specification does this in InitializeEnvironment, but we create the
  // environment here, as we already do for JavaScript and synthetic modules.
  // Module namespace bindings are resolved eagerly while linking, which
  // requires the environment of an imported module to exist, and in a cycle
  // linking can reach an importer before InitializeEnvironment has run for
  // this record.
  Rooted<ExportNameVector> exportNames(cx);
  if (!GetWasmExportNameAtoms(cx, wasmModuleObject, &exportNames)) {
    return nullptr;
  }

  if (!ModuleObject::createWasmEnvironment(cx, moduleObject, exportNames)) {
    return nullptr;
  }

  JS::RootedVector<std::pair<JSString*, JSString*>> wasmImports(cx);
  if (!wasm::GetModuleImports(cx, wasmModuleObject, &wasmImports)) {
    return nullptr;
  }

  Rooted<RequestedModuleVector> requestedModuleRecords(cx);
  Rooted<ImportEntryVector> importEntries(cx);
  if (!requestedModuleRecords.reserve(wasmImports.length()) ||
      !importEntries.reserve(wasmImports.length())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<JSAtom*> specifier(cx);
  Rooted<JSAtom*> importName(cx);
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  for (const auto& wasmImport : wasmImports.get()) {
    specifier = &wasmImport.first->asAtom();
    moduleRequest = nullptr;
    for (const RequestedModule& request : requestedModuleRecords.get()) {
      if (request.moduleRequest()->specifier() == specifier) {
        moduleRequest = request.moduleRequest();
        break;
      }
    }

    if (!moduleRequest) {
      moduleRequest = ModuleRequestObject::create(
          cx, specifier, JS::ModuleType::JavaScriptOrWasm,
          ImportPhase::Evaluation);
      if (!moduleRequest) {
        return nullptr;
      }

      requestedModuleRecords.infallibleEmplaceBack(
          moduleRequest, RequestedModule::WasmDefaultLineNumber,
          JS::ColumnNumberOneOrigin());
    }

    importName = &wasmImport.second->asAtom();
    importEntries.infallibleEmplaceBack(
        moduleRequest, importName, importName, ImportNameValueType::String,
        RequestedModule::WasmDefaultLineNumber, JS::ColumnNumberOneOrigin());
  }

  Rooted<ListObject*> asyncParentModulesList(cx, ListObject::create(cx));
  if (!asyncParentModulesList) {
    return nullptr;
  }

  moduleObject->initAsyncSlots(cx, /* hasTopLevelAwait = */ false,
                               asyncParentModulesList);

  Rooted<ExportEntryVector> exportEntries(cx);
  moduleObject->initImportExportData(&requestedModuleRecords, &importEntries,
                                     &exportEntries, 0, 0, 0);

  if (!ModuleObject::Freeze(cx, moduleObject)) {
    return nullptr;
  }

  return moduleObject;
}

JS_PUBLIC_API void JS::SetModulePrivate(JSObject* module, const Value& value) {
  JSRuntime* rt = module->zone()->runtimeFromMainThread();
  module->as<ModuleObject>().scriptSourceObject()->setPrivate(rt, value);
}

JS_PUBLIC_API void JS::ClearModulePrivate(JSObject* module) {
  // |module| may be gray, be careful not to create edges to it.
  JSRuntime* rt = module->zone()->runtimeFromMainThread();
  module->as<ModuleObject>().scriptSourceObject()->clearPrivate(rt);
}

JS_PUBLIC_API JS::Value JS::GetModulePrivate(JSObject* module) {
  return module->as<ModuleObject>().scriptSourceObject()->getPrivate();
}

JS_PUBLIC_API bool JS::IsCyclicModule(JSObject* module) {
  return module->as<ModuleObject>().hasCyclicModuleFields();
}

#ifdef DEBUG
JS_PUBLIC_API void JS::SetModulePreload(JSObject* module, bool isPreload) {
  MOZ_ASSERT(module->is<ModuleObject>());
  module->as<ModuleObject>().setPreload(isPreload);
}
#endif

JS_PUBLIC_API void JS::ResetPreloadedModule(JSObject* module) {
  MOZ_RELEASE_ASSERT(!ModuleIsLinked(module));
  MOZ_ASSERT(module->is<ModuleObject>());

  auto& moduleObj = module->as<ModuleObject>();
  if (!moduleObj.hasCyclicModuleFields()) {
    return;
  }
  MOZ_ASSERT(moduleObj.isPreload());
  moduleObj.setStatus(ModuleStatus::New);
  moduleObj.loadedModules().clear();
}

JS_PUBLIC_API bool JS::ModuleLink(JSContext* cx, Handle<JSObject*> moduleArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg);

  return ::ModuleLink(cx, moduleArg.as<ModuleObject>());
}

JS_PUBLIC_API bool JS::LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> moduleArg, Handle<Value> hostDefined,
    JS::LoadModuleResolvedCallback resolved,
    JS::LoadModuleRejectedCallback rejected) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg, hostDefined);

  return js::LoadRequestedModules(cx, moduleArg.as<ModuleObject>(), hostDefined,
                                  resolved, rejected);
}

JS_PUBLIC_API bool JS::LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> moduleArg, Handle<Value> hostDefined,
    MutableHandle<JSObject*> promiseOut) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleArg, hostDefined);

  return js::LoadRequestedModules(cx, moduleArg.as<ModuleObject>(), hostDefined,
                                  promiseOut);
}

JS_PUBLIC_API bool JS::ModuleEvaluate(JSContext* cx,
                                      Handle<JSObject*> moduleRecord,
                                      MutableHandle<JS::Value> rval) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(moduleRecord);

  cx->isEvaluatingModule++;
  auto guard = mozilla::MakeScopeExit([cx] {
    MOZ_ASSERT(cx->isEvaluatingModule != 0);
    cx->isEvaluatingModule--;
  });

  if (moduleRecord.as<ModuleObject>()->hasSyntheticModuleFields()) {
    return SyntheticModuleEvaluate(cx, moduleRecord.as<ModuleObject>(), rval);
  }

  return ::ModuleEvaluate(cx, moduleRecord.as<ModuleObject>(), rval);
}

JS_PUBLIC_API bool JS::ThrowOnModuleEvaluationFailure(
    JSContext* cx, Handle<JSObject*> evaluationPromise,
    ModuleErrorBehaviour errorBehaviour) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->releaseCheck(evaluationPromise);

  return OnModuleEvaluationFailure(cx, evaluationPromise, errorBehaviour);
}

JS_PUBLIC_API JS::ModuleType JS::GetRequestedModuleType(
    JSContext* cx, Handle<JSObject*> moduleRecord, uint32_t index) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);

  auto& module = moduleRecord->as<ModuleObject>();
  return module.requestedModules()[index].moduleRequest()->moduleType();
}

JS_PUBLIC_API JSScript* JS::GetModuleScript(JS::HandleObject moduleRecord) {
  AssertHeapIsIdle();

  auto& module = moduleRecord->as<ModuleObject>();

  // Synthetic modules and wasm modules do not have a script.
  if (module.hasSyntheticModuleFields() || module.isWasmModule()) {
    return nullptr;
  }

  return module.script();
}

JS_PUBLIC_API JS::Value JS::GetReferrerPrivate(Handle<Value> referrer) {
  AssertHeapIsIdle();

  if (ModuleObject* module = ReferrerModuleOrNull(referrer)) {
    return module->scriptSourceObject()->getPrivate();
  }

  // Here the |referrer| is the script private returned from
  // ReferrerValueForScript.
  return referrer;
}

JS_PUBLIC_API JSObject* JS::GetModuleObject(HandleScript moduleScript) {
  AssertHeapIsIdle();
  MOZ_ASSERT(moduleScript->isModule());

  return moduleScript->module();
}

JS_PUBLIC_API JSObject* JS::GetModuleNamespace(JSContext* cx,
                                               HandleObject moduleRecord) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRecord);
  MOZ_ASSERT(moduleRecord->is<ModuleObject>());

  return GetOrCreateModuleNamespace(cx, moduleRecord.as<ModuleObject>());
}

JS_PUBLIC_API JSObject* JS::GetModuleForNamespace(
    JSContext* cx, HandleObject moduleNamespace) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleNamespace);
  MOZ_ASSERT(moduleNamespace->is<ModuleNamespaceObject>());

  return &moduleNamespace->as<ModuleNamespaceObject>().module();
}

JS_PUBLIC_API JSObject* JS::GetModuleEnvironment(JSContext* cx,
                                                 Handle<JSObject*> moduleObj) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleObj);
  MOZ_ASSERT(moduleObj->is<ModuleObject>());

  return moduleObj->as<ModuleObject>().environment();
}

JS_PUBLIC_API JSString* JS::GetModuleRequestSpecifier(
    JSContext* cx, Handle<JSObject*> moduleRequestArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRequestArg);

  return moduleRequestArg->as<ModuleRequestObject>().specifier();
}

JS_PUBLIC_API JS::ModuleType JS::GetModuleRequestType(
    JSContext* cx, Handle<JSObject*> moduleRequestArg) {
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  cx->check(moduleRequestArg);

  return moduleRequestArg->as<ModuleRequestObject>().moduleType();
}

JS_PUBLIC_API bool JS::ModuleRequestIsSourcePhase(JSObject* moduleRequestArg) {
  AssertHeapIsIdle();

  return moduleRequestArg->as<ModuleRequestObject>().phase() ==
         ImportPhase::Source;
}

JS_PUBLIC_API void JS::ClearModuleEnvironment(JSObject* moduleObj) {
  MOZ_ASSERT(moduleObj);
  AssertHeapIsIdle();

  js::ModuleEnvironmentObject* env =
      moduleObj->as<js::ModuleObject>().environment();
  if (!env) {
    return;
  }

  const JSClass* clasp = env->getClass();
  uint32_t numReserved = JSCLASS_RESERVED_SLOTS(clasp);
  uint32_t numSlots = env->slotSpan();
  for (uint32_t i = numReserved; i < numSlots; i++) {
    env->setSlot(i, UndefinedValue());
  }
}

JS_PUBLIC_API bool JS::ModuleIsLinked(JSObject* moduleObj) {
  AssertHeapIsIdle();
  return moduleObj->as<ModuleObject>().status() != ModuleStatus::New &&
         moduleObj->as<ModuleObject>().status() != ModuleStatus::Unlinked;
}

////////////////////////////////////////////////////////////////////////////////
// Internal implementation

class ResolveSetEntry {
  ModuleObject* module_;
  JSAtom* exportName_;

 public:
  ResolveSetEntry(ModuleObject* module, JSAtom* exportName)
      : module_(module), exportName_(exportName) {}

  ModuleObject* module() const { return module_; }
  JSAtom* exportName() const { return exportName_; }

  void trace(JSTracer* trc) {
    TraceRoot(trc, &module_, "ResolveSetEntry::module_");
    TraceRoot(trc, &exportName_, "ResolveSetEntry::exportName_");
  }
};

using ResolveSet = GCVector<ResolveSetEntry, 0, SystemAllocPolicy>;

static bool CyclicModuleResolveExport(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      Handle<JSAtom*> exportName,
                                      MutableHandle<ResolveSet> resolveSet,
                                      MutableHandle<Value> result,
                                      ModuleErrorInfo* errorInfoOut = nullptr);
static bool SyntheticModuleResolveExport(JSContext* cx,
                                         Handle<ModuleObject*> module,
                                         Handle<JSAtom*> exportName,
                                         MutableHandle<Value> result,
                                         ModuleErrorInfo* errorInfoOut);
#ifdef NIGHTLY_BUILD
static bool WasmModuleResolveExport(JSContext* cx, Handle<ModuleObject*> module,
                                    Handle<JSAtom*> exportName,
                                    MutableHandle<Value> result,
                                    ModuleErrorInfo* errorInfoOut);
#endif
static ModuleNamespaceObject* ModuleNamespaceCreate(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<UniquePtr<ExportNameVector>> exports,
    ImportPhase phase = ImportPhase::Evaluation);
static bool InnerModuleLinking(JSContext* cx, Handle<ModuleObject*> module,
                               MutableHandle<ModuleVector> stack, size_t index,
                               size_t* indexOut);
static bool InnerModuleEvaluation(JSContext* cx, Handle<ModuleObject*> module,
                                  MutableHandle<ModuleVector> stack,
                                  size_t index, size_t* indexOut);
static bool ExecuteAsyncModule(JSContext* cx, Handle<ModuleObject*> module);
static bool GatherAvailableModuleAncestors(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleVector> execList);

static const char* ModuleStatusName(ModuleStatus status) {
  switch (status) {
    case ModuleStatus::New:
      return "New";
    case ModuleStatus::Unlinked:
      return "Unlinked";
    case ModuleStatus::Linking:
      return "Linking";
    case ModuleStatus::Linked:
      return "Linked";
    case ModuleStatus::Evaluating:
      return "Evaluating";
    case ModuleStatus::EvaluatingAsync:
      return "EvaluatingAsync";
    case ModuleStatus::Evaluated:
      return "Evaluated";
    default:
      MOZ_CRASH("Unexpected ModuleStatus");
  }
}

static bool ContainsElement(const ExportNameVector& list, JSAtom* atom) {
  for (JSAtom* a : list) {
    if (a == atom) {
      return true;
    }
  }

  return false;
}

static bool ContainsElement(Handle<ModuleVector> stack, ModuleObject* module) {
  for (ModuleObject* m : stack) {
    if (m == module) {
      return true;
    }
  }

  return false;
}

// Append |module| to |list| if it's not already present, using |set| to track
// if |module| exists already.
static bool AppendUniqueModule(JSContext* cx, MutableHandle<ModuleVector> list,
                               MutableHandle<ModuleSet> set,
                               ModuleObject* module) {
  auto ptr = set.lookupForAdd(module);
  if (ptr) {
    return true;
  }

  if (!set.add(ptr, module) || !list.append(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

#ifdef DEBUG
static size_t CountElements(Handle<ModuleVector> stack, ModuleObject* module) {
  size_t count = 0;
  for (ModuleObject* m : stack) {
    if (m == module) {
      count++;
    }
  }

  return count;
}
#endif

// https://tc39.es/proposal-json-modules/#sec-smr-getexportednames
static bool SyntheticModuleGetExportedNames(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ExportNameVector> exportedNames) {
  MOZ_ASSERT(exportedNames.empty());

  if (!exportedNames.appendAll(module->syntheticExportNames())) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

#ifdef NIGHTLY_BUILD
static bool WasmModuleGetExportedNames(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ExportNameVector> exportedNames) {
  MOZ_ASSERT(exportedNames.empty());

  Rooted<JSObject*> wasmModuleObject(cx, module->moduleSource());
  return GetWasmExportNameAtoms(cx, wasmModuleObject, exportedNames);
}
#endif

// https://tc39.es/ecma262/#sec-GetImportedModule
ModuleObject* js::GetImportedModule(
    JSContext* cx, Handle<ModuleObject*> referrer,
    Handle<ModuleRequestObject*> moduleRequest) {
  MOZ_ASSERT(referrer);
  MOZ_ASSERT(moduleRequest);

  // Step 1. Assert: Exactly one element of referrer.[[LoadedModules]] is a
  //         Record whose [[Specifier]] is specifier, since LoadRequestedModules
  //         has completed successfully on referrer prior to invoking this
  //         abstract operation.
  //
  //         Impl note: Updated to perform lookup using ModuleRequestObject as
  //         per the Import Attributes Proposal.
  auto record = referrer->loadedModules().lookup(moduleRequest);
  MOZ_ASSERT(record);

  // Step 2. Let record be the Record in referrer.[[LoadedModules]] whose
  //         [[Specifier]] is specifier.
  // Step 3. Return record.[[Module]].
  return record->value();
}

// Export star default proposal:
// https://tc39.es/proposal-export-star-default/
//
// When enabled, `export * from "mod"` may also provide mod's default export.
static bool ExportStarDefaultEnabled() {
#ifdef NIGHTLY_BUILD
  return JS::Prefs::experimental_export_star_default();
#else
  return false;
#endif
}

// https://tc39.es/ecma262/#sec-getexportednames
// ES2023 16.2.1.6.2 GetExportedNames
static bool ModuleGetExportedNames(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleSet> exportStarSet,
    MutableHandle<ExportNameVector> exportedNames) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 4. Let exportedNames be a new empty List.
  MOZ_ASSERT(exportedNames.empty());

  if (module->hasSyntheticModuleFields()) {
    return SyntheticModuleGetExportedNames(cx, module, exportedNames);
  }

#ifdef NIGHTLY_BUILD
  if (module->isWasmModule()) {
    return WasmModuleGetExportedNames(cx, module, exportedNames);
  }
#endif

  // Step 2. If exportStarSet contains module, then:
  if (exportStarSet.has(module)) {
    // Step 2.a. We've reached the starting point of an export * circularity.
    // Step 2.b. Return a new empty List.
    return true;
  }

  // Step 3. Append module to exportStarSet.
  if (!exportStarSet.put(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. For each ExportEntry Record e of module.[[LocalExportEntries]], do:
  for (const ExportEntry& e : module->localExportEntries()) {
    // Step 5.a. Assert: module provides the direct binding for this export.
    // Step 5.b. Append e.[[ExportName]] to exportedNames.
    if (!exportedNames.append(e.exportName())) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  // Step 6. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 6.a. Assert: module imports a specific binding for this export.
    // Step 6.b. Append e.[[ExportName]] to exportedNames.
    if (!exportedNames.append(e.exportName())) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  // Step 7. For each ExportEntry Record e of module.[[StarExportEntries]], do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> requestedModule(cx);
  Rooted<JSAtom*> name(cx);
  for (const ExportEntry& e : module->starExportEntries()) {
    // Step 7.a. Let requestedModule be ? GetImportedModule(module,
    //           e.[[ModuleRequest]]).
    moduleRequest = e.moduleRequest();
    requestedModule = GetImportedModule(cx, module, moduleRequest);
    if (!requestedModule) {
      return false;
    }
    MOZ_ASSERT(requestedModule->status() >= ModuleStatus::Unlinked);

    // Step 7.b. Let starNames be ?
    //           requestedModule.GetExportedNames(exportStarSet).
    Rooted<ExportNameVector> starNames(cx);
    if (!ModuleGetExportedNames(cx, requestedModule, exportStarSet,
                                &starNames)) {
      return false;
    }

    // Step 7.c. For each element n of starNames, do:
    for (JSAtom* name : starNames) {
      // Step 7.c.i. If SameValue(n, "default") is false, then:

      // Export star default proposal
      // https://tc39.es/proposal-export-star-default/#sec-getexportednames
      //
      // This step is deleted, so "default" is no longer excluded here.
      // (see ExportStarDefaultEnabled())
      if (ExportStarDefaultEnabled() || name != cx->names().default_) {
        // Step 7.c.i.1. If n is not an element of exportedNames, then:
        if (!ContainsElement(exportedNames, name)) {
          // Step 7.c.i.1.a. Append n to exportedNames.
          if (!exportedNames.append(name)) {
            ReportOutOfMemory(cx);
            return false;
          }
        }
      }
    }
  }

  // Step 8. Return exportedNames.
  return true;
}

static void ThrowUnexpectedModuleStatus(JSContext* cx, ModuleStatus status) {
  JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                           JSMSG_BAD_MODULE_STATUS, ModuleStatusName(status));
}

// https://tc39.es/ecma262/#sec-HostLoadImportedModule
bool js::HostLoadImportedModule(JSContext* cx, Handle<Value> referrer,
                                Handle<JSObject*> moduleRequest,
                                Handle<Value> hostDefined,
                                Handle<Value> payload, uint32_t lineNumber,
                                JS::ColumnNumberOneOrigin columnNumber) {
  MOZ_ASSERT(moduleRequest);
  MOZ_ASSERT(payload.isObject());
  cx->releaseCheck(referrer, moduleRequest, hostDefined, payload);

  // TODO: The modules system doesn't support passing a wrapped promise from
  // another compartment, which can happen when this is called from
  // ShadowRealmImportValue.
  MOZ_RELEASE_ASSERT(payload.toObject().is<GraphLoadingStateRecordObject>() ||
                     payload.toObject().is<PromiseObject>());

  JS::ModuleLoadHook moduleLoadHook = cx->runtime()->moduleLoadHook;
  if (!moduleLoadHook) {
    JS_ReportErrorASCII(cx, "Module load hook not set");
    return false;
  }

  bool ok = moduleLoadHook(cx, referrer, moduleRequest, hostDefined, payload,
                           lineNumber, columnNumber);

  if (!ok) {
    MOZ_ASSERT(JS_IsExceptionPending(cx));
    if (JS_IsExceptionPending(cx)) {
      return JS::FinishLoadingImportedModuleFailedWithPendingException(cx,
                                                                       payload);
    }

    return JS::FinishLoadingImportedModuleFailed(cx, payload,
                                                 UndefinedHandleValue);
  }

  return true;
}

// https://tc39.es/ecma262/#table-abstract-methods-of-module-records
// Abstract Method 'ResolveExport(exportName, resolveSet)'
static bool ModuleResolveExportWithResolveSet(
    JSContext* cx, Handle<ModuleObject*> module, Handle<JSAtom*> exportName,
    MutableHandle<ResolveSet> resolveSet, MutableHandle<Value> result,
    ModuleErrorInfo* errorInfoOut = nullptr) {
  if (module->hasSyntheticModuleFields()) {
    return SyntheticModuleResolveExport(cx, module, exportName, result,
                                        errorInfoOut);
  }

#ifdef NIGHTLY_BUILD
  if (module->isWasmModule()) {
    return WasmModuleResolveExport(cx, module, exportName, result,
                                   errorInfoOut);
  }
#endif

  // https://tc39.es/ecma262/#sec-resolveexport
  // Step 1. Assert: module.[[Status]] is not new.
  MOZ_ASSERT(module->status() != ModuleStatus::New);
  return CyclicModuleResolveExport(cx, module, exportName, resolveSet, result,
                                   errorInfoOut);
}

// https://tc39.es/ecma262/#table-abstract-methods-of-module-records
// Abstract Method 'ResolveExport(exportName)'
bool js::ModuleResolveExport(JSContext* cx, Handle<ModuleObject*> module,
                             Handle<JSAtom*> exportName,
                             MutableHandle<Value> result,
                             ModuleErrorInfo* errorInfoOut) {
  // https://tc39.es/ecma262/#sec-resolveexport
  // Step 2. If resolveSet is not present, set resolveSet to a new empty List.
  Rooted<ResolveSet> resolveSet(cx);
  return ModuleResolveExportWithResolveSet(cx, module, exportName, &resolveSet,
                                           result, errorInfoOut);
}

static bool CreateResolvedBindingObject(JSContext* cx,
                                        Handle<ModuleObject*> module,
                                        Handle<JSAtom*> bindingName,
                                        MutableHandle<Value> result) {
  ResolvedBindingObject* obj =
      ResolvedBindingObject::create(cx, module, bindingName);
  if (!obj) {
    return false;
  }

  result.setObject(*obj);
  return true;
}

// https://tc39.es/ecma262/#sec-resolveexport
// Source Text Module Record: ResolveExport
//
// Returns an value describing the location of the resolved export or indicating
// a failure.
//
// On success this returns a resolved binding record: { module, bindingName }
//
// There are two failure cases:
//
//  - If no definition was found or the request is found to be circular, *null*
//    is returned.
//
//  - If the request is found to be ambiguous, the string `"ambiguous"` is
//    returned.
static bool CyclicModuleResolveExport(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      Handle<JSAtom*> exportName,
                                      MutableHandle<ResolveSet> resolveSet,
                                      MutableHandle<Value> result,
                                      ModuleErrorInfo* errorInfoOut) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 2. For each Record { [[Module]], [[ExportName]] } r of resolveSet, do:
  for (const auto& entry : resolveSet) {
    // Step 3.a. If module and r.[[Module]] are the same Module Record and
    //           exportName is r.[[ExportName]], then:
    if (entry.module() == module && entry.exportName() == exportName) {
      // Step 3.a.i. Assert: This is a circular import request.
      // Step 3.a.ii. Return null.
      //
      // Note: null here does not necessarily indicate a resolution failure.
      // The caller may still find a concrete binding via another star path, or
      // detect ambiguity.
      result.setNull();
      if (errorInfoOut) {
        errorInfoOut->setCircularImport(module);
      }
      return true;
    }
  }

  // Step 4. Append the Record { [[Module]]: module, [[ExportName]]: exportName
  //         } to resolveSet.
  if (!resolveSet.emplaceBack(module, exportName)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. For each ExportEntry Record e of module.[[LocalExportEntries]], do:
  for (const ExportEntry& e : module->localExportEntries()) {
    // Step 5.a. If e.[[ExportName]] is exportName, then:
    if (exportName == e.exportName()) {
      // Step 5.a.i. Assert: module provides the direct binding for this export.
      // Step 5.a.ii. Return ResolvedBinding Record { [[Module]]: module,
      //              [[BindingName]]: e.[[LocalName]] }.
      Rooted<JSAtom*> localName(cx, e.localName());
      return CreateResolvedBindingObject(cx, module, localName, result);
    }
  }

  // Step 6. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> name(cx);
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 6.a. If e.[[ExportName]] is exportName, then:
    if (exportName == e.exportName()) {
      // Step 6.a.i. Assert: e.[[ModuleRequest]] is not null.
      MOZ_ASSERT(e.moduleRequest());

      // Step 6.a.ii. Let importedModule be ? GetImportedModule(module,
      //              e.[[ModuleRequest]]).
      moduleRequest = e.moduleRequest();
      importedModule = GetImportedModule(cx, module, moduleRequest);
      if (!importedModule) {
        return false;
      }
      MOZ_ASSERT(importedModule->status() >= ModuleStatus::Unlinked);

      // Step 6.a.iii. If e.[[ImportName]] is ~namespace~, then:
      if (e.importNameValueType() == ImportNameValueType::Namespace) {
        // Step 6.a.iii.1. Assert: module does not provide the direct binding
        //                 for this export.
        // Step 6.a.iii.2. If e.[[ModuleRequest]].[[Phase]] is defer, then
        //                 Return ResolvedBinding Record { [[Module]]:
        //                 importedModule, [[BindingName]]: DEFERRED-NAMESPACE
        //                 }.
        if (moduleRequest->phase() == ImportPhase::Deferred) {
          name = cx->names().star_deferred_namespace_star_;
        } else {
          // Step 6.a.iii.3. Else,
          //   Assert: e.[[Phase]] is evaluation.
          MOZ_ASSERT(moduleRequest->phase() == ImportPhase::Evaluation);
          //   Return ResolvedBinding Record {
          //     [[Module]]: importedModule, [[BindingName]]: NAMESPACE }.
          name = cx->names().star_namespace_star_;
        }
        return CreateResolvedBindingObject(cx, importedModule, name, result);
      } else {
        name = e.importName();
        // Step 6.a.iv.1. Assert: module imports a specific binding for this
        //                export.
        // Step 6.a.iv.2. Return ? importedModule.ResolveExport(e.[[ImportName]]
        //                , resolveSet).
        return ModuleResolveExportWithResolveSet(
            cx, importedModule, name, resolveSet, result, errorInfoOut);
      }
    }
  }

  // Step 7. If exportName is "default"), then:

  // Export star default proposal
  // https://tc39.es/proposal-export-star-default/#sec-resolveexport
  //
  // This entire step is deleted, so "default" falls through to the
  // star-export search in steps below like any other name.
  // (see ExportStarDefaultEnabled())
  if (!ExportStarDefaultEnabled() && exportName == cx->names().default_) {
    // Step 7.a. Assert: A default export was not explicitly defined by this
    //           module.
    // Step 7.b. Return null.
    // Step 7.c. NOTE: A default export cannot be provided by an export * from
    //           "mod" declaration.
    result.setNull();
    if (errorInfoOut) {
      errorInfoOut->setImportedModule(module);
    }
    return true;
  }

  // Step 8. Let starResolution be null.
  Rooted<ResolvedBindingObject*> starResolution(cx);
  bool hadCircular = false;

  // Step 9. For each ExportEntry Record e of module.[[StarExportEntries]], do:
  Rooted<Value> resolution(cx);
  Rooted<ResolvedBindingObject*> binding(cx);
  for (const ExportEntry& e : module->starExportEntries()) {
    // Step 9.a. Assert: e.[[ModuleRequest]] is not null.
    MOZ_ASSERT(e.moduleRequest());

    // Step 9.b. Let importedModule be ? GetImportedModule(module,
    //           e.[[ModuleRequest]]).
    moduleRequest = e.moduleRequest();
    importedModule = GetImportedModule(cx, module, moduleRequest);
    if (!importedModule) {
      return false;
    }
    MOZ_ASSERT(importedModule->status() >= ModuleStatus::Unlinked);

    // Step 9.c. Let resolution be ? importedModule.ResolveExport(exportName,
    //           resolveSet).
    //
    // Use a separate local ModuleErrorInfo so that a circular or ambiguous
    // result from one star path does not prematurely update the caller's error
    // information. We copy the relevant fields to errorInfoOut only once we
    // decide to use this resolution (step 9.d) or at step 10.
    ModuleErrorInfo localErrorInfo{e.lineNumber(), e.columnNumber()};
    if (!ModuleResolveExportWithResolveSet(cx, importedModule, exportName,
                                           resolveSet, &resolution,
                                           &localErrorInfo)) {
      return false;
    }

    // Step 9.d. If resolution is AMBIGUOUS, return AMBIGUOUS.
    if (resolution == StringValue(cx->names().ambiguous)) {
      result.set(resolution);
      if (errorInfoOut) {
        errorInfoOut->imported = localErrorInfo.imported;
        errorInfoOut->entry1 = localErrorInfo.entry1;
        errorInfoOut->entry2 = localErrorInfo.entry2;
      }
      return true;
    }

    if (resolution.isNull() && localErrorInfo.isCircular) {
      hadCircular = true;
    }

    // Step 9.e. If resolution is not null, then:
    if (!resolution.isNull()) {
      // Step 9.e.i. Assert: resolution is a ResolvedBinding Record.
      binding = &resolution.toObject().as<ResolvedBindingObject>();

      // Step 9.e.ii. If starResolution is null, set starResolution to
      // resolution.
      if (!starResolution) {
        starResolution = binding;
      } else {
        // Step 9.e.iii. Else:
        // Step 9.e.iii.1. Assert: There is more than one * import that includes
        //                 the requested name.
        // Step 9.e.iii.2. If resolution.[[Module]] and
        //                 starResolution.[[Module]] are not the same Module
        //                 Record, return AMBIGUOUS.
        // Step 9.e.iii.3. If resolution.[[BindingName]] is not
        //                 starResolution.[[BindingName]] and either
        //                 resolution.[[BindingName]] or
        //                 starResolution.[[BindingName]] is namespace, return
        //                 AMBIGUOUS.
        // Step 9.e.iii.4. If resolution.[[BindingName]] is a String,
        //                 starResolution.[[BindingName]] is a String, and
        //                 resolution.[[BindingName]] is not
        //                 starResolution.[[BindingName]]), return AMBIGUOUS.
        if (binding->module() != starResolution->module() ||
            binding->bindingName() != starResolution->bindingName()) {
          result.setString(cx->names().ambiguous);

          if (errorInfoOut) {
            ModuleObject* module1 = starResolution->module();
            ModuleObject* module2 = binding->module();
            errorInfoOut->setForAmbiguousImport(module, module1, module2);
          }
          return true;
        }
      }
    }
  }

  // Step 10. Return starResolution.
  result.setObjectOrNull(starResolution);
  if (!starResolution && errorInfoOut) {
    if (hadCircular) {
      errorInfoOut->setCircularImport(module);
    } else {
      errorInfoOut->setImportedModule(module);
    }
  }
  return true;
}

// https://tc39.es/proposal-json-modules/#sec-smr-resolveexport
static bool SyntheticModuleResolveExport(JSContext* cx,
                                         Handle<ModuleObject*> module,
                                         Handle<JSAtom*> exportName,
                                         MutableHandle<Value> result,
                                         ModuleErrorInfo* errorInfoOut) {
  // Step 2. If module.[[ExportNames]] does not contain exportName, return null.
  if (!ContainsElement(module->syntheticExportNames(), exportName)) {
    result.setNull();
    if (errorInfoOut) {
      errorInfoOut->setImportedModule(module);
    }
    return true;
  }

  // Step 3. Return ResolvedBinding Record { [[Module]]: module,
  // [[BindingName]]: exportName }.
  return CreateResolvedBindingObject(cx, module, exportName, result);
}

#ifdef NIGHTLY_BUILD
static bool WasmModuleResolveExport(JSContext* cx, Handle<ModuleObject*> module,
                                    Handle<JSAtom*> exportName,
                                    MutableHandle<Value> result,
                                    ModuleErrorInfo* errorInfoOut) {
  Rooted<ExportNameVector> exportNames(cx);
  if (!WasmModuleGetExportedNames(cx, module, &exportNames)) {
    return false;
  }

  if (!ContainsElement(exportNames.get(), exportName)) {
    result.setNull();
    if (errorInfoOut) {
      errorInfoOut->setImportedModule(module);
    }
    return true;
  }

  return CreateResolvedBindingObject(cx, module, exportName, result);
}
#endif

// https://tc39.es/ecma262/#sec-getmodulenamespace
// https://tc39.es/proposal-defer-import-eval/#sec-getmodulenamespace
ModuleNamespaceObject* js::GetOrCreateModuleNamespace(
    JSContext* cx, Handle<ModuleObject*> module,
    ImportPhase phase /* = ImportPhase::Evaluation */) {
  MOZ_ASSERT(phase == ImportPhase::Evaluation ||
             phase == ImportPhase::Deferred);

  // Step 1. Assert: If module is a Cyclic Module Record, then module.[[Status]]
  //         is not new or unlinked.
  MOZ_ASSERT(module->status() != ModuleStatus::New &&
             module->status() != ModuleStatus::Unlinked);

  // Step 2. If phase is defer, then let namespace be
  //         module.[[DeferredNamespace]]. Otherwise, let namespace be
  //         module.[[Namespace]].
  Rooted<ModuleNamespaceObject*> ns(cx, phase == ImportPhase::Deferred
                                            ? module->maybeDeferredNamespace()
                                            : module->namespace_());

  // Step 3. If namespace is empty, then:
  if (!ns) {
    // Step 3.a. Let exportedNames be ? module.GetExportedNames().
    Rooted<ModuleSet> exportStarSet(cx);
    Rooted<ExportNameVector> exportedNames(cx);
    if (!ModuleGetExportedNames(cx, module, &exportStarSet, &exportedNames)) {
      return nullptr;
    }

    // Step 3.b. Let unambiguousNames be a new empty List.
    Rooted<UniquePtr<ExportNameVector>> unambiguousNames(
        cx, cx->make_unique<ExportNameVector>());
    if (!unambiguousNames) {
      return nullptr;
    }

    // Step 3.c. For each element name of exportedNames, do:
    Rooted<JSAtom*> name(cx);
    Rooted<Value> resolution(cx);
    for (JSAtom* atom : exportedNames) {
      name = atom;

      // Step 3.c.i. If phase is not defer or name is not "then", then:
      if (phase != ImportPhase::Deferred || name != cx->names().then) {
        // Let resolution be ? module.ResolveExport(name).
        if (!ModuleResolveExport(cx, module, name, &resolution)) {
          return nullptr;
        }

        // Step 3.c.ii. If resolution is a ResolvedBinding Record, append name
        //              to unambiguousNames.
        if (resolution.isObject() && !unambiguousNames->append(name)) {
          ReportOutOfMemory(cx);
          return nullptr;
        }
      }
    }

    // Step 3.d. Set namespace to ModuleNamespaceCreate(module,
    //           unambiguousNames, phase).
    ns = ModuleNamespaceCreate(cx, module, &unambiguousNames, phase);
  }

  // Step 4. Return namespace.
  return ns;
}

bool js::IsResolvedBinding(JSContext* cx, Handle<Value> resolution) {
  MOZ_ASSERT(resolution.isObjectOrNull() ||
             resolution.toString() == cx->names().ambiguous);
  return resolution.isObject();
}

static void InitNamespaceOrSourceBinding(JSContext* cx,
                                         ModuleEnvironmentObject* env,
                                         JSAtom* name, const Value& obj) {
  // The property already exists in the evironment but is not writable, so set
  // the slot directly.
  mozilla::Maybe<PropertyInfo> prop = env->lookup(cx, AtomToId(name));
  MOZ_ASSERT(prop.isSome());
  env->setSlot(prop->slot(), obj);
}

static bool ComputeNamespaceBindings(JSContext* cx,
                                     Handle<ModuleObject*> module,
                                     Handle<ModuleNamespaceObject*> ns) {
  Rooted<JSAtom*> name(cx);
  Rooted<Value> resolution(cx);
  Rooted<ResolvedBindingObject*> binding(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> bindingName(cx);
  for (JSAtom* atom : ns->exports()) {
    name = atom;

    if (!ModuleResolveExport(cx, module, name, &resolution)) {
      return false;
    }

    MOZ_ASSERT(IsResolvedBinding(cx, resolution));
    binding = &resolution.toObject().as<ResolvedBindingObject>();
    importedModule = binding->module();
    bindingName = binding->bindingName();
    if (!ns->addBinding(cx, name, importedModule, bindingName)) {
      return false;
    }
  }

  return true;
}

struct AtomComparator {
  bool operator()(JSAtom* a, JSAtom* b, bool* lessOrEqualp) {
    int32_t result = CompareStrings(a, b);
    *lessOrEqualp = (result <= 0);
    return true;
  }
};

// https://tc39.es/ecma262/#sec-modulenamespacecreate
// https://tc39.es/proposal-defer-import-eval/#sec-modulenamespacecreate
static ModuleNamespaceObject* ModuleNamespaceCreate(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<UniquePtr<ExportNameVector>> exports, ImportPhase phase) {
  // Step 6. Let sortedExports be a List whose elements are the elements of
  //         exports ordered as if an Array of the same values had been sorted
  //         using %Array.prototype.sort% using undefined as comparefn.
  ExportNameVector scratch;
  if (!scratch.resize(exports->length())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  MOZ_ALWAYS_TRUE(MergeSort(exports->begin(), exports->length(),
                            scratch.begin(), AtomComparator()));

  // Step 9 - 10.
  Rooted<ModuleNamespaceObject*> ns(
      cx, ModuleObject::createNamespace(cx, module, exports, phase));
  if (!ns) {
    return nullptr;
  }

  // Pre-compute all binding mappings now instead of on each access.
  if (!ComputeNamespaceBindings(cx, module, ns)) {
    module->clearNamespaceOnFailure(phase);
    return nullptr;
  }

  // Step 12. Return M.
  return ns;
}

void ModuleErrorInfo::setImportedModule(ModuleObject* importedModule) {
  imported = importedModule->filename();
}

void ModuleErrorInfo::setCircularImport(ModuleObject* importedModule) {
  setImportedModule(importedModule);
  isCircular = true;
}

void ModuleErrorInfo::setForAmbiguousImport(ModuleObject* importedModule,
                                            ModuleObject* module1,
                                            ModuleObject* module2) {
  setImportedModule(importedModule);
  entry1 = module1->filename();
  entry2 = module2->filename();
}

static void CreateErrorNumberMessageUTF8(JSContext* cx, unsigned errorNumber,
                                         JSErrorReport* reportOut, ...) {
  va_list ap;
  va_start(ap, reportOut);
  AutoReportFrontendContext fc(cx);
  if (!ExpandErrorArgumentsVA(&fc, GetErrorMessage, nullptr, errorNumber,
                              ArgumentsAreUTF8, reportOut, ap)) {
    ReportOutOfMemory(cx);
    return;
  }

  va_end(ap);
}

static void ThrowResolutionError(JSContext* cx, Handle<ModuleObject*> module,
                                 Handle<Value> resolution, Handle<JSAtom*> name,
                                 ModuleErrorInfo* errorInfo) {
  MOZ_ASSERT(errorInfo);
  auto chars = StringToNewUTF8CharsZ(cx, *name);
  if (!chars) {
    ReportOutOfMemory(cx);
    return;
  }

  bool isAmbiguous = resolution == StringValue(cx->names().ambiguous);

  unsigned errorNumber;
  if (errorInfo->isCircular) {
    errorNumber = JSMSG_MODULE_CIRCULAR_IMPORT;
  } else if (isAmbiguous) {
    errorNumber = JSMSG_MODULE_AMBIGUOUS;
  } else {
    errorNumber = JSMSG_MODULE_NO_EXPORT;
  }

  JSErrorReport report;
  report.isWarning_ = false;
  report.errorNumber = errorNumber;

  if (errorNumber == JSMSG_MODULE_AMBIGUOUS) {
    CreateErrorNumberMessageUTF8(cx, errorNumber, &report, errorInfo->imported,
                                 chars.get(), errorInfo->entry1,
                                 errorInfo->entry2);
  } else {
    CreateErrorNumberMessageUTF8(cx, errorNumber, &report, errorInfo->imported,
                                 chars.get());
  }

  Rooted<JSString*> message(cx, report.newMessageString(cx));
  if (!message) {
    ReportOutOfMemory(cx);
    return;
  }

  const char* file = module->filename();
  RootedString filename(
      cx, JS_NewStringCopyUTF8Z(cx, JS::ConstUTF8CharsZ(file, strlen(file))));
  if (!filename) {
    ReportOutOfMemory(cx);
    return;
  }

  RootedValue error(cx);
  if (!JS::CreateError(cx, JSEXN_SYNTAXERR, nullptr, filename,
                       errorInfo->lineNumber, errorInfo->columnNumber, nullptr,
                       message, JS::NothingHandleValue, &error)) {
    ReportOutOfMemory(cx);
    return;
  }

  cx->setPendingException(error, nullptr);
}

// https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment
// https://tc39.es/proposal-source-phase-imports/#sec-source-text-module-record-initialize-environment
// ES2023 16.2.1.6.4 InitializeEnvironment
static bool ModuleInitializeEnvironment(JSContext* cx,
                                        Handle<ModuleObject*> module) {
  MOZ_ASSERT(module->status() == ModuleStatus::Linking);

  // Step 1. For each ExportEntry Record e of module.[[IndirectExportEntries]],
  //         do:
  Rooted<JSAtom*> exportName(cx);
  Rooted<Value> resolution(cx);
  Rooted<ResolvedBindingObject*> binding(cx);
  Rooted<JSAtom*> bindingName(cx);
  Rooted<ModuleObject*> bindingModule(cx);
  Rooted<ModuleNamespaceObject*> bindingNs(cx);
  for (const ExportEntry& e : module->indirectExportEntries()) {
    // Step 1.a. Assert: e.[[ExportName]] is not null.
    MOZ_ASSERT(e.exportName());

    // Step 1.b. Let resolution be ? module.ResolveExport(e.[[ExportName]]).
    exportName = e.exportName();
    ModuleErrorInfo errorInfo{e.lineNumber(), e.columnNumber()};
    if (!ModuleResolveExport(cx, module, exportName, &resolution, &errorInfo)) {
      return false;
    }

    // Step 1.c. If resolution is either null or AMBIGUOUS, throw a SyntaxError
    //           exception.
    if (!IsResolvedBinding(cx, resolution)) {
      ThrowResolutionError(cx, module, resolution, exportName, &errorInfo);
      return false;
    }

    binding = &resolution.toObject().as<ResolvedBindingObject>();
    bindingName = binding->bindingName();

    // https://tc39.es/ecma262/#sec-module-namespace-exotic-objects-get-p-receiver
    // ES2023 10.4.6.8 Module Namespace Exotic Object [[Get]]
    if (bindingName == cx->names().star_namespace_star_ ||
        bindingName == cx->names().star_deferred_namespace_star_) {
      bindingModule = binding->module();
      ImportPhase phase = bindingName == cx->names().star_namespace_star_
                              ? ImportPhase::Evaluation
                              : ImportPhase::Deferred;
      bindingNs = GetOrCreateModuleNamespace(cx, bindingModule, phase);
      if (!bindingNs) {
        return false;
      }

      // The spec uses an immutable binding here but we have already generated
      // bytecode for an indirect binding. Instead, use an indirect binding to
      // "*namespace*" slot of the environment.
      Rooted<ModuleEnvironmentObject*> env(
          cx, &bindingModule->initialEnvironment());
      InitNamespaceOrSourceBinding(cx, env, bindingName,
                                   ObjectValue(*bindingNs));
    }
  }

  // Step 5. Let env be NewModuleEnvironment(realm.[[GlobalEnv]]).
  // Step 6. Set module.[[Environment]] to env.
  // Note that we have already created the environment by this point.
  Rooted<ModuleEnvironmentObject*> env(cx, &module->initialEnvironment());

  // Step 7. For each ImportEntry Record in of module.[[ImportEntries]], do:
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> importName(cx);
  Rooted<JSAtom*> localName(cx);
  Rooted<ModuleObject*> sourceModule(cx);
  for (const ImportEntry& in : module->importEntries()) {
    // Step 7.a. Let importedModule be ! GetImportedModule(module,
    //           in.[[ModuleRequest]]).
    moduleRequest = in.moduleRequest();
    importedModule = GetImportedModule(cx, module, moduleRequest);
    if (!importedModule) {
      return false;
    }
    MOZ_ASSERT(importedModule->status() >= ModuleStatus::Linking ||
               moduleRequest->phase() == ImportPhase::Source);

    localName = in.localName();
    importName = in.importName();

    // Step 7.b. If in.[[ImportName]] is ~namespace~, then:
    if (in.importNameValueType() == ImportNameValueType::Namespace) {
      // Step 7.b.i. Let namespace be ? GetModuleNamespace(importedModule).
      ModuleNamespaceObject* ns = GetOrCreateModuleNamespace(
          cx, importedModule, moduleRequest->phase());
      if (!ns) {
        return false;
      }

      // Step 7.b.ii. Perform ! env.CreateImmutableBinding(in.[[LocalName]],
      // true). This happens when the environment is created.

      // Step 7.b.iii. Perform ! env.InitializeBinding(in.[[LocalName]],
      // namespace).
      InitNamespaceOrSourceBinding(cx, env, localName, ObjectValue(*ns));
    } else if (in.importNameValueType() == ImportNameValueType::Source) {
      // https://tc39.es/ecma262/#sec-source-text-module-record-initialize-environment
      // Step 7.c. Else if in.[[ImportName]] is ~source~, then
      // Step 7.c.i. Let moduleSourceObject be importedModule.[[ModuleSource]].
      JSObject* moduleSourceObject = importedModule->moduleSource();

      // Step 7.c.ii. If moduleSourceObject is empty, throw a SyntaxError
      //              exception.
      if (!moduleSourceObject) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_MODULE_SOURCE_NOT_AVAILABLE);
        return false;
      }

      // Step 7.c.iii. Perform ! env.CreateImmutableBinding(in.[[LocalName]],
      //               true). This happens when the environment is created.

      // Step 7.c.iv. Perform ! env.InitializeBinding(in.[[LocalName]],
      //              moduleSourceObject).
      InitNamespaceOrSourceBinding(cx, env, localName,
                                   ObjectValue(*moduleSourceObject));
    } else {
      // Step 7.d. Else:
      // Step 7.d.i. Assert: in.[[ImportName]] is a String.
      MOZ_ASSERT(importName &&
                 in.importNameValueType() == ImportNameValueType::String);

      // Step 7.d.ii. Let resolution be ?
      // importedModule.ResolveExport(in.[[ImportName]]).
      ModuleErrorInfo errorInfo{in.lineNumber(), in.columnNumber()};
      if (!ModuleResolveExport(cx, importedModule, importName, &resolution,
                               &errorInfo)) {
        return false;
      }

      // Step 7.d.iii. If resolution is null or ambiguous, throw a SyntaxError
      //              exception.
      if (!IsResolvedBinding(cx, resolution)) {
        ThrowResolutionError(cx, module, resolution, importName, &errorInfo);
        return false;
      }

      auto* binding = &resolution.toObject().as<ResolvedBindingObject>();
      sourceModule = binding->module();
      bindingName = binding->bindingName();

      // Step 7.d.iv. If resolution.[[BindingName]] is namespace or
      // deferred-namespace, then:
      if (bindingName == cx->names().star_namespace_star_ ||
          bindingName == cx->names().star_deferred_namespace_star_) {
        // Step.7.d.iv.1. If resolution.[[BindingName]] is namespace let phase
        // be evaluation, else let phase be defer.
        ImportPhase phase = bindingName == cx->names().star_namespace_star_
                                ? ImportPhase::Evaluation
                                : ImportPhase::Deferred;
        // Step 7.d.iv.2. Let namespace be ?
        //                 GetModuleNamespace(resolution.[[Module]]).
        Rooted<ModuleNamespaceObject*> ns(
            cx, GetOrCreateModuleNamespace(cx, sourceModule, phase));
        if (!ns) {
          return false;
        }

        // Step 7.d.iv.2. Perform !
        //                env.CreateImmutableBinding(in.[[LocalName]], true).
        // Step 7.d.iv.3. Perform ! env.InitializeBinding(in.[[LocalName]],
        //                namespace).
        //
        // This should be InitNamespaceBinding, but we have already generated
        // bytecode assuming an indirect binding. Instead, ensure a special
        // "*namespace*"" binding exists on the target module's environment. We
        // then generate an indirect binding to this synthetic binding.
        InitNamespaceOrSourceBinding(cx, &sourceModule->initialEnvironment(),
                                     bindingName, ObjectValue(*ns));
        if (!env->createImportBinding(cx, localName, sourceModule,
                                      bindingName)) {
          return false;
        }
      } else {
        // Step 7.d.v. Else:
        // Step 7.d.v.1. Perform env.CreateImportBinding(in.[[LocalName]],
        //               resolution.[[Module]], resolution.[[BindingName]]).
        if (!env->createImportBinding(cx, localName, sourceModule,
                                      bindingName)) {
          return false;
        }
      }
    }
  }

  // Steps 8-26.
  //
  // Some of these do not need to happen for practical purposes. For steps
  // 21-23, the bindings that can be handled in a similar way to regulars
  // scripts are done separately. Function Declarations are special due to
  // hoisting and are handled within this function. See ModuleScope and
  // ModuleEnvironmentObject for further details.

  // Step 24. For each element d of lexDeclarations, do:
  // Step 24.a. For each element dn of the BoundNames of d, do:
  // Step 24.a.iii. If d is a FunctionDeclaration, a GeneratorDeclaration, an
  //                AsyncFunctionDeclaration, or an AsyncGeneratorDeclaration,
  //                then:
  // Step 24.a.iii.1 Let fo be InstantiateFunctionObject of d with arguments env
  //                 and privateEnv.
  // Step 24.a.iii.2. Perform ! env.InitializeBinding(dn, fo).
  return ModuleObject::instantiateFunctionDeclarations(cx, module);
}

#ifdef NIGHTLY_BUILD
// https://webassembly.github.io/esm-integration/js-api/index.html#module-declaration-environment-setup
static bool WasmModuleInitializeEnvironment(JSContext* cx,
                                            Handle<ModuleObject*> module) {
  MOZ_ASSERT(module->status() == ModuleStatus::Linking);

  // The environment was created already by CreateWasmModule.
  // Here we just validate that the imports resolve correctly.
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  Rooted<ModuleObject*> importedModule(cx);
  Rooted<JSAtom*> importName(cx);
  Rooted<Value> resolution(cx);
  for (const ImportEntry& in : module->importEntries()) {
    moduleRequest = in.moduleRequest();
    importedModule = GetImportedModule(cx, module, moduleRequest);
    if (!importedModule) {
      return false;
    }

    importName = in.importName();
    ModuleErrorInfo errorInfo{in.lineNumber(), in.columnNumber()};
    if (!ModuleResolveExport(cx, importedModule, importName, &resolution,
                             &errorInfo)) {
      return false;
    }

    if (!IsResolvedBinding(cx, resolution)) {
      ThrowResolutionError(cx, module, resolution, importName, &errorInfo);
      return false;
    }
  }

  return true;
}
#endif

// Reject the load with the pending exception instead of unwinding out of it.
// ContinueModuleLoading sets state.[[IsLoading]] to false and calls the state
// record's rejected handler.
static bool FailWithPendingException(
    JSContext* cx, Handle<GraphLoadingStateRecordObject*> state) {
  JS::ExceptionStack exnStack(cx);
  if (!JS::StealPendingExceptionStack(cx, &exnStack)) {
    return false;
  }

  return ContinueModuleLoading(cx, state, nullptr, ImportPhase::Evaluation,
                               exnStack.exception());
}

static bool FailWithUnsupportedAttributeException(
    JSContext* cx, Handle<GraphLoadingStateRecordObject*> state,
    Handle<ModuleRequestObject*> moduleRequest) {
  UniqueChars printableKey = AtomToPrintableString(
      cx, moduleRequest->getFirstUnsupportedAttributeKey());
  JS_ReportErrorNumberASCII(
      cx, GetErrorMessage, nullptr,
      JSMSG_IMPORT_ATTRIBUTES_STATIC_IMPORT_UNSUPPORTED_ATTRIBUTE,
      printableKey ? printableKey.get() : "");

  return FailWithPendingException(cx, state);
}

// https://tc39.es/proposal-source-phase-imports/#sec-InnerModuleLoading
// InnerModuleLoading ( state, module, loadType )
enum class LoadType { Single, RecursiveLoad };
static bool InnerModuleLoading(JSContext* cx,
                               Handle<GraphLoadingStateRecordObject*> state,
                               Handle<ModuleObject*> module,
                               LoadType loadType) {
  MOZ_ASSERT(state);
  MOZ_ASSERT(module);

  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return FailWithPendingException(cx, state);
  }

  // Step 1. Assert: state.[[IsLoading]] is true.
  MOZ_ASSERT(state->isLoading());

  // Step 2. If loadType is recursive-load, module is a Cyclic Module Record,
  //         module.[[Status]] is new, and state.[[Visited]] does not contain
  //         module, then
  if (loadType == LoadType::RecursiveLoad && module->hasCyclicModuleFields() &&
      module->status() == ModuleStatus::New && !state->visited().has(module)) {
    // Step 2.a. Append module to state.[[Visited]].
    if (!state->visited().putNew(module)) {
      ReportOutOfMemory(cx);
      return FailWithPendingException(cx, state);
    }

    // Step 2.b. Let requestedModulesCount be the number of elements in
    //           module.[[RequestedModules]].
    size_t requestedModulesCount = module->requestedModules().Length();

    // Step 2.c. Set state.[[PendingModulesCount]] to
    //           state.[[PendingModulesCount]] + requestedModulesCount.
    uint32_t count = state->pendingModulesCount() + requestedModulesCount;
    state->setPendingModulesCount(count);

    // Step 2.d For each ModuleRequest Record required of
    //          module.[[RequestedModules]], do
    Rooted<ModuleRequestObject*> moduleRequest(cx);
    Rooted<ModuleObject*> recordModule(cx);
    Rooted<JSAtom*> invalidKey(cx);
    for (const RequestedModule& request : module->requestedModules()) {
      moduleRequest = request.moduleRequest();
      // https://tc39.es/proposal-import-attributes/#sec-InnerModuleLoading
      if (moduleRequest->hasFirstUnsupportedAttributeKey()) {
        if (!FailWithUnsupportedAttributeException(cx, state, moduleRequest)) {
          return false;
        }
      } else if (auto record = module->loadedModules().lookup(moduleRequest)) {
        // Step 2.d.i. If module.[[LoadedModules]] contains a Record whose
        //             [[Specifier]] is required, then
        // Step 2.d.i.1. Let record be that Record.
        // Step 2.d.i.2 If required.[[Phase]] is source, let innerLoadType
        //              be single; else let innerLoadType be recursive-load.
        LoadType innerLoadType = moduleRequest->phase() == ImportPhase::Source
                                     ? LoadType::Single
                                     : LoadType::RecursiveLoad;
        // Step 2.d.i.3. Perform InnerModuleLoading(state, record.[[Module]]).
        recordModule = record->value();
        if (!InnerModuleLoading(cx, state, recordModule, innerLoadType)) {
          return false;
        }
      } else {
        // Step 2.d.ii. Else,
        // Step 2.d.ii.1. Perform HostLoadImportedModule(module, required,
        //                state.[[HostDefined]], state).
        Rooted<Value> referrer(cx, ObjectValue(*module));
        Rooted<Value> hostDefined(cx, state->hostDefined());
        Rooted<Value> payload(cx, ObjectValue(*state));
        if (!HostLoadImportedModule(cx, referrer, moduleRequest, hostDefined,
                                    payload, request.lineNumber(),
                                    request.columnNumber())) {
          return false;
        }
      }

      // Step 2.d.iii. If state.[[IsLoading]] is false, return unused.
      if (!state->isLoading()) {
        return true;
      }
    }
  }

  // Step 3. Assert: state.[[PendingModulesCount]] ≥ 1.
  MOZ_ASSERT(state->pendingModulesCount() >= 1);

  // Step 4. Set state.[[PendingModulesCount]] to
  //         state.[[PendingModulesCount]] - 1.
  uint32_t count = state->pendingModulesCount() - 1;
  state->setPendingModulesCount(count);

  // Step 5. If state.[[PendingModulesCount]] = 0, then
  if (state->pendingModulesCount() == 0) {
    // Step 5.a. Set state.[[IsLoading]] to false.
    state->setIsLoading(false);

    // Step 5.b. For each Cyclic Module Record loaded of state.[[Visited]], do
    for (auto iter = state->visited().iter(); !iter.done(); iter.next()) {
      ModuleObject* loaded = &iter.get()->as<ModuleObject>();
      // Step 5.b.i. If loaded.[[Status]] is new, set loaded.[[Status]] to
      // unlinked.
      if (loaded->status() == ModuleStatus::New) {
        loaded->setStatus(ModuleStatus::Unlinked);
      }
    }

    // Step 5.c. Perform ! Call(state.[[PromiseCapability]].[[Resolve]],
    //                          undefined, « undefined »).
    RootedValue hostDefined(cx, state->hostDefined());
    if (!state->resolved(cx, hostDefined)) {
      return false;
    }
  }

  // Step 6. Return unused.
  return true;
}

// https://tc39.es/proposal-source-phase-imports/#sec-ContinueModuleLoading
// ContinueModuleLoading ( state, phase, moduleCompletion )
static bool ContinueModuleLoading(JSContext* cx,
                                  Handle<GraphLoadingStateRecordObject*> state,
                                  Handle<ModuleObject*> moduleCompletion,
                                  ImportPhase phase, Handle<Value> error) {
  MOZ_ASSERT_IF(moduleCompletion, error.isUndefined());
  MOZ_ASSERT(phase < ImportPhase::Limit);

  // Step 1. If state.[[IsLoading]] is false, return unused.
  if (!state->isLoading()) {
    return true;
  }

  // Step 2. If moduleCompletion is a normal completion, then
  if (moduleCompletion) {
    // Step 2.a. If phase is source, let loadType be single;
    //           otherwise let loadType be recursive-load.
    LoadType loadType = phase == ImportPhase::Source ? LoadType::Single
                                                     : LoadType::RecursiveLoad;
    // Step 2.b. Perform InnerModuleLoading(state, moduleCompletion.[[Value]],
    //                                      loadType).
    return InnerModuleLoading(cx, state, moduleCompletion, loadType);
  }

  // Step 3. Else,
  // Step 3.a. Set state.[[IsLoading]] to false.
  state->setIsLoading(false);

  // Step 3.b. Perform ! Call(state.[[PromiseCapability]].[[Reject]],
  // undefined, « moduleCompletion.[[Value]] »).
  RootedValue hostDefined(cx, state->hostDefined());
  return state->rejected(cx, hostDefined, error);
}

// https://tc39.es/ecma262/#sec-LoadRequestedModules
bool js::LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                              Handle<Value> hostDefined,
                              JS::LoadModuleResolvedCallback resolved,
                              JS::LoadModuleRejectedCallback rejected) {
  if (module->hasSyntheticModuleFields()) {
    // Step 1. Return ! PromiseResolve(%Promise%, undefined).
    return resolved(cx, hostDefined);
  }

  // Step 1. If hostDefined is not present, let hostDefined be empty.
  // Step 2. Let pc be ! NewPromiseCapability(%Promise%).
  // Note: For implementation we use callbacks to notify the results.

  // Step 3. Let state be the GraphLoadingState Record { [[IsLoading]]: true,
  //         [[PendingModulesCount]]: 1, [[Visited]]: « »,
  //         [[PromiseCapability]]: pc, [[HostDefined]]: hostDefined }.
  Rooted<GraphLoadingStateRecordObject*> state(
      cx, GraphLoadingStateRecordObject::create(cx, true, 1, resolved, rejected,
                                                hostDefined));
  if (!state) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Perform InnerModuleLoading(state, module, recursive-load).
  if (!InnerModuleLoading(cx, state, module, LoadType::RecursiveLoad)) {
    // Returning false means the load was abandoned without notifying the
    // caller through |resolved| or |rejected|, i.e. an OOM occurred.
    // Deactivate the state.[[IsLoading]] accordingly.
    state->setIsLoading(false);
    return false;
  }

  return true;
}

bool js::LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                              Handle<Value> hostDefined,
                              MutableHandle<JSObject*> promiseOut) {
  // Step 1. If hostDefined is not present, let hostDefined be empty.
  // Step 2. Let pc be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> pc(cx, CreatePromiseObjectForAsync(cx));
  if (!pc) {
    ReportOutOfMemory(cx);
    return false;
  }

  if (module->hasSyntheticModuleFields()) {
    // Step 1. Return ! PromiseResolve(%Promise%, undefined).
    promiseOut.set(pc);
    return AsyncFunctionReturned(cx, pc, UndefinedHandleValue);
  }

  // Step 3. Let state be the GraphLoadingState Record { [[IsLoading]]: true,
  //         [[PendingModulesCount]]: 1, [[Visited]]: « »,
  //         [[PromiseCapability]]: pc, [[HostDefined]]: hostDefined }.
  Rooted<GraphLoadingStateRecordObject*> state(
      cx, GraphLoadingStateRecordObject::create(cx, true, 1, pc, hostDefined));
  if (!state) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Perform InnerModuleLoading(state, module, recursive-load).
  if (!InnerModuleLoading(cx, state, module, LoadType::RecursiveLoad)) {
    // Returning false means the load was abandoned without notifying the
    // caller through |resolved| or |rejected|, i.e. an OOM occurred.
    // Deactivate the state.[[IsLoading]] accordingly.
    state->setIsLoading(false);
    return false;
  }

  // Step 5. Return pc.[[Promise]].
  promiseOut.set(pc);
  return true;
}

// https://tc39.es/ecma262/#sec-moduledeclarationlinking
// ES2023 16.2.1.5.1 Link
static bool ModuleLink(JSContext* cx, Handle<ModuleObject*> module) {
  if (!module->hasCyclicModuleFields()) {
    return true;
  }

  // Step 1. Assert: module.[[Status]] is one of unlinked, linked,
  //         evaluating-async, or evaluated.
  ModuleStatus status = module->status();
  if (status == ModuleStatus::New || status == ModuleStatus::Linking ||
      status == ModuleStatus::Evaluating) {
    ThrowUnexpectedModuleStatus(cx, status);
    return false;
  }

  // Step 2. Let stack be a new empty List.
  Rooted<ModuleVector> stack(cx);

  // Step 3. Let result be Completion(InnerModuleLinking(module, stack, 0)).
  size_t ignored;
  bool ok = InnerModuleLinking(cx, module, &stack, 0, &ignored);

  // Step 4. If result is an abrupt completion, then:
  if (!ok) {
    // Step 4.a. For each Cyclic Module Record m of stack, do:
    for (ModuleObject* m : stack) {
      // Step 4.a.i. Assert: m.[[Status]] is linking.
      MOZ_ASSERT(m->status() == ModuleStatus::Linking);
      // Step 4.a.ii. Set m.[[Status]] to unlinked.
      m->setStatus(ModuleStatus::Unlinked);
      m->clearDfsAncestorIndex();
    }

    // Step 4.b. Assert: module.[[Status]] is unlinked.
    MOZ_ASSERT(module->status() == ModuleStatus::Unlinked);

    // Step 4.c.
    return false;
  }

  // Step 5. Assert: module.[[Status]] is linked, evaluating-async, or
  //         evaluated.
  MOZ_ASSERT(module->status() == ModuleStatus::Linked ||
             module->status() == ModuleStatus::EvaluatingAsync ||
             module->status() == ModuleStatus::Evaluated);

  // Step 6. Assert: stack is empty.
  MOZ_ASSERT(stack.empty());

  // Step 7. Return unused.
  return true;
}

// https://tc39.es/ecma262/#sec-InnerModuleLinking
// ES2023 16.2.1.5.1.1 InnerModuleLinking
static bool InnerModuleLinking(JSContext* cx, Handle<ModuleObject*> module,
                               MutableHandle<ModuleVector> stack, size_t index,
                               size_t* indexOut) {
  // Step 1. If module is not a Cyclic Module Record, then
  if (!module->hasCyclicModuleFields()) {
    // Step 1.a. Perform ? module.Link().
    // (Skipped as we have already created the environment for these modules).
    // Step 1.b. Return index.
    *indexOut = index;
    return true;
  }

  // Step 2. If module.[[Status]] is linking, linked, evaluating-async, or
  //         evaluated, then:
  if (module->status() == ModuleStatus::Linking ||
      module->status() == ModuleStatus::Linked ||
      module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    // Step 2.a. Return index.
    *indexOut = index;
    return true;
  }

  // Step 3. Assert: module.[[Status]] is unlinked.
  if (module->status() != ModuleStatus::Unlinked) {
    ThrowUnexpectedModuleStatus(cx, module->status());
    return false;
  }

  // Step 8. Append module to stack.
  // Do this before changing the status so that we can recover on failure.
  if (!stack.append(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 4. Set module.[[Status]] to linking.
  module->setStatus(ModuleStatus::Linking);

  // Step 5. Let moduleIndex be index.
  size_t moduleIndex = index;

  // Step 6. Set module.[[DFSAncestorIndex]] to index.
  module->setDfsAncestorIndex(index);

  // Step 7. Set index to index + 1.
  index++;

  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 9. For each ModuleRequest Record required that is an element of
  //         module.[[RequestedModules]], do:
  Rooted<ModuleRequestObject*> required(cx);
  Rooted<ModuleObject*> requiredModule(cx);
  for (const RequestedModule& request : module->requestedModules()) {
    required = request.moduleRequest();
    // Source-phase modules don't participate in recursive linking.
    if (required->phase() == ImportPhase::Source) {
      continue;
    }
    // Step 9.a.i. Let requiredModule be ? GetImportedModule(module,
    //             required).
    requiredModule = GetImportedModule(cx, module, required);
    if (!requiredModule) {
      return false;
    }
    MOZ_ASSERT(requiredModule->status() >= ModuleStatus::Unlinked);

    // Step 9.a.ii Set index to ? InnerModuleLinking(requiredModule, stack,
    //             index).
    if (!InnerModuleLinking(cx, requiredModule, stack, index, &index)) {
      return false;
    }

    // Step 9.a.iii If requiredModule is a Cyclic Module Record, then:
    if (requiredModule->hasCyclicModuleFields()) {
      // Step 9.a.iii.1. Assert: requiredModule.[[Status]] is either linking,
      //                 linked, evaluating-async, or evaluated.
      MOZ_ASSERT(requiredModule->status() == ModuleStatus::Linking ||
                 requiredModule->status() == ModuleStatus::Linked ||
                 requiredModule->status() == ModuleStatus::EvaluatingAsync ||
                 requiredModule->status() == ModuleStatus::Evaluated);

      // Step 9.a.iii.2 Assert: requiredModule.[[Status]] is linking if and
      // only if requiredModule is in stack.
      MOZ_ASSERT((requiredModule->status() == ModuleStatus::Linking) ==
                 ContainsElement(stack, requiredModule));

      // Step 9.a.iii.3 If requiredModule.[[Status]] is linking, then:
      if (requiredModule->status() == ModuleStatus::Linking) {
        // Step 9.ia.iii.3.a. Set module.[[DFSAncestorIndex]] to
        //                    min(module.[[DFSAncestorIndex]],
        //                    requiredModule.[[DFSAncestorIndex]]).
        module->setDfsAncestorIndex(std::min(
            module->dfsAncestorIndex(), requiredModule->dfsAncestorIndex()));
      }
    }
  }

  // Step 10. Perform ? module.InitializeEnvironment().
#ifdef NIGHTLY_BUILD
  if (module->isWasmModule()) {
    if (!WasmModuleInitializeEnvironment(cx, module)) {
      return false;
    }
  } else {
    if (!ModuleInitializeEnvironment(cx, module)) {
      return false;
    }
  }
#else
  if (!ModuleInitializeEnvironment(cx, module)) {
    return false;
  }
#endif

  // Step 11. Assert: module occurs exactly once in stack.
  MOZ_ASSERT(CountElements(stack, module) == 1);

  // Step 12. Assert: module.[[DFSAncestorIndex]] <= moduleIndex.
  MOZ_ASSERT(module->dfsAncestorIndex() <= moduleIndex);

  // Step 13. If module.[[DFSAncestorIndex]] = moduleIndex, then
  if (module->dfsAncestorIndex() == moduleIndex) {
    // Step 13.a.
    bool done = false;

    // Step 13.b. Repeat, while done is false:
    while (!done) {
      // Step 13.b.i. Let requiredModule be the last element in stack.
      // Step 13.b.ii. Remove the last element of stack.
      requiredModule = stack.popCopy();

      // Step 13.b.iv. Set requiredModule.[[Status]] to linked.
      requiredModule->setStatus(ModuleStatus::Linked);

      // Step 13.b.v. If requiredModule and module are the same Module Record,
      //              set done to true.
      done = requiredModule == module;
    }
  }

  // Step 14. Return index.
  *indexOut = index;
  return true;
}

static bool SyntheticModuleEvaluate(JSContext* cx,
                                    Handle<ModuleObject*> moduleArg,
                                    MutableHandle<Value> rval) {
  // Steps 1-12 happen elsewhere in the engine.

  // Step 13. Let pc be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> resultPromise(cx, CreatePromiseObjectForAsync(cx));
  if (!resultPromise) {
    return false;
  }

  // Since the only synthetic modules we support are JSON modules, result is
  // always |undefined|.

  // Step 14. IfAbruptRejectPromise(result, pc) (Skipped)

  // 15. Perform ! pc.[[Resolve]](result).
  if (!AsyncFunctionReturned(cx, resultPromise, JS::UndefinedHandleValue)) {
    return false;
  }

  // 16. Return pc.[[Promise]].
  rval.setObject(*resultPromise);
  return true;
}

// https://tc39.es/ecma262/#sec-moduleevaluation
// ES2023 16.2.1.5.2 Evaluate
static bool ModuleEvaluate(JSContext* cx, Handle<ModuleObject*> moduleArg,
                           MutableHandle<Value> result) {
  Rooted<ModuleObject*> module(cx, moduleArg);

  // Step 2. Assert: module.[[Status]] is linked, evaluating-async, or
  //         evaluated.
  ModuleStatus status = module->status();
  if (status != ModuleStatus::Linked &&
      status != ModuleStatus::EvaluatingAsync &&
      status != ModuleStatus::Evaluated) {
    ThrowUnexpectedModuleStatus(cx, status);
    return false;
  }

  // Step 3. If module.[[Status]] is evaluating-async or evaluated, set module
  //         to module.[[CycleRoot]].
  if (module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    // a. If module.[[CycleRoot]] is not empty, then
    if (module->hasCycleRoot()) {
      // i. Set module to module.[[CycleRoot]].
      module = module->getCycleRoot();
    } else {
      // b. Else
      //   i. Assert: module.[[Status]] is evaluated and
      //      module.[[EvaluationError]] is a throw completion.
      MOZ_ASSERT((module->status() == ModuleStatus::Evaluated) &&
                 module->hadEvaluationError());
    }
  }

  // Step 4. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 4.a. Return module.[[TopLevelCapability]].[[Promise]].
    result.setObject(*module->topLevelCapability());
    return true;
  }

  // Step 5. Let stack be a new empty List.
  Rooted<ModuleVector> stack(cx);

  // Step 6. Let capability be ! NewPromiseCapability(%Promise%).
  // Step 7. Set module.[[TopLevelCapability]] to capability.
  Rooted<PromiseObject*> capability(
      cx, ModuleObject::createTopLevelCapability(cx, module));
  if (!capability) {
    return false;
  }

  // Step 8. Let result be Completion(InnerModuleEvaluation(module, stack, 0)).
  size_t ignored;
  bool ok = InnerModuleEvaluation(cx, module, &stack, 0, &ignored);

  // Step 9. f result is an abrupt completion, then:
  if (!ok) {
    // Attempt to take any pending exception, but make sure we still handle
    // uncatchable exceptions.
    Rooted<Value> error(cx);
    if (cx->isExceptionPending()) {
      (void)cx->getPendingException(&error);
      cx->clearPendingException();
    }

    // Step 9.a. For each Cyclic Module Record m of stack, do
    for (ModuleObject* m : stack) {
      // Step 9.a.i. Assert: m.[[Status]] is evaluating.
      MOZ_ASSERT(m->status() == ModuleStatus::Evaluating);

      // Step 9.a.ii. Set m.[[Status]] to evaluated.
      // Step 9.a.iii. Set m.[[EvaluationError]] to result.
      m->setEvaluationError(error);
    }

    // Handle OOM when appending to the stack or over-recursion errors.
    if (stack.empty() && !module->hadEvaluationError()) {
      module->setEvaluationError(error);
    }

    // Step 9.b. Assert: module.[[Status]] is evaluated.
    MOZ_ASSERT(module->status() == ModuleStatus::Evaluated);

    // Step 9.c. Assert: module.[[EvaluationError]] is result.
    MOZ_ASSERT(module->evaluationError() == error);

    // Step 9.d. Perform ! Call(capability.[[Reject]], undefined,
    //           result.[[Value]]).
    if (!ModuleObject::topLevelCapabilityReject(cx, module, error)) {
      return false;
    }
  } else {
    // Step 10. Else:
    // Step 10.a. Assert: module.[[Status]] is evaluating-async or evaluated.
    MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync ||
               module->status() == ModuleStatus::Evaluated);

    // Step 10.b. Assert: module.[[EvaluationError]] is empty.
    MOZ_ASSERT(!module->hadEvaluationError());

    // Step 10.c. If module.[[Status]] is evalated, then:
    if (module->status() == ModuleStatus::Evaluated) {
      // Step 10.c.ii. Perform ! Call(capability.[[Resolve]], undefined,
      //               undefined).
      if (!ModuleObject::topLevelCapabilityResolve(cx, module)) {
        return false;
      }
    }

    // Step 10.d. Assert: stack is empty.
    MOZ_ASSERT(stack.empty());
  }

  // Step 11. Return capability.[[Promise]].
  result.setObject(*capability);
  return true;
}

// https://tc39.es/proposal-source-phase-imports/#sec-innermoduleevaluation
// 16.2.1.5.3.1 InnerModuleEvaluation
static bool InnerModuleEvaluation(JSContext* cx, Handle<ModuleObject*> module,
                                  MutableHandle<ModuleVector> stack,
                                  size_t index, size_t* indexOut) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 1: If module is not a Cyclic Module Record, then
  if (!module->hasCyclicModuleFields()) {
    // Step 1.a. Perform ? EvaluateModuleSync(module). Skipped: evaluating a
    //           synthetic module is infallible and its bindings are already
    //           set up, so there is nothing to do and nothing to throw.
    // Step 1.b. Return index.
    *indexOut = index;
    return true;
  }

  // Step 2. If module.[[Status]] is evaluating-async or evaluated, then:
  if (module->status() == ModuleStatus::EvaluatingAsync ||
      module->status() == ModuleStatus::Evaluated) {
    // Step 2.a. If module.[[EvaluationError]] is empty, return index.
    if (!module->hadEvaluationError()) {
      *indexOut = index;
      return true;
    }

    // Step 2.b. Otherwise, return ? module.[[EvaluationError]].
    Rooted<Value> error(cx, module->evaluationError());
    cx->setPendingException(error, ShouldCaptureStack::Maybe);
    return false;
  }

  // Step 3. If module.[[Status]] is evaluating, return index.
  if (module->status() == ModuleStatus::Evaluating) {
    *indexOut = index;
    return true;
  }

  // Step 4. Assert: module.[[Status]] is linked.
  MOZ_ASSERT(module->status() == ModuleStatus::Linked);

  // Step 12. Append module to stack.
  // Do this before changing the status so that we can recover on failure.
  if (!stack.append(module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. Set module.[[Status]] to evaluating.
  module->setStatus(ModuleStatus::Evaluating);

  // Step 6. Let moduleIndex be index.
  size_t moduleIndex = index;

  // Step 7. Set module.[[DFSAncestorIndex]] to index.
  module->setDfsAncestorIndex(index);

  // Step 8. Set module.[[PendingAsyncDependencies]] to 0.
  module->setPendingAsyncDependencies(0);

  // Step 9. Set index to index + 1.
  index++;

  // Step 10. Let evaluationList be a new empty List.
  Rooted<ModuleVector> evaluationList(cx);
  Rooted<ModuleSet> evaluationSet(cx);

  // Step 11. For each ModuleRequest Record request of
  //          module.[[RequestedModules]], do:
  Rooted<ModuleRequestObject*> required(cx);
  Rooted<ModuleObject*> requiredModule(cx);
  for (const RequestedModule& request : module->requestedModules()) {
    // Step a. Let requiredModule be GetImportedModule(module, request).
    required = request.moduleRequest();
    requiredModule = GetImportedModule(cx, module, required);
    if (!requiredModule) {
      return false;
    }

    // Step b. If request.[[Phase]] is defer, then:
    if (required->phase() == ImportPhase::Deferred) {
      Rooted<ModuleSet> seen(cx);
      // Step i. Let additionalModules be
      //         GatherAsynchronousTransitiveDependencies(requiredModule).
      Rooted<ModuleVector> additionalModules(cx);
      if (!GatherAsynchronousTransitiveDependencies(cx, requiredModule, &seen,
                                                    &additionalModules)) {
        return false;
      }

      // Step ii. For each Module Record additionalModule of additionalModules,
      //          do
      for (ModuleObject* m : additionalModules) {
        // Step 1. If evaluationList does not contain additionalModule, then
        //   Step a. Append additionalModule to evaluationList.
        if (!AppendUniqueModule(cx, &evaluationList, &evaluationSet, m)) {
          return false;
        }
      }
    } else if (required->phase() == ImportPhase::Evaluation) {
      // Step 11.c. Else if evaluationList does not contain requiredModule,
      // append requiredModule to evaluationList.
      //
      // Note: source-phase imports are excluded here as they are not evaluated.
      // See
      // https://tc39.es/proposal-source-phase-imports/#sec-innermoduleevaluation
      if (!AppendUniqueModule(cx, &evaluationList, &evaluationSet,
                              requiredModule)) {
        return false;
      }
    }
  }

  // Step 14. For each Module Record requiredModule of evaluationList, do:
  for (ModuleObject* m : evaluationList) {
    requiredModule = m;
    MOZ_ASSERT(requiredModule->status() >= ModuleStatus::Linked);

    // Step a. Set index to ? InnerModuleEvaluation(requiredModule, stack,
    //         index).
    if (!InnerModuleEvaluation(cx, requiredModule, stack, index, &index)) {
      return false;
    }

    // Step b. If requiredModule is a Cyclic Module Record, then:
    if (requiredModule->hasCyclicModuleFields()) {
      // Step i. Assert: requiredModule.[[Status]] is either
      //         evaluating, evaluating-async, or evaluated.
      MOZ_ASSERT(requiredModule->status() == ModuleStatus::Evaluating ||
                 requiredModule->status() == ModuleStatus::EvaluatingAsync ||
                 requiredModule->status() == ModuleStatus::Evaluated);

      // Step ii. Assert: requiredModule.[[Status]] is evaluating if
      //          and only if requiredModule is in stack.
      if ((requiredModule->status() == ModuleStatus::Evaluating) !=
          ContainsElement(stack, requiredModule)) {
        ThrowUnexpectedModuleStatus(cx, requiredModule->status());
        return false;
      }

      // Step 14.b.iii. If requiredModule.[[Status]] is evaluating, then:
      if (requiredModule->status() == ModuleStatus::Evaluating) {
        // Step 14.b.iii.1. Set module.[[DFSAncestorIndex]] to
        //                  min(module.[[DFSAncestorIndex]],
        //                  requiredModule.[[DFSAncestorIndex]]).
        module->setDfsAncestorIndex(std::min(
            module->dfsAncestorIndex(), requiredModule->dfsAncestorIndex()));
      } else {
        // Step 14.b.iv. Else:
        // Step 14.b.iv.1. Set requiredModule to requiredModule.[[CycleRoot]].
        requiredModule = requiredModule->getCycleRoot();

        // Step 14.b.iv.2. Assert: requiredModule.[[Status]] is
        // evaluating-async or evaluated.
        MOZ_ASSERT(requiredModule->status() >= ModuleStatus::EvaluatingAsync ||
                   requiredModule->status() == ModuleStatus::Evaluated);

        // Step 14.b.iv.3. If requiredModule.[[EvaluationError]] is not
        // empty, return ? requiredModule.[[EvaluationError]].
        if (requiredModule->hadEvaluationError()) {
          Rooted<Value> error(cx, requiredModule->evaluationError());
          cx->setPendingException(error, ShouldCaptureStack::Maybe);
          return false;
        }
      }

      // Step 14.b.v. If requiredModule.[[AsyncEvaluationOrder]] is an
      // integer, then:
      if (requiredModule->asyncEvaluationOrder().isInteger()) {
        // Step 14.b.v.2. Append module to
        //                requiredModule.[[AsyncParentModules]].
        if (!ModuleObject::appendAsyncParentModule(cx, requiredModule,
                                                   module)) {
          return false;
        }

        // Step 14.b.v.1. Set module.[[PendingAsyncDependencies]] to
        //                module.[[PendingAsyncDependencies]] + 1.
        module->setPendingAsyncDependencies(module->pendingAsyncDependencies() +
                                            1);
      }
    }
  }

  // Step 12. If module.[[PendingAsyncDependencies]] > 0 or module.[[HasTLA]] is
  //          true, then:
  if (module->pendingAsyncDependencies() > 0 || module->hasTopLevelAwait()) {
    // Step 12.a. Assert: module.[[AsyncEvaluationOrder]] is unset.
    MOZ_ASSERT(module->asyncEvaluationOrder().isUnset());

    // Step 12.b. Set module.[[AsyncEvaluationOrder]] to
    // IncrementModuleAsyncEvaluationCount().
    module->asyncEvaluationOrder().set(cx->runtime());

    // Step 12.d. If module.[[PendingAsyncDependencies]] is 0, perform
    //            ExecuteAsyncModule(module).
    if (module->pendingAsyncDependencies() == 0) {
      if (!ExecuteAsyncModule(cx, module)) {
        return false;
      }
    }
  } else {
    // Step 13. Otherwise, perform ? module.ExecuteModule().
    if (!ModuleObject::execute(cx, module)) {
      return false;
    }
  }

  // Step 14. Assert: module occurs exactly once in stack.
  MOZ_ASSERT(CountElements(stack, module) == 1);

  // Step 15. Assert: module.[[DFSAncestorIndex]] <= moduleIndex.
  MOZ_ASSERT(module->dfsAncestorIndex() <= moduleIndex);

  // Step 16. If module.[[DFSAncestorIndex]] = momoduleIndex, then:
  if (module->dfsAncestorIndex() == moduleIndex) {
    // Step 16.a. Let done be false.
    bool done = false;

    // Step 16.b. Repeat, while done is false:
    while (!done) {
      // Step 16.b.i. Let requiredModule be the last element in stack.
      // Step 16.b.ii. Remove the last element of stack.
      requiredModule = stack.popCopy();

      // Step 16.b.iii. Assert: requiredModule.[[AsyncEvaluationOrder]] is
      //                either an integer or unset
      MOZ_ASSERT(requiredModule->asyncEvaluationOrder().isInteger() ||
                 requiredModule->asyncEvaluationOrder().isUnset());

      // Step 16.b.v. If requiredModule.[[AsyncEvaluationOrder]] is unset, set
      //               requiredModule.[[Status]] to evaluated.
      if (requiredModule->asyncEvaluationOrder().isUnset()) {
        requiredModule->setStatus(ModuleStatus::Evaluated);
      } else {
        // Step 16.b.vi. Otherwise, set requiredModule.[[Status]] to
        //               evaluating-async.
        requiredModule->setStatus(ModuleStatus::EvaluatingAsync);
      }

      // Step 16.b.vii. If requiredModule and module are the same Module Record,
      //                set done to true.
      done = requiredModule == module;

      // Step 16.b.viii. Set requiredModule.[[CycleRoot]] to module.
      requiredModule->setCycleRoot(module);
    }
  }

  // Step 17. Return index.
  *indexOut = index;
  return true;
}

// https://tc39.es/ecma262/#sec-execute-async-module
// ES2023 16.2.1.5.2.2 ExecuteAsyncModule
static bool ExecuteAsyncModule(JSContext* cx, Handle<ModuleObject*> module) {
  // Step 1. Assert: module.[[Status]] is evaluating or evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::Evaluating ||
             module->status() == ModuleStatus::EvaluatingAsync);

  // Step 2. Assert: module.[[HasTLA]] is true.
  MOZ_ASSERT(module->hasTopLevelAwait());

  // Steps 3 - 8 are performed by the AsyncAwait opcode.

  // Step 9. Perform ! module.ExecuteModule(capability).
  // Step 10. Return unused.
  return ModuleObject::execute(cx, module);
}

// https://tc39.es/ecma262/#sec-gather-available-ancestors
// ES2023 16.2.1.5.2.3 GatherAvailableAncestors
static bool GatherAvailableModuleAncestors(
    JSContext* cx, Handle<ModuleObject*> module,
    MutableHandle<ModuleVector> execList) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 1. For each Cyclic Module Record m of module.[[AsyncParentModules]],
  //         do:
  Rooted<ListObject*> asyncParentModules(cx, module->asyncParentModules());
  Rooted<ModuleObject*> m(cx);
  for (uint32_t i = 0; i != asyncParentModules->length(); i++) {
    m = &asyncParentModules->getDenseElement(i).toObject().as<ModuleObject>();

    // Step 1.a. If execList does not contain m and
    //           m.[[CycleRoot]].[[EvaluationError]] is empty, then:
    //
    // Note: we also check whether m.[[EvaluationError]] is empty since an error
    // in synchronous execution can prevent the CycleRoot field from being set.
    if (!m->hadEvaluationError() && !m->getCycleRoot()->hadEvaluationError() &&
        !ContainsElement(execList, m)) {
      // Step 1.a.i. Assert: m.[[Status]] is evaluating-async.
      MOZ_ASSERT(m->status() == ModuleStatus::EvaluatingAsync);

      // Step 1.a.ii. Assert: m.[[EvaluationError]] is empty.
      MOZ_ASSERT(!m->hadEvaluationError());

      // Step 1.a.iii. Assert: m.[[AsyncEvaluationOrder]] is an integer.
      MOZ_ASSERT(m->asyncEvaluationOrder().isInteger());

      // Step 1.a.iv. Assert: m.[[PendingAsyncDependencies]] > 0.
      MOZ_ASSERT(m->pendingAsyncDependencies() > 0);

      // Step 1.a.v. Set m.[[PendingAsyncDependencies]] to
      // m.[[PendingAsyncDependencies]] - 1.
      m->setPendingAsyncDependencies(m->pendingAsyncDependencies() - 1);

      // Step 1.a.vi. If m.[[PendingAsyncDependencies]] = 0, then:
      if (m->pendingAsyncDependencies() == 0) {
        // Step 1.a.vi.1. Append m to execList.
        if (!execList.append(m)) {
          return false;
        }

        // Step 1.a.vi.2. If m.[[HasTLA]] is false, perform
        //                GatherAvailableAncestors(m, execList).
        if (!m->hasTopLevelAwait() &&
            !GatherAvailableModuleAncestors(cx, m, execList)) {
          return false;
        }
      }
    }
  }

  // Step 2. Return unused.
  return true;
}

struct EvalOrderComparator {
  bool operator()(ModuleObject* a, ModuleObject* b, bool* lessOrEqualp) {
    *lessOrEqualp =
        a->asyncEvaluationOrder().get() <= b->asyncEvaluationOrder().get();
    return true;
  }
};

static void RejectExecutionWithPendingException(JSContext* cx,
                                                Handle<ModuleObject*> module) {
  // If there is no exception pending then we have been interrupted or have
  // OOM'd and all bets are off. We reject the execution by throwing
  // undefined. Not much more we can do.
  RootedValue exception(cx);
  if (cx->isExceptionPending()) {
    (void)cx->getPendingException(&exception);
    cx->clearPendingException();
  }
  if (!AsyncModuleExecutionRejected(cx, module, exception)) {
    MOZ_ASSERT(cx->isThrowingOverRecursed());
    cx->clearPendingException();
  }
}

// https://tc39.es/ecma262/#sec-async-module-execution-fulfilled
// ES2023 16.2.1.5.2.4 AsyncModuleExecutionFulfilled
void js::AsyncModuleExecutionFulfilled(JSContext* cx,
                                       Handle<ModuleObject*> module) {
  // Step 1. If module.[[Status]] is evaluated, then:
  if (module->status() == ModuleStatus::Evaluated) {
    // Step 1.a. Assert: module.[[EvaluationError]] is not empty.
    MOZ_ASSERT(module->hadEvaluationError());

    // Step 1.b. Return unused.
    return;
  }

  // Step 2. Assert: module.[[Status]] is evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 3. Assert: module.[[AsyncEvaluationOrder]] is an integer.
  MOZ_ASSERT(module->asyncEvaluationOrder().isInteger());

  // Step 4. Assert: module.[[EvaluationError]] is empty.
  MOZ_ASSERT(!module->hadEvaluationError());

  // The following steps are performed in a different order from the
  // spec. Gather available module ancestors before mutating the module object
  // as this can fail in our implementation.

  // Step 8. Let execList be a new empty List.
  Rooted<ModuleVector> execList(cx);

  // Step 9. Perform GatherAvailableAncestors(module, execList).
  if (!GatherAvailableModuleAncestors(cx, module, &execList)) {
    RejectExecutionWithPendingException(cx, module);
    return;
  }

  // Step 10. Let sortedExecList be a List whose elements are the elements of
  //          execList, in the order in which they had their [[AsyncEvaluation]]
  //          fields set to true in InnerModuleEvaluation.

  Rooted<ModuleVector> scratch(cx);
  if (!scratch.resize(execList.length())) {
    ReportOutOfMemory(cx);
    RejectExecutionWithPendingException(cx, module);
    return;
  }

  MOZ_ALWAYS_TRUE(MergeSort(execList.begin(), execList.length(),
                            scratch.begin(), EvalOrderComparator()));

  // Step 11. Assert: All elements of sortedExecList have their
  //          [[AsyncEvaluationOrder]] field set to an integer,
  //          [[PendingAsyncDependencies]] field set to 0, and
  //          [[EvaluationError]] field set to empty.
#ifdef DEBUG
  for (ModuleObject* m : execList) {
    MOZ_ASSERT(m->asyncEvaluationOrder().isInteger());
    MOZ_ASSERT(m->pendingAsyncDependencies() == 0);
    MOZ_ASSERT(!m->hadEvaluationError());
  }
#endif

  // Return to original order of steps.

  ModuleObject::onTopLevelEvaluationFinished(module);

  // Step 5. Set module.[[AsyncEvaluationOrder]] to done.
  module->asyncEvaluationOrder().setDone(cx->runtime());

  // Step 6. Set module.[[Status]] to evaluated.
  module->setStatus(ModuleStatus::Evaluated);

  // Step 7. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 7.a. Assert: module.[[CycleRoot]] is module.
    MOZ_ASSERT(module->getCycleRoot() == module);

    // Step 7.b. Perform ! Call(module.[[TopLevelCapability]].[[Resolve]],
    //           undefined, undefined).
    if (!ModuleObject::topLevelCapabilityResolve(cx, module)) {
      // If Resolve fails, there's nothing more we can do here.
      cx->clearPendingException();
    }
  }

  // Step 12. For each Cyclic Module Record m of sortedExecList, do:
  Rooted<ModuleObject*> m(cx);
  for (ModuleObject* obj : execList) {
    m = obj;

    // Step 12.a. If m.[[Status]] is evaluated, then:
    if (m->status() == ModuleStatus::Evaluated) {
      // Step 12.a.i. Assert: m.[[EvaluationError]] is not empty.
      MOZ_ASSERT(m->hadEvaluationError());
    } else if (m->hasTopLevelAwait()) {
      // Step 12.b. Else if m.[[HasTLA]] is true, then:
      // Step 12.b.i. Perform ExecuteAsyncModule(m).
      if (!ExecuteAsyncModule(cx, m)) {
        RejectExecutionWithPendingException(cx, m);
      }
    } else {
      // Step 12.c. Else:
      // Step 12.c.i. Let result be m.ExecuteModule().
      bool ok = ModuleObject::execute(cx, m);

      // Step 12.c.ii. If result is an abrupt completion, then:
      if (!ok) {
        // Step 12.c.ii.1. Perform AsyncModuleExecutionRejected(m,
        //                 result.[[Value]]).
        RejectExecutionWithPendingException(cx, m);
      } else {
        // Step 12.c.iii. Else:
        // Step 12.c.iii.1. Set m.[[AsyncEvaluationOrder]] to done.
        m->asyncEvaluationOrder().setDone(m->zone()->runtimeFromMainThread());
        // Step 12.c.iii.2. Set m.[[Status]] to evaluated.
        m->setStatus(ModuleStatus::Evaluated);

        // Step 12.c.iii.2. If m.[[TopLevelCapability]] is not empty, then:
        if (m->hasTopLevelCapability()) {
          // Step 12.c.iii.2.a. Assert: m.[[CycleRoot]] is m.
          MOZ_ASSERT(m->getCycleRoot() == m);

          // Step 12.c.iii.2.b. Perform !
          //                    Call(m.[[TopLevelCapability]].[[Resolve]],
          //                    undefined, undefined).
          if (!ModuleObject::topLevelCapabilityResolve(cx, m)) {
            // If Resolve fails, there's nothing more we can do here.
            cx->clearPendingException();
          }
        }
      }
    }
  }

  // Step 13. Return unused.
}

// https://tc39.es/ecma262/#sec-async-module-execution-rejected
// ES2023 16.2.1.5.2.5 AsyncModuleExecutionRejected
bool js::AsyncModuleExecutionRejected(JSContext* cx,
                                      Handle<ModuleObject*> module,
                                      HandleValue error) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 1. If module.[[Status]] is evaluated, then:
  if (module->status() == ModuleStatus::Evaluated) {
    // Step 1.a. Assert: module.[[EvaluationError]] is not empty
    MOZ_ASSERT(module->hadEvaluationError());

    // Step 1.b. Return unused.
    return true;
  }

  // Step 2. Assert: module.[[Status]] is evaluating-async.
  MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync);

  // Step 3. Assert: module.[[AsyncEvaluationOrder]] is an integer.
  MOZ_ASSERT(module->asyncEvaluationOrder().isInteger());

  // Step 4. Assert: module.[[EvaluationError]] is empty.
  MOZ_ASSERT(!module->hadEvaluationError());

  ModuleObject::onTopLevelEvaluationFinished(module);

  // Step 5. Set module.[[EvaluationError]] to ThrowCompletion(error).
  module->setEvaluationError(error);

  // Step 6. Set module.[[Status]] to evaluated.
  MOZ_ASSERT(module->status() == ModuleStatus::Evaluated);

  // Step 7. Set module.[[AsyncEvaluationOrder]] to done.
  module->asyncEvaluationOrder().setDone(cx->runtime());

  // Step 9. If module.[[TopLevelCapability]] is not empty, then:
  if (module->hasTopLevelCapability()) {
    // Step 9.a. Assert: module.[[CycleRoot]] is module.
    MOZ_ASSERT(module->getCycleRoot() == module);

    // Step 9.b. Perform ! Call(module.[[TopLevelCapability]].[[Reject]],
    //           undefined, error).
    if (!ModuleObject::topLevelCapabilityReject(cx, module, error)) {
      // If Reject fails, there's nothing more we can do here.
      cx->clearPendingException();
    }
  }

  // Step 10. For each Cyclic Module Record m of module.[[AsyncParentModules]],
  //         do:
  Rooted<ListObject*> parents(cx, module->asyncParentModules());
  Rooted<ModuleObject*> parent(cx);
  for (uint32_t i = 0; i < parents->length(); i++) {
    parent = &parents->get(i).toObject().as<ModuleObject>();

    // Step 10.a. Perform AsyncModuleExecutionRejected(m, error).
    if (!AsyncModuleExecutionRejected(cx, parent, error)) {
      return false;
    }
  }

  // Step 11. Return unused.
  return true;
}

// https://tc39.es/ecma262/#sec-evaluate-import-call
// NOTE: The caller needs to handle the promise.
static bool EvaluateDynamicImportOptions(
    JSContext* cx, HandleValue optionsArg,
    MutableHandle<ImportAttributeVector> attributesArrayArg) {
  // Step 11. If options is not undefined, then
  if (optionsArg.isUndefined()) {
    return true;
  }

  // Step 11.a. If options is not an Object, then
  if (!optionsArg.isObject()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_EXPECTED_TYPE, "import",
        "object or undefined", InformalValueTypeName(optionsArg));
    return false;
  }

  RootedObject attributesWrapperObject(cx, &optionsArg.toObject());
  RootedValue attributesValue(cx);

  // Step 11.b. Let attributesObj be Completion(Get(options, "with")).
  RootedId withId(cx, NameToId(cx->names().with));
  if (!GetProperty(cx, attributesWrapperObject, attributesWrapperObject, withId,
                   &attributesValue)) {
    return false;
  }

  // Step 11.d. If attributesObj is not undefined, then
  if (attributesValue.isUndefined()) {
    return true;
  }

  //   Step i. If attributesObj is not an Object, then
  if (!attributesValue.isObject()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_EXPECTED_TYPE, "import",
        "object or undefined", InformalValueTypeName(attributesValue));
    return false;
  }

  //   Step ii. Let entries be
  //   Completion(EnumerableOwnProperties(attributesObj, key+value)).
  RootedObject attributesObject(cx, &attributesValue.toObject());
  RootedIdVector attributes(cx);
  if (!GetPropertyKeys(cx, attributesObject, JSITER_OWNONLY, &attributes)) {
    return false;
  }

  uint32_t numberOfAttributes = attributes.length();
  if (numberOfAttributes == 0) {
    return true;
  }

  // Step 10 (reordered). Let attributes be a new empty List.
  if (!attributesArrayArg.reserve(numberOfAttributes)) {
    ReportOutOfMemory(cx);
    return false;
  }

  size_t numberOfValidAttributes = 0;

  //   Step iv. For each element entry of entries, do
  RootedId key(cx);
  RootedValue value(cx);
  Rooted<JSAtom*> keyAtom(cx);
  Rooted<JSString*> valueString(cx);
  for (size_t i = 0; i < numberOfAttributes; i++) {
    //   Step 1. Let key be ! Get(entry, "0").
    key = attributes[i];

    //   Step 2. Let value be ! Get(entry, "1").
    if (!GetProperty(cx, attributesObject, attributesObject, key, &value)) {
      return false;
    }

    //   Step 3. If key is a String, then
    //
    //   JSITER_OWNONLY only returns String and Int keys; Int keys are
    //   converted to String atoms below.
    MOZ_ASSERT(key.isString() || key.isInt());

    //     Step a. If value is not a String, then
    if (!value.isString()) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_NOT_EXPECTED_TYPE, "import", "string",
                                InformalValueTypeName(value));
      return false;
    }

    if (key.isInt()) {
      keyAtom = Int32ToAtom(cx, key.toInt());
      if (!keyAtom) {
        return false;
      }
    } else {
      keyAtom = key.toAtom();
    }

    // Step 11.e (reordered). If AllImportAttributesSupported(attributes) is
    // false, then
    //
    // Note: This should be driven by a host hook
    // (HostGetSupportedImportAttributes), however the infrastructure of said
    // host hook is deeply unclear, and so right now embedders will not have
    // the ability to alter or extend the set of supported attributes.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=1840723.
    if (keyAtom != cx->names().type) {
      UniqueChars printableKey = AtomToPrintableString(cx, keyAtom);
      if (!printableKey) {
        return false;
      }
      JS_ReportErrorNumberASCII(
          cx, GetErrorMessage, nullptr,
          JSMSG_IMPORT_ATTRIBUTES_DYNAMIC_IMPORT_UNSUPPORTED_ATTRIBUTE,
          printableKey.get());
      return false;
    }

    // Step 3.b. Append the ImportAttribute Record { [[Key]]: key,
    // [[Value]]: value } to attributes.
    valueString = value.toString();
    attributesArrayArg.infallibleEmplaceBack(keyAtom, valueString);
    ++numberOfValidAttributes;
  }

  if (numberOfValidAttributes == 0) {
    return true;
  }

  // Step 11.f (skipped). Sort attributes according to the lexicographic order
  // of their [[Key]] fields, treating the value of each such field as a
  // sequence of UTF-16 code unit values.
  //
  // We only support "type", so we can ignore this.

  return true;
}

// https://tc39.es/ecma262/#sec-evaluate-import-call
JSObject* js::StartDynamicModuleImport(JSContext* cx, HandleScript script,
                                       HandleValue specifierArg,
                                       HandleValue optionsArg,
                                       ImportPhase phase) {
  RootedObject promise(cx);
  if (phase == ImportPhase::Source) {
    promise = PromiseObject::createSkippingExecutor(cx);
  } else {
    // Step 7. Let promiseCapability be ! NewPromiseCapability(%Promise%).
    promise = JS::NewPromiseObject(cx, nullptr);
  }
  if (!promise) {
    return nullptr;
  }

  if (!TryStartDynamicModuleImport(cx, script, specifierArg, optionsArg,
                                   promise, phase)) {
    if (!RejectPromiseWithPendingError(cx, promise.as<PromiseObject>())) {
      return nullptr;
    }
  }

  return promise;
}

// https://tc39.es/ecma262/#sec-evaluate-import-call continued.
static bool TryStartDynamicModuleImport(JSContext* cx, HandleScript script,
                                        HandleValue specifierArg,
                                        HandleValue optionsArg,
                                        HandleObject promise,
                                        ImportPhase phase) {
  RootedString specifier(cx, ToString(cx, specifierArg));
  if (!specifier) {
    return false;
  }

  Rooted<JSAtom*> specifierAtom(cx, AtomizeString(cx, specifier));
  if (!specifierAtom) {
    return false;
  }

  RootedObject moduleRequest(cx);
  if (phase == ImportPhase::Source) {
    // https://tc39.es/proposal-source-phase-imports/#sec-evaluate-import-call
    // Step 8. Let moduleRequest be a new ModuleRequest Record { [[Specifier]]:
    //         specifierString, [[Phase]]: source }.
    moduleRequest = ModuleRequestObject::create(
        cx, specifierAtom, JS::ModuleType::JavaScriptOrWasm, phase);
  } else {
    MOZ_ASSERT(phase == ImportPhase::Evaluation ||
               phase == ImportPhase::Deferred);

    Rooted<ImportAttributeVector> attributes(cx);
    if (!EvaluateDynamicImportOptions(cx, optionsArg, &attributes)) {
      return false;
    }

    // Step 12. Let moduleRequest be a new ModuleRequest Record { [[Specifier]]:
    //          specifierString, [[Attributes]]: attributes }.
    moduleRequest =
        ModuleRequestObject::create(cx, specifierAtom, attributes, phase);
  }
  if (!moduleRequest) {
    return false;
  }

  // Step 13. Perform HostLoadImportedModule(referrer, moduleRequest, empty,
  //          promiseCapability).
  RootedValue payload(cx, ObjectValue(*promise));
  Rooted<Value> referrer(cx, ReferrerValueForScript(script));
  (void)HostLoadImportedModule(cx, referrer, moduleRequest,
                               JS::UndefinedHandleValue, payload);

  return true;
}

static bool OnRootModuleRejected(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue error = args.get(0);

  ReportExceptionClosure reportExn(error);
  PrepareScriptEnvironmentAndInvoke(cx, cx->global(), reportExn);

  args.rval().setUndefined();
  return true;
};

bool js::OnModuleEvaluationFailure(JSContext* cx,
                                   HandleObject evaluationPromise,
                                   JS::ModuleErrorBehaviour errorBehaviour) {
  if (evaluationPromise == nullptr) {
    return false;
  }

  // To allow module evaluation to happen synchronously throw the error
  // immediately. This assumes that any error will already have caused the
  // promise to be rejected, and doesn't support top-level await.
  if (errorBehaviour == JS::ThrowModuleErrorsSync) {
    JS::PromiseState state = JS::GetPromiseState(evaluationPromise);
    MOZ_DIAGNOSTIC_ASSERT(state == JS::PromiseState::Rejected ||
                          state == JS::PromiseState::Fulfilled);

    JS::SetSettledPromiseIsHandled(cx, evaluationPromise);
    if (state == JS::PromiseState::Fulfilled) {
      return true;
    }

    RootedValue error(cx, JS::GetPromiseResult(evaluationPromise));
    JS_SetPendingException(cx, error);
    return false;
  }

  RootedFunction onRejected(
      cx, NewHandler(cx, OnRootModuleRejected, evaluationPromise));
  if (!onRejected) {
    return false;
  }

  return JS::AddPromiseReactions(cx, evaluationPromise, nullptr, onRejected);
}

// This is used for |fulfilledClosure| and |rejectedClosure| in
// https://tc39.es/ecma262/#sec-ContinueDynamicImport
//
// It is used to marshal some arguments and pass them through to the promise
// resolve and reject callbacks. It holds a reference to the referencing private
// to keep it alive until it is needed.
class DynamicImportContextObject : public NativeObject {
 public:
  enum { PromiseSlot = 0, ModuleSlot, PhaseSlot, SlotCount };

  static const JSClass class_;

  [[nodiscard]] static DynamicImportContextObject* create(
      JSContext* cx, Handle<PromiseObject*> promise,
      Handle<ModuleObject*> module, ImportPhase phase);

  PromiseObject* promise() const;
  ModuleObject* module() const;
  ImportPhase phase() const;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

/* static */
const JSClass DynamicImportContextObject::class_ = {
    "DynamicImportContextObject",
    JSCLASS_HAS_RESERVED_SLOTS(DynamicImportContextObject::SlotCount)};

/* static */
DynamicImportContextObject* DynamicImportContextObject::create(
    JSContext* cx, Handle<PromiseObject*> promise, Handle<ModuleObject*> module,
    ImportPhase phase) {
  Rooted<DynamicImportContextObject*> self(
      cx, NewObjectWithGivenProto<DynamicImportContextObject>(cx, nullptr));
  if (!self) {
    return nullptr;
  }

  self->initReservedSlot(PromiseSlot, ObjectValue(*promise));
  self->initReservedSlot(ModuleSlot, ObjectValue(*module));
  self->initReservedSlot(PhaseSlot, Int32Value(int32_t(phase)));
  return self;
}

PromiseObject* DynamicImportContextObject::promise() const {
  Value value = getReservedSlot(PromiseSlot);
  if (value.isUndefined()) {
    return nullptr;
  }

  return &value.toObject().as<PromiseObject>();
}

ModuleObject* DynamicImportContextObject::module() const {
  Value value = getReservedSlot(ModuleSlot);
  if (value.isUndefined()) {
    return nullptr;
  }

  return &value.toObject().as<ModuleObject>();
}

ImportPhase DynamicImportContextObject::phase() const {
  Value value = getReservedSlot(PhaseSlot);
  if (value.isUndefined()) {
    return ImportPhase::Limit;
  }

  return static_cast<ImportPhase>(value.toInt32());
}

// https://tc39.es/ecma262/#sec-ContinueDynamicImport
/* static */
bool ContinueDynamicImport(JSContext* cx,
                           Handle<PromiseObject*> promiseCapability,
                           Handle<ModuleObject*> module, ImportPhase phase,
                           bool usePromise) {
  MOZ_ASSERT(module);

  // Step 1, 2: Already handled in FinishLoadingImportedModuleFailed functions.

  // https://tc39.es/proposal-source-phase-imports/#sec-ContinueDynamicImport
  // Step 3. If phase is source, then
  if (phase == ImportPhase::Source) {
    // Step 3.a. Let moduleSource be module.[[ModuleSource]].
    JSObject* moduleSource = module->moduleSource();

    // Step 3.b. If moduleSource is empty, then
    if (!moduleSource) {
      // Step 3.b.i. Perform ! Call(promiseCapability.[[Reject]], undefined,
      //                            « a new SyntaxError »).
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_MODULE_SOURCE_NOT_AVAILABLE);
      return RejectPromiseWithPendingError(cx, promiseCapability);
    }

    // Step 3.c. Else,
    // Step 3.c.i. Perform ! Call(promiseCapability.[[Resolve]], undefined,
    //                            « moduleSource »).
    RootedValue moduleSourceValue(cx, ObjectValue(*moduleSource));
    if (!PromiseObject::resolve(cx, promiseCapability, moduleSourceValue)) {
      return false;
    }

    // Step 3.d. Return unused.
    return true;
  }

  // Step 6. Let linkAndEvaluateClosure be a new Abstract Closure with no
  // parameters that captures module, promiseCapability, and onRejected...
  Rooted<DynamicImportContextObject*> context(
      cx,
      DynamicImportContextObject::create(cx, promiseCapability, module, phase));
  if (!context) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Our implementation provides an option for synchronous completion for
  // environments where we can't use promises.
  if (!usePromise) {
    return LinkAndEvaluateDynamicImport(cx, context);
  }

  // Step 3: The module dependencies has been loaded in the host layer, so we
  // only need to do _linkAndEvaluate_ part defined in the spec. Create a
  // promise that we'll resolve immediately.
  JS::Rooted<PromiseObject*> loadPromise(cx, CreatePromiseObjectForAsync(cx));

  if (!loadPromise) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Step 7. Let linkAndEvaluate be
  //         CreateBuiltinFunction(linkAndEvaluateClosure, 0, "", []).
  Rooted<JSFunction*> linkAndEvaluate(cx);
  linkAndEvaluate = js::NewFunctionWithReserved(
      cx, LinkAndEvaluateDynamicImport, 0, 0, "resolved");
  if (!linkAndEvaluate) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  // Step 8. Perform PerformPromiseThen(loadPromise, linkAndEvaluate,
  // onRejected).
  js::SetFunctionNativeReserved(linkAndEvaluate, 0, ObjectValue(*context));
  if (!JS::AddPromiseReactions(cx, loadPromise, linkAndEvaluate, nullptr)) {
    return RejectPromiseWithPendingError(cx, promiseCapability);
  }

  return AsyncFunctionReturned(cx, loadPromise, UndefinedHandleValue);
}

// https://tc39.es/proposal-defer-import-eval/#sec-GatherAsynchronousTransitiveDependencies
static bool GatherAsynchronousTransitiveDependencies(
    JSContext* cx, Handle<ModuleObject*> module, MutableHandle<ModuleSet> seen,
    MutableHandle<ModuleVector> result) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 3. If seen contains module, return result.
  auto ptr = seen.lookupForAdd(module);
  if (ptr) {
    return true;
  }

  // Step 4. Append module to seen.
  if (!seen.add(ptr, module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. If module is not a Cyclic Module Record, return result.
  if (!module->hasCyclicModuleFields()) {
    return true;
  }

  // Step 6. If module.[[Status]] is evaluating, or IsModuleSCCEvaluated(module)
  // is true, return result.
  if (module->status() == ModuleStatus::Evaluating ||
      IsModuleSCCEvaluated(module)) {
    return true;
  }

  // Step 7. If module.[[HasTLA]] is true, then:
  if (module->hasTopLevelAwait()) {
    if (!result.append(module)) {
      ReportOutOfMemory(cx);
      return false;
    }
    return true;
  }

  // Step 8. For each ModuleRequest Record request of
  // module.[[RequestedModules]]:
  Rooted<ModuleRequestObject*> request(cx);
  Rooted<ModuleObject*> requiredModule(cx);
  for (const RequestedModule& req : module->requestedModules()) {
    request = req.moduleRequest();
    requiredModule = GetImportedModule(cx, module, request);
    if (!requiredModule) {
      return false;
    }

    if (!GatherAsynchronousTransitiveDependencies(cx, requiredModule, seen,
                                                  result)) {
      return false;
    }
  }

  return true;
}

// static
bool LinkAndEvaluateDynamicImport(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Value value = js::GetFunctionNativeReserved(&args.callee(), 0);
  Rooted<DynamicImportContextObject*> context(cx);
  context = &value.toObject().as<DynamicImportContextObject>();
  return LinkAndEvaluateDynamicImport(cx, context);
}

// This performs the steps for |fulfilledClosure| from
// https://tc39.es/ecma262/#sec-ContinueDynamicImport step 6.d.
//
// With adjustment for Top-level await:
// https://GitHub.com/tc39/proposal-dynamic-import/pull/71/files
static bool DynamicImportResolved(JSContext* cx,
                                  Handle<DynamicImportContextObject*> context,
                                  Handle<PromiseObject*> promise) {
  Rooted<ModuleObject*> module(cx, context->module());

#ifdef DEBUG
  if (context->phase() == ImportPhase::Deferred) {
    MOZ_ASSERT(module->status() == ModuleStatus::Linked ||
               module->status() == ModuleStatus::EvaluatingAsync ||
               module->status() == ModuleStatus::Evaluated);
  } else {
    MOZ_ASSERT(module->status() == ModuleStatus::EvaluatingAsync ||
               module->status() == ModuleStatus::Evaluated);
  }

  // This is called when |evaluationPromise| is resolved, step 6.f.
  // For deferred imports where the module was already evaluated via a static
  // import, topLevelCapability may not have been set on the cycle root.
  if (module->hasCyclicModuleFields() && module->hasCycleRoot() &&
      module->getCycleRoot()->hasTopLevelCapability()) {
    MOZ_ASSERT(module->getCycleRoot()
                   ->maybeTopLevelCapability()
                   ->as<PromiseObject>()
                   .state() == JS::PromiseState::Fulfilled);
  }
#endif

  // Step 6.d.i. Let namespace be GetModuleNamespace(module, phase).
  RootedObject ns(cx, GetOrCreateModuleNamespace(cx, module, context->phase()));
  if (!ns) {
    return RejectPromiseWithPendingError(cx, promise);
  }

  // Step 6.d.ii. Perform ! Call(promiseCapability.[[Resolve]], undefined, [
  //              namespace ]).
  RootedValue value(cx, ObjectValue(*ns));
  if (!PromiseObject::resolve(cx, promise, value)) {
    return false;
  }

  return true;
};

static bool DynamicImportResolved(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  Rooted<DynamicImportContextObject*> context(
      cx, ExtraFromHandler<DynamicImportContextObject>(args));

  Rooted<PromiseObject*> promise(cx, TargetFromHandler<PromiseObject>(args));
  Rooted<ModuleObject*> module(cx, context->module());

  // For evaluation-phase imports the evaluate promise resolves with undefined;
  // for deferred imports it is the result of SafePerformPromiseAll (an array).
  MOZ_ASSERT_IF(context->phase() != ImportPhase::Deferred,
                args.get(0).isUndefined());

  if (context->phase() != ImportPhase::Deferred) {
    if (module->status() != ModuleStatus::EvaluatingAsync &&
        module->status() != ModuleStatus::Evaluated) {
      JS_ReportErrorASCII(
          cx, "Unevaluated or errored module returned by module resolve hook");
      return RejectPromiseWithPendingError(cx, promise);
    }
  }

  if (!DynamicImportResolved(cx, context, promise)) {
    return false;
  }

  // Step 6.d.iii. Return NormalCompletion(undefined).
  args.rval().setUndefined();
  return true;
}

// This performs the steps for |rejectedClosure| from
// https://tc39.es/ecma262/#sec-ContinueDynamicImport step 4.
static bool DynamicImportRejected(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue error = args.get(0);

  Rooted<DynamicImportContextObject*> context(
      cx, ExtraFromHandler<DynamicImportContextObject>(args));

  Rooted<PromiseObject*> promise(cx, TargetFromHandler<PromiseObject>(args));

  // Step 4.a. Perform ! Call(promiseCapability.[[Reject]], undefined, [ reason
  // ]).
  if (!PromiseObject::reject(cx, promise, error)) {
    return false;
  }

  // Step 4.b. Return NormalCompletion(undefined).
  args.rval().setUndefined();
  return true;
}

// https://tc39.es/proposal-defer-import-eval/#sec-IsModuleSCCEvaluated
bool js::IsModuleSCCEvaluated(ModuleObject* module) {
  MOZ_ASSERT(module->hasCyclicModuleFields());

  // Step 1. If module.[[CycleRoot]] is not empty, then
  if (module->hasCycleRoot()) {
    // Step 1.a. If module.[[CycleRoot]].[[Status]] is evaluated, return true.
    // Step 1.b. Return false.
    return module->getCycleRoot()->status() == ModuleStatus::Evaluated;
  }

  // Step 2. If module.[[Status]] is evaluated, return true.
  // Step 3. Return false.
  return module->status() == ModuleStatus::Evaluated;
}

// https://tc39.es/proposal-defer-import-eval/#sec-ReadyForSyncExecution
//
// Returns false on failure, with an exception pending. On success, *ready
// holds the result of this operation.
static bool ReadyForSyncExecution(JSContext* cx, Handle<ModuleObject*> module,
                                  MutableHandle<ModuleSet> seen, bool* ready) {
  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  // Step 1. If module is not a Cyclic Module Record, return true.
  if (!module->hasCyclicModuleFields()) {
    *ready = true;
    return true;
  }

  // Step 2. If seen is not present, set seen to a new empty List. The caller
  //         allocates seen.
  // Step 3. If seen contains module, return true.
  auto ptr = seen.lookupForAdd(module);
  if (ptr) {
    *ready = true;
    return true;
  }

  // Step 4. Append module to seen.
  if (!seen.add(ptr, module)) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Step 5. If IsModuleSCCEvaluated(module) is true, return true.
  if (IsModuleSCCEvaluated(module)) {
    *ready = true;
    return true;
  }

  // Step 6. If module.[[Status]] is evaluating or evaluating-async, return
  //         false.
  ModuleStatus status = module->status();
  if (status == ModuleStatus::Evaluating ||
      status == ModuleStatus::EvaluatingAsync) {
    *ready = false;
    return true;
  }

  // Step 7. Assert: module.[[Status]] is linked or evaluated.
  MOZ_ASSERT(status == ModuleStatus::Linked ||
             status == ModuleStatus::Evaluated);

  // Step 8. If module.[[HasTLA]] is true, return false.
  if (module->hasTopLevelAwait()) {
    *ready = false;
    return true;
  }

  // Step 9. For each ModuleRequest Record request of
  //         module.[[RequestedModules]].
  Rooted<ModuleObject*> requiredModule(cx);
  Rooted<ModuleRequestObject*> moduleRequest(cx);
  for (const RequestedModule& request : module->requestedModules()) {
    // Step 9.a. Let requiredModule be the result of calling
    //           GetImportedModule(module, request).
    moduleRequest = request.moduleRequest();
    requiredModule = GetImportedModule(cx, module, moduleRequest);
    MOZ_ASSERT(requiredModule);

    // Step 9.b. If ReadyForSyncExecution(requiredModule, seen) is false, then
    //           return false.
    if (!ReadyForSyncExecution(cx, requiredModule, seen, ready)) {
      return false;
    }
    if (!*ready) {
      return true;
    }
  }

  // Step 10. Return true.
  *ready = true;
  return true;
}

// https://tc39.es/proposal-defer-import-eval/#sec-EvaluateModuleSync
bool js::EvaluateModuleSync(JSContext* cx, Handle<ModuleObject*> module) {
  // Step 1. If ReadyForSyncExecution(module) is false, throw a TypeError
  //         exception.
  Rooted<ModuleSet> seen(cx);
  bool ready = false;
  if (!ReadyForSyncExecution(cx, module, &seen, &ready)) {
    return false;
  }
  if (!ready) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_MODULE_SYNC_EVALUATION_NOT_READY);
    return false;
  }

  // Step 2. Let promise be module.Evaluate().
  Rooted<Value> rval(cx);
  if (!JS::ModuleEvaluate(cx, module, &rval)) {
    return false;
  }

  // Step 3. Assert: promise.[[PromiseState]] is either fulfilled or rejected.
  MOZ_ASSERT(rval.isObject());
  Rooted<PromiseObject*> promise(cx, &rval.toObject().as<PromiseObject>());
  MOZ_ASSERT(promise->state() != JS::PromiseState::Pending);

  // Step 4. If promise.[[PromiseState]] is rejected, then
  if (promise->state() == JS::PromiseState::Rejected) {
    // Step 4.a. If promise.[[PromiseIsHandled]] is false, perform
    //           HostPromiseRejectionTracker(promise, "handle").
    // Step 4.b. Set promise.[[PromiseIsHandled]] to true.
    RootedObject promiseObj(cx, promise);
    if (!JS::SetSettledPromiseIsHandled(cx, promiseObj)) {
      return false;
    }

    // Step 4.c. Return ThrowCompletion(promise.[[PromiseResult]]).
    Rooted<Value> reason(cx, promise->reason());
    cx->setPendingException(reason, ShouldCaptureStack::Maybe);
    return false;
  }

  // Step 5. Return unused.
  return true;
}

// https://tc39.es/ecma262/#sec-ContinueDynamicImport
static bool LinkAndEvaluateDynamicImport(
    JSContext* cx, Handle<DynamicImportContextObject*> context) {
  MOZ_ASSERT(context);
  Rooted<ModuleObject*> module(cx, context->module());
  Rooted<PromiseObject*> promise(cx, context->promise());

  // Step 6.a. Let link be Completion(module.Link()).
  if (!JS::ModuleLink(cx, module)) {
    //   b. If link is an abrupt completion, then
    //      i. Perform ! Call(promiseCapability.[[Reject]], undefined, [
    //         link.[[Value]] ]).
    //      ii. Return unused.
    return RejectPromiseWithPendingError(cx, promise);
  }
  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  JS::Rooted<JSObject*> evaluatePromise(cx);

  // Step 6.e. If phase is defer, then
  if (context->phase() == ImportPhase::Deferred) {
    // Step i. Let evaluationList be
    //         GatherAsynchronousTransitiveDependencies(module).
    Rooted<ModuleSet> seen(cx);
    Rooted<ModuleVector> evaluationList(cx);
    if (!GatherAsynchronousTransitiveDependencies(cx, module, &seen,
                                                  &evaluationList)) {
      return RejectPromiseWithPendingError(cx, promise);
    }

    // Step ii. If evaluationList is empty, then
    //   Step ii.1. Perform fulfilledClosure().
    //   Step ii.2. Return unused.
    if (evaluationList.empty()) {
      return DynamicImportResolved(cx, context, promise);
    }

    // Step iii. Let asyncDepsEvaluationPromises be a new empty List.
    RootedObjectVector asyncDepsEvaluationPromises(cx);
    if (!asyncDepsEvaluationPromises.reserve(evaluationList.length())) {
      return RejectPromiseWithPendingError(cx, promise);
    }

    // Step iv. For each Module Record dep of evaluationList,
    //          append dep.Evaluate() to asyncDepsEvaluationPromises.
    Rooted<ModuleObject*> dep(cx);
    Rooted<JS::Value> rval(cx);
    for (ModuleObject* depObj : evaluationList) {
      dep.set(depObj);
      mozilla::DebugOnly<bool> ok = JS::ModuleEvaluate(cx, dep, &rval);
      MOZ_ASSERT_IF(ok, !JS_IsExceptionPending(cx));
      MOZ_ASSERT_IF(!ok, !rval.isObject());
      if (!rval.isObject()) {
        return RejectPromiseWithPendingError(cx, promise);
      }

      asyncDepsEvaluationPromises.infallibleAppend(&rval.toObject());
    }

    // Step v. Let evaluatePromise be
    //         SafePerformPromiseAll(asyncDepsEvaluationPromises).
    evaluatePromise.set(SafePerformPromiseAll(cx, asyncDepsEvaluationPromises));
    if (!evaluatePromise) {
      return RejectPromiseWithPendingError(cx, promise);
    }
  } else {
    // Step 6.f Else,
    //   Step i. Assert: phase is evaluation.
    MOZ_ASSERT(context->phase() == ImportPhase::Evaluation);

    //   Step ii. Let evaluatePromise be module.Evaluate().
    Rooted<JS::Value> rval(cx);
    mozilla::DebugOnly<bool> ok = JS::ModuleEvaluate(cx, module, &rval);
    MOZ_ASSERT_IF(ok, !JS_IsExceptionPending(cx));
    MOZ_ASSERT_IF(!ok, !rval.isObject());
    if (!rval.isObject()) {
      // If we do not have an evaluation promise or a module request for the
      // module, we can assume that evaluation has failed or been interrupted
      // and can reject the dynamic module.
      return RejectPromiseWithPendingError(cx, promise);
    }

    evaluatePromise.set(&rval.toObject());
    MOZ_ASSERT(evaluatePromise->is<PromiseObject>());
  }

  // Step 6.e. Let onFulfilled be CreateBuiltinFunction(fulfilledClosure, 0, "",
  //           []).
  RootedValue contextValue(cx, ObjectValue(*context));
  RootedFunction onFulfilled(cx);
  onFulfilled = NewHandlerWithExtraValue(cx, DynamicImportResolved, promise,
                                         contextValue);
  if (!onFulfilled) {
    return false;
  }

  // Step 5. Let onRejected be CreateBuiltinFunction(rejectedClosure, 1, "",
  //         []).
  RootedFunction onRejected(cx);
  onRejected = NewHandlerWithExtraValue(cx, DynamicImportRejected, promise,
                                        contextValue);
  if (!onRejected) {
    return false;
  }

  // Step 6.f. Perform PerformPromiseThen(evaluatePromise, onFulfilled,
  //           onRejected).
  // Step 6.g. Return unused.
  return JS::AddPromiseReactionsIgnoringUnhandledRejection(
      cx, evaluatePromise, onFulfilled, onRejected);
}
