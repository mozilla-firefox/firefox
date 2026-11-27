/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGGradientFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "nsContainerFrame.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"

// This is a very simple frame whose only purpose is to capture style change
// events and propagate them to the parent.  Most of the heavy lifting is done
// within the SVGGradientFrame, which is the parent for this frame

nsIFrame* NS_NewSVGStopFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle);

namespace mozilla {

class SVGStopFrame : public nsIFrame {
  friend nsIFrame* ::NS_NewSVGStopFrame(mozilla::PresShell* aPresShell,
                                        ComputedStyle* aStyle);

 protected:
  explicit SVGStopFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {
    AddStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY);
  }

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGStopFrame)

  // nsIFrame interface:
#ifdef DEBUG
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
#endif

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override {}

  nsresult AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                            AttrModType aModType) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGStop"_ns, aResult);
  }
#endif
};

//----------------------------------------------------------------------
// Implementation

NS_IMPL_FRAMEARENA_HELPERS(SVGStopFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

#ifdef DEBUG
void SVGStopFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::stop),
               "Content is not a stop element");

  nsIFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsresult SVGStopFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute,
                                        AttrModType aModType) {
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::offset) {
    MOZ_ASSERT(
        static_cast<SVGGradientFrame*>(do_QueryFrame(GetParent())),
        "Observers observe the gradient, so that's what we must invalidate");
    SVGObserverUtils::InvalidateRenderingObservers(GetParent());
  }

  return nsIFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

}  // namespace mozilla

// -------------------------------------------------------------------------
// Public functions
// -------------------------------------------------------------------------

nsIFrame* NS_NewSVGStopFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGStopFrame(aStyle, aPresShell->GetPresContext());
}
