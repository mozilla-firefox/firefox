/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNSSComponent.h"

#include "BinaryPath.h"
#include "CryptoTask.h"
#include "EnterpriseRoots.h"
#include "ExtendedValidation.h"
#include "NSSCertDBTrustDomain.h"
#include "PKCS11ModuleDB.h"
#include "SSLTokensCache.h"
#include "ScopedNSSTypes.h"
#include "cert.h"
#include "cert_storage/src/cert_storage.h"
#include "certdb.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Atomics.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Base64.h"
#include "mozilla/Casting.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/FilePreferences.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/PublicSSL.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Unused.h"
#include "mozilla/Vector.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozilla/net/SocketProcessParent.h"
#include "mozpkix/pkixnss.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsClientAuthRemember.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsICertOverrideService.h"
#include "nsIFile.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsIPrompt.h"
#include "nsIProperties.h"
#include "nsISerialEventTarget.h"
#include "nsISiteSecurityService.h"
#include "nsITimer.h"
#include "nsITokenPasswordDialogs.h"
#include "nsIWindowWatcher.h"
#include "nsIXULRuntime.h"
#include "nsLiteralString.h"
#include "nsNSSHelper.h"
#include "nsNSSIOLayer.h"
#include "nsNetCID.h"
#include "nsPK11TokenDB.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "nss.h"
#include "p12plcy.h"
#include "pk11pub.h"
#include "prmem.h"
#include "secerr.h"
#include "secmod.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslproto.h"

#if defined(XP_LINUX) && !defined(ANDROID)
#  include <linux/magic.h>
#  include <sys/vfs.h>
#endif

using namespace mozilla;
using namespace mozilla::psm;

LazyLogModule gPIPNSSLog("pipnss");

int nsNSSComponent::mInstanceCount = 0;

// Forward declaration.
nsresult CommonInit();

template <const glean::impl::QuantityMetric* metric>
class MOZ_RAII AutoGleanTimer {
 public:
  explicit AutoGleanTimer(TimeStamp aStart = TimeStamp::Now())
      : mStart(aStart) {}

  ~AutoGleanTimer() {
    TimeStamp end = TimeStamp::Now();
    uint32_t delta = static_cast<uint32_t>((end - mStart).ToMilliseconds());
    metric->Set(delta);
  }

 private:
  const TimeStamp mStart;
};

// Take an nsIFile and get a UTF-8-encoded c-string representation of the
// location of that file (encapsulated in an nsACString).
// This operation is generally to be avoided, except when interacting with
// third-party or legacy libraries that cannot handle `nsIFile`s (such as NSS).
// |result| is encoded in UTF-8.
nsresult FileToCString(const nsCOMPtr<nsIFile>& file, nsACString& result) {
#ifdef XP_WIN
  nsAutoString path;
  nsresult rv = file->GetPath(path);
  if (NS_SUCCEEDED(rv)) {
    CopyUTF16toUTF8(path, result);
  }
  return rv;
#else
  return file->GetNativePath(result);
#endif
}

void TruncateFromLastDirectorySeparator(nsCString& path) {
  static const nsAutoCString kSeparatorString(
      mozilla::FilePreferences::kPathSeparator);
  int32_t index = path.RFind(kSeparatorString);
  if (index == kNotFound) {
    return;
  }
  path.Truncate(index);
}

bool LoadIPCClientCerts() {
  if (!LoadIPCClientCertsModule()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("failed to load ipcclientcerts"));
    return false;
  }
  return true;
}

// This function can be called from chrome or content or socket processes
// to ensure that NSS is initialized.
bool EnsureNSSInitializedChromeOrContent() {
  static Atomic<bool> initialized(false);

  if (initialized) {
    return true;
  }

  // If this is not the main thread (i.e. probably a worker) then forward this
  // call to the main thread.
  if (!NS_IsMainThread()) {
    nsCOMPtr<nsIThread> mainThread;
    nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
    if (NS_FAILED(rv)) {
      return false;
    }

    // Forward to the main thread synchronously.
    mozilla::SyncRunnable::DispatchToThread(
        mainThread,
        NS_NewRunnableFunction("EnsureNSSInitializedChromeOrContent", []() {
          EnsureNSSInitializedChromeOrContent();
        }));

    return initialized;
  }

  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsISupports> nss = do_GetService(PSM_COMPONENT_CONTRACTID);
    if (!nss) {
      return false;
    }
    initialized = true;
    return true;
  }

  if (NSS_IsInitialized()) {
    initialized = true;
    return true;
  }

  if (NSS_NoDB_Init(nullptr) != SECSuccess) {
    return false;
  }

  if (XRE_IsSocketProcess()) {
    if (NS_FAILED(CommonInit())) {
      return false;
    }
    // If ipcclientcerts fails to load, client certificate authentication won't
    // work (if networking is done on the socket process). This is preferable
    // to stopping the program entirely, so treat this as best-effort.
    Unused << NS_WARN_IF(!LoadIPCClientCerts());
    initialized = true;
    return true;
  }

  if (NS_FAILED(mozilla::psm::InitializeCipherSuite())) {
    return false;
  }

  mozilla::psm::DisableMD5();
  mozilla::pkix::RegisterErrorTable();
  initialized = true;
  return true;
}

static const uint32_t OCSP_TIMEOUT_MILLISECONDS_SOFT_MAX = 5000;
static const uint32_t OCSP_TIMEOUT_MILLISECONDS_HARD_MAX = 20000;

void nsNSSComponent::GetRevocationBehaviorFromPrefs(
    /*out*/ CertVerifier::OcspDownloadConfig* odc,
    /*out*/ CertVerifier::OcspStrictConfig* osc,
    /*out*/ uint32_t* certShortLifetimeInDays,
    /*out*/ TimeDuration& softTimeout,
    /*out*/ TimeDuration& hardTimeout) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(odc);
  MOZ_ASSERT(osc);
  MOZ_ASSERT(certShortLifetimeInDays);

  // 0 = disabled
  // 1 = enabled for everything (default)
  // 2 = enabled for EV certificates only
  uint32_t ocspLevel = StaticPrefs::security_OCSP_enabled();
  switch (ocspLevel) {
    case 0:
      *odc = CertVerifier::ocspOff;
      break;
    case 2:
      *odc = CertVerifier::ocspEVOnly;
      break;
    default:
      *odc = CertVerifier::ocspOn;
      break;
  }

  *osc = StaticPrefs::security_OCSP_require() ? CertVerifier::ocspStrict
                                              : CertVerifier::ocspRelaxed;

  *certShortLifetimeInDays =
      StaticPrefs::security_pki_cert_short_lifetime_in_days();

  uint32_t softTimeoutMillis =
      StaticPrefs::security_OCSP_timeoutMilliseconds_soft();
  softTimeoutMillis =
      std::min(softTimeoutMillis, OCSP_TIMEOUT_MILLISECONDS_SOFT_MAX);
  softTimeout = TimeDuration::FromMilliseconds(softTimeoutMillis);

  uint32_t hardTimeoutMillis =
      StaticPrefs::security_OCSP_timeoutMilliseconds_hard();
  hardTimeoutMillis =
      std::min(hardTimeoutMillis, OCSP_TIMEOUT_MILLISECONDS_HARD_MAX);
  hardTimeout = TimeDuration::FromMilliseconds(hardTimeoutMillis);
}

nsNSSComponent::nsNSSComponent()
    : mLoadableCertsLoadedMonitor("nsNSSComponent.mLoadableCertsLoadedMonitor"),
      mLoadableCertsLoaded(false),
      mLoadableCertsLoadedResult(NS_ERROR_FAILURE),
      mMutex("nsNSSComponent.mMutex"),
      mMitmDetecionEnabled(false) {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent::ctor\n"));
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mInstanceCount == 0,
             "nsNSSComponent is a singleton, but instantiated multiple times!");
  ++mInstanceCount;
}

nsNSSComponent::~nsNSSComponent() {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent::dtor\n"));
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  // All cleanup code requiring services needs to happen in xpcom_shutdown

  PrepareForShutdown();
  nsSSLIOLayerHelpers::GlobalCleanup();
  --mInstanceCount;

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent::dtor finished\n"));
}

void nsNSSComponent::UnloadEnterpriseRoots() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return;
  }
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("UnloadEnterpriseRoots"));
  MutexAutoLock lock(mMutex);
  mEnterpriseCerts.Clear();
  setValidationOptions(lock);
  ClearSSLExternalAndInternalSessionCache();
}

class BackgroundImportEnterpriseCertsTask final : public CryptoTask {
 public:
  explicit BackgroundImportEnterpriseCertsTask(nsNSSComponent* nssComponent)
      : mNSSComponent(nssComponent) {}

 private:
  virtual nsresult CalculateResult() override {
    mNSSComponent->ImportEnterpriseRoots();
    mNSSComponent->UpdateCertVerifierWithEnterpriseRoots();
    return NS_OK;
  }

  virtual void CallCallback(nsresult rv) override {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(nullptr, "psm:enterprise-certs-imported",
                                       nullptr);
    }
  }

  RefPtr<nsNSSComponent> mNSSComponent;
};

void nsNSSComponent::MaybeImportEnterpriseRoots() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return;
  }
  bool importEnterpriseRoots = StaticPrefs::security_enterprise_roots_enabled();
  if (importEnterpriseRoots) {
    RefPtr<BackgroundImportEnterpriseCertsTask> task =
        new BackgroundImportEnterpriseCertsTask(this);
    Unused << task->Dispatch();
  }
}

void nsNSSComponent::ImportEnterpriseRoots() {
  MOZ_ASSERT(!NS_IsMainThread());
  if (NS_IsMainThread()) {
    return;
  }

  nsTArray<EnterpriseCert> enterpriseCerts;
  nsresult rv = GatherEnterpriseCerts(enterpriseCerts);
  if (NS_SUCCEEDED(rv)) {
    MutexAutoLock lock(mMutex);
    mEnterpriseCerts = std::move(enterpriseCerts);
  } else {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("failed gathering enterprise roots"));
  }
}

