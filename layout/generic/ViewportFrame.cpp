/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object that is the root of the frame tree, which contains
 * the document's scrollbars and contains fixed-positioned elements
 */

#include "mozilla/ViewportFrame.h"

#include "MobileViewportManager.h"
#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/dom/ViewTransition.h"
#include "nsCanvasFrame.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nsPlaceholderFrame.h"
#include "nsSubDocumentFrame.h"

using namespace mozilla;

// ScrollContainerFrame can create two other wrap lists for scrollbars and such.
static constexpr uint16_t kFirstTopLayerIndex = 2;
enum class TopLayerIndex : uint16_t {
  // The content-accessible top layer (fullscreen, <dialog>, popover).
  Content = kFirstTopLayerIndex,
  // The view transitions and anonymous content top layer. View transitions need
  // to be separate from the content top layer, because the former needs to be
  // potentially captured by a view transition, but the later can't be
  // (otherwise it'd be cyclic).
  // The native anonymous content are for things like the one for DevTools
  // highlighters and other non-web-visible UI.
  ViewTransitionsAndAnonymousContent,
};

ViewportFrame* NS_NewViewportFrame(PresShell* aPresShell,
                                   ComputedStyle* aStyle) {
  return new ViewportFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(ViewportFrame)
NS_QUERYFRAME_HEAD(ViewportFrame)
  NS_QUERYFRAME_ENTRY(ViewportFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

void ViewportFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                         nsIFrame* aPrevInFlow) {
  nsContainerFrame::Init(aContent, aParent, aPrevInFlow);
  // No need to call CreateView() here - the frame ctor will call SetView()
  // with the ViewManager's root view, so we'll assign it in SetViewInternal().

  nsIFrame* parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(this);
  if (parent) {
    nsFrameState state = parent->GetStateBits();

    AddStateBits(state & (NS_FRAME_IN_POPUP));
  }
}

void ViewportFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                     const nsDisplayListSet& aLists) {
  AUTO_PROFILER_LABEL("ViewportFrame::BuildDisplayList",
                      GRAPHICS_DisplayListBuilding);

  nsIFrame* kid = mFrames.FirstChild();
  if (!kid) {
    return;
  }

  nsDisplayListCollection set(aBuilder);
  BuildDisplayListForChild(aBuilder, kid, set);

  // If we have a scrollframe then it takes care of creating the display list
  // for the top layer, but otherwise we need to do it here.
  if (!kid->IsScrollContainerFrame()) {
    bool isOpaque = false;
    if (auto* list = BuildDisplayListForContentTopLayer(aBuilder, &isOpaque)) {
      if (isOpaque) {
        set.DeleteAll(aBuilder);
      }
      set.PositionedDescendants()->AppendToTop(list);
    }
    if (auto* list =
            BuildDisplayListForViewTransitionsAndNACTopLayer(aBuilder)) {
      set.PositionedDescendants()->AppendToTop(list);
    }
  }

  set.MoveTo(aLists);
}

#ifdef DEBUG
// Returns whether we are going to put an element in the top layer for
// fullscreen. This function should matches the CSS rules in ua.css and xul.css.
static bool ShouldInTopLayerForFullscreen(dom::Element* aElement) {
  return !aElement->IsRootElement() &&
         !aElement->IsXULElement(nsGkAtoms::browser);
}
#endif  // DEBUG

