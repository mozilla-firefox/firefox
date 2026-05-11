/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for css3 multi-column layout */

#include "nsColumnSetFrame.h"

#include "mozilla/ColumnUtils.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/ToString.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"

using namespace mozilla;
using namespace mozilla::layout;

// To see this log, use $ MOZ_LOG=ColumnSet:4 ./mach run
static LazyLogModule sColumnSetLog("ColumnSet");
#define COLUMN_SET_LOG(msg, ...) \
  MOZ_LOG(sColumnSetLog, LogLevel::Debug, (msg, ##__VA_ARGS__))

class nsDisplayColumnRule final : public nsPaintedDisplayItem {
 public:
  nsDisplayColumnRule(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayColumnRule);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayColumnRule)

  nsRect GetBounds(nsDisplayListBuilder* aBuilder, bool* aSnap) const override {
    *aSnap = false;
    // We just return the frame's ink-overflow rect, which is guaranteed to
    // contain all the column-rule areas.  It's not worth calculating the exact
    // union of those areas since it would only lead to performance improvements
    // during painting in rare edge cases.
    return mFrame->InkOverflowRect() + ToReferenceFrame();
  }

  bool CreateWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override;
  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;

  NS_DISPLAY_DECL_NAME("ColumnRule", TYPE_COLUMN_RULE);

 private:
  nsTArray<nsCSSBorderRenderer> mBorderRenderers;
};

void nsDisplayColumnRule::Paint(nsDisplayListBuilder* aBuilder,
                                gfxContext* aCtx) {
  static_cast<nsColumnSetFrame*>(mFrame)->CreateBorderRenderers(
      mBorderRenderers, aCtx, GetPaintRect(aBuilder, aCtx), ToReferenceFrame());

  for (auto iter = mBorderRenderers.begin(); iter != mBorderRenderers.end();
       iter++) {
    iter->DrawBorders();
  }
}

bool nsDisplayColumnRule::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  RefPtr dt = gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget();
  if (!dt || !dt->IsValid()) {
    return false;
  }
  gfxContext screenRefCtx(dt);

  bool dummy;
  static_cast<nsColumnSetFrame*>(mFrame)->CreateBorderRenderers(
      mBorderRenderers, &screenRefCtx, GetBounds(aDisplayListBuilder, &dummy),
      ToReferenceFrame());

  if (mBorderRenderers.IsEmpty()) {
    return true;
  }

  for (auto& renderer : mBorderRenderers) {
    renderer.CreateWebRenderCommands(this, aBuilder, aResources, aSc);
  }

  return true;
}

// The maximum number of columns we support.
static constexpr int32_t kMaxColumnCount = 1000;

/**
 * Tracking issues:
 *
 * XXX cursor movement around the top and bottom of colums seems to make the
 * editor lose the caret.
 *
 * XXX should we support CSS columns applied to table elements?
 */
nsContainerFrame* NS_NewColumnSetFrame(PresShell* aPresShell,
                                       ComputedStyle* aStyle,
                                       nsFrameState aStateFlags) {
  nsColumnSetFrame* it =
      new (aPresShell) nsColumnSetFrame(aStyle, aPresShell->GetPresContext());
  it->AddStateBits(aStateFlags);
  return it;
}

NS_IMPL_FRAMEARENA_HELPERS(nsColumnSetFrame)

nsColumnSetFrame::nsColumnSetFrame(ComputedStyle* aStyle,
                                   nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID),
      mLastBalanceBSize(NS_UNCONSTRAINEDSIZE) {}

void nsColumnSetFrame::ForEachColumnRule(
    const std::function<void(const nsRect& lineRect)>& aSetLineRect,
    const nsPoint& aPt) const {
  nsIFrame* child = mFrames.FirstChild();
  if (!child) {
    return;  // no columns
  }

  nsIFrame* nextSibling = child->GetNextSibling();
  if (!nextSibling) {
    return;  // 1 column only - this means no gap to draw on
  }

  const nsStyleColumn* colStyle = StyleColumn();
  nscoord ruleWidth = colStyle->GetColumnRuleWidth();
  if (!ruleWidth) {
    return;
  }

  WritingMode wm = GetWritingMode();
  bool isVertical = wm.IsVertical();
  bool isRTL = wm.IsBidiRTL();

  nsRect contentRect = GetContentRectRelativeToSelf() + aPt;
  nsSize ruleSize = isVertical ? nsSize(contentRect.width, ruleWidth)
                               : nsSize(ruleWidth, contentRect.height);

  while (nextSibling) {
    // The frame tree goes RTL in RTL.
    // The |prevFrame| and |nextFrame| frames here are the visually preceding
    // (left/above) and following (right/below) frames, not in logical writing-
    // mode direction.
    nsIFrame* prevFrame = isRTL ? nextSibling : child;
    nsIFrame* nextFrame = isRTL ? child : nextSibling;

    // Each child frame's position coordinates is actually relative to this
    // nsColumnSetFrame.
    // linePt will be at the top-left edge to paint the line.
    nsPoint linePt;
    if (isVertical) {
      nscoord edgeOfPrev = prevFrame->GetRect().YMost() + aPt.y;
      nscoord edgeOfNext = nextFrame->GetRect().Y() + aPt.y;
      linePt = nsPoint(contentRect.x,
                       (edgeOfPrev + edgeOfNext - ruleSize.height) / 2);
    } else {
      nscoord edgeOfPrev = prevFrame->GetRect().XMost() + aPt.x;
      nscoord edgeOfNext = nextFrame->GetRect().X() + aPt.x;
      linePt = nsPoint((edgeOfPrev + edgeOfNext - ruleSize.width) / 2,
                       contentRect.y);
    }

    aSetLineRect(nsRect(linePt, ruleSize));

    child = nextSibling;
    nextSibling = nextSibling->GetNextSibling();
  }
}

