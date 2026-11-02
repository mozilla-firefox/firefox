/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AsyncImagePipelineManager.h"

#include <algorithm>
#include <iterator>

#include "CompositableHost.h"
#include "gfxEnv.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/AsyncImagePipelineOp.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/layers/RemoteTextureHostWrapper.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/WebRenderImageHost.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {
namespace layers {

AsyncImagePipelineManager::ForwardingExternalImage::~ForwardingExternalImage() {
  DebugOnly<bool> released = SharedSurfacesParent::Release(mImageId);
  MOZ_ASSERT(released);
}

AsyncImagePipelineManager::AsyncImagePipeline::AsyncImagePipeline(
    wr::PipelineId aPipelineId, layers::WebRenderBackend aBackend,
    WebRenderImageHost* aImageHost)
    : mInitialised(false),
      mIsChanged(false),
      mUseExternalImage(false),
      mRotation(wr::WrRotation::Degree0),
      mFilter(wr::ImageRendering::Auto),
      mMixBlendMode(wr::MixBlendMode::Normal),
      mImageHost(aImageHost),
      mDLBuilder(aPipelineId, aBackend) {}

AsyncImagePipelineManager::AsyncImagePipelineManager(
    RefPtr<wr::WebRenderAPI>&& aApi, bool aUseCompositorWnd)
    : mApi(aApi),
      mUseCompositorWnd(aUseCompositorWnd),
      mIdNamespace(mApi->GetNamespace()),
      mUseTripleBuffering(mApi->GetCapabilities().mUseTripleBuffering),
      mResourceId(0),
      mAsyncImageEpoch{0},
      mWillGenerateFrame(false),
      mDestroyed(false),
#ifdef XP_WIN
      mUseWebRenderDCompVideoHwOverlayWin(
          gfx::gfxVars::UseWebRenderDCompVideoHwOverlayWin()),
      mUseWebRenderDCompVideoSwOverlayWin(
          gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin()),
      mUseWebRenderDCompositionTextureOverlayWin(
          gfx::gfxVars::UseWebRenderDCompositionTextureOverlayWin()),
#endif
      mRenderSubmittedUpdatesLock("SubmittedUpdatesLock"),
      mLastCompletedFrameId(0) {
  MOZ_COUNT_CTOR(AsyncImagePipelineManager);
}

AsyncImagePipelineManager::~AsyncImagePipelineManager() {
  MOZ_COUNT_DTOR(AsyncImagePipelineManager);
}

void AsyncImagePipelineManager::Destroy() {
  MOZ_ASSERT(!mDestroyed);
  mApi = nullptr;
  mPipelineTexturesHolders.Clear();
  mDestroyed = true;
}

/* static */
wr::ExternalImageId AsyncImagePipelineManager::GetNextExternalImageId() {
  static std::atomic<uint64_t> sCounter = 0;

  uint64_t id = ++sCounter;
  // Upper 32bit(namespace) needs to be 0.
  // Namespace other than 0 might be used by others.
  MOZ_RELEASE_ASSERT(id != UINT32_MAX);
  return wr::ToExternalImageId(id);
}

void AsyncImagePipelineManager::SetWillGenerateFrame() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  mWillGenerateFrame = true;
}

bool AsyncImagePipelineManager::GetAndResetWillGenerateFrame() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  bool ret = mWillGenerateFrame;
  mWillGenerateFrame = false;
  return ret;
}

void AsyncImagePipelineManager::AddPipeline(const wr::PipelineId& aPipelineId,
                                            WebRenderBridgeParent* aWrBridge) {
  if (mDestroyed) {
    return;
  }

  mPipelineTexturesHolders.WithEntryHandle(
      wr::AsUint64(aPipelineId), [&](auto&& holder) {
        if (holder) {
          // This could happen during tab move between different windows.
          // Previously removed holder could be still alive for waiting
          // destroyed.
          MOZ_ASSERT(holder.Data()->mDestroyedEpoch.isSome());
          holder.Data()->mDestroyedEpoch = Nothing();  // Revive holder
          holder.Data()->mWrBridge = aWrBridge;
          return;
        }

        holder.Insert(std::make_unique<PipelineTexturesHolder>())->mWrBridge =
            aWrBridge;
      });
}