nsresult nsNSSComponent::CommonGetEnterpriseCerts(
    nsTArray<nsTArray<uint8_t>>& enterpriseCerts, bool getRoots) {
  nsresult rv = BlockUntilLoadableCertsLoaded();
  if (NS_FAILED(rv)) {
    return rv;
  }

  enterpriseCerts.Clear();
  MutexAutoLock nsNSSComponentLock(mMutex);
  for (const auto& cert : mEnterpriseCerts) {
    nsTArray<uint8_t> certCopy;
    // mEnterpriseCerts includes both roots and intermediates.
    if (cert.GetIsRoot() == getRoots) {
      cert.CopyBytes(certCopy);
      enterpriseCerts.AppendElement(std::move(certCopy));
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNSSComponent::GetEnterpriseRoots(
    nsTArray<nsTArray<uint8_t>>& enterpriseRoots) {
  return CommonGetEnterpriseCerts(enterpriseRoots, true);
}

nsresult BytesArrayToPEM(const nsTArray<nsTArray<uint8_t>>& bytesArray,
                         nsACString& pemArray) {
  for (const auto& bytes : bytesArray) {
    nsAutoCString base64;
    nsresult rv = Base64Encode(reinterpret_cast<const char*>(bytes.Elements()),
                               bytes.Length(), base64);
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!pemArray.IsEmpty()) {
      pemArray.AppendLiteral("\n");
    }
    pemArray.AppendLiteral("-----BEGIN CERTIFICATE-----\n");
    for (size_t i = 0; i < base64.Length() / 64; i++) {
      pemArray.Append(Substring(base64, i * 64, 64));
      pemArray.AppendLiteral("\n");
    }
    if (base64.Length() % 64 != 0) {
      size_t chunks = base64.Length() / 64;
      pemArray.Append(Substring(base64, chunks * 64));
      pemArray.AppendLiteral("\n");
    }
    pemArray.AppendLiteral("-----END CERTIFICATE-----");
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNSSComponent::GetEnterpriseRootsPEM(nsACString& enterpriseRootsPEM) {
  nsTArray<nsTArray<uint8_t>> enterpriseRoots;
  nsresult rv = GetEnterpriseRoots(enterpriseRoots);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return BytesArrayToPEM(enterpriseRoots, enterpriseRootsPEM);
}

NS_IMETHODIMP
nsNSSComponent::GetEnterpriseIntermediates(
    nsTArray<nsTArray<uint8_t>>& enterpriseIntermediates) {
  return CommonGetEnterpriseCerts(enterpriseIntermediates, false);
}

NS_IMETHODIMP
nsNSSComponent::GetEnterpriseIntermediatesPEM(
    nsACString& enterpriseIntermediatesPEM) {
  nsTArray<nsTArray<uint8_t>> enterpriseIntermediates;
  nsresult rv = GetEnterpriseIntermediates(enterpriseIntermediates);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return BytesArrayToPEM(enterpriseIntermediates, enterpriseIntermediatesPEM);
}

NS_IMETHODIMP
nsNSSComponent::AddEnterpriseIntermediate(
    const nsTArray<uint8_t>& intermediateBytes) {
  nsresult rv = BlockUntilLoadableCertsLoaded();
  if (NS_FAILED(rv)) {
    return rv;
  }
  EnterpriseCert intermediate(intermediateBytes.Elements(),
                              intermediateBytes.Length(), false);
  {
    MutexAutoLock nsNSSComponentLock(mMutex);
    mEnterpriseCerts.AppendElement(std::move(intermediate));
  }

  UpdateCertVerifierWithEnterpriseRoots();
  return NS_OK;
}

class LoadLoadableCertsTask final : public Runnable {
 public:
  LoadLoadableCertsTask(nsNSSComponent* nssComponent,
                        bool importEnterpriseRoots, nsAutoCString&& greBinDir)
      : Runnable("LoadLoadableCertsTask"),
        mNSSComponent(nssComponent),
        mImportEnterpriseRoots(importEnterpriseRoots),
        mGreBinDir(std::move(greBinDir)) {
    MOZ_ASSERT(nssComponent);
  }

  ~LoadLoadableCertsTask() = default;

  nsresult Dispatch();

 private:
  NS_IMETHOD Run() override;
  nsresult LoadLoadableRoots();
  RefPtr<nsNSSComponent> mNSSComponent;
  bool mImportEnterpriseRoots;
  nsAutoCString mGreBinDir;
};

nsresult LoadLoadableCertsTask::Dispatch() {
  // The stream transport service (note: not the socket transport service) can
  // be used to perform background tasks or I/O that would otherwise block the
  // main thread.
  nsCOMPtr<nsIEventTarget> target(
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID));
  if (!target) {
    return NS_ERROR_FAILURE;
  }
  return target->Dispatch(this, NS_DISPATCH_NORMAL);
}

NS_IMETHODIMP
LoadLoadableCertsTask::Run() {
  AutoGleanTimer<&glean::networking::loading_certs_task> timer;

  nsresult loadLoadableRootsResult = LoadLoadableRoots();
  if (NS_WARN_IF(NS_FAILED(loadLoadableRootsResult))) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error, ("LoadLoadableRoots failed"));
    // We don't return loadLoadableRootsResult here because then
    // BlockUntilLoadableCertsLoaded will just wait forever. Instead we'll save
    // its value (below) so we can inform code that relies on the roots module
    // being present that loading it failed.
  }

  // Loading EV information will only succeed if we've successfully loaded the
  // loadable roots module.
  if (NS_SUCCEEDED(loadLoadableRootsResult)) {
    if (NS_FAILED(LoadExtendedValidationInfo())) {
      // This isn't a show-stopper in the same way that failing to load the
      // roots module is.
      MOZ_LOG(gPIPNSSLog, LogLevel::Error, ("failed to load EV info"));
    }
  }

  if (mImportEnterpriseRoots) {
    mNSSComponent->ImportEnterpriseRoots();
    mNSSComponent->UpdateCertVerifierWithEnterpriseRoots();
  }

  if (StaticPrefs::security_osclientcerts_autoload()) {
    bool success = LoadOSClientCertsModule();
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("loading OS client certs module %s",
             success ? "succeeded" : "failed"));
  }

  {
    MonitorAutoLock rootsLoadedLock(mNSSComponent->mLoadableCertsLoadedMonitor);
    mNSSComponent->mLoadableCertsLoaded = true;
    // Cache the result of LoadLoadableRoots so BlockUntilLoadableCertsLoaded
    // can return it to all callers later (we use that particular result because
    // if that operation fails, it's unlikely that any TLS connection will
    // succeed whereas the browser may still be able to operate if the other
    // tasks fail).
    mNSSComponent->mLoadableCertsLoadedResult = loadLoadableRootsResult;
    mNSSComponent->mLoadableCertsLoadedMonitor.NotifyAll();
  }
  return NS_OK;
}

class BackgroundLoadOSClientCertsModuleTask final : public CryptoTask {
 public:
  explicit BackgroundLoadOSClientCertsModuleTask() {}

 private:
  virtual nsresult CalculateResult() override {
    bool success = LoadOSClientCertsModule();
    return success ? NS_OK : NS_ERROR_FAILURE;
  }

  virtual void CallCallback(nsresult rv) override {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("loading OS client certs module %s",
             NS_SUCCEEDED(rv) ? "succeeded" : "failed"));
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(
          nullptr, "psm:load-os-client-certs-module-task-ran", nullptr);
    }
  }
};

void AsyncLoadOrUnloadOSClientCertsModule(bool load) {
  if (load) {
    RefPtr<BackgroundLoadOSClientCertsModuleTask> task =
        new BackgroundLoadOSClientCertsModuleTask();
    Unused << task->Dispatch();
  } else {
    UniqueSECMODModule osClientCertsModule(
        SECMOD_FindModule(kOSClientCertsModuleName.get()));
    if (osClientCertsModule) {
      SECMOD_UnloadUserModule(osClientCertsModule.get());
    }
  }
}

nsresult nsNSSComponent::BlockUntilLoadableCertsLoaded() {
  MonitorAutoLock rootsLoadedLock(mLoadableCertsLoadedMonitor);
  while (!mLoadableCertsLoaded) {
    rootsLoadedLock.Wait();
  }
  MOZ_ASSERT(mLoadableCertsLoaded);

  return mLoadableCertsLoadedResult;
}

#ifndef MOZ_NO_SMART_CARDS
static StaticMutex sCheckForSmartCardChangesMutex MOZ_UNANNOTATED;
MOZ_RUNINIT static TimeStamp sLastCheckedForSmartCardChanges = TimeStamp::Now();
#endif

nsresult nsNSSComponent::CheckForSmartCardChanges() {
#ifndef MOZ_NO_SMART_CARDS
  {
    StaticMutexAutoLock lock(sCheckForSmartCardChangesMutex);
    // Do this at most once every 3 seconds.
    TimeStamp now = TimeStamp::Now();
    if (now - sLastCheckedForSmartCardChanges <
        TimeDuration::FromSeconds(3.0)) {
      return NS_OK;
    }
    sLastCheckedForSmartCardChanges = now;
  }

  // SECMOD_UpdateSlotList attempts to acquire the list lock as well, so we
  // have to do this in three steps.
  Vector<UniqueSECMODModule> modulesWithRemovableSlots;
  {
    AutoSECMODListReadLock secmodLock;
    SECMODModuleList* list = SECMOD_GetDefaultModuleList();
    while (list) {
      if (SECMOD_LockedModuleHasRemovableSlots(list->module)) {
        UniqueSECMODModule module(SECMOD_ReferenceModule(list->module));
        if (!modulesWithRemovableSlots.append(std::move(module))) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      list = list->next;
    }
  }
  for (auto& module : modulesWithRemovableSlots) {
    // Best-effort.
    Unused << SECMOD_UpdateSlotList(module.get());
  }
  AutoSECMODListReadLock secmodLock;
  for (auto& module : modulesWithRemovableSlots) {
    for (int i = 0; i < module->slotCount; i++) {
      // We actually don't care about the return value here - we just need to
      // call this to get NSS to update its view of this slot.
      Unused << PK11_IsPresent(module->slots[i]);
    }
  }
#endif

  return NS_OK;
}

#ifdef MOZ_SYSTEM_NSS
// Returns by reference the path to the directory containing the file that has
// been loaded as MOZ_DLL_PREFIX nss3 MOZ_DLL_SUFFIX.
// |result| is encoded in UTF-8.
static nsresult GetNSS3Directory(nsCString& result) {
  MOZ_ASSERT(NS_IsMainThread());

  UniquePRString nss3Path(
      PR_GetLibraryFilePathname(MOZ_DLL_PREFIX "nss3" MOZ_DLL_SUFFIX,
                                reinterpret_cast<PRFuncPtr>(NSS_Initialize)));
  if (!nss3Path) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nss not loaded?"));
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIFile> nss3File;
  nsresult rv = NS_NewNativeLocalFile(nsDependentCString(nss3Path.get()),
                                      getter_AddRefs(nss3File));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("couldn't initialize file with path '%s'", nss3Path.get()));
    return rv;
  }
  nsCOMPtr<nsIFile> nss3Directory;
  rv = nss3File->GetParent(getter_AddRefs(nss3Directory));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("couldn't get parent directory?"));
    return rv;
  }
  return FileToCString(nss3Directory, result);
}
#endif  // MOZ_SYSTEM_NSS

