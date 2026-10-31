/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JavaScript module (as in, the syntactic construct) operations. */

#ifndef js_Modules_h
#define js_Modules_h

#include <stdint.h>  // uint32_t

#include "jstypes.h"  // JS_PUBLIC_API

#include "js/AllocPolicy.h"     // js::SystemAllocPolicy
#include "js/ColumnNumber.h"    // JS::ColumnNumberOneOrigin
#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions
#include "js/RootingAPI.h"      // JS::{Mutable,}Handle
#include "js/Value.h"           // JS::Value
#include "js/Vector.h"          // js::Vector

struct JS_PUBLIC_API JSContext;
class JS_PUBLIC_API JSObject;
struct JS_PUBLIC_API JSRuntime;
class JS_PUBLIC_API JSString;

namespace JS {
template <typename UnitT>
class SourceText;
}  // namespace JS

namespace mozilla {
union Utf8Unit;
}

namespace JS {

// This enum is used to index into an array, and we assume that we have
// sequential numbers starting at zero for the unknown type.
enum class ModuleType : uint32_t {
  Unknown = 0,
  JavaScript,
  JSON,
  CSS,
  Bytes,
  Text,

  // The specification has renamed the "javascript" module type to
  // "javascript-or-wasm". For now, we'll add JavaScriptOrWasm as
  // an alias of JavaScript. Code that's been updated to handle
  // wasm modules will use JavaScriptOrWasm, other code will continue
  // to use JavaScript. Once everything has been updated to use
  // JavaScriptOrWasm, we'll can just rename JavaScript to JavaScriptOrWasm.
  JavaScriptOrWasm = JavaScript,

  Limit = Text,
};

/**
 * The HostLoadImportedModule hook.
 *
 * See: https://tc39.es/ecma262/#sec-HostLoadImportedModule
 *
 * This embedding-defined hook is used to implement module loading. It is called
 * to get or create a module object corresponding to |moduleRequest| occurring
 * in the context of |referrer|.
 *
 * |referrer| is an opaque JS::Value identifying the referrer of the import. The
 * embedding must pass it back to FinishLoadingImportedModule unchanged.
 *
 * The module specifier string for the request can be obtained by calling
 * JS::GetModuleRequestSpecifier.
 *
 * The private value of the referrer, set with JS::SetScriptPrivate or
 * JS::SetModulePrivate, can be obtained by calling JS::GetReferrerPrivate.
 *
 * If this call succeeds then the embedding must call
 * FinishLoadingImportedModule or one of the FinishLoadingImportedModuleFailed
 * APIs at some point in the future. This is handled by the engine if the call
 * returns false.
 *
 * This hook must obey the restrictions defined in the spec:
 *  - Each time the hook is called with the same referrer, then it must call
 *    FinishLoadingImportedModule with the same result each time.
 *  - The operation must treat the |payload| argument as an opaque
 *    value to be passed through to FinishLoadingImportedModule.
 */
using ModuleLoadHook = bool (*)(JSContext* cx, Handle<Value> referrer,
                                Handle<JSObject*> moduleRequest,
                                Handle<Value> hostDefined,
                                Handle<Value> payload, uint32_t lineNumber,
                                JS::ColumnNumberOneOrigin columnNumber);

/**
 * Get the HostLoadImportedModule hook for the runtime.
 */
extern JS_PUBLIC_API ModuleLoadHook GetModuleLoadHook(JSRuntime* rt);

/**
 * Set the HostLoadImportedModule hook for the runtime to the given function.
 */
extern JS_PUBLIC_API void SetModuleLoadHook(JSRuntime* rt, ModuleLoadHook func);

using LoadModuleResolvedCallback = bool (*)(JSContext* cx,
                                            JS::Handle<JS::Value>);
using LoadModuleRejectedCallback = bool (*)(JSContext* cx,
                                            JS::Handle<JS::Value> hostDefined,
                                            Handle<JS::Value> error);

/**
 * https://tc39.es/ecma262/#sec-LoadRequestedModules
 *
 * Load the dependency module graph of the parameter 'module'.
 *
 * The spec defines using 'promise objects' to notify the result.
 * To address the synchronous loading behavior from mozJSModuleLoader, an
 * overloaded version that takes function callbacks to notify the result is also
 * provided.
 */
extern JS_PUBLIC_API bool LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> module, Handle<Value> hostDefined,
    LoadModuleResolvedCallback resolved, LoadModuleRejectedCallback rejected);

extern JS_PUBLIC_API bool LoadRequestedModules(
    JSContext* cx, Handle<JSObject*> module, Handle<Value> hostDefined,
    MutableHandle<JSObject*> promiseOut);

