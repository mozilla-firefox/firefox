/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowSurfaceX11Image.h"

#include <cairo-xlib.h>
#include <cairo.h>

#include "gfxPlatform.h"
#include "mozilla/gfx/2D.h"

namespace mozilla::widget {

using namespace mozilla::gfx;

WindowSurfaceX11Image::WindowSurfaceX11Image(Display* aDisplay, Window aWindow,
                                             Visual* aVisual,
                                             unsigned int aDepth)
    : WindowSurfaceX11(aDisplay, aWindow, aVisual, aDepth) {}

WindowSurfaceX11Image::~WindowSurfaceX11Image() = default;

static cairo_format_t ToCairoFormat(SurfaceFormat aFormat) {
  switch (aFormat) {
    case SurfaceFormat::B8G8R8A8:
      return CAIRO_FORMAT_ARGB32;
    case SurfaceFormat::B8G8R8X8:
      return CAIRO_FORMAT_RGB24;
    case SurfaceFormat::R5G6B5_UINT16:
      return CAIRO_FORMAT_RGB16_565;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected window surface format");
      return CAIRO_FORMAT_ARGB32;
  }
}

already_AddRefed<gfx::DrawTarget> WindowSurfaceX11Image::Lock(
    const LayoutDeviceIntRegion& aRegion) {
  gfx::IntRect bounds = aRegion.GetBounds().ToUnknownRect();
  gfx::IntSize size(bounds.XMost(), bounds.YMost());

  if (!mImageSurface || !(size <= mImageSurface->GetSize())) {
    SurfaceFormat format = mFormat;
    if (format == SurfaceFormat::UNKNOWN) {
      format = mDepth == 32 ? SurfaceFormat::B8G8R8A8 : SurfaceFormat::B8G8R8X8;
    }
    mImageSurface =
        Factory::CreateDataSourceSurface(size, format, /* aZero = */ true);
    if (!mImageSurface) {
      return nullptr;
    }
  }

  // Report BGRX as BGRA to the draw target. The alpha channel will be
  // discarded when we put the image.
  SurfaceFormat format = mImageSurface->GetFormat();
  if (format == SurfaceFormat::B8G8R8X8) {
    format = SurfaceFormat::B8G8R8A8;
  }

  return gfxPlatform::CreateDrawTargetForData(mImageSurface->GetData(),
                                              mImageSurface->GetSize(),
                                              mImageSurface->Stride(), format);
}

void WindowSurfaceX11Image::Commit(
    const LayoutDeviceIntRegion& aInvalidRegion) {
  if (!mImageSurface) {
    return;
  }

  gfx::IntRect bounds = aInvalidRegion.GetBounds().ToUnknownRect();
  if (bounds.IsEmpty()) {
    return;
  }

  gfx::IntSize size = mImageSurface->GetSize();
  cairo_surface_t* source = cairo_image_surface_create_for_data(
      mImageSurface->GetData(), ToCairoFormat(mImageSurface->GetFormat()),
      size.width, size.height, mImageSurface->Stride());
  cairo_surface_t* target = cairo_xlib_surface_create(
      mDisplay, mWindow, mVisual, bounds.XMost(), bounds.YMost());
  if (cairo_surface_status(source) == CAIRO_STATUS_SUCCESS &&
      cairo_surface_status(target) == CAIRO_STATUS_SUCCESS) {
    cairo_t* cr = cairo_create(target);
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    for (auto iter = aInvalidRegion.RectIter(); !iter.Done(); iter.Next()) {
      const auto& r = iter.Get();
      cairo_rectangle(cr, r.x, r.y, r.width, r.height);
    }
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(target);
  }
  cairo_surface_destroy(target);
  cairo_surface_destroy(source);
}

}  // namespace mozilla::widget
