/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* struct containing the input to nsIFrame::Reflow */

#include "mozilla/ReflowInput.h"

#include <algorithm>

#include "CounterStyleManager.h"
#include "LayoutLogging.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/WritingModes.h"
#include "nsBlockFrame.h"
#include "nsFlexContainerFrame.h"
#include "nsFontInflationData.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsGridContainerFrame.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsImageFrame.h"
#include "nsIPercentBSizeObserver.h"
#include "nsLayoutUtils.h"
#include "nsLineBox.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsTableFrame.h"
#include "StickyScrollContainer.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::layout;

static bool CheckNextInFlowParenthood(nsIFrame* aFrame, nsIFrame* aParent) {
  nsIFrame* frameNext = aFrame->GetNextInFlow();
  nsIFrame* parentNext = aParent->GetNextInFlow();
  return frameNext && parentNext && frameNext->GetParent() == parentNext;
}

/**
 * Adjusts the margin for a list (ol, ul), if necessary, depending on
 * font inflation settings. Unfortunately, because bullets from a list are
 * placed in the margin area, we only have ~40px in which to place the
 * bullets. When they are inflated, however, this causes problems, since
 * the text takes up more space than is available in the margin.
 *
 * This method will return a small amount (in app units) by which the
 * margin can be adjusted, so that the space is available for list
 * bullets to be rendered with font inflation enabled.
 */
static nscoord FontSizeInflationListMarginAdjustment(const nsIFrame* aFrame) {
  // As an optimization we check this block frame specific bit up front before
  // we even check if the frame is a block frame. That's only valid so long as
  // we also have the `IsBlockFrameOrSubclass()` call below. Calling that is
  // expensive though, and we want to avoid it if we know `HasMarker()` would
  // return false.
  if (!aFrame->HasAnyStateBits(NS_BLOCK_HAS_MARKER)) {
    return 0;
  }

  // On desktop font inflation is disabled, so this will always early exit
  // quickly, but checking the frame state bit is still quicker then this call
  // and very likely to early exit on its own so we check this second.
  float inflation = nsLayoutUtils::FontSizeInflationFor(aFrame);
  if (inflation <= 1.0f) {
    return 0;
  }

  if (!aFrame->IsBlockFrameOrSubclass()) {
    return 0;
  }

  // We only want to adjust the margins if we're dealing with an ordered list.
  // We already checked this above.
  MOZ_ASSERT(static_cast<const nsBlockFrame*>(aFrame)->HasMarker());

  const auto* list = aFrame->StyleList();
  if (list->mListStyleType.IsNone()) {
    return 0;
  }

  // The HTML spec states that the default padding for ordered lists
  // begins at 40px, indicating that we have 40px of space to place a
  // bullet. When performing font inflation calculations, we add space
  // equivalent to this, but simply inflated at the same amount as the
  // text, in app units.
  auto margin = nsPresContext::CSSPixelsToAppUnits(40) * (inflation - 1);
  if (!list->mListStyleType.IsName()) {
    return margin;
  }

  nsAtom* type = list->mListStyleType.AsName().AsAtom();
  if (type != nsGkAtoms::disc && type != nsGkAtoms::circle &&
      type != nsGkAtoms::square && type != nsGkAtoms::disclosure_closed &&
      type != nsGkAtoms::disclosure_open) {
    return margin;
  }

  return 0;
}

SizeComputationInput::SizeComputationInput(nsIFrame* aFrame,
                                           gfxContext* aRenderingContext)
    : mFrame(aFrame),
      mRenderingContext(aRenderingContext),
      mWritingMode(aFrame->GetWritingMode()),
      mIsThemed(aFrame->IsThemed()),
      mComputedMargin(mWritingMode),
      mComputedBorderPadding(mWritingMode),
      mComputedPadding(mWritingMode) {
  MOZ_ASSERT(mFrame);
}

SizeComputationInput::SizeComputationInput(
    nsIFrame* aFrame, gfxContext* aRenderingContext,
    WritingMode aContainingBlockWritingMode, nscoord aContainingBlockISize,
    const Maybe<LogicalMargin>& aBorder, const Maybe<LogicalMargin>& aPadding)
    : SizeComputationInput(aFrame, aRenderingContext) {
  MOZ_ASSERT(!mFrame->IsTableColFrame());
  InitOffsets(aContainingBlockWritingMode, aContainingBlockISize,
              mFrame->Type(), {}, aBorder, aPadding);
}

// Initialize a <b>root</b> reflow input with a rendering context to
// use for measuring things.
ReflowInput::ReflowInput(nsPresContext* aPresContext, nsIFrame* aFrame,
                         gfxContext* aRenderingContext,
                         const LogicalSize& aAvailableSpace, InitFlags aFlags)
    : SizeComputationInput(aFrame, aRenderingContext),
      mAvailableSize(aAvailableSpace) {
  MOZ_ASSERT(aRenderingContext, "no rendering context");
  MOZ_ASSERT(aPresContext, "no pres context");
  MOZ_ASSERT(aFrame, "no frame");
  MOZ_ASSERT(aPresContext == aFrame->PresContext(), "wrong pres context");

  if (aFlags.contains(InitFlag::DummyParentReflowInput)) {
    mFlags.mDummyParentReflowInput = true;
  }
  if (aFlags.contains(InitFlag::StaticPosIsCBOrigin)) {
    mFlags.mStaticPosIsCBOrigin = true;
  }

  if (!aFlags.contains(InitFlag::CallerWillInit)) {
    Init(aPresContext);
  }
  // When we encounter a PageContent frame this will be set to true.
  mFlags.mCanHaveClassABreakpoints = false;
}

static nsSize GetICBSize(const nsPresContext* aPresContext,
                         const nsIFrame* aFrame) {
  if (!aPresContext->IsPaginated()) {
    return aPresContext->GetVisibleArea().Size();
  }
  for (const nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    if (f->IsPageContentFrame()) {
      return f->GetSize();
    }
  }
  return aPresContext->GetPageSize();
}

// Initialize a reflow input for a child frame's reflow. Some state
// is copied from the parent reflow input; the remaining state is
// computed.
ReflowInput::ReflowInput(nsPresContext* aPresContext,
                         const ReflowInput& aParentReflowInput,
                         nsIFrame* aFrame, const LogicalSize& aAvailableSpace,
                         const Maybe<LogicalSize>& aContainingBlockSize,
                         InitFlags aFlags,
                         const StyleSizeOverrides& aSizeOverrides,
                         ComputeSizeFlags aComputeSizeFlags)
    : SizeComputationInput(aFrame, aParentReflowInput.mRenderingContext),
      mParentReflowInput(&aParentReflowInput),
      mFloatManager(aParentReflowInput.mFloatManager),
      mLineLayout(mFrame->IsLineParticipant() ? aParentReflowInput.mLineLayout
                                              : nullptr),
      mBreakType(aParentReflowInput.mBreakType),
      mPercentBSizeObserver(
          (aParentReflowInput.mPercentBSizeObserver &&
           aParentReflowInput.mPercentBSizeObserver->NeedsToObserve(*this))
              ? aParentReflowInput.mPercentBSizeObserver
              : nullptr),
      mFlags(aParentReflowInput.mFlags),
      mStyleSizeOverrides(aSizeOverrides),
      mComputeSizeFlags(aComputeSizeFlags),
      mReflowDepth(aParentReflowInput.mReflowDepth + 1),
      mAvailableSize(aAvailableSpace) {
  MOZ_ASSERT(aPresContext, "no pres context");
  MOZ_ASSERT(aFrame, "no frame");
  MOZ_ASSERT(aPresContext == aFrame->PresContext(), "wrong pres context");
  MOZ_ASSERT(!mFlags.mSpecialBSizeReflow || !aFrame->IsSubtreeDirty(),
             "frame should be clean when getting special bsize reflow");

  if (mWritingMode.IsOrthogonalTo(mParentReflowInput->GetWritingMode())) {
    // If the block establishes an orthogonal flow, set up its AvailableISize
    // per https://drafts.csswg.org/css-writing-modes/#orthogonal-auto

    auto GetISizeConstraint = [this](const nsIFrame* aFrame,
                                     bool* aFixed = nullptr) -> nscoord {
      nscoord limit = NS_UNCONSTRAINEDSIZE;
      const auto* pos = aFrame->StylePosition();
      const auto positionProperty = aFrame->StyleDisplay()->mPosition;
      if (auto size = nsLayoutUtils::GetAbsoluteSize(
              *pos->ISize(mWritingMode, positionProperty))) {
        limit = size.value();
        if (aFixed) {
          *aFixed = true;
        }
      } else if (auto maxSize = nsLayoutUtils::GetAbsoluteSize(
                     *pos->MaxISize(mWritingMode, positionProperty))) {
        limit = maxSize.value();
      }
      if (limit != NS_UNCONSTRAINEDSIZE) {
        if (auto minSize = nsLayoutUtils::GetAbsoluteSize(
                *pos->MinISize(mWritingMode, positionProperty))) {
          limit = std::max(limit, minSize.value());
        }
      }
      return limit;
    };

    // See if the containing block has a fixed size we should respect:
    const nsIFrame* cb = mFrame->GetContainingBlock();
    bool isFixed = false;
    nscoord cbLimit = GetISizeConstraint(cb, &isFixed);
    if (isFixed) {
      SetAvailableISize(cbLimit);
    } else {
      // If the CB size wasn't fixed, we consider the nearest scroll container
      // and the ICB.

      nscoord scLimit = NS_UNCONSTRAINEDSIZE;
      // If the containing block was not a scroll container itself, look up the
      // parent chain for a scroller size that we should respect.
      // XXX Could maybe use nsLayoutUtils::GetNearestScrollContainerFrame here,
      // but unsure if we need the additional complexity it supports?
      if (!cb->IsScrollContainerFrame()) {
        for (const nsIFrame* p = mFrame->GetParent(); p; p = p->GetParent()) {
          if (p->IsScrollContainerFrame()) {
            scLimit = GetISizeConstraint(p);
            // Only the closest ancestor scroller is relevant, so quit as soon
            // as we've found one (whether or not it had fixed sizing).
            break;
          }
        }
      }

      LogicalSize icbSize(mWritingMode, GetICBSize(aPresContext, mFrame));
      nscoord icbLimit = icbSize.ISize(mWritingMode);

      SetAvailableISize(std::min(icbLimit, std::min(scLimit, cbLimit)));
    }
  }

  // Note: mFlags was initialized as a copy of aParentReflowInput.mFlags up in
  // this constructor's init list, so the only flags that we need to explicitly
  // initialize here are those that may need a value other than our parent's.
  mFlags.mNextInFlowUntouched =
      aParentReflowInput.mFlags.mNextInFlowUntouched &&
      CheckNextInFlowParenthood(aFrame, aParentReflowInput.mFrame);
  mFlags.mAssumingHScrollbar = mFlags.mAssumingVScrollbar = false;
  mFlags.mIsColumnBalancing = false;
  mFlags.mColumnSetWrapperHasNoBSizeLeft = false;
  mFlags.mTreatBSizeAsIndefinite = false;
  mFlags.mDummyParentReflowInput = false;
  mFlags.mStaticPosIsCBOrigin = aFlags.contains(InitFlag::StaticPosIsCBOrigin);
  mFlags.mIOffsetsNeedCSSAlign = mFlags.mBOffsetsNeedCSSAlign = false;

  // We don't want the mOrthogonalCellFinalReflow flag to be inherited; it's up
  // to the table row frame to set it for its direct children as needed.
  mFlags.mOrthogonalCellFinalReflow = false;

  // aPresContext->IsPaginated() and the named pages pref should have been
  // checked when constructing the root ReflowInput.
  if (aParentReflowInput.mFlags.mCanHaveClassABreakpoints) {
    MOZ_ASSERT(aPresContext->IsPaginated(),
               "mCanHaveClassABreakpoints set during non-paginated reflow.");
  }

  {
    switch (mFrame->Type()) {
      case LayoutFrameType::PageContent:
        // PageContent requires paginated reflow.
        MOZ_ASSERT(aPresContext->IsPaginated(),
                   "nsPageContentFrame should not be in non-paginated reflow");
        MOZ_ASSERT(!mFlags.mCanHaveClassABreakpoints,
                   "mFlags.mCanHaveClassABreakpoints should have been "
                   "initalized to false before we found nsPageContentFrame");
        mFlags.mCanHaveClassABreakpoints = true;
        break;
      case LayoutFrameType::Block:          // FALLTHROUGH
      case LayoutFrameType::Canvas:         // FALLTHROUGH
      case LayoutFrameType::FlexContainer:  // FALLTHROUGH
      case LayoutFrameType::GridContainer:
        if (mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
          // Never allow breakpoints inside of out-of-flow frames.
          mFlags.mCanHaveClassABreakpoints = false;
          break;
        }
        // This frame type can have class A breakpoints, inherit this flag
        // from the parent (this is done for all flags during construction).
        // This also includes Canvas frames, as each PageContent frame always
        // has exactly one child which is a Canvas frame.
        // Do NOT include the subclasses of BlockFrame here, as the ones for
        // which this could be applicable (ColumnSetWrapper and the MathML
        // frames) cannot have class A breakpoints.
        MOZ_ASSERT(mFlags.mCanHaveClassABreakpoints ==
                   aParentReflowInput.mFlags.mCanHaveClassABreakpoints);
        break;
      default:
        mFlags.mCanHaveClassABreakpoints = false;
        break;
    }
  }

  if (aFlags.contains(InitFlag::DummyParentReflowInput) ||
      (mParentReflowInput->mFlags.mDummyParentReflowInput &&
       mFrame->IsTableFrame())) {
    mFlags.mDummyParentReflowInput = true;
  }

  if (!aFlags.contains(InitFlag::CallerWillInit)) {
    Init(aPresContext, aContainingBlockSize);
  }
}

template <typename SizeOrMaxSize>
nscoord SizeComputationInput::ComputeISizeValue(
    const LogicalSize& aContainingBlockSize, StyleBoxSizing aBoxSizing,
    const SizeOrMaxSize& aSize) const {
  WritingMode wm = GetWritingMode();
  const auto borderPadding = ComputedLogicalBorderPadding(wm);
  const LogicalSize contentEdgeToBoxSizing =
      aBoxSizing == StyleBoxSizing::Border ? borderPadding.Size(wm)
                                           : LogicalSize(wm);
  const nscoord boxSizingToMarginEdgeISize =
      borderPadding.IStartEnd(wm) + ComputedLogicalMargin(wm).IStartEnd(wm) -
      contentEdgeToBoxSizing.ISize(wm);

  return mFrame
      ->ComputeISizeValue(mRenderingContext, wm, aContainingBlockSize,
                          contentEdgeToBoxSizing, boxSizingToMarginEdgeISize,
                          aSize,
                          *mFrame->StylePosition()->BSize(
                              wm, mFrame->StyleDisplay()->mPosition),
                          mFrame->GetAspectRatio())
      .mISize;
}

template <typename SizeOrMaxSize>
nscoord SizeComputationInput::ComputeBSizeValueHandlingStretch(
    nscoord aContainingBlockBSize, StyleBoxSizing aBoxSizing,
    const SizeOrMaxSize& aSize) const {
  if (aSize.BehavesLikeStretchOnBlockAxis()) {
    WritingMode wm = GetWritingMode();
    return nsLayoutUtils::ComputeStretchContentBoxBSize(
        aContainingBlockBSize, ComputedLogicalMargin(wm).Size(wm).BSize(wm),
        ComputedLogicalBorderPadding(wm).Size(wm).BSize(wm));
  }
  return ComputeBSizeValue(aContainingBlockBSize, aBoxSizing,
                           aSize.AsLengthPercentage());
}

nscoord SizeComputationInput::ComputeBSizeValue(
    nscoord aContainingBlockBSize, StyleBoxSizing aBoxSizing,
    const LengthPercentage& aSize) const {
  WritingMode wm = GetWritingMode();
  nscoord inside = 0;
  if (aBoxSizing == StyleBoxSizing::Border) {
    inside = ComputedLogicalBorderPadding(wm).BStartEnd(wm);
  }
  return nsLayoutUtils::ComputeBSizeValue(aContainingBlockBSize, inside, aSize);
}

WritingMode ReflowInput::GetCBWritingMode() const {
  return mCBReflowInput ? mCBReflowInput->GetWritingMode()
                        : mFrame->GetContainingBlock()->GetWritingMode();
}

nsSize ReflowInput::ComputedSizeAsContainerIfConstrained() const {
  LogicalSize size = ComputedSize();
  if (size.ISize(mWritingMode) == NS_UNCONSTRAINEDSIZE) {
    size.ISize(mWritingMode) = 0;
  } else {
    size.ISize(mWritingMode) += mComputedBorderPadding.IStartEnd(mWritingMode);
  }
  if (size.BSize(mWritingMode) == NS_UNCONSTRAINEDSIZE) {
    size.BSize(mWritingMode) = 0;
  } else {
    size.BSize(mWritingMode) += mComputedBorderPadding.BStartEnd(mWritingMode);
  }
  return size.GetPhysicalSize(mWritingMode);
}

