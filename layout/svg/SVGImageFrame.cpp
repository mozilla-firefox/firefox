/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGImageFrame.h"

// Keep in (case-insensitive) order:
#include "ImageRegion.h"
#include "SVGGeometryProperty.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "imgIContainer.h"
#include "imgINotificationObserver.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/dom/PerformanceContainerTiming.h"
#include "mozilla/dom/SVGImageElement.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "nsContainerFrame.h"
#include "nsIImageLoadingContent.h"
#include "nsIMutationObserver.h"
#include "nsIReflowCallback.h"
#include "nsLayoutUtils.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::image;
using namespace mozilla::dom::SVGPreserveAspectRatio_Binding;
namespace SVGT = SVGGeometryProperty::Tags;

namespace mozilla {

class SVGImageListener final : public imgINotificationObserver {
 public:
  explicit SVGImageListener(SVGImageFrame* aFrame);

  NS_DECL_ISUPPORTS
  NS_DECL_IMGINOTIFICATIONOBSERVER

  void SetFrame(SVGImageFrame* frame) { mFrame = frame; }

 private:
  ~SVGImageListener() = default;

  SVGImageFrame* mFrame;
};

// ---------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(SVGImageFrame)
  NS_QUERYFRAME_ENTRY(ISVGDisplayableFrame)
  NS_QUERYFRAME_ENTRY(SVGImageFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsIFrame)

}  // namespace mozilla

nsIFrame* NS_NewSVGImageFrame(mozilla::PresShell* aPresShell,
                              mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGImageFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGImageFrame)

SVGImageFrame::~SVGImageFrame() {
  // set the frame to null so we don't send messages to a dead object.
  if (mListener) {
    nsCOMPtr<nsIImageLoadingContent> imageLoader =
        do_QueryInterface(GetContent());
    if (imageLoader) {
      imageLoader->RemoveNativeObserver(mListener);
    }
    reinterpret_cast<SVGImageListener*>(mListener.get())->SetFrame(nullptr);
  }
  mListener = nullptr;
}

void SVGImageFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                         nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::image),
               "Content is not an SVG image!");

  AddStateBits(aParent->GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD);
  nsIFrame::Init(aContent, aParent, aPrevInFlow);

  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // Non-display frames are likely to be patterns, masks or the like.
    // Treat them as always visible.
    // This call must happen before the FrameCreated. This is because the
    // primary frame pointer on our content node isn't set until after this
    // function ends, so there is no way for the resulting OnVisibilityChange
    // notification to get a frame. FrameCreated has a workaround for this in
    // that it passes our frame around so it can be accessed. OnVisibilityChange
    // doesn't have that workaround.
    IncApproximateVisibleCount();
  }

  mListener = new SVGImageListener(this);
  nsCOMPtr<nsIImageLoadingContent> imageLoader =
      do_QueryInterface(GetContent());
  if (!imageLoader) {
    MOZ_CRASH("Why is this not an image loading content?");
  }

  // We should have a PresContext now, so let's notify our image loader that
  // we need to register any image animations with the refresh driver.
  imageLoader->FrameCreated(this);

  imageLoader->AddNativeObserver(mListener);
}

/* virtual */
void SVGImageFrame::Destroy(DestroyContext& aContext) {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    DecApproximateVisibleCount();
  }

  if (mReflowCallbackPosted) {
    PresShell()->CancelReflowCallback(this);
    mReflowCallbackPosted = false;
  }

  nsCOMPtr<nsIImageLoadingContent> imageLoader = do_QueryInterface(mContent);

  if (imageLoader) {
    imageLoader->FrameDestroyed(this);
  }

  nsIFrame::Destroy(aContext);
}