/**
 * The module metadata hook.
 *
 * See: https://tc39.es/ecma262/#sec-hostgetimportmetaproperties
 *
 * Populate the |metaObject| object returned when import.meta is evaluated in
 * the context of the script or module with private value |privateValue|.
 *
 * This is based on the spec's HostGetImportMetaProperties hook but defines
 * properties on the meta object directly rather than returning a list.
 */
using ModuleMetadataHook = bool (*)(JSContext* cx,
                                    Handle<JSObject*> moduleRecord,
                                    Handle<JSObject*> metaObject);

/**
 * Get the hook for populating the import.meta metadata object.
 */
extern JS_PUBLIC_API ModuleMetadataHook GetModuleMetadataHook(JSRuntime* rt);

/**
 * Set the hook for populating the import.meta metadata object to the given
 * function.
 */
extern JS_PUBLIC_API void SetModuleMetadataHook(JSRuntime* rt,
                                                ModuleMetadataHook func);

/**
 * Get the private value of |referrer| in HostLoadImportedModule hook: the value
 * set with JS::SetModulePrivate for a module referrer or with
 * JS::SetScriptPrivate for a classic script referrer, and undefined otherwise.
 */
extern JS_PUBLIC_API Value GetReferrerPrivate(Handle<Value> referrer);

/**
 * A function callback called by the host layer to indicate the call of
 * HostLoadImportedModule has finished.
 *
 * |referrer| must be the value passed to the HostLoadImportedModule hook.
 *
 * See https://tc39.es/ecma262/#sec-FinishLoadingImportedModule
 */
extern JS_PUBLIC_API bool FinishLoadingImportedModule(
    JSContext* cx, Handle<Value> referrer, Handle<JSObject*> moduleRequest,
    Handle<Value> payload, Handle<JSObject*> result, bool usePromise);

/**
 * Overloaded version of FinishLoadingImportedModule for error handling.
 */
extern JS_PUBLIC_API bool FinishLoadingImportedModuleFailed(
    JSContext* cx, Handle<Value> payload, Handle<Value> error);

extern JS_PUBLIC_API bool FinishLoadingImportedModuleFailedWithPendingException(
    JSContext* cx, Handle<Value> payload);

/**
 * Parse the given source buffer as a module in the scope of the current global
 * of cx and return a source text module record.
 */
extern JS_PUBLIC_API JSObject* CompileModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf);

/**
 * Parse the given source buffer as a module in the scope of the current global
 * of cx and return a source text module record.  An error is reported if a
 * UTF-8 encoding error is encountered.
 */
extern JS_PUBLIC_API JSObject* CompileModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf);

/**
 * Parse the given source buffer as a JSON module in the scope of the current
 * global of cx and return a synthetic module record.
 */
extern JS_PUBLIC_API JSObject* CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<char16_t>& srcBuf);

/**
 * Parse the given source buffer as a JSON module in the scope of the current
 * global of cx and return a synthetic module record. An error is reported if a
 * UTF-8 encoding error is encountered.
 */
extern JS_PUBLIC_API JSObject* CompileJsonModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    SourceText<mozilla::Utf8Unit>& srcBuf);

/**
 * Create a synthetic module record that exports a single value as its default
 * export. The caller is responsible for providing the already-constructed
 * value to export.
 *
 * This matches the ECMAScript specification's definition:
 * https://tc39.es/ecma262/#sec-create-default-export-synthetic-module
 */
extern JS_PUBLIC_API JSObject* CreateDefaultExportSyntheticModule(
    JSContext* cx, Handle<Value> defaultExport);

/**
 * Compile the given wasm source buffer as a module record.
 */
extern JS_PUBLIC_API JSObject* CompileWasmModule(
    JSContext* cx, const ReadOnlyCompileOptions& options,
    js::Vector<uint8_t, 0, js::MallocAllocPolicy>& srcBuf);

/**
 * Create a module record for a WebAssembly.Module object produced by
 * JS::FinishCompileForESM.
 */
extern JS_PUBLIC_API JSObject* CreateWasmModule(
    JSContext* cx, Handle<JSObject*> wasmModuleObject);

/**
 * Set a private value associated with a source text module record.
 */
extern JS_PUBLIC_API void SetModulePrivate(JSObject* module,
                                           const Value& value);
/**
 * Clear the private value associated with a source text module record.
 *
 * This is used during unlinking and can be called on a gray module, skipping
 * the usual checks.
 */
extern JS_PUBLIC_API void ClearModulePrivate(JSObject* module);