bool ReflowInput::ShouldReflowAllKids() const {
  // Note that we could make a stronger optimization for IsBResize if
  // we use it in a ShouldReflowChild test that replaces the current
  // checks of NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN, if it
  // were tested there along with NS_FRAME_CONTAINS_RELATIVE_BSIZE.
  // This would need to be combined with a slight change in which
  // frames NS_FRAME_CONTAINS_RELATIVE_BSIZE is marked on.
  return mFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY) || IsIResize() ||
         (IsBResize() &&
          mFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) ||
         mFlags.mIsInLastColumnBalancingReflow;
}

void ReflowInput::SetComputedISize(nscoord aComputedISize,
                                   ResetResizeFlags aFlags) {
  // It'd be nice to assert that |frame| is not in reflow, but this fails
  // because viewport frames reset the computed isize on a copy of their reflow
  // input when reflowing fixed-pos kids.  In that case we actually don't want
  // to mess with the resize flags, because comparing the frame's rect to the
  // munged computed isize is pointless.
  NS_WARNING_ASSERTION(aComputedISize >= 0, "Invalid computed inline-size!");
  if (ComputedISize() != aComputedISize) {
    mComputedSize.ISize(mWritingMode) = std::max(0, aComputedISize);
    if (aFlags == ResetResizeFlags::Yes) {
      InitResizeFlags(mFrame->PresContext(), mFrame->Type());
    }
  }
}

void ReflowInput::SetComputedBSize(nscoord aComputedBSize,
                                   ResetResizeFlags aFlags) {
  // It'd be nice to assert that |frame| is not in reflow, but this fails
  // for the same reason as above.
  NS_WARNING_ASSERTION(aComputedBSize >= 0, "Invalid computed block-size!");
  if (ComputedBSize() != aComputedBSize) {
    mComputedSize.BSize(mWritingMode) = std::max(0, aComputedBSize);
    if (aFlags == ResetResizeFlags::Yes) {
      InitResizeFlags(mFrame->PresContext(), mFrame->Type());
    }
  }
}

void ReflowInput::Init(nsPresContext* aPresContext,
                       const Maybe<LogicalSize>& aContainingBlockSize,
                       const Maybe<LogicalMargin>& aBorder,
                       const Maybe<LogicalMargin>& aPadding) {
  LAYOUT_WARN_IF_FALSE(AvailableISize() != NS_UNCONSTRAINEDSIZE,
                       "have unconstrained inline-size; this should only "
                       "result from very large sizes, not attempts at "
                       "intrinsic inline-size calculation");

  mStylePosition = mFrame->StylePosition();
  mStyleDisplay = mFrame->StyleDisplay();
  mStyleBorder = mFrame->StyleBorder();
  mStyleMargin = mFrame->StyleMargin();

  InitCBReflowInput();

  LayoutFrameType type = mFrame->Type();
  if (type == LayoutFrameType::Placeholder) {
    // Placeholders have a no-op Reflow method that doesn't need the rest of
    // this initialization, so we bail out early.
    mComputedSize.SizeTo(mWritingMode, 0, 0);
    return;
  }

  mFlags.mIsReplaced = mFrame->IsReplaced();

  InitConstraints(aPresContext, aContainingBlockSize, aBorder, aPadding, type);

  InitResizeFlags(aPresContext, type);
  InitDynamicReflowRoot();

  nsIFrame* parent = mFrame->GetParent();
  if (parent && parent->HasAnyStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE) &&
      !(parent->IsScrollContainerFrame() &&
        parent->StyleDisplay()->mOverflowY != StyleOverflow::Hidden)) {
    mFrame->AddStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
  } else if (type == LayoutFrameType::SVGForeignObject) {
    // An SVG foreignObject frame is inherently constrained block-size.
    mFrame->AddStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
  } else {
    const auto positionProperty = mStyleDisplay->mPosition;
    const auto bSizeCoord =
        mStylePosition->BSize(mWritingMode, positionProperty);
    const auto maxBSizeCoord =
        mStylePosition->MaxBSize(mWritingMode, positionProperty);
    if ((!bSizeCoord->BehavesLikeInitialValueOnBlockAxis() ||
         !maxBSizeCoord->BehavesLikeInitialValueOnBlockAxis()) &&
        // Don't set NS_FRAME_IN_CONSTRAINED_BSIZE on body or html elements.
        (mFrame->GetContent() && !(mFrame->GetContent()->IsAnyOfHTMLElements(
                                     nsGkAtoms::body, nsGkAtoms::html)))) {
      // If our block-size was specified as a percentage, then this could
      // actually resolve to 'auto', based on:
      // http://www.w3.org/TR/CSS21/visudet.html#the-height-property
      nsIFrame* containingBlk = mFrame;
      while (containingBlk) {
        const nsStylePosition* stylePos = containingBlk->StylePosition();
        const auto containingBlkPositionProperty =
            containingBlk->StyleDisplay()->mPosition;
        const auto bSizeCoord =
            stylePos->BSize(mWritingMode, containingBlkPositionProperty);
        const auto& maxBSizeCoord =
            stylePos->MaxBSize(mWritingMode, containingBlkPositionProperty);
        if ((bSizeCoord->IsLengthPercentage() && !bSizeCoord->HasPercent()) ||
            (maxBSizeCoord->IsLengthPercentage() &&
             !maxBSizeCoord->HasPercent())) {
          mFrame->AddStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
          break;
        } else if (bSizeCoord->HasPercent() || maxBSizeCoord->HasPercent()) {
          if (!(containingBlk = containingBlk->GetContainingBlock())) {
            // If we've reached the top of the tree, then we don't have
            // a constrained block-size.
            mFrame->RemoveStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
            break;
          }

          continue;
        } else {
          mFrame->RemoveStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
          break;
        }
      }
    } else {
      mFrame->RemoveStateBits(NS_FRAME_IN_CONSTRAINED_BSIZE);
    }
  }

  if (mParentReflowInput &&
      mParentReflowInput->GetWritingMode().IsOrthogonalTo(mWritingMode)) {
    // Orthogonal frames are always reflowed with an unconstrained
    // dimension to avoid incomplete reflow across an orthogonal
    // boundary. Normally this is the block-size, but for column sets
    // with auto-height it's the inline-size, so that they can add
    // columns in the container's block direction
    if (type == LayoutFrameType::ColumnSet &&
        mStylePosition->ISize(mWritingMode, mStyleDisplay->mPosition)
            ->IsAuto()) {
      SetComputedISize(NS_UNCONSTRAINEDSIZE, ResetResizeFlags::No);
    } else {
      SetAvailableBSize(NS_UNCONSTRAINEDSIZE);
    }
  }

  if (mFrame->GetContainSizeAxes().mBContained) {
    // In the case that a box is size contained in block axis, we want to ensure
    // that it is also monolithic. We do this by setting AvailableBSize() to an
    // unconstrained size to avoid fragmentation.
    SetAvailableBSize(NS_UNCONSTRAINEDSIZE);
  }

  LAYOUT_WARN_IF_FALSE(
      (mStyleDisplay->IsInlineOutsideStyle() && !mFrame->IsReplaced()) ||
          type == LayoutFrameType::Text ||
          ComputedISize() != NS_UNCONSTRAINEDSIZE,
      "have unconstrained inline-size; this should only "
      "result from very large sizes, not attempts at "
      "intrinsic inline-size calculation");
}

static bool MightBeContainingBlockFor(nsIFrame* aMaybeContainingBlock,
                                      nsIFrame* aFrame,
                                      const nsStyleDisplay* aStyleDisplay) {
  // Keep this in sync with nsIFrame::GetContainingBlock.
  if (aFrame->IsAbsolutelyPositioned(aStyleDisplay) &&
      aMaybeContainingBlock == aFrame->GetParent()) {
    return true;
  }
  return aMaybeContainingBlock->IsBlockContainer();
}

void ReflowInput::InitCBReflowInput() {
  if (!mParentReflowInput) {
    mCBReflowInput = nullptr;
    return;
  }
  if (mParentReflowInput->mFlags.mDummyParentReflowInput) {
    mCBReflowInput = mParentReflowInput;
    return;
  }

  // To avoid a long walk up the frame tree check if the parent frame can be a
  // containing block for mFrame.
  if (MightBeContainingBlockFor(mParentReflowInput->mFrame, mFrame,
                                mStyleDisplay) &&
      mParentReflowInput->mFrame ==
          mFrame->GetContainingBlock(0, mStyleDisplay)) {
    // Inner table frames need to use the containing block of the outer
    // table frame.
    if (mFrame->IsTableFrame()) {
      mCBReflowInput = mParentReflowInput->mCBReflowInput;
    } else {
      mCBReflowInput = mParentReflowInput;
    }
  } else {
    mCBReflowInput = mParentReflowInput->mCBReflowInput;
  }
}

/* Check whether CalcQuirkContainingBlockHeight would stop on the
 * given reflow input, using its block as a height.  (essentially
 * returns false for any case in which CalcQuirkContainingBlockHeight
 * has a "continue" in its main loop.)
 *
 * XXX Maybe refactor CalcQuirkContainingBlockHeight so it uses
 * this function as well
 */
static bool IsQuirkContainingBlockHeight(const ReflowInput* rs,
                                         LayoutFrameType aFrameType) {
  if (LayoutFrameType::Block == aFrameType ||
      LayoutFrameType::ScrollContainer == aFrameType) {
    // Note: This next condition could change due to a style change,
    // but that would cause a style reflow anyway, which means we're ok.
    if (NS_UNCONSTRAINEDSIZE == rs->ComputedHeight()) {
      if (!rs->mFrame->IsAbsolutelyPositioned(rs->mStyleDisplay)) {
        return false;
      }
    }
  }
  return true;
}

void ReflowInput::InitResizeFlags(nsPresContext* aPresContext,
                                  LayoutFrameType aFrameType) {
  SetIResize(false);
  SetBResize(false);
  SetBResizeForPercentages(false);

  const WritingMode wm = mWritingMode;  // just a shorthand
  // We should report that we have a resize in the inline dimension if
  // *either* the border-box size or the content-box size in that
  // dimension has changed.  It might not actually be necessary to do
  // this if the border-box size has changed and the content-box size
  // has not changed, but since we've historically used the flag to mean
  // border-box size change, continue to do that. It's possible for
  // the content-box size to change without a border-box size change or
  // a style change given (1) a fixed width (possibly fixed by max-width
  // or min-width), box-sizing:border-box, and percentage padding;
  // (2) box-sizing:content-box, M% width, and calc(Npx - M%) padding.
  //
  // However, we don't actually have the information at this point to tell
  // whether the content-box size has changed, since both style data and the
  // UsedPaddingProperty() have already been updated in
  // SizeComputationInput::InitOffsets(). So, we check the HasPaddingChange()
  // bit for the cases where it's possible for the content-box size to have
  // changed without either (a) a change in the border-box size or (b) an
  // nsChangeHint_NeedDirtyReflow change hint due to change in border or
  // padding.
  //
  // We don't clear the HasPaddingChange() bit here, since sometimes we
  // construct reflow input (e.g. in nsBlockFrame::ReflowBlockFrame to compute
  // margin collapsing) without reflowing the frame. Instead, we clear it in
  // nsIFrame::DidReflow().
  bool isIResize =
      // is the border-box resizing?
      mFrame->ISize(wm) !=
          ComputedISize() + ComputedLogicalBorderPadding(wm).IStartEnd(wm) ||
      // or is the content-box resizing?  (see comment above)
      mFrame->HasPaddingChange();

  if (mFrame->HasAnyStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT) &&
      nsLayoutUtils::FontSizeInflationEnabled(aPresContext)) {
    // Create our font inflation data if we don't have it already, and
    // give it our current width information.
    bool dirty = nsFontInflationData::UpdateFontInflationDataISizeFor(*this) &&
                 // Avoid running this at the box-to-block interface
                 // (where we shouldn't be inflating anyway, and where
                 // reflow input construction is probably to construct a
                 // dummy parent reflow input anyway).
                 !mFlags.mDummyParentReflowInput;

    if (dirty || (!mFrame->GetParent() && isIResize)) {
      // When font size inflation is enabled, a change in either:
      //  * the effective width of a font inflation flow root
      //  * the width of the frame
      // needs to cause a dirty reflow since they change the font size
      // inflation calculations, which in turn change the size of text,
      // line-heights, etc.  This is relatively similar to a classic
      // case of style change reflow, except that because inflation
      // doesn't affect the intrinsic sizing codepath, there's no need
      // to invalidate intrinsic sizes.
      //
      // Note that this makes horizontal resizing a good bit more
      // expensive.  However, font size inflation is targeted at a set of
      // devices (zoom-and-pan devices) where the main use case for
      // horizontal resizing needing to be efficient (window resizing) is
      // not present.  It does still increase the cost of dynamic changes
      // caused by script where a style or content change in one place
      // causes a resize in another (e.g., rebalancing a table).

      // FIXME: This isn't so great for the cases where
      // ReflowInput::SetComputedWidth is called, if the first time
      // we go through InitResizeFlags we set IsHResize() to true, and then
      // the second time we'd set it to false even without the
      // NS_FRAME_IS_DIRTY bit already set.
      if (mFrame->IsSVGForeignObjectFrame()) {
        // Foreign object frames use dirty bits in a special way.
        mFrame->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
        nsIFrame* kid = mFrame->PrincipalChildList().FirstChild();
        if (kid) {
          kid->MarkSubtreeDirty();
        }
      } else {
        mFrame->MarkSubtreeDirty();
      }

      // Mark intrinsic widths on all descendants dirty.  We need to do
      // this (1) since we're changing the size of text and need to
      // clear text runs on text frames and (2) since we actually are
      // changing some intrinsic widths, but only those that live inside
      // of containers.

      // It makes sense to do this for descendants but not ancestors
      // (which is unusual) because we're only changing the unusual
      // inflation-dependent intrinsic widths (i.e., ones computed with
      // nsPresContext::mInflationDisabledForShrinkWrap set to false),
      // which should never affect anything outside of their inflation
      // flow root (or, for that matter, even their inflation
      // container).

      // This is also different from what PresShell::FrameNeedsReflow
      // does because it doesn't go through placeholders.  It doesn't
      // need to because we're actually doing something that cares about
      // frame tree geometry (the width on an ancestor) rather than
      // style.

      AutoTArray<nsIFrame*, 32> stack;
      stack.AppendElement(mFrame);

      do {
        nsIFrame* f = stack.PopLastElement();
        for (const auto& childList : f->ChildLists()) {
          for (nsIFrame* kid : childList.mList) {
            kid->MarkIntrinsicISizesDirty();
            stack.AppendElement(kid);
          }
        }
      } while (stack.Length() != 0);
    }
  }

  SetIResize(!mFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY) && isIResize);
  const auto positionProperty = mStyleDisplay->mPosition;

  const auto bSize = mStylePosition->BSize(wm, positionProperty);
  const auto minBSize = mStylePosition->MinBSize(wm, positionProperty);
  const auto maxBSize = mStylePosition->MaxBSize(wm, positionProperty);
  // XXX Should we really need to null check mCBReflowInput?  (We do for
  // at least nsBoxFrame).
  if (mFrame->HasBSizeChange()) {
    // When we have an nsChangeHint_UpdateComputedBSize, we'll set a bit
    // on the frame to indicate we're resizing.  This might catch cases,
    // such as a change between auto and a length, where the box doesn't
    // actually resize but children with percentages resize (since those
    // percentages become auto if their containing block is auto).
    SetBResize(true);
    SetBResizeForPercentages(true);
    // We don't clear the HasBSizeChange state here, since sometimes we
    // construct a ReflowInput (e.g. in nsBlockFrame::ReflowBlockFrame to
    // compute margin collapsing) without reflowing the frame. Instead, we
    // clear it in nsIFrame::DidReflow.
  } else if (mCBReflowInput &&
             mCBReflowInput->IsBResizeForPercentagesForWM(wm) &&
             (bSize->HasPercent() || minBSize->HasPercent() ||
              maxBSize->HasPercent())) {
    // We have a percentage (or calc-with-percentage) block-size, and the
    // value it's relative to has changed.
    SetBResize(true);
    SetBResizeForPercentages(true);
  } else if (aFrameType == LayoutFrameType::TableCell &&
             (mFlags.mSpecialBSizeReflow ||
              mFrame->FirstInFlow()->HasAnyStateBits(
                  NS_TABLE_CELL_HAD_SPECIAL_REFLOW)) &&
             mFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
    // Need to set the bit on the cell so that
    // mCBReflowInput->IsBResize() is set correctly below when
    // reflowing descendant.
    SetBResize(true);
    SetBResizeForPercentages(true);
  } else if (mCBReflowInput && mFrame->IsBlockWrapper()) {
    // XXX Is this problematic for relatively positioned inlines acting
    // as containing block for absolutely positioned elements?
    // Possibly; in that case we should at least be checking
    // IsSubtreeDirty(), I'd think.
    SetBResize(mCBReflowInput->IsBResizeForWM(wm));
    SetBResizeForPercentages(mCBReflowInput->IsBResizeForPercentagesForWM(wm));
  } else if (ComputedBSize() == NS_UNCONSTRAINEDSIZE) {
    // We have an 'auto' block-size.
    if (eCompatibility_NavQuirks == aPresContext->CompatibilityMode() &&
        mCBReflowInput) {
      // FIXME: This should probably also check IsIResize().
      SetBResize(mCBReflowInput->IsBResizeForWM(wm));
    } else {
      SetBResize(IsIResize());
    }
    SetBResize(IsBResize() || mFrame->IsSubtreeDirty());
  } else {
    // We have a non-'auto' block-size, i.e., a length.  Set the BResize
    // flag to whether the size is actually different.
    SetBResize(mFrame->BSize(wm) !=
               ComputedBSize() +
                   ComputedLogicalBorderPadding(wm).BStartEnd(wm));
  }

  bool dependsOnCBBSize =
      (nsStylePosition::BSizeDependsOnContainer(bSize) &&
       // FIXME: condition this on not-abspos?
       !bSize->IsAuto()) ||
      nsStylePosition::MinBSizeDependsOnContainer(minBSize) ||
      nsStylePosition::MaxBSizeDependsOnContainer(maxBSize) ||
      mStylePosition
          ->GetAnchorResolvedInset(LogicalSide::BStart, wm, positionProperty)
          ->HasPercent() ||
      !mStylePosition
           ->GetAnchorResolvedInset(LogicalSide::BEnd, wm, positionProperty)
           ->IsAuto();

  // If mFrame is a flex item, and mFrame's block axis is the flex container's
  // main axis (e.g. in a column-oriented flex container with same
  // writing-mode), then its block-size depends on its CB size, if its
  // flex-basis has a percentage.
  if (mFrame->IsFlexItem() &&
      !nsFlexContainerFrame::IsItemInlineAxisMainAxis(mFrame)) {
    const auto& flexBasis = mStylePosition->mFlexBasis;
    dependsOnCBBSize |= (flexBasis.IsSize() && flexBasis.AsSize().HasPercent());
  }

  if (mFrame->StyleFont()->mLineHeight.IsMozBlockHeight()) {
    // line-height depends on block bsize
    mFrame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
    // but only on containing blocks if this frame is not a suitable block
    dependsOnCBBSize |= !nsLayoutUtils::IsNonWrapperBlock(mFrame);
  }

  // If we're the descendant of a table cell that performs special bsize
  // reflows and we could be the child that requires them, always set
  // the block-axis resize in case this is the first pass before the
  // special bsize reflow.  However, don't do this if it actually is
  // the special bsize reflow, since in that case it will already be
  // set correctly above if we need it set.
  if (!IsBResize() && mCBReflowInput &&
      (mCBReflowInput->mFrame->IsTableCellFrame() ||
       mCBReflowInput->mFlags.mHeightDependsOnAncestorCell) &&
      !mCBReflowInput->mFlags.mSpecialBSizeReflow && dependsOnCBBSize) {
    SetBResize(true);
    mFlags.mHeightDependsOnAncestorCell = true;
  }

  // Set NS_FRAME_CONTAINS_RELATIVE_BSIZE if it's needed.

  // It would be nice to check that |ComputedBSize != NS_UNCONSTRAINEDSIZE|
  // &&ed with the percentage bsize check.  However, this doesn't get
  // along with table special bsize reflows, since a special bsize
  // reflow (a quirk that makes such percentage height work on children
  // of table cells) can cause not just a single percentage height to
  // become fixed, but an entire descendant chain of percentage height
  // to become fixed.
  if (dependsOnCBBSize && mCBReflowInput) {
    const ReflowInput* rs = this;
    bool hitCBReflowInput = false;
    do {
      rs = rs->mParentReflowInput;
      if (!rs) {
        break;
      }

      if (rs->mFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
        break;  // no need to go further
      }
      rs->mFrame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);

      // Keep track of whether we've hit the containing block, because
      // we need to go at least that far.
      if (rs == mCBReflowInput) {
        hitCBReflowInput = true;
      }

      // XXX What about orthogonal flows? It doesn't make sense to
      // keep propagating this bit across an orthogonal boundary,
      // where the meaning of BSize changes. Bug 1175517.
    } while (!hitCBReflowInput ||
             (eCompatibility_NavQuirks == aPresContext->CompatibilityMode() &&
              !IsQuirkContainingBlockHeight(rs, rs->mFrame->Type())));
    // Note: We actually don't need to set the
    // NS_FRAME_CONTAINS_RELATIVE_BSIZE bit for the cases
    // where we hit the early break statements in
    // CalcQuirkContainingBlockHeight. But it doesn't hurt
    // us to set the bit in these cases.
  }
  if (mFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    // If we're reflowing everything, then we'll find out if we need
    // to re-set this.
    mFrame->RemoveStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
  }
}

