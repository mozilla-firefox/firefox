/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorWidgetChild.h"

namespace mozilla {
namespace widget {

CompositorWidgetChild::CompositorWidgetChild(
    RefPtr<CompositorVsyncDispatcher> aVsyncDispatcher,
    RefPtr<CompositorWidgetVsyncObserver> aVsyncObserver,
    const CompositorWidgetInitData&)
    : mVsyncDispatcher(std::move(aVsyncDispatcher)),
      mVsyncObserver(std::move(aVsyncObserver)) {
  MOZ_ASSERT(XRE_IsParentProcess());
}

CompositorWidgetChild::~CompositorWidgetChild() = default;

bool CompositorWidgetChild::Initialize(
    const layers::CompositorOptions& aOptions) {
  return true;
}

void CompositorWidgetChild::Shutdown() { (void)Send__delete__(this); }

mozilla::ipc::IPCResult CompositorWidgetChild::RecvObserveVsync() {
  mVsyncDispatcher->SetCompositorVsyncObserver(mVsyncObserver);
  return IPC_OK();
}

mozilla::ipc::IPCResult CompositorWidgetChild::RecvUnobserveVsync() {
  mVsyncDispatcher->SetCompositorVsyncObserver(nullptr);
  return IPC_OK();
}

void CompositorWidgetChild::NotifyClientSizeChanged(
    const LayoutDeviceIntSize& aClientSize) {
  (void)SendNotifyClientSizeChanged(aClientSize);
}

void CompositorWidgetChild::CleanupResources() { (void)SendCleanupResources(); }

}  // namespace widget
}  // namespace mozilla