/**
 * Get the private value associated with a source text module record.
 */
extern JS_PUBLIC_API Value GetModulePrivate(JSObject* module);

/**
 * Checks if the given module is a cyclic module.
 */
extern JS_PUBLIC_API bool IsCyclicModule(JSObject* module);

#ifdef DEBUG
/**
 * A helper function to set the isPreload flag on the ModuleObject.
 * The flag will be verified later when ResetPreloadedModule is called.
 */
extern JS_PUBLIC_API void SetModulePreload(JSObject* module, bool isPreload);
#endif

/**
 * Set module status to New and clear the [[LoadedModules]] slot and in a Cycloc
 * Module.
 * Used to reset modules that were preloaded earlier, in case the resolution of
 * their specifiers may have changed.
 */
extern JS_PUBLIC_API void ResetPreloadedModule(JSObject* module);

/*
 * Perform the ModuleLink operation on the given source text module record.
 *
 * This transitively resolves all module dependencies (calling the
 * HostResolveImportedModule hook) and initializes the environment record for
 * the module.
 */
extern JS_PUBLIC_API bool ModuleLink(JSContext* cx,
                                     Handle<JSObject*> moduleRecord);

/*
 * Perform the ModuleEvaluate operation on the given source text module record
 * and returns a bool. A result value is returned in result and is either
 * undefined (and ignored) or a promise (if Top Level Await is enabled).
 *
 * If this module has already been evaluated, it returns the evaluation
 * promise. Otherwise, it transitively evaluates all dependences of this module
 * and then evaluates this module.
 *
 * ModuleLink must have completed prior to calling this.
 */
extern JS_PUBLIC_API bool ModuleEvaluate(JSContext* cx,
                                         Handle<JSObject*> moduleRecord,
                                         MutableHandleValue rval);

enum ModuleErrorBehaviour {
  // Report module evaluation errors asynchronously when the evaluation promise
  // is rejected. This is used for web content.
  ReportModuleErrorsAsync,

  // Throw module evaluation errors synchronously by setting an exception on the
  // context. Does not support modules that use top-level await.
  ThrowModuleErrorsSync
};

/*
 * If a module evaluation fails, unwrap the resulting evaluation promise
 * and rethrow.
 *
 * This does nothing if this module succeeds in evaluation. Otherwise, it
 * takes the reason for the module throwing, unwraps it and throws it as a
 * regular error rather than as an uncaught promise.
 *
 * ModuleEvaluate must have completed prior to calling this.
 */
extern JS_PUBLIC_API bool ThrowOnModuleEvaluationFailure(
    JSContext* cx, Handle<JSObject*> evaluationPromise,
    ModuleErrorBehaviour errorBehaviour = ReportModuleErrorsAsync);

/*
 * Get the module type of a requested module.
 */
extern JS_PUBLIC_API ModuleType GetRequestedModuleType(
    JSContext* cx, Handle<JSObject*> moduleRecord, uint32_t index);

/*
 * Get the top-level script for a module which has not yet been executed.
 */
extern JS_PUBLIC_API JSScript* GetModuleScript(Handle<JSObject*> moduleRecord);

extern JS_PUBLIC_API JSString* GetModuleRequestSpecifier(
    JSContext* cx, Handle<JSObject*> moduleRequestArg);

/*
 * Get the module type of the specified module request.
 */
extern JS_PUBLIC_API ModuleType
GetModuleRequestType(JSContext* cx, Handle<JSObject*> moduleRequestArg);

/*
 * Return true if the specified module request is a source phase import.
 */
extern JS_PUBLIC_API bool ModuleRequestIsSourcePhase(
    JSObject* moduleRequestArg);

/*
 * Get the module record for a module script.
 */
extern JS_PUBLIC_API JSObject* GetModuleObject(Handle<JSScript*> moduleScript);

/*
 * Get the namespace object for a module.
 */
extern JS_PUBLIC_API JSObject* GetModuleNamespace(
    JSContext* cx, Handle<JSObject*> moduleRecord);

extern JS_PUBLIC_API JSObject* GetModuleForNamespace(
    JSContext* cx, Handle<JSObject*> moduleNamespace);

extern JS_PUBLIC_API JSObject* GetModuleEnvironment(
    JSContext* cx, Handle<JSObject*> moduleObj);

/*
 * Clear all bindings in a module's environment. Used during shutdown.
 */
extern JS_PUBLIC_API void ClearModuleEnvironment(JSObject* moduleObj);

extern JS_PUBLIC_API bool ModuleIsLinked(JSObject* moduleObj);

}  // namespace JS

#endif  // js_Modules_h
