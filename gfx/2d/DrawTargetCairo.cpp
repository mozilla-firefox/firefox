/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetCairo.h"

#include "SourceSurfaceCairo.h"
#include "PathCairo.h"
#include "HelpersCairo.h"
#include "BorrowedContext.h"
#include "FilterNodeSoftware.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_print.h"
#include "nsPrintfCString.h"

#include "cairo.h"
#include "cairo-tee.h"
#include <string.h>

#include "Blur.h"
#include "Logging.h"
#include "Tools.h"

#ifdef CAIRO_HAS_QUARTZ_SURFACE
#  include "cairo-quartz.h"
#  ifdef MOZ_WIDGET_COCOA
#    include <ApplicationServices/ApplicationServices.h>
#  endif
#endif

#ifdef CAIRO_HAS_XLIB_SURFACE
#  include "cairo-xlib.h"
#endif

#ifdef CAIRO_HAS_WIN32_SURFACE
#  include "cairo-win32.h"
#endif

#define PIXMAN_DONT_DEFINE_STDINT
#include "pixman.h"

#include <algorithm>

// 2^23
#define CAIRO_COORD_MAX (Float(0x7fffff))

namespace mozilla {
namespace gfx {

cairo_surface_t* DrawTargetCairo::mDummySurface;

namespace {

// An RAII class to prepare to draw a context and optional path. Saves and
// restores the context on construction/destruction.
class AutoPrepareForDrawing {
 public:
  AutoPrepareForDrawing(DrawTargetCairo* dt, cairo_t* ctx) : mCtx(ctx) {
    dt->PrepareForDrawing(ctx);
    cairo_save(mCtx);
    MOZ_ASSERT(cairo_status(mCtx) ||
               dt->GetTransform().FuzzyEquals(GetTransform()));
  }

  AutoPrepareForDrawing(DrawTargetCairo* dt, cairo_t* ctx, const Path* path)
      : mCtx(ctx) {
    dt->PrepareForDrawing(ctx, path);
    cairo_save(mCtx);
    MOZ_ASSERT(cairo_status(mCtx) ||
               dt->GetTransform().FuzzyEquals(GetTransform()));
  }

  ~AutoPrepareForDrawing() {
    cairo_restore(mCtx);
    cairo_status_t status = cairo_status(mCtx);
    if (status) {
      gfxWarning() << "DrawTargetCairo context in error state: "
                   << cairo_status_to_string(status) << "(" << status << ")";
    }
  }

 private:
#ifdef DEBUG
  Matrix GetTransform() {
    cairo_matrix_t mat;
    cairo_get_matrix(mCtx, &mat);
    return Matrix(mat.xx, mat.yx, mat.xy, mat.yy, mat.x0, mat.y0);
  }
#endif

  cairo_t* mCtx;
};

/* Clamp r to (0,0) (2^23,2^23)
 * these are to be device coordinates.
 *
 * Returns false if the rectangle is completely out of bounds,
 * true otherwise.
 *
 * This function assumes that it will be called with a rectangle being
 * drawn into a surface with an identity transformation matrix; that
 * is, anything above or to the left of (0,0) will be offscreen.
 *
 * First it checks if the rectangle is entirely beyond
 * CAIRO_COORD_MAX; if so, it can't ever appear on the screen --
 * false is returned.
 *
 * Then it shifts any rectangles with x/y < 0 so that x and y are = 0,
 * and adjusts the width and height appropriately.  For example, a
 * rectangle from (0,-5) with dimensions (5,10) will become a
 * rectangle from (0,0) with dimensions (5,5).
 *
 * If after negative x/y adjustment to 0, either the width or height
 * is negative, then the rectangle is completely offscreen, and
 * nothing is drawn -- false is returned.
 *
 * Finally, if x+width or y+height are greater than CAIRO_COORD_MAX,
 * the width and height are clamped such x+width or y+height are equal
 * to CAIRO_COORD_MAX, and true is returned.
 */
static bool ConditionRect(Rect& r) {
  // if either x or y is way out of bounds;
  // note that we don't handle negative w/h here
  if (r.X() > CAIRO_COORD_MAX || r.Y() > CAIRO_COORD_MAX) return false;

  if (r.X() < 0.f) {
    r.SetWidth(r.XMost());
    if (r.Width() < 0.f) return false;
    r.MoveToX(0.f);
  }

  if (r.XMost() > CAIRO_COORD_MAX) {
    r.SetRightEdge(CAIRO_COORD_MAX);
  }

  if (r.Y() < 0.f) {
    r.SetHeight(r.YMost());
    if (r.Height() < 0.f) return false;

    r.MoveToY(0.f);
  }

  if (r.YMost() > CAIRO_COORD_MAX) {
    r.SetBottomEdge(CAIRO_COORD_MAX);
  }
  return true;
}

}  // end anonymous namespace

static bool SupportsSelfCopy(cairo_surface_t* surface) {
  switch (cairo_surface_get_type(surface)) {
#ifdef CAIRO_HAS_QUARTZ_SURFACE
    case CAIRO_SURFACE_TYPE_QUARTZ:
      return true;
#endif
#ifdef CAIRO_HAS_WIN32_SURFACE
    case CAIRO_SURFACE_TYPE_WIN32:
    case CAIRO_SURFACE_TYPE_WIN32_PRINTING:
      return true;
#endif
    default:
      return false;
  }
}

static bool PatternIsCompatible(const Pattern& aPattern) {
  switch (aPattern.GetType()) {
    case PatternType::LINEAR_GRADIENT: {
      const LinearGradientPattern& pattern =
          static_cast<const LinearGradientPattern&>(aPattern);
      return pattern.mStops->GetBackendType() == BackendType::CAIRO;
    }
    case PatternType::RADIAL_GRADIENT: {
      const RadialGradientPattern& pattern =
          static_cast<const RadialGradientPattern&>(aPattern);
      return pattern.mStops->GetBackendType() == BackendType::CAIRO;
    }
    case PatternType::CONIC_GRADIENT: {
      const ConicGradientPattern& pattern =
          static_cast<const ConicGradientPattern&>(aPattern);
      return pattern.mStops->GetBackendType() == BackendType::CAIRO;
    }
    default:
      return true;
  }
}

static cairo_user_data_key_t surfaceDataKey;

static void ReleaseData(void* aData) {
  DataSourceSurface* data = static_cast<DataSourceSurface*>(aData);
  data->Unmap();
  data->Release();
}

static cairo_surface_t* CopyToImageSurface(unsigned char* aData,
                                           const IntRect& aRect,
                                           int32_t aStride,
                                           SurfaceFormat aFormat) {
  MOZ_ASSERT(aData);

  auto aRectWidth = aRect.Width();
  auto aRectHeight = aRect.Height();

  cairo_surface_t* surf = cairo_image_surface_create(
      GfxFormatToCairoFormat(aFormat), aRectWidth, aRectHeight);
  // In certain scenarios, requesting larger than 8k image fails.  Bug 803568
  // covers the details of how to run into it, but the full detailed
  // investigation hasn't been done to determine the underlying cause.  We
  // will just handle the failure to allocate the surface to avoid a crash.
  if (cairo_surface_status(surf)) {
    gfxWarning() << "Invalid surface DTC " << cairo_surface_status(surf);
    return nullptr;
  }

  unsigned char* surfData = cairo_image_surface_get_data(surf);
  int surfStride = cairo_image_surface_get_stride(surf);
  int32_t pixelWidth = BytesPerPixel(aFormat);

  unsigned char* source = aData + aRect.Y() * aStride + aRect.X() * pixelWidth;

  MOZ_ASSERT(aStride >= aRectWidth * pixelWidth);
  for (int32_t y = 0; y < aRectHeight; ++y) {
    memcpy(surfData + y * surfStride, source + y * aStride,
           aRectWidth * pixelWidth);
  }
  cairo_surface_mark_dirty(surf);
  return surf;
}

/**
 * If aSurface can be represented as a surface of type
 * CAIRO_SURFACE_TYPE_IMAGE then returns that surface. Does
 * not add a reference.
 */
static cairo_surface_t* GetAsImageSurface(cairo_surface_t* aSurface) {
  if (cairo_surface_get_type(aSurface) == CAIRO_SURFACE_TYPE_IMAGE) {
    return aSurface;
#ifdef CAIRO_HAS_WIN32_SURFACE
  } else if (cairo_surface_get_type(aSurface) == CAIRO_SURFACE_TYPE_WIN32) {
    return cairo_win32_surface_get_image(aSurface);
#endif
  }

  return nullptr;
}

static cairo_surface_t* CreateSubImageForData(unsigned char* aData,
                                              const IntRect& aRect, int aStride,
                                              SurfaceFormat aFormat) {
  if (!aData) {
    gfxWarning() << "DrawTargetCairo.CreateSubImageForData null aData";
    return nullptr;
  }
  unsigned char* data =
      aData + aRect.Y() * aStride + aRect.X() * BytesPerPixel(aFormat);

  cairo_surface_t* image = cairo_image_surface_create_for_data(
      data, GfxFormatToCairoFormat(aFormat), aRect.Width(), aRect.Height(),
      aStride);
  // Set the subimage's device offset so that in remains in the same place
  // relative to the parent
  cairo_surface_set_device_offset(image, -aRect.X(), -aRect.Y());
  return image;
}

/**
 * Returns a referenced cairo_surface_t representing the
 * sub-image specified by aSubImage.
 */
static cairo_surface_t* ExtractSubImage(cairo_surface_t* aSurface,
                                        const IntRect& aSubImage,
                                        SurfaceFormat aFormat) {
  // No need to worry about retaining a reference to the original
  // surface since the only caller of this function guarantees
  // that aSurface will stay alive as long as the result

  cairo_surface_t* image = GetAsImageSurface(aSurface);
  if (image) {
    image =
        CreateSubImageForData(cairo_image_surface_get_data(image), aSubImage,
                              cairo_image_surface_get_stride(image), aFormat);
    return image;
  }

  cairo_surface_t* similar = cairo_surface_create_similar(
      aSurface, cairo_surface_get_content(aSurface), aSubImage.Width(),
      aSubImage.Height());

  cairo_t* ctx = cairo_create(similar);
  cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_surface(ctx, aSurface, -aSubImage.X(), -aSubImage.Y());
  cairo_paint(ctx);
  cairo_destroy(ctx);

  cairo_surface_set_device_offset(similar, -aSubImage.X(), -aSubImage.Y());
  return similar;
}

/**
 * Returns cairo surface for the given SourceSurface.
 * If possible, it will use the cairo_surface associated with aSurface,
 * otherwise, it will create a new cairo_surface.
 * In either case, the caller must call cairo_surface_destroy on the
 * result when it is done with it.
 */
static cairo_surface_t* GetCairoSurfaceForSourceSurface(
    SourceSurface* aSurface, bool aExistingOnly = false,
    const IntRect& aSubImage = IntRect()) {
  if (!aSurface) {
    return nullptr;
  }

  IntRect subimage = IntRect(IntPoint(), aSurface->GetSize());
  if (!aSubImage.IsEmpty()) {
    MOZ_ASSERT(!aExistingOnly);
    MOZ_ASSERT(subimage.Contains(aSubImage));
    subimage = aSubImage;
  }

  if (aSurface->GetType() == SurfaceType::CAIRO) {
    cairo_surface_t* surf =
        static_cast<SourceSurfaceCairo*>(aSurface)->GetSurface();
    if (aSubImage.IsEmpty()) {
      cairo_surface_reference(surf);
    } else {
      surf = ExtractSubImage(surf, subimage, aSurface->GetFormat());
    }
    return surf;
  }

  if (aSurface->GetType() == SurfaceType::CAIRO_IMAGE) {
    cairo_surface_t* surf =
        static_cast<const DataSourceSurfaceCairo*>(aSurface)->GetSurface();
    if (aSubImage.IsEmpty()) {
      cairo_surface_reference(surf);
    } else {
      surf = ExtractSubImage(surf, subimage, aSurface->GetFormat());
    }
    return surf;
  }

  if (aExistingOnly) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> data = aSurface->GetDataSurface();
  if (!data) {
    return nullptr;
  }

  DataSourceSurface::MappedSurface map;
  if (!data->Map(DataSourceSurface::READ, &map)) {
    return nullptr;
  }

  cairo_surface_t* surf = CreateSubImageForData(map.mData, subimage,
                                                map.mStride, data->GetFormat());

  // In certain scenarios, requesting larger than 8k image fails.  Bug 803568
  // covers the details of how to run into it, but the full detailed
  // investigation hasn't been done to determine the underlying cause.  We
  // will just handle the failure to allocate the surface to avoid a crash.
  if (!surf || cairo_surface_status(surf)) {
    if (surf && (cairo_surface_status(surf) == CAIRO_STATUS_INVALID_STRIDE)) {
      // If we failed because of an invalid stride then copy into
      // a new surface with a stride that cairo chooses. No need to
      // set user data since we're not dependent on the original
      // data.
      cairo_surface_t* result = CopyToImageSurface(
          map.mData, subimage, map.mStride, data->GetFormat());
      data->Unmap();
      return result;
    }
    data->Unmap();
    return nullptr;
  }

  cairo_surface_set_user_data(surf, &surfaceDataKey, data.forget().take(),
                              ReleaseData);
  return surf;
}

// An RAII class to temporarily clear any device offset set
// on a surface. Note that this does not take a reference to the
// surface.
class AutoClearDeviceOffset final {
 public:
  explicit AutoClearDeviceOffset(SourceSurface* aSurface)
      : mSurface(nullptr), mX(0), mY(0) {
    Init(aSurface);
  }

