/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsWindow_h_
#define _nsWindow_h_

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "CompositorWidget.h"
#include "IMContextWrapper.h"
#include "LookAndFeel.h"
#include "MozContainer.h"
#include "VsyncSource.h"
#include "WaylandSurfaceLock.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/RWLock.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/BaseMargin.h"
#include "mozilla/widget/WindowSurface.h"
#include "mozilla/widget/WindowSurfaceProvider.h"
#include "nsIDragService.h"
#include "nsIWidget.h"
#include "nsRefPtrHashtable.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/LocalAccessible.h"
#endif
#ifdef MOZ_X11
#  include <gdk/gdkx.h>

#  include "X11UndefineNone.h"
#endif

#ifdef MOZ_LOGGING
#  undef LOG
#  undef LOGVERBOSE

#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"

extern mozilla::LazyLogModule gWidgetLog;
extern mozilla::LazyLogModule gWidgetDragLog;
extern mozilla::LazyLogModule gWidgetPopupLog;
extern mozilla::LazyLogModule gWidgetVsync;
extern mozilla::LazyLogModule gWidgetWaylandLog;

#  define LOG_WIN(win, str, ...)                           \
    MOZ_LOG(win->IsPopup() ? gWidgetPopupLog : gWidgetLog, \
            mozilla::LogLevel::Debug,                      \
            ("%s: " str, win->GetDebugTag().get(), ##__VA_ARGS__))
#  define LOG(...) LOG_WIN(this, __VA_ARGS__)
#  define LOGVERBOSE(str, ...)                        \
    MOZ_LOG(IsPopup() ? gWidgetPopupLog : gWidgetLog, \
            mozilla::LogLevel::Verbose,               \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGW(...) MOZ_LOG(gWidgetLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#  define LOGDRAG(...) \
    MOZ_LOG(gWidgetDragLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#  define LOG_POPUP(...) \
    MOZ_LOG(gWidgetPopupLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#  define LOG_VSYNC(str, ...)                       \
    MOZ_LOG(gWidgetVsync, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOG_ENABLED()                                         \
    (MOZ_LOG_TEST(gWidgetPopupLog, mozilla::LogLevel::Debug) || \
     MOZ_LOG_TEST(gWidgetLog, mozilla::LogLevel::Debug))
#  define LOG_ENABLED_VERBOSE()                                   \
    (MOZ_LOG_TEST(gWidgetPopupLog, mozilla::LogLevel::Verbose) || \
     MOZ_LOG_TEST(gWidgetLog, mozilla::LogLevel::Verbose))
#  define LOG_WAYLAND(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))

#else

#  define LOG(...)
#  define LOG_WIN(...)
#  define LOGVERBOSE(...)
#  define LOGW(...)
#  define LOGDRAG(...)
#  define LOG_POPUP(...)
#  define LOG_ENABLED() false

#endif /* MOZ_LOGGING */

#if defined(MOZ_WAYLAND) && !defined(MOZ_X11)
typedef uintptr_t Window;
#endif

class gfxPattern;
class nsIFrame;
class nsMenuPopupFrame;
#if !GTK_CHECK_VERSION(3, 18, 0)
struct _GdkEventTouchpadPinch;
typedef struct _GdkEventTouchpadPinch GdkEventTouchpadPinch;
#endif

// 'Stable' Wayland subsurface rounding algorithm is used by all compositors
// except KDE.
// See
// https://gitlab.freedesktop.org/wayland/wayland-protocols/-/merge_requests/143#note_1343171
// for details.
// We want to replace it by wp-fractional-scale-v2 when it's available.
extern bool gUseStableRounding;

#if !GTK_CHECK_VERSION(3, 22, 0)
typedef enum {
  GDK_ANCHOR_FLIP_X = 1 << 0,
  GDK_ANCHOR_FLIP_Y = 1 << 1,
  GDK_ANCHOR_SLIDE_X = 1 << 2,
  GDK_ANCHOR_SLIDE_Y = 1 << 3,
  GDK_ANCHOR_RESIZE_X = 1 << 4,
  GDK_ANCHOR_RESIZE_Y = 1 << 5,
  GDK_ANCHOR_FLIP = GDK_ANCHOR_FLIP_X | GDK_ANCHOR_FLIP_Y,
  GDK_ANCHOR_SLIDE = GDK_ANCHOR_SLIDE_X | GDK_ANCHOR_SLIDE_Y,
  GDK_ANCHOR_RESIZE = GDK_ANCHOR_RESIZE_X | GDK_ANCHOR_RESIZE_Y
} GdkAnchorHints;
#endif

#if !GTK_CHECK_VERSION(3, 18, 0)
typedef enum {
  GDK_TOUCHPAD_GESTURE_PHASE_BEGIN,
  GDK_TOUCHPAD_GESTURE_PHASE_UPDATE,
  GDK_TOUCHPAD_GESTURE_PHASE_END,
  GDK_TOUCHPAD_GESTURE_PHASE_CANCEL
} GdkTouchpadGesturePhase;
#endif

struct zwp_locked_pointer_v1;
struct zwp_relative_pointer_v1;

namespace mozilla {
enum class NativeKeyBindingsType : uint8_t;

class TimeStamp;
class WaylandVsyncSource;
#ifdef MOZ_X11
class CurrentX11TimeGetter;
#endif

namespace widget {
class DBusMenuBar;
class Screen;
class WaylandSurface;
class WaylandSurfaceLock;
class nsWindowX11;
class nsWindowWayland;
class nsWindowWaylandToplevel;
class nsWindowWaylandPopup;
}  // namespace widget
}  // namespace mozilla

class gfxImageSurface;

class nsWindow : public nsIWidget {
 public:
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::WidgetEventTime WidgetEventTime;
  typedef mozilla::WidgetKeyboardEvent WidgetKeyboardEvent;
  typedef mozilla::widget::PlatformCompositorWidgetDelegate
      PlatformCompositorWidgetDelegate;

  nsWindow();

  static void ReleaseGlobals();

  NS_INLINE_DECL_REFCOUNTING_INHERITED(nsWindow, nsIWidget)

  virtual mozilla::widget::nsWindowWayland* AsWayland() { return nullptr; }
  virtual mozilla::widget::nsWindowWaylandPopup* AsWaylandPopup() {
    return nullptr;
  }
  virtual mozilla::widget::nsWindowWaylandToplevel* AsWaylandToplevel() {
    return nullptr;
  }
  virtual mozilla::widget::nsWindowX11* AsX11() { return nullptr; }

  // called when we are destroyed
  void OnDestroy() override;

  // called to check and see if a widget's dimensions are sane
  bool AreBoundsSane();

  // nsIWidget
  using nsIWidget::Create;  // for Create signature not overridden here
  [[nodiscard]] nsresult Create(nsIWidget* aParent,
                                const LayoutDeviceIntRect& aRect,
                                const InitData&) override;
  void Destroy() override;
  double GetDefaultScaleInternal() override;
  uint32_t GetMaxTouchPoints() const override;
  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() const override;
  void SetModal(bool aModal) override;
  bool IsVisible() const override;
  void ConstrainPosition(DesktopIntPoint&) override;
  void SetSizeConstraints(const SizeConstraints&) override;
  void LockAspectRatio(bool aShouldLock) override;
  void Move(const DesktopPoint&) override;
  void Show(bool aState) override;
  void Resize(const DesktopSize&, bool aRepaint) override;
  void Resize(const DesktopRect&, bool aRepaint) override;
  bool IsEnabled() const override;

  nsSizeMode GetSizeMode() const { return mSizeMode; }
  nsSizeMode SizeMode() override { return mSizeMode; }
  void SetSizeMode(nsSizeMode aMode) override;
  void Enable(bool aState) override;
  void SetFocus(Raise, mozilla::dom::CallerType aCallerType) override;
  LayoutDeviceIntRect GetBounds() override;
  LayoutDeviceIntRect GetScreenBounds() override;
  DesktopIntRect GetClientArea() const { return mClientArea; }
  DesktopIntRect GetScreenBoundsUnscaled();
  LayoutDeviceIntRect GetClientBounds() override;
  LayoutDeviceIntSize GetClientSize() override;
  LayoutDeviceIntPoint GetClientOffset() override;
  LayoutDeviceIntPoint GetScreenEdgeSlop() override;
  nsresult GetRestoredBounds(LayoutDeviceIntRect&) override;
  bool PersistClientBounds() const override { return true; }
  LayoutDeviceIntMargin NormalSizeModeClientToWindowMargin() override;

  bool WorkspaceManagementDisabled();
  bool ConstrainSizeWithScale(int* aWidth, int* aHeight, double aScale);

  // Recomputes the bounds according to our current window position. Dispatches
  // move / resizes as needed.
  void RecomputeBounds(bool aScaleChange = false);
  // Window bounds (as in GetBounds()) are composed as
  // mClientArea.Inflate(mClientMargin)*scale, i.e.:
  //
  // mBounds.x = (mClientArea.x - mClientMargin.left) * scale;
  // mBounds.y = (mClientArea.y - mClientMargin.top) * scale;
  // mBounds.width = (mClientArea.width +
  //                 (mClientMargin.right + mClientMargin.left)) * scale;
  // mBounds.height = (mClientArea.height +
  //                  (mClientMargin.top + mClientMargin.bottom)) * scale;
  //
  // We use mClientMargin and mClientArea in Gdk (logical, widget) coordinates
  // instead of device pixel coordinates to avoid rounding errors.
  struct Bounds {
    // mClientArea is window rendering area in global coordinates.
    DesktopIntRect mClientArea;
    // mClientMargin contains CSD decorations size on Wayland and
    // CSD decorations and system titlebar size on X11.
    DesktopIntMargin mClientMargin;

    static Bounds Compute(const nsWindow*);
#ifdef MOZ_X11
    static Bounds ComputeX11(const nsWindow*);
#endif
#ifdef MOZ_WAYLAND
    static Bounds ComputeWayland(const nsWindow*);
#endif
    static DesktopIntMargin ComputeCSDMargin(GtkWidget* aShell,
                                             GtkWidget* aChild);
  };
  void SchedulePendingBounds();
  void MaybeRecomputeBounds();

  void SetCursor(const Cursor&) override;
  void Invalidate(const LayoutDeviceIntRect& aRect) override;
  void* GetNativeData(uint32_t aDataType) override;
  nsresult SetTitle(const nsAString& aTitle) override;
  void SetIcon(const nsAString& aIconSpec) override;
  void SetWindowClass(const nsAString& xulWinType, const nsAString& xulWinClass,
                      const nsAString& xulWinName) override;
  LayoutDeviceIntPoint WidgetToScreenOffset() override;
  DesktopIntPoint WidgetToScreenOffsetUnscaled();
  void CaptureRollupEvents(bool aDoCapture) override;
  [[nodiscard]] nsresult GetAttention(int32_t aCycleCount) override;
  bool HasPendingInputEvent() override;

  bool PrepareForFullscreenTransition(nsISupports** aData) override;
  void PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                   uint16_t aDuration, nsISupports* aData,
                                   nsIRunnable* aCallback) override;
  already_AddRefed<mozilla::widget::Screen> GetWidgetScreen() override;
  nsresult MakeFullScreen(bool aFullScreen) override;
  void HideWindowChrome(bool aShouldHide) override;

  /**
   * GetLastUserInputTime returns a timestamp for the most recent user input
   * event.  This is intended for pointer grab requests (including drags).
   */
  static guint32 GetLastUserInputTime();

  // utility method, -1 if no change should be made, otherwise returns a
  // value that can be passed to gdk_window_set_decorations
  gint ConvertBorderStyles(BorderStyle aStyle);

  mozilla::widget::IMContextWrapper* GetIMContext() const { return mIMContext; }

  bool DispatchCommandEvent(nsAtom* aCommand);

  /**
   * Return true if the command is enabled or the target is in a remote content.
   * Return false if the command is disabled.
   * Return error if TextEventDispatcher is not available or the event handler
   * returns error.
   */
  mozilla::Result<bool, nsresult> DispatchContentCommandEvent(
      mozilla::EventMessage);

  // event callbacks
  gboolean OnExposeEvent(cairo_t* cr);
  gboolean OnShellConfigureEvent(GdkEventConfigure* aEvent);
  void OnContainerSizeAllocate(GtkAllocation* aAllocation);
  void OnMap();
  void OnUnmap();
  void OnDeleteEvent();
  void OnEnterNotifyEvent(GdkEventCrossing* aEvent);
  void OnLeaveNotifyEvent(GdkEventCrossing* aEvent);
  void OnMotionNotifyEvent(GdkEventMotion* aEvent);
  void OnButtonPressEvent(GdkEventButton* aEvent);
  void OnButtonReleaseEvent(GdkEventButton* aEvent);
  void OnContainerFocusInEvent(GdkEventFocus* aEvent);
  void OnContainerFocusOutEvent(GdkEventFocus* aEvent);
  gboolean OnKeyPressEvent(GdkEventKey* aEvent);
  gboolean OnKeyReleaseEvent(GdkEventKey* aEvent);

  void OnScrollEvent(GdkEventScroll* aEvent);
  void OnSmoothScrollEvent(uint32_t aTime, float aDeltaX, float aDeltaY);

  void OnVisibilityNotifyEvent(GdkVisibilityState aState);
  void OnWindowStateEvent(GtkWidget* aWidget, GdkEventWindowState* aEvent);
  gboolean OnPropertyNotifyEvent(GtkWidget* aWidget, GdkEventProperty* aEvent);
  gboolean OnTouchEvent(GdkEventTouch* aEvent);
  gboolean OnTouchpadPinchEvent(GdkEventTouchpadPinch* aEvent);
  void OnTouchpadHoldEvent(GdkTouchpadGesturePhase aPhase, guint aTime,
                           uint32_t aFingers);

  gint GetInputRegionMarginInGdkCoords();

  void UpdateOpaqueRegionInternal();
  void UpdateOpaqueRegion(const LayoutDeviceIntRegion&) override;
  LayoutDeviceIntRegion GetOpaqueRegionForTesting() const override {
    return GetOpaqueRegion();
  }
  LayoutDeviceIntRegion GetOpaqueRegion() const;

  // Exports a handle to the window, see `gdk_wayland_window_export_handle`.
  using ExportHandlePromise =
      mozilla::MozPromise<nsCString, bool, /*IsExclusive = */ true>;
  RefPtr<ExportHandlePromise> ExportHandle();
  void UnexportHandle();

  already_AddRefed<mozilla::gfx::DrawTarget> StartRemoteDrawingInRegion(
      const LayoutDeviceIntRegion& aInvalidRegion) override;
  void EndRemoteDrawingInRegion(
      mozilla::gfx::DrawTarget* aDrawTarget,
      const LayoutDeviceIntRegion& aInvalidRegion) override;

  virtual void SetProgress(unsigned long progressPercent) {}

  bool SynchronouslyRepaintOnResize() override;

  void OnDPIChanged();
  void OnCheckResize();
  void OnCompositedChanged();
  void DispatchResized();
  void OnScaleEvent();

  // Load new scale from system.

  // aRefreshScreen means notify layout that scale was changed and
  //  it should load correct values.
  // Set aRefreshScreen to false if we operate on hidden window
  // or if we're going to repaint.
  // aForceRefresh is used when fractional scale is changed but
  // ceiled scale is kept.
  void RefreshScale(bool aRefreshScreen, bool aForceRefresh = false);

  static guint32 sLastButtonPressTime;

  MozContainer* GetMozContainer() { return mContainer; }
  GdkWindow* GetGdkWindow() const { return mGdkWindow; }
  void SetGdkWindow(GdkWindow* aGdkWindow);
  GdkWindow* GetToplevelGdkWindow() const;
  GtkWidget* GetGtkWidget() const { return mShell; }
#ifdef MOZ_WAYLAND
  RefPtr<mozilla::widget::WaylandSurface> GetWaylandSurface() {
    return mSurface;
  }
#endif
  bool IsDestroyed() const { return mIsDestroyed; }
  bool IsPopup() const;
  bool IsWaylandPopup() const;
  bool IsDragPopup() { return mIsDragPopup; };

  nsAutoCString GetDebugTag() const;

  void DispatchDragEvent(mozilla::EventMessage aMsg,
                         const LayoutDeviceIntPoint& aRefPoint, guint aTime);
  static void UpdateDragStatus(GdkDragContext* aDragContext,
                               nsIDragService* aDragService);
  void SetDragSource(GdkDragContext* aSourceDragContext);

  WidgetEventTime GetWidgetEventTime(guint32 aEventTime);
  mozilla::TimeStamp GetEventTimeStamp(guint32 aEventTime);
#ifdef MOZ_X11
  mozilla::CurrentX11TimeGetter* GetCurrentTimeGetter();
#endif

  void SetInputContext(const InputContext& aContext,
                       const InputContextAction& aAction) override;
  InputContext GetInputContext() override;
  TextEventDispatcherListener* GetNativeTextEventDispatcherListener() override;
  MOZ_CAN_RUN_SCRIPT bool GetEditCommands(
      mozilla::NativeKeyBindingsType aType,
      const mozilla::WidgetKeyboardEvent& aEvent,
      nsTArray<mozilla::CommandInt>& aCommands) override;

  void SetTransparencyMode(TransparencyMode aMode) override;
  TransparencyMode GetTransparencyMode() override;
  void SetInputRegion(const InputRegion&) override;

  nsresult SynthesizeNativeMouseEvent(
      LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
      mozilla::MouseButton aButton, nsIWidget::NativeModifiers aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeMouseMove(
      LayoutDeviceIntPoint aPoint,
      nsISynthesizedEventCallback* aCallback) override {
    return SynthesizeNativeMouseEvent(
        aPoint, NativeMouseMessage::Move, mozilla::MouseButton::eNotPressed,
        nsIWidget::NativeModifiers::NO_MODIFIERS, aCallback);
  }

  nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, nsIWidget::NativeModifiers aModifierFlags,
      uint32_t aAdditionalFlags,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchPoint(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPointerPressure,
      uint32_t aPointerOrientation,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchPadPinch(TouchpadGesturePhase aEventPhase,
                                         float aScale,
                                         LayoutDeviceIntPoint aPoint,
                                         int32_t aModifierFlags) override;

  nsresult SynthesizeNativeTouchpadPan(
      TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
      double aDeltaX, double aDeltaY, int32_t aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;

  void GetCompositorWidgetInitData(
      mozilla::widget::CompositorWidgetInitData* aInitData) override;

  void SetCustomTitlebar(bool) override;
  void UpdateWindowDraggingRegion(
      const LayoutDeviceIntRegion& aRegion) override;

#ifdef MOZ_ENABLE_DBUS
  void SetDBusMenuBar(RefPtr<mozilla::widget::DBusMenuBar> aDbusMenuBar);
#endif

  // HiDPI scale conversion
  gint GdkCeiledScaleFactor();
  double FractionalScaleFactor() const;

  LayoutDeviceIntPoint ToLayoutDevicePixels(const DesktopIntPoint&);
  LayoutDeviceIntSize ToLayoutDevicePixels(const DesktopIntSize&);
  LayoutDeviceIntRect ToLayoutDevicePixels(const DesktopIntRect&);
  LayoutDeviceIntMargin ToLayoutDevicePixels(const DesktopIntMargin&);
  DesktopIntSize ToDesktopPixels(const LayoutDeviceIntSize&);
  DesktopIntRect ToDesktopPixels(const LayoutDeviceIntRect&);
  DesktopIntPoint ToDesktopPixels(const LayoutDeviceIntPoint&);

  // To GDK
  gint DevicePixelsToGdkCoordRound(int);

  gint DevicePixelsToGdkCoordRoundDown(int);
  GdkPoint DevicePixelsToGdkPointRoundDown(const LayoutDeviceIntPoint&);
  GdkRectangle DevicePixelsToGdkRectRoundOut(const LayoutDeviceIntRect&);
  GdkRectangle DevicePixelsToGdkRectRoundIn(const LayoutDeviceIntRect&);

  // From GDK
  LayoutDeviceIntPoint GdkPointToDevicePixels(const GdkPoint&);
  LayoutDeviceIntPoint GdkEventCoordsToDevicePixels(gdouble aX, gdouble aY);

  bool WidgetTypeSupportsAcceleration() override;
  bool WidgetTypeSupportsNativeCompositing() override;

  nsresult SetSystemFont(const nsCString& aFontName) override;
  nsresult GetSystemFont(nsCString& aFontName) override;

  typedef enum {
    GTK_DECORATION_SYSTEM,  // CSD including shadows
    GTK_DECORATION_CLIENT,  // CSD without shadows
    GTK_DECORATION_NONE,    // WM does not support CSD at all
  } GtkWindowDecoration;
  /**
   * Get the support of Client Side Decoration by checking the desktop
   * environment.
   */
  static GtkWindowDecoration GetSystemGtkWindowDecoration();

  bool IsRemoteContent() const { return HasRemoteContent(); }

  static bool IsToplevelWindowTransparent();

  static nsWindow* GetFocusedWindow();

  mozilla::UniquePtr<mozilla::widget::WaylandSurfaceLock> LockSurface();

  void NotifyOcclusionState(mozilla::widget::OcclusionState aState) override;

  static nsWindow* GetWindow(GdkWindow* window);

  /**
   * Dispatch accessible window activate event for the top level window
   * accessible.
   */
  void DispatchActivateEventAccessible();

  void GtkWidgetDestroyHandler(GtkWidget* aWidget);

  void SetDragPopupSurface(RefPtr<gfxImageSurface> aDragPopupSurface,
                           const LayoutDeviceIntRegion& aInvalidRegion);

  static nsWindow* FromGtkWidget(GtkWidget* widget);
  static nsWindow* FromGdkWindow(GdkWindow* window);

  static nsWindow* FromWidget(nsIWidget* aWidget) {
    if (aWidget && aWidget->IsNativeWidget()) {
      return static_cast<nsWindow*>(aWidget);
    }
    return nullptr;
  }
  static nsWindow* FromWidget(nsWindow*) = delete;

  void SetTextInputArea(LayoutDeviceIntRect aCursorArea);
  DesktopIntRect GetTextInputArea() { return mIMContextInputArea; };
  void UnlockCursor() { mWidgetCursorLocked = false; };
  void InsertEmoji(RefPtr<nsWindow> aToplevelWindow = nullptr);

  static void SessionRestoreFinished();

 protected:
  virtual ~nsWindow();

  virtual void CreateNative() {}
  virtual void DestroyNative() {}

  void ConfigureToplevelWindow();
  virtual void ConfigureToplevelWindowNative() {}

  // Native implementation of OnMap() event for X11/Wayland.
  virtual void OnMapNative() {}

  virtual void EnableVSyncSource() {}
  virtual void DisableVSyncSource() {}
  virtual void WatchHiddenVSyncSource() {}

  // event handling code
  void DispatchActivateEvent(void);
  void DispatchDeactivateEvent(void);
  void DispatchPanGesture(mozilla::PanGestureInput& aPanInput);

  void RegisterTouchWindow() override;

  void NativeMoveResize(bool aMoved, bool aResized);

  virtual void NativeShow(bool aAction) = 0;
  void SetHasMappedToplevel(bool aState);

  bool SetSafeWindowSize(LayoutDeviceIntSize& aSize);
  bool SetSafeWindowSize(DesktopIntSize& aSize);

  void DispatchContextMenuEventFromMouseEvent(
      uint16_t domButton, GdkEventButton* aEvent,
      const mozilla::LayoutDeviceIntPoint& aRefPoint);

  void TryToShowNativeWindowMenu(GdkEventButton* aEvent);

  bool DoTitlebarAction(mozilla::LookAndFeel::TitlebarEvent aEvent,
                        GdkEventButton* aButtonEvent);

  void DestroyChildWindows();
  nsWindow* GetContainerWindow() const;
  Window GetX11Window();
  void SetUrgencyHint(GtkWidget* top_window, bool state);
  void SetDefaultIcon(void);
  void SetWindowDecoration(BorderStyle aStyle);
  void InitButtonEvent(mozilla::WidgetMouseEvent& aEvent,
                       GdkEventButton* aGdkEvent,
                       const mozilla::LayoutDeviceIntPoint& aRefPoint,
                       bool isEraser = false);
  bool CheckForRollup(gdouble aMouseX, gdouble aMouseY, bool aIsWheel,
                      bool aAlwaysRollup);
  void RollupAllMenus() { CheckForRollup(0, 0, false, true); }
  void CheckForRollupDuringGrab() { RollupAllMenus(); }

  bool GetDragInfo(mozilla::WidgetMouseEvent* aMouseEvent, GdkWindow** aWindow,
                   gint* aButton, gint* aRootX, gint* aRootY);

  nsWindow* GetTransientForWindowIfPopup();
  bool IsHandlingTouchSequence(GdkEventSequence* aSequence);

  void ResizeInt(const mozilla::Maybe<DesktopIntPoint>& aMove,
                 DesktopIntSize aSize);

  // Returns a window edge if the given point (in device pixels) is within a
  // resizer region of the window.
  // Only used when drawing decorations client side.
  mozilla::Maybe<GdkWindowEdge> CheckResizerEdge(const LayoutDeviceIntPoint&);

  GtkTextDirection GetTextDirection();

  bool DrawsToCSDTitlebar() const;
  bool ToplevelUsesCSD() const;

  void CreateAndPutGdkScrollEvent(mozilla::LayoutDeviceIntPoint aPoint,
                                  double aDeltaX, double aDeltaY);

  bool IsFullscreenSupported();

  nsCString mGtkWindowAppClass;
  nsCString mGtkWindowAppName;
  nsCString mGtkWindowRoleName;
  void RefreshWindowClass();

  GtkWidget* mShell = nullptr;
  MozContainer* mContainer = nullptr;
  GdkWindow* mGdkWindow = nullptr;
  // mEGLWindow is owned by WaylandSurface or it's X11 ID.
  void* mEGLWindow = nullptr;
#ifdef MOZ_WAYLAND
  RefPtr<mozilla::widget::WaylandSurface> mSurface;
#endif
  RefPtr<gfxImageSurface> mDragPopupSurface;
  LayoutDeviceIntRegion mDragPopupSurfaceRegion;

  PlatformCompositorWidgetDelegate* mCompositorWidgetDelegate = nullptr;

  // The actual size mode that's in effect.
  nsSizeMode mSizeMode = nsSizeMode_Normal;
  // The last size mode we've requested. This might not match mSizeMode if
  // there's a request to change the size mode in progress.
  nsSizeMode mLastSizeModeRequest = nsSizeMode_Normal;
  nsSizeMode mLastSizeModeBeforeFullscreen = nsSizeMode_Normal;

  float mAspectRatio = 0.0f;
  float mAspectRatioSaved = 0.0f;
  mozilla::Maybe<GtkOrientation> mAspectResizer;
  GdkPoint mLastResizePoint{0, 0};

  // Keep in sync with WaylandSurface::sNoScale
  constexpr static const int sNoScale = -1;
  int mCeiledScaleFactor = sNoScale;

  // The size requested, which might not be reflected in mClientArea.  Used in
  // WaylandPopupSetDirectPosition() to remember intended size for popup
  // positioning, in LockAspect() to remember the intended aspect ratio, and
  // to remember a size requested while waiting for moved-to-rect when
  // OnSizeAllocate() might change mClientArea.Size().
  // All these values are in unscaled (Gdk) coordinates.
  DesktopIntSize mLastSizeRequest;
  // Same but for positioning. Used to track move requests.
  DesktopIntPoint mLastMoveRequest;

  // See Bounds for these members.
  DesktopIntRect mClientArea;
  DesktopIntMargin mClientMargin;

  // This field omits duplicate scroll events caused by GNOME bug 726878.
  guint32 mLastScrollEventTime = GDK_CURRENT_TIME;
  mozilla::ScreenCoord mLastPinchEventSpan;

  struct TouchpadPinchGestureState {
    // Focus point of the PHASE_BEGIN event
    ScreenPoint mBeginFocus;

    // Focus point of the most recent PHASE_UPDATE event
    ScreenPoint mCurrentFocus;
  };

  // Used for handling touchpad pinch gestures
  ScreenPoint mCurrentTouchpadFocus;

  // Used for synthesizing touchpad pinch gestures
  TouchpadPinchGestureState mCurrentSynthesizedTouchpadPinch;

  // Used for synthesizing touchpad pan gestures
  struct TouchpadPanGestureState {
    mozilla::Maybe<TouchpadGesturePhase> mTouchpadGesturePhase;
    mozilla::Maybe<uint64_t> mSavedCallbackId;
  };

  // Used for synthesizing touchpad pan gestures
  TouchpadPanGestureState mCurrentSynthesizedTouchpadPan;

  // for touch event handling
  nsRefPtrHashtable<nsPtrHashKey<GdkEventSequence>, mozilla::dom::Touch>
      mTouches;

  // Upper bound on pending ConfigureNotify events to be dispatched to the
  // window. See bug 1225044.
  unsigned int mPendingConfigures = 0;

  // Window titlebar rendering mode, GTK_DECORATION_NONE if it's disabled
  // for this window.
  GtkWindowDecoration mGtkWindowDecoration = GTK_DECORATION_NONE;

  // Draggable titlebar region maintained by UpdateWindowDraggingRegion
  LayoutDeviceIntRegion mDraggableRegion;

  // The cursor cache
  static GdkCursor* gsGtkCursorCache[eCursorCount];

  // If true, draw our own window titlebar.
  bool mDrawInTitlebar = false;

  // This track real window visibility from OS perspective.
  // It's set by OnMap/OnUnmap which is based on Gtk events.
  bool mIsMapped;
  // Has this widget been destroyed yet?
  mozilla::Atomic<bool, mozilla::Relaxed> mIsDestroyed;
  // mIsShown tracks requested visible status from browser perspective, i.e.
  // if the window should be visible or now.
  bool mIsShown : 1;
  // mNeedsShow is set when browser requested to show this window but we failed
  // to do so for some reason (wrong window size for instance).
  // In such case we set mIsShown = true and mNeedsShow = true to indicate
  // that the window is not actually visible but we report to browser that
  // it is visible (mIsShown == true).
  bool mNeedsShow : 1;
  // is this widget enabled?
  bool mEnabled : 1;
  // has the native window for this been created yet?
  bool mCreated : 1;
  // whether we handle touch event
  bool mHandleTouchEvent : 1;
  // true if this is a drag and drop feedback popup
  bool mIsDragPopup : 1;
  bool mCompositedScreen : 1;
  bool mIsAccelerated : 1;
  bool mIsAlert : 1;
  bool mWindowShouldStartDragging : 1;
  bool mHasMappedToplevel : 1;
  bool mPanInProgress : 1;
  bool mPendingBoundsChange : 1;
  // Draw titlebar with :backdrop css state (inactive/unfocused).
  bool mTitlebarBackdropState : 1;
  bool mAlwaysOnTop : 1;
  bool mIsTransparent : 1;
  // We can expect at least one size-allocate event after early resizes.
  bool mHasReceivedSizeAllocate : 1;
  bool mWidgetCursorLocked : 1;
  bool mUndecorated : 1;

  // True when we're on compositing window manager and this
  // window is using visual with alpha channel.
  bool mHasAlphaVisual : 1;

  // Whether we've configured default clear color already.
  bool mConfiguredClearColor : 1;
  // Whether we've received a non-blank paint in which case we can reset the
  // clear color to transparent.
  bool mGotNonBlankPaint : 1;

  // Whether we need to retry capturing the mouse because we' re not mapped yet.
  bool mNeedsToRetryCapturingMouse : 1;

  /* On X11 Gtk tends to ignore window position requests when gtk_window
   * is hidden. Save the position requests at mPopupPosition and apply
   * when the widget is shown.
   */
  bool mX11HiddenPopupPositioned : 1;

  // Popup is hidden only as a part of hierarchy tree update.
  bool mPopupTemporaryHidden : 1;

  // If we're waiting for session restore, don't fiddle with window
  // size/focus etc.
  bool mWaitingToSessionRestore : 1;

  // all of our DND stuff
  void InitDragEvent(mozilla::WidgetDragEvent& aEvent);

  float mLastMotionPressure = 0.0f;

  // Input region margin is stored in layout (scaled) coordinates.
  InputRegion mInputRegion;

  bool DragInProgress(void);

  void DispatchMissedButtonReleases(GdkEventCrossing* aGdkEvent);

  bool IsAlwaysUndecoratedWindow() const;

  // nsIWidget
  WindowRenderer* GetWindowRenderer() override;
  void DidGetNonBlankPaint() override;

  void SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) override;

  void UpdateMozWindowActive();

  void ForceTitlebarRedraw();
  bool DoDrawTilebarCorners();
  bool IsChromeWindowTitlebar();

  void SetPopupWindowDecoration(bool aShowOnTaskbar);

  void ApplySizeConstraints();

  GtkWindow* GetCurrentTopmostWindow() const;
  nsAutoCString GetFrameTag() const;
  nsCString GetPopupTypeName();
  bool IsPopupDirectionRTL();

#ifdef MOZ_LOGGING
  void LogPopupHierarchy();
  void LogPopupAnchorHints(int aHints);
  void LogPopupGravity(GdkGravity aGravity);
#endif

  // When popup is resized by Gtk by move-to-rect callback,
  // we store final popup size here. Then we use mMoveToRectPopupSize size
  // in following popup operations unless mLayoutPopupSizeCleared is set.
  DesktopIntSize mMoveToRectPopupSize;

#ifdef MOZ_ENABLE_DBUS
  RefPtr<mozilla::widget::DBusMenuBar> mDBusMenuBar;
#endif

  struct LastMouseCoordinates {
    template <typename Event>
    void Set(Event* aEvent) {
      mX = aEvent->x;
      mY = aEvent->y;
      mRootX = aEvent->x_root;
      mRootY = aEvent->y_root;
    }

    float mX = 0.0f, mY = 0.0f;
    float mRootX = 0.0f, mRootY = 0.0f;
  } mLastMouseCoordinates;

  // We don't want to fire scroll event with the same timestamp as
  // smooth scroll event.
  guint32 mLastSmoothScrollEventTime = GDK_CURRENT_TIME;

  /**
   * |mIMContext| takes all IME related stuff.
   *
   * This is owned by the top-level nsWindow or the topmost child
   * nsWindow embedded in a non-Gecko widget.
   *
   * The instance is created when the top level widget is created.  And when
   * the widget is destroyed, it's released.  All child windows refer its
   * ancestor widget's instance.  So, one set of IM contexts is created for
   * all windows in a hierarchy.  If the children are released after the top
   * level window is released, the children still have a valid pointer,
   * however, IME doesn't work at that time.
   */
  RefPtr<mozilla::widget::IMContextWrapper> mIMContext;
  DesktopIntRect mIMContextInputArea;

  int mEmojiHidenSignal = 0;

#ifdef MOZ_X11
  mozilla::UniquePtr<mozilla::CurrentX11TimeGetter> mCurrentTimeGetter;
#endif
  static GtkWindowDecoration sGtkWindowDecoration;

  static bool sTransparentMainWindow;

#ifdef ACCESSIBILITY
  RefPtr<mozilla::a11y::LocalAccessible> mRootAccessible;

  /**
   * Request to create the accessible for this window if it is top level.
   */
  void CreateRootAccessible();

  /**
   * Dispatch accessible event for the top level window accessible.
   *
   * @param  aEventType  [in] the accessible event type to dispatch
   */
  void DispatchEventToRootAccessible(uint32_t aEventType);

  /**
   * Dispatch accessible window deactivate event for the top level window
   * accessible.
   */
  void DispatchDeactivateEventAccessible();

  /**
   * Dispatch accessible window maximize event for the top level window
   * accessible.
   */
  void DispatchMaximizeEventAccessible();

  /**
   * Dispatch accessible window minize event for the top level window
   * accessible.
   */
  void DispatchMinimizeEventAccessible();

  /**
   * Dispatch accessible window restore event for the top level window
   * accessible.
   */
  void DispatchRestoreEventAccessible();
#endif

  void SetUserTimeAndStartupTokenForActivatedWindow();

  void KioskLockOnMonitor();

  void EmulateResizeDrag(GdkEventMotion* aEvent);

  void RequestRepaint(LayoutDeviceIntRegion& aRepaintRegion);

  bool DrawDragPopupSurface(cairo_t* cr);
  bool ExtractExposeRegion(LayoutDeviceIntRegion& aRegion, cairo_t* cr);

  // An activation token from our environment (see handling of the
  // XDG_ACTIVATION_TOKEN/DESKTOP_STARTUP_ID) env vars.
  nsCString mWindowActivationTokenFromEnv;
  mozilla::widget::WindowSurfaceProvider mSurfaceProvider;
  GdkDragContext* mSourceDragContext = nullptr;
  bool mSourceDragContextActive = false;
  mozilla::Sides mResizableEdges{mozilla::SideBits::eAll};
  // Running in kiosk mode and requested to stay on specified monitor.
  // If monitor is removed minimize the window.
  mozilla::Maybe<int> mKioskMonitor;
  LayoutDeviceIntRegion mOpaqueRegion MOZ_GUARDED_BY(mOpaqueRegionLock);
  mutable mozilla::RWLock mOpaqueRegionLock{"nsWindow::mOpaqueRegion"};
};

nsWindow* get_window_for_gtk_widget(GtkWidget* widget);
nsWindow* get_window_for_gdk_window(GdkWindow* window);
void GtkWindowSetTransientFor(GtkWindow* aWindow, GtkWindow* aParent);

#endif /* _nsWindow_h_ */
