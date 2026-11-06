/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowWaylandGtk.h"

#include <dlfcn.h>

#include "WaylandVsyncSource.h"
#include "WidgetUtilsGtk.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/VsyncDispatcher.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsAppShell.h"
#include "nsGtkKeyUtils.h"
#include "nsGtkUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsWaylandDisplay.h"
#include "nsWindow.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

// All nsWindowWaylandPopup instances are actually nsWindowWaylandPopupGtk, so
// PopupAsGtk() only returns null when aPopup itself is null (e.g. end of
// chain).
static nsWindowWaylandPopupGtk* PopupAsGtk(nsWindowWaylandPopup* aPopup) {
  return aPopup ? aPopup->AsGtk() : nullptr;
}

/*
  Window restore session uses GetWorkspaceID()/MoveToWorkspace() differently
  on Wayland.

  MoveToWorkspace() is normally called when a window is visible/rendered and
  widget code moves the window to a particular workspace. That approach doesn't
  work on Wayland where an application can't position itself.

  On Wayland we need to place the window before it's shown, so we use this
  path instead:

  1) In ConfigureToplevelWindowNative() we register an "xdg-toplevel-realized"
     GTK signal handler to get notified when the xdg_toplevel window is created.
  2) Block NativeShow() while mWaitingToSessionRestore is set. We can't show
     the toplevel window until we have a window restore ID provided by a
     MoveToWorkspace() call.
  3) In MoveToWorkspace(), save the session ID and show the window. That fires
     the "xdg-toplevel-realized" signal which gives us the xdg_toplevel and we
     restore window state according to the session ID.

  Likewise we use GetWorkspaceID() to create/save the xdg_toplevel session
  for later restore.

  A window which does not wait for session restore cannot be restored, so it
  ignores MoveToWorkspace() and is added to the session under a new ID instead.
  This happens when a previous session is restored from the menu or a closed
  window is reopened.
*/

static struct xdg_toplevel* GetXdgToplevelFromGdkWindow(GdkWindow* aWindow) {
  static auto sGdkWaylandWindowGetXdgToplevel =
      (struct xdg_toplevel * (*)(GdkWindow*))
          dlsym(RTLD_DEFAULT, "gdk_wayland_window_get_xdg_toplevel");
  if (!sGdkWaylandWindowGetXdgToplevel) {
    return nullptr;
  }
  return sGdkWaylandWindowGetXdgToplevel(aWindow);
}

// aRestoreWindow = true means we're loading mSessionRestoreToken
// from already saved toplevel window state.
// aRestoreWindow = false means we're creating a new restore session.
bool nsWindowWaylandToplevelGtk::CreateRestoreSession(bool aRestoreWindow) {
  MOZ_DIAGNOSTIC_ASSERT(!mSessionRestoreToken);
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    LOG("nsWindowWaylandToplevelGtk::CreateRestoreSession(): failed to get "
        "GdkWindow, quit.");
    return false;
  }
  struct xdg_toplevel* toplevel = GetXdgToplevelFromGdkWindow(window);
  if (!toplevel) {
    LOG("nsWindowWaylandToplevelGtk::CreateRestoreSession(): failed to get "
        "xdg-toplevel, quit.");
    return false;
  }
  return CreateRestoreSessionImpl(aRestoreWindow, toplevel);
}

#ifdef MOZ_LOGGING
static void SessionRestoredHandler(void* aData,
                                   xdg_toplevel_session_v1* aToplevelSession) {
  LOGW("nsWindowWayland restored [%p]", aData);
}

static const xdg_toplevel_session_v1_listener sSessionListener = {
    SessionRestoredHandler,
};
#endif

void nsWindowWaylandToplevelGtk::RestoreXdgToplevel() {
  LOG("nsWindowWaylandToplevelGtk::RestoreXdgToplevel() ID %s GdkWindow [%p]",
      mWorkspaceID.get(), GetToplevelGdkWindow());
  if (CreateRestoreSession(/* aRestoreWindow */ true)) {
#ifdef MOZ_LOGGING
    if (LOG_ENABLED()) {
      xdg_toplevel_session_v1_add_listener(mSessionRestoreToken,
                                           &sSessionListener, this);
    }
#endif
  }
}

void nsWindowWaylandToplevelGtk::FocusWaylandWindow(const char* aTokenID) {
  MOZ_DIAGNOSTIC_ASSERT(aTokenID);

  LOG("nsWindowWaylandToplevelGtk::FocusWaylandWindow(%s)", aTokenID);
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

  FocusWaylandWindowImpl(aTokenID, surface);
}

