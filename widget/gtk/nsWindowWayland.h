/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsWindowWayland_h_
#define _nsWindowWayland_h_

#include "WaylandVsyncSource.h"
#include "nsWindow.h"

namespace mozilla::widget {

class nsWindowWaylandToplevel;
class nsWindowWaylandPopup;
class nsWindowWaylandToplevelGtk;
class nsWindowWaylandPopupGtk;

// Base WaylandWindow class
class nsWindowWayland : public nsWindow {
 public:
  nsWindowWayland* AsWayland() override { return this; }

  MOZ_CAN_RUN_SCRIPT void WaylandDragWorkaround(GdkEventButton* aEvent);

  nsresult SynthesizeNativeMouseMove(
      LayoutDeviceIntPoint aPoint,
      nsISynthesizedEventCallback* aCallback) override;

  LayoutDeviceIntPoint GetNativeLockedPoint() {
    MOZ_ASSERT(IsNativePointerLocked());
    return mNativeLockedPoint;
  }

  bool IsNativePointerLocked() { return mLockedPointer && mRelativePointer; }
  void LockNativePointer(NativePointerLockMode aNativePointerLockMode) override;
  void UnlockNativePointer() override;

  virtual bool ApplyEnterLeaveMutterWorkaround() = 0;

  void SetToplevelOpaqueRegion(cairo_region_t* aRegion);
  void ForceToplevelCommit();

  void AppendToLastWindow(nsWindowWayland* aWindow);
  void RemoveFromList();

  nsWindowWayland* FindLastWindow(nsWindowWayland* aWindow);
  nsWindowWayland* GetNextWindow() { return mWaylandWindowNext; }
  nsWindowWayland* GetPrevWindow() { return mWaylandWindowPrev; }

  nsCString GenerateWorkspaceID();

 protected:
  virtual ~nsWindowWayland() = default;

  LayoutDeviceIntPoint mNativeLockedPoint;
  zwp_locked_pointer_v1* mLockedPointer = nullptr;
  zwp_relative_pointer_v1* mRelativePointer = nullptr;

  RefPtr<GdkWindow> mCommitWindow;
  static constexpr int sCommitOrigin = -100;
  int mCommitWindowPosition = sCommitOrigin;

  // Toplevel window (first element) of linked list of Wayland windows.
  // It's nullptr for toplevel.
  RefPtr<nsWindowWaylandToplevel> mWaylandToplevel;

  // Linked list of Wayland windows chain, used for popups.
  // First element in the list is always a toplevel window
  // (i.e. mWaylandToplevel).
  RefPtr<nsWindowWayland> mWaylandWindowPrev;
  RefPtr<nsWindowWayland> mWaylandWindowNext;
};

class nsWindowWaylandToplevel : public nsWindowWayland {
 public:
  nsWindowWaylandToplevel* AsWaylandToplevel() override { return this; }
  virtual nsWindowWaylandToplevelGtk* AsGtk() { return nullptr; }

  // Use xdg-activation protocol to transfer focus from gFocusWindow.
  void TransferFocusTo();
  virtual void FocusWaylandWindow(const char* aTokenID) = 0;

  void GetWorkspaceID(nsAString& aWorkspaceID) override;
  void MoveToWorkspace(const nsAString& aWorkspaceIDStr) override;

  // aRestoreWindow = true loads mSessionRestoreToken from already saved
  // toplevel window state; false creates a new restore session.
  virtual bool CreateRestoreSession(bool aRestoreWindow) = 0;

  void CreateCompositorVsyncDispatcher() override;
  RefPtr<mozilla::VsyncDispatcher> GetVsyncDispatcher() override;

  bool IsPipWindow() const;
  virtual bool PIPMove() = 0;
  virtual bool PIPResize(GdkWindowEdge aEdge) = 0;
  virtual void ClearPipResources() = 0;

  // The popup chain is rooted at the toplevel window itself, so the first
  // popup attached to us is simply our next link.
  nsWindowWaylandPopup* GetFirstPopup() {
    return mWaylandWindowNext ? mWaylandWindowNext->AsWaylandPopup() : nullptr;
  }

 protected:
  virtual ~nsWindowWaylandToplevel();

  void EnableVSyncSource() override;
  void DisableVSyncSource() override;
  void WatchHiddenVSyncSource() override;

  void FocusWaylandWindowImpl(const char* aTokenID, wl_surface* aSurface);

  // aRestoreWindow = true loads mSessionRestoreToken from already saved
  // toplevel window state; false creates a new restore session.
  bool CreateRestoreSessionImpl(bool aRestoreWindow, xdg_toplevel* aToplevel);

  RefPtr<mozilla::WaylandVsyncSource> mWaylandVsyncSource;
  RefPtr<mozilla::VsyncDispatcher> mWaylandVsyncDispatcher;
  xdg_toplevel_session_v1* mSessionRestoreToken = nullptr;
  nsCString mWorkspaceID;
};

class nsWindowWaylandPopup : public nsWindowWayland {
 public:
  nsWindowWaylandPopup();

  nsWindowWaylandPopup* AsWaylandPopup() override { return this; }
  virtual nsWindowWaylandPopupGtk* AsGtk() { return nullptr; }

  LayoutDeviceIntSize GetMoveToRectPopupSize() override;

  virtual void NativeMoveResizeWaylandPopup(bool aMove, bool aResize) = 0;
  virtual void HidePopupWindow(bool aTemporaryHidden,
                               bool aRemoveFromPopupList) = 0;

