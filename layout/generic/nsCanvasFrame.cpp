/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object that goes directly inside the document's scrollbars */

#include "nsCanvasFrame.h"

#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsContainerFrame.h"
#include "nsContentCreatorFunctions.h"
#include "nsDisplayList.h"
#include "nsFrameManager.h"
#include "nsGkAtoms.h"
#include "nsIFrameInlines.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::layout;
using namespace mozilla::gfx;
using namespace mozilla::layers;

nsCanvasFrame* NS_NewCanvasFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsCanvasFrame(aStyle, aPresShell->GetPresContext());
}

nsIPopupContainer* nsIPopupContainer::GetPopupContainer(PresShell* aPresShell) {
  return aPresShell ? aPresShell->GetCanvasFrame() : nullptr;
}

NS_IMPL_FRAMEARENA_HELPERS(nsCanvasFrame)

NS_QUERYFRAME_HEAD(nsCanvasFrame)
  NS_QUERYFRAME_ENTRY(nsCanvasFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
  NS_QUERYFRAME_ENTRY(nsIPopupContainer)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsresult nsCanvasFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  if (!mContent) {
    return NS_OK;
  }

  Document* doc = mContent->OwnerDoc();

  // Create a default tooltip element for system privileged documents.
  if (XRE_IsParentProcess() && doc->NodePrincipal()->IsSystemPrincipal()) {
    nsNodeInfoManager* nodeInfoManager = doc->NodeInfoManager();
    RefPtr<NodeInfo> nodeInfo = nodeInfoManager->GetNodeInfo(
        nsGkAtoms::tooltip, nullptr, kNameSpaceID_XUL, nsINode::ELEMENT_NODE);

    nsresult rv = NS_NewXULElement(getter_AddRefs(mTooltipContent),
                                   nodeInfo.forget(), dom::NOT_FROM_PARSER);
    NS_ENSURE_SUCCESS(rv, rv);

    mTooltipContent->SetAttr(kNameSpaceID_None, nsGkAtoms::_default, u"true"_ns,
                             false);
    // Set the page attribute so XULTooltipElement::PostHandleEvent will find
    // the text for the tooltip from the currently hovered element.
    mTooltipContent->SetAttr(kNameSpaceID_None, nsGkAtoms::page, u"true"_ns,
                             false);

    mTooltipContent->SetProperty(nsGkAtoms::docLevelNativeAnonymousContent,
                                 reinterpret_cast<void*>(true));

    aElements.AppendElement(mTooltipContent);
  }

#ifdef DEBUG
  for (auto& element : aElements) {
    MOZ_ASSERT(element.mContent->GetProperty(
                   nsGkAtoms::docLevelNativeAnonymousContent),
               "NAC from the canvas frame needs to be document-level, otherwise"
               " it (1) inherits from the document which is unexpected, and (2)"
               " StyleChildrenIterator won't be able to find it properly");
  }
#endif
  return NS_OK;
}

void nsCanvasFrame::AppendAnonymousContentTo(nsTArray<nsIContent*>& aElements,
                                             uint32_t aFilter) {
  if (mTooltipContent) {
    aElements.AppendElement(mTooltipContent);
  }
}

void nsCanvasFrame::Destroy(DestroyContext& aContext) {
  if (mTooltipContent) {
    aContext.AddAnonymousContent(mTooltipContent.forget());
  }
  nsContainerFrame::Destroy(aContext);
}

void nsCanvasFrame::SetInitialChildList(ChildListID aListID,
                                        nsFrameList&& aChildList) {
  // In printing, canvas frame's continuations may have multiple children in the
  // principal child list when nsCSSFrameConstructor::ReplicateFixedFrames
  // creates placeholders for fixed-pos elements.
  NS_ASSERTION(aListID != FrameChildListID::Principal || aChildList.IsEmpty() ||
                   aChildList.OnlyChild() || GetPrevInFlow(),
               "Principal child list of first-in-flow canvas frame can have at "
               "most one frame in it!");
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
}

void nsCanvasFrame::AppendFrames(ChildListID aListID,
                                 nsFrameList&& aFrameList) {
#ifdef DEBUG
  MOZ_ASSERT(aListID == FrameChildListID::Principal, "unexpected child list");
  if (!mFrames.IsEmpty()) {
    for (nsIFrame* f : aFrameList) {
      // We only allow native anonymous child frames to be in principal child
      // list in canvas frame.
      MOZ_ASSERT(f->GetContent()->IsInNativeAnonymousSubtree(),
                 "invalid child list");
    }
  }
  nsIFrame::VerifyDirtyBitSet(aFrameList);
#endif
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
}

void nsCanvasFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                 const nsLineList::iterator* aPrevFrameLine,
                                 nsFrameList&& aFrameList) {
  // Because we only support a single child frame inserting is the same
  // as appending
  MOZ_ASSERT(!aPrevFrame, "unexpected previous sibling frame");
  AppendFrames(aListID, std::move(aFrameList));
}

#ifdef DEBUG
void nsCanvasFrame::RemoveFrame(DestroyContext& aContext, ChildListID aListID,
                                nsIFrame* aOldFrame) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal, "unexpected child list");
  nsContainerFrame::RemoveFrame(aContext, aListID, aOldFrame);
}
#endif

nsRect nsCanvasFrame::CanvasArea() const {
  // Not clear which overflow rect we want here, but it probably doesn't
  // matter.
  nsRect result(InkOverflowRect());

  if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(GetParent())) {
    nsRect portRect = scrollContainerFrame->GetScrollPortRect();
    result.UnionRect(result, nsRect(nsPoint(0, 0), portRect.Size()));
  }
  return result;
}

Element* nsCanvasFrame::GetDefaultTooltip() { return mTooltipContent; }

void nsDisplayCanvasBackgroundImage::Paint(nsDisplayListBuilder* aBuilder,
                                           gfxContext* aCtx) {
  auto* frame = static_cast<nsCanvasFrame*>(mFrame);
  nsPoint offset = ToReferenceFrame();
  nsRect bgClipRect = frame->CanvasArea() + offset;

  PaintInternal(aBuilder, aCtx, GetPaintRect(aBuilder, aCtx), &bgClipRect);
}

bool nsDisplayCanvasBackgroundImage::IsSingleFixedPositionImage(
    nsDisplayListBuilder* aBuilder, const nsRect& aClipRect,
    gfxRect* aDestRect) {
  if (!mBackgroundStyle) {
    return false;
  }

  if (mBackgroundStyle->StyleBackground()->mImage.mLayers.Length() != 1) {
    return false;
  }

  nsPresContext* presContext = mFrame->PresContext();
  uint32_t flags = aBuilder->GetBackgroundPaintFlags();
  nsRect borderArea = nsRect(ToReferenceFrame(), mFrame->GetSize());
  const nsStyleImageLayers::Layer& layer =
      mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer];

  if (layer.mAttachment != StyleImageLayerAttachment::Fixed) {
    return false;
  }

  nsBackgroundLayerState state = nsCSSRendering::PrepareImageLayer(
      presContext, mFrame, flags, borderArea, aClipRect, layer);

  // We only care about images here, not gradients.
  if (!mIsRasterImage) {
    return false;
  }

  int32_t appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
  *aDestRect =
      nsLayoutUtils::RectToGfxRect(state.mFillArea, appUnitsPerDevPixel);

  return true;
}

void nsCanvasFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                     const nsDisplayListSet& aLists) {
  MOZ_ASSERT(IsVisibleForPainting(),
             "::-moz-{scrolled-,}canvas doesn't inherit from anything that can "
             "be invisible, and we don't specify visibility in UA sheets");
  MOZ_ASSERT(!IsThemed(),
             "::-moz-{scrolled-,}canvas doesn't have native appearance");
  if (GetPrevInFlow()) {
    DisplayOverflowContainers(aBuilder, aLists);
  }

  // Force a background to be shown. We may have a background propagated to us,
  // in which case StyleBackground wouldn't have the right background
  // and the code in nsIFrame::DisplayBorderBackgroundOutline might not give us
  // a background.
  // We don't have any border or outline, and our background draws over
  // the overflow area, so just add nsDisplayCanvasBackground instead of
  // calling DisplayBorderBackgroundOutline.
  ComputedStyle* bg = nullptr;
  nsIFrame* dependentFrame = nsCSSRendering::FindBackgroundFrame(this);
  if (dependentFrame) {
    bg = dependentFrame->Style();
    if (dependentFrame == this) {
      dependentFrame = nullptr;
    }
  }

  if (!bg) {
    return;
  }

  const ActiveScrolledRoot* asr = aBuilder->CurrentActiveScrolledRoot();

  bool needBlendContainerForBackgroundBlendMode = false;
  nsDisplayListBuilder::AutoContainerASRTracker contASRTracker(aBuilder);

  const bool suppressBackgroundImage = [&] {
    // Handle print settings.
    if (!ComputeShouldPaintBackground().mImage) {
      return true;
    }
    // In high-contrast-mode, we suppress background-image on the canvas frame
    // (even when backplating), because users expect site backgrounds to
    // conform to their HCM background color when a solid color is rendered,
    // and some websites use solid-color images instead of an overwritable
    // background color.
    if (PresContext()->ForcingColors() &&
        StaticPrefs::
            browser_display_suppress_canvas_background_image_on_forced_colors()) {
      return true;
    }
    return false;
  }();

  const bool isPage = GetParent()->IsPageContentFrame();
  const auto& canvasBg = PresShell()->GetCanvasBackground(isPage);

  // Note this list is important so that our blend container only captures our
  // own items.
  nsDisplayList list(aBuilder);

  // Put a scrolled background color item in place, at the bottom of the list.
  //
  // If the canvas background is specified by CSS, we must paint it. If it's
  // not, we don't need to paint it, but we still want to if we can without
  // compromising blending correctness.
  //
  // Painting this extra background used to be desirable for performance in the
  // FrameLayerBuilder era. It's unclear whether it still is (probably not), but
  // changing it causes a lot of fuzzy changes due to subpixel AA (not
  // necessarily regressions, tho?).
  //
  // NOTE(emilio): We used to have an optimization to try _not_ to draw it if
  // there was a fixed image (layers.mImageCount > 0 &&
  // layers.mLayers[0].mAttachment == StyleImageLayerAttachment::Fixed), but
  // it's unclear it was fully correct (didn't check for mix-blend-mode), and it
  // complicates quite a bit the logic. If it's useful for performance on real
  // world websites we could try to re-introduce it.
  nsDisplaySolidColor* backgroundColorItem = nullptr;
  if (NS_GET_A(canvasBg.mColor)) {
    // Note that if CSS didn't specify the background, it can't really be
    // semi-transparent.
    MOZ_ASSERT(
        canvasBg.mCSSSpecified || NS_GET_A(canvasBg.mColor) == 255,
        "Default canvas background should either be transparent or opaque");
    backgroundColorItem = MakeDisplayItem<nsDisplaySolidColor>(
        aBuilder, this,
        CanvasArea() + aBuilder->GetCurrentFrameOffsetToReferenceFrame(),
        canvasBg.mColor);
    list.AppendToTop(backgroundColorItem);
  }

  // Create separate items for each background layer.
  const nsStyleImageLayers& layers = bg->StyleBackground()->mImage;
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, layers) {
    if (layers.mLayers[i].mImage.IsNone() || suppressBackgroundImage) {
      continue;
    }

    nsRect bgRect = GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);

    const ActiveScrolledRoot* thisItemASR = asr;
    nsDisplayList thisItemList(aBuilder);
    nsDisplayBackgroundImage::InitData bgData =
        nsDisplayBackgroundImage::GetInitData(aBuilder, this, i, bgRect, bg);

    if (bgData.shouldFixToViewport) {
      auto* displayData = aBuilder->GetCurrentFixedBackgroundDisplayData();
      nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
          aBuilder, this, aBuilder->GetVisibleRect(), aBuilder->GetDirtyRect());

      DisplayListClipState::AutoSaveRestore clipState(aBuilder);
      nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
          aBuilder);
      if (displayData) {
        const nsPoint offset = GetOffsetTo(PresShell()->GetRootFrame());
        aBuilder->SetVisibleRect(displayData->mVisibleRect + offset);
        aBuilder->SetDirtyRect(displayData->mDirtyRect + offset);

        clipState.SetClipChainForContainingBlockDescendants(
            displayData->mContainingBlockClipChain);
        asrSetter.SetCurrentActiveScrolledRoot(
            displayData->mContainingBlockActiveScrolledRoot);
        asrSetter.SetCurrentScrollParentId(displayData->mScrollParentId);
        thisItemASR = displayData->mContainingBlockActiveScrolledRoot;
      }
      nsDisplayCanvasBackgroundImage* bgItem = nullptr;
      {
        DisplayListClipState::AutoSaveRestore bgImageClip(aBuilder);
        bgImageClip.Clear();
        bgItem = MakeDisplayItemWithIndex<nsDisplayCanvasBackgroundImage>(
            aBuilder, this, /* aIndex = */ i, bgData);
        if (bgItem) {
          bgItem->SetDependentFrame(aBuilder, dependentFrame);
        }
      }
      if (bgItem) {
        const ActiveScrolledRoot* scrollTargetASR =
            asr ? asr->GetNearestScrollASR() : nullptr;
        thisItemList.AppendToTop(
            nsDisplayFixedPosition::CreateForFixedBackground(
                aBuilder, this, nullptr, bgItem, i, scrollTargetASR));
      }

    } else {
      nsDisplayCanvasBackgroundImage* bgItem =
          MakeDisplayItemWithIndex<nsDisplayCanvasBackgroundImage>(
              aBuilder, this, /* aIndex = */ i, bgData);
      if (bgItem) {
        bgItem->SetDependentFrame(aBuilder, dependentFrame);
        thisItemList.AppendToTop(bgItem);
      }
    }

    if (layers.mLayers[i].mBlendMode != StyleBlend::Normal) {
      DisplayListClipState::AutoSaveRestore blendClip(aBuilder);
      thisItemList.AppendNewToTopWithIndex<nsDisplayBlendMode>(
          aBuilder, this, i + 1, &thisItemList, layers.mLayers[i].mBlendMode,
          thisItemASR, nsDisplayItem::ContainerASRType::Constant, true);
      needBlendContainerForBackgroundBlendMode = true;
    }
    list.AppendToTop(&thisItemList);
  }

  if (needBlendContainerForBackgroundBlendMode) {
    const ActiveScrolledRoot* containerASR = contASRTracker.GetContainerASR();
    DisplayListClipState::AutoSaveRestore blendContainerClip(aBuilder);
    list.AppendToTop(nsDisplayBlendContainer::CreateForBackgroundBlendMode(
        aBuilder, this, nullptr, &list, containerASR,
        nsDisplayItem::ContainerASRType::AncestorOfContained));
  }

  aLists.BorderBackground()->AppendToTop(&list);

  for (nsIFrame* kid : PrincipalChildList()) {
    // Put our child into its own pseudo-stack.
    BuildDisplayListForChild(aBuilder, kid, aLists);
  }

  if (GetPrevInFlow() || GetNextInFlow()) {
    DisplayAbsoluteFramesNotBuiltByPlaceholder(aBuilder, aLists);
  }

  if (!canvasBg.mCSSSpecified && backgroundColorItem &&
      (needBlendContainerForBackgroundBlendMode ||
       aBuilder->ContainsBlendMode())) {
    // We can't draw the scrolled canvas background without compromising
    // correctness, since the non-CSS-specified background is not supposed to be
    // part of the blend group. Suppress it by making it transparent.
    backgroundColorItem->OverrideColor(NS_TRANSPARENT);
  }
}

