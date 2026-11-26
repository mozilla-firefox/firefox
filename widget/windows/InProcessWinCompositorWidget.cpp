/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InProcessWinCompositorWidget.h"

#include <ddraw.h>

#include "HeadlessCompositorWidget.h"
#include "HeadlessWidget.h"
#include "VsyncDispatcher.h"
#include "WinCompositorWindowThread.h"
#include "gfxPlatform.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "nsIWidget.h"
#include "nsWindow.h"

namespace mozilla::widget {

using namespace mozilla::gfx;
using namespace mozilla;

/* static */
RefPtr<CompositorWidget> CompositorWidget::CreateLocal(
    const CompositorWidgetInitData& aInitData,
    const layers::CompositorOptions& aOptions, nsIWidget* aWidget) {
  if (aInitData.type() ==
      CompositorWidgetInitData::THeadlessCompositorWidgetInitData) {
    return new HeadlessCompositorWidget(
        aInitData.get_HeadlessCompositorWidgetInitData(), aOptions,
        static_cast<HeadlessWidget*>(aWidget));
  }
  return new InProcessWinCompositorWidget(
      aInitData.get_WinCompositorWidgetInitData(), aOptions,
      static_cast<nsWindow*>(aWidget));
}

InProcessWinCompositorWidget::InProcessWinCompositorWidget(
    const WinCompositorWidgetInitData& aInitData,
    const layers::CompositorOptions& aOptions, nsWindow* aWindow)
    : WinCompositorWidget(aInitData, aOptions),
      mWindow(aWindow),
      mWnd(reinterpret_cast<HWND>(aInitData.hWnd())) {
  MOZ_ASSERT(mWindow);
  MOZ_ASSERT(mWnd && ::IsWindow(mWnd));
}

void InProcessWinCompositorWidget::OnDestroyWindow() {
  gfx::CriticalSectionAutoEnter presentLock(&mPresentLock);
}

bool InProcessWinCompositorWidget::OnWindowResize(
    const LayoutDeviceIntSize& aSize) {
  return true;
}

bool InProcessWinCompositorWidget::PreRender(WidgetRenderingContext* aContext) {
  // This can block waiting for WM_SETTEXT to finish
  // Using PreRender is unnecessarily pessimistic because
  // we technically only need to block during the present call
  // not all of compositor rendering
  mPresentLock.Enter();
  return true;
}

void InProcessWinCompositorWidget::PostRender(
    WidgetRenderingContext* aContext) {
  mPresentLock.Leave();
}

LayoutDeviceIntSize InProcessWinCompositorWidget::GetClientSize() {
  RECT r;
  if (!::GetClientRect(mWnd, &r)) {
    return LayoutDeviceIntSize();
  }
  return LayoutDeviceIntSize(r.right - r.left, r.bottom - r.top);
}

already_AddRefed<gfx::DrawTarget>
InProcessWinCompositorWidget::StartRemoteDrawing() {
  MOZ_ASSERT(!mIsDrawing);

  if (!mBackBuffer.Ensure(nullptr, GetClientSize().ToUnknownSize())) {
    return nullptr;
  }
  RefPtr<DrawTarget> dt = mBackBuffer.CreateDrawTarget();
  if (!dt) {
    return nullptr;
  }
  mIsDrawing = true;
  return dt.forget();
}

void InProcessWinCompositorWidget::EndRemoteDrawingInRegion(
    gfx::DrawTarget* aDrawTarget, const LayoutDeviceIntRegion& aInvalidRegion) {
  if (!mIsDrawing) {
    return;
  }
  mIsDrawing = false;

  HDC windowDC = ::GetDC(mWnd);
  if (!windowDC) {
    return;
  }
  const IntRect bufferRect(IntPoint(), mBackBuffer.Size());
  for (auto it = aInvalidRegion.RectIter(); !it.Done(); it.Next()) {
    IntRect rect = it.Get().ToUnknownRect().Intersect(bufferRect);
    if (!rect.IsEmpty()) {
      mBackBuffer.BlitTo(windowDC, rect);
    }
  }
  ::ReleaseDC(mWnd, windowDC);
}

bool InProcessWinCompositorWidget::InitCompositor(
    layers::Compositor* aCompositor) {
  return true;
}

void InProcessWinCompositorWidget::EnterPresentLock() { mPresentLock.Enter(); }

void InProcessWinCompositorWidget::LeavePresentLock() { mPresentLock.Leave(); }

void InProcessWinCompositorWidget::UpdateTransparency(TransparencyMode aMode) {
  gfx::CriticalSectionAutoEnter presentLock(&mPresentLock);
  SetTransparencyMode(aMode);
}

void InProcessWinCompositorWidget::NotifyVisibilityUpdated(
    bool aIsFullyOccluded) {
  mIsFullyOccluded = aIsFullyOccluded;
}

bool InProcessWinCompositorWidget::GetWindowIsFullyOccluded() const {
  bool isFullyOccluded = mIsFullyOccluded;
  return isFullyOccluded;
}

bool InProcessWinCompositorWidget::IsHidden() const { return ::IsIconic(mWnd); }

nsIWidget* InProcessWinCompositorWidget::RealWidget() { return mWindow; }

void InProcessWinCompositorWidget::ObserveVsync(VsyncObserver* aObserver) {
  if (RefPtr<CompositorVsyncDispatcher> cvd =
          mWindow->GetCompositorVsyncDispatcher()) {
    cvd->SetCompositorVsyncObserver(aObserver);
  }
}

void InProcessWinCompositorWidget::UpdateCompositorWnd(
    const HWND aCompositorWnd, const HWND aParentWnd) {
  MOZ_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aCompositorWnd && aParentWnd);
  MOZ_ASSERT(aParentWnd == mWnd);

  // Since we're in the parent process anyway, we can just call SetParent
  // directly.
  ::SetParent(aCompositorWnd, aParentWnd);
  mSetParentCompleted = true;
}
}  // namespace mozilla::widget
