/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* utility code for drawing images as CSS borders, backgrounds, and shapes. */

#include "nsImageRenderer.h"

#include "mozilla/webrender/WebRenderAPI.h"

#ifdef MOZ_WIDGET_GTK
#  include "nsIconChannel.h"
#endif
#include "ImageOps.h"
#include "ImageRegion.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "mozilla/ISVGDisplayableFrame.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGPaintServerFrame.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "nsCSSRendering.h"
#include "nsCSSRenderingGradients.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsStyleStructInlines.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::layers;

nsSize CSSSizeOrRatio::ComputeConcreteSize() const {
  NS_ASSERTION(CanComputeConcreteSize(), "Cannot compute");
  if (mHasWidth && mHasHeight) {
    return nsSize(mWidth, mHeight);
  }
  if (mHasWidth) {
    return nsSize(mWidth, mRatio.Inverted().ApplyTo(mWidth));
  }

  MOZ_ASSERT(mHasHeight);
  return nsSize(mRatio.ApplyTo(mHeight), mHeight);
}

nsImageRenderer::nsImageRenderer(nsIFrame* aForFrame, const StyleImage* aImage,
                                 uint32_t aFlags)
    : mForFrame(aForFrame),
      mImage(&aImage->FinalImage()),
      mImageResolution(aImage->GetResolution(aForFrame->Style())),
      mType(mImage->tag),
      mImageContainer(nullptr),
      mGradientData(nullptr),
      mPaintServerFrame(nullptr),
      mPrepareResult(ImgDrawResult::NOT_READY),
      mSize(0, 0),
      mFlags(aFlags),
      mExtendMode(ExtendMode::CLAMP),
      mMaskOp(StyleMaskMode::MatchSource) {}

using SymbolicImageKey = std::tuple<RefPtr<nsAtom>, int, nscolor>;
struct SymbolicImageEntry {
  SymbolicImageKey mKey;
  nsCOMPtr<imgIContainer> mImage;
};
struct SymbolicImageCache final
    : public mozilla::MruCache<SymbolicImageKey, SymbolicImageEntry,
                               SymbolicImageCache, 5> {
  static HashNumber Hash(const KeyType& aKey) {
    return AddToHash(std::get<0>(aKey)->hash(),
                     HashGeneric(std::get<1>(aKey), std::get<2>(aKey)));
  }
  static bool Match(const KeyType& aKey, const ValueType& aVal) {
    return aVal.mKey == aKey;
  }
};

NS_DECLARE_FRAME_PROPERTY_DELETABLE(SymbolicImageCacheProp, SymbolicImageCache);

static already_AddRefed<imgIContainer> GetSymbolicIconImage(nsAtom* aName,
                                                            int aScale,
                                                            nsIFrame* aFrame) {
  if (NS_WARN_IF(!XRE_IsParentProcess())) {
    return nullptr;
  }
  const auto fg = aFrame->StyleText()->mColor.ToColor();
  auto key = std::make_tuple(aName, aScale, fg);
  auto* cache = aFrame->GetProperty(SymbolicImageCacheProp());
  if (!cache) {
    cache = new SymbolicImageCache();
    aFrame->SetProperty(SymbolicImageCacheProp(), cache);
  }
  auto lookup = cache->Lookup(key);
  if (lookup) {
    return do_AddRef(lookup.Data().mImage);
  }
  RefPtr<gfx::DataSourceSurface> surface;
#ifdef MOZ_WIDGET_GTK
  surface =
      nsIconChannel::GetSymbolicIcon(nsAtomCString(aName), 16, aScale, fg);
#endif
  if (NS_WARN_IF(!surface)) {
    return nullptr;
  }
  RefPtr drawable = new gfxSurfaceDrawable(surface, surface->GetSize());
  nsCOMPtr<imgIContainer> container = ImageOps::CreateFromDrawable(drawable);
  MOZ_ASSERT(container);
  lookup.Set(SymbolicImageEntry{std::move(key), std::move(container)});
  return do_AddRef(lookup.Data().mImage);
}

