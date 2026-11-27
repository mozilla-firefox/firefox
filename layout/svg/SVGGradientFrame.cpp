/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGGradientFrame.h"

#include <algorithm>

// Keep others in (case-insensitive) order:
#include "AutoReferenceChainGuard.h"
#include "SVGAnimatedTransformList.h"
#include "gfxPattern.h"
#include "gfxUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGGradientElement.h"
#include "mozilla/dom/SVGGradientElementBinding.h"
#include "mozilla/dom/SVGStopElement.h"
#include "mozilla/dom/SVGUnitTypesBinding.h"
#include "nsContentUtils.h"

using namespace mozilla::dom;
using namespace mozilla::dom::SVGGradientElement_Binding;
using namespace mozilla::dom::SVGUnitTypes_Binding;
using namespace mozilla::gfx;

namespace mozilla {

//----------------------------------------------------------------------
// Implementation

SVGGradientFrame::SVGGradientFrame(ComputedStyle* aStyle,
                                   nsPresContext* aPresContext, ClassID aID)
    : SVGPaintServerFrame(aStyle, aPresContext, aID),
      mSource(nullptr),
      mLoopFlag(false),
      mNoHRefURI(false) {}

NS_QUERYFRAME_HEAD(SVGGradientFrame)
  NS_QUERYFRAME_ENTRY(SVGGradientFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGPaintServerFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

nsresult SVGGradientFrame::AttributeChanged(int32_t aNameSpaceID,
                                            nsAtom* aAttribute,
                                            AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::gradientUnits ||
       aAttribute == nsGkAtoms::gradientTransform ||
       aAttribute == nsGkAtoms::spreadMethod)) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  } else if ((aNameSpaceID == kNameSpaceID_XLink ||
              aNameSpaceID == kNameSpaceID_None) &&
             aAttribute == nsGkAtoms::href) {
    // Blow away our reference, if any
    SVGObserverUtils::RemoveTemplateObserver(this);
    mNoHRefURI = false;
    // And update whoever references us
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }

  return SVGPaintServerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                               aModType);
}

//----------------------------------------------------------------------

uint16_t SVGGradientFrame::GetEnumValue(uint32_t aIndex, nsIContent* aDefault) {
  const SVGAnimatedEnumeration& thisEnum =
      static_cast<dom::SVGGradientElement*>(GetContent())
          ->mEnumAttributes[aIndex];

  if (thisEnum.IsExplicitlySet()) {
    return thisEnum.GetAnimValue();
  }

  // Before we recurse, make sure we'll break reference loops and over long
  // reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mLoopFlag,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return static_cast<dom::SVGGradientElement*>(aDefault)
        ->mEnumAttributes[aIndex]
        .GetAnimValue();
  }

  SVGGradientFrame* next = GetReferencedGradient();

  return next ? next->GetEnumValue(aIndex, aDefault)
              : static_cast<dom::SVGGradientElement*>(aDefault)
                    ->mEnumAttributes[aIndex]
                    .GetAnimValue();
}

uint16_t SVGGradientFrame::GetGradientUnits() {
  // This getter is called every time the others are called - maybe cache it?
  return GetEnumValue(dom::SVGGradientElement::GRADIENTUNITS);
}

uint16_t SVGGradientFrame::GetSpreadMethod() {
  return GetEnumValue(dom::SVGGradientElement::SPREADMETHOD);
}

SVGGradientFrame* SVGGradientFrame::GetGradientTransformFrame(
    SVGGradientFrame* aDefault) {
  if (!StyleDisplay()->mTransform.IsNone()) {
    return this;
  }
  // Before we recurse, make sure we'll break reference loops and over long
  // reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mLoopFlag,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return aDefault;
  }

  if (SVGGradientFrame* next = GetReferencedGradient()) {
    return next->GetGradientTransformFrame(aDefault);
  }
  return aDefault;
}

