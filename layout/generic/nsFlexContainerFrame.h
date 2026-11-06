/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: flex" and "display: -webkit-box" */

#ifndef nsFlexContainerFrame_h_
#define nsFlexContainerFrame_h_

#include <tuple>

#include "mozilla/IntrinsicISizesCache.h"
#include "mozilla/dom/FlexBinding.h"
#include "nsContainerFrame.h"
#include "nsILineIterator.h"

nsContainerFrame* NS_NewFlexContainerFrame(mozilla::PresShell* aPresShell,
                                           mozilla::ComputedStyle* aStyle);

/**
 * These structures are used to capture data during reflow to be
 * extracted by devtools via Chrome APIs. The structures are only
 * created when requested in GetFlexFrameWithComputedInfo(), and
 * the structures are attached to the nsFlexContainerFrame via the
 * FlexContainerInfo property.
 */
struct ComputedFlexItemInfo {
  nsCOMPtr<nsINode> mNode;
  nsRect mFrameRect;
  /**
   * mMainBaseSize is a measure of the size of the item in the main
   * axis before the flex sizing algorithm is applied. In the spec,
   * this is called "flex base size", but we use this name to connect
   * the value to the other main axis sizes.
   */
  nscoord mMainBaseSize;
  /**
   * mMainDeltaSize is the amount that the flex sizing algorithm
   * adds to the mMainBaseSize, before clamping to mMainMinSize and
   * mMainMaxSize. This can be thought of as the amount by which the
   * flex layout algorithm "wants" to shrink or grow the item, and
   * would do, if it was unconstrained. Since the flex sizing
   * algorithm proceeds linearly, the mMainDeltaSize for an item only
   * respects the resolved size of items already frozen.
   */
  nscoord mMainDeltaSize;
  nscoord mMainMinSize;
  nscoord mMainMaxSize;
  nscoord mCrossMinSize;
  nscoord mCrossMaxSize;
  mozilla::dom::FlexItemClampState mClampState;
};

struct ComputedFlexLineInfo {
  nsTArray<ComputedFlexItemInfo> mItems;
  nscoord mCrossStart;
  nscoord mCrossSize;
  nscoord mFirstBaselineOffset;
  nscoord mLastBaselineOffset;
  mozilla::dom::FlexLineGrowthState mGrowthState;
};

struct ComputedFlexContainerInfo {
  nsTArray<ComputedFlexLineInfo> mLines;
  mozilla::dom::FlexPhysicalDirection mMainAxisDirection;
  mozilla::dom::FlexPhysicalDirection mCrossAxisDirection;
};

/**
 * Helper class to get the orientation of a flex container's axes.
 */
class MOZ_STACK_CLASS FlexboxAxisInfo final {
 public:
  explicit FlexboxAxisInfo(const nsIFrame* aFlexContainer);

  // Is our main axis the inline axis? (Are we 'flex-direction:row[-reverse]'?)
  bool mIsRowOriented = true;

  // Is our main axis in the opposite direction as mWM's corresponding axis?
  // (e.g. RTL vs LTR)
  bool mIsMainAxisReversed = false;

  // Is our cross axis in the opposite direction as mWM's corresponding axis?
  // (e.g. BTT vs TTB)
  bool mIsCrossAxisReversed = false;

 private:
  // Helpers for constructor which determine the orientation of our axes, based
  // on legacy box properties (-webkit-box-orient, -webkit-box-direction) or
  // modern flexbox properties (flex-direction, flex-wrap) depending on whether
  // the flex container is a "legacy webkit box".
  void InitAxesFromLegacyProps(const nsIFrame* aFlexContainer);
  void InitAxesFromModernProps(const nsIFrame* aFlexContainer);
};

/**
 * This is the rendering object used for laying out elements with
 * "display: flex" or "display: inline-flex".
 *
 * We also use this class for elements with "display: -webkit-box" or
 * "display: -webkit-inline-box".
 *
 * Note: we represent the -webkit-box family of properties (-webkit-box-orient,
 * -webkit-box-flex, etc.) as aliases for their -moz equivalents.  And for
 * -webkit-{inline-}box containers, nsFlexContainerFrame will honor those
 * "legacy" properties for alignment/flexibility/etc. *instead of* honoring the
 * modern flexbox & alignment properties.  For brevity, many comments in
 * nsFlexContainerFrame.cpp simply refer to these properties using their
 * "-webkit" versions, since we're mostly expecting to encounter them in that
 * form. (Technically, the "-moz" versions of these properties *can* influence
 * layout here as well (since that's what the -webkit versions are aliased to)
 * -- but only inside of a "display:-webkit-{inline-}box" container.)
 */
