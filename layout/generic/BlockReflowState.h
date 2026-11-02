/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* state used in reflow of block frames */

#ifndef BlockReflowState_h
#define BlockReflowState_h

#include <tuple>

#include "mozilla/ReflowInput.h"
#include "nsFloatManager.h"
#include "nsLineBox.h"

class nsBlockFrame;
class nsFrameList;
class nsOverflowContinuationTracker;

namespace mozilla {

// BlockReflowState contains additional reflow input information that the
// block frame uses along with ReflowInput. Like ReflowInput, this
// is read-only data that is passed down from a parent frame to its children.
class BlockReflowState {
  using BandInfoType = nsFloatManager::BandInfoType;
  using ShapeType = nsFloatManager::ShapeType;

  // Block reflow input flags.
  struct Flags {
    Flags()
        : mIsBStartMarginRoot(false),
          mIsBEndMarginRoot(false),
          mShouldApplyBStartMargin(false),
          mHasLineAdjacentToTop(false),
          mBlockNeedsFloatManager(false),
          mIsLineLayoutEmpty(false),
          mCanHaveOverflowMarkers(false),
          mShouldApplyTextBoxTrimStart(false),
          mShouldApplyTextBoxTrimAtBlockEnd(false),
          mShouldApplyTextBoxTrimAtFragmentEnd(false),
          mShouldApplyLineClampAuto(false) {}

    // Set in the BlockReflowState constructor when reflowing a "block margin
    // root" frame (i.e. a frame with any of the NS_BLOCK_BFC flag set, for
    // which margins apply by default).
    //
    // The flag is also set when reflowing a frame whose computed BStart border
    // padding is non-zero.
    bool mIsBStartMarginRoot : 1;

    // Set in the BlockReflowState constructor when reflowing a "block margin
    // root" frame (i.e. a frame with any of the NS_BLOCK_BFC flag set, for
    // which margins apply by default).
    //
    // The flag is also set when reflowing a frame whose computed BEnd border
    // padding is non-zero.
    bool mIsBEndMarginRoot : 1;

    // Set if the BStart margin should be considered when placing a linebox that
    // contains a block frame. It may be set as a side-effect of calling
    // nsBlockFrame::ShouldApplyBStartMargin(); once set,
    // ShouldApplyBStartMargin() uses it as a fast-path way to return whether
    // the BStart margin should apply.
    //
    // If the flag hasn't been set in the block reflow state, then
    // ShouldApplyBStartMargin() will crawl the line list to see if a block
    // frame precedes the specified frame. If so, the BStart margin should be
    // applied, and the flag is set to cache the result. (If not, the BStart
    // margin will be applied as a result of the generational margin collapsing
    // logic in nsBlockReflowContext::ComputeCollapsedBStartMargin(). In this
    // case, the flag won't be set, so subsequent calls to
    // ShouldApplyBStartMargin() will continue crawl the line list.)
    //
    // This flag is also set in the BlockReflowState constructor if
    // mIsBStartMarginRoot is set; that is, the frame being reflowed is a margin
    // root by default.
    bool mShouldApplyBStartMargin : 1;

    // Set when mLineAdjacentToTop is valid.
    bool mHasLineAdjacentToTop : 1;

    // Set when the block has the equivalent of NS_BLOCK_*_BFC.
    bool mBlockNeedsFloatManager : 1;

    // Set when nsLineLayout::LineIsEmpty was true at the end of reflowing
    // the current line.
    bool mIsLineLayoutEmpty : 1;

    // Set when we need text-overflow or -webkit-line-clamp processing.
    bool mCanHaveOverflowMarkers : 1;

    // Set when the block or one of its ancestors requests text-box-trim
    // on the trim-start/trim-end side and should be applied to the
    // first/last formatted line of this block/fragment. See the corresponding
    // ReflowInput::Flags for additional details about the two trim-end flags.
    bool mShouldApplyTextBoxTrimStart : 1;
    bool mShouldApplyTextBoxTrimAtBlockEnd : 1;
    bool mShouldApplyTextBoxTrimAtFragmentEnd : 1;

    // Automatic line-clamp relies on certain dimensions, such as margins and
    // BStart being available, so it should only be applied after the first
    // reflow.
    bool mShouldApplyLineClampAuto : 1;
  };

 public:
  BlockReflowState(const ReflowInput& aReflowInput, nsPresContext* aPresContext,
                   nsBlockFrame* aFrame, bool aBStartMarginRoot,
                   bool aBEndMarginRoot, bool aBlockNeedsFloatManager,
                   const nscoord aConsumedBSize,
                   const nscoord aEffectiveContentBoxBSize,
                   const nscoord aInset = 0);

