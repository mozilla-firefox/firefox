/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTableCellFrame.h"

#include <algorithm>

#include "celldata.h"
#include "gfxContext.h"
#include "gfxUtils.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "nsAttrValueInlines.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsHTMLParts.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsTableColFrame.h"
#include "nsTableFrame.h"
#include "nsTableRowFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsTextFrame.h"

// TABLECELL SELECTION
#include "mozilla/LookAndFeel.h"
#include "nsFrameSelection.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

nsTableCellFrame::nsTableCellFrame(ComputedStyle* aStyle,
                                   nsTableFrame* aTableFrame, ClassID aID)
    : nsContainerFrame(aStyle, aTableFrame->PresContext(), aID),
      mDesiredSize(aTableFrame->GetWritingMode()) {
  SetContentEmpty(false);
}

nsTableCellFrame::~nsTableCellFrame() = default;

NS_IMPL_FRAMEARENA_HELPERS(nsTableCellFrame)

void nsTableCellFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                            nsIFrame* aPrevInFlow) {
  // Let the base class do its initialization
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER)) {
    AddStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT);
  }

  if (aPrevInFlow) {
    // Set the column index
    nsTableCellFrame* cellFrame = (nsTableCellFrame*)aPrevInFlow;
    mColIndex = cellFrame->mColIndex;
  } else {
    // Although the spec doesn't say that writing-mode is not applied to
    // table-cells, we still override style value here because we want to
    // make effective writing mode of table structure frames consistent
    // within a table. The content inside table cells is reflowed by an
    // anonymous block, hence their writing mode is not affected.
    mWritingMode = GetTableFrame()->GetWritingMode();
  }
}

void nsTableCellFrame::Destroy(DestroyContext& aContext) {
  nsTableFrame::MaybeUnregisterPositionedTablePart(this);
  nsContainerFrame::Destroy(aContext);
}

// nsIPercentBSizeObserver methods

void nsTableCellFrame::NotifyPercentBSize(const ReflowInput& aReflowInput) {
  // ReflowInput ensures the mCBReflowInput of blocks inside a
  // cell is the cell frame, not the inner-cell block, and that the
  // containing block of an inner table is the containing block of its
  // table wrapper.
  // XXXldb Given the now-stricter |NeedsToObserve|, many if not all of
  // these tests are probably unnecessary.

  // Maybe the cell reflow input; we sure if we're inside the |if|.
  const ReflowInput* cellRI = aReflowInput.mCBReflowInput;

  if (cellRI && cellRI->mFrame == this &&
      (cellRI->ComputedBSize() == NS_UNCONSTRAINEDSIZE ||
       cellRI->ComputedBSize() == 0)) {  // XXXldb Why 0?
    // This is a percentage bsize on a frame whose percentage bsizes
    // are based on the bsize of the cell, since its containing block
    // is the inner cell frame.

    // We'll only honor the percent bsize if sibling-cells/ancestors
    // have specified/pct bsize. (Also, siblings only count for this if
    // both this cell and the sibling cell span exactly 1 row.)

    if (nsTableFrame::AncestorsHaveStyleBSize(*cellRI) ||
        (GetTableFrame()->GetEffectiveRowSpan(*this) == 1 &&
         cellRI->mParentReflowInput->mFrame->HasAnyStateBits(
             NS_ROW_HAS_CELL_WITH_STYLE_BSIZE))) {
      for (const ReflowInput* rs = aReflowInput.mParentReflowInput;
           rs != cellRI; rs = rs->mParentReflowInput) {
        rs->mFrame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
      }

      nsTableFrame::RequestSpecialBSizeReflow(*cellRI);
    }
  }
}

// The cell needs to observe its block and things inside its block but nothing
// below that
bool nsTableCellFrame::NeedsToObserve(const ReflowInput& aReflowInput) {
  const ReflowInput* rs = aReflowInput.mParentReflowInput;
  if (!rs) {
    return false;
  }
  if (rs->mFrame == this) {
    // We always observe the child block.  It will never send any
    // notifications, but we need this so that the observer gets
    // propagated to its kids.
    return true;
  }
  rs = rs->mParentReflowInput;
  if (!rs) {
    return false;
  }

  // We always need to let the percent bsize observer be propagated
  // from a table wrapper frame to an inner table frame.
  LayoutFrameType fType = aReflowInput.mFrame->Type();
  if (fType == LayoutFrameType::Table) {
    return true;
  }

  // We need the observer to be propagated to all children of the cell
  // (i.e., children of the child block) in quirks mode, but only to
  // tables in standards mode.
  // XXX This may not be true in the case of orthogonal flows within
  // the cell (bug 1174711 comment 8); we may need to observe isizes
  // instead of bsizes for orthogonal children.
  return rs->mFrame == this &&
         (PresContext()->CompatibilityMode() == eCompatibility_NavQuirks ||
          fType == LayoutFrameType::TableWrapper);
}

nsresult nsTableCellFrame::AttributeChanged(int32_t aNameSpaceID,
                                            nsAtom* aAttribute, AttrModType) {
  // We need to recalculate in this case because of the nowrap quirk in
  // BasicTableLayoutStrategy
  if (aNameSpaceID == kNameSpaceID_None && aAttribute == nsGkAtoms::nowrap &&
      PresContext()->CompatibilityMode() == eCompatibility_NavQuirks) {
    PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                  NS_FRAME_IS_DIRTY);
  }

  const nsAtom* colSpanAttribute =
      MOZ_UNLIKELY(mContent->AsElement()->IsMathMLElement())
          ? nsGkAtoms::columnspan
          : nsGkAtoms::colspan;
  if (aAttribute == nsGkAtoms::rowspan || aAttribute == colSpanAttribute) {
    nsLayoutUtils::PostRestyleEvent(mContent->AsElement(), RestyleHint{0},
                                    nsChangeHint_UpdateTableCellSpans);
  }
  return NS_OK;
}