gfxMatrix SVGGradientFrame::GetGradientTransform(
    nsIFrame* aSource, const gfxRect* aOverrideBounds) {
  gfxMatrix bboxMatrix;
  uint16_t gradientUnits = GetGradientUnits();
  if (gradientUnits != SVG_UNIT_TYPE_USERSPACEONUSE) {
    NS_ASSERTION(gradientUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
                 "Unknown gradientUnits type");
    // objectBoundingBox is the default anyway

    gfxRect bbox = aOverrideBounds
                       ? *aOverrideBounds
                       : SVGUtils::GetBBox(
                             aSource, {SVGBBoxFlag::UseFrameBoundsForOuterSVG,
                                       SVGBBoxFlag::IncludeFillGeometry});
    bboxMatrix =
        gfxMatrix(bbox.Width(), 0, 0, bbox.Height(), bbox.X(), bbox.Y());
  }

  return bboxMatrix.PreMultiply(
      SVGUtils::GetTransformMatrixInUserSpace(GetGradientTransformFrame(this)));
}

dom::SVGLinearGradientElement* SVGGradientFrame::GetLinearGradientWithLength(
    uint32_t aIndex, dom::SVGLinearGradientElement* aDefault) {
  // If this was a linear gradient with the required length, we would have
  // already found it in SVGLinearGradientFrame::GetLinearGradientWithLength.
  // Since we didn't find the length, continue looking down the chain.

  // Before we recurse, make sure we'll break reference loops and over long
  // reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mLoopFlag,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return aDefault;
  }

  SVGGradientFrame* next = GetReferencedGradient();
  return next ? next->GetLinearGradientWithLength(aIndex, aDefault) : aDefault;
}

dom::SVGRadialGradientElement* SVGGradientFrame::GetRadialGradientWithLength(
    uint32_t aIndex, dom::SVGRadialGradientElement* aDefault) {
  // If this was a radial gradient with the required length, we would have
  // already found it in SVGRadialGradientFrame::GetRadialGradientWithLength.
  // Since we didn't find the length, continue looking down the chain.

  // Before we recurse, make sure we'll break reference loops and over long
  // reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mLoopFlag,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return aDefault;
  }

  SVGGradientFrame* next = GetReferencedGradient();
  return next ? next->GetRadialGradientWithLength(aIndex, aDefault) : aDefault;
}

//----------------------------------------------------------------------
// SVGPaintServerFrame methods:

// helpers

static ColorStop GetStopInformation(const nsIFrame* aStopFrame,
                                    float aGraphicOpacity,
                                    float& aLastPosition) {
  nsIContent* stopContent = aStopFrame->GetContent();
  MOZ_ASSERT(stopContent && stopContent->IsSVGElement(nsGkAtoms::stop));

  float position;
  static_cast<SVGStopElement*>(stopContent)
      ->GetAnimatedNumberValues(&position, nullptr);

  position = std::clamp(position, 0.0f, 1.0f);

  if (position < aLastPosition) {
    position = aLastPosition;
  } else {
    aLastPosition = position;
  }

  const auto* svgReset = aStopFrame->StyleSVGReset();

  sRGBColor stopColor =
      sRGBColor::FromABGR(svgReset->mStopColor.CalcColor(aStopFrame));
  stopColor.a *= svgReset->mStopOpacity * aGraphicOpacity;

  return ColorStop(position, false,
                   StyleAbsoluteColor::FromColor(stopColor.ToABGR()));
}

class MOZ_STACK_CLASS SVGColorStopInterpolator
    : public ColorStopInterpolator<SVGColorStopInterpolator> {
 public:
  SVGColorStopInterpolator(
      gfxPattern* aGradient, const nsTArray<ColorStop>& aStops,
      const StyleColorInterpolationMethod& aStyleColorInterpolationMethod,
      bool aExtend)
      : ColorStopInterpolator(aStops, aStyleColorInterpolationMethod, aExtend),
        mGradient(aGradient) {}

  void CreateStop(float aPosition, DeviceColor aColor) {
    mGradient->AddColorStop(aPosition, aColor);
  }

 private:
  gfxPattern* mGradient;
};