void nsColumnSetFrame::CreateBorderRenderers(
    nsTArray<nsCSSBorderRenderer>& aBorderRenderers, gfxContext* aCtx,
    const nsRect& aDirtyRect, const nsPoint& aPt) {
  WritingMode wm = GetWritingMode();
  bool isVertical = wm.IsVertical();
  const nsStyleColumn* colStyle = StyleColumn();
  StyleBorderStyle ruleStyle;

  // Per spec, inset => ridge and outset => groove
  if (colStyle->mColumnRuleStyle == StyleBorderStyle::Inset) {
    ruleStyle = StyleBorderStyle::Ridge;
  } else if (colStyle->mColumnRuleStyle == StyleBorderStyle::Outset) {
    ruleStyle = StyleBorderStyle::Groove;
  } else {
    ruleStyle = colStyle->mColumnRuleStyle;
  }

  nscoord ruleWidth = colStyle->GetColumnRuleWidth();
  if (!ruleWidth) {
    return;
  }

  aBorderRenderers.Clear();
  nscolor ruleColor =
      GetVisitedDependentColor(&nsStyleColumn::mColumnRuleColor);

  nsPresContext* pc = PresContext();
  // In order to re-use a large amount of code, we treat the column rule as a
  // border. We create a new border style object and fill in all the details of
  // the column rule as the left border. PaintBorder() does all the rendering
  // for us, so we not only save an enormous amount of code but we'll support
  // all the line styles that we support on borders!
  nsStyleBorder border;
  Sides skipSides;
  if (isVertical) {
    border.SetBorderWidth(eSideTop, ruleWidth, pc->AppUnitsPerDevPixel());
    border.SetBorderStyle(eSideTop, ruleStyle);
    border.mBorderTopColor = StyleColor::FromColor(ruleColor);
    skipSides |= mozilla::SideBits::eLeftRight;
    skipSides |= mozilla::SideBits::eBottom;
  } else {
    border.SetBorderWidth(eSideLeft, ruleWidth, pc->AppUnitsPerDevPixel());
    border.SetBorderStyle(eSideLeft, ruleStyle);
    border.mBorderLeftColor = StyleColor::FromColor(ruleColor);
    skipSides |= mozilla::SideBits::eTopBottom;
    skipSides |= mozilla::SideBits::eRight;
  }
  // If we use box-decoration-break: slice (the default), the border
  // renderers will require clipping if we have continuations (see the
  // aNeedsClip parameter to ConstructBorderRenderer in nsCSSRendering).
  //
  // Since it doesn't matter which box-decoration-break we use since
  // we're only drawing borders (and not border-images), use 'clone'.
  border.mBoxDecorationBreak = StyleBoxDecorationBreak::Clone;

  ForEachColumnRule(
      [&](const nsRect& aLineRect) {
        // Assert that we're not drawing a border-image here; if we were, we
        // couldn't ignore the ImgDrawResult that PaintBorderWithStyleBorder
        // returns.
        MOZ_ASSERT(border.mBorderImageSource.IsNone());

        gfx::DrawTarget* dt = aCtx ? aCtx->GetDrawTarget() : nullptr;
        bool borderIsEmpty = false;
        Maybe<nsCSSBorderRenderer> br =
            nsCSSRendering::CreateBorderRendererWithStyleBorder(
                pc, dt, this, aDirtyRect, aLineRect, border, Style(),
                &borderIsEmpty, skipSides);
        if (br.isSome()) {
          MOZ_ASSERT(!borderIsEmpty);
          aBorderRenderers.AppendElement(br.value());
        }
      },
      aPt);
}

static uint32_t ColumnBalancingDepth(const ReflowInput& aReflowInput,
                                     uint32_t aMaxDepth) {
  uint32_t depth = 0;
  for (const ReflowInput* ri = aReflowInput.mParentReflowInput;
       ri && depth < aMaxDepth; ri = ri->mParentReflowInput) {
    if (ri->mFlags.mIsColumnBalancing) {
      ++depth;
    }
  }
  return depth;
}

nsColumnSetFrame::ReflowConfig nsColumnSetFrame::ChooseColumnStrategy(
    const ReflowInput& aReflowInput, bool aForceAuto = false) const {
  const nsStyleColumn* colStyle = StyleColumn();
  const nscoord availContentISize = aReflowInput.AvailableISize();
  nscoord colBSize = aReflowInput.AvailableBSize();
  const nscoord colGap = ColumnUtils::GetColumnGap(this, availContentISize);
  int32_t numColumns =
      colStyle->mColumnCount.IsAuto()
          ? 0
          : std::min(colStyle->mColumnCount.AsInteger(), kMaxColumnCount);

  // If column-fill is set to 'balance' or we have a column-span sibling, then
  // we want to balance the columns.
  bool isBalancing = (colStyle->mColumnFill == StyleColumnFill::Balance ||
                      HasColumnSpanSiblings()) &&
                     !aForceAuto;
  if (isBalancing) {
    const uint32_t kMaxNestedColumnBalancingDepth = 2;
    const uint32_t balancingDepth =
        ColumnBalancingDepth(aReflowInput, kMaxNestedColumnBalancingDepth);
    if (balancingDepth == kMaxNestedColumnBalancingDepth) {
      isBalancing = false;
      numColumns = 1;
      aForceAuto = true;
    }
  }

  nscoord colISize;
  // In vertical writing-mode, "column-width" (inline size) will actually be
  // physical height, but its CSS name is still column-width.
  if (colStyle->mColumnWidth.IsLength()) {
    colISize =
        ColumnUtils::ClampUsedColumnWidth(colStyle->mColumnWidth.AsLength());
    NS_ASSERTION(colISize >= 0, "negative column width");
    // Reduce column count if necessary to make columns fit in the
    // available width. Compute max number of columns that fit in
    // availContentISize, satisfying colGap*(maxColumns - 1) +
    // colISize*maxColumns <= availContentISize
    if (availContentISize != NS_UNCONSTRAINEDSIZE && colGap + colISize > 0 &&
        numColumns > 0) {
      // This expression uses truncated rounding, which is what we
      // want
      int32_t maxColumns =
          std::min(nscoord(kMaxColumnCount),
                   (availContentISize + colGap) / (colGap + colISize));
      numColumns = std::max(1, std::min(numColumns, maxColumns));
    }
  } else if (numColumns > 0 && availContentISize != NS_UNCONSTRAINEDSIZE) {
    nscoord iSizeMinusGaps = availContentISize - colGap * (numColumns - 1);
    colISize = iSizeMinusGaps / numColumns;
  } else {
    colISize = NS_UNCONSTRAINEDSIZE;
  }
  // Take care of the situation where there's only one column but it's
  // still too wide
  colISize = std::max(1, std::min(colISize, availContentISize));

  nscoord expectedISizeLeftOver = 0;

  if (colISize != NS_UNCONSTRAINEDSIZE &&
      availContentISize != NS_UNCONSTRAINEDSIZE) {
    // distribute leftover space

    // First, determine how many columns will be showing if the column
    // count is auto
    if (numColumns <= 0) {
      // choose so that colGap*(nominalColumnCount - 1) +
      // colISize*nominalColumnCount is nearly availContentISize
      // make sure to round down
      if (colGap + colISize > 0) {
        numColumns = (availContentISize + colGap) / (colGap + colISize);
        // The number of columns should never exceed kMaxColumnCount.
        numColumns = std::min(kMaxColumnCount, numColumns);
      }
      if (numColumns <= 0) {
        numColumns = 1;
      }
    }

    // Compute extra space and divide it among the columns
    nscoord extraSpace =
        std::max(0, availContentISize -
                        (colISize * numColumns + colGap * (numColumns - 1)));
    nscoord extraToColumns = extraSpace / numColumns;
    colISize += extraToColumns;
    expectedISizeLeftOver = extraSpace - (extraToColumns * numColumns);
  }

  if (isBalancing) {
    if (numColumns <= 0) {
      // Hmm, auto column count, column width or available width is unknown,
      // and balancing is required. Let's just use one column then.
      numColumns = 1;
    }
    colBSize = std::min(mLastBalanceBSize, colBSize);
  } else {
    // CSS Fragmentation spec says, "To guarantee progress, fragmentainers are
    // assumed to have a minimum block size of 1px regardless of their used
    // size." https://drafts.csswg.org/css-break/#breaking-rules
    //
    // Note: we don't enforce the minimum block-size during balancing because
    // this affects the result. If a balancing column container or its
    // next-in-flows has zero block-size, it eventually gives up balancing, and
    // ends up here.
    colBSize = std::max(colBSize, nsPresContext::CSSPixelsToAppUnits(1));
  }

  ReflowConfig config;
  config.mUsedColCount = numColumns;
  config.mColISize = colISize;
  config.mExpectedISizeLeftOver = expectedISizeLeftOver;
  config.mColGap = colGap;
  config.mColBSize = colBSize;
  config.mIsBalancing = isBalancing;
  config.mForceAuto = aForceAuto;
  config.mKnownFeasibleBSize = NS_UNCONSTRAINEDSIZE;
  config.mKnownInfeasibleBSize = 0;

  COLUMN_SET_LOG(
      "%s: this=%p, mUsedColCount=%d, mColISize=%d, "
      "mExpectedISizeLeftOver=%d, mColGap=%d, mColBSize=%d, mIsBalancing=%d",
      __func__, this, config.mUsedColCount, config.mColISize,
      config.mExpectedISizeLeftOver, config.mColGap, config.mColBSize,
      config.mIsBalancing);

  return config;
}

