/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "shell/ModuleLoader.h"

#include "mozilla/TextUtils.h"

#include "jsapi.h"
#include "jsfriendapi.h"
#include "NamespaceImports.h"

#include "builtin/TestingUtility.h"  // js::CreateScriptPrivate
#include "js/Conversions.h"
#include "js/MapAndSet.h"
#include "js/Modules.h"
#include "js/Prefs.h"
#include "js/Promise.h"             // JS::AddPromiseReactions
#include "js/PropertyAndElement.h"  // JS_DefineProperty, JS_GetProperty
#include "js/SourceText.h"
#include "js/StableStringChars.h"
#include "shell/jsshell.h"
#include "shell/OSObject.h"
#include "shell/StringUtils.h"
#include "util/Text.h"
#include "vm/JSAtomUtils.h"  // AtomizeString, PinAtom
#include "vm/JSContext.h"
#include "vm/StringType.h"

#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::shell;

static constexpr char16_t JavaScriptScheme[] = u"javascript:";

static bool IsJavaScriptURL(Handle<JSLinearString*> path) {
  return StringStartsWith(path, JavaScriptScheme);
}

static JSString* ExtractJavaScriptURLSource(JSContext* cx,
                                            Handle<JSLinearString*> path) {
  MOZ_ASSERT(IsJavaScriptURL(path));

  const size_t schemeLength = js_strlen(JavaScriptScheme);
  return SubString(cx, path, schemeLength);
}

bool ModuleLoader::init(JSContext* cx, HandleString loadPath) {
  loadPathStr = AtomizeString(cx, loadPath);
  if (!loadPathStr || !PinAtom(cx, loadPathStr)) {
    return false;
  }

  MOZ_ASSERT(IsAbsolutePath(loadPathStr));

  char16_t sep = PathSeparator;
  pathSeparatorStr = AtomizeChars(cx, &sep, 1);
  if (!pathSeparatorStr || !PinAtom(cx, pathSeparatorStr)) {
    return false;
  }

  JSRuntime* rt = cx->runtime();
  JS::SetModuleLoadHook(rt, ModuleLoader::LoadImportedModule);
  JS::SetModuleMetadataHook(rt, ModuleLoader::GetImportMetaProperties);
  return true;
}

// static
bool ModuleLoader::LoadImportedModule(JSContext* cx,
                                      JS::Handle<JS::Value> referrer,
                                      JS::Handle<JSObject*> moduleRequest,
                                      JS::HandleValue hostDefined,
                                      JS::HandleValue payload,
                                      uint32_t lineNumber,
                                      JS::ColumnNumberOneOrigin columnNumber) {
  ShellContext* scx = GetShellContext(cx);
  return scx->moduleLoader->loadImportedModule(cx, referrer, moduleRequest,
                                               payload);
}

// static
bool ModuleLoader::GetImportMetaProperties(JSContext* cx,
                                           JS::HandleObject moduleRecord,
                                           JS::HandleObject metaObject) {
  ShellContext* scx = GetShellContext(cx);
  return scx->moduleLoader->populateImportMeta(cx, moduleRecord, metaObject);
}

bool ModuleLoader::ImportMetaResolve(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedValue modulePrivate(
      cx, js::GetFunctionNativeReserved(&args.callee(), ModulePrivateSlot));

  // https://html.spec.whatwg.org/#hostgetimportmetaproperties
  // Step 4.1. Set specifier to ? ToString(specifier).
  //
  // https://tc39.es/ecma262/#sec-tostring
  RootedValue v(cx, args.get(ImportMetaResolveSpecifierArg));
  RootedString specifier(cx, JS::ToString(cx, v));
  if (!specifier) {
    return false;
  }

  // Step 4.2, 4.3 are implemented in importMetaResolve.
  ShellContext* scx = GetShellContext(cx);
  RootedString url(cx);
  if (!scx->moduleLoader->importMetaResolve(cx, modulePrivate, specifier,
                                            &url)) {
    return false;
  }

  // Step 4.4. Return the serialization of url.
  args.rval().setString(url);
  return true;
}

