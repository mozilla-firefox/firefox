/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURED3D11_H
#define MOZILLA_GFX_TEXTURED3D11_H

#include <d3d11.h>
#include <d3d11_1.h>
#include <vector>

#include "d3d9.h"
#include "gfxWindowsPlatform.h"
#include "mozilla/DataMutex.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureHost.h"

namespace mozilla {

namespace gfx {
class FileHandleWrapper;
}  // namespace gfx

namespace gl {
class GLBlitHelper;
}

namespace layers {

class FenceD3D11;

gfx::DeviceResetReason DXGIErrorToDeviceResetReason(HRESULT aError);

already_AddRefed<TextureHost> CreateTextureHostD3D11(
    const SurfaceDescriptor& aDesc, ISurfaceAllocator* aDeallocator,
    LayersBackend aBackend, TextureFlags aFlags);

class MOZ_RAII AutoTextureLock final {
 public:
  AutoTextureLock(IDXGIKeyedMutex* aMutex, HRESULT& aResult,
                  uint32_t aTimeout = 0);
  ~AutoTextureLock();

 private:
  RefPtr<IDXGIKeyedMutex> mMutex;
  HRESULT mResult;
};

class CompositorD3D11;
class ZeroCopyUsageInfo;
class VideoProcessorD3D11;

class D3D11TextureData final : public TextureData {
 public:
  // If aDevice is null, use one provided by gfxWindowsPlatform.
  static D3D11TextureData* Create(gfx::IntSize aSize,
                                  gfx::SurfaceFormat aFormat,
                                  TextureAllocationFlags aAllocFlags,
                                  ID3D11Device* aDevice = nullptr);
  static D3D11TextureData* Create(gfx::SourceSurface* aSurface,
                                  TextureAllocationFlags aAllocFlags,
                                  ID3D11Device* aDevice = nullptr);

  static already_AddRefed<TextureClient> CreateTextureClient(
      ID3D11Texture2D* aTexture, uint32_t aIndex, gfx::IntSize aSize,
      gfx::SurfaceFormat aFormat, gfx::ColorSpace2 aColorSpace,
      gfx::ColorRange aColorRange, KnowsCompositor* aKnowsCompositor,
      RefPtr<ZeroCopyUsageInfo> aUsageInfo,
      const RefPtr<FenceD3D11> aWriteFence);

  virtual ~D3D11TextureData();

  bool UpdateFromSurface(gfx::SourceSurface* aSurface) override;

  bool Lock(OpenMode aMode) override;

  void Unlock() override;

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() override;

  TextureData* CreateSimilar(LayersIPCChannel* aAllocator,
                             LayersBackend aLayersBackend, TextureFlags aFlags,
                             TextureAllocationFlags aAllocFlags) const override;

  void SyncWithObject(RefPtr<SyncObjectClient> aSyncObject) override;

  ID3D11Texture2D* GetD3D11Texture() const { return mTexture; }

  void Deallocate(LayersIPCChannel* aAllocator) override;

  D3D11TextureData* AsD3D11TextureData() override { return this; }

  TextureAllocationFlags GetTextureAllocationFlags() const {
    return mAllocationFlags;
  }

  TextureType GetTextureType() const override { return TextureType::D3D11; }

  void FillInfo(TextureData::Info& aInfo) const override;

  bool Serialize(SurfaceDescriptor& aOutDescrptor) override;
  void GetSubDescriptor(RemoteDecoderVideoSubDescriptor* aOutDesc) override;

  gfx::ColorRange GetColorRange() const { return mColorRange; }
  void SetColorRange(gfx::ColorRange aColorRange) { mColorRange = aColorRange; }

  gfx::IntSize GetSize() const { return mSize; }
  gfx::SurfaceFormat GetSurfaceFormat() const { return mFormat; }

  TextureFlags GetTextureFlags() const override;

  void SetGpuProcessTextureId(GpuProcessTextureId aTextureId) {
    mGpuProcessTextureId = Some(aTextureId);
  }

  Maybe<GpuProcessTextureId> GetGpuProcessTextureId() {
    return mGpuProcessTextureId;
  }

  void IncrementAndSignalWriteFence();

 private:
  D3D11TextureData(ID3D11Device* aDevice, ID3D11Texture2D* aTexture,
                   uint32_t aArrayIndex,
                   RefPtr<gfx::FileHandleWrapper> aSharedHandle,
                   gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
                   const Maybe<CompositeProcessFencesHolderId> aFencesHolderId,
                   const RefPtr<FenceD3D11> aWriteFence,
                   TextureAllocationFlags aFlags);

