/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoChildProcessHost.h"

#include "base/command_line.h"
#include "base/process.h"
#include "base/process_util.h"
#include "base/string_util.h"
#include "base/task.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/process_watcher.h"
#ifdef XP_DARWIN
#  include <mach/mach_traps.h>
#  include "base/rand_util.h"
#  include "chrome/common/mach_ipc_mac.h"
#  include "mozilla/StaticPrefs_media.h"
#endif
#ifdef MOZ_WIDGET_COCOA
#  include <bsm/libbsm.h>
#  include <servers/bootstrap.h>
#  include "nsILocalFileMac.h"
#endif

#include "GeckoProfiler.h"
#include "MainThreadUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/Sprintf.h"
#include "nsXPCOMPrivate.h"
#include "prenv.h"
#include "prerror.h"

#if defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#  include "nsAppDirectoryServiceDefs.h"
#endif

#include <sys/stat.h>

#include "ProtocolUtils.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/Omnijar.h"
#include "mozilla/RDDProcessHost.h"
#include "mozilla/Services.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/glean/IpcMetrics.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/ipc/IOThread.h"
#include "mozilla/ipc/EnvironmentMap.h"
#include "mozilla/ipc/NodeController.h"
#include "mozilla/net/SocketProcessHost.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsExceptionHandler.h"
#include "nsIFile.h"
#include "nsIObserverService.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#  include <stdlib.h>

#  include "nsIWinTaskbar.h"
#  define NS_TASKBAR_CONTRACTID "@mozilla.org/windows-taskbar;1"

#  if defined(MOZ_SANDBOX)
#    include "WinUtils.h"
#    include "mozilla/Preferences.h"
#    include "mozilla/sandboxing/sandboxLogging.h"
#  endif

#  include "mozilla/NativeNt.h"
#  include "mozilla/CacheNtDllThunk.h"
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxLaunch.h"
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "GMPProcessParent.h"
#  include "nsMacUtilsImpl.h"
#endif

#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"

#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsNativeCharsetUtils.h"
#include "nsTArray.h"
#include "nscore.h"  // for NS_FREE_PERMANENT_DATA
#include "nsIThread.h"

using mozilla::MonitorAutoLock;
using mozilla::Preferences;
using mozilla::StaticMutexAutoLock;

#ifdef MOZ_WIDGET_ANDROID
#  include "AndroidBridge.h"
#  include "mozilla/java/GeckoProcessManagerWrappers.h"
#  include "mozilla/java/GeckoProcessTypeWrappers.h"
#  include "mozilla/java/GeckoResultWrappers.h"
#  include "mozilla/jni/Refs.h"
#  include "mozilla/jni/Utils.h"
#endif

#ifdef MOZ_ENABLE_FORKSERVER
#  include "mozilla/ipc/ForkServiceChild.h"
#endif

static bool ShouldHaveDirectoryService() {
  return GeckoProcessType_Default == XRE_GetProcessType();
}

namespace mozilla {
namespace ipc {

struct LaunchResults {
  base::ProcessHandle mHandle = 0;
#ifdef XP_DARWIN
  task_t mChildTask = MACH_PORT_NULL;
#endif
#ifdef XP_IOS
  Maybe<ExtensionKitProcess> mExtensionKitProcess;
  DarwinObjectPtr<xpc_connection_t> mXPCConnection;
  UniqueBEProcessCapabilityGrant mForegroundCapabilityGrant;
#endif
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  UniquePtr<SandboxBroker> mSandboxBroker;
#endif
};
typedef mozilla::MozPromise<LaunchResults, LaunchError, true>
    ProcessLaunchPromise;

// Monotonic counter used to generate a unique ChildID for each process as it is
// created. The parent process is given the ChildID of `0`, and each child
// process is given a non-zero ID.
static Atomic<int32_t> gChildCounter;

class BaseProcessLauncher {
 public:
  BaseProcessLauncher(GeckoChildProcessHost* aHost,
                      geckoargs::ChildProcessArgs&& aExtraOpts)
      : mProcessType(aHost->mProcessType),
        mLaunchOptions(std::move(aHost->mLaunchOptions)),
        mChildArgs(std::move(aExtraOpts))
#ifdef XP_WIN
        ,
        mGroupId(aHost->mGroupId)
#endif
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
        ,
        mAllowedFilesRead(aHost->mAllowedFilesRead),
        mSandboxLevel(aHost->mSandboxLevel),
        mSandbox(aHost->mSandbox),
        mIsFileContent(aHost->mIsFileContent),
        mEnableSandboxLogging(aHost->mEnableSandboxLogging)
#endif
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
        ,
        mDisableOSActivityMode(aHost->mDisableOSActivityMode)
#endif
  {
    aHost->mInitialChannelId.ToProvidedString(mInitialChannelIdString);
    SprintfLiteral(mChildIDString, "%d", aHost->mChildID);

    // Compute the serial event target we'll use for launching.
    nsCOMPtr<nsIEventTarget> threadOrPool = GetIPCLauncher();
    mLaunchThread =
        TaskQueue::Create(threadOrPool.forget(), "BaseProcessLauncher");

    if (ShouldHaveDirectoryService()) {
      // "Current process directory" means the app dir, not the current
      // working dir or similar.
      mozilla::Unused
          << nsDirectoryService::gService->GetCurrentProcessDirectory(
                 getter_AddRefs(mAppDir));
    }
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BaseProcessLauncher);

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  void SetLaunchArchitecture(uint32_t aLaunchArch) {
    mLaunchArch = aLaunchArch;
  }
#endif

  RefPtr<ProcessLaunchPromise> Launch(GeckoChildProcessHost*);

 protected:
  virtual ~BaseProcessLauncher() = default;

  RefPtr<ProcessLaunchPromise> PerformAsyncLaunch();
  RefPtr<ProcessLaunchPromise> FinishLaunch();

  // Overrideable hooks. If superclass behavior is invoked, it's always at the
  // top of the override.
  virtual Result<Ok, LaunchError> DoSetup();
  virtual RefPtr<ProcessHandlePromise> DoLaunch() = 0;
  virtual Result<Ok, LaunchError> DoFinishLaunch();

  void MapChildLogging();

  static BinPathType GetPathToBinary(FilePath&, GeckoProcessType);

  void GetChildLogName(const char* origLogName, nsACString& buffer);

  const char* ChildProcessType() {
    return XRE_GeckoProcessTypeToString(mProcessType);
  }

  nsCOMPtr<nsISerialEventTarget> mLaunchThread;
  GeckoProcessType mProcessType;
  UniquePtr<base::LaunchOptions> mLaunchOptions;
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  uint32_t mLaunchArch = base::PROCESS_ARCH_INVALID;
#endif
  geckoargs::ChildProcessArgs mChildArgs;
#ifdef XP_WIN
  nsString mGroupId;
#endif
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  std::vector<std::wstring> mAllowedFilesRead;
  int32_t mSandboxLevel;
  SandboxingKind mSandbox;
  bool mIsFileContent;
  bool mEnableSandboxLogging;
#endif
#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Controls whether or not the process will be launched with
  // environment variable OS_ACTIVITY_MODE set to "disabled".
  bool mDisableOSActivityMode;
#endif
  LaunchResults mResults = LaunchResults();
  TimeStamp mStartTimeStamp = TimeStamp::Now();
  char mInitialChannelIdString[NSID_LENGTH];
  char mChildIDString[32];

  // Set during launch.
  nsCOMPtr<nsIFile> mAppDir;
};

#ifdef XP_WIN
class WindowsProcessLauncher : public BaseProcessLauncher {
 public:
  WindowsProcessLauncher(GeckoChildProcessHost* aHost,
                         geckoargs::ChildProcessArgs&& aExtraOpts)
      : BaseProcessLauncher(aHost, std::move(aExtraOpts)),
        mCachedNtdllThunk(GetCachedNtDllThunk()) {}

 protected:
  virtual Result<Ok, LaunchError> DoSetup() override;
  virtual RefPtr<ProcessHandlePromise> DoLaunch() override;
  virtual Result<Ok, LaunchError> DoFinishLaunch() override;

  mozilla::Maybe<CommandLine> mCmdLine;
#  ifdef MOZ_SANDBOX
  bool mUseSandbox = false;
#  endif

  const Buffer<IMAGE_THUNK_DATA>* mCachedNtdllThunk;
};
typedef WindowsProcessLauncher ProcessLauncher;
#endif  // XP_WIN

#ifdef XP_UNIX
class PosixProcessLauncher : public BaseProcessLauncher {
 public:
  PosixProcessLauncher(GeckoChildProcessHost* aHost,
                       geckoargs::ChildProcessArgs&& aExtraOpts)
      : BaseProcessLauncher(aHost, std::move(aExtraOpts)),
        mProfileDir(aHost->mProfileDir) {}

 protected:
  virtual Result<Ok, LaunchError> DoSetup() override;
  virtual RefPtr<ProcessHandlePromise> DoLaunch() override;