static bool OnRootModuleEvaluationSettled(JSContext* cx, unsigned argc,
                                          Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  ShellContext* sc = GetShellContext(cx);
  MOZ_ASSERT(sc->pendingRootModuleEvaluations > 0);
  sc->pendingRootModuleEvaluations--;
  args.rval().setUndefined();
  return true;
}

bool ModuleLoader::loadRootModule(JSContext* cx, HandleString path) {
  Rooted<JSAtom*> specifier(cx, AtomizeString(cx, path));
  if (!specifier) {
    return false;
  }
  RootedObject moduleRequest(
      cx, ModuleRequestObject::create(cx, specifier,
                                      JS::ModuleType::JavaScriptOrWasm,
                                      ImportPhase::Evaluation));
  if (!moduleRequest) {
    return false;
  }

  RootedValue rval(cx);
  if (!loadAndExecute(cx, path, moduleRequest, &rval)) {
    return false;
  }

  RootedObject evaluationPromise(cx, &rval.toObject());
  if (evaluationPromise == nullptr) {
    return false;
  }

  RootedFunction onSettled(
      cx, NewNativeFunction(cx, OnRootModuleEvaluationSettled, 0, nullptr));
  if (!onSettled) {
    return false;
  }
  if (!JS::AddPromiseReactions(cx, evaluationPromise, onSettled, onSettled)) {
    return false;
  }

  GetShellContext(cx)->pendingRootModuleEvaluations++;

  return JS::ThrowOnModuleEvaluationFailure(cx, evaluationPromise);
}

bool ModuleLoader::registerTestModule(JSContext* cx, HandleObject moduleRequest,
                                      Handle<ModuleObject*> module) {
  Rooted<JSLinearString*> path(
      cx, resolve(cx, moduleRequest, JS::UndefinedHandleValue));
  if (!path) {
    return false;
  }

  path = normalizePath(cx, path);
  if (!path) {
    return false;
  }

  JS::ModuleType moduleType =
      moduleRequest->as<ModuleRequestObject>().moduleType();

  RootedObject existing(cx);
  if (!lookupModuleInRegistry(cx, moduleType, path, &existing)) {
    return false;
  }
  if (existing) {
    JS_ReportErrorASCII(cx, "Module is already registered");
    return false;
  }

  return addModuleToRegistry(cx, moduleType, path, module);
}

void ModuleLoader::clearModules(JSContext* cx) {
  Handle<GlobalObject*> global = cx->global();
  global->setReservedSlot(GlobalAppSlotModuleRegistry, UndefinedValue());
}

bool ModuleLoader::loadAndExecute(JSContext* cx, HandleString path,
                                  HandleObject moduleRequestArg,
                                  MutableHandleValue rval) {
  RootedObject module(cx, loadAndParse(cx, path, moduleRequestArg));
  if (!module) {
    return false;
  }

  return loadAndExecute(cx, module, rval);
}

bool ModuleLoader::loadAndExecute(JSContext* cx, HandleObject module,
                                  MutableHandleValue rval) {
  MOZ_ASSERT(module);

  RootedValue hostDefined(cx, ObjectValue(*module));
  if (!JS::LoadRequestedModules(cx, module, hostDefined, LoadResolved,
                                LoadRejected)) {
    return false;
  }

  if (JS_IsExceptionPending(cx)) {
    return false;
  }

  return JS::ModuleEvaluate(cx, module, rval);
}

/* static */
bool ModuleLoader::LoadResolved(JSContext* cx, HandleValue hostDefined) {
  RootedObject module(cx, &hostDefined.toObject());
  return JS::ModuleLink(cx, module);
}

/* static */
bool ModuleLoader::LoadRejected(JSContext* cx, HandleValue hostDefined,
                                HandleValue error) {
  JS_SetPendingException(cx, error);
  return true;
}

