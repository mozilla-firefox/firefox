/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCheckboxRadioFrame.h"

#include "LayoutConstants.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "nsIContent.h"
#include "nsLayoutUtils.h"

using namespace mozilla;

nsCheckboxRadioFrame* NS_NewCheckboxRadioFrame(PresShell* aPresShell,
                                               ComputedStyle* aStyle) {
  return new nsCheckboxRadioFrame(aStyle, aPresShell->GetPresContext());
}

nsCheckboxRadioFrame::nsCheckboxRadioFrame(ComputedStyle* aStyle,
                                           nsPresContext* aPresContext)
    : nsAtomicContainerFrame(aStyle, aPresContext, kClassID) {}

nsCheckboxRadioFrame::~nsCheckboxRadioFrame() = default;

NS_IMPL_FRAMEARENA_HELPERS(nsCheckboxRadioFrame)

NS_QUERYFRAME_HEAD(nsCheckboxRadioFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsAtomicContainerFrame)

nscoord nsCheckboxRadioFrame::DefaultSize() {
  const CSSCoord size = StyleDisplay()->HasNativeAppearance()
                            ? PresContext()->Theme()->GetCheckboxRadioPrefSize()
                            : CSSCoord(13.0f);
  return CSSPixel::ToAppUnits(Style()->EffectiveZoom().Zoom(size));
}

/* virtual */
void nsCheckboxRadioFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                            const nsDisplayListSet& aLists) {
  DO_GLOBAL_REFLOW_COUNT_DSP("nsCheckboxRadioFrame");
  DisplayBorderBackgroundOutline(aBuilder, aLists);
}

nscoord nsCheckboxRadioFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                             IntrinsicISizeType aType) {
  return StyleDisplay()->HasNativeAppearance() ? DefaultSize() : 0;
}

/* virtual */
LogicalSize nsCheckboxRadioFrame::ComputeAutoSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  LogicalSize size(aWM, 0, 0);
  if (!StyleDisplay()->HasNativeAppearance()) {
    return size;
  }
  return nsAtomicContainerFrame::ComputeAutoSize(
      aSizingInput, aWM, aCBSize, aAvailableISize, aMargin, aBorderPadding,
      aSizeOverrides, aFlags);
}

Maybe<nscoord> nsCheckboxRadioFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  NS_ASSERTION(!IsSubtreeDirty(), "frame must not be dirty");

  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return Nothing{};
  }

  if (StyleDisplay()->IsBlockOutsideStyle()) {
    return Nothing{};
  }

  // For appearance:none we use a standard CSS baseline, i.e. synthesized from
  // our margin-box.
  if (!StyleDisplay()->HasNativeAppearance()) {
    return Nothing{};
  }

  if (aWM.IsCentralBaseline()) {
    return Some(BSize(aWM) / 2);
  }

  // This is for compatibility with Chrome, Safari and Edge (Dec 2016).
  // Treat radio buttons and checkboxes as having an intrinsic baseline
  // at the block-end of the control (use the block-end content edge rather
  // than the margin edge).
  // For "inverted" lines (typically in writing-mode:vertical-lr), use the
  // block-start end instead.
  // See kCheckboxRadioBorderWidth in Theme.cpp
  CSSCoord border = PresContext()->Theme()->GetCheckboxRadioBorderWidth();
  auto bp = CSSPixel::ToAppUnits(Style()->EffectiveZoom().Zoom(border));
  return Some(aWM.IsLineInverted() ? std::min(bp, BSize(aWM))
                                   : std::max(0, BSize(aWM) - bp));
}

void nsCheckboxRadioFrame::Reflow(nsPresContext* aPresContext,
                                  ReflowOutput& aDesiredSize,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsCheckboxRadioFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsCheckboxRadioFrame::Reflow: aMaxSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  const auto wm = aReflowInput.GetWritingMode();
  MOZ_ASSERT(aReflowInput.ComputedLogicalBorderPadding(wm).IsAllZero());

  const auto contentBoxSize =
      aReflowInput.ComputedSizeWithBSizeFallback([&] { return DefaultSize(); });
  aDesiredSize.SetSize(wm, contentBoxSize);
  if (nsLayoutUtils::FontSizeInflationEnabled(aPresContext)) {
    const float inflation = nsLayoutUtils::FontSizeInflationFor(this);
    aDesiredSize.Width() *= inflation;
    aDesiredSize.Height() *= inflation;
  }

  {
    const nsSize containerSize = aDesiredSize.PhysicalSize();
    for (auto* kid : mFrames) {
      MOZ_ASSERT(kid->IsPlaceholderFrame(),
                 "We only really expect placeholders here");
      const auto kidWm = kid->GetWritingMode();
      auto availSize = aDesiredSize.Size(kidWm);
      availSize.BSize(kidWm) = NS_UNCONSTRAINEDSIZE;
      ReflowInput kidReflowInput(aPresContext, aReflowInput, kid, availSize);
      ReflowOutput kidDesiredSize(kidReflowInput);
      nsReflowStatus status;
      const LogicalPoint position(kidWm);
      ReflowChild(kid, aPresContext, kidDesiredSize, kidReflowInput, kidWm,
                  position, containerSize, ReflowChildFlags::Default, status);
      FinishReflowChild(kid, aPresContext, kidDesiredSize, &kidReflowInput,
                        kidWm, position, containerSize,
                        ReflowChildFlags::Default);
    }
  }

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                 ("exit nsCheckboxRadioFrame::Reflow: size=%d,%d",
                  aDesiredSize.Width(), aDesiredSize.Height()));

  aDesiredSize.SetOverflowAreasToDesiredBounds();
  FinishAndStoreOverflow(&aDesiredSize);
}

nsresult nsCheckboxRadioFrame::HandleEvent(nsPresContext* aPresContext,
                                           WidgetGUIEvent* aEvent,
                                           nsEventStatus* aEventStatus) {
  // Check for disabled content so that selection works properly (?).
  if (IsContentDisabled()) {
    return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  }
  return NS_OK;
}
