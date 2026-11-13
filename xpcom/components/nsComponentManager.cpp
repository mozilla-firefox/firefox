/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsComponentManager.h"

#include <stdlib.h>

#include <memory>

#include "LogModulePrefWatcher.h"
#include "ManifestParser.h"
#include "StaticComponents.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Omnijar.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/URLPreloader.h"
#include "mozilla/Variant.h"
#include "nsArray.h"
#include "nsCOMPtr.h"
#include "nsCategoryManager.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIMutableArray.h"
#include "nsIObserverService.h"
#include "nsIStringEnumerator.h"
#include "nsISupports.h"
#include "nsLayoutModule.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsXPCOM.h"
#include "nscore.h"
#include "prthread.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::xpcom;

static LazyLogModule nsComponentManagerLog("nsComponentManager");

namespace {

class AutoIDString : public nsAutoCStringN<NSID_LENGTH> {
 public:
  explicit AutoIDString(const nsID& aID) {
    SetLength(NSID_LENGTH - 1);
    aID.ToProvidedString(
        *reinterpret_cast<char (*)[NSID_LENGTH]>(BeginWriting()));
  }
};

}  // namespace

namespace mozilla {
namespace xpcom {

using ProcessSelector = Module::ProcessSelector;

// Note: These must be kept in sync with the ProcessSelector definition in
// Module.h.
bool ProcessSelectorMatches(ProcessSelector aSelector) {
  GeckoProcessType type = XRE_GetProcessType();
  if (type == GeckoProcessType_GPU) {
    return !!(aSelector & Module::ALLOW_IN_GPU_PROCESS);
  }

  if (type == GeckoProcessType_RDD) {
    return !!(aSelector & Module::ALLOW_IN_RDD_PROCESS);
  }

  if (type == GeckoProcessType_VR) {
    return !!(aSelector & Module::ALLOW_IN_VR_PROCESS);
  }

  if (type == GeckoProcessType_Utility) {
    return !!(aSelector & Module::ALLOW_IN_UTILITY_PROCESS);
  }

  if (type == GeckoProcessType_GMPlugin) {
    return !!(aSelector & Module::ALLOW_IN_GMPLUGIN_PROCESS);
  }

  // Only allow XPCOM modules which can be loaded in all processes to be loaded
  // in the IPDLUnitTest process.
  if (type == GeckoProcessType_IPDLUnitTest) {
    return size_t(aSelector) == Module::kMaxProcessSelector;
  }

  if (aSelector & Module::MAIN_PROCESS_ONLY) {
    return type == GeckoProcessType_Default;
  }
  if (aSelector & Module::CONTENT_PROCESS_ONLY) {
    return type == GeckoProcessType_Content;
  }
  return true;
}

static bool gProcessMatchTable[Module::kMaxProcessSelector + 1];

bool FastProcessSelectorMatches(ProcessSelector aSelector) {
  return gProcessMatchTable[size_t(aSelector)];
}

}  // namespace xpcom
}  // namespace mozilla

namespace {

/**
 * A wrapper simple wrapper class, which can hold either a dynamic
 * nsFactoryEntry instance, or a static StaticModule entry, and transparently
 * forwards method calls to the wrapped object.
 *
 * This allows the same code to work with either static or dynamic modules
 * without caring about the difference.
 */
class MOZ_STACK_CLASS EntryWrapper final {
 public:
  explicit EntryWrapper(nsFactoryEntry* aEntry) : mEntry(aEntry) {}

  explicit EntryWrapper(const StaticModule* aEntry) : mEntry(aEntry) {}

  const nsID& CID() const {
    return mEntry.match([](const auto* e) -> const nsID& { return e->CID(); });
  }

  already_AddRefed<nsIFactory> GetFactory() const {
    return mEntry.match([](const auto* e) { return e->GetFactory(); });
  }

  bool IsSingleton() const {
    return mEntry.match([](const auto* e) { return e->IsSingleton(); });
  }

  /**
   * Creates an instance of the underlying component. This should be used in
   * preference to GetFactory()->CreateInstance() where appropriate, since it
   * side-steps the necessity of creating a nsIFactory instance for static
   * modules.
   */
  nsresult CreateInstance(const nsIID& aIID, void** aResult) const {
    return mEntry.match(
        [&](const auto* e) { return e->CreateInstance(aIID, aResult); });
  }

  /**
   * Returns the cached service instance for this entry, if any. This should
   * only be accessed while mLock is held.
   */
  nsISupports* ServiceInstance() const {
    return mEntry.match([](const auto* e) { return e->ServiceInstance(); });
  }
  void SetServiceInstance(already_AddRefed<nsISupports> aInst) {
    mEntry.match(
        [&](const auto& e) { e->SetServiceInstance(std::move(aInst)); });
  }

  /**
   * Returns the description string for the module this entry belongs to.
   * Currently always returns "<unknown module>".
   */
  nsCString ModuleDescription() const { return "<unknown module>"_ns; }

 private:
  Variant<nsFactoryEntry*, const StaticModule*> mEntry;
};

}  // namespace

