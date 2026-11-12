/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* base class #1 for rendering objects that have child lists */

#include "nsContainerFrame.h"

#include <algorithm>

#include "AnchorPositioningUtils.h"
#include "CSSAlignUtils.h"
#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLSummaryElement.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/widget/InitData.h"
#include "nsAttrValue.h"
#include "nsAttrValueInlines.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsCanvasFrame.h"
#include "nsContainerFrameInlines.h"
#include "nsDisplayList.h"
#include "nsError.h"
#include "nsFlexContainerFrame.h"
#include "nsFrameSelection.h"
#include "nsIBaseWindow.h"
#include "nsIFrameInlines.h"
#include "nsIWidget.h"
#include "nsPlaceholderFrame.h"
#include "nsPoint.h"
#include "nsPresContext.h"
#include "nsPrintfCString.h"
#include "nsRect.h"
#include "nsStyleConsts.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::layout;

using mozilla::gfx::ColorPattern;
using mozilla::gfx::DeviceColor;
using mozilla::gfx::DrawTarget;
using mozilla::gfx::Rect;
using mozilla::gfx::sRGBColor;
using mozilla::gfx::ToDeviceColor;

nsContainerFrame::~nsContainerFrame() = default;

NS_QUERYFRAME_HEAD(nsContainerFrame)
  NS_QUERYFRAME_ENTRY(nsContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSplittableFrame)

void nsContainerFrame::SetInitialChildList(ChildListID aListID,
                                           nsFrameList&& aChildList) {
#ifdef DEBUG
  nsIFrame::VerifyDirtyBitSet(aChildList);
  for (nsIFrame* f : aChildList) {
    MOZ_ASSERT(f->GetParent() == this, "Unexpected parent");
  }
#endif
  if (aListID == FrameChildListID::Principal) {
    MOZ_ASSERT(mFrames.IsEmpty(),
               "unexpected second call to SetInitialChildList");
    mFrames = std::move(aChildList);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected child list");
  }
}

static bool IsNonFloatPlaceholder(const nsIFrame* aFrame) {
  return aFrame->IsPlaceholderFrame() &&
         !aFrame->HasAnyStateBits(PLACEHOLDER_FOR_FLOAT);
}

/**
 * Returns true if adding or removing a child of aParent can't affect the
 * intrinsic sizes of aParent or its ancestors.
 *
 * That's the case for placeholders of absolutely/fixed positioned and top
 * layer frames (but not floats), unless aParent is inline-level: a placeholder
 * has no intrinsic size itself (see nsPlaceholderFrame::AddInlineMinISize),
 * but an abspos placeholder in inline content introduces a line-break
 * opportunity (see bug 1418472 in nsTextFrame.cpp), which can change the
 * min-content size of the enclosing block.
 */
static bool ChildChangeKeepsIntrinsicSizes(const nsContainerFrame* aParent,
                                           bool aIsNonFloatPlaceholder) {
  return aIsNonFloatPlaceholder && !aParent->IsLineParticipant();
}

/**
 * Returns the IntrinsicDirty value to use when aFrameList is added to
 * aParent's principal child list.
 */
static IntrinsicDirty IntrinsicDirtyForChildListChange(
    const nsContainerFrame* aParent, const nsFrameList& aFrameList) {
  for (nsIFrame* f : aFrameList) {
    if (!ChildChangeKeepsIntrinsicSizes(aParent, IsNonFloatPlaceholder(f))) {
      return IntrinsicDirty::FrameAndAncestors;
    }
  }
  return IntrinsicDirty::None;
}

void nsContainerFrame::AppendFrames(ChildListID aListID,
                                    nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal ||
                 aListID == FrameChildListID::NoReflowPrincipal,
             "unexpected child list");

  if (MOZ_UNLIKELY(aFrameList.IsEmpty())) {
    return;
  }

  const IntrinsicDirty intrinsicDirty =
      IntrinsicDirtyForChildListChange(this, aFrameList);
  DrainSelfOverflowList();  // ensure the last frame is in mFrames
  mFrames.AppendFrames(this, std::move(aFrameList));

  if (aListID != FrameChildListID::NoReflowPrincipal) {
    PresShell()->FrameNeedsReflow(this, intrinsicDirty,
                                  NS_FRAME_HAS_DIRTY_CHILDREN);
  }
}

void nsContainerFrame::InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                                    const nsLineList::iterator* aPrevFrameLine,
                                    nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal ||
                 aListID == FrameChildListID::NoReflowPrincipal,
             "unexpected child list");
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  if (MOZ_UNLIKELY(aFrameList.IsEmpty())) {
    return;
  }

  const IntrinsicDirty intrinsicDirty =
      IntrinsicDirtyForChildListChange(this, aFrameList);
  DrainSelfOverflowList();  // ensure aPrevFrame is in mFrames
  mFrames.InsertFrames(this, aPrevFrame, std::move(aFrameList));

  if (aListID != FrameChildListID::NoReflowPrincipal) {
    PresShell()->FrameNeedsReflow(this, intrinsicDirty,
                                  NS_FRAME_HAS_DIRTY_CHILDREN);
  }
}

void nsContainerFrame::RemoveFrame(DestroyContext& aContext,
                                   ChildListID aListID, nsIFrame* aOldFrame) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal ||
                 aListID == FrameChildListID::NoReflowPrincipal,
             "unexpected child list");

  AutoTArray<nsIFrame*, 10> continuations;
  {
    nsIFrame* continuation = aOldFrame;
    while (continuation) {
      continuations.AppendElement(continuation);
      continuation = continuation->GetNextContinuation();
    }
  }

  mozilla::PresShell* presShell = PresShell();
  nsContainerFrame* lastParent = nullptr;

  // Loop and destroy aOldFrame and all of its continuations.
  //
  // Request a reflow on the parent frames involved unless we were explicitly
  // told not to (FrameChildListID::NoReflowPrincipal).
  const bool generateReflowCommand =
      aListID != FrameChildListID::NoReflowPrincipal;
  const bool isNonFloatPlaceholder = IsNonFloatPlaceholder(aOldFrame);
  for (nsIFrame* continuation : Reversed(continuations)) {
    nsContainerFrame* parent = continuation->GetParent();

    // Please note that 'parent' may not actually be where 'continuation' lives.
    // We really MUST use StealFrame() and nothing else here.
    // @see nsInlineFrame::StealFrame for details.
    parent->StealFrame(continuation);
    continuation->Destroy(aContext);
    if (generateReflowCommand && parent != lastParent) {
      presShell->FrameNeedsReflow(
          parent,
          ChildChangeKeepsIntrinsicSizes(parent, isNonFloatPlaceholder)
              ? IntrinsicDirty::None
              : IntrinsicDirty::FrameAndAncestors,
          NS_FRAME_HAS_DIRTY_CHILDREN);
      lastParent = parent;
    }
  }
}

void nsContainerFrame::DestroyAbsoluteFrames(DestroyContext& aContext) {
  if (auto* absCB = GetAbsoluteContainingBlock()) {
    absCB->DestroyFrames(aContext);
    MarkAsNotAbsoluteContainingBlock();
  }
}

void nsContainerFrame::SafelyDestroyFrameListProp(
    DestroyContext& aContext, mozilla::PresShell* aPresShell,
    FrameListPropertyDescriptor aProp) {
  // Note that the last frame can be removed through another route and thus
  // delete the property -- that's why we fetch the property again before
  // removing each frame rather than fetching it once and iterating the list.
  while (nsFrameList* frameList = GetProperty(aProp)) {
    // Note: Similar to nsFrameList::DestroyFrames(), we remove the frames in
    // reverse order to avoid unnecessary updates to the first-continuation and
    // first-in-flow cache. If we delete them from front to back, updating the
    // cache has a O(n^2) time complexity.
    nsIFrame* frame = frameList->RemoveLastChild();
    if (MOZ_LIKELY(frame)) {
      frame->Destroy(aContext);
    } else {
      (void)TakeProperty(aProp);
      frameList->Delete();
      return;
    }
  }
}

void nsContainerFrame::Destroy(DestroyContext& aContext) {
  DestroyAbsoluteFrames(aContext);

  // Destroy frames on the principal child list.
  mFrames.DestroyFrames(aContext);

  // If we have any IB split siblings, clear their references to us.
  if (HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
    // Delete previous sibling's reference to me.
    if (nsIFrame* prevSib = GetProperty(nsIFrame::IBSplitPrevSibling())) {
      NS_WARNING_ASSERTION(
          this == prevSib->GetProperty(nsIFrame::IBSplitSibling()),
          "IB sibling chain is inconsistent");
      prevSib->RemoveProperty(nsIFrame::IBSplitSibling());
    }

    // Delete next sibling's reference to me.
    if (nsIFrame* nextSib = GetProperty(nsIFrame::IBSplitSibling())) {
      NS_WARNING_ASSERTION(
          this == nextSib->GetProperty(nsIFrame::IBSplitPrevSibling()),
          "IB sibling chain is inconsistent");
      nextSib->RemoveProperty(nsIFrame::IBSplitPrevSibling());
    }

#ifdef DEBUG
    // This is just so we can assert it's not set in nsIFrame::DestroyFrom.
    RemoveStateBits(NS_FRAME_PART_OF_IBSPLIT);
#endif
  }

  if (MOZ_UNLIKELY(!mProperties.IsEmpty())) {
    using T = mozilla::FrameProperties::UntypedDescriptor;
    bool hasO = false, hasOC = false, hasEOC = false;
    mProperties.ForEach([&](const T& aProp, uint64_t) {
      if (aProp == OverflowProperty()) {
        hasO = true;
      } else if (aProp == OverflowContainersProperty()) {
        hasOC = true;
      } else if (aProp == ExcessOverflowContainersProperty()) {
        hasEOC = true;
      }
      return true;
    });

    // Destroy frames on the auxiliary frame lists and delete the lists.
    mozilla::PresShell* presShell = PresShell();
    if (hasO) {
      SafelyDestroyFrameListProp(aContext, presShell, OverflowProperty());
    }

    MOZ_ASSERT(CanContainOverflowContainers() || !(hasOC || hasEOC),
               "this type of frame shouldn't have overflow containers");
    if (hasOC) {
      SafelyDestroyFrameListProp(aContext, presShell,
                                 OverflowContainersProperty());
    }
    if (hasEOC) {
      SafelyDestroyFrameListProp(aContext, presShell,
                                 ExcessOverflowContainersProperty());
    }
  }

  nsSplittableFrame::Destroy(aContext);
}

/////////////////////////////////////////////////////////////////////////////
// Child frame enumeration

const nsFrameList& nsContainerFrame::GetChildList(ChildListID aListID) const {
  // We only know about the principal child list, the overflow lists,
  // and the backdrop list.
  switch (aListID) {
    case FrameChildListID::Principal:
      return mFrames;
    case FrameChildListID::Overflow: {
      nsFrameList* list = GetOverflowFrames();
      return list ? *list : nsFrameList::EmptyList();
    }
    case FrameChildListID::OverflowContainers: {
      nsFrameList* list = GetOverflowContainers();
      return list ? *list : nsFrameList::EmptyList();
    }
    case FrameChildListID::ExcessOverflowContainers: {
      nsFrameList* list = GetExcessOverflowContainers();
      return list ? *list : nsFrameList::EmptyList();
    }
    default:
      return nsSplittableFrame::GetChildList(aListID);
  }
}

void nsContainerFrame::GetChildLists(nsTArray<ChildList>* aLists) const {
  mFrames.AppendIfNonempty(aLists, FrameChildListID::Principal);

  using T = mozilla::FrameProperties::UntypedDescriptor;
  mProperties.ForEach([this, aLists](const T& aProp, uint64_t aValue) {
    typedef const nsFrameList* L;
    if (aProp == OverflowProperty()) {
      reinterpret_cast<L>(aValue)->AppendIfNonempty(aLists,
                                                    FrameChildListID::Overflow);
    } else if (aProp == OverflowContainersProperty()) {
      MOZ_ASSERT(CanContainOverflowContainers(),
                 "found unexpected OverflowContainersProperty");
      (void)this;  // silence clang -Wunused-lambda-capture in opt builds
      reinterpret_cast<L>(aValue)->AppendIfNonempty(
          aLists, FrameChildListID::OverflowContainers);
    } else if (aProp == ExcessOverflowContainersProperty()) {
      MOZ_ASSERT(CanContainOverflowContainers(),
                 "found unexpected ExcessOverflowContainersProperty");
      (void)this;  // silence clang -Wunused-lambda-capture in opt builds
      reinterpret_cast<L>(aValue)->AppendIfNonempty(
          aLists, FrameChildListID::ExcessOverflowContainers);
    }
    return true;
  });

  nsSplittableFrame::GetChildLists(aLists);
}

/////////////////////////////////////////////////////////////////////////////
// Painting/Events

void nsContainerFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                        const nsDisplayListSet& aLists) {
  DisplayBorderBackgroundOutline(aBuilder, aLists);
  BuildDisplayListForNonBlockChildren(aBuilder, aLists);
}

void nsContainerFrame::BuildDisplayListForNonBlockChildren(
    nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists,
    DisplayChildFlags aFlags) {
  nsIFrame* kid = mFrames.FirstChild();
  if (!kid || HidesContent()) {
    return;
  }
  // Put each child's background directly onto the content list
  nsDisplayListSet set(aLists, aLists.Content());
  // The children should be in content order
  while (kid) {
    BuildDisplayListForChild(aBuilder, kid, set, aFlags);
    kid = kid->GetNextSibling();
  }
}

class nsDisplaySelectionOverlay final : public nsPaintedDisplayItem {
 public:
  nsDisplaySelectionOverlay(nsDisplayListBuilder* aBuilder, nsIFrame* aFrame,
                            const DeviceColor& aColor)
      : nsPaintedDisplayItem(aBuilder, aFrame), mColor(aColor) {
    MOZ_COUNT_CTOR(nsDisplaySelectionOverlay);
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplaySelectionOverlay)

