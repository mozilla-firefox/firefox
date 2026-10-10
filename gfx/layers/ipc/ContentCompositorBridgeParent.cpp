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

already_AddRefed<PAPZCTreeManagerParent>
ContentCompositorBridgeParent::AllocPAPZCTreeManagerParent(
    const LayersId& aLayersId) {
  // Check to see if this child process has access to this layer tree.
  if (!LayerTreeOwnerTracker::Get()->IsMapped(aLayersId, OtherPid())) {
    NS_ERROR(
        "Unexpected layers id in AllocPAPZCTreeManagerParent; dropping "
        "message...");
    return nullptr;
  }

  return CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProofOfLock)
          -> already_AddRefed<PAPZCTreeManagerParent> {
        CompositorBridgeParent::LayerTreeState& state =
            CompositorBridgeParent::EnsureLayerTreeStateUnderLock(aLayersId,
                                                                  aProofOfLock);

        // If the widget has shutdown its compositor, we may not have had a
        // chance yet to unmap our layers id, and we could get here without a
        // parent compositor. In this case return an empty APZCTM.
        if (!state.mParent) {
          // Note: we immediately call ClearTree since otherwise the APZCTM will
          // retain a reference to itself, through the checkerboard observer.
          LayersId dummyId{0};
          const bool connectedToWebRender = false;
          RefPtr<APZCTreeManager> temp = APZCTreeManager::Create(dummyId);
          RefPtr tempUpdater =
              MakeRefPtr<APZUpdater>(temp, connectedToWebRender);
          tempUpdater->ClearTree(dummyId);
          return MakeAndAddRef<APZCTreeManagerParent>(aLayersId, temp,
                                                      tempUpdater);
        }

        // If we do not have APZ enabled, we should gracefully fail.
        if (!state.mParent->GetOptions().UseAPZ()) {
          return nullptr;
        }

        return state.mParent->AllocateAPZCTreeManagerParent(aProofOfLock,
                                                            aLayersId, state);
      });
}

already_AddRefed<PAPZParent> ContentCompositorBridgeParent::AllocPAPZParent(
    const LayersId& aLayersId) {
  // Check to see if this child process has access to this layer tree.
  if (!LayerTreeOwnerTracker::Get()->IsMapped(aLayersId, OtherPid())) {
    NS_ERROR("Unexpected layers id in AllocPAPZParent; dropping message...");
    return nullptr;
  }

  auto controller = MakeRefPtr<RemoteContentController>(aLayersId);

  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProofOfLock) {
        CompositorBridgeParent::LayerTreeState& state =
            CompositorBridgeParent::EnsureLayerTreeStateUnderLock(aLayersId,
                                                                  aProofOfLock);
        MOZ_ASSERT(!state.mController);
        state.mController = controller;
      });

  return controller.forget();
}

already_AddRefed<PWebRenderBridgeParent>
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

  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProofOfLock) {
        MOZ_ASSERT(CompositorBridgeParent::GetLayerTreeStateUnderLock(
            layersId, aProofOfLock));
        CompositorBridgeParent::LayerTreeState& state =
            CompositorBridgeParent::EnsureLayerTreeStateUnderLock(layersId,
                                                                  aProofOfLock);
        MOZ_ASSERT(state.mWrBridge == nullptr);
        cbp = state.mParent;
        if (cbp) {
          root = CompositorBridgeParent::EnsureLayerTreeStateUnderLock(
                     cbp->RootLayerTreeId(), aProofOfLock)
                     .mWrBridge;
        }
      });

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
    return WebRenderBridgeParent::CreateDestroyed(aPipelineId,
                                                  std::move(error));
  }

  api = api->Clone();
  RefPtr<AsyncImagePipelineManager> holder = root->AsyncImageManager();
  auto parent = MakeRefPtr<WebRenderBridgeParent>(
      this, aPipelineId, root->CompositorScheduler(), std::move(api),
      std::move(holder), cbp->GetVsyncInterval());

  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProofOfLock) {
        CompositorBridgeParent::LayerTreeState& state =
            CompositorBridgeParent::EnsureLayerTreeStateUnderLock(layersId,
                                                                  aProofOfLock);
        state.mContentCompositorBridgeParent = this;
        state.mWrBridge = parent;
      });

  return parent.forget();
}

mozilla::ipc::IPCResult
ContentCompositorBridgeParent::RecvMapAndNotifyChildCreated(
    const LayersId& child, const LayersId& embedderId,
    const base::ProcessId& pid, CompositorOptions* aOptions) {
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
  if (NS_WARN_IF(!LayerTreeOwnerTracker::Get()->IsMapped(aId, OtherPid()))) {
    return IPC_OK();
  }

  *aDidRasterize = false;

  const CompositorBridgeParent::LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(aId);
  if (!state || !state->mParent) {
    return IPC_OK();
  }

  // Forward to the parent compositor which owns the renderer
  RefPtr<wr::WebRenderAPI> api;
  CompositorBridgeParent::WithIndirectLayerTreesLock(
      [&](const StaticMonitorAutoLock& aProofOfLock) {
        LayersId rootId = state->mParent->RootLayerTreeId();
        const CompositorBridgeParent::LayerTreeState& rootState =
            CompositorBridgeParent::EnsureLayerTreeStateUnderLock(rootId,
                                                                  aProofOfLock);
        if (rootState.mWrBridge) {
          api = rootState.mWrBridge->GetWebRenderAPI();
        }
      });

  if (api) {
    *aDidRasterize = api->CheckAndClearDidRasterize();
  }

  return IPC_OK();
}

void ContentCompositorBridgeParent::DidCompositeLocked(
    LayersId aId, const VsyncId& aVsyncId, TimeStamp& aCompositeStart,
    TimeStamp& aCompositeEnd, const StaticMonitorAutoLock& aProofOfLock) {
  if (CompositorBridgeParent::EnsureLayerTreeStateUnderLock(aId, aProofOfLock)
          .mWrBridge) {
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
    // The boolean value will never used so it doesn't matter whether it's true
    // or false.
    aResolve(true);
    return;
  }

  state->mParent->EndWheelTransaction(aLayersId, std::move(aResolve));
}

void ContentCompositorBridgeParent::DeferredDestroy() { mSelfRef = nullptr; }

ContentCompositorBridgeParent::~ContentCompositorBridgeParent() {
  MOZ_ASSERT(XRE_GetAsyncIOEventTarget());
}

already_AddRefed<PTextureParent>
ContentCompositorBridgeParent::AllocPTextureParent(
    const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
    const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
    const uint64_t& aSerial, const wr::MaybeExternalImageId& aExternalImageId) {
  if (aSharedData.type() == SurfaceDescriptor::TSurfaceDescriptorDcompSurface) {
    return nullptr;
  }
  if (aExternalImageId.isSome() &&
      !OwnsExternalImageId(aExternalImageId.ref())) {
    NS_ERROR("We do not own this external image id.");
    return nullptr;
  }
  return TextureHost::CreateIPDLActor(
      this, aSharedData, std::move(aReadLock), aLayersBackend, aFlags,
      mCompositorManager->GetContentId(), aSerial, aExternalImageId);
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
