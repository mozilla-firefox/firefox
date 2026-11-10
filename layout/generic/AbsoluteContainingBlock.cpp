/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * code for managing absolutely positioned children of a rendering
 * object that is a containing block for them
 */

#include "mozilla/AbsoluteContainingBlock.h"

#include "AnchorPositioningUtils.h"
#include "mozilla/CSSAlignUtils.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/ViewportFrame.h"
#include "mozilla/WritingModes.h"
#include "mozilla/dom/ViewTransition.h"
#include "nsBlockFrame.h"
#include "nsCSSFrameConstructor.h"
#include "nsContainerFrame.h"
#include "nsGridContainerFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"

#ifdef DEBUG
#  include "fmt/format.h"
#  include "nsBlockFrame.h"
#endif

using namespace mozilla;

void AbsoluteContainingBlock::SetInitialChildList(nsIFrame* aDelegatingFrame,
                                                  FrameChildListID aListID,
                                                  nsFrameList&& aChildList) {
  MOZ_ASSERT(aListID == FrameChildListID::Absolute, "unexpected child list");
#ifdef DEBUG
  nsIFrame::VerifyDirtyBitSet(aChildList);
  for (nsIFrame* f : aChildList) {
    MOZ_ASSERT(f->GetParent() == aDelegatingFrame, "Unexpected parent");
  }
#endif
  mAbsoluteFrames = std::move(aChildList);
}

void AbsoluteContainingBlock::AppendFrames(nsIFrame* aDelegatingFrame,
                                           FrameChildListID aListID,
                                           nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Absolute, "unexpected child list");

  // Append the frames to our list of absolutely positioned frames
#ifdef DEBUG
  nsIFrame::VerifyDirtyBitSet(aFrameList);
#endif
  mAbsoluteFrames.AppendFrames(nullptr, std::move(aFrameList));

  // no damage to intrinsic widths, since absolutely positioned frames can't
  // change them
  aDelegatingFrame->PresShell()->FrameNeedsReflow(
      aDelegatingFrame, IntrinsicDirty::None, NS_FRAME_HAS_DIRTY_CHILDREN);
}

void AbsoluteContainingBlock::InsertFrames(nsIFrame* aDelegatingFrame,
                                           FrameChildListID aListID,
                                           nsIFrame* aPrevFrame,
                                           nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Absolute, "unexpected child list");
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == aDelegatingFrame,
               "inserting after sibling frame with different parent");

#ifdef DEBUG
  nsIFrame::VerifyDirtyBitSet(aFrameList);
#endif
  mAbsoluteFrames.InsertFrames(nullptr, aPrevFrame, std::move(aFrameList));

  // no damage to intrinsic widths, since absolutely positioned frames can't
  // change them
  aDelegatingFrame->PresShell()->FrameNeedsReflow(
      aDelegatingFrame, IntrinsicDirty::None, NS_FRAME_HAS_DIRTY_CHILDREN);
}

void AbsoluteContainingBlock::RemoveFrame(FrameDestroyContext& aContext,
                                          FrameChildListID aListID,
                                          nsIFrame* aOldFrame) {
  MOZ_ASSERT(aListID == FrameChildListID::Absolute, "unexpected child list");

  AutoTArray<nsIFrame*, 8> delFrames;
  for (nsIFrame* f = aOldFrame; f; f = f->GetNextInFlow()) {
    delFrames.AppendElement(f);
  }
  for (nsIFrame* delFrame : Reversed(delFrames)) {
    delFrame->GetParent()->GetAbsoluteContainingBlock()->StealFrame(delFrame);
    delFrame->Destroy(aContext);
  }
}

// In a fragmented context, for an absolutely positioned frame, this property
// stores the logical border-box position that the frame would have, if its
// abspos containing block were not being fragmented. The value for this
// property is determined by performing a special reflow on the abspos
// containing block (or a larger subtree that includes it), with an
// unconstrained available block-size.
//
// The position is relative to the absolute containing block's border-box, and
// is stored in the containing block's writing mode.
//
// Note: caller should use GetUnfragmentedPosition() helper to get the property.
NS_DECLARE_FRAME_PROPERTY_DELETABLE(UnfragmentedPositionProperty, LogicalPoint)

// Corresponding property to above, for the size of an absolutely positioned
// frame. However, there are important distinctions to note:
// 1. Writing mode is that of the absolutely positioned frame's.
// 2. Stores border-box size for box-sizing: border-box, or content box size for
//    box-sizing: content-box.
NS_DECLARE_FRAME_PROPERTY_DELETABLE(UnfragmentedSizeProperty, LogicalSize)

// In a fragmented context, for an absolute containing block, this property
// stores the unfragmented containing block rects. This is used to allow
// proper percentage-sizing of its children.
NS_DECLARE_FRAME_PROPERTY_DELETABLE(
    UnfragmentedContainingBlockProperty,
    AbsoluteContainingBlock::ContainingBlockRects)

static LogicalPoint* GetUnfragmentedPosition(const ReflowInput& aCBReflowInput,
                                             const nsIFrame* aFrame) {
  // If the absolute containing block is in a measuring reflow, then aFrame's
  // unfragmented position is going to be updated. Don't return the obsolete
  // value in the property.
  return aCBReflowInput.mFlags.mIsInFragmentainerMeasuringReflow
             ? nullptr
             : aFrame->GetProperty(UnfragmentedPositionProperty());
}

static LogicalSize* GetUnfragmentedSize(const ReflowInput& aCBReflowInput,
                                        const nsIFrame* aFrame) {
  return aCBReflowInput.mFlags.mIsInFragmentainerMeasuringReflow
             ? nullptr
             // Later fragment frames need to know the size for resolving
             // automatic sizes.
             : aFrame->FirstInFlow()->GetProperty(UnfragmentedSizeProperty());
}

// Return aInlineFrame's previous inline continuation if it shares
// aInlineFrame's fragmentainer, or nullptr otherwise.
static nsIFrame* GetPrevInlineContinuationInFragmentainer(
    const nsIFrame* aInlineFrame) {
  MOZ_ASSERT(aInlineFrame->IsInlineFrameOrSubclass());
  const nsBlockFrame* myBlock =
      nsLayoutUtils::FindNearestBlockAncestor(aInlineFrame);
  for (nsIFrame* prev =
           nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(aInlineFrame);
       prev; prev = nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(prev)) {
    if (prev->IsBlockFrameOrSubclass()) {
      // Skip IB-split block siblings.
      continue;
    }
    return nsLayoutUtils::FindNearestBlockAncestor(prev) == myBlock ? prev
                                                                    : nullptr;
  }
  return nullptr;
}

// Return true if aInlineFrame is the first inline continuation in its
// fragmentainer, i.e. no previous continuation shares its nearest block
// ancestor.
static bool IsFirstInlineContinuationInFragmentainer(
    const nsIFrame* aInlineFrame) {
  return !GetPrevInlineContinuationInFragmentainer(aInlineFrame);
}

nsIFrame* AbsoluteContainingBlock::FirstInlineContinuationInFragmentainer(
    nsIFrame* aInlineFrame) {
  MOZ_ASSERT(aInlineFrame->IsInlineFrameOrSubclass());
  nsIFrame* first = aInlineFrame;
  while (nsIFrame* prev = GetPrevInlineContinuationInFragmentainer(first)) {
    first = prev;
  }
  return first;
}

// Walk aInlineFrame's continuation chain and return the *first* continuation
// (near the start-most edges) of the previous fragmentainer (not the first
// fragment we encountered during the walk). Or return nullptr if no such
// continuation in the previous fragmentainer or no previous fragmentainer.
static nsIFrame* GetFirstInlineContinuationInPrevFragmentainer(
    const nsIFrame* aInlineFrame) {
  MOZ_ASSERT(aInlineFrame->IsInlineFrameOrSubclass());
  // An inline always has a block ancestor, and that block is continued per
  // fragmentainer, so two continuations share a fragmentainer iff their nearest
  // block ancestors are the same frame.
  const nsBlockFrame* myBlock =
      nsLayoutUtils::FindNearestBlockAncestor(aInlineFrame);
  nsIFrame* candidate = nullptr;
  const nsBlockFrame* candidateBlock = nullptr;
  for (nsIFrame* prev =
           nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(aInlineFrame);
       prev; prev = nsLayoutUtils::GetPrevContinuationOrIBSplitSibling(prev)) {
    if (prev->IsBlockFrameOrSubclass()) {
      // Skip IB-split block siblings.
      continue;
    }
    const nsBlockFrame* prevBlock =
        nsLayoutUtils::FindNearestBlockAncestor(prev);
    if (prevBlock == myBlock) {
      continue;
    }
    if (!candidate) {
      candidate = prev;
      candidateBlock = prevBlock;
    } else if (prevBlock == candidateBlock) {
      // Continue to walk until we reach the first fragment in the previous
      // fragmentainer.
      candidate = prev;
    } else {
      // Walked back past the candidate fragmentainer. Stop here.
      break;
    }
  }
  return candidate;
}

// Walk aInlineFrame's continuation chain and return the *first* continuation
// near the start-most edges of the next fragmentainer. Or return nullptr if no
// such fragment in the next fragmentainer or no next fragmentainer.
static nsIFrame* GetFirstInlineContinuationInNextFragmentainer(
    const nsIFrame* aInlineFrame) {
  MOZ_ASSERT(aInlineFrame->IsInlineFrameOrSubclass());
  const nsBlockFrame* myBlock =
      nsLayoutUtils::FindNearestBlockAncestor(aInlineFrame);
  for (nsIFrame* next =
           nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aInlineFrame);
       next; next = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(next)) {
    if (next->IsBlockFrameOrSubclass()) {
      // Skip IB-split block siblings.
      continue;
    }
    if (nsLayoutUtils::FindNearestBlockAncestor(next) != myBlock) {
      return next;
    }
  }
  return nullptr;
}

// Return aFrame's first fragment in the previous fragmentainer, or nullptr if
// there is none. For a block CB this is just the prev-in-flow. For an inline
// CB, multiple fragments can live in a fragmentainer, but only the first
// fragment in each fragmentainer owns and reflows the abspos children, so we
// walk back to that first fragment rather than the immediate prev-in-flow.
static nsIFrame* GetFirstContinuationInPrevFragmentainer(
    const nsIFrame* aFrame) {
  return StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
                 aFrame->IsInlineFrameOrSubclass()
             ? GetFirstInlineContinuationInPrevFragmentainer(aFrame)
             : aFrame->GetPrevInFlow();
}

// See the comment in GetFirstContinuationInPrevFragmentainer().
static nsIFrame* GetFirstContinuationInNextFragmentainer(
    const nsIFrame* aFrame) {
  return StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
                 aFrame->IsInlineFrameOrSubclass()
             ? GetFirstInlineContinuationInNextFragmentainer(aFrame)
             : aFrame->GetNextInFlow();
}

nsFrameList AbsoluteContainingBlock::StealPushedChildList() {
  return std::move(mPushedAbsoluteFrames);
}

void AbsoluteContainingBlock::DrainPushedChildList(
    const nsIFrame* aDelegatingFrame) {
  MOZ_ASSERT(aDelegatingFrame->GetAbsoluteContainingBlock() == this,
             "aDelegatingFrame's absCB should be us!");

  // Our pushed absolute child list might be non-empty if our next-in-flow
  // hasn't reflowed yet. Move any child in that list that is a first-in-flow,
  // or whose prev-in-flow is not in our absolute child list, into our absolute
  // child list.
  for (auto iter = mPushedAbsoluteFrames.begin();
       iter != mPushedAbsoluteFrames.end();) {
    // Advance the iterator first, so it's safe to move |child|.
    nsIFrame* const child = *iter++;
    if (!child->GetPrevInFlow() ||
        child->GetPrevInFlow()->GetParent() != aDelegatingFrame) {
      mPushedAbsoluteFrames.RemoveFrame(child);
      mAbsoluteFrames.AppendFrame(nullptr, child);
      if (!child->GetPrevInFlow()) {
        child->RemoveStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
      }
    }
  }
}

void AbsoluteContainingBlock::PullAbsoluteFramesFrom(
    nsContainerFrame* aDelegatingFrame, nsIFrame* aContinuation,
    PullKind aPullKind) {
  AbsoluteContainingBlock* absCB = aContinuation->GetAbsoluteContainingBlock();
  MOZ_ASSERT(absCB,
             "If this delegating frame has an absCB, aContinuation must "
             "have one, too!");

  absCB->DrainPushedChildList(aContinuation);

  for (auto iter = absCB->GetChildList().begin();
       iter != absCB->GetChildList().end();) {
    // Advance the iterator first, so it's safe to move |child|.
    nsIFrame* const child = *iter++;
    nsIFrame* const childPrevInFlow = child->GetPrevInFlow();
    if (aPullKind == PullKind::FirstInFlows && childPrevInFlow) {
      continue;
    }
    absCB->StealFrame(child);
    if (childPrevInFlow && childPrevInFlow->GetParent() == aDelegatingFrame) {
      // We already hold child's prev-in-flow, so child should be appended
      // into our pushed child list to keep the continuations in order.
      mPushedAbsoluteFrames.AppendFrame(aDelegatingFrame, child);
    } else {
      // Either child is a first-in-flow or its prev-in-flow lives in another
      // absolute containing block, so it moves into our child list to be
      // reflowed.
      mAbsoluteFrames.AppendFrame(aDelegatingFrame, child);
      if (!childPrevInFlow) {
        child->RemoveStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
      }
    }
  }

  if (aPullKind == PullKind::All) {
    // We are told to pull every frame from aContinuation. Move aContinuation's
    // pushed child list by appending into our pushed child list.
    nsFrameList pushedFrames = absCB->StealPushedChildList();
    if (pushedFrames.NotEmpty()) {
      mPushedAbsoluteFrames.AppendFrames(aDelegatingFrame,
                                         std::move(pushedFrames));
    }
  }
}

