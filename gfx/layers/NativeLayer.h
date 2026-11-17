/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayer_h
#define mozilla_layers_NativeLayer_h

#include "GLTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/Range.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/GpuFence.h"
#include "mozilla/layers/ScreenshotGrabber.h"
#include "nsISupportsImpl.h"
#include "nsRegion.h"

namespace mozilla {

namespace gl {
class GLContext;
class MozFramebuffer;
}  // namespace gl

namespace wr {
class RenderTextureHost;
}

namespace layers {

class GpuFence;
class NativeLayer;
class NativeLayerCA;
class NativeLayerRemoteMac;
class NativeLayerRemoteWayland;
class NativeLayerRootCA;
class NativeLayerRootWayland;
class NativeLayerRootSnapshotter;
class NativeLayerWayland;
class SurfacePoolHandle;

// NativeLayerRoot and NativeLayer allow building up a flat layer "tree" of
// sibling layers. These layers provide a cross-platform abstraction for the
// platform's native layers, such as CoreAnimation layers on macOS.
// Every layer has a rectangle that describes its position and size in the
// window. The native layer root is usually be created by the window, and then
// the compositing subsystem uses it to create and place the actual layers.
class NativeLayerRoot {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NativeLayerRoot)

  virtual NativeLayerRootCA* AsNativeLayerRootCA() { return nullptr; }
  virtual NativeLayerRootWayland* AsNativeLayerRootWayland() { return nullptr; }

  virtual already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) = 0;
  virtual already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) = 0;
  virtual already_AddRefed<NativeLayer> CreateLayerForColor(
      gfx::DeviceColor aColor) {
    return nullptr;
  }

  // A layer creation method for when the layer will be handed surfaces
  // directly.
  virtual already_AddRefed<NativeLayer> CreateLayerForSurfacePresentation(
      const gfx::IntSize& aSize, bool aIsOpaque) {
    return nullptr;
  }

  virtual void LayerDestroyed(NativeLayer* aLayer) {}

  virtual void AppendLayer(NativeLayer* aLayer) = 0;
  virtual void RemoveLayer(NativeLayer* aLayer) = 0;
  virtual void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) = 0;

  // Called before any layer content changes
  virtual void PrepareForCommit() {}

  // Publish the layer changes to the screen. Returns whether the commit was
  // successful.
  virtual bool CommitToScreen() = 0;

  // When called on a remote instance, synchronously wait until the other side
  // has processed any previous commits.
  virtual void WaitUntilCommitToScreenHasBeenProcessed() {}

  // Returns a new NativeLayerRootSnapshotter that can be used to read back the
  // visual output of this NativeLayerRoot. The snapshotter needs to be
  // destroyed on the same thread that CreateSnapshotter() was called on. Only
  // one snapshotter per NativeLayerRoot can be in existence at any given time.
  // CreateSnapshotter() makes sure of this and crashes if called at a time at
  // which there still exists a snapshotter for this NativeLayerRoot.
  virtual UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() {
    return nullptr;
  }

 protected:
  virtual ~NativeLayerRoot() = default;
};

// Allows reading back the visual output of a NativeLayerRoot.
// Can only be used on a single thread, unlike NativeLayerRoot.
// Holds a strong reference to the NativeLayerRoot that created it.
// On Mac, this owns a GLContext, which wants to be created and destroyed on the
// same thread.
class NativeLayerRootSnapshotter : public profiler_screenshots::Window {
 public:
  virtual ~NativeLayerRootSnapshotter() = default;

  // Reads the composited result of the NativeLayer tree into aReadbackBuffer,
  // synchronously. Should only be called right after a call to CommitToScreen()
  // - in that case it is guaranteed to read back exactly the NativeLayer state
  // that was committed. If called at other times, this API does not define
  // whether the observed state includes NativeLayer modifications which have
  // not been committed. (The macOS implementation will include those pending
  // modifications by doing an offscreen commit.)
  // The readback buffer's stride is assumed to be aReadbackSize.width * 4. Only
  // BGRA is supported.
  virtual bool ReadbackPixels(
      const gfx::IntSize& aReadbackSize, gfx::SurfaceFormat aReadbackFormat,
      const mozilla::Range<uint8_t>& aReadbackBuffer) = 0;
};

