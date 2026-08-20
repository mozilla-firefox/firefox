/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "OrientedImage.h"

#include "ImageRegion.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/SVGImageContext.h"

using std::swap;

namespace mozilla {

using namespace gfx;

namespace image {

NS_IMETHODIMP
OrientedImage::GetWidth(int32_t* aWidth) {
  if (mOrientation.SwapsWidthAndHeight()) {
    return InnerImage()->GetHeight(aWidth);
  } else {
    return InnerImage()->GetWidth(aWidth);
  }
}

NS_IMETHODIMP
OrientedImage::GetHeight(int32_t* aHeight) {
  if (mOrientation.SwapsWidthAndHeight()) {
    return InnerImage()->GetWidth(aHeight);
  } else {
    return InnerImage()->GetHeight(aHeight);
  }
}

nsresult OrientedImage::GetNativeSizes(nsTArray<IntSize>& aNativeSizes) {
  nsresult rv = InnerImage()->GetNativeSizes(aNativeSizes);

  if (mOrientation.SwapsWidthAndHeight()) {
    auto i = aNativeSizes.Length();
    while (i > 0) {
      --i;
      swap(aNativeSizes[i].width, aNativeSizes[i].height);
    }
  }

  return rv;
}

NS_IMETHODIMP
OrientedImage::GetIntrinsicSizeInAppUnits(nsSize* aSize) {
  nsresult rv = InnerImage()->GetIntrinsicSizeInAppUnits(aSize);

  if (mOrientation.SwapsWidthAndHeight()) {
    swap(aSize->width, aSize->height);
  }

  return rv;
}

AspectRatio OrientedImage::GetIntrinsicRatio() {
  AspectRatio ratio = InnerImage()->GetIntrinsicRatio();
  if (ratio && mOrientation.SwapsWidthAndHeight()) {
    ratio = ratio.Inverted();
  }
  return ratio;
}

already_AddRefed<SourceSurface> OrientedImage::OrientSurface(
    Orientation aOrientation, SourceSurface* aSurface) {
  MOZ_ASSERT(aSurface);

  // If the image does not require any re-orientation, return aSurface itself.
  if (aOrientation.IsIdentity()) {
    return do_AddRef(aSurface);
  }

  // Determine the size of the new surface.
  nsIntSize originalSize = aSurface->GetSize();
  nsIntSize targetSize = originalSize;
  if (aOrientation.SwapsWidthAndHeight()) {
    swap(targetSize.width, targetSize.height);
  }

  // Create our drawable.
  auto drawable = MakeRefPtr<gfxSurfaceDrawable>(aSurface, originalSize);

  // Determine an appropriate format for the surface.
  gfx::SurfaceFormat surfaceFormat = IsOpaque(aSurface->GetFormat())
                                         ? gfx::SurfaceFormat::OS_RGBX
                                         : gfx::SurfaceFormat::OS_RGBA;

  // Create the new surface to draw into.
  RefPtr<DrawTarget> target =
      gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
          targetSize, surfaceFormat);
  if (!target || !target->IsValid()) {
    NS_ERROR("Could not create a DrawTarget");
    return nullptr;
  }

  // Draw.
  gfxContext ctx(target);

  ctx.Multiply(OrientationMatrix(aOrientation, originalSize));
  gfxUtils::DrawPixelSnapped(&ctx, drawable, SizeDouble(originalSize),
                             ImageRegion::Create(originalSize), surfaceFormat,
                             SamplingFilter::LINEAR);

