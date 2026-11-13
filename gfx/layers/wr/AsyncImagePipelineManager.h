/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H
#define MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H

#include <vector>

#include "CompositableHost.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/Maybe.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsClassHashtable.h"

namespace mozilla {

namespace wr {
class DisplayListBuilder;
class RenderTextureHostUsageInfo;
class WebRenderAPI;
class WebRenderPipelineInfo;
}  // namespace wr

namespace layers {

class CompositableHost;
class CompositorVsyncScheduler;
class Fence;
class WebRenderImageHost;
class WebRenderTextureHost;

class AsyncImagePipelineManager final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AsyncImagePipelineManager)

  explicit AsyncImagePipelineManager(RefPtr<wr::WebRenderAPI>&& aApi,
                                     bool aUseCompositorWnd);

 protected:
  ~AsyncImagePipelineManager();

 public:
  void Destroy();

  bool UseCompositorWnd() const { return mUseCompositorWnd; }

  void AddPipeline(const wr::PipelineId& aPipelineId,
                   WebRenderBridgeParent* aWrBridge);
  void RemovePipeline(const wr::PipelineId& aPipelineId,
                      const wr::Epoch& aEpoch);
  WebRenderBridgeParent* GetWrBridge(const wr::PipelineId& aPipelineId);

  void HoldExternalImage(const wr::PipelineId& aPipelineId,
                         const wr::Epoch& aEpoch, TextureHost* aTexture);
  void HoldExternalImage(const wr::PipelineId& aPipelineId,
                         const wr::Epoch& aEpoch,
                         const wr::ExternalImageId& aImageId);

  // This is called from the Renderer thread to notify this class about the
  // pipelines in the most recently completed update.
  // @param aInfo PipelineInfo for the update
  // @param aLatestFrameId RenderedFrameId if a frame has been submitted for
  //                       rendering, invalid if not
  // @param aLastCompletedFrameId RenderedFrameId for the last completed frame
  void NotifyPipelinesUpdated(RefPtr<const wr::WebRenderPipelineInfo> aInfo,
                              wr::RenderedFrameId aLatestFrameId,
                              wr::RenderedFrameId aLastCompletedFrameId,
                              RefPtr<Fence>&& aFence);

  // This is run on the compositor thread to process mRenderSubmittedUpdates. We
  // make this public because we need to invoke it from other places.
  void ProcessPipelineUpdates();

  TimeStamp GetCompositionTime() const { return mCompositionTime; }
  CompositionOpportunityId GetCompositionOpportunityId() const {
    return mCompositionOpportunityId;
  }

  void SetCompositionInfo(TimeStamp aTimeStamp,
                          CompositionOpportunityId aCompositionOpportunityId) {
    mCompositionTime = aTimeStamp;
    mCompositionOpportunityId = aCompositionOpportunityId;
    if (!mCompositionTime.IsNull() && !mCompositeUntilTime.IsNull() &&
        mCompositionTime >= mCompositeUntilTime) {
      mCompositeUntilTime = TimeStamp();
    }
  }
  void CompositeUntil(TimeStamp aTimeStamp) {
    if (mCompositeUntilTime.IsNull() || mCompositeUntilTime < aTimeStamp) {
      mCompositeUntilTime = aTimeStamp;
    }
  }
  TimeStamp GetCompositeUntilTime() const { return mCompositeUntilTime; }

  void AddAsyncImagePipeline(const wr::PipelineId& aPipelineId,
                             WebRenderImageHost* aImageHost);
  // Whether aPipelineId is currently registered by a live pipeline, which may
  // be another bridge's root pipeline (registered via AddPipeline) as well as
  // an async image pipeline. A holder that is merely waiting to be destroyed
  // does not count: its id may legitimately be re-registered while the old
  // holder drains, e.g. when a tab moves between windows.
  bool HasLivePipeline(const wr::PipelineId& aPipelineId) const {
    auto* holder = mPipelineTexturesHolders.Get(wr::AsUint64(aPipelineId));
    return holder && holder->mDestroyedEpoch.isNothing();
  }
  void RemoveAsyncImagePipeline(const wr::PipelineId& aPipelineId,
                                AsyncImagePipelineOps* aPendingOps,
                                wr::TransactionBuilder& aTxn);

