/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTableFrame.h"

#include <algorithm>

#include "BasicTableLayoutStrategy.h"
#include "FixedTableLayoutStrategy.h"
#include "gfxContext.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Likely.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/Range.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/WritingModes.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "nsCOMPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSProps.h"
#include "nsCSSRendering.h"
#include "nsCellMap.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsFrameList.h"
#include "nsFrameManager.h"
#include "nsHTMLParts.h"
#include "nsIContent.h"
#include "nsIFrameInlines.h"
#include "nsIScriptError.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsStyleChangeList.h"
#include "nsStyleConsts.h"
#include "nsTableCellFrame.h"
#include "nsTableColFrame.h"
#include "nsTableColGroupFrame.h"
#include "nsTableRowFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsTableWrapperFrame.h"

using namespace mozilla;
using namespace mozilla::image;
using namespace mozilla::layout;

using mozilla::gfx::AutoRestoreTransform;
using mozilla::gfx::DrawTarget;
using mozilla::gfx::Float;
using mozilla::gfx::ToDeviceColor;

namespace mozilla {

struct TableReflowInput final {
  TableReflowInput(const ReflowInput& aReflowInput,
                   const LogicalMargin& aBorderPadding, TableReflowMode aMode)
      : mReflowInput(aReflowInput),
        mWM(aReflowInput.GetWritingMode()),
        mAvailSize(mWM) {
    MOZ_ASSERT(mReflowInput.mFrame->IsTableFrame(),
               "TableReflowInput should only be created for nsTableFrame");
    auto* table = static_cast<nsTableFrame*>(mReflowInput.mFrame);

    mICoord = aBorderPadding.IStart(mWM) + table->GetColSpacing(-1);
    mAvailSize.ISize(mWM) =
        std::max(0, mReflowInput.ComputedISize() - table->GetColSpacing(-1) -
                        table->GetColSpacing(table->GetColCount()));

    mAvailSize.BSize(mWM) = aMode == TableReflowMode::Measuring
                                ? NS_UNCONSTRAINEDSIZE
                                : mReflowInput.AvailableBSize();
    AdvanceBCoord(aBorderPadding.BStart(mWM) +
                  (!table->GetPrevInFlow() ? table->GetRowSpacing(-1) : 0));
    if (aReflowInput.mStyleBorder->mBoxDecorationBreak ==
        StyleBoxDecorationBreak::Clone) {
      // At this point, we're assuming we won't be the last fragment, so we only
      // reserve space for block-end border-padding if we're cloning it on each
      // fragment; and we don't need to reserve any row-spacing for this
      // hypothetical fragmentation, either.
      ReduceAvailableBSizeBy(aBorderPadding.BEnd(mWM));
    }
  }

  // Advance to the next block-offset and reduce the available block-size.
  void AdvanceBCoord(nscoord aAmount) {
    mBCoord += aAmount;
    ReduceAvailableBSizeBy(aAmount);
  }

  const LogicalSize& AvailableSize() const { return mAvailSize; }

  // The real reflow input of the table frame.
  const ReflowInput& mReflowInput;

  // Stationary inline-offset, which won't change after the constructor.
  nscoord mICoord = 0;

  // Running block-offset, which will be adjusted as we reflow children.
  nscoord mBCoord = 0;

 private:
  void ReduceAvailableBSizeBy(nscoord aAmount) {
    if (mAvailSize.BSize(mWM) == NS_UNCONSTRAINEDSIZE) {
      return;
    }
    mAvailSize.BSize(mWM) -= aAmount;
    mAvailSize.BSize(mWM) = std::max(0, mAvailSize.BSize(mWM));
  }

  // mReflowInput's (i.e. table frame's) writing-mode.
  WritingMode mWM;

  // The available size for children. The inline-size is stationary after the
  // constructor, but the block-size will be adjusted as we reflow children.
  LogicalSize mAvailSize;
};

struct TableBCData final {
  TableArea mDamageArea;
  nscoord mBStartBorderWidth = 0;
  nscoord mIEndBorderWidth = 0;
  nscoord mBEndBorderWidth = 0;
  nscoord mIStartBorderWidth = 0;
};

}  // namespace mozilla

/********************************************************************************
 ** nsTableFrame **
 ********************************************************************************/

ComputedStyle* nsTableFrame::GetParentComputedStyle(
    nsIFrame** aProviderFrame) const {
  // Since our parent, the table wrapper frame, returned this frame, we
  // must return whatever our parent would normally have returned.

  MOZ_ASSERT(GetParent(), "table constructed without table wrapper");
  if (!mContent->GetParent() && !Style()->IsPseudoOrAnonBox()) {
    // We're the root.  We have no ComputedStyle parent.
    *aProviderFrame = nullptr;
    return nullptr;
  }

  return GetParent()->DoGetParentComputedStyle(aProviderFrame);
}

nsTableFrame::nsTableFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                           ClassID aID)
    : nsContainerFrame(aStyle, aPresContext, aID) {
  memset(&mBits, 0, sizeof(mBits));
}

void nsTableFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(!mCellMap, "Init called twice");
  MOZ_ASSERT(!mTableLayoutStrategy, "Init called twice");
  MOZ_ASSERT(!aPrevInFlow || aPrevInFlow->IsTableFrame(),
             "prev-in-flow must be of same type");

  // Let the base class do its processing
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  // see if border collapse is on, if so set it
  const nsStyleTableBorder* tableStyle = StyleTableBorder();
  bool borderCollapse =
      (StyleBorderCollapse::Collapse == tableStyle->mBorderCollapse);
  SetBorderCollapse(borderCollapse);
  if (borderCollapse) {
    SetNeedToCalcHasBCBorders(true);
  }

  if (!aPrevInFlow) {
    // If we're the first-in-flow, we manage the cell map & layout strategy that
    // get used by our continuation chain:
    mCellMap = MakeUnique<nsTableCellMap>(*this, borderCollapse);
    if (IsAutoLayout()) {
      mTableLayoutStrategy = MakeUnique<BasicTableLayoutStrategy>(this);
    } else {
      mTableLayoutStrategy = MakeUnique<FixedTableLayoutStrategy>(this);
    }
  } else {
    // Set my isize, because all frames in a table flow are the same isize and
    // code in nsTableWrapperFrame depends on this being set.
    WritingMode wm = GetWritingMode();
    SetSize(LogicalSize(wm, aPrevInFlow->ISize(wm), BSize(wm)));
  }
}

// Define here (Rather than in the header), even if it's trival, to avoid
// UniquePtr members causing compile errors when their destructors are
// implicitly inserted into this destructor. Destruction requires
// the full definition of types that these UniquePtrs are managing, and
// the header only has forward declarations of them.
nsTableFrame::~nsTableFrame() = default;

void nsTableFrame::Destroy(DestroyContext& aContext) {
  MOZ_ASSERT(!mBits.mIsDestroying);
  mBits.mIsDestroying = true;
  nsContainerFrame::Destroy(aContext);
}

static bool IsRepeatedFrame(nsIFrame* kidFrame) {
  return (kidFrame->IsTableRowFrame() || kidFrame->IsTableRowGroupFrame()) &&
         kidFrame->HasAnyStateBits(NS_REPEATED_ROW_OR_ROWGROUP);
}

bool nsTableFrame::PageBreakAfter(nsIFrame* aSourceFrame,
                                  nsIFrame* aNextFrame) {
  const nsStyleDisplay* display = aSourceFrame->StyleDisplay();
  nsTableRowGroupFrame* prevRg = do_QueryFrame(aSourceFrame);
  // don't allow a page break after a repeated element ...
  if ((display->BreakAfter() || (prevRg && prevRg->HasInternalBreakAfter())) &&
      !IsRepeatedFrame(aSourceFrame)) {
    return !(aNextFrame && IsRepeatedFrame(aNextFrame));  // or before
  }

  if (aNextFrame) {
    display = aNextFrame->StyleDisplay();
    // don't allow a page break before a repeated element ...
    nsTableRowGroupFrame* nextRg = do_QueryFrame(aNextFrame);
    if ((display->BreakBefore() ||
         (nextRg && nextRg->HasInternalBreakBefore())) &&
        !IsRepeatedFrame(aNextFrame)) {
      return !IsRepeatedFrame(aSourceFrame);  // or after
    }
  }
  return false;
}

/* static */
void nsTableFrame::PositionedTablePartMaybeChanged(nsContainerFrame* aFrame,
                                                   ComputedStyle* aOldStyle) {
  const bool wasPositioned =
      aOldStyle && aOldStyle->IsAbsPosContainingBlock(aFrame);
  const bool isPositioned = aFrame->IsAbsPosContainingBlock();
  MOZ_ASSERT(isPositioned == aFrame->Style()->IsAbsPosContainingBlock(aFrame));
  if (wasPositioned == isPositioned) {
    return;
  }

  nsTableFrame* tableFrame = GetTableFrame(aFrame);
  MOZ_ASSERT(tableFrame, "Should have a table frame here");
  tableFrame = static_cast<nsTableFrame*>(tableFrame->FirstContinuation());

  // Retrieve the positioned parts array for this table (lazily creating it
  // if it doesn't exist yet).
  TablePartsArray* positionedParts =
      tableFrame->GetOrCreateDeletableProperty(PositionedTablePartsProperty());

  if (isPositioned) {
    // Add this frame to the list.
    positionedParts->AppendElement(aFrame);
  } else {
    positionedParts->RemoveElement(aFrame);
  }
}

/* static */
void nsTableFrame::MaybeUnregisterPositionedTablePart(
    nsContainerFrame* aFrame) {
  if (!aFrame->IsAbsPosContainingBlock()) {
    return;
  }
  nsTableFrame* tableFrame = GetTableFrame(aFrame);
  tableFrame = static_cast<nsTableFrame*>(tableFrame->FirstContinuation());

  if (tableFrame->IsDestroying()) {
    return;  // We're throwing the table away anyways.
  }

  // Retrieve the positioned parts array for this table.
  TablePartsArray* positionedParts =
      tableFrame->GetProperty(PositionedTablePartsProperty());

  // Remove the frame.
  MOZ_ASSERT(
      positionedParts && positionedParts->Contains(aFrame),
      "Asked to unregister a positioned table part that wasn't registered");
  if (positionedParts) {
    positionedParts->RemoveElement(aFrame);
  }
}

void nsTableFrame::SetInitialChildList(ChildListID aListID,
                                       nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  if (aListID != FrameChildListID::Principal) {
    return;
  }

  // If we have a prev-in-flow, then we're a table that has been split and
  // so don't treat this like an append
  if (!GetPrevInFlow()) {
    // process col groups first so that real cols get constructed before
    // anonymous ones due to cells in rows.
    InsertColGroups(0, mFrames);
    InsertRowGroups(mFrames);
    // calc collapsing borders
    if (IsBorderCollapse()) {
      SetFullBCDamageArea();
    }
  }
}

void nsTableFrame::RowOrColSpanChanged(nsTableCellFrame* aCellFrame) {
  if (!aCellFrame) {
    return;
  }
  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap) {
    return;
  }
  // for now just remove the cell from the map and reinsert it
  uint32_t rowIndex = aCellFrame->RowIndex();
  uint32_t colIndex = aCellFrame->ColIndex();
  RemoveCell(aCellFrame, rowIndex);
  AutoTArray<nsTableCellFrame*, 1> cells;
  cells.AppendElement(aCellFrame);
  InsertCells(cells, rowIndex, colIndex - 1);

  // XXX Should this use IntrinsicDirty::FrameAncestorsAndDescendants? It
  // currently doesn't need to, but it might given more optimization.
  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_IS_DIRTY);
}

/* ****** CellMap methods ******* */

/* return the effective col count */
int32_t nsTableFrame::GetEffectiveColCount() const {
  int32_t colCount = GetColCount();
  if (LayoutStrategy()->GetType() == nsITableLayoutStrategy::Auto) {
    nsTableCellMap* cellMap = GetCellMap();
    if (!cellMap) {
      return 0;
    }
    // don't count cols at the end that don't have originating cells
    for (int32_t colIdx = colCount - 1; colIdx >= 0; colIdx--) {
      if (cellMap->GetNumCellsOriginatingInCol(colIdx) > 0) {
        break;
      }
      colCount--;
    }
  }
  return colCount;
}

int32_t nsTableFrame::GetIndexOfLastRealCol() {
  int32_t numCols = mColFrames.Length();
  if (numCols > 0) {
    for (int32_t colIdx = numCols - 1; colIdx >= 0; colIdx--) {
      nsTableColFrame* colFrame = GetColFrame(colIdx);
      if (colFrame) {
        if (eColAnonymousCell != colFrame->GetColType()) {
          return colIdx;
        }
      }
    }
  }
  return -1;
}

nsTableColFrame* nsTableFrame::GetColFrame(int32_t aColIndex) const {
  MOZ_ASSERT(!GetPrevInFlow(), "GetColFrame called on next in flow");
  int32_t numCols = mColFrames.Length();
  if ((aColIndex >= 0) && (aColIndex < numCols)) {
    MOZ_ASSERT(mColFrames.ElementAt(aColIndex));
    return mColFrames.ElementAt(aColIndex);
  } else {
    MOZ_ASSERT_UNREACHABLE("invalid col index");
    return nullptr;
  }
}

int32_t nsTableFrame::GetEffectiveRowSpan(int32_t aRowIndex,
                                          const nsTableCellFrame& aCell) const {
  nsTableCellMap* cellMap = GetCellMap();
  MOZ_ASSERT(nullptr != cellMap, "bad call, cellMap not yet allocated.");

  return cellMap->GetEffectiveRowSpan(aRowIndex, aCell.ColIndex());
}

int32_t nsTableFrame::GetEffectiveRowSpan(const nsTableCellFrame& aCell,
                                          nsCellMap* aCellMap) {
  nsTableCellMap* tableCellMap = GetCellMap();
  if (!tableCellMap) ABORT1(1);

  uint32_t colIndex = aCell.ColIndex();
  uint32_t rowIndex = aCell.RowIndex();

  if (aCellMap) {
    return aCellMap->GetRowSpan(rowIndex, colIndex, true);
  }
  return tableCellMap->GetEffectiveRowSpan(rowIndex, colIndex);
}

int32_t nsTableFrame::GetEffectiveColSpan(const nsTableCellFrame& aCell,
                                          nsCellMap* aCellMap) const {
  nsTableCellMap* tableCellMap = GetCellMap();
  if (!tableCellMap) ABORT1(1);

  uint32_t colIndex = aCell.ColIndex();
  uint32_t rowIndex = aCell.RowIndex();

  if (aCellMap) {
    return aCellMap->GetEffectiveColSpan(*tableCellMap, rowIndex, colIndex);
  }
  return tableCellMap->GetEffectiveColSpan(rowIndex, colIndex);
}

bool nsTableFrame::HasMoreThanOneCell(int32_t aRowIndex) const {
  nsTableCellMap* tableCellMap = GetCellMap();
  if (!tableCellMap) ABORT1(1);
  return tableCellMap->HasMoreThanOneCell(aRowIndex);
}

void nsTableFrame::AdjustRowIndices(int32_t aRowIndex, int32_t aAdjustment) {
  // Iterate over the row groups and adjust the row indices of all rows
  // whose index is >= aRowIndex.
  for (nsTableRowGroupFrame* rg : OrderedGroups().mRowGroups) {
    rg->AdjustRowIndices(aRowIndex, aAdjustment);
  }
}

void nsTableFrame::ResetRowIndices(
    const nsFrameList::Slice& aRowGroupsToExclude) {
  // Iterate over the row groups and adjust the row indices of all rows
  // omit the rowgroups that will be inserted later
  mDeletedRowIndexRanges.clear();

  RowGroupArray rowGroups = OrderedRowGroups();

  nsTHashSet<nsTableRowGroupFrame*> excludeRowGroups;
  for (nsIFrame* excludeRowGroup : aRowGroupsToExclude) {
    if (nsTableRowGroupFrame* rg = do_QueryFrame(excludeRowGroup)) {
      excludeRowGroups.Insert(rg);
#ifdef DEBUG
      {
        // Check to make sure that the row indices of all rows in excluded row
        // groups are '0' (i.e. the initial value since they haven't been added
        // yet)
        const nsFrameList& rowFrames = excludeRowGroup->PrincipalChildList();
        for (nsIFrame* r : rowFrames) {
          auto* row = static_cast<nsTableRowFrame*>(r);
          MOZ_ASSERT(
              row->GetRowIndex() == 0,
              "exclusions cannot be used for rows that were already added,"
              "because we'd need to process mDeletedRowIndexRanges");
        }
      }
#endif
    }
  }

  int32_t rowIndex = 0;
  for (uint32_t rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
    if (!excludeRowGroups.Contains(rgFrame)) {
      const nsFrameList& rowFrames = rgFrame->PrincipalChildList();
      for (nsIFrame* r : rowFrames) {
        if (mozilla::StyleDisplay::TableRow == r->StyleDisplay()->mDisplay) {
          auto* row = static_cast<nsTableRowFrame*>(r);
          row->SetRowIndex(rowIndex);
          rowIndex++;
        }
      }
    }
  }
}

void nsTableFrame::InsertColGroups(int32_t aStartColIndex,
                                   const nsFrameList::Slice& aNewFrames) {
  auto colIndex = aStartColIndex;
  nsIFrame* lastFrame = nullptr;
  for (nsIFrame* f : aNewFrames) {
    lastFrame = f;
    nsTableColGroupFrame* cg = do_QueryFrame(f);
    if (!cg || cg->IsSynthetic()) {
      continue;
    }
    cg->SetStartColumnIndex(colIndex);
    cg->AddColsToTable(colIndex, false, cg->PrincipalChildList());
    int32_t numCols = cg->GetColCount();
    colIndex += numCols;
  }
  auto* next = lastFrame ? lastFrame->GetNextSibling() : nullptr;
  nsTableColGroupFrame::ResetColIndices(next, GetSyntheticColGroup(), colIndex);
#ifdef DEBUG
  VerifyColFrames();
#endif
}

void nsTableFrame::InsertCol(nsTableColFrame& aColFrame, int32_t aColIndex) {
  mColFrames.InsertElementAt(aColIndex, &aColFrame);
  nsTableColType insertedColType = aColFrame.GetColType();
  int32_t numCacheCols = mColFrames.Length();
  if (nsTableCellMap* cellMap = GetCellMap()) {
    int32_t numMapCols = cellMap->GetColCount();
    if (numCacheCols > numMapCols) {
      bool removedFromCache = false;
      if (eColAnonymousCell != insertedColType) {
        if (nsTableColFrame* lastCol = mColFrames.ElementAt(numCacheCols - 1)) {
          nsTableColType lastColType = lastCol->GetColType();
          if (eColAnonymousCell == lastColType) {
            // remove the col from the cache
            mColFrames.RemoveLastElement();
            if (mSyntheticColGroup) {
              MOZ_ASSERT(mSyntheticColGroup->IsSynthetic());
              DestroyContext context(PresShell());
              mSyntheticColGroup->RemoveChild(context, *lastCol, false);

              // remove the col group if it is empty
              if (mSyntheticColGroup->GetColCount() <= 0) {
                mFrames.DestroyFrame(context, mSyntheticColGroup);
                mSyntheticColGroup = nullptr;
              }
            }
            removedFromCache = true;
          }
        }
      }
      if (!removedFromCache) {
        cellMap->AddColsAtEnd(1);
      }
    }
  }
  // for now, just bail and recalc all of the collapsing borders
  if (IsBorderCollapse()) {
    TableArea damageArea(aColIndex, 0, GetColCount() - aColIndex,
                         GetRowCount());
    AddBCDamageArea(damageArea);
  }
}

void nsTableFrame::RemoveCol(int32_t aColIndex, bool aRemoveFromCache,
                             bool aRemoveFromCellMap) {
  if (aRemoveFromCache) {
    mColFrames.RemoveElementAt(aColIndex);
  }
  if (aRemoveFromCellMap) {
    nsTableCellMap* cellMap = GetCellMap();
    if (cellMap) {
      // If we have some anonymous cols at the end already, we just
      // add a new anonymous col.
      if (!mColFrames.IsEmpty() &&
          mColFrames.LastElement() &&  // XXXbz is this ever null?
          mColFrames.LastElement()->GetColType() == eColAnonymousCell) {
        AppendAnonymousColFrames(1);
      } else {
        // All of our colframes correspond to actual <col> tags.  It's possible
        // that we still have at least as many <col> tags as we have logical
        // columns from cells, but we might have one less.  Handle the latter
        // case as follows: First ask the cellmap to drop its last col if it
        // doesn't have any actual cells in it.  Then call
        // MatchCellMapToColCache to append an anonymous column if it's needed;
        // this needs to be after RemoveColsAtEnd, since it will determine the
        // need for a new column frame based on the width of the cell map.
        cellMap->RemoveColsAtEnd();
        MatchCellMapToColCache(cellMap);
      }
    }
  }
  // for now, just bail and recalc all of the collapsing borders
  if (IsBorderCollapse()) {
    SetFullBCDamageArea();
  }
}

/** Get the cell map for this table frame.  It is not always mCellMap.
 * Only the first-in-flow has a legit cell map.
 */
nsTableCellMap* nsTableFrame::GetCellMap() const {
  return static_cast<nsTableFrame*>(FirstInFlow())->mCellMap.get();
}

nsTableColGroupFrame* nsTableFrame::CreateSyntheticColGroupFrame() {
  nsIContent* colGroupContent = GetContent();
  mozilla::PresShell* presShell = PresShell();

  RefPtr<ComputedStyle> colGroupStyle;
  colGroupStyle = presShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
      PseudoStyleType::MozTableColumnGroup);
  // Create a col group frame
  nsTableColGroupFrame* newFrame =
      NS_NewTableColGroupFrame(presShell, colGroupStyle);
  newFrame->SetIsSynthetic();
  newFrame->Init(colGroupContent, this, nullptr);
  return newFrame;
}

int32_t nsTableFrame::GetRealColEnd() const {
  for (auto* col : Reversed(mColFrames)) {
    auto* cg = static_cast<nsTableColGroupFrame*>(col->GetParent());
    if (!cg->IsSynthetic()) {
      return col->GetColIndex() + 1;
    }
  }
  return 0;
}

static int32_t GetColStartAfter(nsIFrame* aPrevSibling) {
  for (nsIFrame* f = aPrevSibling; f; f = f->GetPrevSibling()) {
    if (nsTableColGroupFrame* cg = do_QueryFrame(f)) {
      if (!cg->IsSynthetic()) {
        return cg->GetStartColumnIndex() + cg->GetColCount();
      }
    }
  }
  return 0;
}

void nsTableFrame::AppendAnonymousColFrames(int32_t aNumColsToAdd) {
  MOZ_ASSERT(aNumColsToAdd > 0, "We should be adding _something_.");
  if (!mSyntheticColGroup) {
    int32_t colIndex = GetRealColEnd();
    mSyntheticColGroup = CreateSyntheticColGroupFrame();
    // add the new frame to the child list
    mFrames.AppendFrame(this, mSyntheticColGroup);
    mSyntheticColGroup->SetStartColumnIndex(colIndex);
  }
  AppendAnonymousColFrames(mSyntheticColGroup, aNumColsToAdd, eColAnonymousCell,
                           true);
}

// XXX this needs to be moved to nsCSSFrameConstructor
// Right now it only creates the col frames at the end
void nsTableFrame::AppendAnonymousColFrames(
    nsTableColGroupFrame* aColGroupFrame, int32_t aNumColsToAdd,
    nsTableColType aColType, bool aAddToTable) {
  MOZ_ASSERT(aColGroupFrame, "null frame");
  MOZ_ASSERT(aColType != eColAnonymousCol, "Shouldn't happen");
  MOZ_ASSERT(aNumColsToAdd > 0, "We should be adding _something_.");

  mozilla::PresShell* presShell = PresShell();

  // Get the last col frame
  nsFrameList newColFrames;

  int32_t startIndex = mColFrames.Length();
  int32_t lastIndex = startIndex + aNumColsToAdd - 1;

  for (int32_t childX = startIndex; childX <= lastIndex; childX++) {
    // all anonymous cols that we create here use a pseudo ComputedStyle of the
    // col group
    nsIContent* iContent = aColGroupFrame->GetContent();
    RefPtr<ComputedStyle> computedStyle =
        presShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
            PseudoStyleType::MozTableColumn);
    // ASSERTION to check for bug 54454 sneaking back in...
    NS_ASSERTION(iContent, "null content in CreateAnonymousColFrames");

    // create the new col frame
    nsIFrame* colFrame = NS_NewTableColFrame(presShell, computedStyle);
    ((nsTableColFrame*)colFrame)->SetColType(aColType);
    colFrame->Init(iContent, aColGroupFrame, nullptr);

    newColFrames.AppendFrame(nullptr, colFrame);
  }
  nsFrameList& cols = aColGroupFrame->GetWritableChildList();
  nsIFrame* oldLastCol = cols.LastChild();
  const nsFrameList::Slice& newCols =
      cols.InsertFrames(nullptr, oldLastCol, std::move(newColFrames));
  if (aAddToTable) {
    // get the starting col index in the cache
    int32_t startColIndex;
    if (oldLastCol) {
      startColIndex =
          static_cast<nsTableColFrame*>(oldLastCol)->GetColIndex() + 1;
    } else {
      startColIndex = aColGroupFrame->GetStartColumnIndex();
    }

    aColGroupFrame->AddColsToTable(startColIndex, true, newCols);
  }
}

void nsTableFrame::MatchCellMapToColCache(nsTableCellMap* aCellMap) {
  int32_t numColsInMap = GetColCount();
  int32_t numColsInCache = mColFrames.Length();
  int32_t numColsToAdd = numColsInMap - numColsInCache;
  if (numColsToAdd > 0) {
    // this sets the child list, updates the col cache and cell map
    AppendAnonymousColFrames(numColsToAdd);
  }
  if (numColsToAdd < 0) {
    int32_t numColsNotRemoved = DestroyAnonymousColFrames(-numColsToAdd);
    // if the cell map has fewer cols than the cache, correct it
    if (numColsNotRemoved > 0) {
      aCellMap->AddColsAtEnd(numColsNotRemoved);
    }
  }
}

void nsTableFrame::DidResizeColumns() {
  MOZ_ASSERT(!GetPrevInFlow(), "should only be called on first-in-flow");

  if (mBits.mResizedColumns) {
    return;  // already marked
  }

  for (nsTableFrame* f = this; f;
       f = static_cast<nsTableFrame*>(f->GetNextInFlow())) {
    f->mBits.mResizedColumns = true;
  }
}

void nsTableFrame::AppendCell(nsTableCellFrame& aCellFrame, int32_t aRowIndex) {
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    TableArea damageArea(0, 0, 0, 0);
    cellMap->AppendCell(aCellFrame, aRowIndex, true, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      AddBCDamageArea(damageArea);
    }
  }
}

void nsTableFrame::InsertCells(nsTArray<nsTableCellFrame*>& aCellFrames,
                               int32_t aRowIndex, int32_t aColIndexBefore) {
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    TableArea damageArea(0, 0, 0, 0);
    cellMap->InsertCells(aCellFrames, aRowIndex, aColIndexBefore, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      AddBCDamageArea(damageArea);
    }
  }
}

// this removes the frames from the col group and table, but not the cell map
int32_t nsTableFrame::DestroyAnonymousColFrames(int32_t aNumFrames) {
  // only remove cols that are of type eTypeAnonymous cell (they are at the end)
  int32_t endIndex = mColFrames.Length() - 1;
  int32_t startIndex = (endIndex - aNumFrames) + 1;
  int32_t numColsRemoved = 0;
  DestroyContext context(PresShell());
  for (int32_t colIdx = endIndex; colIdx >= startIndex; colIdx--) {
    nsTableColFrame* colFrame = GetColFrame(colIdx);
    if (colFrame && (eColAnonymousCell == colFrame->GetColType())) {
      auto* cgFrame = static_cast<nsTableColGroupFrame*>(colFrame->GetParent());
      // remove the frame from the colgroup
      cgFrame->RemoveChild(context, *colFrame, false);
      // remove the frame from the cache, but not the cell map
      RemoveCol(colIdx, true, false);
      numColsRemoved++;
    } else {
      break;
    }
  }
  return (aNumFrames - numColsRemoved);
}

void nsTableFrame::RemoveCell(nsTableCellFrame* aCellFrame, int32_t aRowIndex) {
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    TableArea damageArea(0, 0, 0, 0);
    cellMap->RemoveCell(aCellFrame, aRowIndex, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      AddBCDamageArea(damageArea);
    }
  }
}

int32_t nsTableFrame::GetStartRowIndex(
    const nsTableRowGroupFrame* aRowGroupFrame) const {
  RowGroupArray orderedRowGroups = OrderedRowGroups();

  int32_t rowIndex = 0;
  for (uint32_t rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
    nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgIndex];
    if (rgFrame == aRowGroupFrame) {
      break;
    }
    int32_t numRows = rgFrame->GetRowCount();
    rowIndex += numRows;
  }
  return rowIndex;
}

// this cannot extend beyond a single row group
void nsTableFrame::AppendRows(nsTableRowGroupFrame* aRowGroupFrame,
                              int32_t aRowIndex,
                              nsTArray<nsTableRowFrame*>& aRowFrames) {
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    int32_t absRowIndex = GetStartRowIndex(aRowGroupFrame) + aRowIndex;
    InsertRows(aRowGroupFrame, aRowFrames, absRowIndex, true);
  }
}

// this cannot extend beyond a single row group
int32_t nsTableFrame::InsertRows(nsTableRowGroupFrame* aRowGroupFrame,
                                 nsTArray<nsTableRowFrame*>& aRowFrames,
                                 int32_t aRowIndex, bool aConsiderSpans) {
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowsBefore firstRow=%d \n", aRowIndex);
  Dump(true, false, true);
#endif

  int32_t numColsToAdd = 0;
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    TableArea damageArea(0, 0, 0, 0);
    bool shouldRecalculateIndex = !IsDeletedRowIndexRangesEmpty();
    if (shouldRecalculateIndex) {
      ResetRowIndices(nsFrameList::Slice(nullptr, nullptr));
    }
    int32_t origNumRows = cellMap->GetRowCount();
    int32_t numNewRows = aRowFrames.Length();
    cellMap->InsertRows(aRowGroupFrame, aRowFrames, aRowIndex, aConsiderSpans,
                        damageArea);
    MatchCellMapToColCache(cellMap);

    // Perform row index adjustment only if row indices were not
    // reset above
    if (!shouldRecalculateIndex) {
      if (aRowIndex < origNumRows) {
        AdjustRowIndices(aRowIndex, numNewRows);
      }

      // assign the correct row indices to the new rows. If they were
      // recalculated above it may not have been done correctly because each row
      // is constructed with index 0
      for (int32_t rowB = 0; rowB < numNewRows; rowB++) {
        nsTableRowFrame* rowFrame = aRowFrames.ElementAt(rowB);
        rowFrame->SetRowIndex(aRowIndex + rowB);
      }
    }

    if (IsBorderCollapse()) {
      AddBCDamageArea(damageArea);
    }
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowsAfter \n");
  Dump(true, false, true);
#endif

  return numColsToAdd;
}

void nsTableFrame::AddDeletedRowIndex(int32_t aDeletedRowStoredIndex) {
  if (mDeletedRowIndexRanges.empty()) {
    mDeletedRowIndexRanges.insert(std::pair<int32_t, int32_t>(
        aDeletedRowStoredIndex, aDeletedRowStoredIndex));
    return;
  }

  // Find the position of the current deleted row's stored index
  // among the previous deleted row index ranges and merge ranges if
  // they are consecutive, else add a new (disjoint) range to the map.
  // Call to mDeletedRowIndexRanges.upper_bound is
  // O(log(mDeletedRowIndexRanges.size())) therefore call to
  // AddDeletedRowIndex is also ~O(log(mDeletedRowIndexRanges.size()))

  // greaterIter = will point to smallest range in the map with lower value
  //              greater than the aDeletedRowStoredIndex.
  //              If no such value exists, point to end of map.
  // smallerIter = will point to largest range in the map with higher value
  //              smaller than the aDeletedRowStoredIndex
  //              If no such value exists, point to beginning of map.
  // i.e. when both values exist below is true:
  // smallerIter->second < aDeletedRowStoredIndex < greaterIter->first
  auto greaterIter = mDeletedRowIndexRanges.upper_bound(aDeletedRowStoredIndex);
  auto smallerIter = greaterIter;

  if (smallerIter != mDeletedRowIndexRanges.begin()) {
    smallerIter--;
    // While greaterIter might be out-of-bounds (by being equal to end()),
    // smallerIter now cannot be, since we returned early above for a 0-size
    // map.
  }

  // Note: smallerIter can only be equal to greaterIter when both
  // of them point to the beginning of the map and in that case smallerIter
  // does not "exist" but we clip smallerIter to point to beginning of map
  // so that it doesn't point to something unknown or outside the map boundry.
  // Note: When greaterIter is not the end (i.e. it "exists") upper_bound()
  // ensures aDeletedRowStoredIndex < greaterIter->first so no need to
  // assert that.
  MOZ_ASSERT(smallerIter == greaterIter ||
                 aDeletedRowStoredIndex > smallerIter->second,
             "aDeletedRowIndexRanges already contains aDeletedRowStoredIndex! "
             "Trying to delete an already deleted row?");

  if (smallerIter->second == aDeletedRowStoredIndex - 1) {
    if (greaterIter != mDeletedRowIndexRanges.end() &&
        greaterIter->first == aDeletedRowStoredIndex + 1) {
      // merge current index with smaller and greater range as they are
      // consecutive
      smallerIter->second = greaterIter->second;
      mDeletedRowIndexRanges.erase(greaterIter);
    } else {
      // add aDeletedRowStoredIndex in the smaller range as it is consecutive
      smallerIter->second = aDeletedRowStoredIndex;
    }
  } else if (greaterIter != mDeletedRowIndexRanges.end() &&
             greaterIter->first == aDeletedRowStoredIndex + 1) {
    // add aDeletedRowStoredIndex in the greater range as it is consecutive
    mDeletedRowIndexRanges.insert(std::pair<int32_t, int32_t>(
        aDeletedRowStoredIndex, greaterIter->second));
    mDeletedRowIndexRanges.erase(greaterIter);
  } else {
    // add new range as aDeletedRowStoredIndex is disjoint from existing ranges
    mDeletedRowIndexRanges.insert(std::pair<int32_t, int32_t>(
        aDeletedRowStoredIndex, aDeletedRowStoredIndex));
  }
}

int32_t nsTableFrame::GetAdjustmentForStoredIndex(int32_t aStoredIndex) {
  if (mDeletedRowIndexRanges.empty()) {
    return 0;
  }

  int32_t adjustment = 0;

  // O(log(mDeletedRowIndexRanges.size()))
  auto endIter = mDeletedRowIndexRanges.upper_bound(aStoredIndex);
  for (auto iter = mDeletedRowIndexRanges.begin(); iter != endIter; ++iter) {
    adjustment += iter->second - iter->first + 1;
  }

  return adjustment;
}

// this cannot extend beyond a single row group
void nsTableFrame::RemoveRows(nsTableRowFrame& aFirstRowFrame,
                              int32_t aNumRowsToRemove, bool aConsiderSpans) {
#ifdef TBD_OPTIMIZATION
  // decide if we need to rebalance. we have to do this here because the row
  // group cannot do it when it gets the dirty reflow corresponding to the frame
  // being destroyed
  bool stopTelling = false;
  for (nsIFrame* kidFrame = aFirstFrame.FirstChild(); (kidFrame && !stopAsking);
       kidFrame = kidFrame->GetNextSibling()) {
    nsTableCellFrame* cellFrame = do_QueryFrame(kidFrame);
    if (cellFrame) {
      stopTelling = tableFrame->CellChangedWidth(
          *cellFrame, cellFrame->GetPass1MaxElementWidth(),
          cellFrame->GetMaximumWidth(), true);
    }
  }
  // XXX need to consider what happens if there are cells that have rowspans
  // into the deleted row. Need to consider moving rows if a rebalance doesn't
  // happen
#endif

  int32_t firstRowIndex = aFirstRowFrame.GetRowIndex();
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== removeRowsBefore firstRow=%d numRows=%d\n", firstRowIndex,
         aNumRowsToRemove);
  Dump(true, false, true);
#endif
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    TableArea damageArea(0, 0, 0, 0);

    // Mark rows starting from aFirstRowFrame to the next 'aNumRowsToRemove-1'
    // number of rows as deleted.
    nsTableRowGroupFrame* parentFrame = aFirstRowFrame.GetTableRowGroupFrame();
    parentFrame->MarkRowsAsDeleted(aFirstRowFrame, aNumRowsToRemove);

    cellMap->RemoveRows(firstRowIndex, aNumRowsToRemove, aConsiderSpans,
                        damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      AddBCDamageArea(damageArea);
    }
  }

#ifdef DEBUG_TABLE_CELLMAP
  printf("=== removeRowsAfter\n");
  Dump(true, true, true);
#endif
}

// collect the rows ancestors of aFrame
int32_t nsTableFrame::CollectRows(nsIFrame* aFrame,
                                  nsTArray<nsTableRowFrame*>& aCollection) {
  MOZ_ASSERT(aFrame, "null frame");
  int32_t numRows = 0;
  for (nsIFrame* childFrame : aFrame->PrincipalChildList()) {
    aCollection.AppendElement(static_cast<nsTableRowFrame*>(childFrame));
    numRows++;
  }
  return numRows;
}

void nsTableFrame::InsertRowGroups(const nsFrameList::Slice& aRowGroups) {
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowGroupsBefore\n");
  Dump(true, false, true);
#endif
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    RowGroupArray orderedRowGroups = OrderedRowGroups();

    AutoTArray<nsTableRowFrame*, 8> rows;
    // Loop over the rowgroups and check if some of them are new, if they are
    // insert cellmaps in the order that is predefined by OrderedRowGroups.
    // XXXbz this code is O(N*M) where N is number of new rowgroups
    // and M is number of rowgroups we have!
    uint32_t rgIndex;
    for (rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
      for (nsIFrame* rowGroup : aRowGroups) {
        if (orderedRowGroups[rgIndex] == rowGroup) {
          nsTableRowGroupFrame* priorRG =
              (0 == rgIndex) ? nullptr : orderedRowGroups[rgIndex - 1];
          // create and add the cell map for the row group
          cellMap->InsertGroupCellMap(orderedRowGroups[rgIndex], priorRG);

          break;
        }
      }
    }
    cellMap->Synchronize(this);
    ResetRowIndices(aRowGroups);

    // now that the cellmaps are reordered too insert the rows
    for (rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
      for (nsIFrame* rowGroup : aRowGroups) {
        if (orderedRowGroups[rgIndex] == rowGroup) {
          nsTableRowGroupFrame* priorRG =
              (0 == rgIndex) ? nullptr : orderedRowGroups[rgIndex - 1];
          // collect the new row frames in an array and add them to the table
          int32_t numRows = CollectRows(rowGroup, rows);
          if (numRows > 0) {
            int32_t rowIndex = 0;
            if (priorRG) {
              int32_t priorNumRows = priorRG->GetRowCount();
              rowIndex = priorRG->GetStartRowIndex() + priorNumRows;
            }
            InsertRows(orderedRowGroups[rgIndex], rows, rowIndex, true);
            rows.Clear();
          }
          break;
        }
      }
    }
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowGroupsAfter\n");
  Dump(true, true, true);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// Child frame enumeration

static inline bool FrameHasBorder(nsIFrame* f) {
  if (!f->StyleVisibility()->IsVisible()) {
    return false;
  }

  return f->StyleBorder()->HasBorder();
}

void nsTableFrame::CalcHasBCBorders() {
  if (!IsBorderCollapse()) {
    SetHasBCBorders(false);
    return;
  }

  if (FrameHasBorder(this)) {
    SetHasBCBorders(true);
    return;
  }

  auto groups = OrderedGroups();
  // Check col and col group has borders.
  for (nsTableColGroupFrame* cg : groups.mColGroups) {
    if (FrameHasBorder(cg)) {
      SetHasBCBorders(true);
      return;
    }

    for (nsTableColFrame* col = cg->GetFirstColumn(); col;
         col = col->GetNextCol()) {
      if (FrameHasBorder(col)) {
        SetHasBCBorders(true);
        return;
      }
    }
  }

  // check row group, row and cell has borders.
  for (nsTableRowGroupFrame* rowGroup : groups.mRowGroups) {
    if (FrameHasBorder(rowGroup)) {
      SetHasBCBorders(true);
      return;
    }

    for (nsTableRowFrame* row = rowGroup->GetFirstRow(); row;
         row = row->GetNextRow()) {
      if (FrameHasBorder(row)) {
        SetHasBCBorders(true);
        return;
      }

      for (nsTableCellFrame* cell = row->GetFirstCell(); cell;
           cell = cell->GetNextCell()) {
        if (FrameHasBorder(cell)) {
          SetHasBCBorders(true);
          return;
        }
      }
    }
  }

  SetHasBCBorders(false);
}

namespace mozilla {
class nsDisplayTableBorderCollapse;
}

// table paint code is concerned primarily with borders and bg color
// SEC: TODO: adjust the rect for captions
void nsTableFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists) {
  DO_GLOBAL_REFLOW_COUNT_DSP_COLOR("nsTableFrame", NS_RGB(255, 128, 255));

  DisplayBorderBackgroundOutline(aBuilder, aLists);

  nsDisplayTableBackgroundSet tableBGs(aBuilder, this);
  nsDisplayListCollection lists(aBuilder);

  // This is similar to what
  // nsContainerFrame::BuildDisplayListForNonBlockChildren does, except that we
  // allow the children's background and borders to go in our BorderBackground
  // list. This doesn't really affect background painting --- the children won't
  // actually draw their own backgrounds because the nsTableFrame already drew
  // them, unless a child has its own stacking context, in which case the child
  // won't use its passed-in BorderBackground list anyway. It does affect cell
  // borders though; this lets us get cell borders into the nsTableFrame's
  // BorderBackground list.
  for (nsTableColFrame* col :
       static_cast<nsTableFrame*>(FirstInFlow())->mColFrames) {
    tableBGs.AddColumn(col);
  }

  if (!mFrames.IsEmpty() && !HidesContent()) {
    for (nsIFrame* kid : mFrames) {
      BuildDisplayListForChild(aBuilder, kid, lists);
    }
  }

  tableBGs.MoveTo(aLists);
  lists.MoveTo(aLists);

  if (IsVisibleForPainting()) {
    // In the collapsed border model, overlay all collapsed borders.
    if (IsBorderCollapse()) {
      if (HasBCBorders()) {
        aLists.BorderBackground()->AppendNewToTop<nsDisplayTableBorderCollapse>(
            aBuilder, this);
      }
    } else {
      const nsStyleBorder* borderStyle = StyleBorder();
      if (borderStyle->HasBorder()) {
        aLists.BorderBackground()->AppendNewToTop<nsDisplayBorder>(aBuilder,
                                                                   this);
      }
    }
  }
}

LogicalSides nsTableFrame::GetLogicalSkipSides() const {
  LogicalSides skip(mWritingMode);
  if (MOZ_UNLIKELY(StyleBorder()->mBoxDecorationBreak ==
                   StyleBoxDecorationBreak::Clone)) {
    return skip;
  }

  // frame attribute was accounted for in nsHTMLTableElement::MapTableBorderInto
  // account for pagination
  if (GetPrevInFlow()) {
    skip += LogicalSide::BStart;
  }
  if (GetNextInFlow()) {
    skip += LogicalSide::BEnd;
  }
  return skip;
}

void nsTableFrame::SetColumnDimensions(nscoord aBSize, WritingMode aWM,
                                       const LogicalMargin& aBorderPadding,
                                       const nsSize& aContainerSize) {
  const nscoord colBSize =
      aBSize - (aBorderPadding.BStartEnd(aWM) + GetRowSpacing(-1) +
                GetRowSpacing(GetRowCount()));
  int32_t colIdx = 0;
  LogicalPoint colGroupOrigin(aWM,
                              aBorderPadding.IStart(aWM) + GetColSpacing(-1),
                              aBorderPadding.BStart(aWM) + GetRowSpacing(-1));
  nsTableFrame* fif = static_cast<nsTableFrame*>(FirstInFlow());
  for (nsIFrame* colGroupFrame : OrderedGroups().mColGroups) {
    MOZ_ASSERT(colGroupFrame->IsTableColGroupFrame());
    // first we need to figure out the size of the colgroup
    int32_t groupFirstCol = colIdx;
    nscoord colGroupISize = 0;
    nscoord colSpacing = 0;
    const nsFrameList& columnList = colGroupFrame->PrincipalChildList();
    for (nsIFrame* colFrame : columnList) {
      if (mozilla::StyleDisplay::TableColumn ==
          colFrame->StyleDisplay()->mDisplay) {
        NS_ASSERTION(colIdx < GetColCount(), "invalid number of columns");
        colSpacing = GetColSpacing(colIdx);
        colGroupISize +=
            fif->GetColumnISizeFromFirstInFlow(colIdx) + colSpacing;
        ++colIdx;
      }
    }
    if (colGroupISize) {
      colGroupISize -= colSpacing;
    }

    LogicalRect colGroupRect(aWM, colGroupOrigin.I(aWM), colGroupOrigin.B(aWM),
                             colGroupISize, colBSize);
    colGroupFrame->SetRect(aWM, colGroupRect, aContainerSize);
    nsSize colGroupSize = colGroupFrame->GetSize();

    // then we can place the columns correctly within the group
    colIdx = groupFirstCol;
    LogicalPoint colOrigin(aWM);
    for (nsIFrame* colFrame : columnList) {
      if (mozilla::StyleDisplay::TableColumn ==
          colFrame->StyleDisplay()->mDisplay) {
        nscoord colISize = fif->GetColumnISizeFromFirstInFlow(colIdx);
        LogicalRect colRect(aWM, colOrigin.I(aWM), colOrigin.B(aWM), colISize,
                            colBSize);
        colFrame->SetRect(aWM, colRect, colGroupSize);
        colSpacing = GetColSpacing(colIdx);
        colOrigin.I(aWM) += colISize + colSpacing;
        ++colIdx;
      }
    }

    colGroupOrigin.I(aWM) += colGroupISize + colSpacing;
  }
}

// SEC: TODO need to worry about continuing frames prev/next in flow for
// splitting across pages.

// XXX this could be made more general to handle row modifications that change
// the table bsize, but first we need to scrutinize every Invalidate
void nsTableFrame::ProcessRowInserted(nscoord aNewBSize) {
  SetRowInserted(false);  // reset the bit that got us here
  RowGroupArray rowGroups = OrderedRowGroups();
  // find the row group containing the inserted row
  for (uint32_t rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
    NS_ASSERTION(rgFrame, "Must have rgFrame here");
    // find the row that was inserted first
    for (nsIFrame* childFrame : rgFrame->PrincipalChildList()) {
      nsTableRowFrame* rowFrame = do_QueryFrame(childFrame);
      if (rowFrame) {
        if (rowFrame->IsFirstInserted()) {
          rowFrame->SetFirstInserted(false);
          // damage the table from the 1st row inserted to the end of the table
          nsIFrame::InvalidateFrame();
          // XXXbz didn't we do this up front?  Why do we need to do it again?
          SetRowInserted(false);
          return;  // found it, so leave
        }
      }
    }
  }
}

/* virtual */
void nsTableFrame::MarkIntrinsicISizesDirty() {
  nsITableLayoutStrategy* tls = LayoutStrategy();
  if (MOZ_UNLIKELY(!tls)) {
    // This is a FrameNeedsReflow() from nsBlockFrame::RemoveFrame()
    // walking up the ancestor chain in a table next-in-flow.  In this case
    // our original first-in-flow (which owns the TableLayoutStrategy) has
    // already been destroyed and unhooked from the flow chain and thusly
    // LayoutStrategy() returns null.  All the frames in the flow will be
    // destroyed so no need to mark anything dirty here.  See bug 595758.
    return;
  }
  tls->MarkIntrinsicISizesDirty();

  // XXXldb Call SetBCDamageArea?

  nsContainerFrame::MarkIntrinsicISizesDirty();
}

nscoord nsTableFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                     IntrinsicISizeType aType) {
  if (NeedToCalcBCBorders()) {
    CalcBCBorders();
  }

  return aType == IntrinsicISizeType::MinISize
             ? LayoutStrategy()->GetMinISize(aInput.mContext)
             : LayoutStrategy()->GetPrefISize(aInput.mContext, false);
}

/* virtual */ nsIFrame::IntrinsicSizeOffsetData
nsTableFrame::IntrinsicISizeOffsets(nscoord aPercentageBasis) {
  IntrinsicSizeOffsetData result =
      nsContainerFrame::IntrinsicISizeOffsets(aPercentageBasis);

  result.margin = 0;

  if (IsBorderCollapse()) {
    result.padding = 0;

    WritingMode wm = GetWritingMode();
    LogicalMargin outerBC = GetOuterBCBorder(wm);
    result.border = outerBC.IStartEnd(wm);
  }

  return result;
}

/* virtual */
nsIFrame::SizeComputationResult nsTableFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  // Only table wrapper calls this method, and it should use our writing mode.
  MOZ_ASSERT(aWM == GetWritingMode(),
             "aWM should be the same as our writing mode!");

  auto result = nsContainerFrame::ComputeSize(
      aSizingInput, aWM, aCBSize, aAvailableISize, aMargin, aBorderPadding,
      aSizeOverrides, aFlags);

  // If our containing block wants to override inner table frame's inline-size
  // (e.g. when resolving flex base size), don't enforce the min inline-size
  // later in this method.
  if (aSizeOverrides.mApplyOverridesVerbatim && aSizeOverrides.mStyleISize &&
      aSizeOverrides.mStyleISize->IsLengthPercentage()) {
    return result;
  }

  // If we're a container for font size inflation, then shrink
  // wrapping inside of us should not apply font size inflation.
  AutoMaybeDisableFontInflation an(this);

  // Tables never shrink below their min inline-size.
  const IntrinsicSizeInput input(aSizingInput.mRenderingContext, Some(aCBSize),
                                 Nothing());
  nscoord minISize = GetMinISize(input);
  if (minISize > result.mLogicalSize.ISize(aWM)) {
    result.mLogicalSize.ISize(aWM) = minISize;
  }

  return result;
}

nscoord nsTableFrame::TableShrinkISizeToFit(gfxContext* aRenderingContext,
                                            nscoord aISizeInCB) {
  // If we're a container for font size inflation, then shrink
  // wrapping inside of us should not apply font size inflation.
  AutoMaybeDisableFontInflation an(this);

  nscoord result;
  const IntrinsicSizeInput input(aRenderingContext, Nothing(), Nothing());
  nscoord minISize = GetMinISize(input);
  if (minISize > aISizeInCB) {
    result = minISize;
  } else {
    // Tables shrink inline-size to fit with a slightly different algorithm
    // from the one they use for their intrinsic isize (the difference
    // relates to handling of percentage isizes on columns).  So this
    // function differs from nsIFrame::ShrinkISizeToFit by only the
    // following line.
    // Since we've already called GetMinISize, we don't need to do any
    // of the other stuff GetPrefISize does.
    nscoord prefISize = LayoutStrategy()->GetPrefISize(aRenderingContext, true);
    if (prefISize > aISizeInCB) {
      result = aISizeInCB;
    } else {
      result = prefISize;
    }
  }
  return result;
}

/* virtual */
LogicalSize nsTableFrame::ComputeAutoSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  // Tables always shrink-wrap.
  nscoord cbBased =
      aAvailableISize - aMargin.ISize(aWM) - aBorderPadding.ISize(aWM);
  return LogicalSize(
      aWM, TableShrinkISizeToFit(aSizingInput.mRenderingContext, cbBased),
      NS_UNCONSTRAINEDSIZE);
}

// Return true if aParentReflowInput.frame or any of its ancestors within
// the containing table have non-auto bsize. (e.g. pct or fixed bsize)
bool nsTableFrame::AncestorsHaveStyleBSize(
    const ReflowInput& aParentReflowInput) {
  WritingMode wm = aParentReflowInput.GetWritingMode();
  for (const ReflowInput* rs = &aParentReflowInput; rs && rs->mFrame;
       rs = rs->mParentReflowInput) {
    LayoutFrameType frameType = rs->mFrame->Type();
    if (LayoutFrameType::TableCell == frameType ||
        LayoutFrameType::TableRow == frameType ||
        LayoutFrameType::TableRowGroup == frameType) {
      const auto bsize =
          rs->mStylePosition->BSize(wm, AnchorPosResolutionParams::From(rs));
      // calc() with both lengths and percentages treated like 'auto' on
      // internal table elements
      if (!bsize->IsAuto() && !bsize->HasLengthAndPercentage()) {
        return true;
      }
    } else if (LayoutFrameType::Table == frameType) {
      // we reached the containing table, so always return
      return !rs->mStylePosition->BSize(wm, AnchorPosResolutionParams::From(rs))
                  ->IsAuto();
    }
  }
  return false;
}

// See if a special block-size reflow needs to occur and if so,
// call RequestSpecialBSizeReflow
void nsTableFrame::CheckRequestSpecialBSizeReflow(
    const ReflowInput& aReflowInput) {
  NS_ASSERTION(aReflowInput.mFrame->IsTableCellFrame() ||
                   aReflowInput.mFrame->IsTableRowFrame() ||
                   aReflowInput.mFrame->IsTableRowGroupFrame() ||
                   aReflowInput.mFrame->IsTableFrame(),
               "unexpected frame type");
  WritingMode wm = aReflowInput.GetWritingMode();
  if (!aReflowInput.mFrame->GetPrevInFlow() &&  // 1st in flow
      (NS_UNCONSTRAINEDSIZE ==
           aReflowInput.ComputedBSize() ||  // no computed bsize
       0 == aReflowInput.ComputedBSize()) &&
      aReflowInput.mStylePosition
          ->BSize(wm, AnchorPosResolutionParams::From(&aReflowInput))
          ->ConvertsToPercentage() &&  // pct bsize
      nsTableFrame::AncestorsHaveStyleBSize(*aReflowInput.mParentReflowInput)) {
    nsTableFrame::RequestSpecialBSizeReflow(aReflowInput);
  }
}

// Notify the frame and its ancestors (up to the containing table) that a
// special bsize reflow will occur. During a special bsize reflow, a table, row
// group, row, or cell returns the last size it was reflowed at. However, the
// table may change the bsize of row groups, rows, cells in
// DistributeBSizeToRows after. And the row group can change the bsize of rows,
// cells in CalculateRowBSizes.
void nsTableFrame::RequestSpecialBSizeReflow(const ReflowInput& aReflowInput) {
  // notify the frame and its ancestors of the special reflow, stopping at the
  // containing table
  for (const ReflowInput* rs = &aReflowInput; rs && rs->mFrame;
       rs = rs->mParentReflowInput) {
    LayoutFrameType frameType = rs->mFrame->Type();
    NS_ASSERTION(LayoutFrameType::TableCell == frameType ||
                     LayoutFrameType::TableRow == frameType ||
                     LayoutFrameType::TableRowGroup == frameType ||
                     LayoutFrameType::Table == frameType,
                 "unexpected frame type");

    rs->mFrame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
    if (LayoutFrameType::Table == frameType) {
      NS_ASSERTION(rs != &aReflowInput,
                   "should not request special bsize reflow for table");
      // always stop when we reach a table
      break;
    }
  }
}

/******************************************************************************************
 * Before reflow, intrinsic inline-size calculation is done using GetMinISize
 * and GetPrefISize.  This used to be known as pass 1 reflow.
 *
 * After the intrinsic isize calculation, the table determines the
 * column widths using BalanceColumnISizes() and
 * then reflows each child again with a constrained avail isize. This reflow is
 * referred to as the pass 2 reflow.
 *
 * A special bsize reflow (pass 3 reflow) can occur during an initial or resize
 * reflow if (a) a row group, row, cell, or a frame inside a cell has a percent
 * bsize but no computed bsize or (b) in paginated mode, a table has a bsize.
 * (a) supports percent nested tables contained inside cells whose bsizes aren't
 * known until after the pass 2 reflow. (b) is necessary because the table
 * cannot split until after the pass 2 reflow. The mechanics of the special
 * bsize reflow (variety a) are as follows:
 *
 * 1) Each table related frame (table, row group, row, cell) implements
 *    NeedsSpecialReflow() to indicate that it should get the reflow. It does
 *    this when it has a percent bsize but no computed bsize by calling
 *    CheckRequestSpecialBSizeReflow(). This method calls
 *    RequestSpecialBSizeReflow() which calls SetNeedSpecialReflow() on its
 *    ancestors until it reaches the containing table and calls
 *    SetNeedToInitiateSpecialReflow() on it. For percent bsize frames inside
 *    cells, during DidReflow(), the cell's NotifyPercentBSize() is called
 *    (the cell is the reflow input's mPercentBSizeObserver in this case).
 *    NotifyPercentBSize() calls RequestSpecialBSizeReflow().
 *
 * XXX (jfkthame) This comment appears to be out of date; it refers to
 * methods/flags that are no longer present in the code.
 *
 * 2) After the pass 2 reflow, if the table's NeedToInitiateSpecialReflow(true)
 *    was called, it will do the special bsize reflow, setting the reflow
 *    input's mFlags.mSpecialBSizeReflow to true and mSpecialHeightInitiator to
 *    itself. It won't do this if IsPrematureSpecialHeightReflow() returns true
 *    because in that case another special bsize reflow will be coming along
 *    with the containing table as the mSpecialHeightInitiator. It is only
 *    relevant to do the reflow when the mSpecialHeightInitiator is the
 *    containing table, because if it is a remote ancestor, then appropriate
 *    bsizes will not be known.
 *
 * 3) Since the bsizes of the table, row groups, rows, and cells was determined
 *    during the pass 2 reflow, they return their last desired sizes during the
 *    special bsize reflow. The reflow only permits percent bsize frames inside
 *    the cells to resize based on the cells bsize and that bsize was
 *    determined during the pass 2 reflow.
 *
 * So, in the case of deeply nested tables, all of the tables that were told to
 * initiate a special reflow will do so, but if a table is already in a special
 * reflow, it won't inititate the reflow until the current initiator is its
 * containing table. Since these reflows are only received by frames that need
 * them and they don't cause any rebalancing of tables, the extra overhead is
 * minimal.
 *
 * The type of special reflow that occurs during printing (variety b) follows
 * the same mechanism except that all frames will receive the reflow even if
 * they don't really need them.
 *
 * Open issues with the special bsize reflow:
 *
 * 1) At some point there should be 2 kinds of special bsize reflows because (a)
 *    and (b) above are really quite different. This would avoid unnecessary
 *    reflows during printing.
 *
 * 2) When a cell contains frames whose percent bsizes > 100%, there is data
 *    loss (see bug 115245). However, this can also occur if a cell has a fixed
 *    bsize and there is no special bsize reflow.
 *
 * XXXldb Special bsize reflow should really be its own method, not
 * part of nsIFrame::Reflow.  It should then call nsIFrame::Reflow on
 * the contents of the cells to do the necessary block-axis resizing.
 *
 ******************************************************************************************/

/* Layout the entire inner table. */
void nsTableFrame::Reflow(nsPresContext* aPresContext,
                          ReflowOutput& aDesiredSize,
                          const ReflowInput& aReflowInput,
                          nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTableFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
             "The nsTableWrapperFrame should be the out-of-flow if needed");

  const WritingMode wm = aReflowInput.GetWritingMode();
  MOZ_ASSERT(aReflowInput.ComputedLogicalMargin(wm).IsAllZero(),
             "Only nsTableWrapperFrame can have margins!");

  bool isPaginated = aPresContext->IsPaginated();

  if (!GetPrevInFlow() && !mTableLayoutStrategy) {
    NS_ERROR("strategy should have been created in Init");
    return;
  }

  // see if collapsing borders need to be calculated
  if (!GetPrevInFlow() && IsBorderCollapse() && NeedToCalcBCBorders()) {
    CalcBCBorders();
  }

  // Check for an overflow list, and append any row group frames being pushed
  MoveOverflowToChildList();

  bool haveCalledCalcDesiredBSize = false;
  LogicalMargin borderPadding =
      aReflowInput.ComputedLogicalBorderPadding(wm).ApplySkipSides(
          PreReflowBlockLevelLogicalSkipSides());
  nsIFrame* lastChildReflowed = nullptr;
  const nsSize containerSize =
      aReflowInput.ComputedSizeAsContainerIfConstrained();

  // The tentative width is the width we assumed for the table when the child
  // frames were positioned (which only matters in vertical-rl mode, because
  // they're positioned relative to the right-hand edge). Then, after reflowing
  // the kids, we can check whether the table ends up with a different width
  // than this tentative value (either because it was unconstrained, so we used
  // zero, or because it was enlarged by the child frames), we make the
  // necessary positioning adjustments along the x-axis.
  nscoord tentativeContainerWidth = 0;
  bool mayAdjustXForAllChildren = false;
  Groups groups = OrderedGroups();

  // Reflow the entire table (pass 2 and possibly pass 3). This phase is
  // necessary during a constrained initial reflow and other reflows which
  // require either a strategy init or balance. This isn't done during an
  // unconstrained reflow, because it will occur later when the parent reflows
  // with a constrained isize.
  if (IsSubtreeDirty() || aReflowInput.ShouldReflowAllKids() ||
      IsGeometryDirty() || isPaginated || aReflowInput.IsBResize() ||
      NeedToCollapse()) {
    if (aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE ||
        // Also check IsBResize(), to handle the first Reflow preceding a
        // special bsize Reflow, when we've already had a special bsize
        // Reflow (where ComputedBSize() would not be
        // NS_UNCONSTRAINEDSIZE, but without a style change in between).
        aReflowInput.IsBResize()) {
      // XXX Eventually, we should modify DistributeBSizeToRows to use
      // nsTableRowFrame::GetInitialBSize instead of nsIFrame::BSize().
      // That way, it will make its calculations based on internal table
      // frame bsizes as they are before they ever had any extra bsize
      // distributed to them.  In the meantime, this reflows all the
      // internal table frames, which restores them to their state before
      // DistributeBSizeToRows was called.
      SetGeometryDirty();
    }

    bool needToInitiateSpecialReflow = false;
    if (isPaginated) {
      // see if an extra reflow will be necessary in pagination mode
      // when there is a specified table bsize
      if (!GetPrevInFlow() &&
          NS_UNCONSTRAINEDSIZE != aReflowInput.AvailableBSize()) {
        nscoord tableSpecifiedBSize = CalcBorderBoxBSize(
            aReflowInput, borderPadding, NS_UNCONSTRAINEDSIZE);
        if (tableSpecifiedBSize != NS_UNCONSTRAINEDSIZE &&
            tableSpecifiedBSize > 0) {
          needToInitiateSpecialReflow = true;
        }
      }
    } else {
      needToInitiateSpecialReflow =
          HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
    }

    NS_ASSERTION(!aReflowInput.mFlags.mSpecialBSizeReflow,
                 "Shouldn't be in special bsize reflow here!");

    const TableReflowMode firstReflowMode = needToInitiateSpecialReflow
                                                ? TableReflowMode::Measuring
                                                : TableReflowMode::Final;
    ReflowTable(aDesiredSize, aReflowInput, borderPadding, firstReflowMode,
                groups, lastChildReflowed, aStatus);

    // When in vertical-rl mode, there may be two kinds of scenarios in which
    // the positioning of all the children need to be adjusted along the x-axis
    // because the width we assumed for the table when the child frames were
    // being positioned(i.e. tentative width) may be different from the final
    // width for the table:
    // 1. If the computed width for the table is unconstrained, a dummy zero
    //    width was assumed as the tentative width to begin with.
    // 2. If the child frames enlarge the width for the table, the final width
    //    becomes larger than the tentative one.
    // Let's record the tentative width here, if later the final width turns out
    // to be different from this tentative one, it means one of the above
    // scenarios happens, then we adjust positioning of all the children.
    // Note that vertical-lr, unlike vertical-rl, doesn't need to take special
    // care of this situation, because they're positioned relative to the
    // left-hand edge.
    if (wm.IsVerticalRL()) {
      tentativeContainerWidth = containerSize.width;
      mayAdjustXForAllChildren = true;
    }

    // reevaluate special bsize reflow conditions
    if (HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
      needToInitiateSpecialReflow = true;
    }

    // XXXldb Are all these conditions correct?
    if (needToInitiateSpecialReflow && aStatus.IsComplete()) {
      // XXXldb Do we need to set the IsBResize flag on any reflow inputs?

      ReflowInput& mutable_rs = const_cast<ReflowInput&>(aReflowInput);

      // distribute extra block-direction space to rows
      aDesiredSize.BSize(wm) =
          CalcDesiredBSize(aReflowInput, borderPadding, aStatus);
      haveCalledCalcDesiredBSize = true;

      mutable_rs.mFlags.mSpecialBSizeReflow = true;

      ReflowTable(aDesiredSize, aReflowInput, borderPadding,
                  TableReflowMode::Final, groups, lastChildReflowed, aStatus);

      mutable_rs.mFlags.mSpecialBSizeReflow = false;
    }
  }

  if (aStatus.IsIncomplete() &&
      aReflowInput.mStyleBorder->mBoxDecorationBreak ==
          StyleBoxDecorationBreak::Slice) {
    borderPadding.BEnd(wm) = 0;
  }

  aDesiredSize.ISize(wm) =
      aReflowInput.ComputedISize() + borderPadding.IStartEnd(wm);
  if (!haveCalledCalcDesiredBSize) {
    aDesiredSize.BSize(wm) =
        CalcDesiredBSize(aReflowInput, borderPadding, aStatus);
  } else if (lastChildReflowed && aStatus.IsIncomplete()) {
    // If there is an incomplete child, then set the desired block-size to
    // include it but not the next one.
    aDesiredSize.BSize(wm) =
        borderPadding.BEnd(wm) +
        lastChildReflowed->GetLogicalNormalRect(wm, containerSize).BEnd(wm);
  }
  if (IsRowInserted()) {
    ProcessRowInserted(aDesiredSize.BSize(wm));
  }

  // For more information on the reason for what we should do this, refer to the
  // code which defines and evaluates the variables xAdjustmentForAllKids and
  // tentativeContainerWidth in the previous part in this function.
  if (mayAdjustXForAllChildren) {
    nscoord xAdjustmentForAllKids =
        aDesiredSize.Width() - tentativeContainerWidth;
    if (0 != xAdjustmentForAllKids) {
      for (nsIFrame* kid : mFrames) {
        kid->MovePositionBy(nsPoint(xAdjustmentForAllKids, 0));
      }
    }
  }

  SetColumnDimensions(aDesiredSize.BSize(wm), wm, borderPadding,
                      aDesiredSize.PhysicalSize());
  NS_WARNING_ASSERTION(NS_UNCONSTRAINEDSIZE != aReflowInput.AvailableISize(),
                       "reflow branch removed unconstrained available isizes");
  if (NeedToCollapse()) {
    // This code and the code it depends on assumes that all row groups
    // and rows have just been reflowed (i.e., it makes adjustments to
    // their rects that are not idempotent).  Thus the reflow code
    // checks NeedToCollapse() to ensure this is true.
    AdjustForCollapsingRowsCols(aDesiredSize, wm, groups.mRowGroups,
                                borderPadding);
  }

  // Calculate the overflow area contribution from our children. We couldn't
  // do this on the fly during ReflowChildren(), because in vertical-rl mode
  // with unconstrained width, we weren't placing them in their final positions
  // until the fixupKidPositions loop just above.
  for (nsIFrame* kid : groups.mRowGroups) {
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, kid);
  }

  // If there are any relatively-positioned table parts, we need to reflow their
  // absolutely-positioned descendants now that their dimensions are final.
  FixupPositionedTableParts(aPresContext, aDesiredSize, aReflowInput);

  // make sure the table overflow area does include the table rect.
  nsRect tableRect(0, 0, aDesiredSize.Width(), aDesiredSize.Height());

  aDesiredSize.mOverflowAreas.UnionAllWith(tableRect);

  FinishAndStoreOverflow(&aDesiredSize);
}

void nsTableFrame::FixupPositionedTableParts(nsPresContext* aPresContext,
                                             ReflowOutput& aDesiredSize,
                                             const ReflowInput& aReflowInput) {
  TablePartsArray* positionedParts =
      GetProperty(PositionedTablePartsProperty());
  if (!positionedParts) {
    return;
  }

  OverflowChangedTracker overflowTracker;
  overflowTracker.SetSubtreeRoot(this);

  for (nsContainerFrame* positionedPart : *positionedParts) {
    // As we've already finished reflow, positionedParts's size and overflow
    // areas have already been assigned, so we just pull them back out.
    const WritingMode wm = positionedPart->GetWritingMode();
    const LogicalSize size = positionedPart->GetLogicalSize(wm);
    ReflowOutput desiredSize(aReflowInput.GetWritingMode());
    desiredSize.SetSize(wm, size);
    desiredSize.mOverflowAreas =
        positionedPart->GetOverflowAreasRelativeToSelf();

    // Construct a dummy reflow input and reflow status.
    // XXX(seth): Note that the dummy reflow input doesn't have a correct
    // chain of parent reflow inputs. It also doesn't necessarily have a
    // correct containing block.
    LogicalSize availSize = size;
    availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
    ReflowInput reflowInput(aPresContext, positionedPart,
                            aReflowInput.mRenderingContext, availSize,
                            ReflowInput::InitFlag::DummyParentReflowInput);
    nsReflowStatus reflowStatus;

    // Reflow absolutely-positioned descendants of the positioned part.
    // FIXME: Unconditionally using NS_UNCONSTRAINEDSIZE for the bsize and
    // ignoring any change to the reflow status aren't correct. We'll never
    // paginate absolutely positioned frames.
    positionedPart->FinishReflowWithAbsoluteFrames(PresContext(), desiredSize,
                                                   reflowInput, reflowStatus);

    // FinishReflowWithAbsoluteFrames has updated overflow on
    // |positionedPart|.  We need to make sure that update propagates
    // through the intermediate frames between it and this frame.
    nsIFrame* positionedFrameParent = positionedPart->GetParent();
    if (positionedFrameParent != this) {
      overflowTracker.AddFrame(positionedFrameParent,
                               OverflowChangedTracker::CHILDREN_CHANGED);
    }
  }

  // Propagate updated overflow areas up the tree.
  overflowTracker.Flush();

  // Update our own overflow areas. (OverflowChangedTracker doesn't update the
  // subtree root itself.)
  aDesiredSize.SetOverflowAreasToDesiredBounds();
  nsLayoutUtils::UnionChildOverflow(this, aDesiredSize.mOverflowAreas);
}

bool nsTableFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

void nsTableFrame::ReflowTable(ReflowOutput& aDesiredSize,
                               const ReflowInput& aReflowInput,
                               const LogicalMargin& aBorderPadding,
                               TableReflowMode aReflowMode, Groups& aGroups,
                               nsIFrame*& aLastChildReflowed,
                               nsReflowStatus& aStatus) {
  aLastChildReflowed = nullptr;

  if (!GetPrevInFlow()) {
    mTableLayoutStrategy->ComputeColumnISizes(aReflowInput);
  }

  TableReflowInput reflowInput(aReflowInput, aBorderPadding, aReflowMode);
  ReflowChildren(reflowInput, aStatus, aGroups, aLastChildReflowed,
                 aDesiredSize.mOverflowAreas);
}

void nsTableFrame::PushChildrenToOverflow(const RowGroupArray& aRowGroups,
                                          size_t aPushFrom) {
  MOZ_ASSERT(aPushFrom > 0, "pushing first child");

  // Extract the frames from the array into a frame list.
  nsFrameList frames;
  for (size_t childX = aPushFrom; childX < aRowGroups.Length(); ++childX) {
    nsTableRowGroupFrame* rgFrame = aRowGroups[childX];
    if (!rgFrame->IsRepeatable()) {
      mFrames.RemoveFrame(rgFrame);
      frames.AppendFrame(nullptr, rgFrame);
    }
  }

  if (frames.IsEmpty()) {
    return;
  }

  // Add the frames to our overflow list.
  SetOverflowFrames(std::move(frames));
}

