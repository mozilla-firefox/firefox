/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIWidget_h_
#define nsIWidget_h_

#include <cmath>
#include <cstdint>
#include "imgIContainer.h"
#include "ErrorList.h"
#include "Units.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/ScrollableLayerGuid.h"
#include "mozilla/layers/ZoomConstraints.h"
#include "mozilla/image/Resolution.h"
#include "mozilla/widget/IMEData.h"
#include "nsCOMPtr.h"
#include "nsColor.h"
#include "nsDebug.h"
#include "nsID.h"
#include "nsIDOMWindowUtils.h"
#include "nsISupports.h"
#include "nsITheme.h"
#include "nsITimer.h"
#include "nsIWidgetListener.h"
#include "nsRect.h"
#include "nsSize.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsWeakReference.h"
#include "mozilla/widget/InitData.h"
#include "nsXULAppAPI.h"

// Windows specific constant indicating the maximum number of touch points the
// inject api will allow. This also sets the maximum numerical value for touch
// ids we can use when injecting touch points on Windows.
#define TOUCH_INJECT_MAX_POINTS 256

// forward declarations
class nsIBidiKeyboard;
class nsIRollupListener;
class nsIContent;
class nsMenuPopupFrame;
class nsIRunnable;
class nsIWidget;

namespace mozilla {
class CompositorVsyncDispatcher;
class FallbackRenderer;
class LiveResizeListener;
class PanGestureInput;
class MultiTouchInput;
class Mutex;
class PinchGestureInput;
class SwipeTracker;
class VsyncDispatcher;
class WidgetGUIEvent;
class WidgetInputEvent;
class WidgetKeyboardEvent;
enum ScreenRotation : uint8_t;
enum class ColorScheme : uint8_t;
enum class NativeKeyBindingsType : uint8_t;
enum class WindowButtonType : uint8_t;
struct FontRange;
struct SwipeEventQueue;

enum class HapticFeedbackType : uint8_t {
  ShortPress = 0,
  LongPress = 1,
  TextHandleMove = 2,

  End,  // Not a feedback type, only to mark the upper boundary
};

enum class WindowShadow : uint8_t {
  None,
  Menu,
  Panel,
  Tooltip,
};

namespace dom {
class BrowserChild;
enum class CallerType : uint32_t;
}  // namespace dom
class WindowRenderer;
namespace gfx {
class DrawTarget;
class SourceSurface;
}  // namespace gfx
namespace layers {
class APZEventState;
class AsyncDragMetrics;
class Compositor;
class CompositorBridgeChild;
class CompositorBridgeParent;
class CompositorOptions;
class CompositorSession;
class GeckoContentController;
class IAPZCTreeManager;
class ImageContainer;
class LayerManager;
class NativeLayer;
class NativeLayerRoot;
class RemoteCompositorSession;
class WebRenderBridgeChild;
class WebRenderLayerManager;
struct APZEventResult;
struct CompositorScrollUpdate;
struct FrameMetrics;
struct ScrollableLayerGuid;
}  // namespace layers
namespace widget {
enum class ThemeChangeKind : uint8_t;
enum class OcclusionState : uint8_t;
class TextEventDispatcher;
class TextEventDispatcherListener;
class LocalesChangedObserver;
class WidgetShutdownObserver;
class CompositorWidget;
class CompositorWidgetInitData;
class CompositorWidgetDelegate;
class InProcessCompositorWidget;
class WidgetRenderingContext;
class Screen;
}  // namespace widget
namespace wr {
class DisplayListBuilder;
class IpcResourceUpdateQueue;
enum class RenderRoot : uint8_t;
}  // namespace wr
#ifdef ACCESSIBILITY
namespace a11y {
class LocalAccessible;
}
#endif
}  // namespace mozilla

/**
 * Callback function that processes events.
 *
 * The argument is actually a subtype (subclass) of WidgetEvent which carries
 * platform specific information about the event. Platform specific code
 * knows how to deal with it.
 *
 * The return value determines whether or not the default action should take
 * place.
 */
typedef nsEventStatus (*EVENT_CALLBACK)(mozilla::WidgetGUIEvent* aEvent);

// Hide the native window system's real window type so as to avoid
// including native window system types and APIs. This is necessary
// to ensure cross-platform code.
typedef void* nsNativeWidget;

/*
 * TouchPointerState states for SynthesizeNativeTouchPoint. Match
 * touch states in nsIDOMWindowUtils.idl.
 */
enum TouchPointerState : uint8_t {
  // The pointer is in a hover state above the digitizer
  TOUCH_HOVER = (1 << 0),
  // The pointer is in contact with the digitizer
  TOUCH_CONTACT = (1 << 1),
  // The pointer has been removed from the digitizer detection area
  TOUCH_REMOVE = (1 << 2),
  // The pointer has been canceled. Will cancel any pending os level
  // gestures that would triggered as a result of completion of the
  // input sequence. This may not cancel moz platform related events
  // that might get tirggered by input already delivered.
  TOUCH_CANCEL = (1 << 3),

  // ALL_BITS used for validity checking during IPC serialization
  ALL_BITS = (1 << 4) - 1
};

/**
 * Values for the GetNativeData function
 */
#define NS_NATIVE_WINDOW 0
#define NS_NATIVE_GRAPHIC 1
#define NS_NATIVE_WIDGET 3
#define NS_NATIVE_REGION 5
#define NS_NATIVE_OFFSETX 6
#define NS_NATIVE_OFFSETY 7
#define NS_NATIVE_SCREEN 9
// The toplevel GtkWidget containing this nsIWidget:
#define NS_NATIVE_SHELLWIDGET 10
#define NS_NATIVE_OPENGL_CONTEXT 12
// This is available only with GetNativeData() in parent process.  Anybody
// shouldn't access this pointer as a valid pointer since the result may be
// special value like NS_ONLY_ONE_NATIVE_IME_CONTEXT.  So, the result is just
// an identifier of distinguishing a text composition is caused by which native
// IME context.  Note that the result is only valid in the process.  So,
// XP code should use nsIWidget::GetNativeIMEContext() instead of using this.
#define NS_RAW_NATIVE_IME_CONTEXT 14
#define NS_NATIVE_WINDOW_WEBRTC_DEVICE_ID 15
#ifdef XP_WIN
#  define NS_NATIVE_ICOREWINDOW 103  // winrt specific
#endif
#if defined(MOZ_WIDGET_GTK)
#  define NS_NATIVE_EGL_WINDOW 106
#endif
#ifdef MOZ_WIDGET_ANDROID
#  define NS_JAVA_SURFACE 100
#endif

#define MOZ_WIDGET_MAX_SIZE 16384

// Must be kept in sync with xpcom/rust/xpcom/src/interfaces/nonidl.rs
#define NS_IWIDGET_IID \
  {0x06396bf6, 0x2dd8, 0x45e5, {0xac, 0x45, 0x75, 0x26, 0x53, 0xb1, 0xc9, 0x80}}

/**
 * Cursor types.
 */

enum nsCursor {  ///(normal cursor,       usually rendered as an arrow)
  eCursor_standard,
  ///(system is busy,      usually rendered as a hourglass or watch)
  eCursor_wait,
  ///(Selecting something, usually rendered as an IBeam)
  eCursor_select,
  ///(can hyper-link,      usually rendered as a human hand)
  eCursor_hyperlink,
  ///(north/south/west/east edge sizing)
  eCursor_n_resize,
  eCursor_s_resize,
  eCursor_w_resize,
  eCursor_e_resize,
  ///(corner sizing)
  eCursor_nw_resize,
  eCursor_se_resize,
  eCursor_ne_resize,
  eCursor_sw_resize,
  eCursor_crosshair,
  eCursor_move,
  eCursor_help,
  eCursor_copy,  // CSS3
  eCursor_alias,
  eCursor_context_menu,
  eCursor_cell,
  eCursor_grab,
  eCursor_grabbing,
  eCursor_spinning,
  eCursor_zoom_in,
  eCursor_zoom_out,
  eCursor_not_allowed,
  eCursor_col_resize,
  eCursor_row_resize,
  eCursor_no_drop,
  eCursor_vertical_text,
  eCursor_all_scroll,
  eCursor_nesw_resize,
  eCursor_nwse_resize,
  eCursor_ns_resize,
  eCursor_ew_resize,
  eCursor_none,
  // This one is used for array sizing, and so better be the last
  // one in this list...
  eCursorCount,
};

/**
 * Before the OS goes to sleep, this topic is notified.
 */
#define NS_WIDGET_SLEEP_OBSERVER_TOPIC "sleep_notification"

/**
 * After the OS wakes up, this topic is notified.
 */
#define NS_WIDGET_WAKE_OBSERVER_TOPIC "wake_notification"

/**
 * Before the OS suspends the current process, this topic is notified.  Some
 * OS will kill processes that are suspended instead of resuming them.
 * For that reason this topic may be useful to safely close down resources.
 */
#define NS_WIDGET_SUSPEND_PROCESS_OBSERVER_TOPIC "suspend_process_notification"

/**
 * After the current process resumes from being suspended, this topic is
 * notified.
 */
#define NS_WIDGET_RESUME_PROCESS_OBSERVER_TOPIC "resume_process_notification"

/**
 * When an app(-shell) is activated by the OS, this topic is notified.
 * Currently, this only happens on Mac OSX.
 */
#define NS_WIDGET_MAC_APP_ACTIVATE_OBSERVER_TOPIC "mac_app_activate"

namespace mozilla::widget {

/**
 * Size constraints for setting the minimum and maximum size of a widget.
 * Values are in desktop pixels, so they do not depend on the widget's
 * current backing scale factor. Each widget implementation converts to
 * its native coordinate system using its own scale factor when applying
 * the constraints.
 */
struct SizeConstraints {
  SizeConstraints() : mMaxSize(MOZ_WIDGET_MAX_SIZE, MOZ_WIDGET_MAX_SIZE) {}

  SizeConstraints(mozilla::DesktopIntSize aMinSize,
                  mozilla::DesktopIntSize aMaxSize)
      : mMinSize(aMinSize), mMaxSize(aMaxSize) {
    if (mMaxSize.width > MOZ_WIDGET_MAX_SIZE) {
      mMaxSize.width = MOZ_WIDGET_MAX_SIZE;
    }
    if (mMaxSize.height > MOZ_WIDGET_MAX_SIZE) {
      mMaxSize.height = MOZ_WIDGET_MAX_SIZE;
    }
  }