  explicit AutoClearDeviceOffset(const Pattern& aPattern)
      : mSurface(nullptr), mX(0.0), mY(0.0) {
    if (aPattern.GetType() == PatternType::SURFACE) {
      const SurfacePattern& pattern =
          static_cast<const SurfacePattern&>(aPattern);
      Init(pattern.mSurface);
    }
  }

  ~AutoClearDeviceOffset() {
    if (mSurface) {
      cairo_surface_set_device_offset(mSurface, mX, mY);
    }
  }

 private:
  void Init(SourceSurface* aSurface) {
    cairo_surface_t* surface = GetCairoSurfaceForSourceSurface(aSurface, true);
    if (surface) {
      Init(surface);
      cairo_surface_destroy(surface);
    }
  }

  void Init(cairo_surface_t* aSurface) {
    mSurface = aSurface;
    cairo_surface_get_device_offset(mSurface, &mX, &mY);
    cairo_surface_set_device_offset(mSurface, 0, 0);
  }

  cairo_surface_t* mSurface;
  double mX;
  double mY;
};

static inline void CairoPatternAddGradientStop(cairo_pattern_t* aPattern,
                                               const GradientStop& aStop,
                                               Float aNudge = 0) {
  cairo_pattern_add_color_stop_rgba(aPattern, aStop.offset + aNudge,
                                    aStop.color.r, aStop.color.g, aStop.color.b,
                                    aStop.color.a);
}

// Never returns nullptr. As such, you must always pass in Cairo-compatible
// patterns, most notably gradients with a GradientStopCairo.
// The pattern returned must have cairo_pattern_destroy() called on it by the
// caller.
// As the cairo_pattern_t returned may depend on the Pattern passed in, the
// lifetime of the cairo_pattern_t returned must not exceed the lifetime of the
// Pattern passed in.
static cairo_pattern_t* GfxPatternToCairoPattern(const Pattern& aPattern,
                                                 Float aAlpha,
                                                 const Matrix& aTransform) {
  cairo_pattern_t* pat;
  const Matrix* matrix = nullptr;

  switch (aPattern.GetType()) {
    case PatternType::COLOR: {
      DeviceColor color = static_cast<const ColorPattern&>(aPattern).mColor;
      pat = cairo_pattern_create_rgba(color.r, color.g, color.b,
                                      color.a * aAlpha);
      break;
    }

    case PatternType::SURFACE: {
      const SurfacePattern& pattern =
          static_cast<const SurfacePattern&>(aPattern);
      cairo_surface_t* surf = GetCairoSurfaceForSourceSurface(
          pattern.mSurface, false, pattern.mSamplingRect);
      if (!surf) return nullptr;

      pat = cairo_pattern_create_for_surface(surf);

      matrix = &pattern.mMatrix;

      cairo_pattern_set_filter(
          pat, GfxSamplingFilterToCairoFilter(pattern.mSamplingFilter));
      cairo_pattern_set_extend(pat,
                               GfxExtendToCairoExtend(pattern.mExtendMode));

      cairo_surface_destroy(surf);
      break;
    }
    case PatternType::LINEAR_GRADIENT: {
      const LinearGradientPattern& pattern =
          static_cast<const LinearGradientPattern&>(aPattern);

      pat = cairo_pattern_create_linear(pattern.mBegin.x, pattern.mBegin.y,
                                        pattern.mEnd.x, pattern.mEnd.y);

      MOZ_ASSERT(pattern.mStops->GetBackendType() == BackendType::CAIRO);
      GradientStopsCairo* cairoStops =
          static_cast<GradientStopsCairo*>(pattern.mStops.get());
      cairo_pattern_set_extend(
          pat, GfxExtendToCairoExtend(cairoStops->GetExtendMode()));

      matrix = &pattern.mMatrix;

      const std::vector<GradientStop>& stops = cairoStops->GetStops();
      for (size_t i = 0; i < stops.size(); ++i) {
        CairoPatternAddGradientStop(pat, stops[i]);
      }

      break;
    }
    case PatternType::RADIAL_GRADIENT: {
      const RadialGradientPattern& pattern =
          static_cast<const RadialGradientPattern&>(aPattern);

      pat = cairo_pattern_create_radial(pattern.mCenter1.x, pattern.mCenter1.y,
                                        pattern.mRadius1, pattern.mCenter2.x,
                                        pattern.mCenter2.y, pattern.mRadius2);

      MOZ_ASSERT(pattern.mStops->GetBackendType() == BackendType::CAIRO);
      GradientStopsCairo* cairoStops =
          static_cast<GradientStopsCairo*>(pattern.mStops.get());
      cairo_pattern_set_extend(
          pat, GfxExtendToCairoExtend(cairoStops->GetExtendMode()));

      matrix = &pattern.mMatrix;

      const std::vector<GradientStop>& stops = cairoStops->GetStops();
      for (size_t i = 0; i < stops.size(); ++i) {
        CairoPatternAddGradientStop(pat, stops[i]);
      }

      break;
    }
    case PatternType::CONIC_GRADIENT: {
      // XXX(ntim): Bug 1617039 - Implement conic-gradient for Cairo
      pat = cairo_pattern_create_rgba(0.0, 0.0, 0.0, 0.0);

      break;
    }
    default: {
      // We should support all pattern types!
      MOZ_ASSERT(false);
    }
  }

  // The pattern matrix is a matrix that transforms the pattern into user
  // space. Cairo takes a matrix that converts from user space to pattern
  // space. Cairo therefore needs the inverse.
  if (matrix) {
    cairo_matrix_t mat;
    GfxMatrixToCairoMatrix(*matrix, mat);
    cairo_matrix_invert(&mat);
    cairo_pattern_set_matrix(pat, &mat);
  }

  return pat;
}

static bool NeedIntermediateSurface(const Pattern& aPattern,
                                    const DrawOptions& aOptions) {
  // We pre-multiply colours' alpha by the global alpha, so we don't need to
  // use an intermediate surface for them.
  if (aPattern.GetType() == PatternType::COLOR) return false;

  if (aOptions.mAlpha == 1.0) return false;

  return true;
}

DrawTargetCairo::DrawTargetCairo()
    : mContext(nullptr),
      mSurface(nullptr),
      mTransformSingular(false),
      mLockedBits(nullptr),
      mFontOptions(nullptr) {}

DrawTargetCairo::~DrawTargetCairo() {
  cairo_destroy(mContext);
  if (mSurface) {
    cairo_surface_destroy(mSurface);
    mSurface = nullptr;
  }
  if (mFontOptions) {
    cairo_font_options_destroy(mFontOptions);
    mFontOptions = nullptr;
  }
  MOZ_ASSERT(!mLockedBits);
}

bool DrawTargetCairo::IsValid() const {
  return mSurface && !cairo_surface_status(mSurface) && mContext &&
         !cairo_surface_status(cairo_get_group_target(mContext));
}

DrawTargetType DrawTargetCairo::GetType() const {
  if (mContext) {
    cairo_surface_type_t type = cairo_surface_get_type(mSurface);
    if (type == CAIRO_SURFACE_TYPE_TEE) {
      type = cairo_surface_get_type(cairo_tee_surface_index(mSurface, 0));
      MOZ_ASSERT(type != CAIRO_SURFACE_TYPE_TEE, "C'mon!");
      MOZ_ASSERT(
          type == cairo_surface_get_type(cairo_tee_surface_index(mSurface, 1)),
          "What should we do here?");
    }
    switch (type) {
      case CAIRO_SURFACE_TYPE_PDF:
      case CAIRO_SURFACE_TYPE_PS:
      case CAIRO_SURFACE_TYPE_SVG:
      case CAIRO_SURFACE_TYPE_WIN32_PRINTING:
      case CAIRO_SURFACE_TYPE_XML:
        return DrawTargetType::VECTOR;

      case CAIRO_SURFACE_TYPE_VG:
      case CAIRO_SURFACE_TYPE_GL:
      case CAIRO_SURFACE_TYPE_GLITZ:
      case CAIRO_SURFACE_TYPE_QUARTZ:
      case CAIRO_SURFACE_TYPE_DIRECTFB:
        return DrawTargetType::HARDWARE_RASTER;

      case CAIRO_SURFACE_TYPE_SKIA:
      case CAIRO_SURFACE_TYPE_QT:
        MOZ_FALLTHROUGH_ASSERT(
            "Can't determine actual DrawTargetType for DrawTargetCairo - "
            "assuming SOFTWARE_RASTER");
      case CAIRO_SURFACE_TYPE_IMAGE:
      case CAIRO_SURFACE_TYPE_XLIB:
      case CAIRO_SURFACE_TYPE_XCB:
      case CAIRO_SURFACE_TYPE_WIN32:
      case CAIRO_SURFACE_TYPE_BEOS:
      case CAIRO_SURFACE_TYPE_OS2:
      case CAIRO_SURFACE_TYPE_QUARTZ_IMAGE:
      case CAIRO_SURFACE_TYPE_SCRIPT:
      case CAIRO_SURFACE_TYPE_RECORDING:
      case CAIRO_SURFACE_TYPE_DRM:
      case CAIRO_SURFACE_TYPE_SUBSURFACE:
      case CAIRO_SURFACE_TYPE_TEE:  // included to silence warning about
                                    // unhandled enum value
        return DrawTargetType::SOFTWARE_RASTER;
      default:
        MOZ_CRASH("GFX: Unsupported cairo surface type");
    }
  }
  MOZ_ASSERT(false, "Could not determine DrawTargetType for DrawTargetCairo");
  return DrawTargetType::SOFTWARE_RASTER;
}

IntSize DrawTargetCairo::GetSize() const { return mSize; }

SurfaceFormat GfxFormatForCairoSurface(cairo_surface_t* surface) {
  cairo_surface_type_t type = cairo_surface_get_type(surface);
  if (type == CAIRO_SURFACE_TYPE_IMAGE) {
    return CairoFormatToGfxFormat(cairo_image_surface_get_format(surface));
  }
#ifdef CAIRO_HAS_XLIB_SURFACE
  // xlib is currently the only Cairo backend that creates 16bpp surfaces
  if (type == CAIRO_SURFACE_TYPE_XLIB &&
      cairo_xlib_surface_get_depth(surface) == 16) {
    return SurfaceFormat::R5G6B5_UINT16;
  }
#endif
  return CairoContentToGfxFormat(cairo_surface_get_content(surface));
}

// We need to \-escape any single-quotes in the destination and URI strings,
// in order to pass them via the attributes arg to cairo_tag_begin.
//
// We also need to escape any backslashes (bug 1748077), as per doc at
// https://www.cairographics.org/manual/cairo-Tags-and-Links.html#cairo-tag-begin
//
// (Encoding of non-ASCII chars etc gets handled later by the PDF backend.)
static void EscapeForCairo(nsACString& aStr) {
  for (size_t i = aStr.Length(); i > 0;) {
    --i;
    if (aStr[i] == '\'') {
      aStr.ReplaceLiteral(i, 1, "\\'");
    } else if (aStr[i] == '\\') {
      aStr.ReplaceLiteral(i, 1, "\\\\");
    }
  }
}

void DrawTargetCairo::Link(const char* aDest, const char* aURI,
                           const Rect& aRect) {
  if ((!aURI || !*aURI) && (!aDest || !*aDest)) {
    // No destination? Just bail out.
    return;
  }

  double x = aRect.x, y = aRect.y, w = aRect.width, h = aRect.height;
  cairo_user_to_device(mContext, &x, &y);
  cairo_user_to_device_distance(mContext, &w, &h);
  nsPrintfCString attributes("rect=[%f %f %f %f]", x, y, w, h);

  if (aDest && *aDest) {
    nsAutoCString dest(aDest);
    EscapeForCairo(dest);
    attributes.AppendPrintf(" dest='%s'", dest.get());
  }
  if (aURI && *aURI) {
    nsAutoCString uri(aURI);
    EscapeForCairo(uri);
    attributes.AppendPrintf(" uri='%s'", uri.get());
  }

  // We generate a begin/end pair with no content in between, because we are
  // using the rect attribute of the begin tag to specify the link region
  // rather than depending on cairo to accumulate the painted area.
  cairo_tag_begin(mContext, CAIRO_TAG_LINK, attributes.get());
  cairo_tag_end(mContext, CAIRO_TAG_LINK);
}

void DrawTargetCairo::Destination(const char* aDestination,
                                  const Point& aPoint) {
  if (!aDestination || !*aDestination) {
    // No destination? Just bail out.
    return;
  }

  nsAutoCString dest(aDestination);
  EscapeForCairo(dest);

  double x = aPoint.x, y = aPoint.y;
  cairo_user_to_device(mContext, &x, &y);

  nsPrintfCString attributes("name='%s' x=%f y=%f internal", dest.get(), x, y);
  cairo_tag_begin(mContext, CAIRO_TAG_DEST, attributes.get());
  cairo_tag_end(mContext, CAIRO_TAG_DEST);
}

already_AddRefed<SourceSurface> DrawTargetCairo::Snapshot() {
  if (!IsValid()) {
    gfxCriticalNote << "DrawTargetCairo::Snapshot with bad surface "
                    << hexa(mSurface) << ", context " << hexa(mContext)
                    << ", status "
                    << (mSurface ? cairo_surface_status(mSurface) : -1);
    return nullptr;
  }
  if (mSnapshot) {
    RefPtr<SourceSurface> snapshot(mSnapshot);
    return snapshot.forget();
  }

  IntSize size = GetSize();

  mSnapshot = new SourceSurfaceCairo(mSurface, size,
                                     GfxFormatForCairoSurface(mSurface), this);
  RefPtr<SourceSurface> snapshot(mSnapshot);
  return snapshot.forget();
}

bool DrawTargetCairo::LockBits(uint8_t** aData, IntSize* aSize,
                               int32_t* aStride, SurfaceFormat* aFormat,
                               IntPoint* aOrigin) {
  cairo_surface_t* target = cairo_get_group_target(mContext);
  cairo_surface_t* surf = target;
#ifdef CAIRO_HAS_WIN32_SURFACE
  if (cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_WIN32) {
    cairo_surface_t* imgsurf = cairo_win32_surface_get_image(surf);
    if (imgsurf) {
      surf = imgsurf;
    }
  }
#endif
  if (cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_IMAGE &&
      cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
    PointDouble offset;
    cairo_surface_get_device_offset(target, &offset.x.value, &offset.y.value);
    // verify the device offset can be converted to integers suitable for a
    // bounds rect
    IntPoint origin(int32_t(-offset.x), int32_t(-offset.y));
    if (-PointDouble(origin) != offset || (!aOrigin && origin != IntPoint())) {
      return false;
    }

    WillChange();
    Flush();

    mLockedBits = cairo_image_surface_get_data(surf);
    *aData = mLockedBits;
    *aSize = IntSize(cairo_image_surface_get_width(surf),
                     cairo_image_surface_get_height(surf));
    *aStride = cairo_image_surface_get_stride(surf);
    *aFormat = CairoFormatToGfxFormat(cairo_image_surface_get_format(surf));
    if (aOrigin) {
      *aOrigin = origin;
    }
    return true;
  }

  return false;
}

void DrawTargetCairo::ReleaseBits(uint8_t* aData) {
  MOZ_ASSERT(mLockedBits == aData);
  mLockedBits = nullptr;
  cairo_surface_t* surf = cairo_get_group_target(mContext);
#ifdef CAIRO_HAS_WIN32_SURFACE
  if (cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_WIN32) {
    cairo_surface_t* imgsurf = cairo_win32_surface_get_image(surf);
    if (imgsurf) {
      cairo_surface_mark_dirty(imgsurf);
    }
  }
#endif
  cairo_surface_mark_dirty(surf);
}

void DrawTargetCairo::Flush() {
  cairo_surface_t* surf = cairo_get_group_target(mContext);
  cairo_surface_flush(surf);
}

void DrawTargetCairo::PrepareForDrawing(cairo_t* aContext,
                                        const Path* aPath /* = nullptr */) {
  WillChange(aPath);
}

cairo_surface_t* DrawTargetCairo::GetDummySurface() {
  if (mDummySurface) {
    return mDummySurface;
  }

  mDummySurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);