// Returns by reference the path to the desired directory, based on the current
// settings in the directory service.
// |result| is encoded in UTF-8.
static nsresult GetDirectoryPath(const char* directoryKey, nsCString& result) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIProperties> directoryService(
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID));
  if (!directoryService) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("could not get directory service"));
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIFile> directory;
  nsresult rv = directoryService->Get(directoryKey, NS_GET_IID(nsIFile),
                                      getter_AddRefs(directory));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("could not get '%s' from directory service", directoryKey));
    return rv;
  }
  return FileToCString(directory, result);
}

nsresult LoadLoadableCertsTask::LoadLoadableRoots() {
  // We first start checking if the MOZ_SYSTEM_NSS is used
  // If it's not - we check if there is nssckbi library in bin directory
  // If not found again - we finally load the library from XUL
#ifdef MOZ_SYSTEM_NSS

  // First try checking the OS' default library search path.
  nsAutoCString emptyString;
  if (mozilla::psm::LoadLoadableRoots(emptyString)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("loaded CKBI from from OS default library path"));
    return NS_OK;
  }

  // Trying the library provided by NSS
  nsAutoCString nss3Dir;
  nsresult rv = GetNSS3Directory(nss3Dir);

  if (NS_SUCCEEDED(rv)) {
    if (mozilla::psm::LoadLoadableRoots(nss3Dir)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("loaded CKBI from %s", nss3Dir.get()));
      return NS_OK;
    }
  } else {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("could not determine where nss was loaded from"));
  }

#endif  // MOZ_SYSTEM_NSS

  if (mozilla::psm::LoadLoadableRoots(mGreBinDir)) {
    mozilla::glean::pkcs11::external_trust_anchor_module_loaded.Set(true);
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("loaded external CKBI from gre directory"));
    return NS_OK;
  }

  mozilla::glean::pkcs11::external_trust_anchor_module_loaded.Set(false);

  if (LoadLoadableRootsFromXul()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("loaded CKBI from xul"));
    return NS_OK;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("could not load loadable roots"));
  return NS_ERROR_FAILURE;
}

// Table of pref names and SSL cipher ID
typedef struct {
  const char* pref;
  int32_t id;
  bool (*prefGetter)();
} CipherPref;

// Update the switch statement in AccumulateCipherSuite in nsNSSCallbacks.cpp
// when you add/remove cipher suites here.
static const CipherPref sCipherPrefs[] = {
    {"security.ssl3.ecdhe_rsa_aes_128_gcm_sha256",
     TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
     StaticPrefs::security_ssl3_ecdhe_rsa_aes_128_gcm_sha256},
    {"security.ssl3.ecdhe_ecdsa_aes_128_gcm_sha256",
     TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
     StaticPrefs::security_ssl3_ecdhe_ecdsa_aes_128_gcm_sha256},
    {"security.ssl3.ecdhe_ecdsa_chacha20_poly1305_sha256",
     TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
     StaticPrefs::security_ssl3_ecdhe_ecdsa_chacha20_poly1305_sha256},
    {"security.ssl3.ecdhe_rsa_chacha20_poly1305_sha256",
     TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
     StaticPrefs::security_ssl3_ecdhe_rsa_chacha20_poly1305_sha256},
    {"security.ssl3.ecdhe_ecdsa_aes_256_gcm_sha384",
     TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
     StaticPrefs::security_ssl3_ecdhe_ecdsa_aes_256_gcm_sha384},
    {"security.ssl3.ecdhe_rsa_aes_256_gcm_sha384",
     TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
     StaticPrefs::security_ssl3_ecdhe_rsa_aes_256_gcm_sha384},
    {"security.ssl3.ecdhe_rsa_aes_128_sha", TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
     StaticPrefs::security_ssl3_ecdhe_rsa_aes_128_sha},
    {"security.ssl3.ecdhe_ecdsa_aes_128_sha",
     TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
     StaticPrefs::security_ssl3_ecdhe_ecdsa_aes_128_sha},
    {"security.ssl3.ecdhe_rsa_aes_256_sha", TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
     StaticPrefs::security_ssl3_ecdhe_rsa_aes_256_sha},
    {"security.ssl3.ecdhe_ecdsa_aes_256_sha",
     TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
     StaticPrefs::security_ssl3_ecdhe_ecdsa_aes_256_sha},
    {"security.ssl3.dhe_rsa_aes_128_sha", TLS_DHE_RSA_WITH_AES_128_CBC_SHA,
     StaticPrefs::security_ssl3_dhe_rsa_aes_128_sha},
    {"security.ssl3.dhe_rsa_aes_256_sha", TLS_DHE_RSA_WITH_AES_256_CBC_SHA,
     StaticPrefs::security_ssl3_dhe_rsa_aes_256_sha},
    {"security.tls13.aes_128_gcm_sha256", TLS_AES_128_GCM_SHA256,
     StaticPrefs::security_tls13_aes_128_gcm_sha256},
    {"security.tls13.chacha20_poly1305_sha256", TLS_CHACHA20_POLY1305_SHA256,
     StaticPrefs::security_tls13_chacha20_poly1305_sha256},
    {"security.tls13.aes_256_gcm_sha384", TLS_AES_256_GCM_SHA384,
     StaticPrefs::security_tls13_aes_256_gcm_sha384},
    {"security.ssl3.rsa_aes_128_gcm_sha256", TLS_RSA_WITH_AES_128_GCM_SHA256,
     StaticPrefs::security_ssl3_rsa_aes_128_gcm_sha256},
    {"security.ssl3.rsa_aes_256_gcm_sha384", TLS_RSA_WITH_AES_256_GCM_SHA384,
     StaticPrefs::security_ssl3_rsa_aes_256_gcm_sha384},
    {"security.ssl3.rsa_aes_128_sha", TLS_RSA_WITH_AES_128_CBC_SHA,
     StaticPrefs::security_ssl3_rsa_aes_128_sha},
    {"security.ssl3.rsa_aes_256_sha", TLS_RSA_WITH_AES_256_CBC_SHA,
     StaticPrefs::security_ssl3_rsa_aes_256_sha},
};

// These ciphersuites can only be enabled if deprecated versions of TLS are
// also enabled (via the preference "security.tls.version.enable-deprecated").
static const CipherPref sDeprecatedTLS1CipherPrefs[] = {
    {"security.ssl3.deprecated.rsa_des_ede3_sha", TLS_RSA_WITH_3DES_EDE_CBC_SHA,
     StaticPrefs::security_ssl3_deprecated_rsa_des_ede3_sha},
};

// This function will convert from pref values like 1, 2, ...
// to the internal values of SSL_LIBRARY_VERSION_TLS_1_0,
// SSL_LIBRARY_VERSION_TLS_1_1, ...
/*static*/
void nsNSSComponent::FillTLSVersionRange(SSLVersionRange& rangeOut,
                                         uint32_t minFromPrefs,
                                         uint32_t maxFromPrefs,
                                         SSLVersionRange defaults) {
  rangeOut = defaults;
  // determine what versions are supported
  SSLVersionRange supported;
  if (SSL_VersionRangeGetSupported(ssl_variant_stream, &supported) !=
      SECSuccess) {
    return;
  }

  // Clip the defaults by what NSS actually supports to enable
  // working with a system NSS with different ranges.
  rangeOut.min = std::max(rangeOut.min, supported.min);
  rangeOut.max = std::min(rangeOut.max, supported.max);

  // convert min/maxFromPrefs to the internal representation
  minFromPrefs += SSL_LIBRARY_VERSION_3_0;
  maxFromPrefs += SSL_LIBRARY_VERSION_3_0;
  // if min/maxFromPrefs are invalid, use defaults
  if (minFromPrefs > maxFromPrefs || minFromPrefs < supported.min ||
      maxFromPrefs > supported.max ||
      minFromPrefs < SSL_LIBRARY_VERSION_TLS_1_0) {
    return;
  }

  // fill out rangeOut
  rangeOut.min = (uint16_t)minFromPrefs;
  rangeOut.max = (uint16_t)maxFromPrefs;
}

static void ConfigureTLSSessionIdentifiers() {
  bool disableSessionIdentifiers =
      StaticPrefs::security_ssl_disable_session_identifiers();
  SSL_OptionSetDefault(SSL_ENABLE_SESSION_TICKETS, !disableSessionIdentifiers);
  SSL_OptionSetDefault(SSL_NO_CACHE, disableSessionIdentifiers);
}

nsresult CommonInit() {
  SSL_OptionSetDefault(SSL_ENABLE_SSL2, false);
  SSL_OptionSetDefault(SSL_V2_COMPATIBLE_HELLO, false);

  nsresult rv = nsNSSComponent::SetEnabledTLSVersions();
  if (NS_FAILED(rv)) {
    return rv;
  }

  ConfigureTLSSessionIdentifiers();

  SSL_OptionSetDefault(SSL_REQUIRE_SAFE_NEGOTIATION,
                       StaticPrefs::security_ssl_require_safe_negotiation());
  SSL_OptionSetDefault(SSL_ENABLE_RENEGOTIATION, SSL_RENEGOTIATE_REQUIRES_XTN);
  SSL_OptionSetDefault(SSL_ENABLE_EXTENDED_MASTER_SECRET, true);
  SSL_OptionSetDefault(SSL_ENABLE_HELLO_DOWNGRADE_CHECK,
                       StaticPrefs::security_tls_hello_downgrade_check());
  SSL_OptionSetDefault(SSL_ENABLE_FALSE_START,
                       StaticPrefs::security_ssl_enable_false_start());
  // SSL_ENABLE_ALPN also requires calling SSL_SetNextProtoNego in order for
  // the extensions to be negotiated.
  // WebRTC does not do that so it will not use ALPN even when this preference
  // is true.
  SSL_OptionSetDefault(SSL_ENABLE_ALPN,
                       StaticPrefs::security_ssl_enable_alpn());
  SSL_OptionSetDefault(SSL_ENABLE_0RTT_DATA,
                       StaticPrefs::security_tls_enable_0rtt_data());
  SSL_OptionSetDefault(SSL_ENABLE_POST_HANDSHAKE_AUTH,
                       StaticPrefs::security_tls_enable_post_handshake_auth());
  SSL_OptionSetDefault(
      SSL_ENABLE_DELEGATED_CREDENTIALS,
      StaticPrefs::security_tls_enable_delegated_credentials());

  rv = InitializeCipherSuite();
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error,
            ("Unable to initialize cipher suite settings\n"));
    return rv;
  }

  DisableMD5();

  mozilla::pkix::RegisterErrorTable();
  nsSSLIOLayerHelpers::GlobalInit();

  return NS_OK;
}

void PrepareForShutdownInSocketProcess() {
  MOZ_ASSERT(XRE_IsSocketProcess());
  nsSSLIOLayerHelpers::GlobalCleanup();
}