  nsCOMPtr<nsIFile> mProfileDir;
};

#  if defined(XP_MACOSX)
class MacProcessLauncher : public PosixProcessLauncher {
 public:
  MacProcessLauncher(GeckoChildProcessHost* aHost,
                     geckoargs::ChildProcessArgs&& aExtraOpts)
      : PosixProcessLauncher(aHost, std::move(aExtraOpts)),
        // Put a random number into the channel name, so that
        // a compromised renderer can't pretend being the child
        // that's forked off.
        mMachConnectionName(
            StringPrintf("org.mozilla.machname.%d",
                         base::RandInt(0, std::numeric_limits<int>::max()))) {
    MOZ_ASSERT(mMachConnectionName.size() < BOOTSTRAP_MAX_NAME_LEN);
  }

 protected:
  virtual Result<Ok, LaunchError> DoFinishLaunch() override;

  std::string mMachConnectionName;
  // We add a mach port to the command line so the child can communicate its
  // 'task_t' back to the parent.
  mozilla::UniqueMachReceiveRight mParentRecvPort;

  friend class PosixProcessLauncher;
};
typedef MacProcessLauncher ProcessLauncher;
#  elif defined(MOZ_WIDGET_ANDROID)
class AndroidProcessLauncher : public PosixProcessLauncher {
 public:
  AndroidProcessLauncher(GeckoChildProcessHost* aHost,
                         geckoargs::ChildProcessArgs&& aExtraOpts)
      : PosixProcessLauncher(aHost, std::move(aExtraOpts)) {}

 protected:
  virtual RefPtr<ProcessHandlePromise> DoLaunch() override;
  RefPtr<ProcessHandlePromise> LaunchAndroidService(
      const GeckoProcessType aType, const geckoargs::ChildProcessArgs& args);
};
typedef AndroidProcessLauncher ProcessLauncher;
// NB: Technically Android is linux (i.e. XP_LINUX is defined), but we want
// orthogonal IPC machinery there. Conversely, there are tier-3 non-Linux
// platforms (BSD and Solaris) where we want the "linux" IPC machinery. So
// we use MOZ_WIDGET_* to choose the platform backend.
#  elif defined(MOZ_WIDGET_GTK)
class LinuxProcessLauncher : public PosixProcessLauncher {
 public:
  LinuxProcessLauncher(GeckoChildProcessHost* aHost,
                       geckoargs::ChildProcessArgs&& aExtraOpts)
      : PosixProcessLauncher(aHost, std::move(aExtraOpts)) {}

 protected:
  virtual Result<Ok, LaunchError> DoSetup() override;
};
typedef LinuxProcessLauncher ProcessLauncher;
#  elif defined(MOZ_WIDGET_UIKIT)
class IosProcessLauncher : public PosixProcessLauncher {
 public:
  IosProcessLauncher(GeckoChildProcessHost* aHost,
                     geckoargs::ChildProcessArgs&& aExtraOpts)
      : PosixProcessLauncher(aHost, std::move(aExtraOpts)) {}

 protected:
  virtual RefPtr<ProcessHandlePromise> DoLaunch() override;

  DarwinObjectPtr<xpc_object_t> mBootstrapMessage;
};
typedef IosProcessLauncher ProcessLauncher;
#  else
#    error "Unknown platform"
#  endif
#endif  // XP_UNIX

using base::ProcessHandle;
using mozilla::ipc::BaseProcessLauncher;
using mozilla::ipc::ProcessLauncher;

mozilla::StaticAutoPtr<mozilla::LinkedList<GeckoChildProcessHost>>
    GeckoChildProcessHost::sGeckoChildProcessHosts;

mozilla::StaticMutex GeckoChildProcessHost::sMutex;

GeckoChildProcessHost::GeckoChildProcessHost(GeckoProcessType aProcessType,
                                             bool aIsFileContent)
    : mProcessType(aProcessType),
      mChildID(++gChildCounter),
      mIsFileContent(aIsFileContent),
      mMonitor("mozilla.ipc.GeckoChildProcessHost.mMonitor"),
      mLaunchOptions(MakeUnique<base::LaunchOptions>()),
      mInitialChannelId(nsID::GenerateUUID()),
      mProcessState(CREATING_CHANNEL),
#ifdef XP_WIN
      mGroupId(u"-"),
#endif
#if defined(MOZ_SANDBOX) && defined(XP_WIN)
      mEnableSandboxLogging(false),
      mSandboxLevel(0),
#endif
      mHandleLock("mozilla.ipc.GeckoChildProcessHost.mHandleLock"),
      mChildProcessHandle(0),
#if defined(XP_DARWIN)
      mChildTask(MACH_PORT_NULL),
#endif
#if defined(MOZ_SANDBOX) && defined(XP_MACOSX)
      mDisableOSActivityMode(false),
#endif
      mDestroying(false) {
  MOZ_COUNT_CTOR(GeckoChildProcessHost);
  MOZ_RELEASE_ASSERT(mChildID > 0, "gChildCounter overflowed");
  StaticMutexAutoLock lock(sMutex);
  if (!sGeckoChildProcessHosts) {
    sGeckoChildProcessHosts = new mozilla::LinkedList<GeckoChildProcessHost>();
  }
  sGeckoChildProcessHosts->insertBack(this);
#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
  if (aProcessType == GeckoProcessType_RDD) {
    // The RDD process makes limited use of EGL.  If Mesa's shader
    // cache is enabled and the directory isn't explicitly set, then
    // it will try to getpwuid() the user which can cause problems
    // with sandboxing.  Because we shouldn't need shader caching in
    // this process, we just disable the cache to prevent that.
    mLaunchOptions->env_map["MESA_GLSL_CACHE_DISABLE"] = "true";
    mLaunchOptions->env_map["MESA_SHADER_CACHE_DISABLE"] = "true";
    // In case the nvidia driver is also loaded:
    mLaunchOptions->env_map["__GL_SHADER_DISK_CACHE"] = "0";
  }
#endif
}

GeckoChildProcessHost::~GeckoChildProcessHost() {
  AssertIOThread();
  MOZ_RELEASE_ASSERT(mDestroying);

  MOZ_COUNT_DTOR(GeckoChildProcessHost);

  {
    mozilla::AutoWriteLock hLock(mHandleLock);
#if defined(XP_DARWIN)
    if (mChildTask != MACH_PORT_NULL) {
      mach_port_deallocate(mach_task_self(), mChildTask);
    }
#endif
#if defined(XP_IOS)
    if (mForegroundCapabilityGrant) {
      mForegroundCapabilityGrant.reset();
    }
    if (mExtensionKitProcess) {
      mExtensionKitProcess->Invalidate();
    }
    if (mXPCConnection) {
      xpc_connection_cancel(mXPCConnection.get());
    }
#endif

    if (mChildProcessHandle != 0) {
      ProcessWatcher::EnsureProcessTerminated(
          mChildProcessHandle
#ifdef NS_FREE_PERMANENT_DATA
          // If we're doing leak logging, shutdown can be slow.
          ,
          false  // don't "force"
#endif
      );
      mChildProcessHandle = 0;
    }
  }
}

base::ProcessHandle GeckoChildProcessHost::GetChildProcessHandle() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  return mChildProcessHandle;
}

base::ProcessId GeckoChildProcessHost::GetChildProcessId() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  if (!mChildProcessHandle) {
    return 0;
  }
  return base::GetProcId(mChildProcessHandle);
}

#ifdef XP_DARWIN
task_t GeckoChildProcessHost::GetChildTask() {
  mozilla::AutoReadLock handleLock(mHandleLock);
  return mChildTask;
}
#endif

void GeckoChildProcessHost::RemoveFromProcessList() {
  StaticMutexAutoLock lock(sMutex);
  if (!sGeckoChildProcessHosts) {
    return;
  }
  LinkedListElement<GeckoChildProcessHost>::removeFrom(
      *sGeckoChildProcessHosts);
}

void GeckoChildProcessHost::Destroy() {
  MOZ_RELEASE_ASSERT(!mDestroying);
  // We can remove from the list before it's really destroyed
  RemoveFromProcessList();
  RefPtr<ProcessHandlePromise> whenReady = mHandlePromise;

  if (!whenReady) {
    // AsyncLaunch not called yet, so dispatch immediately.
    whenReady = ProcessHandlePromise::CreateAndReject(
        LaunchError("DestroyEarly"), __func__);
  }

  using Value = ProcessHandlePromise::ResolveOrRejectValue;
  mDestroying = true;
  whenReady->Then(XRE_GetAsyncIOEventTarget(), __func__,
                  [this](const Value&) { delete this; });
}

