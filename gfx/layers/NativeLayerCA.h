/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerCA_h
#define mozilla_layers_NativeLayerCA_h

#include <CoreMedia/CoreMedia.h>
#include <IOSurface/IOSurfaceRef.h>

#include <ostream>

#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"

#include "CFTypeRefPtr.h"
#include "mozilla/gfx/MacIOSurface.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/NativeLayerMacSurfaceHandler.h"
#include "mozilla/webrender/RenderMacIOSurfaceTextureHost.h"
#include "nsISupportsImpl.h"
#include "nsRegion.h"

#ifdef __OBJC__
@class CALayer;
@class CARenderer;
#else
typedef void CALayer;
typedef void CARenderer;
#endif

namespace mozilla {

namespace gl {
class GLContextCGL;
class MozFramebuffer;
}  // namespace gl
namespace wr {
class RenderMacIOSurfaceTextureHost;
}  // namespace wr

namespace layers {

class NativeLayerRootSnapshotterCA;

enum class VideoLowPowerType {
  // These must be kept synchronized with the telemetry histogram enums.
  NotVideo,           // Never emitted as telemetry. No video is visible.
  LowPower,           // As best we can tell, we are in the "detached",
                      // low-power compositing mode. We don't use "Success"
                      // because of name collision with telemetry generation.
  FailMultipleVideo,  // There is more than one video visible.
  FailWindowed,       // The window is not fullscreen.
  FailOverlaid,       // Something is on top of the video (likely captions).
  FailBacking,        // The layer behind the video is not full-coverage black.
  FailMacOSVersion,   // macOS version does not meet requirements.
  FailPref,           // Pref is not set.
  FailSurface,        // Surface is not eligible.
  FailEnqueue,        // Enqueueing the video didn't work.
};

// The type of update needed to apply the pending changes to a NativeLayerCA
// representation.
//
// Order is important. Each enum must fully encompass the work implied by the
// previous enums.
enum class NativeLayerCAUpdateType {
  None,
  OnlyVideo,
  All,
};

class SnapshotterCADelegate {
 public:
  virtual ~SnapshotterCADelegate();

  virtual float BackingScale() const { return 1.0f; }
  virtual bool DoCustomReadbackForReftestsIfDesired(
      const gfx::IntSize& aReadbackSize, gfx::SurfaceFormat aReadbackFormat,
      const Range<uint8_t>& aReadbackBuffer) {
    return false;
  }
  virtual void UpdateSnapshotterLayers(CALayer* aRootCALayer) = 0;
  virtual void OnSnapshotterDestroyed(
      NativeLayerRootSnapshotterCA* aSnapshotter) {}
};

// NativeLayerRootCA is the CoreAnimation implementation of the NativeLayerRoot
// interface. A NativeLayerRootCA is created by the widget around an existing
// CALayer with a call to CreateForCALayer - this CALayer is the root of the
// "onscreen" representation of this layer tree.
// All methods can be called from any thread, there is internal locking.
// All effects from mutating methods are buffered locally and don't modify the
// underlying CoreAnimation layers until CommitToScreen() is called. This
// ensures that the modifications happen on the right thread.
//
// More specifically: During normal operation, screen updates are driven from a
// compositing thread. On this thread, the layers are created / destroyed, their
// contents are painted, and the result is committed to the screen. However,
// there are some scenarios that need to involve the main thread, most notably
// window resizing: During a window resize, we still need the drawing part to
// happen on the compositing thread, but the modifications to the underlying
// CALayers need to happen on the main thread, once compositing is done.
//
// NativeLayerRootCA + NativeLayerCA create and maintain *two* CALayer tree
// representations: An "onscreen" representation and an "offscreen"
// representation. These representations are updated via calls to
// CommitToScreen() and CommitOffscreen(), respectively. The reason for having
// two representations is the following: Our implementation of the snapshotter
// API uses CARenderer, which lets us render the composited result of our layer
// tree into a GPU buffer. But CARenderer requires "ownership" of the rendered
// CALayers in the sense that it associates the CALayers with a local
// "CAContext". A CALayer can only be associated with one CAContext at any time.
// If we wanted te render our *onscreen* CALayers with CARenderer, we would need
// to remove them from the window, reparent them to the CARenderer, render them,
// and then put them back into the window. This would lead to a visible flashing
// effect. To solve this problem, we build two CALayer representations, so that
// one representation can stay inside the window and the other can stay attached
// to the CARenderer.
class NativeLayerRootCA final : public NativeLayerRoot {
 public:
  static already_AddRefed<NativeLayerRootCA> CreateForCALayer(CALayer* aLayer);

