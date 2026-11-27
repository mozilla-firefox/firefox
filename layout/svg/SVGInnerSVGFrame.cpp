/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGInnerSVGFrame.h"

#include "mozilla/PresShell.h"

nsIFrame* NS_NewSVGInnerSVGFrame(mozilla::PresShell* aPresShell,
                                 mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGInnerSVGFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGInnerSVGFrame)

//----------------------------------------------------------------------
// nsIFrame methods

NS_QUERYFRAME_HEAD(SVGInnerSVGFrame)
  NS_QUERYFRAME_ENTRY(SVGInnerSVGFrame)
  NS_QUERYFRAME_ENTRY(ISVGSVGFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGViewportFrame)

#ifdef DEBUG
void SVGInnerSVGFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::svg),
               "Content is not an SVG 'svg' element!");

  SVGViewportFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

}  // namespace mozilla