// The arguments needed to finish a dynamic import, passed as the |hostDefined|
// value of LoadRequestedModules so that they are available when the requested
// modules have finished loading.
enum DynamicImportClosureSlot {
  ClosureReferrerSlot = 0,
  ClosureModuleRequestSlot,
  ClosurePayloadSlot,
  ClosureModuleSlot,
  DynamicImportClosureSlotCount
};

static const JSClass DynamicImportClosureClass = {
    "DynamicImportClosure",
    JSCLASS_HAS_RESERVED_SLOTS(DynamicImportClosureSlotCount)};

static JSObject* CreateDynamicImportClosure(JSContext* cx, HandleValue referrer,
                                            HandleObject moduleRequest,
                                            HandleValue payload,
                                            HandleObject module) {
  RootedObject closure(
      cx, JS_NewObjectWithGivenProto(cx, &DynamicImportClosureClass, nullptr));
  if (!closure) {
    return nullptr;
  }

  JS_SetReservedSlot(closure, ClosureReferrerSlot, referrer);
  JS_SetReservedSlot(closure, ClosureModuleRequestSlot,
                     ObjectValue(*moduleRequest));
  JS_SetReservedSlot(closure, ClosurePayloadSlot, payload);
  JS_SetReservedSlot(closure, ClosureModuleSlot, ObjectValue(*module));
  return closure;
}

