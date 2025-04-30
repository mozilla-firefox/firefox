/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: grid | inline-grid" */

#ifndef nsGridContainerFrame_h___
#define nsGridContainerFrame_h___

#include "mozilla/CSSOrderAwareFrameIterator.h"
#include "mozilla/IntrinsicISizesCache.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/HashTable.h"
#include "nsAtomHashKeys.h"
#include "nsContainerFrame.h"
#include "nsILineIterator.h"

namespace mozilla {
class PresShell;
namespace dom {
class Grid;
}
}  // namespace mozilla

/**
 * Factory function.
 * @return a newly allocated nsGridContainerFrame (infallible)
 */
nsContainerFrame* NS_NewGridContainerFrame(mozilla::PresShell* aPresShell,
                                           mozilla::ComputedStyle* aStyle);

namespace mozilla {

/**
 * The number of implicit / explicit tracks and their sizes.
 */
struct ComputedGridTrackInfo {
  ComputedGridTrackInfo(
      uint32_t aNumLeadingImplicitTracks, uint32_t aNumExplicitTracks,
      uint32_t aStartFragmentTrack, uint32_t aEndFragmentTrack,
      nsTArray<nscoord>&& aPositions, nsTArray<nscoord>&& aSizes,
      nsTArray<uint32_t>&& aStates, nsTArray<bool>&& aRemovedRepeatTracks,
      uint32_t aRepeatFirstTrack,
      nsTArray<nsTArray<StyleCustomIdent>>&& aResolvedLineNames,
      bool aIsSubgrid, bool aIsMasonry)
      : mNumLeadingImplicitTracks(aNumLeadingImplicitTracks),
        mNumExplicitTracks(aNumExplicitTracks),
        mStartFragmentTrack(aStartFragmentTrack),
        mEndFragmentTrack(aEndFragmentTrack),
        mPositions(std::move(aPositions)),
        mSizes(std::move(aSizes)),
        mStates(std::move(aStates)),
        mRemovedRepeatTracks(std::move(aRemovedRepeatTracks)),
        mResolvedLineNames(std::move(aResolvedLineNames)),
        mRepeatFirstTrack(aRepeatFirstTrack),
        mIsSubgrid(aIsSubgrid),
        mIsMasonry(aIsMasonry) {}
  uint32_t mNumLeadingImplicitTracks;
  uint32_t mNumExplicitTracks;
  uint32_t mStartFragmentTrack;
  uint32_t mEndFragmentTrack;
  nsTArray<nscoord> mPositions;
  nsTArray<nscoord> mSizes;
  nsTArray<uint32_t> mStates;
  // Indicates if a track has been collapsed. This will be populated for each
  // track in the repeat(auto-fit) and repeat(auto-fill), even if there are no
  // collapsed tracks.
  nsTArray<bool> mRemovedRepeatTracks;
  // Contains lists of all line name lists, including the name lists inside
  // repeats. When a repeat(auto) track exists, the internal track names will
  // appear once each in this array.
  nsTArray<nsTArray<StyleCustomIdent>> mResolvedLineNames;
  uint32_t mRepeatFirstTrack;
  bool mIsSubgrid;
  bool mIsMasonry;
};

struct ComputedGridLineInfo {
  explicit ComputedGridLineInfo(
      nsTArray<nsTArray<RefPtr<nsAtom>>>&& aNames,
      const nsTArray<RefPtr<nsAtom>>& aNamesBefore,
      const nsTArray<RefPtr<nsAtom>>& aNamesAfter,
      nsTArray<RefPtr<nsAtom>>&& aNamesFollowingRepeat)
      : mNames(std::move(aNames)),
        mNamesBefore(aNamesBefore.Clone()),
        mNamesAfter(aNamesAfter.Clone()),
        mNamesFollowingRepeat(std::move(aNamesFollowingRepeat)) {}
  nsTArray<nsTArray<RefPtr<nsAtom>>> mNames;
  nsTArray<RefPtr<nsAtom>> mNamesBefore;
  nsTArray<RefPtr<nsAtom>> mNamesAfter;
  nsTArray<RefPtr<nsAtom>> mNamesFollowingRepeat;
};
}  // namespace mozilla