bool nsImageRenderer::PrepareImage() {
  if (mImage->IsNone()) {
    mPrepareResult = ImgDrawResult::BAD_IMAGE;
    return false;
  }

  const bool isImageRequest = mImage->IsImageRequestType();
  MOZ_ASSERT_IF(!isImageRequest, !mImage->GetImageRequest());
  imgRequestProxy* request = nullptr;
  if (isImageRequest) {
    request = mImage->GetImageRequest();
    if (!request) {
      // request could be null here if the StyleImage refused
      // to load a same-document URL, or the url was invalid, for example.
      mPrepareResult = ImgDrawResult::BAD_IMAGE;
      return false;
    }
  }

  if (!mImage->IsComplete()) {
    MOZ_DIAGNOSTIC_ASSERT(isImageRequest);

    // Make sure the image is actually decoding.
    bool frameComplete = request->StartDecodingWithResult(
        imgIContainer::FLAG_ASYNC_NOTIFY |
        imgIContainer::FLAG_AVOID_REDECODE_FOR_SIZE);

    // Boost the loading priority since we know we want to draw the image.
    if (mFlags & nsImageRenderer::FLAG_PAINTING_TO_WINDOW) {
      request->BoostPriority(imgIRequest::CATEGORY_DISPLAY);
    }

    // Check again to see if we finished.
    // We cannot prepare the image for rendering if it is not fully loaded.
    if (!frameComplete && !mImage->IsComplete()) {
      uint32_t imageStatus = 0;
      request->GetImageStatus(&imageStatus);
      if (imageStatus & imgIRequest::STATUS_ERROR) {
        mPrepareResult = ImgDrawResult::BAD_IMAGE;
        return false;
      }

      // If not errored, and we requested a sync decode, and the image has
      // loaded, push on through because the Draw() will do a sync decode then.
      const bool syncDecodeWillComplete =
          (mFlags & FLAG_SYNC_DECODE_IMAGES) &&
          (imageStatus & imgIRequest::STATUS_LOAD_COMPLETE);

      bool canDrawPartial =
          (mFlags & nsImageRenderer::FLAG_DRAW_PARTIAL_FRAMES) &&
          isImageRequest && mImage->IsSizeAvailable();

      // If we are drawing a partial frame then we want to make sure there are
      // some pixels to draw, otherwise we waste effort pushing through to draw
      // nothing.
      if (!syncDecodeWillComplete && canDrawPartial) {
        nsCOMPtr<imgIContainer> image;
        canDrawPartial =
            canDrawPartial &&
            NS_SUCCEEDED(request->GetImage(getter_AddRefs(image))) && image &&
            image->GetType() == imgIContainer::TYPE_RASTER &&
            image->HasDecodedPixels();
      }

      // If we can draw partial then proceed if we at least have the size
      // available.
      if (!(syncDecodeWillComplete || canDrawPartial)) {
        mPrepareResult = ImgDrawResult::NOT_READY;
        return false;
      }
    }
  }

  if (isImageRequest) {
    nsCOMPtr<imgIContainer> srcImage;
    nsresult rv = request->GetImage(getter_AddRefs(srcImage));
    MOZ_ASSERT(NS_SUCCEEDED(rv) && srcImage,
               "If GetImage() is failing, mImage->IsComplete() "
               "should have returned false");
    if (!NS_SUCCEEDED(rv)) {
      srcImage = nullptr;
    }

    if (srcImage) {
      StyleImageOrientation orientation =
          mForFrame->StyleVisibility()->UsedImageOrientation(request);
      srcImage = nsLayoutUtils::OrientImage(srcImage, orientation);
    }

    mImageContainer.swap(srcImage);
    mPrepareResult = ImgDrawResult::SUCCESS;
  } else if (mImage->IsGradient()) {
    mGradientData = &*mImage->AsGradient();
    mPrepareResult = ImgDrawResult::SUCCESS;
  } else if (mImage->IsElement()) {
    dom::Element* paintElement =  // may be null
        SVGObserverUtils::GetAndObserveBackgroundImage(
            mForFrame->FirstContinuation(), mImage->AsElement().AsAtom());
    // If the referenced element is an <img>, <canvas>, or <video> element,
    // prefer SurfaceFromElement as it's more reliable.
    mImageElementSurface = nsLayoutUtils::SurfaceFromElement(paintElement);

    if (!mImageElementSurface.GetSourceSurface()) {
      nsIFrame* paintServerFrame =
          paintElement ? paintElement->GetPrimaryFrame() : nullptr;
      // If there's no referenced frame, or the referenced frame is
      // non-displayable SVG, then we have nothing valid to paint.
      if (!paintServerFrame || (paintServerFrame->IsSVGFrame() &&
                                !static_cast<SVGPaintServerFrame*>(
                                    do_QueryFrame(paintServerFrame)) &&
                                !static_cast<ISVGDisplayableFrame*>(
                                    do_QueryFrame(paintServerFrame)))) {
        mPrepareResult = ImgDrawResult::BAD_IMAGE;
        return false;
      }
      mPaintServerFrame = paintServerFrame;
    }

    mPrepareResult = ImgDrawResult::SUCCESS;
  } else if (mImage->IsMozSymbolicIcon()) {
    auto deviceScale =
        std::ceil(mForFrame->PresContext()->CSSToDevPixelScale().scale);
    mImageResolution.ScaleBy(deviceScale);
    mImageContainer = GetSymbolicIconImage(mImage->AsMozSymbolicIcon().AsAtom(),
                                           int(deviceScale), mForFrame);
    if (!mImageContainer) {
      mPrepareResult = ImgDrawResult::BAD_IMAGE;
      return false;
    }
    mPrepareResult = ImgDrawResult::SUCCESS;
  } else if (mImage->IsCrossFade()) {
    // See bug 546052 - cross-fade implementation still being worked
    // on.
    mPrepareResult = ImgDrawResult::BAD_IMAGE;
    return false;
  } else if (mImage->IsImage()) {
    mPrepareResult = ImgDrawResult::SUCCESS;
  } else {
    MOZ_ASSERT(mImage->IsNone(), "Unknown image type?");
  }

  return IsReady();
}

CSSSizeOrRatio nsImageRenderer::ComputeIntrinsicSize() {
  NS_ASSERTION(IsReady(),
               "Ensure PrepareImage() has returned true "
               "before calling me");

  CSSSizeOrRatio result;
  switch (mType) {
    case StyleImage::Tag::MozSymbolicIcon:
    case StyleImage::Tag::Url: {
      bool haveWidth, haveHeight;
      CSSIntSize imageIntSize;
      nsLayoutUtils::ComputeSizeForDrawing(mImageContainer, mImageResolution,
                                           imageIntSize, result.mRatio,
                                           haveWidth, haveHeight);
      if (haveWidth) {
        result.SetWidth(CSSPixel::ToAppUnits(imageIntSize.width));
      }
      if (haveHeight) {
        result.SetHeight(CSSPixel::ToAppUnits(imageIntSize.height));
      }

      // If we know the aspect ratio and one of the dimensions,
      // we can compute the other missing width or height.
      if (!haveHeight && haveWidth && result.mRatio) {
        CSSIntCoord intrinsicHeight =
            result.mRatio.Inverted().ApplyTo(imageIntSize.width);
        result.SetHeight(nsPresContext::CSSPixelsToAppUnits(intrinsicHeight));
      } else if (haveHeight && !haveWidth && result.mRatio) {
        CSSIntCoord intrinsicWidth = result.mRatio.ApplyTo(imageIntSize.height);
        result.SetWidth(nsPresContext::CSSPixelsToAppUnits(intrinsicWidth));
      }

      break;
    }
    case StyleImage::Tag::Element: {
      // XXX element() should have the width/height of the referenced element,
      //     and that element's ratio, if it matches.  If it doesn't match, it
      //     should have no width/height or ratio.  See element() in CSS images:
      //     <http://dev.w3.org/csswg/css-images-4/#element-notation>.
      //     Make sure to change nsStyleImageLayers::Size::DependsOnFrameSize
      //     when fixing this!
      if (mPaintServerFrame) {
        // SVG images have no intrinsic size
        if (!mPaintServerFrame->IsSVGFrame()) {
          // The intrinsic image size for a generic nsIFrame paint server is
          // the union of the border-box rects of all of its continuations,
          // rounded to device pixels.
          int32_t appUnitsPerDevPixel =
              mForFrame->PresContext()->AppUnitsPerDevPixel();
          result.SetSize(IntSizeToAppUnits(
              SVGIntegrationUtils::GetContinuationUnionSize(mPaintServerFrame)
                  .ToNearestPixels(appUnitsPerDevPixel),
              appUnitsPerDevPixel));
        }
      } else {
        NS_ASSERTION(mImageElementSurface.GetSourceSurface(),
                     "Surface should be ready.");
        IntSize surfaceSize = mImageElementSurface.mSize;
        result.SetSize(
            nsSize(nsPresContext::CSSPixelsToAppUnits(surfaceSize.width),
                   nsPresContext::CSSPixelsToAppUnits(surfaceSize.height)));
      }
      break;
    }
    case StyleImage::Tag::ImageSet:
      MOZ_FALLTHROUGH_ASSERT("image-set() should be resolved already");
    case StyleImage::Tag::LightDark:
      MOZ_FALLTHROUGH_ASSERT("light-dark() should be resolved already");
    // Bug 546052 cross-fade not yet implemented.
    case StyleImage::Tag::CrossFade:
    // Per <http://dev.w3.org/csswg/css3-images/#gradients>, gradients have no
    // intrinsic dimensions.
    case StyleImage::Tag::Gradient:
    case StyleImage::Tag::Image:
    case StyleImage::Tag::None:
      break;
  }

  return result;
}