void ReflowInput::InitDynamicReflowRoot() {
  if (mFrame->CanBeDynamicReflowRoot()) {
    mFrame->AddStateBits(NS_FRAME_DYNAMIC_REFLOW_ROOT);
  } else {
    mFrame->RemoveStateBits(NS_FRAME_DYNAMIC_REFLOW_ROOT);
  }
}

bool ReflowInput::ShouldApplyAutomaticMinimumOnBlockAxis() const {
  MOZ_ASSERT(!mFrame->HasReplacedSizing());
  return mFlags.mIsBSizeSetByAspectRatio &&
         !mStyleDisplay->IsScrollableOverflow() &&
         mStylePosition->MinBSize(GetWritingMode(), mStyleDisplay->mPosition)
             ->IsAuto();
}

bool ReflowInput::IsInFragmentedContext() const {
  // We consider mFrame with a prev-in-flow being in a fragmented context
  // because nsColumnSetFrame can reflow its last column with an unconstrained
  // available block-size.
  return AvailableBSize() != NS_UNCONSTRAINEDSIZE || mFrame->GetPrevInFlow();
}

/* static */
LogicalMargin ReflowInput::ComputeRelativeOffsets(WritingMode aWM,
                                                  nsIFrame* aFrame,
                                                  const LogicalSize& aCBSize) {
  // In relative positioning, anchor functions are always invalid;
  // anchor-resolved insets should no longer contain any reference to anchor
  // functions.
  LogicalMargin offsets(aWM);
  const nsStylePosition* position = aFrame->StylePosition();
  const auto positionProperty = aFrame->StyleDisplay()->mPosition;

  // Compute the 'inlineStart' and 'inlineEnd' values. 'inlineStart'
  // moves the boxes to the end of the line, and 'inlineEnd' moves the
  // boxes to the start of the line. The computed values are always:
  // inlineStart=-inlineEnd
  const auto inlineStart = position->GetAnchorResolvedInset(
      LogicalSide::IStart, aWM, positionProperty);
  const auto inlineEnd = position->GetAnchorResolvedInset(
      LogicalSide::IEnd, aWM, positionProperty);
  bool inlineStartIsAuto = inlineStart->IsAuto();
  bool inlineEndIsAuto = inlineEnd->IsAuto();

  // If neither 'inlineStart' nor 'inlineEnd' is auto, then we're
  // over-constrained and we ignore one of them
  if (!inlineStartIsAuto && !inlineEndIsAuto) {
    inlineEndIsAuto = true;
  }

  if (inlineStartIsAuto) {
    if (inlineEndIsAuto) {
      // If both are 'auto' (their initial values), the computed values are 0
      offsets.IStart(aWM) = offsets.IEnd(aWM) = 0;
    } else {
      // 'inlineEnd' isn't being treated as 'auto' so compute its value
      offsets.IEnd(aWM) = inlineEnd->IsAuto()
                              ? 0
                              : nsLayoutUtils::ComputeCBDependentValue(
                                    aCBSize.ISize(aWM), inlineEnd);

      // Computed value for 'inlineStart' is minus the value of 'inlineEnd'
      offsets.IStart(aWM) = -offsets.IEnd(aWM);
    }

  } else {
    NS_ASSERTION(inlineEndIsAuto, "unexpected specified constraint");

    // 'InlineStart' isn't 'auto' so compute its value
    offsets.IStart(aWM) =
        nsLayoutUtils::ComputeCBDependentValue(aCBSize.ISize(aWM), inlineStart);

    // Computed value for 'inlineEnd' is minus the value of 'inlineStart'
    offsets.IEnd(aWM) = -offsets.IStart(aWM);
  }

  // Compute the 'blockStart' and 'blockEnd' values. The 'blockStart'
  // and 'blockEnd' properties move relatively positioned elements in
  // the block progression direction. They also must be each other's
  // negative
  const auto blockStart = position->GetAnchorResolvedInset(
      LogicalSide::BStart, aWM, positionProperty);
  const auto blockEnd = position->GetAnchorResolvedInset(LogicalSide::BEnd, aWM,
                                                         positionProperty);
  bool blockStartIsAuto = blockStart->IsAuto();
  bool blockEndIsAuto = blockEnd->IsAuto();

  // Check for percentage based values and a containing block block-size
  // that depends on the content block-size. Treat them like 'auto'
  if (NS_UNCONSTRAINEDSIZE == aCBSize.BSize(aWM)) {
    if (blockStart->HasPercent()) {
      blockStartIsAuto = true;
    }
    if (blockEnd->HasPercent()) {
      blockEndIsAuto = true;
    }
  }

  // If neither is 'auto', 'block-end' is ignored
  if (!blockStartIsAuto && !blockEndIsAuto) {
    blockEndIsAuto = true;
  }

  if (blockStartIsAuto) {
    if (blockEndIsAuto) {
      // If both are 'auto' (their initial values), the computed values are 0
      offsets.BStart(aWM) = offsets.BEnd(aWM) = 0;
    } else {
      // 'blockEnd' isn't being treated as 'auto' so compute its value
      offsets.BEnd(aWM) = blockEnd->IsAuto()
                              ? 0
                              : nsLayoutUtils::ComputeCBDependentValue(
                                    aCBSize.BSize(aWM), blockEnd);

      // Computed value for 'blockStart' is minus the value of 'blockEnd'
      offsets.BStart(aWM) = -offsets.BEnd(aWM);
    }

  } else {
    NS_ASSERTION(blockEndIsAuto, "unexpected specified constraint");

    // 'blockStart' isn't 'auto' so compute its value
    offsets.BStart(aWM) =
        nsLayoutUtils::ComputeCBDependentValue(aCBSize.BSize(aWM), blockStart);

    // Computed value for 'blockEnd' is minus the value of 'blockStart'
    offsets.BEnd(aWM) = -offsets.BStart(aWM);
  }

  // Convert the offsets to physical coordinates and store them on the frame
  const nsMargin physicalOffsets = offsets.GetPhysicalMargin(aWM);
  if (nsMargin* prop =
          aFrame->GetProperty(nsIFrame::ComputedOffsetProperty())) {
    *prop = physicalOffsets;
  } else {
    aFrame->AddProperty(nsIFrame::ComputedOffsetProperty(),
                        new nsMargin(physicalOffsets));
  }

  NS_ASSERTION(offsets.IStart(aWM) == -offsets.IEnd(aWM) &&
                   offsets.BStart(aWM) == -offsets.BEnd(aWM),
               "ComputeRelativeOffsets should return valid results!");

  return offsets;
}

/* static */
void ReflowInput::ApplyRelativePositioning(nsIFrame* aFrame,
                                           const nsMargin& aComputedOffsets,
                                           nsPoint* aPosition) {
  if (!aFrame->IsRelativelyOrStickyPositioned()) {
    NS_ASSERTION(!aFrame->HasProperty(nsIFrame::NormalPositionProperty()),
                 "We assume that changing the 'position' property causes "
                 "frame reconstruction.  If that ever changes, this code "
                 "should call "
                 "aFrame->RemoveProperty(nsIFrame::NormalPositionProperty())");
    return;
  }

  // Store the normal position
  aFrame->SetProperty(nsIFrame::NormalPositionProperty(), *aPosition);

  const nsStyleDisplay* display = aFrame->StyleDisplay();
  if (StylePositionProperty::Relative == display->mPosition) {
    *aPosition += nsPoint(aComputedOffsets.left, aComputedOffsets.top);
  }
  // For sticky positioned elements, we'll leave them until the scroll container
  // reflows and calls StickyScrollContainer::UpdatePositions() to update their
  // positions.
}

// static
void ReflowInput::ComputeAbsPosInlineAutoMargin(nscoord aAvailMarginSpace,
                                                WritingMode aContainingBlockWM,
                                                bool aIsMarginIStartAuto,
                                                bool aIsMarginIEndAuto,
                                                LogicalMargin& aMargin,
                                                LogicalMargin& aOffsets) {
  if (aIsMarginIStartAuto) {
    if (aIsMarginIEndAuto) {
      if (aAvailMarginSpace < 0) {
        // Note that this case is different from the neither-'auto'
        // case below, where the spec says to ignore 'left'/'right'.
        // Ignore the specified value for 'margin-right'.
        aMargin.IEnd(aContainingBlockWM) = aAvailMarginSpace;
      } else {
        // Both 'margin-left' and 'margin-right' are 'auto', so they get
        // equal values
        aMargin.IStart(aContainingBlockWM) = aAvailMarginSpace / 2;
        aMargin.IEnd(aContainingBlockWM) =
            aAvailMarginSpace - aMargin.IStart(aContainingBlockWM);
      }
    } else {
      // Just 'margin-left' is 'auto'
      aMargin.IStart(aContainingBlockWM) = aAvailMarginSpace;
    }
  } else {
    if (aIsMarginIEndAuto) {
      // Just 'margin-right' is 'auto'
      aMargin.IEnd(aContainingBlockWM) = aAvailMarginSpace;
    }
    // Else, both margins are non-auto. This margin box would align to the
    // inset-reduced containing block, so it's not overconstrained.
  }
}

// static
void ReflowInput::ComputeAbsPosBlockAutoMargin(nscoord aAvailMarginSpace,
                                               WritingMode aContainingBlockWM,
                                               bool aIsMarginBStartAuto,
                                               bool aIsMarginBEndAuto,
                                               LogicalMargin& aMargin,
                                               LogicalMargin& aOffsets) {
  if (aIsMarginBStartAuto) {
    if (aIsMarginBEndAuto) {
      // Both 'margin-top' and 'margin-bottom' are 'auto', so they get
      // equal values
      aMargin.BStart(aContainingBlockWM) = aAvailMarginSpace / 2;
      aMargin.BEnd(aContainingBlockWM) =
          aAvailMarginSpace - aMargin.BStart(aContainingBlockWM);
    } else {
      // Just margin-block-start is 'auto'
      aMargin.BStart(aContainingBlockWM) = aAvailMarginSpace;
    }
  } else {
    if (aIsMarginBEndAuto) {
      // Just margin-block-end is 'auto'
      aMargin.BEnd(aContainingBlockWM) = aAvailMarginSpace;
    }
    // Else, both margins are non-auto. See comment in the inline version.
  }
}

void ReflowInput::ApplyRelativePositioning(
    nsIFrame* aFrame, WritingMode aWritingMode,
    const LogicalMargin& aComputedOffsets, LogicalPoint* aPosition,
    const nsSize& aContainerSize) {
  // Subtract the size of the frame from the container size that we
  // use for converting between the logical and physical origins of
  // the frame. This accounts for the fact that logical origins in RTL
  // coordinate systems are at the top right of the frame instead of
  // the top left.
  nsSize frameSize = aFrame->GetSize();
  nsPoint pos =
      aPosition->GetPhysicalPoint(aWritingMode, aContainerSize - frameSize);
  ApplyRelativePositioning(
      aFrame, aComputedOffsets.GetPhysicalMargin(aWritingMode), &pos);
  *aPosition = LogicalPoint(aWritingMode, pos, aContainerSize - frameSize);
}

nsIFrame* ReflowInput::GetHypotheticalBoxContainer(nsIFrame* aFrame,
                                                   nscoord& aCBIStartEdge,
                                                   LogicalSize& aCBSize) const {
  aFrame = aFrame->GetContainingBlock();
  NS_ASSERTION(aFrame != mFrame, "How did that happen?");

  /* Now aFrame is the containing block we want */

  /* Check whether the containing block is currently being reflowed.
     If so, use the info from the reflow input. */
  const ReflowInput* reflowInput;
  if (aFrame->HasAnyStateBits(NS_FRAME_IN_REFLOW)) {
    for (reflowInput = mParentReflowInput;
         reflowInput && reflowInput->mFrame != aFrame;
         reflowInput = reflowInput->mParentReflowInput) {
      /* do nothing */
    }
  } else {
    reflowInput = nullptr;
  }

  if (reflowInput) {
    WritingMode wm = reflowInput->GetWritingMode();
    NS_ASSERTION(wm == aFrame->GetWritingMode(), "unexpected writing mode");
    aCBIStartEdge = reflowInput->ComputedLogicalBorderPadding(wm).IStart(wm);
    aCBSize = reflowInput->ComputedSize(wm);
  } else {
    /* Didn't find a reflow reflowInput for aFrame.  Just compute the
       information we want, on the assumption that aFrame already knows its
       size.  This really ought to be true by now. */
    NS_ASSERTION(!aFrame->HasAnyStateBits(NS_FRAME_IN_REFLOW),
                 "aFrame shouldn't be in reflow; we'll lie if it is");
    WritingMode wm = aFrame->GetWritingMode();
    // Compute CB's offset & content-box size by subtracting borderpadding from
    // frame size.
    const auto& bp = aFrame->GetLogicalUsedBorderAndPadding(wm);
    aCBIStartEdge = bp.IStart(wm);
    aCBSize = aFrame->GetLogicalSize(wm) - bp.Size(wm);
  }

  return aFrame;
}

