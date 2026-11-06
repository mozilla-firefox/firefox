/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * MozContainerWayland is a wrapper over MozContainer which manages
 * WaylandSurface for nsWindow.
 *
 * The widget scheme looks like:
 *
 *   ---------------------------------------------------------
 *  |  mShell Gtk widget (contains wl_surface owned by Gtk+)  |
 *  |                                                         |
 *  |  ---------------------------------------------------    |
 *  | | mContainer (contains wl_surface owned by Gtk+)    |   |
 *  | |                                                   |   |
 *  | |  ---------------------------------------------    |   |
 *  | | | wl_subsurface (owned by WaylandSurface)     |   |   |
 *  | | |                                             |   |   |
 *  | | |                                             |   |   |
 *  | | |                                             |   |   |
 *  | |  ---------------------------------------------    |   |
 *  |  ---------------------------------------------------    |
 *   ---------------------------------------------------------
 *
 *  We draw to wl_subsurface managed by WaylandSurface/MozContainerWayland.
 *  We need to wait until wl_surface of mContainer is created
 *  and then we create and attach our wl_subsurface to it.
 *
 *  First wl_subsurface creation has these steps:
 *
 *  1) moz_container_wayland_size_allocate() handler is called when
 *     mContainer size/position is known.
 *     It calls moz_container_wayland_surface_create_locked(), registers
 *     a frame callback handler
 *     moz_container_wayland_frame_callback_handler().
 *
 *  2) moz_container_wayland_frame_callback_handler() is called
 *     when wl_surface owned by mozContainer is ready.
 *     We call initial_draw_cbs() handler and we can create our wl_subsurface
 *     on top of wl_surface owned by mozContainer.
 *
 *  When MozContainer hides/show again, moz_container_wayland_size_allocate()
 *  handler may not be called as MozContainer size is set. So after first
 *  show/hide sequence use moz_container_wayland_map_event() to create
 *  wl_subsurface of MozContainer.
 */

#include <dlfcn.h>
#include <glib.h>
#include <stdio.h>
#include <wayland-egl.h>

#include "MozContainer.h"
#include "base/task.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/gfx/gfxVars.h"
#include "nsGtkUtils.h"
#include "nsWaylandDisplay.h"
#include "nsWindow.h"
#include "nsWindowWayland.h"

#undef LOGWAYLAND
#undef LOGCONTAINER
#ifdef MOZ_LOGGING
#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
extern mozilla::LazyLogModule gWidgetWaylandLog;
extern mozilla::LazyLogModule gWidgetLog;
#  define LOGWAYLAND(...) \
    MOZ_LOG(gWidgetWaylandLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#  define LOGCONTAINER(...) \
    MOZ_LOG(gWidgetLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#else
#  define LOGWAYLAND(...)
#  define LOGCONTAINER(...)
#endif /* MOZ_LOGGING */

using namespace mozilla;
using namespace mozilla::widget;

static bool moz_container_wayland_ensure_surface(
    MozContainer* container, DesktopIntPoint* aPosition = nullptr);

// Invalidate gtk wl_surface to commit changes to wl_subsurface.
// wl_subsurface changes are effective when parent surface is commited.
static void moz_container_wayland_invalidate(MozContainer* container) {
  LOGWAYLAND("moz_container_wayland_invalidate [%p]\n",
             (void*)moz_container_get_nsWindow(container));

  nsWindow* window = moz_container_get_nsWindow(container);
  MOZ_RELEASE_ASSERT(window);

  window->AsWayland()->ForceToplevelCommit();
}

void moz_container_wayland_unmap(GtkWidget* widget) {
  g_return_if_fail(IS_MOZ_CONTAINER(widget));

  // Unmap MozContainer first so we can remove our resources
  moz_container_unmap(widget);

  LOGCONTAINER("%s [%p]\n", __FUNCTION__,
               (void*)moz_container_get_nsWindow(MOZ_CONTAINER(widget)));

  WaylandSurface* surface = MOZ_WL_SURFACE(MOZ_CONTAINER(widget));
  // MozContainer map/unmap is processed on main thread only
  // so we don't need to lock WaylandSurface here.
  if (surface->IsMapped()) {
    surface->RunUnmapCallback();
  }

  WaylandSurfaceLock lock(surface);
  if (surface->IsPendingGdkCleanup()) {
    surface->GdkCleanUpLocked(lock);
  }
  surface->UnmapLocked(lock);
}

gboolean moz_container_wayland_map_event(GtkWidget* widget,
                                         GdkEventAny* event) {
  LOGCONTAINER("%s [%p]\n", __FUNCTION__,
               (void*)moz_container_get_nsWindow(MOZ_CONTAINER(widget)));

  // Return early if we're not mapped. Gtk may send bogus map_event signal
  // to unmapped widgets (see Bug 1875369).
  if (!gtk_widget_get_mapped(widget)) {
    return false;
  }

  // Make sure we're on main thread as we can't lock mozContainer here
  // due to moz_container_wayland_add_or_fire_initial_draw_callback() call
  // below.
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());

  // Don't create wl_subsurface in map_event when it's already created or
  // if we create it for the first time.
  MozContainer* container = MOZ_CONTAINER(widget);
  if (MOZ_WL_SURFACE(container)->IsMapped() ||
      MOZ_WL_CONTAINER(container)->before_first_size_alloc) {
    return false;
  }

  return moz_container_wayland_ensure_surface(container);
}

void moz_container_wayland_size_allocate(GtkWidget* widget,
                                         GtkAllocation* allocation) {
  GtkAllocation tmp_allocation;

  g_return_if_fail(IS_MOZ_CONTAINER(widget));

  LOGCONTAINER("moz_container_wayland_size_allocate [%p] %d,%d -> %d x %d\n",
               (void*)moz_container_get_nsWindow(MOZ_CONTAINER(widget)),
               allocation->x, allocation->y, allocation->width,
               allocation->height);

  /* short circuit if you can */
  gtk_widget_get_allocation(widget, &tmp_allocation);
  if (tmp_allocation.x == allocation->x && tmp_allocation.y == allocation->y &&
      tmp_allocation.width == allocation->width &&
      tmp_allocation.height == allocation->height) {
    return;
  }
  gtk_widget_set_allocation(widget, allocation);

  if (gtk_widget_get_has_window(widget) && gtk_widget_get_realized(widget)) {
    gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x,
                           allocation->y, allocation->width,
                           allocation->height);
    // We need to position our subsurface according to GdkWindow
    // when offset changes (GdkWindow is maximized for instance).
    // see gtk-clutter-embed.c for reference.
    auto pos = DesktopIntPoint(allocation->x, allocation->y);
    moz_container_wayland_ensure_surface(MOZ_CONTAINER(widget), &pos);
    MOZ_WL_CONTAINER(widget)->before_first_size_alloc = false;
  }
}

