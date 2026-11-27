/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/SVGFilters.h"
#include "nsContainerFrame.h"
#include "nsIFrame.h"

using namespace mozilla::dom;

nsIFrame* NS_NewSVGFELeafFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle);
namespace mozilla {

/*
 * This frame is used by filter primitive elements that don't
 * have special child elements that provide parameters.
 */
class SVGFELeafFrame final : public nsIFrame {
  friend nsIFrame* ::NS_NewSVGFELeafFrame(mozilla::PresShell* aPresShell,
                                          ComputedStyle* aStyle);

 protected:
  explicit SVGFELeafFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {
    AddStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY);
  }

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGFELeafFrame)

#ifdef DEBUG
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGFELeaf"_ns, aResult);
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

nsIFrame* NS_NewSVGFELeafFrame(mozilla::PresShell* aPresShell,
                               mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGFELeafFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGFELeafFrame)

#ifdef DEBUG
void SVGFELeafFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                          nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGFilterPrimitiveElement(),
               "Trying to construct an SVGFELeafFrame for a "
               "content element that doesn't support the right interfaces");

  nsIFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsresult SVGFELeafFrame::AttributeChanged(int32_t aNameSpaceID,
                                          nsAtom* aAttribute,
                                          AttrModType aModType) {
  auto* element =
      static_cast<mozilla::dom::SVGFilterPrimitiveElement*>(GetContent());
  if (element->AttributeAffectsRendering(aNameSpaceID, aAttribute)) {
    MOZ_ASSERT(
        GetParent()->IsSVGFilterFrame(),
        "Observers observe the filter, so that's what we must invalidate");
    SVGObserverUtils::InvalidateRenderingObservers(GetParent());
  }

  return nsIFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

}  // namespace mozilla