static void MoveChildTo(nsIFrame* aChild, LogicalPoint aOrigin, WritingMode aWM,
                        const nsSize& aContainerSize) {
  if (aChild->GetLogicalPosition(aWM, aContainerSize) == aOrigin) {
    return;
  }

  aChild->SetPosition(aWM, aOrigin, aContainerSize);
}

nscoord nsColumnSetFrame::IntrinsicISize(const IntrinsicSizeInput& input,
                                         IntrinsicISizeType aType) {
  return aType == IntrinsicISizeType::MinISize ? MinISize(input)
                                               : PrefISize(input);
}

nscoord nsColumnSetFrame::MinISize(const IntrinsicSizeInput& aInput) {
  nscoord iSize = 0;

  if (mFrames.FirstChild()) {
    iSize = mFrames.FirstChild()->GetMinISize(aInput);
  }
  const nsStyleColumn* colStyle = StyleColumn();
  if (colStyle->mColumnWidth.IsLength()) {
    nscoord colISize =
        ColumnUtils::ClampUsedColumnWidth(colStyle->mColumnWidth.AsLength());
    // As available width reduces to zero, we reduce our number of columns
    // to one, and don't enforce the column width, so just return the min
    // of the child's min-width with any specified column width.
    iSize = std::min(iSize, colISize);
  } else {
    NS_ASSERTION(!colStyle->mColumnCount.IsAuto(),
                 "column-count and column-width can't both be auto");
    // As available width reduces to zero, we still have mColumnCount columns,
    // so compute our minimum size based on the number of columns and their gaps
    // and minimum per-column size.
    nscoord colGap = ColumnUtils::GetColumnGap(this, NS_UNCONSTRAINEDSIZE);
    iSize = ColumnUtils::IntrinsicISize(colStyle->mColumnCount.AsInteger(),
                                        colGap, iSize);
  }
  // XXX count forced column breaks here? Maybe we should return the child's
  // min-width times the minimum number of columns.
  return iSize;
}

nscoord nsColumnSetFrame::PrefISize(const IntrinsicSizeInput& aInput) {
  // Our preferred width is our desired column width, if specified, otherwise
  // the child's preferred width, times the number of columns, plus the width
  // of any required column gaps
  // XXX what about forced column breaks here?
  const nsStyleColumn* colStyle = StyleColumn();

  nscoord colISize;
  if (colStyle->mColumnWidth.IsLength()) {
    colISize =
        ColumnUtils::ClampUsedColumnWidth(colStyle->mColumnWidth.AsLength());
  } else if (mFrames.FirstChild()) {
    colISize = mFrames.FirstChild()->GetPrefISize(aInput);
  } else {
    colISize = 0;
  }

  // If column-count is auto, assume one column.
  uint32_t numColumns =
      colStyle->mColumnCount.IsAuto() ? 1 : colStyle->mColumnCount.AsInteger();
  nscoord colGap = ColumnUtils::GetColumnGap(this, NS_UNCONSTRAINEDSIZE);
  return ColumnUtils::IntrinsicISize(numColumns, colGap, colISize);
}

