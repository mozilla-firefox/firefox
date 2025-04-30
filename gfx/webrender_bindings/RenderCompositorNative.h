/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_NATIVE_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_NATIVE_H

#include <deque>
#include <unordered_map>

#include "GLTypes.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/layers/ScreenshotGrabber.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/TimeStamp.h"

namespace mozilla {

namespace layers {
class GpuFence;
class NativeLayerRootSnapshotter;
class NativeLayerRoot;
class NativeLayer;
class SurfacePoolHandle;
}  // namespace layers

namespace wr {

// RenderCompositorNative is a skeleton class for implementing compositors
// backed by NativeLayer surfaces and tiles. This is not meant to be directly
// instantiated and is instead derived for various use-cases such as OpenGL or
// SWGL.
class RenderCompositorNative : public RenderCompositor {
 public:
  virtual ~RenderCompositorNative();

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) final;
  void Pause() override;
  bool Resume() override;

  layers::WebRenderCompositor CompositorType() const override;

  LayoutDeviceIntSize GetBufferSize() override;

  bool ShouldUseNativeCompositor() override;
  void GetCompositorCapabilities(CompositorCapabilities* aCaps) override;

  bool SurfaceOriginIsTopLeft() override { return true; }

  // Does the readback for the ShouldUseNativeCompositor() case.
  bool MaybeReadback(const gfx::IntSize& aReadbackSize,
                     const wr::ImageFormat& aReadbackFormat,
                     const Range<uint8_t>& aReadbackBuffer,
                     bool* aNeedsYFlip) override;
  bool MaybeRecordFrame(layers::CompositionRecorder& aRecorder) override;
  bool MaybeGrabScreenshot(const gfx::IntSize& aWindowSize) override;
  bool MaybeProcessScreenshotQueue() override;

