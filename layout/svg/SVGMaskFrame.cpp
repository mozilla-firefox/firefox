/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGMaskFrame.h"

// Keep others in (case-insensitive) order:
#include "AutoReferenceChainGuard.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGMaskElement.h"
#include "mozilla/dom/SVGUnitTypesBinding.h"
#include "mozilla/gfx/2D.h"

using namespace mozilla::dom;
using namespace mozilla::dom::SVGUnitTypes_Binding;
using namespace mozilla::gfx;
using namespace mozilla::image;

nsIFrame* NS_NewSVGMaskFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGMaskFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGMaskFrame)

already_AddRefed<SourceSurface> SVGMaskFrame::GetMaskForMaskedFrame(
    MaskParams& aParams) {
  // Make sure we break reference loops and over long reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mInUse, &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return nullptr;
  }

  gfxRect maskArea = GetMaskArea(aParams.maskedFrame);
  if (maskArea.IsEmpty()) {
    return nullptr;
  }
  // Get the clip extents in device space:
  // Minimizing the mask surface extents (using both the current clip extents
  // and maskArea) is important for performance.
  //
  gfxRect maskSurfaceRectDouble = aParams.toUserSpace.TransformBounds(maskArea);
  Rect maskSurfaceRect = ToRect(maskSurfaceRectDouble);
  maskSurfaceRect.RoundOut();

  StyleMaskType maskType;
  if (aParams.maskMode == StyleMaskMode::MatchSource) {
    maskType = StyleSVGReset()->mMaskType;
  } else {
    maskType = aParams.maskMode == StyleMaskMode::Luminance
                   ? StyleMaskType::Luminance
                   : StyleMaskType::Alpha;
  }

  RefPtr<DrawTarget> maskDT;
  if (maskType == StyleMaskType::Luminance) {
    maskDT = aParams.dt->CreateClippedDrawTarget(maskSurfaceRect,
                                                 SurfaceFormat::B8G8R8A8);
  } else {
    maskDT =
        aParams.dt->CreateClippedDrawTarget(maskSurfaceRect, SurfaceFormat::A8);
  }

  if (!maskDT || !maskDT->IsValid()) {
    return nullptr;
  }

  gfxContext tmpCtx(maskDT, /* aPreserveTransform */ true);

  mMatrixForChildren =
      GetMaskTransform(aParams.maskedFrame) * aParams.toUserSpace;

  for (auto* kid : mFrames) {
    gfxMatrix m = mMatrixForChildren;

    // The CTM of each frame referencing us can be different
    ISVGDisplayableFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      SVGFrame->NotifySVGChanged(
          ISVGDisplayableFrame::ChangeFlag::TransformChanged);
      m = SVGUtils::GetTransformMatrixInUserSpace(kid) * m;
    }

    SVGUtils::PaintFrameWithEffects(kid, tmpCtx, m, aParams.imgParams);
  }

  RefPtr<SourceSurface> surface;
  if (maskType == StyleMaskType::Luminance) {
    auto luminanceType = LuminanceType::LUMINANCE;
    if (StyleSVG()->mColorInterpolation == StyleColorInterpolation::Linearrgb) {
      luminanceType = LuminanceType::LINEARRGB;
    }

    RefPtr<SourceSurface> maskSnapshot =
        maskDT->IntoLuminanceSource(luminanceType, aParams.opacity);
    if (!maskSnapshot) {
      return nullptr;
    }
    surface = std::move(maskSnapshot);
  } else {
    maskDT->FillRect(maskSurfaceRect,
                     ColorPattern(DeviceColor::MaskWhite(aParams.opacity)),
                     DrawOptions(1, CompositionOp::OP_IN));
    RefPtr<SourceSurface> maskSnapshot = maskDT->Snapshot();
    if (!maskSnapshot) {
      return nullptr;
    }
    surface = std::move(maskSnapshot);
  }

  return surface.forget();
}

gfxRect SVGMaskFrame::GetMaskArea(nsIFrame* aMaskedFrame) {
  SVGMaskElement* maskElem = static_cast<SVGMaskElement*>(GetContent());

  uint16_t units =
      maskElem->mEnumAttributes[SVGMaskElement::MASKUNITS].GetAnimValue();
  gfxRect bbox;
  if (units == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    bbox =
        SVGUtils::GetBBox(aMaskedFrame, {SVGBBoxFlag::UseFrameBoundsForOuterSVG,
                                         SVGBBoxFlag::IncludeFillGeometry});
  }

  // Bounds in the user space of aMaskedFrame
  gfxRect maskArea = SVGUtils::GetRelativeRect(
      units, &maskElem->mLengthAttributes[SVGMaskElement::ATTR_X], bbox,
      aMaskedFrame);

  return maskArea;
}

nsresult SVGMaskFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute,
                                        AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x || aAttribute == nsGkAtoms::y ||
       aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height ||
       aAttribute == nsGkAtoms::maskUnits ||
       aAttribute == nsGkAtoms::maskContentUnits)) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }

  return SVGContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                             aModType);
}

#ifdef DEBUG
void SVGMaskFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::mask),
               "Content is not an SVG mask");

  SVGContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

gfxMatrix SVGMaskFrame::GetCanvasTM() { return mMatrixForChildren; }

gfxMatrix SVGMaskFrame::GetMaskTransform(nsIFrame* aMaskedFrame) {
  SVGMaskElement* content = static_cast<SVGMaskElement*>(GetContent());

  SVGAnimatedEnumeration* maskContentUnits =
      &content->mEnumAttributes[SVGMaskElement::MASKCONTENTUNITS];

  SVGBBoxFlags flags = SVGBBoxFlag::IncludeFillGeometry;
  if (aMaskedFrame->StyleBorder()->mBoxDecorationBreak ==
      StyleBoxDecorationBreak::Clone) {
    flags += SVGBBoxFlag::IncludeOnlyCurrentFrameForNonSVGElement;
  }

  return SVGUtils::AdjustMatrixForUnits(gfxMatrix(), maskContentUnits,
                                        aMaskedFrame, flags);
}

}  // namespace mozilla
