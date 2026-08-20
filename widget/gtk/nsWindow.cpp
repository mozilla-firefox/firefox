/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindow.h"
#ifdef MOZ_X11
#  include "nsWindowX11.h"
#endif
#ifdef MOZ_WAYLAND
#  include "nsWindowWayland.h"
#endif

#include <algorithm>
#include <cstdint>
#ifdef MOZ_X11
#  include <X11/Xlib.h>
#  include <X11/extensions/XInput2.h>
#endif
#include <dlfcn.h>
#include <gdk/gdkkeysyms.h>

#include "GLContext.h"
#include "GLContextProvider.h"
#include "GSettings.h"
#include "GtkCompositorWidget.h"
#include "InputData.h"
#include "VsyncSource.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxPlatformGtk.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "mozilla/Assertions.h"
#include "mozilla/Components.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mozilla.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/SwipeTracker.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/WritingModes.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/HelpersCairo.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/widget/WindowOcclusionState.h"
#ifdef MOZ_X11
#  include "mozilla/X11Util.h"
#endif
#include "NativeKeyBindings.h"
#include "NativeMenuGtk.h"
#include "Screen.h"
#include "ScreenHelperGTK.h"
#include "SystemTimeConverter.h"
#include "WidgetUtilsGtk.h"
#include "mozilla/XREAppData.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsAppRunner.h"
#include "nsDragService.h"
#include "nsDragServiceGtk.h"
#include "nsGTKToolkit.h"
#include "nsGfxCIID.h"
#include "nsGtkCursors.h"
#include "nsGtkKeyUtils.h"
#include "nsGtkUtils.h"
#include "nsIFile.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsINode.h"
#include "nsIRollupListener.h"
#include "nsIScreenManager.h"
#include "nsIUserIdleServiceInternal.h"
#include "nsIWidgetListener.h"
#include "nsImageToPixbuf.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPresContext.h"
#include "nsShmImage.h"
#include "nsString.h"
#include "nsWidgetsCID.h"
#include "nsXPLookAndFeel.h"
#include "prlink.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/LocalAccessible.h"
#  include "mozilla/a11y/Platform.h"
#  include "nsAccessibilityService.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

// Don't put more than this many rects in the dirty region, just fluff
// out to the bounding-box if there are more
#define MAX_RECTS_IN_REGION 100

#if !GTK_CHECK_VERSION(3, 22, 23)

constexpr gint GDK_WINDOW_STATE_TOP_TILED = 1 << 9;
constexpr gint GDK_WINDOW_STATE_TOP_RESIZABLE = 1 << 10;
constexpr gint GDK_WINDOW_STATE_RIGHT_TILED = 1 << 11;
constexpr gint GDK_WINDOW_STATE_RIGHT_RESIZABLE = 1 << 12;
constexpr gint GDK_WINDOW_STATE_BOTTOM_TILED = 1 << 13;
constexpr gint GDK_WINDOW_STATE_BOTTOM_RESIZABLE = 1 << 14;
constexpr gint GDK_WINDOW_STATE_LEFT_TILED = 1 << 15;
constexpr gint GDK_WINDOW_STATE_LEFT_RESIZABLE = 1 << 16;

#endif

constexpr gint kPerSideTiledStates =
    GDK_WINDOW_STATE_TOP_TILED | GDK_WINDOW_STATE_RIGHT_TILED |
    GDK_WINDOW_STATE_BOTTOM_TILED | GDK_WINDOW_STATE_LEFT_TILED;

constexpr gint kTiledStates = GDK_WINDOW_STATE_TILED | kPerSideTiledStates;

constexpr gint kResizableStates =
    GDK_WINDOW_STATE_TOP_RESIZABLE | GDK_WINDOW_STATE_RIGHT_RESIZABLE |
    GDK_WINDOW_STATE_BOTTOM_RESIZABLE | GDK_WINDOW_STATE_LEFT_RESIZABLE;

#if !GTK_CHECK_VERSION(3, 18, 0)
struct _GdkEventTouchpadPinch {
  GdkEventType type;
  GdkWindow* window;
  gint8 send_event;
  gint8 phase;
  gint8 n_fingers;
  guint32 time;
  gdouble x;
  gdouble y;
  gdouble dx;
  gdouble dy;
  gdouble angle_delta;
  gdouble scale;
  gdouble x_root, y_root;
  guint state;
};

constexpr gint GDK_TOUCHPAD_GESTURE_MASK = 1 << 24;
constexpr GdkEventType GDK_TOUCHPAD_PINCH = static_cast<GdkEventType>(42);

#endif

constexpr gint kEvents =
    GDK_TOUCHPAD_GESTURE_MASK | GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK |
    GDK_VISIBILITY_NOTIFY_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
    GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SMOOTH_SCROLL_MASK |
    GDK_TOUCH_MASK | GDK_SCROLL_MASK | GDK_POINTER_MOTION_MASK |
    GDK_PROPERTY_CHANGE_MASK;

/* utility functions */
static bool is_mouse_in_window(GdkWindow* aWindow, gdouble aMouseX,
                               gdouble aMouseY);
static bool is_drag_threshold_exceeded(GdkEvent* aEvent);
static GtkWidget* get_gtk_widget_for_gdk_window(GdkWindow* window);
static GdkCursor* get_gtk_cursor(nsCursor aCursor);

/* callbacks from widgets */
static gboolean expose_event_cb(GtkWidget* widget, cairo_t* cr);
static gboolean shell_configure_event_cb(GtkWidget* widget,
                                         GdkEventConfigure* event);
static void size_allocate_cb(GtkWidget* widget, GtkAllocation* allocation);
static void toplevel_window_size_allocate_cb(GtkWidget* widget,
                                             GtkAllocation* allocation);
static gboolean delete_event_cb(GtkWidget* widget, GdkEventAny* event);
static gboolean enter_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event);
static gboolean leave_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event);
static gboolean motion_notify_event_cb(GtkWidget* widget,
                                       GdkEventMotion* event);
static gboolean button_press_event_cb(GtkWidget* widget, GdkEventButton* event);
MOZ_CAN_RUN_SCRIPT static gboolean button_release_event_cb(
    GtkWidget* widget, GdkEventButton* event);
static gboolean focus_in_event_cb(GtkWidget* widget, GdkEventFocus* event);
static gboolean focus_out_event_cb(GtkWidget* widget, GdkEventFocus* event);
static gboolean key_press_event_cb(GtkWidget* widget, GdkEventKey* event);
static gboolean key_release_event_cb(GtkWidget* widget, GdkEventKey* event);
static gboolean property_notify_event_cb(GtkWidget* widget,
                                         GdkEventProperty* event);
static gboolean scroll_event_cb(GtkWidget* widget, GdkEventScroll* event);
static gboolean visibility_notify_event_cb(GtkWidget* widget,
                                           GdkEventVisibility* event);
static void hierarchy_changed_cb(GtkWidget* widget,
                                 GtkWidget* previous_toplevel);
static gboolean window_state_event_cb(GtkWidget* widget,
                                      GdkEventWindowState* event);
static void settings_xft_dpi_changed_cb(GtkSettings* settings,
                                        GParamSpec* pspec, nsWindow* data);
static void check_resize_cb(GtkContainer* container, gpointer user_data);
static void screen_composited_changed_cb(GdkScreen* screen, gpointer user_data);
static void widget_composited_changed_cb(GtkWidget* widget, gpointer user_data);

static void scale_changed_cb(GtkWidget* widget, GParamSpec* aPSpec,
                             gpointer aPointer);
static gboolean touch_event_cb(GtkWidget* aWidget, GdkEventTouch* aEvent);
static gboolean generic_event_cb(GtkWidget* widget, GdkEvent* aEvent);
static void widget_destroy_cb(GtkWidget* widget, gpointer user_data);

/* shell D&D events */
static gboolean drag_motion_event_cb(GtkWidget* aWidget,
                                     GdkDragContext* aDragContext, gint aX,
                                     gint aY, guint aTime, gpointer aData);
static void drag_leave_event_cb(GtkWidget* aWidget,
                                GdkDragContext* aDragContext, guint aTime,
                                gpointer aData);
static gboolean drag_drop_event_cb(GtkWidget* aWidget,
                                   GdkDragContext* aDragContext, gint aX,
                                   gint aY, guint aTime, gpointer aData);
static void drag_data_received_event_cb(
    GtkWidget* aWidget, GdkDragContext* aDragContext, gint aX, gint aY,
    GtkSelectionData* aSelectionData, guint aInfo, guint aTime, gpointer aData);

/* initialization static functions */
static nsresult initialize_prefs(void);

static guint32 sLastUserInputTime = GDK_CURRENT_TIME;

static SystemTimeConverter<guint32>& TimeConverter() {
  static SystemTimeConverter<guint32> sTimeConverterSingleton;
  return sTimeConverterSingleton;
}

bool nsWindow::sTransparentMainWindow = false;

// forward declare from mozgtk
extern "C" MOZ_EXPORT void mozgtk_linker_holder();

enum class GtkCsd {
  Unset,
  Zero,
  One,
  Other,
};

static GtkCsd GetGtkCSDEnv() {
  static GtkCsd sResult = [] {
    if (const char* csdOverride = getenv("GTK_CSD")) {
      if (*csdOverride == '0') {
        return GtkCsd::Zero;
      }
      if (*csdOverride == '1') {
        return GtkCsd::One;
      }
      return GtkCsd::Other;
    }
    return GtkCsd::Unset;
  }();
  return sResult;
}

namespace mozilla {

#ifdef MOZ_X11
class CurrentX11TimeGetter {
 public:
  explicit CurrentX11TimeGetter(GdkWindow* aWindow) : mWindow(aWindow) {}

  guint32 GetCurrentTime() const { return gdk_x11_get_server_time(mWindow); }

  void GetTimeAsyncForPossibleBackwardsSkew(const TimeStamp& aNow) {
    // Check for in-flight request
    if (!mAsyncUpdateStart.IsNull()) {
      return;
    }
    mAsyncUpdateStart = aNow;

    Display* xDisplay = GDK_WINDOW_XDISPLAY(mWindow);
    Window xWindow = GDK_WINDOW_XID(mWindow);
    unsigned char c = 'a';
    Atom timeStampPropAtom = TimeStampPropAtom();
    XChangeProperty(xDisplay, xWindow, timeStampPropAtom, timeStampPropAtom, 8,
                    PropModeReplace, &c, 1);
    XFlush(xDisplay);
  }

  gboolean PropertyNotifyHandler(GtkWidget* aWidget, GdkEventProperty* aEvent) {
    if (aEvent->atom != gdk_x11_xatom_to_atom(TimeStampPropAtom())) {
      return FALSE;
    }

    guint32 eventTime = aEvent->time;
    TimeStamp lowerBound = mAsyncUpdateStart;

    TimeConverter().CompensateForBackwardsSkew(eventTime, lowerBound);
    mAsyncUpdateStart = TimeStamp();
    return TRUE;
  }

 private:
  static Atom TimeStampPropAtom() {
    return gdk_x11_get_xatom_by_name_for_display(gdk_display_get_default(),
                                                 "GDK_TIMESTAMP_PROP");
  }

  // This is safe because this class is stored as a member of mWindow and
  // won't outlive it.
  GdkWindow* mWindow;
  TimeStamp mAsyncUpdateStart;
};
#endif

}  // namespace mozilla

// The window from which the focus manager asks us to dispatch key events.
static nsWindow* gFocusWindow = nullptr;
// If we're requested to focus window during session restore, delay
// the request until the session is restored.
static RefPtr<nsWindow> gFocusRequestWindow;
static nsIWidget::Raise gFocusRequestWindowRaise = nsIWidget::Raise::No;
static bool gBlockActivateEvent = false;
static bool gGlobalsInitialized = false;
static bool gUseAspectRatio = true;
bool gUseStableRounding = true;
static uint32_t gLastTouchID = 0;
// See Bug 1777269 for details. We don't know if the suspected leave notify
// event is a correct one when we get it.
// Store it and issue it later from enter notify event if it's correct,
// throw it away otherwise.
constinit static GUniquePtr<GdkEventCrossing> sStoredLeaveNotifyEvent;

// GDK's MAX_WL_BUFFER_SIZE is 4083 (4096 minus header, string
// argument length and NUL byte). Here truncates the string length
// further to prevent Wayland protocol message size limit exceeded
// errors.  See bug 2001083.
#define NS_WINDOW_TITLE_MAX_LENGTH 2048

// cursor cache
static GdkCursor* gCursorCache[eCursorCount];

// Sometimes this actually also includes the state of the modifier keys, but
// only the button state bits are used.
static guint gButtonState;

static inline bool TimestampIsNewerThan(guint32 a, guint32 b) {
  // Timestamps are just the least significant bits of a monotonically
  // increasing function, and so the use of unsigned overflow arithmetic.
  return a - b <= G_MAXUINT32 / 2;
}

static void UpdateLastInputEventTime(void* aGdkEvent) {
  nsCOMPtr<nsIUserIdleServiceInternal> idleService =
      do_GetService("@mozilla.org/widget/useridleservice;1");
  if (idleService) {
    idleService->ResetIdleTimeOut(0);
  }

  guint timestamp = gdk_event_get_time(static_cast<GdkEvent*>(aGdkEvent));
  if (timestamp == GDK_CURRENT_TIME) {
    return;
  }

  sLastUserInputTime = timestamp;
}

// Don't set parent (transient for) if nothing changes.
// gtk_window_set_transient_for() blows up wl_subsurfaces used by aWindow
// even if aParent is the same.
void GtkWindowSetTransientFor(GtkWindow* aWindow, GtkWindow* aParent) {
  GtkWindow* parent = gtk_window_get_transient_for(aWindow);
  if (parent != aParent) {
    gtk_window_set_transient_for(aWindow, aParent);
  }
}

#define gtk_window_set_transient_for(a, b)                         \
  {                                                                \
    MOZ_ASSERT_UNREACHABLE(                                        \
        "gtk_window_set_transient_for() can't be used directly."); \
  }

nsWindow::nsWindow()
    : mIsMapped(false),
      mIsDestroyed(false),
      mIsShown(false),
      mNeedsShow(false),
      mEnabled(true),
      mCreated(false),
      mHandleTouchEvent(false),
      mIsDragPopup(false),
      mCompositedScreen(gdk_screen_is_composited(gdk_screen_get_default())),
      mIsAccelerated(false),
      mIsAlert(false),
      mWindowShouldStartDragging(false),
      mHasMappedToplevel(false),
      mPanInProgress(false),
      mPendingBoundsChange(false),
      mTitlebarBackdropState(false),
      mAlwaysOnTop(false),
      mIsTransparent(false),
      mHasReceivedSizeAllocate(false),
      mWidgetCursorLocked(false),
      mUndecorated(false),
      mHasAlphaVisual(false),
      mConfiguredClearColor(false),
      mGotNonBlankPaint(false),
      mNeedsToRetryCapturingMouse(false),
      mX11HiddenPopupPositioned(false),
      mPopupTemporaryHidden(false),
      mWaitingToSessionRestore(false) {
  SetSafeWindowSize(mSizeConstraints.mMaxSize);

  if (!gGlobalsInitialized) {
    gGlobalsInitialized = true;

    // It's OK if either of these fail, but it may not be one day.
    initialize_prefs();

#ifdef MOZ_WAYLAND
    // Wayland provides clipboard data to application on focus-in event
    // so we need to init our clipboard hooks before we create window
    // and get focus.
    if (GdkIsWaylandDisplay()) {
      nsCOMPtr<nsIClipboard> clipboard =
          do_GetService("@mozilla.org/widget/clipboard;1");
      NS_ASSERTION(clipboard, "Failed to init clipboard!");
    }
#endif
  }
  // Dummy call to mozgtk to prevent the linker from removing
  // the dependency with --as-needed.
  // see toolkit/library/moz.build for details.
  mozgtk_linker_holder();
}

nsWindow::~nsWindow() {
  LOG("nsWindow::~nsWindow()");
  // We don't want to release live nsWindow.
  MOZ_RELEASE_ASSERT(mIsDestroyed, "Releasing live window!");
}

/* static */
void nsWindow::ReleaseGlobals() {
  for (auto& cursor : gCursorCache) {
    if (cursor) {
      g_object_unref(cursor);
      cursor = nullptr;
    }
  }
}

void nsWindow::DispatchActivateEvent(void) {
#ifdef ACCESSIBILITY
  DispatchActivateEventAccessible();
#endif  // ACCESSIBILITY

  if (mWidgetListener) {
    mWidgetListener->WindowActivated();
  }
}

void nsWindow::DispatchDeactivateEvent() {
  if (mWidgetListener) {
    mWidgetListener->WindowDeactivated();
  }

#ifdef ACCESSIBILITY
  DispatchDeactivateEventAccessible();
#endif  // ACCESSIBILITY
}

void nsWindow::DispatchResized() {
  if (mIsDestroyed) {
    return;
  }

  // Wayland popups are painted at 0,0 but we use mClientArea.x/y as popup
  // position so we can't use it for rounding of size coordinates.
  auto clientSize = gUseStableRounding && !IsWaylandPopup()
                        ? GetClientSize()
                        : LayoutDeviceIntSize::Round(mClientArea.Size() *
                                                     GetDesktopToDeviceScale());

  LOG("nsWindow::DispatchResized() client scaled size [%d, %d]",
      (int)clientSize.width, (int)clientSize.height);

  // Check out painting texture size
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(clientSize.width, clientSize.height)) {
    gfxCriticalNoteOnce << "Invalid mClientArea in MaybeDispatchResized "
                        << clientSize << " size state " << mSizeMode;
  }

#ifdef MOZ_WAYLAND
  if (mSurface) {
    LOG("  WaylandSurface unscaled size [%d, %d]", mClientArea.width,
        mClientArea.height);
    mSurface->SetSize(mClientArea.Size());
  }
#endif

  // Notify the GtkCompositorWidget of a ClientSizeChange
  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyClientSizeChanged(clientSize);
  }

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, clientSize);
  }
  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, clientSize);
  }
}

void nsWindow::OnDestroy() {
  if (mOnDestroyCalled) {
    return;
  }

  mOnDestroyCalled = true;

  // Prevent deletion.
  nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

  // release references to children, device context, toolkit + app shell
  nsIWidget::OnDestroy();

  // Remove association between this object and its parent and siblings.
  nsIWidget::Destroy();

  NotifyWindowDestroyed();
}

bool nsWindow::AreBoundsSane() {
  // Check requested size, as mClientArea might not have been updated.
  return !mLastSizeRequest.IsEmpty();
}

void nsWindow::Destroy() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  if (mIsDestroyed || !mCreated) {
    return;
  }

  LOG("nsWindow::Destroy\n");

  mIsDestroyed = true;
  mCreated = false;

  DestroyNative();

  // Cancel (dragleave) the current drag session, if any.
  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  if (dragService) {
    nsDragSession* dragSession =
        static_cast<nsDragSession*>(dragService->GetCurrentSession(this));
    if (dragSession && this == dragSession->GetMostRecentDestWindow()) {
      dragSession->ScheduleLeaveEvent();
    }
  }

  nsIRollupListener* rollupListener = nsIWidget::GetActiveRollupListener();
  if (rollupListener) {
    nsCOMPtr<nsIWidget> rollupWidget = rollupListener->GetRollupWidget();
    if (static_cast<nsIWidget*>(this) == rollupWidget) {
      rollupListener->Rollup({});
    }
  }

  NativeShow(false);

  MOZ_ASSERT(!gtk_widget_get_mapped(mShell));
  MOZ_ASSERT(!gtk_widget_get_mapped(GTK_WIDGET(mContainer)));

  DestroyLayerManager();

  // mSurfaceProvider holds reference to this nsWindow so we need to explicitly
  // clear it here to avoid nsWindow leak.
  mSurfaceProvider.CleanupResources();

  g_signal_handlers_disconnect_by_data(gtk_settings_get_default(), this);

  if (mIMContext) {
    mIMContext->OnDestroyWindow(this);
  }

  // make sure that we remove ourself as the focus window
  if (gFocusWindow == this) {
    LOG("automatically losing focus...\n");
    gFocusWindow = nullptr;
  }

  if (sStoredLeaveNotifyEvent) {
    nsWindow* window = nsWindow::FromGdkWindow(sStoredLeaveNotifyEvent->window);
    if (window == this) {
      sStoredLeaveNotifyEvent = nullptr;
    }
  }

  // We need to detach accessible object here because mContainer is a custom
  // widget and doesn't call gtk_widget_real_destroy() from destroy handler
  // as regular widgets.
  if (AtkObject* ac = gtk_widget_get_accessible(GTK_WIDGET(mContainer))) {
    gtk_accessible_set_widget(GTK_ACCESSIBLE(ac), nullptr);
  }

  // Owned by WaylandSurface or it's X11 ID,
  // just drop the reference here.
  mEGLWindow = nullptr;

  // Emoji picker will be deleted with mContainer
  mEmojiHidenSignal = 0;

  gtk_widget_destroy(mShell);
  mShell = nullptr;
  mContainer = nullptr;
#ifdef MOZ_WAYLAND
  mSurface = nullptr;
#endif

#ifdef ACCESSIBILITY
  if (mRootAccessible) {
    mRootAccessible = nullptr;
  }
#endif

  // Save until last because OnDestroy() may cause us to be deleted.
  OnDestroy();
}

float nsWindow::GetDPI() {
  float dpi = 96.0f;
  nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
  if (screen) {
    screen->GetDpi(&dpi);
  }
  return dpi;
}

double nsWindow::GetDefaultScaleInternal() { return FractionalScaleFactor(); }

DesktopToLayoutDeviceScale nsWindow::GetDesktopToDeviceScale() const {
  return DesktopToLayoutDeviceScale(FractionalScaleFactor());
}

bool nsWindow::WidgetTypeSupportsAcceleration() {
  if (IsSmallPopup() || mIsDragPopup) {
    return false;
  }
  if (mWindowType == WindowType::Popup) {
    return HasRemoteContent();
  }
  return true;
}

bool nsWindow::WidgetTypeSupportsNativeCompositing() {
  if (IsDragPopup()) {
    return false;
  }
#if defined(NIGHTLY_BUILD)
  // For testing purpose use layered native composition for popups
  // on nightly. It uses rapid map/unmap sequences it may reveal
  // hidden bugs in layered code.
  return true;
#else
  return WidgetTypeSupportsAcceleration();
#endif
}

static bool IsPenEvent(GdkEvent* aEvent, bool* isEraser) {
  GdkDevice* device = gdk_event_get_source_device(aEvent);
  GdkInputSource eSource = gdk_device_get_source(device);

  *isEraser = false;
  if (eSource == GDK_SOURCE_PEN) {
    return true;
  } else if (eSource == GDK_SOURCE_ERASER) {
    *isEraser = true;
    return true;
  } else {
#ifdef MOZ_X11
    // Workaround : When using Xwayland, pens are reported as
    // GDK_SOURCE_TOUCHSCREEN If eSource is GDK_SOURCE_TOUCHSCREEN and the
    // GDK_AXIS_XTILT and GDK_AXIS_YTILT axes are reported then it's a pen and
    // not a finger on a screen. Yes, that's a stupid heuristic but it works...
    // Note, however, that the tilt values are not reliable
    // Another approach could be use the device tool type, but that's only
    // available in GTK > 3.22
    gdouble value;
    if (eSource == GDK_SOURCE_TOUCHSCREEN && GdkIsX11Display() &&
        gdk_event_get_axis(aEvent, GDK_AXIS_XTILT, &value) &&
        gdk_event_get_axis(aEvent, GDK_AXIS_YTILT, &value)) {
      LOGW("InitPenEvent(): Is XWayland pen");
      return true;
    }
#endif

    return false;
  }
}

static void FetchAndAdjustPenData(WidgetMouseEvent& aGeckoEvent,
                                  GdkEvent* aEvent) {
  gdouble value;

  // The range of xtilt and ytilt are -1 to 1. Normalize it to -90 to 90.
  if (gdk_event_get_axis(aEvent, GDK_AXIS_XTILT, &value)) {
    int32_t tiltX = int32_t(NS_round(value * 90));
    if (gdk_event_get_axis(aEvent, GDK_AXIS_YTILT, &value)) {
      int32_t tiltY = int32_t(NS_round(value * 90));
      aGeckoEvent.mTilt.emplace(tiltX, tiltY);
    }
  }
  if (gdk_event_get_axis(aEvent, GDK_AXIS_PRESSURE, &value)) {
    aGeckoEvent.mPressure = (float)value;
    // Make sure the pressure is acceptable
    MOZ_ASSERT(aGeckoEvent.mPressure >= 0.0 && aGeckoEvent.mPressure <= 1.0);
  }

  LOGW("FetchAndAdjustPenData(): pressure %.2f\n", aGeckoEvent.mPressure);

  aGeckoEvent.mInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_PEN;
  aGeckoEvent.pointerId = 1;
}

void nsWindow::SetModal(bool aModal) {
  LOG("nsWindow::SetModal %d\n", aModal);
  if (mIsDestroyed) {
    return;
  }

  gtk_window_set_modal(GTK_WINDOW(mShell), aModal ? TRUE : FALSE);
}

// nsIWidget method, which means IsShown.
bool nsWindow::IsVisible() const { return mIsShown; }

void nsWindow::RegisterTouchWindow() {
  mHandleTouchEvent = true;
  mTouches.Clear();
}

LayoutDeviceIntPoint nsWindow::GetScreenEdgeSlop() {
  if (DrawsToCSDTitlebar()) {
    return {std::max(mClientMargin.left, mClientMargin.right),
            std::max(mClientMargin.top, mClientMargin.bottom)};
  }
  return {};
}

void nsWindow::ConstrainPosition(DesktopIntPoint& aPoint) {
  if (!mShell || GdkIsWaylandDisplay()) {
    return;
  }

  double dpiScale = GetDefaultScale().scale;

  // we need to use the window size in logical screen pixels
  auto bounds = GetScreenBounds();
  int32_t logWidth = std::max(NSToIntRound(bounds.width / dpiScale), 1);
  int32_t logHeight = std::max(NSToIntRound(bounds.height / dpiScale), 1);

  /* get our playing field. use the current screen, or failing that
    for any reason, use device caps for the default screen. */
  nsCOMPtr<nsIScreenManager> screenmgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1");
  if (!screenmgr) {
    return;
  }
  nsCOMPtr<nsIScreen> screen;
  screenmgr->ScreenForRect(aPoint.x, aPoint.y, logWidth, logHeight,
                           getter_AddRefs(screen));
  // We don't have any screen so leave the coordinates as is
  if (!screen) {
    return;
  }

  // For normalized windows, use the desktop work area.
  // For full screen windows, use the desktop.
  DesktopIntRect screenRect = mSizeMode == nsSizeMode_Fullscreen
                                  ? screen->GetRectDisplayPix()
                                  : screen->GetAvailRectDisplayPix();

  // Expand for the decoration size if needed.
  auto slop =
      DesktopIntPoint::Round(GetScreenEdgeSlop() / GetDesktopToDeviceScale());
  screenRect.Inflate(slop.x, slop.y);

  aPoint = ConstrainPositionToBounds(aPoint, {logWidth, logHeight}, screenRect);
}

bool nsWindow::ConstrainSizeWithScale(int* aWidth, int* aHeight,
                                      double aScale) {
  // Ensure minimal 1x1 window size
  if (*aWidth <= mClientMargin.LeftRight()) {
    *aWidth = mClientMargin.LeftRight() + 1;
  }
  if (*aHeight <= mClientMargin.TopBottom()) {
    *aHeight = mClientMargin.TopBottom() + 1;
  }

  // We store our constraints in inner sizes for convenience, but that means we
  // need to also constrain inner sizes as inner, rather than outer.
  int scaledWidth = (*aWidth - mClientMargin.LeftRight()) * aScale;
  int scaledHeight = (*aHeight - mClientMargin.TopBottom()) * aScale;
  int tmpWidth = scaledWidth, tmpHeight = scaledHeight;
  nsIWidget::ConstrainSize(&tmpWidth, &tmpHeight);
  if (tmpWidth != scaledWidth || tmpHeight != scaledHeight) {
    *aWidth = int(round(tmpWidth / aScale)) + mClientMargin.LeftRight();
    *aHeight = int(round(tmpHeight / aScale)) + mClientMargin.TopBottom();
    return true;
  }
  return false;
}

// Constraints are in desktop pixels. GDK coords are also desktop pixels on
// GTK so no scale conversion is needed when forwarding them to GTK.
void nsWindow::SetSizeConstraints(const SizeConstraints& aConstraints) {
  mSizeConstraints = aConstraints;
  SetSafeWindowSize(mSizeConstraints.mMinSize);
  SetSafeWindowSize(mSizeConstraints.mMaxSize);

  // Store constraints as inner sizes rather than outer sizes.
  if (SizeMode() == nsSizeMode_Normal) {
    const auto& margin = mClientMargin;
    if (mSizeConstraints.mMinSize.height) {
      mSizeConstraints.mMinSize.height -= margin.TopBottom();
    }
    if (mSizeConstraints.mMinSize.width) {
      mSizeConstraints.mMinSize.width -= margin.LeftRight();
    }
    if (mSizeConstraints.mMaxSize.height != NS_MAXSIZE) {
      mSizeConstraints.mMaxSize.height -= margin.TopBottom();
    }
    if (mSizeConstraints.mMaxSize.width != NS_MAXSIZE) {
      mSizeConstraints.mMaxSize.width -= margin.LeftRight();
    }
  }

  ApplySizeConstraints();
}

// See gtk_window_should_use_csd.
bool nsWindow::ToplevelUsesCSD() const {
  if (!IsTopLevelWidget() || mUndecorated ||
      mSizeMode == nsSizeMode_Fullscreen) {
    return false;
  }

  if (DrawsToCSDTitlebar()) {
    return true;
  }

#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    static auto sGdkWaylandDisplayPrefersSsd =
        (gboolean (*)(const GdkWaylandDisplay*))dlsym(
            RTLD_DEFAULT, "gdk_wayland_display_prefers_ssd");
    // NOTE(emilio): Not using GDK_WAYLAND_DISPLAY to avoid bug 1946088.
    return !sGdkWaylandDisplayPrefersSsd ||
           !sGdkWaylandDisplayPrefersSsd(
               static_cast<GdkWaylandDisplay*>(gdk_display_get_default()));
  }
#endif

  return GetGtkCSDEnv() == GtkCsd::One;
}

bool nsWindow::DrawsToCSDTitlebar() const {
  return mGtkWindowDecoration == GTK_DECORATION_CLIENT && mDrawInTitlebar;
}

void nsWindow::ApplySizeConstraints() {
  if (!mShell) {
    return;
  }

  uint32_t hints = 0;
  auto constraints = mSizeConstraints;
  if (constraints.mMinSize != DesktopIntSize()) {
    gtk_widget_set_size_request(GTK_WIDGET(mContainer),
                                constraints.mMinSize.width,
                                constraints.mMinSize.height);
    if (ToplevelUsesCSD()) {
      const auto& margin = mClientMargin;
      constraints.mMinSize.height += margin.TopBottom();
      constraints.mMinSize.width += margin.LeftRight();
    }
    hints |= GDK_HINT_MIN_SIZE;
  }
  if (mSizeConstraints.mMaxSize != DesktopIntSize(NS_MAXSIZE, NS_MAXSIZE)) {
    if (ToplevelUsesCSD()) {
      const auto& margin = mClientMargin;
      constraints.mMaxSize.height += margin.TopBottom();
      constraints.mMaxSize.width += margin.LeftRight();
    }
    hints |= GDK_HINT_MAX_SIZE;
  }

  // Constraints for the shell are outer sizes, but with SSD we need to use
  // inner sizes.
  GdkGeometry geometry{
      .min_width = constraints.mMinSize.width,
      .min_height = constraints.mMinSize.height,
      .max_width = constraints.mMaxSize.width,
      .max_height = constraints.mMaxSize.height,
  };

  if (mAspectRatio != 0.0f && !mAspectResizer) {
    geometry.min_aspect = geometry.max_aspect = mAspectRatio;
    hints |= GDK_HINT_ASPECT;
  }

  gtk_window_set_geometry_hints(GTK_WINDOW(mShell), nullptr, &geometry,
                                GdkWindowHints(hints));
}

void nsWindow::Show(bool aState) {
  if (aState == mIsShown) {
    return;
  }

  mIsShown = aState;

#ifdef MOZ_LOGGING
  LOG("nsWindow::Show state %d frame %s\n", aState, GetFrameTag().get());
  if (!aState && mSourceDragContext && GdkIsWaylandDisplay()) {
    LOG("  closing Drag&Drop source window, D&D will be canceled!");
  }
#endif

  // Ok, someone called show on a window that isn't sized to a sane
  // value.  Mark this window as needing to have Show() called on it
  // and return.
  if ((aState && !AreBoundsSane()) || !mCreated) {
    LOG("\tbounds are insane or window hasn't been created yet\n");
    mNeedsShow = true;
    return;
  }

  // If someone is hiding this widget, clear any needing show flag.
  if (!aState) mNeedsShow = false;

#ifdef ACCESSIBILITY
  if (aState && a11y::ShouldA11yBeEnabled()) {
    CreateRootAccessible();
  }
#endif

  NativeShow(aState);
  RefreshWindowClass();
}

LayoutDeviceIntPoint nsWindow::ToLayoutDevicePixels(
    const DesktopIntPoint& aPoint) {
  return LayoutDeviceIntPoint::Round(aPoint * GetDesktopToDeviceScale());
}

LayoutDeviceIntSize nsWindow::ToLayoutDevicePixels(
    const DesktopIntSize& aSize) {
  return LayoutDeviceIntSize::Round(aSize * GetDesktopToDeviceScale());
}

LayoutDeviceIntRect nsWindow::ToLayoutDevicePixels(
    const DesktopIntRect& aRect) {
  return LayoutDeviceIntRect::Round(aRect * GetDesktopToDeviceScale());
}

LayoutDeviceIntMargin nsWindow::ToLayoutDevicePixels(
    const DesktopIntMargin& aMargin) {
  return (aMargin * GetDesktopToDeviceScale()).Rounded();
}

DesktopIntPoint nsWindow::ToDesktopPixels(const LayoutDeviceIntPoint& aPoint) {
  return DesktopIntPoint::Round(aPoint / GetDesktopToDeviceScale());
}

DesktopIntSize nsWindow::ToDesktopPixels(const LayoutDeviceIntSize& aSize) {
  return DesktopIntSize::Round(aSize / GetDesktopToDeviceScale());
}

DesktopIntRect nsWindow::ToDesktopPixels(const LayoutDeviceIntRect& aRect) {
  return DesktopIntRect::Round(aRect / GetDesktopToDeviceScale());
}

void nsWindow::ResizeInt(const Maybe<DesktopIntPoint>& aMove,
                         DesktopIntSize aSize) {
  LOG("nsWindow::ResizeInt w:%d h:%d\n", aSize.width, aSize.height);
  auto currentBounds = GetScreenBoundsUnscaled();
  const bool moved = aMove && (*aMove != mLastMoveRequest ||
                               currentBounds.TopLeft() != *aMove);
  if (moved) {
    LOG("  with move to left:%d top:%d", aMove->x.value, aMove->y.value);
    mLastMoveRequest = *aMove;
  }

  const bool resized =
      aSize != mLastSizeRequest || currentBounds.Size() != aSize;
  LOG("  resized %d aSize [%d, %d] mLastSizeRequest [%d, %d] "
      "bounds [%d, %d]",
      resized, aSize.width, aSize.height, mLastSizeRequest.width,
      mLastSizeRequest.height, currentBounds.width, currentBounds.height);

  mLastSizeRequest = aSize;

  // Recalculate aspect ratio when resized from DOM
  if (mAspectRatio != 0.0) {
    LockAspectRatio(true);
  }

  if (!mCreated) {
    return;
  }

  if (!moved && !resized) {
    LOG("  not moved or resized, quit");
    return;
  }

  NativeMoveResize(moved, resized);
}

