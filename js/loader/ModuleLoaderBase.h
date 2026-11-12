/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_loader_ModuleLoaderBase_h
#define js_loader_ModuleLoaderBase_h

#include "mozilla/Attributes.h"  // MOZ_RAII
#include "mozilla/CORSMode.h"
#include "mozilla/dom/ReferrerPolicyBinding.h"
#include "mozilla/MaybeOneOf.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/UniquePtr.h"

#include "ImportMap.h"
#include "LoadedScript.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsILoadInfo.h"  // nsSecurityFlags
#include "nsRefPtrHashtable.h"
#include "nsThreadUtils.h"  // GetMainThreadSerialEventTarget
#include "nsURIHashKey.h"
#include "ResolvedModuleSet.h"
#include "ResolveResult.h"
#include "ScriptLoadRequestList.h"

#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/Modules.h"
#include "js/TypeDecls.h"  // JS::MutableHandle, JS::Handle, JS::Root

class nsIConsoleReportCollector;
class nsIURI;

namespace mozilla {

class LazyLogModule;
union Utf8Unit;

namespace dom {
class SRIMetadata;
}  // namespace dom

}  // namespace mozilla

namespace JS {

class CompileOptions;

template <typename UnitT>
class SourceText;

namespace loader {

class LoadContextBase;
class ModuleLoaderBase;
class ModuleLoadRequest;
class ModuleScript;
class ScriptLoaderInterface;

class ModuleMapKey : public PLDHashEntryHdr {
 public:
  using KeyType = const ModuleMapKey&;
  using KeyTypePointer = const ModuleMapKey*;

  ModuleMapKey(const nsIURI* aUri, const ModuleType aModuleType)
      : mUri(const_cast<nsIURI*>(aUri)), mModuleType(aModuleType) {
    MOZ_COUNT_CTOR(ModuleMapKey);
    MOZ_ASSERT(aUri);
  }
  explicit ModuleMapKey(KeyTypePointer aOther)
      : mUri(std::move(aOther->mUri)), mModuleType(aOther->mModuleType) {
    MOZ_COUNT_CTOR(ModuleMapKey);
    MOZ_ASSERT(mUri);
  }
  ModuleMapKey(ModuleMapKey&& aOther)
      : mUri(std::move(aOther.mUri)), mModuleType(aOther.mModuleType) {
    MOZ_COUNT_CTOR(ModuleMapKey);
    MOZ_ASSERT(mUri);
  }
  MOZ_COUNTED_DTOR(ModuleMapKey)

  bool KeyEquals(KeyTypePointer aKey) const {
    if (mModuleType != aKey->mModuleType) {
      return false;
    }

    bool eq;
    if (NS_SUCCEEDED(mUri->Equals(aKey->mUri, &eq))) {
      return eq;
    }

    return false;
  }

  static KeyTypePointer KeyToPointer(KeyType key) { return &key; }

  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    MOZ_ASSERT(aKey->mUri);
    nsAutoCString spec;
    // This is based on `nsURIHashKey`, and it ignores GetSpec() failures, so
    // do the same here.
    (void)aKey->mUri->GetSpec(spec);
    return mozilla::HashGeneric(mozilla::HashString(spec), aKey->mModuleType);
  }

  enum { ALLOW_MEMMOVE = true };