bool AbsoluteContainingBlock::PrepareAbsoluteFrames(
    nsContainerFrame* aDelegatingFrame, PrepareReason aReason) {
  if (const nsIFrame* prev =
          GetFirstContinuationInPrevFragmentainer(aDelegatingFrame)) {
    AbsoluteContainingBlock* prevAbsCB = prev->GetAbsoluteContainingBlock();
    MOZ_ASSERT(prevAbsCB,
               "If this delegating frame has an absCB, |prev| must "
               "have one, too!");

    // Prepend the pushed absolute frames from the previous absCB to our
    // absolute child list.
    nsFrameList pushedFrames = prevAbsCB->StealPushedChildList();
    if (pushedFrames.NotEmpty()) {
      mAbsoluteFrames.InsertFrames(aDelegatingFrame, nullptr,
                                   std::move(pushedFrames));
    }
  }

  // Similarly, for any children in our pushed child list that don't have a
  // prev-in-flow in our regular child list, we move those children back into
  // our child list.
  DrainPushedChildList(aDelegatingFrame);

  // After column balancing chooses a larger column height, inline continuations
  // that hold abspos children in a later column may be moved into the current
  // fragmentainer. Reparent those abspos children under us (the first
  // continuation in the fragmentainer) so that the rest of the
  // same-fragmentainer continuations don't have any abspos children. We enforce
  // this invariant in SanityCheckChildLists().
  //
  // Under PrepareReason::Destroy, walk past the fragmentainer boundary as well,
  // to reparent the abspos children in later fragmentainers under us. This is
  // to prevent them from being destroyed along with their containing blocks.
  if (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
      aDelegatingFrame->IsInlineFrameOrSubclass() &&
      IsFirstInlineContinuationInFragmentainer(aDelegatingFrame)) {
    const nsBlockFrame* myBlock =
        nsLayoutUtils::FindNearestBlockAncestor(aDelegatingFrame);
    const bool stopAtFragmentainerBoundary = aReason == PrepareReason::Reflow;
    for (nsIFrame* next = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(
             aDelegatingFrame);
         next;
         next = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(next)) {
      if (next->IsBlockFrameOrSubclass()) {
        // Skip IB-split block siblings.
        continue;
      }
      if (stopAtFragmentainerBoundary &&
          nsLayoutUtils::FindNearestBlockAncestor(next) != myBlock) {
        // Reached a continuation in a later fragmentainer.
        break;
      }
      PullAbsoluteFramesFrom(aDelegatingFrame, next, PullKind::All);
    }
  }

  // Steal absolute frame's first-in-flow from the child list of our
  // continuations that appear as the first continuation in each fragmentainer.
  for (nsIFrame* next =
           GetFirstContinuationInNextFragmentainer(aDelegatingFrame);
       next; next = GetFirstContinuationInNextFragmentainer(next)) {
    PullAbsoluteFramesFrom(aDelegatingFrame, next, PullKind::FirstInFlows);
  }

  // The steps above may leave more than one continuation of the same abspos
  // frame in our child list. Ensure at most one continuation of each abspos
  // frame remains in our child list by pushing the rest to our pushed child
  // list.
  nsFrameList newPushedAbsoluteFrames;
  for (auto iter = mAbsoluteFrames.begin(); iter != mAbsoluteFrames.end();) {
    // Advance the iterator first, so it's safe to move |child|.
    nsIFrame* const child = *iter++;
    nsIFrame* const childPrevInFlow = child->GetPrevInFlow();
    if (childPrevInFlow && childPrevInFlow->GetParent() == aDelegatingFrame) {
      mAbsoluteFrames.RemoveFrame(child);
      newPushedAbsoluteFrames.AppendFrame(nullptr, child);
    }
  }
  if (newPushedAbsoluteFrames.NotEmpty()) {
    // These new pushed frames have continuations already in our child list,
    // preceding anything already deferred to a later containing block
    // continuation. Prepend to keep mPushedAbsoluteFrames in order.
    mPushedAbsoluteFrames.InsertFrames(nullptr, nullptr,
                                       std::move(newPushedAbsoluteFrames));
  }

  return HasAbsoluteFrames();
}

void AbsoluteContainingBlock::StealFrame(nsIFrame* aFrame) {
  const DebugOnly<bool> frameRemoved =
      mAbsoluteFrames.StartRemoveFrame(aFrame) ||
      mPushedAbsoluteFrames.ContinueRemoveFrame(aFrame);
  MOZ_ASSERT(frameRemoved, "Failed to find aFrame from our child lists!");
}

#ifdef DEBUG
void AbsoluteContainingBlock::SanityCheckChildLists(
    const nsIFrame* aDelegatingFrame) const {
  if (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
      aDelegatingFrame->IsInlineFrameOrSubclass() &&
      !IsFirstInlineContinuationInFragmentainer(aDelegatingFrame)) {
    // Only the first inline continuation in a fragmentainer serves as the
    // abspos containing block.
    MOZ_ASSERT(GetChildList().IsEmpty(),
               "A non-first inline continuation in a fragmentainer should not "
               "have any abspos children in the child list!");
    MOZ_ASSERT(GetPushedChildList().IsEmpty(),
               "A non-first inline continuation in a fragmentainer should not "
               "have any abspos children in the pushed child list!");
  }

  // TODO(TYLin): This is potentially O(N^2), where N is the number of
  // continuations that an abspos frame gets. Consider putting this behind an
  // about:config pref if it turns out to slow down debug builds too much.
  for (const nsIFrame* child : mAbsoluteFrames) {
    for (nsIFrame* prev = child->GetPrevInFlow(); prev;
         prev = prev->GetPrevInFlow()) {
      MOZ_ASSERT(!GetChildList().ContainsFrame(prev),
                 "It is wrong that both a child and its prev-in-flow are in "
                 "our child list!");
    }
  }

  {
    // Verify that continuations are ordered across both lists concatenated.
    // If a frame and its continuation are both present, the continuation must
    // appear later.
    nsTHashSet<const nsIFrame*> allFrames;
    for (const nsFrameList* list : {&mAbsoluteFrames, &mPushedAbsoluteFrames}) {
      for (const nsIFrame* child : *list) {
        allFrames.Insert(child);
      }
    }

    nsTHashSet<const nsIFrame*> seen;
    auto CheckOrder = [&](const nsIFrame* child) {
      seen.Insert(child);
      const nsIFrame* prev = child->GetPrevInFlow();
      if (prev && allFrames.Contains(prev)) {
        MOZ_ASSERT(seen.Contains(prev),
                   "A frame's continuation appears before the frame in "
                   "mAbsoluteFrames + mPushedAbsoluteFrames!");
      }
    };
    for (const nsFrameList* list : {&mAbsoluteFrames, &mPushedAbsoluteFrames}) {
      for (const nsIFrame* child : *list) {
        CheckOrder(child);
      }
    }
  }

  for (const nsIFrame* next = aDelegatingFrame->GetNextInFlow(); next;
       next = next->GetNextInFlow()) {
    auto* nextAbsCB = next->GetAbsoluteContainingBlock();
    MOZ_ASSERT(nextAbsCB,
               "Delegating frame's next-in-flow should have "
               "AbsoluteContainingBlock!");
    for (nsIFrame* child : nextAbsCB->GetChildList()) {
      MOZ_ASSERT(
          child->GetPrevInFlow(),
          "We should've pulled all abspos first-in-flows to our child list!");
    }
  }
}
#endif

static void MaybeMarkAncestorsAsHavingDescendantDependentOnItsStaticPos(
    nsIFrame* aFrame, nsIFrame* aContainingBlockFrame) {
  MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));
  if (!aFrame->StylePosition()->NeedsHypotheticalPositionIfAbsPos()) {
    return;
  }
  // We should have set the bit when reflowing the previous continuations
  // already.
  if (aFrame->GetPrevContinuation()) {
    return;
  }

  auto* placeholder = aFrame->GetPlaceholderFrame();
  MOZ_ASSERT(placeholder);

  // Only fixed-pos frames can escape their containing block.
  if (!placeholder->HasAnyStateBits(PLACEHOLDER_FOR_FIXEDPOS)) {
    return;
  }

  for (nsIFrame* ancestor = placeholder->GetParent(); ancestor;
       ancestor = ancestor->GetParent()) {
    // Walk towards the ancestor's first continuation. That's the only one that
    // really matters, since it's the only one restyling will look at. We also
    // flag the following continuations just so it's caught on the first
    // early-return ones just to avoid walking them over and over.
    do {
      if (ancestor->DescendantMayDependOnItsStaticPosition()) {
        return;
      }
      // Moving the containing block or anything above it would move our static
      // position as well, so no need to flag it or any of its ancestors.
      if (aFrame == aContainingBlockFrame) {
        return;
      }
      ancestor->SetDescendantMayDependOnItsStaticPosition(true);
      nsIFrame* prev = ancestor->GetPrevContinuation();
      if (!prev) {
        break;
      }
      ancestor = prev;
    } while (true);
  }
}

static bool IsSnapshotContainingBlock(const nsIFrame* aFrame) {
  return aFrame->Style()->GetPseudoType() ==
         PseudoStyleType::MozSnapshotContainingBlock;
}

static PhysicalAxes CheckEarlyCompensatingForScroll(const nsIFrame* aKidFrame) {
  // Three conditions to compensate for scroll, once a default anchor
  // exists:
  // * Used alignment property is `anchor-center`,
  // * `position-area` is not `none`, or
  // * `anchor()` function refers to default anchor, or an anchor that
  //   shares the same scroller with it.
  // First two conditions are checkable right now, so do that.
  if (!aKidFrame->StylePosition()->mPositionArea.IsNone()) {
    return PhysicalAxes{PhysicalAxis::Horizontal, PhysicalAxis::Vertical};
  }
  PhysicalAxes result;
  const auto cbwm = aKidFrame->GetParent()->GetWritingMode();
  // We don't concern ourselves with align/justify-items here, because
  // they don't apply to absolute positioned boxes [1].
  // [1]: https://drafts.csswg.org/css-align-3/#justify-self-property
  if (aKidFrame->StylePosition()->mAlignSelf._0 &
      StyleAlignFlags::ANCHOR_CENTER) {
    result +=
        cbwm.IsVertical() ? PhysicalAxis::Horizontal : PhysicalAxis::Vertical;
  }
  if (aKidFrame->StylePosition()->mJustifySelf._0 &
      StyleAlignFlags::ANCHOR_CENTER) {
    result +=
        cbwm.IsVertical() ? PhysicalAxis::Vertical : PhysicalAxis::Horizontal;
  }
  return result;
}

static AnchorPosResolutionCache PopulateAnchorResolutionCache(
    const nsIFrame* aKidFrame, AnchorPosReferenceData* aData,
    bool aReuseUnfragmentedAnchorPosReferences) {
  MOZ_ASSERT(aKidFrame->HasAnchorPosReference());
  aData->mFrameTreeDepth = aKidFrame->GetDepthInFrameTree();
  if (aReuseUnfragmentedAnchorPosReferences) [[unlikely]] {
    MOZ_ASSERT(
        aKidFrame->FirstInFlow()->HasProperty(UnfragmentedPositionProperty()));
    // We inherited reference data from unfragmented reflow, but still need to
    // repopulate the cache.
    AnchorPosDefaultAnchorCache cache;
    if (aData->mDefaultAnchorName) {
      const auto* presShell = aKidFrame->PresShell();
      cache.mAnchor = presShell->GetAnchorPosAnchor(
          ScopedNameRef{aData->mDefaultAnchorName, aData->mAnchorTreeScope},
          aKidFrame->FirstInFlow(), aData->mFrameTreeDepth);
      MOZ_ASSERT(cache.mAnchor);
      cache.mScrollContainer =
          AnchorPositioningUtils::GetNearestScrollFrame(cache.mAnchor)
              .mScrollContainer;
    }
    return {aData, cache};
  }

  // If the default anchor exists, it will likely be referenced (Except when
  // authors then use `anchor()` without referring to anchors whose nearest
  // scroller that of the default anchor, but that seems
  // counter-productive). This is a prerequisite for scroll compensation. We
  // also need to check for `anchor()` resolutions, so cache information for
  // default anchor and its scrollers right now.
  AnchorPosResolutionCache result{aData, {}};
  // Let this call populate the cache.
  const auto defaultAnchorInfo = AnchorPositioningUtils::ResolveAnchorPosRect(
      aKidFrame, aKidFrame->GetParent(),
      {nullptr, StyleCascadeLevel::Default()}, false, &result);
  if (defaultAnchorInfo) {
    aData->AdjustCompensatingForScroll(
        CheckEarlyCompensatingForScroll(aKidFrame));
  }
  return result;
}

static nsRect ComputeScrollableContainingBlock(
    const nsContainerFrame* aDelegatingFrame, const nsRect& aContainingBlock,
    const OverflowAreas* aOverflowAreas) {
  if (aOverflowAreas && aDelegatingFrame->Style()->GetPseudoType() ==
                            PseudoStyleType::MozScrolledContent) {
    // FIXME(bug 2004432): This is close enough to what we want. In practice
    // we don't want to account for relative positioning and so on, but this
    // seems good enough for now.
    ScrollContainerFrame* sf = do_QueryFrame(aDelegatingFrame->GetParent());
    // Clamp to the scrollable range.
    return sf->GetUnsnappedScrolledRectInternal(
        aOverflowAreas->ScrollableOverflow(), aContainingBlock.Size());
  }
  return aContainingBlock;
}

static SideBits GetScrollCompensatedSidesFor(
    const StylePositionArea& aPositionArea) {
  SideBits sides{SideBits::eNone};
  // The opposite side of the direction keyword is attached to the
  // position-anchor grid, which is then attached to the anchor, and so is
  // scroll compensated. `center` is constrained by the position-area grid
  // on both sides. `span-all` is unconstrained in that axis.
  if (aPositionArea.first == StylePositionAreaKeyword::Left ||
      aPositionArea.first == StylePositionAreaKeyword::SpanLeft) {
    sides |= SideBits::eRight;
  } else if (aPositionArea.first == StylePositionAreaKeyword::Right ||
             aPositionArea.first == StylePositionAreaKeyword::SpanRight) {
    sides |= SideBits::eLeft;
  } else if (aPositionArea.first == StylePositionAreaKeyword::Center) {
    sides |= SideBits::eLeftRight;
  }

  if (aPositionArea.second == StylePositionAreaKeyword::Top ||
      aPositionArea.second == StylePositionAreaKeyword::SpanTop) {
    sides |= SideBits::eBottom;
  } else if (aPositionArea.second == StylePositionAreaKeyword::Bottom ||
             aPositionArea.second == StylePositionAreaKeyword::SpanBottom) {
    sides |= SideBits::eTop;
  } else if (aPositionArea.second == StylePositionAreaKeyword::Center) {
    sides |= SideBits::eTopBottom;
  }

  return sides;
}