  return mDummySurface;
}

static void PaintWithAlpha(cairo_t* aContext, const DrawOptions& aOptions) {
  if (aOptions.mCompositionOp == CompositionOp::OP_SOURCE) {
    // Cairo treats the source operator like a lerp when alpha is < 1.
    // Approximate the desired operator by: out = 0; out += src*alpha;
    if (aOptions.mAlpha == 1) {
      cairo_set_operator(aContext, CAIRO_OPERATOR_SOURCE);
      cairo_paint(aContext);
    } else {
      cairo_set_operator(aContext, CAIRO_OPERATOR_CLEAR);
      cairo_paint(aContext);
      cairo_set_operator(aContext, CAIRO_OPERATOR_ADD);
      cairo_paint_with_alpha(aContext, aOptions.mAlpha);
    }
  } else {
    cairo_set_operator(aContext, GfxOpToCairoOp(aOptions.mCompositionOp));
    cairo_paint_with_alpha(aContext, aOptions.mAlpha);
  }
}

void DrawTargetCairo::DrawSurface(SourceSurface* aSurface, const Rect& aDest,
                                  const Rect& aSource,
                                  const DrawSurfaceOptions& aSurfOptions,
                                  const DrawOptions& aOptions) {
  if (mTransformSingular || aDest.IsEmpty()) {
    return;
  }

  if (!IsValid() || !aSurface) {
    gfxCriticalNote << "DrawSurface with bad surface "
                    << cairo_surface_status(cairo_get_group_target(mContext));
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);
  AutoClearDeviceOffset clear(aSurface);

  float sx = aSource.Width() / aDest.Width();
  float sy = aSource.Height() / aDest.Height();

  cairo_matrix_t src_mat;
  cairo_matrix_init_translate(&src_mat, aSource.X() - aSurface->GetRect().x,
                              aSource.Y() - aSurface->GetRect().y);
  cairo_matrix_scale(&src_mat, sx, sy);

  cairo_surface_t* surf = GetCairoSurfaceForSourceSurface(aSurface);
  if (!surf) {
    gfxWarning()
        << "Failed to create cairo surface for DrawTargetCairo::DrawSurface";
    return;
  }
  cairo_pattern_t* pat = cairo_pattern_create_for_surface(surf);
  cairo_surface_destroy(surf);

  cairo_pattern_set_matrix(pat, &src_mat);
  cairo_pattern_set_filter(
      pat, GfxSamplingFilterToCairoFilter(aSurfOptions.mSamplingFilter));
  // For PDF output, we avoid using EXTEND_PAD here because floating-point
  // error accumulation may lead cairo_pdf_surface to conclude that padding
  // is needed due to an apparent one- or two-pixel mismatch between source
  // pattern and destination rect sizes when we're rendering a pdf.js page,
  // and this forces undesirable fallback to the rasterization codepath
  // instead of simply replaying the recording.
  // (See bug 1777209.)
  cairo_pattern_set_extend(
      pat, cairo_surface_get_type(mSurface) == CAIRO_SURFACE_TYPE_PDF
               ? CAIRO_EXTEND_NONE
               : CAIRO_EXTEND_PAD);

  cairo_set_antialias(mContext,
                      GfxAntialiasToCairoAntialias(aOptions.mAntialiasMode));

  // If the destination rect covers the entire clipped area, then unbounded and
  // bounded operations are identical, and we don't need to push a group.
  bool needsGroup = !IsOperatorBoundByMask(aOptions.mCompositionOp) &&
                    !aDest.Contains(GetUserSpaceClip());

  cairo_translate(mContext, aDest.X(), aDest.Y());

  if (needsGroup) {
    cairo_push_group(mContext);
    cairo_new_path(mContext);
    cairo_rectangle(mContext, 0, 0, aDest.Width(), aDest.Height());
    cairo_set_source(mContext, pat);
    cairo_fill(mContext);
    cairo_pop_group_to_source(mContext);
  } else {
    cairo_new_path(mContext);
    cairo_rectangle(mContext, 0, 0, aDest.Width(), aDest.Height());
    cairo_clip(mContext);
    cairo_set_source(mContext, pat);
  }

  PaintWithAlpha(mContext, aOptions);

  cairo_pattern_destroy(pat);
}

void DrawTargetCairo::DrawFilter(FilterNode* aNode, const Rect& aSourceRect,
                                 const Point& aDestPoint,
                                 const DrawOptions& aOptions) {
  FilterNodeSoftware* filter = static_cast<FilterNodeSoftware*>(aNode);
  filter->Draw(this, aSourceRect, aDestPoint, aOptions);
}

void DrawTargetCairo::DrawSurfaceWithShadow(SourceSurface* aSurface,
                                            const Point& aDest,
                                            const ShadowOptions& aShadow,
                                            CompositionOp aOperator) {
  if (!IsValid() || !aSurface) {
    gfxCriticalNote << "DrawSurfaceWithShadow with bad surface "
                    << cairo_surface_status(cairo_get_group_target(mContext));
    return;
  }

  if (aSurface->GetType() != SurfaceType::CAIRO) {
    return;
  }

  AutoClearDeviceOffset clear(aSurface);

  Float width = Float(aSurface->GetSize().width);
  Float height = Float(aSurface->GetSize().height);

  SourceSurfaceCairo* source = static_cast<SourceSurfaceCairo*>(aSurface);
  cairo_surface_t* sourcesurf = source->GetSurface();
  cairo_surface_t* blursurf;
  cairo_surface_t* surf;

  // We only use the A8 surface for blurred shadows. Unblurred shadows can just
  // use the RGBA surface directly.
  if (cairo_surface_get_type(sourcesurf) == CAIRO_SURFACE_TYPE_TEE) {
    blursurf = cairo_tee_surface_index(sourcesurf, 0);
    surf = cairo_tee_surface_index(sourcesurf, 1);
  } else {
    blursurf = sourcesurf;
    surf = sourcesurf;
  }

  if (aShadow.mSigma != 0.0f) {
    MOZ_ASSERT(cairo_surface_get_type(blursurf) == CAIRO_SURFACE_TYPE_IMAGE);
    Rect extents(0, 0, width, height);
    AlphaBoxBlur blur(extents, cairo_image_surface_get_stride(blursurf),
                      aShadow.mSigma, aShadow.mSigma);
    blur.Blur(cairo_image_surface_get_data(blursurf));
  }

  WillChange();
  ClearSurfaceForUnboundedSource(aOperator);

  cairo_save(mContext);
  cairo_set_operator(mContext, GfxOpToCairoOp(aOperator));
  cairo_identity_matrix(mContext);
  cairo_translate(mContext, aDest.x, aDest.y);

  bool needsGroup = !IsOperatorBoundByMask(aOperator);
  if (needsGroup) {
    cairo_push_group(mContext);
  }

  cairo_set_source_rgba(mContext, aShadow.mColor.r, aShadow.mColor.g,
                        aShadow.mColor.b, aShadow.mColor.a);
  cairo_mask_surface(mContext, blursurf, aShadow.mOffset.x, aShadow.mOffset.y);

  if (blursurf != surf || aSurface->GetFormat() != SurfaceFormat::A8) {
    // Now that the shadow has been drawn, we can draw the surface on top.
    cairo_set_source_surface(mContext, surf, 0, 0);
    cairo_new_path(mContext);
    cairo_rectangle(mContext, 0, 0, width, height);
    cairo_fill(mContext);
  }

  if (needsGroup) {
    cairo_pop_group_to_source(mContext);
    cairo_paint(mContext);
  }

  cairo_restore(mContext);
}

void DrawTargetCairo::DrawPattern(const Pattern& aPattern,
                                  const StrokeOptions& aStrokeOptions,
                                  const DrawOptions& aOptions,
                                  DrawPatternType aDrawType,
                                  bool aPathBoundsClip) {
  if (!PatternIsCompatible(aPattern)) {
    return;
  }

  AutoClearDeviceOffset clear(aPattern);

  cairo_pattern_t* pat =
      GfxPatternToCairoPattern(aPattern, aOptions.mAlpha, GetTransform());
  if (!pat) {
    return;
  }
  if (cairo_pattern_status(pat)) {
    cairo_pattern_destroy(pat);
    gfxWarning() << "Invalid pattern";
    return;
  }

  cairo_set_source(mContext, pat);

  cairo_set_antialias(mContext,
                      GfxAntialiasToCairoAntialias(aOptions.mAntialiasMode));

  if (NeedIntermediateSurface(aPattern, aOptions) ||
      (!IsOperatorBoundByMask(aOptions.mCompositionOp) && !aPathBoundsClip)) {
    cairo_push_group_with_content(mContext, CAIRO_CONTENT_COLOR_ALPHA);

    // Don't want operators to be applied twice
    cairo_set_operator(mContext, CAIRO_OPERATOR_OVER);

    if (aDrawType == DRAW_STROKE) {
      SetCairoStrokeOptions(mContext, aStrokeOptions);
      cairo_stroke_preserve(mContext);
    } else {
      cairo_fill_preserve(mContext);
    }

    cairo_pop_group_to_source(mContext);

    // Now draw the content using the desired operator
    PaintWithAlpha(mContext, aOptions);
  } else {
    cairo_set_operator(mContext, GfxOpToCairoOp(aOptions.mCompositionOp));

    if (aDrawType == DRAW_STROKE) {
      SetCairoStrokeOptions(mContext, aStrokeOptions);
      cairo_stroke_preserve(mContext);
    } else {
      cairo_fill_preserve(mContext);
    }
  }

  cairo_pattern_destroy(pat);
}

void DrawTargetCairo::FillRect(const Rect& aRect, const Pattern& aPattern,
                               const DrawOptions& aOptions) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);

  bool restoreTransform = false;
  Matrix mat;
  Rect r = aRect;

  /* Clamp coordinates to work around a design bug in cairo */
  if (r.Width() > CAIRO_COORD_MAX || r.Height() > CAIRO_COORD_MAX ||
      r.X() < -CAIRO_COORD_MAX || r.X() > CAIRO_COORD_MAX ||
      r.Y() < -CAIRO_COORD_MAX || r.Y() > CAIRO_COORD_MAX) {
    if (!mat.IsRectilinear()) {
      gfxWarning() << "DrawTargetCairo::FillRect() misdrawing huge Rect "
                      "with non-rectilinear transform";
    }

    mat = GetTransform();
    r = mat.TransformBounds(r);

    if (!ConditionRect(r)) {
      gfxWarning() << "Ignoring DrawTargetCairo::FillRect() call with "
                      "out-of-bounds Rect";
      return;
    }

    restoreTransform = true;
    SetTransform(Matrix());
  }

  cairo_new_path(mContext);
  cairo_rectangle(mContext, r.X(), r.Y(), r.Width(), r.Height());

  bool pathBoundsClip = false;

  if (r.Contains(GetUserSpaceClip())) {
    pathBoundsClip = true;
  }

  DrawPattern(aPattern, StrokeOptions(), aOptions, DRAW_FILL, pathBoundsClip);

  if (restoreTransform) {
    SetTransform(mat);
  }
}