class nsGridContainerFrame final : public nsContainerFrame,
                                   public nsILineIterator {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsGridContainerFrame)
  NS_DECL_QUERYFRAME
  using ComputedGridTrackInfo = mozilla::ComputedGridTrackInfo;
  using ComputedGridLineInfo = mozilla::ComputedGridLineInfo;
  using LogicalAxis = mozilla::LogicalAxis;
  using BaselineSharingGroup = mozilla::BaselineSharingGroup;
  using NamedArea = mozilla::StyleNamedArea;

  template <typename T>
  using PerBaseline = mozilla::EnumeratedArray<BaselineSharingGroup, T, 2>;

  template <typename T>
  using PerLogicalAxis = mozilla::EnumeratedArray<LogicalAxis, T, 2>;

  // nsIFrame overrides
  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
  void DidSetComputedStyle(ComputedStyle* aOldStyle) override;

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  void MarkIntrinsicISizesDirty() override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext) const override {
    if (StyleDisplay()->IsContainLayout() ||
        HasAnyStateBits(NS_STATE_GRID_SYNTHESIZE_BASELINE)) {
      return Nothing{};
    }
    return mozilla::Some(GetBBaseline(aBaselineGroup));
  }

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
  void ExtraContainerFrameInfo(nsACString& aTo,
                               bool aListOnlyDeterministic) const override;
#endif

  // nsContainerFrame overrides
  bool DrainSelfOverflowList() override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;

#ifdef DEBUG
  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
