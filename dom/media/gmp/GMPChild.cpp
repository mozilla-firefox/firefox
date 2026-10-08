/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPChild.h"

#include "base/command_line.h"
#include "base/task.h"
#include "ChildProfilerController.h"
#include "ChromiumCDMAdapter.h"
#ifdef XP_LINUX
#  include "dlfcn.h"
#endif
#include "gmp-video-decode.h"
#include "gmp-video-encode.h"
#include "GMPContentChild.h"
#include "GMPLoader.h"
#include "GMPLog.h"
#include "GMPPlatform.h"
#include "GMPProcessChild.h"
#include "GMPProcessParent.h"
#include "GMPUtils.h"
#include "GMPVideoDecoderChild.h"
#include "GMPVideoEncoderChild.h"
#include "GMPVideoHost.h"
#include "mozilla/Algorithm.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/glean/GleanMetrics.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/TextUtils.h"
#include "nsDebugImpl.h"
#include "nsExceptionHandler.h"
#include "nsIFile.h"
#include "nsReadableUtils.h"
#include "nsThreadManager.h"
#include "nsXULAppAPI.h"
#include "nsIXULRuntime.h"
#include "nsXPCOMPrivate.h"  // for XUL_DLL
#include "prio.h"
#ifdef XP_WIN
#  include <stdlib.h>  // for _exit()
#  include "WinUtils.h"
#else
#  include <unistd.h>  // for _exit()
#endif

using namespace mozilla::ipc;

