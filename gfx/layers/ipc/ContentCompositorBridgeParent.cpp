/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ContentCompositorBridgeParent.h"

#include <stdint.h>  // for uint64_t

#include "apz/src/APZCTreeManager.h"  // for APZCTreeManager
#include "gfxUtils.h"
#ifdef XP_WIN
#  include "mozilla/gfx/DeviceManagerDx.h"  // for DeviceManagerDx
#  include "mozilla/layers/ImageDataSerializer.h"
#endif
#include "mozilla/layers/AnimationHelper.h"  // for CompositorAnimationStorage
#include "mozilla/layers/APZCTreeManagerParent.h"  // for APZCTreeManagerParent
#include "mozilla/layers/APZUpdater.h"             // for APZUpdater
#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/LayerTreeOwnerTracker.h"
#include "mozilla/layers/RemoteContentController.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/mozalloc.h"  // for operator new, etc
#include "nsDebug.h"           // for NS_ASSERTION, etc
#include "nsTArray.h"          // for nsTArray
#include "nsXULAppAPI.h"       // for XRE_GetAsyncIOEventTarget
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/BaseProfilerMarkerTypes.h"
#include "GeckoProfiler.h"

namespace mozilla::layers {

void EraseLayerState(LayersId aId);

void ContentCompositorBridgeParent::ActorDestroy(ActorDestroyReason aWhy) {
  mCanSend = false;

  // We must keep this object alive untill the code handling message
  // reception is finished on this thread.
  GetCurrentSerialEventTarget()->Dispatch(NewRunnableMethod(
      "layers::ContentCompositorBridgeParent::DeferredDestroy", this,
      &ContentCompositorBridgeParent::DeferredDestroy));
}

PAPZCTreeManagerParent*
ContentCompositorBridgeParent::AllocPAPZCTreeManagerParent(
    const LayersId& aLayersId) {
  // Check to see if this child process has access to this layer tree.
  if (!LayerTreeOwnerTracker::Get()->IsMapped(aLayersId, OtherPid())) {
    NS_ERROR(
        "Unexpected layers id in AllocPAPZCTreeManagerParent; dropping "
        "message...");
    return nullptr;
  }

  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState& state =
      CompositorBridgeParent::sIndirectLayerTrees[aLayersId];

  // If the widget has shutdown its compositor, we may not have had a chance yet
  // to unmap our layers id, and we could get here without a parent compositor.
  // In this case return an empty APZCTM.
  if (!state.mParent) {
    // Note: we immediately call ClearTree since otherwise the APZCTM will
    // retain a reference to itself, through the checkerboard observer.
    LayersId dummyId{0};
    const bool connectedToWebRender = false;
    RefPtr<APZCTreeManager> temp = APZCTreeManager::Create(dummyId);
    RefPtr<APZUpdater> tempUpdater = new APZUpdater(temp, connectedToWebRender);
    tempUpdater->ClearTree(dummyId);
    return new APZCTreeManagerParent(aLayersId, temp, tempUpdater);
  }

  // If we do not have APZ enabled, we should gracefully fail.
  if (!state.mParent->GetOptions().UseAPZ()) {
    return nullptr;
  }

  state.mParent->AllocateAPZCTreeManagerParent(lock, aLayersId, state);
  return state.mApzcTreeManagerParent;
}

bool ContentCompositorBridgeParent::DeallocPAPZCTreeManagerParent(
    PAPZCTreeManagerParent* aActor) {
  APZCTreeManagerParent* parent = static_cast<APZCTreeManagerParent*>(aActor);

  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  auto iter =
      CompositorBridgeParent::sIndirectLayerTrees.find(parent->GetLayersId());
  if (iter != CompositorBridgeParent::sIndirectLayerTrees.end()) {
    CompositorBridgeParent::LayerTreeState& state = iter->second;
    MOZ_ASSERT(state.mApzcTreeManagerParent == parent);
    state.mApzcTreeManagerParent = nullptr;
  }

  delete parent;

  return true;
}

PAPZParent* ContentCompositorBridgeParent::AllocPAPZParent(
    const LayersId& aLayersId) {
  // Check to see if this child process has access to this layer tree.
  if (!LayerTreeOwnerTracker::Get()->IsMapped(aLayersId, OtherPid())) {
    NS_ERROR("Unexpected layers id in AllocPAPZParent; dropping message...");
    return nullptr;
  }

  RemoteContentController* controller = new RemoteContentController();

  // Increment the controller's refcount before we return it. This will keep the
  // controller alive until it is released by IPDL in DeallocPAPZParent.
  controller->AddRef();

  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  CompositorBridgeParent::LayerTreeState& state =
      CompositorBridgeParent::sIndirectLayerTrees[aLayersId];
  MOZ_ASSERT(!state.mController);
  state.mController = controller;

  return controller;
}

bool ContentCompositorBridgeParent::DeallocPAPZParent(PAPZParent* aActor) {
  RemoteContentController* controller =
      static_cast<RemoteContentController*>(aActor);
  controller->Release();
  return true;
}

PWebRenderBridgeParent*
ContentCompositorBridgeParent::AllocPWebRenderBridgeParent(
    const wr::PipelineId& aPipelineId, const LayoutDeviceIntSize& aSize,
    const WindowKind& aWindowKind) {
  LayersId layersId = wr::AsLayersId(aPipelineId);
  // Check to see if this child process has access to this layer tree.
  if (!LayerTreeOwnerTracker::Get()->IsMapped(layersId, OtherPid())) {
    NS_ERROR(
        "Unexpected layers id in AllocPWebRenderBridgeParent; dropping "
        "message...");
    return nullptr;
  }

  RefPtr<CompositorBridgeParent> cbp = nullptr;
  RefPtr<WebRenderBridgeParent> root = nullptr;

  {  // scope lock
    StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
    MOZ_ASSERT(CompositorBridgeParent::sIndirectLayerTrees.find(layersId) !=
               CompositorBridgeParent::sIndirectLayerTrees.end());
    MOZ_ASSERT(
        CompositorBridgeParent::sIndirectLayerTrees[layersId].mWrBridge ==
        nullptr);
    cbp = CompositorBridgeParent::sIndirectLayerTrees[layersId].mParent;
    if (cbp) {
      root = CompositorBridgeParent::sIndirectLayerTrees[cbp->RootLayerTreeId()]
                 .mWrBridge;
    }
  }

  RefPtr<wr::WebRenderAPI> api;
  if (root) {
    api = root->GetWebRenderAPI();
  }

  if (!root || !api) {
    // This could happen when this function is called after
    // CompositorBridgeParent destruction. This was observed during Tab move
    // between different windows.
    NS_WARNING(
        nsPrintfCString("Created child without a matching parent? root %p",
                        root.get())
            .get());
    nsCString error("NO_PARENT");
    WebRenderBridgeParent* parent =
        WebRenderBridgeParent::CreateDestroyed(aPipelineId, std::move(error));
    parent->AddRef();  // IPDL reference
    return parent;
  }

  api = api->Clone();
  RefPtr<AsyncImagePipelineManager> holder = root->AsyncImageManager();
  WebRenderBridgeParent* parent = new WebRenderBridgeParent(
      this, aPipelineId, root->CompositorScheduler(), std::move(api),
      std::move(holder), cbp->GetVsyncInterval());
  parent->AddRef();  // IPDL reference

  {  // scope lock
    StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
    CompositorBridgeParent::sIndirectLayerTrees[layersId]
        .mContentCompositorBridgeParent = this;
    CompositorBridgeParent::sIndirectLayerTrees[layersId].mWrBridge = parent;
  }

  return parent;
}

bool ContentCompositorBridgeParent::DeallocPWebRenderBridgeParent(
    PWebRenderBridgeParent* aActor) {
  WebRenderBridgeParent* parent = static_cast<WebRenderBridgeParent*>(aActor);
  EraseLayerState(wr::AsLayersId(parent->PipelineId()));
  parent->Release();  // IPDL reference
  return true;
}

mozilla::ipc::IPCResult ContentCompositorBridgeParent::RecvNotifyChildCreated(
    const LayersId& child, CompositorOptions* aOptions) {
  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  for (auto it = CompositorBridgeParent::sIndirectLayerTrees.begin();
       it != CompositorBridgeParent::sIndirectLayerTrees.end(); it++) {
    CompositorBridgeParent::LayerTreeState& lts = it->second;
    if (lts.mParent && lts.mContentCompositorBridgeParent == this) {
      lts.mParent->NotifyChildCreated(child);
      *aOptions = lts.mParent->GetOptions();
      return IPC_OK();
    }
  }
  return IPC_FAIL_NO_REASON(this);
}

mozilla::ipc::IPCResult
ContentCompositorBridgeParent::RecvMapAndNotifyChildCreated(
    const LayersId& child, const base::ProcessId& pid,
    CompositorOptions* aOptions) {
  // This can only be called from the browser process, as the mapping
  // ensures proper window ownership of layer trees.
  return IPC_FAIL_NO_REASON(this);
}

mozilla::ipc::IPCResult
ContentCompositorBridgeParent::RecvNotifyMemoryPressure() {
  // This can only be called from the browser process.
  return IPC_FAIL_NO_REASON(this);
}

mozilla::ipc::IPCResult ContentCompositorBridgeParent::RecvCheckContentOnlyTDR(
    const uint32_t& sequenceNum, bool* isContentOnlyTDR) {
  *isContentOnlyTDR = false;
#ifdef XP_WIN
  gfx::ContentDeviceData compositor;

  gfx::DeviceManagerDx* dm = gfx::DeviceManagerDx::Get();

  // Check that the D3D11 device sequence numbers match.
  gfx::D3D11DeviceStatus status;
  dm->ExportDeviceInfo(&status);

  if (sequenceNum == static_cast<uint32_t>(status.sequenceNumber()) &&
      !dm->HasDeviceReset()) {
    *isContentOnlyTDR = true;
  }

#endif
  return IPC_OK();
};

mozilla::ipc::IPCResult
ContentCompositorBridgeParent::RecvCheckAndClearWRDidRasterize(
    const LayersId& aId, bool* aDidRasterize) {
  *aDidRasterize = false;

  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aId);
  if (!state || !state->mParent) {
    return IPC_OK();
  }