nsColumnSetFrame::ColumnBalanceData nsColumnSetFrame::ReflowColumns(
    ReflowOutput& aDesiredSize, const ReflowInput& aReflowInput,
    nsReflowStatus& aStatus, const ReflowConfig& aConfig,
    bool aUnboundedLastColumn) {
  ColumnBalanceData colData;
  bool allFit = true;
  WritingMode wm = GetWritingMode();
  const bool isRTL = wm.IsBidiRTL();
  const bool shrinkingBSize = mLastBalanceBSize > aConfig.mColBSize;
  const bool changingBSize = mLastBalanceBSize != aConfig.mColBSize;

  COLUMN_SET_LOG(
      "%s: Doing column reflow pass: mLastBalanceBSize=%d,"
      " mColBSize=%d, RTL=%d, mUsedColCount=%d,"
      " mColISize=%d, mColGap=%d",
      __func__, mLastBalanceBSize, aConfig.mColBSize, isRTL,
      aConfig.mUsedColCount, aConfig.mColISize, aConfig.mColGap);

  DrainOverflowColumns();

  if (changingBSize) {
    mLastBalanceBSize = aConfig.mColBSize;
    // XXX Seems like this could fire if incremental reflow pushed the column
    // set down so we reflow incrementally with a different available height.
    // We need a way to do an incremental reflow and be sure availableHeight
    // changes are taken account of! Right now I think block frames with
    // absolute children might exit early.
    /*
    NS_ASSERTION(
        aKidReason != eReflowReason_Incremental,
        "incremental reflow should not have changed the balance height");
    */
  }

  nsRect contentRect(0, 0, 0, 0);
  OverflowAreas overflowRects;

  nsIFrame* child = mFrames.FirstChild();
  LogicalPoint childOrigin(wm, 0, 0);

  // In vertical-rl mode, columns will not be correctly placed if the
  // reflowInput's ComputedWidth() is UNCONSTRAINED (in which case we'll get
  // a containerSize.width of zero here). In that case, the column positions
  // will be adjusted later, after our correct contentSize is known.
  //
  // When column-span is enabled, containerSize.width is always constrained.
  // However, for RTL, we need to adjust the column positions as well after our
  // correct containerSize is known.
  nsSize containerSize = aReflowInput.ComputedSizeAsContainerIfConstrained();

  const nscoord computedBSize =
      aReflowInput.mParentReflowInput->ComputedBSize();
  nscoord contentBEnd = 0;
  bool reflowNext = false;

  while (child) {
    const bool reflowLastColumnWithUnconstrainedAvailBSize =
        aUnboundedLastColumn && colData.mColCount == aConfig.mUsedColCount &&
        aConfig.mIsBalancing;

    // We need to reflow the child (column) ...
    bool reflowChild =
        // if we are told to do so;
        aReflowInput.ShouldReflowAllKids() ||
        // if the child is dirty;
        child->IsSubtreeDirty() ||
        // if it's the last child because we need to obtain the block-end
        // margin;
        !child->GetNextSibling() ||
        // if the next column is dirty, because the next column's first line(s)
        // might be pullable back to this column;
        child->GetNextSibling()->IsSubtreeDirty() ||
        // if this is the last column and we are supposed to assign unbounded
        // block-size to it, because that could change the available block-size
        // from the last time we reflowed it and we should try to pull all the
        // content from its next sibling (Note that it might be the last column,
        // but not be the last child because the desired number of columns has
        // changed.)
        reflowLastColumnWithUnconstrainedAvailBSize;

    // If column-fill is auto (not the default), then we might need to
    // move content between columns for any change in column block-size.
    //
    // The same is true if we have a non-'auto' computed block-size.
    //
    // FIXME: It's not clear to me why it's *ever* valid to have
    // reflowChild be false when changingBSize is true, since it
    // seems like a child broken over multiple columns might need to
    // change the size of the fragment in each column.
    if (!reflowChild && changingBSize &&
        (StyleColumn()->mColumnFill == StyleColumnFill::Auto ||
         computedBSize != NS_UNCONSTRAINEDSIZE)) {
      reflowChild = true;
    }
    // If we need to pull up content from the prev-in-flow then this is not just
    // a block-size shrink. The prev in flow will have set the dirty bit.
    // Check the overflow rect YMost instead of just the child's content
    // block-size. The child may have overflowing content that cares about the
    // available block-size boundary. (It may also have overflowing content that
    // doesn't care about the available block-size boundary, but if so, too bad,
    // this optimization is defeated.) We want scrollable overflow here since
    // this is a calculation that affects layout.
    if (!reflowChild && shrinkingBSize) {
      switch (wm.GetBlockDir()) {
        case WritingMode::BlockDir::TB:
          if (child->ScrollableOverflowRect().YMost() > aConfig.mColBSize) {
            reflowChild = true;
          }
          break;
        case WritingMode::BlockDir::LR:
          if (child->ScrollableOverflowRect().XMost() > aConfig.mColBSize) {
            reflowChild = true;
          }
          break;
        case WritingMode::BlockDir::RL:
          // XXX not sure how to handle this, so for now just don't attempt
          // the optimization
          reflowChild = true;
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("unknown block direction");
          break;
      }
    }

    nscoord childContentBEnd = 0;
    if (!reflowNext && !reflowChild) {
      // This child does not need to be reflowed, but we may need to move it
      MoveChildTo(child, childOrigin, wm, containerSize);

      // If this is the last frame then make sure we get the right status
      nsIFrame* kidNext = child->GetNextSibling();
      if (kidNext) {
        aStatus.Reset();
        if (kidNext->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
          aStatus.SetOverflowIncomplete();
        } else {
          aStatus.SetIncomplete();
        }
      } else {
        aStatus = mLastFrameStatus;
      }
      childContentBEnd = nsLayoutUtils::CalculateContentBEnd(wm, child);

      COLUMN_SET_LOG("%s: Skipping child #%d %p: status=%s", __func__,
                     colData.mColCount, child, ToString(aStatus).c_str());
    } else {
      LogicalSize availSize(wm, aConfig.mColISize, aConfig.mColBSize);
      if (reflowLastColumnWithUnconstrainedAvailBSize) {
        availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;

        COLUMN_SET_LOG(
            "%s: Reflowing last column with unconstrained block-size. Change "
            "available block-size from %d to %d",
            __func__, aConfig.mColBSize, availSize.BSize(wm));
      }

      if (reflowNext) {
        child->MarkSubtreeDirty();
      }

      LogicalSize kidCBSize(wm, availSize.ISize(wm), computedBSize);
      ReflowInput kidReflowInput(PresContext(), aReflowInput, child, availSize,
                                 Some(kidCBSize));
      kidReflowInput.mFlags.mIsTopOfPage = [&]() {
        const bool isNestedMulticolOrInRootPaginatedDoc =
            aReflowInput.mParentReflowInput->mFrame->HasAnyStateBits(
                NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) ||
            PresContext()->IsRootPaginatedDocument();
        if (isNestedMulticolOrInRootPaginatedDoc) {
          if (aConfig.mForceAuto) {
            // If we are forced to fill columns sequentially, force fit the
            // content whether we are at top of page or not.
            return true;
          }
          if (aReflowInput.mFlags.mIsTopOfPage) {
            // If this is the last balancing reflow, we want to force fit
            // content to avoid infinite loops.
            return !aConfig.mIsBalancing || aConfig.mIsLastBalancingReflow;
          }
          // If we are a not at the top of page, we shouldn't force fit content.
          // This is because our ColumnSetWrapperFrame can be pushed to the next
          // column or page and reflowed again with a potentially larger
          // available block-size.
          return false;
        }
        // We are a top-level multicol in a non-paginated root document or in a
        // subdocument (regardless of whether the root document is paginated).
        // Force fit the content only if we are not balancing columns.
        return !aConfig.mIsBalancing;
      }();
      kidReflowInput.mFlags.mTableIsSplittable = false;
      kidReflowInput.mFlags.mIsColumnBalancing = aConfig.mIsBalancing;
      kidReflowInput.mFlags.mIsInLastColumnBalancingReflow =
          aConfig.mIsLastBalancingReflow;
      kidReflowInput.mFlags.mIsInFragmentainerMeasuringReflow =
          aConfig.mIsInMeasuringReflow;
      kidReflowInput.mBreakType = BreakType::Column;

      // We need to reflow any float placeholders, even if our column block-size
      // hasn't changed.
      kidReflowInput.mFlags.mMustReflowPlaceholders = !changingBSize;

      COLUMN_SET_LOG(
          "%s: Reflowing child #%d %p: availSize=(%d,%d), kidCBSize=(%d,%d), "
          "child's mIsTopOfPage=%d",
          __func__, colData.mColCount, child, availSize.ISize(wm),
          availSize.BSize(wm), kidCBSize.ISize(wm), kidCBSize.BSize(wm),
          kidReflowInput.mFlags.mIsTopOfPage);

      // Note if the column's next in flow is not being changed by this
      // incremental reflow. This may allow the current column to avoid trying
      // to pull lines from the next column.
      if (child->GetNextSibling() && !HasAnyStateBits(NS_FRAME_IS_DIRTY) &&
          !child->GetNextSibling()->HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
        kidReflowInput.mFlags.mNextInFlowUntouched = true;
      }

      ReflowOutput kidDesiredSize(wm);

      // XXX it would be cool to consult the float manager for the
      // previous block to figure out the region of floats from the
      // previous column that extend into this column, and subtract
      // that region from the new float manager.  So you could stick a
      // really big float in the first column and text in following
      // columns would flow around it.

      MOZ_ASSERT(kidReflowInput.ComputedLogicalMargin(wm).IsAllZero(),
                 "-moz-column-content has no margin!");
      aStatus.Reset();
      ReflowChild(child, PresContext(), kidDesiredSize, kidReflowInput, wm,
                  childOrigin, containerSize, ReflowChildFlags::Default,
                  aStatus);

      if (colData.mColCount == 1 && aStatus.IsInlineBreakBefore()) {
        COLUMN_SET_LOG("%s: Content in the first column reports break-before!",
                       __func__);
        allFit = false;
        break;
      }

      reflowNext = aStatus.NextInFlowNeedsReflow();

      // The carried-out block-end margin of column content might be non-zero
      // when we try to find the best column balancing block size, but it should
      // never affect the size column set nor be further carried out. Set it to
      // zero.
      //
      // FIXME: For some types of fragmentation, we should carry the margin into
      // the next column. Also see
      // https://drafts.csswg.org/css-break-4/#break-margins
      //
      // FIXME: This should never happen for the last column, since it should be
      // a margin root; see nsBlockFrame::IsMarginRoot(). However, sometimes the
      // last column has an empty continuation while searching for the best
      // column balancing bsize, which prevents the last column from being a
      // margin root.
      kidDesiredSize.mCarriedOutBEndMargin.Zero();

      NS_FRAME_TRACE_REFLOW_OUT("Column::Reflow", aStatus);

      FinishReflowChild(child, PresContext(), kidDesiredSize, &kidReflowInput,
                        wm, childOrigin, containerSize,
                        ReflowChildFlags::Default);

      childContentBEnd = nsLayoutUtils::CalculateContentBEnd(wm, child);
      if (childContentBEnd > aConfig.mColBSize) {
        allFit = false;
      }
      if (childContentBEnd > availSize.BSize(wm)) {
        colData.mMaxOverflowingBSize =
            std::max(childContentBEnd, colData.mMaxOverflowingBSize);
      }

      COLUMN_SET_LOG(
          "%s: Reflowed child #%d %p: status=%s, desiredSize=(%d,%d), "
          "childContentBEnd=%d, CarriedOutBEndMargin=%d (ignored)",
          __func__, colData.mColCount, child, ToString(aStatus).c_str(),
          kidDesiredSize.ISize(wm), kidDesiredSize.BSize(wm), childContentBEnd,
          kidDesiredSize.mCarriedOutBEndMargin.Get());
    }

    contentRect.UnionRect(contentRect, child->GetRect());

    ConsiderChildOverflow(overflowRects, child);
    contentBEnd = std::max(contentBEnd, childContentBEnd);
    colData.mLastBSize = childContentBEnd;
    colData.mSumBSize += childContentBEnd;

    // Build a continuation column if necessary
    nsIFrame* kidNextInFlow = child->GetNextInFlow();

    if (aStatus.IsFullyComplete()) {
      NS_ASSERTION(!kidNextInFlow, "next in flow should have been deleted");
      child = nullptr;
      break;
    }

    // Make sure that the column has a next-in-flow. If not, we must
    // create one to hold the overflowing stuff, even if we're just
    // going to put it on our overflow list and let *our*
    // next in flow handle it.
    if (!kidNextInFlow) {
      NS_ASSERTION(aStatus.NextInFlowNeedsReflow(),
                   "We have to create a continuation, but the block doesn't "
                   "want us to reflow it?");

      // We need to create a continuing column
      kidNextInFlow = CreateNextInFlow(child);
    }

    // Make sure we reflow a next-in-flow when it switches between being
    // normal or overflow container
    if (aStatus.IsOverflowIncomplete()) {
      if (!kidNextInFlow->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        aStatus.SetNextInFlowNeedsReflow();
        reflowNext = true;
        kidNextInFlow->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
      }
    } else if (kidNextInFlow->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
      aStatus.SetNextInFlowNeedsReflow();
      reflowNext = true;
      kidNextInFlow->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
    }

    // We have reached the maximum number of columns. If we are balancing, stop
    // this reflow and continue finding the optimal balancing block-size.
    //
    // Otherwise, i.e. we are not balancing, stop this reflow and let the parent
    // of our multicol container create a next-in-flow if all of the following
    // conditions are met.
    //
    // 1) We fill columns sequentially by the request of the style, not by our
    // internal needs, i.e. aConfig.mForceAuto is false.
    //
    // We don't want to stop this reflow when we force fill the columns
    // sequentially. We usually go into this mode when giving up balancing, and
    // this is the last resort to fit all our children by creating overflow
    // columns.
    //
    // 2) In a fragmented context, our multicol container still has block-size
    // left for its next-in-flow, i.e.
    // aReflowInput.mFlags.mColumnSetWrapperHasNoBSizeLeft is false.
    //
    // Note that in a continuous context, i.e. our multicol container's
    // available block-size is unconstrained, if it has a fixed block-size
    // mColumnSetWrapperHasNoBSizeLeft is always true because nothing stops it
    // from applying all its block-size in the first-in-flow. Otherwise, i.e.
    // our multicol container has an unconstrained block-size, we shouldn't be
    // here because all our children should fit in the very first column even if
    // mColumnSetWrapperHasNoBSizeLeft is false.
    //
    // According to the definition of mColumnSetWrapperHasNoBSizeLeft, if the
    // bit is *not* set, either our multicol container has unconstrained
    // block-size, or it has a constrained block-size and has block-size left
    // for its next-in-flow. In either cases, the parent of our multicol
    // container can create a next-in-flow for the container that guaranteed to
    // have non-zero block-size for the container's children.
    //
    // Put simply, if either one of the above conditions is not met, we are
    // going to create more overflow columns until all our children are fit.
    if (colData.mColCount >= aConfig.mUsedColCount &&
        (aConfig.mIsBalancing ||
         (!aConfig.mForceAuto &&
          !aReflowInput.mFlags.mColumnSetWrapperHasNoBSizeLeft))) {
      NS_ASSERTION(aConfig.mIsBalancing ||
                       aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
                   "Why are we here if we have unlimited block-size to fill "
                   "columns sequentially.");

      // No more columns allowed here. Stop.
      aStatus.SetNextInFlowNeedsReflow();
      kidNextInFlow->MarkSubtreeDirty();
      // Move any of our leftover columns to our overflow list. Our
      // next-in-flow will eventually pick them up.
      nsFrameList continuationColumns = mFrames.TakeFramesAfter(child);
      if (continuationColumns.NotEmpty()) {
        SetOverflowFrames(std::move(continuationColumns));
      }
      child = nullptr;

      COLUMN_SET_LOG("%s: We are not going to create overflow columns.",
                     __func__);
      break;
    }

    if (PresContext()->HasPendingInterrupt()) {
      // Stop the loop now while |child| still points to the frame that bailed
      // out.  We could keep going here and condition a bunch of the code in
      // this loop on whether there's an interrupt, or even just keep going and
      // trying to reflow the blocks (even though we know they'll interrupt
      // right after their first line), but stopping now is conceptually the
      // simplest (and probably fastest) thing.
      break;
    }

    // Advance to the next column
    child = child->GetNextSibling();
    ++colData.mColCount;

    if (child) {
      childOrigin.I(wm) += aConfig.mColISize + aConfig.mColGap;

      COLUMN_SET_LOG("%s: Next childOrigin.iCoord=%d", __func__,
                     childOrigin.I(wm));
    }
  }

  if (PresContext()->CheckForInterrupt(this) &&
      HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    // Mark all our kids starting with |child| dirty

    // Note that this is a CheckForInterrupt call, not a HasPendingInterrupt,
    // because we might have interrupted while reflowing |child|, and since
    // we're about to add a dirty bit to |child| we need to make sure that
    // |this| is scheduled to have dirty bits marked on it and its ancestors.
    // Otherwise, when we go to mark dirty bits on |child|'s ancestors we'll
    // bail out immediately, since it'll already have a dirty bit.
    for (; child; child = child->GetNextSibling()) {
      child->MarkSubtreeDirty();
    }
  }

  colData.mMaxBSize = contentBEnd;
  LogicalSize contentSize = LogicalSize(wm, contentRect.Size());
  contentSize.BSize(wm) = std::max(contentSize.BSize(wm), contentBEnd);
  mLastFrameStatus = aStatus;

  if (computedBSize != NS_UNCONSTRAINEDSIZE && !HasColumnSpanSiblings()) {
    NS_ASSERTION(aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
                 "Available block-size should be constrained because it's "
                 "restricted by the computed block-size when our reflow "
                 "input is created in nsBlockFrame::ReflowBlockFrame()!");

    // If a) our parent ColumnSetWrapper has constrained block-size
    // (nsBlockFrame::ReflowBlockFrame() applies the block-size constraint
    // when creating a ReflowInput for ColumnSetFrame child); and b) we are the
    // sole ColumnSet or the last ColumnSet continuation split by column-spans
    // in a ColumnSetWrapper, extend our block-size to consume the available
    // block-size so that the column-rules are drawn to the content block-end
    // edge of the multicol container.
    contentSize.BSize(wm) =
        std::max(contentSize.BSize(wm), aReflowInput.AvailableBSize());
  }

  aDesiredSize.SetSize(wm, contentSize);
  aDesiredSize.mOverflowAreas = overflowRects;
  aDesiredSize.UnionOverflowAreasWithDesiredBounds();

  // In vertical-rl mode, make a second pass if necessary to reposition the
  // columns with the correct container width. (In other writing modes,
  // correct containerSize was not required for column positioning so we don't
  // need this fixup.)
  //
  // RTL column positions also depend on ColumnSet's actual contentSize. We need
  // this fixup, too.
  if ((wm.IsVerticalRL() || isRTL) &&
      containerSize.width != contentSize.Width(wm)) {
    const nsSize finalContainerSize = aDesiredSize.PhysicalSize();
    OverflowAreas overflowRects;
    for (nsIFrame* child : mFrames) {
      // Get the logical position as set previously using a provisional or
      // dummy containerSize, and reset with the correct container size.
      child->SetPosition(wm, child->GetLogicalPosition(wm, containerSize),
                         finalContainerSize);
      ConsiderChildOverflow(overflowRects, child);
    }
    aDesiredSize.mOverflowAreas = overflowRects;
    aDesiredSize.UnionOverflowAreasWithDesiredBounds();
  }

  colData.mFeasible = allFit && aStatus.IsFullyComplete();

  COLUMN_SET_LOG(
      "%s: Done column reflow pass: %s, mMaxBSize=%d, mSumBSize=%d, "
      "mLastBSize=%d, mMaxOverflowingBSize=%d",
      __func__, colData.mFeasible ? "Feasible :)" : "Infeasible :(",
      colData.mMaxBSize, colData.mSumBSize, colData.mLastBSize,
      colData.mMaxOverflowingBSize);

  return colData;
}