bool HandleTLSPrefChange(const nsCString& prefName) {
  // Note that the code in this function should be kept in sync with
  // gCallbackSecurityPrefs in nsIOService.cpp.
  bool prefFound = true;
  if (prefName.EqualsLiteral("security.tls.version.min") ||
      prefName.EqualsLiteral("security.tls.version.max") ||
      prefName.EqualsLiteral("security.tls.version.enable-deprecated")) {
    Unused << nsNSSComponent::SetEnabledTLSVersions();
  } else if (prefName.EqualsLiteral("security.tls.hello_downgrade_check")) {
    SSL_OptionSetDefault(SSL_ENABLE_HELLO_DOWNGRADE_CHECK,
                         StaticPrefs::security_tls_hello_downgrade_check());
  } else if (prefName.EqualsLiteral("security.ssl.require_safe_negotiation")) {
    SSL_OptionSetDefault(SSL_REQUIRE_SAFE_NEGOTIATION,
                         StaticPrefs::security_ssl_require_safe_negotiation());
  } else if (prefName.EqualsLiteral("security.ssl.enable_false_start")) {
    SSL_OptionSetDefault(SSL_ENABLE_FALSE_START,
                         StaticPrefs::security_ssl_enable_false_start());
  } else if (prefName.EqualsLiteral("security.ssl.enable_alpn")) {
    SSL_OptionSetDefault(SSL_ENABLE_ALPN,
                         StaticPrefs::security_ssl_enable_alpn());
  } else if (prefName.EqualsLiteral("security.tls.enable_0rtt_data")) {
    SSL_OptionSetDefault(SSL_ENABLE_0RTT_DATA,
                         StaticPrefs::security_tls_enable_0rtt_data());
  } else if (prefName.EqualsLiteral(
                 "security.tls.enable_post_handshake_auth")) {
    SSL_OptionSetDefault(
        SSL_ENABLE_POST_HANDSHAKE_AUTH,
        StaticPrefs::security_tls_enable_post_handshake_auth());
  } else if (prefName.EqualsLiteral(
                 "security.tls.enable_delegated_credentials")) {
    SSL_OptionSetDefault(
        SSL_ENABLE_DELEGATED_CREDENTIALS,
        StaticPrefs::security_tls_enable_delegated_credentials());
  } else if (prefName.EqualsLiteral(
                 "security.ssl.disable_session_identifiers")) {
    ConfigureTLSSessionIdentifiers();
  } else {
    prefFound = false;
  }
  return prefFound;
}

namespace {

class CipherSuiteChangeObserver : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static nsresult StartObserve();

 protected:
  virtual ~CipherSuiteChangeObserver() = default;

 private:
  static StaticRefPtr<CipherSuiteChangeObserver> sObserver;
  CipherSuiteChangeObserver() = default;
};

NS_IMPL_ISUPPORTS(CipherSuiteChangeObserver, nsIObserver)

// static
StaticRefPtr<CipherSuiteChangeObserver> CipherSuiteChangeObserver::sObserver;

// static
nsresult CipherSuiteChangeObserver::StartObserve() {
  MOZ_ASSERT(NS_IsMainThread(),
             "CipherSuiteChangeObserver::StartObserve() can only be accessed "
             "on the main thread");
  if (!sObserver) {
    RefPtr<CipherSuiteChangeObserver> observer =
        new CipherSuiteChangeObserver();
    nsresult rv = Preferences::AddStrongObserver(observer.get(), "security.");
    if (NS_FAILED(rv)) {
      sObserver = nullptr;
      return rv;
    }

    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    observerService->AddObserver(observer, NS_XPCOM_SHUTDOWN_OBSERVER_ID,
                                 false);

    sObserver = observer;
  }
  return NS_OK;
}

// Enables or disabled ciphersuites from deprecated versions of TLS as
// appropriate. If security.tls.version.enable-deprecated is true, these
// ciphersuites may be enabled, if the corresponding preference is true.
// Otherwise, these ciphersuites will be disabled.
void SetDeprecatedTLS1CipherPrefs() {
  if (StaticPrefs::security_tls_version_enable_deprecated()) {
    for (const auto& deprecatedTLS1CipherPref : sDeprecatedTLS1CipherPrefs) {
      SSL_CipherPrefSetDefault(deprecatedTLS1CipherPref.id,
                               deprecatedTLS1CipherPref.prefGetter());
    }
  } else {
    for (const auto& deprecatedTLS1CipherPref : sDeprecatedTLS1CipherPrefs) {
      SSL_CipherPrefSetDefault(deprecatedTLS1CipherPref.id, false);
    }
  }
}

// static
void SetKyberPolicy() {
  if (StaticPrefs::security_tls_enable_kyber()) {
    NSS_SetAlgorithmPolicy(SEC_OID_MLKEM768X25519, NSS_USE_ALG_IN_SSL_KX, 0);
  } else {
    NSS_SetAlgorithmPolicy(SEC_OID_MLKEM768X25519, 0, NSS_USE_ALG_IN_SSL_KX);
  }
}

nsresult CipherSuiteChangeObserver::Observe(nsISupports* /*aSubject*/,
                                            const char* aTopic,
                                            const char16_t* someData) {
  MOZ_ASSERT(NS_IsMainThread(),
             "CipherSuiteChangeObserver::Observe can only be accessed on main "
             "thread");
  if (nsCRT::strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID) == 0) {
    NS_ConvertUTF16toUTF8 prefName(someData);
    // Look through the cipher table and set according to pref setting
    for (const auto& cipherPref : sCipherPrefs) {
      if (prefName.Equals(cipherPref.pref)) {
        SSL_CipherPrefSetDefault(cipherPref.id, cipherPref.prefGetter());
        break;
      }
    }
    SetDeprecatedTLS1CipherPrefs();
    SetKyberPolicy();
    nsNSSComponent::DoClearSSLExternalAndInternalSessionCache();
  } else if (nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    Preferences::RemoveObserver(this, "security.");
    MOZ_ASSERT(sObserver.get() == this);
    sObserver = nullptr;
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }
  return NS_OK;
}

}  // namespace

void nsNSSComponent::setValidationOptions(
    const mozilla::MutexAutoLock& proofOfLock) {
  // We access prefs so this must be done on the main thread.
  mMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(!NS_IsMainThread())) {
    return;
  }

  CertVerifier::CertificateTransparencyMode ctMode =
      GetCertificateTransparencyMode();
  nsCString skipCTForHosts;
  Preferences::GetCString(
      "security.pki.certificate_transparency.disable_for_hosts",
      skipCTForHosts);
  nsAutoCString skipCTForSPKIHashesBase64;
  Preferences::GetCString(
      "security.pki.certificate_transparency.disable_for_spki_hashes",
      skipCTForSPKIHashesBase64);
  nsTArray<CopyableTArray<uint8_t>> skipCTForSPKIHashes;
  for (const auto& spkiHashBase64 : skipCTForSPKIHashesBase64.Split(',')) {
    nsAutoCString spkiHashString;
    if (NS_SUCCEEDED(Base64Decode(spkiHashBase64, spkiHashString))) {
      nsTArray<uint8_t> spkiHash(spkiHashString.Data(),
                                 spkiHashString.Length());
      skipCTForSPKIHashes.AppendElement(std::move(spkiHash));
    }
  }
  CertVerifier::CertificateTransparencyConfig ctConfig(
      ctMode, std::move(skipCTForHosts), std::move(skipCTForSPKIHashes));

  NetscapeStepUpPolicy netscapeStepUpPolicy = static_cast<NetscapeStepUpPolicy>(
      StaticPrefs::security_pki_netscape_step_up_policy());
  switch (netscapeStepUpPolicy) {
    case NetscapeStepUpPolicy::AlwaysMatch:
    case NetscapeStepUpPolicy::MatchBefore23August2016:
    case NetscapeStepUpPolicy::MatchBefore23August2015:
    case NetscapeStepUpPolicy::NeverMatch:
      break;
    default:
      netscapeStepUpPolicy = NetscapeStepUpPolicy::AlwaysMatch;
      break;
  }

  CRLiteMode defaultCRLiteMode = CRLiteMode::Disabled;
  CRLiteMode crliteMode =
      static_cast<CRLiteMode>(StaticPrefs::security_pki_crlite_mode());
  switch (crliteMode) {
    case CRLiteMode::Disabled:
    case CRLiteMode::TelemetryOnly:
    case CRLiteMode::Enforce:
    case CRLiteMode::ConfirmRevocations:
      break;
    default:
      crliteMode = defaultCRLiteMode;
      break;
  }

  CertVerifier::OcspDownloadConfig odc;
  CertVerifier::OcspStrictConfig osc;
  uint32_t certShortLifetimeInDays;
  TimeDuration softTimeout;
  TimeDuration hardTimeout;

  GetRevocationBehaviorFromPrefs(&odc, &osc, &certShortLifetimeInDays,
                                 softTimeout, hardTimeout);

  mDefaultCertVerifier = new SharedCertVerifier(
      odc, osc, softTimeout, hardTimeout, certShortLifetimeInDays,
      netscapeStepUpPolicy, std::move(ctConfig), crliteMode, mEnterpriseCerts);
}

void nsNSSComponent::UpdateCertVerifierWithEnterpriseRoots() {
  MutexAutoLock lock(mMutex);
  if (!mDefaultCertVerifier) {
    return;
  }

  RefPtr<SharedCertVerifier> oldCertVerifier = mDefaultCertVerifier;
  nsCString skipCTForHosts(oldCertVerifier->mCTConfig.mSkipForHosts);
  CertVerifier::CertificateTransparencyConfig ctConfig(
      oldCertVerifier->mCTConfig.mMode, std::move(skipCTForHosts),
      oldCertVerifier->mCTConfig.mSkipForSPKIHashes.Clone());
  mDefaultCertVerifier = new SharedCertVerifier(
      oldCertVerifier->mOCSPDownloadConfig,
      oldCertVerifier->mOCSPStrict ? CertVerifier::ocspStrict
                                   : CertVerifier::ocspRelaxed,
      oldCertVerifier->mOCSPTimeoutSoft, oldCertVerifier->mOCSPTimeoutHard,
      oldCertVerifier->mCertShortLifetimeInDays,
      oldCertVerifier->mNetscapeStepUpPolicy, std::move(ctConfig),
      oldCertVerifier->mCRLiteMode, mEnterpriseCerts);
}