  const nsCOMPtr<nsIURI> mUri;
  const ModuleType mModuleType;
};

/*
 * [DOMDOC] Module Loading
 *
 * ModuleLoaderBase provides support for loading module graphs as defined in the
 * EcmaScript specification. A derived module loader class must be created for a
 * specific use case (for example loading HTML module scripts). The derived
 * class provides operations such as fetching of source code and scheduling of
 * module execution.
 *
 * Module loading works in terms of 'requests' which hold data about modules as
 * they move through the loading process. There may be more than one load
 * request active for a single module URI, but the module is only loaded
 * once. This is achieved by tracking all fetching and fetched modules in the
 * module map.
 *
 * The module map is made up of two parts. A module that has been requested but
 * has not finished fetching is represented by an entry in the mFetchingModules
 * map. A module which has been fetched and compiled is represented by a
 * ModuleScript in the mFetchedModules map.
 *
 * Module loading typically works as follows:
 *
 * 1.  The client ensures there is an instance of the derived module loader
 *     class for its global or creates one if necessary.
 *
 * 2.  The client creates a ModuleLoadRequest object for the module to load and
 *     calls the loader's StartModuleLoad() method. This is a top-level request,
 *     i.e. not an import.
 *
 * 3.  The module loader calls the virtual method CanStartLoad() to check
 *     whether the request should be loaded.
 *
 * 4.  If the module is not already present in the module map, the loader calls
 *     the virtual method StartFetch() to set up an asynchronous operation to
 *     fetch the module source.
 *
 * 5.  When the fetch operation is complete, the derived loader calls
 *     OnFetchComplete() passing an error code to indicate success or failure.
 *     OnFetchComplete() completes the request in either case.
 *
 * 6.  On success, the loader attempts to create a module script by calling the
 *     virtual CompileFetchedModule() method.
 *
 * 7.  If compilation is successful, the loader creates load requests for any
 *     imported modules if present. If so, the process repeats from step 3.
 *
 * 8.  When a load request is completed, the virtual OnModuleLoadComplete()
 *     method is called. This is called for the top-level request and import
 *     requests.
 *
 * 9.  The client calls InstantiateModuleGraph() for the top-level request. This
 *     links the loaded module graph.
 *
 * 10. The client calls EvaluateModule() to execute the top-level module.
 */
class ModuleLoaderBase : public nsISupports {
 public:
  // Alias common classes.
  using LoadedScript = JS::loader::LoadedScript;
  using ScriptFetchOptions = JS::loader::ScriptFetchOptions;
  using ScriptLoadRequest = JS::loader::ScriptLoadRequest;
  using ModuleLoadRequest = JS::loader::ModuleLoadRequest;

 private:
  /*
   * Represents an ongoing load operation for a URI initiated for one request
   * and which may have other requests waiting for it to complete.
   *
   * These are tracked in the mFetchingModules map.
   */
  class LoadingRequest final : public nsISupports {
    ~LoadingRequest() = default;

   public:
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
    NS_DECL_CYCLE_COLLECTION_CLASS(LoadingRequest)

    // The request that initiated the load and which is currently fetching or
    // being compiled.
    RefPtr<ModuleLoadRequest> mRequest;

    // A list of any other requests for the same URI that are waiting for the
    // initial load to complete. These will be resumed by ResumeWaitingRequests
    // when that happens.
    nsTArray<RefPtr<ModuleLoadRequest>> mWaiting;
  };

  // Module map
  nsRefPtrHashtable<ModuleMapKey, LoadingRequest> mFetchingModules;
  nsRefPtrHashtable<ModuleMapKey, ModuleScript> mFetchedModules;

  // List of dynamic imports that are currently being loaded.
  ScriptLoadRequestList mDynamicImportRequests;

  nsCOMPtr<nsIGlobalObject> mGlobalObject;

  // If non-null, this module loader is overridden by the module loader pointed
  // by mOverriddenBy.
  // See ModuleLoaderBase::GetCurrentModuleLoader for more details.
  RefPtr<ModuleLoaderBase> mOverriddenBy;

  // https://html.spec.whatwg.org/multipage/webappapis.html#import-maps-allowed
  //
  // Each Window has an import maps allowed boolean, initially true.
  bool mImportMapsAllowed = true;

 protected:
  RefPtr<ScriptLoaderInterface> mLoader;

  mozilla::UniquePtr<ImportMap> mImportMap;

  mozilla::UniquePtr<ResolvedModuleSet> mResolvedModuleSet;

