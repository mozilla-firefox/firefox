/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFieldSetFrame.h"

#include <algorithm>

#include "gfxContext.h"
#include "mozilla/Baseline.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/dom/HTMLLegendElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsBlockFrame.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsStyleConsts.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layout;
using image::ImgDrawResult;

nsContainerFrame* NS_NewFieldSetFrame(PresShell* aPresShell,
                                      ComputedStyle* aStyle) {
  return new nsFieldSetFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsFieldSetFrame)
NS_QUERYFRAME_HEAD(nsFieldSetFrame)
  NS_QUERYFRAME_ENTRY(nsFieldSetFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsFieldSetFrame::nsFieldSetFrame(ComputedStyle* aStyle,
                                 nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID),
      mLegendRect(GetWritingMode()) {
  mLegendSpace = 0;
}

nsRect nsFieldSetFrame::VisualBorderRectRelativeToSelf() const {
  WritingMode wm = GetWritingMode();
  LogicalRect r(wm, LogicalPoint(wm, 0, 0), GetLogicalSize(wm));
  nsSize containerSize = r.Size(wm).GetPhysicalSize(wm);
  nsIFrame* legend = GetLegend();
  if (legend && !GetPrevInFlow()) {
    nscoord legendSize = legend->GetLogicalSize(wm).BSize(wm);
    auto legendMargin = legend->GetLogicalUsedMargin(wm);
    nscoord legendStartMargin = legendMargin.BStart(wm);
    nscoord legendEndMargin = legendMargin.BEnd(wm);
    nscoord border = GetUsedBorder().Side(wm.PhysicalSide(LogicalSide::BStart));
    // Calculate the offset from the border area block-axis start edge needed to
    // center-align our border with the legend's border-box (in the block-axis).
    nscoord off = (legendStartMargin + legendSize / 2) - border / 2;
    // We don't want to display our border above our border area.
    if (off > nscoord(0)) {
      nscoord marginBoxSize = legendStartMargin + legendSize + legendEndMargin;
      if (marginBoxSize > border) {
        // We don't want to display our border below the legend's margin-box,
        // so we align it to the block-axis end if that happens.
        nscoord overflow = off + border - marginBoxSize;
        if (overflow > nscoord(0)) {
          off -= overflow;
        }
        r.BStart(wm) += off;
        r.BSize(wm) -= off;
      }
    }
  }
  return r.GetPhysicalRect(wm, containerSize);
}

nsContainerFrame* nsFieldSetFrame::GetInner() const {
  for (nsIFrame* child : mFrames) {
    if (child->Style()->GetPseudoType() ==
        PseudoStyleType::MozFieldsetContent) {
      return static_cast<nsContainerFrame*>(child);
    }
  }
  return nullptr;
}

nsIFrame* nsFieldSetFrame::GetLegend() const {
  for (nsIFrame* child : mFrames) {
    if (child->Style()->GetPseudoType() !=
        PseudoStyleType::MozFieldsetContent) {
      return child;
    }
  }
  return nullptr;
}

namespace mozilla {

class nsDisplayFieldSetBorder final : public nsPaintedDisplayItem {
 public:
  nsDisplayFieldSetBorder(nsDisplayListBuilder* aBuilder,
                          nsFieldSetFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayFieldSetBorder);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayFieldSetBorder)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
  bool CreateWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override;
  virtual nsRect GetBounds(nsDisplayListBuilder* aBuilder,
                           bool* aSnap) const override;
  NS_DISPLAY_DECL_NAME("FieldSetBorder", TYPE_FIELDSET_BORDER_BACKGROUND)
};

void nsDisplayFieldSetBorder::Paint(nsDisplayListBuilder* aBuilder,
                                    gfxContext* aCtx) {
  (void)static_cast<nsFieldSetFrame*>(mFrame)->PaintBorder(
      aBuilder, *aCtx, ToReferenceFrame(), GetPaintRect(aBuilder, aCtx));
}

nsRect nsDisplayFieldSetBorder::GetBounds(nsDisplayListBuilder* aBuilder,
                                          bool* aSnap) const {
  // Just go ahead and claim our frame's overflow rect as the bounds, because we
  // may have border-image-outset or other features that cause borders to extend
  // outside the border rect.  We could try to duplicate all the complexity
  // nsDisplayBorder has here, but keeping things in sync would be a pain, and
  // this code is not typically performance-sensitive.
  *aSnap = false;
  return Frame()->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
}

bool nsDisplayFieldSetBorder::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  auto frame = static_cast<nsFieldSetFrame*>(mFrame);
  auto offset = ToReferenceFrame();
  Maybe<wr::SpaceAndClipChainHelper> clipOut;

  nsRect rect = frame->VisualBorderRectRelativeToSelf() + offset;
  nsDisplayBoxShadowInner::CreateInsetBoxShadowWebRenderCommands(
      aBuilder, aSc, rect, mFrame, rect);

  if (nsIFrame* legend = frame->GetLegend()) {
    nsRect legendRect = legend->GetNormalRect() + offset;

    // Make sure we clip all of the border in case the legend is smaller.
    nscoord borderTopWidth = frame->GetUsedBorder().top;
    if (legendRect.height < borderTopWidth) {
      legendRect.height = borderTopWidth;
      legendRect.y = offset.y;
    }

    if (!legendRect.IsEmpty()) {
      // We need to clip out the part of the border where the legend would go
      auto appUnitsPerDevPixel = frame->PresContext()->AppUnitsPerDevPixel();
      auto layoutRect = wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(
          frame->InkOverflowRectRelativeToSelf() + offset,
          appUnitsPerDevPixel));

      wr::ComplexClipRegion region;
      region.rect = wr::ToLayoutRect(
          LayoutDeviceRect::FromAppUnits(legendRect, appUnitsPerDevPixel));
      region.mode = wr::ClipMode::ClipOut;
      region.radii = wr::EmptyBorderRadius();

      std::array<wr::WrClipId, 2> clips = {
          aBuilder.DefineRectClip(Nothing(), layoutRect),
          aBuilder.DefineRoundedRectClip(Nothing(), region),
      };
      auto clipChain = aBuilder.DefineClipChain(
          clips, aBuilder.CurrentClipChainIdIfNotRoot());
      clipOut.emplace(aBuilder, clipChain);
    }
  } else {
    rect = nsRect(offset, frame->GetRect().Size());
  }

  ImgDrawResult drawResult = nsCSSRendering::CreateWebRenderCommandsForBorder(
      this, mFrame, rect, aBuilder, aResources, aSc, aManager,
      aDisplayListBuilder);
  if (drawResult == ImgDrawResult::NOT_SUPPORTED) {
    return false;
  }
  return true;
};

}  // namespace mozilla

void nsFieldSetFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                       const nsDisplayListSet& aLists) {
  // Paint our background and border in a special way.
  // REVIEW: We don't really need to check frame emptiness here; if it's empty,
  // the background/border display item won't do anything, and if it isn't
  // empty, we need to paint the outline
  if (!HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER) &&
      IsVisibleForPainting()) {
    DisplayOutsetBoxShadowUnconditional(aBuilder, aLists.BorderBackground());

    const nsRect rect =
        VisualBorderRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);

    nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
        aBuilder, this, rect, aLists.BorderBackground(),
        /* aAllowWillPaintBorderOptimization = */ false);

    aLists.BorderBackground()->AppendNewToTop<nsDisplayFieldSetBorder>(aBuilder,
                                                                       this);

    DisplayOutlineUnconditional(aBuilder, aLists);

    DO_GLOBAL_REFLOW_COUNT_DSP("nsFieldSetFrame");
  }

  if (HidesContent()) {
    return;
  }

  if (GetPrevInFlow()) {
    DisplayOverflowContainers(aBuilder, aLists);
  }

  nsDisplayListCollection contentDisplayItems(aBuilder);
  if (nsIFrame* inner = GetInner()) {
    // Collect the inner frame's display items into their own collection.
    // We need to be calling BuildDisplayList on it before the legend in
    // case it contains out-of-flow frames whose placeholders are in the
    // legend. However, we want the inner frame's display items to be
    // after the legend's display items in z-order, so we need to save them
    // and append them later.
    BuildDisplayListForChild(aBuilder, inner, contentDisplayItems);
  }
  if (nsIFrame* legend = GetLegend()) {
    // The legend's background goes on our BlockBorderBackgrounds list because
    // it's a block child.
    nsDisplayListSet set(aLists, aLists.BlockBorderBackgrounds());
    BuildDisplayListForChild(aBuilder, legend, set);
  }

  if (GetPrevInFlow() || GetNextInFlow()) {
    DisplayAbsoluteFramesNotBuiltByPlaceholder(aBuilder, aLists);
  }

  // Put the inner frame's display items on the master list. Note that this
  // moves its border/background display items to our BorderBackground() list,
  // which isn't really correct, but it's OK because the inner frame is
  // anonymous and can't have its own border and background.
  contentDisplayItems.MoveTo(aLists);
}