static void BuildDisplayListForTopLayerFrame(nsDisplayListBuilder* aBuilder,
                                             nsIFrame* aFrame,
                                             nsDisplayList* aList) {
  nsRect visible;
  nsRect dirty;
  DisplayListClipState::AutoSaveRestore clipState(aBuilder);
  nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(aBuilder);
  if (auto* savedOutOfFlowData =
          nsDisplayListBuilder::GetOutOfFlowData(aFrame)) {
#ifdef DEBUG
    savedOutOfFlowData->CheckASR(aBuilder, aFrame);
#endif
    visible =
        savedOutOfFlowData->GetVisibleRectForFrame(aBuilder, aFrame, &dirty);
    // If we are in the top layer, our containing block is the viewport, which
    // can't be captured by a view transition on the same document itself.
    // Also, the top layer is painted from the root scrollframe, so that
    // already takes care of clearing the ASR / clip when captured.
    // TODO(emilio): We might need to clear the ASR / clip when coming from the
    // viewport (for chrome / XUL docs).
    if (!aBuilder->IsInViewTransitionCapture()) {
      // This function is called after we've finished building display items for
      // the root scroll frame. That means that the content clip from the root
      // scroll frame is no longer on aBuilder. However, we need to make sure
      // that the display items we build in this function have finite clipped
      // bounds with respect to the root ASR, so we restore the *combined clip*
      // that we saved earlier. The combined clip will include the clip from the
      // root scroll frame.
      clipState.SetClipChainForContainingBlockDescendants(
          savedOutOfFlowData->mCombinedClipChain);

      asrSetter.SetCurrentActiveScrolledRoot(
          DisplayPortUtils::GetASRForAbsPosFrame(
              aFrame, savedOutOfFlowData->mContainingBlockActiveScrolledRoot,
              aBuilder));
      asrSetter.SetCurrentScrollParentId(savedOutOfFlowData->mScrollParentId);
    }
  }

  nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
      aBuilder, aFrame, visible, dirty);

  nsDisplayList list;
  aFrame->BuildDisplayListForStackingContext(aBuilder, &list);
  aList->AppendToTop(&list);
}

static bool BackdropListIsOpaque(ViewportFrame* aFrame,
                                 nsDisplayListBuilder* aBuilder,
                                 nsDisplayList* aList) {
  // The common case for ::backdrop elements on the top layer is a single
  // fixed position container, holding an opaque background color covering
  // the whole viewport.
  if (aList->Length() != 1 ||
      aList->GetTop()->GetType() != DisplayItemType::TYPE_FIXED_POSITION) {
    return false;
  }

  // Make sure the fixed position container isn't clipped or scrollable.
  nsDisplayFixedPosition* fixed =
      static_cast<nsDisplayFixedPosition*>(aList->GetTop());
  if (fixed->GetActiveScrolledRoot() || fixed->GetClipChain()) {
    return false;
  }

  nsDisplayList* children = fixed->GetChildren();
  if (!children->GetTop() ||
      children->GetTop()->GetType() != DisplayItemType::TYPE_BACKGROUND_COLOR) {
    return false;
  }

  nsDisplayBackgroundColor* child =
      static_cast<nsDisplayBackgroundColor*>(children->GetTop());
  if (child->GetActiveScrolledRoot() || child->GetClipChain()) {
    return false;
  }

  // Check that the background color is both opaque, and covering the
  // whole viewport.
  bool dummy;
  nsRegion opaque = child->GetOpaqueRegion(aBuilder, &dummy);
  return opaque.Contains(aFrame->GetRect());
}

nsDisplayWrapList* ViewportFrame::MaybeWrapTopLayerList(
    nsDisplayListBuilder* aBuilder, uint16_t aIndex,
    nsDisplayList& aTopLayerList) {
  if (aTopLayerList.IsEmpty()) {
    return nullptr;
  }
  nsPoint offset = aBuilder->GetCurrentFrame()->GetOffsetTo(this);
  nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
      aBuilder, this, aBuilder->GetVisibleRect() + offset,
      aBuilder->GetDirtyRect() + offset);
  // Wrap the whole top layer in a single item with maximum z-index,
  // and append it at the very end, so that it stays at the topmost.
  nsDisplayWrapList* wrapList = MakeDisplayItemWithIndex<nsDisplayWrapper>(
      aBuilder, this, aIndex, &aTopLayerList, false);
  if (!wrapList) {
    return nullptr;
  }
  wrapList->SetOverrideZIndex(
      std::numeric_limits<decltype(wrapList->ZIndex())>::max());
  return wrapList;
}