  mozilla::DesktopIntSize mMinSize;
  mozilla::DesktopIntSize mMaxSize;
};

class MOZ_RAII AutoSynthesizedEventCallbackNotifier final {
 public:
  explicit AutoSynthesizedEventCallbackNotifier(
      nsISynthesizedEventCallback* aCallback)
      : mCallback(aCallback) {}

  void SkipNotification() { mCallback = nullptr; }

  Maybe<uint64_t> SaveCallback() {
    if (!mCallback) {
      return Nothing();
    }
    uint64_t callbackId = ++sCallbackId;
    sSavedCallbacks.InsertOrUpdate(callbackId, mCallback);
    SkipNotification();
    return Some(callbackId);
  }

  ~AutoSynthesizedEventCallbackNotifier() {
    if (mCallback) {
      mCallback->OnCompleteDispatch();
    }
  }

  static void NotifySavedCallback(const uint64_t& aCallbackId) {
    MOZ_ASSERT(aCallbackId > 0, "Callback ID must be non-zero");

    auto entry = sSavedCallbacks.Extract(aCallbackId);
    if (!entry) {
      MOZ_ASSERT_UNREACHABLE("We should always find a saved callback");
      return;
    }

    entry.value()->OnCompleteDispatch();
  }

 private:
  nsCOMPtr<nsISynthesizedEventCallback> mCallback;

  static uint64_t sCallbackId;
  static nsTHashMap<uint64_t, nsCOMPtr<nsISynthesizedEventCallback>>
      sSavedCallbacks;
};

}  // namespace mozilla::widget

/**
 * The base class for all the widgets. It provides the interface for
 * all basic and necessary functionality.
 */
class nsIWidget : public nsSupportsWeakReference {
 public:
  template <class EventType, class InputType>
  friend class DispatchEventOnMainThread;
  friend class mozilla::widget::InProcessCompositorWidget;
  friend class mozilla::layers::RemoteCompositorSession;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::SourceSurface SourceSurface;
  typedef mozilla::layers::CompositorBridgeChild CompositorBridgeChild;
  typedef mozilla::layers::CompositorBridgeParent CompositorBridgeParent;
  typedef mozilla::layers::IAPZCTreeManager IAPZCTreeManager;
  typedef mozilla::layers::GeckoContentController GeckoContentController;
  typedef mozilla::layers::ScrollableLayerGuid ScrollableLayerGuid;
  typedef mozilla::layers::APZEventState APZEventState;
  typedef mozilla::CSSIntRect CSSIntRect;
  typedef mozilla::ScreenRotation ScreenRotation;
  typedef mozilla::widget::CompositorWidgetDelegate CompositorWidgetDelegate;
  typedef mozilla::layers::CompositorSession CompositorSession;
  typedef mozilla::layers::ImageContainer ImageContainer;
  typedef mozilla::dom::BrowserChild BrowserChild;
  typedef mozilla::layers::AsyncDragMetrics AsyncDragMetrics;
  typedef mozilla::layers::FrameMetrics FrameMetrics;
  typedef mozilla::layers::LayerManager LayerManager;
  typedef mozilla::WindowRenderer WindowRenderer;
  typedef mozilla::layers::LayersBackend LayersBackend;
  typedef mozilla::layers::LayersId LayersId;
  typedef mozilla::layers::ZoomConstraints ZoomConstraints;
  typedef mozilla::widget::IMEEnabled IMEEnabled;
  typedef mozilla::widget::IMEMessage IMEMessage;
  typedef mozilla::widget::IMENotification IMENotification;
  typedef mozilla::widget::IMENotificationRequest IMENotificationRequest;
  typedef mozilla::widget::IMENotificationRequests IMENotificationRequests;
  typedef mozilla::widget::IMEState IMEState;
  typedef mozilla::widget::InputContext InputContext;
  typedef mozilla::widget::InputContextAction InputContextAction;
  typedef mozilla::widget::NativeIMEContext NativeIMEContext;
  typedef mozilla::widget::SizeConstraints SizeConstraints;
  typedef mozilla::widget::TextEventDispatcher TextEventDispatcher;
  typedef mozilla::widget::TextEventDispatcherListener
      TextEventDispatcherListener;
  typedef mozilla::LayoutDeviceMargin LayoutDeviceMargin;
  typedef mozilla::LayoutDeviceIntMargin LayoutDeviceIntMargin;
  typedef mozilla::LayoutDeviceIntPoint LayoutDeviceIntPoint;
  typedef mozilla::LayoutDeviceIntRect LayoutDeviceIntRect;
  typedef mozilla::LayoutDeviceRect LayoutDeviceRect;
  typedef mozilla::LayoutDeviceIntRegion LayoutDeviceIntRegion;
  typedef mozilla::LayoutDeviceIntSize LayoutDeviceIntSize;
  typedef mozilla::ScreenIntPoint ScreenIntPoint;
  typedef mozilla::ScreenIntMargin ScreenIntMargin;
  typedef mozilla::ScreenIntSize ScreenIntSize;
  typedef mozilla::ScreenPoint ScreenPoint;
  typedef mozilla::CSSToScreenScale CSSToScreenScale;
  typedef mozilla::DesktopIntRect DesktopIntRect;
  typedef mozilla::DesktopPoint DesktopPoint;
  typedef mozilla::DesktopIntPoint DesktopIntPoint;
  typedef mozilla::DesktopIntSize DesktopIntSize;
  typedef mozilla::DesktopIntMargin DesktopIntMargin;
  typedef mozilla::DesktopRect DesktopRect;
  typedef mozilla::DesktopSize DesktopSize;
  typedef mozilla::CSSPoint CSSPoint;
  typedef mozilla::CSSRect CSSRect;

  NS_DECL_THREADSAFE_ISUPPORTS

  using TouchPointerState = ::TouchPointerState;
  using InitData = mozilla::widget::InitData;
  using WindowType = mozilla::widget::WindowType;
  using PopupType = mozilla::widget::PopupType;
  using PopupLevel = mozilla::widget::PopupLevel;
  using BorderStyle = mozilla::widget::BorderStyle;
  using TransparencyMode = mozilla::widget::TransparencyMode;
  using Screen = mozilla::widget::Screen;

  // Used in UpdateThemeGeometries.
  struct ThemeGeometry {
    // The ThemeGeometryType value for the themed widget, see
    // nsITheme::ThemeGeometryTypeForWidget.
    nsITheme::ThemeGeometryType mType;
    // The device-pixel rect within the window for the themed widget
    LayoutDeviceIntRect mRect;

    ThemeGeometry(nsITheme::ThemeGeometryType aType,
                  const LayoutDeviceIntRect& aRect)
        : mType(aType), mRect(aRect) {}
  };

  NS_INLINE_DECL_STATIC_IID(NS_IWIDGET_IID)

  /**
   * Create and initialize a widget.
   *
   * All the arguments can be null in which case a top level window
   * with size 0 is created. The event callback function has to be
   * provided only if the caller wants to deal with the events this
   * widget receives.  The event callback is basically a preprocess
   * hook called synchronously. The return value determines whether
   * the event goes to the default window procedure or it is hidden
   * to the os. The assumption is that if the event handler returns
   * false the widget does not see the event. The widget should not
   * automatically clear the window to the background color. The
   * calling code must handle paint messages and clear the background
   * itself.
   *
   * If aParent is null, the widget isn't parented (e.g. context menus or
   * independent top level windows).
   *
   * The dimensions given in aRect are specified in the parent's
   * device coordinate system.
   * This must not be called for parentless widgets such as top-level
   * windows, which use the desktop pixel coordinate system; a separate
   * method is provided for these.
   *
   * @param     aParent       parent nsIWidget
   * @param     aRect         the widget dimension
   * @param     aInitData     data that is used for widget initialization
   *
   */
  [[nodiscard]] virtual nsresult Create(nsIWidget* aParent,
                                        const LayoutDeviceIntRect& aRect,
                                        const InitData&) = 0;

  /*
   * As above, but with aRect specified in DesktopPixel units (for top-level
   * widgets).
   * Default implementation just converts aRect to device pixels and calls
   * through to device-pixel Create, but platforms may override this if the
   * mapping is not straightforward or the native platform needs to use the
   * desktop pixel values directly.
   */
  [[nodiscard]] virtual nsresult Create(nsIWidget* aParent,
                                        const DesktopIntRect& aRect,
                                        const InitData& aInitData) {
    LayoutDeviceIntRect devPixRect =
        RoundedToInt(aRect * GetDesktopToDeviceScale());
    return Create(aParent, devPixRect, aInitData);
  }

  /**
   * Allocate, initialize, and return a widget that is a child of
   * |this|.  The returned widget (if nonnull) has gone through the
   * equivalent of CreateInstance(widgetCID) + Create(...).
   *
   * |CreateChild()| lets widget backends decide whether to parent
   * the new child widget to this, nonnatively parent it, or both.
   * This interface exists to support the PuppetWidget backend,
   * which is entirely non-native.  All other params are the same as
   * for |Create()|.
   */
  already_AddRefed<nsIWidget> CreateChild(const LayoutDeviceIntRect& aRect,
                                          const InitData&);

  /**
   * Accessor functions to get and set the attached listener.
   */
  void SetAttachedWidgetListener(nsIWidgetListener* aListener) {
    mAttachedWidgetListener = aListener;
  }
  nsIWidgetListener* GetAttachedWidgetListener() const {
    return mAttachedWidgetListener;
  }
  void SetPreviouslyAttachedWidgetListener(nsIWidgetListener* aListener) {
    mPreviouslyAttachedWidgetListener = aListener;
  }
  nsIWidgetListener* GetPreviouslyAttachedWidgetListener() {
    return mPreviouslyAttachedWidgetListener;
  }

  /**
   * Notifies the root widget of a non-blank paint.
   */
  virtual void DidGetNonBlankPaint() {}

  /**
   * Accessor functions to get and set the listener which handles various
   * actions for the widget.
   */
  nsIWidgetListener* GetWidgetListener() const { return mWidgetListener; }
  void SetWidgetListener(nsIWidgetListener* aListener) {
    mWidgetListener = aListener;
  }