  virtual NativeLayerRootCA* AsNativeLayerRootCA() override { return this; }

  // Can be called on any thread at any point. Returns whether comitting was
  // successful. Will return false if called off the main thread while
  // off-main-thread commits are suspended.
  bool CommitToScreen() override;

  void CommitOffscreen(CALayer* aRootCALayer);
  void OnNativeLayerRootSnapshotterDestroyed(
      NativeLayerRootSnapshotterCA* aNativeLayerRootSnapshotter);

  // Enters a mode during which CommitToScreen(), when called on a non-main
  // thread, will not apply any updates to the CALayer tree.
  void SuspendOffMainThreadCommits();

  // Exits the mode entered by SuspendOffMainThreadCommits().
  // Returns true if the last CommitToScreen() was canceled due to suspension,
  // indicating that another call to CommitToScreen() is needed.
  bool UnsuspendOffMainThreadCommits();

  bool AreOffMainThreadCommitsSuspended();

  void DumpLayerTreeToFile(const char* aPath,
                           const MutexAutoLock& aProofOfLock);

  enum class WhichRepresentation : uint8_t { ONSCREEN, OFFSCREEN };

  // Overridden methods
  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;
  UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() override;

  void SetBackingScale(float aBackingScale);
  float BackingScale();

  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) override;
  already_AddRefed<NativeLayer> CreateLayerForColor(
      gfx::DeviceColor aColor) override;
  already_AddRefed<NativeLayer> CreateLayerForSurfacePresentation(
      const gfx::IntSize& aSize, bool aIsOpaque) override;

  void SetWindowIsFullscreen(bool aFullscreen);

  VideoLowPowerType CheckVideoLowPower(const MutexAutoLock& aProofOfLock);

 protected:
  explicit NativeLayerRootCA(CALayer* aLayer);
  ~NativeLayerRootCA() override;

  // Do a commit of the pending changes to the CALayers. This is used for both
  // onscreen commits (modifying the CALayers attached to the NSView), and for
  // "offscreen" commits.
  // "Offscreen" commits are updating a separate copy of the CALayer tree for
  // use with CARenderer by the snapshotter.
  void CommitRepresentation(WhichRepresentation aRepresentation,
                            CALayer* aRootCALayer,
                            const nsTArray<RefPtr<NativeLayerCA>>& aSublayers,
                            bool aMutatedLayerStructure,
                            bool aWindowIsFullscreen);

  void SetMutatedLayerStructure();

  using UpdateType = NativeLayerCAUpdateType;
  UpdateType GetMaxUpdateRequired(
      WhichRepresentation aRepresentation,
      const nsTArray<RefPtr<NativeLayerCA>>& aSublayers,
      bool aMutatedLayerStructure) const;

  // An implementation of SnapshotterCADelegate, backed by a NativeLayerRootCA.
  struct SnapshotterDelegate final : public SnapshotterCADelegate {
    explicit SnapshotterDelegate(NativeLayerRootCA* aLayerRoot);
    virtual ~SnapshotterDelegate() override;
    virtual float BackingScale() const override {
      return mLayerRoot->BackingScale();
    }
    virtual void UpdateSnapshotterLayers(CALayer* aRootCALayer) override {
      mLayerRoot->CommitOffscreen(aRootCALayer);
    }
    virtual void OnSnapshotterDestroyed(
        NativeLayerRootSnapshotterCA* aSnapshotter) override {
      mLayerRoot->OnNativeLayerRootSnapshotterDestroyed(aSnapshotter);
    }
    RefPtr<NativeLayerRootCA> mLayerRoot;
  };

  Mutex mMutex MOZ_UNANNOTATED;              // protects all other fields
  CALayer* mOnscreenRootCALayer = nullptr;   // strong
  CALayer* mOffscreenRootCALayer = nullptr;  // strong
  NativeLayerRootSnapshotterCA* mWeakSnapshotter = nullptr;
  nsTArray<RefPtr<NativeLayerCA>> mSublayers;  // in z-order
  float mBackingScale = 1.0f;
  bool mMutated = false;

  // While mOffMainThreadCommitsSuspended is true, no commits
  // should happen on a non-main thread, because they might race with
  // main-thread driven updates such as window shape changes, and cause
  // glitches.
  bool mOffMainThreadCommitsSuspended = false;

  // Set to true if CommitToScreen() was aborted because of commit suspension.
  // Set to false when CommitToScreen() completes successfully. When true,
  // indicates that CommitToScreen() needs to be called at the next available
  // opportunity.
  bool mCommitPending = false;

