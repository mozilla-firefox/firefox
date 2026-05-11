/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThreadEventTarget.h"
#include "XPCOMModule.h"

#include "base/basictypes.h"

#include "mozilla/AbstractThread.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/TaskController.h"
#include "mozJSModuleLoader.h"
#include "nsXULAppAPI.h"

#ifndef ANDROID
#  include "nsTerminator.h"
#endif

#include "nsXPCOMPrivate.h"
#include "nsXPCOMCIDInternal.h"

#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/dom/SharedScriptCache.h"
#include "mozilla/SharedStyleSheetCache.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"

#include "prlink.h"

#include "nsCycleCollector.h"
#include "nsObserverService.h"

#include "nsDebugImpl.h"
#include "nsSystemInfo.h"

#include "nsComponentManager.h"
#include "nsCategoryManagerUtils.h"
#include "nsIServiceManager.h"

#include "nsThreadManager.h"
#include "nsThreadPool.h"

#include "nsTimerImpl.h"
#include "TimerThread.h"

#include "nsThread.h"
#include "nsVersionComparatorImpl.h"

#include "nsIFile.h"
#include "nsLocalFile.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsCategoryManager.h"
#include "nsMultiplexInputStream.h"

#include "nsAtomTable.h"
#include "nsISupportsImpl.h"
#include "nsLanguageAtomService.h"

#include "nsSystemInfo.h"
#include "nsMemoryReporterManager.h"
#include "nss.h"
#include "nsNSSComponent.h"

#include <locale.h>
#include "mozilla/Services.h"
#include "mozilla/Omnijar.h"
#include "mozilla/ScriptPreloader.h"
#include "mozilla/Telemetry.h"
#include "mozilla/BackgroundHangMonitor.h"

#include "mozilla/PoisonIOInterposer.h"
#include "mozilla/LateWriteChecks.h"

#include "mozilla/scache/StartupCache.h"

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/message_loop.h"

#include "mozilla/ipc/IOThread.h"
#include "mozilla/AvailableMemoryTracker.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/CountingAllocatorBase.h"
#ifdef MOZ_PHC
#  include "mozilla/PHCManager.h"
#endif
#include "mozilla/ServoStyleConsts.h"

#include "mozilla/ipc/GeckoChildProcessHost.h"

#include "ogg/ogg.h"

#include "GeckoProfiler.h"
#include "ProfilerControl.h"

#include "jsapi.h"
#include "js/Initialization.h"
#include "XPCSelfHostedShmem.h"

#include "gfxPlatform.h"

#include "mozilla/GeckoTrace.h"

using base::AtExitManager;
using mozilla::ipc::IOThreadParent;

// From toolkit/library/rust/lib.rs
extern "C" void GkRust_Init();
extern "C" void GkRust_Shutdown();

namespace {

static AtExitManager* sExitManager;
static MessageLoop* sMessageLoop;
static bool sCommandLineWasInitialized;
static mozilla::BackgroundHangMonitor* sMainHangMonitor;

} /* anonymous namespace */

#ifdef XP_WIN
extern nsresult CreateAnonTempFileRemover();
#endif

nsresult nsThreadManagerGetSingleton(const nsIID& aIID, void** aInstancePtr) {
  NS_ASSERTION(aInstancePtr, "null outptr");
  return nsThreadManager::get().QueryInterface(aIID, aInstancePtr);
}

nsresult nsLocalFileConstructor(const nsIID& aIID, void** aInstancePtr) {
  return nsLocalFile::nsLocalFileConstructor(aIID, aInstancePtr);
}

nsComponentManagerImpl* nsComponentManagerImpl::gComponentManager = nullptr;
bool gXPCOMShuttingDown = false;
bool gXPCOMMainThreadEventsAreDoomed = false;
char16_t* gGREBinPath = nullptr;

// gDebug will be freed during shutdown.
static nsIDebug2* gDebug = nullptr;