/* static */
nsSize nsImageRenderer::ComputeConcreteSize(
    const CSSSizeOrRatio& aSpecifiedSize, const CSSSizeOrRatio& aIntrinsicSize,
    const nsSize& aDefaultSize) {
  // The specified size is fully specified, just use that
  if (aSpecifiedSize.IsConcrete()) {
    return aSpecifiedSize.ComputeConcreteSize();
  }

  MOZ_ASSERT(!aSpecifiedSize.mHasWidth || !aSpecifiedSize.mHasHeight);

  if (!aSpecifiedSize.mHasWidth && !aSpecifiedSize.mHasHeight) {
    // no specified size, try using the intrinsic size
    if (aIntrinsicSize.CanComputeConcreteSize()) {
      return aIntrinsicSize.ComputeConcreteSize();
    }

    if (aIntrinsicSize.mHasWidth) {
      return nsSize(aIntrinsicSize.mWidth, aDefaultSize.height);
    }
    if (aIntrinsicSize.mHasHeight) {
      return nsSize(aDefaultSize.width, aIntrinsicSize.mHeight);
    }

    // couldn't use the intrinsic size either, revert to using the default size
    return ComputeConstrainedSize(aDefaultSize, aIntrinsicSize.mRatio, CONTAIN);
  }

  MOZ_ASSERT(aSpecifiedSize.mHasWidth || aSpecifiedSize.mHasHeight);

  // The specified height is partial, try to compute the missing part.
  if (aSpecifiedSize.mHasWidth) {
    nscoord height;
    if (aIntrinsicSize.HasRatio()) {
      height = aIntrinsicSize.mRatio.Inverted().ApplyTo(aSpecifiedSize.mWidth);
    } else if (aIntrinsicSize.mHasHeight) {
      height = aIntrinsicSize.mHeight;
    } else {
      height = aDefaultSize.height;
    }
    return nsSize(aSpecifiedSize.mWidth, height);
  }

  MOZ_ASSERT(aSpecifiedSize.mHasHeight);
  nscoord width;
  if (aIntrinsicSize.HasRatio()) {
    width = aIntrinsicSize.mRatio.ApplyTo(aSpecifiedSize.mHeight);
  } else if (aIntrinsicSize.mHasWidth) {
    width = aIntrinsicSize.mWidth;
  } else {
    width = aDefaultSize.width;
  }
  return nsSize(width, aSpecifiedSize.mHeight);
}

/* static */
nsSize nsImageRenderer::ComputeConstrainedSize(
    const nsSize& aConstrainingSize, const AspectRatio& aIntrinsicRatio,
    FitType aFitType) {
  if (!aIntrinsicRatio) {
    return aConstrainingSize;
  }

  // Suppose we're doing a "contain" fit. If the image's aspect ratio has a
  // "fatter" shape than the constraint area, then we need to use the
  // constraint area's full width, and we need to use the aspect ratio to
  // produce a height. On the other hand, if the aspect ratio is "skinnier", we
  // use the constraint area's full height, and we use the aspect ratio to
  // produce a width. (If instead we're doing a "cover" fit, then it can easily
  // be seen that we should do precisely the opposite.)
  //
  // We check if the image's aspect ratio is "fatter" than the constraint area
  // by simply applying the aspect ratio to the constraint area's height, to
  // produce a "hypothetical width", and we check whether that
  // aspect-ratio-provided "hypothetical width" is wider than the constraint
  // area's actual width. If it is, then the aspect ratio is fatter than the
  // constraint area.
  //
  // This is equivalent to the more descriptive alternative:
  //
  //   AspectRatio::FromSize(aConstrainingSize) < aIntrinsicRatio
  //
  // But gracefully handling the case where one of the two dimensions from
  // aConstrainingSize is zero. This is easy to prove since:
  //
  //   aConstrainingSize.width / aConstrainingSize.height < aIntrinsicRatio
  //
  // Is trivially equivalent to:
  //
  //   aIntrinsicRatio.width < aIntrinsicRatio * aConstrainingSize.height
  //
  // For the cases where height is not zero.
  //
  // We use float math here to avoid losing precision for very large backgrounds
  // since we use saturating nscoord math otherwise.
  const float constraintWidth = float(aConstrainingSize.width);
  const float hypotheticalWidth =
      aIntrinsicRatio.ApplyToFloat(aConstrainingSize.height);

  nsSize size;
  if ((aFitType == CONTAIN) == (constraintWidth < hypotheticalWidth)) {
    size.width = aConstrainingSize.width;
    size.height = aIntrinsicRatio.Inverted().ApplyTo(aConstrainingSize.width);
    // If we're reducing the size by less than one css pixel, then just use the
    // constraining size.
    if (aFitType == CONTAIN &&
        aConstrainingSize.height - size.height < AppUnitsPerCSSPixel()) {
      size.height = aConstrainingSize.height;
    }
  } else {
    size.height = aConstrainingSize.height;
    size.width = aIntrinsicRatio.ApplyTo(aConstrainingSize.height);
    if (aFitType == CONTAIN &&
        aConstrainingSize.width - size.width < AppUnitsPerCSSPixel()) {
      size.width = aConstrainingSize.width;
    }
  }
  return size;
}