// this is safe to call during InitXPCOM
static already_AddRefed<nsIFile> GetLocationFromDirectoryService(
    const char* aProp) {
  nsCOMPtr<nsIProperties> directoryService;
  nsDirectoryService::Create(NS_GET_IID(nsIProperties),
                             getter_AddRefs(directoryService));

  if (!directoryService) {
    return nullptr;
  }

  nsCOMPtr<nsIFile> file;
  nsresult rv =
      directoryService->Get(aProp, NS_GET_IID(nsIFile), getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return file.forget();
}

static already_AddRefed<nsIFile> CloneAndAppend(nsIFile* aBase,
                                                const nsACString& aAppend) {
  nsCOMPtr<nsIFile> f;
  aBase->Clone(getter_AddRefs(f));
  if (!f) {
    return nullptr;
  }

  f->AppendNative(aAppend);
  return f.forget();
}

////////////////////////////////////////////////////////////////////////////////
// nsComponentManagerImpl
////////////////////////////////////////////////////////////////////////////////

nsresult nsComponentManagerImpl::Create(REFNSIID aIID, void** aResult) {
  if (!gComponentManager) {
    return NS_ERROR_FAILURE;
  }

  return gComponentManager->QueryInterface(aIID, aResult);
}

static const int CONTRACTID_HASHTABLE_INITIAL_LENGTH = 8;

nsComponentManagerImpl::nsComponentManagerImpl()
    : mFactories(CONTRACTID_HASHTABLE_INITIAL_LENGTH),
      mContractIDs(CONTRACTID_HASHTABLE_INITIAL_LENGTH),
      mLock("nsComponentManagerImpl.mLock"),
      mStatus(NOT_INITIALIZED) {}

nsTArray<nsComponentManagerImpl::ComponentLocation>*
    nsComponentManagerImpl::sModuleLocations;

/* static */
void nsComponentManagerImpl::InitializeModuleLocations() {
  if (sModuleLocations) {
    return;
  }

  sModuleLocations = new nsTArray<ComponentLocation>;
}

nsresult nsComponentManagerImpl::Init() {
  {
    gProcessMatchTable[size_t(ProcessSelector::ANY_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ANY_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::MAIN_PROCESS_ONLY)] =
        ProcessSelectorMatches(ProcessSelector::MAIN_PROCESS_ONLY);
    gProcessMatchTable[size_t(ProcessSelector::CONTENT_PROCESS_ONLY)] =
        ProcessSelectorMatches(ProcessSelector::CONTENT_PROCESS_ONLY);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_GPU_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_GPU_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_VR_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_VR_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_RDD_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_RDD_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_GMPLUGIN_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_GMPLUGIN_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_GPU_AND_MAIN_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_GPU_AND_MAIN_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_GPU_AND_VR_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_GPU_AND_VR_PROCESS);
    gProcessMatchTable[size_t(ProcessSelector::ALLOW_IN_GPU_AND_RDD_PROCESS)] =
        ProcessSelectorMatches(ProcessSelector::ALLOW_IN_GPU_AND_RDD_PROCESS);
    gProcessMatchTable[size_t(
        ProcessSelector::ALLOW_IN_GPU_RDD_VR_AND_UTILITY_PROCESS)] =
        ProcessSelectorMatches(
            ProcessSelector::ALLOW_IN_GPU_RDD_VR_AND_UTILITY_PROCESS);
    gProcessMatchTable[size_t(
        ProcessSelector::ALLOW_IN_GPU_RDD_VR_UTILITY_AND_GMPLUGIN_PROCESS)] =
        ProcessSelectorMatches(
            ProcessSelector::ALLOW_IN_GPU_RDD_VR_UTILITY_AND_GMPLUGIN_PROCESS);
  }

  MOZ_ASSERT(NOT_INITIALIZED == mStatus);

  nsCOMPtr<nsIFile> greDir = GetLocationFromDirectoryService(NS_GRE_DIR);
  nsCOMPtr<nsIFile> appDir =
      GetLocationFromDirectoryService(NS_XPCOM_CURRENT_PROCESS_DIR);

  nsCategoryManager::GetSingleton()->SuppressNotifications(true);

  auto* catMan = nsCategoryManager::GetSingleton();
  for (const auto& cat : gStaticCategories) {
    for (const auto& entry : cat) {
      if (entry.Active()) {
        catMan->AddCategoryEntry(cat.Name(), entry.Entry(), entry.Value());
      }
    }
  }

  // This needs to be initialized late enough, so that preferences service can
  // be accessed but before the IO service, and we want it in all process types.
  xpc::ReadOnlyPage::Init();

  bool loadChromeManifests;
  switch (XRE_GetProcessType()) {
    // We are going to assume that only a select few (see below) process types
    // want to load chrome manifests, and that any new process types will not
    // want to load them, because they're not going to be executing JS.
    default:
      loadChromeManifests = false;
      break;

    // XXX The check this code replaced implicitly let through all of these
    // process types, but presumably only the default (parent) and content
    // processes really need chrome manifests...?
    case GeckoProcessType_Default:
    case GeckoProcessType_Content:
      loadChromeManifests = true;
      break;
  }

  if (loadChromeManifests) {
    // This needs to be called very early, before anything in nsLayoutModule is
    // used, and before any calls are made into the JS engine.
    nsLayoutModuleInitialize();

    mJSLoaderReady = true;

    // The overall order in which chrome.manifests are expected to be treated
    // is the following:
    // - greDir's omni.ja or greDir
    // - appDir's omni.ja or appDir

    InitializeModuleLocations();
    ComponentLocation* cl = sModuleLocations->AppendElement();
    cl->type = NS_APP_LOCATION;
    RefPtr<nsZipArchive> greOmnijar =
        mozilla::Omnijar::GetReader(mozilla::Omnijar::GRE);
    if (greOmnijar) {
      cl->location.Init(greOmnijar, "chrome.manifest"_ns);
    } else {
      nsCOMPtr<nsIFile> lf = CloneAndAppend(greDir, "chrome.manifest"_ns);
      cl->location.Init(lf);
    }

    RefPtr<nsZipArchive> appOmnijar =
        mozilla::Omnijar::GetReader(mozilla::Omnijar::APP);
    if (appOmnijar) {
      cl = sModuleLocations->AppendElement();
      cl->type = NS_APP_LOCATION;
      cl->location.Init(appOmnijar, "chrome.manifest"_ns);
    } else {
      bool equals = false;
      appDir->Equals(greDir, &equals);
      if (!equals) {
        cl = sModuleLocations->AppendElement();
        cl->type = NS_APP_LOCATION;
        nsCOMPtr<nsIFile> lf = CloneAndAppend(appDir, "chrome.manifest"_ns);
        cl->location.Init(lf);
      }
    }

    RereadChromeManifests(false);
  }

  nsCategoryManager::GetSingleton()->SuppressNotifications(false);

  RegisterWeakMemoryReporter(this);

  // NB: The logging preference watcher needs to be registered late enough in
  // startup that it's okay to use the preference system, but also as soon as
  // possible so that log modules enabled via preferences are turned on as
  // early as possible.
  //
  // We can't initialize the preference watcher when the log module manager is
  // initialized, as a number of things attempt to start logging before the
  // preference system is initialized.
  //
  // The preference system is registered as a component so at this point during
  // component manager initialization we know it is setup and we can register
  // for notifications.
  LogModulePrefWatcher::RegisterPrefWatcher();

  // Unfortunately, we can't register the nsCategoryManager memory reporter
  // in its constructor (which is triggered by the GetSingleton() call
  // above) because the memory reporter manager isn't initialized at that
  // point.  So we wait until now.
  nsCategoryManager::GetSingleton()->InitMemoryReporter();

  MOZ_LOG(nsComponentManagerLog, LogLevel::Debug,
          ("nsComponentManager: Initialized."));

  mStatus = NORMAL;

  MOZ_ASSERT(!XRE_IsContentProcess() ||
                 CONTRACTID_HASHTABLE_INITIAL_LENGTH <= 8 ||
                 mFactories.Count() > CONTRACTID_HASHTABLE_INITIAL_LENGTH / 3,
             "Initial component hashtable size is too large");

  return NS_OK;
}