/* virtual */
void SVGImageFrame::DidSetComputedStyle(ComputedStyle* aOldStyle) {
  nsIFrame::DidSetComputedStyle(aOldStyle);

  if (!mImageContainer || !aOldStyle) {
    return;
  }

  nsCOMPtr<imgIRequest> currentRequest;
  nsCOMPtr<nsIImageLoadingContent> imageLoader =
      do_QueryInterface(GetContent());
  if (imageLoader) {
    imageLoader->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                            getter_AddRefs(currentRequest));
  }

  StyleImageOrientation newOrientation =
      StyleVisibility()->UsedImageOrientation(currentRequest);
  StyleImageOrientation oldOrientation =
      aOldStyle->StyleVisibility()->UsedImageOrientation(currentRequest);

  if (oldOrientation != newOrientation) {
    nsCOMPtr<imgIContainer> image(mImageContainer->Unwrap());
    mImageContainer = nsLayoutUtils::OrientImage(image, newOrientation);
  }

  // TODO(heycam): We should handle aspect-ratio, like nsImageFrame does.
}

bool SVGImageFrame::DoGetParentSVGTransforms(
    gfx::Matrix* aFromParentTransform) const {
  return SVGUtils::GetParentSVGTransforms(this, aFromParentTransform);
}

//----------------------------------------------------------------------
// nsIFrame methods:

nsresult SVGImageFrame::AttributeChanged(int32_t aNameSpaceID,
                                         nsAtom* aAttribute,
                                         AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::preserveAspectRatio) {
      // We don't paint the content of the image using display lists, therefore
      // we have to invalidate for this children-only transform changes since
      // there is no layer tree to notice that the transform changed and
      // recomposite.
      InvalidateFrame();
      return NS_OK;
    }
  }
  if (aModType == AttrModType::Removal &&
      (aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink) &&
      aAttribute == nsGkAtoms::href) {
    auto* element = static_cast<SVGImageElement*>(GetContent());
    if (aNameSpaceID == kNameSpaceID_None ||
        !element->mStringAttributes[SVGImageElement::HREF].IsExplicitlySet()) {
      mImageContainer = nullptr;
      InvalidateFrame();
    }
  }

  return NS_OK;
}

void SVGImageFrame::OnVisibilityChange(
    Visibility aNewVisibility, const Maybe<OnNonvisible>& aNonvisibleAction) {
  nsCOMPtr<nsIImageLoadingContent> imageLoader =
      do_QueryInterface(GetContent());
  if (imageLoader) {
    imageLoader->OnVisibilityChange(aNewVisibility, aNonvisibleAction);
  }

  nsIFrame::OnVisibilityChange(aNewVisibility, aNonvisibleAction);
}

gfx::Matrix SVGImageFrame::GetRasterImageTransform(int32_t aNativeWidth,
                                                   int32_t aNativeHeight) {
  float x, y, width, height;
  SVGImageElement* element = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      element, &x, &y, &width, &height);

  Matrix viewBoxTM = SVGContentUtils::GetViewBoxTransform(
      width, height, 0, 0, aNativeWidth, aNativeHeight,
      element->mPreserveAspectRatio);

  return viewBoxTM * gfx::Matrix::Translation(x, y);
}

gfx::Matrix SVGImageFrame::GetVectorImageTransform() {
  float x, y;
  SVGImageElement* element = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y>(element, &x, &y);

  // No viewBoxTM needed here -- our height/width overrides any concept of
  // "native size" that the SVG image has, and it will handle viewBox and
  // preserveAspectRatio on its own once we give it a region to draw into.

  return gfx::Matrix::Translation(x, y);
}

bool SVGImageFrame::GetIntrinsicImageDimensions(
    mozilla::gfx::Size& aSize, mozilla::AspectRatio& aAspectRatio) const {
  if (!mImageContainer) {
    return false;
  }

  ImageResolution resolution = mImageContainer->GetResolution();
  int32_t width, height;
  if (NS_FAILED(mImageContainer->GetWidth(&width))) {
    aSize.width = -1;
  } else {
    aSize.width = width;
    resolution.ApplyXTo(aSize.width);
  }

  if (NS_FAILED(mImageContainer->GetHeight(&height))) {
    aSize.height = -1;
  } else {
    aSize.height = height;
    resolution.ApplyYTo(aSize.height);
  }

  aAspectRatio = mImageContainer->GetIntrinsicRatio();
  return true;
}