ImgDrawResult nsFieldSetFrame::PaintBorder(nsDisplayListBuilder* aBuilder,
                                           gfxContext& aRenderingContext,
                                           nsPoint aPt,
                                           const nsRect& aDirtyRect) {
  // If the border is smaller than the legend, move the border down
  // to be centered on the legend.  We call VisualBorderRectRelativeToSelf() to
  // compute the border positioning.
  // FIXME: This means border-radius clamping is incorrect; we should
  // override nsIFrame::GetBorderRadii.
  nsRect rect = VisualBorderRectRelativeToSelf() + aPt;
  nsPresContext* presContext = PresContext();

  const auto skipSides = GetSkipSides();
  PaintBorderFlags borderFlags = aBuilder->ShouldSyncDecodeImages()
                                     ? PaintBorderFlags::SyncDecodeImages
                                     : PaintBorderFlags();

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  nsCSSRendering::PaintBoxShadowInner(presContext, aRenderingContext, this,
                                      rect);

  if (nsIFrame* legend = GetLegend()) {
    // We want to avoid drawing our border under the legend, so clip out the
    // legend while drawing our border.  We don't want to use mLegendRect here,
    // because we do want to draw our border under the legend's inline-start and
    // -end margins.  And we use GetNormalRect(), not GetRect(), because we do
    // not want relative positioning applied to the legend to change how our
    // border looks.
    nsRect legendRect = legend->GetNormalRect() + aPt;

    // Make sure we clip all of the border in case the legend is smaller.
    nscoord borderTopWidth = GetUsedBorder().top;
    if (legendRect.height < borderTopWidth) {
      legendRect.height = borderTopWidth;
      legendRect.y = aPt.y;
    }

    DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();
    // We set up a clip path which has our rect clockwise and the legend rect
    // counterclockwise, with FILL_WINDING as the fill rule.  That will allow us
    // to paint within our rect but outside the legend rect.  For "our rect" we
    // use our ink overflow rect (relative to ourselves, so it's not affected
    // by transforms), because we can have borders sticking outside our border
    // box (e.g. due to border-image-outset).
    RefPtr<PathBuilder> pathBuilder =
        drawTarget->CreatePathBuilder(FillRule::FILL_WINDING);
    int32_t appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
    AppendRectToPath(pathBuilder,
                     NSRectToSnappedRect(InkOverflowRectRelativeToSelf() + aPt,
                                         appUnitsPerDevPixel, *drawTarget),
                     true);
    AppendRectToPath(
        pathBuilder,
        NSRectToSnappedRect(legendRect, appUnitsPerDevPixel, *drawTarget),
        false);
    RefPtr<Path> clipPath = pathBuilder->Finish();

    aRenderingContext.Save();
    aRenderingContext.Clip(clipPath);
    result &= nsCSSRendering::PaintBorder(presContext, aRenderingContext, this,
                                          aDirtyRect, rect, mComputedStyle,
                                          borderFlags, skipSides);
    aRenderingContext.Restore();
  } else {
    result &= nsCSSRendering::PaintBorder(
        presContext, aRenderingContext, this, aDirtyRect,
        nsRect(aPt, mRect.Size()), mComputedStyle, borderFlags, skipSides);
  }

  return result;
}

nscoord nsFieldSetFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                        IntrinsicISizeType aType) {
  // Both inner and legend are children, and if the fieldset is
  // size-contained they should not contribute to the intrinsic size.
  if (Maybe<nscoord> containISize = ContainIntrinsicISize()) {
    return *containISize;
  }

  nscoord legendWidth = 0;
  if (nsIFrame* legend = GetLegend()) {
    legendWidth =
        nsLayoutUtils::IntrinsicForContainer(aInput.mContext, legend, aType);
  }

  nscoord contentWidth = 0;
  if (nsIFrame* inner = GetInner()) {
    // Ignore padding on the inner, since the padding will be applied to the
    // outer instead, and the padding computed for the inner is wrong
    // for percentage padding.
    contentWidth = nsLayoutUtils::IntrinsicForContainer(
        aInput.mContext, inner, aType, aInput.mPercentageBasisForChildren,
        nsLayoutUtils::IGNORE_PADDING);
  }

  return std::max(legendWidth, contentWidth);
}