  /** Returns the listener used for painting */
  nsIWidgetListener* GetPaintListener() const;

  /**
   * Close and destroy the internal native window.
   * This method does not delete the widget.
   */

  virtual void Destroy();

  /**
   * Destroyed() returns true if Destroy() has been called already.
   * Otherwise, false.
   */
  bool Destroyed() const { return mOnDestroyCalled; }

  /** Clear the widget's parent. */
  void ClearParent();

  /**
   * Return the parent Widget of this Widget or nullptr if this is a
   * top level window
   *
   * @return the parent widget or nullptr if it does not have a parent
   *
   */
  nsIWidget* GetParent() const { return mParent; }

  /** Gets called when mParent is cleared. */
  virtual void DidClearParent(nsIWidget* aOldParent) {}

  /**
   * Return the top level Widget of this Widget
   *
   * @return the closest top level widget, as in IsTopLevelWidget().
   */
  nsIWidget* GetTopLevelWidget();
  bool IsTopLevelWidget() const {
    return mWindowType == WindowType::TopLevel ||
           mWindowType == WindowType::Dialog ||
           mWindowType == WindowType::Invisible;
  }

  /**
   * Return the physical DPI of the screen containing the window ...
   * the number of device pixels per inch.
   */
  virtual float GetDPI();

  /**
   * Fallback DPI for when there's no widget available.
   */
  static float GetFallbackDPI();

  /**
   * Return the scaling factor between device pixels and the platform-
   * dependent "desktop pixels" used to manage window positions on a
   * potentially multi-screen, mixed-resolution desktop.
   */
  virtual mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() const {
    return mozilla::DesktopToLayoutDeviceScale(1.0);
  }

  // Utility function for derived-class overrides of ConstrainPosition.
  static DesktopIntPoint ConstrainPositionToBounds(
      const DesktopIntPoint&, const mozilla::DesktopIntSize&,
      const DesktopIntRect&);

  void DynamicToolbarOffsetChanged(mozilla::ScreenIntCoord aOffset);

  /**
   * Return the default scale factor for the window. This is the
   * default number of device pixels per CSS pixel to use. This should
   * depend on OS/platform settings such as the Mac's "UI scale factor"
   * or Windows' "font DPI". This will take into account Gecko preferences
   * overriding the system setting.
   */
  mozilla::CSSToLayoutDeviceScale GetDefaultScale();

  /**
   * Fallback default scale for when there's no widget available.
   */
  static mozilla::CSSToLayoutDeviceScale GetFallbackDefaultScale();

  /**
   * Return the first child of this widget.  Will return null if
   * there are no children.
   */
  nsIWidget* GetFirstChild() const { return mFirstChild; }

  /**
   * Return the last child of this widget.  Will return null if
   * there are no children.
   */
  nsIWidget* GetLastChild() const { return mLastChild; }

  /**
   * Return the next sibling of this widget
   */
  nsIWidget* GetNextSibling() const { return mNextSibling; }

  /**
   * Set the next sibling of this widget
   */
  void SetNextSibling(nsIWidget* aSibling) { mNextSibling = aSibling; }

  /**
   * Return the previous sibling of this widget
   */
  nsIWidget* GetPrevSibling() const { return mPrevSibling; }

  /**
   * Set the previous sibling of this widget
   */
  void SetPrevSibling(nsIWidget* aSibling) { mPrevSibling = aSibling; }

  /**
   * Show or hide this widget
   *
   * @param aState true to show the Widget, false to hide it
   *
   */
  virtual void Show(bool aState) = 0;

  /**
   * Whether or not a widget must be recreated after being hidden to show
   * again properly.
   */
  virtual bool NeedsRecreateToReshow() { return false; }

  /**
   * Make the window modal.
   */
  virtual void SetModal(bool aModal) {}

  /**
   * Are we app modal. Currently only implemented on Cocoa.
   */
  virtual bool IsRunningAppModal() { return false; }

  /**
   * The maximum number of simultaneous touch contacts supported by the device.
   * In the case of devices with multiple digitizers (e.g. multiple touch
   * screens), the value will be the maximum of the set of maximum supported
   * contacts by each individual digitizer.
   */
  virtual uint32_t GetMaxTouchPoints() const;

  /**
   * Returns whether the window is visible
   *
   */
  virtual bool IsVisible() const = 0;

  /**
   * Perform platform-dependent sanity check on a potential window position.
   * This is guaranteed to work only for top-level windows.
   */
  virtual void ConstrainPosition(DesktopIntPoint&) {}

  /**
   * NOTE:
   *
   * For a top-level window widget, the "parent's coordinate system" is the
   * "global" display pixel coordinate space, *not* device pixels (which
   * may be inconsistent between multiple screens, at least in the Mac OS
   * case with mixed hi-dpi and lo-dpi displays). This applies to all the
   * following Move and Resize widget APIs.
   *
   * The display-/device-pixel distinction becomes important for (at least)
   * macOS with Hi-DPI (retina) displays, and Windows when the UI scale factor
   * is set to other than 100%.
   *
   * The Move and Resize methods take floating-point parameters, rather than
   * integer ones. This is important when manipulating top-level widgets,
   * where the coordinate system may not be an integral multiple of the
   * device-pixel space.
   **/

  /**
   * Move this widget.
   *
   * Coordinates refer to the top-left of the widget.  For toplevel windows
   * with decorations, this is the top-left of the titlebar and frame .
   *
   * @param aX the new x position expressed in the parent's coordinate system
   * @param aY the new y position expressed in the parent's coordinate system
   *
   **/
  virtual void Move(const DesktopPoint&) = 0;

  /**
   * Reposition this widget so that the client area has the given offset.
   *
   * @param aOffset  the new offset of the client area expressed as an
   *                 offset from the origin of the client area of the parent
   *                 widget (for root widgets and popup widgets it is in
   *                 screen coordinates)
   **/
  void MoveClient(const DesktopPoint& aOffset);

  /**
   * Resize this widget. Any size constraints set for the window by a
   * previous call to SetSizeConstraints will be applied.
   *
   * @param aWidth  the new width expressed in the parent's coordinate system
   * @param aHeight the new height expressed in the parent's coordinate
   *                system
   * @param aRepaint whether the widget should be repainted
   */
  virtual void Resize(const DesktopSize&, bool aRepaint) = 0;

  /**
   * Lock the aspect ratio of a Window
   * @param aShouldLock bool
   */
  virtual void LockAspectRatio(bool aShouldLock) {}

  /**
   * Move or resize this widget. Any size constraints set for the window by
   * a previous call to SetSizeConstraints will be applied.
   *
   * @param aRepaint whether the widget should be repainted if the size
   *                 changes
   *
   */
  virtual void Resize(const DesktopRect&, bool aRepaint) = 0;

  /**
   * Resize the widget so that the inner client area has the given size.
   *
   * @param aSize    the new size of the client area.
   * @param aRepaint whether the widget should be repainted
   */
  void ResizeClient(const DesktopSize& aSize, bool aRepaint);

  /**
   * Resize and reposition the widget so tht inner client area has the given
   * offset and size.
   *
   * @param aRect    the new offset and size of the client area. The offset is
   *                 expressed as an offset from the origin of the client area
   *                 of the parent widget (for root widgets and popup widgets it
   *                 is in screen coordinates).
   * @param aRepaint whether the widget should be repainted
   */
  void ResizeClient(const DesktopRect& aRect, bool aRepaint);

  /**
   * Minimize, maximize or normalize the window size.
   * Takes a value from nsSizeMode (see nsIWidgetListener.h)
   */
  virtual void SetSizeMode(nsSizeMode aMode) = 0;

  virtual void GetWorkspaceID(nsAString& aWorkspaceID) {
    aWorkspaceID.Truncate();
  }

  virtual void MoveToWorkspace(const nsAString& aWorkspaceID) {}

  // Assume that it is not, since most widgets are not cloaked.
  virtual bool IsCloaked() const { return false; }

  /**
   * Suppress animations that are applied to a window by OS.
   */
  virtual void SuppressAnimation(bool aSuppress) {}

  /** Sets windows-specific mica backdrop on this widget. */
  virtual void SetMicaBackdrop(bool) {}

  /**
   * Return size mode (minimized, maximized, normalized).
   * Returns a value from nsSizeMode (see nsIWidgetListener.h)
   */
  virtual nsSizeMode SizeMode() = 0;

  /** Ask whether the window is tiled. */
  bool IsTiled() const { return mIsTiled; }
  /** Ask whether the widget is fully occluded */
  bool IsFullyOccluded() const { return mIsFullyOccluded; }

  /**
   * Enable or disable this Widget
   *
   * @param aState true to enable the Widget, false to disable it.
   */
  virtual void Enable(bool aState) = 0;

  /**
   * Ask whether the widget is enabled
   */
  virtual bool IsEnabled() const = 0;

  /*
   * Whether we should request activation of this widget's toplevel window.
   */
  enum class Raise {
    No,
    Yes,
  };

  /**
   * Request activation of this window or give focus to this widget.
   */
  virtual void SetFocus(Raise, mozilla::dom::CallerType aCallerType) = 0;

  /**
   * Get this widget's outside dimensions relative to its parent widget. For
   * popup widgets the returned rect is in screen coordinates and not
   * relative to its parent widget.
   *
   * @return the x, y, width and height of this widget.
   */
  virtual LayoutDeviceIntRect GetBounds() = 0;

  /**
   * Get this widget's outside dimensions in device coordinates. This
   * includes any title bar on the window.
   *
   * @return the x, y, width and height of this widget.
   */
  virtual LayoutDeviceIntRect GetScreenBounds() { return GetBounds(); }

  /**
   * Similar to GetScreenBounds except that this function will always
   * get the size when the widget is in the nsSizeMode_Normal size mode
   * even if the current size mode is not nsSizeMode_Normal.
   *
   * If PersistClientBounds() is true, then the returned size are client sizes.
   *
   * This method will fail if the size mode is not nsSizeMode_Normal and
   * the platform doesn't have the ability.
   * This method will always succeed if the current size mode is
   * nsSizeMode_Normal.
   *
   * @param aRect   On return it holds the  x, y, width and height of
   *                this widget.
   */
  [[nodiscard]] virtual nsresult GetRestoredBounds(LayoutDeviceIntRect& aRect);