  // Updated by the layer's view's window to match the fullscreen state
  // of that window.
  bool mWindowIsFullscreen = false;

  // Whether mSublayers has changed since the last onscreen / offscreen commit.
  bool mMutatedOnscreenLayerStructure = false;
  bool mMutatedOffscreenLayerStructure = false;

  // How many times have we committed since the last time we emitted
  // telemetry?
  unsigned int mTelemetryCommitCount = 0;
};

class RenderSourceNLRS;

#ifdef XP_MACOSX
class NativeLayerRootSnapshotterCA final : public NativeLayerRootSnapshotter {
 public:
  static UniquePtr<NativeLayerRootSnapshotterCA> Create(
      UniquePtr<SnapshotterCADelegate>&& aDelegate);
  virtual ~NativeLayerRootSnapshotterCA();

  bool ReadbackPixels(const gfx::IntSize& aReadbackSize,
                      gfx::SurfaceFormat aReadbackFormat,
                      const Range<uint8_t>& aReadbackBuffer) override;
  already_AddRefed<profiler_screenshots::RenderSource> GetWindowContents(
      const gfx::IntSize& aWindowSize) override;
  already_AddRefed<profiler_screenshots::DownscaleTarget> CreateDownscaleTarget(
      const gfx::IntSize& aSize) override;
  already_AddRefed<profiler_screenshots::AsyncReadbackBuffer>
  CreateAsyncReadbackBuffer(const gfx::IntSize& aSize) override;

 protected:
  NativeLayerRootSnapshotterCA(UniquePtr<SnapshotterCADelegate>&& aDelegate,
                               RefPtr<gl::GLContext>&& aGL);
  void UpdateSnapshot(const gfx::IntSize& aSize);

  UniquePtr<SnapshotterCADelegate> mDelegate;
  RefPtr<gl::GLContext> mGL;

  // Can be null. Created and updated in UpdateSnapshot.
  RefPtr<RenderSourceNLRS> mSnapshot;
  CARenderer* mRenderer = nullptr;  // strong
};
#endif

// Wraps one CALayer representation of NativeLayerCA.
struct NativeLayerCARepresentation {
  using UpdateType = NativeLayerCAUpdateType;

  NativeLayerCARepresentation();
  ~NativeLayerCARepresentation();

  // Returns null if the layer is currently completely clipped out.
  CALayer* UnderlyingCALayer() {
    return mWrappingCALayerHasExtent ? mWrappingCALayer : nullptr;
  }

  bool EnqueueSurface(IOSurfaceRef aSurfaceRef, bool aIsHDR);

  // Applies buffered changes to the native CALayers. The contract with the
  // caller is as follows: If any of these values have changed since the last
  // call to ApplyChanges, mMutated[Field] needs to have been set to true
  // before the call. If aUpdate is not All, then a partial update will be
  // applied. In such a case, ApplyChanges may not make any changes that
  // require a CATransacation, because no transaction will be created. In a
  // a partial update, the return value will indicate if all the needed
  // changes were able to be applied under these restrictions. A false return
  // value indicates an All update is necessary.
  bool ApplyChanges(NativeLayerCAUpdateType aUpdate, const gfx::IntSize& aSize,
                    bool aIsOpaque, const gfx::IntPoint& aPosition,
                    const gfx::Matrix4x4& aTransform,
                    const gfx::IntRect& aDisplayRect,
                    const Maybe<gfx::IntRect>& aClipRect,
                    const Maybe<gfx::RoundedRect>& aRoundedClip,
                    float aBackingScale, bool aSurfaceIsFlipped,
                    gfx::SamplingFilter aSamplingFilter, bool aSpecializeVideo,
                    const CFTypeRefPtr<IOSurfaceRef>& aFrontSurface,
                    const Maybe<gfx::DeviceColor>& aColor, bool aIsVideo,
                    bool aIsDRM, bool aIsHDR);

  // Return whether any aspects of this layer representation have been mutated
  // since the last call to ApplyChanges, i.e. whether ApplyChanges needs to
  // be called.
  // This is used to optimize away a CATransaction commit if no layers have
  // changed.
  NativeLayerCAUpdateType HasUpdate(bool aIsVideo);