static void DoRegisterManifest(NSLocationType aType, FileLocation& aFile,
                               bool aChromeOnly) {
  auto result = URLPreloader::Read(aFile);
  if (result.isOk()) {
    nsCString buf(result.unwrap());
    ParseManifest(aType, aFile, buf.BeginWriting(), aChromeOnly);
  } else {
    nsCString uri;
    aFile.GetURIString(uri);
    LogMessage("Could not read chrome manifest '%s'.", uri.get());
  }
}

void nsComponentManagerImpl::RegisterManifest(NSLocationType aType,
                                              FileLocation& aFile,
                                              bool aChromeOnly) {
  DoRegisterManifest(aType, aFile, aChromeOnly);
}

void nsComponentManagerImpl::ManifestManifest(ManifestProcessingContext& aCx,
                                              int aLineNo, char* const* aArgv) {
  char* file = aArgv[0];
  FileLocation f(aCx.mFile, nsDependentCString(file));
  RegisterManifest(aCx.mType, f, aCx.mChromeOnly);
}

void nsComponentManagerImpl::ManifestCategory(ManifestProcessingContext& aCx,
                                              int aLineNo, char* const* aArgv) {
  char* category = aArgv[0];
  char* key = aArgv[1];
  char* value = aArgv[2];

  nsCategoryManager::GetSingleton()->AddCategoryEntry(
      nsDependentCString(category), nsDependentCString(key),
      nsDependentCString(value));
}

void nsComponentManagerImpl::RereadChromeManifests(bool aChromeOnly) {
  for (uint32_t i = 0; i < sModuleLocations->Length(); ++i) {
    ComponentLocation& l = sModuleLocations->ElementAt(i);
    RegisterManifest(l.type, l.location, aChromeOnly);
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "chrome-manifests-loaded", nullptr);
  }
}

