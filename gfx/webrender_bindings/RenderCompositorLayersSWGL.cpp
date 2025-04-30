/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 *
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorLayersSWGL.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "mozilla/layers/BuildConstants.h"
#include "mozilla/layers/Effects.h"
#include "mozilla/layers/TextureHostOGL.h"
#include "mozilla/widget/CompositorWidget.h"
#include "RenderCompositorRecordedFrame.h"

#if defined(XP_WIN)
#  include "mozilla/webrender/RenderCompositorD3D11SWGL.h"
#else
#  include "mozilla/webrender/RenderCompositorOGLSWGL.h"
#endif

namespace mozilla {
using namespace layers;
using namespace gfx;

namespace wr {

UniquePtr<RenderCompositor> RenderCompositorLayersSWGL::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
#ifdef XP_WIN
  return RenderCompositorD3D11SWGL::Create(aWidget, aError);
#else
  return RenderCompositorOGLSWGL::Create(aWidget, aError);
#endif
}

RenderCompositorLayersSWGL::RenderCompositorLayersSWGL(
    Compositor* aCompositor, const RefPtr<widget::CompositorWidget>& aWidget,
    void* aContext)
    : RenderCompositor(aWidget),
      mCompositor(aCompositor),
      mContext(aContext),
      mCurrentTileId(wr::NativeTileId()) {
  MOZ_ASSERT(mCompositor);
  MOZ_ASSERT(mContext);
}

RenderCompositorLayersSWGL::~RenderCompositorLayersSWGL() {
  wr_swgl_destroy_context(mContext);
}

bool RenderCompositorLayersSWGL::MakeCurrent() {
  wr_swgl_make_current(mContext);
  return true;
}

bool RenderCompositorLayersSWGL::BeginFrame() {
  MOZ_ASSERT(!mInFrame);
  MakeCurrent();
  mInFrame = true;
  return true;
}

void RenderCompositorLayersSWGL::CancelFrame() {
  MOZ_ASSERT(mInFrame);
  mInFrame = false;
  if (mCompositingStarted) {
    mCompositor->CancelFrame();
    mCompositingStarted = false;
  }
}

void RenderCompositorLayersSWGL::StartCompositing(
    wr::ColorF aClearColor, const wr::DeviceIntRect* aDirtyRects,
    size_t aNumDirtyRects, const wr::DeviceIntRect* aOpaqueRects,
    size_t aNumOpaqueRects) {
  MOZ_RELEASE_ASSERT(!mCompositingStarted);

  if (!mInFrame || aNumDirtyRects == 0) {
    return;
  }

  gfx::IntRect bounds(gfx::IntPoint(0, 0), GetBufferSize().ToUnknownSize());
  nsIntRegion dirty;

  MOZ_RELEASE_ASSERT(aNumDirtyRects > 0);
  for (size_t i = 0; i < aNumDirtyRects; i++) {
    const auto& rect = aDirtyRects[i];
    dirty.OrWith(
        gfx::IntRect(rect.min.x, rect.min.y, rect.width(), rect.height()));
  }
  dirty.AndWith(bounds);

  nsIntRegion opaque(bounds);
  opaque.SubOut(mWidget->GetTransparentRegion().ToUnknownRegion());
  for (size_t i = 0; i < aNumOpaqueRects; i++) {
    const auto& rect = aOpaqueRects[i];
    opaque.OrWith(
        gfx::IntRect(rect.min.x, rect.min.y, rect.width(), rect.height()));
  }

  mCompositor->SetClearColor(gfx::DeviceColor(aClearColor.r, aClearColor.g,
                                              aClearColor.b, aClearColor.a));

  if (!mCompositor->BeginFrameForWindow(dirty, Nothing(), bounds, opaque)) {
    return;
  }
  mCompositingStarted = true;
}

void RenderCompositorLayersSWGL::CompositorEndFrame() {
  nsTArray<FrameSurface> frameSurfaces = std::move(mFrameSurfaces);

  if (!mCompositingStarted) {
    return;
  }

  for (auto& frameSurface : frameSurfaces) {
    auto surfaceCursor = mSurfaces.find(frameSurface.mId);
    MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
    Surface* surface = surfaceCursor->second.get();

    for (auto it = surface->mTiles.begin(); it != surface->mTiles.end(); ++it) {
      if (!it->second->IsValid()) {
        continue;
      }

      gfx::Point tileOffset(it->first.mX * surface->mTileSize.width,
                            it->first.mY * surface->mTileSize.height);
      gfx::Rect drawRect = it->second->mValidRect + tileOffset;

      RefPtr<TexturedEffect> texturedEffect =
          new EffectRGB(it->second->GetTextureSource(),
                        /* aPremultiplied */ true, frameSurface.mFilter);
      if (surface->mIsOpaque) {
        texturedEffect->mPremultipliedCopy = true;
      }

      texturedEffect->mTextureCoords =
          gfx::Rect(it->second->mValidRect.x / surface->mTileSize.width,
                    it->second->mValidRect.y / surface->mTileSize.height,
                    it->second->mValidRect.width / surface->mTileSize.width,
                    it->second->mValidRect.height / surface->mTileSize.height);

      EffectChain effect;
      effect.mPrimaryEffect = texturedEffect;
      mCompositor->DrawQuad(drawRect, frameSurface.mClipRect, effect, 1.0,
                            frameSurface.mTransform, drawRect);
    }

    if (surface->mExternalImage) {
      HandleExternalImage(surface->mExternalImage, frameSurface);
    }
  }
}

RenderedFrameId RenderCompositorLayersSWGL::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  MOZ_ASSERT(mInFrame);
  mInFrame = false;
  if (mCompositingStarted) {
    mCompositor->EndFrame();
    mCompositingStarted = false;
  }
  return GetNextRenderFrameId();
}

