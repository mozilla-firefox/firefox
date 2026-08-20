/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define MOZ_USE_LAUNCHER_ERROR

#include "sandboxBroker.h"

#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>

#include <string>

#include "ConfigHelpers.h"
#include "GfxDriverInfo.h"
#include "WinUtils.h"
#include "base/win/sid.h"
#include "base/win/windows_version.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ImportDir.h"
#include "mozilla/Logging.h"
#include "mozilla/NSPRLogModulesParser.h"
#include "mozilla/Omnijar.h"
#include "mozilla/Preferences.h"
#include "mozilla/SHA1.h"
#include "mozilla/SandboxSettings.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WinDllServices.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/glean/SecuritySandboxMetrics.h"
#include "mozilla/ipc/LaunchError.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCOMPtr.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsIGfxInfo.h"
#include "nsIProperties.h"
#include "nsIXULRuntime.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "sandbox/win/src/app_container.h"
#include "sandbox/win/src/sandbox.h"
#include "sandbox/win/src/security_level.h"

#define SANDBOX_SUCCEED_OR_CRASH(x)                                   \
  do {                                                                \
    sandbox::ResultCode result = (x);                                 \
    MOZ_RELEASE_ASSERT(result == sandbox::SBOX_ALL_OK, #x " failed"); \
  } while (0)

namespace mozilla {

constexpr wchar_t kLpacFirefoxInstallFiles[] = L"lpacFirefoxInstallFiles";

sandbox::BrokerServices* sBrokerService = nullptr;

// This is set to true in Initialize when our exe file name has a drive type of
// DRIVE_REMOTE, so that we can tailor the sandbox policy as some settings break
// fundamental things when running from a network drive. We default to false in
// case those checks fail as that gives us the strongest policy.
bool SandboxBroker::sRunningFromNetworkDrive = false;

// Cached special directories used for adding policy rules.
static StaticAutoPtr<nsString> sBinDir;
static StaticAutoPtr<nsString> sProfileDir;
static StaticAutoPtr<nsString> sWindowsProfileDir;
static StaticAutoPtr<nsString> sLocalAppDataDir;
static StaticAutoPtr<nsString> sRoamingAppDataDir;
static StaticAutoPtr<nsString> sSystemFontsDir;
static StaticAutoPtr<nsString> sWindowsSystemDir;
static StaticAutoPtr<nsString> sLocalAppDataLowDir;
static StaticAutoPtr<nsString> sLocalAppDataLowParentDir;
#ifdef ENABLE_SYSTEM_EXTENSION_DIRS
static StaticAutoPtr<nsString> sUserExtensionsDir;
#endif

LazyLogModule sSandboxBrokerLog("SandboxBroker");

#define LOG_E(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Error, (__VA_ARGS__))
#define LOG_W(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Warning, (__VA_ARGS__))
#define LOG_D(...) MOZ_LOG(sSandboxBrokerLog, LogLevel::Debug, (__VA_ARGS__))

// Used to store whether we have accumulated an error combination for this
// session.
static StaticAutoPtr<nsTHashtable<nsCStringHashKey>> sLaunchErrors;

// This helper function is our version of SandboxWin::AddWin32kLockdownConfig
// of Chromium, making sure the MITIGATION_WIN32K_DISABLE flag is set before
// adding the SUBSYS_WIN32K_LOCKDOWN rule which is required by
// PolicyBase::AddRuleInternal.
static sandbox::ResultCode AddWin32kLockdownConfig(
    sandbox::TargetConfig* aConfig) {
  sandbox::MitigationFlags flags = aConfig->GetProcessMitigations();
  MOZ_ASSERT(flags,
             "Mitigations should be set before AddWin32kLockdownConfig.");
  MOZ_ASSERT(!(flags & sandbox::MITIGATION_WIN32K_DISABLE),
             "Check not enabling twice.  Should not happen.");

  flags |= sandbox::MITIGATION_WIN32K_DISABLE;
  sandbox::ResultCode result = aConfig->SetProcessMitigations(flags);
  if (result != sandbox::SBOX_ALL_OK) {
    return result;
  }

  result = aConfig->SetFakeGdiInit();
  if (result != sandbox::SBOX_ALL_OK) {
    return result;
  }

  return result;
}

static void CacheAndStandardizeDir(const nsAString& aDir,
                                   StaticAutoPtr<nsString>& aCacheVar) {
  MOZ_ASSERT(!aCacheVar);
  aCacheVar = new nsString(aDir);

  // Convert network share path to format for sandbox policy.
  if (Substring(*aCacheVar, 0, 2).Equals(u"\\\\"_ns)) {
    aCacheVar->InsertLiteral(u"??\\UNC", 1);
  }
}

/* static */
void SandboxBroker::Initialize(sandbox::BrokerServices* aBrokerServices,
                               const nsAString& aBinDir) {
  sBrokerService = aBrokerServices;

  sRunningFromNetworkDrive = widget::WinUtils::RunningFromANetworkDrive();

  if (!aBinDir.IsEmpty()) {
    CacheAndStandardizeDir(aBinDir, sBinDir);
  }

  // Clear statics on shutdown.
  RunOnShutdown([] {
    sLaunchErrors = nullptr;
    sBinDir = nullptr;
    sProfileDir = nullptr;
    sWindowsProfileDir = nullptr;
    sLocalAppDataDir = nullptr;
    sRoamingAppDataDir = nullptr;
    sSystemFontsDir = nullptr;
    sWindowsSystemDir = nullptr;
    sLocalAppDataLowDir = nullptr;
    sLocalAppDataLowParentDir = nullptr;
#ifdef ENABLE_SYSTEM_EXTENSION_DIRS
    sUserExtensionsDir = nullptr;
#endif
  });
}

static void CacheDirectoryServiceDir(nsIProperties* aDirSvc,
                                     const char* aDirKey,
                                     StaticAutoPtr<nsString>& aCacheVar) {
  nsCOMPtr<nsIFile> dirToCache;
  nsresult rv =
      aDirSvc->Get(aDirKey, NS_GET_IID(nsIFile), getter_AddRefs(dirToCache));
  if (NS_FAILED(rv)) {
    // This can only be an NS_WARNING, because it can fail for xpcshell tests.
    NS_WARNING("Failed to get directory to cache.");
    LOG_E("Failed to get directory to cache, key: %s.", aDirKey);
    return;
  }

  nsAutoString dirPath;
  MOZ_ALWAYS_SUCCEEDS(dirToCache->GetPath(dirPath));
  CacheAndStandardizeDir(dirPath, aCacheVar);
}

template <typename TC>
static void AddCachedDirRule(TC* aConfig, sandbox::FileSemantics aAccess,
                             const StaticAutoPtr<nsString>& aBaseDir,
                             const nsLiteralString& aRelativePath = u""_ns) {
  if (!aBaseDir) {
    // This can only be an NS_WARNING, because it can null for xpcshell tests.
    NS_WARNING("Tried to add rule with null base dir.");
    LOG_E("Tried to add rule with null base dir. Relative path: %S, Access: %d",
          static_cast<const wchar_t*>(aRelativePath.get()), aAccess);
    return;
  }

  nsAutoString rulePath(*aBaseDir);
  rulePath.Append(aRelativePath);

  sandbox::ResultCode result =
      aConfig->AllowFileAccess(aAccess, rulePath.get());
  if (sandbox::SBOX_ALL_OK != result) {
    NS_ERROR("Failed to add file policy rule.");
    LOG_E("Failed (ResultCode %d) to add %d access to: %S", result, aAccess,
          rulePath.getW());
  }
}

static void EnsureWindowsDirCached(
    GUID aFolderID, StaticAutoPtr<nsString>& aCacheVar, const char* aErrMsg,
    StaticAutoPtr<nsString>* aParentCacheVar = nullptr) {
  if (aCacheVar) {
    return;
  }

  UniquePtr<wchar_t, mozilla::CoTaskMemFreeDeleter> dirPath;
  if (FAILED(::SHGetKnownFolderPath(aFolderID, 0, nullptr,
                                    getter_Transfers(dirPath)))) {
    NS_ERROR(aErrMsg);
    LOG_E("%s", aErrMsg);
    return;
  }

  nsDependentString dirString(dirPath.get());
  CacheAndStandardizeDir(dirString, aCacheVar);
  if (aParentCacheVar) {
    nsCOMPtr<nsIFile> dirFile;
    nsCOMPtr<nsIFile> parentDir;
    if (NS_FAILED(NS_NewLocalFile(dirString, getter_AddRefs(dirFile))) ||
        NS_FAILED(dirFile->GetParent(getter_AddRefs(parentDir)))) {
      NS_WARNING("Failed to get parent directory to cache.");
      LOG_E("%s parent", aErrMsg);
      return;
    }

    nsString parentPath;
    MOZ_ALWAYS_SUCCEEDS(parentDir->GetPath(parentPath));
    CacheAndStandardizeDir(parentPath, *aParentCacheVar);
  }
}

template <typename TC>
static void AddCachedWindowsDirRule(
    TC* aConfig, sandbox::FileSemantics aAccess, GUID aFolderID,
    const nsLiteralString& aRelativePath = u""_ns) {
  if (aFolderID == FOLDERID_Fonts) {
    EnsureWindowsDirCached(FOLDERID_Fonts, sSystemFontsDir,
                           "Failed to get Windows Fonts folder");
    AddCachedDirRule(aConfig, aAccess, sSystemFontsDir, aRelativePath);
    return;
  }
  if (aFolderID == FOLDERID_System) {
    EnsureWindowsDirCached(FOLDERID_System, sWindowsSystemDir,
                           "Failed to get Windows System folder");
    AddCachedDirRule(aConfig, aAccess, sWindowsSystemDir, aRelativePath);
    return;
  }
  if (aFolderID == FOLDERID_LocalAppData) {
    EnsureWindowsDirCached(FOLDERID_LocalAppData, sLocalAppDataDir,
                           "Failed to get Windows LocalAppData folder");
    AddCachedDirRule(aConfig, aAccess, sLocalAppDataDir, aRelativePath);
    return;
  }
  if (aFolderID == FOLDERID_LocalAppDataLow) {
    // For LocalAppDataLow we also require the parent dir.
    EnsureWindowsDirCached(FOLDERID_LocalAppDataLow, sLocalAppDataLowDir,
                           "Failed to get Windows LocalAppDataLow folder",
                           &sLocalAppDataLowParentDir);
    AddCachedDirRule(aConfig, aAccess, sLocalAppDataLowDir, aRelativePath);
    return;
  }
  if (aFolderID == FOLDERID_RoamingAppData) {
    EnsureWindowsDirCached(FOLDERID_RoamingAppData, sRoamingAppDataDir,
                           "Failed to get Windows RoamingAppData folder");
    AddCachedDirRule(aConfig, aAccess, sRoamingAppDataDir, aRelativePath);
    return;
  }
  if (aFolderID == FOLDERID_Profile) {
    EnsureWindowsDirCached(FOLDERID_Profile, sWindowsProfileDir,
                           "Failed to get Windows Profile folder");
    AddCachedDirRule(aConfig, aAccess, sWindowsProfileDir, aRelativePath);
    return;
  }

  MOZ_CRASH("Unhandled FOLDERID guid.");
}

/* static */
void SandboxBroker::GeckoDependentInitialize() {
  MOZ_ASSERT(NS_IsMainThread());

  // Cache directory paths for use in policy rules, because the directory
  // service must be called on the main thread.
  nsresult rv;
  nsCOMPtr<nsIProperties> dirSvc =
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    MOZ_ASSERT(false,
               "Failed to get directory service, cannot cache directories "
               "for rules.");
    LOG_E(
        "Failed to get directory service, cannot cache directories for "
        "rules.");
    return;
  }

  CacheDirectoryServiceDir(dirSvc, NS_APP_USER_PROFILE_50_DIR, sProfileDir);
#ifdef ENABLE_SYSTEM_EXTENSION_DIRS
  CacheDirectoryServiceDir(dirSvc, XRE_USER_SYS_EXTENSION_DIR,
                           sUserExtensionsDir);
#endif
}

SandboxBroker::SandboxBroker() {
  if (sBrokerService) {
    mPolicy = sBrokerService->CreatePolicy();
    if (sRunningFromNetworkDrive) {
      mPolicy->GetConfig()->SetDoNotUseRestrictingSIDs();
    }
  }
}

#define WSTRING(STRING) L"" STRING

static void AddMozLogRulesToConfig(sandbox::TargetConfig* aConfig,
                                   const base::EnvironmentMap& aEnvironment) {
  auto it = aEnvironment.find(ENVIRONMENT_LITERAL("MOZ_LOG_FILE"));
  if (it == aEnvironment.end()) {
    it = aEnvironment.find(ENVIRONMENT_LITERAL("NSPR_LOG_FILE"));
  }
  if (it == aEnvironment.end()) {
    return;
  }

  char const* logFileModules = getenv("MOZ_LOG");
  if (!logFileModules) {
    return;
  }

  // MOZ_LOG files have a standard file extension appended.
  std::wstring logFileName(it->second);
  logFileName.append(WSTRING(MOZ_LOG_FILE_EXTENSION));

  // Allow for rotation number if rotate is on in the MOZ_LOG settings.
  bool rotate = false;
  NSPRLogModulesParser(
      logFileModules,
      [&rotate](const char* aName, LogLevel aLevel, int32_t aValue) {
        if (strcmp(aName, "rotate") == 0) {
          // Less or eq zero means to turn rotate off.
          rotate = aValue > 0;
        }
      });
  if (rotate) {
    logFileName.append(L".?");
  }

  // Allow for %PID token in the filename. We don't allow it in the dir path, if
  // specified, because we have to use a wildcard as we don't know the PID yet.
  auto pidPos = logFileName.find(WSTRING(MOZ_LOG_PID_TOKEN));
  auto lastSlash = logFileName.find_last_of(L"/\\");
  if (pidPos != std::wstring::npos &&
      (lastSlash == std::wstring::npos || lastSlash < pidPos)) {
    logFileName.replace(pidPos, strlen(MOZ_LOG_PID_TOKEN), L"*");
  }

  auto result = aConfig->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                         logFileName.c_str());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_WARNING("Failed to add rule for MOZ_LOG files.");
    LOG_W("Failed (ResultCode %d) to add rule for MOZ_LOG files", result);
  }
}