struct ModifiedContainingBlock {
  using AnchorOffsetInfo = AbsoluteContainingBlock::AnchorOffsetInfo;

  Maybe<AnchorOffsetInfo> mAnchorOffsetInfo;
  // Unmodified scrollable or local containing block
  nsRect mMaybeScrollableRect;
  // Containing block after all its modifications e.g. By grid/position-area.
  nsRect mFinalRect;

  explicit ModifiedContainingBlock(const nsRect& aRect)
      : mMaybeScrollableRect{aRect}, mFinalRect{aRect} {}
  ModifiedContainingBlock(const nsRect& aMaybeScrollableRect,
                          const nsRect& aFinalRect)
      : mMaybeScrollableRect{aMaybeScrollableRect}, mFinalRect{aFinalRect} {}
  ModifiedContainingBlock(const nsPoint& aOffset,
                          const StylePositionArea& aResolvedArea,
                          const nsRect& aMaybeScrollableRect,
                          const nsRect& aFinalRect)
      : mAnchorOffsetInfo{Some(AnchorOffsetInfo{aOffset, aResolvedArea})},
        mMaybeScrollableRect{aMaybeScrollableRect},
        mFinalRect{aFinalRect} {}

  AnchorOffsetInfo GetAnchorOffsetInfo() const {
    return mAnchorOffsetInfo.valueOr(AnchorOffsetInfo{});
  }
  StylePositionArea ResolvedPositionArea() const {
    return mAnchorOffsetInfo
        .map([](const AnchorOffsetInfo& aInfo) {
          return aInfo.mResolvedPositionArea;
        })
        .valueOr(StylePositionArea{});
  }
};

static ModifiedContainingBlock ComputeContainingBlock(
    bool aIsGrid, const nsContainerFrame* aDelegatingFrame,
    const ReflowInput& aReflowInput,
    const AbsoluteContainingBlock::ContainingBlockRects& aContainingBlockRects,
    nsIFrame* aKidFrame, AnchorPosResolutionCache* aAnchorPosResolutionCache,
    bool aReuseUnfragmentedAnchorPosReferences) {
  if (aReuseUnfragmentedAnchorPosReferences) {
    MOZ_ASSERT(aAnchorPosResolutionCache);
    const auto* referenceData = aAnchorPosResolutionCache->mReferenceData;
    if (const auto positionArea = aKidFrame->StylePosition()->mPositionArea;
        !positionArea.IsNone()) {
      return ModifiedContainingBlock{
          referenceData->AppliedScrollShift(),
          AnchorPositioningUtils::PhysicalizePositionArea(positionArea,
                                                          aKidFrame),
          referenceData->mOriginalContainingBlockRect,
          referenceData->mAdjustedContainingBlock};
    }
    return ModifiedContainingBlock{referenceData->mOriginalContainingBlockRect,
                                   referenceData->mAdjustedContainingBlock};
  }
  // The current containing block, with ongoing modifications.
  // Starts as a local containing block.
  nsRect containingBlock = aContainingBlockRects.mLocal;
  nsRect scrollableContainingBlock = aContainingBlockRects.mScrollable;
  const auto defaultAnchorInfo = [&]() -> Maybe<AnchorPosInfo> {
    if (!aAnchorPosResolutionCache) {
      return Nothing{};
    }
    return AnchorPositioningUtils::ResolveAnchorPosRect(
        aKidFrame, aDelegatingFrame, {nullptr, StyleCascadeLevel::Default()},
        false, aAnchorPosResolutionCache);
  }();
  if (defaultAnchorInfo) {
    // Presence of a valid default anchor causes us to use the scrollable
    // containing block.
    // https://github.com/w3c/csswg-drafts/issues/12552#issuecomment-3210696721
    containingBlock = aContainingBlockRects.mScrollable;
  }

  if (const ViewportFrame* viewport = do_QueryFrame(aDelegatingFrame)) {
    if (IsSnapshotContainingBlock(aKidFrame)) {
      return ModifiedContainingBlock{
          dom::ViewTransition::SnapshotContainingBlockRect(
              viewport->PresContext())};
    }
    MOZ_ASSERT(aContainingBlockRects.mScrollable ==
               aContainingBlockRects.mLocal);
    containingBlock = scrollableContainingBlock =
        viewport->GetContainingBlockAdjustedForScrollbars(aReflowInput,
                                                          aKidFrame);
  }

  // https://drafts.csswg.org/css-position/#original-cb
  // Handle grid-based adjustment first...
  if (aIsGrid) {
    const auto border = aDelegatingFrame->GetUsedBorder();
    const nsPoint borderShift{border.left, border.top};
    const nsRect preGridCB = containingBlock;
    // Shift in by border of the overall grid container.
    containingBlock = nsGridContainerFrame::GridItemCB(aKidFrame) + borderShift;
    if (!defaultAnchorInfo) {
      return ModifiedContainingBlock{preGridCB, containingBlock};
    }
  }
  // ... Then the position-area based adjustment.
  if (defaultAnchorInfo) {
    auto positionArea = aKidFrame->StylePosition()->mPositionArea;
    // Offset should be up to, but not including the containing block's
    // scroll offset.
    const auto offset = AnchorPositioningUtils::GetScrollOffsetFor(
        aAnchorPosResolutionCache->mReferenceData->CompensatingForScrollAxes(),
        aKidFrame, aAnchorPosResolutionCache->mDefaultAnchorCache);
    StylePositionArea resolvedPositionArea{};
    if (!positionArea.IsNone()) {
      // Imagine an abspos container with a scroller in it, and then an
      // anchor in it, where the anchor is visually in the middle of the
      // scrollport. Then, when the scroller moves such that the anchor's
      // left edge is on that of the scrollports, w.r.t. containing block,
      // the anchor is zero left offset horizontally. The position-area
      // grid needs to account for this.
      const auto scrolledAnchorRect = defaultAnchorInfo->mRect - offset;
      const auto scrolledAnchorCb = AnchorPositioningUtils::
          AdjustAbsoluteContainingBlockRectForPositionArea(
              scrolledAnchorRect + aContainingBlockRects.mLocal.TopLeft(),
              containingBlock, aKidFrame->GetWritingMode(),
              aDelegatingFrame->GetWritingMode(), positionArea,
              &resolvedPositionArea);
      // By definition, we're using the default anchor, and are scroll
      // compensated.
      aAnchorPosResolutionCache->mReferenceData->mScrollCompensatedSides =
          GetScrollCompensatedSidesFor(resolvedPositionArea);
      // Unscroll the CB by canceling out the previously applied
      // scroll offset (See above), the offset will be applied later.
      containingBlock = scrolledAnchorCb + offset;
    }
    return ModifiedContainingBlock{offset, resolvedPositionArea,
                                   scrollableContainingBlock, containingBlock};
  }
  return ModifiedContainingBlock{containingBlock};
}

void AbsoluteContainingBlock::Reflow(nsContainerFrame* aDelegatingFrame,
                                     nsPresContext* aPresContext,
                                     const ReflowInput& aReflowInput,
                                     nsReflowStatus& aReflowStatus,
                                     const nsRect& aContainingBlock,
                                     AbsPosReflowFlags aFlags,
                                     OverflowAreas* aOverflowAreas) {
  MOZ_ASSERT(aReflowStatus.IsEmpty(),
             "Caller should pass a fresh reflow status!");

  const auto scrollableContainingBlock = ComputeScrollableContainingBlock(
      aDelegatingFrame, aContainingBlock, aOverflowAreas);
  const ContainingBlockRects passedContainingBlock{aContainingBlock,
                                                   scrollableContainingBlock};

  const auto* unfragmentedContainingBlockRects =
      [&]() -> const ContainingBlockRects* {
    if (aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow) {
      // Doing the measuring reflow, so set the unfragmented containing sizes
      // here.
      NS_WARNING_ASSERTION(aDelegatingFrame->FirstInFlow() == aDelegatingFrame,
                           "Saving unfragmented CB into non-first-in-flow");
      aDelegatingFrame->SetOrUpdateDeletableProperty(
          UnfragmentedContainingBlockProperty(), passedContainingBlock);
      // Just reuse what was passed in.
      return &passedContainingBlock;
    }
    if (const auto* unfragmented = aDelegatingFrame->FirstInFlow()->GetProperty(
            UnfragmentedContainingBlockProperty())) {
      return unfragmented;
    }
    return &passedContainingBlock;
  }();

  const auto* fragmentedContainingBlockRects =
      unfragmentedContainingBlockRects != &passedContainingBlock
          ? &passedContainingBlock
          : nullptr;

#ifdef DEBUG
  SanityCheckChildLists(aDelegatingFrame);
#endif

  if (const nsIFrame* prev =
          GetFirstContinuationInPrevFragmentainer(aDelegatingFrame)) {
    const auto* prevAbsCB = prev->GetAbsoluteContainingBlock();
    MOZ_ASSERT(prevAbsCB,
               "If this delegating frame has an absCB, |prev| must "
               "have one, too!");
    mCumulativeContainingBlockBSize =
        prevAbsCB->mCumulativeContainingBlockBSize;
  } else {
    mCumulativeContainingBlockBSize = 0;
  }

  // Assume all the kids may need a reflow when they are in a fragmented
  // context. We'll perform more targeted check below. For example, skip reflow
  // them when they are positioned in a later fragment.
  const bool reflowAll = aReflowInput.ShouldReflowAllKids() ||
                         aReflowInput.IsInFragmentedContext();
  const bool cbWidthChanged = aFlags.contains(AbsPosReflowFlag::CBWidthChanged);
  const bool cbHeightChanged =
      aFlags.contains(AbsPosReflowFlag::CBHeightChanged);
  const nscoord availBSize = aReflowInput.AvailableBSize();
  const WritingMode containerWM = aReflowInput.GetWritingMode();
  nsFrameList newPushedAbsoluteFrames;
  for (auto iter = mAbsoluteFrames.begin(); iter != mAbsoluteFrames.end();) {
    // Advance the iterator first, so it's safe to move |kidFrame|.
    nsIFrame* const kidFrame = *iter++;
    bool reuseUnfragmentedAnchorPosReferences = false;
    Maybe<AnchorPosResolutionCache> anchorPosResolutionCache;
    if (kidFrame->HasAnchorPosReference()) {
      AnchorPosReferenceData* referenceData = nullptr;
      if (const auto* firstInFlow = kidFrame->FirstInFlow();
          GetUnfragmentedPosition(aReflowInput, firstInFlow)) {
        // Ok, we've done a measuring reflow with no fragmentation, and so the
        // unfragmented position property is now set. Use the existing
        // references, which contains the anchor lookup data from the measuring
        // reflow.
        referenceData =
            firstInFlow->GetProperty(nsIFrame::AnchorPosReferences());
        reuseUnfragmentedAnchorPosReferences = true;
      }
      if (!referenceData) {
        referenceData = kidFrame->GetOrCreateDeletableProperty(
            nsIFrame::AnchorPosReferences());
        referenceData->Reset();
      }
      anchorPosResolutionCache = Some(PopulateAnchorResolutionCache(
          kidFrame, referenceData, reuseUnfragmentedAnchorPosReferences));
      // PopulateAnchorResolutionCache() resolves the default anchor, so we can
      // now tell if the remembered scroll offset is obsolete. The default
      // anchor can change without a fallback change (e.g. the frame has no
      // position fallbacks), in which case TriggerFallbackReflow() won't update
      // the remembered scroll offset, so we forget it here.
      referenceData->ForgetRememberedScrollOffsetIfDefaultAnchorChanged();
    } else {
      kidFrame->RemoveProperty(nsIFrame::AnchorPosReferences());
    }

    bool kidNeedsReflow =
        reflowAll || kidFrame->IsSubtreeDirty() ||
        FrameDependsOnContainer(kidFrame, cbWidthChanged, cbHeightChanged,
                                anchorPosResolutionCache.ptrOr(nullptr));
    if (kidFrame->IsSubtreeDirty()) {
      MaybeMarkAncestorsAsHavingDescendantDependentOnItsStaticPos(
          kidFrame, aDelegatingFrame);
    }
    if (kidNeedsReflow && !aPresContext->HasPendingInterrupt()) {
      const LogicalSize cbSize(containerWM,
                               unfragmentedContainingBlockRects->mLocal.Size());
      const LogicalMargin border =
          aDelegatingFrame->GetLogicalUsedBorder(containerWM)
              .ApplySkipSides(
                  aDelegatingFrame->PreReflowBlockLevelLogicalSkipSides());
      const nsSize cbBorderBoxSize =
          (cbSize + border.Size(containerWM)).GetPhysicalSize(containerWM);

      bool kidFrameNeedsPush = false;
      if (const auto* unfragPos =
              GetUnfragmentedPosition(aReflowInput, kidFrame);
          unfragPos && availBSize != NS_UNCONSTRAINEDSIZE) {
        // If kidFrame's position in this fragment is beyond the end of this
        // fragmentainer, push it to the next fragmentainer.
        const nscoord kidBPosInThisFragment =
            unfragPos->B(containerWM) - mCumulativeContainingBlockBSize;
        if (kidBPosInThisFragment > availBSize) {
          kidFrameNeedsPush = true;
        }
      }

      nsReflowStatus kidStatus;
      if (!kidFrameNeedsPush) {
        ReflowAbsoluteFrame(aDelegatingFrame, aPresContext, aReflowInput,
                            *unfragmentedContainingBlockRects, aFlags, kidFrame,
                            kidStatus, aOverflowAreas,
                            fragmentedContainingBlockRects,
                            anchorPosResolutionCache.ptrOr(nullptr),
                            reuseUnfragmentedAnchorPosReferences);

        if (aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow) {
          LogicalPoint unfragPos(containerWM);
          if (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
              aDelegatingFrame->IsInlineFrameOrSubclass()) {
            // Translate the kid's unfragmented position to the block ancestor's
            // coordinate space so that it is easier to compute its position
            // later in the normal fragmented reflow.
            nsIFrame* blockAncestor =
                nsLayoutUtils::FindNearestBlockAncestor(aDelegatingFrame);
            nsSize blockAncestorSize =
                aReflowInput.mContainingBlockSize.GetPhysicalSize(containerWM);
            nsRect kidRect = kidFrame->GetRectRelativeToSelf() +
                             kidFrame->GetOffsetTo(blockAncestor);
            unfragPos = LogicalRect(containerWM, kidRect, blockAncestorSize)
                            .Origin(containerWM);
          } else {
            unfragPos =
                kidFrame->GetLogicalPosition(containerWM, cbBorderBoxSize);
          }
          kidFrame->SetOrUpdateDeletableProperty(UnfragmentedPositionProperty(),
                                                 unfragPos);

          const LogicalSize kidSize =
              kidFrame->StylePosition()->mBoxSizing == StyleBoxSizing::BorderBox
                  ? kidFrame->GetLogicalSize()
                  : kidFrame->ContentSize();
          kidFrame->SetOrUpdateDeletableProperty(UnfragmentedSizeProperty(),
                                                 kidSize);

          // kidFrame must be a first-in-flow here. In a measuring reflow
          // starting in the first column, we only see first-in-flows (either
          // unsplit or pulled back from later continuations of this absolute
          // containing block). However, in an incremental measuring reflow, if
          // the first-in-flow is not fully-complete, it is possible that we
          // still reflow continuations here.
          NS_ASSERTION(
              !kidFrame->GetPrevInFlow(),
              "UnfragmentedPositionProperty and UnfragmentedSizeProperty "
              "should only be set on first-in-flow!");
        }
        MOZ_ASSERT(!kidStatus.IsInlineBreakBefore(),
                   "ShouldAvoidBreakInside should prevent this from happening");
      }

      nsIFrame* nextFrame = kidFrame->GetNextInFlow();
      if (kidFrameNeedsPush) {
        StealFrame(kidFrame);
        kidFrame->AddStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
        newPushedAbsoluteFrames.AppendFrame(nullptr, kidFrame);
      } else if (!kidStatus.IsFullyComplete()) {
        if (!nextFrame) {
          nextFrame = aPresContext->PresShell()
                          ->FrameConstructor()
                          ->CreateContinuingFrame(kidFrame, aDelegatingFrame);
          nextFrame->AddStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW);
          newPushedAbsoluteFrames.AppendFrame(nullptr, nextFrame);
        } else if (nextFrame->GetParent() != aDelegatingFrame &&
                   nextFrame->GetParent() !=
                       aDelegatingFrame->GetNextInFlow()) {
          nextFrame->GetParent()->GetAbsoluteContainingBlock()->StealFrame(
              nextFrame);
          // nextFrame is in a later absCB continuation. To keep the
          // continuations in order, append it to mPushedAbsoluteFrames.
          mPushedAbsoluteFrames.AppendFrame(aDelegatingFrame, nextFrame);
        }
        aReflowStatus.MergeCompletionStatusFrom(kidStatus);
      } else if (nextFrame) {
        // kidFrame is fully-complete. Delete all its next-in-flows.
        FrameDestroyContext context(aPresContext->PresShell());
        nextFrame->GetParent()->GetAbsoluteContainingBlock()->RemoveFrame(
            context, FrameChildListID::Absolute, nextFrame);
      }
    } else {
      if (aOverflowAreas) {
        aDelegatingFrame->ConsiderChildOverflow(*aOverflowAreas, kidFrame);
      }
    }

    // Make a CheckForInterrupt call, here, not just HasPendingInterrupt.  That
    // will make sure that we end up reflowing aDelegatingFrame in cases when
    // one of our kids interrupted.  Otherwise we'd set the dirty or
    // dirty-children bit on the kid in the condition below, and then when
    // reflow completes and we go to mark dirty bits on all ancestors of that
    // kid we'll immediately bail out, because the kid already has a dirty bit.
    // In particular, we won't set any dirty bits on aDelegatingFrame, so when
    // the following reflow happens we won't reflow the kid in question.  This
    // might be slightly suboptimal in cases where |kidFrame| itself did not
    // interrupt, since we'll trigger a reflow of it too when it's not strictly
    // needed.  But the logic to not do that is enough more complicated, and
    // the case enough of an edge case, that this is probably better.
    if (kidNeedsReflow && aPresContext->CheckForInterrupt(aDelegatingFrame)) {
      if (aDelegatingFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
        kidFrame->MarkSubtreeDirty();
      } else {
        kidFrame->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
      }
    }
  }

  if (newPushedAbsoluteFrames.NotEmpty()) {
    // Prepend the new pushed frames to the front of mPushedAbsoluteFrames.
    mPushedAbsoluteFrames.InsertFrames(nullptr, nullptr,
                                       std::move(newPushedAbsoluteFrames));
  }

  if (availBSize != NS_UNCONSTRAINEDSIZE) {
    mCumulativeContainingBlockBSize += availBSize;
  }

  // Abspos frames can't cause their parent to be incomplete,
  // only overflow incomplete.
  if (aReflowStatus.IsIncomplete() || mPushedAbsoluteFrames.NotEmpty()) {
    aReflowStatus.SetOverflowIncomplete();
    aReflowStatus.SetNextInFlowNeedsReflow();
  }

