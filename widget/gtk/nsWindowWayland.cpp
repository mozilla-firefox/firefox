/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowWayland.h"

#include <dlfcn.h>

#include "WaylandVsyncSource.h"
#include "WidgetUtilsGtk.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/VsyncDispatcher.h"
#include "nsAppShell.h"
#include "nsDragService.h"
#include "nsDragSessionSource.h"
#include "nsIAppWindow.h"
#include "nsIClipboard.h"
#include "nsIDocShell.h"
#include "nsISessionStoreFunctions.h"
#include "nsIUUIDGenerator.h"
#include "nsMenuPopupFrame.h"
#include "nsPIDOMWindow.h"
#include "nsWaylandDisplay.h"
#include "nsWindow.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

bool nsWindowWayland::sUseStableRounding = true;

// Returns the last window of window chain starting at aWindow.
// Returns aWindow itself when it has no windows attached.
nsWindowWayland* nsWindowWayland::FindLastWindow(nsWindowWayland* aWindow) {
  while (aWindow && aWindow->GetNextWindow()) {
    aWindow = aWindow->GetNextWindow();
  }
  return aWindow;
}

nsCString nsWindowWayland::GenerateWorkspaceID() {
  nsID id = nsID::GenerateUUID();
  return nsCString(id.ToString().get());
}

void nsWindowWayland::AppendToLastWindow(nsWindowWayland* aWindow) {
  auto* last = FindLastWindow(this);
  last->mWaylandWindowNext = aWindow;
  aWindow->mWaylandWindowPrev = last;
  aWindow->mWaylandWindowNext = nullptr;
}

void nsWindowWayland::RemoveFromList() {
  MOZ_DIAGNOSTIC_ASSERT(mWaylandWindowPrev, "Not a child window!");
  mWaylandWindowPrev->mWaylandWindowNext = mWaylandWindowNext;
  if (mWaylandWindowNext) {
    mWaylandWindowNext->mWaylandWindowPrev = mWaylandWindowPrev;
  }
  mWaylandWindowNext = mWaylandWindowPrev = nullptr;
}

// Force commit to toplevel GdkWindow (mShell) to propagate
// changes made to mContainer. There's only way how to force Gtk3 to
// do the commit without actual painting (Bug 2070729) and that's child
// subsurface position change.
//
// We create a temporary child window located outside of the window
// origin without any content and move it to issue synced commit to toplevel
// window.
void nsWindowWayland::ForceToplevelCommit() {
  if (!mIsMapped) {
    return;
  }
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    return;
  }
  if (!mCommitWindow) {
    GdkWindowAttr attr = {};
    attr.window_type = GDK_WINDOW_SUBSURFACE;
    attr.wclass = GDK_INPUT_OUTPUT;
    attr.width = attr.height = 1;
    attr.x = attr.y = sCommitOrigin;
    attr.event_mask = 0;
    mCommitWindow = gdk_window_new(window, &attr, GDK_WA_X | GDK_WA_Y);
    cairo_region_t* empty = cairo_region_create();
    gdk_window_input_shape_combine_region(mCommitWindow, empty, 0, 0);
    cairo_region_destroy(empty);
    gdk_window_set_transient_for(mCommitWindow, window);
    gdk_window_show(mCommitWindow);
  }

  // Alternate commit position around sCommitOrigin.
  mCommitWindowPosition = mCommitWindowPosition == sCommitOrigin
                              ? sCommitOrigin + 1
                              : sCommitOrigin;
  gdk_window_move(mCommitWindow, mCommitWindowPosition, mCommitWindowPosition);
}

// Gtk3 caches gdk_window_set_opaque_region() and applies it to the toplevel
// wl_surface only when the GdkWindow is repainted, which rarely happens as we
// paint into mContainer. Set it on the wl_surface directly and commit it
// without a repaint, so the compositor never keeps a stale opaque area.
void nsWindowWayland::SetToplevelOpaqueRegion(cairo_region_t* aRegion) {
  // An unmapped toplevel may have no xdg role yet, and committing it stalls
  // the Gtk frame clock (bug 2081642). Gtk applies the cached region on map.
  if (!mIsMapped) {
    return;
  }
  GdkWindow* window = GetToplevelGdkWindow();
  wl_surface* surface =
      window ? gdk_wayland_window_get_wl_surface(window) : nullptr;
  if (!surface) {
    return;
  }
  wl_region* region = nullptr;
  if (aRegion) {
    region = wl_compositor_create_region(WaylandDisplayGet()->GetCompositor());
    for (int i = 0; i < cairo_region_num_rectangles(aRegion); i++) {
      cairo_rectangle_int_t rect;
      cairo_region_get_rectangle(aRegion, i, &rect);
      wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
    }
  }
  wl_surface_set_opaque_region(surface, region);
  if (region) {
    wl_region_destroy(region);
  }
  ForceToplevelCommit();
}