/* virtual */
void nsTableCellFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldComputedStyle);
  nsTableFrame::PositionedTablePartMaybeChanged(this, aOldComputedStyle);

  if (!aOldComputedStyle) {
    return;  // avoid the following on init
  }

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    if (StyleBorder()->GetComputedBorder() !=
        aOldComputedStyle->StyleBorder()->GetComputedBorder()) {
      // If a table cell's computed border changes, it can change whether or
      // not its parent table is classified as a layout or data table. We
      // send a notification here to invalidate the a11y cache on the table
      // so the next fetch of IsProbablyLayoutTable() is accurate.
      accService->TableLayoutGuessMaybeChanged(PresShell(), mContent);
    }
  }
#endif

  nsTableFrame* tableFrame = GetTableFrame();
  if (tableFrame->IsBorderCollapse() &&
      tableFrame->BCRecalcNeeded(aOldComputedStyle, Style())) {
    uint32_t colIndex = ColIndex();
    uint32_t rowIndex = RowIndex();
    // row span needs to be clamped as we do not create rows in the cellmap
    // which do not have cells originating in them
    // col span needs to be clamped as the cellmap may not reflect it yet
    TableArea damageArea(colIndex, rowIndex,
                         std::min(static_cast<uint32_t>(GetColSpan()),
                                  tableFrame->GetColCount() - colIndex),
                         std::min(static_cast<uint32_t>(GetRowSpan()),
                                  tableFrame->GetRowCount() - rowIndex));
    tableFrame->AddBCDamageArea(damageArea);
  }
}

#ifdef DEBUG
void nsTableCellFrame::AppendFrames(ChildListID aListID,
                                    nsFrameList&& aFrameList) {
  MOZ_CRASH("unsupported operation");
}

void nsTableCellFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                    const nsLineList::iterator* aPrevFrameLine,
                                    nsFrameList&& aFrameList) {
  MOZ_CRASH("unsupported operation");
}

void nsTableCellFrame::RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) {
  MOZ_CRASH("unsupported operation");
}
#endif

void nsTableCellFrame::SetColIndex(int32_t aColIndex) {
  MOZ_ASSERT(!GetPrevContinuation());
  mColIndex = aColIndex;
  // Keep our continuations in sync. Cells can be reindexed dynamically (e.g.
  // when rows are removed), and all continuations should agree on the column
  // index.
  for (nsIFrame* cont = GetNextContinuation(); cont;
       cont = cont->GetNextContinuation()) {
    static_cast<nsTableCellFrame*>(cont)->mColIndex = aColIndex;
  }
}

/* virtual */
nsMargin nsTableCellFrame::GetUsedMargin() const {
  return nsMargin(0, 0, 0, 0);
}

// ASSURE DIFFERENT COLORS for selection
inline nscolor EnsureDifferentColors(nscolor colorA, nscolor colorB) {
  if (colorA == colorB) {
    nscolor res;
    res = NS_RGB(NS_GET_R(colorA) ^ 0xff, NS_GET_G(colorA) ^ 0xff,
                 NS_GET_B(colorA) ^ 0xff);
    return res;
  }
  return colorA;
}

void nsTableCellFrame::DecorateForSelection(DrawTarget* aDrawTarget,
                                            nsPoint aPt) {
  NS_ASSERTION(IsSelected(), "Should only be called for selected cells");
  if (!IsSelectable()) {
    return;
  }
  RefPtr<nsFrameSelection> frameSelection = PresShell()->FrameSelection();
  if (!frameSelection->IsInTableSelectionMode()) {
    return;
  }
  nscoord threePx = nsPresContext::CSSPixelsToAppUnits(3);
  if (mRect.width <= threePx || mRect.height <= threePx) {
    return;
  }
  nscolor bordercolor;
  if (frameSelection->GetDisplaySelection() ==
      nsISelectionController::SELECTION_DISABLED) {
    bordercolor = NS_RGB(176, 176, 176);  // disabled color
  } else {
    bordercolor = LookAndFeel::Color(LookAndFeel::ColorID::Highlight, this);
  }
  // compare bordercolor to background-color
  bordercolor = EnsureDifferentColors(bordercolor,
                                      StyleBackground()->BackgroundColor(this));

  int32_t appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
  Point devPixelOffset = NSPointToPoint(aPt, appUnitsPerDevPixel);

  AutoRestoreTransform autoRestoreTransform(aDrawTarget);
  aDrawTarget->SetTransform(
      aDrawTarget->GetTransform().PreTranslate(devPixelOffset));

  ColorPattern color(ToDeviceColor(bordercolor));

  nscoord onePixel = nsPresContext::CSSPixelsToAppUnits(1);

  StrokeLineWithSnapping(nsPoint(onePixel, 0), nsPoint(mRect.width, 0),
                         appUnitsPerDevPixel, *aDrawTarget, color);
  StrokeLineWithSnapping(nsPoint(0, onePixel), nsPoint(0, mRect.height),
                         appUnitsPerDevPixel, *aDrawTarget, color);
  StrokeLineWithSnapping(nsPoint(onePixel, mRect.height),
                         nsPoint(mRect.width, mRect.height),
                         appUnitsPerDevPixel, *aDrawTarget, color);
  StrokeLineWithSnapping(nsPoint(mRect.width, onePixel),
                         nsPoint(mRect.width, mRect.height),
                         appUnitsPerDevPixel, *aDrawTarget, color);
  // middle
  nsRect r(onePixel, onePixel, mRect.width - onePixel, mRect.height - onePixel);
  Rect devPixelRect = NSRectToSnappedRect(r, appUnitsPerDevPixel, *aDrawTarget);
  aDrawTarget->StrokeRect(devPixelRect, color);
  // shading
  StrokeLineWithSnapping(
      nsPoint(2 * onePixel, mRect.height - 2 * onePixel),
      nsPoint(mRect.width - onePixel, mRect.height - (2 * onePixel)),
      appUnitsPerDevPixel, *aDrawTarget, color);
  StrokeLineWithSnapping(
      nsPoint(mRect.width - (2 * onePixel), 2 * onePixel),
      nsPoint(mRect.width - (2 * onePixel), mRect.height - onePixel),
      appUnitsPerDevPixel, *aDrawTarget, color);
}

