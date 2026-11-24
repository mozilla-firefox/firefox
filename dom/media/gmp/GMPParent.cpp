/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPParent.h"

#include "CDMStorageIdProvider.h"
#include "ChromiumCDMAdapter.h"
#include "GMPContentParent.h"
#include "GMPLog.h"
#include "GMPTimerParent.h"
#include "GeckoProfiler.h"
#include "MediaResult.h"
#include "mozIGeckoMediaPluginService.h"
#include "mozilla/Casting.h"
#include "mozilla/FOGIPC.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/ProcInfoParent.h"
#endif
#include "mozilla/dom/KeySystemNames.h"
#include "mozilla/dom/WidevineCDMManifestBinding.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxInfo.h"
#  include "mozilla/ipc/SharedMemoryHandle.h"
#endif
#include "ProfilerParent.h"
#include "mozilla/SSE.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/Telemetry.h"
#include "mozilla/glean/IpcMetrics.h"
#include "nsComponentManagerUtils.h"
#include "nsIObserverService.h"
#include "nsIRunnable.h"
#include "nsIWritablePropertyBag2.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "runnable_utils.h"
#ifdef XP_WIN
#  include "PDMFactory.h"
#  include "WMFDecoderModule.h"
#  include "mozilla/FileUtilsWin.h"
#  include "mozilla/WinDllServices.h"
#endif
#if defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/java/GeckoProcessManagerWrappers.h"
#  include "mozilla/java/GeckoProcessTypeWrappers.h"
#endif  // defined(MOZ_WIDGET_ANDROID)
#if defined(XP_MACOSX)
#  include "base/process_util.h"
#  include "nsMacUtilsImpl.h"
#endif  // defined(XP_MACOSX)

using mozilla::ipc::GeckoChildProcessHost;

using CrashReporter::AnnotationTable;

namespace mozilla::gmp {

#define GMP_PARENT_LOG_DEBUG(x, ...)                                       \
  GMP_LOG_DEBUG("GMPParent[{}|childPid={}] " x, fmt::ptr(this), mChildPid, \
                ##__VA_ARGS__)

#ifdef __CLASS__
#  undef __CLASS__
#endif
#define __CLASS__ "GMPParent"

GMPParent::GMPParent(nsISerialEventTarget* aGMPEventTarget)
    : mState(GMPState::NotLoaded),
      mPluginId(GeckoChildProcessHost::GetUniqueID()),
      mDeleteProcessOnlyOnUnload(false),
      mAbnormalShutdownInProgress(false),
      mIsBlockingDeletion(false),
      mCanDecrypt(false),
      mGMPContentChildCount(0),
      mChildPid(0),
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
      mChildLaunchArch(base::PROCESS_ARCH_INVALID),
#endif
      mMainThread(GetMainThreadSerialEventTarget()),
      mGMPEventTarget(aGMPEventTarget) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("GMPParent ctor id={}", mPluginId);
}

GMPParent::~GMPParent() {
  // This method is not restricted to a specific thread.
  GMP_PARENT_LOG_DEBUG("GMPParent dtor id={}", mPluginId);
  MOZ_ASSERT(!mProcess);
}

void GMPParent::CloneFrom(const GMPParent* aOther) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  MOZ_ASSERT(aOther->mService);

  mService = aOther->mService;
  mDirectory = aOther->mDirectory;
  mName = aOther->mName;
  mVersion = aOther->mVersion;
  mDescription = aOther->mDescription;
  mDisplayName = aOther->mDisplayName;
  mPluginType = aOther->mPluginType;
#if defined(XP_WIN) || defined(XP_LINUX)
  mLibs = aOther->mLibs;
#endif
  for (const GMPCapability& cap : aOther->mCapabilities) {
    mCapabilities.AppendElement(cap);
  }
  mAdapter = aOther->mAdapter;

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  mChildLaunchArch = aOther->mChildLaunchArch;
#endif
}

#if defined(XP_WIN) || defined(XP_MACOSX)
nsresult GMPParent::GetPluginFileArch(nsIFile* aPluginDir,
                                      const nsString& aBaseName,
                                      uint32_t& aArchSet) {
  // Build up the plugin filename
#  if defined(XP_MACOSX)
  nsAutoString pluginFileName = u"lib"_ns + aBaseName + u".dylib"_ns;
#  elif defined(XP_WIN)
  nsAutoString pluginFileName = aBaseName + u".dll"_ns;
#  endif
  GMP_PARENT_LOG_DEBUG("{}: pluginFileName: {}", __FUNCTION__,
                       NS_LossyConvertUTF16toASCII(pluginFileName).get());

  // Create an nsIFile representing the plugin
  nsCOMPtr<nsIFile> pluginFile;
  nsresult rv = aPluginDir->Clone(getter_AddRefs(pluginFile));
  NS_ENSURE_SUCCESS(rv, rv);
  pluginFile->AppendRelativePath(pluginFileName);

#  if defined(XP_MACOSX)
  // Get the full plugin path
  nsAutoCString pluginPath;
  rv = pluginFile->GetNativePath(pluginPath);
  NS_ENSURE_SUCCESS(rv, rv);
  GMP_PARENT_LOG_DEBUG("{}: pluginPath: {}", __FUNCTION__, pluginPath.get());

  rv = nsMacUtilsImpl::GetArchitecturesForBinary(pluginPath.get(), &aArchSet);
  NS_ENSURE_SUCCESS(rv, rv);

#    if defined(__aarch64__)
  mPluginFilePath = pluginPath;
#    endif
#  elif defined(XP_WIN)
  // Get the full plugin path
  nsAutoString pluginPath;
  rv = pluginFile->GetTarget(pluginPath);
  NS_ENSURE_SUCCESS(rv, rv);
  GMP_PARENT_LOG_DEBUG("{}: pluginPath: {}", __FUNCTION__,
                       NS_LossyConvertUTF16toASCII(pluginPath).get());

  aArchSet = GetExecutableArchitecture(pluginPath.get());
  if (aArchSet == base::PROCESS_ARCH_INVALID) {
    return NS_ERROR_FAILURE;
  }
#  endif

  return NS_OK;
}
#endif  // defined(XP_WIN) || defined(XP_MACOSX)

#ifdef MOZ_WIDGET_ANDROID
void GMPParent::InitForClearkey(GeckoMediaPluginServiceParent* aService) {
  MOZ_ASSERT(aService);
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());

  mService = aService;
  mName = u"clearkey"_ns;
  mDisplayName = "clearkey"_ns;
  mVersion = "0.1"_ns;
  mDescription = "ClearKey Gecko Media Plugin"_ns;
  mPluginType = GMPPluginType::Clearkey;
  mAdapter = u"chromium"_ns;

  mCapabilities.SetCapacity(1);
  auto& video = *mCapabilities.AppendElement();
  video.mAPIName = nsLiteralCString(CHROMIUM_CDM_API);
  video.mAPITags.SetCapacity(2);
  video.mAPITags.AppendElement(nsCString{kClearKeyKeySystemName});
  video.mAPITags.AppendElement(
      nsCString{kClearKeyWithProtectionQueryKeySystemName});
}
#endif