already_AddRefed<gfxPattern> SVGGradientFrame::GetPaintServerPattern(
    nsIFrame* aSource, const DrawTarget* aDrawTarget,
    const gfxMatrix& aContextMatrix, StyleSVGPaint nsStyleSVG::* aFillOrStroke,
    float aGraphicOpacity, imgDrawingParams& aImgParams,
    const gfxRect* aOverrideBounds) {
  uint16_t gradientUnits = GetGradientUnits();
  MOZ_ASSERT(gradientUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX ||
             gradientUnits == SVG_UNIT_TYPE_USERSPACEONUSE);
  if (gradientUnits == SVG_UNIT_TYPE_USERSPACEONUSE) {
    // Set mSource for this consumer.
    // If this gradient is applied to text, our caller will be the glyph, which
    // is not an element, so we need to get the parent
    mSource = aSource->IsTextFrame() ? aSource->GetParent() : aSource;
  }

  AutoTArray<ColorStop, 8> stops;
  GetStops(&stops, aGraphicOpacity);

  uint32_t nStops = stops.Length();

  // SVG specification says that no stops should be treated like
  // the corresponding fill or stroke had "none" specified.
  if (nStops == 0) {
    return MakeAndAddRef<gfxPattern>(DeviceColor());
  }

  if (nStops == 1 || GradientVectorLengthIsZero()) {
    // The gradient paints a single colour, using the stop-color of the last
    // gradient step if there are more than one.
    return MakeAndAddRef<gfxPattern>(ToDeviceColor(stops.LastElement().mColor));
  }

  // Get the transform list (if there is one). We do this after the returns
  // above since this call can be expensive when "gradientUnits" is set to
  // "objectBoundingBox" (since that requiring a GetBBox() call).
  gfxMatrix patternMatrix = GetGradientTransform(aSource, aOverrideBounds);
  if (patternMatrix.IsSingular()) {
    return nullptr;
  }

  // revert any vector effect transform so that the gradient appears unchanged
  if (aFillOrStroke == &nsStyleSVG::mStroke) {
    gfxMatrix userToOuterSVG;
    if (SVGUtils::GetNonScalingStrokeTransform(aSource, &userToOuterSVG)) {
      patternMatrix *= userToOuterSVG;
    }
  }

  if (!patternMatrix.Invert()) {
    return nullptr;
  }

  RefPtr<gfxPattern> gradient = CreateGradient();
  if (!gradient) {
    return nullptr;
  }

  uint16_t aSpread = GetSpreadMethod();
  if (aSpread == SVG_SPREADMETHOD_PAD) {
    gradient->SetExtend(ExtendMode::CLAMP);
  } else if (aSpread == SVG_SPREADMETHOD_REFLECT) {
    gradient->SetExtend(ExtendMode::REFLECT);
  } else if (aSpread == SVG_SPREADMETHOD_REPEAT) {
    gradient->SetExtend(ExtendMode::REPEAT);
  }

  gradient->SetMatrix(patternMatrix);

  if (StyleSVG()->mColorInterpolation == StyleColorInterpolation::Linearrgb) {
    static constexpr auto interpolationMethod = StyleColorInterpolationMethod{
        StyleColorSpace::SrgbLinear, StyleHueInterpolationMethod::Shorter};
    SVGColorStopInterpolator interpolator(gradient, stops, interpolationMethod,
                                          false);
    interpolator.CreateStops();
  } else {
    // setup standard sRGB stops
    for (const auto& stop : stops) {
      gradient->AddColorStop(stop.mPosition, ToDeviceColor(stop.mColor));
    }
  }

  return gradient.forget();
}

// Private (helper) methods

float SVGGradientFrame::GetLengthValue(const SVGAnimatedLength& aLength) {
  // Object bounding box units are handled by setting the appropriate
  // transform in GetGradientTransform, but we need to handle user
  // space units as part of the individual Get* routines.  Fixes 323669.

  uint16_t gradientUnits = GetGradientUnits();
  if (gradientUnits == SVG_UNIT_TYPE_USERSPACEONUSE) {
    return SVGUtils::UserSpace(mSource, &aLength);
  }

  NS_ASSERTION(gradientUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
               "Unknown gradientUnits type");

  if (aLength.IsPercentage()) {
    // We want the percentage of the objectBoundingBox rather than
    // the percentage of the nearest viewport.
    return aLength.GetAnimValInSpecifiedUnits() / 100.0f;
  }
  return aLength.GetAnimValueWithZoom(
      static_cast<dom::SVGElement*>(GetContent()));
}

