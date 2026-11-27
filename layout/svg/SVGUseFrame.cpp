/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SVGUseFrame.h"

#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGUseElement.h"
#include "nsLayoutUtils.h"

using namespace mozilla::dom;

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGUseFrame(mozilla::PresShell* aPresShell,
                            mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGUseFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGUseFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

void SVGUseFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                       nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::use),
               "Content is not an SVG use!");

  mHasValidDimensions =
      static_cast<SVGUseElement*>(aContent)->HasValidDimensions();

  SVGGFrame::Init(aContent, aParent, aPrevInFlow);
}

void SVGUseFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  SVGGFrame::DidSetComputedStyle(aOldComputedStyle);

  if (!aOldComputedStyle) {
    return;
  }
  const auto* newSVGReset = StyleSVGReset();
  const auto* oldSVGReset = aOldComputedStyle->StyleSVGReset();

  if (newSVGReset->mX != oldSVGReset->mX ||
      newSVGReset->mY != oldSVGReset->mY) {
    // make sure our cached transform matrix gets (lazily) updated
    mCanvasTM = nullptr;
    SVGUtils::ScheduleReflowSVG(this);
    SVGUtils::NotifyChildrenOfSVGChange(this, ChangeFlag::TransformChanged);
  }
}

void SVGUseFrame::DimensionAttributeChanged(bool aHadValidDimensions,
                                            bool aAttributeIsUsed) {
  bool invalidate = aAttributeIsUsed;
  if (mHasValidDimensions != aHadValidDimensions) {
    mHasValidDimensions = !mHasValidDimensions;
    invalidate = true;
  }

  if (invalidate) {
    nsLayoutUtils::PostRestyleEvent(GetContent()->AsElement(), RestyleHint{0},
                                    nsChangeHint_InvalidateRenderingObservers);
    SVGUtils::ScheduleReflowSVG(this);
  }
}

void SVGUseFrame::HrefChanged() {
  nsLayoutUtils::PostRestyleEvent(GetContent()->AsElement(), RestyleHint{0},
                                  nsChangeHint_InvalidateRenderingObservers);
  SVGUtils::ScheduleReflowSVG(this);
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

void SVGUseFrame::ReflowSVG() {
  // We only handle x/y offset here, since any width/height that is in force is
  // handled by the SVGOuterSVGFrame for the anonymous <svg> that will be
  // created for that purpose.
  auto* content = SVGUseElement::FromNode(GetContent());
  float x = SVGContentUtils::CoordToFloat(content, StyleSVGReset()->mX,
                                          SVGLength::Axis::X);
  float y = SVGContentUtils::CoordToFloat(content, StyleSVGReset()->mY,
                                          SVGLength::Axis::Y);
  mRect.MoveTo(nsLayoutUtils::RoundGfxRectToAppRect(gfxRect(x, y, 0, 0),
                                                    AppUnitsPerCSSPixel())
                   .TopLeft());

  // If we have a filter, we need to invalidate ourselves because filter
  // output can change even if none of our descendants need repainting.
  if (StyleEffects()->HasFilters()) {
    InvalidateFrame();
  }

  SVGGFrame::ReflowSVG();
}

void SVGUseFrame::NotifySVGChanged(ChangeFlags aFlags) {
  if (aFlags.contains(ChangeFlag::CoordContextChanged) &&
      !aFlags.contains(ChangeFlag::TransformChanged)) {
    // Coordinate context changes affect mCanvasTM if we have a
    // percentage 'x' or 'y'
    if (StyleSVGReset()->mX.HasPercent() || StyleSVGReset()->mY.HasPercent()) {
      aFlags += ChangeFlag::TransformChanged;
      // Ancestor changes can't affect how we render from the perspective of
      // any rendering observers that we may have, so we don't need to
      // invalidate them. We also don't need to invalidate ourself, since our
      // changed ancestor will have invalidated its entire area, which includes
      // our area.
      // For perf reasons we call this before calling NotifySVGChanged() below.
      SVGUtils::ScheduleReflowSVG(this);
    }
  }

  // We don't remove the TRANSFORM_CHANGED flag here if we have a viewBox or
  // non-percentage width/height, since if they're set then they are cloned to
  // an anonymous child <svg>, and its SVGInnerSVGFrame will do that.

  SVGGFrame::NotifySVGChanged(aFlags);
}

}  // namespace mozilla