static void AddDeveloperRepoDirToConfig(sandbox::TargetConfig* aConfig) {
  const wchar_t* developer_repo_dir =
      _wgetenv(WSTRING("MOZ_DEVELOPER_REPO_DIR"));
  if (!developer_repo_dir) {
    return;
  }

  std::wstring repoPath(developer_repo_dir);
  std::replace(repoPath.begin(), repoPath.end(), '/', '\\');
  repoPath.append(WSTRING("\\*"));

  auto result = aConfig->AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                         repoPath.c_str());
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add rule for developer repo dir.");
    LOG_E("Failed (ResultCode %d) to add read access to developer repo dir",
          result);
  }

  // The following is required if the process is using a USER_RESTRICTED or
  // lower access token level.
  result = aConfig->AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                    L"\\??\\MountPointManager");
  if (result != sandbox::SBOX_ALL_OK) {
    NS_ERROR("Failed to add rule for MountPointManager.");
    LOG_E("Failed (ResultCode %d) to add read access to MountPointManager",
          result);
  }
}

#if defined(MOZ_PROFILE_GENERATE)
// It should only be allowed on instrumented builds, never on production
// builds.
static void AddLLVMProfilePathDirectoryToPolicy(
    sandbox::TargetConfig* aConfig) {
  std::wstring parentPath;
  if (GetLlvmProfileDir(parentPath)) {
    (void)aConfig->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   parentPath.c_str());
  }
}
#endif

#undef WSTRING

static void EnsureAppLockerAccess(sandbox::TargetConfig* aConfig) {
  // At USER_LIMITED and above AppLocker is not blocked.
  if (aConfig->GetLockdownTokenLevel() >= sandbox::USER_LIMITED) {
    return;
  }

  // The ntdll check SaferpIsV2PolicyPresent reads from this key.
  auto result = aConfig->AllowRegistryRead(
      LR"(HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Srp\GP\)");
  if (sandbox::SBOX_ALL_OK != result) {
    NS_ERROR(R"(Failed to add rule for Srp\GP.)");
    LOG_E(R"(Failed (ResultCode %d) to add read access to Srp\GP)", result);
  }

  // When AppLocker is deployed via Mobile Device Management, without this
  // rule SaferpIsV2PolicyPresent silently fails to detect AppLocker, causing
  // the AppLocker check to be bypassed entirely.
  AddCachedWindowsDirRule(aConfig, sandbox::FileSemantics::kAllowReadonly,
                          FOLDERID_System, uR"(\AppLocker\MDM)"_ns);

  // Read access to this device is required to make the AppLocker ioctl call.
  result = aConfig->AllowFileAccess(sandbox::FileSemantics::kAllowReadonly,
                                    LR"(\Device\SrpDevice)");
  if (sandbox::SBOX_ALL_OK != result) {
    NS_ERROR("Failed to add rule for SrpDevice.");
    LOG_E("Failed (ResultCode %d) to add read access to SrpDevice", result);
  }
}