  /**
   * Unshifts coords, restores availableBSize to reality.
   * (Constructor applies any cached shift before reflow
   *  so that frames are reflowed with cached shift)
   */
  void UndoAlignContentShift();

  /**
   * Get the available reflow space (the area not occupied by floats)
   * for the current y coordinate. The available space is relative to
   * our coordinate system, which is the content box, with (0, 0) in the
   * upper left.
   *
   * The parameter aCBWM is the containing block's writing mode, which is
   * NOT necessarily the mode currently being used by the float manager.
   *
   * Returns whether there are floats present at the given block-direction
   * coordinate and within the inline size of the content rect.
   *
   * Note: some codepaths clamp this structure's inline-size to be >=0 "for
   * compatibility with nsSpaceManager". So if you encounter a nsFlowAreaRect
   * which appears to have an ISize of 0, you can't necessarily assume that a
   * 0-ISize float-avoiding block would actually fit; you need to check the
   * InitialISizeIsNegative flag to see whether that 0 is actually a clamped
   * negative value (in which case a 0-ISize float-avoiding block *should not*
   * be considered as fitting, because it would intersect some float).
   */
  nsFlowAreaRect GetFloatAvailableSpace(WritingMode aCBWM) const {
    return GetFloatAvailableSpace(aCBWM, mBCoord);
  }
  nsFlowAreaRect GetFloatAvailableSpaceForPlacingFloat(WritingMode aCBWM,
                                                       nscoord aBCoord) const {
    return GetFloatAvailableSpaceWithState(aCBWM, aBCoord, ShapeType::Margin,
                                           nullptr);
  }
  nsFlowAreaRect GetFloatAvailableSpace(WritingMode aCBWM,
                                        nscoord aBCoord) const {
    return GetFloatAvailableSpaceWithState(aCBWM, aBCoord,
                                           ShapeType::ShapeOutside, nullptr);
  }
  nsFlowAreaRect GetFloatAvailableSpaceWithState(
      WritingMode aCBWM, nscoord aBCoord, ShapeType aShapeType,
      nsFloatManager::SavedState* aState) const;
  nsFlowAreaRect GetFloatAvailableSpaceForBSize(
      WritingMode aCBWM, nscoord aBCoord, nscoord aBSize,
      nsFloatManager::SavedState* aState) const;

  // @return true if AddFloat was able to place the float; false if the float
  // did not fit in available space.
  //
  // Note: if it returns false, then the float's position and size should be
  // considered stale/invalid (until the float is successfully placed).
  bool AddFloat(nsLineLayout* aLineLayout, nsIFrame* aFloat,
                nscoord aAvailableISize);

  enum class PlaceFloatResult : uint8_t {
    Placed,
    ShouldPlaceBelowCurrentLine,
    ShouldPlaceInNextContinuation,
  };
  // @param aAvailableISizeInCurrentLine the available inline-size of the
  //        current line if current line is not empty.
  PlaceFloatResult FlowAndPlaceFloat(
      nsIFrame* aFloat, mozilla::Maybe<nscoord> aAvailableISizeInCurrentLine =
                            mozilla::Nothing());

  void PlaceBelowCurrentLineFloats(nsLineBox* aLine);

  // Returns the first coordinate >= aBCoord that clears the
  // floats indicated by aClearType and has enough inline size between floats
  // (or no floats remaining) to accomodate aFloatAvoidingBlock.
  enum class ClearFloatsResult : uint8_t {
    BCoordNoChange,
    BCoordAdvanced,
    FloatsPushedOrSplit,
  };
  std::tuple<nscoord, ClearFloatsResult> ClearFloats(
      nscoord aBCoord, UsedClear aClearType,
      nsIFrame* aFloatAvoidingBlock = nullptr);

  nsFloatManager* FloatManager() const {
    MOZ_ASSERT(mReflowInput.mFloatManager,
               "Float manager should be valid during the lifetime of "
               "BlockReflowState!");
    return mReflowInput.mFloatManager;
  }

  // Advances to the next band, i.e., the next horizontal stripe in
  // which there is a different set of floats.
  // Return false if it did not advance, which only happens for
  // constrained heights (and means that we should get pushed to the
  // next column/page).
  bool AdvanceToNextBand(const LogicalRect& aFloatAvailableSpace,
                         nscoord* aBCoord) const {
    WritingMode wm = mReflowInput.GetWritingMode();
    if (aFloatAvailableSpace.BSize(wm) > 0) {
      // See if there's room in the next band.
      *aBCoord += aFloatAvailableSpace.BSize(wm);
    } else {
      if (mReflowInput.AvailableHeight() != NS_UNCONSTRAINEDSIZE) {
        // Stop trying to clear here; we'll just get pushed to the
        // next column or page and try again there.
        return false;
      }
      MOZ_ASSERT_UNREACHABLE("avail space rect with zero height!");
      *aBCoord += 1;
    }
    return true;
  }

