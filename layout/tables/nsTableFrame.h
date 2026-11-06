/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsTableFrame_h_
#define nsTableFrame_h_

#include "TableArea.h"
#include "celldata.h"
#include "nsCellMap.h"
#include "nsContainerFrame.h"
#include "nsDisplayList.h"
#include "nscore.h"

struct BCPaintBorderAction;
class nsTableCellFrame;
class nsTableCellMap;
class nsTableColFrame;
class nsTableRowGroupFrame;
class nsTableRowFrame;
class nsTableColGroupFrame;
class nsITableLayoutStrategy;

namespace mozilla {
class LogicalMargin;
class PresShell;
class WritingMode;
struct TableBCData;
struct TableReflowInput;

namespace layers {
class StackingContextHelper;
}

// An input to nsTableFrame::ReflowTable() and TableReflowInput.
enum class TableReflowMode : uint8_t {
  // A reflow to measure the block-size of the table. We use this value to
  // request an unconstrained available block in the first reflow if a second
  // special block-size reflow is needed later.
  Measuring,

  // A final reflow with the available block-size in the table frame's
  // ReflowInput.
  Final,
};

class nsDisplayTableItem : public nsPaintedDisplayItem {
 public:
  nsDisplayTableItem(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {}

  // With collapsed borders, parts of the collapsed border can extend outside
  // the table part frames, so allow this display element to blow out to our
  // overflow rect. This is also useful for row frames that have spanning
  // cells extending outside them.
  nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap) const override;
};

class nsDisplayTableBackgroundSet {
 public:
  nsDisplayList* ColGroupBackgrounds() { return &mColGroupBackgrounds; }

  nsDisplayList* ColBackgrounds() { return &mColBackgrounds; }

  nsDisplayTableBackgroundSet(nsDisplayListBuilder* aBuilder, nsIFrame* aTable);

  ~nsDisplayTableBackgroundSet() {
    mozilla::DebugOnly<nsDisplayTableBackgroundSet*> result =
        mBuilder->SetTableBackgroundSet(mPrevTableBackgroundSet);
    MOZ_ASSERT(result == this);
  }

  /**
   * Move all display items in our lists to top of the corresponding lists in
   * the destination.
   */
  void MoveTo(const nsDisplayListSet& aDestination) {
    aDestination.BorderBackground()->AppendToTop(ColGroupBackgrounds());
    aDestination.BorderBackground()->AppendToTop(ColBackgrounds());
  }

  void AddColumn(nsTableColFrame* aFrame) { mColumns.AppendElement(aFrame); }

  nsTableColFrame* GetColForIndex(int32_t aIndex) { return mColumns[aIndex]; }

  const nsPoint& TableToReferenceFrame() { return mToReferenceFrame; }

  const nsRect& GetDirtyRect() { return mDirtyRect; }

  const DisplayItemClipChain* GetTableClipChain() {
    return mCombinedTableClipChain;
  }

  const ActiveScrolledRoot* GetTableASR() { return mTableASR; }

 private:
  // This class is only used on stack, so we don't have to worry about leaking
  // it.  Don't let us be heap-allocated!
  void* operator new(size_t sz) noexcept(true);

 protected:
  nsDisplayListBuilder* mBuilder;
  nsDisplayTableBackgroundSet* mPrevTableBackgroundSet;

  nsDisplayList mColGroupBackgrounds;
  nsDisplayList mColBackgrounds;

  nsTArray<nsTableColFrame*> mColumns;
  nsPoint mToReferenceFrame;
  nsRect mDirtyRect;

  const DisplayItemClipChain* mCombinedTableClipChain;
  const ActiveScrolledRoot* mTableASR;
};

}  // namespace mozilla

/* ========================================================================== */

enum nsTableColType {
  eColContent = 0,            // there is real col content associated
  eColAnonymousCol = 1,       // the result of a span on a col
  eColAnonymousColGroup = 2,  // the result of a span on a col group
  eColAnonymousCell = 3       // the result of a cell alone
};

/**
 * nsTableFrame maps the inner portion of a table (everything except captions.)
 * Used as a pseudo-frame within nsTableWrapperFrame, it may also be used
 * stand-alone as the top-level frame.
 *
 * The principal child list contains row group frames. There is also an
 * additional child list, FrameChildListID::ColGroup, which contains the col
 * group frames.
 */