void nsWindowWaylandToplevelGtk::MaybeCreatePipResources() {
  LOG("MaybeCreatePipResources()");
  if (!IsPipWindow()) {
    return;
  }

  static xx_pip_v1_listener pip_listener = {
      .closed =
          [](void* data, xx_pip_v1*) {
            LOGW("xx_pip_v1_listener::closed()");
            RefPtr self = static_cast<nsWindowWaylandToplevelGtk*>(data);
            gtk_window_close(GTK_WINDOW(self->mShell));
          },
      .configure_bounds =
          [](void* data, xx_pip_v1*, int32_t w, int32_t h) {
            LOGW("xx_pip_v1_listener::configure_bounds(%d, %d)", w, h);
          },
      .configure_size =
          [](void* data, xx_pip_v1*, int32_t w, int32_t h) {
            LOGW("xx_pip_v1_listener::configure_size(%d, %d)", w, h);
            auto* self = static_cast<nsWindowWaylandToplevelGtk*>(data);
            if (w == 0 && h == 0) {
              gtk_window_get_size(GTK_WINDOW(self->mShell), &w, &h);
            }
            self->mPipResources.mConfigureSize = {w, h};
          },
  };

  static xdg_surface_listener surface_listener = {
      .configure =
          [](void* data, struct xdg_surface* surface, uint32_t serial) {
            LOGW("xdg_surface_listener::configure(%u)", serial);
            RefPtr self = static_cast<nsWindowWaylandToplevelGtk*>(data);
            xdg_surface_ack_configure(surface, serial);
            auto size = self->mPipResources.mConfigureSize;
            gtk_window_resize(GTK_WINDOW(self->mShell), size.width,
                              size.height);
          },
  };

  auto* surf = gdk_wayland_window_get_wl_surface(GetToplevelGdkWindow());
  MOZ_DIAGNOSTIC_ASSERT(surf, "Should have a wayland surface by now");
  mPipResources.mXdgSurface =
      xdg_wm_base_get_xdg_surface(WaylandDisplayGet()->GetXdgWm(), surf);
  xdg_surface_add_listener(mPipResources.mXdgSurface, &surface_listener, this);
  mPipResources.mPipSurface = xx_pip_shell_v1_get_pip(
      WaylandDisplayGet()->GetPipShell(), mPipResources.mXdgSurface);
  xx_pip_v1_add_listener(mPipResources.mPipSurface, &pip_listener, this);

  const char* appId = !mGtkWindowAppClass.IsEmpty() ? mGtkWindowAppClass.get()
                                                    : gdk_get_program_class();
  xx_pip_v1_set_app_id(mPipResources.mPipSurface, appId);

  wl_surface_commit(surf);
}

void nsWindowWaylandToplevelGtk::ClearPipResources() {
  MozClearPointer(mPipResources.mPipSurface, xx_pip_v1_destroy);
  MozClearPointer(mPipResources.mXdgSurface, xdg_surface_destroy);
}

bool nsWindowWaylandToplevelGtk::PIPMove() {
  if (!mPipResources.mPipSurface) {
    return false;
  }
  xx_pip_v1_move(mPipResources.mPipSurface,
                 gdk_wayland_device_get_wl_seat(GdkGetPointer()),
                 nsWaylandDisplay::GetLastEventSerial());
  return true;
}

bool nsWindowWaylandToplevelGtk::PIPResize(GdkWindowEdge aEdge) {
  if (!mPipResources.mPipSurface) {
    return false;
  }
  auto pipEdges = [&] {
    switch (aEdge) {
      case GDK_WINDOW_EDGE_NORTH:
        return XX_PIP_V1_RESIZE_EDGE_TOP;
      case GDK_WINDOW_EDGE_NORTH_WEST:
        return XX_PIP_V1_RESIZE_EDGE_TOP_LEFT;
      case GDK_WINDOW_EDGE_NORTH_EAST:
        return XX_PIP_V1_RESIZE_EDGE_TOP_RIGHT;
      case GDK_WINDOW_EDGE_SOUTH:
        return XX_PIP_V1_RESIZE_EDGE_BOTTOM;
      case GDK_WINDOW_EDGE_SOUTH_WEST:
        return XX_PIP_V1_RESIZE_EDGE_BOTTOM_LEFT;
      case GDK_WINDOW_EDGE_SOUTH_EAST:
        return XX_PIP_V1_RESIZE_EDGE_BOTTOM_RIGHT;
      case GDK_WINDOW_EDGE_WEST:
        return XX_PIP_V1_RESIZE_EDGE_LEFT;
      case GDK_WINDOW_EDGE_EAST:
        return XX_PIP_V1_RESIZE_EDGE_RIGHT;
    }
    return XX_PIP_V1_RESIZE_EDGE_TOP_LEFT;
  }();
  xx_pip_v1_resize(mPipResources.mPipSurface,
                   gdk_wayland_device_get_wl_seat(GdkGetPointer()),
                   nsWaylandDisplay::GetLastEventSerial(), pipEdges);
  return true;
}

void nsWindowWaylandToplevelGtk::CreateNative() {
  LOG("nsWindowWaylandToplevelGtk::CreateNative()");

  // Ensure that KeymapWrapper is created on Wayland as we need it for
  // keyboard focus tracking.
  KeymapWrapper::EnsureInstance();

  mSurfaceProvider.Initialize(this);

  // Initialize the window specific VsyncSource early in order to avoid races
  // with BrowserParent::UpdateVsyncParentVsyncDispatcher().
  // Only use for toplevel windows for now, see bug 1619246.

  if (StaticPrefs::widget_wayland_vsync_enabled_AtStartup() &&
      IsTopLevelWidget()) {
    LOG_VSYNC("  create WaylandVsyncSource");
    mWaylandVsyncSource = new WaylandVsyncSource(this);
    mWaylandVsyncSource->Init();
    mWaylandVsyncDispatcher = new VsyncDispatcher(mWaylandVsyncSource);
    mWaylandVsyncSource->EnableVSyncSource();
  }

  if (IsPipWindow()) {
    // This avoids GTK from managing our top level surface role.
    // We'll handle it manually in OnMap().
    gdk_wayland_window_set_use_custom_surface(GetToplevelGdkWindow());
  }

  mWaitingToSessionRestore =
      IsTopLevelWidget() &&
      nsAppShell::UpdateAndGetSessionState() == eSessionRestoring;

  LOGVERBOSE("  mWaitingToSessionRestore %d", mWaitingToSessionRestore);
}