  virtual ~ModuleLoaderBase();

#ifdef DEBUG
  const ScriptLoadRequestList& DynamicImportRequests() const {
    return mDynamicImportRequests;
  }
#endif

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(ModuleLoaderBase)
  explicit ModuleLoaderBase(ScriptLoaderInterface* aLoader,
                            nsIGlobalObject* aGlobalObject);

  void CancelFetchingModules();

  // Called to break cycles during shutdown to prevent memory leaks.
  void Shutdown();

  virtual nsIURI* GetBaseURI() const;

  using MaybeSourceText =
      mozilla::MaybeOneOf<SourceText<char16_t>, SourceText<Utf8Unit>>;

  // Methods that must be implemented by an extending class. These are called
  // internally by ModuleLoaderBase.

 private:
  // Determine the environment's referrer when the referrer script is empty.
  //
  // https://html.spec.whatwg.org/#hostloadimportedmodule
  //   Step 5. Let fetchReferrer be "client".
  //
  // Then check the referrer policy specification for determining the referrer.
  // https://w3c.github.io/webappsec-referrer-policy/#determine-requests-referrer
  virtual nsIURI* GetClientReferrerURI() { return nullptr; }

  // Create default ScriptFetchOptions if the referrer script is empty.
  virtual already_AddRefed<JS::loader::ScriptFetchOptions>
  CreateDefaultScriptFetchOptions() {
    return nullptr;
  }

  // Create a ModuleLoadRequest for static/dynamic imports.
  virtual already_AddRefed<ModuleLoadRequest> CreateRequest(
      JSContext* aCx, nsIURI* aURI, Handle<JSObject*> aModuleRequest,
      Handle<Value> aHostDefined, Handle<Value> aPayload, bool aIsDynamicImport,
      ScriptFetchOptions* aOptions,
      mozilla::dom::ReferrerPolicy aReferrerPolicy, nsIURI* aBaseURL,
      const mozilla::dom::SRIMetadata& aSriMetadata) = 0;

  virtual bool IsDynamicImportSupported() { return true; }

  virtual bool IsForServiceWorker() const { return false; }

  // Called when dynamic import started successfully.
  virtual void OnDynamicImportStarted(ModuleLoadRequest* aRequest) {}

  // Check whether we can load a module. May return false with |aRvOut| set to
  // NS_OK to abort load without returning an error.
  virtual bool CanStartLoad(ModuleLoadRequest* aRequest, nsresult* aRvOut) = 0;

  // Start the process of fetching module source or serialized stencil. This is
  // only called if CanStartLoad returned true.
  virtual nsresult StartFetch(ModuleLoadRequest* aRequest) = 0;

  // Create a JS module for a fetched module request. This might compile source
  // text or decode stencil.
  virtual nsresult CompileFetchedModule(
      JSContext* aCx, Handle<JSObject*> aGlobal, CompileOptions& aOptions,
      ModuleLoadRequest* aRequest, MutableHandle<JSObject*> aModuleOut) = 0;

  // Called when a module script has been loaded, including imports.
  virtual void OnModuleLoadComplete(ModuleLoadRequest* aRequest) = 0;

  virtual bool IsModuleEvaluationAborted(ModuleLoadRequest* aRequest) {
    return false;
  }

  // Get the error message when resolving failed. The default is to call
  // nsContentUtils::FormatLoalizedString. But currently
  // nsContentUtils::FormatLoalizedString cannot be called on a worklet thread,
  // see bug 1808301. So WorkletModuleLoader will override this function to
  // get the error message.
  virtual nsresult GetResolveFailureMessage(ResolveError aError,
                                            const nsAString& aSpecifier,
                                            nsAString& aResult);

  // Public API methods.

 public:
  ScriptLoaderInterface* GetScriptLoaderInterface() const { return mLoader; }

  nsIGlobalObject* GetGlobalObject() const { return mGlobalObject; }

  bool HasFetchingModules() const;

