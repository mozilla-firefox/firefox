/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsTableRowFrame_h_
#define nsTableRowFrame_h_

#include "mozilla/WritingModes.h"
#include "nsContainerFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nscore.h"

class nsTableCellFrame;
namespace mozilla {
class PresShell;
struct TableCellReflowInput;

// Yes if table-cells should use 'vertical-align:top' in
// nsTableCellFrame::BlockDirAlignChild(). This is a hack to workaround our
// current table row group fragmentation to avoid data loss.
enum class ForceAlignTopForTableCell : uint8_t { No, Yes };
}  // namespace mozilla

/**
 * nsTableRowFrame is the frame that maps table rows
 * (HTML tag TR). This class cannot be reused
 * outside of an nsTableRowGroupFrame.  It assumes that its parent is an
 * nsTableRowGroupFrame, and its children are nsTableCellFrames.
 *
 * @see nsTableFrame
 * @see nsTableRowGroupFrame
 * @see nsTableCellFrame
 */
class nsTableRowFrame : public nsContainerFrame {
  using TableCellReflowInput = mozilla::TableCellReflowInput;

 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsTableRowFrame)

  virtual ~nsTableRowFrame();

  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;

  void Destroy(DestroyContext&) override;

  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;

  /** instantiate a new instance of nsTableRowFrame.
   * @param aPresShell the pres shell for this frame
   *
   * @return           the frame that was created
   */
  friend nsTableRowFrame* NS_NewTableRowFrame(mozilla::PresShell* aPresShell,
                                              ComputedStyle* aStyle);

  nsTableRowGroupFrame* GetTableRowGroupFrame() const {
    nsIFrame* parent = GetParent();
    MOZ_ASSERT(parent && parent->IsTableRowGroupFrame());
    return static_cast<nsTableRowGroupFrame*>(parent);
  }

  nsTableFrame* GetTableFrame() const {
    return GetTableRowGroupFrame()->GetTableFrame();
  }

  nsMargin GetUsedMargin() const override;
  nsMargin GetUsedBorder() const override;
  nsMargin GetUsedPadding() const override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  void PaintCellBackgroundsForFrame(nsIFrame* aFrame,
                                    nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists,
                                    const nsPoint& aOffset = nsPoint());

  // Implemented in nsTableCellFrame.h, because it needs to know about the
  // nsTableCellFrame class, but we can't include nsTableCellFrame.h here.
  inline nsTableCellFrame* GetFirstCell() const;

  /** calls Reflow for all of its child cells.
   *
   * Cells with rowspan=1 are all set to the same height and stacked
   * horizontally.
   *
   * Cells are not split unless absolutely necessary.
   *
   * Cells are resized in nsTableFrame::BalanceColumnWidths and
   * nsTableFrame::ShrinkWrapChildren
   *
   * @param aDesiredSize width set to width of the sum of the cells,
   *                     height set to height of cells with rowspan=1.
   *
   * @see nsIFrame::Reflow
   * @see nsTableFrame::BalanceColumnWidths
   * @see nsTableFrame::ShrinkWrapChildren
   */
  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  void DidResize(mozilla::ForceAlignTopForTableCell aForceAlignTop =
                     mozilla::ForceAlignTopForTableCell::No);

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

  void UpdateBSize(nscoord aBSize, nsTableFrame* aTableFrame,
                   nsTableCellFrame* aCellFrame);

  void ResetBSize();

  // calculate the bsize, considering content bsize of the
  // cells and the style bsize of the row and cells, excluding pct bsizes
  nscoord CalcBSize(const ReflowInput& aReflowInput);

  // Support for cells with 'vertical-align: baseline'.

  /**
   * returns the max-ascent amongst all the cells that have
   * 'vertical-align: baseline', *including* cells with rowspans.
   * returns 0 if we don't have any cell with 'vertical-align: baseline'
   */
  nscoord GetMaxCellAscent() const;

  /* return the row ascent
   */
  Maybe<nscoord> GetRowBaseline(mozilla::WritingMode aWM);

  /** returns the ordinal position of this row in its table */
  virtual int32_t GetRowIndex() const;

  /** set this row's starting row index */
  void SetRowIndex(int aRowIndex);

  // See nsTableFrame.h
  int32_t GetAdjustmentForStoredIndex(int32_t aStoredIndex) const;

  // See nsTableFrame.h
  void AddDeletedRowIndex();

  /**
   * This function is called by the row group frame's SplitRowGroup() code when
   * pushing a row frame that has cell frames that span into it. The cell frame
   * should be reflowed with the specified available block-size.
   */
  nscoord ReflowCellFrame(nsPresContext* aPresContext,
                          const ReflowInput& aReflowInput, bool aIsTopOfPage,
                          nsTableCellFrame* aCellFrame, nscoord aAvailableBSize,
                          nsReflowStatus& aStatus);
  /**
   * Collapse the row if required, apply col and colgroup visibility: collapse
   * info to the cells in the row.
   * @return the amount to shift bstart-wards all following rows
   * @param aRowOffset     - shift the row bstart-wards by this amount
   * @param aISize         - new isize of the row
   * @param aCollapseGroup - parent rowgroup is collapsed so this row needs
   *                         to be collapsed
   * @param aDidCollapse   - the row has been collapsed
   */
  nscoord CollapseRowIfNecessary(nscoord aRowOffset, nscoord aISize,
                                 bool aCollapseGroup, bool& aDidCollapse);

  /**
   * Insert a cell frame after the last cell frame that has a col index
   * that is less than aColIndex.  If no such cell frame is found the
   * frame to insert is prepended to the child list.
   * @param aFrame the cell frame to insert
   * @param aColIndex the col index
   */
  void InsertCellFrame(nsTableCellFrame* aFrame, int32_t aColIndex);

  /**
   * Calculate the cell frame's actual block-size given its desired block-size
   * (the border-box block-size in the last reflow). This method takes into
   * account the specified bsize (in the style).
   *
   * @return the specified block-size if it is larger than the desired
   *         block-size. Otherwise, the desired block-size.
   */
  nscoord CalcCellActualBSize(nsTableCellFrame* aCellFrame,
                              const nscoord& aDesiredBSize,
                              mozilla::WritingMode aWM);

  bool IsFirstInserted() const;
  void SetFirstInserted(bool aValue);

  nscoord GetContentBSize() const;
  void SetContentBSize(nscoord aTwipValue);

  bool HasStyleBSize() const;

  bool HasFixedBSize() const;
  void SetHasFixedBSize(bool aValue);

  bool HasPctBSize() const;
  void SetHasPctBSize(bool aValue);

  nscoord GetFixedBSize() const;
  void SetFixedBSize(nscoord aValue);

  float GetPctBSize() const;
  void SetPctBSize(float aPctValue, bool aForce = false);

  nscoord GetInitialBSize(nscoord aBasis = 0) const;

  nsTableRowFrame* GetPrevRow() const;
  nsTableRowFrame* GetNextRow() const;
  // Like GetNextRow(), but continues into the next-in-flows of our row group.
  nsTableRowFrame* GetNextRowAcrossFragments() const;

  bool HasUnpaginatedBSize() const {
    return HasAnyStateBits(NS_TABLE_ROW_HAS_UNPAGINATED_BSIZE);
  }
  nscoord GetUnpaginatedBSize() const;
  void SetUnpaginatedBSize(nscoord aValue);

  nscoord GetBStartBCBorderWidth() const { return mBStartBorderWidth; }
  nscoord GetBEndBCBorderWidth() const { return mBEndBorderWidth; }
  void SetBStartBCBorderWidth(nscoord aWidth) { mBStartBorderWidth = aWidth; }
  void SetBEndBCBorderWidth(nscoord aWidth) { mBEndBorderWidth = aWidth; }
  mozilla::LogicalMargin GetBCBorderWidth(mozilla::WritingMode aWM);

  void InvalidateFrame(uint32_t aDisplayItemKey = 0,
                       bool aRebuildDisplayItems = true) override;
  void InvalidateFrameWithRect(const nsRect& aRect,
                               uint32_t aDisplayItemKey = 0,
                               bool aRebuildDisplayItems = true) override;
  void InvalidateFrameForRemoval() override { InvalidateFrameSubtree(); }

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() override;
#endif

 protected:
  /** protected constructor.
   * @see NewFrame
   */
  explicit nsTableRowFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                           ClassID aID = kClassID);

  void InitChildReflowInput(nsPresContext& aPresContext,
                            const mozilla::LogicalSize& aAvailSize,
                            bool aBorderCollapse,
                            TableCellReflowInput& aReflowInput);

  LogicalSides GetLogicalSkipSides() const override;

  /**
   * Called for incremental/dirty and resize reflows. If aDirtyOnly is true then
   * only reflow dirty cells.
   */
  void ReflowChildren(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
                      const ReflowInput& aReflowInput,
                      nsTableFrame& aTableFrame, nsReflowStatus& aStatus);

 private:
  struct RowBits {
    unsigned mRowIndex : 29;
    unsigned mHasFixedBSize : 1;  // set if the dominating style bsize on the
                                  // row or any cell is pixel based
    unsigned mHasPctBSize : 1;  // set if the dominating style bsize on the row
                                // or any cell is pct based
    unsigned mFirstInserted : 1;  // if true, then it was the bstart-most newly
                                  // inserted row
  } mBits;

  // the desired bsize based on the content of the tallest cell in the row
  nscoord mContentBSize = 0;
  // the bsize based on a style percentage bsize on either the row or any cell
  // if mHasPctBSize is set
  nscoord mStylePctBSize = 0;
  // the bsize based on a style pixel bsize on the row or any
  // cell if mHasFixedBSize is set
  nscoord mStyleFixedBSize = 0;

  // max-ascent and max-descent amongst all cells that have
  // 'vertical-align: baseline'
  nscoord mMaxCellAscent = 0;   // does include cells with rowspan > 1
  nscoord mMaxCellDescent = 0;  // does *not* include cells with rowspan > 1

  // border widths in the collapsing border model of the *inner*
  // half of the border only
  nscoord mBStartBorderWidth = 0;
  nscoord mBEndBorderWidth = 0;

  /**
   * Sets the NS_ROW_HAS_CELL_WITH_STYLE_BSIZE bit to indicate whether
   * this row has any cells that have non-auto-bsize.  (Row-spanning
   * cells are ignored.)
   */
  void InitHasCellWithStyleBSize(nsTableFrame* aTableFrame);
};