// static
mozilla::BinPathType BaseProcessLauncher::GetPathToBinary(
    FilePath& exePath, GeckoProcessType processType) {
  exePath = {};
  BinPathType pathType = XRE_GetChildProcBinPathType(processType);

  if (pathType == BinPathType::Self) {
#if defined(XP_WIN)
    wchar_t exePathBuf[MAXPATHLEN];
    if (!::GetModuleFileNameW(nullptr, exePathBuf, MAXPATHLEN)) {
      MOZ_CRASH("GetModuleFileNameW failed (FIXME)");
    }
    exePath = FilePath::FromWStringHack(exePathBuf);
#else
    exePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
#endif
    return pathType;
  }

#ifdef MOZ_WIDGET_COCOA
  // The GMP child process runs via the Media Plugin Helper executable
  // which is a clone of plugin-container allowing for GMP-specific
  // codesigning entitlements.
  nsCString bundleName;
  std::string executableLeafName;
  if (processType == GeckoProcessType_GMPlugin &&
      mozilla::StaticPrefs::media_plugin_helper_process_enabled()) {
    bundleName = MOZ_EME_PROCESS_BUNDLENAME;
    executableLeafName = MOZ_EME_PROCESS_NAME_BRANDED;
  } else {
    bundleName = MOZ_CHILD_PROCESS_BUNDLENAME;
    executableLeafName = MOZ_CHILD_PROCESS_NAME;
  }
#endif

  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
#ifdef XP_WIN
    exePath = FilePath(char16ptr_t(gGREBinPath));
#elif MOZ_WIDGET_COCOA
    nsCOMPtr<nsIFile> childProcPath;
    if (NS_SUCCEEDED(NS_NewLocalFile(nsDependentString(gGREBinPath),
                                     getter_AddRefs(childProcPath)))) {
      // We need to use an App Bundle on OS X so that we can hide
      // the dock icon. See Bug 557225.
      if (NS_SUCCEEDED(childProcPath->AppendNative(bundleName)) &&
          NS_SUCCEEDED(childProcPath->AppendNative("Contents"_ns)) &&
          NS_SUCCEEDED(childProcPath->AppendNative("MacOS"_ns))) {
        nsCString tempCPath;
        if (NS_SUCCEEDED(childProcPath->GetNativePath(tempCPath))) {
          exePath = FilePath(tempCPath.get());
        }
      }
    }
#else
    nsCString path;
    if (NS_SUCCEEDED(
            NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path))) {
      exePath = FilePath(path.get());
    }
#endif
  }

  if (exePath.empty()) {
#ifdef XP_WIN
    exePath =
        FilePath::FromWStringHack(CommandLine::ForCurrentProcess()->program());
#else
    exePath = FilePath(CommandLine::ForCurrentProcess()->argv()[0]);
#endif
    exePath = exePath.DirName();
  }

#ifdef MOZ_WIDGET_COCOA
  exePath = exePath.Append(executableLeafName);
#else
  exePath = exePath.AppendASCII(MOZ_CHILD_PROCESS_NAME);
#endif

  return pathType;
}

#ifdef MOZ_WIDGET_COCOA
class AutoCFTypeObject {
 public:
  explicit AutoCFTypeObject(CFTypeRef object) { mObject = object; }
  ~AutoCFTypeObject() { ::CFRelease(mObject); }

 private:
  CFTypeRef mObject;
};
#endif

// We start the unique IDs at 1 so that 0 can be used to mean that
// a component has no unique ID assigned to it.
uint32_t GeckoChildProcessHost::sNextUniqueID = 1;

/* static */
uint32_t GeckoChildProcessHost::GetUniqueID() { return sNextUniqueID++; }

/* static */
void GeckoChildProcessHost::SetEnv(const char* aKey, const char* aValue) {
  MOZ_ASSERT(mLaunchOptions);
  mLaunchOptions->env_map[ENVIRONMENT_STRING(aKey)] =
      ENVIRONMENT_STRING(aValue);
}

bool GeckoChildProcessHost::PrepareLaunch(
    geckoargs::ChildProcessArgs& aExtraOpts) {
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  if (!SandboxLaunch::Configure(mProcessType, mSandbox, aExtraOpts,
                                mLaunchOptions.get())) {
    return false;
  }
#endif

#ifdef XP_WIN

#  if defined(MOZ_SANDBOX)
  // We need to get the pref here as the process is launched off main thread.
  if (mProcessType == GeckoProcessType_Content) {
    // Win32k Lockdown state must be initialized on the main thread.
    // This is our last chance to do it before it is read on the IPC Launch
    // thread
    GetWin32kLockdownState();
    mSandboxLevel = GetEffectiveContentSandboxLevel();
    mEnableSandboxLogging =
        Preferences::GetBool("security.sandbox.logging.enabled");

    // We currently have to whitelist certain paths for tests to work in some
    // development configurations.
    nsAutoString readPaths;
    nsresult rv = Preferences::GetString(
        "security.sandbox.content.read_path_whitelist", readPaths);
    if (NS_SUCCEEDED(rv)) {
      for (const nsAString& readPath : readPaths.Split(',')) {
        nsString trimmedPath(readPath);
        trimmedPath.Trim(" ", true, true);
        std::wstring resolvedPath(trimmedPath.Data());
        // Check if path ends with '\' as this indicates we want to give read
        // access to a directory and so it needs a wildcard.
        if (resolvedPath.back() == L'\\') {
          resolvedPath.append(L"*");
        }
        mAllowedFilesRead.push_back(resolvedPath);
      }
    }
  }
#  endif

#  if defined(MOZ_SANDBOX)
  // For other process types we can't rely on them being launched on main
  // thread and they may not have access to prefs in the child process, so allow
  // them to turn on logging via an environment variable.
  mEnableSandboxLogging =
      mEnableSandboxLogging || !!PR_GetEnv("MOZ_SANDBOX_LOGGING");

#  endif
#elif defined(XP_MACOSX)
#  if defined(MOZ_SANDBOX)
  if (ShouldHaveDirectoryService() &&
      mProcessType != GeckoProcessType_GMPlugin) {
    mozilla::Unused << NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                              getter_AddRefs(mProfileDir));
  }
#  endif
#endif

  return true;
}

#ifdef XP_WIN
void GeckoChildProcessHost::InitWindowsGroupID() {
  // On Win7+, pass the application user model to the child, so it can
  // register with it. This insures windows created by the container
  // properly group with the parent app on the Win7 taskbar.
  nsCOMPtr<nsIWinTaskbar> taskbarInfo = do_GetService(NS_TASKBAR_CONTRACTID);
  if (taskbarInfo) {
    bool isSupported = false;
    taskbarInfo->GetAvailable(&isSupported);
    nsAutoString appId;
    if (isSupported && NS_SUCCEEDED(taskbarInfo->GetDefaultGroupId(appId))) {
      MOZ_ASSERT(mGroupId.EqualsLiteral("-"));
      mGroupId.Assign(appId);
    }
  }
}
#endif

bool GeckoChildProcessHost::SyncLaunch(geckoargs::ChildProcessArgs aExtraOpts,
                                       int aTimeoutMs) {
  if (!AsyncLaunch(std::move(aExtraOpts))) {
    return false;
  }
  return WaitUntilConnected(aTimeoutMs);
}

// Note: for most process types, we currently call AsyncLaunch, and therefore
// the *ProcessLauncher constructor, on the main thread, while the
// ProcessLauncher methods to actually execute the launch are called on the IO
// or IPC launcher thread. GMP processes are an exception - the GMP code
// invokes GeckoChildProcessHost from non-main-threads, and therefore we cannot
// rely on having access to mainthread-only services (like the directory
// service) from this code if we're launching that type of process.
bool GeckoChildProcessHost::AsyncLaunch(
    geckoargs::ChildProcessArgs aExtraOpts) {
  if (!PrepareLaunch(aExtraOpts)) {
    return false;
  }

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  if (IsMacSandboxLaunchEnabled() &&
      !AppendMacSandboxParams(aExtraOpts.mArgs)) {
    return false;
  }
#endif

  RefPtr<BaseProcessLauncher> launcher =
      new ProcessLauncher(this, std::move(aExtraOpts));
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  launcher->SetLaunchArchitecture(mLaunchArch);
#endif

  // Note: Destroy() waits on mHandlePromise to delete |this|. As such, we want
  // to be sure that all of our post-launch processing on |this| happens before
  // mHandlePromise notifies.
  MOZ_ASSERT(mHandlePromise == nullptr);
  mHandlePromise =
      mozilla::InvokeAsync<GeckoChildProcessHost*>(
          XRE_GetAsyncIOEventTarget(), launcher.get(), __func__,
          &BaseProcessLauncher::Launch, this)
          ->Then(
              XRE_GetAsyncIOEventTarget(), __func__,
              [this](LaunchResults&& aResults) {
                {
                  {
                    mozilla::AutoWriteLock handleLock(mHandleLock);
                    if (!OpenPrivilegedHandle(base::GetProcId(aResults.mHandle))
#ifdef XP_WIN
                        // If we failed in opening the process handle, try
                        // harder by duplicating one.
                        && !::DuplicateHandle(
                               ::GetCurrentProcess(), aResults.mHandle,
                               ::GetCurrentProcess(), &mChildProcessHandle,
                               PROCESS_DUP_HANDLE | PROCESS_TERMINATE |
                                   PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                   SYNCHRONIZE,
                               FALSE, 0)
#endif  // XP_WIN
                    ) {
                      MOZ_CRASH("cannot open handle to child process");
                    }
                    // The original handle is no longer needed; it must
                    // be closed to prevent a resource leak.
                    base::CloseProcessHandle(aResults.mHandle);
                    // FIXME (bug 1720523): define a cross-platform
                    // "safe" invalid value to use in places like this.
                    aResults.mHandle = 0;

#ifdef XP_DARWIN
                    this->mChildTask = aResults.mChildTask;
#endif
#ifdef XP_IOS
                    this->mExtensionKitProcess = aResults.mExtensionKitProcess;
                    this->mXPCConnection = aResults.mXPCConnection;
                    this->mForegroundCapabilityGrant =
                        std::move(aResults.mForegroundCapabilityGrant);
#endif

                    if (mNodeChannel) {
                      mNodeChannel->SetOtherPid(
                          base::GetProcId(this->mChildProcessHandle));
#ifdef XP_DARWIN
                      mNodeChannel->SetMachTaskPort(this->mChildTask);
#endif
                    }
                  }
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
                  this->mSandboxBroker = std::move(aResults.mSandboxBroker);
#endif

                  MonitorAutoLock lock(mMonitor);
                  // The OnChannel{Connected,Error} may have already advanced
                  // the state.
                  if (mProcessState < PROCESS_CREATED) {
                    mProcessState = PROCESS_CREATED;
                  }
                  lock.Notify();
                }
                return ProcessHandlePromise::CreateAndResolve(
                    GetChildProcessHandle(), __func__);
              },
              [this](const LaunchError aError) {
                // WaitUntilConnected might be waiting for us to signal.
                // If something failed let's set the error state and notify.
                CHROMIUM_LOG(ERROR)
                    << "Failed to launch "
                    << XRE_GeckoProcessTypeToString(mProcessType)
                    << " subprocess @" << aError.FunctionName()
                    << " (Error:" << aError.ErrorCode() << ")";
                glean::subprocess::launch_failure
                    .Get(nsDependentCString(
                        XRE_GeckoProcessTypeToString(mProcessType)))
                    .Add(1);
                nsCString telemetryKey = nsPrintfCString(
#if defined(XP_WIN)
                    "%s,0x%lx,%s",
#else
                    "%s,%ld,%s",
#endif
                    aError.FunctionName().get(), aError.ErrorCode(),
                    XRE_GeckoProcessTypeToString(mProcessType));
                // Max telemetry key is 72 chars
                // https://searchfox.org/mozilla-central/rev/c244b16815d1fc827d141472b9faac5610f250e7/toolkit/components/telemetry/core/TelemetryScalar.cpp#105
                if (telemetryKey.Length() > 72) {
                  NS_WARNING(nsPrintfCString("Truncating telemetry key: %s",
                                             telemetryKey.get())
                                 .get());
                  telemetryKey.Truncate(72);
                }
                glean::dom_parentprocess::process_launch_errors
                    .Get(telemetryKey)
                    .Add(1);
                {
                  MonitorAutoLock lock(mMonitor);
                  mProcessState = PROCESS_ERROR;
                  lock.Notify();
                }
                return ProcessHandlePromise::CreateAndReject(aError, __func__);
              });
  return true;
}