/* static */
bool ModuleLoader::DynamicImportLoadResolved(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject closure(
      cx, &js::GetFunctionNativeReserved(&args.callee().as<JSFunction>(),
                                         LoadReactionHostDefinedSlot)
               .toObject());

  RootedValue referrer(cx, JS::GetReservedSlot(closure, ClosureReferrerSlot));
  RootedObject moduleRequest(
      cx, &JS::GetReservedSlot(closure, ClosureModuleRequestSlot).toObject());
  RootedValue payload(cx, JS::GetReservedSlot(closure, ClosurePayloadSlot));
  RootedObject module(
      cx, &JS::GetReservedSlot(closure, ClosureModuleSlot).toObject());

  if (!JS::FinishLoadingImportedModule(cx, referrer, moduleRequest, payload,
                                       module, /* usePromise = */ true)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

/* static */
bool ModuleLoader::DynamicImportLoadRejected(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject closure(
      cx, &js::GetFunctionNativeReserved(&args.callee().as<JSFunction>(),
                                         LoadReactionHostDefinedSlot)
               .toObject());

  RootedValue payload(cx, JS::GetReservedSlot(closure, ClosurePayloadSlot));
  RootedValue error(cx, args.get(DynamicImportLoadRejectedErrorArg));
  if (!JS::FinishLoadingImportedModuleFailed(cx, payload, error)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

// See https://github.com/tc39/test262/blob/main/INTERPRETING.md#modules
JSObject* ModuleLoader::getOrCreateTest262ModuleSourceModule(JSContext* cx) {
  RootedString key(cx, JS_NewStringCopyZ(cx, "<module source>"));
  if (!key) {
    return nullptr;
  }

  RootedObject module(cx);
  if (!lookupModuleInRegistry(cx, JS::ModuleType::JavaScriptOrWasm, key,
                              &module)) {
    return nullptr;
  }
  if (module) {
    return module;
  }

  // Empty module: The string \0xasm, followed by version number 1.
  // https://webassembly.github.io/spec/core/binary/modules.html#binary-module
  static const uint8_t emptyWasmModule[] = {0x00, 0x61, 0x73, 0x6d,
                                            0x01, 0x00, 0x00, 0x00};
  js::Vector<uint8_t, 0, js::MallocAllocPolicy> srcBuf;
  if (!srcBuf.append(emptyWasmModule, sizeof(emptyWasmModule))) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  JS::CompileOptions options(cx);
  options.setFileAndLine("<module source>", 1);
  module = JS::CompileWasmModule(cx, options, srcBuf);
  if (!module) {
    return nullptr;
  }

  if (!addModuleToRegistry(cx, JS::ModuleType::JavaScriptOrWasm, key, module)) {
    return nullptr;
  }
  return module;
}

static bool IsDynamicImport(HandleValue payload) {
  return payload.isObject() && payload.toObject().is<PromiseObject>();
}

JSObject* ModuleLoader::getOrLoadModule(
    JSContext* cx, JS::HandleValue referrer,
    JS::Handle<JSObject*> moduleRequestArg) {
  Rooted<ModuleRequestObject*> moduleRequest(
      cx, &moduleRequestArg->as<ModuleRequestObject>());

  if (moduleRequest->phase() == ImportPhase::Source &&
      JS::Prefs::experimental_source_phase_imports_test262_module_source() &&
      StringEquals(moduleRequest->specifier(), u"<module source>")) {
    return getOrCreateTest262ModuleSourceModule(cx);
  }

  Rooted<JSLinearString*> path(cx, resolve(cx, moduleRequest, referrer));
  if (!path) {
    return nullptr;
  }

  return loadAndParse(cx, path, moduleRequest);
}

bool ModuleLoader::loadImportedModule(JSContext* cx, JS::HandleValue referrer,
                                      JS::Handle<JSObject*> moduleRequest,
                                      JS::HandleValue payload) {
  RootedObject module(cx, getOrLoadModule(cx, referrer, moduleRequest));
  if (!module) {
    return false;
  }

  // TODO, Bug 1990416: Align the loading descendants behavior of dynamic import
  js::ImportPhase phase = moduleRequest->as<ModuleRequestObject>().phase();
  if (IsDynamicImport(payload) && phase != ImportPhase::Source) {
    RootedObject closure(cx, CreateDynamicImportClosure(
                                 cx, referrer, moduleRequest, payload, module));
    if (!closure) {
      return false;
    }

    RootedFunction onResolved(
        cx, js::NewFunctionWithReserved(cx, DynamicImportLoadResolved,
                                        DynamicImportLoadResolvedNumArgs, 0,
                                        "resolved"));
    if (!onResolved) {
      return false;
    }

    RootedFunction onRejected(
        cx, js::NewFunctionWithReserved(cx, DynamicImportLoadRejected,
                                        DynamicImportLoadRejectedNumArgs, 0,
                                        "rejected"));
    if (!onRejected) {
      return false;
    }

    RootedValue hostDefined(cx, ObjectValue(*closure));
    RootedObject onResolvedObj(cx, JS_GetFunctionObject(onResolved));
    js::SetFunctionNativeReserved(onResolvedObj, LoadReactionHostDefinedSlot,
                                  hostDefined);

    RootedObject onRejectedObj(cx, JS_GetFunctionObject(onRejected));
    js::SetFunctionNativeReserved(onRejectedObj, LoadReactionHostDefinedSlot,
                                  hostDefined);

    RootedObject loadPromise(cx);
    if (!JS::LoadRequestedModules(cx, module, hostDefined, &loadPromise)) {
      return false;
    }

    if (!JS::AddPromiseReactions(cx, loadPromise, onResolvedObj,
                                 onRejectedObj)) {
      return false;
    }

    MOZ_ASSERT(!JS_IsExceptionPending(cx));
    return true;
  }

  return JS::FinishLoadingImportedModule(cx, referrer, moduleRequest, payload,
                                         module, /* usePromise = */ true);
}

bool ModuleLoader::populateImportMeta(JSContext* cx,
                                      JS::HandleObject moduleRecord,
                                      JS::HandleObject metaObject) {
  Rooted<JSLinearString*> path(cx);
  Rooted<JS::Value> modulePrivate(cx, JS::GetModulePrivate(moduleRecord));
  if (!modulePrivate.isUndefined()) {
    if (!getScriptPath(cx, modulePrivate, &path)) {
      return false;
    }
  }

  if (!path) {
    path = NewStringCopyZ<CanGC>(cx, "(unknown)");
    if (!path) {
      return false;
    }
  }

  RootedValue pathValue(cx, StringValue(path));
  if (!JS_DefineProperty(cx, metaObject, "url", pathValue, JSPROP_ENUMERATE)) {
    return false;
  }

  JSFunction* resolveFunc = js::DefineFunctionWithReserved(
      cx, metaObject, "resolve", ImportMetaResolve, ImportMetaResolveNumArgs,
      JSPROP_ENUMERATE);
  if (!resolveFunc) {
    return false;
  }

  RootedObject resolveFuncObj(cx, JS_GetFunctionObject(resolveFunc));
  js::SetFunctionNativeReserved(resolveFuncObj, ModulePrivateSlot,
                                modulePrivate);

  return true;
}

bool ModuleLoader::importMetaResolve(JSContext* cx,
                                     JS::Handle<JS::Value> referencingPrivate,
                                     JS::Handle<JSString*> specifier,
                                     JS::MutableHandle<JSString*> urlOut) {
  Rooted<JSLinearString*> path(cx, resolve(cx, specifier, referencingPrivate));
  if (!path) {
    return false;
  }

  urlOut.set(path);
  return true;
}

JSLinearString* ModuleLoader::resolve(JSContext* cx,
                                      HandleObject moduleRequestArg,
                                      HandleValue referrer) {
  RootedValue referencingInfo(cx, JS::GetReferrerPrivate(referrer));

  ModuleRequestObject* moduleRequest =
      &moduleRequestArg->as<ModuleRequestObject>();
  if (moduleRequest->specifier()->length() == 0) {
    JS_ReportErrorASCII(cx, "Invalid module specifier");
    return nullptr;
  }

  Rooted<JSLinearString*> name(
      cx, JS_EnsureLinearString(cx, moduleRequest->specifier()));
  if (!name) {
    return nullptr;
  }

  return resolve(cx, name, referencingInfo);
}

JSLinearString* ModuleLoader::resolve(JSContext* cx, HandleString specifier,
                                      HandleValue referencingInfo) {
  Rooted<JSLinearString*> name(cx, JS_EnsureLinearString(cx, specifier));
  if (!name) {
    return nullptr;
  }

  if (IsJavaScriptURL(name) || IsAbsolutePath(name)) {
    return name;
  }

  // Treat |name| as a relative path if it starts with either "./" or "../".
  bool isRelative =
      StringStartsWith(name, u"./") || StringStartsWith(name, u"../")
#ifdef XP_WIN
      || StringStartsWith(name, u".\\") || StringStartsWith(name, u"..\\")
#endif
      ;

  RootedString path(cx, loadPathStr);

  if (isRelative) {
    if (referencingInfo.isUndefined()) {
      JS_ReportErrorASCII(cx, "No referencing module for relative import");
      return nullptr;
    }

    Rooted<JSLinearString*> refPath(cx);
    if (!getScriptPath(cx, referencingInfo, &refPath)) {
      return nullptr;
    }

    if (!refPath) {
      JS_ReportErrorASCII(cx, "No path set for referencing module");
      return nullptr;
    }

    int32_t sepIndex = LastIndexOf(refPath, u'/');
#ifdef XP_WIN
    sepIndex = std::max(sepIndex, LastIndexOf(refPath, u'\\'));
#endif
    if (sepIndex >= 0) {
      path = SubString(cx, refPath, 0, sepIndex);
      if (!path) {
        return nullptr;
      }
    }
  }

  RootedString result(cx);
  RootedString pathSep(cx, pathSeparatorStr);
  result = JS_ConcatStrings(cx, path, pathSep);
  if (!result) {
    return nullptr;
  }

  result = JS_ConcatStrings(cx, result, name);
  if (!result) {
    return nullptr;
  }

  Rooted<JSLinearString*> linear(cx, JS_EnsureLinearString(cx, result));
  if (!linear) {
    return nullptr;
  }
  return normalizePath(cx, linear);
}

JSObject* ModuleLoader::loadAndParse(JSContext* cx, HandleString pathArg,
                                     JS::HandleObject moduleRequestArg) {
  Rooted<JSLinearString*> path(cx, JS_EnsureLinearString(cx, pathArg));
  if (!path) {
    return nullptr;
  }

  path = normalizePath(cx, path);
  if (!path) {
    return nullptr;
  }

  JS::ModuleType moduleType =
      moduleRequestArg->as<ModuleRequestObject>().moduleType();
  if (moduleType == JS::ModuleType::Unknown ||
      moduleType == JS::ModuleType::CSS) {
    // We don't support CSS modules in the shell because we don't have access
    // to a CSS parser in standalone shell builds.
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_MODULE_TYPE);
    return nullptr;
  }

  RootedObject module(cx);
  if (!lookupModuleInRegistry(cx, moduleType, path, &module)) {
    return nullptr;
  }

  if (module) {
    // TODO: Until we support evaluation phase imports of wasm modules, we need
    // to guard against first importing a wasm module as source, and then
    // subsequently as evaluation phase. The module will be retrieved from the
    // registry, and then we'll attempt to link it, which isn't currently
    // supported. See Bug 2030454.
    if (moduleRequestArg->as<ModuleRequestObject>().phase() ==
            ImportPhase::Evaluation &&
        module->as<ModuleObject>().moduleSource()) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_WASM_ESM_EVAL_NOT_SUPPORTED);
      return nullptr;
    }
    return module;
  }

  UniqueChars filename = JS_EncodeStringToUTF8(cx, path);
  if (!filename) {
    return nullptr;
  }

  if (moduleType == JS::ModuleType::Bytes) {
    RootedString resolvedPath(cx, ResolvePath(cx, path, RootRelative));
    if (!resolvedPath) {
      return nullptr;
    }

    auto* typedArray = FileAsImmutableTypedArray(cx, resolvedPath);
    if (!typedArray) {
      return nullptr;
    }
    JS::Rooted<JS::Value> defaultExport(cx, ObjectValue(*typedArray));

    module = JS::CreateDefaultExportSyntheticModule(cx, defaultExport);
    if (!module) {
      return nullptr;
    }

    if (!addModuleToRegistry(cx, moduleType, path, module)) {
      return nullptr;
    }

    return module;
  }

  // Normally the mime type determines whether a module is wasm or not, but
  // this doesn't exist in the shell. Instead, we'll use the file extension.
#ifdef NIGHTLY_BUILD
  if (JS::Prefs::experimental_wasm_esm_integration() &&
      StringEndsWith(path, u".wasm")) {
    js::ImportPhase phase = moduleRequestArg->as<ModuleRequestObject>().phase();
    if (phase != ImportPhase::Source) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_WASM_ESM_EVAL_NOT_SUPPORTED);
      return nullptr;
    }
    RootedString resolvedPath(cx, ResolvePath(cx, path, RootRelative));
    if (!resolvedPath) {
      return nullptr;
    }

    UniqueChars resolvedFilename = JS_EncodeStringToUTF8(cx, resolvedPath);
    if (!resolvedFilename) {
      return nullptr;
    }

    FILE* file = OpenFile(cx, resolvedFilename.get(), "rb");
    if (!file) {
      return nullptr;
    }

    size_t fileSize;
    if (!FileSize(cx, resolvedFilename.get(), file, &fileSize)) {
      fclose(file);
      return nullptr;
    }

    js::Vector<uint8_t, 0, js::MallocAllocPolicy> srcBuf;
    if (!srcBuf.growBy(fileSize)) {
      fclose(file);
      ReportOutOfMemory(cx);
      return nullptr;
    }

    if (!ReadFile(cx, resolvedFilename.get(), file,
                  reinterpret_cast<char*>(srcBuf.begin()), fileSize)) {
      fclose(file);
      return nullptr;
    }
    fclose(file);

    JS::CompileOptions options(cx);
    options.setFileAndLine(filename.get(), 1);
    module = JS::CompileWasmModule(cx, options, srcBuf);
    if (!module) {
      return nullptr;
    }

    if (!addModuleToRegistry(cx, moduleType, path, module)) {
      return nullptr;
    }

    return module;
  }
