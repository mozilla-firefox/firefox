/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLmrowFrame.h"

#include "mozilla/PresShell.h"
#include "mozilla/gfx/2D.h"

using namespace mozilla;

//
// <mrow> -- horizontally group any number of subexpressions - implementation
//

nsIFrame* NS_NewMathMLmrowFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsMathMLmrowFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmrowFrame)

nsMathMLmrowFrame::~nsMathMLmrowFrame() = default;

NS_IMETHODIMP
nsMathMLmrowFrame::InheritAutomaticData(nsIFrame* aParent) {
  // let the base class get the default from our parent
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  mPresentationData.flags +=
      MathMLPresentationFlag::StretchAllChildrenVertically;

  return NS_OK;
}

nsresult nsMathMLmrowFrame::AttributeChanged(int32_t aNameSpaceID,
                                             nsAtom* aAttribute,
                                             AttrModType aModType) {
  // Special for <mtable>: In the frame construction code, we also use
  // this frame class as a wrapper for mtable. Hence, we should pass the
  // notification to the real mtable
  if (mContent->IsMathMLElement(nsGkAtoms::mtable)) {
    nsIFrame* frame = mFrames.FirstChild();
    for (; frame; frame = frame->PrincipalChildList().FirstChild()) {
      // drill down to the real mtable
      if (frame->IsTableWrapperFrame()) {
        return frame->AttributeChanged(aNameSpaceID, aAttribute, aModType);
      }
    }
    MOZ_ASSERT_UNREACHABLE("mtable wrapper without the real table frame");
  }

  return nsMathMLContainerFrame::AttributeChanged(aNameSpaceID, aAttribute,
                                                  aModType);
}

/* virtual */
MathMLFrameType nsMathMLmrowFrame::GetMathMLFrameType() {
  if (!IsMrowLike()) {
    nsIMathMLFrame* child = do_QueryFrame(mFrames.FirstChild());
    if (child) {
      // We only have one child, so we return the frame type of that child as if
      // we didn't exist.
      return child->GetMathMLFrameType();
    }
  }
  return nsMathMLFrame::GetMathMLFrameType();
}