  bool HasPendingDynamicImports() const;
  void CancelDynamicImport(ModuleLoadRequest* aRequest, nsresult aResult);
#ifdef DEBUG
  bool HasDynamicImport(const ModuleLoadRequest* aRequest) const;
#endif

  // Start a load for a module script URI. Returns immediately if the module is
  // already being loaded.
  nsresult StartModuleLoad(ModuleLoadRequest* aRequest);
  nsresult RestartModuleLoad(ModuleLoadRequest* aRequest);

  // Notify the module loader when a fetch started by StartFetch() completes.
  // This always completes the request: on failure, including a failure to
  // create the module script, the request is errored and the caller must not
  // run any further error handling for it.
  void OnFetchComplete(ModuleLoadRequest* aRequest, nsresult aRv);

  // Link the module and all its imports. This must occur prior to evaluation.
  bool InstantiateModuleGraph(ModuleLoadRequest* aRequest);

  // Executes the module.
  // Implements https://html.spec.whatwg.org/#run-a-module-script
  nsresult EvaluateModule(ModuleLoadRequest* aRequest);

  // Evaluate a module in the given context. Does not push an entry to the
  // execution stack.
  nsresult EvaluateModuleInContext(JSContext* aCx, ModuleLoadRequest* aRequest,
                                   ModuleErrorBehaviour errorBehaviour);

  void AppendDynamicImport(ModuleLoadRequest* aRequest);
  void ProcessDynamicImport(ModuleLoadRequest* aRequest);
  void CancelAndClearDynamicImports();

  // Process <script type="importmap">
  mozilla::UniquePtr<ImportMap> ParseImportMap(ScriptLoadRequest* aRequest);

  // Implements
  // https://html.spec.whatwg.org/multipage/webappapis.html#register-an-import-map
  void RegisterImportMap(mozilla::UniquePtr<ImportMap> aImportMap,
                         ScriptLoadRequest* aRequest);

  bool HasImportMapRegistered() const { return bool(mImportMap); }

  // Getter for mImportMapsAllowed.
  bool IsImportMapAllowed() const { return mImportMapsAllowed; }
  // https://html.spec.whatwg.org/multipage/webappapis.html#disallow-further-import-maps
  void DisallowImportMaps() { mImportMapsAllowed = false; }

  virtual bool IsModuleTypeAllowed(ModuleType aModuleType) {
    if (aModuleType == ModuleType::Unknown) {
      return false;
    }

    if (aModuleType == ModuleType::CSS &&
        !mozilla::StaticPrefs::layout_css_module_scripts_enabled()) {
      return false;
    }

    return true;
  }

  ImportMap* GetImportMap() { return mImportMap.get(); }

  // Returns whether there has been an entry in the import map
  // for the given aURI.
  bool GetImportMapSRI(nsIURI* aURI, nsIURI* aSourceURI,
                       nsIConsoleReportCollector* aReporter,
                       mozilla::dom::SRIMetadata* aMetadataOut);

  ResolvedModuleSet* GetResolvedModuleSet();

  // Called when a preloaded module script is reused by a normal load. Clear the
  // preload flag of aModuleScript and move its preloaded resolved set, if any,
  // into the global's resolved module set.
  void MovePreloadedSetToResolvedSet(ModuleScript* aModuleScript);

  // Called for each module preload removed before registering an import map.
  // The preloaded module scripts stay in the module map, but the state computed
  // while preloading is reset, as the new import map may change how their
  // specifiers resolve: the [[LoadedModules]] and status of each module that is
  // still a preload, their preloaded resolved sets, and the root's errors.
  void ResetPreloadedModuleGraph(ModuleLoadRequest* aRootRequest);

  // Returns true if the module for given module key is already fetched.
  bool IsModuleFetched(const ModuleMapKey& key) const;

  nsresult GetFetchedModuleURLs(nsTArray<nsCString>& aURLs);

