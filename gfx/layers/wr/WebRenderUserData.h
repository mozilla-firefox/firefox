/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WEBRENDERUSERDATA_H
#define GFX_WEBRENDERUSERDATA_H

#include <vector>

#include "DisplayItemClip.h"
#include "ImageTypes.h"
#include "ImgDrawResult.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/DrawEventRecorderTypes.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/AnimationInfo.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsIFrame.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashSet.h"

namespace mozilla {

class nsDisplayItemGeometry;

namespace webgpu {
class WebGPUChild;
}

namespace wr {
class IpcResourceUpdateQueue;
}

namespace gfx {
class SourceSurface;
}

namespace layers {

class BasicLayerManager;
class CanvasRenderer;
class ImageClient;
class ImageContainer;
class WebRenderBridgeChild;
class WebRenderCanvasData;
class WebRenderCanvasRenderer;
class WebRenderCanvasRendererAsync;
class WebRenderImageData;
class WebRenderImageProviderData;
class WebRenderFallbackData;
class RenderRootStateManager;
class WebRenderGroupData;

class WebRenderBackgroundData {
 public:
  WebRenderBackgroundData(wr::LayoutRect aBounds, wr::ColorF aColor)
      : mBounds(aBounds), mColor(aColor) {}
  void AddWebRenderCommands(wr::DisplayListBuilder& aBuilder);

 protected:
  wr::LayoutRect mBounds;
  wr::ColorF mColor;
};

/// Parent class for arbitrary WebRender-specific data that can be associated
/// to an nsFrame.
class WebRenderUserData {
 public:
  typedef nsTHashSet<RefPtr<WebRenderUserData>> WebRenderUserDataRefTable;

  static bool SupportsAsyncUpdate(nsIFrame* aFrame);

  static bool ProcessInvalidateForImage(nsIFrame* aFrame, DisplayItemType aType,
                                        image::ImageProviderId aProviderId);

  NS_INLINE_DECL_REFCOUNTING(WebRenderUserData)

  WebRenderUserData(RenderRootStateManager* aManager, nsDisplayItem* aItem);
  WebRenderUserData(RenderRootStateManager* aManager, uint32_t mDisplayItemKey,
                    nsIFrame* aFrame);

  virtual WebRenderImageData* AsImageData() { return nullptr; }
  virtual WebRenderImageProviderData* AsImageProviderData() { return nullptr; }
  virtual WebRenderFallbackData* AsFallbackData() { return nullptr; }
  virtual WebRenderCanvasData* AsCanvasData() { return nullptr; }
  virtual WebRenderGroupData* AsGroupData() { return nullptr; }

  enum class UserDataType {
    eImage,
    eFallback,
    eAPZAnimation,
    eAnimation,
    eCanvas,
    eRemote,
    eGroup,
    eMask,
    eImageProvider,  // ImageLib
    eInProcessImage,
  };

  virtual UserDataType GetType() = 0;
  bool IsUsed() { return mUsed; }
  void SetUsed(bool aUsed) { mUsed = aUsed; }
  nsIFrame* GetFrame() { return mFrame; }
  uint32_t GetDisplayItemKey() { return mDisplayItemKey; }
  void RemoveFromTable();
  virtual nsDisplayItemGeometry* GetGeometry() { return nullptr; }

 protected:
  virtual ~WebRenderUserData();

  WebRenderBridgeChild* WrBridge() const;

  RefPtr<RenderRootStateManager> mManager;
  nsIFrame* mFrame;
  uint32_t mDisplayItemKey;
  WebRenderUserDataRefTable* mTable;
  bool mUsed;
};

struct WebRenderUserDataKey {
  WebRenderUserDataKey(uint32_t aFrameKey,
                       WebRenderUserData::UserDataType aType)
      : mFrameKey(aFrameKey), mType(aType) {}

  bool operator==(const WebRenderUserDataKey& other) const = default;
  PLDHashNumber Hash() const {
    return HashGeneric(
        mFrameKey, static_cast<std::underlying_type_t<decltype(mType)>>(mType));
  }

  uint32_t mFrameKey;
  WebRenderUserData::UserDataType mType;
};

typedef nsRefPtrHashtable<
    nsGenericHashKey<mozilla::layers::WebRenderUserDataKey>, WebRenderUserData>
    WebRenderUserDataTable;

/// Holds some data used to share TextureClient/ImageClient with the parent
/// process.
class WebRenderImageData : public WebRenderUserData {
 public:
  WebRenderImageData(RenderRootStateManager* aManager, nsDisplayItem* aItem);
  WebRenderImageData(RenderRootStateManager* aManager, uint32_t aDisplayItemKey,
                     nsIFrame* aFrame);
  virtual ~WebRenderImageData();

