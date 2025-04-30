/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsBaseWidget_h__
#define nsBaseWidget_h__

#include "InputData.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/widget/ThemeChangeKind.h"
#include "mozilla/widget/WindowOcclusionState.h"
#include "nsRect.h"
#include "nsIWidget.h"
#include "nsWidgetsCID.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsCOMPtr.h"
#include "nsIRollupListener.h"
#include "nsIObserver.h"
#include "nsIWidgetListener.h"
#include "nsPIDOMWindow.h"
#include "nsWeakReference.h"

#include <algorithm>

class nsIContent;
class gfxContext;

namespace mozilla {
class CompositorVsyncDispatcher;
class LiveResizeListener;
class FallbackRenderer;
class SwipeTracker;
struct SwipeEventQueue;
class WidgetWheelEvent;

#ifdef ACCESSIBILITY
namespace a11y {
class LocalAccessible;
}
#endif

namespace gfx {
class DrawTarget;
class SourceSurface;
}  // namespace gfx

namespace layers {
class CompositorBridgeChild;
class CompositorBridgeParent;
class IAPZCTreeManager;
class GeckoContentController;
class APZEventState;
struct APZEventResult;
class CompositorSession;
class ImageContainer;
class WebRenderLayerManager;
struct ScrollableLayerGuid;
class RemoteCompositorSession;
}  // namespace layers

namespace widget {
class CompositorWidgetDelegate;
class InProcessCompositorWidget;
class WidgetRenderingContext;
}  // namespace widget

class CompositorVsyncDispatcher;
}  // namespace mozilla

namespace base {
class Thread;
}  // namespace base

// Windows specific constant indicating the maximum number of touch points the
// inject api will allow. This also sets the maximum numerical value for touch
// ids we can use when injecting touch points on Windows.
#define TOUCH_INJECT_MAX_POINTS 256

class nsBaseWidget;

// Helper class used in shutting down gfx related code.
class WidgetShutdownObserver final : public nsIObserver {
  ~WidgetShutdownObserver();

 public:
  explicit WidgetShutdownObserver(nsBaseWidget* aWidget);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  void Register();
  void Unregister();

  nsBaseWidget* mWidget;
  bool mRegistered;
};

// Helper class used for observing locales change.
class LocalesChangedObserver final : public nsIObserver {
  ~LocalesChangedObserver();

 public:
  explicit LocalesChangedObserver(nsBaseWidget* aWidget);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  void Register();
  void Unregister();

  nsBaseWidget* mWidget;
  bool mRegistered;
};

/**
 * Common widget implementation used as base class for native
 * or crossplatform implementations of Widgets.
 * All cross-platform behavior that all widgets need to implement
 * should be placed in this class.
 * (Note: widget implementations are not required to use this
 * class, but it gives them a head start.)
 */

class nsBaseWidget : public nsIWidget, public nsSupportsWeakReference {
  template <class EventType, class InputType>
  friend class DispatchEventOnMainThread;
  friend class mozilla::widget::InProcessCompositorWidget;
  friend class mozilla::layers::RemoteCompositorSession;

 protected:
  typedef base::Thread Thread;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::SourceSurface SourceSurface;
  typedef mozilla::layers::CompositorBridgeChild CompositorBridgeChild;
  typedef mozilla::layers::CompositorBridgeParent CompositorBridgeParent;
  typedef mozilla::layers::IAPZCTreeManager IAPZCTreeManager;
  typedef mozilla::layers::GeckoContentController GeckoContentController;
  typedef mozilla::layers::ScrollableLayerGuid ScrollableLayerGuid;
  typedef mozilla::layers::APZEventState APZEventState;
  typedef mozilla::CSSIntRect CSSIntRect;
  typedef mozilla::CSSRect CSSRect;
  typedef mozilla::ScreenRotation ScreenRotation;
  typedef mozilla::widget::CompositorWidgetDelegate CompositorWidgetDelegate;
  typedef mozilla::layers::CompositorSession CompositorSession;
  typedef mozilla::layers::ImageContainer ImageContainer;

  virtual ~nsBaseWidget();

 public:
  nsBaseWidget();

  explicit nsBaseWidget(BorderStyle aBorderStyle);

