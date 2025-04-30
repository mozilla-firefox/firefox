/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_Layers_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_Layers_H

#include <unordered_map>

#include "mozilla/HashFunctions.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/ScreenshotGrabber.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/webrender/RenderTextureHost.h"

namespace mozilla {

namespace wr {

class SurfaceD3D11SWGL;

class RenderCompositorLayersSWGL : public RenderCompositor {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorLayersSWGL(layers::Compositor* aCompositor,
                             const RefPtr<widget::CompositorWidget>& aWidget,
                             void* aContext);
  virtual ~RenderCompositorLayersSWGL();

  void* swgl() const override { return mContext; }

  bool MakeCurrent() override;

  bool BeginFrame() override;
  void CancelFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) override;

  bool SurfaceOriginIsTopLeft() override { return true; }

  LayoutDeviceIntSize GetBufferSize() override;

  // Should we support this?
  bool SupportsExternalBufferTextures() const override { return false; }

  layers::WebRenderBackend BackendType() const override {
    return layers::WebRenderBackend::SOFTWARE;
  }

  bool ShouldUseNativeCompositor() override { return true; }

  void StartCompositing(wr::ColorF aClearColor,
                        const wr::DeviceIntRect* aDirtyRects,
                        size_t aNumDirtyRects,
                        const wr::DeviceIntRect* aOpaqueRects,
                        size_t aNumOpaqueRects) override;

  void CompositorBeginFrame() override {}
  void CompositorEndFrame() override;
  void Bind(wr::NativeTileId aId, wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
            wr::DeviceIntRect aDirtyRect,
            wr::DeviceIntRect aValidRect) override;
  void Unbind() override;
  bool MapTile(wr::NativeTileId aId, wr::DeviceIntRect aDirtyRect,
               wr::DeviceIntRect aValidRect, void** aData,
               int32_t* aStride) override;
  void UnmapTile() override;
  void CreateSurface(wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
                     wr::DeviceIntSize aTileSize, bool aIsOpaque) override;
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) override;
  void DestroySurface(NativeSurfaceId aId) override;
  void CreateTile(wr::NativeSurfaceId, int32_t aX, int32_t aY) override;
  void DestroyTile(wr::NativeSurfaceId, int32_t aX, int32_t aY) override;
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering,
                  wr::DeviceIntRect aRoundedClipRect,
                  wr::ClipRadius aClipRadius) override;
  void EnableNativeCompositor(bool aEnable) override {}
  void DeInit() override {}

  void MaybeRequestAllowFrameRecording(bool aWillRecord) override;
  bool MaybeRecordFrame(layers::CompositionRecorder& aRecorder) override;
  bool MaybeGrabScreenshot(const gfx::IntSize& aWindowSize) override;
  bool MaybeProcessScreenshotQueue() override;

  // TODO: Screenshots etc

  struct TileKey {
    TileKey(int32_t aX, int32_t aY) : mX(aX), mY(aY) {}

    int32_t mX;
    int32_t mY;
  };

  // Each tile retains a texture, and a DataSourceSurface of the
  // same size. We draw into the source surface, and then copy the
  // changed area into the texture.
  class Tile {
   public:
    Tile() = default;
    virtual ~Tile() = default;

    virtual bool Map(wr::DeviceIntRect aDirtyRect, wr::DeviceIntRect aValidRect,
                     void** aData, int32_t* aStride) = 0;
    virtual void Unmap(const gfx::IntRect& aDirtyRect) = 0;
    virtual layers::DataTextureSource* GetTextureSource() = 0;
    virtual bool IsValid() = 0;

    gfx::Rect mValidRect;

    struct KeyHashFn {
      std::size_t operator()(const TileKey& aId) const {
        return HashGeneric(aId.mX, aId.mY);
      }
    };
  };

  class Surface {
   public:
    explicit Surface(wr::DeviceIntSize aTileSize, bool aIsOpaque)
        : mTileSize(aTileSize), mIsOpaque(aIsOpaque) {}
    virtual ~Surface() {}

    gfx::IntSize TileSize() {
      return gfx::IntSize(mTileSize.width, mTileSize.height);
    }
    virtual SurfaceD3D11SWGL* AsSurfaceD3D11SWGL() { return nullptr; }

    // External images can change size depending on which image
    // is attached, so mTileSize will be 0,0 when mIsExternal
    // is true.
    wr::DeviceIntSize mTileSize;
    bool mIsOpaque;
    bool mIsExternal = false;
    std::unordered_map<TileKey, UniquePtr<Tile>, Tile::KeyHashFn> mTiles;
    RefPtr<RenderTextureHost> mExternalImage;

    struct IdHashFn {
      std::size_t operator()(const wr::NativeSurfaceId& aId) const {
        return HashGeneric(wr::AsUint64(aId));
      }
    };
  };

  static gfx::SamplingFilter ToSamplingFilter(
      wr::ImageRendering aImageRendering);

 protected:
  // The set of surfaces added to be composited for the current frame
  struct FrameSurface {
    wr::NativeSurfaceId mId;
    gfx::Matrix4x4 mTransform;
    gfx::IntRect mClipRect;
    gfx::SamplingFilter mFilter;
  };

  virtual void HandleExternalImage(RenderTextureHost* aExternalImage,
                                   FrameSurface& aFrameSurface) = 0;
  virtual UniquePtr<RenderCompositorLayersSWGL::Surface> DoCreateSurface(
      wr::DeviceIntSize aTileSize, bool aIsOpaque);
  virtual UniquePtr<RenderCompositorLayersSWGL::Tile> DoCreateTile(
      Surface* aSurface) = 0;

  RefPtr<layers::Compositor> mCompositor;
  void* mContext = nullptr;

  std::unordered_map<wr::NativeSurfaceId, UniquePtr<Surface>, Surface::IdHashFn>
      mSurfaces;

  // Temporary state held between MapTile and UnmapTile
  Tile* mCurrentTile = nullptr;
  gfx::IntRect mCurrentTileDirty;
  wr::NativeTileId mCurrentTileId;

  nsTArray<FrameSurface> mFrameSurfaces;
  bool mInFrame = false;
  bool mCompositingStarted = false;

  layers::ScreenshotGrabber mProfilerScreenshotGrabber;
};

static inline bool operator==(const RenderCompositorLayersSWGL::TileKey& a0,
                              const RenderCompositorLayersSWGL::TileKey& a1) {
  return a0.mX == a1.mX && a0.mY == a1.mY;
}

}  // namespace wr
}  // namespace mozilla

#endif