void DrawTargetCairo::CopySurfaceInternal(cairo_surface_t* aSurface,
                                          const IntRect& aSource,
                                          const IntPoint& aDest) {
  if (cairo_surface_status(aSurface)) {
    gfxWarning() << "Invalid surface" << cairo_surface_status(aSurface);
    return;
  }

  cairo_identity_matrix(mContext);

  cairo_set_source_surface(mContext, aSurface, aDest.x - aSource.X(),
                           aDest.y - aSource.Y());
  cairo_set_operator(mContext, CAIRO_OPERATOR_SOURCE);
  cairo_set_antialias(mContext, CAIRO_ANTIALIAS_NONE);

  cairo_reset_clip(mContext);
  cairo_new_path(mContext);
  cairo_rectangle(mContext, aDest.x, aDest.y, aSource.Width(),
                  aSource.Height());
  cairo_fill(mContext);
}

void DrawTargetCairo::CopySurface(SourceSurface* aSurface,
                                  const IntRect& aSource,
                                  const IntPoint& aDest) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);
  AutoClearDeviceOffset clear(aSurface);

  if (!aSurface) {
    gfxWarning() << "Unsupported surface type specified";
    return;
  }

  cairo_surface_t* surf = GetCairoSurfaceForSourceSurface(aSurface);
  if (!surf) {
    gfxWarning() << "Unsupported surface type specified";
    return;
  }

  CopySurfaceInternal(surf, aSource - aSurface->GetRect().TopLeft(), aDest);
  cairo_surface_destroy(surf);
}

