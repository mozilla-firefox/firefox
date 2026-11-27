/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsTableColGroupFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsCOMPtr.h"
#include "nsCSSRendering.h"
#include "nsHTMLParts.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsTableColFrame.h"
#include "nsTableFrame.h"

using namespace mozilla;

#define COLGROUP_SYNTHETIC_BIT NS_FRAME_STATE_BIT(30)

bool nsTableColGroupFrame::IsSynthetic() const {
  return HasAnyStateBits(COLGROUP_SYNTHETIC_BIT);
}

void nsTableColGroupFrame::SetIsSynthetic() {
  AddStateBits(COLGROUP_SYNTHETIC_BIT);
}

static void ResetColIndexOfGroup(nsTableColGroupFrame* aGroup,
                                 int32_t aFirstColIndex,
                                 nsIFrame* aStartColFrame, int32_t& aCurIndex) {
  // reset the starting col index for the first cg only if we should reset
  // the whole colgroup (aStartColFrame defaults to nullptr) or if
  // aFirstColIndex is smaller than the existing starting col index
  if (aCurIndex != aFirstColIndex ||
      aCurIndex < aGroup->GetStartColumnIndex() || !aStartColFrame) {
    aGroup->SetStartColumnIndex(aCurIndex);
  }
  nsIFrame* colFrame = aStartColFrame;
  if (!colFrame || aCurIndex != aFirstColIndex) {
    colFrame = aGroup->PrincipalChildList().FirstChild();
  }
  while (colFrame) {
    if (colFrame->IsTableColFrame()) {
      ((nsTableColFrame*)colFrame)->SetColIndex(aCurIndex);
      aCurIndex++;
    }
    colFrame = colFrame->GetNextSibling();
  }
}

void nsTableColGroupFrame::ResetColIndices(
    nsIFrame* aFirstFrame, nsTableColGroupFrame* aSyntheticColGroup,
    int32_t aFirstColIndex, nsIFrame* aStartColFrame) {
  int32_t colIndex = aFirstColIndex;
  for (nsIFrame* cur = aFirstFrame; cur; cur = cur->GetNextSibling()) {
    if (nsTableColGroupFrame* colGroupFrame = do_QueryFrame(cur)) {
      if (colGroupFrame->IsSynthetic()) {
        MOZ_ASSERT(colGroupFrame == aSyntheticColGroup);
      } else {
        ResetColIndexOfGroup(colGroupFrame, aFirstColIndex, aStartColFrame,
                             colIndex);
      }
    }
  }
  if (aSyntheticColGroup) {
    ResetColIndexOfGroup(aSyntheticColGroup, aFirstColIndex, aStartColFrame,
                         colIndex);
  }
}

nsresult nsTableColGroupFrame::AddColsToTable(int32_t aFirstColIndex,
                                              bool aResetSubsequentColIndices,
                                              const nsFrameList::Slice& aCols) {
  nsTableFrame* tableFrame = GetTableFrame();

  tableFrame->InvalidateFrameSubtree();

  // set the col indices of the col frames and and add col info to the table
  int32_t colIndex = aFirstColIndex;

  // XXX: We cannot use range-based for loop because InsertCol() can destroy the
  // nsTableColFrame in the slice we're traversing! Need to check the validity
  // of *colIter.
  auto colIter = aCols.begin();
  for (auto colIterEnd = aCols.end(); *colIter && colIter != colIterEnd;
       ++colIter) {
    auto* colFrame = static_cast<nsTableColFrame*>(*colIter);
    colFrame->SetColIndex(colIndex);
    mColCount++;
    tableFrame->InsertCol(*colFrame, colIndex);
    colIndex++;
  }

  for (; *colIter; ++colIter) {
    auto* colFrame = static_cast<nsTableColFrame*>(*colIter);
    colFrame->SetColIndex(colIndex);
    colIndex++;
  }

  // We have already set the colindex for all the colframes in this
  // colgroup that come after the first inserted colframe, but there could
  // be other colgroups following this one and their colframes need
  // correct colindices too.
  if (aResetSubsequentColIndices && !IsSynthetic()) {
    ResetColIndices(GetNextSibling(), GetSyntheticColGroup(), colIndex);
  }

  return NS_OK;
}

// don't set mColCount here, it is done in AddColsToTable
void nsTableColGroupFrame::SetInitialChildList(ChildListID aListID,
                                               nsFrameList&& aChildList) {
  MOZ_ASSERT(mFrames.IsEmpty(),
             "unexpected second call to SetInitialChildList");
  MOZ_ASSERT(aListID == FrameChildListID::Principal, "unexpected child list");
#ifdef DEBUG
  for (nsIFrame* f : aChildList) {
    MOZ_ASSERT(f->GetParent() == this, "Unexpected parent");
  }
#endif
  if (aChildList.IsEmpty()) {
    GetTableFrame()->AppendAnonymousColFrames(this, GetSpan(),
                                              eColAnonymousColGroup, false);
    return;
  }

  mFrames.AppendFrames(this, std::move(aChildList));
}

/* virtual */
void nsTableColGroupFrame::DidSetComputedStyle(
    ComputedStyle* aOldComputedStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  if (!aOldComputedStyle) {  // avoid this on init
    return;
  }

  nsTableFrame* tableFrame = GetTableFrame();
  if (tableFrame->IsBorderCollapse() &&
      tableFrame->BCRecalcNeeded(aOldComputedStyle, Style())) {
    int32_t colCount = GetColCount();
    if (!colCount) {
      return;  // this is a degenerated colgroup
    }
    TableArea damageArea(GetFirstColumn()->GetColIndex(), 0, colCount,
                         tableFrame->GetRowCount());
    tableFrame->AddBCDamageArea(damageArea);
  }
}

void nsTableColGroupFrame::AppendFrames(ChildListID aListID,
                                        nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");

  nsTableColFrame* col = GetFirstColumn();
  nsTableColFrame* nextCol;
  while (col && col->GetColType() == eColAnonymousColGroup) {
    // this colgroup spans one or more columns but now that there is a
    // real column below, spanned anonymous columns should be removed,
    // since the HTML spec says to ignore the span of a colgroup if it
    // has content columns in it.
    nextCol = col->GetNextCol();
    DestroyContext context(PresShell());
    RemoveFrame(context, FrameChildListID::Principal, col);
    col = nextCol;
  }

  // Our next colframe should be an eColContent.  We've removed all the
  // eColAnonymousColGroup colframes, eColAnonymousCol colframes always follow
  // eColContent ones, and eColAnonymousCell colframes only appear in a
  // synthetic colgroup, which never gets AppendFrames() called on it.
  MOZ_ASSERT(!col || col->GetColType() == eColContent,
             "What's going on with our columns?");

  const nsFrameList::Slice& newFrames =
      mFrames.AppendFrames(this, std::move(aFrameList));
  InsertColsReflow(GetStartColumnIndex() + mColCount, newFrames);
}

void nsTableColGroupFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  nsTableColFrame* col = GetFirstColumn();
  nsTableColFrame* nextCol;
  while (col && col->GetColType() == eColAnonymousColGroup) {
    // this colgroup spans one or more columns but now that there is a
    // real column below, spanned anonymous columns should be removed,
    // since the HTML spec says to ignore the span of a colgroup if it
    // has content columns in it.
    nextCol = col->GetNextCol();
    if (col == aPrevFrame) {
      // This can happen when we're being appended to
      NS_ASSERTION(!nextCol || nextCol->GetColType() != eColAnonymousColGroup,
                   "Inserting in the middle of our anonymous cols?");
      // We'll want to insert at the beginning
      aPrevFrame = nullptr;
    }
    DestroyContext context(PresShell());
    RemoveFrame(context, FrameChildListID::Principal, col);
    col = nextCol;
  }

  // Our next colframe should be an eColContent.  We've removed all the
  // eColAnonymousColGroup colframes, eColAnonymousCol colframes always follow
  // eColContent ones, and eColAnonymousCell colframes only appear in a
  // synthetic colgroup, which never gets InsertFrames() called on it.
  MOZ_ASSERT(!col || col->GetColType() == eColContent,
             "What's going on with our columns?");

  NS_ASSERTION(!aPrevFrame || aPrevFrame == aPrevFrame->LastContinuation(),
               "Prev frame should be last in continuation chain");
  NS_ASSERTION(!aPrevFrame || !GetNextColumn(aPrevFrame) ||
                   GetNextColumn(aPrevFrame)->GetColType() != eColAnonymousCol,
               "Shouldn't be inserting before a spanned colframe");

  const nsFrameList::Slice& newFrames =
      mFrames.InsertFrames(this, aPrevFrame, std::move(aFrameList));
  nsIFrame* prevFrame = nsTableFrame::GetFrameAtOrBefore(
      this, aPrevFrame, LayoutFrameType::TableCol);

  int32_t colIndex = (prevFrame)
                         ? ((nsTableColFrame*)prevFrame)->GetColIndex() + 1
                         : GetStartColumnIndex();
  InsertColsReflow(colIndex, newFrames);
}

void nsTableColGroupFrame::InsertColsReflow(int32_t aColIndex,
                                            const nsFrameList::Slice& aCols) {
  AddColsToTable(aColIndex, true, aCols);

  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_HAS_DIRTY_CHILDREN);
}

nsTableColGroupFrame* nsTableColGroupFrame::GetSyntheticColGroup() const {
  return GetTableFrame()->GetSyntheticColGroup();
}

void nsTableColGroupFrame::RemoveChild(DestroyContext& aContext,
                                       nsTableColFrame& aChild,
                                       bool aResetSubsequentColIndices) {
  int32_t colIndex = 0;
  nsIFrame* nextChild = nullptr;
  if (aResetSubsequentColIndices) {
    colIndex = aChild.GetColIndex();
    nextChild = aChild.GetNextSibling();
  }
  mFrames.DestroyFrame(aContext, &aChild);
  mColCount--;
  if (aResetSubsequentColIndices) {
    if (nextChild) {  // reset inside this and all following colgroups
      ResetColIndices(this, GetSyntheticColGroup(), colIndex, nextChild);
    } else if (!IsSynthetic()) {
      ResetColIndices(GetNextSibling(), GetSyntheticColGroup(), colIndex);
    }
  }

  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_HAS_DIRTY_CHILDREN);
}

void nsTableColGroupFrame::RemoveFrame(DestroyContext& aContext,
                                       ChildListID aListID,
                                       nsIFrame* aOldFrame) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");

  if (!aOldFrame) {
    return;
  }
  bool contentRemoval = false;

  if (aOldFrame->IsTableColFrame()) {
    nsTableColFrame* colFrame = (nsTableColFrame*)aOldFrame;
    if (colFrame->GetColType() == eColContent) {
      contentRemoval = true;
      // Remove any anonymous column frames this <col> produced via a colspan
      nsTableColFrame* col = colFrame->GetNextCol();
      nsTableColFrame* nextCol;
      while (col && col->GetColType() == eColAnonymousCol) {
        nextCol = col->GetNextCol();
        RemoveFrame(aContext, FrameChildListID::Principal, col);
        col = nextCol;
      }
    }

    int32_t colIndex = colFrame->GetColIndex();
    // The RemoveChild call handles calling FrameNeedsReflow on us.
    RemoveChild(aContext, *colFrame, true);

    nsTableFrame* tableFrame = GetTableFrame();
    tableFrame->RemoveCol(colIndex, true, true);
    if (mFrames.IsEmpty() && contentRemoval && !IsSynthetic()) {
      tableFrame->AppendAnonymousColFrames(this, GetSpan(),
                                           eColAnonymousColGroup, true);
    }
  } else {
    mFrames.DestroyFrame(aContext, aOldFrame);
  }
}

nsIFrame::LogicalSides nsTableColGroupFrame::GetLogicalSkipSides() const {
  LogicalSides skip(mWritingMode);
  if (MOZ_UNLIKELY(StyleBorder()->mBoxDecorationBreak ==
                   StyleBoxDecorationBreak::Clone)) {
    return skip;
  }

  if (GetPrevInFlow()) {
    skip += LogicalSide::BStart;
  }
  if (GetNextInFlow()) {
    skip += LogicalSide::BEnd;
  }
  return skip;
}

void nsTableColGroupFrame::Reflow(nsPresContext* aPresContext,
                                  ReflowOutput& aDesiredSize,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTableColGroupFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_ASSERTION(nullptr != mContent, "bad state -- null content for frame");
  if (StyleVisibility()->mVisible == StyleVisibility::Collapse) {
    GetTableFrame()->SetNeedToCollapse(true);
  }

  const WritingMode wm = GetWritingMode();
  for (nsIFrame* kidFrame : mFrames) {
    // Give the child frame a chance to reflow, even though we know it'll have 0
    // size
    ReflowOutput kidSize(aReflowInput);
    ReflowInput kidReflowInput(aPresContext, aReflowInput, kidFrame,
                               LogicalSize(kidFrame->GetWritingMode()));
    const LogicalPoint dummyPos(wm);
    const nsSize dummyContainerSize;
    nsReflowStatus status;
    ReflowChild(kidFrame, aPresContext, kidSize, kidReflowInput, wm, dummyPos,
                dummyContainerSize, ReflowChildFlags::Default, status);
    FinishReflowChild(kidFrame, aPresContext, kidSize, &kidReflowInput, wm,
                      dummyPos, dummyContainerSize, ReflowChildFlags::Default);
  }

  aDesiredSize.ClearSize();
}

void nsTableColGroupFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                            const nsDisplayListSet& aLists) {
  // Per https://drafts.csswg.org/css-tables-3/#global-style-overrides:
  // "All css properties of table-column and table-column-group boxes are
  // ignored, except when explicitly specified by this specification."
  // CSS outlines and box-shadows fall into this category, so we skip them
  // on these boxes. Colgroup backgrounds are drawn by nsTableFrame.
}

nsTableColFrame* nsTableColGroupFrame::GetFirstColumn() {
  return GetNextColumn(nullptr);
}

nsTableColFrame* nsTableColGroupFrame::GetNextColumn(nsIFrame* aChildFrame) {
  nsTableColFrame* result = nullptr;
  nsIFrame* childFrame = aChildFrame;
  if (!childFrame) {
    childFrame = mFrames.FirstChild();
  } else {
    childFrame = childFrame->GetNextSibling();
  }
  while (childFrame) {
    if (mozilla::StyleDisplay::TableColumn ==
        childFrame->StyleDisplay()->mDisplay) {
      result = (nsTableColFrame*)childFrame;
      break;
    }
    childFrame = childFrame->GetNextSibling();
  }
  return result;
}

int32_t nsTableColGroupFrame::GetSpan() { return StyleTable()->mXSpan; }

/* ----- global methods ----- */

nsTableColGroupFrame* NS_NewTableColGroupFrame(PresShell* aPresShell,
                                               ComputedStyle* aStyle) {
  return new nsTableColGroupFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTableColGroupFrame)
NS_QUERYFRAME_HEAD(nsTableColGroupFrame)
  NS_QUERYFRAME_ENTRY(nsTableColGroupFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

void nsTableColGroupFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                                           bool aRebuildDisplayItems) {
  nsIFrame::InvalidateFrame(aDisplayItemKey, aRebuildDisplayItems);
  if (GetTableFrame()->IsBorderCollapse()) {
    const bool rebuild = StaticPrefs::layout_display_list_retain_sc();
    GetParent()->InvalidateFrameWithRect(InkOverflowRect() + GetPosition(),
                                         aDisplayItemKey, rebuild);
  }
}

void nsTableColGroupFrame::InvalidateFrameWithRect(const nsRect& aRect,
                                                   uint32_t aDisplayItemKey,
                                                   bool aRebuildDisplayItems) {
  nsIFrame::InvalidateFrameWithRect(aRect, aDisplayItemKey,
                                    aRebuildDisplayItems);
  // If we have filters applied that would affects our bounds, then
  // we get an inactive layer created and this is computed
  // within FrameLayerBuilder
  GetParent()->InvalidateFrameWithRect(aRect + GetPosition(), aDisplayItemKey,
                                       aRebuildDisplayItems);
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsTableColGroupFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"TableColGroup"_ns, aResult);
}

void nsTableColGroupFrame::Dump(int32_t aIndent) {
  char* indent = new char[aIndent + 1];
  if (!indent) {
    return;
  }
  for (int32_t i = 0; i < aIndent + 1; i++) {
    indent[i] = ' ';
  }
  indent[aIndent] = 0;

  printf(
      "%s**START COLGROUP DUMP**\n%s startcolIndex=%d  colcount=%d span=%d "
      "isSynthetic=%s",
      indent, indent, GetStartColumnIndex(), GetColCount(), GetSpan(),
      IsSynthetic() ? "true" : "false");

  // verify the colindices
  DebugOnly<int32_t> j = GetStartColumnIndex();
  nsTableColFrame* col = GetFirstColumn();
  while (col) {
    NS_ASSERTION(j == col->GetColIndex(), "wrong colindex on col frame");
    col = col->GetNextCol();
    j++;
  }
  NS_ASSERTION((j - GetStartColumnIndex()) == GetColCount(),
               "number of cols out of sync");
  printf("\n%s**END COLGROUP DUMP** ", indent);
  delete[] indent;
}
#endif