class nsTableFrame : public nsContainerFrame {
  typedef mozilla::image::ImgDrawResult ImgDrawResult;
  typedef mozilla::WritingMode WritingMode;
  typedef mozilla::LogicalMargin LogicalMargin;

 public:
  NS_DECL_FRAMEARENA_HELPERS(nsTableFrame)

  using TablePartsArray = nsTArray<nsContainerFrame*>;
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(PositionedTablePartsProperty,
                                      TablePartsArray)

  /** nsTableWrapperFrame has intimate knowledge of the inner table frame */
  friend class nsTableWrapperFrame;

  using RowGroupArray = AutoTArray<nsTableRowGroupFrame*, 8>;
  // Multiple colgroups are a lot rarer than multiple rowgroups.
  using ColGroupArray = AutoTArray<nsTableColGroupFrame*, 2>;
  struct Groups {
    RowGroupArray mRowGroups;
    ColGroupArray mColGroups;
    nsTableRowGroupFrame* mHead = nullptr;
    nsTableRowGroupFrame* mFoot = nullptr;
  };

  /**
   * instantiate a new instance of nsTableRowFrame.
   *
   * @param aPresShell the pres shell for this frame
   *
   * @return           the frame that was created
   */
  friend nsTableFrame* NS_NewTableFrame(mozilla::PresShell* aPresShell,
                                        ComputedStyle* aStyle);

  /** sets defaults for table-specific style.
   * @see nsIFrame::Init
   */
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;

  // Return true if aParentReflowInput.frame or any of its ancestors within
  // the containing table have non-auto bsize. (e.g. pct or fixed bsize)
  static bool AncestorsHaveStyleBSize(const ReflowInput& aParentReflowInput);

  // See if a special bsize reflow will occur due to having a pct bsize when
  // the pct bsize basis may not yet be valid.
  static void CheckRequestSpecialBSizeReflow(const ReflowInput& aReflowInput);

  // Notify the frame and its ancestors (up to the containing table) that a
  // special height reflow will occur.
  static void RequestSpecialBSizeReflow(const ReflowInput& aReflowInput);

  static bool PageBreakAfter(nsIFrame* aSourceFrame, nsIFrame* aNextFrame);

  // Register or deregister a positioned table part with its nsTableFrame.
  // These objects will be visited by FixupPositionedTableParts after reflow is
  // complete. (See that function for more explanation.) Should be called
  // during style recalculation.
  static void PositionedTablePartMaybeChanged(
      nsContainerFrame*, mozilla::ComputedStyle* aOldStyle);

  // Unregister a positioned table part with its nsTableFrame, if needed.
  static void MaybeUnregisterPositionedTablePart(nsContainerFrame* aFrame);

  /*
   * Notification that rowspan or colspan has changed for content inside a
   * table cell
   */
  void RowOrColSpanChanged(nsTableCellFrame* aCellFrame);

  /** @see nsIFrame::DestroyFrom */
  void Destroy(DestroyContext&) override;

  /** @see nsIFrame::DidSetComputedStyle */
  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;

  nsMargin GetUsedBorder() const override;
  nsMargin GetUsedPadding() const override;
  nsMargin GetUsedMargin() const override;

  /** helper method to find the table parent of any table frame object */
  static nsTableFrame* GetTableFrame(nsIFrame* aSourceFrame);

  // Return the closest sibling of aPriorChildFrame (including aPriroChildFrame)
  // of type aChildType.
  static nsIFrame* GetFrameAtOrBefore(nsIFrame* aParentFrame,
                                      nsIFrame* aPriorChildFrame,
                                      mozilla::LayoutFrameType aChildType);
  bool IsAutoBSize(mozilla::WritingMode aWM);

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  // Gets the index of the last real (not synthetic) column that we have.
  int32_t GetRealColEnd() const;

  /** Get the outer half (i.e., the part outside the height and width of
   *  the table) of the largest segment (?) of border-collapsed border on
   *  the table on each side, or 0 for non border-collapsed tables.
   */
  LogicalMargin GetOuterBCBorder(const WritingMode aWM) const;