namespace mozilla {

#define GMP_CHILD_LOG_DEBUG(x, ...)                                   \
  GMP_LOG_DEBUG("GMPChild[pid=%d] " x, (int)base::GetCurrentProcId(), \
                ##__VA_ARGS__)

namespace gmp {

GMPChild::GMPChild()
    : mGMPMessageLoop(MessageLoop::current()), mGMPLoader(nullptr) {
  GMP_CHILD_LOG_DEBUG("GMPChild ctor");
  nsDebugImpl::SetMultiprocessMode("GMP");
}

GMPChild::~GMPChild() {
  GMP_CHILD_LOG_DEBUG("GMPChild dtor");
#ifdef XP_LINUX
  for (auto& libHandle : mLibHandles) {
    dlclose(libHandle);
  }
#endif
}

bool GMPChild::Init(const nsAString& aPluginPath,
                    mozilla::ipc::UntypedEndpoint&& aEndpoint) {
  GMP_CHILD_LOG_DEBUG("%s pluginPath=%s", __FUNCTION__,
                      NS_ConvertUTF16toUTF8(aPluginPath).get());

  // GMPChild needs nsThreadManager to create the ProfilerChild thread.
  // It is also used on debug builds for the sandbox tests.
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }

  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return false;
  }

  CrashReporterClient::InitSingleton(this);

  mPluginPath = aPluginPath;

  return true;
}

mozilla::ipc::IPCResult GMPChild::RecvProvideStorageId(
    const nsCString& aStorageId) {
  GMP_CHILD_LOG_DEBUG("%s", __FUNCTION__);
  mStorageId = aStorageId;
  return IPC_OK();
}

GMPErr GMPChild::GetAPI(const char* aAPIName, void* aHostAPI, void** aPluginAPI,
                        const nsACString& aKeySystem) {
  if (!mGMPLoader) {
    return GMPGenericErr;
  }
  return mGMPLoader->GetAPI(aAPIName, aHostAPI, aPluginAPI, aKeySystem);
}

mozilla::ipc::IPCResult GMPChild::RecvPreloadLibs(const nsCString& aLibs) {
  // Pre-load libraries that may need to be used by the EME plugin but that
  // can't be loaded after the sandbox has started.
#ifdef XP_WIN
  // Items in this must be lowercase!
  constexpr static const char16_t* whitelist[] = {
      u"dbghelp.dll",      // Dependency for widevine
      u"dxva2.dll",        // Get monitor information
      u"evr.dll",          // MFGetStrideForBitmapInfoHeader
      u"freebl3.dll",      // NSS for clearkey CDM
      u"mfplat.dll",       // MFCreateSample, MFCreateAlignedMemoryBuffer,
                           // MFCreateMediaType
      u"msmpeg2vdec.dll",  // H.264 decoder
      u"nss3.dll",         // NSS for clearkey CDM
      u"ole32.dll",        // required for OPM
      u"oleaut32.dll",     // For _bstr_t use in libwebrtc, see bug 1788592
      u"psapi.dll",        // For GetMappedFileNameW, see bug 1383611
      u"shell32.dll",      // Dependency for widevine
      u"softokn3.dll",     // NSS for clearkey CDM
      u"winmm.dll",        // Dependency for widevine
  };
  constexpr static bool (*IsASCII)(const char16_t*) =
      IsAsciiNullTerminated<char16_t>;
  static_assert(AllOf(std::begin(whitelist), std::end(whitelist), IsASCII),
                "Items in the whitelist must not contain non-ASCII "
                "characters!");

  nsTArray<nsCString> libs;
  SplitAt(", ", aLibs, libs);
  for (nsCString lib : libs) {
    ToLowerCase(lib);
    for (const char16_t* whiteListedLib : whitelist) {
      if (nsDependentString(whiteListedLib)
              .EqualsASCII(lib.Data(), lib.Length())) {
        LoadLibraryW(char16ptr_t(whiteListedLib));
        break;
      }
    }
  }
#elif defined(XP_LINUX)
  constexpr static const char* whitelist[] = {
      // NSS libraries used by clearkey.
      "libfreeblpriv3.so",
      "libsoftokn3.so",
      // glibc libraries merged into libc.so.6; see bug 1725828 and
      // the corresponding code in GMPParent.cpp.
      "libdl.so.2",
      "libpthread.so.0",
      "librt.so.1",
  };

  nsTArray<nsCString> libs;
  SplitAt(", ", aLibs, libs);
  for (const nsCString& lib : libs) {
    for (const char* whiteListedLib : whitelist) {
      if (lib.EqualsASCII(whiteListedLib)) {
        auto libHandle = dlopen(whiteListedLib, RTLD_NOW | RTLD_GLOBAL);
        if (libHandle) {
          mLibHandles.AppendElement(libHandle);
        } else {
          // TODO(bug 1698718): remove the logging once we've identified
          // the cause of the load failure.
          const char* error = dlerror();
          if (error) {
            // We should always have an error, but gracefully handle just in
            // case.
            nsAutoCString nsError{error};
            CrashReporter::AppendAppNotesToCrashReport(nsError);
          }
          // End bug 1698718 logging.

          MOZ_CRASH("Couldn't load lib needed by media plugin");
        }
      }
    }
  }
#endif
  return IPC_OK();
}

bool GMPChild::GetUTF8LibPath(nsACString& aOutLibPath) {
  nsCOMPtr<nsIFile> libFile;

#define GMP_PATH_CRASH(explain)                          \
  do {                                                   \
    nsAutoString path;                                   \
    if (!libFile || NS_FAILED(libFile->GetPath(path))) { \
      path = mPluginPath;                                \
    }                                                    \
    CrashReporter::AnnotateCrashReport(                  \
        CrashReporter::Annotation::GMPLibraryPath,       \
        NS_ConvertUTF16toUTF8(path));                    \
    MOZ_CRASH(explain);                                  \
  } while (false)

  nsresult rv = NS_NewLocalFile(mPluginPath, true, getter_AddRefs(libFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to create file for plugin path");
    return false;
  }

  nsCOMPtr<nsIFile> parent;
  rv = libFile->GetParent(getter_AddRefs(parent));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get parent file for plugin file");
    return false;
  }

  nsAutoString parentLeafName;
  rv = parent->GetLeafName(parentLeafName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get leaf for plugin file");
    return false;
  }

  nsAutoString baseName;
  baseName = Substring(parentLeafName, 4, parentLeafName.Length() - 1);

#if defined(XP_MACOSX)
  nsAutoString binaryName = u"lib"_ns + baseName + u".dylib"_ns;
#elif defined(OS_POSIX)
  nsAutoString binaryName = u"lib"_ns + baseName + u".so"_ns;
#elif defined(XP_WIN)
  nsAutoString binaryName = baseName + u".dll"_ns;
#else
#  error not defined
#endif
  rv = libFile->AppendRelativePath(binaryName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to append lib to plugin file");
    return false;
  }

  if (NS_WARN_IF(!FileExists(libFile))) {
    GMP_PATH_CRASH("Plugin file does not exist");
    return false;
  }

  nsAutoString path;
  rv = libFile->GetPath(path);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    GMP_PATH_CRASH("Failed to get path for plugin file");
    return false;
  }

