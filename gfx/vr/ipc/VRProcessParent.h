/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_VR_PROCESS_PARENT_H
#define GFX_VR_PROCESS_PARENT_H

#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/TaskFactory.h"

namespace mozilla {
namespace ipc {
class SharedPreferenceSerializer;
}
namespace gfx {

class VRChild;

class VRProcessParent final : public mozilla::ipc::GeckoChildProcessHost {
 public:
  class Listener {
   public:
    virtual void OnProcessLaunchComplete(VRProcessParent* aParent) {}

    // Follow GPU and RDD process manager, adding this to avoid
    // unexpectedly shutdown or had its connection severed.
    // This is not called if an error occurs after calling Shutdown().
    virtual void OnProcessUnexpectedShutdown(VRProcessParent* aParent) {}
  };

  explicit VRProcessParent(Listener* aListener);

  // Launch the subprocess asynchronously. On failure, false is returned.
  // Otherwise, true is returned, and the OnProcessLaunchComplete listener
  // callback will be invoked either when a connection has been established, or
  // if a connection could not be established due to an asynchronous error.
  bool Launch();
  // If the process is being launched, block until it has launched and
  // connected. If a launch task is pending, it will fire immediately.
  //
  // Returns true if the process is successfully connected; false otherwise.
  bool WaitForLaunch();
  void Shutdown();
  bool CanShutdown() override { return true; }

  void OnChannelConnected(base::ProcessId peer_pid) override;
  void OnChannelConnectedTask();
  void OnChannelErrorTask();
  void OnChannelClosed();
  bool IsConnected() const;

  VRChild* GetActor() const { return mVRChild.get(); }
  // Return a unique id for this process, guaranteed not to be shared with any
  // past or future instance of VRProcessParent.
  uint64_t GetProcessToken() const;

 private:
  ~VRProcessParent();

  DISALLOW_COPY_AND_ASSIGN(VRProcessParent);

  bool InitAfterConnect(bool aSucceeded);
  void KillHard(const char* aReason);

  RefPtr<VRChild> mVRChild;
  nsCOMPtr<nsIThread> mLaunchThread;
  Listener* mListener;

  enum class LaunchPhase { Unlaunched, Waiting, Complete };
  LaunchPhase mLaunchPhase;
  bool mChannelClosed;
  bool mShutdownRequested;
  UniquePtr<mozilla::ipc::SharedPreferenceSerializer> mPrefSerializer;
};

}  // namespace gfx
}  // namespace mozilla

#endif  // ifndef GFX_VR_PROCESS_PARENT_H