/* virtual */
void nsFieldSetFrame::Reflow(nsPresContext* aPresContext,
                             ReflowOutput& aDesiredSize,
                             const ReflowInput& aReflowInput,
                             nsReflowStatus& aStatus) {
  using LegendAlignValue = mozilla::dom::HTMLLegendElement::LegendAlignValue;

  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsFieldSetFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_WARNING_ASSERTION(aReflowInput.ComputedISize() != NS_UNCONSTRAINEDSIZE,
                       "Should have a precomputed inline-size!");

  OverflowAreas ocBounds;
  nsReflowStatus ocStatus;
  auto* prevInFlow = static_cast<nsFieldSetFrame*>(GetPrevInFlow());
  if (prevInFlow) {
    ReflowOverflowContainerChildren(aPresContext, aReflowInput, ocBounds,
                                    ReflowChildFlags::Default, ocStatus);

    AutoFrameListPtr prevOverflowFrames(prevInFlow->StealOverflowFrames());
    if (prevOverflowFrames) {
      mFrames.InsertFrames(this, nullptr, std::move(*prevOverflowFrames));
    }
  }

  bool reflowInner;
  bool reflowLegend;
  nsIFrame* legend = GetLegend();
  nsContainerFrame* inner = GetInner();
  if (!legend || !inner) {
    if (DrainSelfOverflowList()) {
      legend = GetLegend();
      inner = GetInner();
    }
  }
  if (aReflowInput.ShouldReflowAllKids() || GetNextInFlow() ||
      aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE) {
    reflowInner = inner != nullptr;
    reflowLegend = legend != nullptr;
  } else {
    reflowInner = inner && inner->IsSubtreeDirty();
    reflowLegend = legend && legend->IsSubtreeDirty();
  }

  // @note |this| frame applies borders but not any padding.  Our anonymous
  // inner frame applies the padding (but not borders).
  const auto wm = GetWritingMode();
  auto skipSides = PreReflowBlockLevelLogicalSkipSides();
  LogicalMargin border =
      aReflowInput.ComputedLogicalBorder(wm).ApplySkipSides(skipSides);
  LogicalSize availSize(wm, aReflowInput.ComputedSize().ISize(wm),
                        aReflowInput.AvailableBSize());

  // Figure out how big the legend is if there is one.
  LogicalMargin legendMargin(wm);
  Maybe<ReflowInput> legendReflowInput;
  if (legend) {
    const auto legendWM = legend->GetWritingMode();
    LogicalSize legendAvailSize = availSize.ConvertTo(legendWM, wm);
    ComputeSizeFlags sizeFlags;
    if (legend->StylePosition()
            ->ISize(wm, AnchorPosResolutionParams::From(legend))
            ->IsAuto()) {
      sizeFlags = ComputeSizeFlag::ShrinkWrap;
    }
    ReflowInput::InitFlags initFlags;  // intentionally empty
    StyleSizeOverrides sizeOverrides;  // intentionally empty
    legendReflowInput.emplace(aPresContext, aReflowInput, legend,
                              legendAvailSize, Nothing(), initFlags,
                              sizeOverrides, sizeFlags);
  }
  const bool avoidBreakInside = ShouldAvoidBreakInside(aReflowInput);
  if (reflowLegend) {
    ReflowOutput legendDesiredSize(aReflowInput);

    // We'll move the legend to its proper place later, so the position
    // and containerSize passed here are unimportant.
    const nsSize dummyContainerSize;
    ReflowChild(legend, aPresContext, legendDesiredSize, *legendReflowInput, wm,
                LogicalPoint(wm), dummyContainerSize,
                ReflowChildFlags::NoMoveFrame, aStatus);

    if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
        !(HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
          aReflowInput.mStyleDisplay->IsAbsolutelyPositionedStyle()) &&
        !prevInFlow && !aReflowInput.mFlags.mIsTopOfPage) {
      // Propagate break-before from the legend to the fieldset.
      if (legend->StyleDisplay()->BreakBefore() ||
          aStatus.IsInlineBreakBefore()) {
        aStatus.SetInlineLineBreakBeforeAndReset();
        return;
      }
      // Honor break-inside:avoid by breaking before instead.
      if (MOZ_UNLIKELY(avoidBreakInside) && !aStatus.IsFullyComplete()) {
        aStatus.SetInlineLineBreakBeforeAndReset();
        return;
      }
    }

    // Calculate the legend's margin-box rectangle.
    legendMargin = legend->GetLogicalUsedMargin(wm);
    mLegendRect = LogicalRect(
        wm, 0, 0, legendDesiredSize.ISize(wm) + legendMargin.IStartEnd(wm),
        legendDesiredSize.BSize(wm) + legendMargin.BStartEnd(wm));
    // We subtract mLegendSpace from inner's content-box block-size below.
    nscoord oldSpace = mLegendSpace;
    mLegendSpace = 0;
    nscoord borderBStart = border.BStart(wm);
    if (!prevInFlow) {
      if (mLegendRect.BSize(wm) > borderBStart) {
        mLegendSpace = mLegendRect.BSize(wm) - borderBStart;
      } else {
        // Calculate the border-box position that would center the legend's
        // border-box within the fieldset border:
        nscoord off = (borderBStart - legendDesiredSize.BSize(wm)) / 2;
        off -= legendMargin.BStart(wm);  // convert to a margin-box position
        if (off > nscoord(0)) {
          // Align the legend to the end if center-aligning it would overflow.
          nscoord overflow = off + mLegendRect.BSize(wm) - borderBStart;
          if (overflow > nscoord(0)) {
            off -= overflow;
          }
          mLegendRect.BStart(wm) += off;
        }
      }
    } else {
      mLegendSpace = mLegendRect.BSize(wm);
    }

    // If mLegendSpace changes then we need to reflow |inner| as well.
    if (mLegendSpace != oldSpace && inner) {
      reflowInner = true;
    }

    FinishReflowChild(legend, aPresContext, legendDesiredSize,
                      legendReflowInput.ptr(), wm, LogicalPoint(wm),
                      dummyContainerSize, ReflowChildFlags::NoMoveFrame);
    EnsureChildContinuation(legend, aStatus);
    if (aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
        !legend->GetWritingMode().IsOrthogonalTo(wm) &&
        legend->StyleDisplay()->BreakAfter() &&
        (!legendReflowInput->mFlags.mIsTopOfPage ||
         mLegendRect.BSize(wm) > 0) &&
        aStatus.IsComplete()) {
      // Pretend that we ran out of space to push children of |inner|.
      // XXX(mats) perhaps pushing the inner frame would be more correct,
      // but we don't support that yet.
      availSize.BSize(wm) = nscoord(0);
      aStatus.Reset();
      aStatus.SetIncomplete();
    }
  } else if (!legend) {
    mLegendRect.SetEmpty();
    mLegendSpace = 0;
  } else {
    // mLegendSpace and mLegendRect haven't changed, but we need
    // the used margin when placing the legend.
    legendMargin = legend->GetLogicalUsedMargin(wm);
  }

  // This containerSize is incomplete as yet: it does not include the size
  // of the |inner| frame itself.
  nsSize containerSize =
      (LogicalSize(wm, 0, mLegendSpace) + border.Size(wm)).GetPhysicalSize(wm);
  if (reflowInner) {
    LogicalSize innerAvailSize = availSize;
    innerAvailSize.ISize(wm) =
        aReflowInput.ComputedSizeWithPadding(wm).ISize(wm);
    nscoord remainingComputedBSize = aReflowInput.ComputedBSize();
    if (prevInFlow && remainingComputedBSize != NS_UNCONSTRAINEDSIZE) {
      // Subtract the consumed BSize associated with the legend.
      for (nsIFrame* prev = prevInFlow; prev; prev = prev->GetPrevInFlow()) {
        auto* prevFieldSet = static_cast<nsFieldSetFrame*>(prev);
        remainingComputedBSize -= prevFieldSet->mLegendSpace;
      }
      remainingComputedBSize = std::max(0, remainingComputedBSize);
    }
    if (innerAvailSize.BSize(wm) != NS_UNCONSTRAINEDSIZE) {
      innerAvailSize.BSize(wm) -=
          std::max(mLegendRect.BSize(wm), border.BStart(wm));
      if (StyleBorder()->mBoxDecorationBreak ==
              StyleBoxDecorationBreak::Clone &&
          (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE ||
           remainingComputedBSize +
                   aReflowInput.ComputedLogicalBorderPadding(wm).BStartEnd(
                       wm) >=
               availSize.BSize(wm))) {
        innerAvailSize.BSize(wm) -= border.BEnd(wm);
      }
      innerAvailSize.BSize(wm) = std::max(0, innerAvailSize.BSize(wm));
    }
    ReflowInput kidReflowInput(aPresContext, aReflowInput, inner,
                               innerAvailSize, Nothing(),
                               ReflowInput::InitFlag::CallerWillInit);
    // Override computed padding, in case it's percentage padding
    kidReflowInput.Init(
        aPresContext, Nothing(), Nothing(),
        Some(aReflowInput.ComputedLogicalPadding(inner->GetWritingMode())));

    // Propagate the aspect-ratio flag to |inner| (i.e. the container frame
    // wrapped by nsFieldSetFrame), so we can let |inner|'s reflow code handle
    // automatic content-based minimum.
    // Note: Init() resets this flag, so we have to copy it again here.
    if (aReflowInput.mFlags.mIsBSizeSetByAspectRatio) {
      kidReflowInput.mFlags.mIsBSizeSetByAspectRatio = true;
    }

    if (kidReflowInput.mFlags.mIsTopOfPage) {
      // Prevent break-before from |inner| if we have a legend.
      kidReflowInput.mFlags.mIsTopOfPage = !legend;
    }
    // Our child is "height:100%" but we actually want its height to be reduced
    // by the amount of content-height the legend is eating up, unless our
    // height is unconstrained (in which case the child's will be too).
    if (aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE) {
      kidReflowInput.SetComputedBSize(
          std::max(0, remainingComputedBSize - mLegendSpace));
    }

    if (aReflowInput.ComputedMinBSize() > 0) {
      kidReflowInput.SetComputedMinBSize(
          std::max(0, aReflowInput.ComputedMinBSize() - mLegendSpace));
    }

    if (aReflowInput.ComputedMaxBSize() != NS_UNCONSTRAINEDSIZE) {
      kidReflowInput.SetComputedMaxBSize(
          std::max(0, aReflowInput.ComputedMaxBSize() - mLegendSpace));
    }

    ReflowOutput kidDesiredSize(kidReflowInput);
    NS_ASSERTION(
        kidReflowInput.ComputedPhysicalMargin() == nsMargin(0, 0, 0, 0),
        "Margins on anonymous fieldset child not supported!");
    LogicalPoint pt(wm, border.IStart(wm), border.BStart(wm) + mLegendSpace);

    // We don't know the correct containerSize until we have reflowed |inner|,
    // so we use a dummy value for now; FinishReflowChild will fix the position
    // if necessary.
    const nsSize dummyContainerSize;
    nsReflowStatus status;
    // If our legend needs a continuation then *this* frame will have
    // a continuation as well so we should keep our inner frame continuations
    // too (even if 'inner' ends up being COMPLETE here).  This ensures that
    // our continuation will have a reasonable inline-size.
    ReflowChildFlags flags = aStatus.IsFullyComplete()
                                 ? ReflowChildFlags::Default
                                 : ReflowChildFlags::NoDeleteNextInFlowChild;
    ReflowChild(inner, aPresContext, kidDesiredSize, kidReflowInput, wm, pt,
                dummyContainerSize, flags, status);

    // Honor break-inside:avoid when possible by returning a BreakBefore status.
    if (MOZ_UNLIKELY(avoidBreakInside) && !prevInFlow &&
        !aReflowInput.mFlags.mIsTopOfPage &&
        availSize.BSize(wm) != NS_UNCONSTRAINEDSIZE) {
      if (status.IsInlineBreakBefore() || !status.IsFullyComplete()) {
        aStatus.SetInlineLineBreakBeforeAndReset();
        return;
      }
    }

    // Update containerSize to account for size of the inner frame, so that
    // FinishReflowChild can position it correctly.
    containerSize += kidDesiredSize.PhysicalSize();
    FinishReflowChild(inner, aPresContext, kidDesiredSize, &kidReflowInput, wm,
                      pt, containerSize, ReflowChildFlags::Default);
    EnsureChildContinuation(inner, status);
    aStatus.MergeCompletionStatusFrom(status);
    NS_FRAME_TRACE_REFLOW_OUT("FieldSet::Reflow", aStatus);
  } else if (inner) {
    // |inner| didn't need to be reflowed but we do need to include its size
    // in containerSize.
    containerSize += inner->GetSize();
  } else {
    // No |inner| means it was already complete in an earlier continuation.
    MOZ_ASSERT(prevInFlow, "first-in-flow should always have an inner frame");
    for (nsIFrame* prev = prevInFlow; prev; prev = prev->GetPrevInFlow()) {
      auto* prevFieldSet = static_cast<nsFieldSetFrame*>(prev);
      if (auto* prevInner = prevFieldSet->GetInner()) {
        containerSize += prevInner->GetSize();
        break;
      }
    }
  }

  LogicalRect contentRect(wm);
  if (inner) {
    // We don't support margins on inner, so our content rect is just the
    // inner's border-box. (We don't really care about container size at this
    // point, as we'll figure out the actual positioning later.)
    contentRect = inner->GetLogicalRect(wm, containerSize);
  } else if (prevInFlow) {
    auto size = prevInFlow->GetPaddingRectRelativeToSelf().Size();
    contentRect.ISize(wm) = wm.IsVertical() ? size.height : size.width;
  }

  if (legend) {
    // The legend is positioned inline-wards within the inner's content rect
    // (so that padding on the fieldset affects the legend position).
    LogicalRect innerContentRect = contentRect;
    innerContentRect.Deflate(wm, aReflowInput.ComputedLogicalPadding(wm));
    // If the inner content rect is larger than the legend, we can align the
    // legend.
    if (innerContentRect.ISize(wm) > mLegendRect.ISize(wm)) {
      // NOTE legend @align values are: left/right/center
      // GetLogicalAlign converts left/right to start/end for the given WM.
      // @see HTMLLegendElement::ParseAttribute/LogicalAlign
      auto* legendElement =
          dom::HTMLLegendElement::FromNode(legend->GetContent());
      switch (legendElement->LogicalAlign(wm)) {
        case LegendAlignValue::InlineEnd:
          mLegendRect.IStart(wm) =
              innerContentRect.IEnd(wm) - mLegendRect.ISize(wm);
          break;
        case LegendAlignValue::Center:
          // Note: rounding removed; there doesn't seem to be any need
          mLegendRect.IStart(wm) =
              innerContentRect.IStart(wm) +
              (innerContentRect.ISize(wm) - mLegendRect.ISize(wm)) / 2;
          break;
        case LegendAlignValue::InlineStart:
          mLegendRect.IStart(wm) = innerContentRect.IStart(wm);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("unexpected GetLogicalAlign value");
      }
    } else {
      // otherwise just start-align it.
      mLegendRect.IStart(wm) = innerContentRect.IStart(wm);
    }

    // place the legend
    LogicalRect actualLegendRect = mLegendRect;
    actualLegendRect.Deflate(wm, legendMargin);
    LogicalPoint actualLegendPos(actualLegendRect.Origin(wm));

    // Note that legend's writing mode may be different from the fieldset's,
    // so we need to convert offsets before applying them to it (bug 1134534).
    LogicalMargin offsets = legendReflowInput->ComputedLogicalOffsets(wm);
    ReflowInput::ApplyRelativePositioning(legend, wm, offsets, &actualLegendPos,
                                          containerSize);

    legend->SetPosition(wm, actualLegendPos, containerSize);
  }

  // Skip our block-end border if we're INCOMPLETE.
  if (!aStatus.IsComplete() &&
      StyleBorder()->mBoxDecorationBreak != StyleBoxDecorationBreak::Clone) {
    border.BEnd(wm) = nscoord(0);
  }

  // Return our size and our result.
  LogicalSize finalSize(
      wm, contentRect.ISize(wm) + border.IStartEnd(wm),
      mLegendSpace + border.BStartEnd(wm) + (inner ? inner->BSize(wm) : 0));
  if (Maybe<nscoord> containBSize =
          aReflowInput.mFrame->ContainIntrinsicBSize()) {
    // If we're size-contained in block axis, then we must set finalSize
    // according to contain-intrinsic-block-size, disregarding legend and inner.
    // Note: normally the fieldset's own padding (which we still must honor)
    // would be accounted for as part of inner's size (see kidReflowInput.Init()
    // call above).  So: since we're disregarding sizing information from
    // 'inner', we need to account for that padding ourselves here.
    nscoord contentBoxBSize =
        aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE
            ? aReflowInput.ApplyMinMaxBSize(*containBSize)
            : aReflowInput.ComputedBSize();
    finalSize.BSize(wm) =
        contentBoxBSize +
        aReflowInput.ComputedLogicalBorderPadding(wm).BStartEnd(wm);
  }

  if (aStatus.IsComplete() &&
      aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE &&
      finalSize.BSize(wm) > aReflowInput.AvailableBSize() &&
      border.BEnd(wm) > 0 && aReflowInput.AvailableBSize() > border.BEnd(wm)) {
    // Our end border doesn't fit but it should fit in the next column/page.
    if (MOZ_UNLIKELY(avoidBreakInside)) {
      aStatus.SetInlineLineBreakBeforeAndReset();
      return;
    } else {
      if (StyleBorder()->mBoxDecorationBreak ==
          StyleBoxDecorationBreak::Slice) {
        finalSize.BSize(wm) -= border.BEnd(wm);
      }
      aStatus.SetIncomplete();
    }
  }

  if (!aStatus.IsComplete()) {
    MOZ_ASSERT(aReflowInput.AvailableBSize() != NS_UNCONSTRAINEDSIZE,
               "must be Complete in an unconstrained available block-size");
    // Stretch our BSize to fill the fragmentainer.
    finalSize.BSize(wm) =
        std::max(finalSize.BSize(wm), aReflowInput.AvailableBSize());
  }
  aDesiredSize.SetSize(wm, finalSize);
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  if (legend) {
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, legend);
  }
  if (inner) {
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, inner);
  }

  // Merge overflow container bounds and status.
  aDesiredSize.mOverflowAreas.UnionWith(ocBounds);
  aStatus.MergeCompletionStatusFrom(ocStatus);

  FinishReflowWithAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput,
                                 aStatus);
  InvalidateFrame();
}