  NS_DECL_THREADSAFE_ISUPPORTS

  // nsIWidget interface
  void CaptureRollupEvents(bool aDoCapture) override {}
  nsIWidgetListener* GetWidgetListener() const override;
  void SetWidgetListener(nsIWidgetListener* alistener) override;
  void Destroy() override;
  float GetDPI() override;

  void GetWorkspaceID(nsAString& workspaceID) override;
  void MoveToWorkspace(const nsAString& workspaceID) override;
  bool IsTiled() const override { return mIsTiled; }

  bool IsFullyOccluded() const override { return mIsFullyOccluded; }

  void SetCursor(const Cursor&) override;
  void SetCustomCursorAllowed(bool) override;
  void ClearCachedCursor() final {
    mCursor = {};
    mUpdateCursor = true;
  }
  void SetTransparencyMode(TransparencyMode aMode) override;
  TransparencyMode GetTransparencyMode() override;
  void SetWindowShadowStyle(mozilla::WindowShadow) override {}
  void SetShowsToolbarButton(bool aShow) override {}
  void SetSupportsNativeFullscreen(bool aSupportsNativeFullscreen) override {}
  void SetWindowAnimationType(WindowAnimationType aType) override {}
  void HideWindowChrome(bool aShouldHide) override {}
  bool PrepareForFullscreenTransition(nsISupports** aData) override {
    return false;
  }
  void PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                   uint16_t aDuration, nsISupports* aData,
                                   nsIRunnable* aCallback) override;
  void CleanupFullscreenTransition() override {}
  already_AddRefed<Screen> GetWidgetScreen() override;
  nsresult MakeFullScreen(bool aFullScreen) override;
  void InfallibleMakeFullScreen(bool aFullScreen);

  LayersId GetLayersId() const override;
  WindowRenderer* GetWindowRenderer() override;
  bool HasWindowRenderer() const final { return !!mWindowRenderer; }

  // A remote compositor session tied to this window has been lost and IPC
  // messages will no longer work. The widget must clean up any lingering
  // resources and possibly schedule another paint.
  //
  // A reference to the session object is held until this function has
  // returned. Callers should hold a reference to the widget, since this
  // function could deallocate the widget if it is unparented.
  virtual void NotifyCompositorSessionLost(
      mozilla::layers::CompositorSession* aSession);

  already_AddRefed<mozilla::CompositorVsyncDispatcher>
  GetCompositorVsyncDispatcher();
  virtual void CreateCompositorVsyncDispatcher();
  virtual void CreateCompositor();
  virtual void CreateCompositor(int aWidth, int aHeight);
  virtual void SetCompositorWidgetDelegate(CompositorWidgetDelegate*) {}
  void PrepareWindowEffects() override {}
  void UpdateThemeGeometries(
      const nsTArray<ThemeGeometry>& aThemeGeometries) override {}
  void SetModal(bool aModal) override {}
  uint32_t GetMaxTouchPoints() const override;
  void SetWindowClass(const nsAString& xulWinType, const nsAString& xulWinClass,
                      const nsAString& xulWinName) override {}
  // Return whether this widget interprets parameters to Move and Resize APIs
  // as "desktop pixels" rather than "device pixels", and therefore
  // applies its GetDefaultScale() value to them before using them as mBounds
  // etc (which are always stored in device pixels).
  // Note that APIs that -get- the widget's position/size/bounds, rather than
  // -setting- them (i.e. moving or resizing the widget) will always return
  // values in the widget's device pixels.
  bool BoundsUseDesktopPixels() const {
    return mWindowType <= WindowType::Popup;
  }
  // Default implementation, to be overridden by platforms where desktop coords
  // are virtualized and may not correspond to device pixels on the screen.
  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() override {
    return mozilla::DesktopToLayoutDeviceScale(1.0);
  }
  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScaleByScreen()
      override;

  void ConstrainPosition(DesktopIntPoint&) override {}
  // Utility function for derived-class overrides of ConstrainPosition.
  static DesktopIntPoint ConstrainPositionToBounds(
      const DesktopIntPoint&, const mozilla::DesktopIntSize&,
      const DesktopIntRect&);
  void MoveClient(const DesktopPoint& aOffset) override;
  void ResizeClient(const DesktopSize& aSize, bool aRepaint) override;
  void ResizeClient(const DesktopRect& aRect, bool aRepaint) override;
  LayoutDeviceIntRect GetBounds() override;
  LayoutDeviceIntRect GetClientBounds() override;
  LayoutDeviceIntRect GetScreenBounds() override;
  [[nodiscard]] nsresult GetRestoredBounds(LayoutDeviceIntRect& aRect) override;
  LayoutDeviceIntPoint GetClientOffset() override;
  void EnableDragDrop(bool aEnable) override {};
  nsresult AsyncEnableDragDrop(bool aEnable) override;
  [[nodiscard]] nsresult GetAttention(int32_t aCycleCount) override {
    return NS_OK;
  }
  bool HasPendingInputEvent() override;
  void SetIcon(const nsAString& aIconSpec) override {}
  bool ShowsResizeIndicator(LayoutDeviceIntRect* aResizerRect) override;
  void FreeNativeData(void* data, uint32_t aDataType) override {}
  nsresult ActivateNativeMenuItemAt(const nsAString& indexString) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  nsresult ForceUpdateNativeMenuAt(const nsAString& indexString) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  nsresult NotifyIME(const IMENotification& aIMENotification) final;
  [[nodiscard]] nsresult AttachNativeKeyEvent(
      mozilla::WidgetKeyboardEvent& aEvent) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  bool ComputeShouldAccelerate();
  virtual bool WidgetTypeSupportsAcceleration() { return true; }
  virtual bool WidgetTypeSupportsNativeCompositing() { return true; }
  [[nodiscard]] nsresult OnDefaultButtonLoaded(
      const LayoutDeviceIntRect& aButtonRect) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  already_AddRefed<nsIWidget> CreateChild(const LayoutDeviceIntRect& aRect,
                                          InitData&) final;
  void AttachViewToTopLevel(bool aUseAttachedEvents) override;
  nsIWidgetListener* GetAttachedWidgetListener() const override;
  void SetAttachedWidgetListener(nsIWidgetListener* aListener) override;
  nsIWidgetListener* GetPreviouslyAttachedWidgetListener() override;
  void SetPreviouslyAttachedWidgetListener(nsIWidgetListener*) override;
  NativeIMEContext GetNativeIMEContext() override;
  TextEventDispatcher* GetTextEventDispatcher() final;
  TextEventDispatcherListener* GetNativeTextEventDispatcherListener() override;
  void ZoomToRect(const uint32_t& aPresShellId,
                  const ScrollableLayerGuid::ViewID& aViewId,
                  const CSSRect& aRect, const uint32_t& aFlags) override;

  // Dispatch an event that must be first be routed through APZ.
  ContentAndAPZEventStatus DispatchInputEvent(
      mozilla::WidgetInputEvent* aEvent) override;
  void DispatchEventToAPZOnly(mozilla::WidgetInputEvent* aEvent) override;

  bool DispatchWindowEvent(mozilla::WidgetGUIEvent& event) override;

  void SetConfirmedTargetAPZC(
      uint64_t aInputBlockId,
      const nsTArray<ScrollableLayerGuid>& aTargets) const override;

  void UpdateZoomConstraints(
      const uint32_t& aPresShellId, const ScrollableLayerGuid::ViewID& aViewId,
      const mozilla::Maybe<ZoomConstraints>& aConstraints) override;

  bool AsyncPanZoomEnabled() const override;

  void SwipeFinished() override;
  void ReportSwipeStarted(uint64_t aInputBlockId, bool aStartSwipe) override;
  void TrackScrollEventAsSwipe(const mozilla::PanGestureInput& aSwipeStartEvent,
                               uint32_t aAllowedDirections,
                               uint64_t aInputBlockId);
  struct SwipeInfo {
    bool wantsSwipe;
    uint32_t allowedDirections;
  };
  SwipeInfo SendMayStartSwipe(const mozilla::PanGestureInput& aSwipeStartEvent);
  // Returns a WidgetWheelEvent which needs to be handled by APZ regardless of
  // whether |aPanInput| event was used for SwipeTracker or not.
  mozilla::WidgetWheelEvent MayStartSwipeForAPZ(
      const mozilla::PanGestureInput& aPanInput,
      const mozilla::layers::APZEventResult& aApzResult);

  // Returns true if |aPanInput| event was used for SwipeTracker, false
  // otherwise.
  bool MayStartSwipeForNonAPZ(const mozilla::PanGestureInput& aPanInput);

  void NotifyWindowDestroyed();
  void NotifySizeMoveDone();

  using ByMoveToRect = nsIWidgetListener::ByMoveToRect;
  void NotifyWindowMoved(int32_t aX, int32_t aY,
                         ByMoveToRect = ByMoveToRect::No);

  // Should be called by derived implementations to notify on system color and
  // theme changes. (Only one invocation per change is needed, not one
  // invocation per change per window.)
  void NotifyThemeChanged(mozilla::widget::ThemeChangeKind);

  void NotifyAPZOfDPIChange();

