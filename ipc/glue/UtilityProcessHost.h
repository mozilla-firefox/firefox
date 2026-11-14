/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _include_ipc_glue_UtilityProcessHost_h_
#define _include_ipc_glue_UtilityProcessHost_h_

#include "mozilla/MozPromise.h"
#include "mozilla/UniquePtr.h"
#include "nsITimer.h"
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/ipc/ProcessUtils.h"

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxBroker.h"
#endif

namespace mozilla::ipc {

class UtilityProcessParent;

// UtilityProcessHost is the "parent process" container for a subprocess handle
// and IPC connection. It owns the parent process IPDL actor, which in this
// case, is a UtilityChild.
//
// UtilityProcessHosts are allocated and managed by UtilityProcessManager.
class UtilityProcessHost final : public mozilla::ipc::GeckoChildProcessHost {
  friend class UtilityProcessParent;

 public:
  class Listener {
   protected:
    virtual ~Listener() = default;

   public:
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UtilityProcessHost::Listener);

    // The UtilityProcessHost has unexpectedly shutdown or had its connection
    // severed. This is not called if an error occurs after calling
    // Shutdown().
    virtual void OnProcessUnexpectedShutdown(UtilityProcessHost* aHost) {}
  };

  explicit UtilityProcessHost(SandboxingKind aSandbox,
                              RefPtr<Listener> listener);

  // Launch the subprocess asynchronously. On failure, false is returned.
  // Otherwise, true is returned. If succeeded, a follow-up call should be made
  // to LaunchPromise() which will return a promise that will be resolved once
  // the Utility process has launched and a channel has been established.
  //
  // @param aExtraOpts (geckoargs::ChildProcessArgs)
  //        Extra options to pass to the subprocess.
  bool Launch(geckoargs::ChildProcessArgs aExtraOpts);

  using LaunchPromiseType = MozPromise<Ok, LaunchError, false>;
  // Return a promise that will be resolved once the process has completed its
  // launch. The promise will be immediately resolved if the launch has already
  // succeeded.
  RefPtr<LaunchPromiseType> LaunchPromise();

  // Resolved once the process' channel has been closed and this host is about
  // to go away. Never rejected.
  using ShutdownPromiseType = MozPromise<Ok, nsresult, false>;

  // Inform the process that it should clean up its resources and shut
  // down. This initiates an asynchronous shutdown sequence. After this
  // method returns, it is safe for the caller to forget its pointer to
  // the UtilityProcessHost.
  //
  // The returned promise resolves once that sequence has completed.
  //
  // After this returns, the attached Listener is no longer used.
  RefPtr<ShutdownPromiseType> Shutdown();

  // Return the actor for the top-level actor of the process. If the process
  // has not connected yet, this returns null.
  RefPtr<UtilityProcessParent> GetActor() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mUtilityProcessParent;
  }

  bool IsConnected() const {
    MOZ_ASSERT(NS_IsMainThread());
    return bool(mUtilityProcessParent);
  }

  // Called on the IO thread.
  void OnChannelConnected(base::ProcessId peer_pid) override;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Return the sandbox type to be used with this process type.
  static MacSandboxType GetMacSandboxType();
#endif

 private:
  ~UtilityProcessHost();

  // Called on the main thread with true after a connection has been established
  // or false if it failed (including if it failed before the timeout kicked in)
  void InitAfterConnect(bool aSucceeded);

  // Called on the main thread when the mUtilityProcessParent actor is shutting
  // down.
  void OnChannelClosed(IProtocol::ActorDestroyReason aReason);

  // Kill the remote process, triggering IPC shutdown.
  void KillHard(const char* aReason);

  // Kill the process if it does not acknowledge our shutdown request within
  // dom.ipc.utilityProcess.shutdownTimeoutSecs, so it cannot hang shutdown.
  void StartForceKillTimer();

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Sandbox utility processes based on IsUtilitySandboxEnabled()
  bool IsMacSandboxLaunchEnabled() override { return mDisableOSActivityMode; }

  // Override so we can turn on Utility process-specific sandbox logging
  bool FillMacSandboxInfo(MacSandboxInfo& aInfo) override;
#endif

  DISALLOW_COPY_AND_ASSIGN(UtilityProcessHost);

  RefPtr<Listener> mListener;

  // All members below are only ever accessed on the main thread.
  enum class LaunchPhase { Unlaunched, Waiting, Complete };
  LaunchPhase mLaunchPhase = LaunchPhase::Unlaunched;

  RefPtr<UtilityProcessParent> mUtilityProcessParent;

  UniquePtr<ipc::SharedPreferenceSerializer> mPrefSerializer{};

  bool mShutdownRequested = false;

  nsCOMPtr<nsITimer> mForceKillTimer;

  MozPromiseHolder<ShutdownPromiseType> mShutdownPromise;

  void ResolvePromise();
  void RejectPromise(LaunchError);

#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX) && !defined(MOZ_ASAN)
  void EnsureWidevineL1PathForSandbox(geckoargs::ChildProcessArgs& aExtraOpts);
#endif

#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX)
  void EnanbleMFCDMTelemetryEventIfNeeded() const;
#endif

  RefPtr<LaunchPromiseType::Private> mLaunchPromise{};
  bool mLaunchPromiseSettled = false;
  bool mLaunchPromiseLaunched = false;
  // Will be set to true if the Utility process as successfully started.
  bool mLaunchCompleted = false;

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  RefPtr<SandboxBroker> mSandboxBroker{};
#endif
};

}  // namespace mozilla::ipc

#endif  // _include_ipc_glue_UtilityProcessHost_h_
