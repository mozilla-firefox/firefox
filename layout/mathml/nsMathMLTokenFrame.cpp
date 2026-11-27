/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLTokenFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "nsContentUtils.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsTextFrame.h"

using namespace mozilla;

nsIFrame* NS_NewMathMLTokenFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsMathMLTokenFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLTokenFrame)

nsMathMLTokenFrame::~nsMathMLTokenFrame() = default;

NS_IMETHODIMP
nsMathMLTokenFrame::InheritAutomaticData(nsIFrame* aParent) {
  // let the base class get the default from our parent
  nsMathMLContainerFrame::InheritAutomaticData(aParent);

  return NS_OK;
}

MathMLFrameType nsMathMLTokenFrame::GetMathMLFrameType() {
  // treat everything other than <mi> as ordinary...
  if (!mContent->IsMathMLElement(nsGkAtoms::mi)) {
    return MathMLFrameType::Ordinary;
  }

  StyleMathVariant mathVariant = StyleFont()->mMathVariant;
  if ((mathVariant == StyleMathVariant::None &&
       (StyleFont()->mFont.style.IsItalic() ||
        HasAnyStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI))) ||
      mathVariant == StyleMathVariant::Italic ||
      mathVariant == StyleMathVariant::BoldItalic ||
      mathVariant == StyleMathVariant::SansSerifItalic ||
      mathVariant == StyleMathVariant::SansSerifBoldItalic) {
    return MathMLFrameType::ItalicIdentifier;
  }
  return MathMLFrameType::UprightIdentifier;
}

void nsMathMLTokenFrame::MarkTextFramesAsTokenMathML() {
  nsIFrame* child = nullptr;
  uint32_t childCount = 0;

  // Set flags on child text frames
  // - to force them to trim their leading and trailing whitespaces.
  // - Indicate which frames are suitable for mathvariant
  // - flag single character <mi> frames for special italic treatment
  for (nsIFrame* childFrame = PrincipalChildList().FirstChild(); childFrame;
       childFrame = childFrame->GetNextSibling()) {
    for (nsIFrame* childFrame2 = childFrame->PrincipalChildList().FirstChild();
         childFrame2; childFrame2 = childFrame2->GetNextSibling()) {
      if (childFrame2->IsTextFrame()) {
        childFrame2->AddStateBits(TEXT_IS_IN_TOKEN_MATHML);
        child = childFrame2;
        childCount++;
      }
    }
  }
  if (mContent->IsMathMLElement(nsGkAtoms::mi) && childCount == 1) {
    nsAutoString data;
    nsContentUtils::GetNodeTextContent(mContent, false, data);

    data.CompressWhitespace();
    int32_t length = data.Length();

    bool isSingleCharacter =
        length == 1 || (length == 2 && NS_IS_HIGH_SURROGATE(data[0]));

    if (isSingleCharacter) {
      child->AddStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI);
      AddStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI);
    }
  }
}

void nsMathMLTokenFrame::SetInitialChildList(ChildListID aListID,
                                             nsFrameList&& aChildList) {
  // First, let the base class do its work
  nsMathMLContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  MarkTextFramesAsTokenMathML();
}

void nsMathMLTokenFrame::AppendFrames(ChildListID aListID,
                                      nsFrameList&& aChildList) {
  nsMathMLContainerFrame::AppendFrames(aListID, std::move(aChildList));
  MarkTextFramesAsTokenMathML();
}

void nsMathMLTokenFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aChildList) {
  nsMathMLContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                       std::move(aChildList));
  MarkTextFramesAsTokenMathML();
}

void nsMathMLTokenFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  MarkInReflow();
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // initializations needed for empty markup like <mtag></mtag>
  aDesiredSize.ClearSize();
  aDesiredSize.SetBlockStartAscent(0);
  aDesiredSize.mBoundingMetrics = nsBoundingMetrics();

  for (nsIFrame* childFrame : PrincipalChildList()) {
    // ask our children to compute their bounding metrics
    ReflowOutput childDesiredSize(aReflowInput.GetWritingMode());
    WritingMode wm = childFrame->GetWritingMode();
    LogicalSize availSize = aReflowInput.ComputedSize(wm);
    availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
    ReflowInput childReflowInput(aPresContext, aReflowInput, childFrame,
                                 availSize);
    nsReflowStatus childStatus;
    ReflowChild(childFrame, aPresContext, childDesiredSize, childReflowInput,
                childStatus);
    NS_ASSERTION(childStatus.IsComplete(),
                 "We gave the child unconstrained available block-size, so its "
                 "status should be complete!");
    SaveReflowAndBoundingMetricsFor(childFrame, childDesiredSize,
                                    childDesiredSize.mBoundingMetrics);
  }

  // place and size children
  FinalizeReflow(aReflowInput.mRenderingContext->GetDrawTarget(), aDesiredSize);

  aStatus.Reset();  // This type of frame can't be split.
}

// For token elements, mBoundingMetrics is computed at the ReflowToken
// pass, it is not computed here because our children may be text frames
// that do not implement the GetBoundingMetrics() interface.
/* virtual */
void nsMathMLTokenFrame::Place(DrawTarget* aDrawTarget,
                               const PlaceFlags& aFlags,
                               ReflowOutput& aDesiredSize) {
  mBoundingMetrics = nsBoundingMetrics();
  for (nsIFrame* childFrame : PrincipalChildList()) {
    ReflowOutput childSize(aDesiredSize.GetWritingMode());
    nsBoundingMetrics bmChild;
    GetReflowAndBoundingMetricsFor(childFrame, childSize, bmChild, nullptr);
    auto childMargin = GetMarginForPlace(aFlags, childFrame);
    bmChild.ascent += childMargin.top;
    bmChild.descent += childMargin.bottom;
    bmChild.rightBearing += childMargin.LeftRight();
    bmChild.width += childMargin.LeftRight();

    // compute and cache the bounding metrics
    mBoundingMetrics += bmChild;
  }

  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(this);
  nscoord ascent = fm->MaxAscent();
  nscoord descent = fm->MaxDescent();

  aDesiredSize.mBoundingMetrics = mBoundingMetrics;
  aDesiredSize.Width() = mBoundingMetrics.width;
  aDesiredSize.SetBlockStartAscent(std::max(mBoundingMetrics.ascent, ascent));
  aDesiredSize.Height() = aDesiredSize.BlockStartAscent() +
                          std::max(mBoundingMetrics.descent, descent);

  // Apply width/height to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  auto shiftX = ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                                 mBoundingMetrics);

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);

  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    nscoord dx = borderPadding.left;
    dx += shiftX;
    for (nsIFrame* childFrame : PrincipalChildList()) {
      ReflowOutput childSize(aDesiredSize.GetWritingMode());
      GetReflowAndBoundingMetricsFor(childFrame, childSize,
                                     childSize.mBoundingMetrics);
      auto childMargin = GetMarginForPlace(aFlags, childFrame);

      // place and size the child; (dx,0) makes the caret happy - bug 188146
      nscoord dy = childSize.Height() == 0
                       ? 0
                       : aDesiredSize.BlockStartAscent() -
                             childSize.BlockStartAscent() + childMargin.top;
      FinishReflowChild(childFrame, PresContext(), childSize, nullptr, dx, dy,
                        ReflowChildFlags::Default);
      dx += childSize.Width();
    }
  }

  SetReference(nsPoint(0, aDesiredSize.BlockStartAscent()));
}