#ifdef ACCESSIBILITY
  // Get the accessible for the window.
  mozilla::a11y::LocalAccessible* GetRootAccessible();
#endif

  // Return true if this is a simple widget (that is typically not worth
  // accelerating)
  bool IsSmallPopup() const;

  PopupLevel GetPopupLevel() { return mPopupLevel; }

  const SizeConstraints GetSizeConstraints() override;
  void SetSizeConstraints(const SizeConstraints& aConstraints) override;

  void StartAsyncScrollbarDrag(const AsyncDragMetrics& aDragMetrics) override;

  bool StartAsyncAutoscroll(const ScreenPoint& aAnchorLocation,
                            const ScrollableLayerGuid& aGuid) override;

  void StopAsyncAutoscroll(const ScrollableLayerGuid& aGuid) override;

  mozilla::layers::LayersId GetRootLayerTreeId() override;

  /**
   * Use this when GetLayerManager() returns a BasicLayerManager
   * (nsBaseWidget::GetLayerManager() does). This sets up the widget's
   * layer manager to temporarily render into aTarget.
   *
   * |aNaturalWidgetBounds| is the un-rotated bounds of |aWidget|.
   * |aRotation| is the "virtual rotation" to apply when rendering to
   * the target.  When |aRotation| is ROTATION_0,
   * |aNaturalWidgetBounds| is not used.
   */
  class AutoLayerManagerSetup {
   public:
    AutoLayerManagerSetup(nsBaseWidget* aWidget, gfxContext* aTarget);
    ~AutoLayerManagerSetup();

   private:
    nsBaseWidget* mWidget;
    mozilla::FallbackRenderer* mRenderer = nullptr;
  };
  friend class AutoLayerManagerSetup;

  virtual bool ShouldUseOffMainThreadCompositing();

  static nsIRollupListener* GetActiveRollupListener();

  void Shutdown();

  void QuitIME();

  // These functions should be called at the start and end of a "live" widget
  // resize (i.e. when the window contents are repainting during the resize,
  // such as when the user drags a window border). It will suppress the
  // displayport during the live resize to avoid unneccessary overpainting.
  void NotifyLiveResizeStarted();
  void NotifyLiveResizeStopped();

  void NotifyCompositorScrollUpdate(
      const mozilla::layers::CompositorScrollUpdate& aUpdate) override {};