  bool PrepareDrawTargetInLock(OpenMode aMode);

  friend class gl::GLBlitHelper;
  bool SerializeSpecific(SurfaceDescriptorD3D10* aOutDesc);

  static D3D11TextureData* Create(gfx::IntSize aSize,
                                  gfx::SurfaceFormat aFormat,
                                  gfx::SourceSurface* aSurface,
                                  TextureAllocationFlags aAllocFlags,
                                  ID3D11Device* aDevice = nullptr);

  // Hold on to the DrawTarget because it is expensive to create one each
  // ::Lock.
  RefPtr<gfx::DrawTarget> mDrawTarget;

 public:
  const gfx::IntSize mSize;
  const gfx::SurfaceFormat mFormat;
  const bool mHasKeyedMutex;
  const Maybe<CompositeProcessFencesHolderId> mFencesHolderId;
  const RefPtr<FenceD3D11> mWriteFence;
  gfx::ColorSpace2 mColorSpace = gfx::ColorSpace2::SRGB;

 private:
  gfx::ColorRange mColorRange = gfx::ColorRange::LIMITED;
  bool mNeedsClear = false;

  const RefPtr<ID3D11Device> mDevice;
  RefPtr<ID3D11Texture2D> mTexture;
  const RefPtr<gfx::FileHandleWrapper> mSharedHandle;
  Maybe<GpuProcessTextureId> mGpuProcessTextureId;
  uint32_t mArrayIndex = 0;
  const TextureAllocationFlags mAllocationFlags;
};

class DXGIYCbCrTextureData : public TextureData {
  friend class gl::GLBlitHelper;

 public:
  static DXGIYCbCrTextureData* Create(ID3D11Texture2D* aTextureCb,
                                      ID3D11Texture2D* aTextureY,
                                      ID3D11Texture2D* aTextureCr,
                                      const gfx::IntSize& aSize,
                                      const gfx::IntSize& aSizeY,
                                      const gfx::IntSize& aSizeCbCr,
                                      const gfx::ColorDepth aColorDepth,
                                      const gfx::YUVColorSpace aYUVColorSpace,
                                      const gfx::ColorRange aColorRange);

  bool Lock(OpenMode) override { return true; }

  void Unlock() override {}

  void FillInfo(TextureData::Info& aInfo) const override;

  void SerializeSpecific(SurfaceDescriptorDXGIYCbCr* aOutDesc);
  bool Serialize(SurfaceDescriptor& aOutDescriptor) override;
  void GetSubDescriptor(RemoteDecoderVideoSubDescriptor* aOutDesc) override;

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() override {
    return nullptr;
  }

  void Deallocate(LayersIPCChannel* aAllocator) override;

  bool UpdateFromSurface(gfx::SourceSurface*) override { return false; }

  TextureFlags GetTextureFlags() const override;

  DXGIYCbCrTextureData* AsDXGIYCbCrTextureData() override { return this; }

  ID3D11Texture2D* GetD3D11Texture(size_t index) {
    return mD3D11Textures[index];
  }

  const gfx::IntSize mSize;
  const gfx::IntSize mSizeY;
  const gfx::IntSize mSizeCbCr;
  const gfx::ColorDepth mColorDepth;
  const gfx::YUVColorSpace mYUVColorSpace;
  const gfx::ColorRange mColorRange;
  const CompositeProcessFencesHolderId mFencesHolderId;
  const RefPtr<FenceD3D11> mWriteFence;

 protected:
  DXGIYCbCrTextureData(RefPtr<ID3D11Texture2D> (&aD3D11Textures)[3],
                       RefPtr<gfx::FileHandleWrapper>(aHandles)[3],
                       const gfx::IntSize& aSize, const gfx::IntSize& aSizeY,
                       const gfx::IntSize& aSizeCbCr,
                       const gfx::ColorDepth aColorDepth,
                       const gfx::YUVColorSpace aYUVColorSpace,
                       const gfx::ColorRange aColorRange,
                       const CompositeProcessFencesHolderId aFencesHolderId,
                       const RefPtr<FenceD3D11> aWriteFence);
  virtual ~DXGIYCbCrTextureData();

  RefPtr<ID3D11Texture2D> mD3D11Textures[3];
  RefPtr<gfx::FileHandleWrapper> mHandles[3];
};

/**
 * TextureSource that provides with the necessary APIs to be composited by a
 * CompositorD3D11.
 */
class TextureSourceD3D11 {
 public:
  TextureSourceD3D11() : mFormatOverride(DXGI_FORMAT_UNKNOWN) {}
  virtual ~TextureSourceD3D11() = default;