void nsFieldSetFrame::SetInitialChildList(ChildListID aListID,
                                          nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  MOZ_ASSERT(
      aListID != FrameChildListID::Principal || GetInner() || GetLegend(),
      "Setting principal child list should populate our inner frame "
      "or our rendered legend");
}

void nsFieldSetFrame::AppendFrames(ChildListID aListID,
                                   nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::NoReflowPrincipal &&
                 HasAnyStateBits(NS_FRAME_FIRST_REFLOW),
             "AppendFrames should only be used from "
             "nsCSSFrameConstructor::ConstructFieldSetFrame");
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
  MOZ_ASSERT(GetInner(), "at this point we should have an inner frame");
}

void nsFieldSetFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                   const nsLineList::iterator* aPrevFrameLine,
                                   nsFrameList&& aFrameList) {
  MOZ_ASSERT(
      aListID == FrameChildListID::Principal && !aPrevFrame && !GetLegend(),
      "InsertFrames should only be used to prepend a rendered legend "
      "from nsCSSFrameConstructor::ConstructFramesFromItemList");
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
  MOZ_ASSERT(GetLegend());
}

#ifdef DEBUG
void nsFieldSetFrame::RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) {
  MOZ_CRASH("nsFieldSetFrame::RemoveFrame not supported");
}
#endif