  CopyUTF16toUTF8(path, aOutLibPath);
  return true;
}

static nsCOMPtr<nsIFile> AppendFile(nsCOMPtr<nsIFile>&& aFile,
                                    const nsString& aStr) {
  return (aFile && NS_SUCCEEDED(aFile->Append(aStr))) ? aFile : nullptr;
}

static nsCOMPtr<nsIFile> CloneFile(const nsCOMPtr<nsIFile>& aFile) {
  nsCOMPtr<nsIFile> clone;
  return (aFile && NS_SUCCEEDED(aFile->Clone(getter_AddRefs(clone)))) ? clone
                                                                      : nullptr;
}

static nsCOMPtr<nsIFile> GetParentFile(const nsCOMPtr<nsIFile>& aFile) {
  nsCOMPtr<nsIFile> parent;
  return (aFile && NS_SUCCEEDED(aFile->GetParent(getter_AddRefs(parent))))
             ? parent
             : nullptr;
}

#if defined(XP_WIN)
static bool IsFileLeafEqualToASCII(const nsCOMPtr<nsIFile>& aFile,
                                   const char* aStr) {
  nsAutoString leafName;
  return aFile && NS_SUCCEEDED(aFile->GetLeafName(leafName)) &&
         leafName.EqualsASCII(aStr);
}
#endif

#if defined(XP_WIN)
#  define FIREFOX_FILE MOZ_APP_NAME u".exe"_ns
#else
#  define FIREFOX_FILE MOZ_APP_NAME u""_ns
#endif
#define XUL_LIB_FILE XUL_DLL u""_ns

static nsCOMPtr<nsIFile> GetFirefoxAppPath(
    nsCOMPtr<nsIFile> aPluginContainerPath) {
  MOZ_ASSERT(aPluginContainerPath);
#if defined(XP_MACOSX)
  // On MacOS the firefox binary is a few parent directories up from
  // plugin-container.
  // aPluginContainerPath will end with something like:
  // xxxx/NightlyDebug.app/Contents/MacOS/plugin-container.app/Contents/MacOS/plugin-container
  nsCOMPtr<nsIFile> path = aPluginContainerPath;
  for (int i = 0; i < 4; i++) {
    path = GetParentFile(path);
  }
  return path;
#else
  nsCOMPtr<nsIFile> parent = GetParentFile(aPluginContainerPath);
#  if XP_WIN
  if (IsFileLeafEqualToASCII(parent, "i686")) {
    // We must be on Windows on ARM64, where the plugin-container path will
    // be in the 'i686' subdir. The firefox.exe is in the parent directory.
    parent = GetParentFile(parent);
  }
#  endif
  return parent;
#endif
}

