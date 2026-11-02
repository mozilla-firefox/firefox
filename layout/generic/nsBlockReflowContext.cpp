/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* class that a parent frame uses to reflow a block frame */

#include "nsBlockReflowContext.h"

#include "BlockReflowState.h"
#include "nsBlockFrame.h"
#include "nsColumnSetFrame.h"
#include "nsContainerFrame.h"
#include "nsFloatManager.h"
#include "nsLayoutUtils.h"
#include "nsLineBox.h"

using namespace mozilla;

#ifdef DEBUG
#  include "nsBlockDebugFlags.h"  // For NOISY_BLOCK_DIR_MARGINS
#endif

nsBlockReflowContext::nsBlockReflowContext(nsPresContext* aPresContext,
                                           const ReflowInput& aParentRI)
    : mPresContext(aPresContext),
      mOuterReflowInput(aParentRI),
      mFrame(nullptr),
      mSpace(aParentRI.GetWritingMode()),
      mICoord(0),
      mBCoord(0),
      mMetrics(aParentRI) {}

static nsIFrame* DescendIntoBlockLevelFrame(nsIFrame* aFrame) {
  LayoutFrameType type = aFrame->Type();
  if (type == LayoutFrameType::ColumnSet) {
    static_cast<nsColumnSetFrame*>(aFrame)->DrainOverflowColumns();
    nsIFrame* child = aFrame->PrincipalChildList().FirstChild();
    if (child) {
      return DescendIntoBlockLevelFrame(child);
    }
  }
  return aFrame;
}

