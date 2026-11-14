/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _include_mozilla_gfx_ipc_GPUProcessHost_h_
#define _include_mozilla_gfx_ipc_GPUProcessHost_h_

#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/media/MediaUtils.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/CompositorSurfaceManagerWrappers.h"
#endif

namespace mozilla {
namespace ipc {
class SharedPreferenceSerializer;
}
}  // namespace mozilla
class nsITimer;

namespace mozilla {
namespace gfx {

class GPUChild;

// GPUProcessHost is the "parent process" container for a subprocess handle and
// IPC connection. It owns the parent process IPDL actor, which in this case,
// is a GPUChild.
//
// GPUProcessHosts are allocated and managed by GPUProcessManager. For all
// intents and purposes it is a singleton, though more than one may be allocated
// at a time due to its shutdown being asynchronous.
class GPUProcessHost final : public mozilla::ipc::GeckoChildProcessHost {
  friend class GPUChild;

 public:
  class Listener {
   public:
    virtual void OnProcessLaunchComplete(GPUProcessHost* aHost) {}

    // The GPUProcessHost has unexpectedly shutdown or had its connection
    // severed. This is not called if an error occurs after calling
    // Shutdown().
    virtual void OnProcessUnexpectedShutdown(GPUProcessHost* aHost) {}

    virtual void OnRemoteProcessDeviceReset(
        GPUProcessHost* aHost, const DeviceResetReason& aReason,
        const DeviceResetDetectPlace& aPlace) {}

    virtual void OnProcessDeclaredStable() {}
  };

  explicit GPUProcessHost(Listener* listener);

  // Launch the subprocess asynchronously. On failure, false is returned.
  // Otherwise, true is returned, and the OnProcessLaunchComplete listener
  // callback will be invoked either when a connection has been established and
  // process initialization is complete, or if a connection could not be
  // established due to an asynchronous error.
  //
  // @param aExtraOpts (geckoargs::ChildProcessArgs)
  //        Extra options to pass to the subprocess.
  bool Launch(geckoargs::ChildProcessArgs aExtraOpts);

  // If the process is being launched, block until it has launched and
  // connected, and any initialization has completed. If a launch task is
  // pending, it will fire immediately.
  //
  // Returns true if the process is successfully initialized; false otherwise.
  bool WaitForLaunch();

  // Inform the process that it should clean up its resources and shut down.
  // This initiates an asynchronous shutdown sequence. After this method
  // returns, it is safe for the caller to forget its pointer to the
  // GPUProcessHost.
  //
  // After this returns, the attached Listener is no longer used.
  //
  // Setting aUnexpectedShutdown = true indicates that this is being called to
  // clean up resources in response to an unexpected shutdown having been
  // detected.
  void Shutdown(bool aUnexpectedShutdown = false);

  // Return the actor for the top-level actor of the process. If the process
  // has not connected yet, this returns null.
  GPUChild* GetActor() const { return mGPUChild.get(); }

  // Return a unique id for this process, guaranteed not to be shared with any
  // past or future instance of GPUProcessHost.
  uint64_t GetProcessToken() const;

  bool IsConnected() const { return !!mGPUChild; }

  bool IsLaunchOomError() const {
    MonitorAutoLock lock(mMonitor);
    return mLaunchOomError;
  }

  // Return the time stamp for when we tried to launch the GPU process. This is
  // currently used for Telemetry so that we can determine how long GPU
  // processes take to spin up. Note this doesn't denote a successful launch,
  // just when we attempted launch.
  TimeStamp GetLaunchTime() const { return mLaunchTime; }

  // Called on the IO thread.
  void OnChannelConnected(base::ProcessId peer_pid) override;

  void SetListener(Listener* aListener);

  // Kills the GPU process. Used in normal operation to recover from an error,
  // as well as for tests and diagnostics.
  void KillProcess(bool aGenerateMinidump);

  // Kills the GPU process for tests. Unlike KillProcess, this is not treated as
  // a crash for the purposes of telemetry, crash reporting and fallbacks.
  void KillProcessForTesting();
  bool WasKilledForTesting() const { return mKilledForTesting; }

  // Causes the GPU process to crash. Used for tests and diagnostics
  void CrashProcess();

#ifdef MOZ_WIDGET_ANDROID
  java::CompositorSurfaceManager::Param GetCompositorSurfaceManager();
#endif

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  static MacSandboxType GetMacSandboxType() { return MacSandboxType_GPU; };
#endif

 private:
  ~GPUProcessHost();

  // Called on the main thread after a connection has been established.
  // Creates the PGPU endpoints and begins asynchronous initialization.
  void InitAfterConnect(bool aSucceeded);
  // Called on the main thread after post-connection initialization tasks have
  // completed asynchronously.
  void OnAsyncInitComplete();
  // Synchronously completes any outstanding post-connection initialization
  // tasks which have not yet completed asynchronously.
  bool CompleteInitSynchronously();

  void OnProcessLaunchError(const base::LaunchError aError) override;

  // Called on the main thread when the mGPUChild actor is shutting down.
  void OnChannelClosed();

  // Kill the remote process, triggering IPC shutdown.
  void KillHard(bool aGenerateMinidump);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  static bool sLaunchWithMacSandbox;
  bool IsMacSandboxLaunchEnabled() override { return sLaunchWithMacSandbox; }

  // Override so we can turn on GPU process-specific sandbox logging
  bool FillMacSandboxInfo(MacSandboxInfo& aInfo) override;
#endif

  DISALLOW_COPY_AND_ASSIGN(GPUProcessHost);

  Listener* mListener;

  enum class LaunchPhase { Unlaunched, Waiting, Connected, Complete };
  LaunchPhase mLaunchPhase;

  RefPtr<GPUChild> mGPUChild;
  uint64_t mProcessToken;

  UniquePtr<mozilla::ipc::SharedPreferenceSerializer> mPrefSerializer;

  bool mShutdownRequested;
  bool mChannelClosed;
  bool mKilledForTesting = false;
  bool mLaunchOomError MOZ_GUARDED_BY(mMonitor) = false;

  TimeStamp mLaunchTime;

#ifdef MOZ_WIDGET_ANDROID
  // Binder interface used to send compositor surfaces to GPU process. There is
  // one instance per GPU process which gets initialized after launch, then
  // multiple compositors can take a reference to it.
  java::CompositorSurfaceManager::GlobalRef mCompositorSurfaceManager;
#endif
};

}  // namespace gfx
}  // namespace mozilla

#endif  // _include_mozilla_gfx_ipc_GPUProcessHost_h_
