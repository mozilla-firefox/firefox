/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerRemoteWayland_h
#define mozilla_layers_NativeLayerRemoteWayland_h

#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/NativeLayerCommandQueue.h"
#include "mozilla/layers/NativeLayerWaylandSurfaceHandler.h"
#include "nsRegion.h"

namespace mozilla {

namespace widget {
class DRMFormat;
class WaylandBuffer;
}  // namespace widget

namespace layers {

class SurfacePoolHandleWayland;

// NativeLayerRemoteWayland is a Wayland-specific NativeLayer offered up by
// NativeLayerRootRemoteWaylandChild, which can be rendered into and then sent
// to the NativeLayerRootRemoteWaylandParent. It runs in the GPU process, which
// has no connection to the Wayland display, so it renders into DMABuf buffers
// which are presented by the parent process.
class NativeLayerRemoteWayland final : public NativeLayer {
  friend class NativeLayerRootRemoteWaylandChild;

 public:
  // Overridden methods
  NativeLayerRemoteWayland* AsNativeLayerRemoteWayland() override {
    return this;
  }

  gfx::IntSize GetSize() override;
  void SetPosition(const gfx::IntPoint& aPosition) override;
  gfx::IntPoint GetPosition() override;
  void SetTransform(const gfx::Matrix4x4& aTransform) override;
  gfx::Matrix4x4 GetTransform() override;
  gfx::IntRect GetRect() override;
  void SetSamplingFilter(gfx::SamplingFilter aSamplingFilter) override;
  gfx::SamplingFilter SamplingFilter() override;
  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) override;
  Maybe<GLuint> NextSurfaceAsFramebuffer(const gfx::IntRect& aDisplayRect,
                                         const gfx::IntRegion& aUpdateRegion,
                                         bool aNeedsDepth) override;

  void NotifySurfaceReady() override;
  void DiscardBackbuffers() override;

  bool IsOpaque() override;
  bool IsDRM() { return mIsDRM; }
  bool IsHDR() { return mIsHDR; }
  void SetClipRect(const Maybe<gfx::IntRect>& aClipRect) override;
  Maybe<gfx::IntRect> ClipRect() override;
  void SetRoundedClipRect(
      const Maybe<gfx::RoundedRect>& aRoundedClipRect) override;
  Maybe<gfx::RoundedRect> RoundedClipRect() override;
  gfx::IntRect CurrentSurfaceDisplayRect() override;
  void SetSurfaceIsFlipped(bool aIsFlipped) override;
  bool SurfaceIsFlipped() override;

  void AttachExternalImage(wr::RenderTextureHost* aExternalImage) override;
  RefPtr<GpuFence> GetGpuFence() override;

  RefPtr<widget::WaylandBuffer> FrontBuffer();

  NativeLayerRemoteWayland(const gfx::IntSize& aSize, bool aIsOpaque,
                           SurfacePoolHandleWayland* aSurfacePoolHandle,
                           RefPtr<widget::DRMFormat> aDRMFormat);
  explicit NativeLayerRemoteWayland(bool aIsOpaque);
  explicit NativeLayerRemoteWayland(gfx::DeviceColor aColor);
  ~NativeLayerRemoteWayland() override;

  // If dirty, add CommandChangedSurface and CommandLayerInfo to the queue.
  // Clear dirty flags.
  void FlushDirtyLayerInfoToCommandQueue();

 protected:
  Maybe<NativeLayerWaylandSurfaceHandler> mSurfaceHandler;
  const RefPtr<widget::DRMFormat> mDRMFormat;
  RefPtr<NativeLayerCommandQueue> mCommandQueue;
  const Maybe<gfx::DeviceColor> mColor;
  const bool mIsOpaque = false;

  bool mDirtyLayerInfo = true;
  // mDirtyLayerInfo is set when any of these change:
  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  gfx::IntRect mDisplayRect;
  Maybe<gfx::IntRect> mClipRect;
  Maybe<gfx::RoundedRect> mRoundedClipRect;
  gfx::SamplingFilter mSamplingFilter = gfx::SamplingFilter::POINT;
  bool mSurfaceIsFlipped = false;

  bool mDirtyChangedSurface = true;
  // mDirtyChangedSurface is set when any of these change, or when the
  // value returned by FrontBuffer() changes:
  RefPtr<widget::WaylandBuffer> mExternalImage;
  // Identifies the surface of the last attached external image.
  uintptr_t mLastExternalSurface = 0;
  bool mIsDRM = false;
  bool mIsHDR = false;
  gfx::IntSize mSize;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerRemoteWayland_h