  /**
   * On some platforms (namely, GTK), we can't know the bounds of the client
   * decorations before actually showing the window. For that reason, we instead
   * persist client (inner) sizes.
   */
  virtual bool PersistClientBounds() const { return false; }

  /**
   * Get this widget's client area bounds, if the window has a 3D border
   * appearance this returns the area inside the border. The position is the
   * position of the client area relative to the client area of the parent
   * widget (for root widgets and popup widgets it is in screen coordinates).
   *
   * @return the x, y, width and height of the client area of this widget.
   */
  virtual LayoutDeviceIntRect GetClientBounds() { return GetBounds(); }

  /** Whether to extend the client area into the titlebar. */
  virtual void SetCustomTitlebar(bool) {}

  /**
   * Sets the region around the edges of the window that can be dragged to
   * resize the window. All four sides of the window will get the same margin.
   */
  virtual void SetResizeMargin(mozilla::LayoutDeviceIntCoord) {}

  /**
   * Get the client offset from the window origin.
   *
   * @return the x and y of the offset.
   */
  virtual LayoutDeviceIntPoint GetClientOffset();

  /**
   * Returns the slop from the screen edges in device pixels.
   * @see Window.screenEdgeSlop{X,Y}
   */
  virtual LayoutDeviceIntPoint GetScreenEdgeSlop() { return {}; }

  /**
   * Equivalent to GetClientBounds but only returns the size.
   */
  virtual LayoutDeviceIntSize GetClientSize() {
    // Depending on the backend, overloading this method may be useful if
    // requesting the client offset is expensive.
    return GetClientBounds().Size();
  }

  /**
   * Set the native background color for this widget.
   *
   * Deprecated. Currently only implemented for iOS. (See bug 1901896.)
   *
   * @param aColor the new background color
   */

  virtual void SetBackgroundColor(const nscolor& aColor) {}

  struct Cursor {
    // The system cursor chosen by the page. This is used if there's no custom
    // cursor, or if we fail to use the custom cursor in some way (if the image
    // fails to load, for example).
    nsCursor mDefaultCursor = eCursor_standard;
    // May be null, to represent no custom cursor image.
    nsCOMPtr<imgIContainer> mContainer;
    uint32_t mHotspotX = 0;
    uint32_t mHotspotY = 0;
    mozilla::ImageResolution mResolution;

    bool IsCustom() const { return !!mContainer; }

    bool operator==(const Cursor& aOther) const {
      return mDefaultCursor == aOther.mDefaultCursor &&
             mContainer.get() == aOther.mContainer.get() &&
             mHotspotX == aOther.mHotspotX && mHotspotY == aOther.mHotspotY &&
             mResolution == aOther.mResolution;
    }

    bool operator!=(const Cursor& aOther) const { return !(*this == aOther); }
  };

  /**
   * Sets the cursor for this widget.
   */
  virtual void SetCursor(const Cursor&);
  virtual void SetCustomCursorAllowed(bool);
  /**
   * If a cursor type is currently cached locally for this widget, clear the
   * cached cursor to force an update on the next SetCursor call.
   */
  void ClearCachedCursor() {
    mCursor = {};
    mUpdateCursor = true;
  }

  static nsIntSize CustomCursorSize(const Cursor&);

  /**
   * Get the window type of this widget.
   */
  WindowType GetWindowType() const { return mWindowType; }
  PopupType GetPopupType() const { return mPopupType; }
  bool HasRemoteContent() const { return mHasRemoteContent; }

  /**
   * Set the transparency mode of the top-level window containing this widget.
   * So, e.g., if you call this on the widget for an IFRAME, the top level
   * browser window containing the IFRAME actually gets set. Be careful.
   *
   * This can fail if the platform doesn't support
   * transparency/glass. By default widgets are not
   * transparent.  This will also fail if the toplevel window is not
   * a Mozilla window, e.g., if the widget is in an embedded
   * context.
   *
   * After transparency/glass has been enabled, the initial alpha channel
   * value for all pixels is 1, i.e., opaque.
   * If the window is resized then the alpha channel values for
   * all pixels are reset to 1.
   * Pixel RGB color values are already premultiplied with alpha channel values.
   */
  virtual void SetTransparencyMode(TransparencyMode aMode);

  /**
   * Get the transparency mode of the top-level window that contains this
   * widget.
   */
  virtual TransparencyMode GetTransparencyMode();

  // Cocoa and GTK round widget coordinates to the nearest global "display
  // pixel" integer value; see bug 892994. So we avoid fractional display pixel
  // values by rounding to the nearest value that won't yield a fractional
  // display pixel.
  virtual int32_t RoundsWidgetCoordinatesTo() { return 1; }
  static LayoutDeviceIntRect MaybeRoundToDisplayPixels(
      const LayoutDeviceIntRect& aRect, TransparencyMode aTransparency,
      int32_t aRound);

  LayoutDeviceIntRect MaybeRoundToDisplayPixels(
      const LayoutDeviceIntRect& aRect) {
    return MaybeRoundToDisplayPixels(aRect, GetTransparencyMode(),
                                     RoundsWidgetCoordinatesTo());
  }

  /**
   * Set the shadow style of the window.
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetWindowShadowStyle(mozilla::WindowShadow aStyle) {}

  /**
   * Set the opacity of the window.
   * Values need to be between 0.0f (invisible) and 1.0f (fully opaque).
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetWindowOpacity(float aOpacity) {}

  /**
   * Set the transform of the window. Values are in device pixels,
   * the origin is the top left corner of the window.
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetWindowTransform(const mozilla::gfx::Matrix& aTransform) {}

  /**
   * Set the preferred color-scheme for the widget. Nothing() means system
   * default. Implemented on Windows and macOS.
   */
  virtual void SetColorScheme(const mozilla::Maybe<mozilla::ColorScheme>&) {}

  /**
   * Set whether the window should ignore mouse events or not, and if it should
   * not, what input margin should it use.
   *
   * This is only used on popup windows. The margin is only implemented on
   * Linux.
   */
  struct InputRegion {
    bool mFullyTransparent = false;
    mozilla::LayoutDeviceIntCoord mMargin = 0;
  };
  virtual void SetInputRegion(const InputRegion&) {}

  /*
   * On macOS, this method shows or hides the pill button in the titlebar
   * that's used to collapse the toolbar.
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetShowsToolbarButton(bool aShow) {}

  /*
   * On macOS, this method determines whether we tell cocoa that the window
   * supports native full screen. If we do so, and another window is in
   * native full screen, this window will also appear in native full screen.
   *
   * We generally only want to do this for primary application windows.
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetSupportsNativeFullscreen(bool aSupportsNativeFullscreen) {}

  enum WindowAnimationType {
    eGenericWindowAnimation,
    eDocumentWindowAnimation
  };

  /**
   * Sets the kind of top-level window animation this widget should have. On
   * macOS, this causes a particular kind of animation to be shown when the
   * window is first made visible.
   *
   * Ignored on child widgets and on non-Mac platforms.
   */
  virtual void SetWindowAnimationType(WindowAnimationType aType) {}

  /**
   * Specifies whether the titlebar separator should be hidden.
   * Only implemented on macOS.
   */
  virtual void SetHideTitlebarSeparator(bool) {}

  // Returns whether the macOS titlebar direction is RTL instead of LTR.
  // TODO(emilio): Maybe generalize to other OSes?
  virtual bool IsMacTitlebarDirectionRTL() { return false; }

  /**
   * Hide window chrome (borders, buttons) for this widget.
   */
  virtual void HideWindowChrome(bool aShouldHide) {}

  enum FullscreenTransitionStage {
    eBeforeFullscreenToggle,
    eAfterFullscreenToggle
  };

  /**
   * Prepares for fullscreen transition and returns whether the widget
   * supports fullscreen transition. If this method returns false,
   * PerformFullscreenTransition() must never be called. Otherwise,
   * caller should call that method twice with "before" and "after"
   * stages respectively in order. In the latter case, this method may
   * return some data via aData pointer. Caller must pass that data to
   * PerformFullscreenTransition() if any, and caller is responsible
   * for releasing that data.
   */
  virtual bool PrepareForFullscreenTransition(nsISupports** aData) {
    return false;
  }