bool GeckoChildProcessHost::WaitUntilConnected(int32_t aTimeoutMs) {
  AUTO_PROFILER_LABEL("GeckoChildProcessHost::WaitUntilConnected", OTHER);

  // NB: this uses a different mechanism than the chromium parent
  // class.
  TimeDuration timeout = (aTimeoutMs > 0)
                             ? TimeDuration::FromMilliseconds(aTimeoutMs)
                             : TimeDuration::Forever();

  MonitorAutoLock lock(mMonitor);
  TimeStamp waitStart = TimeStamp::Now();
  TimeStamp current;

  // We'll receive several notifications, we need to exit when we
  // have either successfully launched or have timed out.
  while (mProcessState != PROCESS_CONNECTED) {
    // If there was an error then return it, don't wait out the timeout.
    if (mProcessState == PROCESS_ERROR) {
      break;
    }

    CVStatus status = lock.Wait(timeout);
    if (status == CVStatus::Timeout) {
      break;
    }

    if (timeout != TimeDuration::Forever()) {
      current = TimeStamp::Now();
      timeout -= current - waitStart;
      waitStart = current;
    }
  }

  return mProcessState == PROCESS_CONNECTED;
}

bool GeckoChildProcessHost::WaitForProcessHandle() {
  MonitorAutoLock lock(mMonitor);
  while (mProcessState < PROCESS_CREATED) {
    lock.Wait();
  }
  MOZ_ASSERT(mProcessState == PROCESS_ERROR || GetChildProcessHandle());

  return mProcessState < PROCESS_ERROR;
}

bool GeckoChildProcessHost::LaunchAndWaitForProcessHandle(
    geckoargs::ChildProcessArgs aExtraOpts) {
  if (!AsyncLaunch(std::move(aExtraOpts))) {
    return false;
  }
  return WaitForProcessHandle();
}

void GeckoChildProcessHost::InitializeChannel(
    IPC::Channel::ChannelHandle&& aServerHandle) {
  // Create the IPC channel which will be used for communication with this
  // process.
  mozilla::UniquePtr<IPC::Channel> channel = MakeUnique<IPC::Channel>(
      std::move(aServerHandle), IPC::Channel::MODE_SERVER,
      base::kInvalidProcessId);
#if defined(XP_WIN)
  channel->StartAcceptingHandles(IPC::Channel::MODE_SERVER);
#elif defined(XP_DARWIN)
  channel->StartAcceptingMachPorts(IPC::Channel::MODE_SERVER);
#endif

  mNodeController = NodeController::GetSingleton();
  std::tie(mInitialPort, mNodeChannel) =
      mNodeController->InviteChildProcess(std::move(channel), this);

  MonitorAutoLock lock(mMonitor);
  mProcessState = CHANNEL_INITIALIZED;
  lock.Notify();
}

void GeckoChildProcessHost::SetAlreadyDead() {
  mozilla::AutoWriteLock handleLock(mHandleLock);
  if (mChildProcessHandle &&
      mChildProcessHandle != base::kInvalidProcessHandle) {
    base::CloseProcessHandle(mChildProcessHandle);
  }

  mChildProcessHandle = 0;
}

void BaseProcessLauncher::GetChildLogName(const char* origLogName,
                                          nsACString& buffer) {
#ifdef XP_WIN
  // On Windows we must expand relative paths because sandboxing rules
  // bound only to full paths.  fopen fowards to NtCreateFile which checks
  // the path against the sanboxing rules as passed to fopen (left relative).
  char absPath[MAX_PATH + 2];
  if (_fullpath(absPath, origLogName, sizeof(absPath))) {
    buffer.Append(absPath);
  } else
#endif
  {
    buffer.Append(origLogName);
  }

  // Remove .moz_log extension to avoid its duplication, it will be added
  // automatically by the logging backend
  static constexpr auto kMozLogExt = nsLiteralCString{MOZ_LOG_FILE_EXTENSION};
  if (StringEndsWith(buffer, kMozLogExt)) {
    buffer.Truncate(buffer.Length() - kMozLogExt.Length());
  }

  // Append child-specific postfix to name
  buffer.AppendLiteral(".child-");
  buffer.AppendASCII(mChildIDString);
}

// Windows needs a single dedicated thread for process launching,
// because of thread-safety restrictions/assertions in the sandbox
// code.
//
// Android also needs a single dedicated thread to simplify thread
// safety in java.
//
// Fork server needs a dedicated thread for accessing
// |ForkServiceChild|.
#if defined(XP_WIN) || defined(MOZ_WIDGET_ANDROID) || \
    defined(MOZ_ENABLE_FORKSERVER)

static mozilla::StaticMutex gIPCLaunchThreadMutex;
static mozilla::StaticRefPtr<nsIThread> gIPCLaunchThread
    MOZ_GUARDED_BY(gIPCLaunchThreadMutex);

class IPCLaunchThreadObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
 protected:
  virtual ~IPCLaunchThreadObserver() = default;
};

NS_IMPL_ISUPPORTS(IPCLaunchThreadObserver, nsIObserver, nsISupports)

NS_IMETHODIMP
IPCLaunchThreadObserver::Observe(nsISupports* aSubject, const char* aTopic,
                                 const char16_t* aData) {
  MOZ_RELEASE_ASSERT(strcmp(aTopic, "xpcom-shutdown-threads") == 0);

  nsCOMPtr<nsIThread> thread;
  {
    StaticMutexAutoLock lock(gIPCLaunchThreadMutex);
    thread = gIPCLaunchThread.forget();
  }

  nsresult rv = thread ? thread->Shutdown() : NS_OK;
  mozilla::Unused << NS_WARN_IF(NS_FAILED(rv));
  return rv;
}

nsCOMPtr<nsIEventTarget> GetIPCLauncher() {
  StaticMutexAutoLock lock(gIPCLaunchThreadMutex);
  if (!gIPCLaunchThread) {
    nsCOMPtr<nsIThread> thread;
    nsresult rv = NS_NewNamedThread("IPC Launch"_ns, getter_AddRefs(thread));
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("GeckoChildProcessHost::GetIPCLauncher", [] {
            nsCOMPtr<nsIObserverService> obsService =
                mozilla::services::GetObserverService();
            nsCOMPtr<nsIObserver> obs = new IPCLaunchThreadObserver();
            obsService->AddObserver(obs, "xpcom-shutdown-threads", false);
          }));
      gIPCLaunchThread = thread.forget();
    }
  }

  nsCOMPtr<nsIEventTarget> thread = gIPCLaunchThread.get();
  MOZ_DIAGNOSTIC_ASSERT(thread);
  return thread;
}

#else  // defined(XP_WIN) || defined(MOZ_WIDGET_ANDROID) ||
       // defined(MOZ_ENABLE_FORKSERVER)

// Other platforms use an on-demand thread pool.

nsCOMPtr<nsIEventTarget> GetIPCLauncher() {
  nsCOMPtr<nsIEventTarget> pool =
      mozilla::SharedThreadPool::Get("IPC Launch"_ns);
  MOZ_DIAGNOSTIC_ASSERT(pool);
  return pool;
}