void nsColumnSetFrame::DrainOverflowColumns() {
  // First grab the prev-in-flows overflows and reparent them to this
  // frame.
  nsPresContext* presContext = PresContext();
  nsColumnSetFrame* prev = static_cast<nsColumnSetFrame*>(GetPrevInFlow());
  if (prev) {
    AutoFrameListPtr overflows(presContext, prev->StealOverflowFrames());
    if (overflows) {
      mFrames.InsertFrames(this, nullptr, std::move(*overflows));
    }
  }

  // Now pull back our own overflows and append them to our children.
  // We don't need to reparent them since we're already their parent.
  AutoFrameListPtr overflows(presContext, StealOverflowFrames());
  if (overflows) {
    // We're already the parent for these frames, so no need to set
    // their parent again.
    mFrames.AppendFrames(nullptr, std::move(*overflows));
  }
}

void nsColumnSetFrame::FindBestBalanceBSize(const ReflowInput& aReflowInput,
                                            nsPresContext* aPresContext,
                                            ReflowConfig& aConfig,
                                            ColumnBalanceData aColData,
                                            ReflowOutput& aDesiredSize,
                                            bool aUnboundedLastColumn,
                                            nsReflowStatus& aStatus) {
  MOZ_ASSERT(aConfig.mIsBalancing,
             "Why are we here if we are not balancing columns?");

  const nscoord availableContentBSize = aReflowInput.AvailableBSize();

  // Termination of the algorithm below is guaranteed because
  // aConfig.knownFeasibleBSize - aConfig.knownInfeasibleBSize decreases in
  // every iteration.
  int32_t iterationCount = 1;

  // We set this flag when we detect that we may contain a frame
  // that can break anywhere (thus foiling the linear decrease-by-one
  // search)
  bool maybeContinuousBreakingDetected = false;
  bool possibleOptimalBSizeDetected = false;

  // This is the extra block-size added to the optimal column block-size
  // estimation which is calculated in the while-loop by dividing
  // aColData.mSumBSize into N columns.
  //
  // The constant is arbitrary. We use a half of line-height first. In case a
  // column container uses *zero* (or a very small) line-height, use a half of
  // default line-height 1140/2 = 570 app units as the minimum value. Otherwise
  // we might take more than necessary iterations before finding a feasible
  // block-size.
  nscoord extraBlockSize = std::max(570, aReflowInput.GetLineHeight() / 2);

  // We use divide-by-N to estimate the optimal column block-size only if the
  // last column's available block-size is unbounded.
  bool foundFeasibleBSizeCloserToBest = !aUnboundedLastColumn;

  // Stop the binary search when the difference of the feasible and infeasible
  // block-size is within this gap. Here we use one device pixel.
  const int32_t gapToStop = aPresContext->DevPixelsToAppUnits(1);

  while (!aPresContext->HasPendingInterrupt()) {
    nscoord lastKnownFeasibleBSize = aConfig.mKnownFeasibleBSize;

    // Record what we learned from the last reflow
    if (aColData.mFeasible) {
      // mMaxBSize is feasible. Also, mLastBalanceBSize is feasible.
      aConfig.mKnownFeasibleBSize =
          std::min(aConfig.mKnownFeasibleBSize, aColData.mMaxBSize);
      aConfig.mKnownFeasibleBSize =
          std::min(aConfig.mKnownFeasibleBSize, mLastBalanceBSize);

      // Furthermore, no block-size less than the block-size of the last
      // column can ever be feasible. (We might be able to reduce the
      // block-size of a non-last column by moving content to a later column,
      // but we can't do that with the last column.)
      if (aColData.mColCount == aConfig.mUsedColCount) {
        aConfig.mKnownInfeasibleBSize =
            std::max(aConfig.mKnownInfeasibleBSize, aColData.mLastBSize - 1);
      }
    } else {
      aConfig.mKnownInfeasibleBSize =
          std::max(aConfig.mKnownInfeasibleBSize, mLastBalanceBSize);

      // If a column didn't fit in its available block-size, then its current
      // block-size must be the minimum block-size for unbreakable content in
      // the column, and therefore no smaller block-size can be feasible.
      aConfig.mKnownInfeasibleBSize = std::max(
          aConfig.mKnownInfeasibleBSize, aColData.mMaxOverflowingBSize - 1);

      if (aUnboundedLastColumn) {
        // The last column is unbounded, so all content got reflowed, so the
        // mMaxBSize is feasible.
        aConfig.mKnownFeasibleBSize =
            std::min(aConfig.mKnownFeasibleBSize, aColData.mMaxBSize);

        NS_ASSERTION(mLastFrameStatus.IsComplete(),
                     "Last column should be complete if the available "
                     "block-size is unconstrained!");
      }
    }

    COLUMN_SET_LOG(
        "%s: this=%p, mKnownInfeasibleBSize=%d, mKnownFeasibleBSize=%d",
        __func__, this, aConfig.mKnownInfeasibleBSize,
        aConfig.mKnownFeasibleBSize);

    if (aConfig.mKnownInfeasibleBSize >= aConfig.mKnownFeasibleBSize - 1) {
      // aConfig.mKnownFeasibleBSize is where we want to be. This can happen in
      // the very first iteration when a column container solely has a tall
      // unbreakable child that overflows the container.
      break;
    }

    if (aConfig.mKnownInfeasibleBSize >= availableContentBSize) {
      // There's no feasible block-size to fit our contents. We may need to
      // reflow one more time after this loop.
      break;
    }

    const nscoord gap =
        aConfig.mKnownFeasibleBSize - aConfig.mKnownInfeasibleBSize;
    if (gap <= gapToStop && possibleOptimalBSizeDetected) {
      // We detected a possible optimal block-size in the last iteration. If it
      // is infeasible, we may need to reflow one more time after this loop.
      break;
    }

    if (lastKnownFeasibleBSize - aConfig.mKnownFeasibleBSize == 1) {
      // We decreased the feasible block-size by one twip only. This could
      // indicate that there is a continuously breakable child frame
      // that we are crawling through.
      maybeContinuousBreakingDetected = true;
    }

    nscoord nextGuess = aConfig.mKnownInfeasibleBSize + gap / 2;
    if (aConfig.mKnownFeasibleBSize - nextGuess < extraBlockSize &&
        !maybeContinuousBreakingDetected) {
      // We're close to our target, so just try shrinking just the
      // minimum amount that will cause one of our columns to break
      // differently.
      nextGuess = aConfig.mKnownFeasibleBSize - 1;
    } else if (!foundFeasibleBSizeCloserToBest) {
      // Make a guess by dividing mSumBSize into N columns and adding
      // extraBlockSize to try to make it on the feasible side.
      nextGuess = aColData.mSumBSize / aConfig.mUsedColCount + extraBlockSize;
      // Sanitize it
      nextGuess = std::clamp(nextGuess, aConfig.mKnownInfeasibleBSize + 1,
                             aConfig.mKnownFeasibleBSize - 1);
      // We keep doubling extraBlockSize in every iteration until we find a
      // feasible guess.
      extraBlockSize *= 2;
    } else if (aConfig.mKnownFeasibleBSize == NS_UNCONSTRAINEDSIZE) {
      // This can happen when we had a next-in-flow so we didn't
      // want to do an unbounded block-size measuring step. Let's just increase
      // from the infeasible block-size by some reasonable amount.
      nextGuess = aConfig.mKnownInfeasibleBSize * 2 + extraBlockSize;
    } else if (gap <= gapToStop) {
      // Floor nextGuess to the greatest multiple of gapToStop below or equal to
      // mKnownFeasibleBSize.
      nextGuess = aConfig.mKnownFeasibleBSize / gapToStop * gapToStop;
      possibleOptimalBSizeDetected = true;
    }

    // Don't bother guessing more than our block-size constraint.
    nextGuess = std::min(availableContentBSize, nextGuess);

    COLUMN_SET_LOG("%s: Choosing next guess=%d, iteration=%d", __func__,
                   nextGuess, iterationCount);
    ++iterationCount;

    aConfig.mColBSize = nextGuess;

    aUnboundedLastColumn = false;
    MarkPrincipalChildrenDirty();
    aColData =
        ReflowColumns(aDesiredSize, aReflowInput, aStatus, aConfig, false);

    if (!foundFeasibleBSizeCloserToBest && aColData.mFeasible) {
      foundFeasibleBSizeCloserToBest = true;
    }
  }

  if (!aColData.mFeasible && !aPresContext->HasPendingInterrupt()) {
    // We need to reflow one more time at the feasible block-size to
    // get a valid layout.
    if (aConfig.mKnownInfeasibleBSize >= availableContentBSize) {
      aConfig.mColBSize = availableContentBSize;
      if (mLastBalanceBSize == availableContentBSize) {
        // If we end up here, we have a constrained available content
        // block-size, and our last column's block-size exceeds it. Also, if
        // this is the first balancing iteration, the last column is given
        // unconstrained available block-size, so it has a fully complete
        // reflow status. Therefore, we always want to reflow again at the
        // available content block-size to get a valid layout and a correct
        // reflow status (likely an *incomplete* status) so that our column
        // container can be fragmented if needed.

        if (aReflowInput.mFlags.mColumnSetWrapperHasNoBSizeLeft) {
          // If our column container has a constrained block-size (either in a
          // paginated context or in a nested column container), and is going
          // to consume all its computed block-size in this fragment, then our
          // column container has no block-size left to contain our
          // next-in-flows. We have to give up balancing, and create our
          // own overflow columns.
          //
          // We don't want to create overflow columns immediately when our
          // content doesn't fit since this changes our reflow status from
          // incomplete to complete. Valid reasons include 1) the outer column
          // container might do column balancing, and it can enlarge the
          // available content block-size so that the nested one could fit its
          // content in next balancing iteration; or 2) the outer column
          // container is filling columns sequentially, and may have more
          // inline-size to create more column boxes for the nested column
          // container's next-in-flows.
          aConfig = ChooseColumnStrategy(aReflowInput, true);
        }
      }
    } else {
      aConfig.mColBSize = aConfig.mKnownFeasibleBSize;
    }

    // This is our last attempt to reflow. If our column container's available
    // block-size is unconstrained, make sure that the last column is
    // allowed to have arbitrary block-size here, even though we were
    // balancing. Otherwise we'd have to split, and it's not clear what we'd
    // do with that.
    COLUMN_SET_LOG("%s: Last attempt to call ReflowColumns", __func__);
    aConfig.mIsLastBalancingReflow = true;
    const bool forceUnboundedLastColumn =
        aReflowInput.mParentReflowInput->AvailableBSize() ==
        NS_UNCONSTRAINEDSIZE;
    MarkPrincipalChildrenDirty();
    ReflowColumns(aDesiredSize, aReflowInput, aStatus, aConfig,
                  forceUnboundedLastColumn);
  }
}

