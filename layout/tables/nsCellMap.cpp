/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCellMap.h"

#include <algorithm>

#include "mozilla/PresShell.h"
#include "mozilla/StaticPtr.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableFrame.h"
#include "nsTableRowFrame.h"
#include "nsTableRowGroupFrame.h"

using namespace mozilla;

static void SetDamageArea(int32_t aStartCol, int32_t aStartRow,
                          int32_t aColCount, int32_t aRowCount,
                          TableArea& aDamageArea) {
  NS_ASSERTION(aStartCol >= 0, "negative col index");
  NS_ASSERTION(aStartRow >= 0, "negative row index");
  NS_ASSERTION(aColCount >= 0, "negative col count");
  NS_ASSERTION(aRowCount >= 0, "negative row count");
  aDamageArea.StartCol() = aStartCol;
  aDamageArea.StartRow() = aStartRow;
  aDamageArea.ColCount() = aColCount;
  aDamageArea.RowCount() = aRowCount;
}

// Empty static array used for SafeElementAt() calls on mRows.
static StaticAutoPtr<nsCellMap::CellDataArray> sEmptyRow;

// CellData

CellData::CellData(nsTableCellFrame* aOrigCell) {
  MOZ_COUNT_CTOR(CellData);
  static_assert(sizeof(mOrigCell) == sizeof(mBits),
                "mOrigCell and mBits must be the same size");
  mOrigCell = aOrigCell;
}

CellData::~CellData() { MOZ_COUNT_DTOR(CellData); }

BCCellData::BCCellData(nsTableCellFrame* aOrigCell) : CellData(aOrigCell) {
  MOZ_COUNT_CTOR(BCCellData);
}

BCCellData::~BCCellData() { MOZ_COUNT_DTOR(BCCellData); }

// nsTableCellMap

nsTableCellMap::nsTableCellMap(nsTableFrame& aTableFrame, bool aBorderCollapse)
    : mTableFrame(aTableFrame) {
  MOZ_COUNT_CTOR(nsTableCellMap);

  nsTableFrame::RowGroupArray orderedRowGroups = aTableFrame.OrderedRowGroups();

  nsTableRowGroupFrame* prior = nullptr;
  for (uint32_t rgX = 0; rgX < orderedRowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgX];
    InsertGroupCellMap(rgFrame, prior);
    prior = rgFrame;
  }
  if (aBorderCollapse) {
    mBCInfo = MakeUnique<BCInfo>();
  }
}

nsTableCellMap::~nsTableCellMap() {
  MOZ_COUNT_DTOR(nsTableCellMap);

  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    nsCellMap* next = cellMap->GetNextSibling();
    delete cellMap;
    cellMap = next;
  }
}

// Get the bcData holding the border segments of the iEnd edge of the table
BCData* nsTableCellMap::GetIEndMostBorder(int32_t aRowIndex) {
  if (!mBCInfo) ABORT1(nullptr);

  int32_t numRows = mBCInfo->mIEndBorders.Length();
  if (aRowIndex < numRows) {
    return &mBCInfo->mIEndBorders.ElementAt(aRowIndex);
  }

  mBCInfo->mIEndBorders.SetLength(aRowIndex + 1);
  return &mBCInfo->mIEndBorders.ElementAt(aRowIndex);
}

// Get the bcData holding the border segments of the bEnd edge of the table
BCData* nsTableCellMap::GetBEndMostBorder(int32_t aColIndex) {
  if (!mBCInfo) ABORT1(nullptr);

  int32_t numCols = mBCInfo->mBEndBorders.Length();
  if (aColIndex < numCols) {
    return &mBCInfo->mBEndBorders.ElementAt(aColIndex);
  }

  mBCInfo->mBEndBorders.SetLength(aColIndex + 1);
  return &mBCInfo->mBEndBorders.ElementAt(aColIndex);
}

void nsTableCellMap::InsertGroupCellMap(nsCellMap* aPrevMap,
                                        nsCellMap& aNewMap) {
  nsCellMap* next;
  if (aPrevMap) {
    next = aPrevMap->GetNextSibling();
    aPrevMap->SetNextSibling(&aNewMap);
  } else {
    next = mFirstMap;
    mFirstMap = &aNewMap;
  }
  aNewMap.SetNextSibling(next);
}

void nsTableCellMap::InsertGroupCellMap(nsTableRowGroupFrame* aNewGroup,
                                        nsTableRowGroupFrame*& aPrevGroup) {
  nsCellMap* newMap = new nsCellMap(aNewGroup, mBCInfo != nullptr);
  nsCellMap* prevMap = nullptr;
  nsCellMap* lastMap = mFirstMap;
  if (aPrevGroup) {
    nsCellMap* map = mFirstMap;
    while (map) {
      lastMap = map;
      if (map->GetRowGroup() == aPrevGroup) {
        prevMap = map;
        break;
      }
      map = map->GetNextSibling();
    }
  }
  if (!prevMap) {
    if (aPrevGroup) {
      prevMap = lastMap;
      aPrevGroup = (prevMap) ? prevMap->GetRowGroup() : nullptr;
    } else {
      aPrevGroup = nullptr;
    }
  }
  InsertGroupCellMap(prevMap, *newMap);
}

void nsTableCellMap::RemoveGroupCellMap(nsTableRowGroupFrame* aGroup) {
  nsCellMap* map = mFirstMap;
  nsCellMap* prior = nullptr;
  while (map) {
    if (map->GetRowGroup() == aGroup) {
      nsCellMap* next = map->GetNextSibling();
      if (mFirstMap == map) {
        mFirstMap = next;
      } else {
        prior->SetNextSibling(next);
      }
      delete map;
      break;
    }
    prior = map;
    map = map->GetNextSibling();
  }
}

static nsCellMap* FindMapFor(const nsTableRowGroupFrame* aRowGroup,
                             nsCellMap* aStart, const nsCellMap* aEnd) {
  for (nsCellMap* map = aStart; map != aEnd; map = map->GetNextSibling()) {
    if (aRowGroup == map->GetRowGroup()) {
      return map;
    }
  }

  return nullptr;
}

nsCellMap* nsTableCellMap::GetMapFor(const nsTableRowGroupFrame* aRowGroup,
                                     nsCellMap* aStartHint) const {
  MOZ_ASSERT(aRowGroup, "Must have a rowgroup");
  aRowGroup = static_cast<nsTableRowGroupFrame*>(aRowGroup->FirstInFlow());
  if (aStartHint) {
    nsCellMap* map = FindMapFor(aRowGroup, aStartHint, nullptr);
    if (map) {
      return map;
    }
  }

  nsCellMap* map = FindMapFor(aRowGroup, mFirstMap, aStartHint);
  if (map) {
    return map;
  }

  // If aRowGroup is a repeated header or footer find the header or footer it
  // was repeated from.
  // Bug 1442018: we also need this search for header/footer frames that are
  // not marked as _repeatable_ because they have a next-in-flow, as they may
  // nevertheless have been _repeated_ from an earlier fragment.
  auto isTableHeaderFooterGroup = [](const nsTableRowGroupFrame* aRG) -> bool {
    const auto display = aRG->StyleDisplay()->mDisplay;
    return display == StyleDisplay::TableHeaderGroup ||
           display == StyleDisplay::TableFooterGroup;
  };
  if (aRowGroup->IsRepeatable() ||
      (aRowGroup->GetNextInFlow() && isTableHeaderFooterGroup(aRowGroup))) {
    auto findOtherRowGroupOfType =
        [aRowGroup](nsTableFrame* aTable) -> nsTableRowGroupFrame* {
      const auto display = aRowGroup->StyleDisplay()->mDisplay;
      auto* table = aTable->FirstContinuation();
      for (; table; table = table->GetNextContinuation()) {
        for (auto* child : table->PrincipalChildList()) {
          if (child->StyleDisplay()->mDisplay == display &&
              child != aRowGroup) {
            return static_cast<nsTableRowGroupFrame*>(child);
          }
        }
      }
      return nullptr;
    };
    if (auto* rgOrig = findOtherRowGroupOfType(&mTableFrame)) {
      return GetMapFor(rgOrig, aStartHint);
    }
    MOZ_ASSERT_UNREACHABLE(
        "A repeated header/footer should always have an "
        "original header/footer it was repeated from");
  }

  return nullptr;
}

void nsTableCellMap::Synchronize(nsTableFrame* aTableFrame) {
  AutoTArray<nsCellMap*, 8> maps;

  nsTableFrame::RowGroupArray orderedRowGroups =
      aTableFrame->OrderedRowGroups();
  if (!orderedRowGroups.Length()) {
    return;
  }

  // XXXbz this fails if orderedRowGroups is missing some row groups
  // (due to OOM when appending to the array, e.g. -- we leak maps in
  // that case).

  // Scope |map| outside the loop so we can use it as a hint.
  nsCellMap* map = nullptr;
  for (uint32_t rgX = 0; rgX < orderedRowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgX];
    map = GetMapFor(rgFrame, map);
    if (map) {
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier, or change the return type to void.
      maps.AppendElement(map);
    }
  }
  if (maps.IsEmpty()) {
    MOZ_ASSERT(!mFirstMap);
    return;
  }

  int32_t mapIndex = maps.Length() - 1;  // Might end up -1
  nsCellMap* nextMap = maps.ElementAt(mapIndex);
  nextMap->SetNextSibling(nullptr);
  for (mapIndex--; mapIndex >= 0; mapIndex--) {
    nsCellMap* map = maps.ElementAt(mapIndex);
    map->SetNextSibling(nextMap);
    nextMap = map;
  }
  mFirstMap = nextMap;
}

bool nsTableCellMap::HasMoreThanOneCell(int32_t aRowIndex) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->HasMoreThanOneCell(rowIndex);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  return false;
}

int32_t nsTableCellMap::GetNumCellsOriginatingInRow(int32_t aRowIndex) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->GetNumCellsOriginatingInRow(rowIndex);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  return 0;
}
int32_t nsTableCellMap::GetEffectiveRowSpan(int32_t aRowIndex,
                                            int32_t aColIndex) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->GetRowSpan(rowIndex, aColIndex, true);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  MOZ_ASSERT_UNREACHABLE("Bogus row index?");
  return 0;
}

int32_t nsTableCellMap::GetEffectiveColSpan(int32_t aRowIndex,
                                            int32_t aColIndex) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->GetEffectiveColSpan(*this, rowIndex, aColIndex);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  MOZ_ASSERT_UNREACHABLE("Bogus row index?");
  return 0;
}

nsTableCellFrame* nsTableCellMap::GetCellFrame(int32_t aRowIndex,
                                               int32_t aColIndex,
                                               CellData& aData,
                                               bool aUseRowIfOverlap) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->GetCellFrame(rowIndex, aColIndex, aData, aUseRowIfOverlap);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  return nullptr;
}

nsColInfo* nsTableCellMap::GetColInfoAt(int32_t aColIndex) {
  int32_t numColsToAdd = aColIndex + 1 - mCols.Length();
  if (numColsToAdd > 0) {
    AddColsAtEnd(numColsToAdd);  // XXX this could fail to add cols in theory
  }
  return &mCols.ElementAt(aColIndex);
}

int32_t nsTableCellMap::GetRowCount() const {
  int32_t numRows = 0;
  nsCellMap* map = mFirstMap;
  while (map) {
    numRows += map->GetRowCount();
    map = map->GetNextSibling();
  }
  return numRows;
}

CellData* nsTableCellMap::GetDataAt(int32_t aRowIndex,
                                    int32_t aColIndex) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* map = mFirstMap;
  while (map) {
    if (map->GetRowCount() > rowIndex) {
      return map->GetDataAt(rowIndex, aColIndex);
    }
    rowIndex -= map->GetRowCount();
    map = map->GetNextSibling();
  }
  return nullptr;
}

void nsTableCellMap::AddColsAtEnd(uint32_t aNumCols) {
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier.
  mCols.AppendElements(aNumCols);
  if (mBCInfo) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mBCInfo->mBEndBorders.AppendElements(aNumCols);
  }
}

void nsTableCellMap::RemoveColsAtEnd() {
  // Remove the cols at the end which don't have originating cells or cells
  // spanning into them. Only do this if the col was created as
  // eColAnonymousCell
  int32_t numCols = GetColCount();
  int32_t lastGoodColIndex = mTableFrame.GetIndexOfLastRealCol();
  MOZ_ASSERT(lastGoodColIndex >= -1);
  for (int32_t colX = numCols - 1; colX > lastGoodColIndex; colX--) {
    nsColInfo& colInfo = mCols.ElementAt(colX);
    if ((colInfo.mNumCellsOrig <= 0) && (colInfo.mNumCellsSpan <= 0)) {
      mCols.RemoveElementAt(colX);

      if (mBCInfo) {
        int32_t count = mBCInfo->mBEndBorders.Length();
        if (colX < count) {
          mBCInfo->mBEndBorders.RemoveElementAt(colX);
        }
      }
    } else {
      break;  // only remove until we encounter the 1st valid one
    }
  }
}

void nsTableCellMap::ClearCols() {
  mCols.Clear();
  if (mBCInfo) {
    mBCInfo->mBEndBorders.Clear();
  }
}
void nsTableCellMap::InsertRows(nsTableRowGroupFrame* aParent,
                                nsTArray<nsTableRowFrame*>& aRows,
                                int32_t aFirstRowIndex, bool aConsiderSpans,
                                TableArea& aDamageArea) {
  int32_t numNewRows = aRows.Length();
  if ((numNewRows <= 0) || (aFirstRowIndex < 0)) ABORT0();

  int32_t rowIndex = aFirstRowIndex;
  int32_t rgStartRowIndex = 0;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    nsTableRowGroupFrame* rg = cellMap->GetRowGroup();
    if (rg == aParent) {
      cellMap->InsertRows(*this, aRows, rowIndex, aConsiderSpans,
                          rgStartRowIndex, aDamageArea);
#ifdef DEBUG_TABLE_CELLMAP
      Dump("after InsertRows");
#endif
      if (mBCInfo) {
        int32_t count = mBCInfo->mIEndBorders.Length();
        if (aFirstRowIndex < count) {
          for (int32_t rowX = aFirstRowIndex;
               rowX < aFirstRowIndex + numNewRows; rowX++) {
            mBCInfo->mIEndBorders.InsertElementAt(rowX);
          }
        } else {
          GetIEndMostBorder(
              aFirstRowIndex);  // this will create missing entries
          for (int32_t rowX = aFirstRowIndex + 1;
               rowX < aFirstRowIndex + numNewRows; rowX++) {
            mBCInfo->mIEndBorders.AppendElement();
          }
        }
      }
      return;
    }
    int32_t rowCount = cellMap->GetRowCount();
    rgStartRowIndex += rowCount;
    rowIndex -= rowCount;
    cellMap = cellMap->GetNextSibling();
  }

  NS_ERROR("Attempt to insert row into wrong map.");
}

void nsTableCellMap::RemoveRows(int32_t aFirstRowIndex,
                                int32_t aNumRowsToRemove, bool aConsiderSpans,
                                TableArea& aDamageArea) {
  int32_t rowIndex = aFirstRowIndex;
  int32_t rgStartRowIndex = 0;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    int32_t rowCount = cellMap->GetRowCount();
    if (rowCount > rowIndex) {
      cellMap->RemoveRows(*this, rowIndex, aNumRowsToRemove, aConsiderSpans,
                          rgStartRowIndex, aDamageArea);
      if (mBCInfo) {
        for (int32_t rowX = aFirstRowIndex + aNumRowsToRemove - 1;
             rowX >= aFirstRowIndex; rowX--) {
          if (uint32_t(rowX) < mBCInfo->mIEndBorders.Length()) {
            mBCInfo->mIEndBorders.RemoveElementAt(rowX);
          }
        }
      }
      break;
    }
    rgStartRowIndex += rowCount;
    rowIndex -= rowCount;
    cellMap = cellMap->GetNextSibling();
  }
#ifdef DEBUG_TABLE_CELLMAP
  Dump("after RemoveRows");
#endif
}

CellData* nsTableCellMap::AppendCell(nsTableCellFrame& aCellFrame,
                                     int32_t aRowIndex,
                                     bool aRebuildIfNecessary,
                                     TableArea& aDamageArea) {
  MOZ_ASSERT(&aCellFrame == aCellFrame.FirstInFlow(),
             "invalid call on continuing frame");
  nsIFrame* rgFrame = aCellFrame.GetParent();  // get the row
  if (!rgFrame) {
    return nullptr;
  }
  rgFrame = rgFrame->GetParent();  // get the row group
  if (!rgFrame) {
    return nullptr;
  }

  CellData* result = nullptr;
  int32_t rowIndex = aRowIndex;
  int32_t rgStartRowIndex = 0;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    if (cellMap->GetRowGroup() == rgFrame) {
      result =
          cellMap->AppendCell(*this, &aCellFrame, rowIndex, aRebuildIfNecessary,
                              rgStartRowIndex, aDamageArea);
      break;
    }
    int32_t rowCount = cellMap->GetRowCount();
    rgStartRowIndex += rowCount;
    rowIndex -= rowCount;
    cellMap = cellMap->GetNextSibling();
  }
#ifdef DEBUG_TABLE_CELLMAP
  Dump("after AppendCell");
#endif
  return result;
}

void nsTableCellMap::InsertCells(nsTArray<nsTableCellFrame*>& aCellFrames,
                                 int32_t aRowIndex, int32_t aColIndexBefore,
                                 TableArea& aDamageArea) {
  int32_t rowIndex = aRowIndex;
  int32_t rgStartRowIndex = 0;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    int32_t rowCount = cellMap->GetRowCount();
    if (rowCount > rowIndex) {
      cellMap->InsertCells(*this, aCellFrames, rowIndex, aColIndexBefore,
                           rgStartRowIndex, aDamageArea);
      break;
    }
    rgStartRowIndex += rowCount;
    rowIndex -= rowCount;
    cellMap = cellMap->GetNextSibling();
  }
#ifdef DEBUG_TABLE_CELLMAP
  Dump("after InsertCells");
#endif
}

void nsTableCellMap::RemoveCell(nsTableCellFrame* aCellFrame, int32_t aRowIndex,
                                TableArea& aDamageArea) {
  if (!aCellFrame) ABORT0();
  MOZ_ASSERT(aCellFrame == aCellFrame->FirstInFlow(),
             "invalid call on continuing frame");
  int32_t rowIndex = aRowIndex;
  int32_t rgStartRowIndex = 0;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    int32_t rowCount = cellMap->GetRowCount();
    if (rowCount > rowIndex) {
      cellMap->RemoveCell(*this, aCellFrame, rowIndex, rgStartRowIndex,
                          aDamageArea);
#ifdef DEBUG_TABLE_CELLMAP
      Dump("after RemoveCell");
#endif
      return;
    }
    rgStartRowIndex += rowCount;
    rowIndex -= rowCount;
    cellMap = cellMap->GetNextSibling();
  }
  // if we reach this point - the cell did not get removed, the caller of this
  // routine will delete the cell and the cellmap will probably hold a reference
  // to the deleted cell which will cause a subsequent crash when this cell is
  // referenced later
  NS_ERROR("nsTableCellMap::RemoveCell - could not remove cell");
}

void nsTableCellMap::RebuildConsideringCells(
    nsCellMap* aCellMap, nsTArray<nsTableCellFrame*>* aCellFrames,
    int32_t aRowIndex, int32_t aColIndex, bool aInsert,
    TableArea& aDamageArea) {
  int32_t numOrigCols = GetColCount();
  ClearCols();
  nsCellMap* cellMap = mFirstMap;
  int32_t rowCount = 0;
  while (cellMap) {
    if (cellMap == aCellMap) {
      cellMap->RebuildConsideringCells(*this, numOrigCols, aCellFrames,
                                       aRowIndex, aColIndex, aInsert);
    } else {
      cellMap->RebuildConsideringCells(*this, numOrigCols, nullptr, -1, 0,
                                       false);
    }
    rowCount += cellMap->GetRowCount();
    cellMap = cellMap->GetNextSibling();
  }
  SetDamageArea(0, 0, GetColCount(), rowCount, aDamageArea);
}

void nsTableCellMap::RebuildConsideringRows(
    nsCellMap* aCellMap, int32_t aStartRowIndex,
    nsTArray<nsTableRowFrame*>* aRowsToInsert, int32_t aNumRowsToRemove,
    TableArea& aDamageArea) {
  MOZ_ASSERT(!aRowsToInsert || aNumRowsToRemove == 0,
             "Can't handle both removing and inserting rows at once");

  int32_t numOrigCols = GetColCount();
  ClearCols();
  nsCellMap* cellMap = mFirstMap;
  int32_t rowCount = 0;
  while (cellMap) {
    if (cellMap == aCellMap) {
      cellMap->RebuildConsideringRows(*this, aStartRowIndex, aRowsToInsert,
                                      aNumRowsToRemove);
    } else {
      cellMap->RebuildConsideringCells(*this, numOrigCols, nullptr, -1, 0,
                                       false);
    }
    rowCount += cellMap->GetRowCount();
    cellMap = cellMap->GetNextSibling();
  }
  SetDamageArea(0, 0, GetColCount(), rowCount, aDamageArea);
}

int32_t nsTableCellMap::GetNumCellsOriginatingInCol(int32_t aColIndex) const {
  int32_t colCount = mCols.Length();
  if ((aColIndex >= 0) && (aColIndex < colCount)) {
    return mCols.ElementAt(aColIndex).mNumCellsOrig;
  } else {
    NS_ERROR("nsCellMap::GetNumCellsOriginatingInCol - bad col index");
    return 0;
  }
}

#ifdef DEBUG
void nsTableCellMap::Dump(char* aString) const {
  if (aString) printf("%s \n", aString);
  printf("***** START TABLE CELL MAP DUMP ***** %p\n", (void*)this);
  // output col info
  int32_t colCount = mCols.Length();
  printf("cols array orig/span-> %p", (void*)this);
  for (int32_t colX = 0; colX < colCount; colX++) {
    const nsColInfo& colInfo = mCols.ElementAt(colX);
    printf("%d=%d/%d ", colX, colInfo.mNumCellsOrig, colInfo.mNumCellsSpan);
  }
  printf(" cols in cache %d\n", int(mTableFrame.GetColCache().Length()));
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    cellMap->Dump(nullptr != mBCInfo);
    cellMap = cellMap->GetNextSibling();
  }
  if (nullptr != mBCInfo) {
    printf("***** block-end borders *****\n");
    nscoord size;
    BCBorderOwner owner;
    LogicalSide side;
    bool segStart;
    bool bevel;
    int32_t colIndex;
    int32_t numCols = mBCInfo->mBEndBorders.Length();
    for (int32_t i = 0; i <= 2; i++) {
      printf("\n          ");
      for (colIndex = 0; colIndex < numCols; colIndex++) {
        BCData& cd = mBCInfo->mBEndBorders.ElementAt(colIndex);
        if (0 == i) {
          size = cd.GetBStartEdge(owner, segStart);
          printf("t=%d%X%d ", int32_t(size), owner, segStart);
        } else if (1 == i) {
          size = cd.GetIStartEdge(owner, segStart);
          printf("l=%d%X%d ", int32_t(size), owner, segStart);
        } else {
          size = cd.GetCorner(side, bevel);
          printf("c=%d%hhX%d ", int32_t(size), static_cast<uint8_t>(side),
                 bevel);
        }
      }
      BCData& cd = mBCInfo->mBEndIEndCorner;
      if (0 == i) {
        size = cd.GetBStartEdge(owner, segStart);
        printf("t=%d%X%d ", int32_t(size), owner, segStart);
      } else if (1 == i) {
        size = cd.GetIStartEdge(owner, segStart);
        printf("l=%d%X%d ", int32_t(size), owner, segStart);
      } else {
        size = cd.GetCorner(side, bevel);
        printf("c=%d%hhX%d ", int32_t(size), static_cast<uint8_t>(side), bevel);
      }
    }
    printf("\n");
  }
  printf("***** END TABLE CELL MAP DUMP *****\n");
}
#endif

nsTableCellFrame* nsTableCellMap::GetCellInfoAt(int32_t aRowIndex,
                                                int32_t aColIndex,
                                                bool* aOriginates,
                                                int32_t* aColSpan) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    if (cellMap->GetRowCount() > rowIndex) {
      return cellMap->GetCellInfoAt(*this, rowIndex, aColIndex, aOriginates,
                                    aColSpan);
    }
    rowIndex -= cellMap->GetRowCount();
    cellMap = cellMap->GetNextSibling();
  }
  return nullptr;
}

int32_t nsTableCellMap::GetIndexByRowAndColumn(int32_t aRow,
                                               int32_t aColumn) const {
  int32_t index = 0;

  int32_t colCount = mCols.Length();
  int32_t rowIndex = aRow;

  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    int32_t rowCount = cellMap->GetRowCount();
    if (rowIndex >= rowCount) {
      // If the rowCount is less than the rowIndex, this means that the index is
      // not within the current map. If so, get the index of the last cell in
      // the last row.
      rowIndex -= rowCount;

      int32_t cellMapIdx = cellMap->GetHighestIndex(colCount);
      if (cellMapIdx != -1) {
        index += cellMapIdx + 1;
      }

    } else {
      // Index is in valid range for this cellmap, so get the index of rowIndex
      // and aColumn.
      int32_t cellMapIdx =
          cellMap->GetIndexByRowAndColumn(colCount, rowIndex, aColumn);
      if (cellMapIdx == -1) {
        return -1;  // no cell at the given row and column.
      }

      index += cellMapIdx;
      return index;  // no need to look through further maps here
    }

    cellMap = cellMap->GetNextSibling();
  }

  return -1;
}

void nsTableCellMap::GetRowAndColumnByIndex(int32_t aIndex, int32_t* aRow,
                                            int32_t* aColumn) const {
  *aRow = -1;
  *aColumn = -1;

  int32_t colCount = mCols.Length();

  int32_t previousRows = 0;
  int32_t index = aIndex;

  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    int32_t rowCount = cellMap->GetRowCount();
    // Determine the highest possible index in this map to see
    // if wanted index is in here.
    int32_t cellMapIdx = cellMap->GetHighestIndex(colCount);
    if (cellMapIdx == -1) {
      // The index is not within this map, increase the total row index
      // accordingly.
      previousRows += rowCount;
    } else {
      if (index > cellMapIdx) {
        // The index is not within this map, so decrease it by the cellMapIdx
        // determined index and increase the total row index accordingly.
        index -= cellMapIdx + 1;
        previousRows += rowCount;
      } else {
        cellMap->GetRowAndColumnByIndex(colCount, index, aRow, aColumn);
        // If there were previous indexes, take them into account.
        *aRow += previousRows;
        return;  // no need to look any further.
      }
    }

    cellMap = cellMap->GetNextSibling();
  }
}

bool nsTableCellMap::RowIsSpannedInto(int32_t aRowIndex,
                                      int32_t aNumEffCols) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    if (cellMap->GetRowCount() > rowIndex) {
      return cellMap->RowIsSpannedInto(rowIndex, aNumEffCols);
    }
    rowIndex -= cellMap->GetRowCount();
    cellMap = cellMap->GetNextSibling();
  }
  return false;
}

bool nsTableCellMap::RowHasSpanningCells(int32_t aRowIndex,
                                         int32_t aNumEffCols) const {
  int32_t rowIndex = aRowIndex;
  nsCellMap* cellMap = mFirstMap;
  while (cellMap) {
    if (cellMap->GetRowCount() > rowIndex) {
      return cellMap->RowHasSpanningCells(rowIndex, aNumEffCols);
    }
    rowIndex -= cellMap->GetRowCount();
    cellMap = cellMap->GetNextSibling();
  }
  return false;
}

// FIXME: The only value callers pass for aSide is LogicalSide::BEnd.
// Consider removing support for the other three values.
void nsTableCellMap::ResetBStartStart(LogicalSide aSide, nsCellMap& aCellMap,
                                      uint32_t aRowGroupStart,
                                      uint32_t aRowIndex, uint32_t aColIndex) {
  if (!mBCInfo) ABORT0();

  BCCellData* cellData;
  BCData* bcData = nullptr;

  switch (aSide) {
    case LogicalSide::BEnd:
      aRowIndex++;
      [[fallthrough]];
    case LogicalSide::BStart:
      cellData = (BCCellData*)aCellMap.GetDataAt(aRowIndex - aRowGroupStart,
                                                 aColIndex);
      if (cellData) {
        bcData = &cellData->mData;
      } else {
        NS_ASSERTION(aSide == LogicalSide::BEnd, "program error");
        // try the next row group
        nsCellMap* cellMap = aCellMap.GetNextSibling();
        if (cellMap) {
          cellData = (BCCellData*)cellMap->GetDataAt(0, aColIndex);
          if (cellData) {
            bcData = &cellData->mData;
          } else {
            bcData = GetBEndMostBorder(aColIndex);
          }
        }
      }
      break;
    case LogicalSide::IEnd:
      aColIndex++;
      [[fallthrough]];
    case LogicalSide::IStart:
      cellData = (BCCellData*)aCellMap.GetDataAt(aRowIndex - aRowGroupStart,
                                                 aColIndex);
      if (cellData) {
        bcData = &cellData->mData;
      } else {
        NS_ASSERTION(aSide == LogicalSide::IEnd, "program error");
        bcData = GetIEndMostBorder(aRowIndex);
      }
      break;
  }
  if (bcData) {
    bcData->SetBStartStart(false);
  }
}

// store the aSide border segment at coord = (aRowIndex, aColIndex). For
// bStart/iStart, store the info at coord. For bEnd/iEnd store it at the
// adjacent location so that it is bStart/iStart at that location. If the new
// location is at the iEnd or bEnd edge of the table, then store it one of the
// special arrays (iEnd-most borders, bEnd-most borders).
void nsTableCellMap::SetBCBorderEdge(LogicalSide aSide, nsCellMap& aCellMap,
                                     uint32_t aCellMapStart, uint32_t aRowIndex,
                                     uint32_t aColIndex, uint32_t aLength,
                                     BCBorderOwner aOwner, nscoord aSize,
                                     bool aChanged) {
  if (!mBCInfo) ABORT0();

  BCCellData* cellData;
  int32_t lastIndex, xIndex, yIndex;
  int32_t xPos = aColIndex;
  int32_t rgYPos = aRowIndex - aCellMapStart;
  bool changed;

  switch (aSide) {
    case LogicalSide::BEnd:
      rgYPos++;
      [[fallthrough]];
    case LogicalSide::BStart:
      lastIndex = xPos + aLength - 1;
      for (xIndex = xPos; xIndex <= lastIndex; xIndex++) {
        changed = aChanged && (xIndex == xPos);
        BCData* bcData = nullptr;
        if (rgYPos < aCellMap.GetRowCount()) {
          cellData = (BCCellData*)aCellMap.GetDataAt(rgYPos, xIndex);
          if (!cellData) {  // add a dead cell data
            TableArea damageArea;
            cellData = (BCCellData*)aCellMap.AppendCell(*this, nullptr, rgYPos,
                                                        false, 0, damageArea);
            if (!cellData) ABORT0();
          }
        } else {
          // We are past this row group's content rows (note that mRows might
          // have an entry there for a rowspan extending past the row group).
          // Go to the next non-empty row group.
          NS_ASSERTION(aSide == LogicalSide::BEnd, "program error");
          nsCellMap* cellMap = aCellMap.GetNextSibling();
          while (cellMap && (0 == cellMap->GetRowCount())) {
            cellMap = cellMap->GetNextSibling();
          }
          if (cellMap) {
            cellData = (BCCellData*)cellMap->GetDataAt(0, xIndex);
            if (!cellData) {  // add a dead cell
              TableArea damageArea;
              cellData = (BCCellData*)cellMap->AppendCell(*this, nullptr, 0,
                                                          false, 0, damageArea);
            }
          } else {  // must be at the end of the table
            bcData = GetBEndMostBorder(xIndex);
          }
        }
        if (!bcData && cellData) {
          bcData = &cellData->mData;
        }
        if (bcData) {
          bcData->SetBStartEdge(aOwner, aSize, changed);
        } else {
          NS_ERROR("Cellmap: BStart edge not found");
        }
      }
      break;
    case LogicalSide::IEnd:
      xPos++;
      [[fallthrough]];
    case LogicalSide::IStart:
      // since bStart, bEnd borders were set, there should already be a cellData
      // entry
      lastIndex = rgYPos + aLength - 1;
      for (yIndex = rgYPos; yIndex <= lastIndex; yIndex++) {
        changed = aChanged && (yIndex == rgYPos);
        cellData = (BCCellData*)aCellMap.GetDataAt(yIndex, xPos);
        if (cellData) {
          cellData->mData.SetIStartEdge(aOwner, aSize, changed);
        } else {
          NS_ASSERTION(aSide == LogicalSide::IEnd, "program error");
          BCData* bcData = GetIEndMostBorder(yIndex + aCellMapStart);
          if (bcData) {
            bcData->SetIStartEdge(aOwner, aSize, changed);
          } else {
            NS_ERROR("Cellmap: IStart edge not found");
          }
        }
      }
      break;
  }
}

// store corner info (aOwner, aSubSize, aBevel). For aCorner = eBStartIStart,
// store the info at (aRowIndex, aColIndex). For eBStartIEnd, store it in the
// entry to the iEnd-wards where it would be BStartIStart. For eBEndIEnd, store
// it in the entry to the bEnd-wards. etc.
void nsTableCellMap::SetBCBorderCorner(LogicalCorner aCorner,
                                       nsCellMap& aCellMap,
                                       uint32_t aCellMapStart,
                                       uint32_t aRowIndex, uint32_t aColIndex,
                                       LogicalSide aOwner, nscoord aSubSize,
                                       bool aBevel, bool aIsBEndIEnd) {
  if (!mBCInfo) ABORT0();

  if (aIsBEndIEnd) {
    mBCInfo->mBEndIEndCorner.SetCorner(aSubSize, aOwner, aBevel);
    return;
  }

  int32_t xPos = aColIndex;
  int32_t yPos = aRowIndex;
  int32_t rgYPos = aRowIndex - aCellMapStart;

  if (LogicalCorner::BStartIEnd == aCorner) {
    xPos++;
  } else if (LogicalCorner::BEndIEnd == aCorner) {
    xPos++;
    rgYPos++;
    yPos++;
  } else if (LogicalCorner::BEndIStart == aCorner) {
    rgYPos++;
    yPos++;
  }

  BCCellData* cellData = nullptr;
  BCData* bcData = nullptr;
  if (GetColCount() <= xPos) {
    NS_ASSERTION(xPos == GetColCount(), "program error");
    // at the iEnd edge of the table as we checked the corner before
    NS_ASSERTION(!aIsBEndIEnd, "should be handled before");
    bcData = GetIEndMostBorder(yPos);
  } else if (rgYPos < aCellMap.GetRowCount()) {
    // See the comments in SetBCBorderEdge for why we need to check
    // GetRowCount() before null-checking cellData.
    cellData = (BCCellData*)aCellMap.GetDataAt(rgYPos, xPos);
    if (!cellData) {  // add a dead cell data
      TableArea damageArea;
      cellData = (BCCellData*)aCellMap.AppendCell(*this, nullptr, rgYPos, false,
                                                  0, damageArea);
    }
  } else {
    // try the next non empty row group
    nsCellMap* cellMap = aCellMap.GetNextSibling();
    while (cellMap && (0 == cellMap->GetRowCount())) {
      cellMap = cellMap->GetNextSibling();
    }
    if (cellMap) {
      cellData = (BCCellData*)cellMap->GetDataAt(0, xPos);
      if (!cellData) {  // add a dead cell
        TableArea damageArea;
        cellData = (BCCellData*)cellMap->AppendCell(*this, nullptr, 0, false, 0,
                                                    damageArea);
      }
    } else {  // must be at the bEnd of the table
      bcData = GetBEndMostBorder(xPos);
    }
  }
  if (!bcData && cellData) {
    bcData = &cellData->mData;
  }
  if (bcData) {
    bcData->SetCorner(aSubSize, aOwner, aBevel);
  } else {
    NS_ERROR("program error: Corner not found");
  }
}

nsCellMap::nsCellMap(nsTableRowGroupFrame* aRowGroup, bool aIsBC)
    : mRows(8),
      mContentRowCount(0),
      mRowGroupFrame(aRowGroup),
      mNextSibling(nullptr),
      mIsBC(aIsBC),
      mPresContext(aRowGroup->PresContext()) {
  MOZ_COUNT_CTOR(nsCellMap);
  NS_ASSERTION(mPresContext, "Must have prescontext");
}

nsCellMap::~nsCellMap() {
  MOZ_COUNT_DTOR(nsCellMap);

  uint32_t mapRowCount = mRows.Length();
  for (uint32_t rowX = 0; rowX < mapRowCount; rowX++) {
    CellDataArray& row = mRows[rowX];
    uint32_t colCount = row.Length();
    for (uint32_t colX = 0; colX < colCount; colX++) {
      DestroyCellData(row[colX]);
    }
  }
}

/* static */
void nsCellMap::Init() {
  MOZ_ASSERT(!sEmptyRow, "How did that happen?");
  sEmptyRow = new nsCellMap::CellDataArray();
}

/* static */
void nsCellMap::Shutdown() { sEmptyRow = nullptr; }

nsTableCellFrame* nsCellMap::GetCellFrame(int32_t aRowIndexIn,
                                          int32_t aColIndexIn, CellData& aData,
                                          bool aUseRowIfOverlap) const {
  int32_t rowIndex = aRowIndexIn - aData.GetRowSpanOffset();
  int32_t colIndex = aColIndexIn - aData.GetColSpanOffset();
  if (aData.IsOverlap()) {
    if (aUseRowIfOverlap) {
      colIndex = aColIndexIn;
    } else {
      rowIndex = aRowIndexIn;
    }
  }

  CellData* data =
      mRows.SafeElementAt(rowIndex, *sEmptyRow).SafeElementAt(colIndex);
  if (data) {
    return data->GetCellFrame();
  }
  return nullptr;
}

int32_t nsCellMap::GetHighestIndex(int32_t aColCount) {
  int32_t index = -1;
  int32_t rowCount = mRows.Length();
  for (int32_t rowIdx = 0; rowIdx < rowCount; rowIdx++) {
    const CellDataArray& row = mRows[rowIdx];

    for (int32_t colIdx = 0; colIdx < aColCount; colIdx++) {
      CellData* data = row.SafeElementAt(colIdx);
      // No data means row doesn't have more cells.
      if (!data) {
        break;
      }

      if (data->IsOrig()) {
        index++;
      }
    }
  }

  return index;
}

int32_t nsCellMap::GetIndexByRowAndColumn(int32_t aColCount, int32_t aRow,
                                          int32_t aColumn) const {
  if (uint32_t(aRow) >= mRows.Length()) {
    return -1;
  }

  int32_t index = -1;
  int32_t lastColsIdx = aColCount - 1;

  // Find row index of the cell where row span is started.
  const CellDataArray& row = mRows[aRow];
  CellData* data = row.SafeElementAt(aColumn);
  int32_t origRow = data ? aRow - data->GetRowSpanOffset() : aRow;

  // Calculate cell index.
  for (int32_t rowIdx = 0; rowIdx <= origRow; rowIdx++) {
    const CellDataArray& row = mRows[rowIdx];
    int32_t colCount = (rowIdx == origRow) ? aColumn : lastColsIdx;

    for (int32_t colIdx = 0; colIdx <= colCount; colIdx++) {
      data = row.SafeElementAt(colIdx);
      // No data means row doesn't have more cells.
      if (!data) {
        break;
      }

      if (data->IsOrig()) {
        index++;
      }
    }
  }

  // Given row and column don't point to the cell.
  if (!data) {
    return -1;
  }

  return index;
}

void nsCellMap::GetRowAndColumnByIndex(int32_t aColCount, int32_t aIndex,
                                       int32_t* aRow, int32_t* aColumn) const {
  *aRow = -1;
  *aColumn = -1;

  int32_t index = aIndex;
  int32_t rowCount = mRows.Length();

  for (int32_t rowIdx = 0; rowIdx < rowCount; rowIdx++) {
    const CellDataArray& row = mRows[rowIdx];

    for (int32_t colIdx = 0; colIdx < aColCount; colIdx++) {
      CellData* data = row.SafeElementAt(colIdx);

      // The row doesn't have more cells.
      if (!data) {
        break;
      }

      if (data->IsOrig()) {
        index--;
      }

      if (index < 0) {
        *aRow = rowIdx;
        *aColumn = colIdx;
        return;
      }
    }
  }
}

bool nsCellMap::Grow(nsTableCellMap& aMap, int32_t aNumRows,
                     int32_t aRowIndex) {
  NS_ASSERTION(aNumRows >= 1, "Why are we calling this?");

  // Get the number of cols we want to use for preallocating the row arrays.
  int32_t numCols = aMap.GetColCount();
  if (numCols == 0) {
    numCols = 4;
  }
  uint32_t startRowIndex = (aRowIndex >= 0) ? aRowIndex : mRows.Length();
  NS_ASSERTION(startRowIndex <= mRows.Length(), "Missing grow call inbetween");

  // XXX Change the return type of this function to void, or use a fallible
  // operation.
  mRows.InsertElementsAt(startRowIndex, aNumRows, numCols);
  return true;
}

void nsCellMap::GrowRow(CellDataArray& aRow, int32_t aNumCols)

{
  // Have to have the cast to get the template to do the right thing.
  aRow.InsertElementsAt(aRow.Length(), aNumCols, (CellData*)nullptr);
}

void nsCellMap::InsertRows(nsTableCellMap& aMap,
                           nsTArray<nsTableRowFrame*>& aRows,
                           int32_t aFirstRowIndex, bool aConsiderSpans,
                           int32_t aRgFirstRowIndex, TableArea& aDamageArea) {
  int32_t numCols = aMap.GetColCount();
  NS_ASSERTION(aFirstRowIndex >= 0,
               "nsCellMap::InsertRows called with negative rowIndex");
  if (uint32_t(aFirstRowIndex) > mRows.Length()) {
    // create (aFirstRowIndex - mRows.Length()) empty rows up to aFirstRowIndex
    int32_t numEmptyRows = aFirstRowIndex - mRows.Length();
    if (!Grow(aMap, numEmptyRows)) {
      return;
    }
  }

  if (!aConsiderSpans) {
    // update mContentRowCount, since non-empty rows will be added
    mContentRowCount = std::max(aFirstRowIndex, mContentRowCount);
    ExpandWithRows(aMap, aRows, aFirstRowIndex, aRgFirstRowIndex, aDamageArea);
    return;
  }

  // if any cells span into or out of the row being inserted, then rebuild
  bool spansCauseRebuild =
      CellsSpanInOrOut(aFirstRowIndex, aFirstRowIndex, 0, numCols - 1);

  // update mContentRowCount, since non-empty rows will be added
  mContentRowCount = std::max(aFirstRowIndex, mContentRowCount);

  // if any of the new cells span out of the new rows being added, then rebuild
  // XXX it would be better to only rebuild the portion of the map that follows
  // the new rows
  if (!spansCauseRebuild && (uint32_t(aFirstRowIndex) < mRows.Length())) {
    spansCauseRebuild = CellsSpanOut(aRows);
  }
  if (spansCauseRebuild) {
    aMap.RebuildConsideringRows(this, aFirstRowIndex, &aRows, 0, aDamageArea);
  } else {
    ExpandWithRows(aMap, aRows, aFirstRowIndex, aRgFirstRowIndex, aDamageArea);
  }
}

void nsCellMap::RemoveRows(nsTableCellMap& aMap, int32_t aFirstRowIndex,
                           int32_t aNumRowsToRemove, bool aConsiderSpans,
                           int32_t aRgFirstRowIndex, TableArea& aDamageArea) {
  int32_t numRows = mRows.Length();
  int32_t numCols = aMap.GetColCount();

  if (aFirstRowIndex >= numRows) {
    // reduce the content based row count based on the function arguments
    // as they are known to be real rows even if the cell map did not create
    // rows for them before.
    mContentRowCount -= aNumRowsToRemove;
    return;
  }
  if (!aConsiderSpans) {
    ShrinkWithoutRows(aMap, aFirstRowIndex, aNumRowsToRemove, aRgFirstRowIndex,
                      aDamageArea);
    return;
  }
  int32_t endRowIndex = aFirstRowIndex + aNumRowsToRemove - 1;
  if (endRowIndex >= numRows) {
    NS_ERROR("nsCellMap::RemoveRows tried to remove too many rows");
    endRowIndex = numRows - 1;
  }
  bool spansCauseRebuild =
      CellsSpanInOrOut(aFirstRowIndex, endRowIndex, 0, numCols - 1);
  if (spansCauseRebuild) {
    aMap.RebuildConsideringRows(this, aFirstRowIndex, nullptr, aNumRowsToRemove,
                                aDamageArea);
  } else {
    ShrinkWithoutRows(aMap, aFirstRowIndex, aNumRowsToRemove, aRgFirstRowIndex,
                      aDamageArea);
  }
}

CellData* nsCellMap::AppendCell(nsTableCellMap& aMap,
                                nsTableCellFrame* aCellFrame, int32_t aRowIndex,
                                bool aRebuildIfNecessary,
                                int32_t aRgFirstRowIndex,
                                TableArea& aDamageArea,
                                int32_t* aColToBeginSearch) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  int32_t origNumMapRows = mRows.Length();
  int32_t origNumCols = aMap.GetColCount();
  bool zeroRowSpan = false;
  int32_t rowSpan =
      (aCellFrame) ? GetRowSpanForNewCell(aCellFrame, aRowIndex, zeroRowSpan)
                   : 1;
  // add new rows if necessary
  int32_t endRowIndex = aRowIndex + rowSpan - 1;
  if (endRowIndex >= origNumMapRows) {
    // XXXbz handle allocation failures?
    Grow(aMap, 1 + endRowIndex - origNumMapRows);
  }

  // get the first null or dead CellData in the desired row. It will equal
  // origNumCols if there are none
  CellData* origData = nullptr;
  int32_t startColIndex = 0;
  if (aColToBeginSearch) {
    startColIndex = *aColToBeginSearch;
  }
  for (; startColIndex < origNumCols; startColIndex++) {
    CellData* data = GetDataAt(aRowIndex, startColIndex);
    if (!data) {
      break;
    }
    // The border collapse code relies on having multiple dead cell data entries
    // in a row.
    if (data->IsDead() && aCellFrame) {
      origData = data;
      break;
    }
  }
  // We found the place to append the cell, when the next cell is appended
  // the next search does not need to duplicate the search but can start
  // just at the next cell.
  if (aColToBeginSearch) {
    *aColToBeginSearch = startColIndex + 1;
  }

  int32_t colSpan = aCellFrame ? aCellFrame->GetColSpan() : 1;

  // if the new cell could potentially span into other rows and collide with
  // originating cells there, we will play it safe and just rebuild the map
  if (aRebuildIfNecessary && (aRowIndex < mContentRowCount - 1) &&
      (rowSpan > 1)) {
    AutoTArray<nsTableCellFrame*, 1> newCellArray;
    newCellArray.AppendElement(aCellFrame);
    aMap.RebuildConsideringCells(this, &newCellArray, aRowIndex, startColIndex,
                                 true, aDamageArea);
    return origData;
  }
  mContentRowCount = std::max(mContentRowCount, aRowIndex + 1);

  // add new cols to the table map if necessary
  int32_t endColIndex = startColIndex + colSpan - 1;
  if (endColIndex >= origNumCols) {
    NS_ASSERTION(aCellFrame, "dead cells should not require new columns");
    aMap.AddColsAtEnd(1 + endColIndex - origNumCols);
  }

  // Setup CellData for this cell
  if (origData) {
    NS_ASSERTION(origData->IsDead(),
                 "replacing a non dead cell is a memory leak");
    if (aCellFrame) {  // do nothing to replace a dead cell with a dead cell
      origData->Init(aCellFrame);
      // we are replacing a dead cell, increase the number of cells
      // originating at this column
      nsColInfo* colInfo = aMap.GetColInfoAt(startColIndex);
      NS_ASSERTION(colInfo, "access to a non existing column");
      if (colInfo) {
        colInfo->mNumCellsOrig++;
      }
    }
  } else {
    origData = AllocCellData(aCellFrame);
    if (!origData) ABORT1(origData);
    SetDataAt(aMap, *origData, aRowIndex, startColIndex);
  }

  if (aRebuildIfNecessary) {
    // the caller depends on the damageArea
    // The special case for zeroRowSpan is to adjust for the '2' in
    // GetRowSpanForNewCell.
    uint32_t height = std::min(zeroRowSpan ? rowSpan - 1 : rowSpan,
                               GetRowCount() - aRowIndex);
    SetDamageArea(startColIndex, aRgFirstRowIndex + aRowIndex,
                  1 + endColIndex - startColIndex, height, aDamageArea);
  }

  if (!aCellFrame) {
    return origData;
  }

  // initialize the cell frame
  aCellFrame->SetColIndex(startColIndex);

  // Create CellData objects for the rows that this cell spans. Set
  // their mOrigCell to nullptr and their mSpanData to point to data.
  for (int32_t rowX = aRowIndex; rowX <= endRowIndex; rowX++) {
    // The row at rowX will need to have at least endColIndex columns
    mRows[rowX].SetCapacity(endColIndex);
    for (int32_t colX = startColIndex; colX <= endColIndex; colX++) {
      if ((rowX != aRowIndex) ||
          (colX != startColIndex)) {  // skip orig cell data done above
        CellData* cellData = GetDataAt(rowX, colX);
        if (cellData) {
          if (cellData->IsOrig()) {
            NS_ERROR("cannot overlap originating cell");
            continue;
          }
          if (rowX > aRowIndex) {  // row spanning into cell
            if (cellData->IsRowSpan()) {
              // do nothing, this can be caused by rowspan which is overlapped
              // by a another cell with a rowspan and a colspan
            } else {
              cellData->SetRowSpanOffset(rowX - aRowIndex);
              if (zeroRowSpan) {
                cellData->SetZeroRowSpan(true);
              }
            }
          }
          if (colX > startColIndex) {  // col spanning into cell
            if (!cellData->IsColSpan()) {
              if (cellData->IsRowSpan()) {
                cellData->SetOverlap(true);
              }
              cellData->SetColSpanOffset(colX - startColIndex);
              nsColInfo* colInfo = aMap.GetColInfoAt(colX);
              colInfo->mNumCellsSpan++;
            }
          }
        } else {
          cellData = AllocCellData(nullptr);
          if (!cellData) {
            return origData;
          }
          if (rowX > aRowIndex) {
            cellData->SetRowSpanOffset(rowX - aRowIndex);
            if (zeroRowSpan) {
              cellData->SetZeroRowSpan(true);
            }
          }
          if (colX > startColIndex) {
            cellData->SetColSpanOffset(colX - startColIndex);
          }
          SetDataAt(aMap, *cellData, rowX, colX);
        }
      }
    }
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("appended cell=%p row=%d \n", aCellFrame, aRowIndex);
  aMap.Dump();
#endif
  return origData;
}

bool nsCellMap::CellsSpanOut(nsTArray<nsTableRowFrame*>& aRows) const {
  int32_t numNewRows = aRows.Length();
  for (int32_t rowX = 0; rowX < numNewRows; rowX++) {
    nsTableRowFrame* rowFrame = aRows.ElementAt(rowX);
    for (nsTableCellFrame* cellFrame = rowFrame->GetFirstCell(); cellFrame;
         cellFrame = cellFrame->GetNextCell()) {
      bool zeroSpan;
      int32_t rowSpan = GetRowSpanForNewCell(cellFrame, rowX, zeroSpan);
      if (zeroSpan || rowX + rowSpan > numNewRows) {
        return true;
      }
    }
  }
  return false;
}

// return true if any cells have rows spans into or out of the region
// defined by the row and col indices or any cells have colspans into the region
bool nsCellMap::CellsSpanInOrOut(int32_t aStartRowIndex, int32_t aEndRowIndex,
                                 int32_t aStartColIndex,
                                 int32_t aEndColIndex) const {
  /*
   * this routine will watch the cells adjacent to the region or at the edge
   * they are marked with *. The routine will verify whether they span in or
   * are spanned out.
   *
   *                           startCol          endCol
   *             r1c1   r1c2   r1c3      r1c4    r1c5    r1rc6  r1c7
   *  startrow   r2c1   r2c2  *r2c3     *r2c4   *r2c5   *r2rc6  r2c7
   *  endrow     r3c1   r3c2  *r3c3      r3c4    r3c5   *r3rc6  r3c7
   *             r4c1   r4c2  *r4c3     *r4c4   *r4c5    r4rc6  r4c7
   *             r5c1   r5c2   r5c3      r5c4    r5c5    r5rc6  r5c7
   */

  int32_t numRows = mRows.Length();  // use the cellmap rows to determine the
                                     // current cellmap extent.
  for (int32_t colX = aStartColIndex; colX <= aEndColIndex; colX++) {
    CellData* cellData;
    if (aStartRowIndex > 0) {
      cellData = GetDataAt(aStartRowIndex, colX);
      if (cellData && (cellData->IsRowSpan())) {
        return true;  // there is a row span into the region
      }
      if ((aStartRowIndex >= mContentRowCount) && (mContentRowCount > 0)) {
        cellData = GetDataAt(mContentRowCount - 1, colX);
        if (cellData && cellData->IsZeroRowSpan()) {
          return true;  // When we expand the zerospan it'll span into our row
        }
      }
    }
    if (aEndRowIndex < numRows - 1) {  // is there anything below aEndRowIndex
      cellData = GetDataAt(aEndRowIndex + 1, colX);
      if ((cellData) && (cellData->IsRowSpan())) {
        return true;  // there is a row span out of the region
      }
    } else {
      cellData = GetDataAt(aEndRowIndex, colX);
      if ((cellData) && (cellData->IsRowSpan()) &&
          (mContentRowCount < numRows)) {
        return true;  // this cell might be the cause of a dead row
      }
    }
  }
  if (aStartColIndex > 0) {
    for (int32_t rowX = aStartRowIndex; rowX <= aEndRowIndex; rowX++) {
      CellData* cellData = GetDataAt(rowX, aStartColIndex);
      if (cellData && (cellData->IsColSpan())) {
        return true;  // there is a col span into the region
      }
      cellData = GetDataAt(rowX, aEndColIndex + 1);
      if (cellData && (cellData->IsColSpan())) {
        return true;  // there is a col span out of the region
      }
    }
  }
  return false;
}

void nsCellMap::InsertCells(nsTableCellMap& aMap,
                            nsTArray<nsTableCellFrame*>& aCellFrames,
                            int32_t aRowIndex, int32_t aColIndexBefore,
                            int32_t aRgFirstRowIndex, TableArea& aDamageArea) {
  if (aCellFrames.Length() == 0) {
    return;
  }
  NS_ASSERTION(aColIndexBefore >= -1, "index out of range");
  int32_t numCols = aMap.GetColCount();
  if (aColIndexBefore >= numCols) {
    NS_ERROR(
        "Inserting instead of appending cells indicates a serious cellmap "
        "error");
    aColIndexBefore = numCols - 1;
  }

  // get the starting col index of the 1st new cells
  int32_t startColIndex;
  for (startColIndex = aColIndexBefore + 1; startColIndex < numCols;
       startColIndex++) {
    CellData* data = GetDataAt(aRowIndex, startColIndex);
    if (!data || data->IsOrig() || data->IsDead()) {
      // // Not a span.  Stop.
      break;
    }
  }

  // record whether inserted cells are going to cause complications due
  // to existing row spans, col spans or table sizing.
  bool spansCauseRebuild = false;

  // check that all cells have the same row span
  int32_t numNewCells = aCellFrames.Length();
  bool zeroRowSpan = false;
  int32_t rowSpan = 0;
  for (int32_t cellX = 0; cellX < numNewCells; cellX++) {
    nsTableCellFrame* cell = aCellFrames.ElementAt(cellX);
    int32_t rowSpan2 = GetRowSpanForNewCell(cell, aRowIndex, zeroRowSpan);
    if (rowSpan == 0) {
      rowSpan = rowSpan2;
    } else if (rowSpan != rowSpan2) {
      spansCauseRebuild = true;
      break;
    }
  }

  // check if the new cells will cause the table to add more rows
  if (!spansCauseRebuild) {
    if (mRows.Length() < uint32_t(aRowIndex + rowSpan)) {
      spansCauseRebuild = true;
    }
  }

  if (!spansCauseRebuild) {
    spansCauseRebuild = CellsSpanInOrOut(aRowIndex, aRowIndex + rowSpan - 1,
                                         startColIndex, numCols - 1);
  }
  if (spansCauseRebuild) {
    aMap.RebuildConsideringCells(this, &aCellFrames, aRowIndex, startColIndex,
                                 true, aDamageArea);
  } else {
    ExpandWithCells(aMap, aCellFrames, aRowIndex, startColIndex, rowSpan,
                    zeroRowSpan, aRgFirstRowIndex, aDamageArea);
  }
}

void nsCellMap::ExpandWithRows(nsTableCellMap& aMap,
                               nsTArray<nsTableRowFrame*>& aRowFrames,
                               int32_t aStartRowIndexIn,
                               int32_t aRgFirstRowIndex,
                               TableArea& aDamageArea) {
  int32_t startRowIndex = (aStartRowIndexIn >= 0) ? aStartRowIndexIn : 0;
  NS_ASSERTION(uint32_t(startRowIndex) <= mRows.Length(),
               "caller should have grown cellmap before");

  int32_t numNewRows = aRowFrames.Length();
  mContentRowCount += numNewRows;

  int32_t endRowIndex = startRowIndex + numNewRows - 1;

  // shift the rows after startRowIndex down and insert empty rows that will
  // be filled via the AppendCell call below
  if (!Grow(aMap, numNewRows, startRowIndex)) {
    return;
  }

  int32_t newRowIndex = 0;
  for (int32_t rowX = startRowIndex; rowX <= endRowIndex; rowX++) {
    nsTableRowFrame* rFrame = aRowFrames.ElementAt(newRowIndex);
    // append cells
    int32_t colIndex = 0;
    for (nsTableCellFrame* cellFrame = rFrame->GetFirstCell(); cellFrame;
         cellFrame = cellFrame->GetNextCell()) {
      AppendCell(aMap, cellFrame, rowX, false, aRgFirstRowIndex, aDamageArea,
                 &colIndex);
    }
    newRowIndex++;
  }
  // mark all following rows damaged, they might contain a previously set
  // damage area which we can not shift.
  int32_t firstDamagedRow = aRgFirstRowIndex + startRowIndex;
  SetDamageArea(0, firstDamagedRow, aMap.GetColCount(),
                aMap.GetRowCount() - firstDamagedRow, aDamageArea);
}

void nsCellMap::ExpandWithCells(nsTableCellMap& aMap,
                                nsTArray<nsTableCellFrame*>& aCellFrames,
                                int32_t aRowIndex, int32_t aColIndex,
                                int32_t aRowSpan,  // same for all cells
                                bool aRowSpanIsZero, int32_t aRgFirstRowIndex,
                                TableArea& aDamageArea) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  int32_t endRowIndex = aRowIndex + aRowSpan - 1;
  int32_t startColIndex = aColIndex;
  int32_t endColIndex = aColIndex;
  int32_t numCells = aCellFrames.Length();
  int32_t totalColSpan = 0;

  // add cellData entries for the space taken up by the new cells
  for (int32_t cellX = 0; cellX < numCells; cellX++) {
    nsTableCellFrame* cellFrame = aCellFrames.ElementAt(cellX);
    CellData* origData = AllocCellData(cellFrame);  // the originating cell
    if (!origData) {
      return;
    }

    // set the starting and ending col index for the new cell
    int32_t colSpan = cellFrame->GetColSpan();
    totalColSpan += colSpan;
    if (cellX == 0) {
      endColIndex = aColIndex + colSpan - 1;
    } else {
      startColIndex = endColIndex + 1;
      endColIndex = startColIndex + colSpan - 1;
    }

    // add the originating cell data and any cell data corresponding to row/col
    // spans
    for (int32_t rowX = aRowIndex; rowX <= endRowIndex; rowX++) {
      CellDataArray& row = mRows[rowX];
      // Pre-allocate all the cells we'll need in this array, setting
      // them to null.
      // Have to have the cast to get the template to do the right thing.
      int32_t insertionIndex = row.Length();
      if (insertionIndex > startColIndex) {
        insertionIndex = startColIndex;
      }
      row.InsertElementsAt(insertionIndex, endColIndex - insertionIndex + 1,
                           (CellData*)nullptr);

      for (int32_t colX = startColIndex; colX <= endColIndex; colX++) {
        CellData* data = origData;
        if ((rowX != aRowIndex) || (colX != startColIndex)) {
          data = AllocCellData(nullptr);
          if (!data) {
            return;
          }
          if (rowX > aRowIndex) {
            data->SetRowSpanOffset(rowX - aRowIndex);
            if (aRowSpanIsZero) {
              data->SetZeroRowSpan(true);
            }
          }
          if (colX > startColIndex) {
            data->SetColSpanOffset(colX - startColIndex);
          }
        }
        SetDataAt(aMap, *data, rowX, colX);
      }
    }
    cellFrame->SetColIndex(startColIndex);
  }
  int32_t damageHeight =
      std::max(0, std::min(GetRowGroup()->GetRowCount() - aRowIndex, aRowSpan));
  SetDamageArea(aColIndex, aRgFirstRowIndex + aRowIndex,
                1 + endColIndex - aColIndex, damageHeight, aDamageArea);

  int32_t rowX;

  // update the row and col info due to shifting
  for (rowX = aRowIndex; rowX <= endRowIndex; rowX++) {
    CellDataArray& row = mRows[rowX];
    uint32_t numCols = row.Length();
    uint32_t colX;
    for (colX = aColIndex + totalColSpan; colX < numCols; colX++) {
      CellData* data = row[colX];
      if (data) {
        // increase the origin and span counts beyond the spanned cols
        if (data->IsOrig()) {
          // a cell that gets moved needs adjustment as well as it new
          // orignating col
          data->GetCellFrame()->SetColIndex(colX);
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsOrig++;
        }
        if (data->IsColSpan()) {
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsSpan++;
        }

        // decrease the origin and span counts within the spanned cols
        int32_t colX2 = colX - totalColSpan;
        nsColInfo* colInfo2 = aMap.GetColInfoAt(colX2);
        if (data->IsOrig()) {
          // the old originating col of a moved cell needs adjustment
          colInfo2->mNumCellsOrig--;
        }
        if (data->IsColSpan()) {
          colInfo2->mNumCellsSpan--;
        }
      }
    }
  }
}

void nsCellMap::ShrinkWithoutRows(nsTableCellMap& aMap, int32_t aStartRowIndex,
                                  int32_t aNumRowsToRemove,
                                  int32_t aRgFirstRowIndex,
                                  TableArea& aDamageArea) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  int32_t endRowIndex = aStartRowIndex + aNumRowsToRemove - 1;
  uint32_t colCount = aMap.GetColCount();
  for (int32_t rowX = endRowIndex; rowX >= aStartRowIndex; --rowX) {
    CellDataArray& row = mRows[rowX];
    uint32_t colX;
    for (colX = 0; colX < colCount; colX++) {
      CellData* data = row.SafeElementAt(colX);
      if (data) {
        // Adjust the column counts.
        if (data->IsOrig()) {
          // Decrement the column count.
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsOrig--;
        }
        // colspan=0 is only counted as a spanned cell in the 1st col it spans
        else if (data->IsColSpan()) {
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsSpan--;
        }
      }
    }

    uint32_t rowLength = row.Length();
    // Delete our row information.
    for (colX = 0; colX < rowLength; colX++) {
      DestroyCellData(row[colX]);
    }

    mRows.RemoveElementAt(rowX);

    // Decrement our row and next available index counts.
    mContentRowCount--;
  }
  aMap.RemoveColsAtEnd();
  // mark all following rows damaged, they might contain a previously set
  // damage area which we can not shift.
  int32_t firstDamagedRow = aRgFirstRowIndex + aStartRowIndex;
  SetDamageArea(0, firstDamagedRow, aMap.GetColCount(),
                aMap.GetRowCount() - firstDamagedRow, aDamageArea);
}

