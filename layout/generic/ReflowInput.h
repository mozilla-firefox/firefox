/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* struct containing the input to nsIFrame::Reflow */

#ifndef mozilla_ReflowInput_h
#define mozilla_ReflowInput_h

#include "nsMargin.h"
#include "nsStyleConsts.h"
#include "mozilla/Assertions.h"
#include "mozilla/EnumSet.h"
#include "mozilla/LayoutStructs.h"
#include "mozilla/Maybe.h"
#include "mozilla/WritingModes.h"
#include "LayoutConstants.h"
#include "ReflowOutput.h"
#include <algorithm>

class gfxContext;
class nsFloatManager;
struct nsHypotheticalPosition;
class nsIPercentBSizeObserver;
class nsLineLayout;
class nsPlaceholderFrame;
class nsPresContext;
class nsReflowStatus;

namespace mozilla {
enum class LayoutFrameType : uint8_t;

/**
 * @return aValue clamped to [aMinValue, aMaxValue].
 *
 * @note This function needs to handle aMinValue > aMaxValue. In that case,
 *       aMinValue is returned. That's why we cannot use std::clamp()
 *       since it asserts max >= min.
 * @note If aMinValue and aMaxValue are computed min block-size and max
 *       block-size, it is simpler to use ReflowInput::ApplyMinMaxBSize().
 *       Similarly, there is ReflowInput::ApplyMinMaxISize() for clamping an
 *       inline-size.
 * @see https://www.w3.org/TR/CSS22/visudet.html#min-max-widths
 * @see https://www.w3.org/TR/CSS22/visudet.html#min-max-heights
 */
template <class NumericType>
NumericType CSSMinMax(NumericType aValue, NumericType aMinValue,
                      NumericType aMaxValue) {
  NumericType result = aValue;
  if (aMaxValue < result) {
    result = aMaxValue;
  }
  if (aMinValue > result) {
    result = aMinValue;
  }
  return result;
}

// A base class of ReflowInput that computes only the padding,
// border, and margin, since those values are needed more often.
struct SizeComputationInput {
 public:
  // The frame being reflowed.
  nsIFrame* const mFrame;

  // Rendering context to use for measurement.
  gfxContext* mRenderingContext;

  nsMargin ComputedPhysicalMargin() const {
    return mComputedMargin.GetPhysicalMargin(mWritingMode);
  }
  nsMargin ComputedPhysicalBorderPadding() const {
    return mComputedBorderPadding.GetPhysicalMargin(mWritingMode);
  }
  nsMargin ComputedPhysicalBorder() const {
    return ComputedLogicalBorder(mWritingMode).GetPhysicalMargin(mWritingMode);
  }
  nsMargin ComputedPhysicalPadding() const {
    return mComputedPadding.GetPhysicalMargin(mWritingMode);
  }

  LogicalMargin ComputedLogicalMargin(WritingMode aWM) const {
    return mComputedMargin.ConvertTo(aWM, mWritingMode);
  }
  LogicalMargin ComputedLogicalBorderPadding(WritingMode aWM) const {
    return mComputedBorderPadding.ConvertTo(aWM, mWritingMode);
  }
  LogicalMargin ComputedLogicalPadding(WritingMode aWM) const {
    return mComputedPadding.ConvertTo(aWM, mWritingMode);
  }
  LogicalMargin ComputedLogicalBorder(WritingMode aWM) const {
    return (mComputedBorderPadding - mComputedPadding)
        .ConvertTo(aWM, mWritingMode);
  }

  void SetComputedLogicalMargin(WritingMode aWM, const LogicalMargin& aMargin) {
    mComputedMargin = aMargin.ConvertTo(mWritingMode, aWM);
  }
  void SetComputedLogicalBorderPadding(WritingMode aWM,
                                       const LogicalMargin& aBorderPadding) {
    mComputedBorderPadding = aBorderPadding.ConvertTo(mWritingMode, aWM);
  }
  void SetComputedLogicalPadding(WritingMode aWM,
                                 const LogicalMargin& aPadding) {
    mComputedPadding = aPadding.ConvertTo(mWritingMode, aWM);
  }

  WritingMode GetWritingMode() const { return mWritingMode; }

 protected:
  // cached copy of the frame's writing-mode, for logical coordinates
  const WritingMode mWritingMode;

  // Cached mFrame->IsThemed().
  const bool mIsThemed = false;

  // Computed margin values
  LogicalMargin mComputedMargin;

  // Cached copy of the border + padding values
  LogicalMargin mComputedBorderPadding;

  // Computed padding values
  LogicalMargin mComputedPadding;

 public:
  // Callers using this constructor must call InitOffsets on their own.
  SizeComputationInput(nsIFrame* aFrame, gfxContext* aRenderingContext);

  SizeComputationInput(nsIFrame* aFrame, gfxContext* aRenderingContext,
                       WritingMode aContainingBlockWritingMode,
                       nscoord aContainingBlockISize,
                       const Maybe<LogicalMargin>& aBorder = Nothing(),
                       const Maybe<LogicalMargin>& aPadding = Nothing());

