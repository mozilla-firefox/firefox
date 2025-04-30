/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* base class of all rendering objects */

#include "nsIFrame.h"

#include <stdarg.h>
#include <algorithm>

#include "gfx2DGlue.h"
#include "gfxUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/EventForwards.h"
#include "mozilla/FocusModel.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/CSSAnimation.h"
#include "mozilla/dom/CSSTransition.h"
#include "mozilla/dom/ContentVisibilityAutoStateChangeEvent.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLDetailsElement.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/SelectionMovementUtils.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_print.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/SVGMaskFrame.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/ToString.h"
#include "mozilla/Try.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/WritingModes.h"

#include "nsCOMPtr.h"
#include "nsFieldSetFrame.h"
#include "nsFlexContainerFrame.h"
#include "nsFocusManager.h"
#include "nsFrameList.h"
#include "nsFrameState.h"
#include "nsTextControlFrame.h"
#include "nsPlaceholderFrame.h"
#include "nsIBaseWindow.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsContentUtils.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSProps.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRendering.h"
#include "nsAtom.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsTableWrapperFrame.h"
#include "nsView.h"
#include "nsViewManager.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsStyleConsts.h"
#include "mozilla/Logging.h"
#include "nsLayoutUtils.h"
#include "LayoutLogging.h"
#include "mozilla/RestyleManager.h"
#include "nsImageFrame.h"
#include "nsInlineFrame.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsGridContainerFrame.h"
#include "nsCSSAnonBoxes.h"
#include "nsCanvasFrame.h"

#include "nsFieldSetFrame.h"
#include "nsFrameTraversal.h"
#include "nsRange.h"
#include "nsNameSpaceManager.h"
#include "nsIPercentBSizeObserver.h"
#include "nsStyleStructInlines.h"

#include "nsBidiPresUtils.h"
#include "RubyUtils.h"
#include "TextOverflow.h"
#include "nsAnimationManager.h"

// For triple-click pref
#include "imgIRequest.h"
#include "nsError.h"
#include "nsContainerFrame.h"
#include "nsBlockFrame.h"
#include "nsDisplayList.h"
#include "nsChangeHint.h"
#include "nsSubDocumentFrame.h"
#include "RetainedDisplayListBuilder.h"

#include "gfxContext.h"
#include "nsAbsoluteContainingBlock.h"
#include "ScrollSnap.h"
#include "StickyScrollContainer.h"
#include "nsFontInflationData.h"
#include "nsRegion.h"
#include "nsIFrameInlines.h"
#include "nsStyleChangeList.h"
#include "nsWindowSizes.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/CSSClipPathInstance.h"
#include "mozilla/EffectCompositor.h"
#include "mozilla/EffectSet.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/SVGPathData.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layout/ScrollAnchorContainer.h"
#include "nsPrintfCString.h"
#include "ActiveLayerTracker.h"

#include "nsITheme.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::layout;
typedef nsAbsoluteContainingBlock::AbsPosReflowFlags AbsPosReflowFlags;
using nsStyleTransformMatrix::TransformReferenceBox;

nsIFrame* nsILineIterator::LineInfo::GetLastFrameOnLine() const {
  if (!mNumFramesOnLine) {
    return nullptr;  // empty line, not illegal
  }
  MOZ_ASSERT(mFirstFrameOnLine);
  nsIFrame* maybeLastFrame = mFirstFrameOnLine;
  for ([[maybe_unused]] int32_t i : IntegerRange(mNumFramesOnLine - 1)) {
    maybeLastFrame = maybeLastFrame->GetNextSibling();
    if (NS_WARN_IF(!maybeLastFrame)) {
      return nullptr;
    }
  }
  return maybeLastFrame;
}

#ifdef HAVE_64BIT_BUILD
static_assert(sizeof(nsIFrame) == 120, "nsIFrame should remain small");
#else
static_assert(sizeof(void*) == 4, "Odd build config?");
// FIXME(emilio): Investigate why win32 and android-arm32 have bigger sizes (80)
// than Linux32 (76).
static_assert(sizeof(nsIFrame) <= 80, "nsIFrame should remain small");
#endif

const mozilla::LayoutFrameType nsIFrame::sLayoutFrameTypes[kFrameClassCount] = {
#define FRAME_ID(class_, type_, ...) mozilla::LayoutFrameType::type_,
#define ABSTRACT_FRAME_ID(...)
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
};

const nsIFrame::ClassFlags nsIFrame::sLayoutFrameClassFlags[kFrameClassCount] =
    {
#define FRAME_ID(class_, type_, flags_, ...) flags_,
#define ABSTRACT_FRAME_ID(...)
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
};

std::ostream& operator<<(std::ostream& aStream, const nsDirection& aDirection) {
  return aStream << (aDirection == eDirNext ? "eDirNext" : "eDirPrevious");
}

struct nsContentAndOffset {
  nsIContent* mContent = nullptr;
  int32_t mOffset = 0;
};

#include "nsILineIterator.h"
#include "prenv.h"

// Utility function to set a nsRect-valued property table entry on aFrame,
// reusing the existing storage if the property happens to be already set.
template <typename T>
static void SetOrUpdateRectValuedProperty(
    nsIFrame* aFrame, FrameProperties::Descriptor<T> aProperty,
    const nsRect& aNewValue) {
  bool found;
  nsRect* rectStorage = aFrame->GetProperty(aProperty, &found);
  if (!found) {
    rectStorage = new nsRect(aNewValue);
    aFrame->AddProperty(aProperty, rectStorage);
  } else {
    *rectStorage = aNewValue;
  }
}

FrameDestroyContext::~FrameDestroyContext() {
  for (auto& content : mozilla::Reversed(mAnonymousContent)) {
    mPresShell->NativeAnonymousContentWillBeRemoved(content);
    content->UnbindFromTree();
  }
}

// Formerly the nsIFrameDebug interface

std::ostream& operator<<(std::ostream& aStream, const nsReflowStatus& aStatus) {
  char complete = 'Y';
  if (aStatus.IsIncomplete()) {
    complete = 'N';
  } else if (aStatus.IsOverflowIncomplete()) {
    complete = 'O';
  }

  char brk = 'N';
  if (aStatus.IsInlineBreakBefore()) {
    brk = 'B';
  } else if (aStatus.IsInlineBreakAfter()) {
    brk = 'A';
  }

  aStream << "["
          << "Complete=" << complete << ","
          << "NIF=" << (aStatus.NextInFlowNeedsReflow() ? 'Y' : 'N') << ","
          << "Break=" << brk << ","
          << "FirstLetter=" << (aStatus.FirstLetterComplete() ? 'Y' : 'N')
          << "]";
  return aStream;
}

#ifdef DEBUG

/**
 * Note: the log module is created during library initialization which
 * means that you cannot perform logging before then.
 */
mozilla::LazyLogModule nsIFrame::sFrameLogModule("frame");

#endif

NS_DECLARE_FRAME_PROPERTY_DELETABLE(AbsoluteContainingBlockProperty,
                                    nsAbsoluteContainingBlock)

bool nsIFrame::HasAbsolutelyPositionedChildren() const {
  return IsAbsoluteContainer() &&
         GetAbsoluteContainingBlock()->HasAbsoluteFrames();
}

nsAbsoluteContainingBlock* nsIFrame::GetAbsoluteContainingBlock() const {
  NS_ASSERTION(IsAbsoluteContainer(),
               "The frame is not marked as an abspos container correctly");
  nsAbsoluteContainingBlock* absCB =
      GetProperty(AbsoluteContainingBlockProperty());
  NS_ASSERTION(absCB,
               "The frame is marked as an abspos container but doesn't have "
               "the property");
  return absCB;
}

void nsIFrame::MarkAsAbsoluteContainingBlock() {
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN));
  NS_ASSERTION(!GetProperty(AbsoluteContainingBlockProperty()),
               "Already has an abs-pos containing block property?");
  NS_ASSERTION(!HasAnyStateBits(NS_FRAME_HAS_ABSPOS_CHILDREN),
               "Already has NS_FRAME_HAS_ABSPOS_CHILDREN state bit?");
  AddStateBits(NS_FRAME_HAS_ABSPOS_CHILDREN);
  SetProperty(AbsoluteContainingBlockProperty(),
              new nsAbsoluteContainingBlock(GetAbsoluteListID()));
}

void nsIFrame::MarkAsNotAbsoluteContainingBlock() {
  NS_ASSERTION(!HasAbsolutelyPositionedChildren(), "Think of the children!");
  NS_ASSERTION(GetProperty(AbsoluteContainingBlockProperty()),
               "Should have an abs-pos containing block property");
  NS_ASSERTION(HasAnyStateBits(NS_FRAME_HAS_ABSPOS_CHILDREN),
               "Should have NS_FRAME_HAS_ABSPOS_CHILDREN state bit");
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN));
  RemoveStateBits(NS_FRAME_HAS_ABSPOS_CHILDREN);
  RemoveProperty(AbsoluteContainingBlockProperty());
}

bool nsIFrame::CheckAndClearPaintedState() {
  bool result = HasAnyStateBits(NS_FRAME_PAINTED_THEBES);
  RemoveStateBits(NS_FRAME_PAINTED_THEBES);

  for (const auto& childList : ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      if (child->CheckAndClearPaintedState()) {
        result = true;
      }
    }
  }
  return result;
}

nsIFrame* nsIFrame::FindLineContainer() const {
  MOZ_ASSERT(IsLineParticipant());
  nsIFrame* parent = GetParent();
  while (parent &&
         (parent->IsLineParticipant() || parent->CanContinueTextRun())) {
    parent = parent->GetParent();
  }
  return parent;
}

bool nsIFrame::CheckAndClearDisplayListState() {
  bool result = BuiltDisplayList();
  SetBuiltDisplayList(false);

  for (const auto& childList : ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      if (child->CheckAndClearDisplayListState()) {
        result = true;
      }
    }
  }
  return result;
}

bool nsIFrame::IsVisibleConsideringAncestors(uint32_t aFlags) const {
  if (!StyleVisibility()->IsVisible()) {
    return false;
  }

  if (PresShell()->IsUnderHiddenEmbedderElement()) {
    return false;
  }

  const nsIFrame* frame = this;
  while (frame) {
    nsView* view = frame->GetView();
    if (view && view->GetVisibility() == ViewVisibility::Hide) {
      return false;
    }

    // Checking mMozSubtreeHiddenOnlyVisually is relatively slow because it
    // involves loading more memory. It's only allowed in chrome sheets so let's
    // only support it in the parent process so we can mostly optimize this out
    // in content processes.
    if (XRE_IsParentProcess() &&
        frame->StyleUIReset()->mMozSubtreeHiddenOnlyVisually) {
      return false;
    }

    // This method is used to determine if a frame is focusable, because it's
    // called by nsIFrame::IsFocusable. `content-visibility: auto` should not
    // force this frame to be unfocusable, so we only take into account
    // `content-visibility: hidden` here.
    if (this != frame &&
        frame->HidesContent(IncludeContentVisibility::Hidden)) {
      return false;
    }

    if (nsIFrame* parent = frame->GetParent()) {
      frame = parent;
    } else {
      parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(frame);
      if (!parent) {
        break;
      }

      if ((aFlags & nsIFrame::VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY) == 0 &&
          parent->PresContext()->IsChrome() &&
          !frame->PresContext()->IsChrome()) {
        break;
      }

      frame = parent;
    }
  }

  return true;
}

void nsIFrame::FindCloserFrameForSelection(
    const nsPoint& aPoint, FrameWithDistance* aCurrentBestFrame) {
  if (nsLayoutUtils::PointIsCloserToRect(aPoint, mRect,
                                         aCurrentBestFrame->mXDistance,
                                         aCurrentBestFrame->mYDistance)) {
    aCurrentBestFrame->mFrame = this;
  }
}

void nsIFrame::ElementStateChanged(mozilla::dom::ElementState aStates) {}

void WeakFrame::Clear(mozilla::PresShell* aPresShell) {
  if (aPresShell) {
    aPresShell->RemoveWeakFrame(this);
  }
  mFrame = nullptr;
}

AutoWeakFrame::AutoWeakFrame(const WeakFrame& aOther)
    : mPrev(nullptr), mFrame(nullptr) {
  Init(aOther.GetFrame());
}

void AutoWeakFrame::Clear(mozilla::PresShell* aPresShell) {
  if (aPresShell) {
    aPresShell->RemoveAutoWeakFrame(this);
  }
  mFrame = nullptr;
  mPrev = nullptr;
}

AutoWeakFrame::~AutoWeakFrame() {
  Clear(mFrame ? mFrame->PresContext()->GetPresShell() : nullptr);
}

void AutoWeakFrame::Init(nsIFrame* aFrame) {
  Clear(mFrame ? mFrame->PresContext()->GetPresShell() : nullptr);
  mFrame = aFrame;
  if (mFrame) {
    mozilla::PresShell* presShell = mFrame->PresContext()->GetPresShell();
    NS_WARNING_ASSERTION(presShell, "Null PresShell in AutoWeakFrame!");
    if (presShell) {
      presShell->AddAutoWeakFrame(this);
    } else {
      mFrame = nullptr;
    }
  }
}

void WeakFrame::Init(nsIFrame* aFrame) {
  Clear(mFrame ? mFrame->PresContext()->GetPresShell() : nullptr);
  mFrame = aFrame;
  if (mFrame) {
    mozilla::PresShell* presShell = mFrame->PresContext()->GetPresShell();
    MOZ_ASSERT(presShell, "Null PresShell in WeakFrame!");
    if (presShell) {
      presShell->AddWeakFrame(this);
    } else {
      mFrame = nullptr;
    }
  }
}

nsIFrame* NS_NewEmptyFrame(PresShell* aPresShell, ComputedStyle* aStyle) {
  return new (aPresShell) nsIFrame(aStyle, aPresShell->GetPresContext());
}

nsIFrame::~nsIFrame() {
  MOZ_COUNT_DTOR(nsIFrame);

  MOZ_ASSERT(GetVisibility() != Visibility::ApproximatelyVisible,
             "Visible nsFrame is being destroyed");
}

NS_IMPL_FRAMEARENA_HELPERS(nsIFrame)

// Dummy operator delete.  Will never be called, but must be defined
// to satisfy some C++ ABIs.
void nsIFrame::operator delete(void*, size_t) {
  MOZ_CRASH("nsIFrame::operator delete should never be called");
}

NS_QUERYFRAME_HEAD(nsIFrame)
  NS_QUERYFRAME_ENTRY(nsIFrame)
NS_QUERYFRAME_TAIL_INHERITANCE_ROOT

/////////////////////////////////////////////////////////////////////////////
// nsIFrame

static bool IsFontSizeInflationContainer(nsIFrame* aFrame,
                                         const nsStyleDisplay* aStyleDisplay) {
  /*
   * Font size inflation is built around the idea that we're inflating
   * the fonts for a pan-and-zoom UI so that when the user scales up a
   * block or other container to fill the width of the device, the fonts
   * will be readable.  To do this, we need to pick what counts as a
   * container.
   *
   * From a code perspective, the only hard requirement is that frames
   * that are line participants (nsIFrame::IsLineParticipant) are never
   * containers, since line layout assumes that the inflation is consistent
   * within a line.
   *
   * This is not an imposition, since we obviously want a bunch of text
   * (possibly with inline elements) flowing within a block to count the
   * block (or higher) as its container.
   *
   * We also want form controls, including the text in the anonymous
   * content inside of them, to match each other and the text next to
   * them, so they and their anonymous content should also not be a
   * container.
   *
   * However, because we can't reliably compute sizes across XUL during
   * reflow, any XUL frame with a XUL parent is always a container.
   *
   * There are contexts where it would be nice if some blocks didn't
   * count as a container, so that, for example, an indented quotation
   * didn't end up with a smaller font size.  However, it's hard to
   * distinguish these situations where we really do want the indented
   * thing to count as a container, so we don't try, and blocks are
   * always containers.
   */

  // The root frame should always be an inflation container.
  if (!aFrame->GetParent()) {
    return true;
  }

  nsIContent* content = aFrame->GetContent();
  if (content && content->IsInNativeAnonymousSubtree()) {
    // Native anonymous content shouldn't be a font inflation root,
    // except for the canvas custom content container.
    nsCanvasFrame* canvas = aFrame->PresShell()->GetCanvasFrame();
    return canvas && canvas->GetCustomContentContainer() == content;
  }

  LayoutFrameType frameType = aFrame->Type();
  bool isInline =
      aFrame->GetDisplay().IsInlineFlow() || RubyUtils::IsRubyBox(frameType) ||
      (aStyleDisplay->IsFloatingStyle() &&
       frameType == LayoutFrameType::Letter) ||
      // Given multiple frames for the same node, only the
      // outer one should be considered a container.
      // (Important, e.g., for nsSelectsAreaFrame.)
      (aFrame->GetParent()->GetContent() == content) ||
      (content &&
       // Form controls shouldn't become inflation containers.
       (content->IsAnyOfHTMLElements(nsGkAtoms::option, nsGkAtoms::optgroup,
                                     nsGkAtoms::select, nsGkAtoms::input,
                                     nsGkAtoms::button, nsGkAtoms::textarea)));
  NS_ASSERTION(!aFrame->IsLineParticipant() || isInline ||
                   // br frames and mathml frames report being line
                   // participants even when their position or display is
                   // set
                   aFrame->IsBrFrame() || aFrame->IsMathMLFrame(),
               "line participants must not be containers");
  return !isInline;
}

static void MaybeScheduleReflowSVGNonDisplayText(nsIFrame* aFrame) {
  if (!aFrame->IsInSVGTextSubtree()) {
    return;
  }

  // We need to ensure that any non-display SVGTextFrames get reflowed when a
  // child text frame gets new style. Thus we need to schedule a reflow in
  // |DidSetComputedStyle|. We also need to call it from |DestroyFrom|,
  // because otherwise we won't get notified when style changes to
  // "display:none".
  SVGTextFrame* svgTextFrame = static_cast<SVGTextFrame*>(
      nsLayoutUtils::GetClosestFrameOfType(aFrame, LayoutFrameType::SVGText));
  nsIFrame* anonBlock = svgTextFrame->PrincipalChildList().FirstChild();

  // Note that we must check NS_FRAME_FIRST_REFLOW on our SVGTextFrame's
  // anonymous block frame rather than our aFrame, since NS_FRAME_FIRST_REFLOW
  // may be set on us if we're a new frame that has been inserted after the
  // document's first reflow. (In which case this DidSetComputedStyle call may
  // be happening under frame construction under a Reflow() call.)
  if (!anonBlock || anonBlock->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    return;
  }

  if (!svgTextFrame->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) ||
      svgTextFrame->HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW)) {
    return;
  }

  svgTextFrame->ScheduleReflowSVGNonDisplayText(
      IntrinsicDirty::FrameAncestorsAndDescendants);
}

bool nsIFrame::ShouldPropagateRepaintsToRoot() const {
  if (!IsPrimaryFrame()) {
    // special case for table frames because style images are associated to the
    // table frame, but the table wrapper frame is the primary frame
    if (IsTableFrame()) {
      MOZ_ASSERT(GetParent() && GetParent()->IsTableWrapperFrame());
      return GetParent()->ShouldPropagateRepaintsToRoot();
    }

    return false;
  }
  nsIContent* content = GetContent();
  Document* document = content->OwnerDoc();
  return content == document->GetRootElement() ||
         content == document->GetBodyElement();
}

bool nsIFrame::IsRenderedLegend() const {
  if (auto* parent = GetParent(); parent && parent->IsFieldSetFrame()) {
    return static_cast<nsFieldSetFrame*>(parent)->GetLegend() == this;
  }
  return false;
}

void nsIFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                    nsIFrame* aPrevInFlow) {
  MOZ_ASSERT(nsQueryFrame::FrameIID(mClass) == GetFrameId());
  MOZ_ASSERT(!mContent, "Double-initing a frame?");

  mContent = aContent;
  mParent = aParent;
  MOZ_ASSERT(!mParent || PresShell() == mParent->PresShell());

  if (aPrevInFlow) {
    mWritingMode = aPrevInFlow->GetWritingMode();

    // Copy some state bits from prev-in-flow (the bits that should apply
    // throughout a continuation chain). The bits are sorted according to their
    // order in nsFrameStateBits.h.

    // clang-format off
    AddStateBits(aPrevInFlow->GetStateBits() &
                 (NS_FRAME_GENERATED_CONTENT |
                  NS_FRAME_OUT_OF_FLOW |
                  NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN |
                  NS_FRAME_INDEPENDENT_SELECTION |
                  NS_FRAME_PART_OF_IBSPLIT |
                  NS_FRAME_MAY_BE_TRANSFORMED |
                  NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR));
    // clang-format on

    // Copy other bits in nsIFrame from prev-in-flow.
    mHasColumnSpanSiblings = aPrevInFlow->HasColumnSpanSiblings();
  } else {
    PresContext()->ConstructedFrame();
  }

  if (GetParent()) {
    if (MOZ_UNLIKELY(mContent == PresContext()->Document()->GetRootElement() &&
                     mContent == GetParent()->GetContent())) {
      // Our content is the root element and we have the same content as our
      // parent. That is, we are the internal anonymous frame of the root
      // element. Copy the used mWritingMode from our parent because
      // mDocElementContainingBlock gets its mWritingMode from <body>.
      mWritingMode = GetParent()->GetWritingMode();
    }

    // Copy some state bits from our parent (the bits that should apply
    // recursively throughout a subtree). The bits are sorted according to their
    // order in nsFrameStateBits.h.

    // clang-format off
    AddStateBits(GetParent()->GetStateBits() &
                 (NS_FRAME_GENERATED_CONTENT |
                  NS_FRAME_INDEPENDENT_SELECTION |
                  NS_FRAME_IS_SVG_TEXT |
                  NS_FRAME_IN_POPUP |
                  NS_FRAME_IS_NONDISPLAY));
    // clang-format on

    if (HasAnyStateBits(NS_FRAME_IN_POPUP) && TrackingVisibility()) {
      // Assume all frames in popups are visible.
      IncApproximateVisibleCount();
    }
  }
  if (aPrevInFlow) {
    mMayHaveOpacityAnimation = aPrevInFlow->MayHaveOpacityAnimation();
    mMayHaveTransformAnimation = aPrevInFlow->MayHaveTransformAnimation();
  } else if (mContent) {
    // It's fine to fetch the EffectSet for the style frame here because in the
    // following code we take care of the case where animations may target
    // a different frame.
    EffectSet* effectSet = EffectSet::GetForStyleFrame(this);
    if (effectSet) {
      mMayHaveOpacityAnimation = effectSet->MayHaveOpacityAnimation();

      if (effectSet->MayHaveTransformAnimation()) {
        // If we are the inner table frame for display:table content, then
        // transform animations should go on our parent frame (the table wrapper
        // frame).
        //
        // We do this when initializing the child frame (table inner frame),
        // because when initializng the table wrapper frame, we don't yet have
        // access to its children so we can't tell if we have transform
        // animations or not.
        if (SupportsCSSTransforms()) {
          mMayHaveTransformAnimation = true;
          AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
        } else if (aParent && nsLayoutUtils::GetStyleFrame(aParent) == this) {
          MOZ_ASSERT(
              aParent->SupportsCSSTransforms(),
              "Style frames that don't support transforms should have parents"
              " that do");
          aParent->mMayHaveTransformAnimation = true;
          aParent->AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
        }
      }
    }
  }

  const nsStyleDisplay* disp = StyleDisplay();
  if (disp->HasTransform(this)) {
    // If 'transform' dynamically changes, RestyleManager takes care of
    // updating this bit.
    AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
  }

  if (nsLayoutUtils::FontSizeInflationEnabled(PresContext()) ||
      !GetParent()
#ifdef DEBUG
      // We have assertions that check inflation invariants even when
      // font size inflation is not enabled.
      || true
#endif
  ) {
    if (IsFontSizeInflationContainer(this, disp)) {
      AddStateBits(NS_FRAME_FONT_INFLATION_CONTAINER);
      if (!GetParent() ||
          // I'd use NS_FRAME_OUT_OF_FLOW, but it's not set yet.
          disp->IsFloating(this) || disp->IsAbsolutelyPositioned(this) ||
          GetParent()->IsFlexContainerFrame() ||
          GetParent()->IsGridContainerFrame()) {
        AddStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT);
      }
    }
    NS_ASSERTION(
        GetParent() || HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER),
        "root frame should always be a container");
  }

  if (TrackingVisibility() && PresShell()->AssumeAllFramesVisible()) {
    IncApproximateVisibleCount();
  }

  DidSetComputedStyle(nullptr);

  // For a newly created frame, we need to update this frame's visibility state.
  // Usually we update the state when the frame is restyled and has a
  // VisibilityChange change hint but we don't generate any change hints for
  // newly created frames.
  // Note: We don't need to do this for placeholders since placeholders have
  // different styles so that the styles don't have visibility:hidden even if
  // the parent has visibility:hidden style. We also don't need to update the
  // state when creating continuations because its visibility is the same as its
  // prev-in-flow, and the animation code cares only primary frames.
  if (!IsPlaceholderFrame() && !aPrevInFlow) {
    UpdateVisibleDescendantsState();
  }

  if (!aPrevInFlow && HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // We aren't going to get a reflow, so nothing else will call
    // InvalidateRenderingObservers, we have to do it here.
    SVGObserverUtils::InvalidateRenderingObservers(this);
  }
}

void nsIFrame::InitPrimaryFrame() {
  MOZ_ASSERT(IsPrimaryFrame());
  HandlePrimaryFrameStyleChange(nullptr);
}

void nsIFrame::HandlePrimaryFrameStyleChange(ComputedStyle* aOldStyle) {
  const nsStyleDisplay* disp = StyleDisplay();
  const nsStyleDisplay* oldDisp =
      aOldStyle ? aOldStyle->StyleDisplay() : nullptr;

  const bool wasQueryContainer = oldDisp && oldDisp->IsQueryContainer();
  const bool isQueryContainer = disp->IsQueryContainer();
  if (wasQueryContainer != isQueryContainer) {
    auto* pc = PresContext();
    if (isQueryContainer) {
      pc->RegisterContainerQueryFrame(this);
    } else {
      pc->UnregisterContainerQueryFrame(this);
    }
  }

  const auto cv = disp->ContentVisibility(*this);
  if (!oldDisp || oldDisp->ContentVisibility(*this) != cv) {
    if (cv == StyleContentVisibility::Auto) {
      PresShell()->RegisterContentVisibilityAutoFrame(this);
    } else {
      if (auto* element = Element::FromNodeOrNull(GetContent())) {
        element->ClearContentRelevancy();
      }
      PresShell()->UnregisterContentVisibilityAutoFrame(this);
    }
    PresContext()->SetNeedsToUpdateHiddenByContentVisibilityForAnimations();
  }

  HandleLastRememberedSize();
}

void nsIFrame::Destroy(DestroyContext& aContext) {
  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "destroy called on frame while scripts not blocked");
  NS_ASSERTION(!GetNextSibling() && !GetPrevSibling(),
               "Frames should be removed before destruction.");
  MOZ_ASSERT(!HasAbsolutelyPositionedChildren());
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT),
             "NS_FRAME_PART_OF_IBSPLIT set on non-nsContainerFrame?");

  MaybeScheduleReflowSVGNonDisplayText(this);

  SVGObserverUtils::InvalidateDirectRenderingObservers(
      this, SVGObserverUtils::INVALIDATE_DESTROY);

  const auto* disp = StyleDisplay();
  if (disp->mPosition == StylePositionProperty::Sticky) {
    if (auto* ssc =
            StickyScrollContainer::GetStickyScrollContainerForFrame(this)) {
      ssc->RemoveFrame(this);
    }
  }

  if (HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    if (nsPlaceholderFrame* placeholder = GetPlaceholderFrame()) {
      placeholder->SetOutOfFlowFrame(nullptr);
    }
  }

  nsPresContext* pc = PresContext();
  mozilla::PresShell* ps = pc->GetPresShell();
  if (IsPrimaryFrame()) {
    if (disp->IsQueryContainer()) {
      pc->UnregisterContainerQueryFrame(this);
    }
    if (disp->ContentVisibility(*this) == StyleContentVisibility::Auto) {
      ps->UnregisterContentVisibilityAutoFrame(this);
    }
    // This needs to happen before we clear our Properties() table.
    ActiveLayerTracker::TransferActivityToContent(this, mContent);
  }

  ScrollAnchorContainer* anchor = nullptr;
  if (IsScrollAnchor(&anchor)) {
    anchor->InvalidateAnchor();
  }

  if (HasCSSAnimations() || HasCSSTransitions() ||
      // It's fine to look up the style frame here since if we're destroying the
      // frames for display:table content we should be destroying both wrapper
      // and inner frame.
      EffectSet::GetForStyleFrame(this)) {
    // If no new frame for this element is created by the end of the
    // restyling process, stop animations and transitions for this frame
    RestyleManager::AnimationsWithDestroyedFrame* adf =
        pc->RestyleManager()->GetAnimationsWithDestroyedFrame();
    // AnimationsWithDestroyedFrame only lives during the restyling process.
    if (adf) {
      adf->Put(mContent, mComputedStyle);
    }
  }

  // Disable visibility tracking. Note that we have to do this before we clear
  // frame properties and lose track of whether we were previously visible.
  // XXX(seth): It'd be ideal to assert that we're already marked nonvisible
  // here, but it's unfortunately tricky to guarantee in the face of things like
  // frame reconstruction induced by style changes.
  DisableVisibilityTracking();

  // Ensure that we're not in the approximately visible list anymore.
  ps->RemoveFrameFromApproximatelyVisibleList(this);

  ps->NotifyDestroyingFrame(this);

  if (HasAnyStateBits(NS_FRAME_EXTERNAL_REFERENCE)) {
    ps->ClearFrameRefs(this);
  }

  nsView* view = GetView();
  if (view) {
    view->SetFrame(nullptr);
    view->Destroy();
  }

  // Make sure that our deleted frame can't be returned from GetPrimaryFrame()
  if (IsPrimaryFrame()) {
    mContent->SetPrimaryFrame(nullptr);

    // Pass the root of a generated content subtree (e.g. ::after/::before) to
    // aPostDestroyData to unbind it after frame destruction is done.
    if (HasAnyStateBits(NS_FRAME_GENERATED_CONTENT) &&
        mContent->IsRootOfNativeAnonymousSubtree()) {
      aContext.AddAnonymousContent(mContent.forget());
    }
  }

  // Remove all properties attached to the frame, to ensure any property
  // destructors that need the frame pointer are handled properly.
  RemoveAllProperties();

  // Must retrieve the object ID before calling destructors, so the
  // vtable is still valid.
  //
  // Note to future tweakers: having the method that returns the
  // object size call the destructor will not avoid an indirect call;
  // the compiler cannot devirtualize the call to the destructor even
  // if it's from a method defined in the same class.

  nsQueryFrame::FrameIID id = GetFrameId();
  this->~nsIFrame();

#ifdef DEBUG
  {
    nsIFrame* rootFrame = ps->GetRootFrame();
    MOZ_ASSERT(rootFrame);
    if (this != rootFrame) {
      auto* builder = nsLayoutUtils::GetRetainedDisplayListBuilder(rootFrame);
      auto* data = builder ? builder->Data() : nullptr;

      const bool inData =
          data && (data->IsModified(this) || data->HasProps(this));

      if (inData) {
        DL_LOG(LogLevel::Warning, "Frame %p found in retained data", this);
      }

      MOZ_ASSERT(!inData, "Deleted frame in retained data!");
    }
  }
#endif

  // Now that we're totally cleaned out, we need to add ourselves to
  // the presshell's recycler.
  ps->FreeFrame(id, this);
}

std::pair<int32_t, int32_t> nsIFrame::GetOffsets() const {
  return std::make_pair(0, 0);
}

static void CompareLayers(
    const nsStyleImageLayers* aFirstLayers,
    const nsStyleImageLayers* aSecondLayers,
    const std::function<void(imgRequestProxy* aReq)>& aCallback) {
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT(i, (*aFirstLayers)) {
    const auto& image = aFirstLayers->mLayers[i].mImage;
    if (!image.IsImageRequestType() || !image.IsResolved()) {
      continue;
    }

    // aCallback is called when the style image in aFirstLayers is thought to
    // be different with the corresponded one in aSecondLayers
    if (!aSecondLayers || i >= aSecondLayers->mImageCount ||
        (!aSecondLayers->mLayers[i].mImage.IsResolved() ||
         image.GetImageRequest() !=
             aSecondLayers->mLayers[i].mImage.GetImageRequest())) {
      if (imgRequestProxy* req = image.GetImageRequest()) {
        aCallback(req);
      }
    }
  }
}

static void AddAndRemoveImageAssociations(
    ImageLoader& aImageLoader, nsIFrame* aFrame,
    const nsStyleImageLayers* aOldLayers,
    const nsStyleImageLayers* aNewLayers) {
  // If the old context had a background-image image, or mask-image image,
  // and new context does not have the same image, clear the image load
  // notifier (which keeps the image loading, if it still is) for the frame.
  // We want to do this conservatively because some frames paint their
  // backgrounds from some other frame's style data, and we don't want
  // to clear those notifiers unless we have to.  (They'll be reset
  // when we paint, although we could miss a notification in that
  // interval.)
  if (aOldLayers && aFrame->HasImageRequest()) {
    CompareLayers(aOldLayers, aNewLayers, [&](imgRequestProxy* aReq) {
      aImageLoader.DisassociateRequestFromFrame(aReq, aFrame);
    });
  }

  CompareLayers(aNewLayers, aOldLayers, [&](imgRequestProxy* aReq) {
    aImageLoader.AssociateRequestToFrame(aReq, aFrame);
  });
}

void nsIFrame::AddDisplayItem(nsDisplayItem* aItem) {
  MOZ_DIAGNOSTIC_ASSERT(!mDisplayItems.Contains(aItem));
  mDisplayItems.AppendElement(aItem);
#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->NotifyOfPossibleBoundsChange(PresShell(), mContent);
  }
#endif
}

bool nsIFrame::RemoveDisplayItem(nsDisplayItem* aItem) {
  return mDisplayItems.RemoveElement(aItem);
}

bool nsIFrame::HasDisplayItems() { return !mDisplayItems.IsEmpty(); }

bool nsIFrame::HasDisplayItem(nsDisplayItem* aItem) {
  return mDisplayItems.Contains(aItem);
}

bool nsIFrame::HasDisplayItem(uint32_t aKey) {
  for (nsDisplayItem* i : mDisplayItems) {
    if (i->GetPerFrameKey() == aKey) {
      return true;
    }
  }
  return false;
}

template <typename Condition>
static void DiscardDisplayItems(nsIFrame* aFrame, Condition aCondition) {
  for (nsDisplayItem* i : aFrame->DisplayItems()) {
    // Only discard items that are invalidated by this frame, as we're only
    // guaranteed to rebuild those items. Table background items are created by
    // the relevant table part, but have the cell frame as the primary frame,
    // and we don't want to remove them if this is the cell.
    if (aCondition(i) && i->FrameForInvalidation() == aFrame) {
      i->SetCantBeReused();
    }
  }
}

static void DiscardOldItems(nsIFrame* aFrame) {
  DiscardDisplayItems(aFrame,
                      [](nsDisplayItem* aItem) { return aItem->IsOldItem(); });
}

void nsIFrame::RemoveDisplayItemDataForDeletion() {
  // Destroying a WebRenderUserDataTable can cause destruction of other objects
  // which can remove frame properties in their destructor. If we delete a frame
  // property it runs the destructor of the stored object in the middle of
  // updating the frame property table, so if the destruction of that object
  // causes another update to the frame property table it would leave the frame
  // property table in an inconsistent state. So we remove it from the table and
  // then destroy it. (bug 1530657)
  WebRenderUserDataTable* userDataTable =
      TakeProperty(WebRenderUserDataProperty::Key());
  if (userDataTable) {
    for (const auto& data : userDataTable->Values()) {
      data->RemoveFromTable();
    }
    delete userDataTable;
  }

  auto* builder = nsLayoutUtils::GetRetainedDisplayListBuilder(this);
  if (!builder) {
    MOZ_ASSERT(DisplayItems().IsEmpty());
    MOZ_ASSERT(!IsFrameModified());
    return;
  }

  for (nsDisplayItem* i : DisplayItems()) {
    if (i->GetDependentFrame() == this && !i->HasDeletedFrame()) {
      i->Frame()->MarkNeedsDisplayItemRebuild();
    }
    i->RemoveFrame(this);
  }

  DisplayItems().Clear();

  nsAutoString name;
#ifdef DEBUG_FRAME_DUMP
  if (DL_LOG_TEST(LogLevel::Debug)) {
    GetFrameName(name);
  }
#endif
  DL_LOGV("Removing display item data for frame %p (%s)", this,
          NS_ConvertUTF16toUTF8(name).get());

  auto* data = builder->Data();
  if (MayHaveWillChangeBudget()) {
    // Keep the frame in list, so it can be removed from the will-change budget.
    data->Flags(this) = RetainedDisplayListData::FrameFlag::HadWillChange;
  } else {
    data->Remove(this);
  }
}

void nsIFrame::MarkNeedsDisplayItemRebuild() {
  if (!nsLayoutUtils::AreRetainedDisplayListsEnabled() || IsFrameModified() ||
      HasAnyStateBits(NS_FRAME_IN_POPUP)) {
    // Skip frames that are already marked modified.
    return;
  }

  if (Type() == LayoutFrameType::Placeholder) {
    nsIFrame* oof = static_cast<nsPlaceholderFrame*>(this)->GetOutOfFlowFrame();
    if (oof) {
      oof->MarkNeedsDisplayItemRebuild();
    }
    // Do not mark placeholder frames modified.
    return;
  }

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->NotifyOfPossibleBoundsChange(PresShell(), mContent);
  }
#endif

  nsIFrame* rootFrame = PresShell()->GetRootFrame();

  if (rootFrame->IsFrameModified()) {
    // The whole frame tree is modified.
    return;
  }

  auto* builder = nsLayoutUtils::GetRetainedDisplayListBuilder(this);
  if (!builder) {
    MOZ_ASSERT(DisplayItems().IsEmpty());
    return;
  }

  RetainedDisplayListData* data = builder->Data();
  MOZ_ASSERT(data);

  if (data->AtModifiedFrameLimit()) {
    // This marks the whole frame tree modified.
    // See |RetainedDisplayListBuilder::ShouldBuildPartial()|.
    data->AddModifiedFrame(rootFrame);
    return;
  }

  nsAutoString name;
#ifdef DEBUG_FRAME_DUMP
  if (DL_LOG_TEST(LogLevel::Debug)) {
    GetFrameName(name);
  }
#endif

  DL_LOGV("RDL - Rebuilding display items for frame %p (%s)", this,
          NS_ConvertUTF16toUTF8(name).get());

  data->AddModifiedFrame(this);

  MOZ_ASSERT(
      PresContext()->LayoutPhaseCount(nsLayoutPhase::DisplayListBuilding) == 0);

  // Hopefully this is cheap, but we could use a frame state bit to note
  // the presence of dependencies to speed it up.
  for (nsDisplayItem* i : DisplayItems()) {
    if (i->HasDeletedFrame() || i->Frame() == this) {
      // Ignore the items with deleted frames, and the items with |this| as
      // the primary frame.
      continue;
    }

    if (i->GetDependentFrame() == this) {
      // For items with |this| as a dependent frame, mark the primary frame
      // for rebuild.
      i->Frame()->MarkNeedsDisplayItemRebuild();
    }
  }
}

// Subclass hook for style post processing
/* virtual */
void nsIFrame::DidSetComputedStyle(ComputedStyle* aOldComputedStyle) {
#ifdef ACCESSIBILITY
  // Don't notify for reconstructed frames here, since the frame is still being
  // constructed at this point and so LocalAccessible::GetFrame() will return
  // null. Style changes for reconstructed frames are handled in
  // DocAccessible::PruneOrInsertSubtree.
  if (aOldComputedStyle) {
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->NotifyOfComputedStyleChange(PresShell(), mContent);
    }
  }
#endif

  MaybeScheduleReflowSVGNonDisplayText(this);

  Document* doc = PresContext()->Document();
  ImageLoader* loader = doc->StyleImageLoader();
  // Continuing text frame doesn't initialize its continuation pointer before
  // reaching here for the first time, so we have to exclude text frames. This
  // doesn't affect correctness because text can't match selectors.
  //
  // FIXME(emilio): We should consider fixing that.
  //
  // TODO(emilio): Can we avoid doing some / all of the image stuff when
  // isNonTextFirstContinuation is false? We should consider doing this just for
  // primary frames and pseudos, but the first-line reparenting code makes it
  // all bad, should get around to bug 1465474 eventually :(
  const bool isNonText = !IsTextFrame();
  if (isNonText) {
    mComputedStyle->StartImageLoads(*doc, aOldComputedStyle);
  }

  const bool isRootElementStyle = Style()->IsRootElementStyle();
  if (isRootElementStyle) {
    PresShell()->SyncWindowProperties(/* aSync = */ false);
  }

  const nsStyleImageLayers* oldLayers =
      aOldComputedStyle ? &aOldComputedStyle->StyleBackground()->mImage
                        : nullptr;
  const nsStyleImageLayers* newLayers = &StyleBackground()->mImage;
  AddAndRemoveImageAssociations(*loader, this, oldLayers, newLayers);

  oldLayers =
      aOldComputedStyle ? &aOldComputedStyle->StyleSVGReset()->mMask : nullptr;
  newLayers = &StyleSVGReset()->mMask;
  AddAndRemoveImageAssociations(*loader, this, oldLayers, newLayers);

  const nsStyleDisplay* disp = StyleDisplay();
  bool handleStickyChange = false;
  if (aOldComputedStyle) {
    // Detect style changes that should trigger a scroll anchor adjustment
    // suppression.
    // https://drafts.csswg.org/css-scroll-anchoring/#suppression-triggers
    bool needScrollAnchorSuppression = false;

    const nsStyleMargin* oldMargin = aOldComputedStyle->StyleMargin();
    if (!oldMargin->MarginEquals(*StyleMargin())) {
      needScrollAnchorSuppression = true;
    }

    const nsStylePadding* oldPadding = aOldComputedStyle->StylePadding();
    if (oldPadding->mPadding != StylePadding()->mPadding) {
      SetHasPaddingChange(true);
      needScrollAnchorSuppression = true;
    }

    const nsStyleDisplay* oldDisp = aOldComputedStyle->StyleDisplay();
    if (oldDisp->mOverflowAnchor != disp->mOverflowAnchor) {
      if (auto* container = ScrollAnchorContainer::FindFor(this)) {
        container->InvalidateAnchor();
      }
      if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(this)) {
        scrollContainerFrame->Anchor()->InvalidateAnchor();
      }
    }

    if (mInScrollAnchorChain) {
      const nsStylePosition* pos = StylePosition();
      const nsStylePosition* oldPos = aOldComputedStyle->StylePosition();
      if (!needScrollAnchorSuppression &&
          (oldPos->mOffset != pos->mOffset || oldPos->mWidth != pos->mWidth ||
           oldPos->mMinWidth != pos->mMinWidth ||
           oldPos->mMaxWidth != pos->mMaxWidth ||
           oldPos->mHeight != pos->mHeight ||
           oldPos->mMinHeight != pos->mMinHeight ||
           oldPos->mMaxHeight != pos->mMaxHeight ||
           oldDisp->mPosition != disp->mPosition ||
           oldDisp->mTransform != disp->mTransform)) {
        needScrollAnchorSuppression = true;
      }

      if (needScrollAnchorSuppression &&
          StaticPrefs::layout_css_scroll_anchoring_suppressions_enabled()) {
        ScrollAnchorContainer::FindFor(this)->SuppressAdjustments();
      }
    }

    if (disp->mPosition != oldDisp->mPosition) {
      if (!disp->IsRelativelyOrStickyPositionedStyle() &&
          oldDisp->IsRelativelyOrStickyPositionedStyle()) {
        RemoveProperty(NormalPositionProperty());
      }

      handleStickyChange = disp->mPosition == StylePositionProperty::Sticky ||
                           oldDisp->mPosition == StylePositionProperty::Sticky;
    }
    if (disp->mScrollSnapAlign != oldDisp->mScrollSnapAlign) {
      ScrollSnapUtils::PostPendingResnapFor(this);
    }
    if (isRootElementStyle &&
        disp->mScrollSnapType != oldDisp->mScrollSnapType) {
      if (ScrollContainerFrame* sf =
              PresShell()->GetRootScrollContainerFrame()) {
        sf->PostPendingResnap();
      }
    }
    if (StyleUIReset()->mMozSubtreeHiddenOnlyVisually &&
        !aOldComputedStyle->StyleUIReset()->mMozSubtreeHiddenOnlyVisually) {
      PresShell::ClearMouseCapture(this);
    }
  } else {  // !aOldComputedStyle
    handleStickyChange = disp->mPosition == StylePositionProperty::Sticky;
  }

  if (handleStickyChange && !HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) &&
      !GetPrevInFlow()) {
    // Note that we only add first continuations, but we really only
    // want to add first continuation-or-ib-split-siblings. But since we don't
    // yet know if we're a later part of a block-in-inline split, we'll just
    // add later members of a block-in-inline split here, and then
    // StickyScrollContainer will remove them later.
    if (auto* ssc =
            StickyScrollContainer::GetStickyScrollContainerForFrame(this)) {
      if (disp->mPosition == StylePositionProperty::Sticky) {
        ssc->AddFrame(this);
      } else {
        ssc->RemoveFrame(this);
      }
    }
  }

  imgIRequest* oldBorderImage =
      aOldComputedStyle
          ? aOldComputedStyle->StyleBorder()->GetBorderImageRequest()
          : nullptr;
  imgIRequest* newBorderImage = StyleBorder()->GetBorderImageRequest();
  // FIXME (Bug 759996): The following is no longer true.
  // For border-images, we can't be as conservative (we need to set the
  // new loaders if there has been any change) since the CalcDifference
  // call depended on the result of GetComputedBorder() and that result
  // depends on whether the image has loaded, start the image load now
  // so that we'll get notified when it completes loading and can do a
  // restyle.  Otherwise, the image might finish loading from the
  // network before we start listening to its notifications, and then
  // we'll never know that it's finished loading.  Likewise, we want to
  // do this for freshly-created frames to prevent a similar race if the
  // image loads between reflow (which can depend on whether the image
  // is loaded) and paint.  We also don't really care about any callers who try
  // to paint borders with a different style, because they won't have the
  // correct size for the border either.
  if (oldBorderImage != newBorderImage) {
    // stop and restart the image loading/notification
    if (oldBorderImage && HasImageRequest()) {
      loader->DisassociateRequestFromFrame(oldBorderImage, this);
    }
    if (newBorderImage) {
      loader->AssociateRequestToFrame(newBorderImage, this);
    }
  }

  auto GetShapeImageRequest = [](const ComputedStyle* aStyle) -> imgIRequest* {
    if (!aStyle) {
      return nullptr;
    }
    auto& shape = aStyle->StyleDisplay()->mShapeOutside;
    if (!shape.IsImage()) {
      return nullptr;
    }
    return shape.AsImage().GetImageRequest();
  };

  imgIRequest* oldShapeImage = GetShapeImageRequest(aOldComputedStyle);
  imgIRequest* newShapeImage = GetShapeImageRequest(Style());
  if (oldShapeImage != newShapeImage) {
    if (oldShapeImage && HasImageRequest()) {
      loader->DisassociateRequestFromFrame(oldShapeImage, this);
    }
    if (newShapeImage) {
      loader->AssociateRequestToFrame(
          newShapeImage, this,
          ImageLoader::Flags::
              RequiresReflowOnFirstFrameCompleteAndLoadEventBlocking);
    }
  }

  // SVGObserverUtils::GetEffectProperties() asserts that we only invoke it with
  // the first continuation so we need to check that in advance.
  const bool isNonTextFirstContinuation = isNonText && !GetPrevContinuation();
  if (isNonTextFirstContinuation) {
    // Kick off loading of external SVG resources referenced from properties if
    // any. This currently includes filter, clip-path, and mask.
    SVGObserverUtils::InitiateResourceDocLoads(this);
  }

  // If the page contains markup that overrides text direction, and
  // does not contain any characters that would activate the Unicode
  // bidi algorithm, we need to call |SetBidiEnabled| on the pres
  // context before reflow starts.  See bug 115921.
  if (StyleVisibility()->mDirection == StyleDirection::Rtl) {
    PresContext()->SetBidiEnabled();
  }

  // The following part is for caching offset-path:path(). We cache the
  // flatten gfx path, so we don't have to rebuild and re-flattern it at
  // each cycle if we have animations on offset-* with a fixed offset-path.
  const StyleOffsetPath* oldPath =
      aOldComputedStyle ? &aOldComputedStyle->StyleDisplay()->mOffsetPath
                        : nullptr;
  const StyleOffsetPath& newPath = StyleDisplay()->mOffsetPath;
  if (!oldPath || *oldPath != newPath) {
    // FIXME: Bug 1837042. Cache all basic shapes.
    if (newPath.IsPath()) {
      RefPtr<gfx::PathBuilder> builder = MotionPathUtils::GetPathBuilder();
      RefPtr<gfx::Path> path =
          MotionPathUtils::BuildSVGPath(newPath.AsSVGPathData(), builder);
      if (path) {
        // The newPath could be path('') (i.e. empty path), so its gfx path
        // could be nullptr, and so we only set property for a non-empty path.
        SetProperty(nsIFrame::OffsetPathCache(), path.forget().take());
      } else {
        // May have an old cached path, so we have to delete it.
        RemoveProperty(nsIFrame::OffsetPathCache());
      }
    } else if (oldPath) {
      RemoveProperty(nsIFrame::OffsetPathCache());
    }
  }

  if (IsPrimaryFrame()) {
    MOZ_ASSERT(aOldComputedStyle);
    HandlePrimaryFrameStyleChange(aOldComputedStyle);
  }

  RemoveStateBits(NS_FRAME_SIMPLE_EVENT_REGIONS | NS_FRAME_SIMPLE_DISPLAYLIST);

  mMayHaveRoundedCorners = true;
}

void nsIFrame::HandleLastRememberedSize() {
  MOZ_ASSERT(IsPrimaryFrame());
  // Storing a last remembered size requires contain-intrinsic-size.
  if (!StaticPrefs::layout_css_contain_intrinsic_size_enabled()) {
    return;
  }
  auto* element = Element::FromNodeOrNull(mContent);
  if (!element) {
    return;
  }
  const WritingMode wm = GetWritingMode();
  const nsStylePosition* stylePos = StylePosition();
  bool canRememberBSize = stylePos->ContainIntrinsicBSize(wm).HasAuto();
  bool canRememberISize = stylePos->ContainIntrinsicISize(wm).HasAuto();
  if (!canRememberBSize) {
    element->RemoveLastRememberedBSize();
  }
  if (!canRememberISize) {
    element->RemoveLastRememberedISize();
  }
  if ((canRememberBSize || canRememberISize) && !HidesContent()) {
    bool isNonReplacedInline = IsLineParticipant() && !IsReplaced();
    if (!isNonReplacedInline) {
      PresContext()->Document()->ObserveForLastRememberedSize(*element);
      return;
    }
  }
  PresContext()->Document()->UnobserveForLastRememberedSize(*element);
}

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
void nsIFrame::AssertNewStyleIsSane(ComputedStyle& aNewStyle) {
  MOZ_DIAGNOSTIC_ASSERT(
      aNewStyle.GetPseudoType() == mComputedStyle->GetPseudoType() ||
      // ::first-line continuations are weird, this should probably be fixed via
      // bug 1465474.
      (mComputedStyle->GetPseudoType() == PseudoStyleType::firstLine &&
       aNewStyle.GetPseudoType() == PseudoStyleType::mozLineFrame) ||
      // ::first-letter continuations are broken, in particular floating ones,
      // see bug 1490281. The construction code tries to fix this up after the
      // fact, then restyling undoes it...
      (mComputedStyle->GetPseudoType() == PseudoStyleType::mozText &&
       aNewStyle.GetPseudoType() == PseudoStyleType::firstLetterContinuation) ||
      (mComputedStyle->GetPseudoType() ==
           PseudoStyleType::firstLetterContinuation &&
       aNewStyle.GetPseudoType() == PseudoStyleType::mozText));
}
#endif

void nsIFrame::ReparentFrameViewTo(nsViewManager* aViewManager,
                                   nsView* aNewParentView) {
  if (HasView()) {
    if (IsMenuPopupFrame()) {
      // This view must be parented by the root view, don't reparent it.
      return;
    }
    nsView* view = GetView();
    aViewManager->RemoveChild(view);

    // The view will remember the Z-order and other attributes that have been
    // set on it.
    nsView* insertBefore =
        nsLayoutUtils::FindSiblingViewFor(aNewParentView, this);
    aViewManager->InsertChild(aNewParentView, view, insertBefore,
                              insertBefore != nullptr);
  } else if (HasAnyStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW)) {
    for (const auto& childList : ChildLists()) {
      // Iterate the child frames, and check each child frame to see if it has
      // a view
      for (nsIFrame* child : childList.mList) {
        child->ReparentFrameViewTo(aViewManager, aNewParentView);
      }
    }
  }
}

void nsIFrame::SyncFrameViewProperties(nsView* aView) {
  if (!aView) {
    aView = GetView();
    if (!aView) {
      return;
    }
  }

  nsViewManager* vm = aView->GetViewManager();

  // Make sure visibility is correct. This only affects nsSubDocumentFrame.
  if (!SupportsVisibilityHidden()) {
    // See if the view should be hidden or visible
    ComputedStyle* sc = Style();
    vm->SetViewVisibility(aView, sc->StyleVisibility()->IsVisible()
                                     ? ViewVisibility::Show
                                     : ViewVisibility::Hide);
  }
}

/* virtual */
nsMargin nsIFrame::GetUsedMargin() const {
  nsMargin margin;
  if (((mState & NS_FRAME_FIRST_REFLOW) && !(mState & NS_FRAME_IN_REFLOW)) ||
      IsInSVGTextSubtree()) {
    return margin;
  }

  if (nsMargin* m = GetProperty(UsedMarginProperty())) {
    margin = *m;
  } else if (!StyleMargin()->GetMargin(margin)) {
    // If we get here, our caller probably shouldn't be calling us...
    NS_ERROR(
        "Returning bogus 0-sized margin, because this margin "
        "depends on layout & isn't cached!");
  }
  return margin;
}

/* virtual */
nsMargin nsIFrame::GetUsedBorder() const {
  if (((mState & NS_FRAME_FIRST_REFLOW) && !(mState & NS_FRAME_IN_REFLOW)) ||
      IsInSVGTextSubtree()) {
    return {};
  }

  const nsStyleDisplay* disp = StyleDisplay();
  if (IsThemed(disp)) {
    // Theme methods don't use const-ness.
    auto* mutable_this = const_cast<nsIFrame*>(this);
    nsPresContext* pc = PresContext();
    LayoutDeviceIntMargin widgetBorder = pc->Theme()->GetWidgetBorder(
        pc->DeviceContext(), mutable_this, disp->EffectiveAppearance());
    return LayoutDevicePixel::ToAppUnits(widgetBorder,
                                         pc->AppUnitsPerDevPixel());
  }

  return StyleBorder()->GetComputedBorder();
}

/* virtual */
nsMargin nsIFrame::GetUsedPadding() const {
  nsMargin padding;
  if (((mState & NS_FRAME_FIRST_REFLOW) && !(mState & NS_FRAME_IN_REFLOW)) ||
      IsInSVGTextSubtree()) {
    return padding;
  }

  const nsStyleDisplay* disp = StyleDisplay();
  if (IsThemed(disp)) {
    // Theme methods don't use const-ness.
    nsIFrame* mutable_this = const_cast<nsIFrame*>(this);
    nsPresContext* pc = PresContext();
    LayoutDeviceIntMargin widgetPadding;
    if (pc->Theme()->GetWidgetPadding(pc->DeviceContext(), mutable_this,
                                      disp->EffectiveAppearance(),
                                      &widgetPadding)) {
      return LayoutDevicePixel::ToAppUnits(widgetPadding,
                                           pc->AppUnitsPerDevPixel());
    }
  }

  if (nsMargin* p = GetProperty(UsedPaddingProperty())) {
    padding = *p;
  } else if (!StylePadding()->GetPadding(padding)) {
    // If we get here, our caller probably shouldn't be calling us...
    NS_ERROR(
        "Returning bogus 0-sized padding, because this padding "
        "depends on layout & isn't cached!");
  }
  return padding;
}

nsIFrame::Sides nsIFrame::GetSkipSides() const {
  if (MOZ_UNLIKELY(StyleBorder()->mBoxDecorationBreak ==
                   StyleBoxDecorationBreak::Clone) &&
      !HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
    return Sides();
  }

  // Convert the logical skip sides to physical sides using the frame's
  // writing mode
  WritingMode writingMode = GetWritingMode();
  LogicalSides logicalSkip = GetLogicalSkipSides();
  Sides skip;

  if (logicalSkip.BStart()) {
    if (writingMode.IsVertical()) {
      skip |= writingMode.IsVerticalLR() ? SideBits::eLeft : SideBits::eRight;
    } else {
      skip |= SideBits::eTop;
    }
  }

  if (logicalSkip.BEnd()) {
    if (writingMode.IsVertical()) {
      skip |= writingMode.IsVerticalLR() ? SideBits::eRight : SideBits::eLeft;
    } else {
      skip |= SideBits::eBottom;
    }
  }

  if (logicalSkip.IStart()) {
    if (writingMode.IsVertical()) {
      skip |= SideBits::eTop;
    } else {
      skip |= writingMode.IsBidiLTR() ? SideBits::eLeft : SideBits::eRight;
    }
  }

  if (logicalSkip.IEnd()) {
    if (writingMode.IsVertical()) {
      skip |= SideBits::eBottom;
    } else {
      skip |= writingMode.IsBidiLTR() ? SideBits::eRight : SideBits::eLeft;
    }
  }
  return skip;
}

nsRect nsIFrame::GetPaddingRectRelativeToSelf() const {
  nsMargin border = GetUsedBorder().ApplySkipSides(GetSkipSides());
  nsRect r(0, 0, mRect.width, mRect.height);
  r.Deflate(border);
  return r;
}

nsRect nsIFrame::GetPaddingRect() const {
  return GetPaddingRectRelativeToSelf() + GetPosition();
}

WritingMode nsIFrame::WritingModeForLine(WritingMode aSelfWM,
                                         nsIFrame* aSubFrame) const {
  MOZ_ASSERT(aSelfWM == GetWritingMode());
  WritingMode writingMode = aSelfWM;

  if (StyleTextReset()->mUnicodeBidi == StyleUnicodeBidi::Plaintext) {
    mozilla::intl::BidiEmbeddingLevel frameLevel =
        nsBidiPresUtils::GetFrameBaseLevel(aSubFrame);
    writingMode.SetDirectionFromBidiLevel(frameLevel);
  }

  return writingMode;
}

nsRect nsIFrame::GetMarginRect() const {
  return GetMarginRectRelativeToSelf() + GetPosition();
}

nsRect nsIFrame::GetMarginRectRelativeToSelf() const {
  nsMargin m = GetUsedMargin().ApplySkipSides(GetSkipSides());
  nsRect r(0, 0, mRect.width, mRect.height);
  r.Inflate(m);
  return r;
}

bool nsIFrame::IsTransformed() const {
  if (!HasAnyStateBits(NS_FRAME_MAY_BE_TRANSFORMED)) {
    MOZ_ASSERT(!IsCSSTransformed());
    MOZ_ASSERT(!GetParentSVGTransforms());
    return false;
  }
  return IsCSSTransformed() || GetParentSVGTransforms();
}

bool nsIFrame::IsCSSTransformed() const {
  return HasAnyStateBits(NS_FRAME_MAY_BE_TRANSFORMED) &&
         (StyleDisplay()->HasTransform(this) || HasAnimationOfTransform());
}

bool nsIFrame::HasAnimationOfTransform() const {
  if (!MayHaveTransformAnimation()) {
    MOZ_ASSERT(!IsPrimaryFrame() || !SupportsCSSTransforms() ||
               !nsLayoutUtils::HasAnimationOfTransformAndMotionPath(this));
    return false;
  }
  return IsPrimaryFrame() && SupportsCSSTransforms() &&
         nsLayoutUtils::HasAnimationOfTransformAndMotionPath(this);
}

bool nsIFrame::ChildrenHavePerspective(
    const nsStyleDisplay* aStyleDisplay) const {
  MOZ_ASSERT(aStyleDisplay == StyleDisplay());
  return aStyleDisplay->HasPerspective(this);
}

bool nsIFrame::HasAnimationOfOpacity(EffectSet* aEffectSet) const {
  return ((nsLayoutUtils::IsPrimaryStyleFrame(this) ||
           nsLayoutUtils::FirstContinuationOrIBSplitSibling(this)
               ->IsPrimaryFrame()) &&
          nsLayoutUtils::HasAnimationOfPropertySet(
              this, nsCSSPropertyIDSet::OpacityProperties(), aEffectSet));
}

bool nsIFrame::HasOpacityInternal(float aThreshold,
                                  const nsStyleDisplay* aStyleDisplay,
                                  const nsStyleEffects* aStyleEffects,
                                  EffectSet* aEffectSet) const {
  MOZ_ASSERT(0.0 <= aThreshold && aThreshold <= 1.0, "Invalid argument");
  if (aStyleEffects->mOpacity < aThreshold ||
      aStyleDisplay->mWillChange.bits & StyleWillChangeBits::OPACITY) {
    return true;
  }

  if (!mMayHaveOpacityAnimation) {
    return false;
  }

  return HasAnimationOfOpacity(aEffectSet);
}

bool nsIFrame::DoGetParentSVGTransforms(gfx::Matrix*) const { return false; }

bool nsIFrame::Extend3DContext(const nsStyleDisplay* aStyleDisplay,
                               const nsStyleEffects* aStyleEffects,
                               mozilla::EffectSet* aEffectSetForOpacity) const {
  if (!HasAnyStateBits(NS_FRAME_MAY_BE_TRANSFORMED)) {
    return false;
  }
  const nsStyleDisplay* disp = StyleDisplayWithOptionalParam(aStyleDisplay);
  if (disp->mTransformStyle != StyleTransformStyle::Preserve3d ||
      !SupportsCSSTransforms()) {
    return false;
  }

  // If we're all scroll frame, then all descendants will be clipped, so we
  // can't preserve 3d.
  if (IsScrollContainerFrame()) {
    return false;
  }

  const nsStyleEffects* effects = StyleEffectsWithOptionalParam(aStyleEffects);
  if (HasOpacity(disp, effects, aEffectSetForOpacity)) {
    return false;
  }

  return ShouldApplyOverflowClipping(disp).isEmpty() &&
         !GetClipPropClipRect(disp, effects, GetSize()) &&
         !SVGIntegrationUtils::UsingEffectsForFrame(this) &&
         !effects->HasMixBlendMode() &&
         !ForcesStackingContextForViewTransition() &&
         disp->mIsolation != StyleIsolation::Isolate;
}

bool nsIFrame::Combines3DTransformWithAncestors() const {
  // Check these first as they are faster then both calls below and are we are
  // likely to hit the early return (backface hidden is uncommon and
  // GetReferenceFrame is a hot caller of this which only calls this if
  // IsCSSTransformed is false).
  if (!IsCSSTransformed() && !BackfaceIsHidden()) {
    return false;
  }
  nsIFrame* parent = GetClosestFlattenedTreeAncestorPrimaryFrame();
  return parent && parent->Extend3DContext();
}

bool nsIFrame::In3DContextAndBackfaceIsHidden() const {
  // While both tests fail most of the time, test BackfaceIsHidden()
  // first since it's likely to fail faster.
  return BackfaceIsHidden() && Combines3DTransformWithAncestors();
}

bool nsIFrame::HasPerspective() const {
  if (!IsCSSTransformed()) {
    return false;
  }
  nsIFrame* parent = GetClosestFlattenedTreeAncestorPrimaryFrame();
  if (!parent) {
    return false;
  }
  return parent->ChildrenHavePerspective();
}

nsRect nsIFrame::GetContentRectRelativeToSelf() const {
  nsMargin bp = GetUsedBorderAndPadding().ApplySkipSides(GetSkipSides());
  nsRect r(0, 0, mRect.width, mRect.height);
  r.Deflate(bp);
  return r;
}

nsRect nsIFrame::GetContentRect() const {
  return GetContentRectRelativeToSelf() + GetPosition();
}

bool nsIFrame::ComputeBorderRadii(const BorderRadius& aBorderRadius,
                                  const nsSize& aFrameSize,
                                  const nsSize& aBorderArea, Sides aSkipSides,
                                  nscoord aRadii[8]) {
  // Percentages are relative to whichever side they're on.
  for (const auto i : mozilla::AllPhysicalHalfCorners()) {
    const LengthPercentage& c = aBorderRadius.Get(i);
    nscoord axis = HalfCornerIsX(i) ? aFrameSize.width : aFrameSize.height;
    aRadii[i] = std::max(0, c.Resolve(axis));
  }

  if (aSkipSides.Top()) {
    aRadii[eCornerTopLeftX] = 0;
    aRadii[eCornerTopLeftY] = 0;
    aRadii[eCornerTopRightX] = 0;
    aRadii[eCornerTopRightY] = 0;
  }

  if (aSkipSides.Right()) {
    aRadii[eCornerTopRightX] = 0;
    aRadii[eCornerTopRightY] = 0;
    aRadii[eCornerBottomRightX] = 0;
    aRadii[eCornerBottomRightY] = 0;
  }

  if (aSkipSides.Bottom()) {
    aRadii[eCornerBottomRightX] = 0;
    aRadii[eCornerBottomRightY] = 0;
    aRadii[eCornerBottomLeftX] = 0;
    aRadii[eCornerBottomLeftY] = 0;
  }

  if (aSkipSides.Left()) {
    aRadii[eCornerBottomLeftX] = 0;
    aRadii[eCornerBottomLeftY] = 0;
    aRadii[eCornerTopLeftX] = 0;
    aRadii[eCornerTopLeftY] = 0;
  }

  // css3-background specifies this algorithm for reducing
  // corner radii when they are too big.
  bool haveRadius = false;
  double ratio = 1.0f;
  for (const auto side : mozilla::AllPhysicalSides()) {
    uint32_t hc1 = SideToHalfCorner(side, false, true);
    uint32_t hc2 = SideToHalfCorner(side, true, true);
    nscoord length =
        SideIsVertical(side) ? aBorderArea.height : aBorderArea.width;
    nscoord sum = aRadii[hc1] + aRadii[hc2];
    if (sum) {
      haveRadius = true;
      // avoid floating point division in the normal case
      if (length < sum) {
        ratio = std::min(ratio, double(length) / sum);
      }
    }
  }
  if (ratio < 1.0) {
    for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
      aRadii[corner] *= ratio;
    }
  }

  return haveRadius;
}

void nsIFrame::AdjustBorderRadii(nscoord aRadii[8], const nsMargin& aOffsets) {
  auto AdjustOffset = [](const uint32_t aRadius, const nscoord aOffset) {
    // Implement the cubic formula to adjust offset when aOffset > 0 and
    // aRadius / aOffset < 1.
    // https://drafts.csswg.org/css-shapes/#valdef-shape-box-margin-box
    if (aOffset > 0) {
      const double ratio = aRadius / double(aOffset);
      if (ratio < 1.0) {
        return nscoord(aOffset * (1.0 + std::pow(ratio - 1, 3)));
      }
    }
    return aOffset;
  };

  for (const auto side : mozilla::AllPhysicalSides()) {
    const nscoord offset = aOffsets.Side(side);
    const uint32_t hc1 = SideToHalfCorner(side, false, false);
    const uint32_t hc2 = SideToHalfCorner(side, true, false);
    if (aRadii[hc1] > 0) {
      const nscoord offset1 = AdjustOffset(aRadii[hc1], offset);
      aRadii[hc1] = std::max(0, aRadii[hc1] + offset1);
    }
    if (aRadii[hc2] > 0) {
      const nscoord offset2 = AdjustOffset(aRadii[hc2], offset);
      aRadii[hc2] = std::max(0, aRadii[hc2] + offset2);
    }
  }
}

static inline bool RadiiAreDefinitelyZero(const BorderRadius& aBorderRadius) {
  for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
    if (!aBorderRadius.Get(corner).IsDefinitelyZero()) {
      return false;
    }
  }
  return true;
}

/* virtual */
bool nsIFrame::GetBorderRadii(const nsSize& aFrameSize,
                              const nsSize& aBorderArea, Sides aSkipSides,
                              nscoord aRadii[8]) const {
  if (!mMayHaveRoundedCorners) {
    memset(aRadii, 0, sizeof(nscoord) * 8);
    return false;
  }

  if (IsThemed()) {
    // When we're themed, the native theme code draws the border and
    // background, and therefore it doesn't make sense to tell other
    // code that's interested in border-radius that we have any radii.
    //
    // In an ideal world, we might have a way for the them to tell us an
    // border radius, but since we don't, we're better off assuming
    // zero.
    for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
      aRadii[corner] = 0;
    }
    return false;
  }

  const auto& radii = StyleBorder()->mBorderRadius;
  const bool hasRadii =
      ComputeBorderRadii(radii, aFrameSize, aBorderArea, aSkipSides, aRadii);
  if (!hasRadii) {
    // TODO(emilio): Maybe we can just remove this bit and do the
    // IsDefinitelyZero check unconditionally. That should still avoid most of
    // the work, though maybe not the cache miss of going through the style and
    // the border struct.
    const_cast<nsIFrame*>(this)->mMayHaveRoundedCorners =
        !RadiiAreDefinitelyZero(radii);
  }
  return hasRadii;
}

bool nsIFrame::GetBorderRadii(nscoord aRadii[8]) const {
  nsSize sz = GetSize();
  return GetBorderRadii(sz, sz, GetSkipSides(), aRadii);
}

bool nsIFrame::GetMarginBoxBorderRadii(nscoord aRadii[8]) const {
  return GetBoxBorderRadii(aRadii, GetUsedMargin());
}

bool nsIFrame::GetPaddingBoxBorderRadii(nscoord aRadii[8]) const {
  return GetBoxBorderRadii(aRadii, -GetUsedBorder());
}

bool nsIFrame::GetContentBoxBorderRadii(nscoord aRadii[8]) const {
  return GetBoxBorderRadii(aRadii, -GetUsedBorderAndPadding());
}

bool nsIFrame::GetBoxBorderRadii(nscoord aRadii[8],
                                 const nsMargin& aOffsets) const {
  if (!GetBorderRadii(aRadii)) {
    return false;
  }
  AdjustBorderRadii(aRadii, aOffsets);
  for (const auto corner : mozilla::AllPhysicalHalfCorners()) {
    if (aRadii[corner]) {
      return true;
    }
  }
  return false;
}

bool nsIFrame::GetShapeBoxBorderRadii(nscoord aRadii[8]) const {
  using Tag = StyleShapeOutside::Tag;
  auto& shapeOutside = StyleDisplay()->mShapeOutside;
  auto box = StyleShapeBox::MarginBox;
  switch (shapeOutside.tag) {
    case Tag::Image:
    case Tag::None:
      return false;
    case Tag::Box:
      box = shapeOutside.AsBox();
      break;
    case Tag::Shape:
      box = shapeOutside.AsShape()._1;
      break;
  }

  switch (box) {
    case StyleShapeBox::ContentBox:
      return GetContentBoxBorderRadii(aRadii);
    case StyleShapeBox::PaddingBox:
      return GetPaddingBoxBorderRadii(aRadii);
    case StyleShapeBox::BorderBox:
      return GetBorderRadii(aRadii);
    case StyleShapeBox::MarginBox:
      return GetMarginBoxBorderRadii(aRadii);
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected box value");
      return false;
  }
}

nscoord nsIFrame::OneEmInAppUnits() const {
  return StyleFont()
      ->mFont.size.ScaledBy(nsLayoutUtils::FontSizeInflationFor(this))
      .ToAppUnits();
}

ComputedStyle* nsIFrame::GetAdditionalComputedStyle(int32_t aIndex) const {
  MOZ_ASSERT(aIndex >= 0, "invalid index number");
  return nullptr;
}

void nsIFrame::SetAdditionalComputedStyle(int32_t aIndex,
                                          ComputedStyle* aComputedStyle) {
  MOZ_ASSERT(aIndex >= 0, "invalid index number");
}

nscoord nsIFrame::SynthesizeFallbackBaseline(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup) const {
  const auto margin = GetLogicalUsedMargin(aWM);
  NS_ASSERTION(!IsSubtreeDirty(), "frame must not be dirty");
  if (aWM.IsCentralBaseline()) {
    return (BSize(aWM) + GetLogicalUsedMargin(aWM).BEnd(aWM)) / 2;
  }
  // Baseline for inverted line content is the top (block-start) margin edge,
  // as the frame is in effect "flipped" for alignment purposes.
  if (aWM.IsLineInverted()) {
    const auto marginStart = margin.BStart(aWM);
    return aBaselineGroup == BaselineSharingGroup::First
               ? -marginStart
               : BSize(aWM) + marginStart;
  }
  // Otherwise, the bottom margin edge, per CSS2.1's definition of the
  // 'baseline' value of 'vertical-align'.
  const auto marginEnd = margin.BEnd(aWM);
  return aBaselineGroup == BaselineSharingGroup::First ? BSize(aWM) + marginEnd
                                                       : -marginEnd;
}

nscoord nsIFrame::GetLogicalBaseline(WritingMode aWM) const {
  return GetLogicalBaseline(aWM, GetDefaultBaselineSharingGroup(),
                            BaselineExportContext::LineLayout);
}

nscoord nsIFrame::GetLogicalBaseline(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  const auto result =
      GetNaturalBaselineBOffset(aWM, aBaselineGroup, aExportContext)
          .valueOrFrom([this, aWM, aBaselineGroup]() {
            return SynthesizeFallbackBaseline(aWM, aBaselineGroup);
          });
  if (aBaselineGroup == BaselineSharingGroup::Last) {
    return BSize(aWM) - result;
  }
  return result;
}

const nsFrameList& nsIFrame::GetChildList(ChildListID aListID) const {
  if (IsAbsoluteContainer() && aListID == GetAbsoluteListID()) {
    return GetAbsoluteContainingBlock()->GetChildList();
  } else {
    return nsFrameList::EmptyList();
  }
}

void nsIFrame::GetChildLists(nsTArray<ChildList>* aLists) const {
  if (IsAbsoluteContainer()) {
    const nsFrameList& absoluteList =
        GetAbsoluteContainingBlock()->GetChildList();
    absoluteList.AppendIfNonempty(aLists, GetAbsoluteListID());
  }
}

AutoTArray<nsIFrame::ChildList, 4> nsIFrame::CrossDocChildLists() {
  AutoTArray<ChildList, 4> childLists;
  nsSubDocumentFrame* subdocumentFrame = do_QueryFrame(this);
  if (subdocumentFrame) {
    // Descend into the subdocument
    nsIFrame* root = subdocumentFrame->GetSubdocumentRootFrame();
    if (root) {
      childLists.EmplaceBack(
          nsFrameList(root, nsLayoutUtils::GetLastSibling(root)),
          FrameChildListID::Principal);
    }
  }

  GetChildLists(&childLists);
  return childLists;
}

nsIFrame::CaretBlockAxisMetrics nsIFrame::GetCaretBlockAxisMetrics(
    mozilla::WritingMode aWM, const nsFontMetrics& aFM) const {
  // Note(dshin): Ultimately, this does something highly similar (But still
  // different) to `nsLayoutUtils::GetFirstLinePosition`.
  const auto baseline = GetCaretBaseline();
  nscoord ascent = 0, descent = 0;
  ascent = aFM.MaxAscent();
  descent = aFM.MaxDescent();
  const nscoord height = ascent + descent;
  if (aWM.IsVertical() && aWM.IsLineInverted()) {
    return CaretBlockAxisMetrics{.mOffset = baseline - descent,
                                 .mExtent = height};
  }
  return CaretBlockAxisMetrics{.mOffset = baseline - ascent, .mExtent = height};
}

nscoord nsIFrame::GetFontMetricsDerivedCaretBaseline(nscoord aBSize) const {
  float inflation = nsLayoutUtils::FontSizeInflationFor(this);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(this, inflation);
  const WritingMode wm = GetWritingMode();
  nscoord lineHeight = ReflowInput::CalcLineHeight(
      *Style(), PresContext(), GetContent(), aBSize, inflation);
  return nsLayoutUtils::GetCenteredFontBaseline(fm, lineHeight,
                                                wm.IsLineInverted()) +
         GetLogicalUsedBorderAndPadding(wm).BStart(wm);
}

const nsAtom* nsIFrame::ComputePageValue(const nsAtom* aAutoValue) const {
  const nsAtom* value = aAutoValue ? aAutoValue : nsGkAtoms::_empty;
  const nsIFrame* frame = this;
  // Find what CSS page name value this frame's subtree has, if any.
  // Starting with this frame, check if a page name other than auto is present,
  // and record it if so. Then, if the current frame is a container frame, find
  // the first non-placeholder child and repeat.
  // This will find the most deeply nested first in-flow child of this frame's
  // subtree, and return its page name (with auto resolved if applicable, and
  // subtrees with no page-names returning the empty atom rather than null).
  do {
    if (const nsAtom* maybePageName = frame->GetStylePageName()) {
      value = maybePageName;
    }
    // Get the next frame to read from.
    const nsIFrame* firstNonPlaceholderFrame = nullptr;
    // If this is a container frame, inspect its in-flow children.
    if (const nsContainerFrame* containerFrame = do_QueryFrame(frame)) {
      for (const nsIFrame* childFrame : containerFrame->PrincipalChildList()) {
        if (!childFrame->IsPlaceholderFrame()) {
          firstNonPlaceholderFrame = childFrame;
          break;
        }
      }
    }
    frame = firstNonPlaceholderFrame;
  } while (frame);
  return value;
}

Visibility nsIFrame::GetVisibility() const {
  if (!HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED)) {
    return Visibility::Untracked;
  }

  bool isSet = false;
  uint32_t visibleCount = GetProperty(VisibilityStateProperty(), &isSet);

  MOZ_ASSERT(isSet,
             "Should have a VisibilityStateProperty value "
             "if NS_FRAME_VISIBILITY_IS_TRACKED is set");

  return visibleCount > 0 ? Visibility::ApproximatelyVisible
                          : Visibility::ApproximatelyNonVisible;
}

void nsIFrame::UpdateVisibilitySynchronously() {
  mozilla::PresShell* presShell = PresShell();
  if (!presShell) {
    return;
  }

  if (presShell->AssumeAllFramesVisible()) {
    presShell->EnsureFrameInApproximatelyVisibleList(this);
    return;
  }

  bool visible = StyleVisibility()->IsVisible();
  nsIFrame* f = GetParent();
  nsRect rect = GetRectRelativeToSelf();
  nsIFrame* rectFrame = this;
  while (f && visible) {
    if (ScrollContainerFrame* sf = do_QueryFrame(f)) {
      nsRect transformedRect =
          nsLayoutUtils::TransformFrameRectToAncestor(rectFrame, rect, f);
      if (!sf->IsRectNearlyVisible(transformedRect)) {
        visible = false;
        break;
      }

      // In this code we're trying to synchronously update *approximate*
      // visibility. (In the future we may update precise visibility here as
      // well, which is why the method name does not contain 'approximate'.) The
      // IsRectNearlyVisible() check above tells us that the rect we're checking
      // is approximately visible within the scrollframe, but we still need to
      // ensure that, even if it was scrolled into view, it'd be visible when we
      // consider the rest of the document. To do that, we move transformedRect
      // to be contained in the scrollport as best we can (it might not fit) to
      // pretend that it was scrolled into view.
      rect = transformedRect.MoveInsideAndClamp(sf->GetScrollPortRect());
      rectFrame = f;
    }
    nsIFrame* parent = f->GetParent();
    if (!parent) {
      parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(f);
      if (parent && parent->PresContext()->IsChrome()) {
        break;
      }
    }
    f = parent;
  }

  if (visible) {
    presShell->EnsureFrameInApproximatelyVisibleList(this);
  } else {
    presShell->RemoveFrameFromApproximatelyVisibleList(this);
  }
}

void nsIFrame::EnableVisibilityTracking() {
  if (HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED)) {
    return;  // Nothing to do.
  }

  MOZ_ASSERT(!HasProperty(VisibilityStateProperty()),
             "Shouldn't have a VisibilityStateProperty value "
             "if NS_FRAME_VISIBILITY_IS_TRACKED is not set");

  // Add the state bit so we know to track visibility for this frame, and
  // initialize the frame property.
  AddStateBits(NS_FRAME_VISIBILITY_IS_TRACKED);
  SetProperty(VisibilityStateProperty(), 0);

  mozilla::PresShell* presShell = PresShell();
  if (!presShell) {
    return;
  }

  // Schedule a visibility update. This method will virtually always be called
  // when layout has changed anyway, so it's very unlikely that any additional
  // visibility updates will be triggered by this, but this way we guarantee
  // that if this frame is currently visible we'll eventually find out.
  presShell->ScheduleApproximateFrameVisibilityUpdateSoon();
}

void nsIFrame::DisableVisibilityTracking() {
  if (!HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED)) {
    return;  // Nothing to do.
  }

  bool isSet = false;
  uint32_t visibleCount = TakeProperty(VisibilityStateProperty(), &isSet);

  MOZ_ASSERT(isSet,
             "Should have a VisibilityStateProperty value "
             "if NS_FRAME_VISIBILITY_IS_TRACKED is set");

  RemoveStateBits(NS_FRAME_VISIBILITY_IS_TRACKED);

  if (visibleCount == 0) {
    return;  // We were nonvisible.
  }

  // We were visible, so send an OnVisibilityChange() notification.
  OnVisibilityChange(Visibility::ApproximatelyNonVisible);
}

void nsIFrame::DecApproximateVisibleCount(
    const Maybe<OnNonvisible>& aNonvisibleAction
    /* = Nothing() */) {
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED));

  bool isSet = false;
  uint32_t visibleCount = GetProperty(VisibilityStateProperty(), &isSet);

  MOZ_ASSERT(isSet,
             "Should have a VisibilityStateProperty value "
             "if NS_FRAME_VISIBILITY_IS_TRACKED is set");
  MOZ_ASSERT(visibleCount > 0,
             "Frame is already nonvisible and we're "
             "decrementing its visible count?");

  visibleCount--;
  SetProperty(VisibilityStateProperty(), visibleCount);
  if (visibleCount > 0) {
    return;
  }

  // We just became nonvisible, so send an OnVisibilityChange() notification.
  OnVisibilityChange(Visibility::ApproximatelyNonVisible, aNonvisibleAction);
}

void nsIFrame::IncApproximateVisibleCount() {
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_VISIBILITY_IS_TRACKED));

  bool isSet = false;
  uint32_t visibleCount = GetProperty(VisibilityStateProperty(), &isSet);

  MOZ_ASSERT(isSet,
             "Should have a VisibilityStateProperty value "
             "if NS_FRAME_VISIBILITY_IS_TRACKED is set");

  visibleCount++;
  SetProperty(VisibilityStateProperty(), visibleCount);
  if (visibleCount > 1) {
    return;
  }

  // We just became visible, so send an OnVisibilityChange() notification.
  OnVisibilityChange(Visibility::ApproximatelyVisible);
}

void nsIFrame::OnVisibilityChange(Visibility aNewVisibility,
                                  const Maybe<OnNonvisible>& aNonvisibleAction
                                  /* = Nothing() */) {
  // XXX(seth): In bug 1218990 we'll implement visibility tracking for CSS
  // images here.
}

static nsIFrame* GetActiveSelectionFrame(nsPresContext* aPresContext,
                                         nsIFrame* aFrame) {
  nsIContent* capturingContent = PresShell::GetCapturingContent();
  if (capturingContent) {
    nsIFrame* activeFrame = aPresContext->GetPrimaryFrameFor(capturingContent);
    return activeFrame ? activeFrame : aFrame;
  }

  return aFrame;
}

int16_t nsIFrame::DetermineDisplaySelection() {
  int16_t selType = nsISelectionController::SELECTION_OFF;

  nsCOMPtr<nsISelectionController> selCon;
  nsresult result =
      GetSelectionController(PresContext(), getter_AddRefs(selCon));
  if (NS_SUCCEEDED(result) && selCon) {
    result = selCon->GetDisplaySelection(&selType);
    if (NS_SUCCEEDED(result) &&
        (selType != nsISelectionController::SELECTION_OFF)) {
      // Check whether style allows selection.
      if (!IsSelectable(nullptr)) {
        selType = nsISelectionController::SELECTION_OFF;
      }
    }
  }
  return selType;
}

static Element* FindElementAncestorForMozSelection(nsIContent* aContent) {
  NS_ENSURE_TRUE(aContent, nullptr);
  while (aContent && aContent->IsInNativeAnonymousSubtree()) {
    aContent = aContent->GetClosestNativeAnonymousSubtreeRootParentOrHost();
  }
  NS_ASSERTION(aContent, "aContent isn't in non-anonymous tree?");
  return aContent ? aContent->GetAsElementOrParentElement() : nullptr;
}

already_AddRefed<ComputedStyle> nsIFrame::ComputeSelectionStyle(
    int16_t aSelectionStatus) const {
  // Just bail out if not a selection-status that ::selection applies to.
  if (aSelectionStatus != nsISelectionController::SELECTION_ON &&
      aSelectionStatus != nsISelectionController::SELECTION_DISABLED) {
    return nullptr;
  }
  Element* element = FindElementAncestorForMozSelection(GetContent());
  if (!element) {
    return nullptr;
  }
  RefPtr<ComputedStyle> pseudoStyle =
      PresContext()->StyleSet()->ProbePseudoElementStyle(
          *element, PseudoStyleType::selection, nullptr, Style());
  if (!pseudoStyle) {
    return nullptr;
  }
  // When in high-contrast mode, the style system ends up ignoring the color
  // declarations, which means that the ::selection style becomes the inherited
  // color, and default background. That's no good.
  // When force-color-adjust is set to none allow using the color styles,
  // as they will not be replaced.
  if (PresContext()->ForcingColors() &&
      pseudoStyle->StyleText()->mForcedColorAdjust !=
          StyleForcedColorAdjust::None) {
    return nullptr;
  }
  return do_AddRef(pseudoStyle);
}

already_AddRefed<ComputedStyle> nsIFrame::ComputeHighlightSelectionStyle(
    nsAtom* aHighlightName) {
  Element* element = FindElementAncestorForMozSelection(GetContent());
  if (!element) {
    return nullptr;
  }
  return PresContext()->StyleSet()->ProbePseudoElementStyle(
      *element, PseudoStyleType::highlight, aHighlightName, Style());
}

already_AddRefed<ComputedStyle> nsIFrame::ComputeTargetTextStyle() const {
  const Element* element = FindElementAncestorForMozSelection(GetContent());
  if (!element) {
    return nullptr;
  }
  RefPtr pseudoStyle = PresContext()->StyleSet()->ProbePseudoElementStyle(
      *element, PseudoStyleType::targetText, nullptr, Style());
  if (!pseudoStyle) {
    return nullptr;
  }
  if (PresContext()->ForcingColors() &&
      pseudoStyle->StyleText()->mForcedColorAdjust !=
          StyleForcedColorAdjust::None) {
    return nullptr;
  }
  return pseudoStyle.forget();
}

bool nsIFrame::CanBeDynamicReflowRoot() const {
  const auto& display = *StyleDisplay();
  if (IsLineParticipant() || display.mDisplay.IsRuby() ||
      display.IsInnerTableStyle() ||
      display.DisplayInside() == StyleDisplayInside::Table) {
    // We have a display type where 'width' and 'height' don't actually set the
    // width or height (i.e., the size depends on content).
    MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_DYNAMIC_REFLOW_ROOT),
               "should not have dynamic reflow root bit");
    return false;
  }

  // In general, frames that have contain:layout+size can be reflow roots.
  // (One exception: table-wrapper frames don't work well as reflow roots,
  // because their inner-table ReflowInput init path tries to reuse & deref
  // the wrapper's containing block's reflow input, which may be null if we
  // initiate reflow from the table-wrapper itself.)
  //
  // Changes to `contain` force frame reconstructions, so we used to use
  // NS_FRAME_REFLOW_ROOT, this bit could be set for the whole lifetime of
  // this frame. But after the support of `content-visibility: auto` which
  // is with contain layout + size when it's not relevant to user, and only
  // with contain layout when it is relevant. The frame does not reconstruct
  // when the relevancy changes. So we use NS_FRAME_DYNAMIC_REFLOW_ROOT instead.
  //
  // We place it above the pref check on purpose, to make sure it works for
  // containment even with the pref disabled.
  if (display.IsContainLayout() && GetContainSizeAxes().IsBoth()) {
    return true;
  }

  if (!StaticPrefs::layout_dynamic_reflow_roots_enabled()) {
    return false;
  }

  // We can't serve as a dynamic reflow root if our used 'width' and 'height'
  // might be influenced by content.
  //
  // FIXME: For display:block, we should probably optimize inline-size: auto.
  // FIXME: Other flex and grid cases?
  const auto& pos = *StylePosition();
  const auto positionProperty = StyleDisplay()->mPosition;
  const auto width = pos.GetWidth(positionProperty);
  const auto height = pos.GetHeight(positionProperty);
  if (!width->IsLengthPercentage() || width->HasPercent() ||
      !height->IsLengthPercentage() || height->HasPercent() ||
      IsIntrinsicKeyword(*pos.GetMinWidth(positionProperty)) ||
      IsIntrinsicKeyword(*pos.GetMaxWidth(positionProperty)) ||
      IsIntrinsicKeyword(*pos.GetMinHeight(positionProperty)) ||
      IsIntrinsicKeyword(*pos.GetMaxHeight(positionProperty)) ||
      ((pos.GetMinWidth(positionProperty)->IsAuto() ||
        pos.GetMinHeight(positionProperty)->IsAuto()) &&
       IsFlexOrGridItem())) {
    return false;
  }

  // If our flex-basis is 'auto', it'll defer to 'width' (or 'height') which
  // we've already checked. Otherwise, it preempts them, so we need to
  // perform the same "could-this-value-be-influenced-by-content" checks that
  // we performed for 'width' and 'height' above.
  if (IsFlexItem()) {
    const auto& flexBasis = pos.mFlexBasis;
    if (!flexBasis.IsAuto()) {
      if (!flexBasis.IsSize() || !flexBasis.AsSize().IsLengthPercentage() ||
          flexBasis.AsSize().HasPercent()) {
        return false;
      }
    }
  }

  if (!IsFixedPosContainingBlock()) {
    // We can't treat this frame as a reflow root, since dynamic changes
    // to absolutely-positioned frames inside of it require that we
    // reflow the placeholder before we reflow the absolutely positioned
    // frame.
    // FIXME:  Alternatively, we could sort the reflow roots in
    // PresShell::ProcessReflowCommands by depth in the tree, from
    // deepest to least deep.  However, for performance (FIXME) we
    // should really be sorting them in the opposite order!
    return false;
  }

  // If we participate in a container's block reflow context, or margins
  // can collapse through us, we can't be a dynamic reflow root.
  // (NS_BLOCK_BFC is block specific bit, check first as an optimization, it's
  // okay because we also check that it is a block frame.)
  if (!HasAnyStateBits(NS_BLOCK_BFC) && IsBlockFrameOrSubclass()) {
    return false;
  }

  // Subgrids are never reflow roots, but 'contain:layout/paint' prevents
  // creating a subgrid in the first place.
  if (pos.mGridTemplateColumns.IsSubgrid() ||
      pos.mGridTemplateRows.IsSubgrid()) {
    // NOTE: we could check that 'display' of our parent's primary frame is
    // '[inline-]grid' here but that's probably not worth it in practice.
    if (!display.IsContainLayout() && !display.IsContainPaint()) {
      return false;
    }
  }

  // If we are split, we can't be a dynamic reflow root. Our reflow status may
  // change after reflow, and our parent is responsible to create or delete our
  // next-in-flow.
  if (GetPrevContinuation() || GetNextContinuation()) {
    return false;
  }

  return true;
}

/********************************************************
 * Refreshes each content's frame
 *********************************************************/

void nsIFrame::DisplayOutlineUnconditional(nsDisplayListBuilder* aBuilder,
                                           const nsDisplayListSet& aLists) {
  // Per https://drafts.csswg.org/css-tables-3/#global-style-overrides:
  // "All css properties of table-column and table-column-group boxes are
  // ignored, except when explicitly specified by this specification."
  // CSS outlines fall into this category, so we skip them on these boxes.
  MOZ_ASSERT(!IsTableColGroupFrame() && !IsTableColFrame());
  const auto& outline = *StyleOutline();

  if (!outline.ShouldPaintOutline()) {
    return;
  }

  // Outlines are painted by the table wrapper frame.
  if (IsTableFrame()) {
    return;
  }

  if (HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT) &&
      ScrollableOverflowRect().IsEmpty()) {
    // Skip parts of IB-splits with an empty overflow rect, see bug 434301.
    // We may still want to fix some of the overflow area calculations over in
    // that bug.
    return;
  }

  // We don't display outline-style: auto on themed frames that have their own
  // focus indicators.
  if (outline.mOutlineStyle.IsAuto()) {
    auto* disp = StyleDisplay();
    if (IsThemed(disp) && PresContext()->Theme()->ThemeDrawsFocusForWidget(
                              this, disp->EffectiveAppearance())) {
      return;
    }
  }

  aLists.Outlines()->AppendNewToTop<nsDisplayOutline>(aBuilder, this);
}

void nsIFrame::DisplayOutline(nsDisplayListBuilder* aBuilder,
                              const nsDisplayListSet& aLists) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayOutlineUnconditional(aBuilder, aLists);
}

void nsIFrame::DisplayInsetBoxShadowUnconditional(
    nsDisplayListBuilder* aBuilder, nsDisplayList* aList) {
  // XXXbz should box-shadow for rows/rowgroups/columns/colgroups get painted
  // just because we're visible?  Or should it depend on the cell visibility
  // when we're not the whole table?
  const auto* effects = StyleEffects();
  if (effects->HasBoxShadowWithInset(true)) {
    aList->AppendNewToTop<nsDisplayBoxShadowInner>(aBuilder, this);
  }
}

void nsIFrame::DisplayInsetBoxShadow(nsDisplayListBuilder* aBuilder,
                                     nsDisplayList* aList) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayInsetBoxShadowUnconditional(aBuilder, aList);
}

void nsIFrame::DisplayOutsetBoxShadowUnconditional(
    nsDisplayListBuilder* aBuilder, nsDisplayList* aList) {
  // XXXbz should box-shadow for rows/rowgroups/columns/colgroups get painted
  // just because we're visible?  Or should it depend on the cell visibility
  // when we're not the whole table?
  const auto* effects = StyleEffects();
  if (effects->HasBoxShadowWithInset(false)) {
    aList->AppendNewToTop<nsDisplayBoxShadowOuter>(aBuilder, this);
  }
}

void nsIFrame::DisplayOutsetBoxShadow(nsDisplayListBuilder* aBuilder,
                                      nsDisplayList* aList) {
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayOutsetBoxShadowUnconditional(aBuilder, aList);
}

void nsIFrame::DisplayCaret(nsDisplayListBuilder* aBuilder,
                            nsDisplayList* aList) {
  if (!IsVisibleForPainting()) {
    return;
  }

  aList->AppendNewToTop<nsDisplayCaret>(aBuilder, this);
}

nscolor nsIFrame::GetCaretColorAt(int32_t aOffset) {
  return nsLayoutUtils::GetTextColor(this, &nsStyleUI::mCaretColor);
}

auto nsIFrame::ComputeShouldPaintBackground() const -> ShouldPaintBackground {
  nsPresContext* pc = PresContext();
  ShouldPaintBackground settings{pc->GetBackgroundColorDraw(),
                                 pc->GetBackgroundImageDraw()};
  if (settings.mColor && settings.mImage) {
    return settings;
  }

  if (StyleVisibility()->mPrintColorAdjust == StylePrintColorAdjust::Exact) {
    return {true, true};
  }

  return settings;
}

bool nsIFrame::DisplayBackgroundUnconditional(nsDisplayListBuilder* aBuilder,
                                              const nsDisplayListSet& aLists) {
  if (aBuilder->IsForEventDelivery() && !aBuilder->HitTestIsForVisibility()) {
    // For hit-testing, we generally just need a light-weight data structure
    // like nsDisplayEventReceiver. But if the hit-testing is for visibility,
    // then we need to know the opaque region in order to determine whether to
    // stop or not.
    aLists.BorderBackground()->AppendNewToTop<nsDisplayEventReceiver>(aBuilder,
                                                                      this);
    return false;
  }

  const AppendedBackgroundType result =
      nsDisplayBackgroundImage::AppendBackgroundItemsToTop(
          aBuilder, this,
          GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this),
          aLists.BorderBackground());

  if (result == AppendedBackgroundType::None) {
    aBuilder->BuildCompositorHitTestInfoIfNeeded(this,
                                                 aLists.BorderBackground());
  }

  return result == AppendedBackgroundType::ThemedBackground;
}

void nsIFrame::DisplayBorderBackgroundOutline(nsDisplayListBuilder* aBuilder,
                                              const nsDisplayListSet& aLists) {
  // The visibility check belongs here since child elements have the
  // opportunity to override the visibility property and display even if
  // their parent is hidden.
  if (!IsVisibleForPainting()) {
    return;
  }

  DisplayOutsetBoxShadowUnconditional(aBuilder, aLists.BorderBackground());

  bool bgIsThemed = DisplayBackgroundUnconditional(aBuilder, aLists);
  DisplayInsetBoxShadowUnconditional(aBuilder, aLists.BorderBackground());

  // If there's a themed background, we should not create a border item.
  // It won't be rendered.
  // Don't paint borders for tables here, since they paint them in a different
  // order.
  if (!bgIsThemed && StyleBorder()->HasBorder() && !IsTableFrame()) {
    aLists.BorderBackground()->AppendNewToTop<nsDisplayBorder>(aBuilder, this);
  }

  DisplayOutlineUnconditional(aBuilder, aLists);
}

inline static bool IsSVGContentWithCSSClip(const nsIFrame* aFrame) {
  // The CSS spec says that the 'clip' property only applies to absolutely
  // positioned elements, whereas the SVG spec says that it applies to SVG
  // elements regardless of the value of the 'position' property. Here we obey
  // the CSS spec for outer-<svg> (since that's what we generally do), but
  // obey the SVG spec for other SVG elements to which 'clip' applies.
  return aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT) &&
         aFrame->GetContent()->IsAnyOfSVGElements(nsGkAtoms::svg,
                                                  nsGkAtoms::foreignObject);
}

Maybe<nsRect> nsIFrame::GetClipPropClipRect(const nsStyleDisplay* aDisp,
                                            const nsStyleEffects* aEffects,
                                            const nsSize& aSize) const {
  if (aEffects->mClip.IsAuto() ||
      !(aDisp->IsAbsolutelyPositioned(this) || IsSVGContentWithCSSClip(this))) {
    return Nothing();
  }

  auto& clipRect = aEffects->mClip.AsRect();
  nsRect rect = clipRect.ToLayoutRect();
  if (MOZ_LIKELY(StyleBorder()->mBoxDecorationBreak ==
                 StyleBoxDecorationBreak::Slice)) {
    // The clip applies to the joined boxes so it's relative the first
    // continuation.
    nscoord y = 0;
    for (nsIFrame* f = GetPrevContinuation(); f; f = f->GetPrevContinuation()) {
      y += f->GetRect().height;
    }
    rect.MoveBy(nsPoint(0, -y));
  }

  if (clipRect.right.IsAuto()) {
    rect.width = aSize.width - rect.x;
  }
  if (clipRect.bottom.IsAuto()) {
    rect.height = aSize.height - rect.y;
  }
  return Some(rect);
}

// https://drafts.csswg.org/css-view-transitions-1/#named-and-transitioning
//
// Note https://github.com/w3c/csswg-drafts/issues/11772, however, for the root
// style check.
bool nsIFrame::ForcesStackingContextForViewTransition() const {
  auto* style = Style();
  return (style->StyleUIReset()->HasViewTransitionName() ||
          HasAnyStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION) ||
          style->StyleDisplay()->mWillChange.bits &
              mozilla::StyleWillChangeBits::VIEW_TRANSITION_NAME) &&
         !style->IsRootElementStyle();
}

/**
 * If the CSS 'overflow' property applies to this frame, and is not
 * handled by constructing a dedicated nsHTML/XULScrollFrame, set up clipping
 * for that overflow in aBuilder->ClipState() to clip all containing-block
 * descendants.
 */
static void ApplyOverflowClipping(
    nsDisplayListBuilder* aBuilder, const nsIFrame* aFrame,
    PhysicalAxes aClipAxes,
    DisplayListClipState::AutoClipMultiple& aClipState) {
  // Only 'clip' is handled here (and 'hidden' for table frames, and any
  // non-'visible' value for blocks in a paginated context).
  // We allow 'clip' to apply to any kind of frame. This is required by
  // comboboxes which make their display text (an inline frame) have clipping.
  MOZ_ASSERT(!aClipAxes.isEmpty());
  MOZ_ASSERT(aFrame->ShouldApplyOverflowClipping(aFrame->StyleDisplay()) ==
             aClipAxes);

  nsRect clipRect;
  bool haveRadii = false;
  nscoord radii[8];
  auto* disp = aFrame->StyleDisplay();
  // Only deflate the padding if we clip to the content-box in that axis.
  auto wm = aFrame->GetWritingMode();
  bool cbH = (wm.IsVertical() ? disp->mOverflowClipBoxBlock
                              : disp->mOverflowClipBoxInline) ==
             StyleOverflowClipBox::ContentBox;
  bool cbV = (wm.IsVertical() ? disp->mOverflowClipBoxInline
                              : disp->mOverflowClipBoxBlock) ==
             StyleOverflowClipBox::ContentBox;

  nsMargin boxMargin = -aFrame->GetUsedPadding();
  if (!cbH) {
    boxMargin.left = boxMargin.right = nscoord(0);
  }
  if (!cbV) {
    boxMargin.top = boxMargin.bottom = nscoord(0);
  }

  auto clipMargin = aFrame->OverflowClipMargin(aClipAxes);

  boxMargin -= aFrame->GetUsedBorder();
  boxMargin += nsMargin(clipMargin.height, clipMargin.width, clipMargin.height,
                        clipMargin.width);
  boxMargin.ApplySkipSides(aFrame->GetSkipSides());

  nsRect rect(nsPoint(0, 0), aFrame->GetSize());
  rect.Inflate(boxMargin);
  if (MOZ_UNLIKELY(!aClipAxes.contains(PhysicalAxis::Horizontal))) {
    // NOTE(mats) We shouldn't be clipping at all in this dimension really,
    // but clipping in just one axis isn't supported by our GFX APIs so we
    // clip to our visual overflow rect instead.
    nsRect o = aFrame->InkOverflowRect();
    rect.x = o.x;
    rect.width = o.width;
  }
  if (MOZ_UNLIKELY(!aClipAxes.contains(PhysicalAxis::Vertical))) {
    // See the note above.
    nsRect o = aFrame->InkOverflowRect();
    rect.y = o.y;
    rect.height = o.height;
  }
  clipRect = rect + aBuilder->ToReferenceFrame(aFrame);
  haveRadii = aFrame->GetBoxBorderRadii(radii, boxMargin);
  aClipState.ClipContainingBlockDescendantsExtra(clipRect,
                                                 haveRadii ? radii : nullptr);
}

nsSize nsIFrame::OverflowClipMargin(PhysicalAxes aClipAxes) const {
  nsSize result;
  if (aClipAxes.isEmpty()) {
    return result;
  }
  const auto& margin = StyleMargin()->mOverflowClipMargin;
  if (margin.IsZero()) {
    return result;
  }
  nscoord marginAu = margin.ToAppUnits();
  if (aClipAxes.contains(PhysicalAxis::Horizontal)) {
    result.width = marginAu;
  }
  if (aClipAxes.contains(PhysicalAxis::Vertical)) {
    result.height = marginAu;
  }
  return result;
}

/**
 * Returns whether a display item that gets created with the builder's current
 * state will have a scrolled clip, i.e. a clip that is scrolled by a scroll
 * frame which does not move the item itself.
 */
static bool BuilderHasScrolledClip(nsDisplayListBuilder* aBuilder) {
  const DisplayItemClipChain* currentClip =
      aBuilder->ClipState().GetCurrentCombinedClipChain(aBuilder);
  if (!currentClip) {
    return false;
  }

  const ActiveScrolledRoot* currentClipASR = currentClip->mASR;
  const ActiveScrolledRoot* currentASR = aBuilder->CurrentActiveScrolledRoot();
  return ActiveScrolledRoot::PickDescendant(currentClipASR, currentASR) !=
         currentASR;
}

class AutoSaveRestoreContainsBlendMode {
  nsDisplayListBuilder& mBuilder;
  bool mSavedContainsBlendMode;

 public:
  explicit AutoSaveRestoreContainsBlendMode(nsDisplayListBuilder& aBuilder)
      : mBuilder(aBuilder),
        mSavedContainsBlendMode(aBuilder.ContainsBlendMode()) {}

  ~AutoSaveRestoreContainsBlendMode() {
    mBuilder.SetContainsBlendMode(mSavedContainsBlendMode);
  }
};

static bool IsFrameOrAncestorApzAware(nsIFrame* aFrame) {
  nsIContent* node = aFrame->GetContent();
  if (!node) {
    return false;
  }

  do {
    if (node->IsNodeApzAware()) {
      return true;
    }
    nsIContent* shadowRoot = node->GetShadowRoot();
    if (shadowRoot && shadowRoot->IsNodeApzAware()) {
      return true;
    }

    // Even if the node owning aFrame doesn't have apz-aware event listeners
    // itself, its shadow root or display: contents ancestors (which have no
    // frames) might, so we need to account for them too.
  } while ((node = node->GetFlattenedTreeParent()) && node->IsElement() &&
           node->AsElement()->IsDisplayContents());

  return false;
}

static void CheckForApzAwareEventHandlers(nsDisplayListBuilder* aBuilder,
                                          nsIFrame* aFrame) {
  if (aBuilder->GetAncestorHasApzAwareEventHandler()) {
    return;
  }

  if (IsFrameOrAncestorApzAware(aFrame)) {
    aBuilder->SetAncestorHasApzAwareEventHandler(true);
  }
}

static void UpdateCurrentHitTestInfo(nsDisplayListBuilder* aBuilder,
                                     nsIFrame* aFrame) {
  if (!aBuilder->BuildCompositorHitTestInfo()) {
    // Compositor hit test info is not used.
    return;
  }

  CheckForApzAwareEventHandlers(aBuilder, aFrame);

  const CompositorHitTestInfo info = aFrame->GetCompositorHitTestInfo(aBuilder);
  aBuilder->SetCompositorHitTestInfo(info);
}

/**
 * True if aDescendant participates the context aAncestor participating.
 */
static bool FrameParticipatesIn3DContext(nsIFrame* aAncestor,
                                         nsIFrame* aDescendant) {
  MOZ_ASSERT(aAncestor != aDescendant);
  MOZ_ASSERT(aAncestor->GetContent() != aDescendant->GetContent());
  MOZ_ASSERT(aAncestor->Extend3DContext());

  nsIFrame* ancestor = aAncestor->FirstContinuation();
  MOZ_ASSERT(ancestor->IsPrimaryFrame());

  nsIFrame* frame;
  for (frame = aDescendant->GetClosestFlattenedTreeAncestorPrimaryFrame();
       frame && ancestor != frame;
       frame = frame->GetClosestFlattenedTreeAncestorPrimaryFrame()) {
    if (!frame->Extend3DContext()) {
      return false;
    }
  }

  MOZ_ASSERT(frame == ancestor);
  return true;
}

static bool ItemParticipatesIn3DContext(nsIFrame* aAncestor,
                                        nsDisplayItem* aItem) {
  auto type = aItem->GetType();
  const bool isContainer = type == DisplayItemType::TYPE_WRAP_LIST ||
                           type == DisplayItemType::TYPE_CONTAINER;

  if (isContainer && aItem->GetChildren()->Length() == 1) {
    // If the wraplist has only one child item, use the type of that item.
    type = aItem->GetChildren()->GetBottom()->GetType();
  }

  if (type != DisplayItemType::TYPE_TRANSFORM &&
      type != DisplayItemType::TYPE_PERSPECTIVE) {
    return false;
  }
  nsIFrame* transformFrame = aItem->Frame();
  if (aAncestor->GetContent() == transformFrame->GetContent()) {
    return true;
  }
  return FrameParticipatesIn3DContext(aAncestor, transformFrame);
}

static void WrapSeparatorTransform(nsDisplayListBuilder* aBuilder,
                                   nsIFrame* aFrame,
                                   nsDisplayList* aNonParticipants,
                                   nsDisplayList* aParticipants, int aIndex,
                                   nsDisplayItem** aSeparator) {
  if (aNonParticipants->IsEmpty()) {
    return;
  }

  nsDisplayTransform* item = MakeDisplayItemWithIndex<nsDisplayTransform>(
      aBuilder, aFrame, aIndex, aNonParticipants, aBuilder->GetVisibleRect());

  if (*aSeparator == nullptr && item) {
    *aSeparator = item;
  }

  aParticipants->AppendToTop(item);
}

// Try to compute a clip rect to bound the contents of the mask item
// that will be built for |aMaskedFrame|. If we're not able to compute
// one, return an empty Maybe.
// The returned clip rect, if there is one, is relative to |aMaskedFrame|.
static Maybe<nsRect> ComputeClipForMaskItem(
    nsDisplayListBuilder* aBuilder, nsIFrame* aMaskedFrame,
    const SVGUtils::MaskUsage& aMaskUsage) {
  const nsStyleSVGReset* svgReset = aMaskedFrame->StyleSVGReset();

  nsPoint offsetToUserSpace =
      nsLayoutUtils::ComputeOffsetToUserSpace(aBuilder, aMaskedFrame);
  int32_t devPixelRatio = aMaskedFrame->PresContext()->AppUnitsPerDevPixel();
  gfxPoint devPixelOffsetToUserSpace =
      nsLayoutUtils::PointToGfxPoint(offsetToUserSpace, devPixelRatio);
  CSSToLayoutDeviceScale cssToDevScale =
      aMaskedFrame->PresContext()->CSSToDevPixelScale();

  nsPoint toReferenceFrame;
  aBuilder->FindReferenceFrameFor(aMaskedFrame, &toReferenceFrame);

  Maybe<gfxRect> combinedClip;
  if (aMaskUsage.ShouldApplyBasicShapeOrPath()) {
    Maybe<Rect> result =
        CSSClipPathInstance::GetBoundingRectForBasicShapeOrPathClip(
            aMaskedFrame, svgReset->mClipPath);
    if (result) {
      combinedClip = Some(ThebesRect(*result));
    }
  } else if (aMaskUsage.ShouldApplyClipPath()) {
    gfxRect result = SVGUtils::GetBBox(
        aMaskedFrame,
        SVGUtils::eBBoxIncludeClipped | SVGUtils::eBBoxIncludeFill |
            SVGUtils::eBBoxIncludeMarkers | SVGUtils::eBBoxIncludeStroke |
            SVGUtils::eDoNotClipToBBoxOfContentInsideClipPath);
    combinedClip = Some(
        ThebesRect((CSSRect::FromUnknownRect(ToRect(result)) * cssToDevScale)
                       .ToUnknownRect()));
  } else {
    // The code for this case is adapted from ComputeMaskGeometry().

    nsRect borderArea(toReferenceFrame, aMaskedFrame->GetSize());
    borderArea -= offsetToUserSpace;

    // Use an infinite dirty rect to pass into nsCSSRendering::
    // GetImageLayerClip() because we don't have an actual dirty rect to
    // pass in. This is fine because the only time GetImageLayerClip() will
    // not intersect the incoming dirty rect with something is in the "NoClip"
    // case, and we handle that specially.
    nsRect dirtyRect(nscoord_MIN / 2, nscoord_MIN / 2, nscoord_MAX,
                     nscoord_MAX);

    nsIFrame* firstFrame =
        nsLayoutUtils::FirstContinuationOrIBSplitSibling(aMaskedFrame);
    nsTArray<SVGMaskFrame*> maskFrames;
    // XXX check return value?
    SVGObserverUtils::GetAndObserveMasks(firstFrame, &maskFrames);

    for (uint32_t i = 0; i < maskFrames.Length(); ++i) {
      gfxRect clipArea;
      if (maskFrames[i]) {
        clipArea = maskFrames[i]->GetMaskArea(aMaskedFrame);
        clipArea = ThebesRect(
            (CSSRect::FromUnknownRect(ToRect(clipArea)) * cssToDevScale)
                .ToUnknownRect());
      } else {
        const auto& layer = svgReset->mMask.mLayers[i];
        if (layer.mClip == StyleGeometryBox::NoClip) {
          return Nothing();
        }

        nsCSSRendering::ImageLayerClipState clipState;
        nsCSSRendering::GetImageLayerClip(
            layer, aMaskedFrame, *aMaskedFrame->StyleBorder(), borderArea,
            dirtyRect, false /* aWillPaintBorder */, devPixelRatio, &clipState);
        clipArea = clipState.mDirtyRectInDevPx;
      }
      combinedClip = UnionMaybeRects(combinedClip, Some(clipArea));
    }
  }
  if (combinedClip) {
    // Convert to user space.
    *combinedClip += devPixelOffsetToUserSpace;

    // Round the clip out. In FrameLayerBuilder we round clips to nearest
    // pixels, and if we have a really thin clip here, that can cause the
    // clip to become empty if we didn't round out here.
    // The rounding happens in coordinates that are relative to the reference
    // frame, which matches what FrameLayerBuilder does.
    combinedClip->RoundOut();

    // Convert to app units.
    nsRect result =
        nsLayoutUtils::RoundGfxRectToAppRect(*combinedClip, devPixelRatio);

    // The resulting clip is relative to the reference frame, but the caller
    // expects it to be relative to the masked frame, so adjust it.
    result -= toReferenceFrame;
    return Some(result);
  }
  return Nothing();
}

struct AutoCheckBuilder {
  explicit AutoCheckBuilder(nsDisplayListBuilder* aBuilder)
      : mBuilder(aBuilder) {
    aBuilder->Check();
  }

  ~AutoCheckBuilder() { mBuilder->Check(); }

  nsDisplayListBuilder* mBuilder;
};

/**
 * Tries to reuse a top-level stacking context item from the previous paint.
 * Returns true if an item was reused, otherwise false.
 */
bool TryToReuseStackingContextItem(nsDisplayListBuilder* aBuilder,
                                   nsDisplayList* aList, nsIFrame* aFrame) {
  if (!aBuilder->IsForPainting() || !aBuilder->IsPartialUpdate() ||
      aBuilder->InInvalidSubtree()) {
    return false;
  }

  if (aFrame->IsFrameModified() || aFrame->HasModifiedDescendants()) {
    return false;
  }

  auto& items = aFrame->DisplayItems();
  auto* res = std::find_if(
      items.begin(), items.end(),
      [](nsDisplayItem* aItem) { return aItem->IsPreProcessed(); });

  if (res == items.end()) {
    return false;
  }

  nsDisplayItem* container = *res;
  MOZ_ASSERT(container->Frame() == aFrame);
  DL_LOGD("RDL - Found SC item %p (%s) (frame: %p)", container,
          container->Name(), container->Frame());

  aList->AppendToTop(container);
  aBuilder->ReuseDisplayItem(container);
  return true;
}

void nsIFrame::BuildDisplayListForStackingContext(
    nsDisplayListBuilder* aBuilder, nsDisplayList* aList,
    bool* aCreatedContainerItem) {
#ifdef DEBUG
  DL_LOGV("BuildDisplayListForStackingContext (%p) <", this);
  ScopeExit e(
      [this]() { DL_LOGV("> BuildDisplayListForStackingContext (%p)", this); });
#endif

  AutoCheckBuilder check(aBuilder);

  if (aBuilder->IsReusingStackingContextItems() &&
      TryToReuseStackingContextItem(aBuilder, aList, this)) {
    if (aCreatedContainerItem) {
      *aCreatedContainerItem = true;
    }
    return;
  }

  if (HasAnyStateBits(NS_FRAME_TOO_DEEP_IN_FRAME_TREE)) {
    return;
  }

  const auto& style = *Style();
  const nsStyleDisplay* disp = style.StyleDisplay();
  const nsStyleEffects* effects = style.StyleEffects();
  EffectSet* effectSetForOpacity =
      EffectSet::GetForFrame(this, nsCSSPropertyIDSet::OpacityProperties());
  // We can stop right away if this is a zero-opacity stacking context and
  // we're painting, and we're not animating opacity.
  bool needHitTestInfo = aBuilder->BuildCompositorHitTestInfo() &&
                         Style()->PointerEvents() != StylePointerEvents::None;
  bool opacityItemForEventsOnly = false;
  if (effects->IsTransparent() && aBuilder->IsForPainting() &&
      !(disp->mWillChange.bits & StyleWillChangeBits::OPACITY) &&
      !nsLayoutUtils::HasAnimationOfPropertySet(
          this, nsCSSPropertyIDSet::OpacityProperties(), effectSetForOpacity)) {
    if (needHitTestInfo) {
      opacityItemForEventsOnly = true;
    } else {
      return;
    }
  }

  if (aBuilder->IsForPainting() && disp->mWillChange.bits) {
    aBuilder->AddToWillChangeBudget(this, GetSize());
  }

  // For preserves3d, use the dirty rect already installed on the
  // builder, since aDirtyRect maybe distorted for transforms along
  // the chain.
  nsRect visibleRect = aBuilder->GetVisibleRect();
  nsRect dirtyRect = aBuilder->GetDirtyRect();

  // We build an opacity item if it's not going to be drawn by SVG content.
  // We could in principle skip creating an nsDisplayOpacity item if
  // nsDisplayOpacity::NeedsActiveLayer returns false and usingSVGEffects is
  // true (the nsDisplayFilter/nsDisplayMasksAndClipPaths could handle the
  // opacity). Since SVG has perf issues where we sometimes spend a lot of
  // time creating display list items that might be helpful.  We'd need to
  // restore our mechanism to do that (changed in bug 1482403), and we'd
  // need to invalidate the frame if the value that would be return from
  // NeedsActiveLayer was to change, which we don't currently do.
  const bool useOpacity =
      HasVisualOpacity(disp, effects, effectSetForOpacity) &&
      !SVGUtils::CanOptimizeOpacity(this);

  const bool isTransformed = IsTransformed();
  const bool hasPerspective = isTransformed && HasPerspective();
  const bool extend3DContext =
      Extend3DContext(disp, effects, effectSetForOpacity);
  const bool combines3DTransformWithAncestors =
      (extend3DContext || isTransformed) && Combines3DTransformWithAncestors();

  Maybe<nsDisplayListBuilder::AutoPreserves3DContext> autoPreserves3DContext;
  if (extend3DContext && !combines3DTransformWithAncestors) {
    // Start a new preserves3d context to keep informations on
    // nsDisplayListBuilder.
    autoPreserves3DContext.emplace(aBuilder);
    // Save dirty rect on the builder to avoid being distorted for
    // multiple transforms along the chain.
    aBuilder->SavePreserves3DRect();

    // We rebuild everything within preserve-3d and don't try
    // to retain, so override the dirty rect now.
    if (aBuilder->IsRetainingDisplayList()) {
      dirtyRect = visibleRect;
      aBuilder->SetDisablePartialUpdates(true);
    }
  }

  const bool useBlendMode = effects->mMixBlendMode != StyleBlend::Normal;
  if (useBlendMode) {
    aBuilder->SetContainsBlendMode(true);
  }

  // reset blend mode so we can keep track if this stacking context needs have
  // a nsDisplayBlendContainer. Set the blend mode back when the routine exits
  // so we keep track if the parent stacking context needs a container too.
  AutoSaveRestoreContainsBlendMode autoRestoreBlendMode(*aBuilder);
  aBuilder->SetContainsBlendMode(false);

  // NOTE: When changing this condition make sure to tweak ScrollContainerFrame
  // as well.
  bool usingBackdropFilter = effects->HasBackdropFilters() &&
                             IsVisibleForPainting() &&
                             !style.IsRootElementStyle();

  nsRect visibleRectOutsideTransform = visibleRect;
  nsDisplayTransform::PrerenderInfo prerenderInfo;
  bool inTransform = aBuilder->IsInTransform();
  if (isTransformed) {
    prerenderInfo = nsDisplayTransform::ShouldPrerenderTransformedContent(
        aBuilder, this, &visibleRect);

    switch (prerenderInfo.mDecision) {
      case nsDisplayTransform::PrerenderDecision::Full:
      case nsDisplayTransform::PrerenderDecision::Partial:
        dirtyRect = visibleRect;
        break;
      case nsDisplayTransform::PrerenderDecision::No: {
        // If we didn't prerender an animated frame in a preserve-3d context,
        // then we want disable async animations for the rest of the preserve-3d
        // (especially ancestors).
        if ((extend3DContext || combines3DTransformWithAncestors) &&
            prerenderInfo.mHasAnimations) {
          aBuilder->SavePreserves3DAllowAsyncAnimation(false);
        }

        const nsRect overflow = InkOverflowRectRelativeToSelf();
        if (overflow.IsEmpty() && !extend3DContext) {
          return;
        }

        // If we're in preserve-3d then grab the dirty rect that was given to
        // the root and transform using the combined transform.
        if (combines3DTransformWithAncestors) {
          visibleRect = dirtyRect = aBuilder->GetPreserves3DRect();
        }

        const float appPerDev = PresContext()->AppUnitsPerDevPixel();
        uint32_t flags = nsDisplayTransform::kTransformRectFlags &
                         ~nsDisplayTransform::OFFSET_BY_ORIGIN;
        if (!hasPerspective) {
          flags &= ~nsDisplayTransform::INCLUDE_PERSPECTIVE;
        }
        if (!combines3DTransformWithAncestors) {
          flags &= ~nsDisplayTransform::INCLUDE_PRESERVE3D_ANCESTORS;
        }
        auto transform = nsDisplayTransform::GetResultingTransformMatrix(
            this, nsPoint(), appPerDev, flags);
        nsRect untransformedDirtyRect;
        if (nsDisplayTransform::UntransformRect(dirtyRect, overflow, transform,
                                                appPerDev,
                                                &untransformedDirtyRect)) {
          dirtyRect = untransformedDirtyRect;
          nsDisplayTransform::UntransformRect(visibleRect, overflow, transform,
                                              appPerDev, &visibleRect);
        } else {
          // This should only happen if the transform is singular, in which case
          // nothing is visible anyway
          dirtyRect.SetEmpty();
          visibleRect.SetEmpty();
        }
      }
    }
    inTransform = true;
  } else if (IsFixedPosContainingBlock()) {
    // Restict the building area to the overflow rect for these frames, since
    // RetainedDisplayListBuilder uses it to know if the size of the stacking
    // context changed.
    visibleRect.IntersectRect(visibleRect, InkOverflowRect());
    dirtyRect.IntersectRect(dirtyRect, InkOverflowRect());
  }

  bool hasOverrideDirtyRect = false;
  // If we're doing a partial build, we're not invalid and we're capable
  // of having an override building rect (stacking context and fixed pos
  // containing block), then we should assume we have one.
  // Either we have an explicit one, or nothing in our subtree changed and
  // we have an implicit empty rect.
  //
  // These conditions should match |CanStoreDisplayListBuildingRect()| in
  // RetainedDisplayListBuilder.cpp
  if (!aBuilder->IsReusingStackingContextItems() &&
      aBuilder->IsPartialUpdate() && !aBuilder->InInvalidSubtree() &&
      !IsFrameModified() && IsFixedPosContainingBlock() &&
      !GetPrevContinuation() && !GetNextContinuation()) {
    dirtyRect = nsRect();
    if (HasOverrideDirtyRegion()) {
      nsDisplayListBuilder::DisplayListBuildingData* data =
          GetProperty(nsDisplayListBuilder::DisplayListBuildingRect());
      if (data) {
        dirtyRect = data->mDirtyRect.Intersect(visibleRect);
        hasOverrideDirtyRect = true;
      }
    }
  }

  bool usingFilter = effects->HasFilters() && !style.IsRootElementStyle();
  SVGUtils::MaskUsage maskUsage = SVGUtils::DetermineMaskUsage(this, false);
  bool usingMask = maskUsage.UsingMaskOrClipPath();
  bool usingSVGEffects = usingFilter || usingMask;

  nsRect visibleRectOutsideSVGEffects = visibleRect;
  nsDisplayList hoistedScrollInfoItemsStorage(aBuilder);
  if (usingSVGEffects) {
    dirtyRect =
        SVGIntegrationUtils::GetRequiredSourceForInvalidArea(this, dirtyRect);
    visibleRect =
        SVGIntegrationUtils::GetRequiredSourceForInvalidArea(this, visibleRect);
    aBuilder->EnterSVGEffectsContents(this, &hoistedScrollInfoItemsStorage);
  }

  bool useStickyPosition = disp->mPosition == StylePositionProperty::Sticky;

  bool useFixedPosition =
      disp->mPosition == StylePositionProperty::Fixed &&
      aBuilder->IsPaintingToWindow() && !IsMenuPopupFrame() &&
      (DisplayPortUtils::IsFixedPosFrameInDisplayPort(this) ||
       BuilderHasScrolledClip(aBuilder));

  nsDisplayListBuilder::AutoBuildingDisplayList buildingDisplayList(
      aBuilder, this, visibleRect, dirtyRect, isTransformed);

  UpdateCurrentHitTestInfo(aBuilder, this);

  // Depending on the effects that are applied to this frame, we can create
  // multiple container display items and wrap them around our contents.
  // This enum lists all the potential container display items, in the order
  // outside to inside.
  enum class ContainerItemType : uint8_t {
    None = 0,
    FixedPosition,
    OwnLayerForTransformWithRoundedClip,
    Perspective,
    Transform,
    Filter,
  };

  nsDisplayListBuilder::AutoContainerASRTracker contASRTracker(aBuilder);

  auto cssClip = GetClipPropClipRect(disp, effects, GetSize());
  auto ApplyClipProp = [&](DisplayListClipState::AutoSaveRestore& aClipState) {
    if (!cssClip) {
      return;
    }
    nsPoint offset = aBuilder->GetCurrentFrameOffsetToReferenceFrame();
    aBuilder->IntersectDirtyRect(*cssClip);
    aBuilder->IntersectVisibleRect(*cssClip);
    aClipState.ClipContentDescendants(*cssClip + offset);
  };

  // The CSS clip property is effectively inside the transform, but outside the
  // filters. So if we're not transformed we can apply it just here for
  // simplicity, instead of on each of the places that handle clipCapturedBy.
  DisplayListClipState::AutoSaveRestore untransformedCssClip(aBuilder);
  if (!isTransformed) {
    ApplyClipProp(untransformedCssClip);
  }

  // If there is a current clip, then depending on the container items we
  // create, different things can happen to it. Some container items simply
  // propagate the clip to their children and aren't clipped themselves.
  // But other container items, especially those that establish a different
  // geometry for their contents (e.g. transforms), capture the clip on
  // themselves and unset the clip for their contents. If we create more than
  // one of those container items, the clip will be captured on the outermost
  // one and the inner container items will be unclipped.
  ContainerItemType clipCapturedBy = ContainerItemType::None;
  if (useFixedPosition) {
    clipCapturedBy = ContainerItemType::FixedPosition;
  } else if (isTransformed) {
    const DisplayItemClipChain* currentClip =
        aBuilder->ClipState().GetCurrentCombinedClipChain(aBuilder);
    if ((hasPerspective || extend3DContext) &&
        (currentClip && currentClip->HasRoundedCorners())) {
      // If we're creating an nsDisplayTransform item that is going to combine
      // its transform with its children (preserve-3d or perspective), then we
      // can't have an intermediate surface. Mask layers force an intermediate
      // surface, so if we're going to need both then create a separate
      // wrapping layer for the mask.
      clipCapturedBy = ContainerItemType::OwnLayerForTransformWithRoundedClip;
    } else if (hasPerspective) {
      clipCapturedBy = ContainerItemType::Perspective;
    } else {
      clipCapturedBy = ContainerItemType::Transform;
    }
  } else if (usingFilter) {
    clipCapturedBy = ContainerItemType::Filter;
  }

  DisplayListClipState::AutoSaveRestore clipState(aBuilder);
  if (clipCapturedBy != ContainerItemType::None) {
    clipState.Clear();
  }

  DisplayListClipState::AutoSaveRestore transformedCssClip(aBuilder);
  if (isTransformed) {
    // FIXME(emilio, bug 1525159): In the case we have a both a transform _and_
    // filters, this clips the input to the filters as well, which is not
    // correct (clipping by the `clip` property is supposed to happen after
    // applying the filter effects, per [1].
    //
    // This is not a regression though, since we used to do that anyway before
    // bug 1514384, and even without the transform we get it wrong.
    //
    // [1]: https://drafts.fxtf.org/css-masking/#placement
    ApplyClipProp(transformedCssClip);
  }

  uint32_t numActiveScrollframesEncounteredBefore =
      aBuilder->GetNumActiveScrollframesEncountered();

  nsDisplayListCollection set(aBuilder);
  Maybe<nsRect> clipForMask;
  {
    DisplayListClipState::AutoSaveRestore nestedClipState(aBuilder);
    nsDisplayListBuilder::AutoInTransformSetter inTransformSetter(aBuilder,
                                                                  inTransform);
    nsDisplayListBuilder::AutoEnterFilter filterASRSetter(aBuilder,
                                                          usingFilter);
    nsDisplayListBuilder::AutoInEventsOnly inEventsSetter(
        aBuilder, opacityItemForEventsOnly);

    // If we have a mask, compute a clip to bound the masked content.
    // This is necessary in case the content moves with an ancestor
    // ASR of the mask.
    // Don't do this if we also have a filter, because then the clip
    // would be applied before the filter, violating
    // https://www.w3.org/TR/filter-effects-1/#placement.
    // Filters are a containing block for fixed and absolute descendants,
    // so the masked content cannot move with an ancestor ASR.
    if (usingMask && !usingFilter) {
      clipForMask = ComputeClipForMaskItem(aBuilder, this, maskUsage);
      if (clipForMask) {
        aBuilder->IntersectDirtyRect(*clipForMask);
        aBuilder->IntersectVisibleRect(*clipForMask);
        nestedClipState.ClipContentDescendants(
            *clipForMask + aBuilder->GetCurrentFrameOffsetToReferenceFrame());
      }
    }

    // extend3DContext also guarantees that applyAbsPosClipping and
    // usingSVGEffects are false We only modify the preserve-3d rect if we are
    // the top of a preserve-3d heirarchy
    if (extend3DContext) {
      // Mark these first so MarkAbsoluteFramesForDisplayList knows if we are
      // going to be forced to descend into frames.
      aBuilder->MarkPreserve3DFramesForDisplayList(this);
    }

    aBuilder->AdjustWindowDraggingRegion(this);

    MarkAbsoluteFramesForDisplayList(aBuilder);
    aBuilder->Check();
    BuildDisplayList(aBuilder, set);
    SetBuiltDisplayList(true);
    aBuilder->Check();
    aBuilder->DisplayCaret(this, set.Outlines());

    // Blend modes are a real pain for retained display lists. We build a blend
    // container item if the built list contains any blend mode items within
    // the current stacking context. This can change without an invalidation
    // to the stacking context frame, or the blend mode frame (e.g. by moving
    // an intermediate frame).
    // When we gain/remove a blend container item, we need to mark this frame
    // as invalid and have the full display list for merging to track
    // the change correctly.
    // It seems really hard to track this in advance, as the bookkeeping
    // required to note which stacking contexts have blend descendants
    // is complex and likely to be buggy.
    // Instead we're doing the sad thing, detecting it afterwards, and just
    // repeating display list building if it changed.
    // We have to repeat building for the entire display list (or at least
    // the outer stacking context), since we need to mark this frame as invalid
    // to remove any existing content that isn't wrapped in the blend container,
    // and then we need to build content infront/behind the blend container
    // to get correct positioning during merging.
    if (aBuilder->ContainsBlendMode() && aBuilder->IsRetainingDisplayList()) {
      if (aBuilder->IsPartialUpdate()) {
        aBuilder->SetPartialBuildFailed(true);
      } else {
        aBuilder->SetDisablePartialUpdates(true);
      }
    }
  }

  if (aBuilder->IsBackgroundOnly()) {
    set.BlockBorderBackgrounds()->DeleteAll(aBuilder);
    set.Floats()->DeleteAll(aBuilder);
    set.Content()->DeleteAll(aBuilder);
    set.PositionedDescendants()->DeleteAll(aBuilder);
    set.Outlines()->DeleteAll(aBuilder);
  }

  if (hasOverrideDirtyRect &&
      StaticPrefs::layout_display_list_show_rebuild_area()) {
    nsDisplaySolidColor* color = MakeDisplayItem<nsDisplaySolidColor>(
        aBuilder, this,
        dirtyRect + aBuilder->GetCurrentFrameOffsetToReferenceFrame(),
        NS_RGBA(255, 0, 0, 64), false);
    if (color) {
      color->SetOverrideZIndex(INT32_MAX);
      set.PositionedDescendants()->AppendToTop(color);
    }
  }

  nsIContent* content = GetContent();
  if (!content) {
    content = PresContext()->Document()->GetRootElement();
  }

  nsDisplayList resultList(aBuilder);
  set.SerializeWithCorrectZOrder(&resultList, content);

  // Get the ASR to use for the container items that we create here.
  const ActiveScrolledRoot* containerItemASR = contASRTracker.GetContainerASR();

  bool createdContainer = false;

  // If adding both a nsDisplayBlendContainer and a nsDisplayBlendMode to the
  // same list, the nsDisplayBlendContainer should be added first. This only
  // happens when the element creating this stacking context has mix-blend-mode
  // and also contains a child which has mix-blend-mode.
  // The nsDisplayBlendContainer must be added to the list first, so it does not
  // isolate the containing element blending as well.
  if (aBuilder->ContainsBlendMode()) {
    resultList.AppendToTop(nsDisplayBlendContainer::CreateForMixBlendMode(
        aBuilder, this, &resultList, containerItemASR));
    createdContainer = true;
  }

  if (usingBackdropFilter) {
    nsRect backdropRect =
        GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
    resultList.AppendNewToTop<nsDisplayBackdropFilters>(
        aBuilder, this, &resultList, backdropRect, this);
    createdContainer = true;
  }

  // If there are any SVG effects, wrap the list up in an SVG effects item
  // (which also handles CSS group opacity). Note that we create an SVG effects
  // item even if resultList is empty, since a filter can produce graphical
  // output even if the element being filtered wouldn't otherwise do so.
  if (usingSVGEffects) {
    MOZ_ASSERT(usingFilter || usingMask,
               "Beside filter & mask/clip-path, what else effect do we have?");

    if (clipCapturedBy == ContainerItemType::Filter) {
      clipState.Restore();
    }
    // Revert to the post-filter dirty rect.
    aBuilder->SetVisibleRect(visibleRectOutsideSVGEffects);

    // Skip all filter effects while generating glyph mask.
    if (usingFilter && !aBuilder->IsForGenerateGlyphMask()) {
      /* List now emptied, so add the new list to the top. */
      resultList.AppendNewToTop<nsDisplayFilters>(aBuilder, this, &resultList,
                                                  this, usingBackdropFilter);
      createdContainer = true;
    }

    if (usingMask) {
      // The mask should move with aBuilder->CurrentActiveScrolledRoot(), so
      // that's the ASR we prefer to use for the mask item. However, we can
      // only do this if the mask if clipped with respect to that ASR, because
      // an item always needs to have finite bounds with respect to its ASR.
      // If we weren't able to compute a clip for the mask, we fall back to
      // using containerItemASR, which is the lowest common ancestor clip of
      // the mask's contents. That's not entirely correct, but it satisfies
      // the base requirement of the ASR system (that items have finite bounds
      // wrt. their ASR).
      const ActiveScrolledRoot* maskASR =
          clipForMask.isSome() ? aBuilder->CurrentActiveScrolledRoot()
                               : containerItemASR;
      /* List now emptied, so add the new list to the top. */
      resultList.AppendNewToTop<nsDisplayMasksAndClipPaths>(
          aBuilder, this, &resultList, maskASR, usingBackdropFilter);
      createdContainer = true;
    }

    // TODO(miko): We could probably create a wraplist here and avoid creating
    // it later in |BuildDisplayListForChild()|.
    createdContainer = false;

    // Also add the hoisted scroll info items. We need those for APZ scrolling
    // because nsDisplayMasksAndClipPaths items can't build active layers.
    aBuilder->ExitSVGEffectsContents();
    resultList.AppendToTop(&hoistedScrollInfoItemsStorage);
  }

  // If the list is non-empty and there is CSS group opacity without SVG
  // effects, wrap it up in an opacity item.
  if (useOpacity) {
    const bool needsActiveOpacityLayer =
        nsDisplayOpacity::NeedsActiveLayer(aBuilder, this);
    resultList.AppendNewToTop<nsDisplayOpacity>(
        aBuilder, this, &resultList, containerItemASR, opacityItemForEventsOnly,
        needsActiveOpacityLayer, usingBackdropFilter);
    createdContainer = true;
  }

  // FIXME: Ensure this is the right place to do this.
  if (HasAnyStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION) &&
      StaticPrefs::dom_viewTransitions_live_capture()) {
    resultList.AppendNewToTop<nsDisplayViewTransitionCapture>(
        aBuilder, this, &resultList, containerItemASR, /* aIsRoot = */ false);
    createdContainer = true;
  }

  // If we're going to apply a transformation and don't have preserve-3d set,
  // wrap everything in an nsDisplayTransform. If there's nothing in the list,
  // don't add anything.
  //
  // For the preserve-3d case we want to individually wrap every child in the
  // list with a separate nsDisplayTransform instead. When the child is already
  // an nsDisplayTransform, we can skip this step, as the computed transform
  // will already include our own.
  //
  // We also traverse into sublists created by nsDisplayWrapList, so that we
  // find all the correct children.
  if (isTransformed && extend3DContext) {
    // Install dummy nsDisplayTransform as a leaf containing
    // descendants not participating this 3D rendering context.
    nsDisplayList nonparticipants(aBuilder);
    nsDisplayList participants(aBuilder);
    int index = 1;

    nsDisplayItem* separator = nullptr;

    // TODO: This can be simplified: |participants| is just |resultList|.
    for (nsDisplayItem* item : resultList.TakeItems()) {
      if (ItemParticipatesIn3DContext(this, item) &&
          !item->GetClip().HasClip()) {
        // The frame of this item participates the same 3D context.
        WrapSeparatorTransform(aBuilder, this, &nonparticipants, &participants,
                               index++, &separator);

        participants.AppendToTop(item);
      } else {
        // The frame of the item doesn't participate the current
        // context, or has no transform.
        //
        // For items participating but not transformed, they are add
        // to nonparticipants to get a separator layer for handling
        // clips, if there is, on an intermediate surface.
        // \see ContainerLayer::DefaultComputeEffectiveTransforms().
        nonparticipants.AppendToTop(item);
      }
    }
    WrapSeparatorTransform(aBuilder, this, &nonparticipants, &participants,
                           index++, &separator);

    if (separator) {
      createdContainer = true;
    }

    resultList.AppendToTop(&participants);
  }

  if (isTransformed) {
    transformedCssClip.Restore();
    if (clipCapturedBy == ContainerItemType::Transform) {
      // Restore clip state now so nsDisplayTransform is clipped properly.
      clipState.Restore();
    }
    // Revert to the dirtyrect coming in from the parent, without our transform
    // taken into account.
    aBuilder->SetVisibleRect(visibleRectOutsideTransform);

    if (this != aBuilder->RootReferenceFrame()) {
      // Revert to the outer reference frame and offset because all display
      // items we create from now on are outside the transform.
      nsPoint toOuterReferenceFrame;
      const nsIFrame* outerReferenceFrame =
          aBuilder->FindReferenceFrameFor(GetParent(), &toOuterReferenceFrame);
      toOuterReferenceFrame += GetPosition();

      buildingDisplayList.SetReferenceFrameAndCurrentOffset(
          outerReferenceFrame, toOuterReferenceFrame);
    }

    // We would like to block async animations for ancestors of ones not
    // prerendered in the preserve-3d tree. Now that we've finished processing
    // all descendants, update allowAsyncAnimation to take their prerender
    // state into account
    // FIXME: We don't block async animations for previous siblings because
    // their prerender decisions have been made. We may have to figure out a
    // better way to rollback their prerender decisions.
    // Alternatively we could not block animations for later siblings, and only
    // block them for ancestors of a blocked one.
    if ((extend3DContext || combines3DTransformWithAncestors) &&
        prerenderInfo.CanUseAsyncAnimations() &&
        !aBuilder->GetPreserves3DAllowAsyncAnimation()) {
      // aBuilder->GetPreserves3DAllowAsyncAnimation() means the inner or
      // previous silbing frames are allowed/disallowed for async animations.
      prerenderInfo.mDecision = nsDisplayTransform::PrerenderDecision::No;
    }

    nsDisplayTransform* transformItem = MakeDisplayItem<nsDisplayTransform>(
        aBuilder, this, &resultList, visibleRect, prerenderInfo.mDecision,
        usingBackdropFilter);
    if (transformItem) {
      resultList.AppendToTop(transformItem);
      createdContainer = true;

      if (numActiveScrollframesEncounteredBefore !=
          aBuilder->GetNumActiveScrollframesEncountered()) {
        transformItem->SetContainsASRs(true);
      }

      if (hasPerspective) {
        transformItem->MarkWithAssociatedPerspective();

        if (clipCapturedBy == ContainerItemType::Perspective) {
          clipState.Restore();
        }
        resultList.AppendNewToTop<nsDisplayPerspective>(aBuilder, this,
                                                        &resultList);
        createdContainer = true;
      }
    }
  }

  if (clipCapturedBy ==
      ContainerItemType::OwnLayerForTransformWithRoundedClip) {
    clipState.Restore();
    resultList.AppendNewToTopWithIndex<nsDisplayOwnLayer>(
        aBuilder, this,
        /* aIndex = */ nsDisplayOwnLayer::OwnLayerForTransformWithRoundedClip,
        &resultList, aBuilder->CurrentActiveScrolledRoot(),
        nsDisplayOwnLayerFlags::None, ScrollbarData{},
        /* aForceActive = */ false, false);
    createdContainer = true;
  }

  // If we have sticky positioning, wrap it in a sticky position item.
  if (useFixedPosition) {
    if (clipCapturedBy == ContainerItemType::FixedPosition) {
      clipState.Restore();
    }
    // The ASR for the fixed item should be the ASR of our containing block,
    // which has been set as the builder's current ASR, unless this frame is
    // invisible and we hadn't saved display item data for it. In that case,
    // we need to take the containerItemASR since we might have fixed children.
    // For WebRender, we want to the know what |containerItemASR| is for the
    // case where the fixed-pos item is not a "real" fixed-pos item (e.g. it's
    // nested inside a scrolling transform), so we stash that on the display
    // item as well.
    const ActiveScrolledRoot* fixedASR = ActiveScrolledRoot::PickAncestor(
        containerItemASR, aBuilder->CurrentActiveScrolledRoot());
    resultList.AppendNewToTop<nsDisplayFixedPosition>(
        aBuilder, this, &resultList, fixedASR, containerItemASR);
    createdContainer = true;
  } else if (useStickyPosition) {
    // For position:sticky, the clip needs to be applied both to the sticky
    // container item and to the contents. The container item needs the clip
    // because a scrolled clip needs to move independently from the sticky
    // contents, and the contents need the clip so that they have finite
    // clipped bounds with respect to the container item's ASR. The latter is
    // a little tricky in the case where the sticky item has both fixed and
    // non-fixed descendants, because that means that the sticky container
    // item's ASR is the ASR of the fixed descendant.
    // For WebRender display list building, though, we still want to know the
    // the ASR that the sticky container item would normally have, so we stash
    // that on the display item as the "container ASR" (i.e. the normal ASR of
    // the container item, excluding the special behaviour induced by fixed
    // descendants).
    const ActiveScrolledRoot* stickyASR = ActiveScrolledRoot::PickAncestor(
        containerItemASR, aBuilder->CurrentActiveScrolledRoot());

    auto* stickyItem = MakeDisplayItem<nsDisplayStickyPosition>(
        aBuilder, this, &resultList, stickyASR,
        aBuilder->CurrentActiveScrolledRoot(),
        clipState.IsClippedToDisplayPort());

    bool shouldFlatten = true;

    StickyScrollContainer* stickyScrollContainer =
        StickyScrollContainer::GetStickyScrollContainerForFrame(this);
    if (stickyScrollContainer && stickyScrollContainer->ScrollContainer()
                                     ->IsMaybeAsynchronouslyScrolled()) {
      shouldFlatten = false;
    }

    stickyItem->SetShouldFlatten(shouldFlatten);

    resultList.AppendToTop(stickyItem);
    createdContainer = true;

    // If the sticky element is inside a filter, annotate the scroll frame that
    // scrolls the filter as having out-of-flow content inside a filter (this
    // inhibits paint skipping).
    if (aBuilder->GetFilterASR() && aBuilder->GetFilterASR() == stickyASR) {
      aBuilder->GetFilterASR()
          ->mScrollContainerFrame->SetHasOutOfFlowContentInsideFilter();
    }
  }

  // If there's blending, wrap up the list in a blend-mode item. Note that
  // opacity can be applied before blending as the blend color is not affected
  // by foreground opacity (only background alpha).
  if (useBlendMode) {
    DisplayListClipState::AutoSaveRestore blendModeClipState(aBuilder);
    resultList.AppendNewToTop<nsDisplayBlendMode>(aBuilder, this, &resultList,
                                                  effects->mMixBlendMode,
                                                  containerItemASR, false);
    createdContainer = true;
  }

  if (aBuilder->IsReusingStackingContextItems()) {
    if (resultList.IsEmpty()) {
      return;
    }

    nsDisplayItem* container = resultList.GetBottom();
    if (resultList.Length() > 1 || container->Frame() != this) {
      container = MakeDisplayItem<nsDisplayContainer>(
          aBuilder, this, containerItemASR, &resultList);
    } else {
      MOZ_ASSERT(resultList.Length() == 1);
      resultList.Clear();
    }

    // Mark the outermost display item as reusable. These display items and
    // their chidren can be reused during the next paint if no ancestor or
    // descendant frames have been modified.
    if (!container->IsReusedItem()) {
      container->SetReusable();
    }
    aList->AppendToTop(container);
    createdContainer = true;
  } else {
    aList->AppendToTop(&resultList);
  }

  if (aCreatedContainerItem) {
    *aCreatedContainerItem = createdContainer;
  }
}

static nsDisplayItem* WrapInWrapList(nsDisplayListBuilder* aBuilder,
                                     nsIFrame* aFrame, nsDisplayList* aList,
                                     const ActiveScrolledRoot* aContainerASR,
                                     bool aBuiltContainerItem = false) {
  nsDisplayItem* item = aList->GetBottom();
  if (!item) {
    return nullptr;
  }

  // We need a wrap list if there are multiple items, or if the single
  // item has a different frame. This can change in a partial build depending
  // on which items we build, so we need to ensure that we don't transition
  // to/from a wrap list without invalidating correctly.
  bool needsWrapList =
      aList->Length() > 1 || item->Frame() != aFrame || item->GetChildren();

  // If we have an explicit container item (that can't change without an
  // invalidation) or we're doing a full build and don't need a wrap list, then
  // we can skip adding one.
  if (aBuiltContainerItem || (!aBuilder->IsPartialUpdate() && !needsWrapList)) {
    MOZ_ASSERT(aList->Length() == 1);
    aList->Clear();
    return item;
  }

  // If we're doing a partial build and we didn't need a wrap list
  // previously then we can try to work from there.
  if (aBuilder->IsPartialUpdate() &&
      !aFrame->HasDisplayItem(uint32_t(DisplayItemType::TYPE_CONTAINER))) {
    // If we now need a wrap list, we must previously have had no display items
    // or a single one belonging to this frame. Mark the item itself as
    // discarded so that RetainedDisplayListBuilder uses the ones we just built.
    // We don't want to mark the frame as modified as that would invalidate
    // positioned descendants that might be outside of this list, and might not
    // have been rebuilt this time.
    if (needsWrapList) {
      DiscardOldItems(aFrame);
    } else {
      MOZ_ASSERT(aList->Length() == 1);
      aList->Clear();
      return item;
    }
  }

  // The last case we could try to handle is when we previously had a wrap list,
  // but no longer need it. Unfortunately we can't differentiate this case from
  // a partial build where other children exist but we just didn't build them
  // this time.
  // TODO:RetainedDisplayListBuilder's merge phase has the full list and
  // could strip them out.

  return MakeDisplayItem<nsDisplayContainer>(aBuilder, aFrame, aContainerASR,
                                             aList);
}

/**
 * Check if a frame should be visited for building display list.
 */
static bool DescendIntoChild(nsDisplayListBuilder* aBuilder,
                             const nsIFrame* aChild, const nsRect& aVisible,
                             const nsRect& aDirty) {
  if (aChild->HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO)) {
    return true;
  }

  // If the child is a scrollframe that we want to ignore, then we need
  // to descend into it because its scrolled child may intersect the dirty
  // area even if the scrollframe itself doesn't.
  if (aChild == aBuilder->GetIgnoreScrollFrame()) {
    return true;
  }

  // There are cases where the "ignore scroll frame" on the builder is not set
  // correctly, and so we additionally want to catch cases where the child is
  // a root scrollframe and we are ignoring scrolling on the viewport.
  if (aChild == aBuilder->GetPresShellIgnoreScrollFrame()) {
    return true;
  }

  nsRect overflow = aChild->InkOverflowRect();

  // On mobile, there may be a dynamic toolbar. The root content document's
  // root scroll frame's ink overflow rect does not include the toolbar
  // height, but if the toolbar is hidden, we still want to be able to target
  // content underneath the toolbar, so expand the overflow rect here to
  // allow display list building to descend into the scroll frame.
  if (aBuilder->IsForEventDelivery() &&
      aChild == aChild->PresShell()->GetRootScrollContainerFrame() &&
      aChild->PresContext()->IsRootContentDocumentCrossProcess() &&
      aChild->PresContext()->HasDynamicToolbar()) {
    overflow.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
        aChild->PresContext(), overflow.Size()));
  }

  if (aDirty.Intersects(overflow)) {
    return true;
  }

  if (aChild->ForceDescendIntoIfVisible() && aVisible.Intersects(overflow)) {
    return true;
  }

  if (aChild->IsTablePart()) {
    // Relative positioning and transforms can cause table parts to move, but we
    // will still paint the backgrounds for their ancestor parts under them at
    // their 'normal' position. That means that we must consider the overflow
    // rects at both positions.

    // We convert the overflow rect into the nsTableFrame's coordinate
    // space, applying the normal position offset at each step. Then we
    // compare that against the builder's cached dirty rect in table
    // coordinate space.
    const nsIFrame* f = aChild;
    nsRect normalPositionOverflowRelativeToTable = overflow;

    while (f->IsTablePart()) {
      normalPositionOverflowRelativeToTable += f->GetNormalPosition();
      f = f->GetParent();
    }

    nsDisplayTableBackgroundSet* tableBGs = aBuilder->GetTableBackgroundSet();
    if (tableBGs && tableBGs->GetDirtyRect().Intersects(
                        normalPositionOverflowRelativeToTable)) {
      return true;
    }
  }

  return false;
}

void nsIFrame::BuildDisplayListForSimpleChild(nsDisplayListBuilder* aBuilder,
                                              nsIFrame* aChild,
                                              const nsDisplayListSet& aLists) {
  // This is the shortcut for frames been handled along the common
  // path, the most common one of THE COMMON CASE mentioned later.
  MOZ_ASSERT(aChild->Type() != LayoutFrameType::Placeholder);
  MOZ_ASSERT(!aBuilder->GetSelectedFramesOnly() &&
                 !aBuilder->GetIncludeAllOutOfFlows(),
             "It should be held for painting to window");
  MOZ_ASSERT(aChild->HasAnyStateBits(NS_FRAME_SIMPLE_DISPLAYLIST));

  const nsPoint offset = aChild->GetOffsetTo(this);
  const nsRect visible = aBuilder->GetVisibleRect() - offset;
  const nsRect dirty = aBuilder->GetDirtyRect() - offset;

  if (!DescendIntoChild(aBuilder, aChild, visible, dirty)) {
    DL_LOGV("Skipped frame %p", aChild);
    return;
  }

  // Child cannot be transformed since it is not a stacking context.
  nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
      aBuilder, aChild, visible, dirty, false);

  UpdateCurrentHitTestInfo(aBuilder, aChild);

  aChild->MarkAbsoluteFramesForDisplayList(aBuilder);
  aBuilder->AdjustWindowDraggingRegion(aChild);
  aBuilder->Check();
  aChild->BuildDisplayList(aBuilder, aLists);
  aChild->SetBuiltDisplayList(true);
  aBuilder->Check();
  aBuilder->DisplayCaret(aChild, aLists.Outlines());
}

static bool ShouldSkipFrame(nsDisplayListBuilder* aBuilder,
                            const nsIFrame* aFrame) {
  // If painting is restricted to just the background of the top level frame,
  // then we have nothing to do here.
  if (aBuilder->IsBackgroundOnly()) {
    return true;
  }
  if (aBuilder->IsForGenerateGlyphMask() &&
      (!aFrame->IsTextFrame() && aFrame->IsLeaf())) {
    return true;
  }
  // The placeholder frame should have the same content as the OOF frame.
  if (aBuilder->GetSelectedFramesOnly() &&
      (aFrame->IsLeaf() && !aFrame->IsSelected())) {
    return true;
  }
  static const nsFrameState skipFlags =
      (NS_FRAME_TOO_DEEP_IN_FRAME_TREE | NS_FRAME_IS_NONDISPLAY);
  if (aFrame->HasAnyStateBits(skipFlags)) {
    return true;
  }
  // Checking mMozSubtreeHiddenOnlyVisually is relatively slow because it
  // involves loading more memory. It's only allowed in chrome sheets so let's
  // only support it in the parent process so we can mostly optimize this out in
  // content processes.
  return XRE_IsParentProcess() &&
         aFrame->StyleUIReset()->mMozSubtreeHiddenOnlyVisually;
}

void nsIFrame::BuildDisplayListForChild(nsDisplayListBuilder* aBuilder,
                                        nsIFrame* aChild,
                                        const nsDisplayListSet& aLists,
                                        DisplayChildFlags aFlags) {
  AutoCheckBuilder check(aBuilder);
#ifdef DEBUG
  DL_LOGV("BuildDisplayListForChild (%p) <", aChild);
  ScopeExit e(
      [aChild]() { DL_LOGV("> BuildDisplayListForChild (%p)", aChild); });
#endif

  if (ShouldSkipFrame(aBuilder, aChild)) {
    return;
  }

  if (HidesContent()) {
    return;
  }

  nsIFrame* child = aChild;
  auto* placeholder = child->IsPlaceholderFrame()
                          ? static_cast<nsPlaceholderFrame*>(child)
                          : nullptr;
  nsIFrame* childOrOutOfFlow =
      placeholder ? placeholder->GetOutOfFlowFrame() : child;

  // If we're generating a display list for printing, include Link items for
  // frames that correspond to HTML link elements so that we can have active
  // links in saved PDF output. Note that the state of "within a link" is
  // set on the display-list builder, such that all descendants of the link
  // element will generate display-list links.
  // TODO: we should be able to optimize this so as to avoid creating links
  // for the same destination that entirely overlap each other, which adds
  // nothing useful to the final PDF.
  Maybe<nsDisplayListBuilder::Linkifier> linkifier;
  if (StaticPrefs::print_save_as_pdf_links_enabled() &&
      aBuilder->IsForPrinting()) {
    linkifier.emplace(aBuilder, childOrOutOfFlow, aLists.Content());
    linkifier->MaybeAppendLink(aBuilder, childOrOutOfFlow);
  }

  nsIFrame* parent = childOrOutOfFlow->GetParent();
  const auto* parentDisplay = parent->StyleDisplay();
  const auto overflowClipAxes =
      parent->ShouldApplyOverflowClipping(parentDisplay);

  const bool isPaintingToWindow = aBuilder->IsPaintingToWindow();
  const bool doingShortcut =
      isPaintingToWindow &&
      child->HasAnyStateBits(NS_FRAME_SIMPLE_DISPLAYLIST) &&
      // Animations may change the stacking context state.
      // ShouldApplyOverflowClipping is affected by the parent style, which does
      // not invalidate the NS_FRAME_SIMPLE_DISPLAYLIST bit.
      !(!overflowClipAxes.isEmpty() || child->MayHaveTransformAnimation() ||
        child->MayHaveOpacityAnimation());

  if (aBuilder->IsForPainting()) {
    aBuilder->ClearWillChangeBudgetStatus(child);
  }

  if (StaticPrefs::layout_css_scroll_anchoring_highlight()) {
    if (child->FirstContinuation()->IsScrollAnchor()) {
      nsRect bounds = child->GetContentRectRelativeToSelf() +
                      aBuilder->ToReferenceFrame(child);
      nsDisplaySolidColor* color = MakeDisplayItem<nsDisplaySolidColor>(
          aBuilder, child, bounds, NS_RGBA(255, 0, 255, 64));
      if (color) {
        color->SetOverrideZIndex(INT32_MAX);
        aLists.PositionedDescendants()->AppendToTop(color);
      }
    }
  }

  if (doingShortcut) {
    BuildDisplayListForSimpleChild(aBuilder, child, aLists);
    return;
  }

  // dirty rect in child-relative coordinates
  NS_ASSERTION(aBuilder->GetCurrentFrame() == this, "Wrong coord space!");
  const nsPoint offset = child->GetOffsetTo(this);
  nsRect visible = aBuilder->GetVisibleRect() - offset;
  nsRect dirty = aBuilder->GetDirtyRect() - offset;

  nsDisplayListBuilder::OutOfFlowDisplayData* savedOutOfFlowData = nullptr;
  if (placeholder) {
    if (placeholder->HasAnyStateBits(PLACEHOLDER_FOR_TOPLAYER)) {
      // If the out-of-flow frame is in the top layer, the viewport frame
      // will paint it. Skip it here. Note that, only out-of-flow frames
      // with this property should be skipped, because non-HTML elements
      // may stop their children from being out-of-flow. Those frames
      // should still be handled in the normal in-flow path.
      return;
    }

    child = childOrOutOfFlow;
    if (aBuilder->IsForPainting()) {
      aBuilder->ClearWillChangeBudgetStatus(child);
    }

    // If 'child' is a pushed float then it's owned by a block that's not an
    // ancestor of the placeholder, and it will be painted by that block and
    // should not be painted through the placeholder. Also recheck
    // NS_FRAME_TOO_DEEP_IN_FRAME_TREE and NS_FRAME_IS_NONDISPLAY.
    static const nsFrameState skipFlags =
        (NS_FRAME_IS_PUSHED_FLOAT | NS_FRAME_TOO_DEEP_IN_FRAME_TREE |
         NS_FRAME_IS_NONDISPLAY);
    if (child->HasAnyStateBits(skipFlags) || nsLayoutUtils::IsPopup(child)) {
      return;
    }

    MOZ_ASSERT(child->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW));
    savedOutOfFlowData = nsDisplayListBuilder::GetOutOfFlowData(child);

    if (aBuilder->GetIncludeAllOutOfFlows()) {
      visible = child->InkOverflowRect();
      dirty = child->InkOverflowRect();
    } else if (savedOutOfFlowData) {
      visible =
          savedOutOfFlowData->GetVisibleRectForFrame(aBuilder, child, &dirty);
    } else {
      // The out-of-flow frame did not intersect the dirty area. We may still
      // need to traverse into it, since it may contain placeholders we need
      // to enter to reach other out-of-flow frames that are visible.
      visible.SetEmpty();
      dirty.SetEmpty();
    }
  }

  NS_ASSERTION(!child->IsPlaceholderFrame(),
               "Should have dealt with placeholders already");

  if (!DescendIntoChild(aBuilder, child, visible, dirty)) {
    DL_LOGV("Skipped frame %p", child);
    return;
  }

  const bool isSVG = child->HasAnyStateBits(NS_FRAME_SVG_LAYOUT);

  // This flag is raised if the control flow strays off the common path.
  // The common path is the most common one of THE COMMON CASE mentioned later.
  bool awayFromCommonPath = !isPaintingToWindow;

  // true if this is a real or pseudo stacking context
  bool pseudoStackingContext =
      aFlags.contains(DisplayChildFlag::ForcePseudoStackingContext);

  if (!pseudoStackingContext && !isSVG &&
      aFlags.contains(DisplayChildFlag::Inline) &&
      !child->IsLineParticipant()) {
    // child is a non-inline frame in an inline context, i.e.,
    // it acts like inline-block or inline-table. Therefore it is a
    // pseudo-stacking-context.
    pseudoStackingContext = true;
  }

  const nsStyleDisplay* ourDisp = StyleDisplay();
  // Don't paint our children if the theme object is a leaf.
  if (IsThemed(ourDisp) && !PresContext()->Theme()->WidgetIsContainer(
                               ourDisp->EffectiveAppearance())) {
    return;
  }

  // Since we're now sure that we're adding this frame to the display list
  // (which means we're painting it, modulo occlusion), mark it as visible
  // within the displayport.
  if (isPaintingToWindow && child->TrackingVisibility() &&
      child->IsVisibleForPainting()) {
    child->PresShell()->EnsureFrameInApproximatelyVisibleList(child);
    awayFromCommonPath = true;
  }

  // Child is composited if it's transformed, partially transparent, or has
  // SVG effects or a blend mode..
  const nsStyleDisplay* disp = child->StyleDisplay();
  const nsStyleEffects* effects = child->StyleEffects();

  const bool isPositioned = disp->IsPositionedStyle();
  const bool isStackingContext =
      aFlags.contains(DisplayChildFlag::ForceStackingContext) ||
      child->IsStackingContext(disp, effects);

  if (pseudoStackingContext || isStackingContext || isPositioned ||
      placeholder || (!isSVG && disp->IsFloating(child)) ||
      (isSVG && effects->mClip.IsRect() && IsSVGContentWithCSSClip(child))) {
    pseudoStackingContext = true;
    awayFromCommonPath = true;
  }

  NS_ASSERTION(!isStackingContext || pseudoStackingContext,
               "Stacking contexts must also be pseudo-stacking-contexts");

  nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
      aBuilder, child, visible, dirty);

  UpdateCurrentHitTestInfo(aBuilder, child);

  DisplayListClipState::AutoClipMultiple clipState(aBuilder);
  nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(aBuilder);

  if (savedOutOfFlowData) {
    aBuilder->SetBuildingInvisibleItems(false);

    clipState.SetClipChainForContainingBlockDescendants(
        savedOutOfFlowData->mContainingBlockClipChain);
    asrSetter.SetCurrentActiveScrolledRoot(
        savedOutOfFlowData->mContainingBlockActiveScrolledRoot);
    asrSetter.SetCurrentScrollParentId(savedOutOfFlowData->mScrollParentId);
    MOZ_ASSERT(awayFromCommonPath,
               "It is impossible when savedOutOfFlowData is true");
  } else if (HasAnyStateBits(NS_FRAME_FORCE_DISPLAY_LIST_DESCEND_INTO) &&
             placeholder) {
    NS_ASSERTION(visible.IsEmpty(), "should have empty visible rect");
    // Every item we build from now until we descent into an out of flow that
    // does have saved out of flow data should be invisible. This state gets
    // restored when AutoBuildingDisplayList gets out of scope.
    aBuilder->SetBuildingInvisibleItems(true);

    // If we have nested out-of-flow frames and the outer one isn't visible
    // then we won't have stored clip data for it. We can just clear the clip
    // instead since we know we won't render anything, and the inner out-of-flow
    // frame will setup the correct clip for itself.
    clipState.SetClipChainForContainingBlockDescendants(nullptr);
  }

  // Setup clipping for the parent's overflow:clip,
  // or overflow:hidden on elements that don't support scrolling (and therefore
  // don't create nsHTML/XULScrollFrame). This clipping needs to not clip
  // anything directly rendered by the parent, only the rendering of its
  // children.
  // Don't use overflowClip to restrict the dirty rect, since some of the
  // descendants may not be clipped by it. Even if we end up with unnecessary
  // display items, they'll be pruned during ComputeVisibility.
  //
  // FIXME(emilio): Why can't we handle this more similarly to `clip` (on the
  // parent, rather than on the children)? Would ClipContentDescendants do what
  // we want?
  if (!overflowClipAxes.isEmpty()) {
    ApplyOverflowClipping(aBuilder, parent, overflowClipAxes, clipState);
    awayFromCommonPath = true;
  }

  nsDisplayList list(aBuilder);
  nsDisplayList extraPositionedDescendants(aBuilder);
  const ActiveScrolledRoot* wrapListASR;
  bool builtContainerItem = false;
  if (isStackingContext) {
    // True stacking context.
    // For stacking contexts, BuildDisplayListForStackingContext handles
    // clipping and MarkAbsoluteFramesForDisplayList.
    nsDisplayListBuilder::AutoContainerASRTracker contASRTracker(aBuilder);
    child->BuildDisplayListForStackingContext(aBuilder, &list,
                                              &builtContainerItem);
    wrapListASR = contASRTracker.GetContainerASR();
    if (!aBuilder->IsReusingStackingContextItems() &&
        aBuilder->GetCaretFrame() == child) {
      builtContainerItem = false;
    }
  } else {
    Maybe<nsRect> clipPropClip =
        child->GetClipPropClipRect(disp, effects, child->GetSize());
    if (clipPropClip) {
      aBuilder->IntersectVisibleRect(*clipPropClip);
      aBuilder->IntersectDirtyRect(*clipPropClip);
      clipState.ClipContentDescendants(*clipPropClip +
                                       aBuilder->ToReferenceFrame(child));
      awayFromCommonPath = true;
    }

    child->MarkAbsoluteFramesForDisplayList(aBuilder);
    child->SetBuiltDisplayList(true);

    // Some SVG frames might change opacity without invalidating the frame, so
    // exclude them from the fast-path.
    if (!awayFromCommonPath && !child->IsSVGFrame()) {
      // The shortcut is available for the child for next time.
      child->AddStateBits(NS_FRAME_SIMPLE_DISPLAYLIST);
    }

    if (!pseudoStackingContext) {
      // THIS IS THE COMMON CASE.
      // Not a pseudo or real stacking context. Do the simple thing and
      // return early.
      aBuilder->AdjustWindowDraggingRegion(child);
      aBuilder->Check();
      child->BuildDisplayList(aBuilder, aLists);
      aBuilder->Check();
      aBuilder->DisplayCaret(child, aLists.Outlines());
      return;
    }

    // A pseudo-stacking context (e.g., a positioned element with z-index auto).
    // We allow positioned descendants of the child to escape to our parent
    // stacking context's positioned descendant list, because they might be
    // z-index:non-auto
    nsDisplayListCollection pseudoStack(aBuilder);

    aBuilder->AdjustWindowDraggingRegion(child);
    nsDisplayListBuilder::AutoContainerASRTracker contASRTracker(aBuilder);
    aBuilder->Check();
    child->BuildDisplayList(aBuilder, pseudoStack);
    aBuilder->Check();
    if (aBuilder->DisplayCaret(child, pseudoStack.Outlines())) {
      builtContainerItem = false;
    }
    wrapListASR = contASRTracker.GetContainerASR();

    list.AppendToTop(pseudoStack.BorderBackground());
    list.AppendToTop(pseudoStack.BlockBorderBackgrounds());
    list.AppendToTop(pseudoStack.Floats());
    list.AppendToTop(pseudoStack.Content());
    list.AppendToTop(pseudoStack.Outlines());
    extraPositionedDescendants.AppendToTop(pseudoStack.PositionedDescendants());
  }

  buildingForChild.RestoreBuildingInvisibleItemsValue();

  if (!list.IsEmpty()) {
    if (isPositioned || isStackingContext) {
      // Genuine stacking contexts, and positioned pseudo-stacking-contexts,
      // go in this level.
      nsDisplayItem* item = WrapInWrapList(aBuilder, child, &list, wrapListASR,
                                           builtContainerItem);
      if (isSVG) {
        aLists.Content()->AppendToTop(item);
      } else {
        aLists.PositionedDescendants()->AppendToTop(item);
      }
    } else if (!isSVG && disp->IsFloating(child)) {
      aLists.Floats()->AppendToTop(
          WrapInWrapList(aBuilder, child, &list, wrapListASR));
    } else {
      aLists.Content()->AppendToTop(&list);
    }
  }
  // We delay placing the positioned descendants of positioned frames to here,
  // because in the absence of z-index this is the correct order for them.
  // This doesn't affect correctness because the positioned descendants list
  // is sorted by z-order and content in BuildDisplayListForStackingContext,
  // but it means that sort routine needs to do less work.
  aLists.PositionedDescendants()->AppendToTop(&extraPositionedDescendants);
}

void nsIFrame::MarkAbsoluteFramesForDisplayList(
    nsDisplayListBuilder* aBuilder) {
  if (IsAbsoluteContainer()) {
    aBuilder->MarkFramesForDisplayList(
        this, GetAbsoluteContainingBlock()->GetChildList());
  }
}

nsIContent* nsIFrame::GetContentForEvent(const WidgetEvent* aEvent) const {
  if (!IsGeneratedContentFrame()) {
    return GetContent();
  }
  const nsIFrame* generatedRoot = this;
  while (true) {
    auto* parent = nsLayoutUtils::GetParentOrPlaceholderFor(generatedRoot);
    if (!parent || !parent->IsGeneratedContentFrame()) {
      break;
    }
    generatedRoot = parent;
  }
  // Return the non-generated ancestor.
  return generatedRoot->GetContent()->GetParent();
}

void nsIFrame::FireDOMEvent(const nsAString& aDOMEventName,
                            nsIContent* aContent) {
  nsIContent* target = aContent ? aContent : GetContent();

  if (target) {
    RefPtr<AsyncEventDispatcher> asyncDispatcher = new AsyncEventDispatcher(
        target, aDOMEventName, CanBubble::eYes, ChromeOnlyDispatch::eNo);
    DebugOnly<nsresult> rv = asyncDispatcher->PostDOMEvent();
    NS_ASSERTION(NS_SUCCEEDED(rv), "AsyncEventDispatcher failed to dispatch");
  }
}

nsresult nsIFrame::HandleEvent(nsPresContext* aPresContext,
                               WidgetGUIEvent* aEvent,
                               nsEventStatus* aEventStatus) {
  if (aEvent->mMessage == eMouseMove) {
    // XXX If the second argument of HandleDrag() is WidgetMouseEvent,
    //     the implementation becomes simpler.
    return HandleDrag(aPresContext, aEvent, aEventStatus);
  }

  if ((aEvent->mClass == eMouseEventClass &&
       aEvent->AsMouseEvent()->mButton == MouseButton::ePrimary) ||
      aEvent->mClass == eTouchEventClass) {
    if (aEvent->mMessage == eMouseDown || aEvent->mMessage == eTouchStart) {
      HandlePress(aPresContext, aEvent, aEventStatus);
    } else if (aEvent->mMessage == eMouseUp || aEvent->mMessage == eTouchEnd) {
      HandleRelease(aPresContext, aEvent, aEventStatus);
    }
    return NS_OK;
  }

  // When secondary buttion is down, we need to move selection to make users
  // possible to paste something at click point quickly.
  // When middle button is down, we need to just move selection and focus at
  // the clicked point.  Note that even if middle click paste is not enabled,
  // Chrome moves selection at middle mouse button down.  So, we should follow
  // the behavior for the compatibility.
  if (aEvent->mMessage == eMouseDown) {
    WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
    if (mouseEvent && (mouseEvent->mButton == MouseButton::eSecondary ||
                       mouseEvent->mButton == MouseButton::eMiddle)) {
      if (*aEventStatus == nsEventStatus_eConsumeNoDefault) {
        return NS_OK;
      }
      return MoveCaretToEventPoint(aPresContext, mouseEvent, aEventStatus);
    }
  }

  return NS_OK;
}

nsresult nsIFrame::GetDataForTableSelection(
    const nsFrameSelection* aFrameSelection, mozilla::PresShell* aPresShell,
    WidgetMouseEvent* aMouseEvent, nsIContent** aParentContent,
    int32_t* aContentOffset, TableSelectionMode* aTarget) {
  if (!aFrameSelection || !aPresShell || !aMouseEvent || !aParentContent ||
      !aContentOffset || !aTarget) {
    return NS_ERROR_NULL_POINTER;
  }

  *aParentContent = nullptr;
  *aContentOffset = 0;
  *aTarget = TableSelectionMode::None;

  int16_t displaySelection = aPresShell->GetSelectionFlags();

  bool selectingTableCells = aFrameSelection->IsInTableSelectionMode();

  // DISPLAY_ALL means we're in an editor.
  // If already in cell selection mode,
  //  continue selecting with mouse drag or end on mouse up,
  //  or when using shift key to extend block of cells
  //  (Mouse down does normal selection unless Ctrl/Cmd is pressed)
  bool doTableSelection =
      displaySelection == nsISelectionDisplay::DISPLAY_ALL &&
      selectingTableCells &&
      (aMouseEvent->mMessage == eMouseMove ||
       (aMouseEvent->mMessage == eMouseUp &&
        aMouseEvent->mButton == MouseButton::ePrimary) ||
       aMouseEvent->IsShift());

  if (!doTableSelection) {
    // In Browser, special 'table selection' key must be pressed for table
    // selection or when just Shift is pressed and we're already in table/cell
    // selection mode
#ifdef XP_MACOSX
    doTableSelection = aMouseEvent->IsMeta() ||
                       (aMouseEvent->IsShift() && selectingTableCells);
#else
    doTableSelection = aMouseEvent->IsControl() ||
                       (aMouseEvent->IsShift() && selectingTableCells);
#endif
  }
  if (!doTableSelection) {
    return NS_OK;
  }

  // Get the cell frame or table frame (or parent) of the current content node
  nsIFrame* frame = this;
  bool foundCell = false;
  bool foundTable = false;

  // Get the limiting node to stop parent frame search
  const Element* const independentSelectionLimiter =
      aFrameSelection->GetIndependentSelectionRootElement();

  // If our content node is an ancestor of the limiting node,
  // we should stop the search right now.
  if (independentSelectionLimiter &&
      independentSelectionLimiter->IsInclusiveDescendantOf(GetContent())) {
    return NS_OK;
  }

  // We don't initiate row/col selection from here now,
  //  but we may in future
  // bool selectColumn = false;
  // bool selectRow = false;

  while (frame) {
    // Check for a table cell by querying to a known CellFrame interface
    nsITableCellLayout* cellElement = do_QueryFrame(frame);
    if (cellElement) {
      foundCell = true;
      // TODO: If we want to use proximity to top or left border
      //      for row and column selection, this is the place to do it
      break;
    } else {
      // If not a cell, check for table
      // This will happen when starting frame is the table or child of a table,
      //  such as a row (we were inbetween cells or in table border)
      nsTableWrapperFrame* tableFrame = do_QueryFrame(frame);
      if (tableFrame) {
        foundTable = true;
        // TODO: How can we select row when along left table edge
        //  or select column when along top edge?
        break;
      } else {
        frame = frame->GetParent();
        // Stop if we have hit the selection's limiting content node
        if (frame && frame->GetContent() == independentSelectionLimiter) {
          break;
        }
      }
    }
  }
  // We aren't in a cell or table
  if (!foundCell && !foundTable) {
    return NS_OK;
  }

  nsIContent* tableOrCellContent = frame->GetContent();
  if (!tableOrCellContent) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIContent> parentContent = tableOrCellContent->GetParent();
  if (!parentContent) {
    return NS_ERROR_FAILURE;
  }

  const int32_t offset =
      parentContent->ComputeIndexOf_Deprecated(tableOrCellContent);
  // Not likely?
  if (offset < 0) {
    return NS_ERROR_FAILURE;
  }

  // Everything is OK -- set the return values
  parentContent.forget(aParentContent);

  *aContentOffset = offset;

#if 0
  if (selectRow)
    *aTarget = TableSelectionMode::Row;
  else if (selectColumn)
    *aTarget = TableSelectionMode::Column;
  else
#endif
  if (foundCell) {
    *aTarget = TableSelectionMode::Cell;
  } else if (foundTable) {
    *aTarget = TableSelectionMode::Table;
  }

  return NS_OK;
}

static bool IsEditingHost(const nsIFrame* aFrame) {
  if (aFrame->Style()->GetPseudoType() ==
      PseudoStyleType::mozTextControlEditingRoot) {
    return true;
  }
  nsIContent* content = aFrame->GetContent();
  return content && content->IsEditingHost();
}

static StyleUserSelect UsedUserSelect(const nsIFrame* aFrame) {
  if (aFrame->IsGeneratedContentFrame()) {
    return StyleUserSelect::None;
  }

  // Per https://drafts.csswg.org/css-ui-4/#content-selection:
  //
  // The used value is the same as the computed value, except:
  //
  //    1 - on editable elements where the used value is always 'contain'
  //        regardless of the computed value
  //    2 - when the computed value is auto, in which case the used value is one
  //        of the other values...
  //
  // See https://github.com/w3c/csswg-drafts/issues/3344 to see why we do this
  // at used-value time instead of at computed-value time.

  if (aFrame->IsTextInputFrame() || IsEditingHost(aFrame)) {
    // We don't implement 'contain' itself, but we make 'text' behave as
    // 'contain' for contenteditable and <input> / <textarea> elements anyway so
    // this is ok.
    return StyleUserSelect::Text;
  }

  auto style = aFrame->Style()->UserSelect();
  if (style != StyleUserSelect::Auto) {
    return style;
  }

  auto* parent = nsLayoutUtils::GetParentOrPlaceholderFor(aFrame);
  return parent ? UsedUserSelect(parent) : StyleUserSelect::Text;
}

bool nsIFrame::IsSelectable(StyleUserSelect* aSelectStyle) const {
  auto style = UsedUserSelect(this);
  if (aSelectStyle) {
    *aSelectStyle = style;
  }
  return style != StyleUserSelect::None;
}

bool nsIFrame::ShouldHaveLineIfEmpty() const {
  switch (Style()->GetPseudoType()) {
    case PseudoStyleType::NotPseudo:
      break;
    case PseudoStyleType::scrolledContent:
      return GetParent()->ShouldHaveLineIfEmpty();
    case PseudoStyleType::buttonContent:
      // HTML quirk.
      return GetContent()->IsHTMLElement(nsGkAtoms::input);
    default:
      return false;
  }
  return IsEditingHost(this);
}

/**
 * Handles the Mouse Press Event for the frame
 */
NS_IMETHODIMP
nsIFrame::HandlePress(nsPresContext* aPresContext, WidgetGUIEvent* aEvent,
                      nsEventStatus* aEventStatus) {
  NS_ENSURE_ARG_POINTER(aEventStatus);
  if (nsEventStatus_eConsumeNoDefault == *aEventStatus) {
    return NS_OK;
  }

  NS_ENSURE_ARG_POINTER(aEvent);
  if (aEvent->mClass == eTouchEventClass) {
    return NS_OK;
  }

  return MoveCaretToEventPoint(aPresContext, aEvent->AsMouseEvent(),
                               aEventStatus);
}

nsresult nsIFrame::MoveCaretToEventPoint(nsPresContext* aPresContext,
                                         WidgetMouseEvent* aMouseEvent,
                                         nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aPresContext);
  MOZ_ASSERT(aMouseEvent);
  MOZ_ASSERT(aMouseEvent->mMessage == eMouseDown);
  MOZ_ASSERT(aEventStatus);
  MOZ_ASSERT(nsEventStatus_eConsumeNoDefault != *aEventStatus);

  mozilla::PresShell* presShell = aPresContext->GetPresShell();
  if (!presShell) {
    return NS_ERROR_FAILURE;
  }

  // We often get out of sync state issues with mousedown events that
  // get interrupted by alerts/dialogs.
  // Check with the ESM to see if we should process this one
  if (!aPresContext->EventStateManager()->EventStatusOK(aMouseEvent)) {
    return NS_OK;
  }

  EventStateManager* const esm = aPresContext->EventStateManager();
  if (nsIContent* dragGestureContent = esm->GetTrackingDragGestureContent()) {
    if (dragGestureContent != this->GetContent()) {
      // When the current tracked dragging gesture is different
      // than this frame, it means this frame was being dragged, however
      // it got moved/destroyed. So we should consider the drag is
      // still happening, so return early here.
      return NS_OK;
    }
  }

  const nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      aMouseEvent, RelativeTo{this});

  // When not using `alt`, and clicking on a draggable, but non-editable
  // element, don't do anything, and let d&d handle the event.
  //
  // See bug 48876, bug 388659 and bug 55921 for context here.
  //
  // FIXME(emilio): The .Contains(pt) check looks a bit fishy. When would it be
  // false given we're the event target? If it is needed, why not checking the
  // actual draggable node rect instead?
  if (!aMouseEvent->IsAlt() && GetRectRelativeToSelf().Contains(pt)) {
    for (nsIContent* content = mContent; content;
         content = content->GetFlattenedTreeParent()) {
      if (nsContentUtils::ContentIsDraggable(content) &&
          !content->IsEditable()) {
        return NS_OK;
      }
    }
  }

  // If we are in Navigator and the click is in a draggable node, we don't want
  // to start selection because we don't want to interfere with a potential
  // drag of said node and steal all its glory.
  const bool isEditor =
      presShell->GetSelectionFlags() == nsISelectionDisplay::DISPLAY_ALL;

  // Don't do something if it's middle button down event.
  const bool isPrimaryButtonDown =
      aMouseEvent->mButton == MouseButton::ePrimary;

  // check whether style allows selection
  // if not, don't tell selection the mouse event even occurred.
  StyleUserSelect selectStyle;
  // check for select: none
  if (!IsSelectable(&selectStyle)) {
    return NS_OK;
  }

  if (isPrimaryButtonDown) {
    // If the mouse is dragged outside the nearest enclosing scrollable area
    // while making a selection, the area will be scrolled. To do this, capture
    // the mouse on the nearest scroll container frame. If there isn't a scroll
    // container frame, or something else is already capturing the mouse,
    // there's no reason to capture.
    if (!PresShell::GetCapturingContent()) {
      ScrollContainerFrame* scrollContainerFrame =
          nsLayoutUtils::GetNearestScrollContainerFrame(
              this, nsLayoutUtils::SCROLLABLE_SAME_DOC |
                        nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);
      if (scrollContainerFrame) {
        nsIFrame* capturingFrame = scrollContainerFrame;
        PresShell::SetCapturingContent(capturingFrame->GetContent(),
                                       CaptureFlags::IgnoreAllowedState);
      }
    }
  }

  // XXX This is screwy; it really should use the selection frame, not the
  // event frame
  const nsFrameSelection* frameselection =
      selectStyle == StyleUserSelect::Text ? GetConstFrameSelection()
                                           : presShell->ConstFrameSelection();

  if (!frameselection || frameselection->GetDisplaySelection() ==
                             nsISelectionController::SELECTION_OFF) {
    return NS_OK;  // nothing to do we cannot affect selection from here
  }

#ifdef XP_MACOSX
  // If Control key is pressed on macOS, it should be treated as right click.
  // So, don't change selection.
  if (aMouseEvent->IsControl()) {
    return NS_OK;
  }
  const bool control = aMouseEvent->IsMeta();
#else
  const bool control = aMouseEvent->IsControl();
#endif

  RefPtr<nsFrameSelection> fc = const_cast<nsFrameSelection*>(frameselection);
  if (isPrimaryButtonDown && aMouseEvent->mClickCount > 1) {
    // These methods aren't const but can't actually delete anything,
    // so no need for AutoWeakFrame.
    fc->SetDragState(true);
    return HandleMultiplePress(aPresContext, aMouseEvent, aEventStatus,
                               control);
  }

  ContentOffsets offsets = GetContentOffsetsFromPoint(pt, SKIP_HIDDEN);

  if (!offsets.content) {
    return NS_ERROR_FAILURE;
  }

  const bool isSecondaryButton =
      aMouseEvent->mButton == MouseButton::eSecondary;
  if (isSecondaryButton &&
      !MovingCaretToEventPointAllowedIfSecondaryButtonEvent(
          *frameselection, *aMouseEvent, *offsets.content,
          // When we collapse selection in nsFrameSelection::TakeFocus,
          // we always collapse selection to the start offset.  Therefore,
          // we can ignore the end offset here.  E.g., when an <img> is clicked,
          // set the primary offset to after it, but the the secondary offset
          // may be before it, see OffsetsForSingleFrame for the detail.
          offsets.StartOffset())) {
    return NS_OK;
  }

  if (aMouseEvent->mMessage == eMouseDown &&
      aMouseEvent->mButton == MouseButton::eMiddle &&
      !offsets.content->IsEditable()) {
    // However, some users don't like the Chrome compatible behavior of
    // middle mouse click.  They want to keep selection after starting
    // autoscroll.  However, the selection change is important for middle
    // mouse past.  Therefore, we should allow users to take the traditional
    // behavior back by themselves unless middle click paste is enabled or
    // autoscrolling is disabled.
    if (!Preferences::GetBool("middlemouse.paste", false) &&
        Preferences::GetBool("general.autoScroll", false) &&
        Preferences::GetBool("general.autoscroll.prevent_to_collapse_selection_"
                             "by_middle_mouse_down",
                             false)) {
      return NS_OK;
    }
  }

  if (isPrimaryButtonDown) {
    // Let Ctrl/Cmd + left mouse down do table selection instead of drag
    // initiation.
    nsCOMPtr<nsIContent> parentContent;
    int32_t contentOffset;
    TableSelectionMode target;
    nsresult rv = GetDataForTableSelection(
        frameselection, presShell, aMouseEvent, getter_AddRefs(parentContent),
        &contentOffset, &target);
    if (NS_SUCCEEDED(rv) && parentContent) {
      fc->SetDragState(true);
      return fc->HandleTableSelection(parentContent, contentOffset, target,
                                      aMouseEvent);
    }
  }

  fc->SetDelayedCaretData(0);

  if (isPrimaryButtonDown) {
    // Check if any part of this frame is selected, and if the user clicked
    // inside the selected region, and if it's the left button. If so, we delay
    // starting a new selection since the user may be trying to drag the
    // selected region to some other app.

    if (GetContent() && GetContent()->IsMaybeSelected()) {
      bool inSelection = false;
      UniquePtr<SelectionDetails> details = frameselection->LookUpSelection(
          offsets.content, 0, offsets.EndOffset(), false);

      //
      // If there are any details, check to see if the user clicked
      // within any selected region of the frame.
      //

      for (SelectionDetails* curDetail = details.get(); curDetail;
           curDetail = curDetail->mNext.get()) {
        //
        // If the user clicked inside a selection, then just
        // return without doing anything. We will handle placing
        // the caret later on when the mouse is released. We ignore
        // the spellcheck, find and url formatting selections.
        //
        if (curDetail->mSelectionType != SelectionType::eSpellCheck &&
            curDetail->mSelectionType != SelectionType::eFind &&
            curDetail->mSelectionType != SelectionType::eURLSecondary &&
            curDetail->mSelectionType != SelectionType::eURLStrikeout &&
            curDetail->mSelectionType != SelectionType::eHighlight &&
            curDetail->mSelectionType != SelectionType::eTargetText &&
            curDetail->mStart <= offsets.StartOffset() &&
            offsets.EndOffset() <= curDetail->mEnd) {
          inSelection = true;
        }
      }

      if (inSelection) {
        fc->SetDragState(false);
        fc->SetDelayedCaretData(aMouseEvent);
        return NS_OK;
      }
    }

    fc->SetDragState(true);
  }

  // Do not touch any nsFrame members after this point without adding
  // weakFrame checks.
  const nsFrameSelection::FocusMode focusMode = [&]() {
    // If "Shift" and "Ctrl" are both pressed, "Shift" is given precedence. This
    // mimics the old behaviour.
    const bool isShift =
        aMouseEvent->IsShift() &&
        // If Shift + secondary button press shoud open context menu without a
        // contextmenu event, user wants to open context menu like as a
        // secondary button press without Shift key.
        !(isSecondaryButton &&
          StaticPrefs::dom_event_contextmenu_shift_suppresses_event());
    if (isShift) {
      // If clicked in a link when focused content is editable, we should
      // collapse selection in the link for compatibility with Blink.
      if (isEditor) {
        for (Element* element : mContent->InclusiveAncestorsOfType<Element>()) {
          if (element->IsLink()) {
            return nsFrameSelection::FocusMode::kCollapseToNewPoint;
          }
        }
      }
      return nsFrameSelection::FocusMode::kExtendSelection;
    }

    if (isPrimaryButtonDown && control) {
      return nsFrameSelection::FocusMode::kMultiRangeSelection;
    }

    return nsFrameSelection::FocusMode::kCollapseToNewPoint;
  }();

  nsresult rv = fc->HandleClick(
      MOZ_KnownLive(offsets.content) /* bug 1636889 */, offsets.StartOffset(),
      offsets.EndOffset(), focusMode, offsets.associate);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // We don't handle mouse button up if it's middle button.
  if (isPrimaryButtonDown && offsets.offset != offsets.secondaryOffset) {
    fc->MaintainSelection();
  }

  if (isPrimaryButtonDown && isEditor && !aMouseEvent->IsShift() &&
      (offsets.EndOffset() - offsets.StartOffset()) == 1) {
    // A single node is selected and we aren't extending an existing selection,
    // which means the user clicked directly on an object (either
    // `user-select: all` or a non-text node without children). Therefore,
    // disable selection extension during mouse moves.
    // XXX This is a bit hacky; shouldn't editor be able to deal with this?
    fc->SetDragState(false);
  }

  return NS_OK;
}

bool nsIFrame::MovingCaretToEventPointAllowedIfSecondaryButtonEvent(
    const nsFrameSelection& aFrameSelection,
    WidgetMouseEvent& aSecondaryButtonEvent,
    const nsIContent& aContentAtEventPoint, int32_t aOffsetAtEventPoint) const {
  MOZ_ASSERT(aSecondaryButtonEvent.mButton == MouseButton::eSecondary);

  if (NS_WARN_IF(aOffsetAtEventPoint < 0)) {
    return false;
  }

  const bool contentIsEditable = aContentAtEventPoint.IsEditable();
  const TextControlElement* const contentAsTextControl =
      TextControlElement::FromNodeOrNull(
          aContentAtEventPoint.IsTextControlElement()
              ? &aContentAtEventPoint
              : aContentAtEventPoint.GetClosestNativeAnonymousSubtreeRoot());
  const Selection& selection = aFrameSelection.NormalSelection();
  const bool selectionIsCollapsed =
      selection.AreNormalAndCrossShadowBoundaryRangesCollapsed();
  // If right click in a selection range, we should not collapse
  // selection.
  if (!selectionIsCollapsed && nsContentUtils::IsPointInSelection(
                                   selection, aContentAtEventPoint,
                                   static_cast<uint32_t>(aOffsetAtEventPoint),
                                   true /* aAllowCrossShadowBoundary */)) {
    return false;
  }
  const bool wantToPreventMoveCaret =
      StaticPrefs::
          ui_mouse_right_click_move_caret_stop_if_in_focused_editable_node() &&
      selectionIsCollapsed && (contentIsEditable || contentAsTextControl);
  const bool wantToPreventCollapseSelection =
      StaticPrefs::
          ui_mouse_right_click_collapse_selection_stop_if_non_collapsed_selection() &&
      !selectionIsCollapsed;
  if (wantToPreventMoveCaret || wantToPreventCollapseSelection) {
    // If currently selection is limited in an editing host, we should not
    // collapse selection nor move caret if the clicked point is in the
    // ancestor limiter.  Otherwise, this mouse click moves focus from the
    // editing host to different one or blur the editing host.  In this case,
    // we need to update selection because keeping current selection in the
    // editing host looks like it's not blurred.
    // FIXME: If the active editing host is the document element, editor
    // does not set ancestor limiter properly.  Fix it in the editor side.
    if (nsIContent* ancestorLimiter = selection.GetAncestorLimiter()) {
      MOZ_ASSERT(ancestorLimiter->IsEditable());
      return !aContentAtEventPoint.IsInclusiveDescendantOf(ancestorLimiter);
    }
  }
  // If selection is editable and `stop_if_in_focused_editable_node` pref is
  // set to true, user does not want to move caret to right click place if
  // clicked in the focused text control element.
  if (wantToPreventMoveCaret && contentAsTextControl &&
      contentAsTextControl == nsFocusManager::GetFocusedElementStatic()) {
    return false;
  }
  // If currently selection is not limited in an editing host, we should
  // collapse selection only when this click moves focus to an editing
  // host because we need to update selection in this case.
  if (wantToPreventCollapseSelection && !contentIsEditable) {
    return false;
  }

  return !StaticPrefs::
             ui_mouse_right_click_collapse_selection_stop_if_non_editable_node() ||
         // The user does not want to collapse selection into non-editable
         // content by a right button click.
         contentIsEditable ||
         // Treat clicking in a text control as always clicked on editable
         // content because we want a hack only for clicking in normal text
         // nodes which is outside any editing hosts.
         contentAsTextControl;
}

nsresult nsIFrame::SelectByTypeAtPoint(nsPresContext* aPresContext,
                                       const nsPoint& aPoint,
                                       nsSelectionAmount aBeginAmountType,
                                       nsSelectionAmount aEndAmountType,
                                       uint32_t aSelectFlags) {
  NS_ENSURE_ARG_POINTER(aPresContext);

  // No point in selecting if selection is turned off
  if (DetermineDisplaySelection() == nsISelectionController::SELECTION_OFF) {
    return NS_OK;
  }

  ContentOffsets offsets = GetContentOffsetsFromPoint(
      aPoint, SKIP_HIDDEN | IGNORE_NATIVE_ANONYMOUS_SUBTREE);
  if (!offsets.content) {
    return NS_ERROR_FAILURE;
  }

  uint32_t offset;
  nsIFrame* frame = SelectionMovementUtils::GetFrameForNodeOffset(
      offsets.content, offsets.offset, offsets.associate, &offset);
  if (!frame) {
    return NS_ERROR_FAILURE;
  }
  return frame->PeekBackwardAndForwardForSelection(
      aBeginAmountType, aEndAmountType, static_cast<int32_t>(offset),
      aBeginAmountType != eSelectWord, aSelectFlags);
}

/**
 * Multiple Mouse Press -- line or paragraph selection -- for the frame.
 * Wouldn't it be nice if this didn't have to be hardwired into Frame code?
 */
NS_IMETHODIMP
nsIFrame::HandleMultiplePress(nsPresContext* aPresContext,
                              WidgetGUIEvent* aEvent,
                              nsEventStatus* aEventStatus, bool aControlHeld) {
  NS_ENSURE_ARG_POINTER(aEvent);
  NS_ENSURE_ARG_POINTER(aEventStatus);

  if (nsEventStatus_eConsumeNoDefault == *aEventStatus ||
      DetermineDisplaySelection() == nsISelectionController::SELECTION_OFF) {
    return NS_OK;
  }

  // Find out whether we're doing line or paragraph selection.
  // If browser.triple_click_selects_paragraph is true, triple-click selects
  // paragraph. Otherwise, triple-click selects line, and quadruple-click
  // selects paragraph (on platforms that support quadruple-click).
  nsSelectionAmount beginAmount, endAmount;
  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  if (!mouseEvent) {
    return NS_OK;
  }

  if (mouseEvent->mClickCount == 4) {
    beginAmount = endAmount = eSelectParagraph;
  } else if (mouseEvent->mClickCount == 3) {
    if (Preferences::GetBool("browser.triple_click_selects_paragraph")) {
      beginAmount = endAmount = eSelectParagraph;
    } else {
      beginAmount = eSelectBeginLine;
      endAmount = eSelectEndLine;
    }
  } else if (mouseEvent->mClickCount == 2) {
    // We only want inline frames; PeekBackwardAndForward dislikes blocks
    beginAmount = endAmount = eSelectWord;
  } else {
    return NS_OK;
  }

  nsPoint relPoint = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      mouseEvent, RelativeTo{this});
  return SelectByTypeAtPoint(aPresContext, relPoint, beginAmount, endAmount,
                             (aControlHeld ? SELECT_ACCUMULATE : 0));
}

nsresult nsIFrame::PeekBackwardAndForwardForSelection(
    nsSelectionAmount aAmountBack, nsSelectionAmount aAmountForward,
    int32_t aStartPos, bool aJumpLines, uint32_t aSelectFlags) {
  nsIFrame* baseFrame = this;
  int32_t baseOffset = aStartPos;
  nsresult rv;

  PeekOffsetOptions peekOffsetOptions{PeekOffsetOption::StopAtScroller};
  if (aJumpLines) {
    peekOffsetOptions += PeekOffsetOption::JumpLines;
  }

  Element* const ancestorLimiter = [&]() -> Element* {
    const nsFrameSelection* const frameSelection = GetConstFrameSelection();
    return frameSelection
               ? frameSelection
                     ->GetAncestorLimiterOrIndependentSelectionRootElement()
               : nullptr;
  }();

  if (aAmountBack == eSelectWord) {
    // To avoid selecting the previous word when at start of word,
    // first move one character forward.
    PeekOffsetStruct pos(eSelectCharacter, eDirNext, aStartPos, nsPoint(0, 0),
                         peekOffsetOptions, eDefaultBehavior, ancestorLimiter);
    rv = PeekOffset(&pos);
    if (NS_SUCCEEDED(rv)) {
      baseFrame = pos.mResultFrame;
      baseOffset = pos.mContentOffset;
    }
  }

  // Search backward for a boundary.
  PeekOffsetStruct startpos(aAmountBack, eDirPrevious, baseOffset,
                            nsPoint(0, 0), peekOffsetOptions, eDefaultBehavior,
                            ancestorLimiter);
  rv = baseFrame->PeekOffset(&startpos);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // If the backward search stayed within the same frame, search forward from
  // that position for the end boundary; but if it crossed out to a sibling or
  // ancestor, start from the original position.
  if (startpos.mResultFrame == baseFrame) {
    baseOffset = startpos.mContentOffset;
  } else {
    baseFrame = this;
    baseOffset = aStartPos;
  }

  PeekOffsetStruct endpos(aAmountForward, eDirNext, baseOffset, nsPoint(0, 0),
                          peekOffsetOptions, eDefaultBehavior, ancestorLimiter);
  rv = baseFrame->PeekOffset(&endpos);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Keep frameSelection alive.
  RefPtr<nsFrameSelection> frameSelection = GetFrameSelection();

  const nsFrameSelection::FocusMode focusMode =
      (aSelectFlags & SELECT_ACCUMULATE)
          ? nsFrameSelection::FocusMode::kMultiRangeSelection
          : nsFrameSelection::FocusMode::kCollapseToNewPoint;
  rv = frameSelection->HandleClick(
      MOZ_KnownLive(startpos.mResultContent) /* bug 1636889 */,
      startpos.mContentOffset, startpos.mContentOffset, focusMode,
      CaretAssociationHint::After);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = frameSelection->HandleClick(
      MOZ_KnownLive(endpos.mResultContent) /* bug 1636889 */,
      endpos.mContentOffset, endpos.mContentOffset,
      nsFrameSelection::FocusMode::kExtendSelection,
      CaretAssociationHint::Before);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (aAmountBack == eSelectWord) {
    frameSelection->SetClickSelectionType(ClickSelectionType::Double);
  } else if (aAmountBack == eSelectParagraph) {
    frameSelection->SetClickSelectionType(ClickSelectionType::Triple);
  }

  // maintain selection
  return frameSelection->MaintainSelection(aAmountBack);
}

NS_IMETHODIMP nsIFrame::HandleDrag(nsPresContext* aPresContext,
                                   WidgetGUIEvent* aEvent,
                                   nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aEvent->mClass == eMouseEventClass,
             "HandleDrag can only handle mouse event");

  NS_ENSURE_ARG_POINTER(aEventStatus);

  RefPtr<nsFrameSelection> frameselection = GetFrameSelection();
  if (!frameselection) {
    return NS_OK;
  }

  bool mouseDown = frameselection->GetDragState();
  if (!mouseDown) {
    return NS_OK;
  }

  nsIFrame* scrollbar =
      nsLayoutUtils::GetClosestFrameOfType(this, LayoutFrameType::Scrollbar);
  if (!scrollbar) {
    // XXX Do we really need to exclude non-selectable content here?
    // GetContentOffsetsFromPoint can handle it just fine, although some
    // other stuff might not like it.
    // NOTE: DetermineDisplaySelection() returns SELECTION_OFF for
    // non-selectable frames.
    if (DetermineDisplaySelection() == nsISelectionController::SELECTION_OFF) {
      return NS_OK;
    }
  }

  frameselection->StopAutoScrollTimer();

  // Check if we are dragging in a table cell
  nsCOMPtr<nsIContent> parentContent;
  int32_t contentOffset;
  TableSelectionMode target;
  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  mozilla::PresShell* presShell = aPresContext->PresShell();
  nsresult result;
  result = GetDataForTableSelection(frameselection, presShell, mouseEvent,
                                    getter_AddRefs(parentContent),
                                    &contentOffset, &target);

  AutoWeakFrame weakThis = this;
  if (NS_SUCCEEDED(result) && parentContent) {
    result = frameselection->HandleTableSelection(parentContent, contentOffset,
                                                  target, mouseEvent);
    if (NS_WARN_IF(NS_FAILED(result))) {
      return result;
    }
  } else {
    nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(mouseEvent,
                                                              RelativeTo{this});
    frameselection->HandleDrag(this, pt);
  }

  // The frameselection object notifies selection listeners synchronously above
  // which might have killed us.
  if (!weakThis.IsAlive()) {
    return NS_OK;
  }

  // Get the nearest scroll container frame.
  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetNearestScrollContainerFrame(
          this, nsLayoutUtils::SCROLLABLE_SAME_DOC |
                    nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);

  if (scrollContainerFrame) {
    nsIFrame* capturingFrame = scrollContainerFrame->GetScrolledFrame();
    if (capturingFrame) {
      nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
          mouseEvent, RelativeTo{capturingFrame});
      frameselection->StartAutoScrollTimer(capturingFrame, pt, 30);
    }
  }

  return NS_OK;
}

/**
 * This static method handles part of the nsIFrame::HandleRelease in a way
 * which doesn't rely on the nsFrame object to stay alive.
 */
MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult HandleFrameSelection(
    nsFrameSelection* aFrameSelection, nsIFrame::ContentOffsets& aOffsets,
    bool aHandleTableSel, int32_t aContentOffsetForTableSel,
    TableSelectionMode aTargetForTableSel,
    nsIContent* aParentContentForTableSel, WidgetGUIEvent* aEvent,
    const nsEventStatus* aEventStatus) {
  if (!aFrameSelection) {
    return NS_OK;
  }

  nsresult rv = NS_OK;

  if (nsEventStatus_eConsumeNoDefault != *aEventStatus) {
    if (!aHandleTableSel) {
      if (!aOffsets.content || !aFrameSelection->HasDelayedCaretData()) {
        return NS_ERROR_FAILURE;
      }

      // We are doing this to simulate what we would have done on HandlePress.
      // We didn't do it there to give the user an opportunity to drag
      // the text, but since they didn't drag, we want to place the
      // caret.
      // However, we'll use the mouse position from the release, since:
      //  * it's easier
      //  * that's the normal click position to use (although really, in
      //    the normal case, small movements that don't count as a drag
      //    can do selection)
      aFrameSelection->SetDragState(true);

      const nsFrameSelection::FocusMode focusMode =
          aFrameSelection->IsShiftDownInDelayedCaretData()
              ? nsFrameSelection::FocusMode::kExtendSelection
              : nsFrameSelection::FocusMode::kCollapseToNewPoint;
      rv = aFrameSelection->HandleClick(
          MOZ_KnownLive(aOffsets.content) /* bug 1636889 */,
          aOffsets.StartOffset(), aOffsets.EndOffset(), focusMode,
          aOffsets.associate);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } else if (aParentContentForTableSel) {
      aFrameSelection->SetDragState(false);
      rv = aFrameSelection->HandleTableSelection(
          aParentContentForTableSel, aContentOffsetForTableSel,
          aTargetForTableSel, aEvent->AsMouseEvent());
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    aFrameSelection->SetDelayedCaretData(0);
  }

  aFrameSelection->SetDragState(false);
  aFrameSelection->StopAutoScrollTimer();

  return NS_OK;
}

NS_IMETHODIMP nsIFrame::HandleRelease(nsPresContext* aPresContext,
                                      WidgetGUIEvent* aEvent,
                                      nsEventStatus* aEventStatus) {
  if (aEvent->mClass != eMouseEventClass) {
    return NS_OK;
  }

  nsIFrame* activeFrame = GetActiveSelectionFrame(aPresContext, this);

  nsCOMPtr<nsIContent> captureContent = PresShell::GetCapturingContent();

  bool selectionOff =
      (DetermineDisplaySelection() == nsISelectionController::SELECTION_OFF);

  RefPtr<nsFrameSelection> frameselection;
  ContentOffsets offsets;
  nsCOMPtr<nsIContent> parentContent;
  int32_t contentOffsetForTableSel = 0;
  TableSelectionMode targetForTableSel = TableSelectionMode::None;
  bool handleTableSelection = true;

  if (!selectionOff) {
    frameselection = GetFrameSelection();
    if (nsEventStatus_eConsumeNoDefault != *aEventStatus && frameselection) {
      // Check if the frameselection recorded the mouse going down.
      // If not, the user must have clicked in a part of the selection.
      // Place the caret before continuing!

      if (frameselection->MouseDownRecorded()) {
        nsPoint pt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
            aEvent, RelativeTo{this});
        offsets = GetContentOffsetsFromPoint(pt, SKIP_HIDDEN);
        handleTableSelection = false;
      } else {
        GetDataForTableSelection(frameselection, PresShell(),
                                 aEvent->AsMouseEvent(),
                                 getter_AddRefs(parentContent),
                                 &contentOffsetForTableSel, &targetForTableSel);
      }
    }
  }

  // We might be capturing in some other document and the event just happened to
  // trickle down here. Make sure that document's frame selection is notified.
  // Note, this may cause the current nsFrame object to be deleted, bug 336592.
  RefPtr<nsFrameSelection> frameSelection;
  if (activeFrame != this && activeFrame->DetermineDisplaySelection() !=
                                 nsISelectionController::SELECTION_OFF) {
    frameSelection = activeFrame->GetFrameSelection();
  }

  // Also check the selection of the capturing content which might be in a
  // different document.
  if (!frameSelection && captureContent) {
    if (Document* doc = captureContent->GetComposedDoc()) {
      mozilla::PresShell* capturingPresShell = doc->GetPresShell();
      if (capturingPresShell &&
          capturingPresShell != PresContext()->GetPresShell()) {
        frameSelection = capturingPresShell->FrameSelection();
      }
    }
  }

  if (frameSelection) {
    AutoWeakFrame wf(this);
    frameSelection->SetDragState(false);
    frameSelection->StopAutoScrollTimer();
    if (wf.IsAlive()) {
      ScrollContainerFrame* scrollContainerFrame =
          nsLayoutUtils::GetNearestScrollContainerFrame(
              this, nsLayoutUtils::SCROLLABLE_SAME_DOC |
                        nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);
      if (scrollContainerFrame) {
        // Perform any additional scrolling needed to maintain CSS snap point
        // requirements when autoscrolling is over.
        scrollContainerFrame->ScrollSnap();
      }
    }
  }

  // Do not call any methods of the current object after this point!!!
  // The object is perhaps dead!

  return selectionOff ? NS_OK
                      : HandleFrameSelection(
                            frameselection, offsets, handleTableSelection,
                            contentOffsetForTableSel, targetForTableSel,
                            parentContent, aEvent, aEventStatus);
}

struct MOZ_STACK_CLASS FrameContentRange {
  FrameContentRange(nsIContent* aContent, int32_t aStart, int32_t aEnd)
      : content(aContent), start(aStart), end(aEnd) {}
  nsCOMPtr<nsIContent> content;
  int32_t start;
  int32_t end;
};

static bool IsRelevantBlockFrame(const nsIFrame* aFrame) {
  if (!aFrame->IsBlockOutside()) {
    return false;
  }
  if (aFrame->GetContent() &&
      aFrame->GetContent()->IsInNativeAnonymousSubtree()) {
    // This helps skipping things like scrollbar parts.
    return false;
  }
  auto pseudoType = aFrame->Style()->GetPseudoType();
  if (PseudoStyle::IsAnonBox(pseudoType)) {
    // Table cell contents should be considered block boundaries for this
    // purpose.
    return pseudoType == PseudoStyleType::cellContent;
  }
  return true;
}

// Retrieve the content offsets of a frame
static FrameContentRange GetRangeForFrame(const nsIFrame* aFrame) {
  nsIContent* content = aFrame->GetContent();
  if (!content) {
    NS_WARNING("Frame has no content");
    return FrameContentRange(nullptr, -1, -1);
  }

  LayoutFrameType type = aFrame->Type();
  if (type == LayoutFrameType::Text) {
    auto [offset, offsetEnd] = aFrame->GetOffsets();
    return FrameContentRange(content, offset, offsetEnd);
  }

  if (type == LayoutFrameType::Br) {
    nsIContent* parent = content->GetParent();
    const int32_t beginOffset = parent->ComputeIndexOf_Deprecated(content);
    return FrameContentRange(parent, beginOffset, beginOffset);
  }

  while (content->IsRootOfNativeAnonymousSubtree()) {
    content = content->GetParent();
  }

  if (aFrame->IsReplaced()) {
    if (auto* parent = content->GetParent()) {
      // TODO(emilio): Revise this in presence of Shadow DOM / display:
      // contents, it's likely that we don't want to just walk the light tree,
      // and we need to change the representation of FrameContentRange.
      Maybe<uint32_t> index = parent->ComputeIndexOf(content);
      MOZ_ASSERT(index.isSome());
      return FrameContentRange(parent, static_cast<int32_t>(*index),
                               static_cast<int32_t>(*index + 1));
    }
  }

  return FrameContentRange(content, 0, content->GetChildCount());
}

// The FrameTarget represents the closest frame to a point that can be selected
// The frame is the frame represented, frameEdge says whether one end of the
// frame is the result (in which case different handling is needed), and
// afterFrame says which end is represented if frameEdge is true
struct FrameTarget {
  explicit operator bool() const { return !!frame; }

  nsIFrame* frame = nullptr;
  bool frameEdge = false;
  bool afterFrame = false;
};

// See function implementation for information
static FrameTarget GetSelectionClosestFrame(nsIFrame* aFrame,
                                            const nsPoint& aPoint,
                                            uint32_t aFlags);

static bool SelfIsSelectable(nsIFrame* aFrame, nsIFrame* aParentFrame,
                             uint32_t aFlags) {
  // We should not move selection into a native anonymous subtree when handling
  // selection outside it.
  if ((aFlags & nsIFrame::IGNORE_NATIVE_ANONYMOUS_SUBTREE) &&
      aParentFrame->GetClosestNativeAnonymousSubtreeRoot() !=
          aFrame->GetClosestNativeAnonymousSubtreeRoot()) {
    return false;
  }
  if ((aFlags & nsIFrame::SKIP_HIDDEN) &&
      !aFrame->StyleVisibility()->IsVisible()) {
    return false;
  }
  if (aFrame->IsGeneratedContentFrame()) {
    return false;
  }
  if (aFrame->Style()->UserSelect() == StyleUserSelect::None) {
    return false;
  }
  if (aFrame->IsEmpty() &&
      (!aFrame->IsTextFrame() || !aFrame->ContentIsEditable())) {
    // FIXME(emilio): Historically we haven't treated empty frames as
    // selectable, but also we had special-cases so that editable empty text
    // frames returned false from IsEmpty(). Sort this out (probably by
    // removing the IsEmpty() condition altogether).
    return false;
  }
  return true;
}

static bool FrameContentCanHaveParentSelectionRange(nsIFrame* aFrame) {
  // If we are only near (not directly over) then don't traverse
  // frames with independent selection (e.g. text and list controls, see bug
  // 268497).  Note that this prevents any of the users of this method from
  // entering form controls.
  // XXX We might want some way to allow using the up-arrow to go into a form
  // control, but the focus didn't work right anyway; it'd probably be enough
  // if the left and right arrows could enter textboxes (which I don't believe
  // they can at the moment)
  if (aFrame->IsTextInputFrame() || aFrame->IsListControlFrame()) {
    MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_INDEPENDENT_SELECTION));
    return false;
  }

  // Failure in this assertion means a new type of frame forms the root of an
  // NS_FRAME_INDEPENDENT_SELECTION subtree. In such case, the condition above
  // should be changed to handle it.
  MOZ_ASSERT_IF(
      aFrame->HasAnyStateBits(NS_FRAME_INDEPENDENT_SELECTION),
      aFrame->GetParent()->HasAnyStateBits(NS_FRAME_INDEPENDENT_SELECTION));

  return !aFrame->IsGeneratedContentFrame();
}

static bool SelectionDescendToKids(nsIFrame* aFrame) {
  if (!FrameContentCanHaveParentSelectionRange(aFrame)) {
    return false;
  }
  auto style = aFrame->Style()->UserSelect();
  return style != StyleUserSelect::All && style != StyleUserSelect::None;
}

static FrameTarget GetSelectionClosestFrameForChild(nsIFrame* aChild,
                                                    const nsPoint& aPoint,
                                                    uint32_t aFlags) {
  nsIFrame* parent = aChild->GetParent();
  if (SelectionDescendToKids(aChild)) {
    nsPoint pt = aPoint - aChild->GetOffsetTo(parent);
    return GetSelectionClosestFrame(aChild, pt, aFlags);
  }
  return FrameTarget{aChild, false, false};
}

// When the cursor needs to be at the beginning of a block, it shouldn't be
// before the first child.  A click on a block whose first child is a block
// should put the cursor in the child.  The cursor shouldn't be between the
// blocks, because that's not where it's expected.
// Note that this method is guaranteed to succeed.
static FrameTarget DrillDownToSelectionFrame(nsIFrame* aFrame, bool aEndFrame,
                                             uint32_t aFlags) {
  if (SelectionDescendToKids(aFrame)) {
    nsIFrame* result = nullptr;
    nsIFrame* frame = aFrame->PrincipalChildList().FirstChild();
    if (!aEndFrame) {
      while (frame && !SelfIsSelectable(frame, aFrame, aFlags)) {
        frame = frame->GetNextSibling();
      }
      if (frame) {
        result = frame;
      }
    } else {
      // Because the frame tree is singly linked, to find the last frame,
      // we have to iterate through all the frames
      // XXX I have a feeling this could be slow for long blocks, although
      //     I can't find any slowdowns
      while (frame) {
        if (SelfIsSelectable(frame, aFrame, aFlags)) {
          result = frame;
        }
        frame = frame->GetNextSibling();
      }
    }
    if (result) {
      return DrillDownToSelectionFrame(result, aEndFrame, aFlags);
    }
  }
  // If the current frame has no targetable children, target the current frame
  return FrameTarget{aFrame, true, aEndFrame};
}

// This method finds the closest valid FrameTarget on a given line; if there is
// no valid FrameTarget on the line, it returns a null FrameTarget
static FrameTarget GetSelectionClosestFrameForLine(
    nsBlockFrame* aParent, nsBlockFrame::LineIterator aLine,
    const nsPoint& aPoint, uint32_t aFlags) {
  // Account for end of lines (any iterator from the block is valid)
  if (aLine == aParent->LinesEnd()) {
    return DrillDownToSelectionFrame(aParent, true, aFlags);
  }
  nsIFrame* frame = aLine->mFirstChild;
  nsIFrame* closestFromIStart = nullptr;
  nsIFrame* closestFromIEnd = nullptr;
  nscoord closestIStart = aLine->IStart(), closestIEnd = aLine->IEnd();
  WritingMode wm = aLine->mWritingMode;
  LogicalPoint pt(wm, aPoint, aLine->mContainerSize);
  bool canSkipBr = false;
  bool lastFrameWasEditable = false;
  for (int32_t n = aLine->GetChildCount(); n;
       --n, frame = frame->GetNextSibling()) {
    // Skip brFrames. Can only skip if the line contains at least
    // one selectable and non-empty frame before. Also, avoid skipping brs if
    // the previous thing had a different editableness than us, since then we
    // may end up not being able to select after it if the br is the last thing
    // on the line.
    if (!SelfIsSelectable(frame, aParent, aFlags) ||
        (canSkipBr && frame->IsBrFrame() &&
         lastFrameWasEditable == frame->GetContent()->IsEditable())) {
      continue;
    }
    canSkipBr = true;
    lastFrameWasEditable =
        frame->GetContent() && frame->GetContent()->IsEditable();
    LogicalRect frameRect =
        LogicalRect(wm, frame->GetRect(), aLine->mContainerSize);
    if (pt.I(wm) >= frameRect.IStart(wm)) {
      if (pt.I(wm) < frameRect.IEnd(wm)) {
        return GetSelectionClosestFrameForChild(frame, aPoint, aFlags);
      }
      if (frameRect.IEnd(wm) >= closestIStart) {
        closestFromIStart = frame;
        closestIStart = frameRect.IEnd(wm);
      }
    } else {
      if (frameRect.IStart(wm) <= closestIEnd) {
        closestFromIEnd = frame;
        closestIEnd = frameRect.IStart(wm);
      }
    }
  }
  if (!closestFromIStart && !closestFromIEnd) {
    // We should only get here if there are no selectable frames on a line
    // XXX Do we need more elaborate handling here?
    return FrameTarget();
  }
  if (closestFromIStart &&
      (!closestFromIEnd ||
       (abs(pt.I(wm) - closestIStart) <= abs(pt.I(wm) - closestIEnd)))) {
    return GetSelectionClosestFrameForChild(closestFromIStart, aPoint, aFlags);
  }
  return GetSelectionClosestFrameForChild(closestFromIEnd, aPoint, aFlags);
}

// This method is for the special handling we do for block frames; they're
// special because they represent paragraphs and because they are organized
// into lines, which have bounds that are not stored elsewhere in the
// frame tree.  Returns a null FrameTarget for frames which are not
// blocks or blocks with no lines except editable one.
static FrameTarget GetSelectionClosestFrameForBlock(nsIFrame* aFrame,
                                                    const nsPoint& aPoint,
                                                    uint32_t aFlags) {
  nsBlockFrame* bf = do_QueryFrame(aFrame);
  if (!bf) {
    return FrameTarget();
  }

  // This code searches for the correct line
  nsBlockFrame::LineIterator end = bf->LinesEnd();
  nsBlockFrame::LineIterator curLine = bf->LinesBegin();
  nsBlockFrame::LineIterator closestLine = end;

  if (curLine != end) {
    // Convert aPoint into a LogicalPoint in the writing-mode of this block
    WritingMode wm = curLine->mWritingMode;
    LogicalPoint pt(wm, aPoint, curLine->mContainerSize);
    do {
      // Check to see if our point lies within the line's block-direction bounds
      nscoord BCoord = pt.B(wm) - curLine->BStart();
      nscoord BSize = curLine->BSize();
      if (BCoord >= 0 && BCoord < BSize) {
        closestLine = curLine;
        break;  // We found the line; stop looking
      }
      if (BCoord < 0) {
        break;
      }
      ++curLine;
    } while (curLine != end);

    if (closestLine == end) {
      nsBlockFrame::LineIterator prevLine = curLine.prev();
      nsBlockFrame::LineIterator nextLine = curLine;
      // Avoid empty lines
      while (nextLine != end && nextLine->IsEmpty()) {
        ++nextLine;
      }
      while (prevLine != end && prevLine->IsEmpty()) {
        --prevLine;
      }

      // This hidden pref dictates whether a point above or below all lines
      // comes up with a line or the beginning or end of the frame; 0 on
      // Windows, 1 on other platforms by default at the writing of this code
      int32_t dragOutOfFrame =
          Preferences::GetInt("browser.drag_out_of_frame_style");

      if (prevLine == end) {
        if (dragOutOfFrame == 1 || nextLine == end) {
          return DrillDownToSelectionFrame(aFrame, false, aFlags);
        }
        closestLine = nextLine;
      } else if (nextLine == end) {
        if (dragOutOfFrame == 1) {
          return DrillDownToSelectionFrame(aFrame, true, aFlags);
        }
        closestLine = prevLine;
      } else {  // Figure out which line is closer
        if (pt.B(wm) - prevLine->BEnd() < nextLine->BStart() - pt.B(wm)) {
          closestLine = prevLine;
        } else {
          closestLine = nextLine;
        }
      }
    }
  }

  do {
    if (auto target =
            GetSelectionClosestFrameForLine(bf, closestLine, aPoint, aFlags)) {
      return target;
    }
    ++closestLine;
  } while (closestLine != end);

  // Fall back to just targeting the last targetable place
  return DrillDownToSelectionFrame(aFrame, true, aFlags);
}

// Use frame edge for grid, flex, table, and non-editable images. Choose the
// edge based on the point position past the frame rect. If past the middle,
// caret should be at end, otherwise at start. This behavior matches Blink.
//
// TODO(emilio): Can we use this code path for other replaced elements other
// than images? Or even all other frames? We only get there when we didn't find
// selectable children... At least one XUL test fails if we make this apply to
// XUL labels. Also, editable images need _not_ to use the frame edge, see
// below.
static bool UseFrameEdge(nsIFrame* aFrame) {
  if (aFrame->IsFlexOrGridContainer() || aFrame->IsTableFrame()) {
    return true;
  }
  const nsImageFrame* image = do_QueryFrame(aFrame);
  if (image && !aFrame->GetContent()->IsEditable()) {
    // Editable images are a special-case because editing relies on clicking on
    // an editable image selecting it, for it to show resizers.
    return true;
  }
  return false;
}

static FrameTarget LastResortFrameTargetForFrame(nsIFrame* aFrame,
                                                 const nsPoint& aPoint) {
  if (!UseFrameEdge(aFrame)) {
    return {aFrame, false, false};
  }
  const auto& rect = aFrame->GetRectRelativeToSelf();
  nscoord reference;
  nscoord middle;
  if (aFrame->GetWritingMode().IsVertical()) {
    reference = aPoint.y;
    middle = rect.Height() / 2;
  } else {
    reference = aPoint.x;
    middle = rect.Width() / 2;
  }
  const bool afterFrame = reference > middle;
  return {aFrame, true, afterFrame};
}

// GetSelectionClosestFrame is the helper function that calculates the closest
// frame to the given point.
// It doesn't completely account for offset styles, so needs to be used in
// restricted environments.
// Cannot handle overlapping frames correctly, so it should receive the output
// of GetFrameForPoint
// Guaranteed to return a valid FrameTarget.
// aPoint is relative to aFrame.
static FrameTarget GetSelectionClosestFrame(nsIFrame* aFrame,
                                            const nsPoint& aPoint,
                                            uint32_t aFlags) {
  // Handle blocks; if the frame isn't a block, the method fails
  if (auto target = GetSelectionClosestFrameForBlock(aFrame, aPoint, aFlags)) {
    return target;
  }

  if (aFlags & nsIFrame::IGNORE_NATIVE_ANONYMOUS_SUBTREE &&
      !FrameContentCanHaveParentSelectionRange(aFrame)) {
    return LastResortFrameTargetForFrame(aFrame, aPoint);
  }

  if (nsIFrame* kid = aFrame->PrincipalChildList().FirstChild()) {
    // Go through all the child frames to find the closest one
    nsIFrame::FrameWithDistance closest = {nullptr, nscoord_MAX, nscoord_MAX};
    for (; kid; kid = kid->GetNextSibling()) {
      if (!SelfIsSelectable(kid, aFrame, aFlags)) {
        continue;
      }

      kid->FindCloserFrameForSelection(aPoint, &closest);
    }
    if (closest.mFrame) {
      if (closest.mFrame->IsInSVGTextSubtree()) {
        return FrameTarget{closest.mFrame, false, false};
      }
      return GetSelectionClosestFrameForChild(closest.mFrame, aPoint, aFlags);
    }
  }

  return LastResortFrameTargetForFrame(aFrame, aPoint);
}

static nsIFrame::ContentOffsets OffsetsForSingleFrame(nsIFrame* aFrame,
                                                      const nsPoint& aPoint) {
  nsIFrame::ContentOffsets offsets;
  FrameContentRange range = GetRangeForFrame(aFrame);
  offsets.content = range.content;
  // If there are continuations (meaning it's not one rectangle), this is the
  // best this function can do
  if (aFrame->GetNextContinuation() || aFrame->GetPrevContinuation()) {
    offsets.offset = range.start;
    offsets.secondaryOffset = range.end;
    offsets.associate = CaretAssociationHint::After;
    return offsets;
  }

  // Figure out whether the offsets should be over, after, or before the frame
  nsRect rect(nsPoint(0, 0), aFrame->GetSize());

  bool isBlock = !aFrame->StyleDisplay()->IsInlineFlow();
  bool isRtl = (aFrame->StyleVisibility()->mDirection == StyleDirection::Rtl);
  if ((isBlock && rect.y < aPoint.y) ||
      (!isBlock && ((isRtl && rect.x + rect.width / 2 > aPoint.x) ||
                    (!isRtl && rect.x + rect.width / 2 < aPoint.x)))) {
    offsets.offset = range.end;
    if (rect.Contains(aPoint)) {
      offsets.secondaryOffset = range.start;
    } else {
      offsets.secondaryOffset = range.end;
    }
  } else {
    offsets.offset = range.start;
    if (rect.Contains(aPoint)) {
      offsets.secondaryOffset = range.end;
    } else {
      offsets.secondaryOffset = range.start;
    }
  }
  offsets.associate = offsets.offset == range.start
                          ? CaretAssociationHint::After
                          : CaretAssociationHint::Before;
  return offsets;
}

static nsIFrame* AdjustFrameForSelectionStyles(nsIFrame* aFrame) {
  nsIFrame* adjustedFrame = aFrame;
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    // These are the conditions that make all children not able to handle
    // a cursor.
    auto userSelect = frame->Style()->UserSelect();
    if (userSelect != StyleUserSelect::Auto &&
        userSelect != StyleUserSelect::All) {
      break;
    }
    if (userSelect == StyleUserSelect::All ||
        frame->IsGeneratedContentFrame()) {
      adjustedFrame = frame;
    }
  }
  return adjustedFrame;
}

nsIFrame::ContentOffsets nsIFrame::GetContentOffsetsFromPoint(
    const nsPoint& aPoint, uint32_t aFlags) {
  nsIFrame* adjustedFrame;
  if (aFlags & IGNORE_SELECTION_STYLE) {
    adjustedFrame = this;
  } else {
    // This section of code deals with special selection styles.  Note that
    // -moz-all exists, even though it doesn't need to be explicitly handled.
    //
    // The offset is forced not to end up in generated content; content offsets
    // cannot represent content outside of the document's content tree.

    adjustedFrame = AdjustFrameForSelectionStyles(this);

    // `user-select: all` needs special handling, because clicking on it should
    // lead to the whole frame being selected.
    if (adjustedFrame->Style()->UserSelect() == StyleUserSelect::All) {
      nsPoint adjustedPoint = aPoint + GetOffsetTo(adjustedFrame);
      return OffsetsForSingleFrame(adjustedFrame, adjustedPoint);
    }

    // For other cases, try to find a closest frame starting from the parent of
    // the unselectable frame
    if (adjustedFrame != this) {
      adjustedFrame = adjustedFrame->GetParent();
    }
  }

  nsPoint adjustedPoint = aPoint + GetOffsetTo(adjustedFrame);

  FrameTarget closest =
      GetSelectionClosestFrame(adjustedFrame, adjustedPoint, aFlags);

  // If the correct offset is at one end of a frame, use offset-based
  // calculation method
  if (closest.frameEdge) {
    ContentOffsets offsets;
    FrameContentRange range = GetRangeForFrame(closest.frame);
    offsets.content = range.content;
    if (closest.afterFrame) {
      offsets.offset = range.end;
    } else {
      offsets.offset = range.start;
    }
    offsets.secondaryOffset = offsets.offset;
    offsets.associate = offsets.offset == range.start
                            ? CaretAssociationHint::After
                            : CaretAssociationHint::Before;
    return offsets;
  }

  nsPoint pt;
  if (closest.frame != this) {
    if (closest.frame->IsInSVGTextSubtree()) {
      pt = nsLayoutUtils::TransformAncestorPointToFrame(
          RelativeTo{closest.frame}, aPoint, RelativeTo{this});
    } else {
      pt = aPoint - closest.frame->GetOffsetTo(this);
    }
  } else {
    pt = aPoint;
  }
  return closest.frame->CalcContentOffsetsFromFramePoint(pt);

  // XXX should I add some kind of offset standardization?
  // consider <b>xxxxx</b><i>zzzzz</i>; should any click between the last
  // x and first z put the cursor in the same logical position in addition
  // to the same visual position?
}

nsIFrame::ContentOffsets nsIFrame::CalcContentOffsetsFromFramePoint(
    const nsPoint& aPoint) {
  return OffsetsForSingleFrame(this, aPoint);
}

bool nsIFrame::AssociateImage(const StyleImage& aImage) {
  imgRequestProxy* req = aImage.GetImageRequest();
  if (!req) {
    return false;
  }

  mozilla::css::ImageLoader* loader =
      PresContext()->Document()->StyleImageLoader();

  loader->AssociateRequestToFrame(req, this);
  return true;
}

void nsIFrame::DisassociateImage(const StyleImage& aImage) {
  imgRequestProxy* req = aImage.GetImageRequest();
  if (!req) {
    return;
  }

  mozilla::css::ImageLoader* loader =
      PresContext()->Document()->StyleImageLoader();

  loader->DisassociateRequestFromFrame(req, this);
}

StyleImageRendering nsIFrame::UsedImageRendering() const {
  ComputedStyle* style;
  if (IsCanvasFrame()) {
    // XXXdholbert Maybe we should use FindCanvasBackground here (instead of
    // FindBackground), since we're inside an IsCanvasFrame check? Though then
    // we'd also have to copypaste or abstract-away the multi-part root-frame
    // lookup that the canvas-flavored API requires.
    style = nsCSSRendering::FindBackground(this);
  } else {
    style = Style();
  }
  return style->StyleVisibility()->mImageRendering;
}

// The touch-action CSS property applies to: all elements except: non-replaced
// inline elements, table rows, row groups, table columns, and column groups.
StyleTouchAction nsIFrame::UsedTouchAction() const {
  if (IsLineParticipant()) {
    return StyleTouchAction::AUTO;
  }
  auto& disp = *StyleDisplay();
  if (disp.IsInternalTableStyleExceptCell()) {
    return StyleTouchAction::AUTO;
  }
  return disp.mTouchAction;
}

nsIFrame::Cursor nsIFrame::GetCursor(const nsPoint&) {
  StyleCursorKind kind = StyleUI()->Cursor().keyword;
  if (kind == StyleCursorKind::Auto) {
    // If this is editable, I-beam cursor is better for most elements.
    kind = (mContent && mContent->IsEditable()) ? StyleCursorKind::Text
                                                : StyleCursorKind::Default;
  }
  if (kind == StyleCursorKind::Text && GetWritingMode().IsVertical()) {
    // Per CSS UI spec, UA may treat value 'text' as
    // 'vertical-text' for vertical text.
    kind = StyleCursorKind::VerticalText;
  }

  return Cursor{kind, AllowCustomCursorImage::Yes};
}

// Resize and incremental reflow

/* virtual */
void nsIFrame::MarkIntrinsicISizesDirty() {
  // If we're a flex item, clear our flex-item-specific cached measurements
  // (which likely depended on our now-stale intrinsic isize).
  if (IsFlexItem()) {
    nsFlexContainerFrame::MarkCachedFlexMeasurementsDirty(this);
  }

  if (IsGridItem()) {
    nsGridContainerFrame::MarkCachedGridMeasurementsDirty(this);
  }

  if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT)) {
    nsFontInflationData::MarkFontInflationDataTextDirty(this);
  }
}

void nsIFrame::MarkSubtreeDirty() {
  if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
    return;
  }
  // Unconditionally mark given frame dirty.
  AddStateBits(NS_FRAME_IS_DIRTY);

  // Mark all descendants dirty, unless:
  // - Already dirty.
  // - TableColGroup
  AutoTArray<nsIFrame*, 32> stack;
  for (const auto& childLists : ChildLists()) {
    for (nsIFrame* kid : childLists.mList) {
      stack.AppendElement(kid);
    }
  }
  while (!stack.IsEmpty()) {
    nsIFrame* f = stack.PopLastElement();
    if (f->HasAnyStateBits(NS_FRAME_IS_DIRTY) || f->IsTableColGroupFrame()) {
      continue;
    }

    f->AddStateBits(NS_FRAME_IS_DIRTY);

    for (const auto& childLists : f->ChildLists()) {
      for (nsIFrame* kid : childLists.mList) {
        stack.AppendElement(kid);
      }
    }
  }
}

/* virtual */
void nsIFrame::AddInlineMinISize(const IntrinsicSizeInput& aInput,
                                 InlineMinISizeData* aData) {
  // Note: we are one of the children that mPercentageBasisForChildren was
  // prepared for (i.e. our parent frame prepares the percentage basis for us,
  // not for our own children). Hence it's fine that we're resolving our
  // percentages sizes against this basis in IntrinsicForContainer().
  nscoord isize = nsLayoutUtils::IntrinsicForContainer(
      aInput.mContext, this, IntrinsicISizeType::MinISize,
      aInput.mPercentageBasisForChildren);
  aData->DefaultAddInlineMinISize(this, isize);
}

/* virtual */
void nsIFrame::AddInlinePrefISize(const IntrinsicSizeInput& aInput,
                                  nsIFrame::InlinePrefISizeData* aData) {
  // Note: we are one of the children that mPercentageBasisForChildren was
  // prepared for (i.e. our parent frame prepares the percentage basis for us,
  // not for our own children). Hence it's fine that we're resolving our
  // percentages sizes against this basis in IntrinsicForContainer().
  nscoord isize = nsLayoutUtils::IntrinsicForContainer(
      aInput.mContext, this, IntrinsicISizeType::PrefISize,
      aInput.mPercentageBasisForChildren);
  aData->DefaultAddInlinePrefISize(isize);
}

void nsIFrame::InlineMinISizeData::DefaultAddInlineMinISize(nsIFrame* aFrame,
                                                            nscoord aISize,
                                                            bool aAllowBreak) {
  auto parent = aFrame->GetParent();
  MOZ_ASSERT(parent, "Must have a parent if we get here!");
  const bool mayBreak = aAllowBreak && !aFrame->CanContinueTextRun() &&
                        !parent->Style()->ShouldSuppressLineBreak() &&
                        parent->StyleText()->WhiteSpaceCanWrap(parent);
  if (mayBreak) {
    OptionallyBreak();
  }
  mTrailingWhitespace = 0;
  mSkipWhitespace = false;
  mCurrentLine += aISize;
  mAtStartOfLine = false;
  if (mayBreak) {
    OptionallyBreak();
  }
}

void nsIFrame::InlinePrefISizeData::DefaultAddInlinePrefISize(nscoord aISize) {
  mCurrentLine = NSCoordSaturatingAdd(mCurrentLine, aISize);
  mTrailingWhitespace = 0;
  mSkipWhitespace = false;
  mLineIsEmpty = false;
}

void nsIFrame::InlineMinISizeData::ForceBreak() {
  mCurrentLine -= mTrailingWhitespace;
  mPrevLines = std::max(mPrevLines, mCurrentLine);
  mCurrentLine = mTrailingWhitespace = 0;

  for (const FloatInfo& floatInfo : mFloats) {
    mPrevLines = std::max(floatInfo.ISize(), mPrevLines);
  }
  mFloats.Clear();
  mSkipWhitespace = true;
}

void nsIFrame::InlineMinISizeData::OptionallyBreak(nscoord aHyphenWidth) {
  // If we can fit more content into a smaller width by staying on this
  // line (because we're still at a negative offset due to negative
  // text-indent or negative margin), don't break.  Otherwise, do the
  // same as ForceBreak.  it doesn't really matter when we accumulate
  // floats.
  if (mCurrentLine + aHyphenWidth < 0 || mAtStartOfLine) {
    return;
  }
  mCurrentLine += aHyphenWidth;
  ForceBreak();
}

void nsIFrame::InlinePrefISizeData::ForceBreak(UsedClear aClearType) {
  // If this force break is not clearing any float, we can leave all the
  // floats to the next force break.
  if (!mFloats.IsEmpty() && aClearType != UsedClear::None) {
    // Preferred isize accumulated for floats that have already
    // been cleared past
    nscoord floatsDone = 0;
    // Preferred isize accumulated for floats that have not yet
    // been cleared past
    nscoord floatsCurLeft = 0, floatsCurRight = 0;

    for (const FloatInfo& floatInfo : mFloats) {
      const nsStyleDisplay* floatDisp = floatInfo.Frame()->StyleDisplay();
      auto cbWM = floatInfo.Frame()->GetParent()->GetWritingMode();
      UsedClear clearType = floatDisp->UsedClear(cbWM);
      if (clearType == UsedClear::Left || clearType == UsedClear::Right ||
          clearType == UsedClear::Both) {
        nscoord floatsCur = NSCoordSaturatingAdd(floatsCurLeft, floatsCurRight);
        if (floatsCur > floatsDone) {
          floatsDone = floatsCur;
        }
        if (clearType != UsedClear::Right) {
          floatsCurLeft = 0;
        }
        if (clearType != UsedClear::Left) {
          floatsCurRight = 0;
        }
      }

      UsedFloat floatStyle = floatDisp->UsedFloat(cbWM);
      nscoord& floatsCur =
          floatStyle == UsedFloat::Left ? floatsCurLeft : floatsCurRight;
      nscoord floatISize = floatInfo.ISize();
      // Negative-width floats don't change the available space so they
      // shouldn't change our intrinsic line isize either.
      floatsCur = NSCoordSaturatingAdd(floatsCur, std::max(0, floatISize));
    }

    nscoord floatsCur = NSCoordSaturatingAdd(floatsCurLeft, floatsCurRight);
    if (floatsCur > floatsDone) {
      floatsDone = floatsCur;
    }

    mCurrentLine = NSCoordSaturatingAdd(mCurrentLine, floatsDone);

    if (aClearType == UsedClear::Both) {
      mFloats.Clear();
    } else {
      // If the break type does not clear all floats, it means there may
      // be some floats whose isize should contribute to the intrinsic
      // isize of the next line. The code here scans the current mFloats
      // and keeps floats which are not cleared by this break. Note that
      // floats may be cleared directly or indirectly. See below.
      nsTArray<FloatInfo> newFloats;
      MOZ_ASSERT(
          aClearType == UsedClear::Left || aClearType == UsedClear::Right,
          "Other values should have been handled in other branches");
      UsedFloat clearFloatType =
          aClearType == UsedClear::Left ? UsedFloat::Left : UsedFloat::Right;
      // Iterate the array in reverse so that we can stop when there are
      // no longer any floats we need to keep. See below.
      for (FloatInfo& floatInfo : Reversed(mFloats)) {
        const nsStyleDisplay* floatDisp = floatInfo.Frame()->StyleDisplay();
        auto cbWM = floatInfo.Frame()->GetParent()->GetWritingMode();
        if (floatDisp->UsedFloat(cbWM) != clearFloatType) {
          newFloats.AppendElement(floatInfo);
        } else {
          // This is a float on the side that this break directly clears
          // which means we're not keeping it in mFloats. However, if
          // this float clears floats on the opposite side (via a value
          // of either 'both' or one of 'left'/'right'), any remaining
          // (earlier) floats on that side would be indirectly cleared
          // as well. Thus, we should break out of this loop and stop
          // considering earlier floats to be kept in mFloats.
          UsedClear clearType = floatDisp->UsedClear(cbWM);
          if (clearType != aClearType && clearType != UsedClear::None) {
            break;
          }
        }
      }
      newFloats.Reverse();
      mFloats = std::move(newFloats);
    }
  }

  mCurrentLine =
      NSCoordSaturatingSubtract(mCurrentLine, mTrailingWhitespace, nscoord_MAX);
  mPrevLines = std::max(mPrevLines, mCurrentLine);
  mCurrentLine = mTrailingWhitespace = 0;
  mSkipWhitespace = true;
  mLineIsEmpty = true;
}

static nscoord ResolvePadding(const LengthPercentage& aStyle,
                              nscoord aPercentageBasis) {
  return nsLayoutUtils::ResolveToLength<true>(aStyle, aPercentageBasis);
}

static nscoord ResolveMargin(const AnchorResolvedMargin& aStyle,
                             nscoord aPercentageBasis) {
  if (!aStyle->IsLengthPercentage()) {
    return nscoord(0);
  }
  return nsLayoutUtils::ResolveToLength<false>(aStyle->AsLengthPercentage(),
                                               aPercentageBasis);
}
static nsIFrame::IntrinsicSizeOffsetData IntrinsicSizeOffsets(
    nsIFrame* aFrame, nscoord aPercentageBasis, bool aForISize) {
  nsIFrame::IntrinsicSizeOffsetData result;
  WritingMode wm = aFrame->GetWritingMode();
  bool verticalAxis = aForISize == wm.IsVertical();
  const auto* styleMargin = aFrame->StyleMargin();
  const auto positionProperty = aFrame->StyleDisplay()->mPosition;
  if (verticalAxis) {
    result.margin += ResolveMargin(
        styleMargin->GetMargin(eSideTop, positionProperty), aPercentageBasis);
    result.margin +=
        ResolveMargin(styleMargin->GetMargin(eSideBottom, positionProperty),
                      aPercentageBasis);
  } else {
    result.margin += ResolveMargin(
        styleMargin->GetMargin(eSideLeft, positionProperty), aPercentageBasis);
    result.margin += ResolveMargin(
        styleMargin->GetMargin(eSideRight, positionProperty), aPercentageBasis);
  }

  const auto& padding = aFrame->StylePadding()->mPadding;
  if (verticalAxis) {
    result.padding += ResolvePadding(padding.Get(eSideTop), aPercentageBasis);
    result.padding +=
        ResolvePadding(padding.Get(eSideBottom), aPercentageBasis);
  } else {
    result.padding += ResolvePadding(padding.Get(eSideLeft), aPercentageBasis);
    result.padding += ResolvePadding(padding.Get(eSideRight), aPercentageBasis);
  }

  const nsStyleBorder* styleBorder = aFrame->StyleBorder();
  if (verticalAxis) {
    result.border += styleBorder->GetComputedBorderWidth(eSideTop);
    result.border += styleBorder->GetComputedBorderWidth(eSideBottom);
  } else {
    result.border += styleBorder->GetComputedBorderWidth(eSideLeft);
    result.border += styleBorder->GetComputedBorderWidth(eSideRight);
  }

  const nsStyleDisplay* disp = aFrame->StyleDisplay();
  if (aFrame->IsThemed(disp)) {
    nsPresContext* presContext = aFrame->PresContext();

    LayoutDeviceIntMargin border = presContext->Theme()->GetWidgetBorder(
        presContext->DeviceContext(), aFrame, disp->EffectiveAppearance());
    result.border = presContext->DevPixelsToAppUnits(
        verticalAxis ? border.TopBottom() : border.LeftRight());

    LayoutDeviceIntMargin padding;
    if (presContext->Theme()->GetWidgetPadding(
            presContext->DeviceContext(), aFrame, disp->EffectiveAppearance(),
            &padding)) {
      result.padding = presContext->DevPixelsToAppUnits(
          verticalAxis ? padding.TopBottom() : padding.LeftRight());
    }
  }
  return result;
}

/* virtual */ nsIFrame::IntrinsicSizeOffsetData nsIFrame::IntrinsicISizeOffsets(
    nscoord aPercentageBasis) {
  return IntrinsicSizeOffsets(this, aPercentageBasis, true);
}

nsIFrame::IntrinsicSizeOffsetData nsIFrame::IntrinsicBSizeOffsets(
    nscoord aPercentageBasis) {
  return IntrinsicSizeOffsets(this, aPercentageBasis, false);
}

/* virtual */
IntrinsicSize nsIFrame::GetIntrinsicSize() {
  // Defaults to no intrinsic size.
  return IntrinsicSize();
}

AspectRatio nsIFrame::GetAspectRatio() const {
  // Per spec, 'aspect-ratio' property applies to all elements except inline
  // boxes and internal ruby or table boxes.
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio
  // For those frame types that don't support aspect-ratio, they must not have
  // the natural ratio, so this early return is fine.
  if (!SupportsAspectRatio()) {
    return AspectRatio();
  }

  const StyleAspectRatio& aspectRatio = StylePosition()->mAspectRatio;
  // If aspect-ratio is zero or infinite, it's a degenerate ratio and behaves
  // as auto.
  // https://drafts.csswg.org/css-sizing-4/#valdef-aspect-ratio-ratio
  if (!aspectRatio.BehavesAsAuto()) {
    // Non-auto. Return the preferred aspect ratio from the aspect-ratio style.
    return aspectRatio.ratio.AsRatio().ToLayoutRatio(UseBoxSizing::Yes);
  }

  // The rest of the cases are when aspect-ratio has 'auto'.
  if (auto intrinsicRatio = GetIntrinsicRatio()) {
    return intrinsicRatio;
  }

  if (aspectRatio.HasRatio()) {
    // If it's a degenerate ratio, this returns 0. Just the same as the auto
    // case.
    return aspectRatio.ratio.AsRatio().ToLayoutRatio(UseBoxSizing::No);
  }

  return AspectRatio();
}

/* virtual */
AspectRatio nsIFrame::GetIntrinsicRatio() const { return AspectRatio(); }

static bool ShouldApplyAutomaticMinimumOnInlineAxis(
    WritingMode aWM, const nsStyleDisplay* aDisplay,
    const nsStylePosition* aPosition) {
  // Apply the automatic minimum size for aspect ratio:
  // Note: The replaced elements shouldn't be here, so we only check the scroll
  // container.
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
  return !aDisplay->IsScrollableOverflow() &&
         aPosition->MinISize(aWM, aDisplay->mPosition)->IsAuto();
}

/* virtual */
nsIFrame::SizeComputationResult nsIFrame::ComputeSize(
    gfxContext* aRenderingContext, WritingMode aWM, const LogicalSize& aCBSize,
    nscoord aAvailableISize, const LogicalSize& aMargin,
    const LogicalSize& aBorderPadding, const StyleSizeOverrides& aSizeOverrides,
    ComputeSizeFlags aFlags) {
  MOZ_ASSERT(!GetIntrinsicRatio(),
             "Please override this method and call "
             "nsContainerFrame::ComputeSizeWithIntrinsicDimensions instead.");
  LogicalSize result =
      ComputeAutoSize(aRenderingContext, aWM, aCBSize, aAvailableISize, aMargin,
                      aBorderPadding, aSizeOverrides, aFlags);
  const nsStylePosition* stylePos = StylePosition();
  const nsStyleDisplay* disp = StyleDisplay();
  const auto positionProperty = disp->mPosition;
  auto aspectRatioUsage = AspectRatioUsage::None;

  const auto boxSizingAdjust = stylePos->mBoxSizing == StyleBoxSizing::Border
                                   ? aBorderPadding
                                   : LogicalSize(aWM);
  nscoord boxSizingToMarginEdgeISize = aMargin.ISize(aWM) +
                                       aBorderPadding.ISize(aWM) -
                                       boxSizingAdjust.ISize(aWM);

  const auto& aspectRatio = aSizeOverrides.mAspectRatio
                                ? *aSizeOverrides.mAspectRatio
                                : GetAspectRatio();
  const auto styleISize =
      aSizeOverrides.mStyleISize
          ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleISize)
          : stylePos->ISize(aWM, positionProperty);
  // For bsize, we consider overrides *and then* we resolve 'stretch' to a
  // nscoord value, for convenience (so that we can assume that either
  // isAutoBSize is true, or styleBSize is of type LengthPercentage()).
  const auto styleBSize = [&] {
    auto styleBSizeConsideringOverrides =
        (aSizeOverrides.mStyleBSize)
            ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleBSize)
            : stylePos->BSize(aWM, positionProperty);
    if (styleBSizeConsideringOverrides->BehavesLikeStretchOnBlockAxis() &&
        aCBSize.BSize(aWM) != NS_UNCONSTRAINEDSIZE) {
      // We've got a 'stretch' BSize; resolve it to a length:
      nscoord stretchBSize = nsLayoutUtils::ComputeStretchBSize(
          aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
          stylePos->mBoxSizing);
      return AnchorResolvedSizeHelper::LengthPercentage(
          LengthPercentage::FromAppUnits(stretchBSize));
    }
    return styleBSizeConsideringOverrides;
  }();

  auto parentFrame = GetParent();
  auto alignCB = parentFrame;
  bool isGridItem = IsGridItem();
  const bool isSubgrid = IsSubgrid();
  if (parentFrame && parentFrame->IsTableWrapperFrame() && IsTableFrame()) {
    // An inner table frame is sized as a grid item if its table wrapper is,
    // because they actually have the same CB (the wrapper's CB).
    // @see ReflowInput::InitCBReflowInput
    auto tableWrapper = GetParent();
    auto grandParent = tableWrapper->GetParent();
    isGridItem = grandParent->IsGridContainerFrame() &&
                 !tableWrapper->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW);
    if (isGridItem) {
      // When resolving justify/align-self below, we want to use the grid
      // container's justify/align-items value and WritingMode.
      alignCB = grandParent;
    }
  }

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

  const bool isOrthogonal = aWM.IsOrthogonalTo(alignCB->GetWritingMode());
  const bool isAutoISize = styleISize->IsAuto();
  const bool isAutoBSize =
      nsLayoutUtils::IsAutoBSize(*styleBSize, aCBSize.BSize(aWM));

  MOZ_ASSERT(isAutoBSize || styleBSize->IsLengthPercentage(),
             "We should have resolved away any non-'auto'-like flavors "
             "of styleBSize into a LengthPercentage. (If this fails, we "
             "might run afoul of some AsLengthPercentage() call below.)");

  // Compute inline-axis size
  const bool isSubgriddedInInlineAxis =
      isSubgrid && static_cast<nsGridContainerFrame*>(this)->IsColSubgrid();

  // Per https://drafts.csswg.org/css-grid/#subgrid-box-alignment, if we are
  // subgridded in the inline-axis, ignore our style inline-size, and stretch to
  // fill the CB.
  const bool shouldComputeISize = !isAutoISize && !isSubgriddedInInlineAxis;
  if (shouldComputeISize) {
    auto iSizeResult =
        ComputeISizeValue(aRenderingContext, aWM, aCBSize, boxSizingAdjust,
                          boxSizingToMarginEdgeISize, *styleISize, *styleBSize,
                          aspectRatio, aFlags);
    result.ISize(aWM) = iSizeResult.mISize;
    aspectRatioUsage = iSizeResult.mAspectRatioUsage;
  } else if (MOZ_UNLIKELY(isGridItem) && !IsTrueOverflowContainer()) {
    // 'auto' inline-size for grid-level box - fill the CB for 'stretch' /
    // 'normal' and clamp it to the CB if requested:
    bool isStretchAligned = false;
    bool mayUseAspectRatio = aspectRatio && !isAutoBSize;
    if (!aFlags.contains(ComputeSizeFlag::ShrinkWrap) &&
        !StyleMargin()->HasInlineAxisAuto(aWM, positionProperty) &&
        !alignCB->IsMasonry(isOrthogonal ? LogicalAxis::Block
                                         : LogicalAxis::Inline)) {
      auto inlineAxisAlignment =
          isOrthogonal ? StylePosition()->UsedAlignSelf(alignCB->Style())._0
                       : StylePosition()->UsedJustifySelf(alignCB->Style())._0;
      isStretchAligned = inlineAxisAlignment == StyleAlignFlags::STRETCH ||
                         (inlineAxisAlignment == StyleAlignFlags::NORMAL &&
                          !mayUseAspectRatio);
    }

    // Apply the preferred aspect ratio for alignments other than *stretch* and
    // *normal without aspect ratio*.
    // The spec says all other values should size the items as fit-content, and
    // the intrinsic size should respect the preferred aspect ratio, so we also
    // apply aspect ratio for all other values.
    // https://drafts.csswg.org/css-grid/#grid-item-sizing
    if (!isStretchAligned && mayUseAspectRatio) {
      result.ISize(aWM) = ComputeISizeValueFromAspectRatio(
          aWM, aCBSize, boxSizingAdjust, styleBSize->AsLengthPercentage(),
          aspectRatio);
      aspectRatioUsage = AspectRatioUsage::ToComputeISize;
    }

    if (isStretchAligned ||
        aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize)) {
      auto iSizeToFillCB =
          std::max(nscoord(0), aCBSize.ISize(aWM) - aBorderPadding.ISize(aWM) -
                                   aMargin.ISize(aWM));
      if (isStretchAligned || result.ISize(aWM) > iSizeToFillCB) {
        result.ISize(aWM) = iSizeToFillCB;
      }
    }
  } else if (aspectRatio && !isAutoBSize) {
    // Note: if both the inline size and the block size are auto, the block axis
    // is the ratio-dependent axis by default. That means we only need to
    // transfer the resolved inline size via aspect-ratio to block axis later in
    // this method, but not the other way around.
    //
    // In this branch, we transfer the non-auto block size via aspect-ration to
    // inline axis.
    result.ISize(aWM) = ComputeISizeValueFromAspectRatio(
        aWM, aCBSize, boxSizingAdjust, styleBSize->AsLengthPercentage(),
        aspectRatio);
    aspectRatioUsage = AspectRatioUsage::ToComputeISize;
  }

  // Calculate and apply transferred min & max size contraints.
  // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-size-transfers
  //
  // Note: The basic principle is that sizing constraints transfer through the
  // aspect-ratio to the other side to preserve the aspect ratio to the extent
  // that they can without violating any sizes specified explicitly on that
  // affected axis.
  //
  // FIXME: The spec words may not be correct, so we may have to update this
  // tentative solution once this spec issue gets resolved. Here, we clamp the
  // flex base size by the transferred min and max sizes, and don't include
  // the transferred min & max sizes into its used min & max sizes. So this
  // lets us match other browsers' current behaviors.
  // https://github.com/w3c/csswg-drafts/issues/6071
  //
  // Note: This may make more sense if we clamp the flex base size in
  // FlexItem::ResolveFlexBaseSizeFromAspectRatio(). However, the result should
  // be identical. FlexItem::ResolveFlexBaseSizeFromAspectRatio() only handles
  // the case of the definite cross size, and the definite cross size is clamped
  // by the min & max cross sizes below in this function. This means its flex
  // base size has been clamped by the transferred min & max size already after
  // generating the flex items. So here we make the code more general for both
  // definite cross size and indefinite cross size.
  const bool isDefiniteISize = styleISize->IsLengthPercentage();
  const auto minBSizeCoord = stylePos->MinBSize(aWM, positionProperty);
  const auto maxBSizeCoord = stylePos->MaxBSize(aWM, positionProperty);
  const bool isAutoMinBSize =
      nsLayoutUtils::IsAutoBSize(*minBSizeCoord, aCBSize.BSize(aWM));
  const bool isAutoMaxBSize =
      nsLayoutUtils::IsAutoBSize(*maxBSizeCoord, aCBSize.BSize(aWM));
  if (aspectRatio && !isDefiniteISize) {
    // Note: the spec mentions that
    // 1. This transferred minimum is capped by any definite preferred or
    //    maximum size in the destination axis.
    // 2. This transferred maximum is floored by any definite preferred or
    //    minimum size in the destination axis.
    //
    // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-size-transfers
    //
    // The spec requires us to clamp these by the specified size (it calls it
    // the preferred size). However, we actually don't need to worry about that,
    // because we are here only if the inline size is indefinite.
    //
    // We do not need to clamp the transferred minimum and maximum as long as we
    // always apply the transferred min/max size before the explicit min/max
    // size; the result will be identical.
    const nscoord transferredMinISize =
        isAutoMinBSize ? 0
                       : ComputeISizeValueFromAspectRatio(
                             aWM, aCBSize, boxSizingAdjust,
                             minBSizeCoord->AsLengthPercentage(), aspectRatio);
    const nscoord transferredMaxISize =
        isAutoMaxBSize ? nscoord_MAX
                       : ComputeISizeValueFromAspectRatio(
                             aWM, aCBSize, boxSizingAdjust,
                             maxBSizeCoord->AsLengthPercentage(), aspectRatio);

    result.ISize(aWM) =
        CSSMinMax(result.ISize(aWM), transferredMinISize, transferredMaxISize);
  }

  // Flex items ignore their min & max sizing properties in their flex
  // container's main-axis. (Those properties get applied later in the flexbox
  // algorithm.)
  const bool isFlexItemInlineAxisMainAxis =
      flexItemMainAxis && *flexItemMainAxis == LogicalAxis::Inline;
  // Grid items that are subgridded in inline-axis also ignore their min & max
  // sizing properties in that axis.
  const bool shouldIgnoreMinMaxISize =
      isFlexItemInlineAxisMainAxis || isSubgriddedInInlineAxis;
  const auto maxISizeCoord = stylePos->MaxISize(aWM, positionProperty);
  nscoord maxISize = NS_UNCONSTRAINEDSIZE;
  if (!maxISizeCoord->IsNone() && !shouldIgnoreMinMaxISize) {
    maxISize =
        ComputeISizeValue(aRenderingContext, aWM, aCBSize, boxSizingAdjust,
                          boxSizingToMarginEdgeISize, *maxISizeCoord,
                          *styleBSize, aspectRatio, aFlags)
            .mISize;
    result.ISize(aWM) = std::min(maxISize, result.ISize(aWM));
  }

  const nscoord bSizeAsPercentageBasis = ComputeBSizeValueAsPercentageBasis(
      *styleBSize, *minBSizeCoord, *maxBSizeCoord, aCBSize.BSize(aWM),
      boxSizingAdjust.BSize(aWM));
  const IntrinsicSizeInput input(
      aRenderingContext, Some(aCBSize.ConvertTo(GetWritingMode(), aWM)),
      Some(LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, bSizeAsPercentageBasis)
               .ConvertTo(GetWritingMode(), aWM)));
  const auto minISizeCoord = stylePos->MinISize(aWM, positionProperty);
  nscoord minISize;
  if (!minISizeCoord->IsAuto() && !shouldIgnoreMinMaxISize) {
    minISize =
        ComputeISizeValue(aRenderingContext, aWM, aCBSize, boxSizingAdjust,
                          boxSizingToMarginEdgeISize, *minISizeCoord,
                          *styleBSize, aspectRatio, aFlags)
            .mISize;
  } else if (MOZ_UNLIKELY(
                 aFlags.contains(ComputeSizeFlag::IApplyAutoMinSize))) {
    // This implements "Implied Minimum Size of Grid Items".
    // https://drafts.csswg.org/css-grid/#min-size-auto
    minISize = std::min(maxISize, GetMinISize(input));
    if (styleISize->IsLengthPercentage()) {
      minISize = std::min(minISize, result.ISize(aWM));
    } else if (aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize)) {
      // "if the grid item spans only grid tracks that have a fixed max track
      // sizing function, its automatic minimum size in that dimension is
      // further clamped to less than or equal to the size necessary to fit
      // its margin box within the resulting grid area (flooring at zero)"
      // https://drafts.csswg.org/css-grid/#min-size-auto
      auto maxMinISize =
          std::max(nscoord(0), aCBSize.ISize(aWM) - aBorderPadding.ISize(aWM) -
                                   aMargin.ISize(aWM));
      minISize = std::min(minISize, maxMinISize);
    }
  } else if (aspectRatioUsage == AspectRatioUsage::ToComputeISize &&
             ShouldApplyAutomaticMinimumOnInlineAxis(aWM, disp, stylePos)) {
    // This means we successfully applied aspect-ratio and now need to check
    // if we need to apply the automatic content-based minimum size:
    // https://drafts.csswg.org/css-sizing-4/#aspect-ratio-minimum
    MOZ_ASSERT(!HasReplacedSizing(),
               "aspect-ratio minimums should not apply to replaced elements");
    // The inline size computed by aspect-ratio shouldn't less than the
    // min-content size, which should be capped by its maximum inline size.
    minISize = std::min(GetMinISize(input), maxISize);
  } else {
    // Treat "min-width: auto" as 0.
    // NOTE: Technically, "auto" is supposed to behave like "min-content" on
    // flex items. However, we don't need to worry about that here, because
    // flex items' min-sizes are intentionally ignored until the flex
    // container explicitly considers them during space distribution.
    minISize = 0;
  }
  result.ISize(aWM) = std::max(minISize, result.ISize(aWM));

  // Compute block-axis size
  // (but not if we have auto bsize  -- then, we'll just stick with the bsize
  // that we already calculated in the initial ComputeAutoSize() call. However,
  // if we have a valid preferred aspect ratio, we still have to compute the
  // block size because aspect ratio affects the intrinsic content size.)
  const bool isSubgriddedInBlockAxis =
      isSubgrid && static_cast<nsGridContainerFrame*>(this)->IsRowSubgrid();

  // Per https://drafts.csswg.org/css-grid/#subgrid-box-alignment, if we are
  // subgridded in the block-axis, ignore our style block-size, and stretch to
  // fill the CB.
  const bool shouldComputeBSize = !isAutoBSize && !isSubgriddedInBlockAxis;
  if (shouldComputeBSize) {
    result.BSize(aWM) = nsLayoutUtils::ComputeBSizeValue(
        aCBSize.BSize(aWM), boxSizingAdjust.BSize(aWM),
        styleBSize->AsLengthPercentage());
  } else if (MOZ_UNLIKELY(isGridItem) && styleBSize->IsAuto() &&
             !aFlags.contains(ComputeSizeFlag::IsGridMeasuringReflow) &&
             !IsTrueOverflowContainer() &&
             !alignCB->IsMasonry(isOrthogonal ? LogicalAxis::Inline
                                              : LogicalAxis::Block)) {
    auto cbSize = aCBSize.BSize(aWM);
    if (cbSize != NS_UNCONSTRAINEDSIZE) {
      // 'auto' block-size for grid-level box - fill the CB for 'stretch' /
      // 'normal' and clamp it to the CB if requested:
      bool isStretchAligned = false;
      bool mayUseAspectRatio =
          aspectRatio && result.ISize(aWM) != NS_UNCONSTRAINEDSIZE;
      if (!StyleMargin()->HasBlockAxisAuto(aWM, positionProperty)) {
        auto blockAxisAlignment =
            isOrthogonal ? StylePosition()->UsedJustifySelf(alignCB->Style())._0
                         : StylePosition()->UsedAlignSelf(alignCB->Style())._0;
        isStretchAligned = blockAxisAlignment == StyleAlignFlags::STRETCH ||
                           (blockAxisAlignment == StyleAlignFlags::NORMAL &&
                            !mayUseAspectRatio);
      }

      // Apply the preferred aspect ratio for alignments other than *stretch*
      // and *normal without aspect ratio*.
      // The spec says all other values should size the items as fit-content,
      // and the intrinsic size should respect the preferred aspect ratio, so
      // we also apply aspect ratio for all other values.
      // https://drafts.csswg.org/css-grid/#grid-item-sizing
      if (!isStretchAligned && mayUseAspectRatio) {
        result.BSize(aWM) = aspectRatio.ComputeRatioDependentSize(
            LogicalAxis::Block, aWM, result.ISize(aWM), boxSizingAdjust);
        MOZ_ASSERT(aspectRatioUsage == AspectRatioUsage::None);
        aspectRatioUsage = AspectRatioUsage::ToComputeBSize;
      }

      if (isStretchAligned ||
          aFlags.contains(ComputeSizeFlag::BClampMarginBoxMinSize)) {
        auto bSizeToFillCB = nsLayoutUtils::ComputeStretchContentBoxBSize(
            cbSize, aMargin.BSize(aWM), aBorderPadding.BSize(aWM));
        if (isStretchAligned || (result.BSize(aWM) != NS_UNCONSTRAINEDSIZE &&
                                 result.BSize(aWM) > bSizeToFillCB)) {
          result.BSize(aWM) = bSizeToFillCB;
        }
      }
    }
  } else if (aspectRatio) {
    // If both inline and block dimensions are auto, the block axis is the
    // ratio-dependent axis by default.
    // If we have a super large inline size, aspect-ratio should still be
    // applied (so aspectRatioUsage flag is set as expected). That's why we
    // apply aspect-ratio unconditionally for auto block size here.
    result.BSize(aWM) = aspectRatio.ComputeRatioDependentSize(
        LogicalAxis::Block, aWM, result.ISize(aWM), boxSizingAdjust);
    MOZ_ASSERT(aspectRatioUsage == AspectRatioUsage::None);
    aspectRatioUsage = AspectRatioUsage::ToComputeBSize;
  }

  if (result.BSize(aWM) != NS_UNCONSTRAINEDSIZE) {
    // Flex items ignore their min & max sizing properties in their flex
    // container's main-axis. (Those properties get applied later in the flexbox
    // algorithm.)
    const bool isFlexItemBlockAxisMainAxis =
        flexItemMainAxis && *flexItemMainAxis == LogicalAxis::Block;
    // Grid items that are subgridded in block-axis also ignore their min & max
    // sizing properties in that axis.
    const bool shouldIgnoreMinMaxBSize =
        isFlexItemBlockAxisMainAxis || isSubgriddedInBlockAxis;
    if (!isAutoMaxBSize && !shouldIgnoreMinMaxBSize) {
      nscoord maxBSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
          aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
          boxSizingAdjust.BSize(aWM), *maxBSizeCoord);
      result.BSize(aWM) = std::min(maxBSize, result.BSize(aWM));
    }

    if (!isAutoMinBSize && !shouldIgnoreMinMaxBSize) {
      nscoord minBSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
          aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM),
          boxSizingAdjust.BSize(aWM), *minBSizeCoord);
      result.BSize(aWM) = std::max(minBSize, result.BSize(aWM));
    }
  }

  if (IsThemed(disp)) {
    nsPresContext* pc = PresContext();
    const LayoutDeviceIntSize widget = pc->Theme()->GetMinimumWidgetSize(
        pc, this, disp->EffectiveAppearance());

    // Convert themed widget's physical dimensions to logical coords
    LogicalSize size(aWM, LayoutDeviceIntSize::ToAppUnits(
                              widget, pc->AppUnitsPerDevPixel()));

    // GetMinimumWidgetSize() returns border-box; we need content-box.
    size -= aBorderPadding;

    if (size.BSize(aWM) > result.BSize(aWM)) {
      result.BSize(aWM) = size.BSize(aWM);
    }
    if (size.ISize(aWM) > result.ISize(aWM)) {
      result.ISize(aWM) = size.ISize(aWM);
    }
  }

  result.ISize(aWM) = std::max(0, result.ISize(aWM));
  result.BSize(aWM) = std::max(0, result.BSize(aWM));

  return {result, aspectRatioUsage};
}

nscoord nsIFrame::ComputeBSizeValueAsPercentageBasis(
    const StyleSize& aStyleBSize, const StyleSize& aStyleMinBSize,
    const StyleMaxSize& aStyleMaxBSize, nscoord aCBBSize,
    nscoord aContentEdgeToBoxSizingBSize) {
  if (nsLayoutUtils::IsAutoBSize(aStyleBSize, aCBBSize)) {
    return NS_UNCONSTRAINEDSIZE;
  }

  // TODO(dholbert): This is a temporary hack, to be fixed up in bug 1933604.
  // We don't know have aMargin or aBorderPadding args available,
  // so we use these dummy zero-valued variables as placeholders in
  // our call to ComputeBSizeValueHandlingStretch. (This might mean we
  // end up resolving 'stretch' to something slighlty-too-large for the
  // purposes of this call, if there's actually nonzero margin/border/padding).
  const nscoord dummyMargin = 0;
  const nscoord dummyBorderPadding = 0;

  const nscoord bSize = nsLayoutUtils::ComputeBSizeValueHandlingStretch(
      aCBBSize, dummyMargin, dummyBorderPadding, aContentEdgeToBoxSizingBSize,
      aStyleBSize);

  const nscoord minBSize =
      nsLayoutUtils::IsAutoBSize(aStyleMinBSize, aCBBSize)
          ? 0
          : nsLayoutUtils::ComputeBSizeValueHandlingStretch(
                aCBBSize, dummyMargin, dummyBorderPadding,
                aContentEdgeToBoxSizingBSize, aStyleMinBSize);

  const nscoord maxBSize =
      nsLayoutUtils::IsAutoBSize(aStyleMaxBSize, aCBBSize)
          ? NS_UNCONSTRAINEDSIZE
          : nsLayoutUtils::ComputeBSizeValueHandlingStretch(
                aCBBSize, dummyMargin, dummyBorderPadding,
                aContentEdgeToBoxSizingBSize, aStyleMaxBSize);

  return CSSMinMax(bSize, minBSize, maxBSize);
}

nsRect nsIFrame::ComputeTightBounds(DrawTarget* aDrawTarget) const {
  return InkOverflowRect();
}

/* virtual */
nsresult nsIFrame::GetPrefWidthTightBounds(gfxContext* aContext, nscoord* aX,
                                           nscoord* aXMost) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* virtual */
LogicalSize nsIFrame::ComputeAutoSize(
    gfxContext* aRenderingContext, WritingMode aWM,
    const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
    const mozilla::LogicalSize& aMargin,
    const mozilla::LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, ComputeSizeFlags aFlags) {
  if (IsAbsolutelyPositionedWithDefiniteContainingBlock()) {
    return ComputeAbsolutePosAutoSize(aRenderingContext, aWM, aCBSize,
                                      aAvailableISize, aMargin, aBorderPadding,
                                      aSizeOverrides, aFlags);
  }

  // Use basic shrink-wrapping as a default implementation.
  LogicalSize result(aWM, 0xdeadbeef, NS_UNCONSTRAINEDSIZE);

  const auto positionProperty = StyleDisplay()->mPosition;
  // don't bother setting it if the result won't be used
  const auto styleISize =
      aSizeOverrides.mStyleISize
          ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleISize)
          : StylePosition()->ISize(aWM, positionProperty);
  if (styleISize->IsAuto()) {
    nscoord availBased = nsLayoutUtils::ComputeStretchContentBoxISize(
        aAvailableISize, aMargin.ISize(aWM), aBorderPadding.ISize(aWM));
    const auto* stylePos = StylePosition();
    const auto styleBSize =
        aSizeOverrides.mStyleBSize
            ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleBSize)
            : stylePos->BSize(aWM, positionProperty);
    const LogicalSize contentEdgeToBoxSizing =
        stylePos->mBoxSizing == StyleBoxSizing::Border ? aBorderPadding
                                                       : LogicalSize(aWM);
    const nscoord bSize = ComputeBSizeValueAsPercentageBasis(
        *styleBSize, *stylePos->MinBSize(aWM, positionProperty),
        *stylePos->MaxBSize(aWM, positionProperty), aCBSize.BSize(aWM),
        contentEdgeToBoxSizing.BSize(aWM));
    const IntrinsicSizeInput input(
        aRenderingContext, Some(aCBSize.ConvertTo(GetWritingMode(), aWM)),
        Some(LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, bSize)
                 .ConvertTo(GetWritingMode(), aWM)));
    result.ISize(aWM) = ShrinkISizeToFit(input, availBased, aFlags);
  }
  return result;
}

bool nsIFrame::IsAbsolutelyPositionedWithDefiniteContainingBlock() const {
  // TODO(dshin, Bug 1927861): Even if an absolute container should have a
  // definite size, in a continuation context, the full extent of the containing
  // block is not known.
  return MOZ_UNLIKELY(IsAbsolutelyPositioned()) && !GetPrevInFlow();
}

LogicalSize nsIFrame::ComputeAbsolutePosAutoSize(
    gfxContext* aRenderingContext, WritingMode aWM,
    const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
    const mozilla::LogicalSize& aMargin,
    const mozilla::LogicalSize& aBorderPadding,
    const StyleSizeOverrides& aSizeOverrides, const ComputeSizeFlags& aFlags) {
  MOZ_ASSERT(IsAbsolutelyPositionedWithDefiniteContainingBlock(),
             "Asking for absolute auto size when not absolute");
  // Ideally, this is an assertion, but the containing block could just be
  // really big.
  NS_WARNING_ASSERTION(aCBSize.ISize(aWM) != NS_UNCONSTRAINEDSIZE &&
                           aCBSize.BSize(aWM) != NS_UNCONSTRAINEDSIZE,
                       "Absolute containing block size not definite?");
  LogicalSize result(aWM, static_cast<nscoord>(0xdeadbeef),
                     NS_UNCONSTRAINEDSIZE);

  const auto* stylePos = StylePosition();
  const auto positionProperty = StyleDisplay()->mPosition;
  const auto& styleISize =
      aSizeOverrides.mStyleISize
          ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleISize)
          : stylePos->ISize(aWM, positionProperty);
  const auto& styleBSize =
      aSizeOverrides.mStyleBSize
          ? AnchorResolvedSizeHelper::Overridden(*aSizeOverrides.mStyleBSize)
          : stylePos->BSize(aWM, positionProperty);
  const auto iStartOffsetIsAuto =
      stylePos
          ->GetAnchorResolvedInset(LogicalSide::IStart, aWM, positionProperty)
          ->IsAuto();
  const auto iEndOffsetIsAuto =
      stylePos->GetAnchorResolvedInset(LogicalSide::IEnd, aWM, positionProperty)
          ->IsAuto();
  const auto bStartOffsetIsAuto =
      stylePos
          ->GetAnchorResolvedInset(LogicalSide::BStart, aWM, positionProperty)
          ->IsAuto();
  const auto bEndOffsetIsAuto =
      stylePos->GetAnchorResolvedInset(LogicalSide::BEnd, aWM, positionProperty)
          ->IsAuto();
  const auto boxSizingAdjust = stylePos->mBoxSizing == StyleBoxSizing::Border
                                   ? aBorderPadding
                                   : LogicalSize(aWM);
  auto shouldStretch = [](StyleSelfAlignment aAlignment, const nsIFrame* aFrame,
                          bool aStartIsAuto, bool aEndIsAuto) {
    if (aStartIsAuto || aEndIsAuto) {
      // Note(dshin, bug 1930427): This is not part of the current spec [1];
      // however, no one implements the new inset behaviour [2], and the old
      // behaviour [3] ends up computing the static size if both or one inset is
      // auto.
      //
      // [1]: https://drafts.csswg.org/css-position-3/#abspos-auto-size
      // [2]: https://drafts.csswg.org/css-position-3/#resolving-insets
      // [3]: https://drafts.csswg.org/css-position-3/#abspos-old
      return false;
    }
    // Don't care about flag bits for auto-sizing.
    aAlignment &= ~StyleSelfAlignment::FLAG_BITS;

    if (aAlignment == StyleSelfAlignment::STRETCH) {
      return true;
    }

    if (aAlignment == StyleSelfAlignment::NORMAL) {
      // Some replaced elements behave as semi-replaced elements - we want them
      // to stretch (See bug 1740580).
      return !aFrame->HasReplacedSizing() && !aFrame->IsTableWrapperFrame();
    }

    return false;
  };

  // i.e. Absolute containing block
  const auto* parent = GetParent();
  const auto parentWM = parent->GetWritingMode();
  // Self alignment properties translate `auto` to normal for this purpose.
  // https://drafts.csswg.org/css-align-3/#valdef-justify-self-auto
  const auto inlineAlignSelf = parentWM.IsOrthogonalTo(aWM)
                                   ? stylePos->UsedAlignSelf(nullptr)._0
                                   : stylePos->UsedJustifySelf(nullptr)._0;
  const auto blockAlignSelf = parentWM.IsOrthogonalTo(aWM)
                                  ? stylePos->UsedJustifySelf(nullptr)._0
                                  : stylePos->UsedAlignSelf(nullptr)._0;
  const auto iShouldStretch = shouldStretch(
      inlineAlignSelf, this, iStartOffsetIsAuto, iEndOffsetIsAuto);
  const auto bShouldStretch =
      shouldStretch(blockAlignSelf, this, bStartOffsetIsAuto, bEndOffsetIsAuto);
  const auto iSizeIsAuto = styleISize->IsAuto();
  // Note(dshin, bug 1789477): `auto` in the context of abs-element uses
  // stretch-fit sizing, given specific alignment conditions [1]. Effectively,
  // `auto` is `stretch`. `nsLayoutUtils::IsAutoBSize` is not the right tool
  // here, since the mapping is explicit, and it's incorrect to e.g. map
  // `fit-content` to `stretch`.
  // `-moz-available` behaves like `auto` in general, so map the same way.
  // When Bug 567039 brings `-moz-available` into alignment with `stretch`, this
  // special check can be removed. TODO(dshin): we're probably duplicating the
  // `stretch` logic here, since `stretch` is `stretch-fit` sizing [2].
  //
  // [1]: https://drafts.csswg.org/css-position/#abspos-auto-size
  // [2]: https://drafts.csswg.org/css-sizing-4/#valdef-width-stretch
  const auto bSizeIsAuto = styleBSize->IsAuto() || styleBSize->IsMozAvailable();
  if (bSizeIsAuto && bShouldStretch) {
    result.BSize(aWM) = nsLayoutUtils::ComputeStretchContentBoxBSize(
        aCBSize.BSize(aWM), aMargin.BSize(aWM), aBorderPadding.BSize(aWM));
  }
  if (iSizeIsAuto) {
    if (iShouldStretch) {
      // inline-size to make our margin-box fill the containing block:
      result.ISize(aWM) = nsLayoutUtils::ComputeStretchContentBoxISize(
          aCBSize.ISize(aWM), aMargin.ISize(aWM), aBorderPadding.ISize(aWM));
    } else {
      // inline-size to make our margin-box fill aAvailableISize:
      nscoord availBased = nsLayoutUtils::ComputeStretchContentBoxISize(
          aAvailableISize, aMargin.ISize(aWM), aBorderPadding.ISize(aWM));

      const nscoord bSize = ComputeBSizeValueAsPercentageBasis(
          styleBSize->IsAuto() && result.BSize(aWM) != NS_UNCONSTRAINEDSIZE
              ? StyleSize::LengthPercentage(
                    StyleLengthPercentage::FromAppUnits(result.BSize(aWM)))
              : *styleBSize,
          *stylePos->MinBSize(aWM, positionProperty),
          *stylePos->MaxBSize(aWM, positionProperty), aCBSize.BSize(aWM),
          boxSizingAdjust.BSize(aWM));

      const IntrinsicSizeInput input(
          aRenderingContext, Some(aCBSize.ConvertTo(GetWritingMode(), aWM)),
          Some(LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, bSize)
                   .ConvertTo(GetWritingMode(), aWM)));
      result.ISize(aWM) = ShrinkISizeToFit(input, availBased, aFlags);
    }
  }

  const auto& aspectRatio = aSizeOverrides.mAspectRatio
                                ? *aSizeOverrides.mAspectRatio
                                : GetAspectRatio();
  if (aspectRatio) {
    auto aspectRatioUsage = AspectRatioUsage::None;
    if (iSizeIsAuto != bSizeIsAuto) {
      // Auto axis is dependent.
      if (iSizeIsAuto) {
        aspectRatioUsage = AspectRatioUsage::ToComputeBSize;
      } else {
        aspectRatioUsage = AspectRatioUsage::ToComputeISize;
      }
    } else if (iSizeIsAuto) {
      // Both axes are `auto`.
      if (iShouldStretch != bShouldStretch) {
        // If an axis has stretch, that behaves like a definite size.
        aspectRatioUsage = iShouldStretch ? AspectRatioUsage::ToComputeBSize
                                          : AspectRatioUsage::ToComputeISize;
      } else if (!iShouldStretch) {
        // If one axis has `auto` inset, that is the ratio dependent axis,
        // otherwise the block axis is.
        const bool inlineInsetHasAuto = iStartOffsetIsAuto || iEndOffsetIsAuto;
        const bool blockInsetHasAuto = bStartOffsetIsAuto || bEndOffsetIsAuto;
        aspectRatioUsage = inlineInsetHasAuto && !blockInsetHasAuto
                               ? AspectRatioUsage::ToComputeISize
                               : AspectRatioUsage::ToComputeBSize;
      }
    }

    if (aspectRatioUsage == AspectRatioUsage::ToComputeBSize &&
        !bShouldStretch) {
      result.BSize(aWM) = aspectRatio.ComputeRatioDependentSize(
          LogicalAxis::Block, aWM, result.ISize(aWM), boxSizingAdjust);
    } else if (aspectRatioUsage == AspectRatioUsage::ToComputeISize &&
               !iShouldStretch && result.BSize(aWM) != NS_UNCONSTRAINEDSIZE) {
      result.ISize(aWM) = aspectRatio.ComputeRatioDependentSize(
          LogicalAxis::Inline, aWM, result.BSize(aWM), boxSizingAdjust);
    }
  }

  return result;
}

nscoord nsIFrame::ShrinkISizeToFit(const IntrinsicSizeInput& aInput,
                                   nscoord aISizeInCB,
                                   ComputeSizeFlags aFlags) {
  // If we're a container for font size inflation, then shrink
  // wrapping inside of us should not apply font size inflation.
  AutoMaybeDisableFontInflation an(this);

  nscoord result;
  nscoord minISize = GetMinISize(aInput);
  if (minISize > aISizeInCB) {
    const bool clamp = aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize);
    result = MOZ_UNLIKELY(clamp) ? aISizeInCB : minISize;
  } else {
    nscoord prefISize = GetPrefISize(aInput);
    if (prefISize > aISizeInCB) {
      result = aISizeInCB;
    } else {
      result = prefISize;
    }
  }
  return result;
}

nscoord nsIFrame::IntrinsicISizeFromInline(const IntrinsicSizeInput& aInput,
                                           IntrinsicISizeType aType) {
  MOZ_ASSERT(!IsContainerForFontSizeInflation(),
             "Should not be a container for font size inflation!");

  if (aType == IntrinsicISizeType::MinISize) {
    InlineMinISizeData data;
    AddInlineMinISize(aInput, &data);
    data.ForceBreak();
    return data.mPrevLines;
  }

  InlinePrefISizeData data;
  AddInlinePrefISize(aInput, &data);
  data.ForceBreak();
  return data.mPrevLines;
}

nscoord nsIFrame::ComputeISizeValueFromAspectRatio(
    WritingMode aWM, const LogicalSize& aCBSize,
    const LogicalSize& aContentEdgeToBoxSizing, const LengthPercentage& aBSize,
    const AspectRatio& aAspectRatio) const {
  MOZ_ASSERT(aAspectRatio, "Must have a valid AspectRatio!");
  const nscoord bSize = nsLayoutUtils::ComputeBSizeValue(
      aCBSize.BSize(aWM), aContentEdgeToBoxSizing.BSize(aWM), aBSize);
  return aAspectRatio.ComputeRatioDependentSize(LogicalAxis::Inline, aWM, bSize,
                                                aContentEdgeToBoxSizing);
}

nsIFrame::ISizeComputationResult nsIFrame::ComputeISizeValue(
    gfxContext* aRenderingContext, const WritingMode aWM,
    const LogicalSize& aCBSize, const LogicalSize& aContentEdgeToBoxSizing,
    nscoord aBoxSizingToMarginEdge, ExtremumLength aSize,
    Maybe<nscoord> aAvailableISizeOverride, const StyleSize& aStyleBSize,
    const AspectRatio& aAspectRatio, ComputeSizeFlags aFlags) {
  auto GetAvailableISize = [&]() {
    return aCBSize.ISize(aWM) - aBoxSizingToMarginEdge -
           aContentEdgeToBoxSizing.ISize(aWM);
  };

  // If 'this' is a container for font size inflation, then shrink
  // wrapping inside of it should not apply font size inflation.
  AutoMaybeDisableFontInflation an(this);
  // If we have an aspect-ratio and a definite block size, we should use them to
  // resolve the sizes with intrinsic keywords.
  // https://github.com/w3c/csswg-drafts/issues/5032
  Maybe<nscoord> iSizeFromAspectRatio = [&]() -> Maybe<nscoord> {
    if (aSize == ExtremumLength::MozAvailable ||
        aSize == ExtremumLength::Stretch) {
      return Nothing();
    }
    if (!aAspectRatio) {
      return Nothing();
    }
    if (nsLayoutUtils::IsAutoBSize(aStyleBSize, aCBSize.BSize(aWM))) {
      return Nothing();
    }
    return Some(ComputeISizeValueFromAspectRatio(
        aWM, aCBSize, aContentEdgeToBoxSizing, aStyleBSize.AsLengthPercentage(),
        aAspectRatio));
  }();

  const auto* stylePos = StylePosition();
  const auto positionProperty = StyleDisplay()->mPosition;
  const nscoord bSize = ComputeBSizeValueAsPercentageBasis(
      aStyleBSize, *stylePos->MinBSize(aWM, positionProperty),
      *stylePos->MaxBSize(aWM, positionProperty), aCBSize.BSize(aWM),
      aContentEdgeToBoxSizing.BSize(aWM));
  const IntrinsicSizeInput input(
      aRenderingContext, Some(aCBSize.ConvertTo(GetWritingMode(), aWM)),
      Some(LogicalSize(aWM, NS_UNCONSTRAINEDSIZE, bSize)
               .ConvertTo(GetWritingMode(), aWM)));
  nscoord result;
  switch (aSize) {
    case ExtremumLength::MaxContent:
      result =
          iSizeFromAspectRatio ? *iSizeFromAspectRatio : GetPrefISize(input);
      NS_ASSERTION(result >= 0, "inline-size less than zero");
      return {result, iSizeFromAspectRatio ? AspectRatioUsage::ToComputeISize
                                           : AspectRatioUsage::None};
    case ExtremumLength::MinContent:
      result =
          iSizeFromAspectRatio ? *iSizeFromAspectRatio : GetMinISize(input);
      NS_ASSERTION(result >= 0, "inline-size less than zero");
      if (MOZ_UNLIKELY(
              aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize))) {
        result = std::min(GetAvailableISize(), result);
      }
      return {result, iSizeFromAspectRatio ? AspectRatioUsage::ToComputeISize
                                           : AspectRatioUsage::None};
    case ExtremumLength::FitContentFunction:
    case ExtremumLength::FitContent: {
      nscoord pref = NS_UNCONSTRAINEDSIZE;
      nscoord min = 0;
      if (iSizeFromAspectRatio) {
        // The min-content and max-content size are identical and equal to the
        // size computed from the block size and the aspect ratio.
        pref = min = *iSizeFromAspectRatio;
      } else {
        pref = GetPrefISize(input);
        min = GetMinISize(input);
      }

      const nscoord fill = aAvailableISizeOverride ? *aAvailableISizeOverride
                                                   : GetAvailableISize();
      if (MOZ_UNLIKELY(
              aFlags.contains(ComputeSizeFlag::IClampMarginBoxMinSize))) {
        min = std::min(min, fill);
      }
      result = std::max(min, std::min(pref, fill));
      NS_ASSERTION(result >= 0, "inline-size less than zero");
      return {result};
    }
    case ExtremumLength::MozAvailable:
    case ExtremumLength::Stretch:
      return {GetAvailableISize()};
  }
  MOZ_ASSERT_UNREACHABLE("Unknown extremum length?");
  return {};
}

nscoord nsIFrame::ComputeISizeValue(const WritingMode aWM,
                                    const LogicalSize& aCBSize,
                                    const LogicalSize& aContentEdgeToBoxSizing,
                                    const LengthPercentage& aSize) const {
  LAYOUT_WARN_IF_FALSE(
      aCBSize.ISize(aWM) != NS_UNCONSTRAINEDSIZE,
      "have unconstrained inline-size; this should only result from "
      "very large sizes, not attempts at intrinsic inline-size "
      "calculation");
  NS_ASSERTION(aCBSize.ISize(aWM) >= 0, "inline-size less than zero");

  nscoord result = aSize.Resolve(aCBSize.ISize(aWM));
  // The result of a calc() expression might be less than 0; we
  // should clamp at runtime (below).  (Percentages and coords that
  // are less than 0 have already been dropped by the parser.)
  result -= aContentEdgeToBoxSizing.ISize(aWM);
  return std::max(0, result);
}

void nsIFrame::DidReflow(nsPresContext* aPresContext,
                         const ReflowInput* aReflowInput) {
  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS, ("nsIFrame::DidReflow"));

  if (IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
    RemoveStateBits(NS_FRAME_IN_REFLOW);
    return;
  }

  SVGObserverUtils::InvalidateDirectRenderingObservers(
      this, SVGObserverUtils::INVALIDATE_REFLOW);

  RemoveStateBits(NS_FRAME_IN_REFLOW | NS_FRAME_FIRST_REFLOW |
                  NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN);

  // Clear bits that were used in ReflowInput::InitResizeFlags (see
  // comment there for why we can't clear it there).
  SetHasBSizeChange(false);
  SetHasPaddingChange(false);

  // Notify the percent bsize observer if there is a percent bsize.
  // The observer may be able to initiate another reflow with a computed
  // bsize. This happens in the case where a table cell has no computed
  // bsize but can fabricate one when the cell bsize is known.
  if (aReflowInput && aReflowInput->mPercentBSizeObserver && !GetPrevInFlow()) {
    const auto bsize = aReflowInput->mStylePosition->BSize(
        aReflowInput->GetWritingMode(), aReflowInput->mStyleDisplay->mPosition);
    if (bsize->HasPercent()) {
      aReflowInput->mPercentBSizeObserver->NotifyPercentBSize(*aReflowInput);
    }
  }

  aPresContext->ReflowedFrame();
}

void nsIFrame::FinishReflowWithAbsoluteFrames(nsPresContext* aPresContext,
                                              ReflowOutput& aDesiredSize,
                                              const ReflowInput& aReflowInput,
                                              nsReflowStatus& aStatus,
                                              bool aConstrainBSize) {
  ReflowAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput, aStatus,
                       aConstrainBSize);

  FinishAndStoreOverflow(&aDesiredSize, aReflowInput.mStyleDisplay);
}

void nsIFrame::ReflowAbsoluteFrames(nsPresContext* aPresContext,
                                    ReflowOutput& aDesiredSize,
                                    const ReflowInput& aReflowInput,
                                    nsReflowStatus& aStatus,
                                    bool aConstrainBSize) {
  if (HasAbsolutelyPositionedChildren()) {
    nsAbsoluteContainingBlock* absoluteContainer = GetAbsoluteContainingBlock();

    // Let the absolutely positioned container reflow any absolutely positioned
    // child frames that need to be reflowed

    // The containing block for the abs pos kids is formed by our padding edge.
    nsMargin usedBorder = GetUsedBorder();
    nscoord containingBlockWidth =
        std::max(0, aDesiredSize.Width() - usedBorder.LeftRight());
    nscoord containingBlockHeight =
        std::max(0, aDesiredSize.Height() - usedBorder.TopBottom());
    nsContainerFrame* container = do_QueryFrame(this);
    NS_ASSERTION(container,
                 "Abs-pos children only supported on container frames for now");

    nsRect containingBlock(0, 0, containingBlockWidth, containingBlockHeight);
    AbsPosReflowFlags flags =
        AbsPosReflowFlags::CBWidthAndHeightChanged;  // XXX could be optimized
    if (aConstrainBSize) {
      flags |= AbsPosReflowFlags::ConstrainHeight;
    }
    absoluteContainer->Reflow(container, aPresContext, aReflowInput, aStatus,
                              containingBlock, flags,
                              &aDesiredSize.mOverflowAreas);
  }
}

/* virtual */
bool nsIFrame::CanContinueTextRun() const {
  // By default, a frame will *not* allow a text run to be continued
  // through it.
  return false;
}

void nsIFrame::Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
                      const ReflowInput& aReflowInput,
                      nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  aDesiredSize.ClearSize();
}

bool nsIFrame::IsContentDisabled() const {
  // FIXME(emilio): Doing this via CSS means callers must ensure the style is up
  // to date, and they don't!
  if (StyleUI()->UserInput() == StyleUserInput::None) {
    return true;
  }

  auto* element = nsGenericHTMLElement::FromNodeOrNull(GetContent());
  return element && element->IsDisabled();
}

bool nsIFrame::IsContentRelevant() const {
  MOZ_ASSERT(StyleDisplay()->ContentVisibility(*this) ==
             StyleContentVisibility::Auto);

  auto* element = Element::FromNodeOrNull(GetContent());
  MOZ_ASSERT(element);

  Maybe<ContentRelevancy> relevancy = element->GetContentRelevancy();
  return relevancy.isSome() && !relevancy->isEmpty();
}

bool nsIFrame::HidesContent(
    const EnumSet<IncludeContentVisibility>& aInclude) const {
  auto effectiveContentVisibility = StyleDisplay()->ContentVisibility(*this);
  if (aInclude.contains(IncludeContentVisibility::Hidden) &&
      effectiveContentVisibility == StyleContentVisibility::Hidden) {
    return true;
  }

  if (aInclude.contains(IncludeContentVisibility::Auto) &&
      effectiveContentVisibility == StyleContentVisibility::Auto) {
    return !IsContentRelevant();
  }

  return false;
}

bool nsIFrame::HidesContentForLayout() const {
  return HidesContent() && !PresShell()->IsForcingLayoutForHiddenContent(this);
}

bool nsIFrame::IsHiddenByContentVisibilityOfInFlowParentForLayout() const {
  const auto* parent = GetInFlowParent();
  // The anonymous children owned by parent are important for properly sizing
  // their parents.
  return parent && parent->HidesContentForLayout() &&
         !(parent->HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES) &&
           Style()->IsAnonBox());
}

nsIFrame* nsIFrame::GetClosestContentVisibilityAncestor(
    const EnumSet<IncludeContentVisibility>& aInclude) const {
  auto* parent = GetInFlowParent();
  bool isAnonymousBlock = Style()->IsAnonBox() && parent &&
                          parent->HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES);
  for (nsIFrame* cur = parent; cur; cur = cur->GetInFlowParent()) {
    if (!isAnonymousBlock && cur->HidesContent(aInclude)) {
      return cur;
    }

    // Anonymous boxes are not hidden by the content-visibility of their first
    // non-anonymous ancestor, but can be hidden by ancestors further up the
    // tree.
    isAnonymousBlock = false;
  }

  return nullptr;
}

static bool IsClosedDetailsSlot(const Element* aElement) {
  const auto* slot = HTMLSlotElement::FromNodeOrNull(aElement);
  if (!slot || slot->HasName()) {
    return false;
  }
  const auto* details =
      HTMLDetailsElement::FromNodeOrNull(slot->GetContainingShadowHost());
  return details && !details->GetBoolAttr(nsGkAtoms::open);
}

bool nsIFrame::IsHiddenUntilFoundOrClosedDetails() const {
  for (const auto* f = this; f; f = f->GetInFlowParent()) {
    if (f->HidesContent(nsIFrame::IncludeContentVisibility::Hidden)) {
      if (const auto* element = Element::FromNode(f->GetContent());
          element &&
          !element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::hidden,
                                nsGkAtoms::untilFound, eIgnoreCase) &&
          !IsClosedDetailsSlot(element)) {
        return false;
      }
    }
  }
  return true;
}

bool nsIFrame::IsHiddenByContentVisibilityOnAnyAncestor(
    const EnumSet<IncludeContentVisibility>& aInclude) const {
  return !!GetClosestContentVisibilityAncestor(aInclude);
}

bool nsIFrame::HasSelectionInSubtree() {
  if (IsSelected()) {
    return true;
  }

  RefPtr<nsFrameSelection> frameSelection = GetFrameSelection();
  if (!frameSelection) {
    return false;
  }

  const Selection& selection = frameSelection->NormalSelection();

  for (uint32_t i = 0; i < selection.RangeCount(); i++) {
    auto* range = selection.GetRangeAt(i);
    MOZ_ASSERT(range);

    const auto* commonAncestorNode =
        range->GetRegisteredClosestCommonInclusiveAncestor();
    if (commonAncestorNode &&
        commonAncestorNode->IsInclusiveDescendantOf(GetContent())) {
      return true;
    }
  }

  return false;
}

bool nsIFrame::UpdateIsRelevantContent(
    const ContentRelevancy& aRelevancyToUpdate) {
  MOZ_ASSERT(StyleDisplay()->ContentVisibility(*this) ==
             StyleContentVisibility::Auto);

  auto* element = Element::FromNodeOrNull(GetContent());
  MOZ_ASSERT(element);

  ContentRelevancy newRelevancy;
  Maybe<ContentRelevancy> oldRelevancy = element->GetContentRelevancy();
  if (oldRelevancy.isSome()) {
    newRelevancy = *oldRelevancy;
  }

  auto setRelevancyValue = [&](ContentRelevancyReason reason, bool value) {
    if (value) {
      newRelevancy += reason;
    } else {
      newRelevancy -= reason;
    }
  };

  if (!oldRelevancy ||
      aRelevancyToUpdate.contains(ContentRelevancyReason::Visible)) {
    Maybe<bool> visible = element->GetVisibleForContentVisibility();
    if (visible.isSome()) {
      setRelevancyValue(ContentRelevancyReason::Visible, *visible);
    }
  }

  if (!oldRelevancy ||
      aRelevancyToUpdate.contains(ContentRelevancyReason::FocusInSubtree)) {
    setRelevancyValue(ContentRelevancyReason::FocusInSubtree,
                      element->State().HasAtLeastOneOfStates(
                          ElementState::FOCUS_WITHIN | ElementState::FOCUS));
  }

  if (!oldRelevancy ||
      aRelevancyToUpdate.contains(ContentRelevancyReason::Selected)) {
    setRelevancyValue(ContentRelevancyReason::Selected,
                      HasSelectionInSubtree());
  }

  // If the proximity to the viewport has not been determined yet,
  // and neither the element nor its contents are focused or selected,
  // we should wait for the determination of the proximity. Otherwise,
  // there might be a redundant contentvisibilityautostatechange event.
  // See https://github.com/w3c/csswg-drafts/issues/9803
  bool isProximityToViewportDetermined =
      oldRelevancy ? true : element->GetVisibleForContentVisibility().isSome();
  if (!isProximityToViewportDetermined && newRelevancy.isEmpty()) {
    return false;
  }

  bool overallRelevancyChanged =
      !oldRelevancy || oldRelevancy->isEmpty() != newRelevancy.isEmpty();
  if (!oldRelevancy || *oldRelevancy != newRelevancy) {
    element->SetContentRelevancy(newRelevancy);
  }

  if (!overallRelevancyChanged) {
    return false;
  }

  HandleLastRememberedSize();
  PresContext()->SetNeedsToUpdateHiddenByContentVisibilityForAnimations();
  PresShell()->FrameNeedsReflow(
      this, IntrinsicDirty::FrameAncestorsAndDescendants, NS_FRAME_IS_DIRTY);
  InvalidateFrame();

  ContentVisibilityAutoStateChangeEventInit init;
  init.mSkipped = newRelevancy.isEmpty();
  RefPtr<ContentVisibilityAutoStateChangeEvent> event =
      ContentVisibilityAutoStateChangeEvent::Constructor(
          element, u"contentvisibilityautostatechange"_ns, init);

  // Per
  // https://drafts.csswg.org/css-contain/#content-visibility-auto-state-changed
  // "This event is dispatched by posting a task at the time when the state
  // change occurs."
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(element, event.forget());
  DebugOnly<nsresult> rv = asyncDispatcher->PostDOMEvent();
  NS_ASSERTION(NS_SUCCEEDED(rv), "AsyncEventDispatcher failed to dispatch");
  return true;
}

nsresult nsIFrame::CharacterDataChanged(const CharacterDataChangeInfo&) {
  MOZ_ASSERT_UNREACHABLE("should only be called for text frames");
  return NS_OK;
}

nsresult nsIFrame::AttributeChanged(int32_t aNameSpaceID, nsAtom* aAttribute,
                                    int32_t aModType) {
  return NS_OK;
}

nsIFrame* nsIFrame::GetPrevContinuation() const { return nullptr; }

void nsIFrame::SetPrevContinuation(nsIFrame*) {
  MOZ_ASSERT_UNREACHABLE("Not splittable!");
}

nsIFrame* nsIFrame::GetNextContinuation() const { return nullptr; }

void nsIFrame::SetNextContinuation(nsIFrame*) {
  MOZ_ASSERT_UNREACHABLE("Not splittable!");
}

nsIFrame* nsIFrame::GetPrevInFlow() const { return nullptr; }

void nsIFrame::SetPrevInFlow(nsIFrame*) {
  MOZ_ASSERT_UNREACHABLE("Not splittable!");
}

nsIFrame* nsIFrame::GetNextInFlow() const { return nullptr; }

void nsIFrame::SetNextInFlow(nsIFrame*) {
  MOZ_ASSERT_UNREACHABLE("Not splittable!");
}

nsIFrame* nsIFrame::GetTailContinuation() {
  nsIFrame* frame = this;
  while (frame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
    frame = frame->GetPrevContinuation();
    NS_ASSERTION(frame, "first continuation can't be overflow container");
  }
  for (nsIFrame* next = frame->GetNextContinuation();
       next && !next->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
       next = frame->GetNextContinuation()) {
    frame = next;
  }

  MOZ_ASSERT(frame, "illegal state in continuation chain.");
  return frame;
}

// Associated view object
void nsIFrame::SetView(nsView* aView) {
  if (aView) {
    aView->SetFrame(this);

#ifdef DEBUG
    LayoutFrameType frameType = Type();
    NS_ASSERTION(frameType == LayoutFrameType::SubDocument ||
                     frameType == LayoutFrameType::Viewport ||
                     frameType == LayoutFrameType::MenuPopup,
                 "Only specific frame types can have an nsView");
#endif

    // Store the view on the frame.
    SetViewInternal(aView);

    // Set the frame state bit that says the frame has a view
    AddStateBits(NS_FRAME_HAS_VIEW);

    // Let all of the ancestors know they have a descendant with a view.
    for (nsIFrame* f = GetParent();
         f && !f->HasAnyStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
         f = f->GetParent()) {
      f->AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("Destroying a view while the frame is alive?");
    RemoveStateBits(NS_FRAME_HAS_VIEW);
    SetViewInternal(nullptr);
  }
}

template <nsPoint (nsIFrame::*PositionGetter)() const>
static nsPoint OffsetCalculator(const nsIFrame* aThis, const nsIFrame* aOther) {
  MOZ_ASSERT(aOther, "Must have frame for destination coordinate system!");

  NS_ASSERTION(aThis->PresContext() == aOther->PresContext(),
               "GetOffsetTo called on frames in different documents");

  nsPoint offset(0, 0);
  const nsIFrame* f;
  for (f = aThis; f != aOther && f; f = f->GetParent()) {
    offset += (f->*PositionGetter)();
  }

  if (f != aOther) {
    // Looks like aOther wasn't an ancestor of |this|.  So now we have
    // the root-frame-relative position of |this| in |offset|.  Convert back
    // to the coordinates of aOther
    while (aOther) {
      offset -= (aOther->*PositionGetter)();
      aOther = aOther->GetParent();
    }
  }

  return offset;
}

nsPoint nsIFrame::GetOffsetTo(const nsIFrame* aOther) const {
  return OffsetCalculator<&nsIFrame::GetPosition>(this, aOther);
}

nsPoint nsIFrame::GetOffsetToIgnoringScrolling(const nsIFrame* aOther) const {
  return OffsetCalculator<&nsIFrame::GetPositionIgnoringScrolling>(this,
                                                                   aOther);
}

nsPoint nsIFrame::GetOffsetToCrossDoc(const nsIFrame* aOther) const {
  return GetOffsetToCrossDoc(aOther, PresContext()->AppUnitsPerDevPixel());
}

nsPoint nsIFrame::GetOffsetToCrossDoc(const nsIFrame* aOther,
                                      const int32_t aAPD) const {
  MOZ_ASSERT(aOther, "Must have frame for destination coordinate system!");
  MOZ_DIAGNOSTIC_ASSERT(
      PresContext()->GetRootPresContext() ==
          aOther->PresContext()->GetRootPresContext(),
      "trying to get the offset between frames in different document "
      "hierarchies?");

  const nsIFrame* root = nullptr;
  // offset will hold the final offset
  // docOffset holds the currently accumulated offset at the current APD, it
  // will be converted and added to offset when the current APD changes.
  nsPoint offset(0, 0), docOffset(0, 0);
  const nsIFrame* f = this;
  int32_t currAPD = PresContext()->AppUnitsPerDevPixel();
  while (f && f != aOther) {
    docOffset += f->GetPosition();
    nsIFrame* parent = f->GetParent();
    if (parent) {
      f = parent;
    } else {
      nsPoint newOffset(0, 0);
      root = f;
      f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f, &newOffset);
      int32_t newAPD = f ? f->PresContext()->AppUnitsPerDevPixel() : 0;
      if (!f || newAPD != currAPD) {
        // Convert docOffset to the right APD and add it to offset.
        offset += docOffset.ScaleToOtherAppUnits(currAPD, aAPD);
        docOffset.x = docOffset.y = 0;
      }
      currAPD = newAPD;
      docOffset += newOffset;
    }
  }
  if (f == aOther) {
    offset += docOffset.ScaleToOtherAppUnits(currAPD, aAPD);
  } else {
    // Looks like aOther wasn't an ancestor of |this|.  So now we have
    // the root-document-relative position of |this| in |offset|. Subtract the
    // root-document-relative position of |aOther| from |offset|.
    // This call won't try to recurse again because root is an ancestor of
    // aOther.
    nsPoint negOffset = aOther->GetOffsetToCrossDoc(root, aAPD);
    offset -= negOffset;
  }

  return offset;
}

CSSIntRect nsIFrame::GetScreenRect() const {
  return CSSIntRect::FromAppUnitsToNearest(GetScreenRectInAppUnits());
}

nsRect nsIFrame::GetScreenRectInAppUnits() const {
  nsPresContext* presContext = PresContext();
  nsIFrame* rootFrame = presContext->PresShell()->GetRootFrame();
  nsPoint rootScreenPos(0, 0);
  nsPoint rootFrameOffsetInParent(0, 0);
  nsIFrame* rootFrameParent = nsLayoutUtils::GetCrossDocParentFrameInProcess(
      rootFrame, &rootFrameOffsetInParent);
  if (rootFrameParent) {
    nsRect parentScreenRectAppUnits =
        rootFrameParent->GetScreenRectInAppUnits();
    nsPresContext* parentPresContext = rootFrameParent->PresContext();
    double parentScale = double(presContext->AppUnitsPerDevPixel()) /
                         parentPresContext->AppUnitsPerDevPixel();
    nsPoint rootPt =
        parentScreenRectAppUnits.TopLeft() + rootFrameOffsetInParent;
    rootScreenPos.x = NS_round(parentScale * rootPt.x);
    rootScreenPos.y = NS_round(parentScale * rootPt.y);
  } else {
    nsCOMPtr<nsIWidget> rootWidget =
        presContext->PresShell()->GetViewManager()->GetRootWidget();
    if (rootWidget) {
      LayoutDeviceIntPoint rootDevPx = rootWidget->WidgetToScreenOffset();
      rootScreenPos.x = presContext->DevPixelsToAppUnits(rootDevPx.x);
      rootScreenPos.y = presContext->DevPixelsToAppUnits(rootDevPx.y);
    }
  }

  return nsRect(rootScreenPos + GetOffsetTo(rootFrame), GetSize());
}

// Returns the offset from this frame to the closest geometric parent that
// has a view. Also returns the containing view or null in case of error
void nsIFrame::GetOffsetFromView(nsPoint& aOffset, nsView** aView) const {
  MOZ_ASSERT(nullptr != aView, "null OUT parameter pointer");
  nsIFrame* frame = const_cast<nsIFrame*>(this);

  *aView = nullptr;
  aOffset.MoveTo(0, 0);
  do {
    aOffset += frame->GetPosition();
    frame = frame->GetParent();
  } while (frame && !frame->HasView());

  if (frame) {
    *aView = frame->GetView();
  }
}

nsIWidget* nsIFrame::GetNearestWidget() const {
  return GetClosestView()->GetNearestWidget(nullptr);
}

nsIWidget* nsIFrame::GetNearestWidget(nsPoint& aOffset) const {
  nsPoint offsetToView;
  nsPoint offsetToWidget;
  nsIWidget* widget =
      GetClosestView(&offsetToView)->GetNearestWidget(&offsetToWidget);
  aOffset = offsetToView + offsetToWidget;
  return widget;
}

Matrix4x4Flagged nsIFrame::GetTransformMatrix(ViewportType aViewportType,
                                              RelativeTo aStopAtAncestor,
                                              nsIFrame** aOutAncestor,
                                              uint32_t aFlags) const {
  MOZ_ASSERT(aOutAncestor, "Need a place to put the ancestor!");

  /* If we're transformed, we want to hand back the combination
   * transform/translate matrix that will apply our current transform, then
   * shift us to our parent.
   */
  const bool isTransformed = IsTransformed();
  const nsIFrame* zoomedContentRoot = nullptr;
  if (aStopAtAncestor.mViewportType == ViewportType::Visual) {
    zoomedContentRoot = ViewportUtils::IsZoomedContentRoot(this);
    if (zoomedContentRoot) {
      MOZ_ASSERT(aViewportType != ViewportType::Visual);
    }
  }

  if (isTransformed || zoomedContentRoot) {
    MOZ_ASSERT(GetParent());
    Matrix4x4Flagged result;
    int32_t scaleFactor =
        ((aFlags & IN_CSS_UNITS) ? AppUnitsPerCSSPixel()
                                 : PresContext()->AppUnitsPerDevPixel());

    /* Compute the delta to the parent, which we need because we are converting
     * coordinates to our parent.
     */
    if (isTransformed) {
      // Note: this converts from Matrix4x4 to Matrix4x4Flagged.
      result = nsDisplayTransform::GetResultingTransformMatrix(
          this, nsPoint(), scaleFactor,
          nsDisplayTransform::INCLUDE_PERSPECTIVE);
    }

    // The offset from a zoomed content root to its parent (e.g. from
    // a canvas frame to a scroll frame) is in layout coordinates, so
    // apply it before applying any layout-to-visual transform.
    *aOutAncestor = GetParent();
    nsPoint delta = GetPosition();
    /* Combine the raw transform with a translation to our parent. */
    result.PostTranslate(NSAppUnitsToFloatPixels(delta.x, scaleFactor),
                         NSAppUnitsToFloatPixels(delta.y, scaleFactor), 0.0f);

    if (zoomedContentRoot) {
      Matrix4x4Flagged layoutToVisual;
      if (aFlags & nsIFrame::IN_CSS_UNITS) {
        layoutToVisual = ViewportUtils::GetVisualToLayoutTransform(
                             zoomedContentRoot->GetContent())
                             .Inverse()
                             .ToUnknownMatrix();
      } else {
        layoutToVisual =
            ViewportUtils::GetVisualToLayoutTransform<LayoutDevicePixel>(
                zoomedContentRoot->GetContent())
                .Inverse()
                .ToUnknownMatrix();
      }
      result = result * layoutToVisual;
    }

    return result;
  }

  // We are not transformed, so the returned transform is just going to be a
  // translation up to whatever ancestor we decide to stop at.

  nsPoint crossdocOffset;
  *aOutAncestor =
      nsLayoutUtils::GetCrossDocParentFrameInProcess(this, &crossdocOffset);

  /* Otherwise, we're not transformed.  In that case, we'll walk up the frame
   * tree until we either hit the root frame or something that may be
   * transformed.  We'll then change coordinates into that frame, since we're
   * guaranteed that nothing in-between can be transformed.  First, however,
   * we have to check to see if we have a parent.  If not, we'll set the
   * outparam to null (indicating that there's nothing left) and will hand back
   * the identity matrix.
   */
  if (!*aOutAncestor) {
    return Matrix4x4Flagged();
  }

  /* Keep iterating while the frame can't possibly be transformed. */
  const nsIFrame* current = this;
  auto shouldStopAt = [](const nsIFrame* aCurrent, RelativeTo& aStopAtAncestor,
                         nsIFrame* aOutAncestor, uint32_t aFlags) {
    return aOutAncestor->IsTransformed() ||
           ((aStopAtAncestor.mViewportType == ViewportType::Visual) &&
            ViewportUtils::IsZoomedContentRoot(aOutAncestor)) ||
           ((aFlags & STOP_AT_STACKING_CONTEXT_AND_DISPLAY_PORT) &&
            (aOutAncestor->IsStackingContext() ||
             DisplayPortUtils::FrameHasDisplayPort(aOutAncestor, aCurrent)));
  };

  // We run the GetOffsetToCrossDoc code here as an optimization, instead of
  // walking the parent chain here and then asking GetOffsetToCrossDoc to walk
  // the same parent chain and compute the offset.
  const int32_t finalAPD = PresContext()->AppUnitsPerDevPixel();
  // offset accumulates the offset at finalAPD.
  nsPoint offset = GetPosition();

  int32_t currAPD = (*aOutAncestor)->PresContext()->AppUnitsPerDevPixel();
  // docOffset accumulates the current offset at currAPD, and then flushes to
  // offset at finalAPD when the APD changes or we finish.
  nsPoint docOffset = crossdocOffset;
  MOZ_ASSERT(crossdocOffset == nsPoint(0, 0) || !GetParent());

  while (*aOutAncestor != aStopAtAncestor.mFrame &&
         !shouldStopAt(current, aStopAtAncestor, *aOutAncestor, aFlags)) {
    docOffset += (*aOutAncestor)->GetPosition();

    nsIFrame* parent = (*aOutAncestor)->GetParent();
    if (!parent) {
      crossdocOffset.x = crossdocOffset.y = 0;
      parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(*aOutAncestor,
                                                              &crossdocOffset);

      int32_t newAPD =
          parent ? parent->PresContext()->AppUnitsPerDevPixel() : currAPD;
      if (!parent || newAPD != currAPD) {
        // Convert docOffset to finalAPD and add it to offset.
        offset += docOffset.ScaleToOtherAppUnits(currAPD, finalAPD);
        docOffset.x = docOffset.y = 0;
      }
      currAPD = newAPD;
      docOffset += crossdocOffset;

      if (!parent) {
        break;
      }
    }

    current = *aOutAncestor;
    *aOutAncestor = parent;
  }
  offset += docOffset.ScaleToOtherAppUnits(currAPD, finalAPD);

  NS_ASSERTION(*aOutAncestor, "Somehow ended up with a null ancestor...?");

  int32_t scaleFactor =
      ((aFlags & IN_CSS_UNITS) ? AppUnitsPerCSSPixel()
                               : PresContext()->AppUnitsPerDevPixel());
  return Matrix4x4Flagged::Translation2d(
      NSAppUnitsToFloatPixels(offset.x, scaleFactor),
      NSAppUnitsToFloatPixels(offset.y, scaleFactor));
}

static void InvalidateRenderingObservers(nsIFrame* aDisplayRoot,
                                         nsIFrame* aFrame,
                                         bool aFrameChanged = true) {
  MOZ_ASSERT(aDisplayRoot == nsLayoutUtils::GetDisplayRootFrame(aFrame));
  SVGObserverUtils::InvalidateDirectRenderingObservers(aFrame);
  nsIFrame* parent = aFrame;
  while (parent != aDisplayRoot &&
         (parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(parent)) &&
         !parent->HasAnyStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT)) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(parent);
  }

  if (!aFrameChanged) {
    return;
  }

  aFrame->MarkNeedsDisplayItemRebuild();
}

static void SchedulePaintInternal(
    nsIFrame* aDisplayRoot, nsIFrame* aFrame,
    nsIFrame::PaintType aType = nsIFrame::PAINT_DEFAULT) {
  MOZ_ASSERT(aDisplayRoot == nsLayoutUtils::GetDisplayRootFrame(aFrame));
  nsPresContext* pres = aDisplayRoot->PresContext()->GetRootPresContext();

  // No need to schedule a paint for an external document since they aren't
  // painted directly.
  if (!pres || (pres->Document() && pres->Document()->IsResourceDoc())) {
    return;
  }
  if (!pres->GetContainerWeak()) {
    NS_WARNING("Shouldn't call SchedulePaint in a detached pres context");
    return;
  }

  pres->PresShell()->SchedulePaint();

  if (aType == nsIFrame::PAINT_DEFAULT) {
    aDisplayRoot->AddStateBits(NS_FRAME_UPDATE_LAYER_TREE);
  }
}

static void InvalidateFrameInternal(nsIFrame* aFrame, bool aHasDisplayItem,
                                    bool aRebuildDisplayItems) {
  if (aHasDisplayItem) {
    aFrame->AddStateBits(NS_FRAME_NEEDS_PAINT);
  }

  if (aRebuildDisplayItems) {
    aFrame->MarkNeedsDisplayItemRebuild();
  }
  SVGObserverUtils::InvalidateDirectRenderingObservers(aFrame);
  bool needsSchedulePaint = false;
  if (nsLayoutUtils::IsPopup(aFrame)) {
    needsSchedulePaint = true;
  } else {
    nsIFrame* parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(aFrame);
    while (parent &&
           !parent->HasAnyStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT)) {
      if (aHasDisplayItem && !parent->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
        parent->AddStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT);
      }
      SVGObserverUtils::InvalidateDirectRenderingObservers(parent);

      // If we're inside a popup, then we need to make sure that we
      // call schedule paint so that the NS_FRAME_UPDATE_LAYER_TREE
      // flag gets added to the popup display root frame.
      if (nsLayoutUtils::IsPopup(parent)) {
        needsSchedulePaint = true;
        break;
      }
      parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(parent);
    }
    if (!parent) {
      needsSchedulePaint = true;
    }
  }
  if (!aHasDisplayItem) {
    return;
  }
  if (needsSchedulePaint) {
    nsIFrame* displayRoot = nsLayoutUtils::GetDisplayRootFrame(aFrame);
    SchedulePaintInternal(displayRoot, aFrame);
  }
  if (aFrame->HasAnyStateBits(NS_FRAME_HAS_INVALID_RECT)) {
    aFrame->RemoveProperty(nsIFrame::InvalidationRect());
    aFrame->RemoveStateBits(NS_FRAME_HAS_INVALID_RECT);
  }
}

void nsIFrame::InvalidateFrameSubtree(bool aRebuildDisplayItems /* = true */) {
  InvalidateFrame(0, aRebuildDisplayItems);

  if (HasAnyStateBits(NS_FRAME_ALL_DESCENDANTS_NEED_PAINT)) {
    return;
  }

  AddStateBits(NS_FRAME_ALL_DESCENDANTS_NEED_PAINT);

  for (const auto& childList : CrossDocChildLists()) {
    for (nsIFrame* child : childList.mList) {
      // Don't explicitly rebuild display items for our descendants,
      // since we should be marked and it implicitly includes all
      // descendants.
      child->InvalidateFrameSubtree(false);
    }
  }
}

void nsIFrame::ClearInvalidationStateBits() {
  if (HasAnyStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT)) {
    for (const auto& childList : CrossDocChildLists()) {
      for (nsIFrame* child : childList.mList) {
        child->ClearInvalidationStateBits();
      }
    }
  }

  RemoveStateBits(NS_FRAME_NEEDS_PAINT | NS_FRAME_DESCENDANT_NEEDS_PAINT |
                  NS_FRAME_ALL_DESCENDANTS_NEED_PAINT);
}

bool HasRetainedDataFor(const nsIFrame* aFrame, uint32_t aDisplayItemKey) {
  if (RefPtr<WebRenderUserData> data =
          GetWebRenderUserData<WebRenderFallbackData>(aFrame,
                                                      aDisplayItemKey)) {
    return true;
  }

  return false;
}

void nsIFrame::InvalidateFrame(uint32_t aDisplayItemKey,
                               bool aRebuildDisplayItems /* = true */) {
  bool hasDisplayItem =
      !aDisplayItemKey || HasRetainedDataFor(this, aDisplayItemKey);
  InvalidateFrameInternal(this, hasDisplayItem, aRebuildDisplayItems);
}

void nsIFrame::InvalidateFrameWithRect(const nsRect& aRect,
                                       uint32_t aDisplayItemKey,
                                       bool aRebuildDisplayItems /* = true */) {
  if (aRect.IsEmpty()) {
    return;
  }
  bool hasDisplayItem =
      !aDisplayItemKey || HasRetainedDataFor(this, aDisplayItemKey);
  bool alreadyInvalid = false;
  if (!HasAnyStateBits(NS_FRAME_NEEDS_PAINT)) {
    InvalidateFrameInternal(this, hasDisplayItem, aRebuildDisplayItems);
  } else {
    alreadyInvalid = true;
  }

  if (!hasDisplayItem) {
    return;
  }

  nsRect* rect;
  if (HasAnyStateBits(NS_FRAME_HAS_INVALID_RECT)) {
    rect = GetProperty(InvalidationRect());
    MOZ_ASSERT(rect);
  } else {
    if (alreadyInvalid) {
      return;
    }
    rect = new nsRect();
    AddProperty(InvalidationRect(), rect);
    AddStateBits(NS_FRAME_HAS_INVALID_RECT);
  }

  *rect = rect->Union(aRect);
}

bool nsIFrame::IsInvalid(nsRect& aRect) {
  if (!HasAnyStateBits(NS_FRAME_NEEDS_PAINT)) {
    return false;
  }

  if (HasAnyStateBits(NS_FRAME_HAS_INVALID_RECT)) {
    nsRect* rect = GetProperty(InvalidationRect());
    NS_ASSERTION(
        rect, "Must have an invalid rect if NS_FRAME_HAS_INVALID_RECT is set!");
    aRect = *rect;
  } else {
    aRect.SetEmpty();
  }
  return true;
}

void nsIFrame::SchedulePaint(PaintType aType, bool aFrameChanged) {
  if (PresShell()->IsPaintingSuppressed()) {
    // We can't have any display items yet, and when we unsuppress we will
    // invalidate the root frame.
    return;
  }
  nsIFrame* displayRoot = nsLayoutUtils::GetDisplayRootFrame(this);
  InvalidateRenderingObservers(displayRoot, this, aFrameChanged);
  SchedulePaintInternal(displayRoot, this, aType);
}

void nsIFrame::SchedulePaintWithoutInvalidatingObservers(PaintType aType) {
  nsIFrame* displayRoot = nsLayoutUtils::GetDisplayRootFrame(this);
  SchedulePaintInternal(displayRoot, this, aType);
}

void nsIFrame::InvalidateLayer(DisplayItemType aDisplayItemKey,
                               const nsIntRect* aDamageRect,
                               const nsRect* aFrameDamageRect,
                               uint32_t aFlags /* = 0 */) {
  NS_ASSERTION(aDisplayItemKey > DisplayItemType::TYPE_ZERO, "Need a key");

  nsIFrame* displayRoot = nsLayoutUtils::GetDisplayRootFrame(this);
  InvalidateRenderingObservers(displayRoot, this, false);

  // Check if frame supports WebRender's async update
  if ((aFlags & UPDATE_IS_ASYNC) &&
      WebRenderUserData::SupportsAsyncUpdate(this)) {
    // WebRender does not use layer, then return nullptr.
    return;
  }

  if (aFrameDamageRect && aFrameDamageRect->IsEmpty()) {
    return;
  }

  // In the bug 930056, dialer app startup but not shown on the
  // screen because sometimes we don't have any retainned data
  // for remote type displayitem and thus Repaint event is not
  // triggered. So, always invalidate in this case.
  DisplayItemType displayItemKey = aDisplayItemKey;
  if (aDisplayItemKey == DisplayItemType::TYPE_REMOTE) {
    displayItemKey = DisplayItemType::TYPE_ZERO;
  }

  if (aFrameDamageRect) {
    InvalidateFrameWithRect(*aFrameDamageRect,
                            static_cast<uint32_t>(displayItemKey));
  } else {
    InvalidateFrame(static_cast<uint32_t>(displayItemKey));
  }
}

static nsRect ComputeEffectsRect(nsIFrame* aFrame, const nsRect& aOverflowRect,
                                 const nsSize& aNewSize) {
  nsRect r = aOverflowRect;

  if (aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    // For SVG frames, we only need to account for filters.
    // TODO: We could also take account of clipPath and mask to reduce the
    // ink overflow, but that's not essential.
    if (aFrame->StyleEffects()->HasFilters()) {
      SetOrUpdateRectValuedProperty(aFrame, nsIFrame::PreEffectsBBoxProperty(),
                                    r);
      r = SVGUtils::GetPostFilterInkOverflowRect(aFrame, aOverflowRect);
    }
    return r;
  }

  // box-shadow
  r.UnionRect(r, nsLayoutUtils::GetBoxShadowRectForFrame(aFrame, aNewSize));

  // border-image-outset.
  // We need to include border-image-outset because it can cause the
  // border image to be drawn beyond the border box.

  // (1) It's important we not check whether there's a border-image
  //     since the style hint for a change in border image doesn't cause
  //     reflow, and that's probably more important than optimizing the
  //     overflow areas for the silly case of border-image-outset without
  //     border-image
  // (2) It's important that we not check whether the border-image
  //     is actually loaded, since that would require us to reflow when
  //     the image loads.
  const nsStyleBorder* styleBorder = aFrame->StyleBorder();
  nsMargin outsetMargin = styleBorder->GetImageOutset();

  if (outsetMargin != nsMargin(0, 0, 0, 0)) {
    nsRect outsetRect(nsPoint(0, 0), aNewSize);
    outsetRect.Inflate(outsetMargin);
    r.UnionRect(r, outsetRect);
  }

  // Note that we don't remove the outlineInnerRect if a frame loses outline
  // style. That would require an extra property lookup for every frame,
  // or a new frame state bit to track whether a property had been stored,
  // or something like that. It's not worth doing that here. At most it's
  // only one heap-allocated rect per frame and it will be cleaned up when
  // the frame dies.

  if (SVGIntegrationUtils::UsingOverflowAffectingEffects(aFrame)) {
    SetOrUpdateRectValuedProperty(aFrame, nsIFrame::PreEffectsBBoxProperty(),
                                  r);
    r = SVGIntegrationUtils::ComputePostEffectsInkOverflowRect(aFrame, r);
  }

  return r;
}

void nsIFrame::SetPosition(const nsPoint& aPt) {
  if (mRect.TopLeft() == aPt) {
    return;
  }
  mRect.MoveTo(aPt);
  MarkNeedsDisplayItemRebuild();
}

void nsIFrame::MovePositionBy(const nsPoint& aTranslation) {
  nsPoint position = GetNormalPosition() + aTranslation;

  const nsMargin* computedOffsets = nullptr;
  if (IsRelativelyOrStickyPositioned()) {
    computedOffsets = GetProperty(nsIFrame::ComputedOffsetProperty());
  }
  ReflowInput::ApplyRelativePositioning(
      this, computedOffsets ? *computedOffsets : nsMargin(), &position);
  SetPosition(position);
}

nsRect nsIFrame::GetNormalRect() const {
  // It might be faster to first check
  // StyleDisplay()->IsRelativelyPositionedStyle().
  bool hasProperty;
  nsPoint normalPosition = GetProperty(NormalPositionProperty(), &hasProperty);
  if (hasProperty) {
    return nsRect(normalPosition, GetSize());
  }
  return GetRect();
}

nsRect nsIFrame::GetBoundingClientRect() {
  return nsLayoutUtils::GetAllInFlowRectsUnion(
      this, nsLayoutUtils::GetContainingBlockForClientRect(this),
      nsLayoutUtils::GetAllInFlowRectsFlag::AccountForTransforms);
}

nsPoint nsIFrame::GetPositionIgnoringScrolling() const {
  return GetParent() ? GetParent()->GetPositionOfChildIgnoringScrolling(this)
                     : GetPosition();
}

nsRect nsIFrame::GetOverflowRect(OverflowType aType) const {
  // Note that in some cases the overflow area might not have been
  // updated (yet) to reflect any outline set on the frame or the area
  // of child frames. That's OK because any reflow that updates these
  // areas will invalidate the appropriate area, so any (mis)uses of
  // this method will be fixed up.

  if (mOverflow.mType == OverflowStorageType::Large) {
    // there is an overflow rect, and it's not stored as deltas but as
    // a separately-allocated rect
    return GetOverflowAreasProperty()->Overflow(aType);
  }

  if (aType == OverflowType::Ink &&
      mOverflow.mType != OverflowStorageType::None) {
    return InkOverflowFromDeltas();
  }

  return GetRectRelativeToSelf();
}

OverflowAreas nsIFrame::GetOverflowAreas() const {
  if (mOverflow.mType == OverflowStorageType::Large) {
    // there is an overflow rect, and it's not stored as deltas but as
    // a separately-allocated rect
    return *GetOverflowAreasProperty();
  }

  return OverflowAreas(InkOverflowFromDeltas(),
                       nsRect(nsPoint(0, 0), GetSize()));
}

OverflowAreas nsIFrame::GetOverflowAreasRelativeToSelf() const {
  if (IsTransformed()) {
    if (OverflowAreas* preTransformOverflows =
            GetProperty(PreTransformOverflowAreasProperty())) {
      return *preTransformOverflows;
    }
  }
  return GetOverflowAreas();
}

OverflowAreas nsIFrame::GetOverflowAreasRelativeToParent() const {
  return GetOverflowAreas() + GetPosition();
}

OverflowAreas nsIFrame::GetActualAndNormalOverflowAreasRelativeToParent()
    const {
  if (MOZ_LIKELY(!IsRelativelyOrStickyPositioned())) {
    return GetOverflowAreasRelativeToParent();
  }

  const OverflowAreas overflows = GetOverflowAreas();
  OverflowAreas actualAndNormalOverflows = overflows + GetNormalPosition();
  if (IsRelativelyPositioned()) {
    actualAndNormalOverflows.UnionWith(overflows + GetPosition());
  } else {
    // For sticky positioned elements, we only use the normal position for the
    // scrollable overflow. This avoids circular dependencies between sticky
    // positioned elements and their scroll container. (The scroll position and
    // the scroll container's size impact the sticky position, so we don't want
    // the sticky position to impact them.)
    MOZ_ASSERT(IsStickyPositioned());
    actualAndNormalOverflows.UnionWith(
        OverflowAreas(overflows.InkOverflow() + GetPosition(), nsRect()));
  }
  return actualAndNormalOverflows;
}

nsRect nsIFrame::ScrollableOverflowRectRelativeToParent() const {
  return ScrollableOverflowRect() + GetPosition();
}

nsRect nsIFrame::InkOverflowRectRelativeToParent() const {
  return InkOverflowRect() + GetPosition();
}

nsRect nsIFrame::ScrollableOverflowRectRelativeToSelf() const {
  if (IsTransformed()) {
    if (OverflowAreas* preTransformOverflows =
            GetProperty(PreTransformOverflowAreasProperty())) {
      return preTransformOverflows->ScrollableOverflow();
    }
  }
  return ScrollableOverflowRect();
}

nsRect nsIFrame::InkOverflowRectRelativeToSelf() const {
  if (IsTransformed()) {
    if (OverflowAreas* preTransformOverflows =
            GetProperty(PreTransformOverflowAreasProperty())) {
      return preTransformOverflows->InkOverflow();
    }
  }
  return InkOverflowRect();
}

nsRect nsIFrame::PreEffectsInkOverflowRect() const {
  nsRect* r = GetProperty(nsIFrame::PreEffectsBBoxProperty());
  return r ? *r : InkOverflowRectRelativeToSelf();
}

bool nsIFrame::UpdateOverflow() {
  MOZ_ASSERT(FrameMaintainsOverflow(),
             "Non-display SVG do not maintain ink overflow rects");

  nsRect rect(nsPoint(0, 0), GetSize());
  OverflowAreas overflowAreas(rect, rect);

  if (!ComputeCustomOverflow(overflowAreas)) {
    // If updating overflow wasn't supported by this frame, then it should
    // have scheduled any necessary reflows. We can return false to say nothing
    // changed, and wait for reflow to correct it.
    return false;
  }

  UnionChildOverflow(overflowAreas);

  if (FinishAndStoreOverflow(overflowAreas, GetSize())) {
    if (nsView* view = GetView()) {
      // Make sure the frame's view is properly sized.
      nsViewManager* vm = view->GetViewManager();
      vm->ResizeView(view, overflowAreas.InkOverflow());
    }

    return true;
  }

  // Frames that combine their 3d transform with their ancestors
  // only compute a pre-transform overflow rect, and then contribute
  // to the normal overflow rect of the preserve-3d root. Always return
  // true here so that we propagate changes up to the root for final
  // calculation.
  return Combines3DTransformWithAncestors();
}

/* virtual */
bool nsIFrame::ComputeCustomOverflow(OverflowAreas& aOverflowAreas) {
  return true;
}

bool nsIFrame::DoesClipChildrenInBothAxes() const {
  if (IsScrollContainerOrSubclass()) {
    return true;
  }
  const nsStyleDisplay* display = StyleDisplay();
  if (display->IsContainPaint() && SupportsContainLayoutAndPaint()) {
    return true;
  }
  return display->mOverflowX == StyleOverflow::Clip &&
         display->mOverflowY == StyleOverflow::Clip;
}

/* virtual */
void nsIFrame::UnionChildOverflow(OverflowAreas& aOverflowAreas,
                                  bool aAsIfScrolled) {
  if (aAsIfScrolled || !DoesClipChildrenInBothAxes()) {
    nsLayoutUtils::UnionChildOverflow(this, aOverflowAreas);
  }
}

// Return true if this form control element's preferred size property (but not
// percentage max size property) contains a percentage value that should be
// resolved against zero when calculating its min-content contribution in the
// corresponding axis.
//
// For proper replaced elements, the percentage value in both their max size
// property or preferred size property should be resolved against zero. This is
// handled in IsPercentageResolvedAgainstZero().
inline static bool FormControlShrinksForPercentSize(const nsIFrame* aFrame) {
  if (!aFrame->IsReplaced()) {
    // Quick test to reject most frames.
    return false;
  }

  switch (aFrame->Type()) {
    case LayoutFrameType::Meter:
    case LayoutFrameType::Progress:
    case LayoutFrameType::Range:
    case LayoutFrameType::TextInput:
    case LayoutFrameType::ColorControl:
    case LayoutFrameType::ComboboxControl:
    case LayoutFrameType::ListControl:
    case LayoutFrameType::CheckboxRadio:
    case LayoutFrameType::FileControl:
    case LayoutFrameType::ImageControl:
      return true;
    default:
      // Buttons (GfxButtonControl / HTMLButtonControl) don't have this
      // shrinking behavior.  (Note that color inputs do, even though they
      // inherit from button, so we can't use do_QueryFrame here.)
      return false;
  }
}

bool nsIFrame::IsPercentageResolvedAgainstZero(
    const StyleSize& aStyleSize, const StyleMaxSize& aStyleMaxSize) const {
  const bool sizeHasPercent = aStyleSize.HasPercent();
  return ((sizeHasPercent || aStyleMaxSize.HasPercent()) &&
          HasReplacedSizing()) ||
         (sizeHasPercent && FormControlShrinksForPercentSize(this));
}

// Summary of the Cyclic-Percentage Intrinsic Size Contribution Rules:
//
// Element Type         |       Replaced           |        Non-replaced
// Contribution Type    | min-content  max-content | min-content  max-content
// ---------------------------------------------------------------------------
// min size             | zero         zero        | zero         zero
// max & preferred size | zero         initial     | initial      initial
//
// https://drafts.csswg.org/css-sizing-3/#cyclic-percentage-contribution
bool nsIFrame::IsPercentageResolvedAgainstZero(const LengthPercentage& aSize,
                                               SizeProperty aProperty) const {
  // Early return to avoid calling the virtual function, IsFrameOfType().
  if (aProperty == SizeProperty::MinSize) {
    return true;
  }

  const bool hasPercentOnReplaced = aSize.HasPercent() && HasReplacedSizing();
  if (aProperty == SizeProperty::MaxSize) {
    return hasPercentOnReplaced;
  }

  MOZ_ASSERT(aProperty == SizeProperty::Size);
  return hasPercentOnReplaced ||
         (aSize.HasPercent() && FormControlShrinksForPercentSize(this));
}

bool nsIFrame::IsBlockWrapper() const {
  auto pseudoType = Style()->GetPseudoType();
  return pseudoType == PseudoStyleType::mozBlockInsideInlineWrapper ||
         pseudoType == PseudoStyleType::buttonContent ||
         pseudoType == PseudoStyleType::cellContent ||
         pseudoType == PseudoStyleType::columnSpanWrapper;
}

bool nsIFrame::IsBlockFrameOrSubclass() const {
  const nsBlockFrame* thisAsBlock = do_QueryFrame(this);
  return !!thisAsBlock;
}

bool nsIFrame::IsImageFrameOrSubclass() const {
  const nsImageFrame* asImage = do_QueryFrame(this);
  return !!asImage;
}

bool nsIFrame::IsScrollContainerOrSubclass() const {
  const bool result = IsScrollContainerFrame() || IsListControlFrame();
  MOZ_ASSERT(result == !!QueryFrame(ScrollContainerFrame::kFrameIID));
  return result;
}

bool nsIFrame::IsSubgrid() const {
  return IsGridContainerFrame() &&
         static_cast<const nsGridContainerFrame*>(this)->IsSubgrid();
}

static nsIFrame* GetNearestBlockContainer(nsIFrame* frame) {
  while (!frame->IsBlockContainer()) {
    frame = frame->GetParent();
    NS_ASSERTION(
        frame,
        "How come we got to the root frame without seeing a containing block?");
  }
  return frame;
}

bool nsIFrame::IsBlockContainer() const {
  // The block wrappers we use to wrap blocks inside inlines aren't
  // described in the CSS spec.  We need to make them not be containing
  // blocks.
  // Since the parent of such a block is either a normal block or
  // another such pseudo, this shouldn't cause anything bad to happen.
  // Also the anonymous blocks inside table cells are not containing blocks.
  //
  // If we ever start skipping table row groups from being containing blocks,
  // you need to remove the StickyScrollContainer hack referencing bug 1421660.
  return !IsLineParticipant() && !IsBlockWrapper() && !IsSubgrid() &&
         // Table rows are not containing blocks either
         !IsTableRowFrame();
}

nsIFrame* nsIFrame::GetContainingBlock(
    uint32_t aFlags, const nsStyleDisplay* aStyleDisplay) const {
  MOZ_ASSERT(aStyleDisplay == StyleDisplay());

  // Keep this in sync with MightBeContainingBlockFor in ReflowInput.cpp.

  if (!GetParent()) {
    return nullptr;
  }
  // MathML frames might have absolute positioning style, but they would
  // still be in-flow.  So we have to check to make sure that the frame
  // is really out-of-flow too.
  nsIFrame* f;
  if (IsAbsolutelyPositioned(aStyleDisplay)) {
    f = GetParent();  // the parent is always the containing block
  } else {
    f = GetNearestBlockContainer(GetParent());
  }

  if (aFlags & SKIP_SCROLLED_FRAME && f &&
      f->Style()->GetPseudoType() == PseudoStyleType::scrolledContent) {
    f = f->GetParent();
  }
  return f;
}

#ifdef DEBUG_FRAME_DUMP

Maybe<uint32_t> nsIFrame::ContentIndexInContainer(const nsIFrame* aFrame) {
  if (nsIContent* content = aFrame->GetContent()) {
    return content->ComputeIndexInParentContent();
  }
  return Nothing();
}

nsAutoCString nsIFrame::ListTag(bool aListOnlyDeterministic) const {
  nsAutoString tmp;
  GetFrameName(tmp);

  nsAutoCString tag;
  tag += NS_ConvertUTF16toUTF8(tmp);
  ListPtr(tag, aListOnlyDeterministic, this, "@");
  return tag;
}

std::string nsIFrame::ConvertToString(const LogicalRect& aRect,
                                      const WritingMode aWM, ListFlags aFlags) {
  if (aFlags.contains(ListFlag::DisplayInCSSPixels)) {
    // Abuse CSSRect to store all LogicalRect's dimensions in CSS pixels.
    return ToString(mozilla::CSSRect(CSSPixel::FromAppUnits(aRect.IStart(aWM)),
                                     CSSPixel::FromAppUnits(aRect.BStart(aWM)),
                                     CSSPixel::FromAppUnits(aRect.ISize(aWM)),
                                     CSSPixel::FromAppUnits(aRect.BSize(aWM))));
  }
  return ToString(aRect);
}

std::string nsIFrame::ConvertToString(const LogicalSize& aSize,
                                      const WritingMode aWM, ListFlags aFlags) {
  if (aFlags.contains(ListFlag::DisplayInCSSPixels)) {
    // Abuse CSSSize to store all LogicalSize's dimensions in CSS pixels.
    return ToString(CSSSize(CSSPixel::FromAppUnits(aSize.ISize(aWM)),
                            CSSPixel::FromAppUnits(aSize.BSize(aWM))));
  }
  return ToString(aSize);
}

// Debugging
void nsIFrame::ListGeneric(nsACString& aTo, const char* aPrefix,
                           ListFlags aFlags) const {
  aTo += aPrefix;
  const bool onlyDeterministic =
      aFlags.contains(ListFlag::OnlyListDeterministicInfo);
  aTo += ListTag(onlyDeterministic);
  if (HasView()) {
    aTo += " [view";
    ListPtr(aTo, aFlags, GetView());
    aTo += "]";
  }
  if (!onlyDeterministic) {
    if (GetParent()) {
      aTo += nsPrintfCString(" parent=%p", static_cast<void*>(GetParent()));
    }
    if (GetNextSibling()) {
      aTo += nsPrintfCString(" next=%p", static_cast<void*>(GetNextSibling()));
    }
  }
  if (GetPrevContinuation()) {
    bool fluid = GetPrevInFlow() == GetPrevContinuation();
    aTo += nsPrintfCString(" prev-%s", fluid ? "in-flow" : "continuation");
    ListPtr(aTo, aFlags, GetPrevContinuation());
  }
  if (GetNextContinuation()) {
    bool fluid = GetNextInFlow() == GetNextContinuation();
    aTo += nsPrintfCString(" next-%s", fluid ? "in-flow" : "continuation");
    ListPtr(aTo, aFlags, GetNextContinuation());
  }
  if (const nsAtom* const autoPageValue =
          GetProperty(AutoPageValueProperty())) {
    aTo += " AutoPage=";
    aTo += nsAtomCString(autoPageValue);
  }
  if (const nsIFrame::PageValues* const pageValues =
          GetProperty(PageValuesProperty())) {
    aTo += " PageValues={";
    if (pageValues->mStartPageValue) {
      aTo += nsAtomCString(pageValues->mStartPageValue);
    } else {
      aTo += "<null>";
    }
    aTo += ", ";
    if (pageValues->mEndPageValue) {
      aTo += nsAtomCString(pageValues->mEndPageValue);
    } else {
      aTo += "<null>";
    }
    aTo += "}";
  }
  void* IBsibling = GetProperty(IBSplitSibling());
  if (IBsibling) {
    aTo += nsPrintfCString(" IBSplitSibling");
    ListPtr(aTo, aFlags, IBsibling);
  }
  void* IBprevsibling = GetProperty(IBSplitPrevSibling());
  if (IBprevsibling) {
    aTo += nsPrintfCString(" IBSplitPrevSibling");
    ListPtr(aTo, aFlags, IBprevsibling);
  }
  if (nsLayoutUtils::FontSizeInflationEnabled(PresContext())) {
    if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_FLOW_ROOT)) {
      aTo += nsPrintfCString(" FFR");
      if (nsFontInflationData* data =
              nsFontInflationData::FindFontInflationDataFor(this)) {
        aTo += nsPrintfCString(
            ",enabled=%s,UIS=%s", data->InflationEnabled() ? "yes" : "no",
            ConvertToString(data->UsableISize(), aFlags).c_str());
      }
    }
    if (HasAnyStateBits(NS_FRAME_FONT_INFLATION_CONTAINER)) {
      aTo += nsPrintfCString(" FIC");
    }
    aTo += nsPrintfCString(" FI=%f", nsLayoutUtils::FontSizeInflationFor(this));
  }
  aTo += nsPrintfCString(" %s", ConvertToString(mRect, aFlags).c_str());

  mozilla::WritingMode wm = GetWritingMode();
  if (wm.IsVertical() || wm.IsBidiRTL()) {
    aTo +=
        nsPrintfCString(" wm=%s logical-size=(%s)", ToString(wm).c_str(),
                        ConvertToString(GetLogicalSize(), wm, aFlags).c_str());
  }

  nsIFrame* parent = GetParent();
  if (parent) {
    WritingMode pWM = parent->GetWritingMode();
    if (pWM.IsVertical() || pWM.IsBidiRTL()) {
      nsSize containerSize = parent->mRect.Size();
      LogicalRect lr(pWM, mRect, containerSize);
      aTo += nsPrintfCString(" parent-wm=%s cs=(%s) logical-rect=%s",
                             ToString(pWM).c_str(),
                             ConvertToString(containerSize, aFlags).c_str(),
                             ConvertToString(lr, pWM, aFlags).c_str());
    }
  }
  nsIFrame* f = const_cast<nsIFrame*>(this);
  if (f->HasOverflowAreas()) {
    nsRect io = f->InkOverflowRect();
    if (!io.IsEqualEdges(mRect)) {
      aTo += nsPrintfCString(" ink-overflow=%s",
                             ConvertToString(io, aFlags).c_str());
    }
    nsRect so = f->ScrollableOverflowRect();
    if (!so.IsEqualEdges(mRect)) {
      aTo += nsPrintfCString(" scr-overflow=%s",
                             ConvertToString(so, aFlags).c_str());
    }
  }
  if (OverflowAreas* preTransformOverflows =
          f->GetProperty(PreTransformOverflowAreasProperty())) {
    nsRect io = preTransformOverflows->InkOverflow();
    if (!io.IsEqualEdges(mRect) &&
        (!f->HasOverflowAreas() || !io.IsEqualEdges(f->InkOverflowRect()))) {
      aTo += nsPrintfCString(" pre-transform-ink-overflow=%s",
                             ConvertToString(io, aFlags).c_str());
    }
    nsRect so = preTransformOverflows->ScrollableOverflow();
    if (!so.IsEqualEdges(mRect) &&
        (!f->HasOverflowAreas() ||
         !so.IsEqualEdges(f->ScrollableOverflowRect()))) {
      aTo += nsPrintfCString(" pre-transform-scr-overflow=%s",
                             ConvertToString(so, aFlags).c_str());
    }
  }
  bool hasNormalPosition;
  nsPoint normalPosition = GetNormalPosition(&hasNormalPosition);
  if (hasNormalPosition) {
    aTo += nsPrintfCString(" normal-position=%s",
                           ConvertToString(normalPosition, aFlags).c_str());
  }
  if (HasProperty(BidiDataProperty())) {
    FrameBidiData bidi = GetBidiData();
    aTo += nsPrintfCString(" bidi(%d,%d,%d)", bidi.baseLevel.Value(),
                           bidi.embeddingLevel.Value(),
                           bidi.precedingControl.Value());
  }
  if (IsTransformed()) {
    aTo += nsPrintfCString(" transformed");
  }
  if (ChildrenHavePerspective()) {
    aTo += nsPrintfCString(" perspective");
  }
  if (Extend3DContext()) {
    aTo += nsPrintfCString(" extend-3d");
  }
  if (Combines3DTransformWithAncestors()) {
    aTo += nsPrintfCString(" combines-3d-transform-with-ancestors");
  }
  if (mContent) {
    if (!onlyDeterministic) {
      aTo += nsPrintfCString(" [content=%p]", static_cast<void*>(mContent));
    }
    if (IsPrimaryFrame() && DisplayPortUtils::HasDisplayPort(mContent)) {
      // Anon boxes and continuations point to the same content - Just print on
      // primary frame.
      aTo += "[displayport]"_ns;
    }
  }
  if (!onlyDeterministic) {
    aTo += nsPrintfCString("[cs=%p]", static_cast<void*>(mComputedStyle));
  }
  if (mComputedStyle) {
    const auto pseudoType = mComputedStyle->GetPseudoType();
    const auto pseudoTypeStr = ToString(pseudoType);
    if (!pseudoTypeStr.empty()) {
      aTo += nsPrintfCString("[%s]", pseudoTypeStr.c_str());
    }
  }

  auto contentVisibility = StyleDisplay()->ContentVisibility(*this);
  if (contentVisibility != StyleContentVisibility::Visible) {
    aTo += nsPrintfCString(" [content-visibility=");
    if (contentVisibility == StyleContentVisibility::Auto) {
      aTo += "auto, "_ns;
    } else if (contentVisibility == StyleContentVisibility::Hidden) {
      aTo += "hidden, "_ns;
    }

    if (HidesContent()) {
      aTo += "HidesContent=hidden"_ns;
    } else {
      aTo += "HidesContent=visibile"_ns;
    }
    aTo += "]";
  }

  if (IsFrameModified()) {
    aTo += nsPrintfCString(" modified");
  }

  if (HasModifiedDescendants()) {
    aTo += nsPrintfCString(" has-modified-descendants");
  }
}

void nsIFrame::List(FILE* out, const char* aPrefix, ListFlags aFlags) const {
  nsCString str;
  ListGeneric(str, aPrefix, aFlags);
  fprintf_stderr(out, "%s\n", str.get());
}

void nsIFrame::ListTextRuns(FILE* out) const {
  nsTHashSet<const void*> seen;
  ListTextRuns(out, seen);
}

void nsIFrame::ListTextRuns(FILE* out, nsTHashSet<const void*>& aSeen) const {
  for (const auto& childList : ChildLists()) {
    for (const nsIFrame* kid : childList.mList) {
      kid->ListTextRuns(out, aSeen);
    }
  }
}

void nsIFrame::ListMatchedRules(FILE* out, const char* aPrefix) const {
  nsTArray<const StyleLockedDeclarationBlock*> rawDecls;
  Servo_ComputedValues_GetMatchingDeclarations(Style(), &rawDecls);
  for (const StyleLockedDeclarationBlock* rawRule : rawDecls) {
    nsAutoCString ruleText;
    Servo_DeclarationBlock_GetCssText(rawRule, &ruleText);
    fprintf_stderr(out, "%s%s\n", aPrefix, ruleText.get());
  }
}

void nsIFrame::ListWithMatchedRules(FILE* out, const char* aPrefix) const {
  fprintf_stderr(out, "%s%s\n", aPrefix, ListTag().get());

  nsCString rulePrefix;
  rulePrefix += aPrefix;
  rulePrefix += "    ";
  ListMatchedRules(out, rulePrefix.get());
}

nsresult nsIFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"Frame"_ns, aResult);
}

nsresult nsIFrame::MakeFrameName(const nsAString& aType,
                                 nsAString& aResult) const {
  aResult = aType;
  if (mContent && !mContent->IsText()) {
    nsAutoString buf;
    mContent->NodeInfo()->NameAtom()->ToString(buf);
    if (nsAtom* id = mContent->GetID()) {
      buf.AppendLiteral(" id=");
      buf.Append(nsDependentAtomString(id));
    }
    if (IsSubDocumentFrame()) {
      nsAutoString src;
      mContent->AsElement()->GetAttr(nsGkAtoms::src, src);
      buf.AppendLiteral(" src=");
      buf.Append(src);
    }
    aResult.Append('(');
    aResult.Append(buf);
    aResult.Append(')');
  }
  aResult.Append('(');
  Maybe<uint32_t> index = ContentIndexInContainer(this);
  if (index.isSome()) {
    aResult.AppendInt(*index);
  } else {
    aResult.AppendInt(-1);
  }
  aResult.Append(')');
  return NS_OK;
}

void nsIFrame::DumpFrameTree() const { DumpFrameTree(false); }

void nsIFrame::DumpFrameTree(bool aListOnlyDeterministic) const {
  ListFlags flags;
  if (aListOnlyDeterministic) {
    flags += ListFlag::OnlyListDeterministicInfo;
  }
  PresShell()->GetRootFrame()->List(stderr, "", flags);
}

void nsIFrame::DumpFrameTreeInCSSPixels() const {
  DumpFrameTreeInCSSPixels(false);
}

void nsIFrame::DumpFrameTreeInCSSPixels(bool aListOnlyDeterministic) const {
  ListFlags flags{ListFlag::DisplayInCSSPixels};
  if (aListOnlyDeterministic) {
    flags += ListFlag::OnlyListDeterministicInfo;
  }
  PresShell()->GetRootFrame()->List(stderr, "", flags);
}

void nsIFrame::DumpFrameTreeLimited() const { List(stderr); }
void nsIFrame::DumpFrameTreeLimitedInCSSPixels() const {
  List(stderr, "", ListFlag::DisplayInCSSPixels);
}

#endif

bool nsIFrame::IsVisibleForPainting() const {
  return StyleVisibility()->IsVisible();
}

bool nsIFrame::IsVisibleOrCollapsedForPainting() const {
  return StyleVisibility()->IsVisibleOrCollapsed();
}

/* virtual */
bool nsIFrame::IsEmpty() {
  return IsHiddenByContentVisibilityOfInFlowParentForLayout();
}

bool nsIFrame::CachedIsEmpty() {
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_DIRTY) ||
                 IsHiddenByContentVisibilityOfInFlowParentForLayout(),
             "Must only be called on reflowed lines or those hidden by "
             "content-visibility.");
  return IsEmpty();
}

/* virtual */
bool nsIFrame::IsSelfEmpty() {
  return IsHiddenByContentVisibilityOfInFlowParentForLayout();
}

nsresult nsIFrame::GetSelectionController(nsPresContext* aPresContext,
                                          nsISelectionController** aSelCon) {
  if (!aPresContext || !aSelCon) {
    return NS_ERROR_INVALID_ARG;
  }

  nsIFrame* frame = this;
  while (frame && frame->HasAnyStateBits(NS_FRAME_INDEPENDENT_SELECTION)) {
    if (nsTextControlFrame* tcf = do_QueryFrame(frame)) {
      return tcf->GetOwnedSelectionController(aSelCon);
    }
    frame = frame->GetParent();
  }

  *aSelCon = do_AddRef(aPresContext->PresShell()).take();
  return NS_OK;
}

already_AddRefed<nsFrameSelection> nsIFrame::GetFrameSelection() {
  RefPtr<nsFrameSelection> fs =
      const_cast<nsFrameSelection*>(GetConstFrameSelection());
  return fs.forget();
}

const nsFrameSelection* nsIFrame::GetConstFrameSelection() const {
  nsIFrame* frame = const_cast<nsIFrame*>(this);
  while (frame && frame->HasAnyStateBits(NS_FRAME_INDEPENDENT_SELECTION)) {
    if (nsTextControlFrame* tcf = do_QueryFrame(frame)) {
      return tcf->GetOwnedFrameSelection();
    }
    frame = frame->GetParent();
  }

  return PresShell()->ConstFrameSelection();
}

bool nsIFrame::IsFrameSelected() const {
  NS_ASSERTION(!GetContent() || GetContent()->IsMaybeSelected(),
               "use the public IsSelected() instead");
  if (StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()) {
    if (const ShadowRoot* shadowRoot =
            GetContent()->GetShadowRootForSelection()) {
      return shadowRoot->IsSelected(0, shadowRoot->GetChildCount());
    }
  }
  return GetContent()->IsSelected(0, GetContent()->GetChildCount());
}

nsresult nsIFrame::GetPointFromOffset(int32_t inOffset, nsPoint* outPoint) {
  MOZ_ASSERT(outPoint != nullptr, "Null parameter");
  nsRect contentRect = GetContentRectRelativeToSelf();
  nsPoint pt = contentRect.TopLeft();
  if (mContent) {
    nsIContent* newContent = mContent->GetParent();
    if (newContent) {
      const int32_t newOffset = newContent->ComputeIndexOf_Deprecated(mContent);

      // Find the direction of the frame from the EmbeddingLevelProperty,
      // which is the resolved bidi level set in
      // nsBidiPresUtils::ResolveParagraph (odd levels = right-to-left).
      // If the embedding level isn't set, just use the CSS direction
      // property.
      bool hasBidiData;
      FrameBidiData bidiData = GetProperty(BidiDataProperty(), &hasBidiData);
      bool isRTL = hasBidiData
                       ? bidiData.embeddingLevel.IsRTL()
                       : StyleVisibility()->mDirection == StyleDirection::Rtl;
      if ((!isRTL && inOffset > newOffset) ||
          (isRTL && inOffset <= newOffset)) {
        pt = contentRect.TopRight();
      }
    }
  }
  *outPoint = pt;
  return NS_OK;
}

nsresult nsIFrame::GetCharacterRectsInRange(int32_t aInOffset, int32_t aLength,
                                            nsTArray<nsRect>& aOutRect) {
  /* no text */
  return NS_ERROR_FAILURE;
}

nsresult nsIFrame::GetChildFrameContainingOffset(int32_t inContentOffset,
                                                 bool inHint,
                                                 int32_t* outFrameContentOffset,
                                                 nsIFrame** outChildFrame) {
  MOZ_ASSERT(outChildFrame && outFrameContentOffset, "Null parameter");
  *outFrameContentOffset = (int32_t)inHint;
  // the best frame to reflect any given offset would be a visible frame if
  // possible i.e. we are looking for a valid frame to place the blinking caret
  nsRect rect = GetRect();
  if (!rect.width || !rect.height) {
    // if we have a 0 width or height then lets look for another frame that
    // possibly has the same content.  If we have no frames in flow then just
    // let us return 'this' frame
    nsIFrame* nextFlow = GetNextInFlow();
    if (nextFlow) {
      return nextFlow->GetChildFrameContainingOffset(
          inContentOffset, inHint, outFrameContentOffset, outChildFrame);
    }
  }
  *outChildFrame = this;
  return NS_OK;
}

//
// What I've pieced together about this routine:
// Starting with a block frame (from which a line frame can be gotten)
// and a line number, drill down and get the first/last selectable
// frame on that line, depending on aPos->mDirection.
// aOutSideLimit != 0 means ignore aLineStart, instead work from
// the end (if > 0) or beginning (if < 0).
//
static nsresult GetNextPrevLineFromBlockFrame(PeekOffsetStruct* aPos,
                                              nsIFrame* aBlockFrame,
                                              int32_t aLineStart,
                                              int8_t aOutSideLimit) {
  MOZ_ASSERT(aPos);
  MOZ_ASSERT(aBlockFrame);

  nsPresContext* pc = aBlockFrame->PresContext();

  // magic numbers: aLineStart will be -1 for end of block, 0 will be start of
  // block.

  aPos->mResultFrame = nullptr;
  aPos->mResultContent = nullptr;
  aPos->mAttach = aPos->mDirection == eDirNext ? CaretAssociationHint::After
                                               : CaretAssociationHint::Before;

  AutoAssertNoDomMutations guard;
  nsILineIterator* it = aBlockFrame->GetLineIterator();
  if (!it) {
    return NS_ERROR_FAILURE;
  }
  int32_t searchingLine = aLineStart;
  int32_t countLines = it->GetNumLines();
  if (aOutSideLimit > 0) {  // start at end
    searchingLine = countLines;
  } else if (aOutSideLimit < 0) {  // start at beginning
    searchingLine = -1;            //"next" will be 0
  } else if ((aPos->mDirection == eDirPrevious && searchingLine == 0) ||
             (aPos->mDirection == eDirNext &&
              searchingLine >= (countLines - 1))) {
    // Not found.
    return NS_ERROR_FAILURE;
  }
  nsIFrame* resultFrame = nullptr;
  nsIFrame* farStoppingFrame = nullptr;  // we keep searching until we find a
                                         // "this" frame then we go to next line
  nsIFrame* nearStoppingFrame = nullptr;  // if we are backing up from edge,
                                          // stop here
  bool isBeforeFirstFrame, isAfterLastFrame;
  bool found = false;

  const bool forceInEditableRegion =
      aPos->mOptions.contains(PeekOffsetOption::ForceEditableRegion);
  while (!found) {
    if (aPos->mDirection == eDirPrevious) {
      searchingLine--;
    } else {
      searchingLine++;
    }
    if ((aPos->mDirection == eDirPrevious && searchingLine < 0) ||
        (aPos->mDirection == eDirNext && searchingLine >= countLines)) {
      // we need to jump to new block frame.
      return NS_ERROR_FAILURE;
    }
    {
      auto line = it->GetLine(searchingLine).unwrap();
      if (!line.mNumFramesOnLine) {
        continue;
      }
      nsIFrame* firstFrame = nullptr;
      nsIFrame* lastFrame = nullptr;
      nsIFrame* frame = line.mFirstFrameOnLine;
      int32_t i = line.mNumFramesOnLine;
      do {
        // If the caller wants a frame for a inclusive ancestor of the ancestor
        // limiter, ignore frames for outside the limiter.
        if (aPos->FrameContentIsInAncestorLimiter(frame)) {
          if (!firstFrame) {
            firstFrame = frame;
          }
          lastFrame = frame;
        }
        if (i == 1) {
          break;
        }
        frame = frame->GetNextSibling();
        if (!frame) {
          NS_ERROR("GetLine promised more frames than could be found");
          return NS_ERROR_FAILURE;
        }
      } while (--i);
      if (!lastFrame) {
        // If we're looking for an editable content frame, but all frames in the
        // line are not in the specified editing host, return error because we
        // must reach the editing host boundary.
        return NS_ERROR_FAILURE;
      }
      nsIFrame::GetLastLeaf(&lastFrame);

      if (aPos->mDirection == eDirNext) {
        nearStoppingFrame = firstFrame;
        farStoppingFrame = lastFrame;
      } else {
        nearStoppingFrame = lastFrame;
        farStoppingFrame = firstFrame;
      }
    }
    nsPoint offset;
    nsView* view;  // used for call of get offset from view
    aBlockFrame->GetOffsetFromView(offset, &view);
    nsPoint newDesiredPos =
        aPos->mDesiredCaretPos -
        offset;  // get desired position into blockframe coords
    // TODO: nsILineIterator::FindFrameAt should take optional editing host
    // parameter and if it's set, it should return the nearest editable frame
    // for the editing host when the frame at the desired position is not
    // editable.
    nsresult rv = it->FindFrameAt(searchingLine, newDesiredPos, &resultFrame,
                                  &isBeforeFirstFrame, &isAfterLastFrame);
    if (NS_FAILED(rv)) {
      continue;
    }

    if (resultFrame) {
      // If ancestor limiter is specified and we reached outside content of it,
      // return error because we reached its element boundary.
      if (!aPos->FrameContentIsInAncestorLimiter(resultFrame)) {
        return NS_ERROR_FAILURE;
      }
      // Check to see if this is ANOTHER blockframe inside the other one that
      // we should look inside.
      if (resultFrame->CanProvideLineIterator() &&
          IsRelevantBlockFrame(resultFrame)) {
        aPos->mResultFrame = resultFrame;
        return NS_OK;
      }
      // resultFrame is not a block frame
      Maybe<nsFrameIterator> frameIterator;
      frameIterator.emplace(
          pc, resultFrame, nsFrameIterator::Type::PostOrder,
          false,  // aVisual
          aPos->mOptions.contains(PeekOffsetOption::StopAtScroller),
          false,  // aFollowOOFs
          false   // aSkipPopupChecks
      );

      auto FoundValidFrame = [forceInEditableRegion, aPos](
                                 const nsIFrame::ContentOffsets& aOffsets,
                                 const nsIFrame* aFrame) {
        if (!aOffsets.content) {
          return false;
        }
        if (!aFrame->IsSelectable(nullptr)) {
          return false;
        }
        if (aPos->mAncestorLimiter &&
            !aOffsets.content->IsInclusiveDescendantOf(
                aPos->mAncestorLimiter)) {
          return false;
        }
        if (forceInEditableRegion && !aOffsets.content->IsEditable()) {
          return false;
        }
        return true;
      };

      nsIFrame* storeOldResultFrame = resultFrame;
      while (!found) {
        nsPoint point;
        nsRect tempRect = resultFrame->GetRect();
        nsPoint offset;
        nsView* view;  // used for call of get offset from view
        resultFrame->GetOffsetFromView(offset, &view);
        if (!view) {
          return NS_ERROR_FAILURE;
        }
        if (resultFrame->GetWritingMode().IsVertical()) {
          point.y = aPos->mDesiredCaretPos.y;
          point.x = tempRect.width + offset.x;
        } else {
          point.y = tempRect.height + offset.y;
          point.x = aPos->mDesiredCaretPos.x;
        }

        if (!resultFrame->HasView()) {
          nsView* view;
          nsPoint offset;
          resultFrame->GetOffsetFromView(offset, &view);
          nsIFrame::ContentOffsets offsets =
              resultFrame->GetContentOffsetsFromPoint(
                  point - offset, nsIFrame::IGNORE_NATIVE_ANONYMOUS_SUBTREE);
          aPos->mResultContent = offsets.content;
          aPos->mContentOffset = offsets.offset;
          aPos->mAttach = offsets.associate;
          if (FoundValidFrame(offsets, resultFrame)) {
            found = true;
            break;
          }
        }

        if (aPos->mDirection == eDirPrevious &&
            resultFrame == farStoppingFrame) {
          break;
        }
        if (aPos->mDirection == eDirNext && resultFrame == nearStoppingFrame) {
          break;
        }
        // always try previous on THAT line if that fails go the other way
        resultFrame = frameIterator->Traverse(/* aForward = */ false);
        if (!resultFrame) {
          return NS_ERROR_FAILURE;
        }
      }

      if (!found) {
        resultFrame = storeOldResultFrame;
        frameIterator.reset();
        frameIterator.emplace(
            pc, resultFrame, nsFrameIterator::Type::Leaf,
            false,  // aVisual
            aPos->mOptions.contains(PeekOffsetOption::StopAtScroller),
            false,  // aFollowOOFs
            false   // aSkipPopupChecks
        );
        MOZ_ASSERT(frameIterator);
      }
      while (!found) {
        nsPoint point = aPos->mDesiredCaretPos;
        nsView* view;
        nsPoint offset;
        resultFrame->GetOffsetFromView(offset, &view);
        nsIFrame::ContentOffsets offsets =
            resultFrame->GetContentOffsetsFromPoint(
                point - offset, nsIFrame::IGNORE_NATIVE_ANONYMOUS_SUBTREE);
        aPos->mResultContent = offsets.content;
        aPos->mContentOffset = offsets.offset;
        aPos->mAttach = offsets.associate;
        if (FoundValidFrame(offsets, resultFrame)) {
          found = true;
          aPos->mAttach = resultFrame == farStoppingFrame
                              ? CaretAssociationHint::Before
                              : CaretAssociationHint::After;
          break;
        }
        if (aPos->mDirection == eDirPrevious &&
            resultFrame == nearStoppingFrame) {
          break;
        }
        if (aPos->mDirection == eDirNext && resultFrame == farStoppingFrame) {
          break;
        }
        // previous didnt work now we try "next"
        nsIFrame* tempFrame = frameIterator->Traverse(/* aForward = */ true);
        if (!tempFrame) {
          break;
        }
        resultFrame = tempFrame;
      }
      aPos->mResultFrame = resultFrame;
    } else {
      // we need to jump to new block frame.
      aPos->mAmount = eSelectLine;
      aPos->mStartOffset = 0;
      aPos->mAttach = aPos->mDirection == eDirNext
                          ? CaretAssociationHint::Before
                          : CaretAssociationHint::After;
      if (aPos->mDirection == eDirPrevious) {
        aPos->mStartOffset = -1;  // start from end
      }
      return aBlockFrame->PeekOffset(aPos);
    }
  }
  return NS_OK;
}

nsIFrame::CaretPosition nsIFrame::GetExtremeCaretPosition(bool aStart) {
  CaretPosition result;

  FrameTarget targetFrame = DrillDownToSelectionFrame(this, !aStart, 0);
  FrameContentRange range = GetRangeForFrame(targetFrame.frame);
  result.mResultContent = range.content;
  result.mContentOffset = aStart ? range.start : range.end;
  return result;
}

// If this is a preformatted text frame, see if it ends with a newline
static nsContentAndOffset FindLineBreakInText(nsIFrame* aFrame,
                                              nsDirection aDirection) {
  nsContentAndOffset result;

  if (aFrame->IsGeneratedContentFrame() ||
      !aFrame->HasSignificantTerminalNewline()) {
    return result;
  }

  int32_t endOffset = aFrame->GetOffsets().second;
  result.mContent = aFrame->GetContent();
  result.mOffset = endOffset - (aDirection == eDirPrevious ? 0 : 1);
  return result;
}

// Find the first (or last) descendant of the given frame
// which is either a block-level frame or a BRFrame, or some other kind of break
// which stops the line.
static nsContentAndOffset FindLineBreakingFrame(nsIFrame* aFrame,
                                                nsDirection aDirection) {
  nsContentAndOffset result;

  if (aFrame->IsGeneratedContentFrame()) {
    return result;
  }

  // Treat form controls and other replaced inline level elements as inline
  // leaves.
  if (aFrame->IsReplaced() && aFrame->IsInlineOutside() &&
      !aFrame->IsBrFrame() && !aFrame->IsTextFrame()) {
    return result;
  }

  // Check the frame itself
  // Fall through block-in-inline split frames because their mContent is
  // the content of the inline frames they were created from. The
  // first/last child of such frames is the real block frame we're
  // looking for.
  if ((IsRelevantBlockFrame(aFrame) &&
       !aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) ||
      aFrame->IsBrFrame()) {
    nsIContent* content = aFrame->GetContent();
    result.mContent = content->GetParent();
    // In some cases (bug 310589, bug 370174) we end up here with a null
    // content. This probably shouldn't ever happen, but since it sometimes
    // does, we want to avoid crashing here.
    NS_ASSERTION(result.mContent, "Unexpected orphan content");
    if (result.mContent) {
      result.mOffset = result.mContent->ComputeIndexOf_Deprecated(content) +
                       (aDirection == eDirPrevious ? 1 : 0);
    }
    return result;
  }

  result = FindLineBreakInText(aFrame, aDirection);
  if (result.mContent) {
    return result;
  }

  // Iterate over children and call ourselves recursively
  if (aDirection == eDirPrevious) {
    nsIFrame* child = aFrame->PrincipalChildList().LastChild();
    while (child && !result.mContent) {
      result = FindLineBreakingFrame(child, aDirection);
      child = child->GetPrevSibling();
    }
  } else {  // eDirNext
    nsIFrame* child = aFrame->PrincipalChildList().FirstChild();
    while (child && !result.mContent) {
      result = FindLineBreakingFrame(child, aDirection);
      child = child->GetNextSibling();
    }
  }
  return result;
}

nsresult nsIFrame::PeekOffsetForParagraph(PeekOffsetStruct* aPos) {
  nsIFrame* frame = this;
  nsContentAndOffset blockFrameOrBR;
  blockFrameOrBR.mContent = nullptr;
  bool reachedLimit = IsRelevantBlockFrame(frame) || IsEditingHost(frame);

  auto traverse = [&aPos](nsIFrame* current) {
    return aPos->mDirection == eDirPrevious ? current->GetPrevSibling()
                                            : current->GetNextSibling();
  };

  // Go through containing frames until reaching a block frame.
  // In each step, search the previous (or next) siblings for the closest
  // "stop frame" (a block frame or a BRFrame).
  // If found, set it to be the selection boundary and abort.
  while (!reachedLimit) {
    nsIFrame* parent = frame->GetParent();
    // Treat a frame associated with the root content as if it were a block
    // frame.
    if (!frame->mContent || !frame->mContent->GetParent()) {
      reachedLimit = true;
      break;
    }

    if (aPos->mDirection == eDirNext) {
      // Try to find our own line-break before looking at our siblings.
      blockFrameOrBR = FindLineBreakInText(frame, eDirNext);
    }

    nsIFrame* sibling = traverse(frame);
    while (sibling && !blockFrameOrBR.mContent) {
      blockFrameOrBR = FindLineBreakingFrame(sibling, aPos->mDirection);
      sibling = traverse(sibling);
    }
    if (blockFrameOrBR.mContent) {
      aPos->mResultContent = blockFrameOrBR.mContent;
      aPos->mContentOffset = blockFrameOrBR.mOffset;
      break;
    }
    frame = parent;
    reachedLimit =
        frame && (IsRelevantBlockFrame(frame) || IsEditingHost(frame));
  }

  if (reachedLimit) {  // no "stop frame" found
    aPos->mResultContent = frame->GetContent();
    if (aPos->mResultContent) {
      if (ShadowRoot* shadowRoot =
              aPos->mResultContent->GetShadowRootForSelection()) {
        // Even if there's no children for this node,
        // the elements inside the shadow root is still
        // selectable
        aPos->mResultContent = shadowRoot;
      }
    }
    if (aPos->mDirection == eDirPrevious) {
      aPos->mContentOffset = 0;
    } else if (aPos->mResultContent) {
      aPos->mContentOffset = aPos->mResultContent->GetChildCount();
    }
  }
  return NS_OK;
}

// Determine movement direction relative to frame
static bool IsMovingInFrameDirection(const nsIFrame* frame,
                                     nsDirection aDirection, bool aVisual) {
  bool isReverseDirection =
      aVisual && nsBidiPresUtils::IsReversedDirectionFrame(frame);
  return aDirection == (isReverseDirection ? eDirPrevious : eDirNext);
}

// Determines "are we looking for a boundary between whitespace and
// non-whitespace (in the direction we're moving in)". It is true when moving
// forward and looking for a beginning of a word, or when moving backwards and
// looking for an end of a word.
static bool ShouldWordSelectionEatSpace(const PeekOffsetStruct& aPos) {
  if (aPos.mWordMovementType != eDefaultBehavior) {
    // aPos->mWordMovementType possible values:
    //       eEndWord: eat the space if we're moving backwards
    //       eStartWord: eat the space if we're moving forwards
    return (aPos.mWordMovementType == eEndWord) ==
           (aPos.mDirection == eDirPrevious);
  }
  // Use the hidden preference which is based on operating system
  // behavior. This pref only affects whether moving forward by word
  // should go to the end of this word or start of the next word. When
  // going backwards, the start of the word is always used, on every
  // operating system.
  return aPos.mDirection == eDirNext &&
         StaticPrefs::layout_word_select_eat_space_to_next_word();
}

enum class OffsetIsAtLineEdge : bool { No, Yes };

static void SetPeekResultFromFrame(PeekOffsetStruct& aPos, nsIFrame* aFrame,
                                   int32_t aOffset,
                                   OffsetIsAtLineEdge aAtLineEdge) {
  FrameContentRange range = GetRangeForFrame(aFrame);
  aPos.mResultFrame = aFrame;
  aPos.mResultContent = range.content;
  // Output offset is relative to content, not frame
  aPos.mContentOffset =
      aOffset < 0 ? range.end + aOffset + 1 : range.start + aOffset;
  // Ensure we don't go past the range. This is important if aFrame is empty.
  aPos.mContentOffset = std::clamp(aPos.mContentOffset, range.start, range.end);
  if (aAtLineEdge == OffsetIsAtLineEdge::Yes) {
    aPos.mAttach = aPos.mContentOffset == range.start
                       ? CaretAssociationHint::After
                       : CaretAssociationHint::Before;
  }
}

void nsIFrame::SelectablePeekReport::TransferTo(PeekOffsetStruct& aPos) const {
  return SetPeekResultFromFrame(aPos, mFrame, mOffset, OffsetIsAtLineEdge::No);
}

nsIFrame::SelectablePeekReport::SelectablePeekReport(
    const mozilla::GenericErrorResult<nsresult>&& aErr) {
  MOZ_ASSERT(NS_FAILED(aErr.operator nsresult()));
  // Return an empty report
}

nsresult nsIFrame::PeekOffsetForCharacter(PeekOffsetStruct* aPos,
                                          int32_t aOffset) {
  SelectablePeekReport current{this, aOffset};

  nsIFrame::FrameSearchResult peekSearchState = CONTINUE;

  const bool forceEditableRegion =
      aPos->mOptions.contains(PeekOffsetOption::ForceEditableRegion);

  while (peekSearchState != FOUND) {
    const bool movingInFrameDirection = IsMovingInFrameDirection(
        current.mFrame, aPos->mDirection,
        aPos->mOptions.contains(PeekOffsetOption::Visual));

    if (current.mJumpedLine) {
      // If we jumped lines, it's as if we found a character, but we still need
      // to eat non-renderable content on the new line.
      peekSearchState = current.PeekOffsetNoAmount(movingInFrameDirection);
    } else {
      PeekOffsetCharacterOptions options;
      options.mRespectClusters = aPos->mAmount == eSelectCluster;
      peekSearchState =
          current.PeekOffsetCharacter(movingInFrameDirection, options);
      if (peekSearchState == FOUND && forceEditableRegion &&
          !current.mFrame->ContentIsEditable()) {
        // Treat non-editable content as unselectable.  Note that we may need to
        // set mJumpedLine propery even if it's not editable.  Therefore, we
        // cannot skip the above call.
        peekSearchState = CONTINUE_UNSELECTABLE;
      }
    }

    current.mMovedOverNonSelectableText |=
        peekSearchState == CONTINUE_UNSELECTABLE;

    if (peekSearchState != FOUND) {
      SelectablePeekReport next = current.mFrame->GetFrameFromDirection(*aPos);
      if (next.Failed()) {
        return NS_ERROR_FAILURE;
      }
      next.mJumpedLine |= current.mJumpedLine;
      next.mMovedOverNonSelectableText |= current.mMovedOverNonSelectableText;
      next.mHasSelectableFrame |= current.mHasSelectableFrame;
      current = next;
    }

    // Found frame, but because we moved over non selectable text we want
    // the offset to be at the frame edge. Note that if we are extending the
    // selection, this doesn't matter.
    if (peekSearchState == FOUND && current.mMovedOverNonSelectableText &&
        (!aPos->mOptions.contains(PeekOffsetOption::Extend) ||
         current.mHasSelectableFrame)) {
      auto [start, end] = current.mFrame->GetOffsets();
      current.mOffset = aPos->mDirection == eDirNext ? 0 : end - start;
    }
  }

  // Set outputs
  current.TransferTo(*aPos);
  // If we're dealing with a text frame and moving backward positions us at
  // the end of that line, decrease the offset by one to make sure that
  // we're placed before the linefeed character on the previous line.
  if (current.mOffset < 0 && current.mJumpedLine &&
      aPos->mDirection == eDirPrevious &&
      current.mFrame->HasSignificantTerminalNewline() &&
      !current.mIgnoredBrFrame) {
    --aPos->mContentOffset;
  }
  return NS_OK;
}

nsresult nsIFrame::PeekOffsetForWord(PeekOffsetStruct* aPos, int32_t aOffset) {
  SelectablePeekReport current{this, aOffset};
  bool shouldStopAtHardBreak =
      aPos->mWordMovementType == eDefaultBehavior &&
      StaticPrefs::layout_word_select_eat_space_to_next_word();
  bool wordSelectEatSpace = ShouldWordSelectionEatSpace(*aPos);

  PeekWordState state;
  while (true) {
    bool movingInFrameDirection = IsMovingInFrameDirection(
        current.mFrame, aPos->mDirection,
        aPos->mOptions.contains(PeekOffsetOption::Visual));

    FrameSearchResult searchResult = current.mFrame->PeekOffsetWord(
        movingInFrameDirection, wordSelectEatSpace,
        aPos->mOptions.contains(PeekOffsetOption::IsKeyboardSelect),
        &current.mOffset, &state,
        !aPos->mOptions.contains(PeekOffsetOption::PreserveSpaces));
    if (searchResult == FOUND) {
      break;
    }

    SelectablePeekReport next = [&]() {
      PeekOffsetOptions options = aPos->mOptions;
      if (state.mSawInlineCharacter) {
        // If we've already found a character, we don't want to stop at
        // placeholder frame boundary if there is in the word.
        options += PeekOffsetOption::StopAtPlaceholder;
      }
      return current.mFrame->GetFrameFromDirection(aPos->mDirection, options,
                                                   aPos->mAncestorLimiter);
    }();
    if (next.Failed()) {
      // If we've crossed the line boundary, check to make sure that we
      // have not consumed a trailing newline as whitespace if it's
      // significant.
      if (next.mJumpedLine && wordSelectEatSpace &&
          current.mFrame->HasSignificantTerminalNewline() &&
          current.mFrame->StyleText()->mWhiteSpaceCollapse !=
              StyleWhiteSpaceCollapse::PreserveBreaks) {
        current.mOffset -= 1;
      }
      break;
    }

    if ((next.mJumpedLine || next.mFoundPlaceholder) && !wordSelectEatSpace &&
        state.mSawBeforeType) {
      // We can't jump lines if we're looking for whitespace following
      // non-whitespace, and we already encountered non-whitespace.
      break;
    }

    if (shouldStopAtHardBreak && next.mJumpedHardBreak) {
      /**
       * Prev, always: Jump and stop right there
       * Next, saw inline: just stop
       * Next, no inline: Jump and consume whitespaces
       */
      if (aPos->mDirection == eDirPrevious) {
        // Try moving to the previous line if exists
        current.TransferTo(*aPos);
        current.mFrame->PeekOffsetForCharacter(aPos, current.mOffset);
        return NS_OK;
      }
      if (state.mSawInlineCharacter || current.mJumpedHardBreak) {
        if (current.mFrame->HasSignificantTerminalNewline()) {
          current.mOffset -= 1;
        }
        current.TransferTo(*aPos);
        return NS_OK;
      }
      // Mark the state as whitespace and continue
      state.Update(false, true);
    }

    if (next.mJumpedLine) {
      state.mContext.Truncate();
    }
    current = next;
    // Jumping a line is equivalent to encountering whitespace
    // This affects only when it already met an actual character
    if (wordSelectEatSpace && next.mJumpedLine) {
      state.SetSawBeforeType();
    }
  }

  // Set outputs
  current.TransferTo(*aPos);
  return NS_OK;
}

static nsIFrame* GetFirstSelectableDescendantWithLineIterator(
    const PeekOffsetStruct& aPeekOffsetStruct, nsIFrame* aParentFrame) {
  const bool forceEditableRegion = aPeekOffsetStruct.mOptions.contains(
      PeekOffsetOption::ForceEditableRegion);
  auto FoundValidFrame = [aPeekOffsetStruct,
                          forceEditableRegion](const nsIFrame* aFrame) {
    if (!aFrame->IsSelectable(nullptr)) {
      return false;
    }
    if (!aPeekOffsetStruct.FrameContentIsInAncestorLimiter(aFrame)) {
      return false;
    }
    if (forceEditableRegion && !aFrame->ContentIsEditable()) {
      return false;
    }
    return true;
  };

  for (nsIFrame* child : aParentFrame->PrincipalChildList()) {
    // some children may not be selectable, e.g. :before / :after pseudoelements
    // content with user-select: none, or contenteditable="false"
    // we need to skip them
    if (child->CanProvideLineIterator() && FoundValidFrame(child)) {
      return child;
    }
    if (nsIFrame* nested = GetFirstSelectableDescendantWithLineIterator(
            aPeekOffsetStruct, child)) {
      return nested;
    }
  }
  return nullptr;
}

nsresult nsIFrame::PeekOffsetForLine(PeekOffsetStruct* aPos) {
  nsIFrame* blockFrame = this;
  nsresult result = NS_ERROR_FAILURE;

  // outer loop
  // moving to a next block when no more blocks are available in a subtree
  AutoAssertNoDomMutations guard;
  while (NS_FAILED(result)) {
    auto [newBlock, lineFrame] = blockFrame->GetContainingBlockForLine(
        aPos->mOptions.contains(PeekOffsetOption::StopAtScroller));
    if (!newBlock) {
      return NS_ERROR_FAILURE;
    }
    // FYI: If the editing host is an inline element, the block frame content
    // may be either not editable or editable but belonging to different editing
    // host.
    blockFrame = newBlock;
    nsILineIterator* iter = blockFrame->GetLineIterator();
    int32_t thisLine = iter->FindLineContaining(lineFrame);
    if (NS_WARN_IF(thisLine < 0)) {
      return NS_ERROR_FAILURE;
    }

    int8_t edgeCase = 0;  // no edge case. This should look at thisLine

    // this part will find a frame or a block frame. If it's a block frame
    // it will "drill down" to find a viable frame or it will return an
    // error.
    nsIFrame* lastFrame = this;

    // inner loop - crawling the frames within a specific block subtree
    while (true) {
      result =
          GetNextPrevLineFromBlockFrame(aPos, blockFrame, thisLine, edgeCase);
      // we came back to same spot! keep going
      if (NS_SUCCEEDED(result) &&
          (!aPos->mResultFrame || aPos->mResultFrame == lastFrame)) {
        aPos->mResultFrame = nullptr;
        lastFrame = nullptr;
        if (aPos->mDirection == eDirPrevious) {
          thisLine--;
        } else {
          thisLine++;
        }
        continue;
      }

      if (NS_FAILED(result)) {
        break;
      }

      lastFrame = aPos->mResultFrame;  // set last frame
      /* SPECIAL CHECK FOR NAVIGATION INTO TABLES
       * when we hit a frame which doesn't have line iterator, we need to
       * drill down and find a child with the line iterator to prevent the
       * crawling process to prematurely finish. Note that this is only sound if
       * we're guaranteed to not have multiple children implementing
       * LineIterator.
       *
       * So far known cases are:
       * 1) table wrapper (drill down into table row group)
       * 2) table cell (drill down into its only anon child)
       */
      const bool shouldDrillIntoChildren =
          aPos->mResultFrame->IsTableWrapperFrame() ||
          aPos->mResultFrame->IsTableCellFrame();

      if (shouldDrillIntoChildren) {
        nsIFrame* child = GetFirstSelectableDescendantWithLineIterator(
            *aPos, aPos->mResultFrame);
        if (child) {
          aPos->mResultFrame = child;
        }
      }

      if (!aPos->mResultFrame->CanProvideLineIterator()) {
        // no more selectable content at this level
        break;
      }

      if (aPos->mResultFrame == blockFrame) {
        // Make sure block element is not the same as the one we had before.
        break;
      }

      // we've struck another block element with selectable content!
      if (aPos->mDirection == eDirPrevious) {
        edgeCase = 1;  // far edge, search from end backwards
      } else {
        edgeCase = -1;  // near edge search from beginning onwards
      }
      thisLine = 0;  // this line means nothing now.
      // everything else means something so keep looking "inside" the
      // block
      blockFrame = aPos->mResultFrame;
    }
  }
  return result;
}

nsresult nsIFrame::PeekOffsetForLineEdge(PeekOffsetStruct* aPos) {
  // Adjusted so that the caret can't get confused when content changes
  nsIFrame* frame = AdjustFrameForSelectionStyles(this);
  // FIXME: Use PeekOffsetStruct::mAncestorLimiter instead.
  Element* editingHost = frame->GetContent()->GetEditingHost();

  auto [blockFrame, lineFrame] = frame->GetContainingBlockForLine(
      aPos->mOptions.contains(PeekOffsetOption::StopAtScroller));
  if (!blockFrame) {
    return NS_ERROR_FAILURE;
  }
  AutoAssertNoDomMutations guard;
  nsILineIterator* it = blockFrame->GetLineIterator();
  int32_t thisLine = it->FindLineContaining(lineFrame);
  if (thisLine < 0) {
    return NS_ERROR_FAILURE;
  }

  nsIFrame* baseFrame = nullptr;
  bool endOfLine = eSelectEndLine == aPos->mAmount;

  if (aPos->mOptions.contains(PeekOffsetOption::Visual) &&
      PresContext()->BidiEnabled()) {
    nsIFrame* firstFrame;
    bool isReordered;
    nsIFrame* lastFrame;
    MOZ_TRY(
        it->CheckLineOrder(thisLine, &isReordered, &firstFrame, &lastFrame));
    baseFrame = endOfLine ? lastFrame : firstFrame;
  } else {
    auto line = it->GetLine(thisLine).unwrap();

    nsIFrame* frame = line.mFirstFrameOnLine;
    bool lastFrameWasEditable = false;
    for (int32_t count = line.mNumFramesOnLine; count;
         --count, frame = frame->GetNextSibling()) {
      if (frame->IsGeneratedContentFrame()) {
        continue;
      }
      // When jumping to the end of the line with the "end" key,
      // try to skip over brFrames
      if (endOfLine && line.mNumFramesOnLine > 1 && frame->IsBrFrame() &&
          lastFrameWasEditable == frame->GetContent()->IsEditable()) {
        continue;
      }
      lastFrameWasEditable =
          frame->GetContent() && frame->GetContent()->IsEditable();
      baseFrame = frame;
      if (!endOfLine) {
        break;
      }
    }
  }
  if (!baseFrame) {
    return NS_ERROR_FAILURE;
  }
  // Make sure we are not leaving our inline editing host if exists
  if (editingHost) {
    if (nsIFrame* frame = editingHost->GetPrimaryFrame()) {
      if (frame->IsInlineOutside() &&
          !editingHost->Contains(baseFrame->GetContent())) {
        baseFrame = frame;
        if (endOfLine) {
          baseFrame = baseFrame->LastContinuation();
        }
      }
    }
  }
  FrameTarget targetFrame = DrillDownToSelectionFrame(
      baseFrame, endOfLine, nsIFrame::IGNORE_NATIVE_ANONYMOUS_SUBTREE);
  SetPeekResultFromFrame(*aPos, targetFrame.frame, endOfLine ? -1 : 0,
                         OffsetIsAtLineEdge::Yes);
  if (endOfLine && targetFrame.frame->HasSignificantTerminalNewline()) {
    // Do not position the caret after the terminating newline if we're
    // trying to move to the end of line (see bug 596506)
    --aPos->mContentOffset;
  }
  if (!aPos->mResultContent) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult nsIFrame::PeekOffset(PeekOffsetStruct* aPos) {
  MOZ_ASSERT(aPos);

  if (NS_WARN_IF(HasAnyStateBits(NS_FRAME_IS_DIRTY))) {
    // FIXME(Bug 1654362): <caption> currently can remain dirty.
    return NS_ERROR_UNEXPECTED;
  }

  // Translate content offset to be relative to frame
  int32_t offset = aPos->mStartOffset - GetRangeForFrame(this).start;

  switch (aPos->mAmount) {
    case eSelectCharacter:
    case eSelectCluster:
      return PeekOffsetForCharacter(aPos, offset);
    case eSelectWordNoSpace:
      // eSelectWordNoSpace means that we should not be eating any whitespace
      // when moving to the adjacent word.  This means that we should set aPos->
      // mWordMovementType to eEndWord if we're moving forwards, and to
      // eStartWord if we're moving backwards.
      if (aPos->mDirection == eDirPrevious) {
        aPos->mWordMovementType = eStartWord;
      } else {
        aPos->mWordMovementType = eEndWord;
      }
      // Intentionally fall through the eSelectWord case.
      [[fallthrough]];
    case eSelectWord:
      return PeekOffsetForWord(aPos, offset);
    case eSelectLine:
      return PeekOffsetForLine(aPos);
    case eSelectBeginLine:
    case eSelectEndLine:
      return PeekOffsetForLineEdge(aPos);
    case eSelectParagraph:
      return PeekOffsetForParagraph(aPos);
    default: {
      NS_ASSERTION(false, "Invalid amount");
      return NS_ERROR_FAILURE;
    }
  }
}

nsIFrame::FrameSearchResult nsIFrame::PeekOffsetNoAmount(bool aForward,
                                                         int32_t* aOffset) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  // Sure, we can stop right here.
  return FOUND;
}

nsIFrame::FrameSearchResult nsIFrame::PeekOffsetCharacter(
    bool aForward, int32_t* aOffset, PeekOffsetCharacterOptions aOptions) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  int32_t startOffset = *aOffset;
  // A negative offset means "end of frame", which in our case means offset 1.
  if (startOffset < 0) {
    startOffset = 1;
  }
  if (aForward == (startOffset == 0)) {
    // We're before the frame and moving forward, or after it and moving
    // backwards: skip to the other side and we're done.
    *aOffset = 1 - startOffset;
    return FOUND;
  }
  return CONTINUE;
}

nsIFrame::FrameSearchResult nsIFrame::PeekOffsetWord(
    bool aForward, bool aWordSelectEatSpace, bool aIsKeyboardSelect,
    int32_t* aOffset, PeekWordState* aState, bool /*aTrimSpaces*/) {
  NS_ASSERTION(aOffset && *aOffset <= 1, "aOffset out of range");
  int32_t startOffset = *aOffset;
  // This isn't text, so truncate the context
  aState->mContext.Truncate();
  if (startOffset < 0) {
    startOffset = 1;
  }
  if (aForward == (startOffset == 0)) {
    // We're before the frame and moving forward, or after it and moving
    // backwards. If we're looking for non-whitespace, we found it (without
    // skipping this frame).
    if (!aState->mAtStart) {
      if (aState->mLastCharWasPunctuation) {
        // We're not punctuation, so this is a punctuation boundary.
        if (BreakWordBetweenPunctuation(aState, aForward, false, false,
                                        aIsKeyboardSelect)) {
          return FOUND;
        }
      } else {
        // This is not a punctuation boundary.
        if (aWordSelectEatSpace && aState->mSawBeforeType) {
          return FOUND;
        }
      }
    }
    // Otherwise skip to the other side and note that we encountered
    // non-whitespace.
    *aOffset = 1 - startOffset;
    aState->Update(false,  // not punctuation
                   false   // not whitespace
    );
    if (!aWordSelectEatSpace) {
      aState->SetSawBeforeType();
    }
  }
  return CONTINUE;
}

// static
bool nsIFrame::BreakWordBetweenPunctuation(const PeekWordState* aState,
                                           bool aForward, bool aPunctAfter,
                                           bool aWhitespaceAfter,
                                           bool aIsKeyboardSelect) {
  NS_ASSERTION(aPunctAfter != aState->mLastCharWasPunctuation,
               "Call this only at punctuation boundaries");
  if (aState->mLastCharWasWhitespace) {
    // We always stop between whitespace and punctuation
    return true;
  }
  if (!StaticPrefs::layout_word_select_stop_at_punctuation()) {
    // When this pref is false, we never stop at a punctuation boundary unless
    // it's followed by whitespace (in the relevant direction).
    return aWhitespaceAfter;
  }
  if (!aIsKeyboardSelect) {
    // mouse caret movement (e.g. word selection) always stops at every
    // punctuation boundary
    return true;
  }
  bool afterPunct = aForward ? aState->mLastCharWasPunctuation : aPunctAfter;
  if (!afterPunct) {
    // keyboard caret movement only stops after punctuation (in content order)
    return false;
  }
  // Stop only if we've seen some non-punctuation since the last whitespace;
  // don't stop after punctuation that follows whitespace.
  return aState->mSeenNonPunctuationSinceWhitespace;
}

std::pair<nsIFrame*, nsIFrame*> nsIFrame::GetContainingBlockForLine(
    bool aLockScroll) const {
  const nsIFrame* parentFrame = this;
  const nsIFrame* frame;
  while (parentFrame) {
    frame = parentFrame;
    if (frame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
      // if we are searching for a frame that is not in flow we will not find
      // it. we must instead look for its placeholder
      if (frame->HasAnyStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        // abspos continuations don't have placeholders, get the fif
        frame = frame->FirstInFlow();
      }
      frame = frame->GetPlaceholderFrame();
      if (!frame) {
        return std::pair(nullptr, nullptr);
      }
    }
    parentFrame = frame->GetParent();
    if (parentFrame) {
      if (aLockScroll && parentFrame->IsScrollContainerFrame()) {
        return std::pair(nullptr, nullptr);
      }
      if (parentFrame->CanProvideLineIterator()) {
        return std::pair(const_cast<nsIFrame*>(parentFrame),
                         const_cast<nsIFrame*>(frame));
      }
    }
  }
  return std::pair(nullptr, nullptr);
}

Result<bool, nsresult> nsIFrame::IsVisuallyAtLineEdge(
    nsILineIterator* aLineIterator, int32_t aLine, nsDirection aDirection) {
  auto line = aLineIterator->GetLine(aLine).unwrap();

  const bool lineIsRTL = aLineIterator->IsLineIteratorFlowRTL();

  nsIFrame *firstFrame = nullptr, *lastFrame = nullptr;
  bool isReordered = false;
  MOZ_TRY(aLineIterator->CheckLineOrder(aLine, &isReordered, &firstFrame,
                                        &lastFrame));
  if (!firstFrame || !lastFrame) {
    return true;  // XXX: Why true?  We check whether `this` is at the edge...
  }

  nsIFrame* leftmostFrame = lineIsRTL ? lastFrame : firstFrame;
  nsIFrame* rightmostFrame = lineIsRTL ? firstFrame : lastFrame;
  auto FrameIsRTL = [](nsIFrame* aFrame) {
    return nsBidiPresUtils::FrameDirection(aFrame) ==
           mozilla::intl::BidiDirection::RTL;
  };
  if (!lineIsRTL == (aDirection == eDirPrevious)) {
    nsIFrame* maybeLeftmostFrame = leftmostFrame;
    for ([[maybe_unused]] int32_t i : IntegerRange(line.mNumFramesOnLine)) {
      if (maybeLeftmostFrame == this) {
        return true;
      }
      // If left edge of the line starts with placeholder frames, we can ignore
      // them and should keep checking the following frames.
      if (!maybeLeftmostFrame->IsPlaceholderFrame()) {
        if ((FrameIsRTL(maybeLeftmostFrame) == lineIsRTL) ==
            (aDirection == eDirPrevious)) {
          nsIFrame::GetFirstLeaf(&maybeLeftmostFrame);
        } else {
          nsIFrame::GetLastLeaf(&maybeLeftmostFrame);
        }
        return maybeLeftmostFrame == this;
      }
      maybeLeftmostFrame = nsBidiPresUtils::GetFrameToRightOf(
          maybeLeftmostFrame, line.mFirstFrameOnLine, line.mNumFramesOnLine);
      if (!maybeLeftmostFrame) {
        return false;
      }
    }
    return false;
  }

  nsIFrame* maybeRightmostFrame = rightmostFrame;
  for ([[maybe_unused]] int32_t i : IntegerRange(line.mNumFramesOnLine)) {
    if (maybeRightmostFrame == this) {
      return true;
    }
    // If the line ends with placehlder frames, we can ignore them and should
    // keep checking the preceding frames.
    if (!maybeRightmostFrame->IsPlaceholderFrame()) {
      if ((FrameIsRTL(maybeRightmostFrame) == lineIsRTL) ==
          (aDirection == eDirPrevious)) {
        nsIFrame::GetFirstLeaf(&maybeRightmostFrame);
      } else {
        nsIFrame::GetLastLeaf(&maybeRightmostFrame);
      }
      return maybeRightmostFrame == this;
    }
    maybeRightmostFrame = nsBidiPresUtils::GetFrameToLeftOf(
        maybeRightmostFrame, line.mFirstFrameOnLine, line.mNumFramesOnLine);
    if (!maybeRightmostFrame) {
      return false;
    }
  }
  return false;
}

Result<bool, nsresult> nsIFrame::IsLogicallyAtLineEdge(
    nsILineIterator* aLineIterator, int32_t aLine, nsDirection aDirection) {
  auto line = aLineIterator->GetLine(aLine).unwrap();
  if (!line.mNumFramesOnLine) {
    return false;
  }
  MOZ_ASSERT(line.mFirstFrameOnLine);

  if (aDirection == eDirPrevious) {
    nsIFrame* maybeFirstFrame = line.mFirstFrameOnLine;
    for ([[maybe_unused]] int32_t i : IntegerRange(line.mNumFramesOnLine)) {
      if (maybeFirstFrame == this) {
        return true;
      }
      // If the line starts with placeholder frames, we can ignore them and
      // should keep checking the following frames.
      if (!maybeFirstFrame->IsPlaceholderFrame()) {
        nsIFrame::GetFirstLeaf(&maybeFirstFrame);
        return maybeFirstFrame == this;
      }
      maybeFirstFrame = maybeFirstFrame->GetNextSibling();
      if (!maybeFirstFrame) {
        return false;
      }
    }
    return false;
  }

  // eDirNext
  nsIFrame* maybeLastFrame = line.GetLastFrameOnLine();
  for ([[maybe_unused]] int32_t i : IntegerRange(line.mNumFramesOnLine)) {
    if (maybeLastFrame == this) {
      return true;
    }
    // If the line ends with placehlder frames, we can ignore them and should
    // keep checking the preceding frames.
    if (!maybeLastFrame->IsPlaceholderFrame()) {
      nsIFrame::GetLastLeaf(&maybeLastFrame);
      return maybeLastFrame == this;
    }
    maybeLastFrame = maybeLastFrame->GetPrevSibling();
  }
  return false;
}

nsIFrame::SelectablePeekReport nsIFrame::GetFrameFromDirection(
    nsDirection aDirection, const PeekOffsetOptions& aOptions,
    const Element* aAncestorLimiter) {
  SelectablePeekReport result;

  nsPresContext* presContext = PresContext();
  const bool needsVisualTraversal =
      aOptions.contains(PeekOffsetOption::Visual) && presContext->BidiEnabled();
  const bool followOofs =
      !aOptions.contains(PeekOffsetOption::StopAtPlaceholder);
  nsFrameIterator frameIterator(
      presContext, this, nsFrameIterator::Type::Leaf, needsVisualTraversal,
      aOptions.contains(PeekOffsetOption::StopAtScroller), followOofs,
      false,  // aSkipPopupChecks
      aAncestorLimiter);

  // Find the prev/next selectable frame
  bool selectable = false;
  nsIFrame* traversedFrame = this;
  AutoAssertNoDomMutations guard;
  const nsFrameSelection* frameSelection =
      GetContent() ? GetContent()->GetFrameSelection() : nullptr;
  while (!selectable) {
    auto [blockFrame, lineFrame] = traversedFrame->GetContainingBlockForLine(
        aOptions.contains(PeekOffsetOption::StopAtScroller));
    if (!blockFrame) {
      return result;
    }

    nsILineIterator* it = blockFrame->GetLineIterator();
    int32_t thisLine = it->FindLineContaining(lineFrame);
    if (thisLine < 0) {
      return result;
    }

    bool atLineEdge;
    MOZ_TRY_VAR(
        atLineEdge,
        needsVisualTraversal
            ? traversedFrame->IsVisuallyAtLineEdge(it, thisLine, aDirection)
            : traversedFrame->IsLogicallyAtLineEdge(it, thisLine, aDirection));
    if (atLineEdge) {
      result.mJumpedLine = true;
      if (!aOptions.contains(PeekOffsetOption::JumpLines)) {
        return result;  // we are done. cannot jump lines
      }
      int32_t lineToCheckWrap =
          aDirection == eDirPrevious ? thisLine - 1 : thisLine;
      if (lineToCheckWrap < 0 ||
          !it->GetLine(lineToCheckWrap).unwrap().mIsWrapped) {
        result.mJumpedHardBreak = true;
      }
    }

    traversedFrame = frameIterator.Traverse(aDirection == eDirNext);
    if (!traversedFrame) {
      return result;
    }

    if (aOptions.contains(PeekOffsetOption::StopAtPlaceholder) &&
        traversedFrame->IsPlaceholderFrame()) {
      // XXX If the placeholder frame does not have meaningful content, the user
      // may want to select as a word around the out-of-flow cotent.  However,
      // non-text frame resets context in nsIFrame::PeekOffsetWord(). Therefore,
      // next text frame considers the new word starts from its edge. So, it's
      // not enough to implement such behavior with adding a check here whether
      // the real frame may change the word with its contents if it were not
      // out-of-flow.
      result.mFoundPlaceholder = true;
      return result;
    }

    auto IsSelectable = [aAncestorLimiter, aOptions,
                         frameSelection](const nsIFrame* aFrame) {
      if (!aFrame->IsSelectable(nullptr) ||
          MOZ_UNLIKELY(!aFrame->GetContent())) {
        return false;
      }
      // If the found frame content is managed by different nsFrameSelection, we
      // cannot select the frame content with current selection.
      if (frameSelection != aFrame->GetContent()->GetFrameSelection()) {
        return false;
      }
      if (MOZ_UNLIKELY(aAncestorLimiter &&
                       !aAncestorLimiter->GetPrimaryFrame()) &&
          !aFrame->GetContent()->IsInclusiveFlatTreeDescendantOf(
              aAncestorLimiter)) {
        return false;
      }
      return !aOptions.contains(PeekOffsetOption::ForceEditableRegion) ||
             aFrame->GetContent()->IsEditable();
    };

    // Skip br frames, but only if we can select something before hitting the
    // end of the line or a non-selectable region.
    if (atLineEdge && aDirection == eDirPrevious &&
        traversedFrame->IsBrFrame()) {
      for (nsIFrame* current = traversedFrame->GetPrevSibling(); current;
           current = current->GetPrevSibling()) {
        if (!current->IsBlockOutside() && IsSelectable(current)) {
          if (!current->IsBrFrame()) {
            result.mIgnoredBrFrame = true;
          }
          break;
        }
      }
      if (result.mIgnoredBrFrame) {
        continue;
      }
    }

    selectable = IsSelectable(traversedFrame);
    if (MOZ_UNLIKELY(!frameSelection) && selectable &&
        MOZ_LIKELY(traversedFrame->GetContent())) {
      frameSelection = traversedFrame->GetContent()->GetFrameSelection();
    }
    if (!selectable) {
      if (traversedFrame->IsSelectable(nullptr)) {
        result.mHasSelectableFrame = true;
      }
      result.mMovedOverNonSelectableText = true;
    }
  }  // while (!selectable)

  result.mOffset = (aDirection == eDirNext) ? 0 : -1;

  if (aOptions.contains(PeekOffsetOption::Visual) &&
      nsBidiPresUtils::IsReversedDirectionFrame(traversedFrame)) {
    // The new frame is reverse-direction, go to the other end
    result.mOffset = -1 - result.mOffset;
  }
  result.mFrame = traversedFrame;
  return result;
}

nsIFrame::SelectablePeekReport nsIFrame::GetFrameFromDirection(
    const PeekOffsetStruct& aPos) {
  return GetFrameFromDirection(aPos.mDirection, aPos.mOptions,
                               aPos.mAncestorLimiter);
}

nsView* nsIFrame::GetClosestView(nsPoint* aOffset) const {
  nsPoint offset(0, 0);
  for (const nsIFrame* f = this; f; f = f->GetParent()) {
    if (f->HasView()) {
      if (aOffset) {
        *aOffset = offset;
      }
      return f->GetView();
    }
    offset += f->GetPosition();
  }

  MOZ_ASSERT_UNREACHABLE("No view on any parent?  How did that happen?");
  return nullptr;
}

/* virtual */
void nsIFrame::ChildIsDirty(nsIFrame* aChild) {
  MOZ_ASSERT_UNREACHABLE(
      "should never be called on a frame that doesn't "
      "inherit from nsContainerFrame");
}

#ifdef ACCESSIBILITY
a11y::AccType nsIFrame::AccessibleType() {
  if (IsTableCaption() && !GetRect().IsEmpty()) {
    return a11y::eHTMLCaptionType;
  }
  return a11y::eNoType;
}
#endif

bool nsIFrame::ClearOverflowRects() {
  if (mOverflow.mType == OverflowStorageType::None) {
    return false;
  }
  if (mOverflow.mType == OverflowStorageType::Large) {
    RemoveProperty(OverflowAreasProperty());
  }
  mOverflow.mType = OverflowStorageType::None;
  return true;
}

bool nsIFrame::SetOverflowAreas(const OverflowAreas& aOverflowAreas) {
  if (mOverflow.mType == OverflowStorageType::Large) {
    OverflowAreas* overflow = GetOverflowAreasProperty();
    bool changed = *overflow != aOverflowAreas;
    *overflow = aOverflowAreas;

    // Don't bother with converting to the deltas form if we already
    // have a property.
    return changed;
  }

  const nsRect& vis = aOverflowAreas.InkOverflow();
  uint32_t l = -vis.x,                 // left edge: positive delta is leftwards
      t = -vis.y,                      // top: positive is upwards
      r = vis.XMost() - mRect.width,   // right: positive is rightwards
      b = vis.YMost() - mRect.height;  // bottom: positive is downwards
  if (aOverflowAreas.ScrollableOverflow().IsEqualEdges(
          nsRect(nsPoint(0, 0), GetSize())) &&
      l <= InkOverflowDeltas::kMax && t <= InkOverflowDeltas::kMax &&
      r <= InkOverflowDeltas::kMax && b <= InkOverflowDeltas::kMax &&
      // we have to check these against zero because we *never* want to
      // set a frame as having no overflow in this function.  This is
      // because FinishAndStoreOverflow calls this function prior to
      // SetRect based on whether the overflow areas match aNewSize.
      // In the case where the overflow areas exactly match mRect but
      // do not match aNewSize, we need to store overflow in a property
      // so that our eventual SetRect/SetSize will know that it has to
      // reset our overflow areas.
      (l | t | r | b) != 0) {
    InkOverflowDeltas oldDeltas = mOverflow.mInkOverflowDeltas;
    // It's a "small" overflow area so we store the deltas for each edge
    // directly in the frame, rather than allocating a separate rect.
    // If they're all zero, that's fine; we're setting things to
    // no-overflow.
    mOverflow.mInkOverflowDeltas.mLeft = l;
    mOverflow.mInkOverflowDeltas.mTop = t;
    mOverflow.mInkOverflowDeltas.mRight = r;
    mOverflow.mInkOverflowDeltas.mBottom = b;
    // There was no scrollable overflow before, and there isn't now.
    return oldDeltas != mOverflow.mInkOverflowDeltas;
  } else {
    bool changed =
        !aOverflowAreas.ScrollableOverflow().IsEqualEdges(
            nsRect(nsPoint(0, 0), GetSize())) ||
        !aOverflowAreas.InkOverflow().IsEqualEdges(InkOverflowFromDeltas());

    // it's a large overflow area that we need to store as a property
    mOverflow.mType = OverflowStorageType::Large;
    AddProperty(OverflowAreasProperty(), new OverflowAreas(aOverflowAreas));
    return changed;
  }
}

enum class ApplyTransform : bool { No, Yes };

/**
 * Compute the outline inner rect (so without outline-width and outline-offset)
 * of aFrame, maybe iterating over its descendants, in aFrame's coordinate space
 * or its post-transform coordinate space (depending on aApplyTransform).
 */
static nsRect ComputeOutlineInnerRect(
    nsIFrame* aFrame, ApplyTransform aApplyTransform, bool& aOutValid,
    const nsSize* aSizeOverride = nullptr,
    const OverflowAreas* aOverflowOverride = nullptr) {
  const nsRect bounds(nsPoint(0, 0),
                      aSizeOverride ? *aSizeOverride : aFrame->GetSize());

  // The SVG container frames besides SVGTextFrame do not maintain
  // an accurate mRect. It will make the outline be larger than
  // we expect, we need to make them narrow to their children's outline.
  // aOutValid is set to false if the returned nsRect is not valid
  // and should not be included in the outline rectangle.
  aOutValid = !aFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT) ||
              !aFrame->IsSVGContainerFrame() || aFrame->IsSVGTextFrame();

  nsRect u;

  if (!aFrame->FrameMaintainsOverflow()) {
    return u;
  }

  // Start from our border-box, transformed.  See comment below about
  // transform of children.
  bool doTransform =
      aApplyTransform == ApplyTransform::Yes && aFrame->IsTransformed();
  TransformReferenceBox boundsRefBox(nullptr, bounds);
  if (doTransform) {
    u = nsDisplayTransform::TransformRect(bounds, aFrame, boundsRefBox);
  } else {
    u = bounds;
  }

  if (aOutValid && !StaticPrefs::layout_outline_include_overflow()) {
    return u;
  }

  // Only iterate through the children if the overflow areas suggest
  // that we might need to, and if the frame doesn't clip its overflow
  // anyway.
  if (aOverflowOverride) {
    if (!doTransform && bounds.IsEqualEdges(aOverflowOverride->InkOverflow()) &&
        bounds.IsEqualEdges(aOverflowOverride->ScrollableOverflow())) {
      return u;
    }
  } else {
    if (!doTransform && bounds.IsEqualEdges(aFrame->InkOverflowRect()) &&
        bounds.IsEqualEdges(aFrame->ScrollableOverflowRect())) {
      return u;
    }
  }
  const nsStyleDisplay* disp = aFrame->StyleDisplay();
  LayoutFrameType fType = aFrame->Type();
  if (fType == LayoutFrameType::ScrollContainer ||
      fType == LayoutFrameType::ListControl ||
      fType == LayoutFrameType::SVGOuterSVG) {
    return u;
  }

  auto overflowClipAxes = aFrame->ShouldApplyOverflowClipping(disp);
  auto overflowClipMargin = aFrame->OverflowClipMargin(overflowClipAxes);
  if (overflowClipAxes == kPhysicalAxesBoth && overflowClipMargin == nsSize()) {
    return u;
  }

  const nsStyleEffects* effects = aFrame->StyleEffects();
  Maybe<nsRect> clipPropClipRect =
      aFrame->GetClipPropClipRect(disp, effects, bounds.Size());

  // Iterate over all children except pop-up, absolutely-positioned,
  // float, and overflow ones.
  const FrameChildListIDs skip = {
      FrameChildListID::Absolute, FrameChildListID::Fixed,
      FrameChildListID::Float, FrameChildListID::Overflow};
  for (const auto& [list, listID] : aFrame->ChildLists()) {
    if (skip.contains(listID)) {
      continue;
    }

    for (nsIFrame* child : list) {
      if (child->IsPlaceholderFrame()) {
        continue;
      }

      // Note that passing ApplyTransform::Yes when
      // child->Combines3DTransformWithAncestors() returns true is incorrect if
      // our aApplyTransform is No... but the opposite would be as well.
      // This is because elements within a preserve-3d scene are always
      // transformed up to the top of the scene.  This means we don't have a
      // mechanism for getting a transform up to an intermediate point within
      // the scene.  We choose to over-transform rather than under-transform
      // because this is consistent with other overflow areas.
      bool validRect = true;
      nsRect childRect =
          ComputeOutlineInnerRect(child, ApplyTransform::Yes, validRect) +
          child->GetPosition();

      if (!validRect) {
        continue;
      }

      if (clipPropClipRect) {
        // Intersect with the clip before transforming.
        childRect.IntersectRect(childRect, *clipPropClipRect);
      }

      // Note that we transform each child separately according to
      // aFrame's transform, and then union, which gives a different
      // (smaller) result from unioning and then transforming the
      // union.  This doesn't match the way we handle overflow areas
      // with 2-D transforms, though it does match the way we handle
      // overflow areas in preserve-3d 3-D scenes.
      if (doTransform && !child->Combines3DTransformWithAncestors()) {
        childRect =
            nsDisplayTransform::TransformRect(childRect, aFrame, boundsRefBox);
      }

      // If a SVGContainer has a non-SVGContainer child, we assign
      // its child's outline to this SVGContainer directly.
      if (!aOutValid && validRect) {
        u = childRect;
        aOutValid = true;
      } else {
        u = u.UnionEdges(childRect);
      }
    }
  }

  if (!overflowClipAxes.isEmpty()) {
    OverflowAreas::ApplyOverflowClippingOnRect(u, bounds, overflowClipAxes,
                                               overflowClipMargin);
  }
  return u;
}

static void ComputeAndIncludeOutlineArea(nsIFrame* aFrame,
                                         OverflowAreas& aOverflowAreas,
                                         const nsSize& aNewSize) {
  const nsStyleOutline* outline = aFrame->StyleOutline();
  if (!outline->ShouldPaintOutline()) {
    return;
  }

  // When the outline property is set on a :-moz-block-inside-inline-wrapper
  // pseudo-element, it inherited that outline from the inline that was broken
  // because it contained a block.  In that case, we don't want a really wide
  // outline if the block inside the inline is narrow, so union the actual
  // contents of the anonymous blocks.
  nsIFrame* frameForArea = aFrame;
  do {
    PseudoStyleType pseudoType = frameForArea->Style()->GetPseudoType();
    if (pseudoType != PseudoStyleType::mozBlockInsideInlineWrapper) {
      break;
    }
    // If we're done, we really want it and all its later siblings.
    frameForArea = frameForArea->PrincipalChildList().FirstChild();
    NS_ASSERTION(frameForArea, "anonymous block with no children?");
  } while (frameForArea);

  // Find the union of the border boxes of all descendants, or in
  // the block-in-inline case, all descendants we care about.
  //
  // Note that the interesting perspective-related cases are taken
  // care of by the code that handles those issues for overflow
  // calling FinishAndStoreOverflow again, which in turn calls this
  // function again.  We still need to deal with preserve-3d a bit.
  nsRect innerRect;
  bool validRect = false;
  if (frameForArea == aFrame) {
    innerRect = ComputeOutlineInnerRect(aFrame, ApplyTransform::No, validRect,
                                        &aNewSize, &aOverflowAreas);
  } else {
    for (; frameForArea; frameForArea = frameForArea->GetNextSibling()) {
      nsRect r =
          ComputeOutlineInnerRect(frameForArea, ApplyTransform::Yes, validRect);

      // Adjust for offsets transforms up to aFrame's pre-transform
      // (i.e., normal) coordinate space; see comments in
      // UnionBorderBoxes for some of the subtlety here.
      for (nsIFrame *f = frameForArea, *parent = f->GetParent();
           /* see middle of loop */; f = parent, parent = f->GetParent()) {
        r += f->GetPosition();
        if (parent == aFrame) {
          break;
        }
        if (parent->IsTransformed() && !f->Combines3DTransformWithAncestors()) {
          TransformReferenceBox refBox(parent);
          r = nsDisplayTransform::TransformRect(r, parent, refBox);
        }
      }

      innerRect.UnionRect(innerRect, r);
    }
  }

  // Keep this code in sync with nsDisplayOutline::GetInnerRect.
  if (innerRect == aFrame->GetRectRelativeToSelf()) {
    aFrame->RemoveProperty(nsIFrame::OutlineInnerRectProperty());
  } else {
    SetOrUpdateRectValuedProperty(aFrame, nsIFrame::OutlineInnerRectProperty(),
                                  innerRect);
  }

  nsRect outerRect(innerRect);
  outerRect.Inflate(outline->EffectiveOffsetFor(outerRect));

  if (outline->mOutlineStyle.IsAuto()) {
    nsPresContext* pc = aFrame->PresContext();

    pc->Theme()->GetWidgetOverflow(pc->DeviceContext(), aFrame,
                                   StyleAppearance::FocusOutline, &outerRect);
  } else {
    const nscoord width = outline->GetOutlineWidth();
    outerRect.Inflate(width);
  }

  nsRect& vo = aOverflowAreas.InkOverflow();
  vo = vo.UnionEdges(innerRect.Union(outerRect));
}

bool nsIFrame::FinishAndStoreOverflow(OverflowAreas& aOverflowAreas,
                                      nsSize aNewSize, nsSize* aOldSize,
                                      const nsStyleDisplay* aStyleDisplay) {
  MOZ_ASSERT(FrameMaintainsOverflow(),
             "Don't call - overflow rects not maintained on these SVG frames");

  const nsStyleDisplay* disp = StyleDisplayWithOptionalParam(aStyleDisplay);
  bool hasTransform = IsTransformed();

  nsRect bounds(nsPoint(0, 0), aNewSize);
  // Store the passed in overflow area if we are a preserve-3d frame or we have
  // a transform, and it's not just the frame bounds.
  if (hasTransform || Combines3DTransformWithAncestors()) {
    if (!aOverflowAreas.InkOverflow().IsEqualEdges(bounds) ||
        !aOverflowAreas.ScrollableOverflow().IsEqualEdges(bounds)) {
      OverflowAreas* initial = GetProperty(nsIFrame::InitialOverflowProperty());
      if (!initial) {
        AddProperty(nsIFrame::InitialOverflowProperty(),
                    new OverflowAreas(aOverflowAreas));
      } else if (initial != &aOverflowAreas) {
        *initial = aOverflowAreas;
      }
    } else {
      RemoveProperty(nsIFrame::InitialOverflowProperty());
    }
#ifdef DEBUG
    SetProperty(nsIFrame::DebugInitialOverflowPropertyApplied(), true);
#endif
  } else {
#ifdef DEBUG
    RemoveProperty(nsIFrame::DebugInitialOverflowPropertyApplied());
#endif
  }

  nsSize oldSize = mRect.Size();
  bool sizeChanged = ((aOldSize ? *aOldSize : oldSize) != aNewSize);

  // Our frame size may not have been computed and set yet, but code under
  // functions such as ComputeEffectsRect (which we're about to call) use the
  // values that are stored in our frame rect to compute their results.  We
  // need the results from those functions to be based on the frame size that
  // we *will* have, so we temporarily set our frame size here before calling
  // those functions.
  //
  // XXX Someone should document here why we revert the frame size before we
  // return rather than just leaving it set.
  //
  // We pass false here to avoid invalidating display items for this temporary
  // change. We sometimes reflow frames multiple times, with the final size
  // being the same as the initial. The single call to SetSize after reflow is
  // done will take care of invalidating display items if the size has actually
  // changed.
  SetSize(aNewSize, false);

  const auto overflowClipAxes = ShouldApplyOverflowClipping(disp);

  if (ChildrenHavePerspective(disp) && sizeChanged) {
    RecomputePerspectiveChildrenOverflow(this);

    if (overflowClipAxes != kPhysicalAxesBoth) {
      aOverflowAreas.SetAllTo(bounds);
      DebugOnly<bool> ok = ComputeCustomOverflow(aOverflowAreas);

      // ComputeCustomOverflow() should not return false, when
      // FrameMaintainsOverflow() returns true.
      MOZ_ASSERT(ok, "FrameMaintainsOverflow() != ComputeCustomOverflow()");

      UnionChildOverflow(aOverflowAreas);
    }
  }

  // This is now called FinishAndStoreOverflow() instead of
  // StoreOverflow() because frame-generic ways of adding overflow
  // can happen here, e.g. CSS2 outline and native theme.
  // If the overflow area width or height is nscoord_MAX, then a saturating
  // union may have encountered an overflow, so the overflow may not contain the
  // frame border-box. Don't warn in that case.
  // Don't warn for SVG either, since SVG doesn't need the overflow area
  // to contain the frame bounds.
#ifdef DEBUG
  for (const auto otype : AllOverflowTypes()) {
    const nsRect& r = aOverflowAreas.Overflow(otype);
    NS_ASSERTION(aNewSize.width == 0 || aNewSize.height == 0 ||
                     r.width == nscoord_MAX || r.height == nscoord_MAX ||
                     HasAnyStateBits(NS_FRAME_SVG_LAYOUT) ||
                     r.Contains(nsRect(nsPoint(), aNewSize)),
                 "Computed overflow area must contain frame bounds");
  }
#endif

  // Overflow area must always include the frame's top-left and bottom-right,
  // even if the frame rect is empty (so we can scroll to those positions).
  const bool shouldIncludeBounds = [&] {
    if (aNewSize.width == 0 && IsInlineFrame()) {
      // Pending a real fix for bug 426879, don't do this for inline frames with
      // zero width.
      return false;
    }
    if (HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
      // Do not do this for SVG either, since it will usually massively increase
      // the area unnecessarily (except for SVG that applies clipping, since
      // that's the pre-existing behavior, and breaks pre-rendering otherwise).
      // FIXME(bug 1770704): This check most likely wants to be removed or check
      // for specific frame types at least.
      return !overflowClipAxes.isEmpty();
    }
    return true;
  }();

  if (shouldIncludeBounds) {
    for (const auto otype : AllOverflowTypes()) {
      nsRect& o = aOverflowAreas.Overflow(otype);
      o = o.UnionEdges(bounds);
    }
  }

  // If we clip our children, clear accumulated overflow area in the affected
  // dimension(s). The children are actually clipped to the padding-box, but
  // since the overflow area should include the entire border-box, just set it
  // to the border-box size here.
  if (!overflowClipAxes.isEmpty()) {
    aOverflowAreas.ApplyClipping(bounds, overflowClipAxes,
                                 OverflowClipMargin(overflowClipAxes));
  }

  ComputeAndIncludeOutlineArea(this, aOverflowAreas, aNewSize);

  // Nothing in here should affect scrollable overflow.
  aOverflowAreas.InkOverflow() =
      ComputeEffectsRect(this, aOverflowAreas.InkOverflow(), aNewSize);

  // Absolute position clipping
  const nsStyleEffects* effects = StyleEffects();
  Maybe<nsRect> clipPropClipRect = GetClipPropClipRect(disp, effects, aNewSize);
  if (clipPropClipRect) {
    for (const auto otype : AllOverflowTypes()) {
      nsRect& o = aOverflowAreas.Overflow(otype);
      o.IntersectRect(o, *clipPropClipRect);
    }
  }

  /* If we're transformed, transform the overflow rect by the current
   * transformation. */
  if (hasTransform) {
    SetProperty(nsIFrame::PreTransformOverflowAreasProperty(),
                new OverflowAreas(aOverflowAreas));

    if (Combines3DTransformWithAncestors()) {
      /* If we're a preserve-3d leaf frame, then our pre-transform overflow
       * should be correct. Our post-transform overflow is empty though, because
       * we only contribute to the overflow area of the preserve-3d root frame.
       * If we're an intermediate frame then the pre-transform overflow should
       * contain all our non-preserve-3d children, which is what we want. Again
       * we have no post-transform overflow.
       */
      aOverflowAreas.SetAllTo(nsRect());
    } else {
      TransformReferenceBox refBox(this);
      for (const auto otype : AllOverflowTypes()) {
        nsRect& o = aOverflowAreas.Overflow(otype);
        o = nsDisplayTransform::TransformRect(o, this, refBox);
      }

      /* If we're the root of the 3d context, then we want to include the
       * overflow areas of all the participants. This won't have happened yet as
       * the code above set their overflow area to empty. Manually collect these
       * overflow areas now.
       */
      if (Extend3DContext(disp, effects)) {
        ComputePreserve3DChildrenOverflow(aOverflowAreas);
      }
    }
  } else {
    RemoveProperty(nsIFrame::PreTransformOverflowAreasProperty());
  }

  /* Revert the size change in case some caller is depending on this. */
  SetSize(oldSize, false);

  bool anyOverflowChanged;
  if (aOverflowAreas != OverflowAreas(bounds, bounds)) {
    anyOverflowChanged = SetOverflowAreas(aOverflowAreas);
  } else {
    anyOverflowChanged = ClearOverflowRects();
  }

  if (anyOverflowChanged) {
    SVGObserverUtils::InvalidateDirectRenderingObservers(this);
    if (nsBlockFrame* block = do_QueryFrame(this)) {
      // NOTE(emilio): we need to use BeforeReflow::Yes, because we want to
      // invalidate in cases where we _used_ to have an overflow marker and no
      // longer do.
      if (TextOverflow::CanHaveOverflowMarkers(
              block, TextOverflow::BeforeReflow::Yes)) {
        DiscardDisplayItems(this, [](nsDisplayItem* aItem) {
          return aItem->GetType() == DisplayItemType::TYPE_TEXT_OVERFLOW;
        });
        SchedulePaint(PAINT_DEFAULT);
      }
    }
  }
  return anyOverflowChanged;
}

void nsIFrame::RecomputePerspectiveChildrenOverflow(
    const nsIFrame* aStartFrame) {
  for (const auto& childList : ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      if (!child->FrameMaintainsOverflow()) {
        continue;  // frame does not maintain overflow rects
      }
      if (child->HasPerspective()) {
        OverflowAreas* overflow =
            child->GetProperty(nsIFrame::InitialOverflowProperty());
        nsRect bounds(nsPoint(0, 0), child->GetSize());
        if (overflow) {
          OverflowAreas overflowCopy = *overflow;
          child->FinishAndStoreOverflow(overflowCopy, bounds.Size());
        } else {
          OverflowAreas boundsOverflow;
          boundsOverflow.SetAllTo(bounds);
          child->FinishAndStoreOverflow(boundsOverflow, bounds.Size());
        }
      } else if (child->GetContent() == aStartFrame->GetContent() ||
                 child->GetClosestFlattenedTreeAncestorPrimaryFrame() ==
                     aStartFrame) {
        // If a frame is using perspective, then the size used to compute
        // perspective-origin is the size of the frame belonging to its parent
        // style. We must find any descendant frames using our size
        // (by recursing into frames that have the same containing block)
        // to update their overflow rects too.
        child->RecomputePerspectiveChildrenOverflow(aStartFrame);
      }
    }
  }
}

void nsIFrame::ComputePreserve3DChildrenOverflow(
    OverflowAreas& aOverflowAreas) {
  // Find all descendants that participate in the 3d context, and include their
  // overflow. These descendants have an empty overflow, so won't have been
  // included in the normal overflow calculation. Any children that don't
  // participate have normal overflow, so will have been included already.

  nsRect childVisual;
  nsRect childScrollable;
  for (const auto& childList : ChildLists()) {
    for (nsIFrame* child : childList.mList) {
      // If this child participates in the 3d context, then take the
      // pre-transform region (which contains all descendants that aren't
      // participating in the 3d context) and transform it into the 3d context
      // root coordinate space.
      if (child->Combines3DTransformWithAncestors()) {
        OverflowAreas childOverflow = child->GetOverflowAreasRelativeToSelf();
        TransformReferenceBox refBox(child);
        for (const auto otype : AllOverflowTypes()) {
          nsRect& o = childOverflow.Overflow(otype);
          o = nsDisplayTransform::TransformRect(o, child, refBox);
        }

        aOverflowAreas.UnionWith(childOverflow);

        // If this child also extends the 3d context, then recurse into it
        // looking for more participants.
        if (child->Extend3DContext()) {
          child->ComputePreserve3DChildrenOverflow(aOverflowAreas);
        }
      }
    }
  }
}

bool nsIFrame::ZIndexApplies() const {
  return StyleDisplay()->IsPositionedStyle() || IsFlexOrGridItem() ||
         IsMenuPopupFrame();
}

Maybe<int32_t> nsIFrame::ZIndex() const {
  if (!ZIndexApplies()) {
    return Nothing();
  }
  const auto& zIndex = StylePosition()->mZIndex;
  if (zIndex.IsAuto()) {
    return Nothing();
  }
  return Some(zIndex.AsInteger());
}

bool nsIFrame::IsScrollAnchor(ScrollAnchorContainer** aOutContainer) {
  if (!mInScrollAnchorChain) {
    return false;
  }

  nsIFrame* f = this;

  // FIXME(emilio, bug 1629280): We should find a non-null anchor if we have the
  // flag set, but bug 1629280 makes it so that we cannot really assert it /
  // make this just a `while (true)`, and uncomment the below assertion.
  while (auto* container = ScrollAnchorContainer::FindFor(f)) {
    // MOZ_ASSERT(f->IsInScrollAnchorChain());
    if (nsIFrame* anchor = container->AnchorNode()) {
      if (anchor != this) {
        return false;
      }
      if (aOutContainer) {
        *aOutContainer = container;
      }
      return true;
    }

    f = container->Frame();
  }

  return false;
}

bool nsIFrame::IsInScrollAnchorChain() const { return mInScrollAnchorChain; }

void nsIFrame::SetInScrollAnchorChain(bool aInChain) {
  mInScrollAnchorChain = aInChain;
}

uint32_t nsIFrame::GetDepthInFrameTree() const {
  uint32_t result = 0;
  for (nsContainerFrame* ancestor = GetParent(); ancestor;
       ancestor = ancestor->GetParent()) {
    result++;
  }
  return result;
}

/**
 * This function takes a frame that is part of a block-in-inline split,
 * and _if_ that frame is an anonymous block created by an ib split it
 * returns the block's preceding inline.  This is needed because the
 * split inline's style is the parent of the anonymous block's style.
 *
 * If aFrame is not an anonymous block, null is returned.
 */
static nsIFrame* GetIBSplitSiblingForAnonymousBlock(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "Must have a non-null frame!");
  NS_ASSERTION(aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT),
               "GetIBSplitSibling should only be called on ib-split frames");

  if (aFrame->Style()->GetPseudoType() !=
      PseudoStyleType::mozBlockInsideInlineWrapper) {
    // it's not an anonymous block
    return nullptr;
  }

  // Find the first continuation of the frame.  (Ugh.  This ends up
  // being O(N^2) when it is called O(N) times.)
  aFrame = aFrame->FirstContinuation();

  /*
   * Now look up the nsGkAtoms::IBSplitPrevSibling
   * property.
   */
  nsIFrame* ibSplitSibling =
      aFrame->GetProperty(nsIFrame::IBSplitPrevSibling());
  NS_ASSERTION(ibSplitSibling, "Broken frame tree?");
  return ibSplitSibling;
}

/**
 * Get the parent, corrected for the mangled frame tree resulting from
 * having a block within an inline.  The result only differs from the
 * result of |GetParent| when |GetParent| returns an anonymous block
 * that was created for an element that was 'display: inline' because
 * that element contained a block.
 *
 * Also skip anonymous scrolled-content parents; inherit directly from the
 * outer scroll frame.
 *
 * Also skip NAC parents if the child frame is NAC.
 */
static nsIFrame* GetCorrectedParent(const nsIFrame* aFrame) {
  nsIFrame* parent = aFrame->GetParent();
  if (!parent) {
    return nullptr;
  }

  // For a table caption we want the _inner_ table frame (unless it's anonymous)
  // as the style parent.
  if (aFrame->IsTableCaption()) {
    nsIFrame* innerTable = parent->PrincipalChildList().FirstChild();
    if (!innerTable->Style()->IsAnonBox()) {
      return innerTable;
    }
  }

  // Table wrappers are always anon boxes; if we're in here for an outer
  // table, that actually means its the _inner_ table that wants to
  // know its parent. So get the pseudo of the inner in that case.
  auto pseudo = aFrame->Style()->GetPseudoType();
  if (pseudo == PseudoStyleType::tableWrapper) {
    pseudo =
        aFrame->PrincipalChildList().FirstChild()->Style()->GetPseudoType();
  }

  // Prevent a NAC pseudo-element from inheriting from its NAC parent, and
  // inherit from the NAC generator element instead.
  if (pseudo != PseudoStyleType::NotPseudo) {
    MOZ_ASSERT(aFrame->GetContent());
    Element* element = Element::FromNode(aFrame->GetContent());
    // Make sure to avoid doing the fixup for non-element-backed pseudos like
    // ::first-line and such.
    if (element && !element->IsRootOfNativeAnonymousSubtree() &&
        element->GetPseudoElementType() == aFrame->Style()->GetPseudoType()) {
      while (parent->GetContent() &&
             !parent->GetContent()->IsRootOfNativeAnonymousSubtree()) {
        parent = parent->GetInFlowParent();
      }
      parent = parent->GetInFlowParent();
    }
  }

  return nsIFrame::CorrectStyleParentFrame(parent, pseudo);
}

/* static */
nsIFrame* nsIFrame::CorrectStyleParentFrame(nsIFrame* aProspectiveParent,
                                            PseudoStyleType aChildPseudo) {
  MOZ_ASSERT(aProspectiveParent, "Must have a prospective parent");

  if (aChildPseudo != PseudoStyleType::NotPseudo) {
    // Non-inheriting anon boxes have no style parent frame at all.
    if (PseudoStyle::IsNonInheritingAnonBox(aChildPseudo)) {
      return nullptr;
    }

    // Other anon boxes are parented to their actual parent already, except
    // for non-elements.  Those should not be treated as an anon box.
    if (PseudoStyle::IsAnonBox(aChildPseudo) &&
        !nsCSSAnonBoxes::IsNonElement(aChildPseudo)) {
      NS_ASSERTION(aChildPseudo != PseudoStyleType::mozBlockInsideInlineWrapper,
                   "Should have dealt with kids that have "
                   "NS_FRAME_PART_OF_IBSPLIT elsewhere");
      return aProspectiveParent;
    }
  }

  // Otherwise, walk up out of all anon boxes.  For placeholder frames, walk out
  // of all pseudo-elements as well.  Otherwise ReparentComputedStyle could
  // cause style data to be out of sync with the frame tree.
  nsIFrame* parent = aProspectiveParent;
  do {
    if (parent->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
      nsIFrame* sibling = GetIBSplitSiblingForAnonymousBlock(parent);

      if (sibling) {
        // |parent| was a block in an {ib} split; use the inline as
        // |the style parent.
        parent = sibling;
      }
    }

    if (!parent->Style()->IsPseudoOrAnonBox()) {
      return parent;
    }

    if (!parent->Style()->IsAnonBox() && aChildPseudo != PseudoStyleType::MAX) {
      // nsPlaceholderFrame passes in PseudoStyleType::MAX for
      // aChildPseudo (even though that's not a valid pseudo-type) just to
      // trigger this behavior of walking up to the nearest non-pseudo
      // ancestor.
      return parent;
    }

    parent = parent->GetInFlowParent();
  } while (parent);

  if (aProspectiveParent->Style()->GetPseudoType() ==
      PseudoStyleType::viewportScroll) {
    // aProspectiveParent is the scrollframe for a viewport
    // and the kids are the anonymous scrollbars
    return aProspectiveParent;
  }

  // We can get here if the root element is absolutely positioned.
  // We can't test for this very accurately, but it can only happen
  // when the prospective parent is a canvas frame.
  NS_ASSERTION(aProspectiveParent->IsCanvasFrame(),
               "Should have found a parent before this");
  return nullptr;
}

ComputedStyle* nsIFrame::DoGetParentComputedStyle(
    nsIFrame** aProviderFrame) const {
  *aProviderFrame = nullptr;

  // Handle display:contents and the root frame, when there's no parent frame
  // to inherit from.
  if (MOZ_LIKELY(mContent)) {
    Element* parentElement = mContent->GetFlattenedTreeParentElement();
    if (MOZ_LIKELY(parentElement)) {
      auto pseudo = Style()->GetPseudoType();
      if (pseudo == PseudoStyleType::NotPseudo || !mContent->IsElement() ||
          (!PseudoStyle::IsAnonBox(pseudo) &&
           // Ensure that we don't return the display:contents style
           // of the parent content for pseudos that have the same content
           // as their primary frame (like -moz-list-bullets do):
           IsPrimaryFrame()) ||
          /* if next is true then it's really a request for the table frame's
             parent context, see nsTable[Outer]Frame::GetParentComputedStyle. */
          pseudo == PseudoStyleType::tableWrapper) {
        // In some edge cases involving display: contents, we may end up here
        // for something that's pending to be reframed. In this case we return
        // the wrong style from here (because we've already lost track of it!),
        // but it's not a big deal as we're going to be reframed anyway.
        if (MOZ_LIKELY(parentElement->HasServoData()) &&
            Servo_Element_IsDisplayContents(parentElement)) {
          RefPtr<ComputedStyle> style =
              ServoStyleSet::ResolveServoStyle(*parentElement);
          // NOTE(emilio): we return a weak reference because the element also
          // holds the style context alive. This is a bit silly (we could've
          // returned a weak ref directly), but it's probably not worth
          // optimizing, given this function has just one caller which is rare,
          // and this path is rare itself.
          return style;
        }
      }
    } else {
      if (Style()->GetPseudoType() == PseudoStyleType::NotPseudo) {
        // We're a frame for the root.  We have no style parent.
        return nullptr;
      }
    }
  }

  if (!HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    /*
     * If this frame is an anonymous block created when an inline with a block
     * inside it got split, then the parent style is on its preceding inline. We
     * can get to it using GetIBSplitSiblingForAnonymousBlock.
     */
    if (HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
      nsIFrame* ibSplitSibling = GetIBSplitSiblingForAnonymousBlock(this);
      if (ibSplitSibling) {
        return (*aProviderFrame = ibSplitSibling)->Style();
      }
    }

    // If this frame is one of the blocks that split an inline, we must
    // return the "special" inline parent, i.e., the parent that this
    // frame would have if we didn't mangle the frame structure.
    *aProviderFrame = GetCorrectedParent(this);
    return *aProviderFrame ? (*aProviderFrame)->Style() : nullptr;
  }

  // We're an out-of-flow frame.  For out-of-flow frames, we must
  // resolve underneath the placeholder's parent.  The placeholder is
  // reached from the first-in-flow.
  nsPlaceholderFrame* placeholder = FirstInFlow()->GetPlaceholderFrame();
  if (!placeholder) {
    MOZ_ASSERT_UNREACHABLE("no placeholder frame for out-of-flow frame");
    *aProviderFrame = GetCorrectedParent(this);
    return *aProviderFrame ? (*aProviderFrame)->Style() : nullptr;
  }
  return placeholder->GetParentComputedStyleForOutOfFlow(aProviderFrame);
}

void nsIFrame::GetLastLeaf(nsIFrame** aFrame) {
  if (!aFrame || !*aFrame ||
      // Don't enter into native anoymous subtree from the root like <input> or
      // <textarea>.
      (*aFrame)->ContentIsRootOfNativeAnonymousSubtree()) {
    return;
  }
  for (nsIFrame* maybeLastLeaf = (*aFrame)->PrincipalChildList().LastChild();
       maybeLastLeaf;) {
    nsIFrame* lastChildNotInSubTree = nullptr;
    for (nsIFrame* child = maybeLastLeaf; child;
         child = child->GetPrevSibling()) {
      // ignore anonymous elements, e.g. mozTableAdd* mozTableRemove*
      // see bug 278197 comment #12 #13 for details
      if (!child->ContentIsRootOfNativeAnonymousSubtree()) {
        lastChildNotInSubTree = child;
        break;
      }
    }
    if (!lastChildNotInSubTree) {
      return;
    }
    *aFrame = lastChildNotInSubTree;
    maybeLastLeaf = lastChildNotInSubTree->PrincipalChildList().LastChild();
  }
}

void nsIFrame::GetFirstLeaf(nsIFrame** aFrame) {
  if (!aFrame || !*aFrame) {
    return;
  }
  nsIFrame* child = *aFrame;
  while (true) {
    child = child->PrincipalChildList().FirstChild();
    if (!child) {
      return;  // nothing to do
    }
    *aFrame = child;
  }
}

bool nsIFrame::IsFocusableDueToScrollFrame() {
  if (!IsScrollContainerFrame()) {
    if (nsFieldSetFrame* fieldset = do_QueryFrame(this)) {
      // TODO: Do we have similar special-cases like this where we can have
      // anonymous scrollable boxes hanging off a primary frame?
      if (nsIFrame* inner = fieldset->GetInner()) {
        return inner->IsFocusableDueToScrollFrame();
      }
    }
    return false;
  }
  if (!mContent->IsHTMLElement()) {
    return false;
  }
  if (mContent->IsRootOfNativeAnonymousSubtree()) {
    return false;
  }
  if (!mContent->GetParent()) {
    return false;
  }
  if (mContent->AsElement()->HasAttr(nsGkAtoms::tabindex)) {
    return false;
  }
  // Elements with scrollable view are focusable with script & tabbable
  // Otherwise you couldn't scroll them with keyboard, which is an accessibility
  // issue (e.g. Section 508 rules) However, we don't make them to be focusable
  // with the mouse, because the extra focus outlines are considered
  // unnecessarily ugly.  When clicked on, the selection position within the
  // element will be enough to make them keyboard scrollable.
  auto* scrollContainer = static_cast<ScrollContainerFrame*>(this);
  if (scrollContainer->GetScrollStyles().IsHiddenInBothDirections()) {
    return false;
  }
  if (scrollContainer->GetScrollRange().IsEqualEdges(nsRect())) {
    return false;
  }
  return true;
}

Focusable nsIFrame::IsFocusable(IsFocusableFlags aFlags) {
  // cannot focus content in print preview mode. Only the root can be focused,
  // but that's handled elsewhere.
  if (PresContext()->Type() == nsPresContext::eContext_PrintPreview) {
    return {};
  }

  if (!mContent || !mContent->IsElement()) {
    return {};
  }

  if (!(aFlags & IsFocusableFlags::IgnoreVisibility) &&
      !IsVisibleConsideringAncestors()) {
    return {};
  }

  const StyleUserFocus uf = StyleUI()->UserFocus();
  if (uf == StyleUserFocus::None) {
    return {};
  }
  MOZ_ASSERT(!StyleUI()->IsInert(), "inert implies -moz-user-focus: none");

  const PseudoStyleType pseudo = Style()->GetPseudoType();
  if (pseudo == PseudoStyleType::anonymousItem) {
    return {};
  }

  Focusable focusable;
  if (auto* xul = nsXULElement::FromNode(mContent)) {
    // As a legacy special-case, -moz-user-focus controls focusability and
    // tabability of XUL elements in some circumstances (which default to
    // -moz-user-focus: ignore).
    auto focusability = xul->GetXULFocusability(aFlags);
    focusable.mFocusable =
        focusability.mForcedFocusable.valueOr(uf == StyleUserFocus::Normal);
    if (focusable) {
      focusable.mTabIndex = focusability.mForcedTabIndexIfFocusable.valueOr(0);
    }
  } else {
    focusable = mContent->IsFocusableWithoutStyle(aFlags);
  }

  if (focusable) {
    return focusable;
  }

  // If we're focusing with the mouse we never focus scroll areas.
  if (!(aFlags & IsFocusableFlags::WithMouse) &&
      IsFocusableDueToScrollFrame()) {
    return {true, 0};
  }

  // FIXME(emilio): some callers rely on somewhat broken return values
  // (focusable = false, but non-negative tab-index) from
  // IsFocusableWithoutStyle (for image maps in particular).
  return focusable;
}

/**
 * @return true if this text frame ends with a newline character which is
 * treated as preformatted. It should return false if this is not a text frame.
 */
bool nsIFrame::HasSignificantTerminalNewline() const { return false; }

static StyleVerticalAlignKeyword ConvertSVGDominantBaselineToVerticalAlign(
    StyleDominantBaseline aDominantBaseline) {
  // Most of these are approximate mappings.
  switch (aDominantBaseline) {
    case StyleDominantBaseline::Hanging:
    case StyleDominantBaseline::TextBeforeEdge:
      return StyleVerticalAlignKeyword::TextTop;
    case StyleDominantBaseline::TextAfterEdge:
    case StyleDominantBaseline::Ideographic:
      return StyleVerticalAlignKeyword::TextBottom;
    case StyleDominantBaseline::Central:
    case StyleDominantBaseline::Middle:
    case StyleDominantBaseline::Mathematical:
      return StyleVerticalAlignKeyword::Middle;
    case StyleDominantBaseline::Auto:
    case StyleDominantBaseline::Alphabetic:
      return StyleVerticalAlignKeyword::Baseline;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected aDominantBaseline value");
      return StyleVerticalAlignKeyword::Baseline;
  }
}

Maybe<StyleVerticalAlignKeyword> nsIFrame::VerticalAlignEnum() const {
  if (IsInSVGTextSubtree()) {
    StyleDominantBaseline dominantBaseline = StyleSVG()->mDominantBaseline;
    return Some(ConvertSVGDominantBaselineToVerticalAlign(dominantBaseline));
  }

  const auto& verticalAlign = StyleDisplay()->mVerticalAlign;
  if (verticalAlign.IsKeyword()) {
    return Some(verticalAlign.AsKeyword());
  }

  return Nothing();
}

void nsIFrame::UpdateStyleOfChildAnonBox(nsIFrame* aChildFrame,
                                         ServoRestyleState& aRestyleState) {
#ifdef DEBUG
  nsIFrame* parent = aChildFrame->GetInFlowParent();
  if (aChildFrame->IsTableFrame()) {
    parent = parent->GetParent();
  }
  if (parent->IsLineFrame()) {
    parent = parent->GetParent();
  }
  MOZ_ASSERT(nsLayoutUtils::FirstContinuationOrIBSplitSibling(parent) == this,
             "This should only be used for children!");
#endif  // DEBUG
  MOZ_ASSERT(!GetContent() || !aChildFrame->GetContent() ||
                 aChildFrame->GetContent() == GetContent(),
             "What content node is it a frame for?");
  MOZ_ASSERT(!aChildFrame->GetPrevContinuation(),
             "Only first continuations should end up here");

  // We could force the caller to pass in the pseudo, since some callers know it
  // statically...  But this API is a bit nicer.
  auto pseudo = aChildFrame->Style()->GetPseudoType();
  MOZ_ASSERT(PseudoStyle::IsAnonBox(pseudo), "Child is not an anon box?");
  MOZ_ASSERT(!PseudoStyle::IsNonInheritingAnonBox(pseudo),
             "Why did the caller bother calling us?");

  // Anon boxes inherit from their parent; that's us.
  RefPtr<ComputedStyle> newContext =
      aRestyleState.StyleSet().ResolveInheritingAnonymousBoxStyle(pseudo,
                                                                  Style());

  nsChangeHint childHint =
      UpdateStyleOfOwnedChildFrame(aChildFrame, newContext, aRestyleState);

  // Now that we've updated the style on aChildFrame, check whether it itself
  // has anon boxes to deal with.
  ServoRestyleState childrenState(*aChildFrame, aRestyleState, childHint,
                                  ServoRestyleState::CanUseHandledHints::Yes);
  aChildFrame->UpdateStyleOfOwnedAnonBoxes(childrenState);

  // Assuming anon boxes don't have ::backdrop associated with them... if that
  // ever changes, we'd need to handle that here, like we do in
  // RestyleManager::ProcessPostTraversal

  // We do need to handle block pseudo-elements here, though.  Especially list
  // bullets.
  if (nsBlockFrame* block = do_QueryFrame(aChildFrame)) {
    block->UpdatePseudoElementStyles(childrenState);
  }
}

/* static */
nsChangeHint nsIFrame::UpdateStyleOfOwnedChildFrame(
    nsIFrame* aChildFrame, ComputedStyle* aNewComputedStyle,
    ServoRestyleState& aRestyleState,
    const Maybe<ComputedStyle*>& aContinuationComputedStyle) {
  MOZ_ASSERT(!aChildFrame->GetAdditionalComputedStyle(0),
             "We don't handle additional styles here");

  // Figure out whether we have an actual change.  It's important that we do
  // this, for several reasons:
  //
  // 1) Even if all the child's changes are due to properties it inherits from
  //    us, it's possible that no one ever asked us for those style structs and
  //    hence changes to them aren't reflected in the changes handled at all.
  //
  // 2) Content can change stylesheets that change the styles of pseudos, and
  //    extensions can add/remove stylesheets that change the styles of
  //    anonymous boxes directly.
  uint32_t equalStructs;  // Not used, actually.
  nsChangeHint childHint = aChildFrame->Style()->CalcStyleDifference(
      *aNewComputedStyle, &equalStructs);

  // If aChildFrame is out of flow, then aRestyleState's "changes handled by the
  // parent" doesn't apply to it, because it may have some other parent in the
  // frame tree.
  if (!aChildFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    childHint = NS_RemoveSubsumedHints(
        childHint, aRestyleState.ChangesHandledFor(aChildFrame));
  }
  if (childHint) {
    if (childHint & nsChangeHint_ReconstructFrame) {
      // If we generate a reconstruct here, remove any non-reconstruct hints we
      // may have already generated for this content.
      aRestyleState.ChangeList().PopChangesForContent(
          aChildFrame->GetContent());
    }
    aRestyleState.ChangeList().AppendChange(
        aChildFrame, aChildFrame->GetContent(), childHint);
  }

  aChildFrame->SetComputedStyle(aNewComputedStyle);
  ComputedStyle* continuationStyle = aContinuationComputedStyle
                                         ? *aContinuationComputedStyle
                                         : aNewComputedStyle;
  for (nsIFrame* kid = aChildFrame->GetNextContinuation(); kid;
       kid = kid->GetNextContinuation()) {
    MOZ_ASSERT(!kid->GetAdditionalComputedStyle(0));
    kid->SetComputedStyle(continuationStyle);
  }

  return childHint;
}

/* static */
void nsIFrame::AddInPopupStateBitToDescendants(nsIFrame* aFrame) {
  if (!aFrame->HasAnyStateBits(NS_FRAME_IN_POPUP) &&
      aFrame->TrackingVisibility()) {
    // Assume all frames in popups are visible.
    aFrame->IncApproximateVisibleCount();
  }

  aFrame->AddStateBits(NS_FRAME_IN_POPUP);

  for (const auto& childList : aFrame->CrossDocChildLists()) {
    for (nsIFrame* child : childList.mList) {
      AddInPopupStateBitToDescendants(child);
    }
  }
}

/* static */
void nsIFrame::RemoveInPopupStateBitFromDescendants(nsIFrame* aFrame) {
  if (!aFrame->HasAnyStateBits(NS_FRAME_IN_POPUP) ||
      nsLayoutUtils::IsPopup(aFrame)) {
    return;
  }

  aFrame->RemoveStateBits(NS_FRAME_IN_POPUP);

  if (aFrame->TrackingVisibility()) {
    // We assume all frames in popups are visible, so this decrement balances
    // out the increment in AddInPopupStateBitToDescendants above.
    aFrame->DecApproximateVisibleCount();
  }
  for (const auto& childList : aFrame->CrossDocChildLists()) {
    for (nsIFrame* child : childList.mList) {
      RemoveInPopupStateBitFromDescendants(child);
    }
  }
}

void nsIFrame::SetParent(nsContainerFrame* aParent) {
  // If our parent is a wrapper anon box, our new parent should be too.  We
  // _can_ change parent if our parent is a wrapper anon box, because some
  // wrapper anon boxes can have continuations.
  MOZ_ASSERT_IF(ParentIsWrapperAnonBox(),
                aParent->Style()->IsInheritingAnonBox());

  // Note that the current mParent may already be destroyed at this point.
  mParent = aParent;
  MOZ_ASSERT(!mParent || PresShell() == mParent->PresShell());

  if (HasAnyStateBits(NS_FRAME_HAS_VIEW | NS_FRAME_HAS_CHILD_WITH_VIEW)) {
    for (nsIFrame* f = aParent;
         f && !f->HasAnyStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
         f = f->GetParent()) {
      f->AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
    }
  }

  if (HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
    for (nsIFrame* f = aParent; f; f = f->GetParent()) {
      if (f->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
        break;
      }
      f->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
    }
  }

  if (HasAnyStateBits(NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE)) {
    for (nsIFrame* f = aParent; f; f = f->GetParent()) {
      if (f->HasAnyStateBits(
              NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE)) {
        break;
      }
      f->AddStateBits(NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE);
    }
  }

  if (HasInvalidFrameInSubtree()) {
    for (nsIFrame* f = aParent;
         f && !f->HasAnyStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT |
                                  NS_FRAME_IS_NONDISPLAY);
         f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
      f->AddStateBits(NS_FRAME_DESCENDANT_NEEDS_PAINT);
    }
  }

  if (aParent->HasAnyStateBits(NS_FRAME_IN_POPUP)) {
    AddInPopupStateBitToDescendants(this);
  } else {
    RemoveInPopupStateBitFromDescendants(this);
  }

  // If our new parent only has invalid children, then we just invalidate
  // ourselves too. This is probably faster than clearing the flag all
  // the way up the frame tree.
  if (aParent->HasAnyStateBits(NS_FRAME_ALL_DESCENDANTS_NEED_PAINT)) {
    InvalidateFrame();
  } else {
    SchedulePaint();
  }
}

bool nsIFrame::IsStackingContext(const nsStyleDisplay* aStyleDisplay,
                                 const nsStyleEffects* aStyleEffects) {
  // Properties that influence the output of this function should be handled in
  // change_bits_for_longhand as well.
  if (HasOpacity(aStyleDisplay, aStyleEffects, nullptr)) {
    return true;
  }
  if (IsTransformed()) {
    return true;
  }
  auto willChange = aStyleDisplay->mWillChange.bits;
  if (aStyleDisplay->IsContainPaint() || aStyleDisplay->IsContainLayout() ||
      willChange & StyleWillChangeBits::CONTAIN) {
    if (SupportsContainLayoutAndPaint()) {
      return true;
    }
  }

  if (ForcesStackingContextForViewTransition()) {
    return true;
  }

  // strictly speaking, 'perspective' doesn't require visual atomicity,
  // but the spec says it acts like the rest of these
  if (aStyleDisplay->HasPerspectiveStyle() ||
      willChange & StyleWillChangeBits::PERSPECTIVE) {
    if (SupportsCSSTransforms()) {
      return true;
    }
  }
  if (!StylePosition()->mZIndex.IsAuto() ||
      willChange & StyleWillChangeBits::Z_INDEX) {
    if (ZIndexApplies()) {
      return true;
    }
  }
  // Elements captured in a view transition during a view transition or whose
  // view-transition-name computed value is not none (at any time) form a s
  // https://drafts.csswg.org/css-view-transitions-1/#named-and-transitioning
  return aStyleEffects->mMixBlendMode != StyleBlend::Normal ||
         SVGIntegrationUtils::UsingEffectsForFrame(this) ||
         aStyleDisplay->IsPositionForcingStackingContext() ||
         aStyleDisplay->mIsolation != StyleIsolation::Auto ||
         willChange & StyleWillChangeBits::STACKING_CONTEXT_UNCONDITIONAL;
}

bool nsIFrame::IsStackingContext() {
  return IsStackingContext(StyleDisplay(), StyleEffects());
}

static bool IsFrameRectScrolledOutOfView(const nsIFrame* aTarget,
                                         const nsRect& aTargetRect,
                                         const nsIFrame* aParent) {
  // The ancestor frame we are checking if it clips out aTargetRect relative to
  // aTarget.
  nsIFrame* clipParent = nullptr;

  // find the first scrollable frame or root frame if we are in a fixed pos
  // subtree
  for (nsIFrame* f = const_cast<nsIFrame*>(aParent); f;
       f = nsLayoutUtils::GetCrossDocParentFrameInProcess(f)) {
    ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(f);
    if (scrollContainerFrame) {
      clipParent = f;
      break;
    }
    if (f->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
        nsLayoutUtils::IsReallyFixedPos(f)) {
      clipParent = f->GetParent();
      break;
    }
  }

  if (!clipParent) {
    // Even if we couldn't find the nearest scrollable frame, it might mean we
    // are in an out-of-process iframe, try to see if |aTarget| frame is
    // scrolled out of view in an scrollable frame in a cross-process ancestor
    // document.
    return nsLayoutUtils::FrameRectIsScrolledOutOfViewInCrossProcess(
        aTarget, aTargetRect);
  }

  nsRect clipRect = clipParent->InkOverflowRectRelativeToSelf();
  // We consider that the target is scrolled out if the scrollable (or root)
  // frame is empty.
  if (clipRect.IsEmpty()) {
    return true;
  }

  nsRect transformedRect = nsLayoutUtils::TransformFrameRectToAncestor(
      aTarget, aTargetRect, clipParent);

  if (transformedRect.IsEmpty()) {
    // If the transformed rect is empty it represents a line or a point that we
    // should check is outside the the scrollable rect.
    if (transformedRect.x > clipRect.XMost() ||
        transformedRect.y > clipRect.YMost() ||
        clipRect.x > transformedRect.XMost() ||
        clipRect.y > transformedRect.YMost()) {
      return true;
    }
  } else if (!transformedRect.Intersects(clipRect)) {
    return true;
  }

  nsIFrame* parent = clipParent->GetParent();
  if (!parent) {
    return false;
  }

  return IsFrameRectScrolledOutOfView(clipParent, transformedRect, parent);
}

bool nsIFrame::IsScrolledOutOfView() const {
  nsRect rect = InkOverflowRectRelativeToSelf();
  return IsFrameRectScrolledOutOfView(this, rect, this);
}

gfx::Matrix nsIFrame::ComputeWidgetTransform() const {
  const nsStyleUIReset* uiReset = StyleUIReset();
  if (uiReset->mMozWindowTransform.IsNone()) {
    return gfx::Matrix();
  }

  TransformReferenceBox refBox(nullptr, nsRect(nsPoint(), GetSize()));

  int32_t appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
  gfx::Matrix4x4 matrix = nsStyleTransformMatrix::ReadTransforms(
      uiReset->mMozWindowTransform, refBox, float(appUnitsPerDevPixel));

  gfx::Matrix result2d;
  if (!matrix.CanDraw2D(&result2d)) {
    // FIXME: It would be preferable to reject non-2D transforms at parse time.
    NS_WARNING(
        "-moz-window-transform does not describe a 2D transform, "
        "but only 2d transforms are supported");
    return gfx::Matrix();
  }

  return result2d;
}

void nsIFrame::DoUpdateStyleOfOwnedAnonBoxes(ServoRestyleState& aRestyleState) {
  // As a special case, we check for {ib}-split block frames here, rather
  // than have an nsInlineFrame::AppendDirectlyOwnedAnonBoxes implementation
  // that returns them.
  //
  // (If we did handle them in AppendDirectlyOwnedAnonBoxes, we would have to
  // return *all* of the in-flow {ib}-split block frames, not just the first
  // one.  For restyling, we really just need the first in flow, and the other
  // user of the AppendOwnedAnonBoxes API, AllChildIterator, doesn't need to
  // know about them at all, since these block frames never create NAC.  So we
  // avoid any unncessary hashtable lookups for the {ib}-split frames by calling
  // UpdateStyleOfOwnedAnonBoxesForIBSplit directly here.)
  if (IsInlineFrame()) {
    if (HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
      static_cast<nsInlineFrame*>(this)->UpdateStyleOfOwnedAnonBoxesForIBSplit(
          aRestyleState);
    }
    return;
  }

  AutoTArray<OwnedAnonBox, 4> frames;
  AppendDirectlyOwnedAnonBoxes(frames);
  for (OwnedAnonBox& box : frames) {
    if (box.mUpdateStyleFn) {
      box.mUpdateStyleFn(this, box.mAnonBoxFrame, aRestyleState);
    } else {
      UpdateStyleOfChildAnonBox(box.mAnonBoxFrame, aRestyleState);
    }
  }
}

/* virtual */
void nsIFrame::AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) {
  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES));
  MOZ_ASSERT_UNREACHABLE(
      "Subclasses that have directly owned anonymous boxes should override "
      "this method!");
}

void nsIFrame::DoAppendOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) {
  size_t i = aResult.Length();
  AppendDirectlyOwnedAnonBoxes(aResult);

  // After appending the directly owned anonymous boxes of this frame to
  // aResult above, we need to check each of them to see if they own
  // any anonymous boxes themselves.  Note that we keep progressing
  // through aResult, looking for additional entries in aResult from these
  // subsequent AppendDirectlyOwnedAnonBoxes calls.  (Thus we can't
  // use a ranged for loop here.)

  while (i < aResult.Length()) {
    nsIFrame* f = aResult[i].mAnonBoxFrame;
    if (f->HasAnyStateBits(NS_FRAME_OWNS_ANON_BOXES)) {
      f->AppendDirectlyOwnedAnonBoxes(aResult);
    }
    ++i;
  }
}

nsIFrame::CaretPosition::CaretPosition() : mContentOffset(0) {}

nsIFrame::CaretPosition::~CaretPosition() = default;

bool nsIFrame::HasCSSAnimations() {
  auto* collection = AnimationCollection<CSSAnimation>::Get(this);
  return collection && !collection->mAnimations.IsEmpty();
}

bool nsIFrame::HasCSSTransitions() {
  auto* collection = AnimationCollection<CSSTransition>::Get(this);
  return collection && !collection->mAnimations.IsEmpty();
}

void nsIFrame::AddSizeOfExcludingThisForTree(nsWindowSizes& aSizes) const {
  aSizes.mLayoutFramePropertiesSize +=
      mProperties.SizeOfExcludingThis(aSizes.mState.mMallocSizeOf);

  // We don't do this for Gecko because this stuff is stored in the nsPresArena
  // and so measured elsewhere.
  if (!aSizes.mState.HaveSeenPtr(mComputedStyle)) {
    mComputedStyle->AddSizeOfIncludingThis(aSizes,
                                           &aSizes.mLayoutComputedValuesNonDom);
  }

  // And our additional styles.
  int32_t index = 0;
  while (auto* extra = GetAdditionalComputedStyle(index++)) {
    if (!aSizes.mState.HaveSeenPtr(extra)) {
      extra->AddSizeOfIncludingThis(aSizes,
                                    &aSizes.mLayoutComputedValuesNonDom);
    }
  }

  for (const auto& childList : ChildLists()) {
    for (const nsIFrame* f : childList.mList) {
      f->AddSizeOfExcludingThisForTree(aSizes);
    }
  }
}

nsRect nsIFrame::GetCompositorHitTestArea(nsDisplayListBuilder* aBuilder) {
  nsRect area;

  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetScrollContainerFrameFor(this);
  if (scrollContainerFrame) {
    // If this frame is the scrolled frame of a scroll container frame, then we
    // need to pick up the area corresponding to the overflow rect as well.
    // Otherwise the parts of the overflow that are not occupied by descendants
    // get skipped and the APZ code sends touch events to the content underneath
    // instead. See https://bugzilla.mozilla.org/show_bug.cgi?id=1127773#c15.
    area = ScrollableOverflowRect();
  } else {
    area = GetRectRelativeToSelf();
  }

  if (!area.IsEmpty()) {
    return area + aBuilder->ToReferenceFrame(this);
  }

  return area;
}

CompositorHitTestInfo nsIFrame::GetCompositorHitTestInfo(
    nsDisplayListBuilder* aBuilder) {
  CompositorHitTestInfo result = CompositorHitTestInvisibleToHit;

  if (aBuilder->IsInsidePointerEventsNoneDoc()) {
    // Somewhere up the parent document chain is a subdocument with pointer-
    // events:none set on it.
    return result;
  }
  if (!GetParent()) {
    MOZ_ASSERT(IsViewportFrame());
    // Viewport frames are never event targets, other frames, like canvas
    // frames, are the event targets for any regions viewport frames may cover.
    return result;
  }
  if (Style()->PointerEvents() == StylePointerEvents::None) {
    return result;
  }
  if (!StyleVisibility()->IsVisible()) {
    return result;
  }

  // Anything that didn't match the above conditions is visible to hit-testing.
  result = CompositorHitTestFlags::eVisibleToHitTest;
  SVGUtils::MaskUsage maskUsage = SVGUtils::DetermineMaskUsage(this, false);
  if (maskUsage.UsingMaskOrClipPath()) {
    // If WebRender is enabled, simple clip-paths can be converted into WR
    // clips that WR knows how to hit-test against, so we don't need to mark
    // it as an irregular area.
    if (!maskUsage.IsSimpleClipShape()) {
      result += CompositorHitTestFlags::eIrregularArea;
    }
  }

  if (aBuilder->IsBuildingNonLayerizedScrollbar()) {
    // Scrollbars may be painted into a layer below the actual layer they will
    // scroll, and therefore wheel events may be dispatched to the outer frame
    // instead of the intended scrollframe. To address this, we force a d-t-c
    // region on scrollbar frames that won't be placed in their own layer. See
    // bug 1213324 for details.
    result += CompositorHitTestFlags::eInactiveScrollframe;
  } else if (aBuilder->GetAncestorHasApzAwareEventHandler()) {
    result += CompositorHitTestFlags::eApzAwareListeners;
  } else if (IsRangeFrame()) {
    // Range frames handle touch events directly without having a touch listener
    // so we need to let APZ know that this area cares about events.
    result += CompositorHitTestFlags::eApzAwareListeners;
  }

  if (aBuilder->IsTouchEventPrefEnabledDoc()) {
    // Inherit the touch-action flags from the parent, if there is one. We do
    // this because of how the touch-action on a frame combines the touch-action
    // from ancestor DOM elements. Refer to the documentation in
    // TouchActionHelper.cpp for details; this code is meant to be equivalent to
    // that code, but woven into the top-down recursive display list building
    // process.
    CompositorHitTestInfo inheritedTouchAction =
        aBuilder->GetCompositorHitTestInfo() & CompositorHitTestTouchActionMask;

    nsIFrame* touchActionFrame = this;
    if (ScrollContainerFrame* scrollContainerFrame =
            nsLayoutUtils::GetScrollContainerFrameFor(this)) {
      ScrollStyles ss = scrollContainerFrame->GetScrollStyles();
      if (ss.mVertical != StyleOverflow::Hidden ||
          ss.mHorizontal != StyleOverflow::Hidden) {
        touchActionFrame = scrollContainerFrame;
        // On scrollframes, stop inheriting the pan-x and pan-y flags; instead,
        // reset them back to zero to allow panning on the scrollframe unless we
        // encounter an element that disables it that's inside the scrollframe.
        // This is equivalent to the |considerPanning| variable in
        // TouchActionHelper.cpp, but for a top-down traversal.
        CompositorHitTestInfo panMask(
            CompositorHitTestFlags::eTouchActionPanXDisabled,
            CompositorHitTestFlags::eTouchActionPanYDisabled);
        inheritedTouchAction -= panMask;
      }
    }

    result += inheritedTouchAction;

    const StyleTouchAction touchAction = touchActionFrame->UsedTouchAction();
    // The CSS allows the syntax auto | none | [pan-x || pan-y] | manipulation
    // so we can eliminate some combinations of things.
    if (touchAction == StyleTouchAction::AUTO) {
      // nothing to do
    } else if (touchAction & StyleTouchAction::MANIPULATION) {
      result += CompositorHitTestFlags::eTouchActionAnimatingZoomDisabled;
    } else {
      // This path handles the cases none | [pan-x || pan-y || pinch-zoom] so
      // double-tap is disabled in here.
      if (!(touchAction & StyleTouchAction::PINCH_ZOOM)) {
        result += CompositorHitTestFlags::eTouchActionPinchZoomDisabled;
      }

      result += CompositorHitTestFlags::eTouchActionAnimatingZoomDisabled;

      if (!(touchAction & StyleTouchAction::PAN_X)) {
        result += CompositorHitTestFlags::eTouchActionPanXDisabled;
      }
      if (!(touchAction & StyleTouchAction::PAN_Y)) {
        result += CompositorHitTestFlags::eTouchActionPanYDisabled;
      }
      if (touchAction & StyleTouchAction::NONE) {
        // all the touch-action disabling flags will already have been set above
        MOZ_ASSERT(result.contains(CompositorHitTestTouchActionMask));
      }
    }
  }

  const Maybe<ScrollDirection> scrollDirection =
      aBuilder->GetCurrentScrollbarDirection();
  if (scrollDirection.isSome()) {
    if (GetContent()->IsXULElement(nsGkAtoms::thumb)) {
      const bool thumbGetsLayer = aBuilder->GetCurrentScrollbarTarget() !=
                                  layers::ScrollableLayerGuid::NULL_SCROLL_ID;
      if (thumbGetsLayer) {
        result += CompositorHitTestFlags::eScrollbarThumb;
      } else {
        result += CompositorHitTestFlags::eInactiveScrollframe;
      }
    }

    if (*scrollDirection == ScrollDirection::eVertical) {
      result += CompositorHitTestFlags::eScrollbarVertical;
    }

    // includes the ScrollbarFrame, SliderFrame, anything else that
    // might be inside the xul:scrollbar
    result += CompositorHitTestFlags::eScrollbar;
  }

  return result;
}

// Returns true if we can guarantee there is no visible descendants.
static bool HasNoVisibleDescendants(const nsIFrame* aFrame) {
  for (const auto& childList : aFrame->ChildLists()) {
    for (nsIFrame* f : childList.mList) {
      if (nsPlaceholderFrame::GetRealFrameFor(f)
              ->IsVisibleOrMayHaveVisibleDescendants()) {
        return false;
      }
    }
  }
  return true;
}

void nsIFrame::UpdateVisibleDescendantsState() {
  if (StyleVisibility()->IsVisible()) {
    // Notify invisible ancestors that a visible descendant exists now.
    nsIFrame* ancestor;
    for (ancestor = GetInFlowParent();
         ancestor && !ancestor->StyleVisibility()->IsVisible();
         ancestor = ancestor->GetInFlowParent()) {
      ancestor->mAllDescendantsAreInvisible = false;
    }
  } else {
    mAllDescendantsAreInvisible = HasNoVisibleDescendants(this);
  }
}

PhysicalAxes nsIFrame::ShouldApplyOverflowClipping(
    const nsStyleDisplay* aDisp) const {
  MOZ_ASSERT(aDisp == StyleDisplay(), "Wrong display struct");

  // 'contain:paint', which we handle as 'overflow:clip' here. Except for
  // scrollframes we don't need contain:paint to add any clipping, because
  // the scrollable frame will already clip overflowing content, and because
  // 'contain:paint' should prevent all means of escaping that clipping
  // (e.g. because it forms a fixed-pos containing block).
  if (aDisp->IsContainPaint() && !IsScrollContainerFrame() &&
      SupportsContainLayoutAndPaint()) {
    return kPhysicalAxesBoth;
  }

  // and overflow:hidden that we should interpret as clip
  if (aDisp->mOverflowX == StyleOverflow::Hidden &&
      aDisp->mOverflowY == StyleOverflow::Hidden) {
    // REVIEW: these are the frame types that set up clipping.
    LayoutFrameType type = Type();
    switch (type) {
      case LayoutFrameType::CheckboxRadio:
      case LayoutFrameType::ComboboxControl:
      case LayoutFrameType::HTMLButtonControl:
      case LayoutFrameType::ListControl:
      case LayoutFrameType::Meter:
      case LayoutFrameType::Progress:
      case LayoutFrameType::Range:
      case LayoutFrameType::SubDocument:
      case LayoutFrameType::SVGForeignObject:
      case LayoutFrameType::SVGInnerSVG:
      case LayoutFrameType::SVGOuterSVG:
      case LayoutFrameType::SVGSymbol:
      case LayoutFrameType::Table:
      case LayoutFrameType::TableCell:
        return kPhysicalAxesBoth;
      case LayoutFrameType::TextInput:
        // It has an anonymous scroll container frame that handles any overflow.
        return PhysicalAxes();
      default:
        break;
    }
  }

  // clip overflow:clip, except for nsListControlFrame which is
  // a ScrollContainerFrame sub-class.
  if (MOZ_UNLIKELY((aDisp->mOverflowX == mozilla::StyleOverflow::Clip ||
                    aDisp->mOverflowY == mozilla::StyleOverflow::Clip) &&
                   !IsListControlFrame())) {
    // FIXME: we could use GetViewportScrollStylesOverrideElement() here instead
    // if that worked correctly in a print context. (see bug 1654667)
    const auto* element = Element::FromNodeOrNull(GetContent());
    if (!element ||
        !PresContext()->ElementWouldPropagateScrollStyles(*element)) {
      PhysicalAxes axes;
      if (aDisp->mOverflowX == mozilla::StyleOverflow::Clip) {
        axes += PhysicalAxis::Horizontal;
      }
      if (aDisp->mOverflowY == mozilla::StyleOverflow::Clip) {
        axes += PhysicalAxis::Vertical;
      }
      return axes;
    }
  }

  if (HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
    return PhysicalAxes();
  }

  return IsSuppressedScrollableBlockForPrint() ? kPhysicalAxesBoth
                                               : PhysicalAxes();
}

bool nsIFrame::IsSuppressedScrollableBlockForPrint() const {
  // This condition needs to match the suppressScrollFrame logic in the frame
  // constructor.
  if (!PresContext()->IsPaginated() || !IsBlockFrame() ||
      !StyleDisplay()->IsScrollableOverflow() ||
      !StyleDisplay()->IsBlockOutsideStyle() ||
      mContent->IsInNativeAnonymousSubtree()) {
    return false;
  }
  if (auto* element = Element::FromNode(mContent);
      element && PresContext()->ElementWouldPropagateScrollStyles(*element)) {
    return false;
  }
  return true;
}

bool nsIFrame::HasUnreflowedContainerQueryAncestor() const {
  // If this frame has done the first reflow, its ancestors are guaranteed to
  // have as well.
  if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW) ||
      !PresContext()->HasContainerQueryFrames()) {
    return false;
  }
  for (nsIFrame* cur = GetInFlowParent(); cur; cur = cur->GetInFlowParent()) {
    if (!cur->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
      // Done first reflow from this ancestor up, including query containers.
      return false;
    }
    if (cur->StyleDisplay()->IsQueryContainer()) {
      return true;
    }
  }
  // No query container from this frame up to root.
  return false;
}

bool nsIFrame::ShouldBreakBefore(
    const ReflowInput::BreakType aBreakType) const {
  const auto* display = StyleDisplay();
  return ShouldBreakBetween(display, display->mBreakBefore, aBreakType);
}

bool nsIFrame::ShouldBreakAfter(const ReflowInput::BreakType aBreakType) const {
  const auto* display = StyleDisplay();
  return ShouldBreakBetween(display, display->mBreakAfter, aBreakType);
}

bool nsIFrame::ShouldBreakBetween(
    const nsStyleDisplay* aDisplay, const StyleBreakBetween aBreakBetween,
    const ReflowInput::BreakType aBreakType) const {
  const bool shouldBreakBetween = [&] {
    switch (aBreakBetween) {
      case StyleBreakBetween::Always:
        return true;
      case StyleBreakBetween::Auto:
      case StyleBreakBetween::Avoid:
        return false;
      case StyleBreakBetween::Page:
      case StyleBreakBetween::Left:
      case StyleBreakBetween::Right:
        return aBreakType == ReflowInput::BreakType::Page;
    }
    MOZ_ASSERT_UNREACHABLE("Unknown break-between value!");
    return false;
  }();

  if (!shouldBreakBetween) {
    return false;
  }
  if (IsAbsolutelyPositioned(aDisplay)) {
    // 'break-before' and 'break-after' properties does not apply to
    // absolutely-positioned boxes.
    return false;
  }
  return true;
}

#ifdef DEBUG
static void GetTagName(nsIFrame* aFrame, nsIContent* aContent, int aResultSize,
                       char* aResult) {
  if (aContent) {
    snprintf(aResult, aResultSize, "%s@%p",
             nsAtomCString(aContent->NodeInfo()->NameAtom()).get(), aFrame);
  } else {
    snprintf(aResult, aResultSize, "@%p", aFrame);
  }
}

void nsIFrame::Trace(const char* aMethod, bool aEnter) {
  if (NS_FRAME_LOG_TEST(sFrameLogModule, NS_FRAME_TRACE_CALLS)) {
    char tagbuf[40];
    GetTagName(this, mContent, sizeof(tagbuf), tagbuf);
    printf_stderr("%s: %s %s", tagbuf, aEnter ? "enter" : "exit", aMethod);
  }
}

void nsIFrame::Trace(const char* aMethod, bool aEnter,
                     const nsReflowStatus& aStatus) {
  if (NS_FRAME_LOG_TEST(sFrameLogModule, NS_FRAME_TRACE_CALLS)) {
    char tagbuf[40];
    GetTagName(this, mContent, sizeof(tagbuf), tagbuf);
    printf_stderr("%s: %s %s, status=%scomplete%s", tagbuf,
                  aEnter ? "enter" : "exit", aMethod,
                  aStatus.IsIncomplete() ? "not" : "",
                  (aStatus.NextInFlowNeedsReflow()) ? "+reflow" : "");
  }
}

void nsIFrame::TraceMsg(const char* aFormatString, ...) {
  if (NS_FRAME_LOG_TEST(sFrameLogModule, NS_FRAME_TRACE_CALLS)) {
    // Format arguments into a buffer
    char argbuf[200];
    va_list ap;
    va_start(ap, aFormatString);
    VsprintfLiteral(argbuf, aFormatString, ap);
    va_end(ap);

    char tagbuf[40];
    GetTagName(this, mContent, sizeof(tagbuf), tagbuf);
    printf_stderr("%s: %s", tagbuf, argbuf);
  }
}

void nsIFrame::VerifyDirtyBitSet(const nsFrameList& aFrameList) {
  for (nsIFrame* f : aFrameList) {
    NS_ASSERTION(f->HasAnyStateBits(NS_FRAME_IS_DIRTY), "dirty bit not set");
  }
}

// Validation of SideIsVertical.
#  define CASE(side, result) \
    static_assert(SideIsVertical(side) == result, "SideIsVertical is wrong")
CASE(eSideTop, false);
CASE(eSideRight, true);
CASE(eSideBottom, false);
CASE(eSideLeft, true);
#  undef CASE

// Validation of HalfCornerIsX.
#  define CASE(corner, result) \
    static_assert(HalfCornerIsX(corner) == result, "HalfCornerIsX is wrong")
CASE(eCornerTopLeftX, true);
CASE(eCornerTopLeftY, false);
CASE(eCornerTopRightX, true);
CASE(eCornerTopRightY, false);
CASE(eCornerBottomRightX, true);
CASE(eCornerBottomRightY, false);
CASE(eCornerBottomLeftX, true);
CASE(eCornerBottomLeftY, false);
#  undef CASE

// Validation of HalfToFullCorner.
#  define CASE(corner, result)                        \
    static_assert(HalfToFullCorner(corner) == result, \
                  "HalfToFullCorner is "              \
                  "wrong")
CASE(eCornerTopLeftX, eCornerTopLeft);
CASE(eCornerTopLeftY, eCornerTopLeft);
CASE(eCornerTopRightX, eCornerTopRight);
CASE(eCornerTopRightY, eCornerTopRight);
CASE(eCornerBottomRightX, eCornerBottomRight);
CASE(eCornerBottomRightY, eCornerBottomRight);
CASE(eCornerBottomLeftX, eCornerBottomLeft);
CASE(eCornerBottomLeftY, eCornerBottomLeft);
#  undef CASE

// Validation of FullToHalfCorner.
#  define CASE(corner, vert, result)                        \
    static_assert(FullToHalfCorner(corner, vert) == result, \
                  "FullToHalfCorner is wrong")
CASE(eCornerTopLeft, false, eCornerTopLeftX);
CASE(eCornerTopLeft, true, eCornerTopLeftY);
CASE(eCornerTopRight, false, eCornerTopRightX);
CASE(eCornerTopRight, true, eCornerTopRightY);
CASE(eCornerBottomRight, false, eCornerBottomRightX);
CASE(eCornerBottomRight, true, eCornerBottomRightY);
CASE(eCornerBottomLeft, false, eCornerBottomLeftX);
CASE(eCornerBottomLeft, true, eCornerBottomLeftY);
#  undef CASE

// Validation of SideToFullCorner.
#  define CASE(side, second, result)                        \
    static_assert(SideToFullCorner(side, second) == result, \
                  "SideToFullCorner is wrong")
CASE(eSideTop, false, eCornerTopLeft);
CASE(eSideTop, true, eCornerTopRight);

CASE(eSideRight, false, eCornerTopRight);
CASE(eSideRight, true, eCornerBottomRight);

CASE(eSideBottom, false, eCornerBottomRight);
CASE(eSideBottom, true, eCornerBottomLeft);

CASE(eSideLeft, false, eCornerBottomLeft);
CASE(eSideLeft, true, eCornerTopLeft);
#  undef CASE

// Validation of SideToHalfCorner.
#  define CASE(side, second, parallel, result)                        \
    static_assert(SideToHalfCorner(side, second, parallel) == result, \
                  "SideToHalfCorner is wrong")
CASE(eSideTop, false, true, eCornerTopLeftX);
CASE(eSideTop, false, false, eCornerTopLeftY);
CASE(eSideTop, true, true, eCornerTopRightX);
CASE(eSideTop, true, false, eCornerTopRightY);

CASE(eSideRight, false, false, eCornerTopRightX);
CASE(eSideRight, false, true, eCornerTopRightY);
CASE(eSideRight, true, false, eCornerBottomRightX);
CASE(eSideRight, true, true, eCornerBottomRightY);

CASE(eSideBottom, false, true, eCornerBottomRightX);
CASE(eSideBottom, false, false, eCornerBottomRightY);
CASE(eSideBottom, true, true, eCornerBottomLeftX);
CASE(eSideBottom, true, false, eCornerBottomLeftY);

CASE(eSideLeft, false, false, eCornerBottomLeftX);
CASE(eSideLeft, false, true, eCornerBottomLeftY);
CASE(eSideLeft, true, false, eCornerTopLeftX);
CASE(eSideLeft, true, true, eCornerTopLeftY);
#  undef CASE

#endif