  virtual void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override;
  WebRenderCommandsResult CreateWebRenderCommands(
      mozilla::wr::DisplayListBuilder& aBuilder,
      mozilla::wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      mozilla::layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override;
  NS_DISPLAY_DECL_NAME("SelectionOverlay", TYPE_SELECTION_OVERLAY)

  static DeviceColor ComputeColorFromSelectionStyle(const ComputedStyle&);
  static DeviceColor ApplyTransparencyIfNecessary(nscolor);

 private:
  DeviceColor mColor;
};

DeviceColor nsDisplaySelectionOverlay::ApplyTransparencyIfNecessary(
    nscolor aColor) {
  // If it has already alpha, leave it like that.
  if (NS_GET_A(aColor) != 255) {
    return ToDeviceColor(aColor);
  }

  // NOTE(emilio): Blink and WebKit do something slightly different here, and
  // blend the color with white instead, both for overlays and text backgrounds.
  auto color = sRGBColor::FromABGR(aColor);
  color.a = 0.5;
  return ToDeviceColor(color);
}

DeviceColor nsDisplaySelectionOverlay::ComputeColorFromSelectionStyle(
    const ComputedStyle& aStyle) {
  return ApplyTransparencyIfNecessary(
      aStyle.GetVisitedDependentColor(&nsStyleBackground::mBackgroundColor));
}

void nsDisplaySelectionOverlay::Paint(nsDisplayListBuilder* aBuilder,
                                      gfxContext* aCtx) {
  DrawTarget& aDrawTarget = *aCtx->GetDrawTarget();
  ColorPattern color(mColor);

  nsIntRect pxRect =
      GetPaintRect(aBuilder, aCtx)
          .ToOutsidePixels(mFrame->PresContext()->AppUnitsPerDevPixel());
  Rect rect(pxRect.x, pxRect.y, pxRect.width, pxRect.height);
  MaybeSnapToDevicePixels(rect, aDrawTarget, true);

  aDrawTarget.FillRect(rect, color);
}

WebRenderCommandsResult nsDisplaySelectionOverlay::CreateWebRenderCommands(
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  wr::LayoutRect bounds = wr::ToLayoutRect(LayoutDeviceRect::FromAppUnits(
      nsRect(ToReferenceFrame(), Frame()->GetSize()),
      mFrame->PresContext()->AppUnitsPerDevPixel()));
  aBuilder.PushRect(bounds, bounds, !BackfaceIsHidden(), false, false,
                    wr::ToColorF(mColor));
  return Ok();
}

void nsContainerFrame::DisplaySelectionOverlay(nsDisplayListBuilder* aBuilder,
                                               nsDisplayList* aList,
                                               uint16_t aContentType) {
  if (!IsSelected() || !IsVisibleForPainting()) {
    return;
  }

  int16_t displaySelection = PresShell()->GetSelectionFlags();
  if (!(displaySelection & aContentType)) {
    return;
  }

  const nsFrameSelection* frameSelection = GetConstFrameSelection();
  int16_t selectionValue = frameSelection->GetDisplaySelection();

  if (selectionValue <= nsISelectionController::SELECTION_HIDDEN) {
    return;  // selection is hidden or off
  }

  nsIContent* newContent = mContent->GetParent();

  // check to see if we are anonymous content
  // XXXbz there has GOT to be a better way of determining this!
  int32_t offset =
      newContent ? newContent->ComputeIndexOf_Deprecated(mContent) : 0;

  // look up to see what selection(s) are on this frame
  UniquePtr<SelectionDetails> details = frameSelection->LookUpSelection(
      newContent, offset, 1,
      ShouldPaintNormalSelection()
          ? nsFrameSelection::IgnoreNormalSelection::No
          : nsFrameSelection::IgnoreNormalSelection::Yes);
  if (!details) {
    return;
  }

  bool normal = false;
  AutoTArray<SelectionDetails*, 1> highlights;
  for (SelectionDetails* sd = details.get(); sd; sd = sd->mNext.get()) {
    if (sd->mSelectionType == SelectionType::eNormal) {
      normal = true;
    } else if (sd->mSelectionType == SelectionType::eHighlight) {
      highlights.AppendElement(sd);
    }
  }

  if (aContentType == nsISelectionDisplay::DISPLAY_IMAGES && !normal &&
      highlights.IsEmpty()) {
    // Don't overlay an image if it's not in the primary or a custom highlight
    // selection.
    return;
  }

  // Custom highlights paint below ::selection per spec. Sort them by priority
  // (stable sort preserves insertion order for equal priorities).
  highlights.StableSort(
      [](const SelectionDetails* a, const SelectionDetails* b) -> int {
        const int32_t pa = a->mHighlightData.mHighlight->Priority();
        const int32_t pb = b->mHighlightData.mHighlight->Priority();
        return (pa > pb) - (pa < pb);
      });

  uint16_t index = 0;
  for (const auto* sd : highlights) {
    if (RefPtr<ComputedStyle> style =
            ComputeHighlightSelectionStyle(sd->mHighlightData.mHighlightName)) {
      aList->AppendNewToTopWithIndex<nsDisplaySelectionOverlay>(
          aBuilder, this, index++,
          nsDisplaySelectionOverlay::ComputeColorFromSelectionStyle(*style));
    }
  }

  // ::selection paints on top of all custom highlights.
  if (normal) {
    DeviceColor color;
    if (RefPtr<ComputedStyle> style = ComputeSelectionStyle(selectionValue)) {
      color = nsDisplaySelectionOverlay::ComputeColorFromSelectionStyle(*style);
    } else {
      LookAndFeel::ColorID colorID;
      if (selectionValue == nsISelectionController::SELECTION_ON) {
        colorID = LookAndFeel::ColorID::Highlight;
      } else if (selectionValue ==
                 nsISelectionController::SELECTION_ATTENTION) {
        colorID = LookAndFeel::ColorID::TextSelectAttentionBackground;
      } else {
        colorID = LookAndFeel::ColorID::TextSelectDisabledBackground;
      }
      color = nsDisplaySelectionOverlay::ApplyTransparencyIfNecessary(
          LookAndFeel::Color(colorID, this, NS_RGB(255, 255, 255)));
    }
    aList->AppendNewToTopWithIndex<nsDisplaySelectionOverlay>(aBuilder, this,
                                                              index++, color);
  }
}

/* virtual */
void nsContainerFrame::ChildIsDirty(nsIFrame* aChild) {
  NS_ASSERTION(aChild->IsSubtreeDirty(), "child isn't actually dirty");

  AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
}

nsIFrame::FrameSearchResult nsContainerFrame::PeekOffsetNoAmount(
    bool aForward, int32_t* aOffset) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // Don't allow the caret to stay in an empty (leaf) container frame.
  return CONTINUE_EMPTY;
}

nsIFrame::FrameSearchResult nsContainerFrame::PeekOffsetCharacter(
    bool aForward, int32_t* aOffset, PeekOffsetCharacterOptions aOptions) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // Don't allow the caret to stay in an empty (leaf) container frame.
  return CONTINUE_EMPTY;
}

/////////////////////////////////////////////////////////////////////////////
// Helper member functions

void nsContainerFrame::ReparentFrame(nsIFrame* aFrame,
                                     nsContainerFrame* aOldParent,
                                     nsContainerFrame* aNewParent) {
  NS_ASSERTION(aOldParent == aFrame->GetParent(),
               "Parent not consistent with expectations");

  aFrame->SetParent(aNewParent);
}

void nsContainerFrame::ReparentFrames(nsFrameList& aFrameList,
                                      nsContainerFrame* aOldParent,
                                      nsContainerFrame* aNewParent) {
  for (auto* f : aFrameList) {
    ReparentFrame(f, aOldParent, aNewParent);
  }
}

void nsContainerFrame::SetSizeConstraints(nsPresContext* aPresContext,
                                          nsIWidget* aWidget,
                                          const nsSize& aMinSize,
                                          const nsSize& aMaxSize) {
  // Compute constraints in desktop pixels so they are independent of the
  // widget's current backing scale factor. Each widget converts to its
  // native coordinate system using its own scale.
  //
  // Use the pres context's nearest widget for the desktop-to-device scale
  // to match the scale used by AppUnitsToDevPixels. That widget may differ
  // from aWidget (e.g. for popups on macOS whose NSWindow is initially
  // backed by a different screen than the frame's nearest widget).
  nsIWidget* rootWidget = aPresContext->GetNearestWidget();
  const DesktopToLayoutDeviceScale desktopToDev =
      rootWidget ? rootWidget->GetDesktopToDeviceScale()
                 : aWidget->GetDesktopToDeviceScale();

  auto AppUnitsToDesktop = [&](nscoord aAppUnits) {
    return NSToIntRound(aPresContext->AppUnitsToDevPixels(aAppUnits) /
                        desktopToDev.scale);
  };

  DesktopIntSize minSize(AppUnitsToDesktop(aMinSize.width),
                         AppUnitsToDesktop(aMinSize.height));
  DesktopIntSize maxSize(aMaxSize.width == NS_UNCONSTRAINEDSIZE
                             ? NS_MAXSIZE
                             : AppUnitsToDesktop(aMaxSize.width),
                         aMaxSize.height == NS_UNCONSTRAINEDSIZE
                             ? NS_MAXSIZE
                             : AppUnitsToDesktop(aMaxSize.height));

  // MinSize has a priority over MaxSize
  if (minSize.width > maxSize.width) {
    maxSize.width = minSize.width;
  }
  if (minSize.height > maxSize.height) {
    maxSize.height = minSize.height;
  }

  widget::SizeConstraints constraints(minSize, maxSize);

  // The sizes are in inner window sizes, so convert them into outer window
  // sizes. Use a size of (200, 200) as only the difference between the inner
  // and outer size is needed. NormalSizeModeClientToWindowSizeDifference
  // reports values in aWidget's device pixels, so convert through aWidget's
  // scale rather than rootWidget's.
  const LayoutDeviceIntSize devSizeDiff =
      aWidget->NormalSizeModeClientToWindowSizeDifference();
  const DesktopIntSize sizeDiff =
      DesktopIntSize::Round(devSizeDiff / aWidget->GetDesktopToDeviceScale());
  if (constraints.mMinSize.width) {
    constraints.mMinSize.width += sizeDiff.width;
  }
  if (constraints.mMinSize.height) {
    constraints.mMinSize.height += sizeDiff.height;
  }
  if (constraints.mMaxSize.width != NS_MAXSIZE) {
    constraints.mMaxSize.width += sizeDiff.width;
  }
  if (constraints.mMaxSize.height != NS_MAXSIZE) {
    constraints.mMaxSize.height += sizeDiff.height;
  }

  aWidget->SetSizeConstraints(constraints);
}

void nsContainerFrame::DoInlineMinISize(const IntrinsicSizeInput& aInput,
                                        InlineMinISizeData* aData) {
  auto handleChildren = [&](auto frame, auto data) {
    for (nsIFrame* kid : frame->mFrames) {
      const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                        GetWritingMode());
      kid->AddInlineMinISize(kidInput, data);
    }
  };
  DoInlineIntrinsicISize(aData, handleChildren);
}

void nsContainerFrame::DoInlinePrefISize(const IntrinsicSizeInput& aInput,
                                         InlinePrefISizeData* aData) {
  auto handleChildren = [&](auto frame, auto data) {
    for (nsIFrame* kid : frame->mFrames) {
      const IntrinsicSizeInput kidInput(aInput, kid->GetWritingMode(),
                                        GetWritingMode());
      kid->AddInlinePrefISize(kidInput, data);
    }
  };
  DoInlineIntrinsicISize(aData, handleChildren);
  aData->mLineIsEmpty = false;
}

/* virtual */
LogicalSize nsContainerFrame::ComputeAutoSize(
    const SizeComputationInput& aSizingInput, WritingMode aWM,
    const LogicalSize& aCBSize, nscoord aAvailableISize,
    const LogicalSize& aMargin, const mozilla::LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  const bool isTableCaption = IsTableCaption();
  // Skip table caption, which requires special sizing - see bug 1109571.
  if (IsAbsolutelyPositionedWithDefiniteContainingBlock() && !isTableCaption) {
    return ComputeAbsolutePosAutoSize(aSizingInput, aWM, aCBSize,
                                      aAvailableISize, aMargin, aBorderPadding,
                                      aSizeOverrides, aFlags);
  }
  LogicalSize result(aWM, 0xdeadbeef, NS_UNCONSTRAINEDSIZE);
  if (aFlags.contains(ComputeSizeFlag::ShrinkWrap)) {
    // Delegate to nsIFrame::ComputeAutoSize() for computing the shrink-wrapping
    // size.
    result = nsIFrame::ComputeAutoSize(aSizingInput, aWM, aCBSize,
                                       aAvailableISize, aMargin, aBorderPadding,
                                       aSizeOverrides, aFlags);
  } else {
    result.ISize(aWM) =
        aAvailableISize - aMargin.ISize(aWM) - aBorderPadding.ISize(aWM);
  }

  if (isTableCaption) {
    // If we're a container for font size inflation, then shrink
    // wrapping inside of us should not apply font size inflation.
    AutoMaybeDisableFontInflation an(this);

    WritingMode tableWM = GetParent()->GetWritingMode();
    const IntrinsicSizeInput input(
        aSizingInput.mRenderingContext,
        Some(aCBSize.ConvertTo(GetWritingMode(), aWM)), Nothing());
    if (aWM.IsOrthogonalTo(tableWM)) {
      // For an orthogonal caption on a block-dir side of the table, shrink-wrap
      // to min-isize.
      result.ISize(aWM) = GetMinISize(input);
    } else {
      // The outer frame constrains our available isize to the isize of
      // the table.  Grow if our min-isize is bigger than that, but not
      // larger than the containing block isize.  (It would really be nice
      // to transmit that information another way, so we could grow up to
      // the table's available isize, but that's harder.)
      nscoord min = GetMinISize(input);
      if (min > aCBSize.ISize(aWM)) {
        min = aCBSize.ISize(aWM);
      }
      if (min > result.ISize(aWM)) {
        result.ISize(aWM) = min;
      }
    }
  }
  return result;
}

void nsContainerFrame::ReflowChild(
    nsIFrame* aKidFrame, nsPresContext* aPresContext,
    ReflowOutput& aDesiredSize, const ReflowInput& aReflowInput,
    const WritingMode& aWM, const LogicalPoint& aPos,
    const nsSize& aContainerSize, ReflowChildFlags aFlags,
    nsReflowStatus& aStatus, nsOverflowContinuationTracker* aTracker) {
  MOZ_ASSERT(aReflowInput.mFrame == aKidFrame, "bad reflow input");
  if (aWM.IsPhysicalRTL()) {
    NS_ASSERTION(aContainerSize.width != NS_UNCONSTRAINEDSIZE,
                 "ReflowChild with unconstrained container width!");
  }
  MOZ_ASSERT(aDesiredSize.InkOverflow() == nsRect(0, 0, 0, 0) &&
                 aDesiredSize.ScrollableOverflow() == nsRect(0, 0, 0, 0),
             "please reset the overflow areas before calling ReflowChild");

  // Position the child frame and its view if requested.
  if (ReflowChildFlags::NoMoveFrame !=
      (aFlags & ReflowChildFlags::NoMoveFrame)) {
    aKidFrame->SetPosition(aWM, aPos, aContainerSize);
  }

  // Reflow the child frame
  aKidFrame->Reflow(aPresContext, aDesiredSize, aReflowInput, aStatus);

  // If the child frame is complete, delete any next-in-flows,
  // but only if the NoDeleteNextInFlowChild flag isn't set.
  if (!aStatus.IsInlineBreakBefore() && aStatus.IsFullyComplete() &&
      !(aFlags & ReflowChildFlags::NoDeleteNextInFlowChild)) {
    if (nsIFrame* kidNextInFlow = aKidFrame->GetNextInFlow()) {
      // Remove all of the childs next-in-flows. Make sure that we ask
      // the right parent to do the removal (it's possible that the
      // parent is not this because we are executing pullup code)
      nsOverflowContinuationTracker::AutoFinish fini(aTracker, aKidFrame);
      DestroyContext context(PresShell());
      kidNextInFlow->GetParent()->DeleteNextInFlowChild(context, kidNextInFlow,
                                                        true);
    }
  }
}

