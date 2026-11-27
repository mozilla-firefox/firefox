/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: ruby-base-container" */

#include "nsRubyBaseContainerFrame.h"

#include "RubyUtils.h"
#include "gfxContext.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/WritingModes.h"
#include "nsLayoutUtils.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"
#include "nsRubyBaseFrame.h"
#include "nsRubyTextContainerFrame.h"
#include "nsRubyTextFrame.h"
#include "nsStyleStructInlines.h"
#include "nsTextFrame.h"

using namespace mozilla;
using namespace mozilla::gfx;

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsRubyBaseContainerFrame)
  NS_QUERYFRAME_ENTRY(nsRubyBaseContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsRubyBaseContainerFrame)

nsContainerFrame* NS_NewRubyBaseContainerFrame(PresShell* aPresShell,
                                               ComputedStyle* aStyle) {
  return new nsRubyBaseContainerFrame(aStyle, aPresShell->GetPresContext());
}

//----------------------------------------------------------------------

// nsRubyBaseContainerFrame Method Implementations
// ===============================================

#ifdef DEBUG_FRAME_DUMP
nsresult nsRubyBaseContainerFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"RubyBaseContainer"_ns, aResult);
}
#endif

static gfxBreakPriority LineBreakBefore(nsIFrame* aFrame,
                                        DrawTarget* aDrawTarget,
                                        nsIFrame* aLineContainerFrame,
                                        const nsLineList::iterator* aLine) {
  for (nsIFrame* child = aFrame; child;
       child = child->PrincipalChildList().FirstChild()) {
    if (!child->CanContinueTextRun()) {
      // It is not an inline element. We can break before it.
      return gfxBreakPriority::eNormalBreak;
    }
    if (!child->IsTextFrame()) {
      continue;
    }

    auto textFrame = static_cast<nsTextFrame*>(child);
    gfxSkipCharsIterator iter = textFrame->EnsureTextRun(
        nsTextFrame::eInflated, aDrawTarget, aLineContainerFrame, aLine);
    iter.SetOriginalOffset(textFrame->GetContentOffset());
    uint32_t pos = iter.GetSkippedOffset();
    gfxTextRun* textRun = textFrame->GetTextRun(nsTextFrame::eInflated);
    MOZ_ASSERT(textRun, "fail to build textrun?");
    if (!textRun || pos >= textRun->GetLength()) {
      // The text frame contains no character at all.
      return gfxBreakPriority::eNoBreak;
    }
    // Return whether we can break before the first character.
    if (textRun->CanBreakLineBefore(pos)) {
      return gfxBreakPriority::eNormalBreak;
    }
    // Check whether we can wrap word here.
    const nsStyleText* textStyle = textFrame->StyleText();
    if (textStyle->WordCanWrap(textFrame) && textRun->IsClusterStart(pos)) {
      return gfxBreakPriority::eWordWrapBreak;
    }
    // We cannot break before.
    return gfxBreakPriority::eNoBreak;
  }
  // Neither block, nor text frame is found as a leaf. We won't break
  // before this base frame. It is the behavior of empty spans.
  return gfxBreakPriority::eNoBreak;
}

static void GetIsLineBreakAllowed(nsIFrame* aFrame, bool aIsLineBreakable,
                                  bool* aAllowInitialLineBreak,
                                  bool* aAllowLineBreak) {
  nsIFrame* parent = aFrame->GetParent();
  bool lineBreakSuppressed = parent->Style()->ShouldSuppressLineBreak();
  // Allow line break between ruby bases when white-space allows,
  // we are not inside a nested ruby, and there is no span.
  bool allowLineBreak =
      !lineBreakSuppressed && aFrame->StyleText()->WhiteSpaceCanWrap(aFrame);
  bool allowInitialLineBreak = allowLineBreak;
  if (!aFrame->GetPrevInFlow()) {
    allowInitialLineBreak =
        !lineBreakSuppressed && parent->StyleText()->WhiteSpaceCanWrap(parent);
  }
  if (!aIsLineBreakable) {
    allowInitialLineBreak = false;
  }
  *aAllowInitialLineBreak = allowInitialLineBreak;
  *aAllowLineBreak = allowLineBreak;
}

/**
 * @param aBaseISizeData is an in/out param. This method updates the
 * `skipWhitespace` and `trailingWhitespace` fields of the struct with
 * the base level frame. Note that we don't need to do the same thing
 * for ruby text frames, because they are text run container themselves
 * (see nsTextFrame.cpp:BuildTextRuns), and thus no whitespace collapse
 * happens across the boundary of those frames.
 */