struct nsHypotheticalPosition {
  // offset from inline-start edge of containing block (which is a padding edge)
  nscoord mIStart = 0;
  // offset from block-start edge of containing block (which is a padding edge)
  nscoord mBStart = 0;
  WritingMode mWritingMode;
};

/**
 * aInsideBoxSizing returns the part of the padding, border, and margin
 * in the aAxis dimension that goes inside the edge given by box-sizing;
 * aOutsideBoxSizing returns the rest.
 */
void ReflowInput::CalculateBorderPaddingMargin(
    LogicalAxis aAxis, nscoord aContainingBlockSize, nscoord* aInsideBoxSizing,
    nscoord* aOutsideBoxSizing) const {
  WritingMode wm = GetWritingMode();
  Side startSide = wm.PhysicalSide(MakeLogicalSide(aAxis, LogicalEdge::Start));
  Side endSide = wm.PhysicalSide(MakeLogicalSide(aAxis, LogicalEdge::End));

  nsMargin styleBorder = mStyleBorder->GetComputedBorder();
  nscoord borderStartEnd =
      styleBorder.Side(startSide) + styleBorder.Side(endSide);

  nscoord paddingStartEnd, marginStartEnd;

  // See if the style system can provide us the padding directly
  const auto* stylePadding = mFrame->StylePadding();
  if (nsMargin padding; stylePadding->GetPadding(padding)) {
    paddingStartEnd = padding.Side(startSide) + padding.Side(endSide);
  } else {
    // We have to compute the start and end values
    const nscoord start = nsLayoutUtils::ComputeCBDependentValue(
        aContainingBlockSize, stylePadding->mPadding.Get(startSide));
    const nscoord end = nsLayoutUtils::ComputeCBDependentValue(
        aContainingBlockSize, stylePadding->mPadding.Get(endSide));
    paddingStartEnd = start + end;
  }

  // See if the style system can provide us the margin directly
  if (nsMargin margin; mStyleMargin->GetMargin(margin)) {
    marginStartEnd = margin.Side(startSide) + margin.Side(endSide);
  } else {
    // If the margin is 'auto', ComputeCBDependentValue() will return 0. The
    // correct margin value will be computed later in InitAbsoluteConstraints
    // (which is caller of this function, via CalculateHypotheticalPosition).
    const auto positionProperty = mStyleDisplay->mPosition;
    const nscoord start = nsLayoutUtils::ComputeCBDependentValue(
        aContainingBlockSize,
        mStyleMargin->GetMargin(startSide, positionProperty));
    const nscoord end = nsLayoutUtils::ComputeCBDependentValue(
        aContainingBlockSize,
        mStyleMargin->GetMargin(endSide, positionProperty));
    marginStartEnd = start + end;
  }

  nscoord outside = paddingStartEnd + borderStartEnd + marginStartEnd;
  nscoord inside = 0;
  if (mStylePosition->mBoxSizing == StyleBoxSizing::Border) {
    inside = borderStartEnd + paddingStartEnd;
  }
  outside -= inside;
  *aInsideBoxSizing = inside;
  *aOutsideBoxSizing = outside;
}

/**
 * Returns true iff a pre-order traversal of the normal child
 * frames rooted at aFrame finds no non-empty frame before aDescendant.
 */
static bool AreAllEarlierInFlowFramesEmpty(nsIFrame* aFrame,
                                           nsIFrame* aDescendant,
                                           bool* aFound) {
  if (aFrame == aDescendant) {
    *aFound = true;
    return true;
  }
  if (aFrame->IsPlaceholderFrame()) {
    auto ph = static_cast<nsPlaceholderFrame*>(aFrame);
    MOZ_ASSERT(ph->IsSelfEmpty() && ph->PrincipalChildList().IsEmpty());
    ph->SetLineIsEmptySoFar(true);
  } else {
    if (!aFrame->IsSelfEmpty()) {
      *aFound = false;
      return false;
    }
    for (nsIFrame* f : aFrame->PrincipalChildList()) {
      bool allEmpty = AreAllEarlierInFlowFramesEmpty(f, aDescendant, aFound);
      if (*aFound || !allEmpty) {
        return allEmpty;
      }
    }
  }
  *aFound = false;
  return true;
}

static bool AxisPolarityFlipped(LogicalAxis aThisAxis, WritingMode aThisWm,
                                WritingMode aOtherWm) {
  if (MOZ_LIKELY(aThisWm == aOtherWm)) {
    // Dedicated short circuit for the common case.
    return false;
  }
  LogicalAxis otherAxis = aThisWm.IsOrthogonalTo(aOtherWm)
                              ? GetOrthogonalAxis(aThisAxis)
                              : aThisAxis;
  NS_ASSERTION(
      aThisWm.PhysicalAxis(aThisAxis) == aOtherWm.PhysicalAxis(otherAxis),
      "Physical axes must match!");
  Side thisStartSide =
      aThisWm.PhysicalSide(MakeLogicalSide(aThisAxis, LogicalEdge::Start));
  Side otherStartSide =
      aOtherWm.PhysicalSide(MakeLogicalSide(otherAxis, LogicalEdge::Start));
  return thisStartSide != otherStartSide;
}

static bool InlinePolarityFlipped(WritingMode aThisWm, WritingMode aOtherWm) {
  return AxisPolarityFlipped(LogicalAxis::Inline, aThisWm, aOtherWm);
}

static bool BlockPolarityFlipped(WritingMode aThisWm, WritingMode aOtherWm) {
  return AxisPolarityFlipped(LogicalAxis::Block, aThisWm, aOtherWm);
}

// In the code below, |aCBReflowInput->mFrame| is the absolute containing block,
// while |containingBlock| is the nearest block container of the placeholder
// frame, which may be different from the absolute containing block.
void ReflowInput::CalculateHypotheticalPosition(
    nsPlaceholderFrame* aPlaceholderFrame, const ReflowInput* aCBReflowInput,
    nsHypotheticalPosition& aHypotheticalPos) const {
  NS_ASSERTION(mStyleDisplay->mOriginalDisplay != StyleDisplay::None,
               "mOriginalDisplay has not been properly initialized");

  // Find the nearest containing block frame to the placeholder frame,
  // and its inline-start edge and width.
  nscoord blockIStartContentEdge;
  // Dummy writing mode for blockContentSize, will be changed as needed by
  // GetHypotheticalBoxContainer.
  WritingMode cbwm = aCBReflowInput->GetWritingMode();
  LogicalSize blockContentSize(cbwm);
  nsIFrame* containingBlock = GetHypotheticalBoxContainer(
      aPlaceholderFrame, blockIStartContentEdge, blockContentSize);
  // Now blockContentSize is in containingBlock's writing mode.

  // If it's a replaced element and it has a 'auto' value for
  //'inline size', see if we can get the intrinsic size. This will allow
  // us to exactly determine both the inline edges
  WritingMode wm = containingBlock->GetWritingMode();

  const auto positionProperty = mStyleDisplay->mPosition;
  const auto styleISize = mStylePosition->ISize(wm, positionProperty);
  bool isAutoISize = styleISize->IsAuto();
  Maybe<nsSize> intrinsicSize;
  if (mFlags.mIsReplaced && isAutoISize) {
    // See if we can get the intrinsic size of the element
    intrinsicSize = mFrame->GetIntrinsicSize().ToSize();
  }

  // See if we can calculate what the box inline size would have been if
  // the element had been in the flow
  Maybe<nscoord> boxISize;
  if (mStyleDisplay->IsOriginalDisplayInlineOutside() && !mFlags.mIsReplaced) {
    // For non-replaced inline-level elements the 'inline size' property
    // doesn't apply, so we don't know what the inline size would have
    // been without reflowing it
  } else {
    // It's either a replaced inline-level element or a block-level element

    // Determine the total amount of inline direction
    // border/padding/margin that the element would have had if it had
    // been in the flow. Note that we ignore any 'auto' and 'inherit'
    // values
    nscoord contentEdgeToBoxSizingISize, boxSizingToMarginEdgeISize;
    CalculateBorderPaddingMargin(
        LogicalAxis::Inline, blockContentSize.ISize(wm),
        &contentEdgeToBoxSizingISize, &boxSizingToMarginEdgeISize);

    if (mFlags.mIsReplaced && isAutoISize) {
      // It's a replaced element with an 'auto' inline size so the box inline
      // size is its intrinsic size plus any border/padding/margin
      if (intrinsicSize) {
        boxISize.emplace(LogicalSize(wm, *intrinsicSize).ISize(wm) +
                         contentEdgeToBoxSizingISize +
                         boxSizingToMarginEdgeISize);
      }
    } else if (isAutoISize) {
      // The box inline size is the containing block inline size
      boxISize.emplace(blockContentSize.ISize(wm));
    } else {
      // We need to compute it. It's important we do this, because if it's
      // percentage based this computed value may be different from the computed
      // value calculated using the absolute containing block width
      nscoord contentEdgeToBoxSizingBSize, dummy;
      CalculateBorderPaddingMargin(LogicalAxis::Block,
                                   blockContentSize.ISize(wm),
                                   &contentEdgeToBoxSizingBSize, &dummy);

      const auto contentISize =
          mFrame
              ->ComputeISizeValue(mRenderingContext, wm, blockContentSize,
                                  LogicalSize(wm, contentEdgeToBoxSizingISize,
                                              contentEdgeToBoxSizingBSize),
                                  boxSizingToMarginEdgeISize, *styleISize,
                                  *mStylePosition->BSize(wm, positionProperty),
                                  mFrame->GetAspectRatio())
              .mISize;
      boxISize.emplace(contentISize + contentEdgeToBoxSizingISize +
                       boxSizingToMarginEdgeISize);
    }
  }

  // Get the placeholder x-offset and y-offset in the coordinate
  // space of its containing block
  // XXXbz the placeholder is not fully reflowed yet if our containing block is
  // relatively positioned...
  nsSize containerSize =
      containingBlock->HasAnyStateBits(NS_FRAME_IN_REFLOW)
          ? aCBReflowInput->ComputedSizeAsContainerIfConstrained()
          : containingBlock->GetSize();
  LogicalPoint placeholderOffset(
      wm, aPlaceholderFrame->GetOffsetToIgnoringScrolling(containingBlock),
      containerSize);

  // First, determine the hypothetical box's mBStart.  We want to check the
  // content insertion frame of containingBlock for block-ness, but make
  // sure to compute all coordinates in the coordinate system of
  // containingBlock.
  nsBlockFrame* blockFrame =
      do_QueryFrame(containingBlock->GetContentInsertionFrame());
  if (blockFrame) {
    // Use a null containerSize to convert a LogicalPoint functioning as a
    // vector into a physical nsPoint vector.
    const nsSize nullContainerSize;
    LogicalPoint blockOffset(
        wm, blockFrame->GetOffsetToIgnoringScrolling(containingBlock),
        nullContainerSize);
    bool isValid;
    nsBlockInFlowLineIterator iter(blockFrame, aPlaceholderFrame, &isValid);
    if (!isValid) {
      // Give up.  We're probably dealing with somebody using
      // position:absolute inside native-anonymous content anyway.
      aHypotheticalPos.mBStart = placeholderOffset.B(wm);
    } else {
      NS_ASSERTION(iter.GetContainer() == blockFrame,
                   "Found placeholder in wrong block!");
      nsBlockFrame::LineIterator lineBox = iter.GetLine();

      // How we determine the hypothetical box depends on whether the element
      // would have been inline-level or block-level
      LogicalRect lineBounds = lineBox->GetBounds().ConvertTo(
          wm, lineBox->mWritingMode, lineBox->mContainerSize);
      if (mStyleDisplay->IsOriginalDisplayInlineOutside()) {
        // Use the block-start of the inline box which the placeholder lives in
        // as the hypothetical box's block-start.
        aHypotheticalPos.mBStart = lineBounds.BStart(wm) + blockOffset.B(wm);
      } else {
        // The element would have been block-level which means it would
        // be below the line containing the placeholder frame, unless
        // all the frames before it are empty.  In that case, it would
        // have been just before this line.
        // XXXbz the line box is not fully reflowed yet if our
        // containing block is relatively positioned...
        if (lineBox != iter.End()) {
          nsIFrame* firstFrame = lineBox->mFirstChild;
          bool allEmpty = false;
          if (firstFrame == aPlaceholderFrame) {
            aPlaceholderFrame->SetLineIsEmptySoFar(true);
            allEmpty = true;
          } else {
            auto* prev = aPlaceholderFrame->GetPrevSibling();
            if (prev && prev->IsPlaceholderFrame()) {
              auto* ph = static_cast<nsPlaceholderFrame*>(prev);
              if (ph->GetLineIsEmptySoFar(&allEmpty)) {
                aPlaceholderFrame->SetLineIsEmptySoFar(allEmpty);
              }
            }
          }
          if (!allEmpty) {
            bool found = false;
            while (firstFrame) {  // See bug 223064
              allEmpty = AreAllEarlierInFlowFramesEmpty(
                  firstFrame, aPlaceholderFrame, &found);
              if (found || !allEmpty) {
                break;
              }
              firstFrame = firstFrame->GetNextSibling();
            }
            aPlaceholderFrame->SetLineIsEmptySoFar(allEmpty);
          }
          NS_ASSERTION(firstFrame, "Couldn't find placeholder!");

          if (allEmpty) {
            // The top of the hypothetical box is the top of the line
            // containing the placeholder, since there is nothing in the
            // line before our placeholder except empty frames.
            aHypotheticalPos.mBStart =
                lineBounds.BStart(wm) + blockOffset.B(wm);
          } else {
            // The top of the hypothetical box is just below the line
            // containing the placeholder.
            aHypotheticalPos.mBStart = lineBounds.BEnd(wm) + blockOffset.B(wm);
          }
        } else {
          // Just use the placeholder's block-offset wrt the containing block
          aHypotheticalPos.mBStart = placeholderOffset.B(wm);
        }
      }
    }
  } else {
    // The containing block is not a block, so it's probably something
    // like a XUL box, etc.
    // Just use the placeholder's block-offset
    aHypotheticalPos.mBStart = placeholderOffset.B(wm);
  }

  // Second, determine the hypothetical box's mIStart.
  // How we determine the hypothetical box depends on whether the element
  // would have been inline-level or block-level
  if (mStyleDisplay->IsOriginalDisplayInlineOutside() ||
      mFlags.mIOffsetsNeedCSSAlign) {
    // The placeholder represents the IStart edge of the hypothetical box.
    // (Or if mFlags.mIOffsetsNeedCSSAlign is set, it represents the IStart
    // edge of the Alignment Container.)
    aHypotheticalPos.mIStart = placeholderOffset.I(wm);
  } else {
    aHypotheticalPos.mIStart = blockIStartContentEdge;
  }

  // The current coordinate space is that of the nearest block to the
  // placeholder. Convert to the coordinate space of the absolute containing
  // block.
  const nsIFrame* cbFrame = aCBReflowInput->mFrame;
  nsPoint cbOffset = containingBlock->GetOffsetToIgnoringScrolling(cbFrame);
  if (cbFrame->IsViewportFrame()) {
    // When the containing block is the ViewportFrame, i.e. we are calculating
    // the static position for a fixed-positioned frame, we need to adjust the
    // origin to exclude the scrollbar or scrollbar-gutter area. The
    // ViewportFrame's containing block rect is passed into
    // nsAbsoluteContainingBlock::ReflowAbsoluteFrame(), and it will add the
    // rect's origin to the fixed-positioned frame's final position if needed.
    //
    // Note: The origin of the containing block rect is adjusted in
    // ViewportFrame::AdjustReflowInputForScrollbars(). Ensure the code there
    // remains in sync with the logic here.
    if (ScrollContainerFrame* sf =
            do_QueryFrame(cbFrame->PrincipalChildList().FirstChild())) {
      const nsMargin scrollbarSizes = sf->GetActualScrollbarSizes();
      cbOffset.MoveBy(-scrollbarSizes.left, -scrollbarSizes.top);
    }
  }

  nsSize reflowSize = aCBReflowInput->ComputedSizeAsContainerIfConstrained();
  LogicalPoint logCBOffs(wm, cbOffset, reflowSize - containerSize);
  aHypotheticalPos.mIStart += logCBOffs.I(wm);
  aHypotheticalPos.mBStart += logCBOffs.B(wm);

  // If block direction doesn't match (whether orthogonal or antiparallel),
  // we'll have to convert aHypotheticalPos to be in terms of cbwm.
  // This upcoming conversion must be taken into account for border offsets.
  const bool hypotheticalPosWillUseCbwm =
      cbwm.GetBlockDir() != wm.GetBlockDir();
  // The specified offsets are relative to the absolute containing block's
  // padding edge and our current values are relative to the border edge, so
  // translate.
  const LogicalMargin border = aCBReflowInput->ComputedLogicalBorder(wm);
  if (hypotheticalPosWillUseCbwm && InlinePolarityFlipped(wm, cbwm)) {
    aHypotheticalPos.mIStart += border.IEnd(wm);
  } else {
    aHypotheticalPos.mIStart -= border.IStart(wm);
  }

  if (hypotheticalPosWillUseCbwm && BlockPolarityFlipped(wm, cbwm)) {
    aHypotheticalPos.mBStart += border.BEnd(wm);
  } else {
    aHypotheticalPos.mBStart -= border.BStart(wm);
  }
  // At this point, we have computed aHypotheticalPos using the writing mode
  // of the placeholder's containing block.

  if (hypotheticalPosWillUseCbwm) {
    // If the block direction we used in calculating aHypotheticalPos does not
    // match the absolute containing block's, we need to convert here so that
    // aHypotheticalPos is usable in relation to the absolute containing block.
    // This requires computing or measuring the abspos frame's block-size,
    // which is not otherwise required/used here (as aHypotheticalPos
    // records only the block-start coordinate).

    // This is similar to the inline-size calculation for a replaced
    // inline-level element or a block-level element (above), except that
    // 'auto' sizing is handled differently in the block direction for non-
    // replaced elements and replaced elements lacking an intrinsic size.

    // Determine the total amount of block direction
    // border/padding/margin that the element would have had if it had
    // been in the flow. Note that we ignore any 'auto' and 'inherit'
    // values.
    nscoord insideBoxSizing, outsideBoxSizing;
    CalculateBorderPaddingMargin(LogicalAxis::Block, blockContentSize.BSize(wm),
                                 &insideBoxSizing, &outsideBoxSizing);

    nscoord boxBSize;
    const auto styleBSize = mStylePosition->BSize(wm, positionProperty);
    const bool isAutoBSize =
        nsLayoutUtils::IsAutoBSize(*styleBSize, blockContentSize.BSize(wm));
    if (isAutoBSize) {
      if (mFlags.mIsReplaced && intrinsicSize) {
        // It's a replaced element with an 'auto' block size so the box
        // block size is its intrinsic size plus any border/padding/margin
        boxBSize = LogicalSize(wm, *intrinsicSize).BSize(wm) +
                   outsideBoxSizing + insideBoxSizing;
      } else {
        // XXX Bug 1191801
        // Figure out how to get the correct boxBSize here (need to reflow the
        // positioned frame?)
        boxBSize = 0;
      }
    } else if (styleBSize->BehavesLikeStretchOnBlockAxis()) {
      MOZ_ASSERT(blockContentSize.BSize(wm) != NS_UNCONSTRAINEDSIZE,
                 "If we're 'stretch' with unconstrained size, isAutoBSize "
                 "should be true which should make us skip this code");
      // TODO(dholbert) The 'insideBoxSizing' and 'outsideBoxSizing' usages
      // here aren't quite right, because we're supposed to be passing margin
      // and borderPadding specifically.  The arithmetic seems to work out in
      // testcases though.
      boxBSize = nsLayoutUtils::ComputeStretchContentBoxBSize(
          blockContentSize.BSize(wm), outsideBoxSizing, insideBoxSizing);
    } else {
      // We need to compute it. It's important we do this, because if it's
      // percentage-based this computed value may be different from the
      // computed value calculated using the absolute containing block height.
      boxBSize = nsLayoutUtils::ComputeBSizeValue(
                     blockContentSize.BSize(wm), insideBoxSizing,
                     styleBSize->AsLengthPercentage()) +
                 insideBoxSizing + outsideBoxSizing;
    }

    LogicalSize boxSize(wm, boxISize.valueOr(0), boxBSize);

    LogicalPoint origin(wm, aHypotheticalPos.mIStart, aHypotheticalPos.mBStart);
    origin = origin.ConvertRectOriginTo(cbwm, wm, boxSize.GetPhysicalSize(wm),
                                        reflowSize);

    aHypotheticalPos.mIStart = origin.I(cbwm);
    aHypotheticalPos.mBStart = origin.B(cbwm);
    aHypotheticalPos.mWritingMode = cbwm;
  } else {
    aHypotheticalPos.mWritingMode = wm;
  }
}