// This is an ugly workaround for
// https://bugzilla.mozilla.org/show_bug.cgi?id=1622107
// We try to detect when Wayland compositor / gtk fails to deliver
// info about finished D&D operations and cancel it on our own.
//
// The sequence is:
// 1) D&D is started on both Wayland & Firefox side
// 2) D&D is finished on Wayland side, Wayland doesn't deliver drag finish to
//    Firefox, Firefox D&D is still active.
// 4) Firefox UI is blocked by the D&D from 1)
// 5) We use mouse button to get mouse clicks and cancel the Firefox D&D
//    session.
//
void nsWindowWayland::WaylandDragWorkaround(GdkEventButton* aEvent) {
  // We track only left button state as Firefox performs D&D on left
  // button only.
  if (aEvent->button != 1 || aEvent->type != GDK_BUTTON_RELEASE) {
    return;
  }

  nsCOMPtr<nsIDragService> dragService =
      do_GetService("@mozilla.org/widget/dragservice;1");
  if (!dragService) {
    return;
  }
  nsCOMPtr<nsIDragSession> currentDragSession =
      dragService->GetCurrentSession(this);
  if (!currentDragSession ||
      static_cast<nsDragSessionSource*>(currentDragSession.get())->IsActive()) {
    return;
  }

  LOGDRAG("WaylandDragWorkaround applied, quit D&D session");
  NS_WARNING(
      "Quit unfinished Wayland Drag and Drop operation. Buggy Wayland "
      "compositor?");

  // Nothing was dropped anywhere, so report the drag as cancelled. Without
  // this the dragend event carries the session defaults (dropEffect "none",
  // not cancelled, end point 0,0), which the tab strip reads as "dropped
  // outside the window" and detaches the tab (bug 2001075).
  currentDragSession->UserCancelled();
  currentDragSession->EndDragSession(true, 0);
}

nsresult nsWindowWayland::SynthesizeNativeMouseMove(
    LayoutDeviceIntPoint aPoint, nsISynthesizedEventCallback* aCallback) {
  if (IsNativePointerLocked()) {
    AutoSynthesizedEventCallbackNotifier notifier(aCallback);
    mNativeLockedPoint = aPoint - WidgetToScreenOffset();

    WidgetMouseEvent event(true, eMouseMove, this, WidgetMouseEvent::eReal);
    event.mRefPoint = mNativeLockedPoint;
    event.AssignEventTime(GetWidgetEventTime(0));
    event.mMovement = Some(LayoutDeviceIntPoint(0, 0));
    DispatchInputEvent(&event);

    return NS_OK;
  }

  return nsWindow::SynthesizeNativeMouseMove(aPoint, aCallback);
}

