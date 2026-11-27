/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS :first-letter pseudo-element */

#include "nsFirstLetterFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsCSSFrameConstructor.h"
#include "nsFrameManager.h"
#include "nsIContent.h"
#include "nsLayoutUtils.h"
#include "nsLineLayout.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsTextFrame.h"

using namespace mozilla;
using namespace mozilla::layout;

nsFirstLetterFrame* NS_NewFirstLetterFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle) {
  return new nsFirstLetterFrame(aStyle, aPresShell->GetPresContext());
}

nsFirstLetterFrame* NS_NewFloatingFirstLetterFrame(PresShell* aPresShell,
                                                   ComputedStyle* aStyle) {
  return new nsFloatingFirstLetterFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsFirstLetterFrame)

NS_QUERYFRAME_HEAD(nsFirstLetterFrame)
  NS_QUERYFRAME_ENTRY(nsFirstLetterFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsFloatingFirstLetterFrame)
NS_QUERYFRAME_HEAD(nsFloatingFirstLetterFrame)
  NS_QUERYFRAME_ENTRY(nsFloatingFirstLetterFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsFirstLetterFrame)

#ifdef DEBUG_FRAME_DUMP
nsresult nsFirstLetterFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Letter"_ns, aResult);
}
#endif

void nsFirstLetterFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  BuildDisplayListForInline(aBuilder, aLists);
}

void nsFirstLetterFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                              nsIFrame* aPrevInFlow) {
  RefPtr<ComputedStyle> newSC;
  if (aPrevInFlow) {
    // Get proper ComputedStyle for ourselves.  We're creating the frame
    // that represents everything *except* the first letter, so just create
    // a ComputedStyle that inherits from our style parent, with no extra rules.
    nsIFrame* styleParent =
        CorrectStyleParentFrame(aParent, PseudoStyleType::FirstLetter);
    ComputedStyle* parentComputedStyle = styleParent->Style();
    newSC = PresContext()->StyleSet()->ResolveStyleForFirstLetterContinuation(
        parentComputedStyle);
    SetComputedStyleWithoutNotification(newSC);
  }

  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);
}

void nsFirstLetterFrame::SetInitialChildList(ChildListID aListID,
                                             nsFrameList&& aChildList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal,
             "Principal child list is the only "
             "list that nsFirstLetterFrame should set via this function");
  for (nsIFrame* f : aChildList) {
    MOZ_ASSERT(f->GetParent() == this, "Unexpected parent");
    MOZ_ASSERT(f->IsTextFrame(),
               "We should not have kids that are containers!");
    nsLayoutUtils::MarkDescendantsDirty(f);  // Drops cached textruns
  }

  mFrames = std::move(aChildList);
}

nsresult nsFirstLetterFrame::GetChildFrameContainingOffset(
    int32_t inContentOffset, bool inHint, int32_t* outFrameContentOffset,
    nsIFrame** outChildFrame) {
  nsIFrame* kid = mFrames.FirstChild();
  if (kid) {
    return kid->GetChildFrameContainingOffset(
        inContentOffset, inHint, outFrameContentOffset, outChildFrame);
  }
  return nsIFrame::GetChildFrameContainingOffset(
      inContentOffset, inHint, outFrameContentOffset, outChildFrame);
}

// Needed for non-floating first-letter frames and for the continuations
// following the first-letter that we also use nsFirstLetterFrame for.
/* virtual */
void nsFirstLetterFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                           InlineMinISizeData* aData) {
  DoInlineMinISize(aInput, aData);
}

// Needed for non-floating first-letter frames and for the continuations
// following the first-letter that we also use nsFirstLetterFrame for.
/* virtual */
void nsFirstLetterFrame::AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                                            InlinePrefISizeData* aData) {
  DoInlinePrefISize(aInput, aData);
}

// Needed for floating first-letter frames.
nscoord nsFirstLetterFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  return IntrinsicISizeFromInline(aInput, aType);
}