 private:
  /**
   * Computes margin values from the specified margin style information, and
   * fills in the mComputedMargin member.
   *
   * @param aCBWM Writing mode of the containing block
   * @param aPercentBasis
   *    Inline size of the containing block (in its own writing mode), to use
   *    for resolving percentage margin values in the inline and block axes.
   * @return true if the margin is dependent on the containing block size.
   */
  bool ComputeMargin(WritingMode aCBWM, nscoord aPercentBasis,
                     LayoutFrameType aFrameType);

  /**
   * Computes padding values from the specified padding style information, and
   * fills in the mComputedPadding member.
   *
   * @param aCBWM Writing mode of the containing block
   * @param aPercentBasis
   *    Inline size of the containing block (in its own writing mode), to use
   *    for resolving percentage padding values in the inline and block axes.
   * @return true if the padding is dependent on the containing block size.
   */
  bool ComputePadding(WritingMode aCBWM, nscoord aPercentBasis,
                      LayoutFrameType aFrameType);

 protected:
  void InitOffsets(WritingMode aCBWM, nscoord aPercentBasis,
                   LayoutFrameType aFrameType, ComputeSizeFlags aFlags,
                   const Maybe<LogicalMargin>& aBorder,
                   const Maybe<LogicalMargin>& aPadding,
                   const nsStyleDisplay* aDisplay = nullptr);

  /**
   * Convert StyleSize or StyleMaxSize to nscoord when percentages depend on the
   * inline size of the containing block, and enumerated values are for inline
   * size, min-inline-size, or max-inline-size.  Does not handle auto inline
   * sizes.
   */
  template <typename SizeOrMaxSize>
  inline nscoord ComputeISizeValue(const LogicalSize& aContainingBlockSize,
                                   StyleBoxSizing aBoxSizing,
                                   const SizeOrMaxSize&) const;

  /**
   * Wrapper for SizeComputationInput::ComputeBSizeValue (defined below, which
   * itself is a wrapper for nsLayoutUtils::ComputeBSizeValue). This one just
   * handles 'stretch' sizes first.
   */
  template <typename SizeOrMaxSize>
  inline nscoord ComputeBSizeValueHandlingStretch(
      nscoord aContainingBlockBSize, StyleBoxSizing aBoxSizing,
      const SizeOrMaxSize& aSize) const;

  /**
   * Wrapper for nsLayoutUtils::ComputeBSizeValue, which automatically figures
   * out the value to pass for its aContentEdgeToBoxSizingBoxEdge param.
   */
  nscoord ComputeBSizeValue(nscoord aContainingBlockBSize,
                            StyleBoxSizing aBoxSizing,
                            const LengthPercentage& aCoord) const;
};

/**
 * State passed to a frame during reflow.
 *
 * @see nsIFrame#Reflow()
 */
struct ReflowInput : public SizeComputationInput {
  // the reflow inputs are linked together. this is the pointer to the
  // parent's reflow input
  const ReflowInput* mParentReflowInput = nullptr;

  // A non-owning pointer to the float manager associated with this area,
  // which points to the object owned by nsAutoFloatManager::mNew.
  nsFloatManager* mFloatManager = nullptr;

  // LineLayout object (only for inline reflow; set to nullptr otherwise)
  nsLineLayout* mLineLayout = nullptr;

  // The appropriate reflow input for the containing block (for
  // percentage widths, etc.) of this reflow input's frame. It will be setup
  // properly in InitCBReflowInput().
  const ReflowInput* mCBReflowInput = nullptr;

  // The amount the in-flow position of the block is moving vertically relative
  // to its previous in-flow position (i.e. the amount the line containing the
  // block is moving).
  // This should be zero for anything which is not a block outside, and it
  // should be zero for anything which has a non-block parent.
  // The intended use of this value is to allow the accurate determination
  // of the potential impact of a float
  // This takes on an arbitrary value the first time a block is reflowed
  nscoord mBlockDelta = 0;

  // Physical accessors for the private fields. They are needed for
  // compatibility with not-yet-updated code. New code should use the accessors
  // for logical coordinates, unless the code really works on physical
  // coordinates.
  nscoord AvailableWidth() const { return mAvailableSize.Width(mWritingMode); }
  nscoord AvailableHeight() const {
    return mAvailableSize.Height(mWritingMode);
  }
  nscoord ComputedWidth() const { return mComputedSize.Width(mWritingMode); }
  nscoord ComputedHeight() const { return mComputedSize.Height(mWritingMode); }
  nscoord ComputedMinWidth() const {
    return mComputedMinSize.Width(mWritingMode);
  }
  nscoord ComputedMaxWidth() const {
    return mComputedMaxSize.Width(mWritingMode);
  }
  nscoord ComputedMinHeight() const {
    return mComputedMinSize.Height(mWritingMode);
  }
  nscoord ComputedMaxHeight() const {
    return mComputedMaxSize.Height(mWritingMode);
  }

