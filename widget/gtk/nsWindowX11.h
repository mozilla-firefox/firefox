/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsWindowX11_h_
#define _nsWindowX11_h_

#include "nsWindow.h"

namespace mozilla::widget {

class nsWindowX11 final : public nsWindow {
 public:
  nsWindowX11* AsX11() override { return this; }

  void GetWorkspaceID(nsAString& workspaceID) override;
  void MoveToWorkspace(const nsAString& workspaceID) override;

  void CreateNative() override;
  void DestroyNative() override;

  bool ConfigureX11GLVisual();
  void ConfigureToplevelWindowNative() override;

  void SetProgress(unsigned long progressPercent) override;

  void LockNativePointer(NativePointerLockMode aNativePointerLockMode) override;
  void UnlockNativePointer() override;
  void UpdateNativePointerBarriers();

 protected:
  virtual ~nsWindowX11() = default;

  Window GetX11Window();

  void NativeShow(bool aAction) override;
  bool SendWorkspaceMoveRequest(int32_t workspaceID);

  void OnMapNative() override;

  bool IsFullscreenSupported() override;

  typedef enum {
    GTK_WIDGET_COMPOSITED_DEFAULT = 0,
    GTK_WIDGET_COMPOSITED_DISABLED = 1,
    GTK_WIDGET_COMPOSITED_ENABLED = 2
  } WindowComposeRequest;

  void SetCompositorHint(WindowComposeRequest aState);

  struct Barriers {
    unsigned long mLeft = 0;
    unsigned long mRight = 0;
    unsigned long mTop = 0;
    unsigned long mBottom = 0;
  };

  bool mIsNativePointerLocked = false;
  mozilla::Maybe<Barriers> mNativePointerBarriers;
  mozilla::Maybe<int32_t> mDeferredWorkspaceID;
};

}  // namespace mozilla::widget

#endif