void nsTableCellFrame::ProcessBorders(nsTableFrame* aFrame,
                                      nsDisplayListBuilder* aBuilder,
                                      const nsDisplayListSet& aLists) {
  const nsStyleBorder* borderStyle = StyleBorder();
  if (aFrame->IsBorderCollapse() || !borderStyle->HasBorder()) {
    return;
  }

  if (!GetContentEmpty() ||
      StyleTableBorder()->mEmptyCells == StyleEmptyCells::Show) {
    aLists.BorderBackground()->AppendNewToTop<nsDisplayBorder>(aBuilder, this);
  }
}

void nsTableCellFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                                       bool aRebuildDisplayItems) {
  nsIFrame::InvalidateFrame(aDisplayItemKey, aRebuildDisplayItems);
  if (GetTableFrame()->IsBorderCollapse()) {
    const bool rebuild = StaticPrefs::layout_display_list_retain_sc();
    GetParent()->InvalidateFrameWithRect(InkOverflowRect() + GetPosition(),
                                         aDisplayItemKey, rebuild);
  }
}

void nsTableCellFrame::InvalidateFrameWithRect(const nsRect& aRect,
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

bool nsTableCellFrame::ShouldPaintBordersAndBackgrounds() const {
  // If we're not visible, we don't paint.
  if (!StyleVisibility()->IsVisible()) {
    return false;
  }

  // Consider 'empty-cells', but only in separated borders mode.
  if (!GetContentEmpty()) {
    return true;
  }

  nsTableFrame* tableFrame = GetTableFrame();
  if (tableFrame->IsBorderCollapse()) {
    return true;
  }

  return StyleTableBorder()->mEmptyCells == StyleEmptyCells::Show;
}

bool nsTableCellFrame::ShouldPaintBackground(nsDisplayListBuilder* aBuilder) {
  return ShouldPaintBordersAndBackgrounds();
}

LogicalSides nsTableCellFrame::GetLogicalSkipSides() const {
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

/* virtual */
nsMargin nsTableCellFrame::GetBorderOverflow() { return nsMargin(0, 0, 0, 0); }

void nsTableCellFrame::AlignChildWithinCell(
    nscoord aMaxAscent, ForceAlignTopForTableCell aForceAlignTop) {
  MOZ_ASSERT(aForceAlignTop != ForceAlignTopForTableCell::Yes ||
                 PresContext()->IsPaginated(),
             "We shouldn't force table-cells to do top alignment if "
             "we're not in printing!");

  nsIFrame* const inner = Inner();
  const WritingMode tableWM = GetWritingMode();
  const WritingMode innerWM = inner->GetWritingMode();

  // The anonymous block child is to be placed within the cell's padding rect.
  // Get it in the inner frame's writing mode for alignment calculation.
  const nsSize containerSize = mRect.Size();
  const LogicalRect paddingRect(innerWM, GetPaddingRectRelativeToSelf(),
                                containerSize);

  const LogicalRect kidRect = inner->GetLogicalRect(innerWM, containerSize);

  // Calculate the position for the inner frame, initializing to the origin.
  LogicalPoint kidPosition = paddingRect.Origin(innerWM);

  // Apply table cell alignment to the block coordinate.
  const auto alignment = aForceAlignTop == ForceAlignTopForTableCell::Yes
                             ? TableCellAlignment::Top
                             : GetTableCellAlignment();
  switch (alignment) {
    case TableCellAlignment::Baseline:
      if (auto baseline = GetCellBaseline()) {
        // Align the baseline of the child frame with the baselines of other
        // children in the same row which have baseline alignment
        kidPosition.B(innerWM) =
            paddingRect.BStart(innerWM) + aMaxAscent - *baseline;
        break;
      }
      // fallback to start alignment
      [[fallthrough]];
    case TableCellAlignment::Top:
      // Leave kidPosition at the origin: the child frame will be aligned
      // with the padding rect's block-start.
      break;

    case TableCellAlignment::Bottom:
      // Align the block-end of the child frame with the block-end of the
      // padding rect.
      kidPosition.B(innerWM) =
          paddingRect.BEnd(innerWM) - kidRect.BSize(innerWM);
      break;

    default:
    case TableCellAlignment::Middle:
      // Align the middle of the child frame with the middle of the cell's
      // padding rect.
      kidPosition.B(innerWM) =
          paddingRect.BStart(innerWM) +
          (paddingRect.BSize(innerWM) - kidRect.BSize(innerWM)) / 2;
  }

  // If the content is larger than the cell bSize, align from the padding-rect's
  // bStart edge.
  kidPosition.B(innerWM) =
      std::max(paddingRect.BStart(innerWM), kidPosition.B(innerWM));

  if (kidPosition != kidRect.Origin(innerWM)) {
    // If we're moving the inner frame, invalidate at the old position first.
    inner->InvalidateFrameSubtree();
  }

  inner->SetPosition(innerWM, kidPosition, containerSize);

  ReflowOutput reflowOutput(tableWM);
  reflowOutput.SetSize(tableWM, GetLogicalSize(tableWM));

  nsRect overflow(nsPoint(), GetSize());
  overflow.Inflate(GetBorderOverflow());
  reflowOutput.mOverflowAreas.SetAllTo(overflow);
  ConsiderChildOverflow(reflowOutput.mOverflowAreas, inner);
  FinishAndStoreOverflow(&reflowOutput);

  if (kidPosition != kidRect.Origin(innerWM)) {
    // Invalidate new overflow rect.
    inner->InvalidateFrameSubtree();
  }
}

bool nsTableCellFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  nsRect bounds(nsPoint(0, 0), GetSize());
  bounds.Inflate(GetBorderOverflow());

  aOverflowAreas.UnionAllWith(bounds);
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

// Per CSS 2.1, we map 'sub', 'super', 'text-top', 'text-bottom',
// length, percentage, and calc() values to 'baseline'.
TableCellAlignment nsTableCellFrame::GetTableCellAlignment() const {
  const auto& baselineShift = StyleDisplay()->mBaselineShift;
  if (baselineShift.IsKeyword()) {
    auto value = baselineShift.AsKeyword();
    switch (value) {
      case StyleBaselineShiftKeyword::Top:
        return TableCellAlignment::Top;
      case StyleBaselineShiftKeyword::Bottom:
        return TableCellAlignment::Bottom;
      default:
        break;
    }
  }

  const auto& alignmentBaseline = StyleDisplay()->mAlignmentBaseline;
  if (alignmentBaseline == StyleAlignmentBaseline::Middle) {
    return TableCellAlignment::Middle;
  }

  return TableCellAlignment::Baseline;
}

static bool CellHasVisibleContent(nsTableFrame* aTableFrame,
                                  nsTableCellFrame* aCell) {
  // see  http://www.w3.org/TR/CSS21/tables.html#empty-cells
  nsIFrame* content = aCell->CellContentFrame();
  if (content->GetContentRect().Height() > 0) {
    return true;
  }
  if (aTableFrame->IsBorderCollapse()) {
    return true;
  }
  for (nsIFrame* innerFrame : content->PrincipalChildList()) {
    LayoutFrameType frameType = innerFrame->Type();
    if (LayoutFrameType::Text == frameType) {
      nsTextFrame* textFrame = static_cast<nsTextFrame*>(innerFrame);
      if (textFrame->HasNoncollapsedCharacters()) {
        return true;
      }
    } else if (LayoutFrameType::Placeholder != frameType) {
      return true;
    } else if (nsLayoutUtils::GetFloatFromPlaceholder(innerFrame)) {
      return true;
    }
  }
  return false;
}

nsIFrame* nsTableCellFrame::Inner() const {
  MOZ_ASSERT(mFrames.OnlyChild(),
             "A table cell should have exactly one child!");
  return mFrames.FirstChild();
}

nsIFrame* nsTableCellFrame::CellContentFrame() const {
  nsIFrame* inner = Inner();
  if (ScrollContainerFrame* sf = do_QueryFrame(inner)) {
    return sf->GetScrolledFrame();
  }
  return inner;
}

Maybe<nscoord> nsTableCellFrame::GetCellBaseline() const {
  // Empty cells don't participate in baseline alignment - fallback to
  // start alignment.
  if (GetContentEmpty()) {
    return {};
  }
  // Ignore the position of the inner frame relative to the cell frame
  // since we want the position as though the inner were top-aligned.
  const auto wm = GetWritingMode();
  nscoord result;
  if (StyleDisplay()->IsContainLayout() ||
      !nsLayoutUtils::GetFirstLineBaseline(wm, Inner(), &result)) {
    // Synthesize a baseline from our content box, see bug 1591219.
    return Some(CellContentFrame()->ContentBSize(wm) +
                GetLogicalUsedBorderAndPadding(wm).BStart(wm));
  }
  // `result` already includes the padding-start from the inner frame.
  return Some(result + GetLogicalUsedBorder(wm).BStart(wm));
}

int32_t nsTableCellFrame::GetRowSpan() {
  int32_t rowSpan = 1;

  // Don't look at the content's rowspan if we're a pseudo cell
  if (!Style()->IsPseudoOrAnonBox()) {
    dom::Element* elem = mContent->AsElement();
    const nsAttrValue* attr = elem->GetParsedAttr(nsGkAtoms::rowspan);
    // Note that we don't need to check the tag name, because only table cells
    // (including MathML <mtd>) and table headers parse the "rowspan" attribute
    // into an integer.
    if (attr && attr->Type() == nsAttrValue::eInteger) {
      rowSpan = attr->GetIntegerValue();
    }
  }
  return rowSpan;
}

int32_t nsTableCellFrame::GetColSpan() {
  int32_t colSpan = 1;

  // Don't look at the content's colspan if we're a pseudo cell
  if (!Style()->IsPseudoOrAnonBox()) {
    dom::Element* elem = mContent->AsElement();
    const nsAttrValue* attr = elem->GetParsedAttr(
        MOZ_UNLIKELY(elem->IsMathMLElement()) ? nsGkAtoms::columnspan
                                              : nsGkAtoms::colspan);
    // Note that we don't need to check the tag name, because only table cells
    // (including MathML <mtd>) and table headers parse the "colspan" attribute
    // into an integer.
    if (attr && attr->Type() == nsAttrValue::eInteger) {
      colSpan = attr->GetIntegerValue();
    }
  }
  return colSpan;
}

ScrollContainerFrame* nsTableCellFrame::GetScrollTargetFrame() const {
  return do_QueryFrame(Inner());
}

nscoord nsTableCellFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                         IntrinsicISizeType aType) {
  // Note: a table cell has the same writing mode as its table ancestor, which
  // may differ from its inner frame that derives its writing mode from the
  // style of the <td> element. See nsTableCellFrame::Init().
  const IntrinsicSizeInput innerInput(aInput, Inner()->GetWritingMode(),
                                      GetWritingMode());
  return nsLayoutUtils::IntrinsicForContainer(
      innerInput.mContext, Inner(), aType,
      innerInput.mPercentageBasisForChildren, nsLayoutUtils::IGNORE_PADDING);
}

/* virtual */ nsIFrame::IntrinsicSizeOffsetData
nsTableCellFrame::IntrinsicISizeOffsets(nscoord aPercentageBasis) {
  IntrinsicSizeOffsetData result =
      nsContainerFrame::IntrinsicISizeOffsets(aPercentageBasis);

  result.margin = 0;

  WritingMode wm = GetWritingMode();
  result.border = GetBorderWidth(wm).IStartEnd(wm);

  return result;
}

#ifdef DEBUG
#  define PROBABLY_TOO_LARGE 1000000
static void DebugCheckChildSize(nsIFrame* aChild, ReflowOutput& aMet) {
  WritingMode wm = aMet.GetWritingMode();
  if ((aMet.ISize(wm) < 0) || (aMet.ISize(wm) > PROBABLY_TOO_LARGE)) {
    printf("WARNING: cell content %p has large inline size %d \n",
           static_cast<void*>(aChild), int32_t(aMet.ISize(wm)));
  }
}
#endif

// the computed bsize for the cell, which descendants use for percent bsize
// calculations it is the bsize (minus border, padding) of the cell's first in
// flow during its final reflow without an unconstrained bsize.
static nscoord CalcUnpaginatedBSize(nsTableCellFrame& aCellFrame,
                                    nsTableFrame& aTableFrame,
                                    nscoord aBlockDirBorderPadding) {
  const nsTableCellFrame* firstCellInFlow =
      static_cast<nsTableCellFrame*>(aCellFrame.FirstInFlow());
  nsTableFrame* firstTableInFlow =
      static_cast<nsTableFrame*>(aTableFrame.FirstInFlow());
  nsTableRowFrame* row =
      static_cast<nsTableRowFrame*>(firstCellInFlow->GetParent());
  nsTableRowGroupFrame* firstRGInFlow =
      static_cast<nsTableRowGroupFrame*>(row->GetParent());

  uint32_t rowIndex = firstCellInFlow->RowIndex();
  int32_t rowSpan = aTableFrame.GetEffectiveRowSpan(*firstCellInFlow);

  nscoord computedBSize =
      firstTableInFlow->GetRowSpacing(rowIndex, rowIndex + rowSpan - 1);
  computedBSize -= aBlockDirBorderPadding;
  uint32_t rowX;
  for (row = firstRGInFlow->GetFirstRow(), rowX = 0; row;
       row = row->GetNextRowAcrossFragments(), rowX++) {
    if (rowX > rowIndex + rowSpan - 1) {
      break;
    } else if (rowX >= rowIndex) {
      computedBSize += row->GetUnpaginatedBSize();
    }
  }
  return computedBSize;
}

void nsTableCellFrame::Reflow(nsPresContext* aPresContext,
                              ReflowOutput& aDesiredSize,
                              const ReflowInput& aReflowInput,
                              nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsTableCellFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  if (aReflowInput.mFlags.mSpecialBSizeReflow) {
    FirstInFlow()->AddStateBits(NS_TABLE_CELL_HAD_SPECIAL_REFLOW);
  }

  // see if a special bsize reflow needs to occur due to having a pct height
  nsTableFrame::CheckRequestSpecialBSizeReflow(aReflowInput);

  WritingMode wm = aReflowInput.GetWritingMode();
  LogicalSize availSize = aReflowInput.AvailableSize();

  // @note |this| frame applies borders but not any padding.  Our anonymous
  // inner frame applies the padding (but not borders).
  LogicalMargin border = GetBorderWidth(wm);

  ReflowOutput kidSize(wm);
  SetPriorAvailISize(aReflowInput.AvailableISize());
  nsIFrame* inner = Inner();
  nsTableFrame* tableFrame = GetTableFrame();

  if (aReflowInput.mFlags.mSpecialBSizeReflow || aPresContext->IsPaginated()) {
    // Here, we're changing our own reflow input, so we need to account for our
    // padding, even though we don't apply it anywhere else, to get the correct
    // percentage resolution on children.
    const LogicalMargin bp = border + aReflowInput.ComputedLogicalPadding(wm);
    if (aReflowInput.mFlags.mSpecialBSizeReflow) {
      const_cast<ReflowInput&>(aReflowInput)
          .SetComputedBSize(BSize(wm) - bp.BStartEnd(wm));
    } else {
      const nscoord computedUnpaginatedBSize =
          CalcUnpaginatedBSize(*this, *tableFrame, bp.BStartEnd(wm));
      if (computedUnpaginatedBSize > 0) {
        const_cast<ReflowInput&>(aReflowInput)
            .SetComputedBSize(computedUnpaginatedBSize);
      }
    }
  }

  // We need to apply the skip sides for current fragmentainer's border after
  // we finish calculating the special block-size or unpaginated block-size to
  // prevent the skip sides from affecting the results.
  //
  // We assume we are the last fragment by using
  // PreReflowBlockLevelLogicalSkipSides(), i.e. the block-end border and
  // padding is not skipped.
  border.ApplySkipSides(PreReflowBlockLevelLogicalSkipSides());

  availSize.ISize(wm) -= border.IStartEnd(wm);

  // If we have a constrained available block-size, shrink it by subtracting our
  // block-direction border and padding for our children.
  if (NS_UNCONSTRAINEDSIZE != availSize.BSize(wm)) {
    availSize.BSize(wm) -= border.BStart(wm);

    if (aReflowInput.mStyleBorder->mBoxDecorationBreak ==
        StyleBoxDecorationBreak::Clone) {
      // We have box-decoration-break:clone. Subtract block-end border from the
      // available block-size as well.
      availSize.BSize(wm) -= border.BEnd(wm);
    }
  }

  // Available block-size can became negative after subtracting block-direction
  // border and padding. Per spec, to guarantee progress, fragmentainers are
  // assumed to have a minimum block size of 1px regardless of their used size.
  // https://drafts.csswg.org/css-break/#breaking-rules
  availSize.BSize(wm) =
      std::max(availSize.BSize(wm), nsPresContext::CSSPixelsToAppUnits(1));

  WritingMode kidWM = inner->GetWritingMode();
  ReflowInput kidReflowInput(aPresContext, aReflowInput, inner,
                             availSize.ConvertTo(kidWM, wm), Nothing(),
                             ReflowInput::InitFlag::CallerWillInit);
  // Override computed padding, in case it's percentage padding
  {
    const auto padding = aReflowInput.ComputedLogicalPadding(kidWM);
    kidReflowInput.Init(aPresContext, Nothing(), Nothing(), Some(padding));
    if (inner->IsScrollContainerFrame()) {
      // Propagate explicit block sizes to our inner frame, if it's a scroll
      // frame. Note that in table layout, explicit heights act as a minimum
      // height, see nsTableRowFrame::CalcCellActualBSize.
      //
      // Table cells don't respect box-sizing, so we need to remove the
      // padding, so that the scroll-frame sizes properly (since the
      // scrollbars also add to the padding area).
      auto ToScrolledBSize = [&](const nscoord aBSize) {
        return std::max(0, aBSize - padding.BStartEnd(kidWM));
      };
      nscoord minBSize = aReflowInput.ComputedMinBSize();
      if (aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE) {
        minBSize = std::max(minBSize, aReflowInput.ComputedBSize());
      }
      if (minBSize > 0) {
        kidReflowInput.SetComputedMinBSize(ToScrolledBSize(minBSize));
      }
    }
  }

  // Don't be a percent height observer if we're in the middle of
  // special-bsize reflow, in case we get an accidental NotifyPercentBSize()
  // call (which we shouldn't honor during special-bsize reflow)
  if (!aReflowInput.mFlags.mSpecialBSizeReflow) {
    // mPercentBSizeObserver is for children of cells in quirks mode,
    // but only those than are tables in standards mode.  NeedsToObserve
    // will determine how far this is propagated to descendants.
    kidReflowInput.mPercentBSizeObserver = this;
  }
  // Don't propagate special bsize reflow input to our kids
  kidReflowInput.mFlags.mSpecialBSizeReflow = false;

  if (aReflowInput.mFlags.mSpecialBSizeReflow ||
      FirstInFlow()->HasAnyStateBits(NS_TABLE_CELL_HAD_SPECIAL_REFLOW)) {
    // We need to force the kid to have mBResize set if we've had a
    // special reflow in the past, since the non-special reflow needs to
    // resize back to what it was without the special bsize reflow.
    kidReflowInput.SetBResize(true);
  }

  nsSize containerSize = aReflowInput.ComputedSizeAsContainerIfConstrained();

  const LogicalPoint kidOrigin = border.StartOffset(wm);
  const nsRect origRect = inner->GetRect();
  const nsRect origInkOverflow = inner->InkOverflowRect();
  const bool firstReflow = inner->HasAnyStateBits(NS_FRAME_FIRST_REFLOW);

  ReflowChild(inner, aPresContext, kidSize, kidReflowInput, wm, kidOrigin,
              containerSize, ReflowChildFlags::Default, aStatus);
  if (aStatus.IsOverflowIncomplete()) {
    // Don't pass OVERFLOW_INCOMPLETE through tables until they can actually
    // handle it
    // XXX should paginate overflow as overflow, but not in this patch (bug
    // 379349)
    aStatus.SetIncomplete();
    NS_WARNING(nsPrintfCString("Set table cell incomplete %p", this).get());
  }

  // XXXbz is this invalidate actually needed, really?
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    InvalidateFrameSubtree();
  }

#ifdef DEBUG
  DebugCheckChildSize(inner, kidSize);
#endif

  // Place the child
  FinishReflowChild(inner, aPresContext, kidSize, &kidReflowInput, wm,
                    kidOrigin, containerSize, ReflowChildFlags::Default);

  {
    nsIFrame* prevInFlow = GetPrevInFlow();
    const bool isEmpty =
        prevInFlow
            ? static_cast<nsTableCellFrame*>(prevInFlow)->GetContentEmpty()
            : !CellHasVisibleContent(tableFrame, this);
    SetContentEmpty(isEmpty);
  }

  if (tableFrame->IsBorderCollapse()) {
    nsTableFrame::InvalidateTableFrame(inner, origRect, origInkOverflow,
                                       firstReflow);
  }
  // first, compute the bsize which can be set w/o being restricted by
  // available bsize
  LogicalSize cellSize(wm);
  cellSize.BSize(wm) = kidSize.BSize(wm);

  if (NS_UNCONSTRAINEDSIZE != cellSize.BSize(wm)) {
    cellSize.BSize(wm) += border.BStart(wm);

    if (aStatus.IsComplete() ||
        aReflowInput.mStyleBorder->mBoxDecorationBreak ==
            StyleBoxDecorationBreak::Clone) {
      cellSize.BSize(wm) += border.BEnd(wm);
    }
  }

  // next determine the cell's isize. At this point, we've factored in the
  // cell's style attributes.
  cellSize.ISize(wm) = kidSize.ISize(wm);

  // factor in border (and disregard padding, which is handled by our child).
  if (NS_UNCONSTRAINEDSIZE != cellSize.ISize(wm)) {
    cellSize.ISize(wm) += border.IStartEnd(wm);
  }

  // set the cell's desired size and max element size
  aDesiredSize.SetSize(wm, cellSize);

  // the overflow area will be computed when BlockDirAlignChild() gets called

  if (aReflowInput.mFlags.mSpecialBSizeReflow &&
      NS_UNCONSTRAINEDSIZE == aReflowInput.AvailableBSize()) {
    aDesiredSize.BSize(wm) = BSize(wm);
  }

  // If our parent is in initial reflow, it'll handle invalidating our
  // entire overflow rect.
  if (!GetParent()->HasAnyStateBits(NS_FRAME_FIRST_REFLOW) &&
      nsSize(aDesiredSize.Width(), aDesiredSize.Height()) != mRect.Size()) {
    InvalidateFrame();
  }

  // remember the desired size for this reflow
  SetDesiredSize(aDesiredSize);

  // Any absolutely-positioned children will get reflowed in
  // nsIFrame::FixupPositionedTableParts in another pass, so propagate our
  // dirtiness to them before our parent clears our dirty bits.
  PushDirtyBitToAbsoluteFrames();
}

void nsBCTableCellFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  nsTableCellFrame::Reflow(aPresContext, aDesiredSize, aReflowInput, aStatus);
  mLastUsedBorder = GetUsedBorder();
}

/* ----- global methods ----- */

NS_QUERYFRAME_HEAD(nsTableCellFrame)
  NS_QUERYFRAME_ENTRY(nsTableCellFrame)
  NS_QUERYFRAME_ENTRY(nsITableCellLayout)
  NS_QUERYFRAME_ENTRY(nsIPercentBSizeObserver)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

#ifdef ACCESSIBILITY
a11y::AccType nsTableCellFrame::AccessibleType() {
  return a11y::eHTMLTableCellType;
}
#endif

/* This is primarily for editor access via nsITableLayout */
NS_IMETHODIMP
nsTableCellFrame::GetCellIndexes(int32_t& aRowIndex, int32_t& aColIndex) {
  aRowIndex = RowIndex();
  aColIndex = mColIndex;
  return NS_OK;
}

nsTableCellFrame* NS_NewTableCellFrame(PresShell* aPresShell,
                                       ComputedStyle* aStyle,
                                       nsTableFrame* aTableFrame) {
  if (aTableFrame->IsBorderCollapse()) {
    return new nsBCTableCellFrame(aStyle, aTableFrame);
  }
  return new nsTableCellFrame(aStyle, aTableFrame);
}