EXPORT_XPCOM_API(nsresult)
NS_GetDebug(nsIDebug2** aResult) {
  return nsDebugImpl::Create(NS_GET_IID(nsIDebug2), (void**)aResult);
}

class ICUReporter final : public nsIMemoryReporter,
                          public mozilla::CountingAllocatorBase<ICUReporter> {
 public:
  NS_DECL_ISUPPORTS

  static void* Alloc(const void*, size_t aSize) {
    void* result = CountingMalloc(aSize);
    if (result == nullptr) {
      MOZ_CRASH("Ran out of memory while allocating for ICU");
    }
    return result;
  }

  static void* Realloc(const void*, void* aPtr, size_t aSize) {
    void* result = CountingRealloc(aPtr, aSize);
    if (result == nullptr) {
      MOZ_CRASH("Ran out of memory while reallocating for ICU");
    }
    return result;
  }

  static void Free(const void*, void* aPtr) { return CountingFree(aPtr); }

 private:
  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    MOZ_COLLECT_REPORT(
        "explicit/icu", KIND_HEAP, UNITS_BYTES, MemoryAllocated(),
        "Memory used by ICU, a Unicode and globalization support library.");

    return NS_OK;
  }

  ~ICUReporter() = default;
};

NS_IMPL_ISUPPORTS(ICUReporter, nsIMemoryReporter)

class OggReporter final : public nsIMemoryReporter,
                          public mozilla::CountingAllocatorBase<OggReporter> {
 public:
  NS_DECL_ISUPPORTS

 private:
  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    MOZ_COLLECT_REPORT(
        "explicit/media/libogg", KIND_HEAP, UNITS_BYTES, MemoryAllocated(),
        "Memory allocated through libogg for Ogg, and related media "
        "files.");

    return NS_OK;
  }

  ~OggReporter() = default;
};

NS_IMPL_ISUPPORTS(OggReporter, nsIMemoryReporter)

#define XPCOM_INIT_FATAL(message, res) \
  if (XRE_IsParentProcess()) {         \
    return res;                        \
  }                                    \
  MOZ_CRASH(message);

// Note that on OSX, aBinDirectory will point to .app/Contents/Resources/browser
EXPORT_XPCOM_API(nsresult)
NS_InitXPCOM(nsIServiceManager** aResult, nsIFile* aBinDirectory,
             nsIDirectoryServiceProvider* aAppFileLocationProvider,
             bool aInitJSContext) {
  static bool sInitialized = false;
  if (sInitialized) {
    XPCOM_INIT_FATAL("!sInitialized", NS_ERROR_FAILURE)
  }

  sInitialized = true;

  NS_LogInit();

  NS_InitAtomTable();

  // We don't have the arguments by hand here.  If logging has already been
  // initialized by a previous call to LogModule::Init with the arguments
  // passed, passing (0, nullptr) is alright here.
  mozilla::LogModule::Init(0, nullptr);

  GkRust_Init();

  nsresult rv = NS_OK;

  // We are not shutting down
  gXPCOMShuttingDown = false;

#ifdef XP_UNIX
  // Discover the current value of the umask, and save it where
  // nsSystemInfo::Init can retrieve it when necessary.  There is no way
  // to read the umask without changing it, and the setting is process-
  // global, so this must be done while we are still single-threaded; the
  // nsSystemInfo object is typically created much later, when some piece
  // of chrome JS wants it.  The system call is specified as unable to fail.
  nsSystemInfo::gUserUmask = ::umask(0777);
  ::umask(nsSystemInfo::gUserUmask);
#endif

  // Set up chromium libs
  NS_ASSERTION(!sExitManager && !sMessageLoop, "Bad logic!");

  if (!AtExitManager::AlreadyRegistered()) {
    sExitManager = new AtExitManager();
  }

  MessageLoop* messageLoop = MessageLoop::current();
  if (!messageLoop) {
    sMessageLoop = new MessageLoopForUI(MessageLoop::TYPE_MOZILLA_PARENT);
    sMessageLoop->set_thread_name("Gecko");
    // Set experimental values for main thread hangs:
    // 128ms for transient hangs and 8192ms for permanent hangs
    sMessageLoop->set_hang_timeouts(128, 8192);
  } else if (messageLoop->type() == MessageLoop::TYPE_MOZILLA_CHILD) {
    messageLoop->set_thread_name("Gecko_Child");
    messageLoop->set_hang_timeouts(128, 8192);
  }

  // Establish the main thread here.
  rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    XPCOM_INIT_FATAL("nsThreadManager::get().Init()", rv)
  }

  // Initialise the profiler
  AUTO_PROFILER_INIT2;

  // Set up the timer globals/timer thread
  rv = nsTimerImpl::Startup();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    XPCOM_INIT_FATAL("nsTimerImpl::Startup()", rv)
  }

