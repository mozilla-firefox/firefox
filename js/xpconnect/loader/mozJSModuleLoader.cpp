/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"  // MOZ_ASSERT, MOZ_ASSERT_IF
#include "mozilla/Attributes.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/Logging.h"
#include "mozilla/RefPtr.h"  // RefPtr, mozilla::StaticRefPtr
#include "mozilla/Utf8.h"    // mozilla::Utf8Unit

#include "ScriptLoadRequest.h"
#ifdef ANDROID
#  include <android/log.h>
#endif
#ifdef XP_WIN
#  include <windows.h>
#endif

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ModuleLoader.h"
#include "mozilla/dom/ReferrerPolicyBinding.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WorkerCommon.h"  // dom::GetWorkerPrivateFromContext
#include "mozilla/dom/WorkerPrivate.h"  // dom::WorkerPrivate, dom::AutoSyncLoopHolder
#include "mozilla/dom/WorkerRef.h"  // dom::StrongWorkerRef, dom::ThreadSafeWorkerRef
#include "mozilla/dom/WorkerRunnable.h"  // dom::MainThreadStopSyncLoopRunnable
#include "mozilla/MacroForEach.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/scache/StartupCache.h"
#include "mozilla/scache/StartupCacheUtils.h"
#include "mozilla/ScriptPreloader.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/Try.h"

#include "jsapi.h"
#include "JSServices.h"
#include "mozJSLoaderUtils.h"
#include "mozJSModuleLoader.h"
#include "NonSharedGlobalSyncModuleLoaderScope.h"
#include "nsCOMPtr.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIChannel.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsIJARURI.h"
#include "nsIStreamListener.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsXULAppAPI.h"
#include "SyncModuleLoader.h"
#include "WrapperFactory.h"
#include "xpcprivate.h"
#include "xpcpublic.h"

#include "js/Array.h"  // JS::GetArrayLength, JS::IsArrayObject
#include "js/CharacterEncoding.h"
#include "js/CompilationAndEvaluation.h"
#include "js/CompileOptions.h"         // JS::CompileOptions
#include "js/ErrorReport.h"            // JS_ReportErrorUTF8, JSErrorReport
#include "js/Exception.h"              // JS_ErrorFromException
#include "js/friend/ErrorMessages.h"   // JSMSG_*
#include "js/friend/JSMEnvironment.h"  // JS::ExecuteInJSMEnvironment, JS::GetJSMEnvironmentOfScriptedCaller, JS::NewJSMEnvironment
#include "js/loader/ModuleLoadRequest.h"
#include "js/Modules.h"  // JS::CompileJsonModule, JS::CreateDefaultExportSyntheticModule
#include "js/Object.h"  // JS::GetCompartment
#include "js/Printf.h"
#include "js/PropertyAndElement.h"  // JS_DefineFunctions, JS_DefineProperty, JS_Enumerate, JS_GetElement, JS_GetProperty, JS_GetPropertyById, JS_HasOwnProperty, JS_HasOwnPropertyById, JS_SetProperty, JS_SetPropertyById
#include "js/PropertySpec.h"
#include "js/SourceText.h"  // JS::SourceText

using namespace mozilla;
using namespace mozilla::scache;
using namespace mozilla::loader;
using namespace xpc;
using namespace JS;

#define JS_CACHE_PREFIX(aScopeType, aCompilationTarget) \
  "jsloader/" aScopeType "/" aCompilationTarget

// MOZ_LOG=JSModuleLoader:5
static LazyLogModule gJSCLLog("JSModuleLoader");

#define LOG(args) MOZ_LOG(gJSCLLog, mozilla::LogLevel::Debug, args)

static bool Dump(JSContext* cx, unsigned argc, Value* vp) {
  if (!nsJSUtils::DumpEnabled()) {
    return true;
  }

  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() == 0) {
    return true;
  }

  RootedString str(cx, JS::ToString(cx, args[0]));
  if (!str) {
    return false;
  }

  JS::UniqueChars utf8str = JS_EncodeStringToUTF8(cx, str);
  if (!utf8str) {
    return false;
  }

  MOZ_LOG(nsContentUtils::DOMDumpLog(), mozilla::LogLevel::Debug,
          ("[SystemGlobal.Dump] %s", utf8str.get()));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "Gecko", "%s", utf8str.get());
#endif
#ifdef XP_WIN
  if (IsDebuggerPresent()) {
    nsAutoJSString wstr;
    if (!wstr.init(cx, str)) {
      return false;
    }
    OutputDebugStringW(wstr.get());
  }
#endif
  fputs(utf8str.get(), stdout);
  fflush(stdout);
  return true;
}

static bool Debug(JSContext* cx, unsigned argc, Value* vp) {
#ifdef DEBUG
  return Dump(cx, argc, vp);
#else
  return true;
#endif
}

static const JSFunctionSpec gGlobalFun[] = {
    JS_FN("dump", Dump, 1, 0), JS_FN("debug", Debug, 1, 0),
    JS_FN("atob", Atob, 1, 0), JS_FN("btoa", Btoa, 1, 0), JS_FS_END};

mozJSModuleLoader::mozJSModuleLoader()
    :
#ifdef STARTUP_RECORDER_ENABLED
      mImportStacks(16),
#endif
      mInitialized(false),
      mLoaderGlobal(dom::RootingCx()),
      mServicesObj(dom::RootingCx()) {
}

#define ENSURE_DEP(name)          \
  {                               \
    nsresult rv = Ensure##name(); \
    NS_ENSURE_SUCCESS(rv, rv);    \
  }
