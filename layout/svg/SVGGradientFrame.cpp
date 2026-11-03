/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGGradientFrame.h"

#include <algorithm>

// Keep others in (case-insensitive) order:
#include "AutoReferenceChainGuard.h"
#include "SVGAnimatedTransformList.h"
#include "gfx2DGlue.h"
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
    nsIFrame* aSource, uint16_t aGradientUnits,
    const gfxRect* aOverrideBounds) {
  gfxMatrix bboxMatrix;
  if (aGradientUnits != SVG_UNIT_TYPE_USERSPACEONUSE) {
    NS_ASSERTION(aGradientUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
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

  if (nStops == 1 || GradientVectorLengthIsZero(gradientUnits)) {
    // The gradient paints a single colour, using the stop-color of the last
    // gradient step if there are more than one.
    return MakeAndAddRef<gfxPattern>(ToDeviceColor(stops.LastElement().mColor));
  }

  // Get the transform list (if there is one). We do this after the returns
  // above since this call can be expensive when "gradientUnits" is set to
  // "objectBoundingBox" (since that requiring a GetBBox() call).
  gfxMatrix patternMatrix =
      GetGradientTransform(aSource, gradientUnits, aOverrideBounds);
  if (patternMatrix.IsSingular()) {
    return nullptr;
  }

  // revert any vector effect transform so that the gradient appears unchanged
  if (aFillOrStroke == &nsStyleSVG::mStroke) {
    if (Maybe<gfxMatrix> userToOuterSVG =
            SVGUtils::GetNonScalingStrokeTransform(aSource)) {
      patternMatrix *= *userToOuterSVG;
    }
  }

  if (!patternMatrix.Invert()) {
    return nullptr;
  }

  RefPtr<gfxPattern> gradient = CreateGradient(gradientUnits);
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

class MOZ_STACK_CLASS SVGWrColorStopInterpolator
    : public ColorStopInterpolator<SVGWrColorStopInterpolator> {
 public:
  SVGWrColorStopInterpolator(
      const nsTArray<ColorStop>& aStops,
      const StyleColorInterpolationMethod& aStyleColorInterpolationMethod,
      nsTArray<wr::GradientStop>& aResult)
      : ColorStopInterpolator(aStops, aStyleColorInterpolationMethod, false),
        mResult(aResult) {}

  void CreateStop(float aPosition, DeviceColor aColor) {
    wr::GradientStop* stop = mResult.AppendElement();
    stop->color = wr::ToColorF(aColor);
    stop->offset = aPosition;
  }

 private:
  nsTArray<wr::GradientStop>& mResult;
};

Maybe<SVGGradientFrame::WebRenderGradient>
SVGGradientFrame::GetWebRenderGradient(nsIFrame* aSource,
                                       float aGraphicOpacity) {
  uint16_t gradientUnits = GetGradientUnits();
  if (gradientUnits == SVG_UNIT_TYPE_USERSPACEONUSE) {
    mSource = aSource->IsTextFrame() ? aSource->GetParent() : aSource;
  }

  WebRenderGradient result;

  AutoTArray<ColorStop, 8> stops;
  GetStops(&stops, aGraphicOpacity);

  if (stops.IsEmpty()) {
    result.mSolidColor.emplace(DeviceColor());
    return Some(std::move(result));
  }
  if (stops.Length() == 1 || GradientVectorLengthIsZero(gradientUnits)) {
    result.mSolidColor.emplace(ToDeviceColor(stops.LastElement().mColor));
    return Some(std::move(result));
  }

  gfxMatrix gradientToUserSpace =
      GetGradientTransform(aSource, gradientUnits, nullptr);
  if (gradientToUserSpace.IsSingular()) {
    return Nothing();
  }

  if (!GetWebRenderGeometry(gradientUnits, gradientToUserSpace, result)) {
    return Nothing();
  }

  // spreadMethod="reflect" is expressed as a repeating gradient over twice
  // the vector whose second half mirrors the stops.
  bool reflect = false;
  switch (GetSpreadMethod()) {
    case SVG_SPREADMETHOD_REFLECT:
      reflect = true;
      result.mExtendMode = wr::ExtendMode::Repeat;
      break;
    case SVG_SPREADMETHOD_REPEAT:
      result.mExtendMode = wr::ExtendMode::Repeat;
      break;
    default:
      result.mExtendMode = wr::ExtendMode::Clamp;
      break;
  }

  if (StyleSVG()->mColorInterpolation == StyleColorInterpolation::Linearrgb) {
    static constexpr auto interpolationMethod = StyleColorInterpolationMethod{
        StyleColorSpace::SrgbLinear, StyleHueInterpolationMethod::Shorter};
    SVGWrColorStopInterpolator interpolator(stops, interpolationMethod,
                                            result.mStops);
    interpolator.CreateStops();
  } else {
    for (const auto& stop : stops) {
      wr::GradientStop* wrStop = result.mStops.AppendElement();
      wrStop->color = wr::ToColorF(ToDeviceColor(stop.mColor));
      wrStop->offset = stop.mPosition;
    }
  }

  if (reflect) {
    switch (result.mType) {
      case GradientType::Linear:
        result.mEnd = result.mStart + (result.mEnd - result.mStart) * 2.0f;
        break;
      case GradientType::Radial:
        result.mRadii = result.mRadii * 2.0f;
        break;
    }
    nsTArray<wr::GradientStop> mirrored;
    mirrored.SetCapacity(result.mStops.Length() * 2);
    for (const auto& stop : result.mStops) {
      wr::GradientStop* s = mirrored.AppendElement();
      s->color = stop.color;
      s->offset = stop.offset / 2.0f;
    }
    for (const auto& stop : Reversed(result.mStops)) {
      wr::GradientStop* s = mirrored.AppendElement();
      s->color = stop.color;
      s->offset = 1.0f - stop.offset / 2.0f;
    }
    result.mStops = std::move(mirrored);
  }

  return Some(std::move(result));
}

// Private (helper) methods

float SVGGradientFrame::GetLengthValue(uint16_t aGradientUnits,
                                       const SVGAnimatedLength& aLength) {
  // Object bounding box units are handled by setting the appropriate
  // transform in GetGradientTransform, but we need to handle user
  // space units as part of the individual Get* routines.  Fixes 323669.

  if (aGradientUnits == SVG_UNIT_TYPE_USERSPACEONUSE) {
    return SVGUtils::UserSpace(mSource, &aLength);
  }

  NS_ASSERTION(aGradientUnits == SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
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

float SVGLinearGradientFrame::GetLengthValue(uint16_t aGradientUnits,
                                             uint32_t aIndex) {
  dom::SVGLinearGradientElement* lengthElement = GetLinearGradientWithLength(
      aIndex, static_cast<dom::SVGLinearGradientElement*>(GetContent()));
  // We passed in mContent as a fallback, so, assuming mContent is non-null, the
  // return value should also be non-null.
  MOZ_ASSERT(lengthElement,
             "Got unexpected null element from GetLinearGradientWithLength");

  return GetLengthValue(aGradientUnits,
                        lengthElement->mLengthAttributes[aIndex]);
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

bool SVGLinearGradientFrame::GradientVectorLengthIsZero(
    uint16_t aGradientUnits) {
  return GetLengthValue(aGradientUnits,
                        dom::SVGLinearGradientElement::ATTR_X1) ==
             GetLengthValue(aGradientUnits,
                            dom::SVGLinearGradientElement::ATTR_X2) &&
         GetLengthValue(aGradientUnits,
                        dom::SVGLinearGradientElement::ATTR_Y1) ==
             GetLengthValue(aGradientUnits,
                            dom::SVGLinearGradientElement::ATTR_Y2);
}

already_AddRefed<gfxPattern> SVGLinearGradientFrame::CreateGradient(
    uint16_t aGradientUnits) {
  float x1 =
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_X1);
  float y1 =
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_Y1);
  float x2 =
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_X2);
  float y2 =
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_Y2);

  return MakeAndAddRef<gfxPattern>(x1, y1, x2, y2);
}

bool SVGLinearGradientFrame::GetWebRenderGeometry(
    uint16_t aGradientUnits, const gfxMatrix& aGradientToUserSpace,
    WebRenderGradient& aGradient) {
  gfxPoint p1(
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_X1),
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_Y1));
  gfxPoint p2(
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_X2),
      GetLengthValue(aGradientUnits, dom::SVGLinearGradientElement::ATTR_Y2));

  // Transforming the two end points is only equivalent to transforming the
  // gradient when the lines of constant color, which are perpendicular to the
  // vector, remain perpendicular to the transformed vector.
  const gfxMatrix& m = aGradientToUserSpace;
  double vx = p2.x - p1.x;
  double vy = p2.y - p1.y;
  double nx = -vy;
  double ny = vx;
  double mvx = m._11 * vx + m._21 * vy;
  double mvy = m._12 * vx + m._22 * vy;
  double mnx = m._11 * nx + m._21 * ny;
  double mny = m._12 * nx + m._22 * ny;
  double dot = mvx * mnx + mvy * mny;
  double norm = std::sqrt((mvx * mvx + mvy * mvy) * (mnx * mnx + mny * mny));
  if (norm == 0.0 || std::abs(dot) > 1e-4 * norm) {
    return false;
  }

  aGradient.mType = GradientType::Linear;
  aGradient.mStart = CSSPoint::FromUnknownPoint(ToPoint(m.TransformPoint(p1)));
  aGradient.mEnd = CSSPoint::FromUnknownPoint(ToPoint(m.TransformPoint(p2)));
  return true;
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