Result<Ok, mozilla::ipc::LaunchError> SandboxBroker::LaunchApp(
    const wchar_t* aPath, const wchar_t* aArguments,
    base::EnvironmentMap& aEnvironment, GeckoProcessType aProcessType,
    const bool aEnableLogging, const IMAGE_THUNK_DATA* aCachedNtdllThunk,
    void** aProcessHandle) {
  if (!sBrokerService) {
    return Err(mozilla::ipc::LaunchError("SB::LA::sBrokerService"));
  }

  if (!mPolicy) {
    return Err(mozilla::ipc::LaunchError("SB::LA::mPolicy"));
  }

  // Set stdout and stderr, to allow inheritance for logging.
  mPolicy->SetStdoutHandle(::GetStdHandle(STD_OUTPUT_HANDLE));
  mPolicy->SetStderrHandle(::GetStdHandle(STD_ERROR_HANDLE));

  auto* config = mPolicy->GetConfig();

  // If we're running from a network drive then we can't block loading from
  // remote locations. Strangely using MITIGATION_IMAGE_LOAD_NO_LOW_LABEL in
  // this situation also means the process fails to start (bug 1423296).
  if (sRunningFromNetworkDrive) {
    sandbox::MitigationFlags mitigations = config->GetProcessMitigations();
    mitigations &= ~(sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
                     sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL);
    MOZ_RELEASE_ASSERT(
        config->SetProcessMitigations(mitigations) == sandbox::SBOX_ALL_OK,
        "Setting the reduced set of flags should always succeed");
  }

  sandbox::MitigationFlags delayedMitigations =
      config->GetDelayedProcessMitigations();

  // Only prefer loading from the system directory as a delayed mitigation, and
  // always enable this delayed mitigation. This means that:
  //  - if the launcher or browser process chose to apply the mitigation, child
  //    processes will have it enabled at startup automatically anyway;
  //  - even if the launcher or browser process chose not to apply the
  //    mitigation, at least sandboxed child processes will run with the
  //    mitigation once the sandbox starts (by this time, they will already
  //    have loaded the Visual C++ runtime DLLs, so these are no longer a
  //    concern; also, although some sandboxed child processes can start new
  //    processes, they never start new *Firefox* processes).
  // Refer to EnablePreferLoadFromSystem32IfCompatible for more details.
  MOZ_ASSERT(!(config->GetProcessMitigations() &
               sandbox::MITIGATION_IMAGE_LOAD_PREFER_SYS32));
  delayedMitigations |= sandbox::MITIGATION_IMAGE_LOAD_PREFER_SYS32;

  // Bug 1936749: MpDetours.dll injection is incompatible with ACG.
  constexpr sandbox::MitigationFlags kDynamicCodeFlags =
      sandbox::MITIGATION_DYNAMIC_CODE_DISABLE |
      sandbox::MITIGATION_DYNAMIC_CODE_DISABLE_WITH_OPT_OUT;
  if ((delayedMitigations & kDynamicCodeFlags) &&
      ::GetModuleHandleW(L"MpDetours.dll")) {
    delayedMitigations &= ~kDynamicCodeFlags;
  }

  SANDBOX_SUCCEED_OR_CRASH(
      config->SetDelayedProcessMitigations(delayedMitigations));

  EnsureAppLockerAccess(config);

  // If logging enabled, set up the policy.
  if (aEnableLogging) {
    ApplyLoggingConfig();
  }

#if defined(DEBUG)
  // Allow write access to TEMP directory in debug builds for logging purposes.
  // The path from GetTempPathW can have a length up to MAX_PATH + 1, including
  // the null, so we need MAX_PATH + 2, so we can add an * to the end.
  wchar_t tempPath[MAX_PATH + 2];
  uint32_t pathLen = ::GetTempPathW(MAX_PATH + 1, tempPath);
  if (pathLen > 0) {
    // GetTempPath path ends with \ and returns the length without the null.
    tempPath[pathLen] = L'*';
    tempPath[pathLen + 1] = L'\0';
    auto result =
        config->AllowFileAccess(sandbox::FileSemantics::kAllowAny, tempPath);
    if (result != sandbox::SBOX_ALL_OK) {
      NS_WARNING("Failed to add rule for TEMP debug logging.");
      LOG_W("Failed (ResultCode %d) to add rule for TEMP debug logging",
            result);
    }
  }
#endif

  // Enable the child process to write log files when setup
  AddMozLogRulesToConfig(config, aEnvironment);

#if defined(MOZ_PROFILE_GENERATE)
  AddLLVMProfilePathDirectoryToPolicy(config);
#endif

  if (!mozilla::IsPackagedBuild()) {
    AddDeveloperRepoDirToConfig(config);
  }

  // Create the sandboxed process
  PROCESS_INFORMATION targetInfo = {0};
  DWORD last_error = ERROR_SUCCESS;
  sandbox::ResultCode result =
      sBrokerService->SpawnTarget(aPath, aArguments, aEnvironment,
                                  std::move(mPolicy), &last_error, &targetInfo);
  if (sandbox::SBOX_ALL_OK != result) {
    nsAutoCString key;
    key.AppendASCII(XRE_GeckoProcessTypeToString(aProcessType));
    key.AppendLiteral("/0x");
    key.AppendInt(static_cast<uint32_t>(last_error), 16);

    // Only accumulate for each combination once per session.
    if (!sLaunchErrors) {
      sLaunchErrors = new nsTHashtable<nsCStringHashKey>();
    }
    if (!sLaunchErrors->Contains(key)) {
      glean::sandbox::failed_launch_keyed.Get(key).AccumulateSingleSample(
          result);
      sLaunchErrors->PutEntry(key);
    }

    LOG_E("Failed (ResultCode %d) to SpawnTarget with last_error=%lu", result,
          last_error);

    return Err(mozilla::ipc::LaunchError::FromWin32Error("SB::LA::SpawnTarget",
                                                         last_error));
  }

#ifdef MOZ_THUNDERBIRD
  // In Thunderbird, mInitDllBlocklistOOP is null, so InitDllBlocklistOOP would
  // hit MOZ_RELEASE_ASSERT.
  constexpr bool isThunderbird = true;
#else
  constexpr bool isThunderbird = false;
#endif

  if (!isThunderbird &&
      XRE_GetChildProcBinPathType(aProcessType) == BinPathType::Self) {
    RefPtr<DllServices> dllSvc(DllServices::Get());
    LauncherVoidResultWithLineInfo blocklistInitOk =
        dllSvc->InitDllBlocklistOOP(aPath, targetInfo.hProcess,
                                    aCachedNtdllThunk, aProcessType);
    if (blocklistInitOk.isErr()) {
      dllSvc->HandleLauncherError(blocklistInitOk.unwrapErr(),
                                  XRE_GeckoProcessTypeToString(aProcessType));
      LOG_E("InitDllBlocklistOOP failed at %s:%d with HRESULT 0x%08lX",
            blocklistInitOk.unwrapErr().mFile,
            blocklistInitOk.unwrapErr().mLine,
            blocklistInitOk.unwrapErr().mError.AsHResult());
      TerminateProcess(targetInfo.hProcess, 1);
      CloseHandle(targetInfo.hThread);
      CloseHandle(targetInfo.hProcess);
      return Err(mozilla::ipc::LaunchError(
          "InitDllBlocklistOOP",
          blocklistInitOk.unwrapErr().mError.AsHResult()));
    }
  } else {
    // Load the child executable as a datafile so that we can examine its
    // headers without doing a full load with dependencies and such.
    nsModuleHandle moduleHandle(
        ::LoadLibraryExW(aPath, nullptr, LOAD_LIBRARY_AS_DATAFILE));
    if (moduleHandle) {
      nt::CrossExecTransferManager transferMgr(targetInfo.hProcess,
                                               moduleHandle);
      if (!!transferMgr) {
        LauncherVoidResult importsRestored =
            RestoreImportDirectory(aPath, transferMgr);
        if (importsRestored.isErr()) {
          RefPtr<DllServices> dllSvc(DllServices::Get());
          dllSvc->HandleLauncherError(
              importsRestored.unwrapErr(),
              XRE_GeckoProcessTypeToString(aProcessType));
          LOG_E("Failed to restore import directory with HRESULT 0x%08lX",
                importsRestored.unwrapErr().mError.AsHResult());
          TerminateProcess(targetInfo.hProcess, 1);
          CloseHandle(targetInfo.hThread);
          CloseHandle(targetInfo.hProcess);
          return Err(mozilla::ipc::LaunchError(
              "RestoreImportDirectory",
              importsRestored.unwrapErr().mError.AsHResult()));
        }
      }
    }
  }

  // The sandboxed process is started in a suspended state, resume it now that
  // we've set things up.
  ResumeThread(targetInfo.hThread);
  CloseHandle(targetInfo.hThread);

  // Return the process handle to the caller
  *aProcessHandle = targetInfo.hProcess;

  return Ok();
}

// This function caches and returns an array of NT paths of the executable's
// dependent modules.
// If this returns Nothing(), it means the retrieval of the modules failed
// (e.g. when the launcher process is disabled), so the process should not
// enable pre-spawn CIG.
static const Maybe<Vector<const wchar_t*>>& GetPrespawnCigExceptionModules() {
  // The shared section contains a list of dependent modules as a
  // null-delimited string.  We convert it to a string vector and
  // cache it to avoid converting the same data every time.
  static Maybe<Vector<const wchar_t*>> sDependentModules =
      []() -> Maybe<Vector<const wchar_t*>> {
    RefPtr<DllServices> dllSvc(DllServices::Get());
    auto sharedSection = dllSvc->GetSharedSection();
    if (!sharedSection) {
      return Nothing();
    }

    return sharedSection->GetDependentModules();
  }();

  return sDependentModules;
}

static sandbox::ResultCode AllowProxyLoadFromBinDir(
    sandbox::TargetConfig* aConfig) {
  // Allow modules in the directory containing the executable such as
  // mozglue.dll, nss3.dll, etc.
  nsAutoString rulePath(*sBinDir);
  rulePath.Append(u"\\*"_ns);
  return aConfig->AllowExtraDll(rulePath.get());
}

static sandbox::ResultCode AddCigToConfig(
    sandbox::TargetConfig* aConfig, bool aAlwaysProxyBinDirLoading = false) {
  if (StaticPrefs::security_sandbox_cig_prespawn_enabled()) {
    const Maybe<Vector<const wchar_t*>>& exceptionModules =
        GetPrespawnCigExceptionModules();
    if (exceptionModules.isSome()) {
      sandbox::MitigationFlags mitigations = aConfig->GetProcessMitigations();
      MOZ_ASSERT(mitigations,
                 "Mitigations should be set before AddCigToPolicy.");
      MOZ_ASSERT(!(mitigations & sandbox::MITIGATION_FORCE_MS_SIGNED_BINS),
                 "AddCigToPolicy should not be called twice.");

      mitigations |= sandbox::MITIGATION_FORCE_MS_SIGNED_BINS;
      sandbox::ResultCode result = aConfig->SetProcessMitigations(mitigations);
      if (result != sandbox::SBOX_ALL_OK) {
        return result;
      }

      result = AllowProxyLoadFromBinDir(aConfig);
      if (result != sandbox::SBOX_ALL_OK) {
        return result;
      }

      for (const wchar_t* path : exceptionModules.ref()) {
        result = aConfig->AllowExtraDll(path);
        if (result != sandbox::SBOX_ALL_OK) {
          return result;
        }
      }

      return sandbox::SBOX_ALL_OK;
    }
  }

  sandbox::MitigationFlags delayedMitigations =
      aConfig->GetDelayedProcessMitigations();
  MOZ_ASSERT(delayedMitigations,
             "Delayed mitigations should be set before AddCigToPolicy.");
  MOZ_ASSERT(!(delayedMitigations & sandbox::MITIGATION_FORCE_MS_SIGNED_BINS),
             "AddCigToPolicy should not be called twice.");

  delayedMitigations |= sandbox::MITIGATION_FORCE_MS_SIGNED_BINS;
  sandbox::ResultCode result =
      aConfig->SetDelayedProcessMitigations(delayedMitigations);
  if (result != sandbox::SBOX_ALL_OK) {
    return result;
  }

  if (aAlwaysProxyBinDirLoading) {
    result = AllowProxyLoadFromBinDir(aConfig);
  }
  return result;
}

// Returns the most strict dynamic code mitigation flag that is compatible with
// system libraries MSAudDecMFT.dll and msmpeg2vdec.dll. This depends on the
// Windows version and the architecture. See bug 1783223 comment 27.
//
// Use the result with SetDelayedProcessMitigations. Using non-delayed ACG
// results in incompatibility with third-party antivirus software, the Windows
// internal Shim Engine mechanism, parts of our own DLL blocklist code, and
// AddressSanitizer initialization code. See bug 1783223.
static sandbox::MitigationFlags DynamicCodeFlagForSystemMediaLibraries() {
  static auto dynamicCodeFlag = []() {
#ifdef _M_X64
    if (IsWin10CreatorsUpdateOrLater()) {
      return sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;
    }
#endif  // _M_X64

    if (IsWin10AnniversaryUpdateOrLater()) {
      return sandbox::MITIGATION_DYNAMIC_CODE_DISABLE_WITH_OPT_OUT;
    }

    return sandbox::MitigationFlags{};
  }();
  return dynamicCodeFlag;
}

static auto GetProcessUserSidString() {
  std::unique_ptr<wchar_t, LocalFreeDeleter> userSidString;
  std::unique_ptr<HANDLE, CloseHandleDeleter> tokenHandle;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY,
                          getter_Transfers(tokenHandle))) {
    return userSidString;
  }

  BYTE tokenUserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
  DWORD len = sizeof(tokenUserBuffer);
  if (!::GetTokenInformation(tokenHandle.get(), TokenUser, &tokenUserBuffer,
                             len, &len)) {
    return userSidString;
  }

  auto* tokenUser = reinterpret_cast<TOKEN_USER*>(tokenUserBuffer);
  if (!::ConvertSidToStringSidW(tokenUser->User.Sid,
                                getter_Transfers(userSidString))) {
    userSidString.reset();
  }

  return userSidString;
}