void nsWindow::Resize(const DesktopSize& aSize, bool aRepaint) {
  LOG("nsWindow::Resize %s (scaled %s)", ToString(aSize).c_str(),
      ToString(aSize).c_str());

  double scale = GetDesktopToDeviceScale().scale;
  auto size = DesktopIntSize::Round(aSize);
  auto scaledSize = ToLayoutDevicePixels(size);

  if (ConstrainSizeWithScale(&size.width, &size.height, scale)) {
    LOG("  ConstrainSizeWithScale: w:%d h:%d coord scale %f", size.width,
        size.height, scale);
  }
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(scaledSize.width, scaledSize.height)) {
    gfxCriticalNoteOnce << "Invalid aSize in ResizeInt " << scaledSize
                        << " size state " << mSizeMode;
  }

  ResizeInt(Nothing(), size);
}

void nsWindow::Resize(const DesktopRect& aRect, bool aRepaint) {
  double scale = GetDesktopToDeviceScale().scale;
  auto size = DesktopIntSize::Round(aRect.Size());
  auto topLeft = DesktopIntPoint::Round(aRect.TopLeft());
  auto scaledSize = ToLayoutDevicePixels(size);

  LOG("nsWindow::Resize [%.2f,%.2f] -> [%.2f x %.2f] scaled [%d,%d] -> "
      "[%d x %d] repaint %d",
      aRect.x, aRect.y, aRect.width, aRect.height, topLeft.x.value,
      topLeft.y.value, size.width, size.height, aRepaint);

  if (ConstrainSizeWithScale(&size.width, &size.height, scale)) {
    LOG("  ConstrainSizeWithScale: w:%d h:%d coord scale %f", size.width,
        size.height, scale);
  }
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(scaledSize.width, scaledSize.height)) {
    gfxCriticalNoteOnce << "Invalid aSize in ResizeInt " << scaledSize
                        << " size state " << mSizeMode;
  }

  ResizeInt(Some(topLeft), size);
}

void nsWindow::Enable(bool aState) { mEnabled = aState; }

bool nsWindow::IsEnabled() const { return mEnabled; }

void nsWindow::Move(const DesktopPoint& aTopLeft) {
  double scale = GetDesktopToDeviceScale().scale;
  auto request = DesktopIntPoint::Round(aTopLeft);

  LOG("nsWindow::Move to [%d x %d] scale %f scaled [%.2f x %.2f]",
      request.x.value, request.y.value, scale, request.x.value * scale,
      request.y.value * scale);

  if (mSizeMode != nsSizeMode_Normal && IsTopLevelWidget()) {
    LOG("  size state is not normal, can't move, bailing");
    return;
  }

  // Since a popup window's x/y coordinates are in relation to to
  // the parent, the parent might have moved so we always move a
  // popup window.
  auto pos = GetScreenBoundsUnscaled().TopLeft();
  LOG("  bounds %d x %d\n", int(pos.x), int(pos.y));
  if (pos == request && mLastMoveRequest == request &&
      mWindowType != WindowType::Popup) {
    LOG("  position is the same, return\n");
    return;
  }

  // XXX Should we do some AreBoundsSane check here?
  mLastMoveRequest = request;

  if (!mCreated) {
    LOG("  is not created, return.\n");
    return;
  }

  NativeMoveResize(/* move */ true, /* resize */ false);
}

bool nsWindow::IsPopup() const { return mWindowType == WindowType::Popup; }

bool nsWindow::IsWaylandPopup() const {
  return GdkIsWaylandDisplay() && IsPopup();
}

void nsWindow::SetSizeMode(nsSizeMode aMode) {
  LOG("nsWindow::SetSizeMode %d\n", aMode);

  // Return if there's no shell or our current state is the same as the mode we
  // were just set to.
  if (!mShell) {
    LOG("    no shell");
    return;
  }

  if (mSizeMode == aMode && mLastSizeModeRequest == aMode) {
    LOG("    already set");
    return;
  }

  // It is tempting to try to optimize calls below based only on current
  // mSizeMode, but that wouldn't work if there's a size-request in flight
  // (specially before show). See bug 1789823.
  const auto SizeModeMightBe = [&](nsSizeMode aModeToTest) {
    if (mSizeMode != mLastSizeModeRequest) {
      // Arbitrary size mode requests might be ongoing.
      return true;
    }
    return mSizeMode == aModeToTest;
  };

  if (aMode != nsSizeMode_Fullscreen && aMode != nsSizeMode_Minimized) {
    // Fullscreen and minimized are compatible.
    if (SizeModeMightBe(nsSizeMode_Fullscreen)) {
      MakeFullScreen(false);
    }
  }

  switch (aMode) {
    case nsSizeMode_Maximized:
      LOG("    set maximized");
      gtk_window_maximize(GTK_WINDOW(mShell));
      break;
    case nsSizeMode_Minimized:
      LOG("    set minimized");
      gtk_window_iconify(GTK_WINDOW(mShell));
      break;
    case nsSizeMode_Fullscreen:
      LOG("    set fullscreen");
      MakeFullScreen(true);
      break;
    default:
      MOZ_FALLTHROUGH_ASSERT("Unknown size mode");
    case nsSizeMode_Normal:
      LOG("    set normal");
      if (SizeModeMightBe(nsSizeMode_Maximized)) {
        gtk_window_unmaximize(GTK_WINDOW(mShell));
      }
      if (SizeModeMightBe(nsSizeMode_Minimized)) {
        gtk_window_deiconify(GTK_WINDOW(mShell));
        // We need this for actual deiconification on mutter.
        gtk_window_present(GTK_WINDOW(mShell));
      }
      break;
  }
  mLastSizeModeRequest = aMode;
}

bool nsWindow::WorkspaceManagementDisabled() {
  if (Preferences::GetBool("widget.disable-workspace-management", false)) {
    return true;
  }
  if (Preferences::HasUserValue("widget.workspace-management")) {
    return Preferences::GetBool("widget.workspace-management");
  }

  if (IsGnomeDesktopEnvironment()) {
    // Gnome uses dynamic workspaces by default so disable workspace management
    // in that case.
    return widget::GSettings::GetBoolean("org.gnome.mutter"_ns,
                                         "dynamic-workspaces"_ns)
        .valueOr(false);
  }

  const auto& desktop = GetDesktopEnvironmentIdentifier();
  return desktop.EqualsLiteral("bspwm") || desktop.EqualsLiteral("i3");
}

void nsWindow::GetWorkspaceID(nsAString& workspaceID) {
  workspaceID.Truncate();
}

void nsWindow::MoveToWorkspace(const nsAString& workspaceIDStr) {
  LOG("  MoveToWorkspace disabled, quit");
}

void nsWindow::SetUserTimeAndStartupTokenForActivatedWindow() {
  nsGTKToolkit* toolkit = nsGTKToolkit::GetToolkit();
  if (!toolkit) {
    return;
  }

  mWindowActivationTokenFromEnv = toolkit->GetActivationToken();
  if (!mWindowActivationTokenFromEnv.IsEmpty()) {
    if (!GdkIsWaylandDisplay()) {
      gtk_window_set_startup_id(GTK_WINDOW(mShell),
                                mWindowActivationTokenFromEnv.get());
      // In the case of X11, the above call is all we need. For wayland we need
      // to keep the token around until we take it in
      // TransferFocusToWaylandWindow.
      mWindowActivationTokenFromEnv.Truncate();
    }
  } else if (uint32_t timestamp = toolkit->GetFocusTimestamp()) {
    // We don't have the data we need. Fall back to an
    // approximation ... using the timestamp of the remote command
    // being received as a guess for the timestamp of the user event
    // that triggered it.
    gdk_window_focus(GetToplevelGdkWindow(), timestamp);
  }

  // If we used the startup ID, that already contains the focus timestamp;
  // we don't want to reuse the timestamp next time we raise the window
  toolkit->SetFocusTimestamp(0);
  toolkit->SetActivationToken(""_ns);
}

/* static */
guint32 nsWindow::GetLastUserInputTime() {
  // gdk_x11_display_get_user_time/gtk_get_current_event_time tracks
  // button and key presses, DESKTOP_STARTUP_ID used to start the app,
  // drop events from external drags,
  // WM_DELETE_WINDOW delete events, but not usually mouse motion nor
  // button and key releases.  Therefore use the most recent of
  // gdk_x11_display_get_user_time and the last time that we have seen.
#ifdef MOZ_X11
  GdkDisplay* gdkDisplay = gdk_display_get_default();
  guint32 timestamp = GdkIsX11Display(gdkDisplay)
                          ? gdk_x11_display_get_user_time(gdkDisplay)
                          : gtk_get_current_event_time();
#else
  guint32 timestamp = gtk_get_current_event_time();
#endif

  if (sLastUserInputTime != GDK_CURRENT_TIME &&
      TimestampIsNewerThan(sLastUserInputTime, timestamp)) {
    return sLastUserInputTime;
  }

  return timestamp;
}

// Request activation of this window or give focus to this widget.
// aRaise means whether we should request activation of this widget's
// toplevel window.
//
// nsWindow::SetFocus(Raise::Yes) - Raise and give focus to toplevel window.
// nsWindow::SetFocus(Raise::No) - Give focus to this window.
void nsWindow::SetFocus(Raise aRaise, mozilla::dom::CallerType aCallerType) {
  LOG("nsWindow::SetFocus Raise %d\n", aRaise == Raise::Yes);

  if (mWaitingToSessionRestore) {
    gFocusRequestWindow = this;
    gFocusRequestWindowRaise = aRaise;
    LOG("  waiting to session restore, quit.");
    return;
  }

  // Raise the window if someone passed in true and the prefs are
  // set properly.
  GtkWidget* toplevelWidget = gtk_widget_get_toplevel(GTK_WIDGET(mContainer));

  LOG("  gFocusWindow [%p]\n", gFocusWindow);
  LOG("  mContainer [%p]\n", GTK_WIDGET(mContainer));
  LOG("  Toplevel widget [%p]\n", toplevelWidget);

  // Make sure that our owning widget has focus.  If it doesn't try to
  // grab it.  Note that we don't set our focus flag in this case.
  if (StaticPrefs::mozilla_widget_raise_on_setfocus_AtStartup() &&
      aRaise == Raise::Yes && toplevelWidget &&
      !gtk_widget_has_focus(toplevelWidget)) {
    if (gtk_widget_get_visible(mShell)) {
      LOG("  toplevel is not focused");
      gdk_window_show_unraised(GetToplevelGdkWindow());
      // Unset the urgency hint if possible.
      SetUrgencyHint(mShell, false);
    }
  }

  RefPtr<nsWindow> toplevelWindow = nsWindow::FromGtkWidget(toplevelWidget);
  if (!toplevelWindow) {
    LOG("  missing toplevel nsWindow, quit\n");
    return;
  }

  if (aRaise == Raise::Yes) {
    // means request toplevel activation.

    // This is asynchronous. If and when the window manager accepts the request,
    // then the focus widget will get a focus-in-event signal.
    if (StaticPrefs::mozilla_widget_raise_on_setfocus_AtStartup() &&
        toplevelWindow->mIsShown && toplevelWindow->mShell &&
        !gtk_window_is_active(GTK_WINDOW(toplevelWindow->mShell))) {
      LOG("  toplevel is visible but not active, requesting activation [%p]",
          toplevelWindow.get());

      // Take the time here explicitly for the call below.
      const uint32_t timestamp = [&] {
        if (nsGTKToolkit* toolkit = nsGTKToolkit::GetToolkit()) {
          if (uint32_t t = toolkit->GetFocusTimestamp()) {
            toolkit->SetFocusTimestamp(0);
            return t;
          }
        }
#if defined(MOZ_X11)
        // If it's X11 and there's a startup token, use GDK_CURRENT_TIME, so
        // gtk_window_present_with_time will pull the timestamp from the startup
        // token.
        if (GdkIsX11Display()) {
          nsGTKToolkit* toolkit = nsGTKToolkit::GetToolkit();
          const auto& startupToken = toolkit->GetActivationToken();
          if (!startupToken.IsEmpty()) {
            return static_cast<uint32_t>(GDK_CURRENT_TIME);
          }
        }
#endif
        return GetLastUserInputTime();
      }();

      toplevelWindow->SetUserTimeAndStartupTokenForActivatedWindow();
      gtk_window_present_with_time(GTK_WINDOW(toplevelWindow->mShell),
                                   timestamp);

#ifdef MOZ_WAYLAND
      if (auto* toplevelWayland = toplevelWindow->AsWayland()) {
        auto existingToken =
            std::move(toplevelWayland->mWindowActivationTokenFromEnv);
        if (!existingToken.IsEmpty()) {
          LOG("  has existing activation token.");
          toplevelWayland->FocusWaylandWindow(existingToken.get());
        } else {
          LOG("  missing activation token, try to transfer from focused "
              "window");
          toplevelWayland->TransferFocusTo();
        }
      }
#endif
    }
    return;
  }

  // aRaise == No means that keyboard events should be dispatched from this
  // widget.

  // Ensure GTK_WIDGET(mContainer) is the focused GtkWidget within its toplevel
  // window.
  //
  // For WindowType::Popup, this GtkWidget may not actually be the one that
  // receives the key events as it may be the parent window that is active.
  if (!gtk_widget_is_focus(GTK_WIDGET(mContainer))) {
    // This is synchronous.  It takes focus from a plugin or from a widget
    // in an embedder.  The focus manager already knows that this window
    // is active so gBlockActivateEvent avoids another (unnecessary)
    // activate notification.
    gBlockActivateEvent = true;
    gtk_widget_grab_focus(GTK_WIDGET(mContainer));
    gBlockActivateEvent = false;
  }

  // If this is the widget that already has focus, return.
  if (gFocusWindow == this) {
    LOG("  already have focus");
    return;
  }

  // Set this window to be the focused child window
  gFocusWindow = this;

  if (mIMContext) {
    mIMContext->OnFocusWindow(this);
  }

  LOG("  widget now has focus in SetFocus()");
}

DesktopIntRect nsWindow::GetScreenBoundsUnscaled() {
  DesktopIntRect bounds = mClientArea;
  bounds.Inflate(mClientMargin);
  return bounds;
}

LayoutDeviceIntRect nsWindow::GetScreenBounds() {
  return ToLayoutDevicePixels(GetScreenBoundsUnscaled());
}

LayoutDeviceIntRect nsWindow::GetBounds() {
  return ToLayoutDevicePixels(GetScreenBoundsUnscaled());
}

LayoutDeviceIntSize nsWindow::GetClientSize() {
  return ToLayoutDevicePixels(mClientArea).Size();
}

LayoutDeviceIntRect nsWindow::GetClientBounds() {
  return ToLayoutDevicePixels(mClientArea);
}

LayoutDeviceIntPoint nsWindow::GetClientOffset() {
  auto scale = FractionalScaleFactor();
  return LayoutDeviceIntPoint(int(round(mClientMargin.left * scale)),
                              int(round(mClientMargin.top * scale)));
}

nsresult nsWindow::GetRestoredBounds(LayoutDeviceIntRect& aRect) {
  if (SizeMode() != nsSizeMode_Normal) {
    return NS_ERROR_FAILURE;
  }

  // For window restore, we need to use inner sizes. We don't have any
  // decorations yet when we get to restore it. This matches previous behavior,
  // though it's a bit unfortunate.
  aRect = GetScreenBounds();
  aRect.SizeTo(GetClientSize());
  LOG("nsWindow::GetRestoredBounds() %s", ToString(aRect).c_str());
  return NS_OK;
}

LayoutDeviceIntMargin nsWindow::NormalSizeModeClientToWindowMargin() {
  if (SizeMode() == nsSizeMode_Normal) {
    return ToLayoutDevicePixels(mClientMargin);
  }
  // TODO(emilio): When not using CSD decorations or not in the normal
  // sizemode, we can't know the size of the titlebar and window borders before
  // being shown. In order to return consistent results, we return a zero
  // margin here.
  return {};
}

#ifdef MOZ_X11
LayoutDeviceIntCoord GetXWindowBorder(GdkWindow* aWin) {
  Display* display = GDK_DISPLAY_XDISPLAY(gdk_window_get_display(aWin));
  auto xid = gdk_x11_window_get_xid(aWin);
  Window root;
  int wx, wy;
  unsigned ww, wh, wb = 0, wd;
  XGetGeometry(display, xid, &root, &wx, &wy, &ww, &wh, &wb, &wd);
  return wb;
}
#endif

/*
  Window bounds are computed differently on X11, X11 CSD and Wayland CSD.
  Let's say a non-maximized toplevel window is visible at 100,100:

  X11 (titlebar height is 40, decoration size 0):
    Frame bounds:         contains window position which includes window
                          decoration and system titlebar (100, 60)
    Toplevel bounds:      contains window position without tilebar (100,100)
    mGdkWindow position:  0,0 (located at toplevel origin)

  X11 CSD (without system titlebar, CSD size is 40,40):
    Frame bounds:         contains window position which includes CSD
                          decorations (60,60).
    Toplevel bounds:      same as frame bounds.
    mGdkWindow position:  Relative position to toplevel decoration left
                          top corner, so matches CSD size - (40,40).

  Wayland CSD (with system titlebar, CSD size is 40,40, titlebar height 50):
    Frame bounds:         zero as Wayland doesn't report window position (0,0)
    Toplevel bounds:      same as frame bounds.
    mGdkWindow position:  Relative position to toplevel decoration left
                          top corner, so matches CSD + titlebar size - (40, 90).

  Wayland CSD (without system titlebar, CSD size is 40,40):
    Frame bounds:         zero as Wayland doesn't report window position (0,0)
    Toplevel bounds:      same as frame bounds.
    mGdkWindow position:  Relative position to toplevel decoration left
                          top corner, so matches CSD size - (40,40).
*/
#ifdef MOZ_X11
auto nsWindow::Bounds::ComputeX11(const nsWindow* aWindow) -> Bounds {
  LOG_WIN(aWindow, "Bounds::ComputeX11()");

  auto* toplevel = aWindow->GetToplevelGdkWindow();

  // Window position and size with window decoration AND system titlebar.
  auto GetFrameTitlebarBounds = [&](GdkWindow* aWin) {
    GdkRectangle b{0};
    gdk_window_get_frame_extents(aWin, &b);
    if (gtk_check_version(3, 24, 35) &&
        gdk_window_get_window_type(aWin) == GDK_WINDOW_TEMP) {
      LOG_WIN(
          aWindow,
          "  GetFrameTitlebarBounds gtk 3.24.35 & GDK_WINDOW_TEMP workaround");
      // Workaround for https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/4820
      // Bug 1775017 Gtk < 3.24.35 returns scaled values for
      // override redirected window on X11.
      double scale = aWindow->FractionalScaleFactor();
      return DesktopIntRect(int(round(b.x / scale)), int(round(b.y / scale)),
                            int(round(b.width / scale)),
                            int(round(b.height / scale)));
    }
    auto result = DesktopIntRect(b.x, b.y, b.width, b.height);
    if (gtk_check_version(3, 24, 50)) {
      if (auto border = GetXWindowBorder(aWin)) {
        LOG_WIN(aWindow, "  GetFrameTitlebarBounds gtk 3.24.50 workaround");
        // Workaround for
        // https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/8423
        // Bug 1958174 Gtk doesn't account for window border sizes on X11.
        double scale = aWindow->FractionalScaleFactor();
        result.width += 2 * border / scale;
        result.height += 2 * border / scale;
      }
    }
    return result;
  };

  // Window position and size with decoration but WITHOUT system titlebar.
  auto GetBounds = [&](GdkWindow* aWin) {
    GdkRectangle b{0};
    if (aWindow->IsTopLevelWidget() && aWin == toplevel) {
      // We want the up-to-date size from the X server, not the last configure
      // event size, to avoid spurious resizes on e.g. sizemode changes.
      gdk_window_get_geometry(aWin, nullptr, nullptr, &b.width, &b.height);
      gdk_window_get_origin(aWin, &b.x, &b.y);
    } else {
      gdk_window_get_position(aWin, &b.x, &b.y);
      b.width = gdk_window_get_width(aWin);
      b.height = gdk_window_get_height(aWin);
    }
    return DesktopIntRect(b.x, b.y, b.width, b.height);
  };

  const auto toplevelBoundsWithTitlebar = GetFrameTitlebarBounds(toplevel);
  LOG_WIN(aWindow, "  toplevelBoundsWithTitlebar %s",
          ToString(toplevelBoundsWithTitlebar).c_str());

  if (aWindow->GetSizeMode() == nsSizeMode_Fullscreen) {
    // In order to avoid spurious extra resizes during fullscreen transitions,
    // we assume we're not decorated.
    return {.mClientArea = toplevelBoundsWithTitlebar, .mClientMargin = {}};
  }

  const auto toplevelBounds = GetBounds(toplevel);
  LOG_WIN(aWindow, "  toplevelBounds %s", ToString(toplevelBounds).c_str());

  // Offset from system decoration to gtk decoration.
  const auto systemDecorationOffset = [&] {
    auto offset =
        toplevelBounds.TopLeft() - toplevelBoundsWithTitlebar.TopLeft();
    // If the WM sends us broken values, try to clamp to something sensible.
    // The offset will never be more than the bounds difference, nor negative.
    // See bug 2001722 for an example where this happened.
    offset.x =
        std::max(std::min(int32_t(offset.x), toplevelBoundsWithTitlebar.width -
                                                 toplevelBounds.width),
                 0);
    offset.y =
        std::max(std::min(int32_t(offset.y), toplevelBoundsWithTitlebar.height -
                                                 toplevelBounds.height),
                 0);
    return offset;
  }();

  Bounds result;
  // This is relative to our parent window, that is, to topLevelBounds.
  result.mClientArea = GetBounds(aWindow->GetGdkWindow());
  // Make it relative to topLevelBoundsWithTitlebar
  result.mClientArea.MoveBy(systemDecorationOffset);
  LOG_WIN(aWindow, "  mClientArea %s", ToString(result.mClientArea).c_str());

  if (result.mClientArea.X() < 0 || result.mClientArea.Y() < 0 ||
      result.mClientArea.Width() <= 1 || result.mClientArea.Height() <= 1) {
    // If we don't have gdkwindow bounds, assume we take the whole toplevel
    // except system decorations.
    result.mClientArea =
        DesktopIntRect(systemDecorationOffset, toplevelBounds.Size());
  }

  result.mClientMargin =
      DesktopIntRect(DesktopIntPoint(), toplevelBoundsWithTitlebar.Size()) -
      result.mClientArea;
  result.mClientMargin.EnsureAtLeast(DesktopIntMargin());

  // We want mClientArea in global coordinates. We derive everything from here,
  // so move it to global coords.
  result.mClientArea.MoveBy(toplevelBoundsWithTitlebar.TopLeft());
  return result;
}
#endif
#ifdef MOZ_WAYLAND
auto nsWindow::Bounds::ComputeWayland(const nsWindow* aWindow) -> Bounds {
  LOG_WIN(aWindow, "Bounds::ComputeWayland()");
  auto GetBounds = [&](GdkWindow* aWin) {
    GdkRectangle b{0};
    gdk_window_get_position(aWin, &b.x, &b.y);
    b.width = gdk_window_get_width(aWin);
    b.height = gdk_window_get_height(aWin);
    return DesktopIntRect(b.x, b.y, b.width, b.height);
  };

  const auto toplevelBounds = GetBounds(aWindow->GetToplevelGdkWindow());
  LOG_WIN(aWindow, "  toplevelBounds %s", ToString(toplevelBounds).c_str());

  if (aWindow->GetSizeMode() == nsSizeMode_Fullscreen) {
    return {.mClientArea = toplevelBounds, .mClientMargin = {}};
  }

  Bounds result;
  result.mClientArea = GetBounds(aWindow->GetGdkWindow());
  result.mClientMargin =
      DesktopIntRect(DesktopIntPoint(), toplevelBounds.Size()) -
      result.mClientArea;
  result.mClientMargin.EnsureAtLeast(DesktopIntMargin());

  LOG_WIN(aWindow, "  bounds %s margin %s",
          ToString(result.mClientArea).c_str(),
          ToString(result.mClientMargin).c_str());

  if (result.mClientArea.X() < 0 || result.mClientArea.Y() < 0 ||
      result.mClientArea.Width() <= 1 || result.mClientArea.Height() <= 1) {
    // If we don't have gdkwindow bounds yet, assume we take the whole toplevel.
    result.mClientArea = toplevelBounds;
    result.mClientMargin = {};
  }
  return result;
}
#endif

auto nsWindow::Bounds::Compute(const nsWindow* aWindow) -> Bounds {
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    return ComputeX11(aWindow);
  }
#endif
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    return ComputeWayland(aWindow);
  }
#endif
  MOZ_ASSERT_UNREACHABLE("How?");
  return {};
}

void nsWindow::RecomputeBounds(bool aScaleChange) {
  LOG("RecomputeBounds() scale change %d", aScaleChange);
  mPendingBoundsChange = false;

  auto* toplevel = GetToplevelGdkWindow();
  // Don't recompute bounds while the toplevel is unmapped (e.g. while hiding
  // during shutdown). After we unmap the window, the window manager may take
  // the titlebar away. Recomputing then shrinks mClientMargin (the titlebar
  // height) to 0, so GetScreenBounds()/outerHeight report the size without the
  // titlebar. SessionStore saves that too-short value, and on the next startup
  // the window comes back one titlebar shorter -- shrinking a little more on
  // every session restore (bug 2034108). Keep the last mapped bounds instead;
  // they're refreshed on the next map.
  if (!toplevel || mIsDestroyed || !mIsMapped) {
    return;
  }

  const auto oldMargin = mClientMargin;
  const auto oldClientArea = mClientArea;
  const auto newBounds = Bounds::Compute(this);
  mClientArea = newBounds.mClientArea;
  mClientMargin = newBounds.mClientMargin;

  if (IsPopup()) {
    // Popup windows is not moved by the window manager, and so any change in
    // position is a result of our direction.
    //
    // mClientArea has already been set in Move() or Resize(), and that is more
    // up-to-date than the position in the ConfigureNotify event if the event
    // is from an earlier window move.
    //
    // NOTE(emilio): If we remove the early mClientArea change in Move() /
    // Resize(), we should be able to remove this special case (but some tests
    // would need to be adjusted to deal with the async popup moves).
    MOZ_ASSERT(mLastMoveRequest == oldClientArea.TopLeft());
    mClientArea.MoveTo(oldClientArea.TopLeft());
  }

  // Sometimes the window manager gives us garbage sizes (way past the maximum
  // texture size) causing crashes if we don't enforce sane sizes here.
  auto size = mClientArea.Size();
  if (SetSafeWindowSize(size)) {
    mClientArea.SizeTo(size);
  }

  LOG("client area old: %s new -> %s", ToString(oldClientArea).c_str(),
      ToString(mClientArea).c_str());
  LOG("margin old: %s new -> %s", ToString(oldMargin).c_str(),
      ToString(mClientMargin).c_str());

  const bool clientMarginsChanged = oldMargin != mClientMargin;
  if (clientMarginsChanged) {
    // Our last size request was with a different margin, keep it relative to
    // the new one.
    mLastSizeRequest.width += mClientMargin.LeftRight() - oldMargin.LeftRight();
    mLastSizeRequest.height +=
        mClientMargin.TopBottom() - oldMargin.TopBottom();
  }

  // We need to send WindowMoved even if only the client margins changed
  // so that BrowserParent picks up the new offsets.
  const bool moved = aScaleChange || clientMarginsChanged ||
                     oldClientArea.TopLeft() != mClientArea.TopLeft();
  const bool resized = aScaleChange || clientMarginsChanged ||
                       oldClientArea.Size() != mClientArea.Size();

#ifdef MOZ_X11
  if ((moved || resized) && AsX11()) {
    AsX11()->UpdateNativePointerBarriers();
  }
#endif

  if (moved) {
    NotifyWindowMoved(GetScreenBoundsUnscaled().TopLeft());
  }
  if (resized) {
    DispatchResized();
  }
}

gboolean nsWindow::OnPropertyNotifyEvent(GtkWidget* aWidget,
                                         GdkEventProperty* aEvent) {
  if (aEvent->atom == gdk_atom_intern("_NET_FRAME_EXTENTS", FALSE)) {
    LOG("OnPropertyNotifyEvent(_NET_FRAME_EXTENTS)");
    SchedulePendingBounds();
    return FALSE;
  }
  if (!mGdkWindow) {
    return FALSE;
  }
#ifdef MOZ_X11
  if (GetCurrentTimeGetter()->PropertyNotifyHandler(aWidget, aEvent)) {
    return TRUE;
  }
#endif
  return FALSE;
}

static GdkCursor* GetCursorForImage(const nsIWidget::Cursor& aCursor,
                                    int32_t aWidgetScaleFactor) {
  if (!aCursor.IsCustom()) {
    return nullptr;
  }
  nsIntSize size = nsIWidget::CustomCursorSize(aCursor);

  // NOTE: GTK only allows integer scale factors, so we ceil to the larger scale
  // factor and then tell gtk to scale it down. We ensure to scale at least to
  // the GDK scale factor, so that cursors aren't downsized in HiDPI on wayland,
  // see bug 1707533.
  int32_t gtkScale = std::max(
      aWidgetScaleFactor, int32_t(std::ceil(std::max(aCursor.mResolution.mX,
                                                     aCursor.mResolution.mY))));

  // Reject cursors greater than 128 pixels in some direction, to prevent
  // spoofing.
  // XXX ideally we should rescale. Also, we could modify the API to
  // allow trusted content to set larger cursors.
  //
  // TODO(emilio, bug 1445844): Unify the solution for this with other
  // platforms.
  if (size.width > 128 || size.height > 128) {
    return nullptr;
  }

  nsIntSize rasterSize = size * gtkScale;
  RefPtr<GdkPixbuf> pixbuf =
      nsImageToPixbuf::ImageToPixbuf(aCursor.mContainer, Some(rasterSize));
  if (!pixbuf) {
    return nullptr;
  }

  // Looks like all cursors need an alpha channel (tested on Gtk 2.4.4). This
  // is of course not documented anywhere...
  // So add one if there isn't one yet
  if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
    RefPtr<GdkPixbuf> alphaBuf =
        dont_AddRef(gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0));
    pixbuf = std::move(alphaBuf);
    if (!pixbuf) {
      return nullptr;
    }
  }

  cairo_surface_t* surface =
      gdk_cairo_surface_create_from_pixbuf(pixbuf, gtkScale, nullptr);
  if (!surface) {
    return nullptr;
  }

  auto CleanupSurface =
      MakeScopeExit([&]() { cairo_surface_destroy(surface); });

  return gdk_cursor_new_from_surface(gdk_display_get_default(), surface,
                                     aCursor.mHotspotX, aCursor.mHotspotY);
}

void nsWindow::SetCursor(const Cursor& aCursor) {
  if (mWidgetCursorLocked || !mGdkWindow) {
    return;
  }

  // Only change cursor if it's actually been changed
  if (!mUpdateCursor && mCursor == aCursor) {
    return;
  }

  mUpdateCursor = false;
  mCursor = aCursor;

  // Try to set the cursor image first, and fall back to the numeric cursor.
  GdkCursor* imageCursor = nullptr;
  if (mCustomCursorAllowed) {
    imageCursor = GetCursorForImage(aCursor, GdkCeiledScaleFactor());
  }

  // When using a custom cursor, clear the cursor first using eCursor_none, in
  // order to work around https://gitlab.gnome.org/GNOME/gtk/-/issues/6242
  GdkCursor* nonImageCursor =
      get_gtk_cursor(imageCursor ? eCursor_none : aCursor.mDefaultCursor);
  auto CleanupCursor = mozilla::MakeScopeExit([&]() {
    // get_gtk_cursor returns a weak reference, which we shouldn't unref.
    if (imageCursor) {
      g_object_unref(imageCursor);
    }
  });

  gdk_window_set_cursor(mGdkWindow, nonImageCursor);
  if (imageCursor) {
    gdk_window_set_cursor(mGdkWindow, imageCursor);
  }
}

void nsWindow::Invalidate(const LayoutDeviceIntRect& aRect) {
  if (!mGdkWindow) {
    return;
  }

  GdkRectangle rect = DevicePixelsToGdkRectRoundOut(aRect);
  gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);

  LOG("Invalidate (rect): %d %d %d %d\n", rect.x, rect.y, rect.width,
      rect.height);
}

void* nsWindow::GetNativeData(uint32_t aDataType) {
  switch (aDataType) {
    case NS_NATIVE_WINDOW:
    case NS_NATIVE_WIDGET: {
      return mGdkWindow;
    }

    case NS_NATIVE_SHELLWIDGET:
      return GetGtkWidget();

    case NS_NATIVE_WINDOW_WEBRTC_DEVICE_ID:
      if (!mGdkWindow) {
        return nullptr;
      }
#ifdef MOZ_X11
      if (GdkIsX11Display()) {
        return (void*)GDK_WINDOW_XID(gdk_window_get_toplevel(mGdkWindow));
      }
#endif
      NS_WARNING(
          "nsWindow::GetNativeData(): NS_NATIVE_WINDOW_WEBRTC_DEVICE_ID is not "
          "handled on Wayland!");
      return nullptr;
    case NS_RAW_NATIVE_IME_CONTEXT: {
      void* pseudoIMEContext = GetPseudoIMEContext();
      if (pseudoIMEContext) {
        return pseudoIMEContext;
      }
      // If IME context isn't available on this widget, we should set |this|
      // instead of nullptr.
      if (!mIMContext) {
        return this;
      }
      return mIMContext.get();
    }
    case NS_NATIVE_OPENGL_CONTEXT:
      return nullptr;
    case NS_NATIVE_EGL_WINDOW:
      return mIsDestroyed ? nullptr : mEGLWindow;
    default:
      NS_WARNING("nsWindow::GetNativeData called with bad value");
      return nullptr;
  }
}

nsresult nsWindow::SetTitle(const nsAString& aTitle) {
  if (!mShell) {
    return NS_OK;
  }

  // convert the string into utf8 and set the title.
#define UTF8_FOLLOWBYTE(ch) (((ch) & 0xC0) == 0x80)
  NS_ConvertUTF16toUTF8 titleUTF8(aTitle);
  if (titleUTF8.Length() > NS_WINDOW_TITLE_MAX_LENGTH) {
    // Truncate overlong titles (bug 167315). Make sure we chop after a
    // complete sequence by making sure the next char isn't a follow-byte.
    uint32_t len = NS_WINDOW_TITLE_MAX_LENGTH;
    while (UTF8_FOLLOWBYTE(titleUTF8[len])) --len;
    titleUTF8.Truncate(len);
  }
  gtk_window_set_title(GTK_WINDOW(mShell), (const char*)titleUTF8.get());

  return NS_OK;
}

