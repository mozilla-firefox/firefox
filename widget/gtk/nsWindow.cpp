/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindow.h"

#include <algorithm>
#include <cstdint>
#ifdef MOZ_X11
#include <X11/Xlib.h>
#endif
#include <dlfcn.h>
#include <gdk/gdkkeysyms.h>
#include <wchar.h>

#include "VsyncSource.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxPlatformGtk.h"
#include "gfxUtils.h"
#include "GLContextProvider.h"
#include "GLContext.h"
#include "GtkCompositorWidget.h"
#include "gtkdrawing.h"
#include "imgIContainer.h"
#include "InputData.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Components.h"
#include "mozilla/GRefPtr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/HelpersCairo.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/APZInputBridge.h"
#include "mozilla/layers/IAPZCTreeManager.h"
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
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/WritingModes.h"
#ifdef MOZ_X11
#  include "mozilla/X11Util.h"
#endif
#include "mozilla/XREAppData.h"
#include "NativeKeyBindings.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsAppRunner.h"
#include "nsDragService.h"
#include "nsGTKToolkit.h"
#include "nsGtkKeyUtils.h"
#include "nsGtkCursors.h"
#include "nsGfxCIID.h"
#include "nsGtkUtils.h"
#include "nsIFile.h"
#include "nsIGSettingsService.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsImageToPixbuf.h"
#include "nsINode.h"
#include "nsIRollupListener.h"
#include "nsIScreenManager.h"
#include "nsIUserIdleServiceInternal.h"
#include "nsIWidgetListener.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsPresContext.h"
#include "nsShmImage.h"
#include "nsString.h"
#include "nsWidgetsCID.h"
#include "nsViewManager.h"
#include "nsXPLookAndFeel.h"
#include "prlink.h"
#include "Screen.h"
#include "ScreenHelperGTK.h"
#include "SystemTimeConverter.h"
#include "WidgetUtilsGtk.h"
#include "NativeMenuGtk.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/LocalAccessible.h"
#  include "mozilla/a11y/Platform.h"
#  include "nsAccessibilityService.h"
#endif

#ifdef MOZ_X11
#  include <gdk/gdkkeysyms-compat.h>
#  include <X11/Xatom.h>
#  include <X11/extensions/XShm.h>
#  include <X11/extensions/shape.h>
#  include "gfxXlibSurface.h"
#  include "GLContextGLX.h"  // for GLContextGLX::FindVisual()
#  include "GLContextEGL.h"  // for GLContextEGL::FindVisual()
#  include "WindowSurfaceX11Image.h"
#  include "WindowSurfaceX11SHM.h"
#endif
#ifdef MOZ_WAYLAND
#  include <gdk/gdkwayland.h>
#  include <gdk/gdkkeysyms-compat.h>
#  include "nsIClipboard.h"
#  include "nsView.h"
#  include "WaylandVsyncSource.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
#ifdef MOZ_X11
using mozilla::gl::GLContextEGL;
using mozilla::gl::GLContextGLX;
#endif

// Don't put more than this many rects in the dirty region, just fluff
// out to the bounding-box if there are more
#define MAX_RECTS_IN_REGION 100

#if !GTK_CHECK_VERSION(3, 22, 0)

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
static nsWindow* get_window_for_gtk_widget(GtkWidget* widget);
static nsWindow* get_window_for_gdk_window(GdkWindow* window);
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
MOZ_CAN_RUN_SCRIPT static gboolean button_press_event_cb(GtkWidget* widget,
                                                         GdkEventButton* event);
static gboolean button_release_event_cb(GtkWidget* widget,
                                        GdkEventButton* event);
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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#ifdef MOZ_X11
static GdkFilterReturn popup_take_focus_filter(GdkXEvent* gdk_xevent,
                                               GdkEvent* event, gpointer data);
#endif /* MOZ_X11 */
#ifdef __cplusplus
}
#endif /* __cplusplus */

static gboolean drag_motion_event_cb(GtkWidget* aWidget,
                                     GdkDragContext* aDragContext, gint aX,
                                     gint aY, guint aTime, gpointer aData);
static void drag_leave_event_cb(GtkWidget* aWidget,
                                GdkDragContext* aDragContext, guint aTime,
                                gpointer aData);
static gboolean drag_drop_event_cb(GtkWidget* aWidget,
                                   GdkDragContext* aDragContext, gint aX,
                                   gint aY, guint aTime, gpointer aData);
static void drag_data_received_event_cb(GtkWidget* aWidget,
                                        GdkDragContext* aDragContext, gint aX,
                                        gint aY,
                                        GtkSelectionData* aSelectionData,
                                        guint aInfo, guint32 aTime,
                                        gpointer aData);

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
static bool gBlockActivateEvent = false;
static bool gGlobalsInitialized = false;
static bool gUseAspectRatio = true;
static uint32_t gLastTouchID = 0;
// See Bug 1777269 for details. We don't know if the suspected leave notify
// event is a correct one when we get it.
// Store it and issue it later from enter notify event if it's correct,
// throw it away otherwise.
MOZ_RUNINIT static GUniquePtr<GdkEventCrossing> sStoredLeaveNotifyEvent;

#define NS_WINDOW_TITLE_MAX_LENGTH 4095

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
static void GtkWindowSetTransientFor(GtkWindow* aWindow, GtkWindow* aParent) {
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
    : mWindowVisibilityMutex("nsWindow::mWindowVisibilityMutex"),
      mIsMapped(false),
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
      mPendingBoundsChangeMayChangeCsdMargin(false),
      mTitlebarBackdropState(false),
      mAlwaysOnTop(false),
      mNoAutoHide(false),
      mIsTransparent(false),
      mHasReceivedSizeAllocate(false),
      mWidgetCursorLocked(false),
      mUndecorated(false),
      mPopupTrackInHierarchy(false),
      mPopupTrackInHierarchyConfigured(false),
      mHiddenPopupPositioned(false),
      mHasAlphaVisual(false),
      mPopupAnchored(false),
      mPopupContextMenu(false),
      mPopupMatchesLayout(false),
      mPopupChanged(false),
      mPopupTemporaryHidden(false),
      mPopupClosed(false),
      mPopupUseMoveToRect(false),
      mWaitingForMoveToRectCallback(false),
      mMovedAfterMoveToRect(false),
      mResizedAfterMoveToRect(false),
      mConfiguredClearColor(false),
      mGotNonBlankPaint(false),
      mNeedsToRetryCapturingMouse(false) {
  mWindowType = WindowType::Child;
  mSizeConstraints.mMaxSize = GetSafeWindowSize(mSizeConstraints.mMaxSize);

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

  if (mWidgetListener) mWidgetListener->WindowActivated();
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
  LOG("nsWindow::DispatchResized() size [%d, %d]", (int)(mBounds.width),
      (int)(mBounds.height));

  if (mIsDestroyed) {
    return;
  }

  auto clientSize = GetClientSize();
  // Check mBounds size
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(mBounds.width, mBounds.height)) {
    gfxCriticalNoteOnce << "Invalid mBounds in MaybeDispatchResized " << mBounds
                        << " size state " << mSizeMode;
  }

  // Notify the GtkCompositorWidget of a ClientSizeChange
  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyClientSizeChanged(clientSize);
  }

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, clientSize.width, clientSize.height);
  }
  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, clientSize.width,
                                           clientSize.height);
  }
}

nsIWidgetListener* nsWindow::GetListener() {
  return mAttachedWidgetListener ? mAttachedWidgetListener : mWidgetListener;
}

nsresult nsWindow::DispatchEvent(WidgetGUIEvent* aEvent,
                                 nsEventStatus& aStatus) {
#ifdef DEBUG
  debug_DumpEvent(stdout, aEvent->mWidget, aEvent, "something", 0);
#endif
  aStatus = nsEventStatus_eIgnore;
  nsIWidgetListener* listener = GetListener();
  if (listener) {
    aStatus = listener->HandleEvent(aEvent, mUseAttachedEvents);
  }

  return NS_OK;
}

void nsWindow::OnDestroy(void) {
  if (mOnDestroyCalled) {
    return;
  }

  mOnDestroyCalled = true;

  // Prevent deletion.
  nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

  // release references to children, device context, toolkit + app shell
  nsBaseWidget::OnDestroy();

  // Remove association between this object and its parent and siblings.
  nsBaseWidget::Destroy();

  NotifyWindowDestroyed();
}

bool nsWindow::AreBoundsSane() {
  // Check requested size, as mBounds might not have been updated.
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

#ifdef MOZ_WAYLAND
  // Shut down our local vsync source
  // Also drops reference to nsWindow::mSurface.
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->Shutdown();
    mWaylandVsyncSource = nullptr;
  }
  mWaylandVsyncDispatcher = nullptr;
  UnlockNativePointer();
#endif

  // Cancel (dragleave) the current drag session, if any.
  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  if (dragService) {
    nsDragSession* dragSession =
        static_cast<nsDragSession*>(dragService->GetCurrentSession(this));
    if (dragSession && this == dragSession->GetMostRecentDestWindow()) {
      dragSession->ScheduleLeaveEvent();
    }
  }

  nsIRollupListener* rollupListener = nsBaseWidget::GetActiveRollupListener();
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
    nsWindow* window =
        get_window_for_gdk_window(sStoredLeaveNotifyEvent->window);
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

  gtk_widget_destroy(mShell);
  mShell = nullptr;
  mContainer = nullptr;
#ifdef MOZ_WAYLAND
  mSurface = nullptr;
#endif

  MOZ_ASSERT(!mGdkWindow,
             "mGdkWindow should be NULL when mContainer is destroyed");

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

DesktopToLayoutDeviceScale nsWindow::GetDesktopToDeviceScale() {
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    return DesktopToLayoutDeviceScale(FractionalScaleFactor());
  }
#endif

  // In Gtk/X11, we manage windows using device pixels.
  return DesktopToLayoutDeviceScale(1.0);
}

DesktopToLayoutDeviceScale nsWindow::GetDesktopToDeviceScaleByScreen() {
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    // In wayland there's no absolute screen position, so we need to use the
    // scale factor of our top level, which is what FractionalScaleFactor does,
    // luckily.
    return DesktopToLayoutDeviceScale(FractionalScaleFactor());
  }
#endif
  return nsBaseWidget::GetDesktopToDeviceScale();
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