void nsColumnSetFrame::Reflow(nsPresContext* aPresContext,
                              ReflowOutput& aDesiredSize,
                              const ReflowInput& aReflowInput,
                              nsReflowStatus& aStatus) {
  MarkInReflow();
  // Don't support interruption in columns
  nsPresContext::InterruptPreventer noInterrupts(aPresContext);

  DO_GLOBAL_REFLOW_COUNT("nsColumnSetFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  MOZ_ASSERT(aReflowInput.mCBReflowInput->mFrame->StyleColumn()
                 ->IsColumnContainerStyle(),
             "The column container should have relevant column styles!");
  MOZ_ASSERT(aReflowInput.mParentReflowInput->mFrame->IsColumnSetWrapperFrame(),
             "The column container should be ColumnSetWrapperFrame!");
  MOZ_ASSERT(
      aReflowInput.ComputedLogicalBorderPadding(aReflowInput.GetWritingMode())
          .IsAllZero(),
      "Only the column container can have border and padding!");
  MOZ_ASSERT(
      GetChildList(FrameChildListID::OverflowContainers).IsEmpty() &&
          GetChildList(FrameChildListID::ExcessOverflowContainers).IsEmpty(),
      "ColumnSetFrame should store overflow containers in principal "
      "child list!");

  //------------ Handle Incremental Reflow -----------------

  const bool isNestedMulticol =
      aReflowInput.mParentReflowInput->mFrame->HasAnyStateBits(
          NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
  COLUMN_SET_LOG("%s: Begin Reflow: this=%p, is nested multicol? %s", __func__,
                 this, YesOrNo(isNestedMulticol));

  // If inline size is unconstrained, set aForceAuto to true to allow
  // the columns to expand in the inline direction. (This typically
  // happens in orthogonal flows where the inline direction is the
  // container's block direction).
  ReflowConfig config = ChooseColumnStrategy(
      aReflowInput, aReflowInput.ComputedISize() == NS_UNCONSTRAINEDSIZE);

  const bool shouldDoMeasuringReflow = [&]() {
    if (!aPresContext->FragmentainerAwarePositioningEnabled()) {
      return false;
    }
    if (isNestedMulticol) {
      // Only the top-level multicol can initiate a measuring reflow. If we are
      // a nested multicol, perform a measuring reflow only when the top-level
      // one is doing it.
      return aReflowInput.mFlags.mIsInFragmentainerMeasuringReflow;
    }
    // Below is logic for top-level multicols.
    if (GetPrevInFlow()) {
      // A measuring reflow is only needed on first-in-flow.
      return false;
    }
    // Only do a measuring reflow if there are absolutely positioned descendants
    // since the purpose is to compute their unfragmented positions.
    return nsLayoutUtils::HasAbsolutelyPositionedDescendants(this);
  }();
  if (shouldDoMeasuringReflow) {
    // Reflow the content with an unconstrained available block-size, to
    // compute the unfragmented position of the absolutely positioned
    // descendants.
    if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
      // We need to reflow everything when we are in an incremental measuring
      // reflow.
      MarkPrincipalChildrenDirty();
    }

    ReflowConfig measuringConfig = config;
    measuringConfig.mColBSize = NS_UNCONSTRAINEDSIZE;
    measuringConfig.mIsInMeasuringReflow = true;

    COLUMN_SET_LOG(
        "%s: Doing column measuring reflow with an unconstrained block-size",
        __func__);
    ReflowColumns(aDesiredSize, aReflowInput, aStatus, measuringConfig, true);

    if (isNestedMulticol) {
      // Nested multicols return early after measuring reflow, skipping the
      // subsequent normal reflow. During the top-level multicol's measuring
      // reflow, the top-level multicol hasn't applied its column sizing
      // constraints yet, so our available block-size here is not the actual
      // one. We'll do our normal reflow later when the top-level does its
      // normal reflow.
      COLUMN_SET_LOG(
          "%s: Nested multicol returns early after the column measuring reflow",
          __func__);
      return;
    }

    // Mark columns dirty for normal reflow below.
    MarkPrincipalChildrenDirty();
  }

  // If balancing, then we allow the last column to grow to unbounded
  // block-size during the first reflow. This gives us a way to estimate
  // what the average column block-size should be, because we can measure
  // the block-size of all the columns and sum them up. But don't do this
  // if we have a next in flow because we don't want to suck all its
  // content back here and then have to push it out again!
  nsIFrame* nextInFlow = GetNextInFlow();
  bool unboundedLastColumn = config.mIsBalancing && !nextInFlow;
  COLUMN_SET_LOG("%s: Doing column normal reflow", __func__);
  const ColumnBalanceData colData = ReflowColumns(
      aDesiredSize, aReflowInput, aStatus, config, unboundedLastColumn);

  // If we're not balancing, then we're already done, since we should have
  // reflown all of our children, and there is no need for a binary search to
  // determine proper column block-size.
  if (config.mIsBalancing && !aPresContext->HasPendingInterrupt()) {
    COLUMN_SET_LOG("%s: Doing the column balancing reflow", __func__);
    FindBestBalanceBSize(aReflowInput, aPresContext, config, colData,
                         aDesiredSize, unboundedLastColumn, aStatus);
  }

  if (aPresContext->HasPendingInterrupt() &&
      aReflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
    // In this situation, we might be lying about our reflow status, because
    // our last kid (the one that got interrupted) was incomplete.  Fix that.
    aStatus.Reset();
  }

  NS_ASSERTION(aStatus.IsFullyComplete() ||
                   aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
               "Column set should be complete if the available block-size is "
               "unconstrained");

  MOZ_ASSERT(!HasAbsolutelyPositionedChildren(),
             "ColumnSetWrapperFrame should be the abs.pos container!");
  FinishAndStoreOverflow(&aDesiredSize, aReflowInput.mStyleDisplay);

  COLUMN_SET_LOG("%s: End Reflow: this=%p", __func__, this);
}

void nsColumnSetFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                        const nsDisplayListSet& aLists) {
  DisplayBorderBackgroundOutline(aBuilder, aLists);

  if (IsVisibleForPainting()) {
    aLists.BorderBackground()->AppendNewToTop<nsDisplayColumnRule>(aBuilder,
                                                                   this);
  }

  if (HidesContent()) {
    return;
  }

  // Our children won't have backgrounds so it doesn't matter where we put them.
  for (nsIFrame* f : mFrames) {
    BuildDisplayListForChild(aBuilder, f, aLists);
  }
}

void nsColumnSetFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  // Everything in mFrames is continuations of the first thing in mFrames.
  nsIFrame* column = mFrames.FirstChild();

  // We might not have any columns, apparently?
  if (!column) {
    return;
  }

  MOZ_ASSERT(
      column->Style()->GetPseudoType() == PseudoStyleType::MozColumnContent,
      "What sort of child is this?");
  aResult.AppendElement(OwnedAnonBox(column));
}

Maybe<nscoord> nsColumnSetFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  Maybe<nscoord> result;
  for (const auto* kid : mFrames) {
    auto kidBaseline =
        kid->GetNaturalBaselineBOffset(aWM, aBaselineGroup, aExportContext);
    if (!kidBaseline) {
      continue;
    }
    // The kid frame may not necessarily be aligned with the columnset frame.
    LogicalRect kidRect{aWM, kid->GetLogicalNormalPosition(aWM, GetSize()),
                        kid->GetLogicalSize(aWM)};
    if (aBaselineGroup == BaselineSharingGroup::First) {
      *kidBaseline += kidRect.BStart(aWM);
    } else {
      *kidBaseline += (GetLogicalSize().BSize(aWM) - kidRect.BEnd(aWM));
    }
    // Take the smallest of the baselines (i.e. Closest to border-block-start
    // for `BaselineSharingGroup::First`, border-block-end for
    // `BaselineSharingGroup::Last`)
    if (!result || *kidBaseline < *result) {
      result = kidBaseline;
    }
  }
  return result;
}

#ifdef DEBUG
void nsColumnSetFrame::SetInitialChildList(ChildListID aListID,
                                           nsFrameList&& aChildList) {
  MOZ_ASSERT(aListID != FrameChildListID::Principal || aChildList.OnlyChild(),
             "initial principal child list must have exactly one child");
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
}

void nsColumnSetFrame::AppendFrames(ChildListID aListID,
                                    nsFrameList&& aFrameList) {
  MOZ_CRASH("unsupported operation");
}

void nsColumnSetFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                    const nsLineList::iterator* aPrevFrameLine,
                                    nsFrameList&& aFrameList) {
  MOZ_CRASH("unsupported operation");
}

void nsColumnSetFrame::RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) {
  MOZ_CRASH("unsupported operation");
}
#endif