  // Lazily initialized by first call to ApplyChanges. mWrappingLayer is the
  // layer that applies the intersection of mDisplayRect and mClipRect (if
  // set), and mContentCALayer is the layer that hosts the IOSurface. We do
  // not share clip layers between consecutive NativeLayerCA objects with the
  // same clip rect.
  CALayer* mWrappingCALayer = nullptr;      // strong
  CALayer* mRoundedClipCALayer = nullptr;   // strong
  CALayer* mContentCALayer = nullptr;       // strong
  CALayer* mOpaquenessTintLayer = nullptr;  // strong
  // Opaque black layer under a DRM video's content layer. Compositions that
  // leave out the capture-protected content layer (OS snapshots, including
  // the one macOS animates for the fullscreen transition) show it instead of
  // whatever lies behind the video. It never holds video pixels.
  CALayer* mProtectedBackingLayer = nullptr;  // strong

#ifdef NIGHTLY_BUILD
  bool mLogNextVideoSurface = false;
#endif

  bool mWrappingCALayerHasExtent : 1;

  // These are all initialized to true by the constructor.
  bool mMutatedPosition : 1;
  bool mMutatedTransform : 1;
  bool mMutatedDisplayRect : 1;
  bool mMutatedClipRect : 1;
  bool mMutatedRoundedClipRect : 1;
  bool mMutatedBackingScale : 1;
  bool mMutatedSize : 1;
  bool mMutatedSurfaceIsFlipped : 1;
  bool mMutatedFrontSurface : 1;
  bool mMutatedSamplingFilter : 1;
  bool mMutatedSpecializeVideo : 1;
  bool mMutatedIsDRM : 1;
  bool mMutatedIsHDR : 1;
  // Don't forget to update the constructor when you add a field here.
};

// NativeLayerCA wraps a CALayer and lets you draw to it. It ensures that only
// fully-drawn frames make their way to the screen, by maintaining a swap chain
// of IOSurfaces.
// All calls to mutating methods are buffered, and don't take effect on the
// underlying CoreAnimation layers until ApplyChanges() is called.
// The two most important methods are NextSurface and NotifySurfaceReady:
// NextSurface takes an available surface from the swap chain or creates a new
// surface if necessary. This surface can then be drawn to. Once drawing is
// finished, NotifySurfaceReady marks the surface as ready. This surface is
// committed to the layer during the next call to ApplyChanges().
// The swap chain keeps track of invalid areas within the surfaces.
class NativeLayerCA : public NativeLayer {
 public:
  virtual NativeLayerCA* AsNativeLayerCA() override { return this; }

  // Overridden methods
  gfx::IntSize GetSize() override;
  void SetPosition(const gfx::IntPoint& aPosition) override;
  gfx::IntPoint GetPosition() override;
  void SetTransform(const gfx::Matrix4x4& aTransform) override;
  gfx::Matrix4x4 GetTransform() override;
  gfx::IntRect GetRect() override;
  void SetSamplingFilter(gfx::SamplingFilter aSamplingFilter) override;
  RefPtr<gfx::DrawTarget> NextSurfaceAsDrawTarget(
      const gfx::IntRect& aDisplayRect, const gfx::IntRegion& aUpdateRegion,
      gfx::BackendType aBackendType) override;
  Maybe<GLuint> NextSurfaceAsFramebuffer(const gfx::IntRect& aDisplayRect,
                                         const gfx::IntRegion& aUpdateRegion,
                                         bool aNeedsDepth) override;
  void NotifySurfaceReady() override;
  void DiscardBackbuffers() override;
  bool IsOpaque() override;
  void SetClipRect(const Maybe<gfx::IntRect>& aClipRect) override;
  Maybe<gfx::IntRect> ClipRect() override;
  void SetRoundedClipRect(const Maybe<gfx::RoundedRect>& aClip) override;
  Maybe<gfx::RoundedRect> RoundedClipRect() override;
  gfx::IntRect CurrentSurfaceDisplayRect() override;
  void SetDisplayRect(const gfx::IntRect& aDisplayRect);
  void SetSurfaceIsFlipped(bool aIsFlipped) override;
  bool SurfaceIsFlipped() override;

  // Used to force a specific IOSurfaceRef to be used.
  void SetSurfaceToPresent(CFTypeRefPtr<IOSurfaceRef> aSurfaceRef,
                           gfx::IntSize& aSize, bool aIsDRM, bool aIsHDR);

  void DumpLayer(std::ostream& aOutputStream);
  static void LogSurface(const nsACString& aHeader, IOSurfaceRef aSurfaceRef,
                         CVPixelBufferRef aBuffer,
                         CMVideoFormatDescriptionRef aFormat);

  void AttachExternalImage(wr::RenderTextureHost* aExternalImage) override;
  RefPtr<GpuFence> GetGpuFence() override;

  void SetRootWindowIsFullscreen(bool aFullscreen);

