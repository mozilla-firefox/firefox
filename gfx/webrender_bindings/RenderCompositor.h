/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_H

#include "GLContextTypes.h"
#include "GLTypes.h"
#include "Units.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/Fence.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {

namespace gl {
class GLContext;
#ifdef MOZ_WIDGET_ANDROID
class GLContextEGL;
#endif
}  // namespace gl

namespace layers {
class AndroidHardwareBuffer;
class CompositionRecorder;
class SyncObjectHost;
}  // namespace layers

namespace widget {
class CompositorWidget;
}

namespace wr {

class RenderCompositorLayersSWGL;
class RenderCompositorD3D11SWGL;
class RenderTextureHost;

class RenderCompositor {
 public:
  static UniquePtr<RenderCompositor> Create(
      const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError);

  RenderCompositor(const RefPtr<widget::CompositorWidget>& aWidget);
  virtual ~RenderCompositor();

  virtual bool BeginFrame() = 0;

  // Optional handler in case the frame was aborted allowing the compositor
  // to clean up relevant resources if required.
  virtual void CancelFrame() {}

  // Called to notify the RenderCompositor that all of the commands for a frame
  // have been pushed to the queue.
  // @return a RenderedFrameId for the frame
  virtual RenderedFrameId EndFrame(
      const nsTArray<DeviceIntRect>& aDirtyRects) = 0;
  // Returns false when waiting gpu tasks is failed.
  // It might happen when rendering context is lost.
  virtual bool WaitForGPU() { return true; }

  // On platforms where putting the frame onto the screen involves work in other
  // processes, wait until those other processes have completed that work.
  // Specifically, on macOS, we have to send surfaces to the parent process and
  // it will put them into CALayers, and we want to wait until that's done.
  virtual void WaitUntilPresentationFlushed() {}

  // Check for and return the last completed frame.
  // @return the last (highest) completed RenderedFrameId
  virtual RenderedFrameId GetLastCompletedFrameId() {
    return mLatestRenderFrameId.Prev();
  }

  // Update FrameId when WR rendering does not happen.
  virtual RenderedFrameId UpdateFrameId() { return GetNextRenderFrameId(); }

  virtual void Pause() = 0;
  virtual bool Resume() = 0;
  // Called when WR rendering is skipped
  virtual void Update() {}

  virtual gl::GLContext* gl() const { return nullptr; }
  virtual void* swgl() const { return nullptr; }

  virtual bool MakeCurrent();

  virtual bool UseANGLE() const { return false; }

  virtual bool UseDComp() const { return false; }

  virtual bool UseTripleBuffering() const { return false; }

  virtual layers::WebRenderBackend BackendType() const {
    return layers::WebRenderBackend::HARDWARE;
  }
  virtual layers::WebRenderCompositor CompositorType() const {
    return layers::WebRenderCompositor::DRAW;
  }

  virtual RenderCompositorD3D11SWGL* AsRenderCompositorD3D11SWGL() {
    return nullptr;
  }

  virtual RenderCompositorLayersSWGL* AsRenderCompositorLayersSWGL() {
    return nullptr;
  }

  // True if AttachExternalImage supports being used with an external
  // image that maps to a RenderBufferTextureHost
  virtual bool SupportsExternalBufferTextures() const {
#ifdef MOZ_WAYLAND
    return true;
#else
    return false;
#endif
  }

  virtual LayoutDeviceIntSize GetBufferSize() = 0;

  widget::CompositorWidget* GetWidget() const { return mWidget; }

  layers::SyncObjectHost* GetSyncObject() const { return mSyncObject.get(); }

  virtual gfx::DeviceResetReason IsContextLost(bool aForce);

  virtual bool SupportAsyncScreenshot() { return true; }

  virtual bool ShouldUseNativeCompositor() { return false; }

  virtual bool ShouldUseLayerCompositor() const { return false; }

  virtual bool UseLayerCompositor() const { return false; }