#define ENSURE_DEPS(...) MOZ_FOR_EACH(ENSURE_DEP, (), (__VA_ARGS__));
#define BEGIN_ENSURE(self, ...) \
  {                             \
    if (m##self) return NS_OK;  \
    ENSURE_DEPS(__VA_ARGS__);   \
  }

class MOZ_STACK_CLASS ModuleLoaderInfo {
 public:
  explicit ModuleLoaderInfo(const nsACString& aLocation)
      : mLocation(&aLocation) {}
  explicit ModuleLoaderInfo(JS::loader::ModuleLoadRequest* aRequest)
      : mLocation(nullptr), mURI(aRequest->URI()) {}

  nsIIOService* IOService() {
    MOZ_ASSERT(mIOService);
    return mIOService;
  }
  nsresult EnsureIOService() {
    if (mIOService) {
      return NS_OK;
    }
    nsresult rv;
    mIOService = do_GetIOService(&rv);
    return rv;
  }

  nsIURI* URI() {
    MOZ_ASSERT(mURI);
    return mURI;
  }
  nsresult EnsureURI() {
    BEGIN_ENSURE(URI, IOService);
    MOZ_ASSERT(mLocation);
    return mIOService->NewURI(*mLocation, nullptr, nullptr,
                              getter_AddRefs(mURI));
  }

  nsIChannel* ScriptChannel() {
    MOZ_ASSERT(mScriptChannel);
    return mScriptChannel;
  }
  nsresult EnsureScriptChannel() {
    BEGIN_ENSURE(ScriptChannel, IOService, URI);

    return NS_NewChannel(
        getter_AddRefs(mScriptChannel), mURI,
        nsContentUtils::GetSystemPrincipal(),
        nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
        nsIContentPolicy::TYPE_SCRIPT,
        /* aCookieJarSettings = */ nullptr,
        /* aPerformanceStorage = */ nullptr,
        /* aLoadGroup = */ nullptr, /* aCallbacks = */ nullptr,
        nsIRequest::LOAD_NORMAL, mIOService, /* aSandboxFlags = */ 0);
  }

  nsIURI* ResolvedURI() {
    MOZ_ASSERT(mResolvedURI);
    return mResolvedURI;
  }
  nsresult EnsureResolvedURI() {
    BEGIN_ENSURE(ResolvedURI, URI);
    return ResolveURI(mURI, getter_AddRefs(mResolvedURI));
  }

 private:
  const nsACString* mLocation;
  nsCOMPtr<nsIIOService> mIOService;
  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<nsIChannel> mScriptChannel;
  nsCOMPtr<nsIURI> mResolvedURI;
};

#undef BEGIN_ENSURE
#undef ENSURE_DEPS
#undef ENSURE_DEP

mozJSModuleLoader::~mozJSModuleLoader() {
  MOZ_ASSERT(!mInitialized,
             "UnloadModules() was not explicitly called before cleaning up "
             "mozJSModuleLoader");

  if (mInitialized) {
    UnloadModules();
  }
}

StaticRefPtr<mozJSModuleLoader> mozJSModuleLoader::sSelf;
StaticRefPtr<mozJSModuleLoader> mozJSModuleLoader::sDevToolsLoader;

void mozJSModuleLoader::FindTargetObject(JSContext* aCx,
                                         MutableHandleObject aTargetObject) {
  aTargetObject.set(JS::GetJSMEnvironmentOfScriptedCaller(aCx));

  // The above could fail if the scripted caller is not a JSM (it could be a DOM
  // scope, for instance).
  //
  // If the target object was not in the JSM shared global, return the global
  // instead. This is needed when calling the subscript loader within a frame
  // script, since it the FrameScript NSVO will have been found.
  if (!aTargetObject ||
      !IsLoaderGlobal(JS::GetNonCCWObjectGlobal(aTargetObject))) {
    aTargetObject.set(JS::GetScriptedCallerGlobal(aCx));

    // Return nullptr if the scripted caller is in a different compartment.
    if (JS::GetCompartment(aTargetObject) != js::GetContextCompartment(aCx)) {
      aTargetObject.set(nullptr);
    }
  }
}

void mozJSModuleLoader::InitStatics() {
  MOZ_ASSERT(!sSelf);
  sSelf = new mozJSModuleLoader();

  dom::AutoJSAPI jsapi;
  jsapi.Init();
  JSContext* cx = jsapi.cx();
  sSelf->InitSharedGlobal(cx);

  NonSharedGlobalSyncModuleLoaderScope::InitStatics();
}

void mozJSModuleLoader::UnloadLoaders() {
  if (sSelf) {
    sSelf->Unload();
  }
  if (sDevToolsLoader) {
    sDevToolsLoader->Unload();
  }
}

void mozJSModuleLoader::Unload() {
  UnloadModules();

  if (mModuleLoader) {
    mModuleLoader->Shutdown();
    mModuleLoader = nullptr;
  }
}

void mozJSModuleLoader::ShutdownLoaders() {
  MOZ_ASSERT(sSelf);
  sSelf = nullptr;

  if (sDevToolsLoader) {
    sDevToolsLoader = nullptr;
  }
}

mozJSModuleLoader* mozJSModuleLoader::GetOrCreateDevToolsLoader(
    JSContext* aCx) {
  if (sDevToolsLoader) {
    return sDevToolsLoader;
  }
  sDevToolsLoader = new mozJSModuleLoader();

  sDevToolsLoader->InitSharedGlobal(aCx);

  return sDevToolsLoader;
}

void mozJSModuleLoader::InitSyncModuleLoaderForGlobal(
    nsIGlobalObject* aGlobal) {
  MOZ_ASSERT(!mLoaderGlobal);
  MOZ_ASSERT(!mModuleLoader);

  RefPtr<SyncScriptLoader> scriptLoader = new SyncScriptLoader;
  mModuleLoader = new SyncModuleLoader(scriptLoader, aGlobal);
  mLoaderGlobal = aGlobal->GetGlobalJSObject();
}

void mozJSModuleLoader::DisconnectSyncModuleLoaderFromGlobal() {
  MOZ_ASSERT(mLoaderGlobal);
  MOZ_ASSERT(mModuleLoader);

  mLoaderGlobal = nullptr;
  Unload();
}

/* static */
bool mozJSModuleLoader::IsSharedSystemGlobal(nsIGlobalObject* aGlobal) {
  return sSelf->IsLoaderGlobal(aGlobal->GetGlobalJSObject());
}

/* static */
bool mozJSModuleLoader::IsDevToolsLoaderGlobal(nsIGlobalObject* aGlobal) {
  return sDevToolsLoader &&
         sDevToolsLoader->IsLoaderGlobal(aGlobal->GetGlobalJSObject());
}

#ifdef STARTUP_RECORDER_ENABLED
template <class Key, class Data, class UserData, class Converter>
static size_t SizeOfStringTableExcludingThis(
    const nsBaseHashtable<Key, Data, UserData, Converter>& aTable,
    MallocSizeOf aMallocSizeOf) {
  size_t n = aTable.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : aTable) {
    n += entry.GetKey().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    n += entry.GetData().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }
  return n;
}
#endif