void nsWindow::SetIcon(const nsAString& aIconSpec) {
  if (!mShell) {
    return;
  }

  nsAutoCString iconName;
  if (aIconSpec.EqualsLiteral("default")) {
    nsAutoString brandName;
    WidgetUtils::GetBrandShortName(brandName);
    if (brandName.IsEmpty()) {
      brandName.AssignLiteral(u"Mozilla");
    }
    AppendUTF16toUTF8(brandName, iconName);
    ToLowerCase(iconName);
  } else {
    AppendUTF16toUTF8(aIconSpec, iconName);
  }

  {
    gint* iconSizes = gtk_icon_theme_get_icon_sizes(
        gtk_icon_theme_get_default(), iconName.get());
    const bool foundIcon = (iconSizes[0] != 0);
    g_free(iconSizes);

    if (foundIcon) {
      gtk_window_set_icon_name(GTK_WINDOW(mShell), iconName.get());
      return;
    }
  }

  // Look for icons with the following suffixes appended to the base name
  // The last two entries (for the old XPM format) will be ignored unless
  // no icons are found using other suffixes. XPM icons are deprecated.

  const char16_t extensions[9][8] = {u".png",    u"16.png", u"32.png",
                                     u"48.png",  u"64.png", u"128.png",
                                     u"256.png", u".xpm",   u"16.xpm"};

  RefPtr<GdkPixbuf> icon;
  for (uint32_t i = 0; i < std::size(extensions); i++) {
    // Don't bother looking for XPM versions if we found a PNG.
    if (i == std::size(extensions) - 2 && icon) {
      break;
    }

    nsCOMPtr<nsIFile> iconFile;
    nsAutoCString path;
    ResolveIconName(aIconSpec, nsDependentString(extensions[i]),
                    getter_AddRefs(iconFile));
    if (!iconFile) {
      continue;
    }
    iconFile->GetNativePath(path);
    RefPtr<GdkPixbuf> newIcon =
        dont_AddRef(gdk_pixbuf_new_from_file(path.get(), nullptr));
    if (!newIcon) {
      continue;
    }
    icon = std::move(newIcon);
  }

  if (icon) {
    gtk_window_set_icon(GTK_WINDOW(mShell), icon.get());
  } else {
    // leave the default icon intact if no matching icons were found
  }
}

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  return ToLayoutDevicePixels(mClientArea.TopLeft());
}

DesktopIntPoint nsWindow::WidgetToScreenOffsetUnscaled() {
  return DesktopIntPoint(mClientArea.x, mClientArea.y);
}

void nsWindow::CaptureRollupEvents(bool aDoCapture) {
  LOG("CaptureRollupEvents(%d)\n", aDoCapture);
  if (mIsDestroyed) {
    return;
  }

  static constexpr auto kCaptureEventsMask =
      GdkEventMask(GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                   GDK_POINTER_MOTION_MASK | GDK_TOUCH_MASK);

  static bool sSystemNeedsPointerGrab = [&] {
    if (GdkIsWaylandDisplay()) {
      return false;
    }
    // We only need to grab the pointer for X servers that move the focus with
    // the pointer (like twm, sawfish...). Since we roll up popups on focus out,
    // not grabbing the pointer triggers rollup when the mouse enters the popup
    // and leaves the main window, see bug 1807482.
    //
    // FVWM is also affected but less severely: the pointer can enter the
    // popup, but if it briefly moves out of the popup and over the main window
    // then we see a focus change and roll up the popup.
    //
    // We don't do it for most common desktops, if only because it causes X11
    // crashes like bug 1607713.
    const auto& desktop = GetDesktopEnvironmentIdentifier();
    return desktop.EqualsLiteral("twm") || desktop.EqualsLiteral("sawfish") ||
           StringBeginsWith(desktop, "fvwm"_ns);
  }();

  const bool grabPointer = [] {
    switch (StaticPrefs::widget_gtk_grab_pointer()) {
      case 0:
        return false;
      case 1:
        return true;
      default:
        return sSystemNeedsPointerGrab;
    }
  }();

  if (!grabPointer) {
    return;
  }

  mNeedsToRetryCapturingMouse = false;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  if (aDoCapture) {
    if (mIsDragPopup || DragInProgress()) {
      // Don't add a grab if a drag is in progress, or if the widget is a drag
      // feedback popup. (panels with type="drag").
      return;
    }

    if (!mHasMappedToplevel) {
      // On X, capturing an unmapped window is pointless (returns
      // GDK_GRAB_NOT_VIEWABLE). Avoid the X server round-trip and just retry
      // when we're mapped.
      mNeedsToRetryCapturingMouse = true;
      return;
    }

    // gdk_pointer_grab is deprecated in favor of gdk_device_grab, but that
    // causes a strange bug on X11, most obviously with nested popup menus:
    // we somehow take the pointer position relative to the top left of the
    // outer menu and use it as if it were relative to the submenu.  This
    // doesn't happen with gdk_pointer_grab even though the code is very
    // similar.  See the video attached to bug 1750721 for a demonstration,
    // and see also bug 1820542 for when the same thing happened with
    // another attempt to use gdk_device_grab.
    //
    // (gdk_device_grab is deprecated in favor of gdk_seat_grab as of 3.20,
    // but at the time of this writing we still support older versions of
    // GTK 3.)
    GdkGrabStatus status =
        gdk_pointer_grab(GetToplevelGdkWindow(),
                         /* owner_events = */ true, kCaptureEventsMask,
                         /* confine_to = */ nullptr,
                         /* cursor = */ nullptr, GetLastUserInputTime());
    (void)NS_WARN_IF(status != GDK_GRAB_SUCCESS);
    LOG(" > pointer grab with status %d", int(status));
    gtk_grab_add(GTK_WIDGET(mContainer));
  } else {
    // There may not have been a drag in process when aDoCapture was set,
    // so make sure to remove any added grab.  This is a no-op if the grab
    // was not added to this widget.
    gtk_grab_remove(GTK_WIDGET(mContainer));
    gdk_pointer_ungrab(GetLastUserInputTime());
  }
#pragma GCC diagnostic pop
}

nsresult nsWindow::GetAttention(int32_t aCycleCount) {
  LOG("nsWindow::GetAttention");

  GtkWidget* top_window = GetGtkWidget();
  GtkWidget* top_focused_window =
      gFocusWindow ? gFocusWindow->GetGtkWidget() : nullptr;

  // Don't get attention if the window is focused anyway.
  if (top_window && (gtk_widget_get_visible(top_window)) &&
      top_window != top_focused_window) {
    SetUrgencyHint(top_window, true);
  }

  return NS_OK;
}

bool nsWindow::HasPendingInputEvent() {
  // This sucks, but gtk/gdk has no way to answer the question we want while
  // excluding paint events, and there's no X API that will let us peek
  // without blocking or removing.  To prevent event reordering, peek
  // anything except expose events.  Reordering expose and others should be
  // ok, hopefully.
  bool haveEvent = false;
#ifdef MOZ_X11
  XEvent ev;
  if (GdkIsX11Display()) {
    Display* display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    haveEvent = XCheckMaskEvent(
        display,
        KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
            EnterWindowMask | LeaveWindowMask | PointerMotionMask |
            PointerMotionHintMask | Button1MotionMask | Button2MotionMask |
            Button3MotionMask | Button4MotionMask | Button5MotionMask |
            ButtonMotionMask | KeymapStateMask | VisibilityChangeMask |
            StructureNotifyMask | ResizeRedirectMask | SubstructureNotifyMask |
            SubstructureRedirectMask | FocusChangeMask | PropertyChangeMask |
            ColormapChangeMask | OwnerGrabButtonMask,
        &ev);
    if (haveEvent) {
      XPutBackEvent(display, &ev);
    }
  }
#endif
  return haveEvent;
}

#ifdef cairo_copy_clip_rectangle_list
#  error "Looks like we're including Mozilla's cairo instead of system cairo"
#endif
bool nsWindow::ExtractExposeRegion(LayoutDeviceIntRegion& aRegion,
                                   cairo_t* cr) {
  cairo_rectangle_list_t* rects = cairo_copy_clip_rectangle_list(cr);
  if (rects->status != CAIRO_STATUS_SUCCESS) {
    NS_WARNING("Failed to obtain cairo rectangle list.");
    return false;
  }

  for (int i = 0; i < rects->num_rectangles; i++) {
    const cairo_rectangle_t& r = rects->rectangles[i];
    LOGVERBOSE("  expose region unscaled: [%d, %d] -> [%d x %d]", (int)r.x,
               (int)r.y, (int)r.width, (int)r.height);
    aRegion.Or(aRegion,
               LayoutDeviceIntRect::Truncate((float)r.x, (float)r.y,
                                             (float)r.width, (float)r.height));
  }

  cairo_rectangle_list_destroy(rects);
  return true;
}

void nsWindow::RequestRepaint(LayoutDeviceIntRegion& aRepaintRegion) {
  WindowRenderer* renderer = GetWindowRenderer();
  WebRenderLayerManager* layerManager = renderer->AsWebRender();
  KnowsCompositor* knowsCompositor = renderer->AsKnowsCompositor();

  if (knowsCompositor && layerManager && mCompositorSession) {
    LOG("nsWindow::RequestRepaint()");

    if (!mConfiguredClearColor && !IsPopup()) {
      layerManager->WrBridge()->SendSetDefaultClearColor(LookAndFeel::Color(
          LookAndFeel::ColorID::Window, PreferenceSheet::ColorSchemeForChrome(),
          LookAndFeel::UseStandins::No));
      mConfiguredClearColor = true;
    }

    // We need to paint to the screen even if nothing changed, since if we
    // don't have a compositing window manager, our pixels could be stale.
    layerManager->SetNeedsComposite(true);
    layerManager->SendInvalidRegion(aRepaintRegion.ToUnknownRegion());
  }
}

gboolean nsWindow::OnExposeEvent(cairo_t* cr) {
  LOG("nsWindow::OnExposeEvent GdkWindow [%p] XID [0x%lx]", mGdkWindow,
      GetX11Window());

  // This might destroy us.
  NotifyOcclusionState(OcclusionState::VISIBLE);
  if (mIsDestroyed) {
    LOG("destroyed after NotifyOcclusionState()");
    return FALSE;
  }

  // Send any pending resize events so that layout can update. May run event
  // loop and destroy us.
  MaybeRecomputeBounds();
  if (mIsDestroyed) {
    LOG("destroyed after MaybeRecomputeBounds()");
    return FALSE;
  }

  // Windows that are not visible will be painted after they become visible.
  if (!mHasMappedToplevel) {
    LOG("quit, !mHasMappedToplevel");
    return FALSE;
  }

  if (!GetPaintListener()) {
    LOG("quit, !GetPaintListener()");
    return FALSE;
  }

  LayoutDeviceIntRegion exposeRegion;
  if (!ExtractExposeRegion(exposeRegion, cr)) {
    LOG("  no rects, quit");
    return FALSE;
  }

  if (mIsDragPopup && DrawDragPopupSurface(cr)) {
    return FALSE;
  }

  gint scale = GdkCeiledScaleFactor();
  LayoutDeviceIntRegion region = exposeRegion;
  region.ScaleRoundOut(scale, scale);

  RequestRepaint(region);

  RefPtr<nsWindow> strongThis(this);

  // If the window has been destroyed during the will paint notification,
  // there is nothing left to do.
  if (mIsDestroyed) {
    LOG("quit, mIsDestroyed");
    return TRUE;
  }

  // Re-get all rendering components since the will paint notification
  // might have killed it.
  nsIWidgetListener* listener = GetPaintListener();
  if (!listener) {
    LOG("quit, !listener");
    return FALSE;
  }

  WindowRenderer* renderer = GetWindowRenderer();
  WebRenderLayerManager* layerManager = renderer->AsWebRender();
  KnowsCompositor* knowsCompositor = renderer->AsKnowsCompositor();

  if (knowsCompositor && layerManager && layerManager->NeedsComposite()) {
    LOG("needs composite, ScheduleComposite() call");
    layerManager->ScheduleComposite(wr::RenderReasons::WIDGET);
    layerManager->SetNeedsComposite(false);
  }

  region.AndWith(LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientSize()));
  LOGVERBOSE("painted region scaled %s (client size scaled %s)",
             ToString(region).c_str(), ToString(GetClientSize()).c_str());
  if (region.IsEmpty()) {
    LOG("quit, region.IsEmpty()");
    return TRUE;
  }

  // If this widget uses OMTC...
  if (renderer->GetBackendType() == LayersBackend::LAYERS_WR) {
    LOG("redirect painting to OMTC rendering...");
    listener->PaintWindow(this);
    return TRUE;
  }

  RefPtr<DrawTarget> dt = StartRemoteDrawingInRegion(region);
  if (!dt || !dt->IsValid()) {
    return FALSE;
  }
  Maybe<gfxContext> ctx;
  IntRect boundsRect = region.GetBounds().ToUnknownRect();
  IntPoint offset(0, 0);
  if (dt->GetSize() == boundsRect.Size()) {
    offset = boundsRect.TopLeft();
    dt->SetTransform(Matrix::Translation(-offset));
  }

#ifdef MOZ_X11
  gfxUtils::ClipToRegion(dt, region.ToUnknownRegion());
  ctx.emplace(dt, /* aPreserveTransform */ true);

#  if 0
    // NOTE: Paint flashing region would be wrong for cairo, since
    // cairo inflates the update region, etc.  So don't paint flash
    // for cairo.
#    ifdef DEBUG
    // XXX aEvent->region may refer to a newly-invalid area.  FIXME
    if (0 && WANT_PAINT_FLASHING && gtk_widget_get_window(aEvent))
        gdk_window_flash(mGdkWindow, 1, 100, aEvent->region);
#    endif
#  endif

#endif  // MOZ_X11

  if (renderer->GetBackendType() == LayersBackend::LAYERS_NONE) {
    if (GetTransparencyMode() == TransparencyMode::Transparent &&
        mHasAlphaVisual) {
      // If our draw target is unbuffered and we use an alpha channel,
      // clear the image beforehand to ensure we don't get artifacts from a
      // reused SHM image. See bug 1258086.
      dt->ClearRect(Rect(boundsRect));
    }
    AutoLayerManagerSetup setupLayerManager(this, ctx.ptrOr(nullptr));
    listener->PaintWindow(this);
  }

#ifdef MOZ_X11
  ctx.reset();
  dt->PopClip();
#endif  // MOZ_X11

  EndRemoteDrawingInRegion(dt, region);

  // Synchronously flush any new dirty areas
  if (cairo_region_t* dirtyArea = gdk_window_get_update_area(mGdkWindow)) {
    gdk_window_invalidate_region(mGdkWindow, dirtyArea, false);
    cairo_region_destroy(dirtyArea);
    gdk_window_process_updates(mGdkWindow, false);
  }

  // check the return value!
  return TRUE;
}

gboolean nsWindow::OnShellConfigureEvent(GdkEventConfigure* aEvent) {
  // These events are only received on toplevel windows.
  //
  // GDK ensures that the coordinates are the client window top-left wrt the
  // root window.
  //
  //   GDK calculates the cordinates for real ConfigureNotify events on
  //   managed windows (that would normally be relative to the parent
  //   window).
  //
  //   Synthetic ConfigureNotify events are from the window manager and
  //   already relative to the root window.  GDK creates all X windows with
  //   border_width = 0, so synthetic events also indicate the top-left of
  //   the client window.
  //
  //   Override-redirect windows are children of the root window so parent
  //   coordinates are root coordinates.

#ifdef MOZ_LOGGING
  if (LOG_ENABLED()) {
    auto widgetArea =
        DesktopIntRect(aEvent->x, aEvent->y, aEvent->width, aEvent->height);
    auto scaledWidgetArea = ToLayoutDevicePixels(widgetArea);
    LOG("nsWindow::OnShellConfigureEvent() [%d, %d] -> [%d x %d] scale %.2f "
        "(scaled size %d x %d)\n",
        widgetArea.x, widgetArea.y, widgetArea.width, widgetArea.height,
        FractionalScaleFactor(), scaledWidgetArea.width,
        scaledWidgetArea.height);
  }
#endif

  if (mPendingConfigures > 0) {
    mPendingConfigures--;
  }

  // Don't fire configure event for scale changes, we handle that
  // OnScaleEvent event. Skip that for toplevel windows only.
  if (IsTopLevelWidget() &&
      mCeiledScaleFactor != gdk_window_get_scale_factor(mGdkWindow)) {
    LOG("  scale factor changed to %d, return early",
        gdk_window_get_scale_factor(mGdkWindow));
    return FALSE;
  }

  // We generally want to recalculate bounds whenever we get the container
  // size-allocate event (OnContainerSizeAllocate). Unfortunately some changes
  // like window moves or tiling might get us a toplevel configure event, but
  // not a container size-allocate (understandably), so we need to recompute
  // bounds still.
  if (IsTopLevelWidget()) {
    SchedulePendingBounds();
  }
  return FALSE;
}

void nsWindow::OnContainerSizeAllocate(GtkAllocation* aAllocation) {
  mHasReceivedSizeAllocate = true;
  const auto clientArea = DesktopIntRect(
      aAllocation->x, aAllocation->y, aAllocation->width, aAllocation->height);
#ifdef MOZ_LOGGING
  if (LOG_ENABLED()) {
    auto scaledClientAread = ToLayoutDevicePixels(clientArea);
    LOG("nsWindow::OnContainerSizeAllocate [%d,%d] -> [%d x %d] scaled [%.2f] "
        "[%d x %d]",
        aAllocation->x, aAllocation->y, aAllocation->width, aAllocation->height,
        FractionalScaleFactor(), scaledClientAread.width,
        scaledClientAread.height);
  }
#endif

  SchedulePendingBounds();

  // Invalidate the new part of the window now for the pending paint to
  // minimize background flashes (GDK does not do this for external
  // renewClientSizes of toplevels.)
  if (mClientArea.Size() == clientArea.Size()) {
    return;
  }

  if (mClientArea.width < clientArea.width) {
    GdkRectangle rect{mClientArea.width, 0,
                      clientArea.width - mClientArea.width, clientArea.height};
    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
  }
  if (mClientArea.height < clientArea.height) {
    GdkRectangle rect{0, mClientArea.height, clientArea.width,
                      clientArea.height - mClientArea.height};
    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
  }
}

void nsWindow::SchedulePendingBounds() {
  if (mPendingBoundsChange) {
    return;
  }
  mPendingBoundsChange = true;
  NS_DispatchToCurrentThread(NewRunnableMethod(
      "nsWindow::MaybeRecomputeBounds", this, &nsWindow::MaybeRecomputeBounds));
}

void nsWindow::MaybeRecomputeBounds() {
  LOG("MaybeRecomputeBounds %d", mPendingBoundsChange);
  if (mPendingBoundsChange) {
    // Make sure GTK has laid out our child window / mContainer properly,
    // so that we don't get broken client margins, and in order to avoid
    // spurious resize events.
    gtk_container_check_resize(GTK_CONTAINER(mShell));
    RecomputeBounds();
  }
}

void nsWindow::OnDeleteEvent() {
  if (mWidgetListener) {
    mWidgetListener->RequestWindowClose(this);
  }
}

void nsWindow::OnEnterNotifyEvent(GdkEventCrossing* aEvent) {
  LOG("enter notify (win=%p, sub=%p): %.2f, %.2f mode %d, detail %d\n",
      aEvent->window, aEvent->subwindow, aEvent->x, aEvent->y, aEvent->mode,
      aEvent->detail);
  // This skips NotifyVirtual and NotifyNonlinearVirtual enter notify events
  // when the pointer enters a child window.  If the destination window is a
  // Gecko window then we'll catch the corresponding event on that window,
  // but we won't notice when the pointer directly enters a foreign (plugin)
  // child window without passing over a visible portion of a Gecko window.
  if (aEvent->subwindow) {
    return;
  }

  // Check before checking for ungrab as the button state may have
  // changed while a non-Gecko ancestor window had a pointer grab.
  DispatchMissedButtonReleases(aEvent);
  mLastMouseCoordinates.Set(aEvent);

  WidgetMouseEvent event(true, eMouseEnterIntoWidget, this,
                         WidgetMouseEvent::eReal);

  event.mRefPoint = GdkEventCoordsToDevicePixels(aEvent->x, aEvent->y);
  event.AssignEventTime(GetWidgetEventTime(aEvent->time));
  KeymapWrapper::InitInputEvent(event, aEvent->state);

  LOG("OnEnterNotify");

  DispatchInputEvent(&event);
}

// Some window managers send a bogus top-level leave-notify event on every
// click. That confuses our event handling code in ways that can break websites,
// see bug 1805939 for details.
//
// Make sure to only check this on bogus environments, since for environments
// with CSD, gdk_device_get_window_at_position could return the window even when
// the pointer is in the decoration area.
static bool IsBogusLeaveNotifyEvent(GdkWindow* aWindow,
                                    GdkEventCrossing* aEvent) {
  static bool sBogusWm = [] {
    if (GdkIsWaylandDisplay()) {
      return false;
    }
    const auto& desktopEnv = GetDesktopEnvironmentIdentifier();
    return desktopEnv.EqualsLiteral("fluxbox") ||   // Bug 1805939 comment 0.
           desktopEnv.EqualsLiteral("blackbox") ||  // Bug 1805939 comment 32.
           desktopEnv.EqualsLiteral("lg3d") ||      // Bug 1820405.
           desktopEnv.EqualsLiteral("pekwm") ||     // Bug 1822911.
           StringBeginsWith(desktopEnv, "fvwm"_ns);
  }();

  const bool shouldCheck = [] {
    switch (StaticPrefs::widget_gtk_ignore_bogus_leave_notify()) {
      case 0:
        return false;
      case 1:
        return true;
      default:
        return sBogusWm;
    }
  }();

  if (!shouldCheck || !aWindow) {
    return false;
  }
  GdkDevice* pointer = GdkGetPointer();
  GdkWindow* winAtPt =
      gdk_device_get_window_at_position(pointer, nullptr, nullptr);
  if (!winAtPt) {
    return false;
  }
  // We're still in the same top level window, ignore this leave notify event.
  GdkWindow* topLevelAtPt = gdk_window_get_toplevel(winAtPt);
  GdkWindow* topLevelWidget = gdk_window_get_toplevel(aWindow);
  return topLevelAtPt == topLevelWidget;
}

void nsWindow::OnLeaveNotifyEvent(GdkEventCrossing* aEvent) {
  LOG("leave notify (win=%p, sub=%p): %.2f, %.2f mode %d, detail %d\n",
      aEvent->window, aEvent->subwindow, aEvent->x, aEvent->y, aEvent->mode,
      aEvent->detail);

  // This ignores NotifyVirtual and NotifyNonlinearVirtual leave notify
  // events when the pointer leaves a child window.  If the destination
  // window is a Gecko window then we'll catch the corresponding event on
  // that window.
  //
  // XXXkt However, we will miss toplevel exits when the pointer directly
  // leaves a foreign (plugin) child window without passing over a visible
  // portion of a Gecko window.
  if (aEvent->subwindow) {
    return;
  }

  // The filter out for subwindows should make sure that this is targeted to
  // this nsWindow.
  const bool leavingTopLevel = IsTopLevelWidget();
  if (leavingTopLevel && IsBogusLeaveNotifyEvent(mGdkWindow, aEvent)) {
    return;
  }

  WidgetMouseEvent event(true, eMouseExitFromWidget, this,
                         WidgetMouseEvent::eReal);

  event.mRefPoint = GdkEventCoordsToDevicePixels(aEvent->x, aEvent->y);
  event.AssignEventTime(GetWidgetEventTime(aEvent->time));
  event.mExitFrom = Some(leavingTopLevel ? WidgetMouseEvent::ePlatformTopLevel
                                         : WidgetMouseEvent::ePlatformChild);
  KeymapWrapper::InitInputEvent(event, aEvent->state);

  LOG("OnLeaveNotify");

  DispatchInputEvent(&event);
}

Maybe<GdkWindowEdge> nsWindow::CheckResizerEdge(
    const LayoutDeviceIntPoint& aPoint) {
  // Don't allow resizing maximized/fullscreen windows, nor add extra resizing
  // margins on non PiP windows.
  if (mSizeMode != nsSizeMode_Normal || mPiPType != PiPType::MediaPiP) {
    return Nothing();
  }

  // If we're not in a PiP window, allow 1px resizer edge from the top edge,
  // and nothing else.
  // This is to allow resizes of tiled windows on KDE, see bug 1813554.
  const int resizerHeight = 15 * GdkCeiledScaleFactor();
  const int resizerWidth = resizerHeight * 4;

  auto bounds = GetScreenBounds();
  const int topDist = aPoint.y;
  const int leftDist = aPoint.x;
  const int rightDist = bounds.width - aPoint.x;
  const int bottomDist = bounds.height - aPoint.y;

  // We can't emulate resize of North/West edges on Wayland as we can't shift
  // toplevel window.
  bool waylandLimitedResize = mAspectRatio != 0.0f && GdkIsWaylandDisplay();

  if (topDist <= resizerHeight && mResizableEdges.Top()) {
    if (rightDist <= resizerWidth && mResizableEdges.Right()) {
      return Some(GDK_WINDOW_EDGE_NORTH_EAST);
    }
    if (leftDist <= resizerWidth && mResizableEdges.Left()) {
      return Some(GDK_WINDOW_EDGE_NORTH_WEST);
    }
    return waylandLimitedResize ? Nothing() : Some(GDK_WINDOW_EDGE_NORTH);
  }

  if (bottomDist <= resizerHeight && mResizableEdges.Bottom()) {
    if (rightDist <= resizerWidth && mResizableEdges.Right()) {
      return Some(GDK_WINDOW_EDGE_SOUTH_EAST);
    }
    if (leftDist <= resizerWidth && mResizableEdges.Left()) {
      return Some(GDK_WINDOW_EDGE_SOUTH_WEST);
    }
    return Some(GDK_WINDOW_EDGE_SOUTH);
  }

  if (leftDist <= resizerHeight && mResizableEdges.Left()) {
    if (topDist <= resizerWidth && mResizableEdges.Top()) {
      return Some(GDK_WINDOW_EDGE_NORTH_WEST);
    }
    if (bottomDist <= resizerWidth && mResizableEdges.Bottom()) {
      return Some(GDK_WINDOW_EDGE_SOUTH_WEST);
    }
    return waylandLimitedResize ? Nothing() : Some(GDK_WINDOW_EDGE_WEST);
  }

  if (rightDist <= resizerHeight && mResizableEdges.Right()) {
    if (topDist <= resizerWidth && mResizableEdges.Top()) {
      return Some(GDK_WINDOW_EDGE_NORTH_EAST);
    }
    if (bottomDist <= resizerWidth && mResizableEdges.Bottom()) {
      return Some(GDK_WINDOW_EDGE_SOUTH_EAST);
    }
    return Some(GDK_WINDOW_EDGE_EAST);
  }
  return Nothing();
}

template <typename Event>
static LayoutDeviceIntPoint GetRefPoint(nsWindow* aWindow, Event* aEvent) {
  return aWindow->GdkEventCoordsToDevicePixels(aEvent->x, aEvent->y);
}

void nsWindow::EmulateResizeDrag(GdkEventMotion* aEvent) {
  GdkPoint newPoint{gint(aEvent->x), gint(aEvent->y)};
  auto oldPoint = mLastResizePoint;
  mLastResizePoint = newPoint;

  auto size = GetScreenBoundsUnscaled().Size();
  size.width += newPoint.x - oldPoint.x;
  size.height += newPoint.y - oldPoint.y;

  if (mAspectResizer.value() == GTK_ORIENTATION_VERTICAL) {
    size.width = int(size.height * mAspectRatio);
  } else {  // GTK_ORIENTATION_HORIZONTAL
    size.height = int(size.width / mAspectRatio);
  }
  LOG("  aspect ratio correction %d x %d aspect %.2f\n", size.width,
      size.height, mAspectRatio);
  gtk_window_resize(GTK_WINDOW(mShell), size.width, size.height);
}

