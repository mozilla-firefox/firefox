/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_WAYLAND_SURFACE_H_
#define MOZ_WAYLAND_SURFACE_H_

#include "WUniquePtr.h"
#include "WaylandSurfaceLock.h"
#include "mozilla/Atomics.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/Mutex.h"
#include "nsWaylandDisplay.h"

/* Workaround for bug at wayland-util.h,
 * present in wayland-devel < 1.12
 */
struct wl_surface;
struct wl_subsurface;
struct wl_egl_window;

class MessageLoop;

namespace mozilla::widget {

class WaylandBuffer;
class BufferTransaction;

// WaylandSurface is a wrapper for Wayland rendering target
// which is wl_surface / wl_subsurface.
class WaylandSurface final {
  friend WaylandSurfaceLock;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WaylandSurface);

  WaylandSurface();

  // aRootLayer is a WaylandSurface root which is used by layered (HDR)
  // rendering.
  void Init(RefPtr<WaylandSurface> aRootLayer = nullptr);

#ifdef MOZ_LOGGING
  nsAutoCString GetDebugTag() const;
  void* GetLoggingWidget() const { return mLoggingWidget; };
  void SetLoggingWidget(void* aWidget) { mLoggingWidget = aWidget; }
#endif

  // Fire VSync handler registered to this surface.
  void VSyncCallbackHandler(struct wl_callback* aCallback, uint32_t aTime,
                            bool aEmulated, bool aRoutedFromChildSurface);