size_t mozJSModuleLoader::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  size_t n = aMallocSizeOf(this);
#ifdef STARTUP_RECORDER_ENABLED
  n += SizeOfStringTableExcludingThis(mImportStacks, aMallocSizeOf);
#endif
  return n;
}

void mozJSModuleLoader::CreateLoaderGlobal(JSContext* aCx,
                                           const nsACString& aLocation,
                                           MutableHandleObject aGlobal) {
  auto systemGlobal = MakeRefPtr<SystemGlobal>();
  RealmOptions options;
  auto& creationOptions = options.creationOptions();

  creationOptions.setFreezeBuiltins(true).setNewCompartmentInSystemZone();
  if (IsDevToolsLoader()) {
    creationOptions.setInvisibleToDebugger(true);
  }
  xpc::SetPrefableRealmOptions(options);

  // Defer firing OnNewGlobalObject until after the __URI__ property has
  // been defined so the JS debugger can tell what module the global is
  // for
  RootedObject global(aCx);

#ifdef DEBUG
  // See mozJSModuleLoader::DefineJSServices.
  mIsInitializingLoaderGlobal = true;
#endif
  nsresult rv = xpc::InitClassesWithNewWrappedGlobal(
      aCx, static_cast<nsIGlobalObject*>(systemGlobal),
      nsContentUtils::GetSystemPrincipal(), xpc::DONT_FIRE_ONNEWGLOBALHOOK,
      options, &global);
#ifdef DEBUG
  mIsInitializingLoaderGlobal = false;
#endif
  NS_ENSURE_SUCCESS_VOID(rv);

  NS_ENSURE_TRUE_VOID(global);

  systemGlobal->SetGlobalObject(global);

  JSAutoRealm ar(aCx, global);
  if (!JS_DefineFunctions(aCx, global, gGlobalFun)) {
    return;
  }

  if (!CreateJSServices(aCx)) {
    return;
  }

  if (!DefineJSServices(aCx, global)) {
    return;
  }

  // Set the location information for the new global, so that tools like
  // about:memory may use that information
  xpc::SetLocationForGlobal(global, aLocation);

  MOZ_ASSERT(!mModuleLoader);
  RefPtr<SyncScriptLoader> scriptLoader = new SyncScriptLoader;
  mModuleLoader = new SyncModuleLoader(scriptLoader, systemGlobal);
  systemGlobal->InitModuleLoader(mModuleLoader);

  aGlobal.set(global);
}

void mozJSModuleLoader::InitSharedGlobal(JSContext* aCx) {
  JS::RootedObject globalObj(aCx);

  CreateLoaderGlobal(
      aCx, IsDevToolsLoader() ? "DevTools global"_ns : "shared JSM global"_ns,
      &globalObj);

  // If we fail to create a module global this early, we're not going to
  // get very far, so just bail out now.
  MOZ_RELEASE_ASSERT(globalObj);
  mLoaderGlobal = globalObj;

  // AutoEntryScript required to invoke debugger hook, which is a
  // Gecko-specific concept at present.
  dom::AutoEntryScript aes(globalObj, "module loader report global");
  JS_FireOnNewGlobalObject(aes.cx(), globalObj);
}

// Read script file on the main thread and pass it back to worker.
class ScriptReaderRunnable final : public nsIRunnable,
                                   public nsINamed,
                                   public nsIStreamListener {
 public:
  ScriptReaderRunnable(RefPtr<dom::ThreadSafeWorkerRef>&& aWorkerRef,
                       nsIEventTarget* aSyncLoopTarget,
                       const nsCString& aLocation)
      : mLocation(aLocation),
        mRv(NS_ERROR_FAILURE),
        mWorkerRef(std::move(aWorkerRef)),
        mSyncLoopTarget(aSyncLoopTarget) {}

  NS_DECL_THREADSAFE_ISUPPORTS

  nsCString& Data() { return mData; }

  nsresult Result() const { return mRv; }

  // nsIRunnable

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    nsresult rv = StartReadScriptFromLocation();
    if (NS_FAILED(rv)) {
      OnComplete(rv);
    }

    return NS_OK;
  }

  // nsINamed

  NS_IMETHOD
  GetName(nsACString& aName) override {
    aName.AssignLiteral("ScriptReaderRunnable");
    return NS_OK;
  }

  // nsIStreamListener

  NS_IMETHOD OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInputStream,
                             uint64_t aOffset, uint32_t aCount) override {
    uint32_t read = 0;
    return aInputStream->ReadSegments(AppendSegmentToData, this, aCount, &read);
  }

  // nsIRequestObserver

  NS_IMETHOD OnStartRequest(nsIRequest* aRequest) override { return NS_OK; }

  NS_IMETHOD OnStopRequest(nsIRequest* aRequest,
                           nsresult aStatusCode) override {
    OnComplete(aStatusCode);
    return NS_OK;
  }

 private:
  ~ScriptReaderRunnable() = default;

  static nsresult AppendSegmentToData(nsIInputStream* aInputStream,
                                      void* aClosure, const char* aRawSegment,
                                      uint32_t aToOffset, uint32_t aCount,
                                      uint32_t* outWrittenCount) {
    auto* self = static_cast<ScriptReaderRunnable*>(aClosure);
    self->mData.Append(aRawSegment, aCount);
    *outWrittenCount = aCount;
    return NS_OK;
  }

  void OnComplete(nsresult aRv) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mWorkerRef);

    mRv = aRv;

    RefPtr<dom::MainThreadStopSyncLoopRunnable> runnable =
        new dom::MainThreadStopSyncLoopRunnable(std::move(mSyncLoopTarget),
                                                mRv);
    MOZ_ALWAYS_TRUE(runnable->Dispatch(mWorkerRef->Private()));

    mWorkerRef = nullptr;
    mSyncLoopTarget = nullptr;
  }

  nsresult StartReadScriptFromLocation() {
    MOZ_ASSERT(NS_IsMainThread());

    ModuleLoaderInfo info(mLocation);
    nsresult rv = info.EnsureScriptChannel();
    NS_ENSURE_SUCCESS(rv, rv);

    return info.ScriptChannel()->AsyncOpen(this);
  }

 private:
  nsAutoCString mLocation;
  nsCString mData;
  nsresult mRv;

  RefPtr<dom::ThreadSafeWorkerRef> mWorkerRef;

  nsCOMPtr<nsIEventTarget> mSyncLoopTarget;
};

