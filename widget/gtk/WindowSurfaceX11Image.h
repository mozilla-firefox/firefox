/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_WIDGET_GTK_WINDOW_SURFACE_X11_IMAGE_H
#define MOZILLA_WIDGET_GTK_WINDOW_SURFACE_X11_IMAGE_H

#ifdef MOZ_X11

#  include <glib.h>

#  include "WindowSurfaceX11.h"
#  include "mozilla/gfx/2D.h"

namespace mozilla {
namespace widget {

class WindowSurfaceX11Image : public WindowSurfaceX11 {
 public:
  WindowSurfaceX11Image(Display* aDisplay, Window aWindow, Visual* aVisual,
                        unsigned int aDepth);
  ~WindowSurfaceX11Image();

  already_AddRefed<gfx::DrawTarget> Lock(
      const LayoutDeviceIntRegion& aRegion) override;
  void Commit(const LayoutDeviceIntRegion& aInvalidRegion) override;
  bool IsFallback() const override { return true; }

 private:
  RefPtr<gfx::DataSourceSurface> mImageSurface;
};

}  // namespace widget
}  // namespace mozilla

#endif  // MOZ_X11
#endif  // MOZILLA_WIDGET_GTK_WINDOW_SURFACE_X11_IMAGE_H
