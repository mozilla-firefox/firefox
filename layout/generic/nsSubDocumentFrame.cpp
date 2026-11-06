/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object for replaced elements that contain a document, such
 * as <frame>, <iframe>, and some <object>s
 */

#include "nsSubDocumentFrame.h"

#include "RetainedDisplayListBuilder.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ImageDocument.h"
#include "mozilla/dom/RemoteBrowser.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"  // for StackingContextHelper
#include "mozilla/layers/WebRenderScrollData.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsDisplayList.h"
#include "nsFrameSetFrame.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIObjectLoadingContent.h"
#include "nsIWeakReferenceUtils.h"
#include "nsLayoutUtils.h"
#include "nsObjectLoadingContent.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsStyleConsts.h"
#include "nsStyleStruct.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;

static void PropagateIsUnderHiddenEmbedderElement(nsFrameLoader* aFrameLoader,
                                                  bool aValue) {
  if (!aFrameLoader) {
    return;
  }

  if (BrowsingContext* bc = aFrameLoader->GetExtantBrowsingContext()) {
    if (bc->IsUnderHiddenEmbedderElement() != aValue) {
      (void)bc->SetIsUnderHiddenEmbedderElement(aValue);
    }
  }
}

nsSubDocumentFrame::nsSubDocumentFrame(ComputedStyle* aStyle,
                                       nsPresContext* aPresContext)
    : nsAtomicContainerFrame(aStyle, aPresContext, kClassID),
      mIsInline(false),
      mPostedReflowCallback(false),
      mDidCreateDoc(false),
      mCallingShow(false),
      mIsInObjectOrEmbed(false) {}

#ifdef ACCESSIBILITY
a11y::AccType nsSubDocumentFrame::AccessibleType() {
  return a11y::eOuterDocType;
}
#endif

NS_QUERYFRAME_HEAD(nsSubDocumentFrame)
  NS_QUERYFRAME_ENTRY(nsSubDocumentFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsAtomicContainerFrame)

class AsyncFrameInit : public Runnable {
 public:
  explicit AsyncFrameInit(nsIFrame* aFrame)
      : mozilla::Runnable("AsyncFrameInit"), mFrame(aFrame) {}
  NS_IMETHOD Run() override {
    AUTO_PROFILER_LABEL("AsyncFrameInit::Run", OTHER);
    if (mFrame.IsAlive()) {
      static_cast<nsSubDocumentFrame*>(mFrame.GetFrame())->ShowViewer();
    }
    return NS_OK;
  }

 private:
  WeakFrame mFrame;
};

void nsSubDocumentFrame::EnsureEmbeddingPresShell(class PresShell* aPs) {
  MOZ_ASSERT(aPs);
  nsWeakPtr weakRef = do_GetWeakReference(aPs);
  if (!mInProcessPresShells.Contains(weakRef)) {
    aPs->SetInProcessEmbedderFrame(this);
    mInProcessPresShells.AppendElement(std::move(weakRef));
  }
}

void nsSubDocumentFrame::AddEmbeddingPresShell(class PresShell* aPs) {
  MOZ_ASSERT(aPs);
  nsWeakPtr weakRef = do_GetWeakReference(aPs);
  MOZ_ASSERT(!mInProcessPresShells.Contains(weakRef));
  aPs->SetInProcessEmbedderFrame(this);
  mInProcessPresShells.AppendElement(std::move(weakRef));
}

void nsSubDocumentFrame::RemoveEmbeddingPresShell(class PresShell* aPs) {
  MOZ_ASSERT(aPs);
  nsWeakPtr weakRef = do_GetWeakReference(aPs);
  MOZ_ASSERT(mInProcessPresShells.Contains(weakRef));
  aPs->SetInProcessEmbedderFrame(nullptr);
  if (mLastPaintedPresShell == weakRef) {
    mLastPaintedPresShell = nullptr;
  }
  mInProcessPresShells.RemoveElement(weakRef);
}

void nsSubDocumentFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                              nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(aContent);
  // determine if we are a <frame> or <iframe>
  mIsInline = !aContent->IsHTMLElement(nsGkAtoms::frame);

  nsAtomicContainerFrame::Init(aContent, aParent, aPrevInFlow);

  // The only case we expect an existing primary frame is if we're replicating
  // fixed-positioned frames on a paginated document. In that case we don't want
  // to mess around with the frameloader, or lose track of our real primary
  // frame. That matches what the frame constructor does for all other frames.
  MOZ_ASSERT_IF(aContent->GetPrimaryFrame(),
                PresContext()->IsRootPaginatedDocument());
  if (MOZ_LIKELY(!aContent->GetPrimaryFrame())) {
    aContent->SetPrimaryFrame(this);
  }

  // If we have a detached subdoc's root view on our frame loader, re-insert it
  // into the view tree. This happens when we've been reframed, and ensures the
  // presentation persists across reframes.
  if (RefPtr<nsFrameLoader> frameloader = FrameLoader()) {
    mInProcessPresShells = frameloader->TakeDetachedSubdocs();
    const bool anyLiveShell = FixUpInProcessPresShellsAfterAttach();
    if (!mInProcessPresShells.IsEmpty() && !anyLiveShell) {
      mInProcessPresShells.Clear();
      // Presentation is for a different document, don't restore it.
      frameloader->Hide();
    }
  }

  // NOTE: The frame loader might not yet be initialized yet. If it's not, the
  // call in ShowViewer() should pick things up.
  UpdateEmbeddedBrowsingContextDependentData();
  nsContentUtils::AddScriptRunner(MakeAndAddRef<AsyncFrameInit>(this));
}

void nsSubDocumentFrame::UpdateEmbeddedBrowsingContextDependentData() {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  if (!mFrameLoader) {
    return;
  }
  BrowsingContext* bc = mFrameLoader->GetExtantBrowsingContext();
  if (!bc) {
    return;
  }
  mIsInObjectOrEmbed = bc->IsEmbedderTypeObjectOrEmbed();
  const bool isOrIsGoingToBePrimaryFrame =
      MOZ_LIKELY(IsPrimaryFrame() || !mContent->GetPrimaryFrame());
  if (!isOrIsGoingToBePrimaryFrame) {
    return;
  }
  MaybeUpdateRemoteStyle();
  MaybeUpdateEmbedderColorScheme();
  MaybeUpdateEmbedderScrollbarInset();
  MaybeUpdateEmbedderZoom();
  PropagateIsUnderHiddenEmbedderElement(
      PresShell()->IsUnderHiddenEmbedderElement() ||
      !StyleVisibility()->IsVisible());
}