void nsWindowWaylandToplevelGtk::ConfigureToplevelWindowNative() {
  LOG("nsWindowWaylandToplevelGtk::ConfigureToplevelWindow() register callback "
      "for toplevel GdkWindow [%p]",
      GetToplevelGdkWindow());
  if (!mWaitingToSessionRestore) {
    return;
  }
  auto* window = GetToplevelGdkWindow();
  if (!window) {
    LOG("  quit, missing toplevel GdkWindow!");
    return;
  }
  MOZ_ASSERT(g_signal_lookup("xdg-toplevel-realized", G_OBJECT_TYPE(window)),
             "Session restore support shouldbe disabled!");
  if (g_signal_handler_is_connected(window, mXdgToplevelRealizedID)) {
    return;
  }
  mXdgToplevelRealizedID = g_signal_connect(
      window, "xdg-toplevel-realized",
      G_CALLBACK(+[](GtkWidget* widget) -> gboolean {
        LOGW("nsWindowWaylandToplevelGtk::ConfigureToplevelWindow() callback");
        RefPtr<nsWindowWaylandToplevelGtk> window =
            static_cast<nsWindowWaylandToplevelGtk*>(
                nsWindow::FromGtkWidget(widget));
        if (!window) {
          return FALSE;
        }

        g_signal_handler_disconnect(window->GetToplevelGdkWindow(),
                                    window->mXdgToplevelRealizedID);
        window->mXdgToplevelRealizedID = 0;

        window->RestoreXdgToplevel();
        return FALSE;
      }),
      nullptr);
}

void nsWindowWaylandToplevelGtk::DestroyNative() {
  LOG("nsWindowWaylandToplevelGtk::DestroyNative()");
  if (mXdgToplevelRealizedID) {
    g_signal_handler_disconnect(GetToplevelGdkWindow(), mXdgToplevelRealizedID);
    mXdgToplevelRealizedID = 0;
  }
  MozClearPointer(mSessionRestoreToken, xdg_toplevel_session_v1_destroy);
  ClearPipResources();

  // Shut down our local vsync source
  // Also drops reference to nsWindow::mSurface.
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->Shutdown();
    mWaylandVsyncSource = nullptr;
  }
  mWaylandVsyncDispatcher = nullptr;
  UnlockNativePointer();
}