#if defined(XP_MACOSX)
static bool GetSigPath(const int aRelativeLayers,
                       const nsString& aTargetSigFileName,
                       nsCOMPtr<nsIFile> aExecutablePath,
                       nsCOMPtr<nsIFile>& aOutSigPath) {
  // The sig file will be located in
  // xxxx/NightlyDebug.app/Contents/Resources/XUL.sig
  // xxxx/NightlyDebug.app/Contents/Resources/firefox.sig
  // xxxx/NightlyDebug.app/Contents/MacOS/plugin-container.app/Contents/Resources/plugin-container.sig
  // On MacOS the sig file is a few parent directories up from
  // its executable file.
  // Start to search the path from the path of the executable file we provided.
  MOZ_ASSERT(aExecutablePath);
  nsCOMPtr<nsIFile> path = aExecutablePath;
  for (int i = 0; i < aRelativeLayers; i++) {
    nsCOMPtr<nsIFile> parent;
    if (NS_WARN_IF(NS_FAILED(path->GetParent(getter_AddRefs(parent))))) {
      return false;
    }
    path = parent;
  }
  MOZ_ASSERT(path);
  aOutSigPath = path;
  return NS_SUCCEEDED(path->Append(u"Resources"_ns)) &&
         NS_SUCCEEDED(path->Append(aTargetSigFileName));
}
#endif

static bool AppendHostPath(nsCOMPtr<nsIFile>& aFile,
                           nsTArray<std::pair<nsCString, nsCString>>& aPaths) {
  nsString str;
  if (!FileExists(aFile) || NS_FAILED(aFile->GetPath(str))) {
    return false;
  }

  nsCString filePath = NS_ConvertUTF16toUTF8(str);
  nsCString sigFilePath;
#if defined(XP_MACOSX)
  nsAutoString binary;
  if (NS_FAILED(aFile->GetLeafName(binary))) {
    return false;
  }
  binary.Append(u".sig"_ns);
  nsCOMPtr<nsIFile> sigFile;
  if (GetSigPath(2, binary, aFile, sigFile) &&
      NS_SUCCEEDED(sigFile->GetPath(str))) {
    CopyUTF16toUTF8(str, sigFilePath);
  } else {
    // Cannot successfully get the sig file path.
    // Assume it is located at the same place as plugin-container
    // alternatively.
    sigFilePath = nsCString(NS_ConvertUTF16toUTF8(str) + ".sig"_ns);
  }
#else
  sigFilePath = nsCString(NS_ConvertUTF16toUTF8(str) + ".sig"_ns);
#endif
  aPaths.AppendElement(
      std::make_pair(std::move(filePath), std::move(sigFilePath)));
  return true;
}

nsTArray<std::pair<nsCString, nsCString>>
GMPChild::MakeCDMHostVerificationPaths(const nsACString& aPluginLibPath) {
  // Record the file path and its sig file path.
  nsTArray<std::pair<nsCString, nsCString>> paths;
  // Plugin binary path.
  paths.AppendElement(
      std::make_pair(nsCString(aPluginLibPath), aPluginLibPath + ".sig"_ns));

  // Plugin-container binary path.
  // Note: clang won't let us initialize an nsString from a wstring, so we
  // need to go through UTF8 to get to an nsString.
  const std::string pluginContainer =
      WideToUTF8(CommandLine::ForCurrentProcess()->program());
  nsString str;

  CopyUTF8toUTF16(nsDependentCString(pluginContainer.c_str()), str);
  nsCOMPtr<nsIFile> path;
  if (NS_FAILED(NS_NewLocalFile(str, true, /* aFollowLinks */
                                getter_AddRefs(path))) ||
      !AppendHostPath(path, paths)) {
    // Without successfully determining plugin-container's path, we can't
    // determine libxul's or Firefox's. So give up.
    return paths;
  }

#if defined(XP_WIN)
  // On Windows on ARM64, we should also append the x86 plugin-container's
  // xul.dll.
  const bool isWindowsOnARM64 =
      IsFileLeafEqualToASCII(GetParentFile(path), "i686");
  if (isWindowsOnARM64) {
    nsCOMPtr<nsIFile> x86XulPath =
        AppendFile(GetParentFile(path), XUL_LIB_FILE);
    if (!AppendHostPath(x86XulPath, paths)) {
      return paths;
    }
  }
#endif

  // Firefox application binary path.
  nsCOMPtr<nsIFile> appDir = GetFirefoxAppPath(path);
  path = AppendFile(CloneFile(appDir), FIREFOX_FILE);
  if (!AppendHostPath(path, paths)) {
    return paths;
  }

  // Libxul path. Note: re-using 'appDir' var here, as we assume libxul is in
  // the same directory as Firefox executable.
  appDir->GetPath(str);
  path = AppendFile(CloneFile(appDir), XUL_LIB_FILE);
  if (!AppendHostPath(path, paths)) {
    return paths;
  }

  return paths;
}