void nsSubDocumentFrame::PropagateIsUnderHiddenEmbedderElement(bool aValue) {
  ::PropagateIsUnderHiddenEmbedderElement(mFrameLoader, aValue);
}

void nsSubDocumentFrame::ShowViewer() {
  if (mCallingShow) {
    return;
  }

  RefPtr<nsFrameLoader> frameloader = FrameLoader();
  if (!frameloader || frameloader->IsDead()) {
    return;
  }

  if (!frameloader->IsRemoteFrame() && !PresContext()->IsDynamic()) {
    // We let the printing code take care of loading the document and
    // initializing the shell.
    return;
  }
  AutoWeakFrame weakThis(this);
  mCallingShow = true;
  bool didCreateDoc = frameloader->Show(this);
  if (!weakThis.IsAlive()) {
    return;
  }
  mCallingShow = false;
  mDidCreateDoc = didCreateDoc;
  if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    frameloader->UpdatePositionAndSize(this);
  }
  if (!weakThis.IsAlive()) {
    return;
  }
  nsAutoScriptBlocker sb;
  UpdateEmbeddedBrowsingContextDependentData();
  InvalidateFrame();
}

Document* nsSubDocumentFrame::GetExtantSubdocument() {
  nsIDocShell* ds = GetExtantDocShell();
  return ds ? ds->GetExtantDocument() : nullptr;
}

mozilla::PresShell* nsSubDocumentFrame::GetSubdocumentPresShell() {
  Document* doc = GetExtantSubdocument();
  return doc ? doc->GetPresShell() : nullptr;
}

nsIFrame* nsSubDocumentFrame::GetSubdocumentRootFrame() {
  mozilla::PresShell* ps = GetSubdocumentPresShell();
  return ps ? ps->GetRootFrame() : nullptr;
}

mozilla::PresShell* nsSubDocumentFrame::GetSubdocumentPresShellForPainting(
    uint32_t aFlags) {
  mozilla::PresShell* ps = GetSubdocumentPresShell();
  if (ps) {
    if (auto* pc = ps->GetPresContext()) {
      if (pc->Type() == nsPresContext::eContext_Print &&
          pc->Type() != PresContext()->Type()) {
        // Don't paint from non-print to print frames.
        return nullptr;
      }
    }
    if (!ps->IsPaintingSuppressed() || (aFlags & IGNORE_PAINT_SUPPRESSION)) {
      return ps;
    }
  }
  // If painting is suppressed in the presshell or there's no presShell, we try
  // to look for a better presshell to use.
  if (StaticPrefs::layout_show_previous_page()) {
    RefPtr<mozilla::PresShell> old = do_QueryReferent(mLastPaintedPresShell);
    if (old && old->GetInProcessEmbedderFrame() == this) {
      return old;
    }
  }
  return ps;
}

nsRect nsSubDocumentFrame::GetDestRect() const {
  const nsRect rect = GetContent()->IsHTMLElement(nsGkAtoms::frame)
                          ? GetRectRelativeToSelf()
                          : GetContentRectRelativeToSelf();
  return GetDestRect(rect);
}

nsRect nsSubDocumentFrame::GetDestRect(const nsRect& aConstraintRect) const {
  // Adjust subdocument size, according to 'object-fit' and the subdocument's
  // intrinsic size and ratio.
  return nsLayoutUtils::ComputeObjectDestRect(
      aConstraintRect, ComputeIntrinsicSize(/* aIgnoreContainment = */ true),
      GetIntrinsicRatio(/* aIgnoreContainment = */ true), StylePosition());
}

LayoutDeviceIntSize nsSubDocumentFrame::GetInitialSubdocumentSize() const {
  if (RefPtr<nsFrameLoader> frameloader = FrameLoader()) {
    for (const auto& detachedShell : frameloader->GetDetachedSubdocs()) {
      if (RefPtr<mozilla::PresShell> ps = do_QueryReferent(detachedShell)) {
        if (nsPresContext* pc = ps->GetPresContext()) {
          return LayoutDeviceIntSize::FromAppUnitsRounded(
              pc->GetVisibleArea().Size(), pc->AppUnitsPerDevPixel());
        }
      }
    }
  }
  return LayoutDeviceIntSize::FromAppUnitsRounded(
      kFallbackIntrinsicSize, PresContext()->AppUnitsPerDevPixel());
}

LayoutDeviceIntSize nsSubDocumentFrame::GetSubdocumentSize() const {
  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    return GetInitialSubdocumentSize();
  }
  return LayoutDeviceIntSize::FromAppUnitsRounded(
      GetDestRect().Size(), PresContext()->AppUnitsPerDevPixel());
}

static void WrapBackgroundColorInOwnLayer(nsDisplayListBuilder* aBuilder,
                                          nsIFrame* aFrame,
                                          nsDisplayList* aList) {
  for (nsDisplayItem* item : aList->TakeItems()) {
    if (item->GetType() == DisplayItemType::TYPE_BACKGROUND_COLOR) {
      nsDisplayList tmpList;
      tmpList.AppendToTop(item);
      item = MakeDisplayItemWithIndex<nsDisplayOwnLayer>(
          aBuilder, aFrame, /* aIndex = */ nsDisplayOwnLayer::OwnLayerForSubdoc,
          &tmpList, aBuilder->CurrentActiveScrolledRoot(),
          nsDisplayItem::ContainerASRType::Constant,
          nsDisplayOwnLayerFlags::None, ScrollbarData{}, true, false);
    }
    aList->AppendToTop(item);
  }
}

void nsSubDocumentFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                          const nsDisplayListSet& aLists) {
  if (!IsVisibleForPainting()) {
    return;
  }

  const bool forEvents = aBuilder->IsForEventDelivery();
  if (forEvents && Style()->PointerEvents() == StylePointerEvents::None) {
    // If we are pointer-events:none then we don't need to HitTest background or
    // anything else.
    return;
  }

  nsFrameLoader* frameLoader = FrameLoader();
  const bool isRemoteFrame = frameLoader && frameLoader->IsRemoteFrame();

  nsDisplayListCollection decorations(aBuilder);
  DisplayBorderBackgroundOutline(aBuilder, decorations);
  if (isRemoteFrame) {
    // Wrap background colors of <iframe>s with remote subdocuments in their
    // own layer so we generate a ColorLayer. This is helpful for optimizing
    // compositing; we can skip compositing the ColorLayer when the
    // remote content is opaque.
    WrapBackgroundColorInOwnLayer(aBuilder, this,
                                  decorations.BorderBackground());
  }
  decorations.MoveTo(aLists);

  if (forEvents && !ContentReactsToPointerEvents()) {
    return;
  }

  if (HidesContent()) {
    return;
  }

  // If we're passing pointer events to children then we have to descend into
  // subdocuments no matter what, to determine which parts are transparent for
  // hit-testing or event regions.
  if (!aBuilder->GetDescendIntoSubdocuments()) {
    return;
  }

  if (isRemoteFrame) {
    // We're the subdoc for <browser remote="true"> and it has
    // painted content.  Display its shadow layer tree.
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    clipState.ClipContainingBlockDescendantsToContentBox(aBuilder, this);

    aLists.Content()->AppendNewToTop<nsDisplayRemote>(aBuilder, this);
    return;
  }

  RefPtr<mozilla::PresShell> presShell = GetSubdocumentPresShellForPainting(
      aBuilder->IsIgnoringPaintSuppression() ? IGNORE_PAINT_SUPPRESSION : 0);

  if (!presShell) {
    return;
  }

  if (aBuilder->IsForPainting() && !aBuilder->IsIgnoringPaintSuppression()) {
    mLastPaintedPresShell = do_GetWeakReference(presShell);
  }

  if (aBuilder->IsInFilter()) {
    Document* outerDoc = PresShell()->GetDocument();
    Document* innerDoc = presShell->GetDocument();
    if (outerDoc && innerDoc) {
      if (!outerDoc->NodePrincipal()->Equals(innerDoc->NodePrincipal())) {
        outerDoc->SetUseCounter(eUseCounter_custom_FilteredCrossOriginIFrame);
      }
    }
  }

  Document* subdoc = presShell->GetDocument();
  ViewTransition* vt = subdoc->GetActiveViewTransition();
  if (vt && aBuilder->IsPaintingToWindow() &&
      subdoc->RenderingSuppressedForViewTransitions() &&
      vt->IsHoldingSubdocument()) {
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    clipState.ClipContainingBlockDescendantsToContentBox(aBuilder, this);
    aLists.Content()
        ->AppendNewToTop<nsDisplayViewTransitionSubdocumentSnapshot>(
            aBuilder, this, vt->SubdocumentSnapshotRect());
    return;
  }

  nsIFrame* subdocRootFrame = presShell->GetRootFrame();

  nsPresContext* presContext = presShell->GetPresContext();

  int32_t parentAPD = PresContext()->AppUnitsPerDevPixel();
  int32_t subdocAPD = presContext->AppUnitsPerDevPixel();

  nsRect visible;
  nsRect dirty;
  bool ignoreViewportScrolling = false;
  if (subdocRootFrame) {
    // get the dirty rect relative to the root frame of the subdoc
    visible = aBuilder->GetVisibleRect() + GetOffsetToCrossDoc(subdocRootFrame);
    dirty = aBuilder->GetDirtyRect() + GetOffsetToCrossDoc(subdocRootFrame);
    // and convert into the appunits of the subdoc
    visible = visible.ScaleToOtherAppUnitsRoundOut(parentAPD, subdocAPD);
    dirty = dirty.ScaleToOtherAppUnitsRoundOut(parentAPD, subdocAPD);

    if (ScrollContainerFrame* sf = presShell->GetRootScrollContainerFrame()) {
      // Use a copy, so the rects don't get modified.
      nsRect copyOfDirty = dirty;
      nsRect copyOfVisible = visible;
      // TODO(botond): Can we just axe this DecideScrollableLayer call?
      sf->DecideScrollableLayer(aBuilder, &copyOfVisible, &copyOfDirty,
                                /* aSetBase = */ true);

      ignoreViewportScrolling = presShell->IgnoringViewportScrolling();
    }

    aBuilder->EnterPresShell(subdocRootFrame, !ContentReactsToPointerEvents());
    aBuilder->IncrementPresShellPaintCount(presShell);
  } else {
    visible = aBuilder->GetVisibleRect();
    dirty = aBuilder->GetDirtyRect();
  }

  DisplayListClipState::AutoSaveRestore clipState(aBuilder);
  clipState.ClipContainingBlockDescendantsToContentBox(aBuilder, this);

  ScrollContainerFrame* sf = presShell->GetRootScrollContainerFrame();
  bool constructZoomItem = subdocRootFrame && parentAPD != subdocAPD;
  bool needsOwnLayer = constructZoomItem ||
                       presContext->IsRootContentDocumentCrossProcess() ||
                       (sf && sf->IsScrollingActive());

  nsDisplayList childItems;

  if (subdocRootFrame) {
    DisplayListClipState::AutoSaveRestore nestedClipState(aBuilder);
    if (needsOwnLayer) {
      // Clear current clip. There's no point in propagating it down, since
      // the layer we will construct will be clipped by the current clip.
      // In fact for nsDisplayZoom propagating it down would be incorrect since
      // nsDisplayZoom changes the meaning of appunits.
      nestedClipState.Clear();
    }

    nsDisplayListBuilder::AutoBuildingDisplayList building(
        aBuilder, subdocRootFrame, visible, dirty);
    if (aBuilder->BuildCompositorHitTestInfo()) {
      bool hasDocumentLevelListenersForApzAwareEvents =
          gfxPlatform::AsyncPanZoomEnabled() &&
          nsLayoutUtils::HasDocumentLevelListenersForApzAwareEvents(presShell);

      aBuilder->SetAncestorHasApzAwareEventHandler(
          hasDocumentLevelListenersForApzAwareEvents);
    }
    subdocRootFrame->BuildDisplayListForStackingContext(aBuilder, &childItems);
    if (!aBuilder->IsForEventDelivery()) {
      // If we are going to use a displayzoom below then any items we put
      // under it need to have underlying frames from the subdocument. So we
      // need to calculate the bounds based on which frame will be the
      // underlying frame for the canvas background color item.
      nsRect bounds =
          GetContentRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
      bounds = bounds.ScaleToOtherAppUnitsRoundOut(parentAPD, subdocAPD);

      // Add the canvas background color to the bottom of the list. This
      // happens after we've built the list so that
      // AddCanvasBackgroundColorItem can monkey with the contents if
      // necessary.
      presShell->AddCanvasBackgroundColorItem(
          aBuilder, &childItems, subdocRootFrame, bounds, NS_RGBA(0, 0, 0, 0));
    }
  }

  if (subdocRootFrame) {
    aBuilder->LeavePresShell(subdocRootFrame, &childItems);
  }

  // Generate a resolution and/or zoom item if needed. If one or both of those
  // is created, we don't need to create a separate nsDisplaySubDocument.

  nsDisplayOwnLayerFlags flags =
      nsDisplayOwnLayerFlags::GenerateSubdocInvalidations;
  // If ignoreViewportScrolling is true then the top most layer we create here
  // is going to become the scrollable layer for the root scroll frame, so we
  // want to add nsDisplayOwnLayer::GENERATE_SCROLLABLE_LAYER to whatever layer
  // becomes the topmost. We do this below.
  if (constructZoomItem) {
    nsDisplayOwnLayerFlags zoomFlags = flags;
    if (ignoreViewportScrolling) {
      zoomFlags |= nsDisplayOwnLayerFlags::GenerateScrollableLayer;
    }
    childItems.AppendNewToTop<nsDisplayZoom>(aBuilder, subdocRootFrame, this,
                                             &childItems, subdocAPD, parentAPD,
                                             zoomFlags);

    needsOwnLayer = false;
  }
  // Wrap the zoom item in the resolution item if we have both because we want
  // the resolution scale applied on top of the app units per dev pixel
  // conversion.
  if (ignoreViewportScrolling) {
    flags |= nsDisplayOwnLayerFlags::GenerateScrollableLayer;
  }

  // We always want top level content documents to be in their own layer.
  nsDisplaySubDocument* layerItem = MakeDisplayItem<nsDisplaySubDocument>(
      aBuilder, subdocRootFrame ? subdocRootFrame : this, this, &childItems,
      flags);
  if (layerItem) {
    childItems.AppendToTop(layerItem);
    layerItem->SetShouldFlattenAway(!needsOwnLayer);
  }

  if (vt && vt->IsCapturingSubdocumentSnapshot() &&
      aBuilder->IsPaintingToWindow()) {
    // The iframe element can also be captured by a transition in its own
    // document, which wraps this frame in another
    // nsDisplayViewTransitionCapture, so this one needs a different index.
    childItems.AppendNewToTopWithIndex<nsDisplayViewTransitionCapture>(
        aBuilder, this, /* aIndex = */ 1, &childItems,
        aBuilder->CurrentActiveScrolledRoot(),
        nsDisplayViewTransitionCapture::Kind::Subdocument);
  }

  if (aBuilder->IsForFrameVisibility()) {
    // We don't add the childItems to the return list as we're dealing with them
    // here.
    presShell->RebuildApproximateFrameVisibilityDisplayList(childItems);
    childItems.DeleteAll(aBuilder);
  } else {
    aLists.Content()->AppendToTop(&childItems);
  }
}