  // Forward to the parent compositor which owns the renderer
  RefPtr<wr::WebRenderAPI> api;
  {
    StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
    LayersId rootId = state->mParent->RootLayerTreeId();
    const CompositorBridgeParent::LayerTreeState& rootState =
        CompositorBridgeParent::sIndirectLayerTrees[rootId];
    if (rootState.mWrBridge) {
      api = rootState.mWrBridge->GetWebRenderAPI();
    }
  }

  if (api) {
    *aDidRasterize = api->CheckAndClearDidRasterize();
  }

  return IPC_OK();
}

void ContentCompositorBridgeParent::DidCompositeLocked(
    LayersId aId, const VsyncId& aVsyncId, TimeStamp& aCompositeStart,
    TimeStamp& aCompositeEnd) {
  CompositorBridgeParent::sIndirectLayerTreesLock.AssertCurrentThreadOwns();
  if (CompositorBridgeParent::sIndirectLayerTrees[aId].mWrBridge) {
    MOZ_ASSERT(false);  // this should never get called for a WR compositor
  }
}

bool ContentCompositorBridgeParent::SetTestSampleTime(const LayersId& aId,
                                                      const TimeStamp& aTime) {
  MOZ_ASSERT(aId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aId);
  if (!state) {
    return false;
  }

  MOZ_ASSERT(state->mParent);
  return state->mParent->SetTestSampleTime(aId, aTime);
}