  // Logical accessors for private fields in mWritingMode.
  nscoord AvailableISize() const { return mAvailableSize.ISize(mWritingMode); }
  nscoord AvailableBSize() const { return mAvailableSize.BSize(mWritingMode); }
  nscoord ComputedISize() const { return mComputedSize.ISize(mWritingMode); }
  nscoord ComputedBSize() const { return mComputedSize.BSize(mWritingMode); }
  nscoord ComputedMinISize() const {
    return mComputedMinSize.ISize(mWritingMode);
  }
  nscoord ComputedMaxISize() const {
    return mComputedMaxSize.ISize(mWritingMode);
  }
  nscoord ComputedMinBSize() const {
    return mComputedMinSize.BSize(mWritingMode);
  }
  nscoord ComputedMaxBSize() const {
    return mComputedMaxSize.BSize(mWritingMode);
  }

  // WARNING: In general, adjusting available inline-size or block-size is not
  // safe because ReflowInput has members whose values depend on the available
  // size passing through the constructor. For example,
  // CalculateBlockSideMargins() is called during initialization, and uses
  // AvailableSize(). Make sure your use case doesn't lead to stale member
  // values in ReflowInput!
  void SetAvailableISize(nscoord aAvailableISize) {
    mAvailableSize.ISize(mWritingMode) = aAvailableISize;
  }
  void SetAvailableBSize(nscoord aAvailableBSize) {
    mAvailableSize.BSize(mWritingMode) = aAvailableBSize;
  }

  void SetComputedMinISize(nscoord aMinISize) {
    mComputedMinSize.ISize(mWritingMode) = aMinISize;
  }
  void SetComputedMaxISize(nscoord aMaxISize) {
    mComputedMaxSize.ISize(mWritingMode) = aMaxISize;
  }
  void SetComputedMinBSize(nscoord aMinBSize) {
    mComputedMinSize.BSize(mWritingMode) = aMinBSize;
  }
  void SetComputedMaxBSize(nscoord aMaxBSize) {
    mComputedMaxSize.BSize(mWritingMode) = aMaxBSize;
  }
  void SetPercentageBasisInBlockAxis(nscoord aBSize) {
    mPercentageBasisInBlockAxis = Some(aBSize);
  }

  LogicalSize AvailableSize() const { return mAvailableSize; }
  LogicalSize ComputedSize() const { return mComputedSize; }

  template <typename F>
  LogicalSize ComputedSizeWithBSizeFallback(F&& aFallback) const {
    auto size = mComputedSize;
    if (size.BSize(mWritingMode) == NS_UNCONSTRAINEDSIZE) {
      size.BSize(mWritingMode) = ApplyMinMaxBSize(aFallback());
    }
    return size;
  }

  LogicalSize ComputedMinSize() const { return mComputedMinSize; }
  LogicalSize ComputedMaxSize() const { return mComputedMaxSize; }

  LogicalSize AvailableSize(WritingMode aWM) const {
    return AvailableSize().ConvertTo(aWM, mWritingMode);
  }
  LogicalSize ComputedSize(WritingMode aWM) const {
    return ComputedSize().ConvertTo(aWM, mWritingMode);
  }
  LogicalSize ComputedMinSize(WritingMode aWM) const {
    return ComputedMinSize().ConvertTo(aWM, mWritingMode);
  }
  LogicalSize ComputedMaxSize(WritingMode aWM) const {
    return ComputedMaxSize().ConvertTo(aWM, mWritingMode);
  }

  LogicalSize ComputedSizeWithPadding(WritingMode aWM) const {
    return ComputedSize(aWM) + ComputedLogicalPadding(aWM).Size(aWM);
  }

  LogicalSize ComputedSizeWithBorderPadding(WritingMode aWM) const {
    return ComputedSize(aWM) + ComputedLogicalBorderPadding(aWM).Size(aWM);
  }

  LogicalSize ComputedSizeWithMarginBorderPadding(WritingMode aWM) const {
    return ComputedSizeWithBorderPadding(aWM) +
           ComputedLogicalMargin(aWM).Size(aWM);
  }

  nsSize ComputedPhysicalSize() const {
    return mComputedSize.GetPhysicalSize(mWritingMode);
  }

  nsMargin ComputedPhysicalOffsets() const {
    return mComputedOffsets.GetPhysicalMargin(mWritingMode);
  }

  LogicalMargin ComputedLogicalOffsets(WritingMode aWM) const {
    return mComputedOffsets.ConvertTo(aWM, mWritingMode);
  }

  void SetComputedLogicalOffsets(WritingMode aWM,
                                 const LogicalMargin& aOffsets) {
    mComputedOffsets = aOffsets.ConvertTo(mWritingMode, aWM);
  }

  // Return ReflowInput's computed size including border-padding, with
  // unconstrained dimensions replaced by zero.
  nsSize ComputedSizeAsContainerIfConstrained() const;

  // Get the writing mode of the containing block, to resolve float/clear
  // logical sides appropriately.
  WritingMode GetCBWritingMode() const;

