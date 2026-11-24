/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowSurfaceCairo.h"

#include "gfxPlatform.h"
#include "mozilla/gfx/2D.h"
#include "nsWindow.h"

namespace mozilla {
namespace widget {

using namespace mozilla::gfx;

WindowSurfaceCairo::WindowSurfaceCairo(nsWindow* aWidget) : mWidget(aWidget) {}
WindowSurfaceCairo::~WindowSurfaceCairo() = default;

already_AddRefed<gfx::DrawTarget> WindowSurfaceCairo::Lock(
    const LayoutDeviceIntRegion& aRegion) {
  gfx::IntRect bounds = aRegion.GetBounds().ToUnknownRect();
  gfx::IntSize size(bounds.XMost(), bounds.YMost());

  if (!mImageSurface || !(size <= mImageSurface->GetSize())) {
    mImageSurface = gfx::Factory::CreateDataSourceSurface(
        size, gfx::SurfaceFormat::B8G8R8A8, /* aZero = */ true);
    if (!mImageSurface) {
      return nullptr;
    }
  }

  return gfxPlatform::CreateDrawTargetForData(
      mImageSurface->GetData(), mImageSurface->GetSize(),
      mImageSurface->Stride(), mImageSurface->GetFormat());
}

void WindowSurfaceCairo::Commit(const LayoutDeviceIntRegion& aInvalidRegion) {
  if (!mImageSurface) {
    return;
  }

  // We need to paint in main thread to GtkWidget.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "WindowSurfaceCairo::Commit", [widget = RefPtr{mWidget}, aInvalidRegion,
                                     surface = RefPtr{mImageSurface}]() {
        widget->SetDragPopupSurface(surface, aInvalidRegion);
      }));

  mImageSurface = nullptr;
}

#if 0
void WindowSurfaceCairo::DumpToFile() {
  static int dumpSerial = 0;

  cairo_surface_t* surface = nullptr;
  auto unmap = MakeScopeExit([&] {
    if (surface) {
      cairo_surface_destroy(surface);
    }
  });
  auto size = mImageSurface->GetSize();
  surface = cairo_image_surface_create_for_data(
      mImageSurface->GetData(), CAIRO_FORMAT_ARGB32,
      size.width, size.height, mImageSurface->Stride());
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    nsCString filename;
    filename.Append(nsPrintfCString("firefox-dnd-%.5d.png", dumpSerial++));
    cairo_surface_write_to_png(surface, filename.get());
  }
}
#endif

}  // namespace widget
}  // namespace mozilla
