/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WinCompositorWidget.h"

#include <ddraw.h>

#include "VRShMem.h"
#include "VsyncDispatcher.h"
#include "WinCompositorWindowThread.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/gfx/DeviceManagerDx.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "nsWindow.h"

namespace mozilla {
namespace widget {

using namespace mozilla::gfx;
using namespace mozilla;

WinCompositorWidget::WinCompositorWidget(
    const WinCompositorWidgetInitData& aInitData,
    const layers::CompositorOptions& aOptions)
    : CompositorWidget(aOptions),
      mSetParentCompleted(false),
      mWidgetKey(aInitData.widgetKey()),
      mWnd(reinterpret_cast<HWND>(aInitData.hWnd())),
      mTransparencyMode(uint32_t(aInitData.transparencyMode())),
      mCompositorWnds(nullptr, nullptr) {
  MOZ_ASSERT(mWnd && ::IsWindow(mWnd));
}

WinCompositorWidget::~WinCompositorWidget() { DestroyCompositorWindow(); }

uintptr_t WinCompositorWidget::GetWidgetKey() { return mWidgetKey; }

void WinCompositorWidget::EnsureCompositorWindow() {
  if (mCompositorWnds.mCompositorWnd || mCompositorWnds.mInitialParentWnd) {
    return;
  }

  mCompositorWnds = WinCompositorWindowThread::CreateCompositorWindow();
  UpdateCompositorWnd(mCompositorWnds.mCompositorWnd, mWnd);

  MOZ_ASSERT(mCompositorWnds.mCompositorWnd);
  MOZ_ASSERT(mCompositorWnds.mInitialParentWnd);
}

void WinCompositorWidget::DestroyCompositorWindow() {
  if (!mCompositorWnds.mCompositorWnd && !mCompositorWnds.mInitialParentWnd) {
    return;
  }
  WinCompositorWindowThread::DestroyCompositorWindow(mCompositorWnds);
  mCompositorWnds = WinCompositorWnds(nullptr, nullptr);
}

void WinCompositorWidget::UpdateCompositorWndSizeIfNecessary() {
  if (!mCompositorWnds.mCompositorWnd) {
    return;
  }

  LayoutDeviceIntSize size = GetClientSize();
  if (mLastCompositorWndSize == size) {
    return;
  }

  // This code is racing with the compositor, which needs to reparent the
  // compositor surface to the actual window (mWnd). To avoid racing mutations,
  // we refuse to proceed until ::SetParent() is called in the parent process.
  // After the ::SetParent() call, composition is scheduled in
  // CompositorWidgetParent::UpdateCompositorWnd().
  if (!mSetParentCompleted) {
    // ::SetParent() is not completed yet.
    return;
  }

  MOZ_ASSERT(mWnd == ::GetParent(mCompositorWnds.mCompositorWnd));

  // Force a resize and redraw (but not a move, activate, etc.).
  if (!::SetWindowPos(
          mCompositorWnds.mCompositorWnd, nullptr, 0, 0, size.width,
          size.height,
          SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOOWNERZORDER | SWP_NOZORDER)) {
    return;
  }

  mLastCompositorWndSize = size;
}

}  // namespace widget
}  // namespace mozilla