// Enable the TLS versions given in the prefs, defaulting to TLS 1.0 (min) and
// TLS 1.2 (max) when the prefs aren't set or set to invalid values.
nsresult nsNSSComponent::SetEnabledTLSVersions() {
  // Keep these values in sync with all.js.
  // 1 means TLS 1.0, 2 means TLS 1.1, etc.
  static const uint32_t PSM_DEFAULT_MIN_TLS_VERSION = 3;
  static const uint32_t PSM_DEFAULT_MAX_TLS_VERSION = 4;
  static const uint32_t PSM_DEPRECATED_TLS_VERSION = 1;

  uint32_t minFromPrefs = StaticPrefs::security_tls_version_min();
  uint32_t maxFromPrefs = StaticPrefs::security_tls_version_max();

  // This override should be removed some time after
  // PSM_DEFAULT_MIN_TLS_VERSION is increased to 3.
  bool enableDeprecated = StaticPrefs::security_tls_version_enable_deprecated();
  if (enableDeprecated) {
    minFromPrefs = std::min(minFromPrefs, PSM_DEPRECATED_TLS_VERSION);
  }

  SSLVersionRange defaults = {
      SSL_LIBRARY_VERSION_3_0 + PSM_DEFAULT_MIN_TLS_VERSION,
      SSL_LIBRARY_VERSION_3_0 + PSM_DEFAULT_MAX_TLS_VERSION};
  SSLVersionRange filledInRange;
  FillTLSVersionRange(filledInRange, minFromPrefs, maxFromPrefs, defaults);

  SECStatus srv =
      SSL_VersionRangeSetDefault(ssl_variant_stream, &filledInRange);
  if (srv != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

#if defined(XP_WIN) || (defined(XP_LINUX) && !defined(ANDROID))
// If the profile directory is on a networked drive, we want to set the
// environment variable NSS_SDB_USE_CACHE to yes (as long as it hasn't been set
// before).
static void SetNSSDatabaseCacheModeAsAppropriate() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIFile> profileFile;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profileFile));
  if (NS_FAILED(rv)) {
    // We're probably running without a profile directory, so this is
    // irrelevant.
    return;
  }

  static const char sNSS_SDB_USE_CACHE[] = "NSS_SDB_USE_CACHE";
  static const char sNSS_SDB_USE_CACHE_WITH_VALUE[] = "NSS_SDB_USE_CACHE=yes";
  auto profilePath = profileFile->NativePath();

#  if defined(XP_LINUX) && !defined(ANDROID)
  struct statfs statfs_s;
  if (statfs(profilePath.get(), &statfs_s) == 0 &&
      statfs_s.f_type == NFS_SUPER_MAGIC && !PR_GetEnv(sNSS_SDB_USE_CACHE)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("profile is remote (and NSS_SDB_USE_CACHE wasn't set): "
             "setting NSS_SDB_USE_CACHE"));
    PR_SetEnv(sNSS_SDB_USE_CACHE_WITH_VALUE);
  } else {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("not setting NSS_SDB_USE_CACHE"));
  }
#  endif  // defined(XP_LINUX) && !defined(ANDROID)

#  ifdef XP_WIN
  wchar_t volPath[MAX_PATH];
  if (::GetVolumePathNameW(profilePath.get(), volPath, MAX_PATH) &&
      ::GetDriveTypeW(volPath) == DRIVE_REMOTE &&
      !PR_GetEnv(sNSS_SDB_USE_CACHE)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("profile is remote (and NSS_SDB_USE_CACHE wasn't set): "
             "setting NSS_SDB_USE_CACHE"));
    PR_SetEnv(sNSS_SDB_USE_CACHE_WITH_VALUE);
  } else {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("not setting NSS_SDB_USE_CACHE"));
  }
#  endif  // XP_WIN
}
#endif  // defined(XP_WIN) || (defined(XP_LINUX) && !defined(ANDROID))

static nsresult GetNSSProfilePath(nsAutoCString& aProfilePath) {
  aProfilePath.Truncate();
  nsCOMPtr<nsIFile> profileFile;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profileFile));
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "NSS will be initialized without a profile directory. "
        "Some things may not work as expected.");
    return NS_OK;
  }

#if defined(XP_WIN)
  // SQLite always takes UTF-8 file paths regardless of the current system
  // code page.
  nsAutoString u16ProfilePath;
  rv = profileFile->GetPath(u16ProfilePath);
  CopyUTF16toUTF8(u16ProfilePath, aProfilePath);
#else
  rv = profileFile->GetNativePath(aProfilePath);
#endif
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error,
            ("Could not get native path for profile directory.\n"));
    return rv;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("NSS profile at '%s'\n", aProfilePath.get()));
  return NS_OK;
}

#ifndef ANDROID
// Given a profile path, attempt to rename the PKCS#11 module DB to
// "pkcs11.txt.fips". In the case of a catastrophic failure (e.g. out of
// memory), returns a failing nsresult. If execution could conceivably proceed,
// returns NS_OK even if renaming the file didn't work. This simplifies the
// logic of the calling code.
// |profilePath| is encoded in UTF-8.
static nsresult AttemptToRenamePKCS11ModuleDB(const nsACString& profilePath) {
  nsCOMPtr<nsIFile> profileDir;
  // |profilePath| is encoded in UTF-8 because SQLite always takes UTF-8 file
  // paths regardless of the current system code page.
  MOZ_TRY(NS_NewUTF8LocalFile(profilePath, getter_AddRefs(profileDir)));
  const char* moduleDBFilename = "pkcs11.txt";
  nsAutoCString destModuleDBFilename(moduleDBFilename);
  destModuleDBFilename.Append(".fips");
  nsCOMPtr<nsIFile> dbFile;
  nsresult rv = profileDir->Clone(getter_AddRefs(dbFile));
  if (NS_FAILED(rv) || !dbFile) {
    return NS_ERROR_FAILURE;
  }
  rv = dbFile->AppendNative(nsAutoCString(moduleDBFilename));
  if (NS_FAILED(rv)) {
    return rv;
  }
  // If the PKCS#11 module DB doesn't exist, renaming it won't help.
  bool exists;
  rv = dbFile->Exists(&exists);
  if (NS_FAILED(rv)) {
    return rv;
  }
  // This is strange, but not a catastrophic failure.
  if (!exists) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("%s doesn't exist?", moduleDBFilename));
    return NS_OK;
  }
  nsCOMPtr<nsIFile> destDBFile;
  rv = profileDir->Clone(getter_AddRefs(destDBFile));
  if (NS_FAILED(rv) || !destDBFile) {
    return NS_ERROR_FAILURE;
  }
  rv = destDBFile->AppendNative(destModuleDBFilename);
  if (NS_FAILED(rv)) {
    return rv;
  }
  // If the destination exists, presumably we've already tried this. Doing it
  // again won't help.
  rv = destDBFile->Exists(&exists);
  if (NS_FAILED(rv)) {
    return rv;
  }
  // Unfortunate, but not a catastrophic failure.
  if (exists) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("%s already exists - not overwriting", destModuleDBFilename.get()));
    return NS_OK;
  }
  // Now do the actual move.
  // This may fail on, e.g., a read-only file system. This would be unfortunate,
  // but again it isn't catastropic and we would want to fall back to
  // initializing NSS in no-DB mode.
  Unused << dbFile->MoveToNative(profileDir, destModuleDBFilename);
  return NS_OK;
}
#endif  // ifndef ANDROID

// Given a profile directory, attempt to initialize NSS. If nocertdb is true,
// (or if we don't have a profile directory) simply initialize NSS in no DB mode
// and return. Otherwise, first attempt to initialize in read/write mode, and
// then read-only mode if that fails. If both attempts fail, we may be failing
// to initialize an NSS DB collection that has FIPS mode enabled. Attempt to
// ascertain if this is the case, and if so, rename the offending PKCS#11 module
// DB so we can (hopefully) initialize NSS in read-write mode. Again attempt
// read-only mode if that fails. Finally, fall back to no DB mode. On Android
// we can skip the FIPS workaround since it was never possible to enable FIPS
// there anyway.
// |profilePath| is encoded in UTF-8.
static nsresult InitializeNSSWithFallbacks(const nsACString& profilePath,
                                           bool nocertdb, bool safeMode) {
  if (nocertdb || profilePath.IsEmpty()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("nocertdb mode or empty profile path -> NSS_NoDB_Init"));
    SECStatus srv = NSS_NoDB_Init(nullptr);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    if (srv != SECSuccess) {
      MOZ_CRASH_UNSAFE_PRINTF("InitializeNSSWithFallbacks failed: %d",
                              PR_GetError());
    }
#endif
    return srv == SECSuccess ? NS_OK : NS_ERROR_FAILURE;
  }

  // Try read/write mode. If we're in safeMode, we won't load PKCS#11 modules.
#ifndef ANDROID
  PRErrorCode savedPRErrorCode1;
#endif  // ifndef ANDROID
  PKCS11DBConfig safeModeDBConfig =
      safeMode ? PKCS11DBConfig::DoNotLoadModules : PKCS11DBConfig::LoadModules;
  SECStatus srv = ::mozilla::psm::InitializeNSS(
      profilePath, NSSDBConfig::ReadWrite, safeModeDBConfig);
  if (srv == SECSuccess) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("initialized NSS in r/w mode"));
    return NS_OK;
  }
#ifndef ANDROID
  savedPRErrorCode1 = PR_GetError();
  PRErrorCode savedPRErrorCode2;
#endif  // ifndef ANDROID
  // That failed. Try read-only mode.
  srv = ::mozilla::psm::InitializeNSS(profilePath, NSSDBConfig::ReadOnly,
                                      safeModeDBConfig);
  if (srv == SECSuccess) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("initialized NSS in r-o mode"));
    return NS_OK;
  }
#ifndef ANDROID
  savedPRErrorCode2 = PR_GetError();

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("failed to initialize NSS with codes %d %d", savedPRErrorCode1,
           savedPRErrorCode2));
#endif  // ifndef ANDROID

#ifndef ANDROID
  // That failed as well. Maybe we're trying to load a PKCS#11 module DB that is
  // in FIPS mode, but we don't support FIPS? Test load NSS without PKCS#11
  // modules. If that succeeds, that's probably what's going on.
  if (!safeMode && (savedPRErrorCode1 == SEC_ERROR_LEGACY_DATABASE ||
                    savedPRErrorCode2 == SEC_ERROR_LEGACY_DATABASE ||
                    savedPRErrorCode1 == SEC_ERROR_PKCS11_DEVICE_ERROR ||
                    savedPRErrorCode2 == SEC_ERROR_PKCS11_DEVICE_ERROR)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("attempting no-module db init"));
    // It would make sense to initialize NSS in read-only mode here since this
    // is just a test to see if the PKCS#11 module DB being in FIPS mode is the
    // problem, but for some reason the combination of read-only and no-moddb
    // flags causes NSS initialization to fail, so unfortunately we have to use
    // read-write mode.
    srv = ::mozilla::psm::InitializeNSS(profilePath, NSSDBConfig::ReadWrite,
                                        PKCS11DBConfig::DoNotLoadModules);
    if (srv == SECSuccess) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("FIPS may be the problem"));
      // Unload NSS so we can attempt to fix this situation for the user.
      srv = NSS_Shutdown();
      if (srv != SECSuccess) {
#  ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        MOZ_CRASH_UNSAFE_PRINTF("InitializeNSSWithFallbacks failed: %d",
                                PR_GetError());
