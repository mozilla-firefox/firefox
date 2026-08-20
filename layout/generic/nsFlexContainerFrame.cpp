/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object for CSS "display: flex" */

#include "nsFlexContainerFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/Baseline.h"
#include "mozilla/CSSOrderAwareFrameIterator.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/WritingModes.h"
#include "nsBlockFrame.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsDisplayList.h"
#include "nsFieldSetFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::layout;

// Convenience typedefs for helper classes that we forward-declare in .h file
// (so that nsFlexContainerFrame methods can use them as parameters):
using FlexItem = nsFlexContainerFrame::FlexItem;
using FlexLine = nsFlexContainerFrame::FlexLine;
using FlexboxAxisTracker = nsFlexContainerFrame::FlexboxAxisTracker;
using StrutInfo = nsFlexContainerFrame::StrutInfo;
using CachedBAxisMeasurement = nsFlexContainerFrame::CachedBAxisMeasurement;
using CachedFlexItemData = nsFlexContainerFrame::CachedFlexItemData;

static mozilla::LazyLogModule gFlexContainerLog("FlexContainer");

// FLEX_LOG is a top-level general log print.
#define FLEX_LOG(message, ...) \
  MOZ_LOG(gFlexContainerLog, LogLevel::Debug, (message, ##__VA_ARGS__));

// FLEX_ITEM_LOG is a top-level log print for flex item.
#define FLEX_ITEM_LOG(item_frame, message, ...) \
  MOZ_LOG(gFlexContainerLog, LogLevel::Debug,   \
          ("Flex item %p: " message, item_frame, ##__VA_ARGS__));

// FLEX_LOGV is a verbose log print with built-in two spaces indentation. The
// convention to use FLEX_LOGV is that FLEX_LOGV statements should generally be
// preceded by one FLEX_LOG or FLEX_ITEM_LOG so that there's no need to repeat
// information presented in the preceding LOG statement. If you want extra level
// of indentation, just add two extra spaces at the start of the message string.
#define FLEX_LOGV(message, ...) \
  MOZ_LOG(gFlexContainerLog, LogLevel::Verbose, ("  " message, ##__VA_ARGS__));

// Returns the OrderState enum we should pass to CSSOrderAwareFrameIterator
// (depending on whether aFlexContainer has
// NS_STATE_FLEX_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER state bit).
static CSSOrderAwareFrameIterator::OrderState OrderStateForIter(
    const nsFlexContainerFrame* aFlexContainer) {
  return aFlexContainer->HasAnyStateBits(
             NS_STATE_FLEX_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER)
             ? CSSOrderAwareFrameIterator::OrderState::Ordered
             : CSSOrderAwareFrameIterator::OrderState::Unordered;
}

// Returns the OrderingProperty enum that we should pass to
// CSSOrderAwareFrameIterator (depending on whether it's a legacy box).
static CSSOrderAwareFrameIterator::OrderingProperty OrderingPropertyForIter(
    const nsFlexContainerFrame* aFlexContainer) {
  return aFlexContainer->IsLegacyWebkitBox()
             ? CSSOrderAwareFrameIterator::OrderingProperty::BoxOrdinalGroup
             : CSSOrderAwareFrameIterator::OrderingProperty::Order;
}

// Returns the "align-items" value that's equivalent to the legacy "box-align"
// value in the given style struct.
static StyleAlignFlags ConvertLegacyStyleToAlignItems(
    const nsStyleXUL* aStyleXUL) {
  // -[moz|webkit]-box-align corresponds to modern "align-items"
  switch (aStyleXUL->mBoxAlign) {
    case StyleBoxAlign::Stretch:
      return StyleAlignFlags::STRETCH;
    case StyleBoxAlign::Start:
      return StyleAlignFlags::FLEX_START;
    case StyleBoxAlign::Center:
      return StyleAlignFlags::CENTER;
    case StyleBoxAlign::Baseline:
      return StyleAlignFlags::BASELINE;
    case StyleBoxAlign::End:
      return StyleAlignFlags::FLEX_END;
  }

  MOZ_ASSERT_UNREACHABLE("Unrecognized mBoxAlign enum value");
  // Fall back to default value of "align-items" property:
  return StyleAlignFlags::STRETCH;
}

// Returns the "justify-content" value that's equivalent to the legacy
// "box-pack" value in the given style struct.
static StyleContentDistribution ConvertLegacyStyleToJustifyContent(
    const nsStyleXUL* aStyleXUL) {
  // -[moz|webkit]-box-pack corresponds to modern "justify-content"
  switch (aStyleXUL->mBoxPack) {
    case StyleBoxPack::Start:
      return {StyleAlignFlags::FLEX_START};
    case StyleBoxPack::Center:
      return {StyleAlignFlags::CENTER};
    case StyleBoxPack::End:
      return {StyleAlignFlags::FLEX_END};
    case StyleBoxPack::Justify:
      return {StyleAlignFlags::SPACE_BETWEEN};
  }

  MOZ_ASSERT_UNREACHABLE("Unrecognized mBoxPack enum value");
  // Fall back to default value of "justify-content" property:
  return {StyleAlignFlags::FLEX_START};
}

// Check if the size is auto or it is a keyword in the block axis.
// |aIsInline| should represent whether aSize is in the inline axis, from the
// perspective of the writing mode of the flex item that the size comes from.
//
// max-content and min-content should behave as property's initial value.
// Bug 567039: We treat -moz-fit-content and -moz-available as property's
// initial value for now.
static inline bool IsAutoOrEnumOnBSize(const StyleSize& aSize, bool aIsInline) {
  return aSize.IsAuto() || (!aIsInline && !aSize.IsLengthPercentage());
}

// Returns true if the flex container should be treated as a single-line
// container.
static bool IsSingleLine(const nsIFrame* aFlexContainer,
                         const nsStylePosition* aStylePos) {
  MOZ_ASSERT(aFlexContainer->IsFlexContainerFrame());

  if (aFlexContainer->IsLegacyWebkitBox()) {
    // For legacy -webkit-{inline-}box, ignore the flex-wrap property.
    // These containers are always treated as single-line.
    return true;
  }
  return aStylePos->mFlexWrap == StyleFlexWrap::NOWRAP;
}

// Encapsulates our flex container's main & cross axes. This class is backed by
// a FlexboxAxisInfo helper member variable, and it adds some convenience APIs
// on top of what that struct offers.
class MOZ_STACK_CLASS nsFlexContainerFrame::FlexboxAxisTracker {
 public:
  explicit FlexboxAxisTracker(const nsFlexContainerFrame* aFlexContainer);

  // Accessors:
  LogicalAxis MainAxis() const {
    return IsRowOriented() ? LogicalAxis::Inline : LogicalAxis::Block;
  }
  LogicalAxis CrossAxis() const {
    return IsRowOriented() ? LogicalAxis::Block : LogicalAxis::Inline;
  }

  LogicalSide MainAxisStartSide() const;
  LogicalSide MainAxisEndSide() const {
    return GetOppositeSide(MainAxisStartSide());
  }

  LogicalSide CrossAxisStartSide() const;
  LogicalSide CrossAxisEndSide() const {
    return GetOppositeSide(CrossAxisStartSide());
  }

  mozilla::Side MainAxisPhysicalStartSide() const {
    return mWM.PhysicalSide(MainAxisStartSide());
  }
  mozilla::Side MainAxisPhysicalEndSide() const {
    return mWM.PhysicalSide(MainAxisEndSide());
  }

  mozilla::Side CrossAxisPhysicalStartSide() const {
    return mWM.PhysicalSide(CrossAxisStartSide());
  }
  mozilla::Side CrossAxisPhysicalEndSide() const {
    return mWM.PhysicalSide(CrossAxisEndSide());
  }

  // Returns the flex container's writing mode.
  WritingMode GetWritingMode() const { return mWM; }

  // Returns true if our main axis is in the reverse direction of our
  // writing mode's corresponding axis. (From 'flex-direction: *-reverse')
  bool IsMainAxisReversed() const { return mAxisInfo.mIsMainAxisReversed; }
  // Returns true if our cross axis is in the reverse direction of our
  // writing mode's corresponding axis. (From 'flex-wrap: *-reverse')
  bool IsCrossAxisReversed() const { return mAxisInfo.mIsCrossAxisReversed; }

  bool IsRowOriented() const { return mAxisInfo.mIsRowOriented; }
  bool IsColumnOriented() const { return !IsRowOriented(); }

  // aSize is expected to match the flex container's WritingMode.
  nscoord MainComponent(const LogicalSize& aSize) const {
    return IsRowOriented() ? aSize.ISize(mWM) : aSize.BSize(mWM);
  }
  int32_t MainComponent(const LayoutDeviceIntSize& aIntSize) const {
    return IsMainAxisHorizontal() ? aIntSize.width : aIntSize.height;
  }

  // aSize is expected to match the flex container's WritingMode.
  nscoord CrossComponent(const LogicalSize& aSize) const {
    return IsRowOriented() ? aSize.BSize(mWM) : aSize.ISize(mWM);
  }
  int32_t CrossComponent(const LayoutDeviceIntSize& aIntSize) const {
    return IsMainAxisHorizontal() ? aIntSize.height : aIntSize.width;
  }

  // NOTE: aMargin is expected to use the flex container's WritingMode.
  nscoord MarginSizeInMainAxis(const LogicalMargin& aMargin) const {
    // If we're row-oriented, our main axis is the inline axis.
    return IsRowOriented() ? aMargin.IStartEnd(mWM) : aMargin.BStartEnd(mWM);
  }
  nscoord MarginSizeInCrossAxis(const LogicalMargin& aMargin) const {
    // If we're row-oriented, our cross axis is the block axis.
    return IsRowOriented() ? aMargin.BStartEnd(mWM) : aMargin.IStartEnd(mWM);
  }

  /**
   * Converts a "flex-relative" point (a main-axis & cross-axis coordinate)
   * into a LogicalPoint, using the flex container's writing mode.
   *
   *  @arg aMainCoord  The main-axis coordinate -- i.e an offset from the
   *                   main-start edge of the flex container's content box.
   *  @arg aCrossCoord The cross-axis coordinate -- i.e an offset from the
   *                   cross-start edge of the flex container's content box.
   *  @arg aContainerMainSize  The main size of flex container's content box.
   *  @arg aContainerCrossSize The cross size of flex container's content box.
   *  @return A LogicalPoint, with the flex container's writing mode, that
   *          represents the same position. The logical coordinates are
   *          relative to the flex container's content box.
   */
  LogicalPoint LogicalPointFromFlexRelativePoint(
      nscoord aMainCoord, nscoord aCrossCoord, nscoord aContainerMainSize,
      nscoord aContainerCrossSize) const {
    nscoord logicalCoordInMainAxis =
        IsMainAxisReversed() ? aContainerMainSize - aMainCoord : aMainCoord;
    nscoord logicalCoordInCrossAxis =
        IsCrossAxisReversed() ? aContainerCrossSize - aCrossCoord : aCrossCoord;

    return IsRowOriented() ? LogicalPoint(mWM, logicalCoordInMainAxis,
                                          logicalCoordInCrossAxis)
                           : LogicalPoint(mWM, logicalCoordInCrossAxis,
                                          logicalCoordInMainAxis);
  }

  /**
   * Converts a "flex-relative" size (a main-axis & cross-axis size)
   * into a LogicalSize, using the flex container's writing mode.
   *
   *  @arg aMainSize  The main-axis size.
   *  @arg aCrossSize The cross-axis size.
   *  @return A LogicalSize, with the flex container's writing mode, that
   *          represents the same size.
   */
  LogicalSize LogicalSizeFromFlexRelativeSizes(nscoord aMainSize,
                                               nscoord aCrossSize) const {
    return IsRowOriented() ? LogicalSize(mWM, aMainSize, aCrossSize)
                           : LogicalSize(mWM, aCrossSize, aMainSize);
  }

  /**
   * Converts a "flex-relative" ascent (the distance from the flex container's
   * content-box cross-start edge to its baseline) into a logical ascent (the
   * distance from the flex container's content-box block-start edge to its
   * baseline).
   */
  nscoord LogicalAscentFromFlexRelativeAscent(
      nscoord aFlexRelativeAscent, nscoord aContentBoxCrossSize) const {
    return (IsCrossAxisReversed() ? aContentBoxCrossSize - aFlexRelativeAscent
                                  : aFlexRelativeAscent);
  }

  bool IsMainAxisHorizontal() const {
    // If we're row-oriented, and our writing mode is NOT vertical,
    // or we're column-oriented and our writing mode IS vertical,
    // then our main axis is horizontal. This handles all cases:
    return IsRowOriented() != mWM.IsVertical();
  }

  // Returns true if this flex item's inline axis in aItemWM is parallel (or
  // antiparallel) to the container's main axis. Returns false, otherwise.
  //
  // Note: this is a helper used before constructing FlexItem. Inside of flex
  // reflow code, FlexItem::IsInlineAxisMainAxis() is equivalent & more optimal.
  bool IsInlineAxisMainAxis(WritingMode aItemWM) const {
    return IsRowOriented() != GetWritingMode().IsOrthogonalTo(aItemWM);
  }

  // Maps justify-*: 'left' or 'right' to 'start' or 'end'.
  StyleAlignFlags ResolveJustifyLeftRight(const StyleAlignFlags& aFlags) const {
    MOZ_ASSERT(
        aFlags == StyleAlignFlags::LEFT || aFlags == StyleAlignFlags::RIGHT,
        "This helper accepts only 'LEFT' or 'RIGHT' flags!");

    const auto wm = GetWritingMode();
    const bool isJustifyLeft = aFlags == StyleAlignFlags::LEFT;
    if (IsColumnOriented()) {
      if (!wm.IsVertical()) {
        // Container's alignment axis (main axis) is *not* parallel to the
        // line-left <-> line-right axis or the physical left <-> physical right
        // axis, so we map both 'left' and 'right' to 'start'.
        return StyleAlignFlags::START;
      }

      MOZ_ASSERT(wm.PhysicalAxis(MainAxis()) == PhysicalAxis::Horizontal,
                 "Vertical column-oriented flex container's main axis should "
                 "be parallel to physical left <-> right axis!");
      // Map 'left' or 'right' to 'start' or 'end', depending on its block flow
      // direction.
      return isJustifyLeft == wm.IsVerticalLR() ? StyleAlignFlags::START
                                                : StyleAlignFlags::END;
    }

    MOZ_ASSERT(MainAxis() == LogicalAxis::Inline,
               "Row-oriented flex container's main axis should be parallel to "
               "line-left <-> line-right axis!");

    // If we get here, we're operating on the flex container's inline axis,
    // so we map 'left' to whichever of 'start' or 'end' corresponds to the
    // *line-relative* left side; and similar for 'right'.
    return isJustifyLeft == wm.IsBidiLTR() ? StyleAlignFlags::START
                                           : StyleAlignFlags::END;
  }

  // Delete copy-constructor & reassignment operator, to prevent accidental
  // (unnecessary) copying.
  FlexboxAxisTracker(const FlexboxAxisTracker&) = delete;
  FlexboxAxisTracker& operator=(const FlexboxAxisTracker&) = delete;

 private:
  const WritingMode mWM;  // The flex container's writing mode.
  const FlexboxAxisInfo mAxisInfo;
};

/**
 * Represents a flex item.
 * Includes the various pieces of input that the Flexbox Layout Algorithm uses
 * to resolve a flexible width.
 */
class nsFlexContainerFrame::FlexItem final {
 public:
  // Normal constructor:
  FlexItem(ReflowInput& aFlexItemReflowInput, float aFlexGrow,
           float aFlexShrink, nscoord aFlexBaseSize, nscoord aMainMinSize,
           nscoord aMainMaxSize, nscoord aTentativeCrossSize,
           nscoord aCrossMinSize, nscoord aCrossMaxSize,
           const FlexboxAxisTracker& aAxisTracker);

  // Simplified constructor, to be used only for generating "struts":
  // (NOTE: This "strut" constructor uses the *container's* writing mode, which
  // we'll use on this FlexItem instead of the child frame's real writing mode.
  // This is fine - it doesn't matter what writing mode we use for a
  // strut, since it won't render any content and we already know its size.)
  FlexItem(nsIFrame* aChildFrame, nscoord aCrossSize, WritingMode aContainerWM,
           const FlexboxAxisTracker& aAxisTracker);

  // Clone existing FlexItem for its underlying frame's continuation.
  // @param aContinuation a continuation in our next-in-flow chain.
  FlexItem CloneFor(nsIFrame* const aContinuation) const {
    MOZ_ASSERT(Frame() == aContinuation->FirstInFlow(),
               "aContinuation should be in aItem's continuation chain!");
    FlexItem item(*this);
    item.mFrame = aContinuation;
    item.mHadMeasuringReflow = false;
    return item;
  }

  // Accessors
  nsIFrame* Frame() const { return mFrame; }
  nscoord FlexBaseSize() const { return mFlexBaseSize; }

  nscoord MainMinSize() const {
    MOZ_ASSERT(!mNeedsMinSizeAutoResolution,
               "Someone's using an unresolved 'auto' main min-size");
    return mMainMinSize;
  }
  nscoord MainMaxSize() const { return mMainMaxSize; }

  // Note: These return the main-axis position and size of our *content box*.
  nscoord MainSize() const { return mMainSize; }
  nscoord MainPosition() const { return mMainPosn; }

  nscoord CrossMinSize() const { return mCrossMinSize; }
  nscoord CrossMaxSize() const { return mCrossMaxSize; }

  // Note: These return the cross-axis position and size of our *content box*.
  nscoord CrossSize() const { return mCrossSize; }
  nscoord CrossPosition() const { return mCrossPosn; }

  // Lazy getter for mAscent or mAscentForLast.
  nscoord ResolvedAscent(bool aUseFirstBaseline) const {
    // XXX We should be using the *container's* writing-mode (mCBWM) here,
    // instead of the item's (mWM). This is essentially bug 1155322.
    nscoord& ascent = aUseFirstBaseline ? mAscent : mAscentForLast;
    if (ascent != ReflowOutput::ASK_FOR_BASELINE) {
      return ascent;
    }

    // Use GetFirstLineBaseline() or GetLastLineBaseline() as appropriate:
    bool found = aUseFirstBaseline
                     ? nsLayoutUtils::GetFirstLineBaseline(mWM, mFrame, &ascent)
                     : nsLayoutUtils::GetLastLineBaseline(mWM, mFrame, &ascent);
    if (found) {
      return ascent;
    }

    // If the nsLayoutUtils getter fails, then ask the frame directly:
    auto baselineGroup = aUseFirstBaseline ? BaselineSharingGroup::First
                                           : BaselineSharingGroup::Last;
    if (auto baseline = mFrame->GetNaturalBaselineBOffset(
            mWM, baselineGroup, BaselineExportContext::Other)) {
      // Offset for last baseline from `GetNaturalBaselineBOffset` originates
      // from the frame's block end, so convert it back.
      ascent = baselineGroup == BaselineSharingGroup::First
                   ? *baseline
                   : mFrame->BSize(mWM) - *baseline;
      return ascent;
    }

    // We couldn't determine a baseline, so we synthesize one from border box:
    ascent = Baseline::SynthesizeBOffsetFromBorderBox(
        mFrame, mCBWM, BaselineSharingGroup::First);
    return ascent;
  }

  // Convenience methods to compute the main & cross size of our *margin-box*.
  nscoord OuterMainSize() const {
    return mMainSize + MarginBorderPaddingSizeInMainAxis();
  }

  nscoord OuterCrossSize() const {
    return mCrossSize + MarginBorderPaddingSizeInCrossAxis();
  }

  // Convenience method to return the content-box block-size.
  nscoord BSize() const {
    return IsBlockAxisMainAxis() ? MainSize() : CrossSize();
  }

  // Convenience method to return the measured content-box block-size computed
  // in nsFlexContainerFrame::MeasureBSizeForFlexItem().
  Maybe<nscoord> MeasuredBSize() const;

  // Convenience methods to synthesize a style main size or a style cross size
  // with box-size considered, to provide the size overrides when constructing
  // ReflowInput for flex items.
  StyleSize StyleMainSize() const {
    nscoord mainSize = MainSize();
    if (Frame()->StylePosition()->mBoxSizing == StyleBoxSizing::BorderBox) {
      mainSize += BorderPaddingSizeInMainAxis();
    }
    return StyleSize::FromAppUnits(mainSize);
  }

  StyleSize StyleCrossSize() const {
    nscoord crossSize = CrossSize();
    if (Frame()->StylePosition()->mBoxSizing == StyleBoxSizing::BorderBox) {
      crossSize += BorderPaddingSizeInCrossAxis();
    }
    return StyleSize::FromAppUnits(crossSize);
  }

  // Returns the distance between this FlexItem's baseline and the cross-start
  // edge of its margin-box. Used in baseline alignment.
  //
  // (This function needs to be told which physical start side we're measuring
  // the baseline from, so that it can look up the appropriate components from
  // margin.)
  nscoord BaselineOffsetFromOuterCrossEdge(mozilla::Side aStartSide,
                                           bool aUseFirstLineBaseline) const;

  double ShareOfWeightSoFar() const { return mShareOfWeightSoFar; }

  bool IsFrozen() const { return mIsFrozen; }

  bool HadMinViolation() const {
    MOZ_ASSERT(!mIsFrozen, "min violation has no meaning for frozen items.");
    return mHadMinViolation;
  }

  bool HadMaxViolation() const {
    MOZ_ASSERT(!mIsFrozen, "max violation has no meaning for frozen items.");
    return mHadMaxViolation;
  }

  bool WasMinClamped() const {
    MOZ_ASSERT(mIsFrozen, "min clamping has no meaning for unfrozen items.");
    return mHadMinViolation;
  }

  bool WasMaxClamped() const {
    MOZ_ASSERT(mIsFrozen, "max clamping has no meaning for unfrozen items.");
    return mHadMaxViolation;
  }

  // Indicates whether this item received a preliminary "measuring" reflow
  // before its actual reflow.
  bool HadMeasuringReflow() const { return mHadMeasuringReflow; }

  // Indicates whether this item's computed cross-size property is 'auto'.
  bool IsCrossSizeAuto() const;

  // Indicates whether the cross-size property is set to something definite,
  // for the purpose of preferred aspect ratio calculations.
  bool IsCrossSizeDefinite(const ReflowInput& aItemReflowInput) const;

  // Indicates whether this item's cross-size has been stretched (from having
  // "align-self: stretch" with an auto cross-size and no auto margins in the
  // cross axis).
  bool IsStretched() const { return mIsStretched; }

  bool IsFlexBaseSizeContentBSize() const {
    return mIsFlexBaseSizeContentBSize;
  }

  bool IsMainMinSizeContentBSize() const { return mIsMainMinSizeContentBSize; }

  // Indicates whether we need to resolve an 'auto' value for the main-axis
  // min-[width|height] property.
  bool NeedsMinSizeAutoResolution() const {
    return mNeedsMinSizeAutoResolution;
  }

  bool HasAnyAutoMargin() const { return mHasAnyAutoMargin; }

  BaselineSharingGroup ItemBaselineSharingGroup() const {
    MOZ_ASSERT(mAlignSelf == StyleAlignFlags::BASELINE ||
                   mAlignSelf == StyleAlignFlags::LAST_BASELINE,
               "mBaselineSharingGroup only gets a meaningful value "
               "for baseline-aligned items");
    return mBaselineSharingGroup;
  }

  // Indicates whether this item is a "strut" left behind by an element with
  // visibility:collapse.
  bool IsStrut() const { return mIsStrut; }

  // The main axis and cross axis are relative to mCBWM.
  LogicalAxis MainAxis() const { return mMainAxis; }
  LogicalAxis CrossAxis() const { return GetOrthogonalAxis(mMainAxis); }

  // IsInlineAxisMainAxis() returns true if this item's inline axis is parallel
  // (or antiparallel) to the container's main axis. Otherwise (i.e. if this
  // item's inline axis is orthogonal to the container's main axis), this
  // function returns false. The next 3 methods are all other ways of asking
  // the same question, and only exist for readability at callsites (depending
  // on which axes those callsites are reasoning about).
  bool IsInlineAxisMainAxis() const { return mIsInlineAxisMainAxis; }
  bool IsInlineAxisCrossAxis() const { return !mIsInlineAxisMainAxis; }
  bool IsBlockAxisMainAxis() const { return !mIsInlineAxisMainAxis; }
  bool IsBlockAxisCrossAxis() const { return mIsInlineAxisMainAxis; }

  WritingMode GetWritingMode() const { return mWM; }
  WritingMode ContainingBlockWM() const { return mCBWM; }
  StyleAlignFlags AlignSelf() const { return mAlignSelf; }
  StyleAlignFlags AlignSelfFlags() const { return mAlignSelfFlags; }

  // Returns the flex factor (flex-grow or flex-shrink), depending on
  // 'aIsUsingFlexGrow'.
  //
  // Asserts fatally if called on a frozen item (since frozen items are not
  // flexible).
  float GetFlexFactor(bool aIsUsingFlexGrow) {
    MOZ_ASSERT(!IsFrozen(), "shouldn't need flex factor after item is frozen");

    return aIsUsingFlexGrow ? mFlexGrow : mFlexShrink;
  }

  // Returns the weight that we should use in the "resolving flexible lengths"
  // algorithm.  If we're using the flex grow factor, we just return that;
  // otherwise, we return the "scaled flex shrink factor" (scaled by our flex
  // base size, so that when both large and small items are shrinking, the large
  // items shrink more).
  //
  // I'm calling this a "weight" instead of a "[scaled] flex-[grow|shrink]
  // factor", to more clearly distinguish it from the actual flex-grow &
  // flex-shrink factors.
  //
  // Asserts fatally if called on a frozen item (since frozen items are not
  // flexible).
  float GetWeight(bool aIsUsingFlexGrow) {
    MOZ_ASSERT(!IsFrozen(), "shouldn't need weight after item is frozen");

    if (aIsUsingFlexGrow) {
      return mFlexGrow;
    }

    // We're using flex-shrink --> return mFlexShrink * mFlexBaseSize
    if (mFlexBaseSize == 0) {
      // Special-case for mFlexBaseSize == 0 -- we have no room to shrink, so
      // regardless of mFlexShrink, we should just return 0.
      // (This is really a special-case for when mFlexShrink is infinity, to
      // avoid performing mFlexShrink * mFlexBaseSize = inf * 0 = undefined.)
      return 0.0f;
    }
    return mFlexShrink * mFlexBaseSize;
  }

  bool TreatBSizeAsIndefinite() const { return mTreatBSizeAsIndefinite; }

  const AspectRatio& GetAspectRatio() const { return mAspectRatio; }
  bool HasAspectRatio() const { return !!mAspectRatio; }

  // Getters for margin:
  // ===================
  LogicalMargin Margin() const { return mMargin; }
  nsMargin PhysicalMargin() const { return mMargin.GetPhysicalMargin(mCBWM); }

  // Returns the margin component for a given LogicalSide in flex container's
  // writing-mode.
  nscoord GetMarginComponentForSide(LogicalSide aSide) const {
    return mMargin.Side(aSide, mCBWM);
  }

  // Returns the total space occupied by this item's margins in the given axis
  nscoord MarginSizeInMainAxis() const {
    return mMargin.StartEnd(MainAxis(), mCBWM);
  }
  nscoord MarginSizeInCrossAxis() const {
    return mMargin.StartEnd(CrossAxis(), mCBWM);
  }

  // Getters for border/padding
  // ==========================
  // Returns the total space occupied by this item's borders and padding in
  // the given axis
  LogicalMargin BorderPadding() const { return mBorderPadding; }
  nscoord BorderPaddingSizeInMainAxis() const {
    return mBorderPadding.StartEnd(MainAxis(), mCBWM);
  }
  nscoord BorderPaddingSizeInCrossAxis() const {
    return mBorderPadding.StartEnd(CrossAxis(), mCBWM);
  }

  // Getter for combined margin/border/padding
  // =========================================
  // Returns the total space occupied by this item's margins, borders and
  // padding in the given axis
  nscoord MarginBorderPaddingSizeInMainAxis() const {
    return MarginSizeInMainAxis() + BorderPaddingSizeInMainAxis();
  }
  nscoord MarginBorderPaddingSizeInCrossAxis() const {
    return MarginSizeInCrossAxis() + BorderPaddingSizeInCrossAxis();
  }

  // Setters
  // =======
  // Helper to set the resolved value of min-[width|height]:auto for the main
  // axis. (Should only be used if NeedsMinSizeAutoResolution() returns true.)
  void UpdateMainMinSize(nscoord aNewMinSize) {
    NS_ASSERTION(aNewMinSize >= 0,
                 "How did we end up with a negative min-size?");
    MOZ_ASSERT(
        mMainMaxSize == NS_UNCONSTRAINEDSIZE || mMainMaxSize >= aNewMinSize,
        "Should only use this function for resolving min-size:auto, "
        "and main max-size should be an upper-bound for resolved val");
    MOZ_ASSERT(
        mNeedsMinSizeAutoResolution &&
            (mMainMinSize == 0 || mFrame->IsThemed(mFrame->StyleDisplay())),
        "Should only use this function for resolving min-size:auto, "
        "so we shouldn't already have a nonzero min-size established "
        "(unless it's a themed-widget-imposed minimum size)");

    if (aNewMinSize > mMainMinSize) {
      mMainMinSize = aNewMinSize;
      // Also clamp main-size to be >= new min-size:
      mMainSize = std::max(mMainSize, aNewMinSize);
    }
    mNeedsMinSizeAutoResolution = false;
  }

  // This sets our flex base size, and then sets our main size to the
  // resulting "hypothetical main size" (the base size clamped to our
  // main-axis [min,max] sizing constraints).
  void SetFlexBaseSizeAndMainSize(nscoord aNewFlexBaseSize) {
    MOZ_ASSERT(!mIsFrozen || mFlexBaseSize == NS_UNCONSTRAINEDSIZE,
               "flex base size shouldn't change after we're frozen "
               "(unless we're just resolving an intrinsic size)");
    mFlexBaseSize = aNewFlexBaseSize;

    // Before we've resolved flexible lengths, we keep mMainSize set to
    // the 'hypothetical main size', which is the flex base size, clamped
    // to the [min,max] range:
    mMainSize = CSSMinMax(mFlexBaseSize, mMainMinSize, mMainMaxSize);

    FLEX_ITEM_LOG(mFrame, "Set flex base size: %d, hypothetical main size: %d",
                  mFlexBaseSize, mMainSize);
  }

  // Setters used while we're resolving flexible lengths
  // ---------------------------------------------------

  // Sets the main-size of our flex item's content-box.
  void SetMainSize(nscoord aNewMainSize) {
    MOZ_ASSERT(!mIsFrozen, "main size shouldn't change after we're frozen");
    mMainSize = aNewMainSize;
  }

  void SetShareOfWeightSoFar(double aNewShare) {
    MOZ_ASSERT(!mIsFrozen || aNewShare == 0.0,
               "shouldn't be giving this item any share of the weight "
               "after it's frozen");
    mShareOfWeightSoFar = aNewShare;
  }

  void Freeze() {
    mIsFrozen = true;
    // Now that we are frozen, the meaning of mHadMinViolation and
    // mHadMaxViolation changes to indicate min and max clamping. Clear
    // both of the member variables so that they are ready to be set
    // as clamping state later, if necessary.
    mHadMinViolation = false;
    mHadMaxViolation = false;
  }

  void SetHadMinViolation() {
    MOZ_ASSERT(!mIsFrozen,
               "shouldn't be changing main size & having violations "
               "after we're frozen");
    mHadMinViolation = true;
  }
  void SetHadMaxViolation() {
    MOZ_ASSERT(!mIsFrozen,
               "shouldn't be changing main size & having violations "
               "after we're frozen");
    mHadMaxViolation = true;
  }
  void ClearViolationFlags() {
    MOZ_ASSERT(!mIsFrozen,
               "shouldn't be altering violation flags after we're "
               "frozen");
    mHadMinViolation = mHadMaxViolation = false;
  }

  void SetWasMinClamped() {
    MOZ_ASSERT(!mHadMinViolation && !mHadMaxViolation, "only clamp once");
    // This reuses the mHadMinViolation member variable to track clamping
    // events. This is allowable because mHadMinViolation only reflects
    // a violation up until the item is frozen.
    MOZ_ASSERT(mIsFrozen, "shouldn't set clamping state when we are unfrozen");
    mHadMinViolation = true;
  }
  void SetWasMaxClamped() {
    MOZ_ASSERT(!mHadMinViolation && !mHadMaxViolation, "only clamp once");
    // This reuses the mHadMaxViolation member variable to track clamping
    // events. This is allowable because mHadMaxViolation only reflects
    // a violation up until the item is frozen.
    MOZ_ASSERT(mIsFrozen, "shouldn't set clamping state when we are unfrozen");
    mHadMaxViolation = true;
  }

  // Setters for values that are determined after we've resolved our main size
  // -------------------------------------------------------------------------

  // Sets the main-axis position of our flex item's content-box.
  // (This is the distance between the main-start edge of the flex container
  // and the main-start edge of the flex item's content-box.)
  void SetMainPosition(nscoord aPosn) {
    MOZ_ASSERT(mIsFrozen, "main size should be resolved before this");
    mMainPosn = aPosn;
  }

  // Sets the cross-size of our flex item's content-box.
  void SetCrossSize(nscoord aCrossSize) {
    MOZ_ASSERT(!mIsStretched,
               "Cross size shouldn't be modified after it's been stretched");
    mCrossSize = aCrossSize;
  }

  // Sets the cross-axis position of our flex item's content-box.
  // (This is the distance between the cross-start edge of the flex container
  // and the cross-start edge of the flex item.)
  void SetCrossPosition(nscoord aPosn) {
    MOZ_ASSERT(mIsFrozen, "main size should be resolved before this");
    mCrossPosn = aPosn;
  }

  // After a FlexItem has had a reflow, this method can be used to cache its
  // (possibly-unresolved) ascent, in case it's needed later for
  // baseline-alignment or to establish the container's baseline.
  // (NOTE: This can be marked 'const' even though it's modifying mAscent,
  // because mAscent is mutable. It's nice for this to be 'const', because it
  // means our final reflow can iterate over const FlexItem pointers, and we
  // can be sure it's not modifying those FlexItems, except via this method.)
  void SetAscent(nscoord aAscent) const {
    mAscent = aAscent;  // NOTE: this may be ASK_FOR_BASELINE
  }

  void SetHadMeasuringReflow() { mHadMeasuringReflow = true; }

  void SetIsFlexBaseSizeContentBSize() { mIsFlexBaseSizeContentBSize = true; }

  void SetIsMainMinSizeContentBSize() { mIsMainMinSizeContentBSize = true; }

  // Setter for margin components (for resolving "auto" margins)
  void SetMarginComponentForSide(LogicalSide aSide, nscoord aLength) {
    MOZ_ASSERT(mIsFrozen, "main size should be resolved before this");
    mMargin.Side(aSide, mCBWM) = aLength;
  }

  void MaybeResolveStretchedCrossSize(nscoord aLineCrossSize);
  void ResolveStretchedCrossSize(nscoord aLineCrossSize);
  void ApplyStretchedCrossSize(nscoord aStretchedCrossSize);

  // Resolves flex base size if flex-basis' used value is 'content', using this
  // item's preferred aspect ratio and cross size.
  void ResolveFlexBaseSizeFromAspectRatio(const ReflowInput& aItemReflowInput);

  uint32_t NumAutoMarginsInMainAxis() const {
    return NumAutoMarginsInAxis(MainAxis());
  };

  uint32_t NumAutoMarginsInCrossAxis() const {
    return NumAutoMarginsInAxis(CrossAxis());
  };

  // Once the main size has been resolved, should we bother doing layout to
  // establish the cross size?
  bool CanMainSizeInfluenceCrossSize() const;

  // Returns a main size, clamped by any definite min and max cross size
  // converted through the preferred aspect ratio. The caller is responsible for
  // ensuring that the flex item's preferred aspect ratio is not zero.
  nscoord ClampMainSizeViaCrossAxisConstraints(
      nscoord aMainSize, const ReflowInput& aItemReflowInput) const;

  // Indicates whether we think this flex item needs a "final" reflow
  // (after its final flexed size & final position have been determined).
  //
  // @param aParentReflowInput the flex container's reflow input.
  // @return true if such a reflow is needed, or false if we believe it can
  // simply be moved to its final position and skip the reflow.
  bool NeedsFinalReflow(const ReflowInput& aParentReflowInput) const;

  // Gets the block frame that contains the flex item's content.  This is
  // Frame() itself or one of its descendants.
  nsBlockFrame* BlockFrame() const;

 protected:
  bool IsMinSizeAutoResolutionNeeded() const;

  uint32_t NumAutoMarginsInAxis(LogicalAxis aAxis) const;

  // Values that we already know in constructor, and remain unchanged:
  // The flex item's frame.
  nsIFrame* mFrame = nullptr;
  float mFlexGrow = 0.0f;
  float mFlexShrink = 0.0f;
  AspectRatio mAspectRatio;

  // The flex item's writing mode.
  WritingMode mWM;

  // The flex container's writing mode.
  WritingMode mCBWM;

  // The flex container's main axis in flex container's writing mode.
  LogicalAxis mMainAxis;

  // Stored in flex container's writing mode.
  LogicalMargin mBorderPadding;

  // Stored in flex container's writing mode. Its value can change when we
  // resolve "auto" marigns.
  LogicalMargin mMargin;

  // These are non-const so that we can lazily update them with the item's
  // intrinsic size (obtained via a "measuring" reflow), when necessary.
  // (e.g. for "flex-basis:auto;height:auto" & "min-height:auto")
  nscoord mFlexBaseSize = 0;
  nscoord mMainMinSize = 0;
  nscoord mMainMaxSize = 0;

  // mCrossMinSize and mCrossMaxSize are not changed after constructor.
  nscoord mCrossMinSize = 0;
  nscoord mCrossMaxSize = 0;

  // Values that we compute after constructor:
  nscoord mMainSize = 0;
  nscoord mMainPosn = 0;
  nscoord mCrossSize = 0;
  nscoord mCrossPosn = 0;

  // Mutable b/c it's set & resolved lazily, sometimes via const pointer. See
  // comment above SetAscent().
  // We initialize this to ASK_FOR_BASELINE, and opportunistically fill it in
  // with a real value if we end up reflowing this flex item. (But if we don't
  // reflow this flex item, then this sentinel tells us that we don't know it
  // yet & anyone who cares will need to explicitly request it.)
  //
  // Both mAscent and mAscentForLast are distance from the frame's border-box
  // block-start edge.
  mutable nscoord mAscent = ReflowOutput::ASK_FOR_BASELINE;
  mutable nscoord mAscentForLast = ReflowOutput::ASK_FOR_BASELINE;

  // Temporary state, while we're resolving flexible widths (for our main size)
  // XXXdholbert To save space, we could use a union to make these variables
  // overlay the same memory as some other member vars that aren't touched
  // until after main-size has been resolved. In particular, these could share
  // memory with mMainPosn through mAscent, and mIsStretched.
  double mShareOfWeightSoFar = 0.0;

  bool mIsFrozen = false;
  bool mHadMinViolation = false;
  bool mHadMaxViolation = false;

  // Did this item get a preliminary reflow, to measure its desired height?
  bool mHadMeasuringReflow = false;

  // See IsStretched() documentation.
  bool mIsStretched = false;

  // Is this item a "strut" left behind by an element with visibility:collapse?
  bool mIsStrut = false;

  // See IsInlineAxisMainAxis() documentation. This is not changed after
  // constructor.
  bool mIsInlineAxisMainAxis = true;

  // Does this item need to resolve a min-[width|height]:auto (in main-axis)?
  //
  // Note: mNeedsMinSizeAutoResolution needs to be declared towards the end of
  // the member variables since it's initialized in a method that depends on
  // other members declared above such as mCBWM, mMainAxis, and
  // mIsInlineAxisMainAxis.
  bool mNeedsMinSizeAutoResolution = false;

  // Should we take care to treat this item's resolved BSize as indefinite?
  bool mTreatBSizeAsIndefinite = false;

  // Does this item have an auto margin in either main or cross axis?
  bool mHasAnyAutoMargin = false;

  // Does this item have a content-based flex base size (and is that a size in
  // its block-axis)?
  bool mIsFlexBaseSizeContentBSize = false;

  // Does this item have a content-based resolved auto min size (and is that a
  // size in its block-axis)?
  bool mIsMainMinSizeContentBSize = false;

  // If this item is {first,last}-baseline-aligned using 'align-self', which of
  // its FlexLine's baseline sharing groups does it participate in?
  BaselineSharingGroup mBaselineSharingGroup = BaselineSharingGroup::First;

  // My "align-self" computed value (with "auto" swapped out for parent"s
  // "align-items" value, in our constructor).
  StyleAlignFlags mAlignSelf{StyleAlignFlags::AUTO};

  // Flags for 'align-self' (safe/unsafe/legacy).
  StyleAlignFlags mAlignSelfFlags{0};
};

/**
 * Represents a single flex line in a flex container.
 * Manages an array of the FlexItems that are in the line.
 */
class nsFlexContainerFrame::FlexLine final {
 public:
  explicit FlexLine(nscoord aMainGapSize) : mMainGapSize(aMainGapSize) {}

  nscoord SumOfGaps() const {
    return mNumNonCollapsedItems ? (mNumNonCollapsedItems - 1) * mMainGapSize
                                 : 0;
  }

  // Returns the sum of our FlexItems' outer hypothetical main sizes plus the
  // sum of main axis {row,column}-gaps between items.
  // ("outer" = margin-box, and "hypothetical" = before flexing)
  AuCoord64 TotalOuterHypotheticalMainSize() const {
    return mTotalOuterHypotheticalMainSize;
  }

  // Accessors for our FlexItems & information about them:
  //
  // Note: Callers must use IsEmpty() to ensure that the FlexLine is non-empty
  // before calling accessors that return FlexItem.
  FlexItem& FirstItem() { return mItems[0]; }
  const FlexItem& FirstItem() const { return mItems[0]; }

  FlexItem& LastItem() { return mItems.LastElement(); }
  const FlexItem& LastItem() const { return mItems.LastElement(); }

  // The "startmost"/"endmost" is from the perspective of the flex container's
  // writing-mode, not from the perspective of the flex-relative main axis.
  const FlexItem& StartmostItem(const FlexboxAxisTracker& aAxisTracker) const {
    return aAxisTracker.IsMainAxisReversed() ? LastItem() : FirstItem();
  }
  const FlexItem& EndmostItem(const FlexboxAxisTracker& aAxisTracker) const {
    return aAxisTracker.IsMainAxisReversed() ? FirstItem() : LastItem();
  }

  bool IsEmpty() const { return mItems.IsEmpty(); }

  uint32_t NumItems() const { return mItems.Length(); }

  nsTArray<FlexItem>& Items() { return mItems; }
  const nsTArray<FlexItem>& Items() const { return mItems; }

  // Adds the last flex item's hypothetical outer main-size and
  // margin/border/padding to our totals. This should be called exactly once for
  // each flex item, after we've determined that this line is the correct home
  // for that item.
  void AddLastItemToMainSizeTotals() {
    const FlexItem& lastItem = Items().LastElement();

    // Update our various bookkeeping member-vars:
    if (lastItem.IsFrozen()) {
      mNumFrozenItems++;
    }

    // If the item added was not the first non-collapsed item in the line,
    // we add in any gap space as needed.
    if (!lastItem.IsStrut()) {
      if (mNumNonCollapsedItems) {
        mTotalOuterHypotheticalMainSize += mMainGapSize;
      }
      mNumNonCollapsedItems++;
    }

    mTotalItemMBP += lastItem.MarginBorderPaddingSizeInMainAxis();
    mTotalOuterHypotheticalMainSize += lastItem.OuterMainSize();
  }

  // Computes the cross-size and baseline position of this FlexLine, based on
  // its FlexItems.
  void ComputeCrossSizeAndBaseline(const FlexboxAxisTracker& aAxisTracker);

  // Returns the cross-size of this line.
  nscoord LineCrossSize() const { return mLineCrossSize; }

  // Setter for line cross-size -- needed for cases where the flex container
  // imposes a cross-size on the line. (e.g. for single-line flexbox, or for
  // multi-line flexbox with 'align-content: stretch')
  void SetLineCrossSize(nscoord aLineCrossSize) {
    mLineCrossSize = aLineCrossSize;
  }

  /**
   * Returns the offset within this line where any baseline-aligned FlexItems
   * should place their baseline. The return value represents a distance from
   * the line's cross-start edge.
   *
   * If there are no baseline-aligned FlexItems, returns nscoord_MIN.
   */
  nscoord FirstBaselineOffset() const { return mFirstBaselineOffset; }

  /**
   * Returns the offset within this line where any last baseline-aligned
   * FlexItems should place their baseline. Opposite the case of the first
   * baseline offset, this represents a distance from the line's cross-end
   * edge (since last baseline-aligned items are flush to the cross-end edge).
   *
   * If there are no last baseline-aligned FlexItems, returns nscoord_MIN.
   */
  nscoord LastBaselineOffset() const { return mLastBaselineOffset; }

  // Extract a baseline from this line, which would be suitable for use as the
  // flex container's 'aBaselineGroup' (i.e. first/last) baseline.
  // https://drafts.csswg.org/css-flexbox-1/#flex-baselines
  //
  // The return value always represents a distance from the line's cross-start
  // edge, even if we are querying last baseline. If this line has no flex items
  // in its aBaselineGroup group, this method falls back to trying the opposite
  // group. If this line has no baseline-aligned items at all, this returns
  // nscoord_MIN.
  nscoord ExtractBaselineOffset(BaselineSharingGroup aBaselineGroup) const;

  /**
   * Returns the gap size in the main axis for this line. Used for gap
   * calculations.
   */
  nscoord MainGapSize() const { return mMainGapSize; }

  // Runs the "Resolving Flexible Lengths" algorithm from section 9.7 of the
  // CSS flexbox spec to distribute aFlexContainerMainSize among our flex items.
  // https://drafts.csswg.org/css-flexbox-1/#resolve-flexible-lengths
  void ResolveFlexibleLengths(nscoord aFlexContainerMainSize,
                              ComputedFlexLineInfo* aLineInfo);

  void PositionItemsInMainAxis(const StyleContentDistribution& aJustifyContent,
                               nscoord aContentBoxMainSize,
                               const FlexboxAxisTracker& aAxisTracker);

  void PositionItemsInCrossAxis(nscoord aLineStartPosition,
                                const FlexboxAxisTracker& aAxisTracker);

 private:
  // Helpers for ResolveFlexibleLengths():
  void FreezeItemsEarly(bool aIsUsingFlexGrow, ComputedFlexLineInfo* aLineInfo);

  void FreezeOrRestoreEachFlexibleSize(const nscoord aTotalViolation,
                                       bool aIsFinalIteration);

  // Sum of FlexItems' outer hypothetical main sizes and all main-axis
  // {row,columnm}-gaps between items.
  // (i.e. their flex base sizes, clamped via their min/max-size properties,
  // plus their main-axis margin/border/padding, plus the sum of the gaps.)
  //
  // This variable uses a 64-bit coord type to avoid integer overflow in case
  // several of the individual items have huge hypothetical main sizes, which
  // can happen with percent-width table-layout:fixed descendants. We have to
  // avoid integer overflow in order to shrink items properly in that scenario.
  AuCoord64 mTotalOuterHypotheticalMainSize = 0;

  // Stores this line's flex items.
  nsTArray<FlexItem> mItems;

  // Number of *frozen* FlexItems in this line, based on FlexItem::IsFrozen().
  // Mostly used for optimization purposes, e.g. to bail out early from loops
  // when we can tell they have nothing left to do.
  uint32_t mNumFrozenItems = 0;
  // Number of items that are not collapsed.
  uint32_t mNumNonCollapsedItems = 0;

  // Sum of margin/border/padding for the FlexItems in this FlexLine.
  nscoord mTotalItemMBP = 0;

  nscoord mLineCrossSize = 0;
  nscoord mFirstBaselineOffset = nscoord_MIN;
  nscoord mLastBaselineOffset = nscoord_MIN;

  // Maintain size of each {row,column}-gap in the main axis
  const nscoord mMainGapSize;
};

// The "startmost"/"endmost" is from the perspective of the flex container's
// writing-mode, not from the perspective of the flex-relative cross axis.
const FlexLine& StartmostLine(const nsTArray<FlexLine>& aLines,
                              const FlexboxAxisTracker& aAxisTracker) {
  return aAxisTracker.IsCrossAxisReversed() ? aLines.LastElement() : aLines[0];
}
const FlexLine& EndmostLine(const nsTArray<FlexLine>& aLines,
                            const FlexboxAxisTracker& aAxisTracker) {
  return aAxisTracker.IsCrossAxisReversed() ? aLines[0] : aLines.LastElement();
}

// Information about a strut left behind by a FlexItem that's been collapsed
// using "visibility:collapse".
struct nsFlexContainerFrame::StrutInfo {
  StrutInfo(uint32_t aItemIdx, nscoord aStrutCrossSize)
      : mItemIdx(aItemIdx), mStrutCrossSize(aStrutCrossSize) {}

  uint32_t mItemIdx;        // Index in the child list.
  nscoord mStrutCrossSize;  // The cross-size of this strut.
};

// Flex data shared by the flex container frames in a continuation chain, owned
// by the first-in-flow. The data is initialized at the end of the
// first-in-flow's Reflow().
struct nsFlexContainerFrame::SharedFlexData final {
  // The flex lines generated in DoFlexLayout() by our first-in-flow.
  nsTArray<FlexLine> mLines;

  // The final content main/cross size computed by DoFlexLayout.
  nscoord mContentBoxMainSize = NS_UNCONSTRAINEDSIZE;
  nscoord mContentBoxCrossSize = NS_UNCONSTRAINEDSIZE;

  // Update this struct. Called by the first-in-flow.
  void Update(FlexLayoutResult&& aFlr) {
    mLines = std::move(aFlr.mLines);
    mContentBoxMainSize = aFlr.mContentBoxMainSize;
    mContentBoxCrossSize = aFlr.mContentBoxCrossSize;
  }

  // The frame property under which this struct is stored. Set only on the
  // first-in-flow.
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(Prop, SharedFlexData)
};

// Flex data stored in every flex container's in-flow fragment (continuation).
//
// It's intended to prevent quadratic operations resulting from each fragment
// having to walk its full prev-in-flow chain, and also serves as an argument to
// the flex container next-in-flow's ReflowChildren(), to compute the position
// offset for each flex item.
struct nsFlexContainerFrame::PerFragmentFlexData final {
  // Suppose D is the distance from a flex container fragment's content-box
  // block-start edge to whichever is larger of either (a) the block-end edge of
  // its children, or (b) the available space's block-end edge. (Note: in case
  // (b), D is conceptually the sum of the block-size of the children, the
  // packing space before & in between them, and part of the packing space after
  // them.)
  //
  // This variable stores the sum of the D values for the current flex container
  // fragments and for all its previous fragments
  nscoord mCumulativeContentBoxBSize = 0;

  // This variable accumulates FirstLineOrFirstItemBAxisMetrics::mBEndEdgeShift,
  // for the current flex container fragment and for all its previous fragments.
  // See the comment of mBEndEdgeShift for its computation details. In short,
  // this value is the net block-end edge shift, accumulated for the children in
  // all the previous fragments. This number is non-negative.
  //
  // This value is also used to grow a flex container's block-size if the
  // container's computed block-size is unconstrained. For example: a tall item
  // may be pushed to the next page/column, which leaves some wasted area at the
  // bottom of the current flex container fragment, and causes the flex
  // container fragments to be (collectively) larger than the hypothetical
  // unfragmented size. Another example: a tall flex item may be broken into
  // multiple fragments, and those fragments may have a larger collective
  // block-size as compared to the item's original unfragmented size; the
  // container would need to increase its block-size to account for this.
  nscoord mCumulativeBEndEdgeShift = 0;

  // The frame property under which this struct is stored. Cached on every
  // in-flow fragment (continuation) at the end of the flex container's
  // Reflow().
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(Prop, PerFragmentFlexData)
};

static void BuildStrutInfoFromCollapsedItems(const nsTArray<FlexLine>& aLines,
                                             nsTArray<StrutInfo>& aStruts) {
  MOZ_ASSERT(aStruts.IsEmpty(),
             "We should only build up StrutInfo once per reflow, so "
             "aStruts should be empty when this is called");

  uint32_t itemIdxInContainer = 0;
  for (const FlexLine& line : aLines) {
    for (const FlexItem& item : line.Items()) {
      if (item.Frame()->StyleVisibility()->IsCollapse()) {
        // Note the cross size of the line as the item's strut size.
        aStruts.AppendElement(
            StrutInfo(itemIdxInContainer, line.LineCrossSize()));
      }
      itemIdxInContainer++;
    }
  }
}

static mozilla::StyleAlignFlags SimplifyAlignOrJustifyContentForOneItem(
    const StyleContentDistribution& aAlignmentVal, bool aIsAlign) {
  // Mask away any explicit fallback, to get the main (non-fallback) part of
  // the specified value:
  StyleAlignFlags specified = aAlignmentVal.primary;

  // XXX strip off <overflow-position> bits until we implement it (bug 1311892)
  specified &= ~StyleAlignFlags::FLAG_BITS;

  // FIRST: handle a special-case for "justify-content:stretch" (or equivalent),
  // which requires that we ignore any author-provided explicit fallback value.
  if (specified == StyleAlignFlags::NORMAL) {
    // In a flex container, *-content: "'normal' behaves as 'stretch'".
    // Do that conversion early, so it benefits from our 'stretch' special-case.
    // https://drafts.csswg.org/css-align-3/#distribution-flex
    specified = StyleAlignFlags::STRETCH;
  }
  if (!aIsAlign && specified == StyleAlignFlags::STRETCH) {
    // In a flex container, in "justify-content Axis: [...] 'stretch' behaves
    // as 'flex-start' (ignoring the specified fallback alignment, if any)."
    // https://drafts.csswg.org/css-align-3/#distribution-flex
    // So, we just directly return 'flex-start', & ignore explicit fallback..
    return StyleAlignFlags::FLEX_START;
  }

  // TODO: Check for an explicit fallback value (and if it's present, use it)
  // here once we parse it, see https://github.com/w3c/csswg-drafts/issues/1002.

  // If there's no explicit fallback, use the implied fallback values for
  // space-{between,around,evenly} (since those values only make sense with
  // multiple alignment subjects), and otherwise just use the specified value:
  if (specified == StyleAlignFlags::SPACE_BETWEEN) {
    return StyleAlignFlags::FLEX_START;
  }
  if (specified == StyleAlignFlags::SPACE_AROUND ||
      specified == StyleAlignFlags::SPACE_EVENLY) {
    return StyleAlignFlags::CENTER;
  }
  return specified;
}

bool nsFlexContainerFrame::DrainSelfOverflowList() {
  return DrainAndMergeSelfOverflowList();
}

void nsFlexContainerFrame::AppendFrames(ChildListID aListID,
                                        nsFrameList&& aFrameList) {
  NoteNewChildren(aListID, aFrameList);
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
}

void nsFlexContainerFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  NoteNewChildren(aListID, aFrameList);
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
}

void nsFlexContainerFrame::RemoveFrame(DestroyContext& aContext,
                                       ChildListID aListID,
                                       nsIFrame* aOldFrame) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal, "unexpected child list");

#ifdef DEBUG
  SetDidPushItemsBitIfNeeded(aListID, aOldFrame);
#endif

  nsContainerFrame::RemoveFrame(aContext, aListID, aOldFrame);
}

StyleAlignFlags nsFlexContainerFrame::CSSAlignmentForAbsPosChild(
    const ReflowInput& aChildRI, LogicalAxis aLogicalAxis) const {
  const FlexboxAxisTracker axisTracker(this);

  // If we're row-oriented and the caller is asking about our inline axis (or
  // alternately, if we're column-oriented and the caller is asking about our
  // block axis), then the caller is really asking about our *main* axis.
  // Otherwise, the caller is asking about our cross axis.
  const bool isMainAxis =
      (axisTracker.IsRowOriented() == (aLogicalAxis == LogicalAxis::Inline));
  const nsStylePosition* containerStylePos = StylePosition();
  const bool isAxisReversed = isMainAxis ? axisTracker.IsMainAxisReversed()
                                         : axisTracker.IsCrossAxisReversed();

  StyleAlignFlags alignment{0};
  StyleAlignFlags alignmentFlags{0};
  if (isMainAxis) {
    // We're aligning in the main axis: align according to 'justify-content'.
    // (We don't care about justify-self; it has no effect on children of flex
    // containers, unless https://github.com/w3c/csswg-drafts/issues/7644
    // changes that.)
    alignment = SimplifyAlignOrJustifyContentForOneItem(
        containerStylePos->mJustifyContent,
        /*aIsAlign = */ false);
  } else {
    // We're aligning in the cross axis: align according to 'align-self'.
    // (We don't care about align-content; it has no effect on abspos flex
    // children, per https://github.com/w3c/csswg-drafts/issues/7596 )
    alignment = aChildRI.mStylePosition->UsedAlignSelf(Style())._0;
    // Extract and strip align flag bits
    alignmentFlags = alignment & StyleAlignFlags::FLAG_BITS;
    alignment &= ~StyleAlignFlags::FLAG_BITS;

    if (alignment == StyleAlignFlags::NORMAL) {
      // "the 'normal' keyword behaves as 'start' on replaced
      // absolutely-positioned boxes, and behaves as 'stretch' on all other
      // absolutely-positioned boxes."
      // https://drafts.csswg.org/css-align/#align-abspos
      alignment = aChildRI.mFrame->IsReplaced() ? StyleAlignFlags::START
                                                : StyleAlignFlags::STRETCH;
    }
  }

  if (alignment == StyleAlignFlags::STRETCH) {
    // The default fallback alignment for 'stretch' is 'flex-start'.
    alignment = StyleAlignFlags::FLEX_START;
  }

  // Resolve flex-start, flex-end, auto, left, right, baseline, last baseline;
  if (alignment == StyleAlignFlags::FLEX_START) {
    alignment = isAxisReversed ? StyleAlignFlags::END : StyleAlignFlags::START;
  } else if (alignment == StyleAlignFlags::FLEX_END) {
    alignment = isAxisReversed ? StyleAlignFlags::START : StyleAlignFlags::END;
  } else if (alignment == StyleAlignFlags::LEFT ||
             alignment == StyleAlignFlags::RIGHT) {
    MOZ_ASSERT(isMainAxis, "Only justify-* can have 'left' and 'right'!");
    alignment = axisTracker.ResolveJustifyLeftRight(alignment);
  } else if (alignment == StyleAlignFlags::BASELINE) {
    alignment = StyleAlignFlags::START;
  } else if (alignment == StyleAlignFlags::LAST_BASELINE) {
    alignment = StyleAlignFlags::END;
  }

  MOZ_ASSERT(alignment != StyleAlignFlags::STRETCH,
             "We should've converted 'stretch' to the fallback alignment!");
  MOZ_ASSERT(alignment != StyleAlignFlags::FLEX_START &&
                 alignment != StyleAlignFlags::FLEX_END,
             "AbsoluteContainingBlock doesn't know how to handle "
             "flex-relative axis for flex containers!");

  return (alignment | alignmentFlags);
}

std::pair<StyleAlignFlags, StyleAlignFlags>
nsFlexContainerFrame::UsedAlignSelfAndFlagsForItem(
    const nsIFrame* aFlexItem) const {
  MOZ_ASSERT(aFlexItem->IsFlexItem());

  if (IsLegacyWebkitBox()) {
    // For -webkit-{inline-}box, we need to:
    // (1) Use prefixed "box-align" instead of "align-items" to determine the
    //     container's cross-axis alignment behavior.
    // (2) Suppress the ability for flex items to override that with their own
    //     cross-axis alignment. (The legacy box model doesn't support this.)
    // So, each FlexItem simply copies the container's converted "align-items"
    // value and disregards their own "align-self" property.
    const StyleAlignFlags alignSelf =
        ConvertLegacyStyleToAlignItems(StyleXUL());
    const StyleAlignFlags flags = {0};
    return {alignSelf, flags};
  }

  // Note: we don't need to call nsLayoutUtils::GetStyleFrame(aFlexItem) because
  // the table wrapper frame inherits 'align-self' property from the table
  // frame.
  StyleSelfAlignment usedAlignSelf =
      aFlexItem->StylePosition()->UsedAlignSelf(Style());
  if (MOZ_LIKELY(usedAlignSelf._0 == StyleAlignFlags::NORMAL)) {
    // For flex items, 'align-self:normal' behaves as 'align-self:stretch'.
    // https://drafts.csswg.org/css-align-3/#align-flex
    usedAlignSelf = {StyleAlignFlags::STRETCH};
  }

  // Store the <overflow-position> bits in flags, and strip the bits from the
  // used align-self value.
  const StyleAlignFlags flags = usedAlignSelf._0 & StyleAlignFlags::FLAG_BITS;
  const StyleAlignFlags alignSelf =
      usedAlignSelf._0 & ~StyleAlignFlags::FLAG_BITS;
  return {alignSelf, flags};
}

// Returns whether the given item should stretch in the cross axis.
// We stretch IFF the item's used align-self is 'stretch', it has no auto
// margins in the cross axis, and its cross-axis size property is auto.
// https://drafts.csswg.org/css-flexbox-1/#valdef-align-items-stretch
static bool ShouldStretchCrossSize(const nsFlexContainerFrame* aContainer,
                                   const nsIFrame* aItemFrame,
                                   WritingMode aCBWM, LogicalAxis aCrossAxis) {
  [[maybe_unused]] auto [alignSelf, flags] =
      aContainer->UsedAlignSelfAndFlagsForItem(aItemFrame);
  if (alignSelf != StyleAlignFlags::STRETCH) {
    return false;
  }

  // Ugh, tables.
  const auto* styleFrame = nsLayoutUtils::GetStyleFrame(aItemFrame);
  if (!styleFrame->StylePosition()
           ->Size(aCrossAxis, aCBWM,
                  AnchorPosResolutionParams::From(styleFrame))
           ->IsAuto()) {
    return false;
  }

  return !aItemFrame->StyleMargin()->HasAuto(
      aCrossAxis, aCBWM, AnchorPosResolutionParams::From(aItemFrame));
}

void nsFlexContainerFrame::GenerateFlexItemForChild(
    FlexLine& aLine, nsIFrame* aChildFrame,
    const ReflowInput& aParentReflowInput,
    const FlexboxAxisTracker& aAxisTracker,
    const nscoord aTentativeContentBoxCrossSize) {
  const auto flexWM = aAxisTracker.GetWritingMode();
  const auto childWM = aChildFrame->GetWritingMode();

  // Note: we use GetStyleFrame() to access the sizing & flex properties here.
  // This lets us correctly handle table wrapper frames as flex items since
  // their inline-size and block-size properties are always 'auto'. In order for
  // 'flex-basis:auto' to actually resolve to the author's specified inline-size
  // or block-size, we need to dig through to the inner table.
  const auto* styleFrame = nsLayoutUtils::GetStyleFrame(aChildFrame);
  const auto* stylePos = styleFrame->StylePosition();
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(styleFrame);

  // Construct a StyleSizeOverrides for this flex item so that its ReflowInput
  // below will use and resolve its flex base size rather than its corresponding
  // preferred main size property (only for modern CSS flexbox).
  StyleSizeOverrides sizeOverrides;
  if (!IsLegacyWebkitBox()) {
    Maybe<StyleSize> styleFlexBaseSize;

    // When resolving flex base size, flex items use their 'flex-basis' property
    // in place of their preferred main size (e.g. 'width') for sizing purposes,
    // *unless* they have 'flex-basis:auto' in which case they use their
    // preferred main size after all.
    const auto& flexBasis = stylePos->mFlexBasis;
    const auto styleMainSize =
        stylePos->Size(aAxisTracker.MainAxis(), flexWM, anchorResolutionParams);
    if (IsUsedFlexBasisContent(flexBasis, *styleMainSize)) {
      // If we get here, we're resolving the flex base size for a flex item, and
      // we fall into the flexbox spec section 9.2 step 3, substep C (if we have
      // a definite cross size) or E (if not).
      styleFlexBaseSize.emplace(StyleSize::MaxContent());
    } else if (flexBasis.IsSize() && !flexBasis.IsAuto()) {
      // For all other non-'auto' flex-basis values, we just swap in the
      // flex-basis itself for the preferred main-size property.
      styleFlexBaseSize.emplace(flexBasis.AsSize());
    } else {
      // else: flex-basis is 'auto', which is deferring to some explicit value
      // in the preferred main size.
      MOZ_ASSERT(flexBasis.IsAuto());
      styleFlexBaseSize.emplace(*styleMainSize);
    }

    MOZ_ASSERT(styleFlexBaseSize, "We should've emplace styleFlexBaseSize!");

    // Provide the size override for the preferred main size property.
    if (aAxisTracker.IsInlineAxisMainAxis(childWM)) {
      sizeOverrides.mStyleISize = std::move(styleFlexBaseSize);
    } else {
      sizeOverrides.mStyleBSize = std::move(styleFlexBaseSize);
    }

    // 'flex-basis' should works on the inner table frame for a table flex item,
    // just like how 'height' works on a table element.
    sizeOverrides.mApplyOverridesVerbatim = true;
  }

  // The spec considers a stretched item's cross size definite, so the
  // ReflowInput below needs to know it: it may resolve the item's flex base
  // size by measuring content, and the item's descendants may resolve
  // percentages against the item's cross size, or transfer it through their
  // preferred aspect ratios.
  const bool stretchCrossSize =
      IsSingleLine(aParentReflowInput.mFrame,
                   aParentReflowInput.mStylePosition) &&
      (aAxisTracker.IsColumnOriented() ||
       aTentativeContentBoxCrossSize != NS_UNCONSTRAINEDSIZE) &&
      ShouldStretchCrossSize(this, aChildFrame, flexWM,
                             aAxisTracker.CrossAxis());
  if (stretchCrossSize) {
    auto& crossSizeOverride = aAxisTracker.IsInlineAxisMainAxis(childWM)
                                  ? sizeOverrides.mStyleBSize
                                  : sizeOverrides.mStyleISize;
    crossSizeOverride.emplace(StyleSize::Stretch());
  }

  // Create temporary reflow input just for sizing -- to get hypothetical
  // main-size and the computed values of min / max main-size property.
  // (This reflow input will _not_ be used for reflow.)
  ReflowInput childRI(PresContext(), aParentReflowInput, aChildFrame,
                      aParentReflowInput.ComputedSize(childWM), Nothing(), {},
                      sizeOverrides, {ComputeSizeFlag::ShrinkWrap});

  // FLEX GROW & SHRINK WEIGHTS
  // --------------------------
  float flexGrow, flexShrink;
  if (IsLegacyWebkitBox()) {
    flexGrow = flexShrink = aChildFrame->StyleXUL()->mBoxFlex;
  } else {
    flexGrow = stylePos->mFlexGrow;
    flexShrink = stylePos->mFlexShrink;
  }

  // MAIN SIZES (flex base size, min/max size)
  // -----------------------------------------
  const LogicalSize computedSizeInFlexWM = childRI.ComputedSize(flexWM);
  const LogicalSize computedMinSizeInFlexWM = childRI.ComputedMinSize(flexWM);
  const LogicalSize computedMaxSizeInFlexWM = childRI.ComputedMaxSize(flexWM);

  const nscoord flexBaseSize = aAxisTracker.MainComponent(computedSizeInFlexWM);
  const nscoord mainMinSize =
      aAxisTracker.MainComponent(computedMinSizeInFlexWM);
  const nscoord mainMaxSize =
      aAxisTracker.MainComponent(computedMaxSizeInFlexWM);

  // This is enforced by the ReflowInput where these values come from:
  MOZ_ASSERT(mainMinSize <= mainMaxSize, "min size is larger than max size");

  // CROSS SIZES (tentative cross size, min/max cross size)
  // ------------------------------------------------------
  // Grab the cross size from the reflow input. This might be the right value,
  // or we might resolve it to something else in SizeItemInCrossAxis(); hence,
  // it's tentative. See comment under "Cross Size Determination" for more.
  const nscoord tentativeCrossSize =
      aAxisTracker.CrossComponent(computedSizeInFlexWM);
  const nscoord crossMinSize =
      aAxisTracker.CrossComponent(computedMinSizeInFlexWM);
  const nscoord crossMaxSize =
      aAxisTracker.CrossComponent(computedMaxSizeInFlexWM);

  // Construct the flex item!
  FlexItem& item = *aLine.Items().EmplaceBack(
      childRI, flexGrow, flexShrink, flexBaseSize, mainMinSize, mainMaxSize,
      tentativeCrossSize, crossMinSize, crossMaxSize, aAxisTracker);

  if (stretchCrossSize) {
    // tentativeCrossSize could be still unconstrained even if
    // aTentativeContentBoxCrossSize isn't, due to e.g. mTreatBSizeAsIndefinite.
    // FIXME(emilio, bug 2063696): The table wrapper frame check is a hack
    // around a pre-existing stretch bug, remove when that's fixed.
    if (tentativeCrossSize != NS_UNCONSTRAINEDSIZE &&
        !aChildFrame->IsTableWrapperFrame()) {
      item.ApplyStretchedCrossSize(tentativeCrossSize);
    } else {
      item.ResolveStretchedCrossSize(aTentativeContentBoxCrossSize);
    }
  }

  // Before thinking about freezing the item at its base size, we need to give
  // it a chance to recalculate the base size from its cross size and aspect
  // ratio (since its cross size might've *just* now become definite due to
  // 'stretch' above).
  item.ResolveFlexBaseSizeFromAspectRatio(childRI);

  // If we're inflexible, we can just freeze to our hypothetical main-size
  // up-front.
  if (flexGrow == 0.0f && flexShrink == 0.0f) {
    item.Freeze();
    if (flexBaseSize < mainMinSize) {
      item.SetWasMinClamped();
    } else if (flexBaseSize > mainMaxSize) {
      item.SetWasMaxClamped();
    }
  }

  // Resolve "flex-basis:auto" and/or "min-[width|height]:auto" (which might
  // require us to reflow the item to measure content height)
  ResolveAutoFlexBasisAndMinSize(item, childRI, aAxisTracker);
}

nscoord nsFlexContainerFrame::PartiallyResolveAutoMinSize(
    const FlexItem& aFlexItem, const ReflowInput& aItemReflowInput,
    const FlexboxAxisTracker& aAxisTracker) const {
  MOZ_ASSERT(aFlexItem.NeedsMinSizeAutoResolution(),
             "only call for FlexItems that need min-size auto resolution");

  const auto itemWM = aFlexItem.GetWritingMode();
  const auto cbWM = aAxisTracker.GetWritingMode();
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(&aItemReflowInput);
  const auto mainStyleSize = aItemReflowInput.mStylePosition->Size(
      aAxisTracker.MainAxis(), cbWM, anchorResolutionParams);
  const auto maxMainStyleSize = aItemReflowInput.mStylePosition->MaxSize(
      aAxisTracker.MainAxis(), cbWM, anchorResolutionParams);
  const auto boxSizingAdjust =
      aItemReflowInput.mStylePosition->mBoxSizing == StyleBoxSizing::BorderBox
          ? aFlexItem.BorderPadding().Size(cbWM)
          : LogicalSize(cbWM);

  // Return the percentage basis in cbWM for computing the specified size
  // suggestion.
  auto PercentageBasisForItem = [&]() {
    // If this flex item is a compressible replaced element, according to the
    // list in CSS Sizing 3 §5.2.2, then CSS Sizing 3 §5.2.1c requires us to
    // resolve the percentage part of the preferred main size property against
    // zero, yielding a definite specified size suggestion. Here we can use a
    // zero percentage basis to fulfill this requirement.
    if (aFlexItem.Frame()->IsPercentageResolvedAgainstZero(*mainStyleSize,
                                                           *maxMainStyleSize)) {
      return LogicalSize(cbWM, 0, 0);
    }
    return aItemReflowInput.mContainingBlockSize.ConvertTo(cbWM, itemWM);
  };

  // Compute the specified size suggestion, which is the main-size property if
  // it's definite.
  nscoord specifiedSizeSuggestion = nscoord_MAX;

  if (aAxisTracker.IsRowOriented()) {
    // TODO(dholbert): We need to handle 'stretch' (and its prefixed aliases)
    // here; that's tracked in bug 1936942.  (Note that we do handle 'stretch'
    // in our column-oriented "else" clause below, via the call to
    // ComputeBSizeValueHandlingStretch.)
    if (mainStyleSize->IsLengthPercentage()) {
      // NOTE: We ignore extremum inline-size. This is OK because the caller is
      // responsible for computing the min-content inline-size and min()'ing it
      // with the value we return.
      specifiedSizeSuggestion = aFlexItem.Frame()->ComputeISizeValue(
          cbWM, PercentageBasisForItem(), boxSizingAdjust,
          mainStyleSize->AsLengthPercentage());
    }
  } else {
    // NOTE: We ignore specified block-sizes that behave as 'auto', as
    // identified by IsAutoBSize(); that's OK because the caller is responsible
    // for computing the content-based block-size and and min()'ing it with the
    // value we return.
    const auto percentageBasisBSize = PercentageBasisForItem().BSize(cbWM);
    if (!nsLayoutUtils::IsAutoBSize(*mainStyleSize, percentageBasisBSize)) {
      specifiedSizeSuggestion = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
          percentageBasisBSize, aFlexItem.MarginSizeInMainAxis(),
          aFlexItem.BorderPaddingSizeInMainAxis(), boxSizingAdjust.BSize(cbWM),
          *mainStyleSize);
    }
  }

  if (specifiedSizeSuggestion != nscoord_MAX) {
    // We have the specified size suggestion. Return it now since we don't need
    // to consider transferred size suggestion.
    FLEX_LOGV("Specified size suggestion: %d", specifiedSizeSuggestion);
    return specifiedSizeSuggestion;
  }

  // Compute the transferred size suggestion, which is the cross size converted
  // through the aspect ratio (if the item is replaced, and it has an aspect
  // ratio and a definite cross size).
  if (const auto& aspectRatio = aFlexItem.GetAspectRatio();
      aFlexItem.Frame()->IsReplaced() && aspectRatio &&
      aFlexItem.IsCrossSizeDefinite(aItemReflowInput)) {
    // We have a usable aspect ratio. (not going to divide by 0)
    nscoord transferredSizeSuggestion = aspectRatio.ComputeRatioDependentSize(
        aFlexItem.MainAxis(), cbWM, aFlexItem.CrossSize(), boxSizingAdjust);

    // Clamp the transferred size suggestion by any definite min and max
    // cross size converted through the aspect ratio.
    transferredSizeSuggestion = aFlexItem.ClampMainSizeViaCrossAxisConstraints(
        transferredSizeSuggestion, aItemReflowInput);

    FLEX_LOGV("Transferred size suggestion: %d", transferredSizeSuggestion);
    return transferredSizeSuggestion;
  }

  return nscoord_MAX;
}

// Note: If & when we handle "min-height: min-content" for flex items,
// we may want to resolve that in this function, too.
void nsFlexContainerFrame::ResolveAutoFlexBasisAndMinSize(
    FlexItem& aFlexItem, const ReflowInput& aItemReflowInput,
    const FlexboxAxisTracker& aAxisTracker) {
  // (Note: We can guarantee that the flex-basis will have already been
  // resolved if the main axis is the same as the item's inline
  // axis. Inline-axis values should always be resolvable without reflow.)
  const bool isMainSizeAuto =
      (!aFlexItem.IsInlineAxisMainAxis() &&
       NS_UNCONSTRAINEDSIZE == aFlexItem.FlexBaseSize());

  const bool isMainMinSizeAuto = aFlexItem.NeedsMinSizeAutoResolution();

  if (!isMainSizeAuto && !isMainMinSizeAuto) {
    // Nothing to do; this function is only needed for flex items
    // with a used flex-basis of "auto" or a min-main-size of "auto".
    return;
  }

  FLEX_ITEM_LOG(
      aFlexItem.Frame(),
      "Resolving auto main size? %s; resolving auto min main size? %s",
      YesOrNo(isMainSizeAuto), YesOrNo(isMainMinSizeAuto));

  nscoord resolvedMinSize;  // (only set/used if isMainMinSizeAuto==true)
  bool minSizeNeedsToMeasureContent = false;  // assume the best
  if (isMainMinSizeAuto) {
    if (IsLegacyWebkitBox()) {
      // Allow flex items in a legacy flex container to shrink below their
      // automatic minimum size by setting the resolved minimum size to zero.
      // This behavior is not in the spec, but it aligns with blink and webkit's
      // implementation.
      resolvedMinSize = 0;
    } else {
      // Resolve the min-size, except for considering the min-content size.
      // (We'll consider that later, if we need to.)
      resolvedMinSize = PartiallyResolveAutoMinSize(aFlexItem, aItemReflowInput,
                                                    aAxisTracker);
    }
    if (resolvedMinSize > 0) {
      // If resolvedMinSize were already at 0, we could skip calculating content
      // size suggestion because it can't go any lower.
      minSizeNeedsToMeasureContent = true;
    }
  }

  const bool flexBasisNeedsToMeasureContent = isMainSizeAuto;

  // Measure content, if needed (w/ intrinsic-width method or a reflow)
  if (minSizeNeedsToMeasureContent || flexBasisNeedsToMeasureContent) {
    // Compute the content size suggestion, which is the min-content size in the
    // main axis.
    nscoord contentSizeSuggestion = nscoord_MAX;

    if (aFlexItem.IsInlineAxisMainAxis()) {
      if (minSizeNeedsToMeasureContent) {
        // Compute the flex item's content size suggestion, which is the
        // 'min-content' size on the main axis.
        // https://drafts.csswg.org/css-flexbox-1/#content-size-suggestion
        const auto cbWM = aAxisTracker.GetWritingMode();
        const auto itemWM = aFlexItem.GetWritingMode();
        const nscoord availISize = 0;  // for min-content size
        StyleSizeOverrides sizeOverrides;
        sizeOverrides.mStyleISize.emplace(StyleSize::Auto());
        if (aFlexItem.IsStretched()) {
          sizeOverrides.mStyleBSize.emplace(aFlexItem.StyleCrossSize());
        }
        const auto sizeInItemWM = aFlexItem.Frame()->ComputeSize(
            aItemReflowInput, itemWM, aItemReflowInput.mContainingBlockSize,
            availISize,
            aItemReflowInput.ComputedLogicalMargin(itemWM).Size(itemWM),
            aItemReflowInput.ComputedLogicalBorderPadding(itemWM).Size(itemWM),
            sizeOverrides, {ComputeSizeFlag::ShrinkWrap});

        contentSizeSuggestion = aAxisTracker.MainComponent(
            sizeInItemWM.mLogicalSize.ConvertTo(cbWM, itemWM));
      }
      NS_ASSERTION(!flexBasisNeedsToMeasureContent,
                   "flex-basis:auto should have been resolved in the "
                   "reflow input, for horizontal flexbox. It shouldn't need "
                   "special handling here");
    } else {
      // If this item is flexible (in its block axis)...
      // OR if we're measuring its 'auto' min-BSize, with its main-size (in its
      // block axis) being something non-"auto"...
      // THEN: we assume that the computed BSize that we're reflowing with now
      // could be different from the one we'll use for this flex item's
      // "actual" reflow later on.  In that case, we need to be sure the flex
      // item treats this as a block-axis resize (regardless of whether there
      // are actually any ancestors being resized in that axis).
      // (Note: We don't have to do this for the inline axis, because
      // InitResizeFlags will always turn on mIsIResize on when it sees that
      // the computed ISize is different from current ISize, and that's all we
      // need.)
      bool forceBResizeForMeasuringReflow =
          !aFlexItem.IsFrozen() ||          // Is the item flexible?
          !flexBasisNeedsToMeasureContent;  // Are we *only* measuring it for
                                            // 'min-block-size:auto'?

      const ReflowInput& flexContainerRI = *aItemReflowInput.mParentReflowInput;
      nscoord contentBSize = MeasureFlexItemContentBSize(
          aFlexItem, forceBResizeForMeasuringReflow, flexContainerRI);
      if (minSizeNeedsToMeasureContent) {
        contentSizeSuggestion = contentBSize;
      }
      if (flexBasisNeedsToMeasureContent) {
        aFlexItem.SetFlexBaseSizeAndMainSize(contentBSize);
        aFlexItem.SetIsFlexBaseSizeContentBSize();
      }
    }

    if (minSizeNeedsToMeasureContent) {
      // Clamp the content size suggestion by any definite min and max cross
      // size converted through the aspect ratio.
      if (aFlexItem.HasAspectRatio()) {
        contentSizeSuggestion = aFlexItem.ClampMainSizeViaCrossAxisConstraints(
            contentSizeSuggestion, aItemReflowInput);
      }

      FLEX_LOGV("Content size suggestion: %d", contentSizeSuggestion);
      resolvedMinSize = std::min(resolvedMinSize, contentSizeSuggestion);

      // Clamp the resolved min main size by the max main size if it's definite.
      if (aFlexItem.MainMaxSize() != NS_UNCONSTRAINEDSIZE) {
        resolvedMinSize = std::min(resolvedMinSize, aFlexItem.MainMaxSize());
      } else if (MOZ_UNLIKELY(resolvedMinSize > nscoord_MAX)) {
        NS_WARNING("Bogus resolved auto min main size!");
        // Our resolved min-size is bogus, probably due to some huge sizes in
        // the content. Clamp it to the valid nscoord range, so that we can at
        // least depend on it being <= the max-size (which is also the
        // nscoord_MAX sentinel value if we reach this point).
        resolvedMinSize = nscoord_MAX;
      }
      FLEX_LOGV("Resolved auto min main size: %d", resolvedMinSize);

      if (resolvedMinSize == contentSizeSuggestion) {
        // When we are here, we've measured the item's content-based size, and
        // we used it as the resolved auto min main size. Record the fact so
        // that we can use it to determine whether we allow a flex item to grow
        // its block-size in ReflowFlexItem().
        aFlexItem.SetIsMainMinSizeContentBSize();
      }
    }
  }

  if (isMainMinSizeAuto) {
    aFlexItem.UpdateMainMinSize(resolvedMinSize);
  }
}

/**
 * A cached result for a flex item's block-axis measuring reflow. This cache
 * prevents us from doing exponential reflows in cases of deeply nested flex
 * and scroll frames.
 *
 * We store the cached value in the flex item's frame property table, for
 * simplicity.
 *
 * Right now, we cache the following as a "key", from the item's ReflowInput:
 *   - its ComputedSize
 *   - its min/max block size (in case its ComputedBSize is unconstrained)
 *   - its AvailableBSize
 * ...and we cache the following as the "value", from the item's ReflowOutput:
 *   - its final content-box BSize
 *
 * The assumption here is that a given flex item measurement from our "value"
 * won't change unless one of the pieces of the "key" change, or the flex
 * item's intrinsic size is marked as dirty (due to a style or DOM change).
 * (The latter will cause the cached value to be discarded, in
 * nsIFrame::MarkIntrinsicISizesDirty.)
 *
 * Note that the components of "Key" (mComputed{MinB,MaxB,}Size and
 * mAvailableBSize) are sufficient to catch any changes to the flex container's
 * size that the item may care about for its measuring reflow. Specifically:
 *  - If the item cares about the container's size (e.g. if it has a percent
 *    height and the container's height changes, in a horizontal-WM container)
 *    then that'll be detectable via the item's ReflowInput's "ComputedSize()"
 *    differing from the value in our Key.  And the same applies for the
 *    inline axis.
 *  - If the item is fragmentable (pending bug 939897) and its measured BSize
 *    depends on where it gets fragmented, then that sort of change can be
 *    detected due to the item's ReflowInput's "AvailableBSize()" differing
 *    from the value in our Key.
 *
 * One particular case to consider (& need to be sure not to break when
 * changing this class): the flex item's computed BSize may change between
 * measuring reflows due to how the mIsFlexContainerMeasuringBSize flag affects
 * size computation (see bug 1336708). This is one reason we need to use the
 * computed BSize as part of the key.
 */
class nsFlexContainerFrame::CachedBAxisMeasurement {
  struct Key {
    const LogicalSize mComputedSize;
    const nscoord mComputedMinBSize;
    const nscoord mComputedMaxBSize;
    const nscoord mAvailableBSize;

    explicit Key(const ReflowInput& aRI)
        : mComputedSize(aRI.ComputedSize()),
          mComputedMinBSize(aRI.ComputedMinBSize()),
          mComputedMaxBSize(aRI.ComputedMaxBSize()),
          mAvailableBSize(aRI.AvailableBSize()) {}

    bool operator==(const Key& aOther) const = default;
  };

  const Key mKey;

  // This could/should be const, but it's non-const for now just because it's
  // assigned via a series of steps in the constructor body:
  nscoord mBSize;

 public:
  CachedBAxisMeasurement(const ReflowInput& aReflowInput,
                         const ReflowOutput& aReflowOutput)
      : mKey(aReflowInput) {
    // To get content-box bsize, we have to subtract off border & padding
    // (and floor at 0 in case the border/padding are too large):
    WritingMode itemWM = aReflowInput.GetWritingMode();
    nscoord borderBoxBSize = aReflowOutput.BSize(itemWM);
    mBSize =
        borderBoxBSize -
        aReflowInput.ComputedLogicalBorderPadding(itemWM).BStartEnd(itemWM);
    mBSize = std::max(0, mBSize);
  }

  /**
   * Returns true if this cached flex item measurement is valid for (i.e. can
   * be expected to match the output of) a measuring reflow whose input
   * parameters are given via aReflowInput.
   */
  bool IsValidFor(const ReflowInput& aReflowInput) const {
    return mKey == Key(aReflowInput);
  }

  nscoord BSize() const { return mBSize; }
};

/**
 * A cached copy of various metrics from a flex item's most recent final reflow.
 * It can be used to determine whether we can optimize away the flex item's
 * final reflow, when we perform an incremental reflow of its flex container.
 */
class CachedFinalReflowMetrics final {
 public:
  CachedFinalReflowMetrics(const ReflowInput& aReflowInput,
                           const ReflowOutput& aReflowOutput)
      : CachedFinalReflowMetrics(aReflowInput.GetWritingMode(), aReflowInput,
                                 aReflowOutput) {}

  CachedFinalReflowMetrics(const FlexItem& aItem, const LogicalSize& aSize)
      : mBorderPadding(aItem.BorderPadding().ConvertTo(
            aItem.GetWritingMode(), aItem.ContainingBlockWM())),
        mSize(aSize),
        mTreatBSizeAsIndefinite(aItem.TreatBSizeAsIndefinite()) {}

  const LogicalSize& Size() const { return mSize; }
  const LogicalMargin& BorderPadding() const { return mBorderPadding; }
  bool TreatBSizeAsIndefinite() const { return mTreatBSizeAsIndefinite; }

 private:
  // A convenience constructor with a WritingMode argument.
  CachedFinalReflowMetrics(WritingMode aWM, const ReflowInput& aReflowInput,
                           const ReflowOutput& aReflowOutput)
      : mBorderPadding(aReflowInput.ComputedLogicalBorderPadding(aWM)),
        mSize(aReflowOutput.Size(aWM) - mBorderPadding.Size(aWM)),
        mTreatBSizeAsIndefinite(aReflowInput.mFlags.mTreatBSizeAsIndefinite) {}

  // The flex item's border and padding, in its own writing-mode, that it used
  // used during its most recent "final reflow".
  LogicalMargin mBorderPadding;

  // The flex item's content-box size, in its own writing-mode, that it used
  // during its most recent "final reflow".
  LogicalSize mSize;

  // True if the flex item's BSize was considered "indefinite" in its most
  // recent "final reflow". (For a flex item "final reflow", this is fully
  // determined by the mTreatBSizeAsIndefinite flag in ReflowInput. See the
  // flag's documentation for more information.)
  bool mTreatBSizeAsIndefinite;
};

/**
 * When we instantiate/update a CachedFlexItemData, this enum must be used to
 * indicate the sort of reflow whose results we're capturing. This impacts
 * what we cache & how we use the cached information.
 */
enum class FlexItemReflowType {
  // A reflow to measure the block-axis size of a flex item (as an input to the
  // flex layout algorithm).
  Measuring,

  // A reflow with the flex item's "final" size at the end of the flex layout
  // algorithm.
  Final,
};

/**
 * This class stores information about the conditions and results for the most
 * recent ReflowChild call that we made on a given flex item.  This information
 * helps us reason about whether we can assume that a subsequent ReflowChild()
 * invocation is unnecessary & skippable.
 */
class nsFlexContainerFrame::CachedFlexItemData {
 public:
  CachedFlexItemData(const ReflowInput& aReflowInput,
                     const ReflowOutput& aReflowOutput,
                     FlexItemReflowType aType) {
    Update(aReflowInput, aReflowOutput, aType);
  }

  // This method is intended to be called after we perform either a "measuring
  // reflow" or a "final reflow" for a given flex item.
  void Update(const ReflowInput& aReflowInput,
              const ReflowOutput& aReflowOutput, FlexItemReflowType aType) {
    if (aType == FlexItemReflowType::Measuring) {
      mBAxisMeasurement.reset();
      mBAxisMeasurement.emplace(aReflowInput, aReflowOutput);
      // Clear any cached "last final reflow metrics", too, because now the most
      // recent reflow was *not* a "final reflow".
      mFinalReflowMetrics.reset();
      return;
    }

    MOZ_ASSERT(aType == FlexItemReflowType::Final);
    mFinalReflowMetrics.reset();
    mFinalReflowMetrics.emplace(aReflowInput, aReflowOutput);
  }

  // This method is intended to be called for situations where we decide to
  // skip a final reflow because we've just done a measuring reflow which left
  // us (and our descendants) with the correct sizes. In this scenario, we
  // still want to cache the size as if we did a final reflow (because we've
  // determined that the recent measuring reflow was sufficient).  That way,
  // our flex container can still skip a final reflow for this item in the
  // future as long as conditions are right.
  void Update(const FlexItem& aItem, const LogicalSize& aSize) {
    MOZ_ASSERT(!mFinalReflowMetrics,
               "This version of the method is only intended to be called when "
               "the most recent reflow was a 'measuring reflow'; and that "
               "should have cleared out mFinalReflowMetrics");

    mFinalReflowMetrics.reset();  // Just in case this assert^ fails.
    mFinalReflowMetrics.emplace(aItem, aSize);
  }

  // If the flex container needs a measuring reflow for the flex item, then the
  // resulting block-axis measurements can be cached here.  If no measurement
  // has been needed so far, then this member will be Nothing().
  Maybe<CachedBAxisMeasurement> mBAxisMeasurement;

  // The metrics that the corresponding flex item used in its most recent
  // "final reflow". (Note: the assumption here is that this reflow was this
  // item's most recent reflow of any type.  If the item ends up undergoing a
  // subsequent measuring reflow, then this value needs to be cleared, because
  // at that point it's no longer an accurate way of reasoning about the
  // current state of the frame tree.)
  Maybe<CachedFinalReflowMetrics> mFinalReflowMetrics;

  // Instances of this class are stored under this frame property, on
  // frames that are flex items:
  NS_DECLARE_FRAME_PROPERTY_DELETABLE(Prop, CachedFlexItemData)
};

void nsFlexContainerFrame::MarkCachedFlexMeasurementsDirty(
    nsIFrame* aItemFrame) {
  MOZ_ASSERT(aItemFrame->IsFlexItem());
  if (auto* cache = aItemFrame->GetProperty(CachedFlexItemData::Prop())) {
    cache->mBAxisMeasurement.reset();
    cache->mFinalReflowMetrics.reset();
  }
}

const CachedBAxisMeasurement& nsFlexContainerFrame::MeasureBSizeForFlexItem(
    FlexItem& aItem, ReflowInput& aChildReflowInput) {
  auto* cachedData = aItem.Frame()->GetProperty(CachedFlexItemData::Prop());

  if (cachedData && cachedData->mBAxisMeasurement) {
    if (!aItem.Frame()->IsSubtreeDirty() &&
        cachedData->mBAxisMeasurement->IsValidFor(aChildReflowInput)) {
      FLEX_ITEM_LOG(aItem.Frame(),
                    "[perf] Accepted cached measurement: block-size %d",
                    cachedData->mBAxisMeasurement->BSize());
      return *(cachedData->mBAxisMeasurement);
    }
    FLEX_ITEM_LOG(aItem.Frame(),
                  "[perf] Rejected cached measurement: block-size %d",
                  cachedData->mBAxisMeasurement->BSize());
  } else {
    FLEX_ITEM_LOG(aItem.Frame(), "[perf] No cached measurement");
  }

  // CachedFlexItemData is stored in item's writing mode, so we pass
  // aChildReflowInput into ReflowOutput's constructor.
  ReflowOutput childReflowOutput(aChildReflowInput);
  nsReflowStatus childStatus;

  const ReflowChildFlags flags = ReflowChildFlags::NoMoveFrame;
  const WritingMode outerWM = GetWritingMode();
  const LogicalPoint dummyPosition(outerWM);
  const nsSize dummyContainerSize;

  // We use NoMoveFrame, so the position and container size used here are
  // unimportant.
  ReflowChild(aItem.Frame(), PresContext(), childReflowOutput,
              aChildReflowInput, outerWM, dummyPosition, dummyContainerSize,
              flags, childStatus);
  aItem.SetHadMeasuringReflow();
  MaybePropagateRelativeBSizeFlagFrom(aItem);

  // We always use unconstrained available block-size to measure flex items,
  // which means they should always complete.
  MOZ_ASSERT(childStatus.IsComplete(),
             "We gave flex item unconstrained available block-size, so it "
             "should be complete");

  // Tell the child we're done with its initial reflow.
  // (Necessary for e.g. GetBaseline() to work below w/out asserting)
  FinishReflowChild(aItem.Frame(), PresContext(), childReflowOutput,
                    &aChildReflowInput, outerWM, dummyPosition,
                    dummyContainerSize, flags);

  aItem.SetAscent(childReflowOutput.BlockStartAscent());

  // Update (or add) our cached measurement, so that we can hopefully skip this
  // measuring reflow the next time around:
  if (cachedData) {
    cachedData->Update(aChildReflowInput, childReflowOutput,
                       FlexItemReflowType::Measuring);
  } else {
    cachedData = new CachedFlexItemData(aChildReflowInput, childReflowOutput,
                                        FlexItemReflowType::Measuring);
    aItem.Frame()->SetProperty(CachedFlexItemData::Prop(), cachedData);
  }
  return *(cachedData->mBAxisMeasurement);
}

/* virtual */
void nsFlexContainerFrame::MarkIntrinsicISizesDirty() {
  mCachedIntrinsicSizes.Clear();
  nsContainerFrame::MarkIntrinsicISizesDirty();
}

nscoord nsFlexContainerFrame::MeasureFlexItemContentBSize(
    FlexItem& aFlexItem, bool aForceBResizeForMeasuringReflow,
    const ReflowInput& aParentReflowInput) {
  FLEX_ITEM_LOG(aFlexItem.Frame(), "Measuring item's content block-size");

  // Set up a reflow input for measuring the flex item's content block-size:
  WritingMode wm = aFlexItem.Frame()->GetWritingMode();
  LogicalSize availSize = aParentReflowInput.ComputedSize(wm);
  availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;

  StyleSizeOverrides sizeOverrides;
  if (aFlexItem.IsStretched()) {
    sizeOverrides.mStyleISize.emplace(aFlexItem.StyleCrossSize());
    FLEX_LOGV("Cross size override: %d", aFlexItem.CrossSize());
  }
  sizeOverrides.mStyleBSize.emplace(StyleSize::Auto());

  ReflowInput childRIForMeasuringBSize(
      PresContext(), aParentReflowInput, aFlexItem.Frame(), availSize,
      Nothing(), {}, sizeOverrides, {ComputeSizeFlag::ShrinkWrap});

  // When measuring flex item's content block-size, disregard the item's
  // min-block-size and max-block-size by resetting both to to their
  // unconstraining (extreme) values. The flexbox layout algorithm does still
  // explicitly clamp both sizes when resolving the target main size.
  childRIForMeasuringBSize.SetComputedMinBSize(0);
  childRIForMeasuringBSize.SetComputedMaxBSize(NS_UNCONSTRAINEDSIZE);

  if (aForceBResizeForMeasuringReflow) {
    childRIForMeasuringBSize.SetBResize(true);
    // Not 100% sure this is needed, but be conservative for now:
    childRIForMeasuringBSize.SetBResizeForPercentages(true);
  }

  const CachedBAxisMeasurement& measurement =
      MeasureBSizeForFlexItem(aFlexItem, childRIForMeasuringBSize);

  return measurement.BSize();
}

FlexItem::FlexItem(ReflowInput& aFlexItemReflowInput, float aFlexGrow,
                   float aFlexShrink, nscoord aFlexBaseSize,
                   nscoord aMainMinSize, nscoord aMainMaxSize,
                   nscoord aTentativeCrossSize, nscoord aCrossMinSize,
                   nscoord aCrossMaxSize,
                   const FlexboxAxisTracker& aAxisTracker)
    : mFrame(aFlexItemReflowInput.mFrame),
      mFlexGrow(aFlexGrow),
      mFlexShrink(aFlexShrink),
      mAspectRatio(mFrame->GetAspectRatio()),
      mWM(aFlexItemReflowInput.GetWritingMode()),
      mCBWM(aAxisTracker.GetWritingMode()),
      mMainAxis(aAxisTracker.MainAxis()),
      mBorderPadding(aFlexItemReflowInput.ComputedLogicalBorderPadding(mCBWM)),
      mMargin(aFlexItemReflowInput.ComputedLogicalMargin(mCBWM)),
      mMainMinSize(aMainMinSize),
      mMainMaxSize(aMainMaxSize),
      mCrossMinSize(aCrossMinSize),
      mCrossMaxSize(aCrossMaxSize),
      mCrossSize(aTentativeCrossSize),
      mIsInlineAxisMainAxis(aAxisTracker.IsInlineAxisMainAxis(mWM)),
      mNeedsMinSizeAutoResolution(IsMinSizeAutoResolutionNeeded())
// mAlignSelf, mHasAnyAutoMargin see below
{
  MOZ_ASSERT(mFrame, "expecting a non-null child frame");
  MOZ_ASSERT(!mFrame->IsPlaceholderFrame(),
             "placeholder frames should not be treated as flex items");
  MOZ_ASSERT(mFrame->IsFlexItem(), "mFrame must be a flex item!");
  MOZ_ASSERT(mIsInlineAxisMainAxis ==
                 nsFlexContainerFrame::IsItemInlineAxisMainAxis(mFrame),
             "public API should be consistent with internal state (about "
             "whether flex item's inline axis is flex container's main axis)");

  const auto* container =
      static_cast<nsFlexContainerFrame*>(mFrame->GetParent());
  std::tie(mAlignSelf, mAlignSelfFlags) =
      container->UsedAlignSelfAndFlagsForItem(mFrame);

  // Our main-size is considered definite if any of these are true:
  // (a) main axis is the item's inline axis.
  // (b) flex container has definite main size.
  // (c) flex item has a definite flex basis.
  //
  // Hence, we need to take care to treat the final main-size as *indefinite*
  // if none of these conditions are satisfied.
  if (mIsInlineAxisMainAxis) {
    // The item's block-axis is the flex container's cross axis. We don't need
    // any special handling to treat cross sizes as indefinite, because the
    // cases where we stomp on the cross size with a definite value are all...
    // - situations where the spec requires us to treat the cross size as
    // definite; specifically, `align-self:stretch` whose cross size is
    // definite.
    // - situations where definiteness doesn't matter (e.g. for an element with
    // an aspect ratio, which for now are all leaf nodes and hence
    // can't have any percent-height descendants that would care about the
    // definiteness of its size. (Once bug 1528375 is fixed, we might need to
    // be more careful about definite vs. indefinite sizing on flex items with
    // aspect ratios.)
    mTreatBSizeAsIndefinite = false;
  } else {
    // The item's block-axis is the flex container's main axis. So, the flex
    // item's main size is its BSize, and is considered definite under certain
    // conditions laid out for definite flex-item main-sizes in the spec.
    const ReflowInput* containerRI = aFlexItemReflowInput.mParentReflowInput;
    if (aAxisTracker.IsRowOriented() ||
        (containerRI->ComputedBSize() != NS_UNCONSTRAINEDSIZE &&
         !containerRI->mFlags.mTreatBSizeAsIndefinite)) {
      // The flex *container* has a definite main-size (either by being
      // row-oriented [and using its own inline size which is by definition
      // definite, or by being column-oriented and having a definite
      // block-size).  The spec says this means all of the flex items'
      // post-flexing main sizes should *also* be treated as definite.
      mTreatBSizeAsIndefinite = false;
    } else if (aFlexBaseSize != NS_UNCONSTRAINEDSIZE) {
      // The flex item has a definite flex basis, which we'll treat as making
      // its main-size definite.
      mTreatBSizeAsIndefinite = false;
    } else {
      // Otherwise, we have to treat the item's BSize as indefinite.
      mTreatBSizeAsIndefinite = true;
    }
  }

  SetFlexBaseSizeAndMainSize(aFlexBaseSize);

  const nsStyleMargin* styleMargin = aFlexItemReflowInput.mStyleMargin;
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(&aFlexItemReflowInput);
  mHasAnyAutoMargin =
      styleMargin->HasInlineAxisAuto(mCBWM, anchorResolutionParams) ||
      styleMargin->HasBlockAxisAuto(mCBWM, anchorResolutionParams);

  // Assert that any "auto" margin components are set to 0.
  // (We'll resolve them later; until then, we want to treat them as 0-sized.)
#ifdef DEBUG
  {
    for (const auto side : LogicalSides::All) {
      if (styleMargin->GetMargin(side, mCBWM, anchorResolutionParams)
              ->IsAuto()) {
        MOZ_ASSERT(GetMarginComponentForSide(side) == 0,
                   "Someone else tried to resolve our auto margin");
      }
    }
  }
#endif  // DEBUG

  if (mAlignSelf == StyleAlignFlags::BASELINE ||
      mAlignSelf == StyleAlignFlags::LAST_BASELINE) {
    // Check which of the item's baselines we're meant to use (first vs. last)
    const bool usingItemFirstBaseline =
        (mAlignSelf == StyleAlignFlags::BASELINE);
    if (IsBlockAxisCrossAxis()) {
      // The flex item wants to be aligned in the cross axis using one of its
      // baselines; and the cross axis is the item's block axis, so
      // baseline-alignment in that axis makes sense.

      // To determine the item's baseline sharing group, we check whether the
      // item's block axis has the same vs. opposite flow direction as the
      // corresponding LogicalAxis on the flex container.  We do this by
      // getting the physical side that corresponds to these axes' "logical
      // start" sides, and we compare those physical sides to find out if
      // they're the same vs. opposite.
      mozilla::Side itemBlockStartSide = mWM.PhysicalSide(LogicalSide::BStart);

      // (Note: this is *not* the "flex-start" side; rather, it's the *logical*
      // i.e. WM-relative block-start or inline-start side.)
      mozilla::Side containerStartSideInCrossAxis = mCBWM.PhysicalSide(
          MakeLogicalSide(aAxisTracker.CrossAxis(), LogicalEdge::Start));

      // We already know these two Sides (the item's block-start and the
      // container's 'logical start' side for its cross axis) are in the same
      // physical axis, since we're inside of a check for
      // FlexItem::IsBlockAxisCrossAxis().  So these two Sides must be either
      // the same physical side or opposite from each other.  If the Sides are
      // the same, then the flow direction is the same, which means the item's
      // {first,last} baseline participates in the {first,last}
      // baseline-sharing group in its FlexLine.  Otherwise, the flow direction
      // is opposite, and so the item's {first,last} baseline participates in
      // the opposite i.e. {last,first} baseline-sharing group.  This is
      // roughly per css-align-3 section 9.2, specifically the definition of
      // what makes baseline alignment preferences "compatible".
      bool itemBlockAxisFlowDirMatchesContainer =
          (itemBlockStartSide == containerStartSideInCrossAxis);
      mBaselineSharingGroup =
          (itemBlockAxisFlowDirMatchesContainer == usingItemFirstBaseline)
              ? BaselineSharingGroup::First
              : BaselineSharingGroup::Last;
    } else {
      // The flex item wants to be aligned in the cross axis using one of its
      // baselines, but we cannot get its baseline because the FlexItem's block
      // axis is *orthogonal* to the container's cross axis. To handle this, we
      // are supposed to synthesize a baseline from the item's border box and
      // using that for baseline alignment.
      mBaselineSharingGroup = usingItemFirstBaseline
                                  ? BaselineSharingGroup::First
                                  : BaselineSharingGroup::Last;
    }
  }
}

// Simplified constructor for creating a special "strut" FlexItem, for a child
// with visibility:collapse. The strut has 0 main-size, and it only exists to
// impose a minimum cross size on whichever FlexLine it ends up in.
FlexItem::FlexItem(nsIFrame* aChildFrame, nscoord aCrossSize,
                   WritingMode aContainerWM,
                   const FlexboxAxisTracker& aAxisTracker)
    : mFrame(aChildFrame),
      mWM(aChildFrame->GetWritingMode()),
      mCBWM(aContainerWM),
      mMainAxis(aAxisTracker.MainAxis()),
      mBorderPadding(mCBWM),
      mMargin(mCBWM),
      mCrossSize(aCrossSize),
      // Struts don't do layout, so its WM doesn't matter at this point. So, we
      // just share container's WM for simplicity:
      mIsFrozen(true),
      mIsStrut(true),  // (this is the constructor for making struts, after all)
      mAlignSelf(StyleAlignFlags::FLEX_START) {
  MOZ_ASSERT(mFrame, "expecting a non-null child frame");
  MOZ_ASSERT(mFrame->StyleVisibility()->IsCollapse(),
             "Should only make struts for children with 'visibility:collapse'");
  MOZ_ASSERT(!mFrame->IsPlaceholderFrame(),
             "placeholder frames should not be treated as flex items");
  MOZ_ASSERT(!mFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
             "out-of-flow frames should not be treated as flex items");
}

bool FlexItem::IsMinSizeAutoResolutionNeeded() const {
  // We'll need special behavior for "min-[width|height]:auto" (whichever is in
  // the flex container's main axis) iff:
  // (a) its computed value is "auto", and
  // (b) the item is *not* a scroll container. (A scroll container's automatic
  //     minimum size is zero.)
  // https://drafts.csswg.org/css-flexbox-1/#min-size-auto
  //
  // Note that the scroll container case is redefined to be looking at the
  // computed value instead, see https://github.com/w3c/csswg-drafts/issues/7714
  const auto mainMinSize = Frame()->StylePosition()->MinSize(
      MainAxis(), ContainingBlockWM(),
      AnchorPosResolutionParams::From(Frame()));

  // "min-{height,width}:stretch" never produces an automatic minimum size. You
  // might think it would result in an automatic min-size if the containing
  // block size is indefinite, but "stretch" is instead treated as 0px in that
  // case rather than auto. This WPT requires this behavior:
  // https://wpt.live/css/css-sizing/stretch/indefinite-4.html More details &
  // discussion here: https://github.com/w3c/csswg-drafts/issues/11006
  if (mainMinSize->BehavesLikeStretchOnBlockAxis()) {
    return false;
  }
  return IsAutoOrEnumOnBSize(*mainMinSize, IsInlineAxisMainAxis()) &&
         !Frame()->StyleDisplay()->IsScrollableOverflow();
}

Maybe<nscoord> FlexItem::MeasuredBSize() const {
  auto* cachedData =
      Frame()->FirstInFlow()->GetProperty(CachedFlexItemData::Prop());
  if (!cachedData || !cachedData->mBAxisMeasurement) {
    return Nothing();
  }
  return Some(cachedData->mBAxisMeasurement->BSize());
}

nscoord FlexItem::BaselineOffsetFromOuterCrossEdge(
    mozilla::Side aStartSide, bool aUseFirstLineBaseline) const {
  // NOTE:
  //  * We only use baselines for aligning in the flex container's cross axis.
  //  * Baselines are a measurement in the item's block axis.
  if (IsBlockAxisMainAxis()) {
    // We get here if the item's block axis is *orthogonal* the container's
    // cross axis. For example, a flex item with writing-mode:horizontal-tb in a
    // column-oriented flex container. We need to synthesize the item's baseline
    // from its border-box edge.
    const bool isMainAxisHorizontal =
        mCBWM.PhysicalAxis(MainAxis()) == PhysicalAxis::Horizontal;

    // When the main axis is horizontal, the synthesized baseline is the bottom
    // edge of the item's border-box. Otherwise, when the main axis is vertical,
    // the left edge. This is for compatibility with Google Chrome.
    nscoord marginTopOrLeftToBaseline =
        isMainAxisHorizontal ? PhysicalMargin().top : PhysicalMargin().left;
    if (mCBWM.IsAlphabeticalBaseline()) {
      marginTopOrLeftToBaseline += (isMainAxisHorizontal ? CrossSize() : 0);
    } else {
      MOZ_ASSERT(mCBWM.IsCentralBaseline());
      marginTopOrLeftToBaseline += CrossSize() / 2;
    }

    return aStartSide == mozilla::eSideTop || aStartSide == mozilla::eSideLeft
               ? marginTopOrLeftToBaseline
               : OuterCrossSize() - marginTopOrLeftToBaseline;
  }

  // We get here if the item's block axis is parallel (or antiparallel) to the
  // container's cross axis. We call ResolvedAscent() to get the item's
  // baseline. If the item has no baseline, the method will synthesize one from
  // the border-box edge.
  MOZ_ASSERT(IsBlockAxisCrossAxis(),
             "Only expecting to be doing baseline computations when the "
             "cross axis is the block axis");

  mozilla::Side itemBlockStartSide = mWM.PhysicalSide(LogicalSide::BStart);

  nscoord marginBStartToBaseline = ResolvedAscent(aUseFirstLineBaseline) +
                                   PhysicalMargin().Side(itemBlockStartSide);

  return (aStartSide == itemBlockStartSide)
             ? marginBStartToBaseline
             : OuterCrossSize() - marginBStartToBaseline;
}

bool FlexItem::IsCrossSizeAuto() const {
  const auto* styleFrame = nsLayoutUtils::GetStyleFrame(mFrame);
  const nsStylePosition* stylePos = styleFrame->StylePosition();
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(styleFrame);
  // Check whichever component is in the flex container's cross axis.
  // (IsInlineAxisCrossAxis() tells us whether that's our ISize or BSize, in
  // terms of our own WritingMode, mWM.)
  return IsInlineAxisCrossAxis()
             ? stylePos->ISize(mWM, anchorResolutionParams)->IsAuto()
             : stylePos->BSize(mWM, anchorResolutionParams)->IsAuto();
}

bool FlexItem::IsCrossSizeDefinite(const ReflowInput& aItemReflowInput) const {
  if (IsStretched()) {
    // Definite cross-size, imposed via 'align-self:stretch' & flex container.
    return true;
  }

  const nsStylePosition* pos = aItemReflowInput.mStylePosition;
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(&aItemReflowInput);
  const auto itemWM = GetWritingMode();

  // The logic here should be similar to the logic for isAutoISize/isAutoBSize
  // in nsContainerFrame::ComputeSizeWithIntrinsicDimensions().
  if (IsInlineAxisCrossAxis()) {
    return !pos->ISize(itemWM, anchorResolutionParams)->IsAuto();
  }

  nscoord cbBSize = aItemReflowInput.mContainingBlockSize.BSize(itemWM);
  return !nsLayoutUtils::IsAutoBSize(
      *pos->BSize(itemWM, anchorResolutionParams), cbBSize);
}

void FlexItem::ResolveFlexBaseSizeFromAspectRatio(
    const ReflowInput& aItemReflowInput) {
  // This implements the Flex Layout Algorithm Step 3B:
  // https://drafts.csswg.org/css-flexbox-1/#algo-main-item
  // If the flex item has ...
  //  - an aspect ratio,
  //  - a [used] flex-basis of 'content', and
  //  - a definite cross size
  // then the flex base size is calculated from its inner cross size and the
  // flex item's preferred aspect ratio.
  if (HasAspectRatio() &&
      nsFlexContainerFrame::IsUsedFlexBasisContent(
          aItemReflowInput.mStylePosition->mFlexBasis,
          *aItemReflowInput.mStylePosition->Size(
              MainAxis(), mCBWM,
              AnchorPosResolutionParams::From(&aItemReflowInput))) &&
      IsCrossSizeDefinite(aItemReflowInput)) {
    const LogicalSize contentBoxSizeToBoxSizingAdjust =
        aItemReflowInput.mStylePosition->mBoxSizing == StyleBoxSizing::BorderBox
            ? BorderPadding().Size(mCBWM)
            : LogicalSize(mCBWM);
    const nscoord mainSizeFromRatio = mAspectRatio.ComputeRatioDependentSize(
        MainAxis(), mCBWM, CrossSize(), contentBoxSizeToBoxSizingAdjust);
    SetFlexBaseSizeAndMainSize(mainSizeFromRatio);
  }
}

uint32_t FlexItem::NumAutoMarginsInAxis(LogicalAxis aAxis) const {
  uint32_t numAutoMargins = 0;
  const auto* styleMargin = mFrame->StyleMargin();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(mFrame);
  for (const auto edge : {LogicalEdge::Start, LogicalEdge::End}) {
    const auto side = MakeLogicalSide(aAxis, edge);
    if (styleMargin->GetMargin(side, mCBWM, anchorResolutionParams)->IsAuto()) {
      numAutoMargins++;
    }
  }

  // Mostly for clarity:
  MOZ_ASSERT(numAutoMargins <= 2,
             "We're just looking at one item along one dimension, so we "
             "should only have examined 2 margins");

  return numAutoMargins;
}

bool FlexItem::CanMainSizeInfluenceCrossSize() const {
  if (mIsStretched) {
    // We've already had our cross-size stretched for "align-self:stretch").
    // The container is imposing its cross size on us.
    return false;
  }

  if (mIsStrut) {
    // Struts (for visibility:collapse items) have a predetermined size;
    // no need to measure anything.
    return false;
  }

  if (HasAspectRatio()) {
    // For flex items that have an aspect ratio (and maintain it, i.e. are
    // not stretched, which we already checked above): changes to main-size
    // *do* influence the cross size.
    return true;
  }

  if (IsInlineAxisCrossAxis()) {
    // If we get here, this function is really asking: "can changes to this
    // item's block size have an influence on its inline size"?

    // If a flex item's intrinsic inline size or its descendants' inline size
    // contributions depend on the item's block size, the answer is "yes".
    if (mFrame->HasAnyStateBits(
            NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE)) {
      return true;
    }
    // For blocks and tables, the answer is "no" (aside from the above special
    // case).
    if (mFrame->IsBlockFrame() || mFrame->IsTableWrapperFrame()) {
      // XXXdholbert (Maybe use an IsFrameOfType query or something more
      // general to test this across all frame types? For now, I'm just
      // optimizing for block and table, since those are common containers that
      // can contain arbitrarily-large subtrees (and that reliably have ISize
      // being unaffected by BSize, per CSS2).  So optimizing away needless
      // relayout is possible & especially valuable for these containers.)
      return false;
    }
    // Other opt-outs can go here, as they're identified as being useful
    // (particularly for containers where an extra reflow is expensive). But in
    // general, we have to assume that a flexed BSize *could* influence the
    // ISize. Some examples where this can definitely happen:
    // * Intrinsically-sized multicol with fixed-ISize columns, which adds
    // columns (i.e. grows in inline axis) depending on its block size.
    // * Intrinsically-sized multi-line column-oriented flex container, which
    // adds flex lines (i.e. grows in inline axis) depending on its block size.
  }

  // Default assumption, if we haven't proven otherwise: the resolved main size
  // *can* change the cross size.
  return true;
}

nscoord FlexItem::ClampMainSizeViaCrossAxisConstraints(
    nscoord aMainSize, const ReflowInput& aItemReflowInput) const {
  MOZ_ASSERT(HasAspectRatio(), "Caller should've checked the ratio is valid!");

  const LogicalSize contentBoxSizeToBoxSizingAdjust =
      aItemReflowInput.mStylePosition->mBoxSizing == StyleBoxSizing::BorderBox
          ? BorderPadding().Size(mCBWM)
          : LogicalSize(mCBWM);

  const nscoord mainMinSizeFromRatio = mAspectRatio.ComputeRatioDependentSize(
      MainAxis(), mCBWM, CrossMinSize(), contentBoxSizeToBoxSizingAdjust);
  nscoord clampedMainSize = std::max(aMainSize, mainMinSizeFromRatio);

  if (CrossMaxSize() != NS_UNCONSTRAINEDSIZE) {
    const nscoord mainMaxSizeFromRatio = mAspectRatio.ComputeRatioDependentSize(
        MainAxis(), mCBWM, CrossMaxSize(), contentBoxSizeToBoxSizingAdjust);
    clampedMainSize = std::min(clampedMainSize, mainMaxSizeFromRatio);
  }

  return clampedMainSize;
}

/**
 * Returns true if aFrame or any of its children have the
 * NS_FRAME_CONTAINS_RELATIVE_BSIZE flag set -- i.e. if any of these frames (or
 * their descendants) might have a relative-BSize dependency on aFrame (or its
 * ancestors).
 */
static bool FrameHasRelativeBSizeDependency(nsIFrame* aFrame) {
  if (aFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
    return true;
  }
  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* childFrame : childList.mList) {
      if (childFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
        return true;
      }
    }
  }
  return false;
}

