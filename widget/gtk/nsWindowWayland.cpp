/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <gdk/gdkwayland.h>
#include <gdk/gdkkeysyms-compat.h>
#include <dlfcn.h>

#include "nsWindow.h"
#include "nsWindowWayland.h"

#include "nsDragService.h"
#include "nsGtkUtils.h"
#include "nsIAppWindow.h"
#include "nsAppShell.h"
#include "nsIClipboard.h"
#include "nsIDocShell.h"
#include "nsISessionStoreFunctions.h"
#include "nsPIDOMWindow.h"
#include "nsMenuPopupFrame.h"
#include "WaylandVsyncSource.h"
#include "WidgetUtilsGtk.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/VsyncDispatcher.h"
#include "nsGtkKeyUtils.h"
#include "nsWaylandDisplay.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

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
*/

// Start count from 1 since zero is 'uninitialized'
static int sLastSessionID = 1;

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
bool nsWindowWayland::CreateRestoreSession(bool aRestoreWindow) {
  MOZ_DIAGNOSTIC_ASSERT(!mSessionRestoreToken);
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window) {
    LOG("  failed to get xdg_toplevel, quit.");
    return false;
  }
  struct xdg_toplevel* toplevel = GetXdgToplevelFromGdkWindow(window);
  if (!toplevel) {
    LOG(" failed to get xdg_toplevel, quit.");
    return false;
  }
  auto* session = WaylandDisplayGet()->GetSession();
  if (!session) {
    LOG(" failed to get restore session, quit.");
    return false;
  }
  nsAutoCString id;
  id.AppendInt(mSessionID);
  if (aRestoreWindow) {
    mSessionRestoreToken =
        xx_session_v1_restore_toplevel(session, toplevel, id.get());
  } else {
    mSessionRestoreToken =
        xx_session_v1_add_toplevel(session, toplevel, id.get());
  }
  return !!mSessionRestoreToken;
}

void nsWindowWayland::GetWorkspaceID(nsAString& workspaceID) {
  workspaceID.Truncate();
  if (!mSessionID) {
    mSessionID = ++sLastSessionID;
  }
  workspaceID.AppendInt(mSessionID);

  LOG("nsWindowWayland::GetWorkspaceID() ID %d", mSessionID);

  if (mSessionRestoreToken) {
    return;
  }
  CreateRestoreSession(/* aRestoreWindow */ false);
}

#ifdef MOZ_LOGGING
static void SessionRestoredHandler(void* aData,
                                   xx_toplevel_session_v1* aToplevelSession,
                                   struct xdg_toplevel* aSurface) {
  LOGW("nsWindowWayland restored [%p]", aData);
}

static const xx_toplevel_session_v1_listener sSessionListener = {
    SessionRestoredHandler,
};
#endif

void nsWindowWayland::RestoreXdgToplevel() {
  LOG("nsWindowWayland::RestoreXdgToplevel() ID %d GdkWindow [%p]", mSessionID,
      GetToplevelGdkWindow());
  if (CreateRestoreSession(/* aRestoreWindow */ true)) {
#ifdef MOZ_LOGGING
    if (LOG_ENABLED()) {
      xx_toplevel_session_v1_add_listener(mSessionRestoreToken,
                                          &sSessionListener, this);
    }
#endif
  }
}

void nsWindowWayland::MoveToWorkspace(const nsAString& workspaceIDStr) {
  nsresult rv = NS_OK;
  mSessionID = workspaceIDStr.ToInteger(&rv);
  LOG("nsWindowWayland::MoveToWorkspace() session ID %d", mSessionID);
  if (mWaitingToSessionRestore && mNeedsShow) {
    mWaitingToSessionRestore = false;
    NativeShow(/* show */ true);
  }
}

// This is an ugly workaround for
// https://bugzilla.mozilla.org/show_bug.cgi?id=1622107
// We try to detect when Wayland compositor / gtk fails to deliver
// info about finished D&D operations and cancel it on our own.
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
      static_cast<nsDragSession*>(currentDragSession.get())->IsActive()) {
    return;
  }

  LOGDRAG("WaylandDragWorkaround applied, quit D&D session");
  NS_WARNING(
      "Quit unfinished Wayland Drag and Drop operation. Buggy Wayland "
      "compositor?");
  currentDragSession->EndDragSession(true, 0);
}

