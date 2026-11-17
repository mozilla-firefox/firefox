/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorWidgetVsyncObserver.h"

#include "mozilla/gfx/VsyncBridgeChild.h"
#ifdef MOZ_WAYLAND
#  include "mozilla/WidgetUtilsGtk.h"
#endif

namespace mozilla {
namespace widget {

CompositorWidgetVsyncObserver::CompositorWidgetVsyncObserver(
    RefPtr<VsyncBridgeChild> aVsyncBridge,
    const layers::LayersId& aRootLayerTreeId)
    : mVsyncBridge(aVsyncBridge), mRootLayerTreeId(aRootLayerTreeId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
}

CompositorWidgetVsyncObserver::~CompositorWidgetVsyncObserver() = default;

void CompositorWidgetVsyncObserver::NotifyVsync(const VsyncEvent& aVsync) {
  // Vsync notifications should only arrive on the vsync thread, except on
  // Wayland where the per-widget vsync source runs on the main thread.
  MOZ_ASSERT(XRE_IsParentProcess());
#ifdef MOZ_WAYLAND
  MOZ_ASSERT(!NS_IsMainThread() || GdkIsWaylandDisplay());
#else
  MOZ_ASSERT(!NS_IsMainThread());
#endif

  mVsyncBridge->NotifyVsync(aVsync, mRootLayerTreeId);
}

}  // namespace widget
}  // namespace mozilla
