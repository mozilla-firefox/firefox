/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRGPUChild.h"

#include "VRManager.h"
#include "VRServiceHost.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla {
namespace gfx {

static StaticRefPtr<VRGPUChild> sVRGPUChildSingleton;

/* static */
bool VRGPUChild::InitForGPUProcess(Endpoint<PVRGPUChild>&& aEndpoint) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(!sVRGPUChildSingleton);

  if (!StaticPrefs::dom_vr_enabled() && !StaticPrefs::dom_vr_webxr_enabled()) {
    return false;
  }

  RefPtr<VRGPUChild> child(new VRGPUChild());
  if (!aEndpoint.Bind(child)) {
    return false;
  }
  sVRGPUChildSingleton = child;

#if !defined(MOZ_WIDGET_ANDROID)
  RefPtr<Runnable> task = NS_NewRunnableFunction(
      "VRServiceHost::NotifyVRProcessStarted", []() -> void {
        VRServiceHost* host = VRServiceHost::Get();
        host->NotifyVRProcessStarted();
      });

  NS_DispatchToMainThread(task.forget());
#endif

  return true;
}

/* static */
bool VRGPUChild::IsCreated() { return !!sVRGPUChildSingleton; }

/* static */
VRGPUChild* VRGPUChild::Get() {
  MOZ_RELEASE_ASSERT(IsCreated(), "VRGPUChild haven't initialized yet.");
  return sVRGPUChildSingleton;
}

/*static*/
void VRGPUChild::Shutdown() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (sVRGPUChildSingleton && !sVRGPUChildSingleton->IsClosed()) {
    sVRGPUChildSingleton->Close();
  }
  sVRGPUChildSingleton = nullptr;
}

void VRGPUChild::ActorDestroy(ActorDestroyReason aWhy) {
  VRManager* vm = VRManager::Get();
  mozilla::layers::CompositorThread()->Dispatch(
      NewRunnableMethod("VRGPUChild::ActorDestroy", vm, &VRManager::Shutdown));

  mClosed = true;
}

mozilla::ipc::IPCResult VRGPUChild::RecvNotifyPuppetComplete() {
#if !defined(MOZ_WIDGET_ANDROID)
  VRManager* vm = VRManager::Get();
  mozilla::layers::CompositorThread()->Dispatch(NewRunnableMethod(
      "VRManager::NotifyPuppetComplete", vm, &VRManager::NotifyPuppetComplete));
#endif
  return IPC_OK();
}

bool VRGPUChild::IsClosed() { return mClosed; }

}  // namespace gfx
}  // namespace mozilla