  // Set VSync handler which is fired when it's good time for painting
  // and WalandSurface is visible and VSync is enabled.
  //
  // If aEmulateVSyncCallback is set to true, we file VSync handler even
  // if WaylandSurface is not visible. It's used for painting to hidden
  // surface.
  //
  // Once set, the aVSyncCallbackHandler is preserved between unmap/map.
  //
  // It's VSync source responsibility to disable emulated VSync events
  // by SetVSyncCallbackStateLocked().
  void SetVSyncCallbackHandlerLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(wl_callback*, uint32_t, bool)>&
          aVSyncCallbackHandler,
      bool aEmulateVSyncCallback = false);

  // Clears VSync callback handler. It's used if frame callback handler
  // contains strong reference to WaylandSurface class owner
  // which we want to clear.
  void ClearVSyncCallbackHandlerLocked(const WaylandSurfaceLock& aProofOfLock);

  // Enable/Disable any frame callback emission (includes emulated ones).
  void SetVSyncCallbackStateLocked(const WaylandSurfaceLock& aProofOfLock,
                                   bool aEnabled);
  // Register handler which is called on VSync state change set by
  // SetVSyncCallbackStateLocked().
  void SetVSyncCallbackStateHandlerLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(bool)>& aVSyncCallbackStateHandler);

  // Set a routine which returns whether we should run emulated callback
  // or not. Don't overwrite existing one unless aForce is set.
  void SetVSyncEmulateCheckLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<bool(void)>& aVSyncEmulateCheck, bool aForce = false);

  wl_egl_window* GetEGLWindow(DesktopIntSize aSize);
  bool HasEGLWindow() const { return !!mEGLWindow; }

  // Set WaylandSurface target size (viewport & ELG surface if it's present).
  void SetSize(DesktopIntSize aSize);

  // Calculate 'stable' rounded size for subsurface based
  // on its position.
  LayoutDeviceIntSize GetScaledSize(const WaylandSurfaceLock& aProofOfLock,
                                    const DesktopIntSize& aSize) const;

  // Apply changes to EGLWindow size set by SetSize().
  // ApplyEGLWindowSize() is called from compostor thread
  // right before GL rendering to set EGLWindow size / viewport size
  // for actual back buffer.
  //
  // aEGLWindowSize is scaled backbuffer size and it's used similary
  // as WaylandBuffer size at Attach().
  void ApplyEGLWindowSize(LayoutDeviceIntSize aEGLWindowSize);

  // Mapped means we have all internals created.
  bool IsMapped() const { return mIsMapped; }

  // We've got first frame callback so we're really visible now.
  bool IsVisible() const { return mIsVisible; }

  bool IsToplevelSurface() const { return !mParent; }

  void MarkAsRootSurface() { mIsRootSurface = true; }

  // Called from frame callback and sets the visible flag
  void VisibleCallbackHandler();

  // Indicate that Wayland surface uses Gdk resources which
  // need to be released on main thread by GdkCleanUpLocked().
  // It may be called after Unmap() to make sure
  // Gtk resources are not allocated again.
  bool IsPendingGdkCleanup() const { return mIsPendingGdkCleanup; }

  bool IsOpaqueSurfaceHandlerSet() const { return mIsOpaqueSurfaceHandlerSet; }

  bool HasBufferAttached() const { return mBufferAttached; }

  // Mapped as direct surface of MozContainer
  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 wl_surface* aParentWLSurface,
                 DesktopIntPoint aSubsurfacePosition);
  // Mapped as child of WaylandSurface (used by layers)
  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 WaylandSurfaceLock* aParentWaylandSurfaceLock,
                 DesktopIntPoint aSubsurfacePosition);
  // Unmap surface which hides it
  void UnmapLocked(WaylandSurfaceLock& aSurfaceLock);

  // Clean up Gdk resources, on main thread only
  void GdkCleanUpLocked(const WaylandSurfaceLock& aProofOfLock);

  // Allow to register and run a callback when associated widget (nsWindow)
  // is mapped.
  //
  // Map callback is called *after* WaylandSurface::MapLocked() call
  // by widget code on main thread.
  void SetMapCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(WaylandSurfaceLock& aProofOfLock)>& aMapCB);
  void ClearMapCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  void RunMapCallbackLocked(WaylandSurfaceLock& aProofOfLock);

  // Allow to register and run a callback when associated widget (nsWindow)
  // is unmapped.
  //
  // Unmap callback is called *before* WaylandSurface::UnmapLocked() call
  // by widget code on main thread.
  void SetUnmapCallbackLocked(const WaylandSurfaceLock& aProofOfLock,
                              const std::function<void(void)>& aUnmapCB);
  void ClearUnmapCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  void RunUnmapCallback();

  // Attach WaylandBuffer which shows WaylandBuffer content
  // on screen.
  bool AttachLocked(const WaylandSurfaceLock& aSurfaceLock,
                    RefPtr<WaylandBuffer> aBuffer);
  bool IsBufferAttached(WaylandBuffer* aBuffer);

  // If there's any WaylandBuffer recently attached, detach it.
  // It makes the WaylandSurface invisible and it doesn't have any
  // content.
  void RemoveAttachedBufferLocked(const WaylandSurfaceLock& aProofOfLock);

  // Remove deleted transaction from WaylandSurface, it may release
  // referenced WaylandBuffer.
  void RemoveTransactionLocked(const WaylandSurfaceLock& aSurfaceLock,
                               RefPtr<BufferTransaction> aTransaction);

  // CommitLocked() is needed to call after some of *Locked() method
  // to submit the action to Wayland compositor by wl_surface_commit().

  // It's possible to stack more *Locked() methods
  // together and do commit after the last one to do the changes in atomic way.

  // Need of commit is tracked by mSurfaceNeedsCommit flag and
  // if it's set, CommitLocked() is called when WaylandSurfaceLock is destroyed
  // and WaylandSurface is unlocked.
  void CommitLocked(const WaylandSurfaceLock& aProofOfLock,
                    bool aForceCommit = false, bool aForceDisplayFlush = false);

  void EnableDMABufFormatsLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(DMABufFormats*)>& aFormatRefreshCB);
  void DisableDMABufFormatsLocked(const WaylandSurfaceLock& aProofOfLock);

  // Place this WaylandSurface above aLowerSurface
  void PlaceAboveLocked(const WaylandSurfaceLock& aProofOfLock,
                        WaylandSurfaceLock& aLowerSurfaceLock);
  bool MoveLocked(const WaylandSurfaceLock& aProofOfLock,
                  DesktopIntPoint aPosition);
  void SetViewportFollowsSizeChangesLocked(
      const WaylandSurfaceLock& aProofOfLock);
  void SetViewPortSourceRectLocked(const WaylandSurfaceLock& aProofOfLock,
                                   const DesktopRect& aRect);
  void SetViewPortDestLocked(const WaylandSurfaceLock& aProofOfLock,
                             const DesktopIntSize& aDestSize);
  void SetTransformFlippedLocked(const WaylandSurfaceLock& aProofOfLock,
                                 bool aFlippedX, bool aFlippedY);

  void SetOpaqueRegion(const gfx::IntRegion& aRegion);
  void SetOpaqueRegionLocked(const WaylandSurfaceLock& aProofOfLock,
                             const gfx::IntRegion& aRegion);
  void SetOpaqueLocked(const WaylandSurfaceLock& aProofOfLock);
  void ClearOpaqueRegionLocked(const WaylandSurfaceLock& aProofOfLock);
  void OpaqueCallbackHandler();

  void ClearOpaqueCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  void SetOpaqueCallbackLocked(const WaylandSurfaceLock& aProofOfLock);

  bool DisableUserInputLocked(const WaylandSurfaceLock& aProofOfLock);
  void InvalidateRegionLocked(const WaylandSurfaceLock& aProofOfLock,
                              const gfx::IntRegion& aInvalidRegion);
  void InvalidateLocked(const WaylandSurfaceLock& aProofOfLock);

  // We use two scale systems in Firefox/Wayland. Ceiled (integer) scale and
  // fractional scale. Ceiled scale is easy to implement but comes with
  // rendering overhead while fractional rendering paints buffers with exact
  // scale.
  //
  // Fractional scale is used as rendering optimization.
  // For instance if 225% scale is used, ceiled scale is 3
  // and fractional 2.20.
  //
  // If we paint content with ceiled scale 3 and desktop uses scale 225%,
  // Wayland compositor downscales buffer to 2.20 on rendering
  // but we paint more pixels than necessary (so we use name ceiled).
  //
  // Scale is used by wp_viewport. If a surface has a surface-local size
  // of 100 px by 50 px and wishes to submit buffers with a scale of 1.5,
  // then a buffer of 150px by 75 px should be used and the wp_viewport
  // destination rectangle should be 100 px by 50 px.
  // The wl_surface buffer scale should remain set to 1.
  //
  // For scale 2 (200%) we use surface size 200 x 100 px and set
  // viewport size to 100 x 50 px.
  //
  // We're getting fractional scale number with a small delay from
  // wp_fractional_scale_v1 after first commit to surface.
  // Meanwhile we can use ceiled scale number instead of fractional one or
  // get fractional scale from parent window (if there's any).
  //
  enum ScaleType {
    Disabled = 0,
    Ceiled = 1,
    Fractional = 2,
    Coordinates = 3,
  };

  void SetScaleTypeLocked(const WaylandSurfaceLock& aProofOfLock,
                          ScaleType aScaleType, bool aSetHandler);
  bool IsCoordinatesScaleLocked(const WaylandSurfaceLock& aProofOfLock) const {
    MOZ_DIAGNOSTIC_ASSERT(&aProofOfLock == mSurfaceLock);
    return mScaleType == ScaleType::Coordinates;
  }

  // Right now we support two scale change callbacks.
  // ScaleCallbackType::Widget is used by nsWindow & co to promote scale
  // changes to layout.
  // ScaleCallbackType::Layers is used by HDR compositor to propagate
  // changes to rendered layers/subsurfaces.
  //
  // At least one scale callbacks needs to be set before SetScaleTypeLocked()
  // to run the callback.
  enum ScaleCallbackType {
    Widget = 0,
    Layers = 1,
    CallbackNum = 2,
  };
  void SetScaleCallbackLocked(const WaylandSurfaceLock& aProofOfLock,
                              ScaleCallbackType aCallbackType,
                              std::function<void(void)> aScaleCallback);
  bool HasScaleCallbacksLocked(const WaylandSurfaceLock& aProofOfLock);
  void ClearScaleCallbacksLocked(const WaylandSurfaceLock& aProofOfLock);

  // Returns scale as float point number. If WaylandSurface is not mapped,
  // return fractional scale of parent surface or monitor.
  static constexpr const double sNoScale = -1;
  double GetScale() const;
  uint32_t GetCoordinatesScale() const { return mCoordinatesScale; }
  double GetCoordinatesScaleRounded() const {
    return ((double)mCoordinatesScale) / (1 << 24);
  }
  bool HasCoordinatesScaleLocked(const WaylandSurfaceLock& aProofOfLock) const {
    return !!mCoordinatesScaleManager;
  }

  // Called when screen ceiled scale changes or sets initial scale before we map
  // and paint the surface.
  void SetCeiledScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                            int aScreenCeiledScale);
  // Sets coordinates scale explicitly to the surface,
  bool SetCoordinatesScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                                 uint32_t scale_8_24);

  static void AfterPaintHandler(GdkFrameClock* aClock, void* aData);

  // See https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/3111 why we use it.
  // If child surface covers whole area of parent surface and it's opaque,
  // parent surface will not get any events (frame callbacks) from compositor
  // as it's considered as invisible.
  //
  // Firefox uses the parent wl_surface (owned by GdkWindow) to get input
  // events. Without gdk_wayland_window_add_frame_callback_surface() call,
  // Gdk is not getting any events from compostor and we're frozen.
  //
  // So gdk_wayland_window_add_frame_callback_surface() registers wl_surface
  // owned by WaylandSurface to GtkWindow and requests frame callback
  // for it. Such frame callback is then routed to GdkWindow and it's used
  // to fire events like native GdkWindow ones.
  //
  // To make sure WaylandSurface's wl_surface frame callback is generated,
  // we need to commit the wl_surface regularly as Gdk registers frame callback
  // for it at on_frame_clock_after_paint() event of GdkWindow.
  bool AddOpaqueSurfaceHandlerLocked(const WaylandSurfaceLock& aProofOfLock,
                                     GdkWindow* aGdkWindow,
                                     bool aRegisterCommitHandler);
  bool RemoveOpaqueSurfaceHandlerLocked(const WaylandSurfaceLock& aProofOfLock);

  // Additional callback to call from on_frame_clock_after_paint()
  // and before this wl_surface is commited.
  // It can be used to update subsurfaces from main thread.
  void SetGdkCommitCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock,
      const std::function<void(void)>& aGdkCommitCB);
  void ClearGdkCommitCallbackLocked(const WaylandSurfaceLock& aProofOfLock);

  RefPtr<DMABufFormats> GetDMABufFormats() const { return mFormats; }

  GdkWindow* GetGdkWindow() const;

  static bool IsOpaqueRegionEnabled();

  void SetParentLocked(const WaylandSurfaceLock& aProofOfLock,
                       RefPtr<WaylandSurface> aParent);

  void SetColorRepresentationLocked(const WaylandSurfaceLock& aProofOfLock,
                                    int aWLColorCoeficients, bool aFullRange,
                                    uint32_t aWPChromaLocation = 0);

  static void ImageDescriptionFailed(
      void* aData, struct wp_image_description_v1* aImageDescription,
      uint32_t aCause, const char* aMsg);
  static void ImageDescriptionReady(
      void* aData, struct wp_image_description_v1* aImageDescription,
      uint32_t aIdentity);

  void AssertCurrentThreadOwnsMutex();

  void ForceCommit() { mSurfaceNeedsCommit = true; }
  void SetCommitStateLocked(const WaylandSurfaceLock& aProofOfLock,
                            bool aCommitAllowed) {
    mCommitAllowed = aCommitAllowed;
  }

  static bool SetPrimaries(wp_image_description_creator_params_v1* aParams,
                           mozilla::gfx::YUVColorSpace aColorSpace);
  static bool SetTransferFunction(
      wp_image_description_creator_params_v1* aParams,
      gfx::TransferFunction aTransferFunction);

  void SetHDRMetadata(wp_image_description_creator_params_v1* aParams,
                      gfx::TransferFunction aTransferFunction,
                      GdkWindow* aGdkWindow,
                      const mozilla::gfx::HDRMetadata& aHDRMetadata);

  void SetColorManagementLocked(
      const WaylandSurfaceLock& aProofOfLock,
      wp_color_manager_v1* aColorManager,
      wp_image_description_creator_params_v1* aParams);

 private:
  ~WaylandSurface();

  bool MapLocked(const WaylandSurfaceLock& aProofOfLock,
                 wl_surface* aParentWLSurface,
                 WaylandSurfaceLock* aParentWaylandSurfaceLock,
                 DesktopIntPoint aSubsurfacePosition, bool aSubsurfaceDesync);

  wl_surface* Lock(WaylandSurfaceLock* aWaylandSurfaceLock);
  void Unlock(struct wl_surface** aSurface,
              WaylandSurfaceLock* aWaylandSurfaceLock);
  void Commit(WaylandSurfaceLock* aProofOfLock, bool aForceCommit,
              bool aForceDisplayFlush);

  // Get buffer transaction for WaylandBuffer, create new or recycle one.
  BufferTransaction* GetNextTransactionLocked(
      const WaylandSurfaceLock& aSurfaceLock, WaylandBuffer* aBuffer);
  // Force release/detele all transactions and wl_buffers attached to them.
  void ReleaseAllWaylandTransactionsLocked(WaylandSurfaceLock& aSurfaceLock);

  void SetVSyncCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  void ClearVSyncCallbackLocked(const WaylandSurfaceLock& aProofOfLock);
  bool HasEmulatedVSyncCallbackLocked(
      const WaylandSurfaceLock& aProofOfLock) const;
  bool IsEmulatedVSyncEnabledLocked(const WaylandSurfaceLock& aProofOfLock);
  void RequestEmulatedVSyncLocked(const WaylandSurfaceLock& aProofOfLock);

  // Configures requested scale type. If aSetHandler is set it also
  // install wayland-protocol handlers to call the scale change callbacks.
  //
  // We usually want to install handler to toplevel surfaces only
  // and propagate the scale change to child surfaces.
  bool ConfigureScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                            ScaleType aScaleType, bool aSetProtocolHandler);
  bool ConfigureCoordinateScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                                      bool aSetProtocolHandler);
  bool ConfigureFractionalScaleLocked(const WaylandSurfaceLock& aProofOfLock,
                                      bool aSetProtocolHandler);

  // Weak ref to owning widget (nsWindow or NativeLayerWayland),
  // used for diagnostics/logging only.
  void* mLoggingWidget = nullptr;

  // mIsMapped means we're supposed to be visible
  // (or not if Wayland compositor decides so).
  mozilla::Atomic<bool, mozilla::Relaxed> mIsMapped{false};

  // mIsVisible means we're really visible as we've got frame callback.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsVisible{false};

  // We're root of wl_subsurface tree used by layer compositor.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsRootSurface{false};

  // We used Gdk functions which needs clean up in main thread.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsPendingGdkCleanup{false};

  std::function<void(void)> mGdkCommitCallback;
  std::function<void(WaylandSurfaceLock& aProofOfLock)> mMapCallback;
  std::function<void(void)> mUnmapCallback;

  DesktopIntSize mSize;

  // Parent GdkWindow where we paint to, directly or via subsurface.
  RefPtr<GdkWindow> mGdkWindow;

  // Parent wl_surface owned by mGdkWindow. It's used when we're attached
  // directly to MozContainer.
  wl_surface* mParentSurface = nullptr;

  // Parent WaylandSurface.
  //
  // Layer rendering (compositor) uses mSurface directly attached to
  // wl_surface owned by mParent.
  //
  // For non-compositing rendering (old) mParent is WaylandSurface
  // owned by parent nsWindow.
  RefPtr<WaylandSurface> mParent;

  // wl_surface setup/states
  WUniquePtr<wl_surface> mSurface;
  mozilla::Atomic<bool, mozilla::Relaxed> mSurfaceNeedsCommit{false};
  bool mCommitAllowed = true;

  // When subsurface is desynced, we need to commit to parent surface
  // to see the change in subsurface (this one).
  // In such case we set mSurfaceNeedsCommit to parent for it.
  bool mSubsurfaceDesync = true;

  WUniquePtr<wl_subsurface> mSubsurface;
  DesktopIntPoint mSubsurfacePosition;

  // Wayland buffers recently attached to this surface or held by
  // Wayland compositor.
  // There may be more than one buffer attached, for instance if
  // previous buffer is hold by compositor. We need to keep
  // there buffers live until compositor notify us that we
  // can release them.
  AutoTArray<RefPtr<BufferTransaction>, 3> mBufferTransactions;
  uintptr_t mLatestAttachedBuffer = 0;

  // Indicates mSurface has buffer attached so we can attach subsurface
  // to it and expect to get frame callbacks from Wayland compositor.
  // We set it at AttachLocked() or when we get first frame callback
  // (when EGL is used).
  mozilla::Atomic<bool, mozilla::Relaxed> mBufferAttached{false};

  mozilla::Atomic<wl_egl_window*, mozilla::Relaxed> mEGLWindow{nullptr};

  bool mViewportFollowsSizeChanges = false;
  WUniquePtr<wp_viewport> mViewport;
  DesktopRect mViewportSourceRect{-1, -1, -1, -1};
  DesktopIntSize mViewportDestinationSize{-1, -1};

  // Surface flip state on X/Y asix
  bool mBufferTransformFlippedX = false;
  bool mBufferTransformFlippedY = false;

  // Frame callback for mIsVisible flag
  WUniquePtr<wl_callback> mVisibleFrameCallback;

  // VSync callback handler called every frame or by time for emulated ones.
  struct VSyncCallback {
    std::function<void(wl_callback*, uint32_t, bool)> mCb = nullptr;
    bool mEmulated = false;
    bool IsSet() const { return !!mCb; }
  };
  VSyncCallback mVSyncCallbackHandler;

  WUniquePtr<wl_callback> mVSyncFrameCallback;

  bool mVSyncCallbackEnabled = true;
  std::function<void(bool)> mVSyncCallbackStateHandler = nullptr;
  std::function<bool(void)> mVSyncEmulateCheck = nullptr;

  guint mEmulatedVSyncCallbackTimerID = 0;
  constexpr static int sEmulatedVSyncCallbackTimeoutMs = (int)(1000.0 / 60.0);

  // Frame callback used to set opaque region to wl_surface.
  WUniquePtr<wl_region> mPendingOpaqueRegion;
  WUniquePtr<wl_callback> mOpaqueRegionFrameCallback;

  // WaylandSurface is used from Compositor/Rendering/Main threads.
  mozilla::Mutex mMutex{"WaylandSurface"};
  WaylandSurfaceLock* mSurfaceLock = nullptr;

  // We may mark part of mSurface as opaque (non-transparent) if it's supported
  // by Gtk which allows compositor to skip painting of covered parts.
  mozilla::Atomic<bool, mozilla::Relaxed> mIsOpaqueSurfaceHandlerSet{false};
  gulong mGdkAfterPaintId = 0;
  static bool sIsOpaqueRegionEnabled;
  static void (*sGdkWaylandWindowAddCallbackSurface)(GdkWindow*,
                                                     struct wl_surface*);
  static void (*sGdkWaylandWindowRemoveCallbackSurface)(GdkWindow*,
                                                        struct wl_surface*);

  ScaleType mScaleType = ScaleType::Disabled;

  // mScreenScale is set from main thread only but read from
  // different threads.
  mozilla::Atomic<double, mozilla::Relaxed> mScreenScale{sNoScale};
  // Coordinates scale is in fixed-point 8.24 format.
  // Use GetCoordinatesScaleRounded() to convert it to float point.
  mozilla::Atomic<uint32_t, mozilla::Relaxed> mCoordinatesScale{1 << 24};

  // wp_fractional_scale_v1 / xx_fractional_scale_v2 works differently.
  //
  // wp_fractional_scale_v1 is needed for scale changes listener only
  // so it's optional and we don't need it for every surface.
  //
  // xx_fractional_scale_v2 is used to set coordinates scale to particular
  // surface so it must be present.
  WUniquePtr<wp_fractional_scale_v1> mFractionalScaleListener;
  WUniquePtr<xx_fractional_scale_v2> mCoordinatesScaleManager;

  // Callback issued when fractional / coordinates scale changes.
  // Ceiled (integer) scale changes is monitored by nsWindow as it's
  // tied to GtkWindow.
  std::function<void(void)> mScaleCallbacks[ScaleCallbackType::CallbackNum] = {
      nullptr, nullptr};

  bool mUseDMABufFormats = false;
  // Wayland display notifies us when available DRM formats are are changed.
  // For instance if wl_surface becomes fullscreen we may get DRM formats
  // for direct scanout.
  std::function<void(DMABufFormats*)> mDMABufFormatRefreshCallback;
  RefPtr<DMABufFormats> mFormats;

  // HDR support
  bool mHDRSet = false;
  WUniquePtr<wp_color_management_surface_v1> mColorSurface;
  WUniquePtr<wp_color_representation_surface_v1> mColorRepresentationSurface;
  WUniquePtr<wp_image_description_v1> mImageDescription;

  static void SetLuminances(wp_image_description_creator_params_v1* aParams,
                            float minLum, float maxLum, float refLum);

  static void SetContentLightLevel(
      wp_image_description_creator_params_v1* aParams,
      const mozilla::gfx::ContentLightLevel& aContentLightLevel);

  static void SetMasteringDisplayColorVolume(
      wp_image_description_creator_params_v1* aParams,
      const mozilla::gfx::Smpte2086Metadata& aSmpte2086);
};

}  // namespace mozilla::widget

#endif /* MOZ_WAYLAND_SURFACE_H_ */