// Process fails to start in LPAC with ASan build
#if !defined(MOZ_ASAN)
static void HexEncode(const Span<const uint8_t>& aBytes, nsACString& aEncoded) {
  static const char kHexChars[] = "0123456789abcdef";

  // Each input byte creates two output hex characters.
  char* encodedPtr;
  aEncoded.GetMutableData(&encodedPtr, aBytes.size() * 2);

  for (auto byte : aBytes) {
    *(encodedPtr++) = kHexChars[byte >> 4];
    *(encodedPtr++) = kHexChars[byte & 0xf];
  }
}

// This is left as a void because we might fail to set the permission for some
// reason and yet the LPAC permission is already granted. So returning success
// or failure isn't really that useful.
/* static */
void SandboxBroker::EnsureLpacPermsissionsOnDir(const nsString& aDir) {
  // For MSIX packages we get access through the packageContents capability and
  // we probably won't have access to add the permission either way.
  if (widget::WinUtils::HasPackageIdentity()) {
    return;
  }

  BYTE sidBytes[SECURITY_MAX_SID_SIZE];
  PSID lpacFirefoxInstallFilesSid = static_cast<PSID>(sidBytes);
  if (!sBrokerService->DeriveCapabilitySidFromName(kLpacFirefoxInstallFiles,
                                                   lpacFirefoxInstallFilesSid,
                                                   sizeof(sidBytes))) {
    LOG_E("Failed to derive Firefox install files capability SID.");
    return;
  }

  HANDLE hDir = ::CreateFileW(aDir.get(), WRITE_DAC | READ_CONTROL, 0, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (hDir == INVALID_HANDLE_VALUE) {
    LOG_W("Unable to get directory handle for %s",
          NS_ConvertUTF16toUTF8(aDir).get());
    return;
  }

  UniquePtr<HANDLE, CloseHandleDeleter> autoHandleCloser(hDir);
  PACL pBinDirAcl = nullptr;
  PSECURITY_DESCRIPTOR pSD = nullptr;
  DWORD result =
      ::GetSecurityInfo(hDir, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, &pBinDirAcl, nullptr, &pSD);
  if (result != ERROR_SUCCESS) {
    LOG_E("Failed to get DACL for %s", NS_ConvertUTF16toUTF8(aDir).get());
    return;
  }

  UniquePtr<VOID, LocalFreeDeleter> autoFreeSecDesc(pSD);
  if (!pBinDirAcl) {
    LOG_E("DACL was null for %s", NS_ConvertUTF16toUTF8(aDir).get());
    return;
  }

  for (DWORD i = 0; i < pBinDirAcl->AceCount; ++i) {
    VOID* pAce = nullptr;
    if (!::GetAce(pBinDirAcl, i, &pAce) ||
        static_cast<PACE_HEADER>(pAce)->AceType != ACCESS_ALLOWED_ACE_TYPE) {
      continue;
    }

    auto* pAllowedAce = static_cast<ACCESS_ALLOWED_ACE*>(pAce);
    if ((pAllowedAce->Mask & (GENERIC_READ | GENERIC_EXECUTE)) !=
        (GENERIC_READ | GENERIC_EXECUTE)) {
      continue;
    }

    PSID aceSID = reinterpret_cast<PSID>(&(pAllowedAce->SidStart));
    if (::EqualSid(aceSID, lpacFirefoxInstallFilesSid)) {
      LOG_D("Firefox install files permission found on %s",
            NS_ConvertUTF16toUTF8(aDir).get());
      return;
    }
  }

  EXPLICIT_ACCESS_W newAccess = {0};
  newAccess.grfAccessMode = GRANT_ACCESS;
  newAccess.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
  newAccess.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  ::BuildTrusteeWithSidW(&newAccess.Trustee, lpacFirefoxInstallFilesSid);
  PACL newDacl = nullptr;
  if (ERROR_SUCCESS !=
      ::SetEntriesInAclW(1, &newAccess, pBinDirAcl, &newDacl)) {
    LOG_E("Failed to create new DACL with Firefox install files SID.");
    return;
  }

  UniquePtr<ACL, LocalFreeDeleter> autoFreeAcl(newDacl);
  if (ERROR_SUCCESS != ::SetSecurityInfo(hDir, SE_FILE_OBJECT,
                                         DACL_SECURITY_INFORMATION, nullptr,
                                         nullptr, newDacl, nullptr)) {
    LOG_E("Failed to set new DACL on %s", NS_ConvertUTF16toUTF8(aDir).get());
  }

  LOG_D("Firefox install files permission granted on %s",
        NS_ConvertUTF16toUTF8(aDir).get());
}

static bool IsLowPrivilegedAppContainerSupported() {
  // Chromium doesn't support adding an LPAC before this version due to
  // incompatibility with some process mitigations.
  return IsWin10Sep2018UpdateOrLater();
}

// AddAndConfigureAppContainerProfile deliberately fails if it is called on an
// unsupported version. This is because for some process types the LPAC is
// required to provide a sufficiently strong sandbox. Processes where the use of
// an LPAC is an optional extra should use IsLowPrivilegedAppContainerSupported
// to check support first.
static sandbox::ResultCode AddAndConfigureAppContainerProfile(
    sandbox::TargetConfig* aConfig, const nsAString& aPackagePrefix,
    const nsTArray<base::win::WellKnownCapability>& aWellKnownCapabilites,
    const nsTArray<const wchar_t*>& aNamedCapabilites) {
  // CreateAppContainerProfile requires that the profile name is at most 64
  // characters but 50 on WCOS systems. The size of sha1 is a constant 40,
  // so validate that the base names are sufficiently short that the total
  // length is valid on all systems.
  MOZ_ASSERT(aPackagePrefix.Length() <= 10U,
             "AppContainer Package prefix too long.");

  if (!IsLowPrivilegedAppContainerSupported()) {
    return sandbox::SBOX_ERROR_UNSUPPORTED;
  }

  static nsAutoString uniquePackageStr = []() {
    // userenv.dll may not have been loaded and some of the chromium sandbox
    // AppContainer code assumes that it is. Done here to load once.
    ::LoadLibraryW(L"userenv.dll");

    // Done during the package string initialization so we only do it once.
    SandboxBroker::EnsureLpacPermsissionsOnDir(*sBinDir.get());

    // This mirrors Edge's use of the exe path for the SHA1 hash to give a
    // machine unique name per install.
    nsAutoString ret;
    char exePathBuf[MAX_PATH];
    DWORD pathSize = ::GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH);
    if (!pathSize) {
      return ret;
    }

    SHA1Sum sha1Sum;
    SHA1Sum::Hash sha1Hash;
    sha1Sum.update(exePathBuf, pathSize);
    sha1Sum.finish(sha1Hash);

    nsAutoCString hexEncoded;
    HexEncode(sha1Hash, hexEncoded);
    ret = NS_ConvertUTF8toUTF16(hexEncoded);
    return ret;
  }();

  if (uniquePackageStr.IsEmpty()) {
    return sandbox::SBOX_ERROR_CREATE_APPCONTAINER;
  }

  nsAutoString packageName = aPackagePrefix + uniquePackageStr;
  sandbox::ResultCode result =
      aConfig->AddAppContainerProfile(packageName.get());
  if (result != sandbox::SBOX_ALL_OK) {
    return result;
  }

  // This looks odd, but unfortunately holding a scoped_refptr and
  // dereferencing has DCHECKs that cause a linking problem.
  sandbox::AppContainer* appContainer = aConfig->GetAppContainer();
  appContainer->SetEnableLowPrivilegeAppContainer(true);

  for (auto wkCap : aWellKnownCapabilites) {
    appContainer->AddCapability(wkCap);
  }

  for (auto namedCap : aNamedCapabilites) {
    appContainer->AddCapability(namedCap);
  }

  return sandbox::SBOX_ALL_OK;
}
#endif

void AddShaderCachesToPolicy(sandboxing::SizeTrackingConfig* aConfig,
                             int32_t aSandboxLevel) {
  // The GPU process needs to write to a shader cache for performance reasons
  if (sProfileDir) {
    // Create the shader-cache dir ourselves, so we only need to give read
    // access to it.
    static constexpr auto kShaderCacheDir = u"\\shader-cache"_ns;
    static constexpr auto kShaderCacheDirAnyEntry = u"\\shader-cache\\*"_ns;
    nsAutoString rulePath(*sProfileDir);
    rulePath.Append(kShaderCacheDir);
    if (!::CreateDirectoryW(rulePath.get(), nullptr) &&
        ::GetLastError() != ERROR_ALREADY_EXISTS) {
      LOG_W("Failed to create shader-cache");
    }

    AddCachedDirRule(aConfig, sandbox::FileSemantics::kAllowReadonly,
                     sProfileDir, kShaderCacheDir);

    AddCachedDirRule(aConfig, sandbox::FileSemantics::kAllowAny, sProfileDir,
                     kShaderCacheDirAnyEntry);
  }

  // Add GPU specific shader cache rules.
  const nsCOMPtr<nsIGfxInfo> gfxInfo = components::GfxInfo::Service();
  MOZ_ASSERT(gfxInfo);
  nsAutoString vendorID;
  if (NS_FAILED(gfxInfo->GetAdapterVendorID(vendorID))) {
    NS_WARNING("Failed to get GPU Vendor ID.");
    return;
  }

  if (aSandboxLevel >= 2 && vendorID == widget::GfxDriverInfo::GetDeviceVendor(
                                            widget::DeviceVendor::Intel)) {
    // Add rules to allow Intel's shader cache.
    AddCachedWindowsDirRule(aConfig, sandbox::FileSemantics::kAllowAny,
                            FOLDERID_LocalAppDataLow,
                            u"\\Intel\\ShaderCache\\*"_ns);
    AddCachedWindowsDirRule(aConfig, sandbox::FileSemantics::kAllowQuery,
                            FOLDERID_LocalAppDataLow,
                            u"\\Intel\\ShaderCache"_ns);
    AddCachedWindowsDirRule(aConfig, sandbox::FileSemantics::kAllowQuery,
                            FOLDERID_LocalAppDataLow, u"\\Intel"_ns);
    AddCachedWindowsDirRule(aConfig, sandbox::FileSemantics::kAllowQuery,
                            FOLDERID_LocalAppDataLow);

    // The parent of LocalAppDataLow is cached by AddCachedWindowsDirRule.
    if (sLocalAppDataLowParentDir) {
      AddCachedDirRule(aConfig, sandbox::FileSemantics::kAllowQuery,
                       sLocalAppDataLowParentDir);
    }
  }
}