static void InitPenEvent(WidgetMouseEvent& aGeckoEvent, GdkEvent* aEvent) {
  // Find the source of the event
  GdkDevice* device = gdk_event_get_source_device(aEvent);
  GdkInputSource eSource = gdk_device_get_source(device);
  gdouble value;

  // We distinguish touch screens from pens using the event type
  // Eraser corresponds to the pen with the "erase" button pressed
  if (eSource != GDK_SOURCE_PEN && eSource != GDK_SOURCE_ERASER) {
    bool XWaylandPen = false;
#ifdef MOZ_X11
    // Workaround : When using Xwayland, pens are reported as
    // GDK_SOURCE_TOUCHSCREEN If eSource is GDK_SOURCE_TOUCHSCREEN and the
    // GDK_AXIS_XTILT and GDK_AXIS_YTILT axes are reported then it's a pen and
    // not a finger on a screen. Yes, that's a stupid heuristic but it works...
    // Note, however, that the tilt values are not reliable
    // Another approach could be use the device tool type, but that's only
    // available in GTK > 3.22
    XWaylandPen = (eSource == GDK_SOURCE_TOUCHSCREEN && GdkIsX11Display() &&
                   gdk_event_get_axis(aEvent, GDK_AXIS_XTILT, &value) &&
                   gdk_event_get_axis(aEvent, GDK_AXIS_YTILT, &value));
#endif
    if (!XWaylandPen) {
      return;
    }
    LOGW("InitPenEvent(): Is XWayland pen");
  }

  aGeckoEvent.mInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_PEN;
  aGeckoEvent.pointerId = 1;

  // The range of xtilt and ytilt are -1 to 1. Normalize it to -90 to 90.
  if (gdk_event_get_axis(aEvent, GDK_AXIS_XTILT, &value)) {
    aGeckoEvent.tiltX = int32_t(NS_round(value * 90));
  }
  if (gdk_event_get_axis(aEvent, GDK_AXIS_YTILT, &value)) {
    aGeckoEvent.tiltY = int32_t(NS_round(value * 90));
  }
  if (gdk_event_get_axis(aEvent, GDK_AXIS_PRESSURE, &value)) {
    aGeckoEvent.mPressure = (float)value;
    // Make sure the pression is acceptable
    MOZ_ASSERT(aGeckoEvent.mPressure >= 0.0 && aGeckoEvent.mPressure <= 1.0);
  }

  LOGW("InitPenEvent(): pressure %f\n", aGeckoEvent.mPressure);
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

bool nsWindow::IsMapped() const { return mIsMapped; }

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
  int32_t logWidth = std::max(NSToIntRound(mBounds.width / dpiScale), 1);
  int32_t logHeight = std::max(NSToIntRound(mBounds.height / dpiScale), 1);

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

void nsWindow::ConstrainSize(int* aWidth, int* aHeight) {
  // We store our constraints in inner sizes for convenience, but that means we
  // need to also constrain inner sizes as inner, rather than outer.
  *aWidth -= mClientMargin.LeftRight();
  *aHeight -= mClientMargin.TopBottom();
  nsBaseWidget::ConstrainSize(aWidth, aHeight);
  *aWidth += mClientMargin.LeftRight();
  *aHeight += mClientMargin.TopBottom();
}

void nsWindow::SetSizeConstraints(const SizeConstraints& aConstraints) {
  mSizeConstraints.mMinSize = GetSafeWindowSize(aConstraints.mMinSize);
  mSizeConstraints.mMaxSize = GetSafeWindowSize(aConstraints.mMaxSize);

  // Store constraints as inner sizes rather than outer sizes.
  if (SizeMode() == nsSizeMode_Normal) {
    if (mSizeConstraints.mMinSize.height) {
      mSizeConstraints.mMinSize.height -= mClientMargin.TopBottom();
    }
    if (mSizeConstraints.mMinSize.width) {
      mSizeConstraints.mMinSize.width -= mClientMargin.LeftRight();
    }
    if (mSizeConstraints.mMaxSize.height != NS_MAXSIZE) {
      mSizeConstraints.mMaxSize.height -= mClientMargin.TopBottom();
    }
    if (mSizeConstraints.mMaxSize.width != NS_MAXSIZE) {
      mSizeConstraints.mMaxSize.width -= mClientMargin.LeftRight();
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
        (gboolean(*)(const GdkWaylandDisplay*))dlsym(
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

GdkPoint nsWindow::GetCsdOffsetInGdkCoords() {
  return DevicePixelsToGdkPointRoundDown(
      LayoutDeviceIntPoint(mCsdMargin.top, mCsdMargin.left));
}

void nsWindow::ApplySizeConstraints() {
  if (!mShell) {
    return;
  }

  uint32_t hints = 0;
  auto constraints = mSizeConstraints;
  if (constraints.mMinSize != LayoutDeviceIntSize()) {
    gtk_widget_set_size_request(
        GTK_WIDGET(mContainer),
        DevicePixelsToGdkCoordRoundUp(constraints.mMinSize.width),
        DevicePixelsToGdkCoordRoundUp(constraints.mMinSize.height));
    if (ToplevelUsesCSD()) {
      constraints.mMinSize.height += mClientMargin.TopBottom();
      constraints.mMinSize.width += mClientMargin.LeftRight();
    }
    hints |= GDK_HINT_MIN_SIZE;
  }
  if (mSizeConstraints.mMaxSize !=
      LayoutDeviceIntSize(NS_MAXSIZE, NS_MAXSIZE)) {
    if (ToplevelUsesCSD()) {
      constraints.mMaxSize.height += mClientMargin.TopBottom();
      constraints.mMaxSize.width += mClientMargin.LeftRight();
    }
    hints |= GDK_HINT_MAX_SIZE;
  }

  // Constraints for the shell are outer sizes, but with SSD we need to use
  // inner sizes.
  GdkGeometry geometry{
      .min_width = DevicePixelsToGdkCoordRoundUp(constraints.mMinSize.width),
      .min_height = DevicePixelsToGdkCoordRoundUp(constraints.mMinSize.height),
      .max_width = DevicePixelsToGdkCoordRoundDown(constraints.mMaxSize.width),
      .max_height =
          DevicePixelsToGdkCoordRoundDown(constraints.mMaxSize.height),
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

void nsWindow::ResizeInt(const Maybe<LayoutDeviceIntPoint>& aMove,
                         LayoutDeviceIntSize aSize) {
  LOG("nsWindow::ResizeInt w:%d h:%d\n", aSize.width, aSize.height);
  const bool moved =
      aMove && (*aMove != mLastMoveRequest || mBounds.TopLeft() != *aMove);
  if (moved) {
    LOG("  with move to left:%d top:%d", aMove->x.value, aMove->y.value);
    mLastMoveRequest = *aMove;
  }

  ConstrainSize(&aSize.width, &aSize.height);
  LOG("  ConstrainSize: w:%d h;%d\n", aSize.width, aSize.height);

  const bool resized = aSize != mLastSizeRequest || mBounds.Size() != aSize;
#if MOZ_LOGGING
  LOG("  resized %d aSize [%d, %d] mLastSizeRequest [%d, %d] mBounds [%d, %d]",
      resized, aSize.width, aSize.height, mLastSizeRequest.width,
      mLastSizeRequest.height, mBounds.width, mBounds.height);
#endif

  mLastSizeRequest = aSize;
  // Check size
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(aSize.width, aSize.height)) {
    gfxCriticalNoteOnce << "Invalid aSize in ResizeInt " << aSize
                        << " size state " << mSizeMode;
  }

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

  // We optimistically assume size changes immediately in two cases:
  // 1. Override-redirect window: Size is controlled by only us.
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
  // So we don't update mBounds until OnContainerSizeAllocate() when we know the
  // request is granted.
  bool isOrWillBeVisible = mHasReceivedSizeAllocate || mNeedsShow || mIsShown;
  if (!isOrWillBeVisible ||
      gtk_window_get_window_type(GTK_WINDOW(mShell)) == GTK_WINDOW_POPUP) {
    mBounds.SizeTo(aSize);
    if (moved) {
      mBounds.MoveTo(*aMove);
      NotifyWindowMoved(mBounds.x, mBounds.y);
    }
    DispatchResized();
  }
}

void nsWindow::Resize(double aWidth, double aHeight, bool aRepaint) {
  LOG("nsWindow::Resize %f %f\n", aWidth, aHeight);

  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  auto size = LayoutDeviceIntSize::Round(scale * aWidth, scale * aHeight);

  ResizeInt(Nothing(), size);
}

void nsWindow::Resize(double aX, double aY, double aWidth, double aHeight,
                      bool aRepaint) {
  LOG("nsWindow::Resize [%f,%f] -> [%f x %f] repaint %d\n", aX, aY, aWidth,
      aHeight, aRepaint);

  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  auto size = LayoutDeviceIntSize::Round(scale * aWidth, scale * aHeight);
  auto topLeft = LayoutDeviceIntPoint::Round(scale * aX, scale * aY);

  ResizeInt(Some(topLeft), size);
}

void nsWindow::Enable(bool aState) { mEnabled = aState; }

bool nsWindow::IsEnabled() const { return mEnabled; }

void nsWindow::Move(double aX, double aY) {
  double scale =
      BoundsUseDesktopPixels() ? GetDesktopToDeviceScale().scale : 1.0;
  const LayoutDeviceIntPoint request(NSToIntRound(aX * scale),
                                     NSToIntRound(aY * scale));
  LOG("nsWindow::Move to %d x %d\n", request.x.value, request.y.value);

  if (mSizeMode != nsSizeMode_Normal && IsTopLevelWidget()) {
    LOG("  size state is not normal, bailing");
    return;
  }

  // Since a popup window's x/y coordinates are in relation to to
  // the parent, the parent might have moved so we always move a
  // popup window.
  LOG("  bounds %d x %d\n", mBounds.x, mBounds.y);
  if (mBounds.TopLeft() == request && mLastMoveRequest == request &&
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

static nsMenuPopupFrame* GetMenuPopupFrame(nsIFrame* aFrame) {
  return do_QueryFrame(aFrame);
}

void nsWindow::AppendPopupToHierarchyList(nsWindow* aToplevelWindow) {
  mWaylandToplevel = aToplevelWindow;

  nsWindow* popup = aToplevelWindow;
  while (popup && popup->mWaylandPopupNext) {
    popup = popup->mWaylandPopupNext;
  }
  popup->mWaylandPopupNext = this;

  mWaylandPopupPrev = popup;
  mWaylandPopupNext = nullptr;
  mPopupChanged = true;
  mPopupClosed = false;
}

void nsWindow::RemovePopupFromHierarchyList() {
  // We're already removed from the popup hierarchy
  if (!IsInPopupHierarchy()) {
    return;
  }
  mWaylandPopupPrev->mWaylandPopupNext = mWaylandPopupNext;
  if (mWaylandPopupNext) {
    mWaylandPopupNext->mWaylandPopupPrev = mWaylandPopupPrev;
    mWaylandPopupNext->mPopupChanged = true;
  }
  mWaylandPopupNext = mWaylandPopupPrev = nullptr;
}

// Gtk refuses to map popup window with x < 0 && y < 0 relative coordinates
// see https://gitlab.gnome.org/GNOME/gtk/-/issues/4071
// as a workaround just fool around and place the popup temporary to 0,0.
bool nsWindow::WaylandPopupRemoveNegativePosition(int* aX, int* aY) {
  // https://gitlab.gnome.org/GNOME/gtk/-/issues/4071 applies to temporary
  // windows only
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window || gdk_window_get_window_type(window) != GDK_WINDOW_TEMP) {
    return false;
  }

  LOG("nsWindow::WaylandPopupRemoveNegativePosition()");

  int x, y;
  gtk_window_get_position(GTK_WINDOW(mShell), &x, &y);
  bool moveBack = (x < 0 && y < 0);
  if (moveBack) {
    gtk_window_move(GTK_WINDOW(mShell), 0, 0);
    if (aX) {
      *aX = x;
    }
    if (aY) {
      *aY = y;
    }
  }

  gdk_window_get_geometry(window, &x, &y, nullptr, nullptr);
  if (x < 0 && y < 0) {
    gdk_window_move(window, 0, 0);
  }

  return moveBack;
}

void nsWindow::ShowWaylandPopupWindow() {
  LOG("nsWindow::ShowWaylandPopupWindow. Expected to see visible.");
  MOZ_ASSERT(IsWaylandPopup());

  if (!mPopupTrackInHierarchy) {
    LOG("  popup is not tracked in popup hierarchy, show it now");
    gtk_widget_show(mShell);
    return;
  }

  // Popup position was checked before gdk_window_move_to_rect() callback
  // so just show it.
  if (mPopupUseMoveToRect && mWaitingForMoveToRectCallback) {
    LOG("  active move-to-rect callback, show it as is");
    gtk_widget_show(mShell);
    return;
  }

  if (gtk_widget_is_visible(mShell)) {
    LOG("  is already visible, quit");
    return;
  }

  int x, y;
  bool moved = WaylandPopupRemoveNegativePosition(&x, &y);
  gtk_widget_show(mShell);
  if (moved) {
    LOG("  move back to (%d, %d) and show", x, y);
    gtk_window_move(GTK_WINDOW(mShell), x, y);
  }
}

void nsWindow::WaylandPopupMarkAsClosed() {
  LOG("nsWindow::WaylandPopupMarkAsClosed: [%p]\n", this);
  mPopupClosed = true;
  // If we have any child popup window notify it about
  // parent switch.
  if (mWaylandPopupNext) {
    mWaylandPopupNext->mPopupChanged = true;
  }
}

nsWindow* nsWindow::WaylandPopupFindLast(nsWindow* aPopup) {
  while (aPopup && aPopup->mWaylandPopupNext) {
    aPopup = aPopup->mWaylandPopupNext;
  }
  return aPopup;
}

// Hide and potentially removes popup from popup hierarchy.
void nsWindow::HideWaylandPopupWindow(bool aTemporaryHide,
                                      bool aRemoveFromPopupList) {
  LOG("nsWindow::HideWaylandPopupWindow: remove from list %d\n",
      aRemoveFromPopupList);
  if (aRemoveFromPopupList) {
    RemovePopupFromHierarchyList();
  }

  if (!mPopupClosed) {
    mPopupClosed = !aTemporaryHide;
  }

  bool visible = gtk_widget_is_visible(mShell);
  LOG("  gtk_widget_is_visible() = %d\n", visible);

  // Restore only popups which are really visible
  mPopupTemporaryHidden = aTemporaryHide && visible;

  // Hide only visible popups or popups closed pernamently.
  if (visible) {
    gtk_widget_hide(mShell);

    // If there's pending Move-To-Rect callback and we hide the popup
    // the callback won't be called any more.
    mWaitingForMoveToRectCallback = false;
  }

  if (mPopupClosed) {
    LOG("  Clearing mMoveToRectPopupSize\n");
    mMoveToRectPopupSize = {};
#ifdef MOZ_WAYLAND
    if (moz_container_wayland_is_waiting_to_show(mContainer)) {
      // We need to clear rendering queue, see Bug 1782948.
      LOG("  popup failed to show by Wayland compositor, clear rendering "
          "queue.");
      moz_container_wayland_clear_waiting_to_show_flag(mContainer);
      ClearRenderingQueue();
    }
#endif
  }
}

void nsWindow::HideWaylandToplevelWindow() {
  LOG("nsWindow::HideWaylandToplevelWindow: [%p]\n", this);
  if (mWaylandPopupNext) {
    nsWindow* popup = WaylandPopupFindLast(mWaylandPopupNext);
    while (popup->mWaylandToplevel != nullptr) {
      nsWindow* prev = popup->mWaylandPopupPrev;
      popup->HideWaylandPopupWindow(/* aTemporaryHide */ false,
                                    /* aRemoveFromPopupList */ true);
      popup = prev;
    }
  }
  WaylandStopVsync();
  gtk_widget_hide(mShell);
}

void nsWindow::ShowWaylandToplevelWindow() {
  MOZ_ASSERT(!IsWaylandPopup());
  LOG("nsWindow::ShowWaylandToplevelWindow");
  gtk_widget_show(mShell);
}

void nsWindow::WaylandPopupRemoveClosedPopups() {
  LOG("nsWindow::WaylandPopupRemoveClosedPopups()");
  nsWindow* popup = this;
  while (popup) {
    nsWindow* next = popup->mWaylandPopupNext;
    if (popup->mPopupClosed) {
      popup->HideWaylandPopupWindow(/* aTemporaryHide */ false,
                                    /* aRemoveFromPopupList */ true);
    }
    popup = next;
  }
}

// Hide all tooltips except the latest one.
void nsWindow::WaylandPopupHideTooltips() {
  LOG("nsWindow::WaylandPopupHideTooltips");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  nsWindow* popup = mWaylandPopupNext;
  while (popup && popup->mWaylandPopupNext) {
    if (popup->mPopupType == PopupType::Tooltip) {
      LOG("  hidding tooltip [%p]", popup);
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->mWaylandPopupNext;
  }
}

void nsWindow::WaylandPopupCloseOrphanedPopups() {
#ifdef MOZ_WAYLAND
  LOG("nsWindow::WaylandPopupCloseOrphanedPopups");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  nsWindow* popup = mWaylandPopupNext;
  bool dangling = false;
  while (popup) {
    if (!dangling &&
        moz_container_wayland_is_waiting_to_show(popup->GetMozContainer())) {
      LOG("  popup [%p] is waiting to show, close all child popups", popup);
      dangling = true;
    } else if (dangling) {
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->mWaylandPopupNext;
  }
#endif
}

// We can't show popups with remote content or overflow popups
// on top of regular ones.
// If there's any remote popup opened, close all parent popups of it.
void nsWindow::CloseAllPopupsBeforeRemotePopup() {
  LOG("nsWindow::CloseAllPopupsBeforeRemotePopup");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  // Don't waste time when there's only one popup opened.
  if (!mWaylandPopupNext || mWaylandPopupNext->mWaylandPopupNext == nullptr) {
    return;
  }

  // Find the first opened remote content popup
  nsWindow* remotePopup = mWaylandPopupNext;
  while (remotePopup) {
    if (remotePopup->HasRemoteContent() ||
        remotePopup->IsWidgetOverflowWindow()) {
      LOG("  remote popup [%p]", remotePopup);
      break;
    }
    remotePopup = remotePopup->mWaylandPopupNext;
  }

  if (!remotePopup) {
    return;
  }

  // ...hide opened popups before the remote one.
  nsWindow* popup = mWaylandPopupNext;
  while (popup && popup != remotePopup) {
    LOG("  hidding popup [%p]", popup);
    popup->WaylandPopupMarkAsClosed();
    popup = popup->mWaylandPopupNext;
  }
}

static void GetLayoutPopupWidgetChain(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  pm->GetSubmenuWidgetChain(aLayoutWidgetHierarchy);
  aLayoutWidgetHierarchy->Reverse();
}

// Compare 'this' popup position in Wayland widget hierarchy
// (mWaylandPopupPrev/mWaylandPopupNext) with
// 'this' popup position in layout hierarchy.
//
// When aMustMatchParent is true we also request
// 'this' parents match, i.e. 'this' has the same parent in
// both layout and widget hierarchy.
bool nsWindow::IsPopupInLayoutPopupChain(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy, bool aMustMatchParent) {
  int len = (int)aLayoutWidgetHierarchy->Length();
  for (int i = 0; i < len; i++) {
    if (this == (*aLayoutWidgetHierarchy)[i]) {
      if (!aMustMatchParent) {
        return true;
      }

      // Find correct parent popup for 'this' according to widget
      // hierarchy. That means we need to skip closed popups.
      nsWindow* parentPopup = nullptr;
      if (mWaylandPopupPrev != mWaylandToplevel) {
        parentPopup = mWaylandPopupPrev;
        while (parentPopup != mWaylandToplevel && parentPopup->mPopupClosed) {
          parentPopup = parentPopup->mWaylandPopupPrev;
        }
      }

      if (i == 0) {
        // We found 'this' popups as a first popup in layout hierarchy.
        // It matches layout hierarchy if it's first widget also in
        // wayland widget hierarchy (i.e. parent is null).
        return parentPopup == nullptr;
      }

      return parentPopup == (*aLayoutWidgetHierarchy)[i - 1];
    }
  }
  return false;
}

// Hide popups which are not in popup chain.
void nsWindow::WaylandPopupHierarchyHideByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindow::WaylandPopupHierarchyHideByLayout");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  // Hide all popups which are not in layout popup chain
  nsWindow* popup = mWaylandPopupNext;
  while (popup) {
    // Don't check closed popups and drag source popups and tooltips.
    if (!popup->mPopupClosed && popup->mPopupType != PopupType::Tooltip &&
        !popup->mSourceDragContext) {
      if (!popup->IsPopupInLayoutPopupChain(aLayoutWidgetHierarchy,
                                            /* aMustMatchParent */ false)) {
        LOG("  hidding popup [%p]", popup);
        popup->WaylandPopupMarkAsClosed();
      }
    }
    popup = popup->mWaylandPopupNext;
  }
}

// Mark popups outside of layout hierarchy
void nsWindow::WaylandPopupHierarchyValidateByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindow::WaylandPopupHierarchyValidateByLayout");
  nsWindow* popup = mWaylandPopupNext;
  while (popup) {
    if (popup->mPopupType == PopupType::Tooltip) {
      popup->mPopupMatchesLayout = true;
    } else if (!popup->mPopupClosed) {
      popup->mPopupMatchesLayout = popup->IsPopupInLayoutPopupChain(
          aLayoutWidgetHierarchy, /* aMustMatchParent */ true);
      LOG("  popup [%p] parent window [%p] matches layout %d\n", (void*)popup,
          (void*)popup->mWaylandPopupPrev, popup->mPopupMatchesLayout);
    }
    popup = popup->mWaylandPopupNext;
  }
}

void nsWindow::WaylandPopupHierarchyHideTemporary() {
  LOG("nsWindow::WaylandPopupHierarchyHideTemporary()");
  nsWindow* popup = WaylandPopupFindLast(this);
  while (popup && popup != this) {
    LOG("  temporary hidding popup [%p]", popup);
    nsWindow* prev = popup->mWaylandPopupPrev;
    popup->HideWaylandPopupWindow(/* aTemporaryHide */ true,
                                  /* aRemoveFromPopupList */ false);
    popup = prev;
  }
}

void nsWindow::WaylandPopupHierarchyShowTemporaryHidden() {
  LOG("nsWindow::WaylandPopupHierarchyShowTemporaryHidden()");
  nsWindow* popup = this;
  while (popup) {
    if (popup->mPopupTemporaryHidden) {
      popup->mPopupTemporaryHidden = false;
      LOG("  showing temporary hidden popup [%p]", popup);
      popup->ShowWaylandPopupWindow();
    }
    popup = popup->mWaylandPopupNext;
  }
}

void nsWindow::WaylandPopupHierarchyCalculatePositions() {
  LOG("nsWindow::WaylandPopupHierarchyCalculatePositions()");

  // Set widget hierarchy in Gtk
  nsWindow* popup = mWaylandToplevel->mWaylandPopupNext;
  while (popup) {
    LOG("  popup [%p] set parent window [%p]", (void*)popup,
        (void*)popup->mWaylandPopupPrev);
    GtkWindowSetTransientFor(GTK_WINDOW(popup->mShell),
                             GTK_WINDOW(popup->mWaylandPopupPrev->mShell));
    popup = popup->mWaylandPopupNext;
  }

  popup = this;
  while (popup) {
    // Anchored window has mPopupPosition already calculated against
    // its parent, no need to recalculate.
    LOG("  popup [%p] bounds [%d, %d] -> [%d x %d]", popup,
        (int)(popup->mBounds.x / FractionalScaleFactor()),
        (int)(popup->mBounds.y / FractionalScaleFactor()),
        (int)(popup->mBounds.width / FractionalScaleFactor()),
        (int)(popup->mBounds.height / FractionalScaleFactor()));
#ifdef MOZ_LOGGING
    if (LOG_ENABLED()) {
      if (nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame())) {
        auto r = LayoutDeviceRect::FromAppUnitsRounded(
            popupFrame->GetRect(),
            popupFrame->PresContext()->AppUnitsPerDevPixel());
        LOG("  popup [%p] layout [%d, %d] -> [%d x %d]", popup, r.x, r.y,
            r.width, r.height);
      }
    }
#endif
    if (popup->WaylandPopupIsFirst()) {
      LOG("  popup [%p] has toplevel as parent", popup);
      popup->mRelativePopupPosition = popup->mPopupPosition;
    } else {
      if (popup->mPopupAnchored) {
        LOG("  popup [%p] is anchored", popup);
        if (!popup->mPopupMatchesLayout) {
          NS_WARNING("Anchored popup does not match layout!");
        }
      }
      GdkPoint parent = popup->WaylandGetParentPosition();

      LOG("  popup [%p] uses transformed coordinates\n", popup);
      LOG("    parent position [%d, %d]\n", parent.x, parent.y);
      LOG("    popup position [%d, %d]\n", popup->mPopupPosition.x,
          popup->mPopupPosition.y);

      popup->mRelativePopupPosition.x = popup->mPopupPosition.x - parent.x;
      popup->mRelativePopupPosition.y = popup->mPopupPosition.y - parent.y;
    }
    LOG("  popup [%p] transformed popup coordinates from [%d, %d] to [%d, %d]",
        popup, popup->mPopupPosition.x, popup->mPopupPosition.y,
        popup->mRelativePopupPosition.x, popup->mRelativePopupPosition.y);
    popup = popup->mWaylandPopupNext;
  }
}

// The MenuList popups are used as dropdown menus for example in WebRTC
// microphone/camera chooser or autocomplete widgets.
bool nsWindow::WaylandPopupIsMenu() {
  nsMenuPopupFrame* menuPopupFrame = GetMenuPopupFrame(GetFrame());
  if (menuPopupFrame) {
    return mPopupType == PopupType::Menu && !menuPopupFrame->IsMenuList();
  }
  return false;
}

bool nsWindow::WaylandPopupIsContextMenu() {
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  if (!popupFrame) {
    return false;
  }
  return popupFrame->IsContextMenu();
}

bool nsWindow::WaylandPopupIsPermanent() {
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return popupFrame->IsNoAutoHide();
}

bool nsWindow::WaylandPopupIsAnchored() {
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return !!popupFrame->GetAnchor();
}

bool nsWindow::IsWidgetOverflowWindow() {
  if (this->GetFrame() && this->GetFrame()->GetContent()->GetID()) {
    nsCString nodeId;
    this->GetFrame()->GetContent()->GetID()->ToUTF8String(nodeId);
    return nodeId.Equals("widget-overflow");
  }
  return false;
}

bool nsWindow::WaylandPopupIsFirst() {
  return !mWaylandPopupPrev || !mWaylandPopupPrev->mWaylandToplevel;
}

nsWindow* nsWindow::GetEffectiveParent() {
  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (!parentGtkWindow || !GTK_IS_WIDGET(parentGtkWindow)) {
    return nullptr;
  }
  return get_window_for_gtk_widget(GTK_WIDGET(parentGtkWindow));
}

GdkPoint nsWindow::WaylandGetParentPosition() {
  GdkPoint topLeft = {0, 0};
  nsWindow* window = GetEffectiveParent();
  if (window->IsPopup()) {
    topLeft = DevicePixelsToGdkPointRoundDown(window->WidgetToScreenOffset());
  }
  LOG("nsWindow::WaylandGetParentPosition() [%d, %d]\n", topLeft.x, topLeft.y);
  return topLeft;
}

#ifdef MOZ_LOGGING
void nsWindow::LogPopupHierarchy() {
  if (!LOG_ENABLED()) {
    return;
  }

  LOG("Widget Popup Hierarchy:\n");
  if (!mWaylandToplevel->mWaylandPopupNext) {
    LOG("    Empty\n");
  } else {
    int indent = 4;
    nsWindow* popup = mWaylandToplevel->mWaylandPopupNext;
    while (popup) {
      nsPrintfCString indentString("%*s", indent, " ");
      LOG("%s %s %s nsWindow [%p] Menu %d Permanent %d ContextMenu %d "
          "Anchored %d Visible %d MovedByRect %d\n",
          indentString.get(), popup->GetFrameTag().get(),
          popup->GetPopupTypeName().get(), popup, popup->WaylandPopupIsMenu(),
          popup->WaylandPopupIsPermanent(), popup->mPopupContextMenu,
          popup->mPopupAnchored, gtk_widget_is_visible(popup->mShell),
          popup->mPopupUseMoveToRect);
      indent += 4;
      popup = popup->mWaylandPopupNext;
    }
  }

  LOG("Layout Popup Hierarchy:\n");
  AutoTArray<nsIWidget*, 5> widgetChain;
  GetLayoutPopupWidgetChain(&widgetChain);
  if (widgetChain.Length() == 0) {
    LOG("    Empty\n");
  } else {
    for (unsigned long i = 0; i < widgetChain.Length(); i++) {
      nsWindow* window = static_cast<nsWindow*>(widgetChain[i]);
      nsPrintfCString indentString("%*s", (int)(i + 1) * 4, " ");
      if (window) {
        LOG("%s %s %s nsWindow [%p] Menu %d Permanent %d ContextMenu %d "
            "Anchored %d Visible %d MovedByRect %d\n",
            indentString.get(), window->GetFrameTag().get(),
            window->GetPopupTypeName().get(), window,
            window->WaylandPopupIsMenu(), window->WaylandPopupIsPermanent(),
            window->mPopupContextMenu, window->mPopupAnchored,
            gtk_widget_is_visible(window->mShell), window->mPopupUseMoveToRect);
      } else {
        LOG("%s null window\n", indentString.get());
      }
    }
  }
}
#endif

nsWindow* nsWindow::GetTopmostWindow() {
  if (nsView* view = nsView::GetViewFor(this)) {
    if (nsView* parentView = view->GetParent()) {
      if (nsIWidget* parentWidget = parentView->GetNearestWidget(nullptr)) {
        return static_cast<nsWindow*>(parentWidget);
      }
    }
  }
  return nullptr;
}

// Configure Wayland popup. If true is returned we need to track popup
// in popup hierarchy. Otherwise we just show it as is.
bool nsWindow::WaylandPopupConfigure() {
  if (mIsDragPopup) {
    return false;
  }

  // Don't track popups without frame
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  if (!popupFrame) {
    return false;
  }

  // Popup state can be changed, see Bug 1728952.
  bool permanentStateMatches =
      mPopupTrackInHierarchy == !WaylandPopupIsPermanent();

  // Popup permanent state (noautohide attribute) can change during popup life.
  if (mPopupTrackInHierarchyConfigured && permanentStateMatches) {
    return mPopupTrackInHierarchy;
  }

  // Configure persistent popup params only once.
  // WaylandPopupIsAnchored() can give it wrong value after
  // nsMenuPopupFrame::MoveTo() call which we use in move-to-rect callback
  // to position popup after wayland position change.
  if (!mPopupTrackInHierarchyConfigured) {
    mPopupAnchored = WaylandPopupIsAnchored();
    mPopupContextMenu = WaylandPopupIsContextMenu();
  }

  LOG("nsWindow::WaylandPopupConfigure tracked %d anchored %d hint %d\n",
      mPopupTrackInHierarchy, mPopupAnchored, int(mPopupType));

  // Permanent state changed and popup is mapped.
  // We need to switch popup type but that's done when popup is mapped
  // by Gtk so we need to unmap the popup here.
  // It will be mapped again by gtk_widget_show().
  if (!permanentStateMatches && mIsMapped) {
    LOG("  permanent state change from %d to %d, unmapping",
        mPopupTrackInHierarchy, !WaylandPopupIsPermanent());
    gtk_widget_unmap(mShell);
  }

  mPopupTrackInHierarchy = !WaylandPopupIsPermanent();
  LOG("  tracked in hierarchy %d\n", mPopupTrackInHierarchy);

  // See gdkwindow-wayland.c and
  // should_map_as_popup()/should_map_as_subsurface()
  GdkWindowTypeHint gtkTypeHint;
  switch (mPopupType) {
    case PopupType::Menu:
      // GDK_WINDOW_TYPE_HINT_POPUP_MENU is mapped as xdg_popup by default.
      // We use this type for all menu popups.
      gtkTypeHint = GDK_WINDOW_TYPE_HINT_POPUP_MENU;
      LOG("  popup type Menu");
      break;
    case PopupType::Tooltip:
      gtkTypeHint = GDK_WINDOW_TYPE_HINT_TOOLTIP;
      LOG("  popup type Tooltip");
      break;
    default:
      gtkTypeHint = GDK_WINDOW_TYPE_HINT_UTILITY;
      LOG("  popup type Utility");
      break;
  }

  if (!mPopupTrackInHierarchy) {
    // GDK_WINDOW_TYPE_HINT_UTILITY is mapped as wl_subsurface
    // by default.
    LOG("  not tracked in popup hierarchy, switch to Utility");
    gtkTypeHint = GDK_WINDOW_TYPE_HINT_UTILITY;
  }
  gtk_window_set_type_hint(GTK_WINDOW(mShell), gtkTypeHint);

  mPopupTrackInHierarchyConfigured = true;
  return mPopupTrackInHierarchy;
}

bool nsWindow::IsInPopupHierarchy() {
  return mPopupTrackInHierarchy && mWaylandToplevel && mWaylandPopupPrev;
}

void nsWindow::AddWindowToPopupHierarchy() {
  LOG("nsWindow::AddWindowToPopupHierarchy\n");
  if (!GetFrame()) {
    LOG("  Window without frame cannot be added as popup!\n");
    return;
  }

  // Check if we're already in the hierarchy
  if (!IsInPopupHierarchy()) {
    mWaylandToplevel = GetTopmostWindow();
    AppendPopupToHierarchyList(mWaylandToplevel);
  }
}

// Wayland keeps strong popup window hierarchy. We need to track active
// (visible) popup windows and make sure we hide popup on the same level
// before we open another one on that level. It means that every open
// popup needs to have an unique parent.
void nsWindow::UpdateWaylandPopupHierarchy() {
  LOG("nsWindow::UpdateWaylandPopupHierarchy\n");

  // This popup hasn't been added to popup hierarchy yet so no need to
  // do any configurations.
  if (!IsInPopupHierarchy()) {
    LOG("  popup isn't in hierarchy\n");
    return;
  }

#ifdef MOZ_LOGGING
  LogPopupHierarchy();
  auto printPopupHierarchy = MakeScopeExit([&] { LogPopupHierarchy(); });
#endif

  // Hide all tooltips without the last one. Tooltip can't be popup parent.
  mWaylandToplevel->WaylandPopupHideTooltips();

  // See Bug 1709254 / https://gitlab.gnome.org/GNOME/gtk/-/issues/5092
  // It's possible that Wayland compositor refuses to show
  // a popup although Gtk claims it's visible.
  // We don't know if the popup is shown or not.
  // To avoid application crash refuse to create any child of such invisible
  // popup and close any child of it now.
  mWaylandToplevel->WaylandPopupCloseOrphanedPopups();

  // Check if we have any remote content / overflow window in hierarchy.
  // We can't attach such widget on top of other popup.
  mWaylandToplevel->CloseAllPopupsBeforeRemotePopup();

  // Check if your popup hierarchy matches layout hierarchy.
  // For instance we should not connect hamburger menu on top
  // of context menu.
  // Close all popups from different layout chains if possible.
  AutoTArray<nsIWidget*, 5> layoutPopupWidgetChain;
  GetLayoutPopupWidgetChain(&layoutPopupWidgetChain);

  mWaylandToplevel->WaylandPopupHierarchyHideByLayout(&layoutPopupWidgetChain);
  mWaylandToplevel->WaylandPopupHierarchyValidateByLayout(
      &layoutPopupWidgetChain);

  // Now we have Popup hierarchy complete.
  // Find first unchanged (and still open) popup to start with hierarchy
  // changes.
  nsWindow* changedPopup = mWaylandToplevel->mWaylandPopupNext;
  while (changedPopup) {
    // Stop when parent of this popup was changed and we need to recalc
    // popup position.
    if (changedPopup->mPopupChanged) {
      break;
    }
    // Stop when this popup is closed.
    if (changedPopup->mPopupClosed) {
      break;
    }
    changedPopup = changedPopup->mWaylandPopupNext;
  }

  // We don't need to recompute popup positions, quit now.
  if (!changedPopup) {
    LOG("  changed Popup is null, quit.\n");
    return;
  }

  LOG("  first changed popup [%p]\n", (void*)changedPopup);

  // Hide parent popups if necessary (there are layout discontinuity)
  // reposition the popup and show them again.
  changedPopup->WaylandPopupHierarchyHideTemporary();

  nsWindow* parentOfchangedPopup = nullptr;
  if (changedPopup->mPopupClosed) {
    parentOfchangedPopup = changedPopup->mWaylandPopupPrev;
  }
  changedPopup->WaylandPopupRemoveClosedPopups();

  // It's possible that changedPopup was removed from widget hierarchy,
  // in such case use child popup of the removed one if there's any.
  if (!changedPopup->IsInPopupHierarchy()) {
    if (!parentOfchangedPopup || !parentOfchangedPopup->mWaylandPopupNext) {
      LOG("  last popup was removed, quit.\n");
      return;
    }
    changedPopup = parentOfchangedPopup->mWaylandPopupNext;
  }

  GetLayoutPopupWidgetChain(&layoutPopupWidgetChain);
  mWaylandToplevel->WaylandPopupHierarchyValidateByLayout(
      &layoutPopupWidgetChain);

  changedPopup->WaylandPopupHierarchyCalculatePositions();

  nsWindow* popup = changedPopup;
  while (popup) {
    const bool useMoveToRect = [&] {
      if (!StaticPrefs::widget_wayland_use_move_to_rect_AtStartup()) {
        return false;  // Not available.
      }
      if (!popup->mPopupMatchesLayout) {
        // We can use move_to_rect only when popups in popup hierarchy matches
        // layout hierarchy as move_to_rect request that parent/child
        // popups are adjacent.
        return false;
      }
      if (popup->mPopupType == PopupType::Panel &&
          popup->WaylandPopupIsFirst() &&
          popup->WaylandPopupFitsToplevelWindow(/* aMove */ true)) {
        // Workaround for https://gitlab.gnome.org/GNOME/gtk/-/issues/1986
        //
        // PopupType::Panel types are used for extension popups which may be
        // resized. If such popup uses move-to-rect, we need to hide it before
        // resize and show it again. That leads to massive flickering
        // so use plain move if possible to avoid it.
        //
        // Bug 1760276 - don't use move-to-rect when popup is inside main
        // Firefox window.
        //
        // Use it for first popups only due to another mutter bug
        // https://gitlab.gnome.org/GNOME/gtk/-/issues/5089
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1784873
        return false;
      }
      if (!popup->WaylandPopupIsFirst() &&
          !popup->mWaylandPopupPrev->WaylandPopupIsFirst() &&
          !popup->mWaylandPopupPrev->mPopupUseMoveToRect) {
        // We can't use move-to-rect if there are more parents of
        // wl_subsurface popups types.
        //
        // It's because wl_subsurface is ignored by xgd_popup
        // (created by move-to-rect) so our popup scenario:
        //
        // toplevel -> xgd_popup(1) -> wl_subsurface(2) -> xgd_popup(3)
        //
        // looks for Wayland compositor as:
        //
        // toplevel -> xgd_popup(1) -> xgd_popup(3)
        //
        // If xgd_popup(1) and xgd_popup(3) are not connected
        // move-to-rect applied to xgd_popup(3) fails and we get missing popup.
        return false;
      }
      return true;
    }();

    LOG("  popup [%p] matches layout [%d] anchored [%d] first popup [%d] use "
        "move-to-rect %d\n",
        popup, popup->mPopupMatchesLayout, popup->mPopupAnchored,
        popup->WaylandPopupIsFirst(), useMoveToRect);

    popup->mPopupUseMoveToRect = useMoveToRect;
    popup->WaylandPopupMoveImpl();
    popup->mPopupChanged = false;
    popup = popup->mWaylandPopupNext;
  }

  changedPopup->WaylandPopupHierarchyShowTemporaryHidden();
}

static void NativeMoveResizeCallback(GdkWindow* window,
                                     const GdkRectangle* flipped_rect,
                                     const GdkRectangle* final_rect,
                                     gboolean flipped_x, gboolean flipped_y,
                                     void* aWindow) {
  LOG_POPUP("[%p] NativeMoveResizeCallback flipped_x %d flipped_y %d\n",
            aWindow, flipped_x, flipped_y);
  LOG_POPUP("[%p]    new position [%d, %d] -> [%d x %d]", aWindow,
            final_rect->x, final_rect->y, final_rect->width,
            final_rect->height);
  nsWindow* wnd = get_window_for_gdk_window(window);

  wnd->NativeMoveResizeWaylandPopupCallback(final_rect, flipped_x, flipped_y);
}

// When popup is repositioned by widget code, we need to notify
// layout about it. It's because we control popup placement
// on widget on Wayland so layout may have old popup size/coordinates.
void nsWindow::WaylandPopupPropagateChangesToLayout(bool aMove, bool aResize) {
  LOG("nsWindow::WaylandPopupPropagateChangesToLayout()");

  if (aResize) {
    LOG("  needSizeUpdate\n");
    if (nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame())) {
      RefPtr<PresShell> presShell = popupFrame->PresShell();
      presShell->FrameNeedsReflow(popupFrame, IntrinsicDirty::None,
                                  NS_FRAME_IS_DIRTY);
    }
  }
  if (aMove) {
    LOG("  needPositionUpdate, bounds [%d, %d]", mBounds.x, mBounds.y);
    NotifyWindowMoved(mBounds.x, mBounds.y, ByMoveToRect::Yes);
  }
}

void nsWindow::NativeMoveResizeWaylandPopupCallback(
    const GdkRectangle* aFinalSize, bool aFlippedX, bool aFlippedY) {
  // We're getting move-to-rect callback without move-to-rect call.
  // That indicates a compositor bug. It happens when a window is hidden and
  // shown again before move-to-rect callback is fired.
  // It may lead to incorrect popup placement as we may call
  // gtk_window_move() between hide & show.
  // See Bug 1777919, 1789581.
#if MOZ_LOGGING
  if (!mWaitingForMoveToRectCallback) {
    LOG("  Bogus move-to-rect callback! Expect wrong popup coordinates.");
  }
#endif

  mWaitingForMoveToRectCallback = false;

  bool movedByLayout = mMovedAfterMoveToRect;
  bool resizedByLayout = mResizedAfterMoveToRect;

  // Popup was moved between move-to-rect call and move-to-rect callback
  // and the coordinates from move-to-rect callback are outdated.
  if (movedByLayout || resizedByLayout) {
    LOG("  Another move/resize called during waiting for callback\n");
    mMovedAfterMoveToRect = false;
    mResizedAfterMoveToRect = false;
    // Fire another round of move/resize to reflect latest request from layout.
    NativeMoveResize(movedByLayout, resizedByLayout);
    return;
  }

  const GdkRectangle finalGdkRect = [&] {
    GdkRectangle finalRect = *aFinalSize;
    GdkPoint parent = WaylandGetParentPosition();
    finalRect.x += parent.x;
    finalRect.y += parent.y;
    return finalRect;
  }();

  // With fractional scaling, our devPx->Gdk->devPx conversion might not
  // perfectly round-trip. Compare gdk rects to check whether size or positions
  // have changed from what we'd request otherwise, in order to avoid
  // flickering.
  const GdkRectangle currentGdkRect = DevicePixelsToGdkRectRoundOut(mBounds);
  auto scale = GdkCeiledScaleFactor();
  auto IsSubstantiallyDifferent = [=](gint a, gint b) {
    return std::abs(a - b) > scale;
  };

  const bool needsPositionUpdate =
      IsSubstantiallyDifferent(finalGdkRect.x, currentGdkRect.x) ||
      IsSubstantiallyDifferent(finalGdkRect.y, currentGdkRect.y);
  const bool needsSizeUpdate =
      IsSubstantiallyDifferent(finalGdkRect.width, currentGdkRect.width) ||
      IsSubstantiallyDifferent(finalGdkRect.height, currentGdkRect.height);

  const LayoutDeviceIntRect newBounds =
      MaybeRoundToDisplayPixels(GdkRectToDevicePixels(finalGdkRect));
  LOG("  orig gdk [%d, %d] -> [%d x %d]\n", currentGdkRect.x, currentGdkRect.y,
      currentGdkRect.width, currentGdkRect.height);
  LOG("  new gdk [%d, %d] -> [%d x %d]\n", finalGdkRect.x, finalGdkRect.y,
      finalGdkRect.width, finalGdkRect.height);
  LOG("  orig mBounds [%d, %d] -> [%d x %d]\n", mBounds.x, mBounds.y,
      mBounds.width, mBounds.height);
  LOG("  new mBounds [%d, %d] -> [%d x %d]", newBounds.x, newBounds.y,
      newBounds.width, newBounds.height);

  if (!needsSizeUpdate && !needsPositionUpdate) {
    return;
  }
  if (needsSizeUpdate) {
    // Wayland compositor changed popup size request from layout.
    // Set the constraints to use them in nsMenuPopupFrame::SetPopupPosition().
    // Beware that gtk_window_resize() requests sizes asynchronously and so
    // newBounds might not have the size from the most recent
    // gtk_window_resize().
    if (newBounds.width < mLastSizeRequest.width) {
      mMoveToRectPopupSize.width = newBounds.width;
    }
    if (newBounds.height < mLastSizeRequest.height) {
      mMoveToRectPopupSize.height = newBounds.height;
    }
    LOG("  mMoveToRectPopupSize set to [%d, %d]", mMoveToRectPopupSize.width,
        mMoveToRectPopupSize.height);
  }
  mBounds = newBounds;
  mLastSizeRequest = newBounds.Size();
  mLastMoveRequest = newBounds.TopLeft();
  // Check mBounds size
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(mBounds.width, mBounds.height)) {
    gfxCriticalNoteOnce << "Invalid mBounds in PopupCallback " << mBounds
                        << " size state " << mSizeMode;
  }
  WaylandPopupPropagateChangesToLayout(needsPositionUpdate, needsSizeUpdate);
}

static GdkGravity PopupAlignmentToGdkGravity(int8_t aAlignment) {
  switch (aAlignment) {
    case POPUPALIGNMENT_NONE:
      return GDK_GRAVITY_NORTH_WEST;
    case POPUPALIGNMENT_TOPLEFT:
      return GDK_GRAVITY_NORTH_WEST;
    case POPUPALIGNMENT_TOPRIGHT:
      return GDK_GRAVITY_NORTH_EAST;
    case POPUPALIGNMENT_BOTTOMLEFT:
      return GDK_GRAVITY_SOUTH_WEST;
    case POPUPALIGNMENT_BOTTOMRIGHT:
      return GDK_GRAVITY_SOUTH_EAST;
    case POPUPALIGNMENT_LEFTCENTER:
      return GDK_GRAVITY_WEST;
    case POPUPALIGNMENT_RIGHTCENTER:
      return GDK_GRAVITY_EAST;
    case POPUPALIGNMENT_TOPCENTER:
      return GDK_GRAVITY_NORTH;
    case POPUPALIGNMENT_BOTTOMCENTER:
      return GDK_GRAVITY_SOUTH;
  }
  return GDK_GRAVITY_STATIC;
}

bool nsWindow::IsPopupDirectionRTL() {
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  return popupFrame && popupFrame->IsDirectionRTL();
}

// Position the popup directly by gtk_window_move() and try to keep it
// on screen by just moving it in scope of it's parent window.
//
// It's used when we position noautihode popup and we don't use xdg_positioner.
// See Bug 1718867
void nsWindow::WaylandPopupSetDirectPosition() {
  const LayoutDeviceIntRect frameRect(mLastMoveRequest, mLastSizeRequest);
  GdkRectangle rect = DevicePixelsToGdkRectRoundOut(frameRect);

  LOG("nsWindow::WaylandPopupSetDirectPosition %d,%d -> %d x %d\n", rect.x,
      rect.y, rect.width, rect.height);

  mPopupPosition = {rect.x, rect.y};
  mBounds = frameRect;

  if (mIsDragPopup) {
    gtk_window_move(GTK_WINDOW(mShell), rect.x, rect.y);
    gtk_window_resize(GTK_WINDOW(mShell), rect.width, rect.height);
    // DND window is placed inside container so we need to make hard size
    // request to ensure parent container is resized too.
    gtk_widget_set_size_request(GTK_WIDGET(mShell), rect.width, rect.height);
    return;
  }

  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  nsWindow* window = get_window_for_gtk_widget(GTK_WIDGET(parentGtkWindow));
  if (!window) {
    return;
  }
  GdkWindow* gdkWindow = window->GetGdkWindow();
  if (!gdkWindow) {
    return;
  }

  int parentWidth = gdk_window_get_width(gdkWindow);
  int popupWidth = rect.width;

  int x;
  gdk_window_get_position(gdkWindow, &x, nullptr);

  // If popup is bigger than main window just center it.
  if (popupWidth > parentWidth) {
    mPopupPosition.x = -(parentWidth - popupWidth) / 2 + x;
  } else {
    if (IsPopupDirectionRTL()) {
      // Stick with right window edge
      if (mPopupPosition.x < x) {
        mPopupPosition.x = x;
      }
    } else {
      // Stick with left window edge
      if (mPopupPosition.x + popupWidth > parentWidth + x) {
        mPopupPosition.x = parentWidth + x - popupWidth;
      }
    }
  }

  LOG("  set position [%d, %d]\n", mPopupPosition.x, mPopupPosition.y);
  gtk_window_move(GTK_WINDOW(mShell), mPopupPosition.x, mPopupPosition.y);

  LOG("  set size [%d, %d]\n", rect.width, rect.height);
  gtk_window_resize(GTK_WINDOW(mShell), rect.width, rect.height);

  if (mPopupPosition.x != rect.x) {
    mBounds.MoveTo(GdkPointToDevicePixels(mPopupPosition));
    LOG("  setting new bounds [%d, %d]\n", mBounds.x, mBounds.y);
    WaylandPopupPropagateChangesToLayout(/* move */ true, /* resize */ false);
  }
}

bool nsWindow::WaylandPopupFitsToplevelWindow(bool aMove) {
  LOG("nsWindow::WaylandPopupFitsToplevelWindow() move %d", aMove);

  GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  GtkWindow* tmp = parent;
  while ((tmp = gtk_window_get_transient_for(GTK_WINDOW(parent)))) {
    parent = tmp;
  }
  GdkWindow* toplevelGdkWindow = gtk_widget_get_window(GTK_WIDGET(parent));
  if (!toplevelGdkWindow) {
    NS_WARNING("Toplevel widget without GdkWindow?");
    return false;
  }

  int parentWidth = gdk_window_get_width(toplevelGdkWindow);
  int parentHeight = gdk_window_get_height(toplevelGdkWindow);
  LOG("  parent size %d x %d", parentWidth, parentHeight);

  GdkRectangle requestedRect = DevicePixelsToGdkRectRoundOut(
      LayoutDeviceIntRect(mLastMoveRequest, mLastSizeRequest));
  GdkPoint topLeft =
      aMove ? mPopupPosition : GdkPoint{requestedRect.x, requestedRect.y};
  LOG("  popup topleft %d, %d size %d x %d", topLeft.x, topLeft.y,
      requestedRect.width, requestedRect.height);
  int fits = topLeft.x >= 0 && topLeft.y >= 0 &&
             topLeft.x + requestedRect.width <= parentWidth &&
             topLeft.y + requestedRect.height <= parentHeight;

  LOG("  fits %d", fits);
  return fits;
}

void nsWindow::NativeMoveResizeWaylandPopup(bool aMove, bool aResize) {
  const LayoutDeviceIntRect frameRect(mLastMoveRequest, mLastSizeRequest);
  GdkRectangle rect = DevicePixelsToGdkRectRoundOut(frameRect);

  LOG("nsWindow::NativeMoveResizeWaylandPopup Bounds %d,%d -> %d x %d move %d "
      "resize %d\n",
      rect.x, rect.y, rect.width, rect.height, aMove, aResize);

  // Compositor may be confused by windows with width/height = 0
  // and positioning such windows leads to Bug 1555866.
  if (!AreBoundsSane()) {
    LOG("  Bounds are not sane (width: %d height: %d)\n",
        mLastSizeRequest.width, mLastSizeRequest.height);
    return;
  }

  if (mWaitingForMoveToRectCallback) {
    LOG("  waiting for move to rect, scheduling");
    // mBounds position must not be overwritten before it is applied.
    // OnShellConfigureEvent() will not set mBounds to an old position for
    // GTK_WINDOW_POPUP.
    MOZ_ASSERT(gtk_window_get_window_type(GTK_WINDOW(mShell)) ==
               GTK_WINDOW_POPUP);
    mMovedAfterMoveToRect = aMove;
    mResizedAfterMoveToRect = aResize;
    return;
  }

  mMovedAfterMoveToRect = false;
  mResizedAfterMoveToRect = false;

  bool trackedInHierarchy = WaylandPopupConfigure();

  // Read popup position from layout if it was moved or newly created.
  // This position is used by move-to-rect method as we need anchor and other
  // info to place popup correctly.
  // We need WaylandPopupConfigure() to be called before to have all needed
  // popup info in place (mainly the anchored flag).
  if (aMove) {
    mPopupMoveToRectParams = WaylandPopupGetPositionFromLayout();
  }

  if (!trackedInHierarchy) {
    WaylandPopupSetDirectPosition();
    return;
  }

  if (aResize) {
    LOG("  set size [%d, %d]\n", rect.width, rect.height);
    gtk_window_resize(GTK_WINDOW(mShell), rect.width, rect.height);
  }

  if (!aMove && WaylandPopupFitsToplevelWindow(aMove)) {
    // Popup position has not been changed and its position/size fits
    // parent window so no need to reposition the window.
    LOG("  fits parent window size, just resize\n");
    return;
  }

  // Mark popup as changed as we're updating position/size.
  mPopupChanged = true;

  // Save popup position for former re-calculations when popup hierarchy
  // is changed.
  LOG("  popup position changed from [%d, %d] to [%d, %d]\n", mPopupPosition.x,
      mPopupPosition.y, rect.x, rect.y);
  mPopupPosition = {rect.x, rect.y};
  mBounds = frameRect;

  UpdateWaylandPopupHierarchy();
}

struct PopupSides {
  Maybe<Side> mVertical;
  Maybe<Side> mHorizontal;
};

static PopupSides SidesForPopupAlignment(int8_t aAlignment) {
  switch (aAlignment) {
    case POPUPALIGNMENT_NONE:
      break;
    case POPUPALIGNMENT_TOPLEFT:
      return {Some(eSideTop), Some(eSideLeft)};
    case POPUPALIGNMENT_TOPRIGHT:
      return {Some(eSideTop), Some(eSideRight)};
    case POPUPALIGNMENT_BOTTOMLEFT:
      return {Some(eSideBottom), Some(eSideLeft)};
    case POPUPALIGNMENT_BOTTOMRIGHT:
      return {Some(eSideBottom), Some(eSideRight)};
    case POPUPALIGNMENT_LEFTCENTER:
      return {Nothing(), Some(eSideLeft)};
    case POPUPALIGNMENT_RIGHTCENTER:
      return {Nothing(), Some(eSideRight)};
    case POPUPALIGNMENT_TOPCENTER:
      return {Some(eSideTop), Nothing()};
    case POPUPALIGNMENT_BOTTOMCENTER:
      return {Some(eSideBottom), Nothing()};
  }
  return {};
}

// We want to apply margins based on popup alignment (which would generally be
// just an offset to apply to the popup). However, to deal with flipping
// correctly, we apply the margin to the anchor when possible.
struct ResolvedPopupMargin {
  // A margin to be applied to the anchor.
  nsMargin mAnchorMargin;
  // An offset in app units to be applied to the popup for when we need to tell
  // GTK to center inside the anchor precisely (so we can't really do better in
  // presence of flips).
  nsPoint mPopupOffset;
};

static ResolvedPopupMargin ResolveMargin(nsMenuPopupFrame* aFrame,
                                         int8_t aPopupAlign,
                                         int8_t aAnchorAlign,
                                         bool aAnchoredToPoint,
                                         bool aIsContextMenu) {
  nsMargin margin = aFrame->GetMargin();
  nsPoint offset;

  if (aAnchoredToPoint) {
    // Since GTK doesn't allow us to specify margins itself, when anchored to a
    // point we can just assume we'll be aligned correctly... This is kind of
    // annoying but alas.
    //
    // This calculation must match the relevant unanchored popup calculation in
    // nsMenuPopupFrame::SetPopupPosition(), which should itself be the inverse
    // inverse of nsMenuPopupFrame::MoveTo().
    if (aIsContextMenu && aFrame->IsDirectionRTL()) {
      offset.x = -margin.right;
    } else {
      offset.x = margin.left;
    }
    offset.y = margin.top;
    return {nsMargin(), offset};
  }

  auto popupSides = SidesForPopupAlignment(aPopupAlign);
  auto anchorSides = SidesForPopupAlignment(aAnchorAlign);
  // Matched sides: Invert the margin, so that we pull in the right direction.
  // Popup not aligned to any anchor side: We give up and use the offset,
  // applying the margin from the popup side.
  // Mismatched sides: We swap the margins so that we pull in the right
  // direction, e.g. margin-left: -10px should shrink 10px the _right_ of the
  // box, not the left of the box.
  if (popupSides.mHorizontal == anchorSides.mHorizontal) {
    margin.left = -margin.left;
    margin.right = -margin.right;
  } else if (!anchorSides.mHorizontal) {
    auto popupSide = *popupSides.mHorizontal;
    offset.x += popupSide == eSideRight ? -margin.Side(popupSide)
                                        : margin.Side(popupSide);
    margin.left = margin.right = 0;
  } else {
    std::swap(margin.left, margin.right);
  }

  // Same logic as above, but in the vertical direction.
  if (popupSides.mVertical == anchorSides.mVertical) {
    margin.top = -margin.top;
    margin.bottom = -margin.bottom;
  } else if (!anchorSides.mVertical) {
    auto popupSide = *popupSides.mVertical;
    offset.y += popupSide == eSideBottom ? -margin.Side(popupSide)
                                         : margin.Side(popupSide);
    margin.top = margin.bottom = 0;
  } else {
    std::swap(margin.top, margin.bottom);
  }

  return {margin, offset};
}

#ifdef MOZ_LOGGING
void nsWindow::LogPopupAnchorHints(int aHints) {
  static struct hints_ {
    int hint;
    char name[100];
  } hints[] = {
      {GDK_ANCHOR_FLIP_X, "GDK_ANCHOR_FLIP_X"},
      {GDK_ANCHOR_FLIP_Y, "GDK_ANCHOR_FLIP_Y"},
      {GDK_ANCHOR_SLIDE_X, "GDK_ANCHOR_SLIDE_X"},
      {GDK_ANCHOR_SLIDE_Y, "GDK_ANCHOR_SLIDE_Y"},
      {GDK_ANCHOR_RESIZE_X, "GDK_ANCHOR_RESIZE_X"},
      {GDK_ANCHOR_RESIZE_Y, "GDK_ANCHOR_RESIZE_X"},
  };

  LOG("  PopupAnchorHints");
  for (const auto& hint : hints) {
    if (hint.hint & aHints) {
      LOG("    %s", hint.name);
    }
  }
}

void nsWindow::LogPopupGravity(GdkGravity aGravity) {
  static char gravity[][100]{"NONE",
                             "GDK_GRAVITY_NORTH_WEST",
                             "GDK_GRAVITY_NORTH",
                             "GDK_GRAVITY_NORTH_EAST",
                             "GDK_GRAVITY_WEST",
                             "GDK_GRAVITY_CENTER",
                             "GDK_GRAVITY_EAST",
                             "GDK_GRAVITY_SOUTH_WEST",
                             "GDK_GRAVITY_SOUTH",
                             "GDK_GRAVITY_SOUTH_EAST",
                             "GDK_GRAVITY_STATIC"};
  LOG("    %s", gravity[aGravity]);
}
#endif

const nsWindow::WaylandPopupMoveToRectParams
nsWindow::WaylandPopupGetPositionFromLayout() {
  LOG("nsWindow::WaylandPopupGetPositionFromLayout\n");

  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());

  const bool isTopContextMenu = mPopupContextMenu && !mPopupAnchored;
  const bool isRTL = popupFrame->IsDirectionRTL();
  const bool anchored = popupFrame->IsAnchored();
  int8_t popupAlign = POPUPALIGNMENT_TOPLEFT;
  int8_t anchorAlign = POPUPALIGNMENT_BOTTOMRIGHT;
  if (anchored) {
    // See nsMenuPopupFrame::AdjustPositionForAnchorAlign.
    popupAlign = popupFrame->GetUntransformedPopupAlignment();
    anchorAlign = popupFrame->GetUntransformedPopupAnchor();
  }
  if (isRTL) {
    popupAlign = -popupAlign;
    anchorAlign = -anchorAlign;
  }

  // Although we have mPopupPosition / mRelativePopupPosition here
  // we can't use it. move-to-rect needs anchor rectangle to position a popup
  // but we have only a point from Resize().
  //
  // So we need to extract popup position from nsMenuPopupFrame() and duplicate
  // the layout work here.
  LayoutDeviceIntRect anchorRect;
  ResolvedPopupMargin popupMargin;
  {
    nsRect anchorRectAppUnits = popupFrame->GetUntransformedAnchorRect();
    // This is a somewhat hacky way of applying the popup margin. We don't know
    // if GTK will end up flipping the popup, in which case the offset we
    // compute is just wrong / applied to the wrong side.
    //
    // Instead, we tell it to anchor us at a smaller or bigger rect depending on
    // the margin, which achieves the same result if the popup is positioned
    // correctly, but doesn't misposition the popup when flipped across the
    // anchor.
    popupMargin = ResolveMargin(popupFrame, popupAlign, anchorAlign,
                                anchorRectAppUnits.IsEmpty(), isTopContextMenu);
    LOG("  layout popup CSS anchor (%d, %d) %s, margin %s offset %s\n",
        popupAlign, anchorAlign, ToString(anchorRectAppUnits).c_str(),
        ToString(popupMargin.mAnchorMargin).c_str(),
        ToString(popupMargin.mPopupOffset).c_str());
    anchorRectAppUnits.Inflate(popupMargin.mAnchorMargin);
    LOG("    after margins %s\n", ToString(anchorRectAppUnits).c_str());
    nscoord auPerDev = popupFrame->PresContext()->AppUnitsPerDevPixel();
    anchorRect = LayoutDeviceIntRect::FromAppUnitsToNearest(anchorRectAppUnits,
                                                            auPerDev);
    if (anchorRect.width < 0) {
      auto w = -anchorRect.width;
      anchorRect.width += w + 1;
      anchorRect.x += w;
    }
    LOG("    final %s\n", ToString(anchorRect).c_str());
  }

  LOG("  relative popup rect position [%d, %d] -> [%d x %d]\n", anchorRect.x,
      anchorRect.y, anchorRect.width, anchorRect.height);

  // Get gravity and flip type
  GdkGravity rectAnchor = PopupAlignmentToGdkGravity(anchorAlign);
  GdkGravity menuAnchor = PopupAlignmentToGdkGravity(popupAlign);

  LOG("  parentRect gravity: %d anchor gravity: %d\n", rectAnchor, menuAnchor);

  // Gtk default is: GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE | GDK_ANCHOR_RESIZE.
  // We want to SLIDE_X menu on the dual monitor setup rather than resize it
  // on the other monitor.
  GdkAnchorHints hints =
      GdkAnchorHints(GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE_X | GDK_ANCHOR_RESIZE);

  // slideHorizontal from nsMenuPopupFrame::SetPopupPosition
  int8_t position = popupFrame->GetAlignmentPosition();
  if (position >= POPUPPOSITION_BEFORESTART &&
      position <= POPUPPOSITION_AFTEREND) {
    hints = GdkAnchorHints(hints | GDK_ANCHOR_SLIDE_X);
  }
  // slideVertical from nsMenuPopupFrame::SetPopupPosition
  if (position >= POPUPPOSITION_STARTBEFORE &&
      position <= POPUPPOSITION_ENDAFTER) {
    hints = GdkAnchorHints(hints | GDK_ANCHOR_SLIDE_Y);
  }

  FlipType flipType = popupFrame->GetFlipType();
  if (rectAnchor == GDK_GRAVITY_CENTER && menuAnchor == GDK_GRAVITY_CENTER) {
    // only slide
    hints = GdkAnchorHints(hints | GDK_ANCHOR_SLIDE);
  } else {
    switch (flipType) {
      case FlipType::Both:
        hints = GdkAnchorHints(hints | GDK_ANCHOR_FLIP);
        break;
      case FlipType::Slide:
        hints = GdkAnchorHints(hints | GDK_ANCHOR_SLIDE);
        break;
      case FlipType::Default:
        hints = GdkAnchorHints(hints | GDK_ANCHOR_FLIP);
        break;
      case FlipType::None:
        break;
    }
  }
  if (!WaylandPopupIsMenu()) {
    // we don't want to slide menus to fit the screen rather resize them
    hints = GdkAnchorHints(hints | GDK_ANCHOR_SLIDE);
  }

  // We want tooltips to flip verticaly or slide only.
  // See nsMenuPopupFrame::SetPopupPosition().
  // https://searchfox.org/mozilla-central/rev/d0f5bc50aff3462c9d1546b88d60c5cb020eb15c/layout/xul/nsMenuPopupFrame.cpp#1603
  if (mPopupType == PopupType::Tooltip) {
    hints = GdkAnchorHints(GDK_ANCHOR_FLIP_Y | GDK_ANCHOR_SLIDE);
  }

  return {
      anchorRect,
      rectAnchor,
      menuAnchor,
      hints,
      DevicePixelsToGdkPointRoundDown(LayoutDevicePoint::FromAppUnitsToNearest(
          popupMargin.mPopupOffset,
          popupFrame->PresContext()->AppUnitsPerDevPixel())),
      true};
}

bool nsWindow::WaylandPopupAnchorAdjustForParentPopup(
    GdkRectangle* aPopupAnchor, GdkPoint* aOffset) {
  LOG("nsWindow::WaylandPopupAnchorAdjustForParentPopup");

  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (!parentGtkWindow || !GTK_IS_WIDGET(parentGtkWindow)) {
    NS_WARNING("Popup has no parent!");
    return false;
  }
  GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(parentGtkWindow));
  if (!window) {
    NS_WARNING("Popup parrent is not mapped!");
    return false;
  }

  GdkRectangle parentWindowRect = {0, 0, gdk_window_get_width(window),
                                   gdk_window_get_height(window)};
  LOG("  parent window size %d x %d", parentWindowRect.width,
      parentWindowRect.height);

  // We can't have rectangle anchor with zero width/height.
  if (!aPopupAnchor->width) {
    aPopupAnchor->width = 1;
  }
  if (!aPopupAnchor->height) {
    aPopupAnchor->height = 1;
  }

  GdkRectangle finalRect;
  if (!gdk_rectangle_intersect(aPopupAnchor, &parentWindowRect, &finalRect)) {
    return false;
  }
  *aPopupAnchor = finalRect;
  LOG("  anchor is correct %d,%d -> %d x %d", finalRect.x, finalRect.y,
      finalRect.width, finalRect.height);

  *aOffset = mPopupMoveToRectParams.mOffset;
  LOG("  anchor offset %d, %d", aOffset->x, aOffset->y);
  return true;
}

bool nsWindow::WaylandPopupCheckAndGetAnchor(GdkRectangle* aPopupAnchor,
                                             GdkPoint* aOffset) {
  LOG("nsWindow::WaylandPopupCheckAndGetAnchor");

  GdkWindow* gdkWindow = GetToplevelGdkWindow();
  nsMenuPopupFrame* popupFrame = GetMenuPopupFrame(GetFrame());
  if (!gdkWindow || !popupFrame) {
    LOG("  can't use move-to-rect due missing gdkWindow or popupFrame");
    return false;
  }

  if (popupFrame->IsConstrainedByLayout()) {
    LOG("  can't use move-to-rect, flipped / constrained by layout");
    return false;
  }

  if (!mPopupMoveToRectParams.mAnchorSet) {
    LOG("  can't use move-to-rect due missing anchor");
    return false;
  }
  // Update popup layout coordinates from layout by recent popup hierarchy
  // (calculate correct position according to parent window)
  // and convert to Gtk coordinates.
  LayoutDeviceIntRect anchorRect = mPopupMoveToRectParams.mAnchorRect;
  if (!WaylandPopupIsFirst()) {
    GdkPoint parent = WaylandGetParentPosition();
    LOG("  subtract parent position from anchor [%d, %d]\n", parent.x,
        parent.y);
    anchorRect.MoveBy(-GdkPointToDevicePixels(parent));
  }

  *aPopupAnchor = DevicePixelsToGdkRectRoundOut(anchorRect);
  LOG("  anchored to rectangle [%d, %d] -> [%d x %d]", aPopupAnchor->x,
      aPopupAnchor->y, aPopupAnchor->width, aPopupAnchor->height);

  if (!WaylandPopupAnchorAdjustForParentPopup(aPopupAnchor, aOffset)) {
    LOG("  can't use move-to-rect, anchor is not placed inside of parent "
        "window");
    return false;
  }

  return true;
}

void nsWindow::WaylandPopupPrepareForMove() {
  LOG("nsWindow::WaylandPopupPrepareForMove()");

  if (mPopupType == PopupType::Tooltip) {
    // Don't fiddle with tooltips type, just hide it before move-to-rect
    if (mPopupUseMoveToRect && gtk_widget_is_visible(mShell)) {
      HideWaylandPopupWindow(/* aTemporaryHide */ true,
                             /* aRemoveFromPopupList */ false);
    }
    LOG("  it's tooltip, quit");
    return;
  }

  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1785185#c8
  // gtk_window_move() needs GDK_WINDOW_TYPE_HINT_UTILITY popup type.
  // move-to-rect requires GDK_WINDOW_TYPE_HINT_POPUP_MENU popups type.
  // We need to set it before map event when popup is hidden.
  const GdkWindowTypeHint currentType =
      gtk_window_get_type_hint(GTK_WINDOW(mShell));
  const GdkWindowTypeHint requiredType = mPopupUseMoveToRect
                                             ? GDK_WINDOW_TYPE_HINT_POPUP_MENU
                                             : GDK_WINDOW_TYPE_HINT_UTILITY;

  if (!mPopupUseMoveToRect && currentType == requiredType) {
    LOG("  type matches and we're not forced to hide it, quit.");
    return;
  }

  if (gtk_widget_is_visible(mShell)) {
    HideWaylandPopupWindow(/* aTemporaryHide */ true,
                           /* aRemoveFromPopupList */ false);
  }

  if (currentType != requiredType) {
    LOG("  set type %s",
        requiredType == GDK_WINDOW_TYPE_HINT_POPUP_MENU ? "MENU" : "UTILITY");
    gtk_window_set_type_hint(GTK_WINDOW(mShell), requiredType);
  }
}

// Plain popup move on Wayland - simply place popup on given location.
// We can't just call gtk_window_move() as it's not effective on visible
// popups.
void nsWindow::WaylandPopupMovePlain(int aX, int aY) {
  LOG("nsWindow::WaylandPopupMovePlain(%d, %d)", aX, aY);

  // We can directly move only popups based on wl_subsurface type.
  MOZ_DIAGNOSTIC_ASSERT(gtk_window_get_type_hint(GTK_WINDOW(mShell)) ==
                            GDK_WINDOW_TYPE_HINT_UTILITY ||
                        gtk_window_get_type_hint(GTK_WINDOW(mShell)) ==
                            GDK_WINDOW_TYPE_HINT_TOOLTIP);

  gtk_window_move(GTK_WINDOW(mShell), aX, aY);

  // gtk_window_move() can trick us. When widget is hidden gtk_window_move()
  // does not move the widget but sets new widget coordinates when widget
  // is mapped again.
  //
  // If popup used move-to-rect before
  // (GdkWindow has POSITION_METHOD_MOVE_TO_RECT set), popup will use
  // move-to-rect again when it's mapped and we'll get bogus move-to-rect
  // callback.
  //
  // gdk_window_move() sets position_method to POSITION_METHOD_MOVE_RESIZE
  // so we'll use plain move when popup is shown.
  if (!gtk_widget_get_mapped(mShell)) {
    if (GdkWindow* window = GetToplevelGdkWindow()) {
      gdk_window_move(window, aX, aY);
    }
  }
}

void nsWindow::WaylandPopupMoveImpl() {
  // Available as of GTK 3.24+
  static auto sGdkWindowMoveToRect = (void (*)(
      GdkWindow*, const GdkRectangle*, GdkGravity, GdkGravity, GdkAnchorHints,
      gint, gint))dlsym(RTLD_DEFAULT, "gdk_window_move_to_rect");

  if (mPopupUseMoveToRect && !sGdkWindowMoveToRect) {
    LOG("can't use move-to-rect due missing gdk_window_move_to_rect()");
    mPopupUseMoveToRect = false;
  }

  GdkRectangle gtkAnchorRect;
  GdkPoint offset;
  if (mPopupUseMoveToRect) {
    mPopupUseMoveToRect =
        WaylandPopupCheckAndGetAnchor(&gtkAnchorRect, &offset);
  }

  LOG("nsWindow::WaylandPopupMove");
  LOG("  original widget popup position [%d, %d]\n", mPopupPosition.x,
      mPopupPosition.y);
  LOG("  relative widget popup position [%d, %d]\n", mRelativePopupPosition.x,
      mRelativePopupPosition.y);
  LOG("  popup use move to rect %d", mPopupUseMoveToRect);

  WaylandPopupPrepareForMove();

  if (!mPopupUseMoveToRect) {
    WaylandPopupMovePlain(mRelativePopupPosition.x, mRelativePopupPosition.y);
    // Layout already should be aware of our bounds, since we didn't change it
    // from the widget side for flipping or so.
    return;
  }

  // Correct popup position now. It will be updated by gdk_window_move_to_rect()
  // anyway but we need to set it now to avoid a race condition here.
  WaylandPopupRemoveNegativePosition();

  GdkWindow* gdkWindow = GetToplevelGdkWindow();
  if (!g_signal_handler_find(gdkWindow, G_SIGNAL_MATCH_FUNC, 0, 0, nullptr,
                             FuncToGpointer(NativeMoveResizeCallback), this)) {
    g_signal_connect(gdkWindow, "moved-to-rect",
                     G_CALLBACK(NativeMoveResizeCallback), this);
  }
  mWaitingForMoveToRectCallback = true;

#ifdef MOZ_LOGGING
  if (LOG_ENABLED()) {
    LOG("  Call move-to-rect");
    LOG("  Anchor rect [%d, %d] -> [%d x %d]", gtkAnchorRect.x, gtkAnchorRect.y,
        gtkAnchorRect.width, gtkAnchorRect.height);
    LOG("  Offset [%d, %d]", offset.x, offset.y);
    LOG("  AnchorType");
    LogPopupGravity(mPopupMoveToRectParams.mAnchorRectType);
    LOG("  PopupAnchorType");
    LogPopupGravity(mPopupMoveToRectParams.mPopupAnchorType);
    LogPopupAnchorHints(mPopupMoveToRectParams.mHints);
  }
#endif

  sGdkWindowMoveToRect(gdkWindow, &gtkAnchorRect,
                       mPopupMoveToRectParams.mAnchorRectType,
                       mPopupMoveToRectParams.mPopupAnchorType,
                       mPopupMoveToRectParams.mHints, offset.x, offset.y);
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

#define kDesktopMutterSchema "org.gnome.mutter"_ns
#define kDesktopDynamicWorkspacesKey "dynamic-workspaces"_ns

static bool WorkspaceManagementDisabled(GdkScreen* screen) {
  if (Preferences::GetBool("widget.disable-workspace-management", false)) {
    return true;
  }
  if (Preferences::HasUserValue("widget.workspace-management")) {
    return Preferences::GetBool("widget.workspace-management");
  }

  if (IsGnomeDesktopEnvironment()) {
    // Gnome uses dynamic workspaces by default so disable workspace management
    // in that case.
    bool usesDynamicWorkspaces = true;
    nsCOMPtr<nsIGSettingsService> gsettings =
        do_GetService(NS_GSETTINGSSERVICE_CONTRACTID);
    if (gsettings) {
      nsCOMPtr<nsIGSettingsCollection> mutterSettings;
      gsettings->GetCollectionForSchema(kDesktopMutterSchema,
                                        getter_AddRefs(mutterSettings));
      if (mutterSettings) {
        mutterSettings->GetBoolean(kDesktopDynamicWorkspacesKey,
                                   &usesDynamicWorkspaces);
      }
    }
    return usesDynamicWorkspaces;
  }

  const auto& desktop = GetDesktopEnvironmentIdentifier();
  return desktop.EqualsLiteral("bspwm") || desktop.EqualsLiteral("i3");
}

void nsWindow::GetWorkspaceID(nsAString& workspaceID) {
  workspaceID.Truncate();

  if (!GdkIsX11Display() || !mShell) {
    return;
  }

#ifdef MOZ_X11
  LOG("nsWindow::GetWorkspaceID()\n");

  // Get the gdk window for this widget.
  GdkWindow* gdk_window = GetToplevelGdkWindow();
  if (!gdk_window) {
    LOG("  missing Gdk window, quit.");
    return;
  }

  if (WorkspaceManagementDisabled(gdk_window_get_screen(gdk_window))) {
    LOG("  WorkspaceManagementDisabled, quit.");
    return;
  }

  GdkAtom cardinal_atom = gdk_x11_xatom_to_atom(XA_CARDINAL);
  GdkAtom type_returned;
  int format_returned;
  int length_returned;
  long* wm_desktop;

  if (!gdk_property_get(gdk_window, gdk_atom_intern("_NET_WM_DESKTOP", FALSE),
                        cardinal_atom,
                        0,          // offset
                        INT32_MAX,  // length
                        FALSE,      // delete
                        &type_returned, &format_returned, &length_returned,
                        (guchar**)&wm_desktop)) {
    LOG("  gdk_property_get() failed, quit.");
    return;
  }

  LOG("  got workspace ID %d", (int32_t)wm_desktop[0]);
  workspaceID.AppendInt((int32_t)wm_desktop[0]);
  g_free(wm_desktop);
#endif
}

void nsWindow::MoveToWorkspace(const nsAString& workspaceIDStr) {
  nsresult rv = NS_OK;
  int32_t workspaceID = workspaceIDStr.ToInteger(&rv);

  LOG("nsWindow::MoveToWorkspace() ID %d", workspaceID);
  if (NS_FAILED(rv) || !workspaceID || !GdkIsX11Display() || !mShell) {
    LOG("  MoveToWorkspace disabled, quit");
    return;
  }

#ifdef MOZ_X11
  // Get the gdk window for this widget.
  GdkWindow* gdk_window = GetToplevelGdkWindow();
  if (!gdk_window) {
    LOG("  failed to get GdkWindow, quit.");
    return;
  }

  // This code is inspired by some found in the 'gxtuner' project.
  // https://github.com/brummer10/gxtuner/blob/792d453da0f3a599408008f0f1107823939d730d/deskpager.cpp#L50
  XEvent xevent;
  Display* xdisplay = gdk_x11_get_default_xdisplay();
  GdkScreen* screen = gdk_window_get_screen(gdk_window);
  Window root_win = GDK_WINDOW_XID(gdk_screen_get_root_window(screen));
  GdkDisplay* display = gdk_window_get_display(gdk_window);
  Atom type = gdk_x11_get_xatom_by_name_for_display(display, "_NET_WM_DESKTOP");

  xevent.type = ClientMessage;
  xevent.xclient.type = ClientMessage;
  xevent.xclient.serial = 0;
  xevent.xclient.send_event = TRUE;
  xevent.xclient.display = xdisplay;
  xevent.xclient.window = GDK_WINDOW_XID(gdk_window);
  xevent.xclient.message_type = type;
  xevent.xclient.format = 32;
  xevent.xclient.data.l[0] = workspaceID;
  xevent.xclient.data.l[1] = X11CurrentTime;
  xevent.xclient.data.l[2] = 0;
  xevent.xclient.data.l[3] = 0;
  xevent.xclient.data.l[4] = 0;

  XSendEvent(xdisplay, root_win, FALSE,
             SubstructureNotifyMask | SubstructureRedirectMask, &xevent);

  XFlush(xdisplay);
  LOG("  moved to workspace");
#endif
}

void nsWindow::SetUserTimeAndStartupTokenForActivatedWindow() {
  nsGTKToolkit* toolkit = nsGTKToolkit::GetToolkit();
  if (!toolkit) {
    return;
  }

  mWindowActivationTokenFromEnv = toolkit->GetStartupToken();
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
  toolkit->SetStartupToken(""_ns);
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

#ifdef MOZ_WAYLAND
void nsWindow::FocusWaylandWindow(const char* aTokenID) {
  MOZ_DIAGNOSTIC_ASSERT(aTokenID);

  LOG("nsWindow::FocusWaylandWindow(%s)", aTokenID);
  if (IsDestroyed()) {
    LOG("  already destroyed, quit.");
    return;
  }
  wl_surface* surface =
      mGdkWindow ? gdk_wayland_window_get_wl_surface(mGdkWindow) : nullptr;
  if (!surface) {
    LOG("  mGdkWindow is not visible, quit.");
    return;
  }

  LOG("  requesting xdg-activation, surface ID %d",
      wl_proxy_get_id((struct wl_proxy*)surface));
  xdg_activation_v1* xdg_activation = WaylandDisplayGet()->GetXdgActivation();
  if (!xdg_activation) {
    return;
  }
  xdg_activation_v1_activate(xdg_activation, aTokenID, surface);
}

// Transfer focus from gFocusWindow to aWindow and use xdg_activation
// protocol for it.
void nsWindow::TransferFocusToWaylandWindow(nsWindow* aWindow) {
  LOGW("nsWindow::TransferFocusToWaylandWindow(%p) gFocusWindow %p", aWindow,
       gFocusWindow);
  auto promise = mozilla::widget::RequestWaylandFocusPromise();
  if (NS_WARN_IF(!promise)) {
    LOGW("  quit, failed to create TransferFocusToWaylandWindow [%p]", aWindow);
    return;
  }
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      /* resolve */
      [window = RefPtr{aWindow}](nsCString token) {
        window->FocusWaylandWindow(token.get());
      },
      /* reject */
      [window = RefPtr{aWindow}](bool state) {
        LOGW("TransferFocusToWaylandWindow [%p] failed", window.get());
      });
}
#endif

// Request activation of this window or give focus to this widget.
// aRaise means whether we should request activation of this widget's
// toplevel window.
//
// nsWindow::SetFocus(Raise::Yes) - Raise and give focus to toplevel window.
// nsWindow::SetFocus(Raise::No) - Give focus to this window.
void nsWindow::SetFocus(Raise aRaise, mozilla::dom::CallerType aCallerType) {
  LOG("nsWindow::SetFocus Raise %d\n", aRaise == Raise::Yes);

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

  RefPtr<nsWindow> toplevelWindow = get_window_for_gtk_widget(toplevelWidget);
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
          const auto& startupToken = toolkit->GetStartupToken();
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
      if (GdkIsWaylandDisplay()) {
        auto existingToken =
            std::move(toplevelWindow->mWindowActivationTokenFromEnv);
        if (!existingToken.IsEmpty()) {
          LOG("  has existing activation token.");
          toplevelWindow->FocusWaylandWindow(existingToken.get());
        } else {
          LOG("  missing activation token, try to transfer from focused "
              "window");
          TransferFocusToWaylandWindow(toplevelWindow);
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

LayoutDeviceIntRect nsWindow::GetScreenBounds() { return mBounds; }

LayoutDeviceIntSize nsWindow::GetClientSize() {
  return GetClientBounds().Size();
}

LayoutDeviceIntRect nsWindow::GetClientBounds() {
  LayoutDeviceIntRect rect = GetBounds();
  rect.Deflate(mClientMargin);
  return rect;
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
  return NS_OK;
}

LayoutDeviceIntMargin nsWindow::NormalSizeModeClientToWindowMargin() {
  if (SizeMode() == nsSizeMode_Normal) {
    return mClientMargin;
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

void nsWindow::RecomputeBounds(MayChangeCsdMargin aMayChangeCsdMargin) {
  const bool mayChangeCsdMargin =
      aMayChangeCsdMargin == MayChangeCsdMargin::Yes;
  LOG("RecomputeBounds(%d)", mayChangeCsdMargin);
  mPendingBoundsChange = false;
  mPendingBoundsChangeMayChangeCsdMargin = false;

  auto* toplevel = GetToplevelGdkWindow();
  if (!toplevel || mIsDestroyed) {
    return;
  }

  auto GetFrameBounds = [&](GdkWindow* aWin) {
    GdkRectangle b{0};
    gdk_window_get_frame_extents(aWin, &b);
#ifdef MOZ_X11
    const bool isX11 = GdkIsX11Display();
    if (isX11 && !gtk_check_version(3, 24, 35) &&
        gdk_window_get_window_type(aWin) == GDK_WINDOW_TEMP) {
      // Workaround for https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/4820
      // Bug 1775017 Gtk < 3.24.35 returns scaled values for
      // override redirected window on X11.
      return LayoutDeviceIntRect(b.x, b.y, b.width, b.height);
    }
#endif
    auto result = GdkRectToDevicePixels(b);
#ifdef MOZ_X11
    if (isX11 && !gtk_check_version(3, 24, 50)) {
      if (auto border = GetXWindowBorder(aWin)) {
        // Workaround for
        // https://gitlab.gnome.org/GNOME/gtk/-/merge_requests/8423
        // Bug 1958174 Gtk doesn't account for window border sizes on X11.
        result.width += 2 * border;
        result.height += 2 * border;
      }
    }
#endif
    return result;
  };

  auto GetBounds = [&](GdkWindow* aWin) {
    GdkRectangle b{0};
    if (IsTopLevelWidget() && GdkIsX11Display() && aWin == toplevel) {
      // We want the up-to-date size from the X server, not the last configure
      // event size, to avoid spurious resizes on e.g. sizemode changes.
      gdk_window_get_geometry(aWin, nullptr, nullptr, &b.width, &b.height);
      gdk_window_get_origin(aWin, &b.x, &b.y);
      return GdkRectToDevicePixels(b);
    }
    gdk_window_get_position(aWin, &b.x, &b.y);
    b.width = gdk_window_get_width(aWin);
    b.height = gdk_window_get_height(aWin);
    return GdkRectToDevicePixels(b);
  };

  const auto oldBounds = mBounds;
  const auto oldMargin = mClientMargin;
  const auto frameBounds = GetFrameBounds(toplevel);
  const bool decorated =
      IsTopLevelWidget() && mSizeMode != nsSizeMode_Fullscreen && !mUndecorated;
  const auto toplevelBounds = GetBounds(toplevel);

  mBounds = frameBounds;

  // NOTE(emilio): This is a bit hacky. We try to do our best to get the right
  // margin at all times, but due to how the configure and size-allocate and
  // such events are sent async, we might not always have an updated mGdkWindow
  // position.
  mCsdMargin = [&] {
    if (!decorated || !ToplevelUsesCSD()) {
      return LayoutDeviceIntMargin{};
    }
    if (mayChangeCsdMargin && mGdkWindow) {
      auto gdkWindowBounds = GetBounds(mGdkWindow);
      if (gdkWindowBounds.X() >= 0 && gdkWindowBounds.Y() >= 0 &&
          gdkWindowBounds.Width() > 1 && gdkWindowBounds.Height() > 1) {
        return LayoutDeviceIntRect(LayoutDeviceIntPoint(),
                                   toplevelBounds.Size()) -
               gdkWindowBounds;
      }
    }
    // Don't change it. It might have not changed at all, or if it has, we'll
    // get a better margin once we get relevant size-allocate callbacks.
    return mCsdMargin;
  }();

  mClientMargin = [&] {
    if (!decorated) {
      return LayoutDeviceIntMargin{};
    }
    const auto systemMargin = mBounds - toplevelBounds;
    return systemMargin + mCsdMargin;
  }();
  mClientMargin.EnsureAtLeast(LayoutDeviceIntMargin());

  // Sometimes the window manager gives us garbage sizes (way past the maximum
  // texture size) causing crashes if we don't enforce size constraints again
  // here.
  auto unconstrainedBounds = mBounds;
  ConstrainSize(&mBounds.width, &mBounds.height);

  LOG("bounds: %s -> %s (%s unconstrained)", ToString(oldBounds).c_str(),
      ToString(mBounds).c_str(), ToString(unconstrainedBounds).c_str());
  LOG("margin: %s -> %s", ToString(oldMargin).c_str(),
      ToString(mClientMargin).c_str());

  if (IsPopup()) {
    // Override-redirect window.
    //
    // These windows should not be moved by the window manager, and so any
    // change in position is a result of our direction.  mBounds has already
    // been set in Move() or Resize(), and that is more up-to-date than the
    // position in the ConfigureNotify event if the event is from an earlier
    // window move.
    //
    // Skipping the WindowMoved call saves context menus from an infinite
    // loop when nsXULPopupManager::PopupMoved moves the window to the new
    // position and nsMenuPopupFrame::SetPopupPosition adds
    // offsetForContextMenu on each iteration.
    //
    // FIXME(emilio): This might not be an issue anymore... Maybe try to remove
    // this special case?
    mBounds.MoveTo(mLastMoveRequest);

    // Our back buffer might have been invalidated while we drew the last
    // frame, and its contents might be incorrect. See bug 1280653 comment 7
    // and comment 10. Specifically we must ensure we recomposite the frame
    // as soon as possible to avoid the corrupted frame being displayed.
    GetWindowRenderer()->FlushRendering(wr::RenderReasons::WIDGET);
    return;
  }

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
  const bool moved =
      clientMarginsChanged || oldBounds.TopLeft() != mBounds.TopLeft();
  const bool resized =
      clientMarginsChanged || oldBounds.Size() != mBounds.Size();

  if (moved) {
    if (IsTopLevelWidget()) {
      // The moved check avoids unwanted rollup on spurious configure events
      // from Cygwin/X (bug 672103).
      RollupAllMenus();
    }
    NotifyWindowMoved(mBounds.x, mBounds.y);
  }
  if (resized) {
    DispatchResized();
  }
}

gboolean nsWindow::OnPropertyNotifyEvent(GtkWidget* aWidget,
                                         GdkEventProperty* aEvent) {
  if (aEvent->atom == gdk_atom_intern("_NET_FRAME_EXTENTS", FALSE)) {
    LOG("OnPropertyNotifyEvent(_NET_FRAME_EXTENTS)");
    SchedulePendingBounds(MayChangeCsdMargin::Yes);
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
      return GetToplevelWidget();

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
    case NS_NATIVE_EGL_WINDOW: {
      // On X11 we call it:
      // 1) If window is mapped on OnMap() by nsWindow::ResumeCompositorImpl(),
      //    new EGLSurface/XWindow is created.
      // 2) If window is hidden on OnUnmap(), we replace EGLSurface/XWindow
      //    by offline surface and release XWindow.

      // On Wayland it:
      // 1) If window is mapped on OnMap(), we request frame callback
      //    at MozContainer. If we get frame callback at MozContainer,
      //    nsWindow::ResumeCompositorImpl() is called from it
      //    and EGLSurface/wl_surface is created.
      // 2) If window is hidden on OnUnmap(), we replace EGLSurface/wl_surface
      //    by offline surface and release XWindow.

      // If nsWindow is already destroyed, don't try to get EGL window at all,
      // we're going to be deleted anyway.
      MutexAutoLock lock(mWindowVisibilityMutex);
      void* eglWindow = nullptr;
      if (mIsMapped && !mIsDestroyed) {
#ifdef MOZ_X11
        if (GdkIsX11Display()) {
          eglWindow = (void*)GDK_WINDOW_XID(mGdkWindow);
        }
#endif
#ifdef MOZ_WAYLAND
        if (GdkIsWaylandDisplay()) {
          eglWindow = moz_container_wayland_get_egl_window(mContainer);
        }
#endif
      }
      LOG("Get NS_NATIVE_EGL_WINDOW mGdkWindow %p returned eglWindow %p",
          mGdkWindow, eglWindow);
      return eglWindow;
    }
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

  nsCOMPtr<nsIFile> iconFile;
  nsAutoCString path;

  gint* iconSizes = gtk_icon_theme_get_icon_sizes(gtk_icon_theme_get_default(),
                                                  iconName.get());
  bool foundIcon = (iconSizes[0] != 0);
  g_free(iconSizes);

  if (!foundIcon) {
    // Look for icons with the following suffixes appended to the base name
    // The last two entries (for the old XPM format) will be ignored unless
    // no icons are found using other suffixes. XPM icons are deprecated.

    const char16_t extensions[9][8] = {u".png",    u"16.png", u"32.png",
                                       u"48.png",  u"64.png", u"128.png",
                                       u"256.png", u".xpm",   u"16.xpm"};

    for (uint32_t i = 0; i < std::size(extensions); i++) {
      // Don't bother looking for XPM versions if we found a PNG.
      if (i == std::size(extensions) - 2 && foundIcon) break;

      ResolveIconName(aIconSpec, nsDependentString(extensions[i]),
                      getter_AddRefs(iconFile));
      if (iconFile) {
        iconFile->GetNativePath(path);
        GdkPixbuf* icon = gdk_pixbuf_new_from_file(path.get(), nullptr);
        if (icon) {
          gtk_icon_theme_add_builtin_icon(iconName.get(),
                                          gdk_pixbuf_get_height(icon), icon);
          g_object_unref(icon);
          foundIcon = true;
        }
      }
    }
  }

  // leave the default icon intact if no matching icons were found
  if (foundIcon) {
    gtk_window_set_icon_name(GTK_WINDOW(mShell), iconName.get());
  }
}

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  return mBounds.TopLeft() + GetClientOffset();
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
    Unused << NS_WARN_IF(status != GDK_GRAB_SUCCESS);
    LOG(" > pointer grab with status %d", int(status));
    gtk_grab_add(GTK_WIDGET(mContainer));
  } else {
    // There may not have been a drag in process when aDoCapture was set,
    // so make sure to remove any added grab.  This is a no-op if the grab
    // was not added to this widget.
    gtk_grab_remove(GTK_WIDGET(mContainer));
    gdk_pointer_ungrab(GetLastUserInputTime());
  }
}

nsresult nsWindow::GetAttention(int32_t aCycleCount) {
  LOG("nsWindow::GetAttention");

  GtkWidget* top_window = GetToplevelWidget();
  GtkWidget* top_focused_window =
      gFocusWindow ? gFocusWindow->GetToplevelWidget() : nullptr;

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
static bool ExtractExposeRegion(LayoutDeviceIntRegion& aRegion, cairo_t* cr) {
  cairo_rectangle_list_t* rects = cairo_copy_clip_rectangle_list(cr);
  if (rects->status != CAIRO_STATUS_SUCCESS) {
    NS_WARNING("Failed to obtain cairo rectangle list.");
    return false;
  }

  for (int i = 0; i < rects->num_rectangles; i++) {
    const cairo_rectangle_t& r = rects->rectangles[i];
    aRegion.Or(aRegion,
               LayoutDeviceIntRect::Truncate((float)r.x, (float)r.y,
                                             (float)r.width, (float)r.height));
  }

  cairo_rectangle_list_destroy(rects);
  return true;
}

#ifdef MOZ_WAYLAND
void nsWindow::CreateCompositorVsyncDispatcher() {
  LOG_VSYNC("nsWindow::CreateCompositorVsyncDispatcher()");
  if (!mWaylandVsyncSource) {
    LOG_VSYNC(
        "  mWaylandVsyncSource is missing, create "
        "nsBaseWidget::CompositorVsyncDispatcher()");
    nsBaseWidget::CreateCompositorVsyncDispatcher();
    return;
  }
  if (!mCompositorVsyncDispatcherLock) {
    mCompositorVsyncDispatcherLock =
        MakeUnique<Mutex>("mCompositorVsyncDispatcherLock");
  }
  MutexAutoLock lock(*mCompositorVsyncDispatcherLock);
  if (!mCompositorVsyncDispatcher) {
    LOG_VSYNC("  create CompositorVsyncDispatcher()");
    mCompositorVsyncDispatcher =
        new CompositorVsyncDispatcher(mWaylandVsyncDispatcher);
  }
}
#endif

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
  if (!mGdkWindow || !mHasMappedToplevel) {
    LOG("quit, !mGdkWindow || !mHasMappedToplevel");
    return FALSE;
  }
#ifdef MOZ_WAYLAND
  if (!mIsDragPopup && GdkIsWaylandDisplay() &&
      !moz_container_wayland_can_draw(mContainer)) {
    LOG("quit, !moz_container_wayland_can_draw()");
    return FALSE;
  }
#endif

  if (!GetListener()) {
    LOG("quit, !GetListener()");
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

  // Dispatch WillPaintWindow notification to allow scripts etc. to run
  // before we paint. It also spins event loop which may show/hide the window
  // so we may have new renderer etc.
  GetListener()->WillPaintWindow(this);

  // If the window has been destroyed during the will paint notification,
  // there is nothing left to do.
  if (!mGdkWindow || mIsDestroyed) {
    LOG("quit, !mGdkWindow || mIsDestroyed");
    return TRUE;
  }

  // Re-get all rendering components since the will paint notification
  // might have killed it.
  nsIWidgetListener* listener = GetListener();
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

  // Our bounds may have changed after calling WillPaintWindow.  Clip
  // to the new bounds here.  The region is relative to this
  // window.
  region.And(region,
             LayoutDeviceIntRect(LayoutDeviceIntPoint(), GetClientSize()));
  if (region.IsEmpty()) {
    LOG("quit, region.IsEmpty()");
    return TRUE;
  }

  // If this widget uses OMTC...
  if (renderer->GetBackendType() == LayersBackend::LAYERS_WR) {
    LOG("redirect painting to OMTC rendering...");
    listener->PaintWindow(this, region);

    // Re-get the listener since the will paint notification might have
    // killed it.
    listener = GetListener();
    if (!listener) {
      return TRUE;
    }

    listener->DidPaintWindow();
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

  {
    if (renderer->GetBackendType() == LayersBackend::LAYERS_NONE) {
      if (GetTransparencyMode() == TransparencyMode::Transparent &&
          mHasAlphaVisual) {
        // If our draw target is unbuffered and we use an alpha channel,
        // clear the image beforehand to ensure we don't get artifacts from a
        // reused SHM image. See bug 1258086.
        dt->ClearRect(Rect(boundsRect));
      }
      AutoLayerManagerSetup setupLayerManager(this, ctx.ptrOr(nullptr));
      listener->PaintWindow(this, region);

      // Re-get the listener since the will paint notification might have
      // killed it.
      listener = GetListener();
      if (!listener) {
        return TRUE;
      }
    }
  }

#ifdef MOZ_X11
  ctx.reset();
  dt->PopClip();
#endif  // MOZ_X11

  EndRemoteDrawingInRegion(dt, region);

  listener->DidPaintWindow();

  // Synchronously flush any new dirty areas
  cairo_region_t* dirtyArea = gdk_window_get_update_area(mGdkWindow);

  if (dirtyArea) {
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
  int scale = mGdkWindow ? gdk_window_get_scale_factor(mGdkWindow) : -1;
  LOG("configure event %d,%d -> %d x %d direct mGdkWindow scale %d "
      "(scaled size %d x %d)\n",
      aEvent->x, aEvent->y, aEvent->width, aEvent->height, scale,
      aEvent->width * scale, aEvent->height * scale);
#endif

  if (mPendingConfigures > 0) {
    mPendingConfigures--;
  }

  // Don't fire configure event for scale changes, we handle that
  // OnScaleEvent event. Skip that for toplevel windows only.
  if (mGdkWindow && IsTopLevelWidget() &&
      mCeiledScaleFactor != gdk_window_get_scale_factor(mGdkWindow)) {
    LOG("  scale factor changed to %d,return early",
        gdk_window_get_scale_factor(mGdkWindow));
    return FALSE;
  }

  // FIXME(emilio): This sync bounds computation should ideally not be needed
  // but it causes timeouts on automation. See bug 1946184 comment 14.
  RecomputeBounds(MayChangeCsdMargin::No);
  return FALSE;
}

void nsWindow::OnContainerSizeAllocate(GtkAllocation* aAllocation) {
  LOG("nsWindow::OnContainerSizeAllocate %d,%d -> %d x %d\n", aAllocation->x,
      aAllocation->y, aAllocation->width, aAllocation->height);
  mHasReceivedSizeAllocate = true;
  if (!mGdkWindow) {
    return;
  }

  auto oldClientBounds = GetClientBounds();

  // Bounds will get updated on the main configure.
  // Gecko permits running nested event loops during processing of events,
  // GtkWindow callers of gtk_widget_size_allocate expect the signal handlers
  // to return sometime in the near future.
  // Also, this runs for both top level size_allocate and MozContainer size
  // allocate, so even if the client bounds are the same, we need to recompute
  // the bounds because the client margin might not.
  SchedulePendingBounds(MayChangeCsdMargin::Yes);

  // Invalidate the new part of the window now for the pending paint to
  // minimize background flashes (GDK does not do this for external
  // renewClientSizes of toplevels.)
  LayoutDeviceIntRect newClientBounds = GdkRectToDevicePixels(*aAllocation);
  if (oldClientBounds.Size() == newClientBounds.Size()) {
    return;
  }

  if (oldClientBounds.width < newClientBounds.width) {
    GdkRectangle rect = DevicePixelsToGdkRectRoundOut(LayoutDeviceIntRect(
        oldClientBounds.width, 0, newClientBounds.width - oldClientBounds.width,
        newClientBounds.height));
    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
  }
  if (oldClientBounds.height < newClientBounds.height) {
    GdkRectangle rect = DevicePixelsToGdkRectRoundOut(
        LayoutDeviceIntRect(0, oldClientBounds.height, newClientBounds.width,
                            newClientBounds.height - oldClientBounds.height));
    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
  }
}

void nsWindow::SchedulePendingBounds(MayChangeCsdMargin aMayChangeCsdMargin) {
  mPendingBoundsChangeMayChangeCsdMargin |=
      aMayChangeCsdMargin == MayChangeCsdMargin::Yes;
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
    RecomputeBounds(MayChangeCsdMargin(mPendingBoundsChangeMayChangeCsdMargin));
  }
}

void nsWindow::OnDeleteEvent() {
  if (mWidgetListener) {
    mWidgetListener->RequestWindowClose(this);
  }
}

void nsWindow::OnEnterNotifyEvent(GdkEventCrossing* aEvent) {
  LOG("enter notify (win=%p, sub=%p): %f, %f mode %d, detail %d\n",
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
  LOG("leave notify (win=%p, sub=%p): %f, %f mode %d, detail %d\n",
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

  LOG("OnLeaveNotify");

  DispatchInputEvent(&event);
}

Maybe<GdkWindowEdge> nsWindow::CheckResizerEdge(
    const LayoutDeviceIntPoint& aPoint) {
  // Don't allow resizing maximized/fullscreen windows, nor add extra resizing
  // margins on non PiP windows.
  if (mSizeMode != nsSizeMode_Normal || !mIsPIPWindow) {
    return Nothing();
  }

  // If we're not in a PiP window, allow 1px resizer edge from the top edge,
  // and nothing else.
  // This is to allow resizes of tiled windows on KDE, see bug 1813554.
  const int resizerHeight = 15 * GdkCeiledScaleFactor();
  const int resizerWidth = resizerHeight * 4;

  const int topDist = aPoint.y;
  const int leftDist = aPoint.x;
  const int rightDist = mBounds.width - aPoint.x;
  const int bottomDist = mBounds.height - aPoint.y;

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
  if (aEvent->window == aWindow->GetGdkWindow()) {
    // we are the window that the event happened on so no need for expensive
    // WidgetToScreenOffset
    return aWindow->GdkEventCoordsToDevicePixels(aEvent->x, aEvent->y);
  }
  // XXX we're never quite sure which GdkWindow the event came from due to our
  // custom bubbling in scroll_event_cb(), so use ScreenToWidget to translate
  // the screen root coordinates into coordinates relative to the inner widget.
  return aWindow->GdkEventCoordsToDevicePixels(aEvent->x_root, aEvent->y_root) -
         aWindow->WidgetToScreenOffset();
}

void nsWindow::EmulateResizeDrag(GdkEventMotion* aEvent) {
  auto newPoint = LayoutDeviceIntPoint::Floor(aEvent->x, aEvent->y);
  LayoutDeviceIntPoint diff = newPoint - mLastResizePoint;
  mLastResizePoint = newPoint;

  GdkRectangle size = DevicePixelsToGdkSizeRoundUp(mBounds.Size());
  LayoutDeviceIntSize newSize(size.width + diff.x, size.height + diff.y);

  if (mAspectResizer.value() == GTK_ORIENTATION_VERTICAL) {
    newSize.width = int(newSize.height * mAspectRatio);
  } else {  // GTK_ORIENTATION_HORIZONTAL
    newSize.height = int(newSize.width / mAspectRatio);
  }
  LOG("  aspect ratio correction %d x %d aspect %f\n", newSize.width,
      newSize.height, mAspectRatio);
  gtk_window_resize(GTK_WINDOW(mShell), newSize.width, newSize.height);
}

void nsWindow::OnMotionNotifyEvent(GdkEventMotion* aEvent) {
  mLastMouseCoordinates.Set(aEvent);

  if (!mGdkWindow) {
    return;
  }

  // Emulate gdk_window_begin_resize_drag() for windows
  // with fixed aspect ratio on Wayland.
  if (mAspectResizer && mAspectRatio != 0.0f) {
    EmulateResizeDrag(aEvent);
    return;
  }

  if (mWindowShouldStartDragging) {
    mWindowShouldStartDragging = false;
    GdkWindow* dragWindow = nullptr;

    // find the top-level window
    if (mGdkWindow) {
      dragWindow = gdk_window_get_toplevel(mGdkWindow);
      MOZ_ASSERT(dragWindow, "gdk_window_get_toplevel should not return null");
    }

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
    if (mIsPIPWindow) {
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

  KeymapWrapper::InitInputEvent(event, aEvent->state);
  InitPenEvent(event, (GdkEvent*)aEvent);

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
                               const LayoutDeviceIntPoint& aRefPoint) {
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

  KeymapWrapper::InitInputEvent(aEvent, modifierState);

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

  SetLastMousePressEvent((GdkEvent*)aEvent);
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
      mLastResizePoint = LayoutDeviceIntPoint::Floor(aEvent->x, aEvent->y);
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
    if (!mAspectResizer) {
      gdk_window_begin_resize_drag(GetToplevelGdkWindow(), *edge,
                                   aEvent->button, aEvent->x_root,
                                   aEvent->y_root, aEvent->time);
    }
    return;
  }

  gdouble pressure = 0;
  gdk_event_get_axis((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
  mLastMotionPressure = pressure;

  uint16_t domButton;
  switch (aEvent->button) {
    case 1:
      domButton = MouseButton::ePrimary;
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
  InitButtonEvent(event, aEvent, refPoint);
  event.mPressure = mLastMotionPressure;

  InitPenEvent(event, (GdkEvent*)aEvent);
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

  SetLastMousePressEvent(nullptr);
  mLastMouseCoordinates.Set(aEvent);

  if (!mGdkWindow) {
    return;
  }

  if (mAspectResizer) {
    mAspectResizer = Nothing();
    return;
  }

  if (mWindowShouldStartDragging) {
    mWindowShouldStartDragging = false;
  }

  uint16_t domButton;
  switch (aEvent->button) {
    case 1:
      domButton = MouseButton::ePrimary;
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
  InitButtonEvent(event, aEvent, refPoint);
  gdouble pressure = 0;
  gdk_event_get_axis((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
  event.mPressure = pressure ? (float)pressure : (float)mLastMotionPressure;

  // The mRefPoint is manipulated in DispatchInputEvent, we're saving it
  // to use it for the doubleclick position check.
  const LayoutDeviceIntPoint pos = event.mRefPoint;

  InitPenEvent(event, (GdkEvent*)aEvent);

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

  // Open window manager menu on PIP window to allow user
  // to place it on top / all workspaces.
  if (mAlwaysOnTop && aEvent->button == 3) {
    TryToShowNativeWindowMenu(aEvent);
  }
}

void nsWindow::OnContainerFocusInEvent(GdkEventFocus* aEvent) {
  LOG("OnContainerFocusInEvent");

  // Unset the urgency hint, if possible
  GtkWidget* top_window = GetToplevelWidget();
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
  nsEventStatus status;
  WidgetCommandEvent appCommandEvent(true, aCommand, this);
  DispatchEvent(&appCommandEvent, status);
  return TRUE;
}

bool nsWindow::DispatchContentCommandEvent(EventMessage aMsg) {
  nsEventStatus status;
  WidgetContentCommandEvent event(true, aMsg, this);
  DispatchEvent(&event, status);
  return TRUE;
}

WidgetEventTime nsWindow::GetWidgetEventTime(guint32 aEventTime) {
  return WidgetEventTime(GetEventTimeStamp(aEventTime));
}

TimeStamp nsWindow::GetEventTimeStamp(guint32 aEventTime) {
  if (MOZ_UNLIKELY(!mGdkWindow)) {
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
  MOZ_ASSERT(mGdkWindow, "Expected mGdkWindow to be set");
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
              (gboolean(*)(const GdkEvent*))dlsym(
                  RTLD_DEFAULT, "gdk_event_is_scroll_stop_event");

          LOG("[%d] pan smooth event dx=%f dy=%f inprogress=%d\n", aEvent->time,
              aEvent->delta_x, aEvent->delta_y, mPanInProgress);
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

          if (mCurrentSynthesizedTouchpadPan.mSavedObserver != 0) {
            mozilla::widget::AutoObserverNotifier::NotifySavedObserver(
                mCurrentSynthesizedTouchpadPan.mSavedObserver,
                "touchpadpanevent");
            mCurrentSynthesizedTouchpadPan.mSavedObserver = 0;
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
  LOG("OnSmoothScrollEvent time %d dX %f dY %f", aTime, aDeltaX, aDeltaY);

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

  // We don't care about anything but changes in the maximized/icon/fullscreen
  // states but we need a workaround for bug in Wayland:
  // https://gitlab.gnome.org/GNOME/gtk/issues/67
  // Under wayland the gtk_window_iconify implementation does NOT synthetize
  // window_state_event where the GDK_WINDOW_STATE_ICONIFIED is set.
  // During restore we  won't get aEvent->changed_mask with
  // the GDK_WINDOW_STATE_ICONIFIED so to detect that change we use the stored
  // mSizeMode and obtaining a focus.
  bool waylandWasIconified =
      (GdkIsWaylandDisplay() &&
       aEvent->changed_mask & GDK_WINDOW_STATE_FOCUSED &&
       aEvent->new_window_state & GDK_WINDOW_STATE_FOCUSED &&
       mSizeMode == nsSizeMode_Minimized);
  if (!waylandWasIconified &&
      (aEvent->changed_mask &
       (GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_MAXIMIZED | kTiledStates |
        kResizableStates | GDK_WINDOW_STATE_FULLSCREEN)) == 0) {
    LOG("\tearly return because no interesting bits changed\n");
    return;
  }

  auto oldSizeMode = mSizeMode;
  auto oldIsTiled = mIsTiled;
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

  mIsTiled = aEvent->new_window_state & GDK_WINDOW_STATE_TILED;
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

  if (mSizeMode != oldSizeMode || mIsTiled != oldIsTiled) {
    RecomputeBounds(MayChangeCsdMargin::No);
  }
  if (mSizeMode != oldSizeMode && mWidgetListener) {
    mWidgetListener->SizeModeChanged(mSizeMode);
  }
}

void nsWindow::OnDPIChanged() {
  // Update menu's font size etc.
  // This affects style / layout because it affects system font sizes.
  if (mWidgetListener) {
    if (PresShell* presShell = mWidgetListener->GetPresShell()) {
      presShell->BackingScaleFactorChanged();
    }
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
  if (!mGdkWindow || !IsTopLevelWidget()) {
    return;
  }

  LOG("nsWindow::OnScaleEvent() GdkWindow scale %d",
      gdk_window_get_scale_factor(mGdkWindow));

  RefreshScale(/* aRefreshScreen */ true);
}

void nsWindow::RefreshScale(bool aRefreshScreen) {
  if (!IsTopLevelWidget()) {
    return;
  }

  LOG("nsWindow::RefreshScale() GdkWindow scale %d refresh %d",
      gdk_window_get_scale_factor(mGdkWindow), aRefreshScreen);

#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    WaylandSurfaceLock lock(mSurface);
    mSurface->SetCeiledScaleLocked(lock,
                                   gdk_window_get_scale_factor(mGdkWindow));
  }
#endif

  MOZ_DIAGNOSTIC_ASSERT(mIsMapped && mGdkWindow);
  int ceiledScale = gdk_window_get_scale_factor(mGdkWindow);
  bool scaleChanged = (mCeiledScaleFactor != ceiledScale);
  if (!scaleChanged) {
    return;
  }
  mCeiledScaleFactor = ceiledScale;

  NotifyAPZOfDPIChange();

  if (!aRefreshScreen) {
    return;
  }

  RecomputeBounds(MayChangeCsdMargin::Yes);

  if (mWidgetListener) {
    if (PresShell* presShell = mWidgetListener->GetPresShell()) {
      presShell->BackingScaleFactorChanged();
    }
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
  if (mGdkWindow) {
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

void nsWindow::OnDragDataReceivedEvent(GtkWidget* aWidget,
                                       GdkDragContext* aDragContext, gint aX,
                                       gint aY,
                                       GtkSelectionData* aSelectionData,
                                       guint aInfo, guint aTime,
                                       gpointer aData) {
  LOGDRAG("nsWindow::OnDragDataReceived");

  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  nsDragSession* dragSession =
      static_cast<nsDragSession*>(dragService->GetCurrentSession(this));
  if (dragSession) {
    nsDragSession::AutoEventLoop loop(dragSession);
    dragSession->TargetDataReceived(aWidget, aDragContext, aX, aY,
                                    aSelectionData, aInfo, aTime);
  }
}

nsWindow* nsWindow::GetTransientForWindowIfPopup() {
  if (mWindowType != WindowType::Popup) {
    return nullptr;
  }
  GtkWindow* toplevel = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (toplevel) {
    return get_window_for_gtk_widget(GTK_WIDGET(toplevel));
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
  LOG("OnTouchEvent: x=%f y=%f type=%d\n", aEvent->x, aEvent->y, aEvent->type);
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
      // check to see if we should rollup
      if (CheckForRollup(aEvent->x_root, aEvent->y_root, false, false)) {
        return FALSE;
      }
      msg = eTouchStart;
      break;
    case GDK_TOUCH_UPDATE:
      msg = eTouchMove;
      // Start dragging when motion events happens in the dragging area
      if (mWindowShouldStartDragging) {
        mWindowShouldStartDragging = false;
        if (mGdkWindow) {
          GdkWindow* gdk_window = gdk_window_get_toplevel(mGdkWindow);
          MOZ_ASSERT(gdk_window,
                     "gdk_window_get_toplevel should not return null");

          LOG("  start window dragging window\n");
          gdk_window_begin_move_drag(gdk_window, 1, aEvent->x_root,
                                     aEvent->y_root, aEvent->time);

          // Cancel the event sequence. gdk will steal all subsequent events
          // (including TOUCH_END).
          msg = eTouchCancel;
        }
      }
      break;
    case GDK_TOUCH_END:
      msg = eTouchEnd;
      if (mWindowShouldStartDragging) {
        LOG("  end of window dragging window\n");
        mWindowShouldStartDragging = false;
      }
      break;
    case GDK_TOUCH_CANCEL:
      msg = eTouchCancel;
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

#ifdef MOZ_X11
// Configure GL visual on X11.
bool nsWindow::ConfigureX11GLVisual() {
  auto* screen = gtk_widget_get_screen(mShell);
  int visualId = 0;
  bool haveVisual = false;

  if (gfxVars::UseEGL()) {
    haveVisual = GLContextEGL::FindVisual(&visualId);
  }

  // We are on GLX or use it as a fallback on Mesa, see
  // https://gitlab.freedesktop.org/mesa/mesa/-/issues/149
  if (!haveVisual) {
    auto* display = GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(mShell));
    int screenNumber = GDK_SCREEN_XNUMBER(screen);
    haveVisual = GLContextGLX::FindVisual(display, screenNumber, &visualId);
  }

  GdkVisual* gdkVisual = nullptr;
  if (haveVisual) {
    // If we're using CSD, rendering will go through mContainer, but
    // it will inherit this visual as it is a child of mShell.
    gdkVisual = gdk_x11_screen_lookup_visual(screen, visualId);
  }
  if (!gdkVisual) {
    NS_WARNING("We're missing X11 Visual!");
    // We try to use a fallback alpha visual
    GdkScreen* screen = gtk_widget_get_screen(mShell);
    gdkVisual = gdk_screen_get_rgba_visual(screen);
  }
  if (gdkVisual) {
    gtk_widget_set_visual(mShell, gdkVisual);
    mHasAlphaVisual = true;
    return true;
  }

  return false;
}
#endif

nsAutoCString nsWindow::GetFrameTag() const {
  if (nsIFrame* frame = GetFrame()) {
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
    return mGdkWindow ? gdk_x11_window_get_xid(mGdkWindow) : X11None;
  }
#endif
  return (Window) nullptr;
}

void nsWindow::EnsureGdkWindow() {
  MOZ_DIAGNOSTIC_ASSERT(mIsMapped);
  if (!mGdkWindow) {
    mGdkWindow = gtk_widget_get_window(GTK_WIDGET(mContainer));
    g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", this);
  }
}

void nsWindow::ConfigureCompositor() {
  MOZ_DIAGNOSTIC_ASSERT(mIsMapped);

  LOG("nsWindow::ConfigureCompositor()");
  auto startCompositing = [self = RefPtr{this}, this]() -> void {
    LOG("  moz_container_wayland_add_or_fire_initial_draw_callback "
        "ConfigureCompositor");

    // too late
    if (mIsDestroyed || !mIsMapped) {
      LOG("  quit, mIsDestroyed = %d mIsMapped = %d", !!mIsDestroyed,
          !!mIsMapped);
      return;
    }
    // Compositor will be resumed at nsWindow::SetCompositorWidgetDelegate().
    if (!mCompositorWidgetDelegate) {
      LOG("  quit, missing mCompositorWidgetDelegate");
      return;
    }

    ResumeCompositorImpl();
  };

  if (GdkIsWaylandDisplay()) {
#ifdef MOZ_WAYLAND
    moz_container_wayland_add_or_fire_initial_draw_callback(mContainer,
                                                            startCompositing);
#endif
  } else {
    startCompositing();
  }
}

nsresult nsWindow::Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                          widget::InitData* aInitData) {
  MOZ_DIAGNOSTIC_ASSERT(!aInitData ||
                        aInitData->mWindowType != WindowType::Invisible);

#ifdef ACCESSIBILITY
  // Send a DBus message to check whether a11y is enabled
  a11y::PreInit();
#endif

#ifdef MOZ_WAYLAND
  // Ensure that KeymapWrapper is created on Wayland as we need it for
  // keyboard focus tracking.
  if (GdkIsWaylandDisplay()) {
    KeymapWrapper::EnsureInstance();
  }
#endif

  // Ensure that the toolkit is created.
  nsGTKToolkit::GetToolkit();

  // initialize all the common bits of this class
  BaseCreate(aParent, aInitData);

  LOG("nsWindow::Create()");

  // save our bounds
  mBounds = aRect;
  LOG("  mBounds: x:%d y:%d w:%d h:%d\n", mBounds.x, mBounds.y, mBounds.width,
      mBounds.height);

  ConstrainSize(&mBounds.width, &mBounds.height);
  mLastSizeRequest = mBounds.Size();
  mLastMoveRequest = mBounds.TopLeft();

  const bool popupNeedsAlphaVisual =
      mWindowType == WindowType::Popup && aInitData &&
      aInitData->mTransparencyMode == TransparencyMode::Transparent;

  // Figure out our parent window.
  auto* parentnsWindow = static_cast<nsWindow*>(aParent);
  LOG("  parent window [%p]", parentnsWindow);

  MOZ_DIAGNOSTIC_ASSERT(mWindowType != WindowType::Child);

  MOZ_ASSERT_IF(mWindowType == WindowType::Popup, parentnsWindow);

  if (mWindowType != WindowType::Dialog && mWindowType != WindowType::Popup &&
      mWindowType != WindowType::TopLevel) {
    MOZ_ASSERT_UNREACHABLE("Unexpected eWindowType");
    return NS_ERROR_FAILURE;
  }

  mAlwaysOnTop = aInitData && aInitData->mAlwaysOnTop;
  // mNoAutoHide seems to be always false here.
  // The mNoAutoHide state is set later on nsMenuPopupFrame level
  // and can be changed so we use WaylandPopupIsPermanent() to get
  // recent popup config (Bug 1728952).
  mNoAutoHide = aInitData && aInitData->mNoAutoHide;
  mIsAlert = aInitData && aInitData->mIsAlert;
  mIsDragPopup = aInitData && aInitData->mIsDragPopup;

  // Popups that are not noautohide are only temporary. The are used
  // for menus and the like and disappear when another window is used.
  // For most popups, use the standard GtkWindowType GTK_WINDOW_POPUP,
  // which will use a Window with the override-redirect attribute
  // (for temporary windows).
  // For long-lived windows, their stacking order is managed by the
  // window manager, as indicated by GTK_WINDOW_TOPLEVEL.
  // For Wayland we have to always use GTK_WINDOW_POPUP to control
  // popup window position.
  GtkWindowType type = GTK_WINDOW_TOPLEVEL;
  if (mWindowType == WindowType::Popup) {
    MOZ_ASSERT(aInitData);
    type = GTK_WINDOW_POPUP;
    if (GdkIsX11Display() && mNoAutoHide) {
      type = GTK_WINDOW_TOPLEVEL;
    }
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
  Unused << gfxPlatform::GetPlatform();

  if (IsTopLevelWidget()) {
    mGtkWindowDecoration = GetSystemGtkWindowDecoration();
  }

  // Don't use transparency for PictureInPicture windows.
  bool toplevelNeedsAlphaVisual = false;
  if (mWindowType == WindowType::TopLevel && !mIsPIPWindow) {
    toplevelNeedsAlphaVisual = IsToplevelWindowTransparent();
  }

  bool isGLVisualSet = false;
  mIsAccelerated = ComputeShouldAccelerate();
#ifdef MOZ_X11
  if (GdkIsX11Display() && mIsAccelerated) {
    isGLVisualSet = ConfigureX11GLVisual();
  }
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
    GdkRectangle size = DevicePixelsToGdkSizeRoundUp(mBounds.Size());
    LOG("  nsWindow::Create() Initial resize to %d x %d\n", size.width,
        size.height);
    gtk_window_resize(GTK_WINDOW(mShell), size.width, size.height);
  }
  if (mIsPIPWindow) {
    LOG("  Is PIP window\n");
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
    MOZ_ASSERT(aInitData);
    mGtkWindowRoleName = "Popup";

    LOG("  nsWindow::Create() Popup");

    if (mNoAutoHide) {
      // ... but the window manager does not decorate this window,
      // nor provide a separate taskbar icon.
      if (mBorderStyle == BorderStyle::Default) {
        gtk_window_set_decorated(GTK_WINDOW(mShell), FALSE);
      } else {
        bool decorate = bool(mBorderStyle & BorderStyle::Title);
        gtk_window_set_decorated(GTK_WINDOW(mShell), decorate);
        if (decorate) {
          gtk_window_set_deletable(GTK_WINDOW(mShell),
                                   bool(mBorderStyle & BorderStyle::Close));
        }
      }
      gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mShell), TRUE);
      // Element focus is managed by the parent window so the
      // WM_HINTS input field is set to False to tell the window
      // manager not to set input focus to this window ...
      gtk_window_set_accept_focus(GTK_WINDOW(mShell), FALSE);
#ifdef MOZ_X11
      // ... but when the window manager offers focus through
      // WM_TAKE_FOCUS, focus is requested on the parent window.
      if (GdkIsX11Display()) {
        gtk_widget_realize(mShell);
        gdk_window_add_filter(GetToplevelGdkWindow(), popup_take_focus_filter,
                              nullptr);
      }
#endif
    }

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
    } else if (AreBoundsSane()) {
      GdkRectangle rect = DevicePixelsToGdkRectRoundOut(mBounds);
      mPopupPosition = {rect.x, rect.y};
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
    mSurface = new WaylandSurface(
        parentnsWindow ? MOZ_WL_SURFACE(parentnsWindow->GetMozContainer())
                       : nullptr,
        gfx::IntSize(mLastSizeRequest.width, mLastSizeRequest.height));
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
  if (mAlwaysOnTop) {
    gtk_window_set_focus_on_map(GTK_WINDOW(mShell), FALSE);
  }

  gtk_widget_realize(container);

  // make sure this is the focus widget in the container
  gtk_widget_show(container);

  if (!mAlwaysOnTop) {
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
    MOZ_ASSERT(aInitData);
    // gdk does not automatically set the cursor for "temporary"
    // windows, which are what gtk uses for popups.

    // force SetCursor to actually set the cursor, even though our internal
    // state indicates that we already have the standard cursor.
    mUpdateCursor = true;
    SetCursor(Cursor{eCursor_standard});
  }

  // Also label mShell toplevel window,
  // property_notify_event_cb callback also needs to find its way home
  g_object_set_data(G_OBJECT(GetToplevelGdkWindow()), "nsWindow", this);
  g_object_set_data(G_OBJECT(mShell), "nsWindow", this);

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

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    gtk_widget_set_double_buffered(GTK_WIDGET(mContainer), FALSE);
  }
#endif
#ifdef MOZ_WAYLAND
  // Initialize the window specific VsyncSource early in order to avoid races
  // with BrowserParent::UpdateVsyncParentVsyncDispatcher().
  // Only use for toplevel windows for now, see bug 1619246.
  if (GdkIsWaylandDisplay() &&
      StaticPrefs::widget_wayland_vsync_enabled_AtStartup() &&
      IsTopLevelWidget()) {
    LOG_VSYNC("  create WaylandVsyncSource");
    mWaylandVsyncSource = new WaylandVsyncSource(this);
    mWaylandVsyncSource->Init();
    mWaylandVsyncDispatcher = new VsyncDispatcher(mWaylandVsyncSource);
  }
#endif

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
      mIsPIPWindow ? "PIP window" : "");
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
  const LayoutDeviceIntRect frameRect(mLastMoveRequest, mLastSizeRequest);
  GdkRectangle moveResizeRect = [&] {
    auto cr = frameRect;
    cr.Deflate(mClientMargin);
    // gtk_window_move will undo the csd offset properly on its own, but
    // nothing else, so do it manually if not drawing to the CSD titlebar.
    if (!ToplevelUsesCSD()) {
      cr -= GetClientOffset();
    }
    return DevicePixelsToGdkRectRoundOut(cr);
  }();

  LOG("nsWindow::NativeMoveResize move %d resize %d to %d,%d -> %d x %d\n",
      aMoved, aResized, moveResizeRect.x, moveResizeRect.y,
      moveResizeRect.width, moveResizeRect.height);

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
    LOG("  store position of hidden popup window");
    mHiddenPopupPositioned = true;
    mPopupPosition = {moveResizeRect.x, moveResizeRect.y};
    mBounds.MoveTo(mLastMoveRequest);
  }

  if (IsWaylandPopup()) {
    NativeMoveResizeWaylandPopup(aMoved, aResized);
  } else {
    // x and y give the position of the window manager frame top-left.
    if (aMoved) {
      gtk_window_move(GTK_WINDOW(mShell), moveResizeRect.x, moveResizeRect.y);
    }
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
  }

  if (aResized) {
    // Recompute the input region, in case the window grew or shrunk.
    SetInputRegion(mInputRegion);
  }

  // Does it need to be shown because bounds were previously insane?
  if (mNeedsShow && mIsShown && aResized) {
    NativeShow(true);
  }
}

void nsWindow::ResumeCompositorImpl() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  LOG("nsWindow::ResumeCompositorImpl()\n");

  MOZ_DIAGNOSTIC_ASSERT(mCompositorWidgetDelegate);
  mCompositorWidgetDelegate->SetRenderingSurface(GetX11Window());

  // As WaylandStartVsync needs mCompositorWidgetDelegate this is the right
  // time to start it.
  WaylandStartVsync();

  CompositorBridgeChild* remoteRenderer = GetRemoteRenderer();
  MOZ_RELEASE_ASSERT(remoteRenderer);
  remoteRenderer->SendResume();
  remoteRenderer->SendForcePresent(wr::RenderReasons::WIDGET);
}

void nsWindow::WaylandStartVsync() {
#ifdef MOZ_WAYLAND
  if (!mWaylandVsyncSource) {
    return;
  }
  LOG_VSYNC("nsWindow::WaylandStartVsync");
  mWaylandVsyncSource->EnableVSyncSource();
#endif
}

void nsWindow::WaylandStopVsync() {
#ifdef MOZ_WAYLAND
  if (!mWaylandVsyncSource) {
    return;
  }
  LOG_VSYNC("nsWindow::WaylandStopVsync");
  mWaylandVsyncSource->DisableVSyncSource();
#endif
}

void nsWindow::NativeShow(bool aAction) {
  if (aAction) {
    // unset our flag now that our window has been shown
    mNeedsShow = true;
    auto removeShow = MakeScopeExit([&] { mNeedsShow = false; });

    LOG("nsWindow::NativeShow show\n");

    if (IsWaylandPopup()) {
      mPopupClosed = false;
      if (WaylandPopupConfigure()) {
        AddWindowToPopupHierarchy();
        UpdateWaylandPopupHierarchy();
        if (mPopupClosed) {
          return;
        }
      }
    }
    // Set up usertime/startupID metadata for the created window.
    // On X11 we use gtk_window_set_startup_id() so we need to call it
    // before show.
    if (GdkIsX11Display()) {
      SetUserTimeAndStartupTokenForActivatedWindow();
    }
    if (GdkIsWaylandDisplay()) {
      if (IsWaylandPopup()) {
        ShowWaylandPopupWindow();
      } else {
        ShowWaylandToplevelWindow();
      }
    } else {
      LOG("  calling gtk_widget_show(mShell)\n");
      gtk_widget_show(mShell);
    }
    if (GdkIsWaylandDisplay()) {
      SetUserTimeAndStartupTokenForActivatedWindow();
#ifdef MOZ_WAYLAND
      auto token = std::move(mWindowActivationTokenFromEnv);
      if (!token.IsEmpty()) {
        FocusWaylandWindow(token.get());
      }
#endif
    }
    if (mHiddenPopupPositioned && IsPopup()) {
      LOG("  re-position hidden popup window");
      gtk_window_move(GTK_WINDOW(mShell), mPopupPosition.x, mPopupPosition.y);
      mHiddenPopupPositioned = false;
    }
  } else {
    LOG("nsWindow::NativeShow hide\n");
    if (GdkIsWaylandDisplay()) {
      if (IsWaylandPopup()) {
        // We can't close tracked popups directly as they may have visible
        // child popups. Just mark is as closed and let
        // UpdateWaylandPopupHierarchy() do the job.
        if (IsInPopupHierarchy()) {
          WaylandPopupMarkAsClosed();
          UpdateWaylandPopupHierarchy();
        } else {
          // Close untracked popups directly.
          HideWaylandPopupWindow(/* aTemporaryHide */ false,
                                 /* aRemoveFromPopupList */ true);
        }
      } else {
        HideWaylandToplevelWindow();
      }
    } else {
      // Workaround window freezes on GTK versions before 3.21.2 by
      // ensuring that configure events get dispatched to windows before
      // they are unmapped. See bug 1225044.
      if (gtk_check_version(3, 21, 2) != nullptr && mPendingConfigures > 0) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(mShell), &allocation);

        GdkEventConfigure event;
        PodZero(&event);
        event.type = GDK_CONFIGURE;
        event.window = mGdkWindow;
        event.send_event = TRUE;
        event.x = allocation.x;
        event.y = allocation.y;
        event.width = allocation.width;
        event.height = allocation.height;

        auto* shellClass = GTK_WIDGET_GET_CLASS(mShell);
        for (unsigned int i = 0; i < mPendingConfigures; i++) {
          Unused << shellClass->configure_event(mShell, &event);
        }
        mPendingConfigures = 0;
      }
      gtk_widget_hide(mShell);
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

LayoutDeviceIntSize nsWindow::GetSafeWindowSize(LayoutDeviceIntSize aSize) {
  // The X protocol uses CARD32 for window sizes, but the server (1.11.3)
  // reads it as CARD16.  Sizes of pixmaps, used for drawing, are (unsigned)
  // CARD16 in the protocol, but the server's ProcCreatePixmap returns
  // BadAlloc if dimensions cannot be represented by signed shorts.
  // Because we are creating Cairo surfaces to represent window buffers,
  // we also must ensure that the window can fit in a Cairo surface.
  LayoutDeviceIntSize result = aSize;
  int32_t maxSize = 32767;
  if (mWindowRenderer && mWindowRenderer->AsKnowsCompositor()) {
    maxSize = std::min(
        maxSize, mWindowRenderer->AsKnowsCompositor()->GetMaxTextureSize());
  }
  if (result.width > maxSize) {
    result.width = maxSize;
  }
  if (result.height > maxSize) {
    result.height = maxSize;
  }
  return result;
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

  LOG("nsWindow::SetInputRegion(%d, %d)", aInputRegion.mFullyTransparent,
      int(aInputRegion.mMargin));

  cairo_rectangle_int_t rect = {0, 0, 0, 0};
  cairo_region_t* region = nullptr;
  auto releaseRegion = MakeScopeExit([&] {
    if (region) {
      cairo_region_destroy(region);
    }
  });

  if (aInputRegion.mFullyTransparent) {
    region = cairo_region_create_rectangle(&rect);
  } else if (aInputRegion.mMargin != 0) {
    LayoutDeviceIntRect inputRegion(LayoutDeviceIntPoint(), mLastSizeRequest);
    inputRegion.Deflate(aInputRegion.mMargin);
    GdkRectangle gdkRect = DevicePixelsToGdkRectRoundOut(inputRegion);
    rect = {gdkRect.x, gdkRect.y, gdkRect.width, gdkRect.height};
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

  if (!IsTopLevelWidget()) {
    // We need to clear target buffer alpha values of popup windows as
    // SW-WR paints with alpha blending (see Bug 1674473).
    return;
  }

  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    return;
  }
  MOZ_ASSERT(gdk_window_get_window_type(window) == GDK_WINDOW_TOPLEVEL);

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

      for (auto iter = mOpaqueRegion.RectIter(); !iter.Done(); iter.Next()) {
        auto gdkRect = DevicePixelsToGdkRectRoundIn(iter.Get());
        cairo_rectangle_int_t rect = {gdkRect.x + offset.x,
                                      gdkRect.y + offset.y, gdkRect.width,
                                      gdkRect.height};
        cairo_region_union_rectangle(region, &rect);
      }
    }
    gdk_window_set_opaque_region(window, region);
    if (region) {
      cairo_region_destroy(region);
    }
  }

#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    moz_container_wayland_update_opaque_region(mContainer);
  }
#endif
}

bool nsWindow::IsChromeWindowTitlebar() {
  return mDrawInTitlebar && !mIsPIPWindow &&
         mWindowType == WindowType::TopLevel;
}

bool nsWindow::DoDrawTilebarCorners() {
  return IsChromeWindowTitlebar() && mSizeMode == nsSizeMode_Normal &&
         !mIsTiled;
}

GtkWidget* nsWindow::GetToplevelWidget() const { return mShell; }

GdkWindow* nsWindow::GetToplevelGdkWindow() const {
  return gtk_widget_get_window(mShell);
}

nsWindow* nsWindow::GetContainerWindow() const {
  GtkWidget* owningWidget = GTK_WIDGET(mContainer);
  if (!owningWidget) {
    return nullptr;
  }

  nsWindow* window = get_window_for_gtk_widget(owningWidget);
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

  // note that we don't handle BorderStyle::Close yet
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

already_AddRefed<widget::Screen> nsWindow::GetWidgetScreen() {
  // Wayland can read screen directly
  if (GdkIsWaylandDisplay()) {
    if (RefPtr<Screen> screen = ScreenHelperGTK::GetScreenForWindow(this)) {
      return screen.forget();
    }
  }

  // GetScreenBounds() is slow for the GTK port so we override and use
  // mBounds directly.
  ScreenManager& screenManager = ScreenManager::GetSingleton();
  LayoutDeviceIntRect bounds = mBounds;
  DesktopIntRect deskBounds = RoundedToInt(bounds / GetDesktopToDeviceScale());
  return screenManager.ScreenForRect(deskBounds);
}

RefPtr<VsyncDispatcher> nsWindow::GetVsyncDispatcher() {
#ifdef MOZ_WAYLAND
  if (mWaylandVsyncDispatcher) {
    return mWaylandVsyncDispatcher;
  }
#endif
  return nullptr;
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
    if (mIsPIPWindow) {
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

    if (mIsPIPWindow && gUseAspectRatio) {
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

  gtk_window_set_decorated(GTK_WINDOW(mShell),
                           !mUndecorated && aStyle != BorderStyle::None);

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
  nsIRollupListener::RollupOptions options{0,
                                           nsIRollupListener::FlushViews::Yes};
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

// This is an ugly workaround for
// https://bugzilla.mozilla.org/show_bug.cgi?id=1622107
// We try to detect when Wayland compositor / gtk fails to deliver
// info about finished D&D operations and cancel it on our own.
MOZ_CAN_RUN_SCRIPT static void WaylandDragWorkaround(nsWindow* aWindow,
                                                     GdkEventButton* aEvent) {
  static int buttonPressCountWithDrag = 0;

  // We track only left button state as Firefox performs D&D on left
  // button only.
  if (aEvent->button != 1 || aEvent->type != GDK_BUTTON_PRESS) {
    return;
  }

  nsCOMPtr<nsIDragService> dragService =
      do_GetService("@mozilla.org/widget/dragservice;1");
  if (!dragService) {
    return;
  }
  nsCOMPtr<nsIDragSession> currentDragSession =
      dragService->GetCurrentSession(aWindow);

  if (!currentDragSession) {
    buttonPressCountWithDrag = 0;
    return;
  }

  buttonPressCountWithDrag++;
  if (buttonPressCountWithDrag > 1) {
    NS_WARNING(
        "Quit unfinished Wayland Drag and Drop operation. Buggy Wayland "
        "compositor?");
    buttonPressCountWithDrag = 0;
    currentDragSession->EndDragSession(false, 0);
  }
}

static nsWindow* get_window_for_gtk_widget(GtkWidget* widget) {
  gpointer user_data = g_object_get_data(G_OBJECT(widget), "nsWindow");
  return static_cast<nsWindow*>(user_data);
}

static nsWindow* get_window_for_gdk_window(GdkWindow* window) {
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
  if (nsWindow* w = get_window_for_gdk_window(aWindow)) {
    margin = w->GetInputRegionMarginInGdkCoords();
  }

  x += margin;
  y += margin;

  gint w = gdk_window_get_width(aWindow) - margin;
  gint h = gdk_window_get_height(aWindow) - margin;

  return aMouseX > x && aMouseX < x + w && aMouseY > y && aMouseY < y + h;
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
    RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
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
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return FALSE;
  }

  return window->OnShellConfigureEvent(event);
}

static void size_allocate_cb(GtkWidget* widget, GtkAllocation* allocation) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return;
  }
  window->OnContainerSizeAllocate(allocation);
}

static void toplevel_window_size_allocate_cb(GtkWidget* widget,
                                             GtkAllocation* allocation) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return;
  }

  // NOTE(emilio): We need to do this here to override GTK's own opaque region
  // setting (which would clobber ours).
  window->UpdateOpaqueRegionInternal();
}

static gboolean delete_event_cb(GtkWidget* widget, GdkEventAny* event) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnDeleteEvent();

  return TRUE;
}

static gboolean enter_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event) {
  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
  if (!window) {
    return TRUE;
  }

  // We have stored leave notify - check if it's the correct one and
  // fire it before enter notify in such case.
  if (sStoredLeaveNotifyEvent) {
    auto clearNofityEvent =
        MakeScopeExit([&] { sStoredLeaveNotifyEvent = nullptr; });
    if (event->x_root == sStoredLeaveNotifyEvent->x_root &&
        event->y_root == sStoredLeaveNotifyEvent->y_root &&
        window->ApplyEnterLeaveMutterWorkaround()) {
      // Enter/Leave notify events has the same coordinates
      // and uses know buggy window config.
      // Consider it as a bogus one.
      return TRUE;
    }
    RefPtr<nsWindow> leftWindow =
        get_window_for_gdk_window(sStoredLeaveNotifyEvent->window);
    if (leftWindow) {
      leftWindow->OnLeaveNotifyEvent(sStoredLeaveNotifyEvent.get());
    }
  }

  window->OnEnterNotifyEvent(event);
  return TRUE;
}

static gboolean leave_notify_event_cb(GtkWidget* widget,
                                      GdkEventCrossing* event) {
  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
  if (!window) {
    return TRUE;
  }

  if (window->ApplyEnterLeaveMutterWorkaround()) {
    // The leave event is potentially wrong, don't fire it now but store
    // it for further check at enter_notify_event_cb().
    sStoredLeaveNotifyEvent.reset(reinterpret_cast<GdkEventCrossing*>(
        gdk_event_copy(reinterpret_cast<GdkEvent*>(event))));
  } else {
    sStoredLeaveNotifyEvent = nullptr;
    window->OnLeaveNotifyEvent(event);
  }

  return TRUE;
}

static gboolean motion_notify_event_cb(GtkWidget* widget,
                                       GdkEventMotion* event) {
  UpdateLastInputEventTime(event);

  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
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

  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
  if (!window) {
    return FALSE;
  }

  window->OnButtonPressEvent(event);

  if (GdkIsWaylandDisplay()) {
    WaylandDragWorkaround(window, event);
  }

  return TRUE;
}

static gboolean button_release_event_cb(GtkWidget* widget,
                                        GdkEventButton* event) {
  UpdateLastInputEventTime(event);

  if (event->button == 2 && !StaticPrefs::widget_gtk_middle_click_enabled()) {
    return FALSE;
  }

  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
  if (!window) {
    return FALSE;
  }

  window->OnButtonReleaseEvent(event);

  return TRUE;
}

static gboolean focus_in_event_cb(GtkWidget* widget, GdkEventFocus* event) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnContainerFocusInEvent(event);

  return FALSE;
}

static gboolean focus_out_event_cb(GtkWidget* widget, GdkEventFocus* event) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return FALSE;
  }

  window->OnContainerFocusOutEvent(event);

  return FALSE;
}

#ifdef MOZ_X11
// For long-lived popup windows that don't really take focus themselves but
// may have elements that accept keyboard input when the parent window is
// active, focus is handled specially.  These windows include noautohide
// panels.  (This special handling is not necessary for temporary popups where
// the keyboard is grabbed.)
//
// Mousing over or clicking on these windows should not cause them to steal
// focus from their parent windows, so, the input field of WM_HINTS is set to
// False to request that the window manager not set the input focus to this
// window.  http://tronche.com/gui/x/icccm/sec-4.html#s-4.1.7
//
// However, these windows can still receive WM_TAKE_FOCUS messages from the
// window manager, so they can still detect when the user has indicated that
// they wish to direct keyboard input at these windows.  When the window
// manager offers focus to these windows (after a mouse over or click, for
// example), a request to make the parent window active is issued.  When the
// parent window becomes active, keyboard events will be received.

static GdkFilterReturn popup_take_focus_filter(GdkXEvent* gdk_xevent,
                                               GdkEvent* event, gpointer data) {
  auto* xevent = static_cast<XEvent*>(gdk_xevent);
  if (xevent->type != ClientMessage) {
    return GDK_FILTER_CONTINUE;
  }

  XClientMessageEvent& xclient = xevent->xclient;
  if (xclient.message_type != gdk_x11_get_xatom_by_name("WM_PROTOCOLS")) {
    return GDK_FILTER_CONTINUE;
  }

  Atom atom = xclient.data.l[0];
  if (atom != gdk_x11_get_xatom_by_name("WM_TAKE_FOCUS")) {
    return GDK_FILTER_CONTINUE;
  }

  guint32 timestamp = xclient.data.l[1];

  GtkWidget* widget = get_gtk_widget_for_gdk_window(event->any.window);
  if (!widget) {
    return GDK_FILTER_CONTINUE;
  }

  GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(widget));
  if (!parent) {
    return GDK_FILTER_CONTINUE;
  }

  if (gtk_window_is_active(parent)) {
    return GDK_FILTER_REMOVE;  // leave input focus on the parent
  }

  GdkWindow* parent_window = gtk_widget_get_window(GTK_WIDGET(parent));
  if (!parent_window) {
    return GDK_FILTER_CONTINUE;
  }

  // In case the parent has not been deiconified.
  gdk_window_show_unraised(parent_window);

  // Request focus on the parent window.
  // Use gdk_window_focus rather than gtk_window_present to avoid
  // raising the parent window.
  gdk_window_focus(parent_window, timestamp);
  return GDK_FILTER_REMOVE;
}
#endif /* MOZ_X11 */

static gboolean key_press_event_cb(GtkWidget* widget, GdkEventKey* event) {
  LOGW("key_press_event_cb\n");

  UpdateLastInputEventTime(event);

  // find the window with focus and dispatch this event to that widget
  nsWindow* window = get_window_for_gtk_widget(widget);
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
  GdkDisplay* gdkDisplay = gtk_widget_get_display(widget);
  if (GdkIsX11Display(gdkDisplay)) {
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdkDisplay);
    while (XPending(dpy)) {
      XEvent next_event;
      XPeekEvent(dpy, &next_event);
      GdkWindow* nextGdkWindow =
          gdk_x11_window_lookup_for_display(gdkDisplay, next_event.xany.window);
      if (nextGdkWindow != event->window || next_event.type != KeyPress ||
          next_event.xkey.keycode != event->hardware_keycode ||
          next_event.xkey.state != (event->state & NS_GDKEVENT_MATCH_MASK)) {
        break;
      }
      XNextEvent(dpy, &next_event);
      event->time = next_event.xkey.time;
    }
  }
#endif

  return focusWindow->OnKeyPressEvent(event);
}

static gboolean key_release_event_cb(GtkWidget* widget, GdkEventKey* event) {
  LOGW("key_release_event_cb\n");

  UpdateLastInputEventTime(event);

  // find the window with focus and dispatch this event to that widget
  nsWindow* window = get_window_for_gtk_widget(widget);
  if (!window) {
    return FALSE;
  }

  RefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;
  return focusWindow->OnKeyReleaseEvent(event);
}

static gboolean property_notify_event_cb(GtkWidget* aWidget,
                                         GdkEventProperty* aEvent) {
  RefPtr<nsWindow> window = get_window_for_gdk_window(aEvent->window);
  if (!window) {
    return FALSE;
  }

  return window->OnPropertyNotifyEvent(aWidget, aEvent);
}

static gboolean scroll_event_cb(GtkWidget* widget, GdkEventScroll* event) {
  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
  if (NS_WARN_IF(!window)) {
    return FALSE;
  }

  window->OnScrollEvent(event);

  return TRUE;
}

static gboolean visibility_notify_event_cb(GtkWidget* widget,
                                           GdkEventVisibility* event) {
  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
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
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
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
  RefPtr<nsWindow> window = get_window_for_gtk_widget(GTK_WIDGET(container));
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
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return;
  }
  window->OnCompositedChanged();
}

static void scale_changed_cb(GtkWidget* widget, GParamSpec* aPSpec,
                             gpointer aPointer) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
  if (!window) {
    return;
  }

  window->OnScaleEvent();
}

static gboolean touch_event_cb(GtkWidget* aWidget, GdkEventTouch* aEvent) {
  UpdateLastInputEventTime(aEvent);

  RefPtr<nsWindow> window = get_window_for_gdk_window(aEvent->window);
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

  RefPtr<nsWindow> window = get_window_for_gdk_window(event->window);

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
  RefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
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

gboolean WindowDragMotionHandler(GtkWidget* aWidget,
                                 GdkDragContext* aDragContext, gint aX, gint aY,
                                 guint aTime) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
  if (!window || !window->GetGdkWindow()) {
    LOGDRAG("WindowDragMotionHandler() can't get GdkWindow!");
    return FALSE;
  }

  // We're getting aX,aY in mShell coordinates space.
  // mContainer is shifted by CSD decorations so translate the coords
  // to mContainer space where our content lives.
  if (aWidget == window->GetGtkWidget()) {
    int x, y;
    gdk_window_get_geometry(window->GetGdkWindow(), &x, &y, nullptr, nullptr);
    aX -= x;
    aY -= y;
  }

  LOGDRAG("WindowDragMotionHandler target nsWindow [%p]", window.get());

  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  NS_ENSURE_TRUE(dragService, FALSE);
  nsDragSession* dragSession =
      static_cast<nsDragSession*>(dragService->GetCurrentSession(window));
  if (!dragSession) {
    LOGDRAG(
        "WindowDragMotionHandler missing current session, creating a new one.");
    // This may be the start of an external drag session.
    nsIWidget* widget = window;
    dragSession =
        static_cast<nsDragSession*>(dragService->StartDragSession(widget));
  }
  NS_ENSURE_TRUE(dragSession, FALSE);

  nsDragSession::AutoEventLoop loop(dragSession);
  if (!dragSession->ScheduleMotionEvent(
          window, aDragContext, GetWindowDropPosition(window, aX, aY), aTime)) {
    return FALSE;
  }
  return TRUE;
}

static gboolean drag_motion_event_cb(GtkWidget* aWidget,
                                     GdkDragContext* aDragContext, gint aX,
                                     gint aY, guint aTime, gpointer aData) {
  LOGDRAG("mShell::drag_motion");
  bool result = WindowDragMotionHandler(aWidget, aDragContext, aX, aY, aTime);

  // If we return true, we need to set D&D status by gdk_drag_status()
  // at drag-data-received
  LOGDRAG("mShell::drag_motion, returns %d", result);
  return result;
}

void WindowDragLeaveHandler(GtkWidget* aWidget) {
  LOGDRAG("WindowDragLeaveHandler()\n");

  RefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
  if (!window) {
    LOGDRAG("    Failed - can't find nsWindow!\n");
    return;
  }

  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  nsIWidget* widget = window;
  nsDragSession* dragSession =
      static_cast<nsDragSession*>(dragService->GetCurrentSession(widget));
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

static void drag_leave_event_cb(GtkWidget* aWidget,
                                GdkDragContext* aDragContext, guint aTime,
                                gpointer aData) {
  LOGDRAG("mShell::drag_leave");
  WindowDragLeaveHandler(aWidget);
}

gboolean WindowDragDropHandler(GtkWidget* aWidget, GdkDragContext* aDragContext,
                               gint aX, gint aY, guint aTime) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
  if (!window || !window->GetGdkWindow()) {
    return FALSE;
  }

  // We're getting aX,aY in mShell coordinates space.
  // mContainer is shifted by CSD decorations so translate the coords
  // to mContainer space where our content lives.
  if (aWidget == window->GetGtkWidget()) {
    int x, y;
    gdk_window_get_geometry(window->GetGdkWindow(), &x, &y, nullptr, nullptr);
    aX -= x;
    aY -= y;
  }

  LOGDRAG("WindowDragDropHandler nsWindow [%p]", window.get());
  RefPtr<nsDragService> dragService = nsDragService::GetInstance();
  nsDragSession* dragSession =
      static_cast<nsDragSession*>(dragService->GetCurrentSession(window));
  if (!dragSession) {
    LOGDRAG("    Received dragdrop after drag end.\n");
    return FALSE;
  }
  nsDragSession::AutoEventLoop loop(dragSession);
  return dragSession->ScheduleDropEvent(
      window, aDragContext, GetWindowDropPosition(window, aX, aY), aTime);
}

static gboolean drag_drop_event_cb(GtkWidget* aWidget,
                                   GdkDragContext* aDragContext, gint aX,
                                   gint aY, guint aTime, gpointer aData) {
  LOGDRAG("mShell::drag_drop");
  bool result = WindowDragDropHandler(aWidget, aDragContext, aX, aY, aTime);

  // If drag-drop returns true, we need to terminate D&D by gtk_drag_finish().
  LOGDRAG("mShell::drag_drop result %d", result);
  return result;
}

static void drag_data_received_event_cb(GtkWidget* aWidget,
                                        GdkDragContext* aDragContext, gint aX,
                                        gint aY,
                                        GtkSelectionData* aSelectionData,
                                        guint aInfo, guint aTime,
                                        gpointer aData) {
  RefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
  if (!window) {
    return;
  }
  LOGDRAG("mShell::drag_data_received");
  window->OnDragDataReceivedEvent(aWidget, aDragContext, aX, aY, aSelectionData,
                                  aInfo, aTime, aData);
}

static nsresult initialize_prefs(void) {
  if (Preferences::HasUserValue("widget.use-aspect-ratio")) {
    gUseAspectRatio = Preferences::GetBool("widget.use-aspect-ratio", true);
  } else {
    gUseAspectRatio = IsGnomeDesktopEnvironment() || IsKdeDesktopEnvironment();
  }
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

  return nsBaseWidget::GetWindowRenderer();
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
 * nsBaseWidget::CreateCompositor(), i.e. nsWindow::GetWindowRenderer().
 *
 * SetCompositorWidgetDelegate(null) is called from
 * nsBaseWidget::DestroyCompositor().
 */
void nsWindow::SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) {
  LOG("nsWindow::SetCompositorWidgetDelegate %p mIsMapped %d "
      "mCompositorWidgetDelegate %p\n",
      delegate, !!mIsMapped, mCompositorWidgetDelegate);

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (delegate) {
    mCompositorWidgetDelegate = delegate->AsPlatformSpecificDelegate();
    MOZ_ASSERT(mCompositorWidgetDelegate,
               "nsWindow::SetCompositorWidgetDelegate called with a "
               "non-PlatformCompositorWidgetDelegate");
    if (mIsMapped) {
      ConfigureCompositor();
    }
  } else {
    mCompositorWidgetDelegate = nullptr;
  }
}

bool nsWindow::IsAlwaysUndecoratedWindow() const {
  if (mIsPIPWindow || gKioskMode) {
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
      aState == mDrawInTitlebar) {
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

    if (!gtk_widget_get_realized(GTK_WIDGET(mShell))) {
      LOG("    Using CSD mode fast path\n");
      gtk_window_set_titlebar(GTK_WINDOW(mShell),
                              aState ? gtk_fixed_new() : nullptr);
      return;
    }

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

    // Label mShell toplevel window so property_notify_event_cb callback
    // can find its way home.
    g_object_set_data(G_OBJECT(GetToplevelGdkWindow()), "nsWindow", this);

    if (AreBoundsSane()) {
      GdkRectangle size =
          DevicePixelsToGdkSizeRoundUp(GetClientBounds().Size());
      LOG("    resize to %d x %d\n", size.width, size.height);
      gtk_window_resize(GTK_WINDOW(mShell), size.width, size.height);
    }

    if (visible) {
      mNeedsShow = true;
      NativeShow(true);
    }

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
  if (nsWindow* topmost = GetTopmostWindow()) {
    LOGVERBOSE("nsWindow::GdkCeiledScaleFactor(): toplevel [%p] scale %d",
               topmost, (int)topmost->mCeiledScaleFactor);
    return topmost->mCeiledScaleFactor;
  }

  LOGVERBOSE("nsWindow::GdkCeiledScaleFactor(): monitor scale %d",
             ScreenHelperGTK::GetGTKMonitorScaleFactor());
  return ScreenHelperGTK::GetGTKMonitorScaleFactor();
}

double nsWindow::FractionalScaleFactor() {
#ifdef MOZ_WAYLAND
  if (mSurface) {
    auto scale = mSurface->GetScale();
    if (scale != sNoScale) {
      LOGVERBOSE("nsWindow::FractionalScaleFactor(): fractional scale %f",
                 scale);
      return scale;
    }
  }
#endif
  return GdkCeiledScaleFactor();
}

gint nsWindow::DevicePixelsToGdkCoordRoundUp(int aPixels) {
  double scale = FractionalScaleFactor();
  return ceil(aPixels / scale);
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

GdkRectangle nsWindow::DevicePixelsToGdkSizeRoundUp(
    const LayoutDeviceIntSize& aSize) {
  double scale = FractionalScaleFactor();
  gint width = ceil(aSize.width / scale);
  gint height = ceil(aSize.height / scale);
  return {0, 0, width, height};
}

int nsWindow::GdkCoordToDevicePixels(gint aCoord) {
  return (int)(aCoord * FractionalScaleFactor());
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

LayoutDeviceIntMargin nsWindow::GtkBorderToDevicePixels(
    const GtkBorder& aBorder) {
  double scale = FractionalScaleFactor();
  return LayoutDeviceIntMargin{
      (int)(aBorder.top * scale), (int)(aBorder.right * scale),
      (int)(aBorder.bottom * scale), (int)(aBorder.left * scale)};
}

LayoutDeviceIntRect nsWindow::GdkRectToDevicePixels(const GdkRectangle& aRect) {
  double scale = FractionalScaleFactor();
  return LayoutDeviceIntRect::RoundIn(
      (float)(aRect.x * scale), (float)(aRect.y * scale),
      (float)(aRect.width * scale), (float)(aRect.height * scale));
}

nsresult nsWindow::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
    nsIObserver* aObserver) {
  LOG("SynthesizeNativeMouseEvent(%d, %d, %d, %d, %d)", aPoint.x.value,
      aPoint.y.value, int(aNativeMessage), int(aButton), int(aModifierFlags));

  AutoObserverNotifier notifier(aObserver, "mouseevent");

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
    double aDeltaX, double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
    uint32_t aAdditionalFlags, nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "mousescrollevent");

  if (!mGdkWindow) {
    return NS_OK;
  }

  CreateAndPutGdkScrollEvent(aPoint, aDeltaX, aDeltaY);

  return NS_OK;
}

nsresult nsWindow::SynthesizeNativeTouchPoint(uint32_t aPointerId,
                                              TouchPointerState aPointerState,
                                              LayoutDeviceIntPoint aPoint,
                                              double aPointerPressure,
                                              uint32_t aPointerOrientation,
                                              nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "touchpoint");

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

nsresult nsWindow::SynthesizeNativeTouchpadPan(TouchpadGesturePhase aEventPhase,
                                               LayoutDeviceIntPoint aPoint,
                                               double aDeltaX, double aDeltaY,
                                               int32_t aModifierFlags,
                                               nsIObserver* aObserver) {
  AutoObserverNotifier notifier(aObserver, "touchpadpanevent");

  if (!mGdkWindow) {
    return NS_OK;
  }

  // This should/could maybe send GdkEventTouchpadSwipe events, however we don't
  // currently consume those (either real user input or testing events). So we
  // send gdk scroll events to be more like what we do for real user input. If
  // we start consuming GdkEventTouchpadSwipe and get those hooked up to swipe
  // to nav, then maybe we should test those too.

  mCurrentSynthesizedTouchpadPan.mTouchpadGesturePhase = Some(aEventPhase);
  MOZ_ASSERT(mCurrentSynthesizedTouchpadPan.mSavedObserver == 0);
  mCurrentSynthesizedTouchpadPan.mSavedObserver = notifier.SaveObserver();

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

int32_t nsWindow::RoundsWidgetCoordinatesTo() { return GdkCeiledScaleFactor(); }

void nsWindow::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  nsCString displayName;

  LOG("nsWindow::GetCompositorWidgetInitData");

  Window window = GetX11Window();
#ifdef MOZ_X11
  // We're bit hackish here. Old GLX backend needs XWindow when GLContext
  // is created so get XWindow now before map signal.
  // We may see crashes/errors when nsWindow is unmapped (XWindow is
  // invalidated) but we can't do anything about it.
  if (!window && !gfxVars::UseEGL()) {
    window =
        gdk_x11_window_get_xid(gtk_widget_get_window(GTK_WIDGET(mContainer)));
  }
#endif
  *aInitData = mozilla::widget::GtkCompositorWidgetInitData(
      window, displayName, GdkIsX11Display(), GetClientSize());

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    // Make sure the window XID is propagated to X server, we can fail otherwise
    // in GPU process (Bug 1401634).
    Display* display = DefaultXDisplay();
    XFlush(display);
    displayName = nsCString(XDisplayString(display));
  }
#endif
}

#ifdef MOZ_X11
/* XApp progress support currently works by setting a property
 * on a window with this Atom name.  A supporting window manager
 * will notice this and pass it along to whatever handling has
 * been implemented on that end (e.g. passing it on to a taskbar
 * widget.)  There is no issue if WM support is lacking, this is
 * simply ignored in that case.
 *
 * See https://github.com/linuxmint/xapps/blob/master/libxapp/xapp-gtk-window.c
 * for further details.
 */

#  define PROGRESS_HINT "_NET_WM_XAPP_PROGRESS"

static void set_window_hint_cardinal(Window xid, const gchar* atom_name,
                                     gulong cardinal) {
  GdkDisplay* display;

  display = gdk_display_get_default();

  if (cardinal > 0) {
    XChangeProperty(GDK_DISPLAY_XDISPLAY(display), xid,
                    gdk_x11_get_xatom_by_name_for_display(display, atom_name),
                    XA_CARDINAL, 32, PropModeReplace, (guchar*)&cardinal, 1);
  } else {
    XDeleteProperty(GDK_DISPLAY_XDISPLAY(display), xid,
                    gdk_x11_get_xatom_by_name_for_display(display, atom_name));
  }
}
#endif  // MOZ_X11

void nsWindow::SetProgress(unsigned long progressPercent) {
#ifdef MOZ_X11

  if (!GdkIsX11Display()) {
    return;
  }

  if (!mShell) {
    return;
  }

  progressPercent = MIN(progressPercent, 100);

  set_window_hint_cardinal(GDK_WINDOW_XID(GetToplevelGdkWindow()),
                           PROGRESS_HINT, progressPercent);
#endif  // MOZ_X11
}

#ifdef MOZ_X11
void nsWindow::SetCompositorHint(WindowComposeRequest aState) {
  if (!GdkIsX11Display()) {
    return;
  }

  gulong value = aState;
  GdkAtom cardinal_atom = gdk_x11_xatom_to_atom(XA_CARDINAL);
  gdk_property_change(GetToplevelGdkWindow(),
                      gdk_atom_intern("_NET_WM_BYPASS_COMPOSITOR", FALSE),
                      cardinal_atom,
                      32,  // format
                      GDK_PROP_MODE_REPLACE, (guchar*)&value, 1);
}
#endif

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

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

#ifdef MOZ_WAYLAND
static void relative_pointer_handle_relative_motion(
    void* data, struct zwp_relative_pointer_v1* pointer, uint32_t time_hi,
    uint32_t time_lo, wl_fixed_t dx_w, wl_fixed_t dy_w, wl_fixed_t dx_unaccel_w,
    wl_fixed_t dy_unaccel_w) {
  RefPtr<nsWindow> window(reinterpret_cast<nsWindow*>(data));

  WidgetMouseEvent event(true, eMouseMove, window, WidgetMouseEvent::eReal);

  double scale = window->FractionalScaleFactor();
  event.mRefPoint = window->GetNativePointerLockCenter();
  event.mRefPoint.x += int(wl_fixed_to_double(dx_w) * scale);
  event.mRefPoint.y += int(wl_fixed_to_double(dy_w) * scale);

  event.AssignEventTime(window->GetWidgetEventTime(time_lo));
  window->DispatchInputEvent(&event);
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener =
    {
        relative_pointer_handle_relative_motion,
};

void nsWindow::SetNativePointerLockCenter(
    const LayoutDeviceIntPoint& aLockCenter) {
  mNativePointerLockCenter = aLockCenter;
}

void nsWindow::LockNativePointer() {
  if (!GdkIsWaylandDisplay()) {
    return;
  }

  auto* waylandDisplay = WaylandDisplayGet();

  auto* pointerConstraints = waylandDisplay->GetPointerConstraints();
  if (!pointerConstraints) {
    return;
  }

  auto* relativePointerMgr = waylandDisplay->GetRelativePointerManager();
  if (!relativePointerMgr) {
    return;
  }

  GdkDisplay* display = gdk_display_get_default();

  GdkDeviceManager* manager = gdk_display_get_device_manager(display);
  MOZ_ASSERT(manager);

  GdkDevice* device = gdk_device_manager_get_client_pointer(manager);
  if (!device) {
    NS_WARNING("Could not find Wayland pointer to lock");
    return;
  }
  wl_pointer* pointer = gdk_wayland_device_get_wl_pointer(device);
  MOZ_ASSERT(pointer);

  wl_surface* surface =
      gdk_wayland_window_get_wl_surface(GetToplevelGdkWindow());
  if (!surface) {
    /* Can be null when the window is hidden.
     * Though it's unlikely that a lock request comes in that case, be
     * defensive. */
    return;
  }

  UnlockNativePointer();

  mLockedPointer = zwp_pointer_constraints_v1_lock_pointer(
      pointerConstraints, surface, pointer, nullptr,
      ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  if (!mLockedPointer) {
    NS_WARNING("Could not lock Wayland pointer");
    return;
  }

  mRelativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
      relativePointerMgr, pointer);
  if (!mRelativePointer) {
    NS_WARNING("Could not create relative Wayland pointer");
    zwp_locked_pointer_v1_destroy(mLockedPointer);
    mLockedPointer = nullptr;
    return;
  }

  zwp_relative_pointer_v1_add_listener(mRelativePointer,
                                       &relative_pointer_listener, this);
}

void nsWindow::UnlockNativePointer() {
  if (mRelativePointer) {
    zwp_relative_pointer_v1_destroy(mRelativePointer);
    mRelativePointer = nullptr;
  }
  if (mLockedPointer) {
    zwp_locked_pointer_v1_destroy(mLockedPointer);
    mLockedPointer = nullptr;
  }
}
#endif

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

nsIFrame* nsWindow::GetFrame() const {
  nsView* view = nsView::GetViewFor(this);
  if (!view) {
    return nullptr;
  }
  return view->GetFrame();
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

  if (!mWidgetListener || !mWidgetListener->GetPresShell()) {
    return;
  }

  nsIFrame* frame = GetFrame();
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
    float width = DevicePixelsToGdkCoordRoundDown(mLastSizeRequest.width);
    float height = DevicePixelsToGdkCoordRoundDown(mLastSizeRequest.height);

    mAspectRatio = width / height;
    LOG("nsWindow::LockAspectRatio() width %f height %f aspect %f", width,
        height, mAspectRatio);
  } else {
    mAspectRatio = 0.0;
    LOG("nsWindow::LockAspectRatio() removed aspect ratio");
  }

  ApplySizeConstraints();
}

nsWindow* nsWindow::GetFocusedWindow() { return gFocusWindow; }

#ifdef MOZ_WAYLAND
bool nsWindow::SetEGLNativeWindowSize(
    const LayoutDeviceIntSize& aEGLWindowSize) {
  // SetEGLNativeWindowSize() is Wayland only call.
  MOZ_ASSERT(GdkIsWaylandDisplay());

  if (!mIsMapped) {
    return true;
  }

#  ifdef MOZ_LOGGING
  if (LOG_ENABLED_VERBOSE()) {
    float scale = FractionalScaleFactor();
    static uintptr_t lastSizeLog = 0;
    uintptr_t sizeLog =
        uintptr_t(this) + aEGLWindowSize.width + aEGLWindowSize.height + scale +
        aEGLWindowSize.width / scale + aEGLWindowSize.height / scale;
    if (lastSizeLog != sizeLog) {
      lastSizeLog = sizeLog;
      LOGVERBOSE(
          "nsWindow::SetEGLNativeWindowSize() %d x %d scale %f (unscaled "
          "%f x %f)",
          aEGLWindowSize.width, aEGLWindowSize.height, scale,
          aEGLWindowSize.width / scale, aEGLWindowSize.height / scale);
    }
  }
#  endif
  return mSurface->SetEGLWindowSize(aEGLWindowSize.ToUnknownSize());
}
#endif

nsWindow* nsWindow::GetWindow(GdkWindow* window) {
  return get_window_for_gdk_window(window);
}

void nsWindow::ClearRenderingQueue() {
  LOG("nsWindow::ClearRenderingQueue()");

  if (mWidgetListener) {
    mWidgetListener->RequestWindowClose(this);
  }
  DestroyLayerManager();
}

// nsWindow::OnMap() / nsWindow::OnUnmap() is called from map/unmap mContainer
// handlers directly as we paint to mContainer.
void nsWindow::OnMap() {
  LOG("nsWindow::OnMap");

  {
    MutexAutoLock lock(mWindowVisibilityMutex);
    mIsMapped = true;

    EnsureGdkWindow();
    RefreshScale(/* aRefreshScreen */ false);

    if (mIsAlert) {
      gdk_window_set_override_redirect(GetToplevelGdkWindow(), TRUE);
    }

#ifdef MOZ_X11
    if (GdkIsX11Display()) {
      mSurfaceProvider.Initialize(GetX11Window());

      // Set window manager hint to keep fullscreen windows composited.
      //
      // If the window were to get unredirected, there could be visible
      // tearing because Gecko does not align its framebuffer updates with
      // vblank.
      SetCompositorHint(GTK_WIDGET_COMPOSITED_ENABLED);
    }
#endif
#ifdef MOZ_WAYLAND
    if (GdkIsWaylandDisplay()) {
      mSurfaceProvider.Initialize(this);
    }
#endif
  }

  if (mIsDragPopup && GdkIsX11Display()) {
    if (GtkWidget* parent = gtk_widget_get_parent(mShell)) {
      gtk_widget_set_opacity(parent, 0.0);
    }
  }

  if (mWindowType == WindowType::Popup) {
    if (mNoAutoHide) {
      gint wmd = ConvertBorderStyles(mBorderStyle);
      if (wmd != -1) {
        gdk_window_set_decorations(mGdkWindow, (GdkWMDecoration)wmd);
      }
    }
    // If the popup ignores mouse events, set an empty input shape.
    SetInputRegion(mInputRegion);
  }

  RefreshWindowClass();

  // We're not mapped yet but we have already created compositor.
  if (mCompositorWidgetDelegate) {
    ConfigureCompositor();
  }

  LOG("  finished, new GdkWindow %p XID 0x%lx\n", mGdkWindow, GetX11Window());
}

void nsWindow::OnUnmap() {
  LOG("nsWindow::OnUnmap");

  {
    MutexAutoLock lock(mWindowVisibilityMutex);
    mIsMapped = false;

    if (mSourceDragContext) {
      static auto sGtkDragCancel =
          (void (*)(GdkDragContext*))dlsym(RTLD_DEFAULT, "gtk_drag_cancel");
      if (sGtkDragCancel) {
        sGtkDragCancel(mSourceDragContext);
        mSourceDragContext = nullptr;
      }
    }

    if (mGdkWindow) {
      g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", nullptr);
      mGdkWindow = nullptr;
    }

    // Reset scale for hidden windows
    mCeiledScaleFactor = sNoScale;

    // Clear resources (mainly XWindow) stored at GtkCompositorWidget.
    // It makes sure we don't paint to it when nsWindow becomes hiden/deleted
    // and XWindow is released.
    if (mCompositorWidgetDelegate) {
      mCompositorWidgetDelegate->CleanupResources();
    }

    // Clear nsWindow resources used for old (in-thread) rendering.
    mSurfaceProvider.CleanupResources();
  }

  // Until Bug 1654938 is fixed we delete layer manager for hidden popups,
  // otherwise it can easily hold 1GB+ memory for long time.
  if (mWindowType == WindowType::Popup) {
    DestroyLayerManager();
  } else {
    // Widget is backed by OpenGL EGLSurface created over wl_surface/XWindow.
    //
    // RenderCompositorEGL::Resume() deletes recent EGLSurface,
    // calls nsWindow::GetNativeData(NS_NATIVE_EGL_WINDOW) from compositor
    // thread to get new native rendering surface.
    //
    // For hidden/unmapped windows we return nullptr NS_NATIVE_EGL_WINDOW at
    // nsWindow::GetNativeData() so RenderCompositorEGL::Resume() creates
    // offscreen fallback EGLSurface to avoid compositor pause.
    //
    // We don't want to pause compositor as it may lead to whole
    // browser freeze (Bug 1777664).
    //
    // If RenderCompositorSWGL compositor is used (SW fallback)
    // RenderCompositorSWGL::Resume() only requests full render for next paint
    // as wl_surface/XWindow is managed by WindowSurfaceProvider owned
    // directly by GtkCompositorWidget and that's covered by
    // mCompositorWidgetDelegate->CleanupResources() call above.
    if (CompositorBridgeChild* remoteRenderer = GetRemoteRenderer()) {
      remoteRenderer->SendResume();
    }
  }
}

// Apply workaround for Mutter compositor bug (mzbz#1777269).
//
// When we open a popup window (tooltip for instance) attached to
// GDK_WINDOW_TYPE_HINT_UTILITY parent popup, Mutter compositor sends bogus
// leave/enter events to the GDK_WINDOW_TYPE_HINT_UTILITY popup.
// That leads to immediate tooltip close. As a workaround ignore these
// bogus events.
//
// We need to check two affected window types:
//
// - toplevel window with at least two child popups where the first one is
//   GDK_WINDOW_TYPE_HINT_UTILITY.
// - GDK_WINDOW_TYPE_HINT_UTILITY popup with a child popup
//
// We need to mask two bogus leave/enter sequences:
//  1) Leave (popup) -> Enter (toplevel)
//  2) Leave (toplevel) -> Enter (popup)
//
// TODO: persistent (non-tracked) popups with tooltip/child popups?
//
bool nsWindow::ApplyEnterLeaveMutterWorkaround() {
  // Leave (toplevel) case
  if (mWindowType == WindowType::TopLevel && mWaylandPopupNext &&
      mWaylandPopupNext->mWaylandPopupNext &&
      gtk_window_get_type_hint(GTK_WINDOW(mWaylandPopupNext->GetGtkWidget())) ==
          GDK_WINDOW_TYPE_HINT_UTILITY) {
    LOG("nsWindow::ApplyEnterLeaveMutterWorkaround(): leave toplevel");
    return true;
  }
  // Leave (popup) case
  if (IsWaylandPopup() && mWaylandPopupNext &&
      gtk_window_get_type_hint(GTK_WINDOW(mShell)) ==
          GDK_WINDOW_TYPE_HINT_UTILITY) {
    LOG("nsWindow::ApplyEnterLeaveMutterWorkaround(): leave popup");
    return true;
  }
  return false;
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
    if (auto* menuPopupFrame = GetMenuPopupFrame(GetFrame())) {
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