#endif  // XP_WIN || MOZ_WIDGET_ANDROID || MOZ_ENABLE_FORKSERVER

void
#if defined(XP_WIN)
AddAppDirToCommandLine(CommandLine& aCmdLine, nsIFile* aAppDir)
#else
AddAppDirToCommandLine(geckoargs::ChildProcessArgs& aCmdLine,
                       nsIFile* aAppDir, nsIFile* aProfileDir)
#endif
{
  // Content processes need access to application resources, so pass
  // the full application directory path to the child process.
  if (aAppDir) {
#if defined(XP_WIN)
    nsString path;
    MOZ_ALWAYS_SUCCEEDS(aAppDir->GetPath(path));
    aCmdLine.AppendLooseValue(UTF8ToWide(geckoargs::sAppDir.Name()));
    std::wstring wpath(path.get());
    aCmdLine.AppendLooseValue(wpath);
#else
    nsAutoCString path;
    MOZ_ALWAYS_SUCCEEDS(aAppDir->GetNativePath(path));
    geckoargs::sAppDir.Put(path.get(), aCmdLine);
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
    // Full path to the profile dir
    if (aProfileDir) {
      // If the profile doesn't exist, normalization will
      // fail. But we don't return an error here because some
      // tests require startup with a missing profile dir.
      // For users, almost universally, the profile will be in
      // the home directory and normalization isn't required.
      mozilla::Unused << aProfileDir->Normalize();
      nsAutoCString path;
      MOZ_ALWAYS_SUCCEEDS(aProfileDir->GetNativePath(path));
      geckoargs::sProfile.Put(path.get(), aCmdLine);
    }
#endif
  }
}

#if defined(XP_WIN) && (defined(MOZ_SANDBOX) || defined(_ARM64_))
static bool Contains(const geckoargs::ChildProcessArgs& aExtraOpts,
                     const char* aValue) {
  return std::any_of(aExtraOpts.mArgs.begin(), aExtraOpts.mArgs.end(),
                     [&](const std::string arg) {
                       return arg.find(aValue) != std::string::npos;
                     });
}
#endif  // defined(XP_WIN) && (defined(MOZ_SANDBOX) || defined(_ARM64_))

RefPtr<ProcessLaunchPromise> BaseProcessLauncher::PerformAsyncLaunch() {
  Result<Ok, LaunchError> aError = DoSetup();
  if (aError.isErr()) {
    return ProcessLaunchPromise::CreateAndReject(aError.unwrapErr(), __func__);
  }
  RefPtr<BaseProcessLauncher> self = this;
  return DoLaunch()->Then(
      mLaunchThread, __func__,
      [self](base::ProcessHandle aHandle) {
        self->mResults.mHandle = aHandle;
        return self->FinishLaunch();
      },
      [](LaunchError aError) {
        return ProcessLaunchPromise::CreateAndReject(aError, __func__);
      });
}

Result<Ok, LaunchError> BaseProcessLauncher::DoSetup() {
  RefPtr<BaseProcessLauncher> self = this;
  GetProfilerEnvVarsForChildProcess([self](const char* key, const char* value) {
    self->mLaunchOptions->env_map[ENVIRONMENT_STRING(key)] =
        ENVIRONMENT_STRING(value);
  });
#ifdef MOZ_MEMORY
  if (mProcessType == GeckoProcessType_Content) {
    nsAutoCString mallocOpts(PR_GetEnv("MALLOC_OPTIONS"));
    // Disable randomization of small arenas in content.
    mallocOpts.Append("r");
    self->mLaunchOptions->env_map[ENVIRONMENT_LITERAL("MALLOC_OPTIONS")] =
        ENVIRONMENT_STRING(mallocOpts.get());
  }
#endif

  MapChildLogging();

  geckoargs::sInitialChannelID.Put(mInitialChannelIdString, mChildArgs);

  geckoargs::sParentPid.Put(static_cast<uint64_t>(base::GetCurrentProcId()),
                            mChildArgs);

  if (!CrashReporter::IsDummy() && CrashReporter::GetEnabled() &&
      mProcessType != GeckoProcessType_ForkServer) {
#if defined(MOZ_WIDGET_COCOA) || defined(XP_WIN)
    geckoargs::sCrashReporter.Put(CrashReporter::GetChildNotificationPipe(),
                                  mChildArgs);
#elif defined(XP_UNIX) && !defined(XP_IOS)
    UniqueFileHandle childCrashFd = CrashReporter::GetChildNotificationPipe();
    if (!childCrashFd) {
      return Err(LaunchError("DuplicateFileHandle failed"));
    }
    geckoargs::sCrashReporter.Put(std::move(childCrashFd), mChildArgs);

#  if !defined(MOZ_WIDGET_ANDROID)
    CrashReporter::ProcessId pid = CrashReporter::GetCrashHelperPid();
    if (pid != base::kInvalidProcessId) {
      geckoargs::sCrashHelperPid.Put(pid, mChildArgs);
    }
#  endif  // !defined(MOZ_WIDGET_ANDROID)
#endif    // XP_UNIX && !XP_IOS
  }

  return Ok();
}

void BaseProcessLauncher::MapChildLogging() {
  const char* origNSPRLogName = PR_GetEnv("NSPR_LOG_FILE");
  const char* origMozLogName = PR_GetEnv("MOZ_LOG_FILE");

  if (origNSPRLogName) {
    nsAutoCString nsprLogName;
    GetChildLogName(origNSPRLogName, nsprLogName);
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("NSPR_LOG_FILE")] =
        ENVIRONMENT_STRING(nsprLogName.get());
  }
  if (origMozLogName) {
    nsAutoCString mozLogName;
    GetChildLogName(origMozLogName, mozLogName);
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("MOZ_LOG_FILE")] =
        ENVIRONMENT_STRING(mozLogName.get());
  }

  // `RUST_LOG_CHILD` is meant for logging child processes only.
  nsAutoCString childRustLog(PR_GetEnv("RUST_LOG_CHILD"));
  if (!childRustLog.IsEmpty()) {
    mLaunchOptions->env_map[ENVIRONMENT_LITERAL("RUST_LOG")] =
        ENVIRONMENT_STRING(childRustLog.get());
  }
}

Result<Ok, LaunchError> BaseProcessLauncher::DoFinishLaunch() {
  // We're in the parent and the child was launched. Clean up any FDs which were
  // transferred to the child in the parent as soon as possible, which will
  // allow the parent to detect when the child closes its handle (either due to
  // normal exit or due to crash).
  mChildArgs.mFiles.clear();

  return Ok();
}

#if defined(MOZ_WIDGET_GTK)
Result<Ok, LaunchError> LinuxProcessLauncher::DoSetup() {
  Result<Ok, LaunchError> aError = PosixProcessLauncher::DoSetup();
  if (aError.isErr()) {
    return aError;
  }

  if (mProcessType == GeckoProcessType_Content) {
    // disable IM module to avoid sandbox violation
    mLaunchOptions->env_map["GTK_IM_MODULE"] = "gtk-im-context-simple";

    // Disable ATK accessibility code in content processes because it conflicts
    // with the sandbox, and we proxy that information through the main process
    // anyway.
    mLaunchOptions->env_map["NO_AT_BRIDGE"] = "1";
  }

  return Ok();
}
#endif  // MOZ_WIDGET_GTK