static bool moz_container_wayland_ensure_surface(MozContainer* container,
                                                 DesktopIntPoint* aPosition) {
  WaylandSurface* surface = MOZ_WL_SURFACE(container);
  WaylandSurfaceLock lock(surface);

  // We're already mapped, only move surface and quit.
  if (surface->IsMapped()) {
    if (aPosition && surface->MoveLocked(lock, *aPosition)) {
      moz_container_wayland_invalidate(container);
    }
    return true;
  }

  LOGWAYLAND("%s [%p]\n", __FUNCTION__,
             (void*)moz_container_get_nsWindow(container));

  GdkWindow* gdkWindow = gtk_widget_get_window(GTK_WIDGET(container));
  MOZ_DIAGNOSTIC_ASSERT(gdkWindow);

  wl_surface* parentSurface = gdk_wayland_window_get_wl_surface(gdkWindow);
  if (!parentSurface) {
    LOGWAYLAND("    Failed - missing parent surface!");
    return false;
  }
  LOGWAYLAND("    gtk wl_surface %p ID %d\n", (void*)parentSurface,
             wl_proxy_get_id((struct wl_proxy*)parentSurface));

  nsWindow* window = moz_container_get_nsWindow(container);
  MOZ_RELEASE_ASSERT(window);

  GtkWindow* parent =
      gtk_window_get_transient_for(GTK_WINDOW(window->GetGtkWidget()));
  if (parent) {
    nsWindow* parentWindow =
        static_cast<nsWindow*>(g_object_get_data(G_OBJECT(parent), "nsWindow"));
    MOZ_DIAGNOSTIC_ASSERT(parentWindow);
    surface->SetParentLocked(lock,
                             MOZ_WL_SURFACE(parentWindow->GetMozContainer()));
  }

  if (!surface->MapLocked(lock, parentSurface,
                          aPosition ? *aPosition : DesktopIntPoint())) {
    return false;
  }

  surface->SetViewportFollowsSizeChangesLocked(lock);
  surface->AddOpaqueSurfaceHandlerLocked(lock, gdkWindow,
                                         /* aRegisterCommitHandler */ true);

  bool fractionalScale = StaticPrefs::widget_wayland_fractional_scale_enabled();
  bool setHandler = surface->IsToplevelSurface() && fractionalScale;
  if (setHandler) {
    surface->SetScaleCallbackLocked(
        lock, WaylandSurface::ScaleCallbackType::Widget,
        [win = RefPtr{window}]() {
          win->RefreshScale(/* aRefreshScreen */ true,
                            /* aForceRefresh */ true);
        });
  }
  surface->SetScaleTypeLocked(lock,
                              fractionalScale
                                  ? WaylandSurface::ScaleType::Fractional
                                  : WaylandSurface::ScaleType::Ceiled,
                              /* aSetHandler */ setHandler);

  surface->SetOpaqueRegionLocked(lock,
                                 window->GetOpaqueRegion().ToUnknownRegion());
  surface->DisableUserInputLocked(lock);

  // Commit eplicitly now as moz_container_wayland_invalidate() initiated
  // widget repaint
  surface->CommitLocked(lock);

  moz_container_wayland_invalidate(container);

  // We're set so run map callback now
  surface->RunMapCallbackLocked(lock);

  return true;
}

double moz_container_wayland_get_scale(MozContainer* container) {
  nsWindow* window = moz_container_get_nsWindow(container);
  return window ? window->FractionalScaleFactor() : 1.0;
}