bool nsBlockReflowContext::ComputeCollapsedBStartMargin(
    const ReflowInput& aRI, CollapsingMargin* aMargin,
    nsIFrame* aClearanceFrame, bool* aMayNeedRetry, bool* aBlockIsEmpty) {
  WritingMode wm = aRI.GetWritingMode();
  WritingMode parentWM = mMetrics.GetWritingMode();

  // Include block-start element of frame's margin
  aMargin->Include(aRI.ComputedLogicalMargin(parentWM).BStart(parentWM));

  // The inclusion of the block-end margin when empty is done by the caller
  // since it doesn't need to be done by the top-level (non-recursive)
  // caller.

#ifdef NOISY_BLOCK_DIR_MARGINS
  aRI.mFrame->ListTag(stdout);
  printf(": %d => %d\n", aRI.ComputedLogicalMargin(wm).BStart(wm),
         aMargin->get());
#endif

  bool dirtiedLine = false;
  bool setBlockIsEmpty = false;

  // Calculate the frame's generational block-start-margin from its child
  // blocks. Note that if the frame has a non-zero block-start-border or
  // block-start-padding then this step is skipped because it will be a margin
  // root.  It is also skipped if the frame is a margin root for other
  // reasons.
  nsIFrame* frame = DescendIntoBlockLevelFrame(aRI.mFrame);
  nsPresContext* prescontext = frame->PresContext();
  nsBlockFrame* block = nullptr;
  if (0 == aRI.ComputedLogicalBorderPadding(wm).BStart(wm)) {
    block = do_QueryFrame(frame);
    if (block) {
      bool bStartMarginRoot, unused;
      block->IsMarginRoot(&bStartMarginRoot, &unused);
      if (bStartMarginRoot) {
        block = nullptr;
      }
    }
  }

  // iterate not just through the lines of 'block' but also its
  // overflow lines and the normal and overflow lines of its next in
  // flows. Note that this will traverse some frames more than once:
  // for example, if A contains B and A->nextinflow contains
  // B->nextinflow, we'll traverse B->nextinflow twice. But this is
  // OK because our traversal is idempotent.
  for (; block; block = static_cast<nsBlockFrame*>(block->GetNextInFlow())) {
    for (int overflowLines = 0; overflowLines <= 1; ++overflowLines) {
      nsBlockFrame::LineIterator line;
      nsBlockFrame::LineIterator line_end;
      bool anyLines = true;
      if (overflowLines) {
        nsBlockFrame::FrameLines* frames = block->GetOverflowLines();
        nsLineList* lines = frames ? &frames->mLines : nullptr;
        if (!lines) {
          anyLines = false;
        } else {
          line = lines->begin();
          line_end = lines->end();
        }
      } else {
        line = block->LinesBegin();
        line_end = block->LinesEnd();
      }
      for (; anyLines && line != line_end; ++line) {
        if (!aClearanceFrame && line->HasClearance()) {
          // If we don't have a clearance frame, then we're computing
          // the collapsed margin in the first pass, assuming that all
          // lines have no clearance. So clear their clearance flags.
          line->ClearHasClearance();
          line->MarkDirty();
          dirtiedLine = true;
        }

        bool isEmpty;
        if (line->IsInline()) {
          isEmpty = line->IsEmpty();
        } else {
          nsIFrame* kid = line->mFirstChild;
          if (kid == aClearanceFrame) {
            line->SetHasClearance();
            line->MarkDirty();
            dirtiedLine = true;
            if (!setBlockIsEmpty && aBlockIsEmpty) {
              setBlockIsEmpty = true;
              *aBlockIsEmpty = false;
            }
            goto done;
          }
          // Here is where we recur. Now that we have determined that a
          // generational collapse is required we need to compute the
          // child blocks margin and so in so that we can look into
          // it. For its margins to be computed we need to have a reflow
          // input for it.

          // We may have to construct an extra reflow input here if
          // we drilled down through a block wrapper. At the moment
          // we can only drill down one level so we only have to support
          // one extra reflow input.
          const ReflowInput* outerReflowInput = &aRI;
          if (frame != aRI.mFrame) {
            NS_ASSERTION(frame->GetParent() == aRI.mFrame,
                         "Can only drill through one level of block wrapper");
            LogicalSize availSpace = aRI.ComputedSize(frame->GetWritingMode());
            outerReflowInput =
                new ReflowInput(prescontext, aRI, frame, availSpace);
          }
          {
            LogicalSize availSpace =
                outerReflowInput->ComputedSize(kid->GetWritingMode());
            ReflowInput innerReflowInput(prescontext, *outerReflowInput, kid,
                                         availSpace);
            // Record that we're being optimistic by assuming the kid
            // has no clearance
            if (kid->StyleDisplay()->mClear != StyleClear::None ||
                !nsBlockFrame::BlockCanIntersectFloats(kid)) {
              *aMayNeedRetry = true;
            }
            if (ComputeCollapsedBStartMargin(innerReflowInput, aMargin,
                                             aClearanceFrame, aMayNeedRetry,
                                             &isEmpty)) {
              line->MarkDirty();
              dirtiedLine = true;
            }
            if (isEmpty) {
              LogicalMargin innerMargin =
                  innerReflowInput.ComputedLogicalMargin(parentWM);
              aMargin->Include(innerMargin.BEnd(parentWM));
            }
          }
          if (outerReflowInput != &aRI) {
            delete const_cast<ReflowInput*>(outerReflowInput);
          }
        }
        if (!isEmpty) {
          if (!setBlockIsEmpty && aBlockIsEmpty) {
            setBlockIsEmpty = true;
            *aBlockIsEmpty = false;
          }
          goto done;
        }
      }
      if (!setBlockIsEmpty && aBlockIsEmpty) {
        // The first time we reach here is when this is the first block
        // and we have processed all its normal lines.
        setBlockIsEmpty = true;
        // All lines are empty, or we wouldn't be here!
        *aBlockIsEmpty = aRI.mFrame->IsSelfEmpty();
      }
    }
  }
done:

  if (!setBlockIsEmpty && aBlockIsEmpty) {
    *aBlockIsEmpty = aRI.mFrame->IsEmpty();
  }

#ifdef NOISY_BLOCK_DIR_MARGINS
  aRI.mFrame->ListTag(stdout);
  printf(": => %d\n", aMargin->get());
#endif

  return dirtiedLine;
}

