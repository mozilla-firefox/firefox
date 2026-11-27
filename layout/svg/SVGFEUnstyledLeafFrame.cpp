/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/dom/SVGFilters.h"
#include "nsContainerFrame.h"
#include "nsIFrame.h"

nsIFrame* NS_NewSVGFEUnstyledLeafFrame(mozilla::PresShell* aPresShell,
                                       mozilla::ComputedStyle* aStyle);

namespace mozilla {

class SVGFEUnstyledLeafFrame final : public nsIFrame {
  friend nsIFrame* ::NS_NewSVGFEUnstyledLeafFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle);

 protected:
  explicit SVGFEUnstyledLeafFrame(ComputedStyle* aStyle,
                                  nsPresContext* aPresContext)
      : nsIFrame(aStyle, aPresContext, kClassID) {
    AddStateBits(NS_FRAME_SVG_LAYOUT | NS_FRAME_IS_NONDISPLAY);
  }

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGFEUnstyledLeafFrame)

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override {}

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGFEUnstyledLeaf"_ns, aResult);
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

nsIFrame* NS_NewSVGFEUnstyledLeafFrame(mozilla::PresShell* aPresShell,
                                       mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGFEUnstyledLeafFrame(aStyle,
                                             aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGFEUnstyledLeafFrame)

nsresult SVGFEUnstyledLeafFrame::AttributeChanged(int32_t aNameSpaceID,
                                                  nsAtom* aAttribute,
                                                  AttrModType aModType) {
  auto* element =
      static_cast<mozilla::dom::SVGFilterPrimitiveChildElement*>(GetContent());
  if (element->AttributeAffectsRendering(aNameSpaceID, aAttribute)) {
    MOZ_ASSERT(
        GetParent()->GetParent()->IsSVGFilterFrame(),
        "Observers observe the filter, so that's what we must invalidate");
    SVGObserverUtils::InvalidateRenderingObservers(GetParent()->GetParent());
  }

  return nsIFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
}

}  // namespace mozilla