#  endif
        return NS_ERROR_FAILURE;
      }
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("trying to rename module db"));
      // If this fails non-catastrophically, we'll attempt to initialize NSS
      // again in r/w then r-o mode (both of which will fail), and then we'll
      // fall back to NSS_NoDB_Init, which is the behavior we want.
      nsresult rv = AttemptToRenamePKCS11ModuleDB(profilePath);
      if (NS_FAILED(rv)) {
#  ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        // An nsresult is a uint32_t, but at least one of our compilers doesn't
        // like this format string unless we include the cast. <shruggie emoji>
        MOZ_CRASH_UNSAFE_PRINTF("InitializeNSSWithFallbacks failed: %u",
                                (uint32_t)rv);
#  endif
        return rv;
      }
      srv = ::mozilla::psm::InitializeNSS(profilePath, NSSDBConfig::ReadWrite,
                                          PKCS11DBConfig::LoadModules);
      if (srv == SECSuccess) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("initialized in r/w mode"));
        return NS_OK;
      }
      srv = ::mozilla::psm::InitializeNSS(profilePath, NSSDBConfig::ReadOnly,
                                          PKCS11DBConfig::LoadModules);
      if (srv == SECSuccess) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("initialized in r-o mode"));
        return NS_OK;
      }
    }
  }
#endif

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("last-resort NSS_NoDB_Init"));
  srv = NSS_NoDB_Init(nullptr);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (srv != SECSuccess) {
    MOZ_CRASH_UNSAFE_PRINTF("InitializeNSSWithFallbacks failed: %d",
                            PR_GetError());
  }
#endif
  return srv == SECSuccess ? NS_OK : NS_ERROR_FAILURE;
}

#if defined(NIGHTLY_BUILD) && !defined(ANDROID)
// dbType is either "cert9.db" or "key4.db"
void UnmigrateOneCertDB(const nsCOMPtr<nsIFile>& profileDirectory,
                        const nsACString& dbType) {
  nsCOMPtr<nsIFile> dbFile;
  nsresult rv = profileDirectory->Clone(getter_AddRefs(dbFile));
  if (NS_FAILED(rv)) {
    return;
  }
  rv = dbFile->AppendNative(dbType);
  if (NS_FAILED(rv)) {
    return;
  }
  bool exists;
  rv = dbFile->Exists(&exists);
  if (NS_FAILED(rv)) {
    return;
  }
  // If the unprefixed DB already exists, don't overwrite it.
  if (exists) {
    return;
  }
  nsCOMPtr<nsIFile> prefixedDBFile;
  rv = profileDirectory->Clone(getter_AddRefs(prefixedDBFile));
  if (NS_FAILED(rv)) {
    return;
  }
  nsAutoCString prefixedDBName("gecko-no-share-");
  prefixedDBName.Append(dbType);
  rv = prefixedDBFile->AppendNative(prefixedDBName);
  if (NS_FAILED(rv)) {
    return;
  }
  Unused << prefixedDBFile->MoveToNative(nullptr, dbType);
}

void UnmigrateFromPrefixedCertDBs() {
  nsCOMPtr<nsIFile> profileDirectory;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profileDirectory));
  if (NS_FAILED(rv)) {
    return;
  }
  UnmigrateOneCertDB(profileDirectory, "cert9.db"_ns);
  UnmigrateOneCertDB(profileDirectory, "key4.db"_ns);
}
#endif  // defined(NIGHTLY_BUILD) && !defined(ANDROID)

nsresult nsNSSComponent::InitializeNSS() {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent::InitializeNSS\n"));
  AUTO_PROFILER_LABEL("nsNSSComponent::InitializeNSS", OTHER);
  AUTO_PROFILER_TRACING_MARKER("NSS", "nsNSSComponent::InitializeNSS", OTHER);

  static_assert(
      nsINSSErrorsService::NSS_SEC_ERROR_BASE == SEC_ERROR_BASE &&
          nsINSSErrorsService::NSS_SEC_ERROR_LIMIT == SEC_ERROR_LIMIT &&
          nsINSSErrorsService::NSS_SSL_ERROR_BASE == SSL_ERROR_BASE &&
          nsINSSErrorsService::NSS_SSL_ERROR_LIMIT == SSL_ERROR_LIMIT,
      "You must update the values in nsINSSErrorsService.idl");

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("NSS Initialization beginning\n"));

  nsAutoCString profileStr;
  nsresult rv = GetNSSProfilePath(profileStr);
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

#if defined(NIGHTLY_BUILD) && !defined(ANDROID)
  if (!profileStr.IsEmpty()) {
    UnmigrateFromPrefixedCertDBs();
  }
#endif

#if defined(XP_WIN) || (defined(XP_LINUX) && !defined(ANDROID))
  SetNSSDatabaseCacheModeAsAppropriate();
#endif

  bool nocertdb = StaticPrefs::security_nocertdb_AtStartup();
  bool inSafeMode = true;
  nsCOMPtr<nsIXULRuntime> runtime(do_GetService("@mozilla.org/xre/runtime;1"));
  // There might not be an nsIXULRuntime in embedded situations. This will
  // default to assuming we are in safe mode (as a result, no external PKCS11
  // modules will be loaded).
  if (runtime) {
    rv = runtime->GetInSafeMode(&inSafeMode);
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    if (NS_FAILED(rv)) {
      return rv;
    }
  }
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("inSafeMode: %u\n", inSafeMode));

  rv = InitializeNSSWithFallbacks(profileStr, nocertdb, inSafeMode);
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("failed to initialize NSS"));
    return rv;
  }

  PK11_SetPasswordFunc(PK11PasswordPrompt);

  // Register an observer so we can inform NSS when these prefs change
  Preferences::AddStrongObserver(this, "security.");

  rv = CommonInit();

  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsICertOverrideService> certOverrideService(
      do_GetService(NS_CERTOVERRIDE_CONTRACTID));
  nsCOMPtr<nsIClientAuthRememberService> clientAuthRememberService(
      do_GetService(NS_CLIENTAUTHREMEMBERSERVICE_CONTRACTID));
  nsCOMPtr<nsISiteSecurityService> siteSecurityService(
      do_GetService(NS_SSSERVICE_CONTRACTID));
  nsCOMPtr<nsICertStorage> certStorage(do_GetService(NS_CERT_STORAGE_CID));

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("NSS Initialization done\n"));

  {
    MutexAutoLock lock(mMutex);

    // ensure we have initial values for various root hashes
#ifdef DEBUG
    mTestBuiltInRootHash.Truncate();
    Preferences::GetCString("security.test.built_in_root_hash",
                            mTestBuiltInRootHash);
#endif
    mMitmCanaryIssuer.Truncate();
    Preferences::GetString("security.pki.mitm_canary_issuer",
                           mMitmCanaryIssuer);
    mMitmDetecionEnabled =
        Preferences::GetBool("security.pki.mitm_canary_issuer.enabled", true);

    // Set dynamic options from prefs.
    setValidationOptions(lock);

    bool importEnterpriseRoots =
        StaticPrefs::security_enterprise_roots_enabled();

    nsAutoCString greBinDir;
    rv = GetDirectoryPath(NS_GRE_BIN_DIR, greBinDir);
    if (NS_FAILED(rv)) {
      return rv;
    }

    RefPtr<LoadLoadableCertsTask> loadLoadableCertsTask(
        new LoadLoadableCertsTask(this, importEnterpriseRoots,
                                  std::move(greBinDir)));
    rv = loadLoadableCertsTask->Dispatch();
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    if (NS_FAILED(rv)) {
      return rv;
    }

    return NS_OK;
  }
}

void nsNSSComponent::PrepareForShutdown() {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent::PrepareForShutdown"));
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  PK11_SetPasswordFunc((PK11PasswordFunc) nullptr);

  Preferences::RemoveObserver(this, "security.");

  // Release the default CertVerifier. This will cause any held NSS resources
  // to be released.
  MutexAutoLock lock(mMutex);
  mDefaultCertVerifier = nullptr;
  // We don't actually shut down NSS - XPCOM does, after all threads have been
  // joined and the component manager has been shut down (and so there shouldn't
  // be any XPCOM objects holding NSS resources).
}

nsresult nsNSSComponent::Init() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  MOZ_ASSERT(XRE_IsParentProcess());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  AutoGleanTimer<&glean::networking::nss_initialization> timer;

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("Beginning NSS initialization\n"));

  nsresult rv = InitializeNSS();
  if (NS_FAILED(rv)) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Error,
            ("nsNSSComponent::InitializeNSS() failed\n"));
    return rv;
  }

  rv = RegisterObservers();
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

// nsISupports Implementation for the class
NS_IMPL_ISUPPORTS(nsNSSComponent, nsINSSComponent, nsIObserver)

static const char* const PROFILE_BEFORE_CHANGE_TOPIC = "profile-before-change";