static void relative_pointer_handle_relative_motion(
    void* data, struct zwp_relative_pointer_v1* pointer, uint32_t time_hi,
    uint32_t time_lo, wl_fixed_t dx_w, wl_fixed_t dy_w, wl_fixed_t dx_unaccel_w,
    wl_fixed_t dy_unaccel_w) {
  RefPtr<nsWindowWayland> window(reinterpret_cast<nsWindowWayland*>(data));
  double scale = window->FractionalScaleFactor();
  LOGW(
      "[%p] relative_pointer_handle_relative_motion center dx = %f, "
      "dy = %f scale %f",
      data, wl_fixed_to_double(dx_w), wl_fixed_to_double(dy_w), scale);

  int32_t movementX = int32_t(wl_fixed_to_double(dx_w) * scale);
  int32_t movementY = int32_t(wl_fixed_to_double(dy_w) * scale);
  if (movementX == 0 && movementY == 0) {
    // Ignore event with zero movement.
    return;
  }

  WidgetMouseEvent event(true, eMouseMove, window, WidgetMouseEvent::eReal);
  event.mRefPoint = window->GetNativeLockedPoint();
  event.AssignEventTime(window->GetWidgetEventTime(time_lo));
  event.mMovement = Some(LayoutDeviceIntPoint(movementX, movementY));
  window->DispatchInputEvent(&event);
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener =
    {
        relative_pointer_handle_relative_motion,
};

void nsWindowWayland::LockNativePointer(
    NativePointerLockMode aNativePointerLockMode) {
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
    LOG("nsWindowWayland::LockNativePointer() - quit, missing pointer "
        "manager.");
    return;
  }

  GdkDisplay* display = gdk_display_get_default();

  GdkDeviceManager* manager = gdk_display_get_device_manager(display);
  MOZ_ASSERT(manager);

  GdkDevice* device = gdk_device_manager_get_client_pointer(manager);
  if (!device) {
    LOG("nsWindowWayland::LockNativePointer() - quit, could not find Wayland "
        "pointer "
        "to lock.");
    return;
  }
  wl_pointer* pointer = gdk_wayland_device_get_wl_pointer(device);
  MOZ_ASSERT(pointer);

  wl_surface* surface =
      gdk_wayland_window_get_wl_surface(GetToplevelGdkWindow());
  if (!surface) {
    LOG("nsWindowWayland::LockNativePointer() - quit, toplevel surface is "
        "hidden.");
    /* Can be null when the window is hidden.
     * Though it's unlikely that a lock request comes in that case, be
     * defensive. */
    return;
  }

  UnlockNativePointer();

  LOG("nsWindowWayland::LockNativePointer()");

  mLockedPointer = zwp_pointer_constraints_v1_lock_pointer(
      pointerConstraints, surface, pointer, nullptr,
      ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  if (!mLockedPointer) {
    LOG("  can't lock Wayland pointer");
    return;
  }

  mRelativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
      relativePointerMgr, pointer);
  if (!mRelativePointer) {
    LOG("  can't create relative Wayland pointer");
    zwp_locked_pointer_v1_destroy(mLockedPointer);
    mLockedPointer = nullptr;
    return;
  }

  zwp_relative_pointer_v1_add_listener(mRelativePointer,
                                       &relative_pointer_listener, this);
}

void nsWindowWayland::UnlockNativePointer() {
  mNativeLockedPoint = LayoutDeviceIntPoint(0, 0);
  if (mRelativePointer) {
    zwp_relative_pointer_v1_destroy(mRelativePointer);
    mRelativePointer = nullptr;
  }
  if (mLockedPointer) {
    zwp_locked_pointer_v1_destroy(mLockedPointer);
    mLockedPointer = nullptr;
  }
}

LayoutDeviceIntSize nsWindowWayland::GetRoundedClientSize() {
  WaylandSurfaceLock lock(mSurface);
  return mSurface->GetScaledSize(lock, mClientArea.Size());
}

void nsWindowWayland::InitPrefs() {
  // 'Stable' Wayland subsurface rounding algorithm is used by all compositors
  // except KDE.
  sUseStableRounding = !IsKdeDesktopEnvironment();

  // Wayland provides clipboard data to application on focus-in event
  // so we need to init our clipboard hooks before we create window
  // and get focus.
  nsCOMPtr<nsIClipboard> clipboard =
      do_GetService("@mozilla.org/widget/clipboard;1");
  NS_ASSERTION(clipboard, "Failed to init clipboard!");
}

nsWindowWaylandToplevel::~nsWindowWaylandToplevel() = default;