#ifdef DEBUG
  SanityCheckChildLists(aDelegatingFrame);
#endif
}

static inline bool IsFixedPaddingSize(const LengthPercentage& aCoord) {
  return aCoord.ConvertsToLength();
}
static inline bool IsFixedMarginSize(const AnchorResolvedMargin& aCoord) {
  return aCoord->ConvertsToLength();
}
static inline bool IsFixedOffset(const AnchorResolvedInset& aInset) {
  // For anchor positioning functions, even if the computed value may be a
  // fixed length, it depends on the absolute containing block's size.
  return aInset->ConvertsToLength();
}

bool AbsoluteContainingBlock::FrameDependsOnContainer(
    nsIFrame* f, bool aCBWidthChanged, bool aCBHeightChanged,
    AnchorPosResolutionCache* aAnchorPosResolutionCache) {
  const nsStylePosition* pos = f->StylePosition();
  // See if f's position might have changed because it depends on a
  // placeholder's position.
  if (pos->NeedsHypotheticalPositionIfAbsPos()) {
    return true;
  }
  if (!aCBWidthChanged && !aCBHeightChanged) {
    // skip getting style data
    return false;
  }
  const nsStylePadding* padding = f->StylePadding();
  const nsStyleMargin* margin = f->StyleMargin();
  WritingMode wm = f->GetWritingMode();
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(f, aAnchorPosResolutionCache);
  if (wm.IsVertical() ? aCBHeightChanged : aCBWidthChanged) {
    // See if f's inline-size might have changed.
    // If margin-inline-start/end, padding-inline-start/end,
    // inline-size, min/max-inline-size are all lengths, 'none', or enumerated,
    // then our frame isize does not depend on the parent isize.
    // Note that borders never depend on the parent isize.
    // XXX All of the enumerated values except -moz-available are ok too.
    if (nsStylePosition::ISizeDependsOnContainer(
            pos->ISize(wm, anchorResolutionParams)) ||
        nsStylePosition::MinISizeDependsOnContainer(
            pos->MinISize(wm, anchorResolutionParams)) ||
        nsStylePosition::MaxISizeDependsOnContainer(
            pos->MaxISize(wm, anchorResolutionParams)) ||
        !IsFixedPaddingSize(padding->mPadding.GetIStart(wm)) ||
        !IsFixedPaddingSize(padding->mPadding.GetIEnd(wm))) {
      return true;
    }

    // See if f's position might have changed. If we're RTL then the
    // rules are slightly different. We'll assume percentage or auto
    // margins will always induce a dependency on the size
    if (!IsFixedMarginSize(margin->GetMargin(LogicalSide::IStart, wm,
                                             anchorResolutionParams)) ||
        !IsFixedMarginSize(
            margin->GetMargin(LogicalSide::IEnd, wm, anchorResolutionParams))) {
      return true;
    }
  }
  if (wm.IsVertical() ? aCBWidthChanged : aCBHeightChanged) {
    // See if f's block-size might have changed.
    // If margin-block-start/end, padding-block-start/end,
    // min-block-size, and max-block-size are all lengths or 'none',
    // and bsize is a length or bsize and bend are auto and bstart is not auto,
    // then our frame bsize does not depend on the parent bsize.
    // Note that borders never depend on the parent bsize.
    //
    // FIXME(emilio): Should the BSize(wm).IsAuto() check also for the extremum
    // lengths?
    const auto bSize = pos->BSize(wm, anchorResolutionParams);
    const auto anchorOffsetResolutionParams =
        AnchorPosOffsetResolutionParams::UseCBFrameSize(anchorResolutionParams);
    if ((nsStylePosition::BSizeDependsOnContainer(bSize) &&
         !(bSize->IsAuto() &&
           pos->GetAnchorResolvedInset(LogicalSide::BEnd, wm,
                                       anchorOffsetResolutionParams)
               ->IsAuto() &&
           !pos->GetAnchorResolvedInset(LogicalSide::BStart, wm,
                                        anchorOffsetResolutionParams)
                ->IsAuto())) ||
        nsStylePosition::MinBSizeDependsOnContainer(
            pos->MinBSize(wm, anchorResolutionParams)) ||
        nsStylePosition::MaxBSizeDependsOnContainer(
            pos->MaxBSize(wm, anchorResolutionParams)) ||
        !IsFixedPaddingSize(padding->mPadding.GetBStart(wm)) ||
        !IsFixedPaddingSize(padding->mPadding.GetBEnd(wm))) {
      return true;
    }

    // See if f's position might have changed.
    if (!IsFixedMarginSize(margin->GetMargin(LogicalSide::BStart, wm,
                                             anchorResolutionParams)) ||
        !IsFixedMarginSize(
            margin->GetMargin(LogicalSide::BEnd, wm, anchorResolutionParams))) {
      return true;
    }
  }

  // Since we store coordinates relative to top and left, the position
  // of a frame depends on that of its container if it is fixed relative
  // to the right or bottom, or if it is positioned using percentages
  // relative to the left or top.  Because of the dependency on the
  // sides (left and top) that we use to store coordinates, these tests
  // are easier to do using physical coordinates rather than logical.
  if (aCBWidthChanged) {
    const auto anchorOffsetResolutionParams =
        AnchorPosOffsetResolutionParams::UseCBFrameSize(anchorResolutionParams);
    if (!IsFixedOffset(pos->GetAnchorResolvedInset(
            eSideLeft, anchorOffsetResolutionParams))) {
      return true;
    }
    // Note that even if 'left' is a length, our position can still
    // depend on the containing block width, because if our direction or
    // writing-mode moves from right to left (in either block or inline
    // progression) and 'right' is not 'auto', we will discard 'left'
    // and be positioned relative to the containing block right edge.
    // 'left' length and 'right' auto is the only combination we can be
    // sure of.
    if ((wm.GetInlineDir() == WritingMode::InlineDir::RTL ||
         wm.GetBlockDir() == WritingMode::BlockDir::RL) &&
        !pos->GetAnchorResolvedInset(eSideRight, anchorOffsetResolutionParams)
             ->IsAuto()) {
      return true;
    }
  }
  if (aCBHeightChanged) {
    const auto anchorOffsetResolutionParams =
        AnchorPosOffsetResolutionParams::UseCBFrameSize(anchorResolutionParams);
    if (!IsFixedOffset(pos->GetAnchorResolvedInset(
            eSideTop, anchorOffsetResolutionParams))) {
      return true;
    }
    // See comment above for width changes.
    if (wm.GetInlineDir() == WritingMode::InlineDir::BTT &&
        !pos->GetAnchorResolvedInset(eSideBottom, anchorOffsetResolutionParams)
             ->IsAuto()) {
      return true;
    }
  }

  return false;
}

void AbsoluteContainingBlock::DestroyFrames(DestroyContext& aContext) {
  mAbsoluteFrames.DestroyFrames(aContext);
  mPushedAbsoluteFrames.DestroyFrames(aContext);
}

void AbsoluteContainingBlock::MarkSizeDependentFramesDirty() {
  DoMarkFramesDirty(false);
}

void AbsoluteContainingBlock::MarkAllFramesDirty() { DoMarkFramesDirty(true); }

void AbsoluteContainingBlock::DoMarkFramesDirty(bool aMarkAllDirty) {
  for (nsIFrame* kidFrame : mAbsoluteFrames) {
    if (aMarkAllDirty) {
      kidFrame->MarkSubtreeDirty();
    } else if (FrameDependsOnContainer(kidFrame, true, true)) {
      // Add the weakest flags that will make sure we reflow this frame later
      kidFrame->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
    }
  }
}

// Given an out-of-flow frame, this method returns the parent frame of its
// placeholder frame or null if it doesn't have a placeholder for some reason.
static nsContainerFrame* GetPlaceholderContainer(nsIFrame* aPositionedFrame) {
  nsIFrame* placeholder = aPositionedFrame->GetPlaceholderFrame();
  return placeholder ? placeholder->GetParent() : nullptr;
}

struct NonAutoAlignParams {
  nscoord mCurrentStartInset;
  nscoord mCurrentEndInset;

  NonAutoAlignParams(nscoord aStartInset, nscoord aEndInset)
      : mCurrentStartInset(aStartInset), mCurrentEndInset(aEndInset) {}
};