/**
 * mSize is the image's "preferred" size for this particular rendering, while
 * the drawn (aka concrete) size is the actual rendered size after accounting
 * for background-size etc..  The preferred size is most often the image's
 * intrinsic dimensions.  But for images with incomplete intrinsic dimensions,
 * the preferred size varies, depending on the specified and default sizes, see
 * nsImageRenderer::Compute*Size.
 *
 * This distinction is necessary because the components of a vector image are
 * specified with respect to its preferred size for a rendering situation, not
 * to its actual rendered size.  For example, consider a 4px wide background
 * vector image with no height which contains a left-aligned
 * 2px wide black rectangle with height 100%.  If the background-size width is
 * auto (or 4px), the vector image will render 4px wide, and the black rectangle
 * will be 2px wide.  If the background-size width is 8px, the vector image will
 * render 8px wide, and the black rectangle will be 4px wide -- *not* 2px wide.
 * In both cases mSize.width will be 4px; but in the first case the returned
 * width will be 4px, while in the second case the returned width will be 8px.
 */
void nsImageRenderer::SetPreferredSize(const CSSSizeOrRatio& aIntrinsicSize,
                                       const nsSize& aDefaultSize) {
  mSize.width =
      aIntrinsicSize.mHasWidth ? aIntrinsicSize.mWidth : aDefaultSize.width;
  mSize.height =
      aIntrinsicSize.mHasHeight ? aIntrinsicSize.mHeight : aDefaultSize.height;
}

// Convert from nsImageRenderer flags to the flags we want to use for drawing in
// the imgIContainer namespace.
static uint32_t ConvertImageRendererToDrawFlags(uint32_t aImageRendererFlags) {
  uint32_t drawFlags = imgIContainer::FLAG_ASYNC_NOTIFY;
  if (aImageRendererFlags & nsImageRenderer::FLAG_SYNC_DECODE_IMAGES) {
    drawFlags |= imgIContainer::FLAG_SYNC_DECODE;
  } else {
    drawFlags |= imgIContainer::FLAG_SYNC_DECODE_IF_FAST;
  }
  if (aImageRendererFlags & (nsImageRenderer::FLAG_PAINTING_TO_WINDOW |
                             nsImageRenderer::FLAG_HIGH_QUALITY_SCALING)) {
    drawFlags |= imgIContainer::FLAG_HIGH_QUALITY_SCALING;
  }
  return drawFlags;
}