#ifndef ANDROID
  // If the locale hasn't already been setup by our embedder,
  // get us out of the "C" locale and into the system
  if (strcmp(setlocale(LC_ALL, nullptr), "C") == 0) {
    setlocale(LC_ALL, "");
  }
#endif

  nsDirectoryService::RealInit();

  bool value;

  if (aBinDirectory) {
    rv = aBinDirectory->IsDirectory(&value);

    if (NS_SUCCEEDED(rv) && value) {
      nsDirectoryService::gService->SetCurrentProcessDirectory(aBinDirectory);
    }
  }

  if (aAppFileLocationProvider) {
    rv = nsDirectoryService::gService->RegisterProvider(
        aAppFileLocationProvider);
    if (NS_FAILED(rv)) {
      XPCOM_INIT_FATAL("nsDirectoryService::gService->RegisterProvider()", rv)
    }
  }

  nsCOMPtr<nsIFile> xpcomLib;
  nsDirectoryService::gService->Get(NS_GRE_BIN_DIR, NS_GET_IID(nsIFile),
                                    getter_AddRefs(xpcomLib));
  MOZ_ASSERT(xpcomLib);

  // set gGREBinPath
  nsAutoString path;
  xpcomLib->GetPath(path);
  gGREBinPath = ToNewUnicode(path);

  xpcomLib->AppendNative(nsDependentCString(XPCOM_DLL));
  nsDirectoryService::gService->Set(NS_XPCOM_LIBRARY_FILE, xpcomLib);

  if (!mozilla::Omnijar::IsInitialized()) {
    // If you added a new process type that uses NS_InitXPCOM, and you're
    // *sure* you don't want NS_InitMinimalXPCOM: in addition to everything
    // else you'll probably have to do, please add it to the case in
    // GeckoChildProcessHost.cpp which sets the greomni/appomni flags.
    MOZ_ASSERT(XRE_IsParentProcess() || XRE_IsContentProcess());

    // Note that the Omnijar::FallibleInit does not fail but returns NS_OK if
    // the file is not found at all, as this is an expected possible way of
    // running with an unpacked modules directory.
    nsresult rv = mozilla::Omnijar::FallibleInit();
    if (NS_FAILED(rv)) {
      XPCOM_INIT_FATAL("Omnijar::Init()", NS_ERROR_OMNIJAR_CORRUPT)
    }
  }

  if ((sCommandLineWasInitialized = !CommandLine::IsInitialized())) {
#ifdef XP_WIN
    CommandLine::Init(0, nullptr);
#else
    nsCOMPtr<nsIFile> binaryFile;
    nsDirectoryService::gService->Get(NS_XPCOM_CURRENT_PROCESS_DIR,
                                      NS_GET_IID(nsIFile),
                                      getter_AddRefs(binaryFile));
    if (NS_WARN_IF(!binaryFile)) {
      XPCOM_INIT_FATAL("!binaryFile", NS_ERROR_FAILURE)
    }

    rv = binaryFile->AppendNative("nonexistent-executable"_ns);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      XPCOM_INIT_FATAL("binaryFile->AppendNative()", rv)
    }

    nsCString binaryPath;
    rv = binaryFile->GetNativePath(binaryPath);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      XPCOM_INIT_FATAL("binaryFile->GetNativePath", rv)
    }

    static char const* const argv = {strdup(binaryPath.get())};
    CommandLine::Init(1, &argv);