  bool IsLastPopup() const { return !mWaylandWindowNext; }
  nsWindowWaylandPopup* GetNextPopup() const {
    return mWaylandWindowNext ? mWaylandWindowNext->AsWaylandPopup() : nullptr;
  }
  nsWindowWaylandPopup* GetPrevPopup() const {
    return mWaylandWindowPrev ? mWaylandWindowPrev->AsWaylandPopup() : nullptr;
  }
  bool IsPopupClosed() const { return mPopupClosed; }
  void RemoveClosedPopups();

  void GetLayoutPopupWidgetChain(nsTArray<nsIWidget*>* aLayoutWidgetHierarchy);

  nsWindow* GetEffectiveParent() const;

 protected:
  virtual ~nsWindowWaylandPopup() = default;

  /*  We create popups in two incarnations - wl_subsurface and xdg_popup.
   *
   *  wl_subsurface:
   *    - can't be positioned by move-to-rect
   *    - can stand outside popup widget hierarchy (has toplevel as parent)
   *    - don't have child popup widgets
   *
   *  xdg_popup:
   *    - can be positioned by move-to-rect
   *    - aligned in popup widget hierarchy, first one is attached to toplevel
   *    - has child (popup) widgets
   *
   *  Thus we need to map Firefox popup type to desired one:
   *
   *  wl_subsurface:
   *    - pernament panels
   *
   *  xdg_popup:
   *    - menus
   *    - autohide popups (hamburger menu)
   *    - extension popups
   *    - tooltips
   *
   *  We set mPopupTrackInHierarchy = false for pernament panels which
   *  are always mapped to toplevel and painted as wl_surfaces.
   */
  bool mPopupTrackInHierarchy : 1;
  bool mPopupTrackInHierarchyConfigured : 1;

  // When popup is anchored, mPopupPosition is relative to its parent popup.
  bool mPopupAnchored : 1;

  // When popup is context menu.
  bool mPopupContextMenu : 1;

  // Indicates that this popup matches layout setup so we can use parent popup
  // coordinates reliably.
  bool mPopupMatchesLayout : 1;

  //  Indicates that popup setup was changed and
  //  we need to recalculate popup coordinates.
  bool mPopupChanged : 1;

  // Popup is going to be closed and removed.
  bool mPopupClosed : 1;

  // Popup is positioned by gdk_window_move_to_rect()
  bool mPopupUseMoveToRect : 1;

  /* mWaitingForMoveToRectCallback is set when move-to-rect is called
   * and we're waiting for move-to-rect callback.
   *
   * If another position/resize request comes between move-to-rect call and
   * move-to-rect callback we set mMovedAfterMoveToRect/mResizedAfterMoveToRect.
   */
  bool mWaitingForMoveToRectCallback : 1;
  bool mMovedAfterMoveToRect : 1;
  bool mResizedAfterMoveToRect : 1;

  // Params used for popup placemend by GdkWindowMoveToRect.
  // When popup is only resized and not positioned,
  // we need to reuse last GdkWindowMoveToRect params to avoid
  // popup movement.
  struct WaylandPopupMoveToRectParams {
    LayoutDeviceIntRect mAnchorRect = {0, 0, 0, 0};
    GdkGravity mAnchorRectType = GDK_GRAVITY_NORTH_WEST;
    GdkGravity mPopupAnchorType = GDK_GRAVITY_NORTH_WEST;
    GdkAnchorHints mHints = GDK_ANCHOR_SLIDE;
    GdkPoint mOffset = {0, 0};
    bool mAnchorSet = false;
  };

  WaylandPopupMoveToRectParams mPopupMoveToRectParams;

  // When popup is resized by Gtk by move-to-rect callback,
  // we store final popup size here. Then we use mMoveToRectPopupSize size
  // in following popup operations unless mLayoutPopupSizeCleared is set.
  DesktopIntSize mMoveToRectPopupSize;

  // Wayland Popup section
  bool WaylandPopupIsAnchored();
  bool WaylandPopupIsContextMenu();
  bool WaylandPopupIsPermanent();
  // First popup means it's attached directly to toplevel window
  bool WaylandPopupIsFirst();
  bool IsWidgetOverflowWindow();
  void RemovePopupFromHierarchyList();
  void WaylandPopupHideTooltips();
  void WaylandPopupCloseOrphanedPopups();
  void AppendPopupToHierarchyList(nsWindowWaylandToplevel* aToplevelWindow);
  bool IsInPopupHierarchy();
  void AddWindowToPopupHierarchy();
  void WaylandPopupHierarchyValidateByLayout(
      nsTArray<nsIWidget*>* aLayoutWidgetHierarchy);
  void CloseAllPopupsBeforeRemotePopup();
  bool IsPopupInLayoutPopupChain(nsTArray<nsIWidget*>* aLayoutWidgetHierarchy,
                                 bool aMustMatchParent);
  void WaylandPopupMarkAsClosed();
  const WaylandPopupMoveToRectParams WaylandPopupGetPositionFromLayout();
  void WaylandPopupPropagateChangesToLayout(bool aMove, bool aResize);
  bool IsPopupDirectionRTL();

#ifdef MOZ_LOGGING
  void LogPopupAnchorHints(int aHints);
  void LogPopupGravity(GdkGravity aGravity);
  static void LogPopupHierarchy(nsWindowWaylandPopup* aFirstPopup);
#endif
};

}  // namespace mozilla::widget

#endif