inline int32_t nsTableRowFrame::GetAdjustmentForStoredIndex(
    int32_t aStoredIndex) const {
  nsTableRowGroupFrame* parentFrame = GetTableRowGroupFrame();
  return parentFrame->GetAdjustmentForStoredIndex(aStoredIndex);
}

inline void nsTableRowFrame::AddDeletedRowIndex() {
  nsTableRowGroupFrame* parentFrame = GetTableRowGroupFrame();
  parentFrame->AddDeletedRowIndex(int32_t(mBits.mRowIndex));
}

inline int32_t nsTableRowFrame::GetRowIndex() const {
  int32_t storedRowIndex = int32_t(mBits.mRowIndex);
  int32_t rowIndexAdjustment = GetAdjustmentForStoredIndex(storedRowIndex);
  return (storedRowIndex - rowIndexAdjustment);
}

inline void nsTableRowFrame::SetRowIndex(int aRowIndex) {
  // Note: Setting the index of a row (as in the case of adding new rows) should
  // be preceded by a call to nsTableFrame::RecalculateRowIndices()
  // so as to correctly clear mDeletedRowIndexRanges.
  MOZ_ASSERT(
      GetTableRowGroupFrame()->GetTableFrame()->IsDeletedRowIndexRangesEmpty(),
      "mDeletedRowIndexRanges should be empty here!");
  mBits.mRowIndex = aRowIndex;
}