bool SVGImageFrame::TransformContextForPainting(gfxContext* aGfxContext,
                                                const gfxMatrix& aTransform) {
  gfx::Matrix imageTransform;
  if (mImageContainer->GetType() == imgIContainer::TYPE_VECTOR) {
    imageTransform = GetVectorImageTransform() * ToMatrix(aTransform);
  } else {
    int32_t nativeWidth, nativeHeight;
    if (NS_FAILED(mImageContainer->GetWidth(&nativeWidth)) ||
        NS_FAILED(mImageContainer->GetHeight(&nativeHeight)) ||
        nativeWidth == 0 || nativeHeight == 0) {
      return false;
    }
    mImageContainer->GetResolution().ApplyTo(nativeWidth, nativeHeight);
    imageTransform = GetRasterImageTransform(nativeWidth, nativeHeight) *
                     ToMatrix(aTransform);

    // NOTE: We need to cancel out the effects of Full-Page-Zoom, or else
    // it'll get applied an extra time by DrawSingleUnscaledImage.
    nscoord appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
    gfxFloat pageZoomFactor =
        nsPresContext::AppUnitsToFloatCSSPixels(appUnitsPerDevPx);
    imageTransform.PreScale(pageZoomFactor, pageZoomFactor);
  }

  if (imageTransform.IsSingular()) {
    return false;
  }

  aGfxContext->Multiply(ThebesMatrix(imageTransform));
  return true;
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

void SVGImageFrame::PaintSVG(gfxContext& aContext, const gfxMatrix& aTransform,
                             imgDrawingParams& aImgParams) {
  if (!StyleVisibility()->IsVisible()) {
    return;
  }

  float x, y, width, height;
  SVGImageElement* imgElem = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      imgElem, &x, &y, &width, &height);
  NS_ASSERTION(width > 0 && height > 0,
               "Should only be painting things with valid width/height");

  if (!mImageContainer) {
    nsCOMPtr<imgIRequest> currentRequest;
    nsCOMPtr<nsIImageLoadingContent> imageLoader =
        do_QueryInterface(GetContent());
    if (imageLoader) {
      imageLoader->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                              getter_AddRefs(currentRequest));
    }

    if (currentRequest) {
      currentRequest->GetImage(getter_AddRefs(mImageContainer));
    }
  }

  if (mImageContainer) {
    gfxClipAutoSaveRestore autoSaveClip(&aContext);

    if (StyleDisplay()->IsScrollableOverflow()) {
      gfxRect clipRect =
          SVGUtils::GetClipRectForFrame(this, x, y, width, height);
      autoSaveClip.TransformedClip(aTransform, clipRect);
    }

    gfxContextMatrixAutoSaveRestore autoSaveMatrix(&aContext);

    if (!TransformContextForPainting(&aContext, aTransform)) {
      return;
    }

    // fill-opacity doesn't affect <image>, so if we're allowed to
    // optimize group opacity, the opacity used for compositing the
    // image into the current canvas is just the group opacity.
    float opacity = 1.0f;
    if (SVGUtils::CanOptimizeOpacity(this)) {
      opacity = StyleEffects()->mOpacity;
    }

    gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aContext);
    if (opacity != 1.0f || StyleEffects()->HasMixBlendMode()) {
      autoGroupForBlend.PushGroupForBlendBack(gfxContentType::COLOR_ALPHA,
                                              opacity);
    }

    nscoord appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
    uint32_t flags = aImgParams.imageFlags;
    if (mForceSyncDecoding || UsedImageDecoding() == StyleImageDecoding::Sync) {
      flags |= imgIContainer::FLAG_SYNC_DECODE;
    }

    if (mImageContainer->GetType() == imgIContainer::TYPE_VECTOR) {
      // Package up the attributes of this image element which can override the
      // attributes of mImageContainer's internal SVG document.  The 'width' &
      // 'height' values we're passing in here are in CSS units (though they
      // come from width/height *attributes* in SVG). They influence the region
      // of the SVG image's internal document that is visible, in combination
      // with preserveAspectRatio and viewBox.
      const SVGImageContext context(
          Some(CSSSize(width, height)),
          Some(imgElem->mPreserveAspectRatio.GetAnimValue()));

      // For the actual draw operation to draw crisply (and at the right size),
      // our destination rect needs to be |width|x|height|, *in dev pixels*.
      LayoutDeviceSize devPxSize(width, height);
      nsRect destRect(nsPoint(), LayoutDevicePixel::ToAppUnits(
                                     devPxSize, appUnitsPerDevPx));
      nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest();
      if (currentRequest) {
        Element* element = GetContent()->AsElement();

        ContainerTimingHelpers::MaybeProcessPaintForContainer(element, this,
                                                              destRect);
        LCPHelpers::FinalizeLCPEntryForImage(
            element, static_cast<imgRequestProxy*>(currentRequest.get()),
            destRect);
      }

      // Note: Can't use DrawSingleUnscaledImage for the TYPE_VECTOR case.
      // That method needs our image to have a fixed native width & height,
      // and that's not always true for TYPE_VECTOR images.
      aImgParams.result &= nsLayoutUtils::DrawSingleImage(
          aContext, PresContext(), mImageContainer,
          nsLayoutUtils::GetSamplingFilterForFrame(this), destRect, destRect,
          context, flags);
    } else {  // mImageContainer->GetType() == TYPE_RASTER
      aImgParams.result &= nsLayoutUtils::DrawSingleUnscaledImage(
          aContext, PresContext(), mImageContainer,
          nsLayoutUtils::GetSamplingFilterForFrame(this), nsPoint(0, 0),
          nullptr, SVGImageContext(), flags);
    }
  }
}

void SVGImageFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                     const nsDisplayListSet& aLists) {
  if (!static_cast<const SVGElement*>(GetContent())->HasValidDimensions()) {
    return;
  }

  if (aBuilder->IsForPainting()) {
    if (!IsVisibleForPainting()) {
      return;
    }
    if (StyleEffects()->IsTransparent() && SVGUtils::CanOptimizeOpacity(this)) {
      return;
    }
    aBuilder->BuildCompositorHitTestInfoIfNeeded(this,
                                                 aLists.BorderBackground());
  }

  DisplayOutline(aBuilder, aLists);
  aLists.Content()->AppendNewToTop<DisplaySVGImage>(aBuilder, this);
}

bool SVGImageFrame::IsInvisible() const {
  if (!StyleVisibility()->IsVisible()) {
    return true;
  }

  // Anything below will round to zero later down the pipeline.
  constexpr float opacity_threshold = 1.0 / 128.0;

  return StyleEffects()->mOpacity <= opacity_threshold &&
         SVGUtils::CanOptimizeOpacity(this);
}

bool SVGImageFrame::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, DisplaySVGImage* aItem,
    bool aDryRun) {
  if (!StyleVisibility()->IsVisible()) {
    return true;
  }

  float opacity = 1.0f;
  if (SVGUtils::CanOptimizeOpacity(this)) {
    opacity = StyleEffects()->mOpacity;
  }

  if (opacity != 1.0f) {
    // FIXME: not implemented, might be trivial
    return false;
  }
  if (StyleEffects()->HasMixBlendMode()) {
    // FIXME: not implemented
    return false;
  }

  // try to setup the image
  if (!mImageContainer) {
    nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest();
    if (currentRequest) {
      currentRequest->GetImage(getter_AddRefs(mImageContainer));
    }
  }

  if (!mImageContainer) {
    // nothing to draw (yet)
    return true;
  }

  uint32_t flags = aDisplayListBuilder->GetImageDecodeFlags();
  if (mForceSyncDecoding || UsedImageDecoding() == StyleImageDecoding::Sync) {
    flags |= imgIContainer::FLAG_SYNC_DECODE;
  }

  // Compute bounds of the image
  nscoord appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
  int32_t appUnitsPerCSSPixel = AppUnitsPerCSSPixel();

  float x, y, width, height;
  SVGImageElement* imgElem = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      imgElem, &x, &y, &width, &height);
  NS_ASSERTION(width > 0 && height > 0,
               "Should only be painting things with valid width/height");

  auto toReferenceFrame = aItem->ToReferenceFrame();
  auto appRect = nsLayoutUtils::RoundGfxRectToAppRect(Rect(0, 0, width, height),
                                                      appUnitsPerCSSPixel);
  appRect += toReferenceFrame;
  auto destRect = LayoutDeviceRect::FromAppUnits(appRect, appUnitsPerDevPx);
  auto clipRect = destRect;

  if (StyleDisplay()->IsScrollableOverflow()) {
    // Apply potential non-trivial clip
    auto cssClip = SVGUtils::GetClipRectForFrame(this, 0, 0, width, height);
    auto appClip =
        nsLayoutUtils::RoundGfxRectToAppRect(cssClip, appUnitsPerCSSPixel);
    appClip += toReferenceFrame;
    clipRect = LayoutDeviceRect::FromAppUnits(appClip, appUnitsPerDevPx);

    // Apply preserveAspectRatio
    if (mImageContainer->GetType() == imgIContainer::TYPE_RASTER) {
      int32_t nativeWidth, nativeHeight;
      if (NS_FAILED(mImageContainer->GetWidth(&nativeWidth)) ||
          NS_FAILED(mImageContainer->GetHeight(&nativeHeight)) ||
          nativeWidth == 0 || nativeHeight == 0) {
        // Image has no size; nothing to draw
        return true;
      }

      mImageContainer->GetResolution().ApplyTo(nativeWidth, nativeHeight);

      auto preserveAspectRatio = imgElem->mPreserveAspectRatio.GetAnimValue();
      uint16_t align = preserveAspectRatio.GetAlign();
      uint16_t meetOrSlice = preserveAspectRatio.GetMeetOrSlice();

      // default to the defaults
      if (align == SVG_PRESERVEASPECTRATIO_UNKNOWN) {
        align = SVG_PRESERVEASPECTRATIO_XMIDYMID;
      }
      if (meetOrSlice == SVG_MEETORSLICE_UNKNOWN) {
        meetOrSlice = SVG_MEETORSLICE_MEET;
      }

      // aspect > 1 is horizontal
      // aspect < 1 is vertical
      float nativeAspect = ((float)nativeWidth) / ((float)nativeHeight);
      float viewAspect = width / height;

      // "Meet" is "fit image to view"; "Slice" is "cover view with image".
      //
      // Whether we meet or slice, one side of the destRect will always be
      // perfectly spanned by our image. The only questions to answer are
      // "which side won't span perfectly" and "should that side be grown
      // or shrunk".
      //
      // Because we fit our image to the destRect, this all just reduces to:
      // "if meet, shrink to fit. if slice, grow to fit."
      if (align != SVG_PRESERVEASPECTRATIO_NONE && nativeAspect != viewAspect) {
        // Slightly redundant bools, but they make the conditions clearer
        bool tooTall = nativeAspect > viewAspect;
        bool tooWide = nativeAspect < viewAspect;
        if ((meetOrSlice == SVG_MEETORSLICE_MEET && tooTall) ||
            (meetOrSlice == SVG_MEETORSLICE_SLICE && tooWide)) {
          // Adjust height and realign y
          auto oldHeight = destRect.height;
          destRect.height = destRect.width / nativeAspect;
          auto heightChange = oldHeight - destRect.height;
          switch (align) {
            case SVG_PRESERVEASPECTRATIO_XMINYMIN:
            case SVG_PRESERVEASPECTRATIO_XMIDYMIN:
            case SVG_PRESERVEASPECTRATIO_XMAXYMIN:
              // align to top (no-op)
              break;
            case SVG_PRESERVEASPECTRATIO_XMINYMID:
            case SVG_PRESERVEASPECTRATIO_XMIDYMID:
            case SVG_PRESERVEASPECTRATIO_XMAXYMID:
              // align to center
              destRect.y += heightChange / 2.0f;
              break;
            case SVG_PRESERVEASPECTRATIO_XMINYMAX:
            case SVG_PRESERVEASPECTRATIO_XMIDYMAX:
            case SVG_PRESERVEASPECTRATIO_XMAXYMAX:
              // align to bottom
              destRect.y += heightChange;
              break;
            default:
              MOZ_ASSERT_UNREACHABLE("Unknown value for align");
          }
        } else if ((meetOrSlice == SVG_MEETORSLICE_MEET && tooWide) ||
                   (meetOrSlice == SVG_MEETORSLICE_SLICE && tooTall)) {
          // Adjust width and realign x
          auto oldWidth = destRect.width;
          destRect.width = destRect.height * nativeAspect;
          auto widthChange = oldWidth - destRect.width;
          switch (align) {
            case SVG_PRESERVEASPECTRATIO_XMINYMIN:
            case SVG_PRESERVEASPECTRATIO_XMINYMID:
            case SVG_PRESERVEASPECTRATIO_XMINYMAX:
              // align to left (no-op)
              break;
            case SVG_PRESERVEASPECTRATIO_XMIDYMIN:
            case SVG_PRESERVEASPECTRATIO_XMIDYMID:
            case SVG_PRESERVEASPECTRATIO_XMIDYMAX:
              // align to center
              destRect.x += widthChange / 2.0f;
              break;
            case SVG_PRESERVEASPECTRATIO_XMAXYMIN:
            case SVG_PRESERVEASPECTRATIO_XMAXYMID:
            case SVG_PRESERVEASPECTRATIO_XMAXYMAX:
              // align to right
              destRect.x += widthChange;
              break;
            default:
              MOZ_ASSERT_UNREACHABLE("Unknown value for align");
          }
        }
      }
    }
  }

  SVGImageContext svgContext;
  if (mImageContainer->GetType() == imgIContainer::TYPE_VECTOR) {
    if (StaticPrefs::image_svg_blob_image()) {
      flags |= imgIContainer::FLAG_RECORD_BLOB;
    }
    // Forward preserveAspectRatio to inner SVGs
    svgContext.SetViewportSize(Some(CSSSize(width, height)));
    svgContext.SetPreserveAspectRatio(
        Some(imgElem->mPreserveAspectRatio.GetAnimValue()));
  }

  Maybe<ImageIntRegion> region;
  IntSize decodeSize = nsLayoutUtils::ComputeImageContainerDrawingParameters(
      mImageContainer, this, destRect, clipRect, aSc, flags, svgContext,
      region);

  if (nsCOMPtr<imgIRequest> currentRequest = GetCurrentRequest()) {
    Element* element = GetContent()->AsElement();
    nsRect rectRelativeToSelf =
        LayoutDeviceRect::ToAppUnits(destRect, appUnitsPerDevPx) -
        toReferenceFrame;

    ContainerTimingHelpers::MaybeProcessPaintForContainer(element, this,
                                                          rectRelativeToSelf);

    LCPHelpers::FinalizeLCPEntryForImage(
        element, static_cast<imgRequestProxy*>(currentRequest.get()),
        rectRelativeToSelf);
  }

  RefPtr<image::WebRenderImageProvider> provider;
  ImgDrawResult drawResult = mImageContainer->GetImageProvider(
      aManager->LayerManager(), decodeSize, svgContext, region, flags,
      getter_AddRefs(provider));

  // While we got a container, it may not contain a fully decoded surface. If
  // that is the case, and we have an image we were previously displaying which
  // has a fully decoded surface, then we should prefer the previous image.
  switch (drawResult) {
    case ImgDrawResult::NOT_READY:
    case ImgDrawResult::TEMPORARY_ERROR:
      // nothing to draw (yet)
      return true;
    case ImgDrawResult::NOT_SUPPORTED:
      // things we haven't implemented for WR yet
      return false;
    default:
      // image is ready to draw
      break;
  }

  // Don't do any actual mutations to state if we're doing a dry run
  // (used to decide if we're making this into an active layer)
  if (!aDryRun) {
    // If the image container is empty, we don't want to fallback. Any other
    // failure will be due to resource constraints and fallback is unlikely to
    // help us. Hence we can ignore the return value from PushImage.
    if (provider) {
      aManager->CommandBuilder().PushImageProvider(aItem, provider, drawResult,
                                                   aBuilder, aResources,
                                                   destRect, clipRect);
    }
  }

  return true;
}