NS_IMETHODIMP
nsNSSComponent::Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* someData) {
  // In some tests, we don't receive a "profile-before-change" topic. However,
  // we still have to shut down before the storage service shuts down, because
  // closing the sql-backed softoken requires sqlite still be available. Thus,
  // we observe "xpcom-shutdown" just in case.
  if (nsCRT::strcmp(aTopic, PROFILE_BEFORE_CHANGE_TOPIC) == 0 ||
      nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("receiving profile change or XPCOM shutdown notification"));
    PrepareForShutdown();
  } else if (nsCRT::strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID) == 0) {
    bool clearSessionCache = true;
    NS_ConvertUTF16toUTF8 prefName(someData);

    if (HandleTLSPrefChange(prefName)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("HandleTLSPrefChange done"));
    } else if (
        prefName.EqualsLiteral("security.OCSP.enabled") ||
        prefName.EqualsLiteral("security.OCSP.require") ||
        prefName.EqualsLiteral("security.pki.cert_short_lifetime_in_days") ||
        prefName.EqualsLiteral("security.ssl.enable_ocsp_stapling") ||
        prefName.EqualsLiteral("security.ssl.enable_ocsp_must_staple") ||
        prefName.EqualsLiteral("security.pki.certificate_transparency.mode") ||
        prefName.EqualsLiteral(
            "security.pki.certificate_transparency.disable_for_hosts") ||
        prefName.EqualsLiteral(
            "security.pki.certificate_transparency.disable_for_spki_hashes") ||
        prefName.EqualsLiteral("security.pki.netscape_step_up_policy") ||
        prefName.EqualsLiteral("security.OCSP.timeoutMilliseconds.soft") ||
        prefName.EqualsLiteral("security.OCSP.timeoutMilliseconds.hard") ||
        prefName.EqualsLiteral("security.pki.crlite_mode")) {
      MutexAutoLock lock(mMutex);
      setValidationOptions(lock);
#ifdef DEBUG
    } else if (prefName.EqualsLiteral("security.test.built_in_root_hash")) {
      MutexAutoLock lock(mMutex);
      mTestBuiltInRootHash.Truncate();
      Preferences::GetCString("security.test.built_in_root_hash",
                              mTestBuiltInRootHash);
#endif  // DEBUG
    } else if (prefName.Equals("security.enterprise_roots.enabled")) {
      UnloadEnterpriseRoots();
      MaybeImportEnterpriseRoots();
    } else if (prefName.Equals("security.osclientcerts.autoload")) {
      bool loadOSClientCertsModule =
          StaticPrefs::security_osclientcerts_autoload();
      AsyncLoadOrUnloadOSClientCertsModule(loadOSClientCertsModule);
    } else if (prefName.EqualsLiteral("security.pki.mitm_canary_issuer")) {
      MutexAutoLock lock(mMutex);
      mMitmCanaryIssuer.Truncate();
      Preferences::GetString("security.pki.mitm_canary_issuer",
                             mMitmCanaryIssuer);
    } else if (prefName.EqualsLiteral(
                   "security.pki.mitm_canary_issuer.enabled")) {
      MutexAutoLock lock(mMutex);
      mMitmDetecionEnabled =
          Preferences::GetBool("security.pki.mitm_canary_issuer.enabled", true);
    } else {
      clearSessionCache = false;
    }
    if (clearSessionCache) {
      ClearSSLExternalAndInternalSessionCache();
    }
  } else if (!nsCRT::strcmp(aTopic, "last-pb-context-exited")) {
    nsNSSComponent::DoClearSSLExternalAndInternalSessionCache();
  }

  return NS_OK;
}

/*static*/
nsresult nsNSSComponent::GetNewPrompter(nsIPrompt** result) {
  NS_ENSURE_ARG_POINTER(result);
  *result = nullptr;

  if (!NS_IsMainThread()) {
    NS_ERROR("nsSDRContext::GetNewPrompter called off the main thread");
    return NS_ERROR_NOT_SAME_THREAD;
  }

  nsresult rv;
  nsCOMPtr<nsIWindowWatcher> wwatch(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = wwatch->GetNewPrompter(0, result);
  NS_ENSURE_SUCCESS(rv, rv);

  return rv;
}

nsresult nsNSSComponent::LogoutAuthenticatedPK11() {
  ClearSSLExternalAndInternalSessionCache();

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->NotifyObservers(nullptr, "net:cancel-all-connections", nullptr);
  }

  return NS_OK;
}

nsresult nsNSSComponent::RegisterObservers() {
  nsCOMPtr<nsIObserverService> observerService(
      do_GetService("@mozilla.org/observer-service;1"));
  if (!observerService) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("nsNSSComponent: couldn't get observer service\n"));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("nsNSSComponent: adding observers\n"));
  // Using false for the ownsweak parameter means the observer service will
  // keep a strong reference to this component. As a result, this will live at
  // least as long as the observer service.
  observerService->AddObserver(this, PROFILE_BEFORE_CHANGE_TOPIC, false);
  observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  observerService->AddObserver(this, "last-pb-context-exited", false);

  return NS_OK;
}

nsresult DoesCertMatchFingerprint(const nsTArray<uint8_t>& cert,
                                  const nsCString& fingerprint, bool& result) {
  result = false;

  if (cert.Length() > std::numeric_limits<uint32_t>::max()) {
    return NS_ERROR_INVALID_ARG;
  }
  nsTArray<uint8_t> digestArray;
  nsresult rv = Digest::DigestBuf(SEC_OID_SHA256, cert.Elements(),
                                  cert.Length(), digestArray);
  if (NS_FAILED(rv)) {
    return rv;
  }
  SECItem digestItem = {siBuffer, digestArray.Elements(),
                        static_cast<unsigned int>(digestArray.Length())};
  UniquePORTString certFingerprint(
      CERT_Hexify(&digestItem, true /* use colon delimiters */));
  if (!certFingerprint) {
    return NS_ERROR_FAILURE;
  }

  result = fingerprint.Equals(certFingerprint.get());
  return NS_OK;
}

NS_IMETHODIMP
nsNSSComponent::IsCertTestBuiltInRoot(const nsTArray<uint8_t>& cert,
                                      bool* result) {
  NS_ENSURE_ARG_POINTER(result);
  *result = false;

#ifdef DEBUG
  MutexAutoLock lock(mMutex);
  nsresult rv = DoesCertMatchFingerprint(cert, mTestBuiltInRootHash, *result);
  if (NS_FAILED(rv)) {
    return rv;
  }
#endif  // DEBUG

  return NS_OK;
}

NS_IMETHODIMP
nsNSSComponent::IssuerMatchesMitmCanary(const char* aCertIssuer) {
  MutexAutoLock lock(mMutex);
  if (mMitmDetecionEnabled && !mMitmCanaryIssuer.IsEmpty()) {
    nsString certIssuer = NS_ConvertUTF8toUTF16(aCertIssuer);
    if (mMitmCanaryIssuer.Equals(certIssuer)) {
      return NS_OK;
    }
  }

  return NS_ERROR_FAILURE;
}

SharedCertVerifier::~SharedCertVerifier() = default;

NS_IMETHODIMP
nsNSSComponent::GetDefaultCertVerifier(SharedCertVerifier** result) {
  MutexAutoLock lock(mMutex);
  NS_ENSURE_ARG_POINTER(result);
  RefPtr<SharedCertVerifier> certVerifier(mDefaultCertVerifier);
  certVerifier.forget(result);
  return NS_OK;
}

// static
void nsNSSComponent::DoClearSSLExternalAndInternalSessionCache() {
  SSL_ClearSessionCache();
  mozilla::net::SSLTokensCache::Clear();
}

NS_IMETHODIMP
nsNSSComponent::ClearSSLExternalAndInternalSessionCache() {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (mozilla::net::nsIOService::UseSocketProcess()) {
    if (mozilla::net::gIOService) {
      mozilla::net::gIOService->CallOrWaitForSocketProcess([]() {
        RefPtr<mozilla::net::SocketProcessParent> socketParent =
            mozilla::net::SocketProcessParent::GetSingleton();
        Unused << socketParent->SendClearSessionCache();
      });
    }
  }
  DoClearSSLExternalAndInternalSessionCache();
  return NS_OK;
}

NS_IMETHODIMP
nsNSSComponent::AsyncClearSSLExternalAndInternalSessionCache(
    JSContext* aCx, ::mozilla::dom::Promise** aPromise) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<mozilla::dom::Promise> promise =
      mozilla::dom::Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  if (mozilla::net::nsIOService::UseSocketProcess() &&
      mozilla::net::gIOService) {
    mozilla::net::gIOService->CallOrWaitForSocketProcess([p = RefPtr{
                                                              promise}]() {
      RefPtr<mozilla::net::SocketProcessParent> socketParent =
          mozilla::net::SocketProcessParent::GetSingleton();
      Unused << socketParent->SendClearSessionCache()->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise = RefPtr{p}] { promise->MaybeResolveWithUndefined(); },
          [promise = RefPtr{p}] { promise->MaybeReject(NS_ERROR_UNEXPECTED); });
    });
  } else {
    promise->MaybeResolveWithUndefined();
  }
  DoClearSSLExternalAndInternalSessionCache();
  promise.forget(aPromise);
  return NS_OK;
}

Atomic<bool> sSearchingForClientAuthCertificates{false};

extern "C" {
// Returns true once if gecko is searching for client authentication
// certificates (i.e., if some thread has an
// AutoSearchingForClientAuthCertificates on the stack).
// The idea is when gecko instantiates an
// AutoSearchingForClientAuthCertificates, sSearchingForClientAuthCertificates
// will get set to true. Some thread running some NSS code will result in a
// call to IsGeckoSearchingForClientAuthCertificates(), which will essentially
// claim the search by swapping the value for false. The search will happen,
// but for the rest of the lifetime of the
// AutoSearchingForClientAuthCertificates, this function will return false,
// meaning no other threads will also cause searches to happen.
bool IsGeckoSearchingForClientAuthCertificates() {
  return sSearchingForClientAuthCertificates.exchange(false);
}
}

AutoSearchingForClientAuthCertificates::
    AutoSearchingForClientAuthCertificates() {
  sSearchingForClientAuthCertificates = true;
}

AutoSearchingForClientAuthCertificates::
    ~AutoSearchingForClientAuthCertificates() {
  sSearchingForClientAuthCertificates = false;
}

namespace mozilla {
namespace psm {

already_AddRefed<SharedCertVerifier> GetDefaultCertVerifier() {
  static NS_DEFINE_CID(kNSSComponentCID, NS_NSSCOMPONENT_CID);

  nsCOMPtr<nsINSSComponent> nssComponent(do_GetService(kNSSComponentCID));
  if (!nssComponent) {
    return nullptr;
  }
  nsresult rv = nssComponent->BlockUntilLoadableCertsLoaded();
  if (NS_FAILED(rv)) {
    return nullptr;
  }
  RefPtr<SharedCertVerifier> result;
  rv = nssComponent->GetDefaultCertVerifier(getter_AddRefs(result));
  if (NS_FAILED(rv)) {
    return nullptr;
  }
  return result.forget();
}

// Helper for FindClientCertificatesWithPrivateKeys. Copies all
// CERTCertificates from `from` to `to`.
static inline void CopyCertificatesTo(UniqueCERTCertList& from,
                                      UniqueCERTCertList& to) {
  MOZ_ASSERT(from);
  MOZ_ASSERT(to);
  for (CERTCertListNode* n = CERT_LIST_HEAD(from.get());
       !CERT_LIST_END(n, from.get()); n = CERT_LIST_NEXT(n)) {
    UniqueCERTCertificate cert(CERT_DupCertificate(n->cert));
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("      provisionally adding '%s'", n->cert->subjectName));
    if (CERT_AddCertToListTail(to.get(), cert.get()) == SECSuccess) {
      Unused << cert.release();
    }
  }
}

// Lists all private keys on all modules and returns a list of any corresponding
// client certificates. Returns an empty list if no such certificates can be
// found. Returns null if an error is encountered.
UniqueCERTCertList FindClientCertificatesWithPrivateKeys() {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("FindClientCertificatesWithPrivateKeys"));
  AutoSearchingForClientAuthCertificates _;