#ifdef ACCESSIBILITY
a11y::AccType nsFieldSetFrame::AccessibleType() {
  return a11y::eHTMLGroupboxType;
}
#endif

BaselineSharingGroup nsFieldSetFrame::GetDefaultBaselineSharingGroup() const {
  switch (StyleDisplay()->DisplayInside()) {
    case mozilla::StyleDisplayInside::Grid:
    case mozilla::StyleDisplayInside::Flex:
      return BaselineSharingGroup::First;
    default:
      return BaselineSharingGroup::Last;
  }
}

nscoord nsFieldSetFrame::SynthesizeFallbackBaseline(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup) const {
  return Baseline::SynthesizeBOffsetFromMarginBox(this, aWM, aBaselineGroup);
}

Maybe<nscoord> nsFieldSetFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  if (StyleDisplay()->IsContainLayout()) {
    // If we are layout-contained, our child 'inner' should not
    // affect how we calculate our baseline.
    return Nothing{};
  }
  nsIFrame* inner = GetInner();
  if (MOZ_UNLIKELY(!inner)) {
    return Nothing{};
  }
  MOZ_ASSERT(!inner->GetWritingMode().IsOrthogonalTo(aWM));
  const auto result =
      inner->GetNaturalBaselineBOffset(aWM, aBaselineGroup, aExportContext);
  if (!result) {
    return Nothing{};
  }
  nscoord innerBStart = inner->BStart(aWM, GetSize());
  if (aBaselineGroup == BaselineSharingGroup::First) {
    return Some(*result + innerBStart);
  }
  return Some(*result + BSize(aWM) - (innerBStart + inner->BSize(aWM)));
}