bool FlexItem::NeedsFinalReflow(const ReflowInput& aParentReflowInput) const {
  if (!StaticPrefs::layout_flexbox_item_final_reflow_optimization_enabled()) {
    FLEX_ITEM_LOG(mFrame,
                  "[perf] Item needed a final reflow due to optimization being "
                  "disabled via the preference");
    return true;
  }

  // NOTE: We can have continuations from an earlier constrained reflow.
  if (mFrame->GetPrevInFlow() || mFrame->GetNextInFlow()) {
    // This is an item has continuation(s). Reflow it.
    FLEX_ITEM_LOG(mFrame,
                  "[frag] Item needed a final reflow due to continuation(s)");
    return true;
  }

  // A flex item can grow its block-size in a fragmented context if there's any
  // force break within it (bug 1663079), or if it has a repeated table header
  // or footer (bug 1744363). We currently always reflow it.
  //
  // Bug 1815294: investigate if we can design a more specific condition to
  // prevent triggering O(n^2) behavior when printing a deeply-nested flex
  // container.
  if (aParentReflowInput.IsInFragmentedContext()) {
    FLEX_ITEM_LOG(mFrame,
                  "[frag] Item needed both a measuring reflow and a final "
                  "reflow due to being in a fragmented context");
    return true;
  }

  // Flex item's final content-box size (in terms of its own writing-mode):
  const LogicalSize finalSize = mIsInlineAxisMainAxis
                                    ? LogicalSize(mWM, mMainSize, mCrossSize)
                                    : LogicalSize(mWM, mCrossSize, mMainSize);

  if (HadMeasuringReflow()) {
    // We've already reflowed this flex item once, to measure it. In that
    // reflow, did its frame happen to end up with the correct final size
    // that the flex container would like it to have?
    if (finalSize != mFrame->ContentSize(mWM)) {
      // The measuring reflow left the item with a different size than its
      // final flexed size. So, we need to reflow to give it the correct size.
      FLEX_ITEM_LOG(mFrame,
                    "[perf] Item needed both a measuring reflow and a final "
                    "reflow due to measured size disagreeing with final size");
      return true;
    }

    if (FrameHasRelativeBSizeDependency(mFrame)) {
      // This item has descendants with relative BSizes who may care that its
      // size may now be considered "definite" in the final reflow (whereas it
      // was indefinite during the measuring reflow).
      FLEX_ITEM_LOG(mFrame,
                    "[perf] Item needed both a measuring reflow and a final "
                    "reflow due to BSize potentially becoming definite");
      return true;
    }

    // If we get here, then this flex item had a measuring reflow, it left us
    // with the correct size, none of its descendants care that its BSize may
    // now be considered definite, and it can fit into the available block-size.
    // So it doesn't need a final reflow.
    //
    // We now cache this size as if we had done a final reflow (because we've
    // determined that the measuring reflow was effectively equivalent).  This
    // way, in our next time through flex layout, we may be able to skip both
    // the measuring reflow *and* the final reflow (if conditions are the same
    // as they are now).
    if (auto* cache = mFrame->GetProperty(CachedFlexItemData::Prop())) {
      cache->Update(*this, finalSize);
    }

    return false;
  }

  // This item didn't receive a measuring reflow (at least, not during this
  // reflow of our flex container).  We may still be able to skip reflowing it
  // (i.e. return false from this function), if its subtree is clean & its most
  // recent "final reflow" had it at the correct content-box size &
  // definiteness.
  // Let's check for each condition that would still require us to reflow:
  if (mFrame->IsSubtreeDirty()) {
    FLEX_ITEM_LOG(
        mFrame,
        "[perf] Item needed a final reflow due to its subtree being dirty");
    return true;
  }

  // Cool; this item & its subtree haven't experienced any style/content
  // changes that would automatically require a reflow.

  // Did we cache the metrics from its most recent "final reflow"?
  auto* cache = mFrame->GetProperty(CachedFlexItemData::Prop());
  if (!cache || !cache->mFinalReflowMetrics) {
    FLEX_ITEM_LOG(mFrame,
                  "[perf] Item needed a final reflow due to lacking a cached "
                  "mFinalReflowMetrics (maybe cache was cleared)");
    return true;
  }

  // Does the cached size match our current size?
  if (cache->mFinalReflowMetrics->Size() != finalSize) {
    FLEX_ITEM_LOG(mFrame,
                  "[perf] Item needed a final reflow due to having a different "
                  "content box size vs. its most recent final reflow");
    return true;
  }

  // Does the cached border and padding match our current ones?
  //
  // Note: this is just to detect cases where we have a percent padding whose
  // basis has changed. Any other sort of change to BorderPadding() (e.g. a new
  // specified value) should result in the frame being marked dirty via proper
  // change hint (see nsStylePadding::CalcDifference()), which will force it to
  // reflow.
  if (cache->mFinalReflowMetrics->BorderPadding() !=
      BorderPadding().ConvertTo(mWM, mCBWM)) {
    FLEX_ITEM_LOG(mFrame,
                  "[perf] Item needed a final reflow due to having a different "
                  "border and padding vs. its most recent final reflow");
    return true;
  }

  // The flex container is giving this flex item the same size that the item
  // had on its most recent "final reflow". But if its definiteness changed and
  // one of the descendants cares, then it would still need a reflow.
  if (cache->mFinalReflowMetrics->TreatBSizeAsIndefinite() !=
          mTreatBSizeAsIndefinite &&
      FrameHasRelativeBSizeDependency(mFrame)) {
    FLEX_ITEM_LOG(mFrame,
                  "[perf] Item needed a final reflow due to having its BSize "
                  "change definiteness & having a rel-BSize child");
    return true;
  }

  // If we get here, we can skip the final reflow! (The item's subtree isn't
  // dirty, and our current conditions are sufficiently similar to the most
  // recent "final reflow" that it should have left our subtree in the correct
  // state.)
  FLEX_ITEM_LOG(mFrame, "[perf] Item didn't need a final reflow");
  return false;
}