  /**
   * Emplace our border and padding in aBorder and aPadding if we are
   * border-collapsed. Otherwise, do nothing.
   */
  void GetCollapsedBorderPadding(
      mozilla::Maybe<mozilla::LogicalMargin>& aBorder,
      mozilla::Maybe<mozilla::LogicalMargin>& aPadding) const;

  friend class nsDelayedCalcBCBorders;

  void AddBCDamageArea(const mozilla::TableArea& aValue);
  bool BCRecalcNeeded(ComputedStyle* aOldComputedStyle,
                      ComputedStyle* aNewComputedStyle);
  void PaintBCBorders(DrawTarget& aDrawTarget, const nsRect& aDirtyRect);
  void CreateWebRenderCommandsForBCBorders(
      mozilla::wr::DisplayListBuilder& aBuilder,
      const mozilla::layers::StackingContextHelper& aSc,
      const nsRect& aVisibleRect, const nsPoint& aOffsetToReferenceFrame);

  void MarkIntrinsicISizesDirty() override;
  // For border-collapse tables, the caller must not add padding and
  // border to the results of these functions.
  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;
  IntrinsicSizeOffsetData IntrinsicISizeOffsets(
      nscoord aPercentageBasis = NS_UNCONSTRAINEDSIZE) override;

  SizeComputationResult ComputeSize(
      const SizeComputationInput& aSizingInput, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags) override;

  mozilla::LogicalSize ComputeAutoSize(
      const SizeComputationInput& aSizingInput, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags) override;

  /**
   * A copy of nsIFrame::ShrinkISizeToFit that calls a different
   * GetPrefISize, since tables have two different ones.
   */
  nscoord TableShrinkISizeToFit(gfxContext* aRenderingContext,
                                nscoord aWidthInCB);

  // XXXldb REWRITE THIS COMMENT!
  // clang-format off
  /**
   * Inner tables are reflowed in two steps.
   * <pre>
   * if mFirstPassValid is false, this is our first time through since content was last changed
   *   set pass to 1
   *   do pass 1
   *     get min/max info for all cells in an infinite space
   *   do column balancing
   *   set mFirstPassValid to true
   *   do pass 2
   *     use column widths to Reflow cells
   * </pre>
   *
   * @see nsIFrame::Reflow
   */
  // clang-format on
  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  void ReflowTable(ReflowOutput& aDesiredSize, const ReflowInput& aReflowInput,
                   const LogicalMargin& aBorderPadding,
                   mozilla::TableReflowMode aReflowMode, Groups& aGroups,
                   nsIFrame*& aLastChildReflowed, nsReflowStatus& aStatus);

  ComputedStyle* GetParentComputedStyle(
      nsIFrame** aProviderFrame) const override;

#ifdef DEBUG_FRAME_DUMP
  /** @see nsIFrame::GetFrameName */
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

  /** Return the isize of the column at aColIndex.
   *  This may only be called on the table's first-in-flow.
   */
  nscoord GetColumnISizeFromFirstInFlow(int32_t aColIndex);

  /** Helper to get the column spacing style value.
   *  The argument refers to the space between column aColIndex and column
   *  aColIndex + 1.  An index of -1 indicates the padding between the table
   *  and the left border, an index equal to the number of columns indicates
   *  the padding between the table and the right border.
   *
   *  Although in this class cell spacing does not depend on the index, it
   *  may be important for overriding classes.
   */
  virtual nscoord GetColSpacing(int32_t aColIndex);

  /** Helper to find the sum of the cell spacing between arbitrary columns.
   *  The argument refers to the space between column aColIndex and column
   *  aColIndex + 1.  An index of -1 indicates the padding between the table
   *  and the left border, an index equal to the number of columns indicates
   *  the padding between the table and the right border.
   *
   *  This method is equivalent to
   *  nscoord result = 0;
   *  for (i = aStartColIndex; i < aEndColIndex; i++) {
   *    result += GetColSpacing(i);
   *  }
   *  return result;
   */
  virtual nscoord GetColSpacing(int32_t aStartColIndex, int32_t aEndColIndex);

