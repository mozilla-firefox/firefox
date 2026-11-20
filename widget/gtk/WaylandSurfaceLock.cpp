/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WaylandSurfaceLock.h"
#ifdef MOZ_WAYLAND
#  include "WaylandSurface.h"
#  include "nsWaylandDisplay.h"
#endif
#include "MozContainer.h"
#include "WidgetUtilsGtk.h"

namespace mozilla::widget {

WaylandSurfaceLock::WaylandSurfaceLock(RefPtr<WaylandSurface> aWaylandSurface,
                                       bool aSkipCommit) {
#ifdef MOZ_WAYLAND
  MOZ_DIAGNOSTIC_ASSERT(aWaylandSurface);
  mWaylandSurface = std::move(aWaylandSurface);
  mSkipCommit = aSkipCommit;
  if (GdkIsWaylandDisplay()) {
    mSurface = mWaylandSurface->Lock(this);
  }
#endif
}

#ifdef MOZ_WAYLAND
void WaylandSurfaceLock::Commit() {
  if (GdkIsWaylandDisplay()) {
    mWaylandSurface->Commit(this, mForceCommit,
                            /* flush display */ false);
  }
}

void WaylandSurfaceLock::CommitAndFlushDisplay() {
  Commit();
  wl_display_flush(WaylandDisplayGet()->GetDisplay());
}
#endif

WaylandSurfaceLock::~WaylandSurfaceLock() {
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    if (mForceCommit || !mSkipCommit) {
      Commit();
    }
    mWaylandSurface->Unlock(&mSurface, this);
  }
#endif
}

WaylandSurface* WaylandSurfaceLock::GetSurface() const {
#ifdef MOZ_WAYLAND
  return mWaylandSurface.get();
#else
  return nullptr;
#endif
}

}  // namespace mozilla::widget