// Keeps track of our position along a particular axis (where a '0' position
// corresponds to the 'start' edge of that axis).
// This class shouldn't be instantiated directly -- rather, it should only be
// instantiated via its subclasses defined below.
class MOZ_STACK_CLASS PositionTracker {
 public:
  // Accessor for the current value of the position that we're tracking.
  inline nscoord Position() const { return mPosition; }
  inline LogicalAxis Axis() const { return mAxis; }

  inline LogicalSide StartSide() {
    return MakeLogicalSide(
        mAxis, mIsAxisReversed ? LogicalEdge::End : LogicalEdge::Start);
  }

  inline LogicalSide EndSide() {
    return MakeLogicalSide(
        mAxis, mIsAxisReversed ? LogicalEdge::Start : LogicalEdge::End);
  }

  // Advances our position across the start edge of the given margin, in the
  // axis we're tracking.
  void EnterMargin(const LogicalMargin& aMargin) {
    mPosition += aMargin.Side(StartSide(), mWM);
  }

  // Advances our position across the end edge of the given margin, in the axis
  // we're tracking.
  void ExitMargin(const LogicalMargin& aMargin) {
    mPosition += aMargin.Side(EndSide(), mWM);
  }

  // Advances our current position from the start side of a child frame's
  // border-box to the frame's upper or left edge (depending on our axis).
  // (Note that this is a no-op if our axis grows in the same direction as
  // the corresponding logical axis.)
  void EnterChildFrame(nscoord aChildFrameSize) {
    if (mIsAxisReversed) {
      mPosition += aChildFrameSize;
    }
  }