void ReflowInput::InitAbsoluteConstraints(const ReflowInput* aCBReflowInput,
                                          const LogicalSize& aCBSize) {
  WritingMode wm = GetWritingMode();
  WritingMode cbwm = aCBReflowInput->GetWritingMode();
  NS_WARNING_ASSERTION(aCBSize.BSize(cbwm) != NS_UNCONSTRAINEDSIZE,
                       "containing block bsize must be constrained");

  NS_ASSERTION(!mFrame->IsTableFrame(),
               "InitAbsoluteConstraints should not be called on table frames");
  NS_ASSERTION(mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
               "Why are we here?");

  const auto iStartOffset = mStylePosition->GetAnchorResolvedInset(
      LogicalSide::IStart, cbwm, StylePositionProperty::Absolute);
  const auto iEndOffset = mStylePosition->GetAnchorResolvedInset(
      LogicalSide::IEnd, cbwm, StylePositionProperty::Absolute);
  const auto bStartOffset = mStylePosition->GetAnchorResolvedInset(
      LogicalSide::BStart, cbwm, StylePositionProperty::Absolute);
  const auto bEndOffset = mStylePosition->GetAnchorResolvedInset(
      LogicalSide::BEnd, cbwm, StylePositionProperty::Absolute);
  bool iStartIsAuto = iStartOffset->IsAuto();
  bool iEndIsAuto = iEndOffset->IsAuto();
  bool bStartIsAuto = bStartOffset->IsAuto();
  bool bEndIsAuto = bEndOffset->IsAuto();

  // If both 'inline-start' and 'inline-end' are 'auto' or both 'block-start'
  // and 'block-end' are 'auto', then compute the hypothetical box position
  // where the element would have if it were in the flow.
  nsHypotheticalPosition hypotheticalPos;
  if ((iStartIsAuto && iEndIsAuto) || (bStartIsAuto && bEndIsAuto)) {
    nsPlaceholderFrame* placeholderFrame = mFrame->GetPlaceholderFrame();
    MOZ_ASSERT(placeholderFrame, "no placeholder frame");
    nsIFrame* placeholderParent = placeholderFrame->GetParent();
    MOZ_ASSERT(placeholderParent, "shouldn't have unparented placeholders");

    if (placeholderFrame->HasAnyStateBits(
            PLACEHOLDER_STATICPOS_NEEDS_CSSALIGN)) {
      MOZ_ASSERT(placeholderParent->IsFlexOrGridContainer(),
                 "This flag should only be set on grid/flex children");
      // If the (as-yet unknown) static position will determine the inline
      // and/or block offsets, set flags to note those offsets aren't valid
      // until we can do CSS Box Alignment on the OOF frame.
      mFlags.mIOffsetsNeedCSSAlign = (iStartIsAuto && iEndIsAuto);
      mFlags.mBOffsetsNeedCSSAlign = (bStartIsAuto && bEndIsAuto);
    }

    if (mFlags.mStaticPosIsCBOrigin) {
      hypotheticalPos.mWritingMode = cbwm;
      hypotheticalPos.mIStart = nscoord(0);
      hypotheticalPos.mBStart = nscoord(0);
      if (placeholderParent->IsGridContainerFrame() &&
          placeholderParent->HasAnyStateBits(NS_STATE_GRID_IS_COL_MASONRY |
                                             NS_STATE_GRID_IS_ROW_MASONRY)) {
        // Disable CSS alignment in Masonry layout since we don't have real grid
        // areas in that axis.  We'll use the placeholder position instead as it
        // was calculated by nsGridContainerFrame::MasonryLayout.
        auto cbsz = aCBSize.GetPhysicalSize(cbwm);
        LogicalPoint pos = placeholderFrame->GetLogicalPosition(cbwm, cbsz);
        if (placeholderParent->HasAnyStateBits(NS_STATE_GRID_IS_COL_MASONRY)) {
          mFlags.mIOffsetsNeedCSSAlign = false;
          hypotheticalPos.mIStart = pos.I(cbwm);
        } else {
          mFlags.mBOffsetsNeedCSSAlign = false;
          hypotheticalPos.mBStart = pos.B(cbwm);
        }
      }
    } else {
      // XXXmats all this is broken for orthogonal writing-modes: bug 1521988.
      CalculateHypotheticalPosition(placeholderFrame, aCBReflowInput,
                                    hypotheticalPos);
      if (aCBReflowInput->mFrame->IsGridContainerFrame()) {
        // 'hypotheticalPos' is relative to the padding rect of the CB *frame*.
        // In grid layout the CB is the grid area rectangle, so we translate
        // 'hypotheticalPos' to be relative that rectangle here.
        nsRect cb = nsGridContainerFrame::GridItemCB(mFrame);
        nscoord left(0);
        nscoord right(0);
        if (cbwm.IsBidiLTR()) {
          left = cb.X();
        } else {
          right = aCBReflowInput->ComputedWidth() +
                  aCBReflowInput->ComputedPhysicalPadding().LeftRight() -
                  cb.XMost();
        }
        LogicalMargin offsets(cbwm, nsMargin(cb.Y(), right, nscoord(0), left));
        hypotheticalPos.mIStart -= offsets.IStart(cbwm);
        hypotheticalPos.mBStart -= offsets.BStart(cbwm);
      }
    }
  }

  // Size of the containing block in its writing mode
  LogicalSize cbSize = aCBSize;
  LogicalMargin offsets(cbwm);

  // Handle auto inset values, as per [1].
  // Technically superceded by a new section [2], but none of the browsers seem
  // to follow this behaviour.
  //
  // [1] https://drafts.csswg.org/css-position-3/#abspos-old
  // [2] https://drafts.csswg.org/css-position-3/#resolving-insets
  if (iStartIsAuto) {
    offsets.IStart(cbwm) = 0;
  } else {
    offsets.IStart(cbwm) = nsLayoutUtils::ComputeCBDependentValue(
        cbSize.ISize(cbwm), iStartOffset);
  }
  if (iEndIsAuto) {
    offsets.IEnd(cbwm) = 0;
  } else {
    offsets.IEnd(cbwm) =
        nsLayoutUtils::ComputeCBDependentValue(cbSize.ISize(cbwm), iEndOffset);
  }

  if (iStartIsAuto && iEndIsAuto) {
    if (cbwm.IsInlineReversed() !=
        hypotheticalPos.mWritingMode.IsInlineReversed()) {
      offsets.IEnd(cbwm) = hypotheticalPos.mIStart;
      iEndIsAuto = false;
    } else {
      offsets.IStart(cbwm) = hypotheticalPos.mIStart;
      iStartIsAuto = false;
    }
  }

  if (bStartIsAuto) {
    offsets.BStart(cbwm) = 0;
  } else {
    offsets.BStart(cbwm) = nsLayoutUtils::ComputeCBDependentValue(
        cbSize.BSize(cbwm), bStartOffset);
  }
  if (bEndIsAuto) {
    offsets.BEnd(cbwm) = 0;
  } else {
    offsets.BEnd(cbwm) =
        nsLayoutUtils::ComputeCBDependentValue(cbSize.BSize(cbwm), bEndOffset);
  }

  if (bStartIsAuto && bEndIsAuto) {
    // Treat 'top' like 'static-position'
    offsets.BStart(cbwm) = hypotheticalPos.mBStart;
    bStartIsAuto = false;
  }

  SetComputedLogicalOffsets(cbwm, offsets);

  if (wm.IsOrthogonalTo(cbwm)) {
    if (bStartIsAuto || bEndIsAuto) {
      mComputeSizeFlags += ComputeSizeFlag::ShrinkWrap;
    }
  } else {
    if (iStartIsAuto || iEndIsAuto) {
      mComputeSizeFlags += ComputeSizeFlag::ShrinkWrap;
    }
  }

  nsIFrame::SizeComputationResult sizeResult = {
      LogicalSize(wm), nsIFrame::AspectRatioUsage::None};
  {
    AutoMaybeDisableFontInflation an(mFrame);

    sizeResult = mFrame->ComputeSize(
        mRenderingContext, wm, cbSize.ConvertTo(wm, cbwm),
        cbSize.ConvertTo(wm, cbwm).ISize(wm),  // XXX or AvailableISize()?
        ComputedLogicalMargin(wm).Size(wm) +
            ComputedLogicalOffsets(wm).Size(wm),
        ComputedLogicalBorderPadding(wm).Size(wm), {}, mComputeSizeFlags);
    mComputedSize = sizeResult.mLogicalSize;
    NS_ASSERTION(ComputedISize() >= 0, "Bogus inline-size");
    NS_ASSERTION(
        ComputedBSize() == NS_UNCONSTRAINEDSIZE || ComputedBSize() >= 0,
        "Bogus block-size");
  }

  LogicalSize& computedSize = sizeResult.mLogicalSize;
  computedSize = computedSize.ConvertTo(cbwm, wm);

  mFlags.mIsBSizeSetByAspectRatio = sizeResult.mAspectRatioUsage ==
                                    nsIFrame::AspectRatioUsage::ToComputeBSize;

  // XXX Now that we have ComputeSize, can we condense many of the
  // branches off of widthIsAuto?

  LogicalMargin margin = ComputedLogicalMargin(cbwm);
  const LogicalMargin borderPadding = ComputedLogicalBorderPadding(cbwm);

  bool iSizeIsAuto =
      mStylePosition->ISize(cbwm, mStyleDisplay->mPosition)->IsAuto();
  bool marginIStartIsAuto = false;
  bool marginIEndIsAuto = false;
  bool marginBStartIsAuto = false;
  bool marginBEndIsAuto = false;
  if (iStartIsAuto) {
    // We know 'right' is not 'auto' anymore thanks to the hypothetical
    // box code above.
    // Solve for 'left'.
    if (iSizeIsAuto) {
      // XXXldb This, and the corresponding code in
      // nsAbsoluteContainingBlock.cpp, could probably go away now that
      // we always compute widths.
      offsets.IStart(cbwm) = NS_AUTOOFFSET;
    } else {
      offsets.IStart(cbwm) = cbSize.ISize(cbwm) - offsets.IEnd(cbwm) -
                             computedSize.ISize(cbwm) - margin.IStartEnd(cbwm) -
                             borderPadding.IStartEnd(cbwm);
    }
  } else if (iEndIsAuto) {
    // We know 'left' is not 'auto' anymore thanks to the hypothetical
    // box code above.
    // Solve for 'right'.
    if (iSizeIsAuto) {
      // XXXldb This, and the corresponding code in
      // nsAbsoluteContainingBlock.cpp, could probably go away now that
      // we always compute widths.
      offsets.IEnd(cbwm) = NS_AUTOOFFSET;
    } else {
      offsets.IEnd(cbwm) = cbSize.ISize(cbwm) - offsets.IStart(cbwm) -
                           computedSize.ISize(cbwm) - margin.IStartEnd(cbwm) -
                           borderPadding.IStartEnd(cbwm);
    }
  } else if (!mFrame->HasIntrinsicKeywordForBSize() ||
             !wm.IsOrthogonalTo(cbwm)) {
    // Neither 'inline-start' nor 'inline-end' is 'auto'.
    // The inline-size might not fill all the available space (even though we
    // didn't shrink-wrap) in case:
    //  * insets are explicitly set and the child frame is not stretched
    //  * inline-size was specified
    //  * we're dealing with a replaced element
    //  * width was constrained by min- or max-inline-size.

    nscoord availMarginSpace =
        aCBSize.ISize(cbwm) - offsets.IStartEnd(cbwm) - margin.IStartEnd(cbwm) -
        borderPadding.IStartEnd(cbwm) - computedSize.ISize(cbwm);
    marginIStartIsAuto = mStyleMargin
                             ->GetMargin(LogicalSide::IStart, cbwm,
                                         StylePositionProperty::Absolute)
                             ->IsAuto();
    marginIEndIsAuto = mStyleMargin
                           ->GetMargin(LogicalSide::IEnd, cbwm,
                                       StylePositionProperty::Absolute)
                           ->IsAuto();
    ComputeAbsPosInlineAutoMargin(availMarginSpace, cbwm, marginIStartIsAuto,
                                  marginIEndIsAuto, margin, offsets);
  }

  bool bSizeIsAuto = mStylePosition->BSize(cbwm, mStyleDisplay->mPosition)
                         ->BehavesLikeInitialValueOnBlockAxis();
  if (bStartIsAuto) {
    // solve for block-start
    if (bSizeIsAuto) {
      offsets.BStart(cbwm) = NS_AUTOOFFSET;
    } else {
      offsets.BStart(cbwm) = cbSize.BSize(cbwm) - margin.BStartEnd(cbwm) -
                             borderPadding.BStartEnd(cbwm) -
                             computedSize.BSize(cbwm) - offsets.BEnd(cbwm);
    }
  } else if (bEndIsAuto) {
    // solve for block-end
    if (bSizeIsAuto) {
      offsets.BEnd(cbwm) = NS_AUTOOFFSET;
    } else {
      offsets.BEnd(cbwm) = cbSize.BSize(cbwm) - margin.BStartEnd(cbwm) -
                           borderPadding.BStartEnd(cbwm) -
                           computedSize.BSize(cbwm) - offsets.BStart(cbwm);
    }
  } else if (!mFrame->HasIntrinsicKeywordForBSize() ||
             wm.IsOrthogonalTo(cbwm)) {
    // Neither block-start nor -end is 'auto'.
    nscoord autoBSize = cbSize.BSize(cbwm) - margin.BStartEnd(cbwm) -
                        borderPadding.BStartEnd(cbwm) - offsets.BStartEnd(cbwm);
    autoBSize = std::max(autoBSize, 0);
    // FIXME: Bug 1602669: if |autoBSize| happens to be numerically equal to
    // NS_UNCONSTRAINEDSIZE, we may get some unexpected behavior. We need a
    // better way to distinguish between unconstrained size and resolved size.
    NS_WARNING_ASSERTION(autoBSize != NS_UNCONSTRAINEDSIZE,
                         "Unexpected size from block-start and block-end");

    // The block-size might not fill all the available space in case:
    //  * insets are explicitly set and the child frame is not stretched
    //  * bsize was specified
    //  * we're dealing with a replaced element
    //  * bsize was constrained by min- or max-bsize.
    nscoord availMarginSpace = autoBSize - computedSize.BSize(cbwm);
    marginBStartIsAuto = mStyleMargin
                             ->GetMargin(LogicalSide::BStart, cbwm,
                                         StylePositionProperty::Absolute)
                             ->IsAuto();
    marginBEndIsAuto = mStyleMargin
                           ->GetMargin(LogicalSide::BEnd, cbwm,
                                       StylePositionProperty::Absolute)
                           ->IsAuto();

    ComputeAbsPosBlockAutoMargin(availMarginSpace, cbwm, marginBStartIsAuto,
                                 marginBEndIsAuto, margin, offsets);
  }
  mComputedSize = computedSize.ConvertTo(wm, cbwm);

  SetComputedLogicalOffsets(cbwm, offsets);
  SetComputedLogicalMargin(cbwm, margin);

  // If we have auto margins, update our UsedMarginProperty. The property
  // will have already been created by InitOffsets if it is needed.
  if (marginIStartIsAuto || marginIEndIsAuto || marginBStartIsAuto ||
      marginBEndIsAuto) {
    nsMargin* propValue = mFrame->GetProperty(nsIFrame::UsedMarginProperty());
    MOZ_ASSERT(propValue,
               "UsedMarginProperty should have been created "
               "by InitOffsets.");
    *propValue = margin.GetPhysicalMargin(cbwm);
  }
}