void SandboxBroker::SetSecurityLevelForContentProcess(int32_t aSandboxLevel,
                                                      bool aIsFileProcess) {
  MOZ_RELEASE_ASSERT(mPolicy, "mPolicy must be set before this call.");

  sandbox::JobLevel jobLevel;
  sandbox::TokenLevel accessTokenLevel;
  sandbox::IntegrityLevel initialIntegrityLevel;
  sandbox::IntegrityLevel delayedIntegrityLevel;

  auto* config = mPolicy->GetConfig();

  // The setting of these levels is pretty arbitrary, but they are a useful (if
  // crude) tool while we are tightening the policy. Gaps are left to try and
  // avoid changing their meaning.
  MOZ_RELEASE_ASSERT(aSandboxLevel >= 1,
                     "Should not be called with aSandboxLevel < 1");
  if (aSandboxLevel >= 20) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_LOCKDOWN;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_UNTRUSTED;
  } else if (aSandboxLevel >= 9) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_LOCKDOWN_WITH_TRAVERSE;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_UNTRUSTED;
  } else if (aSandboxLevel >= 8) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_RESTRICTED;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_UNTRUSTED;
  } else if (aSandboxLevel >= 7) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_LIMITED;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_UNTRUSTED;
  } else if (aSandboxLevel >= 4) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_LIMITED;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
  } else if (aSandboxLevel >= 3) {
    jobLevel = sandbox::JobLevel::kLockdown;
    accessTokenLevel = sandbox::USER_LIMITED;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
  } else if (aSandboxLevel == 2) {
    jobLevel = sandbox::JobLevel::kInteractive;
    accessTokenLevel = sandbox::USER_INTERACTIVE;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
  } else {
    MOZ_ASSERT(aSandboxLevel == 1);

    jobLevel = sandbox::JobLevel::kUnprotected;
    accessTokenLevel = sandbox::USER_RESTRICTED_NON_ADMIN;
    initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
  }

  // If the process will handle file: URLs, don't allow settings that
  // block reads.
  if (aIsFileProcess) {
    if (accessTokenLevel < sandbox::USER_RESTRICTED_NON_ADMIN) {
      accessTokenLevel = sandbox::USER_RESTRICTED_NON_ADMIN;
    }
    if (delayedIntegrityLevel > sandbox::INTEGRITY_LEVEL_LOW) {
      delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
    }
  }

#if defined(DEBUG)
  // This is required for a MOZ_ASSERT check in WindowsMessageLoop.cpp
  // WinEventHook, see bug 1366694 for details.
  DWORD uiExceptions = JOB_OBJECT_UILIMIT_HANDLES;
#else
  DWORD uiExceptions = 0;
#endif
  sandbox::ResultCode result = config->SetJobLevel(jobLevel, uiExceptions);
  MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                     "Setting job level failed, have you set memory limit when "
                     "jobLevel == JOB_NONE?");

  result = config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                                 accessTokenLevel);
  MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                     "Lockdown level cannot be USER_UNPROTECTED or USER_LAST "
                     "if initial level was USER_RESTRICTED_SAME_ACCESS");

  result = config->SetIntegrityLevel(initialIntegrityLevel);
  MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                     "SetIntegrityLevel should never fail, what happened?");
  config->SetDelayedIntegrityLevel(delayedIntegrityLevel);

  if (aSandboxLevel > 5) {
    config->SetLockdownDefaultDacl();
    config->AddRestrictingRandomSid();
  }

  if (aSandboxLevel > 4) {
    config->SetDesktop(sandbox::Desktop::kAlternateWinstation);
  }

  if (StaticPrefs::security_sandbox_content_close_ksecdd_handle()) {
    // bug 2006941 and bug 2008739 and bug 2022584 - Trellix DLP uses these
    // functions, so don't do this if their DLLs are loaded
    bool isTrellixDllLoaded;
#if defined(_M_X64)
    isTrellixDllLoaded = !!::GetModuleHandleW(L"fcagff64.dll") ||
                         !!::GetModuleHandleW(L"fcagff64hc.dll");
#elif defined(_M_ARM64)
    isTrellixDllLoaded = !!::GetModuleHandleW(L"fcagffarm64hc.dll");
#else
    isTrellixDllLoaded = !!::GetModuleHandleW(L"fcagff.dll");
#endif
    if (!isTrellixDllLoaded) {
      config->AddKernelObjectToClose(sandbox::HandleToClose::kKsecDD);
    }
  }

  sandbox::MitigationFlags mitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_DEP_NO_ATL_THUNK |
      sandbox::MITIGATION_DEP | sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL;

#if defined(_M_ARM64)
  // Disable CFG on older versions of ARM64 Windows to avoid a crash in COM.
  if (!IsWin10Sep2018UpdateOrLater()) {
    mitigations |= sandbox::MITIGATION_CONTROL_FLOW_GUARD_DISABLE;
  }
#endif

  if (StaticPrefs::security_sandbox_content_shadow_stack_enabled()) {
    mitigations |= sandbox::MITIGATION_CET_COMPAT_MODE;
  }

  result = config->SetProcessMitigations(mitigations);
  MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                     "Invalid flags for SetProcessMitigations.");

  nsIXULRuntime::ContentWin32kLockdownState win32kLockdownState =
      GetContentWin32kLockdownState();

  LOG_W("Win32k Lockdown State: '%s'",
        ContentWin32kLockdownStateToString(win32kLockdownState));

  if (GetContentWin32kLockdownEnabled()) {
    result = AddWin32kLockdownConfig(config);
    MOZ_RELEASE_ASSERT(result == sandbox::SBOX_ALL_OK,
                       "Failed to add the win32k lockdown config");
  }

  mitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                sandbox::MITIGATION_DLL_SEARCH_ORDER;

  result = config->SetDelayedProcessMitigations(mitigations);
  MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                     "Invalid flags for SetDelayedProcessMitigations.");

  // We still have edge cases where the child at low integrity can't read some
  // files, so add a rule to allow read access to everything when required.
  if (aSandboxLevel == 1 || aIsFileProcess) {
    result =
        config->AllowFileAccess(sandbox::FileSemantics::kAllowReadonly, L"*");
    MOZ_RELEASE_ASSERT(sandbox::SBOX_ALL_OK == result,
                       "With these static arguments AddRule should never fail, "
                       "what happened?");
  } else {
    // Add rule to allow access to user specific fonts.
    AddCachedWindowsDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                            FOLDERID_LocalAppData,
                            u"\\Microsoft\\Windows\\Fonts\\*"_ns);

    // Add rule to allow read access to installation directory.
    AddCachedDirRule(config, sandbox::FileSemantics::kAllowReadonly, sBinDir,
                     u"\\*"_ns);

    // Add rule to allow read access to the chrome directory within profile.
    AddCachedDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                     sProfileDir, u"\\chrome\\*"_ns);

    // Add rule to allow read access to the extensions directory within profile.
    AddCachedDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                     sProfileDir, u"\\extensions\\*"_ns);

#ifdef ENABLE_SYSTEM_EXTENSION_DIRS
    // Add rule to allow read access to the per-user extensions directory.
    AddCachedDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                     sUserExtensionsDir, u"\\*"_ns);
#endif
  }

  // Add the policy for the client side of the crash server pipe.
  result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   L"\\??\\pipe\\gecko-crash-server-pipe.*");
  MOZ_RELEASE_ASSERT(
      sandbox::SBOX_ALL_OK == result,
      "With these static arguments AddRule should never fail, what happened?");

  if (aSandboxLevel >= 8) {
    // Content process still needs to be able to read fonts.
    AddCachedWindowsDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                            FOLDERID_Fonts);
    AddCachedWindowsDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                            FOLDERID_Fonts, u"\\*"_ns);

    // Add access to Windows system binary dir to allow DLLs that are not
    // required in all content processes to load later.
    AddCachedWindowsDirRule(config, sandbox::FileSemantics::kAllowReadonly,
                            FOLDERID_System, u"\\*"_ns);

    // USER_RESTRCITED will also block access to the KnownDlls list, so we force
    // that path to fall-back to the normal loading path.
    config->SetForceKnownDllLoadingFallback();

    // If platform video encoding is not remote it requires the KsecDD device.
    if (!StaticPrefs::media_use_remote_encoder_video_platform()) {
      result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                       LR"(\Device\KsecDD)");
      if (sandbox::SBOX_ALL_OK != result) {
        NS_ERROR("Failed to add rule for KsecDD.");
        LOG_E("Failed (ResultCode %d) to add read access to KsecDD", result);
      }
    }

    // We should be able to remove access to these media registry keys below
    // once encoding has moved out of the content process (bug 1972552).

    // Read access for MF Media Source Activate and subkeys/values.
    result = config->AllowRegistryRead(
        L"HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID"
        L"\\{e79167d7-1b85-4d78-b603-798e0e1a4c67}*");
    if (sandbox::SBOX_ALL_OK != result) {
      NS_ERROR("Failed to add rule for MFStartup CLSID.");
      LOG_E("Failed (ResultCode %d) to add rule for MFStartup CLSID.", result);
    }

    // Read access for other Media Foundation Classes.
    result = config->AllowRegistryRead(
        L"HKEY_LOCAL_MACHINE\\Software\\Classes\\MediaFoundation\\*");
    if (sandbox::SBOX_ALL_OK != result) {
      NS_ERROR("Failed to add rule for MFStartup CLSID.");
      LOG_E("Failed (ResultCode %d) to add rule for MFStartup CLSID.", result);
    }

    // Read access for MF H264 Encoder and subkeys/values.
    result = config->AllowRegistryRead(
        L"HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID"
        L"\\{6CA50344-051A-4DED-9779-A43305165E35}*");
    if (sandbox::SBOX_ALL_OK != result) {
      NS_ERROR("Failed to add rule for MF H264 Encoder CLSID.");
      LOG_E("Failed (ResultCode %d) to add rule for MF H264 Encoder CLSID.",
            result);
    }

#if !defined(_WIN64)
    BOOL isWow64Process;
    if (::IsWow64Process(::GetCurrentProcess(), &isWow64Process) &&
        isWow64Process) {
      // Read access for other Media Foundation Classes for WOW64.
      result = config->AllowRegistryRead(
          L"HKEY_LOCAL_MACHINE\\"
          L"Software\\Classes\\WOW6432Node\\MediaFoundation\\*");
      if (sandbox::SBOX_ALL_OK != result) {
        NS_ERROR("Failed to add rule for MFStartup CLSID.");
        LOG_E("Failed (ResultCode %d) to add rule for MFStartup CLSID.",
              result);
      }

      // Read access for MF H264 Encoder and subkeys/values for WOW64.
      result = config->AllowRegistryRead(
          L"HKEY_LOCAL_MACHINE\\Software\\Classes\\WOW6432Node\\CLSID"
          L"\\{6CA50344-051A-4DED-9779-A43305165E35}*");
      if (sandbox::SBOX_ALL_OK != result) {
        NS_ERROR("Failed to add rule for MF H264 Encoder CLSID.");
        LOG_E("Failed (ResultCode %d) to add rule for MF H264 Encoder CLSID.",
              result);
      }
    }