  // Override the module loader with given loader until ResetOverride is called.
  // While overridden, ModuleLoaderBase::GetCurrentModuleLoader returns aLoader.
  //
  // This is used by mozJSModuleLoader to temporarily override the global's
  // module loader with SyncModuleLoader while importing a module graph
  // synchronously.
  void SetOverride(ModuleLoaderBase* aLoader);

  // Returns true if SetOverride was called.
  bool IsOverridden();

  // Returns true if SetOverride was called with aLoader.
  bool IsOverriddenBy(ModuleLoaderBase* aLoader);

  void ResetOverride();

  // Copy fetched modules to `aDest`.
  // `this` shouldn't have any fetching.
  // `aDest` shouldn't have any fetching or fetched modules.
  //
  // This is used when starting sync module load, to replicate the module cache
  // in the sync module loader pointed by `aDest`.
  void CopyModulesTo(ModuleLoaderBase* aDest);

  // Move all fetched modules to `aDest`.
  // Both `this` and `aDest` shouldn't have any fetching.
  //
  // This is used when finishing sync module load, to reflect the loaded modules
  // to the async module loader pointed by `aDest`.
  void MoveModulesTo(ModuleLoaderBase* aDest);

  // Internal methods.

 private:
  friend class JS::loader::ModuleLoadRequest;
  friend class JS::loader::ImportMap;

  static ModuleLoaderBase* GetCurrentModuleLoader(JSContext* aCx);
  static ScriptFetchInfo* GetScriptFetchInfoOrNull(Handle<Value> aReferrer);

  static void EnsureModuleHooksInitialized();

  static bool HostLoadImportedModule(JSContext* aCx, Handle<Value> aReferrer,
                                     Handle<JSObject*> aModuleRequest,
                                     Handle<Value> aHostDefined,
                                     Handle<Value> aPayload,
                                     uint32_t aLineNumber,
                                     JS::ColumnNumberOneOrigin aColumnNumber);
  static bool FinishLoadingImportedModule(JSContext* aCx,
                                          ModuleLoadRequest* aRequest);

  static bool HostPopulateImportMeta(JSContext* aCx,
                                     Handle<JSObject*> aModuleRecord,
                                     Handle<JSObject*> aMetaObject);
  static bool ImportMetaResolve(JSContext* cx, unsigned argc, Value* vp);
  static JSString* ImportMetaResolveImpl(JSContext* aCx,
                                         Handle<Value> aReferencingPrivate,
                                         Handle<JSString*> aSpecifier);

  ResolveResult ResolveModuleSpecifier(ScriptFetchInfo* aFetchInfo,
                                       const nsAString& aSpecifier);

  nsresult HandleResolveFailure(JSContext* aCx, ScriptFetchInfo* aFetchInfo,
                                const nsAString& aSpecifier,
                                ResolveError aError, uint32_t aLineNumber,
                                ColumnNumberOneOrigin aColumnNumber,
                                MutableHandle<Value> aErrorOut);

  enum class RestartRequest { No, Yes };
  nsresult StartOrRestartModuleLoad(ModuleLoadRequest* aRequest,
                                    RestartRequest aRestart);

  bool ModuleMapContainsURL(const ModuleMapKey& key) const;
  bool IsModuleFetching(const ModuleMapKey& key) const;
  void WaitForModuleFetch(ModuleLoadRequest* aRequest);

  void AddToGlobalResolvedSet(
      mozilla::UniquePtr<SpecifierResolutionRecord> aRecord);

  // https://html.spec.whatwg.org/#add-module-to-resolved-module-set
  //
  // aReferrerFetchInfo is the ScriptFetchInfo of whichever script performed
  // this resolution. If it's still part of an active preload, the record is
  // attributed to it directly, so it can be migrated or discarded alongside
  // the rest of that module's own preloaded state; otherwise (including a
  // null aReferrerFetchInfo, e.g. a dynamic import with no referrer) it goes
  // straight into the global resolved set.
  void AddToResolvedModuleSet(
      mozilla::UniquePtr<SpecifierResolutionRecord> aRecord,
      ScriptFetchInfo* aReferrerFetchInfo);