  // Our saved containing block dimensions.
  LogicalSize mContainingBlockSize{mWritingMode};

  // Cached pointers to the various style structs used during initialization.
  const nsStyleDisplay* mStyleDisplay = nullptr;
  const nsStylePosition* mStylePosition = nullptr;
  const nsStyleBorder* mStyleBorder = nullptr;
  const nsStyleMargin* mStyleMargin = nullptr;

  enum class BreakType : uint8_t {
    Auto,
    Column,
    Page,
  };
  BreakType mBreakType = BreakType::Auto;

  // a frame (e.g. nsTableCellFrame) which may need to generate a special
  // reflow for percent bsize calculations
  nsIPercentBSizeObserver* mPercentBSizeObserver = nullptr;

  // CSS margin collapsing sometimes requires us to reflow
  // optimistically assuming that margins collapse to see if clearance
  // is required. When we discover that clearance is required, we
  // store the frame in which clearance was discovered to the location
  // requested here.
  nsIFrame** mDiscoveredClearance = nullptr;

  struct Flags {
    Flags() { memset(this, 0, sizeof(*this)); }

    // Cached mFrame->IsReplaced().
    bool mIsReplaced : 1;

    // used by tables to communicate special reflow (in process) to handle
    // percent bsize frames inside cells which may not have computed bsizes
    bool mSpecialBSizeReflow : 1;

    // nothing in the frame's next-in-flow (or its descendants) is changing
    bool mNextInFlowUntouched : 1;

    // Is the current context at the top of a page?  When true, we force
    // something that's too tall for a page/column to fit anyway to avoid
    // infinite loops.
    bool mIsTopOfPage : 1;

    // parent frame is an ScrollContainerFrame and it is assuming a horizontal
    // scrollbar
    bool mAssumingHScrollbar : 1;

    // parent frame is an ScrollContainerFrame and it is assuming a vertical
    // scrollbar
    bool mAssumingVScrollbar : 1;

    // Is frame a different inline-size than before?
    bool mIsIResize : 1;

    // Is frame (potentially) a different block-size than before?
    // This includes cases where the block-size is 'auto' and the
    // contents or width have changed.
    bool mIsBResize : 1;

    // Has this frame changed block-size in a way that affects
    // block-size percentages on frames for which it is the containing
    // block?  This includes a change between 'auto' and a length that
    // doesn't actually change the frame's block-size.  It does not
    // include cases where the block-size is 'auto' and the frame's
    // contents have changed.
    //
    // In the current code, this is only true when mIsBResize is also
    // true, although it doesn't necessarily need to be that way (e.g.,
    // in the case of a frame changing from 'auto' to a length that
    // produces the same height).
    bool mIsBResizeForPercentages : 1;

    // tables are splittable, this should happen only inside a page and never
    // insider a column frame
    bool mTableIsSplittable : 1;

    // Does frame height depend on an ancestor table-cell?
    bool mHeightDependsOnAncestorCell : 1;

    // Is this the final reflow of an orthogonal table-cell, after row sizing?
    bool mOrthogonalCellFinalReflow : 1;

    // nsColumnSetFrame is balancing columns
    bool mIsColumnBalancing : 1;

    // We have an ancestor nsColumnSetFrame performing the last column balancing
    // reflow. The available block-size of the last column might become
    // unconstrained.
    bool mIsInLastColumnBalancingReflow : 1;

    // True if ColumnSetWrapperFrame has a constrained block-size, and is going
    // to consume all of its block-size in this fragment. This bit is passed to
    // nsColumnSetFrame to determine whether to give up balancing and create
    // overflow columns.
    bool mColumnSetWrapperHasNoBSizeLeft : 1;

    // If this flag is set, the BSize of this frame should be considered
    // indefinite for the purposes of percent resolution on child frames (we
    // should behave as if ComputedBSize() were NS_UNCONSTRAINEDSIZE when doing
    // percent resolution against this.ComputedBSize()).  For example: flex
    // items may have their ComputedBSize() resolved ahead-of-time by their
    // flex container, and yet their BSize might have to be considered
    // indefinite per https://drafts.csswg.org/css-flexbox/#definite-sizes
    bool mTreatBSizeAsIndefinite : 1;

    // a "fake" reflow input made in order to be the parent of a real one
    bool mDummyParentReflowInput : 1;

    // Should this frame reflow its place-holder children? If the available
    // height of this frame didn't change, but its in a paginated environment
    // (e.g. columns), it should always reflow its placeholder children.
    bool mMustReflowPlaceholders : 1;

    // the STATIC_POS_IS_CB_ORIGIN ctor flag
    bool mStaticPosIsCBOrigin : 1;