LayoutDeviceIntSize RenderCompositorLayersSWGL::GetBufferSize() {
  return mWidget->GetClientSize();
}

void RenderCompositorLayersSWGL::Bind(wr::NativeTileId aId,
                                      wr::DeviceIntPoint* aOffset,
                                      uint32_t* aFboId,
                                      wr::DeviceIntRect aDirtyRect,
                                      wr::DeviceIntRect aValidRect) {
  MOZ_RELEASE_ASSERT(false);
}

void RenderCompositorLayersSWGL::Unbind() { MOZ_RELEASE_ASSERT(false); }

bool RenderCompositorLayersSWGL::MapTile(wr::NativeTileId aId,
                                         wr::DeviceIntRect aDirtyRect,
                                         wr::DeviceIntRect aValidRect,
                                         void** aData, int32_t* aStride) {
  auto surfaceCursor = mSurfaces.find(aId.surface_id);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface* surface = surfaceCursor->second.get();

  auto layerCursor = surface->mTiles.find(TileKey(aId.x, aId.y));
  MOZ_RELEASE_ASSERT(layerCursor != surface->mTiles.end());

  mCurrentTile = layerCursor->second.get();
  mCurrentTileId = aId;
  mCurrentTileDirty = gfx::IntRect(aDirtyRect.min.x, aDirtyRect.min.y,
                                   aDirtyRect.width(), aDirtyRect.height());

  if (!mCurrentTile->Map(aDirtyRect, aValidRect, aData, aStride)) {
    gfxCriticalNote << "MapTile failed aValidRect: "
                    << gfx::Rect(aValidRect.min.x, aValidRect.min.y,
                                 aValidRect.width(), aValidRect.height());
    return false;
  }

  // Store the new valid rect, so that we can composite only those pixels
  mCurrentTile->mValidRect = gfx::Rect(aValidRect.min.x, aValidRect.min.y,
                                       aValidRect.width(), aValidRect.height());
  return true;
}

void RenderCompositorLayersSWGL::UnmapTile() {
  mCurrentTile->Unmap(mCurrentTileDirty);
  mCurrentTile = nullptr;
}

void RenderCompositorLayersSWGL::CreateSurface(
    wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
    wr::DeviceIntSize aTileSize, bool aIsOpaque) {
  MOZ_RELEASE_ASSERT(mSurfaces.find(aId) == mSurfaces.end());
  auto surface = DoCreateSurface(aTileSize, aIsOpaque);
  mSurfaces.insert({aId, std::move(surface)});
}

UniquePtr<RenderCompositorLayersSWGL::Surface>
RenderCompositorLayersSWGL::DoCreateSurface(wr::DeviceIntSize aTileSize,
                                            bool aIsOpaque) {
  return MakeUnique<Surface>(aTileSize, aIsOpaque);
}

void RenderCompositorLayersSWGL::CreateExternalSurface(wr::NativeSurfaceId aId,
                                                       bool aIsOpaque) {
  MOZ_RELEASE_ASSERT(mSurfaces.find(aId) == mSurfaces.end());
  auto surface = MakeUnique<Surface>(wr::DeviceIntSize{}, aIsOpaque);
  surface->mIsExternal = true;
  mSurfaces.insert({aId, std::move(surface)});
}

void RenderCompositorLayersSWGL::DestroySurface(NativeSurfaceId aId) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  mSurfaces.erase(surfaceCursor);
}