  virtual ID3D11Texture2D* GetD3D11Texture() const { return mTexture; }
  virtual ID3D11ShaderResourceView* GetShaderResourceView();

 protected:
  virtual gfx::IntSize GetSize() const { return mSize; }

  gfx::IntSize mSize;
  RefPtr<ID3D11Texture2D> mTexture;
  RefPtr<ID3D11ShaderResourceView> mSRV;
  DXGI_FORMAT mFormatOverride;
};

/**
 * A TextureSource that implements the DataTextureSource interface.
 * it can be used without a TextureHost and is able to upload texture data
 * from a gfx::DataSourceSurface.
 */
class DataTextureSourceD3D11 : public DataTextureSource,
                               public TextureSourceD3D11,
                               public BigImageIterator {
 public:
  /// Constructor allowing the texture to perform texture uploads.
  ///
  /// The texture can be used as an actual DataTextureSource.
  DataTextureSourceD3D11(ID3D11Device* aDevice, gfx::SurfaceFormat aFormat,
                         TextureFlags aFlags);

  /// Constructor for textures created around DXGI shared handles, disallowing
  /// texture uploads.
  ///
  /// The texture CANNOT be used as a DataTextureSource.
  DataTextureSourceD3D11(ID3D11Device* aDevice, gfx::SurfaceFormat aFormat,
                         ID3D11Texture2D* aTexture);

  DataTextureSourceD3D11(gfx::SurfaceFormat aFormat,
                         TextureSourceProvider* aProvider,
                         ID3D11Texture2D* aTexture);
  DataTextureSourceD3D11(gfx::SurfaceFormat aFormat,
                         TextureSourceProvider* aProvider, TextureFlags aFlags);

  virtual ~DataTextureSourceD3D11();

  const char* Name() const override { return "DataTextureSourceD3D11"; }

  // DataTextureSource

  bool Update(gfx::DataSourceSurface* aSurface,
              nsIntRegion* aDestRegion = nullptr,
              gfx::IntPoint* aSrcOffset = nullptr,
              gfx::IntPoint* aDstOffset = nullptr) override;

  // TextureSource

  TextureSourceD3D11* AsSourceD3D11() override { return this; }

  ID3D11Texture2D* GetD3D11Texture() const override;

  ID3D11ShaderResourceView* GetShaderResourceView() override;

  // Returns nullptr if this texture was created by a DXGI TextureHost.
  DataTextureSource* AsDataTextureSource() override {
    return mAllowTextureUploads ? this : nullptr;
  }

  void DeallocateDeviceData() override { mTexture = nullptr; }

  gfx::IntSize GetSize() const override { return mSize; }

  gfx::SurfaceFormat GetFormat() const override { return mFormat; }

  // BigImageIterator

  BigImageIterator* AsBigImageIterator() override {
    return mIsTiled ? this : nullptr;
  }

  size_t GetTileCount() override { return mTileTextures.size(); }

  bool NextTile() override { return (++mCurrentTile < mTileTextures.size()); }

  gfx::IntRect GetTileRect() override;

  void EndBigImageIteration() override { mIterating = false; }

  void BeginBigImageIteration() override {
    mIterating = true;
    mCurrentTile = 0;
  }

  RefPtr<TextureSource> ExtractCurrentTile() override;

  void Reset();

 protected:
  gfx::IntRect GetTileRect(uint32_t aIndex) const;

  std::vector<RefPtr<ID3D11Texture2D>> mTileTextures;
  std::vector<RefPtr<ID3D11ShaderResourceView>> mTileSRVs;
  RefPtr<ID3D11Device> mDevice;
  gfx::SurfaceFormat mFormat;
  TextureFlags mFlags;
  uint32_t mCurrentTile;
  bool mIsTiled;
  bool mIterating;
  // Sadly, the code was originally organized so that this class is used both in
  // the cases where we want to perform texture uploads through the
  // DataTextureSource interface, and the cases where we wrap the texture around
  // an existing DXGI handle in which case we should not use it as a
  // DataTextureSource. This member differentiates the two scenarios. When it is
  // false the texture "pretends" to not be a DataTextureSource.
  bool mAllowTextureUploads;
};

/**
 * A TextureHost for shared D3D11 textures.
 */
class DXGITextureHostD3D11 : public TextureHost {
 public:
  DXGITextureHostD3D11(TextureFlags aFlags,
                       const SurfaceDescriptorD3D10& aDescriptor);

  void DeallocateDeviceData() override {}