#endif
  }

  if (aSandboxLevel >= 9) {
    // Before reading the media registry keys (specified for aSandboxLevel >= 8)
    // the user's Classes key is read. We lose access to this at
    // USER_LOCKDOWN_WITH_TRAVERSE because we no longer have the Restricted SID.
    // We should be able to remove this once encoding has moved out of the
    // content process (bug 1972552).
    auto userSidString = GetProcessUserSidString();
    if (userSidString) {
      std::wstring userClassKeyName(L"HKEY_USERS\\");
      userClassKeyName += userSidString.get();
      userClassKeyName += L"_Classes";
      result = config->AllowRegistryRead(userClassKeyName.c_str());
      if (sandbox::SBOX_ALL_OK != result) {
        NS_ERROR("Failed to add rule for user's Classes.");
        LOG_E("Failed (ResultCode %d) to add rule for user's Classes.", result);
      }
    } else {
      NS_ERROR("Failed to get user's SID.");
      LOG_E("Failed to get user's SID. %lx", ::GetLastError());
    }

    // Required for GetUserGeoID, which is used to get timezone information.
    bool geoRuleSet =
        config->AllowRegistryRead(L"HKEY_CURRENT_USER") ==
            sandbox::SBOX_ALL_OK &&
        config->AllowRegistryRead(
            L"HKEY_CURRENT_USER\\Control Panel\\International\\Geo") ==
            sandbox::SBOX_ALL_OK;
    if (!geoRuleSet) {
      NS_ERROR("Failed to add rule for International Geo.");
      LOG_E("Failed (ResultCode %d) to add rule for International Geo.",
            result);
    }
  }
}

void SandboxBroker::SetSecurityLevelForGPUProcess(int32_t aSandboxLevel) {
  MOZ_RELEASE_ASSERT(mPolicy, "mPolicy must be set before this call.");
  MOZ_RELEASE_ASSERT(aSandboxLevel >= 1);

  sandbox::TokenLevel initialTokenLevel = sandbox::USER_RESTRICTED_SAME_ACCESS;
  sandbox::TokenLevel lockdownTokenLevel =
      (aSandboxLevel >= 2) ? sandbox::USER_LIMITED
                           : sandbox::USER_RESTRICTED_NON_ADMIN;

  sandbox::IntegrityLevel initialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;
  sandbox::IntegrityLevel delayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LOW;

  sandbox::JobLevel jobLevel = sandbox::JobLevel::kLimitedUser;

  uint32_t uiExceptions =
      JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_DESKTOP |
      JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS;

  sandbox::MitigationFlags initialMitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_DEP_NO_ATL_THUNK |
      sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL | sandbox::MITIGATION_DEP;

  if (StaticPrefs::security_sandbox_gpu_shadow_stack_enabled()) {
    initialMitigations |= sandbox::MITIGATION_CET_COMPAT_MODE;
  }

  sandbox::MitigationFlags delayedMitigations =
      sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
      sandbox::MITIGATION_DLL_SEARCH_ORDER;

  auto* config = mPolicy->GetConfig();

  SANDBOX_SUCCEED_OR_CRASH(config->SetJobLevel(jobLevel, uiExceptions));
  SANDBOX_SUCCEED_OR_CRASH(
      config->SetTokenLevel(initialTokenLevel, lockdownTokenLevel));
  SANDBOX_SUCCEED_OR_CRASH(config->SetIntegrityLevel(initialIntegrityLevel));
  config->SetDelayedIntegrityLevel(delayedIntegrityLevel);
  SANDBOX_SUCCEED_OR_CRASH(config->SetProcessMitigations(initialMitigations));
  SANDBOX_SUCCEED_OR_CRASH(
      config->SetDelayedProcessMitigations(delayedMitigations));

  config->SetLockdownDefaultDacl();
  config->AddRestrictingRandomSid();

  // Policy wrapper to keep track of available rule space. We allow two spare
  // pages for generic process rules and to allow for padding that occurs in
  // LowLevelPolicy::Done. See bug 2009140.
  // Note that we plan to move to a single font access rule in bug 2002995. This
  // will remove the need for individual rules and mean that we can reduce
  // sandbox::kPolMemPageCount.
  sandboxing::SizeTrackingConfig trackingConfig(config,
                                                sandbox::kPolMemPageCount - 2);

  // Add the policy for the client side of the crash server pipe.
  SANDBOX_SUCCEED_OR_CRASH(
      trackingConfig.AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                     L"\\??\\pipe\\gecko-crash-server-pipe.*"));

  // Add rule to allow read access to installation directory.
  AddCachedDirRule(&trackingConfig, sandbox::FileSemantics::kAllowReadonly,
                   sBinDir, u"\\*"_ns);

  AddShaderCachesToPolicy(&trackingConfig, aSandboxLevel);

  if (aSandboxLevel >= 2) {
    // We don't want to add a rule directly here but use the same retrieval and
    // caching mechanism to get the Windows user's dirs.
    EnsureWindowsDirCached(FOLDERID_Profile, sWindowsProfileDir,
                           "Failed to get Windows Profile folder");
    EnsureWindowsDirCached(FOLDERID_LocalAppData, sLocalAppDataDir,
                           "Failed to get Windows LocalAppData folder");
    EnsureWindowsDirCached(FOLDERID_RoamingAppData, sRoamingAppDataDir,
                           "Failed to get Windows RoamingAppData folder");
    if (sWindowsProfileDir && sLocalAppDataDir && sRoamingAppDataDir) {
      sandboxing::UserFontConfigHelper configHelper(
          LR"(Software\Microsoft\Windows NT\CurrentVersion\Fonts)",
          *sWindowsProfileDir, *sLocalAppDataDir, *sRoamingAppDataDir);
      if (!configHelper.AddRules(trackingConfig)) {
        // We've run out of space for font rules, so fall back to
        // USER_INTERACTIVE to maintain access to all user fonts.
        SANDBOX_SUCCEED_OR_CRASH(config->SetTokenLevel(
            initialTokenLevel, sandbox::USER_INTERACTIVE));
      }
    }
  }
}

#define SANDBOX_ENSURE_SUCCESS(result, message)          \
  do {                                                   \
    MOZ_ASSERT(sandbox::SBOX_ALL_OK == result, message); \
    if (sandbox::SBOX_ALL_OK != result) return false;    \
  } while (0)

bool SandboxBroker::SetSecurityLevelForRDDProcess() {
  if (!mPolicy) {
    return false;
  }

  auto* config = mPolicy->GetConfig();

  auto result =
      config->SetJobLevel(sandbox::JobLevel::kLockdown, 0 /* ui_exceptions */);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetJobLevel should never fail with these arguments, what happened?");

  result = config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                                 sandbox::USER_LIMITED);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetTokenLevel should never fail with these arguments, what happened?");

  config->SetDesktop(sandbox::Desktop::kAlternateWinstation);

  result = config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  SANDBOX_ENSURE_SUCCESS(result,
                         "SetIntegrityLevel should never fail with these "
                         "arguments, what happened?");

  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);

  config->SetLockdownDefaultDacl();
  config->AddRestrictingRandomSid();

  sandbox::MitigationFlags mitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_DEP_NO_ATL_THUNK | sandbox::MITIGATION_DEP |
      sandbox::MITIGATION_NONSYSTEM_FONT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL;

  if (StaticPrefs::security_sandbox_rdd_shadow_stack_enabled()) {
    mitigations |= sandbox::MITIGATION_CET_COMPAT_MODE;
  }

  result = config->SetProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result, "Invalid flags for SetProcessMitigations.");

  mitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                sandbox::MITIGATION_DLL_SEARCH_ORDER;

  if (StaticPrefs::security_sandbox_rdd_acg_enabled()) {
    // The RDD process depends on msmpeg2vdec.dll.
    mitigations |= DynamicCodeFlagForSystemMediaLibraries();
  }

  result = config->SetDelayedProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result,
                         "Invalid flags for SetDelayedProcessMitigations.");

  result = AddCigToConfig(config);
  SANDBOX_ENSURE_SUCCESS(result, "Failed to initialize signed policy rules.");

  // Add the policy for the client side of the crash server pipe.
  result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   L"\\??\\pipe\\gecko-crash-server-pipe.*");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  return true;
}

bool SandboxBroker::SetSecurityLevelForSocketProcess() {
  if (!mPolicy) {
    return false;
  }

  auto* config = mPolicy->GetConfig();

  auto result =
      config->SetJobLevel(sandbox::JobLevel::kLockdown, 0 /* ui_exceptions */);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetJobLevel should never fail with these arguments, what happened?");

  result = config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS,
                                 sandbox::USER_LIMITED);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetTokenLevel should never fail with these arguments, what happened?");

  config->SetDesktop(sandbox::Desktop::kAlternateWinstation);

  result = config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  SANDBOX_ENSURE_SUCCESS(result,
                         "SetIntegrityLevel should never fail with these "
                         "arguments, what happened?");

  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_UNTRUSTED);

  config->SetLockdownDefaultDacl();
  config->AddRestrictingRandomSid();

  sandbox::MitigationFlags mitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_DEP_NO_ATL_THUNK | sandbox::MITIGATION_DEP |
      sandbox::MITIGATION_NONSYSTEM_FONT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL;

  if (StaticPrefs::security_sandbox_socket_shadow_stack_enabled()) {
    mitigations |= sandbox::MITIGATION_CET_COMPAT_MODE;
  }

  result = config->SetProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result, "Invalid flags for SetProcessMitigations.");

  if (StaticPrefs::security_sandbox_socket_win32k_disable()) {
    result = AddWin32kLockdownConfig(config);
    SANDBOX_ENSURE_SUCCESS(result, "Failed to add the win32k lockdown config");
  }

  mitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                sandbox::MITIGATION_DLL_SEARCH_ORDER |
                sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;

  result = config->SetDelayedProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result,
                         "Invalid flags for SetDelayedProcessMitigations.");

  result = AddCigToConfig(config);
  SANDBOX_ENSURE_SUCCESS(result, "Failed to initialize signed policy rules.");

  // Add the policy for the client side of the crash server pipe.
  result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   L"\\??\\pipe\\gecko-crash-server-pipe.*");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  return true;
}

