/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRServiceHost.h"

#include "VRGPUChild.h"
#include "VRManager.h"
#include "VRPuppetCommandBuffer.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/gfx/GPUParent.h"
#include "service/VRService.h"

namespace mozilla {
namespace gfx {

static StaticRefPtr<VRServiceHost> sVRServiceHostSingleton;

/* static */
void VRServiceHost::Init(bool aEnableVRProcess) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (sVRServiceHostSingleton == nullptr) {
    sVRServiceHostSingleton = new VRServiceHost(aEnableVRProcess);
    ClearOnShutdown(&sVRServiceHostSingleton);
  }
}

/* static */
VRServiceHost* VRServiceHost::Get() {
  MOZ_RELEASE_ASSERT(sVRServiceHostSingleton != nullptr);
  return sVRServiceHostSingleton;
}

VRServiceHost::VRServiceHost(bool aEnableVRProcess)
    : mVRService(nullptr),
      mVRProcessEnabled(aEnableVRProcess),
      mVRProcessStarted(false),
      mVRServiceReadyInVRProcess(false),
      mVRServiceRequested(false)

{
  MOZ_COUNT_CTOR(VRServiceHost);
}

VRServiceHost::~VRServiceHost() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_COUNT_DTOR(VRServiceHost);
}

void VRServiceHost::StartService() {
  mVRServiceRequested = true;
  if (mVRProcessEnabled) {
    // VRService in the VR process
    RefreshVRProcess();
  } else if (mVRService) {
    // VRService in the GPU process if enabled, or
    // the parent process if the GPU process is not enabled.
    mVRService->Start();
  }
}

void VRServiceHost::StopService() {
  mVRServiceRequested = false;
  if (mVRProcessEnabled) {
    // VRService in the VR process
    RefreshVRProcess();
  } else if (mVRService) {
    // VRService in the GPU process if enabled, or
    // the parent process if the GPU process is not enabled.
    mVRService->Stop();
  }
}

void VRServiceHost::Shutdown() {
  PuppetReset();
  StopService();
  mVRService = nullptr;
}

void VRServiceHost::Refresh() {
  if (mVRService) {
    mVRService->Refresh();
  }
}

void VRServiceHost::CreateService(volatile VRExternalShmem* aShmem) {
  MOZ_RELEASE_ASSERT(!mVRProcessEnabled);
  mVRService = VRService::Create(aShmem);
}

bool VRServiceHost::NeedVRProcess() {
  if (!mVRProcessEnabled) {
    return false;
  }
  return mVRServiceRequested;
}

void VRServiceHost::RefreshVRProcess() {
  // Start or stop the VR process if needed
  if (NeedVRProcess()) {
    if (!mVRProcessStarted) {
      CreateVRProcess();
    }
  } else {
    if (mVRProcessStarted) {
      ShutdownVRProcess();
    }
  }
}

void VRServiceHost::CreateVRProcess() {
  // This is only allowed to run in the main thread of the GPU process
  if (!XRE_IsGPUProcess()) {
    return;
  }
  // Forward this to the main thread if not already there
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = NS_NewRunnableFunction(
        "VRServiceHost::CreateVRProcess",
        []() -> void { VRServiceHost::Get()->CreateVRProcess(); });
    NS_DispatchToMainThread(task.forget());
    return;
  }
  if (mVRProcessStarted) {
    return;
  }

  mVRProcessStarted = true;
  // Using PGPU channel to tell the main process
  // to create the VR process.
  gfx::GPUParent* gpu = GPUParent::GetSingleton();
  MOZ_RELEASE_ASSERT(gpu);
  (void)gpu->SendCreateVRProcess();
}

void VRServiceHost::NotifyVRProcessStarted() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(mVRProcessEnabled);
  if (!mVRProcessStarted) {
    // We have received this after the VR process
    // has been stopped; the VR service is no
    // longer running in the VR process.
    return;
  }

  if (!VRGPUChild::IsCreated()) {
    return;
  }
  VRGPUChild* vrGPUChild = VRGPUChild::Get();

  // The VR service has started in the VR process
  // If there were pending puppet commands, we
  // can send them now.
  // This must occur before the VRService
  // is started so the buffer can be seen
  // by VRPuppetSession::Initialize().
  if (!mPuppetPendingCommands.IsEmpty()) {
    vrGPUChild->SendPuppetSubmit(mPuppetPendingCommands);
    mPuppetPendingCommands.Clear();
  }

  vrGPUChild->SendStartVRService();
  mVRServiceReadyInVRProcess = true;
}

void VRServiceHost::ShutdownVRProcess() {
  // This is only allowed to run in the main thread of the GPU process
  if (!XRE_IsGPUProcess()) {
    return;
  }
  // Forward this to the main thread if not already there
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = NS_NewRunnableFunction(
        "VRServiceHost::ShutdownVRProcess",
        []() -> void { VRServiceHost::Get()->ShutdownVRProcess(); });
    NS_DispatchToMainThread(task.forget());
    return;
  }
  if (VRGPUChild::IsCreated()) {
    VRGPUChild* vrGPUChild = VRGPUChild::Get();
    vrGPUChild->SendStopVRService();
    if (!vrGPUChild->IsClosed()) {
      vrGPUChild->Close();
    }
    VRGPUChild::Shutdown();
  }
  if (!mVRProcessStarted) {
    return;
  }
  // Using PGPU channel to tell the main process
  // to shutdown VR process.
  gfx::GPUParent* gpu = GPUParent::GetSingleton();
  MOZ_RELEASE_ASSERT(gpu);
  (void)gpu->SendShutdownVRProcess();
  mVRProcessStarted = false;
  mVRServiceReadyInVRProcess = false;
}

