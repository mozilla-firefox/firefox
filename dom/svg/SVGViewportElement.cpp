/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/SVGViewportElement.h"

#include <algorithm>

#include "DOMSVGLength.h"
#include "DOMSVGPoint.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/SMILTypes.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/SVGLengthBinding.h"
#include "mozilla/dom/SVGViewElement.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsStyleUtil.h"
#include "prtime.h"

using namespace mozilla::gfx;

namespace mozilla::dom {

SVGElement::LengthInfo SVGViewportElement::sLengthInfo[4] = {
    {nsGkAtoms::x, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::X},
    {nsGkAtoms::y, 0, SVGLength_Binding::SVG_LENGTHTYPE_NUMBER,
     SVGLength::Axis::Y},
    {nsGkAtoms::width, 100, SVGLength_Binding::SVG_LENGTHTYPE_PERCENTAGE,
     SVGLength::Axis::X},
    {nsGkAtoms::height, 100, SVGLength_Binding::SVG_LENGTHTYPE_PERCENTAGE,
     SVGLength::Axis::Y},
};

//----------------------------------------------------------------------
// Implementation

SVGViewportElement::SVGViewportElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : SVGGraphicsElement(std::move(aNodeInfo)) {}

//----------------------------------------------------------------------

already_AddRefed<SVGAnimatedRect> SVGViewportElement::ViewBox() {
  return mViewBox.ToSVGAnimatedRect(this);
}

already_AddRefed<DOMSVGAnimatedPreserveAspectRatio>
SVGViewportElement::PreserveAspectRatio() {
  return mPreserveAspectRatio.ToDOMAnimatedPreserveAspectRatio(this);
}

//----------------------------------------------------------------------
// nsIContent methods

bool SVGViewportElement::IsNoNamespaceAttrMapped(const nsAtom* name) const {
  // We want to map the 'width' and 'height' attributes into style for
  // outer-<svg>, except when the attributes aren't set (since their default
  // values of '100%' can cause unexpected and undesirable behaviour for SVG
  // inline in HTML). We rely on SVGElement::UpdateContentStyleRule() to
  // prevent mapping of the default values into style (it only maps attributes
  // that are set). We also rely on a check in SVGElement::
  // UpdateContentStyleRule() to prevent us mapping the attributes when they're
  // given a <length> value that is not currently recognized by the SVG
  // specification.

  if (!IsInner() && (name == nsGkAtoms::width || name == nsGkAtoms::height)) {
    return true;
  }

  return SVGGraphicsElement::IsNoNamespaceAttrMapped(name);
}

//----------------------------------------------------------------------
// SVGElement overrides

// Helper for GetViewBoxTransform on root <svg> node
// * aLength: internal value for our <svg> width or height attribute.
// * aViewportLength: length of the corresponding dimension of the viewport.
// * aSelf: the outermost <svg> node itself.
// NOTE: aSelf is not an ancestor viewport element, so it can't be used to
// resolve percentage lengths. (It can only be used to resolve
// 'em'/'ex'-valued units).
inline float ComputeSynthesizedViewBoxDimension(
    const SVGAnimatedLength& aLength, float aViewportLength,
    const SVGViewportElement* aSelf) {
  if (aLength.IsPercentage()) {
    return aViewportLength * aLength.GetAnimValInSpecifiedUnits() / 100.0f;
  }

  return aLength.GetAnimValueWithZoom(aSelf);
}

//----------------------------------------------------------------------
// public helpers:

void SVGViewportElement::UpdateHasChildrenOnlyTransform() {
  mHasChildrenOnlyTransform =
      HasViewBoxOrSyntheticViewBox() ||
      (IsRootSVGSVGElement() &&
       static_cast<SVGSVGElement*>(this)->IsScaledOrTranslated());
}

void SVGViewportElement::ChildrenOnlyTransformChanged(
    ChildrenOnlyTransformChangedFlags aFlags) {
  // Avoid wasteful calls:
  MOZ_ASSERT(!GetPrimaryFrame()->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "Non-display SVG frames don't maintain overflow rects");

  nsChangeHint changeHint;

  bool hadChildrenOnlyTransform = mHasChildrenOnlyTransform;

  UpdateHasChildrenOnlyTransform();

  if (hadChildrenOnlyTransform != mHasChildrenOnlyTransform) {
    // Reconstruct the frame tree to handle stacking context changes:
    // XXXjwatt don't do this for root-<svg> or even outer-<svg>?
    changeHint = nsChangeHint_ReconstructFrame;
  } else {
    // We just assume the old and new transforms are different.
    changeHint = nsChangeHint(nsChangeHint_UpdateOverflow |
                              nsChangeHint_ChildrenOnlyTransform);
  }

  // If we're not reconstructing the frame tree, then we only call
  // PostRestyleEvent if we're not being called under reflow to avoid recursing
  // to death. See bug 767056 comments 10 and 12. Since our SVGOuterSVGFrame
  // is being reflowed we're going to invalidate and repaint its entire area
  // anyway (which will include our children).
  if ((changeHint & nsChangeHint_ReconstructFrame) ||
      !aFlags.contains(ChildrenOnlyTransformChangedFlag::DuringReflow)) {
    nsLayoutUtils::PostRestyleEvent(this, RestyleHint{0}, changeHint);
  }
}

gfx::Matrix SVGViewportElement::GetViewBoxTransform() const {
  float viewportWidth, viewportHeight;
  if (IsInner()) {
    SVGElementMetrics metrics(this);
    viewportWidth = mLengthAttributes[ATTR_WIDTH].GetAnimValueWithZoom(metrics);
    viewportHeight =
        mLengthAttributes[ATTR_HEIGHT].GetAnimValueWithZoom(metrics);
  } else {
    viewportWidth = mViewportSize.width;
    viewportHeight = mViewportSize.height;
  }

  if (!std::isfinite(viewportWidth) || viewportWidth <= 0.0f ||
      !std::isfinite(viewportHeight) || viewportHeight <= 0.0f) {
    return gfx::Matrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // singular
  }

  SVGViewBox viewBox = GetViewBoxWithSynthesis(viewportWidth, viewportHeight);

  if (viewBox.IsEmpty() || !viewBox.IsFinite()) {
    return gfx::Matrix(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // singular
  }

  return SVGContentUtils::GetViewBoxTransform(
      viewportWidth, viewportHeight, viewBox.x, viewBox.y, viewBox.width,
      viewBox.height, GetPreserveAspectRatioWithOverride());
}
//----------------------------------------------------------------------
// SVGViewportElement

float SVGViewportElement::GetLength(SVGLength::Axis aAxis) const {
  const auto& animatedViewBox = GetViewBoxInternal();
  gfxSize size;
  bool shouldComputeWidth =
           (aAxis == SVGLength::Axis::X || aAxis == SVGLength::Axis::XY),
       shouldComputeHeight =
           (aAxis == SVGLength::Axis::Y || aAxis == SVGLength::Axis::XY);

  if (animatedViewBox.HasRect()) {
    float zoom = UserSpaceMetrics::GetZoom(this);
    size = ThebesSize(animatedViewBox.GetAnimValue().Size() * zoom);
  } else if (IsInner()) {
    // Resolving length for inner <svg> is exactly the same as other
    // ordinary element. We shouldn't use the SVGViewportElement overload
    // of GetAnimValue().
    SVGElementMetrics metrics(this);
    if (shouldComputeWidth) {
      size.width = mLengthAttributes[ATTR_WIDTH].GetAnimValueWithZoom(metrics);
    }
    if (shouldComputeHeight) {
      size.height =
          mLengthAttributes[ATTR_HEIGHT].GetAnimValueWithZoom(metrics);
    }
  } else if (ShouldSynthesizeViewBox()) {
    if (shouldComputeWidth) {
      size.width = ComputeSynthesizedViewBoxDimension(
          mLengthAttributes[ATTR_WIDTH], mViewportSize.width, this);
    }
    if (shouldComputeHeight) {
      size.height = ComputeSynthesizedViewBoxDimension(
          mLengthAttributes[ATTR_HEIGHT], mViewportSize.height, this);
    }
  } else {
    size = ThebesSize(mViewportSize);
  }

  size.width = std::max(size.width, 0.0);
  size.height = std::max(size.height, 0.0);

  return float(SVGContentUtils::AxisLength(size, aAxis));
}

//----------------------------------------------------------------------
// SVGElement methods

/* virtual */
gfxMatrix SVGViewportElement::ChildToUserSpaceTransform() const {
  auto viewBox = GetViewBoxTransform();
  if (IsInner()) {
    float x, y;
    const_cast<SVGViewportElement*>(this)->GetAnimatedLengthValues(&x, &y,
                                                                   nullptr);
    return ThebesMatrix(viewBox.PostTranslate(x, y));
  }
  if (IsRootSVGSVGElement()) {
    const auto* svg = static_cast<const SVGSVGElement*>(this);
    const Point& translate = svg->GetCurrentTranslate();
    float scale = svg->CurrentScale();
    return ThebesMatrix(
        viewBox.PostScale(scale, scale).PostTranslate(translate));
  }
  // outer-<svg>, but inline in some other content:
  return ThebesMatrix(viewBox);
}

/* virtual */
bool SVGViewportElement::HasValidDimensions() const {
  return (!mLengthAttributes[ATTR_WIDTH].IsExplicitlySet() ||
          mLengthAttributes[ATTR_WIDTH].GetAnimValInSpecifiedUnits() > 0) &&
         (!mLengthAttributes[ATTR_HEIGHT].IsExplicitlySet() ||
          mLengthAttributes[ATTR_HEIGHT].GetAnimValInSpecifiedUnits() > 0);
}

SVGAnimatedViewBox* SVGViewportElement::GetAnimatedViewBox() {
  return &mViewBox;
}

SVGAnimatedPreserveAspectRatio*
SVGViewportElement::GetAnimatedPreserveAspectRatio() {
  return &mPreserveAspectRatio;
}

bool SVGViewportElement::ShouldSynthesizeViewBox() const {
  MOZ_ASSERT(!HasViewBox(), "Should only be called if we lack a viewBox");

  // We synthesize a viewbox if we're the root node of an SVG image
  // document (and lack an explicit viewBox), as long as our width &
  // height attributes wouldn't yield an empty synthesized viewbox.
  return IsRootSVGSVGElement() && OwnerDoc()->IsBeingUsedAsImage() &&
         HasValidDimensions();
}

//----------------------------------------------------------------------
// implementation helpers

SVGViewBox SVGViewportElement::GetViewBoxWithSynthesis(
    float aViewportWidth, float aViewportHeight) const {
  const auto& animatedViewBox = GetViewBoxInternal();
  if (animatedViewBox.HasRect()) {
    float zoom = UserSpaceMetrics::GetZoom(this);
    return animatedViewBox.GetAnimValue() * zoom;
  }

  if (ShouldSynthesizeViewBox()) {
    // Special case -- fake a viewBox, using height & width attrs.
    // (Use |this| as context, since if we get here, we're outermost <svg>.)
    return SVGViewBox(
        0, 0,
        ComputeSynthesizedViewBoxDimension(mLengthAttributes[ATTR_WIDTH],
                                           mViewportSize.width, this),
        ComputeSynthesizedViewBoxDimension(mLengthAttributes[ATTR_HEIGHT],
                                           mViewportSize.height, this));
  }

  // No viewBox attribute, so we shouldn't auto-scale. This is equivalent
  // to having a viewBox that exactly matches our viewport size.
  return SVGViewBox(0, 0, aViewportWidth, aViewportHeight);
}

SVGElement::LengthAttributesInfo SVGViewportElement::GetLengthInfo() {
  return LengthAttributesInfo(mLengthAttributes, sLengthInfo,
                              std::size(sLengthInfo));
}

}  // namespace mozilla::dom