/**
 * This function returns the offset of an abs/fixed-pos child's static
 * position, with respect to the "start" corner of its alignment container,
 * according to CSS Box Alignment.  This function only operates in a single
 * axis at a time -- callers can choose which axis via the |aAbsPosCBAxis|
 * parameter. This is called under two scenarios:
 * 1. We're statically positioning this absolutely positioned box, meaning
 *    that the offsets are auto and will change depending on the alignment
 *    of the box.
 * 2. The offsets are non-auto, but the element may not fill the inset-reduced
 *    containing block, so its margin box needs to be aligned in that axis.
 *    This is the step 4 of [1]. Should also be noted that, unlike static
 *    positioning, where we may confine the alignment area for flex/grid
 *    parent containers, we explicitly align to the inset-reduced absolute
 *    container size.
 *
 * [1]: https://drafts.csswg.org/css-position-3/#abspos-layout
 *
 * @param aKidReflowInput The ReflowInput for the to-be-aligned abspos child.
 * @param aKidSizeInAbsPosCBWM The child frame's size (after it's been given
 *                             the opportunity to reflow), in terms of
 *                             aAbsPosCBWM.
 * @param aAbsPosCBSize The abspos CB size, in terms of aAbsPosCBWM.
 * @param aPlaceholderContainer The parent of the child frame's corresponding
 *                              placeholder frame, cast to a nsContainerFrame.
 *                              (This will help us choose which alignment enum
 *                              we should use for the child.)
 * @param aAbsPosCBWM The child frame's containing block's WritingMode.
 * @param aAbsPosCBAxis The axis (of the containing block) that we should
 *                      be doing this computation for.
 * @param aNonAutoAlignParams Parameters, if specified, indicating that we're
 *                            handling scenario 2.
 */
static nscoord OffsetToAlignedStaticPos(
    const ReflowInput& aKidReflowInput, const LogicalSize& aKidSizeInAbsPosCBWM,
    const LogicalSize& aAbsPosCBSize,
    const nsContainerFrame* aPlaceholderContainer, WritingMode aAbsPosCBWM,
    LogicalAxis aAbsPosCBAxis, Maybe<NonAutoAlignParams> aNonAutoAlignParams,
    const AbsoluteContainingBlock::AnchorOffsetInfo& aAnchorOffsetInfo) {
  if (!aPlaceholderContainer) {
    // (The placeholder container should be the thing that kicks this whole
    // process off, by setting PLACEHOLDER_STATICPOS_NEEDS_CSSALIGN.  So it
    // should exist... but bail gracefully if it doesn't.)
    NS_ERROR(
        "Missing placeholder-container when computing a "
        "CSS Box Alignment static position");
    return 0;
  }

  // (Most of this function is simply preparing args that we'll pass to
  // AlignJustifySelf at the end.)

  // NOTE: Our alignment container is aPlaceholderContainer's content-box
  // (or an area within it, if aPlaceholderContainer is a grid). So, we'll
  // perform most of our arithmetic/alignment in aPlaceholderContainer's
  // WritingMode. For brevity, we use the abbreviation "pc" for "placeholder
  // container" in variables below.
  WritingMode pcWM = aPlaceholderContainer->GetWritingMode();
  LogicalSize absPosCBSizeInPCWM = aAbsPosCBSize.ConvertTo(pcWM, aAbsPosCBWM);

  // Find what axis aAbsPosCBAxis corresponds to, in placeholder's parent's
  // writing-mode.
  const LogicalAxis pcAxis = aAbsPosCBWM.ConvertAxisTo(aAbsPosCBAxis, pcWM);
  const LogicalSize alignAreaSize = [&]() {
    if (!aNonAutoAlignParams) {
      const bool placeholderContainerIsContainingBlock =
          aPlaceholderContainer == aKidReflowInput.mCBReflowInput->mFrame;

      LayoutFrameType parentType = aPlaceholderContainer->Type();
      LogicalSize alignAreaSize(pcWM);
      if (parentType == LayoutFrameType::FlexContainer) {
        // We store the frame rect in FinishAndStoreOverflow, which runs _after_
        // reflowing the absolute frames, so handle the special case of the
        // frame being the actual containing block here, by getting the size
        // from aAbsPosCBSize.
        //
        // The alignment container is the flex container's content box.
        if (placeholderContainerIsContainingBlock) {
          alignAreaSize = aAbsPosCBSize.ConvertTo(pcWM, aAbsPosCBWM);
          // aAbsPosCBSize is the padding-box, so substract the padding to get
          // the content box.
          alignAreaSize -=
              aPlaceholderContainer->GetLogicalUsedPadding(pcWM).Size(pcWM);
        } else {
          alignAreaSize = aPlaceholderContainer->GetLogicalSize(pcWM);
          LogicalMargin pcBorderPadding =
              aPlaceholderContainer->GetLogicalUsedBorderAndPadding(pcWM);
          alignAreaSize -= pcBorderPadding.Size(pcWM);
        }
        return alignAreaSize;
      }
      if (parentType == LayoutFrameType::GridContainer) {
        // This abspos elem's parent is a grid container. Per CSS Grid 10.1
        // & 10.2:
        //  - If the grid container *also* generates the abspos containing block
        //  (a
        // grid area) for this abspos child, we use that abspos containing block
        // as the alignment container, too. (And its size is aAbsPosCBSize.)
        //  - Otherwise, we use the grid's padding box as the alignment
        //  container.
        // https://drafts.csswg.org/css-grid/#static-position
        if (placeholderContainerIsContainingBlock) {
          // The alignment container is the grid area that we're using as the
          // absolute containing block.
          alignAreaSize = aAbsPosCBSize.ConvertTo(pcWM, aAbsPosCBWM);
        } else {
          // The alignment container is a the grid container's content box
          // (which we can get by subtracting away its border & padding from
          // frame's size):
          alignAreaSize = aPlaceholderContainer->GetLogicalSize(pcWM);
          LogicalMargin pcBorderPadding =
              aPlaceholderContainer->GetLogicalUsedBorderAndPadding(pcWM);
          alignAreaSize -= pcBorderPadding.Size(pcWM);
        }
        return alignAreaSize;
      }
    }
    // Either we're in scenario 1 but within a non-flex/grid parent, or in
    // scenario 2.
    return aAbsPosCBSize.ConvertTo(pcWM, aAbsPosCBWM);
  }();

  const nscoord existingOffset = aNonAutoAlignParams
                                     ? aNonAutoAlignParams->mCurrentStartInset +
                                           aNonAutoAlignParams->mCurrentEndInset
                                     : 0;
  const nscoord alignAreaSizeInAxis =
      ((pcAxis == LogicalAxis::Inline) ? alignAreaSize.ISize(pcWM)
                                       : alignAreaSize.BSize(pcWM)) -
      existingOffset;

  using AlignJustifyFlag = CSSAlignUtils::AlignJustifyFlag;
  CSSAlignUtils::AlignJustifyFlags flags(AlignJustifyFlag::IgnoreAutoMargins);
  // Given that scenario 2 ignores the parent container type, special handling
  // of absolutely-positioned child is also ignored.
  StyleAlignFlags alignConst =
      aNonAutoAlignParams
          ? aPlaceholderContainer
                ->CSSAlignmentForAbsPosChildWithinContainingBlock(
                    aKidReflowInput, pcAxis,
                    aAnchorOffsetInfo.mResolvedPositionArea, absPosCBSizeInPCWM)
          : aPlaceholderContainer->CSSAlignmentForAbsPosChild(aKidReflowInput,
                                                              pcAxis);
  // If the safe bit in alignConst is set, set the safe flag in |flags|.
  const auto safetyBits =
      alignConst & (StyleAlignFlags::SAFE | StyleAlignFlags::UNSAFE);
  alignConst &= ~StyleAlignFlags::FLAG_BITS;
  if (safetyBits & StyleAlignFlags::SAFE) {
    flags += AlignJustifyFlag::OverflowSafe;
  }

  // Find out if placeholder-container & the OOF child have the same start-sides
  // in the placeholder-container's pcAxis.
  WritingMode kidWM = aKidReflowInput.GetWritingMode();
  if (pcWM.ParallelAxisStartsOnSameSide(pcAxis, kidWM)) {
    flags += AlignJustifyFlag::SameSide;
  }

  if (aNonAutoAlignParams) {
    flags += AlignJustifyFlag::AligningMarginBox;
  }

  // (baselineAdjust is unused. CSSAlignmentForAbsPosChild() should've
  // converted 'baseline'/'last baseline' enums to their fallback values.)
  const nscoord baselineAdjust = nscoord(0);

  // AlignJustifySelf operates in the kid's writing mode, so we need to
  // represent the child's size and the desired axis in that writing mode:
  LogicalSize kidSizeInOwnWM =
      aKidSizeInAbsPosCBWM.ConvertTo(kidWM, aAbsPosCBWM);
  const LogicalAxis kidAxis = aAbsPosCBWM.ConvertAxisTo(aAbsPosCBAxis, kidWM);

  // Build an Inset Modified anchor info from the anchor which can be used to
  // align to the anchor-center, if AlignJustifySelf is AnchorCenter.
  Maybe<CSSAlignUtils::AnchorAlignInfo> anchorAlignInfo;
  if (alignConst == StyleAlignFlags::ANCHOR_CENTER &&
      aKidReflowInput.mAnchorPosResolutionCache) {
    AnchorPosReferenceData* referenceData =
        aKidReflowInput.mAnchorPosResolutionCache->mReferenceData;
    if (referenceData) {
      const auto* cachedData = referenceData->Lookup(
          {referenceData->mDefaultAnchorName, referenceData->mAnchorTreeScope});
      if (cachedData && *cachedData) {
        referenceData->AdjustCompensatingForScroll(
            aAbsPosCBWM.PhysicalAxis(aAbsPosCBAxis));
        const auto& data = cachedData->ref();
        if (data.mOffsetData) {
          const nsSize containerSize =
              aAbsPosCBSize.GetPhysicalSize(aAbsPosCBWM);
          // Adjust for position-area, grid, etc.
          const auto cbOffset =
              referenceData->mAdjustedContainingBlock.TopLeft() -
              referenceData->mOriginalContainingBlockRect.TopLeft();
          const nsRect anchorRect(data.mOffsetData->mOrigin - cbOffset,
                                  data.mSize);
          const LogicalRect logicalAnchorRect{aAbsPosCBWM, anchorRect,
                                              containerSize};
          const auto axisInAbsPosCBWM =
              kidWM.ConvertAxisTo(kidAxis, aAbsPosCBWM);
          const auto anchorStart =
              logicalAnchorRect.Start(axisInAbsPosCBWM, aAbsPosCBWM);
          const auto anchorSize =
              logicalAnchorRect.Size(axisInAbsPosCBWM, aAbsPosCBWM);
          anchorAlignInfo =
              Some(CSSAlignUtils::AnchorAlignInfo{anchorStart, anchorSize});
          if (aNonAutoAlignParams) {
            anchorAlignInfo->mAnchorStart -=
                aNonAutoAlignParams->mCurrentStartInset;
          }
        }
      }
    }
  }

  nscoord offset = CSSAlignUtils::AlignJustifySelf(
      alignConst, kidAxis, flags, baselineAdjust, alignAreaSizeInAxis,
      aKidReflowInput, kidSizeInOwnWM, anchorAlignInfo);

  // Safe alignment clamping for anchor-center.
  // When using anchor-center with the safe keyword, or when both insets are
  // auto (which defaults to safe behavior), clamp the element to stay within
  // the containing block.
  if ((!aNonAutoAlignParams || (safetyBits & StyleAlignFlags::SAFE)) &&
      alignConst == StyleAlignFlags::ANCHOR_CENTER) {
    const auto cbSize = aAbsPosCBSize.Size(aAbsPosCBAxis, aAbsPosCBWM);
    const auto kidSize = aKidSizeInAbsPosCBWM.Size(aAbsPosCBAxis, aAbsPosCBWM);

    if (aNonAutoAlignParams) {
      const nscoord currentStartInset = aNonAutoAlignParams->mCurrentStartInset;
      const nscoord finalStart = currentStartInset + offset;
      const nscoord clampedStart =
          CSSMinMax(finalStart, nscoord(0), cbSize - kidSize);
      offset = clampedStart - currentStartInset;
    } else {
      offset = CSSMinMax(offset, nscoord(0), cbSize - kidSize);
    }
  }

  const auto rawAlignConst =
      (pcAxis == LogicalAxis::Inline)
          ? aKidReflowInput.mStylePosition->mJustifySelf._0
          : aKidReflowInput.mStylePosition->mAlignSelf._0;
  if (aNonAutoAlignParams && !safetyBits &&
      (rawAlignConst != StyleAlignFlags::AUTO ||
       alignConst == StyleAlignFlags::ANCHOR_CENTER)) {
    // No `safe` or `unsafe` specified - "in-between" behaviour for relevant
    // alignment values: https://drafts.csswg.org/css-position-3/#abspos-layout
    // Skip if the raw self alignment for this element is `auto` to preserve
    // legacy behaviour, except in the case where the resolved value is
    // anchor-center (where "legacy behavior" is not a concern).
    // Follows https://drafts.csswg.org/css-align-3/#auto-safety-position
    const auto cbSize = aAbsPosCBSize.Size(aAbsPosCBAxis, aAbsPosCBWM);
    // IMCB stands for "Inset-Modified Containing Block."
    const auto imcbStart = aNonAutoAlignParams->mCurrentStartInset;
    const auto imcbEnd = cbSize - aNonAutoAlignParams->mCurrentEndInset;
    // Need to pull the offset into the "current view," unless it already did.
    const auto scrollOffset = aAnchorOffsetInfo.mResolvedPositionArea.IsNone()
                                  ? aAbsPosCBWM.PhysicalAxis(aAbsPosCBAxis) ==
                                            PhysicalAxis::Horizontal
                                        ? aAnchorOffsetInfo.mScrollOffset.x
                                        : aAnchorOffsetInfo.mScrollOffset.y
                                  : 0;
    const auto kidSize = aKidSizeInAbsPosCBWM.Size(aAbsPosCBAxis, aAbsPosCBWM);
    const auto kidStart =
        aNonAutoAlignParams->mCurrentStartInset + offset - scrollOffset;
    const auto kidEnd = kidStart + kidSize;
    // "[...] the overflow limit rect is the bounding rectangle of the alignment
    // subject’s inset-modified containing block and its original containing
    // block."
    const auto overflowLimitRectStart = std::min(0, imcbStart);
    const auto overflowLimitRectEnd = std::max(cbSize, imcbEnd);

    if (kidStart >= imcbStart && kidEnd <= imcbEnd) {
      // 1. We fit inside the IMCB, no action needed.
    } else if (kidSize <= overflowLimitRectEnd - overflowLimitRectStart) {
      // 2. We overflowed IMCB, try to cover IMCB completely, if it's not.
      if (kidStart <= imcbStart && kidEnd >= imcbEnd) {
        // IMCB already covered, ensure that we aren't escaping the limit rect.
        if (kidStart < overflowLimitRectStart) {
          offset += overflowLimitRectStart - kidStart;
        } else if (kidEnd > overflowLimitRectEnd) {
          offset -= kidEnd - overflowLimitRectEnd;
        }
      } else if (kidEnd < imcbEnd && kidStart < imcbStart) {
        // Space to end, overflowing on start - nudge to end.
        offset += std::min(imcbStart - kidStart, imcbEnd - kidEnd);
      } else if (kidStart > imcbStart && kidEnd > imcbEnd) {
        // Space to start, overflowing on end - nudge to start.
        offset -= std::min(kidEnd - imcbEnd, kidStart - imcbStart);
      }
    } else {
      // 3. We'll overflow the limit rect. Start align the subject int overflow
      // limit rect.
      offset =
          -aNonAutoAlignParams->mCurrentStartInset + overflowLimitRectStart;
    }
  }

  // "offset" is in terms of the CSS Box Alignment container (i.e. it's in
  // terms of pcWM). But our return value needs to in terms of the containing
  // block's writing mode, which might have the opposite directionality in the
  // given axis. In that case, we just need to negate "offset" when returning,
  // to make it have the right effect as an offset for coordinates in the
  // containing block's writing mode.
  if (!pcWM.ParallelAxisStartsOnSameSide(pcAxis, aAbsPosCBWM)) {
    return -offset;
  }
  return offset;
}