    // If set, the following two flags indicate that:
    // (1) this frame is absolutely-positioned (or fixed-positioned).
    // (2) this frame's static position depends on the CSS Box Alignment.
    // (3) we do need to compute the static position, because the frame's
    //     {Inline and/or Block} offsets actually depend on it.
    // When these bits are set, the offset values (IStart/IEnd, BStart/BEnd)
    // represent the "start" edge of the frame's CSS Box Alignment container
    // area, in that axis -- and these offsets need to be further-resolved
    // (with CSS Box Alignment) after we know the OOF frame's size.
    // NOTE: The "I" and "B" (for "Inline" and "Block") refer the axes of the
    // *containing block's writing-mode*, NOT mFrame's own writing-mode. This
    // is purely for convenience, since that's the writing-mode we're dealing
    // with when we set & react to these bits.
    bool mIOffsetsNeedCSSAlign : 1;
    bool mBOffsetsNeedCSSAlign : 1;

    // Is this frame or one of its ancestors being reflowed in a different
    // continuation than the one in which it was previously reflowed?  In
    // other words, has it moved to a different column or page than it was in
    // the previous reflow?
    //
    // FIXME: For now, we only ensure that this is set correctly for blocks.
    // This is okay because the only thing that uses it only cares about
    // whether there's been a fragment change within the same block formatting
    // context.
    bool mMovedBlockFragments : 1;

    // Is the block-size computed by aspect-ratio and inline size (i.e. block
    // axis is the ratio-dependent axis)? We set this flag so that we can check
    // whether to apply automatic content-based minimum sizes once we know the
    // children's block-size (after reflowing them).
    // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
    bool mIsBSizeSetByAspectRatio : 1;

    // If true, then children of this frame can generate class A breakpoints
    // for paginated reflow.
    bool mCanHaveClassABreakpoints : 1;
  };
  Flags mFlags;

  StyleSizeOverrides mStyleSizeOverrides;

  ComputeSizeFlags mComputeSizeFlags;

  // This value keeps track of how deeply nested a given reflow input
  // is from the top of the frame tree.
  int16_t mReflowDepth = 0;

  // Accessors for the resize flags.
  bool IsIResize() const { return mFlags.mIsIResize; }
  bool IsBResize() const { return mFlags.mIsBResize; }
  bool IsBResizeForWM(WritingMode aWM) const {
    return aWM.IsOrthogonalTo(mWritingMode) ? mFlags.mIsIResize
                                            : mFlags.mIsBResize;
  }
  bool IsBResizeForPercentagesForWM(WritingMode aWM) const {
    // This uses the relatively-accurate mIsBResizeForPercentages flag
    // when the writing modes are parallel, and is a bit more
    // pessimistic when orthogonal.
    return !aWM.IsOrthogonalTo(mWritingMode) ? mFlags.mIsBResizeForPercentages
                                             : IsIResize();
  }
  void SetIResize(bool aValue) { mFlags.mIsIResize = aValue; }
  void SetBResize(bool aValue) { mFlags.mIsBResize = aValue; }
  void SetBResizeForPercentages(bool aValue) {
    mFlags.mIsBResizeForPercentages = aValue;
  }

  // Values for |aFlags| passed to constructor
  enum class InitFlag : uint8_t {
    // Indicates that the parent of this reflow input is "fake" (see
    // mDummyParentReflowInput in mFlags).
    DummyParentReflowInput,

    // Indicates that the calling function will initialize the reflow input, and
    // that the constructor should not call Init().
    CallerWillInit,

    // The caller wants the abs.pos. static-position resolved at the origin of
    // the containing block, i.e. at LogicalPoint(0, 0). (Note that this
    // doesn't necessarily mean that (0, 0) is the *correct* static position
    // for the frame in question.)
    // @note In a Grid container's masonry axis we'll always use
    // the placeholder's position in that axis regardless of this flag.
    StaticPosIsCBOrigin,
  };
  using InitFlags = EnumSet<InitFlag>;

  // Note: The copy constructor is written by the compiler automatically. You
  // can use that and then override specific values if you want, or you can
  // call Init as desired...

  /**
   * Initialize a ROOT reflow input.
   *
   * @param aPresContext Must be equal to aFrame->PresContext().
   * @param aFrame The frame for whose reflow input is being constructed.
   * @param aRenderingContext The rendering context to be used for measurements.
   * @param aAvailableSpace The available space to reflow aFrame (in aFrame's
   *        writing-mode). See comments for mAvailableSize for more information.
   * @param aFlags A set of flags used for additional boolean parameters (see
   *        InitFlags above).
   */
  ReflowInput(nsPresContext* aPresContext, nsIFrame* aFrame,
              gfxContext* aRenderingContext, const LogicalSize& aAvailableSpace,
              InitFlags aFlags = {});

