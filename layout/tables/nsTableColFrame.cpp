/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsTableColFrame.h"

#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsCOMPtr.h"
#include "nsCSSRendering.h"
#include "nsContainerFrame.h"
#include "nsIContent.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsTableFrame.h"

using namespace mozilla;

#define COL_TYPE_BITS                                                         \
  (NS_FRAME_STATE_BIT(28) | NS_FRAME_STATE_BIT(29) | NS_FRAME_STATE_BIT(30) | \
   NS_FRAME_STATE_BIT(31))
#define COL_TYPE_OFFSET 28

using namespace mozilla;

nsTableColFrame::nsTableColFrame(ComputedStyle* aStyle,
                                 nsPresContext* aPresContext)
    : nsSplittableFrame(aStyle, aPresContext, kClassID),
      mMinCoord(0),
      mPrefCoord(0),
      mSpanMinCoord(0),
      mSpanPrefCoord(0),
      mPrefPercent(0.0f),
      mSpanPrefPercent(0.0f),
      mFinalISize(0),
      mColIndex(0),
      mIStartBorderWidth(0),
      mIEndBorderWidth(0),
      mHasSpecifiedCoord(false) {
  SetColType(eColContent);
  ResetIntrinsics();
  ResetSpanIntrinsics();
  ResetFinalISize();
}

nsTableColFrame::~nsTableColFrame() = default;

nsTableColType nsTableColFrame::GetColType() const {
  return (nsTableColType)((GetStateBits() & COL_TYPE_BITS) >> COL_TYPE_OFFSET);
}

void nsTableColFrame::SetColType(nsTableColType aType) {
  NS_ASSERTION(aType != eColAnonymousCol ||
                   (GetPrevContinuation() &&
                    GetPrevContinuation()->GetNextContinuation() == this &&
                    GetPrevContinuation()->GetNextSibling() == this),
               "spanned content cols must be continuations");
  uint32_t type = aType - eColContent;
  RemoveStateBits(COL_TYPE_BITS);
  AddStateBits(nsFrameState(type << COL_TYPE_OFFSET));
}

/* virtual */
void nsTableColFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  nsSplittableFrame::DidSetComputedStyle(aOldComputedStyle);

  if (!aOldComputedStyle) {  // avoid this on init
    return;
  }

  nsTableFrame* tableFrame = GetTableFrame();
  if (tableFrame->IsBorderCollapse() &&
      tableFrame->BCRecalcNeeded(aOldComputedStyle, Style())) {
    TableArea damageArea(GetColIndex(), 0, 1, tableFrame->GetRowCount());
    tableFrame->AddBCDamageArea(damageArea);
  }
}

void nsTableColFrame::Reflow(nsPresContext* aPresContext,
                             ReflowOutput& aDesiredSize,
                             const ReflowInput& aReflowInput,
                             nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTableColFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  aDesiredSize.ClearSize();
  if (StyleVisibility()->mVisible == StyleVisibility::Collapse) {
    GetTableFrame()->SetNeedToCollapse(true);
  }
}

void nsTableColFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  // Per https://drafts.csswg.org/css-tables-3/#global-style-overrides:
  // "All css properties of table-column and table-column-group boxes are
  // ignored, except when explicitly specified by this specification."
  // CSS outlines and box-shadows fall into this category, so we skip them
  // on these boxes.
  MOZ_ASSERT_UNREACHABLE("Cols don't paint themselves");
}

int32_t nsTableColFrame::GetSpan() { return StyleTable()->mXSpan; }

#ifdef DEBUG
void nsTableColFrame::Dump(int32_t aIndent) {
  char* indent = new char[aIndent + 1];
  if (!indent) return;
  for (int32_t i = 0; i < aIndent + 1; i++) {
    indent[i] = ' ';
  }
  indent[aIndent] = 0;

  printf("%s**START COL DUMP**\n%s colIndex=%d coltype=", indent, indent,
         mColIndex);
  nsTableColType colType = GetColType();
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
  printf("\nm:%d c:%d(%c) p:%f sm:%d sc:%d sp:%f f:%d", int32_t(mMinCoord),
         int32_t(mPrefCoord), mHasSpecifiedCoord ? 's' : 'u', mPrefPercent,
         int32_t(mSpanMinCoord), int32_t(mSpanPrefCoord), mSpanPrefPercent,
         int32_t(GetFinalISize()));
  printf("\n%s**END COL DUMP** ", indent);
  delete[] indent;
}
#endif
/* ----- global methods ----- */

nsTableColFrame* NS_NewTableColFrame(PresShell* aPresShell,
                                     ComputedStyle* aStyle) {
  return new nsTableColFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsTableColFrame)

nsTableColFrame* nsTableColFrame::GetNextCol() const {
  nsIFrame* childFrame = GetNextSibling();
  while (childFrame) {
    if (childFrame->IsTableColFrame()) {
      return (nsTableColFrame*)childFrame;
    }
    childFrame = childFrame->GetNextSibling();
  }
  return nullptr;
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsTableColFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"TableCol"_ns, aResult);
}
#endif

void nsTableColFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                                      bool aRebuildDisplayItems) {
  nsIFrame::InvalidateFrame(aDisplayItemKey, aRebuildDisplayItems);
  if (GetTableFrame()->IsBorderCollapse()) {
    const bool rebuild = StaticPrefs::layout_display_list_retain_sc();
    GetParent()->InvalidateFrameWithRect(InkOverflowRect() + GetPosition(),
                                         aDisplayItemKey, rebuild);
  }
}

void nsTableColFrame::InvalidateFrameWithRect(const nsRect& aRect,
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
