/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IPC_GLUE_GECKOCHILDPROCESSHOST_H_
#define IPC_GLUE_GECKOCHILDPROCESSHOST_H_

#include "base/file_path.h"
#include "base/process_util.h"
#include "base/waitable_event.h"
#include "chrome/common/ipc_message.h"
#include "mojo/core/ports/port_ref.h"

#include "mozilla/GeckoArgs.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/FileDescriptor.h"
#include "mozilla/ipc/NodeChannel.h"
#include "mozilla/ipc/LaunchError.h"
#include "mozilla/ipc/ScopedPort.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/Logging.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RWLock.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"

#include "nsCOMPtr.h"
#include "nsExceptionHandler.h"
#include "nsXULAppAPI.h"  // for GeckoProcessType
#include "nsString.h"

#if defined(XP_IOS)
#  include "mozilla/ipc/ExtensionKitUtils.h"
#endif

#if defined(XP_WIN) && defined(MOZ_SANDBOX)
#  include "sandboxBroker.h"
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#endif

#if (defined(XP_WIN) && defined(_ARM64_)) || \
    (defined(XP_MACOSX) && defined(__aarch64__))
#  define ALLOW_GECKO_CHILD_PROCESS_ARCH
#endif

struct _MacSandboxInfo;
typedef _MacSandboxInfo MacSandboxInfo;