nsresult nsComponentManagerImpl::Shutdown(void) {
  MOZ_ASSERT(NORMAL == mStatus);

  mStatus = SHUTDOWN_IN_PROGRESS;

  // Shutdown the component manager
  MOZ_LOG(nsComponentManagerLog, LogLevel::Debug,
          ("nsComponentManager: Beginning Shutdown."));

  UnregisterWeakMemoryReporter(this);

  // Release all cached factories
  mContractIDs.Clear();
  mFactories.Clear();  // XXX release the objects, don't just clear

  StaticComponents::Shutdown();

  delete sModuleLocations;

  mStatus = SHUTDOWN_COMPLETE;

  MOZ_LOG(nsComponentManagerLog, LogLevel::Debug,
          ("nsComponentManager: Shutdown complete."));

  return NS_OK;
}

nsComponentManagerImpl::~nsComponentManagerImpl() {
  MOZ_LOG(nsComponentManagerLog, LogLevel::Debug,
          ("nsComponentManager: Beginning destruction."));

  if (SHUTDOWN_COMPLETE != mStatus) {
    Shutdown();
  }

  MOZ_LOG(nsComponentManagerLog, LogLevel::Debug,
          ("nsComponentManager: Destroyed."));
}

NS_IMPL_ISUPPORTS(nsComponentManagerImpl, nsIComponentManager,
                  nsIServiceManager, nsIComponentRegistrar,
                  nsISupportsWeakReference, nsIInterfaceRequestor,
                  nsIMemoryReporter)

nsresult nsComponentManagerImpl::GetInterface(const nsIID& aUuid,
                                              void** aResult) {
  NS_WARNING("This isn't supported");
  // fall through to QI as anything QIable is a superset of what can be
  // got via the GetInterface()
  return QueryInterface(aUuid, aResult);
}

Maybe<EntryWrapper> nsComponentManagerImpl::LookupByCID(const nsID& aCID) {
  return LookupByCID(MonitorAutoLock(mLock), aCID);
}

Maybe<EntryWrapper> nsComponentManagerImpl::LookupByCID(const MonitorAutoLock&,
                                                        const nsID& aCID) {
  if (const StaticModule* module = StaticComponents::LookupByCID(aCID)) {
    return Some(EntryWrapper(module));
  }
  if (nsFactoryEntry* entry = mFactories.Get(&aCID)) {
    return Some(EntryWrapper(entry));
  }
  return Nothing();
}

Maybe<EntryWrapper> nsComponentManagerImpl::LookupByContractID(
    const nsACString& aContractID) {
  return LookupByContractID(MonitorAutoLock(mLock), aContractID);
}

Maybe<EntryWrapper> nsComponentManagerImpl::LookupByContractID(
    const MonitorAutoLock&, const nsACString& aContractID) {
  if (const StaticModule* module =
          StaticComponents::LookupByContractID(aContractID)) {
    return Some(EntryWrapper(module));
  }
  if (nsFactoryEntry* entry = mContractIDs.Get(aContractID)) {
    // UnregisterFactory might have left a stale nsFactoryEntry in
    // mContractIDs, so we should check to see whether this entry has
    // anything useful.
    if (entry->mFactory || entry->ServiceInstance()) {
      return Some(EntryWrapper(entry));
    }
  }
  return Nothing();
}

/**
 * CreateInstance()
 *
 * Create an instance of an object that implements an interface and belongs
 * to the implementation aClass using the factory. The factory is immediately
 * released and not held onto for any longer.
 */