// aRestoreWindow = true means we're loading mSessionRestoreToken
// from already saved toplevel window state.
// aRestoreWindow = false means we're creating a new restore session.
bool nsWindowWaylandToplevel::CreateRestoreSessionImpl(
    bool aRestoreWindow, xdg_toplevel* aToplevel) {
  MOZ_DIAGNOSTIC_ASSERT(aToplevel);
  MOZ_DIAGNOSTIC_ASSERT(!mSessionRestoreToken);

  auto* session = WaylandDisplayGet()->GetSession();
  if (!session) {
    LOG("nsWindowWaylandToplevel::CreateRestoreSession(): failed to get "
        "restore session, quit.");
    return false;
  }

  // If we have old profile / workspace ID or it's empty just replace it by
  // UUID to avoid protocol error crash (Bug 2059617). Every ID saved for a
  // Wayland window is a UUID from GenerateWorkspaceID(), while an old one is
  // an integer, so an ID which is not a UUID is replaced.
  nsID id;
  if (!id.Parse(mWorkspaceID)) {
    mWorkspaceID = GenerateWorkspaceID();
    aRestoreWindow = false;
  }

  if (aRestoreWindow) {
    mSessionRestoreToken =
        xdg_session_v1_restore_toplevel(session, aToplevel, mWorkspaceID.get());
  } else {
    mSessionRestoreToken =
        xdg_session_v1_add_toplevel(session, aToplevel, mWorkspaceID.get());
  }

  LOG("nsWindowWaylandToplevel::CreateRestoreSession() ID %s restore %d token "
      "%p",
      mWorkspaceID.get(), aRestoreWindow, mSessionRestoreToken);
  return !!mSessionRestoreToken;
}

void nsWindowWaylandToplevel::GetWorkspaceID(nsAString& aWorkspaceID) {
  if (!nsAppShell::IsSessionRestoreSupported()) {
    aWorkspaceID.Truncate();
    return;
  }
  if (mWorkspaceID.IsEmpty()) {
    mWorkspaceID = GenerateWorkspaceID();
  }
  aWorkspaceID = NS_ConvertUTF8toUTF16(mWorkspaceID);

  LOG("nsWindowWaylandToplevel::GetWorkspaceID() ID %s token %p",
      mWorkspaceID.get(), mSessionRestoreToken);

  if (mSessionRestoreToken) {
    return;
  }

  CreateRestoreSession(/* aRestoreWindow */ false);
}

void nsWindowWaylandToplevel::MoveToWorkspace(
    const nsAString& aWorkspaceIDStr) {
  if (!nsAppShell::IsSessionRestoreSupported()) {
    return;
  }
  LOG("nsWindowWaylandToplevel::MoveToWorkspace() session ID %s "
      "mWaitingToSessionRestore %d mNeedsShow %d",
      NS_ConvertUTF16toUTF8(aWorkspaceIDStr).get(), mWaitingToSessionRestore,
      mNeedsShow);
  // A window which does not wait for session restore is already shown or it is
  // shown without restore, and it is added to the session under its own ID.
  // The passed ID is from a previous session and the compositor may still
  // store state for it, so add_toplevel with it would fail with a name_in_use
  // protocol error (Bug 2064100).
  if (!mWaitingToSessionRestore) {
    return;
  }
  mWorkspaceID = NS_ConvertUTF16toUTF8(aWorkspaceIDStr);
  mWaitingToSessionRestore = false;
  if (mNeedsShow) {
    NativeShow(/* show */ true);
  }
}

void nsWindowWaylandToplevel::FocusWaylandWindowImpl(const char* aTokenID,
                                                     wl_surface* aSurface) {
  MOZ_DIAGNOSTIC_ASSERT(aTokenID);

  if (IsDestroyed() || !aSurface) {
    return;
  }

  LOG("nsWindowWaylandToplevel::FocusWaylandWindow(%s) wl_surface [%p]",
      aTokenID, aSurface);

  LOG("  requesting xdg-activation, surface ID %d",
      wl_proxy_get_id((struct wl_proxy*)aSurface));
  xdg_activation_v1* xdg_activation = WaylandDisplayGet()->GetXdgActivation();
  if (!xdg_activation) {
    return;
  }
  xdg_activation_v1_activate(xdg_activation, aTokenID, aSurface);
}

// Transfer focus from gFocusWindow to aWindow and use xdg_activation
// protocol for it.
void nsWindowWaylandToplevel::TransferFocusTo() {
  LOG("nsWindowWaylandToplevel::TransferFocusTo() gFocusWindow %p",
      GetFocusedWindow());
  auto promise = mozilla::widget::RequestWaylandFocusPromise();
  if (NS_WARN_IF(!promise)) {
    LOG("  quit, failed to create focus promise");
    return;
  }
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      /* resolve */
      [window = RefPtr{this}](nsCString token) {
        window->FocusWaylandWindow(token.get());
      },
      /* reject */
      [window = RefPtr{this}](bool state) {
        LOGW("TransferFocusToWaylandWindow [%p] failed", window.get());
      });
}