static nscoord CalculateColumnPrefISize(
    const IntrinsicSizeInput& aInput, const RubyColumnEnumerator& aEnumerator,
    nsIFrame::InlineIntrinsicISizeData* aBaseISizeData) {
  nscoord max = 0;
  uint32_t levelCount = aEnumerator.GetLevelCount();
  for (uint32_t i = 0; i < levelCount; i++) {
    nsIFrame* frame = aEnumerator.GetFrameAtLevel(i);
    if (frame) {
      nsIFrame::InlinePrefISizeData data;
      if (i == 0) {
        data.SetLineContainer(aBaseISizeData->LineContainer());
        data.mSkipWhitespace = aBaseISizeData->mSkipWhitespace;
        data.mTrailingWhitespace = aBaseISizeData->mTrailingWhitespace;
      } else {
        // The line container of ruby text frames is their parent,
        // ruby text container frame.
        data.SetLineContainer(frame->GetParent());
      }
      frame->AddInlinePrefISize(aInput, &data);
      MOZ_ASSERT(data.mPrevLines == 0, "Shouldn't have prev lines");
      max = std::max(max, data.mCurrentLine);
      if (i == 0) {
        aBaseISizeData->mSkipWhitespace = data.mSkipWhitespace;
        aBaseISizeData->mTrailingWhitespace = data.mTrailingWhitespace;
      }
    }
  }
  return max;
}

// FIXME Currently we use pref isize of ruby content frames for
//       computing min isize of ruby frame, which may cause problem.
//       See bug 1134945.
/* virtual */
void nsRubyBaseContainerFrame::AddInlineMinISize(
    const IntrinsicSizeInput& aInput, InlineMinISizeData* aData) {
  AutoRubyTextContainerArray textContainers(this);

  for (uint32_t i = 0, iend = textContainers.Length(); i < iend; i++) {
    if (textContainers[i]->IsSpanContainer()) {
      // Since spans are not breakable internally, use our pref isize
      // directly if there is any span.
      InlinePrefISizeData data;
      data.SetLineContainer(aData->LineContainer());
      data.mSkipWhitespace = aData->mSkipWhitespace;
      data.mTrailingWhitespace = aData->mTrailingWhitespace;
      AddInlinePrefISize(aInput, &data);
      aData->mCurrentLine += data.mCurrentLine;
      if (data.mCurrentLine > 0) {
        aData->mAtStartOfLine = false;
      }
      aData->mSkipWhitespace = data.mSkipWhitespace;
      aData->mTrailingWhitespace = data.mTrailingWhitespace;
      return;
    }
  }

  bool firstFrame = true;
  bool allowInitialLineBreak, allowLineBreak;
  GetIsLineBreakAllowed(this, !aData->mAtStartOfLine, &allowInitialLineBreak,
                        &allowLineBreak);
  for (nsIFrame* frame = this; frame; frame = frame->GetNextInFlow()) {
    RubyColumnEnumerator enumerator(
        static_cast<nsRubyBaseContainerFrame*>(frame), textContainers);
    for (; !enumerator.AtEnd(); enumerator.Next()) {
      if (firstFrame ? allowInitialLineBreak : allowLineBreak) {
        nsIFrame* baseFrame = enumerator.GetFrameAtLevel(0);
        if (baseFrame) {
          gfxBreakPriority breakPriority = LineBreakBefore(
              baseFrame, aInput.mContext->GetDrawTarget(), nullptr, nullptr);
          if (breakPriority != gfxBreakPriority::eNoBreak) {
            aData->OptionallyBreak();
          }
        }
      }
      firstFrame = false;
      nscoord isize = CalculateColumnPrefISize(aInput, enumerator, aData);
      aData->mCurrentLine += isize;
      if (isize > 0) {
        aData->mAtStartOfLine = false;
      }
    }
  }
}

/* virtual */
void nsRubyBaseContainerFrame::AddInlinePrefISize(
    const IntrinsicSizeInput& aInput, InlinePrefISizeData* aData) {
  AutoRubyTextContainerArray textContainers(this);
  const IntrinsicSizeInput input(aInput.mContext, Nothing(), Nothing());

  nscoord sum = 0;
  for (nsIFrame* frame = this; frame; frame = frame->GetNextInFlow()) {
    RubyColumnEnumerator enumerator(
        static_cast<nsRubyBaseContainerFrame*>(frame), textContainers);
    for (; !enumerator.AtEnd(); enumerator.Next()) {
      sum += CalculateColumnPrefISize(input, enumerator, aData);
    }
  }
  for (uint32_t i = 0, iend = textContainers.Length(); i < iend; i++) {
    if (textContainers[i]->IsSpanContainer()) {
      nsIFrame* frame = textContainers[i]->PrincipalChildList().FirstChild();
      InlinePrefISizeData data;
      frame->AddInlinePrefISize(input, &data);
      MOZ_ASSERT(data.mPrevLines == 0, "Shouldn't have prev lines");
      sum = std::max(sum, data.mCurrentLine);
    }
  }
  aData->mCurrentLine += sum;
}