nsWindowWayland::nsWindowWayland()
    : mPopupTrackInHierarchy(false),
      mPopupTrackInHierarchyConfigured(false),
      mWaylandApplyPopupPositionBeforeShow(true),
      mPopupAnchored(false),
      mPopupContextMenu(false),
      mPopupMatchesLayout(false),
      mPopupChanged(false),
      mPopupClosed(false),
      mPopupUseMoveToRect(false),
      mWaitingForMoveToRectCallback(false),
      mMovedAfterMoveToRect(false),
      mResizedAfterMoveToRect(false) {}

void nsWindowWayland::FocusWaylandWindow(const char* aTokenID) {
  MOZ_DIAGNOSTIC_ASSERT(aTokenID);

  LOG("nsWindowWayland::FocusWaylandWindow(%s)", aTokenID);
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
void nsWindowWayland::TransferFocusTo() {
  LOG("nsWindowWayland::TransferFocusTo() gFocusWindow %p", GetFocusedWindow());
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

void nsWindowWayland::CreateCompositorVsyncDispatcher() {
  LOG_VSYNC("nsWindowWayland::CreateCompositorVsyncDispatcher()");
  if (!mWaylandVsyncSource) {
    LOG_VSYNC(
        "  mWaylandVsyncSource is missing, create "
        "nsIWidget::CompositorVsyncDispatcher()");
    nsIWidget::CreateCompositorVsyncDispatcher();
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

RefPtr<VsyncDispatcher> nsWindowWayland::GetVsyncDispatcher() {
  return mWaylandVsyncDispatcher;
}

void nsWindowWayland::EnableVSyncSource() {
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->EnableVSyncSource();
  }
}

void nsWindowWayland::DisableVSyncSource() {
  if (mWaylandVsyncSource) {
    mWaylandVsyncSource->DisableVSyncSource();
  }
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

LayoutDeviceIntSize nsWindowWayland::GetMoveToRectPopupSize() {
  return ToLayoutDevicePixels(mMoveToRectPopupSize);
}

nsWindow* nsWindowWayland::GetEffectiveParent() const {
  GtkWindow* parentGtkWindow = gtk_window_get_transient_for(GTK_WINDOW(mShell));
  if (!parentGtkWindow || !GTK_IS_WIDGET(parentGtkWindow)) {
    return nullptr;
  }
  return nsWindow::FromGtkWidget(GTK_WIDGET(parentGtkWindow));
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

bool nsWindowWayland::IsPopupDirectionRTL() {
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
void nsWindowWayland::LogPopupAnchorHints(int aHints) {
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

void nsWindowWayland::LogPopupGravity(GdkGravity aGravity) {
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

const nsWindowWayland::WaylandPopupMoveToRectParams
nsWindowWayland::WaylandPopupGetPositionFromLayout() {
  LOG("nsWindowWayland::WaylandPopupGetPositionFromLayout\n");

  nsMenuPopupFrame* popupFrame = GetPopupFrame();

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

bool nsWindowWayland::WaylandPopupAnchorAdjustForParentPopup(
    GdkRectangle* aPopupAnchor, GdkPoint* aOffset) {
  LOG("nsWindowWayland::WaylandPopupAnchorAdjustForParentPopup");

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

bool nsWindowWayland::WaylandPopupCheckAndGetAnchor(GdkRectangle* aPopupAnchor,
                                                    GdkPoint* aOffset) {
  LOG("nsWindowWayland::WaylandPopupCheckAndGetAnchor");

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

void nsWindowWayland::WaylandPopupPrepareForMove() {
  LOG("nsWindowWayland::WaylandPopupPrepareForMove()");

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
void nsWindowWayland::WaylandPopupMovePlain(int aX, int aY) {
  LOG("nsWindowWayland::WaylandPopupMovePlain(%d, %d)", aX, aY);

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
  nsWindow::FromGdkWindow(window)
      ->AsWayland()
      ->NativeMoveResizeWaylandPopupCallback(final_rect, flipped_x, flipped_y);
}

void nsWindowWayland::WaylandPopupMoveImpl() {
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

  LOG("nsWindowWayland::WaylandPopupMove");
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

static void GetLayoutPopupWidgetChain(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  pm->GetSubmenuWidgetChain(aLayoutWidgetHierarchy);
  aLayoutWidgetHierarchy->Reverse();
}

// Wayland keeps strong popup window hierarchy. We need to track active
// (visible) popup windows and make sure we hide popup on the same level
// before we open another one on that level. It means that every open
// popup needs to have an unique parent.
void nsWindowWayland::UpdateWaylandPopupHierarchy() {
  LOG("nsWindowWayland::UpdateWaylandPopupHierarchy\n");

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
  nsWindowWayland* changedPopup = mWaylandToplevel->mWaylandPopupNext;
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

  nsWindowWayland* parentOfchangedPopup = nullptr;
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

  nsWindowWayland* popup = changedPopup;
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
      if (!popup->WaylandPopupIsFirst() &&
          !popup->mWaylandPopupPrev->WaylandPopupIsFirst() &&
          !popup->mWaylandPopupPrev->mPopupUseMoveToRect) {
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
    popup = popup->mWaylandPopupNext;
  }

  changedPopup->WaylandPopupHierarchyShowTemporaryHidden();
}

void nsWindowWayland::AppendPopupToHierarchyList(
    nsWindowWayland* aToplevelWindow) {
  mWaylandToplevel = aToplevelWindow;

  auto* popup = aToplevelWindow;
  while (popup && popup->mWaylandPopupNext) {
    popup = popup->mWaylandPopupNext;
  }
  popup->mWaylandPopupNext = this;

  mWaylandPopupPrev = popup;
  mWaylandPopupNext = nullptr;
  mPopupChanged = true;
  mPopupClosed = false;
}

void nsWindowWayland::RemovePopupFromHierarchyList() {
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
bool nsWindowWayland::WaylandPopupRemoveNegativePosition(int* aX, int* aY) {
  // https://gitlab.gnome.org/GNOME/gtk/-/issues/4071 applies to temporary
  // windows only
  GdkWindow* window = GetToplevelGdkWindow();
  if (!window || gdk_window_get_window_type(window) != GDK_WINDOW_TEMP) {
    return false;
  }

  LOG("nsWindowWayland::WaylandPopupRemoveNegativePosition()");

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

void nsWindowWayland::ShowWaylandPopupWindow() {
  LOG("nsWindowWayland::ShowWaylandPopupWindow. Expected to see visible.");
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

void nsWindowWayland::WaylandPopupMarkAsClosed() {
  LOG("nsWindowWayland::WaylandPopupMarkAsClosed: [%p]\n", this);
  mPopupClosed = true;
  // If we have any child popup window notify it about
  // parent switch.
  if (mWaylandPopupNext) {
    mWaylandPopupNext->mPopupChanged = true;
  }
}

nsWindowWayland* nsWindowWayland::WaylandPopupFindLast(
    nsWindowWayland* aPopup) {
  while (aPopup && aPopup->mWaylandPopupNext) {
    aPopup = aPopup->mWaylandPopupNext;
  }
  return aPopup;
}

// Hide and potentially removes popup from popup hierarchy.
void nsWindowWayland::HideWaylandPopupWindow(bool aTemporaryHide,
                                             bool aRemoveFromPopupList) {
  LOG("nsWindowWayland::HideWaylandPopupWindow: remove from list %d\n",
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

void nsWindowWayland::HideWaylandToplevelWindow() {
  LOG("nsWindowWayland::HideWaylandToplevelWindow: [%p]\n", this);
  if (mWaylandPopupNext) {
    auto* popup = WaylandPopupFindLast(mWaylandPopupNext);
    while (popup->mWaylandToplevel != nullptr) {
      nsWindowWayland* prev = popup->mWaylandPopupPrev;
      popup->HideWaylandPopupWindow(/* aTemporaryHide */ false,
                                    /* aRemoveFromPopupList */ true);
      popup = prev;
    }
  }
  gtk_widget_hide(mShell);
}

void nsWindowWayland::ShowWaylandToplevelWindow() {
  MOZ_ASSERT(!IsWaylandPopup());
  LOG("nsWindowWayland::ShowWaylandToplevelWindow");
  gtk_widget_show(mShell);
}

void nsWindowWayland::WaylandPopupRemoveClosedPopups() {
  LOG("nsWindowWayland::WaylandPopupRemoveClosedPopups()");
  auto* popup = this;
  while (popup) {
    nsWindowWayland* next = popup->mWaylandPopupNext;
    if (popup->mPopupClosed) {
      popup->HideWaylandPopupWindow(/* aTemporaryHide */ false,
                                    /* aRemoveFromPopupList */ true);
    }
    popup = next;
  }
}

// Hide all tooltips except the latest one.
void nsWindowWayland::WaylandPopupHideTooltips() {
  LOG("nsWindowWayland::WaylandPopupHideTooltips");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  nsWindowWayland* popup = mWaylandPopupNext;
  while (popup && popup->mWaylandPopupNext) {
    if (popup->mPopupType == PopupType::Tooltip) {
      LOG("  hidding tooltip [%p]", popup);
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->mWaylandPopupNext;
  }
}

void nsWindowWayland::WaylandPopupCloseOrphanedPopups() {
  LOG("nsWindowWayland::WaylandPopupCloseOrphanedPopups");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  nsWindowWayland* popup = mWaylandPopupNext;
  bool dangling = false;
  while (popup) {
    if (!dangling && !MOZ_WL_SURFACE(popup->GetMozContainer())->IsVisible()) {
      LOG("  popup [%p] is waiting to show, close all child popups", popup);
      dangling = true;
    } else if (dangling) {
      LOG("  popup [%p] is dangling, hide it", popup);
      popup->WaylandPopupMarkAsClosed();
    }
    popup = popup->mWaylandPopupNext;
  }
}

// We can't show popups with remote content or overflow popups
// on top of regular ones.
// If there's any remote popup opened, close all parent popups of it.
void nsWindowWayland::CloseAllPopupsBeforeRemotePopup() {
  LOG("nsWindowWayland::CloseAllPopupsBeforeRemotePopup");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  // Don't waste time when there's only one popup opened.
  if (!mWaylandPopupNext || mWaylandPopupNext->mWaylandPopupNext == nullptr) {
    return;
  }

  // Find the first opened remote content popup
  nsWindowWayland* remotePopup = mWaylandPopupNext;
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
  nsWindowWayland* popup = mWaylandPopupNext;
  while (popup && popup != remotePopup) {
    LOG("  hidding popup [%p]", popup);
    popup->WaylandPopupMarkAsClosed();
    popup = popup->mWaylandPopupNext;
  }
}

// Compare 'this' popup position in Wayland widget hierarchy
// (mWaylandPopupPrev/mWaylandPopupNext) with
// 'this' popup position in layout hierarchy.
//
// When aMustMatchParent is true we also request
// 'this' parents match, i.e. 'this' has the same parent in
// both layout and widget hierarchy.
bool nsWindowWayland::IsPopupInLayoutPopupChain(
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

// Configure Wayland popup. If true is returned we need to track popup
// in popup hierarchy. Otherwise we just show it as is.
bool nsWindowWayland::WaylandPopupConfigure() {
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

  LOG("nsWindowWayland::WaylandPopupConfigure tracked %d anchored %d hint %d "
      "permanent %d\n",
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

bool nsWindowWayland::IsInPopupHierarchy() {
  return mPopupTrackInHierarchy && mWaylandToplevel && mWaylandPopupPrev;
}

void nsWindowWayland::AddWindowToPopupHierarchy() {
  LOG("nsWindowWayland::AddWindowToPopupHierarchy\n");
  if (!GetPopupFrame()) {
    LOG("  Window without frame cannot be added as popup!\n");
    return;
  }

  // Check if we're already in the hierarchy
  if (!IsInPopupHierarchy()) {
    mWaylandToplevel =
        nsWindowWayland::FromWidget(GetTopLevelWidget())->AsWayland();
    if (mWaylandToplevel) {
      AppendPopupToHierarchyList(mWaylandToplevel);
    }
  }
}

// Hide popups which are not in popup chain.
void nsWindowWayland::WaylandPopupHierarchyHideByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindowWayland::WaylandPopupHierarchyHideByLayout");
  MOZ_ASSERT(mWaylandToplevel == nullptr, "Should be called on toplevel only!");

  // Hide all popups which are not in layout popup chain
  nsWindowWayland* popup = mWaylandPopupNext;
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
void nsWindowWayland::WaylandPopupHierarchyValidateByLayout(
    nsTArray<nsIWidget*>* aLayoutWidgetHierarchy) {
  LOG("nsWindowWayland::WaylandPopupHierarchyValidateByLayout");
  nsWindowWayland* popup = mWaylandPopupNext;
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

void nsWindowWayland::WaylandPopupHierarchyHideTemporary() {
  LOG("nsWindowWayland::WaylandPopupHierarchyHideTemporary()");
  nsWindowWayland* popup = WaylandPopupFindLast(this);
  while (popup && popup != this) {
    LOG("  temporary hidding popup [%p]", popup);
    nsWindowWayland* prev = popup->mWaylandPopupPrev;
    popup->HideWaylandPopupWindow(/* aTemporaryHide */ true,
                                  /* aRemoveFromPopupList */ false);
    popup = prev;
  }
}

void nsWindowWayland::WaylandPopupHierarchyShowTemporaryHidden() {
  LOG("nsWindowWayland::WaylandPopupHierarchyShowTemporaryHidden()");
  nsWindowWayland* popup = this;
  while (popup) {
    if (popup->mPopupTemporaryHidden) {
      popup->mPopupTemporaryHidden = false;
      LOG("  showing temporary hidden popup [%p]", popup);
      popup->ShowWaylandPopupWindow();
    }
    popup = popup->mWaylandPopupNext;
  }
}

void nsWindowWayland::WaylandPopupHierarchyCalculatePositions() {
  LOG("nsWindowWayland::WaylandPopupHierarchyCalculatePositions()");

  // Set widget hierarchy in Gtk
  nsWindowWayland* popup = mWaylandToplevel->mWaylandPopupNext;
  while (popup) {
    LOG("  popup [%p] set parent window [%p]", (void*)popup,
        (void*)popup->mWaylandPopupPrev);
    GtkWindowSetTransientFor(GTK_WINDOW(popup->mShell),
                             GTK_WINDOW(popup->mWaylandPopupPrev->mShell));
    popup = popup->mWaylandPopupNext;
  }
}

bool nsWindowWayland::WaylandPopupIsContextMenu() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    return false;
  }
  return popupFrame->IsContextMenu();
}

bool nsWindowWayland::WaylandPopupIsPermanent() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return popupFrame->IsNoAutoHide();
}

bool nsWindowWayland::WaylandPopupIsAnchored() {
  nsMenuPopupFrame* popupFrame = GetPopupFrame();
  if (!popupFrame) {
    // We can always hide popups without frames.
    return false;
  }
  return !!popupFrame->GetAnchor();
}

bool nsWindowWayland::IsWidgetOverflowWindow() {
  if (auto* frame = GetPopupFrame()) {
    if (nsAtom* id = frame->GetContent()->GetID()) {
      return id->Equals(u"widget-overflow"_ns);
    }
  }
  return false;
}

bool nsWindowWayland::WaylandPopupIsFirst() {
  return !mWaylandPopupPrev || !mWaylandPopupPrev->mWaylandToplevel;
}

DesktopIntPoint nsWindowWayland::WaylandGetParentPosition() const {
  MOZ_ASSERT(IsPopup());
  auto* window = GetEffectiveParent();
  if (NS_WARN_IF(!window) || !window->IsPopup()) {
    return {0, 0};
  }
  // If our parent is a popup, offset to our toplevel bounds (note that in
  // Wayland there's no global coordinate space, so our "screen" offset is
  // really relative to the origin of our toplevel).
  DesktopIntPoint offset = window->WidgetToScreenOffsetUnscaled();
  LOG("nsWindowWayland::WaylandGetParentPosition() [%d, %d]\n", offset.x.value,
      offset.y.value);
  return offset;
}

#ifdef MOZ_LOGGING
void nsWindowWayland::LogPopupHierarchy() {
  if (!LOG_ENABLED()) {
    return;
  }

  LOG("Widget Popup Hierarchy:\n");
  if (!mWaylandToplevel->mWaylandPopupNext) {
    LOG("    Empty\n");
  } else {
    int indent = 4;
    nsWindowWayland* popup = mWaylandToplevel->mWaylandPopupNext;
    while (popup) {
      nsPrintfCString indentString("%*s", indent, " ");
      LOG("%s %s %s nsWindow [%p] Permanent %d ContextMenu %d "
          "Anchored %d Visible %d MovedByRect %d\n",
          indentString.get(), popup->GetFrameTag().get(),
          popup->GetPopupTypeName().get(), popup,
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
      nsWindowWayland* window = static_cast<nsWindowWayland*>(widgetChain[i]);
      nsPrintfCString indentString("%*s", (int)(i + 1) * 4, " ");
      if (window) {
        LOG("%s %s %s nsWindow [%p] Permanent %d ContextMenu %d "
            "Anchored %d Visible %d MovedByRect %d\n",
            indentString.get(), window->GetFrameTag().get(),
            window->GetPopupTypeName().get(), window,
            window->WaylandPopupIsPermanent(), window->mPopupContextMenu,
            window->mPopupAnchored, gtk_widget_is_visible(window->mShell),
            window->mPopupUseMoveToRect);
      } else {
        LOG("%s null window\n", indentString.get());
      }
    }
  }
}
#endif

// When popup is repositioned by widget code, we need to notify
// layout about it. It's because we control popup placement
// on widget on Wayland so layout may have old popup size/coordinates.
void nsWindowWayland::WaylandPopupPropagateChangesToLayout(bool aMove,
                                                           bool aResize) {
  LOG("nsWindowWayland::WaylandPopupPropagateChangesToLayout()");

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

void nsWindowWayland::NativeMoveResizeWaylandPopupCallback(
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
void nsWindowWayland::WaylandPopupSetDirectPosition() {
  const DesktopIntRect newRect(mLastMoveRequest, mLastSizeRequest);

  LOG("nsWindowWayland::WaylandPopupSetDirectPosition %s",
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

bool nsWindowWayland::WaylandPopupFitsToplevelWindow() {
  LOG("nsWindowWayland::WaylandPopupFitsToplevelWindow()");

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

  LOG("  parent size %d x %d", parentWindow->mClientArea.width,
      parentWindow->mClientArea.height);

  DesktopIntRect popupRect(mLastMoveRequest, mLastSizeRequest);
  LOG("  popup topleft %d, %d size %d x %d", popupRect.x, popupRect.y,
      popupRect.width, popupRect.height);

  bool fits = parentWindow->mClientArea.Contains(popupRect);
  LOG("  fits %d", fits);
  return fits;
}

void nsWindowWayland::NativeMoveResizeWaylandPopup(bool aMove, bool aResize) {
  GdkRectangle rect{mLastMoveRequest.x, mLastMoveRequest.y,
                    mLastSizeRequest.width, mLastSizeRequest.height};

  LOG("nsWindowWayland::NativeMoveResizeWaylandPopup [%d,%d] -> [%d x %d] move "
      "%d "
      "resize %d\n",
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

bool nsWindowWayland::WaylandPipEnabled() const {
  return mPiPType == PiPType::MediaPiP &&
         StaticPrefs::widget_wayland_experimental_pip_enabled_AtStartup() &&
         GdkIsWaylandDisplay() && WaylandDisplayGet()->GetPipShell();
}

void nsWindowWayland::MaybeCreatePipResources() {
  LOG("MaybeCreatePipResources()");
  if (!WaylandPipEnabled()) {
    return;
  }

  static xx_pip_v1_listener pip_listener = {
      .closed =
          [](void* data, xx_pip_v1*) {
            LOGW("xx_pip_v1_listener::closed()");
            RefPtr self = static_cast<nsWindowWayland*>(data);
            gtk_window_close(GTK_WINDOW(self->mShell));
          },
      .configure_bounds =
          [](void* data, xx_pip_v1*, int32_t w, int32_t h) {
            LOGW("xx_pip_v1_listener::configure_bounds(%d, %d)", w, h);
          },
      .configure_size =
          [](void* data, xx_pip_v1*, int32_t w, int32_t h) {
            LOGW("xx_pip_v1_listener::configure_size(%d, %d)", w, h);
            auto* self = static_cast<nsWindowWayland*>(data);
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
            RefPtr self = static_cast<nsWindowWayland*>(data);
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
  xx_pip_v1_set_app_id(mPipResources.mPipSurface, "org.mozilla." MOZ_APP_NAME);
  wl_surface_commit(surf);
}

void nsWindowWayland::ClearPipResources() {
  MozClearPointer(mPipResources.mPipSurface, xx_pip_v1_destroy);
  MozClearPointer(mPipResources.mXdgSurface, xdg_surface_destroy);
}

bool nsWindowWayland::PIPMove() {
  if (!mPipResources.mPipSurface) {
    return false;
  }
  xx_pip_v1_move(mPipResources.mPipSurface,
                 gdk_wayland_device_get_wl_seat(GdkGetPointer()),
                 nsWaylandDisplay::GetLastEventSerial());
  return true;
}

bool nsWindowWayland::PIPResize(GdkWindowEdge aEdge) {
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

void nsWindowWayland::CreateNative() {
  LOG("nsWindowWayland::CreateNative()");

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

  if (WaylandPipEnabled()) {
    // This avoids GTK from managing our top level surface role.
    // We'll handle it manually in OnMap().
    gdk_wayland_window_set_use_custom_surface(GetToplevelGdkWindow());
  }

  mWaitingToSessionRestore =
      IsTopLevelWidget() &&
      nsAppShell::UpdateAndGetSessionState() == eSessionRestoring;
}

void nsWindowWayland::ConfigureToplevelWindowNative() {
  LOG("nsWindowWayland::ConfigureToplevelWindow() register callback for "
      "toplevel GdkWindow [%p]",
      GetToplevelGdkWindow());
  if (!mWaitingToSessionRestore) {
    return;
  }
  auto* window = GetToplevelGdkWindow();
  if (!window) {
    LOG("  quit, missing toplevel GdkWindow!");
    return;
  }
  if (!g_signal_lookup("xdg-toplevel-realized", G_OBJECT_TYPE(window))) {
    LOG("  quit, missing gtk3 support!");
    return;
  }
  if (g_signal_handler_is_connected(window, mXdgToplevelRealizedID)) {
    return;
  }
  mXdgToplevelRealizedID = g_signal_connect(
      window, "xdg-toplevel-realized",
      G_CALLBACK(+[](GtkWidget* widget) -> gboolean {
        LOGW("nsWindowWayland::ConfigureToplevelWindow() callback");
        RefPtr<nsWindowWayland> window =
            static_cast<nsWindowWayland*>(nsWindow::FromGtkWidget(widget));
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

void nsWindowWayland::DestroyNative() {
  if (mXdgToplevelRealizedID) {
    g_signal_handler_disconnect(GetToplevelGdkWindow(), mXdgToplevelRealizedID);
    mXdgToplevelRealizedID = 0;
  }
  MozClearPointer(mSessionRestoreToken, xx_toplevel_session_v1_destroy);
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

void nsWindowWayland::NativeShow(bool aAction) {
  if (aAction) {
    // unset our flag now that our window has been shown
    mNeedsShow = true;

    if (mWaitingToSessionRestore) {
      LOG("nsWindowWayland::NativeShow() waiting to session restore, quit.");
      if (nsAppShell::UpdateAndGetSessionState() == eSessionRestoring) {
        return;
      }
      mWaitingToSessionRestore = false;
      NS_WARNING("Wayland session restore failed!");
    }

    auto removeShow = MakeScopeExit([&] { mNeedsShow = false; });

    LOG("nsWindowWayland::NativeShow show\n");

    if (IsWaylandPopup()) {
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
      ShowWaylandToplevelWindow();
    }

    SetUserTimeAndStartupTokenForActivatedWindow();

    auto token = std::move(mWindowActivationTokenFromEnv);
    if (!token.IsEmpty()) {
      FocusWaylandWindow(token.get());
    } else if (!IsPopup()) {
      TransferFocusTo();
    }
  } else {
    LOG("nsWindow::NativeShow hide\n");
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
bool nsWindowWayland::ApplyEnterLeaveMutterWorkaround() {
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