// Represents a native layer. Native layers, such as CoreAnimation layers on
// macOS, are used to put pixels on the screen and to refresh and manipulate
// the visual contents of a window efficiently. For example, drawing to a layer
// once and then displaying the layer for multiple frames while moving it to
// different positions will be more efficient than drawing into a window (or a
// non-moving layer) multiple times with different internal offsets.
// There are two sources of "work" for a given composited frame: 1) Our own
// drawing (such as OpenGL compositing into a window or layer) and 2) the
// compositing window manager's work to update the screen. Every pixel we draw
// needs to be copied to the screen by the window manager. This suggests two
// avenues for reducing the work load for a given frame: Drawing fewer pixels
// ourselves, and making the window manager copy fewer pixels to the screen.
// Smart use of native layers allows reducing both work loads: If a visual
// change can be expressed purely as a layer attribute change (such as a change
// in the layer's position), this lets us eliminate our own drawing for that
// change. And secondly, manipulating a small layer rather than a large layer
// will reduce the window manager's work for that frame because it'll only copy
// the pixels of the small layer to the screen.
class NativeLayer {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NativeLayer)

  virtual NativeLayerCA* AsNativeLayerCA() { return nullptr; }
  virtual NativeLayerRemoteMac* AsNativeLayerRemoteMac() { return nullptr; }
  virtual NativeLayerRemoteWayland* AsNativeLayerRemoteWayland() {
    return nullptr;
  }
  virtual NativeLayerWayland* AsNativeLayerWayland() { return nullptr; }

  // The size and opaqueness of a layer are supplied during layer creation and
  // never change.
  virtual gfx::IntSize GetSize() = 0;
  virtual bool IsOpaque() = 0;

  // The location of the layer, in integer device pixels.
  // This is applied to the layer, before the transform is applied.
  virtual void SetPosition(const gfx::IntPoint& aPosition) = 0;
  virtual gfx::IntPoint GetPosition() = 0;

  // Sets a transformation to apply to the Layer. This gets applied to
  // coordinates with the position applied, but before clipping is
  // applied.
  virtual void SetTransform(const gfx::Matrix4x4& aTransform) = 0;
  virtual gfx::Matrix4x4 GetTransform() = 0;

  virtual gfx::IntRect GetRect() = 0;

  // Set an optional clip rect on the layer. The clip rect is in post-transform
  // coordinate space
  virtual void SetClipRect(const Maybe<gfx::IntRect>& aClipRect) = 0;
  virtual Maybe<gfx::IntRect> ClipRect() = 0;

  // Set an optional rounded clip rect on the layer. The clip rect is in
  // post-transform coordinate space
  virtual void SetRoundedClipRect(const Maybe<gfx::RoundedRect>& aClip) = 0;
  virtual Maybe<gfx::RoundedRect> RoundedClipRect() = 0;

  // Returns the "display rect", in content coordinates, of the current front
  // surface. This rect acts as an extra clip and prevents invalid content from
  // getting to the screen. The display rect starts out empty before the first
  // call to NextSurface*. Note the different coordinate space from the regular
  // clip rect: the clip rect is "outside" the layer position, the display rect
  // is "inside" the layer position (moves with the layer).
  virtual gfx::IntRect CurrentSurfaceDisplayRect() = 0;

  // Whether the surface contents are flipped vertically compared to this
  // layer's coordinate system. Can be set on any thread at any time.
  virtual void SetSurfaceIsFlipped(bool aIsFlipped) = 0;
  virtual bool SurfaceIsFlipped() = 0;

  virtual void SetSamplingFilter(gfx::SamplingFilter aSamplingFilter) = 0;
  virtual gfx::SamplingFilter SamplingFilter() {
    return gfx::SamplingFilter::POINT;
  };

  // Returns a DrawTarget. The size of the DrawTarget will be the same as the
  // size of this layer. The caller should draw to that DrawTarget, then drop
  // its reference to the DrawTarget, and then call NotifySurfaceReady(). It can
  // limit its drawing to aUpdateRegion (which is in the DrawTarget's device
  // space). After a call to NextSurface*, NextSurface* must not be called again
  // until after NotifySurfaceReady has been called. Can be called on any
  // thread. When used from multiple threads, callers need to make sure that
  // they still only call NextSurface* and NotifySurfaceReady alternatingly and
  // not in any other order. aUpdateRegion and aDisplayRect are in "content
  // coordinates" and must not extend beyond the layer size. If aDisplayRect
  // contains parts that were not valid before, then those parts must be updated
  // (must be part of aUpdateRegion), so that the entirety of aDisplayRect is
  // valid after the update. The display rect determines the parts of the
  // surface that will be shown; this allows using surfaces with only
  // partially-valid content, as long as none of the invalid content is included
  // in the display rect.
  virtual RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) = 0;

  // Returns a GLuint for a framebuffer that can be used for drawing to the
  // surface. The size of the framebuffer will be the same as the size of this
  // layer. If aNeedsDepth is true, the framebuffer is created with a depth
  // buffer.
  // The framebuffer's depth buffer (if present) may be shared with other
  // framebuffers of the same size, even from entirely different NativeLayer
  // objects. The caller should not assume anything about the depth buffer's
  // existing contents (i.e. it should clear it at the beginning of the draw).
  // Callers should draw to one layer at a time, such that there is no
  // interleaved drawing to different framebuffers that could be tripped up by
  // the sharing.
  // The caller should draw to the framebuffer, unbind it, and then call
  // NotifySurfaceReady(). It can limit its drawing to aUpdateRegion (which is
  // in the framebuffer's device space, possibly "upside down" if
  // SurfaceIsFlipped()).
  // The framebuffer will be created in the GLContext that this layer's
  // SurfacePoolHandle was created for.
  // After a call to NextSurface*, NextSurface* must not be called again until
  // after NotifySurfaceReady has been called. Can be called on any thread. When
  // used from multiple threads, callers need to make sure that they still only
  // call NextSurface and NotifySurfaceReady alternatingly and not in any other
  // order.
  // aUpdateRegion and aDisplayRect are in "content coordinates" and must not
  // extend beyond the layer size. If aDisplayRect contains parts that were not
  // valid before, then those parts must be updated (must be part of
  // aUpdateRegion), so that the entirety of aDisplayRect is valid after the
  // update. The display rect determines the parts of the surface that will be
  // shown; this allows using surfaces with only partially-valid content, as
  // long as none of the invalid content is included in the display rect.
  virtual Maybe<GLuint> NextSurfaceAsFramebuffer(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      bool aNeedsDepth) = 0;

  // Indicates that the surface which has been returned from the most recent
  // call to NextSurface* is now finished being drawn to and can be displayed on
  // the screen. Resets the invalid region on the surface to the empty region.
  virtual void NotifySurfaceReady() = 0;

  // If you know that this layer will likely not draw any more frames, then it's
  // good to call DiscardBackbuffers in order to save memory and allow other
  // layer's to pick up the released surfaces from the pool.
  virtual void DiscardBackbuffers() = 0;

  virtual void AttachExternalImage(wr::RenderTextureHost* aExternalImage) = 0;

  virtual RefPtr<GpuFence> GetGpuFence() = 0;

 protected:
  virtual ~NativeLayer() = default;
};

