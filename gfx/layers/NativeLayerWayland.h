/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerWayland_h
#define mozilla_layers_NativeLayerWayland_h

#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/widget/DMABufFormats.h"
#include "nsRegion.h"
#include "nsTArray.h"

class DMABufSurfaceRGBA;

namespace mozilla::wr {
class RenderDMABUFTextureHost;
}  // namespace mozilla::wr

namespace mozilla::widget {
class WaylandSurfaceLock;
}  // namespace mozilla::widget

namespace mozilla::layers {
class NativeLayerWaylandExternal;
class NativeLayerWaylandRender;

struct LayerState {
  // Layer is visible (has correct size/position), we should paint it
  bool mIsVisible : 1;
  // Layer has been rendered and it's visible
  bool mIsRendered : 1;

  // Layer visibility has been changed
  bool mMutatedVisibility : 1;
  // Layer stacking order was changed (layer was added/removed/mapped/unmapped)
  bool mMutatedStackingOrder : 1;
  // Layer placement (size/position/scale etc.) was changed
  bool mMutatedPlacement : 1;
  // mFrontBuffer was changed and we need to commit it to Wayland compositor
  // to show new content.
  bool mMutatedFrontBuffer : 1;

  // Was rendered in last cycle.
  bool mRenderedLastCycle : 1;

  // For debugging purposse. Resets the layer state
  // to force full init.
  void InvalidateAll() {
    mIsVisible = false;
    mIsRendered = false;

    mMutatedVisibility = true;
    mMutatedStackingOrder = true;
    mMutatedPlacement = true;
    mMutatedFrontBuffer = true;
    mRenderedLastCycle = false;
  }
};

class NativeLayerRootWayland final : public NativeLayerRoot {
 public:
  static already_AddRefed<NativeLayerRootWayland> Create(
      RefPtr<widget::WaylandSurface> aWaylandSurface);

  // Overridden methods
  NativeLayerRootWayland* AsNativeLayerRootWayland() override { return this; }
  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) override;
  UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() override;

  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;

  void PrepareForCommit() override { mFrameInProcess = true; };
  bool CommitToScreen() override;

  // Main thread only
  GdkWindow* GetGdkWindow() const;

  RefPtr<widget::WaylandSurface> GetRootWaylandSurface() {
    return mRootSurface;
  }

  already_AddRefed<widget::DRMFormat> GetDRMFormat() {
    return do_AddRef(static_cast<widget::DRMFormat*>(mDRMFormat));
  }
  void SetDRMFormat(widget::DRMFormat* aFormat);

  void VSyncCallbackHandler(uint32_t aTime, bool aEmulated);

  RefPtr<widget::WaylandBuffer> BorrowExternalBuffer(
      RefPtr<BufferSurface> aBufferSurface);

#ifdef MOZ_LOGGING
  nsAutoCString GetDebugTag() const;
  void* GetLoggingWidget() const;
#endif

  void Init();
  void Shutdown();

  void UpdateLayersOnMainThread();
  void RequestUpdateOnMainThreadLocked(
      const widget::WaylandSurfaceLock& aProofOfLock);

  explicit NativeLayerRootWayland(
      RefPtr<widget::WaylandSurface> aWaylandSurface);

  void NotifyFullscreenChanged(bool aIsFullscreen) {
    mIsFullscreen = aIsFullscreen;
  }

  NativeLayerWaylandRender* GetLayerForSnapshot();

  void SetGLContext(gl::GLContext* aGL) { mGL = aGL; }
  gl::GLContext* gl() { return mGL; }

 private:
  ~NativeLayerRootWayland();

  // Map NativeLayerRootWayland and all child surfaces.
  // Returns true if we're set.
  bool MapLocked(const widget::WaylandSurfaceLock& aProofOfLock);
  bool IsEmptyLocked(const widget::WaylandSurfaceLock& aProofOfLock);
  void ClearLayersLocked(const widget::WaylandSurfaceLock& aProofOfLock);

  bool CommitToScreenLocked(widget::WaylandSurfaceLock& aLock);
  void ConfigureScaleLocked(widget::WaylandSurfaceLock& aProofOfLock);

#ifdef MOZ_LOGGING
  void LogStatsLocked(const widget::WaylandSurfaceLock& aProofOfLock);
#endif

#ifdef MOZ_LOGGING
  void* mLoggingWidget = nullptr;
#endif