#endif
  JS::CompileOptions options(cx);
  options.setFileAndLine(filename.get(), 1);

  RootedString source(cx, fetchSource(cx, path));
  if (!source) {
    return nullptr;
  }

  if (moduleType == JS::ModuleType::Text) {
    JS::RootedValue defaultExport(cx, JS::StringValue(source));
    module = JS::CreateDefaultExportSyntheticModule(cx, defaultExport);
    if (!module) {
      return nullptr;
    }

    if (!addModuleToRegistry(cx, moduleType, path, module)) {
      return nullptr;
    }

    return module;
  }

  JS::AutoStableStringChars linearChars(cx);
  if (!linearChars.initTwoByte(cx, source)) {
    return nullptr;
  }

  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.initMaybeBorrowed(cx, linearChars)) {
    return nullptr;
  }

  if (moduleType == JS::ModuleType::JavaScriptOrWasm) {
    module = JS::CompileModule(cx, options, srcBuf);
    if (!module) {
      return nullptr;
    }

    RootedObject info(cx, js::CreateScriptPrivate(cx, path));
    if (!info) {
      return nullptr;
    }

    JS::SetModulePrivate(module, ObjectValue(*info));
  } else {
    MOZ_ASSERT(moduleType == JS::ModuleType::JSON);
    module = JS::CompileJsonModule(cx, options, srcBuf);
    if (!module) {
      return nullptr;
    }
  }

  if (!addModuleToRegistry(cx, moduleType, path, module)) {
    return nullptr;
  }

  return module;
}

