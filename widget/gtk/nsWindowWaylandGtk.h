/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsWindowWaylandGtk_h_
#define _nsWindowWaylandGtk_h_

#include "nsWindowWayland.h"

namespace mozilla::widget {

class nsWindowWaylandToplevelGtk;
class nsWindowWaylandPopupGtk;

class nsWindowWaylandToplevelGtk final : public nsWindowWaylandToplevel {
 public:
  nsWindowWaylandToplevelGtk* AsGtk() override { return this; }

  void RestoreXdgToplevel();

  // Use xdg-activation protocol to transfer focus from gFocusWindow.
  void FocusWaylandWindow(const char* aTokenID) override;

  void CreateNative() override;
  void DestroyNative() override;

  bool PIPMove() override;
  bool PIPResize(GdkWindowEdge aEdge) override;

  void MaybeCreatePipResources();
  void ClearPipResources() override;

  bool ApplyEnterLeaveMutterWorkaround() override;

 protected:
  virtual ~nsWindowWaylandToplevelGtk() = default;

  void ConfigureToplevelWindowNative() override;
  void OnMapNative() override;

  void NativeShow(bool aAction) override;
  void HideAndRemoveAppPopups();

  bool CreateRestoreSession(bool aRestoreWindow) override;

  gulong mXdgToplevelRealizedID = 0;

  struct {
    struct xdg_surface* mXdgSurface = nullptr;
    struct xx_pip_v1* mPipSurface = nullptr;
    LayoutDeviceIntSize mConfigureSize;
  } mPipResources;
};

class nsWindowWaylandPopupGtk final : public nsWindowWaylandPopup {
 public:
  nsWindowWaylandPopupGtk();
  nsWindowWaylandPopupGtk* AsGtk() override { return this; }

  void NativeMoveResizeWaylandPopup(bool aMove, bool aResize) override;
  void NativeMoveResizeWaylandPopupCallback(const GdkRectangle* aFinalSize,
                                            bool aFlippedX, bool aFlippedY);
  bool ApplyEnterLeaveMutterWorkaround() override;

  void CreateNative() override;
  void DestroyNative() override;

  void NativeShow(bool aAction) override;

  void HidePopupWindow(bool aTemporaryHidden,
                       bool aRemoveFromPopupList) override;

 protected:
  virtual ~nsWindowWaylandPopupGtk() = default;

  /* If popup is not positioned explicitly before show on Wayland,
   * apply usual popup measurement on Show() to make sure
   * it's placed correctly according to initial position set by Create().
   */
  bool mWaylandApplyPopupPositionBeforeShow : 1;

  // Wayland Popup section

  // Gets the offset from this popup's coordinate to our toplevel coordinates.
  DesktopIntPoint WaylandGetParentPosition() const;

  bool WaylandPopupConfigure();
  void ShowWaylandPopupWindow();
  void WaylandPopupHierarchyHideTemporary();
  void WaylandPopupHierarchyShowTemporaryHidden();
  void WaylandPopupHierarchyCalculatePositions();
  void UpdateWaylandPopupHierarchy();
  void WaylandPopupHierarchyHideByLayout(
      nsTArray<nsIWidget*>* aLayoutWidgetHierarchy);
  void WaylandPopupPrepareForMove();
  void WaylandPopupMoveImpl();
  void WaylandPopupMovePlain(int aX, int aY);
  bool WaylandPopupRemoveNegativePosition(int* aX = nullptr, int* aY = nullptr);
  bool WaylandPopupCheckAndGetAnchor(GdkRectangle* aPopupAnchor,
                                     GdkPoint* aOffset);
  bool WaylandPopupAnchorAdjustForParentPopup(GdkRectangle* aPopupAnchor,
                                              GdkPoint* aOffset);
  void WaylandPopupSetDirectPosition();
  bool WaylandPopupFitsToplevelWindow();
};

}  // namespace mozilla::widget

#endif