  // Migrates aFetchInfo's preloaded resolved set, if any, into the global
  // resolved module set.
  void MigratePreloadedResolvedSet(ScriptFetchInfo* aFetchInfo);

 protected:
  void SetModuleFetchStarted(ModuleLoadRequest* aRequest);

 private:
  ModuleScript* GetFetchedModule(const ModuleMapKey& moduleMapKey) const;

  already_AddRefed<LoadingRequest> SetModuleFetchFinishedAndGetWaitingRequests(
      ModuleLoadRequest* aRequest, nsresult aResult);
  void ResumeWaitingRequests(LoadingRequest* aLoadingRequest, bool aSuccess);
  void ResumeWaitingRequest(ModuleLoadRequest* aRequest, bool aSuccess);

  void StartFetchingModuleDependencies(ModuleLoadRequest* aRequest);

  void InstantiateAndEvaluateDynamicImport(ModuleLoadRequest* aRequest);

  static bool OnLoadRequestedModulesResolved(JSContext* aCx, unsigned aArgc,
                                             Value* aVp);
  static bool OnLoadRequestedModulesRejected(JSContext* aCx, unsigned aArgc,
                                             Value* aVp);
  static bool OnLoadRequestedModulesResolved(JSContext* aCx,
                                             Handle<Value> aHostDefined);
  static bool OnLoadRequestedModulesRejected(JSContext* aCx,
                                             Handle<Value> aHostDefined,
                                             Handle<Value> aError);
  static bool OnLoadRequestedModulesResolved(ModuleLoadRequest* aRequest);
  static bool OnLoadRequestedModulesRejected(JSContext* aCx,
                                             ModuleLoadRequest* aRequest,
                                             Handle<Value> aError);

  void RemoveDynamicImport(ModuleLoadRequest* aRequest);

  nsresult CreateModuleScript(ModuleLoadRequest* aRequest);
  void DispatchModuleErrored(ModuleLoadRequest* aRequest);

  void OnFetchSucceeded(ModuleLoadRequest* aRequest);
  void OnFetchFailed(ModuleLoadRequest* aRequest);
  void Cancel(ModuleLoadRequest* aRequest);

  // The slot stored in ImportMetaResolve function.
  enum class ImportMetaSlots : uint32_t { ModuleRecordSlot = 0, SlotCount };

  // The number of args in ImportMetaResolve.
  static const uint32_t ImportMetaResolveNumArgs = 1;
  // The index of the 'specifier' argument in ImportMetaResolve.
  static const uint32_t ImportMetaResolveSpecifierArg = 0;

  // The slot containing the host defined value passed to LoadRequestedModules
  // promise reactions.
  static const uint32_t LoadReactionHostDefinedSlot = 0;

  // The number of args in OnLoadRequestedModulesResolved/Rejected.
  static const uint32_t OnLoadRequestedModulesResolvedNumArgs = 0;
  static const uint32_t OnLoadRequestedModulesRejectedNumArgs = 1;

  // The index of the 'error' argument in OnLoadRequestedModulesRejected.
  static const uint32_t OnLoadRequestedModulesRejectedErrorArg = 0;

 public:
  static mozilla::LazyLogModule gCspPRLog;
  static mozilla::LazyLogModule gModuleLoaderBaseLog;
};

// Override the target module loader with given module loader while this
// instance is on the stack.
class MOZ_RAII AutoOverrideModuleLoader {
 public:
  AutoOverrideModuleLoader(ModuleLoaderBase* aTarget,
                           ModuleLoaderBase* aLoader);
  ~AutoOverrideModuleLoader();

 private:
  RefPtr<ModuleLoaderBase> mTarget;
};

}  // namespace loader
}  // namespace JS

#endif  // js_loader_ModuleLoaderBase_h
