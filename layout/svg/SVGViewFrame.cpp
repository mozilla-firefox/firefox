/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "mozilla/PresShell.h"
#include "mozilla/SVGOuterSVGFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/dom/SVGViewElement.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"

using namespace mozilla::dom;

nsIFrame* NS_NewSVGViewFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle);

namespace mozilla {

/**
 * While views are not directly rendered in SVG they can be linked to
 * and thereby override attributes of an <svg> element via a fragment
 * identifier. The SVGViewFrame class passes on any attribute changes
 * the view receives to the overridden <svg> element (if there is one).
 **/
class SVGViewFrame final : public nsIFrame {
  friend nsIFrame* ::NS_NewSVGViewFrame(mozilla::PresShell* aPresShell,
                                        ComputedStyle* aStyle);

 protected:
  explicit SVGViewFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {
    AddStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY);
  }

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGViewFrame)

#ifdef DEBUG
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGView"_ns, aResult);
  }
#endif

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            AttrModType aModType) override;

  bool ComputeCustomOverflow(OverflowAreas& aOverflowAreas) override {
    // We don't maintain a ink overflow rect
    return false;
  }
};

}  // namespace mozilla

nsIFrame* NS_NewSVGViewFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGViewFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGViewFrame)

#ifdef DEBUG
void SVGViewFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::view),
               "Content is not an SVG view");

  nsIFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsresult SVGViewFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute,
                                        AttrModType aModType) {
  // Ignore zoomAndPan as it does not cause the <svg> element to re-render

  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::preserveAspectRatio ||
       aAttribute == nsGkAtoms::viewBox)) {
    SVGOuterSVGFrame* outerSVGFrame = SVGUtils::GetOuterSVGFrame(this);
    NS_ASSERTION(outerSVGFrame->GetContent()->IsSVGElement(nsGkAtoms::svg),
                 "Expecting an <svg> element");

    SVGSVGElement* svgElement =
        static_cast<SVGSVGElement*>(outerSVGFrame->GetContent());

    nsAutoString viewID;
    mContent->AsElement()->GetAttr(nsGkAtoms::id, viewID);

    if (svgElement->IsOverriddenBy(viewID)) {
      // We're the view that's providing overrides, so pretend that the frame
      // we're overriding was updated.
      outerSVGFrame->AttributeChanged(aNameSpaceID, aAttribute, aModType);
    }
  }

  return nsIFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

}  // namespace mozilla