// This will not be converted to abstract coordinates because it's only
// used in CalcQuirkContainingBlockHeight
static nscoord GetBlockMarginBorderPadding(const ReflowInput* aReflowInput) {
  nscoord result = 0;
  if (!aReflowInput) {
    return result;
  }

  // zero auto margins
  nsMargin margin = aReflowInput->ComputedPhysicalMargin();
  if (NS_AUTOMARGIN == margin.top) {
    margin.top = 0;
  }
  if (NS_AUTOMARGIN == margin.bottom) {
    margin.bottom = 0;
  }

  result += margin.top + margin.bottom;
  result += aReflowInput->ComputedPhysicalBorderPadding().top +
            aReflowInput->ComputedPhysicalBorderPadding().bottom;

  return result;
}

/* Get the height based on the viewport of the containing block specified
 * in aReflowInput when the containing block has mComputedHeight ==
 * NS_UNCONSTRAINEDSIZE This will walk up the chain of containing blocks looking
 * for a computed height until it finds the canvas frame, or it encounters a
 * frame that is not a block, area, or scroll frame. This handles compatibility
 * with IE (see bug 85016 and bug 219693)
 *
 * When we encounter scrolledContent block frames, we skip over them,
 * since they are guaranteed to not be useful for computing the containing
 * block.
 *
 * See also IsQuirkContainingBlockHeight.
 */
static nscoord CalcQuirkContainingBlockHeight(
    const ReflowInput* aCBReflowInput) {
  const ReflowInput* firstAncestorRI = nullptr;   // a candidate for html frame
  const ReflowInput* secondAncestorRI = nullptr;  // a candidate for body frame

  // initialize the default to NS_UNCONSTRAINEDSIZE as this is the containings
  // block computed height when this function is called. It is possible that we
  // don't alter this height especially if we are restricted to one level
  nscoord result = NS_UNCONSTRAINEDSIZE;

  const ReflowInput* ri = aCBReflowInput;
  for (; ri; ri = ri->mParentReflowInput) {
    LayoutFrameType frameType = ri->mFrame->Type();
    // if the ancestor is auto height then skip it and continue up if it
    // is the first block frame and possibly the body/html
    if (LayoutFrameType::Block == frameType ||
        LayoutFrameType::ScrollContainer == frameType) {
      secondAncestorRI = firstAncestorRI;
      firstAncestorRI = ri;

      // If the current frame we're looking at is positioned, we don't want to
      // go any further (see bug 221784).  The behavior we want here is: 1) If
      // not auto-height, use this as the percentage base.  2) If auto-height,
      // keep looking, unless the frame is positioned.
      if (NS_UNCONSTRAINEDSIZE == ri->ComputedHeight()) {
        if (ri->mFrame->IsAbsolutelyPositioned(ri->mStyleDisplay)) {
          break;
        } else {
          continue;
        }
      }
    } else if (LayoutFrameType::Canvas == frameType) {
      // Always continue on to the height calculation
    } else if (LayoutFrameType::PageContent == frameType) {
      nsIFrame* prevInFlow = ri->mFrame->GetPrevInFlow();
      // only use the page content frame for a height basis if it is the first
      // in flow
      if (prevInFlow) {
        break;
      }
    } else {
      break;
    }

    // if the ancestor is the page content frame then the percent base is
    // the avail height, otherwise it is the computed height
    result = (LayoutFrameType::PageContent == frameType) ? ri->AvailableHeight()
                                                         : ri->ComputedHeight();
    // if unconstrained - don't sutract borders - would result in huge height
    if (NS_UNCONSTRAINEDSIZE == result) {
      return result;
    }

    // if we got to the canvas or page content frame, then subtract out
    // margin/border/padding for the BODY and HTML elements
    if ((LayoutFrameType::Canvas == frameType) ||
        (LayoutFrameType::PageContent == frameType)) {
      result -= GetBlockMarginBorderPadding(firstAncestorRI);
      result -= GetBlockMarginBorderPadding(secondAncestorRI);

#ifdef DEBUG
      // make sure the first ancestor is the HTML and the second is the BODY
      if (firstAncestorRI) {
        nsIContent* frameContent = firstAncestorRI->mFrame->GetContent();
        if (frameContent) {
          NS_ASSERTION(frameContent->IsHTMLElement(nsGkAtoms::html),
                       "First ancestor is not HTML");
        }
      }
      if (secondAncestorRI) {
        nsIContent* frameContent = secondAncestorRI->mFrame->GetContent();
        if (frameContent) {
          NS_ASSERTION(frameContent->IsHTMLElement(nsGkAtoms::body),
                       "Second ancestor is not BODY");
        }
      }
#endif

    }
    // if we got to the html frame (a block child of the canvas) ...
    else if (LayoutFrameType::Block == frameType && ri->mParentReflowInput &&
             ri->mParentReflowInput->mFrame->IsCanvasFrame()) {
      // ... then subtract out margin/border/padding for the BODY element
      result -= GetBlockMarginBorderPadding(secondAncestorRI);
    }
    break;
  }

  // Make sure not to return a negative height here!
  return std::max(result, 0);
}

// Called by InitConstraints() to compute the containing block rectangle for
// the element. Handles the special logic for absolutely positioned elements
LogicalSize ReflowInput::ComputeContainingBlockRectangle(
    nsPresContext* aPresContext, const ReflowInput* aContainingBlockRI) const {
  // Unless the element is absolutely positioned, the containing block is
  // formed by the content edge of the nearest block-level ancestor
  LogicalSize cbSize = aContainingBlockRI->ComputedSize();

  WritingMode wm = aContainingBlockRI->GetWritingMode();

  if (aContainingBlockRI->mFlags.mTreatBSizeAsIndefinite) {
    cbSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
  } else if (aContainingBlockRI->mPercentageBasisInBlockAxis) {
    MOZ_ASSERT(cbSize.BSize(wm) == NS_UNCONSTRAINEDSIZE,
               "Why provide a percentage basis when the containing block's "
               "block-size is definite?");
    cbSize.BSize(wm) = *aContainingBlockRI->mPercentageBasisInBlockAxis;
  }

  if (((mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
        // XXXfr hack for making frames behave properly when in overflow
        // container lists, see bug 154892; need to revisit later
        !mFrame->GetPrevInFlow()) ||
       (mFrame->IsTableFrame() &&
        mFrame->GetParent()->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW))) &&
      mStyleDisplay->IsAbsolutelyPositioned(mFrame)) {
    // See if the ancestor is block-level or inline-level
    const auto computedPadding = aContainingBlockRI->ComputedLogicalPadding(wm);
    if (aContainingBlockRI->mStyleDisplay->IsInlineOutsideStyle()) {
      // Base our size on the actual size of the frame.  In cases when this is
      // completely bogus (eg initial reflow), this code shouldn't even be
      // called, since the code in nsInlineFrame::Reflow will pass in
      // the containing block dimensions to our constructor.
      // XXXbz we should be taking the in-flows into account too, but
      // that's very hard.

      LogicalMargin computedBorder =
          aContainingBlockRI->ComputedLogicalBorderPadding(wm) -
          computedPadding;
      cbSize.ISize(wm) =
          aContainingBlockRI->mFrame->ISize(wm) - computedBorder.IStartEnd(wm);
      NS_ASSERTION(cbSize.ISize(wm) >= 0, "Negative containing block isize!");
      cbSize.BSize(wm) =
          aContainingBlockRI->mFrame->BSize(wm) - computedBorder.BStartEnd(wm);
      NS_ASSERTION(cbSize.BSize(wm) >= 0, "Negative containing block bsize!");
    } else {
      // If the ancestor is block-level, the containing block is formed by the
      // padding edge of the ancestor
      cbSize += computedPadding.Size(wm);
    }
  } else {
    auto IsQuirky = [](const StyleSize& aSize) -> bool {
      return aSize.ConvertsToPercentage();
    };
    const auto positionProperty = mStyleDisplay->mPosition;
    // an element in quirks mode gets a containing block based on looking for a
    // parent with a non-auto height if the element has a percent height.
    // Note: We don't emulate this quirk for percents in calc(), or in vertical
    // writing modes, or if the containing block is a flex or grid item.
    if (!wm.IsVertical() && NS_UNCONSTRAINEDSIZE == cbSize.BSize(wm)) {
      if (eCompatibility_NavQuirks == aPresContext->CompatibilityMode() &&
          !aContainingBlockRI->mFrame->IsFlexOrGridItem() &&
          (IsQuirky(*mStylePosition->GetHeight(positionProperty)) ||
           (mFrame->IsTableWrapperFrame() &&
            IsQuirky(*mFrame->PrincipalChildList()
                          .FirstChild()
                          ->StylePosition()
                          ->GetHeight(positionProperty))))) {
        cbSize.BSize(wm) = CalcQuirkContainingBlockHeight(aContainingBlockRI);
      }
    }
  }

  return cbSize.ConvertTo(GetWritingMode(), wm);
}

// XXX refactor this code to have methods for each set of properties
// we are computing: width,height,line-height; margin; offsets