// A strict base sandbox for utility sandboxes to adapt.
struct UtilitySandboxProps {
  sandbox::JobLevel mJobLevel = sandbox::JobLevel::kLockdown;

  sandbox::TokenLevel mInitialTokenLevel = sandbox::USER_RESTRICTED_SAME_ACCESS;
  sandbox::TokenLevel mDelayedTokenLevel = sandbox::USER_LOCKDOWN;

  sandbox::IntegrityLevel mInitialIntegrityLevel =  // before lockdown
      sandbox::INTEGRITY_LEVEL_LOW;
  sandbox::IntegrityLevel mDelayedIntegrityLevel =  // after lockdown
      sandbox::INTEGRITY_LEVEL_UNTRUSTED;

  sandbox::Desktop mDesktop = sandbox::Desktop::kAlternateWinstation;
  bool mLockdownDefaultDacl = true;
  bool mAddRestrictingRandomSid = true;
  bool mUseWin32kLockdown = true;
  bool mUseCig = true;

  sandbox::MitigationFlags mInitialMitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_DEP_NO_ATL_THUNK | sandbox::MITIGATION_DEP |
      sandbox::MITIGATION_NONSYSTEM_FONT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL |
      sandbox::MITIGATION_CET_COMPAT_MODE;

  sandbox::MitigationFlags mExcludedInitialMitigations = 0;

  sandbox::MitigationFlags mDelayedMitigations =
      sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
      sandbox::MITIGATION_DLL_SEARCH_ORDER |
      sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;

  // Low Privileged Application Container settings;
  nsString mPackagePrefix;
  nsTArray<base::win::WellKnownCapability> mWellKnownCapabilites;
  nsTArray<const wchar_t*> mNamedCapabilites;
};

struct GenericUtilitySandboxProps : public UtilitySandboxProps {};

struct UtilityAudioDecodingWmfSandboxProps : public UtilitySandboxProps {
  UtilityAudioDecodingWmfSandboxProps() {
    mDelayedTokenLevel = sandbox::USER_LIMITED;
    mDelayedMitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                          sandbox::MITIGATION_DLL_SEARCH_ORDER;
#ifdef MOZ_WMF
    if (StaticPrefs::security_sandbox_utility_wmf_acg_enabled()) {
      mDelayedMitigations |= DynamicCodeFlagForSystemMediaLibraries();
    }
#else
    mDelayedMitigations |= sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;
#endif  // MOZ_WMF
  }
};

#ifdef MOZ_WMF_MEDIA_ENGINE
struct UtilityMfMediaEngineCdmSandboxProps : public UtilitySandboxProps {
  UtilityMfMediaEngineCdmSandboxProps() {
    mJobLevel = sandbox::JobLevel::kInteractive;
    mInitialTokenLevel = sandbox::USER_UNPROTECTED;
    mDelayedTokenLevel = sandbox::USER_UNPROTECTED;
    mDesktop = sandbox::Desktop::kDefault;
    mLockdownDefaultDacl = false;
    mAddRestrictingRandomSid = false;
    mUseCig = false;

    // When we have an LPAC we can't set an integrity level and the process will
    // default to low integrity anyway. Without an LPAC using low integrity
    // causes problems with the CDMs.
    mInitialIntegrityLevel = sandbox::INTEGRITY_LEVEL_LAST;
    mDelayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_LAST;

    if (StaticPrefs::security_sandbox_utility_wmf_cdm_lpac_enabled()) {
      mPackagePrefix = u"fx.sb.cdm"_ns;
      mWellKnownCapabilites = {
          base::win::WellKnownCapability::kPrivateNetworkClientServer,
          base::win::WellKnownCapability::kInternetClient,
      };
      mNamedCapabilites = {
          L"lpacCom",
          L"lpacIdentityServices",
          L"lpacMedia",
          L"lpacPnPNotifications",
          L"lpacServicesManagement",
          L"lpacSessionManagement",
          L"lpacAppExperience",
          L"lpacInstrumentation",
          L"lpacCryptoServices",
          L"lpacEnterprisePolicyChangeNotifications",
          L"mediaFoundationCdmFiles",
          L"lpacMediaFoundationCdmData",
          L"registryRead",
          kLpacFirefoxInstallFiles,
          L"lpacDeviceAccess",
      };

      // For MSIX packages we need access to the package contents.
      if (widget::WinUtils::HasPackageIdentity()) {
        mNamedCapabilites.AppendElement(L"packageContents");
      }
    }
    mUseWin32kLockdown = false;
    mDelayedMitigations = sandbox::MITIGATION_DLL_SEARCH_ORDER;
  }
};
#endif

struct WindowsUtilitySandboxProps : public UtilitySandboxProps {
  WindowsUtilitySandboxProps() {
    mJobLevel = sandbox::JobLevel::kInteractive;
    mDelayedTokenLevel = sandbox::USER_RESTRICTED_SAME_ACCESS;
    mDesktop = sandbox::Desktop::kDefault;
    mInitialIntegrityLevel = sandbox::INTEGRITY_LEVEL_MEDIUM;
    mDelayedIntegrityLevel = sandbox::INTEGRITY_LEVEL_MEDIUM;
    mUseWin32kLockdown = false;
    mUseCig = false;
    mExcludedInitialMitigations =
        sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED;
    mDelayedMitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                          sandbox::MITIGATION_DLL_SEARCH_ORDER;
  }
};

static const char* WellKnownCapabilityNames[] = {
    "InternetClient",
    "InternetClientServer",
    "PrivateNetworkClientServer",
    "PicturesLibrary",
    "VideosLibrary",
    "MusicLibrary",
    "DocumentsLibrary",
    "EnterpriseAuthentication",
    "SharedUserCertificates",
    "RemovableStorage",
    "Appointments",
    "Contacts",
};

void LogUtilitySandboxProps(const UtilitySandboxProps& us) {
  if (!static_cast<LogModule*>(sSandboxBrokerLog)->ShouldLog(LogLevel::Debug)) {
    return;
  }

  nsAutoCString logMsg;
  logMsg.AppendPrintf("Building sandbox for utility process:\n");
  logMsg.AppendPrintf("\tJob Level: %d\n", static_cast<int>(us.mJobLevel));
  logMsg.AppendPrintf("\tInitial Token Level: %d\n",
                      static_cast<int>(us.mInitialTokenLevel));
  logMsg.AppendPrintf("\tDelayed Token Level: %d\n",
                      static_cast<int>(us.mDelayedTokenLevel));
  logMsg.AppendPrintf("\tInitial Integrity Level: %d\n",
                      static_cast<int>(us.mInitialIntegrityLevel));
  logMsg.AppendPrintf("\tDelayed Integrity Level: %d\n",
                      static_cast<int>(us.mDelayedIntegrityLevel));
  logMsg.AppendPrintf("\tDesktop: %d\n", static_cast<int>(us.mDesktop));
  logMsg.AppendPrintf("\tLockdown Default Dacl: %s\n",
                      us.mLockdownDefaultDacl ? "yes" : "no");
  logMsg.AppendPrintf("\tAdd Random Restricting SID: %s\n",
                      us.mAddRestrictingRandomSid ? "yes" : "no");
  logMsg.AppendPrintf("\tUse Win32k Lockdown: %s\n",
                      us.mUseWin32kLockdown ? "yes" : "no");
  logMsg.AppendPrintf("\tUse CIG: %s\n", us.mUseCig ? "yes" : "no");
  logMsg.AppendPrintf("\tInitial mitigations: %016llx\n",
                      static_cast<uint64_t>(us.mInitialMitigations &
                                            ~us.mExcludedInitialMitigations));
  logMsg.AppendPrintf("\tDelayed mitigations: %016llx\n",
                      static_cast<uint64_t>(us.mDelayedMitigations));
  if (us.mPackagePrefix.IsEmpty()) {
    logMsg.AppendPrintf("\tNo Low Privileged Application Container\n");
  } else {
    logMsg.AppendPrintf("\tLow Privileged Application Container Settings:\n");
    logMsg.AppendPrintf("\t\tPackage Name Prefix: %S\n",
                        static_cast<wchar_t*>(us.mPackagePrefix.get()));
    logMsg.AppendPrintf("\t\tWell Known Capabilities:\n");
    for (auto wkCap : us.mWellKnownCapabilites) {
      logMsg.AppendPrintf("\t\t\t%s\n",
                          WellKnownCapabilityNames[static_cast<int>(wkCap)]);
    }
    logMsg.AppendPrintf("\t\tNamed Capabilities:\n");
    for (auto namedCap : us.mNamedCapabilites) {
      logMsg.AppendPrintf("\t\t\t%S\n", namedCap);
    }
  }

  LOG_D("%s", logMsg.get());
}

bool BuildUtilitySandbox(sandbox::TargetConfig* config,
                         const UtilitySandboxProps& us) {
  LogUtilitySandboxProps(us);

  auto result = config->SetJobLevel(us.mJobLevel, 0 /* ui_exceptions */);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetJobLevel should never fail with these arguments, what happened?");

  result = config->SetTokenLevel(us.mInitialTokenLevel, us.mDelayedTokenLevel);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetTokenLevel should never fail with these arguments, what happened?");

  if (us.mInitialIntegrityLevel != sandbox::INTEGRITY_LEVEL_LAST) {
    result = config->SetIntegrityLevel(us.mInitialIntegrityLevel);
    SANDBOX_ENSURE_SUCCESS(result,
                           "SetIntegrityLevel should never fail with these "
                           "arguments, what happened?");
  }

  if (us.mDelayedIntegrityLevel != sandbox::INTEGRITY_LEVEL_LAST) {
    config->SetDelayedIntegrityLevel(us.mDelayedIntegrityLevel);
  }

  config->SetDesktop(us.mDesktop);

  if (us.mLockdownDefaultDacl) {
    config->SetLockdownDefaultDacl();
  }
  if (us.mAddRestrictingRandomSid) {
    config->AddRestrictingRandomSid();
  }

  result = config->SetProcessMitigations(us.mInitialMitigations &
                                         ~us.mExcludedInitialMitigations);
  SANDBOX_ENSURE_SUCCESS(result, "Invalid flags for SetProcessMitigations.");

  result = config->SetDelayedProcessMitigations(us.mDelayedMitigations);
  SANDBOX_ENSURE_SUCCESS(result,
                         "Invalid flags for SetDelayedProcessMitigations.");

  // Win32k lockdown might not work on earlier versions
  // Bug 1719212, 1769992
  if (us.mUseWin32kLockdown && IsWin10FallCreatorsUpdateOrLater()) {
    result = AddWin32kLockdownConfig(config);
    SANDBOX_ENSURE_SUCCESS(result, "Failed to add the win32k lockdown config");
  }

  if (us.mUseCig) {
    bool alwaysProxyBinDirLoading = mozilla::HasPackageIdentity();
    result = AddCigToConfig(config, alwaysProxyBinDirLoading);
    SANDBOX_ENSURE_SUCCESS(result, "Failed to initialize signed policy rules.");
  }

  // Process fails to start in LPAC with ASan build
#if !defined(MOZ_ASAN)
  if (!us.mPackagePrefix.IsEmpty()) {
    MOZ_ASSERT(us.mInitialIntegrityLevel == sandbox::INTEGRITY_LEVEL_LAST,
               "Initial integrity level cannot be specified if using an LPAC.");

    result = AddAndConfigureAppContainerProfile(config, us.mPackagePrefix,
                                                us.mWellKnownCapabilites,
                                                us.mNamedCapabilites);
    SANDBOX_ENSURE_SUCCESS(result, "Failed to configure AppContainer profile.");
  }
#endif

  // Add the policy for the client side of the crash server pipe.
  result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   L"\\??\\pipe\\gecko-crash-server-pipe.*");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  return true;
}