  bool FloatAvoidingBlockFitsInAvailSpace(
      nsIFrame* aFloatAvoidingBlock,
      const nsFlowAreaRect& aFloatAvailableSpace) const;

  // True if the current block-direction coordinate, for placing the children
  // within the content area, is still adjacent with the block-start of the
  // content area.
  bool IsAdjacentWithBStart() const { return mBCoord == ContentBStart(); }

  const LogicalMargin& BorderPadding() const { return mBorderPadding; }

  // Reconstruct the previous block-end margin that goes before |aLine|.
  void ReconstructMarginBefore(nsLineList::iterator aLine);

  // Caller must have called GetFloatAvailableSpace for the correct position
  // (which need not be the current mBCoord).
  void ComputeFloatAvoidingOffsets(nsIFrame* aFloatAvoidingBlock,
                                   const LogicalRect& aFloatAvailableSpace,
                                   nscoord& aIStartResult,
                                   nscoord& aIEndResult) const;

  // Compute the amount of available space for reflowing a block frame at the
  // current block-direction coordinate mBCoord. Caller must have called
  // GetFloatAvailableSpace for the current mBCoord.
  LogicalRect ComputeBlockAvailSpace(nsIFrame* aFrame,
                                     const nsFlowAreaRect& aFloatAvailableSpace,
                                     bool aBlockAvoidsFloats);

  LogicalSize ComputeAvailableSizeForFloat() const;

  void RecoverStateFrom(nsLineList::iterator aLine, nscoord aDeltaBCoord);

  void AdvanceToNextLine() {
    if (mFlags.mIsLineLayoutEmpty) {
      mFlags.mIsLineLayoutEmpty = false;
    } else {
      mLineNumber++;
    }
  }

  //----------------------------------------

  // This state is the "global" state computed once for the reflow of
  // the block.

  // The block frame that is using this object
  nsBlockFrame* const mBlock;

  nsPresContext* const mPresContext;

  const ReflowInput& mReflowInput;

  // The coordinates within the float manager where the block is being
  // placed <b>after</b> taking into account the blocks border and
  // padding. This, therefore, represents the inner "content area" (in
  // float manager coordinates) where child frames will be placed,
  // including child blocks and floats.
  nscoord mFloatManagerI, mFloatManagerB;

  // XXX get rid of this
  nsReflowStatus mReflowStatus;

  // The float manager state as it was before the contents of this
  // block.  This is needed for positioning bullets, since we only want
  // to move the bullet to flow around floats that were before this
  // block, not floats inside of it.
  nsFloatManager::SavedState mFloatManagerStateBefore;

  // The content area to reflow child frames within.  This is within
  // this frame's coordinate system and writing mode, which means
  // mContentArea.IStart == BorderPadding().IStart and
  // mContentArea.BStart == BorderPadding().BStart.
  // The block size may be NS_UNCONSTRAINEDSIZE, which indicates that there
  // is no page/column boundary below (the common case).
  // mContentArea.BEnd() should only be called after checking that
  // mContentArea.BSize is not NS_UNCONSTRAINEDSIZE; otherwise
  // coordinate overflow may occur.
  LogicalRect mContentArea;
  nscoord ContentIStart() const {
    return mContentArea.IStart(mReflowInput.GetWritingMode());
  }
  nscoord ContentISize() const {
    return mContentArea.ISize(mReflowInput.GetWritingMode());
  }
  nscoord ContentIEnd() const {
    return mContentArea.IEnd(mReflowInput.GetWritingMode());
  }
  nscoord ContentBStart() const {
    return mContentArea.BStart(mReflowInput.GetWritingMode());
  }
  nscoord ContentBSize() const {
    return mContentArea.BSize(mReflowInput.GetWritingMode());
  }
  nscoord ContentBEnd() const {
    NS_ASSERTION(
        ContentBSize() != NS_UNCONSTRAINEDSIZE,
        "ContentBSize() is unconstrained, so ContentBEnd() may overflow.");
    return mContentArea.BEnd(mReflowInput.GetWritingMode());
  }
  LogicalSize ContentSize(WritingMode aWM) const {
    WritingMode wm = mReflowInput.GetWritingMode();
    return mContentArea.Size(wm).ConvertTo(aWM, wm);
  }

  // Amount of inset to apply during line-breaking, used by text-wrap:balance
  // to adjust line-breaks for more consistent lengths throughout the block.
  nscoord mInsetForBalance;