void AbsoluteContainingBlock::ResolveSizeDependentOffsets(
    ReflowInput& aKidReflowInput, const LogicalSize& aCBSize,
    const LogicalSize& aKidSize, const LogicalMargin& aMargin,
    const AnchorOffsetInfo& aAnchorOffsetInfo, LogicalMargin& aOffsets) {
  WritingMode outerWM = aKidReflowInput.mParentReflowInput->GetWritingMode();

  // Now that we know the child's size, we resolve any sentinel values in its
  // IStart/BStart offset coordinates that depend on that size.
  //  * NS_AUTOOFFSET indicates that the child's position in the given axis
  // is determined by its end-wards offset property, combined with its size and
  // available space. e.g.: "top: auto; height: auto; bottom: 50px"
  //  * m{I,B}OffsetsResolvedAfterSize indicate that the child is using its
  // static position in that axis, *and* its static position is determined by
  // the axis-appropriate css-align property (which may require the child's
  // size, e.g. to center it within the parent).
  if ((NS_AUTOOFFSET == aOffsets.IStart(outerWM)) ||
      (NS_AUTOOFFSET == aOffsets.BStart(outerWM)) ||
      aKidReflowInput.mFlags.mIOffsetsNeedCSSAlign ||
      aKidReflowInput.mFlags.mBOffsetsNeedCSSAlign) {
    // placeholderContainer is used in each of the m{I,B}OffsetsNeedCSSAlign
    // clauses. We declare it at this scope so we can avoid having to look
    // it up twice (and only look it up if it's needed).
    nsContainerFrame* placeholderContainer = nullptr;

    if (NS_AUTOOFFSET == aOffsets.IStart(outerWM)) {
      NS_ASSERTION(NS_AUTOOFFSET != aOffsets.IEnd(outerWM),
                   "Can't solve for both start and end");
      aOffsets.IStart(outerWM) =
          aCBSize.ISize(outerWM) - aOffsets.IEnd(outerWM) -
          aMargin.IStartEnd(outerWM) - aKidSize.ISize(outerWM);
    } else if (aKidReflowInput.mFlags.mIOffsetsNeedCSSAlign) {
      placeholderContainer = GetPlaceholderContainer(aKidReflowInput.mFrame);
      nscoord offset = OffsetToAlignedStaticPos(
          aKidReflowInput, aKidSize, aCBSize, placeholderContainer, outerWM,
          LogicalAxis::Inline, Nothing{}, aAnchorOffsetInfo);
      // Shift IStart from its current position (at start corner of the
      // alignment container) by the returned offset.  And set IEnd to the
      // distance between the kid's end edge to containing block's end edge.
      aOffsets.IStart(outerWM) += offset;
      aOffsets.IEnd(outerWM) =
          aCBSize.ISize(outerWM) -
          (aOffsets.IStart(outerWM) + aKidSize.ISize(outerWM));
    }

    if (NS_AUTOOFFSET == aOffsets.BStart(outerWM)) {
      aOffsets.BStart(outerWM) =
          aCBSize.BSize(outerWM) - aOffsets.BEnd(outerWM) -
          aMargin.BStartEnd(outerWM) - aKidSize.BSize(outerWM);
    } else if (aKidReflowInput.mFlags.mBOffsetsNeedCSSAlign) {
      if (!placeholderContainer) {
        placeholderContainer = GetPlaceholderContainer(aKidReflowInput.mFrame);
      }
      nscoord offset = OffsetToAlignedStaticPos(
          aKidReflowInput, aKidSize, aCBSize, placeholderContainer, outerWM,
          LogicalAxis::Block, Nothing{}, aAnchorOffsetInfo);
      // Shift BStart from its current position (at start corner of the
      // alignment container) by the returned offset.  And set BEnd to the
      // distance between the kid's end edge to containing block's end edge.
      aOffsets.BStart(outerWM) += offset;
      aOffsets.BEnd(outerWM) =
          aCBSize.BSize(outerWM) -
          (aOffsets.BStart(outerWM) + aKidSize.BSize(outerWM));
    }
    aKidReflowInput.SetComputedLogicalOffsets(outerWM, aOffsets);
  }
}

void AbsoluteContainingBlock::ResolveAutoMarginsAfterLayout(
    ReflowInput& aKidReflowInput, const LogicalSize& aCBSize,
    const LogicalSize& aKidSize, LogicalMargin& aMargin,
    const LogicalMargin& aOffsets) {
  WritingMode outerWM = aKidReflowInput.mParentReflowInput->GetWritingMode();
  const auto& styleMargin = aKidReflowInput.mStyleMargin;
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(&aKidReflowInput);

  auto ResolveMarginsInAxis = [&](LogicalAxis aAxis) {
    const auto startSide = MakeLogicalSide(aAxis, LogicalEdge::Start);
    const auto endSide = MakeLogicalSide(aAxis, LogicalEdge::End);

    // No need to substract border sizes because aKidSize has it included
    // already. Also, if any offset is auto, the auto margin resolves to zero.
    // https://drafts.csswg.org/css-position-3/#abspos-margins
    const bool autoOffset =
        aOffsets.Side(startSide, outerWM) == NS_AUTOOFFSET ||
        aOffsets.Side(endSide, outerWM) == NS_AUTOOFFSET;

    nscoord availMarginSpace;
    if (autoOffset) {
      availMarginSpace = 0;
    } else {
      const nscoord stretchFitSize = std::max(
          0, aCBSize.Size(aAxis, outerWM) - aOffsets.StartEnd(aAxis, outerWM) -
                 aMargin.StartEnd(aAxis, outerWM));
      availMarginSpace = stretchFitSize - aKidSize.Size(aAxis, outerWM);
    }

    const bool startSideMarginIsAuto =
        styleMargin->GetMargin(startSide, outerWM, anchorResolutionParams)
            ->IsAuto();
    const bool endSideMarginIsAuto =
        styleMargin->GetMargin(endSide, outerWM, anchorResolutionParams)
            ->IsAuto();

    if (aAxis == LogicalAxis::Inline) {
      ReflowInput::ComputeAbsPosInlineAutoMargin(availMarginSpace, outerWM,
                                                 startSideMarginIsAuto,
                                                 endSideMarginIsAuto, aMargin);
    } else {
      ReflowInput::ComputeAbsPosBlockAutoMargin(availMarginSpace, outerWM,
                                                startSideMarginIsAuto,
                                                endSideMarginIsAuto, aMargin);
    }
  };

  ResolveMarginsInAxis(LogicalAxis::Inline);
  ResolveMarginsInAxis(LogicalAxis::Block);
  aKidReflowInput.SetComputedLogicalMargin(outerWM, aMargin);

  nsMargin* propValue =
      aKidReflowInput.mFrame->GetProperty(nsIFrame::UsedMarginProperty());
  // InitOffsets should've created a UsedMarginProperty for us, if any margin is
  // auto.
  MOZ_ASSERT_IF(
      styleMargin->HasInlineAxisAuto(outerWM, anchorResolutionParams) ||
          styleMargin->HasBlockAxisAuto(outerWM, anchorResolutionParams),
      propValue);
  if (propValue) {
    *propValue = aMargin.GetPhysicalMargin(outerWM);
  }
}

struct None {};
using OldCacheState = Variant<None, AnchorPosResolutionCache::PositionTryBackup,
                              AnchorPosResolutionCache::PositionTryFullBackup>;

struct MOZ_STACK_CLASS MOZ_RAII AutoFallbackStyleSetter {
  AutoFallbackStyleSetter(nsIFrame* aFrame, ComputedStyle* aFallbackStyle,
                          AnchorPosResolutionCache* aCache, bool aIsFirstTry)
      : mFrame(aFrame), mCache{aCache}, mOldCacheState{None{}} {
    if (aFallbackStyle) {
      mOldStyle = aFrame->SetComputedStyleWithoutNotification(aFallbackStyle);
    }
    // We need to be able to "go back" to the old, first try (Which is not
    // necessarily base style) cache.
    if (!aIsFirstTry && aCache) {
      // New fallback could just be a flip keyword.
      if (mOldStyle && mOldStyle->StylePosition()->mPositionAnchor !=
                           aFrame->StylePosition()->mPositionAnchor) {
        mOldCacheState =
            OldCacheState{aCache->TryPositionWithDifferentDefaultAnchor()};
        // TODO(dshin, bug 2014913): Fragmentation _can_ change the containing
        // block size from its unfragmented version, and that may cause us to
        // choose a different fallback, and hit this code path.
        *aCache = PopulateAnchorResolutionCache(aFrame, aCache->mReferenceData,
                                                false);
      } else {
        mOldCacheState =
            OldCacheState{aCache->TryPositionWithSameDefaultAnchor()};
        if (aCache->mDefaultAnchorCache.mAnchor) {
          aCache->mReferenceData->AdjustCompensatingForScroll(
              CheckEarlyCompensatingForScroll(aFrame));
        }
      }
    }
  }

  ~AutoFallbackStyleSetter() {
    if (mOldStyle) {
      mFrame->SetComputedStyleWithoutNotification(std::move(mOldStyle));
    }
    std::move(mOldCacheState)
        .match(
            [](None&&) {},
            [&](AnchorPosResolutionCache::PositionTryBackup&& aBackup) {
              mCache->UndoTryPositionWithSameDefaultAnchor(std::move(aBackup));
            },
            [&](AnchorPosResolutionCache::PositionTryFullBackup&& aBackup) {
              mCache->UndoTryPositionWithDifferentDefaultAnchor(
                  std::move(aBackup));
            });
  }

  void CommitCurrentFallback() {
    mOldCacheState = OldCacheState{None{}};
    // If we have a non-layout dependent margin / paddings, which are different
    // from our original style, we need to make sure to commit it into the frame
    // property so that it doesn't get lost after returning from reflow.
    nsMargin margin;
    if (mOldStyle &&
        !mOldStyle->StyleMargin()->MarginEquals(*mFrame->StyleMargin()) &&
        mFrame->StyleMargin()->GetMargin(margin)) {
      mFrame->SetOrUpdateDeletableProperty(nsIFrame::UsedMarginProperty(),
                                           margin);
    }
  }

 private:
  nsIFrame* const mFrame;
  RefPtr<ComputedStyle> mOldStyle;
  AnchorPosResolutionCache* const mCache;
  OldCacheState mOldCacheState;
};

// XXX Optimize the case where it's a resize reflow and the absolutely
// positioned child has the exact same size and position and skip the
// reflow...
void AbsoluteContainingBlock::ReflowAbsoluteFrame(
    nsContainerFrame* aDelegatingFrame, nsPresContext* aPresContext,
    const ReflowInput& aReflowInput,
    const ContainingBlockRects& aContainingBlockRects, AbsPosReflowFlags aFlags,
    nsIFrame* aKidFrame, nsReflowStatus& aStatus, OverflowAreas* aOverflowAreas,
    const ContainingBlockRects* aFragmentedContainingBlockRects,
    AnchorPosResolutionCache* aAnchorPosResolutionCache,
    bool aReuseUnfragmentedAnchorPosReferences) {
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsIFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    fmt::println("abspos {}: begin reflow: availSize={}, orig cbRect={}",
                 aKidFrame->ListTag(), aReflowInput.AvailableSize(),
                 aContainingBlockRects.mLocal);
  }
  AutoNoisyIndenter indent(nsBlockFrame::gNoisy);
