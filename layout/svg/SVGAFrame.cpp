/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Keep in (case-insensitive) order:
#include "SVGLengthList.h"
#include "gfxMatrix.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGContainerFrame.h"
#include "mozilla/dom/SVGAElement.h"

nsIFrame* NS_NewSVGAFrame(mozilla::PresShell* aPresShell,
                          mozilla::ComputedStyle* aStyle);

namespace mozilla {

class SVGAFrame final : public SVGDisplayContainerFrame {
  friend nsIFrame* ::NS_NewSVGAFrame(mozilla::PresShell* aPresShell,
                                     ComputedStyle* aStyle);

 protected:
  explicit SVGAFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : SVGDisplayContainerFrame(aStyle, aPresContext, kClassID) {}

 public:
  NS_DECL_FRAMEARENA_HELPERS(SVGAFrame)

#ifdef DEBUG
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
#endif

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"SVGA"_ns, aResult);
  }
#endif
};

}  // namespace mozilla

//----------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGAFrame(mozilla::PresShell* aPresShell,
                          mozilla::ComputedStyle* aStyle) {
  return new mozilla::SVGAFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGAFrame)

//----------------------------------------------------------------------
// nsIFrame methods
#ifdef DEBUG
void SVGAFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                     nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::a),
               "Trying to construct an SVGAFrame for a "
               "content element that doesn't support the right interfaces");

  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

}  // namespace mozilla
