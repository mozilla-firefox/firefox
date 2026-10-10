/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_ContentCompositorBridgeParent_h
#define mozilla_layers_ContentCompositorBridgeParent_h

#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla::layers {

class CompositorOptions;

/**
 * This class handles layer updates pushed directly from child processes to
 * the compositor thread. It's associated with a CompositorBridgeParent on the
 * compositor thread. While it uses the PCompositorBridge protocol to manage
 * these updates, it doesn't actually drive compositing itself. For that it
 * hands off work to the CompositorBridgeParent it's associated with.
 */
class ContentCompositorBridgeParent final : public CompositorBridgeParentBase {
  friend class CompositorBridgeParent;

 public:
  explicit ContentCompositorBridgeParent(CompositorManagerParent* aManager,
                                         uint32_t aNamespace)
      : CompositorBridgeParentBase(aManager, aNamespace),
        mDestroyCalled(false) {}

  void ActorDestroy(ActorDestroyReason aWhy) override;

  // FIXME/bug 774388: work out what shutdown protocol we need.
  mozilla::ipc::IPCResult RecvInitialize(
      const LayersId& aRootLayerTreeId) override {
    return IPC_FAIL_NO_REASON(this);
  }
  mozilla::ipc::IPCResult RecvWillClose() override { return IPC_OK(); }
  mozilla::ipc::IPCResult RecvPause() override { return IPC_OK(); }
  mozilla::ipc::IPCResult RecvRequestFxrOutput() override {
    return IPC_FAIL_NO_REASON(this);
  }
  mozilla::ipc::IPCResult RecvResume() override { return IPC_OK(); }
  mozilla::ipc::IPCResult RecvResumeAsync() override { return IPC_OK(); }
  mozilla::ipc::IPCResult RecvNotifyChildCreated(
      const LayersId& child, const LayersId& embedderId,
      CompositorOptions* aOptions) override {
    return IPC_FAIL_NO_REASON(this);
  }
  mozilla::ipc::IPCResult RecvMapAndNotifyChildCreated(
      const LayersId& child, const LayersId& embedderId,
      const base::ProcessId& pid, CompositorOptions* aOptions) override;
  mozilla::ipc::IPCResult RecvNotifyChildRecreated(
      const LayersId& child, const LayersId& embedderId,
      CompositorOptions* aOptions) override {
    return IPC_FAIL_NO_REASON(this);
  }
  mozilla::ipc::IPCResult RecvAdoptChild(const LayersId& child,
                                         const LayersId& embedderId) override {
    return IPC_FAIL_NO_REASON(this);
  }
  mozilla::ipc::IPCResult RecvFlushRendering(
      const wr::RenderReasons&) override {
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvFlushRenderingAsync(
      const wr::RenderReasons&) override {
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvForcePresent(const wr::RenderReasons&) override {
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvWaitOnTransactionProcessed() override {
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvStartFrameTimeRecording(
      const int32_t& aBufferSize, uint32_t* aOutStartIndex) override {
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvStopFrameTimeRecording(
      const uint32_t& aStartIndex, nsTArray<float>* intervals) override {
    return IPC_OK();
  }

  mozilla::ipc::IPCResult RecvNotifyMemoryPressure() override;

  mozilla::ipc::IPCResult RecvCheckContentOnlyTDR(
      const uint32_t& sequenceNum, bool* isContentOnlyTDR) override;

  mozilla::ipc::IPCResult RecvCheckAndClearWRDidRasterize(
      const LayersId& aId, bool* aDidRasterize) override;

  mozilla::ipc::IPCResult RecvDynamicToolbarOffsetChanged(
      const int32_t& aOffset) override {
    return IPC_FAIL_NO_REASON(this);
  }

  mozilla::ipc::IPCResult RecvBeginRecording(
      const TimeStamp& aRecordingStart,
      BeginRecordingResolver&& aResolve) override {
    aResolve(false);
    return IPC_OK();
  }

  mozilla::ipc::IPCResult RecvEndRecording(
      EndRecordingResolver&& aResolve) override {
    aResolve(Nothing());
    return IPC_OK();
  }

  bool SetTestSampleTime(const LayersId& aId, const TimeStamp& aTime) override;
  void LeaveTestMode(const LayersId& aId) override;
  void SetTestAsyncScrollOffset(const LayersId& aLayersId,
                                const ScrollableLayerGuid::ViewID& aScrollId,
                                const CSSPoint& aPoint) override;
  void SetTestAsyncZoom(const LayersId& aLayersId,
                        const ScrollableLayerGuid::ViewID& aScrollId,
                        const LayerToParentLayerScale& aZoom) override;
  void FlushApzRepaints(const LayersId& aLayersId) override;
  void GetAPZTestData(const LayersId& aLayersId,
                      APZTestData* aOutData) override;
  void GetFrameUniformity(const LayersId& aLayersId,
                          FrameUniformityData* aOutData) override;
  void SetConfirmedTargetAPZC(
      const LayersId& aLayersId, const uint64_t& aInputBlockId,
      nsTArray<ScrollableLayerGuid>&& aTargets) override;
  void EndWheelTransaction(
      const LayersId& aLayersId,
      PWebRenderBridgeParent::EndWheelTransactionResolver&& aResolve) override;

  // Use DidCompositeLocked if you already hold a lock on
  // sIndirectLayerTreesLock (pass the proof token); otherwise use DidComposite,
  // which would request the lock automatically.
  void DidCompositeLocked(LayersId aId, const VsyncId& aVsyncId,
                          TimeStamp& aCompositeStart, TimeStamp& aCompositeEnd,
                          const StaticMonitorAutoLock& aProofOfLock);

  already_AddRefed<PTextureParent> AllocPTextureParent(
      const SurfaceDescriptor& aSharedData, ReadLockDescriptor& aReadLock,
      const LayersBackend& aLayersBackend, const TextureFlags& aFlags,
      const uint64_t& aSerial,
      const wr::MaybeExternalImageId& aExternalImageId) override;

  bool IsSameProcess() const override;

  already_AddRefed<PCompositorWidgetParent> AllocPCompositorWidgetParent(
      const CompositorWidgetInitData& aInitData) override {
    // Not allowed.
    return nullptr;
  }

  already_AddRefed<PAPZCTreeManagerParent> AllocPAPZCTreeManagerParent(
      const LayersId& aLayersId) override;

  already_AddRefed<PAPZParent> AllocPAPZParent(
      const LayersId& aLayersId) override;

  already_AddRefed<PWebRenderBridgeParent> AllocPWebRenderBridgeParent(
      const wr::PipelineId& aPipelineId, const LayoutDeviceIntSize& aSize,
      const WindowKind& aWindowKind) override;
  // Nothing to do as content WebRenderBridgeParents are fully initialized at
  // construction time.
  void EnsureWebRenderBridgeParentInitialized() override {}

  void ObserveLayersUpdate(LayersId aLayersId, bool aActive) override;

  bool IsRemote() const override { return true; }

  void ScheduleRenderOnCompositorThread(wr::RenderReasons aReasons) override {
    MOZ_ASSERT_UNREACHABLE("Unused for content!");
  }

 private:
  // Private destructor, to discourage deletion outside of Release():
  virtual ~ContentCompositorBridgeParent();

  void DeferredDestroy();

  // There can be many CPCPs, and IPDL-generated code doesn't hold a
  // reference to top-level actors.  So we hold a reference to
  // ourself.  This is released (deferred) in ActorDestroy().
  RefPtr<ContentCompositorBridgeParent> mSelfRef;

  bool mDestroyCalled;
};

}  // namespace mozilla::layers

#endif  // mozilla_layers_ContentCompositorBridgeParent_h