#ifdef DEBUG_FRAME_DUMP
void nsSubDocumentFrame::List(FILE* out, const char* aPrefix,
                              ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);
  fprintf_stderr(out, "%s\n", str.get());

  if (aFlags.contains(ListFlag::TraverseSubdocumentFrames)) {
    nsSubDocumentFrame* f = const_cast<nsSubDocumentFrame*>(this);
    nsIFrame* subdocRootFrame = f->GetSubdocumentRootFrame();
    if (subdocRootFrame) {
      nsCString pfx(aPrefix);
      pfx += "  ";
      subdocRootFrame->List(out, pfx.get(), aFlags);
    }
  }
}

nsresult nsSubDocumentFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"FrameOuter"_ns, aResult);
}
#endif

nscoord nsSubDocumentFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  // Note: when computing max-content inline size (i.e. when aType is
  // IntrinsicISizeType::PrefISize), if the subdocument is an SVG document, then
  // in theory we want to return the same value that SVGOuterSVGFrame does. That
  // method has some special handling of percentage values to avoid unhelpful
  // zero sizing in the presence of orthogonal writing modes. We don't bother
  // with that for SVG documents in <embed> and <object>, since that special
  // handling doesn't look up across document boundaries anyway.
  return GetIntrinsicSize().ISize(GetWritingMode()).valueOr(0);
}

/* virtual */
IntrinsicSize nsSubDocumentFrame::GetIntrinsicSize() {
  return ComputeIntrinsicSize();
}

IntrinsicSize nsSubDocumentFrame::ComputeIntrinsicSize(
    bool aIgnoreContainment) const {
  const auto containAxes =
      aIgnoreContainment ? ContainSizeAxes(false, false) : GetContainSizeAxes();
  if (containAxes.IsBoth()) {
    // Intrinsic size of 'contain:size' replaced elements is determined by
    // contain-intrinsic-size.
    return FinishIntrinsicSize(containAxes, IntrinsicSize(0, 0));
  }

  if (nsCOMPtr<nsIObjectLoadingContent> iolc = do_QueryInterface(mContent)) {
    const auto* olc = static_cast<nsObjectLoadingContent*>(iolc.get());
    if (auto size = olc->GetSubdocumentIntrinsicSize()) {
      // Use the intrinsic size from the child SVG document, if available.
      return FinishIntrinsicSize(containAxes, *size);
    }
  }

  if (!IsInline()) {
    return {};  // <frame> elements have no useful intrinsic size.
  }

  if (mContent->IsXULElement()) {
    return {};  // XUL <iframe> and <browser> have no useful intrinsic size
  }

  // We must be an HTML <iframe>. Return fallback size.
  return FinishIntrinsicSize(containAxes,
                             IntrinsicSize(kFallbackIntrinsicSize));
}

/* virtual */
AspectRatio nsSubDocumentFrame::GetIntrinsicRatio(
    bool aIgnoreContainment) const {
  if (!aIgnoreContainment && GetContainSizeAxes().IsAny()) {
    return {};
  }
  if (nsCOMPtr<nsIObjectLoadingContent> iolc = do_QueryInterface(mContent)) {
    auto olc = static_cast<nsObjectLoadingContent*>(iolc.get());

    auto ratio = olc->GetSubdocumentIntrinsicRatio();
    if (ratio && *ratio) {
      // Use the intrinsic aspect ratio from the child SVG document, if
      // available.
      return *ratio;
    }
  }

  // NOTE(emilio): Even though we have an intrinsic size, we may not have an
  // intrinsic ratio. For example `<iframe style="width: 100px">` should not
  // shrink in the vertical axis to preserve the 300x150 ratio.
  return nsAtomicContainerFrame::GetIntrinsicRatio();
}

/* virtual */
nsIFrame::SizeComputationResult nsSubDocumentFrame::ComputeSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  return {ComputeSizeWithIntrinsicDimensions(
              aSizingInput.mRenderingContext, aWM, GetIntrinsicSize(),
              GetAspectRatio(), aCBSize, aMargin, aBorderPadding,
              aSizeOverrides, aFlags),
          AspectRatioUsage::None};
}

