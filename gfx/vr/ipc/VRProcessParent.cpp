/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRProcessParent.h"

#include "VRChild.h"
#include "VRGPUChild.h"
#include "VRProcessManager.h"
#include "VRThread.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TimeStamp.h"  // for TimeStamp
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/gfx/GPUChild.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "mozilla/ipc/ProtocolTypes.h"
#include "mozilla/ipc/ProtocolUtils.h"  // for IToplevelProtocol
#include "nsAppRunner.h"                // for IToplevelProtocol

using std::string;
using std::vector;

using namespace mozilla::ipc;

namespace mozilla {
namespace gfx {

VRProcessParent::VRProcessParent(Listener* aListener)
    : GeckoChildProcessHost(GeckoProcessType_VR),
      mListener(aListener),
      mLaunchPhase(LaunchPhase::Unlaunched),
      mChannelClosed(false),
      mShutdownRequested(false) {}

VRProcessParent::~VRProcessParent() = default;

bool VRProcessParent::Launch() {
  MOZ_RELEASE_ASSERT(mLaunchPhase == LaunchPhase::Unlaunched);
  MOZ_RELEASE_ASSERT(!mVRChild);
  mLaunchThread = NS_GetCurrentThread();

  mLaunchPhase = LaunchPhase::Waiting;

  geckoargs::ChildProcessArgs extraArgs;
  ProcessChild::AddPlatformBuildID(extraArgs);

  mPrefSerializer = std::make_unique<ipc::SharedPreferenceSerializer>();
  if (!mPrefSerializer->SerializeToSharedMemory(GeckoProcessType_VR,
                                                /* remoteType */ {})) {
    return false;
  }
  mPrefSerializer->AddSharedPrefCmdLineArgs(*this, extraArgs);

  if (!GeckoChildProcessHost::AsyncLaunch(std::move(extraArgs))) {
    mLaunchPhase = LaunchPhase::Complete;
    mPrefSerializer = nullptr;
    return false;
  }
  return true;
}

bool VRProcessParent::WaitForLaunch() {
  if (mLaunchPhase == LaunchPhase::Complete) {
    return !!mVRChild;
  }

  int32_t timeoutMs =
      StaticPrefs::dom_vr_process_startup_timeout_ms_AtStartup();

  // If one of the following environment variables are set we can effectively
  // ignore the timeout - as we can guarantee the compositor process will be
  // terminated
  if (PR_GetEnv("MOZ_DEBUG_CHILD_PROCESS") ||
      PR_GetEnv("MOZ_DEBUG_CHILD_PAUSE")) {
    timeoutMs = 0;
  }

  // Our caller expects the connection to be finished after we return, so we
  // immediately set up the IPDL actor and fire callbacks. The IO thread will
  // still dispatch a notification to the main thread - we'll just ignore it.
  bool result = GeckoChildProcessHost::WaitUntilConnected(timeoutMs);
  result &= InitAfterConnect(result);
  return result;
}

void VRProcessParent::Shutdown() {
  MOZ_RELEASE_ASSERT(!mShutdownRequested);
  mListener = nullptr;
  mShutdownRequested = true;

  if (mVRChild) {
    // The channel might already be closed if we got here unexpectedly.
    if (!mChannelClosed) {
      mVRChild->Close();
      MOZ_ASSERT(!mVRChild);
    }
#ifndef NS_FREE_PERMANENT_DATA
    // No need to communicate shutdown, the VR process doesn't need to
    // communicate anything back.
    KillHard("NormalShutdown");
#endif
  }
}

bool VRProcessParent::InitAfterConnect(bool aSucceeded) {
  MOZ_RELEASE_ASSERT(mLaunchPhase == LaunchPhase::Waiting);
  MOZ_RELEASE_ASSERT(!mVRChild);

  mLaunchPhase = LaunchPhase::Complete;
  mPrefSerializer = nullptr;

  if (aSucceeded) {
    GPUChild* gpuChild = GPUProcessManager::Get()->GetGPUChild();
    if (!gpuChild) {
      NS_WARNING(
          "GPU process haven't connected with the parent process yet"
          "when creating VR process.");
      return false;
    }

    if (!StaticPrefs::dom_vr_enabled() &&
        !StaticPrefs::dom_vr_webxr_enabled()) {
      NS_WARNING("VR is not enabled when trying to create a VRChild");
      return false;
    }

    mVRChild = MakeRefPtr<VRChild>(this);

    bool rv = TakeInitialEndpoint().Bind(mVRChild.get());
    MOZ_RELEASE_ASSERT(rv);

    mVRChild->Init();

    if (mListener) {
      mListener->OnProcessLaunchComplete(this);
    }

    // Make vr-gpu process connection
    Endpoint<PVRGPUChild> vrGPUBridge;
    VRProcessManager* vpm = VRProcessManager::Get();
    bool opened =
        vpm->CreateGPUBridges(gpuChild->OtherEndpointProcInfo(), &vrGPUBridge);
    MOZ_RELEASE_ASSERT(opened);

    (void)gpuChild->SendInitVR(std::move(vrGPUBridge));
  }

  return true;
}

void VRProcessParent::KillHard(const char* aReason) {
  ProcessHandle handle = GetChildProcessHandle();
  if (!base::KillProcess(handle, base::PROCESS_END_KILLED_BY_USER)) {
    NS_WARNING("failed to kill subprocess!");
  }

  SetAlreadyDead();
}

void VRProcessParent::OnChannelConnected(base::ProcessId peer_pid) {
  MOZ_RELEASE_ASSERT(!NS_IsMainThread());

  GeckoChildProcessHost::OnChannelConnected(peer_pid);

  NS_DispatchToMainThread(
      NewRunnableMethod("VRProcessParent::OnChannelConnectedTask", this,
                        &VRProcessParent::OnChannelConnectedTask));
}

void VRProcessParent::OnChannelConnectedTask() {
  if (!mShutdownRequested && mLaunchPhase == LaunchPhase::Waiting) {
    InitAfterConnect(true);
  }
}

void VRProcessParent::OnChannelErrorTask() {
  if (!mShutdownRequested && mLaunchPhase == LaunchPhase::Waiting) {
    InitAfterConnect(false);
  }
}

void VRProcessParent::OnChannelClosed() {
  mChannelClosed = true;
  if (!mShutdownRequested && mListener) {
    // This is an unclean shutdown. Notify we're going away.
    mListener->OnProcessUnexpectedShutdown(this);
  }

  // Release the actor.
  VRChild::Destroy(std::move(mVRChild));
  MOZ_RELEASE_ASSERT(!mVRChild);
}

bool VRProcessParent::IsConnected() const { return !!mVRChild; }

}  // namespace gfx
}  // namespace mozilla
