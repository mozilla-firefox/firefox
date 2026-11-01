/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * code for managing absolutely positioned children of a rendering
 * object that is a containing block for them
 */

#ifndef LAYOUT_GENERIC_ABSOLUTE_CONTAINING_BLOCK_H_
#define LAYOUT_GENERIC_ABSOLUTE_CONTAINING_BLOCK_H_

#include "nsFrameList.h"
#include "nsIFrame.h"

class nsContainerFrame;
class nsPresContext;

namespace mozilla {
enum class AbsPosReflowFlag : uint8_t {
  // Allow the children in the absolute containing block to fragment. Without
  // this flag, the children will be monolithic.
  AllowFragmentation,
  CBWidthChanged,
  CBHeightChanged,
  IsGridContainerCB,
};
using AbsPosReflowFlags = EnumSet<AbsPosReflowFlag>;
struct StylePositionArea;

/**
 * This class contains the logic for being an absolute containing block.  This
 * class is used within viewport frames (for frames representing content with
 * fixed position) and blocks (for frames representing absolutely positioned
 * content), since each set of frames is absolutely positioned with respect to
 * its parent.
 *
 * There is no principal child list, just a named child list which contains
 * the absolutely positioned frames (FrameChildListID::Absolute).
 *
 * All functions include as the first argument the frame that is delegating
 * the request.
 */
class AbsoluteContainingBlock {
 public:
  struct AnchorOffsetInfo {
    nsPoint mScrollOffset;
    StylePositionArea mResolvedPositionArea;
  };

  AbsoluteContainingBlock() = default;

  const nsFrameList& GetChildList() const { return mAbsoluteFrames; }
  const nsFrameList& GetPushedChildList() const {
    return mPushedAbsoluteFrames;
  }

  void SetInitialChildList(nsIFrame* aDelegatingFrame, FrameChildListID aListID,
                           nsFrameList&& aChildList);
  void AppendFrames(nsIFrame* aDelegatingFrame, FrameChildListID aListID,
                    nsFrameList&& aFrameList);
  void InsertFrames(nsIFrame* aDelegatingFrame, FrameChildListID aListID,
                    nsIFrame* aPrevFrame, nsFrameList&& aFrameList);
  void RemoveFrame(FrameDestroyContext&, FrameChildListID, nsIFrame*);

  /**
   * Return the pushed absolute frames. The caller is responsible for passing
   * the ownership of the frames to someone else, or destroying them.
   */
  [[nodiscard]] nsFrameList StealPushedChildList();

  // Used only when the delegating frame is an inline frame.
  enum class PrepareReason : uint8_t {
    // A normal reflow. Pull only from the continuations sharing
    // aDelegatingFrame's fragmentainer.
    Reflow,

    // aDelegatingFrame's continuations are about to be destroyed, so pull the
    // abspos children from all aDelegatingFrame's continuations, including
    // those in later fragmentainers, to keep them alive.
    Destroy,
  };

  /**
   * Prepare our absolute child list so that it is ready to reflow, or when
   * aDelegatingFrame's continuations are about to be destroyed.
   *
   * @return true if we have absolute frames after we return.
   */
  bool PrepareAbsoluteFrames(nsContainerFrame* aDelegatingFrame,
                             PrepareReason aReason = PrepareReason::Reflow);

  /**
   * Return true if we have absolute frames.
   *
   * Note: During reflow, consider calling PrepareAbsoluteFrames() rather than
   * this method; it moves absolute frames from other lists to mAbsoluteFrames,
   * which may be needed to get the correct result.
   */
  bool HasAbsoluteFrames() const { return mAbsoluteFrames.NotEmpty(); }

  /**
   * Called by the delegating frame after it has done its reflow first. This
   * function will reflow any absolutely positioned child frames that need to
   * be reflowed, e.g., because the absolutely positioned child frame has
   * 'auto' for an offset, or a percentage based width or height.
   *
   * @param aOverflowAreas, if non-null, is unioned with (in the local
   * coordinate space) the overflow areas of the absolutely positioned
   * children.
   * @param aReflowStatus This function merges in the statuses of the absolutely
   * positioned children's reflows.
   * @param aContainingBlock Rect representing the area where absolute
   * positioned children can be positioned. Generally, this is the padding rect
   * of `aDelegatingFrame` (Which would not have a valid mRect set during
   * reflow), offset against the `aDelegatingFrame`'s border rect.
   * @param aFlags zero or more AbsPosReflowFlags
   */
  void Reflow(nsContainerFrame* aDelegatingFrame, nsPresContext* aPresContext,
              const ReflowInput& aReflowInput, nsReflowStatus& aReflowStatus,
              const nsRect& aContainingBlock, AbsPosReflowFlags aFlags,
              OverflowAreas* aOverflowAreas);

  using DestroyContext = nsIFrame::DestroyContext;
  void DestroyFrames(DestroyContext&);

  /**
   * Mark our size-dependent absolute frames with NS_FRAME_HAS_DIRTY_CHILDREN
   * so that we'll make sure to reflow them.
   */
  void MarkSizeDependentFramesDirty();

  /**
   * Mark all our absolute frames with NS_FRAME_IS_DIRTY.
   */
  void MarkAllFramesDirty();

  /**
   * Rects for abspos frames to position against. Differences are relevant
   * for containing blocks that scroll.
   *
   * See https://drafts.csswg.org/css-position-4/#scrollable-cb
   */
  struct ContainingBlockRects {
    nsRect mLocal;
    nsRect mScrollable;
  };

