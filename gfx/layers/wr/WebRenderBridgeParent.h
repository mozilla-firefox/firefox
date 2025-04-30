/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_WebRenderBridgeParent_h
#define mozilla_layers_WebRenderBridgeParent_h

#include <unordered_map>
#include <unordered_set>

#include "CompositableHost.h"  // for CompositableHost, ImageCompositeNotificationInfo
#include "GLContextProvider.h"
#include "mozilla/DataMutex.h"
#include "mozilla/layers/CompositableTransactionParent.h"
#include "mozilla/layers/CompositorVsyncSchedulerOwner.h"
#include "mozilla/layers/PWebRenderBridgeParent.h"
#include "mozilla/HashTable.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsTArrayForwardDeclare.h"
#include "WindowRenderer.h"

namespace mozilla {

namespace gl {
class GLContext;
}

namespace widget {
class CompositorWidget;
}

namespace wr {
class WebRenderAPI;
class WebRenderPipelineInfo;
}  // namespace wr

namespace layers {

class AsyncImagePipelineManager;
class Compositor;
class CompositorBridgeParentBase;
class CompositorVsyncScheduler;
class OMTASampler;
class RemoteTextureTxnScheduler;
class UiCompositorControllerParent;
class WebRenderBridgeParentRef;
class WebRenderImageHost;
struct WrAnimations;

struct CompositorAnimationIdsForEpoch {
  CompositorAnimationIdsForEpoch(const wr::Epoch& aEpoch,
                                 nsTArray<uint64_t>&& aIds)
      : mEpoch(aEpoch), mIds(std::move(aIds)) {}