nscoord nsCanvasFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                      IntrinsicISizeType aType) {
  return mFrames.IsEmpty()
             ? 0
             : mFrames.FirstChild()->IntrinsicISize(aInput, aType);
}

void nsCanvasFrame::Reflow(nsPresContext* aPresContext,
                           ReflowOutput& aDesiredSize,
                           const ReflowInput& aReflowInput,
                           nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsCanvasFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE_REFLOW_IN("nsCanvasFrame::Reflow");

  auto* prevCanvasFrame = static_cast<nsCanvasFrame*>(GetPrevInFlow());
  if (prevCanvasFrame) {
    AutoFrameListPtr overflow(prevCanvasFrame->StealOverflowFrames());
    if (overflow) {
      NS_ASSERTION(overflow->OnlyChild(),
                   "must have doc root as canvas frame's only child");
      // Prepend overflow to the our child list. There may already be
      // children placeholders for fixed-pos elements, which don't get
      // reflowed but must not be lost until the canvas frame is destroyed.
      mFrames.InsertFrames(this, nullptr, std::move(*overflow));
    }
  }

  // Set our size up front, since some parts of reflow depend on it
  // being already set.  Note that the computed height may be
  // unconstrained; that's ok.  Consumers should watch out for that.
  SetSize(aReflowInput.ComputedPhysicalSize());

  // Reflow our children.  Typically, we only have one child - the root
  // element's frame or a placeholder for that frame, if the root element
  // is abs-pos or fixed-pos.  Note that this child might be missing though
  // if that frame was Complete in one of our earlier continuations.  This
  // happens when we create additional pages purely to make room for painting
  // overflow (painted by BuildPreviousPageOverflow in nsPageFrame.cpp).
  // We may have additional children which are placeholders for continuations
  // of fixed-pos content, see nsCSSFrameConstructor::ReplicateFixedFrames.
  const WritingMode wm = aReflowInput.GetWritingMode();
  aDesiredSize.SetSize(wm, aReflowInput.ComputedSize());
  if (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE) {
    // Set the block-size to zero for now in case we don't have any non-
    // placeholder children that would update the size in the loop below.
    aDesiredSize.BSize(wm) = nscoord(0);
  }
  aDesiredSize.SetOverflowAreasToDesiredBounds();
  nsIFrame* nextKid = nullptr;
  for (auto* kidFrame = mFrames.FirstChild(); kidFrame; kidFrame = nextKid) {
    nextKid = kidFrame->GetNextSibling();
    ReflowOutput kidDesiredSize(aReflowInput);
    bool kidDirty = kidFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY);
    WritingMode kidWM = kidFrame->GetWritingMode();
    auto availableSize = aReflowInput.AvailableSize(kidWM);
    nscoord bOffset = 0;
    nscoord canvasBSizeSum = 0;
    if (prevCanvasFrame && availableSize.BSize(kidWM) != NS_UNCONSTRAINEDSIZE &&
        !kidFrame->IsPlaceholderFrame() &&
        StaticPrefs::layout_display_list_improve_fragmentation()) {
      for (auto* pif = prevCanvasFrame; pif;
           pif = static_cast<nsCanvasFrame*>(pif->GetPrevInFlow())) {
        canvasBSizeSum += pif->BSize(kidWM);
        auto* pifChild = pif->PrincipalChildList().FirstChild();
        if (pifChild) {
          nscoord layoutOverflow = pifChild->BSize(kidWM) - canvasBSizeSum;
          // A negative value means that the :root frame does not fill
          // the canvas.  In this case we can't determine the offset exactly
          // so we use the end edge of the scrollable overflow as the offset
          // instead.  This will likely push down the content below where it
          // should be placed, creating a gap.  That's preferred over making
          // content overlap which would otherwise occur.
          // See layout/reftests/pagination/inline-block-slice-7.html for an
          // example of this.
          if (layoutOverflow < 0) {
            LogicalRect so(kidWM, pifChild->ScrollableOverflowRect(),
                           pifChild->GetSize());
            layoutOverflow = so.BEnd(kidWM) - canvasBSizeSum;
          }
          bOffset = std::max(bOffset, layoutOverflow);
        }
      }
      availableSize.BSize(kidWM) -= bOffset;
    }

    if (MOZ_LIKELY(availableSize.BSize(kidWM) > 0)) {
      ReflowInput kidReflowInput(aPresContext, aReflowInput, kidFrame,
                                 availableSize);

      if (aReflowInput.IsBResizeForWM(kidReflowInput.GetWritingMode()) &&
          kidFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
        // Tell our kid it's being block-dir resized too.  Bit of a
        // hack for framesets.
        kidReflowInput.SetBResize(true);
      }

      nsSize containerSize = aReflowInput.ComputedPhysicalSize();
      LogicalMargin margin = kidReflowInput.ComputedLogicalMargin(kidWM);
      LogicalPoint kidPt(kidWM, margin.IStart(kidWM), margin.BStart(kidWM));
      (kidWM.IsOrthogonalTo(wm) ? kidPt.I(kidWM) : kidPt.B(kidWM)) += bOffset;

      nsReflowStatus kidStatus;
      ReflowChild(kidFrame, aPresContext, kidDesiredSize, kidReflowInput, kidWM,
                  kidPt, containerSize, ReflowChildFlags::Default, kidStatus);

      FinishReflowChild(kidFrame, aPresContext, kidDesiredSize, &kidReflowInput,
                        kidWM, kidPt, containerSize,
                        ReflowChildFlags::ApplyRelativePositioning);

      if (!kidStatus.IsFullyComplete()) {
        nsIFrame* nextFrame = kidFrame->GetNextInFlow();
        NS_ASSERTION(nextFrame || kidStatus.NextInFlowNeedsReflow(),
                     "If it's incomplete and has no nif yet, it must flag a "
                     "nif reflow.");
        if (!nextFrame) {
          nextFrame = aPresContext->PresShell()
                          ->FrameConstructor()
                          ->CreateContinuingFrame(kidFrame, this);
          SetOverflowFrames(nsFrameList(nextFrame, nextFrame));
          // Root overflow containers will be normal children of
          // the canvas frame, but that's ok because there
          // aren't any other frames we need to isolate them from
          // during reflow.
        }
        if (kidStatus.IsOverflowIncomplete()) {
          nextFrame->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
        }
      }
      aStatus.MergeCompletionStatusFrom(kidStatus);

      // If the child frame was just inserted, then we're responsible for making
      // sure it repaints
      if (kidDirty) {
        // But we have a new child, which will affect our background, so
        // invalidate our whole rect.
        // Note: Even though we request to be sized to our child's size, our
        // scroll frame ensures that we are always the size of the viewport.
        // Also note: GetPosition() on a CanvasFrame is always going to return
        // (0, 0). We only want to invalidate GetRect() since Get*OverflowRect()
        // could also include overflow to our top and left (out of the viewport)
        // which doesn't need to be painted.
        nsIFrame* viewport = PresShell()->GetRootFrame();
        viewport->InvalidateFrame();
      }

      // Return our desired size. Normally it's what we're told, but sometimes
      // we can be given an unconstrained block-size (when a window is
      // sizing-to-content), and we should compute our desired block-size. This
      // is done by PresShell::ResizeReflow, when given the BSizeLimit flag.
      //
      // We do this here rather than at the viewport frame, because the canvas
      // is what draws the background, so it can extend a little bit more than
      // the real content without visual glitches, realistically.
      if (aReflowInput.ComputedBSize() == NS_UNCONSTRAINEDSIZE &&
          !kidFrame->IsPlaceholderFrame()) {
        LogicalSize finalSize = aReflowInput.ComputedSize();
        finalSize.BSize(wm) = nsPresContext::RoundUpAppUnitsToCSSPixel(
            kidFrame->GetLogicalSize(wm).BSize(wm) +
            kidReflowInput.ComputedLogicalMargin(wm).BStartEnd(wm));
        aDesiredSize.SetSize(wm, finalSize);
        aDesiredSize.SetOverflowAreasToDesiredBounds();
      }
      aDesiredSize.mOverflowAreas.UnionWith(kidDesiredSize.mOverflowAreas +
                                            kidFrame->GetPosition());
    } else if (kidFrame->IsPlaceholderFrame()) {
      // Placeholders always fit even if there's no available block-size left.
    } else {
      // This only occurs in paginated mode.  There is no available space on
      // this page due to reserving space for overflow from a previous page,
      // so we push our child to the next page.  Note that we can have some
      // placeholders for fixed pos. frames in mFrames too, so we need to be
      // careful to only push `kidFrame`.
      mFrames.RemoveFrame(kidFrame);
      SetOverflowFrames(nsFrameList(kidFrame, kidFrame));
      aStatus.SetIncomplete();
    }
  }

  if (prevCanvasFrame) {
    ReflowOverflowContainerChildren(aPresContext, aReflowInput,
                                    aDesiredSize.mOverflowAreas,
                                    ReflowChildFlags::Default, aStatus);
  }

  FinishReflowWithAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput,
                                 aStatus);

  NS_FRAME_TRACE_REFLOW_OUT("nsCanvasFrame::Reflow", aStatus);
}

nsIContent* nsCanvasFrame::GetExplicitEventTargetContent(
    const WidgetEvent* aEvent /* = nullptr */) const {
  if (nsIContent* content = nsIFrame::GetExplicitEventTargetContent(aEvent)) {
    return content;
  }
  if (const nsIFrame* kid = mFrames.FirstChild()) {
    return kid->GetExplicitEventTargetContent(aEvent);
  }
  return nullptr;
}

#ifdef DEBUG_FRAME_DUMP
nsresult nsCanvasFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Canvas"_ns, aResult);
}
#endif