  // Request RenderCompositor to enable taking screenshot
  // In WebRender layer compositor case, taking screenshot will be ready in at
  // most one WebRender composition.
  //
  // @return true if taking screenshot is ready.
  virtual bool EnableAsyncScreenshot() { return false; }

  // Interface for wr::Compositor
  virtual void CompositorBeginFrame() {}
  virtual void CompositorEndFrame() {}
  virtual void Bind(wr::NativeTileId aId, wr::DeviceIntPoint* aOffset,
                    uint64_t* aSurfaceHandle, wr::DeviceIntRect aDirtyRect,
                    wr::DeviceIntRect aValidRect) {}
  virtual void Unbind() {}
  virtual bool MapTile(wr::NativeTileId aId, wr::DeviceIntRect aDirtyRect,
                       wr::DeviceIntRect aValidRect, void** aData,
                       int32_t* aStride) {
    return false;
  }
  virtual void UnmapTile() {}
  virtual void CreateSurface(wr::NativeSurfaceId aId,
                             wr::DeviceIntSize aTileSize, bool aIsOpaque) {}
  virtual void CreateSwapChainSurface(wr::NativeSurfaceId aId,
                                      wr::DeviceIntSize aSize, bool aIsOpaque,
                                      bool aNeedsSyncDcompCommit) {}
  virtual void ResizeSwapChainSurface(wr::NativeSurfaceId aId,
                                      wr::DeviceIntSize aSize) {}
  // Without OS layers, the only swapchain is the window and the dirty rects
  // are the region WebRender is about to render to.
  virtual void BindSwapChain(wr::NativeSurfaceId aId,
                             const wr::DeviceIntRect* aDirtyRects,
                             size_t aNumDirtyRects) {
    if (aNumDirtyRects > 0) {
      SetBufferDamageRegion(aDirtyRects, aNumDirtyRects);
    }
  }
  virtual void PresentSwapChain(wr::NativeSurfaceId aId,
                                const wr::DeviceIntRect* aDirtyRects,
                                size_t aNumDirtyRects) {}
  virtual void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) {}
  virtual void CreateBackdropSurface(wr::NativeSurfaceId aId,
                                     wr::ColorF aColor) {}
  virtual void DestroySurface(NativeSurfaceId aId) {}
  virtual void CreateTile(wr::NativeSurfaceId, int32_t aX, int32_t aY) {}
  virtual void DestroyTile(wr::NativeSurfaceId, int32_t aX, int32_t aY) {}
  virtual void AttachExternalImage(wr::NativeSurfaceId aId,
                                   wr::ExternalImageId aExternalImage) {}
  virtual void AddSurface(wr::NativeSurfaceId aId,
                          const wr::CompositorSurfaceTransform& aTransform,
                          wr::DeviceIntRect aClipRect,
                          wr::ImageRendering aImageRendering,
                          wr::DeviceIntRect aRoundedClipRect,
                          wr::ClipRadius aClipRadius) {}
  // Called in the middle of a frame after all surfaces have been added but
  // before tiles are updated to signal that early compositing can start
  virtual void StartCompositing(wr::ColorF aClearColor,
                                const wr::DeviceIntRect* aDirtyRects,
                                size_t aNumDirtyRects,
                                const wr::DeviceIntRect* aOpaqueRects,
                                size_t aNumOpaqueRects) {}
  virtual void DeInit() {}
  // Overrides any of the default compositor capabilities for behavior this
  // compositor might require.
  virtual void GetCompositorCapabilities(CompositorCapabilities* aCaps);

  virtual void GetWindowVisibility(WindowVisibility* aVisibility);

  // Called from WebRender Renderer::composite_simple().
  // WindowProperties is used to control how to composite with WebRender layer
  // compositor.
  //
  // @param WindowProperties::is_opaque Notify if rendering window is opaque.
  // WebRender might use this to optimize layer allocation.
  // @param WindowProperties::enable_screenshot Requests to WebRender to use
  // only one content layer during composite for taking screenshot except debug
  // layer.
  virtual void GetWindowProperties(WindowProperties* aProperties);