ImgDrawResult nsImageRenderer::Draw(nsPresContext* aPresContext,
                                    gfxContext& aRenderingContext,
                                    const nsRect& aDirtyRect,
                                    const nsRect& aDest, const nsRect& aFill,
                                    const nsPoint& aAnchor,
                                    const nsSize& aRepeatSize,
                                    const CSSIntRect& aSrc, float aOpacity) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return ImgDrawResult::TEMPORARY_ERROR;
  }

  if (aDest.IsEmpty() || aFill.IsEmpty() || mSize.width <= 0 ||
      mSize.height <= 0) {
    return ImgDrawResult::SUCCESS;
  }

  SamplingFilter samplingFilter =
      nsLayoutUtils::GetSamplingFilterForFrame(mForFrame);
  ImgDrawResult result = ImgDrawResult::SUCCESS;
  gfxContext* ctx = &aRenderingContext;
  Maybe<gfxContext> tempCtx;
  CompositionOp savedCompositionOp = CompositionOp::OP_OVER;

  if (mMaskOp == StyleMaskMode::Luminance) {
    savedCompositionOp = ctx->CurrentOp();
    ctx->SetOp(CompositionOp::OP_OVER);

    RefPtr<DrawTarget> tempDT = ctx->GetDrawTarget()->CreateClippedDrawTarget(
        Rect(), SurfaceFormat::B8G8R8A8);
    if (!tempDT || !tempDT->IsValid()) {
      gfxDevCrash(LogReason::InvalidContext)
          << "ImageRenderer::Draw problem " << gfx::hexa(tempDT);
      return ImgDrawResult::TEMPORARY_ERROR;
    }
    tempCtx.emplace(tempDT, /* aPreserveTransform */ true);
    ctx = &tempCtx.ref();
    if (!ctx) {
      gfxDevCrash(LogReason::InvalidContext)
          << "ImageRenderer::Draw problem " << gfx::hexa(tempDT);
      return ImgDrawResult::TEMPORARY_ERROR;
    }
  } else if (ctx->CurrentOp() != CompositionOp::OP_OVER) {
    savedCompositionOp = ctx->CurrentOp();
    ctx->SetOp(CompositionOp::OP_OVER);

    IntRect clipRect =
        RoundedOut(ToRect(ctx->GetClipExtents(gfxContext::eDeviceSpace)));
    ctx->GetDrawTarget()->PushLayerWithBlend(false, 1.0, nullptr,
                                             mozilla::gfx::Matrix(), clipRect,
                                             false, savedCompositionOp);
  }

  switch (mType) {
    case StyleImage::Tag::MozSymbolicIcon:
    case StyleImage::Tag::Url: {
      result = nsLayoutUtils::DrawBackgroundImage(
          *ctx, mForFrame, aPresContext, mImageContainer, samplingFilter, aDest,
          aFill, aRepeatSize, aAnchor, aDirtyRect,
          ConvertImageRendererToDrawFlags(mFlags), mExtendMode, aOpacity);
      break;
    }
    case StyleImage::Tag::Image: {
      const auto fill = LayoutDeviceRect::FromAppUnits(
          aFill, aPresContext->AppUnitsPerDevPixel());
      ctx->GetDrawTarget()->FillRect(
          fill.ToUnknownRect(),
          ColorPattern(ToDeviceColor(mImage->AsImage()->CalcColor(mForFrame))),
          DrawOptions(/* aAlpha = */ aOpacity));
      break;
    }
    case StyleImage::Tag::Gradient: {
      nsCSSGradientRenderer renderer = nsCSSGradientRenderer::Create(
          aPresContext, mForFrame->Style(), *mGradientData, mSize);

      renderer.Paint(*ctx, aDest, aFill, aRepeatSize, aSrc, aDirtyRect,
                     aOpacity);
      break;
    }
    case StyleImage::Tag::Element: {
      RefPtr<gfxDrawable> drawable = DrawableForElement(aDest, *ctx);
      if (!drawable) {
        NS_WARNING("Could not create drawable for element");
        return ImgDrawResult::TEMPORARY_ERROR;
      }

      nsCOMPtr<imgIContainer> image(ImageOps::CreateFromDrawable(drawable));
      result = nsLayoutUtils::DrawImage(
          *ctx, mForFrame->Style(), aPresContext, image, samplingFilter, aDest,
          aFill, aAnchor, aDirtyRect, ConvertImageRendererToDrawFlags(mFlags),
          aOpacity);
      break;
    }
    case StyleImage::Tag::ImageSet:
      MOZ_FALLTHROUGH_ASSERT("image-set() should be resolved already");
    case StyleImage::Tag::LightDark:
      MOZ_FALLTHROUGH_ASSERT("light-dark() should be resolved already");
    // See bug 546052 - cross-fade implementation still being worked
    // on.
    case StyleImage::Tag::CrossFade:
    case StyleImage::Tag::None:
      break;
  }

  if (mMaskOp == StyleMaskMode::Luminance) {
    RefPtr<SourceSurface> surf = ctx->GetDrawTarget()->IntoLuminanceSource(
        LuminanceType::LUMINANCE, 1.0f);
    DrawTarget* dt = aRenderingContext.GetDrawTarget();
    Matrix oldTransform = dt->GetTransform();
    dt->SetTransform(Matrix());
    dt->MaskSurface(ColorPattern(DeviceColor(0, 0, 0, 1.0f)), surf, Point(0, 0),
                    DrawOptions(1.0f, savedCompositionOp));
    dt->SetTransform(oldTransform);
    aRenderingContext.SetOp(savedCompositionOp);
  } else if (savedCompositionOp != CompositionOp::OP_OVER) {
    aRenderingContext.GetDrawTarget()->PopLayer();
    aRenderingContext.SetOp(savedCompositionOp);
  }

  if (!mImage->IsComplete()) {
    result &= ImgDrawResult::SUCCESS_NOT_COMPLETE;
  }

  return result;
}

