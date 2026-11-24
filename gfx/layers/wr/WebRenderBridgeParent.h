/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_WebRenderBridgeParent_h
#define mozilla_layers_WebRenderBridgeParent_h

#include <unordered_map>

#include "CompositableHost.h"  // for CompositableHost, ImageCompositeNotificationInfo
#include "GLContextProvider.h"
#include "WindowRenderer.h"
#include "mozilla/DataMutex.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/CompositableTransactionParent.h"
#include "mozilla/layers/CompositorVsyncSchedulerOwner.h"
#include "mozilla/layers/PWebRenderBridgeParent.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsTArrayForwardDeclare.h"

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
class CompositorBridgeParent;
class CompositorBridgeParentBase;
class CompositorVsyncScheduler;
class ContentCompositorBridgeParent;
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
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebRenderBridgeParent, final);

  // Constructor for root WebRenderBridgeParents.
  WebRenderBridgeParent(CompositorBridgeParent* aCompositorBridge,
                        const wr::PipelineId& aPipelineId,
                        widget::CompositorWidget* aWidget,
                        TimeDuration aVsyncRate);

  // Constructor for content WebRenderBridgeParents.
  WebRenderBridgeParent(ContentCompositorBridgeParent* aCompositorBridge,
                        const wr::PipelineId& aPipelineId,
                        CompositorVsyncScheduler* aScheduler,
                        RefPtr<wr::WebRenderAPI>&& aApi,
                        RefPtr<AsyncImagePipelineManager>&& aImageMgr,
                        TimeDuration aVsyncRate);

  WebRenderBridgeParent(const wr::PipelineId& aPipelineId, nsCString&& aError);

  static already_AddRefed<WebRenderBridgeParent> CreateDestroyed(
      const wr::PipelineId& aPipelineId, nsCString&& aError);

  // Ensures the WebRenderBridgeParent has completed initialization, returning
  // true if successful, or false if initialization failed or the bridge is
  // alreay destroyed. Must only be called from Compositor thread.
  bool EnsureInitialized();

  // Called for root WebRenderBridgeParents to complete initialization once the
  // WebRenderAPI and AsyncImagePipelineManager have been created.
  void FinishInitialization(RefPtr<wr::WebRenderAPI>&& aApi,
                            RefPtr<AsyncImagePipelineManager>&& aImageMgr);
  void FinishInitializationError(nsCString&& aError);

  wr::PipelineId PipelineId() { return mPipelineId; }
  // Must only be called from Compositor thread.
  already_AddRefed<wr::WebRenderAPI> GetWebRenderAPI();
  // Must only be called from Compositor thread.
  AsyncImagePipelineManager* AsyncImageManager();
  // Must only be called from Compositor thread.
  CompositorVsyncScheduler* CompositorScheduler();
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
  mozilla::ipc::IPCResult RecvInvalidateRenderedFrame() override;
  mozilla::ipc::IPCResult RecvScheduleComposite(
      const wr::RenderReasons& aReasons) override;
  mozilla::ipc::IPCResult RecvCapture() override;
  mozilla::ipc::IPCResult RecvStartCaptureSequence(
      const uint32_t& aFlags) override;
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
  mozilla::ipc::IPCResult RecvEndWheelTransaction(
      PWebRenderBridgeParent::EndWheelTransactionResolver&& aResolve) override;

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
  GeckoChildID GetChildID() override;
  dom::ContentParentId GetContentId() override;
  void NotifyNotUsed(PTextureParent* aTexture,
                     uint64_t aTransactionId) override;
  void SendAsyncMessage(Span<const AsyncParentMessageData>) override;
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
  // Returns Nothing if we were destroyed, or if we are a root bridge which has
  // not finished initializing. Content bridges are always initialized when
  // created.
  Maybe<TextureFactoryIdentifier> MaybeGetTextureFactoryIdentifier();

  void ExtractImageCompositeNotifications(
      nsTArray<ImageCompositeNotificationInfo>* aNotifications);

  wr::Epoch GetCurrentEpoch() const { return mWrEpoch; }

  bool MatchesNamespace(const wr::ImageKey& aImageKey) const {
    return aImageKey.mNamespace == mLateInit->mIdNamespace;
  }

  bool MatchesNamespace(const wr::BlobImageKey& aBlobKey) const {
    return MatchesNamespace(wr::AsImageKey(aBlobKey));
  }

  bool MatchesNamespace(const wr::SnapshotImageKey& aSnapshotKey) const {
    return MatchesNamespace(wr::AsImageKey(aSnapshotKey));
  }

  bool MatchesNamespace(const wr::FontKey& aFontKey) const {
    return aFontKey.mNamespace == mLateInit->mIdNamespace;
  }

  bool MatchesNamespace(const wr::FontInstanceKey& aFontKey) const {
    return aFontKey.mNamespace == mLateInit->mIdNamespace;
  }

  // Returns the bridge whose namespace is aNamespace, if it is this bridge or
  // another bridge of the same content process and the same WebRender window.
  // Otherwise returns null. A snapshot image can only be adopted
  // (OpAdoptSnapshotImage) from such a bridge.
  already_AddRefed<WebRenderBridgeParent> FindSnapshotSourceBridge(
      const wr::IdNamespace& aNamespace);

  bool OwnsExternalImageId(const wr::ExternalImageId& aId) const {
    return static_cast<uint32_t>(wr::AsUint64(aId) >> 32) ==
           mLateInit->mIdNamespace.mHandle;
  }

  /**
   * AnimationHelper::GetNextCompositorAnimationsId() encodes the child
   * process's GeckoChildID in the upper 32 bits of the id, verify that this is
   * as expected.
   */
  bool OwnsCompositorAnimationsId(uint64_t aId) const {
    return (aId >> 32) == (uint64_t)OtherChildID();
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

  // Sends this bridge's embed policy to WebRender, for when the set of layer
  // trees it embeds changes.
  void RefreshEmbedPolicy();
  LayersId GetLayersId() const;

  void BeginRecording(const TimeStamp& aRecordingStart);

#if defined(MOZ_WIDGET_ANDROID)
  using ScreenPixelsPromise = MozPromise<Ok, nsresult, true>;
  /**
   * Request a screengrab for android
   */
  RefPtr<ScreenPixelsPromise> RequestScreenPixels(
      gfx::IntRect aSourceRect,
      RefPtr<layers::AndroidHardwareBuffer> aHardwareBuffer);
#endif

  /**
   * Stop recording and the frames collected since the call to BeginRecording
   */
  RefPtr<wr::WebRenderAPI::EndRecordingPromise> EndRecording();

  void AddPendingScrollPayload(CompositionPayload& aPayload,
                               const VsyncId& aCompositeStartId);

  nsTArray<CompositionPayload> TakePendingScrollPayload(
      const VsyncId& aCompositeStartId);

  RefPtr<WebRenderBridgeParentRef> GetWebRenderBridgeParentRef();

  void FlushPendingWrTransactionEventsWithWait();

 private:
  class ScheduleSharedSurfaceRelease;

  virtual ~WebRenderBridgeParent();

  bool ProcessEmptyTransactionUpdates(TransactionData& aData,
                                      bool* aScheduleComposite);

  bool ProcessDisplayListData(DisplayListData& aDisplayList, wr::Epoch aWrEpoch,
                              const TimeStamp& aTxnStartTime,
                              bool aValidTransaction, bool aRenderOffscreen,
                              const VsyncId& aVsyncId);

  void SanitizeScrollData(WebRenderScrollData& aScrollData);

  // The namespaces of the pipelines this bridge's display lists may embed:
  // those of its process's async image pipelines and of the layer trees it
  // embeds. Nothing() if they may embed any pipeline.
  Maybe<nsTArray<uint32_t>> ComputeEmbedPolicy() const;

  bool SetDisplayList(const LayoutDeviceRect& aRect, ipc::ByteBuf&& aDLItems,
                      ipc::ByteBuf&& aSpatialTreeDL,
                      ipc::ByteBuf&& aInternerDelta,
                      const wr::BuiltDisplayListDescriptor& aDLDesc,
                      const nsTArray<OpUpdateResource>& aResourceUpdates,
                      const nsTArray<RefCountedShmem>& aSmallShmems,
                      const nsTArray<ipc::Shmem>& aLargeShmems,
                      const TimeStamp& aTxnStartTime,
                      wr::TransactionBuilder& aTxn, wr::Epoch aWrEpoch,
                      const VsyncId& aVsyncId, bool aRenderOffscreen);

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

#if defined(MOZ_WIDGET_ANDROID)
  // If screen pixels have been requested via RequestScreenPixels(), sends the
  // request to the renderer. Should be called immediately prior to generating
  // the frame for which we want the pixels to be captured.
  void MaybeCaptureScreenPixels();
#endif

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

  // Members that may be initialized after construction. Accessing via a `Maybe`
  // ensures a safe crash when attempting to access a member before it has been
  // initialized.
  struct LateInit {
    RefPtr<wr::WebRenderAPI> mApi;
    RefPtr<AsyncImagePipelineManager> mAsyncImageManager;
    RefPtr<CompositorVsyncScheduler> mCompositorScheduler;
    wr::IdNamespace mIdNamespace;
  };
  Maybe<LateInit> mLateInit;

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
  wr::Epoch mWrEpoch{0};
  CompositionOpportunityId mCompositionOpportunityId;
  nsCString mInitError;

  TimeStamp mMostRecentComposite;

  RefPtr<WebRenderBridgeParentRef> mWebRenderBridgeRef;

#if defined(MOZ_WIDGET_ANDROID)
  struct ScreenPixelsRequest {
    gfx::IntRect mSourceRect;
    RefPtr<layers::AndroidHardwareBuffer> mHardwareBuffer;
    RefPtr<ScreenPixelsPromise::Private> mPromise;
  };
  Maybe<ScreenPixelsRequest> mScreenPixelsRequest;
#endif

  uint32_t mBoolParameterBits = 0;
  uint16_t mBlobTileSize = 256;
  wr::RenderReasons mSkippedCompositeReasons = wr::RenderReasons::NONE;
  bool mDestroyed = false;
  bool mIsFirstPaint = false;
  bool mLastNotifiedHasLayers = false;
  bool mReceivedDisplayList = false;
  bool mSkippedComposite = false;
  // Whether this is a root (widget) WebRenderBridgeParent. Determined at
  // construction and stable for the object's lifetime. This must not be
  // derived from mWidget, because mWidget is cleared on a WebRender
  // initialization failure (see FinishInitializationError), which would
  // otherwise make a failed root bridge masquerade as a content bridge during
  // teardown.
  const bool mIsRootWebRenderBridgeParent;
  // These payloads are being used for SCROLL_PRESENT_LATENCY telemetry
  DataMutex<nsClassHashtable<nsUint64HashKey, nsTArray<CompositionPayload>>>
      mPendingScrollPayloads{"WebRenderBridgeParent::mPendingScrollPayloads"};

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
