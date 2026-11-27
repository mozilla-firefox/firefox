/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_loader_WorkerModuleLoader_h
#define mozilla_loader_WorkerModuleLoader_h

#include "js/loader/ModuleLoaderBase.h"
#include "js/loader/ScriptFetchOptions.h"
#include "mozilla/dom/SerializedStackHolder.h"

namespace mozilla::dom::workerinternals::loader {
class WorkerScriptLoader;

// alias common classes
using ScriptFetchOptions = JS::loader::ScriptFetchOptions;
using ScriptKind = JS::loader::ScriptKind;
using ScriptLoadRequest = JS::loader::ScriptLoadRequest;
using ScriptLoadRequestList = JS::loader::ScriptLoadRequestList;
using ModuleLoadRequest = JS::loader::ModuleLoadRequest;

// WorkerModuleLoader
//
// The WorkerModuleLoader provides the methods that implement specification
// step 5 from "To fetch a worklet/module worker script graph", specifically for
// workers. In addition, this implements worker specific initialization for
// Static imports and Dynamic imports.
//
// The steps are outlined in "To fetch the descendants of and link a module
// script" and are common for all Modules. Thus we delegate to ModuleLoaderBase
// for those steps.
class WorkerModuleLoader : public JS::loader::ModuleLoaderBase {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(WorkerModuleLoader,
                                           JS::loader::ModuleLoaderBase)

  WorkerModuleLoader(WorkerScriptLoader* aScriptLoader,
                     nsIGlobalObject* aGlobalObject);

 private:
  ~WorkerModuleLoader() = default;

  nsIURI* GetClientReferrerURI() override;

  already_AddRefed<JS::loader::ScriptFetchOptions>
  CreateDefaultScriptFetchOptions() override;

  already_AddRefed<ModuleLoadRequest> CreateRequest(
      JSContext* aCx, nsIURI* aURI, JS::Handle<JSObject*> aModuleRequest,
      JS::Handle<JS::Value> aHostDefined, JS::Handle<JS::Value> aPayload,
      bool aIsDynamicImport, ScriptFetchOptions* aOptions,
      mozilla::dom::ReferrerPolicy aReferrerPolicy, nsIURI* aBaseURL,
      const mozilla::dom::SRIMetadata& aSriMetadata) override;

  bool CreateDynamicImportLoader();
  void SetScriptLoader(JS::loader::ScriptLoaderInterface* aLoader) {
    mLoader = aLoader;
  }

  WorkerScriptLoader* GetCurrentScriptLoader();

  WorkerScriptLoader* GetScriptLoaderFor(ModuleLoadRequest* aRequest);

  nsIURI* GetBaseURI() const override;

  bool IsDynamicImportSupported() override;

  bool CanStartLoad(ModuleLoadRequest* aRequest, nsresult* aRvOut) override;

  // StartFetch is special for worker modules, as we need to move back to the
  // main thread to start a new load.
  nsresult StartFetch(ModuleLoadRequest* aRequest) override;

  nsresult CompileFetchedModule(
      JSContext* aCx, JS::Handle<JSObject*> aGlobal,
      JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
      JS::MutableHandle<JSObject*> aModuleScript) override;

  nsresult CompileJavaScriptOrWasmModule(
      JSContext* aCx, JS::CompileOptions& aOptions, ModuleLoadRequest* aRequest,
      JS::MutableHandle<JSObject*> aModuleScript);

  nsresult CompileJsonModule(JSContext* aCx, JS::CompileOptions& aOptions,
                             ModuleLoadRequest* aRequest,
                             JS::MutableHandle<JSObject*> aModuleScript);

  nsresult CreateTextModule(JSContext* aCx, JS::CompileOptions& aOptions,
                            ModuleLoadRequest* aRequest,
                            JS::MutableHandle<JSObject*> aModuleScript);

  void OnModuleLoadComplete(ModuleLoadRequest* aRequest) override;

  bool IsModuleEvaluationAborted(ModuleLoadRequest* aRequest) override;

  bool IsModuleTypeAllowed(JS::ModuleType aModuleType) override {
    // https://html.spec.whatwg.org/#module-type-allowed
    // If moduleType is "css" and the CSSStyleSheet interface is not exposed in
    // settings's realm, then return false.
    return aModuleType == JS::ModuleType::JavaScriptOrWasm ||
           aModuleType == JS::ModuleType::JSON ||
           aModuleType == JS::ModuleType::Text;
  }

  virtual bool IsForServiceWorker() const override;
};

}  // namespace mozilla::dom::workerinternals::loader
#endif  // mozilla_loader_WorkerModuleLoader_h