void RenderCompositorLayersSWGL::CreateTile(wr::NativeSurfaceId aId, int32_t aX,
                                            int32_t aY) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface* surface = surfaceCursor->second.get();
  MOZ_RELEASE_ASSERT(!surface->mIsExternal);

  auto tile = DoCreateTile(surface);
  surface->mTiles.insert({TileKey(aX, aY), std::move(tile)});
}

void RenderCompositorLayersSWGL::DestroyTile(wr::NativeSurfaceId aId,
                                             int32_t aX, int32_t aY) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface* surface = surfaceCursor->second.get();
  MOZ_RELEASE_ASSERT(!surface->mIsExternal);

  auto layerCursor = surface->mTiles.find(TileKey(aX, aY));
  MOZ_RELEASE_ASSERT(layerCursor != surface->mTiles.end());
  surface->mTiles.erase(layerCursor);
}

void RenderCompositorLayersSWGL::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {
  RenderTextureHost* image =
      RenderThread::Get()->GetRenderTexture(aExternalImage);
  MOZ_ASSERT(image);
  if (!image) {
    gfxCriticalNoteOnce
        << "Failed to get RenderTextureHost for D3D11SWGL extId:"
        << AsUint64(aExternalImage);
    return;
  }
#if defined(XP_WIN)
  MOZ_RELEASE_ASSERT(image->AsRenderDXGITextureHost() ||
                     image->AsRenderDXGIYCbCrTextureHost());
#elif defined(ANDROID)
  MOZ_RELEASE_ASSERT(image->AsRenderAndroidHardwareBufferTextureHost() ||
                     image->AsRenderAndroidSurfaceTextureHost() ||
                     image->AsRenderEGLImageTextureHost());
#endif

  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());

  Surface* surface = surfaceCursor->second.get();
  surface->mExternalImage = image;
  MOZ_RELEASE_ASSERT(surface->mTiles.empty());
  MOZ_RELEASE_ASSERT(surface->mIsExternal);
}

// static
gfx::SamplingFilter RenderCompositorLayersSWGL::ToSamplingFilter(
    wr::ImageRendering aImageRendering) {
  if (aImageRendering == wr::ImageRendering::Auto) {
    return gfx::SamplingFilter::LINEAR;
  }
  return gfx::SamplingFilter::POINT;
}

void RenderCompositorLayersSWGL::AddSurface(
    wr::NativeSurfaceId aId, const wr::CompositorSurfaceTransform& aTransform,
    wr::DeviceIntRect aClipRect, wr::ImageRendering aImageRendering,
    wr::DeviceIntRect aRoundedClipRect, wr::ClipRadius aClipRadius) {
  float sx = aTransform.scale.x;
  float sy = aTransform.scale.y;
  float tx = aTransform.offset.x;
  float ty = aTransform.offset.y;
  gfx::Matrix4x4 transform(sx, 0.0, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 0.0, 1.0,
                           0.0, tx, ty, 0.0, 1.0);
  gfx::IntRect clipRect(aClipRect.min.x, aClipRect.min.y, aClipRect.width(),
                        aClipRect.height());

  mFrameSurfaces.AppendElement(FrameSurface{aId, transform, clipRect,
                                            ToSamplingFilter(aImageRendering)});
}

void RenderCompositorLayersSWGL::MaybeRequestAllowFrameRecording(
    bool aWillRecord) {
  mCompositor->RequestAllowFrameRecording(aWillRecord);
}

class WindowLMC : public profiler_screenshots::Window {
 public:
  explicit WindowLMC(Compositor* aCompositor) : mCompositor(aCompositor) {}

  already_AddRefed<profiler_screenshots::RenderSource> GetWindowContents(
      const gfx::IntSize& aWindowSize) override;
  already_AddRefed<profiler_screenshots::DownscaleTarget> CreateDownscaleTarget(
      const gfx::IntSize& aSize) override;
  already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
  CreateAsyncReadbackBuffer(const gfx::IntSize& aSize) override;

 protected:
  Compositor* mCompositor;
};

class RenderSourceLMC : public profiler_screenshots::RenderSource {
 public:
  explicit RenderSourceLMC(CompositingRenderTarget* aRT)
      : RenderSource(aRT->GetSize()), mRT(aRT) {}

  const auto& RenderTarget() { return mRT; }

 protected:
  virtual ~RenderSourceLMC() {}

  RefPtr<CompositingRenderTarget> mRT;
};

class DownscaleTargetLMC : public profiler_screenshots::DownscaleTarget {
 public:
  explicit DownscaleTargetLMC(CompositingRenderTarget* aRT,
                              Compositor* aCompositor)
      : profiler_screenshots::DownscaleTarget(aRT->GetSize()),
        mRenderSource(new RenderSourceLMC(aRT)),
        mCompositor(aCompositor) {}