#endif
  }

  NS_ASSERTION(nsComponentManagerImpl::gComponentManager == nullptr,
               "CompMgr not null at init");

  // Create the Component/Service Manager
  nsComponentManagerImpl::gComponentManager = new nsComponentManagerImpl();
  NS_ADDREF(nsComponentManagerImpl::gComponentManager);

  // Global cycle collector initialization.
  if (!nsCycleCollector_init()) {
    XPCOM_INIT_FATAL("nsCycleCollector_init()", NS_ERROR_UNEXPECTED)
  }

  // And start it up for this thread too.
  nsCycleCollector_startup();

  // Register ICU memory functions.  This really shouldn't be necessary: the
  // JS engine should do this on its own inside JS_Init, and memory-reporting
  // code should call a JSAPI function to observe ICU memory usage.  But we
  // can't define the alloc/free functions in the JS engine, because it can't
  // depend on the XPCOM-based memory reporting goop.  So for now, we have
  // this oddness.
  mozilla::SetICUMemoryFunctions();

  // Do the same for libogg.
  ogg_set_mem_functions(
      OggReporter::CountingMalloc, OggReporter::CountingCalloc,
      OggReporter::CountingRealloc, OggReporter::CountingFree);

  rv = nsComponentManagerImpl::gComponentManager->Init();
  if (NS_FAILED(rv)) {
    NS_RELEASE(nsComponentManagerImpl::gComponentManager);
    XPCOM_INIT_FATAL("gComponentManager->Init()", rv)
  }

  if (aResult) {
    NS_ADDREF(*aResult = nsComponentManagerImpl::gComponentManager);
  }

#ifdef MOZ_PHC
  // This is the earliest possible moment we can start PHC while still being
  // able to read prefs.
  mozilla::InitPHCState();
#endif

#ifdef MOZ_MEMORY
  // We did set up our main thread earlier and can read prefs now.
  mozilla::TaskController::SetupIdleMemoryCleanup();
#endif

  // After autoreg, but before we actually instantiate any components,
  // add any services listed in the "xpcom-directory-providers" category
  // to the directory service.
  nsDirectoryService::gService->RegisterCategoryProviders();

  // Init mozilla::SharedThreadPool (which needs the service manager).
  mozilla::SharedThreadPool::InitStatics();

  mozilla::scache::StartupCache::GetSingleton();
  mozilla::AvailableMemoryTracker::Init();

  // Notify observers of xpcom autoregistration start
  NS_CreateServicesFromCategory(NS_XPCOM_STARTUP_CATEGORY, nullptr,
                                NS_XPCOM_STARTUP_OBSERVER_ID);
#ifdef XP_WIN
  CreateAnonTempFileRemover();
#endif

  // The memory reporter manager is up and running -- register our reporters.
  RegisterStrongMemoryReporter(mozilla::MakeAndAddRef<ICUReporter>());
  RegisterStrongMemoryReporter(mozilla::MakeAndAddRef<OggReporter>());
  xpc::SelfHostedShmem::GetSingleton().InitMemoryReporter();

  mozilla::gecko_trace::Init();

  mozilla::Telemetry::Init();

  mozilla::BackgroundHangMonitor::Startup();

  const MessageLoop* const loop = MessageLoop::current();
  sMainHangMonitor = new mozilla::BackgroundHangMonitor(
      loop->thread_name().c_str(), loop->transient_hang_timeout(),
      loop->permanent_hang_timeout());

  mozilla::dom::JSExecutionManager::Initialize();

  if (aInitJSContext) {
    xpc::InitializeJSContext();
  }

  return NS_OK;
}

#undef XPCOM_INIT_FATAL