void nsSubDocumentFrame::Reflow(nsPresContext* aPresContext,
                                ReflowOutput& aDesiredSize,
                                const ReflowInput& aReflowInput,
                                nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsSubDocumentFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsSubDocumentFrame::Reflow: maxSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  NS_ASSERTION(aReflowInput.ComputedISize() != NS_UNCONSTRAINEDSIZE,
               "Shouldn't have unconstrained inline-size here "
               "thanks to the rules of reflow");
  NS_ASSERTION(aReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE,
               "Shouldn't have unconstrained block-size here "
               "thanks to ComputeAutoSize");

  NS_ASSERTION(IsPrimaryFrame() || PresContext()->IsRootPaginatedDocument(),
               "Shouldn't happen");

  // XUL <iframe> or <browser>, or HTML <iframe>, <object> or <embed>
  const auto wm = aReflowInput.GetWritingMode();
  aDesiredSize.SetSize(wm, aReflowInput.ComputedSizeWithBorderPadding(wm));

  if (nsCOMPtr<nsIDocShell> ds = GetExtantDocShell()) {
    const nsMargin& bp = aReflowInput.ComputedPhysicalBorderPadding();
    const nsRect innerRect(bp.left, bp.top,
                           aDesiredSize.Width() - bp.LeftRight(),
                           aDesiredSize.Height() - bp.TopBottom());

    // Size & position the view according to 'object-fit' & 'object-position'.
    const nsRect destRect = GetDestRect(innerRect);
    auto rect = LayoutDeviceIntRect::FromAppUnitsToInside(
        destRect, PresContext()->AppUnitsPerDevPixel());
    mExtraOffset = destRect.TopLeft();
    if (IsPrimaryFrame()) {
      nsDocShell::Cast(ds)->SetPositionAndSize(0, 0, rect.width, rect.height,
                                               nsIBaseWindow::eDelayResize);
    }
  }

  aDesiredSize.SetOverflowAreasToDesiredBounds();

  FinishAndStoreOverflow(&aDesiredSize);

  if (!aPresContext->IsRootPaginatedDocument() && !mPostedReflowCallback) {
    PresShell()->PostReflowCallback(this);
    mPostedReflowCallback = true;
  }

  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("exit nsSubDocumentFrame::Reflow: size=%d,%d status=%s",
       aDesiredSize.Width(), aDesiredSize.Height(), ToString(aStatus).c_str()));
}

bool nsSubDocumentFrame::ReflowFinished() {
  mPostedReflowCallback = false;
  nsFrameLoader* fl = FrameLoader();
  if (!fl) {
    return false;
  }
  if (fl->IsRemoteFrame() && fl->HasRemoteBrowserBeenSized()) {
    // For remote frames we don't need to update the size and position instantly
    // (but we should try to do so if we haven't shown it yet).
    return false;
  }
  RefPtr{fl}->UpdatePositionAndSize(this);
  return false;
}

void nsSubDocumentFrame::ReflowCallbackCanceled() {
  mPostedReflowCallback = false;
}

nsresult nsSubDocumentFrame::AttributeChanged(int32_t aNameSpaceID,
                                              nsAtom* aAttribute, AttrModType) {
  if (aNameSpaceID != kNameSpaceID_None) {
    return NS_OK;
  }

  // If the noResize attribute changes, dis/allow frame to be resized
  if (aAttribute == nsGkAtoms::noresize) {
    // Note that we're not doing content type checks, but that's ok -- if
    // they'd fail we will just end up with a null framesetFrame.
    if (mContent->GetParent()->IsHTMLElement(nsGkAtoms::frameset)) {
      nsIFrame* parentFrame = GetParent();

      if (parentFrame) {
        // There is no interface for nsHTMLFramesetFrame so QI'ing to
        // concrete class, yay!
        nsHTMLFramesetFrame* framesetFrame = do_QueryFrame(parentFrame);
        if (framesetFrame) {
          framesetFrame->RecalculateBorderResize();
        }
      }
    }
  } else if (aAttribute == nsGkAtoms::marginwidth ||
             aAttribute == nsGkAtoms::marginheight) {
    // Notify the frameloader
    if (RefPtr<nsFrameLoader> frameloader = FrameLoader()) {
      frameloader->MarginsChanged();
    }
  }

  return NS_OK;
}

void nsSubDocumentFrame::MaybeUpdateEmbedderColorScheme() {
  nsFrameLoader* fl = mFrameLoader.get();
  if (!fl) {
    return;
  }

  BrowsingContext* bc = fl->GetExtantBrowsingContext();
  if (!bc) {
    return;
  }

  auto ToOverride = [](ColorScheme aScheme) -> PrefersColorSchemeOverride {
    return aScheme == ColorScheme::Dark ? PrefersColorSchemeOverride::Dark
                                        : PrefersColorSchemeOverride::Light;
  };

  EmbedderColorSchemes schemes{
      ToOverride(LookAndFeel::ColorSchemeForFrame(this, ColorSchemeMode::Used)),
      ToOverride(
          LookAndFeel::ColorSchemeForFrame(this, ColorSchemeMode::Preferred))};
  if (bc->GetEmbedderColorSchemes() == schemes) {
    return;
  }

  (void)bc->SetEmbedderColorSchemes(schemes);
}

void nsSubDocumentFrame::MaybeUpdateEmbedderScrollbarInset() {
  nsFrameLoader* fl = mFrameLoader.get();
  if (!fl) {
    return;
  }

  BrowsingContext* bc = fl->GetExtantBrowsingContext();
  if (!bc) {
    return;
  }

  // The content document can't express this in CSS: its viewport scroll frame's
  // style is a non-inheriting anonymous box. Forward our own
  // -moz-scrollbar-inset-{block,inline} instead, resolved to physical sides in
  // our writing mode and converted to device pixels, since the subdocument's
  // writing mode and zoom are both its own.
  const WritingMode wm = GetWritingMode();
  nsPresContext* pc = PresContext();
  const nsMargin physical =
      StyleDisplay()->GetScrollbarInset(wm).GetPhysicalMargin(wm);

  const LayoutDeviceIntMargin inset = LayoutDevicePixel::FromAppUnitsRounded(
      physical, pc->AppUnitsPerDevPixel());
  if (bc->GetEmbedderScrollbarInset() == inset) {
    return;
  }

  (void)bc->SetEmbedderScrollbarInset(inset);
}