#if defined(MOZ_WIDGET_ANDROID)
  void RecvToolbarAnimatorMessageFromCompositor(int32_t) override {};
  void RecvScreenPixels(mozilla::ipc::Shmem&& aMem, const ScreenIntSize& aSize,
                        bool aNeedsYFlip) override {};
#endif

  virtual void LocalesChanged() {}

  virtual void NotifyOcclusionState(mozilla::widget::OcclusionState aState) {}

 protected:
  // These are methods for CompositorWidgetWrapper, and should only be
  // accessed from that class. Derived widgets can choose which methods to
  // implement, or none if supporting out-of-process compositing.
  virtual bool PreRender(mozilla::widget::WidgetRenderingContext* aContext) {
    return true;
  }
  virtual void PostRender(mozilla::widget::WidgetRenderingContext* aContext) {}
  virtual RefPtr<mozilla::layers::NativeLayerRoot> GetNativeLayerRoot() {
    return nullptr;
  }
  virtual already_AddRefed<DrawTarget> StartRemoteDrawing();
  virtual already_AddRefed<DrawTarget> StartRemoteDrawingInRegion(
      const LayoutDeviceIntRegion& aInvalidRegion) {
    return StartRemoteDrawing();
  }
  virtual void EndRemoteDrawing() {}
  virtual void EndRemoteDrawingInRegion(
      DrawTarget* aDrawTarget, const LayoutDeviceIntRegion& aInvalidRegion) {
    EndRemoteDrawing();
  }
  virtual void CleanupRemoteDrawing() {}
  virtual void CleanupWindowEffects() {}
  virtual bool InitCompositor(mozilla::layers::Compositor* aCompositor) {
    return true;
  }
  virtual uint32_t GetGLFrameBufferFormat();
  virtual bool CompositorInitiallyPaused() { return false; }

 protected:
  void ResolveIconName(const nsAString& aIconName, const nsAString& aIconSuffix,
                       nsIFile** aResult);
  virtual void OnDestroy();
  void BaseCreate(nsIWidget* aParent, InitData* aInitData);

  virtual void ConfigureAPZCTreeManager();
  virtual void ConfigureAPZControllerThread();
  virtual already_AddRefed<GeckoContentController>
  CreateRootContentController();

  // Dispatch an event that has already been routed through APZ.
  nsEventStatus ProcessUntransformedAPZEvent(
      mozilla::WidgetInputEvent* aEvent,
      const mozilla::layers::APZEventResult& aApzResult);

  nsresult SynthesizeNativeKeyEvent(int32_t aNativeKeyboardLayout,
                                    int32_t aNativeKeyCode,
                                    uint32_t aModifierFlags,
                                    const nsAString& aCharacters,
                                    const nsAString& aUnmodifiedCharacters,
                                    nsIObserver* aObserver) override {
    mozilla::widget::AutoObserverNotifier notifier(aObserver, "keyevent");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeMouseEvent(LayoutDeviceIntPoint aPoint,
                                      NativeMouseMessage aNativeMessage,
                                      mozilla::MouseButton aButton,
                                      nsIWidget::Modifiers aModifierFlags,
                                      nsIObserver* aObserver) override {
    mozilla::widget::AutoObserverNotifier notifier(aObserver, "mouseevent");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeMouseMove(LayoutDeviceIntPoint aPoint,
                                     nsIObserver* aObserver) override {
    mozilla::widget::AutoObserverNotifier notifier(aObserver, "mouseevent");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
      uint32_t aAdditionalFlags, nsIObserver* aObserver) override {
    mozilla::widget::AutoObserverNotifier notifier(aObserver,
                                                   "mousescrollevent");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeTouchPoint(uint32_t aPointerId,
                                      TouchPointerState aPointerState,
                                      LayoutDeviceIntPoint aPoint,
                                      double aPointerPressure,
                                      uint32_t aPointerOrientation,
                                      nsIObserver* aObserver) override {
    mozilla::widget::AutoObserverNotifier notifier(aObserver, "touchpoint");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeTouchPadPinch(TouchpadGesturePhase aEventPhase,
                                         float aScale,
                                         LayoutDeviceIntPoint aPoint,
                                         int32_t aModifierFlags) override {
    MOZ_RELEASE_ASSERT(
        false, "This method is not implemented on the current platform");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativePenInput(uint32_t aPointerId,
                                    TouchPointerState aPointerState,
                                    LayoutDeviceIntPoint aPoint,
                                    double aPressure, uint32_t aRotation,
                                    int32_t aTiltX, int32_t aTiltY,
                                    int32_t aButton,
                                    nsIObserver* aObserver) override {
    MOZ_RELEASE_ASSERT(
        false, "This method is not implemented on the current platform");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeTouchpadDoubleTap(LayoutDeviceIntPoint aPoint,
                                             uint32_t aModifierFlags) override {
    MOZ_RELEASE_ASSERT(
        false, "This method is not implemented on the current platform");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult SynthesizeNativeTouchpadPan(TouchpadGesturePhase aEventPhase,
                                       LayoutDeviceIntPoint aPoint,
                                       double aDeltaX, double aDeltaY,
                                       int32_t aModifierFlags,
                                       nsIObserver* aObserver) override {
    MOZ_RELEASE_ASSERT(
        false, "This method is not implemented on the current platform");
    return NS_ERROR_UNEXPECTED;
  }

  /**
   * GetPseudoIMEContext() returns pseudo IME context when TextEventDispatcher
   * has non-native input transaction.  Otherwise, returns nullptr.
   */
  void* GetPseudoIMEContext();

 protected:
  virtual already_AddRefed<nsIWidget> AllocateChildPopupWidget() {
    return nsIWidget::CreateChildWindow();
  }

  WindowRenderer* CreateFallbackRenderer();

  PopupType GetPopupType() const { return mPopupType; }

  bool HasRemoteContent() const { return mHasRemoteContent; }

  /**
   * Apply the current size constraints to the given size.
   *
   * @param aWidth width to constrain
   * @param aHeight height to constrain
   */
  void ConstrainSize(int32_t* aWidth, int32_t* aHeight) override {
    SizeConstraints c = GetSizeConstraints();
    *aWidth = std::clamp(*aWidth, c.mMinSize.width, c.mMaxSize.width);
    *aHeight = std::clamp(*aHeight, c.mMinSize.height, c.mMaxSize.height);
  }

  CompositorBridgeChild* GetRemoteRenderer() override;

  void ClearCachedWebrenderResources() override;

  void ClearWebrenderAnimationResources() override;

  bool SetNeedFastSnaphot() override;

  /**
   * Notify the widget that this window is being used with OMTC.
   */
  virtual void WindowUsesOMTC() {}
  virtual void RegisterTouchWindow() {}

  mozilla::dom::Document* GetDocument() const;

  void EnsureTextEventDispatcher();

  // Notify the compositor that a device reset has occurred.
  void OnRenderingDeviceReset();

  bool UseAPZ() const;

  bool AllowWebRenderForThisWindow();

  /**
   * For widgets that support synthesizing native touch events, this function
   * can be used to manage the current state of synthetic pointers. Each widget
   * must maintain its own MultiTouchInput instance and pass it in as the state,
   * along with the desired parameters for the changes. This function returns
   * a new MultiTouchInput object that is ready to be dispatched.
   */
  mozilla::MultiTouchInput UpdateSynthesizedTouchState(
      mozilla::MultiTouchInput* aState, mozilla::TimeStamp aTimeStamp,
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPointerPressure,
      uint32_t aPointerOrientation);

  /**
   * Dispatch the given MultiTouchInput through APZ to Gecko (if APZ is enabled)
   * or directly to gecko (if APZ is not enabled). This function must only
   * be called from the main thread, and if APZ is enabled, that must also be
   * the APZ controller thread.
   */
  void DispatchTouchInput(mozilla::MultiTouchInput& aInput);

  /**
   * Dispatch the given PanGestureInput through APZ to Gecko (if APZ is enabled)
   * or directly to gecko (if APZ is not enabled). This function must only
   * be called from the main thread, and if APZ is enabled, that must also be
   * the APZ controller thread.
   */
  void DispatchPanGestureInput(mozilla::PanGestureInput& aInput);
  void DispatchPinchGestureInput(mozilla::PinchGestureInput& aInput);

  static bool ConvertStatus(nsEventStatus aStatus) {
    return aStatus == nsEventStatus_eConsumeNoDefault;
  }

 protected:
  // Returns whether compositing should use an external surface size.
  virtual bool UseExternalCompositingSurface() const { return false; }

  /**
   * Starts the OMTC compositor destruction sequence.
   *
   * When this function returns, the compositor should not be
   * able to access the opengl context anymore.
   * It is safe to call it several times if platform implementations
   * require the compositor to be destroyed before ~nsBaseWidget is
   * reached (This is the case with gtk2 for instance).
   */
  virtual void DestroyCompositor();
  void DestroyLayerManager();
  void ReleaseContentController();
  void RevokeTransactionIdAllocator();

  void FreeShutdownObserver();
  void FreeLocalesChangedObserver();

  bool IsPIPWindow() const { return mIsPIPWindow; };

  nsIWidgetListener* mWidgetListener;
  nsIWidgetListener* mAttachedWidgetListener;
  nsIWidgetListener* mPreviouslyAttachedWidgetListener;
  RefPtr<WindowRenderer> mWindowRenderer;
  RefPtr<CompositorSession> mCompositorSession;
  RefPtr<CompositorBridgeChild> mCompositorBridgeChild;

  mozilla::UniquePtr<mozilla::Mutex> mCompositorVsyncDispatcherLock;
  RefPtr<mozilla::CompositorVsyncDispatcher> mCompositorVsyncDispatcher;

  RefPtr<IAPZCTreeManager> mAPZC;
  RefPtr<GeckoContentController> mRootContentController;
  RefPtr<APZEventState> mAPZEventState;
  RefPtr<WidgetShutdownObserver> mShutdownObserver;
  RefPtr<LocalesChangedObserver> mLocalesChangedObserver;
  RefPtr<TextEventDispatcher> mTextEventDispatcher;
  RefPtr<mozilla::SwipeTracker> mSwipeTracker;
  mozilla::UniquePtr<mozilla::SwipeEventQueue> mSwipeEventQueue;
  Cursor mCursor;
  bool mCustomCursorAllowed = true;
  BorderStyle mBorderStyle;
  LayoutDeviceIntRect mBounds;
  bool mIsTiled;
  PopupLevel mPopupLevel;
  PopupType mPopupType;
  SizeConstraints mSizeConstraints;
  bool mHasRemoteContent;

  struct FullscreenSavedState {
    DesktopRect windowRect;
    DesktopRect screenRect;
  };
  mozilla::Maybe<FullscreenSavedState> mSavedBounds;

  bool mUpdateCursor;
  bool mUseAttachedEvents;
  bool mIMEHasFocus;
  bool mIMEHasQuit;
  // if the window is fully occluded (rendering may be paused in response)
  bool mIsFullyOccluded;
  bool mNeedFastSnaphot;
  // This flag is only used when APZ is off. It indicates that the current pan
  // gesture was processed as a swipe. Sometimes the swipe animation can finish
  // before momentum events of the pan gesture have stopped firing, so this
  // flag tells us that we shouldn't allow the remaining events to cause
  // scrolling. It is reset to false once a new gesture starts (as indicated by
  // a PANGESTURE_(MAY)START event).
  bool mCurrentPanGestureBelongsToSwipe;

  // It's PictureInPicture window.
  bool mIsPIPWindow : 1;

  struct InitialZoomConstraints {
    InitialZoomConstraints(const uint32_t& aPresShellID,
                           const ScrollableLayerGuid::ViewID& aViewID,
                           const ZoomConstraints& aConstraints)
        : mPresShellID(aPresShellID),
          mViewID(aViewID),
          mConstraints(aConstraints) {}

    uint32_t mPresShellID;
    ScrollableLayerGuid::ViewID mViewID;
    ZoomConstraints mConstraints;
  };

  mozilla::Maybe<InitialZoomConstraints> mInitialZoomConstraints;

  // This points to the resize listeners who have been notified that a live
  // resize is in progress. This should always be empty when a live-resize is
  // not in progress.
  nsTArray<RefPtr<mozilla::LiveResizeListener>> mLiveResizeListeners;

#ifdef DEBUG
 protected:
  static void debug_DumpInvalidate(FILE* aFileOut, nsIWidget* aWidget,
                                   const LayoutDeviceIntRect* aRect,
                                   const char* aWidgetName, int32_t aWindowID);

  static void debug_DumpEvent(FILE* aFileOut, nsIWidget* aWidget,
                              mozilla::WidgetGUIEvent* aGuiEvent,
                              const char* aWidgetName, int32_t aWindowID);

  static void debug_DumpPaintEvent(FILE* aFileOut, nsIWidget* aWidget,
                                   const nsIntRegion& aPaintEvent,
                                   const char* aWidgetName, int32_t aWindowID);

  static bool debug_GetCachedBoolPref(const char* aPrefName);
#endif

 private:
  already_AddRefed<mozilla::layers::WebRenderLayerManager>
  CreateCompositorSession(int aWidth, int aHeight,
                          mozilla::layers::CompositorOptions* aOptionsOut);
};

#endif  // nsBaseWidget_h__