SVGGradientFrame* SVGGradientFrame::GetReferencedGradient() {
  if (mNoHRefURI) {
    return nullptr;
  }

  auto GetHref = [this](nsAString& aHref) {
    dom::SVGGradientElement* grad =
        static_cast<dom::SVGGradientElement*>(this->GetContent());
    if (grad->mStringAttributes[dom::SVGGradientElement::HREF]
            .IsExplicitlySet()) {
      grad->mStringAttributes[dom::SVGGradientElement::HREF].GetAnimValue(aHref,
                                                                          grad);
    } else {
      grad->mStringAttributes[dom::SVGGradientElement::XLINK_HREF].GetAnimValue(
          aHref, grad);
    }
    this->mNoHRefURI = aHref.IsEmpty();
  };

  // We don't call SVGObserverUtils::RemoveTemplateObserver and set
  // `mNoHRefURI = false` on failure since we want to be invalidated if the ID
  // specified by our href starts resolving to a different/valid element.

  return do_QueryFrame(SVGObserverUtils::GetAndObserveTemplate(this, GetHref));
}

void SVGGradientFrame::GetStops(nsTArray<ColorStop>* aStops,
                                float aGraphicOpacity) {
  float lastPosition = 0.0f;
  for (const auto* stopFrame : mFrames) {
    if (stopFrame->IsSVGStopFrame()) {
      aStops->AppendElement(
          GetStopInformation(stopFrame, aGraphicOpacity, lastPosition));
    }
  }
  if (!aStops->IsEmpty()) {
    return;
  }

  // Our gradient element doesn't have stops - try to "inherit" them

  // Before we recurse, make sure we'll break reference loops and over long
  // reference chains:
  static int16_t sRefChainLengthCounter = AutoReferenceChainGuard::noChain;
  AutoReferenceChainGuard refChainGuard(this, &mLoopFlag,
                                        &sRefChainLengthCounter);
  if (MOZ_UNLIKELY(!refChainGuard.Reference())) {
    // Break reference chain
    return;
  }

  SVGGradientFrame* next = GetReferencedGradient();
  if (next) {
    next->GetStops(aStops, aGraphicOpacity);
  }
}

// -------------------------------------------------------------------------
// Linear Gradients
// -------------------------------------------------------------------------

NS_QUERYFRAME_HEAD(SVGLinearGradientFrame)
  NS_QUERYFRAME_ENTRY(SVGLinearGradientFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGGradientFrame)

#ifdef DEBUG
void SVGLinearGradientFrame::Init(nsIContent* aContent,
                                  nsContainerFrame* aParent,
                                  nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::linearGradient),
               "Content is not an SVG linearGradient");

  SVGGradientFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsresult SVGLinearGradientFrame::AttributeChanged(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute,
                                                  AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x1 || aAttribute == nsGkAtoms::y1 ||
       aAttribute == nsGkAtoms::x2 || aAttribute == nsGkAtoms::y2)) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }

  return SVGGradientFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

//----------------------------------------------------------------------

float SVGLinearGradientFrame::GetLengthValue(uint32_t aIndex) {
  dom::SVGLinearGradientElement* lengthElement = GetLinearGradientWithLength(
      aIndex, static_cast<dom::SVGLinearGradientElement*>(GetContent()));
  // We passed in mContent as a fallback, so, assuming mContent is non-null, the
  // return value should also be non-null.
  MOZ_ASSERT(lengthElement,
             "Got unexpected null element from GetLinearGradientWithLength");

  return GetLengthValue(lengthElement->mLengthAttributes[aIndex]);
}

dom::SVGLinearGradientElement*
SVGLinearGradientFrame::GetLinearGradientWithLength(
    uint32_t aIndex, dom::SVGLinearGradientElement* aDefault) {
  dom::SVGLinearGradientElement* thisElement =
      static_cast<dom::SVGLinearGradientElement*>(GetContent());
  const SVGAnimatedLength& length = thisElement->mLengthAttributes[aIndex];

  if (length.IsExplicitlySet()) {
    return thisElement;
  }

  return SVGGradientFrame::GetLinearGradientWithLength(aIndex, aDefault);
}

bool SVGLinearGradientFrame::GradientVectorLengthIsZero() {
  return GetLengthValue(dom::SVGLinearGradientElement::ATTR_X1) ==
             GetLengthValue(dom::SVGLinearGradientElement::ATTR_X2) &&
         GetLengthValue(dom::SVGLinearGradientElement::ATTR_Y1) ==
             GetLengthValue(dom::SVGLinearGradientElement::ATTR_Y2);
}

already_AddRefed<gfxPattern> SVGLinearGradientFrame::CreateGradient() {
  float x1 = GetLengthValue(dom::SVGLinearGradientElement::ATTR_X1);
  float y1 = GetLengthValue(dom::SVGLinearGradientElement::ATTR_Y1);
  float x2 = GetLengthValue(dom::SVGLinearGradientElement::ATTR_X2);
  float y2 = GetLengthValue(dom::SVGLinearGradientElement::ATTR_Y2);

  return MakeAndAddRef<gfxPattern>(x1, y1, x2, y2);
}