class nsFlexContainerFrame final : public nsContainerFrame,
                                   public nsILineIterator {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsFlexContainerFrame)
  NS_DECL_QUERYFRAME

  // Factory method:
  friend nsContainerFrame* NS_NewFlexContainerFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle);

  // Forward-decls of helper classes
  class FlexItem;
  class FlexLine;
  class FlexboxAxisTracker;
  struct StrutInfo;
  class CachedBAxisMeasurement;
  class CachedFlexItemData;
  struct SharedFlexData;
  struct PerFragmentFlexData;
  class FlexItemIterator;

  // nsIFrame overrides
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  void MarkIntrinsicISizesDirty() override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aReflowOutput,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext) const override;

  // Unions the child overflow from our in-flow children.
  void UnionInFlowChildOverflow(mozilla::OverflowAreas&,
                                bool aAsIfScrolled = false);

  // Unions the child overflow from all our children, including out of flows.
  void UnionChildOverflow(mozilla::OverflowAreas&, bool aAsIfScrolled) final;

  // nsContainerFrame overrides
  bool DrainSelfOverflowList() override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;
  mozilla::StyleAlignFlags CSSAlignmentForAbsPosChild(
      const ReflowInput& aChildRI,
      mozilla::LogicalAxis aLogicalAxis) const override;

  // Return aFlexItem's used 'align-self' value and the associated flags
  // (safe/unsafe).
  //
  // Note: This method guarantees not to return StyleAlignFlags::NORMAL because
  // it converts NORMAL to STRETCH.
  std::pair<mozilla::StyleAlignFlags, mozilla::StyleAlignFlags>
  UsedAlignSelfAndFlagsForItem(const nsIFrame* aFlexItem) const;

  /**
   * Helper function to calculate packing space and initial offset of alignment
   * subjects in MainAxisPositionTracker() and CrossAxisPositionTracker() for
   * space-between, space-around, and space-evenly.
   *    * @param aNumThingsToPack             Number of alignment subjects.
   * @param aAlignVal                    Value for align-content or
   *                                     justify-content.
   * @param aFirstSubjectOffset          Outparam for first subject offset.
   * @param aNumPackingSpacesRemaining   Outparam for number of equal-sized
   *                                     packing spaces to apply between each
   *                                     alignment subject.
   * @param aPackingSpaceRemaining       Outparam for total amount of packing
   *                                     space to be divided up.
   */
  static void CalculatePackingSpace(
      uint32_t aNumThingsToPack,
      const mozilla::StyleContentDistribution& aAlignVal,
      nscoord* aFirstSubjectOffset, uint32_t* aNumPackingSpacesRemaining,
      nscoord* aPackingSpaceRemaining);

  /**
   * This property is created by a call to
   * nsFlexContainerFrame::GetFlexFrameWithComputedInfo.
   */
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(FlexContainerInfo,
                                      ComputedFlexContainerInfo)
  /**
   * This function should only be called on a nsFlexContainerFrame
   * that has just been returned by a call to
   * GetFlexFrameWithComputedInfo.
   */
  const ComputedFlexContainerInfo* GetFlexContainerInfo() {
    const ComputedFlexContainerInfo* info = GetProperty(FlexContainerInfo());
    NS_WARNING_ASSERTION(info,
                         "Property generation wasn't requested. "
                         "This is a known issue in Print Preview. "
                         "See Bug 1157012.");
    return info;
  }

  /**
   * Return aFrame as a flex frame after ensuring it has computed flex info.
   * @return nullptr if aFrame is null or doesn't have a flex frame
   *         as its content insertion frame.
   * @note this might destroy layout/style data since it may flush layout.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static nsFlexContainerFrame* GetFlexFrameWithComputedInfo(nsIFrame* aFrame);

  /**
   * Given a frame for a flex item, this method returns true IFF that flex
   * item's inline axis is the same as (i.e. not orthogonal to) its flex
   * container's main axis.
   *
   * (This method is only intended to be used from external
   * callers. Inside of flex reflow code, FlexItem::IsInlineAxisMainAxis() is
   * equivalent & more optimal.)
   *
   * @param aFrame a flex item (must return true from IsFlexItem)
   * @return true iff aFrame's inline axis is the same as (i.e. not orthogonal
   *              to) its flex container's main axis. Otherwise, false.
   */
  static bool IsItemInlineAxisMainAxis(nsIFrame* aFrame);

  /**
   * Returns true iff the given computed 'flex-basis' & main-size property
   * values collectively represent a used flex-basis of 'content'.
   * See https://drafts.csswg.org/css-flexbox-1/#valdef-flex-basis-auto
   *
   * @param aFlexBasis the computed 'flex-basis' for a flex item.
   * @param aMainSize the computed main-size property for a flex item.
   */
  static bool IsUsedFlexBasisContent(const mozilla::StyleFlexBasis& aFlexBasis,
                                     const mozilla::StyleSize& aMainSize);

  /**
   * Callback for nsIFrame::MarkIntrinsicISizesDirty() on a flex item.
   */
  static void MarkCachedFlexMeasurementsDirty(nsIFrame* aItemFrame);

  bool CanProvideLineIterator() const final { return true; }
  nsILineIterator* GetLineIterator() final { return this; }
  int32_t GetNumLines() const final;
  bool IsLineIteratorFlowRTL() final;
  mozilla::Result<LineInfo, nsresult> GetLine(int32_t aLineNumber) final;
  int32_t FindLineContaining(const nsIFrame* aFrame,
                             int32_t aStartLine = 0) final;
  NS_IMETHOD FindFrameAt(int32_t aLineNumber, nsPoint aPos,
                         nsIFrame** aFrameFound, bool* aPosIsBeforeFirstFrame,
                         bool* aPosIsAfterLastFrame) final;
  NS_IMETHOD CheckLineOrder(int32_t aLine, bool* aIsReordered,
                            nsIFrame** aFirstVisual,
                            nsIFrame** aLastVisual) final;

 protected:
  // Protected constructor & destructor
  explicit nsFlexContainerFrame(ComputedStyle* aStyle,
                                nsPresContext* aPresContext)
      : nsContainerFrame(aStyle, aPresContext, kClassID) {}

  virtual ~nsFlexContainerFrame();

  // Protected flex-container-specific methods / member-vars

  /**
   * This method does the bulk of the flex layout, implementing the algorithm
   * described at: https://drafts.csswg.org/css-flexbox-1/#layout-algorithm
   * (with a few initialization pieces happening in the caller, Reflow().
   *
   * (The logic behind the division of work between Reflow and DoFlexLayout is
   * as follows: DoFlexLayout() begins at the step that we have to jump back
   * to, if we find any visibility:collapse children, and Reflow() does
   * everything before that point.)
   *
   * @param aTentativeContentBoxMainSize the "tentative" content-box main-size
   *                                     of the flex container; "tentative"
   *                                     because it may be unconstrained or may
   *                                     run off the page.
   * @param aTentativeContentBoxCrossSize the "tentative" content-box cross-size
   *                                      of the flex container; "tentative"
   *                                      because it may be unconstrained or may
   *                                      run off the page.
   */
  struct FlexLayoutResult final {
    // The flex lines of the flex container.
    nsTArray<FlexLine> mLines;

    // The absolutely-positioned flex children.
    nsTArray<nsIFrame*> mPlaceholders;

    bool mHasCollapsedItems = false;

    // The final content-box main-size of the flex container as if there's no
    // fragmentation.
    nscoord mContentBoxMainSize = NS_UNCONSTRAINEDSIZE;

    // The final content-box cross-size of the flex container as if there's no
    // fragmentation.
    nscoord mContentBoxCrossSize = NS_UNCONSTRAINEDSIZE;

    // The flex container's ascent for the "first baseline" alignment, derived
    // from any baseline-aligned flex items in the startmost (from the
    // perspective of the flex container's WM) flex line, if any such items
    // exist. Otherwise, nscoord_MIN.
    //
    // Note: this is a distance from the border-box block-start edge.
    nscoord mAscent = NS_UNCONSTRAINEDSIZE;

    // The flex container's ascent for the "last baseline" alignment, derived
    // from any baseline-aligned flex items in the endmost (from the perspective
    // of the flex container's WM) flex line, if any such items exist.
    // Otherwise, nscoord_MIN.
    //
    // Note: this is a distance from the border-box block-end edge. It's
    // different from the identically-named-member FlexItem::mAscentForLast,
    // which is a distance from the item frame's border-box block-start edge.
    nscoord mAscentForLast = NS_UNCONSTRAINEDSIZE;
  };
  FlexLayoutResult DoFlexLayout(
      const ReflowInput& aReflowInput,
      const nscoord aTentativeContentBoxMainSize,
      const nscoord aTentativeContentBoxCrossSize,
      const FlexboxAxisTracker& aAxisTracker, nscoord aMainGapSize,
      nscoord aCrossGapSize, nsTArray<StrutInfo>& aStruts,
      ComputedFlexContainerInfo* const aContainerInfo);

  /**
   * If our devtools have requested a ComputedFlexContainerInfo for this flex
   * container, this method ensures that we have one (and if one already exists,
   * this method reinitializes it to look like a freshly-created one).
   *
   * @return the pointer to a freshly created or reinitialized
   *         ComputedFlexContainerInfo if our devtools have requested it;
   *         otherwise nullptr.
   */
  ComputedFlexContainerInfo* CreateOrClearFlexContainerInfo();

  /**
   * Helpers for DoFlexLayout to computed fields in ComputedFlexContainerInfo.
   */
  static void CreateFlexLineAndFlexItemInfo(
      ComputedFlexContainerInfo& aContainerInfo,
      const nsTArray<FlexLine>& aLines);

  static void ComputeFlexDirections(ComputedFlexContainerInfo& aContainerInfo,
                                    const FlexboxAxisTracker& aAxisTracker);

  static void UpdateFlexLineAndItemInfo(
      ComputedFlexContainerInfo& aContainerInfo,
      const nsTArray<FlexLine>& aLines);

  /**
   * Helper to query flex item's consumed block-size.
   */
  static nscoord FlexItemConsumedBSize(const FlexItem& aItem);

  /**
   * Construct a new FlexItem for the given child frame, directly at the end of
   * aLine.
   *
   * Before returning, this method also processes the FlexItem to resolve its
   * flex basis (including e.g. auto-height) as well as to resolve
   * "min-height:auto", via ResolveAutoFlexBasisAndMinSize(). (Basically, the
   * constructed FlexItem will be ready to participate in the "Resolve the
   * Flexible Lengths" step of the Flex Layout Algorithm.)
   * https://drafts.csswg.org/css-flexbox-1/#algo-flex
   *
   * Note that this method **does not** update aLine's main-size bookkeeping to
   * account for the newly-constructed flex item. The caller is responsible for
   * determining whether this line is a good fit for the new item. If so, the
   * caller should update aLine's bookkeeping (via
   * FlexLine::AddLastItemToMainSizeTotals), or move the new item to a new line.
   */
  void GenerateFlexItemForChild(FlexLine& aLine, nsIFrame* aChildFrame,
                                const ReflowInput& aParentReflowInput,
                                const FlexboxAxisTracker& aAxisTracker,
                                const nscoord aTentativeContentBoxCrossSize);

  /**
   * This method looks up cached block-axis measurements for a flex item, or
   * does a measuring reflow and caches those measurements.
   *
   * This avoids exponential reflows - see the comment above the
   * CachedBAxisMeasurement struct.
   */
  const CachedBAxisMeasurement& MeasureBSizeForFlexItem(
      FlexItem& aItem, ReflowInput& aChildReflowInput);

  /**
   * This method performs a "measuring" reflow to get the content BSize of
   * aFlexItem.Frame() (treating it as if it had a computed BSize of "auto"),
   * and returns the resulting BSize measurement.
   * (Helper for ResolveAutoFlexBasisAndMinSize().)
   */
  nscoord MeasureFlexItemContentBSize(FlexItem& aFlexItem,
                                      bool aForceBResizeForMeasuringReflow,
                                      const ReflowInput& aParentReflowInput);

  /**
   * This method resolves an "auto" flex-basis and/or min-main-size value
   * on aFlexItem, if needed.
   * (Helper for GenerateFlexItemForChild().)
   */
  void ResolveAutoFlexBasisAndMinSize(FlexItem& aFlexItem,
                                      const ReflowInput& aItemReflowInput,
                                      const FlexboxAxisTracker& aAxisTracker);

  /**
   * Partially resolves "min-[width|height]:auto" and returns the resulting
   * value. By "partially", I mean we don't consider the min-content size (but
   * we do consider the main-size and main max-size properties, and the
   * preferred aspect ratio). The caller is responsible for computing &
   * considering the min-content size in combination with the partially-resolved
   * value that this function returns.
   *
   * Basically, this function gets the specified size suggestion; if not, the
   * transferred size suggestion; if both sizes do not exist, return
   * nscoord_MAX.
   *
   * Spec reference: https://drafts.csswg.org/css-flexbox-1/#min-size-auto
   * (Helper for ResolveAutoFlexBasisAndMinSize().)
   */
  nscoord PartiallyResolveAutoMinSize(
      const FlexItem& aFlexItem, const ReflowInput& aItemReflowInput,
      const FlexboxAxisTracker& aAxisTracker) const;

  /**
   * This method:
   *  - Creates FlexItems for all of our child frames (except placeholders).
   *  - Groups those FlexItems into FlexLines.
   *  - Returns those FlexLines in the outparam |aLines|.
   *
   * This corresponds to "Collect flex items into flex lines" step in the spec.
   * https://drafts.csswg.org/css-flexbox-1/#algo-line-break
   *
   * For any child frames which are placeholders, this method will instead just
   * append that child to the outparam |aPlaceholders| for separate handling.
   * (Absolutely positioned children of a flex container are *not* flex items.)
   */
  void GenerateFlexLines(const ReflowInput& aReflowInput,
                         const nscoord aTentativeContentBoxMainSize,
                         const nscoord aTentativeContentBoxCrossSize,
                         const nsTArray<StrutInfo>& aStruts,
                         const FlexboxAxisTracker& aAxisTracker,
                         nscoord aMainGapSize,
                         nsTArray<nsIFrame*>& aPlaceholders,
                         nsTArray<FlexLine>& aLines, bool& aHasCollapsedItems);

  /**
   * Generates and returns a FlexLayoutResult that contains the FlexLines and
   * some sizing metrics that should be used to lay out a particular flex
   * container continuation (i.e. don't call this on the first-in-flow).
   */
  FlexLayoutResult GenerateFlexLayoutResult();

  /**
   * Resolves the content-box main-size of a flex container frame,
   * primarily based on:
   * - the "tentative" main size, taken from the reflow input ("tentative"
   *   because it may be unconstrained or may run off the page).
   * - the sizes of our lines of flex items.
   *
   * We assume the available block-size is always *unconstrained* because this
   * is called only in flex algorithm to measure the flex container's size
   * without regards to pagination.
   *
   * Guaranteed to return a definite length, i.e. not NS_UNCONSTRAINEDSIZE,
   * aside from cases with huge lengths which happen to compute to that value.
   *
   * This corresponds to "Determine the main size of the flex container" step in
   * the spec. https://drafts.csswg.org/css-flexbox-1/#algo-main-container
   *
   * (Note: This function should be structurally similar to
   *  ComputeCrossSize().)
   */
  nscoord ComputeMainSize(const ReflowInput& aReflowInput,
                          const FlexboxAxisTracker& aAxisTracker,
                          const nscoord aTentativeContentBoxMainSize,
                          nsTArray<FlexLine>& aLines) const;

  nscoord ComputeCrossSize(const ReflowInput& aReflowInput,
                           const FlexboxAxisTracker& aAxisTracker,
                           const nscoord aTentativeContentBoxCrossSize,
                           nscoord aSumLineCrossSizes, bool* aIsDefinite) const;

  /**
   * Compute the size of the available space that we'll give to our children to
   * reflow into. In particular, compute the available size that we would give
   * to a hypothetical child placed at the IStart/BStart corner of this flex
   * container's content-box.
   *
   * @param aReflowInput the flex container's reflow input.
   * @param aBorderPadding the border and padding of this frame with the
   *                       assumption that this is the last fragment.
   *
   * @return the size of the available space for our children to reflow into.
   */
  mozilla::LogicalSize ComputeAvailableSizeForItems(
      const ReflowInput& aReflowInput,
      const mozilla::LogicalMargin& aBorderPadding) const;

  void SizeItemInCrossAxis(ReflowInput& aChildReflowInput, FlexItem& aItem);

  /**
   * This method computes the metrics to be reported via the flex container's
   * ReflowOutput & nsReflowStatus output parameters in Reflow().
   *
   * @param aContentBoxSize the final content-box size for the flex container as
   *                        a whole, converted from the flex container's
   *                        main/cross sizes. The main/cross sizes are computed
   *                        by DoFlexLayout() if this frame is the
   *                        first-in-flow, or are the stored ones in
   *                        SharedFlexData if this frame is a not the
   *                        first-in-flow.
   * @param aBorderPadding the border and padding for this frame (possibly with
   *                       some sides skipped as-appropriate, if we're in a
   *                       continuation chain).
   * @param aConsumedBSize the sum of our content-box block-size consumed by our
   *                       prev-in-flows.
   * @param aMayNeedNextInFlow true if we may need a next-in-flow because our
   *                           effective content-box block-size exceeds the
   *                           available block-size.
   * @param aMaxBlockEndEdgeOfChildren the maximum block-end edge of the
   *                                   children of this fragment in this frame's
   *                                   coordinate space (as returned by
   *                                   ReflowChildren()).
   * @param aChildrenStatus the reflow status of children (as returned by
   *                        ReflowChildren()).
   * @param aFlr the result returned by DoFlexLayout.
   *             Note: aFlr is mostly an "input" parameter, but we use
   *             aFlr.mAscent as an "in/out" parameter; it's initially the
   *             "tentative" flex container ascent computed in DoFlexLayout; or
   *             nscoord_MIN if the ascent hasn't been established yet. If the
   *             latter, this will be updated with an ascent derived from the
   *             (WM-relative) startmost flex item (if there are any flex
   *             items). Similar for aFlr.mAscentForLast.
   */
  void PopulateReflowOutput(
      ReflowOutput& aReflowOutput, const ReflowInput& aReflowInput,
      nsReflowStatus& aStatus, const mozilla::LogicalSize& aContentBoxSize,
      const mozilla::LogicalMargin& aBorderPadding,
      const nscoord aConsumedBSize, const bool aMayNeedNextInFlow,
      const nscoord aMaxBlockEndEdgeOfChildren,
      const nsReflowStatus& aChildrenStatus,
      const FlexboxAxisTracker& aAxisTracker, FlexLayoutResult& aFlr);

  /**
   * Perform a final Reflow for our child frames.
   *
   * @param aContainerSize this frame's tentative physical border-box size, used
   *                       only for logical to physical coordinate conversion.
   * @param aAvailableSizeForItems the size of the available space for our
   *                               children to reflow into.
   * @param aBorderPadding the border and padding for this frame (possibly with
   *                       some sides skipped as-appropriate, if we're in a
   *                       continuation chain).
   * @param aSumOfPrevInFlowsChildrenBlockSize See the comment for
   *                                           SumOfChildrenBlockSizeProperty.
   * @param aFlr the result returned by DoFlexLayout.
   * @param aFragmentData See the comment for PerFragmentFlexData.
   *                      Note: aFragmentData is an "in/out" parameter. It is
   *                      initialized by the data stored in our prev-in-flow's
   *                      PerFragmentFlexData::Prop(); its fields will then be
   *                      updated and become our PerFragmentFlexData.
   * @return nscoord the maximum block-end edge of children of this fragment in
   *                 flex container's coordinate space.
   * @return nsReflowStatus the reflow status of children (i.e. flex items). If
   *                        any child had an incomplete reflow status, then this
   *                        will be Incomplete. Otherwise, if any child had an
   *                        overflow-incomplete reflow status, this will be
   *                        OverflowIncomplete.
   */
  std::tuple<nscoord, nsReflowStatus> ReflowChildren(
      const ReflowInput& aReflowInput, const nsSize& aContainerSize,
      const mozilla::LogicalSize& aAvailableSizeForItems,
      const mozilla::LogicalMargin& aBorderPadding,
      const FlexboxAxisTracker& aAxisTracker, FlexLayoutResult& aFlr,
      PerFragmentFlexData& aFragmentData);

  /**
   * Moves the given flex item's frame to the given LogicalPosition (modulo any
   * relative positioning).
   *
   * This can be used in cases where we've already done a "measuring reflow"
   * for the flex item at the correct size, and hence can skip its final reflow
   * (but still need to move it to the right final position).
   *
   * @param aItem           The flex item whose frame should be moved.
   * @param aFramePos       The position where the flex item's frame should
   *                        be placed. (pre-relative positioning)
   * @param aContainerSize  The flex container's size (required by some methods
   *                        that we call, to interpret aFramePos correctly).
   */
  void MoveFlexItemToFinalPosition(const FlexItem& aItem,
                                   const mozilla::LogicalPoint& aFramePos,
                                   const nsSize& aContainerSize);
  /**
   * Helper-function to reflow a child frame, at its final position determined
   * by flex layout.
   *
   * @param aAxisTracker    A FlexboxAxisTracker with the flex container's axes.
   * @param aReflowInput    The flex container's reflow input.
   * @param aItem           The flex item to be reflowed.
   * @param aFramePos       The position where the flex item's frame should
   *                        be placed. (pre-relative positioning)
   * @param aIsAdjacentWithBStart True if aFramePos is adjacent with the flex
   *                              container's content-box block-start edge.
   * @param aAvailableSize  The available size to reflow the child frame (in the
   *                        child frame's writing-mode).
   * @param aContainerSize  The flex container's size (required by some methods
   *                        that we call, to interpret aFramePos correctly).
   * @return the child frame's reflow status.
   */
  nsReflowStatus ReflowFlexItem(const FlexboxAxisTracker& aAxisTracker,
                                const ReflowInput& aReflowInput,
                                const FlexItem& aItem,
                                const mozilla::LogicalPoint& aFramePos,
                                const bool aIsAdjacentWithBStart,
                                const mozilla::LogicalSize& aAvailableSize,
                                const nsSize& aContainerSize);

  /**
   * Helper-function to perform a "dummy reflow" on all our nsPlaceholderFrame
   * children, at the container's content-box origin.
   *
   * This doesn't actually represent the static position of the placeholders'
   * out-of-flow (OOF) frames -- we can't compute that until we've reflowed the
   * OOF, because (depending on the CSS Align properties) the static position
   * may be influenced by the OOF's size.  So for now, we just co-opt the
   * placeholder to store the flex container's logical content-box origin, and
   * we defer to nsAbsoluteContainingBlock to determine the OOF's actual static
   * position (using this origin, the OOF's size, and the CSS Align
   * properties).
   *
   * @param aReflowInput       The flex container's reflow input.
   * @param aPlaceholders      An array of all the flex container's
   *                           nsPlaceholderFrame children.
   * @param aContentBoxOrigin  The flex container's logical content-box
   *                           origin (in its own coordinate space).
   * @param aContainerSize     The flex container's size (required by some
   *                           reflow methods to interpret positions correctly).
   */
  void ReflowPlaceholders(const ReflowInput& aReflowInput,
                          nsTArray<nsIFrame*>& aPlaceholders,
                          const mozilla::LogicalPoint& aContentBoxOrigin,
                          const nsSize& aContainerSize);

  /**
   * Helper to implement IntrinsicISize().
   */
  nscoord ComputeIntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                                mozilla::IntrinsicISizeType aType);

  /**
   * If the flex item contains `NS_FRAME_CONTAINS_RELATIVE_BSIZE`, potentially
   * propagate the flag to this flex container. See the function implementation
   * for more details.
   */
  void MaybePropagateRelativeBSizeFlagFrom(const FlexItem& aItem);

  /**
   * Cached values to optimize IntrinsicISize().
   */
  mozilla::IntrinsicISizesCache mCachedIntrinsicSizes;

  /**
   * Cached baselines computed in our last reflow to optimize
   * GetNaturalBaselineBOffset().
   */
  // Note: the first baseline is a distance from our border-box block-start
  // edge.
  nscoord mFirstBaseline = NS_INTRINSIC_ISIZE_UNKNOWN;
  // Note: the last baseline is a distance from our border-box block-end edge.
  nscoord mLastBaseline = NS_INTRINSIC_ISIZE_UNKNOWN;
};

#endif /* nsFlexContainerFrame_h_ */