static auto ToCString(const nsTArray<std::pair<nsCString, nsCString>>& aPairs) {
  return StringJoin(","_ns, aPairs, [](nsACString& dest, const auto& p) {
    dest.AppendPrintf("(%s,%s)", p.first.get(), p.second.get());
  });
}

mozilla::ipc::IPCResult GMPChild::RecvStartPlugin(const nsString& aAdapter) {
  GMP_CHILD_LOG_DEBUG("%s", __FUNCTION__);

  nsAutoCString libPath;
  if (!GetUTF8LibPath(libPath)) {
    CrashReporter::AnnotateCrashReport(
        CrashReporter::Annotation::GMPLibraryPath,
        NS_ConvertUTF16toUTF8(mPluginPath));
#ifdef XP_WIN
    return IPC_FAIL(this,
                    nsPrintfCString("Failed to get lib path with error(%lu).",
                                    GetLastError())
                        .get());
#else
    return IPC_FAIL(this, "Failed to get lib path.");
#endif
  }

  auto platformAPI = new GMPPlatformAPI();
  InitPlatformAPI(*platformAPI, this);

  mGMPLoader = MakeUnique<GMPLoader>();
#if defined(MOZ_SANDBOX) && !defined(XP_MACOSX)
  if (!mGMPLoader->CanSandbox()) {
    GMP_CHILD_LOG_DEBUG("%s Can't sandbox GMP, failing", __FUNCTION__);
    delete platformAPI;
    return IPC_FAIL(this, "Can't sandbox GMP.");
  }
#endif

  GMPAdapter* adapter = nullptr;
  if (aAdapter.EqualsLiteral("chromium")) {
    auto&& paths = MakeCDMHostVerificationPaths(libPath);
    GMP_CHILD_LOG_DEBUG("%s CDM host paths=%s", __func__,
                        ToCString(paths).get());
    adapter = new ChromiumCDMAdapter(std::move(paths));
  }

  if (!mGMPLoader->Load(libPath.get(), libPath.Length(), platformAPI,
                        adapter)) {
    NS_WARNING("Failed to load GMP");
    delete platformAPI;
    CrashReporter::AnnotateCrashReport(
        CrashReporter::Annotation::GMPLibraryPath,
        NS_ConvertUTF16toUTF8(mPluginPath));

#ifdef XP_WIN
    return IPC_FAIL(this, nsPrintfCString("Failed to load GMP with error(%lu).",
                                          GetLastError())
                              .get());
#else
    return IPC_FAIL(this, "Failed to load GMP.");
#endif
  }

  return IPC_OK();
}

MessageLoop* GMPChild::GMPMessageLoop() { return mGMPMessageLoop; }

void GMPChild::ActorDestroy(ActorDestroyReason aWhy) {
  GMP_CHILD_LOG_DEBUG("%s reason=%d", __FUNCTION__, aWhy);

  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    MOZ_ASSERT_IF(aWhy == NormalShutdown,
                  !mGMPContentChildren[i - 1]->IsUsed());
    mGMPContentChildren[i - 1]->Close();
  }

  if (mGMPLoader) {
    mGMPLoader->Shutdown();
  }
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Abnormal shutdown of GMP process!");
    ProcessChild::QuickExit();
  }

  // Send the last bits of Glean data over to the main process.
  glean::FlushFOGData(
      [](ByteBuf&& aBuf) { glean::SendFOGData(std::move(aBuf)); });

  if (mProfilerController) {
    mProfilerController->Shutdown();
    mProfilerController = nullptr;
  }

  CrashReporterClient::DestroySingleton();

  XRE_ShutdownChildProcess();
}