nsDisplayWrapList* ViewportFrame::BuildDisplayListForContentTopLayer(
    nsDisplayListBuilder* aBuilder, bool* aIsOpaque) {
  if (aBuilder->AvoidBuildingDuplicateOofs()) {
    return nullptr;
  }

  nsDisplayList topLayerList;
  auto* doc = PresContext()->Document();

  nsTArray<dom::Element*> topLayer = doc->GetTopLayer();
  for (dom::Element* elem : topLayer) {
    nsIFrame* frame = elem->GetPrimaryFrame();
    if (!frame) {
      continue;
    }
    if (frame->IsHiddenByContentVisibilityOnAnyAncestor(
            nsIFrame::IncludeContentVisibility::Hidden)) {
      continue;
    }

    // There are two cases where an element in fullscreen is not in
    // the top layer:
    // 1. When building display list for purpose other than painting,
    //    it is possible that there is inconsistency between the style
    //    info and the content tree.
    // 2. This is an element which we are not going to put in the top
    //    layer for fullscreen. See ShouldInTopLayerForFullscreen().
    // In both cases, we want to skip the frame here and paint it in
    // the normal path.
    if (frame->StyleDisplay()->mTopLayer == StyleTopLayer::None) {
      MOZ_ASSERT(!aBuilder->IsForPainting() ||
                 !elem->State().HasState(dom::ElementState::FULLSCREEN) ||
                 !ShouldInTopLayerForFullscreen(elem));
      continue;
    }
    MOZ_ASSERT_IF(elem->State().HasState(dom::ElementState::FULLSCREEN),
                  ShouldInTopLayerForFullscreen(elem));
    // Inner SVG, MathML elements, as well as children of some XUL
    // elements are not allowed to be out-of-flow. They should not
    // be handled as top layer element here.
    if (!frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
      MOZ_ASSERT(!elem->GetParent()->IsHTMLElement(),
                 "HTML element should always be out-of-flow if in the top "
                 "layer");
      continue;
    }
    if (auto* backdropFrame = nsLayoutUtils::GetBackdropFrame(elem)) {
      BuildDisplayListForTopLayerFrame(aBuilder, backdropFrame, &topLayerList);
      if (aIsOpaque) {
        *aIsOpaque = BackdropListIsOpaque(this, aBuilder, &topLayerList);
      }
    }
    BuildDisplayListForTopLayerFrame(aBuilder, frame, &topLayerList);
  }

  return MaybeWrapTopLayerList(aBuilder, uint16_t(TopLayerIndex::Content),
                               topLayerList);
}

nsDisplayWrapList*
ViewportFrame::BuildDisplayListForViewTransitionsAndNACTopLayer(
    nsDisplayListBuilder* aBuilder) {
  if (aBuilder->AvoidBuildingDuplicateOofs()) {
    return nullptr;
  }

  nsDisplayList topLayerList;
  auto* doc = PresContext()->Document();
  if (dom::ViewTransition* vt = doc->GetActiveViewTransition()) {
    if (dom::Element* root = vt->GetSnapshotContainingBlock()) {
      if (nsIFrame* frame = root->GetPrimaryFrame()) {
        MOZ_ASSERT(frame->StyleDisplay()->mTopLayer != StyleTopLayer::None,
                   "the snapshot containing block should ensure this");
        MOZ_ASSERT(frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));
        BuildDisplayListForTopLayerFrame(aBuilder, frame, &topLayerList);
      }
    }
  }

  if (dom::Element* container = doc->GetCustomContentContainer()) {
    if (nsIFrame* frame = container->GetPrimaryFrame()) {
      MOZ_ASSERT(frame->StyleDisplay()->mTopLayer != StyleTopLayer::None,
                 "ua.css should ensure this");
      MOZ_ASSERT(frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));
      BuildDisplayListForTopLayerFrame(aBuilder, frame, &topLayerList);
    }
  }

  return MaybeWrapTopLayerList(
      aBuilder, uint16_t(TopLayerIndex::ViewTransitionsAndAnonymousContent),
      topLayerList);
}

#ifdef DEBUG
void ViewportFrame::AppendFrames(ChildListID aListID,
                                 nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");
  NS_ASSERTION(GetChildList(aListID).IsEmpty(), "Shouldn't have any kids!");
  nsContainerFrame::AppendFrames(aListID, std::move(aFrameList));
}

void ViewportFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                 const nsLineList::iterator* aPrevFrameLine,
                                 nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");
  NS_ASSERTION(GetChildList(aListID).IsEmpty(), "Shouldn't have any kids!");
  nsContainerFrame::InsertFrames(aListID, aPrevFrame, aPrevFrameLine,
                                 std::move(aFrameList));
}

void ViewportFrame::RemoveFrame(DestroyContext& aContext, ChildListID aListID,
                                nsIFrame* aOldFrame) {
  NS_ASSERTION(aListID == FrameChildListID::Principal, "unexpected child list");
  nsContainerFrame::RemoveFrame(aContext, aListID, aOldFrame);
}
#endif

void ViewportFrame::Destroy(DestroyContext& aContext) {
  if (PresShell()->IsDestroying()) {
    PresShell::ClearMouseCapture(this);
  }
  nsContainerFrame::Destroy(aContext);
}

nscoord ViewportFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                      IntrinsicISizeType aType) {
  return mFrames.IsEmpty()
             ? 0
             : mFrames.FirstChild()->IntrinsicISize(aInput, aType);
}

nsRect ViewportFrame::GetContainingBlockAdjustedForScrollbars(
    const ReflowInput& aReflowInput) const {
  const WritingMode wm = aReflowInput.GetWritingMode();

  LogicalSize computedSize = aReflowInput.ComputedSize();
  const nsPoint& origin = [&]() {
    // Get our prinicpal child frame and see if we're scrollable
    nsIFrame* kidFrame = mFrames.FirstChild();
    if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(kidFrame)) {
      // Note: In ReflowInput::CalculateHypotheticalPosition(), we exclude the
      // scrollbar or scrollbar-gutter area when computing the offset to
      // ViewportFrame. Ensure the code there remains in sync with the logic
      // here.
      LogicalMargin scrollbars(wm,
                               scrollContainerFrame->GetActualScrollbarSizes());
      computedSize.ISize(wm) =
          std::max(0, aReflowInput.ComputedISize() - scrollbars.IStartEnd(wm));
      computedSize.BSize(wm) =
          std::max(0, aReflowInput.ComputedBSize() - scrollbars.BStartEnd(wm));
      return nsPoint(scrollbars.Left(wm), scrollbars.Top(wm));
    }
    return nsPoint(0, 0);
  }();

  nsRect rect(origin, computedSize.GetPhysicalSize(wm));
  rect.SizeTo(AdjustViewportSizeForFixedPosition(rect));

  return rect;
}