void nsBlockReflowContext::ReflowBlock(const LogicalRect& aSpace,
                                       bool aApplyBStartMargin,
                                       CollapsingMargin& aPrevMargin,
                                       nscoord aClearance, nsLineBox* aLine,
                                       ReflowInput& aFrameRI,
                                       nsReflowStatus& aFrameReflowStatus,
                                       BlockReflowState& aState) {
  mFrame = aFrameRI.mFrame;
  mWritingMode = aState.mReflowInput.GetWritingMode();
  mContainerSize = aState.ContainerSize();
  mSpace = aSpace;

  if (!aState.IsAdjacentWithBStart()) {
    aFrameRI.mFlags.mIsTopOfPage = false;  // make sure this is cleared
  }

  if (aApplyBStartMargin) {
    mBStartMargin = aPrevMargin;

#ifdef NOISY_BLOCK_DIR_MARGINS
    mOuterReflowInput.mFrame->ListTag(stdout);
    printf(": reflowing ");
    mFrame->ListTag(stdout);
    printf(" margin => %d, clearance => %d\n", mBStartMargin.get(), aClearance);
#endif

    // Adjust the available size if it's constrained so that the
    // child frame doesn't think it can reflow into its margin area.
    if (mWritingMode.IsOrthogonalTo(mFrame->GetWritingMode())) {
      if (NS_UNCONSTRAINEDSIZE != aFrameRI.AvailableISize()) {
        aFrameRI.SetAvailableISize(std::max(
            0, aFrameRI.AvailableISize() - mBStartMargin.Get() - aClearance));
      }
    } else {
      if (NS_UNCONSTRAINEDSIZE != aFrameRI.AvailableBSize()) {
        aFrameRI.SetAvailableBSize(std::max(
            0, aFrameRI.AvailableBSize() - mBStartMargin.Get() - aClearance));
      }
    }
  } else {
    // nsBlockFrame::ReflowBlock might call us multiple times with
    // *different* values of aApplyBStartMargin.
    mBStartMargin.Zero();
  }

  nscoord tI = 0, tB = 0;
  // The values of x and y do not matter for floats, so don't bother
  // calculating them. Floats are guaranteed to have their own float
  // manager, so tI and tB don't matter.  mICoord and mBCoord don't
  // matter becacuse they are only used in PlaceBlock, which is not used
  // for floats.
  if (aLine) {
    // Compute inline/block coordinate where reflow will begin. Use the
    // rules from 10.3.3 to determine what to apply. At this point in the
    // reflow auto inline-start/end margins will have a zero value.
    LogicalMargin usedMargin = aFrameRI.ComputedLogicalMargin(mWritingMode);
    mICoord = mSpace.IStart(mWritingMode) + usedMargin.IStart(mWritingMode);
    mBCoord = mSpace.BStart(mWritingMode) + mBStartMargin.Get() + aClearance;

    LogicalRect space(
        mWritingMode, mICoord, mBCoord,
        mSpace.ISize(mWritingMode) - usedMargin.IStartEnd(mWritingMode),
        mSpace.BSize(mWritingMode) - usedMargin.BStartEnd(mWritingMode));
    tI = space.LineLeft(mWritingMode, mContainerSize);
    tB = mBCoord;

    if (!mFrame->HasAnyStateBits(NS_BLOCK_BFC)) {
      aFrameRI.mBlockDelta =
          mOuterReflowInput.mBlockDelta + mBCoord - aLine->BStart();
    }
  }

#ifdef DEBUG
  mMetrics.ISize(mWritingMode) = nscoord(0xdeadbeef);
  mMetrics.BSize(mWritingMode) = nscoord(0xdeadbeef);
#endif

  mOuterReflowInput.mFloatManager->Translate(tI, tB);
  mFrame->Reflow(mPresContext, mMetrics, aFrameRI, aFrameReflowStatus);
  mOuterReflowInput.mFloatManager->Translate(-tI, -tB);

#ifdef DEBUG
  if (!aFrameReflowStatus.IsInlineBreakBefore()) {
    if ((ABSURD_SIZE(mMetrics.ISize(mWritingMode)) ||
         ABSURD_SIZE(mMetrics.BSize(mWritingMode))) &&
        !mFrame->GetParent()->IsAbsurdSizeAssertSuppressed()) {
      printf("nsBlockReflowContext: ");
      mFrame->ListTag(stdout);
      printf(" metrics=%d,%d!\n", mMetrics.ISize(mWritingMode),
             mMetrics.BSize(mWritingMode));
    }
    if ((mMetrics.ISize(mWritingMode) == nscoord(0xdeadbeef)) ||
        (mMetrics.BSize(mWritingMode) == nscoord(0xdeadbeef))) {
      printf("nsBlockReflowContext: ");
      mFrame->ListTag(stdout);
      printf(" didn't set i/b %d,%d!\n", mMetrics.ISize(mWritingMode),
             mMetrics.BSize(mWritingMode));
    }
  }
#endif

  if (!mFrame->HasOverflowAreas()) {
    mMetrics.SetOverflowAreasToDesiredBounds();
  }

  if (!aFrameReflowStatus.IsInlineBreakBefore() &&
      !aFrameRI.WillReflowAgainForClearance() &&
      aFrameReflowStatus.IsFullyComplete()) {
    // If mFrame is fully-complete and has a next-in-flow, we need to delete
    // them now. Do not do this when a break-before is signaled or when a
    // clearance frame is discovered in mFrame's subtree because mFrame is going
    // to get reflowed again (whether the frame is (in)complete is undefined in
    // that case anyway).
    if (nsIFrame* kidNextInFlow = mFrame->GetNextInFlow()) {
      // Remove all of the childs next-in-flows. Make sure that we ask
      // the right parent to do the removal (it's possible that the
      // parent is not this because we are executing pullup code).
      // Floats will eventually be removed via nsBlockFrame::RemoveFloat
      // which detaches the placeholder from the float.
      nsOverflowContinuationTracker::AutoFinish fini(aState.mOverflowTracker,
                                                     mFrame);
      nsIFrame::DestroyContext context(mPresContext->PresShell());
      kidNextInFlow->GetParent()->DeleteNextInFlowChild(context, kidNextInFlow,
                                                        true);
    }
  }

  // If a frame indicated that it needs to be reflowed to apply text-box-trim
  // on the trim-end side, propagate that signal up to the block parent.
  aState.mNeedsTextBoxTrimAtFragmentEndRetry |=
      mMetrics.mNeedsTextBoxTrimAtFragmentEndRetry;
}