nsIFrame* SVGImageFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  if (!HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD) && IgnoreHitTest()) {
    return nullptr;
  }

  Rect rect;
  SVGImageElement* element = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      element, &rect.x, &rect.y, &rect.width, &rect.height);

  if (!rect.Contains(ToPoint(aPoint))) {
    return nullptr;
  }

  // Special case for raster images -- we only want to accept points that fall
  // in the underlying image's (scaled to fit) native bounds.  That region
  // doesn't necessarily map to our <image> element's [x,y,width,height] if the
  // raster image's aspect ratio is being preserved.  We have to look up the
  // native image size & our viewBox transform in order to filter out points
  // that fall outside that area.  (This special case doesn't apply to vector
  // images because they don't limit their drawing to explicit "native
  // bounds" -- they have an infinite canvas on which to place content.)
  if (StyleDisplay()->IsScrollableOverflow() && mImageContainer) {
    if (mImageContainer->GetType() == imgIContainer::TYPE_RASTER) {
      int32_t nativeWidth, nativeHeight;
      if (NS_FAILED(mImageContainer->GetWidth(&nativeWidth)) ||
          NS_FAILED(mImageContainer->GetHeight(&nativeHeight)) ||
          nativeWidth == 0 || nativeHeight == 0) {
        return nullptr;
      }
      mImageContainer->GetResolution().ApplyTo(nativeWidth, nativeHeight);
      Matrix viewBoxTM = SVGContentUtils::GetViewBoxTransform(
          rect.width, rect.height, 0, 0, nativeWidth, nativeHeight,
          element->mPreserveAspectRatio);
      if (!SVGUtils::HitTestRect(viewBoxTM, 0, 0, nativeWidth, nativeHeight,
                                 aPoint.x - rect.x, aPoint.y - rect.y)) {
        return nullptr;
      }
    }
  }

  return this;
}