bool ModuleLoader::lookupModuleInRegistry(JSContext* cx,
                                          JS::ModuleType moduleType,
                                          HandleString path,
                                          MutableHandleObject moduleOut) {
  moduleOut.set(nullptr);

  RootedObject registry(cx, getOrCreateModuleRegistry(cx, moduleType));
  if (!registry) {
    return false;
  }

  RootedValue pathValue(cx, StringValue(path));
  RootedValue moduleValue(cx);
  if (!JS::MapGet(cx, registry, pathValue, &moduleValue)) {
    return false;
  }

  if (!moduleValue.isUndefined()) {
    moduleOut.set(&moduleValue.toObject());
  }

  return true;
}

bool ModuleLoader::addModuleToRegistry(JSContext* cx, JS::ModuleType moduleType,
                                       HandleString path, HandleObject module) {
  RootedObject registry(cx, getOrCreateModuleRegistry(cx, moduleType));
  if (!registry) {
    return false;
  }

  RootedValue pathValue(cx, StringValue(path));
  RootedValue moduleValue(cx, ObjectValue(*module));
  return JS::MapSet(cx, registry, pathValue, moduleValue);
}

static ArrayObject* GetOrCreateRootRegistry(JSContext* cx) {
  Handle<GlobalObject*> global = cx->global();
  RootedValue value(cx, global->getReservedSlot(GlobalAppSlotModuleRegistry));
  if (!value.isUndefined()) {
    return &value.toObject().as<ArrayObject>();
  }

  uint32_t numberOfModuleTypes = uint32_t(JS::ModuleType::Limit) + 1;

  Rooted<ArrayObject*> registry(
      cx, NewDenseFullyAllocatedArray(cx, numberOfModuleTypes, TenuredObject));
  if (!registry) {
    return nullptr;
  }
  registry->ensureDenseInitializedLength(0, numberOfModuleTypes);

  Rooted<JSObject*> innerRegistry(cx);
  for (size_t i = 0; i < numberOfModuleTypes; ++i) {
    innerRegistry = JS::NewMapObject(cx);
    if (!innerRegistry) {
      return nullptr;
    }
    registry->initDenseElement(i, ObjectValue(*innerRegistry));
  }

  global->setReservedSlot(GlobalAppSlotModuleRegistry, ObjectValue(*registry));

  return registry;
}