void GMPChild::ProcessingError(Result aCode, const char* aReason) {
  switch (aCode) {
    case MsgDropped:
      _exit(0);  // Don't trigger a crash report.
    case MsgNotKnown:
      MOZ_CRASH("aborting because of MsgNotKnown");
    case MsgNotAllowed:
      MOZ_CRASH("aborting because of MsgNotAllowed");
    case MsgPayloadError:
      MOZ_CRASH("aborting because of MsgPayloadError");
    case MsgProcessingError:
      MOZ_CRASH("aborting because of MsgProcessingError");
    case MsgRouteError:
      MOZ_CRASH("aborting because of MsgRouteError");
    case MsgValueError:
      MOZ_CRASH("aborting because of MsgValueError");
    default:
      MOZ_CRASH("not reached");
  }
}

PGMPTimerChild* GMPChild::AllocPGMPTimerChild() {
  return new GMPTimerChild(this);
}

bool GMPChild::DeallocPGMPTimerChild(PGMPTimerChild* aActor) {
  MOZ_ASSERT(mTimerChild == static_cast<GMPTimerChild*>(aActor));
  mTimerChild = nullptr;
  return true;
}

GMPTimerChild* GMPChild::GetGMPTimers() {
  if (!mTimerChild) {
    PGMPTimerChild* sc = SendPGMPTimerConstructor();
    if (!sc) {
      return nullptr;
    }
    mTimerChild = static_cast<GMPTimerChild*>(sc);
  }
  return mTimerChild;
}

PGMPStorageChild* GMPChild::AllocPGMPStorageChild() {
  return new GMPStorageChild(this);
}

bool GMPChild::DeallocPGMPStorageChild(PGMPStorageChild* aActor) {
  mStorage = nullptr;
  return true;
}

GMPStorageChild* GMPChild::GetGMPStorage() {
  if (!mStorage) {
    PGMPStorageChild* sc = SendPGMPStorageConstructor();
    if (!sc) {
      return nullptr;
    }
    mStorage = static_cast<GMPStorageChild*>(sc);
  }
  return mStorage;
}

mozilla::ipc::IPCResult GMPChild::RecvCrashPluginNow() {
  MOZ_CRASH();
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvCloseActive() {
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    mGMPContentChildren[i - 1]->CloseActive();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvInitGMPContentChild(
    Endpoint<PGMPContentChild>&& aEndpoint) {
  GMPContentChild* child =
      mGMPContentChildren.AppendElement(new GMPContentChild(this))->get();
  aEndpoint.Bind(child);
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvFlushFOGData(
    FlushFOGDataResolver&& aResolver) {
  GMP_CHILD_LOG_DEBUG("GMPChild RecvFlushFOGData");
  glean::FlushFOGData(std::move(aResolver));
  return IPC_OK();
}

mozilla::ipc::IPCResult GMPChild::RecvTestTriggerMetrics(
    TestTriggerMetricsResolver&& aResolve) {
  GMP_CHILD_LOG_DEBUG("GMPChild RecvTestTriggerMetrics");
  mozilla::glean::test_only_ipc::a_counter.Add(
      nsIXULRuntime::PROCESS_TYPE_GMPLUGIN);
  aResolve(true);
  return IPC_OK();
}

void GMPChild::GMPContentChildActorDestroy(GMPContentChild* aGMPContentChild) {
  for (uint32_t i = mGMPContentChildren.Length(); i > 0; i--) {
    RefPtr<GMPContentChild>& destroyedActor = mGMPContentChildren[i - 1];
    if (destroyedActor.get() == aGMPContentChild) {
      SendPGMPContentChildDestroyed();
      mGMPContentChildren.RemoveElementAt(i - 1);
      break;
    }
  }
}

mozilla::ipc::IPCResult GMPChild::RecvInitProfiler(
    Endpoint<PProfilerChild>&& aEndpoint) {
  mProfilerController =
      mozilla::ChildProfilerController::Create(std::move(aEndpoint));
  return IPC_OK();
}

}  // namespace gmp
}  // namespace mozilla

#undef GMP_CHILD_LOG_DEBUG
#undef __CLASS__