  /**
   * Performs fullscreen transition. This method returns immediately,
   * and will post aCallback to the main thread when the transition
   * finishes.
   */
  virtual void PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                           uint16_t aDuration,
                                           nsISupports* aData,
                                           nsIRunnable* aCallback);

  /**
   * Perform any actions needed after the fullscreen transition has ended.
   */
  virtual void CleanupFullscreenTransition() {}

  /**
   * Return the screen the widget is in, or null if we don't know.
   */
  virtual already_AddRefed<Screen> GetWidgetScreen();

  /**
   * Put the toplevel window into or out of fullscreen mode.
   *
   * @return NS_OK if the widget is setup properly for fullscreen and
   * FullscreenChanged callback has been or will be called. If other
   * value is returned, the caller should continue the change itself.
   */
  virtual nsresult MakeFullScreen(bool aFullScreen);
  void InfallibleMakeFullScreen(bool aFullScreen);

  /**
   * Same as MakeFullScreen, except that, on systems which natively
   * support fullscreen transition, calling this method explicitly
   * requests that behavior.
   * It is currently only supported on macOS 10.7+.
   */
  virtual nsresult MakeFullScreenWithNativeTransition(bool aFullScreen) {
    return MakeFullScreen(aFullScreen);
  }

  /**
   * Invalidate a specified rect for a widget so that it will be repainted
   * later.
   */
  virtual void Invalidate(const LayoutDeviceIntRect& aRect) = 0;

  enum LayerManagerPersistence {
    LAYER_MANAGER_CURRENT = 0,
    LAYER_MANAGER_PERSISTENT
  };

  /**
   * Return the widget's LayerManager. The layer tree for that LayerManager is
   * what gets rendered to the widget.
   *
   * Note that this tries to create a renderer if it doesn't exist.
   */
  virtual WindowRenderer* GetWindowRenderer();

  /**
   * Returns whether there's an existing window renderer.
   */
  bool HasWindowRenderer() const { return !!mWindowRenderer; }

  /**
   * Called before each layer manager transaction to allow any preparation
   * for DrawWindowUnderlay/Overlay that needs to be on the main thread.
   *
   * Always called on the main thread.
   */
  virtual void PrepareWindowEffects() {}

  /**
   * Called when Gecko knows which themed widgets exist in this window.
   * The passed array contains an entry for every themed widget of the right
   * type (currently only StyleAppearance::Toolbar) within the window, except
   * for themed widgets which are transformed or have effects applied to them
   * (e.g. CSS opacity or filters).
   * This could sometimes be called during display list construction
   * outside of painting.
   * If called during painting, it will be called before we actually
   * paint anything.
   */
  virtual void UpdateThemeGeometries(const nsTArray<ThemeGeometry>&) {}

  /**
   * Informs the widget about the region of the window that is opaque.
   *
   * @param aOpaqueRegion the region of the window that is opaque.
   */
  virtual void UpdateOpaqueRegion(const LayoutDeviceIntRegion& aOpaqueRegion) {}
  virtual LayoutDeviceIntRegion GetOpaqueRegionForTesting() const { return {}; }

  /**
   * Informs the widget about the region of the window that is draggable.
   */
  virtual void UpdateWindowDraggingRegion(
      const LayoutDeviceIntRegion& aRegion) {}

  /**
   * Tells the widget whether the given input block results in a swipe.
   * Should be called in response to a WidgetWheelEvent that has
   * mFlags.mCanTriggerSwipe set on it.
   */
  virtual void ReportSwipeStarted(uint64_t aInputBlockId, bool aStartSwipe);

  // Returns true if |aPanInput| event was used for SwipeTracker, false
  // otherwise.
  bool MayStartSwipeForNonAPZ(const mozilla::PanGestureInput& aPanInput);
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

  void NotifyWindowDestroyed();
  void NotifySizeMoveDone();
  using ByMoveToRect = nsIWidgetListener::ByMoveToRect;
  void NotifyWindowMoved(const LayoutDeviceIntPoint&,
                         ByMoveToRect = ByMoveToRect::No);
  void NotifyWindowMoved(const DesktopIntPoint&,
                         ByMoveToRect = ByMoveToRect::No);
  // Should be called by derived implementations to notify on system color and
  // theme changes. (Only one invocation per change is needed, not one
  // invocation per change per window.)
  void NotifyThemeChanged(mozilla::widget::ThemeChangeKind);
  void NotifyAPZOfDPIChange();

  // Return true if this is a simple widget (that is typically not worth
  // accelerating)
  bool IsSmallPopup() const;

  PopupLevel GetPopupLevel() { return mPopupLevel; }

  /**
   * Internal methods
   */
  virtual void* GetNativeData(uint32_t aDataType) = 0;

 protected:
  nsIWidget();
  virtual ~nsIWidget();
  explicit nsIWidget(BorderStyle);

  // These are methods for CompositorWidgetWrapper, and should only be
  // accessed from that class. Derived widgets can choose which methods to
  // implement, or none if supporting out-of-process compositing.
  virtual bool PreRender(mozilla::widget::WidgetRenderingContext* aContext) {
    return true;
  }
  virtual void PostRender(mozilla::widget::WidgetRenderingContext* aContext) {}
  virtual mozilla::layers::NativeLayerRoot* GetNativeLayerRoot() {
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

  void AddToChildList(nsIWidget* aChild);
  void RemoveFromChildList(nsIWidget* aChild);
  void RemoveAllChildren();

  void ResolveIconName(const nsAString& aIconName, const nsAString& aIconSuffix,
                       nsIFile** aResult);
  virtual void OnDestroy();
  void BaseCreate(nsIWidget* aParent, const InitData& aInitData);

  virtual void ConfigureAPZCTreeManager();
  virtual void ConfigureAPZControllerThread();
  virtual already_AddRefed<GeckoContentController>
  CreateRootContentController();

  mozilla::dom::Document* GetDocument() const;
  void EnsureTextEventDispatcher();
  // Notify the compositor that a device reset has occurred.
  void OnRenderingDeviceReset();
  bool UseAPZ() const;
  bool AllowWebRenderForThisWindow();

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
   * require the compositor to be destroyed before ~nsIWidget is
   * reached (This is the case with gtk2 for instance).
   */
  virtual void DestroyCompositor();
  void DestroyLayerManager();
  void ReleaseContentController();
  void RevokeTransactionIdAllocator();

  void FreeShutdownObserver();
  void FreeLocalesChangedObserver();

 public:
  /**
   * Set the widget's title.
   * Must be called after Create.
   *
   * @param aTitle string displayed as the title of the widget
   */
  virtual nsresult SetTitle(const nsAString& aTitle) = 0;

  /**
   * Set the widget's icon.
   * Must be called after Create.
   *
   * @param aIconSpec string specifying the icon to use; convention is to
   *                  pass a resource: URL from which a platform-dependent
   *                  resource file name will be constructed
   */
  virtual void SetIcon(const nsAString& aIconSpec) {}

  /**
   * Return this widget's client origin in screen coordinates.
   *
   * @return screen coordinates stored in the x,y members
   */
  virtual LayoutDeviceIntPoint WidgetToScreenOffset() = 0;

  /**
   * The same as WidgetToScreenOffset(), except in the case of
   * PuppetWidget where this method omits the chrome offset.
   */
  virtual LayoutDeviceIntPoint TopLevelWidgetToScreenOffset() {
    return WidgetToScreenOffset();
  }

  /**
   * For a PuppetWidget, returns the transform from the coordinate
   * space of the PuppetWidget to the coordinate space of the
   * top-level native widget.
   *
   * Identity transform in other cases.
   */
  virtual mozilla::LayoutDeviceToLayoutDeviceMatrix4x4
  WidgetToTopLevelWidgetTransform() {
    return mozilla::LayoutDeviceToLayoutDeviceMatrix4x4();
  }

  mozilla::LayoutDeviceIntPoint WidgetToTopLevelWidgetOffset() {
    return mozilla::LayoutDeviceIntPoint::Round(
        WidgetToTopLevelWidgetTransform().TransformPoint(
            mozilla::LayoutDevicePoint()));
  }

  /**
   * Returns the margins that are applied to go from client sizes to window
   * sizes on a normal sizemode window (which includes window borders and
   * titlebar).
   *
   * This method should work even when the window is not yet visible.
   */
  virtual LayoutDeviceIntMargin NormalSizeModeClientToWindowMargin() {
    return {};
  }

  /**
   * Returns the size difference from client area to window area of a
   * normal-sizemode window.
   */
  LayoutDeviceIntSize NormalSizeModeClientToWindowSizeDifference();

  /**
   * Dispatches an event to the widget
   */
  virtual nsEventStatus DispatchEvent(mozilla::WidgetGUIEvent*);

  /**
   * Dispatches an event to APZ only.
   * No-op in the child process.
   */
  virtual void DispatchEventToAPZOnly(mozilla::WidgetInputEvent* aEvent);

  /*
   * Dispatch a gecko event for this widget.
   * Returns true if it's consumed.  Otherwise, false.
   */
  virtual bool DispatchWindowEvent(mozilla::WidgetGUIEvent& event);

  // A structure that groups the statuses from APZ dispatch and content
  // dispatch.
  struct ContentAndAPZEventStatus {
    // Either of these may not be set if the event was not dispatched
    // to APZ or to content.
    nsEventStatus mApzStatus = nsEventStatus_eIgnore;
    nsEventStatus mContentStatus = nsEventStatus_eIgnore;
  };

  /**
   * Dispatches an event that must be handled by APZ first, when APZ is
   * enabled. If invoked in the child process, it is forwarded to the
   * parent process synchronously.
   */
  virtual ContentAndAPZEventStatus DispatchInputEvent(
      mozilla::WidgetInputEvent* aEvent);

  /**
   * Confirm an APZ-aware event target. This should be used when APZ will
   * not need a layers update to process the event.
   */
  virtual void SetConfirmedTargetAPZC(
      uint64_t aInputBlockId,
      const nsTArray<ScrollableLayerGuid>& aTargets) const;

  /**
   * Returns true if APZ is in use, false otherwise.
   */
  virtual bool AsyncPanZoomEnabled() const;

  /**
   */
  virtual void SwipeFinished();

  /**
   * Enables the dropping of files to a widget.
   */
  virtual void EnableDragDrop(bool aEnable) {}
  void AsyncEnableDragDrop(bool aEnable);

  /**
   * Classify the window for the window manager. Mostly for X11.
   *
   *  @param xulWinType The window type. Characters other than [A-Za-z0-9_-] are
   *                    converted to '_'. Anything before the first colon is
   *                    assigned to name, anything after it to role. If there's
   *                    no colon, assign the whole thing to both role and name.
   *
   * @param xulWinClass The window class. If set, overrides the normal value.
   *                    Otherwise, the program class it used.
   *
   * @param xulWinName The window name. If set, overrides the value specified in
   *                   window type. Otherwise, name from window type is used.
   *
   */
  virtual void SetWindowClass(const nsAString& xulWinType,
                              const nsAString& xulWinClass,
                              const nsAString& xulWinName) {}

  virtual void SetIsEarlyBlankWindow(bool) {}

  /**
   * Enables/Disables system capture of any and all events that would cause a
   * popup to be rolled up. aListener should be set to a non-null value for
   * any popups that are not managed by the popup manager.
   * @param aDoCapture true enables capture, false disables capture
   *
   */
  virtual void CaptureRollupEvents(bool aDoCapture) {}

  /**
   * Bring this window to the user's attention.  This is intended to be a more
   * gentle notification than popping the window to the top or putting up an
   * alert.  See, for example, Win32 FlashWindow or the NotificationManager on
   * the Mac.  The notification should be suppressed if the window is already
   * in the foreground and should be dismissed when the user brings this window
   * to the foreground.
   * @param aCycleCount Maximum number of times to animate the window per system
   *                    conventions. If set to -1, cycles indefinitely until
   *                    window is brought into the foreground.
   */
  [[nodiscard]] virtual nsresult GetAttention(int32_t aCycleCount) {
    return NS_OK;
  }

  /**
   * Ask whether there user input events pending.  All input events are
   * included, including those not targeted at this nsIwidget instance.
   */
  virtual bool HasPendingInputEvent();

  /*
   * Determine whether the widget shows a resize widget. If it does,
   * aResizerRect returns the resizer's rect.
   *
   * Returns false on any platform that does not support it.
   *
   * @param aResizerRect The resizer's rect in device pixels.
   * @return Whether a resize widget is shown.
   */
  virtual bool ShowsResizeIndicator(LayoutDeviceIntRect* aResizerRect);

  // Dispatch an event that has already been routed through APZ.
  nsEventStatus ProcessUntransformedAPZEvent(
      mozilla::WidgetInputEvent* aEvent,
      const mozilla::layers::APZEventResult& aApzResult);

  // TODO: Make this an enum class with MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS or
  //       EnumSet class.
  enum Modifiers : uint32_t {
    NO_MODIFIERS = 0x00000000,
    CAPS_LOCK = 0x00000001,  // when CapsLock is active
    NUM_LOCK = 0x00000002,   // when NumLock is active
    SHIFT_L = 0x00000100,
    SHIFT_R = 0x00000200,
    CTRL_L = 0x00000400,
    CTRL_R = 0x00000800,
    ALT_L = 0x00001000,  // includes Option
    ALT_R = 0x00002000,
    COMMAND_L = 0x00004000,
    COMMAND_R = 0x00008000,
    HELP = 0x00010000,
    ALTGRAPH = 0x00020000,  // AltGr key on Windows.  This emulates
                            // AltRight key behavior of keyboard
                            // layouts which maps AltGr to AltRight
                            // key.
    FUNCTION = 0x00100000,
    NUMERIC_KEY_PAD = 0x01000000  // when the key is coming from the keypad
  };
  /**
   * Utility method intended for testing. Dispatches native key events
   * to this widget to simulate the press and release of a key.
   * @param aNativeKeyboardLayout a *platform-specific* constant.
   * On Mac, this is the resource ID for a 'uchr' or 'kchr' resource.
   * On Windows, it is converted to a hex string and passed to
   * LoadKeyboardLayout, see
   * http://msdn.microsoft.com/en-us/library/ms646305(VS.85).aspx
   * @param aNativeKeyCode a *platform-specific* keycode.
   * On Windows, this is the virtual key code.
   * @param aModifiers some combination of the above 'Modifiers' flags;
   * not all flags will apply to all platforms. Mac ignores the _R
   * modifiers. Windows ignores COMMAND, NUMERIC_KEY_PAD, HELP and
   * FUNCTION.
   * @param aCharacters characters that the OS would decide to generate
   * from the event. On Windows, this is the charCode passed by
   * WM_CHAR.
   * @param aUnmodifiedCharacters characters that the OS would decide
   * to generate from the event if modifier keys (other than shift)
   * were assumed inactive. Needed on Mac, ignored on Windows.
   * @param aCallback the callback that will get notified once the events
   * have been dispatched.
   * @return NS_ERROR_NOT_AVAILABLE to indicate that the keyboard
   * layout is not supported and the event was not fired
   */
  virtual nsresult SynthesizeNativeKeyEvent(
      int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
      uint32_t aModifierFlags, const nsAString& aCharacters,
      const nsAString& aUnmodifiedCharacters,
      nsISynthesizedEventCallback* aCallback) {
    mozilla::widget::AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    return NS_ERROR_UNEXPECTED;
  }

  /**
   * Utility method intended for testing. Dispatches native mouse events
   * may even move the mouse cursor. On Mac the events are guaranteed to
   * be sent to the window containing this widget, but on Windows they'll go
   * to whatever's topmost on the screen at that position, so for
   * cross-platform testing ensure that your window is at the top of the
   * z-order.
   * @param aPoint screen location of the mouse, in device
   * pixels, with origin at the top left
   * @param aNativeMessage abstract native message.
   * @param aButton Mouse button defined by DOM UI Events.
   * @param aModifierFlags Some values of nsIWidget::Modifiers.
   *                       FYI: On Windows, Android and Headless widget on all
   *                       platroms, this hasn't been handled yet.
   * @param aCallback the callback that will get notified once the events
   * have been dispatched.
   */
  enum class NativeMouseMessage : uint32_t {
    ButtonDown,   // button down
    ButtonUp,     // button up
    Move,         // mouse cursor move
    EnterWindow,  // mouse cursor comes into a window
    LeaveWindow,  // mouse cursor leaves from a window
  };
  virtual nsresult SynthesizeNativeMouseEvent(
      LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
      mozilla::MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
      nsISynthesizedEventCallback* aCallback) {
    mozilla::widget::AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    return NS_ERROR_UNEXPECTED;
  }

  /**
   * A shortcut to SynthesizeNativeMouseEvent, abstracting away the native
   * message. aPoint is location in device pixels to which the mouse pointer
   * moves to.
   * @param aCallback the callback that will get notified once the events
   * have been dispatched.
   */
  virtual nsresult SynthesizeNativeMouseMove(
      LayoutDeviceIntPoint aPoint, nsISynthesizedEventCallback* aCallback) {
    mozilla::widget::AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    return NS_ERROR_UNEXPECTED;
  }

  /**
   * Utility method intended for testing. Dispatching native mouse scroll
   * events may move the mouse cursor.
   *
   * @param aPoint            Mouse cursor position in screen coordinates.
   *                          In device pixels, the origin at the top left of
   *                          the primary display.
   * @param aNativeMessage    Platform native message.
   * @param aDeltaX           The delta value for X direction.  If the native
   *                          message doesn't indicate X direction scrolling,
   *                          this may be ignored.
   * @param aDeltaY           The delta value for Y direction.  If the native
   *                          message doesn't indicate Y direction scrolling,
   *                          this may be ignored.
   * @param aDeltaZ           The delta value for Z direction.  If the native
   *                          message doesn't indicate Z direction scrolling,
   *                          this may be ignored.
   * @param aModifierFlags    Must be values of Modifiers, or zero.
   * @param aAdditionalFlags  See nsIDOMWidnowUtils' consts and their
   *                          document.
   * @param aCallback         The callback that will get notified once the
   *                          events have been dispatched.
   */
  virtual nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
      uint32_t aAdditionalFlags, nsISynthesizedEventCallback* aCallback) {
    mozilla::widget::AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    return NS_ERROR_UNEXPECTED;
  }

  /*
   * TouchpadGesturePhase states for SynthesizeNativeTouchPadPinch and
   * SynthesizeNativeTouchpadPan. Match phase states in nsIDOMWindowUtils.idl.
   */
  enum TouchpadGesturePhase {
    PHASE_BEGIN = 0,
    PHASE_UPDATE = 1,
    PHASE_END = 2
  };
  /*
   * Create a new or update an existing touch pointer on the digitizer.
   * To trigger os level gestures, individual touch points should
   * transition through a complete set of touch states which should be
   * sent as individual messages.
   *
   * @param aPointerId The touch point id to create or update.
   * @param aPointerState one or more of the touch states listed above
   * @param aPoint coords of this event
   * @param aPressure 0.0 -> 1.0 float val indicating pressure
   * @param aOrientation 0 -> 359 degree value indicating the
   * orientation of the pointer. Use 90 for normal taps.
   * @param aCallback The callback that will get notified once the events
   * have been dispatched.
   */
  virtual nsresult SynthesizeNativeTouchPoint(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPointerPressure,
      uint32_t aPointerOrientation, nsISynthesizedEventCallback* aCallback) {
    mozilla::widget::AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    return NS_ERROR_UNEXPECTED;
  }
  /*
   * See nsIDOMWindowUtils.sendNativeTouchpadPinch().
   */
  virtual nsresult SynthesizeNativeTouchPadPinch(
      TouchpadGesturePhase aEventPhase, float aScale,
      LayoutDeviceIntPoint aPoint, int32_t aModifierFlags) {
    MOZ_CRASH("SynthesizeNativeTouchPadPinch not implemented on this platform");
    return NS_ERROR_UNEXPECTED;
  }

  /*
   * Helper for simulating a simple tap event with one touch point. When
   * aLongTap is true, simulates a native long tap with a duration equal to
   * ui.click_hold_context_menus.delay. This pref is compatible with the
   * apzc long tap duration. Defaults to 1.5 seconds.
   * @param aCallback The callback that will get notified once the events
   * have been dispatched.
   */
  virtual nsresult SynthesizeNativeTouchTap(
      LayoutDeviceIntPoint aPoint, bool aLongTap,
      nsISynthesizedEventCallback* aCallback);

  virtual nsresult SynthesizeNativePenInput(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPressure, uint32_t aRotation,
      int32_t aTiltX, int32_t aTiltY, int32_t aButton,
      nsISynthesizedEventCallback* aCallback) {
    MOZ_CRASH("SynthesizeNativePenInput not implemented on this platform");
    return NS_ERROR_UNEXPECTED;
  }

  /*
   * Send a native event as if the user double tapped the touchpad with two
   * fingers.
   */
  virtual nsresult SynthesizeNativeTouchpadDoubleTap(
      LayoutDeviceIntPoint aPoint, uint32_t aModifierFlags) {
    MOZ_CRASH(
        "SynthesizeNativeTouchpadDoubleTap not implemented on this platform");
    return NS_ERROR_UNEXPECTED;
  }

  /*
   * See nsIDOMWindowUtils.sendNativeTouchpadPan().
   */
  virtual nsresult SynthesizeNativeTouchpadPan(
      TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
      double aDeltaX, double aDeltaY, int32_t aModifierFlags,
      nsISynthesizedEventCallback* aCallback) {
    MOZ_CRASH("SynthesizeNativeTouchpadPan not implemented on this platform");
    return NS_ERROR_UNEXPECTED;
  }

  virtual void StartAsyncScrollbarDrag(const AsyncDragMetrics& aDragMetrics);

  /**
   * Notify APZ to start autoscrolling.
   * @param aAnchorLocation the location of the autoscroll anchor
   * @param aGuid identifies the scroll frame to be autoscrolled
   * @return true if APZ has been successfully notified
   */
  virtual bool StartAsyncAutoscroll(const ScreenPoint& aAnchorLocation,
                                    const ScrollableLayerGuid& aGuid);

  /**
   * Notify APZ to stop autoscrolling.
   * @param aGuid identifies the scroll frame which is being autoscrolled.
   */
  virtual void StopAsyncAutoscroll(const ScrollableLayerGuid& aGuid);

  virtual LayersId GetRootLayerTreeId();

  /**
   * Use this when GetLayerManager() returns a BasicLayerManager
   * (nsIWidget::GetLayerManager() does). This sets up the widget's
   * layer manager to temporarily render into aTarget.
   *
   * |aNaturalWidgetBounds| is the un-rotated bounds of |aWidget|.
   * |aRotation| is the "virtual rotation" to apply when rendering to
   * the target.  When |aRotation| is ROTATION_0,
   * |aNaturalWidgetBounds| is not used.
   */
  class AutoLayerManagerSetup {
   public:
    AutoLayerManagerSetup(nsIWidget* aWidget, gfxContext* aTarget);
    ~AutoLayerManagerSetup();

   private:
    nsIWidget* mWidget;
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

  // If this widget supports out-of-process compositing, it can override
  // this method to provide additional information to the compositor.
  virtual void GetCompositorWidgetInitData(
      mozilla::widget::CompositorWidgetInitData* aInitData) {}

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

  already_AddRefed<WindowRenderer> CreateFallbackRenderer();

  /**
   * Returns a FallbackRenderer which is intended to be temporary while
   * backgrounded without a GPU process. It listens to GPUProcessManager events
   * in order to destroy itself when the GPU process becomes available.
   */
  already_AddRefed<WindowRenderer> CreateBackgroundedFallbackRenderer();

  /**
   * Setter/Getter of the system font setting for testing.
   */
  virtual nsresult SetSystemFont(const nsCString& aFontName) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  virtual nsresult GetSystemFont(nsCString& aFontName) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Wayland specific routines.
   */
  virtual LayoutDeviceIntSize GetMoveToRectPopupSize() {
    NS_WARNING("GetLayoutPopupRect implemented only for wayland");
    return LayoutDeviceIntSize();
  }

  enum class NativePointerLockMode : uint8_t {
    Regular,
    Unadjusted,
  };

  virtual void LockNativePointer(NativePointerLockMode aNativePointerLockMode) {
  }
  virtual void UnlockNativePointer() {}

  /**
   * Inform the widget to change the native pointer lock mode. This is a no-op
   * if the native pointer isn't locked or if the platform doesn't support
   * unadjusted movement.
   */
  virtual void SetNativePointerLockMode(
      NativePointerLockMode aNativePointerLockMode) {}

  /**
   * Whether unadjusted movement is supported for native pointer lock.
   */
  virtual bool SupportsUnadjustedMovement() { return false; }

  /*
   * Get safe area insets except to cutout.
   * See https://drafts.csswg.org/css-env-1/#safe-area-insets.
   */
  virtual mozilla::LayoutDeviceIntMargin GetSafeAreaInsets() const {
    return mozilla::LayoutDeviceIntMargin();
  }

 private:
  class LongTapInfo {
   public:
    LongTapInfo(int32_t aPointerId, LayoutDeviceIntPoint& aPoint,
                mozilla::TimeDuration aDuration,
                nsISynthesizedEventCallback* aCallback)
        : mPointerId(aPointerId),
          mPosition(aPoint),
          mDuration(aDuration),
          mCallback(aCallback),
          mStamp(mozilla::TimeStamp::Now()) {}

    int32_t mPointerId;
    LayoutDeviceIntPoint mPosition;
    mozilla::TimeDuration mDuration;
    nsCOMPtr<nsISynthesizedEventCallback> mCallback;
    mozilla::TimeStamp mStamp;
  };

  static void OnLongTapTimerCallback(nsITimer* aTimer, void* aClosure);

  static already_AddRefed<nsIBidiKeyboard> CreateBidiKeyboardContentProcess();
  static already_AddRefed<nsIBidiKeyboard> CreateBidiKeyboardInner();

  mozilla::UniquePtr<LongTapInfo> mLongTapTouchPoint;
  nsCOMPtr<nsITimer> mLongTapTimer;
  static int32_t sPointerIdCounter;

 public:
  /**
   * If key events have not been handled by content or XBL handlers, they can
   * be offered to the system (for custom application shortcuts set in system
   * preferences, for example).
   */
  virtual void PostHandleKeyEvent(mozilla::WidgetKeyboardEvent* aEvent);

  /**
   * Activates a native menu item at the position specified by the index
   * string. The index string is a string of positive integers separated
   * by the "|" (pipe) character. The last integer in the string represents
   * the item index in a submenu located using the integers preceding it.
   *
   * Example: 1|0|4
   * In this string, the first integer represents the top-level submenu
   * in the native menu bar. Since the integer is 1, it is the second submeu
   * in the native menu bar. Within that, the first item (index 0) is a
   * submenu, and we want to activate the 5th item within that submenu.
   */
  virtual nsresult ActivateNativeMenuItemAt(const nsAString& indexString) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * This is used for native menu system testing.
   *
   * Updates a native menu at the position specified by the index string.
   * The index string is a string of positive integers separated by the "|"
   * (pipe) character.
   *
   * Example: 1|0|4
   * In this string, the first integer represents the top-level submenu
   * in the native menu bar. Since the integer is 1, it is the second submeu
   * in the native menu bar. Within that, the first item (index 0) is a
   * submenu, and we want to update submenu at index 4 within that submenu.
   *
   * If this is called with an empty string it forces a full reload of the
   * menu system.
   */
  virtual nsresult ForceUpdateNativeMenuAt(const nsAString& indexString) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * This is used for testing macOS service menu code.
   *
   * @param aResult - the current text selection. Is empty if no selection.
   * @return nsresult - whether or not aResult was assigned the selected text.
   */
  [[nodiscard]] virtual nsresult GetSelectionAsPlaintext(nsAString& aResult) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Notify IME of the specified notification.
   *
   * @return If the notification is mouse button event and it's consumed by
   *         IME, this returns NS_SUCCESS_EVENT_CONSUMED.
   */
  nsresult NotifyIME(const IMENotification& aIMENotification);

  /**
   * MaybeDispatchInitialFocusEvent will dispatch a focus event after creation
   * of the widget, in the event that we were not able to observe and respond to
   * the initial focus event. This is necessary for the early skeleton UI
   * window, which is displayed and receives its initial focus event before we
   * can actually respond to it.
   */
  virtual void MaybeDispatchInitialFocusEvent() {}

  /*
   * Notifies the input context changes.
   */
  virtual void SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) = 0;

  /*
   * Get current input context.
   */
  virtual InputContext GetInputContext() = 0;

  /**
   * Get native IME context.  This is different from GetNativeData() with
   * NS_RAW_NATIVE_IME_CONTEXT, the result is unique even if in a remote
   * process.
   */
  virtual NativeIMEContext GetNativeIMEContext();

  /**
   * GetPseudoIMEContext() returns pseudo IME context when TextEventDispatcher
   * has non-native input transaction.  Otherwise, returns nullptr.
   */
  void* GetPseudoIMEContext();

  /*
   * Given a WidgetKeyboardEvent, this method synthesizes a corresponding
   * native (OS-level) event for it. This method allows tests to simulate
   * keystrokes that trigger native key bindings (which require a native
   * event).
   */
  [[nodiscard]] virtual nsresult AttachNativeKeyEvent(
      mozilla::WidgetKeyboardEvent& aEvent) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Retrieve edit commands when the key combination of aEvent is used
   * in platform native applications.
   */
  MOZ_CAN_RUN_SCRIPT virtual bool GetEditCommands(
      mozilla::NativeKeyBindingsType aType,
      const mozilla::WidgetKeyboardEvent& aEvent,
      nsTArray<mozilla::CommandInt>& aCommands);

  /*
   * Retrieves a reference to notification requests of IME.  Note that the
   * reference is valid while the nsIWidget instance is alive.  So, if you
   * need to store the reference for a long time, you need to grab the widget
   * instance too.
   */
  const IMENotificationRequests& IMENotificationRequestsRef();

  bool ComputeShouldAccelerate();
  virtual bool WidgetTypeSupportsAcceleration() { return true; }
  virtual bool WidgetTypeSupportsNativeCompositing() { return true; }

  /*
   * Call this method when a dialog is opened which has a default button.
   * The button's rectangle should be supplied in aButtonRect.
   */
  [[nodiscard]] virtual nsresult OnDefaultButtonLoaded(
      const LayoutDeviceIntRect& aButtonRect) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Return true if this process shouldn't use platform widgets, and
   * so should use PuppetWidgets instead.  If this returns true, the
   * result of creating and using a platform widget is undefined,
   * and likely to end in crashes or other buggy behavior.
   */
  static bool UsePuppetWidgets() { return XRE_IsContentProcess(); }

  static already_AddRefed<nsIWidget> CreateTopLevelWindow();

  static already_AddRefed<nsIWidget> CreateChildWindow();

  /**
   * Allocate and return a "puppet widget" that doesn't directly
   * correlate to a platform widget; platform events and data must
   * be fed to it.  Currently used in content processes.  NULL is
   * returned if puppet widgets aren't supported in this build
   * config, on this platform, or for this process type.
   *
   * This function is called "Create" to match CreateInstance().
   * The returned widget must still be nsIWidget::Create()d.
   */
  static already_AddRefed<nsIWidget> CreatePuppetWidget(
      BrowserChild* aBrowserChild);

  static already_AddRefed<nsIWidget> CreateHeadlessWidget();

  /**
   * Return true if widget has it's own GL context
   */
  virtual bool HasGLContext() { return false; }

  /**
   * Returns true to indicate that this widget paints an opaque background
   * that we want to be visible under the page, so layout should not force
   * a default background.
   */
  virtual bool WidgetPaintsBackground() { return false; }

  virtual bool NeedsPaint() { return IsVisible() && !GetBounds().IsEmpty(); }

  /**
   * Get the natural bounds of this widget.  This method is only
   * meaningful for widgets for which Gecko implements screen
   * rotation natively.  When this is the case, GetBounds() returns
   * the widget bounds taking rotation into account, and
   * GetNaturalBounds() returns the bounds *not* taking rotation
   * into account.
   *
   * No code outside of the composition pipeline should know or care
   * about this.  If you're not an agent of the compositor, you
   * probably shouldn't call this method.
   */
  virtual LayoutDeviceIntRect GetNaturalBounds() { return GetBounds(); }

  /**
   * Set size constraints on the window size such that it is never less than
   * the specified minimum size and never larger than the specified maximum
   * size. The size constraints are sizes of the outer rectangle including
   * the window frame and title bar. Use 0 for an unconstrained minimum size
   * and NS_MAXSIZE for an unconstrained maximum size. Note that this method
   * does not necessarily change the size of a window to conform to this size,
   * thus Resize should be called afterwards.
   *
   * @param aConstraints: the size constraints in desktop pixels
   */
  virtual void SetSizeConstraints(const SizeConstraints& aConstraints);

