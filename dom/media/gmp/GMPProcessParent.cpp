/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPProcessParent.h"

#include "GMPUtils.h"
#include "nsIRunnable.h"
#ifdef XP_WIN
#  include "WinUtils.h"
#endif
#include <string>

#include "GMPLog.h"
#include "base/process_util.h"
#include "base/string_util.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProcessUtils.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/GeckoProcessManagerWrappers.h"
#endif
#include "nsFmtString.h"

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "mozilla/Omnijar.h"
#  include "mozilla/Preferences.h"
#  include "mozilla/Sandbox.h"
#  include "mozilla/SandboxSettings.h"
#  include "nsMacUtilsImpl.h"
#endif

using std::string;
using std::vector;

using mozilla::gmp::GMPProcessParent;
using mozilla::ipc::GeckoChildProcessHost;

namespace mozilla::gmp {

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool GMPProcessParent::sLaunchWithMacSandbox = true;
bool GMPProcessParent::sMacSandboxGMPLogging = false;
#  if defined(DEBUG)
bool GMPProcessParent::sIsMainThreadInitDone = false;
#  endif
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
/* static */
void GMPProcessParent::InitStaticMainThread() {
  // The GMPProcessParent constructor is called off the
  // main thread. Do main thread initialization here.
  MOZ_ASSERT(NS_IsMainThread());
  sMacSandboxGMPLogging =
      Preferences::GetBool("security.sandbox.logging.enabled") ||
      PR_GetEnv("MOZ_SANDBOX_GMP_LOGGING") || PR_GetEnv("MOZ_SANDBOX_LOGGING");
  GMP_LOG_DEBUG("GMPProcessParent::InitStaticMainThread: sandbox logging={}",
                sMacSandboxGMPLogging ? "true" : "false");
#  if defined(DEBUG)
  sIsMainThreadInitDone = true;
#  endif
}
#endif

GMPProcessParent::GMPProcessParent(const std::string& aGMPPath)
    : GeckoChildProcessHost(GeckoProcessType_GMPlugin),
      mGMPPath(aGMPPath),
      mUseXpcom(StaticPrefs::media_gmp_use_minimal_xpcom())
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
      ,
      mRequiresWindowServer(false)
#endif
{
  MOZ_COUNT_CTOR(GMPProcessParent);
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  MOZ_ASSERT(sIsMainThreadInitDone == true);
  mDisableOSActivityMode = sLaunchWithMacSandbox;
#endif
}

GMPProcessParent::~GMPProcessParent() { MOZ_COUNT_DTOR(GMPProcessParent); }

bool GMPProcessParent::Launch(int32_t aTimeoutMs) {
  class PrefSerializerRunnable final : public Runnable {
   public:
    PrefSerializerRunnable()
        : Runnable("GMPProcessParent::PrefSerializerRunnable"),
          mMonitor("GMPProcessParent::PrefSerializerRunnable::mMonitor") {}

    NS_IMETHOD Run() override {
      auto prefSerializer = std::make_unique<ipc::SharedPreferenceSerializer>();
      bool success =
          prefSerializer->SerializeToSharedMemory(GeckoProcessType_GMPlugin,
                                                  /* remoteType */ {});

      MonitorAutoLock lock(mMonitor);
      MOZ_ASSERT(!mComplete);
      if (success) {
        mPrefSerializer = std::move(prefSerializer);
      }
      mComplete = true;
      lock.Notify();
      return NS_OK;
    }

    void Wait(int32_t aTimeoutMs,
              UniquePtr<ipc::SharedPreferenceSerializer>& aOut) {
      MonitorAutoLock lock(mMonitor);

      TimeDuration timeout = TimeDuration::FromMilliseconds(aTimeoutMs);
      while (!mComplete) {
        if (lock.Wait(timeout) == CVStatus::Timeout) {
          return;
        }
      }

      aOut = std::move(mPrefSerializer);
    }

   private:
    Monitor mMonitor;
    UniquePtr<ipc::SharedPreferenceSerializer> mPrefSerializer
        MOZ_GUARDED_BY(mMonitor);
    bool mComplete MOZ_GUARDED_BY(mMonitor) = false;
  };

  nsresult rv;
  geckoargs::ChildProcessArgs args;
  UniquePtr<ipc::SharedPreferenceSerializer> prefSerializer;

  ipc::ProcessChild::AddPlatformBuildID(args);

  if (mUseXpcom) {
    // Dispatch our runnable to the main thread to grab the serialized prefs. We
    // can only do this on the main thread, and unfortunately we are the only
    // process that launches from the non-main thread.
    auto prefTask = MakeRefPtr<PrefSerializerRunnable>();
    rv = NS_DispatchToMainThread(prefTask);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }

    // We don't want to release our thread context while we wait for the main
    // thread to process the prefs. We already block when waiting for the launch
    // of the process itself to finish, and the state machine assumes this call
    // is blocking. This is also important for the buffering of pref updates,
    // since we know any tasks dispatched with updates won't run until we launch
    // (or fail to launch) the process.
    prefTask->Wait(aTimeoutMs, prefSerializer);
    if (NS_WARN_IF(!prefSerializer)) {
      return false;
    }

    prefSerializer->AddSharedPrefCmdLineArgs(*this, args);
  }