  RefPtr<gl::GLContext> mGL;

  // WaylandSurface of nsWindow (our root window).
  // This WaylandSurface is owned by nsWindow so we don't map/unmap it
  // or handle any callbacks.
  // We also use widget::WaylandSurfaceLock for locking whole layer for
  // read/write.
  RefPtr<widget::WaylandSurface> mRootSurface;

  // Copy of DRM format we use to create DMABuf surfaces
  Atomic<widget::DRMFormat*> mDRMFormat{nullptr};

  // Empty buffer attached to mSurface. We need to have something
  // attached to make mSurface and all child visible.
  RefPtr<widget::WaylandBuffer> mTmpBuffer;

  // Child layers attached to this root, they're all on the same level
  // so all child layers are attached to mContainer as subsurfaces.
  // Layer visibility is sorted by Z-order, mSublayers[0] is on bottom.
  nsTArray<RefPtr<NativeLayerWayland>> mSublayers;

  // Child layers which needs to be updated on main thread,
  // they have been added or removed.
  nsTArray<RefPtr<NativeLayerWayland>> mMainThreadUpdateSublayers;

  // Child layers which has been removed and are
  // waiting to be unmapped. We do that in sync with root surface to avoid
  // flickering. When unmapped they're moved to mMainThreadUpdateSublayers
  // for final clean up at main thread.
  nsTArray<RefPtr<NativeLayerWayland>> mRemovedSublayers;

  // External buffers (DMABuf) used by the layers.
  // We want to cache and reuse wl_buffer of external images.
  nsTArray<widget::WaylandBufferHolder> mExternalBuffers;

  // We're between CompositorBeginFrame() / CompositorEndFrame() calls.
  mozilla::Atomic<bool, mozilla::Relaxed> mFrameInProcess{false};

  uint32_t mLastFrameCallbackTime = 0;

  // State flags used for optimizations
  // Layers have been added/removed
  bool mRootMutatedStackingOrder = false;
  // All layers has been rendered
  bool mRootAllLayersRendered = false;
  bool mMainThreadUpdateQueued = false;
  bool mIsFullscreen = false;
  // We're missing commit to root surface
  bool mMissingRootCommit = false;
};

class NativeLayerWayland : public NativeLayer {
 public:
  NativeLayerWayland* AsNativeLayerWayland() override { return this; }
  virtual NativeLayerWaylandExternal* AsNativeLayerWaylandExternal() {
    return nullptr;
  }
  virtual NativeLayerWaylandRender* AsNativeLayerWaylandRender() {
    return nullptr;
  }

  // Overridden methods
  gfx::IntSize GetSize() override;
  void SetPosition(const gfx::IntPoint& aPosition) override;
  gfx::IntPoint GetPosition() override;
  void SetTransform(const gfx::Matrix4x4& aTransform) override;
  gfx::Matrix4x4 GetTransform() override;
  gfx::IntRect GetRect() override;
  void SetSamplingFilter(gfx::SamplingFilter aSamplingFilter) override;

  bool IsOpaque() override;
  void SetClipRect(const Maybe<gfx::IntRect>& aClipRect) override;
  Maybe<gfx::IntRect> ClipRect() override;
  void SetRoundedClipRect(const Maybe<gfx::RoundedRect>& aClip) override;
  Maybe<gfx::RoundedRect> RoundedClipRect() override;
  gfx::IntRect CurrentSurfaceDisplayRect() override;
  void SetSurfaceIsFlipped(bool aIsFlipped) override;
  bool SurfaceIsFlipped() override;

  void RenderLayer(double aScale);
  // TODO
  RefPtr<GpuFence> GetGpuFence() override { return nullptr; }

  RefPtr<widget::WaylandSurface> GetWaylandSurface() { return mSurface; }

  // Surface Map/Unamp happens on rendering thread.
  //
  // We can use surface right after map but we need to finish mapping
  // on main thread to render it correctly.
  //
  // Also Unmap() needs to be finished on main thread.
  bool IsMapped();
  bool IsVisible();
  bool Map(widget::WaylandSurfaceLock& aParentWaylandSurfaceLock);
  void Unmap();

