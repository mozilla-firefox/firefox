/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowX11.h"

#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <gdk/gdkkeysyms-compat.h>

#include "GLContextEGL.h"  // for GLContextEGL::FindVisual()
#include "GLContextGLX.h"  // for GLContextGLX::FindVisual()
#include "GUniquePtr.h"
#include "WindowSurfaceX11Image.h"
#include "WindowSurfaceX11SHM.h"
#include "gfxXlibSurface.h"
#include "mozilla/PodOperations.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "nsWindow.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using mozilla::gl::GLContextEGL;
using mozilla::gl::GLContextGLX;

void nsWindowX11::GetWorkspaceID(nsAString& workspaceID) {
  workspaceID.Truncate();

  if (!mShell) {
    return;
  }

  LOG("nsWindow::GetWorkspaceID()\n");

  // Get the gdk window for this widget.
  GdkWindow* gdk_window = GetToplevelGdkWindow();
  if (!gdk_window) {
    LOG("  missing Gdk window, quit.");
    return;
  }

  if (WorkspaceManagementDisabled()) {
    LOG("  WorkspaceManagementDisabled, quit.");
    return;
  }

  GdkAtom cardinal_atom = gdk_x11_xatom_to_atom(XA_CARDINAL);
  GdkAtom type_returned;
  int format_returned;
  int length_returned;
  GUniquePtr<guchar> wm_desktop;

  if (!gdk_property_get(gdk_window, gdk_atom_intern("_NET_WM_DESKTOP", FALSE),
                        cardinal_atom,
                        0,          // offset
                        INT32_MAX,  // length
                        FALSE,      // delete
                        &type_returned, &format_returned, &length_returned,
                        getter_Transfers(wm_desktop))) {
    LOG("  gdk_property_get() failed, quit.");
    return;
  }

  // Any X11 client can set this property with any format and length. GDK
  // widens format-32 items to long, so only then is the data a long array.
  if (format_returned != 32 || length_returned < (int)sizeof(long)) {
    LOG("  malformed _NET_WM_DESKTOP (format %d, length %d), quit.",
        format_returned, length_returned);
    return;
  }

  int32_t id = (int32_t)reinterpret_cast<long*>(wm_desktop.get())[0];
  LOG("  got workspace ID %d", id);
  workspaceID.AppendInt(id);
}

void nsWindowX11::MoveToWorkspace(const nsAString& workspaceIDStr) {
  nsresult rv = NS_OK;
  int32_t workspaceID = workspaceIDStr.ToInteger(&rv);

  LOG("nsWindow::MoveToWorkspace() ID %d", workspaceID);
  if (NS_FAILED(rv)) {
    LOG("  MoveToWorkspace disabled, quit");
    return;
  }

  if (!SendWorkspaceMoveRequest(workspaceID)) {
    mDeferredWorkspaceID = Some(workspaceID);
    LOG("  deferred until the window is shown");
  }
}

bool nsWindowX11::SendWorkspaceMoveRequest(int32_t workspaceID) {
  if (!mShell || !mIsMapped) {
    return false;
  }

  // Get the gdk window for this widget.
  GdkWindow* gdk_window = GetToplevelGdkWindow();
  if (!gdk_window) {
    LOG("  failed to get GdkWindow, quit.");
    return false;
  }

  GdkDisplay* display = gdk_window_get_display(gdk_window);
  gdk_x11_window_move_to_desktop(gdk_window,
                                 static_cast<uint32_t>(workspaceID));
  gdk_display_flush(display);
  LOG("  moved to workspace");
  return true;
}

Window nsWindowX11::GetX11Window() {
  return gdk_x11_window_get_xid(mGdkWindow);
}