void nsWindowWaylandToplevel::CreateCompositorVsyncDispatcher() {
  LOG_VSYNC("nsWindowWaylandToplevel::CreateCompositorVsyncDispatcher()");
  if (!mWaylandVsyncSource) {
    LOG_VSYNC(
        "  mWaylandVsyncSource is missing, create "
        "nsIWidget::CompositorVsyncDispatcher()");
    nsIWidget::CreateCompositorVsyncDispatcher();
    return;
  }
  if (!mCompositorVsyncDispatcherLock) {
    mCompositorVsyncDispatcherLock =
        std::make_unique<Mutex>("mCompositorVsyncDispatcherLock");
  }
  MutexAutoLock lock(*mCompositorVsyncDispatcherLock);
  if (!mCompositorVsyncDispatcher) {
    LOG_VSYNC("  create CompositorVsyncDispatcher()");
    mCompositorVsyncDispatcher =
        new CompositorVsyncDispatcher(mWaylandVsyncDispatcher);
  }
}

RefPtr<VsyncDispatcher> nsWindowWaylandToplevel::GetVsyncDispatcher() {
  return mWaylandVsyncDispatcher;
}

void nsWindowWaylandToplevel::EnableVSyncSource() {
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->EnableVSyncSource();
  }
}

void nsWindowWaylandToplevel::DisableVSyncSource() {
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->DisableVSyncSource();
  }
}

void nsWindowWaylandToplevel::WatchHiddenVSyncSource() {
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->WatchHiddenWindowVSync();
  }
}

bool nsWindowWaylandToplevel::IsPipWindow() const {
  return mPiPType == PiPType::MediaPiP &&
         StaticPrefs::widget_wayland_experimental_pip_enabled_AtStartup() &&
         GdkIsWaylandDisplay() && WaylandDisplayGet()->GetPipShell();
}

// ----------------------------------------------------------------------
// nsWindowWaylandPopup
// ----------------------------------------------------------------------

nsWindowWaylandPopup::nsWindowWaylandPopup()
    : mPopupTrackInHierarchy(false),
      mPopupTrackInHierarchyConfigured(false),
      mPopupAnchored(false),
      mPopupContextMenu(false),
      mPopupMatchesLayout(false),
      mPopupChanged(false),
      mPopupClosed(false),
      mPopupUseMoveToRect(false),
      mWaitingForMoveToRectCallback(false),
      mMovedAfterMoveToRect(false),
      mResizedAfterMoveToRect(false) {}