// XXX temporary: hold on to a copy of the old physical version of
//    ReflowChild so that we can convert callers incrementally.
void nsContainerFrame::ReflowChild(nsIFrame* aKidFrame,
                                   nsPresContext* aPresContext,
                                   ReflowOutput& aDesiredSize,
                                   const ReflowInput& aReflowInput, nscoord aX,
                                   nscoord aY, ReflowChildFlags aFlags,
                                   nsReflowStatus& aStatus,
                                   nsOverflowContinuationTracker* aTracker) {
  MOZ_ASSERT(aReflowInput.mFrame == aKidFrame, "bad reflow input");

  // Position the child frame and its view if requested.
  if (ReflowChildFlags::NoMoveFrame !=
      (aFlags & ReflowChildFlags::NoMoveFrame)) {
    aKidFrame->SetPosition(nsPoint(aX, aY));
  }

  // Reflow the child frame
  aKidFrame->Reflow(aPresContext, aDesiredSize, aReflowInput, aStatus);

  // If the child frame is complete, delete any next-in-flows,
  // but only if the NoDeleteNextInFlowChild flag isn't set.
  if (aStatus.IsFullyComplete() &&
      !(aFlags & ReflowChildFlags::NoDeleteNextInFlowChild)) {
    if (nsIFrame* kidNextInFlow = aKidFrame->GetNextInFlow()) {
      // Remove all of the childs next-in-flows. Make sure that we ask
      // the right parent to do the removal (it's possible that the
      // parent is not this because we are executing pullup code)
      nsOverflowContinuationTracker::AutoFinish fini(aTracker, aKidFrame);
      DestroyContext context(PresShell());
      kidNextInFlow->GetParent()->DeleteNextInFlowChild(context, kidNextInFlow,
                                                        true);
    }
  }
}

void nsContainerFrame::FinishReflowChild(
    nsIFrame* aKidFrame, nsPresContext* aPresContext,
    const ReflowOutput& aDesiredSize, const ReflowInput* aReflowInput,
    const WritingMode& aWM, const LogicalPoint& aPos,
    const nsSize& aContainerSize, nsIFrame::ReflowChildFlags aFlags) {
  MOZ_ASSERT(!aReflowInput || aReflowInput->mFrame == aKidFrame);
  MOZ_ASSERT(aReflowInput || aKidFrame->IsMathMLFrame() ||
                 aKidFrame->IsTableCellFrame(),
             "aReflowInput should be passed in almost all cases");

  if (aWM.IsPhysicalRTL()) {
    NS_ASSERTION(aContainerSize.width != NS_UNCONSTRAINEDSIZE,
                 "FinishReflowChild with unconstrained container width!");
  }

  const LogicalSize convertedSize = aDesiredSize.Size(aWM);
  LogicalPoint pos(aPos);

  if (aFlags & ReflowChildFlags::ApplyRelativePositioning) {
    MOZ_ASSERT(aReflowInput, "caller must have passed reflow input");
    // ApplyRelativePositioning in right-to-left writing modes needs to know
    // the updated frame width to set the normal position correctly.
    aKidFrame->SetSize(aWM, convertedSize);

    const LogicalMargin offsets = aReflowInput->ComputedLogicalOffsets(aWM);
    ReflowInput::ApplyRelativePositioning(aKidFrame, aWM, offsets, &pos,
                                          aContainerSize);
  }

  if (ReflowChildFlags::NoMoveFrame !=
      (aFlags & ReflowChildFlags::NoMoveFrame)) {
    aKidFrame->SetRect(aWM, LogicalRect(aWM, pos, convertedSize),
                       aContainerSize);
  } else {
    aKidFrame->SetSize(aWM, convertedSize);
  }

  aKidFrame->DidReflow(aPresContext, aReflowInput);
}
#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_AMD64)
#  pragma optimize("", on)
#endif

// XXX temporary: hold on to a copy of the old physical version of
//    FinishReflowChild so that we can convert callers incrementally.
void nsContainerFrame::FinishReflowChild(nsIFrame* aKidFrame,
                                         nsPresContext* aPresContext,
                                         const ReflowOutput& aDesiredSize,
                                         const ReflowInput* aReflowInput,
                                         nscoord aX, nscoord aY,
                                         ReflowChildFlags aFlags) {
  MOZ_ASSERT(!(aFlags & ReflowChildFlags::ApplyRelativePositioning),
             "only the logical version supports ApplyRelativePositioning "
             "since ApplyRelativePositioning requires the container size");

  nsPoint pos(aX, aY);
  nsSize size(aDesiredSize.PhysicalSize());

  if (ReflowChildFlags::NoMoveFrame !=
      (aFlags & ReflowChildFlags::NoMoveFrame)) {
    aKidFrame->SetRect(nsRect(pos, size));
  } else {
    aKidFrame->SetSize(size);
  }

  aKidFrame->DidReflow(aPresContext, aReflowInput);
}

void nsContainerFrame::FinishReflowWithAbsoluteFrames(
    nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
    const ReflowInput& aReflowInput, nsReflowStatus& aStatus) {
  ReflowAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput, aStatus);
  FinishAndStoreOverflow(&aDesiredSize, aReflowInput.mStyleDisplay);
}

void nsContainerFrame::ReflowAbsoluteFrames(nsPresContext* aPresContext,
                                            ReflowOutput& aDesiredSize,
                                            const ReflowInput& aReflowInput,
                                            nsReflowStatus& aStatus) {
  auto* absoluteContainer = GetAbsoluteContainingBlock();
  if (absoluteContainer && absoluteContainer->PrepareAbsoluteFrames(this)) {
    // The containing block for the abs pos kids is formed by our padding edge.
    const auto wm = GetWritingMode();
    LogicalRect cbRect(wm, LogicalPoint(wm), aDesiredSize.Size(wm));
    cbRect.Deflate(wm, GetLogicalUsedBorder(wm).ApplySkipSides(
                           PreReflowBlockLevelLogicalSkipSides()));
    // XXX: To optimize the performance, set the flags only when the CB width or
    // height actually changes.
    AbsPosReflowFlags flags{AbsPosReflowFlag::AllowFragmentation,
                            AbsPosReflowFlag::CBWidthChanged,
                            AbsPosReflowFlag::CBHeightChanged};
    nsReflowStatus absposStatus;
    absoluteContainer->Reflow(
        this, aPresContext, aReflowInput, absposStatus,
        cbRect.GetPhysicalRect(wm, aDesiredSize.PhysicalSize()), flags,
        &aDesiredSize.mOverflowAreas);
    aStatus.MergeCompletionStatusFrom(absposStatus);
  }
}

void nsContainerFrame::ReflowOverflowContainerChildren(
    nsPresContext* aPresContext, const ReflowInput& aReflowInput,
    OverflowAreas& aOverflowRects, ReflowChildFlags aFlags,
    nsReflowStatus& aStatus, ChildFrameMerger aMergeFunc,
    Maybe<nsSize> aContainerSize) {
  MOZ_ASSERT(aPresContext, "null pointer");

  nsFrameList* overflowContainers =
      DrainExcessOverflowContainersList(aMergeFunc);
  if (!overflowContainers) {
    return;  // nothing to reflow
  }

  nsOverflowContinuationTracker tracker(this, false, false);
  bool shouldReflowAllKids = aReflowInput.ShouldReflowAllKids();

  for (nsIFrame* frame : *overflowContainers) {
    if (frame->GetPrevInFlow()->GetParent() != GetPrevInFlow()) {
      // frame's prevInFlow has moved, skip reflowing this frame;
      // it will get reflowed once it's been placed
      if (GetNextInFlow()) {
        // We report OverflowIncomplete status in this case to avoid our parent
        // deleting our next-in-flows which might destroy non-empty frames.
        nsReflowStatus status;
        status.SetOverflowIncomplete();
        aStatus.MergeCompletionStatusFrom(status);
      }
      continue;
    }

    auto ScrollableOverflowExceedsAvailableBSize =
        [this, &aReflowInput](nsIFrame* aFrame) {
          if (aReflowInput.AvailableBSize() == NS_UNCONSTRAINEDSIZE) {
            return false;
          }
          const auto parentWM = GetWritingMode();
          const nscoord scrollableOverflowRectBEnd =
              LogicalRect(parentWM,
                          aFrame->ScrollableOverflowRectRelativeToParent(),
                          GetSize())
                  .BEnd(parentWM);
          return scrollableOverflowRectBEnd > aReflowInput.AvailableBSize();
        };

    // If the available block-size has changed, or the existing scrollable
    // overflow's block-end exceeds it, we need to reflow even if the frame
    // isn't dirty.
    if (shouldReflowAllKids || frame->IsSubtreeDirty() ||
        ScrollableOverflowExceedsAvailableBSize(frame)) {
      nsIFrame* prevInFlow = frame->GetPrevInFlow();
      NS_ASSERTION(prevInFlow,
                   "overflow container frame must have a prev-in-flow");
      NS_ASSERTION(
          frame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
          "overflow container frame must have overflow container bit set");
      WritingMode wm = frame->GetWritingMode();
      // Note: aReflowInput's available inline-size is technically wrong for us
      // to hand off to children here, because it doesn't account for the space
      // that's been used for the container's margin/border/padding (and some
      // other space that a concrete container type, e.g. fieldset and grid [1],
      // might reserve before setting up the available space for their
      // children). Since we don't have a way to query the specific available
      // inline-size each container type used, nor do we know how the container
      // computes its non-overflow-container children's inline-size, we just
      // unconditionally override the frame's inline-size, so that the available
      // inline-size for the children doesn't really matter anyway.
      //
      // [1] For example, fieldset uses its computed inline-size with padding as
      // the available inline-size to reflow its inner child frame.
      // https://searchfox.org/mozilla-central/rev/04f7743d94691fa24212fb43099f9d84c3bfc890/layout/forms/nsFieldSetFrame.cpp#535-536
      const LogicalSize availSpace = aReflowInput.AvailableSize(wm);

      StyleSizeOverrides sizeOverride;
      // We override current continuation's inline-size by using the
      // prev-in-flow's inline-size since both should be the same.
      sizeOverride.mStyleISize.emplace(StyleSize::FromAppUnits(
          frame->StylePosition()->mBoxSizing == StyleBoxSizing::BorderBox
              ? prevInFlow->ISize(wm)
              : prevInFlow->ContentISize(wm)));

      if (frame->IsFlexItem()) {
        // An overflow container's block-size must be 0.
        sizeOverride.mStyleBSize.emplace(StyleSize::FromAppUnits(0));
      }
      ReflowOutput desiredSize(wm);
      ReflowInput reflowInput(aPresContext, aReflowInput, frame, availSpace,
                              Nothing(), {}, sizeOverride);
      const nsSize containerSize =
          aContainerSize ? *aContainerSize
                         : aReflowInput.AvailableSize(wm).GetPhysicalSize(wm);
      const LogicalPoint pos(wm, prevInFlow->IStart(wm, containerSize), 0);
      nsReflowStatus frameStatus;

      ReflowChild(frame, aPresContext, desiredSize, reflowInput, wm, pos,
                  containerSize, aFlags, frameStatus, &tracker);
      FinishReflowChild(frame, aPresContext, desiredSize, &reflowInput, wm, pos,
                        containerSize, aFlags);

      // Handle continuations
      if (!frameStatus.IsFullyComplete()) {
        if (frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
          // Abspos frames can't cause their parent to be incomplete,
          // only overflow incomplete.
          frameStatus.SetOverflowIncomplete();
        } else {
          NS_ASSERTION(frameStatus.IsComplete(),
                       "overflow container frames can't be incomplete, only "
                       "overflow-incomplete");
        }

        // Acquire a next-in-flow, creating it if necessary
        nsIFrame* nif = frame->GetNextInFlow();
        if (!nif) {
          NS_ASSERTION(frameStatus.NextInFlowNeedsReflow(),
                       "Someone forgot a NextInFlowNeedsReflow flag");
          nif = PresShell()->FrameConstructor()->CreateContinuingFrame(frame,
                                                                       this);
        } else if (!nif->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
          // used to be a normal next-in-flow; steal it from the child list
          nif->GetParent()->StealFrame(nif);
        }

        tracker.Insert(nif, frameStatus);
      }
      aStatus.MergeCompletionStatusFrom(frameStatus);
      // At this point it would be nice to assert
      // !frame->GetOverflowRect().IsEmpty(), but we have some unsplittable
      // frames that, when taller than availableHeight will push zero-height
      // content into a next-in-flow.
    } else {
      tracker.Skip(frame, aStatus);
      if (aReflowInput.mFloatManager) {
        nsBlockFrame::RecoverFloatsFor(frame, *aReflowInput.mFloatManager,
                                       aReflowInput.GetWritingMode(),
                                       aReflowInput.ComputedPhysicalSize());
      }
    }
    ConsiderChildOverflow(aOverflowRects, frame, OverflowAreaUnionFlags::None);
  }
}

void nsContainerFrame::DisplayOverflowContainers(
    nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists) {
  if (nsFrameList* overflowconts = GetOverflowContainers()) {
    for (nsIFrame* frame : *overflowconts) {
      BuildDisplayListForChild(aBuilder, frame, aLists);
    }
  }
}

void nsContainerFrame::DisplayAbsoluteFramesNotBuiltByPlaceholder(
    nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists) {
  for (nsIFrame* frame : GetChildList(FrameChildListID::Absolute)) {
    if (frame->HasAnyStateBits(NS_FRAME_IS_PUSHED_OUT_OF_FLOW)) {
      BuildDisplayListForChild(aBuilder, frame, aLists);
    } else if (IsTransformed() && !nsLayoutUtils::IsProperAncestorFrame(
                                      this, frame->GetPlaceholderFrame())) {
      // If this is a transformed absolute containing block and the abspos's
      // placeholder is in a different continuation's subtree, build the abspos
      // directly here so that it ends up in the correct fragment's
      // nsDisplayTransform.
      BuildDisplayListForChild(aBuilder, frame, aLists);
    }
  }
}

bool nsContainerFrame::TryRemoveFrame(FrameListPropertyDescriptor aProp,
                                      nsIFrame* aChildToRemove) {
  nsFrameList* list = GetProperty(aProp);
  if (list && list->StartRemoveFrame(aChildToRemove)) {
    // aChildToRemove *may* have been removed from this list.
    if (list->IsEmpty()) {
      (void)TakeProperty(aProp);
      list->Delete();
    }
    return true;
  }
  return false;
}

bool nsContainerFrame::MaybeStealOverflowContainerFrame(nsIFrame* aChild) {
  bool removed = false;
  if (MOZ_UNLIKELY(aChild->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER))) {
    // Try removing from the overflow container list.
    removed = TryRemoveFrame(OverflowContainersProperty(), aChild);
    if (!removed) {
      // It might be in the excess overflow container list.
      removed = TryRemoveFrame(ExcessOverflowContainersProperty(), aChild);
    }
  }
  return removed;
}