#ifdef ACCESSIBILITY
  // Get the accessible for the window.
  mozilla::a11y::LocalAccessible* GetRootAccessible();
#endif

  /**
   * Return the size constraints currently observed by the widget.
   *
   * @return the constraints in desktop pixels
   */
  virtual const SizeConstraints GetSizeConstraints();

  /**
   * Apply the current size constraints to the given size. The size and
   * constraints are converted through the widget's current scale factor
   * so that callers can continue to work in device pixels.
   *
   * @param aWidth width in device pixels to constrain
   * @param aHeight height in device pixels to constrain
   */
  virtual void ConstrainSize(int32_t* aWidth, int32_t* aHeight) {
    SizeConstraints c = GetSizeConstraints();
    const double scale = GetDesktopToDeviceScale().scale;
    auto ToDevice = [scale](int32_t aDesktop) {
      return aDesktop == NS_MAXSIZE ? NS_MAXSIZE
                                    : NSToIntRound(aDesktop * scale);
    };
    *aWidth = std::clamp(*aWidth, ToDevice(c.mMinSize.width),
                         ToDevice(c.mMaxSize.width));
    *aHeight = std::clamp(*aHeight, ToDevice(c.mMinSize.height),
                          ToDevice(c.mMaxSize.height));
  }

  /**
   * If this is owned by a BrowserChild, return that.  Otherwise return
   * null.
   */
  virtual BrowserChild* GetOwningBrowserChild() { return nullptr; }

  /*
   * Returns the layersId for this widget.
   */
  virtual LayersId GetLayersId() const;

  /**
   * If this isn't directly compositing to its window surface,
   * return the compositor which is doing that on our behalf.
   */
  virtual CompositorBridgeChild* GetRemoteRenderer();

  /**
   * If there is a remote renderer, pause or resume it.
   */
  virtual void PauseOrResumeCompositor(bool aPause);

  /**
   * Clear WebRender resources
   */
  virtual void ClearCachedWebrenderResources();

  /**
   * Request fast snapshot at RenderCompositor of WebRender.
   * Since readback of Windows DirectComposition is very slow.
   */
  virtual bool SetNeedFastSnaphot();

  /** Notify the widget that this window is being used with OMTC. */
  virtual void WindowUsesOMTC() {}
  virtual void RegisterTouchWindow() {}

  /**
   * If this widget has its own vsync dispatcher, return it, otherwise return
   * nullptr. An example of such a local vsync dispatcher would be Wayland frame
   * callbacks.
   */
  virtual RefPtr<mozilla::VsyncDispatcher> GetVsyncDispatcher();

  /**
   * Returns true if the widget requires synchronous repaints on resize,
   * false otherwise.
   */
  virtual bool SynchronouslyRepaintOnResize() { return true; }

  virtual void UpdateZoomConstraints(
      const uint32_t& aPresShellId, const ScrollableLayerGuid::ViewID& aViewId,
      const mozilla::Maybe<ZoomConstraints>& aConstraints);

  /**
   * GetTextEventDispatcher() returns TextEventDispatcher belonging to the
   * widget.  Note that this never returns nullptr.
   */
  TextEventDispatcher* GetTextEventDispatcher();

  // Gets the pres shell this widget is managed by.
  mozilla::PresShell* GetPresShell() const;

  /**
   * GetNativeTextEventDispatcherListener() returns a
   * TextEventDispatcherListener instance which is used when the widget
   * instance handles native IME and/or keyboard events.
   */
  virtual TextEventDispatcherListener* GetNativeTextEventDispatcherListener();

  /**
   * Trigger an animation to zoom to the given |aRect|.
   * |aRect| should be relative to the layout viewport of the widget's root
   * document
   */
  virtual void ZoomToRect(const uint32_t& aPresShellId,
                          const ScrollableLayerGuid::ViewID& aViewId,
                          const CSSRect& aRect, const uint32_t& aFlags);

  /**
   * LookUpDictionary shows the dictionary for the word around current point.
   *
   * @param aText            the word to look up dictiorary.
   * @param aFontRangeArray  text decoration of aText
   * @param aIsVertical      true if the word is vertical layout
   * @param aPoint           top-left point of aText
   */
  virtual void LookUpDictionary(
      const nsAString& aText,
      const nsTArray<mozilla::FontRange>& aFontRangeArray,
      const bool aIsVertical, const LayoutDeviceIntPoint& aPoint) {}

  virtual void RequestFxrOutput() {
    MOZ_ASSERT(false, "This function should only execute in Windows");
  }

  /**
   * NotifyCompositorScrollUpdate notify widget about an update to the
   * composited scroll offset and zoom
   */
  virtual void NotifyCompositorScrollUpdate(
      const mozilla::layers::CompositorScrollUpdate& aUpdate) {}