/* virtual */
bool nsRubyBaseContainerFrame::CanContinueTextRun() const { return true; }

/* virtual */
nsIFrame::SizeComputationResult nsRubyBaseContainerFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  // Ruby base container frame is inline,
  // hence don't compute size before reflow.
  return {LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
          AspectRatioUsage::None};
}

Maybe<nscoord> nsRubyBaseContainerFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return Nothing{};
  }
  return Some(mBaseline);
}

struct nsRubyBaseContainerFrame::RubyReflowInput {
  bool mAllowInitialLineBreak;
  bool mAllowLineBreak;
  const AutoRubyTextContainerArray& mTextContainers;
  const ReflowInput& mBaseReflowInput;
  const nsTArray<UniquePtr<ReflowInput>>& mTextReflowInputs;
};

/* virtual */
void nsRubyBaseContainerFrame::Reflow(nsPresContext* aPresContext,
                                      ReflowOutput& aDesiredSize,
                                      const ReflowInput& aReflowInput,
                                      nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsRubyBaseContainerFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  if (!aReflowInput.mLineLayout) {
    NS_ASSERTION(
        aReflowInput.mLineLayout,
        "No line layout provided to RubyBaseContainerFrame reflow method.");
    return;
  }

  mDescendantLeadings.Reset();

  nsIFrame* lineContainer = aReflowInput.mLineLayout->LineContainerFrame();
  MoveInlineOverflowToChildList(lineContainer);
  // Ask text containers to drain overflows
  AutoRubyTextContainerArray textContainers(this);
  const uint32_t rtcCount = textContainers.Length();
  for (uint32_t i = 0; i < rtcCount; i++) {
    textContainers[i]->MoveInlineOverflowToChildList(lineContainer);
  }

  WritingMode lineWM = aReflowInput.mLineLayout->GetWritingMode();
  LogicalSize availSize(lineWM, aReflowInput.AvailableISize(),
                        aReflowInput.AvailableBSize());

  // We have a reflow input and a line layout for each RTC.
  // They are conceptually the state of the RTCs, but we don't actually
  // reflow those RTCs in this code. These two arrays are holders of
  // the reflow inputs and line layouts.
  // Since there are pointers refer to reflow inputs and line layouts,
  // it is necessary to guarantee that they won't be moved. For this
  // reason, they are wrapped in UniquePtr here.
  AutoTArray<UniquePtr<ReflowInput>, RTC_ARRAY_SIZE> reflowInputs;
  AutoTArray<UniquePtr<nsLineLayout>, RTC_ARRAY_SIZE> lineLayouts;
  reflowInputs.SetCapacity(rtcCount);
  lineLayouts.SetCapacity(rtcCount);

  // Begin the line layout for each ruby text container in advance.
  bool hasSpan = false;
  for (uint32_t i = 0; i < rtcCount; i++) {
    nsRubyTextContainerFrame* textContainer = textContainers[i];
    WritingMode rtcWM = textContainer->GetWritingMode();
    WritingMode reflowWM = lineWM.IsOrthogonalTo(rtcWM) ? rtcWM : lineWM;
    if (textContainer->IsSpanContainer()) {
      hasSpan = true;
    }

    ReflowInput* reflowInput = new ReflowInput(
        aPresContext, *aReflowInput.mParentReflowInput, textContainer,
        availSize.ConvertTo(textContainer->GetWritingMode(), lineWM));
    reflowInputs.AppendElement(reflowInput);
    nsLineLayout* lineLayout =
        new nsLineLayout(aPresContext, reflowInput->mFloatManager, *reflowInput,
                         nullptr, aReflowInput.mLineLayout);
    lineLayout->SetSuppressLineWrap(true);
    lineLayouts.AppendElement(lineLayout);

    // Line number is useless for ruby text
    // XXX nullptr here may cause problem, see comments for
    //     nsLineLayout::mBlockRI and nsLineLayout::AddFloat
    lineLayout->Init(nullptr, reflowInput->GetLineHeight(), -1);
    reflowInput->mLineLayout = lineLayout;

    // Border and padding are suppressed on ruby text containers.
    // If the writing mode is vertical-rl, the horizontal position of
    // rt frames will be updated when reflowing this text container,
    // hence leave container size 0 here for now.
    lineLayout->BeginLineReflow(0, 0, reflowInput->ComputedISize(),
                                NS_UNCONSTRAINEDSIZE, false, false, reflowWM,
                                nsSize(0, 0));
    lineLayout->AttachRootFrameToBaseLineLayout();
  }

  aReflowInput.mLineLayout->BeginSpan(
      this, &aReflowInput, 0, aReflowInput.AvailableISize(), &mBaseline);

  bool allowInitialLineBreak, allowLineBreak;
  GetIsLineBreakAllowed(this, aReflowInput.mLineLayout->LineIsBreakable(),
                        &allowInitialLineBreak, &allowLineBreak);

  // Reflow columns excluding any span
  RubyReflowInput reflowInput = {allowInitialLineBreak,
                                 allowLineBreak && !hasSpan, textContainers,
                                 aReflowInput, reflowInputs};
  aDesiredSize.BSize(lineWM) = 0;
  aDesiredSize.SetBlockStartAscent(0);
  nscoord isize = ReflowColumns(reflowInput, aDesiredSize, aStatus);
  DebugOnly<nscoord> lineSpanSize = aReflowInput.mLineLayout->EndSpan(this);
  aDesiredSize.ISize(lineWM) = isize;
  // When there are no frames inside the ruby base container, EndSpan
  // will return 0. However, in this case, the actual width of the
  // container could be non-zero because of non-empty ruby annotations.
  // XXX When bug 765861 gets fixed, this warning should be upgraded.
  NS_WARNING_ASSERTION(
      aStatus.IsInlineBreak() || isize == lineSpanSize || mFrames.IsEmpty(),
      "bad isize");

  // If there exists any span, the columns must either be completely
  // reflowed, or be not reflowed at all.
  MOZ_ASSERT(aStatus.IsInlineBreakBefore() || aStatus.IsComplete() || !hasSpan);
  if (!aStatus.IsInlineBreakBefore() && aStatus.IsComplete() && hasSpan) {
    // Reflow spans
    RubyReflowInput reflowInput = {false, false, textContainers, aReflowInput,
                                   reflowInputs};
    nscoord spanISize = ReflowSpans(reflowInput);
    isize = std::max(isize, spanISize);
  }

  for (uint32_t i = 0; i < rtcCount; i++) {
    // It happens before the ruby text container is reflowed, and that
    // when it is reflowed, it will just use this size.
    nsRubyTextContainerFrame* textContainer = textContainers[i];
    nsLineLayout* lineLayout = lineLayouts[i].get();

    RubyUtils::ClearReservedISize(textContainer);
    nscoord rtcISize = lineLayout->GetCurrentICoord();
    // Only span containers and containers with collapsed annotations
    // need reserving isize. For normal ruby text containers, their
    // children will be expanded properly. We only need to expand their
    // own size.
    if (!textContainer->IsSpanContainer()) {
      rtcISize = isize;
    } else if (isize > rtcISize) {
      RubyUtils::SetReservedISize(textContainer, isize - rtcISize);
    }

    lineLayout->VerticalAlignLine();
    textContainer->SetISize(rtcISize);
    lineLayout->EndLineReflow();
  }

  // If this ruby base container is empty, size it as if there were
  // an empty inline child inside.
  if (mFrames.IsEmpty()) {
    // Border and padding are suppressed on ruby base container, so we
    // create a dummy zero-sized borderPadding for setting BSize.
    WritingMode frameWM = aReflowInput.GetWritingMode();
    LogicalMargin borderPadding(frameWM);
    nsLayoutUtils::SetBSizeFromFontMetrics(this, aDesiredSize, borderPadding,
                                           lineWM, frameWM);
  }

  ReflowAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput, aStatus);
}