void nsWindow::OnMotionNotifyEvent(GdkEventMotion* aEvent) {
  mLastMouseCoordinates.Set(aEvent);

  // Emulate gdk_window_begin_resize_drag() for windows
  // with fixed aspect ratio on Wayland.
  if (mAspectResizer && mAspectRatio != 0.0f) {
    EmulateResizeDrag(aEvent);
    return;
  }

  if (mWindowShouldStartDragging &&
      is_drag_threshold_exceeded((GdkEvent*)aEvent)) {
    mWindowShouldStartDragging = false;
    // when the drag ends, we don't get a button release event, so we clear
    // the last mouse press event here.
    SetLastPointerDownEvent(nullptr);
    GdkWindow* dragWindow = nullptr;

    // find the top-level window
    dragWindow = gdk_window_get_toplevel(mGdkWindow);
    MOZ_ASSERT(dragWindow, "gdk_window_get_toplevel should not return null");

#ifdef MOZ_X11
    if (dragWindow && GdkIsX11Display()) {
      // Workaround for https://bugzilla.gnome.org/show_bug.cgi?id=789054
      // To avoid crashes disable double-click on WM without _NET_WM_MOVERESIZE.
      // See _should_perform_ewmh_drag() at gdkwindow-x11.c
      GdkScreen* screen = gdk_window_get_screen(dragWindow);
      GdkAtom atom = gdk_atom_intern("_NET_WM_MOVERESIZE", FALSE);
      if (!gdk_x11_screen_supports_net_wm_hint(screen, atom)) {
        dragWindow = nullptr;
      }
    }
#endif

    if (dragWindow) {
#ifdef MOZ_WAYLAND
      if (AsWayland() && AsWayland()->PIPMove()) {
        // The ungrab is needed because the compositor won't send a button
        // release event. GDK may get stuck thinking that the implicit grab is
        // still active.
        GdkSeatUngrab(GdkDeviceGetSeat(GdkGetPointer()));
        return;
      }
#endif
      gdk_window_begin_move_drag(dragWindow, 1, aEvent->x_root, aEvent->y_root,
                                 aEvent->time);
      return;
    }
  }

  mWidgetCursorLocked = false;
  const auto refPoint = GetRefPoint(this, aEvent);
  if (auto edge = CheckResizerEdge(refPoint)) {
    nsCursor cursor = eCursor_none;
    switch (*edge) {
      case GDK_WINDOW_EDGE_SOUTH:
      case GDK_WINDOW_EDGE_NORTH:
        cursor = eCursor_ns_resize;
        break;
      case GDK_WINDOW_EDGE_WEST:
      case GDK_WINDOW_EDGE_EAST:
        cursor = eCursor_ew_resize;
        break;
      case GDK_WINDOW_EDGE_NORTH_WEST:
      case GDK_WINDOW_EDGE_SOUTH_EAST:
        cursor = eCursor_nwse_resize;
        break;
      case GDK_WINDOW_EDGE_NORTH_EAST:
      case GDK_WINDOW_EDGE_SOUTH_WEST:
        cursor = eCursor_nesw_resize;
        break;
    }
    SetCursor(Cursor{cursor});
    // If we set resize cursor on widget level keep it locked and prevent layout
    // to switch it back to default (by synthetic mouse events for instance)
    // until resize is finished. This affects PIP windows only.
    if (mPiPType == PiPType::MediaPiP) {
      mWidgetCursorLocked = true;
    }
    return;
  }

  WidgetMouseEvent event(true, eMouseMove, this, WidgetMouseEvent::eReal);

  gdouble pressure = 0;
  gdk_event_get_axis((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
  // Sometime gdk generate 0 pressure value between normal values
  // We have to ignore that and use last valid value
  if (pressure) {
    mLastMotionPressure = pressure;
  }
  event.mPressure = mLastMotionPressure;
  event.mRefPoint = refPoint;
  event.AssignEventTime(GetWidgetEventTime(aEvent->time));

  bool isEraser;
  bool isPenEvent = IsPenEvent((GdkEvent*)aEvent, &isEraser);

  // Workaround because GTK's aEvent->state does not include the button states
  // for the barrel buttons of the stylus.
  // Actually, an unconditional aEvent->state = gButtonState; should be ok,
  // but let's do this in a minimally invasive fashion.
  if (isPenEvent) {
    aEvent->state |= gButtonState & (GDK_BUTTON2_MASK | GDK_BUTTON3_MASK);
  }

  KeymapWrapper::InitInputEvent(event, aEvent->state, isEraser);

  if (isPenEvent) {
    FetchAndAdjustPenData(event, (GdkEvent*)aEvent);
  }

  DispatchInputEvent(&event);
}

// If the automatic pointer grab on ButtonPress has deactivated before
// ButtonRelease, and the mouse button is released while the pointer is not
// over any a Gecko window, then the ButtonRelease event will not be received.
// (A similar situation exists when the pointer is grabbed with owner_events
// True as the ButtonRelease may be received on a foreign [plugin] window).
// Use this method to check for released buttons when the pointer returns to a
// Gecko window.
void nsWindow::DispatchMissedButtonReleases(GdkEventCrossing* aGdkEvent) {
  guint changed = aGdkEvent->state ^ gButtonState;
  // Only consider button releases.
  // (Ignore button presses that occurred outside Gecko.)
  guint released = changed & gButtonState;
  gButtonState = aGdkEvent->state;

  // Loop over each button, excluding mouse wheel buttons 4 and 5 for which
  // GDK ignores releases.
  for (guint buttonMask = GDK_BUTTON1_MASK; buttonMask <= GDK_BUTTON3_MASK;
       buttonMask <<= 1) {
    if (released & buttonMask) {
      int16_t buttonType;
      switch (buttonMask) {
        case GDK_BUTTON1_MASK:
          buttonType = MouseButton::ePrimary;
          break;
        case GDK_BUTTON2_MASK:
          buttonType = MouseButton::eMiddle;
          break;
        default:
          NS_ASSERTION(buttonMask == GDK_BUTTON3_MASK,
                       "Unexpected button mask");
          buttonType = MouseButton::eSecondary;
      }

      LOG("Synthesized button %u release", guint(buttonType + 1));

      // Dispatch a synthesized button up event to tell Gecko about the
      // change in state.  This event is marked as synthesized so that
      // it is not dispatched as a DOM event, because we don't know the
      // position, widget, modifiers, or time/order.
      WidgetMouseEvent synthEvent(true, eMouseUp, this,
                                  WidgetMouseEvent::eSynthesized);
      synthEvent.mButton = buttonType;
      DispatchInputEvent(&synthEvent);
    }
  }
}

void nsWindow::InitButtonEvent(WidgetMouseEvent& aEvent,
                               GdkEventButton* aGdkEvent,
                               const LayoutDeviceIntPoint& aRefPoint,
                               bool isEraser) {
  aEvent.mRefPoint = aRefPoint;

  guint modifierState = aGdkEvent->state;
  // aEvent's state includes the button state from immediately before this
  // event.  If aEvent is a mousedown or mouseup event, we need to update
  // the button state.
  guint buttonMask = 0;
  switch (aGdkEvent->button) {
    case 1:
      buttonMask = GDK_BUTTON1_MASK;
      break;
    case 2:
      buttonMask = GDK_BUTTON2_MASK;
      break;
    case 3:
      buttonMask = GDK_BUTTON3_MASK;
      break;
  }
  if (aGdkEvent->type == GDK_BUTTON_RELEASE) {
    modifierState &= ~buttonMask;
  } else {
    modifierState |= buttonMask;
  }

  KeymapWrapper::InitInputEvent(aEvent, modifierState, isEraser);

  aEvent.AssignEventTime(GetWidgetEventTime(aGdkEvent->time));

  switch (aGdkEvent->type) {
    case GDK_2BUTTON_PRESS:
      aEvent.mClickCount = 2;
      break;
    case GDK_3BUTTON_PRESS:
      aEvent.mClickCount = 3;
      break;
      // default is one click
    default:
      aEvent.mClickCount = 1;
  }
}

static guint ButtonMaskFromGDKButton(guint button) {
  return GDK_BUTTON1_MASK << (button - 1);
}

void nsWindow::DispatchContextMenuEventFromMouseEvent(
    uint16_t domButton, GdkEventButton* aEvent,
    const LayoutDeviceIntPoint& aRefPoint) {
  if (domButton == MouseButton::eSecondary && MOZ_LIKELY(!mIsDestroyed)) {
    WidgetPointerEvent contextMenuEvent(true, eContextMenu, this);
    InitButtonEvent(contextMenuEvent, aEvent, aRefPoint);
    contextMenuEvent.mPressure = mLastMotionPressure;
    DispatchInputEvent(&contextMenuEvent);
  }
}

void nsWindow::TryToShowNativeWindowMenu(GdkEventButton* aEvent) {
  if (!gdk_window_show_window_menu(GetToplevelGdkWindow(), (GdkEvent*)aEvent)) {
    NS_WARNING("Native context menu wasn't shown");
  }
}

bool nsWindow::DoTitlebarAction(LookAndFeel::TitlebarEvent aEvent,
                                GdkEventButton* aButtonEvent) {
  LOG("DoTitlebarAction %s click",
      aEvent == LookAndFeel::TitlebarEvent::Double_Click ? "double" : "middle");
  switch (LookAndFeel::GetTitlebarAction(aEvent)) {
    case LookAndFeel::TitlebarAction::WindowMenu:
      // Titlebar app menu
      LOG("  action menu");
      TryToShowNativeWindowMenu(aButtonEvent);
      break;
    case LookAndFeel::TitlebarAction::WindowLower:
      LOG("  action lower");
      // Lower is part of gtk_surface1 protocol which we can't support
      // as Gtk keeps it private. So emulate it by minimize.
      if (GdkIsWaylandDisplay()) {
        SetSizeMode(nsSizeMode_Minimized);
      } else {
        gdk_window_lower(GetToplevelGdkWindow());
      }
      break;
    case LookAndFeel::TitlebarAction::WindowMinimize:
      LOG("  action minimize");
      SetSizeMode(nsSizeMode_Minimized);
      break;
    case LookAndFeel::TitlebarAction::WindowMaximize:
      LOG("  action maximize");
      SetSizeMode(nsSizeMode_Maximized);
      break;
    case LookAndFeel::TitlebarAction::WindowMaximizeToggle:
      LOG("  action toggle maximize");
      if (mSizeMode == nsSizeMode_Maximized) {
        SetSizeMode(nsSizeMode_Normal);
      } else if (mSizeMode == nsSizeMode_Normal) {
        SetSizeMode(nsSizeMode_Maximized);
      }
      break;
    case LookAndFeel::TitlebarAction::None:
    default:
      LOG("  action none");
      return false;
  }
  return true;
}

void nsWindow::OnButtonPressEvent(GdkEventButton* aEvent) {
  LOG("Button %u press\n", aEvent->button);

  SetLastPointerDownEvent((GdkEvent*)aEvent);
  mLastMouseCoordinates.Set(aEvent);

  // If you double click in GDK, it will actually generate a second
  // GDK_BUTTON_PRESS before sending the GDK_2BUTTON_PRESS, and this is
  // different than the DOM spec.  GDK puts this in the queue
  // programatically, so it's safe to assume that if there's a
  // double click in the queue, it was generated so we can just drop
  // this click.
  GUniquePtr<GdkEvent> peekedEvent(gdk_event_peek());
  if (peekedEvent) {
    GdkEventType type = peekedEvent->any.type;
    if (type == GDK_2BUTTON_PRESS || type == GDK_3BUTTON_PRESS) {
      return;
    }
  }

  nsWindow* containerWindow = GetContainerWindow();
  if (!gFocusWindow && containerWindow) {
    containerWindow->DispatchActivateEvent();
  }

  const auto refPoint = GetRefPoint(this, aEvent);

  // check to see if we should rollup
  if (CheckForRollup(aEvent->x_root, aEvent->y_root, false, false)) {
    if (aEvent->button == 3 && mDraggableRegion.Contains(refPoint)) {
      GUniquePtr<GdkEvent> eventCopy;
      if (aEvent->type != GDK_BUTTON_PRESS) {
        // If the user double-clicks too fast we'll get a 2BUTTON_PRESS event
        // instead, and that isn't handled by open_window_menu, so coerce it
        // into a regular press.
        eventCopy.reset(gdk_event_copy((GdkEvent*)aEvent));
        eventCopy->type = GDK_BUTTON_PRESS;
      }
      TryToShowNativeWindowMenu(eventCopy ? &eventCopy->button : aEvent);
    }
    return;
  }

  // Check to see if the event is within our window's resize region
  if (auto edge = CheckResizerEdge(refPoint)) {
    // On Wayland Gtk fails to vertically/horizontally resize windows
    // with fixed aspect ratio. We need to emulate
    // gdk_window_begin_resize_drag() at OnMotionNotifyEvent().
    if (mAspectRatio != 0.0f && GdkIsWaylandDisplay()) {
      mLastResizePoint = {gint(aEvent->x), gint(aEvent->y)};
      switch (*edge) {
        case GDK_WINDOW_EDGE_SOUTH:
          mAspectResizer = Some(GTK_ORIENTATION_VERTICAL);
          break;
        case GDK_WINDOW_EDGE_EAST:
          mAspectResizer = Some(GTK_ORIENTATION_HORIZONTAL);
          break;
        default:
          mAspectResizer.reset();
          break;
      }
      ApplySizeConstraints();
    }
    if (mAspectResizer) {
      return;
    }
#ifdef MOZ_WAYLAND
    if (AsWayland() && AsWayland()->PIPResize(*edge)) {
      // The ungrab is needed because the compositor won't send a button release
      // event. GDK may get stuck thinking that the implicit grab is still
      // active.
      GdkSeatUngrab(GdkDeviceGetSeat(GdkGetPointer()));
      return;
    }
#endif
    gdk_window_begin_resize_drag(GetToplevelGdkWindow(), *edge, aEvent->button,
                                 aEvent->x_root, aEvent->y_root, aEvent->time);
    return;
  }

  gdouble pressure = 0;
  gdk_event_get_axis((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
  mLastMotionPressure = pressure;

  bool isEraser;
  bool isPenEvent = IsPenEvent((GdkEvent*)aEvent, &isEraser);

  uint16_t domButton;
  switch (aEvent->button) {
    case 1:
      if (isEraser) {
        domButton = MouseButton::eEraser;
      } else {
        domButton = MouseButton::ePrimary;
      }
      break;
    case 2:
      domButton = MouseButton::eMiddle;
      break;
    case 3:
      domButton = MouseButton::eSecondary;
      break;
    // These are mapped to horizontal scroll
    case 6:
    case 7:
      NS_WARNING("We're not supporting legacy horizontal scroll event");
      return;
    // Map buttons 8-9(10) to back/forward
    case 8:
      if (!Preferences::GetBool("mousebutton.4th.enabled", true)) {
        return;
      }
      DispatchCommandEvent(nsGkAtoms::Back);
      return;
    case 9:
    case 10:
      if (!Preferences::GetBool("mousebutton.5th.enabled", true)) {
        return;
      }
      DispatchCommandEvent(nsGkAtoms::Forward);
      return;
    default:
      return;
  }

  gButtonState |= ButtonMaskFromGDKButton(aEvent->button);

  WidgetMouseEvent event(true, eMouseDown, this, WidgetMouseEvent::eReal);
  event.mButton = domButton;
  InitButtonEvent(event, aEvent, refPoint, isEraser);
  event.mPressure = mLastMotionPressure;

  if (isPenEvent) {
    FetchAndAdjustPenData(event, (GdkEvent*)aEvent);
  }

  nsIWidget::ContentAndAPZEventStatus eventStatus = DispatchInputEvent(&event);

  const bool defaultPrevented =
      eventStatus.mContentStatus == nsEventStatus_eConsumeNoDefault;

  if (!defaultPrevented && mDraggableRegion.Contains(refPoint)) {
    if (domButton == MouseButton::ePrimary) {
      mWindowShouldStartDragging = true;
    } else if (domButton == MouseButton::eMiddle &&
               StaticPrefs::widget_gtk_titlebar_action_middle_click_enabled()) {
      DoTitlebarAction(nsXPLookAndFeel::TitlebarEvent::Middle_Click, aEvent);
    }
  }

  // right menu click on linux should also pop up a context menu
  if (!StaticPrefs::ui_context_menus_after_mouseup() &&
      eventStatus.mApzStatus != nsEventStatus_eConsumeNoDefault) {
    DispatchContextMenuEventFromMouseEvent(domButton, aEvent, refPoint);
  }
}

void nsWindow::OnButtonReleaseEvent(GdkEventButton* aEvent) {
  LOG("Button %u release\n", aEvent->button);

  SetLastPointerDownEvent(nullptr);
  mLastMouseCoordinates.Set(aEvent);

  if (mAspectResizer) {
    mAspectResizer = Nothing();
    return;
  }

  if (mWindowShouldStartDragging) {
    mWindowShouldStartDragging = false;
  }

  bool isEraser;
  bool isPenEvent = IsPenEvent((GdkEvent*)aEvent, &isEraser);

  uint16_t domButton;
  switch (aEvent->button) {
    case 1:
      if (isEraser) {
        domButton = MouseButton::eEraser;
      } else {
        domButton = MouseButton::ePrimary;
      }
      break;
    case 2:
      domButton = MouseButton::eMiddle;
      break;
    case 3:
      domButton = MouseButton::eSecondary;
      break;
    default:
      return;
  }

  gButtonState &= ~ButtonMaskFromGDKButton(aEvent->button);

  const auto refPoint = GetRefPoint(this, aEvent);

  WidgetMouseEvent event(true, eMouseUp, this, WidgetMouseEvent::eReal);
  event.mButton = domButton;
  InitButtonEvent(event, aEvent, refPoint, isEraser);
  gdouble pressure = 0;
  gdk_event_get_axis((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
  event.mPressure = pressure ? (float)pressure : (float)mLastMotionPressure;

  // The mRefPoint is manipulated in DispatchInputEvent, we're saving it
  // to use it for the doubleclick position check.
  const LayoutDeviceIntPoint pos = event.mRefPoint;

  if (isPenEvent) {
    FetchAndAdjustPenData(event, (GdkEvent*)aEvent);
  }

  nsIWidget::ContentAndAPZEventStatus eventStatus = DispatchInputEvent(&event);

  const bool defaultPrevented =
      eventStatus.mContentStatus == nsEventStatus_eConsumeNoDefault;
  // Check if mouse position in titlebar and doubleclick happened to
  // trigger defined action.
  if (!defaultPrevented && mDrawInTitlebar &&
      event.mButton == MouseButton::ePrimary && event.mClickCount == 2 &&
      mDraggableRegion.Contains(pos)) {
    DoTitlebarAction(nsXPLookAndFeel::TitlebarEvent::Double_Click, aEvent);
  }
  mLastMotionPressure = pressure;

  // right menu click on linux should also pop up a context menu
  if (StaticPrefs::ui_context_menus_after_mouseup() &&
      eventStatus.mApzStatus != nsEventStatus_eConsumeNoDefault) {
    DispatchContextMenuEventFromMouseEvent(domButton, aEvent, refPoint);
  }

  // Open window manager menu on Media PiP window to allow user
  // to place it on top / all workspaces.
  if (mPiPType == PiPType::MediaPiP && aEvent->button == 3) {
    TryToShowNativeWindowMenu(aEvent);
  }
}

void nsWindow::OnContainerFocusInEvent(GdkEventFocus* aEvent) {
  LOG("OnContainerFocusInEvent");

  // Unset the urgency hint, if possible
  GtkWidget* top_window = GetGtkWidget();
  if (top_window && (gtk_widget_get_visible(top_window))) {
    SetUrgencyHint(top_window, false);
  }

  // Return if being called within SetFocus because the focus manager
  // already knows that the window is active.
  if (gBlockActivateEvent) {
    LOG("activated notification is blocked");
    return;
  }

  // If keyboard input will be accepted, the focus manager will call
  // SetFocus to set the correct window.
  gFocusWindow = nullptr;

  DispatchActivateEvent();

  if (!gFocusWindow) {
    // We don't really have a window for dispatching key events, but
    // setting a non-nullptr value here prevents OnButtonPressEvent() from
    // dispatching an activation notification if the widget is already
    // active.
    gFocusWindow = this;
  }

  LOG("Events sent from focus in event");
}

void nsWindow::OnContainerFocusOutEvent(GdkEventFocus* aEvent) {
  LOG("OnContainerFocusOutEvent");

  if (IsTopLevelWidget()) {
    // Rollup menus when a window is focused out unless a drag is occurring.
    // This check is because drags grab the keyboard and cause a focus out on
    // versions of GTK before 2.18.
    const bool shouldRollupMenus = [&] {
      nsCOMPtr<nsIDragService> dragService =
          do_GetService("@mozilla.org/widget/dragservice;1");
      nsCOMPtr<nsIDragSession> dragSession =
          dragService->GetCurrentSession(this);
      if (!dragSession) {
        return true;
      }
      // We also roll up when a drag is from a different application
      nsCOMPtr<nsINode> sourceNode;
      dragSession->GetSourceNode(getter_AddRefs(sourceNode));
      return !sourceNode;
    }();

    if (shouldRollupMenus) {
      RollupAllMenus();
    }

    if (RefPtr pm = nsXULPopupManager::GetInstance()) {
      pm->RollupTooltips();
    }
  }

  if (gFocusWindow) {
    RefPtr<nsWindow> kungFuDeathGrip = gFocusWindow;
    if (gFocusWindow->mIMContext) {
      gFocusWindow->mIMContext->OnBlurWindow(gFocusWindow);
    }
    gFocusWindow = nullptr;
  }

  DispatchDeactivateEvent();

  if (IsChromeWindowTitlebar()) {
    // DispatchDeactivateEvent() ultimately results in a call to
    // BrowsingContext::SetIsActiveBrowserWindow(), which resets
    // the state.  We call UpdateMozWindowActive() to keep it in
    // sync with GDK_WINDOW_STATE_FOCUSED.
    UpdateMozWindowActive();
  }

  LOG("Done with container focus out");
}

bool nsWindow::DispatchCommandEvent(nsAtom* aCommand) {
  WidgetCommandEvent appCommandEvent(true, aCommand, this);
  DispatchEvent(&appCommandEvent);
  return true;
}

bool nsWindow::DispatchContentCommandEvent(EventMessage aMsg) {
  WidgetContentCommandEvent event(true, aMsg, this);
  DispatchEvent(&event);
  return true;
}

WidgetEventTime nsWindow::GetWidgetEventTime(guint32 aEventTime) {
  return WidgetEventTime(GetEventTimeStamp(aEventTime));
}

TimeStamp nsWindow::GetEventTimeStamp(guint32 aEventTime) {
  if (MOZ_UNLIKELY(mIsDestroyed)) {
    // nsWindow has been Destroy()ed.
    return TimeStamp::Now();
  }
  if (aEventTime == 0) {
    // Some X11 and GDK events may be received with a time of 0 to indicate
    // that they are synthetic events. Some input method editors do this.
    // In this case too, just return the current timestamp.
    return TimeStamp::Now();
  }

  TimeStamp eventTimeStamp;

  if (GdkIsWaylandDisplay()) {
    // Wayland compositors use monotonic time to set timestamps.
    int64_t timestampTime = g_get_monotonic_time() / 1000;
    guint32 refTimeTruncated = guint32(timestampTime);

    timestampTime -= refTimeTruncated - aEventTime;
    int64_t tick =
        BaseTimeDurationPlatformUtils::TicksFromMilliseconds(timestampTime);
    eventTimeStamp = TimeStamp::FromSystemTime(tick);
  } else {
#ifdef MOZ_X11
    CurrentX11TimeGetter* getCurrentTime = GetCurrentTimeGetter();
    MOZ_ASSERT(getCurrentTime,
               "Null current time getter despite having a window");
    eventTimeStamp =
        TimeConverter().GetTimeStampFromSystemTime(aEventTime, *getCurrentTime);
#endif
  }
  return eventTimeStamp;
}

#ifdef MOZ_X11
mozilla::CurrentX11TimeGetter* nsWindow::GetCurrentTimeGetter() {
  if (MOZ_UNLIKELY(!mCurrentTimeGetter)) {
    mCurrentTimeGetter = MakeUnique<CurrentX11TimeGetter>(mGdkWindow);
  }
  return mCurrentTimeGetter.get();
}
#endif

gboolean nsWindow::OnKeyPressEvent(GdkEventKey* aEvent) {
  LOG("OnKeyPressEvent");

  KeymapWrapper::HandleKeyPressEvent(this, aEvent);
  return TRUE;
}

gboolean nsWindow::OnKeyReleaseEvent(GdkEventKey* aEvent) {
  LOG("OnKeyReleaseEvent");
  if (NS_WARN_IF(!KeymapWrapper::HandleKeyReleaseEvent(this, aEvent))) {
    return FALSE;
  }
  return TRUE;
}

void nsWindow::OnScrollEvent(GdkEventScroll* aEvent) {
  LOG("OnScrollEvent time %d", aEvent->time);

  mLastMouseCoordinates.Set(aEvent);

  // This event was already handled by OnSmoothScrollEvent().
  if (aEvent->time != GDK_CURRENT_TIME &&
      mLastSmoothScrollEventTime == aEvent->time) {
    return;
  }

  // check to see if we should rollup
  if (CheckForRollup(aEvent->x_root, aEvent->y_root, true, false)) {
    return;
  }

  // check for duplicate legacy scroll event, see GNOME bug 726878
  if (aEvent->direction != GDK_SCROLL_SMOOTH &&
      mLastScrollEventTime == aEvent->time) {
    LOG("[%d] duplicate legacy scroll event %d\n", aEvent->time,
        aEvent->direction);
    return;
  }
  WidgetWheelEvent wheelEvent(true, eWheel, this);
  wheelEvent.mDeltaMode = dom::WheelEvent_Binding::DOM_DELTA_LINE;
  switch (aEvent->direction) {
    case GDK_SCROLL_SMOOTH: {
      // As of GTK 3.4, all directional scroll events are provided by
      // the GDK_SCROLL_SMOOTH direction on XInput2 and Wayland devices.
      mLastScrollEventTime = aEvent->time;

      // Special handling for touchpads to support flings
      // (also known as kinetic/inertial/momentum scrolling)
      GdkDevice* device = gdk_event_get_source_device((GdkEvent*)aEvent);
      GdkInputSource source = gdk_device_get_source(device);
      if (source == GDK_SOURCE_TOUCHSCREEN || source == GDK_SOURCE_TOUCHPAD ||
          mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase.isSome()) {
        if (StaticPrefs::apz_gtk_pangesture_enabled() &&
            gtk_check_version(3, 20, 0) == nullptr) {
          static auto sGdkEventIsScrollStopEvent =
              (gboolean (*)(const GdkEvent*))dlsym(
                  RTLD_DEFAULT, "gdk_event_is_scroll_stop_event");

          LOG("[%d] pan smooth event dx=%.2f dy=%.2f inprogress=%d\n",
              aEvent->time, aEvent->delta_x, aEvent->delta_y, mPanInProgress);
          auto eventType = PanGestureInput::PANGESTURE_PAN;
          if (sGdkEventIsScrollStopEvent((GdkEvent*)aEvent)) {
            eventType = PanGestureInput::PANGESTURE_END;
            mPanInProgress = false;
          } else if (!mPanInProgress) {
            eventType = PanGestureInput::PANGESTURE_START;
            mPanInProgress = true;
          } else if (mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase
                         .isSome()) {
            switch (*mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase) {
              case PHASE_BEGIN:
                // we should never hit this because we'll hit the above case
                // before this.
                MOZ_ASSERT_UNREACHABLE();
                eventType = PanGestureInput::PANGESTURE_START;
                mPanInProgress = true;
                break;
              case PHASE_UPDATE:
                // nothing to do here, eventtype should already be set
                MOZ_ASSERT(mPanInProgress);
                MOZ_ASSERT(eventType == PanGestureInput::PANGESTURE_PAN);
                eventType = PanGestureInput::PANGESTURE_PAN;
                break;
              case PHASE_END:
                MOZ_ASSERT(mPanInProgress);
                eventType = PanGestureInput::PANGESTURE_END;
                mPanInProgress = false;
                break;
              default:
                MOZ_ASSERT_UNREACHABLE();
                break;
            }
          }

          mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase.reset();

          const bool isPageMode =
              StaticPrefs::apz_gtk_pangesture_delta_mode() == 1;
          const double multiplier =
              isPageMode
                  ? StaticPrefs::apz_gtk_pangesture_page_delta_mode_multiplier()
                  : StaticPrefs::
                            apz_gtk_pangesture_pixel_delta_mode_multiplier() *
                        FractionalScaleFactor();

          ScreenPoint deltas(float(aEvent->delta_x * multiplier),
                             float(aEvent->delta_y * multiplier));

          LayoutDeviceIntPoint touchPoint = GetRefPoint(this, aEvent);
          PanGestureInput panEvent(
              eventType, GetEventTimeStamp(aEvent->time),
              ScreenPoint(touchPoint.x, touchPoint.y), deltas,
              KeymapWrapper::ComputeKeyModifiers(aEvent->state));
          panEvent.mDeltaType = isPageMode ? PanGestureInput::PANDELTA_PAGE
                                           : PanGestureInput::PANDELTA_PIXEL;
          panEvent.mSimulateMomentum =
              StaticPrefs::apz_gtk_kinetic_scroll_enabled();

          DispatchPanGesture(panEvent);

          if (mCurrentSynthesizedTouchpadPan.mSavedCallbackId.isSome()) {
            mozilla::widget::AutoSynthesizedEventCallbackNotifier::
                NotifySavedCallback(
                    mCurrentSynthesizedTouchpadPan.mSavedCallbackId.ref());
            mCurrentSynthesizedTouchpadPan.mSavedCallbackId.reset();
          }

          return;
        }

        // Older GTK doesn't support stop events, so we can't support fling
        wheelEvent.mScrollType = WidgetWheelEvent::SCROLL_ASYNCHRONOUSLY;
      }

      // TODO - use a more appropriate scrolling unit than lines.
      // Multiply event deltas by 3 to emulate legacy behaviour.
      wheelEvent.mDeltaX = aEvent->delta_x * 3;
      wheelEvent.mDeltaY = aEvent->delta_y * 3;
      wheelEvent.mWheelTicksX = aEvent->delta_x;
      wheelEvent.mWheelTicksY = aEvent->delta_y;
      wheelEvent.mIsNoLineOrPageDelta = true;

      break;
    }
    case GDK_SCROLL_UP:
      wheelEvent.mDeltaY = wheelEvent.mLineOrPageDeltaY = -3;
      wheelEvent.mWheelTicksY = -1;
      break;
    case GDK_SCROLL_DOWN:
      wheelEvent.mDeltaY = wheelEvent.mLineOrPageDeltaY = 3;
      wheelEvent.mWheelTicksY = 1;
      break;
    case GDK_SCROLL_LEFT:
      wheelEvent.mDeltaX = wheelEvent.mLineOrPageDeltaX = -1;
      wheelEvent.mWheelTicksX = -1;
      break;
    case GDK_SCROLL_RIGHT:
      wheelEvent.mDeltaX = wheelEvent.mLineOrPageDeltaX = 1;
      wheelEvent.mWheelTicksX = 1;
      break;
  }

  wheelEvent.mRefPoint = GetRefPoint(this, aEvent);

  KeymapWrapper::InitInputEvent(wheelEvent, aEvent->state);

  wheelEvent.AssignEventTime(GetWidgetEventTime(aEvent->time));

  DispatchInputEvent(&wheelEvent);
}

void nsWindow::OnSmoothScrollEvent(uint32_t aTime, float aDeltaX,
                                   float aDeltaY) {
  LOG("OnSmoothScrollEvent time %d dX %.2f dY %.2f", aTime, aDeltaX, aDeltaY);

  // This event was already handled by OnSmoothScrollEvent().
  mLastSmoothScrollEventTime = aTime;

  if (CheckForRollup(mLastMouseCoordinates.mRootX, mLastMouseCoordinates.mRootY,
                     true, false)) {
    return;
  }

  WidgetWheelEvent wheelEvent(true, eWheel, this);
  wheelEvent.mDeltaMode = dom::WheelEvent_Binding::DOM_DELTA_LINE;
  // Use the same constant as nsWindow::OnScrollEvent().
  wheelEvent.mDeltaX = aDeltaX * 3;
  wheelEvent.mDeltaY = aDeltaY * 3;
  wheelEvent.mWheelTicksX = aDeltaX;
  wheelEvent.mWheelTicksY = aDeltaY;
  wheelEvent.mIsNoLineOrPageDelta = true;
  wheelEvent.mRefPoint = GdkEventCoordsToDevicePixels(mLastMouseCoordinates.mX,
                                                      mLastMouseCoordinates.mY);

  KeymapWrapper::InitInputEvent(wheelEvent,
                                KeymapWrapper::GetCurrentModifierState());
  wheelEvent.AssignEventTime(GetWidgetEventTime(aTime));
  DispatchInputEvent(&wheelEvent);
}

void nsWindow::DispatchPanGesture(PanGestureInput& aPanInput) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mSwipeTracker) {
    // Give the swipe tracker a first pass at the event. If a new pan gesture
    // has been started since the beginning of the swipe, the swipe tracker
    // will know to ignore the event.
    nsEventStatus status = mSwipeTracker->ProcessEvent(aPanInput);
    if (status == nsEventStatus_eConsumeNoDefault) {
      return;
    }
  }

  APZEventResult result;
  if (mAPZC) {
    MOZ_ASSERT(APZThreadUtils::IsControllerThread());

    result = mAPZC->InputBridge()->ReceiveInputEvent(aPanInput);
    if (result.GetStatus() == nsEventStatus_eConsumeNoDefault) {
      return;
    }
  }

  WidgetWheelEvent event = aPanInput.ToWidgetEvent(this);
  if (!mAPZC) {
    if (MayStartSwipeForNonAPZ(aPanInput)) {
      return;
    }
  } else {
    event = MayStartSwipeForAPZ(aPanInput, result);
  }

  ProcessUntransformedAPZEvent(&event, result);
}

void nsWindow::OnVisibilityNotifyEvent(GdkVisibilityState aState) {
  LOG("nsWindow::OnVisibilityNotifyEvent [%p] state 0x%x\n", this, aState);
  auto state = aState == GDK_VISIBILITY_FULLY_OBSCURED
                   ? OcclusionState::OCCLUDED
                   : OcclusionState::UNKNOWN;
  NotifyOcclusionState(state);
}

void nsWindow::OnWindowStateEvent(GtkWidget* aWidget,
                                  GdkEventWindowState* aEvent) {
  LOG("nsWindow::OnWindowStateEvent for %p changed 0x%x new_window_state "
      "0x%x\n",
      aWidget, aEvent->changed_mask, aEvent->new_window_state);

  if (IS_MOZ_CONTAINER(aWidget)) {
    // This event is notifying the container widget of changes to the
    // toplevel window.  Just detect changes affecting whether windows are
    // viewable.
    //
    // (A visibility notify event is sent to each window that becomes
    // viewable when the toplevel is mapped, but we can't rely on that for
    // setting mHasMappedToplevel because these toplevel window state
    // events are asynchronous.  The windows in the hierarchy now may not
    // be the same windows as when the toplevel was mapped, so they may
    // not get VisibilityNotify events.)
    bool mapped = !(aEvent->new_window_state &
                    (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_WITHDRAWN));
    SetHasMappedToplevel(mapped);
    LOG("\tquick return because IS_MOZ_CONTAINER(aWidget) is true\n");
    return;
  }
  // else the widget is a shell widget.

  // The block below is a bit evil.
  //
  // When a window is resized before it is shown, gtk_window_resize() delays
  // resizes until the window is shown.  If gtk_window_state_event() sees a
  // GDK_WINDOW_STATE_MAXIMIZED change [1] before the window is shown, then
  // gtk_window_compute_configure_request_size() ignores the values from the
  // resize [2].  See bug 1449166 for an example of how this could happen.
  //
  // [1] https://gitlab.gnome.org/GNOME/gtk/blob/3.22.30/gtk/gtkwindow.c#L7967
  // [2] https://gitlab.gnome.org/GNOME/gtk/blob/3.22.30/gtk/gtkwindow.c#L9377
  //
  // In order to provide a sensible size for the window when the user exits
  // maximized state, we hide the GDK_WINDOW_STATE_MAXIMIZED change from
  // gtk_window_state_event() so as to trick GTK into using the values from
  // gtk_window_resize() in its configure request.
  //
  // We instead notify gtk_window_state_event() of the maximized state change
  // once the window is shown.
  //
  // See https://gitlab.gnome.org/GNOME/gtk/issues/1044
  //
  // This may be fixed in Gtk 3.24+ but it's still live and kicking
  // (Bug 1791779).
  if (!mIsShown) {
    aEvent->changed_mask = static_cast<GdkWindowState>(
        aEvent->changed_mask & ~GDK_WINDOW_STATE_MAXIMIZED);
  } else if (aEvent->changed_mask & GDK_WINDOW_STATE_WITHDRAWN &&
             aEvent->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) {
    aEvent->changed_mask = static_cast<GdkWindowState>(
        aEvent->changed_mask | GDK_WINDOW_STATE_MAXIMIZED);
  }

  // This is a workaround for https://gitlab.gnome.org/GNOME/gtk/issues/1395
  // Gtk+ controls window active appearance by window-state-event signal.
  if (IsChromeWindowTitlebar() &&
      (aEvent->changed_mask & GDK_WINDOW_STATE_FOCUSED)) {
    // Emulate what Gtk+ does at gtk_window_state_event().
    // We can't check GTK_STATE_FLAG_BACKDROP directly as it's set by Gtk+
    // *after* this window-state-event handler.
    mTitlebarBackdropState =
        !(aEvent->new_window_state & GDK_WINDOW_STATE_FOCUSED);

    // keep IsActiveBrowserWindow in sync with GDK_WINDOW_STATE_FOCUSED
    UpdateMozWindowActive();

    ForceTitlebarRedraw();
  }

  // We don't care about anything but changes in the
  // maximized/icon/fullscreen/tiled/resizable states.
  constexpr auto kInterestingStates =
      GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED |
      GDK_WINDOW_STATE_FULLSCREEN | kTiledStates | kResizableStates;

  // states. Note that Wayland never gets iconified, see:
  // https://gitlab.gnome.org/GNOME/gtk/issues/67
  if (!(aEvent->changed_mask & kInterestingStates)) {
    LOG("\tearly return because no interesting bits changed\n");
    return;
  }

  auto oldSizeMode = mSizeMode;
  if (aEvent->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
    LOG("\tIconified\n");
    mSizeMode = nsSizeMode_Minimized;
#ifdef ACCESSIBILITY
    DispatchMinimizeEventAccessible();
#endif  // ACCESSIBILITY
  } else if (aEvent->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
    LOG("\tFullscreen\n");
    mSizeMode = nsSizeMode_Fullscreen;
  } else if (aEvent->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) {
    LOG("\tMaximized\n");
    mSizeMode = nsSizeMode_Maximized;
#ifdef ACCESSIBILITY
    DispatchMaximizeEventAccessible();
#endif  // ACCESSIBILITY
  } else {
    LOG("\tNormal\n");
    mSizeMode = nsSizeMode_Normal;
#ifdef ACCESSIBILITY
    DispatchRestoreEventAccessible();
#endif  // ACCESSIBILITY
  }

  SetIsTiled(aEvent->new_window_state & GDK_WINDOW_STATE_TILED);
  LOG("\tTiled: %d\n", int(mIsTiled));
  mResizableEdges = [&] {
    Sides result;
    if (mSizeMode != nsSizeMode_Normal) {
      return result;
    }
    // Assume that if we have per-side tiling info, we have per-side resizing
    // info. Otherwise assume resizability of the whole window.
    const bool hasPerSideInfo = aEvent->new_window_state & kPerSideTiledStates;
    if (!hasPerSideInfo ||
        aEvent->new_window_state & GDK_WINDOW_STATE_TOP_RESIZABLE) {
      result |= SideBits::eTop;
    }
    if (!hasPerSideInfo ||
        aEvent->new_window_state & GDK_WINDOW_STATE_LEFT_RESIZABLE) {
      result |= SideBits::eLeft;
    }
    if (!hasPerSideInfo ||
        aEvent->new_window_state & GDK_WINDOW_STATE_RIGHT_RESIZABLE) {
      result |= SideBits::eRight;
    }
    if (!hasPerSideInfo ||
        aEvent->new_window_state & GDK_WINDOW_STATE_BOTTOM_RESIZABLE) {
      result |= SideBits::eBottom;
    }
    return result;
  }();

  if (mSizeMode != oldSizeMode) {
    const bool fullscreenChanging = mSizeMode == nsSizeMode_Fullscreen ||
                                    oldSizeMode == nsSizeMode_Fullscreen;
    if (fullscreenChanging) {
      // As a special-case when going in / out of fullscreen mode we recompute
      // bounds synchronously. This avoids spurious resizes when going into
      // fullscreen mode if the relevant configure hasn't happened yet or what
      // not.
      RecomputeBounds();
    }
    if (mWidgetListener) {
      mWidgetListener->SizeModeChanged(mSizeMode);
    }
    if (fullscreenChanging) {
      if (mCompositorWidgetDelegate) {
        mCompositorWidgetDelegate->NotifyFullscreenChanged(
            mSizeMode == nsSizeMode_Fullscreen);
      }
    }
  }
}

void nsWindow::OnDPIChanged() {
  // Update menu's font size etc.
  // This affects style / layout because it affects system font sizes.
  if (PresShell* presShell = GetPresShell()) {
    presShell->BackingScaleFactorChanged();
  }
  NotifyAPZOfDPIChange();
}

void nsWindow::OnCheckResize() { mPendingConfigures++; }

void nsWindow::OnCompositedChanged() {
  // Update CSD after the change in alpha visibility. This only affects
  // system metrics, not other theme shenanigans.
  NotifyThemeChanged(ThemeChangeKind::MediaQueriesOnly);
  mCompositedScreen = gdk_screen_is_composited(gdk_screen_get_default());
}

// X11(XWayland) and Wayland handles screen scale differently.
// If there are more monitors with different scale factor (say 2 and 3),
// XWayland sends scale 3 to all application windows and downscales
// applications on monitor with scale 2.
// If scale is changed system wide in settings, OnScaleEvent() is send
// to all application windows.
//
// Wayland sends actual scale to each window according to its position
// and also sends OnScaleEvent is scale changes for particular window.
// So we can have toplevel window with scale 3 and its child popup with scale 2
// (because toplevel it's located on more than one screen).
//
// But right now gecko code (or widget/gtk?) expects that toplevel and its
// popup use the same scale factor (which may be actually different).
// We see various rendering/sizing/position errors otherwise,
// maybe we get scale from wrong windows or so.
//
// Let's follow the working scenario for now to avoid complexity
// and maybe fix that later.
void nsWindow::OnScaleEvent() {
  if (!IsTopLevelWidget()) {
    return;
  }

  LOG("nsWindow::OnScaleEvent() GdkWindow scale %d",
      gdk_window_get_scale_factor(mGdkWindow));

  RefreshScale(/* aRefreshScreen */ true);
}

void nsWindow::RefreshScale(bool aRefreshScreen, bool aForceRefresh) {
  if (!IsTopLevelWidget()) {
    return;
  }

  LOG("nsWindow::RefreshScale() GdkWindow scale %d refresh %d",
      gdk_window_get_scale_factor(mGdkWindow), aRefreshScreen);

  int ceiledScale = gdk_window_get_scale_factor(mGdkWindow);
  const bool scaleChanged =
      aForceRefresh || GdkCeiledScaleFactor() != ceiledScale;
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    WaylandSurfaceLock lock(mSurface);
    mSurface->SetCeiledScaleLocked(lock, ceiledScale);
  }
#endif
  mCeiledScaleFactor = ceiledScale;

  if (!scaleChanged) {
    return;
  }

  NotifyAPZOfDPIChange();

  if (!aRefreshScreen) {
    return;
  }

  RecomputeBounds(/* ScaleChanged*/ true);

  if (PresShell* presShell = GetPresShell()) {
    presShell->BackingScaleFactorChanged();
  }

  if (mCursor.IsCustom()) {
    mUpdateCursor = true;
    SetCursor(Cursor{mCursor});
  }
}

void nsWindow::SetDragPopupSurface(
    RefPtr<gfxImageSurface> aDragPopupSurface,
    const LayoutDeviceIntRegion& aInvalidRegion) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  if (!mIsMapped) {
    return;
  }

  mDragPopupSurface = aDragPopupSurface;
  mDragPopupSurfaceRegion = aInvalidRegion;
  if (!mIsDestroyed) {
    gdk_window_invalidate_rect(mGdkWindow, nullptr, false);
  }
}

bool nsWindow::DrawDragPopupSurface(cairo_t* cr) {
  if (!mDragPopupSurface) {
    return false;
  }
  RefPtr<gfxImageSurface> surface = std::move(mDragPopupSurface);

  gfx::IntRect bounds = mDragPopupSurfaceRegion.GetBounds().ToUnknownRect();
  if (bounds.IsEmpty()) {
    return true;
  }

  cairo_surface_t* targetSurface = cairo_get_group_target(cr);
  gfx::IntSize size(bounds.XMost(), bounds.YMost());
  RefPtr<gfx::DrawTarget> dt =
      gfx::Factory::CreateDrawTargetForCairoSurface(targetSurface, size);

  RefPtr<gfx::SourceSurface> surf =
      gfx::Factory::CreateSourceSurfaceForCairoSurface(
          surface->CairoSurface(), surface->GetSize(), surface->Format());
  if (!dt || !surf) {
    return true;
  }

  static auto sCairoSurfaceSetDeviceScalePtr =
      (void (*)(cairo_surface_t*, double, double))dlsym(
          RTLD_DEFAULT, "cairo_surface_set_device_scale");

  if (sCairoSurfaceSetDeviceScalePtr) {
    double scale = FractionalScaleFactor();
    sCairoSurfaceSetDeviceScalePtr(surface->CairoSurface(), scale, scale);
  }

  uint32_t numRects = mDragPopupSurfaceRegion.GetNumRects();
  if (numRects == 1) {
    dt->CopySurface(surf, bounds, bounds.TopLeft());
  } else {
    AutoTArray<IntRect, 32> rects;
    rects.SetCapacity(numRects);
    for (auto iter = mDragPopupSurfaceRegion.RectIter(); !iter.Done();
         iter.Next()) {
      rects.AppendElement(iter.Get().ToUnknownRect());
    }
    dt->PushDeviceSpaceClipRects(rects.Elements(), rects.Length());

    dt->DrawSurface(surf, gfx::Rect(bounds), gfx::Rect(bounds),
                    DrawSurfaceOptions(),
                    DrawOptions(1.0f, CompositionOp::OP_SOURCE));

    dt->PopClip();
  }

  return true;
}

void nsWindow::DispatchDragEvent(EventMessage aMsg,
                                 const LayoutDeviceIntPoint& aRefPoint,
                                 guint aTime) {
  LOGDRAG("nsWindow::DispatchDragEvent %s", ToChar(aMsg));
  WidgetDragEvent event(true, aMsg, this);

  InitDragEvent(event);

  event.mRefPoint = aRefPoint;
  event.AssignEventTime(GetWidgetEventTime(aTime));

  DispatchInputEvent(&event);
}

nsWindow* nsWindow::GetTransientForWindowIfPopup() {
  if (mWindowType != WindowType::Popup) {
    return nullptr;
  }
  GtkWindow* toplevel = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (toplevel) {
    return nsWindow::FromGtkWidget(GTK_WIDGET(toplevel));
  }
  return nullptr;
}

bool nsWindow::IsHandlingTouchSequence(GdkEventSequence* aSequence) {
  return mHandleTouchEvent && mTouches.Contains(aSequence);
}

gboolean nsWindow::OnTouchpadPinchEvent(GdkEventTouchpadPinch* aEvent) {
  if (!StaticPrefs::apz_gtk_touchpad_pinch_enabled()) {
    return TRUE;
  }
  // Do not respond to pinch gestures involving more than two fingers
  // unless specifically preffed on. These are sometimes hooked up to other
  // actions at the desktop environment level and having the browser also
  // pinch can be undesirable.
  if (aEvent->n_fingers > 2 &&
      !StaticPrefs::apz_gtk_touchpad_pinch_three_fingers_enabled()) {
    return FALSE;
  }
  auto pinchGestureType = PinchGestureInput::PINCHGESTURE_SCALE;
  ScreenCoord currentSpan;
  ScreenCoord previousSpan;

  switch (aEvent->phase) {
    case GDK_TOUCHPAD_GESTURE_PHASE_BEGIN:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_START;
      currentSpan = aEvent->scale;
      mCurrentTouchpadFocus = ViewAs<ScreenPixel>(
          GetRefPoint(this, aEvent),
          PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

      // Assign PreviousSpan --> 0.999 to make mDeltaY field of the
      // WidgetWheelEvent that this PinchGestureInput event will be converted
      // to not equal Zero as our discussion because we observed that the
      // scale of the PHASE_BEGIN event is 1.
      previousSpan = 0.999;
      break;

    case GDK_TOUCHPAD_GESTURE_PHASE_UPDATE:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_SCALE;
      mCurrentTouchpadFocus += ScreenPoint(aEvent->dx, aEvent->dy);
      if (aEvent->scale == mLastPinchEventSpan) {
        return FALSE;
      }
      currentSpan = aEvent->scale;
      previousSpan = mLastPinchEventSpan;
      break;

    case GDK_TOUCHPAD_GESTURE_PHASE_END:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_END;
      currentSpan = aEvent->scale;
      previousSpan = mLastPinchEventSpan;
      break;

    default:
      return FALSE;
  }

  PinchGestureInput event(
      pinchGestureType, PinchGestureInput::TRACKPAD,
      GetEventTimeStamp(aEvent->time), ExternalPoint(0, 0),
      mCurrentTouchpadFocus,
      100.0 * ((aEvent->phase == GDK_TOUCHPAD_GESTURE_PHASE_END)
                   ? ScreenCoord(1.f)
                   : currentSpan),
      100.0 * ((aEvent->phase == GDK_TOUCHPAD_GESTURE_PHASE_END)
                   ? ScreenCoord(1.f)
                   : previousSpan),
      KeymapWrapper::ComputeKeyModifiers(aEvent->state));

  if (!event.SetLineOrPageDeltaY(this)) {
    return FALSE;
  }

  mLastPinchEventSpan = aEvent->scale;
  DispatchPinchGestureInput(event);
  return TRUE;
}

void nsWindow::OnTouchpadHoldEvent(GdkTouchpadGesturePhase aPhase, guint aTime,
                                   uint32_t aFingers) {
  if (!StaticPrefs::apz_gtk_touchpad_hold_enabled()) {
    return;
  }
  LOG("OnTouchpadHoldEvent: aPhase %d aFingers %d", aPhase, aFingers);
  MOZ_ASSERT(aPhase !=
             GDK_TOUCHPAD_GESTURE_PHASE_UPDATE);  // not used for hold gestures
  PanGestureInput::PanGestureType eventType =
      (aPhase == GDK_TOUCHPAD_GESTURE_PHASE_BEGIN)
          ? PanGestureInput::PANGESTURE_MAYSTART
          : PanGestureInput::PANGESTURE_CANCELLED;
  ScreenPoint touchPoint = ViewAs<ScreenPixel>(
      GdkEventCoordsToDevicePixels(mLastMouseCoordinates.mX,
                                   mLastMouseCoordinates.mY),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);
  PanGestureInput panEvent(eventType, GetEventTimeStamp(aTime), touchPoint,
                           ScreenPoint(), 0);
  DispatchPanGesture(panEvent);
}

gboolean nsWindow::OnTouchEvent(GdkEventTouch* aEvent) {
  LOG("OnTouchEvent: x=%.2f y=%.2f type=%d\n", aEvent->x, aEvent->y,
      aEvent->type);
  if (!mHandleTouchEvent) {
    // If a popup window was spawned (e.g. as the result of a long-press)
    // and touch events got diverted to that window within a touch sequence,
    // ensure the touch event gets sent to the original window instead. We
    // keep the checks here very conservative so that we only redirect
    // events in this specific scenario.
    nsWindow* targetWindow = GetTransientForWindowIfPopup();
    if (targetWindow &&
        targetWindow->IsHandlingTouchSequence(aEvent->sequence)) {
      return targetWindow->OnTouchEvent(aEvent);
    }

    return FALSE;
  }

  EventMessage msg;
  switch (aEvent->type) {
    case GDK_TOUCH_BEGIN:
      SetLastPointerDownEvent((GdkEvent*)aEvent);
      // check to see if we should rollup
      if (CheckForRollup(aEvent->x_root, aEvent->y_root, false, false)) {
        return FALSE;
      }
      msg = eTouchStart;
      break;
    case GDK_TOUCH_UPDATE:
      msg = eTouchMove;
      // Start dragging when motion events happens in the dragging area
      if (mWindowShouldStartDragging &&
          is_drag_threshold_exceeded((GdkEvent*)aEvent)) {
        mWindowShouldStartDragging = false;
        if (auto* topLevel = GetToplevelGdkWindow()) {
          LOG("  start window dragging window\n");

          bool moved = false;
#ifdef MOZ_WAYLAND
          if (AsWayland()) {
            moved = AsWayland()->PIPMove();
          }
#endif
          if (!moved) {
            gdk_window_begin_move_drag(topLevel, 1, aEvent->x_root,
                                       aEvent->y_root, aEvent->time);
          }
          // Cancel the event sequence. gdk will steal all subsequent events
          // (including TOUCH_END).
          msg = eTouchCancel;
        }
      }
      break;
    case GDK_TOUCH_END:
      msg = eTouchEnd;
      SetLastPointerDownEvent(nullptr);
      if (mWindowShouldStartDragging) {
        LOG("  end of window dragging window\n");
        mWindowShouldStartDragging = false;
      }
      break;
    case GDK_TOUCH_CANCEL:
      msg = eTouchCancel;
      SetLastPointerDownEvent(nullptr);
      break;
    default:
      return FALSE;
  }

  const LayoutDeviceIntPoint touchPoint = GetRefPoint(this, aEvent);

  int32_t id;
  RefPtr<dom::Touch> touch;
  if (mTouches.Remove(aEvent->sequence, getter_AddRefs(touch))) {
    id = touch->mIdentifier;
  } else {
    id = ++gLastTouchID & 0x7FFFFFFF;
  }

  touch =
      new dom::Touch(id, touchPoint, LayoutDeviceIntPoint(1, 1), 0.0f, 0.0f);

  WidgetTouchEvent event(true, msg, this);
  KeymapWrapper::InitInputEvent(event, aEvent->state);

  if (msg == eTouchStart || msg == eTouchMove) {
    mTouches.InsertOrUpdate(aEvent->sequence, std::move(touch));
    // add all touch points to event object
    for (const auto& data : mTouches.Values()) {
      event.mTouches.AppendElement(new dom::Touch(*data));
    }
  } else if (msg == eTouchEnd || msg == eTouchCancel) {
    *event.mTouches.AppendElement() = std::move(touch);
  }

  nsIWidget::ContentAndAPZEventStatus eventStatus = DispatchInputEvent(&event);

  // There's a chance that we are in drag area and the event is not consumed
  // by something on it.
  if (msg == eTouchStart && mDraggableRegion.Contains(touchPoint) &&
      eventStatus.mApzStatus != nsEventStatus_eConsumeNoDefault) {
    mWindowShouldStartDragging = true;
  }
  return TRUE;
}

// Return true if toplevel window is transparent.
// It's transparent when we're running on composited screens
// and we can draw main window without system titlebar.
bool nsWindow::IsToplevelWindowTransparent() {
  static bool transparencyConfigured = false;

  if (!transparencyConfigured) {
    if (gdk_screen_is_composited(gdk_screen_get_default())) {
      // Some Gtk+ themes use non-rectangular toplevel windows. To fully
      // support such themes we need to make toplevel window transparent
      // with ARGB visual.
      // It may cause performanance issue so make it configurable
      // and enable it by default for selected window managers.
      if (Preferences::HasUserValue("mozilla.widget.use-argb-visuals")) {
        // argb visual is explicitly required so use it
        sTransparentMainWindow =
            Preferences::GetBool("mozilla.widget.use-argb-visuals");
      } else {
        // Enable transparent toplevel window if we can draw main window
        // without system titlebar as Gtk+ themes use titlebar round corners.
        sTransparentMainWindow =
            GetSystemGtkWindowDecoration() != GTK_DECORATION_NONE;
      }
    }
    transparencyConfigured = true;
  }

  return sTransparentMainWindow;
}

nsAutoCString nsWindow::GetFrameTag() const {
  if (nsIFrame* frame = GetPopupFrame()) {
#ifdef DEBUG_FRAME_DUMP
    return frame->ListTag();
#else
    nsAutoCString buf;
    buf.AppendPrintf("Frame(%p)", frame);
    if (nsIContent* content = frame->GetContent()) {
      buf.Append(' ');
      AppendUTF16toUTF8(content->NodeName(), buf);
    }
    return buf;
#endif
  }
  return nsAutoCString("(no frame)");
}

nsCString nsWindow::GetPopupTypeName() {
  switch (mPopupType) {
    case PopupType::Menu:
      return nsCString("Menu");
    case PopupType::Tooltip:
      return nsCString("Tooltip");
    case PopupType::Panel:
      return nsCString("Panel/Utility");
    default:
      return nsCString("Unknown");
  }
}

Window nsWindow::GetX11Window() {
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    return gdk_x11_window_get_xid(mGdkWindow);
  }
#endif
  return (Window) nullptr;
}

void nsWindow::SetGdkWindow(GdkWindow* aGdkWindow) {
  LOG("nsWindow::SetGdkWindow() %p", aGdkWindow);
  if (!aGdkWindow) {
    if (mGdkWindow) {
      g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", nullptr);
    }
    mGdkWindow = nullptr;
  } else {
    mGdkWindow = aGdkWindow;
    g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", this);
  }
}

void nsWindow::ConfigureToplevelWindow() {
  // Label mShell toplevel window so property_notify_event_cb callback
  // can find its way home.
  g_object_set_data(G_OBJECT(GetToplevelGdkWindow()), "nsWindow", this);
  g_object_set_data(G_OBJECT(mShell), "nsWindow", this);

  ConfigureToplevelWindowNative();
}

nsresult nsWindow::Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                          const widget::InitData& aInitData) {
  MOZ_DIAGNOSTIC_ASSERT(aInitData.mWindowType != WindowType::Invisible);
#ifdef ACCESSIBILITY
  // Send a DBus message to check whether a11y is enabled
  a11y::PreInit();
#endif

  // Ensure that the toolkit is created.
  nsGTKToolkit::GetToolkit();

  // initialize all the common bits of this class
  BaseCreate(aParent, aInitData);

  LOG("nsWindow::Create()");

  // save our bounds
  LOG("  mBounds: x:%d y:%d w:%d h:%d\n", aRect.x, aRect.y, aRect.width,
      aRect.height);

  mClientArea = ToDesktopPixels(aRect);
  ConstrainSizeWithScale(&mClientArea.width, &mClientArea.height,
                         GetDesktopToDeviceScale().scale);
  mLastSizeRequest = mClientArea.Size();
  mLastMoveRequest = mClientArea.TopLeft();

  const bool popupNeedsAlphaVisual =
      mWindowType == WindowType::Popup &&
      aInitData.mTransparencyMode == TransparencyMode::Transparent;

  // Figure out our parent window.
  auto* parentnsWindow = static_cast<nsWindow*>(aParent);
  LOG("  parent window [%p]", parentnsWindow);

  MOZ_ASSERT_IF(mWindowType == WindowType::Popup, parentnsWindow);
  if (mWindowType != WindowType::Dialog && mWindowType != WindowType::Popup &&
      mWindowType != WindowType::TopLevel) {
    MOZ_ASSERT_UNREACHABLE("Unexpected eWindowType");
    return NS_ERROR_FAILURE;
  }

  mAlwaysOnTop = aInitData.mAlwaysOnTop;
  mIsAlert = aInitData.mIsAlert;
  mIsDragPopup = aInitData.mIsDragPopup;

  // For popups, use the standard GtkWindowType GTK_WINDOW_POPUP,
  // which will use a Window with the override-redirect attribute
  // (for temporary windows).
  // For Wayland we have to always use GTK_WINDOW_POPUP to control
  // popup window position.
  GtkWindowType type = GTK_WINDOW_TOPLEVEL;
  if (mWindowType == WindowType::Popup) {
    type = GTK_WINDOW_POPUP;
  }
  mShell = gtk_window_new(type);

  // It is important that this happens before the realize() call below, so that
  // we don't get bogus CSD margins on Wayland, see bug 1794577.
  mUndecorated = IsAlwaysUndecoratedWindow();
  if (mUndecorated) {
    LOG("  Is undecorated Window\n");
    gtk_window_set_titlebar(GTK_WINDOW(mShell), gtk_fixed_new());
    gtk_window_set_decorated(GTK_WINDOW(mShell), false);
  }

  // Ensure gfxPlatform is initialized, since that is what initializes
  // gfxVars, used below.
  (void)gfxPlatform::GetPlatform();

  if (IsTopLevelWidget()) {
    mGtkWindowDecoration = GetSystemGtkWindowDecoration();
  }

  // Don't use transparency for Media PictureInPicture windows.
  bool toplevelNeedsAlphaVisual = false;
  if (mWindowType == WindowType::TopLevel && mPiPType != PiPType::MediaPiP) {
    toplevelNeedsAlphaVisual = IsToplevelWindowTransparent();
  }

  bool isGLVisualSet = false;
  mIsAccelerated = ComputeShouldAccelerate();
#ifdef MOZ_X11
  isGLVisualSet = mIsAccelerated && AsX11() && AsX11()->ConfigureX11GLVisual();
#endif
  if (!isGLVisualSet && (popupNeedsAlphaVisual || toplevelNeedsAlphaVisual)) {
    // We're running on composited screen so we can use alpha visual
    // for both toplevel and popups.
    if (mCompositedScreen) {
      GdkVisual* visual =
          gdk_screen_get_rgba_visual(gtk_widget_get_screen(mShell));
      if (visual) {
        gtk_widget_set_visual(mShell, visual);
        mHasAlphaVisual = true;
      }
    } else {
      // We can't really provide transparency...
      mIsTransparent = false;
    }
  }

  // We have a toplevel window with transparency.
  // Calls to UpdateTitlebarTransparencyBitmap() from OnExposeEvent()
  // occur before SetTransparencyMode() receives TransparencyMode::Transparent
  // from layout, so set mIsTransparent here.
  if (mWindowType == WindowType::TopLevel && mHasAlphaVisual) {
    mIsTransparent = true;
  }

  // We only move a general managed toplevel window if someone has
  // actually placed the window somewhere.  If no placement has taken
  // place, we just let the window manager Do The Right Thing.
  if (AreBoundsSane()) {
    LOG("  nsWindow::Create() Initial resize to %d x %d\n", mClientArea.width,
        mClientArea.height);
    gtk_window_resize(GTK_WINDOW(mShell), mClientArea.width,
                      mClientArea.height);
  }
  if (mPiPType == PiPType::MediaPiP) {
    LOG("  Is Media PiP window\n");
    gtk_window_set_type_hint(GTK_WINDOW(mShell), GDK_WINDOW_TYPE_HINT_UTILITY);
  } else if (mIsAlert) {
    LOG("  Is alert window\n");
    gtk_window_set_type_hint(GTK_WINDOW(mShell),
                             GDK_WINDOW_TYPE_HINT_NOTIFICATION);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mShell), TRUE);
  } else if (mWindowType == WindowType::Dialog) {
    mGtkWindowRoleName = "Dialog";

    SetDefaultIcon();
    gtk_window_set_type_hint(GTK_WINDOW(mShell), GDK_WINDOW_TYPE_HINT_DIALOG);
    LOG("  nsWindow::Create(): dialog");
    if (parentnsWindow) {
      GtkWindowSetTransientFor(GTK_WINDOW(mShell),
                               GTK_WINDOW(parentnsWindow->GetGtkWidget()));
      LOG("  set parent window [%p]\n", parentnsWindow);
    }
  } else if (mWindowType == WindowType::Popup) {
    mGtkWindowRoleName = "Popup";

    LOG("  nsWindow::Create() Popup");

    if (mIsDragPopup) {
      gtk_window_set_type_hint(GTK_WINDOW(mShell), GDK_WINDOW_TYPE_HINT_DND);
      LOG("  nsWindow::Create() Drag popup\n");
    } else if (GdkIsX11Display()) {
      // Set the window hints on X11 only. Wayland popups are configured
      // at WaylandPopupConfigure().
      GdkWindowTypeHint gtkTypeHint;
      switch (mPopupType) {
        case PopupType::Menu:
          gtkTypeHint = GDK_WINDOW_TYPE_HINT_POPUP_MENU;
          break;
        case PopupType::Tooltip:
          gtkTypeHint = GDK_WINDOW_TYPE_HINT_TOOLTIP;
          break;
        default:
          gtkTypeHint = GDK_WINDOW_TYPE_HINT_UTILITY;
          break;
      }
      gtk_window_set_type_hint(GTK_WINDOW(mShell), gtkTypeHint);
      LOG("  nsWindow::Create() popup type %s", GetPopupTypeName().get());
    }
    if (parentnsWindow) {
      LOG("  set parent window [%p] %s", parentnsWindow,
          parentnsWindow->mGtkWindowRoleName.get());
      GtkWindow* parentWidget = GTK_WINDOW(parentnsWindow->GetGtkWidget());
      GtkWindowSetTransientFor(GTK_WINDOW(mShell), parentWidget);

      // If popup parent is modal, we need to make popup modal too.
      if (mPopupType != PopupType::Tooltip &&
          gtk_window_get_modal(parentWidget)) {
        gtk_window_set_modal(GTK_WINDOW(mShell), true);
      }
    }

    // We need realized mShell at NativeMoveResize().
    gtk_widget_realize(mShell);

    // With popup windows, we want to set their position.
    // Place them immediately on X11 and save initial popup position
    // on Wayland as we place Wayland popup on show.
    if (GdkIsX11Display()) {
      NativeMoveResize(/* move */ true, /* resize */ false);
    }
  } else {  // must be WindowType::TopLevel
    mGtkWindowRoleName = "Toplevel";
    SetDefaultIcon();

    LOG("  nsWindow::Create() Toplevel\n");

    // each toplevel window gets its own window group
    GtkWindowGroup* group = gtk_window_group_new();
    gtk_window_group_add_window(group, GTK_WINDOW(mShell));
    g_object_unref(group);
  }

  if (mAlwaysOnTop) {
    gtk_window_set_keep_above(GTK_WINDOW(mShell), TRUE);
  }

  GtkWidget* container = nullptr;