void nsSubDocumentFrame::MaybeUpdateEmbedderZoom() {
  nsFrameLoader* fl = mFrameLoader.get();
  if (!fl) {
    return;
  }

  BrowsingContext* bc = fl->GetExtantBrowsingContext();
  if (!bc) {
    return;
  }

  BrowsingContext* parent = bc->GetParent();
  if (!parent) {
    // Don't propagate zoom from the top browsing context, i.e. the browser
    // frontend. (If we did propagate at that level, we wouldn't be able to
    // reliably handle per-tab zoom levels, because the top level context's
    // propagated zoom level would replace the per-tab zoom levels.)
    return;
  }

  // The zoom that we propagate to our child document is parent's full-page-zoom
  // level, *combined with* the EffectiveZoom (i.e. the css 'zoom') of this
  // embedding frame.
  auto newZoom = Style()->EffectiveZoom().Zoom(parent->GetFullZoom());
  if (bc->GetFullZoom() == newZoom) {
    return;
  }
  (void)bc->SetFullZoom(newZoom);
}

void nsSubDocumentFrame::MaybeUpdateRemoteStyle(
    ComputedStyle* aOldComputedStyle) {
  if (!mIsInObjectOrEmbed) {
    return;
  }

  if (aOldComputedStyle &&
      aOldComputedStyle->StyleVisibility()->mImageRendering ==
          Style()->StyleVisibility()->mImageRendering) {
    return;
  }

  if (!mFrameLoader) {
    return;
  }

  if (mFrameLoader->IsRemoteFrame()) {
    mFrameLoader->UpdateRemoteStyle(
        Style()->StyleVisibility()->mImageRendering);
    return;
  }

  BrowsingContext* context = mFrameLoader->GetExtantBrowsingContext();
  if (!context) {
    return;
  }

  Document* document = context->GetDocument();
  if (!document) {
    return;
  }

  if (document->IsImageDocument()) {
    document->AsImageDocument()->UpdateRemoteStyle(
        Style()->StyleVisibility()->mImageRendering);
  }
}

void nsSubDocumentFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
  nsAtomicContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  if (aOldComputedStyle) {
    // If there's no old style, the call in Init() or ShowViewer() should have
    // us covered.
    MaybeUpdateEmbedderColorScheme();
    MaybeUpdateEmbedderScrollbarInset();
    MaybeUpdateRemoteStyle(aOldComputedStyle);
    if (aOldComputedStyle->EffectiveZoom() != Style()->EffectiveZoom()) {
      MaybeUpdateEmbedderZoom();
    }
  }

  // If this presshell has invisible ancestors, we don't need to propagate the
  // visibility style change to the subdocument since the subdocument should
  // have already set the IsUnderHiddenEmbedderElement flag in
  // nsSubDocumentFrame::Init.
  if (PresShell()->IsUnderHiddenEmbedderElement()) {
    return;
  }

  const bool isVisible = StyleVisibility()->IsVisible();
  if (!aOldComputedStyle ||
      isVisible != aOldComputedStyle->StyleVisibility()->IsVisible()) {
    PropagateIsUnderHiddenEmbedderElement(!isVisible);
  }
}

nsIFrame* NS_NewSubDocumentFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new nsSubDocumentFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsSubDocumentFrame)

class nsHideViewer final : public Runnable {
 public:
  nsHideViewer(nsIContent* aFrameElement, nsFrameLoader* aFrameLoader,
               PresShell* aPresShell, bool aHideViewerIfFrameless)
      : mozilla::Runnable("nsHideViewer"),
        mFrameElement(aFrameElement),
        mFrameLoader(aFrameLoader),
        mPresShell(aPresShell),
        mHideViewerIfFrameless(aHideViewerIfFrameless) {
    NS_ASSERTION(mFrameElement, "Must have a frame element");
    NS_ASSERTION(mFrameLoader, "Must have a frame loader");
    NS_ASSERTION(mPresShell, "Must have a presshell");
  }

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    // Flush frames, to ensure any pending display:none changes are made.
    // Note it can be unsafe to flush if we've destroyed the presentation
    // for some other reason, like if we're shutting down.
    //
    // But avoid the flush if we know for sure we're away, like when we're out
    // of the document already.
    //
    // FIXME(emilio): This could still be a perf footgun when removing lots of
    // siblings where each of them cause the reframe of an ancestor which happen
    // to contain a subdocument.
    //
    // We should find some way to avoid that!
    if (!mPresShell->IsDestroying() && mFrameElement->IsInComposedDoc()) {
      mPresShell->FlushPendingNotifications(FlushType::Frames);
    }

    // Either the frame has been constructed by now, or it never will be,
    // either way we want to clear the stashed views.
    mFrameLoader->SetDetachedSubdocs({});

    nsSubDocumentFrame* frame = do_QueryFrame(mFrameElement->GetPrimaryFrame());
    if (!frame || frame->FrameLoader() != mFrameLoader) {
      PropagateIsUnderHiddenEmbedderElement(mFrameLoader, true);
      if (mHideViewerIfFrameless) {
        // The frame element has no nsIFrame for the same frame loader.
        // Hide the nsFrameLoader, which destroys the presentation.
        mFrameLoader->Hide();
      }
    }
    return NS_OK;
  }

 private:
  const nsCOMPtr<nsIContent> mFrameElement;
  const RefPtr<nsFrameLoader> mFrameLoader;
  const RefPtr<PresShell> mPresShell;
  const bool mHideViewerIfFrameless;
};

void nsSubDocumentFrame::Destroy(DestroyContext& aContext) {
  if (mPostedReflowCallback) {
    PresShell()->CancelReflowCallback(this);
    mPostedReflowCallback = false;
  }

  // Detach the subdocument's views and stash them in the frame loader.
  // We can then reattach them if we're being reframed (for example if
  // the frame has been made position:fixed).
  if (RefPtr<nsFrameLoader> frameloader = FrameLoader()) {
    ClearDisplayItems();

    PrepareInProcessPresShellsForDetach();
    frameloader->SetDetachedSubdocs(std::move(mInProcessPresShells));

    // We call nsFrameLoader::HideViewer() in a script runner so that we can
    // safely determine whether the frame is being reframed or destroyed.
    nsContentUtils::AddScriptRunner(MakeAndAddRef<nsHideViewer>(
        mContent, frameloader, PresShell(), (mDidCreateDoc || mCallingShow)));
  }

  nsAtomicContainerFrame::Destroy(aContext);
}

nsFrameLoader* nsSubDocumentFrame::FrameLoader() const {
  if (mFrameLoader) {
    return mFrameLoader;
  }

  if (RefPtr<nsFrameLoaderOwner> loaderOwner = do_QueryObject(GetContent())) {
    mFrameLoader = loaderOwner->GetFrameLoader();
  }

  return mFrameLoader;
}