  void UpdateOnMainThread();
  void MainThreadMap();
  void MainThreadUnmap();

  void ForceCommit();

  void PlaceAbove(NativeLayerWayland* aLowerLayer);
  void SetCoordinatesScale(uint32_t aCoordinatesScale);
#ifdef MOZ_LOGGING
  nsAutoCString GetDebugTag() const;
#endif

  void SetFrameCallbackState(bool aState);

  virtual void DiscardBackbuffersLocked(
      const widget::WaylandSurfaceLock& aProofOfLock, bool aForce = false) = 0;
  void DiscardBackbuffers() override;

  NativeLayerWayland(NativeLayerRootWayland* aRootLayer,
                     const gfx::IntSize& aSize, bool aIsOpaque);

  // No need to lock as we used it when new layers are added only
  constexpr static int sLayerClear = 0;
  constexpr static int sLayerRemoved = 1;
  constexpr static int sLayerAdded = 2;

  void MarkClear() { mUsageCount = sLayerClear; }
  void MarkRemoved() { mUsageCount = sLayerRemoved; }
  void MarkAdded() { mUsageCount += sLayerAdded; }

  bool IsRemoved() const { return mUsageCount == sLayerRemoved; }
  bool IsNew() const { return mUsageCount == sLayerAdded; }

  LayerState* State() { return &mState; }

 protected:
  void SetScalelocked(const widget::WaylandSurfaceLock& aProofOfLock,
                      double aScale);
  void UpdateLayerPlacementLocked(
      const widget::WaylandSurfaceLock& aProofOfLock);
  virtual bool CommitFrontBufferToScreenLocked(
      const widget::WaylandSurfaceLock& aProofOfLock) = 0;
  virtual bool IsFrontBufferChanged() = 0;

 protected:
  ~NativeLayerWayland();

  virtual RefPtr<BufferSurface> GetSurface() { return nullptr; }

  // There's a cycle dependency here as NativeLayerRootWayland holds strong
  // reference to NativeLayerWayland and vice versa.
  //
  // Shutdown sequence is:
  //
  // 1) NativeLayerRootWayland is released by GtkCompositorWidget
  // 2) NativeLayerRootWayland calls childs NativeLayerWayland release code and
  //    unrefs them.
  // 3) Child NativeLayerWayland register main thread callback to clean up
  //    and release itself.
  // 4) Child NativeLayerWayland unref itself and parent NativeLayerRootWayland.
  // 5) NativeLayerRootWayland is released when there isn't any
  //    NativeLayerWayland left.
  //
  RefPtr<NativeLayerRootWayland> mRootLayer;

  RefPtr<widget::WaylandSurface> mSurface;

  // Final buffer which we attach to WaylandSurface
  RefPtr<widget::WaylandBuffer> mFrontBuffer;

  const bool mIsOpaque = false;

  // Used at SetLayers() when we need to identify removed layers, new layers
  // and layers removed but returned back.
  // We're adding respective constants to mUsageCount for each layer
  // so removed layers have usage count 1, newly added 2 and removed+added 3.
  int mUsageCount = 0;

  gfx::IntSize mSize;
  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  gfx::IntRect mDisplayRect;
  Maybe<gfx::IntRect> mClipRect;
  Maybe<gfx::RoundedRect> mRoundedClipRect;
  gfx::SamplingFilter mSamplingFilter = gfx::SamplingFilter::POINT;
  double mScale = 1.0f;
  LayerState mState{};
  bool mSurfaceIsFlipped = false;
  bool mIsHDR = false;

  enum class MainThreadUpdate {
    None,
    Map,
    Unmap,
  };

  // Indicate that we need to finish surface map/unmap
  // on main thread.
  // We need to perform main thread unmap even if mapping on main thread
  // is not finished, some main thread resources are created
  // by WaylandSurface itself.
  Atomic<MainThreadUpdate, mozilla::Relaxed> mNeedsMainThreadUpdate{
      MainThreadUpdate::None};

  void SetColorProperties(const widget::WaylandSurfaceLock& aSurfaceLock,
                          widget::WaylandSurface* aParentSurface);
};