// collapsing row groups, rows, col groups and cols are accounted for after both
// passes of reflow so that it has no effect on the calculations of reflow.
void nsTableFrame::AdjustForCollapsingRowsCols(
    ReflowOutput& aDesiredSize, const WritingMode aWM,
    const nsTArray<nsTableRowGroupFrame*>& aRowGroups,
    const LogicalMargin& aBorderPadding) {
  nscoord bTotalOffset = 0;  // total offset among all rows in all row groups

  // reset the bit, it will be set again if row/rowgroup or col/colgroup are
  // collapsed
  SetNeedToCollapse(false);

  // collapse the rows and/or row groups as necessary
  nsTableFrame* firstInFlow = static_cast<nsTableFrame*>(FirstInFlow());
  nscoord iSize = firstInFlow->GetCollapsedISize(aWM, aBorderPadding);
  nscoord rgISize = iSize - GetColSpacing(-1) - GetColSpacing(GetColCount());
  // Walk the list of children
  for (nsTableRowGroupFrame* rg : aRowGroups) {
    bTotalOffset += rg->CollapseRowGroupIfNecessary(bTotalOffset, rgISize, aWM);
  }

  aDesiredSize.BSize(aWM) -= bTotalOffset;
  aDesiredSize.ISize(aWM) = iSize;
}

nscoord nsTableFrame::GetCollapsedISize(const WritingMode aWM,
                                        const LogicalMargin& aBorderPadding) {
  NS_ASSERTION(!GetPrevInFlow(), "GetCollapsedISize called on next in flow");
  nscoord iSize = GetColSpacing(GetColCount());
  iSize += aBorderPadding.IStartEnd(aWM);
  for (nsTableColFrame* colFrame : mColFrames) {
    bool collapseGroup = colFrame->GetParent()->StyleVisibility()->mVisible ==
                         StyleVisibility::Collapse;
    const nsStyleVisibility* colVis = colFrame->StyleVisibility();
    bool collapseCol = StyleVisibility::Collapse == colVis->mVisible;
    if (collapseGroup || collapseCol) {
      SetNeedToCollapse(true);
      continue;
    }
    nscoord colISize = colFrame->GetFinalISize();
    iSize += colISize;
    if (ColumnHasCellSpacingBefore(colFrame->GetColIndex())) {
      iSize += GetColSpacing(colFrame->GetColIndex() - 1);
    }
  }
  return iSize;
}

/* virtual */
void nsTableFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  if (!aOldComputedStyle) {  // avoid this on init
    return;
  }

  if (IsBorderCollapse() && BCRecalcNeeded(aOldComputedStyle, Style())) {
    SetFullBCDamageArea();
  }

  // avoid this on init or nextinflow
  if (!mTableLayoutStrategy || GetPrevInFlow()) {
    return;
  }

  bool isAuto = IsAutoLayout();
  if (isAuto != (LayoutStrategy()->GetType() == nsITableLayoutStrategy::Auto)) {
    if (isAuto) {
      mTableLayoutStrategy = MakeUnique<BasicTableLayoutStrategy>(this);
    } else {
      mTableLayoutStrategy = MakeUnique<FixedTableLayoutStrategy>(this);
    }
  }
}

void nsTableFrame::AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");

  nsIFrame* firstNew = aFrameList.FirstChild();
  if (!firstNew) {
    return;
  }

  DrainSelfOverflowList();  // ensure the last frame is in mFrames
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));

  nsFrameList::Slice newFrames(firstNew, nullptr);
  InsertColGroups(GetRealColEnd(), newFrames);
  InsertRowGroups(newFrames);

#ifdef DEBUG_TABLE_CELLMAP
  printf("=== TableFrame::AppendFrames\n");
  Dump(true, true, true);
#endif
  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_HAS_DIRTY_CHILDREN);
  SetGeometryDirty();
}

void nsTableFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                const nsLineList::iterator* aPrevFrameLine,
                                nsFrameList&& aFrameList) {
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  nsIFrame* firstNew = aFrameList.FirstChild();
  if (!firstNew) {
    return;
  }

  DrainSelfOverflowList();

  // Capture the frame that will follow the inserted frames, before insertion
  // changes the sibling chain. Skip the synthetic colgroup as sentinel because
  // InsertColGroups -> AddColsToTable -> InsertCol can destroy it.
  nsIFrame* afterInserted =
      aPrevFrame ? aPrevFrame->GetNextSibling() : mFrames.FirstChild();
  if (mSyntheticColGroup && afterInserted == mSyntheticColGroup) {
    afterInserted = afterInserted->GetNextSibling();
  }
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));

  nsFrameList::Slice newFrames(firstNew, afterInserted);
  InsertColGroups(GetColStartAfter(aPrevFrame), newFrames);
  InsertRowGroups(newFrames);

  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_HAS_DIRTY_CHILDREN);
  SetGeometryDirty();
}

void nsTableFrame::DoRemoveFrame(DestroyContext& aContext, ChildListID aListID,
                                 nsIFrame* aOldFrame) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");
  nsTableCellMap* cellMap = GetCellMap();

  if (nsTableColGroupFrame* cgFrame = do_QueryFrame(aOldFrame)) {
    nsIFrame* nextSibling = aOldFrame->GetNextSibling();
    int32_t colsStart = cgFrame->GetStartColumnIndex();
    int32_t colCount = cgFrame->GetColCount();
    if (cgFrame == mSyntheticColGroup) {
      mSyntheticColGroup = nullptr;
    }
    mFrames.DestroyFrame(aContext, aOldFrame);
    nsTableColGroupFrame::ResetColIndices(nextSibling, GetSyntheticColGroup(),
                                          colsStart);
    // NB: Frames in mColFrames might be dead already.
    mColFrames.RemoveElementsAt(colsStart, colCount);
    if (IsBorderCollapse()) {
      SetFullBCDamageArea();
    }

    if (!mColFrames.IsEmpty() && mColFrames.LastElement() &&
        mColFrames.LastElement()->GetColType() == eColAnonymousCell) {
      int32_t numAnonymousColsToAdd = GetColCount() - mColFrames.Length();
      if (numAnonymousColsToAdd > 0) {
        AppendAnonymousColFrames(numAnonymousColsToAdd);
      }
    } else if (cellMap) {
      cellMap->RemoveColsAtEnd();
      MatchCellMapToColCache(cellMap);
    }
#ifdef DEBUG
    VerifyColFrames();
#endif
    return;
  }

  nsTableRowGroupFrame* rgFrame = do_QueryFrame(aOldFrame);
  if (cellMap && rgFrame) {
    // remove the row group from the cell map
    cellMap->RemoveGroupCellMap(rgFrame);
  }

  mFrames.DestroyFrame(aContext, aOldFrame);

  // the removal of a row group changes the cellmap, the columns might change
  if (cellMap && rgFrame) {
    cellMap->Synchronize(this);
    // Create an empty slice
    ResetRowIndices(nsFrameList::Slice(nullptr, nullptr));
    TableArea damageArea;
    cellMap->RebuildConsideringCells(nullptr, nullptr, 0, 0, false, damageArea);

    static_cast<nsTableFrame*>(FirstInFlow())->MatchCellMapToColCache(cellMap);
  }
}

void nsTableFrame::RemoveFrame(DestroyContext& aContext, ChildListID aListID,
                               nsIFrame* aOldFrame) {
  mozilla::PresShell* presShell = PresShell();
  nsTableFrame* lastParent = nullptr;
  while (aOldFrame) {
    nsIFrame* oldFrameNextContinuation = aOldFrame->GetNextContinuation();
    nsTableFrame* parent = static_cast<nsTableFrame*>(aOldFrame->GetParent());
    if (parent != lastParent) {
      parent->DrainSelfOverflowList();
    }
    parent->DoRemoveFrame(aContext, aListID, aOldFrame);
    aOldFrame = oldFrameNextContinuation;
    if (parent != lastParent) {
      // for now, just bail and recalc all of the collapsing borders
      // as the cellmap changes we need to recalc
      if (parent->IsBorderCollapse()) {
        parent->SetFullBCDamageArea();
      }
      parent->SetGeometryDirty();
      presShell->FrameNeedsReflow(parent, IntrinsicDirty::FrameAndAncestors,
                                  NS_FRAME_HAS_DIRTY_CHILDREN);
      lastParent = parent;
    }
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== TableFrame::RemoveFrame\n");
  Dump(true, true, true);
#endif
}

/* virtual */
nsMargin nsTableFrame::GetUsedBorder() const {
  if (!IsBorderCollapse()) {
    return nsContainerFrame::GetUsedBorder();
  }

  WritingMode wm = GetWritingMode();
  return GetOuterBCBorder(wm).GetPhysicalMargin(wm);
}

/* virtual */
nsMargin nsTableFrame::GetUsedPadding() const {
  if (!IsBorderCollapse()) {
    return nsContainerFrame::GetUsedPadding();
  }

  return nsMargin(0, 0, 0, 0);
}

/* virtual */
nsMargin nsTableFrame::GetUsedMargin() const {
  // The margin is inherited to the table wrapper frame via
  // the ::-moz-table-wrapper rule in ua.css.
  return nsMargin(0, 0, 0, 0);
}

// TODO(TYLin, dshin): This ideally should be set only in first-in-flow.
// However, the current implementation of border-collapsed table does not
// handle continuation gracefully. One concrete issue is shown in bug 1881157
// comment 3. It is also unclear if the damage area, current included in this
// property, should be stored separately per-continuation.
NS_DECLARE_FRAME_PROPERTY_DELETABLE(TableBCDataProperty, TableBCData)

TableBCData* nsTableFrame::GetTableBCData() const {
  return GetProperty(TableBCDataProperty());
}

static void DivideBCBorderSize(nscoord aPixelSize, nscoord& aSmallHalf,
                               nscoord& aLargeHalf) {
  aSmallHalf = aPixelSize / 2;
  aLargeHalf = aPixelSize - aSmallHalf;
}

LogicalMargin nsTableFrame::GetOuterBCBorder(const WritingMode aWM) const {
  if (NeedToCalcBCBorders()) {
    const_cast<nsTableFrame*>(this)->CalcBCBorders();
  }
  TableBCData* propData = GetTableBCData();
  if (propData) {
    return LogicalMargin(aWM,
                         BC_BORDER_START_HALF(propData->mBStartBorderWidth),
                         BC_BORDER_END_HALF(propData->mIEndBorderWidth),
                         BC_BORDER_END_HALF(propData->mBEndBorderWidth),
                         BC_BORDER_START_HALF(propData->mIStartBorderWidth));
  }
  return LogicalMargin(aWM);
}

void nsTableFrame::GetCollapsedBorderPadding(
    Maybe<LogicalMargin>& aBorder, Maybe<LogicalMargin>& aPadding) const {
  if (IsBorderCollapse()) {
    // Border-collapsed tables don't use any of their padding, and only part of
    // their border.
    const auto wm = GetWritingMode();
    aBorder.emplace(GetOuterBCBorder(wm));
    aPadding.emplace(wm);
  }
}

void nsTableFrame::InitChildReflowInput(ReflowInput& aReflowInput) {
  const auto childWM = aReflowInput.GetWritingMode();
  LogicalMargin border(childWM);
  if (IsBorderCollapse()) {
    nsTableRowGroupFrame* rgFrame =
        static_cast<nsTableRowGroupFrame*>(aReflowInput.mFrame);
    border = rgFrame->GetBCBorderWidth(childWM);
  }
  const LogicalMargin zeroPadding(childWM);
  aReflowInput.Init(PresContext(), Nothing(), Some(border), Some(zeroPadding));

  NS_ASSERTION(!mBits.mResizedColumns ||
                   !aReflowInput.mParentReflowInput->mFlags.mSpecialBSizeReflow,
               "should not resize columns on special bsize reflow");
  if (mBits.mResizedColumns) {
    aReflowInput.SetIResize(true);
  }
}

// Position and size aKidFrame and update our reflow input. The origin of
// aKidRect is relative to the upper-left origin of our frame
void nsTableFrame::PlaceChild(TableReflowInput& aReflowInput,
                              nsIFrame* aKidFrame,
                              const ReflowInput& aKidReflowInput,
                              const mozilla::LogicalPoint& aKidPosition,
                              const nsSize& aContainerSize,
                              ReflowOutput& aKidDesiredSize,
                              const nsRect& aOriginalKidRect,
                              const nsRect& aOriginalKidInkOverflow) {
  WritingMode wm = aReflowInput.mReflowInput.GetWritingMode();
  bool isFirstReflow = aKidFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW);

  // Place and size the child
  FinishReflowChild(aKidFrame, PresContext(), aKidDesiredSize, &aKidReflowInput,
                    wm, aKidPosition, aContainerSize,
                    ReflowChildFlags::ApplyRelativePositioning);

  InvalidateTableFrame(aKidFrame, aOriginalKidRect, aOriginalKidInkOverflow,
                       isFirstReflow);

  aReflowInput.AdvanceBCoord(aKidDesiredSize.BSize(wm));
}

auto nsTableFrame::OrderedGroups() const -> Groups {
  Groups children;
  auto& rowGroups = children.mRowGroups;
  auto& colGroups = children.mColGroups;

  nsIFrame* kidFrame = mFrames.FirstChild();
  while (kidFrame) {
    if (nsTableRowGroupFrame* rowGroup = do_QueryFrame(kidFrame)) {
      switch (kidFrame->StyleDisplay()->DisplayInside()) {
        case StyleDisplayInside::TableHeaderGroup:
          if (children.mHead) {  // treat additional thead like tbody
            rowGroups.AppendElement(rowGroup);
          } else {
            children.mHead = rowGroup;
          }
          break;
        case StyleDisplayInside::TableFooterGroup:
          if (children.mFoot) {  // treat additional tfoot like tbody
            rowGroups.AppendElement(rowGroup);
          } else {
            children.mFoot = rowGroup;
          }
          break;
        case StyleDisplayInside::TableRowGroup:
          rowGroups.AppendElement(rowGroup);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE(
              "How did this produce an nsTableRowGroupFrame?");
          // Just ignore it
          break;
      }
    } else if (nsTableColGroupFrame* cg = do_QueryFrame(kidFrame)) {
      if (cg != mSyntheticColGroup) {
        colGroups.AppendElement(cg);
      }
    }
    // Get the next sibling but skip it if it's also the next-in-flow, since
    // a next-in-flow will not be part of the current table.
    while (kidFrame) {
      nsIFrame* nif = kidFrame->GetNextInFlow();
      kidFrame = kidFrame->GetNextSibling();
      if (kidFrame != nif) {
        break;
      }
    }
  }

  // put the thead first
  if (children.mHead) {
    rowGroups.InsertElementAt(0, children.mHead);
  }
  // put the tfoot after the last tbody
  if (children.mFoot) {
    rowGroups.AppendElement(children.mFoot);
  }
  // Put the synthetic colgroup after all the non-synthetic ones.
  if (mSyntheticColGroup) {
    colGroups.AppendElement(mSyntheticColGroup);
  }
  return children;
}

static bool IsRepeatable(nscoord aFrameBSize, nscoord aPageBSize) {
  return aFrameBSize < (aPageBSize / 4);
}

nscoord nsTableFrame::SetupHeaderFooterChild(
    const TableReflowInput& aReflowInput, nsTableRowGroupFrame* aFrame) {
  nsPresContext* presContext = PresContext();
  const WritingMode wm = GetWritingMode();
  const nscoord pageBSize =
      LogicalSize(wm, presContext->GetPageSize()).BSize(wm);

  // Reflow the child with unconstrained block-size.
  LogicalSize availSize = aReflowInput.AvailableSize();
  availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;

  const nsSize containerSize =
      aReflowInput.mReflowInput.ComputedSizeAsContainerIfConstrained();
  ReflowInput kidReflowInput(presContext, aReflowInput.mReflowInput, aFrame,
                             availSize, Nothing(),
                             ReflowInput::InitFlag::CallerWillInit);
  InitChildReflowInput(kidReflowInput);
  kidReflowInput.mFlags.mIsTopOfPage = true;
  ReflowOutput desiredSize(aReflowInput.mReflowInput);
  nsReflowStatus status;
  ReflowChild(aFrame, presContext, desiredSize, kidReflowInput, wm,
              LogicalPoint(wm, aReflowInput.mICoord, aReflowInput.mBCoord),
              containerSize, ReflowChildFlags::Default, status);
  // The child will be reflowed again "for real" so no need to place it now

  aFrame->SetRepeatable(IsRepeatable(desiredSize.BSize(wm), pageBSize));
  return desiredSize.BSize(wm);
}

void nsTableFrame::PlaceRepeatedFooter(TableReflowInput& aReflowInput,
                                       nsTableRowGroupFrame* aTfoot,
                                       nscoord aFooterBSize) {
  nsPresContext* presContext = PresContext();
  const WritingMode wm = GetWritingMode();
  LogicalSize kidAvailSize = aReflowInput.AvailableSize();
  kidAvailSize.BSize(wm) = aFooterBSize;

  const nsSize containerSize =
      aReflowInput.mReflowInput.ComputedSizeAsContainerIfConstrained();
  ReflowInput footerReflowInput(presContext, aReflowInput.mReflowInput, aTfoot,
                                kidAvailSize, Nothing(),
                                ReflowInput::InitFlag::CallerWillInit);
  InitChildReflowInput(footerReflowInput);

  nsRect origTfootRect = aTfoot->GetRect();
  nsRect origTfootInkOverflow = aTfoot->InkOverflowRect();

  nsReflowStatus footerStatus;
  ReflowOutput desiredSize(aReflowInput.mReflowInput);
  LogicalPoint kidPosition(wm, aReflowInput.mICoord, aReflowInput.mBCoord);
  ReflowChild(aTfoot, presContext, desiredSize, footerReflowInput, wm,
              kidPosition, containerSize, ReflowChildFlags::Default,
              footerStatus);

  PlaceChild(aReflowInput, aTfoot, footerReflowInput, kidPosition,
             containerSize, desiredSize, origTfootRect, origTfootInkOverflow);
}

// Reflow the children based on the avail size and reason in aReflowInput
void nsTableFrame::ReflowChildren(TableReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus, Groups& aGroups,
                                  nsIFrame*& aLastChildReflowed,
                                  OverflowAreas& aOverflowAreas) {
  aStatus.Reset();
  aLastChildReflowed = nullptr;

  nsIFrame* prevKidFrame = nullptr;
  WritingMode wm = aReflowInput.mReflowInput.GetWritingMode();
  NS_WARNING_ASSERTION(
      wm.IsVertical() ||
          NS_UNCONSTRAINEDSIZE != aReflowInput.mReflowInput.ComputedWidth(),
      "shouldn't have unconstrained width in horizontal mode");
  nsSize containerSize =
      aReflowInput.mReflowInput.ComputedSizeAsContainerIfConstrained();

  nsPresContext* presContext = PresContext();
  // nsTableFrame is not able to pull back children from its next-in-flow, per
  // bug 1772383.  So even under paginated contexts, tables should not fragment
  // if they are inside of (i.e. potentially being fragmented by) a column-set
  // frame.  (This is indicated by the "mTableIsSplittable" flag.)
  bool isPaginated =
      presContext->IsPaginated() &&
      aReflowInput.mReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      aReflowInput.mReflowInput.mFlags.mTableIsSplittable;

  // Tables currently (though we ought to fix this) only fragment in
  // paginated contexts, not in multicolumn contexts.  (See bug 888257.)
  // This is partly because they don't correctly handle incremental
  // layout when paginated.
  //
  // Since we propagate NS_FRAME_IS_DIRTY from parent to child at the
  // start of the parent's reflow (behavior that's new as of bug
  // 1308876), we can do things that are effectively incremental reflow
  // during paginated layout.  Since the table code doesn't handle this
  // correctly, we need to set the flag that says to reflow everything
  // within the table structure.
  if (presContext->IsPaginated()) {
    SetGeometryDirty();
  }

  aOverflowAreas.Clear();

  bool reflowAllKids = aReflowInput.mReflowInput.ShouldReflowAllKids() ||
                       mBits.mResizedColumns || IsGeometryDirty() ||
                       NeedToCollapse();

  bool pageBreak = false;
  nscoord footerBSize = 0;

  // Determine the repeatablility of headers and footers, and also the desired
  // height of any repeatable footer.
  // The repeatability of headers on continued tables is handled
  // when they are created in nsCSSFrameConstructor::CreateContinuingTableFrame.
  // We handle the repeatability of footers again here because we need to
  // determine the footer's height anyway. We could perhaps optimize by
  // using the footer's prev-in-flow's height instead of reflowing it again,
  // but there's no real need.
  if (isPaginated) {
    bool reorder = false;
    if (aGroups.mHead && !GetPrevInFlow()) {
      reorder = aGroups.mHead->GetNextInFlow();
      SetupHeaderFooterChild(aReflowInput, aGroups.mHead);
    }
    if (aGroups.mFoot) {
      reorder = reorder || aGroups.mFoot->GetNextInFlow();
      footerBSize = SetupHeaderFooterChild(aReflowInput, aGroups.mFoot);
    }
    if (reorder) {
      // Reorder row groups - the reflow may have changed the nextinflows.
      aGroups = OrderedGroups();
    }
  }
  bool allowRepeatedFooter = false;
  for (size_t childX = 0; childX < aGroups.mRowGroups.Length(); childX++) {
    nsTableRowGroupFrame* kidFrame = aGroups.mRowGroups[childX];
    const nscoord rowSpacing =
        GetRowSpacing(kidFrame->GetStartRowIndex() + kidFrame->GetRowCount());
    // See if we should only reflow the dirty child frames
    if (reflowAllKids || kidFrame->IsSubtreeDirty() ||
        (aReflowInput.mReflowInput.mFlags.mSpecialBSizeReflow &&
         (isPaginated ||
          kidFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)))) {
      // A helper to place a repeated footer if allowed, or set it as
      // non-repeatable.
      auto MaybePlaceRepeatedFooter = [&]() {
        if (allowRepeatedFooter) {
          PlaceRepeatedFooter(aReflowInput, aGroups.mFoot, footerBSize);
        } else if (aGroups.mFoot && aGroups.mFoot->IsRepeatable()) {
          aGroups.mFoot->SetRepeatable(false);
        }
      };

      if (pageBreak) {
        MaybePlaceRepeatedFooter();
        PushChildrenToOverflow(aGroups.mRowGroups, childX);
        aStatus.Reset();
        aStatus.SetIncomplete();
        aLastChildReflowed = allowRepeatedFooter ? aGroups.mFoot : prevKidFrame;
        break;
      }

      LogicalSize kidAvailSize = aReflowInput.AvailableSize();
      allowRepeatedFooter = false;

      // If the child is a tbody in paginated mode, reduce the available
      // block-size by a repeated footer.
      if (isPaginated && (NS_UNCONSTRAINEDSIZE != kidAvailSize.BSize(wm))) {
        if (kidFrame != aGroups.mHead && kidFrame != aGroups.mFoot &&
            aGroups.mFoot && aGroups.mFoot->IsRepeatable()) {
          // the child is a tbody and there is a repeatable footer
          NS_ASSERTION(aGroups.mFoot == aGroups.mRowGroups.LastElement(),
                       "Missing footer!");
          if (footerBSize + rowSpacing < kidAvailSize.BSize(wm)) {
            allowRepeatedFooter = true;
            kidAvailSize.BSize(wm) -= footerBSize + rowSpacing;
          }
        }
      }

      nsRect oldKidRect = kidFrame->GetRect();
      nsRect oldKidInkOverflow = kidFrame->InkOverflowRect();

      ReflowOutput desiredSize(aReflowInput.mReflowInput);

      // Reflow the child into the available space
      ReflowInput kidReflowInput(presContext, aReflowInput.mReflowInput,
                                 kidFrame, kidAvailSize, Nothing(),
                                 ReflowInput::InitFlag::CallerWillInit);
      InitChildReflowInput(kidReflowInput);

      // If this isn't the first row group, and the previous row group has a
      // nonzero BEnd, then we can't be at the top of the page.
      // We ignore a repeated head row group in this check to avoid causing
      // infinite loops in some circumstances - see bug 344883.
      if (childX >
              ((aGroups.mHead && IsRepeatedFrame(aGroups.mHead)) ? 1u : 0u) &&
          (aGroups.mRowGroups[childX - 1]
               ->GetLogicalNormalRect(wm, containerSize)
               .BEnd(wm) > 0)) {
        kidReflowInput.mFlags.mIsTopOfPage = false;
      }

      // record the presence of a next in flow, it might get destroyed so we
      // need to reorder the row group array
      const bool reorder = kidFrame->GetNextInFlow();

      LogicalPoint kidPosition(wm, aReflowInput.mICoord, aReflowInput.mBCoord);
      aStatus.Reset();
      ReflowChild(kidFrame, presContext, desiredSize, kidReflowInput, wm,
                  kidPosition, containerSize, ReflowChildFlags::Default,
                  aStatus);

      if (reorder) {
        // Reorder row aGroups - the reflow may have changed the nextinflows.
        aGroups = OrderedGroups();
        childX = aGroups.mRowGroups.IndexOf(kidFrame);
        MOZ_ASSERT(childX != RowGroupArray::NoIndex,
                   "kidFrame should still be in rowGroups!");
      }
      if (isPaginated && !aStatus.IsFullyComplete() &&
          ShouldAvoidBreakInside(aReflowInput.mReflowInput)) {
        aStatus.SetInlineLineBreakBeforeAndReset();
        break;
      }
      // see if the rowgroup did not fit on this page might be pushed on
      // the next page
      if (isPaginated &&
          (aStatus.IsInlineBreakBefore() ||
           (aStatus.IsComplete() &&
            (kidReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE) &&
            kidReflowInput.AvailableBSize() < desiredSize.BSize(wm)))) {
        if (ShouldAvoidBreakInside(aReflowInput.mReflowInput)) {
          aStatus.SetInlineLineBreakBeforeAndReset();
          break;
        }
        // if we are on top of the page place with dataloss
        if (kidReflowInput.mFlags.mIsTopOfPage) {
          if (childX + 1 < aGroups.mRowGroups.Length()) {
            PlaceChild(aReflowInput, kidFrame, kidReflowInput, kidPosition,
                       containerSize, desiredSize, oldKidRect,
                       oldKidInkOverflow);
            MaybePlaceRepeatedFooter();
            aStatus.Reset();
            aStatus.SetIncomplete();
            PushChildrenToOverflow(aGroups.mRowGroups, childX + 1);
            aLastChildReflowed = allowRepeatedFooter ? aGroups.mFoot : kidFrame;
            break;
          }
        } else {  // we are not on top, push this rowgroup onto the next page
          if (prevKidFrame) {  // we had a rowgroup before so push this
            MaybePlaceRepeatedFooter();
            aStatus.Reset();
            aStatus.SetIncomplete();
            PushChildrenToOverflow(aGroups.mRowGroups, childX);
            aLastChildReflowed =
                allowRepeatedFooter ? aGroups.mFoot : prevKidFrame;
            break;
          } else {  // we can't push so lets make clear how much space we need
            PlaceChild(aReflowInput, kidFrame, kidReflowInput, kidPosition,
                       containerSize, desiredSize, oldKidRect,
                       oldKidInkOverflow);
            MaybePlaceRepeatedFooter();
            aLastChildReflowed = allowRepeatedFooter ? aGroups.mFoot : kidFrame;
            break;
          }
        }
      }

      aLastChildReflowed = kidFrame;

      pageBreak = false;
      // see if there is a page break after this row group or before the next
      // one
      if (aStatus.IsComplete() && isPaginated &&
          (kidReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE)) {
        nsIFrame* nextKid = (childX + 1 < aGroups.mRowGroups.Length())
                                ? aGroups.mRowGroups[childX + 1]
                                : nullptr;
        pageBreak = PageBreakAfter(kidFrame, nextKid);
      }

      // Place the child
      PlaceChild(aReflowInput, kidFrame, kidReflowInput, kidPosition,
                 containerSize, desiredSize, oldKidRect, oldKidInkOverflow);
      aReflowInput.AdvanceBCoord(rowSpacing);

      // Remember where we just were in case we end up pushing children
      prevKidFrame = kidFrame;

      MOZ_ASSERT(!aStatus.IsIncomplete() || isPaginated,
                 "Table contents should only fragment in paginated contexts");

      // Special handling for incomplete children
      if (isPaginated && aStatus.IsIncomplete()) {
        nsIFrame* kidNextInFlow = kidFrame->GetNextInFlow();
        if (!kidNextInFlow) {
          // The child doesn't have a next-in-flow so create a continuing
          // frame. This hooks the child into the flow
          kidNextInFlow =
              PresShell()->FrameConstructor()->CreateContinuingFrame(kidFrame,
                                                                     this);

          // Insert the kid's new next-in-flow into our sibling list...
          mFrames.InsertFrame(nullptr, kidFrame, kidNextInFlow);
          // and in rowGroups after childX so that it will get pushed below.
          aGroups.mRowGroups.InsertElementAt(
              childX + 1, static_cast<nsTableRowGroupFrame*>(kidNextInFlow));
        } else if (kidNextInFlow == kidFrame->GetNextSibling()) {
          // OrderedRowGroups excludes NIFs in the child list from 'rowGroups'
          // so we deal with that here to make sure they get pushed.
          MOZ_ASSERT(!aGroups.mRowGroups.Contains(kidNextInFlow),
                     "OrderedRowGroups must not put our NIF in 'rowGroups'");
          aGroups.mRowGroups.InsertElementAt(
              childX + 1, static_cast<nsTableRowGroupFrame*>(kidNextInFlow));
        }

        // We've used up all of our available space so push the remaining
        // children.
        MaybePlaceRepeatedFooter();
        if (kidFrame->GetNextSibling()) {
          PushChildrenToOverflow(aGroups.mRowGroups, childX + 1);
        }
        aLastChildReflowed = allowRepeatedFooter ? aGroups.mFoot : kidFrame;
        break;
      }
    } else {  // it isn't being reflowed
      aReflowInput.AdvanceBCoord(rowSpacing);
      const LogicalRect kidRect =
          kidFrame->GetLogicalNormalRect(wm, containerSize);
      if (kidRect.BStart(wm) != aReflowInput.mBCoord) {
        // invalidate the old position
        kidFrame->InvalidateFrameSubtree();
        // move to the new position
        kidFrame->MovePositionBy(
            wm, LogicalPoint(wm, 0, aReflowInput.mBCoord - kidRect.BStart(wm)));
        // invalidate the new position
        kidFrame->InvalidateFrameSubtree();
      }

      aReflowInput.AdvanceBCoord(kidRect.BSize(wm));
    }
  }

  for (nsTableColGroupFrame* cg : aGroups.mColGroups) {
    // The column groups don't care about dimensions or reflow inputs. In fact,
    // we mostly need to do this to deal with dynamic style changes to a
    // colgroup having to reflow the table with border-collapsing.
    if (!cg->IsSubtreeDirty()) {
      continue;
    }
    ReflowOutput kidSize(wm);
    ReflowInput kidReflowInput(presContext, cg,
                               aReflowInput.mReflowInput.mRenderingContext,
                               LogicalSize(cg->GetWritingMode()));
    nsReflowStatus cgStatus;
    const LogicalPoint dummyPos(wm);
    const nsSize dummyContainerSize;
    ReflowChild(cg, presContext, kidSize, kidReflowInput, wm, dummyPos,
                dummyContainerSize, ReflowChildFlags::Default, cgStatus);
    FinishReflowChild(cg, presContext, kidSize, &kidReflowInput, wm, dummyPos,
                      dummyContainerSize, ReflowChildFlags::Default);
  }

  // We've now propagated the column resizes and geometry changes to all
  // the children.
  mBits.mResizedColumns = false;
  ClearGeometryDirty();

  // nsTableFrame does not pull children from its next-in-flow (bug 1772383).
  // This is generally fine, since tables only fragment for printing
  // (bug 888257) where incremental-reflow is impossible, and so children don't
  // usually dynamically move back and forth between continuations. However,
  // there are edge cases even with printing where nsTableFrame:
  // (1) Generates a continuation and passes children to it,
  // (2) Receives another call to Reflow, during which it
  // (3) Successfully lays out its remaining children.
  // If the completed status flows up as-is, the continuation will be destroyed.
  // To avoid that, we return an incomplete status if the continuation contains
  // any child that is not a repeated frame.
  auto hasNextInFlowThatMustBePreserved = [this, isPaginated]() -> bool {
    if (!isPaginated) {
      return false;
    }
    auto* nextInFlow = static_cast<nsTableFrame*>(GetNextInFlow());
    if (!nextInFlow) {
      return false;
    }
    for (nsIFrame* kidFrame : nextInFlow->mFrames) {
      if (!IsRepeatedFrame(kidFrame)) {
        return true;
      }
    }
    return false;
  };
  if (aStatus.IsComplete() && hasNextInFlowThatMustBePreserved()) {
    aStatus.SetIncomplete();
  }
}

nscoord nsTableFrame::CalcDesiredBSize(const ReflowInput& aReflowInput,
                                       const LogicalMargin& aBorderPadding,
                                       const nsReflowStatus& aStatus) {
  WritingMode wm = aReflowInput.GetWritingMode();

  auto rowGroups = OrderedGroups().mRowGroups;
  if (rowGroups.IsEmpty()) {
    if (eCompatibility_NavQuirks == PresContext()->CompatibilityMode()) {
      // empty tables should not have a size in quirks mode
      return 0;
    }
    return CalcBorderBoxBSize(aReflowInput, aBorderPadding,
                              aBorderPadding.BStartEnd(wm));
  }

  nsTableCellMap* cellMap = GetCellMap();
  MOZ_ASSERT(cellMap);
  int32_t rowCount = cellMap->GetRowCount();
  int32_t colCount = cellMap->GetColCount();
  nscoord desiredBSize = aBorderPadding.BStartEnd(wm);
  if (rowCount > 0 && colCount > 0) {
    if (!GetPrevInFlow()) {
      desiredBSize += GetRowSpacing(-1);
    }
    const nsTableRowGroupFrame* lastRG = rowGroups.LastElement();
    for (nsTableRowGroupFrame* rg : rowGroups) {
      desiredBSize += rg->BSize(wm);
      if (rg != lastRG || aStatus.IsFullyComplete()) {
        desiredBSize +=
            GetRowSpacing(rg->GetStartRowIndex() + rg->GetRowCount());
      }
    }
    if (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE &&
        aStatus.IsIncomplete()) {
      desiredBSize = std::max(desiredBSize, aReflowInput.AvailableBSize());
    }
  }

  // see if a specified table bsize requires dividing additional space to rows
  if (!GetPrevInFlow()) {
    nscoord bSize =
        CalcBorderBoxBSize(aReflowInput, aBorderPadding, desiredBSize);
    if (bSize > desiredBSize) {
      // proportionately distribute the excess bsize to unconstrained rows in
      // each unconstrained row group.
      DistributeBSizeToRows(aReflowInput, bSize - desiredBSize);
      return bSize;
    }
    // Tables don't shrink below their intrinsic size, apparently, even when
    // constrained by stuff like flex / grid or what not.
    return desiredBSize;
  }

  // FIXME(emilio): Is this right? This only affects fragmented tables...
  return desiredBSize;
}

static void ResizeCells(nsTableFrame& aTableFrame) {
  auto rowGroups = aTableFrame.OrderedGroups().mRowGroups;
  WritingMode wm = aTableFrame.GetWritingMode();
  ReflowOutput tableDesiredSize(wm);
  tableDesiredSize.SetSize(wm, aTableFrame.GetLogicalSize(wm));
  tableDesiredSize.SetOverflowAreasToDesiredBounds();

  for (uint32_t rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];

    ReflowOutput groupDesiredSize(wm);
    groupDesiredSize.SetSize(wm, rgFrame->GetLogicalSize(wm));
    groupDesiredSize.SetOverflowAreasToDesiredBounds();

    nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
    while (rowFrame) {
      rowFrame->DidResize();
      rgFrame->ConsiderChildOverflow(groupDesiredSize.mOverflowAreas, rowFrame);
      rowFrame = rowFrame->GetNextRow();
    }
    rgFrame->FinishAndStoreOverflow(&groupDesiredSize);
    tableDesiredSize.mOverflowAreas.UnionWith(groupDesiredSize.mOverflowAreas +
                                              rgFrame->GetPosition());
  }
  aTableFrame.FinishAndStoreOverflow(&tableDesiredSize);
}

void nsTableFrame::DistributeBSizeToRows(const ReflowInput& aReflowInput,
                                         nscoord aAmount) {
  WritingMode wm = aReflowInput.GetWritingMode();
  LogicalMargin borderPadding = aReflowInput.ComputedLogicalBorderPadding(wm);

  nsSize containerSize = aReflowInput.ComputedSizeAsContainerIfConstrained();

  auto rowGroups = OrderedGroups().mRowGroups;

  nscoord amountUsed = 0;
  // distribute space to each pct bsize row whose row group doesn't have a
  // computed bsize, and base the pct on the table bsize. If the row group had a
  // computed bsize, then this was already done in
  // nsTableRowGroupFrame::CalculateRowBSizes
  nscoord pctBasis =
      aReflowInput.ComputedBSize() - GetRowSpacing(-1, GetRowCount());
  nscoord bOriginRG = borderPadding.BStart(wm) + GetRowSpacing(0);
  nscoord bEndRG = bOriginRG;
  uint32_t rgIdx;
  for (rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
    nscoord amountUsedByRG = 0;
    nscoord bOriginRow = 0;
    const LogicalRect rgNormalRect =
        rgFrame->GetLogicalNormalRect(wm, containerSize);
    if (!rgFrame->HasStyleBSize()) {
      nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
      while (rowFrame) {
        // We don't know the final width of the rowGroupFrame yet, so use 0,0
        // as a dummy containerSize here; we'll adjust the row positions at
        // the end, after the rowGroup size is finalized.
        const nsSize dummyContainerSize;
        const LogicalRect rowNormalRect =
            rowFrame->GetLogicalNormalRect(wm, dummyContainerSize);
        const nscoord rowSpacing = GetRowSpacing(rowFrame->GetRowIndex());
        if ((amountUsed < aAmount) && rowFrame->HasPctBSize()) {
          nscoord pctBSize = rowFrame->GetInitialBSize(pctBasis);
          nscoord amountForRow = std::min(aAmount - amountUsed,
                                          pctBSize - rowNormalRect.BSize(wm));
          if (amountForRow > 0) {
            // XXXbz we don't need to move the row's b-position to bOriginRow?
            nsRect origRowRect = rowFrame->GetRect();
            nscoord newRowBSize = rowNormalRect.BSize(wm) + amountForRow;
            rowFrame->SetSize(
                wm, LogicalSize(wm, rowNormalRect.ISize(wm), newRowBSize));
            bOriginRow += newRowBSize + rowSpacing;
            bEndRG += newRowBSize + rowSpacing;
            amountUsed += amountForRow;
            amountUsedByRG += amountForRow;
            // rowFrame->DidResize();

            rgFrame->InvalidateFrameWithRect(origRowRect);
            rgFrame->InvalidateFrame();
          }
        } else {
          if (amountUsed > 0 && bOriginRow != rowNormalRect.BStart(wm) &&
              !HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
            rowFrame->InvalidateFrameSubtree();
            rowFrame->MovePositionBy(
                wm, LogicalPoint(wm, 0, bOriginRow - rowNormalRect.BStart(wm)));
            rowFrame->InvalidateFrameSubtree();
          }
          bOriginRow += rowNormalRect.BSize(wm) + rowSpacing;
          bEndRG += rowNormalRect.BSize(wm) + rowSpacing;
        }
        rowFrame = rowFrame->GetNextRow();
      }
      if (amountUsed > 0) {
        if (rgNormalRect.BStart(wm) != bOriginRG) {
          rgFrame->InvalidateFrameSubtree();
        }

        nsRect origRgNormalRect = rgFrame->GetRect();
        nsRect origRgInkOverflow = rgFrame->InkOverflowRect();

        rgFrame->MovePositionBy(
            wm, LogicalPoint(wm, 0, bOriginRG - rgNormalRect.BStart(wm)));
        rgFrame->SetSize(wm,
                         LogicalSize(wm, rgNormalRect.ISize(wm),
                                     rgNormalRect.BSize(wm) + amountUsedByRG));

        nsTableFrame::InvalidateTableFrame(rgFrame, origRgNormalRect,
                                           origRgInkOverflow, false);
      }
    } else if (amountUsed > 0 && bOriginRG != rgNormalRect.BStart(wm)) {
      rgFrame->InvalidateFrameSubtree();
      rgFrame->MovePositionBy(
          wm, LogicalPoint(wm, 0, bOriginRG - rgNormalRect.BStart(wm)));
      // Make sure child views are properly positioned
      rgFrame->InvalidateFrameSubtree();
    }
    bOriginRG = bEndRG;
  }

  if (amountUsed >= aAmount) {
    ResizeCells(*this);
    return;
  }

  // get the first row without a style bsize where its row group has an
  // unconstrained bsize
  nsTableRowGroupFrame* firstUnStyledRG = nullptr;
  nsTableRowFrame* firstUnStyledRow = nullptr;
  for (rgIdx = 0; rgIdx < rowGroups.Length() && !firstUnStyledRG; rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
    if (!rgFrame->HasStyleBSize()) {
      nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
      while (rowFrame) {
        if (!rowFrame->HasStyleBSize()) {
          firstUnStyledRG = rgFrame;
          firstUnStyledRow = rowFrame;
          break;
        }
        rowFrame = rowFrame->GetNextRow();
      }
    }
  }

  nsTableRowFrame* lastEligibleRow = nullptr;
  // Accumulate the correct divisor. This will be the total bsize of all
  // unstyled rows inside unstyled row groups, unless there are none, in which
  // case, it will be number of all rows. If the unstyled rows don't have a
  // bsize, divide the space equally among them.
  nscoord divisor = 0;
  int32_t eligibleRows = 0;
  bool expandEmptyRows = false;

  if (!firstUnStyledRow) {
    // there is no unstyled row
    divisor = GetRowCount();
  } else {
    for (rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
      nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
      if (!firstUnStyledRG || !rgFrame->HasStyleBSize()) {
        nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
        while (rowFrame) {
          if (!firstUnStyledRG || !rowFrame->HasStyleBSize()) {
            NS_ASSERTION(rowFrame->BSize(wm) >= 0,
                         "negative row frame block-size");
            divisor += rowFrame->BSize(wm);
            eligibleRows++;
            lastEligibleRow = rowFrame;
          }
          rowFrame = rowFrame->GetNextRow();
        }
      }
    }
    if (divisor <= 0) {
      if (eligibleRows > 0) {
        expandEmptyRows = true;
      } else {
        NS_ERROR("invalid divisor");
        return;
      }
    }
  }
  // allocate the extra bsize to the unstyled row groups and rows
  nscoord bSizeToDistribute = aAmount - amountUsed;
  bOriginRG = borderPadding.BStart(wm) + GetRowSpacing(-1);
  bEndRG = bOriginRG;
  for (rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
    nscoord amountUsedByRG = 0;
    nscoord bOriginRow = 0;
    const LogicalRect rgNormalRect =
        rgFrame->GetLogicalNormalRect(wm, containerSize);
    nsRect rgInkOverflow = rgFrame->InkOverflowRect();
    // see if there is an eligible row group or we distribute to all rows
    if (!firstUnStyledRG || !rgFrame->HasStyleBSize() || !eligibleRows) {
      for (nsTableRowFrame* rowFrame = rgFrame->GetFirstRow(); rowFrame;
           rowFrame = rowFrame->GetNextRow()) {
        const nscoord rowSpacing = GetRowSpacing(rowFrame->GetRowIndex());
        // We don't know the final width of the rowGroupFrame yet, so use 0,0
        // as a dummy containerSize here; we'll adjust the row positions at
        // the end, after the rowGroup size is finalized.
        const nsSize dummyContainerSize;
        const LogicalRect rowNormalRect =
            rowFrame->GetLogicalNormalRect(wm, dummyContainerSize);
        nsRect rowInkOverflow = rowFrame->InkOverflowRect();
        // see if there is an eligible row or we distribute to all rows
        if (!firstUnStyledRow || !rowFrame->HasStyleBSize() || !eligibleRows) {
          float ratio;
          if (eligibleRows) {
            if (!expandEmptyRows) {
              // The amount of additional space each row gets is proportional
              // to its bsize
              ratio = float(rowNormalRect.BSize(wm)) / float(divisor);
            } else {
              // empty rows get all the same additional space
              ratio = 1.0f / float(eligibleRows);
            }
          } else {
            // all rows get the same additional space
            ratio = 1.0f / float(divisor);
          }
          // give rows their additional space, except for the last row which
          // gets the remainder
          nscoord amountForRow =
              (rowFrame == lastEligibleRow)
                  ? aAmount - amountUsed
                  : NSToCoordRound(((float)(bSizeToDistribute)) * ratio);
          amountForRow = std::min(amountForRow, aAmount - amountUsed);

          if (bOriginRow != rowNormalRect.BStart(wm)) {
            rowFrame->InvalidateFrameSubtree();
          }

          // update the row bsize
          nsRect origRowRect = rowFrame->GetRect();
          nscoord newRowBSize = rowNormalRect.BSize(wm) + amountForRow;
          rowFrame->MovePositionBy(
              wm, LogicalPoint(wm, 0, bOriginRow - rowNormalRect.BStart(wm)));
          rowFrame->SetSize(
              wm, LogicalSize(wm, rowNormalRect.ISize(wm), newRowBSize));

          bOriginRow += newRowBSize + rowSpacing;
          bEndRG += newRowBSize + rowSpacing;

          amountUsed += amountForRow;
          amountUsedByRG += amountForRow;
          NS_ASSERTION((amountUsed <= aAmount), "invalid row allocation");
          // rowFrame->DidResize();
          nsTableFrame::InvalidateTableFrame(rowFrame, origRowRect,
                                             rowInkOverflow, false);
        } else {
          if (amountUsed > 0 && bOriginRow != rowNormalRect.BStart(wm)) {
            rowFrame->InvalidateFrameSubtree();
            rowFrame->MovePositionBy(
                wm, LogicalPoint(wm, 0, bOriginRow - rowNormalRect.BStart(wm)));
            rowFrame->InvalidateFrameSubtree();
          }
          bOriginRow += rowNormalRect.BSize(wm) + rowSpacing;
          bEndRG += rowNormalRect.BSize(wm) + rowSpacing;
        }
      }

      if (amountUsed > 0) {
        if (rgNormalRect.BStart(wm) != bOriginRG) {
          rgFrame->InvalidateFrameSubtree();
        }

        nsRect origRgNormalRect = rgFrame->GetRect();
        rgFrame->MovePositionBy(
            wm, LogicalPoint(wm, 0, bOriginRG - rgNormalRect.BStart(wm)));
        rgFrame->SetSize(wm,
                         LogicalSize(wm, rgNormalRect.ISize(wm),
                                     rgNormalRect.BSize(wm) + amountUsedByRG));

        nsTableFrame::InvalidateTableFrame(rgFrame, origRgNormalRect,
                                           rgInkOverflow, false);
      }

      // For vertical-rl mode, we needed to position the rows relative to the
      // right-hand (block-start) side of the group; but we couldn't do that
      // above, as we didn't know the rowGroupFrame's final block size yet.
      // So we used a dummyContainerSize of 0,0 earlier, placing the rows to
      // the left of the rowGroupFrame's (physical) origin. Now we move them
      // all rightwards by its final width.
      if (wm.IsVerticalRL()) {
        nscoord rgWidth = rgFrame->GetSize().width;
        for (nsTableRowFrame* rowFrame = rgFrame->GetFirstRow(); rowFrame;
             rowFrame = rowFrame->GetNextRow()) {
          rowFrame->InvalidateFrameSubtree();
          rowFrame->MovePositionBy(nsPoint(rgWidth, 0));
          rowFrame->InvalidateFrameSubtree();
        }
      }
    } else if (amountUsed > 0 && bOriginRG != rgNormalRect.BStart(wm)) {
      rgFrame->InvalidateFrameSubtree();
      rgFrame->MovePositionBy(
          wm, LogicalPoint(wm, 0, bOriginRG - rgNormalRect.BStart(wm)));
      // Make sure child views are properly positioned
      rgFrame->InvalidateFrameSubtree();
    }
    bOriginRG = bEndRG;
  }

  ResizeCells(*this);
}

nscoord nsTableFrame::GetColumnISizeFromFirstInFlow(int32_t aColIndex) {
  MOZ_ASSERT(this == FirstInFlow());
  nsTableColFrame* colFrame = GetColFrame(aColIndex);
  return colFrame ? colFrame->GetFinalISize() : 0;
}

nscoord nsTableFrame::GetColSpacing() {
  if (IsBorderCollapse()) {
    return 0;
  }
  return StyleTableBorder()->mBorderSpacing.width.ToAppUnits();
}

// XXX: could cache this.  But be sure to check style changes if you do!
nscoord nsTableFrame::GetColSpacing(int32_t aColIndex) {
  NS_ASSERTION(aColIndex >= -1 && aColIndex <= GetColCount(),
               "Column index exceeds the bounds of the table");
  // Index is irrelevant for ordinary tables.  We check that it falls within
  // appropriate bounds to increase confidence of correctness in situations
  // where it does matter.
  return GetColSpacing();
}

nscoord nsTableFrame::GetColSpacing(int32_t aStartColIndex,
                                    int32_t aEndColIndex) {
  NS_ASSERTION(aStartColIndex >= -1 && aStartColIndex <= GetColCount(),
               "Start column index exceeds the bounds of the table");
  NS_ASSERTION(aEndColIndex >= -1 && aEndColIndex <= GetColCount(),
               "End column index exceeds the bounds of the table");
  NS_ASSERTION(aStartColIndex <= aEndColIndex,
               "End index must not be less than start index");
  // Only one possible value so just multiply it out. Tables where index
  // matters will override this function
  return GetColSpacing() * (aEndColIndex - aStartColIndex);
}

nscoord nsTableFrame::GetRowSpacing() {
  if (IsBorderCollapse()) {
    return 0;
  }
  return StyleTableBorder()->mBorderSpacing.height.ToAppUnits();
}

// XXX: could cache this. But be sure to check style changes if you do!
nscoord nsTableFrame::GetRowSpacing(int32_t aRowIndex) {
  NS_ASSERTION(aRowIndex >= -1 && aRowIndex <= GetRowCount(),
               "Row index exceeds the bounds of the table");
  // Index is irrelevant for ordinary tables.  We check that it falls within
  // appropriate bounds to increase confidence of correctness in situations
  // where it does matter.
  return GetRowSpacing();
}

nscoord nsTableFrame::GetRowSpacing(int32_t aStartRowIndex,
                                    int32_t aEndRowIndex) {
  NS_ASSERTION(aStartRowIndex >= -1 && aStartRowIndex <= GetRowCount(),
               "Start row index exceeds the bounds of the table");
  NS_ASSERTION(aEndRowIndex >= -1 && aEndRowIndex <= GetRowCount(),
               "End row index exceeds the bounds of the table");
  NS_ASSERTION(aStartRowIndex <= aEndRowIndex,
               "End index must not be less than start index");
  // Only one possible value so just multiply it out. Tables where index
  // matters will override this function
  return GetRowSpacing() * (aEndRowIndex - aStartRowIndex);
}

/* virtual */
Maybe<nscoord> nsTableFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (StyleDisplay()->IsContainLayout()) {
    return Nothing{};
  }

  RowGroupArray orderedRowGroups = OrderedRowGroups();
  // XXX not sure if this should be the size of the containing block instead.
  nsSize containerSize = mRect.Size();
  auto TableBaseline = [aWM, containerSize](
                           nsTableRowGroupFrame* aRowGroup,
                           nsTableRowFrame* aRow) -> Maybe<nscoord> {
    const nscoord rgBStart =
        aRowGroup->GetLogicalNormalRect(aWM, containerSize).BStart(aWM);
    const nscoord rowBStart =
        aRow->GetLogicalNormalRect(aWM, aRowGroup->GetSize()).BStart(aWM);
    return aRow->GetRowBaseline(aWM).map(
        [rgBStart, rowBStart](nscoord aBaseline) {
          return rgBStart + rowBStart + aBaseline;
        });
  };
  if (aBaselineGroup == BaselineSharingGroup::First) {
    for (uint32_t rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
      nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgIndex];
      nsTableRowFrame* row = rgFrame->GetFirstRow();
      if (row) {
        return TableBaseline(rgFrame, row);
      }
    }
  } else {
    for (uint32_t rgIndex = orderedRowGroups.Length(); rgIndex-- > 0;) {
      nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgIndex];
      nsTableRowFrame* row = rgFrame->GetLastRow();
      if (row) {
        return TableBaseline(rgFrame, row).map([this, aWM](nscoord aBaseline) {
          return BSize(aWM) - aBaseline;
        });
      }
    }
  }
  return Nothing{};
}

/* ----- global methods ----- */

nsTableFrame* NS_NewTableFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsTableFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTableFrame)

nsTableFrame* nsTableFrame::GetTableFrame(nsIFrame* aFrame) {
  for (nsIFrame* ancestor = aFrame->GetParent(); ancestor;
       ancestor = ancestor->GetParent()) {
    if (ancestor->IsTableFrame()) {
      return static_cast<nsTableFrame*>(ancestor);
    }
  }
  MOZ_CRASH("unable to find table parent");
  return nullptr;
}

bool nsTableFrame::IsAutoBSize(WritingMode aWM) {
  const auto bsize =
      StylePosition()->BSize(aWM, AnchorPosResolutionParams::From(this));
  if (bsize->IsAuto()) {
    return true;
  }
  return bsize->ConvertsToPercentage() && bsize->ToPercentage() <= 0.0f;
}

nscoord nsTableFrame::CalcBorderBoxBSize(const ReflowInput& aReflowInput,
                                         const LogicalMargin& aBorderPadding,
                                         nscoord aIntrinsicBorderBoxBSize) {
  WritingMode wm = aReflowInput.GetWritingMode();
  nscoord bSize = aReflowInput.ComputedBSize();
  nscoord bp = aBorderPadding.BStartEnd(wm);
  if (bSize == NS_UNCONSTRAINEDSIZE) {
    if (aIntrinsicBorderBoxBSize == NS_UNCONSTRAINEDSIZE) {
      return NS_UNCONSTRAINEDSIZE;
    }
    bSize = std::max(0, aIntrinsicBorderBoxBSize - bp);
  }
  return aReflowInput.ApplyMinMaxBSize(bSize) + bp;
}

bool nsTableFrame::IsAutoLayout() {
  if (StyleTable()->mLayoutStrategy == StyleTableLayout::Auto) {
    return true;
  }
  // a fixed-layout inline-table must have a inline size
  // and tables with inline size set to 'max-content' must be
  // auto-layout (at least as long as
  // FixedTableLayoutStrategy::GetPrefISize returns nscoord_MAX)
  const auto iSize = StylePosition()->ISize(
      GetWritingMode(), AnchorPosResolutionParams::From(this));
  return iSize->IsAuto() || iSize->IsMaxContent();
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsTableFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Table"_ns, aResult);
}
#endif

// Find the closet sibling before aPriorChildFrame (including aPriorChildFrame)
// that is of type aChildType
nsIFrame* nsTableFrame::GetFrameAtOrBefore(nsIFrame* aParentFrame,
                                           nsIFrame* aPriorChildFrame,
                                           LayoutFrameType aChildType) {
  nsIFrame* result = nullptr;
  if (!aPriorChildFrame) {
    return result;
  }
  if (aChildType == aPriorChildFrame->Type()) {
    return aPriorChildFrame;
  }

  // aPriorChildFrame is not of type aChildType, so we need start from
  // the beginnng and find the closest one
  nsIFrame* lastMatchingFrame = nullptr;
  nsIFrame* childFrame = aParentFrame->PrincipalChildList().FirstChild();
  while (childFrame && (childFrame != aPriorChildFrame)) {
    if (aChildType == childFrame->Type()) {
      lastMatchingFrame = childFrame;
    }
    childFrame = childFrame->GetNextSibling();
  }
  return lastMatchingFrame;
}

#ifdef DEBUG
void nsTableFrame::DumpRowGroup(nsIFrame* aKidFrame) {
  if (!aKidFrame) return;

  for (nsIFrame* cFrame : aKidFrame->PrincipalChildList()) {
    nsTableRowFrame* rowFrame = do_QueryFrame(cFrame);
    if (rowFrame) {
      printf("row(%d)=%p ", rowFrame->GetRowIndex(),
             static_cast<void*>(rowFrame));
      for (nsIFrame* childFrame : cFrame->PrincipalChildList()) {
        nsTableCellFrame* cellFrame = do_QueryFrame(childFrame);
        if (cellFrame) {
          uint32_t colIndex = cellFrame->ColIndex();
          printf("cell(%u)=%p ", colIndex, static_cast<void*>(childFrame));
        }
      }
      printf("\n");
    } else {
      DumpRowGroup(rowFrame);
    }
  }
}

void nsTableFrame::VerifyColFrames() {
  for (int32_t i = 0; i < int32_t(mColFrames.Length()); i++) {
    nsTableColFrame* col = mColFrames[i];
    MOZ_ASSERT(col);
    MOZ_ASSERT(col->GetColIndex() == i);
    MOZ_ASSERT(col->GetParent());
    MOZ_ASSERT(col->GetParent()->IsTableColGroupFrame());
    auto* cg = static_cast<nsTableColGroupFrame*>(col->GetParent());
    int32_t cgStart = cg->GetStartColumnIndex();
    int32_t cgEnd = cgStart + cg->GetColCount();
    MOZ_ASSERT(i >= cgStart && i < cgEnd,
               "col index not within its colgroup's range");
  }
}

void nsTableFrame::Dump(bool aDumpRows, bool aDumpCols, bool aDumpCellMap) {
  printf("***START TABLE DUMP*** \n");
  // dump the columns widths array
  printf("mColWidths=");
  int32_t numCols = GetColCount();
  int32_t colIdx;
  nsTableFrame* fif = static_cast<nsTableFrame*>(FirstInFlow());
  for (colIdx = 0; colIdx < numCols; colIdx++) {
    printf("%d ", fif->GetColumnISizeFromFirstInFlow(colIdx));
  }
  printf("\n");

  if (aDumpRows) {
    nsIFrame* kidFrame = mFrames.FirstChild();
    while (kidFrame) {
      if (kidFrame->IsTableRowGroupFrame()) {
        DumpRowGroup(kidFrame);
      }
      kidFrame = kidFrame->GetNextSibling();
    }
  }

  if (aDumpCols) {
    // output col frame cache
    printf("\n col frame cache ->");
    for (colIdx = 0; colIdx < numCols; colIdx++) {
      nsTableColFrame* colFrame = mColFrames.ElementAt(colIdx);
      if (0 == (colIdx % 8)) {
        printf("\n");
      }
      printf("%d=%p ", colIdx, static_cast<void*>(colFrame));
      nsTableColType colType = colFrame->GetColType();
      switch (colType) {
        case eColContent:
          printf(" content ");
          break;
        case eColAnonymousCol:
          printf(" anonymous-column ");
          break;
        case eColAnonymousColGroup:
          printf(" anonymous-colgroup ");
          break;
        case eColAnonymousCell:
          printf(" anonymous-cell ");
          break;
      }
    }
    printf("\n colgroups->");
    for (nsIFrame* childFrame : mFrames) {
      if (nsTableColGroupFrame* cg = do_QueryFrame(childFrame)) {
        cg->Dump(1);
      }
    }
    for (colIdx = 0; colIdx < numCols; colIdx++) {
      printf("\n");
      nsTableColFrame* colFrame = GetColFrame(colIdx);
      colFrame->Dump(1);
    }
  }
  if (aDumpCellMap) {
    nsTableCellMap* cellMap = GetCellMap();
    cellMap->Dump();
  }
  printf(" ***END TABLE DUMP*** \n");
}
#endif

bool nsTableFrame::ColumnHasCellSpacingBefore(int32_t aColIndex) const {
  if (aColIndex == 0) {
    return true;
  }
  // Since fixed-layout tables should not have their column sizes change
  // as they load, we assume that all columns are significant.
  auto* fif = static_cast<nsTableFrame*>(FirstInFlow());
  if (fif->LayoutStrategy()->GetType() == nsITableLayoutStrategy::Fixed) {
    return true;
  }
  nsTableCellMap* cellMap = fif->GetCellMap();
  if (!cellMap) {
    return false;
  }
  if (cellMap->GetNumCellsOriginatingInCol(aColIndex) > 0) {
    return true;
  }
  // Check if we have a <col> element with a non-zero definite inline size.
  // Note: percentages and calc(%) are intentionally not considered.
  if (const auto* col = fif->GetColFrame(aColIndex)) {
    const auto anchorResolutionParams = AnchorPosResolutionParams::From(col);
    const auto iSize =
        col->StylePosition()->ISize(GetWritingMode(), anchorResolutionParams);
    if (iSize->ConvertsToLength() && iSize->ToLength() > 0) {
      const auto maxISize = col->StylePosition()->MaxISize(
          GetWritingMode(), anchorResolutionParams);
      if (!maxISize->ConvertsToLength() || maxISize->ToLength() > 0) {
        return true;
      }
    }
    const auto minISize = col->StylePosition()->MinISize(
        GetWritingMode(), anchorResolutionParams);
    if (minISize->ConvertsToLength() && minISize->ToLength() > 0) {
      return true;
    }
  }
  return false;
}

/********************************************************************************
 * Collapsing Borders
 *
 *  The CSS spec says to resolve border conflicts in this order:
 *  1) any border with the style HIDDEN wins
 *  2) the widest border with a style that is not NONE wins
 *  3) the border styles are ranked in this order, highest to lowest precedence:
 *     double, solid, dashed, dotted, ridge, outset, groove, inset
 *  4) borders that are of equal width and style (differ only in color) have
 *     this precedence: cell, row, rowgroup, col, colgroup, table
 *  5) if all border styles are NONE, then that's the computed border style.
 *******************************************************************************/

#ifdef DEBUG
#  define VerifyNonNegativeDamageRect(r)                       \
    NS_ASSERTION((r).StartCol() >= 0, "negative col index");   \
    NS_ASSERTION((r).StartRow() >= 0, "negative row index");   \
    NS_ASSERTION((r).ColCount() >= 0, "negative cols damage"); \
    NS_ASSERTION((r).RowCount() >= 0, "negative rows damage");
#  define VerifyDamageRect(r)                          \
    VerifyNonNegativeDamageRect(r);                    \
    NS_ASSERTION((r).EndCol() <= GetColCount(),        \
                 "cols damage extends outside table"); \
    NS_ASSERTION((r).EndRow() <= GetRowCount(),        \
                 "rows damage extends outside table");
#endif

void nsTableFrame::AddBCDamageArea(const TableArea& aValue) {
  MOZ_ASSERT(IsBorderCollapse(),
             "Why call this if we are not border-collapsed?");
#ifdef DEBUG
  VerifyDamageRect(aValue);
#endif

  SetNeedToCalcBCBorders(true);
  SetNeedToCalcHasBCBorders(true);
  // Get the property
  TableBCData* value = GetOrCreateDeletableProperty(TableBCDataProperty());

#ifdef DEBUG
  VerifyNonNegativeDamageRect(value->mDamageArea);
#endif
  // Clamp the old damage area to the current table area in case it shrunk.
  int32_t cols = GetColCount();
  if (value->mDamageArea.EndCol() > cols) {
    if (value->mDamageArea.StartCol() > cols) {
      value->mDamageArea.StartCol() = cols;
      value->mDamageArea.ColCount() = 0;
    } else {
      value->mDamageArea.ColCount() = cols - value->mDamageArea.StartCol();
    }
  }
  int32_t rows = GetRowCount();
  if (value->mDamageArea.EndRow() > rows) {
    if (value->mDamageArea.StartRow() > rows) {
      value->mDamageArea.StartRow() = rows;
      value->mDamageArea.RowCount() = 0;
    } else {
      value->mDamageArea.RowCount() = rows - value->mDamageArea.StartRow();
    }
  }

  // Construct a union of the new and old damage areas.
  value->mDamageArea.UnionArea(value->mDamageArea, aValue);
}

void nsTableFrame::SetFullBCDamageArea() {
  MOZ_ASSERT(IsBorderCollapse(),
             "Why call this if we are not border-collapsed?");

  SetNeedToCalcBCBorders(true);
  SetNeedToCalcHasBCBorders(true);

  TableBCData* value = GetOrCreateDeletableProperty(TableBCDataProperty());
  value->mDamageArea = TableArea(0, 0, GetColCount(), GetRowCount());
}

/* BCCellBorder represents a border segment which can be either an inline-dir
 * or a block-dir segment. For each segment we need to know the color, width,
 * style, who owns it and how long it is in cellmap coordinates.
 * Ownership of these segments is important to calculate which corners should
 * be bevelled. This structure has dual use, its used first to compute the
 * dominant border for inline-dir and block-dir segments and to store the
 * preliminary computed border results in the BCCellBorders structure.
 * This temporary storage is not symmetric with respect to inline-dir and
 * block-dir border segments, its always column oriented. For each column in
 * the cellmap there is a temporary stored block-dir and inline-dir segment.
 * XXX_Bernd this asymmetry is the root of those rowspan bc border errors
 */
struct BCCellBorder {
  BCCellBorder() { Reset(0, 1); }
  void Reset(uint32_t aRowIndex, uint32_t aRowSpan);
  nscolor color;           // border segment color
  nscoord width;           // border segment width
  StyleBorderStyle style;  // border segment style, possible values are defined
                           // in nsStyleConsts.h as StyleBorderStyle::*
  BCBorderOwner owner;     // border segment owner, possible values are defined
                           // in celldata.h. In the cellmap for each border
                           // segment we store the owner and later when
                           // painting we know the owner and can retrieve the
                           // style info from the corresponding frame
  int32_t rowIndex;        // rowIndex of temporary stored inline-dir border
                           // segments relative to the table
  int32_t rowSpan;         // row span of temporary stored inline-dir border
                           // segments
};

void BCCellBorder::Reset(uint32_t aRowIndex, uint32_t aRowSpan) {
  style = StyleBorderStyle::None;
  color = 0;
  width = 0;
  owner = eTableOwner;
  rowIndex = aRowIndex;
  rowSpan = aRowSpan;
}

class BCMapCellIterator;

/*****************************************************************
 *  BCMapCellInfo
 * This structure stores information during the computation of winning borders
 * in CalcBCBorders, so that they don't need to be looked up repeatedly.
 ****************************************************************/
struct BCMapCellInfo final {
  explicit BCMapCellInfo(nsTableFrame* aTableFrame);
  void ResetCellInfo();
  void SetInfo(nsTableRowFrame* aNewRow, int32_t aColIndex,
               BCCellData* aCellData, BCMapCellIterator* aIter,
               nsCellMap* aCellMap = nullptr);

  // Functions to (re)set the border widths on the table related cell frames,
  // where the knowledge about the current position in the table is used.
  // For most "normal" cells that have row/colspan of 1, these functions
  // are called once at most during the reflow, setting the value as given
  // (Discarding the value from the previous reflow, which is now irrelevant).
  // However, for cells spanning multiple rows/coluns, the maximum border
  // width seen is stored. This is controlled by calling the reset functions
  // before the cell's border is computed the first time.
  void ResetIStartBorderWidths();
  void ResetIEndBorderWidths();
  void ResetBStartBorderWidths();
  void ResetBEndBorderWidths();

  void SetIStartBorderWidths(nscoord aWidth);
  void SetIEndBorderWidths(nscoord aWidth);
  void SetBStartBorderWidths(nscoord aWidth);
  void SetBEndBorderWidths(nscoord aWidth);

  // functions to compute the borders; they depend on the
  // knowledge about the current position in the table. The edge functions
  // should be called if a table edge is involved, otherwise the internal
  // functions should be called.
  BCCellBorder GetBStartEdgeBorder();
  BCCellBorder GetBEndEdgeBorder();
  BCCellBorder GetIStartEdgeBorder();
  BCCellBorder GetIEndEdgeBorder();
  BCCellBorder GetIEndInternalBorder();
  BCCellBorder GetIStartInternalBorder();
  BCCellBorder GetBStartInternalBorder();
  BCCellBorder GetBEndInternalBorder();

  // functions to set the internal position information
  void SetColumn(int32_t aColX);
  // Increment the row as we loop over the rows of a rowspan
  void IncrementRow(bool aResetToBStartRowOfCell = false);

  // Helper functions to get extent of the cell
  int32_t GetCellEndRowIndex() const;
  int32_t GetCellEndColIndex() const;

  // Storage of table information required to compute individual cell
  // information.
  nsTableFrame* mTableFrame;
  nsTableFrame* mTableFirstInFlow;
  int32_t mNumTableRows;
  int32_t mNumTableCols;
  WritingMode mTableWM;

  // a cell can only belong to one rowgroup
  nsTableRowGroupFrame* mRowGroup;

  // a cell with a rowspan has a bstart and a bend row, and rows in between
  nsTableRowFrame* mStartRow;
  nsTableRowFrame* mEndRow;
  nsTableRowFrame* mCurrentRowFrame;

  // a cell with a colspan has an istart and iend column and columns in between
  // they can belong to different colgroups
  nsTableColGroupFrame* mColGroup;
  nsTableColGroupFrame* mCurrentColGroupFrame;

  nsTableColFrame* mStartCol;
  nsTableColFrame* mEndCol;
  nsTableColFrame* mCurrentColFrame;

  // cell information
  BCCellData* mCellData;
  nsBCTableCellFrame* mCell;

  int32_t mRowIndex;
  int32_t mRowSpan;
  int32_t mColIndex;
  int32_t mColSpan;

  // flags to describe the position of the cell with respect to the row- and
  // colgroups, for instance mRgAtStart documents that the bStart cell border
  // hits a rowgroup border
  bool mRgAtStart;
  bool mRgAtEnd;
  bool mCgAtStart;
  bool mCgAtEnd;
};

BCMapCellInfo::BCMapCellInfo(nsTableFrame* aTableFrame)
    : mTableFrame(aTableFrame),
      mTableFirstInFlow(static_cast<nsTableFrame*>(aTableFrame->FirstInFlow())),
      mNumTableRows(aTableFrame->GetRowCount()),
      mNumTableCols(aTableFrame->GetColCount()),
      mTableWM(aTableFrame->Style()),
      mCurrentRowFrame(nullptr),
      mCurrentColGroupFrame(nullptr),
      mCurrentColFrame(nullptr) {
  ResetCellInfo();
}

void BCMapCellInfo::ResetCellInfo() {
  mCellData = nullptr;
  mRowGroup = nullptr;
  mStartRow = nullptr;
  mEndRow = nullptr;
  mColGroup = nullptr;
  mStartCol = nullptr;
  mEndCol = nullptr;
  mCell = nullptr;
  mRowIndex = mRowSpan = mColIndex = mColSpan = 0;
  mRgAtStart = mRgAtEnd = mCgAtStart = mCgAtEnd = false;
}

inline int32_t BCMapCellInfo::GetCellEndRowIndex() const {
  return mRowIndex + mRowSpan - 1;
}

inline int32_t BCMapCellInfo::GetCellEndColIndex() const {
  return mColIndex + mColSpan - 1;
}

static TableBCData* GetTableBCData(nsTableFrame* aTableFrame) {
  auto* firstInFlow = static_cast<nsTableFrame*>(aTableFrame->FirstInFlow());
  return firstInFlow->GetTableBCData();
}

/*****************************************************************
 *  BCMapTableInfo
 * This structure stores controls border information global to the
 * table computed during the border-collapsed border calcuation.
 ****************************************************************/
struct BCMapTableInfo final {
  explicit BCMapTableInfo(nsTableFrame* aTableFrame)
      : mTableBCData{GetTableBCData(aTableFrame)} {}

  void ResetTableIStartBorderWidth() { mTableBCData->mIStartBorderWidth = 0; }

  void ResetTableIEndBorderWidth() { mTableBCData->mIEndBorderWidth = 0; }

  void ResetTableBStartBorderWidth() { mTableBCData->mBStartBorderWidth = 0; }

  void ResetTableBEndBorderWidth() { mTableBCData->mBEndBorderWidth = 0; }

  void SetTableIStartBorderWidth(nscoord aWidth);
  void SetTableIEndBorderWidth(nscoord aWidth);
  void SetTableBStartBorderWidth(nscoord aWidth);
  void SetTableBEndBorderWidth(nscoord aWidth);

  TableBCData* mTableBCData;
};

class BCMapCellIterator {
 public:
  BCMapCellIterator(nsTableFrame* aTableFrame, const TableArea& aDamageArea);

  void First(BCMapCellInfo& aMapInfo);

  void Next(BCMapCellInfo& aMapInfo);

  void PeekIEnd(const BCMapCellInfo& aRefInfo, int32_t aRowIndex,
                BCMapCellInfo& aAjaInfo);

  void PeekBEnd(const BCMapCellInfo& aRefInfo, int32_t aColIndex,
                BCMapCellInfo& aAjaInfo);

  void PeekIStart(const BCMapCellInfo& aRefInfo, int32_t aRowIndex,
                  BCMapCellInfo& aAjaInfo);

  bool IsNewRow() { return mIsNewRow; }

  nsTableRowFrame* GetPrevRow() const { return mPrevRow; }
  nsTableRowFrame* GetCurrentRow() const { return mRow; }
  nsTableRowGroupFrame* GetCurrentRowGroup() const { return mRowGroup; }

  int32_t mRowGroupStart;
  int32_t mRowGroupEnd;
  bool mAtEnd;
  nsCellMap* mCellMap;

 private:
  bool SetNewRow(nsTableRowFrame* row = nullptr);
  bool SetNewRowGroup(bool aFindFirstDamagedRow);
  void PeekIAt(const BCMapCellInfo& aRefInfo, int32_t aRowIndex,
               int32_t aColIndex, BCMapCellInfo& aAjaInfo);

  nsTableFrame* mTableFrame;
  nsTableCellMap* mTableCellMap;
  nsTableFrame::RowGroupArray mRowGroups;
  nsTableRowGroupFrame* mRowGroup;
  int32_t mRowGroupIndex;
  uint32_t mNumTableRows;
  nsTableRowFrame* mRow;
  nsTableRowFrame* mPrevRow;
  bool mIsNewRow;
  int32_t mRowIndex;
  uint32_t mNumTableCols;
  int32_t mColIndex;
  // We don't necessarily want to traverse all areas
  // of the table - mArea(Start|End) specify the area to traverse.
  // TODO(dshin): Should be not abuse `nsPoint` for this - See bug 1879847.
  nsPoint mAreaStart;
  nsPoint mAreaEnd;
};

BCMapCellIterator::BCMapCellIterator(nsTableFrame* aTableFrame,
                                     const TableArea& aDamageArea)
    : mRowGroupStart(0),
      mRowGroupEnd(0),
      mCellMap(nullptr),
      mTableFrame(aTableFrame),
      mRowGroups(aTableFrame->OrderedRowGroups()),
      mRowGroup(nullptr),
      mPrevRow(nullptr),
      mIsNewRow(false) {
  mTableCellMap = aTableFrame->GetCellMap();

  mAreaStart.x = aDamageArea.StartCol();
  mAreaStart.y = aDamageArea.StartRow();
  mAreaEnd.x = aDamageArea.EndCol() - 1;
  mAreaEnd.y = aDamageArea.EndRow() - 1;

  mNumTableRows = mTableFrame->GetRowCount();
  mRow = nullptr;
  mRowIndex = 0;
  mNumTableCols = mTableFrame->GetColCount();
  mColIndex = 0;
  mRowGroupIndex = -1;

  mAtEnd = true;  // gets reset when First() is called
}

// fill fields that we need for border collapse computation on a given cell
void BCMapCellInfo::SetInfo(nsTableRowFrame* aNewRow, int32_t aColIndex,
                            BCCellData* aCellData, BCMapCellIterator* aIter,
                            nsCellMap* aCellMap) {
  // fill the cell information
  mCellData = aCellData;
  mColIndex = aColIndex;

  // initialize the row information if it was not previously set for cells in
  // this row
  mRowIndex = 0;
  if (aNewRow) {
    mStartRow = aNewRow;
    mRowIndex = aNewRow->GetRowIndex();
  }

  // fill cell frame info and row information
  mCell = nullptr;
  mRowSpan = 1;
  mColSpan = 1;
  if (aCellData) {
    mCell = static_cast<nsBCTableCellFrame*>(aCellData->GetCellFrame());
    if (mCell) {
      if (!mStartRow) {
        mStartRow = mCell->GetTableRowFrame();
        if (!mStartRow) ABORT0();
        mRowIndex = mStartRow->GetRowIndex();
      }
      mColSpan = mTableFrame->GetEffectiveColSpan(*mCell, aCellMap);
      mRowSpan = mTableFrame->GetEffectiveRowSpan(*mCell, aCellMap);
    }
  }

  if (!mStartRow) {
    mStartRow = aIter->GetCurrentRow();
  }
  if (1 == mRowSpan) {
    mEndRow = mStartRow;
  } else {
    mEndRow = mStartRow->GetNextRow();
    if (mEndRow) {
      for (int32_t span = 2; mEndRow && span < mRowSpan; span++) {
        mEndRow = mEndRow->GetNextRow();
      }
      NS_ASSERTION(mEndRow, "spanned row not found");
    } else {
      NS_ERROR("error in cell map");
      mRowSpan = 1;
      mEndRow = mStartRow;
    }
  }
  // row group frame info
  // try to reuse the rgStart and rgEnd from the iterator as calls to
  // GetRowCount() are computationally expensive and should be avoided if
  // possible
  uint32_t rgStart = aIter->mRowGroupStart;
  uint32_t rgEnd = aIter->mRowGroupEnd;
  mRowGroup = mStartRow->GetTableRowGroupFrame();
  if (mRowGroup != aIter->GetCurrentRowGroup()) {
    rgStart = mRowGroup->GetStartRowIndex();
    rgEnd = rgStart + mRowGroup->GetRowCount() - 1;
  }
  uint32_t rowIndex = mStartRow->GetRowIndex();
  mRgAtStart = rgStart == rowIndex;
  mRgAtEnd = rgEnd == rowIndex + mRowSpan - 1;

  // col frame info
  mStartCol = mTableFirstInFlow->GetColFrame(aColIndex);
  if (!mStartCol) ABORT0();

  mEndCol = mStartCol;
  if (mColSpan > 1) {
    nsTableColFrame* colFrame =
        mTableFirstInFlow->GetColFrame(aColIndex + mColSpan - 1);
    if (!colFrame) ABORT0();
    mEndCol = colFrame;
  }

  // col group frame info
  mColGroup = mStartCol->GetTableColGroupFrame();
  int32_t cgStart = mColGroup->GetStartColumnIndex();
  int32_t cgEnd = std::max(0, cgStart + mColGroup->GetColCount() - 1);
  mCgAtStart = cgStart == aColIndex;
  mCgAtEnd = cgEnd == aColIndex + mColSpan - 1;
}

bool BCMapCellIterator::SetNewRow(nsTableRowFrame* aRow) {
  mAtEnd = true;
  mPrevRow = mRow;
  if (aRow) {
    mRow = aRow;
  } else if (mRow) {
    mRow = mRow->GetNextRow();
  }
  if (mRow) {
    mRowIndex = mRow->GetRowIndex();
    // get to the first entry with an originating cell
    int32_t rgRowIndex = mRowIndex - mRowGroupStart;
    if (uint32_t(rgRowIndex) >= mCellMap->mRows.Length()) ABORT1(false);
    const nsCellMap::CellDataArray& row = mCellMap->mRows[rgRowIndex];

    for (mColIndex = mAreaStart.x; mColIndex <= mAreaEnd.x; mColIndex++) {
      CellData* cellData = row.SafeElementAt(mColIndex);
      if (!cellData) {  // add a dead cell data
        TableArea damageArea;
        cellData = mCellMap->AppendCell(*mTableCellMap, nullptr, rgRowIndex,
                                        false, 0, damageArea);
        if (!cellData) ABORT1(false);
      }
      if (cellData && (cellData->IsOrig() || cellData->IsDead())) {
        break;
      }
    }
    mIsNewRow = true;
    mAtEnd = false;
  } else
    ABORT1(false);

  return !mAtEnd;
}

bool BCMapCellIterator::SetNewRowGroup(bool aFindFirstDamagedRow) {
  mAtEnd = true;
  int32_t numRowGroups = mRowGroups.Length();
  mCellMap = nullptr;
  for (mRowGroupIndex++; mRowGroupIndex < numRowGroups; mRowGroupIndex++) {
    mRowGroup = mRowGroups[mRowGroupIndex];
    int32_t rowCount = mRowGroup->GetRowCount();
    mRowGroupStart = mRowGroup->GetStartRowIndex();
    mRowGroupEnd = mRowGroupStart + rowCount - 1;
    if (rowCount > 0) {
      mCellMap = mTableCellMap->GetMapFor(mRowGroup, mCellMap);
      if (!mCellMap) ABORT1(false);
      nsTableRowFrame* firstRow = mRowGroup->GetFirstRow();
      if (aFindFirstDamagedRow) {
        if ((mAreaStart.y >= mRowGroupStart) &&
            (mAreaStart.y <= mRowGroupEnd)) {
          // the damage area starts in the row group

          // find the correct first damaged row
          int32_t numRows = mAreaStart.y - mRowGroupStart;
          for (int32_t i = 0; i < numRows; i++) {
            firstRow = firstRow->GetNextRow();
            if (!firstRow) ABORT1(false);
          }

        } else {
          continue;
        }
      }
      if (SetNewRow(firstRow)) {  // sets mAtEnd
        break;
      }
    }
  }

  return !mAtEnd;
}

void BCMapCellIterator::First(BCMapCellInfo& aMapInfo) {
  aMapInfo.ResetCellInfo();

  SetNewRowGroup(true);  // sets mAtEnd
  while (!mAtEnd) {
    if ((mAreaStart.y >= mRowGroupStart) && (mAreaStart.y <= mRowGroupEnd)) {
      BCCellData* cellData = static_cast<BCCellData*>(
          mCellMap->GetDataAt(mAreaStart.y - mRowGroupStart, mAreaStart.x));
      if (cellData && (cellData->IsOrig() || cellData->IsDead())) {
        aMapInfo.SetInfo(mRow, mAreaStart.x, cellData, this);
        return;
      } else {
        NS_ASSERTION(((0 == mAreaStart.x) && (mRowGroupStart == mAreaStart.y)),
                     "damage area expanded incorrectly");
      }
    }
    SetNewRowGroup(true);  // sets mAtEnd
  }
}

void BCMapCellIterator::Next(BCMapCellInfo& aMapInfo) {
  if (mAtEnd) ABORT0();
  aMapInfo.ResetCellInfo();

  mIsNewRow = false;
  mColIndex++;
  while ((mRowIndex <= mAreaEnd.y) && !mAtEnd) {
    for (; mColIndex <= mAreaEnd.x; mColIndex++) {
      int32_t rgRowIndex = mRowIndex - mRowGroupStart;
      BCCellData* cellData =
          static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, mColIndex));
      if (!cellData) {  // add a dead cell data
        TableArea damageArea;
        cellData = static_cast<BCCellData*>(mCellMap->AppendCell(
            *mTableCellMap, nullptr, rgRowIndex, false, 0, damageArea));
        if (!cellData) ABORT0();
      }
      if (cellData && (cellData->IsOrig() || cellData->IsDead())) {
        aMapInfo.SetInfo(mRow, mColIndex, cellData, this);
        return;
      }
    }
    if (mRowIndex >= mRowGroupEnd) {
      SetNewRowGroup(false);  // could set mAtEnd
    } else {
      SetNewRow();  // could set mAtEnd
    }
  }
  mAtEnd = true;
}

void BCMapCellIterator::PeekIEnd(const BCMapCellInfo& aRefInfo,
                                 int32_t aRowIndex, BCMapCellInfo& aAjaInfo) {
  PeekIAt(aRefInfo, aRowIndex, aRefInfo.mColIndex + aRefInfo.mColSpan,
          aAjaInfo);
}

void BCMapCellIterator::PeekBEnd(const BCMapCellInfo& aRefInfo,
                                 int32_t aColIndex, BCMapCellInfo& aAjaInfo) {
  aAjaInfo.ResetCellInfo();
  int32_t rowIndex = aRefInfo.mRowIndex + aRefInfo.mRowSpan;
  int32_t rgRowIndex = rowIndex - mRowGroupStart;
  nsTableRowGroupFrame* rg = mRowGroup;
  nsCellMap* cellMap = mCellMap;
  nsTableRowFrame* nextRow = nullptr;
  if (rowIndex > mRowGroupEnd) {
    int32_t nextRgIndex = mRowGroupIndex;
    do {
      nextRgIndex++;
      rg = mRowGroups.SafeElementAt(nextRgIndex);
      if (rg) {
        cellMap = mTableCellMap->GetMapFor(rg, cellMap);
        if (!cellMap) ABORT0();
        // First row of the next row group
        rgRowIndex = 0;
        nextRow = rg->GetFirstRow();
      }
    } while (rg && !nextRow);
    if (!rg) {
      return;
    }
  } else {
    // get the row within the same row group
    nextRow = mRow;
    for (int32_t i = 0; i < aRefInfo.mRowSpan; i++) {
      nextRow = nextRow->GetNextRow();
      if (!nextRow) ABORT0();
    }
  }

  BCCellData* cellData =
      static_cast<BCCellData*>(cellMap->GetDataAt(rgRowIndex, aColIndex));
  if (!cellData) {  // add a dead cell data
    NS_ASSERTION(rgRowIndex < cellMap->GetRowCount(), "program error");
    TableArea damageArea;
    cellData = static_cast<BCCellData*>(cellMap->AppendCell(
        *mTableCellMap, nullptr, rgRowIndex, false, 0, damageArea));
    if (!cellData) ABORT0();
  }
  if (cellData->IsColSpan()) {
    aColIndex -= static_cast<int32_t>(cellData->GetColSpanOffset());
    cellData =
        static_cast<BCCellData*>(cellMap->GetDataAt(rgRowIndex, aColIndex));
  }
  aAjaInfo.SetInfo(nextRow, aColIndex, cellData, this, cellMap);
}

void BCMapCellIterator::PeekIStart(const BCMapCellInfo& aRefInfo,
                                   int32_t aRowIndex, BCMapCellInfo& aAjaInfo) {
  NS_ASSERTION(aRefInfo.mColIndex != 0, "program error");
  PeekIAt(aRefInfo, aRowIndex, aRefInfo.mColIndex - 1, aAjaInfo);
}

void BCMapCellIterator::PeekIAt(const BCMapCellInfo& aRefInfo,
                                int32_t aRowIndex, int32_t aColIndex,
                                BCMapCellInfo& aAjaInfo) {
  aAjaInfo.ResetCellInfo();
  int32_t rgRowIndex = aRowIndex - mRowGroupStart;

  auto* cellData =
      static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, aColIndex));
  if (!cellData) {  // add a dead cell data
    NS_ASSERTION(aColIndex < mTableCellMap->GetColCount(), "program error");
    TableArea damageArea;
    cellData = static_cast<BCCellData*>(mCellMap->AppendCell(
        *mTableCellMap, nullptr, rgRowIndex, false, 0, damageArea));
    if (!cellData) ABORT0();
  }
  nsTableRowFrame* row = nullptr;
  if (cellData->IsRowSpan()) {
    rgRowIndex -= static_cast<int32_t>(cellData->GetRowSpanOffset());
    cellData =
        static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, aColIndex));
    if (!cellData) ABORT0();
  } else {
    row = mRow;
  }
  aAjaInfo.SetInfo(row, aColIndex, cellData, this);
}

#define CELL_CORNER true

/** return the border style, border color and optionally the width for a given
 * frame and side
 * @param aFrame           - query the info for this frame
 * @param aTableWM         - the writing-mode of the frame
 * @param aSide            - the side of the frame
 * @param aStyle           - the border style
 * @param aColor           - the border color
 * @param aWidth           - the border width
 */
static void GetColorAndStyle(const nsIFrame* aFrame, WritingMode aTableWM,
                             LogicalSide aSide, StyleBorderStyle* aStyle,
                             nscolor* aColor, nscoord* aWidth = nullptr) {
  MOZ_ASSERT(aFrame, "null frame");
  MOZ_ASSERT(aStyle && aColor, "null argument");

  // initialize out arg
  *aColor = 0;
  if (aWidth) {
    *aWidth = 0;
  }

  const nsStyleBorder* styleData = aFrame->StyleBorder();
  mozilla::Side physicalSide = aTableWM.PhysicalSide(aSide);
  *aStyle = styleData->GetBorderStyle(physicalSide);

  if ((StyleBorderStyle::None == *aStyle) ||
      (StyleBorderStyle::Hidden == *aStyle)) {
    return;
  }
  *aColor = aFrame->Style()->GetVisitedDependentColor(
      nsStyleBorder::BorderColorFieldFor(physicalSide));

  if (aWidth) {
    *aWidth = styleData->GetComputedBorderWidth(physicalSide);
  }
}

/** coerce the paint style as required by CSS2.1
 * @param aFrame           - query the info for this frame
 * @param aTableWM         - the writing mode of the frame
 * @param aSide            - the side of the frame
 * @param aStyle           - the border style
 * @param aColor           - the border color
 */
static void GetPaintStyleInfo(const nsIFrame* aFrame, WritingMode aTableWM,
                              LogicalSide aSide, StyleBorderStyle* aStyle,
                              nscolor* aColor) {
  GetColorAndStyle(aFrame, aTableWM, aSide, aStyle, aColor);
  if (StyleBorderStyle::Inset == *aStyle) {
    *aStyle = StyleBorderStyle::Ridge;
  } else if (StyleBorderStyle::Outset == *aStyle) {
    *aStyle = StyleBorderStyle::Groove;
  }
}

class nsDelayedCalcBCBorders : public Runnable {
 public:
  explicit nsDelayedCalcBCBorders(nsIFrame* aFrame)
      : mozilla::Runnable("nsDelayedCalcBCBorders"), mFrame(aFrame) {}

  NS_IMETHOD Run() override {
    if (mFrame) {
      auto* tableFrame = static_cast<nsTableFrame*>(mFrame.GetFrame());
      if (tableFrame->NeedToCalcBCBorders()) {
        tableFrame->CalcBCBorders();
      }
    }
    return NS_OK;
  }

 private:
  WeakFrame mFrame;
};

bool nsTableFrame::BCRecalcNeeded(ComputedStyle* aOldComputedStyle,
                                  ComputedStyle* aNewComputedStyle) {
  const nsStyleBorder* oldStyleData = aOldComputedStyle->StyleBorder();
  const nsStyleBorder* newStyleData = aNewComputedStyle->StyleBorder();
  nsChangeHint change = newStyleData->CalcDifference(*oldStyleData);
  if (!change) {
    return false;
  }
  if (change & nsChangeHint_NeedReflow) {
    return true;  // the caller only needs to mark the bc damage area
  }
  if (change & nsChangeHint_RepaintFrame) {
    // we need to recompute the borders and the caller needs to mark
    // the bc damage area
    // XXX In principle this should only be necessary for border style changes
    // However the bc painting code tries to maximize the drawn border segments
    // so it stores in the cellmap where a new border segment starts and this
    // introduces a unwanted cellmap data dependence on color
    nsCOMPtr<nsIRunnable> evt = MakeAndAddRef<nsDelayedCalcBCBorders>(this);
    nsresult rv = GetContent()->OwnerDoc()->Dispatch(evt.forget());
    return NS_SUCCEEDED(rv);
  }
  return false;
}

// Compare two border segments, this comparison depends whether the two
// segments meet at a corner and whether the second segment is inline-dir.
// The return value is whichever of aBorder1 or aBorder2 dominates.
static const BCCellBorder& CompareBorders(
    bool aIsCorner,  // Pass true for corner calculations
    const BCCellBorder& aBorder1, const BCCellBorder& aBorder2,
    bool aSecondIsInlineDir, bool* aFirstDominates = nullptr) {
  bool firstDominates = true;

  if (StyleBorderStyle::Hidden == aBorder1.style) {
    firstDominates = !aIsCorner;
  } else if (StyleBorderStyle::Hidden == aBorder2.style) {
    firstDominates = aIsCorner;
  } else if (aBorder1.width < aBorder2.width) {
    firstDominates = false;
  } else if (aBorder1.width == aBorder2.width) {
    if (static_cast<uint8_t>(aBorder1.style) <
        static_cast<uint8_t>(aBorder2.style)) {
      firstDominates = false;
    } else if (aBorder1.style == aBorder2.style) {
      if (aBorder1.owner == aBorder2.owner) {
        firstDominates = !aSecondIsInlineDir;
      } else if (aBorder1.owner < aBorder2.owner) {
        firstDominates = false;
      }
    }
  }

  if (aFirstDominates) {
    *aFirstDominates = firstDominates;
  }

  if (firstDominates) {
    return aBorder1;
  }
  return aBorder2;
}

/** calc the dominant border by considering the table, row/col group, row/col,
 * cell.
 * Depending on whether the side is block-dir or inline-dir and whether
 * adjacent frames are taken into account the ownership of a single border
 * segment is defined. The return value is the dominating border
 * The cellmap stores only bstart and istart borders for each cellmap position.
 * If the cell border is owned by the cell that is istart-wards of the border
 * it will be an adjacent owner aka eAjaCellOwner. See celldata.h for the other
 * scenarios with a adjacent owner.
 * @param xxxFrame         - the frame for style information, might be zero if
 *                           it should not be considered
 * @param aTableWM         - the writing mode of the frame
 * @param aSide            - side of the frames that should be considered
 * @param aAja             - the border comparison takes place from the point of
 *                           a frame that is adjacent to the cellmap entry, for
 *                           when a cell owns its lower border it will be the
 *                           adjacent owner as in the cellmap only bstart and
 *                           istart borders are stored.
 */
static BCCellBorder CompareBorders(
    const nsIFrame* aTableFrame, const nsIFrame* aColGroupFrame,
    const nsIFrame* aColFrame, const nsIFrame* aRowGroupFrame,
    const nsIFrame* aRowFrame, const nsIFrame* aCellFrame, WritingMode aTableWM,
    LogicalSide aSide, bool aAja) {
  BCCellBorder border, tempBorder;
  bool inlineAxis = IsBlock(aSide);

  // start with the table as dominant if present
  if (aTableFrame) {
    GetColorAndStyle(aTableFrame, aTableWM, aSide, &border.style, &border.color,
                     &border.width);
    border.owner = eTableOwner;
    if (StyleBorderStyle::Hidden == border.style) {
      return border;
    }
  }
  // see if the colgroup is dominant
  if (aColGroupFrame) {
    GetColorAndStyle(aColGroupFrame, aTableWM, aSide, &tempBorder.style,
                     &tempBorder.color, &tempBorder.width);
    tempBorder.owner = aAja && !inlineAxis ? eAjaColGroupOwner : eColGroupOwner;
    // pass here and below false for aSecondIsInlineDir as it is only used for
    // corner calculations.
    border = CompareBorders(!CELL_CORNER, border, tempBorder, false);
    if (StyleBorderStyle::Hidden == border.style) {
      return border;
    }
  }
  // see if the col is dominant
  if (aColFrame) {
    GetColorAndStyle(aColFrame, aTableWM, aSide, &tempBorder.style,
                     &tempBorder.color, &tempBorder.width);
    tempBorder.owner = aAja && !inlineAxis ? eAjaColOwner : eColOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, false);
    if (StyleBorderStyle::Hidden == border.style) {
      return border;
    }
  }
  // see if the rowgroup is dominant
  if (aRowGroupFrame) {
    GetColorAndStyle(aRowGroupFrame, aTableWM, aSide, &tempBorder.style,
                     &tempBorder.color, &tempBorder.width);
    tempBorder.owner = aAja && inlineAxis ? eAjaRowGroupOwner : eRowGroupOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, false);
    if (StyleBorderStyle::Hidden == border.style) {
      return border;
    }
  }
  // see if the row is dominant
  if (aRowFrame) {
    GetColorAndStyle(aRowFrame, aTableWM, aSide, &tempBorder.style,
                     &tempBorder.color, &tempBorder.width);
    tempBorder.owner = aAja && inlineAxis ? eAjaRowOwner : eRowOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, false);
    if (StyleBorderStyle::Hidden == border.style) {
      return border;
    }
  }
  // see if the cell is dominant
  if (aCellFrame) {
    GetColorAndStyle(aCellFrame, aTableWM, aSide, &tempBorder.style,
                     &tempBorder.color, &tempBorder.width);
    tempBorder.owner = aAja ? eAjaCellOwner : eCellOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, false);
  }
  return border;
}

static bool Perpendicular(mozilla::LogicalSide aSide1,
                          mozilla::LogicalSide aSide2) {
  return IsInline(aSide1) != IsInline(aSide2);
}

// Initial value indicating that BCCornerInfo's ownerStyle hasn't been set yet.
#define BORDER_STYLE_UNSET static_cast<StyleBorderStyle>(255)

struct BCCornerInfo {
  BCCornerInfo() {
    ownerColor = 0;
    ownerWidth = subWidth = ownerElem = subSide = subElem = hasDashDot =
        numSegs = bevel = 0;
    ownerSide = static_cast<uint16_t>(LogicalSide::BStart);
    ownerStyle = BORDER_STYLE_UNSET;
    subStyle = StyleBorderStyle::Solid;
  }

  void Set(mozilla::LogicalSide aSide, BCCellBorder border);

  void Update(mozilla::LogicalSide aSide, BCCellBorder border);

  nscolor ownerColor;           // color of borderOwner
  uint16_t ownerWidth;          // width of borderOwner
  uint16_t subWidth;            // width of the largest border intersecting the
                                // border perpendicular to ownerSide
  StyleBorderStyle subStyle;    // border style of subElem
  StyleBorderStyle ownerStyle;  // border style of ownerElem
  uint16_t ownerSide : 2;  // LogicalSide (e.g LogicalSide::BStart, etc) of the
                           // border owning the corner relative to the corner
  uint16_t
      ownerElem : 4;  // elem type (e.g. eTable, eGroup, etc) owning the corner
  uint16_t subSide : 2;  // side of border with subWidth relative to the corner
  uint16_t subElem : 4;  // elem type (e.g. eTable, eGroup, etc) of sub owner
  uint16_t hasDashDot : 1;  // does a dashed, dotted segment enter the corner,
                            // they cannot be beveled
  uint16_t numSegs : 3;     // number of segments entering corner
  uint16_t bevel : 1;       // is the corner beveled (uses the above two fields
                            // together with subWidth)
  // 7 bits are unused
};

// Start a new border at this corner, going in the direction of a given side.
void BCCornerInfo::Set(mozilla::LogicalSide aSide, BCCellBorder aBorder) {
  // FIXME bug 1508921: We mask 4-bit BCBorderOwner enum to 3 bits to preserve
  // buggy behavior found by the frame_above_rules_all.html mochitest.
  ownerElem = aBorder.owner & 0x7;

  ownerStyle = aBorder.style;
  ownerWidth = aBorder.width;
  ownerColor = aBorder.color;
  ownerSide = static_cast<uint16_t>(aSide);
  hasDashDot = 0;
  numSegs = 0;
  if (aBorder.width > 0) {
    numSegs++;
    hasDashDot = (StyleBorderStyle::Dashed == aBorder.style) ||
                 (StyleBorderStyle::Dotted == aBorder.style);
  }
  bevel = 0;
  subWidth = 0;
  // the following will get set later
  subSide = static_cast<uint16_t>(IsInline(aSide) ? LogicalSide::BStart
                                                  : LogicalSide::IStart);
  subElem = eTableOwner;
  subStyle = StyleBorderStyle::Solid;
}

// Add a new border going in the direction of a given side, and update the
// dominant border.
void BCCornerInfo::Update(mozilla::LogicalSide aSide, BCCellBorder aBorder) {
  if (ownerStyle == BORDER_STYLE_UNSET) {
    Set(aSide, aBorder);
  } else {
    bool isInline = IsInline(aSide);  // relative to the corner
    BCCellBorder oldBorder, tempBorder;
    oldBorder.owner = (BCBorderOwner)ownerElem;
    oldBorder.style = ownerStyle;
    oldBorder.width = ownerWidth;
    oldBorder.color = ownerColor;

    LogicalSide oldSide = LogicalSide(ownerSide);

    bool existingWins = false;
    tempBorder = CompareBorders(CELL_CORNER, oldBorder, aBorder, isInline,
                                &existingWins);

    ownerElem = tempBorder.owner;
    ownerStyle = tempBorder.style;
    ownerWidth = tempBorder.width;
    ownerColor = tempBorder.color;
    if (existingWins) {  // existing corner is dominant
      if (::Perpendicular(LogicalSide(ownerSide), aSide)) {
        // see if the new sub info replaces the old
        BCCellBorder subBorder;
        subBorder.owner = (BCBorderOwner)subElem;
        subBorder.style = subStyle;
        subBorder.width = subWidth;
        subBorder.color = 0;  // we are not interested in subBorder color
        bool firstWins;

        tempBorder = CompareBorders(CELL_CORNER, subBorder, aBorder, isInline,
                                    &firstWins);

        subElem = tempBorder.owner;
        subStyle = tempBorder.style;
        subWidth = tempBorder.width;
        if (!firstWins) {
          subSide = static_cast<uint16_t>(aSide);
        }
      }
    } else {  // input args are dominant
      ownerSide = static_cast<uint16_t>(aSide);
      if (::Perpendicular(oldSide, LogicalSide(ownerSide))) {
        subElem = oldBorder.owner;
        subStyle = oldBorder.style;
        subWidth = oldBorder.width;
        subSide = static_cast<uint16_t>(oldSide);
      }
    }
    if (aBorder.width > 0) {
      numSegs++;
      if (!hasDashDot && ((StyleBorderStyle::Dashed == aBorder.style) ||
                          (StyleBorderStyle::Dotted == aBorder.style))) {
        hasDashDot = 1;
      }
    }

    // bevel the corner if only two perpendicular non dashed/dotted segments
    // enter the corner
    bevel = (2 == numSegs) && (subWidth > 1) && (0 == hasDashDot);
  }
}

struct BCCorners {
  BCCorners(int32_t aNumCorners, int32_t aStartIndex);

  BCCornerInfo& operator[](int32_t i) const {
    NS_ASSERTION((i >= startIndex) && (i <= endIndex), "program error");
    return corners[std::clamp(i, startIndex, endIndex) - startIndex];
  }

  int32_t startIndex;
  int32_t endIndex;
  UniquePtr<BCCornerInfo[]> corners;
};

BCCorners::BCCorners(int32_t aNumCorners, int32_t aStartIndex) {
  NS_ASSERTION((aNumCorners > 0) && (aStartIndex >= 0), "program error");
  startIndex = aStartIndex;
  endIndex = aStartIndex + aNumCorners - 1;
  corners = MakeUnique<BCCornerInfo[]>(aNumCorners);
}

struct BCCellBorders {
  BCCellBorders(int32_t aNumBorders, int32_t aStartIndex);

  BCCellBorder& operator[](int32_t i) const {
    NS_ASSERTION((i >= startIndex) && (i <= endIndex), "program error");
    return borders[std::clamp(i, startIndex, endIndex) - startIndex];
  }

  int32_t startIndex;
  int32_t endIndex;
  UniquePtr<BCCellBorder[]> borders;
};

BCCellBorders::BCCellBorders(int32_t aNumBorders, int32_t aStartIndex) {
  NS_ASSERTION((aNumBorders > 0) && (aStartIndex >= 0), "program error");
  startIndex = aStartIndex;
  endIndex = aStartIndex + aNumBorders - 1;
  borders = MakeUnique<BCCellBorder[]>(aNumBorders);
}

// this function sets the new border properties and returns true if the border
// segment will start a new segment and not be accumulated into the previous
// segment.
static bool SetBorder(const BCCellBorder& aNewBorder, BCCellBorder& aBorder) {
  bool changed = (aNewBorder.style != aBorder.style) ||
                 (aNewBorder.width != aBorder.width) ||
                 (aNewBorder.color != aBorder.color);
  aBorder.color = aNewBorder.color;
  aBorder.width = aNewBorder.width;
  aBorder.style = aNewBorder.style;
  aBorder.owner = aNewBorder.owner;

  return changed;
}

// this function will set the inline-dir border. It will return true if the
// existing segment will not be continued. Having a block-dir owner of a corner
// should also start a new segment.
static bool SetInlineDirBorder(const BCCellBorder& aNewBorder,
                               const BCCornerInfo& aCorner,
                               BCCellBorder& aBorder) {
  bool startSeg = ::SetBorder(aNewBorder, aBorder);
  if (!startSeg) {
    startSeg = !IsInline(LogicalSide(aCorner.ownerSide));
  }
  return startSeg;
}

// Make the damage area larger on the top and bottom by at least one row and on
// the left and right at least one column. This is done so that adjacent
// elements are part of the border calculations. The extra segments and borders
// outside the actual damage area will not be updated in the cell map, because
// they in turn would need info from adjacent segments outside the damage area
// to be accurate.
void nsTableFrame::ExpandBCDamageArea(TableArea& aArea) const {
  int32_t numRows = GetRowCount();
  int32_t numCols = GetColCount();

  int32_t firstColIdx = aArea.StartCol();
  int32_t lastColIdx = aArea.EndCol() - 1;
  int32_t startRowIdx = aArea.StartRow();
  int32_t endRowIdx = aArea.EndRow() - 1;

  // expand the damage area in each direction
  if (firstColIdx > 0) {
    firstColIdx--;
  }
  if (lastColIdx < (numCols - 1)) {
    lastColIdx++;
  }
  if (startRowIdx > 0) {
    startRowIdx--;
  }
  if (endRowIdx < (numRows - 1)) {
    endRowIdx++;
  }
  // Check the damage area so that there are no cells spanning in or out. If
  // there are any then make the damage area as big as the table, similarly to
  // the way the cell map decides whether to rebuild versus expand. This could
  // be optimized to expand to the smallest area that contains no spanners, but
  // it may not be worth the effort in general, and it would need to be done in
  // the cell map as well.
  bool haveSpanner = false;
  if ((firstColIdx > 0) || (lastColIdx < (numCols - 1)) || (startRowIdx > 0) ||
      (endRowIdx < (numRows - 1))) {
    nsTableCellMap* tableCellMap = GetCellMap();
    if (!tableCellMap) ABORT0();
    // Get the ordered row groups
    RowGroupArray rowGroups = OrderedRowGroups();

    // Scope outside loop to be used as hint.
    nsCellMap* cellMap = nullptr;
    for (uint32_t rgIdx = 0; rgIdx < rowGroups.Length(); rgIdx++) {
      nsTableRowGroupFrame* rgFrame = rowGroups[rgIdx];
      int32_t rgStartY = rgFrame->GetStartRowIndex();
      int32_t rgEndY = rgStartY + rgFrame->GetRowCount() - 1;
      if (endRowIdx < rgStartY) {
        break;
      }
      cellMap = tableCellMap->GetMapFor(rgFrame, cellMap);
      if (!cellMap) ABORT0();
      // check for spanners from above and below
      if ((startRowIdx > 0) && (startRowIdx >= rgStartY) &&
          (startRowIdx <= rgEndY)) {
        if (uint32_t(startRowIdx - rgStartY) >= cellMap->mRows.Length())
          ABORT0();
        const nsCellMap::CellDataArray& row =
            cellMap->mRows[startRowIdx - rgStartY];
        for (int32_t x = firstColIdx; x <= lastColIdx; x++) {
          CellData* cellData = row.SafeElementAt(x);
          if (cellData && (cellData->IsRowSpan())) {
            haveSpanner = true;
            break;
          }
        }
        if (endRowIdx < rgEndY) {
          if (uint32_t(endRowIdx + 1 - rgStartY) >= cellMap->mRows.Length())
            ABORT0();
          const nsCellMap::CellDataArray& row2 =
              cellMap->mRows[endRowIdx + 1 - rgStartY];
          for (int32_t x = firstColIdx; x <= lastColIdx; x++) {
            CellData* cellData = row2.SafeElementAt(x);
            if (cellData && (cellData->IsRowSpan())) {
              haveSpanner = true;
              break;
            }
          }
        }
      }
      // check for spanners on the left and right
      int32_t iterStartY;
      int32_t iterEndY;
      if ((startRowIdx >= rgStartY) && (startRowIdx <= rgEndY)) {
        // the damage area starts in the row group
        iterStartY = startRowIdx;
        iterEndY = std::min(endRowIdx, rgEndY);
      } else if ((endRowIdx >= rgStartY) && (endRowIdx <= rgEndY)) {
        // the damage area ends in the row group
        iterStartY = rgStartY;
        iterEndY = endRowIdx;
      } else if ((rgStartY >= startRowIdx) && (rgEndY <= endRowIdx)) {
        // the damage area contains the row group
        iterStartY = rgStartY;
        iterEndY = rgEndY;
      } else {
        // the damage area does not overlap the row group
        continue;
      }
      NS_ASSERTION(iterStartY >= 0 && iterEndY >= 0,
                   "table index values are expected to be nonnegative");
      for (int32_t y = iterStartY; y <= iterEndY; y++) {
        if (uint32_t(y - rgStartY) >= cellMap->mRows.Length()) ABORT0();
        const nsCellMap::CellDataArray& row = cellMap->mRows[y - rgStartY];
        CellData* cellData = row.SafeElementAt(firstColIdx);
        if (cellData && (cellData->IsColSpan())) {
          haveSpanner = true;
          break;
        }
        if (lastColIdx < (numCols - 1)) {
          cellData = row.SafeElementAt(lastColIdx + 1);
          if (cellData && (cellData->IsColSpan())) {
            haveSpanner = true;
            break;
          }
        }
      }
    }
  }

  // If the damage area includes the edge of the table, we have to expand
  // the damage area across that whole edge. This is because table-edge
  // borders take the maximum border width among all cells on that edge.
  // i.e. If the first row is damaged, then we consider all the cols to
  // be damaged, and vice versa.
  if (haveSpanner || startRowIdx == 0 || endRowIdx == numRows - 1) {
    aArea.StartCol() = 0;
    aArea.ColCount() = numCols;
  } else {
    aArea.StartCol() = firstColIdx;
    aArea.ColCount() = 1 + lastColIdx - firstColIdx;
  }

  if (haveSpanner || firstColIdx == 0 || lastColIdx == numCols - 1) {
    aArea.StartRow() = 0;
    aArea.RowCount() = numRows;
  } else {
    aArea.StartRow() = startRowIdx;
    aArea.RowCount() = 1 + endRowIdx - startRowIdx;
  }
}

#define ADJACENT true
#define INLINE_DIR true

void BCMapTableInfo::SetTableIStartBorderWidth(nscoord aWidth) {
  mTableBCData->mIStartBorderWidth =
      std::max(mTableBCData->mIStartBorderWidth, aWidth);
}

void BCMapTableInfo::SetTableIEndBorderWidth(nscoord aWidth) {
  mTableBCData->mIEndBorderWidth =
      std::max(mTableBCData->mIEndBorderWidth, aWidth);
}

void BCMapTableInfo::SetTableBStartBorderWidth(nscoord aWidth) {
  mTableBCData->mBStartBorderWidth =
      std::max(mTableBCData->mBStartBorderWidth, aWidth);
}

void BCMapTableInfo::SetTableBEndBorderWidth(nscoord aWidth) {
  mTableBCData->mBEndBorderWidth =
      std::max(mTableBCData->mBEndBorderWidth, aWidth);
}

void BCMapCellInfo::ResetIStartBorderWidths() {
  if (mCell) {
    mCell->SetBorderWidth(LogicalSide::IStart, 0);
  }
  if (mStartCol) {
    mStartCol->SetIStartBorderWidth(0);
  }
}

void BCMapCellInfo::ResetIEndBorderWidths() {
  if (mCell) {
    mCell->SetBorderWidth(LogicalSide::IEnd, 0);
  }
  if (mEndCol) {
    mEndCol->SetIEndBorderWidth(0);
  }
}

void BCMapCellInfo::ResetBStartBorderWidths() {
  if (mCell) {
    mCell->SetBorderWidth(LogicalSide::BStart, 0);
  }
  if (mStartRow) {
    mStartRow->SetBStartBCBorderWidth(0);
  }
}

void BCMapCellInfo::ResetBEndBorderWidths() {
  if (mCell) {
    mCell->SetBorderWidth(LogicalSide::BEnd, 0);
  }
  if (mEndRow) {
    mEndRow->SetBEndBCBorderWidth(0);
  }
}

void BCMapCellInfo::SetIStartBorderWidths(nscoord aWidth) {
  if (mCell) {
    mCell->SetBorderWidth(
        LogicalSide::IStart,
        std::max(aWidth, mCell->GetBorderWidth(LogicalSide::IStart)));
  }
  if (mStartCol) {
    nscoord half = BC_BORDER_END_HALF(aWidth);
    mStartCol->SetIStartBorderWidth(
        std::max(half, mStartCol->GetIStartBorderWidth()));
  }
}

void BCMapCellInfo::SetIEndBorderWidths(nscoord aWidth) {
  // update the borders of the cells and cols affected
  if (mCell) {
    mCell->SetBorderWidth(
        LogicalSide::IEnd,
        std::max(aWidth, mCell->GetBorderWidth(LogicalSide::IEnd)));
  }
  if (mEndCol) {
    nscoord half = BC_BORDER_START_HALF(aWidth);
    mEndCol->SetIEndBorderWidth(std::max(half, mEndCol->GetIEndBorderWidth()));
  }
}

void BCMapCellInfo::SetBStartBorderWidths(nscoord aWidth) {
  if (mCell) {
    mCell->SetBorderWidth(
        LogicalSide::BStart,
        std::max(aWidth, mCell->GetBorderWidth(LogicalSide::BStart)));
  }
  if (mStartRow) {
    nscoord half = BC_BORDER_END_HALF(aWidth);
    mStartRow->SetBStartBCBorderWidth(
        std::max(half, mStartRow->GetBStartBCBorderWidth()));
  }
}

void BCMapCellInfo::SetBEndBorderWidths(nscoord aWidth) {
  // update the borders of the affected cells and rows
  if (mCell) {
    mCell->SetBorderWidth(
        LogicalSide::BEnd,
        std::max(aWidth, mCell->GetBorderWidth(LogicalSide::BEnd)));
  }
  if (mEndRow) {
    nscoord half = BC_BORDER_START_HALF(aWidth);
    mEndRow->SetBEndBCBorderWidth(
        std::max(half, mEndRow->GetBEndBCBorderWidth()));
  }
}

void BCMapCellInfo::SetColumn(int32_t aColX) {
  mCurrentColFrame = mTableFirstInFlow->GetColFrame(aColX);
  mCurrentColGroupFrame =
      static_cast<nsTableColGroupFrame*>(mCurrentColFrame->GetParent());
  if (!mCurrentColGroupFrame) {
    NS_ERROR("null mCurrentColGroupFrame");
  }
}

void BCMapCellInfo::IncrementRow(bool aResetToBStartRowOfCell) {
  mCurrentRowFrame =
      aResetToBStartRowOfCell ? mStartRow : mCurrentRowFrame->GetNextRow();
}

BCCellBorder BCMapCellInfo::GetBStartEdgeBorder() {
  return CompareBorders(mTableFrame, mCurrentColGroupFrame, mCurrentColFrame,
                        mRowGroup, mStartRow, mCell, mTableWM,
                        LogicalSide::BStart, !ADJACENT);
}

BCCellBorder BCMapCellInfo::GetBEndEdgeBorder() {
  return CompareBorders(mTableFrame, mCurrentColGroupFrame, mCurrentColFrame,
                        mRowGroup, mEndRow, mCell, mTableWM, LogicalSide::BEnd,
                        ADJACENT);
}
BCCellBorder BCMapCellInfo::GetIStartEdgeBorder() {
  return CompareBorders(mTableFrame, mColGroup, mStartCol, mRowGroup,
                        mCurrentRowFrame, mCell, mTableWM, LogicalSide::IStart,
                        !ADJACENT);
}
BCCellBorder BCMapCellInfo::GetIEndEdgeBorder() {
  return CompareBorders(mTableFrame, mColGroup, mEndCol, mRowGroup,
                        mCurrentRowFrame, mCell, mTableWM, LogicalSide::IEnd,
                        ADJACENT);
}
BCCellBorder BCMapCellInfo::GetIEndInternalBorder() {
  const nsIFrame* cg = mCgAtEnd ? mColGroup : nullptr;
  return CompareBorders(nullptr, cg, mEndCol, nullptr, nullptr, mCell, mTableWM,
                        LogicalSide::IEnd, ADJACENT);
}

BCCellBorder BCMapCellInfo::GetIStartInternalBorder() {
  const nsIFrame* cg = mCgAtStart ? mColGroup : nullptr;
  return CompareBorders(nullptr, cg, mStartCol, nullptr, nullptr, mCell,
                        mTableWM, LogicalSide::IStart, !ADJACENT);
}

BCCellBorder BCMapCellInfo::GetBEndInternalBorder() {
  const nsIFrame* rg = mRgAtEnd ? mRowGroup : nullptr;
  return CompareBorders(nullptr, nullptr, nullptr, rg, mEndRow, mCell, mTableWM,
                        LogicalSide::BEnd, ADJACENT);
}

BCCellBorder BCMapCellInfo::GetBStartInternalBorder() {
  const nsIFrame* rg = mRgAtStart ? mRowGroup : nullptr;
  return CompareBorders(nullptr, nullptr, nullptr, rg, mStartRow, mCell,
                        mTableWM, LogicalSide::BStart, !ADJACENT);
}

//  Calculate border information for border-collapsed tables.
//  Because borders of table/row/cell, etc merge into one, we need to
//  determine which border dominates at each cell. In addition, corner-specific
//  information, e.g. bevelling, is computed as well.
//
//  Here is the order for storing border edges in the cell map as a cell is
//  processed.
//
//  For each cell, at least 4 edges are processed:
//  * There are colspan * N block-start and block-end edges.
//  * There are rowspan * N inline-start and inline-end edges.
//
//  1) If the cell being processed is at the block-start of the table, store the
//     block-start edge.
//  2) If the cell being processed is at the inline-start of the table, store
//  the
//     inline-start edge.
//  3) Store the inline-end edge.
//  4) Store the block-end edge.
//
//  These steps are traced by calls to `SetBCBorderEdge`.
//
//  Corners are indexed by columns only, to avoid allocating a full row * col
//  array of `BCCornerInfo`. This trades off memory allocation versus moving
//  previous corner information around.
//
//  For each cell:
//  1) If the cell is at the block-start of the table, but not at the
//  inline-start of the table, store its block-start inline-start corner.
//
//  2) If the cell is at the inline-start of the table, store the block-start
//  inline-start corner.
//
//  3) If the cell is at the block-start inline-end of the table, or not at the
//  block-start of the table, store the block-start inline-end corner.
//
//  4) If the cell is at the block-end inline-end of the table, store the
//  block-end inline-end corner.
//
//  5) If the cell is at the block-end of the table, store the block-end
//  inline-start.
//
//  Visually, it looks like this:
//
//  2--1--1--1--1--1--3
//  |  |  |  |  |  |  |
//  2--3--3--3--3--3--3
//  |  |  |  |  |  |  |
//  2--3--3--3--3--3--3
//  |  |  |  |  |  |  |
//  5--5--5--5--5--5--4
//
//  For rowspan/colspan cells, the latest border information is propagated
//  along its "corners".
//
//  These steps are traced by calls to `SetBCBorderCorner`.
void nsTableFrame::CalcBCBorders() {
  NS_ASSERTION(IsBorderCollapse(),
               "calling CalcBCBorders on separated-border table");
  nsTableCellMap* tableCellMap = GetCellMap();
  if (!tableCellMap) ABORT0();
  int32_t numRows = GetRowCount();
  int32_t numCols = GetColCount();
  if (!numRows || !numCols) {
    return;  // nothing to do
  }

  // Get the property holding the table damage area and border widths
  TableBCData* propData = GetTableBCData();
  if (!propData) ABORT0();

  TableArea damageArea(propData->mDamageArea);
  // See documentation for why we do this.
  ExpandBCDamageArea(damageArea);

  // We accumulate border widths as we process the cells, so we need
  // to reset it once in the beginning.
  bool tableBorderReset[4] = {false};

  // Storage for block-direction borders from the previous row, indexed by
  // columns.
  BCCellBorders lastBlockDirBorders(damageArea.ColCount() + 1,
                                    damageArea.StartCol());
  if (!lastBlockDirBorders.borders) ABORT0();
  if (damageArea.StartRow() != 0) {
    // Ok, we've filled with information about the previous row's borders with
    // the default state, which is "no borders." This is incorrect, and leaving
    // it will result in an erroneous behaviour if the previous row did have
    // borders, and the dirty rows don't, as we will not mark the beginning of
    // the no border segment.
    TableArea prevRowArea(damageArea.StartCol(), damageArea.StartRow() - 1,
                          damageArea.ColCount(), 1);
    BCMapCellIterator iter(this, prevRowArea);
    BCMapCellInfo info(this);
    for (iter.First(info); !iter.mAtEnd; iter.Next(info)) {
      if (info.mColIndex == prevRowArea.StartCol()) {
        lastBlockDirBorders.borders[0] = info.GetIStartEdgeBorder();
      }
      lastBlockDirBorders.borders[info.mColIndex - prevRowArea.StartCol() + 1] =
          info.GetIEndEdgeBorder();
    }
  }
  // Inline direction border at block start of the table, computed by the
  // previous cell. Unused afterwards.
  Maybe<BCCellBorder> firstRowBStartEdgeBorder;
  BCCellBorder lastBEndBorder;
  // Storage for inline-direction borders from previous cells, indexed by
  // columns.
  // TODO(dshin): Why ColCount + 1? Number of inline segments should match
  // column count exactly, unlike block direction segments...
  BCCellBorders lastBEndBorders(damageArea.ColCount() + 1,
                                damageArea.StartCol());
  if (!lastBEndBorders.borders) ABORT0();

  BCMapCellInfo info(this);
  // TODO(dshin): This is basically propData, except it uses first-in-flow's
  // data. Consult the definition of `TableBCDataProperty` regarding
  // using the first-in-flow only.
  BCMapTableInfo tableInfo(this);

  // Block-start corners of the cell being traversed, indexed by columns.
  BCCorners bStartCorners(damageArea.ColCount() + 1, damageArea.StartCol());
  if (!bStartCorners.corners) ABORT0();
  // Block-end corners of the cell being traversed, indexed by columns.
  // Note that when a new row starts, they become block-start corners and used
  // as such, until cleared with `Set`.
  BCCorners bEndCorners(damageArea.ColCount() + 1, damageArea.StartCol());
  if (!bEndCorners.corners) ABORT0();

  BCMapCellIterator iter(this, damageArea);
  for (iter.First(info); !iter.mAtEnd; iter.Next(info)) {
    // see if firstRowBStartEdgeBorder, lastBEndBorder need to be reset
    if (iter.IsNewRow()) {
      if (info.mRowIndex == 0) {
        BCCellBorder border;
        if (info.mColIndex == 0) {
          border.Reset(info.mRowIndex, info.mRowSpan);
        } else {
          // Similar to lastBlockDirBorders, the previous block-start border
          // is filled by actually quering the adjacent cell.
          BCMapCellInfo ajaInfo(this);
          iter.PeekIStart(info, info.mRowIndex, ajaInfo);
          border = ajaInfo.GetBStartEdgeBorder();
        }
        firstRowBStartEdgeBorder = Some(border);
      } else {
        firstRowBStartEdgeBorder = Nothing{};
      }
      if (info.mColIndex == 0) {
        lastBEndBorder.Reset(info.GetCellEndRowIndex() + 1, info.mRowSpan);
      } else {
        // Same as above, but for block-end border.
        BCMapCellInfo ajaInfo(this);
        iter.PeekIStart(info, info.mRowIndex, ajaInfo);
        lastBEndBorder = ajaInfo.GetBEndEdgeBorder();
      }
    } else if (info.mColIndex > damageArea.StartCol()) {
      lastBEndBorder = lastBEndBorders[info.mColIndex - 1];
      if (lastBEndBorder.rowIndex > (info.GetCellEndRowIndex() + 1)) {
        // the bEnd border's iStart edge butts against the middle of a rowspan
        lastBEndBorder.Reset(info.GetCellEndRowIndex() + 1, info.mRowSpan);
      }
    }

    // find the dominant border considering the cell's bStart border and the
    // table, row group, row if the border is at the bStart of the table,
    // otherwise it was processed in a previous row
    if (0 == info.mRowIndex) {
      uint8_t idxBStart = static_cast<uint8_t>(LogicalSide::BStart);
      if (!tableBorderReset[idxBStart]) {
        tableInfo.ResetTableBStartBorderWidth();
        tableBorderReset[idxBStart] = true;
      }
      bool reset = false;
      for (int32_t colIdx = info.mColIndex; colIdx <= info.GetCellEndColIndex();
           colIdx++) {
        info.SetColumn(colIdx);
        BCCellBorder currentBorder = info.GetBStartEdgeBorder();
        BCCornerInfo& bStartIStartCorner = bStartCorners[colIdx];
        // Mark inline-end direction border from this corner.
        if (0 == colIdx) {
          bStartIStartCorner.Set(LogicalSide::IEnd, currentBorder);
        } else {
          bStartIStartCorner.Update(LogicalSide::IEnd, currentBorder);
          tableCellMap->SetBCBorderCorner(
              LogicalCorner::BStartIStart, *iter.mCellMap, 0, 0, colIdx,
              LogicalSide(bStartIStartCorner.ownerSide),
              bStartIStartCorner.subWidth, bStartIStartCorner.bevel);
        }
        // Above, we set the corner `colIndex` column as having a border towards
        // inline-end, heading towards the next column. Vice versa is also true,
        // where the next column has a border heading towards this column.
        bStartCorners[colIdx + 1].Set(LogicalSide::IStart, currentBorder);
        MOZ_ASSERT(firstRowBStartEdgeBorder,
                   "Inline start border tracking not set?");
        // update firstRowBStartEdgeBorder and see if a new segment starts
        bool startSeg =
            firstRowBStartEdgeBorder
                ? SetInlineDirBorder(currentBorder, bStartIStartCorner,
                                     firstRowBStartEdgeBorder.ref())
                : true;
        // store the border segment in the cell map
        tableCellMap->SetBCBorderEdge(LogicalSide::BStart, *iter.mCellMap, 0, 0,
                                      colIdx, 1, currentBorder.owner,
                                      currentBorder.width, startSeg);

        // Set border width at block-start (table-wide and for the cell), but
        // only if it's the largest we've encountered.
        tableInfo.SetTableBStartBorderWidth(currentBorder.width);
        if (!reset) {
          info.ResetBStartBorderWidths();
          reset = true;
        }
        info.SetBStartBorderWidths(currentBorder.width);
      }
    } else {
      // see if the bStart border needs to be the start of a segment due to a
      // block-dir border owning the corner
      if (info.mColIndex > 0) {
        BCData& data = info.mCellData->mData;
        if (!data.IsBStartStart()) {
          LogicalSide cornerSide;
          bool bevel;
          data.GetCorner(cornerSide, bevel);
          if (IsBlock(cornerSide)) {
            data.SetBStartStart(true);
          }
        }
      }
    }

    // find the dominant border considering the cell's iStart border and the
    // table, col group, col if the border is at the iStart of the table,
    // otherwise it was processed in a previous col
    if (0 == info.mColIndex) {
      uint8_t idxIStart = static_cast<uint8_t>(LogicalSide::IStart);
      if (!tableBorderReset[idxIStart]) {
        tableInfo.ResetTableIStartBorderWidth();
        tableBorderReset[idxIStart] = true;
      }
      info.mCurrentRowFrame = nullptr;
      bool reset = false;
      for (int32_t rowB = info.mRowIndex; rowB <= info.GetCellEndRowIndex();
           rowB++) {
        info.IncrementRow(rowB == info.mRowIndex);
        BCCellBorder currentBorder = info.GetIStartEdgeBorder();
        BCCornerInfo& bStartIStartCorner =
            (0 == rowB) ? bStartCorners[0] : bEndCorners[0];
        bStartIStartCorner.Update(LogicalSide::BEnd, currentBorder);
        tableCellMap->SetBCBorderCorner(
            LogicalCorner::BStartIStart, *iter.mCellMap, iter.mRowGroupStart,
            rowB, 0, LogicalSide(bStartIStartCorner.ownerSide),
            bStartIStartCorner.subWidth, bStartIStartCorner.bevel);
        bEndCorners[0].Set(LogicalSide::BStart, currentBorder);

        // update lastBlockDirBorders and see if a new segment starts
        bool startSeg = SetBorder(currentBorder, lastBlockDirBorders[0]);
        // store the border segment in the cell map
        tableCellMap->SetBCBorderEdge(LogicalSide::IStart, *iter.mCellMap,
                                      iter.mRowGroupStart, rowB, info.mColIndex,
                                      1, currentBorder.owner,
                                      currentBorder.width, startSeg);
        // Set border width at inline-start (table-wide and for the cell), but
        // only if it's the largest we've encountered.
        tableInfo.SetTableIStartBorderWidth(currentBorder.width);
        if (!reset) {
          info.ResetIStartBorderWidths();
          reset = true;
        }
        info.SetIStartBorderWidths(currentBorder.width);
      }
    }

    // find the dominant border considering the cell's iEnd border, adjacent
    // cells and the table, row group, row
    if (info.mNumTableCols == info.GetCellEndColIndex() + 1) {
      // touches iEnd edge of table
      uint8_t idxIEnd = static_cast<uint8_t>(LogicalSide::IEnd);
      if (!tableBorderReset[idxIEnd]) {
        tableInfo.ResetTableIEndBorderWidth();
        tableBorderReset[idxIEnd] = true;
      }
      info.mCurrentRowFrame = nullptr;
      bool reset = false;
      for (int32_t rowB = info.mRowIndex; rowB <= info.GetCellEndRowIndex();
           rowB++) {
        info.IncrementRow(rowB == info.mRowIndex);
        BCCellBorder currentBorder = info.GetIEndEdgeBorder();
        // Update/store the bStart-iEnd & bEnd-iEnd corners. Note that we
        // overwrite all corner information to the end of the column span.
        BCCornerInfo& bStartIEndCorner =
            (0 == rowB) ? bStartCorners[info.GetCellEndColIndex() + 1]
                        : bEndCorners[info.GetCellEndColIndex() + 1];
        bStartIEndCorner.Update(LogicalSide::BEnd, currentBorder);
        tableCellMap->SetBCBorderCorner(
            LogicalCorner::BStartIEnd, *iter.mCellMap, iter.mRowGroupStart,
            rowB, info.GetCellEndColIndex(),
            LogicalSide(bStartIEndCorner.ownerSide), bStartIEndCorner.subWidth,
            bStartIEndCorner.bevel);
        BCCornerInfo& bEndIEndCorner =
            bEndCorners[info.GetCellEndColIndex() + 1];
        bEndIEndCorner.Set(LogicalSide::BStart, currentBorder);
        tableCellMap->SetBCBorderCorner(
            LogicalCorner::BEndIEnd, *iter.mCellMap, iter.mRowGroupStart, rowB,
            info.GetCellEndColIndex(), LogicalSide(bEndIEndCorner.ownerSide),
            bEndIEndCorner.subWidth, bEndIEndCorner.bevel);
        // update lastBlockDirBorders and see if a new segment starts
        bool startSeg = SetBorder(
            currentBorder, lastBlockDirBorders[info.GetCellEndColIndex() + 1]);
        // store the border segment in the cell map and update cellBorders
        tableCellMap->SetBCBorderEdge(
            LogicalSide::IEnd, *iter.mCellMap, iter.mRowGroupStart, rowB,
            info.GetCellEndColIndex(), 1, currentBorder.owner,
            currentBorder.width, startSeg);
        // Set border width at inline-end (table-wide and for the cell), but
        // only if it's the largest we've encountered.
        tableInfo.SetTableIEndBorderWidth(currentBorder.width);
        if (!reset) {
          info.ResetIEndBorderWidths();
          reset = true;
        }
        info.SetIEndBorderWidths(currentBorder.width);
      }
    } else {
      // Cell entries, but not on the block-end side of the entire table.
      int32_t segLength = 0;
      BCMapCellInfo ajaInfo(this);
      BCMapCellInfo priorAjaInfo(this);
      bool reset = false;
      for (int32_t rowB = info.mRowIndex; rowB <= info.GetCellEndRowIndex();
           rowB += segLength) {
        // Grab the cell adjacent to our inline-end.
        iter.PeekIEnd(info, rowB, ajaInfo);
        BCCellBorder currentBorder = info.GetIEndInternalBorder();
        BCCellBorder adjacentBorder = ajaInfo.GetIStartInternalBorder();
        currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                       adjacentBorder, !INLINE_DIR);

        segLength = std::max(1, ajaInfo.mRowIndex + ajaInfo.mRowSpan - rowB);
        segLength = std::min(segLength, info.mRowIndex + info.mRowSpan - rowB);

        // update lastBlockDirBorders and see if a new segment starts
        bool startSeg = SetBorder(
            currentBorder, lastBlockDirBorders[info.GetCellEndColIndex() + 1]);
        // store the border segment in the cell map and update cellBorders
        if (info.GetCellEndColIndex() < damageArea.EndCol() &&
            rowB >= damageArea.StartRow() && rowB < damageArea.EndRow()) {
          tableCellMap->SetBCBorderEdge(
              LogicalSide::IEnd, *iter.mCellMap, iter.mRowGroupStart, rowB,
              info.GetCellEndColIndex(), segLength, currentBorder.owner,
              currentBorder.width, startSeg);
          if (!reset) {
            info.ResetIEndBorderWidths();
            ajaInfo.ResetIStartBorderWidths();
            reset = true;
          }
          info.SetIEndBorderWidths(currentBorder.width);
          ajaInfo.SetIStartBorderWidths(currentBorder.width);
        }
        // Does the block-start inline-end corner hit the inline-end adjacent
        // cell that wouldn't have an inline border? e.g.
        //
        // o-----------o---------------o
        // |           |               |
        // o-----------x Adjacent cell o
        // | This Cell |   (rowspan)   |
        // o-----------o---------------o
        bool hitsSpanOnIEnd = (rowB > ajaInfo.mRowIndex) &&
                              (rowB < ajaInfo.mRowIndex + ajaInfo.mRowSpan);
        BCCornerInfo* bStartIEndCorner =
            ((0 == rowB) || hitsSpanOnIEnd)
                ? &bStartCorners[info.GetCellEndColIndex() + 1]
                : &bEndCorners[info.GetCellEndColIndex() +
                               1];  // From previous row.
        bStartIEndCorner->Update(LogicalSide::BEnd, currentBorder);
        // If this is a rowspan, need to consider if this "corner" is generating
        // an inline segment for the adjacent cell. e.g.
        //
        // o--------------o----o
        // |              |    |
        // o              x----o
        // | (This "row") |    |
        // o--------------o----o
        if (rowB != info.mRowIndex) {
          currentBorder = priorAjaInfo.GetBEndInternalBorder();
          BCCellBorder adjacentBorder = ajaInfo.GetBStartInternalBorder();
          currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                         adjacentBorder, INLINE_DIR);
          bStartIEndCorner->Update(LogicalSide::IEnd, currentBorder);
        }
        // Check that the spanned area is inside of the invalidation area
        if (info.GetCellEndColIndex() < damageArea.EndCol() &&
            rowB >= damageArea.StartRow()) {
          if (0 != rowB) {
            // Ok, actually store the information
            tableCellMap->SetBCBorderCorner(
                LogicalCorner::BStartIEnd, *iter.mCellMap, iter.mRowGroupStart,
                rowB, info.GetCellEndColIndex(),
                LogicalSide(bStartIEndCorner->ownerSide),
                bStartIEndCorner->subWidth, bStartIEndCorner->bevel);
          }
          // Propagate this segment down the rowspan
          for (int32_t rX = rowB + 1; rX < rowB + segLength; rX++) {
            tableCellMap->SetBCBorderCorner(
                LogicalCorner::BEndIEnd, *iter.mCellMap, iter.mRowGroupStart,
                rX, info.GetCellEndColIndex(),
                LogicalSide(bStartIEndCorner->ownerSide),
                bStartIEndCorner->subWidth, false);
          }
        }
        hitsSpanOnIEnd =
            (rowB + segLength < ajaInfo.mRowIndex + ajaInfo.mRowSpan);
        BCCornerInfo& bEndIEndCorner =
            (hitsSpanOnIEnd) ? bStartCorners[info.GetCellEndColIndex() + 1]
                             : bEndCorners[info.GetCellEndColIndex() + 1];
        bEndIEndCorner.Set(LogicalSide::BStart, currentBorder);
        priorAjaInfo = ajaInfo;
      }
    }
    for (int32_t colIdx = info.mColIndex + 1;
         colIdx <= info.GetCellEndColIndex(); colIdx++) {
      lastBlockDirBorders[colIdx].Reset(0, 1);
    }

    // find the dominant border considering the cell's bEnd border, adjacent
    // cells and the table, row group, row
    if (info.mNumTableRows == info.GetCellEndRowIndex() + 1) {
      // touches bEnd edge of table
      uint8_t idxBEnd = static_cast<uint8_t>(LogicalSide::BEnd);
      if (!tableBorderReset[idxBEnd]) {
        tableInfo.ResetTableBEndBorderWidth();
        tableBorderReset[idxBEnd] = true;
      }
      bool reset = false;
      for (int32_t colIdx = info.mColIndex; colIdx <= info.GetCellEndColIndex();
           colIdx++) {
        info.SetColumn(colIdx);
        BCCellBorder currentBorder = info.GetBEndEdgeBorder();
        BCCornerInfo& bEndIStartCorner = bEndCorners[colIdx];
        bEndIStartCorner.Update(LogicalSide::IEnd, currentBorder);
        tableCellMap->SetBCBorderCorner(
            LogicalCorner::BEndIStart, *iter.mCellMap, iter.mRowGroupStart,
            info.GetCellEndRowIndex(), colIdx,
            LogicalSide(bEndIStartCorner.ownerSide), bEndIStartCorner.subWidth,
            bEndIStartCorner.bevel);
        BCCornerInfo& bEndIEndCorner = bEndCorners[colIdx + 1];
        bEndIEndCorner.Update(LogicalSide::IStart, currentBorder);
        // Store the block-end inline-end corner if it also is the block-end
        // inline-end of the overall table.
        if (info.mNumTableCols == colIdx + 1) {
          tableCellMap->SetBCBorderCorner(
              LogicalCorner::BEndIEnd, *iter.mCellMap, iter.mRowGroupStart,
              info.GetCellEndRowIndex(), colIdx,
              LogicalSide(bEndIEndCorner.ownerSide), bEndIEndCorner.subWidth,
              bEndIEndCorner.bevel, true);
        }
        // update lastBEndBorder and see if a new segment starts
        bool startSeg =
            SetInlineDirBorder(currentBorder, bEndIStartCorner, lastBEndBorder);
        if (!startSeg) {
          // make sure that we did not compare apples to oranges i.e. the
          // current border should be a continuation of the lastBEndBorder,
          // as it is a bEnd border
          // add 1 to the info.GetCellEndRowIndex()
          startSeg =
              (lastBEndBorder.rowIndex != (info.GetCellEndRowIndex() + 1));
        }
        // store the border segment in the cell map and update cellBorders
        tableCellMap->SetBCBorderEdge(
            LogicalSide::BEnd, *iter.mCellMap, iter.mRowGroupStart,
            info.GetCellEndRowIndex(), colIdx, 1, currentBorder.owner,
            currentBorder.width, startSeg);
        // update lastBEndBorders
        lastBEndBorder.rowIndex = info.GetCellEndRowIndex() + 1;
        lastBEndBorder.rowSpan = info.mRowSpan;
        lastBEndBorders[colIdx] = lastBEndBorder;

        // Set border width at block-end (table-wide and for the cell), but
        // only if it's the largest we've encountered.
        if (!reset) {
          info.ResetBEndBorderWidths();
          reset = true;
        }
        info.SetBEndBorderWidths(currentBorder.width);
        tableInfo.SetTableBEndBorderWidth(currentBorder.width);
      }
    } else {
      int32_t segLength = 0;
      BCMapCellInfo ajaInfo(this);
      bool reset = false;
      for (int32_t colIdx = info.mColIndex; colIdx <= info.GetCellEndColIndex();
           colIdx += segLength) {
        // Grab the cell adjacent to our block-end.
        iter.PeekBEnd(info, colIdx, ajaInfo);
        BCCellBorder currentBorder = info.GetBEndInternalBorder();
        BCCellBorder adjacentBorder = ajaInfo.GetBStartInternalBorder();
        currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                       adjacentBorder, INLINE_DIR);
        segLength = std::max(1, ajaInfo.mColIndex + ajaInfo.mColSpan - colIdx);
        segLength =
            std::min(segLength, info.mColIndex + info.mColSpan - colIdx);

        BCCornerInfo& bEndIStartCorner = bEndCorners[colIdx];
        // e.g.
        // o--o----------o
        // |  | This col |
        // o--x----------o
        // |  Adjacent   |
        // o--o----------o
        bool hitsSpanBelow = (colIdx > ajaInfo.mColIndex) &&
                             (colIdx < ajaInfo.mColIndex + ajaInfo.mColSpan);
        bool update = true;
        if (colIdx == info.mColIndex && colIdx > damageArea.StartCol()) {
          int32_t prevRowIndex = lastBEndBorders[colIdx - 1].rowIndex;
          if (prevRowIndex > info.GetCellEndRowIndex() + 1) {
            // hits a rowspan on the iEnd side
            update = false;
            // the corner was taken care of during the cell on the iStart side
          } else if (prevRowIndex < info.GetCellEndRowIndex() + 1) {
            // spans below the cell to the iStart side
            bStartCorners[colIdx] = bEndIStartCorner;
            bEndIStartCorner.Set(LogicalSide::IEnd, currentBorder);
            update = false;
          }
        }
        if (update) {
          bEndIStartCorner.Update(LogicalSide::IEnd, currentBorder);
        }
        // Check that the spanned area is inside of the invalidation area
        if (info.GetCellEndRowIndex() < damageArea.EndRow() &&
            colIdx >= damageArea.StartCol()) {
          if (hitsSpanBelow) {
            tableCellMap->SetBCBorderCorner(
                LogicalCorner::BEndIStart, *iter.mCellMap, iter.mRowGroupStart,
                info.GetCellEndRowIndex(), colIdx,
                LogicalSide(bEndIStartCorner.ownerSide),
                bEndIStartCorner.subWidth, bEndIStartCorner.bevel);
          }
          // Propagate this segment down the colspan
          for (int32_t c = colIdx + 1; c < colIdx + segLength; c++) {
            BCCornerInfo& corner = bEndCorners[c];
            corner.Set(LogicalSide::IEnd, currentBorder);
            tableCellMap->SetBCBorderCorner(
                LogicalCorner::BEndIStart, *iter.mCellMap, iter.mRowGroupStart,
                info.GetCellEndRowIndex(), c, LogicalSide(corner.ownerSide),
                corner.subWidth, false);
          }
        }
        // update lastBEndBorders and see if a new segment starts
        bool startSeg =
            SetInlineDirBorder(currentBorder, bEndIStartCorner, lastBEndBorder);
        if (!startSeg) {
          // make sure that we did not compare apples to oranges i.e. the
          // current border should be a continuation of the lastBEndBorder,
          // as it is a bEnd border
          // add 1 to the info.GetCellEndRowIndex()
          startSeg = (lastBEndBorder.rowIndex != info.GetCellEndRowIndex() + 1);
        }
        lastBEndBorder.rowIndex = info.GetCellEndRowIndex() + 1;
        lastBEndBorder.rowSpan = info.mRowSpan;
        for (int32_t c = colIdx; c < colIdx + segLength; c++) {
          lastBEndBorders[c] = lastBEndBorder;
        }

        // store the border segment the cell map and update cellBorders
        if (info.GetCellEndRowIndex() < damageArea.EndRow() &&
            colIdx >= damageArea.StartCol() && colIdx < damageArea.EndCol()) {
          tableCellMap->SetBCBorderEdge(
              LogicalSide::BEnd, *iter.mCellMap, iter.mRowGroupStart,
              info.GetCellEndRowIndex(), colIdx, segLength, currentBorder.owner,
              currentBorder.width, startSeg);

          if (!reset) {
            info.ResetBEndBorderWidths();
            ajaInfo.ResetBStartBorderWidths();
            reset = true;
          }
          info.SetBEndBorderWidths(currentBorder.width);
          ajaInfo.SetBStartBorderWidths(currentBorder.width);
        }
        // update bEnd-iEnd corner
        BCCornerInfo& bEndIEndCorner = bEndCorners[colIdx + segLength];
        bEndIEndCorner.Update(LogicalSide::IStart, currentBorder);
      }
    }
    // o------o------o
    // |  c1  |      |
    // o------o  c2  o
    // |  c3  |      |
    // o--e1--o--e2--o
    // We normally join edges of successive block-end inline segments by
    // consulting the previous segment; however, cell c2's block-end inline
    // segment e2 is processed before e1, so we need to process such joins
    // out-of-band here, when we're processing c3.
    const auto nextColIndex = info.GetCellEndColIndex() + 1;
    if ((info.mNumTableCols != nextColIndex) &&
        (lastBEndBorders[nextColIndex].rowSpan > 1) &&
        (lastBEndBorders[nextColIndex].rowIndex ==
         info.GetCellEndRowIndex() + 1)) {
      BCCornerInfo& corner = bEndCorners[nextColIndex];
      if (!IsBlock(LogicalSide(corner.ownerSide))) {
        // not a block-dir owner
        BCCellBorder& thisBorder = lastBEndBorder;
        BCCellBorder& nextBorder = lastBEndBorders[info.mColIndex + 1];
        if ((thisBorder.color == nextBorder.color) &&
            (thisBorder.width == nextBorder.width) &&
            (thisBorder.style == nextBorder.style)) {
          // set the flag on the next border indicating it is not the start of a
          // new segment
          if (iter.mCellMap) {
            tableCellMap->ResetBStartStart(
                LogicalSide::BEnd, *iter.mCellMap, iter.mRowGroupStart,
                info.GetCellEndRowIndex(), nextColIndex);
          }
        }
      }
    }
  }  // for (iter.First(info); info.mCell; iter.Next(info)) {
  // reset the bc flag and damage area
  SetNeedToCalcBCBorders(false);
  propData->mDamageArea = TableArea(0, 0, 0, 0);
#ifdef DEBUG_TABLE_CELLMAP
  mCellMap->Dump();
#endif
}

class BCPaintBorderIterator;

struct BCBorderParameters {
  StyleBorderStyle mBorderStyle;
  nscolor mBorderColor;
  nsRect mBorderRect;
  mozilla::Side mStartBevelSide;
  nscoord mStartBevelOffset;
  mozilla::Side mEndBevelSide;
  nscoord mEndBevelOffset;
  bool mBackfaceIsVisible;

  bool NeedToBevel() const {
    if (!mStartBevelOffset && !mEndBevelOffset) {
      return false;
    }

    if (mBorderStyle == StyleBorderStyle::Dashed ||
        mBorderStyle == StyleBorderStyle::Dotted) {
      return false;
    }

    return true;
  }
};

struct BCBlockDirSeg {
  BCBlockDirSeg();

  void Start(BCPaintBorderIterator& aIter, BCBorderOwner aBorderOwner,
             nscoord aBlockSegISize, nscoord aInlineSegBSize,
             Maybe<nscoord> aEmptyRowEndSize);

  void Initialize(BCPaintBorderIterator& aIter);
  void GetBEndCorner(BCPaintBorderIterator& aIter, nscoord aInlineSegBSize);

  Maybe<BCBorderParameters> BuildBorderParameters(BCPaintBorderIterator& aIter,
                                                  nscoord aInlineSegBSize);
  void Paint(BCPaintBorderIterator& aIter, DrawTarget& aDrawTarget,
             nscoord aInlineSegBSize);
  void CreateWebRenderCommands(BCPaintBorderIterator& aIter,
                               nscoord aInlineSegBSize,
                               wr::DisplayListBuilder& aBuilder,
                               const layers::StackingContextHelper& aSc,
                               const nsPoint& aPt);
  void AdvanceOffsetB();
  void IncludeCurrentBorder(BCPaintBorderIterator& aIter);

  union {
    nsTableColFrame* mCol;
    int32_t mColWidth;
  };
  nscoord mOffsetI;  // i-offset with respect to the table edge
  nscoord mOffsetB;  // b-offset with respect to the table edge
  nscoord mLength;   // block-dir length including corners
  nscoord mWidth;    // thickness

  nsTableCellFrame* mAjaCell;    // previous sibling to the first cell
                                 // where the segment starts, it can be
                                 // the owner of a segment
  nsTableCellFrame* mFirstCell;  // cell at the start of the segment
  nsTableRowGroupFrame*
      mFirstRowGroup;           // row group at the start of the segment
  nsTableRowFrame* mFirstRow;   // row at the start of the segment
  nsTableCellFrame* mLastCell;  // cell at the current end of the
                                // segment

  uint8_t mOwner;                // owner of the border, defines the
                                 // style
  LogicalSide mBStartBevelSide;  // direction to bevel at the bStart
  nscoord mBStartBevelOffset;    // how much to bevel at the bStart
  nscoord mBEndInlineSegBSize;   // bSize of the crossing
                                 // inline-dir border
  nscoord mBEndOffset;           // how much longer is the segment due
                                 // to the inline-dir border, by this
                                 // amount the next segment needs to be
                                 // shifted.
  bool mIsBEndBevel;             // should we bevel at the bEnd
};

struct BCInlineDirSeg {
  BCInlineDirSeg();

  void Start(BCPaintBorderIterator& aIter, BCBorderOwner aBorderOwner,
             nscoord aBEndBlockSegISize, nscoord aInlineSegBSize);
  void GetIEndCorner(BCPaintBorderIterator& aIter, nscoord aIStartSegISize);
  void AdvanceOffsetI();
  void IncludeCurrentBorder(BCPaintBorderIterator& aIter);
  Maybe<BCBorderParameters> BuildBorderParameters(BCPaintBorderIterator& aIter);
  void Paint(BCPaintBorderIterator& aIter, DrawTarget& aDrawTarget);
  void CreateWebRenderCommands(BCPaintBorderIterator& aIter,
                               wr::DisplayListBuilder& aBuilder,
                               const layers::StackingContextHelper& aSc,
                               const nsPoint& aPt);

  nscoord mOffsetI;              // i-offset with respect to the table edge
  nscoord mOffsetB;              // b-offset with respect to the table edge
  nscoord mLength;               // inline-dir length including corners
  nscoord mWidth;                // border thickness
  nscoord mIStartBevelOffset;    // how much to bevel at the iStart
  LogicalSide mIStartBevelSide;  // direction to bevel at the iStart
  bool mIsIEndBevel;             // should we bevel at the iEnd end
  nscoord mIEndBevelOffset;      // how much to bevel at the iEnd
  LogicalSide mIEndBevelSide;    // direction to bevel at the iEnd
  nscoord mEndOffset;            // how much longer is the segment due
                                 // to the block-dir border, by this
                                 // amount the next segment needs to be
                                 // shifted.
  uint8_t mOwner;                // owner of the border, defines the
                                 // style
  nsTableCellFrame* mFirstCell;  // cell at the start of the segment
  nsTableCellFrame* mAjaCell;    // neighboring cell to the first cell
                                 // where the segment starts, it can be
                                 // the owner of a segment
};

struct BCPaintData {
  explicit BCPaintData(DrawTarget& aDrawTarget) : mDrawTarget(aDrawTarget) {}

  DrawTarget& mDrawTarget;
};

struct BCCreateWebRenderCommandsData {
  BCCreateWebRenderCommandsData(wr::DisplayListBuilder& aBuilder,
                                const layers::StackingContextHelper& aSc,
                                const nsPoint& aOffsetToReferenceFrame)
      : mBuilder(aBuilder),
        mSc(aSc),
        mOffsetToReferenceFrame(aOffsetToReferenceFrame) {}

  wr::DisplayListBuilder& mBuilder;
  const layers::StackingContextHelper& mSc;
  const nsPoint& mOffsetToReferenceFrame;
};

struct BCPaintBorderAction {
  explicit BCPaintBorderAction(DrawTarget& aDrawTarget)
      : mMode(Mode::Paint), mPaintData(aDrawTarget) {}

  BCPaintBorderAction(wr::DisplayListBuilder& aBuilder,
                      const layers::StackingContextHelper& aSc,
                      const nsPoint& aOffsetToReferenceFrame)
      : mMode(Mode::CreateWebRenderCommands),
        mCreateWebRenderCommandsData(aBuilder, aSc, aOffsetToReferenceFrame) {}

  ~BCPaintBorderAction() {
    // mCreateWebRenderCommandsData is in a union which means the destructor
    // wouldn't be called when BCPaintBorderAction get destroyed. So call the
    // destructor here explicitly.
    if (mMode == Mode::CreateWebRenderCommands) {
      mCreateWebRenderCommandsData.~BCCreateWebRenderCommandsData();
    }
  }

  enum class Mode {
    Paint,
    CreateWebRenderCommands,
  };

  Mode mMode;

  union {
    BCPaintData mPaintData;
    BCCreateWebRenderCommandsData mCreateWebRenderCommandsData;
  };
};

// Iterates over borders (iStart border, corner, bStart border) in the cell map
// within a damage area from iStart to iEnd, bStart to bEnd. All members are in
// terms of the 1st in flow frames, except where suffixed by InFlow.
class BCPaintBorderIterator {
 public:
  explicit BCPaintBorderIterator(nsTableFrame* aTable);
  void Reset();

  /**
   * Determine the damage area in terms of rows and columns and finalize
   * mInitialOffsetI and mInitialOffsetB.
   * @param aDirtyRect - dirty rect in table coordinates
   * @return - true if we need to paint something given dirty rect
   */
  bool SetDamageArea(const nsRect& aDamageRect);
  void First();
  void Next();
  void AccumulateOrDoActionInlineDirSegment(BCPaintBorderAction& aAction);
  void AccumulateOrDoActionBlockDirSegment(BCPaintBorderAction& aAction);
  void ResetVerInfo();
  void StoreColumnWidth(int32_t aIndex);
  bool BlockDirSegmentOwnsCorner();

  nsTableFrame* mTable;
  nsTableFrame* mTableFirstInFlow;
  nsTableCellMap* mTableCellMap;
  nsCellMap* mCellMap;
  WritingMode mTableWM;
  nsTableFrame::RowGroupArray mRowGroups;

  nsTableRowGroupFrame* mPrevRg;
  nsTableRowGroupFrame* mRg;
  bool mIsRepeatedHeader;
  bool mIsRepeatedFooter;
  nsTableRowGroupFrame* mStartRg;   // first row group in the damagearea
  int32_t mRgIndex;                 // current row group index in the
                                    // mRowgroups array
  int32_t mFifRgFirstRowIndex;      // start row index of the first in
                                    // flow of the row group
  int32_t mRgFirstRowIndex;         // row index of the first row in the
                                    // row group
  int32_t mRgLastRowIndex;          // row index of the last row in the row
                                    // group
  int32_t mNumTableRows;            // number of rows in the table and all
                                    // continuations
  int32_t mNumTableCols;            // number of columns in the table
  int32_t mColIndex;                // with respect to the table
  int32_t mRowIndex;                // with respect to the table
  int32_t mRepeatedHeaderRowIndex;  // row index in a repeated
                                    // header, it's equivalent to
                                    // mRowIndex when we're in a repeated
                                    // header, and set to the last row
                                    // index of a repeated header when
                                    // we're not
  bool mIsNewRow;
  bool mAtEnd;  // the iterator cycled over all
                // borders
  nsTableRowFrame* mPrevRow;
  nsTableRowFrame* mRow;
  nsTableRowFrame* mStartRow;  // first row in a inside the damagearea

  // cell properties
  nsTableCellFrame* mPrevCell;
  nsTableCellFrame* mCell;
  BCCellData* mPrevCellData;
  BCCellData* mCellData;
  BCData* mBCData;

  bool IsTableBStartMost() {
    return (mRowIndex == 0) && !mTable->GetPrevInFlow();
  }
  bool IsTableIEndMost() { return (mColIndex >= mNumTableCols); }
  bool IsTableBEndMost() {
    return (mRowIndex >= mNumTableRows) && !mTable->GetNextInFlow();
  }
  bool IsTableIStartMost() { return (mColIndex == 0); }
  bool IsDamageAreaBStartMost() const {
    return mRowIndex == mDamageArea.StartRow();
  }
  bool IsDamageAreaIEndMost() const {
    return mColIndex >= mDamageArea.EndCol();
  }
  bool IsDamageAreaBEndMost() const {
    return mRowIndex >= mDamageArea.EndRow();
  }
  bool IsDamageAreaIStartMost() const {
    return mColIndex == mDamageArea.StartCol();
  }
  int32_t GetRelativeColIndex() const {
    return mColIndex - mDamageArea.StartCol();
  }

  TableArea mDamageArea;  // damageArea in cellmap coordinates
  bool IsAfterRepeatedHeader() {
    return !mIsRepeatedHeader && (mRowIndex == (mRepeatedHeaderRowIndex + 1));
  }
  bool StartRepeatedFooter() const {
    return mIsRepeatedFooter && mRowIndex == mRgFirstRowIndex &&
           mRowIndex != mDamageArea.StartRow();
  }

  nscoord mInitialOffsetI;  // offsetI of the first border with
                            // respect to the table
  nscoord mInitialOffsetB;  // offsetB of the first border with
                            // respect to the table
  nscoord mNextOffsetB;     // offsetB of the next segment
  // this array is used differently when
  // inline-dir and block-dir borders are drawn
  // When inline-dir border are drawn we cache
  // the column widths and the width of the
  // block-dir borders that arrive from bStart
  // When we draw block-dir borders we store
  // lengths and width for block-dir borders
  // before they are drawn while we  move over
  // the columns in the damage area
  // It has one more elements than columns are
  // in the table.
  UniquePtr<BCBlockDirSeg[]> mBlockDirInfo;
  BCInlineDirSeg mInlineSeg;    // the inline-dir segment while we
                                // move over the colums
  nscoord mPrevInlineSegBSize;  // the bSize of the previous
                                // inline-dir border

 private:
  bool SetNewRow(nsTableRowFrame* aRow = nullptr);
  bool SetNewRowGroup();
  void SetNewData(int32_t aRowIndex, int32_t aColIndex);
};

BCPaintBorderIterator::BCPaintBorderIterator(nsTableFrame* aTable)
    : mTable(aTable),
      mTableFirstInFlow(static_cast<nsTableFrame*>(aTable->FirstInFlow())),
      mTableCellMap(aTable->GetCellMap()),
      mCellMap(nullptr),
      mTableWM(aTable->Style()),
      mRowGroups(aTable->OrderedRowGroups()),
      mPrevRg(nullptr),
      mRg(nullptr),
      mIsRepeatedHeader(false),
      mIsRepeatedFooter(false),
      mStartRg(nullptr),
      mRgIndex(0),
      mFifRgFirstRowIndex(0),
      mRgFirstRowIndex(0),
      mRgLastRowIndex(0),
      mColIndex(0),
      mRowIndex(0),
      mIsNewRow(false),
      mAtEnd(false),
      mPrevRow(nullptr),
      mRow(nullptr),
      mStartRow(nullptr),
      mPrevCell(nullptr),
      mCell(nullptr),
      mPrevCellData(nullptr),
      mCellData(nullptr),
      mBCData(nullptr),
      mInitialOffsetI(0),
      mNextOffsetB(0),
      mPrevInlineSegBSize(0) {
  MOZ_ASSERT(mTable->IsBorderCollapse(),
             "Why are we here if the table is not border-collapsed?");

  const LogicalMargin bp = mTable->GetOuterBCBorder(mTableWM);
  // block position of first row in damage area
  mInitialOffsetB = mTable->GetPrevInFlow() ? 0 : bp.BStart(mTableWM);
  mNumTableRows = mTable->GetRowCount();
  mNumTableCols = mTable->GetColCount();

  // initialize to a non existing index
  mRepeatedHeaderRowIndex = -99;
}

bool BCPaintBorderIterator::SetDamageArea(const nsRect& aDirtyRect) {
  nsSize containerSize = mTable->GetSize();
  LogicalRect dirtyRect(mTableWM, aDirtyRect, containerSize);
  uint32_t startRowIndex = 0, endRowIndex = 0;
  bool done = false;
  bool haveIntersect = false;
  // find startRowIndex, endRowIndex
  nscoord rowB = mInitialOffsetB;
  for (uint32_t rgIdx = 0; rgIdx < mRowGroups.Length() && !done; rgIdx++) {
    nsTableRowGroupFrame* rgFrame = mRowGroups[rgIdx];
    for (nsTableRowFrame* rowFrame = rgFrame->GetFirstRow(); rowFrame;
         rowFrame = rowFrame->GetNextRow()) {
      // get the row rect relative to the table rather than the row group
      nscoord rowBSize = rowFrame->BSize(mTableWM);
      const nscoord onePx = mTable->PresContext()->DevPixelsToAppUnits(1);
      if (haveIntersect) {
        // conservatively estimate the half border widths outside the row
        nscoord borderHalf = mTable->GetPrevInFlow()
                                 ? 0
                                 : rowFrame->GetBStartBCBorderWidth() + onePx;

        if (dirtyRect.BEnd(mTableWM) >= rowB - borderHalf) {
          nsTableRowFrame* fifRow =
              static_cast<nsTableRowFrame*>(rowFrame->FirstInFlow());
          endRowIndex = fifRow->GetRowIndex();
        } else {
          done = true;
        }
      } else {
        // conservatively estimate the half border widths outside the row
        nscoord borderHalf = mTable->GetNextInFlow()
                                 ? 0
                                 : rowFrame->GetBEndBCBorderWidth() + onePx;
        if (rowB + rowBSize + borderHalf >= dirtyRect.BStart(mTableWM)) {
          mStartRg = rgFrame;
          mStartRow = rowFrame;
          nsTableRowFrame* fifRow =
              static_cast<nsTableRowFrame*>(rowFrame->FirstInFlow());
          startRowIndex = endRowIndex = fifRow->GetRowIndex();
          haveIntersect = true;
        } else {
          mInitialOffsetB += rowBSize;
        }
      }
      rowB += rowBSize;
    }
  }
  mNextOffsetB = mInitialOffsetB;

  // XXX comment refers to the obsolete NS_FRAME_OUTSIDE_CHILDREN flag
  // XXX but I don't understand it, so not changing it for now
  // table wrapper borders overflow the table, so the table might be
  // target to other areas as the NS_FRAME_OUTSIDE_CHILDREN is set
  // on the table
  if (!haveIntersect) {
    return false;
  }
  // find startColIndex, endColIndex, startColX
  haveIntersect = false;
  if (0 == mNumTableCols) {
    return false;
  }

  LogicalMargin bp = mTable->GetOuterBCBorder(mTableWM);

  // inline position of first col in damage area
  mInitialOffsetI = bp.IStart(mTableWM);

  nscoord x = 0;
  uint32_t startColIndex = 0, endColIndex = 0;
  for (int32_t colIdx = 0; colIdx != mNumTableCols; colIdx++) {
    nsTableColFrame* colFrame = mTableFirstInFlow->GetColFrame(colIdx);
    if (!colFrame) ABORT1(false);
    const nscoord onePx = mTable->PresContext()->DevPixelsToAppUnits(1);
    // get the col rect relative to the table rather than the col group
    nscoord colISize = colFrame->ISize(mTableWM);
    if (haveIntersect) {
      // conservatively estimate the iStart half border width outside the col
      nscoord iStartBorderHalf = colFrame->GetIStartBorderWidth() + onePx;
      if (dirtyRect.IEnd(mTableWM) >= x - iStartBorderHalf) {
        endColIndex = colIdx;
      } else {
        break;
      }
    } else {
      // conservatively estimate the iEnd half border width outside the col
      nscoord iEndBorderHalf = colFrame->GetIEndBorderWidth() + onePx;
      if (x + colISize + iEndBorderHalf >= dirtyRect.IStart(mTableWM)) {
        startColIndex = endColIndex = colIdx;
        haveIntersect = true;
      } else {
        mInitialOffsetI += colISize;
      }
    }
    x += colISize;
  }
  if (!haveIntersect) {
    return false;
  }
  MOZ_ASSERT(endColIndex >= startColIndex);
  mDamageArea =
      TableArea(startColIndex, startRowIndex, 1 + endColIndex - startColIndex,
                1 + endRowIndex - startRowIndex);

  Reset();
  mBlockDirInfo = MakeUnique<BCBlockDirSeg[]>(mDamageArea.ColCount() + 1);
  return true;
}

void BCPaintBorderIterator::Reset() {
  mAtEnd = true;  // gets reset when First() is called
  mRg = mStartRg;
  mPrevRow = nullptr;
  mRow = mStartRow;
  mRowIndex = 0;
  mColIndex = 0;
  mRgIndex = -1;
  mPrevCell = nullptr;
  mCell = nullptr;
  mPrevCellData = nullptr;
  mCellData = nullptr;
  mBCData = nullptr;
  ResetVerInfo();
}

/**
 * Set the iterator data to a new cellmap coordinate
 * @param aRowIndex - the row index
 * @param aColIndex - the col index
 */
void BCPaintBorderIterator::SetNewData(int32_t aY, int32_t aX) {
  if (!mTableCellMap || !mTableCellMap->mBCInfo) ABORT0();

  mColIndex = aX;
  mRowIndex = aY;
  mPrevCellData = mCellData;
  if (IsTableIEndMost() && IsTableBEndMost()) {
    mCell = nullptr;
    mBCData = &mTableCellMap->mBCInfo->mBEndIEndCorner;
  } else if (IsTableIEndMost()) {
    mCellData = nullptr;
    mBCData = &mTableCellMap->mBCInfo->mIEndBorders.ElementAt(aY);
  } else if (IsTableBEndMost()) {
    mCellData = nullptr;
    mBCData = &mTableCellMap->mBCInfo->mBEndBorders.ElementAt(aX);
  } else {
    // We should have set mCellMap during SetNewRowGroup, but if we failed to
    // find the appropriate map there, let's just give up.
    // Bailing out here may leave us with some missing borders, but seems
    // preferable to crashing. (Bug 1442018)
    if (MOZ_UNLIKELY(!mCellMap)) {
      ABORT0();
    }
    if (uint32_t(mRowIndex - mFifRgFirstRowIndex) < mCellMap->mRows.Length()) {
      mBCData = nullptr;
      mCellData = (BCCellData*)mCellMap->mRows[mRowIndex - mFifRgFirstRowIndex]
                      .SafeElementAt(mColIndex);
      if (mCellData) {
        mBCData = &mCellData->mData;
        if (!mCellData->IsOrig()) {
          if (mCellData->IsRowSpan()) {
            aY -= mCellData->GetRowSpanOffset();
          }
          if (mCellData->IsColSpan()) {
            aX -= mCellData->GetColSpanOffset();
          }
          if ((aX >= 0) && (aY >= 0)) {
            mCellData =
                (BCCellData*)mCellMap->mRows[aY - mFifRgFirstRowIndex][aX];
          }
        }
        if (mCellData->IsOrig()) {
          mPrevCell = mCell;
          mCell = mCellData->GetCellFrame();
        }
      }
    }
  }
}

/**
 * Set the iterator to a new row
 * @param aRow - the new row frame, if null the iterator will advance to the
 *               next row
 */
bool BCPaintBorderIterator::SetNewRow(nsTableRowFrame* aRow) {
  mPrevRow = mRow;
  mRow = (aRow) ? aRow : mRow->GetNextRow();
  if (mRow) {
    mIsNewRow = true;
    mRowIndex = mRow->GetRowIndex();
    mColIndex = mDamageArea.StartCol();
    mPrevInlineSegBSize = 0;
    if (mIsRepeatedHeader) {
      mRepeatedHeaderRowIndex = mRowIndex;
    }
  } else {
    mAtEnd = true;
  }
  return !mAtEnd;
}

/**
 * Advance the iterator to the next row group
 */
bool BCPaintBorderIterator::SetNewRowGroup() {
  mRgIndex++;

  mIsRepeatedHeader = false;
  mIsRepeatedFooter = false;

  NS_ASSERTION(mRgIndex >= 0, "mRgIndex out of bounds");
  if (uint32_t(mRgIndex) < mRowGroups.Length()) {
    mPrevRg = mRg;
    mRg = mRowGroups[mRgIndex];
    nsTableRowGroupFrame* fifRg =
        static_cast<nsTableRowGroupFrame*>(mRg->FirstInFlow());
    mFifRgFirstRowIndex = fifRg->GetStartRowIndex();
    mRgFirstRowIndex = mRg->GetStartRowIndex();
    mRgLastRowIndex = mRgFirstRowIndex + mRg->GetRowCount() - 1;

    if (SetNewRow(mRg->GetFirstRow())) {
      mCellMap = mTableCellMap->GetMapFor(fifRg, nullptr);
      if (!mCellMap) ABORT1(false);
    }
    if (mTable->GetPrevInFlow() && !mRg->GetPrevInFlow()) {
      // if mRowGroup doesn't have a prev in flow, then it may be a repeated
      // header or footer
      const nsStyleDisplay* display = mRg->StyleDisplay();
      if (mRowIndex == mDamageArea.StartRow()) {
        mIsRepeatedHeader =
            (mozilla::StyleDisplay::TableHeaderGroup == display->mDisplay);
      } else {
        mIsRepeatedFooter =
            (mozilla::StyleDisplay::TableFooterGroup == display->mDisplay);
      }
    }
  } else {
    mAtEnd = true;
  }
  return !mAtEnd;
}

/**
 *  Move the iterator to the first position in the damageArea
 */
void BCPaintBorderIterator::First() {
  if (!mTable || mDamageArea.StartCol() >= mNumTableCols ||
      mDamageArea.StartRow() >= mNumTableRows)
    ABORT0();

  mAtEnd = false;

  uint32_t numRowGroups = mRowGroups.Length();
  for (uint32_t rgY = 0; rgY < numRowGroups; rgY++) {
    nsTableRowGroupFrame* rowG = mRowGroups[rgY];
    int32_t start = rowG->GetStartRowIndex();
    int32_t end = start + rowG->GetRowCount() - 1;
    if (mDamageArea.StartRow() >= start && mDamageArea.StartRow() <= end) {
      mRgIndex = rgY - 1;  // SetNewRowGroup increments rowGroupIndex
      if (SetNewRowGroup()) {
        while (mRowIndex < mDamageArea.StartRow() && !mAtEnd) {
          SetNewRow();
        }
        if (!mAtEnd) {
          SetNewData(mDamageArea.StartRow(), mDamageArea.StartCol());
        }
      }
      return;
    }
  }
  mAtEnd = true;
}

/**
 * Advance the iterator to the next position
 */
void BCPaintBorderIterator::Next() {
  if (mAtEnd) ABORT0();
  mIsNewRow = false;

  mColIndex++;
  if (mColIndex > mDamageArea.EndCol()) {
    mRowIndex++;
    if (mRowIndex == mDamageArea.EndRow()) {
      mColIndex = mDamageArea.StartCol();
    } else if (mRowIndex < mDamageArea.EndRow()) {
      if (mRowIndex <= mRgLastRowIndex) {
        SetNewRow();
      } else {
        SetNewRowGroup();
      }
    } else {
      mAtEnd = true;
    }
  }
  if (!mAtEnd) {
    SetNewData(mRowIndex, mColIndex);
  }
}

// XXX if CalcVerCornerOffset and CalcHorCornerOffset remain similar, combine
// them
// XXX Update terminology from physical to logical
/** Compute the vertical offset of a vertical border segment
 * @param aCornerOwnerSide - which side owns the corner
 * @param aCornerSubWidth  - how wide is the nonwinning side of the corner
 * @param aHorWidth        - how wide is the horizontal edge of the corner
 * @param aIsStartOfSeg    - does this corner start a new segment
 * @param aIsBevel         - is this corner beveled
 * @return                 - offset in twips
 */
static nscoord CalcVerCornerOffset(LogicalSide aCornerOwnerSide,
                                   nscoord aCornerSubWidth, nscoord aHorWidth,
                                   bool aIsStartOfSeg, bool aIsBevel) {
  nscoord offset = 0;
  // XXX These should be replaced with appropriate side-specific macros (which?)
  nscoord smallHalf, largeHalf;
  if (IsBlock(aCornerOwnerSide)) {
    DivideBCBorderSize(aCornerSubWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    } else {
      offset =
          (LogicalSide::BStart == aCornerOwnerSide) ? smallHalf : -largeHalf;
    }
  } else {
    DivideBCBorderSize(aHorWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    } else {
      offset = (aIsStartOfSeg) ? smallHalf : -largeHalf;
    }
  }
  return offset;
}

/** Compute the horizontal offset of a horizontal border segment
 * @param aCornerOwnerSide - which side owns the corner
 * @param aCornerSubWidth  - how wide is the nonwinning side of the corner
 * @param aVerWidth        - how wide is the vertical edge of the corner
 * @param aIsStartOfSeg    - does this corner start a new segment
 * @param aIsBevel         - is this corner beveled
 * @return                 - offset in twips
 */
static nscoord CalcHorCornerOffset(LogicalSide aCornerOwnerSide,
                                   nscoord aCornerSubWidth, nscoord aVerWidth,
                                   bool aIsStartOfSeg, bool aIsBevel) {
  nscoord offset = 0;
  // XXX These should be replaced with appropriate side-specific macros (which?)
  nscoord smallHalf, largeHalf;
  if (IsInline(aCornerOwnerSide)) {
    DivideBCBorderSize(aCornerSubWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    } else {
      offset =
          (LogicalSide::IStart == aCornerOwnerSide) ? smallHalf : -largeHalf;
    }
  } else {
    DivideBCBorderSize(aVerWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    } else {
      offset = (aIsStartOfSeg) ? smallHalf : -largeHalf;
    }
  }
  return offset;
}

BCBlockDirSeg::BCBlockDirSeg()
    : mFirstRowGroup(nullptr),
      mFirstRow(nullptr),
      mBEndInlineSegBSize(0),
      mBEndOffset(0),
      mIsBEndBevel(false) {
  mCol = nullptr;
  mFirstCell = mLastCell = mAjaCell = nullptr;
  mOffsetI = mOffsetB = mLength = mWidth = mBStartBevelOffset = 0;
  mBStartBevelSide = LogicalSide::BStart;
  mOwner = eCellOwner;
}

/**
 * Start a new block-direction segment
 * @param aIter         - iterator containing the structural information
 * @param aBorderOwner  - determines the border style
 * @param aBlockSegISize  - the width of segment
 * @param aInlineSegBSize - the width of the inline-dir segment joining the
 * corner at the start
 */
void BCBlockDirSeg::Start(BCPaintBorderIterator& aIter,
                          BCBorderOwner aBorderOwner, nscoord aBlockSegISize,
                          nscoord aInlineSegBSize,
                          Maybe<nscoord> aEmptyRowEndBSize) {
  LogicalSide ownerSide = LogicalSide::BStart;
  bool bevel = false;

  nscoord cornerSubWidth =
      (aIter.mBCData) ? aIter.mBCData->GetCorner(ownerSide, bevel) : 0;

  bool bStartBevel = (aBlockSegISize > 0) ? bevel : false;
  nscoord maxInlineSegBSize =
      std::max(aIter.mPrevInlineSegBSize, aInlineSegBSize);
  nscoord offset = CalcVerCornerOffset(ownerSide, cornerSubWidth,
                                       maxInlineSegBSize, true, bStartBevel);

  mBStartBevelOffset = bStartBevel ? maxInlineSegBSize : 0;
  // XXX this assumes that only corners where 2 segments join can be beveled
  mBStartBevelSide =
      (aInlineSegBSize > 0) ? LogicalSide::IEnd : LogicalSide::IStart;
  if (aEmptyRowEndBSize && *aEmptyRowEndBSize < offset) {
    // This segment is starting from an empty row. This will require the the
    // starting segment to overlap with the previously drawn segment, unless the
    // empty row's size clears the overlap.
    mOffsetB += *aEmptyRowEndBSize;
  } else {
    mOffsetB += offset;
  }
  mLength = -offset;
  mWidth = aBlockSegISize;
  mOwner = aBorderOwner;
  mFirstCell = aIter.mCell;
  mFirstRowGroup = aIter.mRg;
  mFirstRow = aIter.mRow;
  if (aIter.GetRelativeColIndex() > 0) {
    mAjaCell = aIter.mBlockDirInfo[aIter.GetRelativeColIndex() - 1].mLastCell;
  }
}

/**
 * Initialize the block-dir segments with information that will persist for any
 * block-dir segment in this column
 * @param aIter - iterator containing the structural information
 */
void BCBlockDirSeg::Initialize(BCPaintBorderIterator& aIter) {
  int32_t relColIndex = aIter.GetRelativeColIndex();
  mCol = aIter.IsTableIEndMost()
             ? aIter.mBlockDirInfo[relColIndex - 1].mCol
             : aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex);
  if (!mCol) ABORT0();
  if (0 == relColIndex) {
    mOffsetI = aIter.mInitialOffsetI;
  }
  // set mOffsetI for the next column
  if (!aIter.IsDamageAreaIEndMost()) {
    aIter.mBlockDirInfo[relColIndex + 1].mOffsetI =
        mOffsetI + mCol->ISize(aIter.mTableWM);
  }
  mOffsetB = aIter.mInitialOffsetB;
  mLastCell = aIter.mCell;
}

/**
 * Compute the offsets for the bEnd corner of a block-dir segment
 * @param aIter           - iterator containing the structural information
 * @param aInlineSegBSize - the width of the inline-dir segment joining the
 * corner at the start
 */
void BCBlockDirSeg::GetBEndCorner(BCPaintBorderIterator& aIter,
                                  nscoord aInlineSegBSize) {
  LogicalSide ownerSide = LogicalSide::BStart;
  nscoord cornerSubWidth = 0;
  bool bevel = false;
  if (aIter.mBCData) {
    cornerSubWidth = aIter.mBCData->GetCorner(ownerSide, bevel);
  }
  mIsBEndBevel = (mWidth > 0) ? bevel : false;
  mBEndInlineSegBSize = std::max(aIter.mPrevInlineSegBSize, aInlineSegBSize);
  mBEndOffset = CalcVerCornerOffset(ownerSide, cornerSubWidth,
                                    mBEndInlineSegBSize, false, mIsBEndBevel);
  mLength += mBEndOffset;
}

Maybe<BCBorderParameters> BCBlockDirSeg::BuildBorderParameters(
    BCPaintBorderIterator& aIter, nscoord aInlineSegBSize) {
  BCBorderParameters result;

  // get the border style, color and paint the segment
  LogicalSide side =
      aIter.IsDamageAreaIEndMost() ? LogicalSide::IEnd : LogicalSide::IStart;
  int32_t relColIndex = aIter.GetRelativeColIndex();
  nsTableColFrame* col = mCol;
  if (!col) ABORT1(Nothing());
  nsTableCellFrame* cell = mFirstCell;  // ???
  nsIFrame* owner = nullptr;
  result.mBorderStyle = StyleBorderStyle::Solid;
  result.mBorderColor = 0xFFFFFFFF;
  result.mBackfaceIsVisible = true;

  switch (mOwner) {
    case eTableOwner:
      owner = aIter.mTable;
      break;
    case eAjaColGroupOwner:
      side = LogicalSide::IEnd;
      if (!aIter.IsTableIEndMost() && (relColIndex > 0)) {
        col = aIter.mBlockDirInfo[relColIndex - 1].mCol;
      }
      [[fallthrough]];
    case eColGroupOwner:
      if (col) {
        owner = col->GetParent();
      }
      break;
    case eAjaColOwner:
      side = LogicalSide::IEnd;
      if (!aIter.IsTableIEndMost() && (relColIndex > 0)) {
        col = aIter.mBlockDirInfo[relColIndex - 1].mCol;
      }
      [[fallthrough]];
    case eColOwner:
      owner = col;
      break;
    case eAjaRowGroupOwner:
      NS_ERROR("a neighboring rowgroup can never own a vertical border");
      [[fallthrough]];
    case eRowGroupOwner:
      NS_ASSERTION(aIter.IsTableIStartMost() || aIter.IsTableIEndMost(),
                   "row group can own border only at table edge");
      owner = mFirstRowGroup;
      break;
    case eAjaRowOwner:
      NS_ERROR("program error");
      [[fallthrough]];
    case eRowOwner:
      NS_ASSERTION(aIter.IsTableIStartMost() || aIter.IsTableIEndMost(),
                   "row can own border only at table edge");
      owner = mFirstRow;
      break;
    case eAjaCellOwner:
      side = LogicalSide::IEnd;
      cell = mAjaCell;
      [[fallthrough]];
    case eCellOwner:
      owner = cell;
      break;
  }
  if (owner) {
    ::GetPaintStyleInfo(owner, aIter.mTableWM, side, &result.mBorderStyle,
                        &result.mBorderColor);
    result.mBackfaceIsVisible = !owner->BackfaceIsHidden();
  }
  nscoord smallHalf, largeHalf;
  DivideBCBorderSize(mWidth, smallHalf, largeHalf);
  LogicalRect segRect(aIter.mTableWM, mOffsetI - largeHalf, mOffsetB, mWidth,
                      mLength);
  nscoord bEndBevelOffset = mIsBEndBevel ? mBEndInlineSegBSize : 0;
  LogicalSide bEndBevelSide =
      (aInlineSegBSize > 0) ? LogicalSide::IEnd : LogicalSide::IStart;

  // Convert logical to physical sides/coordinates for DrawTableBorderSegment.

  result.mBorderRect =
      segRect.GetPhysicalRect(aIter.mTableWM, aIter.mTable->GetSize());
  // XXX For reversed vertical writing-modes (with direction:rtl), we need to
  // invert physicalRect's y-position here, with respect to the table.
  // However, it's not worth fixing the border positions here until the
  // ordering of the table columns themselves is also fixed (bug 1180528).

  result.mStartBevelSide = aIter.mTableWM.PhysicalSide(mBStartBevelSide);
  result.mEndBevelSide = aIter.mTableWM.PhysicalSide(bEndBevelSide);
  result.mStartBevelOffset = mBStartBevelOffset;
  result.mEndBevelOffset = bEndBevelOffset;
  // In vertical-rl mode, the 'start' and 'end' of the block-dir (horizontal)
  // border segment need to be swapped because DrawTableBorderSegment will
  // apply the 'start' bevel at the left edge, and 'end' at the right.
  // (Note: In this case, startBevelSide/endBevelSide will usually both be
  // "top" or "bottom". DrawTableBorderSegment works purely with physical
  // coordinates, so it expects startBevelOffset to be the indentation-from-
  // the-left for the "start" (left) end of the border-segment, and
  // endBevelOffset is the indentation-from-the-right for the "end" (right)
  // end of the border-segment. We've got them reversed, since our block dir
  // is RTL, so we have to swap them here.)
  if (aIter.mTableWM.IsVerticalRL()) {
    std::swap(result.mStartBevelSide, result.mEndBevelSide);
    std::swap(result.mStartBevelOffset, result.mEndBevelOffset);
  }

  return Some(result);
}

/**
 * Paint the block-dir segment
 * @param aIter           - iterator containing the structural information
 * @param aDrawTarget     - the draw target
 * @param aInlineSegBSize - the width of the inline-dir segment joining the
 *                          corner at the start
 */
void BCBlockDirSeg::Paint(BCPaintBorderIterator& aIter, DrawTarget& aDrawTarget,
                          nscoord aInlineSegBSize) {
  Maybe<BCBorderParameters> param =
      BuildBorderParameters(aIter, aInlineSegBSize);
  if (param.isNothing()) {
    return;
  }

  nsCSSRendering::DrawTableBorderSegment(
      aDrawTarget, param->mBorderStyle, param->mBorderColor, param->mBorderRect,
      aIter.mTable->PresContext()->AppUnitsPerDevPixel(),
      param->mStartBevelSide, param->mStartBevelOffset, param->mEndBevelSide,
      param->mEndBevelOffset);
}

// Pushes a border bevel triangle and substracts the relevant rectangle from
// aRect, which, after all the bevels, will end up being a solid segment rect.
static void AdjustAndPushBevel(wr::DisplayListBuilder& aBuilder,
                               wr::LayoutRect& aRect, nscolor aColor,
                               const nsCSSRendering::Bevel& aBevel,
                               int32_t aAppUnitsPerDevPixel,
                               bool aBackfaceIsVisible, bool aIsStart) {
  if (!aBevel.mOffset) {
    return;
  }

  const auto kTransparent = wr::ToColorF(gfx::DeviceColor(0., 0., 0., 0.));
  const bool horizontal =
      aBevel.mSide == eSideTop || aBevel.mSide == eSideBottom;

  // Crappy CSS triangle as known by every web developer ever :)
  Float offset = NSAppUnitsToFloatPixels(aBevel.mOffset, aAppUnitsPerDevPixel);
  wr::LayoutRect bevelRect = aRect;
  wr::BorderSide bevelBorder[4];
  for (const auto i : mozilla::AllPhysicalSides()) {
    bevelBorder[i] =
        wr::ToBorderSide(ToDeviceColor(aColor), StyleBorderStyle::Solid);
  }

  // We're creating a half-transparent triangle using the border primitive.
  //
  // Classic web-dev trick, with a gotcha: we use a single corner to avoid
  // seams and rounding errors.
  //
  // Classic web-dev trick :P
  auto borderWidths = wr::ToBorderWidths(0, 0, 0, 0);
  bevelBorder[aBevel.mSide].color = kTransparent;
  if (aIsStart) {
    if (horizontal) {
      bevelBorder[eSideLeft].color = kTransparent;
      borderWidths.left = offset;
    } else {
      bevelBorder[eSideTop].color = kTransparent;
      borderWidths.top = offset;
    }
  } else {
    if (horizontal) {
      bevelBorder[eSideRight].color = kTransparent;
      borderWidths.right = offset;
    } else {
      bevelBorder[eSideBottom].color = kTransparent;
      borderWidths.bottom = offset;
    }
  }

  if (horizontal) {
    if (aIsStart) {
      aRect.min.x += offset;
      aRect.max.x += offset;
    } else {
      bevelRect.min.x += aRect.width() - offset;
      bevelRect.max.x += aRect.width() - offset;
    }
    aRect.max.x -= offset;
    bevelRect.max.y = bevelRect.min.y + aRect.height();
    bevelRect.max.x = bevelRect.min.x + offset;
    if (aBevel.mSide == eSideTop) {
      borderWidths.bottom = aRect.height();
    } else {
      borderWidths.top = aRect.height();
    }
  } else {
    if (aIsStart) {
      aRect.min.y += offset;
      aRect.max.y += offset;
    } else {
      bevelRect.min.y += aRect.height() - offset;
      bevelRect.max.y += aRect.height() - offset;
    }
    aRect.max.y -= offset;
    bevelRect.max.x = bevelRect.min.x + aRect.width();
    bevelRect.max.y = bevelRect.min.y + offset;
    if (aBevel.mSide == eSideLeft) {
      borderWidths.right = aRect.width();
    } else {
      borderWidths.left = aRect.width();
    }
  }

  Range<const wr::BorderSide> wrsides(bevelBorder, 4);
  // It's important to _not_ anti-alias the bevel, because otherwise we wouldn't
  // be able bevel to sides of the same color without bleeding in the middle.
  aBuilder.PushBorder(bevelRect, bevelRect, aBackfaceIsVisible, borderWidths,
                      wrsides, wr::EmptyBorderRadius(),
                      wr::AntialiasBorder::No);
}

static void CreateWRCommandsForBeveledBorder(
    const BCBorderParameters& aBorderParams, wr::DisplayListBuilder& aBuilder,
    const layers::StackingContextHelper& aSc, const nsPoint& aOffset,
    nscoord aAppUnitsPerDevPixel) {
  MOZ_ASSERT(aBorderParams.NeedToBevel());

  AutoTArray<nsCSSRendering::SolidBeveledBorderSegment, 3> segments;
  nsCSSRendering::GetTableBorderSolidSegments(
      segments, aBorderParams.mBorderStyle, aBorderParams.mBorderColor,
      aBorderParams.mBorderRect, aAppUnitsPerDevPixel,
      aBorderParams.mStartBevelSide, aBorderParams.mStartBevelOffset,
      aBorderParams.mEndBevelSide, aBorderParams.mEndBevelOffset);

  for (const auto& segment : segments) {
    auto rect = LayoutDeviceRect::FromUnknownRect(
        NSRectToRect(segment.mRect + aOffset, aAppUnitsPerDevPixel));
    auto r = wr::ToLayoutRect(rect);
    auto color = wr::ToColorF(ToDeviceColor(segment.mColor));

    // Adjust for the start bevel if needed.
    AdjustAndPushBevel(aBuilder, r, segment.mColor, segment.mStartBevel,
                       aAppUnitsPerDevPixel, aBorderParams.mBackfaceIsVisible,
                       true);

    AdjustAndPushBevel(aBuilder, r, segment.mColor, segment.mEndBevel,
                       aAppUnitsPerDevPixel, aBorderParams.mBackfaceIsVisible,
                       false);

    aBuilder.PushRect(r, r, aBorderParams.mBackfaceIsVisible, false, false,
                      color);
  }
}

static void CreateWRCommandsForBorderSegment(
    const BCBorderParameters& aBorderParams, wr::DisplayListBuilder& aBuilder,
    const layers::StackingContextHelper& aSc, const nsPoint& aOffset,
    nscoord aAppUnitsPerDevPixel) {
  if (aBorderParams.NeedToBevel()) {
    CreateWRCommandsForBeveledBorder(aBorderParams, aBuilder, aSc, aOffset,
                                     aAppUnitsPerDevPixel);
    return;
  }

  auto borderRect = LayoutDeviceRect::FromUnknownRect(
      NSRectToRect(aBorderParams.mBorderRect + aOffset, aAppUnitsPerDevPixel));

  wr::LayoutRect r = wr::ToLayoutRect(borderRect);
  wr::BorderSide wrSide[4];
  for (const auto i : mozilla::AllPhysicalSides()) {
    wrSide[i] = wr::ToBorderSide(ToDeviceColor(aBorderParams.mBorderColor),
                                 StyleBorderStyle::None);
  }
  const bool horizontal = aBorderParams.mStartBevelSide == eSideTop ||
                          aBorderParams.mStartBevelSide == eSideBottom;
  auto borderWidth = horizontal ? r.height() : r.width();

  // All border style is set to none except left side. So setting the widths of
  // each side to width of rect is fine.
  auto borderWidths = wr::ToBorderWidths(0, 0, 0, 0);

  wrSide[horizontal ? eSideTop : eSideLeft] = wr::ToBorderSide(
      ToDeviceColor(aBorderParams.mBorderColor), aBorderParams.mBorderStyle);

  if (horizontal) {
    borderWidths.top = borderWidth;
  } else {
    borderWidths.left = borderWidth;
  }

  Range<const wr::BorderSide> wrsides(wrSide, 4);
  aBuilder.PushBorder(r, r, aBorderParams.mBackfaceIsVisible, borderWidths,
                      wrsides, wr::EmptyBorderRadius());
}

void BCBlockDirSeg::CreateWebRenderCommands(
    BCPaintBorderIterator& aIter, nscoord aInlineSegBSize,
    wr::DisplayListBuilder& aBuilder, const layers::StackingContextHelper& aSc,
    const nsPoint& aOffset) {
  Maybe<BCBorderParameters> param =
      BuildBorderParameters(aIter, aInlineSegBSize);
  if (param.isNothing()) {
    return;
  }

  CreateWRCommandsForBorderSegment(
      *param, aBuilder, aSc, aOffset,
      aIter.mTable->PresContext()->AppUnitsPerDevPixel());
}

/**
 * Advance the start point of a segment
 */
void BCBlockDirSeg::AdvanceOffsetB() { mOffsetB += mLength - mBEndOffset; }

/**
 * Accumulate the current segment
 */
void BCBlockDirSeg::IncludeCurrentBorder(BCPaintBorderIterator& aIter) {
  mLastCell = aIter.mCell;
  mLength += aIter.mRow->BSize(aIter.mTableWM);
}

BCInlineDirSeg::BCInlineDirSeg()
    : mIsIEndBevel(false),
      mIEndBevelOffset(0),
      mIEndBevelSide(LogicalSide::BStart),
      mEndOffset(0),
      mOwner(eTableOwner) {
  mOffsetI = mOffsetB = mLength = mWidth = mIStartBevelOffset = 0;
  mIStartBevelSide = LogicalSide::BStart;
  mFirstCell = mAjaCell = nullptr;
}

/** Initialize an inline-dir border segment for painting
  * @param aIter              - iterator storing the current and adjacent frames
  * @param aBorderOwner       - which frame owns the border
  * @param aBEndBlockSegISize - block-dir segment width coming from up
  * @param aInlineSegBSize    - the thickness of the segment
  +  */
void BCInlineDirSeg::Start(BCPaintBorderIterator& aIter,
                           BCBorderOwner aBorderOwner,
                           nscoord aBEndBlockSegISize,
                           nscoord aInlineSegBSize) {
  LogicalSide cornerOwnerSide = LogicalSide::BStart;
  bool bevel = false;

  mOwner = aBorderOwner;
  nscoord cornerSubWidth =
      (aIter.mBCData) ? aIter.mBCData->GetCorner(cornerOwnerSide, bevel) : 0;

  bool iStartBevel = (aInlineSegBSize > 0) ? bevel : false;
  int32_t relColIndex = aIter.GetRelativeColIndex();
  nscoord maxBlockSegISize =
      std::max(aIter.mBlockDirInfo[relColIndex].mWidth, aBEndBlockSegISize);
  nscoord offset = CalcHorCornerOffset(cornerOwnerSide, cornerSubWidth,
                                       maxBlockSegISize, true, iStartBevel);
  mIStartBevelOffset =
      (iStartBevel && (aInlineSegBSize > 0)) ? maxBlockSegISize : 0;
  // XXX this assumes that only corners where 2 segments join can be beveled
  mIStartBevelSide =
      (aBEndBlockSegISize > 0) ? LogicalSide::BEnd : LogicalSide::BStart;
  mOffsetI += offset;
  mLength = -offset;
  mWidth = aInlineSegBSize;
  mFirstCell = aIter.mCell;
  mAjaCell = (aIter.IsDamageAreaBStartMost())
                 ? nullptr
                 : aIter.mBlockDirInfo[relColIndex].mLastCell;
}

/**
 * Compute the offsets for the iEnd corner of an inline-dir segment
 * @param aIter         - iterator containing the structural information
 * @param aIStartSegISize - the iSize of the block-dir segment joining the
 * corner at the start
 */
void BCInlineDirSeg::GetIEndCorner(BCPaintBorderIterator& aIter,
                                   nscoord aIStartSegISize) {
  LogicalSide ownerSide = LogicalSide::BStart;
  nscoord cornerSubWidth = 0;
  bool bevel = false;
  if (aIter.mBCData) {
    cornerSubWidth = aIter.mBCData->GetCorner(ownerSide, bevel);
  }

  mIsIEndBevel = (mWidth > 0) ? bevel : 0;
  int32_t relColIndex = aIter.GetRelativeColIndex();
  nscoord verWidth =
      std::max(aIter.mBlockDirInfo[relColIndex].mWidth, aIStartSegISize);
  mEndOffset = CalcHorCornerOffset(ownerSide, cornerSubWidth, verWidth, false,
                                   mIsIEndBevel);
  mLength += mEndOffset;
  mIEndBevelOffset = mIsIEndBevel ? verWidth : 0;
  mIEndBevelSide =
      (aIStartSegISize > 0) ? LogicalSide::BEnd : LogicalSide::BStart;
}

Maybe<BCBorderParameters> BCInlineDirSeg::BuildBorderParameters(
    BCPaintBorderIterator& aIter) {
  BCBorderParameters result;

  // get the border style, color and paint the segment
  LogicalSide side =
      aIter.IsDamageAreaBEndMost() ? LogicalSide::BEnd : LogicalSide::BStart;
  nsIFrame* rg = aIter.mRg;
  if (!rg) ABORT1(Nothing());
  nsIFrame* row = aIter.mRow;
  if (!row) ABORT1(Nothing());
  nsIFrame* cell = mFirstCell;
  nsIFrame* col;
  nsIFrame* owner = nullptr;
  result.mBackfaceIsVisible = true;
  result.mBorderStyle = StyleBorderStyle::Solid;
  result.mBorderColor = 0xFFFFFFFF;

  switch (mOwner) {
    case eTableOwner:
      owner = aIter.mTable;
      break;
    case eAjaColGroupOwner:
      NS_ERROR("neighboring colgroups can never own an inline-dir border");
      [[fallthrough]];
    case eColGroupOwner:
      NS_ASSERTION(aIter.IsTableBStartMost() || aIter.IsTableBEndMost(),
                   "col group can own border only at the table edge");
      col = aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex - 1);
      if (!col) ABORT1(Nothing());
      owner = col->GetParent();
      break;
    case eAjaColOwner:
      NS_ERROR("neighboring column can never own an inline-dir border");
      [[fallthrough]];
    case eColOwner:
      NS_ASSERTION(aIter.IsTableBStartMost() || aIter.IsTableBEndMost(),
                   "col can own border only at the table edge");
      owner = aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex - 1);
      break;
    case eAjaRowGroupOwner:
      side = LogicalSide::BEnd;
      rg = (aIter.IsTableBEndMost()) ? aIter.mRg : aIter.mPrevRg;
      [[fallthrough]];
    case eRowGroupOwner:
      owner = rg;
      break;
    case eAjaRowOwner:
      side = LogicalSide::BEnd;
      row = (aIter.IsTableBEndMost()) ? aIter.mRow : aIter.mPrevRow;
      [[fallthrough]];
    case eRowOwner:
      owner = row;
      break;
    case eAjaCellOwner:
      side = LogicalSide::BEnd;
      // if this is null due to the damage area origin-y > 0, then the border
      // won't show up anyway
      cell = mAjaCell;
      [[fallthrough]];
    case eCellOwner:
      owner = cell;
      break;
  }
  if (owner) {
    ::GetPaintStyleInfo(owner, aIter.mTableWM, side, &result.mBorderStyle,
                        &result.mBorderColor);
    result.mBackfaceIsVisible = !owner->BackfaceIsHidden();
  }
  nscoord smallHalf, largeHalf;
  DivideBCBorderSize(mWidth, smallHalf, largeHalf);
  LogicalRect segRect(aIter.mTableWM, mOffsetI, mOffsetB - largeHalf, mLength,
                      mWidth);

  // Convert logical to physical sides/coordinates for DrawTableBorderSegment.
  result.mBorderRect =
      segRect.GetPhysicalRect(aIter.mTableWM, aIter.mTable->GetSize());
  result.mStartBevelSide = aIter.mTableWM.PhysicalSide(mIStartBevelSide);
  result.mEndBevelSide = aIter.mTableWM.PhysicalSide(mIEndBevelSide);
  result.mStartBevelOffset = mIStartBevelOffset;
  result.mEndBevelOffset = mIEndBevelOffset;
  // With inline-RTL directionality, the 'start' and 'end' of the inline-dir
  // border segment need to be swapped because DrawTableBorderSegment will
  // apply the 'start' bevel physically at the left or top edge, and 'end' at
  // the right or bottom.
  // (Note: startBevelSide/endBevelSide will be "top" or "bottom" in horizontal
  // writing mode, or "left" or "right" in vertical mode.
  // DrawTableBorderSegment works purely with physical coordinates, so it
  // expects startBevelOffset to be the indentation-from-the-left or top end
  // of the border-segment, and endBevelOffset is the indentation-from-the-
  // right or bottom end. If the writing mode is inline-RTL, our "start" and
  // "end" will be reversed from this physical-coord view, so we have to swap
  // them here.
  if (aIter.mTableWM.IsBidiRTL()) {
    std::swap(result.mStartBevelSide, result.mEndBevelSide);
    std::swap(result.mStartBevelOffset, result.mEndBevelOffset);
  }

  return Some(result);
}

/**
 * Paint the inline-dir segment
 * @param aIter       - iterator containing the structural information
 * @param aDrawTarget - the draw target
 */
void BCInlineDirSeg::Paint(BCPaintBorderIterator& aIter,
                           DrawTarget& aDrawTarget) {
  Maybe<BCBorderParameters> param = BuildBorderParameters(aIter);
  if (param.isNothing()) {
    return;
  }

  nsCSSRendering::DrawTableBorderSegment(
      aDrawTarget, param->mBorderStyle, param->mBorderColor, param->mBorderRect,
      aIter.mTable->PresContext()->AppUnitsPerDevPixel(),
      param->mStartBevelSide, param->mStartBevelOffset, param->mEndBevelSide,
      param->mEndBevelOffset);
}

void BCInlineDirSeg::CreateWebRenderCommands(
    BCPaintBorderIterator& aIter, wr::DisplayListBuilder& aBuilder,
    const layers::StackingContextHelper& aSc, const nsPoint& aPt) {
  Maybe<BCBorderParameters> param = BuildBorderParameters(aIter);
  if (param.isNothing()) {
    return;
  }

  CreateWRCommandsForBorderSegment(
      *param, aBuilder, aSc, aPt,
      aIter.mTable->PresContext()->AppUnitsPerDevPixel());
}

/**
 * Advance the start point of a segment
 */
void BCInlineDirSeg::AdvanceOffsetI() { mOffsetI += (mLength - mEndOffset); }

/**
 * Accumulate the current segment
 */
void BCInlineDirSeg::IncludeCurrentBorder(BCPaintBorderIterator& aIter) {
  mLength += aIter.mBlockDirInfo[aIter.GetRelativeColIndex()].mColWidth;
}

/**
 * store the column width information while painting inline-dir segment
 */
void BCPaintBorderIterator::StoreColumnWidth(int32_t aIndex) {
  if (IsTableIEndMost()) {
    mBlockDirInfo[aIndex].mColWidth = mBlockDirInfo[aIndex - 1].mColWidth;
  } else {
    nsTableColFrame* col = mTableFirstInFlow->GetColFrame(mColIndex);
    if (!col) ABORT0();
    mBlockDirInfo[aIndex].mColWidth = col->ISize(mTableWM);
  }
}
/**
 * Determine if a block-dir segment owns the corner
 */
bool BCPaintBorderIterator::BlockDirSegmentOwnsCorner() {
  LogicalSide cornerOwnerSide = LogicalSide::BStart;
  bool bevel = false;
  if (mBCData) {
    mBCData->GetCorner(cornerOwnerSide, bevel);
  }
  // unitialized ownerside, bevel
  return (LogicalSide::BStart == cornerOwnerSide) ||
         (LogicalSide::BEnd == cornerOwnerSide);
}

/**
 * Paint if necessary an inline-dir segment, otherwise accumulate it
 * @param aDrawTarget - the draw target
 */
void BCPaintBorderIterator::AccumulateOrDoActionInlineDirSegment(
    BCPaintBorderAction& aAction) {
  int32_t relColIndex = GetRelativeColIndex();
  // store the current col width if it hasn't been already
  if (mBlockDirInfo[relColIndex].mColWidth < 0) {
    StoreColumnWidth(relColIndex);
  }

  BCBorderOwner borderOwner = eCellOwner;
  BCBorderOwner ignoreBorderOwner;
  bool isSegStart = true;
  bool ignoreSegStart;

  nscoord iStartSegISize =
      mBCData ? mBCData->GetIStartEdge(ignoreBorderOwner, ignoreSegStart) : 0;
  nscoord bStartSegBSize =
      mBCData ? mBCData->GetBStartEdge(borderOwner, isSegStart) : 0;

  if (mIsNewRow || (IsDamageAreaIStartMost() && IsDamageAreaBEndMost())) {
    // reset for every new row and on the bottom of the last row
    mInlineSeg.mOffsetB = mNextOffsetB;
    mNextOffsetB = mNextOffsetB + mRow->BSize(mTableWM);
    mInlineSeg.mOffsetI = mInitialOffsetI;
    mInlineSeg.Start(*this, borderOwner, iStartSegISize, bStartSegBSize);
  }

  if (!IsDamageAreaIStartMost() &&
      (isSegStart || IsDamageAreaIEndMost() || BlockDirSegmentOwnsCorner())) {
    // paint the previous seg or the current one if IsDamageAreaIEndMost()
    if (mInlineSeg.mLength > 0) {
      mInlineSeg.GetIEndCorner(*this, iStartSegISize);
      if (mInlineSeg.mWidth > 0) {
        if (aAction.mMode == BCPaintBorderAction::Mode::Paint) {
          mInlineSeg.Paint(*this, aAction.mPaintData.mDrawTarget);
        } else {
          MOZ_ASSERT(aAction.mMode ==
                     BCPaintBorderAction::Mode::CreateWebRenderCommands);
          mInlineSeg.CreateWebRenderCommands(
              *this, aAction.mCreateWebRenderCommandsData.mBuilder,
              aAction.mCreateWebRenderCommandsData.mSc,
              aAction.mCreateWebRenderCommandsData.mOffsetToReferenceFrame);
        }
      }
      mInlineSeg.AdvanceOffsetI();
    }
    mInlineSeg.Start(*this, borderOwner, iStartSegISize, bStartSegBSize);
  }
  mInlineSeg.IncludeCurrentBorder(*this);
  mBlockDirInfo[relColIndex].mWidth = iStartSegISize;
  mBlockDirInfo[relColIndex].mLastCell = mCell;
}

/**
 * Paint if necessary a block-dir segment, otherwise accumulate it
 * @param aDrawTarget - the draw target
 */
void BCPaintBorderIterator::AccumulateOrDoActionBlockDirSegment(
    BCPaintBorderAction& aAction) {
  BCBorderOwner borderOwner = eCellOwner;
  BCBorderOwner ignoreBorderOwner;
  bool isSegStart = true;
  bool ignoreSegStart;

  nscoord blockSegISize =
      mBCData ? mBCData->GetIStartEdge(borderOwner, isSegStart) : 0;
  nscoord inlineSegBSize =
      mBCData ? mBCData->GetBStartEdge(ignoreBorderOwner, ignoreSegStart) : 0;

  int32_t relColIndex = GetRelativeColIndex();
  BCBlockDirSeg& blockDirSeg = mBlockDirInfo[relColIndex];
  if (!blockDirSeg.mCol) {  // on the first damaged row and the first segment in
                            // the col
    blockDirSeg.Initialize(*this);
    blockDirSeg.Start(*this, borderOwner, blockSegISize, inlineSegBSize,
                      Nothing{});
  }

  if (!IsDamageAreaBStartMost() &&
      (isSegStart || IsDamageAreaBEndMost() || IsAfterRepeatedHeader() ||
       StartRepeatedFooter())) {
    Maybe<nscoord> emptyRowEndSize;
    // paint the previous seg or the current one if IsDamageAreaBEndMost()
    if (blockDirSeg.mLength > 0) {
      blockDirSeg.GetBEndCorner(*this, inlineSegBSize);
      if (blockDirSeg.mWidth > 0) {
        if (aAction.mMode == BCPaintBorderAction::Mode::Paint) {
          blockDirSeg.Paint(*this, aAction.mPaintData.mDrawTarget,
                            inlineSegBSize);
        } else {
          MOZ_ASSERT(aAction.mMode ==
                     BCPaintBorderAction::Mode::CreateWebRenderCommands);
          blockDirSeg.CreateWebRenderCommands(
              *this, inlineSegBSize,
              aAction.mCreateWebRenderCommandsData.mBuilder,
              aAction.mCreateWebRenderCommandsData.mSc,
              aAction.mCreateWebRenderCommandsData.mOffsetToReferenceFrame);
        }
      }
      blockDirSeg.AdvanceOffsetB();
      if (mRow->PrincipalChildList().IsEmpty()) {
        emptyRowEndSize = Some(mRow->BSize(mTableWM));
      }
    }
    blockDirSeg.Start(*this, borderOwner, blockSegISize, inlineSegBSize,
                      emptyRowEndSize);
  }
  blockDirSeg.IncludeCurrentBorder(*this);
  mPrevInlineSegBSize = inlineSegBSize;
}

/**
 * Reset the block-dir information cache
 */
void BCPaintBorderIterator::ResetVerInfo() {
  if (mBlockDirInfo) {
    memset(mBlockDirInfo.get(), 0,
           mDamageArea.ColCount() * sizeof(BCBlockDirSeg));
    // XXX reinitialize properly
    for (auto xIndex : IntegerRange(mDamageArea.ColCount())) {
      mBlockDirInfo[xIndex].mColWidth = -1;
    }
  }
}

void nsTableFrame::IterateBCBorders(BCPaintBorderAction& aAction,
                                    const nsRect& aDirtyRect) {
  // We first transfer the aDirtyRect into cellmap coordinates to compute which
  // cell borders need to be painted
  BCPaintBorderIterator iter(this);
  if (!iter.SetDamageArea(aDirtyRect)) {
    return;
  }

  // XXX comment still has physical terminology
  // First, paint all of the vertical borders from top to bottom and left to
  // right as they become complete. They are painted first, since they are less
  // efficient to paint than horizontal segments. They were stored with as few
  // segments as possible (since horizontal borders are painted last and
  // possibly over them). For every cell in a row that fails in the damage are
  // we look up if the current border would start a new segment, if so we paint
  // the previously stored vertical segment and start a new segment. After
  // this we  the now active segment with the current border. These
  // segments are stored in mBlockDirInfo to be used on the next row
  for (iter.First(); !iter.mAtEnd; iter.Next()) {
    iter.AccumulateOrDoActionBlockDirSegment(aAction);
  }

  // Next, paint all of the inline-dir border segments from bStart to bEnd reuse
  // the mBlockDirInfo array to keep track of col widths and block-dir segments
  // for corner calculations
  iter.Reset();
  for (iter.First(); !iter.mAtEnd; iter.Next()) {
    iter.AccumulateOrDoActionInlineDirSegment(aAction);
  }
}

/**
 * Method to paint BCBorders, this does not use currently display lists although
 * it will do this in future
 * @param aDrawTarget - the rendering context
 * @param aDirtyRect  - inside this rectangle the BC Borders will redrawn
 */
void nsTableFrame::PaintBCBorders(DrawTarget& aDrawTarget,
                                  const nsRect& aDirtyRect) {
  BCPaintBorderAction action(aDrawTarget);
  IterateBCBorders(action, aDirtyRect);
}

void nsTableFrame::CreateWebRenderCommandsForBCBorders(
    wr::DisplayListBuilder& aBuilder,
    const mozilla::layers::StackingContextHelper& aSc,
    const nsRect& aVisibleRect, const nsPoint& aOffsetToReferenceFrame) {
  BCPaintBorderAction action(aBuilder, aSc, aOffsetToReferenceFrame);
  // We always draw whole table border for webrender. Passing the visible rect
  // dirty rect.
  IterateBCBorders(action, aVisibleRect - aOffsetToReferenceFrame);
}

bool nsTableFrame::RowHasSpanningCells(int32_t aRowIndex, int32_t aNumEffCols) {
  bool result = false;
  nsTableCellMap* cellMap = GetCellMap();
  MOZ_ASSERT(cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->RowHasSpanningCells(aRowIndex, aNumEffCols);
  }
  return result;
}

bool nsTableFrame::RowIsSpannedInto(int32_t aRowIndex, int32_t aNumEffCols) {
  bool result = false;
  nsTableCellMap* cellMap = GetCellMap();
  MOZ_ASSERT(cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->RowIsSpannedInto(aRowIndex, aNumEffCols);
  }
  return result;
}

/* static */
void nsTableFrame::InvalidateTableFrame(nsIFrame* aFrame,
                                        const nsRect& aOrigRect,
                                        const nsRect& aOrigInkOverflow,
                                        bool aIsFirstReflow) {
  nsIFrame* parent = aFrame->GetParent();
  NS_ASSERTION(parent, "What happened here?");

  if (parent->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Don't bother; we'll invalidate the parent's overflow rect when
    // we finish reflowing it.
    return;
  }

  // The part that looks at both the rect and the overflow rect is a
  // bit of a hack.  See nsBlockFrame::ReflowLine for an eloquent
  // description of its hackishness.
  //
  // This doesn't really make sense now that we have DLBI.
  // This code can probably be simplified a fair bit.
  nsRect inkOverflow = aFrame->InkOverflowRect();
  if (aIsFirstReflow || aOrigRect.TopLeft() != aFrame->GetPosition() ||
      aOrigInkOverflow.TopLeft() != inkOverflow.TopLeft()) {
    // Invalidate the old and new overflow rects.  Note that if the
    // frame moved, we can't just use aOrigInkOverflow, since it's in
    // coordinates relative to the old position.  So invalidate via
    // aFrame's parent, and reposition that overflow rect to the right
    // place.
    // XXXbz this doesn't handle outlines, does it?
    aFrame->InvalidateFrame();
    parent->InvalidateFrameWithRect(aOrigInkOverflow + aOrigRect.TopLeft());
  } else if (aOrigRect.Size() != aFrame->GetSize() ||
             aOrigInkOverflow.Size() != inkOverflow.Size()) {
    aFrame->InvalidateFrameWithRect(aOrigInkOverflow);
    aFrame->InvalidateFrame();
  }
}

void nsTableFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  nsIFrame* wrapper = GetParent();
  MOZ_ASSERT(
      wrapper->Style()->GetPseudoType() == PseudoStyleType::MozTableWrapper,
      "What happened to our parent?");
  aResult.AppendElement(
      OwnedAnonBox(wrapper, &UpdateStyleOfOwnedAnonBoxesForTableWrapper));
}

/* static */
void nsTableFrame::UpdateStyleOfOwnedAnonBoxesForTableWrapper(
    nsIFrame* aOwningFrame, nsIFrame* aWrapperFrame,
    ServoRestyleState& aRestyleState) {
  MOZ_ASSERT(aWrapperFrame->Style()->GetPseudoType() ==
                 PseudoStyleType::MozTableWrapper,
             "What happened to our parent?");

  RefPtr<ComputedStyle> newStyle =
      aRestyleState.StyleSet().ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozTableWrapper, aOwningFrame->Style());

  // Figure out whether we have an actual change.  It's important that we do
  // this, even though all the wrapper's changes are due to properties it
  // inherits from us, because it's possible that no one ever asked us for those
  // style structs and hence changes to them aren't reflected in
  // the handled changes at all.
  //
  // Also note that extensions can add/remove stylesheets that change the styles
  // of anonymous boxes directly, so we need to handle that potential change
  // here.
  //
  // NOTE(emilio): We can't use the ChangesHandledFor optimization (and we
  // assert against that), because the table wrapper is up in the frame tree
  // compared to the owner frame.
  uint32_t equalStructs;  // Not used, actually.
  nsChangeHint wrapperHint =
      aWrapperFrame->Style()->CalcStyleDifference(*newStyle, &equalStructs);

  if (wrapperHint) {
    aRestyleState.ChangeList().AppendChange(
        aWrapperFrame, aWrapperFrame->GetContent(), wrapperHint);
  }

  for (nsIFrame* cur = aWrapperFrame; cur; cur = cur->GetNextContinuation()) {
    cur->SetComputedStyle(newStyle);
  }

  MOZ_ASSERT(!aWrapperFrame->HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES),
             "Wrapper frame doesn't have any anon boxes of its own!");
}

namespace mozilla {

nsRect nsDisplayTableItem::GetBounds(nsDisplayListBuilder* aBuilder,
                                     bool* aSnap) const {
  *aSnap = false;
  return mFrame->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
}

nsDisplayTableBackgroundSet::nsDisplayTableBackgroundSet(
    nsDisplayListBuilder* aBuilder, nsIFrame* aTable)
    : mBuilder(aBuilder),
      mColGroupBackgrounds(aBuilder),
      mColBackgrounds(aBuilder) {
  mPrevTableBackgroundSet = mBuilder->SetTableBackgroundSet(this);
  mozilla::DebugOnly<const nsIFrame*> reference =
      mBuilder->FindReferenceFrameFor(aTable, &mToReferenceFrame);
  MOZ_ASSERT(nsLayoutUtils::FindNearestCommonAncestorFrame(reference, aTable));
  mDirtyRect = mBuilder->GetDirtyRect();
  mCombinedTableClipChain =
      mBuilder->ClipState().GetCurrentCombinedClipChain(aBuilder);
  mTableASR = mBuilder->CurrentActiveScrolledRoot();
}

// A display item that draws all collapsed borders for a table.
// At some point, we may want to find a nicer partitioning for dividing
// border-collapse segments into their own display items.
class nsDisplayTableBorderCollapse final : public nsDisplayTableItem {
 public:
  nsDisplayTableBorderCollapse(nsDisplayListBuilder* aBuilder,
                               nsTableFrame* aFrame)
      : nsDisplayTableItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayTableBorderCollapse);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayTableBorderCollapse)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
  bool CreateWebRenderCommands(
      wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override;
  NS_DISPLAY_DECL_NAME("TableBorderCollapse", TYPE_TABLE_BORDER_COLLAPSE)
};

void nsDisplayTableBorderCollapse::Paint(nsDisplayListBuilder* aBuilder,
                                         gfxContext* aCtx) {
  nsPoint pt = ToReferenceFrame();
  DrawTarget* drawTarget = aCtx->GetDrawTarget();

  gfxPoint devPixelOffset = nsLayoutUtils::PointToGfxPoint(
      pt, mFrame->PresContext()->AppUnitsPerDevPixel());

  // XXX we should probably get rid of this translation at some stage
  // But that would mean modifying PaintBCBorders, ugh
  AutoRestoreTransform autoRestoreTransform(drawTarget);
  drawTarget->SetTransform(
      drawTarget->GetTransform().PreTranslate(ToPoint(devPixelOffset)));

  static_cast<nsTableFrame*>(mFrame)->PaintBCBorders(
      *drawTarget, GetPaintRect(aBuilder, aCtx) - pt);
}

bool nsDisplayTableBorderCollapse::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  bool dummy;
  static_cast<nsTableFrame*>(mFrame)->CreateWebRenderCommandsForBCBorders(
      aBuilder, aSc, GetBounds(aDisplayListBuilder, &dummy),
      ToReferenceFrame());
  return true;
}

}  // namespace mozilla