  /**
   * Initialize a reflow input for a child frame's reflow. Some parts of the
   * state are copied from the parent's reflow input. The remainder is computed.
   *
   * @param aPresContext Must be equal to aFrame->PresContext().
   * @param aParentReflowInput A reference to an ReflowInput object that
   *        is to be the parent of this object.
   * @param aFrame The frame for whose reflow input is being constructed.
   * @param aAvailableSpace The available space to reflow aFrame (in aFrame's
   *        writing-mode). See comments for mAvailableSize for more information.
   * @param aContainingBlockSize An optional size (in aFrame's writing mode),
   *        specifying the containing block size to use instead of the default
   *        size computed by ComputeContainingBlockRectangle(). If
   *        InitFlag::CallerWillInit is used, this is ignored. Pass it via
   *        Init() instead.
   * @param aFlags A set of flags used for additional boolean parameters (see
   *        InitFlags above).
   * @param aStyleSizeOverrides The style data used to override mFrame's when we
   *        call nsIFrame::ComputeSize() internally.
   * @param aComputeSizeFlags A set of flags used when we call
   *        nsIFrame::ComputeSize() internally.
   */
  ReflowInput(nsPresContext* aPresContext,
              const ReflowInput& aParentReflowInput, nsIFrame* aFrame,
              const LogicalSize& aAvailableSpace,
              const Maybe<LogicalSize>& aContainingBlockSize = Nothing(),
              InitFlags aFlags = {},
              const StyleSizeOverrides& aSizeOverrides = {},
              ComputeSizeFlags aComputeSizeFlags = {});

  /**
   * This method initializes various data members. It is automatically called by
   * the constructors if InitFlags::CallerWillInit is *not* used.
   *
   * @param aContainingBlockSize An optional size (in mFrame's writing mode),
   *        specifying the containing block size to use instead of the default
   *        size computed by ComputeContainingBlockRectangle().
   * @param aBorder An optional border (in mFrame's writing mode). If given, use
   *        it instead of the border computed from mFrame's StyleBorder.
   * @param aPadding An optional padding (in mFrame's writing mode). If given,
   *        use it instead of the padding computing from mFrame's StylePadding.
   */
  void Init(nsPresContext* aPresContext,
            const Maybe<LogicalSize>& aContainingBlockSize = Nothing(),
            const Maybe<LogicalMargin>& aBorder = Nothing(),
            const Maybe<LogicalMargin>& aPadding = Nothing());

  /**
   * Get the used line-height property. The return value will be >= 0.
   */
  nscoord GetLineHeight() const;

  /**
   * Set the used line-height. aLineHeight must be >= 0.
   */
  void SetLineHeight(nscoord aLineHeight);

  /**
   * Calculate the used line-height property without a reflow input instance.
   * The return value will be >= 0.
   *
   * @param aBlockBSize The computed block size of the content rect of the block
   *                    that the line should fill. Only used with
   *                    line-height:-moz-block-height. NS_UNCONSTRAINEDSIZE
   *                    results in a normal line-height for
   *                    line-height:-moz-block-height.
   * @param aFontSizeInflation The result of the appropriate
   *                           nsLayoutUtils::FontSizeInflationFor call,
   *                           or 1.0 if during intrinsic size
   *                           calculation.
   */
  static nscoord CalcLineHeight(const ComputedStyle&,
                                nsPresContext* aPresContext,
                                const nsIContent* aContent, nscoord aBlockBSize,
                                float aFontSizeInflation);

  static nscoord CalcLineHeight(const StyleLineHeight&,
                                const nsStyleFont& aRelativeToFont,
                                nsPresContext* aPresContext, bool aIsVertical,
                                const nsIContent* aContent, nscoord aBlockBSize,
                                float aFontSizeInflation);

  static nscoord CalcLineHeightForCanvas(const StyleLineHeight& aLh,
                                         const nsFont& aRelativeToFont,
                                         nsAtom* aLanguage,
                                         bool aExplicitLanguage,
                                         nsPresContext* aPresContext,
                                         WritingMode aWM);

  static constexpr float kNormalLineHeightFactor = 1.2f;

  LogicalSize ComputeContainingBlockRectangle(
      nsPresContext* aPresContext, const ReflowInput* aContainingBlockRI) const;

  /**
   * Apply the mComputed(Min/Max)ISize constraints to the content
   * size computed so far.
   */
  nscoord ApplyMinMaxISize(nscoord aISize) const {
    if (NS_UNCONSTRAINEDSIZE != ComputedMaxISize()) {
      aISize = std::min(aISize, ComputedMaxISize());
    }
    return std::max(aISize, ComputedMinISize());
  }

  /**
   * Apply the mComputed(Min/Max)BSize constraints to the content
   * size computed so far.
   *
   * @param aBSize The block-size that we've computed an to which we want to
   *        apply min/max constraints.
   * @param aConsumed The amount of the computed block-size that was consumed by
   *        our prev-in-flows.
   */
  nscoord ApplyMinMaxBSize(nscoord aBSize, nscoord aConsumed = 0) const {
    aBSize += aConsumed;

    if (NS_UNCONSTRAINEDSIZE != ComputedMaxBSize()) {
      aBSize = std::min(aBSize, ComputedMaxBSize());
    }

    if (NS_UNCONSTRAINEDSIZE != ComputedMinBSize()) {
      aBSize = std::max(aBSize, ComputedMinBSize());
    }

    return aBSize - aConsumed;
  }

  bool ShouldReflowAllKids() const;