void ContentCompositorBridgeParent::LeaveTestMode(const LayersId& aId) {
  MOZ_ASSERT(aId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aId);
  if (!state) {
    return;
  }

  MOZ_ASSERT(state->mParent);
  state->mParent->LeaveTestMode(aId);
}

void ContentCompositorBridgeParent::SetTestAsyncScrollOffset(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const CSSPoint& aPoint) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state) {
    return;
  }

  MOZ_ASSERT(state->mParent);
  state->mParent->SetTestAsyncScrollOffset(aLayersId, aScrollId, aPoint);
}

void ContentCompositorBridgeParent::SetTestAsyncZoom(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const LayerToParentLayerScale& aZoom) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state) {
    return;
  }

  MOZ_ASSERT(state->mParent);
  state->mParent->SetTestAsyncZoom(aLayersId, aScrollId, aZoom);
}

void ContentCompositorBridgeParent::FlushApzRepaints(
    const LayersId& aLayersId) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  state->mParent->FlushApzRepaints(aLayersId);
}

void ContentCompositorBridgeParent::GetAPZTestData(const LayersId& aLayersId,
                                                   APZTestData* aOutData) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  state->mParent->GetAPZTestData(aLayersId, aOutData);
}

void ContentCompositorBridgeParent::GetFrameUniformity(
    const LayersId& aLayersId, FrameUniformityData* aOutData) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  state->mParent->GetFrameUniformity(aLayersId, aOutData);
}