void ReflowInput::InitConstraints(
    nsPresContext* aPresContext, const Maybe<LogicalSize>& aContainingBlockSize,
    const Maybe<LogicalMargin>& aBorder, const Maybe<LogicalMargin>& aPadding,
    LayoutFrameType aFrameType) {
  WritingMode wm = GetWritingMode();
  LogicalSize cbSize = aContainingBlockSize.valueOr(
      LogicalSize(mWritingMode, NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE));

  // If this is a reflow root, then set the computed width and
  // height equal to the available space
  if (nullptr == mParentReflowInput || mFlags.mDummyParentReflowInput) {
    // XXXldb This doesn't mean what it used to!
    InitOffsets(wm, cbSize.ISize(wm), aFrameType, mComputeSizeFlags, aBorder,
                aPadding, mStyleDisplay);
    // Override mComputedMargin since reflow roots start from the
    // frame's boundary, which is inside the margin.
    SetComputedLogicalMargin(wm, LogicalMargin(wm));
    SetComputedLogicalOffsets(wm, LogicalMargin(wm));

    const auto borderPadding = ComputedLogicalBorderPadding(wm);
    SetComputedISize(
        std::max(0, AvailableISize() - borderPadding.IStartEnd(wm)),
        ResetResizeFlags::No);
    SetComputedBSize(
        AvailableBSize() != NS_UNCONSTRAINEDSIZE
            ? std::max(0, AvailableBSize() - borderPadding.BStartEnd(wm))
            : NS_UNCONSTRAINEDSIZE,
        ResetResizeFlags::No);

    mComputedMinSize.SizeTo(mWritingMode, 0, 0);
    mComputedMaxSize.SizeTo(mWritingMode, NS_UNCONSTRAINEDSIZE,
                            NS_UNCONSTRAINEDSIZE);
  } else {
    // Get the containing block's reflow input
    const ReflowInput* cbri = mCBReflowInput;
    MOZ_ASSERT(cbri, "no containing block");
    MOZ_ASSERT(mFrame->GetParent());

    // If we weren't given a containing block size, then compute one.
    if (aContainingBlockSize.isNothing()) {
      cbSize = ComputeContainingBlockRectangle(aPresContext, cbri);
    }

    // See if the containing block height is based on the size of its
    // content
    if (NS_UNCONSTRAINEDSIZE == cbSize.BSize(wm)) {
      // See if the containing block is a cell frame which needs
      // to use the mComputedHeight of the cell instead of what the cell block
      // passed in.
      // XXX It seems like this could lead to bugs with min-height and friends
      if (cbri->mParentReflowInput && cbri->mFrame->IsTableCellFrame()) {
        cbSize.BSize(wm) = cbri->ComputedSize(wm).BSize(wm);
      }
    }

    // XXX Might need to also pass the CB height (not width) for page boxes,
    // too, if we implement them.

    // For calculating positioning offsets, margins, borders and
    // padding, we use the writing mode of the containing block
    WritingMode cbwm = cbri->GetWritingMode();
    InitOffsets(cbwm, cbSize.ConvertTo(cbwm, wm).ISize(cbwm), aFrameType,
                mComputeSizeFlags, aBorder, aPadding, mStyleDisplay);

    // For calculating the size of this box, we use its own writing mode
    const auto blockSize = mStylePosition->BSize(wm, mStyleDisplay->mPosition);
    bool isAutoBSize = blockSize->BehavesLikeInitialValueOnBlockAxis();

    // Check for a percentage based block size and a containing block
    // block size that depends on the content block size
    if (blockSize->HasPercent()) {
      if (NS_UNCONSTRAINEDSIZE == cbSize.BSize(wm)) {
        // this if clause enables %-blockSize on replaced inline frames,
        // such as images.  See bug 54119.  The else clause "blockSizeUnit =
        // eStyleUnit_Auto;" used to be called exclusively.
        if (mFlags.mIsReplaced && mStyleDisplay->IsInlineOutsideStyle()) {
          // Get the containing block's reflow input
          NS_ASSERTION(cbri, "no containing block");
          // in quirks mode, get the cb height using the special quirk method
          if (!wm.IsVertical() &&
              eCompatibility_NavQuirks == aPresContext->CompatibilityMode()) {
            if (!cbri->mFrame->IsTableCellFrame() &&
                !cbri->mFrame->IsFlexOrGridItem()) {
              cbSize.BSize(wm) = CalcQuirkContainingBlockHeight(cbri);
              if (cbSize.BSize(wm) == NS_UNCONSTRAINEDSIZE) {
                isAutoBSize = true;
              }
            } else {
              isAutoBSize = true;
            }
          }
          // in standard mode, use the cb block size.  if it's "auto",
          // as will be the case by default in BODY, use auto block size
          // as per CSS2 spec.
          else {
            nscoord computedBSize = cbri->ComputedSize(wm).BSize(wm);
            if (NS_UNCONSTRAINEDSIZE != computedBSize) {
              cbSize.BSize(wm) = computedBSize;
            } else {
              isAutoBSize = true;
            }
          }
        } else {
          // default to interpreting the blockSize like 'auto'
          isAutoBSize = true;
        }
      }
    }

    // Compute our offsets if the element is relatively positioned.  We
    // need the correct containing block inline-size and block-size
    // here, which is why we need to do it after all the quirks-n-such
    // above. (If the element is sticky positioned, we need to wait
    // until the scroll container knows its size, so we compute offsets
    // from StickyScrollContainer::UpdatePositions.)
    if (mStyleDisplay->IsRelativelyPositioned(mFrame)) {
      const LogicalMargin offsets =
          ComputeRelativeOffsets(cbwm, mFrame, cbSize.ConvertTo(cbwm, wm));
      SetComputedLogicalOffsets(cbwm, offsets);
    } else {
      // Initialize offsets to 0
      SetComputedLogicalOffsets(wm, LogicalMargin(wm));
    }

    // Calculate the computed values for min and max properties.  Note that
    // this MUST come after we've computed our border and padding.
    ComputeMinMaxValues(cbSize);

    // Calculate the computed inlineSize and blockSize.
    // This varies by frame type.

    if (IsInternalTableFrame()) {
      // Internal table elements. The rules vary depending on the type.
      // Calculate the computed isize
      bool rowOrRowGroup = false;
      const auto inlineSize =
          mStylePosition->ISize(wm, mStyleDisplay->mPosition);
      bool isAutoISize = inlineSize->IsAuto();
      if ((StyleDisplay::TableRow == mStyleDisplay->mDisplay) ||
          (StyleDisplay::TableRowGroup == mStyleDisplay->mDisplay)) {
        // 'inlineSize' property doesn't apply to table rows and row groups
        isAutoISize = true;
        rowOrRowGroup = true;
      }

      // calc() with both percentages and lengths act like auto on internal
      // table elements
      if (isAutoISize || inlineSize->HasLengthAndPercentage()) {
        if (AvailableISize() != NS_UNCONSTRAINEDSIZE && !rowOrRowGroup) {
          // Internal table elements don't have margins. Only tables and
          // cells have border and padding
          SetComputedISize(
              std::max(0, AvailableISize() -
                              ComputedLogicalBorderPadding(wm).IStartEnd(wm)),
              ResetResizeFlags::No);
        } else {
          SetComputedISize(AvailableISize(), ResetResizeFlags::No);
        }
        NS_ASSERTION(ComputedISize() >= 0, "Bogus computed isize");

      } else {
        SetComputedISize(
            ComputeISizeValue(cbSize, mStylePosition->mBoxSizing, *inlineSize),
            ResetResizeFlags::No);
      }

      // Calculate the computed block size
      if (StyleDisplay::TableColumn == mStyleDisplay->mDisplay ||
          StyleDisplay::TableColumnGroup == mStyleDisplay->mDisplay) {
        // 'blockSize' property doesn't apply to table columns and column groups
        isAutoBSize = true;
      }
      // calc() with both percentages and lengths acts like 'auto' on internal
      // table elements
      if (isAutoBSize || blockSize->HasLengthAndPercentage()) {
        SetComputedBSize(NS_UNCONSTRAINEDSIZE, ResetResizeFlags::No);
      } else {
        SetComputedBSize(
            ComputeBSizeValue(cbSize.BSize(wm), mStylePosition->mBoxSizing,
                              blockSize->AsLengthPercentage()),
            ResetResizeFlags::No);
      }

      // Doesn't apply to internal table elements
      mComputedMinSize.SizeTo(mWritingMode, 0, 0);
      mComputedMaxSize.SizeTo(mWritingMode, NS_UNCONSTRAINEDSIZE,
                              NS_UNCONSTRAINEDSIZE);
    } else if (mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
               mStyleDisplay->IsAbsolutelyPositionedStyle() &&
               // XXXfr hack for making frames behave properly when in overflow
               // container lists, see bug 154892; need to revisit later
               !mFrame->GetPrevInFlow()) {
      InitAbsoluteConstraints(cbri,
                              cbSize.ConvertTo(cbri->GetWritingMode(), wm));
    } else {
      AutoMaybeDisableFontInflation an(mFrame);

      nsIFrame* const alignCB = [&] {
        nsIFrame* cb = mFrame->GetParent();
        if (cb->IsTableWrapperFrame()) {
          nsIFrame* alignCBParent = cb->GetParent();
          if (alignCBParent && alignCBParent->IsGridContainerFrame()) {
            return alignCBParent;
          }
        }
        return cb;
      }();

      const bool isInlineLevel = [&] {
        if (mFrame->IsTableFrame()) {
          // An inner table frame is not inline-level, even if it happens to
          // have 'display:inline-table'. (That makes its table-wrapper frame be
          // inline-level, but not the inner table frame)
          return false;
        }
        if (mStyleDisplay->IsInlineOutsideStyle()) {
          return true;
        }
        if (mFlags.mIsReplaced && (mStyleDisplay->IsInnerTableStyle() ||
                                   mStyleDisplay->DisplayOutside() ==
                                       StyleDisplayOutside::TableCaption)) {
          // Internal table values on replaced elements behave as inline
          // https://drafts.csswg.org/css-tables-3/#table-structure
          //
          //     ... it is handled instead as though the author had declared
          //     either 'block' (for 'table' display) or 'inline' (for all
          //     other values)"
          //
          // FIXME(emilio): The only test that covers this is
          // table-anonymous-objects-211.xht, which fails on other browsers (but
          // differently to us, if you just remove this condition).
          return true;
        }
        if (mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
            !mStyleDisplay->IsAbsolutelyPositionedStyle()) {
          // Floats are treated as inline-level and also shrink-wrap.
          return true;
        }
        return false;
      }();

      if (mParentReflowInput->mFlags.mOrthogonalCellFinalReflow) {
        // This is the "extra" reflow for the inner content of an orthogonal
        // table cell, after the row size has been determined; so we want to
        // directly use the cell size without further adjustment.
        const auto* cell = mFrame->GetParent();
        MOZ_ASSERT(cell->IsTableCellFrame(),
                   "unexpected mOrthogonalCellFinalReflow flag!");
        cbSize = LogicalSize(wm, cell->GetPaddingRectRelativeToSelf().Size());
        SetAvailableISize(cbSize.ISize(wm));
      } else {
        const bool shouldShrinkWrap = [&] {
          if (isInlineLevel) {
            return true;
          }
          if (mFlags.mIsReplaced && !alignCB->IsFlexOrGridContainer()) {
            // Shrink-wrap replaced elements when in-flow (out of flows are
            // handled above). We exclude replaced elements in grid or flex
            // contexts, where we don't want to shrink-wrap unconditionally (so
            // that stretching can happen). When grid/flex explicitly want
            // shrink-wrapping, they can request it directly using the relevant
            // flag.
            return true;
          }
          if (!alignCB->IsGridContainerFrame() &&
              mWritingMode.IsOrthogonalTo(alignCB->GetWritingMode())) {
            // Shrink-wrap blocks that are orthogonal to their container (unless
            // we're in a grid?)
            return true;
          }
          return false;
        }();

        if (shouldShrinkWrap) {
          mComputeSizeFlags += ComputeSizeFlag::ShrinkWrap;
        }

        if (cbSize.ISize(wm) == NS_UNCONSTRAINEDSIZE) {
          // For orthogonal flows, where we found a parent orthogonal-limit for
          // AvailableISize() in Init(), we'll use the same here as well.
          cbSize.ISize(wm) = AvailableISize();
        }
      }

      auto size =
          mFrame->ComputeSize(mRenderingContext, wm, cbSize, AvailableISize(),
                              ComputedLogicalMargin(wm).Size(wm),
                              ComputedLogicalBorderPadding(wm).Size(wm),
                              mStyleSizeOverrides, mComputeSizeFlags);

      mComputedSize = size.mLogicalSize;
      NS_ASSERTION(ComputedISize() >= 0, "Bogus inline-size");
      NS_ASSERTION(
          ComputedBSize() == NS_UNCONSTRAINEDSIZE || ComputedBSize() >= 0,
          "Bogus block-size");

      mFlags.mIsBSizeSetByAspectRatio =
          size.mAspectRatioUsage == nsIFrame::AspectRatioUsage::ToComputeBSize;

      const bool shouldCalculateBlockSideMargins = [&]() {
        if (isInlineLevel) {
          return false;
        }
        if (mFrame->IsTableFrame()) {
          return false;
        }
        if (alignCB->IsFlexOrGridContainer()) {
          // Exclude flex and grid items.
          return false;
        }
        const auto pseudoType = mFrame->Style()->GetPseudoType();
        if (pseudoType == PseudoStyleType::marker &&
            mFrame->GetParent()->StyleList()->mListStylePosition ==
                StyleListStylePosition::Outside) {
          // Exclude outside ::markers.
          return false;
        }
        if (pseudoType == PseudoStyleType::columnContent) {
          // Exclude -moz-column-content since it cannot have any margin.
          return false;
        }
        return true;
      }();

      if (shouldCalculateBlockSideMargins) {
        CalculateBlockSideMargins();
      }
    }
  }

  // Save our containing block dimensions
  mContainingBlockSize = cbSize;
}

static void UpdateProp(nsIFrame* aFrame,
                       const FramePropertyDescriptor<nsMargin>* aProperty,
                       bool aNeeded, const nsMargin& aNewValue) {
  if (aNeeded) {
    if (nsMargin* propValue = aFrame->GetProperty(aProperty)) {
      *propValue = aNewValue;
    } else {
      aFrame->AddProperty(aProperty, new nsMargin(aNewValue));
    }
  } else {
    aFrame->RemoveProperty(aProperty);
  }
}

void SizeComputationInput::InitOffsets(WritingMode aCBWM, nscoord aPercentBasis,
                                       LayoutFrameType aFrameType,
                                       ComputeSizeFlags aFlags,
                                       const Maybe<LogicalMargin>& aBorder,
                                       const Maybe<LogicalMargin>& aPadding,
                                       const nsStyleDisplay* aDisplay) {
  nsPresContext* presContext = mFrame->PresContext();

  // Compute margins from the specified margin style information. These
  // become the default computed values, and may be adjusted below
  // XXX fix to provide 0,0 for the top&bottom margins for
  // inline-non-replaced elements
  bool needMarginProp = ComputeMargin(aCBWM, aPercentBasis, aFrameType);
  // Note that ComputeMargin() simplistically resolves 'auto' margins to 0.
  // In formatting contexts where this isn't correct, some later code will
  // need to update the UsedMargin() property with the actual resolved value.
  // One example of this is ::CalculateBlockSideMargins().
  ::UpdateProp(mFrame, nsIFrame::UsedMarginProperty(), needMarginProp,
               ComputedPhysicalMargin());

  const WritingMode wm = GetWritingMode();
  const nsStyleDisplay* disp = mFrame->StyleDisplayWithOptionalParam(aDisplay);
  bool needPaddingProp;
  LayoutDeviceIntMargin widgetPadding;
  if (mIsThemed && presContext->Theme()->GetWidgetPadding(
                       presContext->DeviceContext(), mFrame,
                       disp->EffectiveAppearance(), &widgetPadding)) {
    const nsMargin padding = LayoutDevicePixel::ToAppUnits(
        widgetPadding, presContext->AppUnitsPerDevPixel());
    SetComputedLogicalPadding(wm, LogicalMargin(wm, padding));
    needPaddingProp = false;
  } else if (mFrame->IsInSVGTextSubtree()) {
    SetComputedLogicalPadding(wm, LogicalMargin(wm));
    needPaddingProp = false;
  } else if (aPadding) {  // padding is an input arg
    SetComputedLogicalPadding(wm, *aPadding);
    nsMargin stylePadding;
    // If the caller passes a padding that doesn't match our style (like
    // nsTextControlFrame might due due to theming), then we also need a
    // padding prop.
    needPaddingProp = !mFrame->StylePadding()->GetPadding(stylePadding) ||
                      aPadding->GetPhysicalMargin(wm) != stylePadding;
  } else {
    needPaddingProp = ComputePadding(aCBWM, aPercentBasis, aFrameType);
  }

  // Add [align|justify]-content:baseline padding contribution.
  typedef const FramePropertyDescriptor<SmallValueHolder<nscoord>>* Prop;
  auto ApplyBaselinePadding = [this, wm, &needPaddingProp](LogicalAxis aAxis,
                                                           Prop aProp) {
    bool found;
    nscoord val = mFrame->GetProperty(aProp, &found);
    if (found) {
      NS_ASSERTION(val != nscoord(0), "zero in this property is useless");
      LogicalSide side;
      if (val > 0) {
        side = MakeLogicalSide(aAxis, LogicalEdge::Start);
      } else {
        side = MakeLogicalSide(aAxis, LogicalEdge::End);
        val = -val;
      }
      mComputedPadding.Side(side, wm) += val;
      needPaddingProp = true;
      if (aAxis == LogicalAxis::Block && val > 0) {
        // We have a baseline-adjusted block-axis start padding, so
        // we need this to mark lines dirty when mIsBResize is true:
        this->mFrame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
      }
    }
  };
  if (!aFlags.contains(ComputeSizeFlag::IsGridMeasuringReflow)) {
    ApplyBaselinePadding(LogicalAxis::Block, nsIFrame::BBaselinePadProperty());
  }
  if (!aFlags.contains(ComputeSizeFlag::ShrinkWrap)) {
    ApplyBaselinePadding(LogicalAxis::Inline, nsIFrame::IBaselinePadProperty());
  }

  LogicalMargin border(wm);
  if (mIsThemed) {
    const LayoutDeviceIntMargin widgetBorder =
        presContext->Theme()->GetWidgetBorder(
            presContext->DeviceContext(), mFrame, disp->EffectiveAppearance());
    border = LogicalMargin(
        wm, LayoutDevicePixel::ToAppUnits(widgetBorder,
                                          presContext->AppUnitsPerDevPixel()));
  } else if (mFrame->IsInSVGTextSubtree()) {
    // Do nothing since the border local variable is initialized all zero.
  } else if (aBorder) {  // border is an input arg
    border = *aBorder;
  } else {
    border = LogicalMargin(wm, mFrame->StyleBorder()->GetComputedBorder());
  }
  SetComputedLogicalBorderPadding(wm, border + ComputedLogicalPadding(wm));

  if (aFrameType == LayoutFrameType::Scrollbar) {
    // scrollbars may have had their width or height smashed to zero
    // by the associated scrollframe, in which case we must not report
    // any padding or border.
    nsSize size(mFrame->GetSize());
    if (size.width == 0 || size.height == 0) {
      SetComputedLogicalPadding(wm, LogicalMargin(wm));
      SetComputedLogicalBorderPadding(wm, LogicalMargin(wm));
    }
  }

  bool hasPaddingChange;
  if (nsMargin* oldPadding =
          mFrame->GetProperty(nsIFrame::UsedPaddingProperty())) {
    // Note: If a padding change is already detectable without resolving the
    // percentage, e.g. a padding is changing from 50px to 50%,
    // nsIFrame::DidSetComputedStyle() will cache the old padding in
    // UsedPaddingProperty().
    hasPaddingChange = *oldPadding != ComputedPhysicalPadding();
  } else {
    // Our padding may have changed, but we can't tell at this point.
    hasPaddingChange = needPaddingProp;
  }
  // Keep mHasPaddingChange bit set until we've done reflow. We'll clear it in
  // nsIFrame::DidReflow()
  mFrame->SetHasPaddingChange(mFrame->HasPaddingChange() || hasPaddingChange);

  ::UpdateProp(mFrame, nsIFrame::UsedPaddingProperty(), needPaddingProp,
               ComputedPhysicalPadding());
}