  gfx::SurfaceFormat GetFormat() const override { return mFormat; }

  gfx::IntSize GetSize() const override { return mSize; }
  gfx::ColorRange GetColorRange() const override { return mColorRange; }

  already_AddRefed<gfx::DataSourceSurface> GetAsSurface(
      gfx::DataSourceSurface* aSurface) override;

  // Return DataSourceSurface using aDevice withou readback to CPU.
  already_AddRefed<gfx::DataSourceSurface> GetAsSurfaceWithDevice(
      ID3D11Device* const aDevice,
      DataMutex<RefPtr<VideoProcessorD3D11>>& aVideoProcessorD3D11);

  void CreateRenderTexture(
      const wr::ExternalImageId& aExternalImageId) override;

  uint32_t NumSubTextures() override;

  void PushResourceUpdates(wr::TransactionBuilder& aResources,
                           ResourceUpdateOp aOp,
                           const Range<wr::ImageKey>& aImageKeys,
                           const wr::ExternalImageId& aExtID) override;

  void PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                        const wr::LayoutRect& aBounds,
                        const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
                        const Range<wr::ImageKey>& aImageKeys,
                        PushDisplayItemFlagSet aFlags) override;

  bool SupportsExternalCompositing(WebRenderBackend aBackend) override;

  DXGITextureHostD3D11* AsDXGITextureHostD3D11() override { return this; }

  const RefPtr<gfx::FileHandleWrapper> mHandle;
  const Maybe<GpuProcessTextureId> mGpuProcessTextureId;
  const uint32_t mArrayIndex;
  const gfx::IntSize mSize;
  const gfx::SurfaceFormat mFormat;
  const bool mHasKeyedMutex;
  const Maybe<CompositeProcessFencesHolderId> mFencesHolderId;
  const gfx::ColorSpace2 mColorSpace;
  const gfx::ColorRange mColorRange;
};

class DXGIYCbCrTextureHostD3D11 : public TextureHost {
 public:
  DXGIYCbCrTextureHostD3D11(TextureFlags aFlags,
                            const SurfaceDescriptorDXGIYCbCr& aDescriptor);

  void DeallocateDeviceData() override {}

  gfx::SurfaceFormat GetFormat() const override {
    return gfx::SurfaceFormat::YUV420;
  }

  gfx::ColorDepth GetColorDepth() const override { return mColorDepth; }
  gfx::YUVColorSpace GetYUVColorSpace() const override {
    return mYUVColorSpace;
  }
  gfx::ColorRange GetColorRange() const override { return mColorRange; }

  gfx::IntSize GetSize() const override { return mSize; }

  already_AddRefed<gfx::DataSourceSurface> GetAsSurface(
      gfx::DataSourceSurface* aSurface) override {
    return nullptr;
  }

  void CreateRenderTexture(
      const wr::ExternalImageId& aExternalImageId) override;

  uint32_t NumSubTextures() override;

  void PushResourceUpdates(wr::TransactionBuilder& aResources,
                           ResourceUpdateOp aOp,
                           const Range<wr::ImageKey>& aImageKeys,
                           const wr::ExternalImageId& aExtID) override;

  void PushDisplayItems(wr::DisplayListBuilder& aBuilder,
                        const wr::LayoutRect& aBounds,
                        const wr::LayoutRect& aClip, wr::ImageRendering aFilter,
                        const Range<wr::ImageKey>& aImageKeys,
                        PushDisplayItemFlagSet aFlags) override;

  bool SupportsExternalCompositing(WebRenderBackend aBackend) override;

  void NotifyNotUsed() override;

  DXGIYCbCrTextureHostD3D11* AsDXGIYCbCrTextureHostD3D11() override {
    return this;
  }

  void SetReadFence(RefPtr<FenceD3D11> aReadFence);

  const gfx::IntSize mSize;
  const gfx::IntSize mSizeY;
  const gfx::IntSize mSizeCbCr;
  const gfx::ColorDepth mColorDepth;
  const gfx::YUVColorSpace mYUVColorSpace;
  const gfx::ColorRange mColorRange;
  const CompositeProcessFencesHolderId mFencesHolderId;