  already_AddRefed<profiler_screenshots::RenderSource> AsRenderSource()
      override {
    return do_AddRef(mRenderSource);
  }

  bool DownscaleFrom(profiler_screenshots::RenderSource* aSource,
                     const IntRect& aSourceRect,
                     const IntRect& aDestRect) override {
    MOZ_RELEASE_ASSERT(aSourceRect.TopLeft() == IntPoint());
    MOZ_RELEASE_ASSERT(aDestRect.TopLeft() == IntPoint());
    RefPtr<CompositingRenderTarget> previousTarget =
        mCompositor->GetCurrentRenderTarget();

    mCompositor->SetRenderTarget(mRenderSource->RenderTarget());
    bool result = mCompositor->BlitRenderTarget(
        static_cast<RenderSourceLMC*>(aSource)->RenderTarget(),
        aSourceRect.Size(), aDestRect.Size());

    // Restore the old render target.
    mCompositor->SetRenderTarget(previousTarget);

    return result;
  }

 protected:
  virtual ~DownscaleTargetLMC() {}

  RefPtr<RenderSourceLMC> mRenderSource;
  Compositor* mCompositor;
};

class AsyncReadbackBufferLMC
    : public profiler_screenshots::AsyncReadbackBuffer {
 public:
  AsyncReadbackBufferLMC(mozilla::layers::AsyncReadbackBuffer* aARB,
                         Compositor* aCompositor)
      : profiler_screenshots::AsyncReadbackBuffer(aARB->GetSize()),
        mARB(aARB),
        mCompositor(aCompositor) {}
  void CopyFrom(profiler_screenshots::RenderSource* aSource) override {
    mCompositor->ReadbackRenderTarget(
        static_cast<RenderSourceLMC*>(aSource)->RenderTarget(), mARB);
  }
  bool MapAndCopyInto(DataSourceSurface* aSurface,
                      const IntSize& aReadSize) override {
    return mARB->MapAndCopyInto(aSurface, aReadSize);
  }

 protected:
  virtual ~AsyncReadbackBufferLMC() {}

  RefPtr<mozilla::layers::AsyncReadbackBuffer> mARB;
  Compositor* mCompositor;
};

already_AddRefed<profiler_screenshots::RenderSource>
WindowLMC::GetWindowContents(const gfx::IntSize& aWindowSize) {
  RefPtr<CompositingRenderTarget> rt = mCompositor->GetWindowRenderTarget();
  if (!rt) {
    return nullptr;
  }
  return MakeAndAddRef<RenderSourceLMC>(rt);
}

already_AddRefed<profiler_screenshots::DownscaleTarget>
WindowLMC::CreateDownscaleTarget(const gfx::IntSize& aSize) {
  RefPtr<CompositingRenderTarget> rt =
      mCompositor->CreateRenderTarget(IntRect({}, aSize), INIT_MODE_NONE);
  return MakeAndAddRef<DownscaleTargetLMC>(rt, mCompositor);
}

already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
WindowLMC::CreateAsyncReadbackBuffer(const gfx::IntSize& aSize) {
  RefPtr<AsyncReadbackBuffer> carb =
      mCompositor->CreateAsyncReadbackBuffer(aSize);
  if (!carb) {
    return nullptr;
  }
  return MakeAndAddRef<AsyncReadbackBufferLMC>(carb, mCompositor);
}

bool RenderCompositorLayersSWGL::MaybeRecordFrame(
    layers::CompositionRecorder& aRecorder) {
  WindowLMC window(mCompositor);
  gfx::IntSize size = GetBufferSize().ToUnknownSize();
  RefPtr<layers::profiler_screenshots::RenderSource> snapshot =
      window.GetWindowContents(size);
  if (!snapshot) {
    return true;
  }

  RefPtr<layers::profiler_screenshots::AsyncReadbackBuffer> buffer =
      window.CreateAsyncReadbackBuffer(size);
  buffer->CopyFrom(snapshot);

  RefPtr<layers::RecordedFrame> frame =
      new RenderCompositorRecordedFrame(TimeStamp::Now(), std::move(buffer));
  aRecorder.RecordFrame(frame);
  return false;
}

bool RenderCompositorLayersSWGL::MaybeGrabScreenshot(
    const gfx::IntSize& aWindowSize) {
  if (!mCompositingStarted) {
    return true;
  }
  WindowLMC window(mCompositor);
  mProfilerScreenshotGrabber.MaybeGrabScreenshot(window, aWindowSize);
  return true;
}

bool RenderCompositorLayersSWGL::MaybeProcessScreenshotQueue() {
  mProfilerScreenshotGrabber.MaybeProcessQueue();
  return true;
}

}  // namespace wr
}  // namespace mozilla