  // Interface for wr::Compositor
  void CompositorBeginFrame() override;
  void CompositorEndFrame() override;
  void CreateSurface(wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
                     wr::DeviceIntSize aTileSize, bool aIsOpaque) override;
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) override;
  void CreateBackdropSurface(wr::NativeSurfaceId aId,
                             wr::ColorF aColor) override;
  void DestroySurface(NativeSurfaceId aId) override;
  void CreateTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void DestroyTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering,
                  wr::DeviceIntRect aRoundedClipRect,
                  wr::ClipRadius aClipRadius) override;

  struct TileKey {
    TileKey(int32_t aX, int32_t aY) : mX(aX), mY(aY) {}

    int32_t mX;
    int32_t mY;
  };

 protected:
  explicit RenderCompositorNative(
      const RefPtr<widget::CompositorWidget>& aWidget,
      gl::GLContext* aGL = nullptr);

  virtual bool InitDefaultFramebuffer(const gfx::IntRect& aBounds) = 0;
  virtual void DoSwap() = 0;
  virtual void DoFlush() {}

  void BindNativeLayer(wr::NativeTileId aId, const gfx::IntRect& aDirtyRect);
  void UnbindNativeLayer();

  // Can be null.
  RefPtr<layers::NativeLayerRoot> mNativeLayerRoot;
  UniquePtr<layers::NativeLayerRootSnapshotter> mNativeLayerRootSnapshotter;
  layers::ScreenshotGrabber mProfilerScreenshotGrabber;
  RefPtr<layers::NativeLayer> mNativeLayerForEntireWindow;
  RefPtr<layers::SurfacePoolHandle> mSurfacePoolHandle;

  struct TileKeyHashFn {
    std::size_t operator()(const TileKey& aId) const {
      return HashGeneric(aId.mX, aId.mY);
    }
  };

  struct Surface {
    explicit Surface(wr::DeviceIntSize aTileSize, bool aIsOpaque)
        : mTileSize(aTileSize), mIsOpaque(aIsOpaque) {}
    gfx::IntSize TileSize() {
      return gfx::IntSize(mTileSize.width, mTileSize.height);
    }

    // External images can change size depending on which image
    // is attached, so mTileSize will be 0,0 when mIsExternal
    // is true.
    wr::DeviceIntSize mTileSize;
    bool mIsOpaque;
    bool mIsExternal = false;
    std::unordered_map<TileKey, RefPtr<layers::NativeLayer>, TileKeyHashFn>
        mNativeLayers;
  };

  struct SurfaceIdHashFn {
    std::size_t operator()(const wr::NativeSurfaceId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  // Used in native compositor mode:
  RefPtr<layers::NativeLayer> mCurrentlyBoundNativeLayer;
  nsTArray<RefPtr<layers::NativeLayer>> mAddedLayers;
  uint64_t mTotalTilePixelCount = 0;
  uint64_t mAddedTilePixelCount = 0;
  uint64_t mAddedClippedPixelCount = 0;
  uint64_t mDrawnPixelCount = 0;
  gfx::IntRect mVisibleBounds;
  std::unordered_map<wr::NativeSurfaceId, Surface, SurfaceIdHashFn> mSurfaces;
  TimeStamp mBeginFrameTimeStamp;
  std::deque<RefPtr<layers::GpuFence>> mPendingGpuFeces;
};

static inline bool operator==(const RenderCompositorNative::TileKey& a0,
                              const RenderCompositorNative::TileKey& a1) {
  return a0.mX == a1.mX && a0.mY == a1.mY;
}

// RenderCompositorNativeOGL is a NativeLayer compositor that exposes an
// OpenGL framebuffer for the respective NativeLayer bound to each tile.
class RenderCompositorNativeOGL : public RenderCompositorNative {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorNativeOGL(const RefPtr<widget::CompositorWidget>& aWidget,
                            RefPtr<gl::GLContext>&& aGL);
  virtual ~RenderCompositorNativeOGL();

  bool WaitForGPU() override;

  gl::GLContext* gl() const override { return mGL; }

  void Bind(wr::NativeTileId aId, wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
            wr::DeviceIntRect aDirtyRect,
            wr::DeviceIntRect aValidRect) override;
  void Unbind() override;

 protected:
  void InsertFrameDoneSync();

  bool InitDefaultFramebuffer(const gfx::IntRect& aBounds) override;
  void DoSwap() override;
  void DoFlush() override;

  RefPtr<gl::GLContext> mGL;

  struct BackPressureFences {
    explicit BackPressureFences(
        std::deque<RefPtr<layers::GpuFence>>&& aGpuFeces)
        : mGpuFeces(std::move(aGpuFeces)) {}

    GLsync mSync = nullptr;
    std::deque<RefPtr<layers::GpuFence>> mGpuFeces;
  };

  // Used to apply back-pressure in WaitForGPU().
  UniquePtr<BackPressureFences> mPreviousFrameDoneFences;
  UniquePtr<BackPressureFences> mThisFrameDoneFences;
};

// RenderCompositorNativeSWGL is a NativeLayer compositor that only
// deals with mapping the underlying buffer for SWGL usage of a tile.
class RenderCompositorNativeSWGL : public RenderCompositorNative {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorNativeSWGL(const RefPtr<widget::CompositorWidget>& aWidget,
                             void* aContext);
  virtual ~RenderCompositorNativeSWGL();

  void* swgl() const override { return mContext; }

  bool MakeCurrent() override;

  void CancelFrame() override;

  layers::WebRenderBackend BackendType() const override {
    return layers::WebRenderBackend::SOFTWARE;
  }

  // Maps an underlying layer and sets aData to the top left pixel of
  // aValidRect.  The row stride is set to aStride, note this doesn't
  // mean there are always aStride bytes available per row (the
  // last row will fall short if aValidRect is not at X==0).
  bool MapTile(wr::NativeTileId aId, wr::DeviceIntRect aDirtyRect,
               wr::DeviceIntRect aValidRect, void** aData,
               int32_t* aStride) override;
  void UnmapTile() override;

 protected:
  bool InitDefaultFramebuffer(const gfx::IntRect& aBounds) override;
  void DoSwap() override;

  bool MapNativeLayer(layers::NativeLayer* aLayer,
                      const gfx::IntRect& aDirtyRect,
                      const gfx::IntRect& aValidRect);
  void UnmapNativeLayer();

  void* mContext = nullptr;
  RefPtr<gfx::DrawTarget> mLayerTarget;
  uint8_t* mLayerData = nullptr;
  uint8_t* mLayerValidRectData = nullptr;
  int32_t mLayerStride = 0;
};

}  // namespace wr
}  // namespace mozilla

#endif