 protected:
  friend class NativeLayerRootCA;
  friend struct NativeLayerCARepresentation;
  using UpdateType = NativeLayerCAUpdateType;
  using WhichRepresentation = NativeLayerRootCA::WhichRepresentation;
  using Representation = NativeLayerCARepresentation;

  NativeLayerCA(const gfx::IntSize& aSize, bool aIsOpaque,
                SurfacePoolHandleCA* aSurfacePoolHandle);
  explicit NativeLayerCA(bool aIsOpaque);
  explicit NativeLayerCA(gfx::DeviceColor aColor);
  // This constructor is used for surfaces being directly supplied
  // to the layer.
  explicit NativeLayerCA(const gfx::IntSize& aSize, bool aIsOpaque);

  ~NativeLayerCA() override;

  // To be called by NativeLayerRootCA:
  CALayer* UnderlyingCALayer(WhichRepresentation aRepresentation);

  NativeLayerCAUpdateType HasUpdate(WhichRepresentation aRepresentation);

  // Apply pending updates to the underlaying CALayer. Sets *aMustRebuild to
  // true if the update requires changing which set of CALayers should be in the
  // parent.
  bool ApplyChanges(WhichRepresentation aRepresentation, UpdateType aUpdate,
                    bool* aMustRebuild);

  void SetBackingScale(float aBackingScale);

  // Invalidate aUpdateRegion and make sure that mInProgressSurface retains any
  // valid content from the previous surface outside of aUpdateRegion, so that
  // only aUpdateRegion needs to be drawn. If content needs to be copied,
  // aCopyFn is called to do the copying.
  // aCopyFn: Fn(CFTypeRefPtr<IOSurfaceRef> aValidSourceIOSurface,
  //             const gfx::IntRegion& aCopyRegion) -> void
  template <typename F>
  void HandlePartialUpdate(const MutexAutoLock& aProofOfLock,
                           const gfx::IntRect& aDisplayRect,
                           const gfx::IntRegion& aUpdateRegion, F&& aCopyFn);

  bool IsVideo(const MutexAutoLock& aProofOfLock);
  bool ShouldSpecializeVideo(const MutexAutoLock& aProofOfLock);

  // This function returns a CGRect if a clip should be applied to the layer.
  // If set, the CGRect has the scaled position of the clip relative to the
  // surface origin and the scaled size of the clip rect.
  static Maybe<CGRect> CalculateClipGeometry(
      const gfx::IntSize& aSize, const gfx::IntPoint& aPosition,
      const gfx::Matrix4x4& aTransform, const gfx::IntRect& aDisplayRect,
      const Maybe<gfx::IntRect>& aClipRect, float aBackingScale);

  Representation& GetRepresentation(WhichRepresentation aRepresentation);
  template <typename F>
  void ForAllRepresentations(F aFn);

  // Controls access to all fields of this class.
  Mutex mMutex MOZ_UNANNOTATED;

  // The IOSurface to use for this layer. Optional.
  // If not present, we get use a surface from mSurfaceHandler or mTextureHost
  // instead. We mark the surface as "in use" as long as it is in
  // mSurfaceToPresent, to prevent other processes from recycling it.
  CFTypeRefPtr<IOSurfaceRef> mSurfaceToPresent;

  Maybe<NativeLayerMacSurfaceHandler> mSurfaceHandler;

  RefPtr<wr::RenderMacIOSurfaceTextureHost> mTextureHost;
  bool mTextureHostIsVideo = false;

  Representation mOnscreenRepresentation;
  Representation mOffscreenRepresentation;

  gfx::IntPoint mPosition;
  gfx::Matrix4x4 mTransform;
  gfx::IntRect mDisplayRect;
  gfx::IntSize mSize;
  Maybe<gfx::IntRect> mClipRect;
  Maybe<gfx::RoundedRect> mRoundedClipRect;
  Maybe<gfx::DeviceColor> mColor;
  gfx::SamplingFilter mSamplingFilter = gfx::SamplingFilter::POINT;
  float mBackingScale = 1.0f;
  bool mSurfaceIsFlipped = false;
  const bool mIsOpaque = false;
  bool mRootWindowIsFullscreen = false;
  bool mSpecializeVideo = false;
  bool mIsDRM = false;
  bool mIsHDR = false;

#ifdef NIGHTLY_BUILD
  // Track the consistency of our caller's API usage. Layers that are drawn
  // should only ever be called with NotifySurfaceReady. Layers that are
  // external should only ever be called with AttachExternalImage.
  bool mHasEverAttachExternalImage = false;
  bool mHasEverNotifySurfaceReady = false;
#endif
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerCA_h