float SVGRadialGradientFrame::GetLengthValue(uint16_t aGradientUnits,
                                             uint32_t aIndex,
                                             Maybe<float> aDefaultValue) {
  dom::SVGRadialGradientElement* lengthElement = GetRadialGradientWithLength(
      aIndex, !aDefaultValue
                  ? static_cast<dom::SVGRadialGradientElement*>(GetContent())
                  : nullptr);

  // If we passed our content as a fallback, then assuming that is non-null,
  // the return value should also be non-null.
  MOZ_ASSERT(aDefaultValue || lengthElement,
             "Got unexpected null element from GetRadialGradientWithLength");

  return lengthElement
             ? GetLengthValue(aGradientUnits,
                              lengthElement->mLengthAttributes[aIndex])
             : *aDefaultValue;
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

bool SVGRadialGradientFrame::GradientVectorLengthIsZero(
    uint16_t aGradientUnits) {
  float cx =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CX);
  float cy =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CY);
  float r =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_R);
  // If fx or fy are not set, use cx/cy instead
  float fx = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FX, cx);
  float fy = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FY, cy);
  float fr =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_FR);
  return cx == fx && cy == fy && r == fr;
}

already_AddRefed<gfxPattern> SVGRadialGradientFrame::CreateGradient(
    uint16_t aGradientUnits) {
  float cx =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CX);
  float cy =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CY);
  float r =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_R);
  if (r < 0) {
    return nullptr;
  }
  // If fx or fy are not set, use cx/cy instead
  float fx = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FX, cx);
  float fy = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FY, cy);
  float fr =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_FR);
  if (fr < 0) {
    return nullptr;
  }

  return MakeAndAddRef<gfxPattern>(fx, fy, fr, cx, cy, r);
}