ImgDrawResult nsImageRenderer::BuildWebRenderDisplayItems(
    nsPresContext* aPresContext, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
    const nsRect& aDirtyRect, const nsRect& aDest, const nsRect& aFill,
    const nsPoint& aAnchor, const nsSize& aRepeatSize, const CSSIntRect& aSrc,
    float aOpacity) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return ImgDrawResult::NOT_READY;
  }

  if (aDest.IsEmpty() || aFill.IsEmpty() || mSize.width <= 0 ||
      mSize.height <= 0) {
    return ImgDrawResult::SUCCESS;
  }

  ImgDrawResult drawResult = ImgDrawResult::SUCCESS;
  switch (mType) {
    case StyleImage::Tag::Gradient: {
      nsCSSGradientRenderer renderer = nsCSSGradientRenderer::Create(
          aPresContext, mForFrame->Style(), *mGradientData, mSize);

      renderer.BuildWebRenderDisplayItems(aBuilder, aSc, aDest, aFill,
                                          aRepeatSize, aSrc,
                                          !aItem->BackfaceIsHidden(), aOpacity);
      break;
    }
    case StyleImage::Tag::MozSymbolicIcon:
    case StyleImage::Tag::Url: {
      ExtendMode extendMode = mExtendMode;
      if (aDest.Contains(aFill)) {
        extendMode = ExtendMode::CLAMP;
      }

      uint32_t containerFlags = ConvertImageRendererToDrawFlags(mFlags);
      if (extendMode == ExtendMode::CLAMP &&
          StaticPrefs::image_svg_blob_image() &&
          mImageContainer->GetType() == imgIContainer::TYPE_VECTOR) {
        containerFlags |= imgIContainer::FLAG_RECORD_BLOB;
      }

      CSSIntSize destCSSSize{
          nsPresContext::AppUnitsToIntCSSPixels(aDest.width),
          nsPresContext::AppUnitsToIntCSSPixels(aDest.height)};

      SVGImageContext svgContext(Some(destCSSSize));
      Maybe<ImageIntRegion> region;

      const int32_t appUnitsPerDevPixel = aPresContext->AppUnitsPerDevPixel();
      const auto destRect =
          LayoutDeviceRect::FromAppUnits(aDest, appUnitsPerDevPixel);
      const auto clipRect =
          LayoutDeviceRect::FromAppUnits(aFill, appUnitsPerDevPixel);
      auto stretchSize = wr::ToLayoutSize(destRect.Size());

      gfx::IntSize decodeSize =
          nsLayoutUtils::ComputeImageContainerDrawingParameters(
              mImageContainer, mForFrame, destRect, clipRect, aSc,
              containerFlags, svgContext, region);

      RefPtr<image::WebRenderImageProvider> provider;
      drawResult = mImageContainer->GetImageProvider(
          aManager->LayerManager(), decodeSize, svgContext, region,
          containerFlags, getter_AddRefs(provider));

      Maybe<wr::ImageKey> key =
          aManager->CommandBuilder().CreateImageProviderKey(
              aItem, provider, drawResult, aResources);
      if (key.isNothing()) {
        break;
      }

      auto rendering =
          wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
      wr::LayoutRect clip = wr::ToLayoutRect(clipRect);

      // If we provided a region to the provider, then it already took the
      // dest rect into account when it did the recording.
      wr::LayoutRect dest = region ? clip : wr::ToLayoutRect(destRect);

      if (extendMode == ExtendMode::CLAMP) {
        // The image is not repeating. Just push as a regular image.
        aBuilder.PushImage(dest, clip, !aItem->BackfaceIsHidden(), false,
                           rendering, key.value(), true,
                           wr::ColorF{1.0f, 1.0f, 1.0f, aOpacity});
      } else {
        nsPoint firstTilePos = nsLayoutUtils::GetBackgroundFirstTilePos(
            aDest.TopLeft(), aFill.TopLeft(), aRepeatSize);
        LayoutDeviceRect fillRect = LayoutDeviceRect::FromAppUnits(
            nsRect(firstTilePos.x, firstTilePos.y,
                   aFill.XMost() - firstTilePos.x,
                   aFill.YMost() - firstTilePos.y),
            appUnitsPerDevPixel);
        wr::LayoutRect fill = wr::ToLayoutRect(fillRect);

        switch (extendMode) {
          case ExtendMode::REPEAT_Y:
            fill.min.x = dest.min.x;
            fill.max.x = dest.max.x;
            stretchSize.width = dest.width();
            break;
          case ExtendMode::REPEAT_X:
            fill.min.y = dest.min.y;
            fill.max.y = dest.max.y;
            stretchSize.height = dest.height();
            break;
          default:
            break;
        }

        LayoutDeviceSize gapSize = LayoutDeviceSize::FromAppUnits(
            aRepeatSize - aDest.Size(), appUnitsPerDevPixel);

        aBuilder.PushRepeatingImage(fill, clip, !aItem->BackfaceIsHidden(),
                                    stretchSize, wr::ToLayoutSize(gapSize),
                                    rendering, key.value(), true,
                                    wr::ColorF{1.0f, 1.0f, 1.0f, aOpacity});
      }
      break;
    }
    case StyleImage::Tag::Image: {
      const int32_t appUnitsPerDevPixel = aPresContext->AppUnitsPerDevPixel();
      auto fillRect = wr::ToLayoutRect(
          LayoutDeviceRect::FromAppUnits(aFill, appUnitsPerDevPixel));
      aBuilder.PushRect(
          fillRect, fillRect, !aItem->BackfaceIsHidden(),
          /* aFoceAntiAliasing = */ false, /* aIsCheckerboard = */ false,
          wr::ToColorF(ToDeviceColor(mImage->AsImage()->CalcColor(mForFrame))));
      break;
    }
    default:
      break;
  }

  if (!mImage->IsComplete() && drawResult == ImgDrawResult::SUCCESS) {
    return ImgDrawResult::SUCCESS_NOT_COMPLETE;
  }
  return drawResult;
}

already_AddRefed<gfxDrawable> nsImageRenderer::DrawableForElement(
    const nsRect& aImageRect, gfxContext& aContext) {
  NS_ASSERTION(mType == StyleImage::Tag::Element,
               "DrawableForElement only makes sense if backed by an element");
  if (mPaintServerFrame) {
    // XXX(seth): In order to not pass FLAG_SYNC_DECODE_IMAGES here,
    // DrawableFromPaintServer would have to return a ImgDrawResult indicating
    // whether any images could not be painted because they weren't fully
    // decoded. Even always passing FLAG_SYNC_DECODE_IMAGES won't eliminate all
    // problems, as it won't help if there are image which haven't finished
    // loading, but it's better than nothing.
    int32_t appUnitsPerDevPixel =
        mForFrame->PresContext()->AppUnitsPerDevPixel();
    nsRect destRect = aImageRect - aImageRect.TopLeft();
    nsIntSize roundedOut = destRect.ToOutsidePixels(appUnitsPerDevPixel).Size();
    IntSize imageSize(roundedOut.width, roundedOut.height);

    RefPtr<gfxDrawable> drawable;

    SurfaceFormat format = aContext.GetDrawTarget()->GetFormat();
    // Don't allow creating images that are too big
    if (aContext.GetDrawTarget()->CanCreateSimilarDrawTarget(imageSize,
                                                             format)) {
      drawable = SVGIntegrationUtils::DrawableFromPaintServer(
          mPaintServerFrame, mForFrame, mSize, imageSize,
          aContext.GetDrawTarget(), aContext.CurrentMatrixDouble(),
          SVGIntegrationUtils::DecodeFlag::SyncDecodeImages);
    }

    return drawable.forget();
  }
  NS_ASSERTION(mImageElementSurface.GetSourceSurface(),
               "Surface should be ready.");
  RefPtr<gfxDrawable> drawable =
      new gfxSurfaceDrawable(mImageElementSurface.GetSourceSurface().get(),
                             mImageElementSurface.mSize);
  return drawable.forget();
}

ImgDrawResult nsImageRenderer::DrawLayer(
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDest, const nsRect& aFill, const nsPoint& aAnchor,
    const nsRect& aDirty, const nsSize& aRepeatSize, float aOpacity) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return ImgDrawResult::TEMPORARY_ERROR;
  }

  if (aDest.IsEmpty() || aFill.IsEmpty() || mSize.width <= 0 ||
      mSize.height <= 0) {
    return ImgDrawResult::SUCCESS;
  }

  return Draw(
      aPresContext, aRenderingContext, aDirty, aDest, aFill, aAnchor,
      aRepeatSize,
      CSSIntRect(0, 0, nsPresContext::AppUnitsToIntCSSPixels(mSize.width),
                 nsPresContext::AppUnitsToIntCSSPixels(mSize.height)),
      aOpacity);
}