bool SandboxBroker::SetSecurityLevelForUtilityProcess(
    mozilla::ipc::SandboxingKind aSandbox) {
  MOZ_ASSERT(IsUtilitySandboxEnabled(aSandbox));

  if (!mPolicy) {
    return false;
  }

  auto* config = mPolicy->GetConfig();

  switch (aSandbox) {
    case mozilla::ipc::SandboxingKind::GENERIC_UTILITY:
      return BuildUtilitySandbox(config, GenericUtilitySandboxProps());
    case mozilla::ipc::SandboxingKind::UTILITY_AUDIO_DECODING_WMF:
      return BuildUtilitySandbox(config, UtilityAudioDecodingWmfSandboxProps());
#ifdef MOZ_WMF_MEDIA_ENGINE
    case mozilla::ipc::SandboxingKind::MF_MEDIA_ENGINE_CDM: {
      if (!BuildUtilitySandbox(config, UtilityMfMediaEngineCdmSandboxProps())) {
        return false;
      }
      // Allow MFTEnumEx to enumerate registered MFT decoders/encoders.
      auto result = config->AllowRegistryRead(
          L"HKEY_LOCAL_MACHINE\\Software\\Classes\\MediaFoundation\\*");
      if (sandbox::SBOX_ALL_OK != result) {
        NS_WARNING("Failed to add MediaFoundation registry rule for MFCDM.");
      }
      // MFTEnumEx reads CLSID subkeys to enumerate registered Media Foundation
      // Transforms. The exact CLSIDs vary per system and are not known at build
      // time, so a wildcard covering the full CLSID hive is required.
      result = config->AllowRegistryRead(
          L"HKEY_LOCAL_MACHINE\\Software\\Classes\\CLSID\\*");
      if (sandbox::SBOX_ALL_OK != result) {
        NS_WARNING("Failed to add CLSID registry rule for MFCDM.");
      }
      return true;
    }
#endif
    case mozilla::ipc::SandboxingKind::WINDOWS_UTILS:
      return BuildUtilitySandbox(config, WindowsUtilitySandboxProps());
    case mozilla::ipc::SandboxingKind::HW_INFERENCE:
      return BuildUtilitySandbox(config, GenericUtilitySandboxProps());
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown sandboxing value");
      return false;
  }
}

bool SandboxBroker::SetSecurityLevelForGMPlugin(
    GMPSandboxKind aGMPSandboxKind) {
  if (!mPolicy) {
    return false;
  }

  auto* config = mPolicy->GetConfig();

  auto result =
      config->SetJobLevel(sandbox::JobLevel::kLockdown, 0 /* ui_exceptions */);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetJobLevel should never fail with these arguments, what happened?");

  // The Widevine CDM on Windows can only load at USER_RESTRICTED
  auto level = (aGMPSandboxKind == Widevine) ? sandbox::USER_RESTRICTED
                                             : sandbox::USER_LOCKDOWN;
  result = config->SetTokenLevel(sandbox::USER_RESTRICTED_SAME_ACCESS, level);
  SANDBOX_ENSURE_SUCCESS(
      result,
      "SetTokenLevel should never fail with these arguments, what happened?");

  config->SetDesktop(sandbox::Desktop::kAlternateWinstation);

  result = config->SetIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);
  MOZ_ASSERT(sandbox::SBOX_ALL_OK == result,
             "SetIntegrityLevel should never fail with these arguments, what "
             "happened?");

  config->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_UNTRUSTED);

  config->SetLockdownDefaultDacl();
  config->AddRestrictingRandomSid();

  sandbox::MitigationFlags mitigations =
      sandbox::MITIGATION_BOTTOM_UP_ASLR | sandbox::MITIGATION_HEAP_TERMINATE |
      sandbox::MITIGATION_SEHOP | sandbox::MITIGATION_EXTENSION_POINT_DISABLE |
      sandbox::MITIGATION_NONSYSTEM_FONT_DISABLE |
      sandbox::MITIGATION_KTM_COMPONENT | sandbox::MITIGATION_FSCTL_DISABLED |
      sandbox::MITIGATION_IMAGE_LOAD_NO_REMOTE |
      sandbox::MITIGATION_IMAGE_LOAD_NO_LOW_LABEL |
      sandbox::MITIGATION_DEP_NO_ATL_THUNK | sandbox::MITIGATION_DEP;

  if (StaticPrefs::security_sandbox_gmp_shadow_stack_enabled()) {
    mitigations |= sandbox::MITIGATION_CET_COMPAT_MODE;
  }

  result = config->SetProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result, "Invalid flags for SetProcessMitigations.");

  // win32k is currently not disabled for clearkey due to WMF decoding or
  // widevine due to intermittent test failures, where the GMP process fails
  // very early. See bug 1449348.
  // The sandbox doesn't provide Output Protection Manager API brokering
  // anymore, so we can't use this for the Fake plugin that is used to partially
  // test it. This brokering was only used in the tests anyway.
  if (StaticPrefs::security_sandbox_gmp_win32k_disable() &&
      aGMPSandboxKind != Widevine && aGMPSandboxKind != Clearkey &&
      aGMPSandboxKind != Fake) {
    result = AddWin32kLockdownConfig(config);
    SANDBOX_ENSURE_SUCCESS(result, "Failed to add the win32k lockdown policy");
  }

  mitigations = sandbox::MITIGATION_STRICT_HANDLE_CHECKS |
                sandbox::MITIGATION_DLL_SEARCH_ORDER;
  if (StaticPrefs::security_sandbox_gmp_acg_enabled()) {
    auto acgMitigation = sandbox::MITIGATION_DYNAMIC_CODE_DISABLE;
    if (aGMPSandboxKind == Widevine) {
      // We can't guarantee that widevine won't use dynamic code.
      acgMitigation = 0;
    } else if (aGMPSandboxKind == Clearkey) {
      // Clearkey uses system decoding libraries.
      acgMitigation = DynamicCodeFlagForSystemMediaLibraries();
    }
    mitigations |= acgMitigation;
  }

  result = config->SetDelayedProcessMitigations(mitigations);
  SANDBOX_ENSURE_SUCCESS(result,
                         "Invalid flags for SetDelayedProcessMitigations.");

  // Add the policy for the client side of the crash server pipe.
  result = config->AllowFileAccess(sandbox::FileSemantics::kAllowAny,
                                   L"\\??\\pipe\\gecko-crash-server-pipe.*");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  // The following rules were added because, during analysis of an EME
  // plugin during development, these registry keys were accessed when
  // loading the plugin. Commenting out these policy exceptions caused
  // plugin loading to fail, so they are necessary for proper functioning
  // of at least one EME plugin.
  result = config->AllowRegistryRead(L"HKEY_CURRENT_USER");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result =
      config->AllowRegistryRead(L"HKEY_CURRENT_USER\\Control Panel\\Desktop");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result = config->AllowRegistryRead(
      L"HKEY_CURRENT_USER\\Control Panel\\Desktop\\LanguageConfiguration");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result = config->AllowRegistryRead(
      L"HKEY_LOCAL_MACHINE"
      L"\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SideBySide");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  // The following rules were added because, during analysis of an EME
  // plugin during development, these registry keys were accessed when
  // loading the plugin. Commenting out these policy exceptions did not
  // cause anything to break during initial testing, but might cause
  // unforeseen issues down the road.
  result = config->AllowRegistryRead(
      L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\MUI\\Settings");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result = config->AllowRegistryRead(
      L"HKEY_CURRENT_USER"
      L"\\Software\\Policies\\Microsoft\\Control Panel\\Desktop");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result = config->AllowRegistryRead(
      L"HKEY_CURRENT_USER\\Control Panel\\Desktop\\PreferredUILanguages");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "With these static arguments AddRule should never fail, what happened?");

  result = config->AllowRegistryRead(
      L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion"
      L"\\SideBySide\\PreferExternalManifest");
  SANDBOX_ENSURE_SUCCESS(
      result,
      "with these static arguments addrule should never fail, what happened?");

  return true;
}
#undef SANDBOX_ENSURE_SUCCESS

bool SandboxBroker::AllowReadFile(wchar_t const* file) {
  if (!mPolicy) {
    return false;
  }

  auto result = mPolicy->GetConfig()->AllowFileAccess(
      sandbox::FileSemantics::kAllowReadonly, file);
  if (sandbox::SBOX_ALL_OK != result) {
    LOG_E("Failed (ResultCode %d) to add read access to: %S", result, file);
    return false;
  }

  return true;
}

void SandboxBroker::AddHandleToShare(HANDLE aHandle) {
  mPolicy->AddHandleToShare(aHandle);
}

bool SandboxBroker::IsWin32kLockedDown() {
  return mPolicy->GetConfig()->GetProcessMitigations() &
         sandbox::MITIGATION_WIN32K_DISABLE;
}

void SandboxBroker::ApplyLoggingConfig() {
  MOZ_ASSERT(mPolicy);

  auto* config = mPolicy->GetConfig();

  // Add dummy rules, so that we can log in the interception code.
  // We already have a file interception set up for the client side of pipes.
  (void)config->AllowRegistryRead(L"HKEY_CURRENT_USER\\dummy");
}

SandboxBroker::~SandboxBroker() = default;

}  // namespace mozilla