NS_IMPL_FRAMEARENA_HELPERS(nsBCTableCellFrame)

LogicalMargin nsTableCellFrame::GetBorderWidth(WritingMode aWM) const {
  return LogicalMargin(aWM, StyleBorder()->GetComputedBorder());
}

void nsTableCellFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  aResult.AppendElement(OwnedAnonBox(Inner()));
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsTableCellFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"TableCell"_ns, aResult);
}
#endif

// nsBCTableCellFrame

nsBCTableCellFrame::nsBCTableCellFrame(ComputedStyle* aStyle,
                                       nsTableFrame* aTableFrame)
    : nsTableCellFrame(aStyle, aTableFrame, kClassID) {}

nsBCTableCellFrame::~nsBCTableCellFrame() = default;

/* virtual */
nsMargin nsBCTableCellFrame::GetUsedBorder() const {
  WritingMode wm = GetWritingMode();
  return GetBorderWidth(wm).GetPhysicalMargin(wm);
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsBCTableCellFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"BCTableCell"_ns, aResult);
}
#endif

LogicalMargin nsBCTableCellFrame::GetBorderWidth(WritingMode aWM) const {
  return LogicalMargin(
      aWM, BC_BORDER_END_HALF(mBStartBorder), BC_BORDER_START_HALF(mIEndBorder),
      BC_BORDER_START_HALF(mBEndBorder), BC_BORDER_END_HALF(mIStartBorder));
}