void AsyncImagePipelineManager::RemovePipeline(
    const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch) {
  if (mDestroyed) {
    return;
  }

  PipelineTexturesHolder* holder =
      mPipelineTexturesHolders.Get(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(holder);
  if (!holder) {
    return;
  }
  holder->mWrBridge = nullptr;
  holder->mDestroyedEpoch = Some(aEpoch);
}

WebRenderBridgeParent* AsyncImagePipelineManager::GetWrBridge(
    const wr::PipelineId& aPipelineId) {
  if (mDestroyed) {
    return nullptr;
  }

  PipelineTexturesHolder* holder =
      mPipelineTexturesHolders.Get(wr::AsUint64(aPipelineId));
  if (!holder) {
    return nullptr;
  }
  if (holder->mWrBridge) {
    MOZ_ASSERT(holder->mDestroyedEpoch.isNothing());
    return holder->mWrBridge;
  }

  return nullptr;
}

void AsyncImagePipelineManager::AddAsyncImagePipeline(
    const wr::PipelineId& aPipelineId, WebRenderImageHost* aImageHost) {
  if (mDestroyed) {
    return;
  }
  MOZ_ASSERT(aImageHost);
  uint64_t id = wr::AsUint64(aPipelineId);

  MOZ_ASSERT(!mAsyncImagePipelines.Contains(id));
  auto holder = std::make_unique<AsyncImagePipeline>(
      aPipelineId, mApi->GetCapabilities().mBackendType, aImageHost);
  mAsyncImagePipelines.InsertOrUpdate(id, std::move(holder));
  AddPipeline(aPipelineId, /* aWrBridge */ nullptr);
}

void AsyncImagePipelineManager::RemoveAsyncImagePipeline(
    const wr::PipelineId& aPipelineId, AsyncImagePipelineOps* aPendingOps,
    wr::TransactionBuilder& aTxn) {
  if (mDestroyed) {
    return;
  }

  if (aPendingOps) {
    aPendingOps->mList.emplace(
        AsyncImagePipelineOp::RemoveAsyncImagePipeline(this, aPipelineId));
    return;
  }

  uint64_t id = wr::AsUint64(aPipelineId);
  if (auto entry = mAsyncImagePipelines.Lookup(id)) {
    const auto& holder = entry.Data();
    wr::Epoch epoch = GetNextImageEpoch();
    aTxn.ClearDisplayList(epoch, mIdNamespace, aPipelineId);
    for (wr::ImageKey key : holder->mKeys) {
      aTxn.DeleteImage(key);
    }
    entry.Remove();
    RemovePipeline(aPipelineId, epoch);
  }
}

void AsyncImagePipelineManager::UpdateAsyncImagePipeline(
    const wr::PipelineId& aPipelineId, const LayoutDeviceRect& aScBounds,
    const wr::WrRotation aRotation, const wr::ImageRendering& aFilter,
    const wr::MixBlendMode& aMixBlendMode) {
  if (mDestroyed) {
    return;
  }
  AsyncImagePipeline* pipeline =
      mAsyncImagePipelines.Get(wr::AsUint64(aPipelineId));
  if (!pipeline) {
    return;
  }
  pipeline->mInitialised = true;
  pipeline->Update(aScBounds, aRotation, aFilter, aMixBlendMode);
}

Maybe<TextureHost::ResourceUpdateOp> AsyncImagePipelineManager::UpdateImageKeys(
    const wr::Epoch& aEpoch, const wr::PipelineId& aPipelineId,
    AsyncImagePipeline* aPipeline, TextureHost* aTexture,
    nsTArray<wr::ImageKey>& aKeys, wr::TransactionBuilder& aSceneBuilderTxn,
    wr::TransactionBuilder& aMaybeFastTxn) {
  MOZ_ASSERT(aKeys.IsEmpty());
  MOZ_ASSERT(aPipeline);

  if (aTexture == aPipeline->mCurrentTexture.get()) {
    // The texture has not changed, just reuse previous ImageKeys.
    aKeys = aPipeline->mKeys.Clone();
    return Nothing();
  }

  auto* wrapper = aTexture ? aTexture->AsRemoteTextureHostWrapper() : nullptr;
  if (wrapper && !aPipeline->mImageHost->GetAsyncRef()) {
    RemoteTextureMap::Get()->GetRemoteTexture(wrapper);
  }

  if (!aTexture || aTexture->NumSubTextures() == 0) {
    // We don't have a new texture or texture does not have SubTextures, there
    // isn't much we can do.
    aKeys = aPipeline->mKeys.Clone();
    return Nothing();
  }

  RefPtr<TextureHost> previousTexture =
      std::move(aPipeline->mCurrentTexture.get());
  aPipeline->mCurrentTexture = aTexture;

  WebRenderTextureHost* wrTexture = aTexture->AsWebRenderTextureHost();
  MOZ_ASSERT(wrTexture);
  if (!wrTexture) {
    gfxCriticalNote << "WebRenderTextureHost is not used";
  }

  bool useExternalImage = !!wrTexture;
  aPipeline->mUseExternalImage = useExternalImage;

  // The non-external image code path falls back to converting the texture into
  // an rgb image.
  auto numKeys = useExternalImage ? aTexture->NumSubTextures() : 1;
  MOZ_ASSERT(numKeys > 0);

  // If we already had a texture and the format hasn't changed, better to reuse
  // the image keys than create new ones.
  auto backend = aSceneBuilderTxn.GetCapabilities().mBackendType;

  bool videoOverlayDisabled = false;
  RefPtr<wr::RenderTextureHostUsageInfo> usageInfo;
  // video overlay of DXGITextureHostD3D11 may be disabled dynamically
  const bool checkVideoOverlayDisabled = !!aTexture->AsDXGITextureHostD3D11();
  if (checkVideoOverlayDisabled) {
    auto externalImageKey = wrTexture->GetExternalImageKey();
    usageInfo = wr::RenderThread::Get()->GetOrMergeUsageInfo(
        externalImageKey,
        aPipeline->mImageHost->GetRenderTextureHostUsageInfo());
    if (usageInfo) {
      videoOverlayDisabled = usageInfo->VideoOverlayDisabled();
      aPipeline->mImageHost->SetRenderTextureHostUsageInfo(usageInfo);
    }
  }
  MOZ_ASSERT_IF(aPipeline->mVideoOverlayDisabled, videoOverlayDisabled);

  bool canUpdate =
      !!previousTexture &&
      previousTexture->GetTextureHostType() == aTexture->GetTextureHostType() &&
      previousTexture->GetSize() == aTexture->GetSize() &&
      previousTexture->GetFormat() == aTexture->GetFormat() &&
      previousTexture->GetColorDepth() == aTexture->GetColorDepth() &&
      previousTexture->NeedsYFlip() == aTexture->NeedsYFlip() &&
      previousTexture->SupportsExternalCompositing(backend) ==
          aTexture->SupportsExternalCompositing(backend) &&
      aPipeline->mKeys.Length() == numKeys &&
      aPipeline->mVideoOverlayDisabled == videoOverlayDisabled;

  if (videoOverlayDisabled) {
    MOZ_ASSERT(usageInfo);
    aPipeline->mVideoOverlayDisabled = true;
  }

  if (!canUpdate) {
    for (auto key : aPipeline->mKeys) {
      // Destroy ImageKeys on transaction of scene builder thread, since
      // DisplayList is updated on SceneBuilder thread. It prevents too early
      // ImageKey deletion.
      aSceneBuilderTxn.DeleteImage(key);
    }
    aPipeline->mKeys.Clear();
    for (uint32_t i = 0; i < numKeys; ++i) {
      aPipeline->mKeys.AppendElement(GenerateImageKey());
    }
  }

  aKeys = aPipeline->mKeys.Clone();

  auto op = canUpdate ? TextureHost::UPDATE_IMAGE : TextureHost::ADD_IMAGE;

  if (!useExternalImage) {
    return UpdateWithoutExternalImage(aTexture, aKeys[0], op, aMaybeFastTxn);
  }

  wrTexture->MaybeNotifyForUse(aMaybeFastTxn);

  Range<wr::ImageKey> keys(&aKeys[0], aKeys.Length());
  auto externalImageKey = wrTexture->GetExternalImageKey();
  wrTexture->PushResourceUpdates(aMaybeFastTxn, op, keys, externalImageKey);

  return Some(op);
}

Maybe<TextureHost::ResourceUpdateOp>
AsyncImagePipelineManager::UpdateWithoutExternalImage(
    TextureHost* aTexture, wr::ImageKey aKey, TextureHost::ResourceUpdateOp aOp,
    wr::TransactionBuilder& aTxn) {
  MOZ_ASSERT(aTexture);

  RefPtr<gfx::DataSourceSurface> dSurf = aTexture->GetAsSurface();
  if (!dSurf) {
    NS_ERROR("TextureHost does not return DataSourceSurface");
    return Nothing();
  }
  gfx::DataSourceSurface::MappedSurface map;
  if (!dSurf->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    NS_ERROR("DataSourceSurface failed to map");
    return Nothing();
  }

  gfx::IntSize size = dSurf->GetSize();
  auto format = wr::SurfaceFormatToImageFormat(dSurf->GetFormat());
  if (NS_WARN_IF(!format)) {
    dSurf->Unmap();
    return Nothing();
  }
  wr::ImageDescriptor descriptor(size, map.mStride, *format,
                                 wr::ToOpacityType(dSurf->GetFormat()));

  // Costly copy right here...
  wr::Vec<uint8_t> bytes;
  bytes.PushBytes(Range<uint8_t>(map.mData, size.height * map.mStride));

  if (aOp == TextureHost::UPDATE_IMAGE) {
    aTxn.UpdateImageBuffer(aKey, descriptor, bytes);
  } else {
    aTxn.AddImage(aKey, descriptor, bytes);
  }

  dSurf->Unmap();

  return Some(aOp);
}

void AsyncImagePipelineManager::ApplyAsyncImagesOfImageBridge(
    wr::TransactionBuilder& aSceneBuilderTxn,
    wr::TransactionBuilder& aFastTxn) {
  if (mDestroyed || mAsyncImagePipelines.Count() == 0) {
    return;
  }

#ifdef XP_WIN
  // UseWebRenderDCompVideoHwOverlayWin(), UseWebRenderDCompVideoSwOverlayWin()
  // and gfxVars::UseWebRenderDCompositionTextureOverlayWin() could be changed
  // from true to false, when overlay task is failed. In this case, DisplayItems
  // need to be re-pushed to WebRender for disabling video overlay.
  bool isChanged = (mUseWebRenderDCompVideoHwOverlayWin !=
                    gfx::gfxVars::UseWebRenderDCompVideoHwOverlayWin()) ||
                   (mUseWebRenderDCompVideoSwOverlayWin !=
                    gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin()) ||
                   (mUseWebRenderDCompositionTextureOverlayWin !=
                    gfx::gfxVars::UseWebRenderDCompositionTextureOverlayWin());

  if (isChanged) {
    mUseWebRenderDCompVideoHwOverlayWin =
        gfx::gfxVars::UseWebRenderDCompVideoHwOverlayWin();
    mUseWebRenderDCompVideoSwOverlayWin =
        gfx::gfxVars::UseWebRenderDCompVideoSwOverlayWin();
    mUseWebRenderDCompositionTextureOverlayWin =
        gfx::gfxVars::UseWebRenderDCompositionTextureOverlayWin();
  }
#endif

  wr::Epoch epoch = GetNextImageEpoch();

  // We use a pipeline with a very small display list for each video element.
  // Update each of them if needed.
  for (const auto& entry : mAsyncImagePipelines) {
    wr::PipelineId pipelineId = wr::AsPipelineId(entry.GetKey());
    AsyncImagePipeline* pipeline = entry.GetWeak();

#ifdef XP_WIN
    if (isChanged) {
      pipeline->mIsChanged = true;
    }
#endif

    // If aync image pipeline does not use ImageBridge, do not need to apply.
    if (!pipeline->mImageHost->GetAsyncRef()) {
      continue;
    }
    TextureHost* texture =
        pipeline->mImageHost->GetAsTextureHostForComposite(this);

    ApplyAsyncImageForPipeline(epoch, pipelineId, pipeline, texture,
                               aSceneBuilderTxn, aFastTxn);
  }
}

void AsyncImagePipelineManager::ApplyAsyncImageForPipeline(
    const wr::Epoch& aEpoch, const wr::PipelineId& aPipelineId,
    AsyncImagePipeline* aPipeline, TextureHost* aTexture,
    wr::TransactionBuilder& aSceneBuilderTxn,
    wr::TransactionBuilder& aMaybeFastTxn) {
  nsTArray<wr::ImageKey> keys;
  auto op = UpdateImageKeys(aEpoch, aPipelineId, aPipeline, aTexture, keys,
                            aSceneBuilderTxn, aMaybeFastTxn);

  bool updateDisplayList =
      aPipeline->mInitialised &&
      (aPipeline->mIsChanged || op == Some(TextureHost::ADD_IMAGE)) &&
      !!aPipeline->mCurrentTexture;

  if (!updateDisplayList) {
    // We don't need to update the display list, either because we can't or
    // because the previous one is still up to date. We may, however, have
    // updated some resources.

    // Use transaction of scene builder thread to notify epoch.
    // It is for making epoch update consistent.
    aSceneBuilderTxn.UpdateEpoch(aPipelineId, aEpoch);
    if (aPipeline->mCurrentTexture) {
      HoldExternalImage(aPipelineId, aEpoch, aPipeline->mCurrentTexture);
    }
    return;
  }

  aPipeline->mIsChanged = false;
  // A single image at integral coordinates: no normalization to be exact about.
  aPipeline->mDLBuilder.Begin(AppUnitsPerCSSPixel());

  float opacity = 1.0f;
  wr::StackingContextParams params;
  params.opacity = &opacity;
  params.mix_blend_mode = aPipeline->mMixBlendMode;

  wr::WrComputedTransformData computedTransform;
  computedTransform.vertical_flip =
      aPipeline->mCurrentTexture && aPipeline->mCurrentTexture->NeedsYFlip();
  computedTransform.scale_from = {
      float(aPipeline->mCurrentTexture->GetSize().width),
      float(aPipeline->mCurrentTexture->GetSize().height)};
  computedTransform.rotation = aPipeline->mRotation;

  wr::WrSpatialId referenceFrameId = aPipeline->mDLBuilder.DefineComputedFrame(
      wr::ToLayoutPoint(aPipeline->mScBounds.TopLeft()), computedTransform);
  aPipeline->mDLBuilder.PushStackingContext(
      params,
      // This is fine to do unconditionally because we only push images
      // here.
      wr::RasterSpace::Screen(), Some(referenceFrameId));

  Maybe<wr::SpaceAndClipChainHelper> spaceAndClipChainHelper;
  spaceAndClipChainHelper.emplace(aPipeline->mDLBuilder, referenceFrameId);

  if (aPipeline->mCurrentTexture && !keys.IsEmpty()) {
    LayoutDeviceRect rect(0, 0, aPipeline->mCurrentTexture->GetSize().width,
                          aPipeline->mCurrentTexture->GetSize().height);

    if (aPipeline->mUseExternalImage) {
      MOZ_ASSERT(aPipeline->mCurrentTexture->AsWebRenderTextureHost());
      Range<wr::ImageKey> range_keys(&keys[0], keys.Length());
      TextureHost::PushDisplayItemFlagSet flags;
      flags += TextureHost::PushDisplayItemFlag::PREFER_COMPOSITOR_SURFACE;
      if (aPipeline->mVideoOverlayDisabled &&
          aPipeline->mDLBuilder.GetBackendType() !=
              WebRenderBackend::SOFTWARE) {
        flags +=
            TextureHost::PushDisplayItemFlag::EXTERNAL_COMPOSITING_DISABLED;
      }
      if (mApi->GetCapabilities().mSupportsExternalBufferTextures) {
        flags +=
            TextureHost::PushDisplayItemFlag::SUPPORTS_EXTERNAL_BUFFER_TEXTURES;
      }
      aPipeline->mCurrentTexture->PushDisplayItems(
          aPipeline->mDLBuilder, wr::ToLayoutRect(rect), wr::ToLayoutRect(rect),
          aPipeline->mFilter, range_keys, flags);
      HoldExternalImage(aPipelineId, aEpoch, aPipeline->mCurrentTexture);
    } else {
      MOZ_ASSERT(keys.Length() == 1);
      aPipeline->mDLBuilder.PushImage(wr::ToLayoutRect(rect),
                                      wr::ToLayoutRect(rect), true, false,
                                      aPipeline->mFilter, keys[0]);
    }
  }

  spaceAndClipChainHelper.reset();
  aPipeline->mDLBuilder.PopStackingContext();

  wr::BuiltDisplayList dl;
  aPipeline->mDLBuilder.End(dl);

  aSceneBuilderTxn.SetDisplayList(aEpoch, mIdNamespace, aPipelineId, dl.dl_desc,
                                  dl.dl_items, dl.dl_spatial_tree,
                                  dl.dl_interner_delta);
}

void AsyncImagePipelineManager::ApplyAsyncImageForPipeline(
    const wr::PipelineId& aPipelineId, wr::TransactionBuilder& aTxn,
    wr::TransactionBuilder& aTxnForImageBridge,
    AsyncImagePipelineOps* aPendingOps,
    RemoteTextureInfoList* aPendingRemoteTextures) {
  AsyncImagePipeline* pipeline =
      mAsyncImagePipelines.Get(wr::AsUint64(aPipelineId));
  if (!pipeline) {
    return;
  }

  // ready event of RemoteTexture that uses ImageBridge do not need to be
  // checked here.
  if (pipeline->mImageHost->GetAsyncRef()) {
    aPendingRemoteTextures = nullptr;
  }

  wr::TransactionBuilder fastTxn(mApi, /* aUseSceneBuilderThread */ false);
  wr::AutoTransactionSender sender(mApi, &fastTxn);

  // Transaction for async image pipeline that uses ImageBridge always need to
  // be non low priority.
  auto& sceneBuilderTxn =
      pipeline->mImageHost->GetAsyncRef() ? aTxnForImageBridge : aTxn;

  // Use transaction of using non scene builder thread when ImageHost uses
  // ImageBridge. ApplyAsyncImagesOfImageBridge() handles transaction of adding
  // and updating wr::ImageKeys of ImageHosts that uses ImageBridge. Then
  // AsyncImagePipelineManager always needs to use non scene builder thread
  // transaction for adding and updating wr::ImageKeys of ImageHosts that uses
  // ImageBridge. Otherwise, ordering of wr::ImageKeys updating in webrender
  // becomes inconsistent.
  auto& maybeFastTxn = pipeline->mImageHost->GetAsyncRef() ? fastTxn : aTxn;

  wr::Epoch epoch = GetNextImageEpoch();
  TextureHost* texture =
      pipeline->mImageHost->GetAsTextureHostForComposite(this);
  auto* wrapper = texture ? texture->AsRemoteTextureHostWrapper() : nullptr;

  // Store pending remote texture that is used for waiting at WebRenderAPI.
  if (aPendingRemoteTextures && texture &&
      texture != pipeline->mCurrentTexture && wrapper) {
    aPendingRemoteTextures->mList.emplace(wrapper->GetRemoteTextureInfo());
  }

  if (aPendingOps && !pipeline->mImageHost->GetAsyncRef()) {
    aPendingOps->mList.emplace(AsyncImagePipelineOp::ApplyAsyncImageForPipeline(
        this, aPipelineId, texture));
    return;
  }

  ApplyAsyncImageForPipeline(epoch, aPipelineId, pipeline, texture,
                             sceneBuilderTxn, maybeFastTxn);
}

void AsyncImagePipelineManager::ApplyAsyncImageForPipeline(
    const wr::PipelineId& aPipelineId, TextureHost* aTexture,
    wr::TransactionBuilder& aTxn) {
  AsyncImagePipeline* pipeline =
      mAsyncImagePipelines.Get(wr::AsUint64(aPipelineId));
  if (!pipeline) {
    return;
  }
  MOZ_ASSERT(!pipeline->mImageHost->GetAsyncRef());

  wr::Epoch epoch = GetNextImageEpoch();
  ApplyAsyncImageForPipeline(epoch, aPipelineId, pipeline, aTexture, aTxn,
                             aTxn);
}

void AsyncImagePipelineManager::SetEmptyDisplayList(
    const wr::PipelineId& aPipelineId, wr::TransactionBuilder& aTxn,
    wr::TransactionBuilder& aTxnForImageBridge) {
  AsyncImagePipeline* pipeline =
      mAsyncImagePipelines.Get(wr::AsUint64(aPipelineId));
  if (!pipeline) {
    return;
  }

  // Transaction for async image pipeline that uses ImageBridge always need to
  // be non low priority.
  auto& txn = pipeline->mImageHost->GetAsyncRef() ? aTxnForImageBridge : aTxn;

  wr::Epoch epoch = GetNextImageEpoch();
  wr::DisplayListBuilder builder(aPipelineId,
                                 mApi->GetCapabilities().mBackendType);
  // As above: nothing here is normalized by a scroll offset.
  builder.Begin(AppUnitsPerCSSPixel());

  wr::BuiltDisplayList dl;
  builder.End(dl);
  txn.SetDisplayList(epoch, mIdNamespace, aPipelineId, dl.dl_desc, dl.dl_items,
                     dl.dl_spatial_tree, dl.dl_interner_delta);
}

void AsyncImagePipelineManager::HoldExternalImage(
    const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch,
    TextureHost* aTexture) {
  if (mDestroyed) {
    return;
  }
  MOZ_ASSERT(aTexture);

  PipelineTexturesHolder* holder =
      mPipelineTexturesHolders.Get(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(holder);
  if (!holder) {
    return;
  }
  if (aTexture->NeedsDeferredDeletion()) {
    // Hold WebRenderTextureHost until rendering completed.
    holder->mTextureHostsUntilRenderCompleted.emplace_back(
        std::make_unique<ForwardingTextureHost>(aEpoch, aTexture));
  } else {
    // Hold WebRenderTextureHost until submitted for rendering.
    holder->mTextureHostsUntilRenderSubmitted.emplace_back(aEpoch, aTexture);
  }
}

void AsyncImagePipelineManager::HoldExternalImage(
    const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch,
    const wr::ExternalImageId& aImageId) {
  if (mDestroyed) {
    SharedSurfacesParent::Release(aImageId);
    return;
  }

  PipelineTexturesHolder* holder =
      mPipelineTexturesHolders.Get(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(holder);
  if (!holder) {
    SharedSurfacesParent::Release(aImageId);
    return;
  }

  holder->mExternalImages.emplace_back(
      std::make_unique<ForwardingExternalImage>(aEpoch, aImageId));
}

void AsyncImagePipelineManager::NotifyPipelinesUpdated(
    RefPtr<const wr::WebRenderPipelineInfo> aInfo,
    wr::RenderedFrameId aLatestFrameId,
    wr::RenderedFrameId aLastCompletedFrameId, RefPtr<Fence>&& aReadFence) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  MOZ_ASSERT(mLastCompletedFrameId <= aLastCompletedFrameId.mId);
  MOZ_ASSERT(aLatestFrameId.IsValid());

  mLastCompletedFrameId = aLastCompletedFrameId.mId;

  {
    // We need to lock for mRenderSubmittedUpdates because it can be accessed
    // on the compositor thread.
    MutexAutoLock lock(mRenderSubmittedUpdatesLock);

    // Move the pending updates into the submitted ones.
    mRenderSubmittedUpdates.emplace_back(
        aLatestFrameId,
        WebRenderPipelineInfoHolder(std::move(aInfo), std::move(aReadFence)));
  }

  // Queue a runnable on the compositor thread to process the updates.
  // This will also call CheckForTextureHostsNotUsedByGPU.
  layers::CompositorThread()->Dispatch(
      NewRunnableMethod("ProcessPipelineUpdates", this,
                        &AsyncImagePipelineManager::ProcessPipelineUpdates));
}

void AsyncImagePipelineManager::ProcessPipelineUpdates() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  if (mDestroyed) {
    return;
  }

  std::vector<std::pair<wr::RenderedFrameId, WebRenderPipelineInfoHolder>>
      submittedUpdates;
  {
    // We need to lock for mRenderSubmittedUpdates because it can be accessed on
    // the compositor thread.
    MutexAutoLock lock(mRenderSubmittedUpdatesLock);
    mRenderSubmittedUpdates.swap(submittedUpdates);
  }

  // submittedUpdates is a vector of RenderedFrameIds paired with vectors of
  // WebRenderPipelineInfo.
  for (auto& update : submittedUpdates) {
    auto& holder = update.second;
    const auto& info = holder.mInfo->Raw();

    mReadFence = std::move(holder.mReadFence);

    for (auto& epoch : info.epochs) {
      ProcessPipelineRendered(epoch.pipeline_id, epoch.epoch, update.first);
    }
    for (auto& removedPipeline : info.removed_pipelines) {
      ProcessPipelineRemoved(removedPipeline, update.first);
    }
  }
  CheckForTextureHostsNotUsedByGPU();
}

void AsyncImagePipelineManager::ProcessPipelineRendered(
    const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch,
    wr::RenderedFrameId aRenderedFrameId) {
  if (auto entry = mPipelineTexturesHolders.Lookup(wr::AsUint64(aPipelineId))) {
    const auto& holder = entry.Data();
    // For TextureHosts that can be released on render submission, using aEpoch
    // find the first that we can't release and then release all prior to that.
    auto firstSubmittedHostToKeep = std::find_if(
        holder->mTextureHostsUntilRenderSubmitted.begin(),
        holder->mTextureHostsUntilRenderSubmitted.end(),
        [&aEpoch](const auto& entry) { return aEpoch <= entry.mEpoch; });

    // Set read fence if TextureHost owns AndroidHardwareBuffer.
    // The TextureHost handled by mTextureHostsUntilRenderSubmitted instead of
    // mTextureHostsUntilRenderCompleted, since android fence could be used
    // to wait until its end of usage by GPU.
    for (auto it = holder->mTextureHostsUntilRenderSubmitted.begin();
         it != firstSubmittedHostToKeep; ++it) {
      const auto& entry = it;
      if (entry->mTexture->GetAndroidHardwareBuffer() && mReadFence) {
        entry->mTexture->SetReadFence(mReadFence);
      }
    }

    holder->mTextureHostsUntilRenderSubmitted.erase(
        holder->mTextureHostsUntilRenderSubmitted.begin(),
        firstSubmittedHostToKeep);

    // For TextureHosts that need to wait until render completed, find the first
    // that is later than aEpoch and then move all prior to that to
    // mTexturesInUseByGPU paired with aRenderedFrameId.  These will be released
    // once rendering has completed for aRenderedFrameId.
    auto firstCompletedHostToKeep = std::find_if(
        holder->mTextureHostsUntilRenderCompleted.begin(),
        holder->mTextureHostsUntilRenderCompleted.end(),
        [&aEpoch](const auto& entry) { return aEpoch <= entry->mEpoch; });

    for (auto it = holder->mTextureHostsUntilRenderCompleted.begin();
         it != firstCompletedHostToKeep; ++it) {
      const auto& entry = *it;
      auto* texture = entry->mTexture.get();
      if (texture && mReadFence) {
        texture->SetReadFence(mReadFence);
      }
    }

    if (firstCompletedHostToKeep !=
        holder->mTextureHostsUntilRenderCompleted.begin()) {
      std::vector<UniquePtr<ForwardingTextureHost>> hostsUntilCompleted(
          std::make_move_iterator(
              holder->mTextureHostsUntilRenderCompleted.begin()),
          std::make_move_iterator(firstCompletedHostToKeep));
      mTexturesInUseByGPU.emplace_back(aRenderedFrameId,
                                       std::move(hostsUntilCompleted));
      holder->mTextureHostsUntilRenderCompleted.erase(
          holder->mTextureHostsUntilRenderCompleted.begin(),
          firstCompletedHostToKeep);
    }

    // Using aEpoch, find the first external image that we can't release and
    // then release all prior to that.
    auto firstImageToKeep = std::find_if(
        holder->mExternalImages.begin(), holder->mExternalImages.end(),
        [&aEpoch](const auto& entry) { return aEpoch <= entry->mEpoch; });
    holder->mExternalImages.erase(holder->mExternalImages.begin(),
                                  firstImageToKeep);
  }
}

void AsyncImagePipelineManager::ProcessPipelineRemoved(
    const wr::RemovedPipeline& aRemovedPipeline,
    wr::RenderedFrameId aRenderedFrameId) {
  if (mDestroyed) {
    return;
  }
  if (auto entry = mPipelineTexturesHolders.Lookup(
          wr::AsUint64(aRemovedPipeline.pipeline_id))) {
    const auto& holder = entry.Data();
    if (holder->mDestroyedEpoch.isSome()) {
      if (!holder->mTextureHostsUntilRenderCompleted.empty()) {
        // Move all TextureHosts that must be held until render completed to
        // mTexturesInUseByGPU paired with aRenderedFrameId.
        mTexturesInUseByGPU.emplace_back(
            aRenderedFrameId,
            std::move(holder->mTextureHostsUntilRenderCompleted));
      }

      // Remove Pipeline releasing all remaining TextureHosts and external
      // images.
      entry.Remove();
    }

    // If mDestroyedEpoch contains nothing it means we reused the same pipeline
    // id (probably because we moved the tab to another window). In this case we
    // need to keep the holder.
  }
}

void AsyncImagePipelineManager::CheckForTextureHostsNotUsedByGPU() {
  uint64_t lastCompletedFrameId = mLastCompletedFrameId;

  // Find first entry after mLastCompletedFrameId and release all prior ones.
  auto firstTexturesToKeep =
      std::find_if(mTexturesInUseByGPU.begin(), mTexturesInUseByGPU.end(),
                   [lastCompletedFrameId](const auto& entry) {
                     return lastCompletedFrameId < entry.first.mId;
                   });
  mTexturesInUseByGPU.erase(mTexturesInUseByGPU.begin(), firstTexturesToKeep);
}

wr::Epoch AsyncImagePipelineManager::GetNextImageEpoch() {
  mAsyncImageEpoch.mHandle++;
  return mAsyncImageEpoch;
}

AsyncImagePipelineManager::WebRenderPipelineInfoHolder::
    WebRenderPipelineInfoHolder(RefPtr<const wr::WebRenderPipelineInfo>&& aInfo,
                                RefPtr<Fence>&& aReadFence)
    : mInfo(aInfo), mReadFence(std::move(aReadFence)) {}

AsyncImagePipelineManager::WebRenderPipelineInfoHolder::
    ~WebRenderPipelineInfoHolder() = default;

}  // namespace layers
}  // namespace mozilla
