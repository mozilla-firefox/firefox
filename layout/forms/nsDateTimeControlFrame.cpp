/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This frame type is used for input type=date, time, month, week, and
 * datetime-local.
 */

#include "nsDateTimeControlFrame.h"

#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "nsLayoutUtils.h"
#include "nsTextControlFrame.h"

using namespace mozilla;
using namespace mozilla::dom;

nsIFrame* NS_NewDateTimeControlFrame(PresShell* aPresShell,
                                     ComputedStyle* aStyle) {
  return new nsDateTimeControlFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsDateTimeControlFrame)

NS_QUERYFRAME_HEAD(nsDateTimeControlFrame)
  NS_QUERYFRAME_ENTRY(nsDateTimeControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsDateTimeControlFrame::nsDateTimeControlFrame(ComputedStyle* aStyle,
                                               nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID) {}

nscoord nsDateTimeControlFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                               IntrinsicISizeType aType) {
  return mFrames.IsEmpty() ? 0
                           : nsLayoutUtils::IntrinsicForContainer(
                                 aInput.mContext, mFrames.FirstChild(), aType);
}

Maybe<nscoord> nsDateTimeControlFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  return nsTextControlFrame::GetSingleLineTextControlBaseline(
      this, mFirstBaseline, aWM, aBaselineGroup);
}

void nsDateTimeControlFrame::Reflow(nsPresContext* aPresContext,
                                    ReflowOutput& aDesiredSize,
                                    const ReflowInput& aReflowInput,
                                    nsReflowStatus& aStatus) {
  MarkInReflow();

  DO_GLOBAL_REFLOW_COUNT("nsDateTimeControlFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsDateTimeControlFrame::Reflow: availSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  NS_ASSERTION(mFrames.GetLength() <= 1,
               "There should be no more than 1 frames");

  const WritingMode myWM = aReflowInput.GetWritingMode();

  {
    auto baseline = nsTextControlFrame::ComputeBaseline(
        this, aReflowInput, /* aForSingleLineControl = */ true);
    mFirstBaseline = baseline.valueOr(NS_INTRINSIC_ISIZE_UNKNOWN);
    if (baseline) {
      aDesiredSize.SetBlockStartAscent(*baseline);
    }
  }

  // The ISize of our content box, which is the available ISize
  // for our anonymous content:
  const nscoord contentBoxISize = aReflowInput.ComputedISize();
  nscoord contentBoxBSize = aReflowInput.ComputedBSize();

  // Figure out our border-box sizes as well (by adding borderPadding to
  // content-box sizes):
  const auto borderPadding = aReflowInput.ComputedLogicalBorderPadding(myWM);
  const nscoord borderBoxISize =
      contentBoxISize + borderPadding.IStartEnd(myWM);

  nscoord borderBoxBSize;
  if (contentBoxBSize != NS_UNCONSTRAINEDSIZE) {
    borderBoxBSize = contentBoxBSize + borderPadding.BStartEnd(myWM);
  }  // else, we'll figure out borderBoxBSize after we resolve contentBoxBSize.

  nsIFrame* inputAreaFrame = mFrames.FirstChild();
  if (!inputAreaFrame) {  // display:none?
    if (contentBoxBSize == NS_UNCONSTRAINEDSIZE) {
      contentBoxBSize = 0;
      borderBoxBSize = borderPadding.BStartEnd(myWM);
    }
  } else {
    ReflowOutput childDesiredSize(aReflowInput);

    WritingMode wm = inputAreaFrame->GetWritingMode();
    LogicalSize availSize = aReflowInput.ComputedSize(wm);
    availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;

    ReflowInput childReflowInput(aPresContext, aReflowInput, inputAreaFrame,
                                 availSize);

    // Convert input area margin into my own writing-mode (in case it differs):
    LogicalMargin childMargin = childReflowInput.ComputedLogicalMargin(myWM);

    // offsets of input area frame within this frame:
    LogicalPoint childOffset =
        borderPadding.StartOffset(myWM) + childMargin.StartOffset(myWM);

    nsReflowStatus childStatus;
    // We initially reflow the child with a dummy containerSize; positioning
    // will be fixed later.
    const nsSize dummyContainerSize;
    ReflowChild(inputAreaFrame, aPresContext, childDesiredSize,
                childReflowInput, myWM, childOffset, dummyContainerSize,
                ReflowChildFlags::Default, childStatus);
    MOZ_ASSERT(childStatus.IsFullyComplete(),
               "We gave our child unconstrained available block-size, "
               "so it should be complete");

    nscoord childMarginBoxBSize =
        childDesiredSize.BSize(myWM) + childMargin.BStartEnd(myWM);

    if (contentBoxBSize == NS_UNCONSTRAINEDSIZE) {
      // We are intrinsically sized -- we should shrinkwrap the input area's
      // block-size, or our line-height:
      contentBoxBSize =
          std::max(aReflowInput.GetLineHeight(), childMarginBoxBSize);

      // Make sure we obey min/max-bsize in the case when we're doing intrinsic
      // sizing (we get it for free when we have a non-intrinsic
      // aReflowInput.ComputedBSize()).  Note that we do this before
      // adjusting for borderpadding, since ComputedMaxBSize and
      // ComputedMinBSize are content heights.
      contentBoxBSize = aReflowInput.ApplyMinMaxBSize(contentBoxBSize);

      borderBoxBSize = contentBoxBSize + borderPadding.BStartEnd(myWM);
    }

    // Center child in block axis
    nscoord extraSpace = contentBoxBSize - childMarginBoxBSize;
    childOffset.B(myWM) += std::max(0, extraSpace / 2);

    // Needed in FinishReflowChild, for logical-to-physical conversion:
    nsSize borderBoxSize =
        LogicalSize(myWM, borderBoxISize, borderBoxBSize).GetPhysicalSize(myWM);

    // Place the child
    FinishReflowChild(inputAreaFrame, aPresContext, childDesiredSize,
                      &childReflowInput, myWM, childOffset, borderBoxSize,
                      ReflowChildFlags::Default);
  }

  LogicalSize logicalDesiredSize(myWM, borderBoxISize, borderBoxBSize);
  aDesiredSize.SetSize(myWM, logicalDesiredSize);
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  if (inputAreaFrame) {
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, inputAreaFrame);
  }

  FinishAndStoreOverflow(&aDesiredSize);

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                 ("exit nsDateTimeControlFrame::Reflow: size=%d,%d",
                  aDesiredSize.Width(), aDesiredSize.Height()));
}