 protected:
  // Handles will be closed automatically when `UniqueFileHandle` gets
  // destroyed.
  RefPtr<gfx::FileHandleWrapper> mHandles[3];
  bool mIsLocked = false;
  RefPtr<FenceD3D11> mReadFence;
};

class CompositingRenderTargetD3D11 : public CompositingRenderTarget,
                                     public TextureSourceD3D11 {
 public:
  CompositingRenderTargetD3D11(
      ID3D11Texture2D* aTexture, const gfx::IntPoint& aOrigin,
      DXGI_FORMAT aFormatOverride = DXGI_FORMAT_UNKNOWN);

  const char* Name() const override { return "CompositingRenderTargetD3D11"; }

  TextureSourceD3D11* AsSourceD3D11() override { return this; }

  void BindRenderTarget(ID3D11DeviceContext* aContext);

  gfx::IntSize GetSize() const override;

  void SetSize(const gfx::IntSize& aSize) { mSize = aSize; }

 private:
  friend class CompositorD3D11;
  RefPtr<ID3D11RenderTargetView> mRTView;
};

class SyncObjectD3D11Host : public SyncObjectHost {
 public:
  explicit SyncObjectD3D11Host(ID3D11Device* aDevice);

  bool Init() override;

  SyncHandle GetSyncHandle() override;

  bool Synchronize(bool aFallible) override;

  IDXGIKeyedMutex* GetKeyedMutex() { return mKeyedMutex.get(); };

 private:
  virtual ~SyncObjectD3D11Host() = default;

  SyncHandle mSyncHandle;
  RefPtr<ID3D11Device> mDevice;
  RefPtr<IDXGIResource1> mSyncTexture;
  RefPtr<IDXGIKeyedMutex> mKeyedMutex;
};

class SyncObjectD3D11Client : public SyncObjectClient {
 public:
  SyncObjectD3D11Client(SyncHandle aSyncHandle, ID3D11Device* aDevice);

  bool Synchronize(bool aFallible) override;

  bool IsSyncObjectValid() override;

  void EnsureInitialized() override {}

  SyncType GetSyncType() override { return SyncType::D3D11; }

  void RegisterTexture(ID3D11Texture2D* aTexture);

 protected:
  explicit SyncObjectD3D11Client(SyncHandle aSyncHandle);
  bool Init(ID3D11Device* aDevice, bool aFallible);
  bool SynchronizeInternal(ID3D11Device* aDevice, bool aFallible);
  Mutex mSyncLock MOZ_UNANNOTATED;
  RefPtr<ID3D11Texture2D> mSyncTexture;
  std::vector<ID3D11Texture2D*> mSyncedTextures;

 private:
  SyncHandle mSyncHandle;
  RefPtr<IDXGIKeyedMutex> mKeyedMutex;
  const RefPtr<ID3D11Device> mDevice;
};

class SyncObjectD3D11ClientContentDevice : public SyncObjectD3D11Client {
 public:
  explicit SyncObjectD3D11ClientContentDevice(SyncHandle aSyncHandle);

  bool Synchronize(bool aFallible) override;

  bool IsSyncObjectValid() override;

  void EnsureInitialized() override;

 private:
  RefPtr<ID3D11Device> mContentDevice;
};

inline uint32_t GetMaxTextureSizeForFeatureLevel(
    D3D_FEATURE_LEVEL aFeatureLevel) {
  int32_t maxTextureSize;
  switch (aFeatureLevel) {
    case D3D_FEATURE_LEVEL_11_1:
    case D3D_FEATURE_LEVEL_11_0:
      maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
      break;
    case D3D_FEATURE_LEVEL_10_1:
    case D3D_FEATURE_LEVEL_10_0:
      maxTextureSize = D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
      break;
    case D3D_FEATURE_LEVEL_9_3:
      maxTextureSize = D3D_FL9_3_REQ_TEXTURE2D_U_OR_V_DIMENSION;
      break;
    default:
      maxTextureSize = D3D_FL9_1_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  }
  return maxTextureSize;
}

uint32_t GetMaxTextureSizeFromDevice(ID3D11Device* aDevice);
void ReportTextureMemoryUsage(ID3D11Texture2D* aTexture, size_t aBytes);

class AutoLockD3D11Texture {
 public:
  explicit AutoLockD3D11Texture(ID3D11Texture2D* aTexture);
  ~AutoLockD3D11Texture();

 private:
  RefPtr<IDXGIKeyedMutex> mMutex;
};

class D3D11MTAutoEnter {
 public:
  explicit D3D11MTAutoEnter(already_AddRefed<ID3D10Multithread> aMT)
      : mMT(aMT) {
    if (mMT) {
      mMT->Enter();
    }
  }
  ~D3D11MTAutoEnter() {
    if (mMT) {
      mMT->Leave();
    }
  }

 private:
  RefPtr<ID3D10Multithread> mMT;
};

}  // namespace layers
}  // namespace mozilla

#endif /* MOZILLA_GFX_TEXTURED3D11_H */