  /**
   * Return the first inline continuation in aInlineFrame's fragmentainer, i.e.
   * the only continuation there that is allowed to own abspos children.
   */
  static nsIFrame* FirstInlineContinuationInFragmentainer(
      nsIFrame* aInlineFrame);

 protected:
  /**
   * Returns true if the position of aFrame depends on the position of
   * its placeholder or if the position or size of aFrame depends on a
   * containing block dimension that changed.
   */
  bool FrameDependsOnContainer(
      nsIFrame* aFrame, bool aCBWidthChanged, bool aCBHeightChanged,
      mozilla::AnchorPosResolutionCache* aAnchorPosResolutionCache = nullptr);

  /**
   * After an abspos child's size is known, this method can be used to
   * resolve size-dependent values in the ComputedLogicalOffsets on its
   * reflow input.
   *
   * aCBSize, aKidSize, aMargin, aOffsets, are all expected in the absolute
   * containing block's writing-mode.
   *
   * aOffset is an outparam.
   */
  void ResolveSizeDependentOffsets(ReflowInput& aKidReflowInput,
                                   const LogicalSize& aCBSize,
                                   const LogicalSize& aKidSize,
                                   const LogicalMargin& aMargin,
                                   const AnchorOffsetInfo& aAnchorOffsetInfo,
                                   LogicalMargin& aOffsets);

  /**
   * For frames that have intrinsic block sizes, since we want to use the
   * frame's actual instrinsic block-size, we don't compute margins in
   * InitAbsoluteConstraints because the block-size isn't computed yet. This
   * method computes the margins for them after layout.
   *
   * aCBSize, aKidSize, aMargin, aOffsets, are all expected in the absolute
   * containing block's writing-mode.
   *
   * aMargin and aOffsets are both outparams (though we only touch aOffsets if
   * the position is overconstrained)
   */
  void ResolveAutoMarginsAfterLayout(ReflowInput& aKidReflowInput,
                                     const LogicalSize& aCBSize,
                                     const LogicalSize& aKidSize,
                                     LogicalMargin& aMargin,
                                     const LogicalMargin& aOffsets);

  void ReflowAbsoluteFrame(
      nsContainerFrame* aDelegatingFrame, nsPresContext* aPresContext,
      const ReflowInput& aReflowInput,
      const ContainingBlockRects& aContainingBlockRects,
      AbsPosReflowFlags aFlags, nsIFrame* aKidFrame, nsReflowStatus& aStatus,
      OverflowAreas* aOverflowAreas,
      const ContainingBlockRects* aFragmentedContainingBlockRects,
      AnchorPosResolutionCache* aAnchorPosResolutionCache,
      bool aReuseUnfragmentedAnchorPosReferences);

  /**
   * Mark our absolute frames dirty.
   * @param aMarkAllDirty if true, all will be marked with NS_FRAME_IS_DIRTY.
   * Otherwise, the size-dependant ones will be marked with
   * NS_FRAME_HAS_DIRTY_CHILDREN.
   */
  void DoMarkFramesDirty(bool aMarkAllDirty);

  /**
   * Remove aFrame from one of our frame lists without destroying it.
   */
  void StealFrame(nsIFrame* aFrame);

  /**
   * Move any frame in our pushed absolute list into our absolute child list, if
   * it is a first-in-flow, or if its prev-in-flow is not present in our
   * absolute child list.
   *
   * @param aDelegatingFrame the frame that owns us.
   */
  void DrainPushedChildList(const nsIFrame* aDelegatingFrame);

  /**
   * Move abspos children from aContinuation's absolute containing block into
   * our absolute child list, reparenting them to aDelegatingFrame.
   *
   * @param aDelegatingFrame the frame that owns us.
   * @param aPullKind if PullKind::FirstInFlows, only first-in-flow children are
   * moved. If PullKind::All, all children, including those in aContinuation's
   * pushed child list, are moved.
   */
  enum class PullKind : uint8_t { FirstInFlows, All };
  void PullAbsoluteFramesFrom(nsContainerFrame* aDelegatingFrame,
                              nsIFrame* aContinuation, PullKind aPullKind);

  // Stores the abspos frames that have been placed in this containing block.
  nsFrameList mAbsoluteFrames;

  // A temporary frame list used during reflow, storing abspos frames that need
  // to be reflowed by the delegating frame's next-in-flow after transferring
  // them to its own AbsoluteContainingBlock.
  nsFrameList mPushedAbsoluteFrames;

  // Suppose D is the distance from an absolute containing block fragment's
  // border-box block-start edge to whichever is larger of either (a) its
  // border-box block-end edge, or (b) the available space's block-end
  // edge.
  //
  // TODO (TYLin, Bug 2009647): We currently assume (a) cannot be bigger than
  // (b), but it can if there is an unfragmentable in-flow element.
  //
  // This variable stores the sum of the D values for the current absolute
  // containing block fragment and for all its previous fragments. It represents
  // the offset from the start of the theoretical unfragmented abspos containing
  // block to the start of the current fragment. During reflow, we subtract this
  // value from abspos frames' unfragmented positions to get their local
  // coordinate space position in the current fragment.
  nscoord mCumulativeContainingBlockBSize = 0;

#ifdef DEBUG
  void SanityCheckChildLists(const nsIFrame* aDelegatingFrame) const;
#endif
};

}  // namespace mozilla

#endif /* LAYOUT_GENERIC_ABSOLUTE_CONTAINING_BLOCK_H_ */