void DrawTargetCairo::CopyRect(const IntRect& aSource, const IntPoint& aDest) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);

  IntRect source = aSource;
  cairo_surface_t* surf = mSurface;

  if (!SupportsSelfCopy(mSurface) && aSource.ContainsY(aDest.y)) {
    cairo_surface_t* similar = cairo_surface_create_similar(
        mSurface, GfxFormatToCairoContent(GetFormat()), aSource.Width(),
        aSource.Height());
    cairo_t* ctx = cairo_create(similar);
    cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(ctx, surf, -aSource.X(), -aSource.Y());
    cairo_paint(ctx);
    cairo_destroy(ctx);

    source.MoveTo(0, 0);
    surf = similar;
  }

  CopySurfaceInternal(surf, source, aDest);

  if (surf != mSurface) {
    cairo_surface_destroy(surf);
  }
}

void DrawTargetCairo::ClearRect(const Rect& aRect) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);

  if (!mContext || aRect.Width() < 0 || aRect.Height() < 0 ||
      !std::isfinite(aRect.X()) || !std::isfinite(aRect.Width()) ||
      !std::isfinite(aRect.Y()) || !std::isfinite(aRect.Height())) {
    gfxCriticalNote << "ClearRect with invalid argument " << gfx::hexa(mContext)
                    << " with " << aRect.Width() << "x" << aRect.Height()
                    << " [" << aRect.X() << ", " << aRect.Y() << "]";
  }

  cairo_set_antialias(mContext, CAIRO_ANTIALIAS_NONE);
  cairo_new_path(mContext);
  cairo_set_operator(mContext, CAIRO_OPERATOR_CLEAR);
  cairo_rectangle(mContext, aRect.X(), aRect.Y(), aRect.Width(),
                  aRect.Height());
  cairo_fill(mContext);
}

void DrawTargetCairo::StrokeRect(
    const Rect& aRect, const Pattern& aPattern,
    const StrokeOptions& aStrokeOptions /* = StrokeOptions() */,
    const DrawOptions& aOptions /* = DrawOptions() */) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);

  cairo_new_path(mContext);
  cairo_rectangle(mContext, aRect.X(), aRect.Y(), aRect.Width(),
                  aRect.Height());

  DrawPattern(aPattern, aStrokeOptions, aOptions, DRAW_STROKE);
}

void DrawTargetCairo::StrokeLine(
    const Point& aStart, const Point& aEnd, const Pattern& aPattern,
    const StrokeOptions& aStrokeOptions /* = StrokeOptions() */,
    const DrawOptions& aOptions /* = DrawOptions() */) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);

  cairo_new_path(mContext);
  cairo_move_to(mContext, aStart.x, aStart.y);
  cairo_line_to(mContext, aEnd.x, aEnd.y);

  DrawPattern(aPattern, aStrokeOptions, aOptions, DRAW_STROKE);
}

void DrawTargetCairo::Stroke(
    const Path* aPath, const Pattern& aPattern,
    const StrokeOptions& aStrokeOptions /* = StrokeOptions() */,
    const DrawOptions& aOptions /* = DrawOptions() */) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext, aPath);

  if (aPath->GetBackendType() != BackendType::CAIRO) return;

  PathCairo* path =
      const_cast<PathCairo*>(static_cast<const PathCairo*>(aPath));
  path->SetPathOnContext(mContext);

  DrawPattern(aPattern, aStrokeOptions, aOptions, DRAW_STROKE);
}

void DrawTargetCairo::Fill(const Path* aPath, const Pattern& aPattern,
                           const DrawOptions& aOptions /* = DrawOptions() */) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext, aPath);

  if (aPath->GetBackendType() != BackendType::CAIRO) return;

  PathCairo* path =
      const_cast<PathCairo*>(static_cast<const PathCairo*>(aPath));
  path->SetPathOnContext(mContext);

  DrawPattern(aPattern, StrokeOptions(), aOptions, DRAW_FILL);
}