  geckoargs::sPluginNativeEvent.Put(
      StaticPrefs::media_gmp_use_native_event_processing(), args);

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  GMP_LOG_DEBUG("GMPProcessParent::Launch() mLaunchArch: {}", mLaunchArch);
#  if defined(XP_MACOSX)
  mLaunchOptions->arch = mLaunchArch;
  if (mLaunchArch == base::PROCESS_ARCH_X86_64) {
    mLaunchOptions->env_map["MOZ_SHMEM_PAGESIZE_16K"] = 1;
  }
#  endif
#endif

  // Resolve symlinks in the plugin path. The sandbox prevents
  // resolving symlinks in the child process if access to link
  // source file is denied.
#ifdef XP_WIN
  nsAutoString normalizedPath;
#else
  nsAutoCString normalizedPath;
#endif
  rv = NormalizePath(mGMPPath.c_str(), normalizedPath);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_LOG_DEBUG(
        "GMPProcessParent::Launch: "
        "plugin path normaliziation failed for path: {}",
        mGMPPath.c_str());
  }

#ifdef XP_WIN
  std::wstring wGMPPath;
  if (NS_SUCCEEDED(rv)) {
    wGMPPath = normalizedPath.get();
  } else {
    wGMPPath = UTF8ToWide(mGMPPath.c_str());
  }

  // The sandbox doesn't allow file system rules where the paths contain
  // symbolic links or junction points. Sometimes the Users folder has been
  // moved to another drive using a junction point, so allow for this specific
  // case. See bug 1236680 for details.
  if (NS_WARN_IF(
          !widget::WinUtils::ResolveJunctionPointsAndSymLinks(wGMPPath))) {
    GMP_LOG_DEBUG("ResolveJunctionPointsAndSymLinks failed for GMP path={}",
                  NS_ConvertUTF16toUTF8(wGMPPath.c_str()).get());
    return false;
  }
  GMP_LOG_DEBUG("GMPProcessParent::Launch() resolved path to {}",
                NS_ConvertUTF16toUTF8(wGMPPath.c_str()).get());

#  ifdef MOZ_SANDBOX
  // If the GMP path is a network path that is not mapped to a drive letter,
  // then we need to fix the path format for the sandbox rule.
  wchar_t volPath[MAX_PATH];
  if (::GetVolumePathNameW(wGMPPath.c_str(), volPath, MAX_PATH) &&
      ::GetDriveTypeW(volPath) == DRIVE_REMOTE &&
      wGMPPath.compare(0, 2, L"\\\\") == 0) {
    std::wstring sandboxGMPPath(wGMPPath);
    sandboxGMPPath.insert(1, L"??\\UNC");
    mAllowedFilesRead.push_back(sandboxGMPPath + L"\\*");
  } else {
    mAllowedFilesRead.push_back(wGMPPath + L"\\*");
  }
#  endif

  std::string gmpPath = WideToUTF8(wGMPPath);
  geckoargs::sPluginPath.Put(gmpPath.c_str(), args);
#else
  if (NS_SUCCEEDED(rv)) {
    geckoargs::sPluginPath.Put(normalizedPath.get(), args);
  } else {
    geckoargs::sPluginPath.Put(mGMPPath.c_str(), args);
  }
#endif

#ifdef MOZ_WIDGET_ANDROID
  java::GeckoProcessManager::SetIsolatedGmpEnabled(
      StaticPrefs::media_gmp_android_isolated_process());
