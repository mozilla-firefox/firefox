/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRParent.h"

#include "VRGPUParent.h"
#include "gfxConfig.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/CrashReporterClient.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "nsDebugImpl.h"
#include "nsPrintfCString.h"
#include "nsThreadManager.h"

#if defined(XP_WIN)
#  include <process.h>

#  include "mozilla/gfx/DeviceManagerDx.h"
#else
#  include <unistd.h>
#endif

namespace mozilla {
namespace gfx {

using mozilla::ipc::IPCResult;

IPCResult VRParent::RecvNewGPUVRManager(Endpoint<PVRGPUParent>&& aEndpoint) {
  RefPtr<VRGPUParent> vrGPUParent =
      VRGPUParent::CreateForGPU(std::move(aEndpoint));
  if (!vrGPUParent) {
    return IPC_FAIL_NO_REASON(this);
  }

  mVRGPUParent = std::move(vrGPUParent);
  return IPC_OK();
}

IPCResult VRParent::RecvInit(nsTArray<GfxVarUpdate>&& vars,
                             const DevicePrefs& devicePrefs) {
  (void)SendInitComplete();

  gfxVars::ApplyUpdate(vars);

  // Inherit device preferences.
  gfxConfig::Inherit(Feature::HW_COMPOSITING, devicePrefs.hwCompositing());
  gfxConfig::Inherit(Feature::D3D11_COMPOSITING,
                     devicePrefs.d3d11Compositing());
  gfxConfig::Inherit(Feature::OPENGL_COMPOSITING, devicePrefs.oglCompositing());

#if defined(XP_WIN)
  if (gfxConfig::IsEnabled(Feature::D3D11_COMPOSITING)) {
    DeviceManagerDx::Get()->CreateCompositorDevices();
  }
#endif
  return IPC_OK();
}

IPCResult VRParent::RecvUpdateVar(const nsTArray<GfxVarUpdate>& aUpdate) {
  gfxVars::ApplyUpdate(aUpdate);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRParent::RecvPreferenceUpdate(const Pref& aPref) {
  Preferences::SetPreference(aPref);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRParent::RecvOpenVRControllerActionPathToVR(
    const nsCString& aPath) {
  mOpenVRControllerAction = aPath;
  return IPC_OK();
}

mozilla::ipc::IPCResult VRParent::RecvOpenVRControllerManifestPathToVR(
    const VRControllerType& aType, const nsCString& aPath) {
  mOpenVRControllerManifest.InsertOrUpdate(static_cast<uint32_t>(aType), aPath);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRParent::RecvRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage, const Maybe<FileDescriptor>& aDMDFile,
    const RequestMemoryReportResolver& aResolver) {
  MOZ_RELEASE_ASSERT(XRE_IsVRProcess());
  nsPrintfCString processName("VR (pid %u)", (unsigned)getpid());

  mozilla::dom::MemoryReportRequestClient::Start(
      aGeneration, aAnonymize, aMinimizeMemoryUsage, aDMDFile, processName,
      [&](const MemoryReport& aReport) { (void)SendAddMemoryReport(aReport); },
      aResolver);
  return IPC_OK();
}

void VRParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("Shutting down VR process early due to a crash!");
    ipc::ProcessChild::QuickExit();
  }
  if (mVRGPUParent && !mVRGPUParent->IsClosed()) {
    mVRGPUParent->Close();
  }
  mVRGPUParent = nullptr;

#ifndef NS_FREE_PERMANENT_DATA
  // No point in going through XPCOM shutdown because we don't keep persistent
  // state.
  ipc::ProcessChild::QuickExit();
#endif

#if defined(XP_WIN)
  DeviceManagerDx::Shutdown();
#endif
  gfxVars::Shutdown();
  gfxConfig::Shutdown();
  ipc::CrashReporterClient::DestroySingleton();
  // Only calling XRE_ShutdownChildProcess() at the child process
  // instead of the main process. Otherwise, it will close all child processes
  // that are spawned from the main process.
  XRE_ShutdownChildProcess();
}

bool VRParent::Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
                    const char* aParentBuildID) {
  // Initialize the thread manager before starting IPC. Otherwise, messages
  // may be posted to the main thread and we won't be able to process them.
  if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
    return false;
  }

  // Now it's safe to start IPC.
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return false;
  }

  nsDebugImpl::SetMultiprocessMode("VR");

  // This must be checked before any IPDL message, which may hit sentinel
  // errors due to parent and content processes having different
  // versions.
  MessageChannel* channel = GetIPCChannel();
  if (channel && !channel->SendBuildIDsMatchMessage(aParentBuildID)) {
    // We need to quit this process if the buildID doesn't match the parent's.
    // This can occur when an update occurred in the background.
    ipc::ProcessChild::QuickExit();
  }

  // Init crash reporter support.
  ipc::CrashReporterClient::InitSingleton(this);

  gfxConfig::Init();
  gfxVars::Initialize();
#if defined(XP_WIN)
  DeviceManagerDx::Init();
#endif
  if (NS_FAILED(NS_InitMinimalXPCOM())) {
    return false;
  }

  mozilla::ipc::SetThisProcessName("VR Process");
  return true;
}

bool VRParent::GetOpenVRControllerActionPath(nsCString* aPath) {
  if (!mOpenVRControllerAction.IsEmpty()) {
    *aPath = mOpenVRControllerAction;
    return true;
  }

  return false;
}

bool VRParent::GetOpenVRControllerManifestPath(VRControllerType aType,
                                               nsCString* aPath) {
  return mOpenVRControllerManifest.Get(static_cast<uint32_t>(aType), aPath);
}

}  // namespace gfx
}  // namespace mozilla