class NativeLayerWaylandRender final : public NativeLayerWayland {
 public:
  NativeLayerWaylandRender* AsNativeLayerWaylandRender() override {
    return this;
  }

  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) override;
  Maybe<GLuint> NextSurfaceAsFramebuffer(const gfx::IntRect& aDisplayRect,
                                         const gfx::IntRegion& aUpdateRegion,
                                         bool aNeedsDepth) override;
  void NotifySurfaceReady() override;
  void AttachExternalImage(wr::RenderTextureHost* aExternalImage) override;
  bool IsFrontBufferChanged() override;

  NativeLayerWaylandRender(NativeLayerRootWayland* aRootLayer,
                           const gfx::IntSize& aSize, bool aIsOpaque,
                           SurfacePoolHandleWayland* aSurfacePoolHandle);

  void CopyFrontBufferToFrameBuffer(GLuint aFB);
  gl::GLContext* gl();

 private:
  ~NativeLayerWaylandRender() override;

  void DiscardBackbuffersLocked(const widget::WaylandSurfaceLock& aProofOfLock,
                                bool aForce) override;
  void ReadBackFrontBuffer(const widget::WaylandSurfaceLock& aProofOfLock);
  bool CommitFrontBufferToScreenLocked(
      const widget::WaylandSurfaceLock& aProofOfLock) override;

  const RefPtr<SurfacePoolHandleWayland> mSurfacePoolHandle;
  RefPtr<widget::WaylandBuffer> mInProgressBuffer;
  gfx::IntRegion mDirtyRegion;
};

class NativeLayerWaylandExternal final : public NativeLayerWayland {
 public:
  // Overridden methods
  NativeLayerWaylandExternal* AsNativeLayerWaylandExternal() override {
    return this;
  }
  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) override;
  Maybe<GLuint> NextSurfaceAsFramebuffer(const gfx::IntRect& aDisplayRect,
                                         const gfx::IntRegion& aUpdateRegion,
                                         bool aNeedsDepth) override;
  void NotifySurfaceReady() override {};
  void AttachExternalImage(wr::RenderTextureHost* aExternalImage) override;
  bool IsFrontBufferChanged() override;

  NativeLayerWaylandExternal(NativeLayerRootWayland* aRootLayer,
                             bool aIsOpaque);

 protected:
  RefPtr<BufferSurface> GetSurface() override;

 private:
  ~NativeLayerWaylandExternal() override;

  void DiscardBackbuffersLocked(const widget::WaylandSurfaceLock& aProofOfLock,
                                bool aForce) override;
  void FreeUnusedBackBuffers();
  bool CommitFrontBufferToScreenLocked(
      const widget::WaylandSurfaceLock& aProofOfLock) override;

  uintptr_t mLastSurface = 0;
  RefPtr<wr::RenderTextureHost> mTextureHost;
};

class NativeLayerRootSnapshotterWayland final
    : public NativeLayerRootSnapshotter {
 public:
  static UniquePtr<NativeLayerRootSnapshotterWayland> Create(
      NativeLayerRootWayland* aRootLayer, gl::GLContext* aGL);
  virtual ~NativeLayerRootSnapshotterWayland();

  bool ReadbackPixels(const gfx::IntSize& aReadbackSize,
                      gfx::SurfaceFormat aReadbackFormat,
                      const Range<uint8_t>& aReadbackBuffer) override;
  already_AddRefed<profiler_screenshots::RenderSource> GetWindowContents(
      const gfx::IntSize& aWindowSize) override;
  already_AddRefed<profiler_screenshots::DownscaleTarget> CreateDownscaleTarget(
      const gfx::IntSize& aSize) override;
  already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
  CreateAsyncReadbackBuffer(const gfx::IntSize& aSize) override;

#ifdef MOZ_LOGGING
  nsAutoCString GetDebugTag() const;
#endif

 protected:
  NativeLayerRootSnapshotterWayland(NativeLayerRootWayland* aRootLayer,
                                    gl::GLContext* aGL);
  void UpdateSnapshot(const gfx::IntSize& aSize);

  RefPtr<NativeLayerRootWayland> mRootLayer;
  RefPtr<NativeLayerWaylandRender> mLayerForSnapshot;
  RefPtr<gl::GLContext> mGL;

  // Can be null. Created and updated in UpdateSnapshot.
  RefPtr<RenderSourceNLRS> mSnapshot;
};

}  // namespace mozilla::layers

#endif  // mozilla_layers_NativeLayerWayland_h