#ifdef XP_UNIX
Result<Ok, LaunchError> PosixProcessLauncher::DoSetup() {
  Result<Ok, LaunchError> aError = BaseProcessLauncher::DoSetup();
  if (aError.isErr()) {
    return aError;
  }

  // XPCOM may not be initialized in some subprocesses.  We don't want
  // to initialize XPCOM just for the directory service, especially
  // since LD_LIBRARY_PATH is already set correctly in subprocesses
  // (meaning that we don't need to set that up in the environment).
  if (ShouldHaveDirectoryService()) {
    MOZ_ASSERT(gGREBinPath);
    nsCString path;
    NS_CopyUnicodeToNative(nsDependentString(gGREBinPath), path);
#  if defined(XP_LINUX) || defined(__DragonFly__) || defined(XP_FREEBSD) || \
      defined(XP_NETBSD) || defined(XP_OPENBSD)
    const char* ld_library_path = PR_GetEnv("LD_LIBRARY_PATH");
    nsCString new_ld_lib_path(path.get());

    if (ld_library_path && *ld_library_path) {
      new_ld_lib_path.Append(':');
      new_ld_lib_path.Append(ld_library_path);
    }
    mLaunchOptions->env_map["LD_LIBRARY_PATH"] = new_ld_lib_path.get();

#  elif XP_MACOSX
    // With signed production Mac builds, the dynamic linker (dyld) will
    // ignore dyld environment variables preventing the use of variables
    // such as DYLD_LIBRARY_PATH and DYLD_INSERT_LIBRARIES.

    // If we're running with gtests, add the gtest XUL ahead of normal XUL on
    // the DYLD_LIBRARY_PATH so that plugin-container.app loads it instead.
    nsCString new_dyld_lib_path(path.get());
    if (PR_GetEnv("MOZ_RUN_GTEST")) {
      new_dyld_lib_path = path + "/gtest:"_ns + new_dyld_lib_path;
      mLaunchOptions->env_map["DYLD_LIBRARY_PATH"] = new_dyld_lib_path.get();
    }

    // DYLD_INSERT_LIBRARIES is currently unused by default but we allow
    // it to be set by the external environment.
    const char* interpose = PR_GetEnv("DYLD_INSERT_LIBRARIES");
    if (interpose && strlen(interpose) > 0) {
      mLaunchOptions->env_map["DYLD_INSERT_LIBRARIES"] = interpose;
    }

    // Prevent connection attempts to diagnosticd(8) to save cycles. Log
    // messages can trigger these connection attempts, but access to
    // diagnosticd is blocked in sandboxed child processes.
#    if defined(MOZ_SANDBOX) && defined(XP_MACOSX)
    if (mDisableOSActivityMode) {
      mLaunchOptions->env_map["OS_ACTIVITY_MODE"] = "disable";
    }
#    endif  // defined(MOZ_SANDBOX)
#  endif
  }

  FilePath exePath;
  BinPathType pathType = GetPathToBinary(exePath, mProcessType);

  // Make sure the executable path is present at the start of our argument list.
  // If we're using BinPathType::Self, also add the `-contentproc` argument.
  if (pathType == BinPathType::Self) {
    std::string args[]{exePath.value(), "-contentproc"};
    mChildArgs.mArgs.insert(mChildArgs.mArgs.begin(), std::begin(args),
                            std::end(args));
  } else {
    mChildArgs.mArgs.insert(mChildArgs.mArgs.begin(), exePath.value());
  }

  if ((mProcessType == GeckoProcessType_Content ||
       mProcessType == GeckoProcessType_ForkServer) &&
      Omnijar::IsInitialized()) {
    // Make sure that child processes can find the omnijar, if they
    // use full XPCOM.  See Omnijar::ChildProcessInit and its callers.
    nsAutoCString path;
    nsCOMPtr<nsIFile> greFile = Omnijar::GetPath(Omnijar::GRE);
    if (greFile && NS_SUCCEEDED(greFile->GetNativePath(path))) {
      geckoargs::sGREOmni.Put(path.get(), mChildArgs);
    }
    nsCOMPtr<nsIFile> appFile = Omnijar::GetPath(Omnijar::APP);
    if (appFile && NS_SUCCEEDED(appFile->GetNativePath(path))) {
      geckoargs::sAppOmni.Put(path.get(), mChildArgs);
    }
  }

  if (mProcessType != GeckoProcessType_GMPlugin) {
    // Add the application directory path (-appdir path)
#  ifdef XP_MACOSX
    AddAppDirToCommandLine(mChildArgs, mAppDir, mProfileDir);
#  else
    AddAppDirToCommandLine(mChildArgs, mAppDir, nullptr);
#  endif
  }

  // XXX Command line params past this point are expected to be at
  // the end of the command line string, and in a specific order.
  // See XRE_InitChildProcess in nsEmbedFunction.

#  ifdef MOZ_WIDGET_COCOA
  {
    auto* thisMac = static_cast<MacProcessLauncher*>(this);
    kern_return_t kr =
        bootstrap_check_in(bootstrap_port, thisMac->mMachConnectionName.c_str(),
                           getter_Transfers(thisMac->mParentRecvPort));
    if (kr != KERN_SUCCESS) {
      CHROMIUM_LOG(ERROR) << "parent bootstrap_check_in failed: "
                          << mach_error_string(kr);
      return Err(LaunchError("bootstrap_check_in", kr));
    }
    mChildArgs.mArgs.push_back(thisMac->mMachConnectionName.c_str());
  }
#  endif  // MOZ_WIDGET_COCOA

  mChildArgs.mArgs.push_back(mChildIDString);

  mChildArgs.mArgs.push_back(ChildProcessType());

#  ifdef MOZ_ENABLE_FORKSERVER
  MOZ_ASSERT(mProcessType != GeckoProcessType_ForkServer ||
                 mChildArgs.mFiles.size() == 1,
             "The ForkServer only expects a single FD argument");
#  endif

#  if !defined(MOZ_WIDGET_ANDROID)
  // Add any files which need to be transferred to fds_to_remap.
  // NOTE: This doesn't transfer ownership of the files out of `mChildArgs`.
  geckoargs::AddToFdsToRemap(mChildArgs, mLaunchOptions->fds_to_remap);
#  endif

  return Ok();
}
#endif  // XP_UNIX

#if defined(MOZ_WIDGET_ANDROID)
RefPtr<ProcessHandlePromise> AndroidProcessLauncher::DoLaunch() {
  return LaunchAndroidService(mProcessType, mChildArgs);
}
#endif  // MOZ_WIDGET_ANDROID

#ifdef XP_UNIX
RefPtr<ProcessHandlePromise> PosixProcessLauncher::DoLaunch() {
  ProcessHandle handle = 0;
  Result<Ok, LaunchError> result = Err(LaunchError{"Launch not attempted"});
#  ifdef MOZ_ENABLE_FORKSERVER
  if (mProcessType != GeckoProcessType_ForkServer && ForkServiceChild::Get()) {
    result = ForkServiceChild::Get()->SendForkNewSubprocess(
        std::move(mChildArgs), std::move(*mLaunchOptions), &handle);
  } else
#  endif
  {
    result =
        base::LaunchApp(mChildArgs.mArgs, std::move(*mLaunchOptions), &handle);
  }

  if (result.isErr()) {
    return ProcessHandlePromise::CreateAndReject(result.unwrapErr(), __func__);
  }
  return ProcessHandlePromise::CreateAndResolve(handle, __func__);
}
#endif  // XP_UNIX

#ifdef XP_IOS
RefPtr<ProcessHandlePromise> IosProcessLauncher::DoLaunch() {
  ExtensionKitProcess::Kind kind = ExtensionKitProcess::Kind::WebContent;
  if (mProcessType == GeckoProcessType_GPU) {
    kind = ExtensionKitProcess::Kind::Rendering;
  } else if (mProcessType == GeckoProcessType_Socket) {
    kind = ExtensionKitProcess::Kind::Networking;
  }

  DarwinObjectPtr<xpc_object_t> bootstrapMessage =
      AdoptDarwinObject(xpc_dictionary_create_empty());
  xpc_dictionary_set_string(bootstrapMessage.get(), "message-name",
                            "bootstrap");

  DarwinObjectPtr<xpc_object_t> environDict =
      AdoptDarwinObject(xpc_dictionary_create_empty());
  for (auto& [envKey, envValue] : mLaunchOptions->env_map) {
    xpc_dictionary_set_string(environDict.get(), envKey.c_str(),
                              envValue.c_str());
  }
  xpc_dictionary_set_value(bootstrapMessage.get(), "environ",
                           environDict.get());

  // Setup stdout and stderr to inherit.
  xpc_dictionary_set_fd(bootstrapMessage.get(), "stdout", STDOUT_FILENO);
  xpc_dictionary_set_fd(bootstrapMessage.get(), "stderr", STDERR_FILENO);

  DarwinObjectPtr<xpc_object_t> argsArray =
      AdoptDarwinObject(xpc_array_create_empty());
  for (auto& argv : mChildArgs.mArgs) {
    xpc_array_set_string(argsArray.get(), XPC_ARRAY_APPEND, argv.c_str());
  }
  MOZ_ASSERT(xpc_array_get_count(argsArray.get()) == mChildArgs.mArgs.size());
  xpc_dictionary_set_value(bootstrapMessage.get(), "argv", argsArray.get());

  DarwinObjectPtr<xpc_object_t> fdsArray =
      AdoptDarwinObject(xpc_array_create_empty());
  for (auto& file : mChildArgs.mFiles) {
    xpc_array_set_fd(fdsArray.get(), XPC_ARRAY_APPEND, file.get());
  }
  MOZ_ASSERT(xpc_array_get_count(fdsArray.get()) == mChildArgs.mFiles.size());
  xpc_dictionary_set_value(bootstrapMessage.get(), "fds", fdsArray.get());

  DarwinObjectPtr<xpc_object_t> sendRightsArray =
      AdoptDarwinObject(xpc_array_create_empty());
  for (auto& sendRight : mChildArgs.mSendRights) {
    // NOTE: As iOS doesn't expose an xpc_array_set_mach_send function, send
    // rights are wrapped with single-key dictionaries.
    DarwinObjectPtr<xpc_object_t> sendRightWrapper =
        AdoptDarwinObject(xpc_dictionary_create_empty());
    xpc_dictionary_set_mach_send(sendRightWrapper.get(), "port",
                                 sendRight.get());
    xpc_array_set_value(sendRightsArray.get(), XPC_ARRAY_APPEND,
                        sendRightWrapper.get());
  }
  MOZ_ASSERT(xpc_array_get_count(sendRightsArray.get()) ==
             mChildArgs.mSendRights.size());
  xpc_dictionary_set_value(bootstrapMessage.get(), "sendRights",
                           sendRightsArray.get());

  auto promise = MakeRefPtr<ProcessHandlePromise::Private>(__func__);
  ExtensionKitProcess::StartProcess(kind, [self = RefPtr{this}, promise,
                                           bootstrapMessage =
                                               std::move(bootstrapMessage)](
                                              Result<ExtensionKitProcess,
                                                     LaunchError>&& result) {
    if (result.isErr()) {
      CHROMIUM_LOG(ERROR) << "ExtensionKitProcess::StartProcess failed";
      promise->Reject(result.unwrapErr(), __func__);
      return;
    }

    auto process = result.unwrap();
    self->mResults.mForegroundCapabilityGrant =
        process.GrantForegroundCapability();
    self->mResults.mXPCConnection = process.MakeLibXPCConnection();
    self->mResults.mExtensionKitProcess = Some(std::move(process));

    // We don't actually use the event handler for anything other than
    // watching for errors. Once the promise is resolved, this becomes a
    // no-op.
    xpc_connection_set_event_handler(self->mResults.mXPCConnection.get(), ^(
                                         xpc_object_t event) {
      if (!event || xpc_get_type(event) == XPC_TYPE_ERROR) {
        CHROMIUM_LOG(WARNING) << "XPC connection received encountered an error";
        promise->Reject(LaunchError("xpc_connection_event_handler"), __func__);
      }
    });
    xpc_connection_resume(self->mResults.mXPCConnection.get());

    // Send our bootstrap message to the content and wait for it to reply with
    // the task port before resolving.
    // FIXME: Should we have a time-out for if the child process doesn't respond
    // in time? The main thread may be blocked while we're starting this
    // process.
    xpc_connection_send_message_with_reply(
        self->mResults.mXPCConnection.get(), bootstrapMessage.get(), nullptr,
        ^(xpc_object_t reply) {
          if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
            CHROMIUM_LOG(ERROR)
                << "Got error sending XPC bootstrap message to child";
            promise->Reject(
                LaunchError("xpc_connection_send_message_with_reply error"),
                __func__);
            return;
          }

          if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
            CHROMIUM_LOG(ERROR)
                << "Unexpected reply type for bootstrap message from child";
            promise->Reject(
                LaunchError(
                    "xpc_connection_send_message_with_reply non-dictionary"),
                __func__);
            return;
          }

          // FIXME: We have to trust the child to tell us its pid & mach task.
          // WebKit uses `xpc_connection_get_pid` to get the pid, however this
          // is marked as unavailable on iOS.
          //
          // Given how the process is started, however, validating this
          // information it sends us this early during startup may be
          // unnecessary.
          self->mResults.mChildTask =
              xpc_dictionary_copy_mach_send(reply, "task");
          pid_t pid =
              static_cast<pid_t>(xpc_dictionary_get_int64(reply, "pid"));
          CHROMIUM_LOG(INFO) << "ExtensionKit process started, task: "
                             << self->mResults.mChildTask << ", pid: " << pid;

          pid_t taskPid;
          kern_return_t kr = pid_for_task(self->mResults.mChildTask, &taskPid);
          if (kr != KERN_SUCCESS || pid != taskPid) {
            CHROMIUM_LOG(ERROR) << "Could not validate child task matches pid";
            promise->Reject(LaunchError("pid_for_task mismatch"), __func__);
            return;
          }

          promise->Resolve(pid, __func__);
        });
  });

  return promise;
}
#endif