NS_IMETHODIMP
nsComponentManagerImpl::CreateInstance(const nsCID& aClass, const nsIID& aIID,
                                       void** aResult) {
  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't CreateInstance during shutdown. CID: %s; IID: %s",
             AutoIDString(aClass).get(), AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  if (!aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  *aResult = nullptr;

  Maybe<EntryWrapper> entry = LookupByCID(aClass);

  if (!entry) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  if (NS_WARN_IF(entry->IsSingleton())) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Error,
            ("Not allowed to CreateInstance for singleton %s",
             AutoIDString(aClass).get()));
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsresult rv;
  nsCOMPtr<nsIFactory> factory = entry->GetFactory();
  if (factory) {
    rv = factory->CreateInstance(aIID, aResult);
    if (NS_SUCCEEDED(rv) && !*aResult) {
      NS_ERROR("Factory did not return an object but returned success!");
      rv = NS_ERROR_SERVICE_NOT_AVAILABLE;
    }
  } else {
    // Translate error values
    rv = NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  MOZ_LOG(
      nsComponentManagerLog, LogLevel::Warning,
      ("nsComponentManager: CreateInstance(%s) %s", AutoIDString(aClass).get(),
       NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));

  return rv;
}

/**
 * CreateInstanceByContractID()
 *
 * A variant of CreateInstance() that creates an instance of the object that
 * implements the interface aIID and whose implementation has a contractID
 * aContractID.
 *
 * This is only a convenience routine that turns around can calls the
 * CreateInstance() with classid and iid.
 */
NS_IMETHODIMP
nsComponentManagerImpl::CreateInstanceByContractID(const char* aContractID,
                                                   const nsIID& aIID,
                                                   void** aResult) {
  if (NS_WARN_IF(!aContractID)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't CreateInstanceByContractID during shutdown. ContractID: "
             "%s; IID: %s",
             aContractID, AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  if (!aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  *aResult = nullptr;

  Maybe<EntryWrapper> entry =
      LookupByContractID(nsDependentCString(aContractID));

  if (!entry) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  if (NS_WARN_IF(entry->IsSingleton())) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Error,
            ("Not allowed to CreateInstanceByContractID for singleton %s",
             aContractID));
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsresult rv;
  nsCOMPtr<nsIFactory> factory = entry->GetFactory();
  if (factory) {
    rv = factory->CreateInstance(aIID, aResult);
    if (NS_SUCCEEDED(rv) && !*aResult) {
      NS_ERROR("Factory did not return an object but returned success!");
      rv = NS_ERROR_SERVICE_NOT_AVAILABLE;
    }
  } else {
    // Translate error values
    rv = NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
          ("nsComponentManager: CreateInstanceByContractID(%s) %s", aContractID,
           NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));

  return rv;
}

nsresult nsComponentManagerImpl::FreeServices() {
  NS_ASSERTION(gXPCOMShuttingDown,
               "Must be shutting down in order to free all services");

  if (!gXPCOMShuttingDown) {
    return NS_ERROR_FAILURE;
  }

  for (nsFactoryEntry* entry : mFactories.Values()) {
    entry->mFactory = nullptr;
    entry->SetServiceInstance(nullptr);
  }

  for (const auto& module : gStaticModules) {
    module.SetServiceInstance(nullptr);
  }

  return NS_OK;
}

// This should only ever be called within the monitor!
nsComponentManagerImpl::PendingServiceInfo*
nsComponentManagerImpl::AddPendingService(const nsCID& aServiceCID,
                                          PRThread* aThread) {
  PendingServiceInfo* newInfo = mPendingServices.AppendElement();
  if (newInfo) {
    newInfo->cid = &aServiceCID;
    newInfo->thread = aThread;
  }
  return newInfo;
}

// This should only ever be called within the monitor!
void nsComponentManagerImpl::RemovePendingService(MonitorAutoLock& aLock,
                                                  const nsCID& aServiceCID) {
  uint32_t pendingCount = mPendingServices.Length();
  for (uint32_t index = 0; index < pendingCount; ++index) {
    const PendingServiceInfo& info = mPendingServices.ElementAt(index);
    if (info.cid->Equals(aServiceCID)) {
      mPendingServices.RemoveElementAt(index);
      aLock.NotifyAll();
      return;
    }
  }
}

// This should only ever be called within the monitor!
PRThread* nsComponentManagerImpl::GetPendingServiceThread(
    const nsCID& aServiceCID) const {
  uint32_t pendingCount = mPendingServices.Length();
  for (uint32_t index = 0; index < pendingCount; ++index) {
    const PendingServiceInfo& info = mPendingServices.ElementAt(index);
    if (info.cid->Equals(aServiceCID)) {
      return info.thread;
    }
  }
  return nullptr;
}

nsresult nsComponentManagerImpl::GetServiceLocked(Maybe<MonitorAutoLock>& aLock,
                                                  EntryWrapper& aEntry,
                                                  const nsIID& aIID,
                                                  void** aResult) {
  MOZ_ASSERT(aLock.isSome());
  if (!aLock.isSome()) {
    return NS_ERROR_INVALID_ARG;
  }

  if (auto* service = aEntry.ServiceInstance()) {
    aLock.reset();
    return service->QueryInterface(aIID, aResult);
  }

  PRThread* currentPRThread = PR_GetCurrentThread();
  MOZ_ASSERT(currentPRThread, "This should never be null!");

  PRThread* pendingPRThread;
  while ((pendingPRThread = GetPendingServiceThread(aEntry.CID()))) {
    if (pendingPRThread == currentPRThread) {
      NS_ERROR("Recursive GetService!");
      return NS_ERROR_NOT_AVAILABLE;
    }

    aLock->Wait();
  }

  // It's still possible that the other thread failed to create the
  // service so we're not guaranteed to have an entry or service yet.
  if (auto* service = aEntry.ServiceInstance()) {
    aLock.reset();
    return service->QueryInterface(aIID, aResult);
  }

  DebugOnly<PendingServiceInfo*> newInfo =
      AddPendingService(aEntry.CID(), currentPRThread);
  NS_ASSERTION(newInfo, "Failed to add info to the array!");

  // We need to not be holding the service manager's lock while calling
  // CreateInstance, because it invokes user code which could try to re-enter
  // the service manager:

  nsCOMPtr<nsISupports> service;
  auto cleanup = MakeScopeExit([&]() {
    // `service` must be released after the lock is released, so if we fail and
    // still have a reference, release the lock before releasing it.
    if (service) {
      MOZ_ASSERT(aLock.isSome());
      aLock.reset();
      service = nullptr;
    }
  });
  nsresult rv;
  mLock.AssertCurrentThreadOwns();
  {
    MonitorAutoUnlock unlock(mLock);
    AUTO_PROFILER_MARKER_TEXT(
        "GetService", OTHER, MarkerStack::Capture(),
        nsDependentCString(nsIDToCString(aEntry.CID()).get()));
    rv = aEntry.CreateInstance(aIID, getter_AddRefs(service));
  }
  if (NS_SUCCEEDED(rv) && !service) {
    NS_ERROR("Factory did not return an object but returned success");
    return NS_ERROR_SERVICE_NOT_AVAILABLE;
  }

#ifdef DEBUG
  pendingPRThread = GetPendingServiceThread(aEntry.CID());
  MOZ_ASSERT(pendingPRThread == currentPRThread,
             "Pending service array has been changed!");
#endif
  MOZ_ASSERT(aLock.isSome());
  RemovePendingService(*aLock, aEntry.CID());

  if (NS_FAILED(rv)) {
    return rv;
  }

  NS_ASSERTION(!aEntry.ServiceInstance(),
               "Created two instances of a service!");

  aEntry.SetServiceInstance(service.forget());

  aLock.reset();

  *aResult = do_AddRef(aEntry.ServiceInstance()).take();
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::GetService(const nsCID& aClass, const nsIID& aIID,
                                   void** aResult) {
  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't GetService during shutdown. CID: %s; IID: %s",
             AutoIDString(aClass).get(), AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  Maybe<MonitorAutoLock> lock(std::in_place, mLock);

  Maybe<EntryWrapper> entry = LookupByCID(*lock, aClass);
  if (!entry) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  return GetServiceLocked(lock, *entry, aIID, aResult);
}

nsresult nsComponentManagerImpl::GetService(ModuleID aId, const nsIID& aIID,
                                            void** aResult) {
  const auto& entry = gStaticModules[size_t(aId)];
  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't GetService during shutdown. CID: %s; IID: %s",
             AutoIDString(entry.CID()).get(), AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  Maybe<MonitorAutoLock> lock(std::in_place, mLock);

  Maybe<EntryWrapper> wrapper;
  if (entry.Overridable()) {
    // If we expect this service to be overridden by test code, we need to look
    // it up by contract ID every time.
    wrapper = LookupByContractID(*lock, entry.ContractID());
    if (!wrapper) {
      return NS_ERROR_FACTORY_NOT_REGISTERED;
    }
  } else if (!entry.Active()) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  } else {
    wrapper.emplace(&entry);
  }
  return GetServiceLocked(lock, *wrapper, aIID, aResult);
}

NS_IMETHODIMP
nsComponentManagerImpl::IsServiceInstantiated(const nsCID& aClass,
                                              const nsIID& aIID,
                                              bool* aResult) {
  if (gXPCOMShuttingDown) {
    MOZ_LOG(
        nsComponentManagerLog, LogLevel::Warning,
        ("Can't call IsServiceInstantiated during shutdown. CID: %s; IID: %s",
         AutoIDString(aClass).get(), AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  if (Maybe<EntryWrapper> entry = LookupByCID(aClass)) {
    if (auto* service = entry->ServiceInstance()) {
      nsCOMPtr<nsISupports> instance;
      nsresult rv = service->QueryInterface(aIID, getter_AddRefs(instance));
      *aResult = (instance != nullptr);
      return rv;
    }
  }

  *aResult = false;
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsServiceInstantiatedByContractID(
    const char* aContractID, const nsIID& aIID, bool* aResult) {
  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't call IsServiceInstantiatedByContractID during shutdown. "
             "ContractID: %s; IID: %s",
             aContractID, AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  if (Maybe<EntryWrapper> entry =
          LookupByContractID(nsDependentCString(aContractID))) {
    if (auto* service = entry->ServiceInstance()) {
      nsCOMPtr<nsISupports> instance;
      nsresult rv = service->QueryInterface(aIID, getter_AddRefs(instance));
      *aResult = (instance != nullptr);
      return rv;
    }
  }

  *aResult = false;
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::GetServiceByContractID(const char* aContractID,
                                               const nsIID& aIID,
                                               void** aResult) {
  if (gXPCOMShuttingDown) {
    MOZ_LOG(nsComponentManagerLog, LogLevel::Warning,
            ("Can't call GetServiceByContractID during shutdown. "
             "ContractID: %s; IID: %s",
             aContractID, AutoIDString(aIID).get()));
    return NS_ERROR_UNEXPECTED;
  }

  AUTO_PROFILER_LABEL_DYNAMIC_CSTR_NONSENSITIVE("GetServiceByContractID", OTHER,
                                                aContractID);
  Maybe<MonitorAutoLock> lock(std::in_place, mLock);

  Maybe<EntryWrapper> entry =
      LookupByContractID(*lock, nsDependentCString(aContractID));
  if (!entry) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  return GetServiceLocked(lock, *entry, aIID, aResult);
}

NS_IMETHODIMP
nsComponentManagerImpl::RegisterFactory(const nsCID& aClass, const char* aName,
                                        const char* aContractID,
                                        nsIFactory* aFactory) {
  if (!aFactory) {
    // If a null factory is passed in, this call just wants to reset
    // the contract ID to point to an existing CID entry.
    if (!aContractID) {
      return NS_ERROR_INVALID_ARG;
    }

    nsDependentCString contractID(aContractID);

    MonitorAutoLock lock(mLock);
    nsFactoryEntry* oldf = mFactories.Get(&aClass);
    if (oldf) {
      StaticComponents::InvalidateContractID(contractID);
      mContractIDs.InsertOrUpdate(contractID, oldf);
      return NS_OK;
    }

    if (StaticComponents::LookupByCID(aClass)) {
      // If this is the CID of a static module, just reset the invalid bit of
      // the static entry for this contract ID, and assume it points to the
      // correct class.
      if (StaticComponents::InvalidateContractID(contractID, false)) {
        mContractIDs.Remove(contractID);
        return NS_OK;
      }
    }
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  auto f = std::make_unique<nsFactoryEntry>(aClass, aFactory);

  MonitorAutoLock lock(mLock);
  return mFactories.WithEntryHandle(&f->mCID, [&](auto&& entry) {
    if (entry) {
      return NS_ERROR_FACTORY_EXISTS;
    }
    if (StaticComponents::LookupByCID(f->mCID)) {
      return NS_ERROR_FACTORY_EXISTS;
    }
    if (aContractID) {
      nsDependentCString contractID(aContractID);
      mContractIDs.InsertOrUpdate(contractID, f.get());
      // We allow dynamically-registered contract IDs to override static
      // entries, so invalidate any static entry for this contract ID.
      StaticComponents::InvalidateContractID(contractID);
    }
    entry.Insert(f.release());

    return NS_OK;
  });
}

NS_IMETHODIMP
nsComponentManagerImpl::UnregisterFactory(const nsCID& aClass,
                                          nsIFactory* aFactory) {
  // Don't release the dying factory or service object until releasing
  // the component manager monitor.
  nsCOMPtr<nsIFactory> dyingFactory;
  nsCOMPtr<nsISupports> dyingServiceObject;

  {
    MonitorAutoLock lock(mLock);
    auto entry = mFactories.Lookup(&aClass);
    nsFactoryEntry* f = entry ? entry.Data() : nullptr;
    if (!f || f->mFactory != aFactory) {
      // Note: We do not support unregistering static factories.
      return NS_ERROR_FACTORY_NOT_REGISTERED;
    }

    entry.Remove();

    // This might leave a stale contractid -> factory mapping in
    // place, so null out the factory entry (see
    // nsFactoryEntry::GetFactory)
    f->mFactory.swap(dyingFactory);
    f->mServiceObject.swap(dyingServiceObject);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::AutoRegister(nsIFile* aLocation) {
  XRE_AddManifestLocation(NS_EXTENSION_LOCATION, aLocation);
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsCIDRegistered(const nsCID& aClass, bool* aResult) {
  *aResult = LookupByCID(aClass).isSome();
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsContractIDRegistered(const char* aClass,
                                               bool* aResult) {
  if (NS_WARN_IF(!aClass)) {
    return NS_ERROR_INVALID_ARG;
  }

  Maybe<EntryWrapper> entry = LookupByContractID(nsDependentCString(aClass));

  *aResult = entry.isSome();
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::GetContractIDs(nsTArray<nsCString>& aResult) {
  aResult = ToTArray<nsTArray<nsCString>>(mContractIDs.Keys());

  for (const auto& entry : gContractEntries) {
    if (!entry.Invalid()) {
      aResult.AppendElement(entry.ContractID());
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::ContractIDToCID(const char* aContractID,
                                        nsCID** aResult) {
  {
    MonitorAutoLock lock(mLock);
    Maybe<EntryWrapper> entry =
        LookupByContractID(lock, nsDependentCString(aContractID));
    if (entry) {
      *aResult = (nsCID*)moz_xmalloc(sizeof(nsCID));
      **aResult = entry->CID();
      return NS_OK;
    }
  }
  *aResult = nullptr;
  return NS_ERROR_FACTORY_NOT_REGISTERED;
}

MOZ_DEFINE_MALLOC_SIZE_OF(ComponentManagerMallocSizeOf)

NS_IMETHODIMP
nsComponentManagerImpl::CollectReports(nsIHandleReportCallback* aHandleReport,
                                       nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/xpcom/component-manager", KIND_HEAP, UNITS_BYTES,
                     SizeOfIncludingThis(ComponentManagerMallocSizeOf),
                     "Memory used for the XPCOM component manager.");

  return NS_OK;
}

size_t nsComponentManagerImpl::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) {
  size_t n = aMallocSizeOf(this);

  MonitorAutoLock lock(mLock);

  n += mFactories.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& data : mFactories.Values()) {
    n += data->SizeOfIncludingThis(aMallocSizeOf);
  }

  n += mContractIDs.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& key : mContractIDs.Keys()) {
    // We don't measure the nsFactoryEntry data because it's owned by
    // mFactories (which is measured above).
    n += key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  }

  if (sModuleLocations) {
    n += sModuleLocations->ShallowSizeOfIncludingThis(aMallocSizeOf);
  }

  n += mPendingServices.ShallowSizeOfExcludingThis(aMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mMon
  // - sModuleLocations' entries

  return n;
}

////////////////////////////////////////////////////////////////////////////////
// nsFactoryEntry
////////////////////////////////////////////////////////////////////////////////

nsFactoryEntry::nsFactoryEntry(const nsCID& aCID, nsIFactory* aFactory)
    : mCID(aCID), mFactory(aFactory) {}

already_AddRefed<nsIFactory> nsFactoryEntry::GetFactory() const {
  nsComponentManagerImpl::gComponentManager->mLock.AssertNotCurrentThreadOwns();

  nsCOMPtr<nsIFactory> factory = mFactory;
  return factory.forget();
}

nsresult nsFactoryEntry::CreateInstance(const nsIID& aIID,
                                        void** aResult) const {
  nsCOMPtr<nsIFactory> factory = GetFactory();
  NS_ENSURE_TRUE(factory, NS_ERROR_FAILURE);
  return factory->CreateInstance(aIID, aResult);
}

size_t nsFactoryEntry::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) {
  size_t n = aMallocSizeOf(this);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mCID;
  // - mFactory;
  // - mServiceObject;

  return n;
}

////////////////////////////////////////////////////////////////////////////////
// Static Access Functions
////////////////////////////////////////////////////////////////////////////////

nsresult NS_GetComponentManager(nsIComponentManager** aResult) {
  if (!nsComponentManagerImpl::gComponentManager) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  NS_ADDREF(*aResult = nsComponentManagerImpl::gComponentManager);
  return NS_OK;
}

nsresult NS_GetServiceManager(nsIServiceManager** aResult) {
  if (!nsComponentManagerImpl::gComponentManager) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  NS_ADDREF(*aResult = nsComponentManagerImpl::gComponentManager);
  return NS_OK;
}

nsresult NS_GetComponentRegistrar(nsIComponentRegistrar** aResult) {
  if (!nsComponentManagerImpl::gComponentManager) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  NS_ADDREF(*aResult = nsComponentManagerImpl::gComponentManager);
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::GetComponentESModules(
    nsIUTF8StringEnumerator** aESModules) {
  nsCOMPtr<nsIUTF8StringEnumerator> result =
      StaticComponents::GetComponentESModules();
  result.forget(aESModules);
  return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::GetManifestLocations(nsIArray** aLocations) {
  NS_ENSURE_ARG_POINTER(aLocations);
  *aLocations = nullptr;

  if (!sModuleLocations) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsIMutableArray> locations = nsArray::Create();
  nsresult rv;
  for (uint32_t i = 0; i < sModuleLocations->Length(); ++i) {
    ComponentLocation& l = sModuleLocations->ElementAt(i);
    FileLocation loc = l.location;
    nsCString uriString;
    loc.GetURIString(uriString);
    nsCOMPtr<nsIURI> uri;
    rv = NS_NewURI(getter_AddRefs(uri), uriString);
    if (NS_SUCCEEDED(rv)) {
      locations->AppendElement(uri);
    }
  }

  locations.forget(aLocations);
  return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
XRE_AddManifestLocation(NSLocationType aType, nsIFile* aLocation) {
  nsComponentManagerImpl::InitializeModuleLocations();
  nsComponentManagerImpl::ComponentLocation* c =
      nsComponentManagerImpl::sModuleLocations->AppendElement();
  c->type = aType;
  c->location.Init(aLocation);

  if (nsComponentManagerImpl::gComponentManager &&
      nsComponentManagerImpl::NORMAL ==
          nsComponentManagerImpl::gComponentManager->mStatus) {
    nsComponentManagerImpl::gComponentManager->RegisterManifest(
        aType, c->location, false);
  }

  return NS_OK;
}

// Expose some important global interfaces to rust for the rust xpcom API. These
// methods return a non-owning reference to the component manager, which should
// live for the lifetime of XPCOM.
extern "C" {

const nsIComponentManager* Gecko_GetComponentManager() {
  return nsComponentManagerImpl::gComponentManager;
}

const nsIServiceManager* Gecko_GetServiceManager() {
  return nsComponentManagerImpl::gComponentManager;
}

const nsIComponentRegistrar* Gecko_GetComponentRegistrar() {
  return nsComponentManagerImpl::gComponentManager;
}

// FFI-compatible version of `GetServiceHelper::operator()`.
nsresult Gecko_GetServiceByModuleID(ModuleID aId, const nsIID* aIID,
                                    void** aResult) {
  return nsComponentManagerImpl::gComponentManager->GetService(aId, *aIID,
                                                               aResult);
}

// FFI-compatible version of `CreateInstanceHelper::operator()`.
nsresult Gecko_CreateInstanceByModuleID(ModuleID aId, const nsIID* aIID,
                                        void** aResult) {
  const auto& entry = gStaticModules[size_t(aId)];
  if (!entry.Active()) {
    return NS_ERROR_FACTORY_NOT_REGISTERED;
  }

  nsresult rv = entry.CreateInstance(*aIID, aResult);
  return rv;
}
}