  // Interface for partial present
  // The maximum number of dirty rects WebRender may pass when presenting a
  // swapchain, whether it is the window or an OS layer. Zero disables partial
  // present.
  virtual uint32_t GetMaxPartialPresentRects() { return 0; }
  // Whether the window's own swapchain is partially presented.
  virtual bool UseWindowPartialPresent() { return false; }
  virtual bool RequestFullRender() { return false; }
  // Returns the number of frames since the window's current back buffer was
  // last presented. WebRender uses it to determine the region which must be
  // rendered in addition to the current frame's dirty rect. 1 means that the
  // back buffer holds the previous frame. 0 means that its content is unknown,
  // which forces a full render.
  virtual size_t GetBufferAge() const { return 0; }
  // Allows webrender to specify the total region that will be rendered to this
  // frame, ie the frame's dirty region and some previous frames' dirty regions,
  // if applicable (calculated using the buffer age). Must be called before
  // anything has been rendered to the main framebuffer.
  virtual void SetBufferDamageRegion(const wr::DeviceIntRect* aRects,
                                     size_t aNumRects) {}

  // Whether the surface origin is top-left.
  virtual bool SurfaceOriginIsTopLeft() { return false; }

  // Does readback if wr_renderer_readback() could not get correct WR rendered
  // result. It could happen when WebRender renders to multiple overlay layers.
  virtual bool MaybeReadback(const gfx::IntSize& aReadbackSize,
                             const wr::ImageFormat& aReadbackFormat,
                             const Range<uint8_t>& aReadbackBuffer,
                             bool* aNeedsYFlip) {
    return false;
  }
  virtual void MaybeRequestAllowFrameRecording(bool aWillRecord) {}
  virtual bool MaybeRecordFrame(layers::CompositionRecorder& aRecorder) {
    return false;
  }
  virtual bool MaybeGrabScreenshot(const gfx::IntSize& aWindowSize) {
    return false;
  }
  virtual bool MaybeProcessScreenshotQueue() { return false; }
#ifdef MOZ_WIDGET_ANDROID
  // Copies aSourceRect of the most recently rendered frame into
  // aHardwareBuffer, flipping and scaling as required. Returns whether the
  // capture succeeded.
  virtual bool CaptureScreenPixels(
      const gfx::IntRect& aSourceRect,
      layers::AndroidHardwareBuffer* aHardwareBuffer) {
    return false;
  }
#endif

  virtual RefPtr<layers::Fence> GetAndResetReadFence() { return nullptr; }

  virtual bool IsPaused() { return false; }

  virtual void MaybeWaitingForPendingReadFence(RenderTextureHost* aTexture) {}

 protected:
  // We default this to 2, so that mLatestRenderFrameId.Prev() is always valid.
  RenderedFrameId mLatestRenderFrameId = RenderedFrameId{2};
  RenderedFrameId GetNextRenderFrameId() {
    mLatestRenderFrameId = mLatestRenderFrameId.Next();
    return mLatestRenderFrameId;
  }

#ifdef MOZ_WIDGET_ANDROID
  // Copies aSourceRect from aGL's default framebuffer into aHardwareBuffer,
  // flipping and scaling as required. Returns whether the capture succeeded.
  static bool CaptureScreenPixelsFromGL(
      gl::GLContextEGL* aGL, const gfx::IntRect& aSourceRect,
      const LayoutDeviceIntSize& aFramebufferSize,
      gl::OriginPos aFramebufferOrigin,
      layers::AndroidHardwareBuffer* aHardwareBuffer);
#endif

  RefPtr<widget::CompositorWidget> mWidget;
  RefPtr<layers::SyncObjectHost> mSyncObject;
};

}  // namespace wr
}  // namespace mozilla

#endif