// This code enforces section 10.3.3 of the CSS2 spec for this formula:
//
// 'margin-left' + 'border-left-width' + 'padding-left' + 'width' +
//   'padding-right' + 'border-right-width' + 'margin-right'
//   = width of containing block
//
// Note: the width unit is not auto when this is called
void ReflowInput::CalculateBlockSideMargins() {
  MOZ_ASSERT(!mFrame->IsTableFrame(),
             "Inner table frame cannot have computed margins!");

  // Calculations here are done in the containing block's writing mode,
  // which is where margins will eventually be applied: we're calculating
  // margins that will be used by the container in its inline direction,
  // which in the case of an orthogonal contained block will correspond to
  // the block direction of this reflow input. So in the orthogonal-flow
  // case, "CalculateBlock*Side*Margins" will actually end up adjusting
  // the BStart/BEnd margins; those are the "sides" of the block from its
  // container's point of view.
  WritingMode cbWM = GetCBWritingMode();

  nscoord availISizeCBWM = AvailableSize(cbWM).ISize(cbWM);
  nscoord computedISizeCBWM = ComputedSize(cbWM).ISize(cbWM);
  if (computedISizeCBWM == NS_UNCONSTRAINEDSIZE) {
    // For orthogonal flows, where we found a parent orthogonal-limit
    // for AvailableISize() in Init(), we don't have meaningful sizes to
    // adjust.  Act like the sum is already correct (below).
    return;
  }

  LAYOUT_WARN_IF_FALSE(NS_UNCONSTRAINEDSIZE != computedISizeCBWM &&
                           NS_UNCONSTRAINEDSIZE != availISizeCBWM,
                       "have unconstrained inline-size; this should only "
                       "result from very large sizes, not attempts at "
                       "intrinsic inline-size calculation");

  LogicalMargin margin = ComputedLogicalMargin(cbWM);
  LogicalMargin borderPadding = ComputedLogicalBorderPadding(cbWM);
  nscoord sum = margin.IStartEnd(cbWM) + borderPadding.IStartEnd(cbWM) +
                computedISizeCBWM;
  if (sum == availISizeCBWM) {
    // The sum is already correct
    return;
  }

  // Determine the start and end margin values. The isize value
  // remains constant while we do this.

  // Calculate how much space is available for margins
  nscoord availMarginSpace = availISizeCBWM - sum;

  // If the available margin space is negative, then don't follow the
  // usual overconstraint rules.
  if (availMarginSpace < 0) {
    margin.IEnd(cbWM) += availMarginSpace;
    SetComputedLogicalMargin(cbWM, margin);
    return;
  }

  const auto positionProperty = mStyleDisplay->mPosition;
  // The css2 spec clearly defines how block elements should behave
  // in section 10.3.3.
  bool isAutoStartMargin =
      mStyleMargin->GetMargin(LogicalSide::IStart, cbWM, positionProperty)
          ->IsAuto();
  bool isAutoEndMargin =
      mStyleMargin->GetMargin(LogicalSide::IEnd, cbWM, positionProperty)
          ->IsAuto();
  if (!isAutoStartMargin && !isAutoEndMargin) {
    // Neither margin is 'auto' so we're over constrained. Use the
    // 'direction' property of the parent to tell which margin to
    // ignore
    // First check if there is an HTML alignment that we should honor
    const StyleTextAlign* textAlign =
        mParentReflowInput
            ? &mParentReflowInput->mFrame->StyleText()->mTextAlign
            : nullptr;
    if (textAlign && (*textAlign == StyleTextAlign::MozLeft ||
                      *textAlign == StyleTextAlign::MozCenter ||
                      *textAlign == StyleTextAlign::MozRight)) {
      if (mParentReflowInput->mWritingMode.IsBidiLTR()) {
        isAutoStartMargin = *textAlign != StyleTextAlign::MozLeft;
        isAutoEndMargin = *textAlign != StyleTextAlign::MozRight;
      } else {
        isAutoStartMargin = *textAlign != StyleTextAlign::MozRight;
        isAutoEndMargin = *textAlign != StyleTextAlign::MozLeft;
      }
    }
    // Otherwise apply the CSS rules, and ignore one margin by forcing
    // it to 'auto', depending on 'direction'.
    else {
      isAutoEndMargin = true;
    }
  }

  // Logic which is common to blocks and tables
  // The computed margins need not be zero because the 'auto' could come from
  // overconstraint or from HTML alignment so values need to be accumulated

  if (isAutoStartMargin) {
    if (isAutoEndMargin) {
      // Both margins are 'auto' so the computed addition should be equal
      nscoord forStart = availMarginSpace / 2;
      margin.IStart(cbWM) += forStart;
      margin.IEnd(cbWM) += availMarginSpace - forStart;
    } else {
      margin.IStart(cbWM) += availMarginSpace;
    }
  } else if (isAutoEndMargin) {
    margin.IEnd(cbWM) += availMarginSpace;
  }
  SetComputedLogicalMargin(cbWM, margin);

  if (isAutoStartMargin || isAutoEndMargin) {
    // Update the UsedMargin property if we were tracking it already.
    nsMargin* propValue = mFrame->GetProperty(nsIFrame::UsedMarginProperty());
    if (propValue) {
      *propValue = margin.GetPhysicalMargin(cbWM);
    }
  }
}

// For "normal" we use the font's normal line height (em height + leading).
// If both internal leading and  external leading specified by font itself are
// zeros, we should compensate this by creating extra (external) leading.
// This is necessary because without this compensation, normal line height might
// look too tight.
static nscoord GetNormalLineHeight(nsFontMetrics* aFontMetrics) {
  MOZ_ASSERT(aFontMetrics, "no font metrics");
  nscoord externalLeading = aFontMetrics->ExternalLeading();
  nscoord internalLeading = aFontMetrics->InternalLeading();
  nscoord emHeight = aFontMetrics->EmHeight();
  if (!internalLeading && !externalLeading) {
    return NSToCoordRound(static_cast<float>(emHeight) *
                          ReflowInput::kNormalLineHeightFactor);
  }
  return emHeight + internalLeading + externalLeading;
}

static inline nscoord ComputeLineHeight(const StyleLineHeight& aLh,
                                        const nsFont& aFont, nsAtom* aLanguage,
                                        bool aExplicitLanguage,
                                        nsPresContext* aPresContext,
                                        bool aIsVertical, nscoord aBlockBSize,
                                        float aFontSizeInflation) {
  if (aLh.IsLength()) {
    nscoord result = aLh.AsLength().ToAppUnits();
    if (aFontSizeInflation != 1.0f) {
      result = NSToCoordRound(static_cast<float>(result) * aFontSizeInflation);
    }
    return result;
  }

  if (aLh.IsNumber()) {
    // For factor units the computed value of the line-height property
    // is found by multiplying the factor by the font's computed size
    // (adjusted for min-size prefs and text zoom).
    return aFont.size.ScaledBy(aLh.AsNumber() * aFontSizeInflation)
        .ToAppUnits();
  }

  MOZ_ASSERT(aLh.IsNormal() || aLh.IsMozBlockHeight());
  if (aLh.IsMozBlockHeight() && aBlockBSize != NS_UNCONSTRAINEDSIZE) {
    return aBlockBSize;
  }

  auto size = aFont.size;
  size.ScaleBy(aFontSizeInflation);

  if (aPresContext) {
    nsFont font = aFont;
    font.size = size;
    nsFontMetrics::Params params;
    params.language = aLanguage;
    params.explicitLanguage = aExplicitLanguage;
    params.orientation =
        aIsVertical ? nsFontMetrics::eVertical : nsFontMetrics::eHorizontal;
    params.userFontSet = aPresContext->GetUserFontSet();
    params.textPerf = aPresContext->GetTextPerfMetrics();
    params.featureValueLookup = aPresContext->GetFontFeatureValuesLookup();
    RefPtr<nsFontMetrics> fm = aPresContext->GetMetricsFor(font, params);
    return GetNormalLineHeight(fm);
  }
  // If we don't have a pres context, use a 1.2em fallback.
  size.ScaleBy(ReflowInput::kNormalLineHeightFactor);
  return size.ToAppUnits();
}

nscoord ReflowInput::GetLineHeight() const {
  if (mLineHeight != NS_UNCONSTRAINEDSIZE) {
    return mLineHeight;
  }

  nscoord blockBSize = nsLayoutUtils::IsNonWrapperBlock(mFrame)
                           ? ComputedBSize()
                           : (mCBReflowInput ? mCBReflowInput->ComputedBSize()
                                             : NS_UNCONSTRAINEDSIZE);
  mLineHeight = CalcLineHeight(*mFrame->Style(), mFrame->PresContext(),
                               mFrame->GetContent(), blockBSize,
                               nsLayoutUtils::FontSizeInflationFor(mFrame));
  return mLineHeight;
}

void ReflowInput::SetLineHeight(nscoord aLineHeight) {
  MOZ_ASSERT(aLineHeight >= 0, "aLineHeight must be >= 0!");

  if (mLineHeight != aLineHeight) {
    mLineHeight = aLineHeight;
    // Setting used line height can change a frame's block-size if mFrame's
    // block-size behaves as auto.
    InitResizeFlags(mFrame->PresContext(), mFrame->Type());
  }
}

/* static */
nscoord ReflowInput::CalcLineHeight(const ComputedStyle& aStyle,
                                    nsPresContext* aPresContext,
                                    const nsIContent* aContent,
                                    nscoord aBlockBSize,
                                    float aFontSizeInflation) {
  const StyleLineHeight& lh = aStyle.StyleFont()->mLineHeight;
  WritingMode wm(&aStyle);
  const bool vertical = wm.IsVertical() && !wm.IsSideways();
  return CalcLineHeight(lh, *aStyle.StyleFont(), aPresContext, vertical,
                        aContent, aBlockBSize, aFontSizeInflation);
}

nscoord ReflowInput::CalcLineHeight(
    const StyleLineHeight& aLh, const nsStyleFont& aRelativeToFont,
    nsPresContext* aPresContext, bool aIsVertical, const nsIContent* aContent,
    nscoord aBlockBSize, float aFontSizeInflation) {
  nscoord lineHeight =
      ComputeLineHeight(aLh, aRelativeToFont.mFont, aRelativeToFont.mLanguage,
                        aRelativeToFont.mExplicitLanguage, aPresContext,
                        aIsVertical, aBlockBSize, aFontSizeInflation);

  NS_ASSERTION(lineHeight >= 0, "ComputeLineHeight screwed up");

  const auto* input = HTMLInputElement::FromNodeOrNull(aContent);
  if (input && input->IsSingleLineTextControl()) {
    // For Web-compatibility, single-line text input elements cannot
    // have a line-height smaller than 'normal'.
    if (!aLh.IsNormal()) {
      nscoord normal = ComputeLineHeight(
          StyleLineHeight::Normal(), aRelativeToFont.mFont,
          aRelativeToFont.mLanguage, aRelativeToFont.mExplicitLanguage,
          aPresContext, aIsVertical, aBlockBSize, aFontSizeInflation);
      if (lineHeight < normal) {
        lineHeight = normal;
      }
    }
  }

  return lineHeight;
}

nscoord ReflowInput::CalcLineHeightForCanvas(const StyleLineHeight& aLh,
                                             const nsFont& aRelativeToFont,
                                             nsAtom* aLanguage,
                                             bool aExplicitLanguage,
                                             nsPresContext* aPresContext,
                                             WritingMode aWM) {
  return ComputeLineHeight(aLh, aRelativeToFont, aLanguage, aExplicitLanguage,
                           aPresContext, aWM.IsVertical() && !aWM.IsSideways(),
                           NS_UNCONSTRAINEDSIZE, 1.0f);
}

bool SizeComputationInput::ComputeMargin(WritingMode aCBWM,
                                         nscoord aPercentBasis,
                                         LayoutFrameType aFrameType) {
  // SVG text frames have no margin.
  if (mFrame->IsInSVGTextSubtree()) {
    return false;
  }

  if (aFrameType == LayoutFrameType::Table) {
    // Table frame's margin is inherited to the table wrapper frame via the
    // ::-moz-table-wrapper rule in ua.css, so don't set any margins for it.
    SetComputedLogicalMargin(mWritingMode, LogicalMargin(mWritingMode));
    return false;
  }

  // If style style can provide us the margin directly, then use it.
  const nsStyleMargin* styleMargin = mFrame->StyleMargin();

  nsMargin margin;
  const bool isLayoutDependent = !styleMargin->GetMargin(margin);
  if (isLayoutDependent) {
    // We have to compute the value. Note that this calculation is
    // performed according to the writing mode of the containing block
    // (http://dev.w3.org/csswg/css-writing-modes-3/#orthogonal-flows)
    if (aPercentBasis == NS_UNCONSTRAINEDSIZE) {
      aPercentBasis = 0;
    }
    LogicalMargin m(aCBWM);
    const auto positionProperty = mFrame->StyleDisplay()->mPosition;
    for (const LogicalSide side : LogicalSides::All) {
      m.Side(side, aCBWM) = nsLayoutUtils::ComputeCBDependentValue(
          aPercentBasis, styleMargin->GetMargin(side, aCBWM, positionProperty));
    }
    SetComputedLogicalMargin(aCBWM, m);
  } else {
    SetComputedLogicalMargin(mWritingMode, LogicalMargin(mWritingMode, margin));
  }

  // ... but font-size-inflation-based margin adjustment uses the
  // frame's writing mode
  nscoord marginAdjustment = FontSizeInflationListMarginAdjustment(mFrame);

  if (marginAdjustment > 0) {
    LogicalMargin m = ComputedLogicalMargin(mWritingMode);
    m.IStart(mWritingMode) += marginAdjustment;
    SetComputedLogicalMargin(mWritingMode, m);
  }

  return isLayoutDependent;
}

bool SizeComputationInput::ComputePadding(WritingMode aCBWM,
                                          nscoord aPercentBasis,
                                          LayoutFrameType aFrameType) {
  // If style can provide us the padding directly, then use it.
  const nsStylePadding* stylePadding = mFrame->StylePadding();
  nsMargin padding;
  bool isCBDependent = !stylePadding->GetPadding(padding);
  // a table row/col group, row/col doesn't have padding
  // XXXldb Neither do border-collapse tables.
  if (LayoutFrameType::TableRowGroup == aFrameType ||
      LayoutFrameType::TableColGroup == aFrameType ||
      LayoutFrameType::TableRow == aFrameType ||
      LayoutFrameType::TableCol == aFrameType) {
    SetComputedLogicalPadding(mWritingMode, LogicalMargin(mWritingMode));
  } else if (isCBDependent) {
    // We have to compute the value. This calculation is performed
    // according to the writing mode of the containing block
    // (http://dev.w3.org/csswg/css-writing-modes-3/#orthogonal-flows)
    // clamp negative calc() results to 0
    if (aPercentBasis == NS_UNCONSTRAINEDSIZE) {
      aPercentBasis = 0;
    }
    LogicalMargin p(aCBWM);
    for (const LogicalSide side : LogicalSides::All) {
      p.Side(side, aCBWM) = std::max(
          0, nsLayoutUtils::ComputeCBDependentValue(
                 aPercentBasis, stylePadding->mPadding.Get(side, aCBWM)));
    }
    SetComputedLogicalPadding(aCBWM, p);
  } else {
    SetComputedLogicalPadding(mWritingMode,
                              LogicalMargin(mWritingMode, padding));
  }
  return isCBDependent;
}

void ReflowInput::ComputeMinMaxValues(const LogicalSize& aCBSize) {
  WritingMode wm = GetWritingMode();

  const auto positionProperty = mStyleDisplay->mPosition;
  const auto minISize = mStylePosition->MinISize(wm, positionProperty);
  const auto maxISize = mStylePosition->MaxISize(wm, positionProperty);
  const auto minBSize = mStylePosition->MinBSize(wm, positionProperty);
  const auto maxBSize = mStylePosition->MaxBSize(wm, positionProperty);

  LogicalSize minWidgetSize(wm);
  if (mIsThemed) {
    nsPresContext* pc = mFrame->PresContext();
    const LayoutDeviceIntSize widget = pc->Theme()->GetMinimumWidgetSize(
        pc, mFrame, mStyleDisplay->EffectiveAppearance());

    // Convert themed widget's physical dimensions to logical coords.
    minWidgetSize = {
        wm, LayoutDeviceIntSize::ToAppUnits(widget, pc->AppUnitsPerDevPixel())};

    // GetMinimumWidgetSize() returns border-box; we need content-box.
    minWidgetSize -= ComputedLogicalBorderPadding(wm).Size(wm);
  }

  // NOTE: min-width:auto resolves to 0, except on a flex item. (But
  // even there, it's supposed to be ignored (i.e. treated as 0) until
  // the flex container explicitly resolves & considers it.)
  if (minISize->IsAuto()) {
    SetComputedMinISize(0);
  } else {
    SetComputedMinISize(
        ComputeISizeValue(aCBSize, mStylePosition->mBoxSizing, *minISize));
  }

  if (mIsThemed) {
    SetComputedMinISize(std::max(ComputedMinISize(), minWidgetSize.ISize(wm)));
  }

  if (maxISize->IsNone()) {
    // Specified value of 'none'
    SetComputedMaxISize(NS_UNCONSTRAINEDSIZE);
  } else {
    SetComputedMaxISize(
        ComputeISizeValue(aCBSize, mStylePosition->mBoxSizing, *maxISize));
  }

  // If the computed value of 'min-width' is greater than the value of
  // 'max-width', 'max-width' is set to the value of 'min-width'
  if (ComputedMinISize() > ComputedMaxISize()) {
    SetComputedMaxISize(ComputedMinISize());
  }

  // Check for percentage based values and a containing block height that
  // depends on the content height. Treat them like the initial value.
  // Likewise, check for calc() with percentages on internal table elements;
  // that's treated as the initial value too.
  const bool isInternalTableFrame = IsInternalTableFrame();
  const nscoord& bPercentageBasis = aCBSize.BSize(wm);
  auto BSizeBehavesAsInitialValue = [&](const auto& aBSize) {
    if (nsLayoutUtils::IsAutoBSize(aBSize, bPercentageBasis)) {
      return true;
    }
    if (isInternalTableFrame) {
      return aBSize.HasLengthAndPercentage();
    }
    return false;
  };

  // NOTE: min-height:auto resolves to 0, except on a flex item. (But
  // even there, it's supposed to be ignored (i.e. treated as 0) until
  // the flex container explicitly resolves & considers it.)
  if (BSizeBehavesAsInitialValue(*minBSize)) {
    SetComputedMinBSize(0);
  } else {
    SetComputedMinBSize(ComputeBSizeValueHandlingStretch(
        bPercentageBasis, mStylePosition->mBoxSizing, *minBSize));
  }

  if (mIsThemed) {
    SetComputedMinBSize(std::max(ComputedMinBSize(), minWidgetSize.BSize(wm)));
  }

  if (BSizeBehavesAsInitialValue(*maxBSize)) {
    // Specified value of 'none'
    SetComputedMaxBSize(NS_UNCONSTRAINEDSIZE);
  } else {
    SetComputedMaxBSize(ComputeBSizeValueHandlingStretch(
        bPercentageBasis, mStylePosition->mBoxSizing, *maxBSize));
  }

  // If the computed value of 'min-height' is greater than the value of
  // 'max-height', 'max-height' is set to the value of 'min-height'
  if (ComputedMinBSize() > ComputedMaxBSize()) {
    SetComputedMaxBSize(ComputedMinBSize());
  }
}

bool ReflowInput::IsInternalTableFrame() const {
  return mFrame->IsTableRowGroupFrame() || mFrame->IsTableColGroupFrame() ||
         mFrame->IsTableRowFrame() || mFrame->IsTableCellFrame();
}
