/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_ModuleLoadRequest_h
#define js_loader_ModuleLoadRequest_h

#include "mozilla/Assertions.h"
#include "mozilla/HoldDropJSObjects.h"

#include "LoadContextBase.h"
#include "ModuleLoaderBase.h"
#include "nsTHashtable.h"
#include "nsURIHashKey.h"
#include "ScriptLoadRequest.h"

#include "js/RootingAPI.h"
#include "js/Value.h"

namespace JS::loader {

class LoadedScript;
class ModuleScript;
class ModuleLoaderBase;

// A load request for a module, created for every top level module script and
// every module import.  Load request can share an ModuleScript if there are
// multiple imports of the same module.

class ModuleLoadRequest final : public ScriptLoadRequest {
  ~ModuleLoadRequest();

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(ModuleLoadRequest,
                                                         ScriptLoadRequest)
  using SRIMetadata = mozilla::dom::SRIMetadata;

  enum class Kind {
    // Top-level modules, not imported statically nor dynamically.
    TopLevel,

    // Modules imported statically with `import` declarations.
    StaticImport,

    // Modules imported dynamically with dynamic `import()`.
    DynamicImport,
  };

  ModuleLoadRequest(ModuleType aModuleType, const SRIMetadata& aIntegrity,
                    nsIURI* aReferrer, LoadContextBase* aContext, Kind aKind,
                    ModuleLoaderBase* aLoader, ModuleLoadRequest* aRootModule);
  ModuleLoadRequest(const ModuleLoadRequest& aOther) = delete;
  ModuleLoadRequest(ModuleLoadRequest&& aOther) = delete;

  bool IsTopLevel() const override { return mKind == Kind::TopLevel; }
  bool IsStaticImport() const { return mKind == Kind::StaticImport; }
  bool IsDynamicImport() const { return mKind == Kind::DynamicImport; }

  bool IsSourcePhaseRequest() const;

  bool IsErrored() const;

  nsIGlobalObject* GetGlobalObject();

  void SetReady() override;
  void Cancel() override { mLoader->Cancel(this); };

  void SetImport(Handle<Value> aReferrer, Handle<JSObject*> aModuleRequestObj,
                 Handle<Value> aPayload);
  void ClearImport();

  void ModuleLoaded();
  void ModuleErrored();

  // Tells the load context that this request stopped waiting on an in-progress
  // fetch of the same URL. Must be called whenever that happens, whether the
  // fetch resolved or was canceled.
  void NotifyModuleWaitFinished();

  ModuleLoadRequest* GetRootModule() {
    if (!mRootModule) {
      return this;
    }
    return mRootModule;
  }

  // Convenience methods to call into the module loader for this request.

  void CancelDynamicImport(nsresult aResult) {
    MOZ_ASSERT(IsDynamicImport());
    mLoader->CancelDynamicImport(this, aResult);
  }
#ifdef DEBUG
  bool IsRegisteredDynamicImport() const {
    return IsDynamicImport() && mLoader->HasDynamicImport(this);
  }
#endif
  nsresult StartModuleLoad() { return mLoader->StartModuleLoad(this); }
  nsresult RestartModuleLoad() { return mLoader->RestartModuleLoad(this); }
  void OnFetchComplete(nsresult aRv) { mLoader->OnFetchComplete(this, aRv); }
  bool InstantiateModuleGraph() {
    return mLoader->InstantiateModuleGraph(this);
  }
  nsresult EvaluateModule() { return mLoader->EvaluateModule(this); }
  void ProcessDynamicImport() { mLoader->ProcessDynamicImport(this); }

  void LoadFinished();

#ifdef NIGHTLY_BUILD
  void SetHasWasmMimeTypeEssence() { mHasWasmMimeTypeEssence = true; }

  bool HasWasmMimeTypeEssence() { return mHasWasmMimeTypeEssence; }
#endif

  void SetErroredLoadingImports() {
    MOZ_ASSERT(IsDynamicImport());
    MOZ_ASSERT(IsFetching() || IsCompiling());
    mErroredLoadingImports = true;
  }

  bool IsErroredLoadingImports() const { return mErroredLoadingImports; }

  void UpdateReferrerPolicy(mozilla::dom::ReferrerPolicy aReferrerPolicy) {
    FetchInfo()->UpdateReferrerPolicy(aReferrerPolicy);
  }

 public:
  // Fields.
  const Kind mKind;

  // Type of module (JavaScript, JSON)
  const ModuleType mModuleType;

  // Is this the top level request for a dynamic module import?
  const bool mIsDynamicImport;

#ifdef NIGHTLY_BUILD
  // We can only distinguish JavaScript and Wasm modules by mime type essence.
  bool mHasWasmMimeTypeEssence = false;
#endif

  // A flag (for dynamic import) that indicates the module script is
  // successfully fetched and compiled, but its dependencies are failed to load.
  bool mErroredLoadingImports;

  // Pointer to the script loader, used to trigger actions when the module load
  // finishes.
  RefPtr<ModuleLoaderBase> mLoader;

  // Pointer to the top level module of this module graph, nullptr if this is a
  // top level module
  RefPtr<ModuleLoadRequest> mRootModule;

  // Set to a module script object after a successful load or nullptr on
  // failure.
  RefPtr<ModuleScript> mModuleScript;

  // The referrer value passed to the HostLoadImportedModule hook, to be passed
  // back to FinishLoadingImportedModule.
  Heap<Value> mReferrerValue;
  Heap<JSObject*> mModuleRequestObj;
  Heap<Value> mPayload;
};

}  // namespace JS::loader

#endif  // js_loader_ModuleLoadRequest_h