void nsWindowWaylandToplevelGtk::NativeShow(bool aAction) {
  if (aAction) {
    mNeedsShow = true;
    if (mWaitingToSessionRestore) {
      LOG("nsWindowWaylandToplevelGtk::NativeShow() waiting to session "
          "restore, "
          "quit.");
      if (nsAppShell::UpdateAndGetSessionState() == eSessionRestoring) {
        return;
      }
      mWaitingToSessionRestore = false;
      NS_WARNING("Wayland session restore failed!");
    }

    LOG("nsWindowWaylandToplevelGtk::NativeShow show\n");

    gtk_widget_show(mShell);
    SetUserTimeAndStartupTokenForActivatedWindow();

    auto token = std::move(mWindowActivationTokenFromEnv);
    if (!token.IsEmpty()) {
      FocusWaylandWindow(token.get());
    } else {
      TransferFocusTo();
    }
    mNeedsShow = false;
  } else {
    LOG("nsWindowWaylandToplevelGtk::NativeShow hide\n");
    // Close any popups left open on top of us before we disappear.
    HideAndRemoveAppPopups();
    gtk_widget_hide(mShell);
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
bool nsWindowWaylandToplevelGtk::ApplyEnterLeaveMutterWorkaround() {
  // Leave (toplevel) case
  auto* firstPopup = GetFirstPopup();
  if (mWindowType == WindowType::TopLevel && firstPopup &&
      !firstPopup->IsLastPopup() &&
      gtk_window_get_type_hint(GTK_WINDOW(firstPopup->GetGtkWidget())) ==
          GDK_WINDOW_TYPE_HINT_UTILITY) {
    LOG("nsWindowWaylandToplevelGtk::ApplyEnterLeaveMutterWorkaround(): leave "
        "toplevel");
    return true;
  }
  return false;
}

void nsWindowWaylandToplevelGtk::OnMapNative() { MaybeCreatePipResources(); }

void nsWindowWaylandToplevelGtk::HideAndRemoveAppPopups() {
  // Hide back-to-front so child popups close before their parents.
  auto* popup = FindLastWindow(this)->AsWaylandPopup();
  while (popup) {
    auto* prev = popup->GetPrevPopup();
    popup->HidePopupWindow(/* aTemporaryHide */ false,
                           /* aRemoveFromPopupList */ true);
    popup = prev;
  }
}

// ----------------------------------------------------------------------
// nsWindowWaylandPopupGtk
// ----------------------------------------------------------------------

nsWindowWaylandPopupGtk::nsWindowWaylandPopupGtk()
    : mWaylandApplyPopupPositionBeforeShow(true) {}

void nsWindowWaylandPopupGtk::CreateNative() {
  LOG("nsWindowWaylandPopupGtk::CreateNative()");

  // Ensure that KeymapWrapper is created on Wayland as we need it for
  // keyboard focus tracking.
  KeymapWrapper::EnsureInstance();

  mSurfaceProvider.Initialize(this);
}

void nsWindowWaylandPopupGtk::DestroyNative() {
  LOG("nsWindowWaylandPopupGtk::DestroyNative()");
  UnlockNativePointer();
}

bool nsWindowWaylandPopupGtk::WaylandPopupAnchorAdjustForParentPopup(
    GdkRectangle* aPopupAnchor, GdkPoint* aOffset) {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupAnchorAdjustForParentPopup");

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

bool nsWindowWaylandPopupGtk::WaylandPopupCheckAndGetAnchor(
    GdkRectangle* aPopupAnchor, GdkPoint* aOffset) {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupCheckAndGetAnchor");

  GdkWindow* gdkWindow = GetToplevelGdkWindow();
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!gdkWindow || !popupFrame) {
    LOG("  can't use move-to-rect due missing gdkWindow or popupFrame");
    return false;
  }

  if (popupFrame->IsConstrainedByLayout()) {
    LOG("  can't use move-to-rect, flipped / constrained by layout");
    return false;
  }

  if (!mPopupMoveToRectParams.mAnchorSet) {
    mPopupMoveToRectParams = WaylandPopupGetPositionFromLayout();
  }

  // Update popup layout coordinates from layout by recent popup hierarchy
  // (calculate correct position according to parent window)
  // and convert to Gtk coordinates.
  DesktopIntRect anchorRect =
      ToDesktopPixels(mPopupMoveToRectParams.mAnchorRect);
  if (!WaylandPopupIsFirst()) {
    DesktopIntPoint parent = WaylandGetParentPosition();
    LOG("  subtract parent position from anchor [%d, %d]\n", parent.x.value,
        parent.y.value);
    anchorRect.MoveBy(-parent);
  }

  *aPopupAnchor = GdkRectangle{anchorRect.x, anchorRect.y, anchorRect.width,
                               anchorRect.height};
  LOG("  anchored to rectangle [%d, %d] -> [%d x %d]", aPopupAnchor->x,
      aPopupAnchor->y, aPopupAnchor->width, aPopupAnchor->height);

  if (!WaylandPopupAnchorAdjustForParentPopup(aPopupAnchor, aOffset)) {
    LOG("  can't use move-to-rect, anchor is not placed inside of parent "
        "window");
    return false;
  }

  return true;
}

void nsWindowWaylandPopupGtk::WaylandPopupPrepareForMove() {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupPrepareForMove()");

  if (mPopupType == PopupType::Tooltip) {
    // Don't fiddle with tooltips type, just hide it before move-to-rect
    if (mPopupUseMoveToRect && gtk_widget_is_visible(mShell)) {
      HidePopupWindow(/* aTemporaryHide */ true,
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
    HidePopupWindow(/* aTemporaryHide */ true,
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
void nsWindowWaylandPopupGtk::WaylandPopupMovePlain(int aX, int aY) {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupMovePlain(%d, %d)", aX, aY);

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
  MOZ_DIAGNOSTIC_ASSERT(nsWindow::FromGdkWindow(window), "Missing nsWindow!");
  MOZ_DIAGNOSTIC_ASSERT(nsWindow::FromGdkWindow(window)->AsWaylandPopup(),
                        "Not a Wayland popup!");
  static_cast<nsWindowWaylandPopupGtk*>(
      nsWindow::FromGdkWindow(window)->AsWaylandPopup())
      ->NativeMoveResizeWaylandPopupCallback(final_rect, flipped_x, flipped_y);
}

void nsWindowWaylandPopupGtk::WaylandPopupMoveImpl() {
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

  LOG("nsWindowWaylandPopupGtk::WaylandPopupMove");
  LOG("  popup use move to rect %d", mPopupUseMoveToRect);

  WaylandPopupPrepareForMove();

  if (!mPopupUseMoveToRect) {
    auto pos = mLastMoveRequest - WaylandGetParentPosition();
    WaylandPopupMovePlain(pos.x, pos.y);
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

// Wayland keeps strong popup window hierarchy. We need to track active
// (visible) popup windows and make sure we hide popup on the same level
// before we open another one on that level. It means that every open
// popup needs to have an unique parent.
void nsWindowWaylandPopupGtk::UpdateWaylandPopupHierarchy() {
  LOG("nsWindowWaylandPopupGtk::UpdateWaylandPopupHierarchy\n");

  // This popup hasn't been added to popup hierarchy yet so no need to
  // do any configurations.
  if (!IsInPopupHierarchy()) {
    LOG("  popup isn't in hierarchy\n");
    return;
  }

  nsWindowWaylandPopupGtk* firstPopup =
      PopupAsGtk(mWaylandToplevel->GetFirstPopup());
  MOZ_DIAGNOSTIC_ASSERT(firstPopup, "In hierarchy without a first popup?");
  if (!firstPopup) {
    return;
  }

#ifdef MOZ_LOGGING
  LogPopupHierarchy(firstPopup);
  auto printPopupHierarchy =
      MakeScopeExit([&] { LogPopupHierarchy(firstPopup); });
#endif

  // Hide all tooltips without the last one. Tooltip can't be popup parent.
  firstPopup->WaylandPopupHideTooltips();

  // See Bug 1709254 / https://gitlab.gnome.org/GNOME/gtk/-/issues/5092
  // It's possible that Wayland compositor refuses to show
  // a popup although Gtk claims it's visible.
  // We don't know if the popup is shown or not.
  // To avoid application crash refuse to create any child of such invisible
  // popup and close any child of it now.
  firstPopup->WaylandPopupCloseOrphanedPopups();

  // Check if we have any remote content / overflow window in hierarchy.
  // We can't attach such widget on top of other popup.
  firstPopup->CloseAllPopupsBeforeRemotePopup();

  // Check if your popup hierarchy matches layout hierarchy.
  // For instance we should not connect hamburger menu on top
  // of context menu.
  // Close all popups from different layout chains if possible.
  AutoTArray<nsIWidget*, 5> layoutPopupWidgetChain;
  GetLayoutPopupWidgetChain(&layoutPopupWidgetChain);

  firstPopup->WaylandPopupHierarchyHideByLayout(&layoutPopupWidgetChain);
  firstPopup->WaylandPopupHierarchyValidateByLayout(&layoutPopupWidgetChain);

  // Now we have Popup hierarchy complete.
  // Find first unchanged (and still open) popup to start with hierarchy
  // changes.
  nsWindowWaylandPopupGtk* changedPopup = firstPopup;
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
    changedPopup = PopupAsGtk(changedPopup->GetNextPopup());
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

  // May be the toplevel window when changedPopup is the first popup.
  nsWindowWayland* parentOfChangedPopup = nullptr;
  if (changedPopup->mPopupClosed) {
    parentOfChangedPopup = changedPopup->GetPrevWindow();
  }
  changedPopup->RemoveClosedPopups();

  // There's nothing left, just quit
  firstPopup = PopupAsGtk(mWaylandToplevel->GetFirstPopup());
  if (!firstPopup) {
    return;
  }

  // It's possible that changedPopup was removed from widget hierarchy,
  // in such case use child popup of the removed one if there's any.
  if (!changedPopup->IsInPopupHierarchy()) {
    if (!parentOfChangedPopup || !parentOfChangedPopup->GetNextWindow()) {
      LOG("  last popup was removed, quit.\n");
      return;
    }
    changedPopup = static_cast<nsWindowWaylandPopupGtk*>(
        parentOfChangedPopup->GetNextWindow()->AsWaylandPopup());
  }

  GetLayoutPopupWidgetChain(&layoutPopupWidgetChain);
  firstPopup->WaylandPopupHierarchyValidateByLayout(&layoutPopupWidgetChain);

  changedPopup->WaylandPopupHierarchyCalculatePositions();

  nsWindowWaylandPopupGtk* popup = changedPopup;
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
      nsWindowWaylandPopupGtk* parentPopup = PopupAsGtk(popup->GetPrevPopup());
      if (parentPopup && !parentPopup->WaylandPopupIsFirst() &&
          !parentPopup->mPopupUseMoveToRect) {
        // We can't use move-to-rect if there are more parents of
        // wl_subsurface popups types.
        //
        // It's because wl_subsurface is ignored by xdg_popup
        // (created by move-to-rect) so our popup scenario:
        //
        // toplevel -> xdg_popup(1) -> wl_subsurface(2) -> xdg_popup(3)
        //
        // looks for Wayland compositor as:
        //
        // toplevel -> xdg_popup(1) -> xdg_popup(3)
        //
        // If xdg_popup(1) and xdg_popup(3) are not connected
        // move-to-rect applied to xdg_popup(3) fails and we get missing popup.
        return false;
      }
      return true;
    }();

    popup->mPopupUseMoveToRect = useMoveToRect;

    LOG("  popup [%p] matches layout [%d] anchored [%d] first popup [%d] use "
        "move-to-rect %d\n",
        popup, popup->mPopupMatchesLayout, popup->mPopupAnchored,
        popup->WaylandPopupIsFirst(), popup->mPopupUseMoveToRect);

    if (popup->mPopupUseMoveToRect && !popup->mPopupMatchesLayout) {
      gfxCriticalNote << "Wayland: Positioned popup with missing anchor!";
    }

    popup->WaylandPopupMoveImpl();
    popup->mPopupChanged = false;
    popup = PopupAsGtk(popup->GetNextPopup());
  }

  changedPopup->WaylandPopupHierarchyShowTemporaryHidden();
}

// Gtk refuses to map popup window with x < 0 && y < 0 relative coordinates
// see https://gitlab.gnome.org/GNOME/gtk/-/issues/4071
// as a workaround just fool around and place the popup temporary to 0,0.
bool nsWindowWaylandPopupGtk::WaylandPopupRemoveNegativePosition(int* aX,
                                                                 int* aY) {
  // https://gitlab.gnome.org/GNOME/gtk/-/issues/4071 applies to temporary
  // windows only
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window || gdk_window_get_window_type(window) != GDK_WINDOW_TEMP) {
    return false;
  }

  LOG("nsWindowWaylandPopupGtk::WaylandPopupRemoveNegativePosition()");

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

void nsWindowWaylandPopupGtk::ShowWaylandPopupWindow() {
  LOG("nsWindowWaylandPopupGtk::ShowWaylandPopupWindow. Expected to see "
      "visible.");
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

// Hide and potentially removes popup from popup hierarchy.
void nsWindowWaylandPopupGtk::HidePopupWindow(bool aTemporaryHide,
                                              bool aRemoveFromPopupList) {
  LOG("nsWindowWaylandPopupGtk::HidePopupWindow: remove from list %d\n",
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
  }
}

void nsWindowWaylandPopupGtk::NativeShow(bool aAction) {
  if (aAction) {
    // unset our flag now that our window has been shown
    mNeedsShow = true;
    auto removeShow = MakeScopeExit([&] { mNeedsShow = false; });

    LOG("nsWindowWaylandPopupGtk::NativeShow show\n");

    mPopupClosed = false;
    const bool trackedInHierarchy = WaylandPopupConfigure();
    if (trackedInHierarchy) {
      AddWindowToPopupHierarchy();
    }
    if (mWaylandApplyPopupPositionBeforeShow) {
      // NOTE(emilio): This will end up calling UpdateWaylandPopupHierarchy if
      // needed.
      NativeMoveResize(/* move */ true, /* resize */ false);
    } else if (trackedInHierarchy) {
      UpdateWaylandPopupHierarchy();
    }
    if (mPopupClosed) {
      return;
    }
    ShowWaylandPopupWindow();
  } else {
    LOG("nsWindowWaylandPopupGtk::NativeShow hide\n");
    // We can't close tracked popups directly as they may have visible
    // child popups. Just mark is as closed and let
    // UpdateWaylandPopupHierarchy() do the job.
    if (IsInPopupHierarchy()) {
      WaylandPopupMarkAsClosed();
      UpdateWaylandPopupHierarchy();
    } else {
      // Close untracked popups directly.
      HidePopupWindow(/* aTemporaryHide */ false,
                      /* aRemoveFromPopupList */ true);
    }
  }
}

// Configure Wayland popup. If true is returned we need to track popup
// in popup hierarchy. Otherwise we just show it as is.
bool nsWindowWaylandPopupGtk::WaylandPopupConfigure() {
  if (mIsDragPopup) {
    return false;
  }

  // Don't track popups without frame
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
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

  LOG("nsWindowWaylandPopupGtk::WaylandPopupConfigure tracked %d anchored %d "
      "hint %d permanent %d\n",
      mPopupTrackInHierarchy, mPopupAnchored, int(mPopupType),
      WaylandPopupIsPermanent());

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

// Hide popups which are not in popup chain.
void nsWindowWaylandPopupGtk::WaylandPopupHierarchyHideByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupHierarchyHideByLayout");
  MOZ_ASSERT(WaylandPopupIsFirst(), "Should be called on the first popup!");

  // Hide all popups which are not in layout popup chain
  nsWindowWaylandPopupGtk* popup = this;
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
    popup = PopupAsGtk(popup->GetNextPopup());
  }
}

void nsWindowWaylandPopupGtk::WaylandPopupHierarchyHideTemporary() {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupHierarchyHideTemporary()");
  nsWindowWaylandPopup* popup = FindLastWindow(this)->AsWaylandPopup();
  while (popup && popup != this) {
    LOG("  temporary hidding popup [%p]", popup);
    nsWindowWaylandPopup* prev = popup->GetPrevPopup();
    popup->HidePopupWindow(/* aTemporaryHide */ true,
                           /* aRemoveFromPopupList */ false);
    popup = prev;
  }
}

void nsWindowWaylandPopupGtk::WaylandPopupHierarchyShowTemporaryHidden() {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupHierarchyShowTemporaryHidden()");
  nsWindowWaylandPopupGtk* popup = this;
  while (popup) {
    if (popup->mPopupTemporaryHidden) {
      popup->mPopupTemporaryHidden = false;
      LOG("  showing temporary hidden popup [%p]", popup);
      popup->ShowWaylandPopupWindow();
    }
    popup = PopupAsGtk(popup->GetNextPopup());
  }
}

void nsWindowWaylandPopupGtk::WaylandPopupHierarchyCalculatePositions() {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupHierarchyCalculatePositions()");

  // Set widget hierarchy in Gtk
  nsWindowWaylandPopupGtk* popup =
      PopupAsGtk(mWaylandToplevel->GetFirstPopup());
  while (popup) {
    LOG("  popup [%p] set parent window [%p]", (void*)popup,
        (void*)popup->mWaylandWindowPrev.get());
    GtkWindowSetTransientFor(
        GTK_WINDOW(popup->mShell),
        GTK_WINDOW(popup->mWaylandWindowPrev->GetGtkWidget()));
    popup = PopupAsGtk(popup->GetNextPopup());
  }
}

DesktopIntPoint nsWindowWaylandPopupGtk::WaylandGetParentPosition() const {
  MOZ_ASSERT(IsPopup());
  auto* window = GetEffectiveParent();
  if (NS_WARN_IF(!window) || !window->IsPopup()) {
    return {0, 0};
  }
  // If our parent is a popup, offset to our toplevel bounds (note that in
  // Wayland there's no global coordinate space, so our "screen" offset is
  // really relative to the origin of our toplevel).
  DesktopIntPoint offset = window->WidgetToScreenOffsetUnscaled();
  LOG("nsWindowWaylandPopupGtk::WaylandGetParentPosition() [%d, %d]\n",
      offset.x.value, offset.y.value);
  return offset;
}

void nsWindowWaylandPopupGtk::NativeMoveResizeWaylandPopupCallback(
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
    DesktopIntPoint parent = WaylandGetParentPosition();
    finalRect.x += parent.x;
    finalRect.y += parent.y;
    return finalRect;
  }();

  // With fractional scaling, our devPx->Gdk->devPx conversion might not
  // perfectly round-trip. Compare gdk rects to check whether size or positions
  // have changed from what we'd request otherwise, in order to avoid
  // flickering.
  const auto currentRect = mClientArea;
  auto scale = GdkCeiledScaleFactor();
  auto IsSubstantiallyDifferent = [=](gint a, gint b) {
    return std::abs(a - b) > scale;
  };

  const bool needsPositionUpdate =
      IsSubstantiallyDifferent(finalGdkRect.x, currentRect.x) ||
      IsSubstantiallyDifferent(finalGdkRect.y, currentRect.y);
  const bool needsSizeUpdate =
      IsSubstantiallyDifferent(finalGdkRect.width, currentRect.width) ||
      IsSubstantiallyDifferent(finalGdkRect.height, currentRect.height);
  const DesktopIntRect newClientArea = DesktopIntRect(
      finalGdkRect.x, finalGdkRect.y, finalGdkRect.width, finalGdkRect.height);

  LOG("  orig gdk [%d, %d] -> [%d x %d]", currentRect.x, currentRect.y,
      currentRect.width, currentRect.height);
  LOG("  new gdk [%d, %d] -> [%d x %d]\n", finalGdkRect.x, finalGdkRect.y,
      finalGdkRect.width, finalGdkRect.height);
  LOG("  new mClientArea [%d, %d] -> [%d x %d]", newClientArea.x,
      newClientArea.y, newClientArea.width, newClientArea.height);

  if (!needsSizeUpdate && !needsPositionUpdate) {
    LOG("  Size/position is the same, quit.");
    return;
  }
  if (needsSizeUpdate) {
    // Wayland compositor changed popup size request from layout.
    // Set the constraints to use them in nsMenuPopupFrame::SetPopupPosition().
    // Beware that gtk_window_resize() requests sizes asynchronously and so
    // newClientArea might not have the size from the most recent
    // gtk_window_resize().
    if (newClientArea.width < mLastSizeRequest.width) {
      mMoveToRectPopupSize.width = newClientArea.width;
    }
    if (newClientArea.height < mLastSizeRequest.height) {
      mMoveToRectPopupSize.height = newClientArea.height;
    }
    LOG("  mMoveToRectPopupSize set to [%d, %d]", mMoveToRectPopupSize.width,
        mMoveToRectPopupSize.height);
  }

  mClientArea = newClientArea;
  mLastSizeRequest = newClientArea.Size();
  mLastMoveRequest = newClientArea.TopLeft();

  // Check mClientArea size
  auto scaledSize = ToLayoutDevicePixels(mClientArea);
  if (mCompositorSession &&
      !wr::WindowSizeSanityCheck(scaledSize.width, scaledSize.height)) {
    gfxCriticalNoteOnce << "Invalid mClientArea in PopupCallback " << scaledSize
                        << " size state " << mSizeMode;
  }
  WaylandPopupPropagateChangesToLayout(needsPositionUpdate, needsSizeUpdate);
}

// Position the popup directly by gtk_window_move() and try to keep it
// on screen by just moving it in scope of it's parent window.
//
// It's used when we position noautihode popup and we don't use xdg_positioner.
// See Bug 1718867
void nsWindowWaylandPopupGtk::WaylandPopupSetDirectPosition() {
  const DesktopIntRect newRect(mLastMoveRequest, mLastSizeRequest);

  LOG("nsWindowWaylandPopupGtk::WaylandPopupSetDirectPosition %s",
      ToString(newRect).c_str());

  mClientArea = newRect;

  if (mIsDragPopup) {
    gtk_window_move(GTK_WINDOW(mShell), newRect.x, newRect.y);
    gtk_window_resize(GTK_WINDOW(mShell), newRect.width, newRect.height);
    // DND window is placed inside container so we need to make hard size
    // request to ensure parent container is resized too.
    gtk_widget_set_size_request(GTK_WIDGET(mShell), newRect.width,
                                newRect.height);
    return;
  }

  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  auto* window = nsWindow::FromGtkWidget(GTK_WIDGET(parentGtkWindow));
  if (!window) {
    return;
  }
  GdkWindow* gdkWindow = window->GetGdkWindow();
  if (!gdkWindow) {
    return;
  }

  int parentWidth = gdk_window_get_width(gdkWindow);
  int popupWidth = newRect.width;

  int x;
  gdk_window_get_position(gdkWindow, &x, nullptr);

  auto pos = newRect.TopLeft();
  // If popup is bigger than main window just center it.
  if (popupWidth > parentWidth) {
    pos.x = -(parentWidth - popupWidth) / 2 + x;
  } else {
    if (pos.x < x) {
      // Stick with left window edge if it's placed too left
      pos.x = x;
    } else if (pos.x + popupWidth > parentWidth + x) {
      // Stick with right window edge otherwise
      pos.x = parentWidth + x - popupWidth;
    }
  }

  LOG("  set position [%d, %d]\n", pos.x.value, pos.y.value);
  gtk_window_move(GTK_WINDOW(mShell), pos.x, pos.y);

  LOG("  set size [%d, %d]\n", newRect.width, newRect.height);
  gtk_window_resize(GTK_WINDOW(mShell), newRect.width, newRect.height);

  if (pos.x != newRect.x) {
    mClientArea.MoveTo(pos);
    WaylandPopupPropagateChangesToLayout(/* move */ true, /* resize */ false);
  }
}

bool nsWindowWaylandPopupGtk::WaylandPopupFitsToplevelWindow() {
  LOG("nsWindowWaylandPopupGtk::WaylandPopupFitsToplevelWindow()");

  GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  GtkWindow* tmp = parent;
  while ((tmp = gtk_window_get_transient_for(GTK_WINDOW(parent)))) {
    parent = tmp;
  }
  GdkWindow* toplevelGdkWindow = gtk_widget_get_window(GTK_WIDGET(parent));
  if (NS_WARN_IF(!toplevelGdkWindow)) {
    return false;
  }

  int parentWidth = gdk_window_get_width(toplevelGdkWindow);
  int parentHeight = gdk_window_get_height(toplevelGdkWindow);
  DesktopIntRect parentWidgetRect(0, 0, parentWidth, parentHeight);

  nsWindowWayland* parentWindow = nullptr;
  if (auto* window = nsWindow::FromGtkWidget(GTK_WIDGET(parent))) {
    parentWindow = window->AsWayland();
  }
  if (!parentWindow) {
    return false;
  }

  DesktopIntRect parentClientArea = parentWindow->GetClientArea();
  LOG("  parent size %d x %d", parentClientArea.width, parentClientArea.height);

  DesktopIntRect popupRect(mLastMoveRequest, mLastSizeRequest);
  LOG("  popup topleft %d, %d size %d x %d", popupRect.x, popupRect.y,
      popupRect.width, popupRect.height);

  bool fits = parentClientArea.Contains(popupRect);
  LOG("  fits %d", fits);
  return fits;
}

void nsWindowWaylandPopupGtk::NativeMoveResizeWaylandPopup(bool aMove,
                                                           bool aResize) {
  GdkRectangle rect{mLastMoveRequest.x, mLastMoveRequest.y,
                    mLastSizeRequest.width, mLastSizeRequest.height};

  LOG("nsWindowWaylandPopupGtk::NativeMoveResizeWaylandPopup [%d,%d] -> [%d x "
      "%d] "
      "move %d resize %d\n",
      rect.x, rect.y, rect.width, rect.height, aMove, aResize);

  // Compositor may be confused by windows with width/height = 0
  // and positioning such windows leads to Bug 1555866.
  if (!AreBoundsSane()) {
    LOG("  Bounds are not sane (width: %d height: %d)\n",
        mLastSizeRequest.width, mLastSizeRequest.height);
    return;
  }

  // It's safe to expect the popup position is handled onwards.
  if (aMove) {
    mWaylandApplyPopupPositionBeforeShow = false;
  }

  MOZ_ASSERT(mClientMargin.IsAllZero());

  if (mWaitingForMoveToRectCallback) {
    LOG("  waiting for move to rect, scheduling");
    // mClientArea position must not be overwritten before it is applied.
    // OnShellConfigureEvent() will not set mClientArea to an old position for
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
  // Read popup position from layout if it was moved.
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

  if (!aMove && WaylandPopupFitsToplevelWindow()) {
    // Popup position has not been changed and its position/size fits
    // parent window so no need to reposition the window.
    LOG("  fits parent window size, just resize\n");
    return;
  }

  // Mark popup as changed as we're updating position/size.
  mPopupChanged = true;

  mClientArea = DesktopIntRect(mLastMoveRequest, mLastSizeRequest);

  UpdateWaylandPopupHierarchy();
}

bool nsWindowWaylandPopupGtk::ApplyEnterLeaveMutterWorkaround() {
  // Leave (popup) case; see
  // nsWindowWaylandToplevelGtk::ApplyEnterLeaveMutterWorkaround() for the
  // toplevel case and more context on this workaround.
  if (mWaylandWindowNext && gtk_window_get_type_hint(GTK_WINDOW(mShell)) ==
                                GDK_WINDOW_TYPE_HINT_UTILITY) {
    LOG("nsWindowWaylandPopupGtk::ApplyEnterLeaveMutterWorkaround(): leave "
        "popup");
    return true;
  }
  return false;
}