/* virtual */
nsIFrame::SizeComputationResult nsFirstLetterFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  if (GetPrevInFlow()) {
    // We're wrapping the text *after* the first letter, so behave like an
    // inline frame.
    return {LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
            AspectRatioUsage::None};
  }
  return nsContainerFrame::ComputeSize(aSizingInput, aWM, aCBSize,
                                       aAvailableISize, aMargin, aBorderPadding,
                                       aSizeOverrides, aFlags);
}

bool nsFirstLetterFrame::UseTightBounds() const {
  int v = StaticPrefs::layout_css_floating_first_letter_tight_glyph_bounds();

  // Check for the simple cases:
  //   pref value > 0: use legacy gecko behavior
  //   pref value = 0: use webkit/blink-like behavior
  if (v > 0) {
    return true;
  }
  if (v == 0) {
    return false;
  }

  // Pref value < 0: use heuristics to determine whether the page is assuming
  // webkit/blink-style behavior:
  // If line-height is less than font-size, or there is a negative block-start
  // or -end margin, use webkit/blink behavior.
  if (nsTextFrame* textFrame = do_QueryFrame(mFrames.FirstChild())) {
    RefPtr<nsFontMetrics> fm = textFrame->InflatedFontMetrics();
    if (textFrame->ComputeLineHeight() < fm->EmHeight()) {
      return false;
    }
  }

  const auto wm = GetWritingMode();
  const auto* styleMargin = StyleMargin();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);
  const auto bStart =
      styleMargin->GetMargin(LogicalSide::BStart, wm, anchorResolutionParams);
  // Currently, we only check for margins with negative *length* values;
  // negative percentages seem unlikely to be used/useful in this context.
  if (bStart->ConvertsToLength() && bStart->ToLength() < 0) {
    return false;
  }
  const auto bEnd =
      styleMargin->GetMargin(LogicalSide::BEnd, wm, anchorResolutionParams);
  return !(bEnd->ConvertsToLength() && bEnd->ToLength() < 0);
}

void nsFirstLetterFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aMetrics,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aReflowStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsFirstLetterFrame");
  MOZ_ASSERT(aReflowStatus.IsEmpty(),
             "Caller should pass a fresh reflow status!");

  // Grab overflow list
  DrainOverflowFrames(aPresContext);

  nsIFrame* kid = mFrames.FirstChild();

  // Setup reflow input for our child
  WritingMode wm = aReflowInput.GetWritingMode();
  LogicalSize availSize = aReflowInput.AvailableSize();
  const auto bp = aReflowInput.ComputedLogicalBorderPadding(wm);
  NS_ASSERTION(availSize.ISize(wm) != NS_UNCONSTRAINEDSIZE,
               "should no longer use unconstrained inline size");
  availSize.ISize(wm) -= bp.IStartEnd(wm);
  if (NS_UNCONSTRAINEDSIZE != availSize.BSize(wm)) {
    availSize.BSize(wm) -= bp.BStartEnd(wm);
  }

  WritingMode lineWM = aMetrics.GetWritingMode();
  ReflowOutput kidMetrics(lineWM);

  // Reflow the child
  if (!aReflowInput.mLineLayout) {
    // When there is no lineLayout provided, we provide our own. The
    // only time that the first-letter-frame is not reflowing in a
    // line context is when its floating.
    WritingMode kidWritingMode = WritingModeForLine(wm, kid);
    LogicalSize kidAvailSize = availSize.ConvertTo(kidWritingMode, wm);
    ReflowInput rs(aPresContext, aReflowInput, kid, kidAvailSize);
    nsLineLayout ll(aPresContext, nullptr, aReflowInput, nullptr, nullptr);

    // This frame does not get constructed for an empty inline frame, so
    // `CollapseEmptyInlineFramesInLine` should not matter.
    ll.BeginLineReflow(
        bp.IStart(wm), bp.BStart(wm), availSize.ISize(wm), NS_UNCONSTRAINEDSIZE,
        false, true, kidWritingMode,
        nsSize(aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));
    rs.mLineLayout = &ll;
    ll.SetInFirstLetter(true);
    ll.SetFirstLetterStyleOK(true);

    kid->Reflow(aPresContext, kidMetrics, rs, aReflowStatus);

    ll.EndLineReflow();
    ll.SetInFirstLetter(false);

    // In the floating first-letter case, we need to set this ourselves;
    // nsLineLayout::BeginSpan will set it in the other case
    mBaseline = kidMetrics.BlockStartAscent();

    // Place and size the child and update the output metrics
    LogicalSize convertedSize = kidMetrics.Size(wm);

    const bool tightBounds = UseTightBounds();
    const nscoord shift =
        tightBounds ? 0
                    // Shift by half of the difference between the line-height
                    // we're going to use and current height of the kid frame.
                    : (rs.GetLineHeight() - convertedSize.BSize(wm)) / 2;

    kid->SetRect(nsRect(bp.IStart(wm), bp.BStart(wm) + shift,
                        convertedSize.ISize(wm), convertedSize.BSize(wm)));
    kid->FinishAndStoreOverflow(&kidMetrics, rs.mStyleDisplay);
    kid->DidReflow(aPresContext, nullptr);

    if (!tightBounds) {
      // Adjust size to account for line-height.
      convertedSize.BSize(wm) = rs.GetLineHeight();
    }

    convertedSize.ISize(wm) += bp.IStartEnd(wm);
    convertedSize.BSize(wm) += bp.BStartEnd(wm);
    aMetrics.SetSize(wm, convertedSize);
    aMetrics.SetBlockStartAscent(kidMetrics.BlockStartAscent() + bp.BStart(wm));

    // Ensure that the overflow rect contains the child textframe's
    // overflow rect.
    // Note that if this is floating, the overline/underline drawable
    // area is in the overflow rect of the child textframe.
    aMetrics.UnionOverflowAreasWithDesiredBounds();
    ConsiderChildOverflow(aMetrics.mOverflowAreas, kid);

    FinishAndStoreOverflow(&aMetrics, aReflowInput.mStyleDisplay);
  } else {
    // Pretend we are a span and reflow the child frame
    nsLineLayout* ll = aReflowInput.mLineLayout;
    bool pushedFrame;

    ll->SetInFirstLetter(Style()->GetPseudoType() ==
                         PseudoStyleType::FirstLetter);
    ll->BeginSpan(this, &aReflowInput, bp.IStart(wm), availSize.ISize(wm),
                  &mBaseline);
    ll->ReflowFrame(kid, aReflowStatus, &kidMetrics, pushedFrame);
    NS_ASSERTION(lineWM.IsVertical() == wm.IsVertical(),
                 "we're assuming we can mix sizes between lineWM and wm "
                 "since we shouldn't have orthogonal writing modes within "
                 "a line.");
    aMetrics.ISize(lineWM) = ll->EndSpan(this) + bp.IStartEnd(wm);
    ll->SetInFirstLetter(false);

    if (mComputedStyle->StyleTextReset()->mInitialLetter.size != 0.0f) {
      aMetrics.SetBlockStartAscent(kidMetrics.BlockStartAscent() +
                                   bp.BStart(wm));
      aMetrics.BSize(lineWM) = kidMetrics.BSize(lineWM) + bp.BStartEnd(wm);
    } else {
      nsLayoutUtils::SetBSizeFromFontMetrics(this, aMetrics, bp, lineWM, wm);
    }
  }

  if (!aReflowStatus.IsInlineBreakBefore()) {
    // Create a continuation or remove existing continuations based on
    // the reflow completion status.
    if (aReflowStatus.IsComplete()) {
      if (aReflowInput.mLineLayout) {
        aReflowInput.mLineLayout->SetFirstLetterStyleOK(false);
      }
      if (nsIFrame* kidNextInFlow = kid->GetNextInFlow()) {
        DestroyContext context(PresShell());
        // Remove all of the childs next-in-flows
        kidNextInFlow->GetParent()->DeleteNextInFlowChild(context,
                                                          kidNextInFlow, true);
      }
    } else {
      // Create a continuation for the child frame if it doesn't already
      // have one.
      if (!IsFloating()) {
        CreateNextInFlow(kid);
        // And then push it to our overflow list
        nsFrameList overflow = mFrames.TakeFramesAfter(kid);
        if (overflow.NotEmpty()) {
          SetOverflowFrames(std::move(overflow));
        }
      } else if (!kid->GetNextInFlow()) {
        // For floating first letter frames (if a continuation wasn't already
        // created for us) we need to put the continuation with the rest of the
        // text that the first letter frame was made out of.
        nsIFrame* continuation;
        CreateContinuationForFloatingParent(kid, &continuation, true);
      }
    }
  }
}

