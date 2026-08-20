/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_SVGDrawingParameters_h
#define mozilla_image_SVGDrawingParameters_h

#include "ImageRegion.h"
#include "gfxContext.h"
#include "gfxTypes.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "nsSize.h"

namespace mozilla {
namespace image {

struct SVGDrawingParameters {
  typedef mozilla::gfx::IntSize IntSize;
  typedef mozilla::gfx::SamplingFilter SamplingFilter;

  SVGDrawingParameters(gfxContext* aContext, const nsIntSize& aRasterSize,
                       const nsIntSize& aDrawSize, const ImageRegion& aRegion,
                       SamplingFilter aSamplingFilter,
                       const SVGImageContext& aSVGContext, float aAnimationTime,
                       uint32_t aFlags, float aOpacity)
      : context(aContext),
        size(aRasterSize),
        drawSize(aDrawSize),
        region(aRegion),
        samplingFilter(aSamplingFilter),
        svgContext(aSVGContext),
        viewportSize(aRasterSize.width, aRasterSize.height),
        animationTime(aAnimationTime),
        flags(aFlags),
        opacity(aOpacity) {
    if (auto sz = aSVGContext.GetViewportSize()) {
      viewportSize = *sz;
    }
  }

  gfxContext* context;
  IntSize size;      // Size to rasterize a surface at.
  IntSize drawSize;  // Size to draw the given surface at.
  ImageRegion region;
  SamplingFilter samplingFilter;
  const SVGImageContext& svgContext;
  CSSSize viewportSize;
  float animationTime;
  uint32_t flags;
  gfxFloat opacity;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_SVGDrawingParameters_h