/**
 * Attempt to place the block frame within the available space.  If
 * it fits, apply inline-dir ("horizontal") positioning (CSS 10.3.3),
 * collapse margins (CSS2 8.3.1). Also apply relative positioning.
 */
bool nsBlockReflowContext::PlaceBlock(const ReflowInput& aReflowInput,
                                      bool aForceFit, nsLineBox* aLine,
                                      CollapsingMargin& aBEndMarginResult,
                                      OverflowAreas& aOverflowAreas,
                                      const nsReflowStatus& aReflowStatus) {
  // Compute collapsed block-end margin value.
  WritingMode parentWM = mMetrics.GetWritingMode();

  // Don't apply the block-end margin if the block has a *later* sibling across
  // column-span split.
  if (aReflowStatus.IsComplete() && !mFrame->HasColumnSpanSiblings()) {
    aBEndMarginResult = mMetrics.mCarriedOutBEndMargin;
    aBEndMarginResult.Include(
        aReflowInput.ComputedLogicalMargin(parentWM).BEnd(parentWM));
  } else {
    // The used block-end-margin is set to zero before a break.
    aBEndMarginResult.Zero();
  }

  nscoord backupContainingBlockAdvance = 0;

  // Check whether the block's block-end margin collapses with its block-start
  // margin. See CSS 2.1 section 8.3.1; those rules seem to match
  // nsBlockFrame::IsEmpty(). Any such block must have zero block-size so
  // check that first. Note that a block can have clearance and still
  // have adjoining block-start/end margins, because the clearance goes
  // above the block-start margin.
  // Mark the frame as non-dirty; it has been reflowed (or we wouldn't
  // be here), and we don't want to assert in CachedIsEmpty()
  mFrame->RemoveStateBits(NS_FRAME_IS_DIRTY);
  bool empty = 0 == mMetrics.BSize(parentWM) && aLine->CachedIsEmpty();
  if (empty) {
    // Collapse the block-end margin with the block-start margin that was
    // already applied.
    aBEndMarginResult.Include(mBStartMargin);

#ifdef NOISY_BLOCK_DIR_MARGINS
    printf("  ");
    mOuterReflowInput.mFrame->ListTag(stdout);
    printf(": ");
    mFrame->ListTag(stdout);
    printf(
        " -- collapsing block start & end margin together; BStart=%d "
        "spaceBStart=%d\n",
        mBCoord, mSpace.BStart(mWritingMode));
#endif
    // Section 8.3.1 of CSS 2.1 says that blocks with adjoining
    // "top/bottom" (i.e. block-start/end) margins whose top margin collapses
    // with their parent's top margin should have their top border-edge at the
    // top border-edge of their parent. We actually don't have to do
    // anything special to make this happen. In that situation,
    // nsBlockFrame::ShouldApplyBStartMargin will have returned false,
    // and mBStartMargin and aClearance will have been zero in
    // ReflowBlock.

    // If we did apply our block-start margin, but now we're collapsing it
    // into the block-end margin, we need to back up the containing
    // block's bCoord-advance by our block-start margin so that it doesn't get
    // counted twice. Note that here we're allowing the line's bounds
    // to become different from the block's position; we do this
    // because the containing block will place the next line at the
    // line's BEnd, and it must place the next line at a different
    // point from where this empty block will be.
    backupContainingBlockAdvance = mBStartMargin.Get();
  }

  // See if the frame fit. If it's the first frame or empty then it
  // always fits. If the block-size is unconstrained then it always fits,
  // even if there's some sort of integer overflow that makes bCoord +
  // mMetrics.BSize() appear to go beyond the available block size.
  const nscoord fragmentBStart = mBCoord - backupContainingBlockAdvance;
  if (!empty && !aForceFit &&
      mSpace.BSize(mWritingMode) != NS_UNCONSTRAINEDSIZE) {
    const nscoord bSize = mMetrics.BSize(mWritingMode);
    const nscoord bEnd = fragmentBStart + bSize;
    if (bEnd > mSpace.BEnd(mWritingMode)) {
      // didn't fit, we must acquit.
      mFrame->DidReflow(mPresContext, &aReflowInput);
      return false;
    }
    if (bSize == 0 && aReflowStatus.IsIncomplete() &&
        fragmentBStart == mSpace.BStart(mWritingMode) &&
        !mFrame->HasAbsolutelyPositionedChildren()) {
      // Reject a zero-height fragment of an incomplete block when it is the
      // first content in the fragmentainer. Per CSS Fragmentation spec, a
      // Class C break point exists only when there is a non-zero gap between
      // the content edge of a block container box and the outer edges of its
      // child content. If the fragment is at the very start of the available
      // space (no preceding siblings, no clearance gap, no margin gap), there
      // is no such gap, so we push the block to the next fragmentainer.
      // Exception: keep a zero-height fragment that serves as the containing
      // block origin for absolutely-positioned children.
      // https://www.w3.org/TR/css-break-3/#end-block
      mFrame->DidReflow(mPresContext, &aReflowInput);
      return false;
    }
  }

  aLine->SetBounds(mWritingMode, mICoord, fragmentBStart,
                   mMetrics.ISize(mWritingMode), mMetrics.BSize(mWritingMode),
                   mContainerSize);

  // Now place the frame and complete the reflow process
  nsContainerFrame::FinishReflowChild(
      mFrame, mPresContext, mMetrics, &aReflowInput, mWritingMode,
      LogicalPoint(mWritingMode, mICoord, mBCoord), mContainerSize,
      nsIFrame::ReflowChildFlags::ApplyRelativePositioning);

  aOverflowAreas = mMetrics.mOverflowAreas + mFrame->GetPosition();

  return true;
}