void ContentCompositorBridgeParent::SetConfirmedTargetAPZC(
    const LayersId& aLayersId, const uint64_t& aInputBlockId,
    nsTArray<ScrollableLayerGuid>&& aTargets) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  state->mParent->SetConfirmedTargetAPZC(aLayersId, aInputBlockId,
                                         std::move(aTargets));
}

void ContentCompositorBridgeParent::EndWheelTransaction(
    const LayersId& aLayersId,
    PWebRenderBridgeParent::EndWheelTransactionResolver&& aResolve) {
  MOZ_ASSERT(aLayersId.IsValid());
  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  state->mParent->EndWheelTransaction(aLayersId, std::move(aResolve));
}

void ContentCompositorBridgeParent::DeferredDestroy() { mSelfRef = nullptr; }

ContentCompositorBridgeParent::~ContentCompositorBridgeParent() {
  MOZ_ASSERT(XRE_GetAsyncIOEventTarget());
}

PTextureParent* ContentCompositorBridgeParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const LayersId& aId, const uint64_t& aSerial,
    const wr::MaybeExternalImageId& aExternalImageId) {
  CompositorBridgeParent::LayerTreeState* state = nullptr;

  StaticMonitorAutoLock lock(CompositorBridgeParent::sIndirectLayerTreesLock);
  auto itr = CompositorBridgeParent::sIndirectLayerTrees.find(aId);
  if (CompositorBridgeParent::sIndirectLayerTrees.end() != itr) {
    state = &itr->second;
  }

  TextureFlags flags = aFlags;

  LayersBackend actualBackend = LayersBackend::LAYERS_NONE;
  if (!state) {
    // The compositor was recreated, and we're receiving layers updates for a
    // a layer manager that will soon be discarded or invalidated. We can't
    // return null because this will mess up deserialization later and we'll
    // kill the content process. Instead, we signal that the underlying
    // TextureHost should not attempt to access the compositor.
    flags |= TextureFlags::INVALID_COMPOSITOR;
  } else if (actualBackend != LayersBackend::LAYERS_NONE &&
             aLayersBackend != actualBackend) {
    gfxDevCrash(gfx::LogReason::PAllocTextureBackendMismatch)
        << "Texture backend is wrong";
  }

  return TextureHost::CreateIPDLActor(
      this, aSharedData, std::move(aReadLock), aLayersBackend, aFlags,
      mCompositorManager->GetContentId(), aSerial, aExternalImageId);
}

bool ContentCompositorBridgeParent::DeallocPTextureParent(
    PTextureParent* actor) {
  return TextureHost::DestroyIPDLActor(actor);
}

bool ContentCompositorBridgeParent::IsSameProcess() const {
  return OtherPid() == base::GetCurrentProcId();
}

void ContentCompositorBridgeParent::ObserveLayersUpdate(LayersId aLayersId,
                                                        bool aActive) {
  MOZ_ASSERT(aLayersId.IsValid());

  CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aLayersId);
  if (!state || !state->mParent) {
    return;
  }

  (void)state->mParent->SendObserveLayersUpdate(aLayersId, aActive);
}

}  // namespace mozilla::layers