// Configure GL visual on X11.
bool nsWindowX11::ConfigureX11GLVisual() {
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

void nsWindowX11::ConfigureToplevelWindowNative() {
  if (xpc::IsInAutomation()) {
    // Opt out of the _NET_WM_FRAME_DRAWN protocol, so the GDK frame clock
    // doesn't get stuck waiting for the compositor. We composite the window
    // contents ourselves anyway, so it would only help with non-CSD titlebars
    // and window shadows.
    //
    // We've observed the frame clock getting stuck at least on ubuntu 24.04 on
    // automation, see bug 1982990.
    //
    // TODO(emilio): Should we do this async? We get very little value from it
    // it seems, and the compositor could get stuck on real hardware too?
    gdk_x11_window_set_frame_sync_enabled(GetToplevelGdkWindow(), FALSE);
  }
}

void nsWindowX11::CreateNative() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  gtk_widget_set_double_buffered(GTK_WIDGET(mContainer), FALSE);
#pragma GCC diagnostic pop

  mSurfaceProvider.Initialize(GetX11Window());
}

void nsWindowX11::DestroyNative() { UnlockNativePointer(); }

void nsWindowX11::SetCompositorHint(WindowComposeRequest aState) {
  gulong value = aState;
  GdkAtom cardinal_atom = gdk_x11_xatom_to_atom(XA_CARDINAL);
  gdk_property_change(GetToplevelGdkWindow(),
                      gdk_atom_intern("_NET_WM_BYPASS_COMPOSITOR", FALSE),
                      cardinal_atom,
                      32,  // format
                      GDK_PROP_MODE_REPLACE, (guchar*)&value, 1);
}

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

#define PROGRESS_HINT "_NET_WM_XAPP_PROGRESS"

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

void nsWindowX11::SetProgress(unsigned long progressPercent) {
  progressPercent = MIN(progressPercent, 100);
  set_window_hint_cardinal(GDK_WINDOW_XID(GetToplevelGdkWindow()),
                           PROGRESS_HINT, progressPercent);
}

static bool SupportsPointerBarriers(Display* aDisplay) {
  MOZ_ASSERT(StaticPrefs::dom_pointer_lock_native_lock_enabled());
  // XXX We expect the capability doesn't change as we use only one display
  // everywhere.
  static const bool sSupported = [&] {
    int eventBase = -1;
    int errorBase = -1;
    if (!XFixesQueryExtension(aDisplay, &eventBase, &errorBase)) {
      return false;
    }

    int major = 0;
    int minor = 0;
    if (!XFixesQueryVersion(aDisplay, &major, &minor)) {
      return false;
    }

    return major >= 5;
  }();
  return sSupported;
}

void nsWindowX11::UpdateNativePointerBarriers() {
  if (!StaticPrefs::dom_pointer_lock_native_lock_enabled()) {
    MOZ_ASSERT(!mIsNativePointerLocked);
    MOZ_ASSERT(!mNativePointerBarriers);
    return;
  }

  if (NS_WARN_IF(!mGdkWindow)) {
    return;
  }

  Display* display = GDK_DISPLAY_XDISPLAY(gdk_window_get_display(mGdkWindow));
  if (NS_WARN_IF(!SupportsPointerBarriers(display))) {
    MOZ_ASSERT(!mNativePointerBarriers);
    return;
  }

  if (mNativePointerBarriers) {
    XFixesDestroyPointerBarrier(display, mNativePointerBarriers->mLeft);
    XFixesDestroyPointerBarrier(display, mNativePointerBarriers->mRight);
    XFixesDestroyPointerBarrier(display, mNativePointerBarriers->mTop);
    XFixesDestroyPointerBarrier(display, mNativePointerBarriers->mBottom);
    mNativePointerBarriers.reset();
  }

  if (mIsNativePointerLocked) {
    Window window = GetX11Window();
    mNativePointerBarriers.emplace(
        XFixesCreatePointerBarrier(
            display, window, mClientArea.X(), mClientArea.Y(), mClientArea.X(),
            mClientArea.YMost(), BarrierPositiveX, 0, nullptr),
        XFixesCreatePointerBarrier(display, window, mClientArea.XMost(),
                                   mClientArea.Y(), mClientArea.XMost(),
                                   mClientArea.YMost(), BarrierNegativeX, 0,
                                   nullptr),
        XFixesCreatePointerBarrier(
            display, window, mClientArea.X(), mClientArea.Y(),
            mClientArea.XMost(), mClientArea.Y(), BarrierPositiveY, 0, nullptr),
        XFixesCreatePointerBarrier(display, window, mClientArea.X(),
                                   mClientArea.YMost(), mClientArea.XMost(),
                                   mClientArea.YMost(), BarrierNegativeY, 0,
                                   nullptr));
  }
}