void nsContainerFrame::StealFrame(nsIFrame* aChild) {
#ifdef DEBUG
  if (!mFrames.ContainsFrame(aChild)) {
    nsFrameList* list = GetOverflowFrames();
    if (!list || !list->ContainsFrame(aChild)) {
      list = GetOverflowContainers();
      if (!list || !list->ContainsFrame(aChild)) {
        list = GetExcessOverflowContainers();
        MOZ_ASSERT(list && list->ContainsFrame(aChild),
                   "aChild isn't our child"
                   " or on a frame list not supported by StealFrame");
      }
    }
  }
#endif

  if (MaybeStealOverflowContainerFrame(aChild)) {
    return;
  }

  // NOTE nsColumnSetFrame and nsCanvasFrame have their overflow containers
  // on the normal lists so we might get here also if the frame bit
  // NS_FRAME_IS_OVERFLOW_CONTAINER is set.
  if (mFrames.StartRemoveFrame(aChild)) {
    return;
  }

  // We didn't find the child in our principal child list.
  // Maybe it's on the overflow list?
  nsFrameList* frameList = GetOverflowFrames();
  if (frameList && frameList->ContinueRemoveFrame(aChild)) {
    if (frameList->IsEmpty()) {
      DestroyOverflowList();
    }
    return;
  }

  MOZ_ASSERT_UNREACHABLE("StealFrame: can't find aChild");
}

nsFrameList nsContainerFrame::StealFramesAfter(nsIFrame* aChild) {
  NS_ASSERTION(
      !aChild || !aChild->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
      "StealFramesAfter doesn't handle overflow containers");
  NS_ASSERTION(!IsBlockFrame(), "unexpected call");

  if (!aChild) {
    return std::move(mFrames);
  }

  for (nsIFrame* f : mFrames) {
    if (f == aChild) {
      return mFrames.TakeFramesAfter(f);
    }
  }

  // We didn't find the child in the principal child list.
  // Maybe it's on the overflow list?
  if (nsFrameList* overflowFrames = GetOverflowFrames()) {
    for (nsIFrame* f : *overflowFrames) {
      if (f == aChild) {
        return mFrames.TakeFramesAfter(f);
      }
    }
  }

  NS_ERROR("StealFramesAfter: can't find aChild");
  return nsFrameList();
}

/*
 * Create a next-in-flow for aFrame. Will return the newly created
 * frame <b>if and only if</b> a new frame is created; otherwise
 * nullptr is returned.
 */
nsIFrame* nsContainerFrame::CreateNextInFlow(nsIFrame* aFrame) {
  MOZ_ASSERT(
      !IsBlockFrame(),
      "you should have called nsBlockFrame::CreateContinuationFor instead");
  MOZ_ASSERT(mFrames.ContainsFrame(aFrame), "expected an in-flow child frame");

  nsIFrame* nextInFlow = aFrame->GetNextInFlow();
  if (nullptr == nextInFlow) {
    // Create a continuation frame for the child frame and insert it
    // into our child list.
    nextInFlow =
        PresShell()->FrameConstructor()->CreateContinuingFrame(aFrame, this);
    mFrames.InsertFrame(nullptr, aFrame, nextInFlow);

    NS_FRAME_LOG(NS_FRAME_TRACE_NEW_FRAMES,
                 ("nsContainerFrame::CreateNextInFlow: frame=%p nextInFlow=%p",
                  aFrame, nextInFlow));

    return nextInFlow;
  }
  return nullptr;
}

/**
 * Remove and delete aNextInFlow and its next-in-flows. Updates the sibling and
 * flow pointers
 */
void nsContainerFrame::DeleteNextInFlowChild(DestroyContext& aContext,
                                             nsIFrame* aNextInFlow,
                                             bool aDeletingEmptyFrames) {
#ifdef DEBUG
  nsIFrame* prevInFlow = aNextInFlow->GetPrevInFlow();
#endif
  MOZ_ASSERT(prevInFlow, "bad prev-in-flow");

  // If the next-in-flow has a next-in-flow then delete it, too (and
  // delete it first).
  // Do this in a loop so we don't overflow the stack for frames
  // with very many next-in-flows
  nsIFrame* nextNextInFlow = aNextInFlow->GetNextInFlow();
  if (nextNextInFlow) {
    AutoTArray<nsIFrame*, 8> frames;
    for (nsIFrame* f = nextNextInFlow; f; f = f->GetNextInFlow()) {
      frames.AppendElement(f);
    }
    for (nsIFrame* delFrame : Reversed(frames)) {
      nsContainerFrame* parent = delFrame->GetParent();
      parent->DeleteNextInFlowChild(aContext, delFrame, aDeletingEmptyFrames);
    }
  }

  // Take the next-in-flow out of the parent's child list
  StealFrame(aNextInFlow);

#ifdef DEBUG
  if (aDeletingEmptyFrames) {
    nsLayoutUtils::AssertTreeOnlyEmptyNextInFlows(aNextInFlow);
  }
#endif

  // Delete the next-in-flow frame and its descendants. This will also
  // remove it from its next-in-flow/prev-in-flow chain.
  aNextInFlow->Destroy(aContext);

  MOZ_ASSERT(!prevInFlow->GetNextInFlow(), "non null next-in-flow");
}

void nsContainerFrame::PushChildrenToOverflow(nsIFrame* aFromChild,
                                              nsIFrame* aPrevSibling) {
  MOZ_ASSERT(aFromChild, "null pointer");
  MOZ_ASSERT(aPrevSibling, "pushing first child");
  MOZ_ASSERT(aPrevSibling->GetNextSibling() == aFromChild, "bad prev sibling");

  // Add the frames to our overflow list (let our next in flow drain
  // our overflow list when it is ready)
  SetOverflowFrames(mFrames.TakeFramesAfter(aPrevSibling));
}

bool nsContainerFrame::PushIncompleteChildren(
    const FrameHashtable& aPushedItems, const FrameHashtable& aIncompleteItems,
    const FrameHashtable& aOverflowIncompleteItems) {
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Grid / Flex containers can call this!");

  if (aPushedItems.IsEmpty() && aIncompleteItems.IsEmpty() &&
      aOverflowIncompleteItems.IsEmpty()) {
    return false;
  }

  // Iterate the children in normal document order and append them (or a NIF)
  // to one of the following frame lists according to their status.
  nsFrameList pushedList;
  nsFrameList incompleteList;
  nsFrameList overflowIncompleteList;
  auto* fc = PresShell()->FrameConstructor();
  for (nsIFrame* child = PrincipalChildList().FirstChild(); child;) {
    MOZ_ASSERT((aPushedItems.Contains(child) ? 1 : 0) +
                       (aIncompleteItems.Contains(child) ? 1 : 0) +
                       (aOverflowIncompleteItems.Contains(child) ? 1 : 0) <=
                   1,
               "child should only be in one of these sets");
    // Save the next-sibling so we can continue the loop if |child| is moved.
    nsIFrame* next = child->GetNextSibling();
    if (aPushedItems.Contains(child)) {
      MOZ_ASSERT(child->GetParent() == this);
      StealFrame(child);
      pushedList.AppendFrame(nullptr, child);
    } else if (aIncompleteItems.Contains(child)) {
      nsIFrame* childNIF = child->GetNextInFlow();
      if (!childNIF) {
        childNIF = fc->CreateContinuingFrame(child, this);
        incompleteList.AppendFrame(nullptr, childNIF);
      } else {
        auto* parent = childNIF->GetParent();
        MOZ_ASSERT(parent != this || !mFrames.ContainsFrame(childNIF),
                   "child's NIF shouldn't be in the same principal list");
        // If child's existing NIF is an overflow container, convert it to an
        // actual NIF, since now |child| has non-overflow stuff to give it.
        // Or, if it's further away then our next-in-flow, then pull it up.
        if (childNIF->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER) ||
            (parent != this && parent != GetNextInFlow())) {
          parent->StealFrame(childNIF);
          childNIF->RemoveStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
          if (parent == this) {
            incompleteList.AppendFrame(nullptr, childNIF);
          } else {
            // If childNIF already lives on the next fragment, then we
            // don't need to reparent it, since we know it's destined to end
            // up there anyway.  Just move it to its parent's overflow list.
            if (parent == GetNextInFlow()) {
              nsFrameList toMove(childNIF, childNIF);
              parent->MergeSortedOverflow(toMove);
            } else {
              ReparentFrame(childNIF, parent, this);
              incompleteList.AppendFrame(nullptr, childNIF);
            }
          }
        }
      }
    } else if (aOverflowIncompleteItems.Contains(child)) {
      nsIFrame* childNIF = child->GetNextInFlow();
      if (!childNIF) {
        childNIF = fc->CreateContinuingFrame(child, this);
        childNIF->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
        overflowIncompleteList.AppendFrame(nullptr, childNIF);
      } else {
        DebugOnly<nsContainerFrame*> lastParent = this;
        auto* nif = static_cast<nsContainerFrame*>(GetNextInFlow());
        // If child has any non-overflow-container NIFs, convert them to
        // overflow containers, since that's all |child| needs now.
        while (childNIF &&
               !childNIF->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
          auto* parent = childNIF->GetParent();
          parent->StealFrame(childNIF);
          childNIF->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
          if (parent == this) {
            overflowIncompleteList.AppendFrame(nullptr, childNIF);
          } else {
            if (!nif || parent == nif) {
              nsFrameList toMove(childNIF, childNIF);
              parent->MergeSortedExcessOverflowContainers(toMove);
            } else {
              ReparentFrame(childNIF, parent, nif);
              nsFrameList toMove(childNIF, childNIF);
              nif->MergeSortedExcessOverflowContainers(toMove);
            }
            // We only need to reparent the first childNIF (or not at all if
            // its parent is our NIF).
            nif = nullptr;
          }
          lastParent = parent;
          childNIF = childNIF->GetNextInFlow();
        }
      }
    }
    child = next;
  }

  // Merge the results into our respective overflow child lists.
  if (!pushedList.IsEmpty()) {
    MergeSortedOverflow(pushedList);
  }
  if (!incompleteList.IsEmpty()) {
    MergeSortedOverflow(incompleteList);
  }
  if (!overflowIncompleteList.IsEmpty()) {
    // If our next-in-flow already has overflow containers list, merge the
    // overflowIncompleteList into that list. Otherwise, merge it into our
    // excess overflow containers list, to be drained by our next-in-flow.
    auto* nif = static_cast<nsContainerFrame*>(GetNextInFlow());
    nsFrameList* oc = nif ? nif->GetOverflowContainers() : nullptr;
    if (oc) {
      ReparentFrames(overflowIncompleteList, this, nif);
      MergeSortedFrameLists(*oc, overflowIncompleteList, GetContent());
    } else {
      MergeSortedExcessOverflowContainers(overflowIncompleteList);
    }
  }
  return true;
}

void nsContainerFrame::NormalizeChildLists() {
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Flex / Grid containers can call this!");

  // Note: the following description uses grid container as an example. Flex
  // container is similar.
  //
  // First we gather child frames we should include in our reflow/placement,
  // i.e. overflowed children from our prev-in-flow, and pushed first-in-flow
  // children (that might now fit). It's important to note that these children
  // can be in arbitrary order vis-a-vis the current children in our lists.
  // E.g. grid items in the document order: A, B, C may be placed in the rows
  // 3, 2, 1.  Assume each row goes in a separate grid container fragment,
  // and we reflow the second fragment.  Now if C (in fragment 1) overflows,
  // we can't just prepend it to our mFrames like we usually do because that
  // would violate the document order invariant that other code depends on.
  // Similarly if we pull up child A (from fragment 3) we can't just append
  // that for the same reason.  Instead, we must sort these children into
  // our child lists.  (The sorting is trivial given that both lists are
  // already fully sorted individually - it's just a merge.)
  //
  // The invariants that we maintain are that each grid container child list
  // is sorted in the normal document order at all times, but that children
  // in different grid container continuations may be in arbitrary order.

  const auto didPushItemsBit = IsFlexContainerFrame()
                                   ? NS_STATE_FLEX_DID_PUSH_ITEMS
                                   : NS_STATE_GRID_DID_PUSH_ITEMS;
  const auto hasChildNifBit = IsFlexContainerFrame()
                                  ? NS_STATE_FLEX_HAS_CHILD_NIFS
                                  : NS_STATE_GRID_HAS_CHILD_NIFS;

  auto* prevInFlow = static_cast<nsContainerFrame*>(GetPrevInFlow());
  // Merge overflow frames from our prev-in-flow into our principal child list.
  if (prevInFlow) {
    AutoFrameListPtr overflow(prevInFlow->StealOverflowFrames());
    if (overflow) {
      ReparentFrames(*overflow, prevInFlow, this);
      MergeSortedFrameLists(mFrames, *overflow, GetContent());

      // Move trailing next-in-flows into our overflow list.
      nsFrameList continuations;
      for (nsIFrame* f = mFrames.FirstChild(); f;) {
        nsIFrame* next = f->GetNextSibling();
        nsIFrame* pif = f->GetPrevInFlow();
        if (pif && pif->GetParent() == this) {
          mFrames.RemoveFrame(f);
          continuations.AppendFrame(nullptr, f);
        }
        f = next;
      }
      MergeSortedOverflow(continuations);

      // Move prev-in-flow's excess overflow containers list into our own
      // overflow containers list. If we already have an excess overflow
      // containers list, any child in that list which doesn't have a
      // prev-in-flow in this frame is also merged into our overflow container
      // list.
      nsFrameList* overflowContainers =
          DrainExcessOverflowContainersList(MergeSortedFrameListsFor);

      // Move trailing OC next-in-flows into our excess overflow containers
      // list.
      if (overflowContainers) {
        nsFrameList moveToEOC;
        for (nsIFrame* f = overflowContainers->FirstChild(); f;) {
          nsIFrame* next = f->GetNextSibling();
          nsIFrame* pif = f->GetPrevInFlow();
          if (pif && pif->GetParent() == this) {
            overflowContainers->RemoveFrame(f);
            moveToEOC.AppendFrame(nullptr, f);
          }
          f = next;
        }
        if (overflowContainers->IsEmpty()) {
          DestroyOverflowContainers();
        }
        MergeSortedExcessOverflowContainers(moveToEOC);
      }
    }
  }

  // For each item in aItems, pull up its next-in-flow (if any), and reparent it
  // to our next-in-flow, unless its parent is already ourselves or our
  // next-in-flow (to avoid leaving a hole there).
  auto PullItemsNextInFlow = [this](const nsFrameList& aItems) {
    auto* firstNIF = static_cast<nsContainerFrame*>(GetNextInFlow());
    if (!firstNIF) {
      return;
    }
    nsFrameList childNIFs;
    nsFrameList childOCNIFs;
    for (auto* child : aItems) {
      if (auto* childNIF = child->GetNextInFlow()) {
        if (auto* parent = childNIF->GetParent();
            parent != this && parent != firstNIF) {
          parent->StealFrame(childNIF);
          ReparentFrame(childNIF, parent, firstNIF);
          if (childNIF->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
            childOCNIFs.AppendFrame(nullptr, childNIF);
          } else {
            childNIFs.AppendFrame(nullptr, childNIF);
          }
        }
      }
    }
    // Merge aItems' NIFs into our NIF's respective overflow child lists.
    firstNIF->MergeSortedOverflow(childNIFs);
    firstNIF->MergeSortedExcessOverflowContainers(childOCNIFs);
  };

  // Merge our own overflow frames into our principal child list,
  // except those that are a next-in-flow for one of our items.
  DebugOnly<bool> foundOwnPushedChild = false;
  {
    nsFrameList* ourOverflow = GetOverflowFrames();
    if (ourOverflow) {
      nsFrameList items;
      for (nsIFrame* f = ourOverflow->FirstChild(); f;) {
        nsIFrame* next = f->GetNextSibling();
        nsIFrame* pif = f->GetPrevInFlow();
        if (!pif || pif->GetParent() != this) {
          MOZ_ASSERT(f->GetParent() == this);
          ourOverflow->RemoveFrame(f);
          items.AppendFrame(nullptr, f);
          if (!pif) {
            foundOwnPushedChild = true;
          }
        }
        f = next;
      }

      if (ourOverflow->IsEmpty()) {
        DestroyOverflowList();
        ourOverflow = nullptr;
      }
      if (items.NotEmpty()) {
        PullItemsNextInFlow(items);
      }
      MergeSortedFrameLists(mFrames, items, GetContent());
    }
  }

  // Push any child next-in-flows in our principal list to OverflowList.
  if (HasAnyStateBits(hasChildNifBit)) {
    nsFrameList framesToPush;
    nsIFrame* firstChild = mFrames.FirstChild();
    // Note that we potentially modify our mFrames list as we go.
    for (auto* child = firstChild; child; child = child->GetNextSibling()) {
      if (auto* childNIF = child->GetNextInFlow()) {
        if (childNIF->GetParent() == this) {
          for (auto* c = child->GetNextSibling(); c; c = c->GetNextSibling()) {
            if (c == childNIF) {
              // child's next-in-flow is in our principal child list, push it.
              mFrames.RemoveFrame(childNIF);
              framesToPush.AppendFrame(nullptr, childNIF);
              break;
            }
          }
        }
      }
    }
    if (!framesToPush.IsEmpty()) {
      MergeSortedOverflow(framesToPush);
    }
    RemoveStateBits(hasChildNifBit);
  }

  // Pull up any first-in-flow children we might have pushed.
  if (HasAnyStateBits(didPushItemsBit)) {
    RemoveStateBits(didPushItemsBit);
    nsFrameList items;
    auto* nif = static_cast<nsContainerFrame*>(GetNextInFlow());
    DebugOnly<bool> nifNeedPushedItem = false;
    while (nif) {
      nsFrameList nifItems;
      for (nsIFrame* nifChild = nif->PrincipalChildList().FirstChild();
           nifChild;) {
        nsIFrame* next = nifChild->GetNextSibling();
        if (!nifChild->GetPrevInFlow()) {
          nif->StealFrame(nifChild);
          ReparentFrame(nifChild, nif, this);
          nifItems.AppendFrame(nullptr, nifChild);
          nifNeedPushedItem = false;
        }
        nifChild = next;
      }
      MergeSortedFrameLists(items, nifItems, GetContent());

      if (!nif->HasAnyStateBits(didPushItemsBit)) {
        MOZ_ASSERT(!nifNeedPushedItem || mDidPushItemsBitMayLie,
                   "The state bit stored in didPushItemsBit lied!");
        break;
      }
      nifNeedPushedItem = true;

      for (nsIFrame* nifChild =
               nif->GetChildList(FrameChildListID::Overflow).FirstChild();
           nifChild;) {
        nsIFrame* next = nifChild->GetNextSibling();
        if (!nifChild->GetPrevInFlow()) {
          nif->StealFrame(nifChild);
          ReparentFrame(nifChild, nif, this);
          nifItems.AppendFrame(nullptr, nifChild);
          nifNeedPushedItem = false;
        }
        nifChild = next;
      }
      MergeSortedFrameLists(items, nifItems, GetContent());

      nif->RemoveStateBits(didPushItemsBit);
      nif = static_cast<nsContainerFrame*>(nif->GetNextInFlow());
      MOZ_ASSERT(nif || !nifNeedPushedItem || mDidPushItemsBitMayLie,
                 "The state bit stored in didPushItemsBit lied!");
    }

    if (!items.IsEmpty()) {
      PullItemsNextInFlow(items);
    }

    MOZ_ASSERT(
        foundOwnPushedChild || !items.IsEmpty() || mDidPushItemsBitMayLie,
        "The state bit stored in didPushItemsBit lied!");
    MergeSortedFrameLists(mFrames, items, GetContent());
  }
}