bool DrawTargetCairo::IsCurrentGroupOpaque() {
  cairo_surface_t* surf = cairo_get_group_target(mContext);

  if (!surf) {
    return false;
  }

  return cairo_surface_get_content(surf) == CAIRO_CONTENT_COLOR;
}

void DrawTargetCairo::SetFontOptions(cairo_antialias_t aAAMode) {
  //   This will attempt to detect if the currently set scaled font on the
  // context has enabled subpixel AA. If it is not permitted, then it will
  // downgrade to grayscale AA.
  //   This only currently works effectively for the cairo-ft backend relative
  // to system defaults, as only cairo-ft reflect system defaults in the scaled
  // font state. However, this will work for cairo-ft on both tree Cairo and
  // system Cairo.
  //   Other backends leave the CAIRO_ANTIALIAS_DEFAULT setting untouched while
  // potentially interpreting it as subpixel or even other types of AA that
  // can't be safely equivocated with grayscale AA. For this reason we don't
  // try to also detect and modify the default AA setting, only explicit
  // subpixel AA. These other backends must instead rely on tree Cairo's
  // cairo_surface_set_subpixel_antialiasing extension.

  // If allowing subpixel AA, then leave Cairo's default AA state.
  if (mPermitSubpixelAA && aAAMode == CAIRO_ANTIALIAS_DEFAULT) {
    return;
  }

  if (!mFontOptions) {
    mFontOptions = cairo_font_options_create();
    if (!mFontOptions) {
      gfxWarning() << "Failed allocating Cairo font options";
      return;
    }
  }

  cairo_get_font_options(mContext, mFontOptions);
  cairo_antialias_t oldAA = cairo_font_options_get_antialias(mFontOptions);
  cairo_antialias_t newAA =
      aAAMode == CAIRO_ANTIALIAS_DEFAULT ? oldAA : aAAMode;
  // Nothing to change if switching to default AA.
  if (newAA == CAIRO_ANTIALIAS_DEFAULT) {
    return;
  }
  // If the current font requests subpixel AA, force it to gray since we don't
  // allow subpixel AA.
  if (!mPermitSubpixelAA && newAA == CAIRO_ANTIALIAS_SUBPIXEL) {
    newAA = CAIRO_ANTIALIAS_GRAY;
  }
  // Only override old AA with lower levels of AA.
  if (oldAA == CAIRO_ANTIALIAS_DEFAULT || (int)newAA < (int)oldAA) {
    cairo_font_options_set_antialias(mFontOptions, newAA);
    cairo_set_font_options(mContext, mFontOptions);
  }
}

void DrawTargetCairo::SetPermitSubpixelAA(bool aPermitSubpixelAA) {
  DrawTarget::SetPermitSubpixelAA(aPermitSubpixelAA);
  cairo_surface_set_subpixel_antialiasing(
      cairo_get_group_target(mContext),
      aPermitSubpixelAA ? CAIRO_SUBPIXEL_ANTIALIASING_ENABLED
                        : CAIRO_SUBPIXEL_ANTIALIASING_DISABLED);
}

static bool SupportsVariationSettings(cairo_surface_t* surface) {
  switch (cairo_surface_get_type(surface)) {
    case CAIRO_SURFACE_TYPE_PDF:
    case CAIRO_SURFACE_TYPE_PS:
      return false;
    default:
      return true;
  }
}

void DrawTargetCairo::FillGlyphs(ScaledFont* aFont, const GlyphBuffer& aBuffer,
                                 const Pattern& aPattern,
                                 const DrawOptions& aOptions) {
  if (mTransformSingular) {
    return;
  }

  if (!IsValid()) {
    gfxDebug() << "FillGlyphs bad surface "
               << cairo_surface_status(cairo_get_group_target(mContext));
    return;
  }

  cairo_scaled_font_t* cairoScaledFont =
      aFont ? aFont->GetCairoScaledFont() : nullptr;
  if (!cairoScaledFont) {
    gfxDevCrash(LogReason::InvalidFont) << "Invalid scaled font";
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);
  AutoClearDeviceOffset clear(aPattern);

  cairo_set_scaled_font(mContext, cairoScaledFont);

  cairo_pattern_t* pat =
      GfxPatternToCairoPattern(aPattern, aOptions.mAlpha, GetTransform());
  if (!pat) return;

  cairo_set_source(mContext, pat);
  cairo_pattern_destroy(pat);

  cairo_antialias_t aa = GfxAntialiasToCairoAntialias(aOptions.mAntialiasMode);
  cairo_set_antialias(mContext, aa);

  // Override any font-specific options as necessary.
  SetFontOptions(aa);

  // Convert our GlyphBuffer into a vector of Cairo glyphs. This code can
  // execute millions of times in short periods, so we want to avoid heap
  // allocation whenever possible. So we use an inline vector capacity of 1024
  // bytes (the maximum allowed by mozilla::Vector), which gives an inline
  // length of 1024 / 24 = 42 elements, which is enough to typically avoid heap
  // allocation in ~99% of cases.
  Vector<cairo_glyph_t, 1024 / sizeof(cairo_glyph_t)> glyphs;
  if (!glyphs.resizeUninitialized(aBuffer.mNumGlyphs)) {
    gfxDevCrash(LogReason::GlyphAllocFailedCairo) << "glyphs allocation failed";
    return;
  }
  for (uint32_t i = 0; i < aBuffer.mNumGlyphs; ++i) {
    glyphs[i].index = aBuffer.mGlyphs[i].mIndex;
    glyphs[i].x = aBuffer.mGlyphs[i].mPosition.x;
    glyphs[i].y = aBuffer.mGlyphs[i].mPosition.y;
  }

  if (!SupportsVariationSettings(mSurface) && aFont->HasVariationSettings() &&
      StaticPrefs::print_font_variations_as_paths()) {
    cairo_set_fill_rule(mContext, CAIRO_FILL_RULE_WINDING);
    cairo_new_path(mContext);
    cairo_glyph_path(mContext, &glyphs[0], aBuffer.mNumGlyphs);
    cairo_set_operator(mContext, CAIRO_OPERATOR_OVER);
    cairo_fill(mContext);
  } else {
    cairo_show_glyphs(mContext, &glyphs[0], aBuffer.mNumGlyphs);
  }

  if (cairo_surface_status(cairo_get_group_target(mContext))) {
    gfxDebug() << "Ending FillGlyphs with a bad surface "
               << cairo_surface_status(cairo_get_group_target(mContext));
  }
}

void DrawTargetCairo::Mask(const Pattern& aSource, const Pattern& aMask,
                           const DrawOptions& aOptions /* = DrawOptions() */) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);
  AutoClearDeviceOffset clearSource(aSource);
  AutoClearDeviceOffset clearMask(aMask);

  cairo_set_antialias(mContext,
                      GfxAntialiasToCairoAntialias(aOptions.mAntialiasMode));

  cairo_pattern_t* source =
      GfxPatternToCairoPattern(aSource, aOptions.mAlpha, GetTransform());
  if (!source) {
    return;
  }

  cairo_pattern_t* mask =
      GfxPatternToCairoPattern(aMask, aOptions.mAlpha, GetTransform());
  if (!mask) {
    cairo_pattern_destroy(source);
    return;
  }

  if (cairo_pattern_status(source) || cairo_pattern_status(mask)) {
    cairo_pattern_destroy(source);
    cairo_pattern_destroy(mask);
    gfxWarning() << "Invalid pattern";
    return;
  }

  cairo_set_source(mContext, source);
  cairo_set_operator(mContext, GfxOpToCairoOp(aOptions.mCompositionOp));
  cairo_mask(mContext, mask);

  cairo_pattern_destroy(mask);
  cairo_pattern_destroy(source);
}

void DrawTargetCairo::MaskSurface(const Pattern& aSource, SourceSurface* aMask,
                                  Point aOffset, const DrawOptions& aOptions) {
  if (mTransformSingular) {
    return;
  }

  AutoPrepareForDrawing prep(this, mContext);
  AutoClearDeviceOffset clearSource(aSource);
  AutoClearDeviceOffset clearMask(aMask);

  if (!PatternIsCompatible(aSource)) {
    return;
  }

  cairo_set_antialias(mContext,
                      GfxAntialiasToCairoAntialias(aOptions.mAntialiasMode));

  cairo_pattern_t* pat =
      GfxPatternToCairoPattern(aSource, aOptions.mAlpha, GetTransform());
  if (!pat) {
    return;
  }

  if (cairo_pattern_status(pat)) {
    cairo_pattern_destroy(pat);
    gfxWarning() << "Invalid pattern";
    return;
  }

  cairo_set_source(mContext, pat);

  if (NeedIntermediateSurface(aSource, aOptions)) {
    cairo_push_group_with_content(mContext, CAIRO_CONTENT_COLOR_ALPHA);

    // Don't want operators to be applied twice
    cairo_set_operator(mContext, CAIRO_OPERATOR_OVER);

    // Now draw the content using the desired operator
    cairo_paint_with_alpha(mContext, aOptions.mAlpha);

    cairo_pop_group_to_source(mContext);
  }

  cairo_surface_t* surf = GetCairoSurfaceForSourceSurface(aMask);
  if (!surf) {
    cairo_pattern_destroy(pat);
    return;
  }
  cairo_pattern_t* mask = cairo_pattern_create_for_surface(surf);
  cairo_matrix_t matrix;

  cairo_matrix_init_translate(&matrix, -aOffset.x - aMask->GetRect().x,
                              -aOffset.y - aMask->GetRect().y);
  cairo_pattern_set_matrix(mask, &matrix);

  cairo_set_operator(mContext, GfxOpToCairoOp(aOptions.mCompositionOp));

  cairo_mask(mContext, mask);

  cairo_surface_destroy(surf);
  cairo_pattern_destroy(mask);
  cairo_pattern_destroy(pat);
}