  // Advances our current position from a frame's upper or left border-box edge
  // (whichever is in the axis we're tracking) to the 'end' side of the frame
  // in the axis that we're tracking. (Note that this is a no-op if our axis
  // is reversed with respect to the corresponding logical axis.)
  void ExitChildFrame(nscoord aChildFrameSize) {
    if (!mIsAxisReversed) {
      mPosition += aChildFrameSize;
    }
  }

  // Delete copy-constructor & reassignment operator, to prevent accidental
  // (unnecessary) copying.
  PositionTracker(const PositionTracker&) = delete;
  PositionTracker& operator=(const PositionTracker&) = delete;

 protected:
  // Protected constructor, to be sure we're only instantiated via a subclass.
  PositionTracker(WritingMode aWM, LogicalAxis aAxis, bool aIsAxisReversed)
      : mWM(aWM), mAxis(aAxis), mIsAxisReversed(aIsAxisReversed) {}

  // Member data:
  // The position we're tracking.
  nscoord mPosition = 0;

  // The flex container's writing mode.
  const WritingMode mWM;

  // The axis along which we're moving.
  const LogicalAxis mAxis = LogicalAxis::Inline;

  // Is the axis along which we're moving reversed (e.g. LTR vs RTL) with
  // respect to the corresponding axis on the flex container's WM?
  const bool mIsAxisReversed = false;
};

// Tracks our position in the main axis, when we're laying out flex items.
// The "0" position represents the main-start edge of the flex container's
// content-box.
class MOZ_STACK_CLASS MainAxisPositionTracker : public PositionTracker {
 public:
  MainAxisPositionTracker(const FlexboxAxisTracker& aAxisTracker,
                          const FlexLine* aLine,
                          const StyleContentDistribution& aJustifyContent,
                          nscoord aContentBoxMainSize);

  ~MainAxisPositionTracker() {
    MOZ_ASSERT(mNumPackingSpacesRemaining == 0,
               "miscounted the number of packing spaces");
    MOZ_ASSERT(mNumAutoMarginsInMainAxis == 0,
               "miscounted the number of auto margins");
  }

  // Advances past the gap space (if any) between two flex items
  void TraverseGap(nscoord aGapSize) { mPosition += aGapSize; }

  // Advances past the packing space (if any) between two flex items
  void TraversePackingSpace();

  // If aItem has any 'auto' margins in the main axis, this method updates the
  // corresponding values in its margin.
  void ResolveAutoMarginsInMainAxis(FlexItem& aItem);

 private:
  nscoord mPackingSpaceRemaining = 0;
  uint32_t mNumAutoMarginsInMainAxis = 0;
  uint32_t mNumPackingSpacesRemaining = 0;
  StyleContentDistribution mJustifyContent = {StyleAlignFlags::AUTO};
};

// Utility class for managing our position along the cross axis along
// the whole flex container (at a higher level than a single line).
// The "0" position represents the cross-start edge of the flex container's
// content-box.
class MOZ_STACK_CLASS CrossAxisPositionTracker : public PositionTracker {
 public:
  CrossAxisPositionTracker(nsTArray<FlexLine>& aLines,
                           const ReflowInput& aReflowInput,
                           nscoord aContentBoxCrossSize,
                           bool aIsCrossSizeDefinite,
                           const FlexboxAxisTracker& aAxisTracker,
                           const nscoord aCrossGapSize);

  // Advances past the gap (if any) between two flex lines
  void TraverseGap() { mPosition += mCrossGapSize; }

  // Advances past the packing space (if any) between two flex lines
  void TraversePackingSpace();

  // Advances past the given FlexLine
  void TraverseLine(FlexLine& aLine) { mPosition += aLine.LineCrossSize(); }

  // Redeclare the frame-related methods from PositionTracker with
  // = delete, to be sure (at compile time) that no client code can invoke
  // them. (Unlike the other PositionTracker derived classes, this class here
  // deals with FlexLines, not with individual FlexItems or frames.)
  void EnterMargin(const LogicalMargin& aMargin) = delete;
  void ExitMargin(const LogicalMargin& aMargin) = delete;
  void EnterChildFrame(nscoord aChildFrameSize) = delete;
  void ExitChildFrame(nscoord aChildFrameSize) = delete;

 private:
  nscoord mPackingSpaceRemaining = 0;
  uint32_t mNumPackingSpacesRemaining = 0;
  StyleContentDistribution mAlignContent = {StyleAlignFlags::AUTO};

  const nscoord mCrossGapSize;
};

// Utility class for managing our position along the cross axis, *within* a
// single flex line.
class MOZ_STACK_CLASS SingleLineCrossAxisPositionTracker
    : public PositionTracker {
 public:
  explicit SingleLineCrossAxisPositionTracker(
      const FlexboxAxisTracker& aAxisTracker);

  void ResolveAutoMarginsInCrossAxis(const FlexLine& aLine, FlexItem& aItem);

  void EnterAlignPackingSpace(const FlexLine& aLine, const FlexItem& aItem,
                              const FlexboxAxisTracker& aAxisTracker);

  // Resets our position to the cross-start edge of this line.
  inline void ResetPosition() { mPosition = 0; }
};

//----------------------------------------------------------------------

// Frame class boilerplate
// =======================

NS_QUERYFRAME_HEAD(nsFlexContainerFrame)
  NS_QUERYFRAME_ENTRY(nsFlexContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

NS_IMPL_FRAMEARENA_HELPERS(nsFlexContainerFrame)

nsContainerFrame* NS_NewFlexContainerFrame(PresShell* aPresShell,
                                           ComputedStyle* aStyle) {
  return new (aPresShell)
      nsFlexContainerFrame(aStyle, aPresShell->GetPresContext());
}

//----------------------------------------------------------------------

// nsFlexContainerFrame Method Implementations
// ===========================================

/* virtual */
nsFlexContainerFrame::~nsFlexContainerFrame() = default;

/* virtual */
void nsFlexContainerFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                                nsIFrame* aPrevInFlow) {
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);

  if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER)) {
    AddStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT);
  }

  auto displayInside = StyleDisplay()->DisplayInside();
  // If this frame is for a scrollable element, then it will actually have
  // "display:block", and its *parent frame* will have the real
  // flex-flavored display value. So in that case, check the parent frame to
  // find out if we're legacy.
  //
  // TODO(emilio): Maybe ::-moz-scrolled-content and co should inherit `display`
  // (or a blockified version thereof, to not hit bug 456484).
  if (displayInside == StyleDisplayInside::Flow) {
    MOZ_ASSERT(StyleDisplay()->mDisplay == StyleDisplay::Block);
    MOZ_ASSERT(Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent,
               "The only way a nsFlexContainerFrame can have 'display:block' "
               "should be if it's the inner part of a scrollable element");
    displayInside = GetParent()->StyleDisplay()->DisplayInside();
  }

  if (displayInside == StyleDisplayInside::WebkitBox) {
    AddStateBits(NS_STATE_FLEX_IS_EMULATING_LEGACY_WEBKIT_BOX);
  }
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsFlexContainerFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"FlexContainer"_ns, aResult);
}
#endif

void nsFlexContainerFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                            const nsDisplayListSet& aLists) {
  nsDisplayListCollection tempLists(aBuilder);
  DisplayBorderBackgroundOutline(aBuilder, tempLists);
  if (HidesContent()) {
    tempLists.MoveTo(aLists);
    return;
  }

  if (GetPrevInFlow()) {
    DisplayOverflowContainers(aBuilder, tempLists);
  }

  // Our children are all block-level, so their borders/backgrounds all go on
  // the BlockBorderBackgrounds list.
  nsDisplayListSet childLists(tempLists, tempLists.BlockBorderBackgrounds());

  CSSOrderAwareFrameIterator iter(
      this, FrameChildListID::Principal,
      CSSOrderAwareFrameIterator::ChildFilter::IncludeAll,
      OrderStateForIter(this), OrderingPropertyForIter(this));

  const auto flags = DisplayFlagsForFlexOrGridItem();
  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* childFrame = *iter;
    BuildDisplayListForChild(aBuilder, childFrame, childLists, flags);
  }

  if (GetPrevInFlow() || GetNextInFlow()) {
    DisplayAbsoluteFramesNotBuiltByPlaceholder(aBuilder, tempLists);
  }

  tempLists.MoveTo(aLists);
}

void FlexLine::FreezeItemsEarly(bool aIsUsingFlexGrow,
                                ComputedFlexLineInfo* aLineInfo) {
  // After we've established the type of flexing we're doing (growing vs.
  // shrinking), and before we try to flex any items, we freeze items that
  // obviously *can't* flex.
  //
  // Quoting the spec:
  //  # Freeze, setting its target main size to its hypothetical main size...
  //  #  - any item that has a flex factor of zero
  //  #  - if using the flex grow factor: any item that has a flex base size
  //  #    greater than its hypothetical main size
  //  #  - if using the flex shrink factor: any item that has a flex base size
  //  #    smaller than its hypothetical main size
  //  https://drafts.csswg.org/css-flexbox/#resolve-flexible-lengths
  //
  // (NOTE: At this point, item->MainSize() *is* the item's hypothetical
  // main size, since SetFlexBaseSizeAndMainSize() sets it up that way, and the
  // item hasn't had a chance to flex away from that yet.)

  // Since this loop only operates on unfrozen flex items, we can break as
  // soon as we have seen all of them.
  uint32_t numUnfrozenItemsToBeSeen = NumItems() - mNumFrozenItems;
  for (FlexItem& item : Items()) {
    if (numUnfrozenItemsToBeSeen == 0) {
      break;
    }

    if (!item.IsFrozen()) {
      numUnfrozenItemsToBeSeen--;
      bool shouldFreeze = (0.0f == item.GetFlexFactor(aIsUsingFlexGrow));
      if (!shouldFreeze) {
        if (aIsUsingFlexGrow) {
          if (item.FlexBaseSize() > item.MainSize()) {
            shouldFreeze = true;
          }
        } else {  // using flex-shrink
          if (item.FlexBaseSize() < item.MainSize()) {
            shouldFreeze = true;
          }
        }
      }
      if (shouldFreeze) {
        // Freeze item! (at its hypothetical main size)
        item.Freeze();
        if (item.FlexBaseSize() < item.MainSize()) {
          item.SetWasMinClamped();
        } else if (item.FlexBaseSize() > item.MainSize()) {
          item.SetWasMaxClamped();
        }
        mNumFrozenItems++;
      }
    }
  }

  MOZ_ASSERT(numUnfrozenItemsToBeSeen == 0, "miscounted frozen items?");
}

// Based on the sign of aTotalViolation, this function freezes a subset of our
// flexible sizes, and restores the remaining ones to their initial pref sizes.
void FlexLine::FreezeOrRestoreEachFlexibleSize(const nscoord aTotalViolation,
                                               bool aIsFinalIteration) {
  enum FreezeType {
    eFreezeEverything,
    eFreezeMinViolations,
    eFreezeMaxViolations
  };

  FreezeType freezeType;
  if (aTotalViolation == 0) {
    freezeType = eFreezeEverything;
  } else if (aTotalViolation > 0) {
    freezeType = eFreezeMinViolations;
  } else {  // aTotalViolation < 0
    freezeType = eFreezeMaxViolations;
  }

  // Since this loop only operates on unfrozen flex items, we can break as
  // soon as we have seen all of them.
  uint32_t numUnfrozenItemsToBeSeen = NumItems() - mNumFrozenItems;
  for (FlexItem& item : Items()) {
    if (numUnfrozenItemsToBeSeen == 0) {
      break;
    }

    if (!item.IsFrozen()) {
      numUnfrozenItemsToBeSeen--;

      MOZ_ASSERT(!item.HadMinViolation() || !item.HadMaxViolation(),
                 "Can have either min or max violation, but not both");

      bool hadMinViolation = item.HadMinViolation();
      bool hadMaxViolation = item.HadMaxViolation();
      if (eFreezeEverything == freezeType ||
          (eFreezeMinViolations == freezeType && hadMinViolation) ||
          (eFreezeMaxViolations == freezeType && hadMaxViolation)) {
        MOZ_ASSERT(item.MainSize() >= item.MainMinSize(),
                   "Freezing item at a size below its minimum");
        MOZ_ASSERT(item.MainSize() <= item.MainMaxSize(),
                   "Freezing item at a size above its maximum");

        item.Freeze();
        if (hadMinViolation) {
          item.SetWasMinClamped();
        } else if (hadMaxViolation) {
          item.SetWasMaxClamped();
        }
        mNumFrozenItems++;
      } else if (MOZ_UNLIKELY(aIsFinalIteration)) {
        // XXXdholbert If & when bug 765861 is fixed, we should upgrade this
        // assertion to be fatal except in documents with enormous lengths.
        NS_ERROR(
            "Final iteration still has unfrozen items, this shouldn't"
            " happen unless there was nscoord under/overflow.");
        item.Freeze();
        mNumFrozenItems++;
      }  // else, we'll reset this item's main size to its flex base size on the
         // next iteration of this algorithm.

      if (!item.IsFrozen()) {
        // Clear this item's violation(s), now that we've dealt with them
        item.ClearViolationFlags();
      }
    }
  }

  MOZ_ASSERT(numUnfrozenItemsToBeSeen == 0, "miscounted frozen items?");
}

void FlexLine::ResolveFlexibleLengths(nscoord aFlexContainerMainSize,
                                      ComputedFlexLineInfo* aLineInfo) {
  // In this function, we use 64-bit coord type to avoid integer overflow in
  // case several of the individual items have huge hypothetical main sizes,
  // which can happen with percent-width table-layout:fixed descendants. Here we
  // promote the container's main size to 64-bit to make the arithmetic
  // convenient.
  AuCoord64 flexContainerMainSize(aFlexContainerMainSize);

  // Before we start resolving sizes: if we have an aLineInfo structure to fill
  // out, we inform it of each item's base size, and we initialize the "delta"
  // for each item to 0. (And if the flex algorithm wants to grow or shrink the
  // item, we'll update this delta further down.)
  if (aLineInfo) {
    uint32_t itemIndex = 0;
    for (FlexItem& item : Items()) {
      aLineInfo->mItems[itemIndex].mMainBaseSize = item.FlexBaseSize();
      aLineInfo->mItems[itemIndex].mMainDeltaSize = 0;
      ++itemIndex;
    }
  }

  // Determine whether we're going to be growing or shrinking items.
  const bool isUsingFlexGrow =
      (mTotalOuterHypotheticalMainSize < flexContainerMainSize);

  if (aLineInfo) {
    aLineInfo->mGrowthState =
        isUsingFlexGrow ? mozilla::dom::FlexLineGrowthState::Growing
                        : mozilla::dom::FlexLineGrowthState::Shrinking;
  }

  // Do an "early freeze" for flex items that obviously can't flex in the
  // direction we've chosen:
  FreezeItemsEarly(isUsingFlexGrow, aLineInfo);

  if ((mNumFrozenItems == NumItems()) && !aLineInfo) {
    // All our items are frozen, so we have no flexible lengths to resolve,
    // and we aren't being asked to generate computed line info.
    FLEX_LOG("No flexible length to resolve");
    return;
  }
  MOZ_ASSERT(!IsEmpty() || aLineInfo,
             "empty lines should take the early-return above");

  FLEX_LOG("Resolving flexible lengths for items");

  // Subtract space occupied by our items' margins/borders/padding/gaps, so
  // we can just be dealing with the space available for our flex items' content
  // boxes.
  const AuCoord64 totalItemMBPAndGaps = mTotalItemMBP + SumOfGaps();
  const AuCoord64 spaceAvailableForFlexItemsContentBoxes =
      flexContainerMainSize - totalItemMBPAndGaps;

  Maybe<AuCoord64> origAvailableFreeSpace;

  // NOTE: I claim that this chunk of the algorithm (the looping part) needs to
  // run the loop at MOST NumItems() times.  This claim should hold up
  // because we'll freeze at least one item on each loop iteration, and once
  // we've run out of items to freeze, there's nothing left to do.  However,
  // in most cases, we'll break out of this loop long before we hit that many
  // iterations.
  for (uint32_t iterationCounter = 0; iterationCounter < NumItems();
       iterationCounter++) {
    // Set every not-yet-frozen item's used main size to its
    // flex base size, and subtract all the used main sizes from our
    // total amount of space to determine the 'available free space'
    // (positive or negative) to be distributed among our flexible items.
    AuCoord64 availableFreeSpace = spaceAvailableForFlexItemsContentBoxes;
    for (FlexItem& item : Items()) {
      if (!item.IsFrozen()) {
        item.SetMainSize(item.FlexBaseSize());
      }
      availableFreeSpace -= item.MainSize();
    }

    FLEX_LOGV("Available free space: %" PRId64 "; flex items should \"%s\"",
              availableFreeSpace.value, isUsingFlexGrow ? "grow" : "shrink");

    // The sign of our free space should agree with the type of flexing
    // (grow/shrink) that we're doing. Any disagreement should've made us use
    // the other type of flexing, or should've been resolved in
    // FreezeItemsEarly.
    //
    // Note: it's possible that an individual flex item has huge
    // margin/border/padding that makes either its
    // MarginBorderPaddingSizeInMainAxis() or OuterMainSize() negative due to
    // integer overflow. If that happens, the accumulated
    // mTotalOuterHypotheticalMainSize or mTotalItemMBP could be negative due to
    // that one item's negative (overflowed) size. Likewise, a huge main gap
    // size between flex items can also make our accumulated SumOfGaps()
    // negative. In these case, we throw up our hands and don't require
    // isUsingFlexGrow to agree with availableFreeSpace. Luckily, we won't get
    // stuck in the algorithm below, and just distribute the wrong
    // availableFreeSpace with the wrong grow/shrink factors.
    MOZ_ASSERT(!(mTotalOuterHypotheticalMainSize >= 0 && mTotalItemMBP >= 0 &&
                 totalItemMBPAndGaps >= 0) ||
                   (isUsingFlexGrow && availableFreeSpace >= 0) ||
                   (!isUsingFlexGrow && availableFreeSpace <= 0),
               "availableFreeSpace's sign should match isUsingFlexGrow");

    // If we have any free space available, give each flexible item a portion
    // of availableFreeSpace.
    if (availableFreeSpace != AuCoord64(0)) {
      // The first time we do this, we initialize origAvailableFreeSpace.
      if (!origAvailableFreeSpace) {
        origAvailableFreeSpace.emplace(availableFreeSpace);
      }

      // STRATEGY: On each item, we compute & store its "share" of the total
      // weight that we've seen so far:
      //   curWeight / weightSum
      //
      // Then, when we go to actually distribute the space (in the next loop),
      // we can simply walk backwards through the elements and give each item
      // its "share" multiplied by the remaining available space.
      //
      // SPECIAL CASE: If the sum of the weights is larger than the
      // maximum representable double (overflowing to infinity), then we can't
      // sensibly divide out proportional shares anymore. In that case, we
      // simply treat the flex item(s) with the largest weights as if
      // their weights were infinite (dwarfing all the others), and we
      // distribute all of the available space among them.
      double weightSum = 0.0;
      double flexFactorSum = 0.0;
      double largestWeight = 0.0;
      uint32_t numItemsWithLargestWeight = 0;

      // Since this loop only operates on unfrozen flex items, we can break as
      // soon as we have seen all of them.
      uint32_t numUnfrozenItemsToBeSeen = NumItems() - mNumFrozenItems;
      for (FlexItem& item : Items()) {
        if (numUnfrozenItemsToBeSeen == 0) {
          break;
        }

        if (!item.IsFrozen()) {
          numUnfrozenItemsToBeSeen--;

          const double curWeight = item.GetWeight(isUsingFlexGrow);
          const double curFlexFactor = item.GetFlexFactor(isUsingFlexGrow);
          MOZ_ASSERT(curWeight >= 0.0, "weights are non-negative");
          MOZ_ASSERT(curFlexFactor >= 0.0, "flex factors are non-negative");

          weightSum += curWeight;
          flexFactorSum += curFlexFactor;

          if (std::isfinite(weightSum)) {
            if (curWeight == 0.0) {
              item.SetShareOfWeightSoFar(0.0);
            } else {
              item.SetShareOfWeightSoFar(curWeight / weightSum);
            }
          }  // else, the sum of weights overflows to infinity, in which
             // case we don't bother with "SetShareOfWeightSoFar" since
             // we know we won't use it. (instead, we'll just give every
             // item with the largest weight an equal share of space.)

          // Update our largest-weight tracking vars
          if (curWeight > largestWeight) {
            largestWeight = curWeight;
            numItemsWithLargestWeight = 1;
          } else if (curWeight == largestWeight) {
            numItemsWithLargestWeight++;
          }
        }
      }

      MOZ_ASSERT(numUnfrozenItemsToBeSeen == 0, "miscounted frozen items?");

      if (weightSum != 0.0) {
        MOZ_ASSERT(flexFactorSum != 0.0,
                   "flex factor sum can't be 0, if a weighted sum "
                   "of its components (weightSum) is nonzero");
        if (flexFactorSum < 1.0) {
          // Our unfrozen flex items don't want all of the original free space!
          // (Their flex factors add up to something less than 1.)
          // Hence, make sure we don't distribute any more than the portion of
          // our original free space that these items actually want.
          auto totalDesiredPortionOfOrigFreeSpace =
              AuCoord64::FromRound(*origAvailableFreeSpace * flexFactorSum);

          // Clamp availableFreeSpace to be no larger than that ^^.
          // (using min or max, depending on sign).
          // This should not change the sign of availableFreeSpace (except
          // possibly by setting it to 0), as enforced by this assertion:
          NS_ASSERTION(totalDesiredPortionOfOrigFreeSpace == AuCoord64(0) ||
                           ((totalDesiredPortionOfOrigFreeSpace > 0) ==
                            (availableFreeSpace > 0)),
                       "When we reduce available free space for flex "
                       "factors < 1, we shouldn't change the sign of the "
                       "free space...");

          if (availableFreeSpace > 0) {
            availableFreeSpace = std::min(availableFreeSpace,
                                          totalDesiredPortionOfOrigFreeSpace);
          } else {
            availableFreeSpace = std::max(availableFreeSpace,
                                          totalDesiredPortionOfOrigFreeSpace);
          }
        }

        FLEX_LOGV("Distributing available space:");
        // Since this loop only operates on unfrozen flex items, we can break as
        // soon as we have seen all of them.
        numUnfrozenItemsToBeSeen = NumItems() - mNumFrozenItems;

        // NOTE: It's important that we traverse our items in *reverse* order
        // here, for correct width distribution according to the items'
        // "ShareOfWeightSoFar" progressively-calculated values.
        for (FlexItem& item : Reversed(Items())) {
          if (numUnfrozenItemsToBeSeen == 0) {
            break;
          }

          if (!item.IsFrozen()) {
            numUnfrozenItemsToBeSeen--;

            // To avoid rounding issues, we compute the change in size for this
            // item, and then subtract it from the remaining available space.
            AuCoord64 sizeDelta = 0;
            if (std::isfinite(weightSum)) {
              double myShareOfRemainingSpace = item.ShareOfWeightSoFar();

              MOZ_ASSERT(myShareOfRemainingSpace >= 0.0 &&
                             myShareOfRemainingSpace <= 1.0,
                         "my share should be nonnegative fractional amount");

              if (myShareOfRemainingSpace == 1.0) {
                // (We special-case 1.0 to avoid float error from converting
                // availableFreeSpace from integer*1.0 --> double --> integer)
                sizeDelta = availableFreeSpace;
              } else if (myShareOfRemainingSpace > 0.0) {
                sizeDelta = AuCoord64::FromRound(availableFreeSpace *
                                                 myShareOfRemainingSpace);
              }
            } else if (item.GetWeight(isUsingFlexGrow) == largestWeight) {
              // Total flexibility is infinite, so we're just distributing
              // the available space equally among the items that are tied for
              // having the largest weight (and this is one of those items).
              sizeDelta = AuCoord64::FromRound(
                  availableFreeSpace / double(numItemsWithLargestWeight));
              numItemsWithLargestWeight--;
            }

            availableFreeSpace -= sizeDelta;

            item.SetMainSize(item.MainSize() +
                             nscoord(sizeDelta.ToMinMaxClamped()));
            FLEX_LOGV("  Flex item %p receives %" PRId64 ", for a total of %d",
                      item.Frame(), sizeDelta.value, item.MainSize());
          }
        }

        MOZ_ASSERT(numUnfrozenItemsToBeSeen == 0, "miscounted frozen items?");

        // If we have an aLineInfo structure to fill out, capture any
        // size changes that may have occurred in the previous loop.
        // We don't do this inside the previous loop, because we don't
        // want to burden layout when aLineInfo is null.
        if (aLineInfo) {
          uint32_t itemIndex = 0;
          for (FlexItem& item : Items()) {
            if (!item.IsFrozen()) {
              // Calculate a deltaSize that represents how much the flex sizing
              // algorithm "wants" to stretch or shrink this item during this
              // pass through the algorithm. Later passes through the algorithm
              // may overwrite this, until this item is frozen. Note that this
              // value may not reflect how much the size of the item is
              // actually changed, since the size of the item will be clamped
              // to min and max values later in this pass. That's intentional,
              // since we want to report the value that the sizing algorithm
              // tried to stretch or shrink the item.
              nscoord deltaSize =
                  item.MainSize() - aLineInfo->mItems[itemIndex].mMainBaseSize;

              aLineInfo->mItems[itemIndex].mMainDeltaSize = deltaSize;
            }
            ++itemIndex;
          }
        }
      }
    }

    // Fix min/max violations:
    nscoord totalViolation = 0;  // keeps track of adjustments for min/max
    FLEX_LOGV("Checking for violations:");

    // Since this loop only operates on unfrozen flex items, we can break as
    // soon as we have seen all of them.
    uint32_t numUnfrozenItemsToBeSeen = NumItems() - mNumFrozenItems;
    for (FlexItem& item : Items()) {
      if (numUnfrozenItemsToBeSeen == 0) {
        break;
      }

      if (!item.IsFrozen()) {
        numUnfrozenItemsToBeSeen--;

        if (item.MainSize() < item.MainMinSize()) {
          // min violation
          totalViolation += item.MainMinSize() - item.MainSize();
          item.SetMainSize(item.MainMinSize());
          item.SetHadMinViolation();
        } else if (item.MainSize() > item.MainMaxSize()) {
          // max violation
          totalViolation += item.MainMaxSize() - item.MainSize();
          item.SetMainSize(item.MainMaxSize());
          item.SetHadMaxViolation();
        }
      }
    }

    MOZ_ASSERT(numUnfrozenItemsToBeSeen == 0, "miscounted frozen items?");

    FreezeOrRestoreEachFlexibleSize(totalViolation,
                                    iterationCounter + 1 == NumItems());

    FLEX_LOGV("Total violation: %d", totalViolation);

    if (mNumFrozenItems == NumItems()) {
      break;
    }

    MOZ_ASSERT(totalViolation != 0,
               "Zero violation should've made us freeze all items & break");
  }

#ifdef DEBUG
  // Post-condition: all items should've been frozen.
  // Make sure the counts match:
  MOZ_ASSERT(mNumFrozenItems == NumItems(), "All items should be frozen");

  // For good measure, check each item directly, in case our counts are busted:
  for (const FlexItem& item : Items()) {
    MOZ_ASSERT(item.IsFrozen(), "All items should be frozen");
  }
#endif  // DEBUG
}