RefPtr<GenericPromise> GMPParent::Init(GeckoMediaPluginServiceParent* aService,
                                       nsIFile* aPluginDir) {
  MOZ_ASSERT(aPluginDir);
  MOZ_ASSERT(aService);
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());

  mService = aService;
  mDirectory = aPluginDir;

  // aPluginDir is <profile-dir>/<gmp-plugin-id>/<version>
  // where <gmp-plugin-id> should be gmp-gmpopenh264
  nsCOMPtr<nsIFile> parent;
  nsresult rv = aPluginDir->GetParent(getter_AddRefs(parent));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }
  nsAutoString parentLeafName;
  rv = parent->GetLeafName(parentLeafName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }
  GMP_PARENT_LOG_DEBUG("{}: for {}", __FUNCTION__,
                       NS_LossyConvertUTF16toASCII(parentLeafName).get());

  MOZ_ASSERT(parentLeafName.Length() > 4);
  mName = Substring(parentLeafName, 4);

#if defined(XP_WIN) || defined(XP_MACOSX)
  uint32_t pluginArch = base::PROCESS_ARCH_INVALID;
  rv = GetPluginFileArch(
      aPluginDir,
#  ifdef MOZ_WMF_CDM
      mName.Equals(u"widevinecdm-l1"_ns) ? u"Google.Widevine.CDM"_ns : mName,
#  else
      mName,
#  endif
      pluginArch);
  if (NS_FAILED(rv)) {
    GMP_PARENT_LOG_DEBUG("{}: Plugin arch error: {}", __FUNCTION__,
                         uint32_t(rv));
  } else {
    GMP_PARENT_LOG_DEBUG("{}: Plugin arch: 0x{:x}", __FUNCTION__, pluginArch);
  }

  const uint32_t x86 = base::PROCESS_ARCH_X86_64 | base::PROCESS_ARCH_I386;
#  ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  const uint32_t arm64 = base::PROCESS_ARCH_ARM_64;

  mChildLaunchArch = pluginArch;
  // When executing in an ARM64 process, if the library is x86 or x64,
  // set |mChildLaunchArch| to x64 and allow the library to be used as long
  // as this process is a universal binary.
  if (!(pluginArch & arm64) && (pluginArch & x86)) {
    bool isWidevine = parentLeafName.Find(u"widevine") != kNotFound;
    bool isWidevineAllowed =
        StaticPrefs::media_gmp_widevinecdm_allow_x64_plugin_on_arm64();
    bool isH264 = parentLeafName.Find(u"openh264") != kNotFound;
    bool isH264Allowed =
        StaticPrefs::media_gmp_gmpopenh264_allow_x64_plugin_on_arm64();
    bool isClearkey = parentLeafName.Find(u"clearkey") != kNotFound;
    bool isClearkeyAllowed =
        StaticPrefs::media_gmp_gmpclearkey_allow_x64_plugin_on_arm64();

    // Only allow x64 child GMP processes for Widevine, OpenH264 and Clearkey
    if (!isWidevine && !isH264 && !isClearkey) {
      return GenericPromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED,
                                             __func__);
    }
    // And only if prefs permit it.
    if ((isWidevine && !isWidevineAllowed) || (isH264 && !isH264Allowed) ||
        (isClearkey && !isClearkeyAllowed)) {
      return GenericPromise::CreateAndReject(NS_ERROR_PLUGIN_DISABLED,
                                             __func__);
    }

#    ifdef XP_MACOSX
    // We have an x64 library. Get the bundle architecture to determine
    // if we are a universal binary and hence if we can launch an x64
    // child process to host this plugin.
    uint32_t bundleArch = base::PROCESS_ARCH_INVALID;
    rv = nsMacUtilsImpl::GetArchitecturesForBundle(&bundleArch);
    if (NS_FAILED(rv)) {
      // If we fail here, continue as if this is not a univeral binary.
      GMP_PARENT_LOG_DEBUG("{}: Bundle arch error: {}", __FUNCTION__,
                           uint32_t(rv));
    } else {
      GMP_PARENT_LOG_DEBUG("{}: Bundle arch: 0x{:x}", __FUNCTION__, bundleArch);
    }

    bool isUniversalBinary = (bundleArch & base::PROCESS_ARCH_X86_64) &&
                             (bundleArch & base::PROCESS_ARCH_ARM_64);
    if (isUniversalBinary) {
      mChildLaunchArch = base::PROCESS_ARCH_X86_64;
      PreTranslateBins();
    } else {
      return GenericPromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED,
                                             __func__);
    }
#    endif
  }
#  else
  // When executing in a non-ARM process, if the library is not x86 or x64,
  // remove it and return an error. This prevents a child process crash due
  // to loading an incompatible library and forces a new plugin version to be
  // downloaded when the check is next performed. This could occur if a profile
  // is moved from an ARM64 system to an x64 system.
  if ((pluginArch & x86) == 0) {
    GMP_PARENT_LOG_DEBUG("{}: Removing plugin directory", __FUNCTION__);
    aPluginDir->Remove(true);
    return GenericPromise::CreateAndReject(NS_ERROR_NOT_IMPLEMENTED, __func__);
  }
#  endif  // defined(ALLOW_GECKO_CHILD_PROCESS_ARCH)
#endif    // defined(XP_WIN) || defined(XP_MACOSX)

  return ReadGMPMetaData();
}

void GMPParent::Crash() {
  if (mState != GMPState::NotLoaded) {
    (void)SendCrashPluginNow();
  }
}

class NotifyGMPProcessLoadedTask : public Runnable {
 public:
  explicit NotifyGMPProcessLoadedTask(const ::base::ProcessId aProcessId,
                                      GMPParent* aGMPParent)
      : Runnable("NotifyGMPProcessLoadedTask"),
        mProcessId(aProcessId),
        mGMPParent(aGMPParent) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    bool canProfile = true;

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
    if (SandboxInfo::Get().Test(SandboxInfo::kEnabledForMedia) &&
        ipc::shared_memory::UsingPosixShm()) {
      canProfile = false;
    }
#endif

    nsCOMPtr<nsISerialEventTarget> gmpEventTarget =
        mGMPParent->GMPEventTarget();
    if (NS_WARN_IF(!gmpEventTarget)) {
      return NS_ERROR_FAILURE;
    }

#if defined(XP_WIN)
    RefPtr<DllServices> dllSvc(DllServices::Get());
    bool isReadyForBackgroundProcessing =
        dllSvc->IsReadyForBackgroundProcessing();
    gmpEventTarget->Dispatch(NewRunnableMethod<bool, bool>(
        "GMPParent::SendInitDllServices", mGMPParent,
        &GMPParent::SendInitDllServices, isReadyForBackgroundProcessing,
        Telemetry::CanRecordReleaseData()));
#endif