  // This method doesn't apply min/max computed widths to the value passed in.
  void SetComputedWidth(nscoord aComputedWidth) {
    if (mWritingMode.IsVertical()) {
      SetComputedBSize(aComputedWidth);
    } else {
      SetComputedISize(aComputedWidth);
    }
  }

  // This method doesn't apply min/max computed heights to the value passed in.
  void SetComputedHeight(nscoord aComputedHeight) {
    if (mWritingMode.IsVertical()) {
      SetComputedISize(aComputedHeight);
    } else {
      SetComputedBSize(aComputedHeight);
    }
  }

  // Use "No" to request SetComputedISize/SetComputedBSize not to reset resize
  // flags.
  enum class ResetResizeFlags : bool { No, Yes };

  // This method doesn't apply min/max computed inline-sizes to the value passed
  // in.
  void SetComputedISize(nscoord aComputedISize,
                        ResetResizeFlags aFlags = ResetResizeFlags::Yes);

  // These methods don't apply min/max computed block-sizes to the value passed
  // in.
  void SetComputedBSize(nscoord aComputedBSize,
                        ResetResizeFlags aFlags = ResetResizeFlags::Yes);

  bool WillReflowAgainForClearance() const {
    return mDiscoveredClearance && *mDiscoveredClearance;
  }

  // Returns true if we should apply automatic minimum on the block axis.
  //
  // The automatic minimum size in the ratio-dependent axis of a box with a
  // preferred aspect ratio that is neither a replaced element nor a scroll
  // container is its min-content size clamped from above by its maximum size.
  //
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
  bool ShouldApplyAutomaticMinimumOnBlockAxis() const;

  // Returns true if mFrame has a constrained available block-size, or if mFrame
  // is a continuation. When this method returns true, mFrame can be considered
  // to be in a "fragmented context."
  //
  // Note: this method usually returns true when mFrame is in a paged
  // environment (e.g. printing) or has a multi-column container ancestor.
  // However, this doesn't include several cases when we're intentionally
  // performing layout in a fragmentation-ignoring way, e.g. 1) mFrame is a flex
  // or grid item, and this ReflowInput is for a measuring reflow with an
  // unconstrained available block-size, or 2) mFrame is (or is inside of) an
  // element that forms an orthogonal writing-mode.
  bool IsInFragmentedContext() const;

  // Compute the offsets for a relative position element
  //
  // @param aWM the writing mode of aCBSize and the returned offsets.
  static LogicalMargin ComputeRelativeOffsets(WritingMode aWM, nsIFrame* aFrame,
                                              const LogicalSize& aCBSize);

  // If aFrame is a relatively or sticky positioned element, adjust aPosition
  // appropriately.
  //
  // @param aComputedOffsets aFrame's relative offset, either from the cached
  //        nsIFrame::ComputedOffsetProperty() or ComputedPhysicalOffsets().
  //        Note: This parameter is used only when aFrame is relatively
  //        positioned, not sticky positioned.
  // @param aPosition [in/out] Pass aFrame's normal position (pre-relative
  //        positioning), and this method will update it to indicate aFrame's
  //        actual position.
  static void ApplyRelativePositioning(nsIFrame* aFrame,
                                       const nsMargin& aComputedOffsets,
                                       nsPoint* aPosition);

  static void ApplyRelativePositioning(nsIFrame* aFrame,
                                       WritingMode aWritingMode,
                                       const LogicalMargin& aComputedOffsets,
                                       LogicalPoint* aPosition,
                                       const nsSize& aContainerSize);

  // Resolve any block-axis 'auto' margins (if any) for an absolutely positioned
  // frame. aMargin and aOffsets are both outparams (though we only touch
  // aOffsets if the position is overconstrained)
  static void ComputeAbsPosBlockAutoMargin(nscoord aAvailMarginSpace,
                                           WritingMode aContainingBlockWM,
                                           bool aIsMarginBStartAuto,
                                           bool aIsMarginBEndAuto,
                                           LogicalMargin& aMargin,
                                           LogicalMargin& aOffsets);

  // Resolve any inline-axis 'auto' margins (if any) for an absolutely
  // positioned frame. aMargin and aOffsets are both outparams (though we only
  // touch aOffsets if the position is overconstrained)
  static void ComputeAbsPosInlineAutoMargin(nscoord aAvailMarginSpace,
                                            WritingMode aContainingBlockWM,
                                            bool aIsMarginIStartAuto,
                                            bool aIsMarginIEndAuto,
                                            LogicalMargin& aMargin,
                                            LogicalMargin& aOffsets);

 protected:
  void InitCBReflowInput();
  void InitResizeFlags(nsPresContext* aPresContext, LayoutFrameType aFrameType);
  void InitDynamicReflowRoot();

  void InitConstraints(nsPresContext* aPresContext,
                       const Maybe<LogicalSize>& aContainingBlockSize,
                       const Maybe<LogicalMargin>& aBorder,
                       const Maybe<LogicalMargin>& aPadding,
                       LayoutFrameType aFrameType);

