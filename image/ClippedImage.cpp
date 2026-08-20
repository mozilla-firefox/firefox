/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClippedImage.h"

#include <algorithm>
#include <utility>

#include "ImageRegion.h"
#include "Orientation.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/gfx/2D.h"
#include "nsPresContext.h"

namespace mozilla {

using namespace gfx;
using std::max;

namespace image {

class ClippedImageCachedSurface {
 public:
  ClippedImageCachedSurface(already_AddRefed<SourceSurface> aSurface,
                            const nsIntSize& aSize,
                            const SVGImageContext& aSVGContext, float aFrame,
                            uint32_t aFlags, ImgDrawResult aDrawResult)
      : mSurface(aSurface),
        mSize(aSize),
        mSVGContext(aSVGContext),
        mFrame(aFrame),
        mFlags(aFlags),
        mDrawResult(aDrawResult) {
    MOZ_ASSERT(mSurface, "Must have a valid surface");
  }

  bool Matches(const nsIntSize& aSize, const SVGImageContext& aSVGContext,
               float aFrame, uint32_t aFlags) const {
    return mSize == aSize && mSVGContext == aSVGContext && mFrame == aFrame &&
           mFlags == aFlags;
  }

  already_AddRefed<SourceSurface> Surface() const {
    RefPtr<SourceSurface> surf(mSurface);
    return surf.forget();
  }

  ImgDrawResult GetDrawResult() const { return mDrawResult; }

  bool NeedsRedraw() const {
    return mDrawResult != ImgDrawResult::SUCCESS &&
           mDrawResult != ImgDrawResult::BAD_IMAGE;
  }

 private:
  RefPtr<SourceSurface> mSurface;
  const nsIntSize mSize;
  SVGImageContext mSVGContext;
  const float mFrame;
  const uint32_t mFlags;
  const ImgDrawResult mDrawResult;
};

class DrawSingleTileCallback : public gfxDrawingCallback {
 public:
  DrawSingleTileCallback(ClippedImage* aImage, const nsIntSize& aSize,
                         const SVGImageContext& aSVGContext,
                         uint32_t aWhichFrame, uint32_t aFlags, float aOpacity)
      : mImage(aImage),
        mSize(aSize),
        mSVGContext(aSVGContext),
        mWhichFrame(aWhichFrame),
        mFlags(aFlags),
        mDrawResult(ImgDrawResult::NOT_READY),
        mOpacity(aOpacity) {
    MOZ_ASSERT(mImage, "Must have an image to clip");
  }

  virtual bool operator()(gfxContext* aContext, const gfxRect& aFillRect,
                          const SamplingFilter aSamplingFilter,
                          const gfxMatrix& aTransform) override {
    MOZ_ASSERT(aTransform.IsIdentity(),
               "Caller is probably CreateSamplingRestrictedDrawable, "
               "which should not happen");

    // Draw the image. |gfxCallbackDrawable| always calls this function with
    // arguments that guarantee we never tile.
    mDrawResult = mImage->DrawSingleTile(
        aContext, mSize, ImageRegion::Create(aFillRect), mWhichFrame,
        aSamplingFilter, mSVGContext, mFlags, mOpacity);

    return true;
  }

  ImgDrawResult GetDrawResult() { return mDrawResult; }