#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    mSurface = new WaylandSurface();
    mSurface->Init();

    // Child surfaces get scale from parent window so we need it to
    // set it early.
    if (parentnsWindow) {
      WaylandSurfaceLock lock(mSurface);
      mSurface->SetParentLocked(
          lock, MOZ_WL_SURFACE(parentnsWindow->GetMozContainer()));
    }
  }
  container = moz_container_new(this, mSurface);
#else
  container = moz_container_new(this, nullptr);
#endif

  // Create a container to hold child windows and child GtkWidgets.
  mContainer = MOZ_CONTAINER(container);
  g_object_set_data(G_OBJECT(mContainer), "nsWindow", this);

  // Prevent GtkWindow from painting a background to avoid flickering.
  gtk_widget_set_app_paintable(
      GTK_WIDGET(mContainer),
      StaticPrefs::widget_transparent_windows_AtStartup());

  gtk_widget_add_events(GTK_WIDGET(mContainer), kEvents);
  gtk_widget_add_events(mShell, GDK_PROPERTY_CHANGE_MASK);
  gtk_widget_set_app_paintable(
      mShell, StaticPrefs::widget_transparent_windows_AtStartup());

  // If we draw to mContainer window then configure it now because
  // gtk_container_add() realizes the child widget.
  gtk_widget_set_has_window(container, true);
  gtk_container_add(GTK_CONTAINER(mShell), container);

  // alwaysontop windows are generally used for peripheral indicators,
  // so we don't focus them by default.
  const bool shouldFocus = !mAlwaysOnTop || mPiPType == PiPType::DocumentPiP;
  if (!shouldFocus) {
    gtk_window_set_focus_on_map(GTK_WINDOW(mShell), FALSE);
  }

  gtk_widget_realize(container);
  // mGdkWindow is set by moz_container_realize() / SetGdkWindow().
  MOZ_DIAGNOSTIC_ASSERT(mGdkWindow, "MozContainer realize failed?");

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    mEGLWindow = (void*)GDK_WINDOW_XID(mGdkWindow);
  }
#endif
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay() && mIsAccelerated) {
    mEGLWindow = mSurface->GetEGLWindow(mClientArea.Size());
  }
#endif
  if (mEGLWindow) {
    LOG("Get NS_NATIVE_EGL_WINDOW mGdkWindow %p returned mEGLWindow %p",
        mGdkWindow, mEGLWindow);
  }

  CreateNative();
  ConfigureToplevelWindow();

  // make sure this is the focus widget in the container
  gtk_widget_show(container);

  if (shouldFocus) {
    gtk_widget_grab_focus(container);
  }

  if (mWindowType == WindowType::TopLevel && gKioskMode) {
    if (gKioskMonitor != -1) {
      mKioskMonitor = Some(gKioskMonitor);
      LOG("  set kiosk mode monitor %d", mKioskMonitor.value());
    } else {
      LOG("  set kiosk mode");
    }
    // Kiosk mode always use fullscreen.
    MakeFullScreen(/* aFullScreen */ true);
  }

  if (mWindowType == WindowType::Popup) {
    // gdk does not automatically set the cursor for "temporary"
    // windows, which are what gtk uses for popups.

    // force SetCursor to actually set the cursor, even though our internal
    // state indicates that we already have the standard cursor.
    mUpdateCursor = true;
    SetCursor(Cursor{eCursor_standard});
  }

  if (GdkIsX11Display()
#ifdef MOZ_WAYLAND
      || !StaticPrefs::widget_wayland_native_data_session_AtStartup()
#endif
  ) {
    gtk_drag_dest_set((GtkWidget*)mShell, (GtkDestDefaults)0, nullptr, 0,
                      (GdkDragAction)0);
    g_signal_connect(mShell, "drag_motion", G_CALLBACK(drag_motion_event_cb),
                     nullptr);
    g_signal_connect(mShell, "drag_leave", G_CALLBACK(drag_leave_event_cb),
                     nullptr);
    g_signal_connect(mShell, "drag_drop", G_CALLBACK(drag_drop_event_cb),
                     nullptr);
    g_signal_connect(mShell, "drag_data_received",
                     G_CALLBACK(drag_data_received_event_cb), nullptr);
  }

  // attach listeners for events
  g_signal_connect(mShell, "configure_event",
                   G_CALLBACK(shell_configure_event_cb), nullptr);
  g_signal_connect(mShell, "delete_event", G_CALLBACK(delete_event_cb),
                   nullptr);
  g_signal_connect(mShell, "window_state_event",
                   G_CALLBACK(window_state_event_cb), nullptr);
  g_signal_connect(mShell, "visibility-notify-event",
                   G_CALLBACK(visibility_notify_event_cb), nullptr);
  g_signal_connect(mShell, "check-resize", G_CALLBACK(check_resize_cb),
                   nullptr);
  g_signal_connect(mShell, "composited-changed",
                   G_CALLBACK(widget_composited_changed_cb), nullptr);
  g_signal_connect(mShell, "property-notify-event",
                   G_CALLBACK(property_notify_event_cb), nullptr);

  if (mWindowType == WindowType::TopLevel) {
    g_signal_connect_after(mShell, "size_allocate",
                           G_CALLBACK(toplevel_window_size_allocate_cb),
                           nullptr);
  }

  GdkScreen* screen = gtk_widget_get_screen(mShell);
  if (!g_signal_handler_find(screen, G_SIGNAL_MATCH_FUNC, 0, 0, nullptr,
                             FuncToGpointer(screen_composited_changed_cb),
                             nullptr)) {
    g_signal_connect(screen, "composited-changed",
                     G_CALLBACK(screen_composited_changed_cb), nullptr);
  }

  GtkSettings* default_settings = gtk_settings_get_default();
  g_signal_connect_after(default_settings, "notify::gtk-xft-dpi",
                         G_CALLBACK(settings_xft_dpi_changed_cb), this);

  // Widget signals
  g_signal_connect_after(mContainer, "size_allocate",
                         G_CALLBACK(size_allocate_cb), nullptr);
  g_signal_connect(mContainer, "hierarchy-changed",
                   G_CALLBACK(hierarchy_changed_cb), nullptr);
  g_signal_connect(mContainer, "notify::scale-factor",
                   G_CALLBACK(scale_changed_cb), nullptr);

  // Initialize mHasMappedToplevel.
  hierarchy_changed_cb(GTK_WIDGET(mContainer), nullptr);
  // Expose, focus, key, and drag events are sent even to GTK_NO_WINDOW
  // widgets.
  g_signal_connect(G_OBJECT(mContainer), "draw", G_CALLBACK(expose_event_cb),
                   nullptr);
  g_signal_connect(mContainer, "focus_in_event", G_CALLBACK(focus_in_event_cb),
                   nullptr);
  g_signal_connect(mContainer, "focus_out_event",
                   G_CALLBACK(focus_out_event_cb), nullptr);
  g_signal_connect(mContainer, "key_press_event",
                   G_CALLBACK(key_press_event_cb), nullptr);
  g_signal_connect(mContainer, "key_release_event",
                   G_CALLBACK(key_release_event_cb), nullptr);

  g_signal_connect(mShell, "destroy", G_CALLBACK(widget_destroy_cb), nullptr);

  // We create input contexts for all containers, except for
  // toplevel popup windows
  if (mWindowType != WindowType::Popup) {
    mIMContext = new IMContextWrapper(this);
  }

  // A popup attached to a modal parent window doesn't get mouse events
  // from Gtk as they'are directed to the modal parent. This is usually solved
  // by pointer grab which that doesn't work on Wayland in our current
  // setup as it performs show and grab in one step.
  //
  // We emulate it by setting popup as modal too but then patent
  // window doesn't get mouse events outside of popup (Bug 1899299).
  // we need to listen
  //
  // Surprisingly attaching events handler to mShell fixes it
  // and we're getting events from both parent and popup windows.
  GtkWidget* eventWidget = (mWindowType == WindowType::Popup &&
                            gtk_window_get_modal(GTK_WINDOW(mShell)))
                               ? mShell
                               : GTK_WIDGET(mContainer);
  g_signal_connect(eventWidget, "enter-notify-event",
                   G_CALLBACK(enter_notify_event_cb), nullptr);
  g_signal_connect(eventWidget, "leave-notify-event",
                   G_CALLBACK(leave_notify_event_cb), nullptr);
  g_signal_connect(eventWidget, "motion-notify-event",
                   G_CALLBACK(motion_notify_event_cb), nullptr);
  g_signal_connect(eventWidget, "button-press-event",
                   G_CALLBACK(button_press_event_cb), nullptr);
  g_signal_connect(eventWidget, "button-release-event",
                   G_CALLBACK(button_release_event_cb), nullptr);
  g_signal_connect(eventWidget, "scroll-event", G_CALLBACK(scroll_event_cb),
                   nullptr);
  if (gtk_check_version(3, 18, 0) == nullptr) {
    g_signal_connect(eventWidget, "event", G_CALLBACK(generic_event_cb),
                     nullptr);
  }
  g_signal_connect(eventWidget, "touch-event", G_CALLBACK(touch_event_cb),
                   nullptr);

  LOG("  nsWindow type %d %s\n", int(mWindowType),
      mPiPType == PiPType::MediaPiP
          ? "Media PiP window"
          : (mPiPType == PiPType::DocumentPiP ? "Document PiP window" : ""));
  LOG("  mShell %p (window %p) mContainer %p mGdkWindow %p XID 0x%lx\n", mShell,
      GetToplevelGdkWindow(), mContainer, mGdkWindow, GetX11Window());

  // Set default application name when it's empty.
  if (mGtkWindowAppName.IsEmpty()) {
    mGtkWindowAppName = gAppData->name;
  }

  mCreated = true;
  return NS_OK;
}

void nsWindow::RefreshWindowClass(void) {
  GdkWindow* gdkWindow = GetToplevelGdkWindow();
  if (!gdkWindow) {
    return;
  }

  if (!mGtkWindowRoleName.IsEmpty()) {
    gdk_window_set_role(gdkWindow, mGtkWindowRoleName.get());
  }

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    XClassHint* class_hint = XAllocClassHint();
    if (!class_hint) {
      return;
    }

    const char* res_name =
        !mGtkWindowAppName.IsEmpty() ? mGtkWindowAppName.get() : gAppData->name;

    const char* res_class = !mGtkWindowAppClass.IsEmpty()
                                ? mGtkWindowAppClass.get()
                                : gdk_get_program_class();

    if (!res_name || !res_class) {
      XFree(class_hint);
      return;
    }

    class_hint->res_name = const_cast<char*>(res_name);
    class_hint->res_class = const_cast<char*>(res_class);

    // Can't use gtk_window_set_wmclass() for this; it prints
    // a warning & refuses to make the change.
    GdkDisplay* display = gdk_display_get_default();
    XSetClassHint(GDK_DISPLAY_XDISPLAY(display),
                  gdk_x11_window_get_xid(gdkWindow), class_hint);
    XFree(class_hint);
  }
#endif /* MOZ_X11 */

#ifdef MOZ_WAYLAND
  static auto sGdkWaylandWindowSetApplicationId =
      (void (*)(GdkWindow*, const char*))dlsym(
          RTLD_DEFAULT, "gdk_wayland_window_set_application_id");

  if (GdkIsWaylandDisplay() && sGdkWaylandWindowSetApplicationId &&
      !mGtkWindowAppClass.IsEmpty()) {
    sGdkWaylandWindowSetApplicationId(gdkWindow, mGtkWindowAppClass.get());
  }
#endif /* MOZ_WAYLAND */
}