LayoutDeviceIntSize nsWindowWaylandPopup::GetMoveToRectPopupSize() {
  return ToLayoutDevicePixels(mMoveToRectPopupSize);
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

bool nsWindowWaylandPopup::IsPopupDirectionRTL() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  return popupFrame && popupFrame->IsDirectionRTL();
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
void nsWindowWaylandPopup::LogPopupAnchorHints(int aHints) {
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

void nsWindowWaylandPopup::LogPopupGravity(GdkGravity aGravity) {
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

void nsWindowWaylandPopup::LogPopupHierarchy(
    nsWindowWaylandPopup* aFirstPopup) {
  if (!LOG_ENABLED()) {
    return;
  }

  LOGW("Widget Popup Hierarchy:\n");
  {
    if (!aFirstPopup) {
      LOGW("     Empty\n");
    } else {
      int indent = 4;
      for (nsWindowWaylandPopup* popup = aFirstPopup; popup;
           popup = popup->GetNextPopup()) {
        nsPrintfCString indentString("%*s", indent, " ");
        LOGW(
            "%s %s %s nsWindow [%p] Permanent %d ContextMenu %d "
            "Anchored %d Visible %d MovedByRect %d\n",
            indentString.get(), popup->GetFrameTag().get(),
            popup->GetPopupTypeName().get(), popup,
            popup->WaylandPopupIsPermanent(), popup->mPopupContextMenu,
            popup->mPopupAnchored, gtk_widget_is_visible(popup->mShell),
            popup->mPopupUseMoveToRect);
        indent += 4;
      }
    }
  }

  LOGW("Layout Popup Hierarchy:\n");
  AutoTArray<nsIWidget*, 5> widgetChain;
  aFirstPopup->GetLayoutPopupWidgetChain(&widgetChain);
  if (widgetChain.Length() == 0) {
    LOGW("     Empty\n");
  } else {
    for (unsigned long i = 0; i < widgetChain.Length(); i++) {
      auto* window = static_cast<nsWindowWaylandPopup*>(widgetChain[i]);
      nsPrintfCString indentString("%*s", (int)(i + 1) * 4, " ");
      if (window) {
        LOGW(
            "%s %s %s nsWindow [%p] Permanent %d ContextMenu %d "
            "Anchored %d Visible %d MovedByRect %d\n",
            indentString.get(), window->GetFrameTag().get(),
            window->GetPopupTypeName().get(), window,
            window->WaylandPopupIsPermanent(), window->mPopupContextMenu,
            window->mPopupAnchored, gtk_widget_is_visible(window->mShell),
            window->mPopupUseMoveToRect);
      } else {
        LOGW("%s null window\n", indentString.get());
      }
    }
  }
}
#endif

const nsWindowWaylandPopup::WaylandPopupMoveToRectParams
nsWindowWaylandPopup::WaylandPopupGetPositionFromLayout() {
  LOG("nsWindowWaylandPopup::WaylandPopupGetPositionFromLayout\n");

  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    return {};
  }

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
  if (isRTL && (anchored || isTopContextMenu)) {
    nsMenuPopupFrame::FlipAnchorForRTL(anchorAlign, popupAlign);
  }

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

  // slideHorizontal from nsMenuPopupFrame::SetPopupPosition
  const int8_t position = popupFrame->GetAlignmentPosition();
  // Gtk default is: GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE | GDK_ANCHOR_RESIZE.
  const auto hints = GdkAnchorHints([&] {
    // We want tooltips to flip vertically or slide only.
    // See nsMenuPopupFrame::SetPopupPosition().
    // https://searchfox.org/mozilla-central/rev/d0f5bc50aff3462c9d1546b88d60c5cb020eb15c/layout/xul/nsMenuPopupFrame.cpp#1603
    if (mPopupType == PopupType::Tooltip) {
      return GDK_ANCHOR_FLIP_Y | GDK_ANCHOR_SLIDE;
    }
    // We want to SLIDE_X menu on dual monitor setup rather than resize it
    // on the other monitor, so we always allow sliding horizontally.
    // slideVertical position check comes from the same variable in
    // nsMenuPopupFrame::SetPopupPosition.
    //
    // NOTE(emilio): It feels odd to not honor Gecko's FlipType more, but
    // historically we've done that... Maybe reconsider? But note that
    // flipping is tried before sliding, so it seems not too bad?
    const bool slideVertical =
        (position >= POPUPPOSITION_STARTBEFORE &&
         position <= POPUPPOSITION_ENDAFTER) ||
        !anchored || popupFrame->GetFlipType() == FlipType::Slide ||
        (rectAnchor == GDK_GRAVITY_CENTER && menuAnchor == GDK_GRAVITY_CENTER);
    return GDK_ANCHOR_FLIP | GDK_ANCHOR_SLIDE_X |
           (slideVertical ? GDK_ANCHOR_SLIDE_Y : 0) | GDK_ANCHOR_RESIZE;
  }());

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

void nsWindowWaylandPopup::GetLayoutPopupWidgetChain(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  pm->GetSubmenuWidgetChain(aLayoutWidgetHierarchy);
  aLayoutWidgetHierarchy->Reverse();
}

void nsWindowWaylandPopup::RemoveClosedPopups() {
  LOG("nsWindowWaylandPopup::RemoveClosedPopups()");
  nsWindowWaylandPopup* popup = this;
  while (popup) {
    nsWindowWaylandPopup* next = popup->GetNextPopup();
    if (popup->IsPopupClosed()) {
      popup->HidePopupWindow(/* aTemporaryHide */ false,
                             /* aRemoveFromPopupList */ true);
    }
    popup = next;
  }
}

void nsWindowWaylandPopup::AppendPopupToHierarchyList(
    nsWindowWaylandToplevel* aToplevelWindow) {
  mWaylandToplevel = aToplevelWindow;
  mWaylandToplevel->AppendToLastWindow(this);
  mPopupChanged = true;
  mPopupClosed = false;
}

void nsWindowWaylandPopup::RemovePopupFromHierarchyList() {
  // We're already removed from the popup hierarchy
  if (!IsInPopupHierarchy()) {
    return;
  }
  if (GetNextPopup()) {
    GetNextPopup()->mPopupChanged = true;
  }
  RemoveFromList();
}

void nsWindowWaylandPopup::WaylandPopupMarkAsClosed() {
  LOG("nsWindowWaylandPopup::WaylandPopupMarkAsClosed: [%p]\n", this);
  mPopupClosed = true;
  // If we have any child popup window notify it about
  // parent switch.
  if (nsWindowWaylandPopup* next = GetNextPopup()) {
    next->mPopupChanged = true;
  }
}

// Hide all tooltips except the latest one. Called on the first popup in the
// Wayland popup hierarchy.
void nsWindowWaylandPopup::WaylandPopupHideTooltips() {
  LOG("nsWindowWaylandPopup::WaylandPopupHideTooltips");
  MOZ_ASSERT(WaylandPopupIsFirst(), "Should be called on the first popup!");

  nsWindowWaylandPopup* popup = this;
  while (popup && popup->GetNextPopup()) {
    if (popup->mPopupType == PopupType::Tooltip) {
      LOG("  hidding tooltip [%p]", popup);
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->GetNextPopup();
  }
}

void nsWindowWaylandPopup::WaylandPopupCloseOrphanedPopups() {
  LOG("nsWindowWaylandPopup::WaylandPopupCloseOrphanedPopups");
  MOZ_ASSERT(WaylandPopupIsFirst(), "Should be called on the first popup!");

  nsWindowWaylandPopup* popup = this;
  bool dangling = false;
  while (popup) {
    if (!dangling && !MOZ_WL_SURFACE(popup->GetMozContainer())->IsVisible()) {
      LOG("  popup [%p] is waiting to show, close all child popups", popup);
      dangling = true;
    } else if (dangling) {
      LOG("  popup [%p] is dangling, hide it", popup);
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->GetNextPopup();
  }
}

// We can't show popups with remote content or overflow popups
// on top of regular ones.
// If there's any remote popup opened, close all parent popups of it.
void nsWindowWaylandPopup::CloseAllPopupsBeforeRemotePopup() {
  LOG("nsWindowWaylandPopup::CloseAllPopupsBeforeRemotePopup");
  MOZ_ASSERT(WaylandPopupIsFirst(), "Should be called on the first popup!");

  // Don't waste time when there's only one popup opened.
  if (!mWaylandWindowNext) {
    return;
  }

  // Find the first opened remote content popup
  nsWindowWaylandPopup* remotePopup = this;
  while (remotePopup) {
    if (remotePopup->HasRemoteContent() ||
        remotePopup->IsWidgetOverflowWindow()) {
      LOG("  remote popup [%p]", remotePopup);
      break;
    }
    remotePopup = remotePopup->GetNextPopup();
  }

  if (!remotePopup) {
    return;
  }

  // ...hide opened popups before the remote one.
  nsWindowWaylandPopup* popup = this;
  while (popup && popup != remotePopup) {
    LOG("  hidding popup [%p]", popup);
    popup->WaylandPopupMarkAsClosed();
    popup = popup->GetNextPopup();
  }
}

// Compare 'this' popup position in Wayland widget hierarchy
// (mWaylandWindowPrev/mWaylandWindowNext) with
// 'this' popup position in layout hierarchy.
//
// When aMustMatchParent is true we also request
// 'this' parents match, i.e. 'this' has the same parent in
// both layout and widget hierarchy.
bool nsWindowWaylandPopup::IsPopupInLayoutPopupChain(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy, bool aMustMatchParent) {
  int len = (int)aLayoutWidgetHierarchy->Length();
  for (int i = 0; i < len; i++) {
    if (this == (*aLayoutWidgetHierarchy)[i]) {
      if (!aMustMatchParent) {
        return true;
      }

      // Find correct parent popup for 'this' according to widget
      // hierarchy. That means we need to skip closed popups.
      nsWindowWayland* parentPopup = nullptr;

      // Find opened parent for second and more nested popups.
      if (mWaylandWindowPrev != mWaylandToplevel) {
        parentPopup = mWaylandWindowPrev;
        while (parentPopup && parentPopup->AsWaylandPopup()->IsPopupClosed()) {
          parentPopup = parentPopup->GetPrevWindow()->AsWaylandPopup();
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

bool nsWindowWaylandPopup::IsInPopupHierarchy() {
  return mPopupTrackInHierarchy && mWaylandToplevel && mWaylandWindowPrev;
}

void nsWindowWaylandPopup::AddWindowToPopupHierarchy() {
  LOG("nsWindowWaylandPopup::AddWindowToPopupHierarchy\n");
  if (!GetPopupFrame()) {
    LOG("  Window without frame cannot be added as popup!\n");
    return;
  }
  // Check if we're already in the hierarchy
  if (!IsInPopupHierarchy()) {
    mWaylandToplevel =
        nsWindow::FromWidget(GetTopLevelWidget())->AsWaylandToplevel();
    if (mWaylandToplevel) {
      AppendPopupToHierarchyList(mWaylandToplevel->AsWaylandToplevel());
    }
  }
}

// Mark popups outside of layout hierarchy
void nsWindowWaylandPopup::WaylandPopupHierarchyValidateByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindowWaylandPopup::WaylandPopupHierarchyValidateByLayout");
  nsWindowWaylandPopup* popup = this;
  while (popup) {
    if (popup->mPopupType == PopupType::Tooltip) {
      popup->mPopupMatchesLayout = true;
    } else if (!popup->mPopupClosed) {
      popup->mPopupMatchesLayout = popup->IsPopupInLayoutPopupChain(
          aLayoutWidgetHierarchy, /* aMustMatchParent */ true);
      LOG("  popup [%p] parent window [%p] matches layout %d\n", (void*)popup,
          (void*)popup->mWaylandWindowPrev.get(), popup->mPopupMatchesLayout);
    }
    popup = popup->GetNextPopup();
  }
}

bool nsWindowWaylandPopup::WaylandPopupIsContextMenu() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    return false;
  }
  return popupFrame->IsContextMenu();
}

bool nsWindowWaylandPopup::WaylandPopupIsPermanent() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return popupFrame->IsNoAutoHide();
}

bool nsWindowWaylandPopup::WaylandPopupIsAnchored() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return !!popupFrame->GetAnchor();
}

bool nsWindowWaylandPopup::IsWidgetOverflowWindow() {
  if (auto* frame = GetPopupFrame()) {
    if (nsAtom* id = frame->GetContent()->GetID()) {
      return id->Equals(u"widget-overflow"_ns);
    }
  }
  return false;
}

// First popup means it's attached directly to the toplevel window, i.e. our
// previous link is the toplevel rather than another popup.
bool nsWindowWaylandPopup::WaylandPopupIsFirst() {
  return !mWaylandWindowPrev || mWaylandWindowPrev == mWaylandToplevel;
}

// When popup is repositioned by widget code, we need to notify
// layout about it. It's because we control popup placement
// on widget on Wayland so layout may have old popup size/coordinates.
void nsWindowWaylandPopup::WaylandPopupPropagateChangesToLayout(bool aMove,
                                                                bool aResize) {
  LOG("nsWindowWaylandPopup::WaylandPopupPropagateChangesToLayout()");

  if (aResize) {
    LOG("  needSizeUpdate\n");
    if (nsMenuPopupFrame* popupFrame = GetPopupFrame()) {
      RefPtr<PresShell> presShell = popupFrame->PresShell();
      presShell->FrameNeedsReflow(popupFrame, IntrinsicDirty::None,
                                  NS_FRAME_IS_DIRTY);
    }
  }
  if (aMove) {
    LOG("  needPositionUpdate, bounds [%d, %d]", mClientArea.x, mClientArea.y);
    NotifyWindowMoved(mClientArea.TopLeft(), ByMoveToRect::Yes);
  }
}

nsWindow* nsWindowWaylandPopup::GetEffectiveParent() const {
  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (!parentGtkWindow || !GTK_IS_WIDGET(parentGtkWindow)) {
    return nullptr;
  }
  return nsWindow::FromGtkWidget(GTK_WIDGET(parentGtkWindow));
}