NS_IMPL_ISUPPORTS(ScriptReaderRunnable, nsIRunnable, nsINamed,
                  nsIStreamListener)

/* static */
nsresult mozJSModuleLoader::ReadScriptOnMainThread(JSContext* aCx,
                                                   const nsCString& aLocation,
                                                   nsCString& aData) {
  dom::WorkerPrivate* workerPrivate = dom::GetWorkerPrivateFromContext(aCx);
  MOZ_ASSERT(workerPrivate);

  dom::AutoSyncLoopHolder syncLoop(workerPrivate, dom::WorkerStatus::Canceling);
  nsCOMPtr<nsISerialEventTarget> syncLoopTarget =
      syncLoop.GetSerialEventTarget();
  if (!syncLoopTarget) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  RefPtr<dom::StrongWorkerRef> workerRef = dom::StrongWorkerRef::Create(
      workerPrivate, "mozJSModuleLoader::ScriptReaderRunnable", nullptr);
  if (!workerRef) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }
  RefPtr<dom::ThreadSafeWorkerRef> tsWorkerRef =
      MakeRefPtr<dom::ThreadSafeWorkerRef>(workerRef);

  RefPtr<ScriptReaderRunnable> runnable = new ScriptReaderRunnable(
      std::move(tsWorkerRef), syncLoopTarget, aLocation);

  if (NS_FAILED(NS_DispatchToMainThread(runnable))) {
    return NS_ERROR_FAILURE;
  }

  syncLoop.Run();

  if (NS_FAILED(runnable->Result())) {
    return runnable->Result();
  }

  aData = std::move(runnable->Data());

  return NS_OK;
}

static mozilla::Result<nsCString, nsresult> ReadScript(ModuleLoaderInfo& aInfo);

/* static */
nsresult mozJSModuleLoader::CompileJsonModuleFromSource(
    JSContext* aCx, const nsACString& aSource, const nsACString& aLocation,
    JS::MutableHandle<JSObject*> aModuleOut) {
  CompileOptions options(aCx);
  options.setFileAndLine(PromiseFlatCString(aLocation).get(), 1);
  SetModuleOptions(options);

  JS::SourceText<mozilla::Utf8Unit> srcBuf;
  if (!srcBuf.init(aCx, aSource.Data(), aSource.Length(),
                   JS::SourceOwnership::Borrowed)) {
    return NS_ERROR_FAILURE;
  }

  JSObject* module = JS::CompileJsonModule(aCx, options, srcBuf);
  if (!module) {
    return NS_ERROR_FAILURE;
  }

  aModuleOut.set(module);
  return NS_OK;
}

/* static */
nsresult mozJSModuleLoader::CompileCssModuleFromSource(
    JSContext* aCx, SyncModuleLoader* aModuleLoader, const nsACString& aSource,
    nsIURI* aBaseURI, JS::MutableHandle<JSObject*> aModuleOut) {
  return dom::CreateCssModule(aCx, aModuleLoader->GetGlobalObject(), aSource,
                              aBaseURI, aModuleOut);
}
/* static */
nsresult mozJSModuleLoader::CreateTextModuleFromSource(
    JSContext* aCx, const nsACString& aSource, const nsACString& aLocation,
    JS::MutableHandle<JSObject*> aModuleOut) {
  CompileOptions options(aCx);
  options.setFileAndLine(PromiseFlatCString(aLocation).get(), 1);
  SetModuleOptions(options);

  auto str = JS_NewStringCopyUTF8N(
      aCx, JS::UTF8Chars(aSource.Data(), aSource.Length()));
  if (!str) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  JS::RootedValue defaultExport(aCx, JS::StringValue(str));
  JSObject* module = JS::CreateDefaultExportSyntheticModule(aCx, defaultExport);
  if (!module) {
    return NS_ERROR_FAILURE;
  }

  aModuleOut.set(module);
  return NS_OK;
}