    if (canProfile) {
      ipc::Endpoint<PProfilerChild> profilerParent(
          ProfilerParent::CreateForProcess(mProcessId));

      gmpEventTarget->Dispatch(
          NewRunnableMethod<ipc::Endpoint<mozilla::PProfilerChild>&&>(
              "GMPParent::SendInitProfiler", mGMPParent,
              &GMPParent::SendInitProfiler, std::move(profilerParent)));
    }

    return NS_OK;
  }

  ::base::ProcessId mProcessId;
  const RefPtr<GMPParent> mGMPParent;
};

nsresult GMPParent::LoadProcess() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  MOZ_ASSERT(mState == GMPState::NotLoaded);

  if (NS_WARN_IF(mPluginType == GMPPluginType::WidevineL1)) {
    GMP_PARENT_LOG_DEBUG("{}: cannot load process for WidevineL1",
                         __FUNCTION__);
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsAutoString path;
#ifdef MOZ_WIDGET_ANDROID
  // We need to bundle any CDMs with the APK, so we can just supply the library
  // name to the child process.
  path = mName;
#else
  if (NS_WARN_IF(!mDirectory) ||
      NS_WARN_IF(NS_FAILED(mDirectory->GetPath(path)))) {
    return NS_ERROR_FAILURE;
  }
#endif
  GMP_PARENT_LOG_DEBUG("{}: for {}", __FUNCTION__,
                       NS_ConvertUTF16toUTF8(path).get());

  if (!mProcess) {
    mProcess = new GMPProcessParent(NS_ConvertUTF16toUTF8(path).get());
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
    mProcess->SetRequiresWindowServer(mAdapter.EqualsLiteral("chromium"));
#endif

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
    mProcess->SetLaunchArchitecture(mChildLaunchArch);
#endif

    // Coverage builds instrument plugin-container.exe heavily enough that
    // startup reliably exceeds the default 30s budget; give it headroom so we
    // don't fail to launch on a fixed-timeout race.
#if defined(MOZ_CODE_COVERAGE)
    constexpr int32_t kLaunchTimeoutMs = 60 * 1000;
#else
    constexpr int32_t kLaunchTimeoutMs = 30 * 1000;
#endif
    if (!mProcess->Launch(kLaunchTimeoutMs)) {
      GMP_PARENT_LOG_DEBUG("{}: Failed to launch new child process",
                           __FUNCTION__);
      mProcess = nullptr;
      return NS_ERROR_FAILURE;
    }

    mChildPid = mProcess->GetChildProcessId();
    GMP_PARENT_LOG_DEBUG("{}: Launched new child process", __FUNCTION__);

    bool opened = mProcess->TakeInitialEndpoint().Bind(this);
    if (!opened) {
      GMP_PARENT_LOG_DEBUG("{}: Failed to open channel to new child process",
                           __FUNCTION__);
      mProcess = nullptr;
      return NS_ERROR_FAILURE;
    }
    GMP_PARENT_LOG_DEBUG("{}: Opened channel to new child process",
                         __FUNCTION__);

    // ComputeStorageId may return empty string, we leave the error handling to
    // CDM. The CDM will reject the promise once we provide a empty string of
    // storage id.
    bool ok =
        SendProvideStorageId(CDMStorageIdProvider::ComputeStorageId(mNodeId));
    if (!ok) {
      GMP_PARENT_LOG_DEBUG("{}: Failed to send storage id to child process",
                           __FUNCTION__);
      return NS_ERROR_FAILURE;
    }
    GMP_PARENT_LOG_DEBUG("{}: Sent storage id to child process", __FUNCTION__);

#if defined(XP_WIN) || defined(XP_LINUX)
    if (!mLibs.IsEmpty()) {
      bool ok = SendPreloadLibs(mLibs);
      if (!ok) {
        GMP_PARENT_LOG_DEBUG("{}: Failed to send preload-libs to child process",
                             __FUNCTION__);
        return NS_ERROR_FAILURE;
      }
      GMP_PARENT_LOG_DEBUG("{}: Sent preload-libs ('{}') to child process",
                           __FUNCTION__, mLibs.get());
    }
#endif

#ifdef MOZ_WIDGET_ANDROID
    // On Android the GMP process may be isolated, meaning we must create a
    // ProcInfoParent to allow GetProcInfo() to query the process info remotely.
    Endpoint<PProcInfoParent> procInfoParent;
    Endpoint<PProcInfoChild> procInfoChild;
    if (NS_SUCCEEDED(PProcInfo::CreateEndpoints(
            mozilla::ipc::EndpointProcInfo::Current(), OtherEndpointProcInfo(),
            &procInfoParent, &procInfoChild)) &&
        SendInitProcInfo(std::move(procInfoChild))) {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "GMPParent::CreateProcInfoParent",
          [endpoint = std::move(procInfoParent)]() mutable {
            RefPtr<ProcInfoParent> actor =
                ProcInfoParent::Create(std::move(endpoint));
          }));
    }
#endif

    NS_DispatchToMainThread(new NotifyGMPProcessLoadedTask(OtherPid(), this));

    // Intr call to block initialization on plugin load.
    if (!SendStartPlugin(mAdapter)) {
      GMP_PARENT_LOG_DEBUG("{}: Failed to send start to child process",
                           __FUNCTION__);
      return NS_ERROR_FAILURE;
    }
    GMP_PARENT_LOG_DEBUG("{}: Sent StartPlugin to child process", __FUNCTION__);
  }

  mState = GMPState::Loaded;

  return NS_OK;
}

void GMPParent::OnPreferenceChange(const mozilla::dom::Pref& aPref) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{}", __FUNCTION__);

  if (!mProcess || !mProcess->UseXPCOM()) {
    return;
  }

  (void)SendPreferenceUpdate(aPref);
}