/**
 * This struct stores the continuations after this frame and
 * corresponding text containers. It is used to speed up looking
 * ahead for nonempty continuations.
 */
struct MOZ_STACK_CLASS nsRubyBaseContainerFrame::PullFrameState {
  ContinuationTraversingState mBase;
  AutoTArray<ContinuationTraversingState, RTC_ARRAY_SIZE> mTexts;
  const AutoRubyTextContainerArray& mTextContainers;

  PullFrameState(nsRubyBaseContainerFrame* aBaseContainer,
                 const AutoRubyTextContainerArray& aTextContainers);
};

nscoord nsRubyBaseContainerFrame::ReflowColumns(
    const RubyReflowInput& aReflowInput, ReflowOutput& aDesiredSize,
    nsReflowStatus& aStatus) {
  nsLineLayout* lineLayout = aReflowInput.mBaseReflowInput.mLineLayout;
  const uint32_t rtcCount = aReflowInput.mTextContainers.Length();
  nscoord icoord = lineLayout->GetCurrentICoord();
  MOZ_ASSERT(icoord == 0, "border/padding of rbc should have been suppressed");
  nsReflowStatus reflowStatus;
  aStatus.Reset();

  uint32_t columnIndex = 0;
  RubyColumn column;
  column.mTextFrames.SetCapacity(rtcCount);
  RubyColumnEnumerator e(this, aReflowInput.mTextContainers);
  for (; !e.AtEnd(); e.Next()) {
    e.GetColumn(column);
    icoord += ReflowOneColumn(aReflowInput, columnIndex, column, aDesiredSize,
                              reflowStatus);
    if (!reflowStatus.IsInlineBreakBefore()) {
      columnIndex++;
    }
    if (reflowStatus.IsInlineBreak()) {
      break;
    }
    // We are not handling overflow here.
    MOZ_ASSERT(reflowStatus.IsEmpty());
  }

  bool isComplete = false;
  PullFrameState pullFrameState(this, aReflowInput.mTextContainers);
  while (!reflowStatus.IsInlineBreak()) {
    // We are not handling overflow here.
    MOZ_ASSERT(reflowStatus.IsEmpty());

    // Try pull some frames from next continuations. This call replaces
    // frames in |column| with the frame pulled in each level.
    PullOneColumn(lineLayout, pullFrameState, column, isComplete);
    if (isComplete) {
      // No more frames can be pulled.
      break;
    }
    icoord += ReflowOneColumn(aReflowInput, columnIndex, column, aDesiredSize,
                              reflowStatus);
    if (!reflowStatus.IsInlineBreakBefore()) {
      columnIndex++;
    }
  }

  if (!e.AtEnd() && reflowStatus.IsInlineBreakAfter()) {
    // The current column has been successfully placed.
    // Skip to the next column and mark break before.
    e.Next();
    e.GetColumn(column);
    reflowStatus.SetInlineLineBreakBeforeAndReset();
  }
  if (!e.AtEnd() || (GetNextInFlow() && !isComplete)) {
    aStatus.SetIncomplete();
  }

  if (reflowStatus.IsInlineBreakBefore()) {
    if (!columnIndex || !aReflowInput.mAllowLineBreak) {
      // If no column has been placed yet, or we have any span,
      // the whole container should be in the next line.
      aStatus.SetInlineLineBreakBeforeAndReset();
      return 0;
    }
    aStatus.SetInlineLineBreakAfter();
    MOZ_ASSERT(aStatus.IsComplete() || aReflowInput.mAllowLineBreak);

    // If we are on an intra-level whitespace column, null values in
    // column.mBaseFrame and column.mTextFrames don't represent the
    // end of the frame-sibling-chain at that level -- instead, they
    // represent an anonymous empty intra-level whitespace box. It is
    // likely that there are frames in the next column (which can't be
    // intra-level whitespace). Those frames should be pushed as well.
    Maybe<RubyColumn> nextColumn;
    if (column.mIsIntraLevelWhitespace && !e.AtEnd()) {
      e.Next();
      nextColumn.emplace();
      e.GetColumn(nextColumn.ref());
    }
    nsIFrame* baseFrame = column.mBaseFrame;
    if (!baseFrame & nextColumn.isSome()) {
      baseFrame = nextColumn->mBaseFrame;
    }
    if (baseFrame) {
      PushChildrenToOverflow(baseFrame, baseFrame->GetPrevSibling());
    }
    for (uint32_t i = 0; i < rtcCount; i++) {
      nsRubyTextFrame* textFrame = column.mTextFrames[i];
      if (!textFrame && nextColumn.isSome()) {
        textFrame = nextColumn->mTextFrames[i];
      }
      if (textFrame) {
        aReflowInput.mTextContainers[i]->PushChildrenToOverflow(
            textFrame, textFrame->GetPrevSibling());
      }
    }
  } else if (reflowStatus.IsInlineBreakAfter()) {
    // |reflowStatus| being break after here may only happen when
    // there is a break after the column just pulled, or the whole
    // segment has been completely reflowed. In those cases, we do
    // not need to push anything.
    MOZ_ASSERT(e.AtEnd());
    aStatus.SetInlineLineBreakAfter();
  }

  return icoord;
}

