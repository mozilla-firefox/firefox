/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsWindowWayland_h_
#define _nsWindowWayland_h_

namespace mozilla::widget {

class nsWindowWayland final : public nsWindow {
 public:
  nsWindowWayland();

  nsWindowWayland* AsWayland() override { return this; }
  nsWindow* GetEffectiveParent() const;

  void GetWorkspaceID(nsAString& workspaceID) override;
  void MoveToWorkspace(const nsAString& workspaceIDStr) override;
  void RestoreXdgToplevel();

  // Use xdg-activation protocol to transfer focus from gFocusWindow.
  void TransferFocusTo();
  void FocusWaylandWindow(const char* aTokenID);

  MOZ_CAN_RUN_SCRIPT void WaylandDragWorkaround(GdkEventButton* aEvent);

  void CreateCompositorVsyncDispatcher() override;
  RefPtr<mozilla::VsyncDispatcher> GetVsyncDispatcher() override;

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

  LayoutDeviceIntSize GetMoveToRectPopupSize() override;

  void NativeMoveResizeWaylandPopup(bool aMove, bool aResize);
  void NativeMoveResizeWaylandPopupCallback(const GdkRectangle* aFinalSize,
                                            bool aFlippedX, bool aFlippedY);
  void CreateNative() override;
  void DestroyNative() override;

  void ConfigureToplevelWindowNative() override;

  bool PIPMove();
  bool PIPResize(GdkWindowEdge aEdge);

  void MaybeCreatePipResources();
  void ClearPipResources();

  bool ApplyEnterLeaveMutterWorkaround();

 protected:
  virtual ~nsWindowWayland() = default;

  RefPtr<mozilla::widget::WaylandSurface> mSurface;

  /*  Gkt creates popup in two incarnations - wl_subsurface and xdg_popup.
   *  Kind of popup is choosen before GdkWindow is mapped so we can change
   *  it only when GdkWindow is hidden.
   *
   *  Relevant Gtk code is at gdkwindow-wayland.c
   *  in should_map_as_popup() and should_map_as_subsurface()
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
   *  Thus we need to map Firefox popup type to desired Gtk one:
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

  /* If popup is not positioned explicitly before show on Wayland,
   * apply usual popup measurement on Show() to make sure
   * it's placed correctly according to initial position set by Create().
   */
  bool mWaylandApplyPopupPositionBeforeShow : 1;

  // When popup is anchored, mPopupPosition is relative to its parent popup.
  bool mPopupAnchored : 1;

  // When popup is context menu.
  bool mPopupContextMenu : 1;

  // Indicates that this popup matches layout setup so we can use parent popup
  // coordinates reliably.
  bool mPopupMatchesLayout : 1;

  /*  Indicates that popup setup was changed and
   *  we need to recalculate popup coordinates.
   */
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

  // Wayland Popup section

  // Gets the offset from this popup's coordinate to our toplevel coordinates.
  DesktopIntPoint WaylandGetParentPosition() const;

  bool WaylandPopupConfigure();
  bool WaylandPopupIsAnchored();
  bool WaylandPopupIsContextMenu();
  bool WaylandPopupIsPermanent();
  // First popup means it's attached directly to toplevel window
  bool WaylandPopupIsFirst();
  bool IsWidgetOverflowWindow();
  void RemovePopupFromHierarchyList();
  void ShowWaylandPopupWindow();
  void HideWaylandPopupWindow(bool aTemporaryHidden, bool aRemoveFromPopupList);
  void ShowWaylandToplevelWindow();
  void HideWaylandToplevelWindow();
  void WaylandPopupHideTooltips();
  void WaylandPopupCloseOrphanedPopups();
  void AppendPopupToHierarchyList(nsWindowWayland* aToplevelWindow);
  void WaylandPopupHierarchyHideTemporary();
  void WaylandPopupHierarchyShowTemporaryHidden();
  void WaylandPopupHierarchyCalculatePositions();
  bool IsInPopupHierarchy();
  void AddWindowToPopupHierarchy();
  void UpdateWaylandPopupHierarchy();
  void WaylandPopupHierarchyHideByLayout(
      nsTArray<nsIWidget*>* aLayoutWidgetHierarchy);
  void WaylandPopupHierarchyValidateByLayout(
      nsTArray<nsIWidget*>* aLayoutWidgetHierarchy);
  void CloseAllPopupsBeforeRemotePopup();
  void WaylandPopupHideClosedPopups();
  void WaylandPopupPrepareForMove();
  void WaylandPopupMoveImpl();
  void WaylandPopupMovePlain(int aX, int aY);
  bool WaylandPopupRemoveNegativePosition(int* aX = nullptr, int* aY = nullptr);
  bool WaylandPopupCheckAndGetAnchor(GdkRectangle* aPopupAnchor,
                                     GdkPoint* aOffset);
  bool WaylandPopupAnchorAdjustForParentPopup(GdkRectangle* aPopupAnchor,
                                              GdkPoint* aOffset);
  bool IsPopupInLayoutPopupChain(nsTArray<nsIWidget*>* aLayoutWidgetHierarchy,
                                 bool aMustMatchParent);
  void WaylandPopupMarkAsClosed();
  void WaylandPopupRemoveClosedPopups();
  void WaylandPopupSetDirectPosition();
  bool WaylandPopupFitsToplevelWindow();
  const WaylandPopupMoveToRectParams WaylandPopupGetPositionFromLayout();
  void WaylandPopupPropagateChangesToLayout(bool aMove, bool aResize);
  nsWindowWayland* WaylandPopupFindLast(nsWindowWayland* aPopup);
  GtkWindow* GetCurrentTopmostWindow() const;
  bool IsPopupDirectionRTL();

#ifdef MOZ_LOGGING
  void LogPopupHierarchy();
  void LogPopupAnchorHints(int aHints);
  void LogPopupGravity(GdkGravity aGravity);
#endif

  void NativeShow(bool aAction) override;

  bool WaylandPipEnabled() const;

  void EnableVSyncSource() override;
  void DisableVSyncSource() override;

  bool CreateRestoreSession(bool aRestoreWindow);

  // Toplevel window (first element) of linked list of Wayland popups. It's null
  // if we're the toplevel.
  RefPtr<nsWindowWayland> mWaylandToplevel;

  // Next/Previous popups in Wayland popup hierarchy.
  RefPtr<nsWindowWayland> mWaylandPopupNext;
  RefPtr<nsWindowWayland> mWaylandPopupPrev;

  // When popup is resized by Gtk by move-to-rect callback,
  // we store final popup size here. Then we use mMoveToRectPopupSize size
  // in following popup operations unless mLayoutPopupSizeCleared is set.
  DesktopIntSize mMoveToRectPopupSize;

  RefPtr<mozilla::WaylandVsyncSource> mWaylandVsyncSource;
  RefPtr<mozilla::VsyncDispatcher> mWaylandVsyncDispatcher;
  LayoutDeviceIntPoint mNativeLockedPoint;
  xx_toplevel_session_v1* mSessionRestoreToken = nullptr;
  int mSessionID = 0;
  gulong mXdgToplevelRealizedID = 0;

  zwp_locked_pointer_v1* mLockedPointer = nullptr;
  zwp_relative_pointer_v1* mRelativePointer = nullptr;
  struct {
    struct xdg_surface* mXdgSurface = nullptr;
    struct xx_pip_v1* mPipSurface = nullptr;
    LayoutDeviceIntSize mConfigureSize;
  } mPipResources;
};

}  // namespace mozilla::widget

#endif