void SVGImageFrame::ReflowSVG() {
  NS_ASSERTION(SVGUtils::OuterSVGIsCallingReflowSVG(this),
               "This call is probably a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    return;
  }

  float x, y, width, height;
  SVGImageElement* element = static_cast<SVGImageElement*>(GetContent());
  SVGGeometryProperty::ResolveAll<SVGT::X, SVGT::Y, SVGT::Width, SVGT::Height>(
      element, &x, &y, &width, &height);

  Rect extent(x, y, width, height);

  if (!extent.IsEmpty()) {
    mRect = nsLayoutUtils::RoundGfxRectToAppRect(extent, AppUnitsPerCSSPixel());
  } else {
    mRect.SetEmpty();
  }

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);

    if (!mReflowCallbackPosted) {
      mReflowCallbackPosted = true;
      PresShell()->PostReflowCallback(this);
    }
  }

  nsRect overflow = nsRect(nsPoint(0, 0), mRect.Size());
  OverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);

  // Invalidate, but only if this is not our first reflow (since if it is our
  // first reflow then we haven't had our first paint yet).
  if (!GetParent()->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    InvalidateFrame();
  }
}

bool SVGImageFrame::ReflowFinished() {
  mReflowCallbackPosted = false;

  // XXX(seth): We don't need this. The purpose of updating visibility
  // synchronously is to ensure that animated images start animating
  // immediately. In the short term, however,
  // nsImageLoadingContent::OnUnlockedDraw() is enough to ensure that
  // animations start as soon as the image is painted for the first time, and in
  // the long term we want to update visibility information from the display
  // list whenever we paint, so we don't actually need to do this. However, to
  // avoid behavior changes during the transition from the old image visibility
  // code, we'll leave it in for now.
  UpdateVisibilitySynchronously();

  return false;
}