// Utility classes for NativeLayerRootSnapshotter (NLRS) profiler screenshots.

class RenderSourceNLRS : public profiler_screenshots::RenderSource {
 public:
  explicit RenderSourceNLRS(UniquePtr<gl::MozFramebuffer>&& aFramebuffer);
  auto& FB() { return *mFramebuffer; }
  ~RenderSourceNLRS() override;

 protected:
  UniquePtr<gl::MozFramebuffer> mFramebuffer;
};

class DownscaleTargetNLRS : public profiler_screenshots::DownscaleTarget {
 public:
  DownscaleTargetNLRS(gl::GLContext* aGL,
                      UniquePtr<gl::MozFramebuffer>&& aFramebuffer);
  already_AddRefed<profiler_screenshots::RenderSource> AsRenderSource()
      override {
    return do_AddRef(mRenderSource);
  };
  bool DownscaleFrom(profiler_screenshots::RenderSource* aSource,
                     const gfx::IntRect& aSourceRect,
                     const gfx::IntRect& aDestRect) override;

 protected:
  RefPtr<gl::GLContext> mGL;
  RefPtr<RenderSourceNLRS> mRenderSource;
};

class AsyncReadbackBufferNLRS
    : public profiler_screenshots::AsyncReadbackBuffer {
 public:
  AsyncReadbackBufferNLRS(gl::GLContext* aGL, const gfx::IntSize& aSize,
                          GLuint aBufferHandle, bool aYFlip);
  void CopyFrom(profiler_screenshots::RenderSource* aSource) override;
  bool MapAndCopyInto(gfx::DataSourceSurface* aSurface,
                      const gfx::IntSize& aReadSize) override;

 protected:
  virtual ~AsyncReadbackBufferNLRS();
  RefPtr<gl::GLContext> mGL;
  GLuint mBufferHandle = 0;
  bool mYFlip;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayer_h