#ifdef XP_MACOSX
Result<Ok, LaunchError> MacProcessLauncher::DoFinishLaunch() {
  Result<Ok, LaunchError> aError = PosixProcessLauncher::DoFinishLaunch();
  if (aError.isErr()) {
    return aError;
  }

  MOZ_ASSERT(mParentRecvPort, "should have been configured during DoSetup()");

  // Wait for the child process to send us its 'task_t' data, then send it the
  // mach send/receive rights which are being passed on the commandline.
  const int kTimeoutMs = 10000;
  MOZ_TRY(MachHandleProcessCheckIn(
      mParentRecvPort.get(), base::GetProcId(mResults.mHandle), kTimeoutMs,
      mChildArgs.mSendRights, &mResults.mChildTask));

  return Ok();
}
#endif  // XP_MACOSX

#ifdef XP_WIN
Result<Ok, LaunchError> WindowsProcessLauncher::DoSetup() {
  Result<Ok, LaunchError> aError = BaseProcessLauncher::DoSetup();
  if (aError.isErr()) {
    return aError;
  }

  FilePath exePath;
  BinPathType pathType = GetPathToBinary(exePath, mProcessType);

  mCmdLine.emplace(exePath.ToWStringHack());

  if (pathType == BinPathType::Self) {
    mCmdLine->AppendLooseValue(UTF8ToWide("-contentproc"));
  }

#  ifdef HAS_DLL_BLOCKLIST
  if (IsDynamicBlocklistDisabled(
          gSafeMode,
          CommandLine::ForCurrentProcess()->HasSwitch(UTF8ToWide(
              mozilla::geckoargs::sDisableDynamicDllBlocklist.sMatch)))) {
    mCmdLine->AppendLooseValue(
        UTF8ToWide(mozilla::geckoargs::sDisableDynamicDllBlocklist.sMatch));
  }
#  endif  // HAS_DLL_BLOCKLIST

  for (const std::string& arg : mChildArgs.mArgs) {
    mCmdLine->AppendLooseValue(UTF8ToWide(arg));
  }

#  if defined(MOZ_SANDBOX)
  mResults.mSandboxBroker = MakeUnique<SandboxBroker>();

  // XXX: Bug 1124167: We should get rid of the process specific logic for
  // sandboxing in this class at some point. Unfortunately it will take a bit
  // of reorganizing so I don't think this patch is the right time.
  switch (mProcessType) {
    case GeckoProcessType_Content:
      if (mSandboxLevel > 0) {
        // For now we treat every failure as fatal in
        // SetSecurityLevelForContentProcess and just crash there right away.
        // Should this change in the future then we should also handle the error
        // here.
        mResults.mSandboxBroker->SetSecurityLevelForContentProcess(
            mSandboxLevel, mIsFileContent);
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_IPDLUnitTest:
      // XXX: We don't sandbox this process type yet
      break;
    case GeckoProcessType_GMPlugin:
      if (!PR_GetEnv("MOZ_DISABLE_GMP_SANDBOX")) {
        auto gmpSandboxKind = GMPSandboxKind::Default;
        if (Contains(mChildArgs, "gmp-widevinecdm")) {
          gmpSandboxKind = GMPSandboxKind::Widevine;
        } else if (Contains(mChildArgs, "gmp-clearkey")) {
          gmpSandboxKind = GMPSandboxKind::Clearkey;
        }

        if (NS_WARN_IF(!mResults.mSandboxBroker->SetSecurityLevelForGMPlugin(
                gmpSandboxKind))) {
          return Err(LaunchError("SetSecurityLevelForGMPlugin"));
        }
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_GPU:
      if (mSandboxLevel > 0 && !PR_GetEnv("MOZ_DISABLE_GPU_SANDBOX")) {
        // For now we treat every failure as fatal in
        // SetSecurityLevelForGPUProcess and just crash there right away. Should
        // this change in the future then we should also handle the error here.
        mResults.mSandboxBroker->SetSecurityLevelForGPUProcess(mSandboxLevel);
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_VR:
      if (mSandboxLevel > 0 && !PR_GetEnv("MOZ_DISABLE_VR_SANDBOX")) {
        // TODO: Implement sandbox for VR process, Bug 1430043.
      }
      break;
    case GeckoProcessType_RDD:
      if (!PR_GetEnv("MOZ_DISABLE_RDD_SANDBOX")) {
        if (NS_WARN_IF(
                !mResults.mSandboxBroker->SetSecurityLevelForRDDProcess())) {
          return Err(LaunchError("SetSecurityLevelForRDDProcess"));
        }
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_Socket:
      if (!PR_GetEnv("MOZ_DISABLE_SOCKET_PROCESS_SANDBOX")) {
        if (NS_WARN_IF(
                !mResults.mSandboxBroker->SetSecurityLevelForSocketProcess())) {
          return Err(LaunchError("SetSecurityLevelForSocketProcess"));
        }
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_Utility:
      if (IsUtilitySandboxEnabled(mSandbox)) {
        if (!mResults.mSandboxBroker->SetSecurityLevelForUtilityProcess(
                mSandbox)) {
          return Err(LaunchError("SetSecurityLevelForUtilityProcess"));
        }
        mUseSandbox = true;
      }
      break;
    case GeckoProcessType_Default:
    default:
      MOZ_CRASH("Bad process type in GeckoChildProcessHost");
      break;
  };

  if (mUseSandbox) {
    for (auto it = mAllowedFilesRead.begin(); it != mAllowedFilesRead.end();
         ++it) {
      mResults.mSandboxBroker->AllowReadFile(it->c_str());
    }

    if (mResults.mSandboxBroker->IsWin32kLockedDown()) {
      mCmdLine->AppendLooseValue(
          UTF8ToWide(geckoargs::sWin32kLockedDown.Name()));
    }
  }
#  endif  // defined(MOZ_SANDBOX)

  // Add the application directory path (-appdir path)
  AddAppDirToCommandLine(mCmdLine.ref(), mAppDir);

  // XXX Command line params past this point are expected to be at
  // the end of the command line string, and in a specific order.
  // See XRE_InitChildProcess in nsEmbedFunction.

  // Win app model id
  mCmdLine->AppendLooseValue(mGroupId.get());

  // Gecko child id
  mCmdLine->AppendLooseValue(UTF8ToWide(mChildIDString));

  // Process type
  mCmdLine->AppendLooseValue(UTF8ToWide(ChildProcessType()));

  // Add any files which need to be transferred to handles_to_inherit.
  for (auto& file : mChildArgs.mFiles) {
    mLaunchOptions->handles_to_inherit.push_back(file.get());
  }

#  ifdef MOZ_SANDBOX
  if (mUseSandbox) {
    // Mark the handles to inherit as inheritable.
    for (HANDLE h : mLaunchOptions->handles_to_inherit) {
      mResults.mSandboxBroker->AddHandleToShare(h);
    }
  }
#  endif  // MOZ_SANDBOX

  return Ok();
}

RefPtr<ProcessHandlePromise> WindowsProcessLauncher::DoLaunch() {
  ProcessHandle handle = 0;
#  ifdef MOZ_SANDBOX
  if (mUseSandbox) {
    const IMAGE_THUNK_DATA* cachedNtdllThunk =
        mCachedNtdllThunk ? mCachedNtdllThunk->begin() : nullptr;
    Result<Ok, LaunchError> err = mResults.mSandboxBroker->LaunchApp(
        mCmdLine->program().c_str(), mCmdLine->command_line_string().c_str(),
        mLaunchOptions->env_map, mProcessType, mEnableSandboxLogging,
        cachedNtdllThunk, &handle);
    if (err.isOk()) {
      EnvironmentLog("MOZ_PROCESS_LOG")
          .print("==> process %d launched child process %d (%S)\n",
                 base::GetCurrentProcId(), base::GetProcId(handle),
                 mCmdLine->command_line_string().c_str());
      return ProcessHandlePromise::CreateAndResolve(handle, __func__);
    }
    return ProcessHandlePromise::CreateAndReject(err.unwrapErr(), __func__);
  }
#  endif  // defined(MOZ_SANDBOX)

  Result<Ok, LaunchError> launchErr =
      base::LaunchApp(mCmdLine.ref(), *mLaunchOptions, &handle);
  if (launchErr.isErr()) {
    return ProcessHandlePromise::CreateAndReject(launchErr.unwrapErr(),
                                                 __func__);
  }
  return ProcessHandlePromise::CreateAndResolve(handle, __func__);
}

Result<Ok, LaunchError> WindowsProcessLauncher::DoFinishLaunch() {
  Result<Ok, LaunchError> err = BaseProcessLauncher::DoFinishLaunch();
  if (err.isErr()) {
    return err;
  }

  return Ok();
}
#endif  // XP_WIN

RefPtr<ProcessLaunchPromise> BaseProcessLauncher::FinishLaunch() {
  Result<Ok, LaunchError> aError = DoFinishLaunch();
  if (aError.isErr()) {
    return ProcessLaunchPromise::CreateAndReject(aError.unwrapErr(), __func__);
  }

  MOZ_DIAGNOSTIC_ASSERT(mResults.mHandle);

  glean::process::child_launch.AccumulateRawDuration(TimeStamp::Now() -
                                                     mStartTimeStamp);

  return ProcessLaunchPromise::CreateAndResolve(std::move(mResults), __func__);
}

bool GeckoChildProcessHost::OpenPrivilegedHandle(base::ProcessId aPid) {
  if (mChildProcessHandle) {
    MOZ_ASSERT(aPid == base::GetProcId(mChildProcessHandle));
    return true;
  }

  return base::OpenPrivilegedProcessHandle(aPid, &mChildProcessHandle);
}

void GeckoChildProcessHost::OnChannelConnected(base::ProcessId peer_pid) {
  {
    mozilla::AutoWriteLock hLock(mHandleLock);
    if (!OpenPrivilegedHandle(peer_pid)) {
      MOZ_CRASH("can't open handle to child process");
    }
  }
  MonitorAutoLock lock(mMonitor);
  mProcessState = PROCESS_CONNECTED;
  lock.Notify();
}

RefPtr<ProcessHandlePromise> GeckoChildProcessHost::WhenProcessHandleReady() {
  MOZ_ASSERT(mHandlePromise != nullptr);
  return mHandlePromise;
}

#ifdef MOZ_WIDGET_ANDROID
RefPtr<ProcessHandlePromise> AndroidProcessLauncher::LaunchAndroidService(
    const GeckoProcessType aType, const geckoargs::ChildProcessArgs& args) {
  JNIEnv* const env = mozilla::jni::GetEnvForThread();
  MOZ_ASSERT(env);

  const size_t argvSize = args.mArgs.size();
  jni::ObjectArray::LocalRef jargs =
      jni::ObjectArray::New<jni::String>(argvSize);
  for (size_t ix = 0; ix < argvSize; ix++) {
    jargs->SetElement(ix, jni::StringParam(args.mArgs[ix].c_str(), env));
  }

  std::vector<int> fds(args.mFiles.size());
  for (size_t ix = 0; ix < fds.size(); ix++) {
    fds[ix] = args.mFiles[ix].get();
  }
  jni::IntArray::LocalRef jfds = jni::IntArray::New(fds.data(), fds.size());

  auto type = java::GeckoProcessType::FromInt(aType);
  auto genericResult = java::GeckoProcessManager::Start(type, jargs, jfds);
  auto typedResult = java::GeckoResult::LocalRef(std::move(genericResult));
  return ProcessHandlePromise::FromGeckoResult(typedResult);
}
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool GeckoChildProcessHost::AppendMacSandboxParams(StringVector& aArgs) {
  MacSandboxInfo info;
  if (NS_WARN_IF(!FillMacSandboxInfo(info))) {
    return false;
  }
  info.AppendAsParams(aArgs);
  return true;
}

// Fill |aInfo| with the flags needed to launch the utility sandbox
bool GeckoChildProcessHost::FillMacSandboxInfo(MacSandboxInfo& aInfo) {
  aInfo.type = GetDefaultMacSandboxType();
  aInfo.shouldLog = Preferences::GetBool("security.sandbox.logging.enabled") ||
                    PR_GetEnv("MOZ_SANDBOX_LOGGING");

  nsAutoCString appPath;
  if (!nsMacUtilsImpl::GetAppPath(appPath)) {
    MOZ_CRASH("Failed to get app path");
  }
  aInfo.appPath.assign(appPath.get());
  return true;
}

void GeckoChildProcessHost::DisableOSActivityMode() {
  mDisableOSActivityMode = true;
}

//
// If early sandbox startup is enabled for this process type, map the
// process type to the sandbox type and enable the sandbox. Returns true
// if no errors were encountered or if early sandbox startup is not
// enabled for this process. Returns false if an error was encountered.
//
/* static */
bool GeckoChildProcessHost::StartMacSandbox(int aArgc, char** aArgv,
                                            std::string& aErrorMessage) {
  MacSandboxType sandboxType = MacSandboxType_Invalid;
  switch (XRE_GetProcessType()) {
    // For now, only support early sandbox startup for content,
    // RDD, and GMP processes. Add case statements for the additional
    // process types once early sandbox startup is implemented for them.
    case GeckoProcessType_Content:
      // Content processes don't use GeckoChildProcessHost
      // to configure sandboxing so hard code the sandbox type.
      sandboxType = MacSandboxType_Content;
      break;
    case GeckoProcessType_RDD:
      sandboxType = RDDProcessHost::GetMacSandboxType();
      break;
    case GeckoProcessType_Socket:
      sandboxType = net::SocketProcessHost::GetMacSandboxType();
      break;
    case GeckoProcessType_GMPlugin:
      sandboxType = gmp::GMPProcessParent::GetMacSandboxType();
      break;
    case GeckoProcessType_Utility:
      sandboxType = ipc::UtilityProcessHost::GetMacSandboxType();
      break;
    default:
      return true;
  }

  return mozilla::StartMacSandboxIfEnabled(sandboxType, aArgc, aArgv,
                                           aErrorMessage);
}

#endif /* XP_MACOSX && MOZ_SANDBOX */

/* static */
void GeckoChildProcessHost::GetAll(const GeckoProcessCallback& aCallback) {
  StaticMutexAutoLock lock(sMutex);
  if (!sGeckoChildProcessHosts) {
    return;
  }
  for (GeckoChildProcessHost* gp = sGeckoChildProcessHosts->getFirst(); gp;
       gp = static_cast<mozilla::LinkedListElement<GeckoChildProcessHost>*>(gp)
                ->getNext()) {
    aCallback(gp);
  }
}

RefPtr<ProcessLaunchPromise> BaseProcessLauncher::Launch(
    GeckoChildProcessHost* aHost) {
  AssertIOThread();

  // The ForkServer doesn't use IPC::Channel for communication, so we can skip
  // initializing it.
  if (mProcessType != GeckoProcessType_ForkServer) {
    IPC::Channel::ChannelHandle serverHandle;
    IPC::Channel::ChannelHandle clientHandle;
    if (!IPC::Channel::CreateRawPipe(&serverHandle, &clientHandle)) {
      return ProcessLaunchPromise::CreateAndReject(LaunchError("CreateRawPipe"),
                                                   __func__);
    }
    aHost->InitializeChannel(std::move(serverHandle));
    geckoargs::sIPCHandle.Put(std::move(clientHandle), mChildArgs);
  }

  return InvokeAsync(mLaunchThread, this, __func__,
                     &BaseProcessLauncher::PerformAsyncLaunch);
}

}  // namespace ipc
}  // namespace mozilla