int32_t nsCellMap::GetEffectiveColSpan(const nsTableCellMap& aMap,
                                       int32_t aRowIndex,
                                       int32_t aColIndex) const {
  int32_t numColsInTable = aMap.GetColCount();
  int32_t colSpan = 1;
  if (uint32_t(aRowIndex) >= mRows.Length()) {
    return colSpan;
  }

  const CellDataArray& row = mRows[aRowIndex];
  int32_t colX;
  CellData* data;
  int32_t maxCols = numColsInTable;
  bool hitOverlap = false;  // XXX this is not ever being set to true
  for (colX = aColIndex + 1; colX < maxCols; colX++) {
    data = row.SafeElementAt(colX);
    if (data) {
      // for an overlapping situation get the colspan from the originating cell
      // and use that as the max number of cols to iterate. Since this is rare,
      // only pay the price of looking up the cell's colspan here.
      if (!hitOverlap && data->IsOverlap()) {
        CellData* origData = row.SafeElementAt(aColIndex);
        if (origData && origData->IsOrig()) {
          nsTableCellFrame* cellFrame = origData->GetCellFrame();
          if (cellFrame) {
            // possible change the number of colums to iterate
            maxCols = std::min(aColIndex + cellFrame->GetColSpan(), maxCols);
            if (colX >= maxCols) {
              break;
            }
          }
        }
      }
      if (data->IsColSpan()) {
        colSpan++;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  // Enforce that the effective colSpan is between 1 and MAX_COLSPAN:
  return std::clamp(colSpan, 1, MAX_COLSPAN);
}

int32_t nsCellMap::GetRowSpanForNewCell(nsTableCellFrame* aCellFrameToAdd,
                                        int32_t aRowIndex,
                                        bool& aIsZeroRowSpan) const {
  aIsZeroRowSpan = false;
  int32_t rowSpan = aCellFrameToAdd->GetRowSpan();
  if (0 == rowSpan) {
    // Use a min value of 2 for a zero rowspan to make computations easier
    // elsewhere. Zero rowspans are only content dependent!
    rowSpan = std::clamp(mContentRowCount - aRowIndex, 2, MAX_ROWSPAN);
    aIsZeroRowSpan = true;
  }
  return rowSpan;
}

bool nsCellMap::HasMoreThanOneCell(int32_t aRowIndex) const {
  const CellDataArray& row = mRows.SafeElementAt(aRowIndex, *sEmptyRow);
  uint32_t maxColIndex = row.Length();
  uint32_t colIndex;
  bool foundOne = false;
  for (colIndex = 0; colIndex < maxColIndex; colIndex++) {
    CellData* cellData = row[colIndex];
    if (cellData && (cellData->GetCellFrame() || cellData->IsRowSpan())) {
      if (foundOne) {
        return true;
      }
      foundOne = true;
    }
  }
  return false;
}

int32_t nsCellMap::GetNumCellsOriginatingInRow(int32_t aRowIndex) const {
  const CellDataArray& row = mRows.SafeElementAt(aRowIndex, *sEmptyRow);
  uint32_t count = 0;
  uint32_t maxColIndex = row.Length();
  uint32_t colIndex;
  for (colIndex = 0; colIndex < maxColIndex; colIndex++) {
    CellData* cellData = row[colIndex];
    if (cellData && cellData->IsOrig()) {
      count++;
    }
  }
  return count;
}

int32_t nsCellMap::GetRowSpan(int32_t aRowIndex, int32_t aColIndex,
                              bool aGetEffective) const {
  int32_t rowSpan = 1;
  int32_t rowCount = (aGetEffective) ? mContentRowCount : mRows.Length();
  int32_t rowX;
  for (rowX = aRowIndex + 1; rowX < rowCount; rowX++) {
    CellData* data = GetDataAt(rowX, aColIndex);
    if (data) {
      if (data->IsRowSpan()) {
        rowSpan++;
      } else {
        break;
      }
    } else {
      break;
    }
  }

  // Enforce that the effective rowSpan is between 1 and MAX_ROWSPAN:
  return std::clamp(rowSpan, 1, MAX_ROWSPAN);
}

void nsCellMap::ShrinkWithoutCell(nsTableCellMap& aMap,
                                  nsTableCellFrame& aCellFrame,
                                  int32_t aRowIndex, int32_t aColIndex,
                                  int32_t aRgFirstRowIndex,
                                  TableArea& aDamageArea) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  uint32_t colX, rowX;

  // get the rowspan and colspan from the cell map since the content may have
  // changed
  int32_t rowSpan = GetRowSpan(aRowIndex, aColIndex, true);
  uint32_t colSpan = GetEffectiveColSpan(aMap, aRowIndex, aColIndex);
  uint32_t endRowIndex = aRowIndex + rowSpan - 1;
  uint32_t endColIndex = aColIndex + colSpan - 1;

  // adjust the col counts due to the deleted cell before removing it
  for (colX = aColIndex; colX <= endColIndex; colX++) {
    nsColInfo* colInfo = aMap.GetColInfoAt(colX);
    if (colX == uint32_t(aColIndex)) {
      colInfo->mNumCellsOrig--;
    } else {
      colInfo->mNumCellsSpan--;
    }
  }

  // remove the deleted cell and cellData entries for it
  for (rowX = aRowIndex; rowX <= endRowIndex; rowX++) {
    CellDataArray& row = mRows[rowX];

    // endIndexForRow points at the first slot we don't want to clean up.  This
    // makes the aColIndex == 0 case work right with our unsigned int colX.
    NS_ASSERTION(endColIndex + 1 <= row.Length(), "span beyond the row size!");
    uint32_t endIndexForRow = std::min(endColIndex + 1, uint32_t(row.Length()));

    // Since endIndexForRow <= row.Length(), enough to compare aColIndex to it.
    if (uint32_t(aColIndex) < endIndexForRow) {
      for (colX = endIndexForRow; colX > uint32_t(aColIndex); colX--) {
        DestroyCellData(row[colX - 1]);
      }
      row.RemoveElementsAt(aColIndex, endIndexForRow - aColIndex);
    }
  }

  uint32_t numCols = aMap.GetColCount();

  // update the row and col info due to shifting
  for (rowX = aRowIndex; rowX <= endRowIndex; rowX++) {
    CellDataArray& row = mRows[rowX];
    for (colX = aColIndex; colX < numCols - colSpan; colX++) {
      CellData* data = row.SafeElementAt(colX);
      if (data) {
        if (data->IsOrig()) {
          // a cell that gets moved to the left needs adjustment in its new
          // location
          data->GetCellFrame()->SetColIndex(colX);
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsOrig++;
          // a cell that gets moved to the left needs adjustment in its old
          // location
          colInfo = aMap.GetColInfoAt(colX + colSpan);
          if (colInfo) {
            colInfo->mNumCellsOrig--;
          }
        }

        else if (data->IsColSpan()) {
          // a cell that gets moved to the left needs adjustment
          // in its new location
          nsColInfo* colInfo = aMap.GetColInfoAt(colX);
          colInfo->mNumCellsSpan++;
          // a cell that gets moved to the left needs adjustment
          // in its old location
          colInfo = aMap.GetColInfoAt(colX + colSpan);
          if (colInfo) {
            colInfo->mNumCellsSpan--;
          }
        }
      }
    }
  }
  aMap.RemoveColsAtEnd();
  SetDamageArea(aColIndex, aRgFirstRowIndex + aRowIndex,
                std::max(0, aMap.GetColCount() - aColIndex - 1),
                1 + endRowIndex - aRowIndex, aDamageArea);
}

void nsCellMap::RebuildConsideringRows(
    nsTableCellMap& aMap, int32_t aStartRowIndex,
    nsTArray<nsTableRowFrame*>* aRowsToInsert, int32_t aNumRowsToRemove) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  // copy the old cell map into a new array
  uint32_t numOrigRows = mRows.Length();
  nsTArray<CellDataArray> origRows = std::move(mRows);

  int32_t rowNumberChange;
  if (aRowsToInsert) {
    rowNumberChange = aRowsToInsert->Length();
  } else {
    rowNumberChange = -aNumRowsToRemove;
  }

  // adjust mContentRowCount based on the function arguments as they are known
  // to be real rows.
  mContentRowCount += rowNumberChange;
  NS_ASSERTION(mContentRowCount >= 0, "previous mContentRowCount was wrong");
  // mRows is empty now.  Grow it to the size we expect it to have.
  if (mContentRowCount) {
    if (!Grow(aMap, mContentRowCount)) {
      // Bail, I guess...  Not sure what else we can do here.
      return;
    }
  }

  // aStartRowIndex might be after all existing rows so we should limit the
  // copy to the amount of exisiting rows
  uint32_t copyEndRowIndex = std::min(numOrigRows, uint32_t(aStartRowIndex));

  // rowX keeps track of where we are in mRows while setting up the
  // new cellmap.
  uint32_t rowX = 0;
  TableArea damageArea;
  // put back the rows before the affected ones just as before.  Note that we
  // can't just copy the old rows in bit-for-bit, because they might be
  // spanning out into the rows we're adding/removing.
  for (; rowX < copyEndRowIndex; rowX++) {
    const CellDataArray& row = origRows[rowX];
    uint32_t numCols = row.Length();
    for (uint32_t colX = 0; colX < numCols; colX++) {
      // put in the original cell from the cell map
      const CellData* data = row.ElementAt(colX);
      if (data && data->IsOrig()) {
        AppendCell(aMap, data->GetCellFrame(), rowX, false, 0, damageArea);
      }
    }
  }

  // Now handle the new rows being inserted, if any.
  uint32_t copyStartRowIndex;
  rowX = aStartRowIndex;
  if (aRowsToInsert) {
    // add in the new cells and create rows if necessary
    int32_t numNewRows = aRowsToInsert->Length();
    for (int32_t newRowX = 0; newRowX < numNewRows; newRowX++) {
      nsTableRowFrame* rFrame = aRowsToInsert->ElementAt(newRowX);
      for (nsTableCellFrame* cellFrame = rFrame->GetFirstCell(); cellFrame;
           cellFrame = cellFrame->GetNextCell()) {
        AppendCell(aMap, cellFrame, rowX, false, 0, damageArea);
      }
      rowX++;
    }
    copyStartRowIndex = aStartRowIndex;
  } else {
    copyStartRowIndex = aStartRowIndex + aNumRowsToRemove;
  }

  // put back the rows after the affected ones just as before.  Again, we can't
  // just copy the old bits because that would not handle the new rows spanning
  // out or our earlier old rows spanning through the damaged area.
  for (uint32_t copyRowX = copyStartRowIndex; copyRowX < numOrigRows;
       copyRowX++) {
    const CellDataArray& row = origRows[copyRowX];
    uint32_t numCols = row.Length();
    for (uint32_t colX = 0; colX < numCols; colX++) {
      // put in the original cell from the cell map
      CellData* data = row.ElementAt(colX);
      if (data && data->IsOrig()) {
        AppendCell(aMap, data->GetCellFrame(), rowX, false, 0, damageArea);
      }
    }
    rowX++;
  }

  // delete the old cell map.  Now rowX no longer has anything to do with mRows
  for (rowX = 0; rowX < numOrigRows; rowX++) {
    CellDataArray& row = origRows[rowX];
    uint32_t len = row.Length();
    for (uint32_t colX = 0; colX < len; colX++) {
      DestroyCellData(row[colX]);
    }
  }
}

void nsCellMap::RebuildConsideringCells(
    nsTableCellMap& aMap, int32_t aNumOrigCols,
    nsTArray<nsTableCellFrame*>* aCellFrames, int32_t aRowIndex,
    int32_t aColIndex, bool aInsert) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  // copy the old cell map into a new array
  int32_t numOrigRows = mRows.Length();
  nsTArray<CellDataArray> origRows = std::move(mRows);

  int32_t numNewCells = (aCellFrames) ? aCellFrames->Length() : 0;

  // the new cells might extend the previous column number
  NS_ASSERTION(aNumOrigCols >= aColIndex,
               "Appending cells far beyond cellmap data?!");
  int32_t numCols =
      aInsert ? std::max(aNumOrigCols, aColIndex + 1) : aNumOrigCols;

  // build the new cell map.  Hard to say what, if anything, we can preallocate
  // here...  Should come back to that sometime, perhaps.
  int32_t rowX;
  TableArea damageArea;
  for (rowX = 0; rowX < numOrigRows; rowX++) {
    const CellDataArray& row = origRows[rowX];
    for (int32_t colX = 0; colX < numCols; colX++) {
      if ((rowX == aRowIndex) && (colX == aColIndex)) {
        if (aInsert) {  // put in the new cells
          for (int32_t cellX = 0; cellX < numNewCells; cellX++) {
            nsTableCellFrame* cell = aCellFrames->ElementAt(cellX);
            if (cell) {
              AppendCell(aMap, cell, rowX, false, 0, damageArea);
            }
          }
        } else {
          continue;  // do not put the deleted cell back
        }
      }
      // put in the original cell from the cell map
      CellData* data = row.SafeElementAt(colX);
      if (data && data->IsOrig()) {
        AppendCell(aMap, data->GetCellFrame(), rowX, false, 0, damageArea);
      }
    }
  }
  if (aInsert &&
      numOrigRows <=
          aRowIndex) {  // append the new cells below the last original row
    NS_ASSERTION(numOrigRows == aRowIndex,
                 "Appending cells far beyond the last row");
    for (int32_t cellX = 0; cellX < numNewCells; cellX++) {
      nsTableCellFrame* cell = aCellFrames->ElementAt(cellX);
      if (cell) {
        AppendCell(aMap, cell, aRowIndex, false, 0, damageArea);
      }
    }
  }

  // delete the old cell map
  for (rowX = 0; rowX < numOrigRows; rowX++) {
    CellDataArray& row = origRows[rowX];
    uint32_t len = row.Length();
    for (uint32_t colX = 0; colX < len; colX++) {
      DestroyCellData(row.SafeElementAt(colX));
    }
  }
  // expand the cellmap to cover empty content rows
  if (mRows.Length() < uint32_t(mContentRowCount)) {
    Grow(aMap, mContentRowCount - mRows.Length());
  }
}

void nsCellMap::RemoveCell(nsTableCellMap& aMap, nsTableCellFrame* aCellFrame,
                           int32_t aRowIndex, int32_t aRgFirstRowIndex,
                           TableArea& aDamageArea) {
  uint32_t numRows = mRows.Length();
  if (uint32_t(aRowIndex) >= numRows) {
    NS_ERROR("bad arg in nsCellMap::RemoveCell");
    return;
  }
  int32_t numCols = aMap.GetColCount();

  // Now aRowIndex is guaranteed OK.

  // get the starting col index of the cell to remove
  int32_t startColIndex;
  for (startColIndex = 0; startColIndex < numCols; startColIndex++) {
    CellData* data = mRows[aRowIndex].SafeElementAt(startColIndex);
    if (data && (data->IsOrig()) && (aCellFrame == data->GetCellFrame())) {
      break;  // we found the col index
    }
  }

  int32_t rowSpan = GetRowSpan(aRowIndex, startColIndex, false);
  // record whether removing the cells is going to cause complications due
  // to existing row spans, col spans or table sizing.
  bool spansCauseRebuild = CellsSpanInOrOut(aRowIndex, aRowIndex + rowSpan - 1,
                                            startColIndex, numCols - 1);
  // XXX if the cell has a col span to the end of the map, and the end has no
  // originating cells, we need to assume that this the only such cell, and
  // rebuild so that there are no extraneous cols at the end. The same is true
  // for removing rows.
  if (!spansCauseRebuild) {
    if (!aCellFrame->GetRowSpan() || !aCellFrame->GetColSpan()) {
      spansCauseRebuild = true;
    }
  }

  if (spansCauseRebuild) {
    aMap.RebuildConsideringCells(this, nullptr, aRowIndex, startColIndex, false,
                                 aDamageArea);
  } else {
    ShrinkWithoutCell(aMap, *aCellFrame, aRowIndex, startColIndex,
                      aRgFirstRowIndex, aDamageArea);
  }
}

#ifdef DEBUG
void nsCellMap::Dump(bool aIsBorderCollapse) const {
  printf("\n  ***** START GROUP CELL MAP DUMP ***** %p\n", (void*)this);
  nsTableRowGroupFrame* rg = GetRowGroup();
  const nsStyleDisplay* display = rg->StyleDisplay();
  switch (display->DisplayInside()) {
    case StyleDisplayInside::TableHeaderGroup:
      printf("  thead ");
      break;
    case StyleDisplayInside::TableFooterGroup:
      printf("  tfoot ");
      break;
    case StyleDisplayInside::TableRowGroup:
      printf("  tbody ");
      break;
    default:
      printf("HUH? wrong display type on rowgroup");
  }
  uint32_t mapRowCount = mRows.Length();
  printf("mapRowCount=%u tableRowCount=%d\n", mapRowCount, mContentRowCount);

  uint32_t rowIndex, colIndex;
  for (rowIndex = 0; rowIndex < mapRowCount; rowIndex++) {
    const CellDataArray& row = mRows[rowIndex];
    printf("  row %d : ", rowIndex);
    uint32_t colCount = row.Length();
    for (colIndex = 0; colIndex < colCount; colIndex++) {
      CellData* cd = row[colIndex];
      if (cd) {
        if (cd->IsOrig()) {
          printf("C%d,%d  ", rowIndex, colIndex);
        } else {
          if (cd->IsRowSpan()) {
            printf("R ");
          }
          if (cd->IsColSpan()) {
            printf("C ");
          }
          if (!(cd->IsRowSpan() && cd->IsColSpan())) {
            printf("  ");
          }
          printf("  ");
        }
      } else {
        printf("----  ");
      }
    }
    if (aIsBorderCollapse) {
      nscoord size;
      BCBorderOwner owner;
      LogicalSide side;
      bool segStart;
      bool bevel;
      for (int32_t i = 0; i <= 2; i++) {
        printf("\n          ");
        for (colIndex = 0; colIndex < colCount; colIndex++) {
          BCCellData* cd = (BCCellData*)row[colIndex];
          if (cd) {
            if (0 == i) {
              size = cd->mData.GetBStartEdge(owner, segStart);
              printf("t=%d%d%d ", int32_t(size), owner, segStart);
            } else if (1 == i) {
              size = cd->mData.GetIStartEdge(owner, segStart);
              printf("l=%d%d%d ", int32_t(size), owner, segStart);
            } else {
              size = cd->mData.GetCorner(side, bevel);
              printf("c=%d%hhu%d ", int32_t(size), static_cast<uint8_t>(side),
                     bevel);
            }
          }
        }
      }
    }
    printf("\n");
  }

  // output info mapping Ci,j to cell address
  for (uint32_t rIndex = 0; rIndex < mapRowCount; rIndex++) {
    const CellDataArray& row = mRows[rIndex];
    uint32_t colCount = row.Length();
    printf("  ");
    for (colIndex = 0; colIndex < colCount; colIndex++) {
      CellData* cd = row[colIndex];
      if (cd) {
        if (cd->IsOrig()) {
          nsTableCellFrame* cellFrame = cd->GetCellFrame();
          uint32_t cellFrameColIndex = cellFrame->ColIndex();
          printf("C%d,%d=%p(%u)  ", rIndex, colIndex, (void*)cellFrame,
                 cellFrameColIndex);
        }
      }
    }
    printf("\n");
  }

  printf("  ***** END GROUP CELL MAP DUMP *****\n");
}
#endif

CellData* nsCellMap::GetDataAt(int32_t aMapRowIndex, int32_t aColIndex) const {
  return mRows.SafeElementAt(aMapRowIndex, *sEmptyRow).SafeElementAt(aColIndex);
}

// only called if the cell at aMapRowIndex, aColIndex is null or dead
// (the latter from ExpandZeroColSpans (XXXmats which has now been removed -
// are there other ways cells may be dead?)).
void nsCellMap::SetDataAt(nsTableCellMap& aMap, CellData& aNewCell,
                          int32_t aMapRowIndex, int32_t aColIndex) {
  NS_ASSERTION(!!aMap.mBCInfo == mIsBC, "BC state mismatch");
  if (uint32_t(aMapRowIndex) >= mRows.Length()) {
    NS_ERROR("SetDataAt called with row index > num rows");
    return;
  }

  CellDataArray& row = mRows[aMapRowIndex];

  // the table map may need cols added
  int32_t numColsToAdd = aColIndex + 1 - aMap.GetColCount();
  if (numColsToAdd > 0) {
    aMap.AddColsAtEnd(numColsToAdd);
  }
  // the row may need cols added
  numColsToAdd = aColIndex + 1 - row.Length();
  if (numColsToAdd > 0) {
    // XXXbz need to handle allocation failures.
    GrowRow(row, numColsToAdd);
  }

  DestroyCellData(row[aColIndex]);

  row.ReplaceElementsAt(aColIndex, 1, &aNewCell);
  // update the originating cell counts if cell originates in this row, col
  nsColInfo* colInfo = aMap.GetColInfoAt(aColIndex);
  if (colInfo) {
    if (aNewCell.IsOrig()) {
      colInfo->mNumCellsOrig++;
    } else if (aNewCell.IsColSpan()) {
      colInfo->mNumCellsSpan++;
    }
  } else {
    NS_ERROR("SetDataAt called with col index > table map num cols");
  }
}

nsTableCellFrame* nsCellMap::GetCellInfoAt(const nsTableCellMap& aMap,
                                           int32_t aRowX, int32_t aColX,
                                           bool* aOriginates,
                                           int32_t* aColSpan) const {
  if (aOriginates) {
    *aOriginates = false;
  }
  CellData* data = GetDataAt(aRowX, aColX);
  nsTableCellFrame* cellFrame = nullptr;
  if (data) {
    if (data->IsOrig()) {
      cellFrame = data->GetCellFrame();
      if (aOriginates) {
        *aOriginates = true;
      }
    } else {
      cellFrame = GetCellFrame(aRowX, aColX, *data, true);
    }
    if (cellFrame && aColSpan) {
      uint32_t initialColIndex = cellFrame->ColIndex();
      *aColSpan = GetEffectiveColSpan(aMap, aRowX, initialColIndex);
    }
  }
  return cellFrame;
}

bool nsCellMap::RowIsSpannedInto(int32_t aRowIndex, int32_t aNumEffCols) const {
  if ((0 > aRowIndex) || (aRowIndex >= mContentRowCount)) {
    return false;
  }
  for (int32_t colIndex = 0; colIndex < aNumEffCols; colIndex++) {
    CellData* cd = GetDataAt(aRowIndex, colIndex);
    if (cd) {              // there's really a cell at (aRowIndex, colIndex)
      if (cd->IsSpan()) {  // the cell at (aRowIndex, colIndex) is the result of
                           // a span
        if (cd->IsRowSpan() && GetCellFrame(aRowIndex, colIndex, *cd,
                                            true)) {  // XXX why the last check
          return true;
        }
      }
    }
  }
  return false;
}

bool nsCellMap::RowHasSpanningCells(int32_t aRowIndex,
                                    int32_t aNumEffCols) const {
  if ((0 > aRowIndex) || (aRowIndex >= mContentRowCount)) {
    return false;
  }
  if (aRowIndex != mContentRowCount - 1) {
    // aRowIndex is not the last row, so we check the next row after aRowIndex
    // for spanners
    for (int32_t colIndex = 0; colIndex < aNumEffCols; colIndex++) {
      CellData* cd = GetDataAt(aRowIndex, colIndex);
      if (cd && (cd->IsOrig())) {  // cell originates
        CellData* cd2 = GetDataAt(aRowIndex + 1, colIndex);
        if (cd2 && cd2->IsRowSpan()) {  // cd2 is spanned by a row
          if (cd->GetCellFrame() ==
              GetCellFrame(aRowIndex + 1, colIndex, *cd2, true)) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

void nsCellMap::DestroyCellData(CellData* aData) {
  if (!aData) {
    return;
  }

  if (mIsBC) {
    BCCellData* bcData = static_cast<BCCellData*>(aData);
    bcData->~BCCellData();
    FrameArena::Free(eArenaObjectID_BCCellData, bcData);
  } else {
    aData->~CellData();
    FrameArena::Free(eArenaObjectID_CellData, aData);
  }
}

CellData* nsCellMap::AllocCellData(nsTableCellFrame* aOrigCell) {
  if (mIsBC) {
    BCCellData* data = (BCCellData*)FrameArena::Allocate(
        eArenaObjectID_BCCellData, sizeof(BCCellData));
    if (data) {
      new (data) BCCellData(aOrigCell);
    }
    return data;
  }

  CellData* data = (CellData*)FrameArena::Allocate(eArenaObjectID_CellData,
                                                   sizeof(CellData));
  if (data) {
    new (data) CellData(aOrigCell);
  }
  return data;
}

void nsCellMapColumnIterator::AdvanceRowGroup() {
  do {
    mCurMapStart += mCurMapContentRowCount;
    mCurMap = mCurMap->GetNextSibling();
    if (!mCurMap) {
      // Set mCurMapContentRowCount and mCurMapRelevantRowCount to 0 in case
      // mCurMap has no next sibling.  This can happen if we just handled the
      // last originating cell.  Future calls will end up with mFoundCells ==
      // mOrigCells, but for this one mFoundCells was definitely not big enough
      // if we got here.
      mCurMapContentRowCount = 0;
      mCurMapRelevantRowCount = 0;
      break;
    }

    mCurMapContentRowCount = mCurMap->GetRowCount();
    uint32_t rowArrayLength = mCurMap->mRows.Length();
    mCurMapRelevantRowCount = std::min(mCurMapContentRowCount, rowArrayLength);
  } while (0 == mCurMapRelevantRowCount);

  NS_ASSERTION(mCurMapRelevantRowCount != 0 || !mCurMap,
               "How did that happen?");

  // Set mCurMapRow to 0, since cells can't span across table row groups.
  mCurMapRow = 0;
}

void nsCellMapColumnIterator::IncrementRow(int32_t aIncrement) {
  MOZ_ASSERT(aIncrement >= 0, "Bogus increment");
  MOZ_ASSERT(mCurMap, "Bogus mOrigCells?");
  if (aIncrement == 0) {
    AdvanceRowGroup();
  } else {
    mCurMapRow += aIncrement;
    if (mCurMapRow >= mCurMapRelevantRowCount) {
      AdvanceRowGroup();
    }
  }
}

nsTableCellFrame* nsCellMapColumnIterator::GetNextFrame(int32_t* aRow,
                                                        int32_t* aColSpan) {
  // Fast-path for the case when we don't have anything left in the column and
  // we know it.
  if (mFoundCells == mOrigCells) {
    *aRow = 0;
    *aColSpan = 1;
    return nullptr;
  }

  while (true) {
    NS_ASSERTION(mCurMapRow < mCurMapRelevantRowCount, "Bogus mOrigCells?");
    // Safe to just get the row (which is faster than calling GetDataAt(), but
    // there may not be that many cells in it, so have to use SafeElementAt for
    // the mCol.
    const nsCellMap::CellDataArray& row = mCurMap->mRows[mCurMapRow];
    CellData* cellData = row.SafeElementAt(mCol);
    if (!cellData || cellData->IsDead()) {
      // Could hit this if there are fewer cells in this row than others, for
      // example.
      IncrementRow(1);
      continue;
    }

    if (cellData->IsColSpan()) {
      // Look up the originating data for this cell, advance by its relative
      // rowspan.
      int32_t rowspanOffset = cellData->GetRowSpanOffset();
      nsTableCellFrame* cellFrame =
          mCurMap->GetCellFrame(mCurMapRow, mCol, *cellData, false);
      NS_ASSERTION(cellFrame, "Must have usable originating data here");
      int32_t rowSpan = cellFrame->GetRowSpan();
      if (rowSpan == 0) {
        AdvanceRowGroup();
      } else {
        IncrementRow(rowSpan - rowspanOffset);
      }
      continue;
    }

    NS_ASSERTION(cellData->IsOrig(),
                 "Must have originating cellData by this point.  "
                 "See comment on mCurMapRow in header.");

    nsTableCellFrame* cellFrame = cellData->GetCellFrame();
    NS_ASSERTION(cellFrame, "Orig data without cellframe?");

    *aRow = mCurMapStart + mCurMapRow;
    *aColSpan = mCurMap->GetEffectiveColSpan(*mMap, mCurMapRow, mCol);

    IncrementRow(cellFrame->GetRowSpan());

    ++mFoundCells;

    MOZ_ASSERT(cellData == mMap->GetDataAt(*aRow, mCol),
               "Giving caller bogus row?");

    return cellFrame;
  }

  MOZ_ASSERT_UNREACHABLE("Can't get here");
  return nullptr;
}