void DrawTargetCairo::PushClip(const Path* aPath) {
  if (aPath->GetBackendType() != BackendType::CAIRO) {
    return;
  }

  WillChange(aPath);
  cairo_save(mContext);

  PathCairo* path =
      const_cast<PathCairo*>(static_cast<const PathCairo*>(aPath));

  if (mTransformSingular) {
    cairo_new_path(mContext);
    cairo_rectangle(mContext, 0, 0, 0, 0);
  } else {
    path->SetPathOnContext(mContext);
  }
  cairo_clip_preserve(mContext);

  ++mClipDepth;
}

void DrawTargetCairo::PushClipRect(const Rect& aRect) {
  WillChange();
  cairo_save(mContext);

  cairo_new_path(mContext);
  if (mTransformSingular) {
    cairo_rectangle(mContext, 0, 0, 0, 0);
  } else {
    cairo_rectangle(mContext, aRect.X(), aRect.Y(), aRect.Width(),
                    aRect.Height());
  }
  cairo_clip_preserve(mContext);

  ++mClipDepth;
}

void DrawTargetCairo::PopClip() {
  if (NS_WARN_IF(mClipDepth <= 0)) {
    return;
  }

  // save/restore does not affect the path, so no need to call WillChange()

  // cairo_restore will restore the transform too and we don't want to do that
  // so we'll save it now and restore it after the cairo_restore
  cairo_matrix_t mat;
  cairo_get_matrix(mContext, &mat);

  cairo_restore(mContext);

  cairo_set_matrix(mContext, &mat);

  --mClipDepth;
}

bool DrawTargetCairo::RemoveAllClips() {
  while (mClipDepth > 0) {
    PopClip();
  }
  return true;
}

void DrawTargetCairo::PushLayer(bool aOpaque, Float aOpacity,
                                SourceSurface* aMask,
                                const Matrix& aMaskTransform,
                                const IntRect& aBounds, bool aCopyBackground) {
  PushLayerWithBlend(aOpaque, aOpacity, aMask, aMaskTransform, aBounds,
                     aCopyBackground, CompositionOp::OP_OVER);
}

void DrawTargetCairo::PushLayerWithBlend(bool aOpaque, Float aOpacity,
                                         SourceSurface* aMask,
                                         const Matrix& aMaskTransform,
                                         const IntRect& aBounds,
                                         bool aCopyBackground,
                                         CompositionOp aCompositionOp) {
  cairo_content_t content = CAIRO_CONTENT_COLOR_ALPHA;

  if (mFormat == SurfaceFormat::A8) {
    content = CAIRO_CONTENT_ALPHA;
  } else if (aOpaque) {
    content = CAIRO_CONTENT_COLOR;
  }

  if (aCopyBackground) {
    cairo_surface_t* source = cairo_get_group_target(mContext);
    cairo_push_group_with_content(mContext, content);
    cairo_surface_t* dest = cairo_get_group_target(mContext);
    cairo_t* ctx = cairo_create(dest);
    cairo_set_source_surface(ctx, source, 0, 0);
    cairo_set_operator(ctx, CAIRO_OPERATOR_SOURCE);
    cairo_paint(ctx);
    cairo_destroy(ctx);
  } else {
    cairo_push_group_with_content(mContext, content);
  }

  PushedLayer layer(aOpacity, aCompositionOp, mPermitSubpixelAA);

  if (aMask) {
    cairo_surface_t* surf = GetCairoSurfaceForSourceSurface(aMask);
    if (surf) {
      layer.mMaskPattern = cairo_pattern_create_for_surface(surf);
      Matrix maskTransform = aMaskTransform;
      maskTransform.PreTranslate(aMask->GetRect().X(), aMask->GetRect().Y());
      cairo_matrix_t mat;
      GfxMatrixToCairoMatrix(maskTransform, mat);
      cairo_matrix_invert(&mat);
      cairo_pattern_set_matrix(layer.mMaskPattern, &mat);
      cairo_surface_destroy(surf);
    } else {
      gfxCriticalError() << "Failed to get cairo surface for mask surface!";
    }
  }

  mPushedLayers.push_back(layer);

  SetPermitSubpixelAA(aOpaque);
}

void DrawTargetCairo::PopLayer() {
  MOZ_RELEASE_ASSERT(!mPushedLayers.empty());

  cairo_set_operator(mContext, CAIRO_OPERATOR_OVER);

  cairo_pop_group_to_source(mContext);

  PushedLayer layer = mPushedLayers.back();
  mPushedLayers.pop_back();

  if (!layer.mMaskPattern) {
    cairo_set_operator(mContext, GfxOpToCairoOp(layer.mCompositionOp));
    cairo_paint_with_alpha(mContext, layer.mOpacity);
  } else {
    if (layer.mOpacity != Float(1.0)) {
      cairo_push_group_with_content(mContext, CAIRO_CONTENT_COLOR_ALPHA);

      // Now draw the content using the desired operator
      cairo_paint_with_alpha(mContext, layer.mOpacity);

      cairo_pop_group_to_source(mContext);
    }
    cairo_set_operator(mContext, GfxOpToCairoOp(layer.mCompositionOp));
    cairo_mask(mContext, layer.mMaskPattern);
  }

  cairo_matrix_t mat;
  GfxMatrixToCairoMatrix(mTransform, mat);
  cairo_set_matrix(mContext, &mat);

  cairo_set_operator(mContext, CAIRO_OPERATOR_OVER);

  cairo_pattern_destroy(layer.mMaskPattern);
  SetPermitSubpixelAA(layer.mWasPermittingSubpixelAA);
}

void DrawTargetCairo::ClearSurfaceForUnboundedSource(
    const CompositionOp& aOperator) {
  if (aOperator != CompositionOp::OP_SOURCE) return;
  cairo_set_operator(mContext, CAIRO_OPERATOR_CLEAR);
  // It doesn't really matter what the source is here, since Paint
  // isn't bounded by the source and the mask covers the entire clip
  // region.
  cairo_paint(mContext);
}

already_AddRefed<GradientStops> DrawTargetCairo::CreateGradientStops(
    GradientStop* aStops, uint32_t aNumStops, ExtendMode aExtendMode) const {
  return MakeAndAddRef<GradientStopsCairo>(aStops, aNumStops, aExtendMode);
}

already_AddRefed<FilterNode> DrawTargetCairo::CreateFilter(FilterType aType) {
  return FilterNodeSoftware::Create(aType);
}

already_AddRefed<SourceSurface> DrawTargetCairo::CreateSourceSurfaceFromData(
    unsigned char* aData, const IntSize& aSize, int32_t aStride,
    SurfaceFormat aFormat) const {
  if (!aData) {
    gfxWarning() << "DrawTargetCairo::CreateSourceSurfaceFromData null aData";
    return nullptr;
  }

  cairo_surface_t* surf =
      CopyToImageSurface(aData, IntRect(IntPoint(), aSize), aStride, aFormat);
  if (!surf) {
    return nullptr;
  }

  RefPtr<SourceSurfaceCairo> source_surf =
      new SourceSurfaceCairo(surf, aSize, aFormat);
  cairo_surface_destroy(surf);

  return source_surf.forget();
}

already_AddRefed<SourceSurface> DrawTargetCairo::OptimizeSourceSurface(
    SourceSurface* aSurface) const {
  RefPtr<SourceSurface> surface(aSurface);
  return surface.forget();
}

already_AddRefed<SourceSurface>
DrawTargetCairo::CreateSourceSurfaceFromNativeSurface(
    const NativeSurface& aSurface) const {
  return nullptr;
}

already_AddRefed<DrawTarget> DrawTargetCairo::CreateSimilarDrawTarget(
    const IntSize& aSize, SurfaceFormat aFormat) const {
  if (cairo_surface_status(cairo_get_group_target(mContext))) {
    RefPtr<DrawTargetCairo> target = new DrawTargetCairo();
    if (target->Init(aSize, aFormat)) {
      return target.forget();
    }
  }

  cairo_surface_t* similar;
  switch (cairo_surface_get_type(mSurface)) {
#ifdef CAIRO_HAS_WIN32_SURFACE
    case CAIRO_SURFACE_TYPE_WIN32:
      similar = cairo_win32_surface_create_with_dib(
          GfxFormatToCairoFormat(aFormat), aSize.width, aSize.height);
      break;
#endif
#ifdef CAIRO_HAS_QUARTZ_SURFACE
    case CAIRO_SURFACE_TYPE_QUARTZ:
      if (StaticPrefs::gfx_cairo_quartz_cg_layer_enabled()) {
        similar = cairo_quartz_surface_create_cg_layer(
            mSurface, GfxFormatToCairoContent(aFormat), aSize.width,
            aSize.height);
        break;
      }
      [[fallthrough]];
#endif
    default:
      similar = cairo_surface_create_similar(mSurface,
                                             GfxFormatToCairoContent(aFormat),
                                             aSize.width, aSize.height);
      break;
  }

  if (!cairo_surface_status(similar)) {
    RefPtr<DrawTargetCairo> target = new DrawTargetCairo();
    if (target->InitAlreadyReferenced(similar, aSize)) {
      return target.forget();
    }
  }

  gfxCriticalError(
      CriticalLog::DefaultOptions(Factory::ReasonableSurfaceSize(aSize)))
      << "Failed to create similar cairo surface! Size: " << aSize
      << " Status: " << cairo_surface_status(similar)
      << cairo_surface_status(cairo_get_group_target(mContext)) << " format "
      << (int)aFormat;
  cairo_surface_destroy(similar);

  return nullptr;
}