auto nsSubDocumentFrame::GetRemotePaintData() const -> RemoteFramePaintData {
  if (mRetainedRemoteFrame) {
    return *mRetainedRemoteFrame;
  }

  RemoteFramePaintData data;
  nsFrameLoader* fl = FrameLoader();
  if (!fl) {
    return data;
  }

  auto* rb = fl->GetRemoteBrowser();
  if (!rb) {
    return data;
  }
  data.mLayersId = rb->GetLayersId();
  data.mTabId = rb->GetTabId();
  return data;
}

void nsSubDocumentFrame::ResetFrameLoader(RetainPaintData aRetain) {
  if (aRetain == RetainPaintData::Yes && mFrameLoader) {
    mRetainedRemoteFrame = Some(GetRemotePaintData());
  } else {
    mRetainedRemoteFrame.reset();
  }
  mFrameLoader = nullptr;
  ClearDisplayItems();
  nsContentUtils::AddScriptRunner(MakeAndAddRef<AsyncFrameInit>(this));
}

void nsSubDocumentFrame::ClearRetainedPaintData() {
  mRetainedRemoteFrame.reset();
  ClearDisplayItems();
  InvalidateFrameSubtree();
}

// XXX this should be called ObtainDocShell or something like that,
// to indicate that it could have side effects
nsIDocShell* nsSubDocumentFrame::GetDocShell() const {
  // How can FrameLoader() return null???
  if (NS_WARN_IF(!FrameLoader())) {
    return nullptr;
  }
  return mFrameLoader->GetDocShell(IgnoreErrors());
}

nsIDocShell* nsSubDocumentFrame::GetExtantDocShell() const {
  return mFrameLoader ? mFrameLoader->GetExistingDocShell() : nullptr;
}

static void DestroyDisplayItemDataForFrames(nsIFrame* aFrame) {
  // Destroying a WebRenderUserDataTable can cause destruction of other objects
  // which can remove frame properties in their destructor. If we delete a frame
  // property it runs the destructor of the stored object in the middle of
  // updating the frame property table, so if the destruction of that object
  // causes another update to the frame property table it would leave the frame
  // property table in an inconsistent state. So we remove it from the table and
  // then destroy it. (bug 1530657)
  WebRenderUserDataTable* userDataTable =
      aFrame->TakeProperty(WebRenderUserDataProperty::Key());
  if (userDataTable) {
    for (const auto& data : userDataTable->Values()) {
      data->RemoveFromTable();
    }
    delete userDataTable;
  }

  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      DestroyDisplayItemDataForFrames(child);
    }
  }
}

nsresult nsSubDocumentFrame::BeginSwapDocShells(nsIFrame* aOther) {
  if (!aOther || !aOther->IsSubDocumentFrame()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  nsSubDocumentFrame* other = static_cast<nsSubDocumentFrame*>(aOther);
  if (!mFrameLoader || !mDidCreateDoc || mCallingShow || !other->mFrameLoader ||
      !other->mDidCreateDoc) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  ClearDisplayItems();
  other->ClearDisplayItems();

  PrepareInProcessPresShellsForDetach();
  other->PrepareInProcessPresShellsForDetach();

  mFrameLoader.swap(other->mFrameLoader);
  return NS_OK;
}

static CallState EndSwapDocShellsForDocument(Document& aDocument) {
  // Our docshell trees have been updated for the new hierarchy. Now also update
  // all nsDeviceContext::mWidget to that of the container view in the new
  // hierarchy.
  if (nsCOMPtr<nsIDocShell> ds = aDocument.GetDocShell()) {
    nsCOMPtr<nsIDocumentViewer> viewer;
    ds->GetDocViewer(getter_AddRefs(viewer));
    while (viewer) {
      RefPtr<nsPresContext> pc = viewer->GetPresContext();
      if (pc && pc->GetPresShell()) {
        pc->GetPresShell()->SetNeverPainting(ds->IsInvisible());
      }
      nsDeviceContext* dc = pc ? pc->DeviceContext() : nullptr;
      if (dc) {
        nsSubDocumentFrame* f = viewer->FindContainerFrame();
        nsIWidget* widget = f ? f->GetNearestWidget() : nullptr;
        if (widget) {
          widget = widget->GetTopLevelWidget();
        }
        dc->Init(widget);
      }
      viewer = viewer->GetPreviousViewer();
    }
  }

  aDocument.EnumerateSubDocuments(EndSwapDocShellsForDocument);
  return CallState::Continue;
}

static CallState BeginSwapDocShellsForDocument(Document& aDocument) {
  if (PresShell* presShell = aDocument.GetPresShell()) {
    // Disable painting while the presentation shell is detached.
    presShell->SetNeverPainting(true);

    if (nsIFrame* rootFrame = presShell->GetRootFrame()) {
      ::DestroyDisplayItemDataForFrames(rootFrame);
    }
  }
  aDocument.EnumerateSubDocuments(BeginSwapDocShellsForDocument);
  return CallState::Continue;
}

void nsSubDocumentFrame::PrepareInProcessPresShellsForDetach() {
  for (const auto& shell : mInProcessPresShells) {
    if (RefPtr<class PresShell> ps = do_QueryReferent(shell)) {
      BeginSwapDocShellsForDocument(*ps->GetDocument());
    }
  }
}

bool nsSubDocumentFrame::FixUpInProcessPresShellsAfterAttach() {
  bool anyLiveShell = false;
  for (auto& shell : mInProcessPresShells) {
    if (RefPtr<mozilla::PresShell> ps = do_QueryReferent(shell)) {
      if (ps && !ps->IsDestroying()) {
        anyLiveShell = true;
        ps->SetInProcessEmbedderFrame(this);
        EndSwapDocShellsForDocument(*ps->GetDocument());
      }
    }
  }
  return anyLiveShell;
}

void nsSubDocumentFrame::EndSwapDocShells(nsIFrame* aOther) {
  auto* other = static_cast<nsSubDocumentFrame*>(aOther);

  mInProcessPresShells.SwapElements(other->mInProcessPresShells);
  FixUpInProcessPresShellsAfterAttach();
  other->FixUpInProcessPresShellsAfterAttach();

  // Now make sure we reflow both frames, in case their contents
  // determine their size.
  // And repaint them, for good measure, in case there's nothing
  // interesting that happens during reflow.
  PresShell()->FrameNeedsReflow(this, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_IS_DIRTY);
  InvalidateFrameSubtree();
  PropagateIsUnderHiddenEmbedderElement(
      PresShell()->IsUnderHiddenEmbedderElement() ||
      !StyleVisibility()->IsVisible());

  other->PresShell()->FrameNeedsReflow(other, IntrinsicDirty::FrameAndAncestors,
                                       NS_FRAME_IS_DIRTY);
  other->InvalidateFrameSubtree();
  other->PropagateIsUnderHiddenEmbedderElement(
      other->PresShell()->IsUnderHiddenEmbedderElement() ||
      !other->StyleVisibility()->IsVisible());
}

void nsSubDocumentFrame::ClearDisplayItems() {
  if (auto* builder = nsLayoutUtils::GetRetainedDisplayListBuilder(this)) {
    DL_LOGD("nsSubDocumentFrame::ClearDisplayItems() %p", this);
    builder->ClearRetainedData();
  }
}

void nsSubDocumentFrame::SubdocumentIntrinsicSizeOrRatioChanged() {
  const nsStylePosition* pos = StylePosition();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);
  bool dependsOnIntrinsics =
      !pos->GetWidth(anchorResolutionParams)->ConvertsToLength() ||
      !pos->GetHeight(anchorResolutionParams)->ConvertsToLength();

  if (dependsOnIntrinsics || pos->mObjectFit != StyleObjectFit::Fill) {
    auto dirtyHint = dependsOnIntrinsics
                         ? IntrinsicDirty::FrameAncestorsAndDescendants
                         : IntrinsicDirty::None;
    PresShell()->FrameNeedsReflow(this, dirtyHint, NS_FRAME_IS_DIRTY);
    InvalidateFrame();
  }
}