#endif

  // We need to wait until OnChannelConnected to clear the pref serializer, but
  // SyncLaunch will block until that is called, so we don't actually need to do
  // any overriding, and it only lives on the stack.
  bool launched = SyncLaunch(std::move(args), aTimeoutMs);
  if (launched) {
    nsFmtString name{u"GMPProcessParent {}", static_cast<void*>(this)};
    mShutdownBlocker = media::ShutdownBlockingTicket::Create(
        name, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__);
  }
  return launched;
}

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool GMPProcessParent::IsMacSandboxLaunchEnabled() {
  return sLaunchWithMacSandbox;
}

void GMPProcessParent::SetRequiresWindowServer(bool aRequiresWindowServer) {
  mRequiresWindowServer = aRequiresWindowServer;
}

bool GMPProcessParent::FillMacSandboxInfo(MacSandboxInfo& aInfo) {
  aInfo.type = MacSandboxType_GMP;
  aInfo.hasWindowServer = mRequiresWindowServer;
  aInfo.shouldLog = (aInfo.shouldLog || sMacSandboxGMPLogging);
  nsAutoCString appPath;
  if (!nsMacUtilsImpl::GetAppPath(appPath)) {
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: failed to get app path");
    return false;
  }
  aInfo.appPath.assign(appPath.get());

  GMP_LOG_DEBUG(
      "GMPProcessParent::FillMacSandboxInfo: "
      "plugin dir path: {}",
      mGMPPath.c_str());
  nsCOMPtr<nsIFile> pluginDir;
  nsresult rv = NS_NewLocalFile(NS_ConvertUTF8toUTF16(mGMPPath.c_str()),
                                getter_AddRefs(pluginDir));
  if (NS_FAILED(rv)) {
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: "
        "NS_NewLocalFile failed for plugin dir, rv={}",
        uint32_t(rv));
    return false;
  }

  rv = pluginDir->Normalize();
  if (NS_FAILED(rv)) {
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: "
        "failed to normalize plugin dir path, rv={}",
        uint32_t(rv));
    return false;
  }

  nsAutoCString resolvedPluginPath;
  pluginDir->GetNativePath(resolvedPluginPath);
  aInfo.pluginPath.assign(resolvedPluginPath.get());
  GMP_LOG_DEBUG(
      "GMPProcessParent::FillMacSandboxInfo: "
      "resolved plugin dir path: {}",
      resolvedPluginPath.get());

  if (!mozilla::IsPackagedBuild()) {
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: IsPackagedBuild()=false");

    // Repo dir
    nsCOMPtr<nsIFile> repoDir;
    rv = nsMacUtilsImpl::GetRepoDir(getter_AddRefs(repoDir));
    if (NS_FAILED(rv)) {
      GMP_LOG_DEBUG(
          "GMPProcessParent::FillMacSandboxInfo: failed to get repo dir");
      return false;
    }
    nsCString repoDirPath;
    (void)repoDir->GetNativePath(repoDirPath);
    aInfo.testingReadPath1 = repoDirPath.get();
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: "
        "repo dir path: {}",
        repoDirPath.get());

    // Object dir
    nsCOMPtr<nsIFile> objDir;
    rv = nsMacUtilsImpl::GetObjDir(getter_AddRefs(objDir));
    if (NS_FAILED(rv)) {
      GMP_LOG_DEBUG(
          "GMPProcessParent::FillMacSandboxInfo: failed to get object dir");
      return false;
    }
    nsCString objDirPath;
    (void)objDir->GetNativePath(objDirPath);
    aInfo.testingReadPath2 = objDirPath.get();
    GMP_LOG_DEBUG(
        "GMPProcessParent::FillMacSandboxInfo: "
        "object dir path: {}",
        objDirPath.get());
  }
  return true;
}
#endif

nsresult GMPProcessParent::NormalizePath(const char* aPath,
                                         PathString& aNormalizedPath) {
  nsCOMPtr<nsIFile> fileOrDir;
  nsresult rv =
      NS_NewLocalFile(NS_ConvertUTF8toUTF16(aPath), getter_AddRefs(fileOrDir));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = fileOrDir->Normalize();
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef XP_WIN
  return fileOrDir->GetTarget(aNormalizedPath);
#else
  bool isLink = false;
  rv = fileOrDir->IsSymlink(&isLink);
  NS_ENSURE_SUCCESS(rv, rv);
  if (isLink) {
    return fileOrDir->GetNativeTarget(aNormalizedPath);
  }
  return fileOrDir->GetNativePath(aNormalizedPath);
#endif
}

}  // namespace mozilla::gmp