#if defined(MOZ_WIDGET_ANDROID)
  /**
   * RecvToolbarAnimatorMessageFromCompositor receive message from compositor
   * thread.
   *
   * @param aMessage message being sent to Android UI thread.
   */
  virtual void RecvToolbarAnimatorMessageFromCompositor(int32_t aMessage) {}

  virtual void UpdateDynamicToolbarMaxHeight(mozilla::ScreenIntCoord aHeight) {}
  virtual mozilla::ScreenIntCoord GetDynamicToolbarMaxHeight() const {
    return 0;
  }
#endif

  void EnsureLocalesChangedObserver();
  virtual void LocalesChanged() {}
  virtual void NotifyOcclusionState(mozilla::widget::OcclusionState) {}

  static already_AddRefed<nsIBidiKeyboard> CreateBidiKeyboard();

  // If this is a popup, returns the associated frame if any.
  nsMenuPopupFrame* GetPopupFrame() const;
  // Returns the frame currently associated to this widget.
  nsIFrame* GetFrame() const;

  /**
   * Like GetDefaultScale, but taking into account only the system settings
   * and ignoring Gecko preferences.
   */
  virtual double GetDefaultScaleInternal() { return 1.0; }

  // On a given platform, we might have three kinds of widgets:
  //   In the parent process, we might have native, puppet, or headless widgets.
  //   In child processes, we only have Puppet widgets.
  enum class WidgetType : uint8_t {
    Native,
    Headless,
    Puppet,
  };
  bool IsPuppetWidget() const { return mWidgetType == WidgetType::Puppet; }
  bool IsHeadlessWidget() const { return mWidgetType == WidgetType::Headless; }
  bool IsNativeWidget() const { return mWidgetType == WidgetType::Native; }

  using WindowButtonType = mozilla::WindowButtonType;

  /**
   * Layout uses this to alert the widget to the client rect representing
   * the window maximize button.  An empty rect indicates there is no
   * maximize button (for example, in fullscreen).  This is only implemented
   * on Windows.
   */
  virtual void SetWindowButtonRect(WindowButtonType aButtonType,
                                   const LayoutDeviceIntRect& aClientRect) {}

  /**
   * Provide haptic feedback for this widget.
   */
  virtual void PerformHapticFeedback(mozilla::HapticFeedbackType aType) {}

