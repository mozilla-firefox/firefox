/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_SVGDrawingCallback_h
#define mozilla_image_SVGDrawingCallback_h

#include "Units.h"
#include "gfxDrawable.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"

namespace mozilla {
namespace image {
class SVGDocumentWrapper;

class SVGDrawingCallback final : public gfxDrawingCallback {
 public:
  SVGDrawingCallback(SVGDocumentWrapper* aSVGDocumentWrapper,
                     const CSSSize& aViewportSize, const gfx::IntSize& aSize,
                     uint32_t aImageFlags);

  ~SVGDrawingCallback();

  bool operator()(gfxContext* aContext, const gfxRect& aFillRect,
                  const gfx::SamplingFilter aSamplingFilter,
                  const gfxMatrix& aTransform) override;

 private:
  RefPtr<SVGDocumentWrapper> mSVGDocumentWrapper;
  const CSSSize mViewportSize;
  const gfx::IntSize mSize;
  uint32_t mImageFlags;
};

}  // namespace image
}  // namespace mozilla

#endif  // mozilla_image_SVGDrawingCallback_h
