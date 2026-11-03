/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_LAYER_NATIVE_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_LAYER_NATIVE_H

#include <deque>
#include <unordered_map>

#include "GLTypes.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/layers/GpuFence.h"
#include "mozilla/layers/ScreenshotGrabber.h"
#include "mozilla/webrender/RenderCompositor.h"

namespace mozilla {

namespace layers {
class NativeLayerRootSnapshotter;
class NativeLayerRoot;
class NativeLayer;
class SurfacePoolHandle;
}  // namespace layers

namespace wr {

// RenderCompositorLayerNative is a skeleton class for implementing layer
// compositors backed by NativeLayer surfaces. This is not meant to be directly
// instantiated and is instead derived for various use-cases such as OpenGL.
class RenderCompositorLayerNative : public RenderCompositor {
 public:
  virtual ~RenderCompositorLayerNative();

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) final;
  void Pause() override;
  bool Resume() override;

  layers::WebRenderCompositor CompositorType() const override;

  LayoutDeviceIntSize GetBufferSize() override;

  bool ShouldUseNativeCompositor() override;
  bool ShouldUseLayerCompositor() const override;
  bool UseLayerCompositor() const override;
  bool EnableAsyncScreenshot() override;
  void GetCompositorCapabilities(CompositorCapabilities* aCaps) override;
  void GetWindowProperties(WindowProperties* aProperties) override;
  uint32_t GetMaxPartialPresentRects() override;

  bool SurfaceOriginIsTopLeft() override { return true; }

  // Does the readback for the ShouldUseNativeCompositor() case.
  bool MaybeReadback(const gfx::IntSize& aReadbackSize,
                     const wr::ImageFormat& aReadbackFormat,
                     const Range<uint8_t>& aReadbackBuffer,
                     bool* aNeedsYFlip) override;
  bool MaybeRecordFrame(layers::CompositionRecorder& aRecorder) override;
  bool MaybeGrabScreenshot(const gfx::IntSize& aWindowSize) override;
  bool MaybeProcessScreenshotQueue() override;

  void WaitUntilPresentationFlushed() override;

  // Interface for wr::Compositor
  void CompositorBeginFrame() override;
  void CompositorEndFrame() override;
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) override;
  void DestroySurface(NativeSurfaceId aId) override;
  void CreateSwapChainSurface(wr::NativeSurfaceId aId, wr::DeviceIntSize aSize,
                              bool aIsOpaque,
                              bool aNeedsSyncDcompCommit) override;
  void ResizeSwapChainSurface(wr::NativeSurfaceId aId,
                              wr::DeviceIntSize aSize) override;
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering,
                  wr::DeviceIntRect aRoundedClipRect,
                  wr::ClipRadius aClipRadius) override;

  static gfx::SamplingFilter ToSamplingFilter(
      wr::ImageRendering aImageRendering);

 protected:
  explicit RenderCompositorLayerNative(
      const RefPtr<widget::CompositorWidget>& aWidget,
      gl::GLContext* aGL = nullptr);

  virtual bool InitDefaultFramebuffer() = 0;
  virtual void DoSwap() = 0;
  virtual void DoFlush() {}

  void BindNativeLayer(wr::NativeSurfaceId aId);
  void UnbindNativeLayer();

  RefPtr<layers::NativeLayerRoot> mNativeLayerRoot;
  UniquePtr<layers::NativeLayerRootSnapshotter> mNativeLayerRootSnapshotter;
  layers::ScreenshotGrabber mProfilerScreenshotGrabber;
  RefPtr<layers::SurfacePoolHandle> mSurfacePoolHandle;

  struct Surface {
    Surface(wr::DeviceIntSize aSize, bool aIsOpaque);
    ~Surface();

    gfx::IntSize Size() const {
      return gfx::IntSize(mSize.width, mSize.height);
    }

    // External images can change size depending on which image
    // is attached, so mSize will be 0,0 when mIsExternal
    // is true.
    wr::DeviceIntSize mSize;
    bool mIsOpaque;
    bool mIsExternal = false;
    RefPtr<layers::NativeLayer> mNativeLayer;
  };

  struct SurfaceIdHashFn {
    std::size_t operator()(const wr::NativeSurfaceId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  RefPtr<layers::NativeLayer> mCurrentlyBoundNativeLayer;
  nsTArray<RefPtr<layers::NativeLayer>> mAddedLayers;
  gfx::IntRect mVisibleBounds;
  std::unordered_map<wr::NativeSurfaceId, Surface, SurfaceIdHashFn> mSurfaces;
  TimeStamp mBeginFrameTimeStamp;
  std::deque<RefPtr<layers::GpuFence>> mPendingGpuFences;
  // Used when platform does not support to take screenshot with multiple
  // layers. By GetWindowProperties(), it notifies WebRender layer manager to
  // use single layer for taking screenshot. By EnableAsyncScreenshot(), it
  // notifies if taking screenshot is ready.
  bool mEnableAsyncScreenshot = false;
  // The flag for enabling screenshot with WebRender layer compositor in next
  // composite.
  bool mEnableAsyncScreenshotInNextFrame = false;
  int mCurrentFrame = 0;
  int mAsyncScreenshotLastFrameUsed = 0;
};

// RenderCompositorLayerNativeOGL is a layer compositor that exposes an
// OpenGL framebuffer for the respective NativeLayer bound to each Surface.
class RenderCompositorLayerNativeOGL : public RenderCompositorLayerNative {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositorLayerNativeOGL(
      const RefPtr<widget::CompositorWidget>& aWidget,
      RefPtr<gl::GLContext>&& aGL);
  virtual ~RenderCompositorLayerNativeOGL();

  bool WaitForGPU() override;

  gl::GLContext* gl() const override { return mGL; }

  void BindSwapChain(wr::NativeSurfaceId aId,
                     const wr::DeviceIntRect* aDirtyRects,
                     size_t aNumDirtyRects) override;
  void PresentSwapChain(wr::NativeSurfaceId aId,
                        const wr::DeviceIntRect* aDirtyRects,
                        size_t aNumDirtyRects) override;

  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;

 protected:
  void InsertFrameDoneSync();

  bool InitDefaultFramebuffer() override;
  void DoSwap() override;
  void DoFlush() override;

  RefPtr<gl::GLContext> mGL;

  struct BackPressureFences {
    explicit BackPressureFences(
        std::deque<RefPtr<layers::GpuFence>>&& aGpuFences)
        : mGpuFences(std::move(aGpuFences)) {}

    GLsync mSync = nullptr;
    std::deque<RefPtr<layers::GpuFence>> mGpuFences;
  };

  // Used to apply back-pressure in WaitForGPU().
  UniquePtr<BackPressureFences> mPreviousFrameDoneFences;
  UniquePtr<BackPressureFences> mThisFrameDoneFences;
};

}  // namespace wr
}  // namespace mozilla

#endif