void nsContainerFrame::NoteNewChildren(ChildListID aListID,
                                       const nsFrameList& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal, "unexpected child list");
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Flex / Grid containers can call this!");

  mozilla::PresShell* presShell = PresShell();
  const auto didPushItemsBit = IsFlexContainerFrame()
                                   ? NS_STATE_FLEX_DID_PUSH_ITEMS
                                   : NS_STATE_GRID_DID_PUSH_ITEMS;
  for (auto* pif = GetPrevInFlow(); pif; pif = pif->GetPrevInFlow()) {
    pif->AddStateBits(didPushItemsBit);
    presShell->FrameNeedsReflow(pif, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_IS_DIRTY);
  }
}

bool nsContainerFrame::MoveOverflowToChildList() {
  bool result = false;

  // Check for an overflow list with our prev-in-flow
  nsContainerFrame* prevInFlow = (nsContainerFrame*)GetPrevInFlow();
  if (nullptr != prevInFlow) {
    AutoFrameListPtr prevOverflowFrames(prevInFlow->StealOverflowFrames());
    if (prevOverflowFrames) {
      // Tables are special; they can have repeated header/footer
      // frames on mFrames at this point.
      NS_ASSERTION(mFrames.IsEmpty() || IsTableFrame(), "bad overflow list");
      mFrames.AppendFrames(this, std::move(*prevOverflowFrames));
      result = true;
    }
  }

  // It's also possible that we have an overflow list for ourselves.
  return DrainSelfOverflowList() || result;
}

void nsContainerFrame::MergeSortedOverflow(nsFrameList& aList) {
  if (aList.IsEmpty()) {
    return;
  }
  MOZ_ASSERT(
      !aList.FirstChild()->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
      "this is the wrong list to put this child frame");
  MOZ_ASSERT(aList.FirstChild()->GetParent() == this);
  nsFrameList* overflow = GetOverflowFrames();
  if (overflow) {
    MergeSortedFrameLists(*overflow, aList, GetContent());
  } else {
    SetOverflowFrames(std::move(aList));
  }
}

void nsContainerFrame::MergeSortedExcessOverflowContainers(nsFrameList& aList) {
  if (aList.IsEmpty()) {
    return;
  }
  MOZ_ASSERT(
      aList.FirstChild()->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER),
      "this is the wrong list to put this child frame");
  MOZ_ASSERT(aList.FirstChild()->GetParent() == this);
  if (nsFrameList* eoc = GetExcessOverflowContainers()) {
    MergeSortedFrameLists(*eoc, aList, GetContent());
  } else {
    SetExcessOverflowContainers(std::move(aList));
  }
}

nsIFrame* nsContainerFrame::GetFirstNonAnonBoxInSubtree(nsIFrame* aFrame) {
  while (aFrame) {
    // If aFrame isn't an anonymous container, or it's text or such, then it'll
    // do.
    if (!aFrame->Style()->IsAnonBox() ||
        PseudoStyle::IsNonElement(aFrame->Style()->GetPseudoType())) {
      break;
    }
    aFrame = aFrame->PrincipalChildList().FirstChild();
  }
  return aFrame;
}

/**
 * Is aFrame1 a prev-continuation of aFrame2?
 */
static bool IsPrevContinuationOf(nsIFrame* aFrame1, nsIFrame* aFrame2) {
  nsIFrame* prev = aFrame2;
  while ((prev = prev->GetPrevContinuation())) {
    if (prev == aFrame1) {
      return true;
    }
  }
  return false;
}

void nsContainerFrame::MergeSortedFrameLists(nsFrameList& aDest,
                                             nsFrameList& aSrc,
                                             nsIContent* aCommonAncestor) {
  // Returns a frame whose DOM node can be used for the purpose of ordering
  // aFrame among its sibling frames by DOM position. If aFrame is
  // non-anonymous, this just returns aFrame itself. Otherwise, this returns the
  // first non-anonymous descendant in aFrame's continuation chain.
  auto FrameForDOMPositionComparison = [](nsIFrame* aFrame) {
    if (!aFrame->Style()->IsAnonBox()) {
      // The usual case.
      return aFrame;
    }

    // Walk the continuation chain from the start, and return the first
    // non-anonymous descendant that we find.
    for (nsIFrame* f = aFrame->FirstContinuation(); f;
         f = f->GetNextContinuation()) {
      if (nsIFrame* nonAnonBox = GetFirstNonAnonBoxInSubtree(f)) {
        return nonAnonBox;
      }
    }

    MOZ_ASSERT_UNREACHABLE(
        "Why is there no non-anonymous descendants in the continuation chain?");
    return aFrame;
  };

  nsIFrame* dest = aDest.FirstChild();
  for (nsIFrame* src = aSrc.FirstChild(); src;) {
    if (!dest) {
      aDest.AppendFrames(nullptr, std::move(aSrc));
      break;
    }
    nsIContent* srcContent = FrameForDOMPositionComparison(src)->GetContent();
    nsIContent* destContent = FrameForDOMPositionComparison(dest)->GetContent();
    int32_t result = nsContentUtils::CompareTreePosition<TreeKind::Flat>(
        srcContent, destContent, aCommonAncestor);
    if (MOZ_UNLIKELY(result == 0)) {
      // NOTE: we get here when comparing ::before/::after for the same element.
      if (MOZ_UNLIKELY(srcContent->IsGeneratedContentContainerForBefore())) {
        if (MOZ_LIKELY(!destContent->IsGeneratedContentContainerForBefore()) ||
            ::IsPrevContinuationOf(src, dest)) {
          result = -1;
        }
      } else if (MOZ_UNLIKELY(
                     srcContent->IsGeneratedContentContainerForAfter())) {
        if (MOZ_UNLIKELY(destContent->IsGeneratedContentContainerForAfter()) &&
            ::IsPrevContinuationOf(src, dest)) {
          result = -1;
        }
      } else if (::IsPrevContinuationOf(src, dest)) {
        result = -1;
      }
    }
    if (result < 0) {
      // src should come before dest
      nsIFrame* next = src->GetNextSibling();
      aSrc.RemoveFrame(src);
      aDest.InsertFrame(nullptr, dest->GetPrevSibling(), src);
      src = next;
    } else {
      dest = dest->GetNextSibling();
    }
  }
  MOZ_ASSERT(aSrc.IsEmpty());
}

bool nsContainerFrame::MoveInlineOverflowToChildList(nsIFrame* aLineContainer) {
  MOZ_ASSERT(aLineContainer,
             "Must have line container for moving inline overflows");

  bool result = false;

  // Check for an overflow list with our prev-in-flow
  if (auto prevInFlow = static_cast<nsContainerFrame*>(GetPrevInFlow())) {
    AutoFrameListPtr prevOverflowFrames(prevInFlow->StealOverflowFrames());
    if (prevOverflowFrames) {
      // We may need to reparent floats from prev-in-flow to our line
      // container if the container has prev continuation.
      if (aLineContainer->GetPrevContinuation()) {
        ReparentFloatsForInlineChild(aLineContainer,
                                     prevOverflowFrames->FirstChild(), true);
      }
      // Prepend overflow frames to the list.
      mFrames.InsertFrames(this, nullptr, std::move(*prevOverflowFrames));
      result = true;
    }
  }

  // It's also possible that we have overflow list for ourselves.
  return DrainSelfOverflowList() || result;
}

bool nsContainerFrame::DrainSelfOverflowList() {
  AutoFrameListPtr overflowFrames(StealOverflowFrames());
  if (overflowFrames) {
    mFrames.AppendFrames(nullptr, std::move(*overflowFrames));
    return true;
  }
  return false;
}

bool nsContainerFrame::DrainAndMergeSelfOverflowList() {
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Flex / Grid containers can call this!");

  // Unlike nsContainerFrame::DrainSelfOverflowList, flex or grid containers
  // need to merge these lists so that the resulting mFrames is in document
  // content order.
  // NOTE: nsContainerFrame::AppendFrames/InsertFrames calls this method and
  // there are also direct calls from the fctor (FindAppendPrevSibling).
  AutoFrameListPtr overflowFrames(StealOverflowFrames());
  if (overflowFrames) {
    MergeSortedFrameLists(mFrames, *overflowFrames, GetContent());
    // We set a frame bit to push them again in Reflow() to avoid creating
    // multiple flex / grid items per flex / grid container fragment for the
    // same content.
    AddStateBits(IsFlexContainerFrame() ? NS_STATE_FLEX_HAS_CHILD_NIFS
                                        : NS_STATE_GRID_HAS_CHILD_NIFS);
    return true;
  }
  return false;
}

nsFrameList* nsContainerFrame::DrainExcessOverflowContainersList(
    ChildFrameMerger aMergeFunc) {
  nsFrameList* overflowContainers = GetOverflowContainers();

  // Drain excess overflow containers from our prev-in-flow.
  if (auto* prev = static_cast<nsContainerFrame*>(GetPrevInFlow())) {
    AutoFrameListPtr excessFrames(prev->StealExcessOverflowContainers());
    if (excessFrames) {
      excessFrames->ApplySetParent(this);
      if (overflowContainers) {
        // The default merge function is AppendFrames, so we use excessFrames as
        // the destination and then assign the result to overflowContainers.
        aMergeFunc(*excessFrames, *overflowContainers, this);
        *overflowContainers = std::move(*excessFrames);
      } else {
        overflowContainers = SetOverflowContainers(std::move(*excessFrames));
      }
    }
  }

  // Our own excess overflow containers from a previous reflow can still be
  // present if our next-in-flow hasn't been reflown yet.  Move any children
  // from it that don't have a continuation in this frame to the
  // OverflowContainers list.
  AutoFrameListPtr selfExcessOCFrames(StealExcessOverflowContainers());
  if (selfExcessOCFrames) {
    nsFrameList toMove;
    auto child = selfExcessOCFrames->FirstChild();
    while (child) {
      auto next = child->GetNextSibling();
      MOZ_ASSERT(child->GetPrevInFlow(),
                 "ExcessOverflowContainers frames must be continuations");
      if (child->GetPrevInFlow()->GetParent() != this) {
        selfExcessOCFrames->RemoveFrame(child);
        toMove.AppendFrame(nullptr, child);
      }
      child = next;
    }

    // If there's any remaining excess overflow containers, put them back.
    if (selfExcessOCFrames->NotEmpty()) {
      SetExcessOverflowContainers(std::move(*selfExcessOCFrames));
    }

    if (toMove.NotEmpty()) {
      if (overflowContainers) {
        aMergeFunc(*overflowContainers, toMove, this);
      } else {
        overflowContainers = SetOverflowContainers(std::move(toMove));
      }
    }
  }

  return overflowContainers;
}

nsIFrame* nsContainerFrame::GetNextInFlowChild(
    ContinuationTraversingState& aState, bool* aIsInOverflow) {
  nsContainerFrame*& nextInFlow = aState.mNextInFlow;
  while (nextInFlow) {
    // See if there is any frame in the container
    nsIFrame* frame = nextInFlow->mFrames.FirstChild();
    if (frame) {
      if (aIsInOverflow) {
        *aIsInOverflow = false;
      }
      return frame;
    }
    // No frames in the principal list, try its overflow list
    nsFrameList* overflowFrames = nextInFlow->GetOverflowFrames();
    if (overflowFrames) {
      if (aIsInOverflow) {
        *aIsInOverflow = true;
      }
      return overflowFrames->FirstChild();
    }
    nextInFlow = static_cast<nsContainerFrame*>(nextInFlow->GetNextInFlow());
  }
  return nullptr;
}

nsIFrame* nsContainerFrame::PullNextInFlowChild(
    ContinuationTraversingState& aState) {
  bool isInOverflow;
  nsIFrame* frame = GetNextInFlowChild(aState, &isInOverflow);
  if (frame) {
    nsContainerFrame* nextInFlow = aState.mNextInFlow;
    if (isInOverflow) {
      nsFrameList* overflowFrames = nextInFlow->GetOverflowFrames();
      overflowFrames->RemoveFirstChild();
      if (overflowFrames->IsEmpty()) {
        nextInFlow->DestroyOverflowList();
      }
    } else {
      nextInFlow->mFrames.RemoveFirstChild();
    }

    // Move the frame to the principal frame list of this container
    mFrames.AppendFrame(this, frame);
  }
  return frame;
}

/* static */
void nsContainerFrame::ReparentFloatsForInlineChild(nsIFrame* aOurLineContainer,
                                                    nsIFrame* aFrame,
                                                    bool aReparentSiblings) {
  // XXXbz this would be better if it took a nsFrameList or a frame
  // list slice....
  NS_ASSERTION(aOurLineContainer->GetNextContinuation() ||
                   aOurLineContainer->GetPrevContinuation(),
               "Don't call this when we have no continuation, it's a waste");
  if (!aFrame) {
    NS_ASSERTION(aReparentSiblings, "Why did we get called?");
    return;
  }

  nsBlockFrame* frameBlock = nsLayoutUtils::GetFloatContainingBlock(aFrame);
  if (!frameBlock || frameBlock == aOurLineContainer) {
    return;
  }

  nsBlockFrame* ourBlock = do_QueryFrame(aOurLineContainer);
  NS_ASSERTION(ourBlock, "Not a block, but broke vertically?");

  while (true) {
    ourBlock->ReparentFloats(aFrame, frameBlock, false);

    if (!aReparentSiblings) {
      return;
    }
    nsIFrame* next = aFrame->GetNextSibling();
    if (!next) {
      return;
    }
    if (next->GetParent() == aFrame->GetParent()) {
      aFrame = next;
      continue;
    }
    // This is paranoid and will hardly ever get hit ... but we can't actually
    // trust that the frames in the sibling chain all have the same parent,
    // because lazy reparenting may be going on. If we find a different
    // parent we need to redo our analysis.
    ReparentFloatsForInlineChild(aOurLineContainer, next, aReparentSiblings);
    return;
  }
}

bool nsContainerFrame::ResolvedOrientationIsVertical() const {
  StyleOrient orient = StyleDisplay()->mOrient;
  switch (orient) {
    case StyleOrient::Horizontal:
      return false;
    case StyleOrient::Vertical:
      return true;
    case StyleOrient::Inline:
      return GetWritingMode().IsVertical();
    case StyleOrient::Block:
      return !GetWritingMode().IsVertical();
  }
  MOZ_ASSERT_UNREACHABLE("unexpected -moz-orient value");
  return false;
}

LogicalSize nsContainerFrame::ComputeSizeWithIntrinsicDimensions(
    gfxContext* aRenderingContext, WritingMode aWM,
    const IntrinsicSize& aIntrinsicSize, const AspectRatio& aAspectRatio,
    const LogicalSize& aCBSize, const LogicalSize& aMargin,
    const LogicalSize& aBorderPadding, const StyleSizeOverrides& aSizeOverrides,
    ComputeSizeFlags aFlags) {
  const nsStylePosition* stylePos = StylePosition();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(this);
  const auto styleISize =
      aSizeOverrides.mStyleISize
          ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleISize)
          : stylePos->ISize(aWM, anchorResolutionParams);

  const auto styleBSize = [&] {
    auto styleBSizeConsideringOverrides =
        aSizeOverrides.mStyleBSize
            ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleBSize)
            : stylePos->BSize(aWM, anchorResolutionParams);
    if (styleBSizeConsideringOverrides->BehavesLikeStretchOnBlockAxis() &&
        aCBSize.BSize(aWM) != NS_UNCONSTRAINEDSIZE) {
      // We've got a 'stretch' BSize; resolve it to a length:
      nscoord stretchBSize = nsLayoutUtils::ComputeStretchSize(
          aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
          stylePos->mBoxSizing);
      // Note(dshin): This allocates.
      return AnchorResolvedSizeHelper::LengthPercentage(
          LengthPercentage::FromAppUnits(stretchBSize));
    }
    return styleBSizeConsideringOverrides;
  }();

  const auto& aspectRatio =
      aSizeOverrides.mAspectRatio ? *aSizeOverrides.mAspectRatio : aAspectRatio;

  auto* parentFrame = GetParent();
  const bool isGridItem = IsGridItem();

  // flexItemMainAxis is set if this frame is a flex item in a modern flexbox
  // layout. It indicates which logical axis (in this frame's own WM)
  // corresponds to its flex container's main axis.
  Maybe<LogicalAxis> flexItemMainAxis;
  if (IsFlexItem() && !parentFrame->HasAnyStateBits(
                          NS_STATE_FLEX_IS_EMULATING_LEGACY_WEBKIT_BOX)) {
    flexItemMainAxis = Some(nsFlexContainerFrame::IsItemInlineAxisMainAxis(this)
                                ? LogicalAxis::Inline
                                : LogicalAxis::Block);
  }

  // Handle intrinsic sizes and their interaction with
  // {min-,max-,}{width,height} according to the rules in
  // https://www.w3.org/TR/CSS22/visudet.html#min-max-widths and
  // https://drafts.csswg.org/css-sizing-3/#intrinsic-sizes

  // Note: throughout the following section of the function, I avoid
  // a * (b / c) because of its reduced accuracy relative to a * b / c
  // or (a * b) / c (which are equivalent).

  const bool isAutoOrMaxContentISize =
      styleISize->IsAuto() || styleISize->IsMaxContent();
  const bool isAutoBSize =
      nsLayoutUtils::IsAutoBSize(*styleBSize, aCBSize.BSize(aWM));

  const auto boxSizingAdjust = stylePos->mBoxSizing == StyleBoxSizing::BorderBox
                                   ? aBorderPadding
                                   : LogicalSize(aWM);
  // We don't expect these initial values of iSize/bSize to be used, but this
  // silences a GCC warning about them being uninitialized.
  nscoord minISize, maxISize, minBSize, maxBSize, iSize = 0, bSize = 0;
  enum class FillCB {
    // No stretching or clamping in the relevant axis.
    No,
    // Stretch to fill the containing block size in the relevant axis while
    // preserving the aspect-ratio. If both axes are stretched, the aspect-ratio
    // will be disregarded.
    Stretch,
    // Clamp to fill the containing block size in the relevant axis while
    // preserving the aspect-ratio. If both axes are clamped, the aspect-ratio
    // is respected, and the dimensions do not overflow the containing block
    // size.
    Clamp,
  };

  FillCB inlineFillCB = FillCB::No;  // fill CB behavior in the inline axis
  FillCB blockFillCB = FillCB::No;   // fill CB behavior in the block axis

  const LogicalSize fallbackIntrinsicSize(aWM, kFallbackIntrinsicSize);
  const Maybe<nscoord>& maybeIntrinsicISize = aIntrinsicSize.ISize(aWM);
  const bool hasIntrinsicISize = maybeIntrinsicISize.isSome();
  nscoord intrinsicISize = std::max(0, maybeIntrinsicISize.valueOr(0));

  const auto& maybeIntrinsicBSize = aIntrinsicSize.BSize(aWM);
  const bool hasIntrinsicBSize = maybeIntrinsicBSize.isSome();
  nscoord intrinsicBSize = std::max(0, maybeIntrinsicBSize.valueOr(0));

  Maybe<nscoord> iSizeToFillCB;
  Maybe<nscoord> bSizeToFillCB;
  if (!isAutoOrMaxContentISize) {
    iSize = ComputeISizeValue(aRenderingContext, aWM, aCBSize, aMargin,
                              aBorderPadding, *styleISize, *styleBSize,
                              aspectRatio, aFlags)
                .mISize;
  } else if (MOZ_UNLIKELY(isGridItem) &&
             !parentFrame->IsMasonry(aWM, LogicalAxis::Inline)) {
    MOZ_ASSERT(!IsTrueOverflowContainer());
    // 'auto' inline-size for grid-level box - apply 'stretch' as needed:
    auto cbSize = aCBSize.ISize(aWM);
    if (cbSize != NS_UNCONSTRAINEDSIZE) {
      if (!StyleMargin()->HasInlineAxisAuto(
              aWM, AnchorPosResolutionParams::From(this))) {
        auto inlineAxisAlignment = stylePos->UsedSelfAlignment(
            aWM, LogicalAxis::Inline, parentFrame->GetWritingMode(),
            parentFrame->Style());
        if (inlineAxisAlignment == StyleAlignFlags::STRETCH) {
          inlineFillCB = FillCB::Stretch;
        }
      }
      if (inlineFillCB != FillCB::No ||
          aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize)) {
        iSizeToFillCB.emplace(std::max(
            0, cbSize - aBorderPadding.ISize(aWM) - aMargin.ISize(aWM)));
      }
    } else {
      // Reset this flag to avoid applying the clamping below.
      aFlags -= ComputeSizeFlag::IClampMarginBoxMinSize;
    }
  }

  // Flex items ignore their min & max sizing properties in their flex
  // container's main-axis. (Those properties get applied later in the flexbox
  // algorithm.)
  const bool isFlexItemInlineAxisMainAxis =
      flexItemMainAxis && *flexItemMainAxis == LogicalAxis::Inline;
  const auto maxISizeCoord = stylePos->MaxISize(aWM, anchorResolutionParams);
  if (!maxISizeCoord->IsNone() && !isFlexItemInlineAxisMainAxis) {
    maxISize = ComputeISizeValue(aRenderingContext, aWM, aCBSize, aMargin,
                                 aBorderPadding, *maxISizeCoord, *styleBSize,
                                 aspectRatio, aFlags)
                   .mISize;
  } else {
    maxISize = nscoord_MAX;
  }

  const auto minISizeCoord = stylePos->MinISize(aWM, anchorResolutionParams);
  if (!minISizeCoord->IsAuto() && !isFlexItemInlineAxisMainAxis) {
    minISize = ComputeISizeValue(aRenderingContext, aWM, aCBSize, aMargin,
                                 aBorderPadding, *minISizeCoord, *styleBSize,
                                 aspectRatio, aFlags)
                   .mISize;
  } else {
    // Treat "min-width: auto" as 0.
    // NOTE: Technically, "auto" is supposed to behave like "min-content" on
    // flex items. However, we don't need to worry about that here, because
    // flex items' min-sizes are intentionally ignored until the flex
    // container explicitly considers them during space distribution.
    minISize = 0;
  }

  if (!isAutoBSize) {
    bSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
        aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
        boxSizingAdjust.BSize(aWM), *styleBSize);
  } else if (MOZ_UNLIKELY(isGridItem) &&
             !parentFrame->IsMasonry(aWM, LogicalAxis::Block)) {
    MOZ_ASSERT(!IsTrueOverflowContainer());
    // 'auto' block-size for grid-level box - apply 'stretch' as needed:
    auto cbSize = aCBSize.BSize(aWM);
    if (cbSize != NS_UNCONSTRAINEDSIZE) {
      if (!StyleMargin()->HasBlockAxisAuto(
              aWM, AnchorPosResolutionParams::From(this))) {
        auto blockAxisAlignment = stylePos->UsedSelfAlignment(
            aWM, LogicalAxis::Block, parentFrame->GetWritingMode(),
            parentFrame->Style());
        if (blockAxisAlignment == StyleAlignFlags::STRETCH) {
          blockFillCB = FillCB::Stretch;
        }
      }
      if (blockFillCB != FillCB::No ||
          aFlags.contains(ComputeSizeFlag::BClampMarginBoxMinSize)) {
        bSizeToFillCB.emplace(std::max(
            0, cbSize - aBorderPadding.BSize(aWM) - aMargin.BSize(aWM)));
      }
    } else {
      // Reset this flag to avoid applying the clamping below.
      aFlags -= ComputeSizeFlag::BClampMarginBoxMinSize;
    }
  }

  // Flex items ignore their min & max sizing properties in their flex
  // container's main-axis. (Those properties get applied later in the flexbox
  // algorithm.)
  const bool isFlexItemBlockAxisMainAxis =
      flexItemMainAxis && *flexItemMainAxis == LogicalAxis::Block;
  const auto maxBSizeCoord = stylePos->MaxBSize(aWM, anchorResolutionParams);
  if (!nsLayoutUtils::IsAutoBSize(*maxBSizeCoord, aCBSize.BSize(aWM)) &&
      !isFlexItemBlockAxisMainAxis) {
    maxBSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
        aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
        boxSizingAdjust.BSize(aWM), *maxBSizeCoord);
  } else {
    maxBSize = nscoord_MAX;
  }

  const auto minBSizeCoord = stylePos->MinBSize(aWM, anchorResolutionParams);
  if (!nsLayoutUtils::IsAutoBSize(*minBSizeCoord, aCBSize.BSize(aWM)) &&
      !isFlexItemBlockAxisMainAxis) {
    minBSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
        aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
        boxSizingAdjust.BSize(aWM), *minBSizeCoord);
  } else {
    minBSize = 0;
  }

  NS_ASSERTION(aCBSize.ISize(aWM) != NS_UNCONSTRAINEDSIZE,
               "Our containing block must not have unconstrained inline-size!");
  MOZ_ASSERT(!(inlineFillCB != FillCB::No ||
               aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize)) ||
                 iSizeToFillCB,
             "iSizeToFillCB must be valid when stretching or clamping in the "
             "inline axis!");
  MOZ_ASSERT(!(blockFillCB != FillCB::No ||
               aFlags.contains(ComputeSizeFlag::BClampMarginBoxMinSize)) ||
                 bSizeToFillCB,
             "bSizeToFillCB must be valid when stretching or clamping in the "
             "block axis!");

  // Now calculate the used values for iSize and bSize:
  if (isAutoOrMaxContentISize) {
    if (isAutoBSize) {
      // 'auto' iSize, 'auto' bSize

      // Get tentative values - CSS 2.1 sections 10.3.2 and 10.6.2:

      nscoord tentISize, tentBSize;

      if (hasIntrinsicISize) {
        tentISize = intrinsicISize;
      } else if (hasIntrinsicBSize && aspectRatio) {
        tentISize = aspectRatio.ComputeRatioDependentSize(
            LogicalAxis::Inline, aWM, intrinsicBSize, boxSizingAdjust);
      } else if (aspectRatio) {
        tentISize =
            aCBSize.ISize(aWM) - aBorderPadding.ISize(aWM) - aMargin.ISize(aWM);
        if (tentISize < 0) {
          tentISize = 0;
        }
      } else {
        tentISize = fallbackIntrinsicSize.ISize(aWM);
      }

      // If we need to clamp the inline size to fit the CB, we use the 'stretch'
      // or 'normal' codepath.  We use the ratio-preserving 'normal' codepath
      // unless we have 'stretch' in the other axis.
      if (aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize) &&
          inlineFillCB != FillCB::Stretch && tentISize > *iSizeToFillCB) {
        inlineFillCB =
            (blockFillCB == FillCB::Stretch ? FillCB::Stretch : FillCB::Clamp);
      }

      // Honor aspect ratio if there's an intrinsic isize, even if there's an
      // intrinsic bsize.
      if (aspectRatio && (!hasIntrinsicBSize || hasIntrinsicISize)) {
        tentBSize = aspectRatio.ComputeRatioDependentSize(
            LogicalAxis::Block, aWM, tentISize, boxSizingAdjust);
      } else if (hasIntrinsicBSize) {
        tentBSize = intrinsicBSize;
      } else {
        tentBSize = fallbackIntrinsicSize.BSize(aWM);
      }

      // (ditto the comment about clamping the inline size above)
      if (aFlags.contains(ComputeSizeFlag::BClampMarginBoxMinSize) &&
          blockFillCB != FillCB::Stretch && tentBSize > *bSizeToFillCB) {
        blockFillCB =
            (inlineFillCB == FillCB::Stretch ? FillCB::Stretch : FillCB::Clamp);
      }

      // The slash notation is the used value of "align-self / justify-self".
      if (inlineFillCB == FillCB::Stretch) {
        tentISize = *iSizeToFillCB;  // * / 'stretch'
        if (blockFillCB == FillCB::Stretch) {
          tentBSize = *bSizeToFillCB;  // 'stretch' / 'stretch'
        } else if (aspectRatio) {
          // * (except 'stretch') / 'stretch'
          tentBSize = aspectRatio.ComputeRatioDependentSize(
              LogicalAxis::Block, aWM, *iSizeToFillCB, boxSizingAdjust);
        }
      } else if (blockFillCB == FillCB::Stretch) {
        tentBSize = *bSizeToFillCB;  // 'stretch' / * (except 'stretch')
        if (aspectRatio) {
          tentISize = aspectRatio.ComputeRatioDependentSize(
              LogicalAxis::Inline, aWM, *bSizeToFillCB, boxSizingAdjust);
        }
      } else if (inlineFillCB == FillCB::Clamp && aspectRatio) {
        tentISize = *iSizeToFillCB;  // * (except 'stretch') / 'normal'
        tentBSize = aspectRatio.ComputeRatioDependentSize(
            LogicalAxis::Block, aWM, *iSizeToFillCB, boxSizingAdjust);
        if (blockFillCB == FillCB::Clamp && tentBSize > *bSizeToFillCB) {
          // Clamp within the CB size with preserved aspect ratio.
          tentBSize = *bSizeToFillCB;  // 'normal' / 'normal'
          tentISize = aspectRatio.ComputeRatioDependentSize(
              LogicalAxis::Inline, aWM, *bSizeToFillCB, boxSizingAdjust);
        }
      } else if (blockFillCB == FillCB::Clamp && aspectRatio) {
        // 'normal' / * (except 'normal' and 'stretch')
        tentBSize = *bSizeToFillCB;
        tentISize = aspectRatio.ComputeRatioDependentSize(
            LogicalAxis::Inline, aWM, *bSizeToFillCB, boxSizingAdjust);
      }

      // ComputeAutoSizeWithIntrinsicDimensions preserves the ratio when
      // applying the min/max-size.  We don't want that when we have 'stretch'
      // in either axis because tentISize/tentBSize is likely not according to
      // ratio now.
      if (aspectRatio && inlineFillCB != FillCB::Stretch &&
          blockFillCB != FillCB::Stretch) {
        nsSize autoSize = nsLayoutUtils::ComputeAutoSizeWithIntrinsicDimensions(
            minISize, minBSize, maxISize, maxBSize, tentISize, tentBSize);
        // The nsSize that ComputeAutoSizeWithIntrinsicDimensions returns will
        // actually contain logical values if the parameters passed to it were
        // logical coordinates, so we do NOT perform a physical-to-logical
        // conversion here, but just assign the fields directly to our result.
        iSize = autoSize.width;
        bSize = autoSize.height;
      } else {
        // Not honoring an intrinsic ratio: clamp the dimensions independently.
        iSize = CSSMinMax(tentISize, minISize, maxISize);
        bSize = CSSMinMax(tentBSize, minBSize, maxBSize);
      }
    } else {
      // 'auto' iSize, non-'auto' bSize
      bSize = CSSMinMax(bSize, minBSize, maxBSize);
      if (inlineFillCB == FillCB::Stretch) {
        iSize = *iSizeToFillCB;
      } else if (aspectRatio) {
        iSize = aspectRatio.ComputeRatioDependentSize(LogicalAxis::Inline, aWM,
                                                      bSize, boxSizingAdjust);
      } else if (hasIntrinsicISize) {
        iSize = aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize) &&
                        intrinsicISize > *iSizeToFillCB
                    ? *iSizeToFillCB
                    : intrinsicISize;
      } else {
        iSize = fallbackIntrinsicSize.ISize(aWM);
      }
      iSize = CSSMinMax(iSize, minISize, maxISize);
    }
  } else {
    if (isAutoBSize) {
      // non-'auto' iSize, 'auto' bSize
      iSize = CSSMinMax(iSize, minISize, maxISize);
      if (blockFillCB == FillCB::Stretch) {
        bSize = *bSizeToFillCB;
      } else if (aspectRatio) {
        bSize = aspectRatio.ComputeRatioDependentSize(LogicalAxis::Block, aWM,
                                                      iSize, boxSizingAdjust);
      } else if (hasIntrinsicBSize) {
        bSize = aFlags.contains(ComputeSizeFlag::BClampMarginBoxMinSize) &&
                        intrinsicBSize > *bSizeToFillCB
                    ? *bSizeToFillCB
                    : intrinsicBSize;
      } else {
        bSize = fallbackIntrinsicSize.BSize(aWM);
      }
      bSize = CSSMinMax(bSize, minBSize, maxBSize);
    } else {
      // non-'auto' iSize, non-'auto' bSize
      iSize = CSSMinMax(iSize, minISize, maxISize);
      bSize = CSSMinMax(bSize, minBSize, maxBSize);
    }
  }

  return LogicalSize(aWM, iSize, bSize);
}