void nsWindow::SetWindowClass(const nsAString& xulWinType,
                              const nsAString& xulWinClass,
                              const nsAString& xulWinName) {
  if (!mShell) {
    return;
  }

  // If window type attribute is set, parse it into name and role
  if (!xulWinType.IsEmpty()) {
    char* res_name = ToNewCString(xulWinType, mozilla::fallible);
    const char* role = nullptr;

    if (res_name) {
      // Parse res_name into a name and role. Characters other than
      // [A-Za-z0-9_-] are converted to '_'. Anything after the first
      // colon is assigned to role; if there's no colon, assign the
      // whole thing to both role and res_name.
      for (char* c = res_name; *c; c++) {
        if (':' == *c) {
          *c = 0;
          role = c + 1;
        } else if (!isascii(*c) ||
                   (!isalnum(*c) && ('_' != *c) && ('-' != *c))) {
          *c = '_';
        }
      }
      res_name[0] = (char)toupper(res_name[0]);
      if (!role) role = res_name;

      mGtkWindowAppName = res_name;
      mGtkWindowRoleName = role;
      free(res_name);
    }
  }

  // If window class attribute is set, store it as app class
  // If this attribute is not set, reset app class to default
  if (!xulWinClass.IsEmpty()) {
    CopyUTF16toUTF8(xulWinClass, mGtkWindowAppClass);
  } else {
    mGtkWindowAppClass = nullptr;
  }

  // If window class attribute is set, store it as app name
  // If both name and type are not set, reset app name to default
  if (!xulWinName.IsEmpty()) {
    CopyUTF16toUTF8(xulWinName, mGtkWindowAppName);
  } else if (xulWinType.IsEmpty()) {
    mGtkWindowAppClass = nullptr;
  }

  RefreshWindowClass();
}

nsAutoCString nsWindow::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("[%p]", this);
  return tag;
}

void nsWindow::NativeMoveResize(bool aMoved, bool aResized) {
  const DesktopIntRect frameRect(mLastMoveRequest, mLastSizeRequest);

  // Window size is calculated without decorations, Gtk adds decorations
  // size to our request at gtk_window_resize().
  GdkRectangle moveResizeRect = [&] {
    auto cr = frameRect;
    // In CSD mode gtk_window_move() / gtk_window_resize() expects coordinates
    // without CSD frame so remove it.
    // Note that popups should have zero margin.
    cr.Deflate(mClientMargin);
    // SSD mode expects coordinates with decorations (outer frame)
    // so put the margin offset back.
    if (!ToplevelUsesCSD()) {
      cr -= DesktopIntPoint(mClientMargin.left, mClientMargin.top);
    }
    return GdkRectangle{cr.x, cr.y, cr.width, cr.height};
  }();

  LOG("nsWindow::NativeMoveResize mLastMoveRequest [%d,%d] mClientMargin "
      "[%d,%d] move %d resize %d to [%d,%d] -> [%d x %d]\n",
      int(mLastMoveRequest.x), int(mLastMoveRequest.y), int(mClientMargin.left),
      int(mClientMargin.top), aMoved, aResized, moveResizeRect.x,
      moveResizeRect.y, moveResizeRect.width, moveResizeRect.height);

  if (aResized && !AreBoundsSane()) {
    LOG("  bounds are insane, hidding the window");
    // We have been resized but to incorrect size.
    // If someone has set this so that the needs show flag is false
    // and it needs to be hidden, update the flag and hide the
    // window.  This flag will be cleared the next time someone
    // hides the window or shows it.  It also prevents us from
    // calling NativeShow(false) excessively on the window which
    // causes unneeded X traffic.
    if (!mNeedsShow && mIsShown) {
      mNeedsShow = true;
      NativeShow(false);
    }
    if (aMoved) {
      LOG("  moving to %d x %d", moveResizeRect.x, moveResizeRect.y);
      gtk_window_move(GTK_WINDOW(mShell), moveResizeRect.x, moveResizeRect.y);
    }
    return;
  }

  // Set position to hidden window on X11 may fail, so save the position
  // and move it when it's shown.
  if (aMoved && GdkIsX11Display() && IsPopup() &&
      !gtk_widget_get_visible(GTK_WIDGET(mShell))) {
    mX11HiddenPopupPositioned = true;
    mClientArea.MoveTo(mLastMoveRequest);
  }

#ifdef MOZ_WAYLAND
  if (IsWaylandPopup()) {
    AsWayland()->NativeMoveResizeWaylandPopup(aMoved, aResized);
  } else
#endif
  {
    if (aResized) {
      gtk_window_resize(GTK_WINDOW(mShell), moveResizeRect.width,
                        moveResizeRect.height);
      if (mIsDragPopup) {
        // DND window is placed inside container so we need to make hard size
        // request to ensure parent container is resized too.
        gtk_widget_set_size_request(GTK_WIDGET(mShell), moveResizeRect.width,
                                    moveResizeRect.height);
      }
    }
    // x and y give the position of the window manager frame top-left.
    if (aMoved) {
      gtk_window_move(GTK_WINDOW(mShell), moveResizeRect.x, moveResizeRect.y);
    }
  }

  if (aResized) {
    // Recompute the input region, in case the window grew or shrunk.
    SetInputRegion(mInputRegion);
  }

  // Does it need to be shown because bounds were previously insane?
  if (mNeedsShow && mIsShown && aResized) {
    NativeShow(true);
  }

  // We optimistically assume size/position changes immediately in two cases:
  //
  // 1. Popup: Size is controlled by only us.
  // 2. Managed window that has not not yet received a size-allocate event:
  //    Resize() Callers expect initial sizes to be applied synchronously.
  //    If the size request is not honored, then we'll correct in
  //    OnContainerSizeAllocate().
  //
  // When a managed window has already received a size-allocate, we cannot
  // assume we'll always get a notification if our request does not get
  // honored: "If the configure request has not changed, we don't ever resend
  // it, because it could mean fighting the user or window manager."
  // https://gitlab.gnome.org/GNOME/gtk/-/blob/3.24.31/gtk/gtkwindow.c#L9782
  // So we don't update mClientArea until OnContainerSizeAllocate() when we know
  // the request is granted.
  bool isOrWillBeVisible = mHasReceivedSizeAllocate || mNeedsShow || mIsShown;
  if (!isOrWillBeVisible || IsPopup()) {
    if (aResized) {
      mClientArea.SizeTo(mLastSizeRequest);
    }
    if (aMoved) {
      mClientArea.MoveTo(mLastMoveRequest);
      NotifyWindowMoved(mClientArea.TopLeft());
    }
    if (aResized) {
      DispatchResized();
    }
  }
}

void nsWindow::SetHasMappedToplevel(bool aState) {
  LOG("nsWindow::SetHasMappedToplevel(%d)", aState);
  if (aState == mHasMappedToplevel) {
    return;
  }
  // Even when aState == mHasMappedToplevel (as when this method is called
  // from Show()), child windows need to have their state checked, so don't
  // return early.
  mHasMappedToplevel = aState;
  if (aState && mNeedsToRetryCapturingMouse) {
    CaptureRollupEvents(true);
    MOZ_ASSERT(!mNeedsToRetryCapturingMouse);
  }
}

bool nsWindow::SetSafeWindowSize(LayoutDeviceIntSize& aSize) {
  // The X protocol uses CARD32 for window sizes, but the server (1.11.3)
  // reads it as CARD16.  Sizes of pixmaps, used for drawing, are (unsigned)
  // CARD16 in the protocol, but the server's ProcCreatePixmap returns
  // BadAlloc if dimensions cannot be represented by signed shorts.
  // Because we are creating Cairo surfaces to represent window buffers,
  // we also must ensure that the window can fit in a Cairo surface.
  // Keep some reserve for window margin/decorations.
  bool changed = false;
  int32_t maxSize = 32000;
  if (mWindowRenderer && mWindowRenderer->AsKnowsCompositor()) {
    maxSize = std::min(
        maxSize, mWindowRenderer->AsKnowsCompositor()->GetMaxTextureSize());
  }
  if (aSize.width > maxSize) {
    aSize.width = maxSize;
    changed = true;
  }
  if (aSize.height > maxSize) {
    aSize.height = maxSize;
    changed = true;
  }
  return changed;
}

bool nsWindow::SetSafeWindowSize(DesktopIntSize& aSize) {
  auto layoutDeviceSize = ToLayoutDevicePixels(aSize);
  auto ret = SetSafeWindowSize(layoutDeviceSize);
  if (ret) {
    aSize = ToDesktopPixels(layoutDeviceSize);
  }
  return ret;
}

void nsWindow::SetTransparencyMode(TransparencyMode aMode) {
  const bool isTransparent = aMode == TransparencyMode::Transparent;

  if (mIsTransparent == isTransparent) {
    return;
  }

  if (mWindowType != WindowType::Popup) {
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1344839 reported
    // problems cleaning the layer manager for toplevel windows.
    // Ignore the request so as to workaround that.
    // mIsTransparent is set in Create() if transparency may be required.
    if (isTransparent) {
      NS_WARNING(
          "Non-initial transparent mode not supported on non-popup windows.");
    }
    return;
  }

  if (!mCompositedScreen) {
    return;
  }

  mIsTransparent = isTransparent;

  if (!mHasAlphaVisual) {
    // The choice of layer manager depends on
    // GtkCompositorWidgetInitData::Shaped(), which will need to change, so
    // clean out the old layer manager.
    DestroyLayerManager();
  }
}

TransparencyMode nsWindow::GetTransparencyMode() {
  return mIsTransparent ? TransparencyMode::Transparent
                        : TransparencyMode::Opaque;
}

gint nsWindow::GetInputRegionMarginInGdkCoords() {
  return DevicePixelsToGdkCoordRoundDown(mInputRegion.mMargin);
}

void nsWindow::SetInputRegion(const InputRegion& aInputRegion) {
  mInputRegion = aInputRegion;

  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    return;
  }

  int unscaledMargin = GetInputRegionMarginInGdkCoords();
  LOG("nsWindow::SetInputRegion(%d, %d)", mInputRegion.mFullyTransparent,
      unscaledMargin);

  cairo_rectangle_int_t rect = {0, 0, 0, 0};
  cairo_region_t* region = nullptr;
  auto releaseRegion = MakeScopeExit([&] {
    if (region) {
      cairo_region_destroy(region);
    }
  });

  if (mInputRegion.mFullyTransparent) {
    region = cairo_region_create_rectangle(&rect);
  } else if (unscaledMargin != 0) {
    DesktopIntRect inputRegion(DesktopIntPoint(), mLastSizeRequest);
    inputRegion.Deflate(unscaledMargin);
    rect = {inputRegion.x, inputRegion.y, inputRegion.width,
            inputRegion.height};
    region = cairo_region_create_rectangle(&rect);
  }

  gdk_window_input_shape_combine_region(window, region, 0, 0);

  // On Wayland gdk_window_input_shape_combine_region() call is cached and
  // applied to underlying wl_surface when GdkWindow is repainted.
  // Force repaint of GdkWindow to apply the change immediately.
  if (GdkIsWaylandDisplay()) {
    gdk_window_invalidate_rect(window, nullptr, false);
  }
}

// For setting the draggable titlebar region from CSS
// with -moz-window-dragging: drag.
void nsWindow::UpdateWindowDraggingRegion(
    const LayoutDeviceIntRegion& aRegion) {
  if (mDraggableRegion != aRegion) {
    mDraggableRegion = aRegion;
  }
}

#ifdef MOZ_ENABLE_DBUS
void nsWindow::SetDBusMenuBar(
    RefPtr<mozilla::widget::DBusMenuBar> aDbusMenuBar) {
  mDBusMenuBar = std::move(aDbusMenuBar);
}
#endif

LayoutDeviceIntRegion nsWindow::GetOpaqueRegion() const {
  AutoReadLock r(mOpaqueRegionLock);
  return mOpaqueRegion;
}

void nsWindow::UpdateOpaqueRegion(const LayoutDeviceIntRegion& aRegion) {
  {
    AutoReadLock r(mOpaqueRegionLock);
    if (mOpaqueRegion == aRegion) {
      return;
    }
  }
  {
    AutoWriteLock w(mOpaqueRegionLock);
    mOpaqueRegion = aRegion;
  }
  UpdateOpaqueRegionInternal();
}

void nsWindow::UpdateOpaqueRegionInternal() {
  if (!mCompositedScreen) {
    return;
  }

  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    return;
  }

  {
    AutoReadLock lock(mOpaqueRegionLock);
    cairo_region_t* region = nullptr;
    if (!mOpaqueRegion.IsEmpty()) {
      // NOTE(emilio): The opaque region is relative to our mContainer /
      // mGdkWindow / inner window, but we're setting it on the top level
      // GdkWindow / mShell.
      //
      // So we need to offset the rects by the position of mGdkWindow, in order
      // for them to be in the right coordinate system.
      GdkPoint offset{0, 0};
      gdk_window_get_position(mGdkWindow, &offset.x, &offset.y);

      region = cairo_region_create();

      const auto clientRegion =
          LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientSize());
      for (auto iter = mOpaqueRegion.RectIter(); !iter.Done(); iter.Next()) {
        auto thisRect = iter.Get().Intersect(clientRegion);
        if (thisRect.IsEmpty()) {
          continue;
        }
        auto gdkRect = DevicePixelsToGdkRectRoundIn(thisRect);
        cairo_rectangle_int_t rect = {gdkRect.x + offset.x,
                                      gdkRect.y + offset.y, gdkRect.width,
                                      gdkRect.height};
        LOG("nsWindow::UpdateOpaqueRegionInternal() set opaque region [%d,%d] "
            "-> [%d x %d]",
            gdkRect.x, gdkRect.y, gdkRect.width, gdkRect.height);
        cairo_region_union_rectangle(region, &rect);
      }
    } else {
      LOG("nsWindow::UpdateOpaqueRegionInternal() window is transparent");
    }
    gdk_window_set_opaque_region(window, region);
    if (region) {
      cairo_region_destroy(region);
    }

#ifdef MOZ_WAYLAND
    if (GdkIsWaylandDisplay()) {
      mSurface->SetOpaqueRegion(mOpaqueRegion.ToUnknownRegion());
    }
#endif
  }
}

bool nsWindow::IsChromeWindowTitlebar() {
  return mDrawInTitlebar && mPiPType != PiPType::MediaPiP &&
         mWindowType == WindowType::TopLevel;
}

bool nsWindow::DoDrawTilebarCorners() {
  return IsChromeWindowTitlebar() && mSizeMode == nsSizeMode_Normal &&
         !mIsTiled;
}

GdkWindow* nsWindow::GetToplevelGdkWindow() const {
  return gtk_widget_get_window(mShell);
}

nsWindow* nsWindow::GetContainerWindow() const {
  GtkWidget* owningWidget = GTK_WIDGET(mContainer);
  if (!owningWidget) {
    return nullptr;
  }

  nsWindow* window = nsWindow::FromGtkWidget(owningWidget);
  NS_ASSERTION(window, "No nsWindow for container widget");
  return window;
}

void nsWindow::SetUrgencyHint(GtkWidget* top_window, bool state) {
  LOG("  nsWindow::SetUrgencyHint widget %p\n", top_window);
  if (!top_window) {
    return;
  }
  GdkWindow* window = gtk_widget_get_window(top_window);
  if (!window) {
    return;
  }
  // TODO: Use xdg-activation on Wayland?
  gdk_window_set_urgency_hint(window, state);
}

void nsWindow::SetDefaultIcon(void) { SetIcon(u"default"_ns); }

gint nsWindow::ConvertBorderStyles(BorderStyle aStyle) {
  gint w = 0;

  if (aStyle == BorderStyle::Default) {
    return -1;
  }

  if (aStyle & BorderStyle::All) w |= GDK_DECOR_ALL;
  if (aStyle & BorderStyle::Border) w |= GDK_DECOR_BORDER;
  if (aStyle & BorderStyle::ResizeH) w |= GDK_DECOR_RESIZEH;
  if (aStyle & BorderStyle::Title) w |= GDK_DECOR_TITLE;
  if (aStyle & BorderStyle::Menu) w |= GDK_DECOR_MENU;
  if (aStyle & BorderStyle::Minimize) w |= GDK_DECOR_MINIMIZE;
  if (aStyle & BorderStyle::Maximize) w |= GDK_DECOR_MAXIMIZE;

  return w;
}

class FullscreenTransitionWindow final : public nsISupports {
 public:
  NS_DECL_ISUPPORTS

  explicit FullscreenTransitionWindow(GtkWidget* aWidget);

  GtkWidget* mWindow;

 private:
  ~FullscreenTransitionWindow();
};

NS_IMPL_ISUPPORTS0(FullscreenTransitionWindow)

FullscreenTransitionWindow::FullscreenTransitionWindow(GtkWidget* aWidget) {
  mWindow = gtk_window_new(GTK_WINDOW_POPUP);
  GtkWindow* gtkWin = GTK_WINDOW(mWindow);

  gtk_window_set_type_hint(gtkWin, GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
  GtkWindowSetTransientFor(gtkWin, GTK_WINDOW(aWidget));
  gtk_window_set_decorated(gtkWin, false);

  GdkWindow* gdkWin = gtk_widget_get_window(aWidget);
  GdkScreen* screen = gtk_widget_get_screen(aWidget);
  gint monitorNum = gdk_screen_get_monitor_at_window(screen, gdkWin);
  GdkRectangle monitorRect;
  gdk_screen_get_monitor_geometry(screen, monitorNum, &monitorRect);
  gtk_window_set_screen(gtkWin, screen);
  gtk_window_move(gtkWin, monitorRect.x, monitorRect.y);
  MOZ_ASSERT(monitorRect.width > 0 && monitorRect.height > 0,
             "Can't resize window smaller than 1x1.");
  gtk_window_resize(gtkWin, monitorRect.width, monitorRect.height);

  GdkRGBA bgColor;
  bgColor.red = bgColor.green = bgColor.blue = 0.0;
  bgColor.alpha = 1.0;
  gtk_widget_override_background_color(mWindow, GTK_STATE_FLAG_NORMAL,
                                       &bgColor);

  gtk_widget_set_opacity(mWindow, 0.0);
  gtk_widget_show(mWindow);
}

FullscreenTransitionWindow::~FullscreenTransitionWindow() {
  gtk_widget_destroy(mWindow);
}

class FullscreenTransitionData {
 public:
  FullscreenTransitionData(nsIWidget::FullscreenTransitionStage aStage,
                           uint16_t aDuration, nsIRunnable* aCallback,
                           FullscreenTransitionWindow* aWindow)
      : mStage(aStage),
        mStartTime(TimeStamp::Now()),
        mDuration(TimeDuration::FromMilliseconds(aDuration)),
        mCallback(aCallback),
        mWindow(aWindow) {}

  static const guint sInterval = 1000 / 30;  // 30fps
  static gboolean TimeoutCallback(gpointer aData);

 private:
  nsIWidget::FullscreenTransitionStage mStage;
  TimeStamp mStartTime;
  TimeDuration mDuration;
  nsCOMPtr<nsIRunnable> mCallback;
  RefPtr<FullscreenTransitionWindow> mWindow;
};

/* static */
gboolean FullscreenTransitionData::TimeoutCallback(gpointer aData) {
  bool finishing = false;
  auto* data = static_cast<FullscreenTransitionData*>(aData);
  gdouble opacity = (TimeStamp::Now() - data->mStartTime) / data->mDuration;
  if (opacity >= 1.0) {
    opacity = 1.0;
    finishing = true;
  }
  if (data->mStage == nsIWidget::eAfterFullscreenToggle) {
    opacity = 1.0 - opacity;
  }
  gtk_widget_set_opacity(data->mWindow->mWindow, opacity);

  if (!finishing) {
    return TRUE;
  }
  NS_DispatchToMainThread(data->mCallback.forget());
  delete data;
  return FALSE;
}

/* virtual */
bool nsWindow::PrepareForFullscreenTransition(nsISupports** aData) {
  if (!mCompositedScreen) {
    return false;
  }
  *aData = do_AddRef(new FullscreenTransitionWindow(mShell)).take();
  return true;
}

/* virtual */
void nsWindow::PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                           uint16_t aDuration,
                                           nsISupports* aData,
                                           nsIRunnable* aCallback) {
  auto* data = static_cast<FullscreenTransitionWindow*>(aData);
  // This will be released at the end of the last timeout callback for it.
  auto* transitionData =
      new FullscreenTransitionData(aStage, aDuration, aCallback, data);
  g_timeout_add_full(G_PRIORITY_HIGH, FullscreenTransitionData::sInterval,
                     FullscreenTransitionData::TimeoutCallback, transitionData,
                     nullptr);
}

already_AddRefed<mozilla::widget::Screen> nsWindow::GetWidgetScreen() {
  // Wayland can read screen directly
  if (GdkIsWaylandDisplay()) {
    if (RefPtr<mozilla::widget::Screen> screen =
            ScreenHelperGTK::GetScreenForWindow(this)) {
      return screen.forget();
    }
  }

  ScreenManager& screenManager = ScreenManager::GetSingleton();
  DesktopIntRect deskBounds =
      RoundedToInt(GetScreenBounds() / GetDesktopToDeviceScale());
  return screenManager.ScreenForRect(deskBounds);
}

bool nsWindow::SynchronouslyRepaintOnResize() {
  if (GdkIsWaylandDisplay()) {
    // See Bug 1734368
    // Don't request synchronous repaint on HW accelerated backend - mesa can be
    // deadlocked when it's missing back buffer and main event loop is blocked.
    return false;
  }

  // default is synced repaint.
  return true;
}

void nsWindow::KioskLockOnMonitor() {
  // Available as of GTK 3.18+
  static auto sGdkWindowFullscreenOnMonitor =
      (void (*)(GdkWindow* window, gint monitor))dlsym(
          RTLD_DEFAULT, "gdk_window_fullscreen_on_monitor");

  if (!sGdkWindowFullscreenOnMonitor) {
    return;
  }

  int monitor = mKioskMonitor.value();
  if (monitor < 0 || monitor >= ScreenHelperGTK::GetMonitorCount()) {
    LOG("nsWindow::KioskLockOnMonitor() wrong monitor number! (%d)\n", monitor);
    return;
  }

  LOG("nsWindow::KioskLockOnMonitor() locked on %d\n", monitor);
  sGdkWindowFullscreenOnMonitor(GetToplevelGdkWindow(), monitor);
}

static bool IsFullscreenSupported(GtkWidget* aShell) {
#ifdef MOZ_X11
  GdkScreen* screen = gtk_widget_get_screen(aShell);
  GdkAtom atom = gdk_atom_intern("_NET_WM_STATE_FULLSCREEN", FALSE);
  return gdk_x11_screen_supports_net_wm_hint(screen, atom);
#else
  return true;
#endif
}

nsresult nsWindow::MakeFullScreen(bool aFullScreen) {
  LOG("nsWindow::MakeFullScreen aFullScreen %d\n", aFullScreen);

  if (GdkIsX11Display() && !IsFullscreenSupported(mShell)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (aFullScreen) {
    if (mSizeMode != nsSizeMode_Fullscreen &&
        mSizeMode != nsSizeMode_Minimized) {
      mLastSizeModeBeforeFullscreen = mSizeMode;
    }
    if (mPiPType == PiPType::MediaPiP) {
      gtk_window_set_type_hint(GTK_WINDOW(mShell), GDK_WINDOW_TYPE_HINT_NORMAL);
      if (gUseAspectRatio) {
        mAspectRatioSaved = mAspectRatio;
        mAspectRatio = 0.0f;
        ApplySizeConstraints();
      }
    }

    if (mKioskMonitor.isSome()) {
      KioskLockOnMonitor();
    } else {
      gtk_window_fullscreen(GTK_WINDOW(mShell));
    }
  } else {
    // Kiosk mode always use fullscreen mode.
    if (gKioskMode) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    gtk_window_unfullscreen(GTK_WINDOW(mShell));

    if (mPiPType == PiPType::MediaPiP && gUseAspectRatio) {
      mAspectRatio = mAspectRatioSaved;
      // ApplySizeConstraints();
    }
  }

  MOZ_ASSERT(mLastSizeModeBeforeFullscreen != nsSizeMode_Fullscreen);
  return NS_OK;
}

void nsWindow::SetWindowDecoration(BorderStyle aStyle) {
  LOG("nsWindow::SetWindowDecoration() Border style %x\n", int(aStyle));

  // Sawfish, metacity, and presumably other window managers get
  // confused if we change the window decorations while the window
  // is visible.
  bool wasVisible = false;

  if (gtk_widget_is_visible(GTK_WIDGET(mShell))) {
    gtk_widget_hide(GTK_WIDGET(mShell));
    wasVisible = true;
  }

  const bool decorated = !mUndecorated && aStyle != BorderStyle::None;
  gtk_window_set_decorated(GTK_WINDOW(mShell), decorated);

  if (!decorated) {
    // Work around for https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/8875,
    // window shadow doesn't update on Wayland after removing window
    // decorations. This doesn't seem like it'd affect GTK4.
    gdk_window_set_shadow_width(GetToplevelGdkWindow(), 0, 0, 0, 0);
  }

  gint wmd = ConvertBorderStyles(aStyle);
  if (wmd != -1) {
    gdk_window_set_decorations(GetToplevelGdkWindow(), (GdkWMDecoration)wmd);
  }

  if (wasVisible) {
    gtk_widget_show(GTK_WIDGET(mShell));
  }

#ifdef MOZ_X11
  // For some window managers, adding or removing window decorations
  // requires unmapping and remapping our toplevel window.  Go ahead
  // and flush the queue here so that we don't end up with a BadWindow
  // error later when this happens (when the persistence timer fires
  // and GetWindowPos is called)
  if (GdkIsX11Display()) {
    XSync(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), X11False);
  } else
#endif /* MOZ_X11 */
  {
    gdk_flush();
  }
}

void nsWindow::HideWindowChrome(bool aShouldHide) {
  SetWindowDecoration(aShouldHide ? BorderStyle::None : mBorderStyle);
}

bool nsWindow::CheckForRollup(gdouble aMouseX, gdouble aMouseY, bool aIsWheel,
                              bool aAlwaysRollup) {
  LOG("nsWindow::CheckForRollup() aAlwaysRollup %d", aAlwaysRollup);
  nsIRollupListener* rollupListener = GetActiveRollupListener();
  nsCOMPtr<nsIWidget> rollupWidget;
  if (rollupListener) {
    rollupWidget = rollupListener->GetRollupWidget();
  }
  if (!rollupWidget) {
    return false;
  }

  auto* rollupWindow =
      (GdkWindow*)rollupWidget->GetNativeData(NS_NATIVE_WINDOW);
  if (!aAlwaysRollup && is_mouse_in_window(rollupWindow, aMouseX, aMouseY)) {
    return false;
  }
  bool retVal = false;
  if (aIsWheel) {
    retVal = rollupListener->ShouldConsumeOnMouseWheelEvent();
    if (!rollupListener->ShouldRollupOnMouseWheelEvent()) {
      return retVal;
    }
  }
  LayoutDeviceIntPoint point;
  nsIRollupListener::RollupOptions options;
  // if we're dealing with menus, we probably have submenus and
  // we don't want to rollup if the click is in a parent menu of
  // the current submenu
  if (!aAlwaysRollup) {
    AutoTArray<nsIWidget*, 5> widgetChain;
    uint32_t sameTypeCount =
        rollupListener->GetSubmenuWidgetChain(&widgetChain);
    for (unsigned long i = 0; i < widgetChain.Length(); ++i) {
      nsIWidget* widget = widgetChain[i];
      auto* currWindow = (GdkWindow*)widget->GetNativeData(NS_NATIVE_WINDOW);
      if (is_mouse_in_window(currWindow, aMouseX, aMouseY)) {
        // Don't roll up if the mouse event occurred within a menu of the same
        // type.
        // If the mouse event occurred in a menu higher than that, roll up, but
        // pass the number of popups to Rollup so that only those of the same
        // type close up.
        if (i < sameTypeCount) {
          return retVal;
        }
        options.mCount = sameTypeCount;
        break;
      }
    }  // foreach parent menu widget
    if (!aIsWheel) {
      point = GdkEventCoordsToDevicePixels(aMouseX, aMouseY);
      options.mPoint = &point;
    }
  }

  if (mSizeMode == nsSizeMode_Minimized) {
    // When we try to rollup in a minimized window, transitionend events for
    // panels might not fire and thus we might not hide the popup after all,
    // see bug 1810797.
    options.mAllowAnimations = nsIRollupListener::AllowAnimations::No;
  }

  if (rollupListener->Rollup(options)) {
    retVal = true;
  }
  return retVal;
}

bool nsWindow::DragInProgress() {
  nsCOMPtr<nsIDragService> dragService =
      do_GetService("@mozilla.org/widget/dragservice;1");
  if (!dragService) {
    return false;
  }

  nsCOMPtr<nsIDragSession> currentDragSession =
      dragService->GetCurrentSession(this);
  return !!currentDragSession;
}

nsWindow* nsWindow::FromGtkWidget(GtkWidget* widget) {
  gpointer user_data = g_object_get_data(G_OBJECT(widget), "nsWindow");
  return static_cast<nsWindow*>(user_data);
}

nsWindow* nsWindow::FromGdkWindow(GdkWindow* window) {
  gpointer user_data = g_object_get_data(G_OBJECT(window), "nsWindow");
  return static_cast<nsWindow*>(user_data);
}

static bool is_mouse_in_window(GdkWindow* aWindow, gdouble aMouseX,
                               gdouble aMouseY) {
  GdkWindow* window = aWindow;
  if (!window) {
    return false;
  }

  gint x = 0;
  gint y = 0;

  {
    gint offsetX = 0;
    gint offsetY = 0;

    while (window) {
      gint tmpX = 0;
      gint tmpY = 0;

      gdk_window_get_position(window, &tmpX, &tmpY);
      GtkWidget* widget = get_gtk_widget_for_gdk_window(window);

      // if this is a window, compute x and y given its origin and our
      // offset
      if (GTK_IS_WINDOW(widget)) {
        x = tmpX + offsetX;
        y = tmpY + offsetY;
        break;
      }

      offsetX += tmpX;
      offsetY += tmpY;
      window = gdk_window_get_parent(window);
    }
  }

  gint margin = 0;
  if (nsWindow* w = nsWindow::FromGdkWindow(aWindow)) {
    margin = w->GetInputRegionMarginInGdkCoords();
  }

  x += margin;
  y += margin;

  gint w = gdk_window_get_width(aWindow) - margin;
  gint h = gdk_window_get_height(aWindow) - margin;

  return aMouseX > x && aMouseX < x + w && aMouseY > y && aMouseY < y + h;
}

static bool is_drag_threshold_exceeded(GdkEvent* aEvent) {
  GdkEvent* lastEvent = GetLastPointerDownEvent();

  if (!lastEvent) {
    return false;
  }

  const int32_t pixelThresholdX =
      LookAndFeel::GetInt(LookAndFeel::IntID::DragThresholdX, 5);
  const int32_t pixelThresholdY =
      LookAndFeel::GetInt(LookAndFeel::IntID::DragThresholdY, 5);

  gdouble lastX, lastY, currentX, currentY;
  gdk_event_get_root_coords(lastEvent, &lastX, &lastY);
  gdk_event_get_root_coords(aEvent, &currentX, &currentY);

  return std::abs(currentX - lastX) > pixelThresholdX ||
         std::abs(currentY - lastY) > pixelThresholdY;
}

static GtkWidget* get_gtk_widget_for_gdk_window(GdkWindow* window) {
  gpointer user_data = nullptr;
  gdk_window_get_user_data(window, &user_data);

  return GTK_WIDGET(user_data);
}

static GdkCursor* get_gtk_cursor_from_type(uint8_t aCursorType) {
  GdkDisplay* defaultDisplay = gdk_display_get_default();
  GdkCursor* gdkcursor = nullptr;

  // GtkCursors are defined at nsGtkCursors.h
  if (aCursorType > MOZ_CURSOR_NONE) {
    return nullptr;
  }

  // If by now we don't have a xcursor, this means we have to make a custom
  // one. First, we try creating a named cursor based on the hash of our
  // custom bitmap, as libXcursor has some magic to convert bitmapped cursors
  // to themed cursors
  if (GtkCursors[aCursorType].hash) {
    gdkcursor =
        gdk_cursor_new_from_name(defaultDisplay, GtkCursors[aCursorType].hash);
    if (gdkcursor) {
      return gdkcursor;
    }
  }

  LOGW("get_gtk_cursor_from_type(): Failed to get cursor type %d, try bitmap",
       aCursorType);

  // If we still don't have a xcursor, we now really create a bitmap cursor
  GdkPixbuf* cursor_pixbuf =
      gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
  if (!cursor_pixbuf) {
    return nullptr;
  }

  guchar* data = gdk_pixbuf_get_pixels(cursor_pixbuf);

  // Read data from GtkCursors and compose RGBA surface from 1bit bitmap and
  // mask GtkCursors bits and mask are 32x32 monochrome bitmaps (1 bit for
  // each pixel) so it's 128 byte array (4 bytes for are one bitmap row and
  // there are 32 rows here).
  const unsigned char* bits = GtkCursors[aCursorType].bits;
  const unsigned char* mask_bits = GtkCursors[aCursorType].mask_bits;

  for (int i = 0; i < 128; i++) {
    char bit = (char)*bits++;
    char mask = (char)*mask_bits++;
    for (int j = 0; j < 8; j++) {
      unsigned char pix = ~(((bit >> j) & 0x01) * 0xff);
      *data++ = pix;
      *data++ = pix;
      *data++ = pix;
      *data++ = (((mask >> j) & 0x01) * 0xff);
    }
  }

  gdkcursor = gdk_cursor_new_from_pixbuf(
      gdk_display_get_default(), cursor_pixbuf, GtkCursors[aCursorType].hot_x,
      GtkCursors[aCursorType].hot_y);

  g_object_unref(cursor_pixbuf);
  return gdkcursor;
}

static GdkCursor* get_gtk_cursor_legacy(nsCursor aCursor) {
  GdkCursor* gdkcursor = nullptr;
  Maybe<uint8_t> fallbackType;

  GdkDisplay* defaultDisplay = gdk_display_get_default();

  // The strategy here is to use standard GDK cursors, and, if not available,
  // load by standard name with gdk_cursor_new_from_name.
  // Spec is here: http://www.freedesktop.org/wiki/Specifications/cursor-spec/
  switch (aCursor) {
    case eCursor_standard:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_LEFT_PTR);
      break;
    case eCursor_wait:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_WATCH);
      break;
    case eCursor_select:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_XTERM);
      break;
    case eCursor_hyperlink:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_HAND2);
      break;
    case eCursor_n_resize:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_TOP_SIDE);
      break;
    case eCursor_s_resize:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_BOTTOM_SIDE);
      break;
    case eCursor_w_resize:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_LEFT_SIDE);
      break;
    case eCursor_e_resize:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_RIGHT_SIDE);
      break;
    case eCursor_nw_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_TOP_LEFT_CORNER);
      break;
    case eCursor_se_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_BOTTOM_RIGHT_CORNER);
      break;
    case eCursor_ne_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_TOP_RIGHT_CORNER);
      break;
    case eCursor_sw_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_BOTTOM_LEFT_CORNER);
      break;
    case eCursor_crosshair:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_CROSSHAIR);
      break;
    case eCursor_move:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_FLEUR);
      break;
    case eCursor_help:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_QUESTION_ARROW);
      break;
    case eCursor_copy:  // CSS3
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "copy");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_COPY);
      break;
    case eCursor_alias:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "alias");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ALIAS);
      break;
    case eCursor_context_menu:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "context-menu");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_CONTEXT_MENU);
      break;
    case eCursor_cell:
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_PLUS);
      break;
    // Those two aren’t standardized. Trying both KDE’s and GNOME’s names
    case eCursor_grab:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "openhand");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_HAND_GRAB);
      break;
    case eCursor_grabbing:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "closedhand");
      if (!gdkcursor) {
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "grabbing");
      }
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_HAND_GRABBING);
      break;
    case eCursor_spinning:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "progress");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_SPINNING);
      break;
    case eCursor_zoom_in:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "zoom-in");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ZOOM_IN);
      break;
    case eCursor_zoom_out:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "zoom-out");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ZOOM_OUT);
      break;
    case eCursor_not_allowed:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "not-allowed");
      if (!gdkcursor) {  // nonstandard, yet common
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "crossed_circle");
      }
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NOT_ALLOWED);
      break;
    case eCursor_no_drop:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "no-drop");
      if (!gdkcursor) {  // this nonstandard sequence makes it work on KDE and
                         // GNOME
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "forbidden");
      }
      if (!gdkcursor) {
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "circle");
      }
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NOT_ALLOWED);
      break;
    case eCursor_vertical_text:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "vertical-text");
      if (!gdkcursor) {
        fallbackType.emplace(MOZ_CURSOR_VERTICAL_TEXT);
      }
      break;
    case eCursor_all_scroll:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "all-scroll");
      break;
    case eCursor_nesw_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "size_bdiag");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NESW_RESIZE);
      break;
    case eCursor_nwse_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "size_fdiag");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NWSE_RESIZE);
      break;
    case eCursor_ns_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_SB_V_DOUBLE_ARROW);
      break;
    case eCursor_ew_resize:
      gdkcursor =
          gdk_cursor_new_for_display(defaultDisplay, GDK_SB_H_DOUBLE_ARROW);
      break;
    // Here, two better fitting cursors exist in some cursor themes. Try those
    // first
    case eCursor_row_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "split_v");
      if (!gdkcursor) {
        gdkcursor =
            gdk_cursor_new_for_display(defaultDisplay, GDK_SB_V_DOUBLE_ARROW);
      }
      break;
    case eCursor_col_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "split_h");
      if (!gdkcursor) {
        gdkcursor =
            gdk_cursor_new_for_display(defaultDisplay, GDK_SB_H_DOUBLE_ARROW);
      }
      break;
    case eCursor_none:
      fallbackType.emplace(MOZ_CURSOR_NONE);
      break;
    default:
      NS_ASSERTION(aCursor, "Invalid cursor type");
      gdkcursor = gdk_cursor_new_for_display(defaultDisplay, GDK_LEFT_PTR);
      break;
  }

  if (!gdkcursor && fallbackType.isSome()) {
    LOGW("get_gtk_cursor_legacy(): Failed to get cursor %d, try fallback",
         aCursor);
    gdkcursor = get_gtk_cursor_from_type(*fallbackType);
  }

  return gdkcursor;
}

