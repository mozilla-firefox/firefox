/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerWaylandSurfaceHandler_h
#define mozilla_layers_NativeLayerWaylandSurfaceHandler_h

#include "GLTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "nsISupportsImpl.h"
#include "nsRegion.h"

namespace mozilla {

namespace gl {
class GLContext;
}  // namespace gl

namespace wr {
class RenderTextureHost;
}  // namespace wr

namespace widget {
class WaylandSurfaceLock;
}  // namespace widget

namespace layers {
class SurfacePoolHandleWayland;

// A companion to Wayland-specific subclasses of NativeLayer, this class
// handles the implementation of the surface management calls. The
// expectation is that NativeLayerWaylandSurfaceHandler is composed into
// these classes, rather than be used as a superclass.
// Methods taking a WaylandSurfaceLock pointer accept null when the buffers
// are presented by another process, in which case buffer usage is tracked
// with the DMABuf global reference counter.
class NativeLayerWaylandSurfaceHandler {
 public:
  NativeLayerWaylandSurfaceHandler(
      const gfx::IntSize& aSize, SurfacePoolHandleWayland* aSurfacePoolHandle);

  gfx::IntSize Size() { return mSize; }

  // Returns the "display rect", in content coordinates, of the current front
  // surface. This rect acts as an extra clip and prevents invalid content from
  // getting to the screen. The display rect starts out empty before the first
  // call to NextSurface*. Note the different coordinate space from the regular
  // clip rect: the clip rect is "outside" the layer position, the display rect
  // is "inside" the layer position (moves with the layer).
  gfx::IntRect DisplayRect() { return mDisplayRect; }
  gfx::IntRegion DirtyRegion() { return mDirtyRegion; }
  RefPtr<widget::WaylandBuffer> FrontBuffer() { return mFrontBuffer; }

  void SetSurfaceIsFlipped(bool aIsFlipped) { mSurfaceIsFlipped = aIsFlipped; }
  bool SurfaceIsFlipped() { return mSurfaceIsFlipped; }

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
  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const widget::WaylandSurfaceLock* aProofOfLock,
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType, RefPtr<widget::DRMFormat> aDRMFormat);

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
  Maybe<GLuint> NextSurfaceAsFramebuffer(
      const widget::WaylandSurfaceLock* aProofOfLock,
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      bool aNeedsDepth, RefPtr<widget::DRMFormat> aDRMFormat);

  // Indicates that the surface which has been returned from the most recent
  // call to NextSurface* is now finished being drawn to and can be displayed on
  // the screen. Resets the invalid region on the surface to the empty region.
  // Returns true if the display rect has changed.
  void NotifySurfaceReady(const widget::WaylandSurfaceLock* aProofOfLock);
  bool IsFrontBufferChanged();

  void CopyFrontBufferToFrameBuffer(
      const widget::WaylandSurfaceLock* aProofOfLock, GLuint aFB);
  gl::GLContext* gl();

 protected:
  friend class NativeLayerWaylandRender;

  void DiscardBackbuffersLocked(const widget::WaylandSurfaceLock* aProofOfLock,
                                bool aForce);
  void ReadBackFrontBuffer(const widget::WaylandSurfaceLock* aProofOfLock);
  bool CommitFrontBufferToScreenLocked(
      const widget::WaylandSurfaceLock* aProofOfLock);

  gfx::IntSize mSize;
  gfx::IntRect mDisplayRect;
  bool mSurfaceIsFlipped = false;

  const RefPtr<SurfacePoolHandleWayland> mSurfacePoolHandle;
  RefPtr<widget::WaylandBuffer> mInProgressBuffer;
  RefPtr<widget::WaylandBuffer> mFrontBuffer;
  gfx::IntRegion mDirtyRegion;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerWaylandSurfaceHandler_h
