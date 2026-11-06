/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsCellMap_h_
#define nsCellMap_h_

#include <algorithm>

#include "TableArea.h"
#include "celldata.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsRect.h"
#include "nsTArray.h"
#include "nscore.h"

#undef DEBUG_TABLE_CELLMAP

class nsTableCellFrame;
class nsTableRowFrame;
class nsTableRowGroupFrame;
class nsTableFrame;
class nsCellMap;
class nsPresContext;
class nsCellMapColumnIterator;

struct nsColInfo {
  int32_t mNumCellsOrig;  // number of cells originating in the col
  int32_t mNumCellsSpan;  // number of cells spanning into the col via colspans
                          // (not rowspans)

  nsColInfo();
  nsColInfo(int32_t aNumCellsOrig, int32_t aNumCellsSpan);
};

struct BCInfo {
  nsTArray<BCData> mIEndBorders;
  nsTArray<BCData> mBEndBorders;
  BCData mBEndIEndCorner;
};

class nsTableCellMap {
  typedef mozilla::TableArea TableArea;

 public:
  nsTableCellMap(nsTableFrame& aTableFrame, bool aBorderCollapse);

  /** destructor
   * NOT VIRTUAL BECAUSE THIS CLASS SHOULD **NEVER** BE SUBCLASSED
   */
  ~nsTableCellMap();

  void RemoveGroupCellMap(nsTableRowGroupFrame* aRowGroup);

  void InsertGroupCellMap(nsTableRowGroupFrame* aNewRowGroup,
                          nsTableRowGroupFrame*& aPrevRowGroup);

  /**
   * Get the nsCellMap for the given row group.  If aStartHint is non-null,
   * will start looking with that cellmap and only fall back to starting at the
   * beginning of the list if that doesn't find us the right nsCellMap.
   * Otherwise, just start at the beginning.
   *
   * aRowGroup must not be null.
   */
  nsCellMap* GetMapFor(const nsTableRowGroupFrame* aRowGroup,
                       nsCellMap* aStartHint) const;

  /** synchronize the cellmaps with the rowgroups again **/
  void Synchronize(nsTableFrame* aTableFrame);

  nsTableCellFrame* GetCellFrame(int32_t aRowIndex, int32_t aColIndex,
                                 CellData& aData, bool aUseRowIfOverlap) const;

  /** return the CellData for the cell at (aRowIndex, aColIndex) */
  CellData* GetDataAt(int32_t aRowIndex, int32_t aColIndex) const;

  // this function creates a col if needed
  nsColInfo* GetColInfoAt(int32_t aColIndex);

  /** append the cellFrame at the end of the row at aRowIndex and return the col
   * index
   */
  CellData* AppendCell(nsTableCellFrame& aCellFrame, int32_t aRowIndex,
                       bool aRebuildIfNecessary, TableArea& aDamageArea);

  void InsertCells(nsTArray<nsTableCellFrame*>& aCellFrames, int32_t aRowIndex,
                   int32_t aColIndexBefore, TableArea& aDamageArea);

  void RemoveCell(nsTableCellFrame* aCellFrame, int32_t aRowIndex,
                  TableArea& aDamageArea);
  /** Remove the previously gathered column information */
  void ClearCols();
  void InsertRows(nsTableRowGroupFrame* aRowGroup,
                  nsTArray<nsTableRowFrame*>& aRows, int32_t aFirstRowIndex,
                  bool aConsiderSpans, TableArea& aDamageArea);

  void RemoveRows(int32_t aFirstRowIndex, int32_t aNumRowsToRemove,
                  bool aConsiderSpans, TableArea& aDamageArea);

  int32_t GetNumCellsOriginatingInRow(int32_t aRowIndex) const;
  int32_t GetNumCellsOriginatingInCol(int32_t aColIndex) const;

  /** indicate whether the row has more than one cell that either originates
   * or is spanned from the rows above
   */
  bool HasMoreThanOneCell(int32_t aRowIndex) const;

  int32_t GetEffectiveRowSpan(int32_t aRowIndex, int32_t aColIndex) const;
  int32_t GetEffectiveColSpan(int32_t aRowIndex, int32_t aColIndex) const;

  /** return the total number of columns in the table represented by this
   * CellMap */
  int32_t GetColCount() const;

  /** return the actual number of rows in the table represented by this CellMap
   */
  int32_t GetRowCount() const;

  nsTableCellFrame* GetCellInfoAt(int32_t aRowX, int32_t aColX,
                                  bool* aOriginates = nullptr,
                                  int32_t* aColSpan = nullptr) const;

  /**
   * Returns the index at the given row and column coordinates.
   *
   * @see  nsITableLayout::GetIndexByRowAndColumn()
   *
   * @param aRow     [in] the row coordinate
   * @param aColumn  [in] the column coordinate
   * @returns             the index for the cell
   */
  int32_t GetIndexByRowAndColumn(int32_t aRow, int32_t aColumn) const;

  /**
   * Retrieves the row and column coordinates for the given index.
   *
   * @see  nsITableLayout::GetRowAndColumnByIndex()
   *
   * @param aIndex  [in] the index for which coordinates are to be retrieved
   * @param aRow    [out] the row coordinate to be returned
   * @param aColumn [out] the column coordinate to be returned
   */
  void GetRowAndColumnByIndex(int32_t aIndex, int32_t* aRow,
                              int32_t* aColumn) const;

  void AddColsAtEnd(uint32_t aNumCols);
  void RemoveColsAtEnd();

  bool RowIsSpannedInto(int32_t aRowIndex, int32_t aNumEffCols) const;
  bool RowHasSpanningCells(int32_t aRowIndex, int32_t aNumEffCols) const;
  void RebuildConsideringCells(nsCellMap* aCellMap,
                               nsTArray<nsTableCellFrame*>* aCellFrames,
                               int32_t aRowIndex, int32_t aColIndex,
                               bool aInsert, TableArea& aDamageArea);

 protected:
  /**
   * Rebuild due to rows being inserted or deleted with cells spanning
   * into or out of the rows.  This function can only handle insertion
   * or deletion but NOT both.  So either aRowsToInsert must be null
   * or aNumRowsToRemove must be 0.
   *
   * // XXXbz are both allowed to happen?  That'd be a no-op...
   */
  void RebuildConsideringRows(nsCellMap* aCellMap, int32_t aStartRowIndex,
                              nsTArray<nsTableRowFrame*>* aRowsToInsert,
                              int32_t aNumRowsToRemove, TableArea& aDamageArea);

 public:
  void ResetBStartStart(mozilla::LogicalSide aSide, nsCellMap& aCellMap,
                        uint32_t aRowGroupStart, uint32_t aYPos,
                        uint32_t aXPos);

  void SetBCBorderEdge(mozilla::LogicalSide aEdge, nsCellMap& aCellMap,
                       uint32_t aCellMapStart, uint32_t aYPos, uint32_t aXPos,
                       uint32_t aLength, BCBorderOwner aOwner, nscoord aSize,
                       bool aChanged);

  void SetBCBorderCorner(mozilla::LogicalCorner aCorner, nsCellMap& aCellMap,
                         uint32_t aCellMapStart, uint32_t aYPos, uint32_t aXPos,
                         mozilla::LogicalSide aOwner, nscoord aSubSize,
                         bool aBevel, bool aIsBottomRight = false);

  /** dump a representation of the cell map to stdout for debugging */
#ifdef DEBUG
  void Dump(char* aString = nullptr) const;
#endif

 protected:
  BCData* GetIEndMostBorder(int32_t aRowIndex);
  BCData* GetBEndMostBorder(int32_t aColIndex);

  friend class nsCellMap;
  friend class BCMapCellIterator;
  friend class BCPaintBorderIterator;
  friend class nsCellMapColumnIterator;

  /** Insert a row group cellmap after aPrevMap, if aPrefMap is null insert it
   * at the beginning, the ordering of the cellmap corresponds to the ordering
   * of rowgroups once OrderRowGroups has been called
   */
  void InsertGroupCellMap(nsCellMap* aPrevMap, nsCellMap& aNewMap);

  nsTableFrame& mTableFrame;
  AutoTArray<nsColInfo, 8> mCols;
  nsCellMap* mFirstMap = nullptr;
  // border collapsing info
  mozilla::UniquePtr<BCInfo> mBCInfo;
};

/**
 * It maintains an Rows x Columns grid onto which the cells of the table are
 * mapped. This makes processing of rowspan and colspan attributes much easier.
 * Each cell is represented by a CellData object.
 *
 * @see CellData
 * @see nsTableFrame::AddCellToMap
 * @see nsTableFrame::GrowCellMap
 * @see nsTableFrame::BuildCellIntoMap
 *
 * mRows is an array of rows.  Each row is an array of cells.  a cell
 * can be null.
 */
class nsCellMap {
  typedef mozilla::TableArea TableArea;

 public:
  /** constructor
   * @param aRowGroupFrame the row group frame this is a cellmap for
   * @param aIsBC whether the table is doing border-collapse
   */
  nsCellMap(nsTableRowGroupFrame* aRowGroupFrame, bool aIsBC);

  /** destructor
   * NOT VIRTUAL BECAUSE THIS CLASS SHOULD **NEVER** BE SUBCLASSED
   */
  ~nsCellMap();

  static void Init();
  static void Shutdown();

  nsCellMap* GetNextSibling() const;
  void SetNextSibling(nsCellMap* aSibling);

  nsTableRowGroupFrame* GetRowGroup() const;

  nsTableCellFrame* GetCellFrame(int32_t aRowIndex, int32_t aColIndex,
                                 CellData& aData,
                                 bool aUseRowSpanIfOverlap) const;

  /**
   * Returns highest cell index within the cell map.
   *
   * @param  aColCount  [in] the number of columns in the table
   */
  int32_t GetHighestIndex(int32_t aColCount);

  /**
   * Returns the index of the given row and column coordinates.
   *
   * @see  nsITableLayout::GetIndexByRowAndColumn()
   *
   * @param aColCount    [in] the number of columns in the table
   * @param aRow         [in] the row coordinate
   * @param aColumn      [in] the column coordinate
   */
  int32_t GetIndexByRowAndColumn(int32_t aColCount, int32_t aRow,
                                 int32_t aColumn) const;

  /**
   * Get the row and column coordinates at the given index.
   *
   * @see  nsITableLayout::GetRowAndColumnByIndex()
   *
   * @param aColCount  [in] the number of columns in the table
   * @param aIndex     [in] the index for which coordinates are to be retrieved
   * @param aRow       [out] the row coordinate to be returned
   * @param aColumn    [out] the column coordinate to be returned
   */
  void GetRowAndColumnByIndex(int32_t aColCount, int32_t aIndex, int32_t* aRow,
                              int32_t* aColumn) const;

  /** append the cellFrame at an empty or dead cell or finally at the end of
   * the row at aRowIndex and return a pointer to the celldata entry in the
   * cellmap
   *
   * @param aMap               - reference to the table cell map
   * @param aCellFrame         - a pointer to the cellframe which will be
   *                             appended to the row
   * @param aRowIndex          - to this row the celldata entry will be added
   * @param aRebuildIfNecessay - if a cell spans into a row below it might be
   *                             necesserary to rebuild the cellmap as this
   *                             rowspan might overlap another cell.
   * @param aDamageArea        - area in cellmap coordinates which have been
   *                             updated.
   * @param aColToBeginSearch  - if not null contains the column number where
   *                             the search for a empty or dead cell in the
   *                             row should start
   * @return                   - a pointer to the celldata entry inserted into
   *                             the cellmap
   */
  CellData* AppendCell(nsTableCellMap& aMap, nsTableCellFrame* aCellFrame,
                       int32_t aRowIndex, bool aRebuildIfNecessary,
                       int32_t aRgFirstRowIndex, TableArea& aDamageArea,
                       int32_t* aBeginSearchAtCol = nullptr);

  void InsertCells(nsTableCellMap& aMap,
                   nsTArray<nsTableCellFrame*>& aCellFrames, int32_t aRowIndex,
                   int32_t aColIndexBefore, int32_t aRgFirstRowIndex,
                   TableArea& aDamageArea);

  void RemoveCell(nsTableCellMap& aMap, nsTableCellFrame* aCellFrame,
                  int32_t aRowIndex, int32_t aRgFirstRowIndex,
                  TableArea& aDamageArea);

  void InsertRows(nsTableCellMap& aMap, nsTArray<nsTableRowFrame*>& aRows,
                  int32_t aFirstRowIndex, bool aConsiderSpans,
                  int32_t aRgFirstRowIndex, TableArea& aDamageArea);

  void RemoveRows(nsTableCellMap& aMap, int32_t aFirstRowIndex,
                  int32_t aNumRowsToRemove, bool aConsiderSpans,
                  int32_t aRgFirstRowIndex, TableArea& aDamageArea);

  int32_t GetNumCellsOriginatingInRow(int32_t aRowIndex) const;
  int32_t GetNumCellsOriginatingInCol(int32_t aColIndex) const;

  /** return the number of rows in the table represented by this CellMap */
  int32_t GetRowCount(bool aConsiderDeadRowSpanRows = false) const;

  nsTableCellFrame* GetCellInfoAt(const nsTableCellMap& aMap, int32_t aRowX,
                                  int32_t aColX, bool* aOriginates = nullptr,
                                  int32_t* aColSpan = nullptr) const;

  bool RowIsSpannedInto(int32_t aRowIndex, int32_t aNumEffCols) const;

  bool RowHasSpanningCells(int32_t aRowIndex, int32_t aNumEffCols) const;

  /** indicate whether the row has more than one cell that either originates
   * or is spanned from the rows above
   */
  bool HasMoreThanOneCell(int32_t aRowIndex) const;

  /* Get the rowspan for a cell starting at aRowIndex and aColIndex.
   * If aGetEffective is true the size will not exceed the last content based
   * row. Cells can have a specified rowspan that extends below the last
   * content based row. This is legitimate considering incr. reflow where the
   * content rows will arive later.
   */
  int32_t GetRowSpan(int32_t aRowIndex, int32_t aColIndex,
                     bool aGetEffective) const;

  int32_t GetEffectiveColSpan(const nsTableCellMap& aMap, int32_t aRowIndex,
                              int32_t aColIndex) const;

  typedef nsTArray<CellData*> CellDataArray;

  /** dump a representation of the cell map to stdout for debugging */
#ifdef DEBUG
  void Dump(bool aIsBorderCollapse) const;
#endif

 protected:
  friend class nsTableCellMap;
  friend class BCMapCellIterator;
  friend class BCPaintBorderIterator;
  friend class nsTableFrame;
  friend class nsCellMapColumnIterator;

  /**
   * Increase the number of rows in this cellmap by aNumRows.  Put the
   * new rows at aRowIndex.  If aRowIndex is -1, put them at the end.
   */
  bool Grow(nsTableCellMap& aMap, int32_t aNumRows, int32_t aRowIndex = -1);

  void GrowRow(CellDataArray& aRow, int32_t aNumCols);

  /** assign aCellData to the cell at (aRow,aColumn) */
  void SetDataAt(nsTableCellMap& aMap, CellData& aCellData,
                 int32_t aMapRowIndex, int32_t aColIndex);

  CellData* GetDataAt(int32_t aMapRowIndex, int32_t aColIndex) const;

  void ExpandWithRows(nsTableCellMap& aMap,
                      nsTArray<nsTableRowFrame*>& aRowFrames,
                      int32_t aStartRowIndex, int32_t aRgFirstRowIndex,
                      TableArea& aDamageArea);

  void ExpandWithCells(nsTableCellMap& aMap,
                       nsTArray<nsTableCellFrame*>& aCellFrames,
                       int32_t aRowIndex, int32_t aColIndex, int32_t aRowSpan,
                       bool aRowSpanIsZero, int32_t aRgFirstRowIndex,
                       TableArea& aDamageArea);

  void ShrinkWithoutRows(nsTableCellMap& aMap, int32_t aFirstRowIndex,
                         int32_t aNumRowsToRemove, int32_t aRgFirstRowIndex,
                         TableArea& aDamageArea);

  void ShrinkWithoutCell(nsTableCellMap& aMap, nsTableCellFrame& aCellFrame,
                         int32_t aRowIndex, int32_t aColIndex,
                         int32_t aRgFirstRowIndex, TableArea& aDamageArea);

  /**
   * Rebuild due to rows being inserted or deleted with cells spanning
   * into or out of the rows.  This function can only handle insertion
   * or deletion but NOT both.  So either aRowsToInsert must be null
   * or aNumRowsToRemove must be 0.
   *
   * // XXXbz are both allowed to happen?  That'd be a no-op...
   */
  void RebuildConsideringRows(nsTableCellMap& aMap, int32_t aStartRowIndex,
                              nsTArray<nsTableRowFrame*>* aRowsToInsert,
                              int32_t aNumRowsToRemove);

  void RebuildConsideringCells(nsTableCellMap& aMap, int32_t aNumOrigCols,
                               nsTArray<nsTableCellFrame*>* aCellFrames,
                               int32_t aRowIndex, int32_t aColIndex,
                               bool aInsert);

  bool CellsSpanOut(nsTArray<nsTableRowFrame*>& aNewRows) const;

  /** If a cell spans out of the area defined by aStartRowIndex, aEndRowIndex
    * and aStartColIndex, aEndColIndex the cellmap changes are more severe so
    * the corresponding routines needs to be called. This is also necessary if
    * cells outside spans into this region.
    * @aStartRowIndex       - y start index
    * @aEndRowIndex         - y end index
    * @param aStartColIndex - x start index
    * @param aEndColIndex   - x end index
    * @return               - true if a cell span crosses the border of the
                              region
    */
  bool CellsSpanInOrOut(int32_t aStartRowIndex, int32_t aEndRowIndex,
                        int32_t aStartColIndex, int32_t aEndColIndex) const;

  int32_t GetRowSpanForNewCell(nsTableCellFrame* aCellFrameToAdd,
                               int32_t aRowIndex, bool& aIsZeroRowSpan) const;

  // Destroy a CellData struct.  This will handle the case of aData
  // actually being a BCCellData properly.
  void DestroyCellData(CellData* aData);
  // Allocate a CellData struct.  This will handle needing to create a
  // BCCellData properly.
  // @param aOrigCell the originating cell to pass to the celldata constructor
  CellData* AllocCellData(nsTableCellFrame* aOrigCell);

  /** an array containing, for each row, the CellDatas for the cells
   * in that row.  It can be larger than mContentRowCount due to row spans
   * extending beyond the table */
  // XXXbz once we have auto TArrays, we should probably use them here.
  nsTArray<CellDataArray> mRows;

  /** the number of rows in the table (content) which is not indentical to the
   * number of rows in the cell map due to row spans extending beyond the end
   * of thetable (dead rows) or empty tr tags
   */
  int32_t mContentRowCount;

  // the row group that corresponds to this map
  nsTableRowGroupFrame* mRowGroupFrame;

  // the next row group cell map
  nsCellMap* mNextSibling;

  // Whether this is a BC cellmap or not
  bool mIsBC;

  // Prescontext to deallocate and allocate celldata
  RefPtr<nsPresContext> mPresContext;
};

/**
 * A class for iterating the cells in a given column.  Must be given a
 * non-null nsTableCellMap and a column number valid for that cellmap.
 */
class nsCellMapColumnIterator {
 public:
  nsCellMapColumnIterator(const nsTableCellMap* aMap, int32_t aCol)
      : mMap(aMap),
        mCurMap(aMap->mFirstMap),
        mCurMapStart(0),
        mCurMapRow(0),
        mCol(aCol),
        mFoundCells(0),
        mCurMapContentRowCount(0),
        mCurMapRelevantRowCount(0) {
    MOZ_ASSERT(aMap, "Must have map");
    MOZ_ASSERT(mCol < aMap->GetColCount(), "Invalid column");
    mOrigCells = aMap->GetNumCellsOriginatingInCol(mCol);
    if (mCurMap) {
      mCurMapContentRowCount = mCurMap->GetRowCount();
      uint32_t rowArrayLength = mCurMap->mRows.Length();
      mCurMapRelevantRowCount =
          std::min(mCurMapContentRowCount, rowArrayLength);
      if (mCurMapRelevantRowCount == 0 && mOrigCells > 0) {
        // This row group is useless; advance!
        AdvanceRowGroup();
      }
    }
#ifdef DEBUG
    else {
      NS_ASSERTION(mOrigCells == 0, "Why no rowgroups?");
    }
#endif
  }

  nsTableCellFrame* GetNextFrame(int32_t* aRow, int32_t* aColSpan);

 private:
  void AdvanceRowGroup();

  // Advance the row; aIncrement is considered to be a cell's rowspan,
  // so if 0 is passed in we'll advance to the next rowgroup.
  void IncrementRow(int32_t aIncrement);

  const nsTableCellMap* mMap;
  const nsCellMap* mCurMap;

  // mCurMapStart is the row in the entire nsTableCellMap where
  // mCurMap starts.  This is used to compute row indices to pass to
  // nsTableCellMap::GetDataAt, so must be a _content_ row index.
  uint32_t mCurMapStart;

  // In steady-state mCurMapRow is the row in our current nsCellMap
  // that we'll use the next time GetNextFrame() is called.  Due to
  // the way we skip over rowspans, the entry in mCurMapRow and mCol
  // is either null, dead, originating, or a colspan.  In particular,
  // it cannot be a rowspan or overlap entry.
  uint32_t mCurMapRow;
  const int32_t mCol;
  uint32_t mOrigCells;
  uint32_t mFoundCells;

  // The number of content rows in mCurMap.  This may be bigger than the number
  // of "relevant" rows, or it might be smaller.
  uint32_t mCurMapContentRowCount;

  // The number of "relevant" rows in mCurMap.  That is, the number of rows
  // which might have an originating cell in them.  Once mCurMapRow reaches
  // mCurMapRelevantRowCount, we should move to the next map.
  uint32_t mCurMapRelevantRowCount;
};

/* ----- inline methods ----- */
inline int32_t nsTableCellMap::GetColCount() const { return mCols.Length(); }

inline nsCellMap* nsCellMap::GetNextSibling() const { return mNextSibling; }

inline void nsCellMap::SetNextSibling(nsCellMap* aSibling) {
  mNextSibling = aSibling;
}

inline nsTableRowGroupFrame* nsCellMap::GetRowGroup() const {
  return mRowGroupFrame;
}

inline int32_t nsCellMap::GetRowCount(bool aConsiderDeadRowSpanRows) const {
  int32_t rowCount =
      (aConsiderDeadRowSpanRows) ? mRows.Length() : mContentRowCount;
  return rowCount;
}

// nsColInfo

inline nsColInfo::nsColInfo() : mNumCellsOrig(0), mNumCellsSpan(0) {}

inline nsColInfo::nsColInfo(int32_t aNumCellsOrig, int32_t aNumCellsSpan)
    : mNumCellsOrig(aNumCellsOrig), mNumCellsSpan(aNumCellsSpan) {}

#endif
