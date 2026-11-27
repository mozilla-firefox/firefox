/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: ruby-text-container" */

#include "nsRubyTextContainerFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/WritingModes.h"
#include "nsLayoutUtils.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"

using namespace mozilla;

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsRubyTextContainerFrame)
  NS_QUERYFRAME_ENTRY(nsRubyTextContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsRubyTextContainerFrame)

nsContainerFrame* NS_NewRubyTextContainerFrame(PresShell* aPresShell,
                                               ComputedStyle* aStyle) {
  return new nsRubyTextContainerFrame(aStyle, aPresShell->GetPresContext());
}

//----------------------------------------------------------------------

// nsRubyTextContainerFrame Method Implementations
// ===============================================

#ifdef DEBUG_FRAME_DUMP
nsresult nsRubyTextContainerFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"RubyTextContainer"_ns, aResult);
}
#endif

/* virtual */
void nsRubyTextContainerFrame::SetInitialChildList(ChildListID aListID,
                                                   nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  if (aListID == FrameChildListID::Principal) {
    UpdateSpanFlag();
  }
}

/* virtual */
void nsRubyTextContainerFrame::AppendFrames(ChildListID aListID,
                                            nsFrameList&& aFrameList) {
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
  UpdateSpanFlag();
}

/* virtual */
void nsRubyTextContainerFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
  UpdateSpanFlag();
}

/* virtual */
void nsRubyTextContainerFrame::RemoveFrame(DestroyContext& aContext,
                                           ChildListID aListID,
                                           nsIFrame* aOldFrame) {
  nsContainerFrame::RemoveFrame(aContext, aListID, aOldFrame);
  UpdateSpanFlag();
}

void nsRubyTextContainerFrame::UpdateSpanFlag() {
  bool isSpan = false;
  // The continuation checks are safe here because spans never break.
  if (!GetPrevContinuation() && !GetNextContinuation()) {
    nsIFrame* onlyChild = mFrames.OnlyChild();
    if (onlyChild && onlyChild->IsPseudoFrame(GetContent())) {
      // Per CSS Ruby spec, if the only child of an rtc frame is
      // a pseudo rt frame, it spans all bases in the segment.
      isSpan = true;
    }
  }

  if (isSpan) {
    AddStateBits(NS_RUBY_TEXT_CONTAINER_IS_SPAN);
  } else {
    RemoveStateBits(NS_RUBY_TEXT_CONTAINER_IS_SPAN);
  }
}

/* virtual */
void nsRubyTextContainerFrame::Reflow(nsPresContext* aPresContext,
                                      ReflowOutput& aDesiredSize,
                                      const ReflowInput& aReflowInput,
                                      nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsRubyTextContainerFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  // Although a ruby text container may have continuations, returning
  // complete reflow status is still safe, since its parent, ruby frame,
  // ignores the status, and continuations of the ruby base container
  // will take care of our continuations.
  WritingMode rtcWM = GetWritingMode();

  nscoord minBCoord = nscoord_MAX;
  nscoord maxBCoord = nscoord_MIN;
  // The container size is not yet known, so we use a dummy (0, 0) size.
  // The block-dir position will be corrected below after containerSize
  // is finalized.
  const nsSize dummyContainerSize;
  for (nsIFrame* child : mFrames) {
    MOZ_ASSERT(child->IsRubyTextFrame());
    LogicalRect rect = child->GetLogicalRect(rtcWM, dummyContainerSize);
    LogicalMargin margin = child->GetLogicalUsedMargin(rtcWM);
    nscoord blockStart = rect.BStart(rtcWM) - margin.BStart(rtcWM);
    minBCoord = std::min(minBCoord, blockStart);
    nscoord blockEnd = rect.BEnd(rtcWM) + margin.BEnd(rtcWM);
    maxBCoord = std::max(maxBCoord, blockEnd);
  }

  if (!mFrames.IsEmpty()) {
    if (MOZ_UNLIKELY(minBCoord > maxBCoord)) {
      // XXX When bug 765861 gets fixed, this warning should be upgraded.
      NS_WARNING("bad block coord");
      minBCoord = maxBCoord = 0;
    }
    LogicalSize size(rtcWM, mISize, maxBCoord - minBCoord);
    nsSize containerSize = size.GetPhysicalSize(rtcWM);
    for (nsIFrame* child : mFrames) {
      // We reflowed the child with a dummy container size, as the true size
      // was not yet known at that time.
      LogicalPoint pos = child->GetLogicalPosition(rtcWM, dummyContainerSize);
      // Adjust block position to account for minBCoord,
      // then reposition child based on the true container width.
      pos.B(rtcWM) -= minBCoord;
      // Relative positioning hasn't happened yet.
      // So MovePositionBy should not be used here.
      child->SetPosition(rtcWM, pos, containerSize);
    }
    aDesiredSize.SetSize(rtcWM, size);
  } else {
    // If this ruby text container is empty, size it as if there were
    // an empty inline child inside.
    // Border and padding are suppressed on ruby text container, so we
    // create a dummy zero-sized borderPadding for setting BSize.
    aDesiredSize.ISize(rtcWM) = mISize;
    LogicalMargin borderPadding(rtcWM);
    nsLayoutUtils::SetBSizeFromFontMetrics(this, aDesiredSize, borderPadding,
                                           rtcWM, rtcWM);
  }
}

RubyMetrics nsRubyTextContainerFrame::RubyMetrics(
    float aRubyMetricsFactor) const {
  mozilla::RubyMetrics result;
  WritingMode containerWM = GetWritingMode();
  bool foundAnyFrames = false;
  for (const auto* f : mFrames) {
    WritingMode wm = f->GetWritingMode();
    if (wm.IsOrthogonalTo(containerWM) || f->IsPlaceholderFrame()) {
      continue;
    }
    mozilla::RubyMetrics m = f->RubyMetrics(aRubyMetricsFactor);
    const LogicalMargin borderPadding = f->GetLogicalUsedBorderAndPadding(wm);
    m.mAscent += borderPadding.BStart(wm);
    m.mDescent += borderPadding.BEnd(wm);
    const LogicalMargin margin = f->GetLogicalUsedMargin(wm);
    m.mAscent += margin.BStart(wm);
    m.mDescent += margin.BEnd(wm);
    result.CombineWith(m);
    foundAnyFrames = true;
  }
  if (!foundAnyFrames) {
    result = nsIFrame::RubyMetrics(aRubyMetricsFactor);
  }
  return result;
}