mozilla::ipc::IPCResult GMPParent::RecvPGMPContentChildDestroyed() {
  --mGMPContentChildCount;
  if (!IsUsed()) {
    CloseIfUnused();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPParent::RecvFOGData(ByteBuf&& aBuf) {
  GMP_PARENT_LOG_DEBUG("GMPParent RecvFOGData");
  glean::FOGData(std::move(aBuf));
  return IPC_OK();
}

#if defined(XP_WIN)
mozilla::ipc::IPCResult GMPParent::RecvGetModulesTrust(
    ModuleIdentifiers&& aModIdents, bool aRunAtNormalPriority,
    GetModulesTrustResolver&& aResolver) {
  class ModulesTrustRunnable final : public Runnable {
   public:
    ModulesTrustRunnable(ModuleIdentifiers&& aModIdents,
                         bool aRunAtNormalPriority,
                         GetModulesTrustResolver&& aResolver)
        : Runnable("GMPParent::RecvGetModulesTrust::ModulesTrustRunnable"),
          mModIdents(std::move(aModIdents)),
          mResolver(std::move(aResolver)),
          mEventTarget(GetCurrentSerialEventTarget()),
          mRunAtNormalPriority(aRunAtNormalPriority) {}

    NS_IMETHOD Run() override {
      RefPtr<DllServices> dllSvc(DllServices::Get());
      dllSvc->GetModulesTrust(std::move(mModIdents), mRunAtNormalPriority)
          ->Then(
              mEventTarget, __func__,
              [self = RefPtr{this}](ModulesMapResult&& aResult) {
                self->mResolver(Some(ModulesMapResult(std::move(aResult))));
              },
              [self = RefPtr{this}](nsresult aRv) {
                self->mResolver(Nothing());
              });
      return NS_OK;
    }

   private:
    ~ModulesTrustRunnable() override = default;

    ModuleIdentifiers mModIdents;
    GetModulesTrustResolver mResolver;
    nsCOMPtr<nsISerialEventTarget> mEventTarget;
    bool mRunAtNormalPriority;
  };

  NS_DispatchToMainThread(MakeAndAddRef<ModulesTrustRunnable>(
      std::move(aModIdents), aRunAtNormalPriority, std::move(aResolver)));
  return IPC_OK();
}
#endif  // defined(XP_WIN)

void GMPParent::CloseIfUnused() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{}", __FUNCTION__);

  if ((mDeleteProcessOnlyOnUnload || mState == GMPState::Loaded ||
       mState == GMPState::Unloading) &&
      !IsUsed()) {
    // Ensure all timers are killed.
    for (auto* timer : ManagedPGMPTimerParent()) {
      static_cast<GMPTimerParent*>(timer)->Shutdown();
    }

    // Shutdown GMPStorage. Given that all protocol actors must be shutdown
    // (!Used() is true), all storage operations should be complete.
    for (auto* storage : ManagedPGMPStorageParent()) {
      static_cast<GMPStorageParent*>(storage)->Shutdown();
    }
    Shutdown();
  }
}

void GMPParent::CloseActive(bool aDieWhenUnloaded) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{}: state {}", __FUNCTION__, uint32_t(mState.load()));

  if (aDieWhenUnloaded) {
    mDeleteProcessOnlyOnUnload = true;  // don't allow this to go back...
  }
  if (mState == GMPState::Loaded) {
    mState = GMPState::Unloading;
  }
  if (mState != GMPState::NotLoaded && IsUsed()) {
    (void)SendCloseActive();
    CloseIfUnused();
  }
}

void GMPParent::MarkForDeletion() {
  mDeleteProcessOnlyOnUnload = true;
  mIsBlockingDeletion = true;
}

bool GMPParent::IsMarkedForDeletion() { return mIsBlockingDeletion; }

void GMPParent::Shutdown() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{}", __FUNCTION__);

  if (mAbnormalShutdownInProgress) {
    return;
  }

  MOZ_ASSERT(!IsUsed());
  switch (mState) {
    case GMPState::NotLoaded:
    case GMPState::Closing:
    case GMPState::Closed:
      return;
    default:
      break;
  }

  RefPtr<GMPParent> self(this);
  DeleteProcess();

  // XXX Get rid of mDeleteProcessOnlyOnUnload and this code when
  // Bug 1043671 is fixed
  if (!mDeleteProcessOnlyOnUnload) {
    // Destroy ourselves and rise from the fire to save memory
    mService->ReAddOnGMPThread(self);
  }  // else we've been asked to die and stay dead
  MOZ_ASSERT(mState == GMPState::NotLoaded || mState == GMPState::Closing);
}

class NotifyGMPShutdownTask : public Runnable {
 public:
  explicit NotifyGMPShutdownTask(const nsAString& aNodeId)
      : Runnable("NotifyGMPShutdownTask"), mNodeId(aNodeId) {}
  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> obsService =
        mozilla::services::GetObserverService();
    MOZ_ASSERT(obsService);
    if (obsService) {
      obsService->NotifyObservers(nullptr, "gmp-shutdown", mNodeId.get());
    }
    return NS_OK;
  }
  nsString mNodeId;
};

void GMPParent::DeleteProcess() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());

  switch (mState) {
    case GMPState::Closed:
      // Closing has finished, we can proceed to destroy the process.
      break;
    case GMPState::Closing:
      // Closing in progress, just waiting for the shutdown response.
      GMP_PARENT_LOG_DEBUG("{}: Shutdown handshake in progress.", __FUNCTION__);
      return;
    default: {
      // Don't Close() twice!
      // Probably remove when bug 1043671 is resolved
      GMP_PARENT_LOG_DEBUG("{}: Shutdown handshake starting.", __FUNCTION__);

      RefPtr<GMPParent> self = this;
      nsCOMPtr<nsISerialEventTarget> gmpEventTarget = GMPEventTarget();
      mState = GMPState::Closing;
      // Let's attempt to get the profile from the child process if we can
      // before we destroy it. This is particularly important for the GMP
      // process because we aggressively shut it down when not in active use, so
      // it is easy to miss the recordings during profiling.
      SendShutdown()->Then(
          gmpEventTarget, __func__,
          [self](ProfileAndAdditionalInformation&& aProfileAndAdditionalInfo) {
            GMP_LOG_DEBUG(
                "GMPParent[{}|childPid={}] DeleteProcess: Shutdown handshake "
                "success, profileLen={}.",
                fmt::ptr(self.get()), self->mChildPid,
                aProfileAndAdditionalInfo.mProfile.Length());
            if (!aProfileAndAdditionalInfo.mProfile.IsEmpty()) {
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "GMPParent::DeleteProcess",
                  [profileAndAdditionalInfo =
                       std::move(aProfileAndAdditionalInfo)]() mutable {
                    profiler_received_exit_profile(
                        std::move(profileAndAdditionalInfo));
                  }));
            }
            self->mState = GMPState::Closed;
            self->Close();
            self->DeleteProcess();
          },
          [self](const ipc::ResponseRejectReason&) {
            // We crashed during shutdown, ActorDestroy will perform cleanup.
            GMP_LOG_DEBUG(
                "GMPParent[{}|childPid={}] DeleteProcess: Shutdown handshake "
                "error.",
                fmt::ptr(self.get()), self->mChildPid);
          });
      return;
    }
  }

  GMPEventTarget()->Dispatch(NS_NewRunnableFunction(
      "gmp::GeckoMediaPluginServiceParent::PluginTerminated",
      [self = RefPtr{this}] { self->mService->PluginTerminated(self); }));

  GMP_PARENT_LOG_DEBUG("{}: Shutting down process.", __FUNCTION__);
  mProcess = nullptr;