MainAxisPositionTracker::MainAxisPositionTracker(
    const FlexboxAxisTracker& aAxisTracker, const FlexLine* aLine,
    const StyleContentDistribution& aJustifyContent,
    nscoord aContentBoxMainSize)
    : PositionTracker(aAxisTracker.GetWritingMode(), aAxisTracker.MainAxis(),
                      aAxisTracker.IsMainAxisReversed()),
      // we chip away at this below
      mPackingSpaceRemaining(aContentBoxMainSize),
      mJustifyContent(aJustifyContent) {
  // Extract the flag portion of mJustifyContent and strip off the flag bits
  // NOTE: This must happen before any assignment to mJustifyContent to
  // avoid overwriting the flag bits.
  StyleAlignFlags justifyContentFlags =
      mJustifyContent.primary & StyleAlignFlags::FLAG_BITS;
  mJustifyContent.primary &= ~StyleAlignFlags::FLAG_BITS;

  // 'normal' behaves as 'stretch', and 'stretch' behaves as 'flex-start',
  // in the main axis
  // https://drafts.csswg.org/css-align-3/#propdef-justify-content
  if (mJustifyContent.primary == StyleAlignFlags::NORMAL ||
      mJustifyContent.primary == StyleAlignFlags::STRETCH) {
    mJustifyContent.primary = StyleAlignFlags::FLEX_START;
  }

  // mPackingSpaceRemaining is initialized to the container's main size.  Now
  // we'll subtract out the main sizes of our flex items, so that it ends up
  // with the *actual* amount of packing space.
  for (const FlexItem& item : aLine->Items()) {
    mPackingSpaceRemaining -= item.OuterMainSize();
    mNumAutoMarginsInMainAxis += item.NumAutoMarginsInMainAxis();
  }

  // Subtract space required for row/col gap from the remaining packing space
  mPackingSpaceRemaining -= aLine->SumOfGaps();

  // If packing space is negative or we only have one item, 'space-between'
  // falls back to 'safe flex-start'[1], and 'space-around' & 'space-evenly'
  // fall back to 'safe center'. In those cases, it's simplest to just pretend
  // we have a different 'justify-content' value and share code.
  // https://drafts.csswg.org/css-align-3/#distribution-values
  if (mPackingSpaceRemaining < 0 || aLine->NumItems() == 1) {
    if (mJustifyContent.primary == StyleAlignFlags::SPACE_BETWEEN) {
      // [1]  XXXdholbert Per spec, we should do...
      //   justifyContentFlags = StyleAlignFlags::SAFE;
      // ...here, but for now let's not do that since it causes overflow to be
      // inadvertently clipped in situations with a reversed main axis!
      // See https://github.com/w3c/csswg-drafts/issues/11937
      mJustifyContent.primary = StyleAlignFlags::FLEX_START;
    } else if (mJustifyContent.primary == StyleAlignFlags::SPACE_AROUND ||
               mJustifyContent.primary == StyleAlignFlags::SPACE_EVENLY) {
      justifyContentFlags = StyleAlignFlags::SAFE;
      mJustifyContent.primary = StyleAlignFlags::CENTER;
    }
  }

  if (mPackingSpaceRemaining <= 0) {
    // No available packing space to use for resolving auto margins.
    mNumAutoMarginsInMainAxis = 0;
    // If packing space is negative and <overflow-position> is set to 'safe'
    // all justify options fall back to 'start'
    if (justifyContentFlags & StyleAlignFlags::SAFE) {
      mJustifyContent.primary = StyleAlignFlags::START;
    }
  }

  // Map 'left'/'right' to 'start'/'end'
  if (mJustifyContent.primary == StyleAlignFlags::LEFT ||
      mJustifyContent.primary == StyleAlignFlags::RIGHT) {
    mJustifyContent.primary =
        aAxisTracker.ResolveJustifyLeftRight(mJustifyContent.primary);
  }

  // Map 'start'/'end' to 'flex-start'/'flex-end'.
  if (mJustifyContent.primary == StyleAlignFlags::START) {
    mJustifyContent.primary = aAxisTracker.IsMainAxisReversed()
                                  ? StyleAlignFlags::FLEX_END
                                  : StyleAlignFlags::FLEX_START;
  } else if (mJustifyContent.primary == StyleAlignFlags::END) {
    mJustifyContent.primary = aAxisTracker.IsMainAxisReversed()
                                  ? StyleAlignFlags::FLEX_START
                                  : StyleAlignFlags::FLEX_END;
  }

  // Figure out how much space we'll set aside for auto margins or
  // packing spaces, and advance past any leading packing-space.
  if (mNumAutoMarginsInMainAxis == 0 && mPackingSpaceRemaining != 0 &&
      !aLine->IsEmpty()) {
    if (mJustifyContent.primary == StyleAlignFlags::FLEX_START) {
      // All packing space should go at the end --> nothing to do here.
    } else if (mJustifyContent.primary == StyleAlignFlags::FLEX_END) {
      // All packing space goes at the beginning
      mPosition += mPackingSpaceRemaining;
    } else if (mJustifyContent.primary == StyleAlignFlags::CENTER) {
      // Half the packing space goes at the beginning
      mPosition += mPackingSpaceRemaining / 2;
    } else if (mJustifyContent.primary == StyleAlignFlags::SPACE_BETWEEN ||
               mJustifyContent.primary == StyleAlignFlags::SPACE_AROUND ||
               mJustifyContent.primary == StyleAlignFlags::SPACE_EVENLY) {
      nsFlexContainerFrame::CalculatePackingSpace(
          aLine->NumItems(), mJustifyContent, &mPosition,
          &mNumPackingSpacesRemaining, &mPackingSpaceRemaining);
    } else {
      MOZ_ASSERT_UNREACHABLE("Unexpected justify-content value");
    }
  }

  MOZ_ASSERT(mNumPackingSpacesRemaining == 0 || mNumAutoMarginsInMainAxis == 0,
             "extra space should either go to packing space or to "
             "auto margins, but not to both");
}

void MainAxisPositionTracker::ResolveAutoMarginsInMainAxis(FlexItem& aItem) {
  if (mNumAutoMarginsInMainAxis) {
    const auto* styleMargin = aItem.Frame()->StyleMargin();
    const auto anchorResolutionParams =
        AnchorPosResolutionParams::From(aItem.Frame());
    for (const auto side : {StartSide(), EndSide()}) {
      if (styleMargin->GetMargin(side, mWM, anchorResolutionParams)->IsAuto()) {
        // NOTE: This integer math will skew the distribution of remainder
        // app-units towards the end, which is fine.
        nscoord curAutoMarginSize =
            mPackingSpaceRemaining / mNumAutoMarginsInMainAxis;

        MOZ_ASSERT(aItem.GetMarginComponentForSide(side) == 0,
                   "Expecting auto margins to have value '0' before we "
                   "resolve them");
        aItem.SetMarginComponentForSide(side, curAutoMarginSize);

        mNumAutoMarginsInMainAxis--;
        mPackingSpaceRemaining -= curAutoMarginSize;
      }
    }
  }
}

void MainAxisPositionTracker::TraversePackingSpace() {
  if (mNumPackingSpacesRemaining) {
    MOZ_ASSERT(mJustifyContent.primary == StyleAlignFlags::SPACE_BETWEEN ||
                   mJustifyContent.primary == StyleAlignFlags::SPACE_AROUND ||
                   mJustifyContent.primary == StyleAlignFlags::SPACE_EVENLY,
               "mNumPackingSpacesRemaining only applies for "
               "space-between/space-around/space-evenly");

    MOZ_ASSERT(mPackingSpaceRemaining >= 0,
               "ran out of packing space earlier than we expected");

    // NOTE: This integer math will skew the distribution of remainder
    // app-units towards the end, which is fine.
    nscoord curPackingSpace =
        mPackingSpaceRemaining / mNumPackingSpacesRemaining;

    mPosition += curPackingSpace;
    mNumPackingSpacesRemaining--;
    mPackingSpaceRemaining -= curPackingSpace;
  }
}

CrossAxisPositionTracker::CrossAxisPositionTracker(
    nsTArray<FlexLine>& aLines, const ReflowInput& aReflowInput,
    nscoord aContentBoxCrossSize, bool aIsCrossSizeDefinite,
    const FlexboxAxisTracker& aAxisTracker, const nscoord aCrossGapSize)
    : PositionTracker(aAxisTracker.GetWritingMode(), aAxisTracker.CrossAxis(),
                      aAxisTracker.IsCrossAxisReversed()),
      mAlignContent(aReflowInput.mStylePosition->mAlignContent),
      mCrossGapSize(aCrossGapSize) {
  // Extract and strip the flag bits from alignContent
  StyleAlignFlags alignContentFlags =
      mAlignContent.primary & StyleAlignFlags::FLAG_BITS;
  mAlignContent.primary &= ~StyleAlignFlags::FLAG_BITS;

  // 'normal' behaves as 'stretch'
  if (mAlignContent.primary == StyleAlignFlags::NORMAL) {
    mAlignContent.primary = StyleAlignFlags::STRETCH;
  }

  if (IsSingleLine(aReflowInput.mFrame, aReflowInput.mStylePosition)) {
    MOZ_ASSERT(aLines.Length() == 1,
               "If we're styled as single-line, we should only have 1 line");
    // "If the flex container is single-line and has a definite cross size, the
    // cross size of the flex line is the flex container's inner cross size."
    //
    // SOURCE: https://drafts.csswg.org/css-flexbox/#algo-cross-line
    // NOTE: This means (by definition) that there's no packing space, which
    // means we don't need to be concerned with "align-content" at all and we
    // can return early. This is handy, because this is the usual case (for
    // single-line flexbox).
    if (aIsCrossSizeDefinite) {
      aLines[0].SetLineCrossSize(aContentBoxCrossSize);
      return;
    }

    // "If the flex container is single-line, then clamp the line's
    // cross-size to be within the container's computed min and max cross-size
    // properties."
    aLines[0].SetLineCrossSize(
        aReflowInput.ApplyMinMaxBSize(aLines[0].LineCrossSize()));
  }

  // NOTE: The rest of this function should essentially match
  // MainAxisPositionTracker's constructor, though with FlexLines instead of
  // FlexItems, and with the additional value "stretch" (and of course with
  // cross sizes instead of main sizes.)

  // Figure out how much packing space we have (container's cross size minus
  // all the lines' cross sizes).  Also, share this loop to count how many
  // lines we have. (We need that count in some cases below.)
  mPackingSpaceRemaining = aContentBoxCrossSize;
  uint32_t numLines = 0;
  for (FlexLine& line : aLines) {
    mPackingSpaceRemaining -= line.LineCrossSize();
    numLines++;
  }

  // Subtract space required for row/col gap from the remaining packing space
  MOZ_ASSERT(numLines >= 1,
             "GenerateFlexLines should've produced at least 1 line");
  mPackingSpaceRemaining -= aCrossGapSize * (numLines - 1);

  // If packing space is negative, 'stretch' behaves like 'flex-start',
  // 'space-between' behaves like 'safe flex-start', and 'space-around' and
  // 'space-evenly' behave like 'safe center'. In those cases, it's simplest to
  // just pretend we have a different 'align-content' value and share code. (If
  // we only have one line, all of the 'space-*' keywords fall back as well, but
  // 'stretch' doesn't because even a single line can still stretch.)
  // https://drafts.csswg.org/css-align-3/#distribution-values
  if (mPackingSpaceRemaining < 0 &&
      mAlignContent.primary == StyleAlignFlags::STRETCH) {
    mAlignContent.primary = StyleAlignFlags::FLEX_START;
  } else if (mPackingSpaceRemaining < 0 || numLines == 1) {
    if (mAlignContent.primary == StyleAlignFlags::SPACE_BETWEEN) {
      alignContentFlags = StyleAlignFlags::SAFE;
      mAlignContent.primary = StyleAlignFlags::FLEX_START;
    } else if (mAlignContent.primary == StyleAlignFlags::SPACE_AROUND ||
               mAlignContent.primary == StyleAlignFlags::SPACE_EVENLY) {
      alignContentFlags = StyleAlignFlags::SAFE;
      mAlignContent.primary = StyleAlignFlags::CENTER;
    }
  }

  // If <overflow-position> is 'safe' and packing space is negative
  // all align options fall back to 'start'
  if ((alignContentFlags & StyleAlignFlags::SAFE) &&
      mPackingSpaceRemaining < 0) {
    mAlignContent.primary = StyleAlignFlags::START;
  }

  // Map 'start'/'end' to 'flex-start'/'flex-end'.
  if (mAlignContent.primary == StyleAlignFlags::START) {
    mAlignContent.primary = aAxisTracker.IsCrossAxisReversed()
                                ? StyleAlignFlags::FLEX_END
                                : StyleAlignFlags::FLEX_START;
  } else if (mAlignContent.primary == StyleAlignFlags::END) {
    mAlignContent.primary = aAxisTracker.IsCrossAxisReversed()
                                ? StyleAlignFlags::FLEX_START
                                : StyleAlignFlags::FLEX_END;
  }

  // Figure out how much space we'll set aside for packing spaces, and advance
  // past any leading packing-space.
  if (mPackingSpaceRemaining != 0) {
    if (mAlignContent.primary == StyleAlignFlags::BASELINE ||
        mAlignContent.primary == StyleAlignFlags::LAST_BASELINE) {
      // TODO: Bug 1480850 will implement 'align-content: [first/last] baseline'
      // for flexbox. Until then, behaves as if align-content is 'flex-start' by
      // doing nothing.
    } else if (mAlignContent.primary == StyleAlignFlags::FLEX_START) {
      // All packing space should go at the end --> nothing to do here.
    } else if (mAlignContent.primary == StyleAlignFlags::FLEX_END) {
      // All packing space goes at the beginning
      mPosition += mPackingSpaceRemaining;
    } else if (mAlignContent.primary == StyleAlignFlags::CENTER) {
      // Half the packing space goes at the beginning
      mPosition += mPackingSpaceRemaining / 2;
    } else if (mAlignContent.primary == StyleAlignFlags::SPACE_BETWEEN ||
               mAlignContent.primary == StyleAlignFlags::SPACE_AROUND ||
               mAlignContent.primary == StyleAlignFlags::SPACE_EVENLY) {
      nsFlexContainerFrame::CalculatePackingSpace(
          numLines, mAlignContent, &mPosition, &mNumPackingSpacesRemaining,
          &mPackingSpaceRemaining);
    } else if (mAlignContent.primary == StyleAlignFlags::STRETCH) {
      // Split space equally between the lines:
      MOZ_ASSERT(mPackingSpaceRemaining > 0,
                 "negative packing space should make us use 'flex-start' "
                 "instead of 'stretch' (and we shouldn't bother with this "
                 "code if we have 0 packing space)");

      uint32_t numLinesLeft = numLines;
      for (FlexLine& line : aLines) {
        // Our share is the amount of space remaining, divided by the number
        // of lines remainig.
        MOZ_ASSERT(numLinesLeft > 0, "miscalculated num lines");
        nscoord shareOfExtraSpace = mPackingSpaceRemaining / numLinesLeft;
        nscoord newSize = line.LineCrossSize() + shareOfExtraSpace;
        line.SetLineCrossSize(newSize);

        mPackingSpaceRemaining -= shareOfExtraSpace;
        numLinesLeft--;
      }
      MOZ_ASSERT(numLinesLeft == 0, "miscalculated num lines");
    } else {
      MOZ_ASSERT_UNREACHABLE("Unexpected align-content value");
    }
  }
}

void CrossAxisPositionTracker::TraversePackingSpace() {
  if (mNumPackingSpacesRemaining) {
    MOZ_ASSERT(mAlignContent.primary == StyleAlignFlags::SPACE_BETWEEN ||
                   mAlignContent.primary == StyleAlignFlags::SPACE_AROUND ||
                   mAlignContent.primary == StyleAlignFlags::SPACE_EVENLY,
               "mNumPackingSpacesRemaining only applies for "
               "space-between/space-around/space-evenly");

    MOZ_ASSERT(mPackingSpaceRemaining >= 0,
               "ran out of packing space earlier than we expected");

    // NOTE: This integer math will skew the distribution of remainder
    // app-units towards the end, which is fine.
    nscoord curPackingSpace =
        mPackingSpaceRemaining / mNumPackingSpacesRemaining;

    mPosition += curPackingSpace;
    mNumPackingSpacesRemaining--;
    mPackingSpaceRemaining -= curPackingSpace;
  }
}

SingleLineCrossAxisPositionTracker::SingleLineCrossAxisPositionTracker(
    const FlexboxAxisTracker& aAxisTracker)
    : PositionTracker(aAxisTracker.GetWritingMode(), aAxisTracker.CrossAxis(),
                      aAxisTracker.IsCrossAxisReversed()) {}

void FlexLine::ComputeCrossSizeAndBaseline(
    const FlexboxAxisTracker& aAxisTracker) {
  // NOTE: in these "cross{Start,End}ToFurthest{First,Last}Baseline" variables,
  // the "first/last" term is referring to the flex *line's* baseline-sharing
  // groups, which may or may not match any flex *item's* exact align-self
  // value. See the code that sets FlexItem::mBaselineSharingGroup for more
  // details.
  nscoord crossStartToFurthestFirstBaseline = nscoord_MIN;
  nscoord crossEndToFurthestFirstBaseline = nscoord_MIN;
  nscoord crossStartToFurthestLastBaseline = nscoord_MIN;
  nscoord crossEndToFurthestLastBaseline = nscoord_MIN;

  nscoord largestOuterCrossSize = 0;
  for (const FlexItem& item : Items()) {
    nscoord curOuterCrossSize = item.OuterCrossSize();

    if ((item.AlignSelf() == StyleAlignFlags::BASELINE ||
         item.AlignSelf() == StyleAlignFlags::LAST_BASELINE) &&
        item.NumAutoMarginsInCrossAxis() == 0) {
      const bool usingItemFirstBaseline =
          (item.AlignSelf() == StyleAlignFlags::BASELINE);

      // Find distance from our item's cross-start and cross-end margin-box
      // edges to its baseline.
      //
      // Here's a diagram of a flex-item that we might be doing this on.
      // "mmm" is the margin-box, "bbb" is the border-box. The bottom of
      // the text "BASE" is the baseline.
      //
      // ---(cross-start)---
      //                ___              ___            ___
      //   mmmmmmmmmmmm  |                |margin-start  |
      //   m          m  |               _|_   ___       |
      //   m bbbbbbbb m  |curOuterCrossSize     |        |crossStartToBaseline
      //   m b      b m  |                      |ascent  |
      //   m b BASE b m  |                     _|_      _|_
      //   m b      b m  |                               |
      //   m bbbbbbbb m  |                               |crossEndToBaseline
      //   m          m  |                               |
      //   mmmmmmmmmmmm _|_                             _|_
      //
      // ---(cross-end)---
      //
      // We already have the curOuterCrossSize, margin-start, and the ascent.
      // * We can get crossStartToBaseline by adding margin-start + ascent.
      // * If we subtract that from the curOuterCrossSize, we get
      //   crossEndToBaseline.

      nscoord crossStartToBaseline = item.BaselineOffsetFromOuterCrossEdge(
          aAxisTracker.CrossAxisPhysicalStartSide(), usingItemFirstBaseline);
      nscoord crossEndToBaseline = curOuterCrossSize - crossStartToBaseline;

      // Now, update our "largest" values for these (across all the flex items
      // in this flex line), so we can use them in computing the line's cross
      // size below:
      if (item.ItemBaselineSharingGroup() == BaselineSharingGroup::First) {
        crossStartToFurthestFirstBaseline =
            std::max(crossStartToFurthestFirstBaseline, crossStartToBaseline);
        crossEndToFurthestFirstBaseline =
            std::max(crossEndToFurthestFirstBaseline, crossEndToBaseline);
      } else {
        crossStartToFurthestLastBaseline =
            std::max(crossStartToFurthestLastBaseline, crossStartToBaseline);
        crossEndToFurthestLastBaseline =
            std::max(crossEndToFurthestLastBaseline, crossEndToBaseline);
      }
    } else {
      largestOuterCrossSize =
          std::max(largestOuterCrossSize, curOuterCrossSize);
    }
  }

  // The line's baseline offset is the distance from the line's edge to the
  // furthest item-baseline. The item(s) with that baseline will be exactly
  // aligned with the line's edge.
  mFirstBaselineOffset = crossStartToFurthestFirstBaseline;
  mLastBaselineOffset = crossEndToFurthestLastBaseline;

  // The line's cross-size is the larger of:
  //  (a) [largest cross-start-to-baseline + largest baseline-to-cross-end] of
  //      all baseline-aligned items with no cross-axis auto margins...
  // and
  //  (b) [largest cross-start-to-baseline + largest baseline-to-cross-end] of
  //      all last baseline-aligned items with no cross-axis auto margins...
  // and
  //  (c) largest cross-size of all other children.
  mLineCrossSize = std::max(
      std::max(
          crossStartToFurthestFirstBaseline + crossEndToFurthestFirstBaseline,
          crossStartToFurthestLastBaseline + crossEndToFurthestLastBaseline),
      largestOuterCrossSize);
}

nscoord FlexLine::ExtractBaselineOffset(
    BaselineSharingGroup aBaselineGroup) const {
  auto LastBaselineOffsetFromStartEdge = [this]() {
    // Convert the distance to be relative from the line's cross-start edge.
    const nscoord offset = LastBaselineOffset();
    return offset != nscoord_MIN ? LineCrossSize() - offset : offset;
  };

  auto PrimaryBaseline = [=, this]() {
    return aBaselineGroup == BaselineSharingGroup::First
               ? FirstBaselineOffset()
               : LastBaselineOffsetFromStartEdge();
  };
  auto SecondaryBaseline = [=, this]() {
    return aBaselineGroup == BaselineSharingGroup::First
               ? LastBaselineOffsetFromStartEdge()
               : FirstBaselineOffset();
  };

  const nscoord primaryBaseline = PrimaryBaseline();
  if (primaryBaseline != nscoord_MIN) {
    return primaryBaseline;
  }
  return SecondaryBaseline();
}

void FlexItem::MaybeResolveStretchedCrossSize(nscoord aLineCrossSize) {
  // If we've already been stretched, we can bail out early.
  // No need to redo the calculation.
  if (mIsStretched) {
    return;
  }

  auto* fc = static_cast<nsFlexContainerFrame*>(mFrame->GetParent());
  if (!ShouldStretchCrossSize(fc, mFrame, mCBWM, CrossAxis())) {
    return;
  }

  ResolveStretchedCrossSize(aLineCrossSize);
}

void FlexItem::ResolveStretchedCrossSize(nscoord aLineCrossSize) {
  ApplyStretchedCrossSize(nsLayoutUtils::ComputeStretchContentBoxBSize(
      aLineCrossSize, MarginSizeInCrossAxis(), BorderPaddingSizeInCrossAxis()));
}

void FlexItem::ApplyStretchedCrossSize(nscoord aStretchedCrossSize) {
  MOZ_ASSERT(!mIsStretched, "Should only stretch once");
  // Update the cross-size (clamped to its min/max range) & make a note that
  // it's stretched, so we know to override the reflow input's computed
  // cross-size in our final reflow.
  SetCrossSize(CSSMinMax(aStretchedCrossSize, mCrossMinSize, mCrossMaxSize));
  mIsStretched = true;
}

static nsBlockFrame* FindFlexItemBlockFrame(nsIFrame* aFrame) {
  if (nsBlockFrame* block = do_QueryFrame(aFrame)) {
    return block;
  }
  for (nsIFrame* f : aFrame->PrincipalChildList()) {
    if (nsBlockFrame* block = FindFlexItemBlockFrame(f)) {
      return block;
    }
  }
  return nullptr;
}

nsBlockFrame* FlexItem::BlockFrame() const {
  return FindFlexItemBlockFrame(Frame());
}

void SingleLineCrossAxisPositionTracker::ResolveAutoMarginsInCrossAxis(
    const FlexLine& aLine, FlexItem& aItem) {
  // Subtract the space that our item is already occupying, to see how much
  // space (if any) is available for its auto margins.
  nscoord spaceForAutoMargins = aLine.LineCrossSize() - aItem.OuterCrossSize();

  if (spaceForAutoMargins <= 0) {
    return;  // No available space  --> nothing to do
  }

  uint32_t numAutoMargins = aItem.NumAutoMarginsInCrossAxis();
  if (numAutoMargins == 0) {
    return;  // No auto margins --> nothing to do.
  }

  // OK, we have at least one auto margin and we have some available space.
  // Give each auto margin a share of the space.
  const auto* styleMargin = aItem.Frame()->StyleMargin();
  const auto anchorResolutionParams =
      AnchorPosResolutionParams::From(aItem.Frame());
  for (const auto side : {StartSide(), EndSide()}) {
    if (styleMargin->GetMargin(side, mWM, anchorResolutionParams)->IsAuto()) {
      MOZ_ASSERT(aItem.GetMarginComponentForSide(side) == 0,
                 "Expecting auto margins to have value '0' before we "
                 "update them");

      // NOTE: integer divison is fine here; numAutoMargins is either 1 or 2.
      // If it's 2 & spaceForAutoMargins is odd, 1st margin gets smaller half.
      nscoord curAutoMarginSize = spaceForAutoMargins / numAutoMargins;
      aItem.SetMarginComponentForSide(side, curAutoMarginSize);
      numAutoMargins--;
      spaceForAutoMargins -= curAutoMarginSize;
    }
  }
}

void SingleLineCrossAxisPositionTracker::EnterAlignPackingSpace(
    const FlexLine& aLine, const FlexItem& aItem,
    const FlexboxAxisTracker& aAxisTracker) {
  // We don't do align-self alignment on items that have auto margins
  // in the cross axis.
  if (aItem.NumAutoMarginsInCrossAxis()) {
    return;
  }

  StyleAlignFlags alignSelf = aItem.AlignSelf();
  // NOTE: 'stretch' behaves like 'flex-start' once we've stretched any
  // auto-sized items (which we've already done).
  if (alignSelf == StyleAlignFlags::STRETCH) {
    alignSelf = StyleAlignFlags::FLEX_START;
  }

  // Map 'self-start'/'self-end' to 'start'/'end'
  if (alignSelf == StyleAlignFlags::SELF_START ||
      alignSelf == StyleAlignFlags::SELF_END) {
    const LogicalAxis logCrossAxis =
        aAxisTracker.IsRowOriented() ? LogicalAxis::Block : LogicalAxis::Inline;
    const WritingMode cWM = aAxisTracker.GetWritingMode();
    const bool sameStart =
        cWM.ParallelAxisStartsOnSameSide(logCrossAxis, aItem.GetWritingMode());
    alignSelf = sameStart == (alignSelf == StyleAlignFlags::SELF_START)
                    ? StyleAlignFlags::START
                    : StyleAlignFlags::END;
  }

  // Map 'start'/'end' to 'flex-start'/'flex-end'.
  if (alignSelf == StyleAlignFlags::START) {
    alignSelf = aAxisTracker.IsCrossAxisReversed()
                    ? StyleAlignFlags::FLEX_END
                    : StyleAlignFlags::FLEX_START;
  } else if (alignSelf == StyleAlignFlags::END) {
    alignSelf = aAxisTracker.IsCrossAxisReversed() ? StyleAlignFlags::FLEX_START
                                                   : StyleAlignFlags::FLEX_END;
  }

  // 'align-self' falls back to 'flex-start' if it is 'center'/'flex-end' and we
  // have cross axis overflow
  // XXX we should really be falling back to 'start' as of bug 1472843
  if (aLine.LineCrossSize() < aItem.OuterCrossSize() &&
      (aItem.AlignSelfFlags() & StyleAlignFlags::SAFE)) {
    alignSelf = StyleAlignFlags::FLEX_START;
  }

  if (alignSelf == StyleAlignFlags::FLEX_START) {
    // No space to skip over -- we're done.
  } else if (alignSelf == StyleAlignFlags::FLEX_END) {
    mPosition += aLine.LineCrossSize() - aItem.OuterCrossSize();
  } else if (alignSelf == StyleAlignFlags::CENTER ||
             alignSelf == StyleAlignFlags::ANCHOR_CENTER) {
    // TODO(dshin, Bug 1909339): For now, treat `anchor-center` as `center`.
    // Note: If cross-size is odd, the "after" space will get the extra unit.
    mPosition += (aLine.LineCrossSize() - aItem.OuterCrossSize()) / 2;
  } else if (alignSelf == StyleAlignFlags::BASELINE ||
             alignSelf == StyleAlignFlags::LAST_BASELINE) {
    const bool usingItemFirstBaseline =
        (alignSelf == StyleAlignFlags::BASELINE);

    // The first-baseline sharing group gets (collectively) aligned to the
    // FlexLine's cross-start side, and similarly the last-baseline sharing
    // group gets snapped to the cross-end side.
    const bool isFirstBaselineSharingGroup =
        aItem.ItemBaselineSharingGroup() == BaselineSharingGroup::First;
    const mozilla::Side alignSide =
        isFirstBaselineSharingGroup ? aAxisTracker.CrossAxisPhysicalStartSide()
                                    : aAxisTracker.CrossAxisPhysicalEndSide();

    // To compute the aligned position for our flex item, we determine:
    // (1) The distance from the item's alignSide edge to the item's relevant
    //     baseline.
    nscoord itemBaselineOffset = aItem.BaselineOffsetFromOuterCrossEdge(
        alignSide, usingItemFirstBaseline);

    // (2) The distance between the FlexLine's alignSide edge and the relevant
    //     baseline-sharing-group's baseline position.
    nscoord lineBaselineOffset = isFirstBaselineSharingGroup
                                     ? aLine.FirstBaselineOffset()
                                     : aLine.LastBaselineOffset();

    NS_ASSERTION(lineBaselineOffset >= itemBaselineOffset,
                 "failed at finding largest baseline offset");

    // (3) The difference between the above offsets, which tells us how far we
    //     need to shift the item away from the FlexLine's alignSide edge so
    //     that its baseline is at the proper position for its group.
    nscoord itemOffsetFromLineEdge = lineBaselineOffset - itemBaselineOffset;

    if (isFirstBaselineSharingGroup) {
      // alignSide is the line's cross-start edge. mPosition is already there.
      // From there, we step *forward* by the baseline adjustment:
      mPosition += itemOffsetFromLineEdge;
    } else {
      // alignSide is the line's cross-end edge. Advance mPosition to align
      // item with that edge (as in FLEX_END case)...
      mPosition += aLine.LineCrossSize() - aItem.OuterCrossSize();
      // ...and step *back* by the baseline adjustment:
      mPosition -= itemOffsetFromLineEdge;
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected align-self value");
  }
}

FlexboxAxisInfo::FlexboxAxisInfo(const nsIFrame* aFlexContainer) {
  MOZ_ASSERT(aFlexContainer && aFlexContainer->IsFlexContainerFrame(),
             "Only flex containers may be passed to this constructor!");
  if (aFlexContainer->IsLegacyWebkitBox()) {
    InitAxesFromLegacyProps(aFlexContainer);
  } else {
    InitAxesFromModernProps(aFlexContainer);
  }
}

void FlexboxAxisInfo::InitAxesFromLegacyProps(const nsIFrame* aFlexContainer) {
  const nsStyleXUL* styleXUL = aFlexContainer->StyleXUL();

  const bool boxOrientIsVertical =
      styleXUL->mBoxOrient == StyleBoxOrient::Vertical;
  const bool wmIsVertical = aFlexContainer->GetWritingMode().IsVertical();

  // If box-orient agrees with our writing-mode, then we're "row-oriented"
  // (i.e. the flexbox main axis is the same as our writing mode's inline
  // direction).  Otherwise, we're column-oriented (i.e. the flexbox's main
  // axis is perpendicular to the writing-mode's inline direction).
  mIsRowOriented = (boxOrientIsVertical == wmIsVertical);

  // Legacy flexbox can use "-webkit-box-direction: reverse" to reverse the
  // main axis (so it runs in the reverse direction of the inline axis):
  mIsMainAxisReversed = styleXUL->mBoxDirection == StyleBoxDirection::Reverse;

  // Legacy flexbox does not support reversing the cross axis -- it has no
  // equivalent of modern flexbox's "flex-wrap: wrap-reverse".
  mIsCrossAxisReversed = false;
}

void FlexboxAxisInfo::InitAxesFromModernProps(const nsIFrame* aFlexContainer) {
  const nsStylePosition* stylePos = aFlexContainer->StylePosition();
  StyleFlexDirection flexDirection = stylePos->mFlexDirection;

  // Determine main axis:
  switch (flexDirection) {
    case StyleFlexDirection::Row:
      mIsRowOriented = true;
      mIsMainAxisReversed = false;
      break;
    case StyleFlexDirection::RowReverse:
      mIsRowOriented = true;
      mIsMainAxisReversed = true;
      break;
    case StyleFlexDirection::Column:
      mIsRowOriented = false;
      mIsMainAxisReversed = false;
      break;
    case StyleFlexDirection::ColumnReverse:
      mIsRowOriented = false;
      mIsMainAxisReversed = true;
      break;
  }

  // "flex-wrap: wrap-reverse" reverses our cross axis.
  mIsCrossAxisReversed = !!(stylePos->mFlexWrap & StyleFlexWrap::WRAP_REVERSE);
}

FlexboxAxisTracker::FlexboxAxisTracker(
    const nsFlexContainerFrame* aFlexContainer)
    : mWM(aFlexContainer->GetWritingMode()), mAxisInfo(aFlexContainer) {}

LogicalSide FlexboxAxisTracker::MainAxisStartSide() const {
  return MakeLogicalSide(
      MainAxis(), IsMainAxisReversed() ? LogicalEdge::End : LogicalEdge::Start);
}

LogicalSide FlexboxAxisTracker::CrossAxisStartSide() const {
  return MakeLogicalSide(CrossAxis(), IsCrossAxisReversed()
                                          ? LogicalEdge::End
                                          : LogicalEdge::Start);
}

void nsFlexContainerFrame::GenerateFlexLines(
    const ReflowInput& aReflowInput, const nscoord aTentativeContentBoxMainSize,
    const nscoord aTentativeContentBoxCrossSize,
    const nsTArray<StrutInfo>& aStruts, const FlexboxAxisTracker& aAxisTracker,
    nscoord aMainGapSize, nsTArray<nsIFrame*>& aPlaceholders,
    nsTArray<FlexLine>& aLines, bool& aHasCollapsedItems) {
  MOZ_ASSERT(aLines.IsEmpty(), "Expecting outparam to start out empty");

  auto ConstructNewFlexLine = [&aLines, aMainGapSize]() {
    return aLines.EmplaceBack(aMainGapSize);
  };

  // We have at least one FlexLine. Even an empty flex container has a single
  // (empty) flex line.
  FlexLine* curLine = ConstructNewFlexLine();

  nscoord wrapThreshold;
  if (IsSingleLine(aReflowInput.mFrame, aReflowInput.mStylePosition)) {
    // Not wrapping. Set threshold to sentinel value that tells us not to wrap.
    wrapThreshold = NS_UNCONSTRAINEDSIZE;
  } else {
    // Wrapping! Set wrap threshold to flex container's content-box main-size.
    wrapThreshold = aTentativeContentBoxMainSize;

    // If the flex container doesn't have a definite content-box main-size
    // (e.g. if main axis is vertical & 'height' is 'auto'), make sure we at
    // least wrap when we hit its max main-size.
    if (wrapThreshold == NS_UNCONSTRAINEDSIZE) {
      const nscoord flexContainerMaxMainSize =
          aAxisTracker.MainComponent(aReflowInput.ComputedMaxSize());
      wrapThreshold = flexContainerMaxMainSize;
    }
  }

  // Tracks the index of the next strut, in aStruts (and when this hits
  // aStruts.Length(), that means there are no more struts):
  uint32_t nextStrutIdx = 0;

  // Overall index of the current flex item in the flex container. (This gets
  // checked against entries in aStruts.)
  uint32_t itemIdxInContainer = 0;

  CSSOrderAwareFrameIterator iter(
      this, FrameChildListID::Principal,
      CSSOrderAwareFrameIterator::ChildFilter::IncludeAll,
      CSSOrderAwareFrameIterator::OrderState::Unknown,
      OrderingPropertyForIter(this));

  AddOrRemoveStateBits(NS_STATE_FLEX_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER,
                       iter.ItemsAreAlreadyInOrder());

  const bool useMozBoxCollapseBehavior =
      StyleVisibility()->UseLegacyCollapseBehavior();

  for (; !iter.AtEnd(); iter.Next()) {
    nsIFrame* childFrame = *iter;
    // Don't create flex items / lines for placeholder frames:
    if (childFrame->IsPlaceholderFrame()) {
      aPlaceholders.AppendElement(childFrame);
      continue;
    }

    const bool collapsed = childFrame->StyleVisibility()->IsCollapse();
    aHasCollapsedItems = aHasCollapsedItems || collapsed;

    if (useMozBoxCollapseBehavior && collapsed) {
      // Legacy visibility:collapse behavior: make a 0-sized strut. (No need to
      // bother with aStruts and remembering cross size.)
      curLine->Items().EmplaceBack(childFrame, 0, aReflowInput.GetWritingMode(),
                                   aAxisTracker);
    } else if (nextStrutIdx < aStruts.Length() &&
               aStruts[nextStrutIdx].mItemIdx == itemIdxInContainer) {
      // Use the simplified "strut" FlexItem constructor:
      curLine->Items().EmplaceBack(childFrame,
                                   aStruts[nextStrutIdx].mStrutCrossSize,
                                   aReflowInput.GetWritingMode(), aAxisTracker);
      nextStrutIdx++;
    } else {
      GenerateFlexItemForChild(*curLine, childFrame, aReflowInput, aAxisTracker,
                               aTentativeContentBoxCrossSize);
    }

    // Check if we need to wrap the newly appended item to a new line, i.e. if
    // its outer hypothetical main size pushes our line over the threshold.
    // But we don't wrap if the line-length is unconstrained, nor do we wrap if
    // this was the first item on the line.
    if (wrapThreshold != NS_UNCONSTRAINEDSIZE &&
        curLine->Items().Length() > 1) {
      // If the line will be longer than wrapThreshold or at least as long as
      // nscoord_MAX because of the newly appended item, then wrap and move the
      // item to a new line.
      auto newOuterSize = curLine->TotalOuterHypotheticalMainSize();
      newOuterSize += curLine->Items().LastElement().OuterMainSize();

      // Account for gap between this line's previous item and this item.
      newOuterSize += aMainGapSize;

      if (newOuterSize >= nscoord_MAX || newOuterSize > wrapThreshold) {
        curLine = ConstructNewFlexLine();

        // Get the previous line after adding a new line because the address can
        // change if nsTArray needs to reallocate a new space for the new line.
        FlexLine& prevLine = aLines[aLines.Length() - 2];

        // Move the item from the end of prevLine to the end of curLine.
        curLine->Items().AppendElement(prevLine.Items().PopLastElement());
      }
    }

    // Update the line's bookkeeping about how large its items collectively are.
    curLine->AddLastItemToMainSizeTotals();
    itemIdxInContainer++;
  }
}

nsFlexContainerFrame::FlexLayoutResult
nsFlexContainerFrame::GenerateFlexLayoutResult() {
  MOZ_ASSERT(GetPrevInFlow(), "This should be called by non-first-in-flows!");

  auto* data = FirstInFlow()->GetProperty(SharedFlexData::Prop());
  MOZ_ASSERT(data, "SharedFlexData should be set by our first-in-flow!");

  FlexLayoutResult flr;

  // The order state of the children is consistent across entire continuation
  // chain due to calling nsContainerFrame::NormalizeChildLists() at the
  // beginning of Reflow(), so we can align our state bit with our
  // prev-in-flow's state. Setup here before calling OrderStateForIter() below.
  AddOrRemoveStateBits(NS_STATE_FLEX_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER,
                       GetPrevInFlow()->HasAnyStateBits(
                           NS_STATE_FLEX_NORMAL_FLOW_CHILDREN_IN_CSS_ORDER));

  // Construct flex items for this flex container fragment from existing flex
  // items in SharedFlexData.
  CSSOrderAwareFrameIterator iter(
      this, FrameChildListID::Principal,
      CSSOrderAwareFrameIterator::ChildFilter::SkipPlaceholders,
      OrderStateForIter(this), OrderingPropertyForIter(this));

  auto ConstructNewFlexLine = [&flr]() {
    // Use zero main gap size since it doesn't matter in flex container's
    // next-in-flows. We've computed flex items' positions in first-in-flow.
    return flr.mLines.EmplaceBack(0);
  };

  // We have at least one FlexLine. Even an empty flex container has a single
  // (empty) flex line.
  FlexLine* currentLine = ConstructNewFlexLine();

  if (!iter.AtEnd()) {
    nsIFrame* child = *iter;
    nsIFrame* childFirstInFlow = child->FirstInFlow();

    // We are iterating nested for-loops over the FlexLines and FlexItems
    // generated by GenerateFlexLines() and cached in flex container's
    // first-in-flow. For each flex item, check if its frame (must be a
    // first-in-flow) is the first-in-flow of the first child frame in this flex
    // container continuation. If so, clone the data from that FlexItem into a
    // FlexLine. When we find a match for the item, we know that the next child
    // frame might have its first-in-flow as the next item in the same original
    // line. In this case, we'll put the cloned data in the same line here as
    // well.
    for (const FlexLine& line : data->mLines) {
      // If currentLine is empty, either it is the first line, or all the items
      // in the previous line have been placed in our prev-in-flows. No need to
      // construct a new line.
      if (!currentLine->IsEmpty()) {
        currentLine = ConstructNewFlexLine();
      }
      for (const FlexItem& item : line.Items()) {
        if (item.Frame() == childFirstInFlow) {
          currentLine->Items().AppendElement(item.CloneFor(child));
          iter.Next();
          if (iter.AtEnd()) {
            // We've constructed flex items for all children. No need to check
            // rest of the items.
            child = childFirstInFlow = nullptr;
            break;
          }
          child = *iter;
          childFirstInFlow = child->FirstInFlow();
        }
      }
      if (iter.AtEnd()) {
        // We've constructed flex items for all children. No need to check
        // rest of the lines.
        break;
      }
    }
  }

  flr.mContentBoxMainSize = data->mContentBoxMainSize;
  flr.mContentBoxCrossSize = data->mContentBoxCrossSize;

  return flr;
}

// Returns the largest outer hypothetical main-size of any line in |aLines|.
// (i.e. the hypothetical main-size of the largest line)
static AuCoord64 GetLargestLineMainSize(nsTArray<FlexLine>& aLines) {
  AuCoord64 largestLineOuterSize = 0;
  for (const FlexLine& line : aLines) {
    largestLineOuterSize =
        std::max(largestLineOuterSize, line.TotalOuterHypotheticalMainSize());
  }
  return largestLineOuterSize;
}

nscoord nsFlexContainerFrame::ComputeMainSize(
    const ReflowInput& aReflowInput, const FlexboxAxisTracker& aAxisTracker,
    const nscoord aTentativeContentBoxMainSize,
    nsTArray<FlexLine>& aLines) const {
  if (aAxisTracker.IsRowOriented()) {
    // Row-oriented --> our main axis is the inline axis, so our main size
    // is our inline size (which should already be resolved).
    return aTentativeContentBoxMainSize;
  }

  const bool shouldApplyAutomaticMinimumOnBlockAxis =
      aReflowInput.ShouldApplyAutomaticMinimumOnBlockAxis();
  if (aTentativeContentBoxMainSize != NS_UNCONSTRAINEDSIZE &&
      !shouldApplyAutomaticMinimumOnBlockAxis) {
    // Column-oriented case, with fixed BSize:
    // Just use our fixed block-size because we always assume the available
    // block-size is unconstrained, and the reflow input has already done the
    // appropriate min/max-BSize clamping.
    return aTentativeContentBoxMainSize;
  }

  // Column-oriented case, with size-containment in block axis:
  // Behave as if we had no content and just use our MinBSize.
  if (Maybe<nscoord> containBSize =
          aReflowInput.mFrame->ContainIntrinsicBSize()) {
    return aReflowInput.ApplyMinMaxBSize(*containBSize);
  }

  const AuCoord64 largestLineMainSize = GetLargestLineMainSize(aLines);
  const nscoord contentBSize = aReflowInput.ApplyMinMaxBSize(
      nscoord(largestLineMainSize.ToMinMaxClamped()));

  // If the clamped largest FlexLine length is larger than the tentative main
  // size (which is resolved by aspect-ratio), we extend it to contain the
  // entire FlexLine.
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
  if (shouldApplyAutomaticMinimumOnBlockAxis) {
    // Column-oriented case, with auto BSize which is resolved by
    // aspect-ratio.
    return std::max(contentBSize, aTentativeContentBoxMainSize);
  }

  // Column-oriented case, with auto BSize:
  // Resolve auto BSize to the largest FlexLine length, clamped to our
  // computed min/max main-size properties.
  return contentBSize;
}

nscoord nsFlexContainerFrame::ComputeCrossSize(
    const ReflowInput& aReflowInput, const FlexboxAxisTracker& aAxisTracker,
    const nscoord aTentativeContentBoxCrossSize, nscoord aSumLineCrossSizes,
    bool* aIsDefinite) const {
  MOZ_ASSERT(aIsDefinite, "outparam pointer must be non-null");

  if (aAxisTracker.IsColumnOriented()) {
    // Column-oriented --> our cross axis is the inline axis, so our cross size
    // is our inline size (which should already be resolved).
    *aIsDefinite = true;
    // FIXME: Bug 1661847 - there are cases where aTentativeContentBoxCrossSize
    // (i.e. aReflowInput.ComputedISize()) might not be the right thing to
    // return here. Specifically: if our cross size is an intrinsic size, and we
    // have flex items that are flexible and have aspect ratios, then we may
    // need to take their post-flexing main sizes into account (multiplied
    // through their aspect ratios to get their cross sizes), in order to
    // determine their flex line's size & the flex container's cross size (e.g.
    // as `aSumLineCrossSizes`).
    return aTentativeContentBoxCrossSize;
  }

  const bool shouldApplyAutomaticMinimumOnBlockAxis =
      aReflowInput.ShouldApplyAutomaticMinimumOnBlockAxis();
  const nscoord computedBSize = aReflowInput.ComputedBSize();
  if (computedBSize != NS_UNCONSTRAINEDSIZE &&
      !shouldApplyAutomaticMinimumOnBlockAxis) {
    // Row-oriented case (cross axis is block-axis), with fixed BSize:
    *aIsDefinite = true;

    // Just use our fixed block-size because we always assume the available
    // block-size is unconstrained, and the reflow input has already done the
    // appropriate min/max-BSize clamping.
    return computedBSize;
  }

  // Row-oriented case, with size-containment in block axis:
  // Behave as if we had no content and just use our MinBSize.
  if (Maybe<nscoord> containBSize =
          aReflowInput.mFrame->ContainIntrinsicBSize()) {
    *aIsDefinite = true;
    return aReflowInput.ApplyMinMaxBSize(*containBSize);
  }

  // The cross size must not be definite in the following cases.
  *aIsDefinite = false;

  const nscoord contentBSize =
      aReflowInput.ApplyMinMaxBSize(aSumLineCrossSizes);
  // If the content block-size is larger than the effective computed
  // block-size, we extend the block-size to contain all the content.
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
  if (shouldApplyAutomaticMinimumOnBlockAxis) {
    // Row-oriented case (cross axis is block-axis), with auto BSize which is
    // resolved by aspect-ratio or content size.
    return std::max(contentBSize, computedBSize);
  }

  // Row-oriented case (cross axis is block axis), with auto BSize:
  // Shrink-wrap our line(s), subject to our min-size / max-size
  // constraints in that (block) axis.
  return contentBSize;
}

LogicalSize nsFlexContainerFrame::ComputeAvailableSizeForItems(
    const ReflowInput& aReflowInput,
    const mozilla::LogicalMargin& aBorderPadding) const {
  const WritingMode wm = GetWritingMode();
  nscoord availableBSize = aReflowInput.AvailableBSize();

  if (availableBSize != NS_UNCONSTRAINEDSIZE) {
    // Available block-size is constrained. Subtract block-start border and
    // padding from it.
    availableBSize -= aBorderPadding.BStart(wm);

    if (aReflowInput.mStyleBorder->mBoxDecorationBreak ==
        StyleBoxDecorationBreak::Clone) {
      // We have box-decoration-break:clone. Subtract block-end border and
      // padding from the available block-size as well.
      availableBSize -= aBorderPadding.BEnd(wm);
    }

    // Available block-size can became negative after subtracting block-axis
    // border and padding. Per spec, to guarantee progress, fragmentainers are
    // assumed to have a minimum block size of 1px regardless of their used
    // size. https://drafts.csswg.org/css-break/#breaking-rules
    availableBSize =
        std::max(nsPresContext::CSSPixelsToAppUnits(1), availableBSize);
  }

  return LogicalSize(wm, aReflowInput.ComputedISize(), availableBSize);
}

void FlexLine::PositionItemsInMainAxis(
    const StyleContentDistribution& aJustifyContent,
    nscoord aContentBoxMainSize, const FlexboxAxisTracker& aAxisTracker) {
  MainAxisPositionTracker mainAxisPosnTracker(
      aAxisTracker, this, aJustifyContent, aContentBoxMainSize);
  bool hadItemBefore = false;
  for (FlexItem& item : Items()) {
    const bool strut = item.IsStrut();
    if (hadItemBefore && !strut) {
      mainAxisPosnTracker.TraverseGap(mMainGapSize);
    }

    nscoord itemMainBorderBoxSize =
        item.MainSize() + item.BorderPaddingSizeInMainAxis();

    // Resolve any main-axis 'auto' margins on aChild to an actual value.
    mainAxisPosnTracker.ResolveAutoMarginsInMainAxis(item);

    // Advance our position tracker to child's upper-left content-box corner,
    // and use that as its position in the main axis.
    mainAxisPosnTracker.EnterMargin(item.Margin());
    mainAxisPosnTracker.EnterChildFrame(itemMainBorderBoxSize);

    item.SetMainPosition(mainAxisPosnTracker.Position());

    mainAxisPosnTracker.ExitChildFrame(itemMainBorderBoxSize);
    mainAxisPosnTracker.ExitMargin(item.Margin());
    mainAxisPosnTracker.TraversePackingSpace();
    hadItemBefore |= !strut;
  }
}

void nsFlexContainerFrame::SizeItemInCrossAxis(ReflowInput& aChildReflowInput,
                                               FlexItem& aItem) {
  // If cross axis is the item's inline axis, just use ISize from reflow input,
  // and don't bother with a full reflow.
  if (aItem.IsInlineAxisCrossAxis()) {
    aItem.SetCrossSize(aChildReflowInput.ComputedISize());
    return;
  }

  MOZ_ASSERT(!aItem.HadMeasuringReflow(),
             "We shouldn't need more than one measuring reflow");

  if (aItem.AlignSelf() == StyleAlignFlags::STRETCH) {
    // This item's got "align-self: stretch", so we probably imposed a
    // stretched computed cross-size on it during its previous
    // reflow. We're not imposing that BSize for *this* "measuring" reflow, so
    // we need to tell it to treat this reflow as a resize in its block axis
    // (regardless of whether any of its ancestors are actually being resized).
    // (Note: we know that the cross axis is the item's *block* axis -- if it
    // weren't, then we would've taken the early-return above.)
    aChildReflowInput.SetBResize(true);
    // Not 100% sure this is needed, but be conservative for now:
    aChildReflowInput.SetBResizeForPercentages(true);
  }

  // Potentially reflow the item, and get the sizing info.
  const CachedBAxisMeasurement& measurement =
      MeasureBSizeForFlexItem(aItem, aChildReflowInput);

  // Save the sizing info that we learned from this reflow
  // -----------------------------------------------------

  // Tentatively store the child's desired content-box cross-size.
  aItem.SetCrossSize(measurement.BSize());
}

void FlexLine::PositionItemsInCrossAxis(
    nscoord aLineStartPosition, const FlexboxAxisTracker& aAxisTracker) {
  SingleLineCrossAxisPositionTracker lineCrossAxisPosnTracker(aAxisTracker);

  for (FlexItem& item : Items()) {
    // First, stretch the item's cross size (if appropriate), and resolve any
    // auto margins in this axis.
    item.MaybeResolveStretchedCrossSize(mLineCrossSize);
    lineCrossAxisPosnTracker.ResolveAutoMarginsInCrossAxis(*this, item);

    // Compute the cross-axis position of this item
    nscoord itemCrossBorderBoxSize =
        item.CrossSize() + item.BorderPaddingSizeInCrossAxis();
    lineCrossAxisPosnTracker.EnterAlignPackingSpace(*this, item, aAxisTracker);
    lineCrossAxisPosnTracker.EnterMargin(item.Margin());
    lineCrossAxisPosnTracker.EnterChildFrame(itemCrossBorderBoxSize);

    item.SetCrossPosition(aLineStartPosition +
                          lineCrossAxisPosnTracker.Position());

    // Back out to cross-axis edge of the line.
    lineCrossAxisPosnTracker.ResetPosition();
  }
}

void nsFlexContainerFrame::Reflow(nsPresContext* aPresContext,
                                  ReflowOutput& aReflowOutput,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus) {
  NormalizeChildLists();

  if (IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
    return;
  }

  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsFlexContainerFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  MOZ_ASSERT(aPresContext == PresContext());
  NS_WARNING_ASSERTION(
      aReflowInput.ComputedISize() != NS_UNCONSTRAINEDSIZE,
      "Unconstrained inline size; this should only result from huge sizes "
      "(not intrinsic sizing w/ orthogonal flows)");

  FLEX_LOG("Reflow flex container frame %p", this);

  if (IsFrameTreeTooDeep(aReflowInput, aReflowOutput, aStatus)) {
    return;
  }

#ifdef DEBUG
  mDidPushItemsBitMayLie = false;
  SanityCheckChildListsBeforeReflow();
#endif  // DEBUG

  // We (and our children) can only depend on our ancestor's bsize if we have
  // a percent-bsize, or if we're positioned and we have "block-start" and
  // "block-end" set and have block-size:auto.  (There are actually other cases,
  // too -- e.g. if our parent is itself a block-dir flex container and we're
  // flexible -- but we'll let our ancestors handle those sorts of cases, by
  // by propagating the bsize dependency through
  // `MaybePropagateRelativeBSizeFlagFrom`)
  //
  // TODO(emilio): the !bsize.IsLengthPercentage() preserves behavior, but it's
  // too conservative. min/max-content don't really depend on the container.
  WritingMode wm = aReflowInput.GetWritingMode();
  const nsStylePosition* stylePos = StylePosition();
  const auto anchorResolutionParams =
      AnchorPosOffsetResolutionParams::UseCBFrameSize(
          AnchorPosResolutionParams::From(this));
  const auto bsize = stylePos->BSize(wm, anchorResolutionParams.mBaseParams);
  if (bsize->HasPercent() ||
      (StyleDisplay()->IsAbsolutelyPositionedStyle() &&
       (bsize->IsAuto() || !bsize->IsLengthPercentage()) &&
       !stylePos
            ->GetAnchorResolvedInset(LogicalSide::BStart, wm,
                                     anchorResolutionParams)
            ->IsAuto() &&
       !stylePos
            ->GetAnchorResolvedInset(LogicalSide::BEnd, wm,
                                     anchorResolutionParams)
            ->IsAuto())) {
    AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
  }

  const FlexboxAxisTracker axisTracker(this);

  // Check to see if we need to create a computed info structure, to
  // be filled out for use by devtools.
  ComputedFlexContainerInfo* containerInfo = CreateOrClearFlexContainerInfo();

  FlexLayoutResult flr;
  PerFragmentFlexData fragmentData;
  const nsIFrame* prevInFlow = GetPrevInFlow();
  if (!prevInFlow) {
    const LogicalSize tentativeContentBoxSize = aReflowInput.ComputedSize();
    const nscoord tentativeContentBoxMainSize =
        axisTracker.MainComponent(tentativeContentBoxSize);
    const nscoord tentativeContentBoxCrossSize =
        axisTracker.CrossComponent(tentativeContentBoxSize);

    // Calculate gap sizes for main and cross axis. We only need them in
    // DoFlexLayout in the first-in-flow, so no need to worry about consumed
    // block-size.
    const auto& mainGapStyle =
        axisTracker.IsRowOriented() ? stylePos->mColumnGap : stylePos->mRowGap;
    const auto& crossGapStyle =
        axisTracker.IsRowOriented() ? stylePos->mRowGap : stylePos->mColumnGap;
    const nscoord mainGapSize = nsLayoutUtils::ResolveGapToLength(
        mainGapStyle, tentativeContentBoxMainSize);
    const nscoord crossGapSize = nsLayoutUtils::ResolveGapToLength(
        crossGapStyle, tentativeContentBoxCrossSize);

    // When fragmenting a flex container, we run the flex algorithm without
    // regards to pagination in order to compute the flex container's desired
    // content-box size. https://drafts.csswg.org/css-flexbox-1/#pagination-algo
    //
    // Note: For a multi-line column-oriented flex container, the sample
    // algorithm suggests we wrap the flex line at the block-end edge of a
    // column/page, but we do not implement it intentionally. This brings the
    // layout result closer to the one as if there's no fragmentation.
    AutoTArray<StrutInfo, 1> struts;
    flr = DoFlexLayout(aReflowInput, tentativeContentBoxMainSize,
                       tentativeContentBoxCrossSize, axisTracker, mainGapSize,
                       crossGapSize, struts, containerInfo);

    if (!struts.IsEmpty()) {
      // We're restarting flex layout, with new knowledge of collapsed items.
      flr.mLines.Clear();
      flr.mPlaceholders.Clear();
      flr = DoFlexLayout(aReflowInput, tentativeContentBoxMainSize,
                         tentativeContentBoxCrossSize, axisTracker, mainGapSize,
                         crossGapSize, struts, containerInfo);
    }
  } else {
    flr = GenerateFlexLayoutResult();
    auto* fragmentDataProp =
        prevInFlow->GetProperty(PerFragmentFlexData::Prop());
    MOZ_ASSERT(fragmentDataProp,
               "PerFragmentFlexData should be set in our prev-in-flow!");
    fragmentData = *fragmentDataProp;
  }

  LogicalSize contentBoxSize = axisTracker.LogicalSizeFromFlexRelativeSizes(
      flr.mContentBoxMainSize, flr.mContentBoxCrossSize);

  const nscoord consumedBSize = CalcAndCacheConsumedBSize();
  const nscoord effectiveContentBSize =
      contentBoxSize.BSize(wm) - consumedBSize;
  LogicalMargin borderPadding = aReflowInput.ComputedLogicalBorderPadding(wm);
  if (MOZ_UNLIKELY(aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE)) {
    // We assume we are the last fragment by using
    // PreReflowBlockLevelLogicalSkipSides(), and skip block-end border and
    // padding if needed.
    borderPadding.ApplySkipSides(PreReflowBlockLevelLogicalSkipSides());
  }

  // Determine this frame's tentative border-box size. This is used for logical
  // to physical coordinate conversion when positioning children.
  //
  // Note that vertical-rl writing-mode is the only case where the block flow
  // direction progresses in a negative physical direction, and therefore block
  // direction coordinate conversion depends on knowing the width of the
  // coordinate space in order to translate between the logical and physical
  // origins. As a result, if our final border-box block-size is different from
  // this tentative one, and we are in vertical-rl writing mode, we need to
  // adjust our children's position after reflowing them.
  const LogicalSize tentativeBorderBoxSize(
      wm, contentBoxSize.ISize(wm) + borderPadding.IStartEnd(wm),
      std::min(effectiveContentBSize + borderPadding.BStartEnd(wm),
               aReflowInput.AvailableBSize()));
  const nsSize containerSize = tentativeBorderBoxSize.GetPhysicalSize(wm);

  OverflowAreas ocBounds;
  nsReflowStatus ocStatus;
  if (prevInFlow) {
    ReflowOverflowContainerChildren(
        aPresContext, aReflowInput, ocBounds, ReflowChildFlags::Default,
        ocStatus, MergeSortedFrameListsFor, Some(containerSize));
  }

  const LogicalSize availableSizeForItems =
      ComputeAvailableSizeForItems(aReflowInput, borderPadding);
  const auto [childrenBEndEdge, childrenStatus] =
      ReflowChildren(aReflowInput, containerSize, availableSizeForItems,
                     borderPadding, axisTracker, flr, fragmentData);

  bool mayNeedNextInFlow = false;
  if (aReflowInput.IsInFragmentedContext()) {
    // This fragment's contribution to the flex container's cumulative
    // content-box block-size, if it turns out that this is the final vs.
    // non-final fragment:
    //
    // * If it turns out we *are* the final fragment, then this fragment's
    // content-box contribution is the distance from the start of our content
    // box to the block-end edge of our children (note the borderPadding
    // subtraction is just to get us to a content-box-relative offset here):
    const nscoord bSizeContributionIfFinalFragment =
        childrenBEndEdge - borderPadding.BStart(wm);

    // * If it turns out we're *not* the final fragment, then this fragment's
    // content-box extends to the edge of the availableSizeForItems (at least),
    // regardless of whether we actually have items at that location:
    const nscoord bSizeContributionIfNotFinalFragment = std::max(
        bSizeContributionIfFinalFragment, availableSizeForItems.BSize(wm));

    // mCumulativeBEndEdgeShift was updated in ReflowChildren(), and our
    // children's block-size may grow in fragmented context. If our block-size
    // and max-block-size are unconstrained, then we allow the flex container to
    // grow to accommodate any children whose sizes grew as a result of
    // fragmentation.
    if (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE) {
      contentBoxSize.BSize(wm) = aReflowInput.ApplyMinMaxBSize(
          contentBoxSize.BSize(wm) + fragmentData.mCumulativeBEndEdgeShift);

      if (childrenStatus.IsComplete()) {
        // All of the children fit! We know that we're using a content-based
        // block-size, and we know our children's block-size may have grown due
        // to fragmentation. So we allow ourselves to grow our block-size here
        // to contain the block-end edge of our last child (subject to our
        // min/max constraints).
        contentBoxSize.BSize(wm) = aReflowInput.ApplyMinMaxBSize(std::max(
            contentBoxSize.BSize(wm), fragmentData.mCumulativeContentBoxBSize +
                                          bSizeContributionIfFinalFragment));
      } else {
        // As in the if-branch above, we extend our block-size, but in this case
        // we know that a child didn't fit and might overshot our available
        // size, so we assume this fragment won't be the final fragment, and
        // hence it should contribute bSizeContributionIfNotFinalFragment
        // (subject to our min/max constraints).
        contentBoxSize.BSize(wm) = aReflowInput.ApplyMinMaxBSize(std::max(
            contentBoxSize.BSize(wm), fragmentData.mCumulativeContentBoxBSize +
                                          bSizeContributionIfNotFinalFragment));

        if (aReflowInput.ComputedMaxBSize() == NS_UNCONSTRAINEDSIZE) {
          mayNeedNextInFlow = true;
        } else {
          // The definite max-block-size can be the upper bound of our
          // content-box block-size. We should check whether we need a
          // next-in-flow.
          mayNeedNextInFlow = contentBoxSize.BSize(wm) - consumedBSize >
                              availableSizeForItems.BSize(wm);
        }
      }
    } else {
      mayNeedNextInFlow = contentBoxSize.BSize(wm) - consumedBSize >
                          availableSizeForItems.BSize(wm);
    }
    fragmentData.mCumulativeContentBoxBSize +=
        bSizeContributionIfNotFinalFragment;

    // If we may need a next-in-flow, we'll need to skip block-end border and
    // padding.
    if (mayNeedNextInFlow && aReflowInput.mStyleBorder->mBoxDecorationBreak ==
                                 StyleBoxDecorationBreak::Slice) {
      borderPadding.BEnd(wm) = 0;
    }
  }

  PopulateReflowOutput(aReflowOutput, aReflowInput, aStatus, contentBoxSize,
                       borderPadding, consumedBSize, mayNeedNextInFlow,
                       childrenBEndEdge, childrenStatus, axisTracker, flr);

  if (wm.IsVerticalRL()) {
    // If the final border-box block-size is different from the tentative one,
    // adjust our children's position.
    const nscoord deltaBCoord =
        tentativeBorderBoxSize.BSize(wm) - aReflowOutput.Size(wm).BSize(wm);
    if (deltaBCoord != 0) {
      const LogicalPoint delta(wm, 0, deltaBCoord);
      for (const FlexLine& line : flr.mLines) {
        for (const FlexItem& item : line.Items()) {
          item.Frame()->MovePositionBy(wm, delta);
        }
      }
    }
  }

  // Overflow area = union(my overflow area, children's overflow areas)
  aReflowOutput.SetOverflowAreasToDesiredBounds();
  UnionInFlowChildOverflow(aReflowOutput.mOverflowAreas);

  // Merge overflow container bounds and status.
  aReflowOutput.mOverflowAreas.UnionWith(ocBounds);
  aStatus.MergeCompletionStatusFrom(ocStatus);

  FinishReflowWithAbsoluteFrames(PresContext(), aReflowOutput, aReflowInput,
                                 aStatus);

  // Finally update our line and item measurements in our containerInfo.
  if (MOZ_UNLIKELY(containerInfo)) {
    UpdateFlexLineAndItemInfo(*containerInfo, flr.mLines);
  }

  // If we are the first-in-flow, we want to store data for our next-in-flows,
  // or clear the existing data if it is not needed.
  if (!prevInFlow) {
    SharedFlexData* sharedData = GetProperty(SharedFlexData::Prop());
    if (!aStatus.IsFullyComplete()) {
      if (!sharedData) {
        sharedData = new SharedFlexData;
        SetProperty(SharedFlexData::Prop(), sharedData);
      }
      sharedData->Update(std::move(flr));
    } else if (sharedData && !GetNextInFlow()) {
      // We are fully-complete, so no next-in-flow is needed. However, if we
      // report SetInlineLineBreakBeforeAndReset() in an incremental reflow, our
      // next-in-flow might still exist. It can be reflowed again before us if
      // it is an overflow container. Delete the existing data only if we don't
      // have a next-in-flow.
      RemoveProperty(SharedFlexData::Prop());
    }
  }

  PerFragmentFlexData* fragmentDataProp =
      GetProperty(PerFragmentFlexData::Prop());
  if (!aStatus.IsFullyComplete()) {
    if (!fragmentDataProp) {
      fragmentDataProp = new PerFragmentFlexData;
      SetProperty(PerFragmentFlexData::Prop(), fragmentDataProp);
    }
    *fragmentDataProp = fragmentData;
  } else if (fragmentDataProp && !GetNextInFlow()) {
    // Similar to the condition to remove SharedFlexData, delete the
    // existing data only if we don't have a next-in-flow.
    RemoveProperty(PerFragmentFlexData::Prop());
  }
}

Maybe<nscoord> nsFlexContainerFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext) const {
  if (StyleDisplay()->IsContainLayout() ||
      HasAnyStateBits(NS_STATE_FLEX_SYNTHESIZE_BASELINE)) {
    return Nothing{};
  }
  return Some(aBaselineGroup == BaselineSharingGroup::First ? mFirstBaseline
                                                            : mLastBaseline);
}

void nsFlexContainerFrame::UnionInFlowChildOverflow(
    OverflowAreas& aOverflowAreas, bool aAsIfScrolled) {
  // The CSS Overflow spec [1] requires that a scrollable container's
  // scrollable overflow should include the following areas.
  //
  // a) "the box's own content and padding areas": we treat the *content* as
  // the scrolled inner frame's theoretical content-box that's intrinsically
  // sized to the union of all the flex items' margin boxes, _without_
  // relative positioning applied. The *padding areas* is just inflation on
  // top of the theoretical content-box by the flex container's padding.
  //
  // b) "the margin areas of grid item and flex item boxes for which the box
  // establishes a containing block": a) already includes the flex items'
  // normal-positioned margin boxes into the scrollable overflow, but their
  // relative-positioned margin boxes should also be included because relpos
  // children are still flex items.
  //
  // [1] https://drafts.csswg.org/css-overflow-3/#scrollable.
  const bool isScrolledContent =
      aAsIfScrolled ||
      Style()->GetPseudoType() == PseudoStyleType::MozScrolledContent;
  bool anyScrolledContentItem = false;
  // Union of normal-positioned margin boxes for all the items.
  nsRect itemMarginBoxes;
  // Overflow areas containing the union of relative-positioned and
  // stick-positioned margin boxes of relpos items.
  //
  // Note for sticky-positioned margin boxes, we only union it with the ink
  // overflow to avoid circular dependencies with the scroll container. (The
  // scroll position and the scroll container's size impact the sticky position,
  // so we don't want the sticky position to impact them.)
  OverflowAreas relPosItemMarginBoxes;
  const bool useMozBoxCollapseBehavior =
      StyleVisibility()->UseLegacyCollapseBehavior();
  for (nsIFrame* f : mFrames) {
    if (useMozBoxCollapseBehavior && f->StyleVisibility()->IsCollapse()) {
      continue;
    }
    ConsiderChildOverflow(aOverflowAreas, f,
                          aAsIfScrolled ? OverflowAreaUnionFlags::AsIfScrolled
                                        : OverflowAreaUnionFlags::None);
    if (!isScrolledContent) {
      continue;
    }
    if (f->IsPlaceholderFrame()) {
      continue;
    }
    anyScrolledContentItem = true;
    if (MOZ_UNLIKELY(f->IsRelativelyOrStickyPositioned())) {
      const nsRect marginRect = f->GetMarginRectRelativeToSelf();
      itemMarginBoxes =
          itemMarginBoxes.Union(marginRect + f->GetNormalPosition());
      if (f->IsRelativelyPositioned()) {
        relPosItemMarginBoxes.UnionAllWith(marginRect + f->GetPosition());
      } else {
        MOZ_ASSERT(f->IsStickyPositioned());
        relPosItemMarginBoxes.UnionWith(
            OverflowAreas(marginRect + f->GetPosition(), nsRect()));
      }
    } else {
      itemMarginBoxes = itemMarginBoxes.Union(f->GetMarginRect());
    }
  }

  if (anyScrolledContentItem) {
    itemMarginBoxes.Inflate(GetUsedPadding());
    aOverflowAreas.UnionAllWith(itemMarginBoxes);
    aOverflowAreas.UnionWith(relPosItemMarginBoxes);
  }
}