// -------------------------------------------------------------------------
// Radial Gradients
// -------------------------------------------------------------------------

NS_QUERYFRAME_HEAD(SVGRadialGradientFrame)
  NS_QUERYFRAME_ENTRY(SVGRadialGradientFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGGradientFrame)

#ifdef DEBUG
void SVGRadialGradientFrame::Init(nsIContent* aContent,
                                  nsContainerFrame* aParent,
                                  nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::radialGradient),
               "Content is not an SVG radialGradient");

  SVGGradientFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsresult SVGRadialGradientFrame::AttributeChanged(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute,
                                                  AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::r || aAttribute == nsGkAtoms::cx ||
       aAttribute == nsGkAtoms::cy || aAttribute == nsGkAtoms::fx ||
       aAttribute == nsGkAtoms::fy)) {
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }

  return SVGGradientFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

//----------------------------------------------------------------------

float SVGRadialGradientFrame::GetLengthValue(uint32_t aIndex,
                                             Maybe<float> aDefaultValue) {
  dom::SVGRadialGradientElement* lengthElement = GetRadialGradientWithLength(
      aIndex, aDefaultValue.isNothing()
                  ? static_cast<dom::SVGRadialGradientElement*>(GetContent())
                  : nullptr);

  // If we passed our content as a fallback, then assuming that is non-null,
  // the return value should also be non-null.
  MOZ_ASSERT(aDefaultValue.isSome() || lengthElement,
             "Got unexpected null element from GetRadialGradientWithLength");

  return lengthElement
             ? GetLengthValue(lengthElement->mLengthAttributes[aIndex])
             : aDefaultValue.value();
}

dom::SVGRadialGradientElement*
SVGRadialGradientFrame::GetRadialGradientWithLength(
    uint32_t aIndex, dom::SVGRadialGradientElement* aDefault) {
  dom::SVGRadialGradientElement* thisElement =
      static_cast<dom::SVGRadialGradientElement*>(GetContent());
  const SVGAnimatedLength& length = thisElement->mLengthAttributes[aIndex];

  if (length.IsExplicitlySet()) {
    return thisElement;
  }

  return SVGGradientFrame::GetRadialGradientWithLength(aIndex, aDefault);
}

bool SVGRadialGradientFrame::GradientVectorLengthIsZero() {
  float cx = GetLengthValue(dom::SVGRadialGradientElement::ATTR_CX);
  float cy = GetLengthValue(dom::SVGRadialGradientElement::ATTR_CY);
  float r = GetLengthValue(dom::SVGRadialGradientElement::ATTR_R);
  // If fx or fy are not set, use cx/cy instead
  float fx = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FX, cx);
  float fy = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FY, cy);
  float fr = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FR);
  return cx == fx && cy == fy && r == fr;
}

already_AddRefed<gfxPattern> SVGRadialGradientFrame::CreateGradient() {
  float cx = GetLengthValue(dom::SVGRadialGradientElement::ATTR_CX);
  float cy = GetLengthValue(dom::SVGRadialGradientElement::ATTR_CY);
  float r = GetLengthValue(dom::SVGRadialGradientElement::ATTR_R);
  // If fx or fy are not set, use cx/cy instead
  float fx = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FX, cx);
  float fy = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FY, cy);
  float fr = GetLengthValue(dom::SVGRadialGradientElement::ATTR_FR);

  return MakeAndAddRef<gfxPattern>(fx, fy, fr, cx, cy, r);
}

}  // namespace mozilla

// -------------------------------------------------------------------------
// Public functions
// -------------------------------------------------------------------------

nsIFrame* NS_NewSVGLinearGradientFrame(mozilla::PresShell* aPresShell,
                                       mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGLinearGradientFrame(aStyle,
                                             aPresShell->GetPresContext());
}

nsIFrame* NS_NewSVGRadialGradientFrame(mozilla::PresShell* aPresShell,
                                       mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGRadialGradientFrame(aStyle,
                                             aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGLinearGradientFrame)
NS_IMPL_FRAMEARENA_HELPERS(SVGRadialGradientFrame)

}  // namespace mozilla