void VRServiceHost::PuppetSubmit(const nsTArray<uint64_t>& aBuffer) {
  if (!mVRProcessEnabled) {
    // Puppet is running in this process, submit commands directly
    VRPuppetCommandBuffer::Get().Submit(aBuffer);
    return;
  }

  // We need to send the buffer to the VR process
  SendPuppetSubmitToVRProcess(aBuffer);
}

void VRServiceHost::SendPuppetSubmitToVRProcess(
    const nsTArray<uint64_t>& aBuffer) {
  // This is only allowed to run in the main thread of the GPU process
  if (!XRE_IsGPUProcess()) {
    return;
  }
  // Forward this to the main thread if not already there
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = NS_NewRunnableFunction(
        "VRServiceHost::SendPuppetSubmitToVRProcess",
        [buffer{aBuffer.Clone()}]() -> void {
          VRServiceHost::Get()->SendPuppetSubmitToVRProcess(buffer);
        });
    NS_DispatchToMainThread(task.forget());
    return;
  }
  if (!mVRServiceReadyInVRProcess) {
    // Queue the commands to be sent to the VR process once it is started
    mPuppetPendingCommands.AppendElements(aBuffer);
    return;
  }
  if (VRGPUChild::IsCreated()) {
    VRGPUChild* vrGPUChild = VRGPUChild::Get();
    vrGPUChild->SendPuppetSubmit(aBuffer);
  }
}

void VRServiceHost::PuppetReset() {
  // If we're already into ShutdownFinal, the VRPuppetCommandBuffer instance
  // will have been cleared, so don't try to access it after that point.
  if (!mVRProcessEnabled &&
      !(NS_IsMainThread() &&
        PastShutdownPhase(ShutdownPhase::XPCOMShutdownFinal))) {
    // Puppet is running in this process, tell it to reset directly.
    VRPuppetCommandBuffer::Get().Reset();
  }

  mPuppetPendingCommands.Clear();
  if (!mVRProcessStarted) {
    // Process is stopped, so puppet state is already clear
    return;
  }

  // We need to tell the VR process to reset the puppet
  SendPuppetResetToVRProcess();
}

void VRServiceHost::SendPuppetResetToVRProcess() {
  // This is only allowed to run in the main thread of the GPU process
  if (!XRE_IsGPUProcess()) {
    return;
  }
  // Forward this to the main thread if not already there
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = NS_NewRunnableFunction(
        "VRServiceHost::SendPuppetResetToVRProcess",
        []() -> void { VRServiceHost::Get()->SendPuppetResetToVRProcess(); });
    NS_DispatchToMainThread(task.forget());
    return;
  }
  if (VRGPUChild::IsCreated()) {
    VRGPUChild* vrGPUChild = VRGPUChild::Get();
    vrGPUChild->SendPuppetReset();
  }
}

void VRServiceHost::CheckForPuppetCompletion() {
  if (!mVRProcessEnabled) {
    // Puppet is running in this process, ask it directly
    if (VRPuppetCommandBuffer::Get().HasEnded()) {
      VRManager::Get()->NotifyPuppetComplete();
    }
  }
  if (!mPuppetPendingCommands.IsEmpty()) {
    // There are puppet commands pending to be sent to the
    // VR process once its started, thus it has not ended.
    return;
  }
  if (!mVRProcessStarted) {
    // The VR process will be kept alive as long
    // as there is a queue in the puppet command
    // buffer.  If the process is stopped, we can
    // infer that the queue has been cleared and
    // puppet state is reset.
    VRManager::Get()->NotifyPuppetComplete();
  }

  // We need to ask the VR process if the puppet has ended
  SendPuppetCheckForCompletionToVRProcess();

  // VRGPUChild::RecvNotifyPuppetComplete will call
  // VRManager::NotifyPuppetComplete if the puppet has completed
  // in the VR Process.
}

void VRServiceHost::SendPuppetCheckForCompletionToVRProcess() {
  // This is only allowed to run in the main thread of the GPU process
  if (!XRE_IsGPUProcess()) {
    return;
  }
  // Forward this to the main thread if not already there
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = NS_NewRunnableFunction(
        "VRServiceHost::SendPuppetCheckForCompletionToVRProcess", []() -> void {
          VRServiceHost::Get()->SendPuppetCheckForCompletionToVRProcess();
        });
    NS_DispatchToMainThread(task.forget());
    return;
  }
  if (VRGPUChild::IsCreated()) {
    VRGPUChild* vrGPUChild = VRGPUChild::Get();
    vrGPUChild->SendPuppetCheckForCompletion();
  }
}

}  // namespace gfx
}  // namespace mozilla