  // Physical size. Use only for physical <-> logical coordinate conversion.
  //
  // Note: for vertical-rl writing-mode, if mContainerSize's width is
  // initialized to zero due to unconstrained block-size, lines will be
  // positioned (physically) incorrectly. We will fix them up at the end of
  // nsBlockFrame::Reflow() after we know the total block-size of the frame.
  nsSize mContainerSize;
  const nsSize& ContainerSize() const { return mContainerSize; }

  /**
   * Append aFloatCont and its next-in-flows within the same block to mBlock's
   * pushed floats list. aFloatCont should not be on any child list when making
   * this call. Its next-in-flows will be removed from mBlock using StealFrame()
   * before being added to mBlock's pushed floats list. All appended frames will
   * be marked NS_FRAME_IS_PUSHED_OUT_OF_FLOW.
   */
  void AppendPushedFloatChain(nsIFrame* aFloatCont);

  // Track child overflow continuations.
  nsOverflowContinuationTracker* mOverflowTracker;

  //----------------------------------------

  // This state is "running" state updated by the reflow of each line
  // in the block. This same state is "recovered" when a line is not
  // dirty and is passed over during incremental reflow.

  // The current line being reflowed
  // If it is mBlock->end_lines(), then it is invalid.
  nsLineList::iterator mCurrentLine;

  // When mHasLineAdjacentToTop is set, this refers to a line
  // which we know is adjacent to the top of the block (in other words,
  // all lines before it are empty and do not have clearance. This line is
  // always before the current line.
  nsLineList::iterator mLineAdjacentToTop;

  // The current block-direction coordinate in the block
  nscoord mBCoord;

  // mBlock's computed logical border+padding with pre-reflow skip sides applied
  // (See the constructor and nsIFrame::PreReflowBlockLevelLogicalSkipSides).
  const LogicalMargin mBorderPadding;

  // The overflow areas of all floats placed so far
  OverflowAreas mFloatOverflowAreas;

  // Previous child. This is used when pulling up a frame to update
  // the sibling list.
  nsIFrame* mPrevChild;

  // The previous child frames collapsed bottom margin value.
  CollapsingMargin mPrevBEndMargin;

  // The current next-in-flow for the block. When lines are pulled
  // from a next-in-flow, this is used to know which next-in-flow to
  // pull from. When a next-in-flow is emptied of lines, we advance
  // this to the next next-in-flow.
  nsBlockFrame* mNextInFlow;

  //----------------------------------------

  // Temporary state, for line-reflow. This state is used during the reflow
  // of a given line, but doesn't have meaning before or after.

  // The list of floats that are "current-line" floats. These are
  // added to the line after the line has been reflowed, to keep the
  // list fiddling from being N^2.
  nsTArray<nsIFrame*> mCurrentLineFloats;

  // The list of floats which are "below current-line"
  // floats. These are reflowed/placed after the line is reflowed
  // and placed. Again, this is done to keep the list fiddling from
  // being N^2.
  nsTArray<nsIFrame*> mBelowCurrentLineFloats;

  // The list of floats that are waiting on a break opportunity in order to be
  // placed, since we're on a nowrap context.
  nsTArray<nsIFrame*> mNoWrapFloats;

  const nscoord mMinLineHeight;

  int32_t mLineNumber;

  // Signals that this block should be reflowed again in order to apply
  // text-box-trim on the trim-end side of a specific line. Used by
  // fragmented boxes when a line overflows into another fragment, and
  // text-box-trim may affect which lines fit in the current fragment.
  bool mNeedsTextBoxTrimAtFragmentEndRetry = false;

  Flags mFlags;

  // Cache the result of nsBlockFrame::FindTrailingClear() from mBlock's
  // prev-in-flows. See nsBlockFrame::ReflowPushedFloats().
  UsedClear mTrailingClearFromPIF;

  // The amount of computed content block-size "consumed" by our previous
  // continuations.
  const nscoord mConsumedBSize;

  // The amount of block-axis alignment shift to assume during reflow.
  // Cached between reflows in the AlignContentShift property.
  // (This system optimizes reflow for not changing the shift.)
  nscoord mAlignContentShift;

  // Cache the current line's BSize if nsBlockFrame::PlaceLine() fails to
  // place the line. When redoing the line, it will be used to query the
  // accurate float available space in AddFloat() and
  // nsBlockFrame::PlaceLine().
  Maybe<nscoord> mLineBSize;

 private:
  bool CanPlaceFloat(nscoord aFloatISize,
                     const nsFlowAreaRect& aFloatAvailableSpace);

  void PushFloatPastBreak(nsIFrame* aFloat);

  void RecoverFloats(nsLineList::iterator aLine, nscoord aDeltaBCoord);
};

};  // namespace mozilla

#endif  // BlockReflowState_h