 private:
  RefPtr<ClippedImage> mImage;
  const nsIntSize mSize;
  const SVGImageContext& mSVGContext;
  const uint32_t mWhichFrame;
  const uint32_t mFlags;
  ImgDrawResult mDrawResult;
  float mOpacity;
};

ClippedImage::ClippedImage(Image* aImage, nsIntRect aClip,
                           const Maybe<CSSSize>& aSVGViewportSize)
    : ImageWrapper(aImage), mClip(aClip), mSVGViewportSize(aSVGViewportSize) {
  MOZ_ASSERT(aImage != nullptr, "ClippedImage requires an existing Image");
  MOZ_ASSERT_IF(aSVGViewportSize,
                aImage->GetType() == imgIContainer::TYPE_VECTOR);
}

ClippedImage::~ClippedImage() = default;

bool ClippedImage::ShouldClip() {
  // We need to evaluate the clipping region against the image's width and
  // height once they're available to determine if it's valid and whether we
  // actually need to do any work. We may fail if the image's width and height
  // aren't available yet, in which case we'll try again later.
  if (mShouldClip.isNothing()) {
    int32_t width, height;
    RefPtr<ProgressTracker> progressTracker =
        InnerImage()->GetProgressTracker();
    if (InnerImage()->HasError()) {
      // If there's a problem with the inner image we'll let it handle
      // everything.
      mShouldClip.emplace(false);
    } else if (mSVGViewportSize && !mSVGViewportSize->IsEmpty()) {
      // Clamp the clipping region to the size of the SVG viewport.
      nsIntRect svgViewportRect(
          nsIntPoint(0, 0),
          CSSIntSize::Ceil(*mSVGViewportSize).ToUnknownSize());

      mClip = mClip.Intersect(svgViewportRect);

      // If the clipping region is the same size as the SVG viewport size
      // we don't have to do anything.
      mShouldClip.emplace(!mClip.IsEqualInterior(svgViewportRect));
    } else if (NS_SUCCEEDED(InnerImage()->GetWidth(&width)) && width > 0 &&
               NS_SUCCEEDED(InnerImage()->GetHeight(&height)) && height > 0) {
      // Clamp the clipping region to the size of the underlying image.
      mClip = mClip.Intersect(nsIntRect(0, 0, width, height));

      // If the clipping region is the same size as the underlying image we
      // don't have to do anything.
      mShouldClip.emplace(
          !mClip.IsEqualInterior(nsIntRect(0, 0, width, height)));
    } else if (progressTracker &&
               !(progressTracker->GetProgress() & FLAG_LOAD_COMPLETE)) {
      // The image just hasn't finished loading yet. We don't yet know whether
      // clipping with be needed or not for now. Just return without memorizing
      // anything.
      return false;
    } else {
      // We have a fully loaded image without a clearly defined width and
      // height. This can happen with SVG images.
      mShouldClip.emplace(false);
    }
  }

  MOZ_ASSERT(mShouldClip.isSome(), "Should have computed a result");
  return *mShouldClip;
}

NS_IMETHODIMP
ClippedImage::GetWidth(int32_t* aWidth) {
  if (!ShouldClip()) {
    return InnerImage()->GetWidth(aWidth);
  }

  *aWidth = mClip.Width();
  return NS_OK;
}

NS_IMETHODIMP
ClippedImage::GetHeight(int32_t* aHeight) {
  if (!ShouldClip()) {
    return InnerImage()->GetHeight(aHeight);
  }

  *aHeight = mClip.Height();
  return NS_OK;
}

NS_IMETHODIMP
ClippedImage::GetIntrinsicSize(ImageIntrinsicSize* aIntrinsicSize) {
  if (!ShouldClip()) {
    return InnerImage()->GetIntrinsicSize(aIntrinsicSize);
  }

  aIntrinsicSize->mWidth = Some(mClip.Width());
  aIntrinsicSize->mHeight = Some(mClip.Height());
  return NS_OK;
}

NS_IMETHODIMP
ClippedImage::GetIntrinsicSizeInAppUnits(nsSize* aSize) {
  if (!ShouldClip()) {
    return InnerImage()->GetIntrinsicSizeInAppUnits(aSize);
  }

  *aSize = nsSize(nsPresContext::CSSPixelsToAppUnits(mClip.Width()),
                  nsPresContext::CSSPixelsToAppUnits(mClip.Height()));
  return NS_OK;
}

AspectRatio ClippedImage::GetIntrinsicRatio() {
  if (!ShouldClip()) {
    return InnerImage()->GetIntrinsicRatio();
  }
  return AspectRatio::FromSize(mClip.Width(), mClip.Height());
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
ClippedImage::GetFrame(uint32_t aWhichFrame, uint32_t aFlags) {
  RefPtr<SourceSurface> surface;
  std::tie(std::ignore, surface) = GetFrameInternal(
      mClip.Size(), SVGImageContext(), Nothing(), aWhichFrame, aFlags, 1.0);
  return surface.forget();
}

NS_IMETHODIMP_(already_AddRefed<SourceSurface>)
ClippedImage::GetFrameAtSize(const IntSize& aSize, uint32_t aWhichFrame,
                             uint32_t aFlags) {
  // XXX(seth): It'd be nice to support downscale-during-decode for this case,
  // but right now we just fall back to the intrinsic size.
  return GetFrame(aWhichFrame, aFlags);
}

std::pair<ImgDrawResult, RefPtr<SourceSurface>> ClippedImage::GetFrameInternal(
    const nsIntSize& aSize, const SVGImageContext& aSVGContext,
    const Maybe<ImageIntRegion>& aRegion, uint32_t aWhichFrame, uint32_t aFlags,
    float aOpacity) {
  if (!ShouldClip()) {
    RefPtr<SourceSurface> surface = InnerImage()->GetFrame(aWhichFrame, aFlags);
    return std::make_pair(
        surface ? ImgDrawResult::SUCCESS : ImgDrawResult::NOT_READY,
        std::move(surface));
  }

  float frameToDraw = InnerImage()->GetFrameIndex(aWhichFrame);
  if (!mCachedSurface ||
      !mCachedSurface->Matches(aSize, aSVGContext, frameToDraw, aFlags) ||
      mCachedSurface->NeedsRedraw()) {
    // Create a surface to draw into.
    RefPtr<DrawTarget> target =
        gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
            IntSize(aSize.width, aSize.height), SurfaceFormat::OS_RGBA);
    if (!target || !target->IsValid()) {
      NS_ERROR("Could not create a DrawTarget");
      return std::make_pair(ImgDrawResult::TEMPORARY_ERROR,
                            RefPtr<SourceSurface>());
    }

    gfxContext ctx(target);

    // Create our callback.
    auto drawTileCallback = MakeRefPtr<DrawSingleTileCallback>(
        this, aSize, aSVGContext, aWhichFrame, aFlags, aOpacity);
    auto drawable = MakeRefPtr<gfxCallbackDrawable>(drawTileCallback, aSize);

    // Actually draw. The callback will end up invoking DrawSingleTile.
    gfxUtils::DrawPixelSnapped(&ctx, drawable, SizeDouble(aSize),
                               ImageRegion::Create(aSize),
                               SurfaceFormat::OS_RGBA, SamplingFilter::LINEAR,
                               imgIContainer::FLAG_CLAMP);

    // Cache the resulting surface.
    mCachedSurface = MakeUnique<ClippedImageCachedSurface>(
        target->Snapshot(), aSize, aSVGContext, frameToDraw, aFlags,
        drawTileCallback->GetDrawResult());
  }

  MOZ_ASSERT(mCachedSurface, "Should have a cached surface now");
  RefPtr<SourceSurface> surface = mCachedSurface->Surface();
  return std::make_pair(mCachedSurface->GetDrawResult(), std::move(surface));
}

NS_IMETHODIMP_(bool)
ClippedImage::IsImageContainerAvailable(WindowRenderer* aRenderer,
                                        uint32_t aFlags) {
  if (!ShouldClip()) {
    return InnerImage()->IsImageContainerAvailable(aRenderer, aFlags);
  }
  return false;
}

NS_IMETHODIMP_(ImgDrawResult)
ClippedImage::GetImageProvider(WindowRenderer* aRenderer,
                               const gfx::IntSize& aSize,
                               const SVGImageContext& aSVGContext,
                               const Maybe<ImageIntRegion>& aRegion,
                               uint32_t aFlags,
                               WebRenderImageProvider** aProvider) {
  // XXX(seth): We currently don't have a way of clipping the result of
  // GetImageContainer. We work around this by always returning null, but if it
  // ever turns out that ClippedImage is widely used on codepaths that can
  // actually benefit from GetImageContainer, it would be a good idea to fix
  // that method for performance reasons.

  if (!ShouldClip()) {
    return InnerImage()->GetImageProvider(aRenderer, aSize, aSVGContext,
                                          aRegion, aFlags, aProvider);
  }

  return ImgDrawResult::NOT_SUPPORTED;
}

static bool MustCreateSurface(gfxContext* aContext, const nsIntSize& aSize,
                              const ImageRegion& aRegion,
                              const uint32_t aFlags) {
  gfxRect imageRect(0, 0, aSize.width, aSize.height);
  bool willTile = !imageRect.Contains(aRegion.Rect()) &&
                  !(aFlags & imgIContainer::FLAG_CLAMP);
  bool willResample = aContext->CurrentMatrix().HasNonIntegerTranslation() &&
                      (willTile || !aRegion.RestrictionContains(imageRect));
  return willTile || willResample;
}

NS_IMETHODIMP_(ImgDrawResult)
ClippedImage::Draw(gfxContext* aContext, const nsIntSize& aSize,
                   const ImageRegion& aRegion, uint32_t aWhichFrame,
                   SamplingFilter aSamplingFilter,
                   const SVGImageContext& aSVGContext, uint32_t aFlags,
                   float aOpacity) {
  if (!ShouldClip()) {
    return InnerImage()->Draw(aContext, aSize, aRegion, aWhichFrame,
                              aSamplingFilter, aSVGContext, aFlags, aOpacity);
  }

  // Check for tiling. If we need to tile then we need to create a
  // gfxCallbackDrawable to handle drawing for us.
  if (MustCreateSurface(aContext, aSize, aRegion, aFlags)) {
    // Create a temporary surface containing a single tile of this image.
    // GetFrame will call DrawSingleTile internally.
    auto [result, surface] = GetFrameInternal(aSize, aSVGContext, Nothing(),
                                              aWhichFrame, aFlags, aOpacity);
    if (!surface) {
      MOZ_ASSERT(result != ImgDrawResult::SUCCESS);
      return result;
    }

    // Create a drawable from that surface.
    auto drawable = MakeRefPtr<gfxSurfaceDrawable>(surface, aSize);

    // Draw.
    gfxUtils::DrawPixelSnapped(aContext, drawable, SizeDouble(aSize), aRegion,
                               SurfaceFormat::OS_RGBA, aSamplingFilter,
                               aOpacity);

    return result;
  }

  return DrawSingleTile(aContext, aSize, aRegion, aWhichFrame, aSamplingFilter,
                        aSVGContext, aFlags, aOpacity);
}

ImgDrawResult ClippedImage::DrawSingleTile(
    gfxContext* aContext, const nsIntSize& aSize, const ImageRegion& aRegion,
    uint32_t aWhichFrame, SamplingFilter aSamplingFilter,
    const SVGImageContext& aSVGContext, uint32_t aFlags, float aOpacity) {
  MOZ_ASSERT(!MustCreateSurface(aContext, aSize, aRegion, aFlags),
             "Shouldn't need to create a surface");

  gfxRect clip(mClip.X(), mClip.Y(), mClip.Width(), mClip.Height());
  nsIntSize size(aSize);
  nsIntSize innerSize(aSize);
  bool needScale = false;
  if (mSVGViewportSize && !mSVGViewportSize->IsEmpty()) {
    innerSize = CSSIntSize::Ceil(*mSVGViewportSize).ToUnknownSize();
    needScale = true;
  } else if (NS_SUCCEEDED(InnerImage()->GetWidth(&innerSize.width)) &&
             NS_SUCCEEDED(InnerImage()->GetHeight(&innerSize.height))) {
    needScale = true;
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "If ShouldClip() led us to draw then we should never get here");
  }

