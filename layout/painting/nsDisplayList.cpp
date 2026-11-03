/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * structures that represent things to be painted (ordered in z-order),
 * used during painting and hit testing
 */

#include "nsDisplayList.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "ActiveLayerTracker.h"
#include "BorderConsts.h"
#include "ImageContainer.h"
#include "LayerAnimationInfo.h"
#include "StickyScrollContainer.h"
#include "TextDrawTarget.h"
#include "UnitTransforms.h"
#include "gfxContext.h"
#include "gfxMatrix.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "mozilla/AnimationPerformanceWarning.h"
#include "mozilla/AnimationUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EffectSet.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/Likely.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/OperatorNewExtensions.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/SVGClipPathFrame.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/SVGMaskFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoComputedData.h"
#include "mozilla/ShapeUtils.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/ViewportFrame.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/PerformanceContainerTiming.h"
#include "mozilla/dom/PerformanceMainThread.h"
#include "mozilla/dom/RemoteBrowser.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/SVGPathData.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ServiceWorkerRegistrar.h"
#include "mozilla/dom/ServiceWorkerRegistration.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/layers/AnimationHelper.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/TreeTraversal.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/WebRenderMessages.h"
#include "mozilla/layers/WebRenderScrollData.h"
#include "nsCSSProps.h"
#include "nsCSSRendering.h"
#include "nsCSSRenderingBorders.h"
#include "nsCSSRenderingGradients.h"
#include "nsCanvasFrame.h"
#include "nsCaret.h"
#include "nsCaseTreatment.h"
#include "nsDOMTokenList.h"
#include "nsEscape.h"
#include "nsFocusManager.h"
#include "nsFrameSelection.h"
#include "nsIFrameInlines.h"
#include "nsImageFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContextInlines.h"
#include "nsPrintfCString.h"
#include "nsRefreshDriver.h"
#include "nsRegion.h"
#include "nsSliderFrame.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "nsStyleTransformMatrix.h"
#include "nsSubDocumentFrame.h"
#include "nsTableCellFrame.h"
#include "nsTableColFrame.h"
#include "nsTextFrame.h"
#include "nsTextPaintStyle.h"
#include "nsTransitionManager.h"

namespace mozilla {

using namespace dom;
using namespace gfx;
using namespace layout;
using namespace layers;
using namespace image;

LazyLogModule sContentDisplayListLog("dl.content");
LazyLogModule sParentDisplayListLog("dl.parent");

LazyLogModule& GetLoggerByProcess() {
  return XRE_IsContentProcess() ? sContentDisplayListLog
                                : sParentDisplayListLog;
}

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
void AssertUniqueItem(nsDisplayItem* aItem) {
  for (nsDisplayItem* i : aItem->Frame()->DisplayItems()) {
    if (i != aItem && !i->HasDeletedFrame() && i->Frame() == aItem->Frame() &&
        i->GetPerFrameKey() == aItem->GetPerFrameKey()) {
      if (i->IsPreProcessedItem() || i->IsPreProcessed()) {
        continue;
      }
      MOZ_DIAGNOSTIC_CRASH("Duplicate display item!");
    }
  }
}
#endif

bool ShouldBuildItemForEvents(const DisplayItemType aType) {
  return aType == DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO ||
         aType == DisplayItemType::TYPE_REMOTE ||
         (GetDisplayItemFlagsForType(aType) & TYPE_IS_CONTAINER);
}

static bool ItemTypeSupportsHitTesting(const DisplayItemType aType) {
  switch (aType) {
    case DisplayItemType::TYPE_BACKGROUND:
    case DisplayItemType::TYPE_BACKGROUND_COLOR:
    case DisplayItemType::TYPE_THEMED_BACKGROUND:
      return true;
    default:
      return false;
  }
}

void InitializeHitTestInfo(nsDisplayListBuilder* aBuilder,
                           nsPaintedDisplayItem* aItem,
                           const DisplayItemType aType) {
  if (ItemTypeSupportsHitTesting(aType)) {
    aItem->InitializeHitTestInfo(aBuilder);
  }
}

/* static */
already_AddRefed<ActiveScrolledRoot> ActiveScrolledRoot::GetOrCreateASRForFrame(
    const ActiveScrolledRoot* aParent, nsIFrame* aFrame,
    nsTArray<RefPtr<ActiveScrolledRoot>>& aActiveScrolledRoots,
    ASRKind asrKind) {
  MOZ_ASSERT_IF(asrKind == ASRKind::Scroll,
                aFrame->IsScrollContainerOrSubclass());

  if (asrKind == ASRKind::Sticky) {
    aFrame = aFrame->FirstContinuation();
  }

  RefPtr<ActiveScrolledRoot> asr = aFrame->GetProperty(
      asrKind == ASRKind::Scroll ? ActiveScrolledRootCache()
                                 : StickyActiveScrolledRootCache());

#ifdef DEBUG
  if (asr && aActiveScrolledRoots.Contains(asr)) {
    // If this is the second time we are called for this frame in this same
    // paint, assert that we aren't changing any of the values. (The values can
    // change *between* paints, but not during one paint.)
    MOZ_ASSERT(asr->mParent == aParent);
    MOZ_ASSERT(asr->mFrame == aFrame);
    MOZ_ASSERT(asr->mKind == asrKind);
    asr->AssertDepthInvariant();
  }
#endif

  if (!asr) {
    asr = new ActiveScrolledRoot();

    RefPtr<ActiveScrolledRoot> ref = asr;
    aFrame->SetProperty(asrKind == ASRKind::Scroll
                            ? ActiveScrolledRootCache()
                            : StickyActiveScrolledRootCache(),
                        ref.forget().take());
    aActiveScrolledRoots.AppendElement(asr);
  }
  asr->mParent = aParent;
  asr->mFrame = aFrame;
  asr->mKind = asrKind;
  asr->mDepth = aParent ? aParent->mDepth + 1 : 1;

  return asr.forget();
}

/* static */
bool ActiveScrolledRoot::IsAncestor(const ActiveScrolledRoot* aAncestor,
                                    const ActiveScrolledRoot* aDescendant) {
  if (!aAncestor) {
    // nullptr is the root
    return true;
  }
  if (Depth(aAncestor) > Depth(aDescendant)) {
    return false;
  }
  const ActiveScrolledRoot* asr = aDescendant;
  while (asr) {
    if (asr == aAncestor) {
      return true;
    }
    asr = asr->mParent;
  }
  return false;
}

/* static */
bool ActiveScrolledRoot::IsProperAncestor(
    const ActiveScrolledRoot* aAncestor,
    const ActiveScrolledRoot* aDescendant) {
  return aAncestor != aDescendant && IsAncestor(aAncestor, aDescendant);
}

/* static */
const ActiveScrolledRoot* ActiveScrolledRoot::LowestCommonAncestor(
    const ActiveScrolledRoot* aOne, const ActiveScrolledRoot* aTwo) {
  uint32_t depth1 = Depth(aOne);
  uint32_t depth2 = Depth(aTwo);
  if (depth1 > depth2) {
    for (uint32_t i = 0; i < (depth1 - depth2); ++i) {
      MOZ_ASSERT(aOne);
      aOne = aOne->mParent;
    }
  } else if (depth1 < depth2) {
    for (uint32_t i = 0; i < (depth2 - depth1); ++i) {
      MOZ_ASSERT(aTwo);
      aTwo = aTwo->mParent;
    }
  }
  while (aOne != aTwo) {
    MOZ_DIAGNOSTIC_ASSERT(aOne);
    MOZ_DIAGNOSTIC_ASSERT(aTwo);
    if (MOZ_UNLIKELY(!aOne || !aTwo)) {
      gfxCriticalNoteOnce << "ActiveScrolledRoot::mDepth was incorrect";
      return nullptr;
    }
    aOne->AssertDepthInvariant();
    aTwo->AssertDepthInvariant();
    aOne = aOne->mParent;
    aTwo = aTwo->mParent;
  }
  return aOne;
}

ScrollContainerFrame* ActiveScrolledRoot::ScrollFrameOrNull() const {
  if (mKind == ASRKind::Scroll) {
    ScrollContainerFrame* scrollFrame =
        static_cast<ScrollContainerFrame*>(mFrame);
    MOZ_ASSERT(scrollFrame);
    return scrollFrame;
  }
  return nullptr;
}

const ActiveScrolledRoot* ActiveScrolledRoot::GetNearestScrollASR() const {
  const ActiveScrolledRoot* ret = this;

  while (ret && ret->mKind != ASRKind::Scroll) {
    ret = ret->mParent;
  }

  if (!ret || ret->mKind != ASRKind::Scroll) {
    return nullptr;
  }

  return ret;
}

layers::ScrollableLayerGuid::ViewID
ActiveScrolledRoot::GetNearestScrollASRViewId() const {
  const ActiveScrolledRoot* scrollASR = GetNearestScrollASR();
  if (scrollASR) {
    return scrollASR->GetViewId();
  }
  return ScrollableLayerGuid::NULL_SCROLL_ID;
}

/* static */
const ActiveScrolledRoot* ActiveScrolledRoot::GetStickyASRFromFrame(
    nsIFrame* aStickyFrame) {
  return aStickyFrame->FirstContinuation()->GetProperty(
      StickyActiveScrolledRootCache());
}

/* static */
nsCString ActiveScrolledRoot::ToString(
    const ActiveScrolledRoot* aActiveScrolledRoot) {
  nsAutoCString str;
  if (!aActiveScrolledRoot) {
    str.AppendPrintf("null");
    return str;
  }
  if (aActiveScrolledRoot->mKind == ASRKind::Sticky) {
    str.AppendPrintf("sticky ");
  }
  for (const auto* asr = aActiveScrolledRoot; asr; asr = asr->mParent) {
    str.AppendPrintf("<0x%p>", asr->mFrame);
    if (asr->mParent) {
      str.AppendLiteral(", ");
    }
  }
  return std::move(str);
}

ScrollableLayerGuid::ViewID ActiveScrolledRoot::ComputeViewId() const {
  const ActiveScrolledRoot* scrollASR = GetNearestScrollASR();
  MOZ_ASSERT(scrollASR,
             "ComputeViewId() called on ASR with no enclosing scroll frame");
  nsIContent* content = scrollASR->ScrollFrame()->GetContent();
  return nsLayoutUtils::FindOrCreateIDFor(content);
}

ActiveScrolledRoot::~ActiveScrolledRoot() {
  if (mFrame) {
    mFrame->RemoveProperty(mKind == ASRKind::Sticky
                               ? StickyActiveScrolledRootCache()
                               : ActiveScrolledRootCache());
  }
}

void ActiveScrolledRoot::AssertDepthInvariant() const {
  MOZ_DIAGNOSTIC_ASSERT(mDepth == (mParent ? mParent->mDepth + 1 : 1));
}

static uint64_t AddAnimationsForWebRender(
    nsDisplayItem* aItem, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder,
    const Maybe<LayoutDevicePoint>& aPosition = Nothing()) {
  auto* effects = EffectSet::GetForFrame(aItem->Frame(), aItem->GetType());
  if (!effects || effects->IsEmpty()) {
    // If there is no animation on the nsIFrame, that means
    //  1) we've never created any animations on this frame or
    //  2) the frame was reconstruced or
    //  3) all animations on the frame have finished
    // in such cases we don't need do anything here.
    //
    // Even if there is a WebRenderAnimationData for the display item type on
    // this frame, it's going to be discarded since it's not marked as being
    // used.
    return 0;
  }

  RefPtr<WebRenderAnimationData> animationData =
      aManager->CommandBuilder()
          .CreateOrRecycleWebRenderUserData<WebRenderAnimationData>(aItem);
  AnimationInfo& animationInfo = animationData->GetAnimationInfo();
  nsIFrame* frame = aItem->Frame();
  animationInfo.AddAnimationsForDisplayItem(
      frame, aDisplayListBuilder, aItem, aItem->GetType(),
      aManager->LayerManager(), aPosition);

  // Note that animationsId can be 0 (uninitialized in AnimationInfo) if there
  // are no active animations.
  uint64_t animationsId = animationInfo.GetCompositorAnimationsId();
  if (!animationInfo.GetAnimations().IsEmpty()) {
    OpAddCompositorAnimations anim(
        CompositorAnimations(animationInfo.GetAnimations(), animationsId));
    aManager->WrBridge()->AddWebRenderParentCommand(anim);
    aManager->AddActiveCompositorAnimationId(animationsId);
  } else if (animationsId) {
    aManager->AddCompositorAnimationsIdForDiscard(animationsId);
    animationsId = 0;
  }

  return animationsId;
}

static bool GenerateAndPushTextMask(nsIFrame* aFrame, gfxContext* aContext,
                                    const nsRect& aFillRect,
                                    nsDisplayListBuilder* aBuilder) {
  if (aBuilder->IsForGenerateGlyphMask()) {
    return false;
  }

  SVGObserverUtils::GetAndObserveBackgroundClip(aFrame);

  // The main function of enabling background-clip:text property value.
  // When a nsDisplayBackgroundImage detects "text" bg-clip style, it will call
  // this function to
  // 1. Generate a mask by all descendant text frames
  // 2. Push the generated mask into aContext.

  gfxContext* sourceCtx = aContext;
  LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
      aFillRect, aFrame->PresContext()->AppUnitsPerDevPixel());

  // Create a mask surface.
  RefPtr<DrawTarget> sourceTarget = sourceCtx->GetDrawTarget();
  RefPtr<DrawTarget> maskDT = sourceTarget->CreateClippedDrawTarget(
      bounds.ToUnknownRect(), SurfaceFormat::A8);
  if (!maskDT || !maskDT->IsValid()) {
    return false;
  }
  gfxContext maskCtx(maskDT, /* aPreserveTransform */ true);
  maskCtx.Multiply(Matrix::Translation(bounds.TopLeft().ToUnknownPoint()));

  // Shade text shape into mask A8 surface.
  nsLayoutUtils::PaintFrame(
      &maskCtx, aFrame, nsRect(nsPoint(0, 0), aFrame->GetSize()),
      NS_RGB(255, 255, 255), nsDisplayListBuilderMode::GenerateGlyph);

  // Push the generated mask into aContext, so that the caller can pop and
  // blend with it.

  Matrix currentMatrix = sourceCtx->CurrentMatrix();
  Matrix invCurrentMatrix = currentMatrix;
  invCurrentMatrix.Invert();

  RefPtr<SourceSurface> maskSurface = maskDT->Snapshot();
  sourceCtx->PushGroupForBlendBack(gfxContentType::COLOR_ALPHA, 1.0,
                                   maskSurface, invCurrentMatrix);

  return true;
}

nsDisplayWrapper* nsDisplayWrapList::CreateShallowCopy(
    nsDisplayListBuilder* aBuilder) {
  const nsDisplayWrapList* wrappedItem = AsDisplayWrapList();
  MOZ_ASSERT(wrappedItem);

  // Create a new nsDisplayWrapList using a copy-constructor. This is done
  // to preserve the information about bounds.
  nsDisplayWrapper* wrapper =
      new (aBuilder) nsDisplayWrapper(aBuilder, *wrappedItem);
  wrapper->SetType(nsDisplayWrapper::ItemType());
  MOZ_ASSERT(wrapper);

  // Set the display list pointer of the new wrapper item to the display list
  // of the wrapped item.
  wrapper->mListPtr = wrappedItem->mListPtr;
  return wrapper;
}

nsDisplayWrapList* nsDisplayListBuilder::MergeItems(
    nsTArray<nsDisplayItem*>& aItems) {
  // For merging, we create a temporary item by cloning the last item of the
  // mergeable items list. This ensures that the temporary item will have the
  // correct frame and bounds.
  nsDisplayWrapList* last = aItems.PopLastElement()->AsDisplayWrapList();
  MOZ_ASSERT(last);
  nsDisplayWrapList* merged = last->Clone(this);
  MOZ_ASSERT(merged);
  AddTemporaryItem(merged);

  // Create nsDisplayWrappers that point to the internal display lists of the
  // items we are merging. These nsDisplayWrappers are added to the display list
  // of the temporary item.
  for (nsDisplayItem* item : aItems) {
    MOZ_ASSERT(item);
    MOZ_ASSERT(merged->CanMerge(item));
    merged->Merge(item);
    MOZ_ASSERT(item->AsDisplayWrapList());
    merged->GetChildren()->AppendToTop(
        static_cast<nsDisplayWrapList*>(item)->CreateShallowCopy(this));
  }

  merged->GetChildren()->AppendToTop(last->CreateShallowCopy(this));

  return merged;
}

// FIXME(emilio): This whole business should ideally not be needed at all, but
// there are a variety of hard-to-deal-with caret invalidation issues, like
// bug 1888583, and caret changes are relatively uncommon, enough that it
// probably isn't worth chasing all them down.
void nsDisplayListBuilder::InvalidateCaretFramesIfNeeded() {
  if (mPaintedCarets.IsEmpty()) {
    return;
  }
  size_t i = mPaintedCarets.Length();
  while (i--) {
    nsCaret* caret = mPaintedCarets[i];
    nsIFrame* oldCaret = caret->GetLastPaintedFrame();
    nsIFrame* currentCaret = caret->GetPaintGeometry();
    if (oldCaret == currentCaret) {
      // Keep tracking this caret, it hasn't changed.
      continue;
    }
    if (oldCaret) {
      oldCaret->MarkNeedsDisplayItemRebuild();
    }
    if (currentCaret) {
      currentCaret->MarkNeedsDisplayItemRebuild();
    }
    // If / when we paint this caret, we'll track it again.
    caret->SetLastPaintedFrame(nullptr);
    mPaintedCarets.RemoveElementAt(i);
  }
}

void nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter::
    SetCurrentActiveScrolledRoot(
        const ActiveScrolledRoot* aActiveScrolledRoot) {
  MOZ_ASSERT(!mUsed);

  // Set the builder's mCurrentActiveScrolledRoot.
  mBuilder->mCurrentActiveScrolledRoot = aActiveScrolledRoot;

  // We also need to adjust the builder's mCurrentContainerASR.
  // mCurrentContainerASR needs to be an ASR that all the container's
  // contents have finite bounds with respect to. If aActiveScrolledRoot
  // is an ancestor ASR of mCurrentContainerASR, that means we need to
  // set mCurrentContainerASR to aActiveScrolledRoot, because otherwise
  // the items that will be created with aActiveScrolledRoot wouldn't
  // have finite bounds with respect to mCurrentContainerASR. There's one
  // exception, in the case where there's a content clip on the builder
  // that is scrolled by a descendant ASR of aActiveScrolledRoot. This
  // content clip will clip all items that are created while this
  // AutoCurrentActiveScrolledRootSetter exists. This means that the items
  // created during our lifetime will have finite bounds with respect to
  // the content clip's ASR, even if the items' actual ASR is an ancestor
  // of that. And it also means that mCurrentContainerASR only needs to be
  // set to the content clip's ASR and not all the way to aActiveScrolledRoot.
  // This case is tested by fixed-pos-scrolled-clip-opacity-layerize.html
  // and fixed-pos-scrolled-clip-opacity-inside-layerize.html.

  // finiteBoundsASR is the leafmost ASR that all items created during
  // object's lifetime have finite bounds with respect to.
  // TODO(bug 2001862): Explanation may need revising.
  const ActiveScrolledRoot* finiteBoundsASR = aActiveScrolledRoot;
  if (!mBuilder->IsInViewTransitionCapture()) {
    finiteBoundsASR =
        ActiveScrolledRoot::IsAncestor(aActiveScrolledRoot, mContentClipASR)
            ? mContentClipASR
            : aActiveScrolledRoot;
  }

  // mCurrentContainerASR is adjusted so that it's still an ancestor of
  // finiteBoundsASR.
  mBuilder->mCurrentContainerASR = ActiveScrolledRoot::PickAncestor(
      mBuilder->mCurrentContainerASR, finiteBoundsASR);

  // If we are entering out-of-flow content inside a CSS filter, mark
  // scroll frames wrt. which the content is fixed as containing such content.
  if (mBuilder->mFilterASR && ActiveScrolledRoot::IsAncestor(
                                  aActiveScrolledRoot, mBuilder->mFilterASR)) {
    for (const ActiveScrolledRoot* asr = mBuilder->mFilterASR;
         asr && asr != aActiveScrolledRoot; asr = asr->mParent) {
      if (ScrollContainerFrame* scrollFrame = asr->ScrollFrameOrNull()) {
        scrollFrame->SetHasOutOfFlowContentInsideFilter();
      }
    }
  }

  mUsed = true;
}

void nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter::
    EnterScrollFrame(ScrollContainerFrame* aScrollContainerFrame) {
  MOZ_ASSERT(!mUsed);
  ActiveScrolledRoot* asr = mBuilder->GetOrCreateActiveScrolledRoot(
      mBuilder->mCurrentActiveScrolledRoot, aScrollContainerFrame);
  mBuilder->mCurrentActiveScrolledRoot = asr;
  mUsed = true;
}

void nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter::
    InsertScrollFrame(ScrollContainerFrame* aScrollContainerFrame) {
  MOZ_ASSERT(!mUsed);
  size_t descendantsEndIndex = mBuilder->mActiveScrolledRoots.Length();
  const ActiveScrolledRoot* parentASR = mBuilder->mCurrentActiveScrolledRoot;
  const ActiveScrolledRoot* asr =
      mBuilder->GetOrCreateActiveScrolledRoot(parentASR, aScrollContainerFrame);
  mBuilder->mCurrentActiveScrolledRoot = asr;

  // All child ASRs of parentASR that were created while this
  // AutoCurrentActiveScrolledRootSetter object was on the stack belong to us
  // now. Reparent them to asr.
  for (size_t i = mDescendantsStartIndex; i < descendantsEndIndex; i++) {
    ActiveScrolledRoot* descendantASR = mBuilder->mActiveScrolledRoots[i];
    if (ActiveScrolledRoot::IsAncestor(parentASR, descendantASR)) {
      descendantASR->IncrementDepth();
      if (descendantASR->mParent == parentASR) {
        descendantASR->mParent = asr;
      }
    }
  }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  for (size_t i = mDescendantsStartIndex; i < descendantsEndIndex; i++) {
    mBuilder->mActiveScrolledRoots[i]->AssertDepthInvariant();
  }
#endif

  mUsed = true;
}

nsDisplayListBuilder::AutoContainerASRTracker::AutoContainerASRTracker(
    nsDisplayListBuilder* aBuilder)
    : mBuilder(aBuilder), mSavedContainerASR(aBuilder->mCurrentContainerASR) {
  mBuilder->mCurrentContainerASR = mBuilder->mCurrentActiveScrolledRoot;
}

nsPresContext* nsDisplayListBuilder::CurrentPresContext() {
  return CurrentPresShellState()->mPresShell->GetPresContext();
}

#ifdef DEBUG
// A weird case for native anonymous content in the custom content container
// when the root is captured by a view transition. This content is built outside
// of the view transition capture but the containing block (the canvas frame)
// was built inside the capture, so savedOutOfFlowData is saved as if we are
// inside the capture while we are outside it (bug 2002160).
static bool InTopLayerAndActiveViewTransition(nsIFrame* aFrame) {
  if (!aFrame->PresContext()->Document()->GetActiveViewTransition()) {
    return false;
  }
  if (!aFrame->GetContent()->IsInNativeAnonymousSubtree()) {
    return false;
  }
  for (nsIFrame* curr = aFrame; curr; curr = curr->GetParent()) {
    if (curr->StyleDisplay()->mTopLayer == StyleTopLayer::Auto) {
      return true;
    }
  }
  return false;
}

void nsDisplayListBuilder::OutOfFlowDisplayData::CheckASR(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame) {
  if (!aBuilder->IsPaintingToWindow()) {
    return;
  }
  auto* asr = mContainingBlockActiveScrolledRoot;
  if (mContainingBlockInViewTransitionCapture) {
    MOZ_ASSERT(!asr);
    MOZ_ASSERT(aBuilder->IsInViewTransitionCapture() ||
               InTopLayerAndActiveViewTransition(aFrame));
    return;
  }
  auto frameAndASRKind = asr ? FrameAndASRKind{asr->mFrame, asr->mKind}
                             : FrameAndASRKind::default_value();
  if (frameAndASRKind ==
      DisplayPortUtils::GetASRAncestorFrame(
          {aFrame->GetParent(), ActiveScrolledRoot::ASRKind::Scroll},
          aBuilder)) {
    // All as expected.
    return;
  }
  MOZ_ASSERT(!asr);
  MOZ_ASSERT(InTopLayerAndActiveViewTransition(aFrame));
}
#endif

/* static */
nsRect nsDisplayListBuilder::OutOfFlowDisplayData::ComputeVisibleRectForFrame(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
    const nsRect& aVisibleRect, const nsRect& aDirtyRect,
    nsRect* aOutDirtyRect) {
  nsRect visible = aVisibleRect;
  nsRect dirtyRectRelativeToDirtyFrame = aDirtyRect;

  bool inPartialUpdate =
      aBuilder->IsRetainingDisplayList() && aBuilder->IsPartialUpdate();
  if (MOZ_LIKELY(StaticPrefs::apz_allow_zooming()) &&
      aBuilder->IsPaintingToWindow() && !inPartialUpdate &&
      DisplayPortUtils::IsFixedPosFrameInDisplayPort(aFrame)) {
    dirtyRectRelativeToDirtyFrame =
        nsRect(nsPoint(0, 0), aFrame->GetParent()->GetSize());

    // If there's a visual viewport size set, restrict the amount of the
    // fixed-position element we paint to the visual viewport. (In general
    // the fixed-position element can be as large as the layout viewport,
    // which at a high zoom level can cause us to paint too large of an
    // area.)
    PresShell* presShell = aFrame->PresShell();
    if (presShell->IsVisualViewportSizeSet()) {
      dirtyRectRelativeToDirtyFrame =
          nsRect(presShell->GetVisualViewportOffsetRelativeToLayoutViewport(),
                 presShell->GetVisualViewportSize());
      // But if we have a displayport, expand it to the displayport, so
      // that async-scrolling the visual viewport within the layout viewport
      // will not checkerboard.
      if (nsIFrame* rootScrollContainerFrame =
              presShell->GetRootScrollContainerFrame()) {
        nsRect displayport;
        // Note that the displayport here is already in the right coordinate
        // space: it's relative to the scroll port (= layout viewport), but
        // covers the visual viewport with some margins around it, which is
        // exactly what we want.
        if (DisplayPortUtils::GetDisplayPort(
                rootScrollContainerFrame->GetContent(), &displayport,
                DisplayPortOptions().With(ContentGeometryType::Fixed))) {
          dirtyRectRelativeToDirtyFrame = displayport;
        }
      }
    }
    visible = dirtyRectRelativeToDirtyFrame;
    if (StaticPrefs::apz_test_logging_enabled() &&
        presShell->GetDocument()->IsContentDocument()) {
      nsLayoutUtils::LogAdditionalTestData(
          aBuilder, "fixedPosDisplayport",
          ToString(CSSSize::FromAppUnits(visible)));
    }
  }

  *aOutDirtyRect = dirtyRectRelativeToDirtyFrame - aFrame->GetPosition();
  visible -= aFrame->GetPosition();

  nsRect overflowRect = aFrame->InkOverflowRect();

  if (aFrame->IsTransformed() && EffectCompositor::HasAnimationsForCompositor(
                                     aFrame, DisplayItemType::TYPE_TRANSFORM)) {
    /**
     * Add a fuzz factor to the overflow rectangle so that elements only
     * just out of view are pulled into the display list, so they can be
     * prerendered if necessary.
     */
    overflowRect.Inflate(nsPresContext::CSSPixelsToAppUnits(32));
  }

  visible.IntersectRect(visible, overflowRect);
  aOutDirtyRect->IntersectRect(*aOutDirtyRect, overflowRect);

  return visible;
}

nsDisplayListBuilder::Linkifier::Linkifier(nsDisplayListBuilder* aBuilder,
                                           nsIFrame* aFrame,
                                           nsDisplayList* aList)
    : mList(aList) {
  // Find the element that we need to check for link-ness, bailing out if
  // we can't find one.
  Element* elem = Element::FromNodeOrNull(aFrame->GetContent());
  if (!elem) {
    return;
  }

  // If the element has an id and/or name attribute, generate a destination
  // for possible internal linking.
  auto maybeGenerateDest = [&](const nsAtom* aAttr) {
    nsAutoString attrValue;
    elem->GetAttr(aAttr, attrValue);
    if (!attrValue.IsEmpty()) {
      NS_ConvertUTF16toUTF8 dest(attrValue);
      // Ensure that we only emit a given destination once, although there may
      // be multiple frames associated with a given element; we'll simply use
      // the first of them as the target of any links to it.
      // XXX(jfkthame) This prevents emitting duplicate destinations *on the
      // same page*, but does not prevent duplicates on subsequent pages, as
      // each new page is handled by a new temporary DisplayListBuilder. This
      // seems to be harmless in practice, though a bit wasteful of space. To
      // fix, we need to maintain the set of already-seen destinations globally
      // for the print job, rather than attached to the (per-page) builder.
      if (aBuilder->mDestinations.EnsureInserted(dest)) {
        auto* destination = MakeDisplayItem<nsDisplayDestination>(
            aBuilder, aFrame, dest.get(), aFrame->GetRect().TopLeft());
        mList->AppendToTop(destination);
      }
    }
  };

  if (StaticPrefs::print_save_as_pdf_internal_destinations_enabled()) {
    if (elem->HasID()) {
      maybeGenerateDest(nsGkAtoms::id);
    }
    if (elem->HasName()) {
      maybeGenerateDest(nsGkAtoms::name);
    }
  }

  // Links don't nest, so if the builder already has a destination, no need to
  // check for a link element here.
  if (!aBuilder->mLinkURI.IsEmpty() || !aBuilder->mLinkDest.IsEmpty()) {
    return;
  }

  // Check if we have actually found a link.
  if (!elem->IsLink()) {
    return;
  }

  nsCOMPtr<nsIURI> uri = elem->GetHrefURI();
  if (!uri) {
    return;
  }

  // Is it potentially a local (in-document) destination?
  bool hasRef, eqExRef;
  nsIURI* docURI;
  if (StaticPrefs::print_save_as_pdf_internal_destinations_enabled() &&
      NS_SUCCEEDED(uri->GetHasRef(&hasRef)) && hasRef &&
      (docURI = aFrame->PresContext()->Document()->GetDocumentURI()) &&
      NS_SUCCEEDED(uri->EqualsExceptRef(docURI, &eqExRef)) && eqExRef) {
    // Try to get a local destination name. If this fails, we'll leave the
    // mLinkDest string empty, but still try to set mLinkURI below.
    if (NS_FAILED(uri->GetRef(aBuilder->mLinkDest))) {
      aBuilder->mLinkDest.Truncate();
    }
    // The destination name is simply a string; we don't want URL-escaping
    // applied to it.
    if (!aBuilder->mLinkDest.IsEmpty()) {
      NS_UnescapeURL(aBuilder->mLinkDest);
    }
  }

  if (NS_FAILED(uri->GetSpec(aBuilder->mLinkURI))) {
    aBuilder->mLinkURI.Truncate();
  }

  // If we didn't get either kind of destination, we won't try to linkify at
  // this level.
  if (aBuilder->mLinkDest.IsEmpty() && aBuilder->mLinkURI.IsEmpty()) {
    return;
  }

  nsPoint offset;
  aBuilder->mLinkReferenceFrame =
      aBuilder->FindReferenceFrameFor(aFrame, &offset);
  aBuilder->mLinkBounds = nsRect(offset, aFrame->GetSize());

  // Record that we need to reset the builder's state on destruction.
  mBuilderToReset = aBuilder;
}

void nsDisplayListBuilder::Linkifier::MaybeAppendLink(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame) {
  // Note that we may generate a link here even if the constructor bailed out
  // without updating aBuilder->mLinkURI/Dest, because it may have been set by
  // an ancestor that was associated with a link element.
  if (aBuilder->mLinkURI.IsEmpty() && aBuilder->mLinkDest.IsEmpty()) {
    return;
  }
  if (!mBuilderToReset) {
    // mBuilderToReset is only set for the link element's frame, so this is a
    // descendant of that frame. If it is entirely covered by that frame, a link
    // for it would be redundant.
    nsPoint offset;
    const nsIFrame* referenceFrame =
        aBuilder->FindReferenceFrameFor(aFrame, &offset);
    if (referenceFrame == aBuilder->mLinkReferenceFrame &&
        aBuilder->mLinkBounds.Contains(nsRect(offset, aFrame->GetSize()))) {
      return;
    }
  }
  auto* link = MakeDisplayItem<nsDisplayLink>(
      aBuilder, aFrame, aBuilder->mLinkDest.get(), aBuilder->mLinkURI.get(),
      aFrame->GetRect());
  mList->AppendToTop(link);
}

uint32_t nsDisplayListBuilder::sPaintSequenceNumber(1);

nsDisplayListBuilder::nsDisplayListBuilder(nsIFrame* aReferenceFrame,
                                           nsDisplayListBuilderMode aMode,
                                           bool aBuildCaret,
                                           bool aRetainingDisplayList)
    : mReferenceFrame(aReferenceFrame),
      mIgnoreScrollFrame(nullptr),
      mCurrentActiveScrolledRoot(nullptr),
      mCurrentContainerASR(nullptr),
      mCurrentFrame(aReferenceFrame),
      mCurrentReferenceFrame(aReferenceFrame),
      mScrollInfoItemsForHoisting(nullptr),
      mFirstClipChainToDestroy(nullptr),
      mTableBackgroundSet(nullptr),
      mCurrentScrollParentId(ScrollableLayerGuid::NULL_SCROLL_ID),
      mCurrentScrollbarTarget(ScrollableLayerGuid::NULL_SCROLL_ID),
      mFilterASR(nullptr),
      mDirtyRect(-1, -1, -1, -1),
      mMode(aMode),
      mIsBuildingScrollbar(false),
      mCurrentScrollbarWillHaveLayer(false),
      mBuildCaret(aBuildCaret),
      mRetainingDisplayList(aRetainingDisplayList),
      mPartialUpdate(false),
      mIgnoreSuppression(false),
      mIncludeAllOutOfFlows(false),
      mDescendIntoSubdocuments(true),
      mSelectedFramesOnly(false),
      mAllowMergingAndFlattening(true),
      mInTransform(false),
      mInEventsOnly(false),
      mInFilter(false),
      mInViewTransitionCapture(false),
      mIsInChromePresContext(false),
      mSyncDecodeImages(false),
      mIsPaintingToWindow(false),
      mAsyncPanZoomEnabled(nsLayoutUtils::AsyncPanZoomEnabled(aReferenceFrame)),
      mUseHighQualityScaling(false),
      mIsPaintingForWebRender(false),
      mAncestorHasApzAwareEventHandler(false),
      mHaveScrollableDisplayPort(false),
      mWindowDraggingAllowed(false),
      mIsBuildingForPopup(nsLayoutUtils::IsPopup(aReferenceFrame)),
      mForceLayerForScrollParent(false),
      mContainsNonMinimalDisplayPort(false),
      mBuildingInvisibleItems(false),
      mIsBuilding(false),
      mInInvalidSubtree(false),
      mDisablePartialUpdates(false),
      mPartialBuildFailed(false),
      mIsInActiveDocShell(false),
      mBuildAsyncZoomContainer(false),
      mIsRelativeToLayoutViewport(false),
      mUseOverlayScrollbars(false),
      mAlwaysLayerizeScrollbars(false) {
  MOZ_COUNT_CTOR(nsDisplayListBuilder);

  ShouldRebuildDisplayListDueToPrefChange();

  mUseOverlayScrollbars =
      !!LookAndFeel::GetInt(LookAndFeel::IntID::UseOverlayScrollbars);

  mAlwaysLayerizeScrollbars =
      StaticPrefs::layout_scrollbars_always_layerize_track();

  static_assert(
      static_cast<uint32_t>(DisplayItemType::TYPE_MAX) < (1 << TYPE_BITS),
      "Check TYPE_MAX should not overflow");

  mIsReusingStackingContextItems =
      mRetainingDisplayList && StaticPrefs::layout_display_list_retain_sc();
}

void nsDisplayListBuilder::BeginFrame() {
  mIsPaintingToWindow = false;
  mUseHighQualityScaling = false;
  mIgnoreSuppression = false;
  mInTransform = false;
  mInFilter = false;
  mSyncDecodeImages = false;
}

void nsDisplayListBuilder::EndFrame() {
  NS_ASSERTION(!mInInvalidSubtree,
               "Someone forgot to cleanup mInInvalidSubtree!");
  mCurrentContainerASR = nullptr;
  mActiveScrolledRoots.Clear();
  FreeClipChains();
  FreeTemporaryItems();
  mAsyncScrollsWithAnchor.Clear();
}

void nsDisplayListBuilder::MarkFrameForDisplay(nsIFrame* aFrame,
                                               const nsIFrame* aStopAtFrame) {
  mFramesMarkedForDisplay.AppendElement(aFrame);
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(f)) {
    if (f->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO)) {
      return;
    }
    f->AddStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO);
    if (f == aStopAtFrame) {
      // we've reached a frame that we know will be painted, so we can stop.
      break;
    }
  }
}

void nsDisplayListBuilder::AddFrameMarkedForDisplayIfVisible(nsIFrame* aFrame) {
  mFramesMarkedForDisplayIfVisible.AppendElement(aFrame);
}

static void MarkFrameForDisplayIfVisibleInternal(nsIFrame* aFrame,
                                                 const nsIFrame* aStopAtFrame) {
  for (nsIFrame* f = aFrame; f; f = nsLayoutUtils::GetDisplayListParent(f)) {
    if (f->ForceDescendIntoIfVisible()) {
      return;
    }
    f->SetForceDescendIntoIfVisible(true);

    // This condition must match the condition in
    // nsLayoutUtils::GetParentOrPlaceholderFor which is used by
    // nsLayoutUtils::GetDisplayListParent
    if (f->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) && !f->GetPrevInFlow()) {
      nsIFrame* parent = f->GetParent();
      if (parent && !parent->ForceDescendIntoIfVisible()) {
        // If the GetDisplayListParent call is going to walk to a placeholder,
        // in rare cases the placeholder might be contained in a different
        // continuation from the oof. So we have to make sure to mark the oofs
        // parent. In the common case this doesn't make us do any extra work,
        // just changes the order in which we visit the frames since walking
        // through placeholders will walk through the parent, and we stop when
        // we find a ForceDescendIntoIfVisible bit set.
        MarkFrameForDisplayIfVisibleInternal(parent, aStopAtFrame);
      }
    }

    if (f == aStopAtFrame) {
      // we've reached a frame that we know will be painted, so we can stop.
      break;
    }
  }
}

void nsDisplayListBuilder::MarkFrameForDisplayIfVisible(
    nsIFrame* aFrame, const nsIFrame* aStopAtFrame) {
  AddFrameMarkedForDisplayIfVisible(aFrame);

  MarkFrameForDisplayIfVisibleInternal(aFrame, aStopAtFrame);
}

void nsDisplayListBuilder::SetIsRelativeToLayoutViewport() {
  mIsRelativeToLayoutViewport = true;
  UpdateShouldBuildAsyncZoomContainer();
}

void nsDisplayListBuilder::ForceLayerForScrollParent() {
  mForceLayerForScrollParent = true;
  mNumActiveScrollframesEncountered++;
}

void nsDisplayListBuilder::UpdateShouldBuildAsyncZoomContainer() {
  const Document* document = mReferenceFrame->PresContext()->Document();
  mBuildAsyncZoomContainer = !mIsRelativeToLayoutViewport &&
                             !document->Fullscreen() &&
                             nsLayoutUtils::AllowZoomingForDocument(document);

  // If mIsRelativeToLayoutViewport == false, hit-testing on this
  // display list will take into account the pres shell resolution.
  // If we're not building an async zoom container (meaning, the
  // resolution will not take effect visually), the resolution better
  // be 1.0, otherwise rendering and hit-testing are out of sync.
#ifdef DEBUG
  if (!mIsRelativeToLayoutViewport && !mBuildAsyncZoomContainer) {
    MOZ_ASSERT(document->GetPresShell()->GetResolution() == 1.0f);
  }
#endif
}

// Certain prefs may cause display list items to be added or removed when they
// are toggled. In those cases, we need to fully rebuild the display list.
bool nsDisplayListBuilder::ShouldRebuildDisplayListDueToPrefChange() {
  // If we transition between wrapping the RCD-RSF contents into an async
  // zoom container vs. not, we need to rebuild the display list. This only
  // happens when the zooming or container scrolling prefs are toggled
  // (manually by the user, or during test setup).
  bool didBuildAsyncZoomContainer = mBuildAsyncZoomContainer;
  UpdateShouldBuildAsyncZoomContainer();

  bool hadOverlayScrollbarsLastTime = mUseOverlayScrollbars;
  mUseOverlayScrollbars =
      !!LookAndFeel::GetInt(LookAndFeel::IntID::UseOverlayScrollbars);

  bool alwaysLayerizedScrollbarsLastTime = mAlwaysLayerizeScrollbars;
  mAlwaysLayerizeScrollbars =
      StaticPrefs::layout_scrollbars_always_layerize_track();

  bool oldShouldActivateAllScrollFrames = mShouldActivateAllScrollFrames;
  mShouldActivateAllScrollFrames =
      ScrollContainerFrame::ShouldActivateAllScrollFrames(nullptr,
                                                          mReferenceFrame);

  if (didBuildAsyncZoomContainer != mBuildAsyncZoomContainer) {
    return true;
  }

  if (hadOverlayScrollbarsLastTime != mUseOverlayScrollbars) {
    return true;
  }

  if (alwaysLayerizedScrollbarsLastTime != mAlwaysLayerizeScrollbars) {
    return true;
  }

  if (oldShouldActivateAllScrollFrames != mShouldActivateAllScrollFrames) {
    return true;
  }

  return false;
}

void nsDisplayListBuilder::AddScrollContainerFrameToNotify(
    ScrollContainerFrame* aScrollContainerFrame) {
  mScrollContainerFramesToNotify.insert(aScrollContainerFrame);
}

void nsDisplayListBuilder::NotifyAndClearScrollContainerFrames() {
  for (const auto& it : mScrollContainerFramesToNotify) {
    it->NotifyApzTransaction();
  }
  mScrollContainerFramesToNotify.clear();
}

bool nsDisplayListBuilder::MarkOutOfFlowFrameForDisplay(
    nsIFrame* aDirtyFrame, nsIFrame* aFrame, const nsRect& aVisibleRect,
    const nsRect& aDirtyRect) {
  MOZ_ASSERT(aFrame->GetParent() == aDirtyFrame);
  nsRect dirty;
  nsRect visible = OutOfFlowDisplayData::ComputeVisibleRectForFrame(
      this, aFrame, aVisibleRect, aDirtyRect, &dirty);
  if (!aFrame->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO) &&
      visible.IsEmpty()) {
    return false;
  }

  // Only MarkFrameForDisplay if we're dirty. If this is a nested out-of-flow
  // frame, then it will also mark any outer frames to ensure that building
  // reaches the dirty feame.
  if (!dirty.IsEmpty() || aFrame->ForceDescendIntoIfVisible()) {
    MarkFrameForDisplay(aFrame, aDirtyFrame);
  }

  return true;
}

static void UnmarkFrameForDisplay(nsIFrame* aFrame,
                                  const nsIFrame* aStopAtFrame) {
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(f)) {
    if (!f->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO)) {
      return;
    }
    f->RemoveStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO);
    if (f == aStopAtFrame) {
      // we've reached a frame that we know will be painted, so we can stop.
      break;
    }
  }
}

static void UnmarkFrameForDisplayIfVisible(nsIFrame* aFrame) {
  for (nsIFrame* f = aFrame; f; f = nsLayoutUtils::GetDisplayListParent(f)) {
    if (!f->ForceDescendIntoIfVisible()) {
      return;
    }
    f->SetForceDescendIntoIfVisible(false);

    // This condition must match the condition in
    // nsLayoutUtils::GetParentOrPlaceholderFor which is used by
    // nsLayoutUtils::GetDisplayListParent
    if (f->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) && !f->GetPrevInFlow()) {
      nsIFrame* parent = f->GetParent();
      if (parent && parent->ForceDescendIntoIfVisible()) {
        // If the GetDisplayListParent call is going to walk to a placeholder,
        // in rare cases the placeholder might be contained in a different
        // continuation from the oof. So we have to make sure to mark the oofs
        // parent. In the common case this doesn't make us do any extra work,
        // just changes the order in which we visit the frames since walking
        // through placeholders will walk through the parent, and we stop when
        // we find a ForceDescendIntoIfVisible bit set.
        UnmarkFrameForDisplayIfVisible(f);
      }
    }
  }
}

nsDisplayListBuilder::~nsDisplayListBuilder() {
  NS_ASSERTION(mFramesMarkedForDisplay.Length() == 0,
               "All frames should have been unmarked");
  NS_ASSERTION(mFramesWithOOFData.Length() == 0,
               "All OOF data should have been removed");
  NS_ASSERTION(mPresShellStates.Length() == 0,
               "All presshells should have been exited");

  DisplayItemClipChain* c = mFirstClipChainToDestroy;
  while (c) {
    DisplayItemClipChain* next = c->mNextClipChainToDestroy;
    c->DisplayItemClipChain::~DisplayItemClipChain();
    DisplayListArena::Free(DisplayListArenaObjectId::CLIPCHAIN, c);
    c = next;
  }

  MOZ_COUNT_DTOR(nsDisplayListBuilder);
}

uint32_t nsDisplayListBuilder::GetBackgroundPaintFlags() {
  uint32_t flags = 0;
  if (mSyncDecodeImages) {
    flags |= nsCSSRendering::PAINTBG_SYNC_DECODE_IMAGES;
  }
  if (mIsPaintingToWindow) {
    flags |= nsCSSRendering::PAINTBG_TO_WINDOW;
  }
  if (mUseHighQualityScaling) {
    flags |= nsCSSRendering::PAINTBG_HIGH_QUALITY_SCALING;
  }
  return flags;
}

// TODO(emilio): Maybe unify BackgroundPaintFlags and IamgeRendererFlags.
uint32_t nsDisplayListBuilder::GetImageRendererFlags() const {
  uint32_t flags = 0;
  if (mSyncDecodeImages) {
    flags |= nsImageRenderer::FLAG_SYNC_DECODE_IMAGES;
  }
  if (mIsPaintingToWindow) {
    flags |= nsImageRenderer::FLAG_PAINTING_TO_WINDOW;
  }
  if (mUseHighQualityScaling) {
    flags |= nsImageRenderer::FLAG_HIGH_QUALITY_SCALING;
  }
  return flags;
}

uint32_t nsDisplayListBuilder::GetImageDecodeFlags() const {
  uint32_t flags = imgIContainer::FLAG_ASYNC_NOTIFY;
  if (mSyncDecodeImages) {
    flags |= imgIContainer::FLAG_SYNC_DECODE;
  } else {
    flags |= imgIContainer::FLAG_SYNC_DECODE_IF_FAST;
  }
  if (mIsPaintingToWindow || mUseHighQualityScaling) {
    flags |= imgIContainer::FLAG_HIGH_QUALITY_SCALING;
  }
  return flags;
}

nsCaret* nsDisplayListBuilder::GetCaret() {
  RefPtr<nsCaret> caret = CurrentPresShellState()->mPresShell->GetActiveCaret();
  return caret;
}

void nsDisplayListBuilder::IncrementPresShellPaintCount(PresShell* aPresShell) {
  if (mIsPaintingToWindow) {
    aPresShell->IncrementPaintCount();
  }
}

void nsDisplayListBuilder::EnterPresShell(const nsIFrame* aReferenceFrame,
                                          bool aPointerEventsNoneDoc) {
  nsCSSRendering::PresShellChanged();

  PresShellState* state = mPresShellStates.AppendElement();
  state->mPresShell = aReferenceFrame->PresShell();
  state->mFirstFrameMarkedForDisplay = mFramesMarkedForDisplay.Length();
  state->mFirstFrameWithOOFData = mFramesWithOOFData.Length();

  ScrollContainerFrame* sf = state->mPresShell->GetRootScrollContainerFrame();
  if (sf && IsInSubdocument()) {
    // We are forcing a rebuild of nsDisplayCanvasBackgroundColor to make sure
    // that the canvas background color will be set correctly, and that only one
    // unscrollable item will be created.
    // This is done to avoid, for example, a case where only scrollbar frames
    // are invalidated - we would skip creating nsDisplayCanvasBackgroundColor
    // and possibly end up with an extra nsDisplaySolidColor item.
    // We skip this for the root document, since we don't want to use
    // MarkFrameForDisplayIfVisible before ComputeRebuildRegion. We'll
    // do it manually there.
    nsCanvasFrame* canvasFrame = do_QueryFrame(sf->GetScrolledFrame());
    if (canvasFrame) {
      MarkFrameForDisplayIfVisible(canvasFrame, aReferenceFrame);
    }
  }

#ifdef DEBUG
  state->mAutoLayoutPhase.emplace(aReferenceFrame->PresContext(),
                                  nsLayoutPhase::DisplayListBuilding);
#endif

  if (!IsForEventDelivery()) {
    state->mPresShell->UpdateCanvasBackground();
  }

  bool buildCaret = mBuildCaret;
  if (mIgnoreSuppression || !state->mPresShell->IsPaintingSuppressed()) {
    state->mIsBackgroundOnly = false;
  } else {
    state->mIsBackgroundOnly = true;
    buildCaret = false;
  }

  bool pointerEventsNone = aPointerEventsNoneDoc;
  if (IsInSubdocument()) {
    pointerEventsNone |= mPresShellStates[mPresShellStates.Length() - 2]
                             .mInsidePointerEventsNoneDoc;
  }
  state->mInsidePointerEventsNoneDoc = pointerEventsNone;

  state->mPresShellIgnoreScrollFrame =
      state->mPresShell->IgnoringViewportScrolling()
          ? state->mPresShell->GetRootScrollContainerFrame()
          : nullptr;

  nsPresContext* pc = aReferenceFrame->PresContext();
  mIsInChromePresContext = pc->IsChrome();
  nsIDocShell* docShell = pc->GetDocShell();

  if (docShell) {
    docShell->GetWindowDraggingAllowed(&mWindowDraggingAllowed);
  }

  state->mTouchEventPrefEnabledDoc = dom::TouchEvent::PrefEnabled(docShell);

  if (auto* vt = pc->Document()->GetActiveViewTransition()) {
    // We must ensure captured view transition elements, including offscreen
    // elements, are reached for display list building.
    AutoTArray<nsIFrame*, 32> capturedFrames;
    vt->GetCapturedFrames(capturedFrames);
    for (const auto& frame : capturedFrames) {
      MarkFrameForDisplay(frame, aReferenceFrame);
    }
  }

  if (!buildCaret) {
    return;
  }

  state->mCaretFrame = [&]() -> nsIFrame* {
    RefPtr<nsCaret> caret = state->mPresShell->GetActiveCaret();
    nsIFrame* currentCaret = caret->GetPaintGeometry(&mCaretRect);
    if (!currentCaret) {
      return nullptr;
    }

    // Check if the display root for the caret matches the display root that
    // we're painting, and only use it if it matches. Likely we only need this
    // for carets inside popups.
    if (nsLayoutUtils::GetDisplayRootFrame(currentCaret) !=
        nsLayoutUtils::GetDisplayRootFrame(aReferenceFrame)) {
      return nullptr;
    }

    // Caret frames add visual area to their frame, but we don't update the
    // overflow area. Use flags to make sure we build display items for that
    // frame instead.
    MOZ_ASSERT(currentCaret->PresShell() == state->mPresShell);
    MarkFrameForDisplay(currentCaret, aReferenceFrame);
    caret->SetLastPaintedFrame(currentCaret);
    if (!mPaintedCarets.Contains(caret)) {
      mPaintedCarets.AppendElement(std::move(caret));
    }
    return currentCaret;
  }();
}

// A non-blank paint is a paint that does not just contain the canvas
// background.
static bool DisplayListIsNonBlank(nsDisplayList* aList) {
  for (nsDisplayItem* i : *aList) {
    switch (i->GetType()) {
      case DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO:
      case DisplayItemType::TYPE_CANVAS_BACKGROUND_IMAGE:
        continue;
      case DisplayItemType::TYPE_SOLID_COLOR:
      case DisplayItemType::TYPE_BACKGROUND:
      case DisplayItemType::TYPE_BACKGROUND_COLOR:
        if (i->Frame()->IsCanvasFrame()) {
          continue;
        }
        return true;
      default:
        return true;
    }
  }
  return false;
}

// A contentful paint is a paint that does contains DOM content (text,
// images, non-blank canvases, SVG): "First Contentful Paint entry
// contains a DOMHighResTimeStamp reporting the time when the browser
// first rendered any text, image (including background images),
// non-white canvas or SVG. This excludes any content of iframes, but
// includes text with pending webfonts. This is the first time users
// could start consuming page content."
static bool DisplayListIsContentful(nsDisplayListBuilder* aBuilder,
                                    nsDisplayList* aList) {
  for (nsDisplayItem* i : *aList) {
    DisplayItemType type = i->GetType();
    nsDisplayList* children = i->GetChildren();

    switch (type) {
      case DisplayItemType::TYPE_SUBDOCUMENT:  // iframes are ignored
        break;
      // CANVASes check if they may have been modified (as a stand-in
      // actually tracking all modifications)
      default:
        if (i->IsContentful()) {
          bool dummy;
          nsRect bound = i->GetBounds(aBuilder, &dummy);
          if (!bound.IsEmpty()) {
            return true;
          }
        }
        if (children) {
          if (DisplayListIsContentful(aBuilder, children)) {
            return true;
          }
        }
        break;
    }
  }
  return false;
}

void nsDisplayListBuilder::LeavePresShell(const nsIFrame* aReferenceFrame,
                                          nsDisplayList* aPaintedContents) {
  NS_ASSERTION(
      CurrentPresShellState()->mPresShell == aReferenceFrame->PresShell(),
      "Presshell mismatch");

  nsCSSRendering::PresShellChanged();
  if (mIsPaintingToWindow && aPaintedContents) {
    nsPresContext* pc = aReferenceFrame->PresContext();
    if (!pc->HadNonBlankPaint()) {
      if (!CurrentPresShellState()->mIsBackgroundOnly &&
          DisplayListIsNonBlank(aPaintedContents)) {
        pc->NotifyNonBlankPaint();
      }
    }
    nsRootPresContext* rootPresContext = pc->GetRootPresContext();
    // NotifyContentfulPaint is responsible for dropping LCP entries that
    // arrive after scroll.
    if (rootPresContext &&
        (!pc->HadFirstContentfulPaint() || !pc->HasStoppedGeneratingLCP())) {
      if (!CurrentPresShellState()->mIsBackgroundOnly) {
        if (pc->HasEverBuiltInvisibleText() ||
            DisplayListIsContentful(this, aPaintedContents)) {
          pc->NotifyContentfulPaint();
        }
      }
    }
  }

  ResetMarkedFramesForDisplayList(aReferenceFrame);
  mPresShellStates.RemoveLastElement();

  if (!mPresShellStates.IsEmpty()) {
    nsPresContext* pc = CurrentPresContext();
    nsIDocShell* docShell = pc->GetDocShell();
    if (docShell) {
      docShell->GetWindowDraggingAllowed(&mWindowDraggingAllowed);
    }
    mIsInChromePresContext = pc->IsChrome();
  } else {
    for (uint32_t i = 0; i < mFramesMarkedForDisplayIfVisible.Length(); ++i) {
      UnmarkFrameForDisplayIfVisible(mFramesMarkedForDisplayIfVisible[i]);
    }
    mFramesMarkedForDisplayIfVisible.SetLength(0);
  }
}

void nsDisplayListBuilder::FreeClipChains() {
  // Iterate the clip chains from newest to oldest (forward
  // iteration), so that we destroy descendants first which
  // will drop the ref count on their ancestors.
  DisplayItemClipChain** indirect = &mFirstClipChainToDestroy;

  while (*indirect) {
    if (!(*indirect)->mRefCount) {
      DisplayItemClipChain* next = (*indirect)->mNextClipChainToDestroy;

      mClipDeduplicator.erase(*indirect);
      (*indirect)->DisplayItemClipChain::~DisplayItemClipChain();
      DisplayListArena::Free(DisplayListArenaObjectId::CLIPCHAIN, *indirect);

      *indirect = next;
    } else {
      indirect = &(*indirect)->mNextClipChainToDestroy;
    }
  }
}

void nsDisplayListBuilder::FreeTemporaryItems() {
  for (nsDisplayItem* i : mTemporaryItems) {
    // Temporary display items are not added to the frames.
    MOZ_ASSERT(i->Frame());
    i->RemoveFrame(i->Frame());
    i->Destroy(this);
  }

  mTemporaryItems.Clear();
}

void nsDisplayListBuilder::ResetMarkedFramesForDisplayList(
    const nsIFrame* aReferenceFrame) {
  // Unmark and pop off the frames marked for display in this pres shell.
  uint32_t firstFrameForShell =
      CurrentPresShellState()->mFirstFrameMarkedForDisplay;
  for (uint32_t i = firstFrameForShell; i < mFramesMarkedForDisplay.Length();
       ++i) {
    UnmarkFrameForDisplay(mFramesMarkedForDisplay[i], aReferenceFrame);
  }
  mFramesMarkedForDisplay.SetLength(firstFrameForShell);

  firstFrameForShell = CurrentPresShellState()->mFirstFrameWithOOFData;
  for (uint32_t i = firstFrameForShell; i < mFramesWithOOFData.Length(); ++i) {
    mFramesWithOOFData[i]->RemoveProperty(OutOfFlowDisplayDataProperty());
  }
  mFramesWithOOFData.SetLength(firstFrameForShell);
}

void nsDisplayListBuilder::ClearFixedBackgroundDisplayData() {
  CurrentPresShellState()->mFixedBackgroundDisplayData = Nothing();
}

void nsDisplayListBuilder::MarkFramesForDisplayList(
    nsIFrame* aDirtyFrame, const nsFrameList& aFrames) {
  nsRect visibleRect = GetVisibleRect();
  nsRect dirtyRect = GetDirtyRect();

  // If we are entering content that is fixed to the RCD-RSF, we are
  // crossing the async zoom container boundary, and need to convert from
  // visual to layout coordinates.
  if (ViewportFrame* viewportFrame = do_QueryFrame(aDirtyFrame)) {
    if (IsForEventDelivery() && ShouldBuildAsyncZoomContainer() &&
        viewportFrame->PresContext()->IsRootContentDocumentCrossProcess()) {
      if (viewportFrame->PresShell()->GetRootScrollContainerFrame()) {
#ifdef DEBUG
        for (nsIFrame* f : aFrames) {
          MOZ_ASSERT(ViewportUtils::IsZoomedContentRoot(f));
        }
#endif
        visibleRect = ViewportUtils::VisualToLayout(visibleRect,
                                                    viewportFrame->PresShell());
        dirtyRect = ViewportUtils::VisualToLayout(dirtyRect,
                                                  viewportFrame->PresShell());
      }
#ifdef DEBUG
      else {
        // This is an edge case that should only happen if we are in a
        // document with a XUL root element so that it does not have a root
        // scroll frame but it has fixed pos content and all of the frames in
        // aFrames are that fixed pos content.
        for (nsIFrame* f : aFrames) {
          MOZ_ASSERT(!ViewportUtils::IsZoomedContentRoot(f) &&
                     f->GetParent() == aDirtyFrame &&
                     f->StyleDisplay()->mPosition ==
                         StylePositionProperty::Fixed);
        }
        // There's no root scroll frame so there can't be any zooming or async
        // panning so we don't need to adjust the visible and dirty rects.
      }
#endif
    }
  }

  bool markedFrames = false;
  for (nsIFrame* e : aFrames) {
    // Skip the AccessibleCaret frame when building no caret.
    if (!IsBuildingCaret()) {
      nsIContent* content = e->GetContent();
      if (content && content->IsInNativeAnonymousSubtree() &&
          content->IsElement()) {
        const nsAttrValue* classes = content->AsElement()->GetClasses();
        if (classes &&
            classes->Contains(nsGkAtoms::mozAccessiblecaret, eCaseMatters)) {
          continue;
        }
      }
    }
    if (MarkOutOfFlowFrameForDisplay(aDirtyFrame, e, visibleRect, dirtyRect)) {
      markedFrames = true;
    }
  }

  if (markedFrames) {
    // mClipState.GetClipChainForContainingBlockDescendants can return pointers
    // to objects on the stack, so we need to clone the chain.
    const DisplayItemClipChain* clipChain =
        CopyWholeChain(mClipState.GetClipChainForContainingBlockDescendants());
    const DisplayItemClipChain* combinedClipChain =
        mClipState.GetCurrentCombinedClipChain(this);
    const ActiveScrolledRoot* asr = mCurrentActiveScrolledRoot;

    OutOfFlowDisplayData* data = new OutOfFlowDisplayData(
        clipChain, combinedClipChain, asr, mCurrentScrollParentId, visibleRect,
        dirtyRect, mInViewTransitionCapture);
    aDirtyFrame->SetProperty(
        nsDisplayListBuilder::OutOfFlowDisplayDataProperty(), data);
    mFramesWithOOFData.AppendElement(aDirtyFrame);
  }

  if (!aDirtyFrame->GetParent()) {
    // This is the viewport frame of aDirtyFrame's presshell.
    // Store the current display data so that it can be used for fixed
    // background images.
    NS_ASSERTION(
        CurrentPresShellState()->mPresShell == aDirtyFrame->PresShell(),
        "Presshell mismatch");
    MOZ_ASSERT(!CurrentPresShellState()->mFixedBackgroundDisplayData,
               "already traversed this presshell's root frame?");

    const DisplayItemClipChain* clipChain =
        CopyWholeChain(mClipState.GetClipChainForContainingBlockDescendants());
    const DisplayItemClipChain* combinedClipChain =
        mClipState.GetCurrentCombinedClipChain(this);
    const ActiveScrolledRoot* asr = mCurrentActiveScrolledRoot;
    CurrentPresShellState()->mFixedBackgroundDisplayData.emplace(
        clipChain, combinedClipChain, asr, mCurrentScrollParentId,
        GetVisibleRect(), GetDirtyRect(), mInViewTransitionCapture);
  }
}

/**
 * Mark all preserve-3d children with
 * NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO to make sure
 * nsIFrame::BuildDisplayListForChild() would visit them.  Also compute
 * dirty rect for preserve-3d children.
 *
 * @param aDirtyFrame is the frame to mark children extending context.
 */
void nsDisplayListBuilder::MarkPreserve3DFramesForDisplayList(
    nsIFrame* aDirtyFrame) {
  for (const auto& childList : aDirtyFrame->ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      if (child->Combines3DTransformWithAncestors()) {
        MarkFrameForDisplay(child, aDirtyFrame);
      }

      if (child->IsBlockWrapper()) {
        // Mark preserve-3d frames inside the block wrapper.
        MarkPreserve3DFramesForDisplayList(child);
      }
    }
  }
}

ActiveScrolledRoot* nsDisplayListBuilder::GetOrCreateActiveScrolledRoot(
    const ActiveScrolledRoot* aParent, nsIFrame* aFrame,
    ActiveScrolledRoot::ASRKind asrKind) {
  RefPtr<ActiveScrolledRoot> asr = ActiveScrolledRoot::GetOrCreateASRForFrame(
      aParent, aFrame, mActiveScrolledRoots, asrKind);
  return asr;
}

const DisplayItemClipChain* nsDisplayListBuilder::AllocateDisplayItemClipChain(
    const DisplayItemClip& aClip, const ActiveScrolledRoot* aASR,
    const DisplayItemClipChain* aParent) {
  MOZ_DIAGNOSTIC_ASSERT(!(aParent && aParent->mOnStack));
  void* p = DisplayListArena::Allocate(DisplayListArenaObjectId::CLIPCHAIN,
                                       sizeof(DisplayItemClipChain));
  DisplayItemClipChain* c = new (KnownNotNull, p)
      DisplayItemClipChain(aClip, aASR, aParent, mFirstClipChainToDestroy);
#if defined(DEBUG) || defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  c->mOnStack = false;
#endif
  auto result = mClipDeduplicator.insert(c);
  if (!result.second) {
    // An equivalent clip chain item was already created, so let's return that
    // instead. Destroy the one we just created.
    // Note that this can cause clip chains from different coordinate systems to
    // collapse into the same clip chain object, because clip chains do not keep
    // track of the reference frame that they were created in.
    c->DisplayItemClipChain::~DisplayItemClipChain();
    DisplayListArena::Free(DisplayListArenaObjectId::CLIPCHAIN, c);
    return *(result.first);
  }
  mFirstClipChainToDestroy = c;
  return c;
}

struct ClipChainItem {
  DisplayItemClip clip;
  const ActiveScrolledRoot* asr;
};

static const DisplayItemClipChain* FindCommonAncestorClipForIntersection(
    const DisplayItemClipChain* aOne, const DisplayItemClipChain* aTwo) {
  for (const ActiveScrolledRoot* asr =
           ActiveScrolledRoot::PickDescendant(aOne->mASR, aTwo->mASR);
       asr; asr = asr->mParent) {
    if (aOne == aTwo) {
      return aOne;
    }
    if (aOne->mASR == asr) {
      aOne = aOne->mParent;
    }
    if (aTwo->mASR == asr) {
      aTwo = aTwo->mParent;
    }
    if (!aOne) {
      return aTwo;
    }
    if (!aTwo) {
      return aOne;
    }
  }
  return nullptr;
}

const DisplayItemClipChain* nsDisplayListBuilder::CreateClipChainIntersection(
    const DisplayItemClipChain* aAncestor,
    const DisplayItemClipChain* aLeafClip1,
    const DisplayItemClipChain* aLeafClip2) {
  AutoTArray<ClipChainItem, 8> intersectedClips;

  const DisplayItemClipChain* clip1 = aLeafClip1;
  const DisplayItemClipChain* clip2 = aLeafClip2;

  const ActiveScrolledRoot* asr = ActiveScrolledRoot::PickDescendant(
      clip1 ? clip1->mASR : nullptr, clip2 ? clip2->mASR : nullptr);

  // Build up the intersection from the leaf to the root and put it into
  // intersectedClips. The loop below will convert intersectedClips into an
  // actual DisplayItemClipChain.
  // (We need to do this in two passes because we need the parent clip in order
  // to create the DisplayItemClipChain object, but the parent clip has not
  // been created at that point.)
  while (!aAncestor || asr != aAncestor->mASR) {
    if (clip1 && clip1->mASR == asr) {
      if (clip2 && clip2->mASR == asr) {
        DisplayItemClip intersection = clip1->mClip;
        intersection.IntersectWith(clip2->mClip);
        intersectedClips.AppendElement(
            ClipChainItem{std::move(intersection), asr});
        clip2 = clip2->mParent;
      } else {
        intersectedClips.AppendElement(ClipChainItem{clip1->mClip, asr});
      }
      clip1 = clip1->mParent;
    } else if (clip2 && clip2->mASR == asr) {
      intersectedClips.AppendElement(ClipChainItem{clip2->mClip, asr});
      clip2 = clip2->mParent;
    }
    if (!asr) {
      MOZ_ASSERT(!aAncestor, "We should have exited this loop earlier");
      break;
    }
    asr = asr->mParent;
  }

  // Convert intersectedClips into a DisplayItemClipChain.
  const DisplayItemClipChain* parentSC = aAncestor;
  for (auto& sc : Reversed(intersectedClips)) {
    parentSC = AllocateDisplayItemClipChain(sc.clip, sc.asr, parentSC);
  }
  return parentSC;
}

const DisplayItemClipChain* nsDisplayListBuilder::CreateClipChainIntersection(
    const DisplayItemClipChain* aLeafClip1,
    const DisplayItemClipChain* aLeafClip2) {
  // aLeafClip2 might be a reference to a clip on the stack. We need to make
  // sure that CreateClipChainIntersection will allocate the actual intersected
  // clip in the builder's arena, so for the aLeafClip1 == nullptr case,
  // we supply nullptr as the common ancestor so that
  // CreateClipChainIntersection clones the whole chain.
  const DisplayItemClipChain* ancestorClip =
      aLeafClip1 ? FindCommonAncestorClipForIntersection(aLeafClip1, aLeafClip2)
                 : nullptr;

  return CreateClipChainIntersection(ancestorClip, aLeafClip1, aLeafClip2);
}

const DisplayItemClipChain* nsDisplayListBuilder::CopyWholeChain(
    const DisplayItemClipChain* aClipChain) {
  return CreateClipChainIntersection(nullptr, aClipChain, nullptr);
}

const nsIFrame* nsDisplayListBuilder::FindReferenceFrameFor(
    const nsIFrame* aFrame, nsPoint* aOffset) const {
  auto MaybeApplyAdditionalOffset = [&]() {
    if (auto offset = AdditionalOffset()) {
      *aOffset += *offset;
    }
  };

  if (aFrame == mCurrentFrame) {
    if (aOffset) {
      *aOffset = mCurrentOffsetToReferenceFrame;
    }
    return mCurrentReferenceFrame;
  }

  for (const nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    if (f == mReferenceFrame || f->IsTransformed()) {
      if (aOffset) {
        *aOffset = aFrame->GetOffsetToCrossDoc(f);
        MaybeApplyAdditionalOffset();
      }
      return f;
    }
  }

  if (aOffset) {
    *aOffset = aFrame->GetOffsetToCrossDoc(mReferenceFrame);
    MaybeApplyAdditionalOffset();
  }

  return mReferenceFrame;
}

// Sticky frames are active if their nearest scrollable frame is also active.
static bool IsStickyFrameActive(nsDisplayListBuilder* aBuilder,
                                nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->StyleDisplay()->mPosition ==
             StylePositionProperty::Sticky);

  auto* ssc = StickyScrollContainer::GetOrCreateForFrame(aFrame);
  return ssc && ssc->ScrollContainer()->IsMaybeAsynchronouslyScrolled();
}

bool nsDisplayListBuilder::IsAnimatedGeometryRoot(nsIFrame* aFrame,
                                                  nsIFrame** aParent) {
  if (aFrame == mReferenceFrame) {
    return true;
  }

  if (!IsPaintingToWindow()) {
    if (aParent) {
      *aParent = nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
    }
    return false;
  }

  nsIFrame* parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
  if (!parent) {
    return true;
  }
  *aParent = parent;

  if (aFrame->StyleDisplay()->mPosition == StylePositionProperty::Sticky &&
      IsStickyFrameActive(this, aFrame)) {
    return true;
  }

  if (aFrame->IsTransformed()) {
    if (EffectCompositor::HasAnimationsForCompositor(
            aFrame, DisplayItemType::TYPE_TRANSFORM)) {
      return true;
    }
  }

  if (parent->IsScrollContainerOrSubclass()) {
    ScrollContainerFrame* sf = do_QueryFrame(parent);
    if (sf->GetScrolledFrame() == aFrame) {
      MOZ_ASSERT(!aFrame->IsTransformed());
      return sf->IsMaybeAsynchronouslyScrolled();
    }
  }

  return false;
}

nsIFrame* nsDisplayListBuilder::FindAnimatedGeometryRootFrameFor(
    nsIFrame* aFrame) {
  MOZ_ASSERT(nsLayoutUtils::IsAncestorFrameCrossDocInProcess(
      RootReferenceFrame(), aFrame));
  nsIFrame* cursor = aFrame;
  while (cursor != RootReferenceFrame()) {
    nsIFrame* next;
    if (IsAnimatedGeometryRoot(cursor, &next)) {
      return cursor;
    }
    cursor = next;
  }
  return cursor;
}

static nsRect ApplyAllClipNonRoundedIntersection(
    const DisplayItemClipChain* aClipChain, const nsRect& aRect) {
  nsRect result = aRect;
  while (aClipChain) {
    result = aClipChain->mClip.ApplyNonRoundedIntersection(result);
    aClipChain = aClipChain->mParent;
  }
  return result;
}

void nsDisplayListBuilder::AdjustWindowDraggingRegion(nsIFrame* aFrame) {
  if (!mWindowDraggingAllowed || !IsForPainting()) {
    return;
  }

  const nsStyleUIReset* styleUI = aFrame->StyleUIReset();
  if (styleUI->mWindowDragging == StyleWindowDragging::Default) {
    // This frame has the default value and doesn't influence the window
    // dragging region.
    return;
  }

  if (!aFrame->StyleVisibility()->IsVisible()) {
    // Invisible frames don't influence the window dragging region.
    return;
  }

  LayoutDeviceToLayoutDeviceMatrix4x4 referenceFrameToRootReferenceFrame;

  // The const_cast is for nsLayoutUtils::GetTransformToAncestor.
  nsIFrame* referenceFrame =
      const_cast<nsIFrame*>(FindReferenceFrameFor(aFrame));

  if (IsInTransform()) {
    // Only support 2d rectilinear transforms. Transform support is needed for
    // the horizontal flip transform that's applied to the urlbar textbox in
    // RTL mode - it should be able to exclude itself from the draggable region.
    referenceFrameToRootReferenceFrame =
        ViewAs<LayoutDeviceToLayoutDeviceMatrix4x4>(
            nsLayoutUtils::GetTransformToAncestor(RelativeTo{referenceFrame},
                                                  RelativeTo{mReferenceFrame})
                .GetMatrix());
    Matrix referenceFrameToRootReferenceFrame2d;
    if (!referenceFrameToRootReferenceFrame.Is2D(
            &referenceFrameToRootReferenceFrame2d) ||
        !referenceFrameToRootReferenceFrame2d.IsRectilinear()) {
      return;
    }
  } else {
    MOZ_ASSERT(referenceFrame == mReferenceFrame,
               "referenceFrameToRootReferenceFrame needs to be adjusted");
  }

  // We do some basic visibility checking on the frame's border box here.
  // We intersect it both with the current dirty rect and with the current
  // clip. Either one is just a conservative approximation on its own, but
  // their intersection luckily works well enough for our purposes, so that
  // we don't have to do full-blown visibility computations.
  // The most important case we need to handle is the scrolled-off tab:
  // If the tab bar overflows, tab parts that are clipped by the scrollbox
  // should not be allowed to interfere with the window dragging region. Using
  // just the current DisplayItemClip is not enough to cover this case
  // completely because clips are reset while building stacking context
  // contents, so for example we'd fail to clip frames that have a clip path
  // applied to them. But the current dirty rect doesn't get reset in that
  // case, so we use it to make this case work.
  nsRect borderBox = aFrame->GetRectRelativeToSelf().Intersect(mVisibleRect);
  borderBox += ToReferenceFrame(aFrame);
  const DisplayItemClipChain* clip =
      ClipState().GetCurrentCombinedClipChain(this);
  borderBox = ApplyAllClipNonRoundedIntersection(clip, borderBox);
  if (borderBox.IsEmpty()) {
    return;
  }

  LayoutDeviceRect devPixelBorderBox = LayoutDevicePixel::FromAppUnits(
      borderBox, aFrame->PresContext()->AppUnitsPerDevPixel());

  LayoutDeviceRect transformedDevPixelBorderBox =
      TransformBy(referenceFrameToRootReferenceFrame, devPixelBorderBox);
  transformedDevPixelBorderBox.Round();
  LayoutDeviceIntRect transformedDevPixelBorderBoxInt;

  if (!transformedDevPixelBorderBox.ToIntRect(
          &transformedDevPixelBorderBoxInt)) {
    return;
  }

  LayoutDeviceIntRegion& region =
      styleUI->mWindowDragging == StyleWindowDragging::Drag
          ? mWindowDraggingRegion
          : mWindowNoDraggingRegion;

  if (!IsRetainingDisplayList()) {
    region.OrWith(transformedDevPixelBorderBoxInt);
    return;
  }

  gfx::IntRect rect(transformedDevPixelBorderBoxInt.ToUnknownRect());
  if (styleUI->mWindowDragging == StyleWindowDragging::Drag) {
    mRetainedWindowDraggingRegion.Add(aFrame, rect);
  } else {
    mRetainedWindowNoDraggingRegion.Add(aFrame, rect);
  }
}

LayoutDeviceIntRegion nsDisplayListBuilder::GetWindowDraggingRegion() const {
  LayoutDeviceIntRegion result;
  if (!IsRetainingDisplayList()) {
    result.Sub(mWindowDraggingRegion, mWindowNoDraggingRegion);
    return result;
  }

  LayoutDeviceIntRegion dragRegion =
      mRetainedWindowDraggingRegion.ToLayoutDeviceIntRegion();

  LayoutDeviceIntRegion noDragRegion =
      mRetainedWindowNoDraggingRegion.ToLayoutDeviceIntRegion();

  result.Sub(dragRegion, noDragRegion);
  return result;
}

void nsDisplayTransform::AddSizeOfExcludingThis(nsWindowSizes& aSizes) const {
  nsPaintedDisplayItem::AddSizeOfExcludingThis(aSizes);
  aSizes.mLayoutRetainedDisplayListSize +=
      aSizes.mState.mMallocSizeOf(mTransformPreserves3D.get());
}

void nsDisplayListBuilder::AddSizeOfExcludingThis(nsWindowSizes& aSizes) const {
  size_t n = 0;
  MallocSizeOf mallocSizeOf = aSizes.mState.mMallocSizeOf;
  n += mRetainedWindowDraggingRegion.SizeOfExcludingThis(mallocSizeOf);
  n += mRetainedWindowNoDraggingRegion.SizeOfExcludingThis(mallocSizeOf);
  n += mRetainedWindowOpaqueRegion.SizeOfExcludingThis(mallocSizeOf);
  // XXX can't measure mClipDeduplicator since it uses std::unordered_set.

  aSizes.mLayoutRetainedDisplayListSize += n;
}

void RetainedDisplayList::AddSizeOfExcludingThis(nsWindowSizes& aSizes) const {
  for (nsDisplayItem* item : *this) {
    item->AddSizeOfExcludingThis(aSizes);
    if (RetainedDisplayList* children = item->GetChildren()) {
      children->AddSizeOfExcludingThis(aSizes);
    }
  }

  size_t n = 0;

  n += mDAG.mDirectPredecessorList.ShallowSizeOfExcludingThis(
      aSizes.mState.mMallocSizeOf);
  n += mDAG.mNodesInfo.ShallowSizeOfExcludingThis(aSizes.mState.mMallocSizeOf);
  n += mOldItems.ShallowSizeOfExcludingThis(aSizes.mState.mMallocSizeOf);

  aSizes.mLayoutRetainedDisplayListSize += n;
}

size_t nsDisplayListBuilder::WeakFrameRegion::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = 0;
  n += mFrames.ShallowSizeOfExcludingThis(aMallocSizeOf);
  n += mRects.ShallowSizeOfExcludingThis(aMallocSizeOf);
  return n;
}

/**
 * Removes modified frames and rects from this WeakFrameRegion.
 */
void nsDisplayListBuilder::WeakFrameRegion::RemoveModifiedFramesAndRects() {
  MOZ_ASSERT(mFrames.Length() == mRects.Length());

  uint32_t i = 0;
  uint32_t length = mFrames.Length();

  while (i < length) {
    auto& [weakFrame, rawFrame] = mFrames[i];

    if (!weakFrame.IsAlive() ||
        AnyContentAncestorModified(weakFrame.GetFrame())) {
      // To avoid multiple O(n) shifts in the array, move the last element of
      // the array to the current position and decrease the array length.
      mFrameSet.Remove(rawFrame);
      mFrames[i] = std::move(mFrames[length - 1]);
      mRects[i] = std::move(mRects[length - 1]);
      length--;
    } else {
      i++;
    }
  }

  mFrames.TruncateLength(length);
  mRects.TruncateLength(length);
}

void nsDisplayListBuilder::RemoveModifiedWindowRegions() {
  mRetainedWindowDraggingRegion.RemoveModifiedFramesAndRects();
  mRetainedWindowNoDraggingRegion.RemoveModifiedFramesAndRects();
  mRetainedWindowOpaqueRegion.RemoveModifiedFramesAndRects();
}

void nsDisplayListBuilder::ClearRetainedWindowRegions() {
  mRetainedWindowDraggingRegion.Clear();
  mRetainedWindowNoDraggingRegion.Clear();
  mRetainedWindowOpaqueRegion.Clear();
}

void nsDisplayListBuilder::EnterSVGEffectsContents(
    nsIFrame* aEffectsFrame, nsDisplayList* aHoistedItemsStorage) {
  MOZ_ASSERT(aHoistedItemsStorage);
  if (mSVGEffectsFrames.IsEmpty()) {
    MOZ_ASSERT(!mScrollInfoItemsForHoisting);
    mScrollInfoItemsForHoisting = aHoistedItemsStorage;
  }
  mSVGEffectsFrames.AppendElement(aEffectsFrame);
}

void nsDisplayListBuilder::ExitSVGEffectsContents() {
  MOZ_ASSERT(!mSVGEffectsFrames.IsEmpty());
  mSVGEffectsFrames.RemoveLastElement();
  MOZ_ASSERT(mScrollInfoItemsForHoisting);
  if (mSVGEffectsFrames.IsEmpty()) {
    mScrollInfoItemsForHoisting = nullptr;
  }
}

bool nsDisplayListBuilder::ShouldBuildScrollInfoItemsForHoisting() const {
  /*
   * Note: if changing the conditions under which scroll info layers
   * are created, make a corresponding change to
   * ScrollFrameWillBuildScrollInfoLayer() in nsSliderFrame.cpp.
   */
  for (nsIFrame* frame : mSVGEffectsFrames) {
    if (SVGIntegrationUtils::UsesSVGEffectsNotSupportedInCompositor(frame)) {
      return true;
    }
  }
  return false;
}

void nsDisplayListBuilder::AppendNewScrollInfoItemForHoisting(
    nsDisplayScrollInfoLayer* aScrollInfoItem) {
  MOZ_ASSERT(ShouldBuildScrollInfoItemsForHoisting());
  MOZ_ASSERT(mScrollInfoItemsForHoisting);
  mScrollInfoItemsForHoisting->AppendToTop(aScrollInfoItem);
}

void nsDisplayListBuilder::BuildCompositorHitTestInfoIfNeeded(
    nsIFrame* aFrame, nsDisplayList* aList) {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aList);

  if (!BuildCompositorHitTestInfo()) {
    return;
  }

  const CompositorHitTestInfo info = aFrame->GetCompositorHitTestInfo(this);
  if (info != CompositorHitTestInvisibleToHit) {
    aList->AppendNewToTop<nsDisplayCompositorHitTestInfo>(this, aFrame);
  }
}

void nsDisplayListBuilder::AddReusableDisplayItem(nsDisplayItem* aItem) {
  mReuseableItems.Insert(aItem);
}

void nsDisplayListBuilder::RemoveReusedDisplayItem(nsDisplayItem* aItem) {
  MOZ_ASSERT(aItem->IsReusedItem());
  mReuseableItems.Remove(aItem);
}

void nsDisplayListBuilder::ClearReuseableDisplayItems() {
  const size_t total = mReuseableItems.Count();

  size_t reused = 0;
  for (auto* item : mReuseableItems) {
    if (item->IsReusedItem()) {
      reused++;
      item->SetReusable();
    } else {
      item->Destroy(this);
    }
  }

  DL_LOGI("RDL - Reused %zu of %zu SC display items", reused, total);
  mReuseableItems.Clear();
}

void nsDisplayListBuilder::ReuseDisplayItem(nsDisplayItem* aItem) {
  const auto* previous = mCurrentContainerASR;
  const auto* asr = aItem->GetActiveScrolledRoot();
  mCurrentContainerASR =
      ActiveScrolledRoot::PickAncestor(asr, mCurrentContainerASR);

  if (previous != mCurrentContainerASR) {
    DL_LOGV("RDL - Changed mCurrentContainerASR from %p to %p", previous,
            mCurrentContainerASR);
  }

  aItem->SetReusedItem();
}

void nsDisplayListSet::MoveTo(const nsDisplayListSet& aDestination) const {
  aDestination.BorderBackground()->AppendToTop(BorderBackground());
  aDestination.BlockBorderBackgrounds()->AppendToTop(BlockBorderBackgrounds());
  aDestination.Floats()->AppendToTop(Floats());
  aDestination.Content()->AppendToTop(Content());
  aDestination.PositionedDescendants()->AppendToTop(PositionedDescendants());
  aDestination.Outlines()->AppendToTop(Outlines());
}

nsRect nsDisplayList::GetClippedBounds(nsDisplayListBuilder* aBuilder) const {
  nsRect bounds;
  for (nsDisplayItem* i : *this) {
    bounds.UnionRect(bounds, i->GetClippedBounds(aBuilder));
  }
  return bounds;
}

nsRect nsDisplayList::GetClippedBoundsWithRespectToASR(
    nsDisplayListBuilder* aBuilder, const ActiveScrolledRoot* aASR,
    nsRect* aBuildingRect) const {
  nsRect bounds;
  for (nsDisplayItem* i : *this) {
    nsRect r = i->GetClippedBounds(aBuilder);
    if (aASR != i->GetActiveScrolledRoot() && !r.IsEmpty()) {
      if (Maybe<nsRect> clip = i->GetClipWithRespectToASR(aBuilder, aASR)) {
        r = clip.ref();
      }
    }
    if (aBuildingRect) {
      aBuildingRect->UnionRect(*aBuildingRect, i->GetBuildingRect());
    }
    bounds.UnionRect(bounds, r);
  }
  return bounds;
}

nsRect nsDisplayList::GetBuildingRect() const {
  nsRect result;
  for (nsDisplayItem* i : *this) {
    result.UnionRect(result, i->GetBuildingRect());
  }
  return result;
}

WindowRenderer* nsDisplayListBuilder::GetWidgetWindowRenderer() {
  if (RootReferenceFrame() !=
      nsLayoutUtils::GetDisplayRootFrame(RootReferenceFrame())) {
    return nullptr;
  }
  if (nsIWidget* window = RootReferenceFrame()->GetNearestWidget()) {
    return window->GetWindowRenderer();
  }
  return nullptr;
}

WebRenderLayerManager* nsDisplayListBuilder::GetWidgetLayerManager() {
  if (WindowRenderer* renderer = GetWidgetWindowRenderer()) {
    return renderer->AsWebRender();
  }
  return nullptr;
}

void nsDisplayList::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx,
                          int32_t aAppUnitsPerDevPixel) {
  FlattenedDisplayListIterator iter(aBuilder, this);
  while (iter.HasNext()) {
    nsPaintedDisplayItem* item = iter.GetNextItem()->AsPaintedDisplayItem();
    if (!item) {
      continue;
    }

    nsRect visible = item->GetClippedBounds(aBuilder);
    visible = visible.Intersect(item->GetPaintRect(aBuilder, aCtx));
    if (visible.IsEmpty()) {
      continue;
    }

    DisplayItemClip currentClip = item->GetClip();
    if (currentClip.HasClip()) {
      aCtx->Save();
      if (currentClip.IsRectClippedByRoundedCorner(visible)) {
        currentClip.ApplyTo(aCtx, aAppUnitsPerDevPixel);
      } else {
        currentClip.ApplyRectTo(aCtx, aAppUnitsPerDevPixel);
      }
    }
    aCtx->NewPath();

    item->Paint(aBuilder, aCtx);

    if (currentClip.HasClip()) {
      aCtx->Restore();
    }
  }
}

/**
 * We paint by executing a layer manager transaction, constructing a
 * single layer representing the display list, and then making it the
 * root of the layer manager, drawing into the PaintedLayers.
 */
void nsDisplayList::PaintRoot(nsDisplayListBuilder* aBuilder, gfxContext* aCtx,
                              uint32_t aFlags,
                              Maybe<double> aDisplayListBuildTime) {
  AUTO_PROFILER_LABEL("nsDisplayList::PaintRoot", GRAPHICS);

  RefPtr<WebRenderLayerManager> layerManager;
  WindowRenderer* renderer = nullptr;
  bool widgetTransaction = false;
  bool doBeginTransaction = true;
  if (aFlags & PAINT_USE_WIDGET_LAYERS) {
    renderer = aBuilder->GetWidgetWindowRenderer();
    if (renderer) {
      // The fallback renderer doesn't retain any content, so it's
      // not meaningful to use it when drawing to an external context.
      if (aCtx && renderer->AsFallback()) {
        MOZ_ASSERT(!(aFlags & PAINT_EXISTING_TRANSACTION));
        renderer = nullptr;
      } else {
        layerManager = renderer->AsWebRender();
        doBeginTransaction = !(aFlags & PAINT_EXISTING_TRANSACTION);
        widgetTransaction = true;
      }
    }
  }

  nsIFrame* frame = aBuilder->RootReferenceFrame();
  nsPresContext* presContext = frame->PresContext();
  PresShell* presShell = presContext->PresShell();
  Document* document = presShell->GetDocument();

  ScopeExit g([&]() {
#ifdef DEBUG
    MOZ_ASSERT(!layerManager || !layerManager->GetTarget());
#endif

    // For layers-free mode, we check the invalidation state bits in the
    // EndTransaction. So we clear the invalidation state bits after
    // EndTransaction.
    if (widgetTransaction ||
        // SVG-as-an-image docs don't paint as part of the retained layer tree,
        // but they still need the invalidation state bits cleared in order for
        // invalidation for CSS/SMIL animation to work properly.
        (document && document->IsBeingUsedAsImage())) {
      DL_LOGD("Clearing invalidation state bits");
      frame->ClearInvalidationStateBits();
    }
  });

  if (!renderer) {
    if (!aCtx) {
      NS_WARNING("Nowhere to paint into");
      return;
    }
    Paint(aBuilder, aCtx, presContext->AppUnitsPerDevPixel());

    return;
  }

  if (renderer->GetBackendType() == LayersBackend::LAYERS_WR) {
    MOZ_ASSERT(layerManager);
    if (doBeginTransaction) {
      if (aCtx) {
        if (!layerManager->BeginTransactionWithTarget(aCtx, nsCString())) {
          return;
        }
      } else {
        if (!layerManager->BeginTransaction(nsCString())) {
          return;
        }
      }
    }

    layerManager->SetTransactionIdAllocator(presContext->RefreshDriver());

    bool sent = false;
    if (aFlags & PAINT_IDENTICAL_DISPLAY_LIST) {
      MOZ_ASSERT(!aCtx);
      sent = layerManager->EndEmptyTransaction();
    }

    if (!sent) {
      auto* wrManager = static_cast<WebRenderLayerManager*>(layerManager.get());

      nsIDocShell* docShell = presContext->GetDocShell();
      WrFiltersHolder wrFilters;
      gfx::Matrix5x4* colorMatrix =
          nsDocShell::Cast(docShell)->GetColorMatrix();
      if (colorMatrix) {
        // Note: This color matrix was added here in for accessibility in
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1431466 , it could be
        // done with regular SVG in the document as long as it is accelerated,
        // and it's probably best to do this in linearRGB, now that it is
        // feasible to do so
        // TODO(ahale): Make sure to test this works correctly before enabling
        if (StaticPrefs::gfx_webrender_svg_filter_effects() &&
            StaticPrefs::
                gfx_webrender_svg_filter_effects_also_use_for_docshell_fecolormatrix()) {
          // WebRender SVGFE code needs a valid filter region, so use 1<<30 as
          // rendering will already be heavily degraded at that range.
          static constexpr float kExtent = 1024.0f * 1024.0f * 1024.0f;
          wr::LayoutRect subregion = {{-kExtent, -kExtent}, {kExtent, kExtent}};
          auto node = wr::FilterOpGraphNode{};
          node.input.buffer_id = wr::FilterOpGraphPictureBufferId::None();
          node.input2.buffer_id = wr::FilterOpGraphPictureBufferId::None();
          node.subregion = subregion;
          wrFilters.filters.AppendElement(
              wr::FilterOp::SVGFESourceGraphic(node));
          node.input.buffer_id = wr::FilterOpGraphPictureBufferId::BufferId(0);
          wrFilters.filters.AppendElement(
              wr::FilterOp::SVGFEColorMatrix(node, colorMatrix->components));
        } else {
          wrFilters.filters.AppendElement(
              wr::FilterOp::ColorMatrix(colorMatrix->components));
        }
      }

      wrManager->EndTransactionWithoutLayer(this, aBuilder,
                                            std::move(wrFilters), nullptr,
                                            aDisplayListBuildTime.valueOr(0.0),
                                            aFlags & PAINT_COMPOSITE_OFFSCREEN);
    }

    if (presContext->RefreshDriver()->IsInRefresh() ||
        presContext->RefreshDriver()->IsPaintPending()) {
      presContext->NotifyInvalidation(layerManager->GetLastTransactionId(),
                                      frame->GetRect());
    }
    return;
  }

  FallbackRenderer* fallback = renderer->AsFallback();
  MOZ_ASSERT(fallback);

  if (doBeginTransaction) {
    MOZ_ASSERT(!aCtx);
    if (!fallback->BeginTransaction()) {
      return;
    }
  }

  fallback->EndTransactionWithList(aBuilder, this,
                                   presContext->AppUnitsPerDevPixel(),
                                   WindowRenderer::END_DEFAULT);
}

void nsDisplayList::DeleteAll(nsDisplayListBuilder* aBuilder) {
  for (auto* item : TakeItems()) {
    item->Destroy(aBuilder);
  }
}

static bool IsFrameReceivingPointerEvents(nsIFrame* aFrame) {
  return aFrame->Style()->PointerEvents() != StylePointerEvents::None;
}

// A list of frames, and their z depth. Used for sorting
// the results of hit testing.
struct FramesWithDepth {
  explicit FramesWithDepth(float aDepth) : mDepth(aDepth) {}

  bool operator<(const FramesWithDepth& aOther) const {
    // We want to sort so that the shallowest item (highest depth value) is
    // first. Round to have some error tolerance (multiply with 8 translates
    // effectively to <<3).
    double lDepth = round(mDepth * 8.);
    double rDepth = round(aOther.mDepth * 8.);
    return lDepth > rDepth;
  }
  bool operator==(const FramesWithDepth& aOther) const {
    return this == &aOther;
  }

  float mDepth;
  nsTArray<nsIFrame*> mFrames;
};

// Sort the frames by depth and then moves all the contained frames to the
// destination
static void FlushFramesArray(nsTArray<FramesWithDepth>& aSource,
                             nsTArray<nsIFrame*>* aDest) {
  if (aSource.IsEmpty()) {
    return;
  }
  aSource.StableSort();
  uint32_t length = aSource.Length();
  for (uint32_t i = 0; i < length; i++) {
    aDest->AppendElements(std::move(aSource[i].mFrames));
  }
  aSource.Clear();
}

void nsDisplayList::HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                            nsDisplayItem::HitTestState* aState,
                            nsTArray<nsIFrame*>* aOutFrames) const {
  nsDisplayItem* item;

  if (aState->mGatheringPreserves3DLeaves) {
    // Collect leaves of the current 3D rendering context.
    for (nsDisplayItem* item : *this) {
      auto itemType = item->GetType();
      if (itemType != DisplayItemType::TYPE_TRANSFORM ||
          !static_cast<nsDisplayTransform*>(item)->IsLeafOf3DContext()) {
        item->HitTest(aBuilder, aRect, aState, aOutFrames);
      } else {
        // One of leaves in the current 3D rendering context.
        aState->mItemBuffer.AppendElement(item);
      }
    }
    return;
  }

  int32_t itemBufferStart = aState->mItemBuffer.Length();
  for (nsDisplayItem* item : *this) {
    aState->mItemBuffer.AppendElement(item);
  }

  AutoTArray<FramesWithDepth, 16> temp;
  for (int32_t i = aState->mItemBuffer.Length() - 1; i >= itemBufferStart;
       --i) {
    // Pop element off the end of the buffer. We want to shorten the buffer
    // so that recursive calls to HitTest have more buffer space.
    item = aState->mItemBuffer[i];
    aState->mItemBuffer.SetLength(i);

    bool snap;
    nsRect r = item->GetBounds(aBuilder, &snap).Intersect(aRect);
    auto itemType = item->GetType();
    bool same3DContext =
        (itemType == DisplayItemType::TYPE_TRANSFORM &&
         static_cast<nsDisplayTransform*>(item)->IsParticipating3DContext()) ||
        (itemType == DisplayItemType::TYPE_PERSPECTIVE &&
         item->Frame()->Extend3DContext());
    if (same3DContext &&
        (itemType != DisplayItemType::TYPE_TRANSFORM ||
         !static_cast<nsDisplayTransform*>(item)->IsLeafOf3DContext())) {
      if (!item->GetClip().MayIntersect(aRect)) {
        continue;
      }
      AutoTArray<nsIFrame*, 1> neverUsed;
      // Start gathering leaves of the 3D rendering context, and
      // append leaves at the end of mItemBuffer.  Leaves are
      // processed at following iterations.
      aState->mGatheringPreserves3DLeaves = true;
      item->HitTest(aBuilder, aRect, aState, &neverUsed);
      aState->mGatheringPreserves3DLeaves = false;
      i = aState->mItemBuffer.Length();
      continue;
    }

    if (!same3DContext && !item->GetClip().MayIntersect(r)) {
      continue;
    }

    const bool savedTransformHasBackfaceVisible =
        aState->mTransformHasBackfaceVisible;
    if (aState->mTransformHasBackfaceVisible &&
        !item->Combines3DTransformWithAncestors()) {
      // exiting a preserve 3d context, the transform is no longer applied to
      // this item, so reset the tracking var
      aState->mTransformHasBackfaceVisible = false;
    }
    AutoTArray<nsIFrame*, 16> outFrames;
    item->HitTest(aBuilder, aRect, aState, &outFrames);
    MOZ_ASSERT(!aState->mTransformHasBackfaceVisible ||
               !item->In3DContextAndBackfaceIsHidden() ||
               !outFrames.Contains(item->Frame()));
    aState->mTransformHasBackfaceVisible = savedTransformHasBackfaceVisible;

    // For 3d transforms with preserve-3d we add hit frames into the temp list
    // so we can sort them later, otherwise we add them directly to the output
    // list.
    nsTArray<nsIFrame*>* writeFrames = aOutFrames;
    if (item->GetType() == DisplayItemType::TYPE_TRANSFORM &&
        static_cast<nsDisplayTransform*>(item)->IsLeafOf3DContext()) {
      if (outFrames.Length()) {
        nsDisplayTransform* transform = static_cast<nsDisplayTransform*>(item);
        nsPoint point = aRect.TopLeft();
        // A 1x1 rect means a point, otherwise use the center of the rect
        if (aRect.width != 1 || aRect.height != 1) {
          point = aRect.Center();
        }
        temp.AppendElement(
            FramesWithDepth(transform->GetHitDepthAtPoint(aBuilder, point)));
        writeFrames = &temp[temp.Length() - 1].mFrames;
      }
    } else {
      // We may have just finished a run of consecutive preserve-3d
      // transforms, so flush these into the destination array before
      // processing our frame list.
      FlushFramesArray(temp, aOutFrames);
    }

    for (uint32_t j = 0; j < outFrames.Length(); j++) {
      nsIFrame* f = outFrames.ElementAt(j);
      // Filter out some frames depending on the type of hittest
      // we are doing. For visibility tests, pass through all frames.
      // For pointer tests, only pass through frames that are styled
      // to receive pointer events.
      if (aBuilder->HitTestIsForVisibility() ||
          IsFrameReceivingPointerEvents(f)) {
        writeFrames->AppendElement(f);
      }
    }

    if (aBuilder->HitTestIsForVisibility()) {
      aState->mHitOccludingItem = [&] {
        if (aState->mHitOccludingItem) {
          // We already hit something before.
          return true;
        }
        if (aState->mCurrentOpacity == 1.0f &&
            item->GetOpaqueRegion(aBuilder, &snap).Contains(aRect)) {
          // An opaque item always occludes everything. Note that we need to
          // check wrapping opacity and such as well.
          return true;
        }
        float threshold = aBuilder->VisibilityThreshold();
        if (threshold == 1.0f) {
          return false;
        }
        float itemOpacity = [&] {
          switch (item->GetType()) {
            case DisplayItemType::TYPE_OPACITY:
              return static_cast<nsDisplayOpacity*>(item)->GetOpacity();
            case DisplayItemType::TYPE_BACKGROUND_COLOR:
              return static_cast<nsDisplayBackgroundColor*>(item)->GetOpacity();
            default:
              // Be conservative and assume it won't occlude other items.
              return 0.0f;
          }
        }();
        return itemOpacity * aState->mCurrentOpacity >= threshold;
      }();

      if (aState->mHitOccludingItem) {
        // We're exiting early, so pop the remaining items off the buffer.
        aState->mItemBuffer.TruncateLength(itemBufferStart);
        break;
      }
    }
  }
  // Clear any remaining preserve-3d transforms.
  FlushFramesArray(temp, aOutFrames);
  NS_ASSERTION(aState->mItemBuffer.Length() == uint32_t(itemBufferStart),
               "How did we forget to pop some elements?");
}

static nsIContent* FindContentInDocument(nsDisplayItem* aItem, Document* aDoc) {
  nsIFrame* f = aItem->Frame();
  while (f) {
    nsPresContext* pc = f->PresContext();
    if (pc->Document() == aDoc) {
      return f->GetContent();
    }
    f = nsLayoutUtils::GetCrossDocParentFrameInProcess(
        pc->PresShell()->GetRootFrame());
  }
  return nullptr;
}

struct ZSortItem {
  nsDisplayItem* item;
  int32_t zIndex;

  explicit ZSortItem(nsDisplayItem* aItem)
      : item(aItem), zIndex(aItem->ZIndex()) {}

  operator nsDisplayItem*() { return item; }
};

struct ZOrderComparator {
  bool LessThan(const ZSortItem& aLeft, const ZSortItem& aRight) const {
    return aLeft.zIndex < aRight.zIndex;
  }
};

void nsDisplayList::SortByZOrder() { Sort<ZSortItem>(ZOrderComparator()); }

struct ContentComparator {
  nsIContent* mCommonAncestor;

  explicit ContentComparator(nsIContent* aCommonAncestor)
      : mCommonAncestor(aCommonAncestor) {}

  bool LessThan(nsDisplayItem* aLeft, nsDisplayItem* aRight) const {
    // It's possible that the nsIContent for aItem1 or aItem2 is in a
    // subdocument of commonAncestor, because display items for subdocuments
    // have been mixed into the same list. Ensure that we're looking at content
    // in commonAncestor's document.
    Document* commonAncestorDoc = mCommonAncestor->OwnerDoc();
    nsIContent* content1 = FindContentInDocument(aLeft, commonAncestorDoc);
    nsIContent* content2 = FindContentInDocument(aRight, commonAncestorDoc);
    if (!content1 || !content2) {
      NS_ERROR("Document trees are mixed up!");
      // Something weird going on
      return true;
    }
    return content1 != content2 &&
           nsContentUtils::CompareTreePosition<TreeKind::Flat>(
               content1, content2, mCommonAncestor) < 0;
  }
};

void nsDisplayList::SortByContentOrder(nsIContent* aCommonAncestor) {
  Sort<nsDisplayItem*>(ContentComparator(aCommonAncestor));
}

#if !defined(DEBUG) && !defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
static_assert(sizeof(nsDisplayItem) <= 176, "nsDisplayItem has grown");
#endif

nsDisplayItem::nsDisplayItem(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame)
    : nsDisplayItem(aBuilder, aFrame, aBuilder->CurrentActiveScrolledRoot()) {}

nsDisplayItem::nsDisplayItem(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                             const ActiveScrolledRoot* aActiveScrolledRoot)
    : mFrame(aFrame), mActiveScrolledRoot(aActiveScrolledRoot) {
  MOZ_COUNT_CTOR(nsDisplayItem);
  MOZ_ASSERT(mFrame);
  if (aBuilder->IsRetainingDisplayList()) {
    mFrame->AddDisplayItem(this);
  }

  aBuilder->FindReferenceFrameFor(aFrame, &mToReferenceFrame);
  NS_ASSERTION(
      aBuilder->GetVisibleRect().width >= 0 || !aBuilder->IsForPainting(),
      "visible rect not set");

  mClipChain = aBuilder->ClipState().GetCurrentCombinedClipChain(aBuilder);

  // The visible rect is for mCurrentFrame, so we have to use
  // mCurrentOffsetToReferenceFrame
  nsRect visible = aBuilder->GetVisibleRect() +
                   aBuilder->GetCurrentFrameOffsetToReferenceFrame();
  SetBuildingRect(visible);

  const nsStyleDisplay* disp = mFrame->StyleDisplay();
  if (mFrame->BackfaceIsHidden(disp)) {
    mItemFlags += ItemFlag::BackfaceHidden;
  }
  if (mFrame->Combines3DTransformWithAncestors()) {
    mItemFlags += ItemFlag::Combines3DTransformWithAncestors;
  }
}

void nsDisplayItem::SetDeletedFrame() { mItemFlags += ItemFlag::DeletedFrame; }

const ActiveScrolledRoot* nsDisplayItem::GetNearestScrollASR() const {
  const ActiveScrolledRoot* asr = GetActiveScrolledRoot();
  if (asr) {
    return asr->GetNearestScrollASR();
  }
  return nullptr;
}

bool nsDisplayItem::HasDeletedFrame() const {
  bool retval = mItemFlags.contains(ItemFlag::DeletedFrame) ||
                (GetType() == DisplayItemType::TYPE_REMOTE &&
                 !static_cast<const nsDisplayRemote*>(this)->GetFrameLoader());
  MOZ_ASSERT(retval || mFrame);
  return retval;
}

/* static */
bool nsDisplayItem::ForceActiveLayers() {
  return StaticPrefs::layers_force_active();
}

int32_t nsDisplayItem::ZIndex() const { return mFrame->ZIndex().valueOr(0); }

void nsDisplayItem::SetClipChain(const DisplayItemClipChain* aClipChain,
                                 bool aStore) {
  mClipChain = aClipChain;
}

Maybe<nsRect> nsDisplayItem::GetClipWithRespectToASR(
    nsDisplayListBuilder* aBuilder, const ActiveScrolledRoot* aASR) const {
  if (const DisplayItemClip* clip =
          DisplayItemClipChain::ClipForASR(GetClipChain(), aASR)) {
    return Some(clip->GetClipRect());
  }

  return Nothing();
}

const DisplayItemClip& nsDisplayItem::GetClip() const {
  const DisplayItemClip* clip =
      DisplayItemClipChain::ClipForASR(mClipChain, mActiveScrolledRoot);
  return clip ? *clip : DisplayItemClip::NoClip();
}

void nsDisplayItem::IntersectClip(nsDisplayListBuilder* aBuilder,
                                  const DisplayItemClipChain* aOther,
                                  bool aStore) {
  if (!aOther || mClipChain == aOther) {
    return;
  }

  // aOther might be a reference to a clip on the stack. We need to make sure
  // that CreateClipChainIntersection will allocate the actual intersected
  // clip in the builder's arena, so for the mClipChain == nullptr case,
  // we supply nullptr as the common ancestor so that
  // CreateClipChainIntersection clones the whole chain.
  const DisplayItemClipChain* ancestorClip =
      mClipChain ? FindCommonAncestorClipForIntersection(mClipChain, aOther)
                 : nullptr;

  SetClipChain(
      aBuilder->CreateClipChainIntersection(ancestorClip, mClipChain, aOther),
      aStore);
}

nsRect nsDisplayItem::GetClippedBounds(nsDisplayListBuilder* aBuilder) const {
  bool snap;
  nsRect r = GetBounds(aBuilder, &snap);
  return GetClip().ApplyNonRoundedIntersection(r);
}

nsDisplayContainer::nsDisplayContainer(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, nsDisplayList* aList)
    : nsDisplayItem(aBuilder, aFrame, aActiveScrolledRoot),
      mFrameASR(aContainerASRType == ContainerASRType::AncestorOfContained
                    ? aBuilder->CurrentActiveScrolledRoot()
                    : nullptr),
      mContainerASRType(aContainerASRType) {
  MOZ_COUNT_CTOR(nsDisplayContainer);
  mChildren.AppendToTop(aList);
  UpdateBounds(aBuilder);

  // Clear and store the clip chain set by nsDisplayItem constructor.
  nsDisplayItem::SetClipChain(nullptr, true);
}

nsRect nsDisplayItem::GetPaintRect(nsDisplayListBuilder* aBuilder,
                                   gfxContext* aCtx) {
  bool dummy;
  nsRect result = GetBounds(aBuilder, &dummy);
  if (aCtx) {
    result.IntersectRect(result,
                         nsLayoutUtils::RoundGfxRectToAppRect(
                             aCtx->GetClipExtents(),
                             mFrame->PresContext()->AppUnitsPerDevPixel()));
  }
  return result;
}

WebRenderCommandsResult nsDisplayContainer::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  aManager->CommandBuilder().CreateWebRenderCommandsFromDisplayList(
      GetChildren(), this, aDisplayListBuilder, aSc, aBuilder, aResources,
      false);
  return Ok();
}

nsRect nsDisplayContainer::GetBounds(nsDisplayListBuilder* aBuilder,
                                     bool* aSnap) const {
  *aSnap = false;
  return mBounds;
}

nsRect nsDisplayContainer::GetComponentAlphaBounds(
    nsDisplayListBuilder* aBuilder) const {
  return mChildren.GetComponentAlphaBounds(aBuilder);
}

static nsRegion GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                nsDisplayList* aList,
                                const nsRect& aListBounds) {
  return aList->GetOpaqueRegion(aBuilder);
}

nsRegion nsDisplayContainer::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                             bool* aSnap) const {
  return ::mozilla::GetOpaqueRegion(aBuilder, GetChildren(),
                                    GetBounds(aBuilder, aSnap));
}

Maybe<nsRect> nsDisplayContainer::GetClipWithRespectToASR(
    nsDisplayListBuilder* aBuilder, const ActiveScrolledRoot* aASR) const {
  // Our children should have finite bounds with respect to |aASR|.
  if (aASR == mActiveScrolledRoot) {
    return Some(mBounds);
  }

  return Some(mChildren.GetClippedBoundsWithRespectToASR(aBuilder, aASR));
}

void nsDisplayContainer::HitTest(nsDisplayListBuilder* aBuilder,
                                 const nsRect& aRect, HitTestState* aState,
                                 nsTArray<nsIFrame*>* aOutFrames) {
  mChildren.HitTest(aBuilder, aRect, aState, aOutFrames);
}

void nsDisplayContainer::UpdateBounds(nsDisplayListBuilder* aBuilder) {
  // Container item bounds are expected to be clipped.
  mBounds =
      mChildren.GetClippedBoundsWithRespectToASR(aBuilder, mActiveScrolledRoot);
}

nsRect nsDisplaySolidColor::GetBounds(nsDisplayListBuilder* aBuilder,
                                      bool* aSnap) const {
  *aSnap = true;
  return mBounds;
}

void nsDisplaySolidColor::Paint(nsDisplayListBuilder* aBuilder,
                                gfxContext* aCtx) {
  if (!NS_GET_A(mColor)) {
    return;
  }
  int32_t appUnitsPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  DrawTarget* drawTarget = aCtx->GetDrawTarget();
  Rect rect = NSRectToSnappedRect(GetPaintRect(aBuilder, aCtx),
                                  appUnitsPerDevPixel, *drawTarget);
  drawTarget->FillRect(rect, ColorPattern(ToDeviceColor(mColor)));
}

void nsDisplaySolidColor::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (rgba " << (int)NS_GET_R(mColor) << "," << (int)NS_GET_G(mColor)
          << "," << (int)NS_GET_B(mColor) << "," << (int)NS_GET_A(mColor)
          << ")";
}

WebRenderCommandsResult nsDisplaySolidColor::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!NS_GET_A(mColor)) {
    return Ok();
  }
  LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
      mBounds, mFrame->PresContext()->AppUnitsPerDevPixel());
  wr::LayoutRect r = wr::ToLayoutRect(bounds);
  aBuilder.PushRect(r, r, !BackfaceIsHidden(), false, mIsCheckerboardBackground,
                    wr::ToColorF(ToDeviceColor(mColor)));

  return Ok();
}

nsRect nsDisplaySolidColorRegion::GetBounds(nsDisplayListBuilder* aBuilder,
                                            bool* aSnap) const {
  *aSnap = true;
  return mRegion.GetBounds();
}

void nsDisplaySolidColorRegion::Paint(nsDisplayListBuilder* aBuilder,
                                      gfxContext* aCtx) {
  int32_t appUnitsPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  DrawTarget* drawTarget = aCtx->GetDrawTarget();
  ColorPattern color(ToDeviceColor(mColor));
  for (auto iter = mRegion.RectIter(); !iter.Done(); iter.Next()) {
    Rect rect =
        NSRectToSnappedRect(iter.Get(), appUnitsPerDevPixel, *drawTarget);
    drawTarget->FillRect(rect, color);
  }
}

void nsDisplaySolidColorRegion::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (rgba " << int(mColor.r * 255) << "," << int(mColor.g * 255)
          << "," << int(mColor.b * 255) << "," << mColor.a << ")";
}

WebRenderCommandsResult nsDisplaySolidColorRegion::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  for (auto iter = mRegion.RectIter(); !iter.Done(); iter.Next()) {
    nsRect rect = iter.Get();
    LayoutDeviceRect layerRects = LayoutDeviceRect::FromAppUnits(
        rect, mFrame->PresContext()->AppUnitsPerDevPixel());
    wr::LayoutRect r = wr::ToLayoutRect(layerRects);
    aBuilder.PushRect(r, r, !BackfaceIsHidden(), false, false,
                      wr::ToColorF(ToDeviceColor(mColor)));
  }

  return Ok();
}

static void RegisterThemeGeometry(nsDisplayListBuilder* aBuilder,
                                  nsDisplayItem* aItem, nsIFrame* aFrame,
                                  nsITheme::ThemeGeometryType aType) {
  if (aBuilder->IsInChromeDocumentOrPopup()) {
    nsIFrame* displayRoot = nsLayoutUtils::GetDisplayRootFrame(aFrame);
    bool preservesAxisAlignedRectangles = false;
    nsRect borderBox = nsLayoutUtils::TransformFrameRectToAncestor(
        aFrame, aFrame->GetRectRelativeToSelf(), displayRoot,
        &preservesAxisAlignedRectangles);
    if (preservesAxisAlignedRectangles) {
      aBuilder->RegisterThemeGeometry(
          aType, aItem,
          LayoutDeviceIntRect::FromUnknownRect(borderBox.ToNearestPixels(
              aFrame->PresContext()->AppUnitsPerDevPixel())));
    }
  }
}

// Return the bounds of the viewport relative to |aFrame|'s reference frame.
// Returns Nothing() if transforming into |aFrame|'s coordinate space fails.
static Maybe<nsRect> GetViewportRectRelativeToReferenceFrame(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame) {
  nsIFrame* rootFrame = aFrame->PresShell()->GetRootFrame();
  nsRect rootRect = rootFrame->GetRectRelativeToSelf();
  if (nsLayoutUtils::TransformRect(rootFrame, aFrame, rootRect) ==
      nsLayoutUtils::TRANSFORM_SUCCEEDED) {
    return Some(rootRect + aBuilder->ToReferenceFrame(aFrame));
  }
  return Nothing();
}

/* static */ nsDisplayBackgroundImage::InitData
nsDisplayBackgroundImage::GetInitData(nsDisplayListBuilder* aBuilder,
                                      nsIFrame* aFrame, uint16_t aLayer,
                                      const nsRect& aBackgroundRect,
                                      const ComputedStyle* aBackgroundStyle) {
  nsPresContext* presContext = aFrame->PresContext();
  uint32_t flags = aBuilder->GetBackgroundPaintFlags();
  const nsStyleImageLayers::Layer& layer =
      aBackgroundStyle->StyleBackground()->mImage.mLayers[aLayer];

  bool isTransformedFixed;
  nsBackgroundLayerState state = nsCSSRendering::PrepareImageLayer(
      presContext, aFrame, flags, aBackgroundRect, aBackgroundRect, layer,
      &isTransformedFixed);

  // background-attachment:fixed is treated as background-attachment:scroll
  // if it's affected by a transform.
  // See https://www.w3.org/Bugs/Public/show_bug.cgi?id=17521.
  bool shouldTreatAsFixed =
      layer.mAttachment == StyleImageLayerAttachment::Fixed &&
      !isTransformedFixed;

  bool shouldFixToViewport = shouldTreatAsFixed && !layer.mImage.IsNone();
  bool isRasterImage = state.mImageRenderer.IsRasterImage();
  nsCOMPtr<imgIContainer> image;
  if (isRasterImage) {
    image = state.mImageRenderer.GetImage();
  }
  return InitData{aBuilder,        aBackgroundStyle, image,
                  aBackgroundRect, state.mFillArea,  state.mDestArea,
                  aLayer,          isRasterImage,    shouldFixToViewport};
}

nsDisplayBackgroundImage::nsDisplayBackgroundImage(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, const InitData& aInitData,
    nsIFrame* aFrameForBounds)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mBackgroundStyle(aInitData.backgroundStyle),
      mImage(aInitData.image),
      mDependentFrame(nullptr),
      mBackgroundRect(aInitData.backgroundRect),
      mFillRect(aInitData.fillArea),
      mDestRect(aInitData.destArea),
      mLayer(aInitData.layer),
      mIsRasterImage(aInitData.isRasterImage) {
  MOZ_COUNT_CTOR(nsDisplayBackgroundImage);
#ifdef DEBUG
  if (mBackgroundStyle && mBackgroundStyle != mFrame->Style()) {
    // If this changes, then you also need to adjust css::ImageLoader to
    // invalidate mFrame as needed.
    MOZ_ASSERT(mFrame->IsCanvasFrame() || mFrame->IsTablePart());
  }
#endif

  mBounds = GetBoundsInternal(aInitData.builder, aFrameForBounds);
  if (aInitData.shouldFixToViewport) {
    // Expand the item's visible rect to cover the entire bounds, limited to the
    // viewport rect. This is necessary because the background's clip can move
    // asynchronously.
    if (Maybe<nsRect> viewportRect = GetViewportRectRelativeToReferenceFrame(
            aInitData.builder, mFrame)) {
      SetBuildingRect(mBounds.Intersect(*viewportRect));
    }
  }
}

void nsDisplayBackgroundImage::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mDependentFrame);
  nsPaintedDisplayItem::Destroy(aBuilder);
}

static bool NeedsBorderAreaMask(nsIFrame* aFrame, StyleBackgroundClip aClip) {
  if (aClip != StyleBackgroundClip::BorderArea) {
    return false;
  }
  for (auto side : AllPhysicalSides()) {
    if (!aFrame->StyleBorder()->GetComputedBorderWidth(side)) {
      continue;
    }
    switch (aFrame->StyleBorder()->GetBorderStyle(side)) {
      case StyleBorderStyle::Dashed:
      case StyleBorderStyle::Dotted:
      case StyleBorderStyle::Double:
        return true;
      default:
        break;
    }
  }
  return false;
}

static void SetBackgroundClipRegion(
    DisplayListClipState::AutoSaveRestore& aClipState, nsIFrame* aFrame,
    const nsStyleImageLayers::Layer& aLayer, const nsRect& aBackgroundRect,
    bool aWillPaintBorder) {
  nsCSSRendering::ImageLayerClipState clip;
  nsCSSRendering::GetImageLayerClip(
      aLayer, aFrame, *aFrame->StyleBorder(), aBackgroundRect, aBackgroundRect,
      aWillPaintBorder, aFrame->PresContext()->AppUnitsPerDevPixel(), &clip);

  // Prevent applying the border-radius clip if we have a border-area mask.
  // It would be redundant and just cause extra antialiasing seams.
  const auto* radii =
      clip.mHasRoundedCorners && !NeedsBorderAreaMask(aFrame, aLayer.mClip)
          ? &clip.mRadii
          : nullptr;
  if (clip.mHasAdditionalBGClipArea) {
    aClipState.ClipContentDescendants(clip.mAdditionalBGClipArea,
                                      clip.mBGClipArea, radii);
  } else {
    aClipState.ClipContentDescendants(clip.mBGClipArea, radii);
  }
}

/**
 * This is used for the find bar highlighter overlay. It's only accessible
 * through the AnonymousContent API, so it's not exposed to general web pages.
 */
static bool SpecialCutoutRegionCase(nsDisplayListBuilder* aBuilder,
                                    nsIFrame* aFrame,
                                    const nsRect& aBackgroundRect,
                                    nsDisplayList* aList, nscolor aColor) {
  nsIContent* content = aFrame->GetContent();
  if (!content) {
    return false;
  }

  void* cutoutRegion = content->GetProperty(nsGkAtoms::cutoutregion);
  if (!cutoutRegion) {
    return false;
  }

  if (NS_GET_A(aColor) == 0) {
    return true;
  }

  nsRegion region;
  region.Sub(aBackgroundRect, *static_cast<nsRegion*>(cutoutRegion));
  region.MoveBy(aBuilder->ToReferenceFrame(aFrame));
  aList->AppendNewToTop<nsDisplaySolidColorRegion>(aBuilder, aFrame, region,
                                                   aColor);

  return true;
}

enum class TableType : uint8_t {
  Table,
  TableCol,
  TableColGroup,
  TableRow,
  TableRowGroup,
  TableCell,

  MAX,
};

enum class TableTypeBits : uint8_t { Count = 3 };

static_assert(static_cast<uint8_t>(TableType::MAX) <
                  (1 << (static_cast<uint8_t>(TableTypeBits::Count) + 1)),
              "TableType cannot fit with TableTypeBits::Count");
TableType GetTableTypeFromFrame(nsIFrame* aFrame);

static uint16_t CalculateTablePerFrameKey(const uint16_t aIndex,
                                          const TableType aType) {
  const uint32_t key = (aIndex << static_cast<uint8_t>(TableTypeBits::Count)) |
                       static_cast<uint8_t>(aType);

  return static_cast<uint16_t>(key);
}

static nsDisplayBackgroundImage* CreateBackgroundImage(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsIFrame* aSecondaryFrame,
    const nsDisplayBackgroundImage::InitData& aBgData) {
  const auto index = aBgData.layer;

  if (aSecondaryFrame) {
    const auto tableType = GetTableTypeFromFrame(aFrame);
    const uint16_t tableItemIndex = CalculateTablePerFrameKey(index, tableType);

    return MakeDisplayItemWithIndex<nsDisplayTableBackgroundImage>(
        aBuilder, aSecondaryFrame, tableItemIndex, aBgData, aFrame);
  }

  return MakeDisplayItemWithIndex<nsDisplayBackgroundImage>(aBuilder, aFrame,
                                                            index, aBgData);
}

static nsDisplayThemedBackground* CreateThemedBackground(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsIFrame* aSecondaryFrame,
    const nsRect& aBgRect) {
  if (aSecondaryFrame) {
    const uint16_t index = static_cast<uint16_t>(GetTableTypeFromFrame(aFrame));
    return MakeDisplayItemWithIndex<nsDisplayTableThemedBackground>(
        aBuilder, aSecondaryFrame, index, aBgRect, aFrame);
  }

  return MakeDisplayItem<nsDisplayThemedBackground>(aBuilder, aFrame, aBgRect);
}

static nsDisplayBackgroundColor* CreateBackgroundColor(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsIFrame* aSecondaryFrame,
    nsRect& aBgRect, const ComputedStyle* aBgSC, nscolor aColor) {
  if (aSecondaryFrame) {
    const uint16_t index = static_cast<uint16_t>(GetTableTypeFromFrame(aFrame));
    return MakeDisplayItemWithIndex<nsDisplayTableBackgroundColor>(
        aBuilder, aSecondaryFrame, index, aBgRect, aBgSC, aColor, aFrame);
  }

  return MakeDisplayItem<nsDisplayBackgroundColor>(aBuilder, aFrame, aBgRect,
                                                   aBgSC, aColor);
}

static void DealWithWindowsAppearanceHacks(nsIFrame* aFrame,
                                           nsDisplayListBuilder* aBuilder) {
  if (!XRE_IsParentProcess()) {
    return;
  }

  const auto& disp = *aFrame->StyleDisplay();

  // We use default appearance rather than effective appearance because we want
  // to handle when titlebar buttons that have appearance: none.
  const auto defaultAppearance = disp.mDefaultAppearance;
  if (MOZ_LIKELY(defaultAppearance == StyleAppearance::None)) {
    return;
  }

  if (auto type = disp.GetWindowButtonType()) {
    if (auto* widget = aFrame->GetNearestWidget()) {
      auto rect = LayoutDevicePixel::FromAppUnitsToNearest(
          nsRect(aBuilder->ToReferenceFrame(aFrame), aFrame->GetSize()),
          aFrame->PresContext()->AppUnitsPerDevPixel());
      widget->SetWindowButtonRect(*type, rect);
    }
  }
}

/*static*/
AppendedBackgroundType nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
    const nsRect& aBackgroundRect, nsDisplayList* aList,
    bool aAllowWillPaintBorderOptimization, const nsRect& aBackgroundOriginRect,
    nsIFrame* aSecondaryReferenceFrame,
    Maybe<nsDisplayListBuilder::AutoBuildingDisplayList>*
        aAutoBuildingDisplayList) {
  MOZ_ASSERT(!aFrame->IsCanvasFrame(),
             "We don't expect propagated canvas backgrounds here");
#ifdef DEBUG
  {
    nsIFrame* bgFrame = nsCSSRendering::FindBackgroundFrame(aFrame);
    MOZ_ASSERT(
        !bgFrame || bgFrame == aFrame,
        "Should only suppress backgrounds, never propagate to another frame");
  }
#endif

  DealWithWindowsAppearanceHacks(aFrame, aBuilder);

  const bool isThemed = aFrame->IsThemed();

  const ComputedStyle* bgSC = aFrame->Style();
  const nsStyleBackground* bg = bgSC->StyleBackground();
  const bool needsBackgroundColor =
      aBuilder->IsForEventDelivery() ||
      (EffectCompositor::HasAnimationsForCompositor(
           aFrame, DisplayItemType::TYPE_BACKGROUND_COLOR) &&
       !isThemed);
  if (!needsBackgroundColor && !isThemed && bg->IsTransparent(bgSC)) {
    return AppendedBackgroundType::None;
  }

  bool drawBackgroundColor = false;
  bool drawBackgroundImage = false;
  nscolor color = NS_RGBA(0, 0, 0, 0);
  // Don't get background color / images if we propagated our background to the
  // canvas (that is, if FindBackgroundFrame is null). But don't early return
  // yet, since we might still need a background-color item for hit-testing.
  if (!isThemed && nsCSSRendering::FindBackgroundFrame(aFrame)) {
    color = nsCSSRendering::DetermineBackgroundColor(
        aFrame->PresContext(), bgSC, aFrame, drawBackgroundImage,
        drawBackgroundColor);
  }

  if (SpecialCutoutRegionCase(aBuilder, aFrame, aBackgroundRect, aList,
                              color)) {
    return AppendedBackgroundType::None;
  }

  const nsStyleBorder& border = *aFrame->StyleBorder();
  const bool willPaintBorder =
      aAllowWillPaintBorderOptimization && !isThemed &&
      !aFrame->StyleEffects()->HasBoxShadowWithInset(true) &&
      border.HasBorder();

  auto EnsureBuildingDisplayList = [&] {
    if (!aAutoBuildingDisplayList || *aAutoBuildingDisplayList) {
      return;
    }
    nsPoint offset = aBuilder->GetCurrentFrame()->GetOffsetTo(aFrame);
    aAutoBuildingDisplayList->emplace(aBuilder, aFrame,
                                      aBuilder->GetVisibleRect() + offset,
                                      aBuilder->GetDirtyRect() + offset);
  };

  // An auxiliary list is necessary in case we have background blending; if that
  // is the case, background items need to be wrapped by a blend container to
  // isolate blending to the background
  nsDisplayList bgItemList;
  // Even if we don't actually have a background color to paint, we may still
  // need to create an item for hit testing and we still need to create an item
  // for background-color animations.
  if ((drawBackgroundColor && color != NS_RGBA(0, 0, 0, 0)) ||
      needsBackgroundColor) {
    EnsureBuildingDisplayList();
    Maybe<DisplayListClipState::AutoSaveRestore> clipState;
    nsRect bgColorRect = aBackgroundRect;
    if (!isThemed && !aBuilder->IsForEventDelivery()) {
      // Disable the will-paint-border optimization for background
      // colors with no border-radius. Enabling it for background colors
      // doesn't help much (there are no tiling issues) and clipping the
      // background breaks detection of the element's border-box being
      // opaque. For nonzero border-radius we still need it because we
      // want to inset the background if possible to avoid antialiasing
      // artifacts along the rounded corners.
      const bool useWillPaintBorderOptimization =
          willPaintBorder &&
          nsLayoutUtils::HasNonZeroCorner(border.mBorderRadius);

      nsCSSRendering::ImageLayerClipState clip;
      nsCSSRendering::GetImageLayerClip(
          bg->BottomLayer(), aFrame, border, aBackgroundRect, aBackgroundRect,
          useWillPaintBorderOptimization,
          aFrame->PresContext()->AppUnitsPerDevPixel(), &clip);

      bgColorRect = bgColorRect.Intersect(clip.mBGClipArea);
      if (clip.mHasAdditionalBGClipArea) {
        bgColorRect = bgColorRect.Intersect(clip.mAdditionalBGClipArea);
      }
      if (clip.mHasRoundedCorners &&
          !NeedsBorderAreaMask(aFrame, bg->BottomLayer().mClip)) {
        clipState.emplace(aBuilder);
        clipState->ClipContentDescendants(clip.mBGClipArea, &clip.mRadii);
      }
    }

    nsDisplayBackgroundColor* bgItem = CreateBackgroundColor(
        aBuilder, aFrame, aSecondaryReferenceFrame, bgColorRect, bgSC,
        drawBackgroundColor ? color : NS_RGBA(0, 0, 0, 0));

    if (bgItem) {
      bgItemList.AppendToTop(bgItem);
    }
  }

  if (isThemed) {
    nsDisplayThemedBackground* bgItem = CreateThemedBackground(
        aBuilder, aFrame, aSecondaryReferenceFrame, aBackgroundRect);

    if (bgItem) {
      bgItem->Init(aBuilder);
      bgItemList.AppendToTop(bgItem);
    }

    if (!bgItemList.IsEmpty()) {
      aList->AppendToTop(&bgItemList);
      return AppendedBackgroundType::ThemedBackground;
    }

    return AppendedBackgroundType::None;
  }

  if (!drawBackgroundImage) {
    if (!bgItemList.IsEmpty()) {
      aList->AppendToTop(&bgItemList);
      return AppendedBackgroundType::Background;
    }

    return AppendedBackgroundType::None;
  }

  const ActiveScrolledRoot* asr = aBuilder->CurrentActiveScrolledRoot();

  bool needBlendContainer = false;
  const nsRect& bgOriginRect =
      aBackgroundOriginRect.IsEmpty() ? aBackgroundRect : aBackgroundOriginRect;

  // Passing bg == nullptr in this macro will result in one iteration with
  // i = 0.
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, bg->mImage) {
    if (bg->mImage.mLayers[i].mImage.IsNone()) {
      continue;
    }

    EnsureBuildingDisplayList();

    if (bg->mImage.mLayers[i].mBlendMode != StyleBlend::Normal) {
      needBlendContainer = true;
    }

    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    if (!aBuilder->IsForEventDelivery()) {
      const nsStyleImageLayers::Layer& layer = bg->mImage.mLayers[i];
      SetBackgroundClipRegion(clipState, aFrame, layer, aBackgroundRect,
                              willPaintBorder);
    }

    nsDisplayList thisItemList;
    nsDisplayBackgroundImage::InitData bgData =
        nsDisplayBackgroundImage::GetInitData(aBuilder, aFrame, i, bgOriginRect,
                                              bgSC);

    if (bgData.shouldFixToViewport) {
      auto* displayData = aBuilder->GetCurrentFixedBackgroundDisplayData();
      nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
          aBuilder, aFrame, aBuilder->GetVisibleRect(),
          aBuilder->GetDirtyRect());

      nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
          aBuilder);
      if (displayData) {
        asrSetter.SetCurrentActiveScrolledRoot(
            displayData->mContainingBlockActiveScrolledRoot);
        asrSetter.SetCurrentScrollParentId(displayData->mScrollParentId);
        if (nsLayoutUtils::UsesAsyncScrolling(aFrame)) {
          // Override the dirty rect on the builder to be the dirty rect of
          // the viewport.
          // displayData->mDirtyRect is relative to the presshell's viewport
          // frame (the root frame), and we need it to be relative to aFrame.
          nsIFrame* rootFrame =
              aBuilder->CurrentPresShellState()->mPresShell->GetRootFrame();
          // There cannot be any transforms between aFrame and rootFrame
          // because then bgData.shouldFixToViewport would have been false.
          nsRect visibleRect =
              displayData->mVisibleRect + aFrame->GetOffsetTo(rootFrame);
          aBuilder->SetVisibleRect(visibleRect);
          nsRect dirtyRect =
              displayData->mDirtyRect + aFrame->GetOffsetTo(rootFrame);
          aBuilder->SetDirtyRect(dirtyRect);
        }
      }

      nsDisplayBackgroundImage* bgItem = nullptr;
      {
        // The clip is captured by the nsDisplayFixedPosition, so clear the
        // clip for the nsDisplayBackgroundImage inside.
        DisplayListClipState::AutoSaveRestore bgImageClip(aBuilder);
        bgImageClip.Clear();
        bgItem = CreateBackgroundImage(aBuilder, aFrame,
                                       aSecondaryReferenceFrame, bgData);
      }
      if (bgItem) {
        const ActiveScrolledRoot* scrollTargetASR =
            asr ? asr->GetNearestScrollASR() : nullptr;
        thisItemList.AppendToTop(
            nsDisplayFixedPosition::CreateForFixedBackground(
                aBuilder, aFrame, aSecondaryReferenceFrame, bgItem, i,
                scrollTargetASR));
      }
    } else {  // bgData.shouldFixToViewport == false
      nsDisplayBackgroundImage* bgItem = CreateBackgroundImage(
          aBuilder, aFrame, aSecondaryReferenceFrame, bgData);
      if (bgItem) {
        thisItemList.AppendToTop(bgItem);
      }
    }

    if (bg->mImage.mLayers[i].mBlendMode != StyleBlend::Normal) {
      // asr is scrolled. Even if we wrap a fixed background layer, that's
      // fine, because the item will have a scrolled clip that limits the
      // item with respect to asr.
      if (aSecondaryReferenceFrame) {
        const auto tableType = GetTableTypeFromFrame(aFrame);
        const uint16_t index = CalculateTablePerFrameKey(i + 1, tableType);

        thisItemList.AppendNewToTopWithIndex<nsDisplayTableBlendMode>(
            aBuilder, aSecondaryReferenceFrame, index, &thisItemList,
            bg->mImage.mLayers[i].mBlendMode, asr, ContainerASRType::Constant,
            aFrame, true);
      } else {
        thisItemList.AppendNewToTopWithIndex<nsDisplayBlendMode>(
            aBuilder, aFrame, i + 1, &thisItemList,
            bg->mImage.mLayers[i].mBlendMode, asr, ContainerASRType::Constant,
            true);
      }
    }
    bgItemList.AppendToTop(&thisItemList);
  }

  if (needBlendContainer) {
    bgItemList.AppendToTop(
        nsDisplayBlendContainer::CreateForBackgroundBlendMode(
            aBuilder, aFrame, aSecondaryReferenceFrame, &bgItemList, asr,
            nsDisplayItem::ContainerASRType::Constant));
  }

  if (!bgItemList.IsEmpty()) {
    aList->AppendToTop(&bgItemList);
    return AppendedBackgroundType::Background;
  }

  return AppendedBackgroundType::None;
}

// Check that the rounded border of aFrame, added to aToReferenceFrame,
// intersects aRect.  Assumes that the unrounded border has already
// been checked for intersection.
static bool RoundedBorderIntersectsRect(nsIFrame* aFrame,
                                        const nsPoint& aFrameToReferenceFrame,
                                        const nsRect& aTestRect) {
  if (!nsRect(aFrameToReferenceFrame, aFrame->GetSize())
           .Intersects(aTestRect)) {
    return false;
  }

  nsRectCornerRadii radii;
  return !aFrame->GetBorderRadii(radii) ||
         nsLayoutUtils::RoundedRectIntersectsRect(
             nsRect(aFrameToReferenceFrame, aFrame->GetSize()), radii,
             aTestRect);
}

// Returns TRUE if aContainedRect is guaranteed to be contained in
// the rounded rect defined by aRoundedRect and aRadii. Complex cases are
// handled conservatively by returning FALSE in some situations where
// a more thorough analysis could return TRUE.
//
// See also RoundedRectIntersectsRect.
static bool RoundedRectContainsRect(const nsRect& aRoundedRect,
                                    const nsRectCornerRadii& aRadii,
                                    const nsRect& aContainedRect) {
  nsRegion rgn = nsLayoutUtils::RoundedRectIntersectRect(aRoundedRect, aRadii,
                                                         aContainedRect);
  return rgn.Contains(aContainedRect);
}

bool nsDisplayBackgroundImage::CanApplyOpacity(
    WebRenderLayerManager* aManager, nsDisplayListBuilder* aBuilder) const {
  return CanBuildWebRenderDisplayItems(aManager, aBuilder);
}

bool nsDisplayBackgroundImage::CanBuildWebRenderDisplayItems(
    WebRenderLayerManager* aManager, nsDisplayListBuilder* aBuilder) const {
  return mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer].mClip !=
             StyleBackgroundClip::Text &&
         nsCSSRendering::CanBuildWebRenderDisplayItemsForStyleImageLayer(
             aManager, *StyleFrame()->PresContext(), StyleFrame(),
             mBackgroundStyle->StyleBackground(), mLayer,
             aBuilder->GetBackgroundPaintFlags());
}

static void GetInnerBorderAreaClip(
    nsIFrame* aFrame, const nsCSSRendering::ImageLayerClipState& aClip,
    const nsRect& aBackgroundRect, nsRect& aRect, nsRectCornerRadii& aRadii) {
  nsMargin border = aFrame->GetUsedBorder();
  border.ApplySkipSides(aFrame->GetSkipSides());
  aRect = aClip.mBGClipArea;
  aRect.Deflate(border);
  if (aClip.mHasRoundedCorners) {
    aRadii = aClip.mRadii;
    aRadii.AdjustInwards(border);
  }
}

static bool GetBorderAreaExclusion(nsIFrame* aFrame,
                                   const nsStyleImageLayers::Layer& aLayer,
                                   const nsRect& aBackgroundRect, nsRect& aRect,
                                   nsRectCornerRadii& aRadii) {
  if (aLayer.mClip != StyleBackgroundClip::BorderArea) {
    return false;
  }
  nsCSSRendering::ImageLayerClipState clip;
  nsCSSRendering::GetImageLayerClip(
      aLayer, aFrame, *aFrame->StyleBorder(), aBackgroundRect, aBackgroundRect,
      /* aWillPaintBorder = */ false,
      aFrame->PresContext()->AppUnitsPerDevPixel(), &clip);
  GetInnerBorderAreaClip(aFrame, clip, aBackgroundRect, aRect, aRadii);
  return true;
}

static void PushBorderAreaClipOut(
    wr::DisplayListBuilder& aBuilder, nsIFrame* aFrame,
    const nsStyleImageLayers::Layer& aLayer, const nsRect& aBackgroundRect,
    Maybe<wr::SpaceAndClipChainHelper>& aClipHelper) {
  nsRect rect;
  nsRectCornerRadii radii;
  if (!GetBorderAreaExclusion(aFrame, aLayer, aBackgroundRect, rect, radii)) {
    return;
  }

  if (NeedsBorderAreaMask(aFrame, aLayer.mClip)) {
    auto renderer = nsCSSRendering::CreateNullBorderRendererWithStyleBorder(
        aFrame->PresContext(), nullptr, aFrame, aBackgroundRect,
        aBackgroundRect, *aFrame->StyleBorder(), aFrame->Style(), nullptr,
        aFrame->GetSkipSides());
    if (renderer) {
      wr::WrClipId clipId = renderer->CreateWebRenderClip(aBuilder);
      wr::WrClipChainId chain = aBuilder.DefineClipChain(
          {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());
      aClipHelper.emplace(aBuilder, chain);
      return;
    }
  }

  wr::ComplexClipRegion region = wr::ToComplexClipRegion(
      rect, radii, aFrame->PresContext()->AppUnitsPerDevPixel());
  region.mode = wr::ClipMode::ClipOut;
  wr::WrClipId clipId = aBuilder.DefineRoundedRectClip(Nothing(), region);
  wr::WrClipChainId chain = aBuilder.DefineClipChain(
      {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());
  aClipHelper.emplace(aBuilder, chain);
}

// Software-rendering counterpart of PushBorderAreaClipOut: clips the context to
// the area painted by the border (the border box with the padding box removed).
static void ClipBackgroundToBorderArea(gfxContext* aCtx, nsIFrame* aFrame,
                                       const nsStyleImageLayers::Layer& aLayer,
                                       const nsRect& aBackgroundRect) {
  MOZ_ASSERT(aLayer.mClip == StyleBackgroundClip::BorderArea);
  const int32_t auPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();

  nsCSSRendering::ImageLayerClipState clip;
  nsCSSRendering::GetImageLayerClip(
      aLayer, aFrame, *aFrame->StyleBorder(), aBackgroundRect, aBackgroundRect,
      /* aWillPaintBorder = */ false, auPerDevPixel, &clip);

  nsRect innerNsRect;
  nsRectCornerRadii innerNsRadii;
  GetInnerBorderAreaClip(aFrame, clip, aBackgroundRect, innerNsRect,
                         innerNsRadii);

  DrawTarget* dt = aCtx->GetDrawTarget();
  RefPtr<PathBuilder> builder = dt->CreatePathBuilder();

  Rect outerRect = NSRectToRect(clip.mBGClipArea, auPerDevPixel);
  outerRect.Round();
  Rect innerRect = NSRectToRect(innerNsRect, auPerDevPixel);
  innerRect.Round();
  // The ring is the outer rounded rect minus the inner rounded rect: append
  // them with opposite winding so the nonzero fill rule leaves the ring.
  if (clip.mHasRoundedCorners) {
    RectCornerRadii outerRadii, innerRadii;
    nsCSSRendering::ComputePixelRadii(clip.mRadii, auPerDevPixel, &outerRadii);
    nsCSSRendering::ComputePixelRadii(innerNsRadii, auPerDevPixel, &innerRadii);

    AppendRoundedRectToPath(builder, outerRect, outerRadii, true);
    AppendRoundedRectToPath(builder, innerRect, innerRadii, false);
  } else {
    AppendRectToPath(builder, outerRect, true);
    AppendRectToPath(builder, innerRect, false);
  }
  RefPtr<Path> ring = builder->Finish();
  aCtx->Clip(ring);
}

WebRenderCommandsResult nsDisplayBackgroundImage::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!CanBuildWebRenderDisplayItems(aManager->LayerManager(),
                                     aDisplayListBuilder)) {
    return Err("background image is not WebRender-representable");
  }

  uint32_t paintFlags = aDisplayListBuilder->GetBackgroundPaintFlags();
  bool dummy;
  nsCSSRendering::PaintBGParams params =
      nsCSSRendering::PaintBGParams::ForSingleLayer(
          *StyleFrame()->PresContext(), GetBounds(aDisplayListBuilder, &dummy),
          mBackgroundRect, StyleFrame(), paintFlags, mLayer,
          CompositionOp::OP_OVER, aBuilder.GetInheritedOpacity());
  params.bgClipRect = &mBounds;

  Maybe<wr::SpaceAndClipChainHelper> borderAreaClip;
  PushBorderAreaClipOut(
      aBuilder, StyleFrame(),
      mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer],
      mBackgroundRect, borderAreaClip);

  ImgDrawResult result =
      nsCSSRendering::BuildWebRenderDisplayItemsForStyleImageLayer(
          params, aBuilder, aResources, aSc, aManager, this);
  if (result == ImgDrawResult::NOT_SUPPORTED) {
    return Err("background image layer is not supported");
  }

  if (nsIContent* content = StyleFrame()->GetContent()) {
    if (imgRequestProxy* requestProxy = mBackgroundStyle->StyleBackground()
                                            ->mImage.mLayers[mLayer]
                                            .mImage.GetImageRequest()) {
      nsRect rectRelativeToSelf = mBounds - ToReferenceFrame();
      Element* element = content->AsElement();

      ContainerTimingHelpers::MaybeProcessPaintForContainer(
          element, StyleFrame(), rectRelativeToSelf);

      // LCP don't consider gradient backgrounds.
      LCPHelpers::FinalizeLCPEntryForImage(element, requestProxy,
                                           rectRelativeToSelf);
    }
  }

  return Ok();
}

void nsDisplayBackgroundImage::HitTest(nsDisplayListBuilder* aBuilder,
                                       const nsRect& aRect,
                                       HitTestState* aState,
                                       nsTArray<nsIFrame*>* aOutFrames) {
  if (ShouldIgnoreForBackfaceHidden(aState)) {
    return;
  }

  if (RoundedBorderIntersectsRect(mFrame, ToReferenceFrame(), aRect)) {
    aOutFrames->AppendElement(mFrame);
  }
}

static nsRect GetInsideClipRect(const nsDisplayItem* aItem,
                                StyleBackgroundClip aClip, const nsRect& aRect,
                                const nsRect& aBackgroundRect) {
  if (aRect.IsEmpty()) {
    return {};
  }

  nsIFrame* frame = aItem->Frame();

  nsRect clipRect = aBackgroundRect;
  if (frame->IsCanvasFrame()) {
    nsCanvasFrame* canvasFrame = static_cast<nsCanvasFrame*>(frame);
    clipRect = canvasFrame->CanvasArea() + aItem->ToReferenceFrame();
  } else if (aClip == StyleBackgroundClip::PaddingBox ||
             aClip == StyleBackgroundClip::ContentBox) {
    nsMargin border = frame->GetUsedBorder();
    if (aClip == StyleBackgroundClip::ContentBox) {
      border += frame->GetUsedPadding();
    }
    border.ApplySkipSides(frame->GetSkipSides());
    clipRect.Deflate(border);
  }

  return clipRect.Intersect(aRect);
}

nsRegion nsDisplayBackgroundImage::GetOpaqueRegion(
    nsDisplayListBuilder* aBuilder, bool* aSnap) const {
  nsRegion result;
  *aSnap = false;

  if (!mBackgroundStyle) {
    return result;
  }

  *aSnap = true;

  // For StyleBoxDecorationBreak::Slice, don't try to optimize here, since
  // this could easily lead to O(N^2) behavior inside InlineBackgroundData,
  // which expects frames to be sent to it in content order, not reverse
  // content order which we'll produce here.
  // Of course, if there's only one frame in the flow, it doesn't matter.
  if (mFrame->StyleBorder()->mBoxDecorationBreak ==
          StyleBoxDecorationBreak::Clone ||
      (!mFrame->GetPrevContinuation() && !mFrame->GetNextContinuation())) {
    const nsStyleImageLayers::Layer& layer =
        mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer];
    if (layer.mImage.IsOpaque() && layer.mBlendMode == StyleBlend::Normal &&
        layer.mRepeat.mXRepeat != StyleImageLayerRepeat::Space &&
        layer.mRepeat.mYRepeat != StyleImageLayerRepeat::Space &&
        layer.mClip != StyleBackgroundClip::Text &&
        // 'border-area' leaves a hole in the middle, so nothing is reliably
        // opaque.
        layer.mClip != StyleBackgroundClip::BorderArea) {
      result = GetInsideClipRect(this, layer.mClip, mBounds, mBackgroundRect);
    }
  }

  return result;
}

Maybe<nscolor> nsDisplayBackgroundImage::IsUniform(
    nsDisplayListBuilder* aBuilder) const {
  if (!mBackgroundStyle) {
    return Some(NS_RGBA(0, 0, 0, 0));
  }
  return Nothing();
}

nsRect nsDisplayBackgroundImage::GetPositioningArea() const {
  if (!mBackgroundStyle) {
    return nsRect();
  }
  nsIFrame* attachedToFrame;
  bool transformedFixed;
  return nsCSSRendering::ComputeImageLayerPositioningArea(
             mFrame->PresContext(), mFrame, mBackgroundRect,
             mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer],
             &attachedToFrame, &transformedFixed) +
         ToReferenceFrame();
}

bool nsDisplayBackgroundImage::RenderingMightDependOnPositioningAreaSizeChange()
    const {
  if (!mBackgroundStyle) {
    return false;
  }

  nsRectCornerRadii radii;
  if (mFrame->GetBorderRadii(radii)) {
    // A change in the size of the positioning area might change the position
    // of the rounded corners.
    return true;
  }

  const nsStyleImageLayers::Layer& layer =
      mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer];
  return layer.RenderingMightDependOnPositioningAreaSizeChange();
}

void nsDisplayBackgroundImage::Paint(nsDisplayListBuilder* aBuilder,
                                     gfxContext* aCtx) {
  PaintInternal(aBuilder, aCtx, GetPaintRect(aBuilder, aCtx), &mBounds);
}

void nsDisplayBackgroundImage::PaintInternal(nsDisplayListBuilder* aBuilder,
                                             gfxContext* aCtx,
                                             const nsRect& aBounds,
                                             nsRect* aClipRect) {
  gfxContext* ctx = aCtx;
  const nsStyleImageLayers::Layer& layer =
      mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer];
  StyleBackgroundClip clip = layer.mClip;
  if (clip == StyleBackgroundClip::Text) {
    if (!GenerateAndPushTextMask(StyleFrame(), aCtx, mBackgroundRect,
                                 aBuilder)) {
      return;
    }
  }

  auto popTextGroup = MakeScopeExit([&] {
    if (clip == StyleBackgroundClip::Text) {
      ctx->PopGroupAndBlend();
    }
  });

  // For 'background-clip: border-area' clip out the padding box (the area not
  // painted by the border). The outer border-box clip is applied by the
  // display item's clip.
  Maybe<gfxContextAutoSaveRestore> borderAreaClip;
  if (clip == StyleBackgroundClip::BorderArea) {
    borderAreaClip.emplace(ctx);
    ClipBackgroundToBorderArea(ctx, StyleFrame(), layer, mBackgroundRect);
  }

  nsCSSRendering::PaintBGParams params =
      nsCSSRendering::PaintBGParams::ForSingleLayer(
          *StyleFrame()->PresContext(), aBounds, mBackgroundRect, StyleFrame(),
          aBuilder->GetBackgroundPaintFlags(), mLayer, CompositionOp::OP_OVER,
          1.0f);
  params.bgClipRect = aClipRect;
  (void)nsCSSRendering::PaintStyleImageLayer(params, *aCtx);
}

void nsDisplayBackgroundImage::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  if (!mBackgroundStyle) {
    return;
  }

  const auto* geometry =
      static_cast<const nsDisplayBackgroundGeometry*>(aGeometry);

  bool snap;
  nsRect bounds = GetBounds(aBuilder, &snap);
  nsRect positioningArea = GetPositioningArea();
  if (positioningArea.TopLeft() != geometry->mPositioningArea.TopLeft() ||
      (positioningArea.Size() != geometry->mPositioningArea.Size() &&
       RenderingMightDependOnPositioningAreaSizeChange())) {
    // Positioning area changed in a way that could cause everything to change,
    // so invalidate everything (both old and new painting areas).
    aInvalidRegion->Or(bounds, geometry->mBounds);
    return;
  }
  if (!mDestRect.IsEqualInterior(geometry->mDestRect)) {
    // Dest area changed in a way that could cause everything to change,
    // so invalidate everything (both old and new painting areas).
    aInvalidRegion->Or(bounds, geometry->mBounds);
    return;
  }
  if (!bounds.IsEqualInterior(geometry->mBounds)) {
    // Positioning area is unchanged, so invalidate just the change in the
    // painting area.
    aInvalidRegion->Xor(bounds, geometry->mBounds);
  }
}

nsRect nsDisplayBackgroundImage::GetBounds(nsDisplayListBuilder* aBuilder,
                                           bool* aSnap) const {
  *aSnap = true;
  return mBounds;
}

nsRect nsDisplayBackgroundImage::GetBoundsInternal(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrameForBounds) {
  // This allows nsDisplayTableBackgroundImage to change the frame used for
  // bounds calculation.
  nsIFrame* frame = aFrameForBounds ? aFrameForBounds : mFrame;

  nsPresContext* presContext = frame->PresContext();

  if (!mBackgroundStyle) {
    return nsRect();
  }

  nsRect clipRect = mBackgroundRect;
  if (frame->IsCanvasFrame()) {
    nsCanvasFrame* canvasFrame = static_cast<nsCanvasFrame*>(frame);
    clipRect = canvasFrame->CanvasArea() + ToReferenceFrame();
  }
  const nsStyleImageLayers::Layer& layer =
      mBackgroundStyle->StyleBackground()->mImage.mLayers[mLayer];
  return nsCSSRendering::GetBackgroundLayerRect(
      presContext, frame, mBackgroundRect, clipRect, layer,
      aBuilder->GetBackgroundPaintFlags());
}

nsDisplayTableBackgroundImage::nsDisplayTableBackgroundImage(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, const InitData& aData,
    nsIFrame* aCellFrame)
    : nsDisplayBackgroundImage(aBuilder, aFrame, aData, aCellFrame),
      mStyleFrame(aCellFrame) {
  if (aBuilder->IsRetainingDisplayList()) {
    mStyleFrame->AddDisplayItem(this);
  }
}

void nsDisplayTableBackgroundImage::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mStyleFrame);
  nsDisplayBackgroundImage::Destroy(aBuilder);
}

bool nsDisplayTableBackgroundImage::IsInvalid(nsRect& aRect) const {
  bool result = mStyleFrame ? mStyleFrame->IsInvalid(aRect) : false;
  aRect += ToReferenceFrame();
  return result;
}

nsDisplayThemedBackground::nsDisplayThemedBackground(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
    const nsRect& aBackgroundRect)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mBackgroundRect(aBackgroundRect),
      mThemeTransparency(nsITheme::eUnknownTransparency),
      mAppearance(StyleAppearance::None) {
  MOZ_COUNT_CTOR(nsDisplayThemedBackground);
}

void nsDisplayThemedBackground::Init(nsDisplayListBuilder* aBuilder) {
  const nsStyleDisplay* disp = StyleFrame()->StyleDisplay();
  mAppearance = disp->EffectiveAppearance();
  StyleFrame()->IsThemed(disp, &mThemeTransparency);

  // Perform necessary RegisterThemeGeometry
  nsITheme* theme = StyleFrame()->PresContext()->Theme();
  nsITheme::ThemeGeometryType type =
      theme->ThemeGeometryTypeForWidget(StyleFrame(), mAppearance);
  if (type != nsITheme::eThemeGeometryTypeUnknown) {
    RegisterThemeGeometry(aBuilder, this, StyleFrame(), type);
  }

  mBounds = GetBoundsInternal();
}

void nsDisplayThemedBackground::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (themed, appearance:" << (int)mAppearance << ")";
}

void nsDisplayThemedBackground::HitTest(nsDisplayListBuilder* aBuilder,
                                        const nsRect& aRect,
                                        HitTestState* aState,
                                        nsTArray<nsIFrame*>* aOutFrames) {
  if (ShouldIgnoreForBackfaceHidden(aState)) {
    return;
  }

  // Assume that any point in our background rect is a hit.
  if (mBackgroundRect.Intersects(aRect)) {
    aOutFrames->AppendElement(mFrame);
  }
}

nsRegion nsDisplayThemedBackground::GetOpaqueRegion(
    nsDisplayListBuilder* aBuilder, bool* aSnap) const {
  nsRegion result;
  *aSnap = false;

  if (mThemeTransparency == nsITheme::eOpaque) {
    *aSnap = true;
    result = mBackgroundRect;
  }
  return result;
}

Maybe<nscolor> nsDisplayThemedBackground::IsUniform(
    nsDisplayListBuilder* aBuilder) const {
  return Nothing();
}

nsRect nsDisplayThemedBackground::GetPositioningArea() const {
  return mBackgroundRect;
}

void nsDisplayThemedBackground::Paint(nsDisplayListBuilder* aBuilder,
                                      gfxContext* aCtx) {
  PaintInternal(aBuilder, aCtx, GetPaintRect(aBuilder, aCtx), nullptr);
}

void nsDisplayThemedBackground::PaintInternal(nsDisplayListBuilder* aBuilder,
                                              gfxContext* aCtx,
                                              const nsRect& aBounds,
                                              nsRect* aClipRect) {
  // XXXzw this ignores aClipRect.
  nsPresContext* presContext = StyleFrame()->PresContext();
  nsITheme* theme = presContext->Theme();
  nsRect drawing(mBackgroundRect);
  theme->GetWidgetOverflow(presContext->DeviceContext(), StyleFrame(),
                           mAppearance, &drawing);
  drawing.IntersectRect(drawing, aBounds);
  theme->DrawWidgetBackground(aCtx, StyleFrame(), mAppearance, mBackgroundRect,
                              drawing);
}

WebRenderCommandsResult nsDisplayThemedBackground::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  nsITheme* theme = StyleFrame()->PresContext()->Theme();
  if (!theme->CreateWebRenderCommandsForWidget(aBuilder, aResources, aSc,
                                               aManager, StyleFrame(),
                                               mAppearance, mBackgroundRect)) {
    return Err("theme cannot draw this appearance with WebRender");
  }
  return Ok();
}

bool nsDisplayThemedBackground::IsWindowActive() const {
  return !mFrame->PresContext()->Document()->IsTopLevelWindowInactive();
}

void nsDisplayThemedBackground::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const auto* geometry =
      static_cast<const nsDisplayThemedBackgroundGeometry*>(aGeometry);

  bool snap;
  nsRect bounds = GetBounds(aBuilder, &snap);
  nsRect positioningArea = GetPositioningArea();
  if (!positioningArea.IsEqualInterior(geometry->mPositioningArea)) {
    // Invalidate everything (both old and new painting areas).
    aInvalidRegion->Or(bounds, geometry->mBounds);
    return;
  }
  if (!bounds.IsEqualInterior(geometry->mBounds)) {
    // Positioning area is unchanged, so invalidate just the change in the
    // painting area.
    aInvalidRegion->Xor(bounds, geometry->mBounds);
  }
  nsITheme* theme = StyleFrame()->PresContext()->Theme();
  if (theme->WidgetAppearanceDependsOnWindowFocus(mAppearance) &&
      IsWindowActive() != geometry->mWindowIsActive) {
    aInvalidRegion->Or(*aInvalidRegion, bounds);
  }
}

nsRect nsDisplayThemedBackground::GetBounds(nsDisplayListBuilder* aBuilder,
                                            bool* aSnap) const {
  *aSnap = true;
  return mBounds;
}

nsRect nsDisplayThemedBackground::GetBoundsInternal() {
  nsPresContext* presContext = mFrame->PresContext();

  nsRect r = mBackgroundRect - ToReferenceFrame();
  presContext->Theme()->GetWidgetOverflow(
      presContext->DeviceContext(), mFrame,
      mFrame->StyleDisplay()->EffectiveAppearance(), &r);
  return r + ToReferenceFrame();
}

void nsDisplayTableThemedBackground::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mAncestorFrame);
  nsDisplayThemedBackground::Destroy(aBuilder);
}

#if defined(MOZ_REFLOW_PERF_DSP) && defined(MOZ_REFLOW_PERF)
void nsDisplayReflowCount::Paint(nsDisplayListBuilder* aBuilder,
                                 gfxContext* aCtx) {
  mFrame->PresShell()->PaintCount(mFrameName, aCtx, mFrame->PresContext(),
                                  mFrame, ToReferenceFrame(), mColor);
}
#endif

void nsDisplayBackgroundColor::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mDependentFrame);
  nsPaintedDisplayItem::Destroy(aBuilder);
}

bool nsDisplayBackgroundColor::CanApplyOpacity(
    WebRenderLayerManager* aManager, nsDisplayListBuilder* aBuilder) const {
  // Don't apply opacity if the background color is animated since the color is
  // going to be changed on the compositor.
  return !EffectCompositor::HasAnimationsForCompositor(
      mFrame, DisplayItemType::TYPE_BACKGROUND_COLOR);
}

WebRenderCommandsResult nsDisplayBackgroundColor::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  gfx::sRGBColor color = mColor;
  color.a *= aBuilder.GetInheritedOpacity();

  if (color == sRGBColor() &&
      !EffectCompositor::HasAnimationsForCompositor(
          mFrame, DisplayItemType::TYPE_BACKGROUND_COLOR)) {
    return Ok();
  }

  if (HasBackgroundClipText()) {
    return Err("background-clip: text is not supported");
  }

  uint64_t animationsId = 0;
  // We don't support background-color animations on table elements yet.
  if (GetType() == DisplayItemType::TYPE_BACKGROUND_COLOR) {
    animationsId =
        AddAnimationsForWebRender(this, aManager, aDisplayListBuilder);
  }

  LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
      mBackgroundRect, mFrame->PresContext()->AppUnitsPerDevPixel());
  wr::LayoutRect r = wr::ToLayoutRect(bounds);

  Maybe<wr::SpaceAndClipChainHelper> borderAreaClip;
  if (mBottomLayerClip == StyleBackgroundClip::BorderArea) {
    PushBorderAreaClipOut(aBuilder, mFrame,
                          mFrame->StyleBackground()->BottomLayer(),
                          mBackgroundRect, borderAreaClip);
  }

  if (animationsId) {
    wr::WrAnimationProperty prop{
        wr::WrAnimationType::BackgroundColor,
        animationsId,
    };
    aBuilder.PushRectWithAnimation(r, r, !BackfaceIsHidden(),
                                   wr::ToColorF(ToDeviceColor(color)), &prop);
  } else {
    aBuilder.PushRect(r, r, !BackfaceIsHidden(), false, false,
                      wr::ToColorF(ToDeviceColor(color)));
  }

  return Ok();
}

void nsDisplayBackgroundColor::PaintWithClip(nsDisplayListBuilder* aBuilder,
                                             gfxContext* aCtx,
                                             const DisplayItemClip& aClip) {
  MOZ_ASSERT(!HasBackgroundClipText());
  MOZ_ASSERT(mBottomLayerClip != StyleBackgroundClip::BorderArea);

  if (mColor == sRGBColor()) {
    return;
  }

  nsRect fillRect = mBackgroundRect;
  if (aClip.HasClip()) {
    fillRect.IntersectRect(fillRect, aClip.GetClipRect());
  }

  DrawTarget* dt = aCtx->GetDrawTarget();
  int32_t A2D = mFrame->PresContext()->AppUnitsPerDevPixel();
  Rect bounds = ToRect(nsLayoutUtils::RectToGfxRect(fillRect, A2D));
  MaybeSnapToDevicePixels(bounds, *dt);
  ColorPattern fill(ToDeviceColor(mColor));

  if (aClip.GetRoundedRectCount()) {
    MOZ_ASSERT(aClip.GetRoundedRectCount() == 1);

    AutoTArray<DisplayItemClip::RoundedRect, 1> roundedRect;
    aClip.AppendRoundedRects(&roundedRect);

    bool pushedClip = false;
    if (!fillRect.Contains(roundedRect[0].mRect)) {
      dt->PushClipRect(bounds);
      pushedClip = true;
    }

    RectCornerRadii pixelRadii;
    nsCSSRendering::ComputePixelRadii(roundedRect[0].mRadii, A2D, &pixelRadii);
    dt->FillRoundedRect(
        RoundedRect(NSRectToSnappedRect(roundedRect[0].mRect, A2D, *dt),
                    pixelRadii),
        fill);
    if (pushedClip) {
      dt->PopClip();
    }
  } else {
    dt->FillRect(bounds, fill);
  }
}

void nsDisplayBackgroundColor::Paint(nsDisplayListBuilder* aBuilder,
                                     gfxContext* aCtx) {
  if (mColor == sRGBColor()) {
    return;
  }

#if 0
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1148418#c21 for why this
  // results in a precision induced rounding issue that makes the rect one
  // pixel shorter in rare cases. Disabled in favor of the old code for now.
  // Note that the pref layout.css.devPixelsPerPx needs to be set to 1 to
  // reproduce the bug.
  //
  // TODO:
  // This new path does not include support for background-clip:text; need to
  // be fixed if/when we switch to this new code path.

  DrawTarget& aDrawTarget = *aCtx->GetDrawTarget();

  Rect rect = NSRectToSnappedRect(mBackgroundRect,
                                  mFrame->PresContext()->AppUnitsPerDevPixel(),
                                  aDrawTarget);
  ColorPattern color(ToDeviceColor(mColor));
  aDrawTarget.FillRect(rect, color);
#else
  gfxContext* ctx = aCtx;
  gfxRect bounds = nsLayoutUtils::RectToGfxRect(
      mBackgroundRect, mFrame->PresContext()->AppUnitsPerDevPixel());

  if (HasBackgroundClipText()) {
    if (!GenerateAndPushTextMask(mFrame, aCtx, mBackgroundRect, aBuilder)) {
      return;
    }

    ctx->SetColor(mColor);
    ctx->NewPath();
    ctx->SnappedRectangle(bounds);
    ctx->Fill();
    ctx->PopGroupAndBlend();
    return;
  }

  Maybe<gfxContextAutoSaveRestore> borderAreaClip;
  if (mBottomLayerClip == StyleBackgroundClip::BorderArea) {
    borderAreaClip.emplace(ctx);
    ClipBackgroundToBorderArea(
        ctx, mFrame, mFrame->StyleBackground()->BottomLayer(), mBackgroundRect);
  }

  ctx->SetColor(mColor);
  ctx->NewPath();
  ctx->SnappedRectangle(bounds);
  ctx->Fill();
#endif
}

nsRegion nsDisplayBackgroundColor::GetOpaqueRegion(
    nsDisplayListBuilder* aBuilder, bool* aSnap) const {
  *aSnap = false;

  if (mColor.a != 1 ||
      // Even if the current alpha channel is 1, we treat this item as if it's
      // non-opaque if there is a background-color animation since the animation
      // might change the alpha channel.
      EffectCompositor::HasAnimationsForCompositor(
          mFrame, DisplayItemType::TYPE_BACKGROUND_COLOR)) {
    return nsRegion();
  }

  if (!mHasStyle || HasBackgroundClipText() ||
      mBottomLayerClip == StyleBackgroundClip::BorderArea) {
    return nsRegion();
  }

  *aSnap = true;
  return GetInsideClipRect(this, mBottomLayerClip, mBackgroundRect,
                           mBackgroundRect);
}

Maybe<nscolor> nsDisplayBackgroundColor::IsUniform(
    nsDisplayListBuilder* aBuilder) const {
  return Some(mColor.ToABGR());
}

void nsDisplayBackgroundColor::HitTest(nsDisplayListBuilder* aBuilder,
                                       const nsRect& aRect,
                                       HitTestState* aState,
                                       nsTArray<nsIFrame*>* aOutFrames) {
  if (ShouldIgnoreForBackfaceHidden(aState)) {
    return;
  }

  if (!RoundedBorderIntersectsRect(mFrame, ToReferenceFrame(), aRect)) {
    // aRect doesn't intersect our border-radius curve.
    return;
  }

  aOutFrames->AppendElement(mFrame);
}

void nsDisplayBackgroundColor::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (rgba " << mColor.r << "," << mColor.g << "," << mColor.b << ","
          << mColor.a << ")";
  aStream << " backgroundRect" << mBackgroundRect;
}

nsRect nsDisplayOutline::GetBounds(nsDisplayListBuilder* aBuilder,
                                   bool* aSnap) const {
  *aSnap = false;
  return mFrame->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
}

nsRect nsDisplayOutline::GetInnerRect() const {
  if (nsRect* savedOutlineInnerRect =
          mFrame->GetProperty(nsIFrame::OutlineInnerRectProperty())) {
    return *savedOutlineInnerRect;
  }
  return mFrame->GetRectRelativeToSelf();
}

void nsDisplayOutline::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  // TODO join outlines together
  MOZ_ASSERT(mFrame->StyleOutline()->ShouldPaintOutline(),
             "Should have not created a nsDisplayOutline!");

  nsRect rect = GetInnerRect() + ToReferenceFrame();
  nsPresContext* pc = mFrame->PresContext();
  if (IsThemedOutline()) {
    pc->Theme()->DrawWidgetBackground(aCtx, mFrame,
                                      StyleAppearance::FocusOutline, rect,
                                      GetPaintRect(aBuilder, aCtx));
    return;
  }

  nsCSSRendering::PaintNonThemedOutline(
      pc, *aCtx, mFrame, GetPaintRect(aBuilder, aCtx), rect, mFrame->Style());
}

bool nsDisplayOutline::IsThemedOutline() const {
#ifdef DEBUG
  nsPresContext* pc = mFrame->PresContext();
  MOZ_ASSERT(
      pc->Theme()->ThemeSupportsWidget(pc, mFrame,
                                       StyleAppearance::FocusOutline),
      "All of our supported platforms have support for themed focus-outlines");
#endif
  return mFrame->StyleOutline()->mOutlineStyle.IsAuto();
}

WebRenderCommandsResult nsDisplayOutline::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  nsPresContext* pc = mFrame->PresContext();
  nsRect rect = GetInnerRect() + ToReferenceFrame();
  if (IsThemedOutline()) {
    if (!pc->Theme()->CreateWebRenderCommandsForWidget(
            aBuilder, aResources, aSc, aManager, mFrame,
            StyleAppearance::FocusOutline, rect)) {
      return Err("theme cannot draw the focus outline with WebRender");
    }
    return Ok();
  }

  bool dummy;
  Maybe<nsCSSBorderRenderer> borderRenderer =
      nsCSSRendering::CreateBorderRendererForNonThemedOutline(
          pc, /* aDrawTarget = */ nullptr, mFrame,
          GetBounds(aDisplayListBuilder, &dummy), rect, mFrame->Style());

  if (!borderRenderer) {
    // No border renderer means "there is no outline".
    // Paint nothing and return success.
    return Ok();
  }

  borderRenderer->CreateWebRenderCommands(this, aBuilder, aResources, aSc);
  return Ok();
}

bool nsDisplayOutline::HasRadius() const {
  const auto& radius = mFrame->StyleBorder()->mBorderRadius;
  return !nsLayoutUtils::HasNonZeroCorner(radius);
}

bool nsDisplayOutline::IsInvisibleInRect(const nsRect& aRect) const {
  nsRect borderBox(ToReferenceFrame(), mFrame->GetSize());
  // aRect is entirely inside the border-rect, and the outline isn't rendered
  // inside the border-rect, so the outline is not visible.
  return borderBox.Contains(aRect) && !HasRadius() &&
         mFrame->StyleOutline()->mOutlineOffset >= 0;
}

void nsDisplayEventReceiver::HitTest(nsDisplayListBuilder* aBuilder,
                                     const nsRect& aRect, HitTestState* aState,
                                     nsTArray<nsIFrame*>* aOutFrames) {
  if (ShouldIgnoreForBackfaceHidden(aState)) {
    return;
  }

  if (!RoundedBorderIntersectsRect(mFrame, ToReferenceFrame(), aRect)) {
    // aRect doesn't intersect our border-radius curve.
    return;
  }

  aOutFrames->AppendElement(mFrame);
}

WebRenderCommandsResult nsDisplayCompositorHitTestInfo::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  return Ok();
}

int32_t nsDisplayCompositorHitTestInfo::ZIndex() const {
  return mOverrideZIndex ? *mOverrideZIndex : nsDisplayItem::ZIndex();
}

void nsDisplayCompositorHitTestInfo::SetOverrideZIndex(int32_t aZIndex) {
  mOverrideZIndex = Some(aZIndex);
}

nsDisplayCaret::nsDisplayCaret(nsDisplayListBuilder* aBuilder,
                               nsIFrame* aCaretFrame)
    : nsPaintedDisplayItem(aBuilder, aCaretFrame),
      mCaret(aBuilder->GetCaret()),
      mBounds(aBuilder->GetCaretRect() + ToReferenceFrame()) {
  MOZ_COUNT_CTOR(nsDisplayCaret);
  // The presence of a caret doesn't change the overflow rect
  // of the owning frame, so the normal building rect might not
  // include the caret at all. We use MarkFrameForDisplay to ensure
  // we build this item, and here we override the building rect
  // to cover the pixels we're going to draw.
  SetBuildingRect(mBounds);
}

nsRect nsDisplayCaret::GetBounds(nsDisplayListBuilder* aBuilder,
                                 bool* aSnap) const {
  *aSnap = true;
  // The caret returns a rect in the coordinates of mFrame.
  return mBounds;
}

void nsDisplayCaret::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  // Note: Because we exist, we know that the caret is visible, so we don't
  // need to check for the caret's visibility.
  mCaret->PaintCaret(*aCtx->GetDrawTarget(), mFrame, ToReferenceFrame());
}

WebRenderCommandsResult nsDisplayCaret::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  using namespace layers;
  nsRect caretRect;
  nsRect hookRect;
  nscolor caretColor;
  nsIFrame* frame =
      mCaret->GetPaintGeometry(&caretRect, &hookRect, &caretColor);
  if (NS_WARN_IF(!frame) || NS_WARN_IF(frame != mFrame)) {
    return Ok();
  }

  int32_t appUnitsPerDevPixel = frame->PresContext()->AppUnitsPerDevPixel();
  gfx::DeviceColor color = ToDeviceColor(caretColor);
  LayoutDeviceRect devCaretRect = LayoutDeviceRect::FromAppUnits(
      caretRect + ToReferenceFrame(), appUnitsPerDevPixel);
  LayoutDeviceRect devHookRect = LayoutDeviceRect::FromAppUnits(
      hookRect + ToReferenceFrame(), appUnitsPerDevPixel);

  wr::LayoutRect caret = wr::ToLayoutRect(devCaretRect);
  wr::LayoutRect hook = wr::ToLayoutRect(devHookRect);

  // Note, WR will pixel snap anything that is layout aligned.
  aBuilder.PushRect(caret, caret, !BackfaceIsHidden(), false, false,
                    wr::ToColorF(color));

  if (!devHookRect.IsEmpty()) {
    aBuilder.PushRect(hook, hook, !BackfaceIsHidden(), false, false,
                      wr::ToColorF(color));
  }
  return Ok();
}

nsDisplayBorder::nsDisplayBorder(nsDisplayListBuilder* aBuilder,
                                 nsIFrame* aFrame)
    : nsPaintedDisplayItem(aBuilder, aFrame) {
  MOZ_COUNT_CTOR(nsDisplayBorder);

  mBounds = CalculateBounds<nsRect>(*mFrame->StyleBorder());
}

bool nsDisplayBorder::IsInvisibleInRect(const nsRect& aRect) const {
  nsRect paddingRect = GetPaddingRect();
  const nsStyleBorder* styleBorder;
  if (paddingRect.Contains(aRect) &&
      !(styleBorder = mFrame->StyleBorder())->IsBorderImageSizeAvailable() &&
      !nsLayoutUtils::HasNonZeroCorner(styleBorder->mBorderRadius)) {
    // aRect is entirely inside the content rect, and no part
    // of the border is rendered inside the content rect, so we are not
    // visible
    // Skip this if there's a border-image (which draws a background
    // too) or if there is a border-radius (which makes the border draw
    // further in).
    return true;
  }

  return false;
}

nsDisplayItemGeometry* nsDisplayBorder::AllocateGeometry(
    nsDisplayListBuilder* aBuilder) {
  return new nsDisplayBorderGeometry(this, aBuilder);
}

void nsDisplayBorder::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const auto* geometry = static_cast<const nsDisplayBorderGeometry*>(aGeometry);
  bool snap;

  if (!geometry->mBounds.IsEqualInterior(GetBounds(aBuilder, &snap))) {
    // We can probably get away with only invalidating the difference
    // between the border and padding rects, but the XUL ui at least
    // is apparently painting a background with this?
    aInvalidRegion->Or(GetBounds(aBuilder, &snap), geometry->mBounds);
  }
}

WebRenderCommandsResult nsDisplayBorder::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  nsRect rect = nsRect(ToReferenceFrame(), mFrame->GetSize());

  ImgDrawResult drawResult = nsCSSRendering::CreateWebRenderCommandsForBorder(
      this, mFrame, rect, aBuilder, aResources, aSc, aManager,
      aDisplayListBuilder);

  if (drawResult == ImgDrawResult::NOT_SUPPORTED) {
    return Err("border is not WebRender-representable");
  }
  return Ok();
};

void nsDisplayBorder::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  nsPoint offset = ToReferenceFrame();

  PaintBorderFlags flags = aBuilder->ShouldSyncDecodeImages()
                               ? PaintBorderFlags::SyncDecodeImages
                               : PaintBorderFlags();

  (void)nsCSSRendering::PaintBorder(
      mFrame->PresContext(), *aCtx, mFrame, GetPaintRect(aBuilder, aCtx),
      nsRect(offset, mFrame->GetSize()), mFrame->Style(), flags,
      mFrame->GetSkipSides());
}

nsRect nsDisplayBorder::GetBounds(nsDisplayListBuilder* aBuilder,
                                  bool* aSnap) const {
  *aSnap = true;
  return mBounds;
}

void nsDisplayBoxShadowOuter::Paint(nsDisplayListBuilder* aBuilder,
                                    gfxContext* aCtx) {
  nsPoint offset = ToReferenceFrame();
  nsRect borderRect = mFrame->VisualBorderRectRelativeToSelf() + offset;
  nsPresContext* presContext = mFrame->PresContext();

  AUTO_PROFILER_LABEL("nsDisplayBoxShadowOuter::Paint", GRAPHICS);

  nsCSSRendering::PaintBoxShadowOuter(presContext, *aCtx, mFrame, borderRect,
                                      GetPaintRect(aBuilder, aCtx), 1.0f);
}

nsRect nsDisplayBoxShadowOuter::GetBounds(nsDisplayListBuilder* aBuilder,
                                          bool* aSnap) const {
  *aSnap = false;
  return mBounds;
}

nsRect nsDisplayBoxShadowOuter::GetBoundsInternal() {
  return nsLayoutUtils::GetBoxShadowRectForFrame(mFrame, mFrame->GetSize()) +
         ToReferenceFrame();
}

bool nsDisplayBoxShadowOuter::IsInvisibleInRect(const nsRect& aRect) const {
  nsPoint origin = ToReferenceFrame();
  nsRect frameRect(origin, mFrame->GetSize());
  if (!frameRect.Contains(aRect)) {
    return false;
  }

  // the visible region is entirely inside the border-rect, and box shadows
  // never render within the border-rect (unless there's a border radius).
  nsRectCornerRadii twipsRadii;
  bool hasBorderRadii = mFrame->GetBorderRadii(twipsRadii);
  if (!hasBorderRadii) {
    return true;
  }

  return RoundedRectContainsRect(frameRect, twipsRadii, aRect);
}

bool nsDisplayBoxShadowOuter::CanBuildWebRenderDisplayItems() const {
  auto shadows = mFrame->StyleEffects()->mBoxShadow.AsSpan();
  if (shadows.IsEmpty()) {
    return false;
  }

  bool hasBorderRadius;
  // We don't support native themed things yet like box shadows around
  // input buttons.
  //
  // TODO(emilio): The non-native theme could provide the right rect+radius
  // instead relatively painlessly, if we find this causes performance issues or
  // what not.
  return !nsCSSRendering::HasBoxShadowNativeTheme(mFrame, hasBorderRadius);
}

WebRenderCommandsResult nsDisplayBoxShadowOuter::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!CanBuildWebRenderDisplayItems()) {
    return Err("outer box-shadow is not WebRender-representable");
  }

  int32_t appUnitsPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  nsPoint offset = ToReferenceFrame();
  nsRect borderRect = mFrame->VisualBorderRectRelativeToSelf() + offset;
  bool snap;
  nsRect bounds = GetBounds(aDisplayListBuilder, &snap);

  bool hasBorderRadius;
  bool nativeTheme =
      nsCSSRendering::HasBoxShadowNativeTheme(mFrame, hasBorderRadius);

  // Don't need the full size of the shadow rect like we do in
  // nsCSSRendering since WR takes care of calculations for blur
  // and spread radius.
  nsRect frameRect =
      nsCSSRendering::GetShadowRect(borderRect, nativeTheme, mFrame);

  RectCornerRadii borderRadii;
  if (hasBorderRadius) {
    hasBorderRadius = nsCSSRendering::GetBorderRadii(frameRect, borderRect,
                                                     mFrame, borderRadii);
  }

  // For sliced inline elements split across continuations, frameRect is the
  // joined-box rect spanning all continuations, but each continuation pushes
  // its own box-shadow primitive. Without clamping the clip to the actual
  // fragment edges on the sides shared with sibling continuations, blur from
  // the joined-box shadow leaks past the line-break edges. Mirrors the
  // fragmentClip logic in nsCSSRendering::PaintBoxShadowOuter.
  Sides skipSides = mFrame->GetSkipSides();
  if (!skipSides.IsEmpty()) {
    if (skipSides.Left()) {
      nscoord xmost = bounds.XMost();
      bounds.x = borderRect.x;
      bounds.width = xmost - bounds.x;
    }
    if (skipSides.Right()) {
      nscoord overflow = bounds.XMost() - borderRect.XMost();
      if (overflow > 0) {
        bounds.width -= overflow;
      }
    }
    if (skipSides.Top()) {
      nscoord ymost = bounds.YMost();
      bounds.y = borderRect.y;
      bounds.height = ymost - bounds.y;
    }
    if (skipSides.Bottom()) {
      nscoord overflow = bounds.YMost() - borderRect.YMost();
      if (overflow > 0) {
        bounds.height -= overflow;
      }
    }
  }

  // Everything here is in app units, change to device units.
  LayoutDeviceRect clipRect =
      LayoutDeviceRect::FromAppUnits(bounds, appUnitsPerDevPixel);
  auto shadows = mFrame->StyleEffects()->mBoxShadow.AsSpan();
  MOZ_ASSERT(!shadows.IsEmpty());

  for (const auto& shadow : Reversed(shadows)) {
    if (shadow.inset) {
      continue;
    }

    float blurRadius =
        float(shadow.base.blur.ToAppUnits()) / float(appUnitsPerDevPixel);
    gfx::sRGBColor shadowColor = nsCSSRendering::GetShadowColor(
        shadow.base, mFrame, aBuilder.GetInheritedOpacity());

    // We don't move the shadow rect here since WR does it for us
    // Now translate everything to device pixels.
    const nsRect& shadowRect = frameRect;
    LayoutDevicePoint shadowOffset = LayoutDevicePoint::FromAppUnits(
        nsPoint(shadow.base.horizontal.ToAppUnits(),
                shadow.base.vertical.ToAppUnits()),
        appUnitsPerDevPixel);

    LayoutDeviceRect deviceBox =
        LayoutDeviceRect::FromAppUnits(shadowRect, appUnitsPerDevPixel);
    wr::LayoutRect deviceBoxRect = wr::ToLayoutRect(deviceBox);
    wr::LayoutRect deviceClipRect = wr::ToLayoutRect(clipRect);

    nscoord spread = shadow.spread.ToAppUnits();
    float spreadRadius = float(spread) / float(appUnitsPerDevPixel);

    wr::BorderRadius borderRadius = wr::EmptyBorderRadius();
    wr::BorderRadius shadowRadius = wr::EmptyBorderRadius();
    if (hasBorderRadius) {
      borderRadius = wr::ToBorderRadius(borderRadii);
      if (spreadRadius) {
        auto shadowRadii = borderRadii;
        shadowRadii.AdjustOutwards(
            Margin(spreadRadius, spreadRadius, spreadRadius, spreadRadius));
        shadowRadius = wr::ToBorderRadius(shadowRadii);
      } else {
        shadowRadius = borderRadius;
      }
    }

    aBuilder.PushBoxShadow(deviceBoxRect, deviceClipRect, !BackfaceIsHidden(),
                           deviceBoxRect, wr::ToLayoutVector2D(shadowOffset),
                           wr::ToColorF(ToDeviceColor(shadowColor)), blurRadius,
                           spreadRadius, borderRadius, shadowRadius,
                           wr::BoxShadowClipMode::Outset);
  }

  return Ok();
}

void nsDisplayBoxShadowOuter::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const auto* geometry =
      static_cast<const nsDisplayItemGenericGeometry*>(aGeometry);
  bool snap;
  if (!geometry->mBounds.IsEqualInterior(GetBounds(aBuilder, &snap)) ||
      !geometry->mBorderRect.IsEqualInterior(GetBorderRect())) {
    nsRegion oldShadow, newShadow;
    nsRectCornerRadii dontCare;
    bool hasBorderRadius = mFrame->GetBorderRadii(dontCare);
    if (hasBorderRadius) {
      // If we have rounded corners then we need to invalidate the frame area
      // too since we paint into it.
      oldShadow = geometry->mBounds;
      newShadow = GetBounds(aBuilder, &snap);
    } else {
      oldShadow.Sub(geometry->mBounds, geometry->mBorderRect);
      newShadow.Sub(GetBounds(aBuilder, &snap), GetBorderRect());
    }
    aInvalidRegion->Or(oldShadow, newShadow);
  }
}

void nsDisplayBoxShadowInner::Paint(nsDisplayListBuilder* aBuilder,
                                    gfxContext* aCtx) {
  nsPoint offset = ToReferenceFrame();
  nsRect borderRect = nsRect(offset, mFrame->GetSize());
  nsPresContext* presContext = mFrame->PresContext();

  AUTO_PROFILER_LABEL("nsDisplayBoxShadowInner::Paint", GRAPHICS);

  nsCSSRendering::PaintBoxShadowInner(presContext, *aCtx, mFrame, borderRect);
}

bool nsDisplayBoxShadowInner::CanCreateWebRenderCommands(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
    const nsPoint& aReferenceOffset) {
  auto shadows = aFrame->StyleEffects()->mBoxShadow.AsSpan();
  if (shadows.IsEmpty()) {
    // Means we don't have to paint anything
    return true;
  }

  bool hasBorderRadius;
  bool nativeTheme =
      nsCSSRendering::HasBoxShadowNativeTheme(aFrame, hasBorderRadius);

  // We don't support native themed things yet like box shadows around
  // input buttons.
  return !nativeTheme;
}

/* static */
void nsDisplayBoxShadowInner::CreateInsetBoxShadowWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, const StackingContextHelper& aSc,
    nsRect& aVisibleRect, nsIFrame* aFrame, const nsRect& aBorderRect) {
  if (!nsCSSRendering::ShouldPaintBoxShadowInner(aFrame)) {
    return;
  }

  int32_t appUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();

  auto shadows = aFrame->StyleEffects()->mBoxShadow.AsSpan();

  LayoutDeviceRect clipRect =
      LayoutDeviceRect::FromAppUnits(aVisibleRect, appUnitsPerDevPixel);

  for (const auto& shadow : Reversed(shadows)) {
    if (!shadow.inset) {
      continue;
    }

    nsRect shadowRect =
        nsCSSRendering::GetBoxShadowInnerPaddingRect(aFrame, aBorderRect);
    RectCornerRadii innerRadii;
    bool hasBorderRadius =
        nsCSSRendering::GetShadowInnerRadii(aFrame, aBorderRect, innerRadii);

    // Now translate everything to device pixels.
    LayoutDeviceRect deviceBoxRect =
        LayoutDeviceRect::FromAppUnits(shadowRect, appUnitsPerDevPixel);
    wr::LayoutRect deviceClipRect = wr::ToLayoutRect(clipRect);
    sRGBColor shadowColor =
        nsCSSRendering::GetShadowColor(shadow.base, aFrame, 1.0);

    LayoutDevicePoint shadowOffset = LayoutDevicePoint::FromAppUnits(
        nsPoint(shadow.base.horizontal.ToAppUnits(),
                shadow.base.vertical.ToAppUnits()),
        appUnitsPerDevPixel);

    float blurRadius =
        float(shadow.base.blur.ToAppUnits()) / float(appUnitsPerDevPixel);

    nscoord spread = shadow.spread.ToAppUnits();
    float spreadRadius = spread / float(appUnitsPerDevPixel);

    wr::BorderRadius borderRadius = wr::EmptyBorderRadius();
    wr::BorderRadius shadowRadius = wr::EmptyBorderRadius();
    if (hasBorderRadius) {
      borderRadius = wr::ToBorderRadius(innerRadii);
      if (spreadRadius) {
        RectCornerRadii shadowRadii = innerRadii;
        shadowRadii.AdjustInwards(
            Margin(spreadRadius, spreadRadius, spreadRadius, spreadRadius));
        shadowRadius = wr::ToBorderRadius(shadowRadii);
      } else {
        shadowRadius = borderRadius;
      }
    }

    aBuilder.PushBoxShadow(
        wr::ToLayoutRect(deviceBoxRect), deviceClipRect,
        !aFrame->BackfaceIsHidden(), wr::ToLayoutRect(deviceBoxRect),
        wr::ToLayoutVector2D(shadowOffset),
        wr::ToColorF(ToDeviceColor(shadowColor)), blurRadius, spreadRadius,
        borderRadius, shadowRadius, wr::BoxShadowClipMode::Inset);
  }
}

WebRenderCommandsResult nsDisplayBoxShadowInner::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!CanCreateWebRenderCommands(aDisplayListBuilder, mFrame,
                                  ToReferenceFrame())) {
    return Err("inset box-shadow is not WebRender-representable");
  }

  bool snap;
  nsRect visible = GetBounds(aDisplayListBuilder, &snap);
  nsPoint offset = ToReferenceFrame();
  nsRect borderRect = nsRect(offset, mFrame->GetSize());
  nsDisplayBoxShadowInner::CreateInsetBoxShadowWebRenderCommands(
      aBuilder, aSc, visible, mFrame, borderRect);

  return Ok();
}

nsDisplayWrapList::nsDisplayWrapList(nsDisplayListBuilder* aBuilder,
                                     nsIFrame* aFrame, nsDisplayList* aList,
                                     bool aClearClipChain)
    : nsDisplayWrapList(aBuilder, aFrame, aList,
                        aBuilder->CurrentActiveScrolledRoot(),
                        ContainerASRType::Constant, aClearClipChain) {}

nsDisplayWrapList::nsDisplayWrapList(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, bool aClearClipChain)
    : nsPaintedDisplayItem(aBuilder, aFrame, aActiveScrolledRoot),
      mFrameASR(aContainerASRType == ContainerASRType::AncestorOfContained
                    ? aBuilder->CurrentActiveScrolledRoot()
                    : nullptr),
      mOverrideZIndex(0),
      mContainerASRType(aContainerASRType),
      mHasZIndexOverride(false),
      mClearingClipChain(aClearClipChain) {
  MOZ_COUNT_CTOR(nsDisplayWrapList);

  mBaseBuildingRect = GetBuildingRect();

  mListPtr = &mList;
  mListPtr->AppendToTop(aList);
  mOriginalClipChain = mClipChain;
  nsDisplayWrapList::UpdateBounds(aBuilder);
}

nsDisplayWrapList::nsDisplayWrapList(nsDisplayListBuilder* aBuilder,
                                     nsIFrame* aFrame, nsDisplayItem* aItem)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mOverrideZIndex(0),
      mHasZIndexOverride(false) {
  MOZ_COUNT_CTOR(nsDisplayWrapList);

  mBaseBuildingRect = GetBuildingRect();

  mListPtr = &mList;
  mListPtr->AppendToTop(aItem);
  mOriginalClipChain = mClipChain;
  nsDisplayWrapList::UpdateBounds(aBuilder);

  if (!aFrame || !aFrame->IsTransformed()) {
    return;
  }

  // See the previous nsDisplayWrapList constructor
  if (aItem->Frame() == aFrame) {
    mToReferenceFrame = aItem->ToReferenceFrame();
  }

  nsRect visible = aBuilder->GetVisibleRect() +
                   aBuilder->GetCurrentFrameOffsetToReferenceFrame();

  SetBuildingRect(visible);
}

void nsDisplayWrapList::HitTest(nsDisplayListBuilder* aBuilder,
                                const nsRect& aRect, HitTestState* aState,
                                nsTArray<nsIFrame*>* aOutFrames) {
  mListPtr->HitTest(aBuilder, aRect, aState, aOutFrames);
}

nsRect nsDisplayWrapList::GetBounds(nsDisplayListBuilder* aBuilder,
                                    bool* aSnap) const {
  *aSnap = false;
  return mBounds;
}

nsRegion nsDisplayWrapList::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                            bool* aSnap) const {
  *aSnap = false;
  bool snap;
  return ::mozilla::GetOpaqueRegion(aBuilder, GetChildren(),
                                    GetBounds(aBuilder, &snap));
}

Maybe<nscolor> nsDisplayWrapList::IsUniform(
    nsDisplayListBuilder* aBuilder) const {
  // We could try to do something but let's conservatively just return Nothing.
  return Nothing();
}

void nsDisplayWrapper::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  NS_ERROR("nsDisplayWrapper should have been flattened away for painting");
}

nsRect nsDisplayWrapList::GetComponentAlphaBounds(
    nsDisplayListBuilder* aBuilder) const {
  return mListPtr->GetComponentAlphaBounds(aBuilder);
}

WebRenderCommandsResult
nsDisplayWrapList::CreateWebRenderCommandsNewClipListOption(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, bool aNewClipList) {
  aManager->CommandBuilder().CreateWebRenderCommandsFromDisplayList(
      GetChildren(), this, aDisplayListBuilder, aSc, aBuilder, aResources,
      aNewClipList);
  return Ok();
}

nsDisplayOpacity::nsDisplayOpacity(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, bool aForEventsOnly,
    bool aNeedsActiveLayer, bool aWrapsBackdropFilter, bool aForceIsolation)
    : nsDisplayWrapList(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, true),
      mOpacity(aFrame->StyleEffects()->mOpacity),
      mForEventsOnly(aForEventsOnly),
      mNeedsActiveLayer(aNeedsActiveLayer),
      mChildOpacityState(ChildOpacityState::Unknown),
      mWrapsBackdropFilter(aWrapsBackdropFilter),
      mForceIsolation(aForceIsolation) {
  MOZ_COUNT_CTOR(nsDisplayOpacity);
}

void nsDisplayOpacity::HitTest(nsDisplayListBuilder* aBuilder,
                               const nsRect& aRect,
                               nsDisplayItem::HitTestState* aState,
                               nsTArray<nsIFrame*>* aOutFrames) {
  AutoRestore<float> opacity(aState->mCurrentOpacity);
  aState->mCurrentOpacity *= mOpacity;

  // TODO(emilio): special-casing zero is a bit arbitrary... Maybe we should
  // only consider fully opaque items? Or make this configurable somehow?
  if (aBuilder->HitTestIsForVisibility() && mOpacity == 0.0f) {
    return;
  }
  nsDisplayWrapList::HitTest(aBuilder, aRect, aState, aOutFrames);
}

nsRegion nsDisplayOpacity::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                           bool* aSnap) const {
  *aSnap = false;
  // The only time where mOpacity == 1.0 should be when we have will-change.
  // We could report this as opaque then but when the will-change value starts
  // animating the element would become non opaque and could cause repaints.
  return nsRegion();
}

void nsDisplayOpacity::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  if (GetOpacity() == 0.0f) {
    return;
  }

  int32_t apd = mFrame->PresContext()->AppUnitsPerDevPixel();

  if (GetOpacity() == 1.0f) {
    GetChildren()->Paint(aBuilder, aCtx, apd);
    return;
  }

  bool unusedSnap = false;
  auto deviceSpaceBounds = IntRect::FromUnknownRect(
      RoundedOut(ToRect(aCtx->UserToDevice(nsLayoutUtils::RectToGfxRect(
          GetBounds(aBuilder, &unusedSnap), apd)))));

  aCtx->GetDrawTarget()->PushLayer(false, GetOpacity(), nullptr, gfx::Matrix(),
                                   deviceSpaceBounds);
  GetChildren()->Paint(aBuilder, aCtx, apd);
  aCtx->GetDrawTarget()->PopLayer();
}

/* static */
bool nsDisplayOpacity::NeedsActiveLayer(nsIFrame* aFrame) {
  return EffectCompositor::HasAnimationsForCompositor(
             aFrame, DisplayItemType::TYPE_OPACITY) ||
         ActiveLayerTracker::IsOpacityAnimated(aFrame);
}

bool nsDisplayOpacity::CanApplyOpacity(WebRenderLayerManager* aManager,
                                       nsDisplayListBuilder* aBuilder) const {
  return !EffectCompositor::HasAnimationsForCompositor(
      mFrame, DisplayItemType::TYPE_OPACITY);
}

// Only try folding our opacity down if we have at most |kOpacityMaxChildCount|
// children that don't overlap and can all apply the opacity to themselves.
static const size_t kOpacityMaxChildCount = 3;

// |kOpacityMaxListSize| defines an early exit condition for opacity items that
// are likely have more child items than |kOpacityMaxChildCount|.
static const size_t kOpacityMaxListSize = kOpacityMaxChildCount * 2;

/**
 * Recursively iterates through |aList| and collects at most
 * |kOpacityMaxChildCount| display item pointers to items that return true for
 * CanApplyOpacity(). The item pointers are added to |aArray|.
 *
 * LayerEventRegions and WrapList items are ignored.
 *
 * We need to do this recursively, because the child display items might contain
 * nested nsDisplayWrapLists.
 *
 * Returns false if there are more than |kOpacityMaxChildCount| items, or if an
 * item that returns false for CanApplyOpacity() is encountered.
 * Otherwise returns true.
 */
static bool CollectItemsWithOpacity(WebRenderLayerManager* aManager,
                                    nsDisplayListBuilder* aBuilder,
                                    nsDisplayList* aList,
                                    nsTArray<nsPaintedDisplayItem*>& aArray) {
  if (aList->Length() > kOpacityMaxListSize) {
    // Exit early, since |aList| will likely contain more than
    // |kOpacityMaxChildCount| items.
    return false;
  }

  for (nsDisplayItem* i : *aList) {
    const DisplayItemType type = i->GetType();

    if (type == DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO) {
      continue;
    }

    // Descend only into wraplists.
    if (type == DisplayItemType::TYPE_WRAP_LIST ||
        type == DisplayItemType::TYPE_CONTAINER) {
      // The current display item has children, process them first.
      if (!CollectItemsWithOpacity(aManager, aBuilder, i->GetChildren(),
                                   aArray)) {
        return false;
      }

      continue;
    }

    if (aArray.Length() == kOpacityMaxChildCount) {
      return false;
    }

    auto* item = i->AsPaintedDisplayItem();
    if (!item || !item->CanApplyOpacity(aManager, aBuilder)) {
      return false;
    }

    aArray.AppendElement(item);
  }

  return true;
}

bool nsDisplayOpacity::CanApplyToChildren(WebRenderLayerManager* aManager,
                                          nsDisplayListBuilder* aBuilder) {
  if (mChildOpacityState == ChildOpacityState::Deferred) {
    return false;
  }

  // Iterate through the child display list and copy at most
  // |kOpacityMaxChildCount| child display item pointers to a temporary list.
  AutoTArray<nsPaintedDisplayItem*, kOpacityMaxChildCount> items;
  if (!CollectItemsWithOpacity(aManager, aBuilder, &mList, items)) {
    mChildOpacityState = ChildOpacityState::Deferred;
    return false;
  }

  struct {
    nsPaintedDisplayItem* item{};
    nsRect bounds;
  } children[kOpacityMaxChildCount];

  bool snap;
  size_t childCount = 0;
  for (nsPaintedDisplayItem* item : items) {
    children[childCount].item = item;
    children[childCount].bounds = item->GetBounds(aBuilder, &snap);
    childCount++;
  }

  for (size_t i = 0; i < childCount; i++) {
    for (size_t j = i + 1; j < childCount; j++) {
      if (children[i].bounds.Intersects(children[j].bounds)) {
        mChildOpacityState = ChildOpacityState::Deferred;
        return false;
      }
    }
  }

  mChildOpacityState = ChildOpacityState::Applied;
  return true;
}

/**
 * Returns true if this nsDisplayOpacity contains only a filter or a mask item
 * that has the same frame as the opacity item, and that supports painting with
 * opacity. In this case the opacity item can be optimized away.
 */
bool nsDisplayOpacity::ApplyToMask() {
  if (mList.Length() != 1) {
    return false;
  }

  nsDisplayItem* item = mList.GetBottom();
  if (item->Frame() != mFrame) {
    // The effect item needs to have the same frame as the opacity item.
    return false;
  }

  const DisplayItemType type = item->GetType();
  if (type == DisplayItemType::TYPE_MASK) {
    return true;
  }

  return false;
}

bool nsDisplayOpacity::CanApplyOpacityToChildren(
    WebRenderLayerManager* aManager, nsDisplayListBuilder* aBuilder,
    float aInheritedOpacity) {
  if (mFrame->GetPrevContinuation() || mFrame->GetNextContinuation() ||
      mFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    // If we've been split, then we might need to merge, so
    // don't flatten us away.
    return false;
  }

  if (mNeedsActiveLayer || mOpacity == 0.0) {
    // If our opacity is zero then we'll discard all descendant display items
    // except for layer event regions, so there's no point in doing this
    // optimization (and if we do do it, then invalidations of those descendants
    // might trigger repainting).
    return false;
  }

  if (mList.IsEmpty()) {
    return false;
  }

  // We can only flatten opacity items into a mask if we haven't
  // already flattened an earlier ancestor, since the SVG code pulls the opacity
  // from style directly, and won't know about the outer opacity value.
  if (aInheritedOpacity == 1.0f && ApplyToMask()) {
    MOZ_ASSERT(SVGIntegrationUtils::UsingEffectsForFrame(mFrame));
    mChildOpacityState = ChildOpacityState::Applied;
    return true;
  }

  // Return true if we successfully applied opacity to child items.
  return CanApplyToChildren(aManager, aBuilder);
}

void nsDisplayOpacity::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const auto* geometry =
      static_cast<const nsDisplayOpacityGeometry*>(aGeometry);

  bool snap;
  if (mOpacity != geometry->mOpacity) {
    aInvalidRegion->Or(GetBounds(aBuilder, &snap), geometry->mBounds);
  }
}

void nsDisplayOpacity::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (opacity " << mOpacity << ", mChildOpacityState: ";
  switch (mChildOpacityState) {
    case ChildOpacityState::Unknown:
      aStream << "Unknown";
      break;
    case ChildOpacityState::Applied:
      aStream << "Applied";
      break;
    case ChildOpacityState::Deferred:
      aStream << "Deferred";
      break;
    default:
      break;
  }

  aStream << ")";
}

WebRenderCommandsResult nsDisplayOpacity::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  MOZ_ASSERT(mChildOpacityState != ChildOpacityState::Applied);
  float oldOpacity = aBuilder.GetInheritedOpacity();
  const DisplayItemClipChain* oldClipChain = aBuilder.GetInheritedClipChain();
  aBuilder.SetInheritedOpacity(1.0f);
  aBuilder.SetInheritedClipChain(nullptr);
  float opacity = mOpacity * oldOpacity;
  float* opacityForSC = &opacity;

  uint64_t animationsId =
      AddAnimationsForWebRender(this, aManager, aDisplayListBuilder);
  wr::WrAnimationProperty prop{
      wr::WrAnimationType::Opacity,
      animationsId,
  };

  wr::StackingContextParams params;
  params.animation = animationsId ? &prop : nullptr;
  params.opacity = opacityForSC;
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
  if (mWrapsBackdropFilter) {
    params.flags |= wr::StackingContextFlags::WRAPS_BACKDROP_FILTER;
  }
  if (mForceIsolation) {
    params.flags |= wr::StackingContextFlags::FORCED_ISOLATION;
  }
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params);

  aManager->CommandBuilder().CreateWebRenderCommandsFromDisplayList(
      &mList, this, aDisplayListBuilder, sc, aBuilder, aResources);
  aBuilder.SetInheritedOpacity(oldOpacity);
  aBuilder.SetInheritedClipChain(oldClipChain);
  return Ok();
}

nsDisplayBlendMode::nsDisplayBlendMode(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    StyleBlend aBlendMode, const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, const bool aIsForBackground)
    : nsDisplayWrapList(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, true),
      mBlendMode(aBlendMode),
      mIsForBackground(aIsForBackground) {
  MOZ_COUNT_CTOR(nsDisplayBlendMode);
}

nsRegion nsDisplayBlendMode::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                             bool* aSnap) const {
  *aSnap = false;
  // We are never considered opaque
  return nsRegion();
}

WebRenderCommandsResult nsDisplayBlendMode::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  wr::StackingContextParams params;
  params.mix_blend_mode =
      wr::ToMixBlendMode(nsCSSRendering::GetGFXBlendMode(mBlendMode));
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params);

  return nsDisplayWrapList::CreateWebRenderCommands(
      aBuilder, aResources, sc, aManager, aDisplayListBuilder);
}

void nsDisplayBlendMode::Paint(nsDisplayListBuilder* aBuilder,
                               gfxContext* aCtx) {
  // This should be switched to use PushLayerWithBlend, once it's
  // been implemented for all DrawTarget backends.
  DrawTarget* dt = aCtx->GetDrawTarget();
  int32_t appUnitsPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  Rect rect = NSRectToRect(GetPaintRect(aBuilder, aCtx), appUnitsPerDevPixel);
  rect.RoundOut();

  // Create a temporary DrawTarget that is clipped to the area that
  // we're going to draw to. This will include the same transform as
  // is currently on |dt|.
  RefPtr<DrawTarget> temp =
      dt->CreateClippedDrawTarget(rect, SurfaceFormat::B8G8R8A8);
  if (!temp) {
    return;
  }

  gfxContext ctx(temp, /* aPreserveTransform */ true);

  GetChildren()->Paint(aBuilder, &ctx,
                       mFrame->PresContext()->AppUnitsPerDevPixel());

  // Draw the temporary DT to the real destination, applying the blend mode, but
  // no transform.
  temp->Flush();
  RefPtr<SourceSurface> surface = temp->Snapshot();
  gfxContextMatrixAutoSaveRestore saveMatrix(aCtx);
  dt->SetTransform(Matrix());
  dt->DrawSurface(
      surface, Rect(surface->GetRect()), Rect(surface->GetRect()),
      DrawSurfaceOptions(),
      DrawOptions(1.0f, nsCSSRendering::GetGFXBlendMode(mBlendMode)));
}

gfx::CompositionOp nsDisplayBlendMode::BlendMode() {
  return nsCSSRendering::GetGFXBlendMode(mBlendMode);
}

bool nsDisplayBlendMode::CanMerge(const nsDisplayItem* aItem) const {
  // Items for the same content element should be merged into a single
  // compositing group.
  if (!HasDifferentFrame(aItem) || !HasSameTypeAndClip(aItem) ||
      !HasSameContent(aItem)) {
    return false;
  }

  const auto* item = static_cast<const nsDisplayBlendMode*>(aItem);
  if (mIsForBackground || item->mIsForBackground) {
    // Don't merge background-blend-mode items
    return false;
  }

  return true;
}

/* static */
nsDisplayBlendContainer* nsDisplayBlendContainer::CreateForMixBlendMode(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType) {
  return MakeDisplayItemWithIndex<nsDisplayBlendContainer>(
      aBuilder, aFrame, uint16_t(BlendContainerType::MixBlendMode), aList,
      aActiveScrolledRoot, aContainerASRType, BlendContainerType::MixBlendMode);
}

/* static */
nsDisplayBlendContainer* nsDisplayBlendContainer::CreateForBackgroundBlendMode(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsIFrame* aSecondaryFrame,
    nsDisplayList* aList, const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType) {
  if (aSecondaryFrame) {
    auto type = GetTableTypeFromFrame(aFrame);
    auto index = static_cast<uint16_t>(type);

    return MakeDisplayItemWithIndex<nsDisplayTableBlendContainer>(
        aBuilder, aSecondaryFrame, index, aList, aActiveScrolledRoot,
        aContainerASRType, BlendContainerType::BackgroundBlendMode, aFrame);
  }

  return MakeDisplayItemWithIndex<nsDisplayBlendContainer>(
      aBuilder, aFrame, uint16_t(BlendContainerType::BackgroundBlendMode),
      aList, aActiveScrolledRoot, aContainerASRType,
      BlendContainerType::BackgroundBlendMode);
}

/* static */
nsDisplayBlendContainer* nsDisplayBlendContainer::CreateForIsolation(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, bool aNeedsIsolation) {
  auto type = aNeedsIsolation ? BlendContainerType::NeedsIsolationNeedsContainer
                              : BlendContainerType::NeedsIsolationNothing;
  return MakeDisplayItemWithIndex<nsDisplayBlendContainer>(
      aBuilder, aFrame, uint16_t(BlendContainerType::NeedsIsolationNothing),
      aList, aActiveScrolledRoot, aContainerASRType, type);
}

nsDisplayBlendContainer::nsDisplayBlendContainer(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, BlendContainerType aBlendContainerType)
    : nsDisplayWrapList(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, true),
      mBlendContainerType(aBlendContainerType) {
  MOZ_COUNT_CTOR(nsDisplayBlendContainer);
}

void nsDisplayBlendContainer::Paint(nsDisplayListBuilder* aBuilder,
                                    gfxContext* aCtx) {
  aCtx->GetDrawTarget()->PushLayer(false, 1.0, nullptr, gfx::Matrix());
  GetChildren()->Paint(aBuilder, aCtx,
                       mFrame->PresContext()->AppUnitsPerDevPixel());
  aCtx->GetDrawTarget()->PopLayer();
}

WebRenderCommandsResult nsDisplayBlendContainer::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  Maybe<StackingContextHelper> layer;
  const StackingContextHelper* sc = &aSc;
  if (CreatesStackingContextHelper()) {
    wr::StackingContextParams params;
    params.flags |= wr::StackingContextFlags::IS_BLEND_CONTAINER;
    params.clip =
        wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
    layer.emplace(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder, params);
    sc = layer.ptr();
  }

  return nsDisplayWrapList::CreateWebRenderCommandsNewClipListOption(
      aBuilder, aResources, *sc, aManager, aDisplayListBuilder, layer.isSome());
}

void nsDisplayTableBlendContainer::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mAncestorFrame);
  nsDisplayBlendContainer::Destroy(aBuilder);
}

void nsDisplayTableBlendMode::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mAncestorFrame);
  nsDisplayBlendMode::Destroy(aBuilder);
}

nsDisplayOwnLayer::nsDisplayOwnLayer(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, nsDisplayOwnLayerFlags aFlags,
    const ScrollbarData& aScrollbarData, bool aForceActive,
    bool aClearClipChain)
    : nsDisplayWrapList(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, aClearClipChain),
      mFlags(aFlags),
      mScrollbarData(aScrollbarData),
      mForceActive(aForceActive),
      mWrAnimationId(0) {
  MOZ_COUNT_CTOR(nsDisplayOwnLayer);
}

bool nsDisplayOwnLayer::IsScrollThumbLayer() const {
  return mScrollbarData.mScrollbarLayerType == ScrollbarLayerType::Thumb;
}

bool nsDisplayOwnLayer::IsScrollbarContainer() const {
  return mScrollbarData.mScrollbarLayerType == ScrollbarLayerType::Container;
}

bool nsDisplayOwnLayer::IsRootScrollbarContainer() const {
  return IsScrollbarContainer() && IsScrollbarLayerForRoot();
}

bool nsDisplayOwnLayer::IsScrollbarLayerForRoot() const {
  return mFrame->PresContext()->IsRootContentDocumentCrossProcess() &&
         mScrollbarData.mTargetViewId ==
             nsLayoutUtils::ScrollIdForRootScrollFrame(mFrame->PresContext());
}

bool nsDisplayOwnLayer::IsZoomingLayer() const {
  return GetType() == DisplayItemType::TYPE_ASYNC_ZOOM;
}

bool nsDisplayOwnLayer::IsFixedPositionLayer() const {
  return GetType() == DisplayItemType::TYPE_FIXED_POSITION ||
         GetType() == DisplayItemType::TYPE_TABLE_FIXED_POSITION;
}

bool nsDisplayOwnLayer::IsStickyPositionLayer() const {
  return GetType() == DisplayItemType::TYPE_STICKY_POSITION;
}

/* static */
bool nsDisplayOwnLayer::HasDynamicToolbar(nsIFrame* aFrame) {
  if (!aFrame->PresContext()->IsRootContentDocumentCrossProcess()) {
    return false;
  }
  return aFrame->PresContext()->HasDynamicToolbar() ||
         // For tests on Android, this pref is set to simulate the dynamic
         // toolbar
         StaticPrefs::apz_fixed_margin_override_enabled();
}

bool nsDisplayOwnLayer::HasDynamicToolbar() const {
  return HasDynamicToolbar(mFrame);
}

WebRenderCommandsResult nsDisplayOwnLayer::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder, bool aForceIsolation) {
  uint64_t animationId = 0;
  const bool needsProp = aManager->LayerManager()->AsyncPanZoomEnabled() &&
                         (IsScrollThumbLayer() || IsZoomingLayer() ||
                          ShouldGetFixedAnimationId() ||
                          (IsRootScrollbarContainer() && HasDynamicToolbar()));

  if (needsProp) {
    // APZ is enabled and this is a scroll thumb or zooming layer, so we need
    // to create and set an animation id. That way APZ can adjust the position/
    // zoom of this content asynchronously as needed.
    RefPtr<WebRenderAPZAnimationData> animationData =
        aManager->CommandBuilder()
            .CreateOrRecycleWebRenderUserData<WebRenderAPZAnimationData>(this);
    mWrAnimationId = animationData->GetAnimationId();
    animationId = mWrAnimationId;
  }

  wr::StackingContextParams params;
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
  const bool rootScrollbarContainer = IsRootScrollbarContainer();
  if (rootScrollbarContainer) {
    params.prim_flags |= wr::PrimitiveFlags::IS_SCROLLBAR_CONTAINER;
  }
  if (aForceIsolation) {
    params.flags |= wr::StackingContextFlags::FORCED_ISOLATION;
  }
  const bool shouldSnap = IsZoomingLayer() || ShouldGetFixedAnimationId() ||
                          (rootScrollbarContainer && HasDynamicToolbar());

  Maybe<wr::WrSpatialId> referenceFrame;
  if (animationId || shouldSnap) {
    wr::ReferenceFrameParams rfParams;
    rfParams.animation_id = animationId;
    rfParams.is_2d_scale_translation = shouldSnap;
    rfParams.should_snap = shouldSnap;
    referenceFrame = Some(aManager->CommandBuilder().DefineReferenceFrame(
        aBuilder, mFrame,
        WebRenderCommandBuilder::FrameReferenceFrame::OwnLayer,
        LayoutDevicePoint(), rfParams));
  }

  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params, referenceFrame);

  return nsDisplayWrapList::CreateWebRenderCommands(
      aBuilder, aResources, sc, aManager, aDisplayListBuilder);
}

bool nsDisplayOwnLayer::UpdateScrollData(WebRenderScrollData* aData,
                                         WebRenderLayerScrollData* aLayerData) {
  bool isRelevantToApz = (IsScrollThumbLayer() || IsScrollbarContainer() ||
                          IsZoomingLayer() || ShouldGetFixedAnimationId());

  if (!isRelevantToApz) {
    return false;
  }

  if (!aLayerData) {
    return true;
  }

  if (IsZoomingLayer()) {
    aLayerData->SetZoomAnimationId(mWrAnimationId);
    return true;
  }

  if (IsFixedPositionLayer() && ShouldGetFixedAnimationId()) {
    aLayerData->SetFixedPositionAnimationId(mWrAnimationId);
    return true;
  }

  MOZ_ASSERT(IsScrollbarContainer() || IsScrollThumbLayer());

  aLayerData->SetScrollbarData(mScrollbarData);

  if (IsRootScrollbarContainer() && HasDynamicToolbar()) {
    aLayerData->SetScrollbarAnimationId(mWrAnimationId);
    return true;
  }

  if (IsScrollThumbLayer()) {
    aLayerData->SetScrollbarAnimationId(mWrAnimationId);
    LayoutDeviceRect bounds = LayoutDeviceIntRect::FromAppUnits(
        mBounds, mFrame->PresContext()->AppUnitsPerDevPixel());
    // Subframe scrollbars are subject to the pinch-zoom scale,
    // but root scrollbars are not because they are outside of the
    // region that is zoomed.
    const float resolution =
        IsScrollbarLayerForRoot()
            ? 1.0f
            : mFrame->PresShell()->GetCumulativeResolution();
    LayerIntRect layerBounds =
        RoundedOut(bounds * LayoutDeviceToLayerScale(resolution));
    aLayerData->SetVisibleRect(layerBounds);
  }
  return true;
}

void nsDisplayOwnLayer::WriteDebugInfo(std::stringstream& aStream) {
  aStream << nsPrintfCString(" (flags 0x%x) (scrolltarget %" PRIu64 ")",
                             (int)mFlags, mScrollbarData.mTargetViewId)
                 .get();
}

WebRenderCommandsResult nsDisplayViewTransitionCapture::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  Maybe<wr::SnapshotInfo> si;
  nsPresContext* pc = mFrame->PresContext();
  nsIFrame* capturedFrame =
      mIsRoot ? pc->FrameConstructor()->GetRootElementStyleFrame()
              : nsLayoutUtils::FirstContinuationOrIBSplitSibling(mFrame);
  auto captureRect =
      ViewTransition::CapturedInkOverflowRectForFrame(capturedFrame, mIsRoot);
  auto* vt = pc->Document()->GetActiveViewTransition();
  auto key = [&]() -> Maybe<wr::SnapshotImageKey> {
    if (NS_WARN_IF(!vt)) {
      return Nothing();
    }
    const auto* key =
        vt->GetImageKeyForCapturedFrame(capturedFrame, aManager, aResources);
    return key ? Some(wr::SnapshotImageKey{*key}) : Nothing();
  }();
  VT_LOG_DEBUG(
      "nsDisplayViewTransitionCapture::CreateWebrenderCommands(%s, key=%s)",
      capturedFrame->ListTag().get(), ToString(key).c_str());
  wr::StackingContextParams params;
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());

  // This is for the case if this frame is transformed. In this case, the wr
  // display list looks like this, with the reference frame each stacking
  // context is in:
  //
  // PushStackingContext(...)  // For the root
  //   ...
  //   nsDisplayTransform      // For the captured element if it is transformed
  //     PushStackingContext   // In a new reference frame if the transform
  //                           // matrix is not identity
  //     VTCapture             // For the captured element
  //       PushStackingContext // In a new reference frame
  //       ...
  //       Other display items // For the descendants of the captured element
  //       ...
  //       PopStackingContext
  //     PopStackingContext
  //   ...
  // PopStackingContext
  //
  // The wrapper, nsDisplayTransform, creates a new coordinate system whose
  // origin is from ToReferenceFrame(mFrame). The VTCapture item is inside this
  // nsDisplayTransform, so it's in this coordinate system. This works well if
  // the transform matrix is not identity. However, if the transform matrix is
  // identity, we don't create a new coordinate system (because we don't push a
  // reference frame in WR), at this moment, the |ref_frame_offset| of the
  // stacking context of this VTCapture becomes the |origin| of the
  // nsDisplayTransform (see nsDisplayTransform::CreateWebRenderCommands() for
  // more details), so this offset may makes the
  // position of this VTCapture wrong. (Note that the ToReferenceFrame(mFrame)
  // of this VTCapture is the frame itself, but ToReferenceFrame(mFrame) of the
  // nsDisplayTransform is its containing frame, e.g. viewport frame.)
  //
  // Therefore, we push a reference frame for this display item if the frame is
  // transformed. This makes this VTCapture creates a new coordinate system, so
  // its |ref_frame_offset| will be (0, 0), just fit the nsDisplayTransform's
  // position.
  //
  // FIXME: We can avoid pushing a reference frame if the transform matrix is
  // not identity, because we generate a new coordinate system already from the
  // nsDisplayTransform (see nsDisplayTransform::CreateWebRenderCommands() for
  // more details).
  Maybe<wr::WrSpatialId> referenceFrame;
  if (mFrame->IsTransformed()) {
    // Use an identity matrix to force to push a reference frame to create a new
    // coordinate system for view transition captured frame.
    wr::ReferenceFrameParams rfParams;
    rfParams.transform_style = params.transform_style;
    referenceFrame = Some(aManager->CommandBuilder().DefineReferenceFrame(
        aBuilder, mFrame,
        WebRenderCommandBuilder::FrameReferenceFrame::ViewTransitionCapture,
        LayoutDevicePoint(), rfParams));
  }

  if (key) {
    vt->UpdateActiveRectForCapturedFrame(capturedFrame, aSc.GetInheritedScale(),
                                         captureRect);
    // The capture rect is relative to the first fragment of the captured
    // element, but we might be a merged item for another fragment.
    const nsPoint offsetToCapturedFrame =
        mIsRoot || capturedFrame == mFrame ? nsPoint()
                                           : capturedFrame->GetOffsetTo(mFrame);

    si.emplace(wr::SnapshotInfo{
        .key = *key,
        .area = wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(
            captureRect + offsetToCapturedFrame + ToReferenceFrame(),
            pc->AppUnitsPerDevPixel())),
        .detached = true,
    });
    params.snapshot = si.ptr();
  }
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params, referenceFrame);
  return nsDisplayWrapList::CreateWebRenderCommands(
      aBuilder, aResources, sc, aManager, aDisplayListBuilder);
}

nsDisplaySubDocument::nsDisplaySubDocument(nsDisplayListBuilder* aBuilder,
                                           nsIFrame* aFrame,
                                           nsSubDocumentFrame* aSubDocFrame,
                                           nsDisplayList* aList,
                                           nsDisplayOwnLayerFlags aFlags)
    : nsDisplayOwnLayer(aBuilder, aFrame, aList,
                        aBuilder->CurrentActiveScrolledRoot(),
                        ContainerASRType::Constant, aFlags),
      mShouldFlatten(false),
      mSubDocFrame(aSubDocFrame) {
  MOZ_COUNT_CTOR(nsDisplaySubDocument);

  if (aBuilder->IsRetainingDisplayList() && mSubDocFrame &&
      mSubDocFrame != mFrame) {
    mSubDocFrame->AddDisplayItem(this);
  }
}

void nsDisplaySubDocument::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mSubDocFrame);
  nsDisplayOwnLayer::Destroy(aBuilder);
}

nsIFrame* nsDisplaySubDocument::FrameForInvalidation() const {
  return mSubDocFrame ? mSubDocFrame : mFrame;
}

void nsDisplaySubDocument::RemoveFrame(nsIFrame* aFrame) {
  if (aFrame == mSubDocFrame) {
    mSubDocFrame = nullptr;
    SetDeletedFrame();
  }
  nsDisplayOwnLayer::RemoveFrame(aFrame);
}

static bool UseDisplayPortForViewport(nsDisplayListBuilder* aBuilder,
                                      nsIFrame* aFrame) {
  return aBuilder->IsPaintingToWindow() &&
         DisplayPortUtils::ViewportHasDisplayPort(aFrame->PresContext());
}

nsRect nsDisplaySubDocument::GetBounds(nsDisplayListBuilder* aBuilder,
                                       bool* aSnap) const {
  bool usingDisplayPort = UseDisplayPortForViewport(aBuilder, mFrame);

  if ((mFlags & nsDisplayOwnLayerFlags::GenerateScrollableLayer) &&
      usingDisplayPort) {
    *aSnap = false;
    return mFrame->GetRect() + aBuilder->ToReferenceFrame(mFrame);
  }

  return nsDisplayOwnLayer::GetBounds(aBuilder, aSnap);
}

nsRegion nsDisplaySubDocument::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                               bool* aSnap) const {
  bool usingDisplayPort = UseDisplayPortForViewport(aBuilder, mFrame);

  if ((mFlags & nsDisplayOwnLayerFlags::GenerateScrollableLayer) &&
      usingDisplayPort) {
    *aSnap = false;
    return nsRegion();
  }

  return nsDisplayOwnLayer::GetOpaqueRegion(aBuilder, aSnap);
}

/* static */
nsDisplayFixedPosition* nsDisplayFixedPosition::CreateForFixedBackground(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsIFrame* aSecondaryFrame,
    nsDisplayBackgroundImage* aImage, const uint16_t aIndex,
    const ActiveScrolledRoot* aScrollTargetASR) {
  nsDisplayList temp;
  temp.AppendToTop(aImage);

  if (aSecondaryFrame) {
    auto tableType = GetTableTypeFromFrame(aFrame);
    const uint16_t index = CalculateTablePerFrameKey(aIndex + 1, tableType);
    return MakeDisplayItemWithIndex<nsDisplayTableFixedPosition>(
        aBuilder, aSecondaryFrame, index, &temp, aFrame, aScrollTargetASR);
  }

  return MakeDisplayItemWithIndex<nsDisplayFixedPosition>(
      aBuilder, aFrame, aIndex + 1, &temp, aScrollTargetASR);
}

nsDisplayFixedPosition::nsDisplayFixedPosition(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType,
    const ActiveScrolledRoot* aScrollTargetASR, bool aForceIsolation)
    : nsDisplayOwnLayer(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType),
      mScrollTargetASR(aScrollTargetASR),
      mIsFixedBackground(false),
      mForceIsolation(aForceIsolation) {
  MOZ_COUNT_CTOR(nsDisplayFixedPosition);
  MOZ_ASSERT_IF(mScrollTargetASR,
                mScrollTargetASR->mKind == ActiveScrolledRoot::ASRKind::Scroll);
}

nsDisplayFixedPosition::nsDisplayFixedPosition(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aScrollTargetASR)
    : nsDisplayOwnLayer(aBuilder, aFrame, aList,
                        aBuilder->CurrentActiveScrolledRoot(),
                        ContainerASRType::Constant),
      // For fixed backgrounds, this is the ASR for the nearest scroll frame.
      mScrollTargetASR(aScrollTargetASR),
      mIsFixedBackground(true),
      mForceIsolation(false) {
  MOZ_COUNT_CTOR(nsDisplayFixedPosition);
  MOZ_ASSERT_IF(mScrollTargetASR,
                mScrollTargetASR->mKind == ActiveScrolledRoot::ASRKind::Scroll);
}

ScrollableLayerGuid::ViewID nsDisplayFixedPosition::GetScrollTargetId() const {
  if (mScrollTargetASR &&
      (mIsFixedBackground || !nsLayoutUtils::IsReallyFixedPos(mFrame))) {
    return mScrollTargetASR->GetViewId();
  }
  return nsLayoutUtils::ScrollIdForRootScrollFrame(mFrame->PresContext());
}

WebRenderCommandsResult nsDisplayFixedPosition::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  SideBits sides = SideBits::eNone;
  if (!mIsFixedBackground) {
    sides = nsLayoutUtils::GetSideBitsForFixedPositionContent(mFrame);
  }

  // We install this RAII scrolltarget tracker so that any
  // nsDisplayCompositorHitTestInfo items inside this fixed-pos item (and that
  // share the same ASR as this item) use the correct scroll target. That way
  // attempts to scroll on those items will scroll the root scroll frame.
  wr::DisplayListBuilder::FixedPosScrollTargetTracker tracker(
      aBuilder, GetActiveScrolledRoot(), GetScrollTargetId(), sides);
  return nsDisplayOwnLayer::CreateWebRenderCommands(
      aBuilder, aResources, aSc, aManager, aDisplayListBuilder);
}

bool nsDisplayFixedPosition::UpdateScrollData(
    WebRenderScrollData* aData, WebRenderLayerScrollData* aLayerData) {
  if (aLayerData) {
    if (!mIsFixedBackground) {
      aLayerData->SetFixedPositionSides(
          nsLayoutUtils::GetSideBitsForFixedPositionContent(mFrame));
    }
    aLayerData->SetFixedPositionScrollContainerId(GetScrollTargetId());
  }
  nsDisplayOwnLayer::UpdateScrollData(aData, aLayerData);
  return true;
}

bool nsDisplayFixedPosition::ShouldGetFixedAnimationId() {
#if defined(MOZ_WIDGET_ANDROID)
  return mFrame->PresContext()->IsRootContentDocumentCrossProcess() &&
         nsLayoutUtils::ScrollIdForRootScrollFrame(mFrame->PresContext()) ==
             GetScrollTargetId();
#else
  return HasDynamicToolbar() &&
         (nsLayoutUtils::ScrollIdForRootScrollFrame(mFrame->PresContext()) ==
          GetScrollTargetId());
#endif
}

void nsDisplayFixedPosition::WriteDebugInfo(std::stringstream& aStream) {
  aStream << nsPrintfCString(
                 " (containerASR %s) (scrolltarget %" PRIu64 ")",
                 ActiveScrolledRoot::ToString(mScrollTargetASR).get(),
                 GetScrollTargetId())
                 .get();
}

TableType GetTableTypeFromFrame(nsIFrame* aFrame) {
  if (aFrame->IsTableFrame()) {
    return TableType::Table;
  }

  if (aFrame->IsTableColFrame()) {
    return TableType::TableCol;
  }

  if (aFrame->IsTableColGroupFrame()) {
    return TableType::TableColGroup;
  }

  if (aFrame->IsTableRowFrame()) {
    return TableType::TableRow;
  }

  if (aFrame->IsTableRowGroupFrame()) {
    return TableType::TableRowGroup;
  }

  if (aFrame->IsTableCellFrame()) {
    return TableType::TableCell;
  }

  MOZ_ASSERT_UNREACHABLE("Invalid frame.");
  return TableType::Table;
}

nsDisplayTableFixedPosition::nsDisplayTableFixedPosition(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    nsIFrame* aAncestorFrame, const ActiveScrolledRoot* aScrollTargetASR)
    : nsDisplayFixedPosition(aBuilder, aFrame, aList, aScrollTargetASR),
      mAncestorFrame(aAncestorFrame) {
  if (aBuilder->IsRetainingDisplayList()) {
    mAncestorFrame->AddDisplayItem(this);
  }
}

void nsDisplayTableFixedPosition::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mAncestorFrame);
  nsDisplayFixedPosition::Destroy(aBuilder);
}

nsDisplayStickyPosition::nsDisplayStickyPosition(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, const ActiveScrolledRoot* aContainerASR)
    : nsDisplayOwnLayer(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, nsDisplayOwnLayerFlags::None,
                        layers::ScrollbarData{},
                        /*aForceActive=*/true, /*aClearClipChain=*/true),
      mContainerASR(aContainerASR),
      mShouldFlatten(false) {
  MOZ_COUNT_CTOR(nsDisplayStickyPosition);
}

StickyScrollContainer* nsDisplayStickyPosition::GetStickyScrollContainer() {
  auto* ssc = StickyScrollContainer::GetOrCreateForFrame(mFrame);
  if (!ssc) {
    return nullptr;
  }
  // If there's no ASR for the scrollframe that this sticky item is attached
  // to, then don't create a WR sticky item for it either. Trying to do so
  // will end in sadness because WR will interpret some coordinates as
  // relative to the nearest enclosing scrollframe, which will correspond
  // to the nearest ancestor ASR on the gecko side. That ASR will not be the
  // same as the scrollframe this sticky item is actually supposed to be
  // attached to, thus the sadness.
  // Not sending WR the sticky item is ok, because the enclosing scrollframe
  // will never be asynchronously scrolled. Instead we will always position
  // the sticky items correctly on the gecko side and WR will never need to
  // adjust their position itself.
  MOZ_ASSERT(ssc->ScrollContainer()->IsMaybeAsynchronouslyScrolled());
  if (!ssc->ScrollContainer()->IsMaybeAsynchronouslyScrolled()) {
    return nullptr;
  }
  return ssc;
}

WebRenderCommandsResult nsDisplayStickyPosition::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  StickyScrollContainer* stickyScrollContainer = GetStickyScrollContainer();

  Maybe<wr::SpaceAndClipChainHelper> saccHelper;

  if (stickyScrollContainer && !stickyScrollContainer->ShouldFlattenAway()) {
    const ActiveScrolledRoot* stickyAsr =
        ActiveScrolledRoot::GetStickyASRFromFrame(mFrame);
    MOZ_ASSERT(stickyAsr);
    auto spatialId = aBuilder.GetSpatialIdForDefinedLayer(stickyAsr);
    MOZ_ASSERT(spatialId.isSome());
    saccHelper.emplace(aBuilder, *spatialId);
  }

  wr::StackingContextParams params;
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params);
  return nsDisplayOwnLayer::CreateWebRenderCommands(
      aBuilder, aResources, sc, aManager, aDisplayListBuilder);
}

void nsDisplayStickyPosition::CalculateLayerScrollRanges(
    StickyScrollContainer* aStickyScrollContainer, float aAppUnitsPerDevPixel,
    float aScaleX, float aScaleY, LayerRectAbsolute& aStickyOuter,
    LayerRectAbsolute& aStickyInner) {
  nsRectAbsolute outer;
  nsRectAbsolute inner;
  aStickyScrollContainer->GetScrollRanges(mFrame, &outer, &inner);
  aStickyOuter.SetBox(
      NSAppUnitsToFloatPixels(outer.X(), aAppUnitsPerDevPixel) * aScaleX,
      NSAppUnitsToFloatPixels(outer.Y(), aAppUnitsPerDevPixel) * aScaleY,
      NSAppUnitsToFloatPixels(outer.XMost(), aAppUnitsPerDevPixel) * aScaleX,
      NSAppUnitsToFloatPixels(outer.YMost(), aAppUnitsPerDevPixel) * aScaleY);
  aStickyInner.SetBox(
      NSAppUnitsToFloatPixels(inner.X(), aAppUnitsPerDevPixel) * aScaleX,
      NSAppUnitsToFloatPixels(inner.Y(), aAppUnitsPerDevPixel) * aScaleY,
      NSAppUnitsToFloatPixels(inner.XMost(), aAppUnitsPerDevPixel) * aScaleX,
      NSAppUnitsToFloatPixels(inner.YMost(), aAppUnitsPerDevPixel) * aScaleY);
}

bool nsDisplayStickyPosition::UpdateScrollData(
    WebRenderScrollData* aData, WebRenderLayerScrollData* aLayerData) {
  bool hasDynamicToolbar = HasDynamicToolbar();
  if (aLayerData && hasDynamicToolbar) {
    StickyScrollContainer* stickyScrollContainer = GetStickyScrollContainer();
    if (stickyScrollContainer) {
      float auPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
      float cumulativeResolution =
          mFrame->PresShell()->GetCumulativeResolution();
      LayerRectAbsolute stickyOuter;
      LayerRectAbsolute stickyInner;
      CalculateLayerScrollRanges(stickyScrollContainer, auPerDevPixel,
                                 cumulativeResolution, cumulativeResolution,
                                 stickyOuter, stickyInner);
      aLayerData->SetStickyScrollRangeOuter(stickyOuter);
      aLayerData->SetStickyScrollRangeInner(stickyInner);

      SideBits sides =
          nsLayoutUtils::GetSideBitsForFixedPositionContent(mFrame);
      aLayerData->SetFixedPositionSides(sides);

      ScrollableLayerGuid::ViewID scrollId = nsLayoutUtils::FindOrCreateIDFor(
          stickyScrollContainer->ScrollContainer()
              ->GetScrolledFrame()
              ->GetContent());
      aLayerData->SetStickyPositionScrollContainerId(scrollId);
    }

    if (ShouldGetStickyAnimationId()) {
      // The animation id created by ClipManager is keyed on the ASR's
      // mFrame, which for sticky ASRs is the first continuation.
      auto displayItemKey = nsDisplayItem::GetPerFrameKey(
          0, 0, DisplayItemType::TYPE_STICKY_POSITION);
      RefPtr<WebRenderAPZAnimationData> animationData =
          aData->GetManager()
              ->CommandBuilder()
              .GetWebRenderUserData<WebRenderAPZAnimationData>(
                  displayItemKey, mFrame->FirstContinuation());
      if (animationData) {
        aLayerData->SetStickyPositionAnimationId(
            animationData->GetAnimationId());
      } else {
        if (!stickyScrollContainer) {
          MOZ_DIAGNOSTIC_CRASH(
              "no animation data because there's no sticky scroll container");
        } else if (stickyScrollContainer->ShouldFlattenAway()) {
          MOZ_DIAGNOSTIC_CRASH(
              "no animation data because the sticky item is being flattened");
        } else {
          MOZ_DIAGNOSTIC_CRASH("no animation data for some other reason");
        }
      }
    }
  }
  // Return true if either there is a dynamic toolbar affecting this sticky
  // item or the OwnLayer base implementation returns true for some other
  // reason.
  bool ret = hasDynamicToolbar;
  ret |= nsDisplayOwnLayer::UpdateScrollData(aData, aLayerData);
  return ret;
}

bool nsDisplayStickyPosition::ShouldGetStickyAnimationId(
    nsIFrame* aStickyFrame) {
  // Also implies being in the cross-process RCD.
  // The animation id is how we translate the layer by the dynamic toolbar
  // offset. It's not needed for regular sticky positioning, that's handled
  // by WebRender without an animation id.
  return nsDisplayOwnLayer::HasDynamicToolbar(aStickyFrame);
}

bool nsDisplayStickyPosition::ShouldGetStickyAnimationId() const {
  return ShouldGetStickyAnimationId(mFrame);
}

nsDisplayScrollInfoLayer::nsDisplayScrollInfoLayer(
    nsDisplayListBuilder* aBuilder, nsIFrame* aScrolledFrame,
    nsIFrame* aScrollFrame, const CompositorHitTestInfo& aHitInfo,
    const nsRect& aHitArea)
    : nsDisplayWrapList(aBuilder, aScrollFrame),
      mScrollFrame(aScrollFrame),
      mScrolledFrame(aScrolledFrame),
      mScrollParentId(aBuilder->GetCurrentScrollParentId()),
      mHitInfo(aHitInfo),
      mHitArea(aHitArea) {
#ifdef NS_BUILD_REFCNT_LOGGING
  MOZ_COUNT_CTOR(nsDisplayScrollInfoLayer);
#endif
}

UniquePtr<ScrollMetadata> nsDisplayScrollInfoLayer::ComputeScrollMetadata(
    nsDisplayListBuilder* aBuilder, WebRenderLayerManager* aLayerManager) {
  ScrollMetadata metadata = nsLayoutUtils::ComputeScrollMetadata(
      mScrolledFrame, mScrollFrame, mScrollFrame->GetContent(), Frame(),
      ToReferenceFrame(), aLayerManager, mScrollParentId,
      mScrollFrame->GetSize(), false);
  metadata.GetMetrics().SetIsScrollInfoLayer(true);
  ScrollContainerFrame* scrollContainerFrame =
      mScrollFrame->GetScrollTargetFrame();
  if (scrollContainerFrame) {
    aBuilder->AddScrollContainerFrameToNotify(scrollContainerFrame);
  }

  return std::make_unique<ScrollMetadata>(metadata);
}

bool nsDisplayScrollInfoLayer::UpdateScrollData(
    WebRenderScrollData* aData, WebRenderLayerScrollData* aLayerData) {
  if (aLayerData) {
    UniquePtr<ScrollMetadata> metadata =
        ComputeScrollMetadata(aData->GetBuilder(), aData->GetManager());
    MOZ_ASSERT(aData);
    MOZ_ASSERT(metadata);
    aLayerData->AppendScrollMetadata(*aData, *metadata);
  }
  return true;
}

WebRenderCommandsResult nsDisplayScrollInfoLayer::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  ScrollableLayerGuid::ViewID scrollId =
      nsLayoutUtils::FindOrCreateIDFor(mScrollFrame->GetContent());

  const LayoutDeviceRect devRect = LayoutDeviceRect::FromAppUnits(
      mHitArea, mScrollFrame->PresContext()->AppUnitsPerDevPixel());

  const wr::LayoutRect rect = wr::ToLayoutRect(devRect);

  aBuilder.PushHitTest(rect, rect, !BackfaceIsHidden(), scrollId, mHitInfo,
                       SideBits::eNone);

  return Ok();
}

void nsDisplayScrollInfoLayer::WriteDebugInfo(std::stringstream& aStream) {
  aStream << " (scrollframe " << mScrollFrame << " scrolledFrame "
          << mScrolledFrame << ")";
}

nsDisplayZoom::nsDisplayZoom(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                             nsSubDocumentFrame* aSubDocFrame,
                             nsDisplayList* aList, int32_t aAPD,
                             int32_t aParentAPD, nsDisplayOwnLayerFlags aFlags)
    : nsDisplaySubDocument(aBuilder, aFrame, aSubDocFrame, aList, aFlags),
      mAPD(aAPD),
      mParentAPD(aParentAPD) {
  MOZ_COUNT_CTOR(nsDisplayZoom);
}

nsRect nsDisplayZoom::GetBounds(nsDisplayListBuilder* aBuilder,
                                bool* aSnap) const {
  nsRect bounds = nsDisplaySubDocument::GetBounds(aBuilder, aSnap);
  *aSnap = false;
  return bounds.ScaleToOtherAppUnitsRoundOut(mAPD, mParentAPD);
}

void nsDisplayZoom::HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                            HitTestState* aState,
                            nsTArray<nsIFrame*>* aOutFrames) {
  nsRect rect;
  // A 1x1 rect indicates we are just hit testing a point, so pass down a 1x1
  // rect as well instead of possibly rounding the width or height to zero.
  if (aRect.width == 1 && aRect.height == 1) {
    rect.MoveTo(aRect.TopLeft().ScaleToOtherAppUnits(mParentAPD, mAPD));
    rect.width = rect.height = 1;
  } else {
    rect = aRect.ScaleToOtherAppUnitsRoundOut(mParentAPD, mAPD);
  }
  mList.HitTest(aBuilder, rect, aState, aOutFrames);
}

nsDisplayAsyncZoom::nsDisplayAsyncZoom(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, FrameMetrics::ViewID aViewID)
    : nsDisplayOwnLayer(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType),
      mViewID(aViewID) {
  MOZ_COUNT_CTOR(nsDisplayAsyncZoom);
}

void nsDisplayAsyncZoom::HitTest(nsDisplayListBuilder* aBuilder,
                                 const nsRect& aRect, HitTestState* aState,
                                 nsTArray<nsIFrame*>* aOutFrames) {
#ifdef DEBUG
  ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(mFrame);
  MOZ_ASSERT(scrollContainerFrame &&
             ViewportUtils::IsZoomedContentRoot(
                 scrollContainerFrame->GetScrolledFrame()));
#endif
  nsRect rect = ViewportUtils::VisualToLayout(aRect, mFrame->PresShell());
  mList.HitTest(aBuilder, rect, aState, aOutFrames);
}

bool nsDisplayAsyncZoom::UpdateScrollData(
    WebRenderScrollData* aData, WebRenderLayerScrollData* aLayerData) {
  bool ret = nsDisplayOwnLayer::UpdateScrollData(aData, aLayerData);
  MOZ_ASSERT(ret);
  if (aLayerData) {
    aLayerData->SetAsyncZoomContainerId(mViewID);
  }
  return ret;
}

///////////////////////////////////////////////////
// nsDisplayTransform Implementation
//

#ifndef DEBUG
static_assert(sizeof(nsDisplayTransform) <= 512,
              "nsDisplayTransform has grown");
#endif

nsDisplayTransform::nsDisplayTransform(nsDisplayListBuilder* aBuilder,
                                       nsIFrame* aFrame, nsDisplayList* aList,
                                       const nsRect& aChildrenBuildingRect)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mTransform(Some(Matrix4x4())),
      mChildrenBuildingRect(aChildrenBuildingRect),
      mPrerenderDecision(PrerenderDecision::No),
      mIsTransformSeparator(true),
      mHasTransformGetter(false),
      mHasAssociatedPerspective(false),
      mContainsASRs(false),
      mWrapsBackdropFilter(false),
      mForceIsolation(false) {
  MOZ_COUNT_CTOR(nsDisplayTransform);
  MOZ_ASSERT(aFrame, "Must have a frame!");
  Init(aBuilder, aList);
}

nsDisplayTransform::nsDisplayTransform(nsDisplayListBuilder* aBuilder,
                                       nsIFrame* aFrame, nsDisplayList* aList,
                                       const nsRect& aChildrenBuildingRect,
                                       PrerenderDecision aPrerenderDecision,
                                       bool aWrapsBackdropFilter,
                                       bool aForceIsolation)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mChildrenBuildingRect(aChildrenBuildingRect),
      mPrerenderDecision(aPrerenderDecision),
      mIsTransformSeparator(false),
      mHasTransformGetter(false),
      mHasAssociatedPerspective(false),
      mContainsASRs(false),
      mWrapsBackdropFilter(aWrapsBackdropFilter),
      mForceIsolation(aForceIsolation) {
  MOZ_COUNT_CTOR(nsDisplayTransform);
  MOZ_ASSERT(aFrame, "Must have a frame!");
  SetReferenceFrameToAncestor(aBuilder);
  Init(aBuilder, aList);
}

nsDisplayTransform::nsDisplayTransform(nsDisplayListBuilder* aBuilder,
                                       nsIFrame* aFrame, nsDisplayList* aList,
                                       const nsRect& aChildrenBuildingRect,
                                       decltype(WithTransformGetter))
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mChildrenBuildingRect(aChildrenBuildingRect),
      mPrerenderDecision(PrerenderDecision::No),
      mIsTransformSeparator(false),
      mHasTransformGetter(true),
      mHasAssociatedPerspective(false),
      mContainsASRs(false),
      mWrapsBackdropFilter(false),
      mForceIsolation(false) {
  MOZ_COUNT_CTOR(nsDisplayTransform);
  MOZ_ASSERT(aFrame, "Must have a frame!");
  MOZ_ASSERT(aFrame->GetTransformGetter());
  Init(aBuilder, aList);
}

void nsDisplayTransform::SetReferenceFrameToAncestor(
    nsDisplayListBuilder* aBuilder) {
  if (mFrame == aBuilder->RootReferenceFrame()) {
    return;
  }
  // We manually recompute mToReferenceFrame without going through the
  // builder, since this won't apply the 'additional offset'. Our
  // children will already be painting with that applied, and we don't
  // want to include it a second time in our transform. We don't recompute
  // our visible/building rects, since those should still include the additional
  // offset.
  // TODO: Are there are things computed using our ToReferenceFrame that should
  // have the additional offset applied? Should we instead just manually remove
  // the offset from our transform instead of this more general value?
  // Can we instead apply the additional offset to us and not our children, like
  // we do for all other offsets (and how reference frames are supposed to
  // work)?
  // The reference frame on the builder has already been set to the ancestor
  // reference frame by BuildDisplayListForStackingContext using
  // SetReferenceFrameAndCurrentOffset, so we just compute the offset.
  MOZ_ASSERT(aBuilder->FindReferenceFrameFor(
                 nsLayoutUtils::GetCrossDocParentFrameInProcess(mFrame)) ==
             aBuilder->GetCurrentReferenceFrame());
  mToReferenceFrame =
      mFrame->GetOffsetToCrossDoc(aBuilder->GetCurrentReferenceFrame());
}

void nsDisplayTransform::Init(nsDisplayListBuilder* aBuilder,
                              nsDisplayList* aChildren) {
  mChildren.AppendToTop(aChildren);
  UpdateBounds(aBuilder);
}

bool nsDisplayTransform::ShouldFlattenAway(nsDisplayListBuilder* aBuilder) {
  return false;
}

/* Returns the delta specified by the transform-origin property.
 * This is a positive delta, meaning that it indicates the direction to move
 * to get from (0, 0) of the frame to the transform origin.  This function is
 * called off the main thread.
 */
/* static */
Point3D nsDisplayTransform::GetDeltaToTransformOrigin(
    const nsIFrame* aFrame, TransformReferenceBox& aRefBox,
    float aAppUnitsPerPixel) {
  MOZ_ASSERT(aFrame, "Can't get delta for a null frame!");
  MOZ_ASSERT(aFrame->IsTransformed() || aFrame->BackfaceIsHidden() ||
                 aFrame->Combines3DTransformWithAncestors(),
             "Shouldn't get a delta for an untransformed frame!");

  if (!aFrame->IsTransformed()) {
    return Point3D();
  }

  /* For both of the coordinates, if the value of transform is a
   * percentage, it's relative to the size of the frame.  Otherwise, if it's
   * a distance, it's already computed for us!
   */
  const nsStyleDisplay* display = aFrame->StyleDisplay();

  const StyleTransformOrigin& transformOrigin = display->mTransformOrigin;
  CSSPoint origin = nsStyleTransformMatrix::Convert2DPosition(
      transformOrigin.horizontal, transformOrigin.vertical, aRefBox);

  // Note:
  // 1. SVG frames have a reference box that can be (and typically is) offset
  //    from the TopLeft() of the frame. We need to account for that here.
  // 2. If we are using transform-box:content-box in CSS layout, we have the
  //    offset from TopLeft() of the frame as well.
  origin.x += CSSPixel::FromAppUnits(aRefBox.X());
  origin.y += CSSPixel::FromAppUnits(aRefBox.Y());

  float scale = AppUnitsPerCSSPixel() / float(aAppUnitsPerPixel);
  float z = transformOrigin.depth._0;
  return Point3D(origin.x * scale, origin.y * scale, z * scale);
}

/* static */
bool nsDisplayTransform::ComputePerspectiveMatrix(const nsIFrame* aFrame,
                                                  float aAppUnitsPerPixel,
                                                  Matrix4x4& aOutMatrix) {
  MOZ_ASSERT(aFrame, "Can't get delta for a null frame!");
  MOZ_ASSERT(aFrame->IsTransformed() || aFrame->BackfaceIsHidden() ||
                 aFrame->Combines3DTransformWithAncestors(),
             "Shouldn't get a delta for an untransformed frame!");
  MOZ_ASSERT(aOutMatrix.IsIdentity(), "Must have a blank output matrix");

  if (!aFrame->IsTransformed()) {
    return false;
  }

  // TODO: Is it possible that the perspectiveFrame's bounds haven't been set
  // correctly yet (similar to the aBoundsOverride case for
  // GetResultingTransformMatrix)?
  nsIFrame* perspectiveFrame =
      aFrame->GetClosestFlattenedTreeAncestorPrimaryFrame();
  if (!perspectiveFrame) {
    return false;
  }

  /* Grab the values for perspective and perspective-origin (if present) */
  const nsStyleDisplay* perspectiveDisplay = perspectiveFrame->StyleDisplay();
  if (perspectiveDisplay->mChildPerspective.IsNone()) {
    return false;
  }

  MOZ_ASSERT(perspectiveDisplay->mChildPerspective.IsLength());
  float perspective =
      perspectiveDisplay->mChildPerspective.AsLength().ToCSSPixels();
  perspective = std::max(1.0f, perspective);
  if (perspective < std::numeric_limits<Float>::epsilon()) {
    return true;
  }

  TransformReferenceBox refBox(perspectiveFrame);

  Point perspectiveOrigin = nsStyleTransformMatrix::Convert2DPosition(
      perspectiveDisplay->mPerspectiveOrigin.horizontal,
      perspectiveDisplay->mPerspectiveOrigin.vertical, refBox,
      aAppUnitsPerPixel);

  /* GetOffsetTo computes the offset required to move from 0,0 in
   * perspectiveFrame to 0,0 in aFrame. Although we actually want the inverse of
   * this, it's faster to compute this way.
   */
  nsPoint frameToPerspectiveOffset = -aFrame->GetOffsetTo(perspectiveFrame);
  Point frameToPerspectiveGfxOffset(
      NSAppUnitsToFloatPixels(frameToPerspectiveOffset.x, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(frameToPerspectiveOffset.y, aAppUnitsPerPixel));

  /* Move the perspective origin to be relative to aFrame, instead of relative
   * to the containing block which is how it was specified in the style system.
   */
  perspectiveOrigin += frameToPerspectiveGfxOffset;

  aOutMatrix._34 =
      -1.0 / NSAppUnitsToFloatPixels(CSSPixel::ToAppUnits(perspective),
                                     aAppUnitsPerPixel);

  aOutMatrix.ChangeBasis(Point3D(perspectiveOrigin.x, perspectiveOrigin.y, 0));
  return true;
}

nsDisplayTransform::FrameTransformProperties::FrameTransformProperties(
    const nsIFrame* aFrame, TransformReferenceBox& aRefBox,
    float aAppUnitsPerPixel)
    : mFrame(aFrame),
      mTranslate(aFrame->StyleDisplay()->mTranslate),
      mRotate(aFrame->StyleDisplay()->mRotate),
      mScale(aFrame->StyleDisplay()->mScale),
      mTransform(aFrame->StyleDisplay()->mTransform),
      mMotion(aFrame->StyleDisplay()->mOffsetPath.IsNone()
                  ? Nothing()
                  : MotionPathUtils::ResolveMotionPath(aFrame, aRefBox)),
      mToTransformOrigin(
          GetDeltaToTransformOrigin(aFrame, aRefBox, aAppUnitsPerPixel)) {}

/* Wraps up the transform matrix in a change-of-basis matrix pair that
 * translates from local coordinate space to transform coordinate space, then
 * hands it back.
 */
Matrix4x4 nsDisplayTransform::GetResultingTransformMatrix(
    const FrameTransformProperties& aProperties, TransformReferenceBox& aRefBox,
    float aAppUnitsPerPixel) {
  return GetResultingTransformMatrixInternal(aProperties, aRefBox, nsPoint(),
                                             aAppUnitsPerPixel, 0);
}

Matrix4x4 nsDisplayTransform::GetResultingTransformMatrix(
    const nsIFrame* aFrame, const nsPoint& aOrigin, float aAppUnitsPerPixel,
    uint32_t aFlags) {
  TransformReferenceBox refBox(aFrame);
  FrameTransformProperties props(aFrame, refBox, aAppUnitsPerPixel);
  return GetResultingTransformMatrixInternal(props, refBox, aOrigin,
                                             aAppUnitsPerPixel, aFlags);
}

Matrix4x4 nsDisplayTransform::GetResultingTransformMatrixInternal(
    const FrameTransformProperties& aProperties, TransformReferenceBox& aRefBox,
    const nsPoint& aOrigin, float aAppUnitsPerPixel, uint32_t aFlags) {
  const nsIFrame* frame = aProperties.mFrame;
  NS_ASSERTION(frame || !(aFlags & INCLUDE_PERSPECTIVE),
               "Must have a frame to compute perspective!");

  // IncrementScaleRestyleCountIfNeeded in ActiveLayerTracker.cpp is a
  // simplified copy of this function.

  // Get the underlying transform matrix:

  /* Get the matrix, then change its basis to factor in the origin. */
  Matrix4x4 result;

  // See the comment for SVGContainerFrame::HasChildrenOnlyTransform for
  // an explanation of what children-only transforms are.
  Matrix parentsChildrenOnlyTransform;
  const bool parentHasChildrenOnlyTransform =
      frame && frame->HasAnyStateBits(NS_FRAME_MAY_BE_TRANSFORMED) &&
      frame->GetParentSVGTransforms(&parentsChildrenOnlyTransform) &&
      !parentsChildrenOnlyTransform.IsIdentity();
  bool shouldRound = nsLayoutUtils::ShouldSnapToGrid(frame);

  /* Transformed frames always have a transform, or are preserving 3d (and might
   * still have perspective!) */
  if (aProperties.HasTransform()) {
    // Calling from the compositor side, where we don't have access to frames
    // but transforms already have appropriate zoom applied.
    result = nsStyleTransformMatrix::ReadTransforms(
        aProperties.mTranslate, aProperties.mRotate, aProperties.mScale,
        aProperties.mMotion.ptrOr(nullptr), aProperties.mTransform, aRefBox,
        aAppUnitsPerPixel, nsStyleTransformMatrix::Zoomed::Yes);
  }

  // Apply any translation due to 'transform-origin' and/or 'transform-box':
  if (aProperties.mToTransformOrigin != gfx::Point3D()) {
    result.ChangeBasis(aProperties.mToTransformOrigin);
  }

  if (parentHasChildrenOnlyTransform) {
    float pixelsPerCSSPx = AppUnitsPerCSSPixel() / aAppUnitsPerPixel;
    parentsChildrenOnlyTransform._31 *= pixelsPerCSSPx;
    parentsChildrenOnlyTransform._32 *= pixelsPerCSSPx;
    auto parentsChildrenOnlyTransform3D =
        Matrix4x4::From2D(parentsChildrenOnlyTransform);
    // <svg> outer anon child frame doesn't need the extra basis change because
    // it is the root of the svg rendering (and thus its offset with respect to
    // its parent, like border / padding shouldn't be accounted for).
    if (frame->GetPosition() != nsPoint() &&
        !frame->IsSVGOuterSVGAnonChildFrame()) {
      const Point3D frameOffset(
          NSAppUnitsToFloatPixels(-frame->GetPosition().x, aAppUnitsPerPixel),
          NSAppUnitsToFloatPixels(-frame->GetPosition().y, aAppUnitsPerPixel),
          0);
      parentsChildrenOnlyTransform3D.ChangeBasis(frameOffset);
    }

    result *= parentsChildrenOnlyTransform3D;
  }

  Matrix4x4 perspectiveMatrix;
  bool hasPerspective = aFlags & INCLUDE_PERSPECTIVE;
  if (hasPerspective) {
    if (ComputePerspectiveMatrix(frame, aAppUnitsPerPixel, perspectiveMatrix)) {
      result *= perspectiveMatrix;
    }
  }

  if ((aFlags & INCLUDE_PRESERVE3D_ANCESTORS) && frame &&
      frame->Combines3DTransformWithAncestors()) {
    // Include the transform set on our parent
    nsIFrame* parentFrame =
        frame->GetClosestFlattenedTreeAncestorPrimaryFrame();
    NS_ASSERTION(parentFrame && parentFrame->IsTransformed() &&
                     parentFrame->Extend3DContext(),
                 "Preserve3D mismatch!");
    TransformReferenceBox refBox(parentFrame);
    FrameTransformProperties props(parentFrame, refBox, aAppUnitsPerPixel);

    // Whenever we are including preserve3d we want to also include perspective
    // (if it exists).
    uint32_t flags = (INCLUDE_PRESERVE3D_ANCESTORS | INCLUDE_PERSPECTIVE);

    // If this frame isn't transformed (but we exist for backface-visibility),
    // then we're not a reference frame so no offset to origin will be added.
    // Otherwise we need to manually translate into our parent's coordinate
    // space.
    if (frame->IsTransformed()) {
      nsLayoutUtils::PostTranslate(result, frame->GetPosition(),
                                   aAppUnitsPerPixel, shouldRound);
    }
    Matrix4x4 parent = GetResultingTransformMatrixInternal(
        props, refBox, nsPoint(0, 0), aAppUnitsPerPixel, flags);
    result = result * parent;
  }

  MOZ_ASSERT((aOrigin == nsPoint()) || (aFlags & OFFSET_BY_ORIGIN));
  if ((aFlags & OFFSET_BY_ORIGIN) && (aOrigin != nsPoint())) {
    nsLayoutUtils::PostTranslate(result, aOrigin, aAppUnitsPerPixel,
                                 shouldRound);
  }

  return result;
}

bool nsDisplayOpacity::CanUseAsyncAnimations() {
  if (ActiveLayerTracker::IsOpacityAnimated(mFrame)) {
    return true;
  }

  EffectCompositor::SetPerformanceWarning(
      mFrame, nsCSSPropertyIDSet::OpacityProperties(),
      AnimationPerformanceWarning(
          AnimationPerformanceWarning::Type::OpacityFrameInactive));
  return false;
}

bool nsDisplayTransform::CanUseAsyncAnimations() {
  return mPrerenderDecision != PrerenderDecision::No;
}

bool nsDisplayBackgroundColor::CanUseAsyncAnimations() {
  return StaticPrefs::gfx_omta_background_color();
}

void nsDisplayTableBackgroundColor::Destroy(nsDisplayListBuilder* aBuilder) {
  RemoveDisplayItemFromFrame(aBuilder, mAncestorFrame);
  nsDisplayBackgroundColor::Destroy(aBuilder);
}

static bool IsInStickyPositionedSubtree(const nsIFrame* aFrame) {
  for (const nsIFrame* frame = aFrame; frame;
       frame = nsLayoutUtils::GetCrossDocParentFrameInProcess(frame)) {
    if (frame->IsStickyPositioned()) {
      return true;
    }
  }
  return false;
}

static bool ShouldUsePartialPrerender(const nsIFrame* aFrame) {
  return StaticPrefs::layout_animation_prerender_partial() &&
         // Bug 1642547: Support partial prerender for position:sticky elements.
         !IsInStickyPositionedSubtree(aFrame);
}

/* static */
auto nsDisplayTransform::ShouldPrerenderTransformedContent(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsRect* aDirtyRect)
    -> PrerenderInfo {
  PrerenderInfo result;

  // Prerendering only makes sense if we are painting to the window so that the
  // extra prerendered content can be animated into view by the compositor.
  // GenerateGlyphMask (for background-clip: text) uses a nested builder, so it
  // can be inside a builder that is painting to window, and it's buggy, so just
  // allow it to minimize bugs.
  if (!aBuilder->IsPaintingToWindow() && !aBuilder->IsForGenerateGlyphMask()) {
    return result;
  }

  // If we are in a preserve-3d tree, and we've disallowed async animations, we
  // return No prerender decision directly.
  if ((aFrame->Extend3DContext() ||
       aFrame->Combines3DTransformWithAncestors()) &&
      !aBuilder->GetPreserves3DAllowAsyncAnimation()) {
    return result;
  }

  // Elements whose transform has been modified recently, or which
  // have a compositor-animated transform, can be prerendered. An element
  // might have only just had its transform animated in which case
  // the ActiveLayerManager may not have been notified yet.
  static constexpr nsCSSPropertyIDSet transformSet =
      nsCSSPropertyIDSet::TransformLikeProperties();
  if (!ActiveLayerTracker::IsTransformAnimated(aFrame) &&
      !(aFrame->StyleDisplay()->mWillChange.bits &
        StyleWillChangeBits::TRANSFORM) &&
      !EffectCompositor::HasAnimationsForCompositor(
          aFrame, DisplayItemType::TYPE_TRANSFORM)) {
    EffectCompositor::SetPerformanceWarning(
        aFrame, transformSet,
        AnimationPerformanceWarning(
            AnimationPerformanceWarning::Type::TransformFrameInactive));

    // This case happens when we're sure that the frame is not animated and its
    // preserve-3d ancestors are not, either. So we don't need to pre-render.
    // However, this decision shouldn't affect the decisions for other frames in
    // the preserve-3d context. We need this flag to determine whether we should
    // block async animations on other frames in the current preserve-3d tree.
    result.mHasAnimations = false;
    return result;
  }

  // We should not allow prerender if any ancestor container element has
  // mask/clip-path effects.
  //
  // With prerender and async transform animation, we do not need to restyle an
  // animated element to respect position changes, since that transform is done
  // by layer animation. As a result, the container element is not aware of
  // position change of that containing element and loses the chance to update
  // the content of mask/clip-path.
  //
  // Why do we need to update a mask? This is relative to how we generate a
  // mask layer in ContainerState::SetupMaskLayerForCSSMask. While creating a
  // mask layer, to reduce memory usage, we did not choose the size of the
  // masked element as mask size. Instead, we read the union of bounds of all
  // children display items by nsDisplayWrapList::GetBounds, which is smaller
  // than or equal to the masked element's boundary, and use it as the position
  // size of the mask layer. That union bounds is actually affected by the
  // geometry of the animated element. To keep the content of mask up to date,
  // forbidding of prerender is required.
  for (nsIFrame* container =
           nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
       container;
       container = nsLayoutUtils::GetCrossDocParentFrameInProcess(container)) {
    const nsStyleSVGReset* svgReset = container->StyleSVGReset();
    if (svgReset->HasMask() || svgReset->HasClipPath()) {
      return result;
    }
  }

  // If the incoming dirty rect already contains the entire overflow area,
  // we are already rendering the entire content.
  nsRect overflow = aFrame->InkOverflowRectRelativeToSelf();
  // UntransformRect will not touch the output rect (`&untranformedDirtyRect`)
  // in cases of non-invertible transforms, so we set `untransformedRect` to
  // `aDirtyRect` as an initial value for such cases.
  nsRect untransformedDirtyRect = *aDirtyRect;
  UntransformRect(*aDirtyRect, overflow, aFrame, &untransformedDirtyRect);
  if (untransformedDirtyRect.Contains(overflow)) {
    *aDirtyRect = untransformedDirtyRect;
    result.mDecision = PrerenderDecision::Full;
    return result;
  }

  float viewportRatio =
      StaticPrefs::layout_animation_prerender_viewport_ratio_limit();
  uint32_t absoluteLimitX =
      StaticPrefs::layout_animation_prerender_absolute_limit_x();
  uint32_t absoluteLimitY =
      StaticPrefs::layout_animation_prerender_absolute_limit_y();
  nsSize refSize = aBuilder->RootReferenceFrame()->GetSize();

  float resolution = aFrame->PresShell()->GetCumulativeResolution();
  if (resolution < 1.0f) {
    refSize.SizeTo(
        NSCoordSaturatingNonnegativeMultiply(refSize.width, 1.0f / resolution),
        NSCoordSaturatingNonnegativeMultiply(refSize.height,
                                             1.0f / resolution));
  }

  // Only prerender if the transformed frame's size is <= a multiple of the
  // reference frame size (~viewport), and less than an absolute limit.
  // Both the ratio and the absolute limit are configurable.
  nscoord maxLength = std::max(nscoord(refSize.width * viewportRatio),
                               nscoord(refSize.height * viewportRatio));
  nsSize relativeLimit(maxLength, maxLength);
  nsSize absoluteLimit(
      aFrame->PresContext()->DevPixelsToAppUnits(absoluteLimitX),
      aFrame->PresContext()->DevPixelsToAppUnits(absoluteLimitY));
  nsSize maxSize = Min(relativeLimit, absoluteLimit);

  const auto transform = nsLayoutUtils::GetTransformToAncestor(
      RelativeTo{aFrame},
      RelativeTo{nsLayoutUtils::GetDisplayRootFrame(aFrame)});
  const gfxRect transformedBounds = transform.TransformAndClipBounds(
      gfxRect(overflow.x, overflow.y, overflow.width, overflow.height),
      gfxRect::MaxIntRect());
  const nsSize frameSize =
      nsSize(transformedBounds.width, transformedBounds.height);

  uint64_t maxLimitArea = uint64_t(maxSize.width) * maxSize.height;
  uint64_t frameArea = uint64_t(frameSize.width) * frameSize.height;
  if (frameArea <= maxLimitArea && frameSize <= absoluteLimit) {
    *aDirtyRect = overflow;
    result.mDecision = PrerenderDecision::Full;
    return result;
  }

  if (ShouldUsePartialPrerender(aFrame)) {
    *aDirtyRect = nsLayoutUtils::ComputePartialPrerenderArea(
        aFrame, untransformedDirtyRect, overflow, maxSize);
    result.mDecision = PrerenderDecision::Partial;
    return result;
  }

  if (frameArea > maxLimitArea) {
    uint64_t appUnitsPerPixel = AppUnitsPerCSSPixel();
    EffectCompositor::SetPerformanceWarning(
        aFrame, transformSet,
        AnimationPerformanceWarning(
            AnimationPerformanceWarning::Type::ContentTooLargeArea,
            {
                int(frameArea / (appUnitsPerPixel * appUnitsPerPixel)),
                int(maxLimitArea / (appUnitsPerPixel * appUnitsPerPixel)),
            }));
  } else {
    EffectCompositor::SetPerformanceWarning(
        aFrame, transformSet,
        AnimationPerformanceWarning(
            AnimationPerformanceWarning::Type::ContentTooLarge,
            {
                nsPresContext::AppUnitsToIntCSSPixels(frameSize.width),
                nsPresContext::AppUnitsToIntCSSPixels(frameSize.height),
                nsPresContext::AppUnitsToIntCSSPixels(relativeLimit.width),
                nsPresContext::AppUnitsToIntCSSPixels(relativeLimit.height),
                nsPresContext::AppUnitsToIntCSSPixels(absoluteLimit.width),
                nsPresContext::AppUnitsToIntCSSPixels(absoluteLimit.height),
            }));
  }

  return result;
}

/* If the matrix is singular, or a hidden backface is shown, the frame won't be
 * visible or hit. */
static bool IsFrameVisible(nsIFrame* aFrame, const Matrix4x4& aMatrix) {
  if (aMatrix.IsSingular()) {
    return false;
  }
  if (aFrame->BackfaceIsHidden() && aMatrix.IsBackfaceVisible()) {
    return false;
  }
  return true;
}

const Matrix4x4Flagged& nsDisplayTransform::GetTransform() const {
  if (mTransform) {
    return *mTransform;
  }

  float scale = mFrame->PresContext()->AppUnitsPerDevPixel();

  if (mHasTransformGetter) {
    mTransform.emplace((mFrame->GetTransformGetter())(mFrame, scale));
    Point3D newOrigin =
        Point3D(NSAppUnitsToFloatPixels(mToReferenceFrame.x, scale),
                NSAppUnitsToFloatPixels(mToReferenceFrame.y, scale), 0.0f);
    mTransform->ChangeBasis(newOrigin.x, newOrigin.y, newOrigin.z);
  } else if (!mIsTransformSeparator) {
    DebugOnly<bool> isReference = mFrame->IsTransformed() ||
                                  mFrame->Combines3DTransformWithAncestors() ||
                                  mFrame->Extend3DContext();
    MOZ_ASSERT(isReference);
    mTransform.emplace(
        GetResultingTransformMatrix(mFrame, ToReferenceFrame(), scale,
                                    INCLUDE_PERSPECTIVE | OFFSET_BY_ORIGIN));
  } else {
    // Use identity matrix
    mTransform.emplace();
  }

  return *mTransform;
}

const Matrix4x4Flagged& nsDisplayTransform::GetInverseTransform() const {
  if (mInverseTransform) {
    return *mInverseTransform;
  }

  MOZ_ASSERT(!GetTransform().IsSingular());

  mInverseTransform.emplace(GetTransform().Inverse());

  return *mInverseTransform;
}

Matrix4x4 nsDisplayTransform::GetTransformForRendering(
    LayoutDevicePoint* aOutOrigin, const nsDisplayListBuilder* aBuilder) const {
  if (!mFrame->HasPerspective() || mHasTransformGetter ||
      mIsTransformSeparator) {
    if (!mHasTransformGetter && !mIsTransformSeparator && aOutOrigin) {
      // If aOutOrigin is provided, put the offset to origin into it, because
      // we need to keep it separate for webrender. The combination of
      // *aOutOrigin and the returned matrix here should always be equivalent
      // to what GetTransform() would have returned.
      float scale = mFrame->PresContext()->AppUnitsPerDevPixel();
      *aOutOrigin = LayoutDevicePoint::FromAppUnits(ToReferenceFrame(), scale);

      // The rounding behavior should also be the same as GetTransform().
      if (nsLayoutUtils::ShouldSnapToGrid(mFrame, aBuilder)) {
        aOutOrigin->Round();
      }
      return GetResultingTransformMatrix(mFrame, nsPoint(0, 0), scale,
                                         INCLUDE_PERSPECTIVE);
    }
    return GetTransform().GetMatrix();
  }
  MOZ_ASSERT(!mHasTransformGetter);

  float scale = mFrame->PresContext()->AppUnitsPerDevPixel();
  // Don't include perspective transform, or the offset to origin, since
  // nsDisplayPerspective will handle both of those.
  return GetResultingTransformMatrix(mFrame, nsPoint(), scale, 0);
}

const Matrix4x4& nsDisplayTransform::GetAccumulatedPreserved3DTransform(
    nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(!mFrame->Extend3DContext() || IsLeafOf3DContext());

  if (!IsLeafOf3DContext()) {
    return GetTransform().GetMatrix();
  }

  if (!mTransformPreserves3D) {
    const nsIFrame* establisher;  // Establisher of the 3D rendering context.
    for (establisher = mFrame;
         establisher && establisher->Combines3DTransformWithAncestors();
         establisher =
             establisher->GetClosestFlattenedTreeAncestorPrimaryFrame()) {
    }
    const nsIFrame* establisherReference = aBuilder->FindReferenceFrameFor(
        nsLayoutUtils::GetCrossDocParentFrameInProcess(establisher));

    nsPoint offset = establisher->GetOffsetToCrossDoc(establisherReference);
    float scale = mFrame->PresContext()->AppUnitsPerDevPixel();
    uint32_t flags =
        INCLUDE_PRESERVE3D_ANCESTORS | INCLUDE_PERSPECTIVE | OFFSET_BY_ORIGIN;
    mTransformPreserves3D = std::make_unique<Matrix4x4>(
        GetResultingTransformMatrix(mFrame, offset, scale, flags));
  }

  return *mTransformPreserves3D;
}

WebRenderCommandsResult nsDisplayTransform::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  // We want to make sure we don't pollute the transform property in the WR
  // stacking context by including the position of this frame (relative to the
  // parent reference frame). We need to keep those separate; the position of
  // this frame goes into the stacking context bounds while the transform goes
  // into the transform.
  LayoutDevicePoint position;
  Matrix4x4 newTransformMatrix =
      GetTransformForRendering(&position, aDisplayListBuilder);

  gfx::Matrix4x4* transformForSC = &newTransformMatrix;
  if (newTransformMatrix.IsIdentity() && !mHasAssociatedPerspective) {
    // If the transform is an identity transform, strip it out so that WR
    // doesn't turn this stacking context into a reference frame, as it
    // affects positioning. Keep it when paired with a perspective: the
    // reference frame is what tells WR to flatten our 3D descendants into
    // our plane before the perspective applies.
    transformForSC = nullptr;

    // In ChooseScaleAndSetTransform, we round the offset from the reference
    // frame used to adjust the transform, if there is no transform, or it
    // is just a translation. We need to do the same here.
    if (nsLayoutUtils::ShouldSnapToGrid(mFrame, aDisplayListBuilder)) {
      position.Round();
    }
  }

  // We don't send animations for transform separator display items.
  uint64_t animationsId =
      mIsTransformSeparator
          ? 0
          : AddAnimationsForWebRender(
                this, aManager, aDisplayListBuilder,
                IsPartialPrerender() ? Some(position) : Nothing());

  nsDisplayTransform* deferredTransformItem = nullptr;
  if (ShouldDeferTransform()) {
    // If it has perspective, we create a new scroll data via the
    // UpdateScrollData call because that scenario is more complex. Otherwise,
    // if we don't contain any ASRs then just stash the transform on the
    // StackingContextHelper and apply it to any scroll data that are created
    // inside this nsDisplayTransform.
    deferredTransformItem = this;
  }

  // Determine if we're possibly animated (= would need an active layer in FLB).
  const bool animated = !mIsTransformSeparator &&
                        ActiveLayerTracker::IsTransformAnimated(Frame());

  wr::StackingContextParams params;
  params.mBoundTransform = &newTransformMatrix;

  if (mWrapsBackdropFilter) {
    params.flags |= wr::StackingContextFlags::WRAPS_BACKDROP_FILTER;
  }
  if (mForceIsolation) {
    params.flags |= wr::StackingContextFlags::FORCED_ISOLATION;
  }

  params.prim_flags = !BackfaceIsHidden()
                          ? wr::PrimitiveFlags::IS_BACKFACE_VISIBLE
                          : wr::PrimitiveFlags{0};
  params.mDeferredTransformItem = deferredTransformItem;
  params.mAnimated = animated;
  params.SetPreserve3D(mFrame->Extend3DContext() && !mIsTransformSeparator);
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());

  LayoutDeviceSize boundsSize = LayoutDeviceSize::FromAppUnits(
      mChildBounds.Size(), mFrame->PresContext()->AppUnitsPerDevPixel());

  // The origin of an outer <svg>'s content goes into its own translation-only
  // reference frame that WebRender snaps to the device pixel grid, so that the
  // SVG content is laid out from a pixel aligned origin wherever the <svg>
  // element lands. The viewBox transform then applies unsnapped inside it.
  Maybe<StackingContextHelper> originSc;
  const gfx::Matrix4x4 identity;
  if (mFrame->IsSVGOuterSVGAnonChildFrame() && !animated &&
      !mIsTransformSeparator) {
    // The origin stacking context only copies the state that applies to it.
    // The rest stays on the inner one, which must remain the direct child of
    // a perspective or of a 3D rendering context it belongs to. This frame is
    // never CSS-transformed, so it can't be in either situation.
    MOZ_ASSERT(!mHasAssociatedPerspective);
    MOZ_ASSERT(!mFrame->Combines3DTransformWithAncestors());
    wr::StackingContextParams originParams;
    // An identity bound transform makes the helper inherit the scale and the
    // snapping surface transform, which blob rasterization relies on.
    originParams.mBoundTransform = &identity;
    originParams.clip = params.clip;
    originParams.prim_flags = params.prim_flags;
    wr::ReferenceFrameParams originRfParams;
    originRfParams.is_2d_scale_translation = true;
    originRfParams.should_snap = true;
    wr::WrSpatialId originSpatialId =
        aManager->CommandBuilder().DefineReferenceFrame(
            aBuilder, mFrame,
            WebRenderCommandBuilder::FrameReferenceFrame::SVGOrigin, position,
            originRfParams);
    originSc.emplace(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                     originParams, Some(originSpatialId), boundsSize);
    // Items take their spatial node from the clip manager, which maps their
    // ASR to a spatial id. The inner stacking context only overrides that
    // mapping if it creates a reference frame or has a non-zero origin, which
    // is not the case with an identity viewBox transform. Override it here so
    // that the content is always positioned by the snapped origin frame.
    aManager->CommandBuilder().PushOverrideForASR(GetActiveScrolledRoot(),
                                                  originSpatialId);
    position = LayoutDevicePoint();
  }

  // The transform, or the position alone if it is untransformed, goes into a
  // reference frame. This has to be defined once originSc is in place, as
  // that is the space it is in.
  Maybe<wr::WrSpatialId> referenceFrame;
  if (transformForSC || animationsId || position != LayoutDevicePoint()) {
    wr::ReferenceFrameParams rfParams;
    rfParams.transform_style = params.transform_style;
    if (transformForSC || animationsId) {
      rfParams.transform = wr::ToLayoutTransform(
          transformForSC ? *transformForSC : gfx::Matrix4x4());
      rfParams.animation_id = animationsId;
      rfParams.paired_with_perspective = mHasAssociatedPerspective;
    } else {
      rfParams.is_2d_scale_translation = true;
    }
    referenceFrame = Some(aManager->CommandBuilder().DefineReferenceFrame(
        aBuilder, mFrame,
        mIsTransformSeparator
            ? WebRenderCommandBuilder::FrameReferenceFrame::TransformSeparator
            : WebRenderCommandBuilder::FrameReferenceFrame::Transform,
        position, rfParams));
  }

  {
    StackingContextHelper sc(originSc ? *originSc : aSc,
                             GetActiveScrolledRoot(), mFrame, this, aBuilder,
                             params, referenceFrame, boundsSize);

    aManager->CommandBuilder().CreateWebRenderCommandsFromDisplayList(
        GetChildren(), this, aDisplayListBuilder, sc, aBuilder, aResources);
  }

  if (originSc) {
    aManager->CommandBuilder().PopOverrideForASR(GetActiveScrolledRoot());
  }
  return Ok();
}

bool nsDisplayTransform::UpdateScrollData(
    WebRenderScrollData* aData, WebRenderLayerScrollData* aLayerData) {
  if (ShouldDeferTransform()) {
    // This case is handled in CreateWebRenderCommands by stashing the transform
    // on the stacking context.
    return false;
  }
  if (aLayerData) {
    aLayerData->SetTransform(GetTransform().GetMatrix());
    aLayerData->SetTransformIsPerspective(mFrame->ChildrenHavePerspective());
  }
  return true;
}

bool nsDisplayTransform::ShouldSkipTransform(
    nsDisplayListBuilder* aBuilder) const {
  return (aBuilder->RootReferenceFrame() == mFrame) &&
         aBuilder->IsForGenerateGlyphMask();
}

void nsDisplayTransform::Collect3DTransformLeaves(
    nsDisplayListBuilder* aBuilder, nsTArray<nsDisplayTransform*>& aLeaves) {
  if (!IsParticipating3DContext() || IsLeafOf3DContext()) {
    aLeaves.AppendElement(this);
    return;
  }

  FlattenedDisplayListIterator iter(aBuilder, &mChildren);
  while (iter.HasNext()) {
    nsDisplayItem* item = iter.GetNextItem();
    if (item->GetType() == DisplayItemType::TYPE_PERSPECTIVE) {
      auto* perspective = static_cast<nsDisplayPerspective*>(item);
      if (!perspective->GetChildren()->GetTop()) {
        continue;
      }
      item = perspective->GetChildren()->GetTop();
    }
    if (item->GetType() != DisplayItemType::TYPE_TRANSFORM) {
      gfxCriticalError() << "Invalid child item within 3D transform of type: "
                         << item->Name();
      continue;
    }
    static_cast<nsDisplayTransform*>(item)->Collect3DTransformLeaves(aBuilder,
                                                                     aLeaves);
  }
}

static RefPtr<gfx::Path> BuildPathFromPolygon(const RefPtr<DrawTarget>& aDT,
                                              const gfx::Polygon& aPolygon) {
  MOZ_ASSERT(!aPolygon.IsEmpty());

  RefPtr<PathBuilder> pathBuilder = aDT->CreatePathBuilder();
  const nsTArray<Point4D>& points = aPolygon.GetPoints();

  pathBuilder->MoveTo(points[0].As2DPoint());

  for (size_t i = 1; i < points.Length(); ++i) {
    pathBuilder->LineTo(points[i].As2DPoint());
  }

  pathBuilder->Close();
  return pathBuilder->Finish();
}

void nsDisplayTransform::CollectSorted3DTransformLeaves(
    nsDisplayListBuilder* aBuilder, nsTArray<TransformPolygon>& aLeaves) {
  std::list<TransformPolygon> inputLayers;

  nsTArray<nsDisplayTransform*> leaves;
  Collect3DTransformLeaves(aBuilder, leaves);
  for (nsDisplayTransform* item : leaves) {
    auto bounds = LayoutDeviceRect::FromAppUnits(
        item->mChildBounds, item->mFrame->PresContext()->AppUnitsPerDevPixel());
    Matrix4x4 transform = item->GetAccumulatedPreserved3DTransform(aBuilder);

    if (!IsFrameVisible(item->mFrame, transform)) {
      continue;
    }
    gfx::Polygon polygon =
        gfx::Polygon::FromRect(gfx::Rect(bounds.ToUnknownRect()));

    polygon.TransformToScreenSpace(transform);

    if (polygon.GetPoints().Length() >= 3) {
      inputLayers.push_back(TransformPolygon(item, std::move(polygon)));
    }
  }

  if (inputLayers.empty()) {
    return;
  }

  BSPTree<nsDisplayTransform> tree(inputLayers);
  nsTArray<TransformPolygon> orderedLayers(tree.GetDrawOrder());

  for (TransformPolygon& polygon : orderedLayers) {
    Matrix4x4 inverse =
        polygon.data->GetAccumulatedPreserved3DTransform(aBuilder).Inverse();

    MOZ_ASSERT(polygon.geometry);
    polygon.geometry->TransformToLayerSpace(inverse);
  }

  aLeaves = std::move(orderedLayers);
}

void nsDisplayTransform::Paint(nsDisplayListBuilder* aBuilder,
                               gfxContext* aCtx) {
  Paint(aBuilder, aCtx, Nothing());
}

void nsDisplayTransform::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx,
                               const Maybe<gfx::Polygon>& aPolygon) {
  if (IsParticipating3DContext() && !IsLeafOf3DContext()) {
    MOZ_ASSERT(!aPolygon);
    nsTArray<TransformPolygon> leaves;
    CollectSorted3DTransformLeaves(aBuilder, leaves);
    for (TransformPolygon& item : leaves) {
      item.data->Paint(aBuilder, aCtx, item.geometry);
    }
    return;
  }

  gfxContextMatrixAutoSaveRestore saveMatrix(aCtx);
  Matrix4x4 trans = ShouldSkipTransform(aBuilder)
                        ? Matrix4x4()
                        : GetAccumulatedPreserved3DTransform(aBuilder);
  if (!IsFrameVisible(mFrame, trans)) {
    return;
  }

  Matrix trans2d;
  if (trans.CanDraw2D(&trans2d)) {
    aCtx->Multiply(ThebesMatrix(trans2d));

    if (aPolygon) {
      RefPtr<gfx::Path> path =
          BuildPathFromPolygon(aCtx->GetDrawTarget(), *aPolygon);
      aCtx->GetDrawTarget()->PushClip(path);
    }

    GetChildren()->Paint(aBuilder, aCtx,
                         mFrame->PresContext()->AppUnitsPerDevPixel());

    if (aPolygon) {
      aCtx->GetDrawTarget()->PopClip();
    }
    return;
  }

  // TODO: Implement 3d transform handling, including plane splitting and
  // sorting. See BasicCompositor.
  auto pixelBounds = LayoutDeviceRect::FromAppUnitsToOutside(
      mChildBounds, mFrame->PresContext()->AppUnitsPerDevPixel());
  RefPtr<DrawTarget> untransformedDT =
      gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
          IntSize(pixelBounds.Width(), pixelBounds.Height()),
          SurfaceFormat::B8G8R8A8, true);
  if (!untransformedDT || !untransformedDT->IsValid()) {
    return;
  }
  untransformedDT->SetTransform(
      Matrix::Translation(-Point(pixelBounds.X(), pixelBounds.Y())));

  gfxContext groupTarget(untransformedDT, /* aPreserveTransform */ true);

  if (aPolygon) {
    RefPtr<gfx::Path> path =
        BuildPathFromPolygon(aCtx->GetDrawTarget(), *aPolygon);
    aCtx->GetDrawTarget()->PushClip(path);
  }

  GetChildren()->Paint(aBuilder, &groupTarget,
                       mFrame->PresContext()->AppUnitsPerDevPixel());

  if (aPolygon) {
    aCtx->GetDrawTarget()->PopClip();
  }

  RefPtr<SourceSurface> untransformedSurf = untransformedDT->Snapshot();

  trans.PreTranslate(pixelBounds.X(), pixelBounds.Y(), 0);
  aCtx->GetDrawTarget()->Draw3DTransformedSurface(untransformedSurf, trans);
}

bool nsDisplayTransform::MayBeAnimated(nsDisplayListBuilder* aBuilder) const {
  // If EffectCompositor::HasAnimationsForCompositor() is true then we can
  // completely bypass the main thread for this animation, so it is always
  // worthwhile.
  // For ActiveLayerTracker::IsTransformAnimated() cases the main thread is
  // already involved so there is less to be gained.
  // Therefore we check that the *post-transform* bounds of this item are
  // big enough to justify an active layer.
  return EffectCompositor::HasAnimationsForCompositor(
             mFrame, DisplayItemType::TYPE_TRANSFORM) ||
         ActiveLayerTracker::IsTransformAnimated(mFrame);
}

nsRect nsDisplayTransform::TransformUntransformedBounds(
    nsDisplayListBuilder* aBuilder, const Matrix4x4Flagged& aMatrix) const {
  const nsRect untransformedBounds = GetUntransformedBounds(aBuilder);
  // GetTransform always operates in dev pixels.
  const float factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  return nsLayoutUtils::MatrixTransformRect(untransformedBounds, aMatrix,
                                            factor);
}

/**
 * Returns the bounds for this transform. The bounds are calculated during
 * display list building and merging, see |nsDisplayTransform::UpdateBounds()|.
 */
nsRect nsDisplayTransform::GetBounds(nsDisplayListBuilder* aBuilder,
                                     bool* aSnap) const {
  *aSnap = false;
  return mBounds;
}

void nsDisplayTransform::ComputeBounds(nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(mFrame->Extend3DContext() || IsLeafOf3DContext());

  /* Some transforms can get empty bounds in 2D, but might get transformed again
   * and get non-empty bounds. A simple example of this would be a 180 degree
   * rotation getting applied twice.
   * We should not depend on transforming bounds level by level.
   *
   * This function collects the bounds of this transform and stores it in
   * nsDisplayListBuilder. If this is not a leaf of a 3D context, we recurse
   * down and include the bounds of the child transforms.
   * The bounds are transformed with the accumulated transformation matrix up to
   * the 3D context root coordinate space.
   */
  nsDisplayListBuilder::AutoAccumulateTransform accTransform(aBuilder);
  accTransform.Accumulate(GetTransform().GetMatrix());

  // Do not dive into another 3D context.
  if (!IsLeafOf3DContext()) {
    for (nsDisplayItem* i : *GetChildren()) {
      i->DoUpdateBoundsPreserves3D(aBuilder);
    }
  }

  /* The child transforms that extend 3D context further will have empty bounds,
   * so the untransformed bounds here is the bounds of all the non-preserve-3d
   * content under this transform.
   */
  const nsRect rect = TransformUntransformedBounds(
      aBuilder, accTransform.GetCurrentTransform());
  aBuilder->AccumulateRect(rect);
}

void nsDisplayTransform::DoUpdateBoundsPreserves3D(
    nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(mFrame->Combines3DTransformWithAncestors() ||
             IsTransformSeparator());
  // Updating is not going through to child 3D context.
  ComputeBounds(aBuilder);
}

void nsDisplayTransform::UpdateBounds(nsDisplayListBuilder* aBuilder) {
  UpdateUntransformedBounds(aBuilder);

  if (IsTransformSeparator()) {
    MOZ_ASSERT(GetTransform().IsIdentity());
    mBounds = mChildBounds;
    return;
  }

  if (mFrame->Extend3DContext()) {
    if (!Combines3DTransformWithAncestors()) {
      // The transform establishes a 3D context. |UpdateBoundsFor3D()| will
      // collect the bounds from the child transforms.
      UpdateBoundsFor3D(aBuilder);
    } else {
      // With nested 3D transforms, the 2D bounds might not be useful.
      mBounds = nsRect();
    }

    return;
  }

  MOZ_ASSERT(!mFrame->Extend3DContext());

  // We would like to avoid calculating 2D bounds here for nested 3D transforms,
  // but mix-blend-mode relies on having bounds set. See bug 1556956.

  // A stand-alone transform.
  mBounds = TransformUntransformedBounds(aBuilder, GetTransform());
}

void nsDisplayTransform::UpdateBoundsFor3D(nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(mFrame->Extend3DContext() &&
             !mFrame->Combines3DTransformWithAncestors() &&
             !IsTransformSeparator());

  // Always start updating from an establisher of a 3D rendering context.
  nsDisplayListBuilder::AutoAccumulateRect accRect(aBuilder);
  nsDisplayListBuilder::AutoAccumulateTransform accTransform(aBuilder);
  accTransform.StartRoot();
  ComputeBounds(aBuilder);
  mBounds = aBuilder->GetAccumulatedRect();
}

void nsDisplayTransform::UpdateUntransformedBounds(
    nsDisplayListBuilder* aBuilder) {
  mChildBounds = GetChildren()->GetClippedBoundsWithRespectToASR(
      aBuilder, mActiveScrolledRoot);
}

#ifdef DEBUG_HIT
#  include <time.h>
#endif

/* HitTest does some fun stuff with matrix transforms to obtain the answer. */
void nsDisplayTransform::HitTest(nsDisplayListBuilder* aBuilder,
                                 const nsRect& aRect, HitTestState* aState,
                                 nsTArray<nsIFrame*>* aOutFrames) {
  if (aState->mGatheringPreserves3DLeaves) {
    GetChildren()->HitTest(aBuilder, aRect, aState, aOutFrames);
    return;
  }

  /* Here's how this works:
   * 1. Get the matrix.  If it's singular, abort (clearly we didn't hit
   *    anything).
   * 2. Invert the matrix.
   * 3. Use it to transform the rect into the correct space.
   * 4. Pass that rect down through to the list's version of HitTest.
   */
  // GetTransform always operates in dev pixels.
  float factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  Matrix4x4 matrix = GetAccumulatedPreserved3DTransform(aBuilder);

  if (!IsFrameVisible(mFrame, matrix)) {
    return;
  }

  const bool oldHitOccludingItem = aState->mHitOccludingItem;

  /* We want to go from transformed-space to regular space.
   * Thus we have to invert the matrix, which normally does
   * the reverse operation (e.g. regular->transformed)
   */

  /* Now, apply the transform and pass it down the channel. */
  matrix.Invert();
  nsRect resultingRect;
  // Magic width/height indicating we're hit testing a point, not a rect
  const bool testingPoint = aRect.width == 1 && aRect.height == 1;
  if (testingPoint) {
    Point4D point =
        matrix.ProjectPoint(Point(NSAppUnitsToFloatPixels(aRect.x, factor),
                                  NSAppUnitsToFloatPixels(aRect.y, factor)));
    if (!point.HasPositiveWCoord()) {
      return;
    }

    Point point2d = point.As2DPoint();

    resultingRect =
        nsRect(NSFloatPixelsToAppUnits(float(point2d.x), factor),
               NSFloatPixelsToAppUnits(float(point2d.y), factor), 1, 1);

  } else {
    Rect originalRect(NSAppUnitsToFloatPixels(aRect.x, factor),
                      NSAppUnitsToFloatPixels(aRect.y, factor),
                      NSAppUnitsToFloatPixels(aRect.width, factor),
                      NSAppUnitsToFloatPixels(aRect.height, factor));

    Rect childGfxBounds(NSAppUnitsToFloatPixels(mChildBounds.x, factor),
                        NSAppUnitsToFloatPixels(mChildBounds.y, factor),
                        NSAppUnitsToFloatPixels(mChildBounds.width, factor),
                        NSAppUnitsToFloatPixels(mChildBounds.height, factor));

    Rect rect = matrix.ProjectRectBounds(originalRect, childGfxBounds);

    resultingRect =
        nsRect(NSFloatPixelsToAppUnits(float(rect.X()), factor),
               NSFloatPixelsToAppUnits(float(rect.Y()), factor),
               NSFloatPixelsToAppUnits(float(rect.Width()), factor),
               NSFloatPixelsToAppUnits(float(rect.Height()), factor));
  }

  if (resultingRect.IsEmpty()) {
    return;
  }

#ifdef DEBUG_HIT
  printf("Frame: %p\n", dynamic_cast<void*>(mFrame));
  printf("  Untransformed point: (%f, %f)\n", resultingRect.X(),
         resultingRect.Y());
  uint32_t originalFrameCount = aOutFrames.Length();
#endif

  const bool savedTransformHasBackfaceVisible =
      aState->mTransformHasBackfaceVisible;
  if (IsLeafOf3DContext()) {
    aState->mTransformHasBackfaceVisible = matrix.IsBackfaceVisible();
  }
  GetChildren()->HitTest(aBuilder, resultingRect, aState, aOutFrames);
  if (IsLeafOf3DContext()) {
    aState->mTransformHasBackfaceVisible = savedTransformHasBackfaceVisible;
  }

  if (aState->mHitOccludingItem && !testingPoint && !mBounds.Contains(aRect)) {
    MOZ_ASSERT(aBuilder->HitTestIsForVisibility());
    // We're hit-testing a rect that's bigger than our child bounds, but
    // resultingRect is clipped by our bounds (in ProjectRectBounds above), so
    // we can't stop hit-testing altogether.
    //
    // FIXME(emilio): I think this means that theoretically we might include
    // some frames fully behind other transformed-but-opaque frames? Then again
    // that's our pre-existing behavior for other untransformed content that
    // doesn't fill the whole rect. To be fully correct I think we'd need proper
    // "known occluded region" tracking, but that might be overkill for our
    // purposes here.
    aState->mHitOccludingItem = oldHitOccludingItem;
  }

#ifdef DEBUG_HIT
  if (originalFrameCount != aOutFrames.Length())
    printf("  Hit! Time: %f, first frame: %p\n", static_cast<double>(clock()),
           dynamic_cast<void*>(aOutFrames.ElementAt(0)));
  printf("=== end of hit test ===\n");
#endif
}

float nsDisplayTransform::GetHitDepthAtPoint(nsDisplayListBuilder* aBuilder,
                                             const nsPoint& aPoint) {
  // GetTransform always operates in dev pixels.
  float factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  Matrix4x4 matrix = GetAccumulatedPreserved3DTransform(aBuilder);

  NS_ASSERTION(IsFrameVisible(mFrame, matrix),
               "We can't have hit a frame that isn't visible!");

  Matrix4x4 inverse = matrix;
  inverse.Invert();

  // Compute the value z so that (aPoint.x, aPoint.y, z, 1) gets transformed by
  // inverse to the z=0 plane. This is the same thing that
  // Matrix4x4Typed::ProjectPoint does, but we are only interested in the z
  // value, not the projected point, thus we extract the formula here, look
  // there for how this equation is determined.
  // https://searchfox.org/mozilla-central/rev/bd4d1cd1ca3037e6dc8d4081a4303824880b1b45/gfx/2d/Matrix.h#724
  return -(NSAppUnitsToFloatPixels(aPoint.x, factor) * inverse._13 +
           NSAppUnitsToFloatPixels(aPoint.y, factor) * inverse._23 +
           inverse._43) /
         inverse._33;
}

/* The transform is opaque iff the transform consists solely of scales and
 * translations and if the underlying content is opaque.  Thus if the transform
 * is of the form
 *
 * |a c e|
 * |b d f|
 * |0 0 1|
 *
 * We need b and c to be zero.
 *
 * We also need to check whether the underlying opaque content completely fills
 * our visible rect. We use UntransformRect which expands to the axis-aligned
 * bounding rect, but that's OK since if
 * mStoredList.GetVisibleRect().Contains(untransformedVisible), then it
 * certainly contains the actual (non-axis-aligned) untransformed rect.
 */
nsRegion nsDisplayTransform::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                             bool* aSnap) const {
  *aSnap = false;

  nsRect untransformedVisible;
  if (!UntransformBuildingRect(aBuilder, &untransformedVisible)) {
    return nsRegion();
  }

  const Matrix4x4Flagged& matrix = GetTransform();
  Matrix matrix2d;
  if (!matrix.Is2D(&matrix2d) || !matrix2d.PreservesAxisAlignedRectangles()) {
    return nsRegion();
  }

  nsRegion result;

  const nsRect bounds = GetUntransformedBounds(aBuilder);
  const nsRegion opaque =
      ::mozilla::GetOpaqueRegion(aBuilder, GetChildren(), bounds);

  if (opaque.Contains(untransformedVisible)) {
    bool tmpSnap;
    result = GetBuildingRect().Intersect(GetBounds(aBuilder, &tmpSnap));
  }
  return result;
}

nsRect nsDisplayTransform::GetComponentAlphaBounds(
    nsDisplayListBuilder* aBuilder) const {
  if (GetChildren()->GetComponentAlphaBounds(aBuilder).IsEmpty()) {
    return nsRect();
  }

  bool snap;
  return GetBounds(aBuilder, &snap);
}

/* TransformRect takes in as parameters a rectangle (in app space) and returns
 * the smallest rectangle (in app space) containing the transformed image of
 * that rectangle.  That is, it takes the four corners of the rectangle,
 * transforms them according to the matrix associated with the specified frame,
 * then returns the smallest rectangle containing the four transformed points.
 *
 * @param aUntransformedBounds The rectangle (in app units) to transform.
 * @param aFrame The frame whose transformation should be applied.
 * @param aOrigin The delta from the frame origin to the coordinate space origin
 * @return The smallest rectangle containing the image of the transformed
 *         rectangle.
 */
nsRect nsDisplayTransform::TransformRect(const nsRect& aUntransformedBounds,
                                         const nsIFrame* aFrame,
                                         TransformReferenceBox& aRefBox) {
  MOZ_ASSERT(aFrame, "Can't take the transform based on a null frame!");

  float factor = aFrame->PresContext()->AppUnitsPerDevPixel();

  FrameTransformProperties props(aFrame, aRefBox, factor);
  return nsLayoutUtils::MatrixTransformRect(
      aUntransformedBounds,
      GetResultingTransformMatrixInternal(
          props, aRefBox, nsPoint(), factor,
          kTransformRectFlags & ~OFFSET_BY_ORIGIN),
      factor);
}

bool nsDisplayTransform::UntransformRect(const nsRect& aTransformedBounds,
                                         const nsRect& aChildBounds,
                                         const nsIFrame* aFrame,
                                         nsRect* aOutRect) {
  MOZ_ASSERT(aFrame, "Can't take the transform based on a null frame!");

  float factor = aFrame->PresContext()->AppUnitsPerDevPixel();
  Matrix4x4 transform = GetResultingTransformMatrix(
      aFrame, nsPoint(), factor, kTransformRectFlags & ~OFFSET_BY_ORIGIN);
  return UntransformRect(aTransformedBounds, aChildBounds, transform, factor,
                         aOutRect);
}

bool nsDisplayTransform::UntransformRect(const nsRect& aTransformedBounds,
                                         const nsRect& aChildBounds,
                                         const Matrix4x4& aMatrix,
                                         float aAppUnitsPerPixel,
                                         nsRect* aOutRect) {
  Maybe<Matrix4x4> inverse = aMatrix.MaybeInverse();
  if (inverse.isNothing()) {
    return false;
  }

  RectDouble result(
      NSAppUnitsToFloatPixels(aTransformedBounds.x, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aTransformedBounds.y, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aTransformedBounds.width, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aTransformedBounds.height, aAppUnitsPerPixel));

  RectDouble childGfxBounds(
      NSAppUnitsToFloatPixels(aChildBounds.x, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aChildBounds.y, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aChildBounds.width, aAppUnitsPerPixel),
      NSAppUnitsToFloatPixels(aChildBounds.height, aAppUnitsPerPixel));

  result = inverse->ProjectRectBounds(result, childGfxBounds);
  *aOutRect = nsLayoutUtils::RoundGfxRectToAppRect(result, aAppUnitsPerPixel);
  return true;
}

bool nsDisplayTransform::UntransformRect(nsDisplayListBuilder* aBuilder,
                                         const nsRect& aRect,
                                         nsRect* aOutRect) const {
  if (GetTransform().IsSingular()) {
    return false;
  }

  // GetTransform always operates in dev pixels.
  float factor = mFrame->PresContext()->AppUnitsPerDevPixel();
  RectDouble result(NSAppUnitsToFloatPixels(aRect.x, factor),
                    NSAppUnitsToFloatPixels(aRect.y, factor),
                    NSAppUnitsToFloatPixels(aRect.width, factor),
                    NSAppUnitsToFloatPixels(aRect.height, factor));

  nsRect childBounds = GetUntransformedBounds(aBuilder);
  RectDouble childGfxBounds(
      NSAppUnitsToFloatPixels(childBounds.x, factor),
      NSAppUnitsToFloatPixels(childBounds.y, factor),
      NSAppUnitsToFloatPixels(childBounds.width, factor),
      NSAppUnitsToFloatPixels(childBounds.height, factor));

  /* We want to untransform the matrix, so invert the transformation first! */
  result = GetInverseTransform().ProjectRectBounds(result, childGfxBounds);

  *aOutRect = nsLayoutUtils::RoundGfxRectToAppRect(result, factor);

  return true;
}

void nsDisplayTransform::WriteDebugInfo(std::stringstream& aStream) {
  aStream << GetTransform().GetMatrix();
  if (IsTransformSeparator()) {
    aStream << " transform-separator";
  }
  if (IsLeafOf3DContext()) {
    aStream << " 3d-context-leaf";
  }
  if (mFrame->Extend3DContext()) {
    aStream << " extends-3d-context";
  }
  if (mFrame->Combines3DTransformWithAncestors()) {
    aStream << " combines-3d-with-ancestors";
  }

  aStream << " prerender(";
  switch (mPrerenderDecision) {
    case PrerenderDecision::No:
      aStream << "no";
      break;
    case PrerenderDecision::Partial:
      aStream << "partial";
      break;
    case PrerenderDecision::Full:
      aStream << "full";
      break;
  }
  aStream << ")";
  aStream << " childrenBuildingRect" << mChildrenBuildingRect;
}

nsDisplayPerspective::nsDisplayPerspective(nsDisplayListBuilder* aBuilder,
                                           nsIFrame* aFrame,
                                           nsDisplayList* aList)
    : nsPaintedDisplayItem(aBuilder, aFrame) {
  mList.AppendToTop(aList);
  MOZ_ASSERT(mList.Length() == 1);
  MOZ_ASSERT(mList.GetTop()->GetType() == DisplayItemType::TYPE_TRANSFORM);
}

void nsDisplayPerspective::Paint(nsDisplayListBuilder* aBuilder,
                                 gfxContext* aCtx) {
  // Just directly recurse into children, since we'll include the persepctive
  // value in any nsDisplayTransform children.
  GetChildren()->Paint(aBuilder, aCtx,
                       mFrame->PresContext()->AppUnitsPerDevPixel());
}

nsRegion nsDisplayPerspective::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                               bool* aSnap) const {
  if (!GetChildren()->GetTop()) {
    *aSnap = false;
    return nsRegion();
  }

  return GetChildren()->GetTop()->GetOpaqueRegion(aBuilder, aSnap);
}

WebRenderCommandsResult nsDisplayPerspective::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  float appUnitsPerPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  Matrix4x4 perspectiveMatrix;
  DebugOnly<bool> hasPerspective = nsDisplayTransform::ComputePerspectiveMatrix(
      mFrame, appUnitsPerPixel, perspectiveMatrix);
  MOZ_ASSERT(hasPerspective, "Why did we create nsDisplayPerspective?");

  /*
   * ClipListToRange can remove our child after we were created.
   */
  if (!GetChildren()->GetTop()) {
    return Err("perspective item lost its child");
  }

  /*
   * The resulting matrix is still in the coordinate space of the transformed
   * frame. Append a translation to the reference frame coordinates.
   */
  nsDisplayTransform* transform =
      static_cast<nsDisplayTransform*>(GetChildren()->GetTop());

  Point3D newOrigin =
      Point3D(NSAppUnitsToFloatPixels(transform->ToReferenceFrame().x,
                                      appUnitsPerPixel),
              NSAppUnitsToFloatPixels(transform->ToReferenceFrame().y,
                                      appUnitsPerPixel),
              0.0f);
  Point3D roundedOrigin(NS_round(newOrigin.x), NS_round(newOrigin.y), 0);

  perspectiveMatrix.PostTranslate(roundedOrigin);

  nsIFrame* perspectiveFrame =
      mFrame->GetClosestFlattenedTreeAncestorPrimaryFrame();

  // Passing true here is always correct, since perspective always combines
  // transforms with the descendants. However that'd make WR do a lot of work
  // that it doesn't really need to do if there aren't other transforms forming
  // part of the 3D context.
  //
  // WR knows how to treat perspective in that case, so the only thing we need
  // to do is to ensure we pass true when we're involved in a 3d context in any
  // other way via the transform-style property on either the transformed frame
  // or the perspective frame in order to not confuse WR's preserve-3d code in
  // very awful ways.
  bool preserve3D =
      mFrame->Extend3DContext() || perspectiveFrame->Extend3DContext();

  wr::StackingContextParams params;
  wr::ReferenceFrameParams rfParams;
  rfParams.transform = wr::ToLayoutTransform(perspectiveMatrix);
  rfParams.kind = wr::WrReferenceFrameKind::Perspective;
  rfParams.SetPreserve3D(preserve3D);

  params.prim_flags = !BackfaceIsHidden()
                          ? wr::PrimitiveFlags::IS_BACKFACE_VISIBLE
                          : wr::PrimitiveFlags{0};
  params.SetPreserve3D(preserve3D);
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());

  Maybe<uint64_t> scrollingRelativeTo;
  for (const auto* asr = GetActiveScrolledRoot(); asr; asr = asr->mParent) {
    // In OOP documents, the root scrollable frame of the in-process root
    // document is always active, so using IsAncestorFrameCrossDocInProcess
    // should be fine here.
    if (ScrollContainerFrame* scrollFrame = asr->ScrollFrameOrNull()) {
      if (nsLayoutUtils::IsAncestorFrameCrossDocInProcess(
              scrollFrame->GetScrolledFrame(), perspectiveFrame)) {
        scrollingRelativeTo.emplace(asr->GetViewId());
        break;
      }
    }
  }

  // We put the perspective reference frame wrapping the transformed frame,
  // even though there may be arbitrarily nested scroll frames in between.
  //
  // We need to know how many ancestor scroll-frames are we nested in, in order
  // for the async scrolling code in WebRender to calculate the right
  // transformation for the perspective contents.
  rfParams.scrolling_relative_to = scrollingRelativeTo.ptrOr(nullptr);

  wr::WrSpatialId referenceFrame =
      aManager->CommandBuilder().DefineReferenceFrame(
          aBuilder, mFrame,
          WebRenderCommandBuilder::FrameReferenceFrame::Perspective,
          LayoutDevicePoint(), rfParams);
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params, Some(referenceFrame));

  aManager->CommandBuilder().CreateWebRenderCommandsFromDisplayList(
      GetChildren(), this, aDisplayListBuilder, sc, aBuilder, aResources);

  return Ok();
}

nsDisplayText::nsDisplayText(nsDisplayListBuilder* aBuilder,
                             nsTextFrame* aFrame)
    : nsPaintedDisplayItem(aBuilder, aFrame),
      mVisIStartEdge(0),
      mVisIEndEdge(0) {
  MOZ_COUNT_CTOR(nsDisplayText);
  mBounds = mFrame->InkOverflowRectRelativeToSelf() + ToReferenceFrame();
  // Bug 748228
  mBounds.Inflate(mFrame->PresContext()->AppUnitsPerDevPixel());
  mVisibleRect = aBuilder->GetVisibleRect() +
                 aBuilder->GetCurrentFrameOffsetToReferenceFrame();
}

bool nsDisplayText::CanApplyOpacity(WebRenderLayerManager* aManager,
                                    nsDisplayListBuilder* aBuilder) const {
  auto* f = static_cast<nsTextFrame*>(mFrame);

  if (f->IsSelected()) {
    return false;
  }

  const nsStyleText* textStyle = f->StyleText();
  if (textStyle->HasTextShadow()) {
    return false;
  }

  nsTextFrame::TextDecorations decorations;
  f->GetTextDecorations(f->PresContext(), nsTextFrame::eResolvedColors,
                        decorations);
  return !decorations.HasDecorationLines();
}

void nsDisplayText::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  AUTO_PROFILER_LABEL("nsDisplayText::Paint", GRAPHICS);
  // We don't pass mVisibleRect here, since this can be called from within
  // the WebRender fallback painting path, and we don't want to issue
  // recorded commands that are dependent on the visible/building rect.
  RenderToContext(aCtx, aBuilder, GetPaintRect(aBuilder, aCtx));

  auto* textFrame = static_cast<nsTextFrame*>(mFrame);
  nsRect boundsRelativeToSelf = mBounds - ToReferenceFrame();

  ContainerTimingHelpers::MaybeProcessPaintForContainer(
      textFrame->GetContent(), textFrame, boundsRelativeToSelf);

  LCPTextFrameHelper::MaybeUnionTextFrame(textFrame, boundsRelativeToSelf);
}

WebRenderCommandsResult nsDisplayText::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  auto* f = static_cast<nsTextFrame*>(mFrame);
  auto appUnitsPerDevPixel = f->PresContext()->AppUnitsPerDevPixel();

  nsRect bounds = f->WebRenderBounds() + ToReferenceFrame();
  // Bug 748228
  bounds.Inflate(appUnitsPerDevPixel);

  if (bounds.IsEmpty()) {
    return Ok();
  }

  // For large font sizes, punt to a blob image, to avoid the blurry rendering
  // that results from WR clamping the glyph size used for rasterization.
  //
  // (See FONT_SIZE_LIMIT in webrender/src/glyph_rasterizer/mod.rs.)
  //
  // This is not strictly accurate, as final used font sizes might not be the
  // same as claimed by the fontGroup's style.size (eg: due to font-size-adjust
  // altering the used size of the font actually used).
  // It also fails to consider how transforms might affect the device-font-size
  // that webrender uses (and clamps).
  // But it should be near enough for practical purposes; the limitations just
  // mean we might sometimes end up with webrender still applying some bitmap
  // scaling, or bail out when we didn't really need to.
  constexpr float kWebRenderFontSizeLimit = 320.0;
  f->EnsureTextRun(nsTextFrame::eInflated);
  gfxTextRun* textRun = f->GetTextRun(nsTextFrame::eInflated);
  if (textRun &&
      textRun->GetFontGroup()->GetStyle()->size > kWebRenderFontSizeLimit) {
    return Err("font size exceeds the WebRender glyph rasterization limit");
  }

  gfx::Point deviceOffset =
      LayoutDevicePoint::FromAppUnits(bounds.TopLeft(), appUnitsPerDevPixel)
          .ToUnknownPoint();

  // Clipping the bounds to the PaintRect (factoring in what's covered by parent
  // frames) lets us early reject a bunch of things.
  nsRect visible = mVisibleRect;

  // Add the "source rect" area from which the given shadows could intersect
  // with mVisibleRect, and which therefore needs to included in the paint
  // operation, to the `visible` rect that we will use to limit the bounds of
  // what we send to the renderer.
  auto addShadowSourceToVisible = [&](Span<const StyleSimpleShadow> aShadows) {
    for (const auto& shadow : aShadows) {
      nsRect sourceRect = mVisibleRect;
      // Negate the offsets, because we're looking for the "source" rect that
      // could cast a shadow into the visible rect, rather than a "target" area
      // onto which the visible rect would cast a shadow.
      sourceRect.MoveBy(-shadow.horizontal.ToAppUnits(),
                        -shadow.vertical.ToAppUnits());
      // Inflate to account for the shadow blur.
      sourceRect.Inflate(nsContextBoxBlur::GetBlurRadiusMargin(
          shadow.blur.ToAppUnits(), appUnitsPerDevPixel));
      visible.OrWith(sourceRect);
    }
  };

  // Shadows can translate things back into view, so we enlarge the notional
  // "visible" rect to ensure we don't skip painting relevant parts that might
  // cast a shadow within the visible area.
  addShadowSourceToVisible(f->StyleText()->mTextShadow.AsSpan());

  // Similarly for shadows that may be cast by selection pseudo-elements
  // (::selection, ::target-text) and custom highlights (::highlight()).
  if (f->IsSelected()) {
    nsTextPaintStyle textPaint(f);
    UniquePtr<SelectionDetails> details = f->GetSelectionDetails();
    for (const auto* sd = details.get(); sd; sd = sd->mNext.get()) {
      Span<const StyleSimpleShadow> shadows = f->GetSelectionTextShadow(
          sd->mSelectionType, textPaint, sd->mHighlightData.mHighlightName);
      addShadowSourceToVisible(shadows);
    }
  }

  // Inflate a little extra to allow for potential antialiasing "blur".
  visible.Inflate(3 * appUnitsPerDevPixel);
  bounds = bounds.Intersect(visible);

  gfxContext* textDrawer = aBuilder.GetTextContext(aResources, aSc, aManager,
                                                   this, bounds, deviceOffset);

  nsRect boundsRelativeToSelf = bounds - ToReferenceFrame();

  ContainerTimingHelpers::MaybeProcessPaintForContainer(f->GetContent(), f,
                                                        boundsRelativeToSelf);

  LCPTextFrameHelper::MaybeUnionTextFrame(f, boundsRelativeToSelf);

  RenderToContext(textDrawer, aDisplayListBuilder, mVisibleRect,
                  aBuilder.GetInheritedOpacity(), true);
  if (!textDrawer->GetTextDrawer()->Finish()) {
    return Err("text drawer could not emit all glyph commands");
  }

  return Ok();
}

void nsDisplayText::RenderToContext(gfxContext* aCtx,
                                    nsDisplayListBuilder* aBuilder,
                                    const nsRect& aVisibleRect, float aOpacity,
                                    bool aIsRecording) {
  nsTextFrame* f = static_cast<nsTextFrame*>(mFrame);

  // Add 1 pixel of dirty area around mVisibleRect to allow us to paint
  // antialiased pixels beyond the measured text extents.
  // This is temporary until we do this in the actual calculation of text
  // extents.
  auto A2D = mFrame->PresContext()->AppUnitsPerDevPixel();
  LayoutDeviceRect extraVisible =
      LayoutDeviceRect::FromAppUnits(aVisibleRect, A2D);
  extraVisible.Inflate(1);

  gfxRect pixelVisible(extraVisible.x, extraVisible.y, extraVisible.width,
                       extraVisible.height);
  pixelVisible.Inflate(2);
  pixelVisible.RoundOut();

  gfxClipAutoSaveRestore autoSaveClip(aCtx);
  if (!aBuilder->IsForGenerateGlyphMask() && !aIsRecording) {
    autoSaveClip.Clip(pixelVisible);
  }

  NS_ASSERTION(mVisIStartEdge >= 0, "illegal start edge");
  NS_ASSERTION(mVisIEndEdge >= 0, "illegal end edge");

  gfxContextMatrixAutoSaveRestore matrixSR;

  nsPoint framePt = ToReferenceFrame();
  if (f->Style()->IsTextCombined()) {
    auto [offset, scale] = f->GetTextCombineOffsetAndScale();
    gfxTextRun* textRun = f->GetTextRun(nsTextFrame::eInflated);
    bool rtl = textRun && textRun->IsRightToLeft();
    if (rtl) {
      framePt.x -= offset;
    } else {
      framePt.x += offset;
    }
    if (scale != 1.0f) {
      if (auto* textDrawer = aCtx->GetTextDrawer()) {
        // WebRender doesn't support scaling text like this yet
        textDrawer->FoundUnsupportedFeature();
        return;
      }
      matrixSR.SetContext(aCtx);
      // Setup matrix to compress text for text-combine-upright if
      // necessary. This is done here because we want selection be
      // compressed at the same time as text.
      gfxPoint pt = nsLayoutUtils::PointToGfxPoint(framePt, A2D);
      if (rtl) {
        pt.x += gfxFloat(f->GetSize().width) / A2D;
      }
      gfxMatrix mat = aCtx->CurrentMatrixDouble()
                          .PreTranslate(pt)
                          .PreScale(scale, 1.0)
                          .PreTranslate(-pt);
      aCtx->SetMatrixDouble(mat);
    }
  }
  nsTextFrame::PaintTextParams params(aCtx);
  params.framePt = gfx::Point(framePt.x, framePt.y);
  params.dirtyRect = extraVisible;

  if (aBuilder->IsForGenerateGlyphMask()) {
    params.state = nsTextFrame::PaintTextParams::GenerateTextMask;
  } else {
    params.state = nsTextFrame::PaintTextParams::PaintText;
  }

  imgDrawingParams imgParams(aBuilder->GetImageDecodeFlags());
  f->PaintText(params, mVisIStartEdge, mVisIEndEdge, ToReferenceFrame(),
               f->IsSelected(), imgParams, aOpacity);
}

// This could go to nsDisplayListInvalidation.h, but
// |nsTextFrame::TextDecorations| requires including of nsTextFrame.h which
// would produce circular dependencies.
class nsDisplayTextGeometry : public nsDisplayItemGenericGeometry {
 public:
  nsDisplayTextGeometry(nsDisplayText* aItem, nsDisplayListBuilder* aBuilder)
      : nsDisplayItemGenericGeometry(aItem, aBuilder),
        mVisIStartEdge(aItem->VisIStartEdge()),
        mVisIEndEdge(aItem->VisIEndEdge()) {
    nsTextFrame* f = static_cast<nsTextFrame*>(aItem->Frame());
    f->GetTextDecorations(f->PresContext(), nsTextFrame::eResolvedColors,
                          mDecorations);
  }

  /**
   * We store the computed text decorations here since they are
   * computed using style data from parent frames. Any changes to these
   * styles will only invalidate the parent frame and not this frame.
   */
  nsTextFrame::TextDecorations mDecorations;
  nscoord mVisIStartEdge;
  nscoord mVisIEndEdge;
};

nsDisplayItemGeometry* nsDisplayText::AllocateGeometry(
    nsDisplayListBuilder* aBuilder) {
  return new nsDisplayTextGeometry(this, aBuilder);
}

void nsDisplayText::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const nsDisplayTextGeometry* geometry =
      static_cast<const nsDisplayTextGeometry*>(aGeometry);
  nsTextFrame* f = static_cast<nsTextFrame*>(mFrame);

  nsTextFrame::TextDecorations decorations;
  f->GetTextDecorations(f->PresContext(), nsTextFrame::eResolvedColors,
                        decorations);

  bool snap;
  const nsRect& newRect = geometry->mBounds;
  nsRect oldRect = GetBounds(aBuilder, &snap);
  if (decorations != geometry->mDecorations ||
      mVisIStartEdge != geometry->mVisIStartEdge ||
      mVisIEndEdge != geometry->mVisIEndEdge ||
      !oldRect.IsEqualInterior(newRect) ||
      !geometry->mBorderRect.IsEqualInterior(GetBorderRect())) {
    aInvalidRegion->Or(oldRect, newRect);
  }
}

void nsDisplayText::WriteDebugInfo(std::stringstream& aStream) {
#ifdef DEBUG
  aStream << " (\"";

  nsTextFrame* f = static_cast<nsTextFrame*>(mFrame);
  nsCString buf;
  f->ToCString(buf);

  aStream << buf.get() << "\")";
#endif
}

nsDisplayEffectsBase::nsDisplayEffectsBase(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, bool aClearClipChain)
    : nsDisplayWrapList(aBuilder, aFrame, aList, aActiveScrolledRoot,
                        aContainerASRType, aClearClipChain) {
  MOZ_COUNT_CTOR(nsDisplayEffectsBase);
}

nsDisplayEffectsBase::nsDisplayEffectsBase(nsDisplayListBuilder* aBuilder,
                                           nsIFrame* aFrame,
                                           nsDisplayList* aList)
    : nsDisplayWrapList(aBuilder, aFrame, aList) {
  MOZ_COUNT_CTOR(nsDisplayEffectsBase);
}

nsRegion nsDisplayEffectsBase::GetOpaqueRegion(nsDisplayListBuilder* aBuilder,
                                               bool* aSnap) const {
  *aSnap = false;
  return nsRegion();
}

void nsDisplayEffectsBase::HitTest(nsDisplayListBuilder* aBuilder,
                                   const nsRect& aRect, HitTestState* aState,
                                   nsTArray<nsIFrame*>* aOutFrames) {
  nsPoint rectCenter(aRect.x + aRect.width / 2, aRect.y + aRect.height / 2);
  if (SVGIntegrationUtils::HitTestFrameForEffects(
          mFrame, rectCenter - ToReferenceFrame())) {
    mList.HitTest(aBuilder, aRect, aState, aOutFrames);
  }
}

gfxRect nsDisplayEffectsBase::BBoxInUserSpace() const {
  return SVGUtils::GetBBox(mFrame);
}

CSSPoint nsDisplayEffectsBase::UserSpaceOffset() const {
  return SVGUtils::FrameSpaceInCSSPxToUserSpaceOffset(mFrame);
}

void nsDisplayEffectsBase::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  const auto* geometry =
      static_cast<const nsDisplaySVGEffectGeometry*>(aGeometry);
  bool snap;
  nsRect bounds = GetBounds(aBuilder, &snap);
  if (geometry->mFrameOffsetToReferenceFrame != ToReferenceFrame() ||
      geometry->mUserSpaceOffset != UserSpaceOffset() ||
      !geometry->mBBox.IsEqualInterior(BBoxInUserSpace())) {
    // Filter and mask output can depend on the location of the frame's user
    // space and on the frame's BBox. We need to invalidate if either of these
    // change relative to the reference frame.
    // Invalidations from our inactive layer manager are not enough to catch
    // some of these cases because filters can produce output even if there's
    // nothing in the filter input.
    aInvalidRegion->Or(bounds, geometry->mBounds);
  }
}

bool nsDisplayEffectsBase::ValidateSVGFrame() {
  if (mFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    ISVGDisplayableFrame* svgFrame = do_QueryFrame(mFrame);
    if (!svgFrame) {
      return false;
    }
    if (auto* svgElement = SVGElement::FromNode(mFrame->GetContent())) {
      // The SVG spec says only to draw filters if the element
      // has valid dimensions.
      return svgElement->HasValidDimensions();
    }
    return false;
  }

  return true;
}

using PaintFramesParams = SVGIntegrationUtils::PaintFramesParams;

static void ComputeMaskGeometry(PaintFramesParams& aParams) {
  // Properties are added lazily and may have been removed by a restyle, so
  // make sure all applicable ones are set again.
  nsIFrame* firstFrame =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(aParams.frame);

  const nsStyleSVGReset* svgReset = firstFrame->StyleSVGReset();

  nsTArray<SVGMaskFrame*> maskFrames;
  // XXX check return value?
  SVGObserverUtils::GetAndObserveMasks(firstFrame, &maskFrames);

  if (maskFrames.Length() == 0) {
    return;
  }

  gfxContext& ctx = aParams.ctx;
  nsIFrame* frame = aParams.frame;

  nsPoint offsetToUserSpace =
      nsLayoutUtils::ComputeOffsetToUserSpace(aParams.builder, aParams.frame);

  auto cssToDevScale = frame->PresContext()->CSSToDevPixelScale();
  int32_t appUnitsPerDevPixel = frame->PresContext()->AppUnitsPerDevPixel();

  gfxPoint devPixelOffsetToUserSpace =
      nsLayoutUtils::PointToGfxPoint(offsetToUserSpace, appUnitsPerDevPixel);

  gfxContextMatrixAutoSaveRestore matSR(&ctx);
  ctx.SetMatrixDouble(
      ctx.CurrentMatrixDouble().PreTranslate(devPixelOffsetToUserSpace));

  // Convert boaderArea and dirtyRect to user space.
  nsRect userSpaceBorderArea = aParams.borderArea - offsetToUserSpace;
  nsRect userSpaceDirtyRect = aParams.dirtyRect - offsetToUserSpace;

  // Union all mask layer rectangles in user space.
  LayoutDeviceRect maskInUserSpace;
  for (size_t i = 0; i < maskFrames.Length(); i++) {
    SVGMaskFrame* maskFrame = maskFrames[i];
    LayoutDeviceRect currentMaskSurfaceRect;

    if (maskFrame) {
      auto rect = maskFrame->GetMaskArea(aParams.frame);
      currentMaskSurfaceRect =
          CSSRect::FromUnknownRect(ToRect(rect)) * cssToDevScale;
    } else {
      nsCSSRendering::ImageLayerClipState clipState;
      nsCSSRendering::GetImageLayerClip(
          svgReset->mMask.mLayers[i], frame, *frame->StyleBorder(),
          userSpaceBorderArea, userSpaceDirtyRect,
          /* aWillPaintBorder = */ false, appUnitsPerDevPixel, &clipState);
      currentMaskSurfaceRect = LayoutDeviceRect::FromUnknownRect(
          ToRect(clipState.mDirtyRectInDevPx));
    }

    maskInUserSpace = maskInUserSpace.Union(currentMaskSurfaceRect);
  }

  if (!maskInUserSpace.IsEmpty()) {
    aParams.maskRect = Some(maskInUserSpace);
  } else {
    aParams.maskRect = Nothing();
  }
}

nsDisplayMasksAndClipPaths::nsDisplayMasksAndClipPaths(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame, nsDisplayList* aList,
    const ActiveScrolledRoot* aActiveScrolledRoot,
    ContainerASRType aContainerASRType, bool aWrapsBackdropFilter,
    bool aForceIsolation)
    : nsDisplayEffectsBase(aBuilder, aFrame, aList, aActiveScrolledRoot,
                           aContainerASRType, /* aClearClipChain = */ true),
      mWrapsBackdropFilter(aWrapsBackdropFilter),
      mForceIsolation(aForceIsolation) {
  MOZ_COUNT_CTOR(nsDisplayMasksAndClipPaths);

  nsPresContext* presContext = mFrame->PresContext();
  uint32_t flags =
      aBuilder->GetBackgroundPaintFlags() | nsCSSRendering::PAINTBG_MASK_IMAGE;
  const nsStyleSVGReset* svgReset = aFrame->StyleSVGReset();
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, svgReset->mMask) {
    const auto& layer = svgReset->mMask.mLayers[i];
    if (!layer.mImage.IsResolved()) {
      continue;
    }
    const nsRect& borderArea = mFrame->GetRectRelativeToSelf();
    // NOTE(emilio): We only care about the dest rect so we don't bother
    // computing a clip.
    bool isTransformedFixed = false;
    nsBackgroundLayerState state = nsCSSRendering::PrepareImageLayer(
        presContext, aFrame, flags, borderArea, borderArea, layer,
        &isTransformedFixed);
    mDestRects.AppendElement(state.mDestArea);
  }
}

static bool CanMergeDisplayMaskFrame(nsIFrame* aFrame) {
  // Do not merge items for box-decoration-break:clone elements,
  // since each box should have its own mask in that case.
  if (aFrame->StyleBorder()->mBoxDecorationBreak ==
      StyleBoxDecorationBreak::Clone) {
    return false;
  }

  // Do not merge if either frame has a mask. Continuation frames should apply
  // the mask independently (just like nsDisplayBackgroundImage).
  if (aFrame->StyleSVGReset()->HasMask()) {
    return false;
  }

  return true;
}

bool nsDisplayMasksAndClipPaths::CanMerge(const nsDisplayItem* aItem) const {
  // Items for the same content element should be merged into a single
  // compositing group.
  if (!HasDifferentFrame(aItem) || !HasSameTypeAndClip(aItem) ||
      !HasSameContent(aItem)) {
    return false;
  }

  return CanMergeDisplayMaskFrame(mFrame) &&
         CanMergeDisplayMaskFrame(aItem->Frame());
}

bool nsDisplayMasksAndClipPaths::IsValidMask() {
  if (!ValidateSVGFrame()) {
    return false;
  }

  return SVGUtils::DetermineMaskUsage(mFrame, false).UsingMaskOrClipPath();
}

bool nsDisplayMasksAndClipPaths::PaintMask(nsDisplayListBuilder* aBuilder,
                                           gfxContext* aMaskContext,
                                           bool aHandleOpacity,
                                           bool* aMaskPainted) {
  MOZ_ASSERT(aMaskContext->GetDrawTarget()->GetFormat() == SurfaceFormat::A8);

  imgDrawingParams imgParams(aBuilder->GetImageDecodeFlags());
  nsRect borderArea = nsRect(ToReferenceFrame(), mFrame->GetSize());
  PaintFramesParams params(*aMaskContext, mFrame, mBounds, borderArea, aBuilder,
                           aHandleOpacity, imgParams);
  ComputeMaskGeometry(params);
  bool maskIsComplete = false;
  bool painted = SVGIntegrationUtils::PaintMask(params, maskIsComplete);
  if (aMaskPainted) {
    *aMaskPainted = painted;
  }

  return maskIsComplete &&
         (imgParams.result == ImgDrawResult::SUCCESS ||
          imgParams.result == ImgDrawResult::SUCCESS_NOT_COMPLETE ||
          imgParams.result == ImgDrawResult::WRONG_SIZE);
}

void nsDisplayMasksAndClipPaths::ComputeInvalidationRegion(
    nsDisplayListBuilder* aBuilder, const nsDisplayItemGeometry* aGeometry,
    nsRegion* aInvalidRegion) const {
  nsDisplayEffectsBase::ComputeInvalidationRegion(aBuilder, aGeometry,
                                                  aInvalidRegion);

  const auto* geometry =
      static_cast<const nsDisplayMasksAndClipPathsGeometry*>(aGeometry);
  bool snap;
  nsRect bounds = GetBounds(aBuilder, &snap);

  if (mDestRects.Length() != geometry->mDestRects.Length()) {
    aInvalidRegion->Or(bounds, geometry->mBounds);
  } else {
    for (size_t i = 0; i < mDestRects.Length(); i++) {
      if (!mDestRects[i].IsEqualInterior(geometry->mDestRects[i])) {
        aInvalidRegion->Or(bounds, geometry->mBounds);
        break;
      }
    }
  }
}

void nsDisplayMasksAndClipPaths::PaintWithContentsPaintCallback(
    nsDisplayListBuilder* aBuilder, gfxContext* aCtx,
    const std::function<void()>& aPaintChildren) {
  // Clip the drawing target by mVisibleRect, which contains the visible
  // region of the target frame and its out-of-flow and inflow descendants.
  Rect bounds = NSRectToRect(GetPaintRect(aBuilder, aCtx),
                             mFrame->PresContext()->AppUnitsPerDevPixel());
  bounds.RoundOut();
  gfxClipAutoSaveRestore autoSaveClip(aCtx);
  autoSaveClip.Clip(bounds);

  imgDrawingParams imgParams(aBuilder->GetImageDecodeFlags());
  nsRect borderArea = nsRect(ToReferenceFrame(), mFrame->GetSize());
  PaintFramesParams params(*aCtx, mFrame, GetPaintRect(aBuilder, aCtx),
                           borderArea, aBuilder, false, imgParams);

  ComputeMaskGeometry(params);

  SVGIntegrationUtils::PaintMaskAndClipPath(params, aPaintChildren);
}

void nsDisplayMasksAndClipPaths::Paint(nsDisplayListBuilder* aBuilder,
                                       gfxContext* aCtx) {
  if (!IsValidMask()) {
    return;
  }
  PaintWithContentsPaintCallback(aBuilder, aCtx, [&] {
    GetChildren()->Paint(aBuilder, aCtx,
                         mFrame->PresContext()->AppUnitsPerDevPixel());
  });
}

// Records the commands of a path, to define a WebRender path clip with them.
class WrPathRecorder final : public gfx::PathBuilder {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(WrPathRecorder, override)

  WrPathRecorder() : PathBuilder(gfx::FillRule::FILL_WINDING) {}

  void MoveTo(const gfx::Point& aPoint) override {
    EndSubPath();
    BeginSubPath(aPoint);
  }

  void LineTo(const gfx::Point& aPoint) override {
    EnsureSubPath();
    mCommands.AppendElement(wr::PathCommand::LineTo);
    PushPoint(aPoint);
    mCurrentPoint = aPoint;
  }

  void BezierTo(const gfx::Point& aCP1, const gfx::Point& aCP2,
                const gfx::Point& aCP3) override {
    EnsureSubPath();
    mCommands.AppendElement(wr::PathCommand::CubicTo);
    PushPoint(aCP1);
    PushPoint(aCP2);
    PushPoint(aCP3);
    mCurrentPoint = aCP3;
  }

  void QuadraticBezierTo(const gfx::Point& aCP1,
                         const gfx::Point& aCP2) override {
    EnsureSubPath();
    mCommands.AppendElement(wr::PathCommand::QuadraticTo);
    PushPoint(aCP1);
    PushPoint(aCP2);
    mCurrentPoint = aCP2;
  }

  void Close() override {
    if (mInSubPath) {
      mCommands.AppendElement(wr::PathCommand::Close);
      mInSubPath = false;
    }
    mCurrentPoint = mBeginPoint;
  }

  void Arc(const gfx::Point& aOrigin, float aRadius, float aStartAngle,
           float aEndAngle, bool aAntiClockwise) override {
    gfx::ArcToBezier(this, aOrigin, gfx::Size(aRadius, aRadius), aStartAngle,
                     aEndAngle, aAntiClockwise);
  }

  // The commands are read from the recorder rather than from a Path.
  already_AddRefed<gfx::Path> Finish() override {
    EndSubPath();
    return nullptr;
  }

  void Reset(gfx::FillRule aFillRule) override {
    mCommands.Clear();
    mPoints.Clear();
    mInSubPath = false;
    ResetBounds();
    mFillRule = aFillRule;
  }

  void Transform(const gfx::Matrix& aTransform) override {
    ResetBounds();
    for (auto& point : mPoints) {
      gfx::Point p = aTransform.TransformPoint(gfx::Point(point.x, point.y));
      point = wr::LayoutPoint{p.x, p.y};
      AccumulateBounds(p);
    }
  }

  gfx::BackendType GetBackendType() const override {
    return gfx::BackendType::NONE;
  }

  bool IsActive() const override { return !mCommands.IsEmpty(); }

  // Defines a clip to the inside of the recorded path. Makes the points
  // relative to the clip rect's origin in place, so that a path that only
  // moves keeps interning to the same WebRender path.
  wr::WrClipId DefineClip(wr::DisplayListBuilder& aBuilder,
                          wr::FillRule aFillRule) {
    MOZ_ASSERT(!mInSubPath, "Finish() must be called first");
    if (mPoints.IsEmpty() || !mAllFinite) {
      // Nothing is inside of an empty or non-finite path.
      return aBuilder.DefineRectClip(Nothing(), wr::LayoutRect{});
    }

    // Path clips are positioned by a rect that also clips their content.
    // Inflate it so that it does not clip the path's anti-aliasing.
    const gfx::Point origin(mMin.x - 1.0f, mMin.y - 1.0f);
    const wr::LayoutRect rect{wr::LayoutPoint{origin.x, origin.y},
                              wr::LayoutPoint{mMax.x + 1.0f, mMax.y + 1.0f}};
    for (auto& point : mPoints) {
      point.x -= origin.x;
      point.y -= origin.y;
    }
    const wr::LayoutRect aabb{
        wr::LayoutPoint{mMin.x - origin.x, mMin.y - origin.y},
        wr::LayoutPoint{mMax.x - origin.x, mMax.y - origin.y}};

    return aBuilder.DefinePathClip(mCommands, mPoints, aabb, rect, aFillRule);
  }

 private:
  void BeginSubPath(const gfx::Point& aPoint) {
    mCommands.AppendElement(wr::PathCommand::Begin);
    PushPoint(aPoint);
    mInSubPath = true;
    mBeginPoint = aPoint;
    mCurrentPoint = aPoint;
  }

  // Drawing after a Close() resumes from the closed sub-path's first point.
  void EnsureSubPath() {
    if (!mInSubPath) {
      BeginSubPath(mCurrentPoint);
    }
  }

  void EndSubPath() {
    if (mInSubPath) {
      mCommands.AppendElement(wr::PathCommand::End);
      mInSubPath = false;
    }
  }

  void PushPoint(const gfx::Point& aPoint) {
    mPoints.AppendElement(wr::LayoutPoint{aPoint.x, aPoint.y});
    AccumulateBounds(aPoint);
  }

  void AccumulateBounds(const gfx::Point& aPoint) {
    mMin.x = std::min(mMin.x, aPoint.x);
    mMin.y = std::min(mMin.y, aPoint.y);
    mMax.x = std::max(mMax.x, aPoint.x);
    mMax.y = std::max(mMax.y, aPoint.y);
    mAllFinite &= std::isfinite(aPoint.x) && std::isfinite(aPoint.y);
  }

  void ResetBounds() {
    mMin = gfx::Point(kInfinity, kInfinity);
    mMax = gfx::Point(-kInfinity, -kInfinity);
    mAllFinite = true;
  }

  static constexpr float kInfinity = std::numeric_limits<float>::infinity();

  nsTArray<wr::PathCommand> mCommands;
  nsTArray<wr::LayoutPoint> mPoints;
  gfx::Point mMin{kInfinity, kInfinity};
  gfx::Point mMax{-kInfinity, -kInfinity};
  bool mAllFinite = true;
  bool mInSubPath = false;
};

static Maybe<wr::WrClipChainId> CreateSimpleClipRegion(
    const nsDisplayMasksAndClipPaths& aDisplayItem,
    wr::DisplayListBuilder& aBuilder) {
  nsIFrame* frame = aDisplayItem.Frame();
  const auto* style = frame->StyleSVGReset();
  MOZ_ASSERT(style->HasClipPath() || style->HasMask());
  if (!SVGUtils::DetermineMaskUsage(frame, false).IsSimpleClipShape()) {
    return Nothing();
  }

  const auto& clipPath = style->mClipPath;
  auto appUnitsPerDevPixel = frame->PresContext()->AppUnitsPerDevPixel();

  if (clipPath.IsUrl()) {
    // A trivial <clipPath> whose single child is a simple shape. The shape is
    // in the clipped frame's user space, which is mapped to the reference
    // frame the same way SVGIntegrationUtils positions the mask painting.
    SVGClipPathFrame* clipPathFrame = nullptr;
    SVGObserverUtils::GetAndObserveClipPath(
        nsLayoutUtils::FirstContinuationOrIBSplitSibling(frame),
        &clipPathFrame);
    CSSRect rect;
    CSSSize radii;
    if (!clipPathFrame ||
        !clipPathFrame->GetSimpleClipShape(frame, &rect, &radii)) {
      return Nothing();
    }

    nsPoint offsetToBoundingBox =
        aDisplayItem.ToReferenceFrame() -
        SVGIntegrationUtils::GetOffsetToBoundingBox(frame);
    if (!frame->IsSVGFrame()) {
      nsPresContext* pc = frame->PresContext();
      offsetToBoundingBox =
          nsPoint(pc->RoundAppUnitsToNearestDevPixels(offsetToBoundingBox.x),
                  pc->RoundAppUnitsToNearestDevPixels(offsetToBoundingBox.y));
    }
    const auto cssToDev = frame->PresContext()->CSSToDevPixelScale();
    LayoutDevicePoint offset =
        LayoutDevicePoint::FromAppUnits(offsetToBoundingBox,
                                        appUnitsPerDevPixel) -
        SVGUtils::FrameSpaceInCSSPxToUserSpaceOffset(frame) * cssToDev;

    LayoutDeviceRect devRect = rect * cssToDev;
    devRect.MoveBy(offset);
    LayoutDeviceSize devRadii = radii * cssToDev;
    wr::LayoutRect wrRect = wr::ToLayoutRect(devRect);

    wr::WrClipId clipId{};
    if (devRadii.IsEmpty()) {
      clipId = aBuilder.DefineRectClip(Nothing(), wrRect);
    } else {
      wr::ComplexClipRegion region;
      region.rect = wrRect;
      region.radii = wr::EmptyBorderRadius();
      wr::LayoutSize r{devRadii.width, devRadii.height};
      region.radii.top_left = r;
      region.radii.top_right = r;
      region.radii.bottom_left = r;
      region.radii.bottom_right = r;
      region.inset = wr::EmptyLayoutSideOffsets();
      region.mode = wr::ClipMode::Clip;
      clipId = aBuilder.DefineRoundedRectClip(Nothing(), region);
    }
    wr::WrClipChainId clipChainId = aBuilder.DefineClipChain(
        {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());
    return Some(clipChainId);
  }

  const auto& shape = *clipPath.AsShape()._0;
  const nsRect refBox =
      nsLayoutUtils::ComputeClipPathGeometryBox(frame, clipPath.AsShape()._1);

  wr::WrClipId clipId{};

  // The reference box of an SVG frame is in its user space, which for leaf
  // frames does not include the frame's own position while ToReferenceFrame()
  // does.
  nsPoint toReferenceFrame = aDisplayItem.ToReferenceFrame();
  if (frame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    toReferenceFrame -= CSSPixel::ToAppUnits(
        SVGUtils::FrameSpaceInCSSPxToUserSpaceOffset(frame));
  }

  switch (shape.tag) {
    case StyleBasicShape::Tag::Rect: {
      const nsRect rect =
          ShapeUtils::ComputeInsetRect(shape.AsRect().rect, refBox) +
          toReferenceFrame;

      nsRectCornerRadii radii;
      if (ShapeUtils::ComputeRectRadii(shape.AsRect().round, refBox, rect,
                                       radii)) {
        clipId = aBuilder.DefineRoundedRectClip(
            Nothing(),
            wr::ToComplexClipRegion(rect, radii, appUnitsPerDevPixel));
      } else {
        clipId = aBuilder.DefineRectClip(
            Nothing(), wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(
                           rect, appUnitsPerDevPixel)));
      }

      break;
    }
    case StyleBasicShape::Tag::Ellipse:
    case StyleBasicShape::Tag::Circle: {
      nsPoint center = ShapeUtils::ComputeCircleOrEllipseCenter(shape, refBox);

      nsSize radii;
      if (shape.IsEllipse()) {
        radii = ShapeUtils::ComputeEllipseRadii(shape, center, refBox);
      } else {
        nscoord radius = ShapeUtils::ComputeCircleRadius(shape, center, refBox);
        radii = {radius, radius};
      }

      nsRect ellipseRect(
          toReferenceFrame + center - nsPoint(radii.width, radii.height),
          radii * 2);

      nsRectCornerRadii ellipseRadii;
      for (const auto corner : AllPhysicalHalfCorners()) {
        ellipseRadii[corner] =
            HalfCornerIsX(corner) ? radii.width : radii.height;
      }

      clipId = aBuilder.DefineRoundedRectClip(
          Nothing(), wr::ToComplexClipRegion(ellipseRect, ellipseRadii,
                                             appUnitsPerDevPixel));

      break;
    }
    case StyleBasicShape::Tag::PathOrShape: {
      // Same as CSSClipPathInstance::CreateClipPathPath and
      // CSSClipPathInstance::CreateClipPathShape, positioned in the reference
      // frame.
      RefPtr<WrPathRecorder> recorder = MakeRefPtr<WrPathRecorder>();
      nsPoint toUserSpace = toReferenceFrame;
      if (!frame->IsSVGFrame()) {
        // Snapped like the mask image, see ComputeEffectOffset.
        const nsPoint toBoundingBox =
            SVGIntegrationUtils::GetOffsetToBoundingBox(frame);
        const nsPoint p = toReferenceFrame - toBoundingBox;
        toUserSpace =
            nsPoint(
                frame->PresContext()->RoundAppUnitsToNearestDevPixels(p.x),
                frame->PresContext()->RoundAppUnitsToNearestDevPixels(p.y)) +
            toBoundingBox;
      }
      const gfx::Point offset =
          LayoutDevicePoint::FromAppUnits(refBox.TopLeft() + toUserSpace,
                                          appUnitsPerDevPixel)
              .ToUnknownPoint();
      const float scale =
          float(AppUnitsPerCSSPixel()) / float(appUnitsPerDevPixel);
      StyleFillRule fill;
      if (shape.AsPathOrShape().IsPath()) {
        const auto& path = shape.AsPathOrShape().AsPath();
        fill = path.fill;
        RefPtr<gfx::Path> unused = SVGPathData::BuildPath(
            path.path._0.AsSpan(), recorder, StyleStrokeLinecap::Butt, 0.0, {},
            offset, frame->Style()->EffectiveZoom().Zoom(scale));
      } else {
        // The zoom was already applied to the shape's commands.
        const auto& shapeFunction = shape.AsPathOrShape().AsShape();
        fill = shapeFunction.fill;
        RefPtr<gfx::Path> unused = SVGPathData::BuildPath(
            shapeFunction.commands.AsSpan(), recorder, StyleStrokeLinecap::Butt,
            0.0, CSSSize::FromAppUnits(refBox.Size()), offset, scale);
      }

      clipId = recorder->DefineClip(aBuilder, fill == StyleFillRule::Nonzero
                                                  ? wr::FillRule::Nonzero
                                                  : wr::FillRule::Evenodd);

      break;
    }
    default:
      // Please don't add more exceptions, try to find a way to define the clip
      // without using a mask image.
      //
      // And if you _really really_ need to add an exception, add it to
      // SVGUtils::DetermineMaskUsage
      MOZ_ASSERT_UNREACHABLE("Unhandled shape id?");
      return Nothing();
  }

  wr::WrClipChainId clipChainId = aBuilder.DefineClipChain(
      {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());

  return Some(clipChainId);
}

static void FillPolygonDataForDisplayItem(
    const nsDisplayMasksAndClipPaths& aDisplayItem,
    nsTArray<wr::LayoutPoint>& aPoints, wr::FillRule& aFillRule) {
  nsIFrame* frame = aDisplayItem.Frame();
  const auto* style = frame->StyleSVGReset();
  bool isPolygon = style->HasClipPath() && style->mClipPath.IsShape() &&
                   style->mClipPath.AsShape()._0->IsPolygon();
  if (!isPolygon) {
    return;
  }

  const auto& clipPath = style->mClipPath;
  const auto& shape = *clipPath.AsShape()._0;
  const nsRect refBox =
      nsLayoutUtils::ComputeClipPathGeometryBox(frame, clipPath.AsShape()._1);

  // We only fill polygon data for polygons that are below a complexity
  // limit.
  nsTArray<nsPoint> vertices =
      ShapeUtils::ComputePolygonVertices(shape, refBox);
  if (vertices.Length() > wr::POLYGON_CLIP_VERTEX_MAX) {
    return;
  }

  auto appUnitsPerDevPixel = frame->PresContext()->AppUnitsPerDevPixel();

  for (size_t i = 0; i < vertices.Length(); ++i) {
    wr::LayoutPoint point = wr::ToLayoutPoint(
        LayoutDevicePoint::FromAppUnits(vertices[i], appUnitsPerDevPixel));
    aPoints.AppendElement(point);
  }

  aFillRule = (shape.AsPolygon().fill == StyleFillRule::Nonzero)
                  ? wr::FillRule::Nonzero
                  : wr::FillRule::Evenodd;
}

static Maybe<wr::WrClipChainId> CreateWRClipPathAndMasks(
    nsDisplayMasksAndClipPaths* aDisplayItem, const LayoutDeviceRect& aBounds,
    wr::IpcResourceUpdateQueue& aResources, wr::DisplayListBuilder& aBuilder,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (auto clip = CreateSimpleClipRegion(*aDisplayItem, aBuilder)) {
    return clip;
  }

  Maybe<wr::ImageMask> mask = aManager->CommandBuilder().BuildWrMaskImage(
      aDisplayItem, aBuilder, aResources, aSc, aDisplayListBuilder, aBounds);
  if (!mask) {
    return Nothing();
  }

  // We couldn't create a simple clip region, but before we create an image
  // mask clip, see if we can get a polygon clip to add to it.
  nsTArray<wr::LayoutPoint> points;
  wr::FillRule fillRule = wr::FillRule::Nonzero;
  FillPolygonDataForDisplayItem(*aDisplayItem, points, fillRule);

  wr::WrClipId clipId =
      aBuilder.DefineImageMaskClip(mask.ref(), points, fillRule);

  wr::WrClipChainId clipChainId = aBuilder.DefineClipChain(
      {&clipId, 1}, aBuilder.CurrentClipChainIdIfNotRoot());

  return Some(clipChainId);
}

WebRenderCommandsResult nsDisplayMasksAndClipPaths::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  bool snap;
  auto appUnitsPerDevPixel = mFrame->PresContext()->AppUnitsPerDevPixel();
  nsRect displayBounds = GetBounds(aDisplayListBuilder, &snap);
  LayoutDeviceRect bounds =
      LayoutDeviceRect::FromAppUnits(displayBounds, appUnitsPerDevPixel);

  Maybe<wr::WrClipChainId> clip = CreateWRClipPathAndMasks(
      this, bounds, aResources, aBuilder, aSc, aManager, aDisplayListBuilder);

  float oldOpacity = aBuilder.GetInheritedOpacity();

  Maybe<StackingContextHelper> layer;
  const StackingContextHelper* sc = &aSc;
  if (clip) {
    // Create a new stacking context to attach the mask to, ensuring the mask is
    // applied to the aggregate, and not the individual elements.

    // The stacking context shouldn't have any offset.
    bounds.MoveTo(0, 0);

    Maybe<float> opacity =
        (SVGUtils::DetermineMaskUsage(mFrame, false).IsSimpleClipShape() &&
         aBuilder.GetInheritedOpacity() != 1.0f)
            ? Some(aBuilder.GetInheritedOpacity())
            : Nothing();

    wr::StackingContextParams params;
    params.clip = wr::WrStackingContextClip::ClipChain(clip->id);
    params.opacity = opacity.ptrOr(nullptr);
    if (mForceIsolation) {
      params.flags |= wr::StackingContextFlags::FORCED_ISOLATION;
    }
    if (mWrapsBackdropFilter) {
      params.flags |= wr::StackingContextFlags::WRAPS_BACKDROP_FILTER;
    }
    layer.emplace(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder, params,
                  Nothing(), bounds.Size());
    sc = layer.ptr();
  }

  aBuilder.SetInheritedOpacity(1.0f);
  const DisplayItemClipChain* oldClipChain = aBuilder.GetInheritedClipChain();
  aBuilder.SetInheritedClipChain(nullptr);
  WebRenderCommandsResult result = CreateWebRenderCommandsNewClipListOption(
      aBuilder, aResources, *sc, aManager, aDisplayListBuilder, layer.isSome());
  aBuilder.SetInheritedOpacity(oldOpacity);
  aBuilder.SetInheritedClipChain(oldClipChain);

  return result;
}

Maybe<nsRect> nsDisplayMasksAndClipPaths::GetClipWithRespectToASR(
    nsDisplayListBuilder* aBuilder, const ActiveScrolledRoot* aASR) const {
  if (const DisplayItemClip* clip =
          DisplayItemClipChain::ClipForASR(GetClipChain(), aASR)) {
    return Some(clip->GetClipRect());
  }
  // This item does not have a clip with respect to |aASR|. However, we
  // might still have finite bounds with respect to |aASR|. Check our
  // children.
  nsDisplayList* childList = GetSameCoordinateSystemChildren();
  if (childList) {
    return Some(childList->GetClippedBoundsWithRespectToASR(aBuilder, aASR));
  }
#ifdef DEBUG
  NS_ASSERTION(false, "item should have finite clip with respect to aASR");
#endif
  return Nothing();
}

#ifdef MOZ_DUMP_PAINTING
void nsDisplayMasksAndClipPaths::PrintEffects(nsACString& aTo) {
  nsIFrame* firstFrame =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(mFrame);
  bool first = true;
  aTo += " effects=(";
  SVGClipPathFrame* clipPathFrame;
  // XXX Check return value?
  SVGObserverUtils::GetAndObserveClipPath(firstFrame, &clipPathFrame);
  if (clipPathFrame) {
    if (!first) {
      aTo += ", ";
    }
    aTo += nsPrintfCString(
        "clip(%s)", clipPathFrame->IsTrivial() ? "trivial" : "non-trivial");
    first = false;
  } else if (mFrame->StyleSVGReset()->HasClipPath()) {
    if (!first) {
      aTo += ", ";
    }
    aTo += "clip(basic-shape)";
    first = false;
  }

  nsTArray<SVGMaskFrame*> masks;
  // XXX check return value?
  SVGObserverUtils::GetAndObserveMasks(firstFrame, &masks);
  if (!masks.IsEmpty() && masks[0]) {
    if (!first) {
      aTo += ", ";
    }
    aTo += "mask";
  }
  aTo += ")";
}
#endif

WebRenderCommandsResult nsDisplayBackdropFilters::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  WrFiltersHolder wrFilters;
  const ComputedStyle& style = mStyle ? *mStyle : *mFrame->Style();
  auto filterChain = style.StyleEffects()->mBackdropFilters.AsSpan();
  // Try building a CSS filter chain
  WrFiltersStatus status = SVGIntegrationUtils::CreateWebRenderCSSFilters(
      filterChain, mFrame, wrFilters);
  if (status == WrFiltersStatus::BLOB_FALLBACK) {
    // If the filters are too complex for CSS filters, try SVG filters
    auto offsetForSVGFilters =
        nsLayoutUtils::ComputeOffsetToUserSpace(aDisplayListBuilder, mFrame);
    status = SVGIntegrationUtils::BuildWebRenderFilters(
        mFrame, filterChain, StyleFilterType::BackdropFilter, wrFilters,
        offsetForSVGFilters);
  }

  if (status == WrFiltersStatus::BLOB_FALLBACK) {
    // TODO: If painting backdrop-filters on the content side is implemented,
    // consider returning an error to fall back to that.
    wrFilters = {};
  }

  if (status == WrFiltersStatus::UNSUPPORTED) {
    wrFilters = {};
  }

  nsCSSRendering::ImageLayerClipState clip;
  nsCSSRendering::GetImageLayerClip(
      style.StyleBackground()->BottomLayer(), mFrame, *style.StyleBorder(),
      mBackdropRect, mBackdropRect, false,
      mFrame->PresContext()->AppUnitsPerDevPixel(), &clip);

  LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
      mBackdropRect, mFrame->PresContext()->AppUnitsPerDevPixel());

  wr::ComplexClipRegion region =
      wr::ToComplexClipRegion(clip.mBGClipArea, clip.mRadii,
                              mFrame->PresContext()->AppUnitsPerDevPixel());

  aBuilder.PushBackdropFilter(wr::ToLayoutRect(bounds), region,
                              wrFilters.filters, wrFilters.filter_datas,
                              !BackfaceIsHidden());

  wr::StackingContextParams params;
  params.clip =
      wr::WrStackingContextClip::ClipChain(aBuilder.CurrentClipChainId());
  params.flags = wr::StackingContextFlags::FORCED_ISOLATION;
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params);

  return nsDisplayWrapList::CreateWebRenderCommands(
      aBuilder, aResources, sc, aManager, aDisplayListBuilder);
}

void nsDisplayBackdropFilters::Paint(nsDisplayListBuilder* aBuilder,
                                     gfxContext* aCtx) {
  // TODO: Implement backdrop filters
  GetChildren()->Paint(aBuilder, aCtx,
                       mFrame->PresContext()->AppUnitsPerDevPixel());
}

nsRect nsDisplayBackdropFilters::GetBounds(nsDisplayListBuilder* aBuilder,
                                           bool* aSnap) const {
  nsRect childBounds = nsDisplayWrapList::GetBounds(aBuilder, aSnap);

  *aSnap = false;

  return mBackdropRect.Union(childBounds);
}

/* static */
nsDisplayFilters::nsDisplayFilters(nsDisplayListBuilder* aBuilder,
                                   nsIFrame* aFrame, nsDisplayList* aList,
                                   nsIFrame* aStyleFrame,
                                   bool aWrapsBackdropFilter)
    : nsDisplayEffectsBase(aBuilder, aFrame, aList),
      mStyle(aFrame == aStyleFrame ? nullptr : aStyleFrame->Style()),
      mEffectsBounds(aFrame->InkOverflowRectRelativeToSelf()),
      mWrapsBackdropFilter(aWrapsBackdropFilter) {
  MOZ_COUNT_CTOR(nsDisplayFilters);
  mVisibleRect = aBuilder->GetVisibleRect() +
                 aBuilder->GetCurrentFrameOffsetToReferenceFrame();
}

void nsDisplayFilters::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  PaintWithContentsPaintCallback(aBuilder, aCtx, [&](gfxContext* aContext) {
    GetChildren()->Paint(aBuilder, aContext,
                         mFrame->PresContext()->AppUnitsPerDevPixel());
  });
}

void nsDisplayFilters::PaintWithContentsPaintCallback(
    nsDisplayListBuilder* aBuilder, gfxContext* aCtx,
    const std::function<void(gfxContext* aContext)>& aPaintChildren) {
  imgDrawingParams imgParams(aBuilder->GetImageDecodeFlags());
  nsRect borderArea = nsRect(ToReferenceFrame(), mFrame->GetSize());
  PaintFramesParams params(*aCtx, mFrame, mVisibleRect, borderArea, aBuilder,
                           false, imgParams);

  gfxPoint userSpaceToFrameSpaceOffset =
      SVGIntegrationUtils::GetOffsetToUserSpaceInDevPx(mFrame, params);

  auto filterChain = mStyle ? mStyle->StyleEffects()->mFilters.AsSpan()
                            : mFrame->StyleEffects()->mFilters.AsSpan();
  SVGIntegrationUtils::PaintFilter(
      params, filterChain,
      [&](gfxContext& aContext, imgDrawingParams&, const gfxMatrix*,
          const nsIntRect*) {
        gfxContextMatrixAutoSaveRestore autoSR(&aContext);
        aContext.SetMatrixDouble(aContext.CurrentMatrixDouble().PreTranslate(
            -userSpaceToFrameSpaceOffset));
        aPaintChildren(&aContext);
      });
}

bool nsDisplayFilters::CanCreateWebRenderCommands() const {
  return SVGIntegrationUtils::CanCreateWebRenderFiltersForFrame(mFrame);
}

WebRenderCommandsResult nsDisplayFilters::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  // The SVG spec says not to draw anything for an element without valid
  // dimensions, filter included. SVGIntegrationUtils::PaintFilter bails out for
  // the software path; this one has to do the same or a filter that paints
  // without reading its input, feFlood say, still fills the filter region.
  if (!ValidateSVGFrame()) {
    return Ok();
  }

  WrFiltersHolder wrFilters;
  const ComputedStyle& style = mStyle ? *mStyle : *mFrame->Style();
  auto filterChain = style.StyleEffects()->mFilters.AsSpan();
  // Try building a CSS filter chain
  WrFiltersStatus status = SVGIntegrationUtils::CreateWebRenderCSSFilters(
      filterChain, mFrame, wrFilters);
  if (status == WrFiltersStatus::BLOB_FALLBACK) {
    // Try building an SVG filter graph
    auto offsetForSVGFilters =
        nsLayoutUtils::ComputeOffsetToUserSpace(aDisplayListBuilder, mFrame);
    status = SVGIntegrationUtils::BuildWebRenderFilters(
        mFrame, filterChain, StyleFilterType::Filter, wrFilters,
        offsetForSVGFilters);
    if (status == WrFiltersStatus::BLOB_FALLBACK && mStyle) {
      // TODO(bug 1769223): Support fallback filters in the root code-path,
      // perhaps. For now treat it the same way as invalid filters.
      status = WrFiltersStatus::UNSUPPORTED;
    }
  }

  switch (status) {
    case WrFiltersStatus::BLOB_FALLBACK:
      // Draw using fallback.
      return Err("filter chain is too complex for WebRender");
    case WrFiltersStatus::UNSUPPORTED:
      // https://drafts.fxtf.org/filter-effects/#typedef-filter-url:
      //
      //   If the filter references a non-existent object or the referenced
      //   object is not a filter element, then the whole filter chain is
      //   ignored. No filter is applied to the object.
      //
      // Note that other engines have a weird discrepancy between SVG and HTML
      // content here, but the spec is clear.
      wrFilters = {};
      break;
    case WrFiltersStatus::DISABLED_FOR_PERFORMANCE:
      // SVG spec allows us to drop the entire filter graph if it contains too
      // many filters to render or other performance considerations.
      wrFilters = {};
      break;
    case WrFiltersStatus::CHAIN:
    case WrFiltersStatus::SVGFE:
      // Filter the image using the wrFilters produced above.
      break;
  }

  uint64_t clipChainId;
  if (wrFilters.post_filters_clip) {
    auto devPxRect = LayoutDeviceRect::FromAppUnits(
        wrFilters.post_filters_clip.value() + ToReferenceFrame(),
        mFrame->PresContext()->AppUnitsPerDevPixel());
    auto clipId =
        aBuilder.DefineRectClip(Nothing(), wr::ToLayoutRect(devPxRect));
    clipChainId = aBuilder
                      .DefineClipChain({&clipId, 1},
                                       aBuilder.CurrentClipChainIdIfNotRoot())
                      .id;
  } else {
    clipChainId = aBuilder.CurrentClipChainId();
  }
  wr::WrStackingContextClip clip =
      wr::WrStackingContextClip::ClipChain(clipChainId);

  float opacity = aBuilder.GetInheritedOpacity();
  aBuilder.SetInheritedOpacity(1.0f);
  const DisplayItemClipChain* oldClipChain = aBuilder.GetInheritedClipChain();
  aBuilder.SetInheritedClipChain(nullptr);
  wr::StackingContextParams params;
  params.mFilters = std::move(wrFilters.filters);
  params.mFilterDatas = std::move(wrFilters.filter_datas);
  params.opacity = opacity != 1.0f ? &opacity : nullptr;
  params.clip = clip;
  if (mWrapsBackdropFilter) {
    params.flags |= wr::StackingContextFlags::WRAPS_BACKDROP_FILTER;
  }
  StackingContextHelper sc(aSc, GetActiveScrolledRoot(), mFrame, this, aBuilder,
                           params);

  WebRenderCommandsResult result =
      nsDisplayEffectsBase::CreateWebRenderCommands(
          aBuilder, aResources, sc, aManager, aDisplayListBuilder);
  aBuilder.SetInheritedOpacity(opacity);
  aBuilder.SetInheritedClipChain(oldClipChain);

  return result;
}

#ifdef MOZ_DUMP_PAINTING
void nsDisplayFilters::PrintEffects(nsACString& aTo) {
  nsIFrame* firstFrame =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(mFrame);
  bool first = true;
  aTo += " effects=(";
  // We may exist for a mix of CSS filter functions and/or references to SVG
  // filters.  If we have invalid references to SVG filters then we paint
  // nothing, but otherwise we will apply one or more filters.
  if (SVGObserverUtils::GetAndObserveFilters(firstFrame, nullptr) !=
      SVGObserverUtils::ReferenceState::HasRefsSomeInvalid) {
    if (!first) {
      aTo += ", ";
    }
    aTo += "filter";
  }
  aTo += ")";
}
#endif

nsDisplaySVGWrapper::nsDisplaySVGWrapper(nsDisplayListBuilder* aBuilder,
                                         nsIFrame* aFrame, nsDisplayList* aList)
    : nsDisplayWrapList(aBuilder, aFrame, aList) {
  MOZ_COUNT_CTOR(nsDisplaySVGWrapper);
}

bool nsDisplaySVGWrapper::ShouldFlattenAway(nsDisplayListBuilder* aBuilder) {
  return !aBuilder->GetWidgetLayerManager();
}

WebRenderCommandsResult nsDisplaySVGWrapper::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  return CreateWebRenderCommandsNewClipListOption(
      aBuilder, aResources, aSc, aManager, aDisplayListBuilder, false);
}

nsDisplayForeignObject::nsDisplayForeignObject(nsDisplayListBuilder* aBuilder,
                                               nsIFrame* aFrame,
                                               nsDisplayList* aList)
    : nsDisplayWrapList(aBuilder, aFrame, aList) {
  MOZ_COUNT_CTOR(nsDisplayForeignObject);
}

bool nsDisplayForeignObject::ShouldFlattenAway(nsDisplayListBuilder* aBuilder) {
  return !aBuilder->GetWidgetLayerManager();
}

WebRenderCommandsResult nsDisplayForeignObject::CreateWebRenderCommands(
    wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc, RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  AutoRestore<bool> restoreDoGrouping(aManager->CommandBuilder().mDoGrouping);
  aManager->CommandBuilder().mDoGrouping = false;
  return CreateWebRenderCommandsNewClipListOption(
      aBuilder, aResources, aSc, aManager, aDisplayListBuilder, false);
}

void nsDisplayLink::Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) {
  auto appPerDev = mFrame->PresContext()->AppUnitsPerDevPixel();
  aCtx->GetDrawTarget()->Link(
      mLinkURI.get(), mLinkDest.get(),
      NSRectToRect(GetPaintRect(aBuilder, aCtx), appPerDev));
}

void nsDisplayDestination::Paint(nsDisplayListBuilder* aBuilder,
                                 gfxContext* aCtx) {
  auto appPerDev = mFrame->PresContext()->AppUnitsPerDevPixel();
  aCtx->GetDrawTarget()->Destination(
      mDestinationName.get(),
      NSPointToPoint(GetPaintRect(aBuilder, aCtx).TopLeft(), appPerDev));
}

void nsDisplayAccessibleId::Paint(nsDisplayListBuilder* aBuilder,
                                  gfxContext* aCtx) {
  aCtx->GetDrawTarget()->AccessibleId(mInnerWindowId, mAccId);
}

void nsDisplayListCollection::SerializeWithCorrectZOrder(
    nsDisplayList* aOutResultList, nsIContent* aContent) {
  // Sort PositionedDescendants() in CSS 'z-order' order.  The list is already
  // in content document order and SortByZOrder is a stable sort which
  // guarantees that boxes produced by the same element are placed together
  // in the sort. Consider a position:relative inline element that breaks
  // across lines and has absolutely positioned children; all the abs-pos
  // children should be z-ordered after all the boxes for the position:relative
  // element itself.
  PositionedDescendants()->SortByZOrder();

  // Now follow the rules of http://www.w3.org/TR/CSS21/zindex.html
  // 1,2: backgrounds and borders
  aOutResultList->AppendToTop(BorderBackground());
  // 3: negative z-index children.
  for (auto* item : PositionedDescendants()->TakeItems()) {
    if (item->ZIndex() < 0) {
      aOutResultList->AppendToTop(item);
    } else {
      PositionedDescendants()->AppendToTop(item);
    }
  }

  // 4: block backgrounds
  aOutResultList->AppendToTop(BlockBorderBackgrounds());
  // 5: floats
  aOutResultList->AppendToTop(Floats());
  // 7: general content
  aOutResultList->AppendToTop(Content());
  // 7.5: outlines, in content tree order. We need to sort by content order
  // because an element with outline that breaks and has children with outline
  // might have placed child outline items between its own outline items.
  // The element's outline items need to all come before any child outline
  // items.
  if (aContent) {
    Outlines()->SortByContentOrder(aContent);
  }
  aOutResultList->AppendToTop(Outlines());
  // 8, 9: non-negative z-index children
  aOutResultList->AppendToTop(PositionedDescendants());
}

uint32_t PaintTelemetry::sPaintLevel = 0;

PaintTelemetry::AutoRecordPaint::AutoRecordPaint() {
  // Don't record nested paints.
  if (sPaintLevel++ > 0) {
    return;
  }

  mStart = TimeStamp::Now();
}

PaintTelemetry::AutoRecordPaint::~AutoRecordPaint() {
  MOZ_ASSERT(sPaintLevel != 0);
  if (--sPaintLevel > 0) {
    return;
  }

  // If we're in multi-process mode, don't include paint times for the parent
  // process.
  if (gfxVars::BrowserTabsRemoteAutostart() && XRE_IsParentProcess()) {
    return;
  }

  // Record the total time.
  mozilla::glean::gfx_content::paint_time.AccumulateRawDuration(
      TimeStamp::Now() - mStart);
}

static nsIFrame* GetSelfOrPlaceholderFor(nsIFrame* aFrame) {
  if (aFrame->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
    return aFrame;
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) &&
      !aFrame->GetPrevInFlow()) {
    return aFrame->GetPlaceholderFrame();
  }

  return aFrame;
}

static nsIFrame* GetAncestorFor(nsIFrame* aFrame) {
  nsIFrame* f = GetSelfOrPlaceholderFor(aFrame);
  MOZ_ASSERT(f);
  return nsLayoutUtils::GetCrossDocParentFrameInProcess(f);
}

nsDisplayListBuilder::AutoBuildingDisplayList::AutoBuildingDisplayList(
    nsDisplayListBuilder* aBuilder, nsIFrame* aForChild,
    const nsRect& aVisibleRect, const nsRect& aDirtyRect,
    const bool aIsTransformed)
    : mBuilder(aBuilder),
      mPrevFrame(aBuilder->mCurrentFrame),
      mPrevReferenceFrame(aBuilder->mCurrentReferenceFrame),
      mPrevVisibleRect(aBuilder->mVisibleRect),
      mPrevDirtyRect(aBuilder->mDirtyRect),
      mPrevOffset(aBuilder->mCurrentOffsetToReferenceFrame),
      mPrevAdditionalOffset(aBuilder->mAdditionalOffset),
      mPrevCompositorHitTestInfo(aBuilder->mCompositorHitTestInfo),
      mPrevAncestorHasApzAwareEventHandler(
          aBuilder->mAncestorHasApzAwareEventHandler),
      mPrevBuildingInvisibleItems(aBuilder->mBuildingInvisibleItems),
      mPrevInInvalidSubtree(aBuilder->mInInvalidSubtree) {
  // If the last AutoBuildingDisplayList on the stack that we created was for
  // this same frame then we are already up to date and can skip this work (this
  // happens eg when BuildDisplayListForChild calls
  // BuildDisplayListForStackingContext).
  if (aForChild != mPrevFrame) {
    if (aIsTransformed) {
      aBuilder->mCurrentOffsetToReferenceFrame =
          aBuilder->AdditionalOffset().refOr(nsPoint());
      aBuilder->mCurrentReferenceFrame = aForChild;
    } else if (aBuilder->mCurrentFrame == aForChild->GetParent()) {
      aBuilder->mCurrentOffsetToReferenceFrame += aForChild->GetPosition();
    } else {
      aBuilder->mCurrentReferenceFrame = aBuilder->FindReferenceFrameFor(
          aForChild, &aBuilder->mCurrentOffsetToReferenceFrame);
    }
  }

  // If aForChild is being visited from a frame other than it's ancestor frame,
  // mInInvalidSubtree will need to be recalculated the slow way.
  if (aForChild == mPrevFrame || GetAncestorFor(aForChild) == mPrevFrame) {
    aBuilder->mInInvalidSubtree =
        aBuilder->mInInvalidSubtree || aForChild->IsFrameModified();
  } else {
    aBuilder->mInInvalidSubtree = AnyContentAncestorModified(aForChild);
  }

  aBuilder->mCurrentFrame = aForChild;
  aBuilder->mVisibleRect = aVisibleRect;
  aBuilder->mDirtyRect =
      aBuilder->mInInvalidSubtree ? aVisibleRect : aDirtyRect;
}

}  // namespace mozilla