  void UpdateAsyncImagePipeline(const wr::PipelineId& aPipelineId,
                                const LayoutDeviceRect& aScBounds,
                                wr::WrRotation aRotation,
                                const wr::ImageRendering& aFilter,
                                const wr::MixBlendMode& aMixBlendMode);
  void ApplyAsyncImagesOfImageBridge(wr::TransactionBuilder& aSceneBuilderTxn,
                                     wr::TransactionBuilder& aFastTxn);
  void ApplyAsyncImageForPipeline(
      const wr::PipelineId& aPipelineId, wr::TransactionBuilder& aTxn,
      wr::TransactionBuilder& aTxnForImageBridge,
      AsyncImagePipelineOps* aPendingOps,
      RemoteTextureInfoList* aPendingRemoteTextures);

  void ApplyAsyncImageForPipeline(const wr::PipelineId& aPipelineId,
                                  TextureHost* aTexture,
                                  wr::TransactionBuilder& aTxn);

  void SetEmptyDisplayList(const wr::PipelineId& aPipelineId,
                           wr::TransactionBuilder& aTxn,
                           wr::TransactionBuilder& aTxnForImageBridge);

  void AppendImageCompositeNotification(
      const ImageCompositeNotificationInfo& aNotification) {
    mImageCompositeNotifications.AppendElement(aNotification);
  }

  void FlushImageNotifications(
      nsTArray<ImageCompositeNotificationInfo>* aNotifications) {
    aNotifications->AppendElements(std::move(mImageCompositeNotifications));
  }

  void SetWillGenerateFrame();
  bool GetAndResetWillGenerateFrame();

  static wr::ExternalImageId GetNextExternalImageId();

  void SetTextureFactoryIdentifier(
      const TextureFactoryIdentifier& aTextureFactoryIdentifier) {
    mTextureFactoryIdentifier = aTextureFactoryIdentifier;
  }

  TextureFactoryIdentifier GetTextureFactoryIdentifier() const {
    return mTextureFactoryIdentifier;
  }

 private:
  void ProcessPipelineRendered(const wr::PipelineId& aPipelineId,
                               const wr::Epoch& aEpoch,
                               wr::RenderedFrameId aRenderedFrameId);
  void ProcessPipelineRemoved(const wr::RemovedPipeline& aRemovedPipeline,
                              wr::RenderedFrameId aRenderedFrameId);

  wr::Epoch GetNextImageEpoch();
  uint32_t GetNextResourceId() { return ++mResourceId; }
  wr::IdNamespace GetNamespace() { return mIdNamespace; }
  wr::ImageKey GenerateImageKey() {
    wr::ImageKey key;
    key.mNamespace = GetNamespace();
    key.mHandle = GetNextResourceId();
    return key;
  }

  struct ForwardingTextureHost {
    ForwardingTextureHost(const wr::Epoch& aEpoch, TextureHost* aTexture)
        : mEpoch(aEpoch), mTexture(aTexture) {}
    wr::Epoch mEpoch;
    CompositableTextureHostRef mTexture;
  };

  struct ForwardingExternalImage {
    ForwardingExternalImage(const wr::Epoch& aEpoch,
                            const wr::ExternalImageId& aImageId)
        : mEpoch(aEpoch), mImageId(aImageId) {}
    ~ForwardingExternalImage();
    wr::Epoch mEpoch;
    wr::ExternalImageId mImageId;
  };

  struct PipelineTexturesHolder {
    // Holds forwarding WebRenderTextureHosts.
    std::vector<ForwardingTextureHost> mTextureHostsUntilRenderSubmitted;
    // TextureHosts that must be held until rendering has completed. UniquePtr
    // is used to make the entries movable, ideally ForwardingTextureHost would
    // be fully movable.
    std::vector<UniquePtr<ForwardingTextureHost>>
        mTextureHostsUntilRenderCompleted;
    std::vector<UniquePtr<ForwardingExternalImage>> mExternalImages;
    Maybe<wr::Epoch> mDestroyedEpoch;
    WebRenderBridgeParent* MOZ_NON_OWNING_REF mWrBridge = nullptr;
  };

  struct AsyncImagePipeline {
    AsyncImagePipeline(wr::PipelineId aPipelineId,
                       layers::WebRenderBackend aBackend,
                       WebRenderImageHost* aImageHost);
    void Update(const LayoutDeviceRect& aScBounds, wr::WrRotation aRotation,
                const wr::ImageRendering& aFilter,
                const wr::MixBlendMode& aMixBlendMode) {
      mIsChanged |= !mScBounds.IsEqualEdges(aScBounds) ||
                    mRotation != aRotation || mFilter != aFilter ||
                    mMixBlendMode != aMixBlendMode;
      mScBounds = aScBounds;
      mRotation = aRotation;
      mFilter = aFilter;
      mMixBlendMode = aMixBlendMode;
    }