  /** Helper to get the row spacing style value.
   *  The argument refers to the space between row aRowIndex and row
   *  aRowIndex + 1.  An index of -1 indicates the padding between the table
   *  and the top border, an index equal to the number of rows indicates
   *  the padding between the table and the bottom border.
   *
   *  Although in this class cell spacing does not depend on the index, it
   *  may be important for overriding classes.
   */
  virtual nscoord GetRowSpacing(int32_t aRowIndex);

  /** Helper to find the sum of the cell spacing between arbitrary rows.
   *  The argument refers to the space between row aRowIndex and row
   *  aRowIndex + 1.  An index of -1 indicates the padding between the table
   *  and the top border, an index equal to the number of rows indicates
   *  the padding between the table and the bottom border.
   *
   *  This method is equivalent to
   *  nscoord result = 0;
   *  for (i = aStartRowIndex; i < aEndRowIndex; i++) {
   *    result += GetRowSpacing(i);
   *  }
   *  return result;
   */
  virtual nscoord GetRowSpacing(int32_t aStartRowIndex, int32_t aEndRowIndex);

 private:
  /* For the base implementation of nsTableFrame, cell spacing does not depend
   * on row/column indexing.
   */
  nscoord GetColSpacing();
  nscoord GetRowSpacing();

 public:
  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext) const override;

  /** return the row span of a cell, taking into account row span magic at the
   * bottom of a table. The row span equals the number of rows spanned by aCell
   * starting at aStartRowIndex, and can be smaller if aStartRowIndex is greater
   * than the row index in which aCell originates.
   *
   * @param aStartRowIndex the cell
   * @param aCell          the cell
   *
   * @return  the row span, correcting for row spans that extend beyond the
   * bottom of the table.
   */
  int32_t GetEffectiveRowSpan(int32_t aStartRowIndex,
                              const nsTableCellFrame& aCell) const;
  int32_t GetEffectiveRowSpan(const nsTableCellFrame& aCell,
                              nsCellMap* aCellMap = nullptr);

  /** return the col span of a cell, taking into account col span magic at the
   * edge of a table.
   *
   * @param aCell      the cell
   *
   * @return  the col span, correcting for col spans that extend beyond the edge
   *          of the table.
   */
  int32_t GetEffectiveColSpan(const nsTableCellFrame& aCell,
                              nsCellMap* aCellMap = nullptr) const;

  /** indicate whether the row has more than one cell that either originates
   * or is spanned from the rows above
   */
  bool HasMoreThanOneCell(int32_t aRowIndex) const;

  /** return the column frame associated with aColIndex
   * returns nullptr if the col frame has not yet been allocated, or if
   * aColIndex is out of range
   */
  nsTableColFrame* GetColFrame(int32_t aColIndex) const;

  /** Insert a col frame reference into the colframe cache and adapt the cellmap
   * @param aColFrame    - the column frame
   * @param aColIndex    - index where the column should be inserted into the
   *                       colframe cache
   */
  void InsertCol(nsTableColFrame& aColFrame, int32_t aColIndex);

  nsTableColGroupFrame* CreateSyntheticColGroupFrame();

  int32_t DestroyAnonymousColFrames(int32_t aNumFrames);

  // Append aNumColsToAdd anonymous col frames of type eColAnonymousCell to our
  // last synthetic colgroup.  If we have no such colgroup, then create one.
  void AppendAnonymousColFrames(int32_t aNumColsToAdd);

  // Append aNumColsToAdd anonymous col frames of type aColType to
  // aColGroupFrame.  If aAddToTable is true, also call AddColsToTable on the
  // new cols.
  void AppendAnonymousColFrames(nsTableColGroupFrame* aColGroupFrame,
                                int32_t aNumColsToAdd, nsTableColType aColType,
                                bool aAddToTable);

  void MatchCellMapToColCache(nsTableCellMap* aCellMap);

  void DidResizeColumns();

  void AppendCell(nsTableCellFrame& aCellFrame, int32_t aRowIndex);

  void InsertCells(nsTArray<nsTableCellFrame*>& aCellFrames, int32_t aRowIndex,
                   int32_t aColIndexBefore);

  void RemoveCell(nsTableCellFrame* aCellFrame, int32_t aRowIndex);

  void AppendRows(nsTableRowGroupFrame* aRowGroupFrame, int32_t aRowIndex,
                  nsTArray<nsTableRowFrame*>& aRowFrames);

  int32_t InsertRows(nsTableRowGroupFrame* aRowGroupFrame,
                     nsTArray<nsTableRowFrame*>& aFrames, int32_t aRowIndex,
                     bool aConsiderSpans);

  void RemoveRows(nsTableRowFrame& aFirstRowFrame, int32_t aNumRowsToRemove,
                  bool aConsiderSpans);

  /** Insert multiple rowgroups into the table cellmap handling
   * @param aRowGroups - iterator that iterates over the rowgroups to insert
   */
  void InsertRowGroups(const nsFrameList::Slice& aNewFrames);
  void InsertColGroups(int32_t aStartColIndex,
                       const nsFrameList::Slice& aNewFrames);

  void RemoveCol(int32_t aColIndex, bool aRemoveFromCache,
                 bool aRemoveFromCellMap);

  bool ColumnHasCellSpacingBefore(int32_t aColIndex) const;

  bool HasPctCol() const;
  void SetHasPctCol(bool aValue);

  bool HasCellSpanningPctCol() const;
  void SetHasCellSpanningPctCol(bool aValue);

  /**
   * To be called on a frame by its parent after setting its size/position and
   * calling DidReflow (possibly via FinishReflowChild()).  This can also be
   * used for child frames which are not being reflowed but did have their size
   * or position changed.
   *
   * @param aFrame The frame to invalidate
   * @param aOrigRect The original rect of aFrame (before the change).
   * @param aOrigInkOverflow The original overflow rect of aFrame.
   * @param aIsFirstReflow True if the size/position change is due to the
   *                       first reflow of aFrame.
   */
  static void InvalidateTableFrame(nsIFrame* aFrame, const nsRect& aOrigRect,
                                   const nsRect& aOrigInkOverflow,
                                   bool aIsFirstReflow);

  void UnionChildOverflow(mozilla::OverflowAreas&,
                          bool aAsIfScrolled = false) final;

  // Return our wrapper frame.
  void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) override;

 protected:
  static void UpdateStyleOfOwnedAnonBoxesForTableWrapper(
      nsIFrame* aOwningFrame, nsIFrame* aWrapperFrame,
      mozilla::ServoRestyleState& aRestyleState);

  /** protected constructor.
   * @see NewFrame
   */
  explicit nsTableFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                        ClassID aID = kClassID);

  virtual ~nsTableFrame();

  void InitChildReflowInput(ReflowInput& aReflowInput);

  LogicalSides GetLogicalSkipSides() const override;

  void IterateBCBorders(BCPaintBorderAction& aAction, const nsRect& aDirtyRect);

 public:
  bool IsRowInserted() const;
  void SetRowInserted(bool aValue);

 protected:
  // A helper function to reflow a header or footer with unconstrained
  // block-size to see if it should be made repeatable.
  // @return the desired block-size for a header or footer.
  nscoord SetupHeaderFooterChild(const mozilla::TableReflowInput& aReflowInput,
                                 nsTableRowGroupFrame* aFrame);

  void ReflowChildren(mozilla::TableReflowInput& aReflowInput,
                      nsReflowStatus& aStatus, Groups& aGroups,
                      nsIFrame*& aLastChildReflowed,
                      mozilla::OverflowAreas& aOverflowAreas);

  /** return the isize of the table taking into account visibility collapse
   * on columns and colgroups
   * @param aBorderPadding  the border and padding of the table
   */
  nscoord GetCollapsedISize(const WritingMode aWM,
                            const LogicalMargin& aBorderPadding);

  /** Adjust the table for visibility.collapse set on rowgroups, rows,
   * colgroups and cols
   * @param aDesiredSize    the metrics of the table
   * @param aBorderPadding  the border and padding of the table
   */
  void AdjustForCollapsingRowsCols(
      ReflowOutput& aDesiredSize, const WritingMode aWM,
      const nsTArray<nsTableRowGroupFrame*>& aRowGroups,
      const LogicalMargin& aBorderPadding);

  /** FixupPositionedTableParts is called at the end of table reflow to reflow
   *  the absolutely positioned descendants of positioned table parts. This is
   *  necessary because the dimensions of table parts may change after they've
   *  been reflowed (e.g. in AdjustForCollapsingRowsCols).
   */
  void FixupPositionedTableParts(nsPresContext* aPresContext,
                                 ReflowOutput& aDesiredSize,
                                 const ReflowInput& aReflowInput);

  nsITableLayoutStrategy* LayoutStrategy() const {
    return FirstInFlowAsTable()->mTableLayoutStrategy.get();
  }

 private:
  /* Handle a row that got inserted during reflow.  aNewHeight is the
     new height of the table after reflow. */
  void ProcessRowInserted(nscoord aNewHeight);

 protected:
  // Calculate the border-box block-size of this table, with the min-block-size,
  // max-block-size, and intrinsic border-box block considered.
  nscoord CalcBorderBoxBSize(const ReflowInput& aReflowInput,
                             const LogicalMargin& aBorderPadding,
                             nscoord aIntrinsicBorderBoxBSize);

  // Calculate the desired block-size of this table.
  //
  // Note: this method is accurate after the children are reflowed. It might
  // distribute extra block-size to table rows if the table has a specified
  // block-size larger than the intrinsic block-size.
  nscoord CalcDesiredBSize(const ReflowInput& aReflowInput,
                           const LogicalMargin& aBorderPadding,
                           const nsReflowStatus& aStatus);

  // The following is a helper for CalcDesiredBSize
  void DistributeBSizeToRows(const ReflowInput& aReflowInput, nscoord aAmount);

  void PlaceChild(mozilla::TableReflowInput& aReflowInput, nsIFrame* aKidFrame,
                  const ReflowInput& aKidReflowInput,
                  const mozilla::LogicalPoint& aKidPosition,
                  const nsSize& aContainerSize, ReflowOutput& aKidDesiredSize,
                  const nsRect& aOriginalKidRect,
                  const nsRect& aOriginalKidInkOverflow);
  void PlaceRepeatedFooter(mozilla::TableReflowInput& aReflowInput,
                           nsTableRowGroupFrame* aTfoot, nscoord aFooterBSize);

  // Push all our non-repeatable child frames from the aRowGroups array, in
  // order, starting from the frame at aPushFrom to the end of the array. The
  // pushed frames are put on our overflow list. This is a table specific
  // version that takes into account repeated header and footer frames when
  // continuing table frames.
  void PushChildrenToOverflow(const RowGroupArray& aRowGroups,
                              size_t aPushFrom);

 public:
  // Return the children frames in the display order (e.g. thead before tbodies
  // before tfoot). If there are multiple theads or tfoots, all but the first
  // one are treated as tbodies instead.
  Groups OrderedGroups() const;
  RowGroupArray OrderedRowGroups() const { return OrderedGroups().mRowGroups; }

  // Returns true if there are any cells above the row at
  // aRowIndex and spanning into the row at aRowIndex, the number of
  // effective columns limits the search up to that column
  bool RowIsSpannedInto(int32_t aRowIndex, int32_t aNumEffCols);

  // Returns true if there is a cell originating in aRowIndex
  // which spans into the next row,  the number of effective
  // columns limits the search up to that column
  bool RowHasSpanningCells(int32_t aRowIndex, int32_t aNumEffCols);

 public:
  bool IsBorderCollapse() const;

  bool NeedToCalcBCBorders() const;
  void SetNeedToCalcBCBorders(bool aValue);

  bool NeedToCollapse() const;
  void SetNeedToCollapse(bool aValue);

  bool NeedToCalcHasBCBorders() const;
  void SetNeedToCalcHasBCBorders(bool aValue);

  void CalcHasBCBorders();
  bool HasBCBorders();
  void SetHasBCBorders(bool aValue);

  /** The GeometryDirty bit is similar to the NS_FRAME_IS_DIRTY frame
   * state bit, which implies that all descendants are dirty.  The
   * GeometryDirty still implies that all the parts of the table are
   * dirty, but resizing optimizations should still apply to the
   * contents of the individual cells.
   */
  void SetGeometryDirty() { mBits.mGeometryDirty = true; }
  void ClearGeometryDirty() { mBits.mGeometryDirty = false; }
  bool IsGeometryDirty() const { return mBits.mGeometryDirty; }

  /** Get the cell map for this table frame.  It is not always mCellMap.
   * Only the firstInFlow has a legit cell map
   */
  nsTableCellMap* GetCellMap() const;

  /** Iterate over the row groups and adjust the row indices of all rows
   * whose index is >= aRowIndex.
   * @param aRowIndex   - start adjusting with this index
   * @param aAdjustment - shift the row index by this amount
   */
  void AdjustRowIndices(int32_t aRowIndex, int32_t aAdjustment);

  /** Reset the rowindices of all rows as they might have changed due to
   * rowgroup reordering, exclude new row group frames that show in the
   * reordering but are not yet inserted into the cellmap
   * @param aRowGroupsToExclude - an iterator that will produce the row groups
   *                              to exclude.
   */
  void ResetRowIndices(const nsFrameList::Slice& aRowGroupsToExclude);

  nsTArray<nsTableColFrame*>& GetColCache();

  mozilla::TableBCData* GetTableBCData() const;

 protected:
  void SetBorderCollapse(bool aValue);

  void SetFullBCDamageArea();
  void CalcBCBorders();

  void ExpandBCDamageArea(mozilla::TableArea& aRect) const;

  void SetColumnDimensions(nscoord aHeight, WritingMode aWM,
                           const LogicalMargin& aBorderPadding,
                           const nsSize& aContainerSize);

  int32_t CollectRows(nsIFrame* aFrame,
                      nsTArray<nsTableRowFrame*>& aCollection);

 public: /* ----- Cell Map public methods ----- */
  int32_t GetStartRowIndex(const nsTableRowGroupFrame* aRowGroupFrame) const;

  /** returns the number of rows in this table.
   */
  int32_t GetRowCount() const { return GetCellMap()->GetRowCount(); }

  /** returns the number of columns in this table after redundant columns have
   * been removed
   */
  int32_t GetEffectiveColCount() const;

  /* return the col count including dead cols */
  int32_t GetColCount() const { return GetCellMap()->GetColCount(); }

  // return the last col index which isn't of type eColAnonymousCell
  int32_t GetIndexOfLastRealCol();

  /** returns true if table-layout:auto  */
  bool IsAutoLayout();

 public:
  /* ---------- Row index management methods ------------ */

  /** Add the given index to the existing ranges of
   *  deleted row indices and merge ranges if, with the addition of the new
   *  index, they become consecutive.
   *  @param aDeletedRowStoredIndex - index of the row that was deleted
   *  Note - 'stored' index here refers to the index that was assigned to
   *  the row before any remove row operations were performed i.e. the
   *  value of mRowIndex and not the value returned by GetRowIndex()
   */
  void AddDeletedRowIndex(int32_t aDeletedRowStoredIndex);

  nsTableFrame* FirstInFlowAsTable() const {
    return static_cast<nsTableFrame*>(FirstInFlow());
  }

  /** Calculate the change that aStoredIndex must be increased/decreased by
   *  to get new index.
   *  Note that aStoredIndex is always the index of an undeleted row (since
   *  rows that have already been deleted can never call this method).
   *  @param aStoredIndex - The stored index value that must be adjusted
   *  Note - 'stored' index here refers to the index that was assigned to
   *  the row before any remove row operations were performed i.e. the
   *  value of mRowIndex and not the value returned by GetRowIndex()
   */
  int32_t GetAdjustmentForStoredIndex(int32_t aStoredIndex);

  /** Returns whether mDeletedRowIndexRanges is empty
   */
  bool IsDeletedRowIndexRangesEmpty() const {
    return FirstInFlowAsTable()->mDeletedRowIndexRanges.empty();
  }

  bool IsDestroying() const { return mBits.mIsDestroying; }

  nsTableColGroupFrame* GetSyntheticColGroup() const {
    return mSyntheticColGroup;
  }

 public:
#ifdef DEBUG
  void Dump(bool aDumpRows, bool aDumpCols, bool aDumpCellMap);
  void VerifyColFrames();
#endif

 protected:
  /**
   * Helper method for RemoveFrame.
   */
  void DoRemoveFrame(DestroyContext&, ChildListID, nsIFrame*);
#ifdef DEBUG
  void DumpRowGroup(nsIFrame* aChildFrame);
#endif
  // DATA MEMBERS
  AutoTArray<nsTableColFrame*, 8> mColFrames;
  nsTableColGroupFrame* mSyntheticColGroup = nullptr;

  struct TableBits {
    uint32_t mHasPctCol : 1;        // does any cell or col have a pct width
    uint32_t mCellSpansPctCol : 1;  // does any cell span a col with a pct width
                                    // (or containing a cell with a pct width)
    uint32_t mIsBorderCollapse : 1;  // border collapsing model vs. separate
                                     // model
    uint32_t mRowInserted : 1;
    uint32_t mNeedToCalcBCBorders : 1;
    uint32_t mGeometryDirty : 1;
    uint32_t mNeedToCollapse : 1;  // rows, cols that have visibility:collapse
                                   // need to be collapsed
    uint32_t mResizedColumns : 1;  // have we resized columns since last reflow?
    uint32_t mNeedToCalcHasBCBorders : 1;
    uint32_t mHasBCBorders : 1;
    uint32_t mIsDestroying : 1;  // Whether we're in the process of destroying
                                 // this table frame.
  } mBits;

  std::map<int32_t, int32_t> mDeletedRowIndexRanges;  // maintains ranges of row
                                                      // indices of deleted rows
  mozilla::UniquePtr<nsTableCellMap> mCellMap;  // maintains the relationships
                                                // between rows, cols, and cells
  // the layout strategy for this frame
  mozilla::UniquePtr<nsITableLayoutStrategy> mTableLayoutStrategy;
};