nsRect nsContainerFrame::ComputeSimpleTightBounds(
    DrawTarget* aDrawTarget) const {
  if (StyleOutline()->ShouldPaintOutline() || StyleBorder()->HasBorder() ||
      !StyleBackground()->IsTransparent(this) ||
      StyleDisplay()->HasNativeAppearance()) {
    // Not necessarily tight, due to clipping, negative
    // outline-offset, and lots of other issues, but that's OK
    return InkOverflowRect();
  }

  nsRect r(0, 0, 0, 0);
  for (const auto& childLists : ChildLists()) {
    for (nsIFrame* child : childLists.mList) {
      r.UnionRect(
          r, child->ComputeTightBounds(aDrawTarget) + child->GetPosition());
    }
  }
  return r;
}

void nsContainerFrame::PushDirtyBitToAbsoluteFrames() {
  if (!HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    return;  // No dirty bit to push.
  }
  if (!HasAbsolutelyPositionedChildren()) {
    return;  // No absolute children to push to.
  }
  GetAbsoluteContainingBlock()->MarkAllFramesDirty();
}

// Define the MAX_FRAME_DEPTH to be the ContentSink's MAX_REFLOW_DEPTH plus
// 4 for the frames above the document's frames:
//  the Viewport, GFXScroll, ScrollPort, and Canvas
#define MAX_FRAME_DEPTH (MAX_REFLOW_DEPTH + 4)

bool nsContainerFrame::IsFrameTreeTooDeep(const ReflowInput& aReflowInput,
                                          ReflowOutput& aMetrics,
                                          nsReflowStatus& aStatus) {
  if (aReflowInput.mReflowDepth > MAX_FRAME_DEPTH) {
    NS_WARNING("frame tree too deep; setting zero size and returning");
    AddStateBits(NS_FRAME_TOO_DEEP_IN_FRAME_TREE);
    ClearOverflowRects();
    aMetrics.ClearSize();
    aMetrics.SetBlockStartAscent(0);
    aMetrics.mCarriedOutBEndMargin.Zero();
    aMetrics.mOverflowAreas.Clear();

    aStatus.Reset();
    if (GetNextInFlow()) {
      // Reflow depth might vary between reflows, so we might have
      // successfully reflowed and split this frame before.  If so, we
      // shouldn't delete its continuations.
      aStatus.SetIncomplete();
    }

    return true;
  }
  RemoveStateBits(NS_FRAME_TOO_DEEP_IN_FRAME_TREE);
  return false;
}

bool nsContainerFrame::ShouldAvoidBreakInside(
    const ReflowInput& aReflowInput) const {
  MOZ_ASSERT(this == aReflowInput.mFrame,
             "Caller should pass a ReflowInput for this frame!");

  const auto* disp = StyleDisplay();
  const bool mayAvoidBreak = [&] {
    switch (disp->mBreakInside) {
      case StyleBreakWithin::Auto:
        return false;
      case StyleBreakWithin::Avoid:
        return true;
      case StyleBreakWithin::AvoidPage:
        return aReflowInput.mBreakType == BreakType::Page;
      case StyleBreakWithin::AvoidColumn:
        return aReflowInput.mBreakType == BreakType::Column;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown break-inside value");
    return false;
  }();

  if (!mayAvoidBreak) {
    return false;
  }
  if (aReflowInput.mFlags.mIsTopOfPage) {
    return false;
  }
  if (IsAbsolutelyPositioned(disp)) {
    return false;
  }
  if (GetPrevInFlow()) {
    return false;
  }
  return true;
}

void nsContainerFrame::ConsiderChildOverflow(OverflowAreas& aOverflowAreas,
                                             nsIFrame* aChildFrame,
                                             OverflowAreaUnionFlags aFlags) {
  const OverflowAreas childOverflows = [&]() -> OverflowAreas {
    if (StyleDisplay()->IsContainLayout() && SupportsContainLayoutAndPaint() &&
        !(aFlags & OverflowAreaUnionFlags::AsIfScrolled)) {
      // If we have layout containment and are not a non-atomic, inline-level
      // principal box, we should only consider our child's ink overflow,
      // leaving the scrollable regions of the parent unaffected.
      // Note: scrollable overflow is a subset of ink overflow,
      // so this has the same affect as unioning the child's ink and
      // scrollable overflow with the parent's ink overflow.
      return OverflowAreas(aChildFrame->InkOverflowRect(), nsRect()) +
             aChildFrame->GetPosition();
    }
    return aChildFrame->GetActualAndNormalOverflowAreasRelativeToParent();
  }();
  if (aFlags & OverflowAreaUnionFlags::ChildIsAbsPos) {
    aOverflowAreas.UnionWithAbsoluteOverflowAreas(childOverflows);
  } else {
    aOverflowAreas.UnionWith(childOverflows);
  }
}

StyleAlignFlags nsContainerFrame::CSSAlignmentForAbsPosChild(
    const ReflowInput& aChildRI, LogicalAxis aLogicalAxis) const {
  MOZ_ASSERT(aChildRI.mFrame->IsAbsolutelyPositioned(),
             "This method should only be called for abspos children");
  // For computing the static position of an absolutely positioned box,
  // `auto` takes from parent's `align-items`.
  StyleAlignFlags alignment =
      aChildRI.mStylePosition->UsedSelfAlignment(aLogicalAxis, Style());
  return CSSAlignUtils::UsedAlignmentForAbsPos(aChildRI.mFrame, alignment,
                                               aLogicalAxis, GetWritingMode());
}

StyleAlignFlags
nsContainerFrame::CSSAlignmentForAbsPosChildWithinContainingBlock(
    const SizeComputationInput& aSizingInput, LogicalAxis aLogicalAxis,
    const StylePositionArea& aResolvedPositionArea,
    const LogicalSize& aCBSize) const {
  MOZ_ASSERT(aSizingInput.mFrame->IsAbsolutelyPositioned(),
             "This method should only be called for abspos children");
  // When determining the position of absolutely-positioned boxes,
  // `auto` behaves as `normal`.
  StyleAlignFlags alignment =
      aSizingInput.mFrame->StylePosition()->UsedSelfAlignment(aLogicalAxis,
                                                              nullptr);

  // Check if position-area is set - if so, it determines the default alignment
  // https://drafts.csswg.org/css-anchor-position/#position-area-alignment
  if (!aResolvedPositionArea.IsNone() && alignment == StyleAlignFlags::NORMAL) {
    const WritingMode cbWM = GetWritingMode();
    const auto anchorResolutionParams = AnchorPosResolutionParams::From(
        &aSizingInput, /* aIgnorePositionArea = */ true);
    const auto anchorOffsetResolutionParams =
        AnchorPosOffsetResolutionParams::ExplicitCBFrameSize(
            anchorResolutionParams, &aCBSize);

    // Check if we have exactly one auto inset in this axis (IMCB situation)
    const auto singleAutoInset =
        aSizingInput.mFrame->StylePosition()->GetSingleAutoInsetInAxis(
            aLogicalAxis, cbWM, anchorOffsetResolutionParams);

    // Check if exactly one inset in the axis is auto
    // https://drafts.csswg.org/css-anchor-position/#position-area-alignment
    // "However, if only one inset property in the relevant axis is auto, the
    // default alignment is instead towards the edge with the non-auto inset;
    // and this is an unsafe alignment."
    if (singleAutoInset.isSome()) {
      const LogicalSide startSide = aLogicalAxis == LogicalAxis::Inline
                                        ? LogicalSide::IStart
                                        : LogicalSide::BStart;
      const mozilla::Side autoSide = *singleAutoInset;
      const mozilla::Side startPhysicalSide = cbWM.PhysicalSide(startSide);
      // Exactly one inset is auto - align toward the non-auto edge, unsafely
      alignment = (autoSide == startPhysicalSide) ? StyleAlignFlags::END
                                                  : StyleAlignFlags::START;
      alignment |= StyleAlignFlags::UNSAFE;
    } else {
      // Use default position-area self-alignment:
      // https://drafts.csswg.org/css-anchor-position-1/#position-area-alignment
      const auto axis = ToStyleLogicalAxis(aLogicalAxis);
      const auto cbSWM = cbWM.ToStyleWritingMode();
      const auto selfWM =
          aSizingInput.mFrame->GetWritingMode().ToStyleWritingMode();
      Servo_ResolvePositionAreaSelfAlignment(&aResolvedPositionArea, axis,
                                             &cbSWM, &selfWM, &alignment);
    }
  }

  return CSSAlignUtils::UsedAlignmentForAbsPos(aSizingInput.mFrame, alignment,
                                               aLogicalAxis, GetWritingMode());
}

nsOverflowContinuationTracker::nsOverflowContinuationTracker(
    nsContainerFrame* aFrame, bool aWalkOOFFrames,
    bool aSkipOverflowContainerChildren)
    : mOverflowContList(nullptr),
      mPrevOverflowCont(nullptr),
      mSentry(nullptr),
      mParent(aFrame),
      mSkipOverflowContainerChildren(aSkipOverflowContainerChildren),
      mWalkOOFFrames(aWalkOOFFrames) {
  MOZ_ASSERT(aFrame, "null frame pointer");
  SetupOverflowContList();
}

void nsOverflowContinuationTracker::SetupOverflowContList() {
  MOZ_ASSERT(mParent, "null frame pointer");
  MOZ_ASSERT(!mOverflowContList, "already have list");
  nsContainerFrame* nif =
      static_cast<nsContainerFrame*>(mParent->GetNextInFlow());
  if (nif) {
    mOverflowContList = nif->GetOverflowContainers();
    if (mOverflowContList) {
      mParent = nif;
      SetUpListWalker();
    }
  }
  if (!mOverflowContList) {
    mOverflowContList = mParent->GetExcessOverflowContainers();
    if (mOverflowContList) {
      SetUpListWalker();
    }
  }
}

/**
 * Helper function to walk past overflow continuations whose prev-in-flow
 * isn't a normal child and to set mSentry and mPrevOverflowCont correctly.
 */
void nsOverflowContinuationTracker::SetUpListWalker() {
  NS_ASSERTION(!mSentry && !mPrevOverflowCont,
               "forgot to reset mSentry or mPrevOverflowCont");
  if (mOverflowContList) {
    nsIFrame* cur = mOverflowContList->FirstChild();
    if (mSkipOverflowContainerChildren) {
      while (cur && cur->GetPrevInFlow()->HasAnyStateBits(
                        NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        mPrevOverflowCont = cur;
        cur = cur->GetNextSibling();
      }
      while (cur &&
             (cur->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) != mWalkOOFFrames)) {
        mPrevOverflowCont = cur;
        cur = cur->GetNextSibling();
      }
    }
    if (cur) {
      mSentry = cur->GetPrevInFlow();
    }
  }
}

/**
 * Helper function to step forward through the overflow continuations list.
 * Sets mSentry and mPrevOverflowCont, skipping over OOF or non-OOF frames
 * as appropriate. May only be called when we have already set up an
 * mOverflowContList; mOverflowContList cannot be null.
 */
void nsOverflowContinuationTracker::StepForward() {
  MOZ_ASSERT(mOverflowContList, "null list");

  // Step forward
  if (mPrevOverflowCont) {
    mPrevOverflowCont = mPrevOverflowCont->GetNextSibling();
  } else {
    mPrevOverflowCont = mOverflowContList->FirstChild();
  }

  // Skip over oof or non-oof frames as appropriate
  if (mSkipOverflowContainerChildren) {
    nsIFrame* cur = mPrevOverflowCont->GetNextSibling();
    while (cur &&
           (cur->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) != mWalkOOFFrames)) {
      mPrevOverflowCont = cur;
      cur = cur->GetNextSibling();
    }
  }

  // Set up the sentry
  mSentry = (mPrevOverflowCont->GetNextSibling())
                ? mPrevOverflowCont->GetNextSibling()->GetPrevInFlow()
                : nullptr;
}