static GdkCursor* get_gtk_cursor_from_name(nsCursor aCursor) {
  GdkCursor* gdkcursor = nullptr;
  Maybe<uint8_t> fallbackType;

  GdkDisplay* defaultDisplay = gdk_display_get_default();

  switch (aCursor) {
    case eCursor_standard:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "default");
      break;
    case eCursor_wait:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "wait");
      break;
    case eCursor_select:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "text");
      break;
    case eCursor_hyperlink:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "pointer");
      break;
    case eCursor_n_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "n-resize");
      break;
    case eCursor_s_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "s-resize");
      break;
    case eCursor_w_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "w-resize");
      break;
    case eCursor_e_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "e-resize");
      break;
    case eCursor_nw_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "nw-resize");
      break;
    case eCursor_se_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "se-resize");
      break;
    case eCursor_ne_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "ne-resize");
      break;
    case eCursor_sw_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "sw-resize");
      break;
    case eCursor_crosshair:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "crosshair");
      break;
    case eCursor_move:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "move");
      break;
    case eCursor_help:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "help");
      break;
    case eCursor_copy:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "copy");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_COPY);
      break;
    case eCursor_alias:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "alias");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ALIAS);
      break;
    case eCursor_context_menu:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "context-menu");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_CONTEXT_MENU);
      break;
    case eCursor_cell:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "cell");
      break;
    case eCursor_grab:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "grab");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_HAND_GRAB);
      break;
    case eCursor_grabbing:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "grabbing");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_HAND_GRABBING);
      break;
    case eCursor_spinning:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "progress");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_SPINNING);
      break;
    case eCursor_zoom_in:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "zoom-in");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ZOOM_IN);
      break;
    case eCursor_zoom_out:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "zoom-out");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_ZOOM_OUT);
      break;
    case eCursor_not_allowed:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "not-allowed");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NOT_ALLOWED);
      break;
    case eCursor_no_drop:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "no-drop");
      if (!gdkcursor) {  // this nonstandard sequence makes it work on KDE and
                         // GNOME
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "forbidden");
      }
      if (!gdkcursor) {
        gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "circle");
      }
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NOT_ALLOWED);
      break;
    case eCursor_vertical_text:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "vertical-text");
      if (!gdkcursor) {
        fallbackType.emplace(MOZ_CURSOR_VERTICAL_TEXT);
      }
      break;
    case eCursor_all_scroll:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "all-scroll");
      break;
    case eCursor_nesw_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "nesw-resize");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NESW_RESIZE);
      break;
    case eCursor_nwse_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "nwse-resize");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NWSE_RESIZE);
      break;
    case eCursor_ns_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "ns-resize");
      break;
    case eCursor_ew_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "ew-resize");
      break;
    case eCursor_row_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "row-resize");
      break;
    case eCursor_col_resize:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "col-resize");
      break;
    case eCursor_none:
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "none");
      if (!gdkcursor) fallbackType.emplace(MOZ_CURSOR_NONE);
      break;
    default:
      NS_ASSERTION(aCursor, "Invalid cursor type");
      gdkcursor = gdk_cursor_new_from_name(defaultDisplay, "default");
      break;
  }

  if (!gdkcursor && fallbackType.isSome()) {
    LOGW("get_gtk_cursor_from_name(): Failed to get cursor %d, try fallback",
         aCursor);
    gdkcursor = get_gtk_cursor_from_type(*fallbackType);
  }

  return gdkcursor;
}

static GdkCursor* get_gtk_cursor(nsCursor aCursor) {
  GdkCursor* gdkcursor = nullptr;

  if ((gdkcursor = gCursorCache[aCursor])) {
    return gdkcursor;
  }

  gdkcursor = StaticPrefs::widget_gtk_legacy_cursors_enabled()
                  ? get_gtk_cursor_legacy(aCursor)
                  : get_gtk_cursor_from_name(aCursor);

  gCursorCache[aCursor] = gdkcursor;

  return gdkcursor;
}

// gtk callbacks

void draw_window_of_widget(GtkWidget* widget, GdkWindow* aWindow, cairo_t* cr) {
  if (gtk_cairo_should_draw_window(cr, aWindow)) {
    RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
    if (!window) {
      NS_WARNING("Cannot get nsWindow from GtkWidget");
    } else {
      cairo_save(cr);
      gtk_cairo_transform_to_window(cr, widget, aWindow);
      // TODO - window->OnExposeEvent() can destroy this or other windows,
      // do we need to handle it somehow?
      window->OnExposeEvent(cr);
      cairo_restore(cr);
    }
  }
}

/* static */
gboolean expose_event_cb(GtkWidget* widget, cairo_t* cr) {
  draw_window_of_widget(widget, gtk_widget_get_window(widget), cr);

  // A strong reference is already held during "draw" signal emission,
  // but GTK+ 3.4 wants the object to live a little longer than that
  // (bug 1225970).
  g_object_ref(widget);
  g_idle_add(
      [](gpointer data) -> gboolean {
        g_object_unref(data);
        return G_SOURCE_REMOVE;
      },
      widget);

  return FALSE;
}

static gboolean shell_configure_event_cb(GtkWidget* widget,
                                         GdkEventConfigure* event) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  return window->OnShellConfigureEvent(event);
}

static void size_allocate_cb(GtkWidget* widget, GtkAllocation* allocation) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return;
  }
  window->OnContainerSizeAllocate(allocation);
}

static void toplevel_window_size_allocate_cb(GtkWidget* widget,
                                             GtkAllocation* allocation) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return;
  }

  // NOTE(emilio): We need to do this here to override GTK's own opaque region
  // setting (which would clobber ours).
  window->UpdateOpaqueRegionInternal();
}

static gboolean delete_event_cb(GtkWidget* widget, GdkEventAny* event) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnDeleteEvent();

  return TRUE;
}

static gboolean enter_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event) {
  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return TRUE;
  }

  // We have stored leave notify - check if it's the correct one and
  // fire it before enter notify in such case.
  if (sStoredLeaveNotifyEvent) {
    auto clearNofityEvent =
        MakeScopeExit([&] { sStoredLeaveNotifyEvent = nullptr; });
#ifdef MOZ_WAYLAND
    if (event->x_root == sStoredLeaveNotifyEvent->x_root &&
        event->y_root == sStoredLeaveNotifyEvent->y_root &&
        window->AsWayland() &&
        window->AsWayland()->ApplyEnterLeaveMutterWorkaround()) {
      // Enter/Leave notify events has the same coordinates
      // and uses know buggy window config.
      // Consider it as a bogus one.
      return TRUE;
    }
#endif
    RefPtr<nsWindow> leftWindow =
        nsWindow::FromGdkWindow(sStoredLeaveNotifyEvent->window);
    if (leftWindow) {
      leftWindow->OnLeaveNotifyEvent(sStoredLeaveNotifyEvent.get());
    }
  }

  window->OnEnterNotifyEvent(event);
  return TRUE;
}

static gboolean leave_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event) {
  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return TRUE;
  }

#ifdef MOZ_WAYLAND
  if (window->AsWayland() &&
      window->AsWayland()->ApplyEnterLeaveMutterWorkaround()) {
    // The leave event is potentially wrong, don't fire it now but store
    // it for further check at enter_notify_event_cb().
    sStoredLeaveNotifyEvent.reset(reinterpret_cast<GdkEventCrossing*>(
        gdk_event_copy(reinterpret_cast<GdkEvent*>(event))));
  } else
#endif
  {
    sStoredLeaveNotifyEvent = nullptr;
    window->OnLeaveNotifyEvent(event);
  }

  return TRUE;
}

static gboolean motion_notify_event_cb(GtkWidget* widget,
                                       GdkEventMotion* event) {
  UpdateLastInputEventTime(event);

  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return FALSE;
  }

  window->OnMotionNotifyEvent(event);

  return TRUE;
}

static gboolean button_press_event_cb(GtkWidget* widget,
                                      GdkEventButton* event) {
  UpdateLastInputEventTime(event);

  if (event->button == 2 && !StaticPrefs::widget_gtk_middle_click_enabled()) {
    return FALSE;
  }

  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return FALSE;
  }

  window->OnButtonPressEvent(event);

  return TRUE;
}

static gboolean button_release_event_cb(GtkWidget* widget,
                                        GdkEventButton* event) {
  UpdateLastInputEventTime(event);

  if (event->button == 2 && !StaticPrefs::widget_gtk_middle_click_enabled()) {
    return FALSE;
  }

  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return FALSE;
  }

  window->OnButtonReleaseEvent(event);

#ifdef MOZ_WAYLAND
  if (RefPtr<nsWindowWayland> w = window->AsWayland()) {
    w->WaylandDragWorkaround(event);
  }
#endif

  return TRUE;
}

static gboolean focus_in_event_cb(GtkWidget* widget, GdkEventFocus* event) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnContainerFocusInEvent(event);

  return FALSE;
}

static gboolean focus_out_event_cb(GtkWidget* widget, GdkEventFocus* event) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnContainerFocusOutEvent(event);

  return FALSE;
}

static gboolean key_press_event_cb(GtkWidget* widget, GdkEventKey* event) {
  LOGW("key_press_event_cb\n");

  UpdateLastInputEventTime(event);

  // find the window with focus and dispatch this event to that widget
  nsWindow* window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  RefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;

#ifdef MOZ_X11
  // Keyboard repeat can cause key press events to queue up when there are
  // slow event handlers (bug 301029).  Throttle these events by removing
  // consecutive pending duplicate KeyPress events to the same window.
  // We use the event time of the last one.
  // Note: GDK calls XkbSetDetectableAutorepeat so that KeyRelease events
  // are generated only when the key is physically released.
#  define NS_GDKEVENT_MATCH_MASK 0x1FFF  // GDK_SHIFT_MASK .. GDK_BUTTON5_MASK
  // Our headers undefine X11 KeyPress - let's redefine it here.
#  ifndef KeyPress
#    define KeyPress 2
#  endif
  if (StaticPrefs::widget_gtk_x11_key_repeat_throttle_enabled()) {
    GdkDisplay* gdkDisplay = gtk_widget_get_display(widget);
    if (GdkIsX11Display(gdkDisplay)) {
      Display* dpy = GDK_DISPLAY_XDISPLAY(gdkDisplay);
      while (XPending(dpy)) {
        XEvent next_event;
        XPeekEvent(dpy, &next_event);
        GdkWindow* nextGdkWindow = gdk_x11_window_lookup_for_display(
            gdkDisplay, next_event.xany.window);
        if (nextGdkWindow != event->window || next_event.type != KeyPress ||
            next_event.xkey.keycode != event->hardware_keycode ||
            next_event.xkey.state != (event->state & NS_GDKEVENT_MATCH_MASK)) {
          break;
        }
        XNextEvent(dpy, &next_event);
        event->time = next_event.xkey.time;
      }
    }
  }
#endif

  return focusWindow->OnKeyPressEvent(event);
}

static gboolean key_release_event_cb(GtkWidget* widget, GdkEventKey* event) {
  LOGW("key_release_event_cb\n");

  UpdateLastInputEventTime(event);

  // find the window with focus and dispatch this event to that widget
  nsWindow* window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  RefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;
  return focusWindow->OnKeyReleaseEvent(event);
}

static gboolean property_notify_event_cb(GtkWidget* aWidget,
                                         GdkEventProperty* aEvent) {
  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(aEvent->window);
  if (!window) {
    return FALSE;
  }

  return window->OnPropertyNotifyEvent(aWidget, aEvent);
}

static gboolean scroll_event_cb(GtkWidget* widget, GdkEventScroll* event) {
  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (NS_WARN_IF(!window)) {
    return FALSE;
  }

  window->OnScrollEvent(event);

  return TRUE;
}

static gboolean visibility_notify_event_cb(GtkWidget* widget,
                                           GdkEventVisibility* event) {
  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);
  if (!window) {
    return FALSE;
  }
  window->OnVisibilityNotifyEvent(event->state);
  return TRUE;
}

static void hierarchy_changed_cb(GtkWidget* widget,
                                 GtkWidget* previous_toplevel) {
  GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
  GdkWindowState old_window_state = GDK_WINDOW_STATE_WITHDRAWN;
  GdkEventWindowState event;

  event.new_window_state = GDK_WINDOW_STATE_WITHDRAWN;

  if (GTK_IS_WINDOW(previous_toplevel)) {
    g_signal_handlers_disconnect_by_func(
        previous_toplevel, FuncToGpointer(window_state_event_cb), widget);
    GdkWindow* win = gtk_widget_get_window(previous_toplevel);
    if (win) {
      old_window_state = gdk_window_get_state(win);
    }
  }

  if (GTK_IS_WINDOW(toplevel)) {
    g_signal_connect_swapped(toplevel, "window-state-event",
                             G_CALLBACK(window_state_event_cb), widget);
    GdkWindow* win = gtk_widget_get_window(toplevel);
    if (win) {
      event.new_window_state = gdk_window_get_state(win);
    }
  }

  event.changed_mask =
      static_cast<GdkWindowState>(old_window_state ^ event.new_window_state);

  if (event.changed_mask) {
    event.type = GDK_WINDOW_STATE;
    event.window = nullptr;
    event.send_event = TRUE;
    window_state_event_cb(widget, &event);
  }
}

static gboolean window_state_event_cb(GtkWidget* widget,
                                      GdkEventWindowState* event) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnWindowStateEvent(widget, event);

  return FALSE;
}

static void settings_xft_dpi_changed_cb(GtkSettings* gtk_settings,
                                        GParamSpec* pspec, nsWindow* data) {
  RefPtr<nsWindow> window = data;
  window->OnDPIChanged();
  // Even though the window size in screen pixels has not changed,
  // nsViewManager stores the dimensions in app units.
  // DispatchResized() updates those.
  window->DispatchResized();
}

static void check_resize_cb(GtkContainer* container, gpointer user_data) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(GTK_WIDGET(container));
  if (!window) {
    return;
  }
  window->OnCheckResize();
}

static void screen_composited_changed_cb(GdkScreen* screen,
                                         gpointer user_data) {
  // This callback can run before gfxPlatform::Init() in rare
  // cases involving the profile manager. When this happens,
  // we have no reason to reset any compositors as graphics
  // hasn't been initialized yet.
  if (GPUProcessManager::Get()) {
    GPUProcessManager::Get()->ResetCompositors();
  }
}

static void widget_composited_changed_cb(GtkWidget* widget,
                                         gpointer user_data) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return;
  }
  window->OnCompositedChanged();
}

static void scale_changed_cb(GtkWidget* widget, GParamSpec* aPSpec,
                             gpointer aPointer) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return;
  }

  window->OnScaleEvent();
}

static gboolean touch_event_cb(GtkWidget* aWidget, GdkEventTouch* aEvent) {
  UpdateLastInputEventTime(aEvent);

  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(aEvent->window);
  if (!window) {
    return FALSE;
  }

  return window->OnTouchEvent(aEvent);
}

// This function called generic because there is no signal specific to touchpad
// pinch events.
static gboolean generic_event_cb(GtkWidget* widget, GdkEvent* aEvent) {
  if (aEvent->type != GDK_TOUCHPAD_PINCH) {
    return FALSE;
  }
  // Using reinterpret_cast because the touchpad_pinch field of GdkEvent is not
  // available in GTK+ versions lower than v3.18
  GdkEventTouchpadPinch* event =
      reinterpret_cast<GdkEventTouchpadPinch*>(aEvent);

  RefPtr<nsWindow> window = nsWindow::FromGdkWindow(event->window);

  if (!window) {
    return FALSE;
  }
  return window->OnTouchpadPinchEvent(event);
}

void nsWindow::GtkWidgetDestroyHandler(GtkWidget* aWidget) {
  if (!mIsDestroyed) {
    NS_WARNING("GtkWidgetDestroyHandler called for live nsWindow!");
    Destroy();
  }
  if (aWidget == mShell) {
    mShell = nullptr;
    return;
  }
}

void widget_destroy_cb(GtkWidget* widget, gpointer user_data) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(widget);
  if (!window) {
    return;
  }
  window->GtkWidgetDestroyHandler(widget);
}

//////////////////////////////////////////////////////////////////////
// These are all of our drag and drop operations

void nsWindow::InitDragEvent(WidgetDragEvent& aEvent) {
  // set the keyboard modifiers
  guint modifierState = KeymapWrapper::GetCurrentModifierState();
  KeymapWrapper::InitInputEvent(aEvent, modifierState);
}

static nsresult initialize_prefs(void) {
  if (Preferences::HasUserValue("widget.use-aspect-ratio")) {
    gUseAspectRatio = Preferences::GetBool("widget.use-aspect-ratio", true);
  } else {
    gUseAspectRatio = IsGnomeDesktopEnvironment() || IsKdeDesktopEnvironment();
  }
  // 'Stable' Wayland subsurface rounding algorithm is used by all compositors
  // except KDE.
  gUseStableRounding = !IsKdeDesktopEnvironment() || GdkIsX11Display();
  return NS_OK;
}

#ifdef ACCESSIBILITY
void nsWindow::CreateRootAccessible() {
  if (!mRootAccessible) {
    LOG("nsWindow:: Create Toplevel Accessibility\n");
    mRootAccessible = GetRootAccessible();
  }
}

void nsWindow::DispatchEventToRootAccessible(uint32_t aEventType) {
  if (!a11y::ShouldA11yBeEnabled()) {
    return;
  }

  nsAccessibilityService* accService = GetOrCreateAccService();
  if (!accService) {
    return;
  }

  // Get the root document accessible and fire event to it.
  CreateRootAccessible();
  if (mRootAccessible) {
    accService->FireAccessibleEvent(aEventType, mRootAccessible);
  }
}

void nsWindow::DispatchActivateEventAccessible(void) {
  DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_ACTIVATE);
}

void nsWindow::DispatchDeactivateEventAccessible(void) {
  DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_DEACTIVATE);
}

void nsWindow::DispatchMaximizeEventAccessible(void) {
  DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_MAXIMIZE);
}

void nsWindow::DispatchMinimizeEventAccessible(void) {
  DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_MINIMIZE);
}

void nsWindow::DispatchRestoreEventAccessible(void) {
  DispatchEventToRootAccessible(nsIAccessibleEvent::EVENT_WINDOW_RESTORE);
}

#endif /* #ifdef ACCESSIBILITY */

void nsWindow::SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) {
  if (!mIMContext) {
    return;
  }
  mIMContext->SetInputContext(this, &aContext, &aAction);
}

InputContext nsWindow::GetInputContext() {
  InputContext context;
  if (!mIMContext) {
    context.mIMEState.mEnabled = IMEEnabled::Disabled;
    context.mIMEState.mOpen = IMEState::OPEN_STATE_NOT_SUPPORTED;
  } else {
    context = mIMContext->GetInputContext();
  }
  return context;
}

TextEventDispatcherListener* nsWindow::GetNativeTextEventDispatcherListener() {
  if (NS_WARN_IF(!mIMContext)) {
    return nullptr;
  }
  return mIMContext;
}

bool nsWindow::GetEditCommands(NativeKeyBindingsType aType,
                               const WidgetKeyboardEvent& aEvent,
                               nsTArray<CommandInt>& aCommands) {
  // Validate the arguments.
  if (NS_WARN_IF(!nsIWidget::GetEditCommands(aType, aEvent, aCommands))) {
    return false;
  }

  Maybe<WritingMode> writingMode;
  if (aEvent.NeedsToRemapNavigationKey()) {
    if (RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcher()) {
      writingMode = dispatcher->MaybeQueryWritingModeAtSelection();
    }
  }

  NativeKeyBindings* keyBindings = NativeKeyBindings::GetInstance(aType);
  keyBindings->GetEditCommands(aEvent, writingMode, aCommands);
  return true;
}

already_AddRefed<DrawTarget> nsWindow::StartRemoteDrawingInRegion(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  return mSurfaceProvider.StartRemoteDrawingInRegion(aInvalidRegion);
}

void nsWindow::EndRemoteDrawingInRegion(
    DrawTarget* aDrawTarget, const LayoutDeviceIntRegion& aInvalidRegion) {
  mSurfaceProvider.EndRemoteDrawingInRegion(aDrawTarget, aInvalidRegion);
}

bool nsWindow::GetDragInfo(WidgetMouseEvent* aMouseEvent, GdkWindow** aWindow,
                           gint* aButton, gint* aRootX, gint* aRootY) {
  if (aMouseEvent->mButton != MouseButton::ePrimary) {
    // we can only begin a move drag with the left mouse button
    return false;
  }
  *aButton = 1;

  // get the gdk window for this widget
  GdkWindow* gdk_window = mGdkWindow;
  if (!gdk_window) {
    return false;
  }
#ifdef DEBUG
  // GDK_IS_WINDOW(...) expands to a statement-expression, and
  // statement-expressions are not allowed in template-argument lists. So we
  // have to make the MOZ_ASSERT condition indirect.
  if (!GDK_IS_WINDOW(gdk_window)) {
    MOZ_ASSERT(false, "must really be window");
  }
#endif

  // find the top-level window
  gdk_window = gdk_window_get_toplevel(gdk_window);
  MOZ_ASSERT(gdk_window, "gdk_window_get_toplevel should not return null");
  *aWindow = gdk_window;

  if (!aMouseEvent->mWidget) {
    return false;
  }

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    // Workaround for https://bugzilla.gnome.org/show_bug.cgi?id=789054
    // To avoid crashes disable double-click on WM without _NET_WM_MOVERESIZE.
    // See _should_perform_ewmh_drag() at gdkwindow-x11.c
    // XXXsmaug remove this old hack. gtk should be fixed now.
    GdkScreen* screen = gdk_window_get_screen(gdk_window);
    GdkAtom atom = gdk_atom_intern("_NET_WM_MOVERESIZE", FALSE);
    if (!gdk_x11_screen_supports_net_wm_hint(screen, atom)) {
      static TimeStamp lastTimeStamp;
      if (lastTimeStamp != aMouseEvent->mTimeStamp) {
        lastTimeStamp = aMouseEvent->mTimeStamp;
      } else {
        return false;
      }
    }
  }
#endif

  // FIXME: It would be nice to have the widget position at the time
  // of the event, but it's relatively unlikely that the widget has
  // moved since the mousedown.  (On the other hand, it's quite likely
  // that the mouse has moved, which is why we use the mouse position
  // from the event.)
  LayoutDeviceIntPoint offset = aMouseEvent->mWidget->WidgetToScreenOffset();
  *aRootX = aMouseEvent->mRefPoint.x + offset.x;
  *aRootY = aMouseEvent->mRefPoint.y + offset.y;

  return true;
}

nsIWidget::WindowRenderer* nsWindow::GetWindowRenderer() {
  if (mIsDestroyed) {
    // Prevent external code from triggering the re-creation of the
    // LayerManager/Compositor during shutdown. Just return what we currently
    // have, which is most likely null.
    return mWindowRenderer;
  }

  return nsIWidget::GetWindowRenderer();
}

void nsWindow::DidGetNonBlankPaint() {
  if (mGotNonBlankPaint) {
    return;
  }
  mGotNonBlankPaint = true;
  if (!mConfiguredClearColor) {
    // Nothing to do, we hadn't overridden the clear color.
    mConfiguredClearColor = true;
    return;
  }
  // Reset the clear color set in the expose event to transparent.
  GetWindowRenderer()->AsWebRender()->WrBridge()->SendSetDefaultClearColor(
      NS_TRANSPARENT);
}

/* nsWindow::SetCompositorWidgetDelegate() sets remote GtkCompositorWidget
 * to render into with compositor.
 *
 * SetCompositorWidgetDelegate(delegate) is called from
 * nsIWidget::CreateCompositor(), i.e. nsWindow::GetWindowRenderer().
 *
 * SetCompositorWidgetDelegate(null) is called from
 * nsIWidget::DestroyCompositor().
 */
void nsWindow::SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) {
  LOG("nsWindow::SetCompositorWidgetDelegate %p mIsMapped %d "
      "mCompositorWidgetDelegate %p\n",
      delegate, !!mIsMapped, mCompositorWidgetDelegate);

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  mCompositorWidgetDelegate =
      delegate ? delegate->AsPlatformSpecificDelegate() : nullptr;

  if (mCompositorWidgetDelegate && GdkIsX11Display()) {
    CompositorBridgeChild* remoteRenderer = GetRemoteRenderer();
    MOZ_RELEASE_ASSERT(remoteRenderer);
    remoteRenderer->SendResume();
    remoteRenderer->SendForcePresent(wr::RenderReasons::WIDGET);
  }
}

bool nsWindow::IsAlwaysUndecoratedWindow() const {
  if (mPiPType == PiPType::MediaPiP || gKioskMode) {
    return true;
  }
  if (mWindowType == WindowType::Dialog &&
      mBorderStyle != BorderStyle::Default &&
      mBorderStyle != BorderStyle::All &&
      !(mBorderStyle & BorderStyle::Title) &&
      !(mBorderStyle & BorderStyle::ResizeH)) {
    return true;
  }
  return false;
}

void nsWindow::SetCustomTitlebar(bool aState) {
  LOG("nsWindow::SetCustomTitlebar() State %d mGtkWindowDecoration %d\n",
      aState, (int)mGtkWindowDecoration);

  if (mGtkWindowDecoration == GTK_DECORATION_NONE ||
      aState == mDrawInTitlebar || mIsDestroyed) {
    LOG("  already set, quit");
    return;
  }

  if (mUndecorated) {
    MOZ_ASSERT(aState, "Unexpected decoration request");
    MOZ_ASSERT(!gtk_window_get_decorated(GTK_WINDOW(mShell)));
    return;
  }

  mDrawInTitlebar = aState;

  if (mGtkWindowDecoration == GTK_DECORATION_SYSTEM) {
    SetWindowDecoration(aState ? BorderStyle::Border : mBorderStyle);
  } else if (mGtkWindowDecoration == GTK_DECORATION_CLIENT) {
    LOG("    Using CSD mode\n");

    // We need to disable/enable VSync as WaylandSurface unmap
    // removes all callbacks. Will be fixed by Bug 2000840.
    DisableVSyncSource();

    /* Window manager does not support GDK_DECOR_BORDER,
     * emulate it by CSD.
     *
     * gtk_window_set_titlebar() works on unrealized widgets only,
     * we need to handle mShell carefully here.
     * When CSD is enabled mGdkWindow is owned by mContainer which is good
     * as we can't delete our mGdkWindow. To make mShell unrealized while
     * mContainer is preserved we temporary reparent mContainer to an
     * invisible GtkWindow.
     */
    bool visible = !mNeedsShow && mIsShown;
    if (visible) {
      NativeShow(false);
    }

    // Using GTK_WINDOW_POPUP rather than
    // GTK_WINDOW_TOPLEVEL in the hope that POPUP results in less
    // initialization and window manager interaction.
    GtkWidget* tmpWindow = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_realize(tmpWindow);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_widget_reparent(GTK_WIDGET(mContainer), tmpWindow);
    gtk_widget_unrealize(GTK_WIDGET(mShell));

    // Add a hidden titlebar widget to trigger CSD, but disable the default
    // titlebar.  GtkFixed is a somewhat random choice for a simple unused
    // widget. gtk_window_set_titlebar() takes ownership of the titlebar
    // widget.
    gtk_window_set_titlebar(GTK_WINDOW(mShell),
                            aState ? gtk_fixed_new() : nullptr);

    /* A workaround for https://bugzilla.gnome.org/show_bug.cgi?id=791081
     * gtk_widget_realize() throws:
     * "In pixman_region32_init_rect: Invalid rectangle passed"
     * when mShell has default 1x1 size.
     */
    GtkAllocation allocation = {0, 0, 0, 0};
    gtk_widget_get_preferred_width(GTK_WIDGET(mShell), nullptr,
                                   &allocation.width);
    gtk_widget_get_preferred_height(GTK_WIDGET(mShell), nullptr,
                                    &allocation.height);
    gtk_widget_size_allocate(GTK_WIDGET(mShell), &allocation);

    gtk_widget_realize(GTK_WIDGET(mShell));
    gtk_widget_reparent(GTK_WIDGET(mContainer), GTK_WIDGET(mShell));
#pragma GCC diagnostic pop

    if (AreBoundsSane()) {
      gtk_window_resize(GTK_WINDOW(mShell), mClientArea.width,
                        mClientArea.height);
    }

    ConfigureToplevelWindow();

    if (visible) {
      mNeedsShow = true;
      NativeShow(true);
    }
    EnableVSyncSource();
    gtk_widget_destroy(tmpWindow);
  }

  // Recompute the input region (which should generally be null, but this is
  // enough to work around bug 1844497, which is probably a gtk bug).
  SetInputRegion(mInputRegion);
}

GtkWindow* nsWindow::GetCurrentTopmostWindow() const {
  GtkWindow* parentWindow = GTK_WINDOW(GetGtkWidget());
  GtkWindow* topmostParentWindow = nullptr;
  while (parentWindow) {
    topmostParentWindow = parentWindow;
    parentWindow = gtk_window_get_transient_for(parentWindow);
  }
  return topmostParentWindow;
}

gint nsWindow::GdkCeiledScaleFactor() {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  // mCeiledScaleFactor is set by notify::scale-factor callback
  // for visible windows.
  if (mCeiledScaleFactor != sNoScale) {
    LOGVERBOSE("nsWindow::GdkCeiledScaleFactor(): ceiled scale %d",
               (int)mCeiledScaleFactor);
    return mCeiledScaleFactor;
  }

  // We're missing scale for window (is hidden?), read parent scale
  if (nsWindow* topmost = nsWindow::FromWidget(GetTopLevelWidget())) {
    LOGVERBOSE("nsWindow::GdkCeiledScaleFactor(): toplevel [%p] scale %d",
               topmost, (int)topmost->mCeiledScaleFactor);
    return topmost->mCeiledScaleFactor;
  }

  LOGVERBOSE("nsWindow::GdkCeiledScaleFactor(): monitor scale %d",
             ScreenHelperGTK::GetGTKMonitorScaleFactor());
  return ScreenHelperGTK::GetGTKMonitorScaleFactor();
}

double nsWindow::FractionalScaleFactor() const {
#ifdef MOZ_WAYLAND
  if (mSurface) {
    auto scale = mSurface->GetScale();
    if (scale != sNoScale) {
#  ifdef MOZ_LOGGING
      if (LOG_ENABLED_VERBOSE()) {
        static float lastScaleLog = 0.0;
        if (lastScaleLog != scale) {
          lastScaleLog = scale;
          LOGVERBOSE("nsWindow::FractionalScaleFactor(): fractional scale %.2f",
                     scale);
        }
      }
#  endif
      return scale;
    }
  }
#endif
  return ScreenHelperGTK::GetGTKMonitorFractionalScaleFactor();
}

gint nsWindow::DevicePixelsToGdkCoordRound(int aPixels) {
  double scale = FractionalScaleFactor();
  return int(round(aPixels / scale));
}

gint nsWindow::DevicePixelsToGdkCoordRoundDown(int aPixels) {
  double scale = FractionalScaleFactor();
  return floor(aPixels / scale);
}

GdkPoint nsWindow::DevicePixelsToGdkPointRoundDown(
    const LayoutDeviceIntPoint& aPoint) {
  double scale = FractionalScaleFactor();
  return {int(aPoint.x / scale), int(aPoint.y / scale)};
}

GdkRectangle nsWindow::DevicePixelsToGdkRectRoundOut(
    const LayoutDeviceIntRect& aRect) {
  double scale = FractionalScaleFactor();
  int x = floor(aRect.x / scale);
  int y = floor(aRect.y / scale);
  int right = ceil((aRect.x + aRect.width) / scale);
  int bottom = ceil((aRect.y + aRect.height) / scale);
  return {x, y, right - x, bottom - y};
}

GdkRectangle nsWindow::DevicePixelsToGdkRectRoundIn(
    const LayoutDeviceIntRect& aRect) {
  double scale = FractionalScaleFactor();
  int x = ceil(aRect.x / scale);
  int y = ceil(aRect.y / scale);
  int right = floor((aRect.x + aRect.width) / scale);
  int bottom = floor((aRect.y + aRect.height) / scale);
  return {x, y, std::max(right - x, 0), std::max(bottom - y, 0)};
}

LayoutDeviceIntPoint nsWindow::GdkEventCoordsToDevicePixels(gdouble aX,
                                                            gdouble aY) {
  double scale = FractionalScaleFactor();
  return LayoutDeviceIntPoint::Floor((float)(aX * scale), (float)(aY * scale));
}

LayoutDeviceIntPoint nsWindow::GdkPointToDevicePixels(const GdkPoint& aPoint) {
  double scale = FractionalScaleFactor();
  return LayoutDeviceIntPoint::Floor((float)(aPoint.x * scale),
                                     (float)(aPoint.y * scale));
}