EXPORT_XPCOM_API(nsresult)
NS_InitMinimalXPCOM() {
  NS_SetMainThread();
  mozilla::TimeStamp::Startup();
  NS_LogInit();
  NS_InitAtomTable();

  // We don't have the arguments by hand here.  If logging has already been
  // initialized by a previous call to LogModule::Init with the arguments
  // passed, passing (0, nullptr) is alright here.
  mozilla::LogModule::Init(0, nullptr);

  GkRust_Init();

  nsresult rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Set up the timer globals/timer thread.
  rv = nsTimerImpl::Startup();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Create the Component/Service Manager
  nsComponentManagerImpl::gComponentManager = new nsComponentManagerImpl();
  NS_ADDREF(nsComponentManagerImpl::gComponentManager);

  rv = nsComponentManagerImpl::gComponentManager->Init();
  if (NS_FAILED(rv)) {
    NS_RELEASE(nsComponentManagerImpl::gComponentManager);
    return rv;
  }

  // Global cycle collector initialization.
  if (!nsCycleCollector_init()) {
    return NS_ERROR_UNEXPECTED;
  }

  mozilla::SharedThreadPool::InitStatics();
  mozilla::gecko_trace::Init();
  mozilla::Telemetry::Init();
  mozilla::BackgroundHangMonitor::Startup();

  return NS_OK;
}

//
// NS_ShutdownXPCOM()
//
// The shutdown sequence for xpcom would be
//
// - Notify "xpcom-shutdown" for modules to release primary (root) references
// - Shutdown XPCOM timers
// - Notify "xpcom-shutdown-threads" for thread joins
// - Shutdown the event queues
// - Release the Global Service Manager
//   - Release all service instances held by the global service manager
//   - Release the Global Service Manager itself
// - Release the Component Manager
//   - Release all factories cached by the Component Manager
//   - Notify module loaders to shut down
//   - Unload Libraries
//   - Release Contractid Cache held by Component Manager
//   - Release dll abstraction held by Component Manager
//   - Release the Registry held by Component Manager
//   - Finally, release the component manager itself
//
EXPORT_XPCOM_API(nsresult)
NS_ShutdownXPCOM(nsIServiceManager* aServMgr) {
  return mozilla::ShutdownXPCOM(aServMgr);
}