ImgDrawResult nsImageRenderer::BuildWebRenderDisplayItemsForLayer(
    nsPresContext* aPresContext, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
    const nsRect& aDest, const nsRect& aFill, const nsPoint& aAnchor,
    const nsRect& aDirty, const nsSize& aRepeatSize, float aOpacity) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return mPrepareResult;
  }

  CSSIntRect srcRect(0, 0, nsPresContext::AppUnitsToIntCSSPixels(mSize.width),
                     nsPresContext::AppUnitsToIntCSSPixels(mSize.height));

  if (aDest.IsEmpty() || aFill.IsEmpty() || srcRect.IsEmpty()) {
    return ImgDrawResult::SUCCESS;
  }
  return BuildWebRenderDisplayItems(aPresContext, aBuilder, aResources, aSc,
                                    aManager, aItem, aDirty, aDest, aFill,
                                    aAnchor, aRepeatSize, srcRect, aOpacity);
}

/**
 * Compute the size and position of the master copy of the image. I.e., a single
 * tile used to fill the dest rect.
 * aFill The destination rect to be filled
 * aHFill and aVFill are the repeat patterns for the component -
 * StyleBorderImageRepeatKeyword - i.e., how a tiling unit is used to fill aFill
 * aUnitSize The size of the source rect in dest coords.
 */
static nsRect ComputeTile(nsRect& aFill, StyleBorderImageRepeatKeyword aHFill,
                          StyleBorderImageRepeatKeyword aVFill,
                          const nsSize& aUnitSize, nsSize& aRepeatSize) {
  nsRect tile;
  switch (aHFill) {
    case StyleBorderImageRepeatKeyword::Stretch:
      tile.x = aFill.x;
      tile.width = aFill.width;
      aRepeatSize.width = tile.width;
      break;
    case StyleBorderImageRepeatKeyword::Repeat:
      tile.x = aFill.x + aFill.width / 2 - aUnitSize.width / 2;
      tile.width = aUnitSize.width;
      aRepeatSize.width = tile.width;
      break;
    case StyleBorderImageRepeatKeyword::Round:
      tile.x = aFill.x;
      tile.width =
          nsCSSRendering::ComputeRoundedSize(aUnitSize.width, aFill.width);
      aRepeatSize.width = tile.width;
      break;
    case StyleBorderImageRepeatKeyword::Space: {
      nscoord space;
      aRepeatSize.width = nsCSSRendering::ComputeBorderSpacedRepeatSize(
          aUnitSize.width, aFill.width, space);
      tile.x = aFill.x + space;
      tile.width = aUnitSize.width;
      aFill.x = tile.x;
      aFill.width = aFill.width - space * 2;
    } break;
    default:
      MOZ_ASSERT_UNREACHABLE("unrecognized border-image fill style");
  }

  switch (aVFill) {
    case StyleBorderImageRepeatKeyword::Stretch:
      tile.y = aFill.y;
      tile.height = aFill.height;
      aRepeatSize.height = tile.height;
      break;
    case StyleBorderImageRepeatKeyword::Repeat:
      tile.y = aFill.y + aFill.height / 2 - aUnitSize.height / 2;
      tile.height = aUnitSize.height;
      aRepeatSize.height = tile.height;
      break;
    case StyleBorderImageRepeatKeyword::Round:
      tile.y = aFill.y;
      tile.height =
          nsCSSRendering::ComputeRoundedSize(aUnitSize.height, aFill.height);
      aRepeatSize.height = tile.height;
      break;
    case StyleBorderImageRepeatKeyword::Space: {
      nscoord space;
      aRepeatSize.height = nsCSSRendering::ComputeBorderSpacedRepeatSize(
          aUnitSize.height, aFill.height, space);
      tile.y = aFill.y + space;
      tile.height = aUnitSize.height;
      aFill.y = tile.y;
      aFill.height = aFill.height - space * 2;
    } break;
    default:
      MOZ_ASSERT_UNREACHABLE("unrecognized border-image fill style");
  }

  return tile;
}

/**
 * Returns true if the given set of arguments will require the tiles which fill
 * the dest rect to be scaled from the source tile. See comment on ComputeTile
 * for argument descriptions.
 */
static bool RequiresScaling(const nsRect& aFill,
                            StyleBorderImageRepeatKeyword aHFill,
                            StyleBorderImageRepeatKeyword aVFill,
                            const nsSize& aUnitSize) {
  // If we have no tiling in either direction, we can skip the intermediate
  // scaling step.
  return (aHFill != StyleBorderImageRepeatKeyword::Stretch ||
          aVFill != StyleBorderImageRepeatKeyword::Stretch) &&
         (aUnitSize.width != aFill.width || aUnitSize.height != aFill.height);
}