inline bool nsTableFrame::HasPctCol() const { return (bool)mBits.mHasPctCol; }

inline void nsTableFrame::SetHasPctCol(bool aValue) {
  mBits.mHasPctCol = (unsigned)aValue;
}

inline bool nsTableFrame::HasCellSpanningPctCol() const {
  return (bool)mBits.mCellSpansPctCol;
}

inline void nsTableFrame::SetHasCellSpanningPctCol(bool aValue) {
  mBits.mCellSpansPctCol = (unsigned)aValue;
}

inline bool nsTableFrame::IsRowInserted() const {
  return (bool)mBits.mRowInserted;
}

inline void nsTableFrame::SetRowInserted(bool aValue) {
  mBits.mRowInserted = (unsigned)aValue;
}

inline void nsTableFrame::SetNeedToCollapse(bool aValue) {
  FirstInFlowAsTable()->mBits.mNeedToCollapse = (unsigned)aValue;
}

inline bool nsTableFrame::NeedToCollapse() const {
  return (bool)FirstInFlowAsTable()->mBits.mNeedToCollapse;
}

inline nsTArray<nsTableColFrame*>& nsTableFrame::GetColCache() {
  return mColFrames;
}

inline bool nsTableFrame::IsBorderCollapse() const {
  return (bool)mBits.mIsBorderCollapse;
}

