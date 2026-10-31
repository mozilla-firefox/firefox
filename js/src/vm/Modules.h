/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Modules_h
#define vm_Modules_h

#include "NamespaceImports.h"

#include "builtin/ModuleObject.h"
#include "js/AllocPolicy.h"
#include "js/GCVector.h"
#include "js/RootingAPI.h"

struct JSContext;

namespace js {

using ModuleVector = GCVector<ModuleObject*, 0, SystemAllocPolicy>;

// A struct with detailed error information when import/export failed.
struct ModuleErrorInfo {
  ModuleErrorInfo(uint32_t lineNumber_, JS::ColumnNumberOneOrigin columnNumber_)
      : lineNumber(lineNumber_), columnNumber(columnNumber_) {}

  void setImportedModule(ModuleObject* importedModule);
  void setCircularImport(ModuleObject* importedModule);
  void setForAmbiguousImport(ModuleObject* importedModule,
                             ModuleObject* module1, ModuleObject* module2);

  uint32_t lineNumber;
  JS::ColumnNumberOneOrigin columnNumber;

  // The filename of the imported module.
  const char* imported = nullptr;

  // The filenames of the ambiguous entries.
  const char* entry1 = nullptr;
  const char* entry2 = nullptr;

  // A bool to indicate the error is a circular import when it's true.
  bool isCircular = false;
};

ModuleNamespaceObject* GetOrCreateModuleNamespace(
    JSContext* cx, Handle<ModuleObject*> module,
    ImportPhase phase = ImportPhase::Evaluation);

ModuleObject* GetImportedModule(JSContext* cx, Handle<ModuleObject*> referrer,
                                Handle<ModuleRequestObject*> moduleRequest);

// https://tc39.es/proposal-defer-import-eval/#sec-EvaluateModuleSync
// Synchronously evaluates module, throwing a TypeError before evaluating if
// module's evaluation would not return an already settled promise.
bool EvaluateModuleSync(JSContext* cx, Handle<ModuleObject*> module);

// https://tc39.es/proposal-defer-import-eval/#sec-IsModuleSCCEvaluated
bool IsModuleSCCEvaluated(ModuleObject* module);

void AsyncModuleExecutionFulfilled(JSContext* cx, Handle<ModuleObject*> module);

// This function recusively calls AsyncModuleExecutionRejected on async parent
// modules. It returns false if the stack recusion limit is exceeded.
bool AsyncModuleExecutionRejected(JSContext* cx, Handle<ModuleObject*> module,
                                  HandleValue error);

bool OnModuleEvaluationFailure(JSContext* cx, HandleObject evaluationPromise,
                               JS::ModuleErrorBehaviour errorBehaviour);

bool LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                          HandleValue hostDefined,
                          JS::LoadModuleResolvedCallback resolved,
                          JS::LoadModuleRejectedCallback rejected);

bool LoadRequestedModules(JSContext* cx, Handle<ModuleObject*> module,
                          HandleValue hostDefined,
                          MutableHandle<JSObject*> promiseOut);

ModuleObject* GetImportedModule(JSContext* cx, Handle<ModuleObject*> referrer,
                                Handle<ModuleRequestObject*> moduleRequest);

bool ModuleResolveExport(JSContext* cx, Handle<ModuleObject*> module,
                         Handle<JSAtom*> exportName,
                         MutableHandle<Value> result,
                         ModuleErrorInfo* errorInfoOut = nullptr);

bool IsResolvedBinding(JSContext* cx, Handle<Value> resolution);

bool HostLoadImportedModule(
    JSContext* cx, Handle<Value> referrer, Handle<JSObject*> moduleRequest,
    Handle<Value> hostDefined, Handle<Value> payload, uint32_t lineNumber = 0,
    JS::ColumnNumberOneOrigin columnNumber = JS::ColumnNumberOneOrigin());

}  // namespace js

#endif  // vm_Modules_h
