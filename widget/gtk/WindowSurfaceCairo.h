/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_WIDGET_GTK_WINDOW_SURFACE_CAIRO_H
#define MOZILLA_WIDGET_GTK_WINDOW_SURFACE_CAIRO_H

#include <glib.h>

#include "mozilla/gfx/2D.h"
#include "mozilla/widget/WindowSurface.h"

class nsWindow;

namespace mozilla {
namespace widget {

class WindowSurfaceCairo : public WindowSurface {
 public:
  explicit WindowSurfaceCairo(nsWindow* aWidget);
  ~WindowSurfaceCairo();

  already_AddRefed<gfx::DrawTarget> Lock(
      const LayoutDeviceIntRegion& aRegion) override;
  void Commit(const LayoutDeviceIntRegion& aInvalidRegion) override;
  bool IsFallback() const override { return true; }

 private:
  RefPtr<gfx::DataSourceSurface> mImageSurface;
  RefPtr<nsWindow> mWidget;
};

}  // namespace widget
}  // namespace mozilla

#endif  // MOZILLA_WIDGET_GTK_WINDOW_SURFACE_CAIRO_H