  WebRenderImageData* AsImageData() override { return this; }
  UserDataType GetType() override { return UserDataType::eImage; }
  static UserDataType Type() { return UserDataType::eImage; }
  Maybe<wr::ImageKey> GetImageKey() { return mKey; }
  void SetImageKey(const wr::ImageKey& aKey);
  already_AddRefed<ImageClient> GetImageClient();

  Maybe<wr::ImageKey> UpdateImageKey(ImageContainer* aContainer,
                                     wr::IpcResourceUpdateQueue& aResources,
                                     bool aFallback = false);

  void CreateAsyncImageWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder, ImageContainer* aContainer,
      const StackingContextHelper& aSc, const LayoutDeviceRect& aBounds,
      const LayoutDeviceRect& aSCBounds, wr::WrRotation aRotation,
      const wr::ImageRendering& aFilter, const wr::MixBlendMode& aMixBlendMode,
      bool aIsBackfaceVisible);

  void CreateImageClientIfNeeded();

  bool IsAsync() { return mPipelineId.isSome(); }

  void ClearImageKey();

 protected:
  Maybe<wr::ImageKey> mKey;
  RefPtr<TextureClient> mTextureOfImage;
  RefPtr<ImageClient> mImageClient;
  Maybe<wr::PipelineId> mPipelineId;
  RefPtr<ImageContainer> mContainer;
};

/// Holds some data used to share ImageLib results with the parent process.
/// This may be either in the form of a blob recording or a rasterized surface.
class WebRenderImageProviderData final : public WebRenderUserData {
 public:
  WebRenderImageProviderData(RenderRootStateManager* aManager,
                             nsDisplayItem* aItem);
  WebRenderImageProviderData(RenderRootStateManager* aManager,
                             uint32_t aDisplayItemKey, nsIFrame* aFrame);
  ~WebRenderImageProviderData() override;

  WebRenderImageProviderData* AsImageProviderData() override { return this; }
  UserDataType GetType() override { return UserDataType::eImageProvider; }
  static UserDataType Type() { return UserDataType::eImageProvider; }

  Maybe<wr::ImageKey> UpdateImageKey(image::WebRenderImageProvider* aProvider,
                                     image::ImgDrawResult aDrawResult,
                                     wr::IpcResourceUpdateQueue& aResources);

  bool Invalidate(image::ImageProviderId aProviderId) const;