#if defined(MOZ_WIDGET_ANDROID)
  if (mState != GMPState::NotLoaded) {
    nsCOMPtr<nsIEventTarget> launcherThread(ipc::GetIPCLauncher());
    MOZ_ASSERT(launcherThread);

    auto procType = java::GeckoProcessType::GMPLUGIN();
    auto selector =
        java::GeckoProcessManager::Selector::New(procType, OtherPid());

    launcherThread->Dispatch(NS_NewRunnableFunction(
        "GMPParent::DeleteProcess",
        [selector =
             java::GeckoProcessManager::Selector::GlobalRef(selector)]() {
          java::GeckoProcessManager::ShutdownProcess(selector);
        }));
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  mState = GMPState::NotLoaded;

  nsCOMPtr<nsIRunnable> r =
      new NotifyGMPShutdownTask(NS_ConvertUTF8toUTF16(mNodeId));
  mMainThread->Dispatch(r.forget());
}

GMPState GMPParent::State() const { return mState; }

nsCOMPtr<nsISerialEventTarget> GMPParent::GMPEventTarget() {
  return mGMPEventTarget;
}

/* static */
bool GMPCapability::Supports(const nsTArray<GMPCapability>& aCapabilities,
                             const nsACString& aAPI,
                             const nsTArray<nsCString>& aTags) {
  for (const nsCString& tag : aTags) {
    if (!GMPCapability::Supports(aCapabilities, aAPI, tag)) {
      return false;
    }
  }
  return true;
}

/* static */
bool GMPCapability::Supports(const nsTArray<GMPCapability>& aCapabilities,
                             const nsACString& aAPI, const nsCString& aTag) {
  for (const GMPCapability& capabilities : aCapabilities) {
    if (!capabilities.mAPIName.Equals(aAPI)) {
      continue;
    }
    for (const nsCString& tag : capabilities.mAPITags) {
      if (tag.Equals(aTag)) {
#ifdef XP_WIN
        // Clearkey on Windows advertises that it can decode in its GMP info
        // file, but uses Windows Media Foundation to decode. That's not present
        // on Windows XP, and on some Vista, Windows N, and KN variants without
        // certain services packs.
        if (tag.EqualsLiteral(kClearKeyKeySystemName)) {
          if (capabilities.mAPIName.EqualsLiteral(GMP_API_VIDEO_DECODER)) {
            auto pdmFactory = MakeRefPtr<PDMFactory>();
            if (pdmFactory->SupportsMimeType("video/avc"_ns).isEmpty()) {
              continue;
            }
          }
        }
#endif
        return true;
      }
    }
  }
  return false;
}

bool GMPParent::EnsureProcessLoaded() {
  switch (mState) {
    case GMPState::NotLoaded:
      return NS_SUCCEEDED(LoadProcess());
    case GMPState::Loaded:
      return true;
    case GMPState::Unloading:
    case GMPState::Closing:
    case GMPState::Closed:
      return false;
  }

  MOZ_ASSERT_UNREACHABLE("Unhandled GMPState!");
  return false;
}

void GMPParent::AddCrashAnnotations() {
  if (mCrashReporter) {
    mCrashReporter->AddAnnotationBool(CrashReporter::Annotation::GMPPlugin,
                                      true);
    mCrashReporter->AddAnnotationNSCString(
        CrashReporter::Annotation::PluginFilename,
        NS_ConvertUTF16toUTF8(mName));
    mCrashReporter->AddAnnotationNSCString(
        CrashReporter::Annotation::PluginName, mDisplayName);
    mCrashReporter->AddAnnotationNSCString(
        CrashReporter::Annotation::PluginVersion, mVersion);
  }
}

void GMPParent::GetCrashID(nsString& aResult) {
  AddCrashAnnotations();
  GenerateCrashReport(&aResult);
}

static void GMPNotifyObservers(const uint32_t aPluginID,
                               const nsACString& aPluginName,
                               const nsAString& aPluginDumpID) {
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  nsCOMPtr<nsIWritablePropertyBag2> propbag =
      do_CreateInstance("@mozilla.org/hash-property-bag;1");
  if (obs && propbag) {
    propbag->SetPropertyAsUint32(u"pluginID"_ns, aPluginID);
    propbag->SetPropertyAsACString(u"pluginName"_ns, aPluginName);
    propbag->SetPropertyAsAString(u"pluginDumpID"_ns, aPluginDumpID);
    obs->NotifyObservers(propbag, "gmp-plugin-crash", nullptr);
  }

  RefPtr<gmp::GeckoMediaPluginService> service =
      gmp::GeckoMediaPluginService::GetGeckoMediaPluginService();
  if (service) {
    service->RunPluginCrashCallbacks(aPluginID, aPluginName);
  }
}

void GMPParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{}: ({}), state={}", __FUNCTION__, (int)aWhy,
                       uint32_t(mState.load()));

  if (AbnormalShutdown == aWhy) {
    glean::subprocess::abnormal_abort.Get("gmplugin"_ns).Add(1);
    nsString dumpID;
    GetCrashID(dumpID);
    if (dumpID.IsEmpty()) {
      NS_WARNING("GMP crash without crash report");
      dumpID = mName;
      dumpID += '-';
      AppendUTF8toUTF16(mVersion, dumpID);
    }

    // NotifyObservers is mainthread-only
    nsCOMPtr<nsIRunnable> r =
        WrapRunnableNM(&GMPNotifyObservers, mPluginId, mDisplayName, dumpID);
    mMainThread->Dispatch(r.forget());
  }

  // warn us off trying to close again
  mState = GMPState::Closed;
  mAbnormalShutdownInProgress = true;
  CloseActive(false);

  // Normal Shutdown() will delete the process on unwind. GMPProcessParent
  // blocks shutdown to avoid races.
  if (AbnormalShutdown == aWhy) {
    RefPtr<GMPParent> self(this);
    // Must not call Close() again in DeleteProcess(), as we'll recurse
    // infinitely if we do.
    MOZ_ASSERT(mState == GMPState::Closed);
    DeleteProcess();
    // Note: final destruction will be Dispatched to ourself
    mService->ReAddOnGMPThread(self);
  }
}

already_AddRefed<PGMPStorageParent> GMPParent::AllocPGMPStorageParent() {
  auto p = MakeRefPtr<GMPStorageParent>(mNodeId, this);
  if (NS_WARN_IF(NS_FAILED(p->Init()))) {
    return nullptr;
  }
  return p.forget();
}

already_AddRefed<PGMPTimerParent> GMPParent::AllocPGMPTimerParent() {
  nsCOMPtr<nsISerialEventTarget> target = GMPEventTarget();
  return MakeAndAddRef<GMPTimerParent>(std::move(target));
}

bool ReadInfoField(GMPInfoFileParser& aParser, const nsCString& aKey,
                   nsACString& aOutValue) {
  if (!aParser.Contains(aKey) || aParser.Get(aKey).IsEmpty()) {
    return false;
  }
  aOutValue = aParser.Get(aKey);
  return true;
}