  (void)BlockUntilLoadableCertsLoaded();
  (void)CheckForSmartCardChanges();

  UniqueCERTCertList certsWithPrivateKeys(CERT_NewCertList());
  if (!certsWithPrivateKeys) {
    return nullptr;
  }

  UniquePK11SlotInfo internalSlot(PK11_GetInternalKeySlot());

  AutoSECMODListReadLock secmodLock;
  SECMODModuleList* list = SECMOD_GetDefaultModuleList();
  while (list) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("  module '%s'", list->module->commonName));
    for (int i = 0; i < list->module->slotCount; i++) {
      PK11SlotInfo* slot = list->module->slots[i];
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("    slot '%s'", PK11_GetSlotName(slot)));
      // If this is the internal certificate/key slot or the slot on the
      // builtin roots module, there may be many more certificates than private
      // keys, so search by private keys (PK11_HasRootCerts will be true if the
      // slot contains an object with the vendor-specific CK_CLASS
      // CKO_NSS_BUILTIN_ROOT_LIST, which should only be the case for the NSS
      // builtin roots module).
      if (internalSlot.get() == slot || PK11_HasRootCerts(slot)) {
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                ("    (looking at internal/builtin slot)"));
        if (PK11_Authenticate(slot, true, nullptr) != SECSuccess) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("    (couldn't authenticate)"));
          continue;
        }
        UniqueSECKEYPrivateKeyList privateKeys(
            PK11_ListPrivKeysInSlot(slot, nullptr, nullptr));
        if (!privateKeys) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("      (no private keys)"));
          continue;
        }
        for (SECKEYPrivateKeyListNode* node = PRIVKEY_LIST_HEAD(privateKeys);
             !PRIVKEY_LIST_END(node, privateKeys);
             node = PRIVKEY_LIST_NEXT(node)) {
          UniqueCERTCertList certs(PK11_GetCertsMatchingPrivateKey(node->key));
          if (!certs) {
            MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                    ("      PK11_GetCertsMatchingPrivateKey encountered an "
                     "error "));
            continue;
          }
          if (CERT_LIST_EMPTY(certs)) {
            MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("      (no certs for key)"));
            continue;
          }
          CopyCertificatesTo(certs, certsWithPrivateKeys);
        }
      } else {
        // ... otherwise, optimistically assume that searching by certificate
        // won't take too much time. Since "friendly" slots expose certificates
        // without needing to be authenticated to, this results in fewer PIN
        // dialogs shown to the user.
        MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                ("    (looking at non-internal slot)"));

        if (!PK11_IsPresent(slot)) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("    (not present)"));
          continue;
        }
        // If this isn't a "friendly" slot, authenticate to expose certificates.
        if (!PK11_IsFriendly(slot) &&
            PK11_Authenticate(slot, true, nullptr) != SECSuccess) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("    (couldn't authenticate)"));
          continue;
        }
        UniqueCERTCertList certsInSlot(PK11_ListCertsInSlot(slot));
        if (!certsInSlot) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                  ("      (couldn't list certs in slot)"));
          continue;
        }
        // When NSS decodes a certificate, if that certificate has a
        // corresponding private key (or public key, if the slot it's on hasn't
        // been logged into), it notes it as a "user cert".
        if (CERT_FilterCertListForUserCerts(certsInSlot.get()) != SECSuccess) {
          MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
                  ("      (couldn't filter certs)"));
          continue;
        }
        CopyCertificatesTo(certsInSlot, certsWithPrivateKeys);
      }
    }
    list = list->next;
  }

  if (CERT_FilterCertListByUsage(certsWithPrivateKeys.get(), certUsageSSLClient,
                                 false) != SECSuccess) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("  CERT_FilterCertListByUsage encountered an error - returning"));
    return nullptr;
  }

  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gPIPNSSLog, LogLevel::Debug))) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("  returning:"));
    for (CERTCertListNode* n = CERT_LIST_HEAD(certsWithPrivateKeys);
         !CERT_LIST_END(n, certsWithPrivateKeys); n = CERT_LIST_NEXT(n)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug, ("    %s", n->cert->subjectName));
    }
  }

  return certsWithPrivateKeys;
}

CertVerifier::CertificateTransparencyMode GetCertificateTransparencyMode() {
  const CertVerifier::CertificateTransparencyMode defaultCTMode =
      CertVerifier::CertificateTransparencyMode::TelemetryOnly;
  CertVerifier::CertificateTransparencyMode ctMode =
      static_cast<CertVerifier::CertificateTransparencyMode>(
          StaticPrefs::security_pki_certificate_transparency_mode());
  switch (ctMode) {
    case CertVerifier::CertificateTransparencyMode::Disabled:
    case CertVerifier::CertificateTransparencyMode::TelemetryOnly:
    case CertVerifier::CertificateTransparencyMode::Enforce:
      break;
    default:
      ctMode = defaultCTMode;
      break;
  }
  return ctMode;
}

}  // namespace psm
}  // namespace mozilla

NS_IMPL_ISUPPORTS(PipUIContext, nsIInterfaceRequestor)

PipUIContext::PipUIContext() = default;

PipUIContext::~PipUIContext() = default;

NS_IMETHODIMP
PipUIContext::GetInterface(const nsIID& uuid, void** result) {
  NS_ENSURE_ARG_POINTER(result);
  *result = nullptr;

  if (!NS_IsMainThread()) {
    NS_ERROR("PipUIContext::GetInterface called off the main thread");
    return NS_ERROR_NOT_SAME_THREAD;
  }

  if (!uuid.Equals(NS_GET_IID(nsIPrompt))) return NS_ERROR_NO_INTERFACE;

  nsIPrompt* prompt = nullptr;
  nsresult rv = nsNSSComponent::GetNewPrompter(&prompt);
  *result = prompt;
  return rv;
}

nsresult getNSSDialogs(void** _result, REFNSIID aIID, const char* contract) {
  if (!NS_IsMainThread()) {
    NS_ERROR("getNSSDialogs called off the main thread");
    return NS_ERROR_NOT_SAME_THREAD;
  }

  nsresult rv;

  nsCOMPtr<nsISupports> svc = do_GetService(contract, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = svc->QueryInterface(aIID, _result);

  return rv;
}

nsresult setPassword(PK11SlotInfo* slot, nsIInterfaceRequestor* ctx) {
  MOZ_ASSERT(slot);
  MOZ_ASSERT(ctx);
  NS_ENSURE_ARG_POINTER(slot);
  NS_ENSURE_ARG_POINTER(ctx);

  if (PK11_NeedUserInit(slot)) {
    nsCOMPtr<nsITokenPasswordDialogs> dialogs;
    nsresult rv = getNSSDialogs(getter_AddRefs(dialogs),
                                NS_GET_IID(nsITokenPasswordDialogs),
                                NS_TOKENPASSWORDSDIALOG_CONTRACTID);
    if (NS_FAILED(rv)) {
      return rv;
    }

    bool canceled;
    nsCOMPtr<nsIPK11Token> token = new nsPK11Token(slot);
    rv = dialogs->SetPassword(ctx, token, &canceled);
    if (NS_FAILED(rv)) {
      return rv;
    }

    if (canceled) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  return NS_OK;
}

static PRBool ConvertBetweenUCS2andASCII(PRBool toUnicode, unsigned char* inBuf,
                                         unsigned int inBufLen,
                                         unsigned char* outBuf,
                                         unsigned int maxOutBufLen,
                                         unsigned int* outBufLen,
                                         PRBool swapBytes) {
  std::unique_ptr<unsigned char[]> inBufDup(new unsigned char[inBufLen]);
  if (!inBufDup) {
    return PR_FALSE;
  }
  std::memcpy(inBufDup.get(), inBuf, inBufLen * sizeof(unsigned char));

  // If converting Unicode to ASCII, swap bytes before conversion as neccessary.
  if (!toUnicode && swapBytes) {
    if (inBufLen % 2 != 0) {
      return PR_FALSE;
    }
    mozilla::NativeEndian::swapFromLittleEndianInPlace(
        reinterpret_cast<char16_t*>(inBufDup.get()), inBufLen / 2);
  }
  return PORT_UCS2_UTF8Conversion(toUnicode, inBufDup.get(), inBufLen, outBuf,
                                  maxOutBufLen, outBufLen);
}

namespace mozilla {
namespace psm {

nsresult InitializeCipherSuite() {
  MOZ_ASSERT(NS_IsMainThread(),
             "InitializeCipherSuite() can only be accessed on the main thread");

  if (NSS_SetDomesticPolicy() != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  // Disable any ciphers that NSS might have enabled by default
  for (uint16_t i = 0; i < SSL_NumImplementedCiphers; ++i) {
    uint16_t cipher_id = SSL_ImplementedCiphers[i];
    SSL_CipherPrefSetDefault(cipher_id, false);
  }

  // Now only set SSL/TLS ciphers we knew about at compile time
  for (const auto& cipherPref : sCipherPrefs) {
    SSL_CipherPrefSetDefault(cipherPref.id, cipherPref.prefGetter());
  }

  SetDeprecatedTLS1CipherPrefs();

  // Enable ciphers for PKCS#12
  SEC_PKCS12EnableCipher(PKCS12_RC4_40, 1);
  SEC_PKCS12EnableCipher(PKCS12_RC4_128, 1);
  SEC_PKCS12EnableCipher(PKCS12_RC2_CBC_40, 1);
  SEC_PKCS12EnableCipher(PKCS12_RC2_CBC_128, 1);
  SEC_PKCS12EnableCipher(PKCS12_DES_56, 1);
  SEC_PKCS12EnableCipher(PKCS12_DES_EDE3_168, 1);
  SEC_PKCS12EnableCipher(PKCS12_AES_CBC_128, 1);
  SEC_PKCS12EnableCipher(PKCS12_AES_CBC_192, 1);
  SEC_PKCS12EnableCipher(PKCS12_AES_CBC_256, 1);
  SEC_PKCS12SetPreferredCipher(PKCS12_DES_EDE3_168, 1);
  PORT_SetUCS2_ASCIIConversionFunction(ConvertBetweenUCS2andASCII);

  // PSM enforces a minimum RSA key size of 1024 bits, which is overridable.
  // NSS has its own minimum, which is not overridable (the default is 1023
  // bits). This sets the NSS minimum to 512 bits so users can still connect to
  // devices like wifi routers with woefully small keys (they would have to add
  // an override to do so, but they already do for such devices).
  NSS_OptionSet(NSS_RSA_MIN_KEY_SIZE, 512);

  SetKyberPolicy();

  // Observe preference change around cipher suite setting.
  return CipherSuiteChangeObserver::StartObserve();
}

}  // namespace psm
}  // namespace mozilla