JSObject* ModuleLoader::getOrCreateModuleRegistry(JSContext* cx,
                                                  JS::ModuleType moduleType) {
  Rooted<ArrayObject*> rootRegistry(cx, GetOrCreateRootRegistry(cx));
  if (!rootRegistry) {
    return nullptr;
  }

  uint32_t index = uint32_t(moduleType);
  MOZ_ASSERT(rootRegistry->containsDenseElement(index));
  return &rootRegistry->getDenseElement(index).toObject();
}

bool ModuleLoader::getScriptPath(JSContext* cx, HandleValue privateValue,
                                 MutableHandle<JSLinearString*> pathOut) {
  pathOut.set(nullptr);

  RootedObject infoObj(cx, &privateValue.toObject());
  RootedValue pathValue(cx);
  if (!JS_GetProperty(cx, infoObj, "path", &pathValue)) {
    return false;
  }

  if (pathValue.isUndefined()) {
    return true;
  }

  RootedString path(cx, pathValue.toString());
  pathOut.set(JS_EnsureLinearString(cx, path));
  return pathOut;
}

JSLinearString* ModuleLoader::normalizePath(JSContext* cx,
                                            Handle<JSLinearString*> pathArg) {
  Rooted<JSLinearString*> path(cx, pathArg);

  if (IsJavaScriptURL(path)) {
    return path;
  }

#ifdef XP_WIN
  // Replace all forward slashes with backward slashes.
  path = ReplaceCharGlobally(cx, path, u'/', PathSeparator);
  if (!path) {
    return nullptr;
  }

  // Remove the drive letter, if present.
  Rooted<JSLinearString*> drive(cx);
  if (path->length() > 2 && mozilla::IsAsciiAlpha(CharAt(path, 0)) &&
      CharAt(path, 1) == u':' && CharAt(path, 2) == u'\\') {
    drive = SubString(cx, path, 0, 2);
    path = SubString(cx, path, 2);
    if (!drive || !path) {
      return nullptr;
    }
  }
#endif  // XP_WIN

  // Normalize the path by removing redundant path components.
  Rooted<GCVector<JSLinearString*>> components(cx, cx);
  size_t lastSep = 0;
  while (lastSep < path->length()) {
    int32_t i = IndexOf(path, PathSeparator, lastSep);
    if (i < 0) {
      i = path->length();
    }

    Rooted<JSLinearString*> part(cx, SubString(cx, path, lastSep, i));
    if (!part) {
      return nullptr;
    }

    lastSep = i + 1;

    // Remove "." when preceded by a path component.
    if (StringEquals(part, u".") && !components.empty()) {
      continue;
    }

    if (StringEquals(part, u"..") && !components.empty()) {
      // Replace "./.." with "..".
      if (StringEquals(components.back(), u".")) {
        components.back() = part;
        continue;
      }

      // When preceded by a non-empty path component, remove ".." and the
      // preceding component, unless the preceding component is also "..".
      if (!StringEquals(components.back(), u"") &&
          !StringEquals(components.back(), u"..")) {
        components.popBack();
        continue;
      }
    }

    if (!components.append(part)) {
      return nullptr;
    }
  }

  Rooted<JSLinearString*> pathSep(cx, pathSeparatorStr);
  RootedString normalized(cx, JoinStrings(cx, components, pathSep));
  if (!normalized) {
    return nullptr;
  }

#ifdef XP_WIN
  if (drive) {
    normalized = JS_ConcatStrings(cx, drive, normalized);
    if (!normalized) {
      return nullptr;
    }
  }
#endif

  return JS_EnsureLinearString(cx, normalized);
}

JSString* ModuleLoader::fetchSource(JSContext* cx,
                                    Handle<JSLinearString*> path) {
  if (IsJavaScriptURL(path)) {
    return ExtractJavaScriptURLSource(cx, path);
  }

  RootedString resolvedPath(cx, ResolvePath(cx, path, RootRelative));
  if (!resolvedPath) {
    return nullptr;
  }

  return FileAsString(cx, resolvedPath);
}