RefPtr<GenericPromise> GMPParent::ReadGMPMetaData() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  MOZ_ASSERT(mDirectory, "Plugin directory cannot be NULL!");
  MOZ_ASSERT(!mName.IsEmpty(), "Plugin mName cannot be empty!");

  nsCOMPtr<nsIFile> infoFile;
  nsresult rv = mDirectory->Clone(getter_AddRefs(infoFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }
  infoFile->AppendRelativePath(mName + u".info"_ns);

  if (FileExists(infoFile)) {
    return ReadGMPInfoFile(infoFile);
  }

  // Maybe this is the Widevine adapted plugin?
  nsCOMPtr<nsIFile> manifestFile;
  rv = mDirectory->Clone(getter_AddRefs(manifestFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }
  manifestFile->AppendRelativePath(u"manifest.json"_ns);
  return ReadChromiumManifestFile(manifestFile);
}

#if defined(XP_LINUX)
static void ApplyGlibcWorkaround(nsCString& aLibs) {
  // These glibc libraries were merged into libc.so.6 as of glibc
  // 2.34; they now exist only as stub libraries for compatibility and
  // newly linked code won't depend on them, so we need to ensure
  // they're loaded for plugins that may have been linked against a
  // different version of glibc.  (See also bug 1725828.)
  if (!aLibs.IsEmpty()) {
    aLibs.AppendLiteral(", ");
  }
  aLibs.AppendLiteral("libdl.so.2, libpthread.so.0, librt.so.1");
}
#endif

#if defined(XP_WIN)
static void ApplyOleaut32(nsCString& aLibs) {
  // In the libwebrtc update in bug 1766646 an include of comdef.h for using
  // _bstr_t was introduced. This resulted in a dependency on comsupp.lib which
  // contains a `_variant_t vtMissing` that would get cleared in an exit
  // handler. VariantClear is defined in oleaut32.dll, and so we'd try to load
  // oleaut32.dll on exit but get denied by the sandbox.
  // Note that we had includes of comdef.h before bug 1766646 but it is the use
  // of _bstr_t that triggers the vtMissing exit handler.
  // See bug 1788592 for details.
  if (!aLibs.IsEmpty()) {
    aLibs.AppendLiteral(", ");
  }
  aLibs.AppendLiteral("oleaut32.dll");
}
#endif

static constexpr uint64_t MakeVersion(uint16_t aA, uint16_t aB, uint16_t aC,
                                      uint16_t aD) {
  return (static_cast<uint64_t>(aA) << 48) | (static_cast<uint64_t>(aB) << 32) |
         (static_cast<uint64_t>(aC) << 16) | aD;
}

static nsresult ParseVersion(const nsACString& aVersion,
                             uint64_t* aParsedVersion) {
  MOZ_ASSERT(aParsedVersion);

  uint64_t version = 0;
  uint32_t fragmentCount = 0;
  nsresult rv = NS_OK;

  for (const auto& fragment : aVersion.Split('.')) {
    ++fragmentCount;
    if (NS_WARN_IF(fragmentCount >= 5)) {
      return NS_ERROR_FAILURE;
    }

    uint32_t fragmentInt = fragment.ToUnsignedInteger(&rv, /* aRadix */ 10);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    version = (version << 16) | SaturatingCast<uint16_t>(fragmentInt);
  }

  if (NS_WARN_IF(fragmentCount == 0)) {
    return NS_ERROR_FAILURE;
  }

  *aParsedVersion = version << (4 - fragmentCount) * 16;
  return NS_OK;
}

RefPtr<GenericPromise> GMPParent::ReadGMPInfoFile(nsIFile* aFile) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMPInfoFileParser parser;
  if (!parser.Init(aFile)) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  nsAutoCString apis;
  if (!ReadInfoField(parser, "name"_ns, mDisplayName) ||
      !ReadInfoField(parser, "description"_ns, mDescription) ||
      !ReadInfoField(parser, "version"_ns, mVersion) ||
      !ReadInfoField(parser, "apis"_ns, apis)) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

#if defined(XP_WIN) || defined(XP_LINUX)
  // "Libraries" field is optional.
  ReadInfoField(parser, "libraries"_ns, mLibs);
#endif

  UpdatePluginType();

  // We check the version for OpenH264 because we may need to add additional API
  // tags to indicate we support more advanced modes for newer versions of the
  // plugin.
  bool addMozSupportsH264Advanced = false;
  bool addMozSupportsH264TemporalSVC = false;
  if (mPluginType == GMPPluginType::OpenH264) {
    uint64_t parsedVersion = 0;
    nsresult rv = ParseVersion(mVersion, &parsedVersion);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return GenericPromise::CreateAndReject(rv, __func__);
    }

    // Earlier versions only supported decoding/encoding constrained baseline.
    addMozSupportsH264Advanced = parsedVersion >= MakeVersion(2, 3, 2, 0);

    // Earlier versions did not expose the encoded SVC temporal layer ID.
    addMozSupportsH264TemporalSVC = parsedVersion > MakeVersion(2, 5, 0, 0);
  }

#ifdef XP_LINUX
  // The glibc workaround (see above) isn't needed for clearkey
  // because it's built along with the browser.
  if (mPluginType != GMPPluginType::Clearkey) {
    ApplyGlibcWorkaround(mLibs);
  }
#endif

#ifdef XP_WIN
  ApplyOleaut32(mLibs);
#endif

  nsTArray<nsCString> apiTokens;
  SplitAt(", ", apis, apiTokens);
  for (const nsCString& api : apiTokens) {
    int32_t tagsStart = api.FindChar('[');
    if (tagsStart == 0) {
      // Not allowed to be the first character.
      // API name must be at least one character.
      continue;
    }

    GMPCapability cap;
    if (tagsStart == -1) {
      // No tags.
      cap.mAPIName.Assign(api);
    } else {
      auto tagsEnd = api.FindChar(']');
      if (tagsEnd == -1 || tagsEnd < tagsStart) {
        // Invalid syntax, skip whole capability.
        continue;
      }

      cap.mAPIName.Assign(Substring(api, 0, tagsStart));

      if ((tagsEnd - tagsStart) > 1) {
        const nsDependentCSubstring ts(
            Substring(api, tagsStart + 1, tagsEnd - tagsStart - 1));
        nsTArray<nsCString> tagTokens;
        SplitAt(":", ts, tagTokens);
        for (const nsCString& tag : tagTokens) {
          cap.mAPITags.AppendElement(tag);
        }
      }
    }

    if (mPluginType == GMPPluginType::OpenH264) {
      if (addMozSupportsH264Advanced &&
          !cap.mAPITags.Contains("moz-h264-advanced"_ns)) {
        cap.mAPITags.AppendElement("moz-h264-advanced"_ns);
      }
      if (addMozSupportsH264TemporalSVC &&
          !cap.mAPITags.Contains("moz-h264-temporal-svc"_ns)) {
        cap.mAPITags.AppendElement("moz-h264-temporal-svc"_ns);
      }
    }

    mCapabilities.AppendElement(std::move(cap));
  }

  if (mCapabilities.IsEmpty()) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  return GenericPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> GMPParent::ReadChromiumManifestFile(nsIFile* aFile) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  nsAutoCString json;
  if (!ReadIntoString(aFile, json, 5 * 1024)) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  // DOM JSON parsing needs to run on the main thread.
  return InvokeAsync(mMainThread, this, __func__,
                     &GMPParent::ParseChromiumManifest,
                     NS_ConvertUTF8toUTF16(json));
}

