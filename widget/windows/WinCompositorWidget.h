/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef widget_windows_WinCompositorWidget_h
#define widget_windows_WinCompositorWidget_h

#include "CompositorWidget.h"
#include "mozilla/Atomics.h"
#include "mozilla/Mutex.h"
#include "mozilla/gfx/CriticalSection.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/widget/WinCompositorWindowThread.h"
#include "nsIWidget.h"

class nsWindow;

namespace mozilla::widget {

class PlatformCompositorWidgetDelegate : public CompositorWidgetDelegate {
 public:
  // Callbacks for nsWindow.
  virtual void EnterPresentLock() = 0;
  virtual void LeavePresentLock() = 0;
  virtual void OnDestroyWindow() = 0;
  virtual bool OnWindowResize(const LayoutDeviceIntSize& aSize) = 0;

  // Transparency handling.
  virtual void UpdateTransparency(TransparencyMode aMode) = 0;

  // Deliver visibility info
  virtual void NotifyVisibilityUpdated(bool aIsFullyOccluded) = 0;

  // CompositorWidgetDelegate Overrides

  PlatformCompositorWidgetDelegate* AsPlatformSpecificDelegate() override {
    return this;
  }
};

class WinCompositorWidgetInitData;

// This is the Windows-specific implementation of CompositorWidget. For
// the most part it only requires an HWND, however it maintains extra state
// for transparent windows, as well as for synchronizing WM_SETTEXT messages
// with the compositor.
class WinCompositorWidget : public CompositorWidget {
 public:
  WinCompositorWidget(const WinCompositorWidgetInitData& aInitData,
                      const layers::CompositorOptions& aOptions);
  ~WinCompositorWidget() override;

  // CompositorWidget Overrides

  uintptr_t GetWidgetKey() override;
  WinCompositorWidget* AsWindows() override { return this; }

  HWND GetHwnd() const {
    return mCompositorWnds.mCompositorWnd ? mCompositorWnds.mCompositorWnd
                                          : mWnd;
  }

  HWND GetCompositorHwnd() const { return mCompositorWnds.mCompositorWnd; }

  void EnsureCompositorWindow();
  void DestroyCompositorWindow();
  void UpdateCompositorWndSizeIfNecessary();

  virtual bool GetWindowIsFullyOccluded() const = 0;

  virtual void UpdateCompositorWnd(const HWND aCompositorWnd,
                                   const HWND aParentWnd) = 0;
  virtual void SetRootLayerTreeID(const layers::LayersId& aRootLayerTreeId) = 0;

  bool TransparencyModeIs(TransparencyMode aMode) const {
    return TransparencyMode(uint32_t(mTransparencyMode)) == aMode;
  }

 protected:
  void SetTransparencyMode(TransparencyMode aMode) {
    mTransparencyMode = uint32_t(aMode);
  }

  bool mSetParentCompleted = false;

 private:
  uintptr_t mWidgetKey;
  HWND mWnd;
  mozilla::Atomic<uint32_t, MemoryOrdering::Relaxed> mTransparencyMode;
  WinCompositorWnds mCompositorWnds;
  LayoutDeviceIntSize mLastCompositorWndSize;
};

}  // namespace mozilla::widget

#endif  // widget_windows_WinCompositorWidget_h