/* static */
nsresult mozJSModuleLoader::LoadSingleModuleOnWorker(
    SyncModuleLoader* aModuleLoader, JSContext* aCx,
    JS::loader::ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  nsAutoCString location;
  nsresult rv = aRequest->URI()->GetSpec(location);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString data;
  rv = ReadScriptOnMainThread(aCx, location, data);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (aRequest->mModuleType) {
    case JS::ModuleType::JavaScriptOrWasm: {
      CompileOptions options(aCx);
      // NOTE: ScriptPreloader::FillCompileOptionsForCachedStencil shouldn't be
      //       used here because the module is put into the worker global's
      //       module map, instead of the shared global's module map, where the
      //       worker module loader doesn't support lazy source.
      //       Accessing the source requires the synchronous communication with
      //       the main thread, and supporting it requires too much complexity
      //       compared to the benefit.
      options.setNoScriptRval(true);
      options.setFileAndLine(location.get(), 1);
      SetModuleOptions(options);

      // Worker global doesn't have the source hook.
      MOZ_ASSERT(!options.sourceIsLazy);

      JS::SourceText<mozilla::Utf8Unit> srcBuf;
      if (!srcBuf.init(aCx, data.get(), data.Length(),
                       JS::SourceOwnership::Borrowed)) {
        return NS_ERROR_FAILURE;
      }

      RefPtr<JS::Stencil> stencil =
          CompileModuleScriptToStencil(aCx, options, srcBuf);
      if (!stencil) {
        return NS_ERROR_FAILURE;
      }

      JS::InstantiateOptions instantiateOptions;
      aModuleOut.set(
          JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
      if (!aModuleOut) {
        return NS_ERROR_FAILURE;
      }
      break;
    }
    case JS::ModuleType::JSON:
      rv = CompileJsonModuleFromSource(aCx, data, location, aModuleOut);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    case JS::ModuleType::Text:
      rv = CreateTextModuleFromSource(aCx, data, location, aModuleOut);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    case JS::ModuleType::CSS:
      JS_ReportErrorASCII(aCx, "CSS module scripts not supported on workers");
      break;
    case JS::ModuleType::Unknown:
    case JS::ModuleType::Bytes:
      JS_ReportErrorASCII(aCx, "Unsupported module type");
      return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

/* static */
nsresult mozJSModuleLoader::LoadSingleModule(
    SyncModuleLoader* aModuleLoader, JSContext* aCx,
    JS::loader::ModuleLoadRequest* aRequest,
    JS::MutableHandle<JSObject*> aModuleOut) {
  AUTO_PROFILER_MARKER_TEXT(
      "ChromeUtils.importESModule static import", JS,
      MarkerOptions(MarkerStack::Capture(),
                    MarkerInnerWindowIdFromJSContext(aCx)),
      nsContentUtils::TruncatedURLForDisplay(aRequest->URI()));

  if (!NS_IsMainThread()) {
    return LoadSingleModuleOnWorker(aModuleLoader, aCx, aRequest, aModuleOut);
  }

  switch (aRequest->mModuleType) {
    case JS::ModuleType::JavaScriptOrWasm: {
      ModuleLoaderInfo info(aRequest);
      nsresult rv = info.EnsureResolvedURI();
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIFile> sourceFile;
      rv = GetSourceFile(info.ResolvedURI(), getter_AddRefs(sourceFile));
      NS_ENSURE_SUCCESS(rv, rv);

      bool realFile = LocationIsRealFile(aRequest->URI());

      rv = GetScriptForLocation(aCx, info, sourceFile, realFile, aModuleOut);
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    }
    case JS::ModuleType::JSON:
    case JS::ModuleType::CSS:
    case JS::ModuleType::Text: {
      ModuleLoaderInfo info(aRequest);
      nsAutoCString location;
      nsresult rv = aRequest->URI()->GetSpec(location);
      NS_ENSURE_SUCCESS(rv, rv);
      nsCString source = MOZ_TRY(ReadScript(info));
      if (aRequest->mModuleType == JS::ModuleType::JSON) {
        rv = CompileJsonModuleFromSource(aCx, source, location, aModuleOut);
      } else if (aRequest->mModuleType == JS::ModuleType::CSS) {
        rv = CompileCssModuleFromSource(aCx, aModuleLoader, source,
                                        aRequest->BaseURL(), aModuleOut);
      } else {
        rv = CreateTextModuleFromSource(aCx, source, location, aModuleOut);
      }
      NS_ENSURE_SUCCESS(rv, rv);
      break;
    }
    case JS::ModuleType::Unknown:
    case JS::ModuleType::Bytes:
      JS_ReportErrorASCII(aCx, "Unsupported module type");
      return NS_ERROR_FAILURE;
  }

#ifdef STARTUP_RECORDER_ENABLED
  if (aModuleLoader == sSelf->mModuleLoader) {
    sSelf->RecordImportStack(aCx, aRequest);
  } else if (sDevToolsLoader &&
             aModuleLoader == sDevToolsLoader->mModuleLoader) {
    sDevToolsLoader->RecordImportStack(aCx, aRequest);
  } else {
    // NOTE: Do not record import stack for non-shared globals, given the
    //       loader is associated with the global only while importing.
  }
#endif

  return NS_OK;
}

/* static */
nsresult mozJSModuleLoader::GetSourceFile(nsIURI* aResolvedURI,
                                          nsIFile** aSourceFileOut) {
  // Get the JAR if there is one.
  nsCOMPtr<nsIJARURI> jarURI;
  nsresult rv = NS_OK;
  jarURI = do_QueryInterface(aResolvedURI, &rv);
  nsCOMPtr<nsIFileURL> baseFileURL;
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIURI> baseURI;
    while (jarURI) {
      jarURI->GetJARFile(getter_AddRefs(baseURI));
      jarURI = do_QueryInterface(baseURI, &rv);
    }
    baseFileURL = do_QueryInterface(baseURI, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    baseFileURL = do_QueryInterface(aResolvedURI, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return baseFileURL->GetFile(aSourceFileOut);
}

/* static */
bool mozJSModuleLoader::LocationIsRealFile(nsIURI* aURI) {
  // We need to be extra careful checking for URIs pointing to files.
  // EnsureFile may not always get called, especially on resource URIs so we
  // need to call GetFile to make sure this is a valid file.
  nsresult rv = NS_OK;
  nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(aURI, &rv);
  nsCOMPtr<nsIFile> testFile;
  if (NS_SUCCEEDED(rv)) {
    fileURL->GetFile(getter_AddRefs(testFile));
  }

  return bool(testFile);
}

static mozilla::Result<nsCString, nsresult> ReadScript(
    ModuleLoaderInfo& aInfo) {
  MOZ_TRY(aInfo.EnsureScriptChannel());

  nsCOMPtr<nsIInputStream> scriptStream;
  MOZ_TRY(aInfo.ScriptChannel()->Open(getter_AddRefs(scriptStream)));

  uint64_t len64;
  uint32_t bytesRead;

  MOZ_TRY(scriptStream->Available(&len64));
  NS_ENSURE_TRUE(len64 < UINT32_MAX, Err(NS_ERROR_FILE_TOO_BIG));
  NS_ENSURE_TRUE(len64, Err(NS_ERROR_FAILURE));
  uint32_t len = (uint32_t)len64;

  /* malloc an internal buf the size of the file */
  nsCString str;
  if (!str.SetLength(len, fallible)) {
    return Err(NS_ERROR_OUT_OF_MEMORY);
  }

  /* read the file in one swoop */
  MOZ_TRY(scriptStream->Read(str.BeginWriting(), len, &bytesRead));
  if (bytesRead != len) {
    return Err(NS_BASE_STREAM_OSERROR);
  }

  return std::move(str);
}

/* static */
void mozJSModuleLoader::SetModuleOptions(CompileOptions& aOptions) {
  aOptions.setModule();

  // Top level await is not supported in synchronously loaded modules.
  aOptions.topLevelAwait = false;
}

/* static */
nsresult mozJSModuleLoader::GetScriptForLocation(
    JSContext* aCx, ModuleLoaderInfo& aInfo, nsIFile* aModuleFile,
    bool aUseMemMap, JS::MutableHandle<JSObject*> aModuleOut,
    char** aLocationOut) {
  // JS compilation errors are returned via an exception on the context.
  MOZ_ASSERT(!JS_IsExceptionPending(aCx));

  aModuleOut.set(nullptr);

  nsAutoCString nativePath;
  nsresult rv = aInfo.URI()->GetSpec(nativePath);
  NS_ENSURE_SUCCESS(rv, rv);

  // Before compiling the script, first check to see if we have it in
  // the preloader cache or the startupcache.  Note: as a rule, preloader cache
  // errors and startupcache errors are not fatal to loading the script, since
  // we can always slow-load.

  bool storeIntoStartupCache = false;
  StartupCache* cache = StartupCache::GetSingleton();

  aInfo.EnsureResolvedURI();

  nsAutoCString cachePath;
  scache::ResourceType resourceType;
  rv = PathifyURI(JS_CACHE_PREFIX("non-syntactic", "module"),
                  aInfo.ResolvedURI(), cachePath, &resourceType);
  NS_ENSURE_SUCCESS(rv, rv);

  JS::DecodeOptions decodeOptions;
  ScriptPreloader::FillDecodeOptionsForCachedStencil(decodeOptions);

  // Skip all caching for scripts not from omni.ja to avoid serving stale
  // bytecode when JAR files from built-in add-ons installed in the profile
  // directory are updated.
  bool shouldUseCache = (resourceType == scache::ResourceType::Gre ||
                         resourceType == scache::ResourceType::App);

  RefPtr<JS::Stencil> stencil;
  if (shouldUseCache) {
    stencil = ScriptPreloader::GetSingleton().GetCachedStencil(
        aCx, decodeOptions, cachePath);

    if (!stencil && cache) {
      ReadCachedStencil(cache, cachePath, aCx, decodeOptions,
                        getter_AddRefs(stencil));
      if (!stencil) {
        JS_ClearPendingException(aCx);

        storeIntoStartupCache = true;
      }
    }
  }

  if (stencil) {
    LOG(("Successfully loaded %s from cache\n", nativePath.get()));
  } else {
    // The script wasn't in the cache , so compile it now.
    LOG(("Slow loading %s\n", nativePath.get()));

    CompileOptions options(aCx);
    ScriptPreloader::FillCompileOptionsForCachedStencil(options);
    options.setFileAndLine(nativePath.get(), 1);
    SetModuleOptions(options);

    // If we can no longer write to caches, we should stop using lazy sources
    // and instead let normal syntax parsing occur. This can occur in content
    // processes after the ScriptPreloader is flushed where we can read but no
    // longer write.
    if (!storeIntoStartupCache && !ScriptPreloader::GetSingleton().Active()) {
      options.setSourceIsLazy(false);
    }

    if (aUseMemMap) {
      AutoMemMap map;
      auto result = map.init(aModuleFile);
      if (result.isErr()) {
        rv = result.propagateErr();
        if (rv == NS_ERROR_FILE_NOT_FOUND) {
          // In local builds, files are read from the disk instead of omni.ja,
          // which causes aUseMemMap to be true. To be consistent with packaged
          // builds, call CheckForBrokenChromeURL() here. For context, see:
          // https://bugzilla.mozilla.org/show_bug.cgi?id=2018078#c4
          mozilla::net::CheckForBrokenChromeURL(nullptr, aInfo.URI());
        }
        return rv;
      }

      // Note: exceptions will get handled further down;
      // don't early return for them here.
      auto buf = map.get<char>();

      JS::SourceText<mozilla::Utf8Unit> srcBuf;
      if (srcBuf.init(aCx, buf.get(), map.size(),
                      JS::SourceOwnership::Borrowed)) {
        stencil = CompileModuleScriptToStencil(aCx, options, srcBuf);
      }
    } else {
      nsCString str = MOZ_TRY(ReadScript(aInfo));

      JS::SourceText<mozilla::Utf8Unit> srcBuf;
      if (srcBuf.init(aCx, str.get(), str.Length(),
                      JS::SourceOwnership::Borrowed)) {
        stencil = CompileModuleScriptToStencil(aCx, options, srcBuf);
      }
    }

#ifdef DEBUG
    // The above shouldn't touch any options for instantiation.
    JS::InstantiateOptions instantiateOptions(options);
    instantiateOptions.assertDefault();
#endif

    if (!stencil) {
      return NS_ERROR_FAILURE;
    }
  }

  JS::InstantiateOptions instantiateOptions;
  aModuleOut.set(
      JS::InstantiateModuleStencil(aCx, instantiateOptions, stencil));
  if (!aModuleOut) {
    return NS_ERROR_FAILURE;
  }

  // ScriptPreloader::NoteScript needs to be called unconditionally, to
  // reflect the usage into the next session's cache.
  ScriptPreloader::GetSingleton().NoteStencil(nativePath, cachePath, stencil);
  if (ScriptPreloader::GetSingleton().Active()) {
    storeIntoStartupCache = false;
  }

  // Write to startup cache only when we didn't have any cache for the script
  // and compiled it.
  if (storeIntoStartupCache) {
    MOZ_ASSERT(stencil);

    // We successfully compiled the script, so cache it.
    rv = WriteCachedStencil(cache, cachePath, aCx, stencil);

    // Don't treat failure to write as fatal, since we might be working
    // with a read-only cache.
    if (NS_SUCCEEDED(rv)) {
      LOG(("Successfully wrote to cache\n"));
    } else {
      LOG(("Failed to write to cache\n"));
    }
  }

  /* Owned by ModuleEntry. Freed when we remove from the table. */
  if (aLocationOut) {
    *aLocationOut = ToNewCString(nativePath, mozilla::fallible);
    if (!*aLocationOut) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  return NS_OK;
}

void mozJSModuleLoader::UnloadModules() {
  MOZ_ASSERT(!mIsUnloaded);

  mInitialized = false;
  mIsUnloaded = true;

  if (mLoaderGlobal) {
    MOZ_ASSERT(JS_HasExtensibleLexicalEnvironment(mLoaderGlobal));
    JS::RootedObject lexicalEnv(dom::RootingCx(),
                                JS_ExtensibleLexicalEnvironment(mLoaderGlobal));
    JS_SetAllNonReservedSlotsToUndefined(lexicalEnv);
    JS_SetAllNonReservedSlotsToUndefined(mLoaderGlobal);
    mLoaderGlobal = nullptr;
  }
  mServicesObj = nullptr;

#ifdef STARTUP_RECORDER_ENABLED
  mImportStacks.Clear();
#endif
}

nsresult mozJSModuleLoader::IsESModuleLoaded(const nsACString& aLocation,
                                             bool* retval) {
  MOZ_ASSERT(nsContentUtils::IsCallerChrome());

  if (mIsUnloaded) {
    *retval = false;
    return NS_OK;
  }

  mInitialized = true;
  ModuleLoaderInfo info(aLocation);

  nsresult rv = info.EnsureURI();
  NS_ENSURE_SUCCESS(rv, rv);

  if (mModuleLoader->IsModuleFetched(
          JS::loader::ModuleMapKey(info.URI(), ModuleType::JavaScriptOrWasm))) {
    *retval = true;
    return NS_OK;
  }

  *retval = false;
  return NS_OK;
}

nsresult mozJSModuleLoader::GetLoadedESModules(
    nsTArray<nsCString>& aLoadedModules) {
  return mModuleLoader->GetFetchedModuleURLs(aLoadedModules);
}

#ifdef STARTUP_RECORDER_ENABLED
void mozJSModuleLoader::RecordImportStack(
    JSContext* aCx, JS::loader::ModuleLoadRequest* aRequest) {
  if (!StaticPrefs::browser_startup_record()) {
    return;
  }

  nsAutoCString location;
  nsresult rv = aRequest->URI()->GetSpec(location);
  if (NS_FAILED(rv)) {
    return;
  }

  auto recordJSStackOnly = [&]() {
    mImportStacks.InsertOrUpdate(
        location, xpc_PrintJSStack(aCx, false, false, false).get());
  };

  if (aRequest->IsTopLevel()) {
    recordJSStackOnly();
    return;
  }

  nsAutoCString importerSpec;
  rv = aRequest->mReferrer->GetSpec(importerSpec);
  if (NS_FAILED(rv)) {
    recordJSStackOnly();
    return;
  }

  auto importerStack = mImportStacks.Lookup(importerSpec);
  if (!importerStack) {
    // The importer's stack is not collected, possibly due to OOM.
    recordJSStackOnly();
    return;
  }

  nsAutoCString stack;

  stack += "* import [\"";
  stack += importerSpec;
  stack += "\"]\n";
  stack += *importerStack;

  mImportStacks.InsertOrUpdate(location, stack);
}
#endif

nsresult mozJSModuleLoader::GetModuleImportStack(const nsACString& aLocation,
                                                 nsACString& retval) {
#ifdef STARTUP_RECORDER_ENABLED
  MOZ_ASSERT(nsContentUtils::IsCallerChrome());

  // When querying the DevTools loader, it may not be initialized yet
  if (!mInitialized) {
    return NS_ERROR_FAILURE;
  }

  auto str = mImportStacks.Lookup(aLocation);
  if (!str) {
    return NS_ERROR_FAILURE;
  }

  retval = *str;
  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

nsresult mozJSModuleLoader::ImportESModule(
    JSContext* aCx, const nsACString& aLocation,
    JS::MutableHandleObject aModuleNamespace) {
  using namespace JS::loader;

  if (mIsUnloaded) {
    JS_ReportErrorASCII(aCx, "Module loaded is already unloaded");
    return NS_ERROR_FAILURE;
  }

  mInitialized = true;

  // Called from ChromeUtils::ImportESModule.
  nsCString str(aLocation);

  AUTO_PROFILER_MARKER_TEXT(
      "ChromeUtils.importESModule", JS,
      MarkerOptions(MarkerStack::Capture(),
                    MarkerInnerWindowIdFromJSContext(aCx)),
      Substring(aLocation, 0, std::min(size_t(128), aLocation.Length())));

  RootedObject globalObj(aCx, GetSharedGlobal());
  MOZ_ASSERT(globalObj);
  MOZ_ASSERT_IF(NS_IsMainThread(),
                xpc::Scriptability::Get(globalObj).Allowed());

  // The module loader should be instantiated when fetching the shared global
  MOZ_ASSERT(mModuleLoader);

  JSAutoRealm ar(aCx, globalObj);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aLocation);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!nsContentSecurityUtils::IsTrustedScheme(uri)) {
    JS_ReportErrorASCII(aCx,
                        "System modules must be loaded from a trusted scheme");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> principal =
      mModuleLoader->GetGlobalObject()->PrincipalOrNull();
  MOZ_ASSERT(principal);

  RefPtr<ScriptFetchOptions> options = new ScriptFetchOptions(
      CORS_NONE, /* aNonce = */ u""_ns, dom::RequestPriority::Auto,
      ParserMetadata::NotParserInserted, principal);

  RefPtr<SyncLoadContext> context = new SyncLoadContext();

  RefPtr<ModuleLoadRequest> request = new ModuleLoadRequest(
      JS::ModuleType::JavaScriptOrWasm, dom::SRIMetadata(),
      /* aReferrer = */ nullptr, context, ModuleLoadRequest::Kind::TopLevel,
      mModuleLoader, nullptr);

  request->NoCacheEntryFound(dom::ReferrerPolicy::No_referrer, options, uri);

  rv = request->StartModuleLoad();
  if (NS_FAILED(rv)) {
    mModuleLoader->MaybeReportLoadError(aCx);
    return rv;
  }

  rv = mModuleLoader->ProcessRequests();
  if (NS_FAILED(rv)) {
    mModuleLoader->MaybeReportLoadError(aCx);
    return rv;
  }

  MOZ_ASSERT(request->IsFinished());
  if (!request->mModuleScript) {
    mModuleLoader->MaybeReportLoadError(aCx);
    return NS_ERROR_FAILURE;
  }

  // All modules are loaded. MaybeReportLoadError isn't necessary from here.

  if (!request->mModuleScript->HasErrorToRethrow() &&
      !request->InstantiateModuleGraph()) {
    return NS_ERROR_FAILURE;
  }

  rv = mModuleLoader->EvaluateModuleInContext(aCx, request,
                                              JS::ThrowModuleErrorsSync);
  NS_ENSURE_SUCCESS(rv, rv);
  if (JS_IsExceptionPending(aCx)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ModuleScript> moduleScript = request->mModuleScript;
  JS::Rooted<JSObject*> module(aCx, moduleScript->ModuleRecord());
  aModuleNamespace.set(JS::GetModuleNamespace(aCx, module));

  return NS_OK;
}

bool mozJSModuleLoader::CreateJSServices(JSContext* aCx) {
  JSObject* services = NewJSServices(aCx);
  if (!services) {
    return false;
  }

  mServicesObj = services;
  return true;
}

bool mozJSModuleLoader::DefineJSServices(JSContext* aCx,
                                         JS::Handle<JSObject*> aGlobal) {
  if (!mServicesObj) {
    // This function is called whenever creating a new global that needs
    // `Services`, including the loader's shared global.
    //
    // This function is no-op if it's called during creating the loader's
    // shared global.
    //
    // See also  CreateAndDefineJSServices.
    MOZ_ASSERT(!mLoaderGlobal);
    MOZ_ASSERT(mIsInitializingLoaderGlobal);
    return true;
  }

  JS::Rooted<JS::Value> services(aCx, ObjectValue(*mServicesObj));
  if (!JS_WrapValue(aCx, &services)) {
    return false;
  }

  JS::Rooted<JS::PropertyKey> servicesId(
      aCx, XPCJSContext::Get()->GetStringID(XPCJSContext::IDX_SERVICES));
  return JS_DefinePropertyById(aCx, aGlobal, servicesId, services, 0);
}

size_t mozJSModuleLoader::ModuleEntry::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += aMallocSizeOf(location);

  return n;
}

//----------------------------------------------------------------------

/* static */
MOZ_THREAD_LOCAL(mozJSModuleLoader*)
NonSharedGlobalSyncModuleLoaderScope::sTlsActiveLoader;

void NonSharedGlobalSyncModuleLoaderScope::InitStatics() {
  sTlsActiveLoader.infallibleInit();
}

NonSharedGlobalSyncModuleLoaderScope::NonSharedGlobalSyncModuleLoaderScope(
    JSContext* aCx, nsIGlobalObject* aGlobal) {
  MOZ_ASSERT_IF(NS_IsMainThread(),
                !mozJSModuleLoader::IsSharedSystemGlobal(aGlobal));
  MOZ_ASSERT_IF(NS_IsMainThread(),
                !mozJSModuleLoader::IsDevToolsLoaderGlobal(aGlobal));

  mAsyncModuleLoader = aGlobal->GetModuleLoader(aCx);
  MOZ_ASSERT(mAsyncModuleLoader,
             "The consumer should guarantee the global returns non-null module "
             "loader");

  mLoader = new mozJSModuleLoader();
  mLoader->InitSyncModuleLoaderForGlobal(aGlobal);

  mAsyncModuleLoader->CopyModulesTo(mLoader->mModuleLoader);

  mMaybeOverride.emplace(mAsyncModuleLoader, mLoader->mModuleLoader);

  MOZ_ASSERT(!sTlsActiveLoader.get());
  sTlsActiveLoader.set(mLoader);
}

NonSharedGlobalSyncModuleLoaderScope::~NonSharedGlobalSyncModuleLoaderScope() {
  MOZ_ASSERT(sTlsActiveLoader.get() == mLoader);
  sTlsActiveLoader.set(nullptr);

  mLoader->DisconnectSyncModuleLoaderFromGlobal();
}

void NonSharedGlobalSyncModuleLoaderScope::Finish() {
  mLoader->mModuleLoader->MoveModulesTo(mAsyncModuleLoader);
}

/* static */
bool NonSharedGlobalSyncModuleLoaderScope::IsActive() {
  return !!sTlsActiveLoader.get();
}

/*static */
mozJSModuleLoader* NonSharedGlobalSyncModuleLoaderScope::ActiveLoader() {
  return sTlsActiveLoader.get();
}
