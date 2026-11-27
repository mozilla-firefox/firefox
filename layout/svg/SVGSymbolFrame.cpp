/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGSymbolFrame.h"

#include "mozilla/PresShell.h"
#include "mozilla/dom/SVGSymbolElement.h"

nsIFrame* NS_NewSVGSymbolFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGSymbolFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGSymbolFrame)

//----------------------------------------------------------------------
// nsIFrame methods

NS_QUERYFRAME_HEAD(SVGSymbolFrame)
  NS_QUERYFRAME_ENTRY(SVGSymbolFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGViewportFrame)

void SVGSymbolFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                          nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(aContent->IsSVGElement(nsGkAtoms::symbol),
             "Content is not an SVG 'symbol' element!");

  if (!dom::SVGSymbolElement::FromNode(aContent)->CouldBeRendered()) {
    AddStateBits(NS_FRAME_IS_NONDISPLAY);
  }

  SVGViewportFrame::Init(aContent, aParent, aPrevInFlow);
}

void SVGSymbolFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                      const nsDisplayListSet& aLists) {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    return;
  }
  SVGViewportFrame::BuildDisplayList(aBuilder, aLists);
}

}  // namespace mozilla