  return target->Snapshot();
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
OrientedImage::GetFrame(uint32_t aWhichFrame, uint32_t aFlags) {
  // Get a SourceSurface for the inner image then orient it according to
  // mOrientation.
  RefPtr<SourceSurface> innerSurface =
      InnerImage()->GetFrame(aWhichFrame, aFlags);
  NS_ENSURE_TRUE(innerSurface, nullptr);

  return OrientSurface(mOrientation, innerSurface);
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
OrientedImage::GetFrameAtSize(const IntSize& aSize, uint32_t aWhichFrame,
                              uint32_t aFlags) {
  // Get a SourceSurface for the inner image then orient it according to
  // mOrientation.
  IntSize innerSize = aSize;
  if (mOrientation.SwapsWidthAndHeight()) {
    swap(innerSize.width, innerSize.height);
  }
  RefPtr<SourceSurface> innerSurface =
      InnerImage()->GetFrameAtSize(innerSize, aWhichFrame, aFlags);
  NS_ENSURE_TRUE(innerSurface, nullptr);

  return OrientSurface(mOrientation, innerSurface);
}

NS_IMETHODIMP_(bool)
OrientedImage::IsImageContainerAvailable(WindowRenderer* aRenderer,
                                         uint32_t aFlags) {
  if (mOrientation.IsIdentity()) {
    return InnerImage()->IsImageContainerAvailable(aRenderer, aFlags);
  }
  return false;
}

NS_IMETHODIMP_(ImgDrawResult)
OrientedImage::GetImageProvider(WindowRenderer* aRenderer,
                                const gfx::IntSize& aSize,
                                const SVGImageContext& aSVGContext,
                                const Maybe<ImageIntRegion>& aRegion,
                                uint32_t aFlags,
                                WebRenderImageProvider** aProvider) {
  // XXX(seth): We currently don't have a way of orienting the result of
  // GetImageContainer. We work around this by always returning null, but if it
  // ever turns out that OrientedImage is widely used on codepaths that can
  // actually benefit from GetImageContainer, it would be a good idea to fix
  // that method for performance reasons.

  if (mOrientation.IsIdentity()) {
    return InnerImage()->GetImageProvider(aRenderer, aSize, aSVGContext,
                                          aRegion, aFlags, aProvider);
  }

  return ImgDrawResult::NOT_SUPPORTED;
}

struct MatrixBuilder {
  explicit MatrixBuilder(bool aInvert) : mInvert(aInvert) {}

  gfxMatrix Build() { return mMatrix; }

  void Scale(gfxFloat aX, gfxFloat aY) {
    if (mInvert) {
      mMatrix *= gfxMatrix::Scaling(1.0 / aX, 1.0 / aY);
    } else {
      mMatrix.PreScale(aX, aY);
    }
  }

  void Rotate(gfxFloat aPhi) {
    if (mInvert) {
      mMatrix *= gfxMatrix::Rotation(-aPhi);
    } else {
      mMatrix.PreRotate(aPhi);
    }
  }

  void Translate(gfxPoint aDelta) {
    if (mInvert) {
      mMatrix *= gfxMatrix::Translation(-aDelta);
    } else {
      mMatrix.PreTranslate(aDelta);
    }
  }

 private:
  gfxMatrix mMatrix;
  bool mInvert;
};

gfxMatrix OrientedImage::OrientationMatrix(Orientation aOrientation,
                                           const nsIntSize& aSize,
                                           bool aInvert /* = false */) {
  MatrixBuilder builder(aInvert);

  // Apply reflection, if present. (For a regular, non-flipFirst reflection,
  // this logically happens second, but we apply it first because these
  // transformations are all premultiplied.) A translation is necessary to place
  // the image back in the first quadrant.
  if (aOrientation.flip == Flip::Horizontal && !aOrientation.flipFirst) {
    if (aOrientation.SwapsWidthAndHeight()) {
      builder.Translate(gfxPoint(aSize.height, 0));
    } else {
      builder.Translate(gfxPoint(aSize.width, 0));
    }
    builder.Scale(-1.0, 1.0);
  }

  // Apply rotation, if present. Again, a translation is used to place the
  // image back in the first quadrant.
  switch (aOrientation.rotation) {
    case Angle::D0:
      break;
    case Angle::D90:
      builder.Translate(gfxPoint(aSize.height, 0));
      builder.Rotate(-1.5 * M_PI);
      break;
    case Angle::D180:
      builder.Translate(gfxPoint(aSize.width, aSize.height));
      builder.Rotate(-1.0 * M_PI);
      break;
    case Angle::D270:
      builder.Translate(gfxPoint(0, aSize.width));
      builder.Rotate(-0.5 * M_PI);
      break;
    default:
      MOZ_ASSERT(false, "Invalid rotation value");
  }

  // Apply a flipFirst reflection.
  if (aOrientation.flip == Flip::Horizontal && aOrientation.flipFirst) {
    builder.Translate(gfxPoint(aSize.width, 0.0));
    builder.Scale(-1.0, 1.0);
  }

  return builder.Build();
}

NS_IMETHODIMP_(ImgDrawResult)
OrientedImage::Draw(gfxContext* aContext, const nsIntSize& aSize,
                    const ImageRegion& aRegion, uint32_t aWhichFrame,
                    SamplingFilter aSamplingFilter,
                    const SVGImageContext& aSVGContext, uint32_t aFlags,
                    float aOpacity) {
  if (mOrientation.IsIdentity()) {
    return InnerImage()->Draw(aContext, aSize, aRegion, aWhichFrame,
                              aSamplingFilter, aSVGContext, aFlags, aOpacity);
  }

  // Update the image size to match the image's coordinate system. (This could
  // be done using TransformBounds but since it's only a size a swap is enough.)
  nsIntSize size(aSize);
  if (mOrientation.SwapsWidthAndHeight()) {
    swap(size.width, size.height);
  }

  // Update the matrix so that we transform the image into the orientation
  // expected by the caller before drawing.
  gfxMatrix matrix(OrientationMatrix(size));
  gfxContextMatrixAutoSaveRestore saveMatrix(aContext);
  aContext->Multiply(matrix);

  // The region is already in the orientation expected by the caller, but we
  // need it to be in the image's coordinate system, so we transform it using
  // the inverse of the orientation matrix.
  gfxMatrix inverseMatrix(OrientationMatrix(size, /* aInvert = */ true));
  ImageRegion region(aRegion);
  region.TransformBoundsBy(inverseMatrix);

  auto orientViewport = [&](const SVGImageContext& aOldContext) {
    SVGImageContext context(aOldContext);
    auto oldViewport = aOldContext.GetViewportSize();
    if (oldViewport && mOrientation.SwapsWidthAndHeight()) {
      // Swap width and height:
      CSSSize newViewport(oldViewport->height, oldViewport->width);
      context.SetViewportSize(Some(newViewport));
    }
    return context;
  };

  return InnerImage()->Draw(aContext, size, region, aWhichFrame,
                            aSamplingFilter, orientViewport(aSVGContext),
                            aFlags, aOpacity);
}

nsIntSize OrientedImage::OptimalImageSizeForDest(const gfxSize& aDest,
                                                 uint32_t aWhichFrame,
                                                 SamplingFilter aSamplingFilter,
                                                 uint32_t aFlags) {
  if (!mOrientation.SwapsWidthAndHeight()) {
    return InnerImage()->OptimalImageSizeForDest(aDest, aWhichFrame,
                                                 aSamplingFilter, aFlags);
  }

  // Swap the size for the calculation, then swap it back for the caller.
  gfxSize destSize(aDest.height, aDest.width);
  nsIntSize innerImageSize(InnerImage()->OptimalImageSizeForDest(
      destSize, aWhichFrame, aSamplingFilter, aFlags));
  return nsIntSize(innerImageSize.height, innerImageSize.width);
}

NS_IMETHODIMP_(nsIntRect)
OrientedImage::GetImageSpaceInvalidationRect(const nsIntRect& aRect) {
  nsIntRect rect(InnerImage()->GetImageSpaceInvalidationRect(aRect));

  if (mOrientation.IsIdentity()) {
    return rect;
  }

  nsIntSize innerSize;
  nsresult rv = InnerImage()->GetWidth(&innerSize.width);
  rv = NS_FAILED(rv) ? rv : InnerImage()->GetHeight(&innerSize.height);
  if (NS_FAILED(rv)) {
    // Fall back to identity if the width and height aren't available.
    return rect;
  }

  // Transform the invalidation rect into the correct orientation.
  gfxMatrix matrix(OrientationMatrix(innerSize));
  gfxRect invalidRect(matrix.TransformBounds(
      gfxRect(rect.X(), rect.Y(), rect.Width(), rect.Height())));

  return IntRect::RoundOut(invalidRect.X(), invalidRect.Y(),
                           invalidRect.Width(), invalidRect.Height());
}

}  // namespace image
}  // namespace mozilla