bool SVGRadialGradientFrame::GetWebRenderGeometry(
    uint16_t aGradientUnits, const gfxMatrix& aGradientToUserSpace,
    WebRenderGradient& aGradient) {
  float cx =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CX);
  float cy =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_CY);
  float r =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_R);
  float fx = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FX, cx);
  float fy = GetLengthValue(aGradientUnits,
                            dom::SVGRadialGradientElement::ATTR_FY, cy);
  float fr =
      GetLengthValue(aGradientUnits, dom::SVGRadialGradientElement::ATTR_FR);

  // WebRender radial gradients have no focal point.
  if (fx != cx || fy != cy || fr != 0.0f) {
    return false;
  }

  // A circle maps to an axis aligned ellipse under a scale, and to a circle
  // under a similarity. Anything else (skew, non-uniform scale with rotation)
  // has no WebRender equivalent.
  const gfxMatrix& m = aGradientToUserSpace;
  double exx = m._11 * r;
  double exy = m._12 * r;
  double eyx = m._21 * r;
  double eyy = m._22 * r;
  CSSSize radii;
  if (!m.HasNonAxisAlignedTransform()) {
    radii = CSSSize(std::abs(exx), std::abs(eyy));
  } else {
    double lx = std::hypot(exx, exy);
    double ly = std::hypot(eyx, eyy);
    double dot = exx * eyx + exy * eyy;
    if (std::abs(lx - ly) > 1e-4 * lx || std::abs(dot) > 1e-4 * lx * ly) {
      return false;
    }
    radii = CSSSize(lx, ly);
  }

  aGradient.mType = GradientType::Radial;
  aGradient.mCenter =
      CSSPoint::FromUnknownPoint(ToPoint(m.TransformPoint(gfxPoint(cx, cy))));
  aGradient.mRadii = radii;
  return true;
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