inline void nsTableFrame::SetBorderCollapse(bool aValue) {
  mBits.mIsBorderCollapse = aValue;
}

inline bool nsTableFrame::NeedToCalcBCBorders() const {
  return (bool)mBits.mNeedToCalcBCBorders;
}

inline void nsTableFrame::SetNeedToCalcBCBorders(bool aValue) {
  mBits.mNeedToCalcBCBorders = (unsigned)aValue;
}

inline bool nsTableFrame::NeedToCalcHasBCBorders() const {
  return (bool)mBits.mNeedToCalcHasBCBorders;
}

inline void nsTableFrame::SetNeedToCalcHasBCBorders(bool aValue) {
  mBits.mNeedToCalcHasBCBorders = (unsigned)aValue;
}

inline bool nsTableFrame::HasBCBorders() {
  if (NeedToCalcHasBCBorders()) {
    CalcHasBCBorders();
    SetNeedToCalcHasBCBorders(false);
  }
  return (bool)mBits.mHasBCBorders;
}

inline void nsTableFrame::SetHasBCBorders(bool aValue) {
  mBits.mHasBCBorders = (unsigned)aValue;
}

#define ABORT0()                                       \
  {                                                    \
    NS_ASSERTION(false, "CellIterator program error"); \
    return;                                            \
  }

#define ABORT1(aReturn)                                \
  {                                                    \
    NS_ASSERTION(false, "CellIterator program error"); \
    return aReturn;                                    \
  }

#endif
