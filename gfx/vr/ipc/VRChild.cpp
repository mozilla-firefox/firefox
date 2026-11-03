/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRChild.h"

#include "VRProcessManager.h"
#include "VRProcessParent.h"
#include "gfxConfig.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/VsyncDispatcher.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/IpcMetrics.h"

namespace mozilla {
namespace gfx {

class OpenVRControllerManifestManager {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(OpenVRControllerManifestManager)
 public:
  explicit OpenVRControllerManifestManager() = default;

  void SetOpenVRControllerActionPath(const nsCString& aPath) {
    mAction = aPath;
  }

  void SetOpenVRControllerManifestPath(VRControllerType aType,
                                       const nsCString& aPath) {
    mManifest.InsertOrUpdate(static_cast<uint32_t>(aType), aPath);
  }

  bool GetActionPath(nsCString* aPath) {
    if (!mAction.IsEmpty()) {
      *aPath = mAction;
      return true;
    }
    return false;
  }

  bool GetManifestPath(VRControllerType aType, nsCString* aPath) {
    return mManifest.Get(static_cast<uint32_t>(aType), aPath);
  }

  OpenVRControllerManifestManager(const OpenVRControllerManifestManager&) =
      delete;
  const OpenVRControllerManifestManager& operator=(
      const OpenVRControllerManifestManager&) = delete;

 private:
  ~OpenVRControllerManifestManager() {
    if (!mAction.IsEmpty() && remove(mAction.get()) != 0) {
      MOZ_ASSERT(false, "Delete controller action file failed.");
    }
    mAction = "";

    for (const auto& path : mManifest.Values()) {
      if (!path.IsEmpty() && remove(path.get()) != 0) {
        MOZ_ASSERT(false, "Delete controller manifest file failed.");
      }
    }
    mManifest.Clear();
  }

  nsCString mAction;
  nsTHashMap<nsUint32HashKey, nsCString> mManifest;
};

StaticRefPtr<OpenVRControllerManifestManager> sOpenVRControllerManifestManager;

VRChild::VRChild(VRProcessParent* aHost) : mHost(aHost), mVRReady(false) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
}

VRChild::~VRChild() = default;

mozilla::ipc::IPCResult VRChild::RecvAddMemoryReport(
    const MemoryReport& aReport) {
  if (mMemoryReportRequest) {
    mMemoryReportRequest->RecvReport(aReport);
  }
  return IPC_OK();
}

void VRChild::ActorDestroy(ActorDestroyReason aWhy) {
  if (aWhy == AbnormalShutdown) {
    GenerateCrashReport();

    glean::subprocess::abnormal_abort
        .Get(nsDependentCString(
            XRE_GeckoProcessTypeToString(GeckoProcessType_VR)))
        .Add(1);
  }
  gfxVars::RemoveReceiver(this);
  mHost->OnChannelClosed();
  mHost = nullptr;
}

void VRChild::Init() {
  nsTArray<GfxVarUpdate> updates = gfxVars::FetchNonDefaultVars();

  DevicePrefs devicePrefs;
  devicePrefs.hwCompositing() = gfxConfig::GetValue(Feature::HW_COMPOSITING);
  devicePrefs.d3d11Compositing() =
      gfxConfig::GetValue(Feature::D3D11_COMPOSITING);
  devicePrefs.oglCompositing() =
      gfxConfig::GetValue(Feature::OPENGL_COMPOSITING);

  SendInit(updates, devicePrefs);

  if (!sOpenVRControllerManifestManager) {
    sOpenVRControllerManifestManager = new OpenVRControllerManifestManager();
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "ClearOnShutdown OpenVRControllerManifestManager",
        []() { ClearOnShutdown(&sOpenVRControllerManifestManager); }));
  }

  nsCString output;
  if (sOpenVRControllerManifestManager->GetActionPath(&output)) {
    SendOpenVRControllerActionPathToVR(output);
  }
  if (sOpenVRControllerManifestManager->GetManifestPath(
          VRControllerType::HTCVive, &output)) {
    SendOpenVRControllerManifestPathToVR(VRControllerType::HTCVive, output);
  }
  if (sOpenVRControllerManifestManager->GetManifestPath(VRControllerType::MSMR,
                                                        &output)) {
    SendOpenVRControllerManifestPathToVR(VRControllerType::MSMR, output);
  }
  if (sOpenVRControllerManifestManager->GetManifestPath(
          VRControllerType::ValveIndex, &output)) {
    SendOpenVRControllerManifestPathToVR(VRControllerType::ValveIndex, output);
  }
  gfxVars::AddReceiver(this);
}

bool VRChild::EnsureVRReady() {
  if (!mVRReady) {
    return false;
  }

  return true;
}

mozilla::ipc::IPCResult VRChild::RecvOpenVRControllerActionPathToParent(
    const nsCString& aPath) {
  sOpenVRControllerManifestManager->SetOpenVRControllerActionPath(aPath);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRChild::RecvOpenVRControllerManifestPathToParent(
    const VRControllerType& aType, const nsCString& aPath) {
  sOpenVRControllerManifestManager->SetOpenVRControllerManifestPath(aType,
                                                                    aPath);
  return IPC_OK();
}

mozilla::ipc::IPCResult VRChild::RecvInitComplete() {
  // We synchronously requested VR parameters before this arrived.
  mVRReady = true;
  return IPC_OK();
}

bool VRChild::SendRequestMemoryReport(const uint32_t& aGeneration,
                                      const bool& aAnonymize,
                                      const bool& aMinimizeMemoryUsage,
                                      const Maybe<FileDescriptor>& aDMDFile) {
  mMemoryReportRequest = std::make_unique<MemoryReportRequestHost>(aGeneration);

  PVRChild::SendRequestMemoryReport(aGeneration, aAnonymize,
                                    aMinimizeMemoryUsage, aDMDFile)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](uint32_t aGeneration2) {
            if (VRProcessManager* vpm = VRProcessManager::Get()) {
              if (VRChild* child = vpm->GetVRChild()) {
                if (child->mMemoryReportRequest) {
                  child->mMemoryReportRequest->Finish(aGeneration2);
                  child->mMemoryReportRequest = nullptr;
                }
              }
            }
          },
          [](mozilla::ipc::ResponseRejectReason) {
            if (VRProcessManager* vpm = VRProcessManager::Get()) {
              if (VRChild* child = vpm->GetVRChild()) {
                child->mMemoryReportRequest = nullptr;
              }
            }
          });

  return true;
}

void VRChild::OnVarChanged(const nsTArray<GfxVarUpdate>& aVar) {
  SendUpdateVar(aVar);
}

class DeferredDeleteVRChild : public Runnable {
 public:
  explicit DeferredDeleteVRChild(RefPtr<VRChild>&& aChild)
      : Runnable("gfx::DeferredDeleteVRChild"), mChild(std::move(aChild)) {}

  NS_IMETHODIMP Run() override { return NS_OK; }

 private:
  RefPtr<VRChild> mChild;
};

/* static */
void VRChild::Destroy(RefPtr<VRChild>&& aChild) {
  NS_DispatchToMainThread(new DeferredDeleteVRChild(std::move(aChild)));
}

}  // namespace gfx
}  // namespace mozilla