nscoord nsBCTableCellFrame::GetBorderWidth(LogicalSide aSide) const {
  switch (aSide) {
    case LogicalSide::BStart:
      return BC_BORDER_END_HALF(mBStartBorder);
    case LogicalSide::IEnd:
      return BC_BORDER_START_HALF(mIEndBorder);
    case LogicalSide::BEnd:
      return BC_BORDER_START_HALF(mBEndBorder);
    default:
      return BC_BORDER_END_HALF(mIStartBorder);
  }
}

void nsBCTableCellFrame::SetBorderWidth(LogicalSide aSide, nscoord aValue) {
  switch (aSide) {
    case LogicalSide::BStart:
      mBStartBorder = aValue;
      break;
    case LogicalSide::IEnd:
      mIEndBorder = aValue;
      break;
    case LogicalSide::BEnd:
      mBEndBorder = aValue;
      break;
    default:
      mIStartBorder = aValue;
  }
}

/* virtual */
nsMargin nsBCTableCellFrame::GetBorderOverflow() {
  WritingMode wm = GetWritingMode();
  LogicalMargin halfBorder(
      wm, BC_BORDER_START_HALF(mBStartBorder), BC_BORDER_END_HALF(mIEndBorder),
      BC_BORDER_END_HALF(mBEndBorder), BC_BORDER_START_HALF(mIStartBorder));
  return halfBorder.GetPhysicalMargin(wm);
}