nscoord nsRubyBaseContainerFrame::ReflowOneColumn(
    const RubyReflowInput& aReflowInput, uint32_t aColumnIndex,
    const RubyColumn& aColumn, ReflowOutput& aDesiredSize,
    nsReflowStatus& aStatus) {
  const ReflowInput& baseReflowInput = aReflowInput.mBaseReflowInput;
  const auto& textReflowInputs = aReflowInput.mTextReflowInputs;
  nscoord istart = baseReflowInput.mLineLayout->GetCurrentICoord();

  if (aColumn.mBaseFrame) {
    bool allowBreakBefore = aColumnIndex ? aReflowInput.mAllowLineBreak
                                         : aReflowInput.mAllowInitialLineBreak;
    if (allowBreakBefore) {
      gfxBreakPriority breakPriority =
          LineBreakBefore(aColumn.mBaseFrame,
                          baseReflowInput.mRenderingContext->GetDrawTarget(),
                          baseReflowInput.mLineLayout->LineContainerFrame(),
                          baseReflowInput.mLineLayout->GetLine());
      if (breakPriority != gfxBreakPriority::eNoBreak) {
        gfxBreakPriority lastBreakPriority =
            baseReflowInput.mLineLayout->LastOptionalBreakPriority();
        if (breakPriority >= lastBreakPriority) {
          // Either we have been overflow, or we are forced
          // to break here, do break before.
          if (istart > baseReflowInput.AvailableISize() ||
              baseReflowInput.mLineLayout->NotifyOptionalBreakPosition(
                  aColumn.mBaseFrame, 0, true, breakPriority)) {
            aStatus.SetInlineLineBreakBeforeAndReset();
            return 0;
          }
        }
      }
    }
  }

  const uint32_t rtcCount = aReflowInput.mTextContainers.Length();
  MOZ_ASSERT(aColumn.mTextFrames.Length() == rtcCount);
  MOZ_ASSERT(textReflowInputs.Length() == rtcCount);
  nscoord columnISize = 0;

  nsAutoString baseText;
  if (aColumn.mBaseFrame) {
    nsLayoutUtils::GetFrameTextContent(aColumn.mBaseFrame, baseText);
  }

  // Reflow text frames
  for (uint32_t i = 0; i < rtcCount; i++) {
    nsRubyTextFrame* textFrame = aColumn.mTextFrames[i];
    if (textFrame) {
      bool isCollapsed = false;
      if (textFrame->StyleVisibility()->mVisible == StyleVisibility::Collapse) {
        isCollapsed = true;
      } else {
        // Per CSS Ruby spec, the content comparison for auto-hiding
        // takes place prior to white spaces collapsing (white-space)
        // and text transformation (text-transform), and ignores elements
        // (considers only the textContent of the boxes). Which means
        // using the content tree text comparison is correct.
        nsAutoString annotationText;
        nsLayoutUtils::GetFrameTextContent(textFrame, annotationText);
        isCollapsed = annotationText.Equals(baseText);
      }
      if (isCollapsed) {
        textFrame->AddStateBits(NS_RUBY_TEXT_FRAME_COLLAPSED);
      } else {
        textFrame->RemoveStateBits(NS_RUBY_TEXT_FRAME_COLLAPSED);
      }
      RubyUtils::ClearReservedISize(textFrame);

      bool pushedFrame;
      nsReflowStatus reflowStatus;
      nsLineLayout* lineLayout = textReflowInputs[i]->mLineLayout;
      nscoord textIStart = lineLayout->GetCurrentICoord();
      lineLayout->ReflowFrame(textFrame, reflowStatus, nullptr, pushedFrame);
      if (MOZ_UNLIKELY(reflowStatus.IsInlineBreak() || pushedFrame)) {
        MOZ_ASSERT_UNREACHABLE(
            "Any line break inside ruby box should have been suppressed");
        // For safety, always drain the overflow list, so that
        // no frames are left there after reflow.
        textFrame->DrainSelfOverflowList();
      }
      nscoord textISize = lineLayout->GetCurrentICoord() - textIStart;
      columnISize = std::max(columnISize, textISize);
    }
  }

  // Reflow the base frame
  if (aColumn.mBaseFrame) {
    RubyUtils::ClearReservedISize(aColumn.mBaseFrame);

    bool pushedFrame;
    nsReflowStatus reflowStatus;
    nsLineLayout* lineLayout = baseReflowInput.mLineLayout;
    WritingMode lineWM = lineLayout->GetWritingMode();
    nscoord baseIStart = lineLayout->GetCurrentICoord();
    ReflowOutput metrics(lineWM);
    lineLayout->ReflowFrame(aColumn.mBaseFrame, reflowStatus, &metrics,
                            pushedFrame);
    if (MOZ_UNLIKELY(reflowStatus.IsInlineBreak() || pushedFrame)) {
      MOZ_ASSERT_UNREACHABLE(
          "Any line break inside ruby box should have been suppressed");
      // For safety, always drain the overflow list, so that
      // no frames are left there after reflow.
      aColumn.mBaseFrame->DrainSelfOverflowList();
    }
    nscoord baseISize = lineLayout->GetCurrentICoord() - baseIStart;
    columnISize = std::max(columnISize, baseISize);
    // Calculate ascent & descent of the base frame and update desired
    // size of this base container accordingly.
    nscoord oldAscent = aDesiredSize.BlockStartAscent();
    nscoord oldDescent = aDesiredSize.BSize(lineWM) - oldAscent;
    nscoord baseAscent = metrics.BlockStartAscent();
    nscoord baseDesent = metrics.BSize(lineWM) - baseAscent;
    LogicalMargin margin = aColumn.mBaseFrame->GetLogicalUsedMargin(lineWM);
    nscoord newAscent = std::max(baseAscent + margin.BStart(lineWM), oldAscent);
    nscoord newDescent = std::max(baseDesent + margin.BEnd(lineWM), oldDescent);
    aDesiredSize.SetBlockStartAscent(newAscent);
    aDesiredSize.BSize(lineWM) = newAscent + newDescent;
  }

  // Align all the line layout to the new coordinate.
  nscoord icoord = istart + columnISize;
  nscoord deltaISize = icoord - baseReflowInput.mLineLayout->GetCurrentICoord();
  if (deltaISize > 0) {
    baseReflowInput.mLineLayout->AdvanceICoord(deltaISize);
    if (aColumn.mBaseFrame) {
      RubyUtils::SetReservedISize(aColumn.mBaseFrame, deltaISize);
    }
  }
  for (uint32_t i = 0; i < rtcCount; i++) {
    if (aReflowInput.mTextContainers[i]->IsSpanContainer()) {
      continue;
    }
    nsLineLayout* lineLayout = textReflowInputs[i]->mLineLayout;
    nsRubyTextFrame* textFrame = aColumn.mTextFrames[i];
    nscoord deltaISize = icoord - lineLayout->GetCurrentICoord();
    if (deltaISize > 0) {
      lineLayout->AdvanceICoord(deltaISize);
      if (textFrame && !textFrame->IsCollapsed()) {
        RubyUtils::SetReservedISize(textFrame, deltaISize);
      }
    }
    if (aColumn.mBaseFrame && textFrame) {
      lineLayout->AttachLastFrameToBaseLineLayout();
    }
  }

  return columnISize;
}