#ifdef DEBUG
  virtual nsresult SetHiDPIMode(bool aHiDPI) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  virtual nsresult RestoreHiDPIMode() { return NS_ERROR_NOT_IMPLEMENTED; }
#endif

 protected:
  // keep the list of children.  We also keep track of our siblings.
  // The ownership model is as follows: parent holds a strong ref to
  // the first element of the list, and each element holds a strong
  // ref to the next element in the list.  The prevsibling and
  // lastchild pointers are weak, which is fine as long as they are
  // maintained properly.
  nsCOMPtr<nsIWidget> mFirstChild;
  nsIWidget* MOZ_NON_OWNING_REF mLastChild = nullptr;
  nsCOMPtr<nsIWidget> mNextSibling;
  nsIWidget* MOZ_NON_OWNING_REF mPrevSibling = nullptr;
  // Keeps us alive.
  nsIWidget* MOZ_NON_OWNING_REF mParent = nullptr;
  // When Destroy() is called, the sub class should set this true.
  bool mOnDestroyCalled = false;
  WindowType mWindowType = WindowType::TopLevel;
  WidgetType mWidgetType = WidgetType::Native;

  nsIWidgetListener* mWidgetListener = nullptr;
  nsIWidgetListener* mAttachedWidgetListener = nullptr;
  nsIWidgetListener* mPreviouslyAttachedWidgetListener = nullptr;
  RefPtr<WindowRenderer> mWindowRenderer;
  RefPtr<CompositorSession> mCompositorSession;
  RefPtr<CompositorBridgeChild> mCompositorBridgeChild;

  mozilla::UniquePtr<mozilla::Mutex> mCompositorVsyncDispatcherLock;
  RefPtr<mozilla::CompositorVsyncDispatcher> mCompositorVsyncDispatcher;

  RefPtr<IAPZCTreeManager> mAPZC;
  RefPtr<GeckoContentController> mRootContentController;
  RefPtr<APZEventState> mAPZEventState;
  RefPtr<mozilla::widget::WidgetShutdownObserver> mShutdownObserver;
  RefPtr<mozilla::widget::LocalesChangedObserver> mLocalesChangedObserver;
  RefPtr<TextEventDispatcher> mTextEventDispatcher;
  RefPtr<mozilla::SwipeTracker> mSwipeTracker;
  mozilla::UniquePtr<mozilla::SwipeEventQueue> mSwipeEventQueue;
  Cursor mCursor;
  bool mCustomCursorAllowed = true;
  BorderStyle mBorderStyle;
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

  mozilla::widget::PiPType mPiPType;

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

#endif  // nsIWidget_h_