void nsWindowX11::LockNativePointer(
    NativePointerLockMode aNativePointerLockMode) {
  if (!StaticPrefs::dom_pointer_lock_native_lock_enabled()) {
    MOZ_ASSERT(!mIsNativePointerLocked);
    MOZ_ASSERT(!mNativePointerBarriers);
    return;
  }

  if (mIsNativePointerLocked) {
    MOZ_ASSERT(mNativePointerBarriers);
    return;
  }

  mIsNativePointerLocked = true;
  UpdateNativePointerBarriers();
}

void nsWindowX11::UnlockNativePointer() {
  if (!mIsNativePointerLocked) {
    MOZ_ASSERT(!mNativePointerBarriers);
    return;
  }
  MOZ_ASSERT(StaticPrefs::dom_pointer_lock_native_lock_enabled());
  mIsNativePointerLocked = false;
  UpdateNativePointerBarriers();
}

void nsWindowX11::NativeShow(bool aAction) {
  if (aAction) {
    // unset our flag now that our window has been shown
    mNeedsShow = true;
    auto removeShow = MakeScopeExit([&] { mNeedsShow = false; });

    LOG("nsWindowX11::NativeShow show\n");

    // Set up usertime/startupID metadata for the created window.
    // On X11 we use gtk_window_set_startup_id() so we need to call it
    // before show.
    SetUserTimeAndStartupTokenForActivatedWindow();
    LOG("  calling gtk_widget_show(mShell)\n");
    gtk_widget_show(mShell);

    if (mDeferredWorkspaceID &&
        SendWorkspaceMoveRequest(*mDeferredWorkspaceID)) {
      mDeferredWorkspaceID.reset();
    }

    if (mX11HiddenPopupPositioned) {
      LOG("  re-position hidden popup window [%d, %d]", mClientArea.x,
          mClientArea.y);
      gtk_window_move(GTK_WINDOW(mShell), mClientArea.x, mClientArea.y);
      mX11HiddenPopupPositioned = false;
    }
  } else {
    LOG("nsWindow::NativeShow hide\n");

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
        (void)shellClass->configure_event(mShell, &event);
      }
      mPendingConfigures = 0;
    }
    gtk_widget_hide(mShell);
  }
}

void nsWindowX11::OnMapNative() {
  if (mIsDragPopup) {
    if (GtkWidget* parent = gtk_widget_get_parent(mShell)) {
      gtk_widget_set_opacity(parent, 0.0);
    }
  }

  if (CompositorBridgeChild* remoteRenderer = GetRemoteRenderer()) {
    remoteRenderer->SendResume();
    remoteRenderer->SendForcePresent(wr::RenderReasons::WIDGET);
  }

  // Set window manager hint to keep fullscreen windows composited.
  //
  // If the window were to get unredirected, there could be visible
  // tearing because Gecko does not align its framebuffer updates with
  // vblank.
  //
  // This must be (re-)applied whenever the shell's X window is created,
  // including after CSD-triggered re-realize in SetCustomTitlebar().
  SetCompositorHint(GTK_WIDGET_COMPOSITED_ENABLED);

  XFlush(DefaultXDisplay());
}

bool nsWindowX11::IsFullscreenSupported() {
  GdkScreen* screen = gtk_widget_get_screen(mShell);
  GdkAtom atom = gdk_atom_intern("_NET_WM_STATE_FULLSCREEN", FALSE);
  return gdk_x11_screen_supports_net_wm_hint(screen, atom);
}