namespace mozilla {

void SetICUMemoryFunctions() {
  static bool sICUReporterInitialized = false;
  if (!sICUReporterInitialized) {
    if (!JS_SetICUMemoryFunctions(ICUReporter::Alloc, ICUReporter::Realloc,
                                  ICUReporter::Free)) {
      MOZ_CRASH("JS_SetICUMemoryFunctions failed.");
    }
    sICUReporterInitialized = true;
  }
}

nsresult ShutdownXPCOM(nsIServiceManager* aServMgr) {
  // Make sure the hang monitor is enabled for shutdown.
  BackgroundHangMonitor().NotifyActivity();

  if (!NS_IsMainThread()) {
    MOZ_CRASH("Shutdown on wrong thread");
  }

  // Notify observers of xpcom shutting down
  {
    // Block it so that the COMPtr will get deleted before we hit
    // servicemanager shutdown

    nsCOMPtr<nsIThread> thread = do_GetCurrentThread();
    if (NS_WARN_IF(!thread)) {
      return NS_ERROR_UNEXPECTED;
    }

    mozilla::AppShutdown::AdvanceShutdownPhase(
        mozilla::ShutdownPhase::XPCOMWillShutdown);

    // We want the service manager to be the subject of notifications
    nsCOMPtr<nsIServiceManager> mgr;
    (void)NS_GetServiceManager(getter_AddRefs(mgr));
    MOZ_DIAGNOSTIC_ASSERT(mgr != nullptr, "Service manager not present!");
    mozilla::AppShutdown::AdvanceShutdownPhase(
        mozilla::ShutdownPhase::XPCOMShutdown, nullptr, do_QueryInterface(mgr));

    // This must happen after the shutdown of media and widgets, which
    // are triggered by the NS_XPCOM_SHUTDOWN_OBSERVER_ID notification.
    gfxPlatform::ShutdownLayersIPC();

    mozilla::AppShutdown::AdvanceShutdownPhase(
        mozilla::ShutdownPhase::XPCOMShutdownThreads);
#ifdef DEBUG
    // Prime an assertion at ThreadEventTarget::Dispatch to avoid late
    // dispatches to non main-thread threads.
    ThreadEventTarget::XPCOMShutdownThreadsNotificationFinished();
#endif

    // Shutdown the timer thread and all timers that might still be alive
    nsTimerImpl::Shutdown();

    // Have an extra round of processing after the timers went away.
    NS_ProcessPendingEvents(thread);

    // Shutdown all remaining threads.  This method does not return until
    // all threads created using the thread manager (with the exception of
    // the main thread) have exited.
    nsThreadManager::get().ShutdownNonMainThreads();

    RefPtr<nsObserverService> observerService;
    CallGetService("@mozilla.org/observer-service;1",
                   (nsObserverService**)getter_AddRefs(observerService));
    if (observerService) {
      observerService->Shutdown();
    }

#ifdef NS_FREE_PERMANENT_DATA
    // In leak-checking / ASAN / etc. builds, shut down the Servo thread-pool,
    // which will wait for all the work to be done. For other builds, we don't
    // really want to wait on shutdown for possibly slow tasks.
    Servo_ShutdownThreadPool();
#endif

    // XPCOMShutdownFinal is the default phase for ClearOnShutdown.
    // This AdvanceShutdownPhase will thus free most ClearOnShutdown()'ed
    // smart pointers. Some destructors may fire extra main thread runnables
    // that will be processed inside AdvanceShutdownPhase.
    AppShutdown::AdvanceShutdownPhase(ShutdownPhase::XPCOMShutdownFinal);

    // Shutdown the main thread, processing our very last round of events, and
    // then mark that we've finished main thread event processing.
    nsThreadManager::get().ShutdownMainThread();
    gXPCOMMainThreadEventsAreDoomed = true;

    BackgroundHangMonitor().NotifyActivity();

    mozilla::dom::JSExecutionManager::Shutdown();
  }

  // XPCOM is officially in shutdown mode NOW
  // Set this only after the observers have been notified as this
  // will cause servicemanager to become inaccessible.
  mozilla::services::Shutdown();

  // We may have AddRef'd for the caller of NS_InitXPCOM, so release it
  // here again:
  NS_IF_RELEASE(aServMgr);

  // Shutdown global servicemanager
  if (nsComponentManagerImpl::gComponentManager) {
    nsComponentManagerImpl::gComponentManager->FreeServices();
  }

  // Remove the remaining main thread representations
  nsThreadManager::get().ReleaseMainThread();
  AbstractThread::ShutdownMainThread();

  // Release the directory service
  nsDirectoryService::gService = nullptr;

  free(gGREBinPath);
  gGREBinPath = nullptr;

  // FIXME: This can cause harmless writes from sqlite committing
  // log files. We have to ignore them before we can move
  // the mozilla::PoisonWrite call before this point. See bug
  // 834945 for the details.
  mozJSModuleLoader::UnloadLoaders();

  // Clear the profiler's JS context before cycle collection. The profiler will
  // notify the JS engine that it can let go of any data it's holding on to for
  // profiling purposes.
  PROFILER_CLEAR_JS_CONTEXT();

  mozilla::dom::SharedScriptCache::PrepareForLastCC();

  bool shutdownCollect;
#ifdef NS_FREE_PERMANENT_DATA
  shutdownCollect = true;
#else
  shutdownCollect = !!PR_GetEnv("MOZ_CC_RUN_DURING_SHUTDOWN");
#endif
  nsCycleCollector_shutdown(shutdownCollect);

  // There can be code trying to refer to global objects during the final cc
  // shutdown. This is the phase for such global objects to correctly release.
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::CCPostLastCycleCollection);

  mozilla::scache::StartupCache::DeleteSingleton();
  mozilla::ScriptPreloader::DeleteSingleton();

  PROFILER_MARKER_UNTYPED("Shutdown xpcom", OTHER);