void SVGImageFrame::ReflowCallbackCanceled() { mReflowCallbackPosted = false; }

already_AddRefed<imgIRequest> SVGImageFrame::GetCurrentRequest() const {
  nsCOMPtr<imgIRequest> request;
  nsCOMPtr<nsIImageLoadingContent> imageLoader =
      do_QueryInterface(GetContent());
  if (imageLoader) {
    imageLoader->GetRequest(nsIImageLoadingContent::CURRENT_REQUEST,
                            getter_AddRefs(request));
  }
  return request.forget();
}

bool SVGImageFrame::IgnoreHitTest() const {
  switch (Style()->PointerEvents()) {
    case StylePointerEvents::None:
      break;
    case StylePointerEvents::Visiblepainted:
    case StylePointerEvents::Auto:
      if (StyleVisibility()->IsVisible()) {
        /* XXX: should check pixel transparency */
        return false;
      }
      break;
    case StylePointerEvents::Visiblefill:
    case StylePointerEvents::Visiblestroke:
    case StylePointerEvents::Visible:
      if (StyleVisibility()->IsVisible()) {
        return false;
      }
      break;
    case StylePointerEvents::Painted:
      /* XXX: should check pixel transparency */
      return false;
    case StylePointerEvents::Fill:
    case StylePointerEvents::Stroke:
    case StylePointerEvents::All:
      return false;
    default:
      NS_ERROR("not reached");
      break;
  }

  return true;
}