RefPtr<DrawTarget> DrawTargetCairo::CreateClippedDrawTarget(
    const Rect& aBounds, SurfaceFormat aFormat) {
  RefPtr<DrawTarget> result;
  // Doing this save()/restore() dance is wasteful
  cairo_save(mContext);

  if (!aBounds.IsEmpty()) {
    cairo_new_path(mContext);
    cairo_rectangle(mContext, aBounds.X(), aBounds.Y(), aBounds.Width(),
                    aBounds.Height());
    cairo_clip(mContext);
  }
  cairo_identity_matrix(mContext);
  IntRect clipBounds = IntRect::RoundOut(GetUserSpaceClip());
  if (!clipBounds.IsEmpty()) {
    RefPtr<DrawTarget> dt = CreateSimilarDrawTarget(
        IntSize(clipBounds.width, clipBounds.height), aFormat);
    if (dt) {
      result = gfx::Factory::CreateOffsetDrawTarget(
          dt, IntPoint(clipBounds.x, clipBounds.y));
      if (result) {
        result->SetTransform(mTransform);
      }
    }
  } else {
    // Everything is clipped but we still want some kind of surface
    result = CreateSimilarDrawTarget(IntSize(1, 1), aFormat);
  }

  cairo_restore(mContext);
  return result;
}
bool DrawTargetCairo::InitAlreadyReferenced(cairo_surface_t* aSurface,
                                            const IntSize& aSize,
                                            SurfaceFormat* aFormat) {
  if (cairo_surface_status(aSurface)) {
    gfxCriticalNote << "Attempt to create DrawTarget for invalid surface. "
                    << aSize
                    << " Cairo Status: " << cairo_surface_status(aSurface);
    cairo_surface_destroy(aSurface);
    return false;
  }

  mContext = cairo_create(aSurface);
  mSurface = aSurface;
  mSize = aSize;
  mFormat = aFormat ? *aFormat : GfxFormatForCairoSurface(aSurface);

  // Cairo image surface have a bug where they will allocate a mask surface (for
  // clipping) the size of the clip extents, and don't take the surface extents
  // into account. Add a manual clip to the surface extents to prevent this.
  cairo_new_path(mContext);
  cairo_rectangle(mContext, 0, 0, mSize.width, mSize.height);
  cairo_clip(mContext);

  if (mFormat == SurfaceFormat::A8R8G8B8_UINT32 ||
      mFormat == SurfaceFormat::R8G8B8A8) {
    SetPermitSubpixelAA(false);
  } else {
    SetPermitSubpixelAA(true);
  }

  return true;
}

already_AddRefed<DrawTarget> DrawTargetCairo::CreateShadowDrawTarget(
    const IntSize& aSize, SurfaceFormat aFormat, float aSigma) const {
  cairo_surface_t* similar = cairo_surface_create_similar(
      cairo_get_target(mContext), GfxFormatToCairoContent(aFormat), aSize.width,
      aSize.height);

  if (cairo_surface_status(similar)) {
    return nullptr;
  }

  // If we don't have a blur then we can use the RGBA mask and keep all the
  // operations in graphics memory.
  if (aSigma == 0.0f || aFormat == SurfaceFormat::A8) {
    RefPtr<DrawTargetCairo> target = new DrawTargetCairo();
    if (target->InitAlreadyReferenced(similar, aSize)) {
      return target.forget();
    } else {
      return nullptr;
    }
  }

  cairo_surface_t* blursurf =
      cairo_image_surface_create(CAIRO_FORMAT_A8, aSize.width, aSize.height);

  if (cairo_surface_status(blursurf)) {
    return nullptr;
  }

  cairo_surface_t* tee = cairo_tee_surface_create(blursurf);
  cairo_surface_destroy(blursurf);
  if (cairo_surface_status(tee)) {
    cairo_surface_destroy(similar);
    return nullptr;
  }

  cairo_tee_surface_add(tee, similar);
  cairo_surface_destroy(similar);

  RefPtr<DrawTargetCairo> target = new DrawTargetCairo();
  if (target->InitAlreadyReferenced(tee, aSize)) {
    return target.forget();
  }
  return nullptr;
}

bool DrawTargetCairo::Draw3DTransformedSurface(SourceSurface* aSurface,
                                               const Matrix4x4& aMatrix) {
  return DrawTarget::Draw3DTransformedSurface(aSurface, aMatrix);
}

bool DrawTargetCairo::Init(cairo_surface_t* aSurface, const IntSize& aSize,
                           SurfaceFormat* aFormat) {
  cairo_surface_reference(aSurface);
  return InitAlreadyReferenced(aSurface, aSize, aFormat);
}

bool DrawTargetCairo::Init(const IntSize& aSize, SurfaceFormat aFormat) {
  cairo_surface_t* surf = cairo_image_surface_create(
      GfxFormatToCairoFormat(aFormat), aSize.width, aSize.height);
  return InitAlreadyReferenced(surf, aSize);
}

bool DrawTargetCairo::Init(unsigned char* aData, const IntSize& aSize,
                           int32_t aStride, SurfaceFormat aFormat) {
  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      aData, GfxFormatToCairoFormat(aFormat), aSize.width, aSize.height,
      aStride);
  return InitAlreadyReferenced(surf, aSize);
}

void* DrawTargetCairo::GetNativeSurface(NativeSurfaceType aType) {
  if (aType == NativeSurfaceType::CAIRO_CONTEXT) {
    return mContext;
  }

  return nullptr;
}

void DrawTargetCairo::MarkSnapshotIndependent() {
  if (mSnapshot) {
    if (mSnapshot->refCount() > 1) {
      // We only need to worry about snapshots that someone else knows about
      mSnapshot->DrawTargetWillChange();
    }
    mSnapshot = nullptr;
  }
}

void DrawTargetCairo::WillChange(const Path* aPath /* = nullptr */) {
  MarkSnapshotIndependent();
  MOZ_ASSERT(!mLockedBits);
}

void DrawTargetCairo::SetTransform(const Matrix& aTransform) {
  DrawTarget::SetTransform(aTransform);

  mTransformSingular = aTransform.IsSingular();
  if (!mTransformSingular) {
    cairo_matrix_t mat;
    GfxMatrixToCairoMatrix(mTransform, mat);
    cairo_set_matrix(mContext, &mat);
  }
}

Rect DrawTargetCairo::GetUserSpaceClip() const {
  double clipX1, clipY1, clipX2, clipY2;
  cairo_clip_extents(mContext, &clipX1, &clipY1, &clipX2, &clipY2);
  return Rect(clipX1, clipY1, clipX2 - clipX1,
              clipY2 - clipY1);  // Narrowing of doubles to floats
}

#ifdef MOZ_X11
bool BorrowedXlibDrawable::Init(DrawTarget* aDT) {
  MOZ_ASSERT(aDT, "Caller should check for nullptr");
  MOZ_ASSERT(!mDT, "Can't initialize twice!");
  mDT = aDT;
  mDrawable = X11None;

#  ifdef CAIRO_HAS_XLIB_SURFACE
  if (aDT->GetBackendType() != BackendType::CAIRO || aDT->IsTiledDrawTarget()) {
    return false;
  }

  DrawTargetCairo* cairoDT = static_cast<DrawTargetCairo*>(aDT);
  cairo_surface_t* surf = cairo_get_group_target(cairoDT->mContext);
  if (cairo_surface_get_type(surf) != CAIRO_SURFACE_TYPE_XLIB) {
    return false;
  }
  cairo_surface_flush(surf);

  cairoDT->WillChange();

  mDisplay = cairo_xlib_surface_get_display(surf);
  mDrawable = cairo_xlib_surface_get_drawable(surf);
  mScreen = cairo_xlib_surface_get_screen(surf);
  mVisual = cairo_xlib_surface_get_visual(surf);
  mSize.width = cairo_xlib_surface_get_width(surf);
  mSize.height = cairo_xlib_surface_get_height(surf);

  double x = 0, y = 0;
  cairo_surface_get_device_offset(surf, &x, &y);
  mOffset = Point(x, y);

  return true;
#  else
  return false;
#  endif
}

void BorrowedXlibDrawable::Finish() {
  DrawTargetCairo* cairoDT = static_cast<DrawTargetCairo*>(mDT);
  cairo_surface_t* surf = cairo_get_group_target(cairoDT->mContext);
  cairo_surface_mark_dirty(surf);
  if (mDrawable) {
    mDrawable = X11None;
  }
}
#endif

}  // namespace gfx
}  // namespace mozilla