void nsFlexContainerFrame::UnionChildOverflow(OverflowAreas& aOverflowAreas,
                                              bool aAsIfScrolled) {
  UnionInFlowChildOverflow(aOverflowAreas, aAsIfScrolled);
  // Union with child frames, skipping the principal list since we already
  // handled those above.
  nsLayoutUtils::UnionChildOverflow(this, aOverflowAreas,
                                    {FrameChildListID::Principal});
}

void nsFlexContainerFrame::CalculatePackingSpace(
    uint32_t aNumThingsToPack, const StyleContentDistribution& aAlignVal,
    nscoord* aFirstSubjectOffset, uint32_t* aNumPackingSpacesRemaining,
    nscoord* aPackingSpaceRemaining) {
  StyleAlignFlags val = aAlignVal.primary;
  MOZ_ASSERT(val == StyleAlignFlags::SPACE_BETWEEN ||
                 val == StyleAlignFlags::SPACE_AROUND ||
                 val == StyleAlignFlags::SPACE_EVENLY,
             "Unexpected alignment value");

  MOZ_ASSERT(*aPackingSpaceRemaining >= 0,
             "Should not be called with negative packing space");

  // Note: In the aNumThingsToPack==1 case, the fallback behavior for
  // 'space-between' depends on precise information about the axes that we
  // don't have here. So, for that case, we just depend on the caller to
  // explicitly convert 'space-{between,around,evenly}' keywords to the
  // appropriate fallback alignment and skip this function.
  MOZ_ASSERT(aNumThingsToPack > 1,
             "Should not be called unless there's more than 1 thing to pack");

  // Packing spaces between items:
  *aNumPackingSpacesRemaining = aNumThingsToPack - 1;

  if (val == StyleAlignFlags::SPACE_BETWEEN) {
    // No need to reserve space at beginning/end, so we're done.
    return;
  }

  // We need to add 1 or 2 packing spaces, split between beginning/end, for
  // space-around / space-evenly:
  size_t numPackingSpacesForEdges =
      val == StyleAlignFlags::SPACE_AROUND ? 1 : 2;

  // How big will each "full" packing space be:
  nscoord packingSpaceSize =
      *aPackingSpaceRemaining /
      (*aNumPackingSpacesRemaining + numPackingSpacesForEdges);
  // How much packing-space are we allocating to the edges:
  nscoord totalEdgePackingSpace = numPackingSpacesForEdges * packingSpaceSize;

  // Use half of that edge packing space right now:
  *aFirstSubjectOffset += totalEdgePackingSpace / 2;
  // ...but we need to subtract all of it right away, so that we won't
  // hand out any of it to intermediate packing spaces.
  *aPackingSpaceRemaining -= totalEdgePackingSpace;
}

ComputedFlexContainerInfo*
nsFlexContainerFrame::CreateOrClearFlexContainerInfo() {
  if (!HasAnyStateBits(NS_STATE_FLEX_COMPUTED_INFO)) {
    return nullptr;
  }

  // The flag that sets ShouldGenerateComputedInfo() will never be cleared.
  // That's acceptable because it's only set in a Chrome API invoked by
  // devtools, and won't impact normal browsing.

  // Re-use the ComputedFlexContainerInfo, if it exists.
  ComputedFlexContainerInfo* info = GetProperty(FlexContainerInfo());
  if (info) {
    // We can reuse, as long as we clear out old data.
    info->mLines.Clear();
  } else {
    info = new ComputedFlexContainerInfo();
    SetProperty(FlexContainerInfo(), info);
  }

  return info;
}

nscoord nsFlexContainerFrame::FlexItemConsumedBSize(const FlexItem& aItem) {
  nsSplittableFrame* f = do_QueryFrame(aItem.Frame());
  return f ? ConsumedBSize(f) : 0;
}

void nsFlexContainerFrame::CreateFlexLineAndFlexItemInfo(
    ComputedFlexContainerInfo& aContainerInfo,
    const nsTArray<FlexLine>& aLines) {
  for (const FlexLine& line : aLines) {
    ComputedFlexLineInfo* lineInfo = aContainerInfo.mLines.AppendElement();
    // Most of the remaining lineInfo properties will be filled out in
    // UpdateFlexLineAndItemInfo (some will be provided by other functions),
    // when we have real values. But we still add all the items here, so
    // we can capture computed data for each item as we proceed.
    for (const FlexItem& item : line.Items()) {
      nsIFrame* frame = item.Frame();

      // The frame may be for an element, or it may be for an
      // anonymous flex item, e.g. wrapping one or more text nodes.
      // DevTools wants the content node for the actual child in
      // the DOM tree, so we descend through anonymous boxes.
      nsIContent* content = nullptr;
      nsIFrame* targetFrame = GetFirstNonAnonBoxInSubtree(frame);
      if (targetFrame) {
        content = targetFrame->GetContent();
      }

      // Skip over content that is only whitespace, which might
      // have been broken off from a text node which is our real
      // target.
      while (content && content->TextIsOnlyWhitespace()) {
        // If content is only whitespace, try the frame sibling.
        targetFrame = targetFrame->GetNextSibling();
        if (targetFrame) {
          content = targetFrame->GetContent();
        } else {
          content = nullptr;
        }
      }

      ComputedFlexItemInfo* itemInfo = lineInfo->mItems.AppendElement();

      itemInfo->mNode = content;

      // itemInfo->mMainBaseSize and mMainDeltaSize will be filled out
      // in ResolveFlexibleLengths(). Other measurements will be captured in
      // UpdateFlexLineAndItemInfo.
    }
  }
}

void nsFlexContainerFrame::ComputeFlexDirections(
    ComputedFlexContainerInfo& aContainerInfo,
    const FlexboxAxisTracker& aAxisTracker) {
  auto ConvertPhysicalStartSideToFlexPhysicalDirection =
      [](mozilla::Side aStartSide) {
        switch (aStartSide) {
          case eSideLeft:
            return dom::FlexPhysicalDirection::Horizontal_lr;
          case eSideRight:
            return dom::FlexPhysicalDirection::Horizontal_rl;
          case eSideTop:
            return dom::FlexPhysicalDirection::Vertical_tb;
          case eSideBottom:
            return dom::FlexPhysicalDirection::Vertical_bt;
        }

        MOZ_ASSERT_UNREACHABLE("We should handle all sides!");
        return dom::FlexPhysicalDirection::Horizontal_lr;
      };

  aContainerInfo.mMainAxisDirection =
      ConvertPhysicalStartSideToFlexPhysicalDirection(
          aAxisTracker.MainAxisPhysicalStartSide());
  aContainerInfo.mCrossAxisDirection =
      ConvertPhysicalStartSideToFlexPhysicalDirection(
          aAxisTracker.CrossAxisPhysicalStartSide());
}

void nsFlexContainerFrame::UpdateFlexLineAndItemInfo(
    ComputedFlexContainerInfo& aContainerInfo,
    const nsTArray<FlexLine>& aLines) {
  uint32_t lineIndex = 0;
  for (const FlexLine& line : aLines) {
    ComputedFlexLineInfo& lineInfo = aContainerInfo.mLines[lineIndex];

    lineInfo.mCrossSize = line.LineCrossSize();
    lineInfo.mFirstBaselineOffset = line.FirstBaselineOffset();
    lineInfo.mLastBaselineOffset = line.LastBaselineOffset();

    uint32_t itemIndex = 0;
    for (const FlexItem& item : line.Items()) {
      ComputedFlexItemInfo& itemInfo = lineInfo.mItems[itemIndex];
      itemInfo.mFrameRect = item.Frame()->GetRect();
      itemInfo.mMainMinSize = item.MainMinSize();
      itemInfo.mMainMaxSize = item.MainMaxSize();
      itemInfo.mCrossMinSize = item.CrossMinSize();
      itemInfo.mCrossMaxSize = item.CrossMaxSize();
      itemInfo.mClampState =
          item.WasMinClamped()
              ? mozilla::dom::FlexItemClampState::Clamped_to_min
              : (item.WasMaxClamped()
                     ? mozilla::dom::FlexItemClampState::Clamped_to_max
                     : mozilla::dom::FlexItemClampState::Unclamped);
      ++itemIndex;
    }
    ++lineIndex;
  }
}

nsFlexContainerFrame* nsFlexContainerFrame::GetFlexFrameWithComputedInfo(
    nsIFrame* aFrame) {
  // Prepare a lambda function that we may need to call multiple times.
  auto GetFlexContainerFrame = [](nsIFrame* aFrame) -> nsFlexContainerFrame* {
    // Return the aFrame's content insertion frame, iff it is a flex container.
    if (!aFrame) {
      return nullptr;
    }
    return do_QueryFrame(aFrame->GetContentInsertionFrame());
  };

  nsFlexContainerFrame* flexFrame = GetFlexContainerFrame(aFrame);
  if (!flexFrame) {
    return nullptr;
  }
  // Generate the FlexContainerInfo data, if it's not already there.
  if (flexFrame->HasProperty(FlexContainerInfo())) {
    return flexFrame;
  }
  // Trigger a reflow that generates additional flex property data.
  // Hold onto aFrame while we do this, in case reflow destroys it.
  AutoWeakFrame weakFrameRef(aFrame);

  RefPtr<mozilla::PresShell> presShell = flexFrame->PresShell();
  flexFrame->AddStateBits(NS_STATE_FLEX_COMPUTED_INFO);
  presShell->FrameNeedsReflow(flexFrame, IntrinsicDirty::None,
                              NS_FRAME_IS_DIRTY);
  presShell->FlushPendingNotifications(FlushType::Layout);

  // Since the reflow may have side effects, get the flex frame
  // again. But if the weakFrameRef is no longer valid, then we
  // must bail out.
  if (!weakFrameRef.IsAlive()) {
    return nullptr;
  }

  flexFrame = GetFlexContainerFrame(weakFrameRef.GetFrame());

  NS_WARNING_ASSERTION(
      !flexFrame || flexFrame->HasProperty(FlexContainerInfo()),
      "The state bit should've made our forced-reflow "
      "generate a FlexContainerInfo object");
  return flexFrame;
}

/* static */
bool nsFlexContainerFrame::IsItemInlineAxisMainAxis(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame && aFrame->IsFlexItem(), "expecting arg to be a flex item");
  const WritingMode flexItemWM = aFrame->GetWritingMode();
  const nsIFrame* flexContainer = aFrame->GetParent();

  if (flexContainer->IsLegacyWebkitBox()) {
    // For legacy boxes, the main axis is determined by "box-orient", and we can
    // just directly check if that's vertical, and compare that to whether the
    // item's WM is also vertical:
    bool boxOrientIsVertical =
        flexContainer->StyleXUL()->mBoxOrient == StyleBoxOrient::Vertical;
    return flexItemWM.IsVertical() == boxOrientIsVertical;
  }

  // For modern CSS flexbox, we get our return value by asking two questions
  // and comparing their answers.
  // Question 1: does aFrame have the same inline axis as its flex container?
  bool itemInlineAxisIsParallelToParent =
      !flexItemWM.IsOrthogonalTo(flexContainer->GetWritingMode());

  // Question 2: is aFrame's flex container row-oriented? (This tells us
  // whether the flex container's main axis is its inline axis.)
  auto flexDirection = flexContainer->StylePosition()->mFlexDirection;
  bool flexContainerIsRowOriented =
      flexDirection == StyleFlexDirection::Row ||
      flexDirection == StyleFlexDirection::RowReverse;

  // aFrame's inline axis is its flex container's main axis IFF the above
  // questions have the same answer.
  return flexContainerIsRowOriented == itemInlineAxisIsParallelToParent;
}

/* static */
bool nsFlexContainerFrame::IsUsedFlexBasisContent(
    const StyleFlexBasis& aFlexBasis, const StyleSize& aMainSize) {
  // We have a used flex-basis of 'content' if flex-basis explicitly has that
  // value, OR if flex-basis is 'auto' (deferring to the main-size property)
  // and the main-size property is also 'auto'.
  // See https://drafts.csswg.org/css-flexbox-1/#valdef-flex-basis-auto
  if (aFlexBasis.IsContent()) {
    return true;
  }
  return aFlexBasis.IsAuto() && aMainSize.IsAuto();
}

nsFlexContainerFrame::FlexLayoutResult nsFlexContainerFrame::DoFlexLayout(
    const ReflowInput& aReflowInput, const nscoord aTentativeContentBoxMainSize,
    const nscoord aTentativeContentBoxCrossSize,
    const FlexboxAxisTracker& aAxisTracker, nscoord aMainGapSize,
    nscoord aCrossGapSize, nsTArray<StrutInfo>& aStruts,
    ComputedFlexContainerInfo* const aContainerInfo) {
  FlexLayoutResult flr;

  GenerateFlexLines(aReflowInput, aTentativeContentBoxMainSize,
                    aTentativeContentBoxCrossSize, aStruts, aAxisTracker,
                    aMainGapSize, flr.mPlaceholders, flr.mLines,
                    flr.mHasCollapsedItems);

  if ((flr.mLines.Length() == 1 && flr.mLines[0].IsEmpty()) ||
      aReflowInput.mStyleDisplay->IsContainLayout()) {
    // We have no flex items, or we're layout-contained. So, we have no
    // baseline, and our parent should synthesize a baseline if needed.
    AddStateBits(NS_STATE_FLEX_SYNTHESIZE_BASELINE);
  } else {
    RemoveStateBits(NS_STATE_FLEX_SYNTHESIZE_BASELINE);
  }

  // Construct our computed info if we've been asked to do so. This is
  // necessary to do now so we can capture some computed values for
  // FlexItems during layout that would not otherwise be saved (like
  // size adjustments). We'll later fix up the line properties,
  // because the correct values aren't available yet.
  if (aContainerInfo) {
    MOZ_ASSERT(HasAnyStateBits(NS_STATE_FLEX_COMPUTED_INFO),
               "We should only have the info struct if we should generate it");

    if (!aStruts.IsEmpty()) {
      // We restarted DoFlexLayout, and may have stale mLines to clear:
      aContainerInfo->mLines.Clear();
    } else {
      MOZ_ASSERT(aContainerInfo->mLines.IsEmpty(), "Shouldn't have lines yet.");
    }

    CreateFlexLineAndFlexItemInfo(*aContainerInfo, flr.mLines);
    ComputeFlexDirections(*aContainerInfo, aAxisTracker);
  }

  flr.mContentBoxMainSize = ComputeMainSize(
      aReflowInput, aAxisTracker, aTentativeContentBoxMainSize, flr.mLines);

  uint32_t lineIndex = 0;
  for (FlexLine& line : flr.mLines) {
    ComputedFlexLineInfo* lineInfo =
        aContainerInfo ? &aContainerInfo->mLines[lineIndex] : nullptr;
    line.ResolveFlexibleLengths(flr.mContentBoxMainSize, lineInfo);
    ++lineIndex;
  }

  // Cross Size Determination - Flexbox spec section 9.4
  // https://drafts.csswg.org/css-flexbox-1/#cross-sizing
  // ===================================================
  // Calculate the hypothetical cross size of each item:

  // 'sumLineCrossSizes' includes the size of all gaps between lines. We
  // initialize it with the sum of all the gaps, and add each line's cross size
  // at the end of the following for-loop.
  nscoord sumLineCrossSizes = aCrossGapSize * (flr.mLines.Length() - 1);
  for (FlexLine& line : flr.mLines) {
    for (FlexItem& item : line.Items()) {
      // The item may already have the correct cross-size; only recalculate
      // if the item's main size resolution (flexing) could have influenced it:
      if (item.CanMainSizeInfluenceCrossSize()) {
        StyleSizeOverrides sizeOverrides;
        if (item.IsInlineAxisMainAxis()) {
          sizeOverrides.mStyleISize.emplace(item.StyleMainSize());
        } else {
          sizeOverrides.mStyleBSize.emplace(item.StyleMainSize());
        }
        FLEX_ITEM_LOG(item.Frame(), "Sizing item in cross axis");
        FLEX_LOGV("Main size override: %d", item.MainSize());

        const WritingMode wm = item.GetWritingMode();
        LogicalSize availSize = aReflowInput.ComputedSize(wm);
        availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
        ReflowInput childReflowInput(PresContext(), aReflowInput, item.Frame(),
                                     availSize, Nothing(), {}, sizeOverrides,
                                     {ComputeSizeFlag::ShrinkWrap});
        if (item.IsBlockAxisMainAxis() && item.TreatBSizeAsIndefinite()) {
          childReflowInput.mFlags.mTreatBSizeAsIndefinite = true;
        }

        SizeItemInCrossAxis(childReflowInput, item);
      }
    }
    // Now that we've finished with this line's items, size the line itself:
    line.ComputeCrossSizeAndBaseline(aAxisTracker);
    sumLineCrossSizes += line.LineCrossSize();
  }

  bool isCrossSizeDefinite;
  flr.mContentBoxCrossSize = ComputeCrossSize(
      aReflowInput, aAxisTracker, aTentativeContentBoxCrossSize,
      sumLineCrossSizes, &isCrossSizeDefinite);

  // Set up state for cross-axis alignment, at a high level (outside the
  // scope of a particular flex line)
  CrossAxisPositionTracker crossAxisPosnTracker(
      flr.mLines, aReflowInput, flr.mContentBoxCrossSize, isCrossSizeDefinite,
      aAxisTracker, aCrossGapSize);

  // Now that we know the cross size of each line (including
  // "align-content:stretch" adjustments, from the CrossAxisPositionTracker
  // constructor), we can create struts for any flex items with
  // "visibility: collapse" (and restart flex layout).
  // Make sure to only do this if we had no struts.
  if (aStruts.IsEmpty() && flr.mHasCollapsedItems &&
      !StyleVisibility()->UseLegacyCollapseBehavior()) {
    BuildStrutInfoFromCollapsedItems(flr.mLines, aStruts);
    if (!aStruts.IsEmpty()) {
      // Restart flex layout, using our struts.
      return flr;
    }
  }

  // If the flex container is row-oriented, it should derive its first/last
  // baseline from the WM-relative startmost/endmost FlexLine if any items in
  // the line participate in baseline alignment.
  // https://drafts.csswg.org/css-flexbox-1/#flex-baselines
  //
  // Initialize the relevant variables here so that we can establish baselines
  // while iterating FlexLine later (while crossAxisPosnTracker is conveniently
  // pointing at the cross-start edge of that line, which the line's baseline
  // offset is measured from).
  const FlexLine* lineForFirstBaseline = nullptr;
  const FlexLine* lineForLastBaseline = nullptr;
  if (aAxisTracker.IsRowOriented()) {
    lineForFirstBaseline = &StartmostLine(flr.mLines, aAxisTracker);
    lineForLastBaseline = &EndmostLine(flr.mLines, aAxisTracker);
  } else {
    // For column-oriented flex container, use sentinel value to prompt us to
    // get baselines from the startmost/endmost items.
    flr.mAscent = nscoord_MIN;
    flr.mAscentForLast = nscoord_MIN;
  }

  const auto justifyContent =
      aReflowInput.mFrame->IsLegacyWebkitBox()
          ? ConvertLegacyStyleToJustifyContent(StyleXUL())
          : aReflowInput.mStylePosition->mJustifyContent;

  lineIndex = 0;
  for (FlexLine& line : flr.mLines) {
    // Main-Axis Alignment - Flexbox spec section 9.5
    // https://drafts.csswg.org/css-flexbox-1/#main-alignment
    // ==============================================
    line.PositionItemsInMainAxis(justifyContent, flr.mContentBoxMainSize,
                                 aAxisTracker);

    // See if we need to extract some computed info for this line.
    if (MOZ_UNLIKELY(aContainerInfo)) {
      ComputedFlexLineInfo& lineInfo = aContainerInfo->mLines[lineIndex];
      lineInfo.mCrossStart = crossAxisPosnTracker.Position();
    }

    // Cross-Axis Alignment - Flexbox spec section 9.6
    // https://drafts.csswg.org/css-flexbox-1/#cross-alignment
    // ===============================================
    line.PositionItemsInCrossAxis(crossAxisPosnTracker.Position(),
                                  aAxisTracker);

    // Flex Container Baselines - Flexbox spec section 8.5
    // https://drafts.csswg.org/css-flexbox-1/#flex-baselines
    auto ComputeAscentFromLine = [&](const FlexLine& aLine,
                                     BaselineSharingGroup aBaselineGroup) {
      MOZ_ASSERT(aAxisTracker.IsRowOriented(),
                 "This makes sense only if we are row-oriented!");

      // baselineOffsetInLine is a distance from the line's cross-start edge.
      const nscoord baselineOffsetInLine =
          aLine.ExtractBaselineOffset(aBaselineGroup);

      if (baselineOffsetInLine == nscoord_MIN) {
        // No "first baseline"-aligned or "last baseline"-aligned items in
        // aLine. Return a sentinel value to prompt us to get baseline from the
        // startmost or endmost FlexItem after we've reflowed it.
        return nscoord_MIN;
      }

      // This "ascent" variable is a distance from the flex container's
      // content-box block-start edge.
      const nscoord ascent = aAxisTracker.LogicalAscentFromFlexRelativeAscent(
          crossAxisPosnTracker.Position() + baselineOffsetInLine,
          flr.mContentBoxCrossSize);

      // Convert "ascent" variable to a distance from border-box start or end
      // edge, per documentation for FlexLayoutResult ascent members.
      const auto wm = aAxisTracker.GetWritingMode();
      if (aBaselineGroup == BaselineSharingGroup::First) {
        return ascent +
               aReflowInput.ComputedLogicalBorderPadding(wm).BStart(wm);
      }
      return flr.mContentBoxCrossSize - ascent +
             aReflowInput.ComputedLogicalBorderPadding(wm).BEnd(wm);
    };

    if (lineForFirstBaseline && lineForFirstBaseline == &line) {
      flr.mAscent = ComputeAscentFromLine(line, BaselineSharingGroup::First);
    }
    if (lineForLastBaseline && lineForLastBaseline == &line) {
      flr.mAscentForLast =
          ComputeAscentFromLine(line, BaselineSharingGroup::Last);
    }

    crossAxisPosnTracker.TraverseLine(line);
    crossAxisPosnTracker.TraversePackingSpace();

    if (&line != &flr.mLines.LastElement()) {
      crossAxisPosnTracker.TraverseGap();
    }
    ++lineIndex;
  }

  return flr;
}

// This data structure is used in fragmentation, storing the block coordinate
// metrics when reflowing 1) the BStart-most line in each fragment of a
// row-oriented flex container or, 2) the BStart-most item in each fragment of a
// single-line column-oriented flex container.
//
// When we lay out a row-oriented flex container fragment, its first line might
// contain one or more monolithic items that were pushed from the previous
// fragment specifically to avoid having those monolithic items overlap the
// page/column break. The situation is similar for single-row column-oriented
// flex container fragments, but a bit simpler; only their first item might have
// been pushed to avoid overlapping a page/column break.
//
// We'll have to place any such pushed items at the block-start edge of the
// current fragment's content-box, which is as close as we can get them to their
// theoretical/unfragmented position (without slicing them); but it does
// represent a shift away from their theoretical/unfragmented position (which
// was somewhere in the previous fragment).
//
// When that happens, we need to record the maximum such shift that we had to
// perform so that we can apply the same block-endwards shift to "downstream"
// items (items towards the block-end edge) that we could otherwise collide
// with. We also potentially apply the same shift when computing the block-end
// edge of this flex container fragment's content-box so that we don't
// inadvertently shift the last item (or line-of-items) to overlap the flex
// container's border, or content beyond the flex container.
//
// We use this structure to keep track of several metrics, in service of this
// goal. This structure is also necessary to adjust PerFragmentFlexData at the
// end of ReflowChildren().
//
// Note: "First" in the struct name means "BStart-most", not the order in the
// flex line array or flex item array.
struct FirstLineOrFirstItemBAxisMetrics final {
  // This value stores the block-end edge shift for 1) the BStart-most line in
  // the current fragment of a row-oriented flex container, or 2) the
  // BStart-most item in the current fragment of a single-line column-oriented
  // flex container. This number is non-negative.
  //
  // This value may become positive when any item is a first-in-flow and also
  // satisfies either the above condition 1) or 2), since that's a hint that it
  // could be monolithic or have a monolithic first descendant, and therefore an
  // item that might incur a page/column-break-dodging position-shift that this
  // variable needs to track.
  //
  // This value also stores the fragmentation-imposed growth in the block-size
  // of a) the BStart-most line in the current fragment of a row-oriented flex
  // container, or b) the BStart-most item in the current fragment of a
  // single-line column-oriented flex container. This number is non-negative.
  nscoord mBEndEdgeShift = 0;

  // The first and second value in the pair store the max block-end edges for
  // items before and after applying the per-item position-shift in the block
  // axis. We only record the block-end edges for items with first-in-flow
  // frames placed in the current flex container fragment. This is used only by
  // row-oriented flex containers.
  Maybe<std::pair<nscoord, nscoord>> mMaxBEndEdge;
};

std::tuple<nscoord, nsReflowStatus> nsFlexContainerFrame::ReflowChildren(
    const ReflowInput& aReflowInput, const nsSize& aContainerSize,
    const LogicalSize& aAvailableSizeForItems,
    const LogicalMargin& aBorderPadding, const FlexboxAxisTracker& aAxisTracker,
    FlexLayoutResult& aFlr, PerFragmentFlexData& aFragmentData) {
  if (HidesContentForLayout()) {
    return {0, nsReflowStatus()};
  }

  // Before giving each child a final reflow, calculate the origin of the
  // flex container's content box (with respect to its border-box), so that
  // we can compute our flex item's final positions.
  WritingMode flexWM = aReflowInput.GetWritingMode();
  const LogicalPoint containerContentBoxOrigin =
      aBorderPadding.StartOffset(flexWM);

  // The block-end of children is relative to the flex container's border-box.
  nscoord maxBlockEndEdgeOfChildren = containerContentBoxOrigin.B(flexWM);

  FirstLineOrFirstItemBAxisMetrics bAxisMetrics;
  FrameHashtable pushedItems;
  FrameHashtable incompleteItems;
  FrameHashtable overflowIncompleteItems;

  const bool isSingleLine =
      IsSingleLine(aReflowInput.mFrame, aReflowInput.mStylePosition);
  const FlexLine& startmostLine = StartmostLine(aFlr.mLines, aAxisTracker);
  const FlexLine& endmostLine = EndmostLine(aFlr.mLines, aAxisTracker);
  const FlexItem* startmostItem =
      startmostLine.IsEmpty() ? nullptr
                              : &startmostLine.StartmostItem(aAxisTracker);
  const FlexItem* endmostItem =
      endmostLine.IsEmpty() ? nullptr : &endmostLine.EndmostItem(aAxisTracker);

  bool endmostItemOrLineHasBreakAfter = false;
  // If true, push all remaining flex items to the container's next-in-flow.
  bool shouldPushRemainingItems = false;

  // FINAL REFLOW: Give each child frame another chance to reflow.
  const size_t numLines = aFlr.mLines.Length();
  for (size_t lineIdx = 0; lineIdx < numLines; ++lineIdx) {
    // Iterate flex lines from the startmost to endmost (relative to flex
    // container's writing-mode).
    const auto& line =
        aFlr.mLines[aAxisTracker.IsCrossAxisReversed() ? numLines - lineIdx - 1
                                                       : lineIdx];
    MOZ_ASSERT(lineIdx != 0 || &line == &startmostLine,
               "Logic for finding startmost line should be consistent!");

    // These two variables can be set when we are a row-oriented flex container
    // during fragmentation.
    bool lineHasBreakBefore = false;
    bool lineHasBreakAfter = false;

    const size_t numItems = line.Items().Length();
    for (size_t itemIdx = 0; itemIdx < numItems; ++itemIdx) {
      // Iterate flex items from the startmost to endmost (relative to flex
      // container's writing-mode).
      const FlexItem& item = line.Items()[aAxisTracker.IsMainAxisReversed()
                                              ? numItems - itemIdx - 1
                                              : itemIdx];
      MOZ_ASSERT(lineIdx != 0 || itemIdx != 0 || &item == startmostItem,
                 "Logic for finding startmost item should be consistent!");

      LogicalPoint framePos = aAxisTracker.LogicalPointFromFlexRelativePoint(
          item.MainPosition(), item.CrossPosition(), aFlr.mContentBoxMainSize,
          aFlr.mContentBoxCrossSize);
      // This variable records the item's block-end edge before we give it a
      // per-item-position-shift, if the item is a first-in-flow in the
      // startmost line of a row-oriented flex container fragment. It is used to
      // determine the block-end edge shift for the startmost line at the end of
      // the outer loop.
      Maybe<nscoord> frameBPosBeforePerItemShift;

      if (item.Frame()->GetPrevInFlow()) {
        // The item is a continuation. Lay it out at the beginning of the
        // available space.
        framePos.B(flexWM) = 0;
      } else if (GetPrevInFlow()) {
        // The item we're placing is not a continuation; though we're placing it
        // into a flex container fragment which *is* a continuation. To compute
        // the item's correct position in this fragment, we adjust the item's
        // theoretical/unfragmented block-direction position by subtracting the
        // cumulative content-box block-size for all the previous fragments and
        // adding the cumulative block-end edge shift.
        //
        // Note that the item's position in this fragment has not been finalized
        // yet. At this point, we've adjusted the item's
        // theoretical/unfragmented position to be relative to the block-end
        // edge of the previous container fragment's content-box. Later, we'll
        // compute per-item position-shift to finalize its position.
        framePos.B(flexWM) -= aFragmentData.mCumulativeContentBoxBSize;
        framePos.B(flexWM) += aFragmentData.mCumulativeBEndEdgeShift;

        // This helper gets the per-item position-shift in the block-axis.
        auto GetPerItemPositionShiftToBEnd = [&]() {
          if (framePos.B(flexWM) >= 0) {
            // The item final position might be in current flex container
            // fragment or in any of the later fragments. No adjustment needed.
            return 0;
          }

          // The item's block position is negative, but we want to place it at
          // the content-box block-start edge of this container fragment. To
          // achieve this, return a negated (positive) value to make the final
          // block position zero.
          //
          // This scenario occurs when fragmenting a row-oriented flex container
          // where this item is pushed to this container fragment.
          return -framePos.B(flexWM);
        };

        if (aAxisTracker.IsRowOriented()) {
          if (&line == &startmostLine) {
            frameBPosBeforePerItemShift.emplace(framePos.B(flexWM));
            framePos.B(flexWM) += GetPerItemPositionShiftToBEnd();
          } else {
            // We've computed two things for the startmost line during the outer
            // loop's first iteration: 1) how far the block-end edge had to
            // shift and 2) how large the block-size needed to grow. Here, we
            // just shift all items in the rest of the lines the same amount.
            framePos.B(flexWM) += bAxisMetrics.mBEndEdgeShift;
          }
        } else {
          MOZ_ASSERT(aAxisTracker.IsColumnOriented());
          if (isSingleLine) {
            if (&item == startmostItem) {
              bAxisMetrics.mBEndEdgeShift = GetPerItemPositionShiftToBEnd();
            }
            framePos.B(flexWM) += bAxisMetrics.mBEndEdgeShift;
          } else {
            // Bug 1806717: We need a more sophisticated solution for multi-line
            // column-oriented flex container when each line has a different
            // position-shift value. For now, we don't shift them.
          }
        }
      }

      // Adjust available block-size for the item. (We compute it here because
      // framePos is still relative to the container's content-box.)
      //
      // Note: The available block-size can become negative if item's
      // block-direction position is below available space's block-end.
      const nscoord availableBSizeForItem =
          aAvailableSizeForItems.BSize(flexWM) == NS_UNCONSTRAINEDSIZE
              ? NS_UNCONSTRAINEDSIZE
              : aAvailableSizeForItems.BSize(flexWM) - framePos.B(flexWM);

      // Adjust framePos to be relative to the container's border-box
      // (i.e. its frame rect), instead of the container's content-box:
      framePos += containerContentBoxOrigin;

      // Check if we can skip reflowing the item because it will be pushed to
      // our next-in-flow -- i.e. if there was a forced break before it, or its
      // position is beyond the available space's block-end.
      bool itemInPushedItems = false;
      if (shouldPushRemainingItems) {
        FLEX_ITEM_LOG(
            item.Frame(),
            "[frag] Item needed to be pushed to container's next-in-flow due "
            "to a forced break before it");
        pushedItems.Insert(item.Frame());
        itemInPushedItems = true;
      } else if (availableBSizeForItem != NS_UNCONSTRAINEDSIZE &&
                 availableBSizeForItem <= 0) {
        // The item's position is beyond the available space, so we have to push
        // it.
        //
        // Note: Even if all of our items are beyond the available space & get
        // pushed here, we'll be guaranteed to place at least one of them (and
        // make progress) in one of the flex container's *next* fragment. It's
        // because ComputeAvailableSizeForItems() always reserves at least 1px
        // available block-size for its children, and we consume all available
        // block-size and add it to
        // PerFragmentFlexData::mCumulativeContentBoxBSize even if we are not
        // laying out any child.
        FLEX_ITEM_LOG(
            item.Frame(),
            "[frag] Item needed to be pushed to container's next-in-flow due "
            "to being positioned beyond block-end edge of available space");
        pushedItems.Insert(item.Frame());
        itemInPushedItems = true;
      } else if (item.NeedsFinalReflow(aReflowInput)) {
        // The available size must be in item's writing-mode.
        const WritingMode itemWM = item.GetWritingMode();
        const auto availableSize =
            LogicalSize(flexWM, aAvailableSizeForItems.ISize(flexWM),
                        availableBSizeForItem)
                .ConvertTo(itemWM, flexWM);

        const bool isAdjacentWithBStart =
            framePos.B(flexWM) == containerContentBoxOrigin.B(flexWM);
        const nsReflowStatus childStatus =
            ReflowFlexItem(aAxisTracker, aReflowInput, item, framePos,
                           isAdjacentWithBStart, availableSize, aContainerSize);

        if (aReflowInput.IsInFragmentedContext()) {
          const bool itemHasBreakBefore =
              item.Frame()->ShouldBreakBefore(aReflowInput.mBreakType) ||
              childStatus.IsInlineBreakBefore();
          if (itemHasBreakBefore) {
            if (aAxisTracker.IsRowOriented()) {
              lineHasBreakBefore = true;
            } else if (isSingleLine) {
              if (&item == startmostItem) {
                if (!GetPrevInFlow() && !aReflowInput.mFlags.mIsTopOfPage) {
                  // If we are first-in-flow and not at top-of-page, early
                  // return here to propagate forced break-before from the
                  // startmost item to the flex container.
                  nsReflowStatus childrenStatus;
                  childrenStatus.SetInlineLineBreakBeforeAndReset();
                  return {0, childrenStatus};
                }
              } else {
                shouldPushRemainingItems = true;
              }
            } else {
              // Bug 1806717: We haven't implemented fragmentation for
              // multi-line column-oriented flex container, so we just ignore
              // forced breaks for now.
            }
          }
        }

        const bool shouldPushItem = [&]() {
          if (shouldPushRemainingItems) {
            return true;
          }
          if (availableBSizeForItem == NS_UNCONSTRAINEDSIZE) {
            // If the available block-size is unconstrained, then we're not
            // fragmenting and we don't want to push the item.
            return false;
          }
          if (isAdjacentWithBStart) {
            // The flex item is adjacent with block-start of the container's
            // content-box. Don't push it, or we'll trap in an infinite loop.
            return false;
          }
          if (item.Frame()->BSize() <= availableBSizeForItem) {
            return false;
          }
          if (aAxisTracker.IsColumnOriented() &&
              item.Frame()->StyleDisplay()->mBreakBefore ==
                  StyleBreakBetween::Avoid) {
            return false;
          }
          return true;
        }();
        if (shouldPushItem) {
          FLEX_ITEM_LOG(
              item.Frame(),
              "[frag] Item needed to be pushed to container's next-in-flow "
              "because it encounters a forced break before it, or its "
              "block-size is larger than the available space");
          pushedItems.Insert(item.Frame());
          itemInPushedItems = true;
        } else if (childStatus.IsIncomplete()) {
          incompleteItems.Insert(item.Frame());
        } else if (childStatus.IsOverflowIncomplete()) {
          overflowIncompleteItems.Insert(item.Frame());
        }

        if (aReflowInput.IsInFragmentedContext()) {
          const bool itemHasBreakAfter =
              item.Frame()->ShouldBreakAfter(aReflowInput.mBreakType) ||
              childStatus.IsInlineBreakAfter();
          if (itemHasBreakAfter) {
            if (aAxisTracker.IsRowOriented()) {
              lineHasBreakAfter = true;
            } else if (isSingleLine) {
              shouldPushRemainingItems = true;
              if (&item == endmostItem) {
                endmostItemOrLineHasBreakAfter = true;
              }
            } else {
              // Bug 1806717: We haven't implemented fragmentation for
              // multi-line column-oriented flex container, so we just ignore
              // forced breaks for now.
            }
          }
        }
      } else {
        // We already reflowed the item with the right content-box size, so we
        // can simply move it into place.
        MoveFlexItemToFinalPosition(item, framePos, aContainerSize);
      }

      if (!itemInPushedItems) {
        const nscoord borderBoxBSize = item.Frame()->BSize(flexWM);
        const nscoord bEndEdgeAfterPerItemShift =
            framePos.B(flexWM) + borderBoxBSize;

        // The item (or a fragment thereof) was placed in this flex container
        // fragment. Update the max block-end edge with the item's block-end
        // edge.
        maxBlockEndEdgeOfChildren =
            std::max(maxBlockEndEdgeOfChildren, bEndEdgeAfterPerItemShift);

        if (frameBPosBeforePerItemShift) {
          // Make the block-end edge relative to flex container's border-box
          // because bEndEdgeAfterPerItemShift is relative to the border-box.
          const nscoord bEndEdgeBeforePerItemShift =
              containerContentBoxOrigin.B(flexWM) +
              *frameBPosBeforePerItemShift + borderBoxBSize;

          if (bAxisMetrics.mMaxBEndEdge) {
            auto& [before, after] = *bAxisMetrics.mMaxBEndEdge;
            before = std::max(before, bEndEdgeBeforePerItemShift);
            after = std::max(after, bEndEdgeAfterPerItemShift);
          } else {
            bAxisMetrics.mMaxBEndEdge.emplace(bEndEdgeBeforePerItemShift,
                                              bEndEdgeAfterPerItemShift);
          }
        }

        if (item.Frame()->GetPrevInFlow()) {
          // Items with a previous-continuation may experience some
          // fragmentation-imposed growth in their block-size; we compute that
          // here.
          const nscoord bSizeOfThisFragment =
              item.Frame()->ContentSize(flexWM).BSize(flexWM);
          const nscoord consumedBSize = FlexItemConsumedBSize(item);
          const nscoord unfragmentedBSize = item.BSize();
          nscoord bSizeGrowthOfThisFragment = 0;

          if (consumedBSize >= unfragmentedBSize) {
            // The item's block-size has been grown to exceed the unfragmented
            // block-size in the previous fragments.
            bSizeGrowthOfThisFragment = bSizeOfThisFragment;
          } else if (consumedBSize + bSizeOfThisFragment >= unfragmentedBSize) {
            // The item's block-size just grows in the current fragment to
            // exceed the unfragmented block-size.
            bSizeGrowthOfThisFragment =
                consumedBSize + bSizeOfThisFragment - unfragmentedBSize;
          }

          if (aAxisTracker.IsRowOriented()) {
            if (&line == &startmostLine) {
              bAxisMetrics.mBEndEdgeShift = std::max(
                  bAxisMetrics.mBEndEdgeShift, bSizeGrowthOfThisFragment);
            }
          } else {
            MOZ_ASSERT(aAxisTracker.IsColumnOriented());
            if (isSingleLine) {
              if (&item == startmostItem) {
                MOZ_ASSERT(bAxisMetrics.mBEndEdgeShift == 0,
                           "The item's frame is a continuation, so it "
                           "shouldn't shift!");
                bAxisMetrics.mBEndEdgeShift = bSizeGrowthOfThisFragment;
              }
            } else {
              // Bug 1806717: We need a more sophisticated solution for
              // multi-line column-oriented flex container when each line has a
              // different block-size growth value. For now, we don't deal with
              // them.
            }
          }
        }
      }

      // If the item has auto margins, and we were tracking the UsedMargin
      // property, set the property to the computed margin values.
      if (item.HasAnyAutoMargin()) {
        nsMargin* propValue =
            item.Frame()->GetProperty(nsIFrame::UsedMarginProperty());
        if (propValue) {
          *propValue = item.PhysicalMargin();
        }
      }
    }

    if (aReflowInput.IsInFragmentedContext() && aAxisTracker.IsRowOriented()) {
      // Propagate forced break values from the flex items to its flex line.
      if (lineHasBreakBefore) {
        if (&line == &startmostLine) {
          if (!GetPrevInFlow() && !aReflowInput.mFlags.mIsTopOfPage) {
            // If we are first-in-flow and not at top-of-page, early return here
            // to propagate forced break-before from the startmost line to the
            // flex container.
            nsReflowStatus childrenStatus;
            childrenStatus.SetInlineLineBreakBeforeAndReset();
            return {0, childrenStatus};
          }
        } else {
          // Current non-startmost line has forced break-before, so push all the
          // items in this line.
          for (const FlexItem& item : line.Items()) {
            pushedItems.Insert(item.Frame());
            incompleteItems.Remove(item.Frame());
            overflowIncompleteItems.Remove(item.Frame());
          }
          shouldPushRemainingItems = true;
        }
      }
      if (lineHasBreakAfter) {
        shouldPushRemainingItems = true;
        if (&line == &endmostLine) {
          endmostItemOrLineHasBreakAfter = true;
        }
      }
    }

    // Now we've finished processing all the items in the startmost line.
    // Determine the amount by which the startmost line's block-end edge has
    // shifted, so we can apply the same shift for the remaining lines.
    if (GetPrevInFlow() && aAxisTracker.IsRowOriented() &&
        &line == &startmostLine && bAxisMetrics.mMaxBEndEdge) {
      auto& [before, after] = *bAxisMetrics.mMaxBEndEdge;
      bAxisMetrics.mBEndEdgeShift =
          std::max(bAxisMetrics.mBEndEdgeShift, after - before);
    }
  }

  if (!aFlr.mPlaceholders.IsEmpty()) {
    ReflowPlaceholders(aReflowInput, aFlr.mPlaceholders,
                       containerContentBoxOrigin, aContainerSize);
  }

  nsReflowStatus childrenStatus;
  if (!pushedItems.IsEmpty() || !incompleteItems.IsEmpty()) {
    childrenStatus.SetIncomplete();
  } else if (!overflowIncompleteItems.IsEmpty()) {
    childrenStatus.SetOverflowIncomplete();
  } else if (endmostItemOrLineHasBreakAfter) {
    childrenStatus.SetInlineLineBreakAfter();
  }
  PushIncompleteChildren(pushedItems, incompleteItems, overflowIncompleteItems);

  // TODO: Try making this a fatal assertion after we fix bug 1751260.
  NS_ASSERTION(childrenStatus.IsFullyComplete() ||
                   aAvailableSizeForItems.BSize(flexWM) != NS_UNCONSTRAINEDSIZE,
               "We shouldn't have any incomplete children if the available "
               "block-size is unconstrained!");

  if (!pushedItems.IsEmpty()) {
    AddStateBits(NS_STATE_FLEX_DID_PUSH_ITEMS);
  }

  if (GetPrevInFlow()) {
    aFragmentData.mCumulativeBEndEdgeShift += bAxisMetrics.mBEndEdgeShift;
  }

  return {maxBlockEndEdgeOfChildren, childrenStatus};
}