static bool IsCDMAPISupported(
    const mozilla::dom::WidevineCDMManifest& aManifest) {
  if (!aManifest.mX_cdm_module_versions.WasPassed() ||
      !aManifest.mX_cdm_interface_versions.WasPassed() ||
      !aManifest.mX_cdm_host_versions.WasPassed()) {
    return false;
  }

  nsresult ignored;  // Note: ToInteger returns 0 on failure.
  int32_t moduleVersion =
      aManifest.mX_cdm_module_versions.Value().ToInteger(&ignored);
  int32_t interfaceVersion =
      aManifest.mX_cdm_interface_versions.Value().ToInteger(&ignored);
  int32_t hostVersion =
      aManifest.mX_cdm_host_versions.Value().ToInteger(&ignored);
  return ChromiumCDMAdapter::Supports(moduleVersion, interfaceVersion,
                                      hostVersion);
}

RefPtr<GenericPromise> GMPParent::ParseChromiumManifest(
    const nsAString& aJSON) {
  GMP_PARENT_LOG_DEBUG("{}: for '{}'", __FUNCTION__,
                       NS_LossyConvertUTF16toASCII(aJSON).get());

  MOZ_ASSERT(NS_IsMainThread());
  mozilla::dom::WidevineCDMManifest m;
  if (!m.Init(aJSON)) {
    GMP_PARENT_LOG_DEBUG("{}: Failed to initialize json parser, failing.",
                         __FUNCTION__);
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  CopyUTF16toUTF8(m.mName, mDisplayName);
  CopyUTF16toUTF8(m.mVersion, mVersion);

  if (m.mDescription.WasPassed()) {
    CopyUTF16toUTF8(m.mDescription.Value(), mDescription);
  }

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  if (!mozilla::SandboxInfo::Get().CanSandboxMedia()) {
    nsPrintfCString msg(
        "GMPParent::ParseChromiumManifest: Plugin \"%s\" is an EME CDM"
        " but this system can't sandbox it; not loading.",
        mDisplayName.get());
    printf_stderr("%s\n", msg.get());
    GMP_PARENT_LOG_DEBUG("{}", msg.get());
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }
#endif

  UpdatePluginType();

  GMPCapability video;

  if (IsCDMAPISupported(m)) {
    video.mAPIName = nsLiteralCString(CHROMIUM_CDM_API);
    mAdapter = u"chromium"_ns;
#ifdef MOZ_WMF_CDM
  } else if (mPluginType == GMPPluginType::WidevineL1) {
    video.mAPIName = nsCString(kWidevineExperimentAPIName);
    mAdapter = NS_ConvertUTF8toUTF16(kWidevineExperimentAPIName);
#endif
  } else {
    GMP_PARENT_LOG_DEBUG("{}: CDM API not supported, failing.", __FUNCTION__);
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  // We hard code a few of the settings because they can't be stored in the
  // widevine manifest without making our API different to widevine's.
  switch (mPluginType) {
    case GMPPluginType::Clearkey:
      video.mAPITags.AppendElement(nsCString{kClearKeyKeySystemName});
      video.mAPITags.AppendElement(
          nsCString{kClearKeyWithProtectionQueryKeySystemName});
#if XP_WIN
      mLibs = nsLiteralCString(
          "dxva2.dll, evr.dll, freebl3.dll, mfh264dec.dll, mfplat.dll, "
          "msmpeg2vdec.dll, nss3.dll, softokn3.dll");
#elif XP_LINUX
      mLibs = "libfreeblpriv3.so, libsoftokn3.so"_ns;
#endif
      break;
    case GMPPluginType::Widevine:
      video.mAPITags.AppendElement(nsCString{kWidevineKeySystemName});
#if XP_WIN
      // psapi.dll added for GetMappedFileNameW, which could possibly be avoided
      // in future versions, see bug 1383611 for details.
      mLibs = "dxva2.dll, ole32.dll, psapi.dll, shell32.dll, winmm.dll"_ns;
#endif
      break;
#ifdef MOZ_WMF_CDM
    case GMPPluginType::WidevineL1:
      video.mAPITags.AppendElement(nsCString{kWidevineExperimentKeySystemName});
      video.mAPITags.AppendElement(
          nsCString{kWidevineExperiment2KeySystemName});
      break;
#endif
    case GMPPluginType::Fake:
      // The fake CDM just exposes a key system with id "fake".
      video.mAPITags.AppendElement(nsCString{"fake"});
#if XP_WIN
      mLibs = "dxva2.dll, ole32.dll"_ns;
#endif
      break;
    default:
      GMP_PARENT_LOG_DEBUG("{}: Unrecognized key system: {}, failing.",
                           __FUNCTION__, mDisplayName.get());
      return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

#ifdef XP_LINUX
  ApplyGlibcWorkaround(mLibs);
#endif

#ifdef XP_WIN
  ApplyOleaut32(mLibs);
#endif

  nsTArray<nsCString> codecs;

  if (m.mX_cdm_codecs.WasPassed()) {
    nsCString codecsString;
    codecsString = NS_ConvertUTF16toUTF8(m.mX_cdm_codecs.Value());
    SplitAt(",", codecsString, codecs);
  }

  // Parse the codec strings in the manifest and map them to strings used
  // internally by Gecko for capability recognition.
  //
  // Google's code to parse manifests can be used as a reference for strings
  // the manifest may contain
  // https://source.chromium.org/chromium/chromium/src/+/master:components/cdm/common/cdm_manifest.cc;l=74;drc=775880ced8a989191281e93854c7f2201f25068f
  //
  // Gecko's internal strings can be found at
  // https://searchfox.org/mozilla-central/rev/ea63a0888d406fae720cf24f4727d87569a8cab5/dom/media/eme/MediaKeySystemAccess.cpp#149-155
  for (const nsCString& chromiumCodec : codecs) {
    nsCString codec;
    if (chromiumCodec.EqualsASCII("vp8")) {
      codec = "vp8"_ns;
    } else if (chromiumCodec.EqualsASCII("vp9.0") ||  // Legacy string.
               chromiumCodec.EqualsASCII("vp09")) {
      codec = "vp9"_ns;
    } else if (chromiumCodec.EqualsASCII("avc1")) {
      codec = "h264"_ns;
    } else if (chromiumCodec.EqualsASCII("av01")) {
      codec = "av1"_ns;
    } else {
      GMP_PARENT_LOG_DEBUG("{}: Unrecognized codec: {}.", __FUNCTION__,
                           chromiumCodec.get());
      MOZ_ASSERT_UNREACHABLE(
          "Unhandled codec string! Need to add it to the parser.");
      continue;
    }

    video.mAPITags.AppendElement(codec);
  }

  mCapabilities.AppendElement(std::move(video));

  GMP_PARENT_LOG_DEBUG("{}: Successfully parsed manifest.", __FUNCTION__);
  return GenericPromise::CreateAndResolve(true, __func__);
}

bool GMPParent::CanBeSharedCrossNodeIds() const {
  return mNodeId.IsEmpty() &&
         // XXX bug 1159300 hack -- maybe remove after openh264 1.4
         // We don't want to use CDM decoders for non-encrypted playback
         // just yet; especially not for WebRTC. Don't allow CDMs to be used
         // without a node ID.
         !mCanDecrypt;
}

bool GMPParent::CanBeUsedFrom(const nsACString& aNodeId) const {
  return mNodeId == aNodeId;
}

void GMPParent::SetNodeId(const nsACString& aNodeId) {
  MOZ_ASSERT(!aNodeId.IsEmpty());
  mNodeId = aNodeId;
}

void GMPParent::UpdatePluginType() {
  if (mDisplayName.EqualsLiteral("WidevineCdm")) {
    mPluginType = GMPPluginType::Widevine;
#ifdef MOZ_WMF_CDM
  } else if (mDisplayName.EqualsLiteral(kWidevineExperimentAPIName)) {
    mPluginType = GMPPluginType::WidevineL1;
#endif
  } else if (mDisplayName.EqualsLiteral("gmpopenh264")) {
    mPluginType = GMPPluginType::OpenH264;
  } else if (mDisplayName.EqualsLiteral("clearkey")) {
    mPluginType = GMPPluginType::Clearkey;
  } else if (mDisplayName.EqualsLiteral("fake")) {
    mPluginType = GMPPluginType::Fake;
  } else {
    mPluginType = GMPPluginType::Unknown;
  }
}

const nsCString& GMPParent::GetDisplayName() const { return mDisplayName; }

const nsCString& GMPParent::GetVersion() const { return mVersion; }

uint32_t GMPParent::GetPluginId() const { return mPluginId; }

void GMPParent::ResolveGetContentParentPromises() {
  nsTArray<UniquePtr<MozPromiseHolder<GetGMPContentParentPromise>>> promises =
      std::move(mGetContentParentPromises);
  MOZ_ASSERT(mGetContentParentPromises.IsEmpty());
  RefPtr<GMPContentParentCloseBlocker> blocker(
      new GMPContentParentCloseBlocker(mGMPContentParent));
  for (auto& holder : promises) {
    holder->Resolve(blocker, __func__);
  }
}

bool GMPParent::OpenPGMPContent() {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  MOZ_ASSERT(!mGMPContentParent);

  Endpoint<PGMPContentParent> parent;
  Endpoint<PGMPContentChild> child;
  if (NS_WARN_IF(NS_FAILED(PGMPContent::CreateEndpoints(
          mozilla::ipc::EndpointProcInfo::Current(), OtherEndpointProcInfo(),
          &parent, &child)))) {
    return false;
  }

  mGMPContentParent = new GMPContentParent(this);

  if (!parent.Bind(mGMPContentParent)) {
    return false;
  }

  if (!SendInitGMPContentChild(std::move(child))) {
    return false;
  }

  ResolveGetContentParentPromises();

  return true;
}

void GMPParent::RejectGetContentParentPromises() {
  nsTArray<UniquePtr<MozPromiseHolder<GetGMPContentParentPromise>>> promises =
      std::move(mGetContentParentPromises);
  MOZ_ASSERT(mGetContentParentPromises.IsEmpty());
  for (auto& holder : promises) {
    holder->Reject(NS_ERROR_FAILURE, __func__);
  }
}

void GMPParent::GetGMPContentParent(
    UniquePtr<MozPromiseHolder<GetGMPContentParentPromise>>&& aPromiseHolder) {
  MOZ_ASSERT(GMPEventTarget()->IsOnCurrentThread());
  GMP_PARENT_LOG_DEBUG("{} {}", __FUNCTION__, fmt::ptr(this));

  if (mGMPContentParent) {
    RefPtr<GMPContentParentCloseBlocker> blocker(
        new GMPContentParentCloseBlocker(mGMPContentParent));
    aPromiseHolder->Resolve(blocker, __func__);
  } else {
    mGetContentParentPromises.AppendElement(std::move(aPromiseHolder));
    // If we don't have a GMPContentParent and we try to get one for the first
    // time (mGetContentParentPromises.Length() == 1) then call
    // PGMPContent::Open. If more calls to GetGMPContentParent happen before
    // mGMPContentParent has been set then we should just store them, so that
    // they get called when we set mGMPContentParent as a result of the
    // PGMPContent::Open call.
    if (mGetContentParentPromises.Length() == 1) {
      if (!EnsureProcessLoaded() || !OpenPGMPContent()) {
        RejectGetContentParentPromises();
        return;
      }
      // We want to increment this as soon as possible, to avoid that we'd try
      // to shut down the GMP process while we're still trying to get a
      // PGMPContentParent actor.
      ++mGMPContentChildCount;
    }
  }
}

already_AddRefed<GMPContentParent> GMPParent::ForgetGMPContentParent() {
  MOZ_ASSERT(mGetContentParentPromises.IsEmpty());
  return mGMPContentParent.forget();
}

bool GMPParent::EnsureProcessLoaded(GeckoChildID* aID) {
  if (!EnsureProcessLoaded()) {
    return false;
  }
  *aID = OtherChildID();
  return true;
}

void GMPParent::IncrementGMPContentChildCount() { ++mGMPContentChildCount; }

nsString GMPParent::GetPluginBaseName() const { return u"gmp-"_ns + mName; }

#if defined(XP_MACOSX) && defined(__aarch64__)
void GMPParent::PreTranslateBins() {
  nsCOMPtr<nsIRunnable> event = mozilla::NewRunnableMethod(
      "RosettaTranslation", this, &GMPParent::PreTranslateBinsWorker);

  DebugOnly<nsresult> rv =
      NS_DispatchBackgroundTask(event.forget(), NS_DISPATCH_EVENT_MAY_BLOCK);

  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

void GMPParent::PreTranslateBinsWorker() {
  int rv = nsMacUtilsImpl::PreTranslateXUL();
  GMP_PARENT_LOG_DEBUG("{}: XUL translation result: {}", __FUNCTION__, rv);

  rv = nsMacUtilsImpl::PreTranslateBinary(mPluginFilePath);
  GMP_PARENT_LOG_DEBUG("{}: {} translation result: {}", __FUNCTION__,
                       mPluginFilePath.get(), rv);
}
#endif

}  // namespace mozilla::gmp

#undef GMP_PARENT_LOG_DEBUG
#undef __CLASS__