ImgDrawResult nsImageRenderer::DrawBorderImageComponent(
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    const nsRect& aDirtyRect, const nsRect& aFill, const CSSIntRect& aSrc,
    StyleBorderImageRepeatKeyword aHFill, StyleBorderImageRepeatKeyword aVFill,
    const nsSize& aUnitSize, uint8_t aIndex,
    const Maybe<nsSize>& aSVGViewportSize, const bool aHasIntrinsicRatio) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return ImgDrawResult::BAD_ARGS;
  }

  if (aFill.IsEmpty() || aSrc.IsEmpty()) {
    return ImgDrawResult::SUCCESS;
  }

  const bool hasImage = !!mImageContainer;
  if (hasImage || mType == StyleImage::Tag::Element) {
    nsCOMPtr<imgIContainer> subImage;

    // To draw one portion of an image into a border component, we stretch that
    // portion to match the size of that border component and then draw onto.
    // However, preserveAspectRatio attribute of a SVG image may break this
    // rule. To get correct rendering result, we add
    // FLAG_FORCE_PRESERVEASPECTRATIO_NONE flag here, to tell mImage to ignore
    // preserveAspectRatio attribute, and always do non-uniform stretch.
    uint32_t drawFlags = ConvertImageRendererToDrawFlags(mFlags) |
                         imgIContainer::FLAG_FORCE_PRESERVEASPECTRATIO_NONE;
    // For those SVG image sources which don't have fixed aspect ratio (i.e.
    // without viewport size and viewBox), we should scale the source uniformly
    // after the viewport size is decided by "Default Sizing Algorithm".
    if (!aHasIntrinsicRatio) {
      drawFlags = drawFlags | imgIContainer::FLAG_FORCE_UNIFORM_SCALING;
    }
    // Retrieve or create the subimage we'll draw.
    nsIntRect srcRect(aSrc.x, aSrc.y, aSrc.width, aSrc.height);
    if (hasImage) {
      subImage = ImageOps::Clip(mImageContainer, srcRect, aSVGViewportSize);
    } else {
      // This path, for Element, is currently slower than it
      // needs to be because we don't cache anything. (In particular, if we have
      // to draw to a temporary surface inside ClippedImage, we don't cache that
      // temporary surface since we immediately throw the ClippedImage we create
      // here away.) However, if we did cache, we'd need to know when to
      // invalidate that cache, and it's not clear that it's worth the trouble
      // since using border-image with -moz-element is rare.

      RefPtr<gfxDrawable> drawable =
          DrawableForElement(nsRect(nsPoint(), mSize), aRenderingContext);
      if (!drawable) {
        NS_WARNING("Could not create drawable for element");
        return ImgDrawResult::TEMPORARY_ERROR;
      }

      nsCOMPtr<imgIContainer> image(ImageOps::CreateFromDrawable(drawable));
      subImage = ImageOps::Clip(image, srcRect, aSVGViewportSize);
    }

    MOZ_ASSERT(!aSVGViewportSize ||
               subImage->GetType() == imgIContainer::TYPE_VECTOR);

    SamplingFilter samplingFilter =
        nsLayoutUtils::GetSamplingFilterForFrame(mForFrame);

    if (!RequiresScaling(aFill, aHFill, aVFill, aUnitSize)) {
      ImgDrawResult result = nsLayoutUtils::DrawSingleImage(
          aRenderingContext, aPresContext, subImage, samplingFilter, aFill,
          aDirtyRect, SVGImageContext(), drawFlags);

      if (!mImage->IsComplete()) {
        result &= ImgDrawResult::SUCCESS_NOT_COMPLETE;
      }

      return result;
    }

    nsSize repeatSize;
    nsRect fillRect(aFill);
    nsRect tile = ComputeTile(fillRect, aHFill, aVFill, aUnitSize, repeatSize);

    ImgDrawResult result = nsLayoutUtils::DrawBackgroundImage(
        aRenderingContext, mForFrame, aPresContext, subImage, samplingFilter,
        tile, fillRect, repeatSize, tile.TopLeft(), aDirtyRect, drawFlags,
        ExtendMode::CLAMP, 1.0);

    if (!mImage->IsComplete()) {
      result &= ImgDrawResult::SUCCESS_NOT_COMPLETE;
    }

    return result;
  }

  nsSize repeatSize(aFill.Size());
  nsRect fillRect(aFill);
  nsRect destTile =
      RequiresScaling(fillRect, aHFill, aVFill, aUnitSize)
          ? ComputeTile(fillRect, aHFill, aVFill, aUnitSize, repeatSize)
          : fillRect;

  return Draw(aPresContext, aRenderingContext, aDirtyRect, destTile, fillRect,
              destTile.TopLeft(), repeatSize, aSrc);
}

ImgDrawResult nsImageRenderer::DrawShapeImage(nsPresContext* aPresContext,
                                              gfxContext& aRenderingContext) {
  if (!IsReady()) {
    MOZ_ASSERT_UNREACHABLE(
        "Ensure PrepareImage() has returned true before "
        "calling me");
    return ImgDrawResult::NOT_READY;
  }

  if (mSize.width <= 0 || mSize.height <= 0) {
    return ImgDrawResult::SUCCESS;
  }

  if (mImage->IsImageRequestType()) {
    uint32_t drawFlags =
        ConvertImageRendererToDrawFlags(mFlags) | imgIContainer::FRAME_FIRST;
    nsRect dest(nsPoint(0, 0), mSize);
    // We have a tricky situation in our choice of SamplingFilter. Shape
    // images define a float area based on the alpha values in the rendered
    // pixels. When multiple device pixels are used for one css pixel, the
    // sampling can change crisp edges into aliased edges. For visual pixels,
    // that's usually the right choice. For defining a float area, it can
    // cause problems. If a style is using a shape-image-threshold value that
    // is less than the alpha of the edge pixels, any filtering may smear the
    // alpha into adjacent pixels and expand the float area in a confusing
    // way. Since the alpha threshold can be set precisely in CSS, and since a
    // web author may be counting on that threshold to define a precise float
    // area from an image, it is least confusing to have the rendered pixels
    // have unfiltered alpha. We use SamplingFilter::POINT to ensure that each
    // rendered pixel has an alpha that precisely matches the alpha of the
    // closest pixel in the image.
    return nsLayoutUtils::DrawSingleImage(
        aRenderingContext, aPresContext, mImageContainer, SamplingFilter::POINT,
        dest, dest, SVGImageContext(), drawFlags);
  }

  if (mImage->IsGradient()) {
    nsCSSGradientRenderer renderer = nsCSSGradientRenderer::Create(
        aPresContext, mForFrame->Style(), *mGradientData, mSize);
    nsRect dest(nsPoint(0, 0), mSize);
    renderer.Paint(aRenderingContext, dest, dest, mSize,
                   CSSIntRect::FromAppUnitsRounded(dest), dest, 1.0);
    return ImgDrawResult::SUCCESS;
  }

  // Unsupported image type.
  return ImgDrawResult::BAD_IMAGE;
}

bool nsImageRenderer::IsRasterImage() const {
  return mImageContainer &&
         mImageContainer->GetType() == imgIContainer::TYPE_RASTER;
}

already_AddRefed<imgIContainer> nsImageRenderer::GetImage() {
  return do_AddRef(mImageContainer);
}