/* virtual */
bool nsFirstLetterFrame::CanContinueTextRun() const {
  // We can continue a text run through a first-letter frame.
  return true;
}

void nsFirstLetterFrame::CreateContinuationForFloatingParent(
    nsIFrame* aChild, nsIFrame** aContinuation, bool aIsFluid) {
  NS_ASSERTION(IsFloating(),
               "can only call this on floating first letter frames");
  MOZ_ASSERT(aContinuation, "bad args");

  *aContinuation = nullptr;

  mozilla::PresShell* presShell = PresShell();
  nsPlaceholderFrame* placeholderFrame = GetPlaceholderFrame();
  nsContainerFrame* parent = placeholderFrame->GetParent();

  nsIFrame* continuation = presShell->FrameConstructor()->CreateContinuingFrame(
      aChild, parent, aIsFluid);

  // The continuation will have gotten the first letter style from its
  // prev continuation, so we need to repair the ComputedStyle so it
  // doesn't have the first letter styling.
  //
  // Note that getting parent frame's ComputedStyle is different from getting
  // this frame's ComputedStyle's parent in the presence of ::first-line,
  // which we do want the continuation to inherit from.
  ComputedStyle* parentSC = parent->Style();
  RefPtr<ComputedStyle> newSC =
      presShell->StyleSet()->ResolveStyleForFirstLetterContinuation(parentSC);
  continuation->SetComputedStyle(newSC);
  nsLayoutUtils::MarkDescendantsDirty(continuation);

  // XXX Bidi may not be involved but we have to use the list name
  // FrameChildListID::NoReflowPrincipal because this is just like creating a
  // continuation except we have to insert it in a different place and we don't
  // want a reflow command to try to be issued.
  parent->InsertFrames(FrameChildListID::NoReflowPrincipal, placeholderFrame,
                       nullptr, nsFrameList(continuation, continuation));

  *aContinuation = continuation;
}