#endif

  bool CanProvideLineIterator() const final { return true; }
  nsILineIterator* GetLineIterator() final { return this; }
  int32_t GetNumLines() const final;
  bool IsLineIteratorFlowRTL() final;
  mozilla::Result<LineInfo, nsresult> GetLine(int32_t aLineNumber) final;
  int32_t FindLineContaining(nsIFrame* aFrame, int32_t aStartLine = 0) final;
  NS_IMETHOD FindFrameAt(int32_t aLineNumber, nsPoint aPos,
                         nsIFrame** aFrameFound, bool* aPosIsBeforeFirstFrame,
                         bool* aPosIsAfterLastFrame) final;
  NS_IMETHOD CheckLineOrder(int32_t aLine, bool* aIsReordered,
                            nsIFrame** aFirstVisual,
                            nsIFrame** aLastVisual) final;

  /**
   * Return the containing block for aChild which MUST be an abs.pos. child
   * of a grid container and that container must have been reflowed.
   */
  static const nsRect& GridItemCB(nsIFrame* aChild);

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(GridItemContainingBlockRect, nsRect)

  /**
   * These properties are created by a call to
   * nsGridContainerFrame::GetGridFrameWithComputedInfo, typically from
   * Element::GetGridFragments.
   */
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(GridColTrackInfo, ComputedGridTrackInfo)
  const ComputedGridTrackInfo* GetComputedTemplateColumns() {
    const ComputedGridTrackInfo* info = GetProperty(GridColTrackInfo());
    MOZ_ASSERT(info, "Property generation wasn't requested.");
    return info;
  }

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(GridRowTrackInfo, ComputedGridTrackInfo)
  const ComputedGridTrackInfo* GetComputedTemplateRows() {
    const ComputedGridTrackInfo* info = GetProperty(GridRowTrackInfo());
    MOZ_ASSERT(info, "Property generation wasn't requested.");
    return info;
  }

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(GridColumnLineInfo, ComputedGridLineInfo)
  const ComputedGridLineInfo* GetComputedTemplateColumnLines() {
    const ComputedGridLineInfo* info = GetProperty(GridColumnLineInfo());
    MOZ_ASSERT(info, "Property generation wasn't requested.");
    return info;
  }

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(GridRowLineInfo, ComputedGridLineInfo)
  const ComputedGridLineInfo* GetComputedTemplateRowLines() {
    const ComputedGridLineInfo* info = GetProperty(GridRowLineInfo());
    MOZ_ASSERT(info, "Property generation wasn't requested.");
    return info;
  }

  /**
   * This property is set by the creation of a dom::Grid object, and cleared
   * during GC unlink. Since the Grid object manages the lifecycle, the property
   * itself is set without a destructor. The property is also cleared whenever
   * new grid computed info is generated during reflow, ensuring that we aren't
   * holding a stale dom::Grid object.
   */
  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(GridFragmentInfo, mozilla::dom::Grid)
  mozilla::dom::Grid* GetGridFragmentInfo() {
    return GetProperty(GridFragmentInfo());
  }

  using ImplicitNamedAreas =
      mozilla::HashMap<mozilla::AtomHashKey, NamedArea, mozilla::AtomHashKey>;
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(ImplicitNamedAreasProperty,
                                      ImplicitNamedAreas)
  ImplicitNamedAreas* GetImplicitNamedAreas() const {
    return GetProperty(ImplicitNamedAreasProperty());
  }

  using ExplicitNamedAreas = mozilla::StyleOwnedSlice<NamedArea>;
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(ExplicitNamedAreasProperty,
                                      ExplicitNamedAreas)
  ExplicitNamedAreas* GetExplicitNamedAreas() const {
    return GetProperty(ExplicitNamedAreasProperty());
  }

  using nsContainerFrame::IsMasonry;

  /** Return true if this frame has masonry layout in any axis. */
  bool IsMasonry() const {
    return HasAnyStateBits(NS_STATE_GRID_IS_ROW_MASONRY |
                           NS_STATE_GRID_IS_COL_MASONRY);
  }

  /** Return true if this frame is subgridded in its aAxis. */
  bool IsSubgrid(LogicalAxis aAxis) const {
    return HasAnyStateBits(aAxis == mozilla::LogicalAxis::Block
                               ? NS_STATE_GRID_IS_ROW_SUBGRID
                               : NS_STATE_GRID_IS_COL_SUBGRID);
  }
  bool IsColSubgrid() const { return IsSubgrid(mozilla::LogicalAxis::Inline); }
  bool IsRowSubgrid() const { return IsSubgrid(mozilla::LogicalAxis::Block); }
  /** Return true if this frame is subgridded in any axis. */
  bool IsSubgrid() const {
    return HasAnyStateBits(NS_STATE_GRID_IS_ROW_SUBGRID |
                           NS_STATE_GRID_IS_COL_SUBGRID);
  }

  /** Return true if this frame has an item that is subgridded in our aAxis. */
  bool HasSubgridItems(LogicalAxis aAxis) const {
    return HasAnyStateBits(aAxis == mozilla::LogicalAxis::Block
                               ? NS_STATE_GRID_HAS_ROW_SUBGRID_ITEM
                               : NS_STATE_GRID_HAS_COL_SUBGRID_ITEM);
  }
  /** Return true if this frame has any subgrid items. */
  bool HasSubgridItems() const {
    return HasAnyStateBits(NS_STATE_GRID_HAS_ROW_SUBGRID_ITEM |
                           NS_STATE_GRID_HAS_COL_SUBGRID_ITEM);
  }
  /**
   * Return true if the grid item aChild should stretch in its aAxis (i.e. aAxis
   * is in the aChild's writing-mode).
   *
   * Note: this method does *not* consider the grid item's aspect-ratio and
   * natural size in the axis when the self-alignment value is 'normal' per
   * https://drafts.csswg.org/css-grid/#grid-item-sizing
   */
  bool GridItemShouldStretch(const nsIFrame* aChild, LogicalAxis aAxis) const;

  /**
   * Returns true if aFrame forms an independent formatting context and hence
   * should be inhibited from being a subgrid (i.e. if the used value of
   * 'grid-template-{rows,columns}:subgrid' should be 'none').
   * https://drafts.csswg.org/css-grid-2/#subgrid-listing
   *
   * (Note this only makes sense to call if aFrame is itself either a grid
   * container frame or a wrapper frame for a grid container frame, e.g. a
   * scroll container frame for a scrollable grid.  Having said that, this is
   * technically safe to call on any non-null frame.)
   */
  static bool ShouldInhibitSubgridDueToIFC(const nsIFrame* aFrame);

  /**
   * Return a container grid frame for the supplied frame, if available.
   * @return nullptr if aFrame has no grid container.
   */
  static nsGridContainerFrame* GetGridContainerFrame(nsIFrame* aFrame);

  /**
   * Return a container grid frame, and ensure it has computed grid info
   * @return nullptr if aFrame has no grid container, or frame was destroyed
   * @note this might destroy layout/style data since it may flush layout
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static nsGridContainerFrame* GetGridFrameWithComputedInfo(nsIFrame* aFrame);

  /**
   * Callback for nsIFrame::MarkIntrinsicISizesDirty() on a grid item.
   */
  static void MarkCachedGridMeasurementsDirty(nsIFrame* aItemFrame);

  class CachedBAxisMeasurement;
  struct Subgrid;
  struct UsedTrackSizes;
  struct TrackSize;
  struct GridItemInfo;
  struct GridReflowInput;
  struct FindItemInGridOrderResult {
    // The first(last) item in (reverse) grid order.
    const GridItemInfo* mItem;
    // Does the above item span the first(last) track?
    bool mIsInEdgeTrack;
  };

  /** Return our parent grid container; |this| MUST be a subgrid. */
  nsGridContainerFrame* ParentGridContainerForSubgrid() const;

  // https://drafts.csswg.org/css-sizing/#constraints
  enum class SizingConstraint {
    MinContent,   // sizing under min-content constraint
    MaxContent,   // sizing under max-content constraint
    NoConstraint  // no constraint, used during Reflow
  };

 protected:
  typedef mozilla::LogicalRect LogicalRect;
  typedef mozilla::WritingMode WritingMode;
  struct Grid;
  struct GridArea;
  class LineNameMap;
  struct LineRange;
  struct SharedGridData;
  struct SubgridFallbackTrackSizingFunctions;
  struct TrackSizingFunctions;
  struct Tracks;
  struct TranslatedLineRange;
  friend nsContainerFrame* NS_NewGridContainerFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle);
  explicit nsGridContainerFrame(ComputedStyle* aStyle,
                                nsPresContext* aPresContext)
      : nsContainerFrame(aStyle, aPresContext, kClassID) {
    for (auto& perAxisBaseline : mBaseline) {
      for (auto& baseline : perAxisBaseline) {
        baseline = NS_INTRINSIC_ISIZE_UNKNOWN;
      }
    }
  }

  /**
   * XXX temporary - move the ImplicitNamedAreas stuff to the style system.
   * The implicit area names that come from x-start .. x-end lines in
   * grid-template-columns / grid-template-rows are stored in this frame
   * property when needed, as a ImplicitNamedAreas* value.
   */
  void InitImplicitNamedAreas(const nsStylePosition* aStyle);

  using LineNameList =
      const mozilla::StyleOwnedSlice<mozilla::StyleCustomIdent>;
  void AddImplicitNamedAreas(mozilla::Span<LineNameList>);
  using StyleLineNameListValue =
      const mozilla::StyleGenericLineNameListValue<mozilla::StyleInteger>;
  void AddImplicitNamedAreas(mozilla::Span<StyleLineNameListValue>);

  /**
   * Reflow and place our children.
   * @return the consumed size of all of this grid container's continuations
   *         so far including this frame
   */
  nscoord ReflowChildren(GridReflowInput& aGridRI,
                         const LogicalRect& aContentArea,
                         const nsSize& aContainerSize,
                         ReflowOutput& aDesiredSize, nsReflowStatus& aStatus);

  /**
   * Helper to implement IntrinsicISize().
   */
  nscoord ComputeIntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                                mozilla::IntrinsicISizeType aType);

  nscoord GetBBaseline(BaselineSharingGroup aBaselineGroup) const {
    return mBaseline[mozilla::LogicalAxis::Block][aBaselineGroup];
  }
  nscoord GetIBaseline(BaselineSharingGroup aBaselineGroup) const {
    return mBaseline[mozilla::LogicalAxis::Inline][aBaselineGroup];
  }

  /**
   * Calculate this grid container's baselines.
   * @param aBaselineSet which baseline(s) to derive from a baseline-group or
   * items; a baseline not included is synthesized from the border-box instead.
   * @param aFragmentStartTrack is the first track in this fragment in the same
   * axis as aMajor.  Pass zero if that's not the axis we're fragmenting in.
   * @param aFirstExcludedTrack should be the first track in the next fragment
   * or one beyond the final track in the last fragment, in aMajor's axis.
   * Pass the number of tracks if that's not the axis we're fragmenting in.
   */
  enum BaselineSet : uint32_t {
    eNone = 0x0,
    eFirst = 0x1,
    eLast = 0x2,
    eBoth = eFirst | eLast,
  };
  void CalculateBaselines(BaselineSet aBaselineSet,
                          mozilla::CSSOrderAwareFrameIterator* aIter,
                          const nsTArray<GridItemInfo>* aGridItems,
                          const Tracks& aTracks, uint32_t aFragmentStartTrack,
                          uint32_t aFirstExcludedTrack, WritingMode aWM,
                          const nsSize& aCBPhysicalSize,
                          nscoord aCBBorderPaddingStart,
                          nscoord aCBBorderPaddingStartEnd, nscoord aCBSize);

  /**
   * Synthesize a Grid container baseline for aGroup.
   */
  nscoord SynthesizeBaseline(const FindItemInGridOrderResult& aGridOrderItem,
                             LogicalAxis aAxis, BaselineSharingGroup aGroup,
                             const nsSize& aCBPhysicalSize, nscoord aCBSize,
                             WritingMode aCBWM);
  /**
   * Find the first item in Grid Order in this fragment.
   * https://drafts.csswg.org/css-grid/#grid-order
   * @param aFragmentStartTrack is the first track in this fragment in the same
   * axis as aMajor.  Pass zero if that's not the axis we're fragmenting in.
   */
  static FindItemInGridOrderResult FindFirstItemInGridOrder(
      mozilla::CSSOrderAwareFrameIterator& aIter,
      const nsTArray<GridItemInfo>& aGridItems, LineRange GridArea::* aMajor,
      LineRange GridArea::* aMinor, uint32_t aFragmentStartTrack);
  /**
   * Find the last item in Grid Order in this fragment.
   * @param aFragmentStartTrack is the first track in this fragment in the same
   * axis as aMajor.  Pass zero if that's not the axis we're fragmenting in.
   * @param aFirstExcludedTrack should be the first track in the next fragment
   * or one beyond the final track in the last fragment, in aMajor's axis.
   * Pass the number of tracks if that's not the axis we're fragmenting in.
   */
  static FindItemInGridOrderResult FindLastItemInGridOrder(
      mozilla::ReverseCSSOrderAwareFrameIterator& aIter,
      const nsTArray<GridItemInfo>& aGridItems, LineRange GridArea::* aMajor,
      LineRange GridArea::* aMinor, uint32_t aFragmentStartTrack,
      uint32_t aFirstExcludedTrack);

  /**
   * Update our NS_STATE_GRID_IS_COL/ROW_SUBGRID bits and related subgrid state
   * on our entire continuation chain based on the current style.
   * This is needed because grid-template-columns/rows style changes only
   * trigger a reflow so we need to update this dynamically.
   */
  void UpdateSubgridFrameState();

  /**
   * Return the NS_STATE_GRID_IS_COL/ROW_SUBGRID and
   * NS_STATE_GRID_IS_ROW/COL_MASONRY bits we ought to have.
   */
  nsFrameState ComputeSelfSubgridMasonryBits() const;

 private:
  // Helpers for ReflowChildren
  struct Fragmentainer {
    /**
     * The distance from the first grid container fragment's block-axis content
     * edge to the fragmentainer end.
     */
    nscoord mToFragmentainerEnd;
    /**
     * True if the current fragment is at the start of the fragmentainer.
     */
    bool mIsTopOfPage;
    /**
     * Is there a Class C break opportunity at the start content edge?
     */
    bool mCanBreakAtStart;
    /**
     * Is there a Class C break opportunity at the end content edge?
     */
    bool mCanBreakAtEnd;
    /**
     * Is the grid container's block-size unconstrained?
     */
    bool mIsAutoBSize;
  };

  mozilla::Maybe<nsGridContainerFrame::Fragmentainer> GetNearestFragmentainer(
      const GridReflowInput& aGridRI) const;

  // @return the consumed size of all continuations so far including this frame
  nscoord ReflowInFragmentainer(GridReflowInput& aGridRI,
                                const LogicalRect& aContentArea,
                                ReflowOutput& aDesiredSize,
                                nsReflowStatus& aStatus,
                                Fragmentainer& aFragmentainer,
                                const nsSize& aContainerSize);

  // Helper for ReflowInFragmentainer
  // @return the consumed size of all continuations so far including this frame
  nscoord ReflowRowsInFragmentainer(
      GridReflowInput& aGridRI, const LogicalRect& aContentArea,
      ReflowOutput& aDesiredSize, nsReflowStatus& aStatus,
      Fragmentainer& aFragmentainer, const nsSize& aContainerSize,
      const nsTArray<const GridItemInfo*>& aItems, uint32_t aStartRow,
      uint32_t aEndRow, nscoord aBSize, nscoord aAvailableSize);

  // Helper for ReflowChildren / ReflowInFragmentainer
  void ReflowInFlowChild(nsIFrame* aChild, const GridItemInfo* aGridItemInfo,
                         nsSize aContainerSize,
                         const mozilla::Maybe<nscoord>& aStretchBSize,
                         const Fragmentainer* aFragmentainer,
                         const GridReflowInput& aGridRI,
                         const LogicalRect& aContentArea,
                         ReflowOutput& aDesiredSize, nsReflowStatus& aStatus);

  // Helper for Reflow. This is intended to be called *before* the final call to
  // CalculateTrackSizesForAxis() for the block-axis.
  //
  // @return The block-size that can be used to (re-)resolve the final row
  // sizes.
  nscoord ComputeBSizeForResolvingRowSizes(
      GridReflowInput& aGridRI, const Grid& aGrid, nscoord aComputedBSize,
      const Maybe<nscoord>& aContainIntrinsicBSize) const;

  // Helper for Reflow. This is intended to be called *after* the final call to
  // CalculateTrackSizesForAxis() for the block-axis.
  //
  // @param aBSizeForResolvingRowSizes the value returned by
  // ComputeBSizeForResolvingRowSizes().
  // @return The intrinsic content block-size that can be used with other
  // logic in Reflow() to determine the content block-size.
  nscoord ComputeIntrinsicContentBSize(
      const GridReflowInput& aGridRI, nscoord aComputedBSize,
      nscoord aBSizeForResolvingRowSizes,
      const Maybe<nscoord>& aContainIntrinsicBSize) const;

  /**
   * Places and reflows items when we have masonry layout.
   * It handles unconstrained reflow and also fragmentation when the row axis
   * is the masonry axis.  ReflowInFragmentainer handles the case when we're
   * fragmenting and our row axis is a grid axis and it handles masonry layout
   * in the column axis in that case.
   * @return the intrinsic size in the masonry axis
   */
  nscoord MasonryLayout(GridReflowInput& aGridRI,
                        const LogicalRect& aContentArea,
                        SizingConstraint aConstraint,
                        ReflowOutput& aDesiredSize, nsReflowStatus& aStatus,
                        Fragmentainer* aFragmentainer,
                        const nsSize& aContainerSize);

  // Return the stored UsedTrackSizes, if any.
  UsedTrackSizes* GetUsedTrackSizes() const;

  // Store the given TrackSizes in aAxis on a UsedTrackSizes frame property.
  void StoreUsedTrackSizes(LogicalAxis aAxis,
                           const nsTArray<TrackSize>& aSizes);

  // The internal implementation for AddImplicitNamedAreas().
  void AddImplicitNamedAreasInternal(LineNameList& aNameList,
                                     ImplicitNamedAreas*& aAreas);

  mozilla::IntrinsicISizesCache mCachedIntrinsicSizes;

  // Our baselines, one per BaselineSharingGroup per axis.
  PerLogicalAxis<PerBaseline<nscoord>> mBaseline;
};

#endif /* nsGridContainerFrame_h___ */