  if (needScale) {
    double scaleX = aSize.width / clip.Width();
    double scaleY = aSize.height / clip.Height();

    // Map the clip and size to the scale requested by the caller.
    clip.Scale(scaleX, scaleY);
    size = innerSize;
    size.Scale(scaleX, scaleY);
  }

  // We restrict our drawing to only the clipping region, and translate so that
  // the clipping region is placed at the position the caller expects.
  ImageRegion region(aRegion);
  region.MoveBy(clip.X(), clip.Y());
  region = region.Intersect(clip);

  gfxContextMatrixAutoSaveRestore saveMatrix(aContext);
  aContext->Multiply(gfxMatrix::Translation(-clip.X(), -clip.Y()));

  // The SVG viewport we got is relative to our own (clipped) coordinate space.
  // Map it to the inner image, which we're about to draw at `size`.
  SVGImageContext innerContext(aSVGContext);
  if (needScale && aSVGContext.GetViewportSize()) {
    innerContext.SetViewportSize(Some(CSSSize(size.width, size.height)));
  }

  return InnerImage()->Draw(aContext, size, region, aWhichFrame,
                            aSamplingFilter, innerContext, aFlags, aOpacity);
}

NS_IMETHODIMP
ClippedImage::RequestDiscard() {
  // We're very aggressive about discarding.
  mCachedSurface = nullptr;

  return InnerImage()->RequestDiscard();
}

NS_IMETHODIMP_(Orientation)
ClippedImage::GetOrientation() {
  // XXX(seth): This should not actually be here; this is just to work around a
  // what appears to be a bug in MSVC's linker.
  return InnerImage()->GetOrientation();
}

nsIntSize ClippedImage::OptimalImageSizeForDest(const gfxSize& aDest,
                                                uint32_t aWhichFrame,
                                                SamplingFilter aSamplingFilter,
                                                uint32_t aFlags) {
  if (!ShouldClip()) {
    return InnerImage()->OptimalImageSizeForDest(aDest, aWhichFrame,
                                                 aSamplingFilter, aFlags);
  }

  IntSize innerSize;
  bool needScale = false;
  bool forceUniformScaling = false;
  if (mSVGViewportSize && !mSVGViewportSize->IsEmpty()) {
    innerSize = CSSIntSize::Ceil(*mSVGViewportSize).ToUnknownSize();
    needScale = true;
    forceUniformScaling = (aFlags & imgIContainer::FLAG_FORCE_UNIFORM_SCALING);
  } else if (NS_SUCCEEDED(InnerImage()->GetWidth(&innerSize.width)) &&
             NS_SUCCEEDED(InnerImage()->GetHeight(&innerSize.height))) {
    needScale = true;
  }

  if (needScale) {
    // To avoid ugly sampling artifacts, ClippedImage needs the image size to
    // be chosen such that the clipping region lies on pixel boundaries.

    // First, we select a scale that's good for ClippedImage. An integer
    // multiple of the size of the clipping region is always fine.
    IntSize scale = IntSize::Ceil(aDest.width / mClip.Width(),
                                  aDest.height / mClip.Height());

    if (forceUniformScaling) {
      scale.width = scale.height = max(scale.height, scale.width);
    }

    // Determine the size we'd prefer to render the inner image at, and ask the
    // inner image what size we should actually use.
    gfxSize desiredSize(double(innerSize.width) * scale.width,
                        double(innerSize.height) * scale.height);
    IntSize innerDesiredSize = InnerImage()->OptimalImageSizeForDest(
        desiredSize, aWhichFrame, aSamplingFilter, aFlags);

    // To get our final result, we take the inner image's desired size and
    // determine how large the clipped region would be at that scale. (Again, we
    // ensure an integer multiple of the size of the clipping region.)
    IntSize finalScale =
        IntSize::Ceil(double(innerDesiredSize.width) / innerSize.width,
                      double(innerDesiredSize.height) / innerSize.height);
    return mClip.Size() * finalScale;
  }

  MOZ_ASSERT_UNREACHABLE(
      "If ShouldClip() led us to draw then we should never get here");
  return InnerImage()->OptimalImageSizeForDest(aDest, aWhichFrame,
                                               aSamplingFilter, aFlags);
}

NS_IMETHODIMP_(nsIntRect)
ClippedImage::GetImageSpaceInvalidationRect(const nsIntRect& aRect) {
  if (!ShouldClip()) {
    return InnerImage()->GetImageSpaceInvalidationRect(aRect);
  }

  nsIntRect rect(InnerImage()->GetImageSpaceInvalidationRect(aRect));
  rect = rect.Intersect(mClip);
  rect.MoveBy(-mClip.X(), -mClip.Y());
  return rect;
}

}  // namespace image
}  // namespace mozilla