inline bool nsTableRowFrame::IsFirstInserted() const {
  return bool(mBits.mFirstInserted);
}

inline void nsTableRowFrame::SetFirstInserted(bool aValue) {
  mBits.mFirstInserted = aValue;
}

inline bool nsTableRowFrame::HasStyleBSize() const {
  return (bool)mBits.mHasFixedBSize || (bool)mBits.mHasPctBSize;
}

inline bool nsTableRowFrame::HasFixedBSize() const {
  return (bool)mBits.mHasFixedBSize;
}

inline void nsTableRowFrame::SetHasFixedBSize(bool aValue) {
  mBits.mHasFixedBSize = aValue;
}

inline bool nsTableRowFrame::HasPctBSize() const {
  return (bool)mBits.mHasPctBSize;
}

inline void nsTableRowFrame::SetHasPctBSize(bool aValue) {
  mBits.mHasPctBSize = aValue;
}

inline nscoord nsTableRowFrame::GetContentBSize() const {
  return mContentBSize;
}

inline void nsTableRowFrame::SetContentBSize(nscoord aValue) {
  mContentBSize = aValue;
}

inline nscoord nsTableRowFrame::GetFixedBSize() const {
  if (mBits.mHasFixedBSize) {
    return mStyleFixedBSize;
  }
  return 0;
}

inline float nsTableRowFrame::GetPctBSize() const {
  if (mBits.mHasPctBSize) {
    return (float)mStylePctBSize / 100.0f;
  }
  return 0.0f;
}

inline mozilla::LogicalMargin nsTableRowFrame::GetBCBorderWidth(
    mozilla::WritingMode aWM) {
  return mozilla::LogicalMargin(aWM, mBStartBorderWidth, 0, mBEndBorderWidth,
                                0);
}

#endif