#endif  // DEBUG

  const WritingMode outerWM = aReflowInput.GetWritingMode();
  const WritingMode wm = aKidFrame->GetWritingMode();

  const bool isGrid = aFlags.contains(AbsPosReflowFlag::IsGridContainerCB);
  const auto* kidStylePosition = aKidFrame->StylePosition();
  auto fallbacks = kidStylePosition->mPositionTryFallbacks.value._0.AsSpan();
  const auto fallbackScope = kidStylePosition->mPositionTryFallbacks.scope;
  Maybe<uint32_t> currentFallbackIndex;
  const StylePositionTryFallbacksItem* currentFallback = nullptr;
  RefPtr<ComputedStyle> currentFallbackStyle;
  RefPtr<ComputedStyle> firstTryStyle;
  Maybe<uint32_t> firstTryIndex;
  // If non-'normal' position-try-order is in effect, we keep track of the
  // index of the "best" option seen, and its size in the relevant axis, so
  // that once all fallbacks have been considered we can reset to the one
  // that provided the most space.
  Maybe<uint32_t> bestIndex;
  nscoord bestSize = -1;
  // Flag to indicate that we've determined which fallback to use and should
  // exit the loop.
  bool finalizing = false;

  auto tryOrder = kidStylePosition->mPositionTryOrder;
  // If position-try-order is a logical value, resolve to physical using
  // the containing block's writing mode.
  switch (tryOrder) {
    case StylePositionTryOrder::MostInlineSize:
      tryOrder = outerWM.IsVertical() ? StylePositionTryOrder::MostHeight
                                      : StylePositionTryOrder::MostWidth;
      break;
    case StylePositionTryOrder::MostBlockSize:
      tryOrder = outerWM.IsVertical() ? StylePositionTryOrder::MostWidth
                                      : StylePositionTryOrder::MostHeight;
      break;
    default:
      break;
  }

  const auto* baseStyle = aKidFrame->Style();
  // Set the current fallback to the given index, or reset to the base position
  // if Nothing() is passed.
  auto SeekFallbackTo = [&](Maybe<uint32_t> aIndex) -> bool {
    if (!aIndex) {
      currentFallbackIndex = Nothing();
      currentFallback = nullptr;
      currentFallbackStyle = nullptr;
      return true;
    }
    uint32_t index = *aIndex;
    if (index >= fallbacks.Length()) {
      return false;
    }

    const StylePositionTryFallbacksItem* nextFallback;
    RefPtr<ComputedStyle> nextFallbackStyle;
    while (true) {
      nextFallback = &fallbacks[index];
      nextFallbackStyle = aPresContext->StyleSet()->ResolvePositionTry(
          fallbackScope, *aKidFrame->GetContent()->AsElement(), *baseStyle,
          *nextFallback);
      if (nextFallbackStyle) {
        break;
      }
      // No @position-try rule for this name was found, per spec we should
      // skip it.
      index++;
      if (index >= fallbacks.Length()) {
        return false;
      }
    }
    currentFallbackIndex = Some(index);
    currentFallback = nextFallback;
    currentFallbackStyle = std::move(nextFallbackStyle);
    return true;
  };

  // Advance to the next fallback to be tried. Normally this is simply the next
  // index in the position-try-fallbacks list, but we have some special cases:
  // - if we're currently at the last-successful fallback (recorded as
  //   firstTryIndex), we "advance" to the base position
  // - we skip the last-successful fallback when we reach its position again
  auto TryAdvanceFallback = [&]() -> bool {
    if (fallbacks.IsEmpty()) {
      return false;
    }
    if (firstTryIndex && currentFallbackIndex == firstTryIndex) {
      return SeekFallbackTo(Nothing());
    }
    uint32_t nextFallbackIndex =
        currentFallbackIndex ? *currentFallbackIndex + 1 : 0;
    if (firstTryIndex && nextFallbackIndex == *firstTryIndex) {
      ++nextFallbackIndex;
    }
    return SeekFallbackTo(Some(nextFallbackIndex));
  };

  Maybe<nsRect> firstTryRect;
  if (auto* lastSuccessfulPosition =
          aKidFrame->GetProperty(nsIFrame::LastSuccessfulPositionFallback());
      lastSuccessfulPosition && lastSuccessfulPosition->mRecordedIndex &&
      SeekFallbackTo(lastSuccessfulPosition->mRecordedIndex)) {
    // Remember which fallback we're trying first; also record its style,
    // in case we need to restore it later.
    firstTryIndex = lastSuccessfulPosition->mRecordedIndex;
    firstTryStyle = currentFallbackStyle;
  }

  // Assume we *are* overflowing the CB and if we find a fallback that doesn't
  // overflow, we set this to false and break the loop.
  bool isOverflowingCB = true;

  do {
    AutoFallbackStyleSetter fallback(aKidFrame, currentFallbackStyle,
                                     aAnchorPosResolutionCache,
                                     firstTryIndex == currentFallbackIndex);
    auto cb = ComputeContainingBlock(isGrid, aDelegatingFrame, aReflowInput,
                                     aContainingBlockRects, aKidFrame,
                                     aAnchorPosResolutionCache,
                                     aReuseUnfragmentedAnchorPosReferences);
    PhysicalAxes earlyScrollCompensation;
    if (aAnchorPosResolutionCache) {
      const auto& originalCb = cb.mMaybeScrollableRect;
      aAnchorPosResolutionCache->mReferenceData->mOriginalContainingBlockRect =
          originalCb;
      // Stash the adjusted containing block as well, since the insets need to
      // resolve against the adjusted CB, e.g. With `position-area: bottom
      // right;`, + `left: anchor(right);`
      // resolves to 0.
      aAnchorPosResolutionCache->mReferenceData->mAdjustedContainingBlock =
          cb.mFinalRect;
      // May need to recompute scroll compensation if e.g. anchor-center in one
      // axis, then `anchor(--default-anchor)` in another.
      earlyScrollCompensation = aAnchorPosResolutionCache->mReferenceData
                                    ->CompensatingForScrollAxes();
    }
    const LogicalSize cbSize(outerWM, cb.mFinalRect.Size());

    ReflowInput::InitFlags initFlags;
    const bool staticPosIsCBOrigin = [&] {
      if (aFlags.contains(AbsPosReflowFlag::IsGridContainerCB)) {
        // When a grid container generates the abs.pos. CB for a *child* then
        // the static position is determined via CSS Box Alignment within the
        // abs.pos. CB (a grid area, i.e. a piece of the grid). In this
        // scenario, due to the multiple coordinate spaces in play, we use a
        // convenience flag to simply have the child's ReflowInput give it a
        // static position at its abs.pos. CB origin, and then we'll align &
        // offset it from there.
        nsIFrame* placeholder = aKidFrame->GetPlaceholderFrame();
        if (placeholder && placeholder->GetParent() == aDelegatingFrame) {
          return true;
        }
      }
      if (aKidFrame->IsMenuPopupFrame()) {
        // Popups never use their static pos.
        return true;
      }
      // TODO(emilio): Either reparent the top layer placeholder frames to the
      // viewport, or return true here for top layer frames more generally (not
      // only menupopups), see https://github.com/w3c/csswg-drafts/issues/8040.
      return false;
    }();

    if (staticPosIsCBOrigin) {
      initFlags += ReflowInput::InitFlag::StaticPosIsCBOrigin;
    }

    const bool kidFrameMaySplit =
        aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&

        // Don't split if told not to (e.g. for fixed frames)
        aFlags.contains(AbsPosReflowFlag::AllowFragmentation) &&

        // In the legacy (pref-off) behavior, don't split abspos frames whose
        // containing block is formed by an inline frame.
        (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() ||
         !aDelegatingFrame->IsInlineFrameOrSubclass()) &&

        // Bug 1588623: Support splitting absolute positioned multicol
        // containers.
        !aKidFrame->IsColumnSetWrapperFrame();

    // Get the border values
    const LogicalMargin border =
        aDelegatingFrame->GetLogicalUsedBorder(outerWM).ApplySkipSides(
            aDelegatingFrame->PreReflowBlockLevelLogicalSkipSides());

    const nsIFrame* kidPrevInFlow = aKidFrame->GetPrevInFlow();
    const LogicalPoint* const unfragmentedPosition =
        GetUnfragmentedPosition(aReflowInput, aKidFrame);
    nscoord availBSize;
    if (kidFrameMaySplit) {
      if (unfragmentedPosition) {
        // The unfragmented position is relative to the absolute containing
        // block's first fragment, so we subtract
        // mCumulativeContainingBlockBSize to get the position in this fragment.
        const nscoord kidBPosInThisFragment =
            unfragmentedPosition->B(outerWM) - mCumulativeContainingBlockBSize;
        availBSize = aReflowInput.AvailableBSize() - kidBPosInThisFragment;
        NS_ASSERTION(availBSize >= 0, "Why is available block-size < 0?");
      } else if (!aDelegatingFrame->GetPrevInFlow()) {
        // aDelegatingFrame is a first-in-flow. We subtract our containing
        // block's border-block-start, to consider the available space as
        // starting at the containing block's padding-edge.
        availBSize = aReflowInput.AvailableBSize() - border.BStart(outerWM);
      } else {
        // aDelegatingFrame is *not* a first-in-flow. Then we don't need to
        // subtract the containing block's border. Instead, we consider this
        // whole fragment as our available space, i.e., we allow abspos
        // continuations to overlap any border that their containing block
        // parent might have (including borders generated by
        // 'box-decoration-break:clone').
        availBSize = aReflowInput.AvailableBSize();
      }
    } else {
      availBSize = NS_UNCONSTRAINEDSIZE;
    }
    StyleSizeOverrides sizeOverrides;
    Maybe<nscoord> unfragmentedBSizeAsMinBSize;
    if (const auto* unfragmentedSize =
            GetUnfragmentedSize(aReflowInput, aKidFrame)) {
      // ReflowInput for fragmented absolute frames will not compute absolute
      // constraints - it'd be redundant anyway, so just use the unfragmented
      // size and skip it.
      auto resolutionParams =
          AnchorPosResolutionParams::From(aKidFrame, aAnchorPosResolutionCache);
      const auto* stylePos = aKidFrame->StylePosition();
      if (stylePos->ISize(wm, resolutionParams)->IsAuto()) {
        sizeOverrides.mStyleISize.emplace(
            StyleSize::FromAppUnits(unfragmentedSize->ISize(wm)));
      }
      if (stylePos->BSize(wm, resolutionParams)->IsAuto()) {
        unfragmentedBSizeAsMinBSize = Some(unfragmentedSize->BSize(wm));
      }
    }
    const LogicalSize availSize(outerWM, cbSize.ISize(outerWM), availBSize);
    ReflowInput kidReflowInput(aPresContext, aReflowInput, aKidFrame,
                               availSize.ConvertTo(wm, outerWM),
                               Some(cbSize.ConvertTo(wm, outerWM)), initFlags,
                               sizeOverrides, {}, aAnchorPosResolutionCache);

    if (unfragmentedBSizeAsMinBSize) {
      // The kid has 'auto' block-size. Instead of setting unfragmented
      // block-size to sizeOverrides above, use it as a min-block-size lower
      // bound to keep allowing fragmentation-imposed block-size growth.
      const nscoord contentBSize =
          *unfragmentedBSizeAsMinBSize -
          (kidReflowInput.mStylePosition->mBoxSizing ==
                   StyleBoxSizing::BorderBox
               ? kidReflowInput.ComputedLogicalBorderPadding(wm).BStartEnd(wm)
               : 0);
      kidReflowInput.SetComputedMinBSize(contentBSize);
    }

    if (unfragmentedPosition) {
      // Do nothing. If aKidFrame may split, we've adjusted availBSize before
      // creating kidReflowInput.
    } else if (!kidPrevInFlow) {
      // ReflowInput's constructor may change the available block-size to
      // unconstrained, e.g. in orthogonal reflow, so we retrieve it again and
      // account for kid's constraints in its own writing-mode if needed.
      nscoord kidAvailBSize = kidReflowInput.AvailableBSize();
      if (kidAvailBSize != NS_UNCONSTRAINEDSIZE) {
        kidAvailBSize -= kidReflowInput.ComputedLogicalMargin(wm).BStart(wm);
        nscoord kidOffsetBStart =
            kidReflowInput.ComputedLogicalOffsets(wm).BStart(wm);
        if (kidOffsetBStart != NS_AUTOOFFSET) {
          kidOffsetBStart -= mCumulativeContainingBlockBSize;
          kidAvailBSize -= kidOffsetBStart;
        }
        kidReflowInput.SetAvailableBSize(kidAvailBSize);
      }
    }

    // Do the reflow
    ReflowOutput kidDesiredSize(kidReflowInput);
    aKidFrame->Reflow(aPresContext, kidDesiredSize, kidReflowInput, aStatus);

    nsMargin insets;
    if (aKidFrame->IsMenuPopupFrame()) {
      // Do nothing. Popup frame will handle its own positioning.
    } else if (unfragmentedPosition || kidPrevInFlow) {
      LogicalPoint kidPos(outerWM);
      if (unfragmentedPosition) {
        MOZ_ASSERT(!kidPrevInFlow, "aKidFrame should be a first-in-flow!");

        // aKidFrame is a first-in-flow. Place it at its unfragmented position
        // with the block-start position adjusted.
        kidPos = *unfragmentedPosition;
        kidPos.B(outerWM) -= mCumulativeContainingBlockBSize;
      } else {
        // aKidFrame is a next-in-flow. Place it at the block-edge start of its
        // containing block, with the same inline-position as in its
        // unfragmented position.
        const LogicalPoint* unfragPos =
            GetUnfragmentedPosition(aReflowInput, aKidFrame->FirstInFlow());
        MOZ_ASSERT(unfragPos,
                   "A first-in-flow should have stored an unfragmented "
                   "position during a measuring reflow!");
        if (unfragPos) [[likely]] {
          kidPos = *unfragPos;
          kidPos.B(outerWM) = 0;
        }
      }
      const LogicalSize kidSize = kidDesiredSize.Size(outerWM);
      nsRect kidRect;
      if (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled() &&
          aDelegatingFrame->IsInlineFrameOrSubclass()) {
        nsIFrame* blockAncestor =
            nsLayoutUtils::FindNearestBlockAncestor(aDelegatingFrame);

        // The block ancestor's size, stashed in the inline reflow input's
        // containing block size, used to translate the coordinate spaces
        // between the block ancestor and aDelegatingFrame (Note we cannot use
        // the block ancestor's GetSize() because it is still in reflow).
        const nsSize blockAncestorSize =
            aReflowInput.mContainingBlockSize.GetPhysicalSize(outerWM);
        kidRect = LogicalRect(outerWM, kidPos, kidSize)
                      .GetPhysicalRect(outerWM, blockAncestorSize) -
                  aDelegatingFrame->GetOffsetTo(blockAncestor);
      } else {
        // We can have reflows in a spanner that is also a multicol.
        const auto maybeFragmentedCbSize =
            (aFragmentedContainingBlockRects ? *aFragmentedContainingBlockRects
                                             : aContainingBlockRects)
                .mLocal.Size();
        // TODO(dshin): Fix this up for anchor positioning. Scroll containers
        // are monolithic and will not fragment, but an anchor-positioned
        // frame's percentage size still needs to resolve against the correct
        // containing block.
        const LogicalSize unmodifiedCBSize(outerWM, maybeFragmentedCbSize);
        const nsSize cbBorderBoxSize =
            (unmodifiedCBSize + border.Size(outerWM)).GetPhysicalSize(outerWM);
        kidRect = LogicalRect(outerWM, kidPos, kidSize)
                      .GetPhysicalRect(outerWM, cbBorderBoxSize);
      }
      aKidFrame->SetRect(kidRect);
    } else {
      // Position the child relative to our padding edge.
      const LogicalSize kidSize = kidDesiredSize.Size(outerWM);

      LogicalMargin offsets = kidReflowInput.ComputedLogicalOffsets(outerWM);
      LogicalMargin margin = kidReflowInput.ComputedLogicalMargin(outerWM);

      // If we're doing CSS Box Alignment in either axis, that will apply the
      // margin for us in that axis (since the thing that's aligned is the
      // margin box).  So, we clear out the margin here to avoid applying it
      // twice.
      if (kidReflowInput.mFlags.mIOffsetsNeedCSSAlign) {
        margin.IStart(outerWM) = margin.IEnd(outerWM) = 0;
      }
      if (kidReflowInput.mFlags.mBOffsetsNeedCSSAlign) {
        margin.BStart(outerWM) = margin.BEnd(outerWM) = 0;
      }

      // If we're solving for start in either inline or block direction,
      // then compute it now that we know the dimensions.
      ResolveSizeDependentOffsets(kidReflowInput, cbSize, kidSize, margin,
                                  cb.GetAnchorOffsetInfo(), offsets);

      ResolveAutoMarginsAfterLayout(kidReflowInput, cbSize, kidSize, margin,
                                    offsets);

      // If the inset is constrained as non-auto, we may have a child that does
      // not fill out the inset-reduced containing block. In this case, we need
      // to align the child by its margin box:
      // https://drafts.csswg.org/css-position-3/#abspos-layout
      const auto* stylePos = aKidFrame->StylePosition();
      const auto anchorResolutionParams =
          AnchorPosOffsetResolutionParams::ExplicitCBFrameSize(
              AnchorPosResolutionParams::From(aKidFrame,
                                              aAnchorPosResolutionCache),
              &cbSize);
      const bool iStartInsetAuto =
          stylePos
              ->GetAnchorResolvedInset(LogicalSide::IStart, outerWM,
                                       anchorResolutionParams)
              ->IsAuto();
      const bool iEndInsetAuto =
          stylePos
              ->GetAnchorResolvedInset(LogicalSide::IEnd, outerWM,
                                       anchorResolutionParams)
              ->IsAuto();
      const bool iInsetAuto = iStartInsetAuto || iEndInsetAuto;

      const bool bStartInsetAuto =
          stylePos
              ->GetAnchorResolvedInset(LogicalSide::BStart, outerWM,
                                       anchorResolutionParams)
              ->IsAuto();
      const bool bEndInsetAuto =
          stylePos
              ->GetAnchorResolvedInset(LogicalSide::BEnd, outerWM,
                                       anchorResolutionParams)
              ->IsAuto();
      const bool bInsetAuto = bStartInsetAuto || bEndInsetAuto;
      const LogicalSize kidMarginBox{
          outerWM, margin.IStartEnd(outerWM) + kidSize.ISize(outerWM),
          margin.BStartEnd(outerWM) + kidSize.BSize(outerWM)};
      const auto* placeholderContainer =
          GetPlaceholderContainer(kidReflowInput.mFrame);

      insets = [&]() {
        auto result = offsets;
        // Zero out weaker insets, if one exists - This offset gets forced to
        // the margin edge of the child on that side, and for the purposes of
        // overflow checks, we consider them to be zero.
        if (iStartInsetAuto && !iEndInsetAuto) {
          result.IStart(outerWM) = 0;
        } else if (iInsetAuto) {
          result.IEnd(outerWM) = 0;
        }
        if (bStartInsetAuto && !bEndInsetAuto) {
          result.BStart(outerWM) = 0;
        } else if (bInsetAuto) {
          result.BEnd(outerWM) = 0;
        }
        return result.GetPhysicalMargin(outerWM);
      }();
      if (aAnchorPosResolutionCache) {
        aAnchorPosResolutionCache->mReferenceData->mInsets = insets;
      }
      if (!iInsetAuto) {
        MOZ_ASSERT(
            !kidReflowInput.mFlags.mIOffsetsNeedCSSAlign,
            "Non-auto inline inset but requires CSS alignment for static "
            "position?");
        auto alignOffset = OffsetToAlignedStaticPos(
            kidReflowInput, kidMarginBox, cbSize, placeholderContainer, outerWM,
            LogicalAxis::Inline,
            Some(NonAutoAlignParams{
                offsets.IStart(outerWM),
                offsets.IEnd(outerWM),
            }),
            cb.GetAnchorOffsetInfo());

        offsets.IStart(outerWM) += alignOffset;
        offsets.IEnd(outerWM) =
            cbSize.ISize(outerWM) -
            (offsets.IStart(outerWM) + kidMarginBox.ISize(outerWM));
      }
      if (!bInsetAuto) {
        MOZ_ASSERT(!kidReflowInput.mFlags.mBOffsetsNeedCSSAlign,
                   "Non-auto block inset but requires CSS alignment for static "
                   "position?");
        auto alignOffset = OffsetToAlignedStaticPos(
            kidReflowInput, kidMarginBox, cbSize, placeholderContainer, outerWM,
            LogicalAxis::Block,
            Some(NonAutoAlignParams{
                offsets.BStart(outerWM),
                offsets.BEnd(outerWM),
            }),
            cb.GetAnchorOffsetInfo());
        offsets.BStart(outerWM) += alignOffset;
        offsets.BEnd(outerWM) =
            cbSize.BSize(outerWM) -
            (offsets.BStart(outerWM) + kidMarginBox.BSize(outerWM));
      }

      LogicalRect rect(
          outerWM, offsets.StartOffset(outerWM) + margin.StartOffset(outerWM),
          kidSize);
      nsRect r = rect.GetPhysicalRect(outerWM, cbSize.GetPhysicalSize(outerWM));

      // So far, we've positioned against the padding edge of the containing
      // block, which is necessary for inset computation. However, the position
      // of a frame originates against the border box.
      r += cb.mFinalRect.TopLeft();

      const auto scrollShift = [&]() -> nsPoint {
        if (!aAnchorPosResolutionCache) {
          return {};
        }
        auto* referenceData = aAnchorPosResolutionCache->mReferenceData;
        if (referenceData->CompensatingForScrollAxes().isEmpty()) {
          return {};
        }
        if (cb.mAnchorOffsetInfo &&
            earlyScrollCompensation ==
                referenceData->CompensatingForScrollAxes()) {
          // Able to use the already-resolved value.
          return cb.mAnchorOffsetInfo->mScrollOffset;
        }
        return AnchorPositioningUtils::GetScrollOffsetFor(
            referenceData->CompensatingForScrollAxes(), aKidFrame,
            aAnchorPosResolutionCache->mDefaultAnchorCache);
      }();
      if (aAnchorPosResolutionCache) {
        auto* referenceData = aAnchorPosResolutionCache->mReferenceData;
        const auto& remembered = referenceData->mRememberedScrollOffset;
        referenceData->mDefaultScrollShift =
            remembered ? scrollShift - remembered->mOffset : scrollShift;
      }
      r -= scrollShift;
      aKidFrame->SetRect(r);
    }

    aKidFrame->DidReflow(aPresContext, &kidReflowInput);

    if (!firstTryRect) {
      firstTryRect.emplace(aKidFrame->GetRect());
    }

    const auto FitsInContainingBlock = [&]() {
      if (aAnchorPosResolutionCache) {
        return AnchorPositioningUtils::FitsInContainingBlock(
            aKidFrame, *aAnchorPosResolutionCache->mReferenceData);
      }
      auto imcbSize = cb.mFinalRect.Size();
      imcbSize -= nsSize{insets.LeftRight(), insets.TopBottom()};
      return aKidFrame->GetMarginRectRelativeToSelf().Size() <= imcbSize;
    };

    // FIXME(bug 2004495): Per spec this should be the inset-modified
    // containing-block, see:
    // https://drafts.csswg.org/css-anchor-position-1/#fallback-apply
    const auto fits = aStatus.IsComplete() && FitsInContainingBlock();
    // If the position-try-order is normal and the base style fits, we
    // can skip the comparison of other styles. However, if position-try-order
    // is anything other than normal, we have to check all the styles in that
    // order including the base style.
    if (fallbacks.IsEmpty() || finalizing ||
        (fits && tryOrder == StylePositionTryOrder::Normal)) {
      // We completed the reflow - Either we had a fallback that fit, or we
      // didn't have any to try in the first place.
      isOverflowingCB = !fits;
      fallback.CommitCurrentFallback();
      break;
    }

    if (fits) {
      auto imcbSize = cb.mFinalRect.Size();
      imcbSize -= nsSize{insets.LeftRight(), insets.TopBottom()};
      switch (tryOrder) {
        case StylePositionTryOrder::MostWidth:
          if (imcbSize.Width() > bestSize) {
            bestSize = imcbSize.Width();
            bestIndex = currentFallbackIndex;
          }
          break;
        case StylePositionTryOrder::MostHeight:
          if (imcbSize.Height() > bestSize) {
            bestSize = imcbSize.Height();
            bestIndex = currentFallbackIndex;
          }
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("unexpected try-order value");
          break;
      }
    }

    if (!TryAdvanceFallback()) {
      // If there are no further fallbacks, we're done.
      if (bestSize >= 0) {
        SeekFallbackTo(bestIndex);
      } else {
        // If we're going to roll back to the first try position, and the
        // target's size was different, we need to do a "finalizing" reflow
        // to ensure the inner layout is correct. If the size is unchanged,
        // we can just break the fallback loop now.
        if (isOverflowingCB && firstTryRect &&
            firstTryRect->Size() != aKidFrame->GetSize()) {
          SeekFallbackTo(firstTryIndex);
        } else {
          break;
        }
      }
      // The fallback we've just selected is the final choice, regardless of
      // whether it overflows.
      finalizing = true;
    }

    // Try with the next fallback.
    aKidFrame->AddStateBits(NS_FRAME_IS_DIRTY);
    aStatus.Reset();
  } while (true);

  [&]() {
    if (!isOverflowingCB || !firstTryRect) {
      return;
    }
    // We gave up applying fallbacks. Recover previous values, if changed, and
    // reset currentFallbackIndex/Style to match.
    // Because we rolled back to first try data, our cache should be up-to-date.
    currentFallbackIndex = firstTryIndex;
    currentFallbackStyle = firstTryStyle;
    auto rect = *firstTryRect;
    if (isOverflowingCB &&
        !aKidFrame->StylePosition()->mPositionArea.IsNone()) {
      // The anchored element overflows the IMCB of its position-area. Would it
      // have fit within the original CB? If so, shift it to stay within that.
      if (rect.width <= aContainingBlockRects.mLocal.width &&
          rect.height <= aContainingBlockRects.mLocal.height) {
        if (rect.x < aContainingBlockRects.mLocal.x) {
          rect.x = aContainingBlockRects.mLocal.x;
        } else if (rect.XMost() > aContainingBlockRects.mLocal.XMost()) {
          rect.x = aContainingBlockRects.mLocal.XMost() - rect.width;
        }
        if (rect.y < aContainingBlockRects.mLocal.y) {
          rect.y = aContainingBlockRects.mLocal.y;
        } else if (rect.YMost() > aContainingBlockRects.mLocal.YMost()) {
          rect.y = aContainingBlockRects.mLocal.YMost() - rect.height;
        }
      }
    }
    if (rect.TopLeft() == aKidFrame->GetPosition()) {
      return;
    }
    aKidFrame->SetPosition(rect.TopLeft());
    if (aKidFrame->FrameMaintainsOverflow()) {
      aKidFrame->UpdateOverflow();
    }
  }();

  // NOTE: We don't touch the last recorded index, that's done at resize
  // observer time.
  auto* lastSuccessfulPosition =
      currentFallbackIndex
          ? aKidFrame->GetOrCreateDeletableProperty(
                nsIFrame::LastSuccessfulPositionFallback())
          : aKidFrame->GetProperty(nsIFrame::LastSuccessfulPositionFallback());
  if (lastSuccessfulPosition) {
    lastSuccessfulPosition->mLastIndex = currentFallbackIndex;
    lastSuccessfulPosition->mLastStyle = std::move(currentFallbackStyle);
    lastSuccessfulPosition->mTriedAllFallbacks = isOverflowingCB;
  }

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsIFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent - 1);
    fmt::println("abspos {}: rect {}", aKidFrame->ListTag(),
                 aKidFrame->GetRect());
  }
#endif
  // If author asked for `position-visibility: no-overflow` and we overflow
  // `usedCB`, treat as "strongly hidden". Note that for anchored frames this
  // happens in ComputePositionVisibility. But no-overflow also applies to
  // non-anchored frames.
  if (!aAnchorPosResolutionCache) {
    aKidFrame->AddOrRemoveStateBits(
        NS_FRAME_POSITION_VISIBILITY_HIDDEN,
        isOverflowingCB && aKidFrame->StylePosition()->mPositionVisibility &
                               StylePositionVisibility::NO_OVERFLOW);
  }

  if (aOverflowAreas) {
    aDelegatingFrame->ConsiderChildOverflow(
        *aOverflowAreas, aKidFrame, OverflowAreaUnionFlags::ChildIsAbsPos);
  }
}