ScrollContainerFrame* nsFieldSetFrame::GetScrollTargetFrame() const {
  return do_QueryFrame(GetInner());
}

void nsFieldSetFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  if (nsIFrame* kid = GetInner()) {
    aResult.AppendElement(OwnedAnonBox(kid));
  }
}

void nsFieldSetFrame::EnsureChildContinuation(nsIFrame* aChild,
                                              const nsReflowStatus& aStatus) {
  MOZ_ASSERT(aChild == GetLegend() || aChild == GetInner(),
             "unexpected child frame");
  nsIFrame* nif = aChild->GetNextInFlow();
  if (aStatus.IsFullyComplete()) {
    if (nif) {
      // NOTE: we want to avoid our DEBUG version of RemoveFrame above.
      DestroyContext context(PresShell());
      nsContainerFrame::RemoveFrame(context,
                                    FrameChildListID::NoReflowPrincipal, nif);
      MOZ_ASSERT(!aChild->GetNextInFlow());
    }
  } else {
    nsFrameList nifs;
    if (!nif) {
      auto* fc = PresShell()->FrameConstructor();
      nif = fc->CreateContinuingFrame(aChild, this);
      if (aStatus.IsOverflowIncomplete()) {
        nif->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
      }
      nifs = nsFrameList(nif, nif);
    } else {
      // Steal all nifs and push them again in case they are currently on
      // the wrong list.
      for (nsIFrame* n = nif; n; n = n->GetNextInFlow()) {
        n->GetParent()->StealFrame(n);
        nifs.AppendFrame(this, n);
        if (aStatus.IsOverflowIncomplete()) {
          n->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
        } else {
          n->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
        }
      }
    }
    if (aStatus.IsOverflowIncomplete()) {
      if (nsFrameList* eoc = GetExcessOverflowContainers()) {
        eoc->AppendFrames(nullptr, std::move(nifs));
      } else {
        SetExcessOverflowContainers(std::move(nifs));
      }
    } else {
      if (nsFrameList* oc = GetOverflowFrames()) {
        oc->AppendFrames(nullptr, std::move(nifs));
      } else {
        SetOverflowFrames(std::move(nifs));
      }
    }
  }
}