namespace mozilla {
namespace ipc {

typedef mozilla::MozPromise<base::ProcessHandle, LaunchError, false>
    ProcessHandlePromise;

// Log of child process lifetimes, consumed by external auditing tools, so
// please keep the format stable:
//
//   ++PROCESS [pid = 4208] [childID = 37] [type = tab]
//   --PROCESS [pid = 4208] [childID = 37] [type = tab]
//
// The --PROCESS line is emitted once the parent has released the process, so
// the operating system may reuse its pid from that point on. It may be
// emitted while the child is still winding down, and it is not emitted at all
// if the parent goes away first, so it means "no longer ours" rather than
// "has exited".
//
// Content processes log the remote type they serve on a separate line, once
// when they launch and again whenever it changes. A process launched for
// preallocation only learns the remote type it ends up serving once it is
// adopted, so it is logged twice:
//
//   REMOTETYPE [childID = 37] [remoteType = prealloc]
//   REMOTETYPE [childID = 37] [remoteType = web]
//
// remoteType comes last and may itself contain brackets for IPv6 origins, so
// it has to be parsed greedily to the end of the line.
//
// What a utility process actually does is decided by the actors bound into
// it, and one process can host several of them, so utility processes log a
// line per actor. These are logged as the actors connect, not at launch:
//
//   UTILITYACTOR [childID = 38] [actorName = audioDecoder_Generic]
//   UTILITYACTOR [childID = 38] [actorName = jSOracle]
//
// actorName is a WebIDLUtilityActorName value, the same name about:processes
// displays.
//
// The lines for one process are emitted from different threads and may arrive
// in any order. Join on childID rather than pid, and treat the last
// REMOTETYPE line as the current one.
extern LazyLogModule gChildProcessLifecycleLog;

class GeckoChildProcessHost
    : public SupportsThreadSafeWeakPtr<GeckoChildProcessHost> {
 protected:
  typedef mozilla::Monitor Monitor;
  typedef std::vector<std::string> StringVector;

 public:
  using ProcessId = base::ProcessId;
  using ProcessHandle = base::ProcessHandle;

  MOZ_DECLARE_REFCOUNTED_TYPENAME(GeckoChildProcessHost)

  explicit GeckoChildProcessHost(GeckoProcessType aProcessType,
                                 bool aIsFileContent = false);

  static uint32_t GetUniqueID();

  // Call this before launching to set an environment variable for the
  // child process.  The arguments must be UTF-8.
  void SetEnv(const char* aKey, const char* aValue);

  // Does not block.  The IPC channel may not be initialized yet, and
  // the child process may or may not have been created when this
  // method returns.
  bool AsyncLaunch(
      geckoargs::ChildProcessArgs aExtraOpts = geckoargs::ChildProcessArgs{});

  virtual bool WaitUntilConnected(int32_t aTimeoutMs = 0);

  // Block until the IPC channel for our subprocess is initialized and
  // the OS process is created.  The subprocess may or may not have
  // connected back to us when this method returns.
  //
  // NB: on POSIX, this method is relatively cheap, and doesn't
  // require disk IO.  On win32 however, it requires at least the
  // analogue of stat().  This difference induces a semantic
  // difference in this method: on POSIX, when we return, we know the
  // subprocess has been created, but we don't know whether its
  // executable image can be loaded.  On win32, we do know that when
  // we return.  But we don't know if dynamic linking succeeded on
  // either platform.
  bool LaunchAndWaitForProcessHandle(
      geckoargs::ChildProcessArgs aExtraOpts = geckoargs::ChildProcessArgs());
  bool WaitForProcessHandle();

  // Block until the child process has been created and it connects to
  // the IPC channel, meaning it's fully initialized.  (Or until an
  // error occurs.)
  bool SyncLaunch(
      geckoargs::ChildProcessArgs aExtraOpts = geckoargs::ChildProcessArgs(),
      int32_t timeoutMs = 0);

  virtual void OnChannelConnected(base::ProcessId peer_pid);

  // Resolves to the process handle when it's available (see
  // LaunchAndWaitForProcessHandle); use with AsyncLaunch.
  RefPtr<ProcessHandlePromise> WhenProcessHandleReady();

  bool InitializeChannel(IPC::Channel::ChannelHandle* aClientHandle);

  virtual bool CanShutdown() { return true; }

  UntypedEndpoint TakeInitialEndpoint() {
    return UntypedEndpoint{PrivateIPDLInterface{}, std::move(mInitialPort),
                           mInitialChannelId, EndpointProcInfo::Current(),
                           EndpointProcInfo{.mPid = GetChildProcessId(),
                                            .mChildID = GetChildID()}};
  }

  // Returns a "borrowed" handle to the child process - the handle returned
  // by this function must not be closed by the caller.  The handle is also
  // not guaranteed to remain valid; if the caller is using it for anything
  // more than logging or asserting non-null, it will need to deal with
  // synchronization.
  //
  // Warning: the null value here is 0, not kInvalidProcessHandle.
  ProcessHandle GetChildProcessHandle();

  // Returns the child's process ID; as for GetChildProcessHandle, there is
  // no inherent guarantee that it will remain valid or continue to
  // reference the same process.
  //
  // The null value here is also 0; this matches the result of
  // GetProcId on a zero or (on Windows) invalid handle.
  ProcessId GetChildProcessId();

  // Return the child's GeckoChildID. This is a unique identifier given out to
  // each process started with `GeckoChildProcessHost` which will only ever
  // identify this process.
  GeckoChildID GetChildID() const { return mChildID; }

  GeckoProcessType GetProcessType() { return mProcessType; }

#ifdef XP_MACOSX
  task_t GetChildTask();
#endif

#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  void SetLaunchArchitecture(uint32_t aArch) { mLaunchArch = aArch; }
#endif

  // For bug 943174: Skip the EnsureProcessTerminated call in the destructor.
  void SetAlreadyDead();

#if defined(MOZ_SANDBOX) && defined(XP_MACOSX)
  // Start the sandbox from the child process.
  static bool StartMacSandbox(int aArgc, char** aArgv,
                              std::string& aErrorMessage);

  // The sandbox type that will be use when sandboxing is
  // enabled in the derived class and FillMacSandboxInfo
  // has not been overridden.
  static MacSandboxType GetDefaultMacSandboxType() {
    return MacSandboxType_Utility;
  };

  // Must be called before the process is launched. Determines if
  // child processes will be launched with OS_ACTIVITY_MODE set to
  // "disabled" or not. When |mDisableOSActivityMode| is set to true,
  // child processes will be launched with OS_ACTIVITY_MODE
  // disabled to avoid connection attempts to diagnosticd(8) which are
  // blocked in child processes due to sandboxing.
  void DisableOSActivityMode();
#endif  // defined(MOZ_SANDBOX) && defined(XP_MACOSX)

  // Captures a snapshot view of the list of all processes.
  static nsTArray<RefPtr<GeckoChildProcessHost>> GetAll();

  friend class BaseProcessLauncher;
  friend class PosixProcessLauncher;
  friend class WindowsProcessLauncher;

 protected:
  friend class SupportsThreadSafeWeakPtr<GeckoChildProcessHost>;

  virtual ~GeckoChildProcessHost();
  GeckoProcessType mProcessType;
  GeckoChildID mChildID;
  bool mIsFileContent;
  mutable Monitor mMonitor;
  FilePath mProcessPath;
#ifdef ALLOW_GECKO_CHILD_PROCESS_ARCH
  // Used on platforms where we may launch a child process with a different
  // architecture than the parent process.
  uint32_t mLaunchArch = base::PROCESS_ARCH_INVALID;
#endif
  // GeckoChildProcessHost holds the launch options so they can be set
  // up on the main thread using main-thread-only APIs like prefs, and
  // then used for the actual launch on another thread.  This pointer
  // is set to null to free the options after the child is launched.
  UniquePtr<base::LaunchOptions> mLaunchOptions;
  ScopedPort mInitialPort;
  nsID mInitialChannelId;
  RefPtr<NodeController> mNodeController;
  RefPtr<NodeChannel> mNodeChannel;

  // This value must be accessed while holding mMonitor.
  enum {
    // This object has been constructed, but the OS process has not
    // yet.
    CREATING_CHANNEL = 0,
    // The IPC channel for our subprocess has been created, but the OS
    // process has still not been created.
    CHANNEL_INITIALIZED,
    // The OS process has been created, but it hasn't yet connected to
    // our IPC channel.
    PROCESS_CREATED,
    // The process is launched and connected to our IPC channel.  All
    // is well.
    PROCESS_CONNECTED,
    PROCESS_ERROR
  } mProcessState MOZ_GUARDED_BY(mMonitor);

  bool PrepareLaunch(geckoargs::ChildProcessArgs& aExtraOpts);

#ifdef XP_WIN
  void InitWindowsGroupID();
  nsString mGroupId;
#  ifdef MOZ_SANDBOX
  UniquePtr<SandboxBroker> mSandboxBroker;
  std::vector<std::wstring> mAllowedFilesRead;
  bool mEnableSandboxLogging;
  int32_t mSandboxLevel;
#  endif
#endif  // XP_WIN

  // Only set by UtilityProcessHost. The sandbox policy associated with
  // mUtilitySandbox will only be honored under MOZ_SANDBOX. However, on macOS,
  // we will choose the proper firefox binary to run independently of
  // MOZ_SANDBOX. This ensures that the utility process always runs with the
  // expected set of entitlements.
  SandboxingKind mUtilitySandbox;

  mozilla::RWLock mHandleLock;
  ProcessHandle mChildProcessHandle MOZ_GUARDED_BY(mHandleLock);
#if defined(XP_MACOSX)
  task_t mChildTask MOZ_GUARDED_BY(mHandleLock);
#endif
#if defined(MOZ_WIDGET_UIKIT)
  Maybe<ExtensionKitProcess> mExtensionKitProcess MOZ_GUARDED_BY(mHandleLock);
  DarwinObjectPtr<xpc_connection_t> mXPCConnection MOZ_GUARDED_BY(mHandleLock);
  UniqueBEProcessCapabilityGrant mForegroundCapabilityGrant
      MOZ_GUARDED_BY(mHandleLock);
#endif
  RefPtr<ProcessHandlePromise> mHandlePromise;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  bool mDisableOSActivityMode;
#endif

  bool OpenPrivilegedHandle(base::ProcessId aPid) MOZ_REQUIRES(mHandleLock);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Override this method to return true to launch the child process
  // using the Mac utility (by default) sandbox. Override
  // FillMacSandboxInfo() to change the sandbox type and settings.
  virtual bool IsMacSandboxLaunchEnabled() { return false; }

  // Fill a MacSandboxInfo to configure the sandbox
  virtual bool FillMacSandboxInfo(MacSandboxInfo& aInfo);

  // Adds the command line arguments needed to enable
  // sandboxing of the child process at startup before
  // the child event loop is up.
  virtual bool AppendMacSandboxParams(StringVector& aArgs);
#endif

  virtual void OnProcessLaunchError(const LaunchError aError);

 private:
  DISALLOW_EVIL_CONSTRUCTORS(GeckoChildProcessHost);

  // Mac and Windows. Set this up before we're called from a different thread.
  nsCOMPtr<nsIFile> mProfileDir;

  static std::atomic<uint32_t> sNextUniqueID;
};

nsCOMPtr<nsISerialEventTarget> GetIPCLauncher();

#ifdef MOZ_WIDGET_COCOA
enum class MacOSChildProcessBundleType { EME, GPU, PKCS11, Default, Count };

nsLiteralCString GetChildProcessBundleNameForTesting(
    MacOSChildProcessBundleType aBundleType);

// In order to catch cases during testing where the plist path resolution fails,
// this function resolves the executable by reading the bundle's Info.plist
// without falling back to the branding macros.
bool ResolveChildProcessPathFromPlistForTesting(
    MacOSChildProcessBundleType aBundleType, nsACString& aExecutablePath);
#endif  // MOZ_WIDGET_COCOA

} /* namespace ipc */
} /* namespace mozilla */

#endif /* IPC_GLUE_GECKOCHILDPROCESSHOST_H_ */