 protected:
  RefPtr<image::WebRenderImageProvider> mProvider;
  Maybe<wr::ImageKey> mKey;
  image::ImgDrawResult mDrawResult = image::ImgDrawResult::NOT_READY;
};

/// Used for fallback rendering.
///
/// In most cases this uses blob images but it can also render on the content
/// side directly into a texture.
class WebRenderFallbackData : public WebRenderUserData {
 public:
  WebRenderFallbackData(RenderRootStateManager* aManager, nsDisplayItem* aItem);
  WebRenderFallbackData(RenderRootStateManager* aManager,
                        uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderFallbackData();

  WebRenderFallbackData* AsFallbackData() override { return this; }
  UserDataType GetType() override { return UserDataType::eFallback; }
  static UserDataType Type() { return UserDataType::eFallback; }
  nsDisplayItemGeometry* GetGeometry() override { return mGeometry.get(); }

  void SetInvalid(bool aInvalid) { mInvalid = aInvalid; }
  bool IsInvalid() { return mInvalid; }
  void SetFonts(const std::vector<RefPtr<gfx::ScaledFont>>& aFonts) {
    mFonts = aFonts;
  }
  Maybe<wr::BlobImageKey> GetBlobImageKey() { return mBlobKey; }
  void SetBlobImageKey(const wr::BlobImageKey& aKey);
  Maybe<wr::ImageKey> GetImageKey();

  /// Create a WebRenderImageData to manage the image we are about to render
  /// into.
  WebRenderImageData* PaintIntoImage();

  gfx::DrawEventRecorderPrivate_ExternalSurfacesHolder mExternalSurfaces;
  UniquePtr<nsDisplayItemGeometry> mGeometry;
  DisplayItemClip mClip;
  nsRect mBounds;
  nsRect mBuildingRect;
  gfx::MatrixScales mScale;
  float mOpacity;

 protected:
  void ClearImageKey();

  std::vector<RefPtr<gfx::ScaledFont>> mFonts;
  Maybe<wr::BlobImageKey> mBlobKey;
  // When rendering into a blob image, mImageData is null. It is non-null only
  // when we render directly into a texture on the content side.
  RefPtr<WebRenderImageData> mImageData;
  bool mInvalid;
};

class WebRenderAPZAnimationData : public WebRenderUserData {
 public:
  WebRenderAPZAnimationData(RenderRootStateManager* aManager,
                            nsDisplayItem* aItem);
  WebRenderAPZAnimationData(RenderRootStateManager* aManager,
                            uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderAPZAnimationData() = default;

  UserDataType GetType() override { return UserDataType::eAPZAnimation; }
  static UserDataType Type() { return UserDataType::eAPZAnimation; }
  uint64_t GetAnimationId() { return mAnimationId; }

 private:
  uint64_t mAnimationId;
};

class WebRenderAnimationData : public WebRenderUserData {
 public:
  WebRenderAnimationData(RenderRootStateManager* aManager,
                         nsDisplayItem* aItem);
  WebRenderAnimationData(RenderRootStateManager* aManager,
                         uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderAnimationData();

  UserDataType GetType() override { return UserDataType::eAnimation; }
  static UserDataType Type() { return UserDataType::eAnimation; }
  AnimationInfo& GetAnimationInfo() { return mAnimationInfo; }

 protected:
  AnimationInfo mAnimationInfo;
};

class WebRenderCanvasData : public WebRenderUserData {
 public:
  WebRenderCanvasData(RenderRootStateManager* aManager, nsDisplayItem* aItem);
  WebRenderCanvasData(RenderRootStateManager* aManager,
                      uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderCanvasData();

  WebRenderCanvasData* AsCanvasData() override { return this; }
  UserDataType GetType() override { return UserDataType::eCanvas; }
  static UserDataType Type() { return UserDataType::eCanvas; }

  void ClearCanvasRenderer();
  WebRenderCanvasRendererAsync* GetCanvasRenderer();
  WebRenderCanvasRendererAsync* CreateCanvasRenderer();
  bool SetCanvasRenderer(CanvasRenderer* aCanvasRenderer);

  void SetImageContainer(ImageContainer* aImageContainer);
  ImageContainer* GetImageContainer();
  void ClearImageContainer();

 protected:
  RefPtr<WebRenderCanvasRendererAsync> mCanvasRenderer;
  RefPtr<ImageContainer> mContainer;
};

class WebRenderMaskData : public WebRenderUserData {
 public:
  explicit WebRenderMaskData(RenderRootStateManager* aManager,
                             nsDisplayItem* aItem);
  explicit WebRenderMaskData(RenderRootStateManager* aManager,
                             uint32_t aDisplayItemKey, nsIFrame* aFrame);
  virtual ~WebRenderMaskData() {
    MOZ_COUNT_DTOR(WebRenderMaskData);
    ClearImageKey();
  }

  void ClearImageKey();
  void Invalidate();

  UserDataType GetType() override { return UserDataType::eMask; }
  static UserDataType Type() { return UserDataType::eMask; }

  Maybe<wr::BlobImageKey> mBlobKey;
  std::vector<RefPtr<gfx::ScaledFont>> mFonts;
  gfx::DrawEventRecorderPrivate_ExternalSurfacesHolder mExternalSurfaces;
  LayerIntRect mItemRect;
  // Sub-pixel offset of the item's bounds from mItemRect, which the recorded
  // alpha is rasterized against.
  gfx::Point mResidual;
  nsPoint mMaskOffset;
  nsStyleImageLayers mMaskStyle;
  gfx::MatrixScales mScale;
  bool mShouldHandleOpacity;
  // The recording behind mBlobKey, made with deterministic reference ids, so
  // that an identical new recording doesn't need to be uploaded and
  // rasterized again. Empty if it was too large to keep.
  nsTArray<uint8_t> mRecording;
  gfx::IntSize mRecordingSize;
};

extern void DestroyWebRenderUserDataTable(WebRenderUserDataTable* aTable);

struct WebRenderUserDataProperty {
  NS_DECLARE_FRAME_PROPERTY_WITH_DTOR(Key, WebRenderUserDataTable,
                                      DestroyWebRenderUserDataTable)
};

template <class T>
already_AddRefed<T> GetWebRenderUserData(const nsIFrame* aFrame,
                                         uint32_t aPerFrameKey) {
  MOZ_ASSERT(aFrame);
  WebRenderUserDataTable* userDataTable =
      aFrame->GetProperty(WebRenderUserDataProperty::Key());
  if (!userDataTable) {
    return nullptr;
  }

  WebRenderUserData* data =
      userDataTable->GetWeak(WebRenderUserDataKey(aPerFrameKey, T::Type()));
  if (data) {
    RefPtr<T> result = static_cast<T*>(data);
    return result.forget();
  }

  return nullptr;
}

}  // namespace layers
}  // namespace mozilla

#endif /* GFX_WEBRENDERUSERDATA_H */