  // Shutdown xpcom. This will release all loaders and cause others holding
  // a refcount to the component manager to release it.
  if (nsComponentManagerImpl::gComponentManager) {
    DebugOnly<nsresult> rv =
        (nsComponentManagerImpl::gComponentManager)->Shutdown();
    NS_ASSERTION(NS_SUCCEEDED(rv.value), "Component Manager shutdown failed.");
  } else {
    NS_WARNING("Component Manager was never created ...");
  }

  mozilla::ScriptPreloader::DeleteCacheDataSingleton();

  mozilla::dom::SharedScriptCache::DeleteSingleton();
  mozilla::SharedStyleSheetCache::DeleteSingleton();

  // Release shared memory which might be borrowed by the JS engine.
  xpc::SelfHostedShmem::Shutdown();

  // After all threads have been joined and the component manager has been shut
  // down, any remaining objects that could be holding NSS resources (should)
  // have been released, so we can safely shut down NSS.
  if (NSS_IsInitialized()) {
    nsNSSComponent::DoClearSSLExternalAndInternalSessionCache();
    if (NSS_Shutdown() != SECSuccess) {
      // If you're seeing this crash and/or warning, some NSS resources are
      // still in use (see bugs 1417680 and 1230312). Set the environment
      // variable 'MOZ_IGNORE_NSS_SHUTDOWN_LEAKS' to some value to ignore this.
      // Also, if leak checking is enabled, report this as a fake leak instead
      // of crashing.
#if defined(DEBUG) && !defined(ANDROID)
      if (!getenv("MOZ_IGNORE_NSS_SHUTDOWN_LEAKS") &&
          !getenv("XPCOM_MEM_BLOAT_LOG") && !getenv("XPCOM_MEM_LEAK_LOG") &&
          !getenv("XPCOM_MEM_REFCNT_LOG") && !getenv("XPCOM_MEM_COMPTR_LOG")) {
        MOZ_CRASH("NSS_Shutdown failed");
      } else {
#  ifdef NS_BUILD_REFCNT_LOGGING
        // Create a fake leak.
        NS_LogCtor((void*)0x100, "NSSShutdownFailed", 100);
#  endif  // NS_BUILD_REFCNT_LOGGING
        NS_WARNING("NSS_Shutdown failed");
      }
#else
      NS_WARNING("NSS_Shutdown failed");
#endif  // defined(DEBUG) && !defined(ANDROID)
    }
  }

  // Finally, release the component manager last because it unloads the
  // libraries:
  if (nsComponentManagerImpl::gComponentManager) {
    nsrefcnt cnt;
    NS_RELEASE2(nsComponentManagerImpl::gComponentManager, cnt);
    NS_ASSERTION(cnt == 0, "Component Manager being held past XPCOM shutdown.");
  }
  nsComponentManagerImpl::gComponentManager = nullptr;
  nsCategoryManager::Destroy();

  nsLanguageAtomService::Shutdown();

  GkRust_Shutdown();

#ifdef NS_FREE_PERMANENT_DATA
  // As we do shutdown Servo only in leak-checking builds, there may still
  // be async parse tasks going on in the Servo thread-pool in other builds.
  // CSS parsing heavily uses the atom table, so we can safely drop it only
  // if Servo has been stopped, too.
  NS_ShutdownAtomTable();
#endif

  NS_IF_RELEASE(gDebug);

  mozilla::ipc::IOThread::Shutdown();

  delete sMessageLoop;
  sMessageLoop = nullptr;

  mozilla::TaskController::Shutdown();

  if (sCommandLineWasInitialized) {
    CommandLine::Terminate();
    sCommandLineWasInitialized = false;
  }

  delete sExitManager;
  sExitManager = nullptr;

  Omnijar::CleanUp();

  BackgroundHangMonitor::Shutdown();

  delete sMainHangMonitor;
  sMainHangMonitor = nullptr;

  NS_LogTerm();

  return NS_OK;
}

}  // namespace mozilla