nsTextFrame* nsFirstLetterFrame::CreateContinuationForFramesAfter(
    nsTextFrame* aFrame) {
  auto* presShell = PresShell();
  auto* parent = GetParent();
  auto* letterContinuation = static_cast<nsFirstLetterFrame*>(
      presShell->FrameConstructor()->CreateContinuingFrame(this, parent, true));

  parent->InsertFrames(FrameChildListID::NoReflowPrincipal, this, nullptr,
                       nsFrameList(letterContinuation, letterContinuation));

  nsTextFrame* next;
  auto list = mFrames.TakeFramesAfter(aFrame);
  if (list.NotEmpty()) {
    // If we already have additional frames, just move them to the continuation.
    next = static_cast<nsTextFrame*>(list.FirstChild());
    for (auto* frame : list) {
      frame->SetParent(letterContinuation);
    }
    // If the first frame of the list was not a fluid continuation, we need to
    // insert one there to accept the overflowing text without disrupting the
    // existing fixed continuation.
    if (!next->HasAnyStateBits(NS_FRAME_IS_FLUID_CONTINUATION)) {
      next = static_cast<nsTextFrame*>(
          presShell->FrameConstructor()->CreateContinuingFrame(
              aFrame, letterContinuation));
      list.InsertFrame(letterContinuation, nullptr, next);
    }
    letterContinuation->SetInitialChildList(FrameChildListID::Principal,
                                            std::move(list));
  } else {
    // We don't have extra frames already, so create a new text continuation.
    next = static_cast<nsTextFrame*>(
        presShell->FrameConstructor()->CreateContinuingFrame(
            aFrame, letterContinuation));
    letterContinuation->SetInitialChildList(FrameChildListID::Principal,
                                            nsFrameList(next, next));
  }

  // Update the computed style of the continuation text frame(s) that are
  // no longer supposed to be first-letter style.
  ComputedStyle* parentSC = letterContinuation->Style();
  RefPtr<ComputedStyle> newSC =
      presShell->StyleSet()->ResolveStyleForFirstLetterContinuation(parentSC);
  for (auto* frame : letterContinuation->PrincipalChildList()) {
    frame->SetComputedStyle(newSC);
  }

  return next;
}

void nsFirstLetterFrame::DrainOverflowFrames(nsPresContext* aPresContext) {
  // Check for an overflow list with our prev-in-flow
  nsFirstLetterFrame* prevInFlow = (nsFirstLetterFrame*)GetPrevInFlow();
  if (prevInFlow) {
    AutoFrameListPtr overflowFrames(prevInFlow->StealOverflowFrames());
    if (overflowFrames) {
      NS_ASSERTION(mFrames.IsEmpty(), "bad overflow list");
      mFrames.InsertFrames(this, nullptr, std::move(*overflowFrames));
    }
  }

  // It's also possible that we have an overflow list for ourselves
  AutoFrameListPtr overflowFrames(StealOverflowFrames());
  if (overflowFrames) {
    NS_ASSERTION(mFrames.NotEmpty(), "overflow list w/o frames");
    mFrames.AppendFrames(nullptr, std::move(*overflowFrames));
  }

  // Now repair our first frames ComputedStyle (since we only reflow
  // one frame there is no point in doing any other ones until they
  // are reflowed)
  nsIFrame* kid = mFrames.FirstChild();
  if (kid) {
    nsIContent* kidContent = kid->GetContent();
    if (kidContent) {
      NS_ASSERTION(kidContent->IsText(), "should contain only text nodes");
      ComputedStyle* parentSC;
      if (prevInFlow) {
        // This is for the rest of the content not in the first-letter.
        nsIFrame* styleParent =
            CorrectStyleParentFrame(GetParent(), PseudoStyleType::FirstLetter);
        parentSC = styleParent->Style();
      } else {
        // And this for the first-letter style.
        parentSC = mComputedStyle;
      }
      RefPtr<ComputedStyle> sc =
          aPresContext->StyleSet()->ResolveStyleForText(kidContent, parentSC);
      kid->SetComputedStyle(sc);
      nsLayoutUtils::MarkDescendantsDirty(kid);
    }
  }
}

Maybe<nscoord> nsFirstLetterFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return Nothing{};
  }
  return Some(mBaseline);
}

LogicalSides nsFirstLetterFrame::GetLogicalSkipSides() const {
  if (GetPrevContinuation()) {
    // We shouldn't get calls to GetLogicalSkipSides for later continuations
    // since they have separate ComputedStyles with initial values for all the
    // properties that could trigger a call to GetLogicalSkipSides. Then again,
    // it's not really an error to call GetLogicalSkipSides on any frame, so
    // that's why we handle it properly.
    return LogicalSides(mWritingMode, LogicalSides::All);
  }
  return LogicalSides(mWritingMode);  // first continuation displays all sides
}