void ViewportFrame::Reflow(nsPresContext* aPresContext,
                           ReflowOutput& aDesiredSize,
                           const ReflowInput& aReflowInput,
                           nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("ViewportFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE_REFLOW_IN("ViewportFrame::Reflow");

  // Because |Reflow| sets ComputedBSize() on the child to our
  // ComputedBSize().
  AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);

  // Set our size up front, since some parts of reflow depend on it
  // being already set.  Note that the computed height may be
  // unconstrained; that's ok.  Consumers should watch out for that.
  SetSize(aReflowInput.ComputedPhysicalSize());

  // Reflow the main content first so that the placeholders of the
  // fixed-position frames will be in the right places on an initial
  // reflow.
  nscoord kidBSize = 0;
  WritingMode wm = aReflowInput.GetWritingMode();

  if (mFrames.NotEmpty()) {
    // Deal with a non-incremental reflow or an incremental reflow
    // targeted at our one-and-only principal child frame.
    if (aReflowInput.ShouldReflowAllKids() ||
        mFrames.FirstChild()->IsSubtreeDirty()) {
      // Reflow our one-and-only principal child frame
      nsIFrame* kidFrame = mFrames.FirstChild();
      ReflowOutput kidDesiredSize(aReflowInput);
      const WritingMode kidWM = kidFrame->GetWritingMode();
      LogicalSize availableSpace = aReflowInput.AvailableSize(kidWM);
      ReflowInput kidReflowInput(aPresContext, aReflowInput, kidFrame,
                                 availableSpace);

      // Reflow the frame
      kidReflowInput.SetComputedBSize(aReflowInput.ComputedBSize());
      if (aReflowInput.IsBResizeForWM(kidWM)) {
        kidReflowInput.SetBResize(true);
      }
      if (aReflowInput.IsBResizeForPercentagesForWM(kidWM)) {
        kidReflowInput.SetBResizeForPercentages(true);
      }
      ReflowChild(kidFrame, aPresContext, kidDesiredSize, kidReflowInput, 0, 0,
                  ReflowChildFlags::Default, aStatus);
      kidBSize = kidDesiredSize.BSize(wm);

      FinishReflowChild(kidFrame, aPresContext, kidDesiredSize, &kidReflowInput,
                        0, 0, ReflowChildFlags::Default);
    } else {
      kidBSize = LogicalSize(wm, mFrames.FirstChild()->GetSize()).BSize(wm);
    }
  }

  NS_ASSERTION(aReflowInput.AvailableISize() != NS_UNCONSTRAINEDSIZE,
               "shouldn't happen anymore");

  // Return the max size as our desired size
  LogicalSize maxSize(wm, aReflowInput.AvailableISize(),
                      // Being flowed initially at an unconstrained block size
                      // means we should return our child's intrinsic size.
                      aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE
                          ? aReflowInput.ComputedBSize()
                          : kidBSize);
  aDesiredSize.SetSize(wm, maxSize);
  aDesiredSize.SetOverflowAreasToDesiredBounds();

  if (HasAbsolutelyPositionedChildren()) {
    // Make a copy of the reflow input and change the computed block size to
    // reflect the available space for the fixed items
    ReflowInput reflowInput(aReflowInput);

    if (reflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
      // We have an intrinsic-block-size document with abs-pos/fixed-pos
      // children. Set the available block-size and computed block-size to our
      // chosen block-size.
      reflowInput.SetAvailableBSize(maxSize.BSize(wm));
      // Not having border/padding simplifies things
      NS_ASSERTION(reflowInput.ComputedPhysicalBorderPadding() == nsMargin(),
                   "Viewports can't have border/padding");
      reflowInput.SetComputedBSize(maxSize.BSize(wm));
    }

    // The containing block for children. We intentionally not take scrollbar
    // size and dynamic toolbar into account because
    // ::-moz-snapshot-containing-block should include those areas.
    //
    // We will take them into account in AbsoluteContainingBlock::Reflow(),
    // for kid frames other than ::-moz-snapshot-containing-block.
    const nsRect cb(nsPoint(), reflowInput.ComputedPhysicalSize());
    // XXX: To optimize the performance, set the flags only when the CB width or
    // height actually changes.
    AbsPosReflowFlags flags{AbsPosReflowFlag::CBWidthChanged,
                            AbsPosReflowFlag::CBHeightChanged};
    nsReflowStatus absposStatus;
    GetAbsoluteContainingBlock()->Reflow(this, aPresContext, reflowInput,
                                         absposStatus, cb, flags,
                                         /* aOverflowAreas = */ nullptr);
    aStatus.MergeCompletionStatusFrom(absposStatus);
  }

  if (mFrames.NotEmpty()) {
    ConsiderChildOverflow(aDesiredSize.mOverflowAreas, mFrames.FirstChild());
  }

  // If we were dirty then do a repaint
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    InvalidateFrame();
  }

  // Clipping is handled by the document container (e.g., nsSubDocumentFrame),
  // so we don't need to change our overflow areas.
  FinishAndStoreOverflow(&aDesiredSize);

  NS_FRAME_TRACE_REFLOW_OUT("ViewportFrame::Reflow", aStatus);
}

void ViewportFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  if (mFrames.NotEmpty()) {
    aResult.AppendElement(mFrames.FirstChild());
  }
}

nsSize ViewportFrame::AdjustViewportSizeForFixedPosition(
    const nsRect& aViewportRect) const {
  nsSize result = aViewportRect.Size();

  mozilla::PresShell* presShell = PresShell();
  const nsSize fixedViewportSize = presShell->GetFixedViewportSize();
  if (result < fixedViewportSize) {
    result = fixedViewportSize;
  }

  return result;
}

#ifdef DEBUG_FRAME_DUMP
nsresult ViewportFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Viewport"_ns, aResult);
}
#endif