bool nsSubDocumentFrame::ContentReactsToPointerEvents() const {
  if (Style()->PointerEvents() == StylePointerEvents::None) {
    return false;
  }
  if (mIsInObjectOrEmbed) {
    if (nsCOMPtr<nsIObjectLoadingContent> iolc = do_QueryInterface(mContent)) {
      const auto* olc = static_cast<nsObjectLoadingContent*>(iolc.get());
      if (olc->IsSyntheticImageDocument()) {
        return false;
      }
    }
  }
  return true;
}

nsDisplayRemote::nsDisplayRemote(nsDisplayListBuilder* aBuilder,
                                 nsSubDocumentFrame* aFrame)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mEventRegionsOverride(EventRegionsOverride::NoOverride) {
  if (aBuilder->BuildCompositorHitTestInfo()) {
    if (aBuilder->IsInsidePointerEventsNoneDoc() ||
        !aFrame->ContentReactsToPointerEvents()) {
      mEventRegionsOverride |= EventRegionsOverride::ForceEmptyHitRegion;
    }
    if (nsLayoutUtils::HasDocumentLevelListenersForApzAwareEvents(
            aFrame->PresShell())) {
      mEventRegionsOverride |= EventRegionsOverride::ForceDispatchToContent;
    }
  }

  mPaintData = aFrame->GetRemotePaintData();
}

namespace mozilla {

void nsDisplayRemote::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  DrawTarget* target = aCtx->GetDrawTarget();
  if (!target->IsRecording() || mPaintData.mTabId == 0) {
    NS_WARNING("Remote iframe not rendered");
    return;
  }

  // Rendering the inner document will apply a scale to account for its app
  // units per dev pixel ratio. We want to apply the inverse scaling using our
  // app units per dev pixel ratio, so that no actual scaling will be applied if
  // they match. For in-process rendering, nsSubDocumentFrame creates an
  // nsDisplayZoom item if the app units per dev pixel ratio changes.
  //
  // Similarly, rendering the inner document will scale up by the cross process
  // paint scale again, so we also need to account for that.
  const int32_t appUnitsPerDevPixel =
      mFrame->PresContext()->AppUnitsPerDevPixel();

  gfxContextMatrixAutoSaveRestore saveMatrix(aCtx);
  gfxFloat targetAuPerDev =
      gfxFloat(AppUnitsPerCSSPixel()) / aCtx->GetCrossProcessPaintScale();

  gfxFloat scale = targetAuPerDev / appUnitsPerDevPixel;
  aCtx->Multiply(gfxMatrix::Scaling(scale, scale));

  Rect destRect =
      NSRectToSnappedRect(GetContentRect(), targetAuPerDev, *target);
  target->DrawDependentSurface(mPaintData.mTabId, destRect);
}

WebRenderCommandsResult nsDisplayRemote::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!mPaintData.mLayersId.IsValid()) {
    return Ok();
  }

  auto* subDocFrame = static_cast<nsSubDocumentFrame*>(mFrame);
  nsRect destRect = subDocFrame->GetDestRect();
  if (aDisplayListBuilder->IsForPainting()) {
    subDocFrame->SetRasterScale(aSc.GetInheritedScale());
    const nsRect buildingRect = GetBuildingRect() - ToReferenceFrame();
    Maybe<nsRect> visibleRect =
        buildingRect.EdgeInclusiveIntersection(destRect);
    if (visibleRect) {
      *visibleRect -= destRect.TopLeft();
    }
    subDocFrame->SetVisibleRect(visibleRect);
  }
  nscoord auPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  nsPoint layerOffset =
      aDisplayListBuilder->ToReferenceFrame(mFrame) + destRect.TopLeft();
  mOffset = LayoutDevicePoint::FromAppUnits(layerOffset, auPerDevPixel);

  destRect.MoveTo(0, 0);
  auto rect = LayoutDeviceRect::FromAppUnits(destRect, auPerDevPixel);
  rect += mOffset;

  aBuilder.PushIFrame(rect, !BackfaceIsHidden(),
                      mozilla::wr::AsPipelineId(mPaintData.mLayersId),
                      /*ignoreMissingPipelines*/ true);

  return Ok();
}

bool nsDisplayRemote::UpdateScrollData(
    mozilla::layers::WebRenderScrollData* aData,
    mozilla::layers::WebRenderLayerScrollData* aLayerData) {
  if (!mPaintData.mLayersId.IsValid()) {
    return true;
  }

  if (aLayerData) {
    aLayerData->SetReferentId(mPaintData.mLayersId);

    auto size = static_cast<nsSubDocumentFrame*>(mFrame)->GetSubdocumentSize();
    Matrix4x4 m = Matrix4x4::Translation(mOffset.x, mOffset.y, 0.0);
    aLayerData->SetTransform(m);
    aLayerData->SetEventRegionsOverride(mEventRegionsOverride);
    aLayerData->SetRemoteDocumentSize(LayerIntSize(size.width, size.height));
  }
  return true;
}

nsFrameLoader* nsDisplayRemote::GetFrameLoader() const {
  return static_cast<nsSubDocumentFrame*>(mFrame)->FrameLoader();
}

}  // namespace mozilla