  // Returns the nearest containing block or block frame (whether or not
  // it is a containing block) for the specified frame.  Also returns
  // the inline-start edge and logical size of the containing block's
  // content area.
  // These are returned in the coordinate space of the containing block.
  nsIFrame* GetHypotheticalBoxContainer(nsIFrame* aFrame,
                                        nscoord& aCBIStartEdge,
                                        LogicalSize& aCBSize) const;

  // Calculate the position of the hypothetical box that the placeholder frame
  // (for a position:fixed/absolute element) would have if it were in the flow
  // (i.e., positioned statically).
  //
  // The position of the hypothetical box is relative to the padding edge of the
  // absolute containing block (aCBReflowInput->mFrame). The writing mode of the
  // hypothetical box will have the same block direction as the absolute
  // containing block, but it may differ in the inline direction.
  void CalculateHypotheticalPosition(
      nsPlaceholderFrame* aPlaceholderFrame, const ReflowInput* aCBReflowInput,
      nsHypotheticalPosition& aHypotheticalPos) const;

  void InitAbsoluteConstraints(const ReflowInput* aCBReflowInput,
                               const LogicalSize& aCBSize);

  // Calculates the computed values for the 'min-inline-size',
  // 'max-inline-size', 'min-block-size', and 'max-block-size' properties, and
  // stores them in the assorted data members
  void ComputeMinMaxValues(const LogicalSize& aCBSize);

  // aInsideBoxSizing returns the part of the padding, border, and margin
  // in the aAxis dimension that goes inside the edge given by box-sizing;
  // aOutsideBoxSizing returns the rest.
  void CalculateBorderPaddingMargin(LogicalAxis aAxis,
                                    nscoord aContainingBlockSize,
                                    nscoord* aInsideBoxSizing,
                                    nscoord* aOutsideBoxSizing) const;

  void CalculateBlockSideMargins();

  /**
   * @return true if mFrame is an internal table frame, i.e. an
   * ns[RowGroup|ColGroup|Row|Cell]Frame.  (We exclude nsTableColFrame
   * here since we never setup a ReflowInput for those.)
   */
  bool IsInternalTableFrame() const;

 private:
  // The available size in which to reflow the frame. The space represents the
  // amount of room for the frame's margin, border, padding, and content area.
  //
  // The available inline-size should be constrained. The frame's inline-size
  // you choose should fit within it.

  // In galley mode, the available block-size is always unconstrained, and only
  // page mode or multi-column layout involves a constrained available
  // block-size.
  //
  // An unconstrained available block-size means you can choose whatever size
  // you want. If the value is constrained, the frame's block-start border,
  // padding, and content, must fit. If a frame is fully-complete after reflow,
  // then its block-end border, padding, and margin (and similar for its
  // fully-complete ancestors) will need to fit within this available
  // block-size. However, if a frame is monolithic, it may choose a block-size
  // larger than the available block-size.
  LogicalSize mAvailableSize{mWritingMode};

  // The computed size specifies the frame's content area, and it does not apply
  // to inline non-replaced elements.
  //
  // For block-level frames, the computed inline-size is based on the
  // inline-size of the containing block, the margin/border/padding areas, and
  // the min/max inline-size.
  //
  // For non-replaced block-level frames in the flow and floated, if the
  // computed block-size is NS_UNCONSTRAINEDSIZE, you should choose a block-size
  // to shrink wrap around the normal flow child frames. The block-size must be
  // within the limit of the min/max block-size if there is such a limit.
  LogicalSize mComputedSize{mWritingMode};

  // Computed values for 'inset' properties. Only applies to 'positioned'
  // elements.
  LogicalMargin mComputedOffsets{mWritingMode};

  // Computed value for 'min-inline-size'/'min-block-size'.
  LogicalSize mComputedMinSize{mWritingMode};

  // Computed value for 'max-inline-size'/'max-block-size'.
  LogicalSize mComputedMaxSize{mWritingMode, NS_UNCONSTRAINEDSIZE,
                               NS_UNCONSTRAINEDSIZE};

  // Percentage basis in the block axis for the purpose of percentage resolution
  // on children.
  //
  // This will be ignored when mTreatBSizeAsIndefinite flag is true, or when a
  // customized containing block size is provided via ReflowInput's constructor
  // or Init(). When this percentage basis exists, it will be used to replace
  // the containing block's ComputedBSize() in
  // ComputeContainingBlockRectangle().
  //
  // This is currently used in a special scenario where we treat certain
  // sized-to-content flex items as having an 'auto' block-size for their final
  // reflow to accomodate fragmentation-imposed block-size growth. This sort of
  // flex item does nonetheless have a known block-size (from the flex layout
  // algorithm) that it needs to use as a definite percentage-basis for its
  // children during its final reflow; and we represent that here.
  Maybe<nscoord> mPercentageBasisInBlockAxis;

  // Cache the used line-height property.
  mutable nscoord mLineHeight = NS_UNCONSTRAINEDSIZE;
};

}  // namespace mozilla

#endif  // mozilla_ReflowInput_h