    bool mInitialised;
    bool mIsChanged;
    bool mUseExternalImage;
    LayoutDeviceRect mScBounds;
    wr::WrRotation mRotation;
    wr::ImageRendering mFilter;
    wr::MixBlendMode mMixBlendMode;
    const RefPtr<WebRenderImageHost> mImageHost;
    CompositableTextureHostRef mCurrentTexture;
    nsTArray<wr::ImageKey> mKeys;
    wr::DisplayListBuilder mDLBuilder;
    bool mVideoOverlayDisabled = false;
  };

  void ApplyAsyncImageForPipeline(const wr::Epoch& aEpoch,
                                  const wr::PipelineId& aPipelineId,
                                  AsyncImagePipeline* aPipeline,
                                  TextureHost* aTexture,
                                  wr::TransactionBuilder& aSceneBuilderTxn,
                                  wr::TransactionBuilder& aMaybeFastTxn);
  Maybe<TextureHost::ResourceUpdateOp> UpdateImageKeys(
      const wr::Epoch& aEpoch, const wr::PipelineId& aPipelineId,
      AsyncImagePipeline* aPipeline, TextureHost* aTexture,
      nsTArray<wr::ImageKey>& aKeys, wr::TransactionBuilder& aSceneBuilderTxn,
      wr::TransactionBuilder& aMaybeFastTxn);
  Maybe<TextureHost::ResourceUpdateOp> UpdateWithoutExternalImage(
      TextureHost* aTexture, wr::ImageKey aKey, TextureHost::ResourceUpdateOp,
      wr::TransactionBuilder& aTxn);

  void CheckForTextureHostsNotUsedByGPU();

  RefPtr<wr::WebRenderAPI> mApi;
  bool mUseCompositorWnd;
  TextureFactoryIdentifier mTextureFactoryIdentifier;

  const wr::IdNamespace mIdNamespace;
  const bool mUseTripleBuffering;
  uint32_t mResourceId;

  nsClassHashtable<nsUint64HashKey, PipelineTexturesHolder>
      mPipelineTexturesHolders;
  nsClassHashtable<nsUint64HashKey, AsyncImagePipeline> mAsyncImagePipelines;
  wr::Epoch mAsyncImageEpoch;
  bool mWillGenerateFrame;
  bool mDestroyed;

#ifdef XP_WIN
  bool mUseWebRenderDCompVideoHwOverlayWin;
  bool mUseWebRenderDCompVideoSwOverlayWin;
  bool mUseWebRenderDCompositionTextureOverlayWin;
#endif

  // Render time for the current composition.
  TimeStamp mCompositionTime;

  // CompositionOpportunityId of the current composition.
  CompositionOpportunityId mCompositionOpportunityId;

  // When nonnull, during rendering, some compositable indicated that it will
  // change its rendering at this time. In order not to miss it, we composite
  // on every vsync until this time occurs (this is the latest such time).
  TimeStamp mCompositeUntilTime;

  nsTArray<ImageCompositeNotificationInfo> mImageCompositeNotifications;

  struct WebRenderPipelineInfoHolder {
    WebRenderPipelineInfoHolder(RefPtr<const wr::WebRenderPipelineInfo>&& aInfo,
                                RefPtr<Fence>&& aFence);
    ~WebRenderPipelineInfoHolder();
    WebRenderPipelineInfoHolder(WebRenderPipelineInfoHolder&&) = default;
    RefPtr<const wr::WebRenderPipelineInfo> mInfo;
    RefPtr<Fence> mFence;
  };

  std::vector<std::pair<wr::RenderedFrameId, WebRenderPipelineInfoHolder>>
      mRenderSubmittedUpdates;
  Mutex mRenderSubmittedUpdatesLock MOZ_UNANNOTATED;

  Atomic<uint64_t> mLastCompletedFrameId;
  std::vector<std::pair<wr::RenderedFrameId,
                        std::vector<UniquePtr<ForwardingTextureHost>>>>
      mTexturesInUseByGPU;
  RefPtr<Fence> mReadFence;
};

}  // namespace layers
}  // namespace mozilla

#endif /* MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H */