  wr::Epoch mEpoch;
  nsTArray<uint64_t> mIds;
};

class WebRenderBridgeParent final : public PWebRenderBridgeParent,
                                    public CompositorVsyncSchedulerOwner,
                                    public CompositableParentManager,
                                    public FrameRecorder {
 public:
  WebRenderBridgeParent(CompositorBridgeParentBase* aCompositorBridge,
                        const wr::PipelineId& aPipelineId,
                        widget::CompositorWidget* aWidget,
                        CompositorVsyncScheduler* aScheduler,
                        RefPtr<wr::WebRenderAPI>&& aApi,
                        RefPtr<AsyncImagePipelineManager>&& aImageMgr,
                        TimeDuration aVsyncRate);

  static WebRenderBridgeParent* CreateDestroyed(
      const wr::PipelineId& aPipelineId, nsCString&& aError);

  wr::PipelineId PipelineId() { return mPipelineId; }
  already_AddRefed<wr::WebRenderAPI> GetWebRenderAPI() {
    return do_AddRef(mApi);
  }
  AsyncImagePipelineManager* AsyncImageManager() { return mAsyncImageManager; }
  CompositorVsyncScheduler* CompositorScheduler() {
    return mCompositorScheduler.get();
  }
  CompositorBridgeParentBase* GetCompositorBridge() {
    return mCompositorBridge;
  }

  void UpdateQualitySettings();
  void UpdateDebugFlags();
  void UpdateParameters();
  void UpdateBoolParameters();
  void UpdateProfilerUI();

  mozilla::ipc::IPCResult RecvEnsureConnected(
      TextureFactoryIdentifier* aTextureFactoryIdentifier,
      MaybeIdNamespace* aMaybeIdNamespace, nsCString* aError) override;

  mozilla::ipc::IPCResult RecvNewCompositable(
      const CompositableHandle& aHandle, const TextureInfo& aInfo) override;
  mozilla::ipc::IPCResult RecvReleaseCompositable(
      const CompositableHandle& aHandle) override;

  mozilla::ipc::IPCResult RecvShutdown() override;
  mozilla::ipc::IPCResult RecvShutdownSync() override;
  mozilla::ipc::IPCResult RecvDeleteCompositorAnimations(
      nsTArray<uint64_t>&& aIds) override;
  mozilla::ipc::IPCResult RecvUpdateResources(
      const wr::IdNamespace& aIdNamespace,
      nsTArray<OpUpdateResource>&& aUpdates,
      nsTArray<RefCountedShmem>&& aSmallShmems,
      nsTArray<ipc::Shmem>&& aLargeShmems) override;
  mozilla::ipc::IPCResult RecvSetDisplayList(
      DisplayListData&& aDisplayList, nsTArray<OpDestroy>&& aToDestroy,
      const uint64_t& aFwdTransactionId, const TransactionId& aTransactionId,
      const bool& aContainsSVGGroup, const VsyncId& aVsyncId,
      const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
      const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
      const TimeStamp& aFwdTime, nsTArray<CompositionPayload>&& aPayloads,
      const bool& aRenderOffscreen) override;
  mozilla::ipc::IPCResult RecvEmptyTransaction(
      const FocusTarget& aFocusTarget,
      Maybe<TransactionData>&& aTransactionData,
      nsTArray<OpDestroy>&& aToDestroy, const uint64_t& aFwdTransactionId,
      const TransactionId& aTransactionId, const VsyncId& aVsyncId,
      const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
      const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
      const TimeStamp& aFwdTime,
      nsTArray<CompositionPayload>&& aPayloads) override;
  mozilla::ipc::IPCResult RecvSetFocusTarget(
      const FocusTarget& aFocusTarget) override;
  mozilla::ipc::IPCResult RecvParentCommands(
      const wr::IdNamespace& aIdNamespace,
      nsTArray<WebRenderParentCommand>&& commands) override;
  mozilla::ipc::IPCResult RecvGetSnapshot(NotNull<PTextureParent*> aTexture,
                                          bool* aNeedsYFlip) override;

  mozilla::ipc::IPCResult RecvClearCachedResources() override;
  mozilla::ipc::IPCResult RecvClearAnimationResources() override;
  mozilla::ipc::IPCResult RecvInvalidateRenderedFrame() override;
  mozilla::ipc::IPCResult RecvScheduleComposite(
      const wr::RenderReasons& aReasons) override;
  mozilla::ipc::IPCResult RecvCapture() override;
  mozilla::ipc::IPCResult RecvStartCaptureSequence(
      const nsACString& path, const uint32_t& aFlags) override;
  mozilla::ipc::IPCResult RecvStopCaptureSequence() override;
  mozilla::ipc::IPCResult RecvSyncWithCompositor() override;

  mozilla::ipc::IPCResult RecvSetConfirmedTargetAPZC(
      const uint64_t& aBlockId,
      nsTArray<ScrollableLayerGuid>&& aTargets) override;

  mozilla::ipc::IPCResult RecvSetTestSampleTime(
      const TimeStamp& aTime) override;
  mozilla::ipc::IPCResult RecvLeaveTestMode() override;
  mozilla::ipc::IPCResult RecvGetAnimationValue(
      const uint64_t& aCompositorAnimationsId, OMTAValue* aValue) override;
  mozilla::ipc::IPCResult RecvSetAsyncScrollOffset(
      const ScrollableLayerGuid::ViewID& aScrollId, const float& aX,
      const float& aY) override;
  mozilla::ipc::IPCResult RecvSetAsyncZoom(
      const ScrollableLayerGuid::ViewID& aScrollId,
      const float& aZoom) override;
  mozilla::ipc::IPCResult RecvFlushApzRepaints() override;
  mozilla::ipc::IPCResult RecvGetAPZTestData(APZTestData* data) override;
  mozilla::ipc::IPCResult RecvGetFrameUniformity(
      FrameUniformityData* aOutData) override;

  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvSetDefaultClearColor(
      const uint32_t& aColor) override;
  void SetClearColor(const gfx::DeviceColor& aColor);

  void Pause();
  bool Resume();

  void Destroy();

  // CompositorVsyncSchedulerOwner
  bool IsPendingComposite() override { return false; }
  void FinishPendingComposite() override {}
  void CompositeToTarget(VsyncId aId, wr::RenderReasons aReasons,
                         gfx::DrawTarget* aTarget,
                         const gfx::IntRect* aRect = nullptr) override;
  TimeDuration GetVsyncInterval() const override;

  // CompositableParentManager
  bool IsSameProcess() const override;
  base::ProcessId GetChildProcessId() override;
  dom::ContentParentId GetContentId() override;
  void NotifyNotUsed(PTextureParent* aTexture,
                     uint64_t aTransactionId) override;
  void SendAsyncMessage(
      const nsTArray<AsyncParentMessageData>& aMessage) override;
  void SendPendingAsyncMessages() override;
  void SetAboutToSendAsyncMessages() override;

  void HoldPendingTransactionId(
      const wr::Epoch& aWrEpoch, TransactionId aTransactionId,
      bool aContainsSVGGroup, const VsyncId& aVsyncId,
      const TimeStamp& aVsyncStartTime, const TimeStamp& aRefreshStartTime,
      const TimeStamp& aTxnStartTime, const nsACString& aTxnURL,
      const TimeStamp& aFwdTime, const bool aIsFirstPaint,
      nsTArray<CompositionPayload>&& aPayloads,
      const bool aUseForTelemetry = true);
  TransactionId LastPendingTransactionId();
  void FlushTransactionIdsForEpoch(
      const wr::Epoch& aEpoch, const VsyncId& aCompositeStartId,
      const TimeStamp& aCompositeStartTime, const TimeStamp& aRenderStartTime,
      const TimeStamp& aEndTime, UiCompositorControllerParent* aUiController,
      wr::RendererStats* aStats, nsTArray<FrameStats>& aOutputStats,
      nsTArray<TransactionId>& aOutputTransactions);
  void NotifySceneBuiltForEpoch(const wr::Epoch& aEpoch,
                                const TimeStamp& aEndTime);

  void RetrySkippedComposite();

  TextureFactoryIdentifier GetTextureFactoryIdentifier();

  void ExtractImageCompositeNotifications(
      nsTArray<ImageCompositeNotificationInfo>* aNotifications);

  wr::Epoch GetCurrentEpoch() const { return mWrEpoch; }
  wr::IdNamespace GetIdNamespace() { return mIdNamespace; }

  bool MatchesNamespace(const wr::ImageKey& aImageKey) const {
    return aImageKey.mNamespace == mIdNamespace;
  }

  bool MatchesNamespace(const wr::BlobImageKey& aBlobKey) const {
    return MatchesNamespace(wr::AsImageKey(aBlobKey));
  }

  bool MatchesNamespace(const wr::SnapshotImageKey& aSnapshotKey) const {
    return MatchesNamespace(wr::AsImageKey(aSnapshotKey));
  }

  bool MatchesNamespace(const wr::FontKey& aFontKey) const {
    return aFontKey.mNamespace == mIdNamespace;
  }

  bool MatchesNamespace(const wr::FontInstanceKey& aFontKey) const {
    return aFontKey.mNamespace == mIdNamespace;
  }

  void FlushRendering(wr::RenderReasons aReasons, bool aBlocking);

  /**
   * Schedule generating WebRender frame definitely at next composite timing.
   *
   * WebRenderBridgeParent uses composite timing to check if there is an update
   * to AsyncImagePipelines. If there is no update, WebRenderBridgeParent skips
   * to generate frame. If we need to generate new frame at next composite
   * timing, call this method.
   *
   * Call CompositorVsyncScheduler::ScheduleComposition() directly, if we just
   * want to trigger AsyncImagePipelines update checks.
   */
  void ScheduleGenerateFrame(wr::RenderReasons aReason);

  /**
   * Invalidate rendered frame.
   *
   * WebRender could skip frame rendering if there is no update.
   * This function is used to force invalidating even when there is no update.
   */
  void InvalidateRenderedFrame(wr::RenderReasons aReasons);

  /**
   * Schedule forced frame rendering at next composite timing.
   *
   * WebRender could skip frame rendering if there is no update.
   * This function is used to force rendering even when there is no update.
   */
  void ScheduleForcedGenerateFrame(wr::RenderReasons aReasons);

  void ScheduleFrameAfterSceneBuild(
      RefPtr<const wr::WebRenderPipelineInfo> aInfo);

  wr::Epoch UpdateWebRender(
      CompositorVsyncScheduler* aScheduler, RefPtr<wr::WebRenderAPI>&& aApi,
      AsyncImagePipelineManager* aImageMgr,
      const TextureFactoryIdentifier& aTextureFactoryIdentifier);

  void RemoveEpochDataPriorTo(const wr::Epoch& aRenderedEpoch);

  bool IsRootWebRenderBridgeParent() const;
  LayersId GetLayersId() const;

  void BeginRecording(const TimeStamp& aRecordingStart);

#if defined(MOZ_WIDGET_ANDROID)
  /**
   * Request a screengrab for android
   */
  void RequestScreenPixels(UiCompositorControllerParent* aController);
  void MaybeCaptureScreenPixels();
#endif
  /**
   * Stop recording and the frames collected since the call to BeginRecording
   */
  RefPtr<wr::WebRenderAPI::EndRecordingPromise> EndRecording();

  void DisableNativeCompositor();
  void AddPendingScrollPayload(CompositionPayload& aPayload,
                               const VsyncId& aCompositeStartId);

  nsTArray<CompositionPayload> TakePendingScrollPayload(
      const VsyncId& aCompositeStartId);

  RefPtr<WebRenderBridgeParentRef> GetWebRenderBridgeParentRef();

  void FlushPendingWrTransactionEventsWithWait();

 private:
  class ScheduleSharedSurfaceRelease;

  WebRenderBridgeParent(const wr::PipelineId& aPipelineId, nsCString&& aError);
  virtual ~WebRenderBridgeParent();

  bool ProcessEmptyTransactionUpdates(TransactionData& aData,
                                      bool* aScheduleComposite);

  bool ProcessDisplayListData(DisplayListData& aDisplayList, wr::Epoch aWrEpoch,
                              const TimeStamp& aTxnStartTime,
                              bool aValidTransaction, bool aRenderOffscreen,
                              const VsyncId& aVsyncId);

  bool SetDisplayList(const LayoutDeviceRect& aRect, ipc::ByteBuf&& aDLItems,
                      ipc::ByteBuf&& aDLCache, ipc::ByteBuf&& aSpatialTreeDL,
                      const wr::BuiltDisplayListDescriptor& aDLDesc,
                      const nsTArray<OpUpdateResource>& aResourceUpdates,
                      const nsTArray<RefCountedShmem>& aSmallShmems,
                      const nsTArray<ipc::Shmem>& aLargeShmems,
                      const TimeStamp& aTxnStartTime,
                      wr::TransactionBuilder& aTxn, wr::Epoch aWrEpoch);

  void UpdateAPZFocusState(const FocusTarget& aFocus);
  void UpdateAPZScrollData(const wr::Epoch& aEpoch,
                           WebRenderScrollData&& aData);
  void UpdateAPZScrollOffsets(ScrollUpdatesMap&& aUpdates,
                              uint32_t aPaintSequenceNumber);

  bool UpdateResources(const nsTArray<OpUpdateResource>& aResourceUpdates,
                       const nsTArray<RefCountedShmem>& aSmallShmems,
                       const nsTArray<ipc::Shmem>& aLargeShmems,
                       wr::TransactionBuilder& aUpdates);
  bool AddSharedExternalImage(wr::ExternalImageId aExtId, wr::ImageKey aKey,
                              wr::TransactionBuilder& aResources);
  bool UpdateSharedExternalImage(
      wr::ExternalImageId aExtId, wr::ImageKey aKey,
      const ImageIntRect& aDirtyRect, wr::TransactionBuilder& aResources,
      UniquePtr<ScheduleSharedSurfaceRelease>& aScheduleRelease);
  void ObserveSharedSurfaceRelease(
      const nsTArray<wr::ExternalImageKeyPair>& aPairs,
      const bool& aFromCheckpoint);

  bool PushExternalImageForTexture(wr::ExternalImageId aExtId,
                                   wr::ImageKey aKey, TextureHost* aTexture,
                                   bool aIsUpdate,
                                   wr::TransactionBuilder& aResources);

  void AddPipelineIdForCompositable(const wr::PipelineId& aPipelineIds,
                                    const CompositableHandle& aHandle,
                                    const CompositableHandleOwner& aOwner,
                                    wr::TransactionBuilder& aTxn,
                                    wr::TransactionBuilder& aTxnForImageBridge);
  void RemovePipelineIdForCompositable(const wr::PipelineId& aPipelineId,
                                       AsyncImagePipelineOps* aPendingOps,
                                       wr::TransactionBuilder& aTxn);

  void DeleteImage(const wr::ImageKey& aKey, wr::TransactionBuilder& aUpdates);
  void ReleaseTextureOfImage(const wr::ImageKey& aKey);

  bool ProcessWebRenderParentCommands(
      const nsTArray<WebRenderParentCommand>& aCommands,
      wr::TransactionBuilder& aTxn);

  void ClearResources();
  void ClearAnimationResources();
  mozilla::ipc::IPCResult HandleShutdown();

  void MaybeNotifyOfLayers(wr::TransactionBuilder&, bool aWillHaveLayers);

  void ResetPreviousSampleTime();

  void SetOMTASampleTime();
  RefPtr<OMTASampler> GetOMTASampler() const;

  CompositorBridgeParent* GetRootCompositorBridgeParent() const;

  RefPtr<WebRenderBridgeParent> GetRootWebRenderBridgeParent() const;

  // Tell APZ what the subsequent sampling's timestamp should be.
  void SetAPZSampleTime();

  wr::Epoch GetNextWrEpoch();
  // This function is expected to be used when GetNextWrEpoch() is called,
  // but TransactionBuilder does not have resource updates nor display list.
  // In this case, ScheduleGenerateFrame is not triggered via SceneBuilder.
  // Then we want to rollback WrEpoch. See Bug 1490117.
  void RollbackWrEpoch();

  void FlushSceneBuilds();
  void FlushFrameGeneration(wr::RenderReasons aReasons);
  void FlushFramePresentation();

  void MaybeGenerateFrame(VsyncId aId, bool aForceGenerateFrame,
                          wr::RenderReasons aReasons);

  VsyncId GetVsyncIdForEpoch(const wr::Epoch& aEpoch) {
    for (auto& id : mPendingTransactionIds) {
      if (id.mEpoch.mHandle == aEpoch.mHandle) {
        return id.mVsyncId;
      }
    }
    return VsyncId();
  }

 private:
  struct PendingTransactionId {
    PendingTransactionId(const wr::Epoch& aEpoch, TransactionId aId,
                         bool aContainsSVGGroup, const VsyncId& aVsyncId,
                         const TimeStamp& aVsyncStartTime,
                         const TimeStamp& aRefreshStartTime,
                         const TimeStamp& aTxnStartTime,
                         const nsACString& aTxnURL, const TimeStamp& aFwdTime,
                         const bool aIsFirstPaint, const bool aUseForTelemetry,
                         nsTArray<CompositionPayload>&& aPayloads)
        : mEpoch(aEpoch),
          mId(aId),
          mVsyncId(aVsyncId),
          mVsyncStartTime(aVsyncStartTime),
          mRefreshStartTime(aRefreshStartTime),
          mTxnStartTime(aTxnStartTime),
          mTxnURL(aTxnURL),
          mFwdTime(aFwdTime),
          mSkippedComposites(0),
          mContainsSVGGroup(aContainsSVGGroup),
          mIsFirstPaint(aIsFirstPaint),
          mUseForTelemetry(aUseForTelemetry),
          mPayloads(std::move(aPayloads)) {}
    wr::Epoch mEpoch;
    TransactionId mId;
    VsyncId mVsyncId;
    TimeStamp mVsyncStartTime;
    TimeStamp mRefreshStartTime;
    TimeStamp mTxnStartTime;
    nsCString mTxnURL;
    TimeStamp mFwdTime;
    TimeStamp mSceneBuiltTime;
    uint32_t mSkippedComposites;
    bool mContainsSVGGroup;
    bool mIsFirstPaint;
    bool mUseForTelemetry;
    nsTArray<CompositionPayload> mPayloads;
  };

  CompositorBridgeParentBase* MOZ_NON_OWNING_REF mCompositorBridge;
  wr::PipelineId mPipelineId;
  RefPtr<widget::CompositorWidget> mWidget;
  RefPtr<wr::WebRenderAPI> mApi;
  RefPtr<AsyncImagePipelineManager> mAsyncImageManager;
  RefPtr<CompositorVsyncScheduler> mCompositorScheduler;
  // mActiveAnimations is used to avoid leaking animations when
  // WebRenderBridgeParent is destroyed abnormally and Tab move between
  // different windows.
  std::unordered_map<uint64_t, wr::Epoch> mActiveAnimations;
  std::unordered_map<uint64_t, RefPtr<WebRenderImageHost>> mAsyncCompositables;
  std::unordered_map<uint64_t, CompositableTextureHostRef> mTextureHosts;
  std::unordered_map<uint64_t, wr::ExternalImageId> mSharedSurfaceIds;

  TimeDuration mVsyncRate;
  TimeStamp mPreviousFrameTimeStamp;

  std::deque<PendingTransactionId> mPendingTransactionIds;
  std::queue<CompositorAnimationIdsForEpoch> mCompositorAnimationsToDelete;
  wr::Epoch mWrEpoch;
  wr::IdNamespace mIdNamespace;
  CompositionOpportunityId mCompositionOpportunityId;
  nsCString mInitError;

  TimeStamp mMostRecentComposite;

  RefPtr<WebRenderBridgeParentRef> mWebRenderBridgeRef;

#if defined(MOZ_WIDGET_ANDROID)
  UiCompositorControllerParent* mScreenPixelsTarget;
#endif
  uint32_t mBoolParameterBits;
  uint16_t mBlobTileSize;
  wr::RenderReasons mSkippedCompositeReasons;
  bool mDestroyed;
  bool mIsFirstPaint;
  bool mLastNotifiedHasLayers = false;
  bool mReceivedDisplayList = false;
  bool mSkippedComposite = false;
  bool mDisablingNativeCompositor = false;
  // These payloads are being used for SCROLL_PRESENT_LATENCY telemetry
  DataMutex<nsClassHashtable<nsUint64HashKey, nsTArray<CompositionPayload>>>
      mPendingScrollPayloads;

  RefPtr<RemoteTextureTxnScheduler> mRemoteTextureTxnScheduler;
};

// Use this class, since WebRenderBridgeParent could not supports
// ThreadSafeWeakPtr.
// This class provides a ref of WebRenderBridgeParent when
// the WebRenderBridgeParent is not destroyed. Then it works similar to
// weak pointer.
class WebRenderBridgeParentRef final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebRenderBridgeParentRef)

  explicit WebRenderBridgeParentRef(WebRenderBridgeParent* aWebRenderBridge);

  RefPtr<WebRenderBridgeParent> WrBridge();
  void Clear();

 protected:
  ~WebRenderBridgeParentRef();

  RefPtr<WebRenderBridgeParent> mWebRenderBridge;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_WebRenderBridgeParent_h
