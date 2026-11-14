/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _include_dom_media_ipc_RDDProcessHost_h_
#define _include_dom_media_ipc_RDDProcessHost_h_
#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/media/MediaUtils.h"

namespace mozilla::ipc {
class SharedPreferenceSerializer;
}  // namespace mozilla::ipc
class nsITimer;

namespace mozilla {

class RDDChild;

// RDDProcessHost is the "parent process" container for a subprocess handle and
// IPC connection. It owns the parent process IPDL actor, which in this case,
// is a RDDChild.
//
// RDDProcessHosts are allocated and managed by RDDProcessManager. For all
// intents and purposes it is a singleton, though more than one may be allocated
// at a time due to its shutdown being asynchronous.
class RDDProcessHost final : public mozilla::ipc::GeckoChildProcessHost {
  friend class RDDChild;

 public:
  class Listener {
   public:
    // The RDDProcessHost has unexpectedly shutdown or had its connection
    // severed. This is not called if an error occurs after calling
    // Shutdown().
    virtual void OnProcessUnexpectedShutdown(RDDProcessHost* aHost) {}
  };

  explicit RDDProcessHost(Listener* listener);

  // Launch the subprocess asynchronously. On failure, false is returned.
  // Otherwise, true is returned. If succeeded, a follow-up call should be made
  // to LaunchPromise() which will return a promise that will be resolved once
  // the RDD process has launched and a channel has been established.
  //
  // @param aExtraOpts (geckoargs::ChildProcessArgs)
  //        Extra options to pass to the subprocess.
  bool Launch(geckoargs::ChildProcessArgs aExtraOpts);

  // Return a promise that will be resolved once the process has completed its
  // launch. The promise will be immediately resolved if the launch has already
  // succeeded.
  RefPtr<GenericNonExclusivePromise> LaunchPromise();

  // Inform the process that it should clean up its resources and shut
  // down. This initiates an asynchronous shutdown sequence. After this
  // method returns, it is safe for the caller to forget its pointer to
  // the RDDProcessHost.
  //
  // After this returns, the attached Listener is no longer used.
  void Shutdown();

  // Return the actor for the top-level actor of the process. If the process
  // has not connected yet, this returns null.
  RDDChild* GetActor() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mRDDChild.get();
  }

  // Return a unique id for this process, guaranteed not to be shared with any
  // past or future instance of RDDProcessHost.
  uint64_t GetProcessToken() const;

  bool IsConnected() const {
    MOZ_ASSERT(NS_IsMainThread());
    return !!mRDDChild;
  }

  // Return the time stamp for when we tried to launch the RDD process.
  // This is currently used for Telemetry so that we can determine how
  // long RDD processes take to spin up. Note this doesn't denote a
  // successful launch, just when we attempted launch.
  TimeStamp GetLaunchTime() const { return mLaunchTime; }

  // Called on the IO thread.
  void OnChannelConnected(base::ProcessId peer_pid) override;

  void SetListener(Listener* aListener);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Return the sandbox type to be used with this process type.
  static MacSandboxType GetMacSandboxType();
#endif

 private:
  ~RDDProcessHost();

  // Called on the main thread with true after a connection has been established
  // or false if it failed (including if it failed before the timeout kicked in)
  void InitAfterConnect(bool aSucceeded);

  // Called on the main thread when the mRDDChild actor is shutting down.
  void OnChannelClosed();

  // Kill the remote process, triggering IPC shutdown.
  void KillHard(const char* aReason);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  static bool sLaunchWithMacSandbox;

  // Sandbox the RDD process at launch for all instances
  bool IsMacSandboxLaunchEnabled() override { return sLaunchWithMacSandbox; }

  // Override so we can turn on RDD process-specific sandbox logging
  bool FillMacSandboxInfo(MacSandboxInfo& aInfo) override;
#endif

  DISALLOW_COPY_AND_ASSIGN(RDDProcessHost);

  Listener* const mListener;

  // All members below are only ever accessed on the main thread.
  enum class LaunchPhase { Unlaunched, Waiting, Complete };
  LaunchPhase mLaunchPhase = LaunchPhase::Unlaunched;

  RefPtr<RDDChild> mRDDChild;
  uint64_t mProcessToken = 0;

  UniquePtr<ipc::SharedPreferenceSerializer> mPrefSerializer;

  bool mShutdownRequested = false;
  bool mChannelClosed = false;

  TimeStamp mLaunchTime;
  void RejectPromise();
  void ResolvePromise();

  RefPtr<GenericNonExclusivePromise::Private> mLaunchPromise;
  bool mLaunchPromiseSettled = false;
  // Will be set to true if we've exceeded the allowed startup time or if the
  // RDD process as successfully started. This is used to determine if the
  // timeout runnable needs to execute code or not.
  bool mTimerChecked = false;
};

}  // namespace mozilla

#endif  // _include_dom_media_ipc_RDDProcessHost_h_