void SVGImageFrame::NotifySVGChanged(ChangeFlags aFlags) {
  MOZ_ASSERT(aFlags.contains(ChangeFlag::TransformChanged) ||
                 aFlags.contains(ChangeFlag::CoordContextChanged),
             "Invalidation logic may need adjusting");
}

SVGBBox SVGImageFrame::GetBBoxContribution(const Matrix& aToBBoxUserspace,
                                           SVGBBoxFlags aFlags) {
  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return {};
  }

  if (aFlags.contains(SVGBBoxFlag::ForGetClientRects) &&
      aToBBoxUserspace.PreservesAxisAlignedRectangles()) {
    if (!mRect.IsEmpty()) {
      Rect rect = NSRectToRect(mRect, AppUnitsPerCSSPixel());
      return aToBBoxUserspace.TransformBounds(rect);
    }
    return {};
  }

  auto* element = static_cast<SVGImageElement*>(GetContent());

  Rect rect = element->GeometryBounds(aToBBoxUserspace);

  if (aFlags.contains(SVGBBoxFlag::DisregardCSSZoom)) {
    rect.Scale(1 / Style()->EffectiveZoom().ToFloat());
  }

  return rect;
}

//----------------------------------------------------------------------
// SVGImageListener implementation

NS_IMPL_ISUPPORTS(SVGImageListener, imgINotificationObserver)

SVGImageListener::SVGImageListener(SVGImageFrame* aFrame) : mFrame(aFrame) {}

void SVGImageListener::Notify(imgIRequest* aRequest, int32_t aType,
                              const nsIntRect* aData) {
  if (!mFrame) {
    return;
  }

  if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    mFrame->InvalidateFrame();
    nsLayoutUtils::PostRestyleEvent(mFrame->GetContent()->AsElement(),
                                    RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
    SVGUtils::ScheduleReflowSVG(mFrame);
  }

  if (aType == imgINotificationObserver::FRAME_UPDATE) {
    // No new dimensions, so we don't need to call
    // SVGUtils::InvalidateAndScheduleBoundsUpdate.
    nsLayoutUtils::PostRestyleEvent(mFrame->GetContent()->AsElement(),
                                    RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
    mFrame->InvalidateFrame();
  }

  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    // Called once the resource's dimensions have been obtained.
    nsCOMPtr<imgIContainer> image;
    aRequest->GetImage(getter_AddRefs(image));
    if (image) {
      StyleImageOrientation orientation =
          mFrame->StyleVisibility()->UsedImageOrientation(aRequest);
      image = nsLayoutUtils::OrientImage(image, orientation);
      image->SetAnimationMode(mFrame->PresContext()->ImageAnimationMode());
      mFrame->mImageContainer = std::move(image);
    }
    mFrame->InvalidateFrame();
    nsLayoutUtils::PostRestyleEvent(mFrame->GetContent()->AsElement(),
                                    RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
    SVGUtils::ScheduleReflowSVG(mFrame);
  }
}

}  // namespace mozilla