nsRubyBaseContainerFrame::PullFrameState::PullFrameState(
    nsRubyBaseContainerFrame* aBaseContainer,
    const AutoRubyTextContainerArray& aTextContainers)
    : mBase(aBaseContainer), mTextContainers(aTextContainers) {
  const uint32_t rtcCount = aTextContainers.Length();
  for (uint32_t i = 0; i < rtcCount; i++) {
    mTexts.AppendElement(aTextContainers[i]);
  }
}

void nsRubyBaseContainerFrame::PullOneColumn(nsLineLayout* aLineLayout,
                                             PullFrameState& aPullFrameState,
                                             RubyColumn& aColumn,
                                             bool& aIsComplete) {
  const AutoRubyTextContainerArray& textContainers =
      aPullFrameState.mTextContainers;
  const uint32_t rtcCount = textContainers.Length();

  nsIFrame* nextBase = GetNextInFlowChild(aPullFrameState.mBase);
  MOZ_ASSERT(!nextBase || nextBase->IsRubyBaseFrame());
  aColumn.mBaseFrame = static_cast<nsRubyBaseFrame*>(nextBase);
  bool foundFrame = !!aColumn.mBaseFrame;
  bool pullingIntraLevelWhitespace =
      aColumn.mBaseFrame && aColumn.mBaseFrame->IsIntraLevelWhitespace();

  aColumn.mTextFrames.ClearAndRetainStorage();
  for (uint32_t i = 0; i < rtcCount; i++) {
    nsIFrame* nextText =
        textContainers[i]->GetNextInFlowChild(aPullFrameState.mTexts[i]);
    MOZ_ASSERT(!nextText || nextText->IsRubyTextFrame());
    nsRubyTextFrame* textFrame = static_cast<nsRubyTextFrame*>(nextText);
    aColumn.mTextFrames.AppendElement(textFrame);
    foundFrame = foundFrame || nextText;
    if (nextText && !pullingIntraLevelWhitespace) {
      pullingIntraLevelWhitespace = textFrame->IsIntraLevelWhitespace();
    }
  }
  // If there exists any frame in continations, we haven't
  // completed the reflow process.
  aIsComplete = !foundFrame;
  if (!foundFrame) {
    return;
  }

  aColumn.mIsIntraLevelWhitespace = pullingIntraLevelWhitespace;
  if (pullingIntraLevelWhitespace) {
    // We are pulling an intra-level whitespace. Drop all frames which
    // are not part of this intra-level whitespace column. (Those frames
    // are really part of the *next* column, after the pulled one.)
    if (aColumn.mBaseFrame && !aColumn.mBaseFrame->IsIntraLevelWhitespace()) {
      aColumn.mBaseFrame = nullptr;
    }
    for (uint32_t i = 0; i < rtcCount; i++) {
      nsRubyTextFrame*& textFrame = aColumn.mTextFrames[i];
      if (textFrame && !textFrame->IsIntraLevelWhitespace()) {
        textFrame = nullptr;
      }
    }
  } else {
    // We are not pulling an intra-level whitespace, which means all
    // elements we are going to pull can have non-whitespace content,
    // which may contain float which we need to reparent.
    MOZ_ASSERT(aColumn.begin() != aColumn.end(),
               "Ruby column shouldn't be empty");
    nsBlockFrame* oldFloatCB =
        nsLayoutUtils::GetFloatContainingBlock(*aColumn.begin());
#ifdef DEBUG
    MOZ_ASSERT(oldFloatCB, "Must have found a float containing block");
    for (nsIFrame* frame : aColumn) {
      MOZ_ASSERT(nsLayoutUtils::GetFloatContainingBlock(frame) == oldFloatCB,
                 "All frames in the same ruby column should share "
                 "the same old float containing block");
    }
#endif
    nsBlockFrame* newFloatCB = do_QueryFrame(aLineLayout->LineContainerFrame());
    MOZ_ASSERT(newFloatCB, "Must have a float containing block");
    if (oldFloatCB != newFloatCB) {
      for (nsIFrame* frame : aColumn) {
        newFloatCB->ReparentFloats(frame, oldFloatCB, false);
      }
    }
  }

  // Pull the frames of this column.
  if (aColumn.mBaseFrame) {
    DebugOnly<nsIFrame*> pulled = PullNextInFlowChild(aPullFrameState.mBase);
    MOZ_ASSERT(pulled == aColumn.mBaseFrame, "pulled a wrong frame?");
  }
  for (uint32_t i = 0; i < rtcCount; i++) {
    if (aColumn.mTextFrames[i]) {
      DebugOnly<nsIFrame*> pulled =
          textContainers[i]->PullNextInFlowChild(aPullFrameState.mTexts[i]);
      MOZ_ASSERT(pulled == aColumn.mTextFrames[i], "pulled a wrong frame?");
    }
  }

  if (!aIsComplete) {
    // We pulled frames from the next line, hence mark it dirty.
    aLineLayout->SetDirtyNextLine();
  }
}

nscoord nsRubyBaseContainerFrame::ReflowSpans(
    const RubyReflowInput& aReflowInput) {
  nscoord spanISize = 0;
  for (uint32_t i = 0, iend = aReflowInput.mTextContainers.Length(); i < iend;
       i++) {
    nsRubyTextContainerFrame* container = aReflowInput.mTextContainers[i];
    if (!container->IsSpanContainer()) {
      continue;
    }

    nsIFrame* rtFrame = container->PrincipalChildList().FirstChild();
    nsReflowStatus reflowStatus;
    bool pushedFrame;
    nsLineLayout* lineLayout = aReflowInput.mTextReflowInputs[i]->mLineLayout;
    MOZ_ASSERT(lineLayout->GetCurrentICoord() == 0,
               "border/padding of rtc should have been suppressed");
    lineLayout->ReflowFrame(rtFrame, reflowStatus, nullptr, pushedFrame);
    MOZ_ASSERT(!reflowStatus.IsInlineBreak() && !pushedFrame,
               "Any line break inside ruby box should has been suppressed");
    spanISize = std::max(spanISize, lineLayout->GetCurrentICoord());
  }
  return spanISize;
}