nsresult nsOverflowContinuationTracker::Insert(nsIFrame* aOverflowCont,
                                               nsReflowStatus& aReflowStatus) {
  MOZ_ASSERT(aOverflowCont, "null frame pointer");
  MOZ_ASSERT(!mSkipOverflowContainerChildren ||
                 mWalkOOFFrames ==
                     aOverflowCont->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW),
             "shouldn't insert frame that doesn't match walker type");
  MOZ_ASSERT(aOverflowCont->GetPrevInFlow(),
             "overflow containers must have a prev-in-flow");

  nsresult rv = NS_OK;
  bool reparented = false;
  bool addToList = !mSentry || aOverflowCont != mSentry->GetNextInFlow();

  // If we have a list and aOverflowCont is already in it then don't try to
  // add it again.
  if (addToList && aOverflowCont->GetParent() == mParent &&
      aOverflowCont->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER) &&
      mOverflowContList && mOverflowContList->ContainsFrame(aOverflowCont)) {
    addToList = false;
    mPrevOverflowCont = aOverflowCont->GetPrevSibling();
  }

  if (addToList) {
    if (aOverflowCont->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
      // aOverflowCont is in some other overflow container list,
      // steal it first
      NS_ASSERTION(!(mOverflowContList &&
                     mOverflowContList->ContainsFrame(aOverflowCont)),
                   "overflow containers out of order");
      aOverflowCont->GetParent()->StealFrame(aOverflowCont);
    } else {
      aOverflowCont->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
    }
    if (!mOverflowContList) {
      // Note: We don't use SetExcessOverflowContainers() since it requires
      // setting a non-empty list. It's OK to manually set an empty list to
      // ExcessOverflowContainersProperty() because we are going to insert
      // aOverflowCont to mOverflowContList below, which guarantees an nonempty
      // list in ExcessOverflowContainersProperty().
      mOverflowContList = new nsFrameList();
      mParent->SetProperty(nsContainerFrame::ExcessOverflowContainersProperty(),
                           mOverflowContList);
      SetUpListWalker();
    }
    if (aOverflowCont->GetParent() != mParent) {
      reparented = true;
    }

    // If aOverflowCont has a prev/next-in-flow that might be in
    // mOverflowContList we need to find it and insert after/before it to
    // maintain the order amongst next-in-flows in this list.
    nsIFrame* pif = aOverflowCont->GetPrevInFlow();
    nsIFrame* nif = aOverflowCont->GetNextInFlow();
    if ((pif && pif->GetParent() == mParent && pif != mPrevOverflowCont) ||
        (nif && nif->GetParent() == mParent && mPrevOverflowCont)) {
      for (nsIFrame* f : *mOverflowContList) {
        if (f == pif) {
          mPrevOverflowCont = pif;
          break;
        }
        if (f == nif) {
          mPrevOverflowCont = f->GetPrevSibling();
          break;
        }
      }
    }

    mOverflowContList->InsertFrame(mParent, mPrevOverflowCont, aOverflowCont);
    aReflowStatus.SetNextInFlowNeedsReflow();
  }

  // If we need to reflow it, mark it dirty
  if (aReflowStatus.NextInFlowNeedsReflow()) {
    aOverflowCont->MarkSubtreeDirty();
  }

  // It's in our list, just step forward
  StepForward();
  NS_ASSERTION(mPrevOverflowCont == aOverflowCont ||
                   (mSkipOverflowContainerChildren &&
                    mPrevOverflowCont->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW) !=
                        aOverflowCont->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)),
               "OverflowContTracker in unexpected state");

  if (addToList) {
    // Convert all non-overflow-container next-in-flows of aOverflowCont
    // into overflow containers and move them to our overflow
    // tracker. This preserves the invariant that the next-in-flows
    // of an overflow container are also overflow containers.
    nsIFrame* f = aOverflowCont->GetNextInFlow();
    if (f && (!f->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER) ||
              (!reparented && f->GetParent() == mParent) ||
              (reparented && f->GetParent() != mParent))) {
      if (!f->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        f->GetParent()->StealFrame(f);
      }
      Insert(f, aReflowStatus);
    }
  }
  return rv;
}

void nsOverflowContinuationTracker::BeginFinish(nsIFrame* aChild) {
  MOZ_ASSERT(aChild, "null ptr");
  MOZ_ASSERT(aChild->GetNextInFlow(),
             "supposed to call Finish *before* deleting next-in-flow!");

  for (nsIFrame* f = aChild; f; f = f->GetNextInFlow()) {
    // We'll update these in EndFinish after the next-in-flows are gone.
    if (f == mPrevOverflowCont) {
      mSentry = nullptr;
      mPrevOverflowCont = nullptr;
      break;
    }
    if (f == mSentry) {
      mSentry = nullptr;
      break;
    }
  }
}

void nsOverflowContinuationTracker::EndFinish(nsIFrame* aChild) {
  if (!mOverflowContList) {
    return;
  }
  // Forget mOverflowContList if it was deleted.
  nsFrameList* eoc = mParent->GetExcessOverflowContainers();
  if (eoc != mOverflowContList) {
    nsFrameList* oc = mParent->GetOverflowContainers();
    if (oc != mOverflowContList) {
      // mOverflowContList was deleted
      mPrevOverflowCont = nullptr;
      mSentry = nullptr;
      mParent = aChild->GetParent();
      mOverflowContList = nullptr;
      SetupOverflowContList();
      return;
    }
  }
  // The list survived, update mSentry if needed.
  if (!mSentry) {
    if (!mPrevOverflowCont) {
      SetUpListWalker();
    } else {
      mozilla::AutoRestore<nsIFrame*> saved(mPrevOverflowCont);
      // step backward to make StepForward() use our current mPrevOverflowCont
      mPrevOverflowCont = mPrevOverflowCont->GetPrevSibling();
      StepForward();
    }
  }
}

RubyMetrics nsContainerFrame::RubyMetricsIncludingChildren(
    float aRubyMetricsFactor) const {
  mozilla::RubyMetrics result;
  WritingMode containerWM = GetWritingMode();
  bool foundAnyFrames = false;
  for (const auto* f : mFrames) {
    WritingMode wm = f->GetWritingMode();
    if (wm.IsOrthogonalTo(containerWM) || f->IsPlaceholderFrame()) {
      continue;
    }
    mozilla::RubyMetrics m = f->RubyMetrics(aRubyMetricsFactor);
    result.CombineWith(m);
    foundAnyFrames = true;
  }
  if (!foundAnyFrames) {
    result = nsIFrame::RubyMetrics(aRubyMetricsFactor);
  }
  return result;
}

/////////////////////////////////////////////////////////////////////////////
// Debugging

#ifdef DEBUG
void nsContainerFrame::SanityCheckChildListsBeforeReflow() const {
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Flex / Grid containers can call this!");

  const auto didPushItemsBit = IsFlexContainerFrame()
                                   ? NS_STATE_FLEX_DID_PUSH_ITEMS
                                   : NS_STATE_GRID_DID_PUSH_ITEMS;
  ChildListIDs absLists = {FrameChildListID::Absolute,
                           FrameChildListID::PushedAbsolute,
                           FrameChildListID::OverflowContainers,
                           FrameChildListID::ExcessOverflowContainers};
  ChildListIDs itemLists = {FrameChildListID::Principal,
                            FrameChildListID::Overflow};
  for (const nsIFrame* f = this; f; f = f->GetNextInFlow()) {
    MOZ_ASSERT(!f->HasAnyStateBits(didPushItemsBit),
               "At start of reflow, we should've pulled items back from all "
               "NIFs and cleared the state bit stored in didPushItemsBit in "
               "the process.");
    for (const auto& [list, listID] : f->ChildLists()) {
      if (!itemLists.contains(listID)) {
        MOZ_ASSERT(absLists.contains(listID),
                   "unexpected non-empty child list");
        continue;
      }
      for (const auto* child : list) {
        MOZ_ASSERT(f == this || child->GetPrevInFlow(),
                   "all pushed items must be pulled up before reflow");
      }
    }
  }
  // If we have a prev-in-flow, each of its children's next-in-flow
  // should be one of our children or be null.
  const auto* pif = static_cast<nsContainerFrame*>(GetPrevInFlow());
  if (pif) {
    const nsFrameList* oc = GetOverflowContainers();
    const nsFrameList* eoc = GetExcessOverflowContainers();
    const nsFrameList* pifEOC = pif->GetExcessOverflowContainers();
    for (const nsIFrame* child : pif->PrincipalChildList()) {
      const nsIFrame* childNIF = child->GetNextInFlow();
      MOZ_ASSERT(!childNIF || mFrames.ContainsFrame(childNIF) ||
                 (pifEOC && pifEOC->ContainsFrame(childNIF)) ||
                 (oc && oc->ContainsFrame(childNIF)) ||
                 (eoc && eoc->ContainsFrame(childNIF)));
    }
  }
}

void nsContainerFrame::SetDidPushItemsBitIfNeeded(ChildListID aListID,
                                                  nsIFrame* aOldFrame) {
  MOZ_ASSERT(IsFlexOrGridContainer(),
             "Only Flex / Grid containers can call this!");

  // Note that FrameChildListID::Principal doesn't mean aOldFrame must be on
  // that list. It can also be on FrameChildListID::Overflow, in which case it
  // might be a pushed item, and if it's the only pushed item our DID_PUSH_ITEMS
  // bit will lie.
  if (aListID == FrameChildListID::Principal && !aOldFrame->GetPrevInFlow()) {
    // Since the bit may lie, set the mDidPushItemsBitMayLie value to true for
    // ourself and for all our prev-in-flows.
    nsContainerFrame* frameThatMayLie = this;
    do {
      frameThatMayLie->mDidPushItemsBitMayLie = true;
      frameThatMayLie =
          static_cast<nsContainerFrame*>(frameThatMayLie->GetPrevInFlow());
    } while (frameThatMayLie);
  }
}
#endif

#ifdef DEBUG_FRAME_DUMP
void nsContainerFrame::List(FILE* out, const char* aPrefix,
                            ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);
  ExtraContainerFrameInfo(str,
                          aFlags.contains(ListFlag::OnlyListDeterministicInfo));

  // Output the frame name and various fields.
  fprintf_stderr(out, "%s <\n", str.get());

  const nsCString pfx = nsCString(aPrefix) + "  "_ns;

  // Output principal child list separately since we want to omit its
  // name and address.
  for (nsIFrame* kid : PrincipalChildList()) {
    kid->List(out, pfx.get(), aFlags);
  }

  // Output rest of the child lists.
  const ChildListIDs skippedListIDs = {FrameChildListID::Principal};
  ListChildLists(out, pfx.get(), aFlags, skippedListIDs);

  fprintf_stderr(out, "%s>\n", aPrefix);
}

void nsContainerFrame::ListWithMatchedRules(FILE* out,
                                            const char* aPrefix) const {
  fprintf_stderr(out, "%s%s\n", aPrefix, ListTag().get());

  nsCString rulePrefix;
  rulePrefix += aPrefix;
  rulePrefix += "    ";
  ListMatchedRules(out, rulePrefix.get());

  nsCString childPrefix;
  childPrefix += aPrefix;
  childPrefix += "  ";

  for (const auto& childList : ChildLists()) {
    for (const nsIFrame* kid : childList.mList) {
      kid->ListWithMatchedRules(out, childPrefix.get());
    }
  }
}

void nsContainerFrame::ListChildLists(FILE* aOut, const char* aPrefix,
                                      ListFlags aFlags,
                                      ChildListIDs aSkippedListIDs) const {
  const nsCString nestedPfx = nsCString(aPrefix) + "  "_ns;

  for (const auto& [list, listID] : ChildLists()) {
    if (aSkippedListIDs.contains(listID)) {
      continue;
    }

    // Use nsPrintfCString so that %p don't output prefix "0x". This is
    // consistent with nsIFrame::ListTag().
    nsCString str{nsPrintfCString("%s%s", aPrefix, ChildListName(listID))};
    ListPtr(str, aFlags, &GetChildList(listID), "@");
    str += " <\n";
    fprintf_stderr(aOut, "%s", str.get());

    for (nsIFrame* kid : list) {
      // Verify the child frame's parent frame pointer is correct.
      NS_ASSERTION(kid->GetParent() == this, "Bad parent frame pointer!");
      kid->List(aOut, nestedPfx.get(), aFlags);
    }
    fprintf_stderr(aOut, "%s>\n", aPrefix);
  }
}

void nsContainerFrame::ExtraContainerFrameInfo(nsACString&, bool) const {}

#endif