namespace mozilla {

class nsDisplayTableCellSelection final : public nsPaintedDisplayItem {
 public:
  nsDisplayTableCellSelection(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayTableCellSelection);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayTableCellSelection)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override {
    static_cast<nsTableCellFrame*>(mFrame)->DecorateForSelection(
        aCtx->GetDrawTarget(), ToReferenceFrame());
  }
  NS_DISPLAY_DECL_NAME("TableCellSelection", TYPE_TABLE_CELL_SELECTION)

  WebRenderCommandsResult CreateWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override {
    RefPtr<nsFrameSelection> frameSelection =
        mFrame->PresShell()->FrameSelection();
    if (frameSelection->IsInTableSelectionMode()) {
      return Err("table selection decoration needs the fallback paint path");
    }
    return Ok();
  }
};

}  // namespace mozilla

void nsTableCellFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                        const nsDisplayListSet& aLists) {
  DO_GLOBAL_REFLOW_COUNT_DSP("nsTableCellFrame");
  if (ShouldPaintBordersAndBackgrounds()) {
    // display outset box-shadows if we need to.
    bool hasBoxShadow = !StyleEffects()->mBoxShadow.IsEmpty();
    if (hasBoxShadow) {
      aLists.BorderBackground()->AppendNewToTop<nsDisplayBoxShadowOuter>(
          aBuilder, this);
    }

    nsRect bgRect = GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
    nsRect bgRectInsideBorder = bgRect;

    // If we're doing collapsed borders, and this element forms a new stacking
    // context or has position:relative (which paints as though it did), inset
    // the background rect so that we don't overpaint the inset part of our
    // borders.
    nsTableFrame* tableFrame = GetTableFrame();
    if (tableFrame->IsBorderCollapse() &&
        (IsStackingContext() ||
         StyleDisplay()->mPosition == StylePositionProperty::Relative)) {
      bgRectInsideBorder.Deflate(GetUsedBorder());
    }

    // display background if we need to.
    const AppendedBackgroundType result =
        nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
            aBuilder, this, bgRectInsideBorder, aLists.BorderBackground(), true,
            bgRect);
    if (result == AppendedBackgroundType::None) {
      aBuilder->BuildCompositorHitTestInfoIfNeeded(this,
                                                   aLists.BorderBackground());
    }

    // display inset box-shadows if we need to.
    if (hasBoxShadow) {
      aLists.BorderBackground()->AppendNewToTop<nsDisplayBoxShadowInner>(
          aBuilder, this);
    }

    // display borders if we need to
    ProcessBorders(tableFrame, aBuilder, aLists);

    // and display the selection border if we need to
    if (IsSelected()) {
      aLists.BorderBackground()->AppendNewToTop<nsDisplayTableCellSelection>(
          aBuilder, this);
    }

    // This can be null if display list building initiated in the middle
    // of the table, which can happen with background-clip:text and
    // -moz-element.
    nsDisplayTableBackgroundSet* backgrounds =
        aBuilder->GetTableBackgroundSet();
    if (backgrounds) {
      // Compute bgRect relative to reference frame, but using the
      // normal (without position:relative offsets) positions for the
      // cell, row and row group.
      bgRect = GetRectRelativeToSelf() + GetNormalPosition();

      nsTableRowFrame* row = GetTableRowFrame();
      bgRect += row->GetNormalPosition();

      nsTableRowGroupFrame* rowGroup = row->GetTableRowGroupFrame();
      bgRect += rowGroup->GetNormalPosition();

      bgRect += backgrounds->TableToReferenceFrame();

      DisplayListClipState::AutoSaveRestore clipState(aBuilder);
      nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
          aBuilder);
      if (IsStackingContext() || row->IsStackingContext() ||
          rowGroup->IsStackingContext() || tableFrame->IsStackingContext()) {
        // The col/colgroup items we create below will be inserted directly into
        // the BorderBackgrounds list of the table frame. That means that
        // they'll be moved *outside* of any wrapper items created for any
        // frames between this table cell frame and the table wrapper frame, and
        // will not participate in those frames's opacity / transform / filter /
        // mask effects. If one of those frames is a stacking context, then we
        // may have one or more of those wrapper items, and one of them may have
        // captured a clip. In order to ensure correct clipping and scrolling of
        // the col/colgroup items, restore the clip and ASR that we observed
        // when we entered the table frame. If that frame is a stacking context
        // but doesn't have any clip capturing wrapper items, then we'll
        // double-apply the clip. That's ok.
        clipState.SetClipChainForContainingBlockDescendants(
            backgrounds->GetTableClipChain());
        asrSetter.SetCurrentActiveScrolledRoot(backgrounds->GetTableASR());
      }

      // Create backgrounds items as needed for the column and column
      // group that this cell occupies.
      nsTableColFrame* col = backgrounds->GetColForIndex(ColIndex());
      nsTableColGroupFrame* colGroup = col->GetTableColGroupFrame();

      Maybe<nsDisplayListBuilder::AutoBuildingDisplayList> buildingForColGroup;
      nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
          aBuilder, colGroup, bgRect, backgrounds->ColGroupBackgrounds(), false,
          colGroup->GetRect() + backgrounds->TableToReferenceFrame(), this,
          &buildingForColGroup);

      Maybe<nsDisplayListBuilder::AutoBuildingDisplayList> buildingForCol;
      nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
          aBuilder, col, bgRect, backgrounds->ColBackgrounds(), false,
          col->GetRect() + colGroup->GetPosition() +
              backgrounds->TableToReferenceFrame(),
          this, &buildingForCol);
    }
  }

  // the 'empty-cells' property has no effect on 'outline'
  DisplayOutline(aBuilder, aLists);
  if (HidesContent()) {
    return;
  }

  // The child's background will go in our BorderBackground() list.
  // This isn't a problem since it won't have a real background except for
  // event handling. We do not call BuildDisplayListForNonBlockChildren
  // because that/ would put the child's background in the Content() list
  // which isn't right (e.g., would end up on top of our child floats for
  // event handling).
  BuildDisplayListForChild(aBuilder, Inner(), aLists);
}