nsresult nsWindow::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::NativeModifiers aModifierFlags,
    nsISynthesizedEventCallback* aCallback) {
  LOG("SynthesizeNativeMouseEvent(%d, %d, %d, %d, %d)", aPoint.x.value,
      aPoint.y.value, int(aNativeMessage), int(aButton),
      static_cast<int>(aModifierFlags));

  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  if (!mGdkWindow) {
    return NS_OK;
  }

  // When a button-press/release event is requested, create it here and put it
  // in the event queue. This will not emit a motion event - this needs to be
  // done explicitly *before* requesting a button-press/release. You will also
  // need to wait for the motion event to be dispatched before requesting a
  // button-press/release event to maintain the desired event order.
  switch (aNativeMessage) {
    case NativeMouseMessage::ButtonDown:
    case NativeMouseMessage::ButtonUp: {
      GdkEvent event;
      memset(&event, 0, sizeof(GdkEvent));
      event.type = aNativeMessage == NativeMouseMessage::ButtonDown
                       ? GDK_BUTTON_PRESS
                       : GDK_BUTTON_RELEASE;
      switch (aButton) {
        case MouseButton::ePrimary:
        case MouseButton::eMiddle:
        case MouseButton::eSecondary:
        case MouseButton::eX1:
        case MouseButton::eX2:
          event.button.button = aButton + 1;
          break;
        default:
          return NS_ERROR_INVALID_ARG;
      }
      event.button.state =
          KeymapWrapper::ConvertWidgetModifierToGdkState(aModifierFlags);
      event.button.window = mGdkWindow;
      event.button.time = GDK_CURRENT_TIME;

      // Get device for event source
      event.button.device = GdkGetPointer();

      event.button.x_root = DevicePixelsToGdkCoordRoundDown(aPoint.x);
      event.button.y_root = DevicePixelsToGdkCoordRoundDown(aPoint.y);

      LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
      event.button.x = DevicePixelsToGdkCoordRoundDown(pointInWindow.x);
      event.button.y = DevicePixelsToGdkCoordRoundDown(pointInWindow.y);

      gdk_event_put(&event);
      return NS_OK;
    }
    case NativeMouseMessage::Move: {
      // We don't support specific events other than button-press/release. In
      // all other cases we'll synthesize a motion event that will be emitted by
      // gdk_display_warp_pointer().
      // XXX How to activate native modifier for the other events?
#ifdef MOZ_WAYLAND
      // Impossible to warp the pointer on Wayland.
      // For pointer lock, pointer-constraints and relative-pointer are used.
      if (GdkIsWaylandDisplay()) {
        return NS_OK;
      }
#endif
      GdkScreen* screen = gdk_window_get_screen(mGdkWindow);
      GdkPoint point = DevicePixelsToGdkPointRoundDown(aPoint);
      gdk_device_warp(GdkGetPointer(), screen, point.x, point.y);
      return NS_OK;
    }
    case NativeMouseMessage::EnterWindow:
    case NativeMouseMessage::LeaveWindow:
      MOZ_ASSERT_UNREACHABLE("Non supported mouse event on Linux");
      return NS_ERROR_INVALID_ARG;
  }
  return NS_ERROR_UNEXPECTED;
}

void nsWindow::CreateAndPutGdkScrollEvent(mozilla::LayoutDeviceIntPoint aPoint,
                                          double aDeltaX, double aDeltaY) {
  GdkEvent event;
  memset(&event, 0, sizeof(GdkEvent));
  event.type = GDK_SCROLL;
  event.scroll.window = mGdkWindow;
  event.scroll.time = GDK_CURRENT_TIME;
  // Get device for event source
  GdkDisplay* display = gdk_window_get_display(mGdkWindow);
  GdkDeviceManager* device_manager = gdk_display_get_device_manager(display);
  // See note in nsWindow::SynthesizeNativeTouchpadPan about the device we use
  // here.
  event.scroll.device = gdk_device_manager_get_client_pointer(device_manager);
  event.scroll.x_root = DevicePixelsToGdkCoordRoundDown(aPoint.x);
  event.scroll.y_root = DevicePixelsToGdkCoordRoundDown(aPoint.y);

  LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
  event.scroll.x = DevicePixelsToGdkCoordRoundDown(pointInWindow.x);
  event.scroll.y = DevicePixelsToGdkCoordRoundDown(pointInWindow.y);

  // The delta values are backwards on Linux compared to Windows and Cocoa,
  // hence the negation.
  event.scroll.direction = GDK_SCROLL_SMOOTH;
  event.scroll.delta_x = -aDeltaX;
  event.scroll.delta_y = -aDeltaY;

  gdk_event_put(&event);
}

nsresult nsWindow::SynthesizeNativeMouseScrollEvent(
    mozilla::LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage,
    double aDeltaX, double aDeltaY, double aDeltaZ,
    nsIWidget::NativeModifiers aModifierFlags, uint32_t aAdditionalFlags,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  if (!mGdkWindow) {
    return NS_OK;
  }

  CreateAndPutGdkScrollEvent(aPoint, aDeltaX, aDeltaY);

  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeTouchPoint(
    uint32_t aPointerId, TouchPointerState aPointerState,
    LayoutDeviceIntPoint aPoint, double aPointerPressure,
    uint32_t aPointerOrientation, nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  if (!mGdkWindow) {
    return NS_OK;
  }

  GdkEvent event;
  memset(&event, 0, sizeof(GdkEvent));

  static std::map<uint32_t, GdkEventSequence*> sKnownPointers;

  auto result = sKnownPointers.find(aPointerId);
  switch (aPointerState) {
    case TOUCH_CONTACT:
      if (result == sKnownPointers.end()) {
        // GdkEventSequence isn't a thing we can instantiate, and never gets
        // dereferenced in the gtk code. It's an opaque pointer, the only
        // requirement is that it be distinct from other instances of
        // GdkEventSequence*.
        event.touch.sequence = (GdkEventSequence*)((uintptr_t)aPointerId);
        sKnownPointers[aPointerId] = event.touch.sequence;
        event.type = GDK_TOUCH_BEGIN;
      } else {
        event.touch.sequence = result->second;
        event.type = GDK_TOUCH_UPDATE;
      }
      break;
    case TOUCH_REMOVE:
      event.type = GDK_TOUCH_END;
      if (result == sKnownPointers.end()) {
        NS_WARNING("Tried to synthesize touch-end for unknown pointer!");
        return NS_ERROR_UNEXPECTED;
      }
      event.touch.sequence = result->second;
      sKnownPointers.erase(result);
      break;
    case TOUCH_CANCEL:
      event.type = GDK_TOUCH_CANCEL;
      if (result == sKnownPointers.end()) {
        NS_WARNING("Tried to synthesize touch-cancel for unknown pointer!");
        return NS_ERROR_UNEXPECTED;
      }
      event.touch.sequence = result->second;
      sKnownPointers.erase(result);
      break;
    case TOUCH_HOVER:
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }

  event.touch.window = mGdkWindow;
  event.touch.time = GDK_CURRENT_TIME;

  GdkDisplay* display = gdk_window_get_display(mGdkWindow);
  GdkDeviceManager* device_manager = gdk_display_get_device_manager(display);
  event.touch.device = gdk_device_manager_get_client_pointer(device_manager);

  event.touch.x_root = DevicePixelsToGdkCoordRoundDown(aPoint.x);
  event.touch.y_root = DevicePixelsToGdkCoordRoundDown(aPoint.y);

  LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
  event.touch.x = DevicePixelsToGdkCoordRoundDown(pointInWindow.x);
  event.touch.y = DevicePixelsToGdkCoordRoundDown(pointInWindow.y);

  gdk_event_put(&event);

  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeTouchPadPinch(
    TouchpadGesturePhase aEventPhase, float aScale, LayoutDeviceIntPoint aPoint,
    int32_t aModifierFlags) {
  if (!mGdkWindow) {
    return NS_OK;
  }
  GdkEvent event;
  memset(&event, 0, sizeof(GdkEvent));

  GdkEventTouchpadPinch* touchpad_event =
      reinterpret_cast<GdkEventTouchpadPinch*>(&event);
  touchpad_event->type = GDK_TOUCHPAD_PINCH;

  const ScreenIntPoint widgetToScreenOffset = ViewAs<ScreenPixel>(
      WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

  ScreenPoint pointInWindow =
      ViewAs<ScreenPixel>(
          aPoint,
          PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent) -
      widgetToScreenOffset;

  gdouble dx = 0, dy = 0;

  switch (aEventPhase) {
    case PHASE_BEGIN:
      touchpad_event->phase = GDK_TOUCHPAD_GESTURE_PHASE_BEGIN;
      mCurrentSynthesizedTouchpadPinch = {pointInWindow, pointInWindow};
      break;
    case PHASE_UPDATE:
      dx = pointInWindow.x - mCurrentSynthesizedTouchpadPinch.mCurrentFocus.x;
      dy = pointInWindow.y - mCurrentSynthesizedTouchpadPinch.mCurrentFocus.y;
      mCurrentSynthesizedTouchpadPinch.mCurrentFocus = pointInWindow;
      touchpad_event->phase = GDK_TOUCHPAD_GESTURE_PHASE_UPDATE;
      break;
    case PHASE_END:
      touchpad_event->phase = GDK_TOUCHPAD_GESTURE_PHASE_END;
      break;

    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }

  touchpad_event->window = mGdkWindow;
  // We only set the fields of GdkEventTouchpadPinch which are
  // actually used in OnTouchpadPinchEvent().
  // GdkEventTouchpadPinch has additional fields.
  // If OnTouchpadPinchEvent() is changed to use other fields, this function
  // will need to change to set them as well.
  touchpad_event->time = GDK_CURRENT_TIME;
  touchpad_event->scale = aScale;
  touchpad_event->x_root = DevicePixelsToGdkCoordRoundDown(
      mCurrentSynthesizedTouchpadPinch.mBeginFocus.x +
      ScreenCoord(widgetToScreenOffset.x));
  touchpad_event->y_root = DevicePixelsToGdkCoordRoundDown(
      mCurrentSynthesizedTouchpadPinch.mBeginFocus.y +
      ScreenCoord(widgetToScreenOffset.y));

  touchpad_event->x = DevicePixelsToGdkCoordRoundDown(
      mCurrentSynthesizedTouchpadPinch.mBeginFocus.x);
  touchpad_event->y = DevicePixelsToGdkCoordRoundDown(
      mCurrentSynthesizedTouchpadPinch.mBeginFocus.y);

  touchpad_event->dx = dx;
  touchpad_event->dy = dy;

  touchpad_event->state = aModifierFlags;

  gdk_event_put(&event);

  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeTouchpadPan(
    TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
    double aDeltaX, double aDeltaY, int32_t aModifierFlags,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  if (!mGdkWindow) {
    return NS_OK;
  }

  // This should/could maybe send GdkEventTouchpadSwipe events, however we don't
  // currently consume those (either real user input or testing events). So we
  // send gdk scroll events to be more like what we do for real user input. If
  // we start consuming GdkEventTouchpadSwipe and get those hooked up to swipe
  // to nav, then maybe we should test those too.

  mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase = Some(aEventPhase);
  MOZ_ASSERT(mCurrentSynthesizedTouchpadPan.mSavedCallbackId.isNothing());
  mCurrentSynthesizedTouchpadPan.mSavedCallbackId = notifier.SaveCallback();
  // Note that CreateAndPutGdkScrollEvent sets the device source for the created
  // event as the "client pointer" (a kind of default device) which will
  // probably be of type mouse. We would ideally want to set the device of the
  // created event to be a touchpad, but the system might not have a touchpad.
  // To get around this we use
  // mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase being something to
  // indicate that we should treat the source of the event as touchpad in
  // OnScrollEvent.
  CreateAndPutGdkScrollEvent(aPoint, aDeltaX, aDeltaY);

  return NS_OK;
}

nsWindow::GtkWindowDecoration nsWindow::GetSystemGtkWindowDecoration() {
  static GtkWindowDecoration sGtkWindowDecoration = [] {
    // Allow MOZ_GTK_TITLEBAR_DECORATION to override our heuristics
    if (const char* decorationOverride =
            getenv("MOZ_GTK_TITLEBAR_DECORATION")) {
      if (strcmp(decorationOverride, "none") == 0) {
        return GTK_DECORATION_NONE;
      }
      if (strcmp(decorationOverride, "client") == 0) {
        return GTK_DECORATION_CLIENT;
      }
      if (strcmp(decorationOverride, "system") == 0) {
        return GTK_DECORATION_SYSTEM;
      }
    }

    // nsWindow::GetSystemGtkWindowDecoration can be called from various
    // threads so we can't use gfxPlatformGtk here.
    if (GdkIsWaylandDisplay()) {
      return GTK_DECORATION_CLIENT;
    }

    // GTK_CSD forces CSD mode - use also CSD because window manager
    // decorations does not work with CSD.
    // We check GTK_CSD as well as gtk_window_should_use_csd() does.
    auto env = GetGtkCSDEnv();
    if (env != GtkCsd::Unset) {
      return env == GtkCsd::Zero ? GTK_DECORATION_NONE : GTK_DECORATION_CLIENT;
    }

    // TODO: Consider switching this to GetDesktopEnvironmentIdentifier().
    const char* currentDesktop = getenv("XDG_CURRENT_DESKTOP");
    if (!currentDesktop) {
      return GTK_DECORATION_NONE;
    }
    if (strstr(currentDesktop, "i3")) {
      return GTK_DECORATION_NONE;
    }

    // Tested desktops: pop:GNOME, KDE, Enlightenment, LXDE, openbox, MATE,
    // X-Cinnamon, Pantheon, Deepin, GNOME, LXQt, Unity.
    return GTK_DECORATION_CLIENT;
  }();
  return sGtkWindowDecoration;
}

void nsWindow::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  MOZ_DIAGNOSTIC_ASSERT(!mIsDestroyed);

  LOG("nsWindow::GetCompositorWidgetInitData");

  nsCString displayName;
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    // Make sure the window XID is propagated to X server, we can fail otherwise
    // in GPU process (Bug 1401634).
    Display* display = DefaultXDisplay();
    XFlush(display);
    displayName = nsCString(XDisplayString(display));
  }
#endif

  // Wayland popups are painted at 0,0 but we use mClientArea.x/y as popup
  // position so we can't use it for rounding of size coordinates.
  auto clientSize = gUseStableRounding && !IsWaylandPopup()
                        ? GetClientSize()
                        : LayoutDeviceIntSize::Round(mClientArea.Size() *
                                                     GetDesktopToDeviceScale());

  *aInitData = mozilla::widget::GtkCompositorWidgetInitData(
      GetX11Window(), displayName, GdkIsX11Display(), clientSize);
}

nsresult nsWindow::SetSystemFont(const nsCString& aFontName) {
  GtkSettings* settings = gtk_settings_get_default();
  g_object_set(settings, "gtk-font-name", aFontName.get(), nullptr);
  return NS_OK;
}

nsresult nsWindow::GetSystemFont(nsCString& aFontName) {
  GtkSettings* settings = gtk_settings_get_default();
  gchar* fontName = nullptr;
  g_object_get(settings, "gtk-font-name", &fontName, nullptr);
  if (fontName) {
    aFontName.Assign(fontName);
    g_free(fontName);
  }
  return NS_OK;
}

static already_AddRefed<nsIWidget> CreateWindow() {
  nsCOMPtr<nsIWidget> window;
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    window = new nsWindowWayland();
  }
#endif
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    window = new nsWindowX11();
  }
#endif
  MOZ_DIAGNOSTIC_ASSERT(window);
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = CreateWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = CreateWindow();
  return window.forget();
}

static nsIFrame* FindTitlebarFrame(nsIFrame* aFrame) {
  for (nsIFrame* childFrame : aFrame->PrincipalChildList()) {
    StyleAppearance appearance =
        childFrame->StyleDisplay()->EffectiveAppearance();
    if (appearance == StyleAppearance::MozWindowTitlebar ||
        appearance == StyleAppearance::MozWindowTitlebarMaximized) {
      return childFrame;
    }

    if (nsIFrame* foundFrame = FindTitlebarFrame(childFrame)) {
      return foundFrame;
    }
  }
  return nullptr;
}

void nsWindow::UpdateMozWindowActive() {
  // Update activation state for the :-moz-window-inactive pseudoclass.
  // Normally, this follows focus; we override it here to follow
  // GDK_WINDOW_STATE_FOCUSED.
  if (mozilla::dom::Document* document = GetDocument()) {
    if (nsPIDOMWindowOuter* window = document->GetWindow()) {
      if (RefPtr<mozilla::dom::BrowsingContext> bc =
              window->GetBrowsingContext()) {
        bc->SetIsActiveBrowserWindow(!mTitlebarBackdropState);
      }
    }
  }
}

void nsWindow::ForceTitlebarRedraw() {
  MOZ_ASSERT(mDrawInTitlebar, "We should not redraw invisible titlebar.");
  PresShell* ps = GetPresShell();
  if (!ps) {
    return;
  }
  nsIFrame* frame = ps->GetRootFrame();
  if (!frame) {
    return;
  }
  frame = FindTitlebarFrame(frame);
  if (frame) {
    nsIContent* content = frame->GetContent();
    if (content) {
      nsLayoutUtils::PostRestyleEvent(content->AsElement(), RestyleHint{0},
                                      nsChangeHint_RepaintFrame);
    }
  }
}

void nsWindow::LockAspectRatio(bool aShouldLock) {
  if (!gUseAspectRatio) {
    return;
  }

  if (aShouldLock) {
    float width = mLastSizeRequest.width;
    float height = mLastSizeRequest.height;

    mAspectRatio = width / height;
    LOG("nsWindow::LockAspectRatio() width %.2f height %.2f aspect %.2f", width,
        height, mAspectRatio);
  } else {
    mAspectRatio = 0.0;
    LOG("nsWindow::LockAspectRatio() removed aspect ratio");
  }

  ApplySizeConstraints();
}

nsWindow* nsWindow::GetFocusedWindow() { return gFocusWindow; }

nsWindow* nsWindow::GetWindow(GdkWindow* window) {
  return nsWindow::FromGdkWindow(window);
}

// nsWindow::OnMap() / nsWindow::OnUnmap() is called from map/unmap mContainer
// handlers directly as we paint to mContainer.
void nsWindow::OnMap() {
  LOG("nsWindow::OnMap");

  mIsMapped = true;

  RefreshScale(/* aRefreshScreen */ false);

  if (mIsAlert) {
    gdk_window_set_override_redirect(GetToplevelGdkWindow(), TRUE);
  }

  if (mWindowType == WindowType::Popup) {
    // If the popup ignores mouse events, set an empty input shape.
    SetInputRegion(mInputRegion);
  }

  RefreshWindowClass();

  OnMapNative();

  LOG("  finished, GdkWindow %p XID 0x%lx\n", mGdkWindow, GetX11Window());
}

void nsWindow::OnUnmap() {
  LOG("nsWindow::OnUnmap");

#ifdef MOZ_WAYLAND
  if (AsWayland()) {
    AsWayland()->ClearPipResources();
  }
#endif

  {
    mIsMapped = false;
    mHasReceivedSizeAllocate = false;

    if (mSourceDragContext) {
      static auto sGtkDragCancel =
          (void (*)(GdkDragContext*))dlsym(RTLD_DEFAULT, "gtk_drag_cancel");
      if (sGtkDragCancel) {
        LOGDRAG("nsWindow::OnUnmap() Drag cancel");
        sGtkDragCancel(mSourceDragContext);
        mSourceDragContext = nullptr;
      }
    }

    // Reset scale for hidden windows
    mCeiledScaleFactor = sNoScale;
  }

  // Until bug 1654938 is fixed we delete layer manager for hidden popups,
  // otherwise it can easily hold 1GB+ memory for a long time.
  //
  // Don't do this for temporarily hidden popups tho, as remote content is not
  // set up to survive the layer manager destruction of the containing window,
  // see bug 1958695.
  if (mWindowType == WindowType::Popup && !mPopupTemporaryHidden) {
    DestroyLayerManager();
  }
}

void nsWindow::NotifyOcclusionState(OcclusionState aState) {
  if (!IsTopLevelWidget()) {
    return;
  }

  bool isFullyOccluded = aState == OcclusionState::OCCLUDED;
  if (mIsFullyOccluded == isFullyOccluded) {
    return;
  }
  mIsFullyOccluded = isFullyOccluded;

  LOG("nsWindow::NotifyOcclusionState() mIsFullyOccluded %d", mIsFullyOccluded);
  if (mWidgetListener) {
    mWidgetListener->OcclusionStateChanged(mIsFullyOccluded);
  }
}

void nsWindow::SetDragSource(GdkDragContext* aSourceDragContext) {
  mSourceDragContext = aSourceDragContext;
  if (IsPopup() &&
      (widget::GdkIsWaylandDisplay() || widget::IsXWaylandProtocol())) {
    if (auto* menuPopupFrame = GetPopupFrame()) {
      menuPopupFrame->SetIsDragSource(!!aSourceDragContext);
    }
  }
}

UniquePtr<WaylandSurfaceLock> nsWindow::LockSurface() {
#ifdef MOZ_WAYLAND
  if (mIsDestroyed || !mSurface) {
    return nullptr;
  }
  return MakeUnique<WaylandSurfaceLock>(MOZ_WL_SURFACE(mContainer));
#else
  return nullptr;
#endif
}

using GdkWaylandWindowExported = void (*)(GdkWindow* window, const char* handle,
                                          gpointer user_data);

RefPtr<nsWindow::ExportHandlePromise> nsWindow::ExportHandle() {
  auto promise = MakeRefPtr<ExportHandlePromise::Private>(__func__);
  auto* toplevel = GetToplevelGdkWindow();
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    static auto sGdkWaylandWindowExportHandle = (gboolean (*)(
        const GdkWindow*, GdkWaylandWindowExported, gpointer,
        GDestroyNotify))dlsym(RTLD_DEFAULT, "gdk_wayland_window_export_handle");
    if (!sGdkWaylandWindowExportHandle || !toplevel) {
      promise->Reject(false, __func__);
    }
    const bool success = sGdkWaylandWindowExportHandle(
        toplevel,
        [](GdkWindow*, const char* handle, gpointer promise) {
          // NOTE: This addrefs, the releaser destroys.
          RefPtr self = static_cast<ExportHandlePromise::Private*>(promise);
          self->Resolve(nsPrintfCString("wayland:%s", handle), __func__);
        },
        promise.get(),
        [](gpointer promise) {
          RefPtr self =
              dont_AddRef(static_cast<ExportHandlePromise::Private*>(promise));
          // NOTE: This gets ignored if not pending.
          self->Reject(false, __func__);
        });
    if (success) {
      // Transfer ownership to the callback.
      promise.get()->AddRef();
    } else {
      promise->Reject(false, __func__);
    }
    return promise.forget();
  }
#endif
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    promise->Resolve(
        nsPrintfCString("x11:%lx", gdk_x11_window_get_xid(toplevel)), __func__);
    return promise.forget();
  }
#endif
  MOZ_ASSERT_UNREACHABLE("how?");
  promise->Reject(false, __func__);
  return promise.forget();
}

void nsWindow::UnexportHandle() {
  static auto sGdkWaylandWindowUnexportHandle = (void (*)(GdkWindow*))dlsym(
      RTLD_DEFAULT, "gdk_wayland_window_unexport_handle");
  if (GdkIsWaylandDisplay() && sGdkWaylandWindowUnexportHandle) {
    if (auto* toplevel = GetToplevelGdkWindow()) {
      sGdkWaylandWindowUnexportHandle(toplevel);
    }
  }
}

void nsWindow::SetTextInputArea(LayoutDeviceIntRect aCursorArea) {
  mIMContextInputArea = ToDesktopPixels(aCursorArea);
  LOG("nsWindow::SetTextInputArea() pos [%d, %d]", mIMContextInputArea.x,
      mIMContextInputArea.y);
}

void nsWindow::InsertEmoji(RefPtr<nsWindow> aToplevelWindow) {
  if (!StaticPrefs::widget_gtk_native_emoji_dialog()) {
    return;
  }

  if (IsTopLevelWidget()) {
    if (nsIWidget* popup =
            nsXULPopupManager::GetInstance()->GetRollupWidget()) {
      if (nsWindow* window = nsWindow::FromWidget(popup)) {
        LOG("nsWindow::InsertEmoji() - redirect to child popup [%p]", window);
        window->InsertEmoji(this);
      }
      return;
    }
  }

  if (!aToplevelWindow) {
    aToplevelWindow = this;
  }
  mozilla::widget::IMContextWrapper* IMContext =
      aToplevelWindow->GetIMContext();

  if (mIsDestroyed || !IMContext || !IMContext->IsEditable()) {
    LOG("nsWindow::InsertEmoji() failed, mIMContext [%p] editable [%d]",
        (void*)IMContext, IMContext ? IMContext->IsEditable() : 0);
    return;
  }

  GtkWidget* entry = moz_container_get_entry(MOZ_CONTAINER(mContainer));
  if (!entry) {
    entry = moz_container_entry_set(MOZ_CONTAINER(mContainer), gtk_entry_new());
    gtk_widget_show(entry);
    g_signal_connect(entry, "insert_text",
                     G_CALLBACK(+[](GtkWidget* entry, gchar* text, gint length,
                                    gint* position, gpointer data) {
                       nsWindow* window = static_cast<nsWindow*>(data);
                       if (!window || window->IsDestroyed()) {
                         return;
                       }
                       LOGW("[%p] nsWindow::Emoji() insert_text", window);
                       WidgetContentCommandEvent insertTextEvent(
                           true, eContentCommandInsertText, window);
                       NS_ConvertUTF8toUTF16 str(text);
                       insertTextEvent.mString.emplace(str);
                       window->DispatchEvent(&insertTextEvent);
                     }),
                     aToplevelWindow);
  }

  DesktopIntRect input = aToplevelWindow->GetTextInputArea();
  auto offset = IsTopLevelWidget()
                    ? DesktopIntPoint()
                    : WidgetToScreenOffsetUnscaled() -
                          DesktopIntPoint(aToplevelWindow->mClientMargin.left,
                                          aToplevelWindow->mClientMargin.top);

  LOG("nsWindow::InsertEmoji() carret [%d, %d] offset [%d, %d] height %d",
      int(input.x), int(input.y), int(offset.x), int(offset.y), input.height);
  moz_container_entry_position(MOZ_CONTAINER(mContainer), input.x - offset.x,
                               input.y - offset.y, input.height);
  // We may hide cursor when text input is active but we don't want to do it
  // for emoji picker.
  mWidgetCursorLocked = true;

  // Calls gtk_entry_insert_emoji() directly, creates emoji chooser widget
  // as child of GtkEntry.
  g_signal_emit_by_name(entry, "insert-emoji");

  if (!mEmojiHidenSignal) {
    GtkWidget* chooser =
        GTK_WIDGET(g_object_get_data(G_OBJECT(entry), "gtk-emoji-chooser"));
    if (!chooser) {
      return;
    }
    mEmojiHidenSignal = g_signal_connect(
        chooser, "hide", G_CALLBACK(+[](GtkWidget* emojiPicker, gpointer data) {
          nsWindow* window = static_cast<nsWindow*>(data);
          if (!window || window->IsDestroyed()) {
            return;
          }
          LOGW("[%p] nsWindow::Emoji() emoji picker hide", window);
          window->UnlockCursor();
        }),
        this);
  }
}

uint32_t nsWindow::GetMaxTouchPoints() const {
#ifdef MOZ_WAYLAND
  // We may want to read max touch points from GdkDevice:num-touches.
  // But that means we need to enumerate touch GdkDevice(s) first
  // and then query it. Not sure it's worth the effort, just return
  // fixed value if touch device is present for now.
  if (GdkIsWaylandDisplay()) {
    static constexpr uint32_t sMaxTouchPoints = 5;
    return WaylandDisplayGet()->GetTouch() ? sMaxTouchPoints : 0;
  }
#endif
#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    static const uint32_t sMaxTouchPoints = [] {
      Display* xDisplay = mozilla::DefaultXDisplay();
      if (!xDisplay) {
        return 0u;
      }

      using XIQueryDeviceFunc = XIDeviceInfo* (*)(Display*, int, int*);
      using XIFreeDeviceInfoFunc = void (*)(XIDeviceInfo*);
      auto queryDevice =
          (XIQueryDeviceFunc)dlsym(RTLD_DEFAULT, "XIQueryDevice");
      auto freeDeviceInfo =
          (XIFreeDeviceInfoFunc)dlsym(RTLD_DEFAULT, "XIFreeDeviceInfo");
      if (!queryDevice || !freeDeviceInfo) {
        return 0u;
      }

      int nDevices = 0;
      XIDeviceInfo* devices = queryDevice(xDisplay, XIAllDevices, &nDevices);
      if (!devices) {
        return 0u;
      }

      uint32_t maxTouchPoints = 0;
      for (int i = 0; i < nDevices; i++) {
        for (int j = 0; j < devices[i].num_classes; j++) {
          if (devices[i].classes[j]->type == XITouchClass) {
            auto* touchClass =
                reinterpret_cast<XITouchClassInfo*>(devices[i].classes[j]);
            if (touchClass->mode == XIDirectTouch &&
                static_cast<uint32_t>(touchClass->num_touches) >
                    maxTouchPoints) {
              maxTouchPoints = touchClass->num_touches;
            }
          }
        }
      }
      freeDeviceInfo(devices);
      return maxTouchPoints;
    }();
    return sMaxTouchPoints;
  }
#endif
  return 0;
}

void nsWindow::SessionRestoreFinished() {
  LOGW("nsWindow::SessionRestoreFinished() set focus to [%p]",
       gFocusRequestWindow.get());
  if (!gFocusRequestWindow) {
    return;
  }
  if (gFocusRequestWindow->mWaitingToSessionRestore) {
    NS_WARNING(
        "Session restore finished before nsWindow::MoveToWorkspace() calls!");
    gFocusRequestWindow->mWaitingToSessionRestore = false;
  }
  gFocusRequestWindow->SetFocus(gFocusRequestWindowRaise,
                                mozilla::dom::CallerType::System);
  gFocusRequestWindow = nullptr;
}

// If aForce = true we create a new session if there isn't any one.
static RefPtr<nsDragSessionGtk> GetDragSession(RefPtr<nsWindow> aWindow,
                                               bool aForce = false) {
  if (!aWindow || !aWindow->GetGdkWindow()) {
    LOGDRAG("DataOffer::GetDragSession(): missing mWindow, quit!");
    return nullptr;
  }
  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  NS_ENSURE_TRUE(dragService, nullptr);
  RefPtr<nsDragSessionGtk> dragSession =
      static_cast<nsDragSessionGtk*>(dragService->GetCurrentSession(aWindow));
  if (!dragSession && aForce) {
    LOGDRAG(
        "DataOffer::GetDragSession(): missing current session, creating a new "
        "one.");
    // This may be the start of an external drag session.
    nsIWidget* widget = aWindow;
    dragSession =
        static_cast<nsDragSessionGtk*>(dragService->StartDragSession(widget));
  }
  NS_ENSURE_TRUE(dragSession, nullptr);
  return dragSession;
}

static LayoutDeviceIntPoint GetWindowDropPosition(nsWindow* aWindow, int aX,
                                                  int aY) {
  // Workaround for Bug 1710344
  // Caused by Gtk issue https://gitlab.gnome.org/GNOME/gtk/-/issues/4437
  if (aWindow->IsWaylandPopup()) {
    int tx = 0, ty = 0;
    gdk_window_get_position(aWindow->GetToplevelGdkWindow(), &tx, &ty);
    aX += tx;
    aY += ty;
  }
  LOGDRAG("WindowDropPosition [%d, %d]", aX, aY);
  return aWindow->GdkPointToDevicePixels({aX, aY});
}

static gboolean drag_motion_event_cb(GtkWidget* aWidget,
                                     GdkDragContext* aDragContext, gint aX,
                                     gint aY, guint aTime, gpointer aData) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(aWidget);

  RefPtr<nsDragSessionGtk> dragSession =
      GetDragSession(window, /* aForce */ true);
  NS_ENSURE_TRUE(dragSession, FALSE);

  nsDragSession::AutoEventLoop loop(dragSession);

  // We're getting aX,aY in mShell coordinates space.
  // mContainer is shifted by CSD decorations so translate the coords
  // to mContainer space where our content lives.
  //
  // TODO: We may use client offset on Wayland but X11 seems to relly
  // on gdk_window_get_geometry().
  if (aWidget == window->GetGtkWidget()) {
    int x, y;
    gdk_window_get_geometry(window->GetGdkWindow(), &x, &y, nullptr, nullptr);
    aX -= x;
    aY -= y;
  }

  LOGDRAG("mShell::drag_motion_event_cb target nsWindow [%p] point [%d, %d]",
          window.get(), (int)aX, (int)aY);

  return dragSession->ScheduleMotionEvent(
      window, aDragContext, GetWindowDropPosition(window, aX, aY), aTime);
}

static void drag_leave_event_cb(GtkWidget* aWidget,
                                GdkDragContext* aDragContext, guint aTime,
                                gpointer aData) {
  LOGDRAG("mShell::drag_leave");
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(aWidget);
  RefPtr<nsDragSessionGtk> dragSession = GetDragSession(window);
  if (!dragSession) {
    LOGDRAG("    Received dragleave after drag had ended.\n");
    return;
  }

  nsDragSession::AutoEventLoop loop(dragSession);

  nsWindow* mostRecentDragWindow = dragSession->GetMostRecentDestWindow();
  if (!mostRecentDragWindow) {
    // This can happen when the target will not accept a drop.  A GTK drag
    // source sends the leave message to the destination before the
    // drag-failed signal on the source widget, but the leave message goes
    // via the X server, and so doesn't get processed at least until the
    // event loop runs again.
    LOGDRAG("    Failed - GetMostRecentDestWindow()!\n");
    return;
  }

  if (aWidget != window->GetGtkWidget()) {
    // When the drag moves between widgets, GTK can send leave signal for
    // the old widget after the motion or drop signal for the new widget.
    // We'll send the leave event when the motion or drop event is run.
    LOGDRAG("    Failed - GtkWidget mismatch!\n");
    return;
  }

  LOGDRAG("WindowDragLeaveHandler nsWindow %p\n", (void*)mostRecentDragWindow);
  dragSession->ScheduleLeaveEvent();
}

static gboolean drag_drop_event_cb(GtkWidget* aWidget,
                                   GdkDragContext* aDragContext, gint aX,
                                   gint aY, guint aTime, gpointer aData) {
  RefPtr<nsWindow> window = nsWindow::FromGtkWidget(aWidget);

  RefPtr<nsDragSessionGtk> dragSession =
      GetDragSession(window, /* aForce */ false);
  NS_ENSURE_TRUE(dragSession, FALSE);

  nsDragSession::AutoEventLoop loop(dragSession);

  // We're getting aX,aY in mShell coordinates space.
  // mContainer is shifted by CSD decorations so translate the coords
  // to mContainer space where our content lives.
  if (aWidget == window->GetGtkWidget()) {
    int x, y;
    gdk_window_get_geometry(window->GetGdkWindow(), &x, &y, nullptr, nullptr);
    aX -= x;
    aY -= y;
  }

  LOGDRAG("WindowDragDropHandler nsWindow [%p] point [%d, %d]", window.get(),
          (int)aX, (int)aY);

  // If drag-drop returns true, we need to terminate D&D by gtk_drag_finish().
  return dragSession->ScheduleDropEvent(
      window, aDragContext, GetWindowDropPosition(window, aX, aY), aTime);
}

static void drag_data_received_event_cb(GtkWidget* aWidget,
                                        GdkDragContext* aDragContext, gint aX,
                                        gint aY,
                                        GtkSelectionData* aSelectionData,
                                        guint aInfo, guint aTime,
                                        gpointer aData) {
  RefPtr<nsDragSessionGtk> dragSession =
      GetDragSession(nsWindow::FromGtkWidget(aWidget));
  NS_ENSURE_TRUE_VOID(dragSession);

  LOGDRAG("mShell::drag_data_received_event_cb [%p]",
          nsWindow::FromGtkWidget(aWidget));

  nsDragSession::AutoEventLoop loop(dragSession);
  dragSession->DragDataReceived(aWidget, aDragContext, aX, aY, aSelectionData,
                                aInfo, aTime);
}