void nsFlexContainerFrame::PopulateReflowOutput(
    ReflowOutput& aReflowOutput, const ReflowInput& aReflowInput,
    nsReflowStatus& aStatus, const LogicalSize& aContentBoxSize,
    const LogicalMargin& aBorderPadding, const nscoord aConsumedBSize,
    const bool aMayNeedNextInFlow, const nscoord aMaxBlockEndEdgeOfChildren,
    const nsReflowStatus& aChildrenStatus,
    const FlexboxAxisTracker& aAxisTracker, FlexLayoutResult& aFlr) {
  const WritingMode flexWM = aReflowInput.GetWritingMode();

  // Compute flex container's desired size (in its own writing-mode).
  LogicalSize desiredSizeInFlexWM(flexWM);
  desiredSizeInFlexWM.ISize(flexWM) =
      aContentBoxSize.ISize(flexWM) + aBorderPadding.IStartEnd(flexWM);

  // Unconditionally skip adding block-end border and padding for now. We add it
  // lower down, after we've established baseline and decided whether bottom
  // border-padding fits (if we're fragmented).
  const nscoord effectiveContentBSizeWithBStartBP =
      aContentBoxSize.BSize(flexWM) - aConsumedBSize +
      aBorderPadding.BStart(flexWM);
  nscoord blockEndContainerBP = aBorderPadding.BEnd(flexWM);

  if (aMayNeedNextInFlow) {
    // We assume our status should be reported as incomplete because we may need
    // a next-in-flow.
    bool isStatusIncomplete = true;

    const nscoord availableBSizeMinusBEndBP =
        aReflowInput.AvailableBSize() - aBorderPadding.BEnd(flexWM);

    if (aMaxBlockEndEdgeOfChildren <= availableBSizeMinusBEndBP) {
      // Consume all the available block-size.
      desiredSizeInFlexWM.BSize(flexWM) = availableBSizeMinusBEndBP;
    } else {
      // This case happens if we have some tall unbreakable children exceeding
      // the available block-size.
      desiredSizeInFlexWM.BSize(flexWM) = std::min(
          effectiveContentBSizeWithBStartBP, aMaxBlockEndEdgeOfChildren);

      if ((aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE ||
           aChildrenStatus.IsFullyComplete()) &&
          aMaxBlockEndEdgeOfChildren >= effectiveContentBSizeWithBStartBP) {
        // We have some tall unbreakable child that's sticking off the end of
        // our fragment, *and* forcing us to consume all of our remaining
        // content block-size and call ourselves complete.
        //
        // - If we have a definite block-size: we get here if the tall child
        //   makes us reach that block-size.
        // - If we have a content-based block-size: we get here if the tall
        //   child makes us reach the content-based block-size from a
        //   theoretical unfragmented layout, *and* all our children are
        //   complete. (Note that if we have some incomplete child, then we
        //   instead prefer to return an incomplete status, so we can get a
        //   next-in-flow to include that child's requested next-in-flow, in the
        //   spirit of having a block-size that fits the content.)
        //
        // TODO: the auto-height case might need more subtlety; see bug 1828977.
        isStatusIncomplete = false;

        // We also potentially need to get the unskipped block-end border and
        // padding (if we assumed it'd be skipped as part of our tentative
        // assumption that we'd be incomplete).
        if (aReflowInput.mStyleBorder->mBoxDecorationBreak ==
            StyleBoxDecorationBreak::Slice) {
          blockEndContainerBP =
              aReflowInput.ComputedLogicalBorderPadding(flexWM).BEnd(flexWM);
        }
      }
    }

    if (isStatusIncomplete) {
      aStatus.SetIncomplete();
    }
  } else {
    // Our own effective content-box block-size can fit within the available
    // block-size.
    desiredSizeInFlexWM.BSize(flexWM) = effectiveContentBSizeWithBStartBP;
  }

  // Now, we account for how the block-end border and padding (if any) impacts
  // our desired size. If adding it pushes us over the available block-size,
  // then we become incomplete (unless we already weren't asking for any
  // block-size, in which case we stay complete to avoid looping forever).
  //
  // NOTE: If we have auto block-size, we allow our block-end border and padding
  // to push us over the available block-size without requesting a continuation,
  // for consistency with the behavior of "display:block" elements.
  const nscoord effectiveContentBSizeWithBStartEndBP =
      desiredSizeInFlexWM.BSize(flexWM) + blockEndContainerBP;

  if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      effectiveContentBSizeWithBStartEndBP > aReflowInput.AvailableBSize() &&
      desiredSizeInFlexWM.BSize(flexWM) != 0 &&
      aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE) {
    // We couldn't fit with the block-end border and padding included, so we'll
    // need a continuation.
    aStatus.SetIncomplete();

    if (aReflowInput.mStyleBorder->mBoxDecorationBreak ==
        StyleBoxDecorationBreak::Slice) {
      blockEndContainerBP = 0;
    }
  }

  // The variable "blockEndContainerBP" now accurately reflects how much (if
  // any) block-end border and padding we want for this frame, so we can proceed
  // to add it in.
  desiredSizeInFlexWM.BSize(flexWM) += blockEndContainerBP;

  if (aStatus.IsComplete() && !aChildrenStatus.IsFullyComplete()) {
    aStatus.SetOverflowIncomplete();
    aStatus.SetNextInFlowNeedsReflow();
  }

  // If we are the first-in-flow and not fully complete (either our block-size
  // or any of our flex items cannot fit in the available block-size), and the
  // style requires us to avoid breaking inside, set the status to prompt our
  // parent to push us to the next page/column.
  if (!GetPrevInFlow() && !aStatus.IsFullyComplete() &&
      ShouldAvoidBreakInside(aReflowInput)) {
    aStatus.SetInlineLineBreakBeforeAndReset();
    return;
  }

  // Propagate forced break values from flex items or flex lines.
  if (aChildrenStatus.IsInlineBreakBefore()) {
    aStatus.SetInlineLineBreakBeforeAndReset();
  }
  if (aChildrenStatus.IsInlineBreakAfter()) {
    aStatus.SetInlineLineBreakAfter();
  }

  // If we haven't established a baseline for the container yet, i.e. if we
  // don't have any flex item in the startmost flex line that participates in
  // baseline alignment, then use the startmost flex item to derive the
  // container's baseline.
  if (const FlexLine& line = StartmostLine(aFlr.mLines, aAxisTracker);
      aFlr.mAscent == nscoord_MIN && !line.IsEmpty()) {
    const FlexItem& item = line.StartmostItem(aAxisTracker);
    aFlr.mAscent = item.Frame()
                       ->GetLogicalPosition(
                           flexWM, desiredSizeInFlexWM.GetPhysicalSize(flexWM))
                       .B(flexWM) +
                   item.ResolvedAscent(true);
  }

  // Likewise, if we don't have any flex item in the endmost flex line that
  // participates in last baseline alignment, then use the endmost flex item to
  // derived the container's last baseline.
  if (const FlexLine& line = EndmostLine(aFlr.mLines, aAxisTracker);
      aFlr.mAscentForLast == nscoord_MIN && !line.IsEmpty()) {
    const FlexItem& item = line.EndmostItem(aAxisTracker);
    const nscoord lastAscent =
        item.Frame()
            ->GetLogicalPosition(flexWM,
                                 desiredSizeInFlexWM.GetPhysicalSize(flexWM))
            .B(flexWM) +
        item.ResolvedAscent(false);

    aFlr.mAscentForLast = desiredSizeInFlexWM.BSize(flexWM) - lastAscent;
  }

  if (aFlr.mAscent == nscoord_MIN) {
    // Still don't have our baseline set -- this happens if we have no
    // children, if our children are huge enough that they have nscoord_MIN
    // as their baseline, or our content is hidden in which case, we'll use the
    // wrong baseline (but no big deal).
    NS_WARNING_ASSERTION(
        HidesContentForLayout() || aFlr.mLines[0].IsEmpty(),
        "Have flex items but didn't get an ascent - that's odd (or there are "
        "just gigantic sizes involved)");
    // Per spec, synthesize baseline from the flex container's content box
    // (i.e. use block-end side of content-box)
    // XXXdholbert This only makes sense if parent's writing mode is
    // horizontal (& even then, really we should be using the BSize in terms
    // of the parent's writing mode, not ours). Clean up in bug 1155322.
    aFlr.mAscent = effectiveContentBSizeWithBStartBP;
  }

  if (aFlr.mAscentForLast == nscoord_MIN) {
    // Still don't have our last baseline set -- this happens if we have no
    // children, if our children are huge enough that they have nscoord_MIN
    // as their baseline, or our content is hidden in which case, we'll use the
    // wrong baseline (but no big deal).
    NS_WARNING_ASSERTION(
        HidesContentForLayout() || aFlr.mLines[0].IsEmpty(),
        "Have flex items but didn't get an ascent - that's odd (or there are "
        "just gigantic sizes involved)");
    // Per spec, synthesize baseline from the flex container's content box
    // (i.e. use block-end side of content-box)
    // XXXdholbert This only makes sense if parent's writing mode is
    // horizontal (& even then, really we should be using the BSize in terms
    // of the parent's writing mode, not ours). Clean up in bug 1155322.
    aFlr.mAscentForLast = blockEndContainerBP;
  }

  if (HasAnyStateBits(NS_STATE_FLEX_SYNTHESIZE_BASELINE)) {
    // This will force our parent to call GetLogicalBaseline, which will
    // synthesize a margin-box baseline.
    aReflowOutput.SetBlockStartAscent(ReflowOutput::ASK_FOR_BASELINE);
  } else {
    // XXXdholbert aFlr.mAscent needs to be in terms of our parent's
    // writing-mode here. See bug 1155322.
    aReflowOutput.SetBlockStartAscent(aFlr.mAscent);
  }

  // Cache the container baselines so that our parent can baseline-align us.
  mFirstBaseline = aFlr.mAscent;
  mLastBaseline = aFlr.mAscentForLast;

  // Convert flex container's final desired size to parent's WM, for outparam.
  aReflowOutput.SetSize(flexWM, desiredSizeInFlexWM);
}

void nsFlexContainerFrame::MoveFlexItemToFinalPosition(
    const FlexItem& aItem, const LogicalPoint& aFramePos,
    const nsSize& aContainerSize) {
  const WritingMode outerWM = aItem.ContainingBlockWM();
  const nsStyleDisplay* display = aItem.Frame()->StyleDisplay();
  LogicalPoint pos(aFramePos);
  if (display->IsRelativelyOrStickyPositionedStyle()) {
    // If the item is relatively positioned, look up its offsets (cached from
    // previous reflow). A sticky positioned item can pass a dummy
    // logicalOffsets into ApplyRelativePositioning().
    LogicalMargin logicalOffsets(outerWM);
    if (display->IsRelativelyPositionedStyle()) {
      nsMargin* cachedOffsets =
          aItem.Frame()->GetProperty(nsIFrame::ComputedOffsetProperty());
      MOZ_ASSERT(
          cachedOffsets,
          "relpos previously-reflowed frame should've cached its offsets");
      logicalOffsets = LogicalMargin(outerWM, *cachedOffsets);
    }
    ReflowInput::ApplyRelativePositioning(aItem.Frame(), outerWM,
                                          logicalOffsets, &pos, aContainerSize);
  }

  FLEX_ITEM_LOG(aItem.Frame(), "Moving item to its desired position %s",
                ToString(pos).c_str());
  aItem.Frame()->SetPosition(outerWM, pos, aContainerSize);
}

nsReflowStatus nsFlexContainerFrame::ReflowFlexItem(
    const FlexboxAxisTracker& aAxisTracker, const ReflowInput& aReflowInput,
    const FlexItem& aItem, const LogicalPoint& aFramePos,
    const bool aIsAdjacentWithBStart, const LogicalSize& aAvailableSize,
    const nsSize& aContainerSize) {
  FLEX_ITEM_LOG(aItem.Frame(), "Doing final reflow");

  // Returns true if we should use 'auto' in block axis's StyleSizeOverrides to
  // allow fragmentation-imposed block-size growth.
  auto ComputeBSizeOverrideWithAuto = [&]() {
    if (!aReflowInput.IsInFragmentedContext()) {
      return false;
    }
    if (aItem.Frame()->IsReplaced()) {
      // Disallow fragmentation-imposed block-size growth for replaced elements
      // since they are monolithic, and cannot be fragmented.
      return false;
    }
    if (aItem.HasAspectRatio()) {
      // Aspect-ratio's automatic content-based minimum size doesn't work
      // properly in a fragmented context (Bug 1868284) when we use 'auto'
      // block-size to apply the fragmentation-imposed block-size growth.
      // Disable it for now so that items with aspect-ratios can still use their
      // known block-sizes (from flex layout algorithm) in final reflow.
      return false;
    }
    if (aItem.IsBlockAxisMainAxis()) {
      if (aItem.IsFlexBaseSizeContentBSize()) {
        // The flex item resolved its indefinite flex-basis to the content
        // block-size.
        if (aItem.IsMainMinSizeContentBSize()) {
          // The item's flex base size and main min-size are both content
          // block-size. We interpret this content-based block-size as
          // permission to apply fragmentation-imposed block-size growth.
          return true;
        }
        if (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE) {
          // The flex container has an indefinite block-size. We allow the
          // item's to apply fragmentation-imposed block-size growth.
          return true;
        }
      }
      return false;
    }

    MOZ_ASSERT(aItem.IsBlockAxisCrossAxis());
    MOZ_ASSERT(aItem.IsStretched(),
               "No need to override block-size with 'auto' if the item is not "
               "stretched in the cross axis!");

    Maybe<nscoord> measuredBSize = aItem.MeasuredBSize();
    if (measuredBSize && aItem.CrossSize() == *measuredBSize) {
      // The item has a measured content-based block-size due to having an
      // indefinite cross-size. If its cross-size is equal to the content-based
      // block-size, then it is the tallest item that established the cross-size
      // of the flex line. We allow it apply fragmentation-imposed block-size
      // growth.
      //
      // Note: We only allow the tallest item to grow because it is likely to
      // have the most impact on the overall flex container block-size growth.
      // This is not a perfect solution since other shorter items in the same
      // line might also have fragmentation-imposed block-size growth, but
      // currently there is no reliable way to detect whether they will outgrow
      // the tallest item.
      return true;
    }
    return false;
  };

  StyleSizeOverrides sizeOverrides;
  bool overrideBSizeWithAuto = false;

  // Override flex item's main size.
  if (aItem.IsInlineAxisMainAxis()) {
    sizeOverrides.mStyleISize.emplace(aItem.StyleMainSize());
    FLEX_LOGV("Main size (inline-size) override: %d", aItem.MainSize());
  } else {
    overrideBSizeWithAuto = ComputeBSizeOverrideWithAuto();
    if (overrideBSizeWithAuto) {
      sizeOverrides.mStyleBSize.emplace(StyleSize::Auto());
      FLEX_LOGV("Main size (block-size) override: Auto");
    } else {
      sizeOverrides.mStyleBSize.emplace(aItem.StyleMainSize());
      FLEX_LOGV("Main size (block-size) override: %d", aItem.MainSize());
    }
  }

  // Override flex item's cross size if it was stretched in the cross axis (in
  // which case we're imposing a cross size).
  if (aItem.IsStretched()) {
    if (aItem.IsInlineAxisCrossAxis()) {
      sizeOverrides.mStyleISize.emplace(aItem.StyleCrossSize());
      FLEX_LOGV("Cross size (inline-size) override: %d", aItem.CrossSize());
    } else {
      overrideBSizeWithAuto = ComputeBSizeOverrideWithAuto();
      if (overrideBSizeWithAuto) {
        sizeOverrides.mStyleBSize.emplace(StyleSize::Auto());
        FLEX_LOGV("Cross size (block-size) override: Auto");
      } else {
        sizeOverrides.mStyleBSize.emplace(aItem.StyleCrossSize());
        FLEX_LOGV("Cross size (block-size) override: %d", aItem.CrossSize());
      }
    }
  }
  if (sizeOverrides.mStyleBSize) {
    // We are overriding the block-size. For robustness, we always assume that
    // this represents a block-axis resize for the frame. This may be
    // conservative, but we do capture all the conditions in the block-axis
    // (checked in NeedsFinalReflow()) that make this item require a final
    // reflow. This sets relevant flags in ReflowInput::InitResizeFlags().
    aItem.Frame()->SetHasBSizeChange(true);
  }

  ReflowInput childReflowInput(PresContext(), aReflowInput, aItem.Frame(),
                               aAvailableSize, Nothing(), {}, sizeOverrides,
                               {ComputeSizeFlag::ShrinkWrap});
  if (overrideBSizeWithAuto) {
    // If we use 'auto' to override the item's block-size, set the item's
    // original block-size to min-size as a lower bound.
    childReflowInput.SetComputedMinBSize(aItem.BSize());

    // Set the item's block-size as the percentage basis so that its children
    // can resolve percentage sizes correctly.
    childReflowInput.SetPercentageBasisInBlockAxis(aItem.BSize());
  }

  if (aItem.TreatBSizeAsIndefinite() && aItem.IsBlockAxisMainAxis()) {
    childReflowInput.mFlags.mTreatBSizeAsIndefinite = true;
  }

  if (aItem.IsStretched() && aItem.IsBlockAxisCrossAxis()) {
    // This item is stretched (in the cross axis), and that axis is its block
    // axis.  That stretching effectively gives it a relative BSize.
    // XXXdholbert This flag only makes a difference if we use the flex items'
    // frame-state when deciding whether to reflow them -- and we don't, as of
    // the changes in bug 851607. So this has no effect right now, but it might
    // make a difference if we optimize to use dirty bits in the
    // future. (Reftests flexbox-resizeviewport-1.xhtml and -2.xhtml are
    // intended to catch any regressions here, if we end up relying on this bit
    // & neglecting to set it.)
    aItem.Frame()->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
  }

  if (!aIsAdjacentWithBStart) {
    // mIsTopOfPage bit in childReflowInput is carried over from aReflowInput.
    // However, if this item's position is not adjacent with the flex
    // container's content-box block-start edge, we should clear it.
    childReflowInput.mFlags.mIsTopOfPage = false;
  }

  // NOTE: Be very careful about doing anything else with childReflowInput
  // after this point, because some of its methods (e.g. SetComputedWidth)
  // internally call InitResizeFlags and stomp on mVResize & mHResize.

  FLEX_ITEM_LOG(aItem.Frame(), "Reflowing item at its desired position %s",
                ToString(aFramePos).c_str());

  // CachedFlexItemData is stored in item's writing mode, so we pass
  // aChildReflowInput into ReflowOutput's constructor.
  ReflowOutput childReflowOutput(childReflowInput);
  nsReflowStatus childStatus;
  WritingMode outerWM = aReflowInput.GetWritingMode();
  ReflowChild(aItem.Frame(), PresContext(), childReflowOutput, childReflowInput,
              outerWM, aFramePos, aContainerSize, ReflowChildFlags::Default,
              childStatus);

  // XXXdholbert Perhaps we should call CheckForInterrupt here; see bug 1495532.

  FinishReflowChild(aItem.Frame(), PresContext(), childReflowOutput,
                    &childReflowInput, outerWM, aFramePos, aContainerSize,
                    ReflowChildFlags::ApplyRelativePositioning);

  aItem.SetAscent(childReflowOutput.BlockStartAscent());

  // Update our cached flex item info:
  if (auto* cached = aItem.Frame()->GetProperty(CachedFlexItemData::Prop())) {
    cached->Update(childReflowInput, childReflowOutput,
                   FlexItemReflowType::Final);
  } else {
    cached = new CachedFlexItemData(childReflowInput, childReflowOutput,
                                    FlexItemReflowType::Final);
    aItem.Frame()->SetProperty(CachedFlexItemData::Prop(), cached);
  }

  return childStatus;
}

void nsFlexContainerFrame::ReflowPlaceholders(
    const ReflowInput& aReflowInput, nsTArray<nsIFrame*>& aPlaceholders,
    const LogicalPoint& aContentBoxOrigin, const nsSize& aContainerSize) {
  WritingMode outerWM = aReflowInput.GetWritingMode();

  // As noted in this method's documentation, we'll reflow every entry in
  // |aPlaceholders| at the container's content-box origin.
  for (nsIFrame* placeholder : aPlaceholders) {
    MOZ_ASSERT(placeholder->IsPlaceholderFrame(),
               "placeholders array should only contain placeholder frames");
    WritingMode wm = placeholder->GetWritingMode();
    LogicalSize availSize = aReflowInput.ComputedSize(wm);
    ReflowInput childReflowInput(PresContext(), aReflowInput, placeholder,
                                 availSize);
    // No need to set the -webkit-line-clamp related flags when reflowing
    // a placeholder.
    ReflowOutput childReflowOutput(outerWM);
    nsReflowStatus childStatus;
    ReflowChild(placeholder, PresContext(), childReflowOutput, childReflowInput,
                outerWM, aContentBoxOrigin, aContainerSize,
                ReflowChildFlags::Default, childStatus);

    FinishReflowChild(placeholder, PresContext(), childReflowOutput,
                      &childReflowInput, outerWM, aContentBoxOrigin,
                      aContainerSize, ReflowChildFlags::Default);

    // Mark the placeholder frame to indicate that it's not actually at the
    // element's static position, because we need to apply CSS Alignment after
    // we determine the OOF's size:
    placeholder->AddStateBits(PLACEHOLDER_STATICPOS_NEEDS_CSSALIGN);
  }
}

nscoord nsFlexContainerFrame::ComputeIntrinsicISize(
    const IntrinsicSizeInput& aInput, IntrinsicISizeType aType) {
  FLEX_LOG("Compute %s isize for flex container frame %p",
           aType == IntrinsicISizeType::MinISize ? "min" : "pref", this);

  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    return *containISize;
  }

  nscoord containerISize = 0;
  const nsStylePosition* stylePos = StylePosition();
  const FlexboxAxisTracker axisTracker(this);

  nscoord mainGapSize;
  if (axisTracker.IsRowOriented()) {
    mainGapSize = nsLayoutUtils::ResolveGapToLength(stylePos->mColumnGap,
                                                    NS_UNCONSTRAINEDSIZE);
  } else {
    mainGapSize = nsLayoutUtils::ResolveGapToLength(stylePos->mRowGap,
                                                    NS_UNCONSTRAINEDSIZE);
  }

  const bool isSingleLine = IsSingleLine(this, stylePos);
  const auto flexWM = GetWritingMode();

  // The loop below sets aside space for a gap before each item besides the
  // first. This bool helps us handle that special-case.
  bool onFirstChild = true;

  for (nsIFrame* childFrame : mFrames) {
    // Skip out-of-flow children because they don't participate in flex layout.
    if (childFrame->IsPlaceholderFrame()) {
      continue;
    }

    if (childFrame->StyleVisibility()->IsCollapse()) {
      // If we're collapsed, we don't take space in the main axis.
      continue;
    }

    const auto childWM = childFrame->GetWritingMode();
    const IntrinsicSizeInput childInput(aInput, childWM, flexWM);
    const auto* styleFrame = nsLayoutUtils::GetStyleFrame(childFrame);
    const auto* childStylePos = styleFrame->StylePosition();

    // A flex item with a definite block size can transfer its block size to the
    // inline-axis via its own aspect-ratio or serve as a percentage basis for
    // its children with aspect-ratios. Both can influence the item's intrinsic
    // inline size contribution to the flex container's intrinsic inline size.
    // So we "pre-stretch" the item's cross size (with that size considered to
    // be definite) based on the flex container's definite cross size.
    //
    // Note: The logic here is similar to the "pre-stretch" in
    // GenerateFlexItemForChild().
    //
    // We only do this if the flex container is single-line and row-oriented,
    // and if it has a definite cross size for the items to stretch to fill.
    StyleSizeOverrides sizeOverrides;
    if (isSingleLine && axisTracker.IsRowOriented() &&
        aInput.mPercentageBasisForChildren &&
        aInput.mPercentageBasisForChildren->BSize(flexWM) !=
            NS_UNCONSTRAINEDSIZE &&
        ShouldStretchCrossSize(this, childFrame, flexWM,
                               axisTracker.CrossAxis())) {
      // FIXME(emilio): Shouldn't this pass
      // aInput.mPercentageBasisForChildren->ISize() to IntrinsicBSizeOffsets()?
      const auto offsets = childFrame->IntrinsicBSizeOffsets();
      // FIXME(bug 1933566): We resolve this here manually because 'stretch'
      // isn't yet properly handled in intrinsic sizing, but we should
      // eventually simplify this to look more like GenerateFlexItemForChild(),
      // once that works.
      const auto stretchedStyleCrossSize =
          StyleSize::FromAppUnits(nsLayoutUtils::ComputeStretchBSize(
              aInput.mPercentageBasisForChildren->BSize(flexWM), offsets.margin,
              offsets.BorderPadding(), childStylePos->mBoxSizing));
      // The size override is in the child's own writing mode.
      if (flexWM.IsOrthogonalTo(childWM)) {
        sizeOverrides.mStyleISize.emplace(stretchedStyleCrossSize);
      } else {
        sizeOverrides.mStyleBSize.emplace(stretchedStyleCrossSize);
      }
    }
    nscoord childISize = nsLayoutUtils::IntrinsicForContainer(
        childInput.mContext, childFrame, aType,
        childInput.mPercentageBasisForChildren, 0, sizeOverrides);

    // * For a row-oriented single-line flex container, the intrinsic
    // {min/pref}-isize is the sum of its items' {min/pref}-isizes and
    // (n-1) column gaps.
    // * For a column-oriented flex container, the intrinsic min isize
    // is the max of its items' min isizes.
    // * For a row-oriented multi-line flex container, the intrinsic
    // pref isize is former (sum), and its min isize is the latter (max).
    if (axisTracker.IsRowOriented() &&
        (isSingleLine || aType == IntrinsicISizeType::PrefISize)) {
      containerISize += childISize;
      if (!onFirstChild) {
        containerISize += mainGapSize;
      }
      onFirstChild = false;
    } else {  // (col-oriented, or MinISize for multi-line row flex container)
      containerISize = std::max(containerISize, childISize);
    }
  }

  return containerISize;
}

nscoord nsFlexContainerFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                             IntrinsicISizeType aType) {
  return mCachedIntrinsicSizes.GetOrSet(*this, aType, aInput, [&] {
    return ComputeIntrinsicISize(aInput, aType);
  });
}

int32_t nsFlexContainerFrame::GetNumLines() const {
  // TODO(emilio, bug 1793251): Treating all row oriented frames as single-lines
  // might not be great for flex-wrap'd containers, consider trying to do
  // better? We probably would need to persist more stuff than we do after
  // layout.
  return FlexboxAxisInfo(this).mIsRowOriented ? 1 : mFrames.GetLength();
}

bool nsFlexContainerFrame::IsLineIteratorFlowRTL() {
  FlexboxAxisInfo info(this);
  if (info.mIsRowOriented) {
    const bool isRtl = StyleVisibility()->mDirection == StyleDirection::Rtl;
    return info.mIsMainAxisReversed != isRtl;
  }
  return false;
}

Result<nsILineIterator::LineInfo, nsresult> nsFlexContainerFrame::GetLine(
    int32_t aLineNumber) {
  if (aLineNumber < 0 || aLineNumber >= GetNumLines()) {
    return Err(NS_ERROR_FAILURE);
  }
  FlexboxAxisInfo info(this);
  LineInfo lineInfo;
  if (info.mIsRowOriented) {
    lineInfo.mLineBounds = GetRect();
    lineInfo.mFirstFrameOnLine = mFrames.FirstChild();
    // This isn't quite ideal for multi-line row flexbox, see bug 1793251.
    lineInfo.mNumFramesOnLine = mFrames.GetLength();
  } else {
    // TODO(emilio, bug 1793322): Deal with column-reverse (mIsMainAxisReversed)
    nsIFrame* f = mFrames.FrameAt(aLineNumber);
    lineInfo.mLineBounds = f->GetRect();
    lineInfo.mFirstFrameOnLine = f;
    lineInfo.mNumFramesOnLine = 1;
  }
  return lineInfo;
}

int32_t nsFlexContainerFrame::FindLineContaining(const nsIFrame* aFrame,
                                                 int32_t aStartLine) {
  const int32_t index = mFrames.IndexOf(aFrame);
  if (index < 0) {
    return -1;
  }
  const FlexboxAxisInfo info(this);
  if (info.mIsRowOriented) {
    return 0;
  }
  if (index < aStartLine) {
    return -1;
  }
  return index;
}

NS_IMETHODIMP
nsFlexContainerFrame::CheckLineOrder(int32_t aLine, bool* aIsReordered,
                                     nsIFrame** aFirstVisual,
                                     nsIFrame** aLastVisual) {
  *aIsReordered = false;
  *aFirstVisual = nullptr;
  *aLastVisual = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsFlexContainerFrame::FindFrameAt(int32_t aLineNumber, nsPoint aPos,
                                  nsIFrame** aFrameFound,
                                  bool* aPosIsBeforeFirstFrame,
                                  bool* aPosIsAfterLastFrame) {
  const auto wm = GetWritingMode();
  const LogicalPoint pos(wm, aPos, GetSize());
  const FlexboxAxisInfo info(this);

  *aFrameFound = nullptr;
  *aPosIsBeforeFirstFrame = true;
  *aPosIsAfterLastFrame = false;

  if (!info.mIsRowOriented) {
    nsIFrame* f = mFrames.FrameAt(aLineNumber);
    if (!f) {
      return NS_OK;
    }

    auto rect = f->GetLogicalRect(wm, GetSize());
    *aFrameFound = f;
    *aPosIsBeforeFirstFrame = pos.I(wm) < rect.IStart(wm);
    *aPosIsAfterLastFrame = pos.I(wm) > rect.IEnd(wm);
    return NS_OK;
  }

  LineFrameFinder finder(aPos, GetSize(), GetWritingMode(),
                         IsLineIteratorFlowRTL());
  for (nsIFrame* f : mFrames) {
    finder.Scan(f);
    if (finder.IsDone()) {
      break;
    }
  }
  finder.Finish(aFrameFound, aPosIsBeforeFirstFrame, aPosIsAfterLastFrame);
  return NS_OK;
}

void nsFlexContainerFrame::MaybePropagateRelativeBSizeFlagFrom(
    const FlexItem& aItem) {
  const auto* itemFrame = aItem.Frame();
  if (!itemFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
    return;
  }

  if (HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE) || !IsFlexItem()) {
    // No additional action required, `NeedsFinalReflow` should handle it
    // correctly.
    return;
  }

  if (!aItem.TreatBSizeAsIndefinite()) {
    // The item's block-size is already definite, so its descendants with
    // relative block-size won't depend on our block-size becoming definite
    // from our parent's final reflow.
    return;
  }

  // At this point, our block size may become definite from our parent flex
  // container's final reflow. However, parent container will not examine our
  // children for `NS_FRAME_CONTAINS_RELATIVE_BSIZE`, so we need propagate it
  // up the chain.
  AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
}
