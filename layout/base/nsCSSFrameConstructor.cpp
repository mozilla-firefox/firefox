/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * construction of a frame tree that is nearly isomorphic to the content
 * tree and updating of that tree in response to dynamic changes
 */

#include "nsCSSFrameConstructor.h"

#include "ActiveLayerTracker.h"
#include "ChildIterator.h"
#include "PseudoStyleType.h"
#include "RetainedDisplayListBuilder.h"
#include "RubyUtils.h"
#include "StickyScrollContainer.h"
#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/Assertions.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Likely.h"
#include "mozilla/ManualNAC.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/PrintedSheetFrame.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SVGGradientFrame.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoStyleSetInlines.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CharacterData.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/GeneratedImageContent.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/dom/HTMLSharedListElement.h"
#include "mozilla/dom/HTMLSummaryElement.h"
#include "mozilla/intl/LocaleService.h"
#include "nsAtom.h"
#include "nsAutoLayoutPhase.h"
#include "nsBlockFrame.h"
#include "nsCRT.h"
#include "nsCanvasFrame.h"
#include "nsCheckboxRadioFrame.h"
#include "nsComboboxControlFrame.h"
#include "nsContainerFrame.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsFieldSetFrame.h"
#include "nsFirstLetterFrame.h"
#include "nsFlexContainerFrame.h"
#include "nsGkAtoms.h"
#include "nsGridContainerFrame.h"
#include "nsHTMLParts.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIFormControl.h"
#include "nsIFrameInlines.h"
#include "nsIObjectLoadingContent.h"
#include "nsIPopupContainer.h"
#include "nsIScriptError.h"
#include "nsImageFrame.h"
#include "nsInlineFrame.h"
#include "nsLayoutUtils.h"
#include "nsListControlFrame.h"
#include "nsMathMLParts.h"
#include "nsNameSpaceManager.h"
#include "nsPageContentFrame.h"
#include "nsPageFrame.h"
#include "nsPageSequenceFrame.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsRefreshDriver.h"
#include "nsRubyBaseContainerFrame.h"
#include "nsRubyBaseFrame.h"
#include "nsRubyFrame.h"
#include "nsRubyTextContainerFrame.h"
#include "nsRubyTextFrame.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableColFrame.h"
#include "nsTableFrame.h"
#include "nsTableRowFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsTableWrapperFrame.h"
#include "nsTextControlFrame.h"
#include "nsTextNode.h"
#include "nsTransitionManager.h"
#include "nsUnicharUtils.h"
#include "nsXULElement.h"

#ifdef XP_MACOSX
#  include "nsIDocShell.h"
#endif

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

#undef NOISY_FIRST_LETTER

using namespace mozilla;
using namespace mozilla::dom;

nsIFrame* NS_NewHTMLCanvasFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewHTMLVideoFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewHTMLAudioFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsContainerFrame* NS_NewSVGOuterSVGFrame(PresShell* aPresShell,
                                         ComputedStyle* aStyle);
nsContainerFrame* NS_NewSVGOuterSVGAnonChildFrame(PresShell* aPresShell,
                                                  ComputedStyle* aStyle);
nsIFrame* NS_NewSVGInnerSVGFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGGeometryFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGGFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsContainerFrame* NS_NewSVGForeignObjectFrame(PresShell* aPresShell,
                                              ComputedStyle* aStyle);
nsIFrame* NS_NewSVGAFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGSwitchFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGSymbolFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGTextFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGContainerFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGUseFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGViewFrame(PresShell* aPresShell, ComputedStyle* aStyle);
extern nsIFrame* NS_NewSVGLinearGradientFrame(PresShell* aPresShell,
                                              ComputedStyle* aStyle);
extern nsIFrame* NS_NewSVGRadialGradientFrame(PresShell* aPresShell,
                                              ComputedStyle* aStyle);
extern nsIFrame* NS_NewSVGStopFrame(PresShell* aPresShell,
                                    ComputedStyle* aStyle);
nsContainerFrame* NS_NewSVGMarkerFrame(PresShell* aPresShell,
                                       ComputedStyle* aStyle);
nsContainerFrame* NS_NewSVGMarkerAnonChildFrame(PresShell* aPresShell,
                                                ComputedStyle* aStyle);
extern nsIFrame* NS_NewSVGImageFrame(PresShell* aPresShell,
                                     ComputedStyle* aStyle);
nsIFrame* NS_NewSVGClipPathFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGFilterFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGPatternFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGMaskFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGFEContainerFrame(PresShell* aPresShell,
                                    ComputedStyle* aStyle);
nsIFrame* NS_NewSVGFELeafFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGFEImageFrame(PresShell* aPresShell, ComputedStyle* aStyle);
nsIFrame* NS_NewSVGFEUnstyledLeafFrame(PresShell* aPresShell,
                                       ComputedStyle* aStyle);
nsIFrame* NS_NewFileControlLabelFrame(PresShell*, ComputedStyle*);
nsIFrame* NS_NewComboboxLabelFrame(PresShell*, ComputedStyle*);
nsIFrame* NS_NewMiddleCroppingLabelFrame(PresShell*, ComputedStyle*);
nsIFrame* NS_NewInputButtonControlFrame(PresShell*, ComputedStyle*);

#include "mozilla/dom/NodeInfo.h"
#include "nsContentCreatorFunctions.h"
#include "nsNodeInfoManager.h"
#include "prenv.h"

#ifdef DEBUG
// Set the environment variable GECKO_FRAMECTOR_DEBUG_FLAGS to one or
// more of the following flags (comma separated) for handy debug
// output.
static bool gNoisyContentUpdates = false;
static bool gReallyNoisyContentUpdates = false;
static bool gNoisyInlineConstruction = false;

struct FrameCtorDebugFlags {
  const char* name;
  bool* on;
};

static FrameCtorDebugFlags gFrameCtorDebugFlags[] = {
    {"content-updates", &gNoisyContentUpdates},
    {"really-noisy-content-updates", &gReallyNoisyContentUpdates},
    {"noisy-inline", &gNoisyInlineConstruction}};

#  define NUM_DEBUG_FLAGS (std::size(gFrameCtorDebugFlags))
#endif

//------------------------------------------------------------------

nsIFrame* NS_NewLeafBoxFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewRangeFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewTextBoxFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewSplitterFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewMenuPopupFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewTreeBodyFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewSliderFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewScrollbarFrame(PresShell* aPresShell, ComputedStyle* aStyle);

nsIFrame* NS_NewScrollbarButtonFrame(PresShell*, ComputedStyle*);
nsIFrame* NS_NewSimpleXULLeafFrame(PresShell*, ComputedStyle*);

nsIFrame* NS_NewXULImageFrame(PresShell*, ComputedStyle*);
nsIFrame* NS_NewImageFrameForContentProperty(PresShell*, ComputedStyle*);
nsIFrame* NS_NewImageFrameForGeneratedContentIndex(PresShell*, ComputedStyle*);
nsIFrame* NS_NewImageFrameForListStyleImage(PresShell*, ComputedStyle*);
nsIFrame* NS_NewImageFrameForViewTransition(PresShell*, ComputedStyle*);

// Returns true if aFrame is an anonymous flex/grid item.
static inline bool IsAnonymousItem(const nsIFrame* aFrame) {
  return aFrame->Style()->GetPseudoType() == PseudoStyleType::MozAnonymousItem;
}

// Returns true IFF the given nsIFrame is a nsFlexContainerFrame and represents
// a -webkit-{inline-}box container.
static inline bool IsFlexContainerForLegacyWebKitBox(const nsIFrame* aFrame) {
  return aFrame->IsFlexContainerFrame() && aFrame->IsLegacyWebkitBox();
}

static MOZ_ALWAYS_INLINE void AssertAnonymousFlexOrGridItemParent(
    const nsIFrame* aChild, const nsIFrame* aParent) {
  MOZ_ASSERT(IsAnonymousItem(aChild), "expected an anonymous item child frame");
  MOZ_ASSERT(aParent, "expected a parent frame");
  MOZ_ASSERT(aParent->IsFlexOrGridContainer(),
             "anonymous items should only exist as children of flex/grid "
             "container frames");
}

static MOZ_ALWAYS_INLINE void AssertAnonymousFlexOrGridItemParent(
    const nsIFrame* aChild) {
  AssertAnonymousFlexOrGridItemParent(aChild, aChild->GetParent());
}

#define ToCreationFunc(_func)                              \
  [](PresShell* aPs, ComputedStyle* aStyle) -> nsIFrame* { \
    return _func(aPs, aStyle);                             \
  }

/**
 * True if aFrame is an actual inline frame in the sense of non-replaced
 * display:inline CSS boxes.  In other words, it can be affected by {ib}
 * splitting and can contain first-letter frames.  Basically, this is either an
 * inline frame (positioned or otherwise) or an line frame (this last because
 * it can contain first-letter and because inserting blocks in the middle of it
 * needs to terminate it).
 */
static bool IsInlineFrame(const nsIFrame* aFrame) {
  return aFrame->IsLineParticipant();
}

/**
 * True for display: contents elements.
 */
static inline bool IsDisplayContents(const Element* aElement) {
  return aElement->IsDisplayContents();
}

static inline bool IsDisplayContents(const nsIContent* aContent) {
  return aContent->IsElement() && IsDisplayContents(aContent->AsElement());
}

/**
 * True if aFrame is an instance of an SVG frame class or is an inline/block
 * frame being used for SVG text.
 */
static bool IsFrameForSVG(const nsIFrame* aFrame) {
  return aFrame->IsSVGFrame() || aFrame->IsInSVGTextSubtree();
}

static bool IsLastContinuationForColumnContent(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  return aFrame->Style()->GetPseudoType() ==
             PseudoStyleType::MozColumnContent &&
         !aFrame->GetNextContinuation();
}

/**
 * Returns true iff aFrame explicitly prevents its descendants from floating
 * (at least, down to the level of descendants which themselves are
 * float-containing blocks -- those will manage the floating status of any
 * lower-level descendents inside them, of course).
 */
static bool ShouldSuppressFloatingOfDescendants(nsIFrame* aFrame) {
  return aFrame->IsFlexOrGridContainer() || aFrame->IsMathMLFrame();
}

// Return true if column-span descendants should be suppressed under aFrame's
// subtree (until a multi-column container re-establishing a block formatting
// context). Basically, this is testing whether aFrame establishes a new block
// formatting context or not.
static bool ShouldSuppressColumnSpanDescendants(nsIFrame* aFrame) {
  if (aFrame->Style()->GetPseudoType() == PseudoStyleType::MozColumnContent) {
    // Never suppress column-span under ::-moz-column-content frames.
    return false;
  }

  if (aFrame->IsInlineFrame()) {
    // Allow inline frames to have column-span block children.
    return false;
  }

  if (!aFrame->IsBlockFrameOrSubclass() ||
      aFrame->HasAnyStateBits(NS_BLOCK_BFC | NS_FRAME_OUT_OF_FLOW) ||
      aFrame->IsFixedPosContainingBlock()) {
    // Need to suppress column-span if we:
    // - Are a different block formatting context,
    // - Are an out-of-flow frame, OR
    // - Establish a containing block for fixed-position descendants
    //
    // For example, the children of a column-span never need to be further
    // processed even if there is a nested column-span child. Because a
    // column-span always creates its own block formatting context, a nested
    // column-span child won't be in the same block formatting context with the
    // nearest multi-column ancestor. This is the same case as if the
    // column-span is outside of a multi-column hierarchy.
    return true;
  }

  return false;
}

// Reparent a frame into a wrapper frame that is a child of its old parent.
static void ReparentFrame(RestyleManager* aRestyleManager,
                          nsContainerFrame* aNewParentFrame, nsIFrame* aFrame,
                          bool aForceStyleReparent) {
  aFrame->SetParent(aNewParentFrame);
  // We reparent frames for two reasons: to put them inside ::first-line, and to
  // put them inside some wrapper anonymous boxes.
  if (aForceStyleReparent) {
    aRestyleManager->ReparentComputedStyleForFirstLine(aFrame);
  }
}

static void ReparentFrames(nsCSSFrameConstructor* aFrameConstructor,
                           nsContainerFrame* aNewParentFrame,
                           const nsFrameList& aFrameList,
                           bool aForceStyleReparent) {
  RestyleManager* restyleManager = aFrameConstructor->RestyleManager();
  for (nsIFrame* f : aFrameList) {
    ReparentFrame(restyleManager, aNewParentFrame, f, aForceStyleReparent);
  }
}

//----------------------------------------------------------------------
//
// When inline frames get weird and have block frames in them, we
// annotate them to help us respond to incremental content changes
// more easily.

static inline bool IsFramePartOfIBSplit(nsIFrame* aFrame) {
  bool result = aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT);
  MOZ_ASSERT(!result || aFrame->IsBlockFrameOrSubclass() ||
                 aFrame->IsInlineFrameOrSubclass(),
             "only block/inline frames can have NS_FRAME_PART_OF_IBSPLIT");
  return result;
}

static nsContainerFrame* GetIBSplitSibling(nsIFrame* aFrame) {
  MOZ_ASSERT(IsFramePartOfIBSplit(aFrame), "Shouldn't call this");

  // We only store the "ib-split sibling" annotation with the first
  // frame in the continuation chain. Walk back to find that frame now.
  return aFrame->FirstContinuation()->GetProperty(nsIFrame::IBSplitSibling());
}

static nsContainerFrame* GetIBSplitPrevSibling(nsIFrame* aFrame) {
  MOZ_ASSERT(IsFramePartOfIBSplit(aFrame), "Shouldn't call this");

  // We only store the ib-split sibling annotation with the first
  // frame in the continuation chain. Walk back to find that frame now.
  return aFrame->FirstContinuation()->GetProperty(
      nsIFrame::IBSplitPrevSibling());
}

static nsContainerFrame* GetLastIBSplitSibling(nsIFrame* aFrame) {
  for (nsIFrame *frame = aFrame, *next;; frame = next) {
    next = GetIBSplitSibling(frame);
    if (!next) {
      return static_cast<nsContainerFrame*>(frame);
    }
  }
  MOZ_ASSERT_UNREACHABLE("unreachable code");
  return nullptr;
}

static void SetFrameIsIBSplit(nsContainerFrame* aFrame,
                              nsContainerFrame* aIBSplitSibling) {
  MOZ_ASSERT(aFrame, "bad args!");

  // We should be the only continuation
  NS_ASSERTION(!aFrame->GetPrevContinuation(),
               "assigning ib-split sibling to other than first continuation!");
  NS_ASSERTION(!aFrame->GetNextContinuation() ||
                   IsFramePartOfIBSplit(aFrame->GetNextContinuation()),
               "should have no non-ib-split continuations here");

  // Mark the frame as ib-split.
  aFrame->AddStateBits(NS_FRAME_PART_OF_IBSPLIT);

  if (aIBSplitSibling) {
    NS_ASSERTION(!aIBSplitSibling->GetPrevContinuation(),
                 "assigning something other than the first continuation as the "
                 "ib-split sibling");

    // Store the ib-split sibling (if we were given one) with the
    // first frame in the flow.
    aFrame->SetProperty(nsIFrame::IBSplitSibling(), aIBSplitSibling);
    aIBSplitSibling->SetProperty(nsIFrame::IBSplitPrevSibling(), aFrame);
  }
}

static nsIFrame* GetIBContainingBlockFor(nsIFrame* aFrame) {
  MOZ_ASSERT(
      IsFramePartOfIBSplit(aFrame),
      "GetIBContainingBlockFor() should only be called on known IB frames");

  // Get the first "normal" ancestor of the target frame.
  nsIFrame* parentFrame;
  do {
    parentFrame = aFrame->GetParent();

    if (!parentFrame) {
      NS_ERROR("no unsplit block frame in IB hierarchy");
      return aFrame;
    }

    // Note that we ignore non-ib-split frames which have a pseudo on their
    // ComputedStyle -- they're not the frames we're looking for!  In
    // particular, they may be hiding a real parent that _is_ in an ib-split.
    if (!IsFramePartOfIBSplit(parentFrame) &&
        !parentFrame->Style()->IsPseudoOrAnonBox()) {
      break;
    }

    aFrame = parentFrame;
  } while (true);

  // post-conditions
  NS_ASSERTION(parentFrame,
               "no normal ancestor found for ib-split frame "
               "in GetIBContainingBlockFor");
  NS_ASSERTION(parentFrame != aFrame,
               "parentFrame is actually the child frame - bogus reslt");

  return parentFrame;
}

// Find the multicol containing block suitable for reframing.
//
// Note: this function may not return a ColumnSetWrapperFrame. For example, if
// the multicol containing block has "overflow:scroll" style,
// ScrollContainerFrame is returned because ColumnSetWrapperFrame is the
// scrolled frame which has the -moz-scrolled-content pseudo style. We may walk
// up "too far", but in terms of correctness of reframing, it's OK.
static nsContainerFrame* GetMultiColumnContainingBlockFor(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR),
             "Should only be called if the frame has a multi-column ancestor!");

  nsContainerFrame* current = aFrame->GetParent();
  while (current &&
         (current->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) ||
          current->Style()->IsPseudoOrAnonBox())) {
    current = current->GetParent();
  }

  MOZ_ASSERT(current,
             "No multicol containing block in a valid column hierarchy?");

  return current;
}

static bool InsertSeparatorBeforeAccessKey() {
  static bool sInitialized = false;
  static bool sValue = false;
  if (!sInitialized) {
    sInitialized = true;
    sValue =
        intl::LocaleService::GetInstance()->InsertSeparatorBeforeAccesskeys();
  }
  return sValue;
}

static bool AlwaysAppendAccessKey() {
  static bool sInitialized = false;
  static bool sValue = false;
  if (!sInitialized) {
    sInitialized = true;
    sValue = intl::LocaleService::GetInstance()->AlwaysAppendAccesskeys();
  }
  return sValue;
}

//----------------------------------------------------------------------

// Block/inline frame construction logic. We maintain a few invariants here:
//
// 1. Block frames contain block and inline frames.
//
// 2. Inline frames only contain inline frames. If an inline parent has a block
// child then the block child is migrated upward until it lands in a block
// parent (the inline frames containing block is where it will end up).

inline void SetInitialSingleChild(nsContainerFrame* aParent, nsIFrame* aFrame) {
  MOZ_ASSERT(!aFrame->GetNextSibling(), "Should be using a frame list");
  aParent->SetInitialChildList(FrameChildListID::Principal,
                               nsFrameList(aFrame, aFrame));
}

// -----------------------------------------------------------

// Structure used when constructing formatting object trees. Contains
// state information needed for absolutely positioned elements
namespace mozilla {
struct AbsoluteFrameList final : public nsFrameList {
  // Containing block for absolutely positioned elements.
  nsContainerFrame* mContainingBlock;

  explicit AbsoluteFrameList(nsContainerFrame* aContainingBlock = nullptr)
      : mContainingBlock(aContainingBlock) {}

  // Transfer frames in aOther to this list. aOther becomes empty after this
  // operation.
  AbsoluteFrameList(AbsoluteFrameList&& aOther) = default;
  AbsoluteFrameList& operator=(AbsoluteFrameList&& aOther) = default;

#ifdef DEBUG
  // XXXbz Does this need a debug-only assignment operator that nulls out the
  // childList in the AbsoluteFrameList we're copying?  Introducing a difference
  // between debug and non-debug behavior seems bad, so I guess not...
  ~AbsoluteFrameList() {
    NS_ASSERTION(!FirstChild(),
                 "Dangling child list.  Someone forgot to insert it?");
  }
#endif
};
}  // namespace mozilla

// -----------------------------------------------------------

// Structure for saving the existing state when pushing/poping containing
// blocks. The destructor restores the state to its previous state
class MOZ_STACK_CLASS nsFrameConstructorSaveState {
 public:
  ~nsFrameConstructorSaveState();

 private:
  // Pointer to struct whose data we save/restore.
  AbsoluteFrameList* mList = nullptr;

  // The saved pointer to the fixed list.
  AbsoluteFrameList* mSavedFixedList = nullptr;

  // Copy of original frame list. This can be the original absolute list or a
  // float list.
  AbsoluteFrameList mSavedList;

  // The name of the child list in which our frames would belong.
  mozilla::FrameChildListID mChildListID = FrameChildListID::Principal;
  nsFrameConstructorState* mState = nullptr;

  friend class nsFrameConstructorState;
};

// Structure used for maintaining state information during the
// frame construction process
class MOZ_STACK_CLASS nsFrameConstructorState {
 public:
  nsPresContext* mPresContext;
  PresShell* mPresShell;
  nsCSSFrameConstructor* mFrameConstructor;

  // Containing block information for out-of-flow frames.
  //
  // Floats are easy. Whatever is our float CB.
  //
  // Regular abspos elements are easy too. Its containing block can be the
  // nearest abspos element, or the ICB (the canvas frame).
  //
  // Top layer abspos elements are always children of the ICB, but we can get
  // away with having two different lists (mAbsoluteList and
  // mTopLayerAbsoluteList), because because top layer frames cause
  // non-top-layer frames to be contained inside (so any descendants of a top
  // layer abspos can never share containing block with it, unless they're also
  // in the top layer).
  //
  // Regular fixed elements however are trickier. Fixed elements can be
  // contained in one of three lists:
  //
  //  * mAbsoluteList, if our abspos cb is also a fixpos cb (e.g., is
  //                   transformed or has a filter).
  //
  //  * mAncestorFixedList, if the fixpos cb is an ancestor element other than
  //                        the viewport frame, (so, a transformed / filtered
  //                        ancestor).
  //
  //  * mRealFixedList, which is also the fixed list used for the top layer
  //                    fixed items, which is the fixed list of the viewport
  //                    frame.
  //
  // It is important that mRealFixedList is shared between regular and top layer
  // fixpos elements, since no-top-layer descendants of top layer fixed elements
  // could share ICB and vice versa, so without that there would be no guarantee
  // of layout ordering between them.
  AbsoluteFrameList mFloatedList;
  AbsoluteFrameList mAbsoluteList;
  AbsoluteFrameList mTopLayerAbsoluteList;
  AbsoluteFrameList mAncestorFixedList;
  AbsoluteFrameList mRealFixedList;

  // Never null, always pointing to one of the lists documented above.
  AbsoluteFrameList* mFixedList;

  // What `page: auto` resolves to. This is the used page-name of the parent
  // frame. Updated by AutoFrameConstructionPageName.
  const nsAtom* mAutoPageNameValue = nullptr;

  nsCOMPtr<nsILayoutHistoryState> mFrameState;
  // These bits will be added to the state bits of any frame we construct
  // using this state.
  nsFrameState mAdditionalStateBits = NS_FRAME_STATE_NONE;

  // If false (which is the default) then call SetPrimaryFrame() as needed
  // during frame construction.  If true, don't make any SetPrimaryFrame()
  // calls, except for generated content which doesn't have a primary frame
  // yet.  The mCreatingExtraFrames == true mode is meant to be used for
  // construction of random "extra" frames for elements via normal frame
  // construction APIs (e.g. replication of things across pages in paginated
  // mode).
  bool mCreatingExtraFrames;

  // This keeps track of whether we have found a "rendered legend" for
  // the current FieldSetFrame.
  bool mHasRenderedLegend;

  nsTArray<RefPtr<nsIContent>> mGeneratedContentWithInitializer;

#ifdef DEBUG
  // Record the float containing block candidate passed into
  // MaybePushFloatContainingBlock() to keep track that we've call the method to
  // handle the float CB scope before processing the CB's children. It is reset
  // in ConstructFramesFromItemList().
  nsContainerFrame* mFloatCBCandidate = nullptr;
#endif

  // Constructor
  // Use the passed-in history state.
  nsFrameConstructorState(
      PresShell* aPresShell, nsContainerFrame* aFixedContainingBlock,
      nsContainerFrame* aAbsoluteContainingBlock,
      nsContainerFrame* aFloatContainingBlock,
      already_AddRefed<nsILayoutHistoryState> aHistoryState);
  // Get the history state from the pres context's pres shell.
  nsFrameConstructorState(PresShell* aPresShell,
                          nsContainerFrame* aFixedContainingBlock,
                          nsContainerFrame* aAbsoluteContainingBlock,
                          nsContainerFrame* aFloatContainingBlock);

  ~nsFrameConstructorState();

  // Process the frame insertions for all the out-of-flow nsAbsoluteItems.
  void ProcessFrameInsertionsForAllLists();

  // Function to push the existing absolute containing block state and
  // create a new scope. Code that uses this function should get matching
  // logic in GetAbsoluteContainingBlock.
  // Also makes aNewAbsoluteContainingBlock the containing block for
  // fixed-pos elements if necessary.
  // aPositionedFrame is the frame whose style actually makes
  // aNewAbsoluteContainingBlock a containing block. E.g. for a scrollable
  // element aPositionedFrame is the element's primary frame and
  // aNewAbsoluteContainingBlock is the scrolled frame.
  void PushAbsoluteContainingBlock(
      nsContainerFrame* aNewAbsoluteContainingBlock, nsIFrame* aPositionedFrame,
      nsFrameConstructorSaveState& aSaveState);

  // Function to forbid floats descendants under aFloatCBCandidate, or open a
  // new float containing block scope for aFloatCBCandidate. The current
  // state is saved in aSaveState if a new scope is pushed.
  void MaybePushFloatContainingBlock(nsContainerFrame* aFloatCBCandidate,
                                     nsFrameConstructorSaveState& aSaveState);

  // Helper function for MaybePushFloatContainingBlock().
  void PushFloatContainingBlock(nsContainerFrame* aNewFloatContainingBlock,
                                nsFrameConstructorSaveState& aSaveState);

  // Function to return the proper geometric parent for a frame with display
  // struct given by aStyleDisplay and parent's frame given by
  // aContentParentFrame.
  nsContainerFrame* GetGeometricParent(
      const nsStyleDisplay& aStyleDisplay,
      nsContainerFrame* aContentParentFrame) const;

  // Collect absolute frames in mAbsoluteList which are proper descendants
  // of aNewParent, and reparent them to aNewParent.
  //
  // Note: This function does something unusual that moves absolute items
  // after their frames are constructed under a column hierarchy which has
  // column-span elements. Do not use this if you're not dealing with
  // columns.
  void ReparentAbsoluteItems(nsContainerFrame* aNewParent);

  // Collect floats in mFloatedList which are proper descendants of aNewParent,
  // and reparent them to aNewParent.
  //
  // Note: This function does something unusual that moves floats after their
  // frames are constructed under a column hierarchy which has column-span
  // elements. Do not use this if you're not dealing with columns.
  void ReparentFloats(nsContainerFrame* aNewParent);

  /**
   * Function to add a new frame to the right frame list.  This MUST be called
   * on frames before their children have been processed if the frames might
   * conceivably be out-of-flow; otherwise cleanup in error cases won't work
   * right.  Also, this MUST be called on frames after they have been
   * initialized.
   * @param aNewFrame the frame to add
   * @param aFrameList the list to add in-flow frames to
   * @param aContent the content pointer for aNewFrame
   * @param aParentFrame the parent frame for the content if it were in-flow
   * @param aCanBePositioned pass false if the frame isn't allowed to be
   *        positioned
   * @param aCanBeFloated pass false if the frame isn't allowed to be
   *        floated
   */
  void AddChild(nsIFrame* aNewFrame, nsFrameList& aFrameList,
                nsIContent* aContent, nsContainerFrame* aParentFrame,
                bool aCanBePositioned = true, bool aCanBeFloated = true,
                bool aInsertAfter = false,
                nsIFrame* aInsertAfterFrame = nullptr);

  /**
   * Function to return the fixed-pos element list.  Normally this will just
   * hand back the fixed-pos element list, but in case we're dealing with a
   * transformed element that's acting as an abs-pos and fixed-pos container,
   * we'll hand back the abs-pos list.  Callers should use this function if they
   * want to get the list acting as the fixed-pos item parent.
   */
  AbsoluteFrameList& GetFixedList() { return *mFixedList; }
  const AbsoluteFrameList& GetFixedList() const { return *mFixedList; }

 protected:
  friend class nsFrameConstructorSaveState;

  /**
   * ProcessFrameInsertions takes the frames in aFrameList and adds them as
   * kids to the aChildListID child list of |aFrameList.containingBlock|.
   */
  void ProcessFrameInsertions(AbsoluteFrameList& aFrameList,
                              mozilla::FrameChildListID aChildListID);

  /**
   * GetOutOfFlowFrameList selects the out-of-flow frame list the new
   * frame should be added to. If the frame shouldn't be added to any
   * out-of-flow list, it returns nullptr. The corresponding type of
   * placeholder is also returned via the aPlaceholderType parameter
   * if this method doesn't return nullptr. The caller should check
   * whether the returned list really has a containing block.
   */
  AbsoluteFrameList* GetOutOfFlowFrameList(nsIFrame* aNewFrame,
                                           bool aCanBePositioned,
                                           bool aCanBeFloated,
                                           nsFrameState* aPlaceholderType);
};

nsFrameConstructorState::nsFrameConstructorState(
    PresShell* aPresShell, nsContainerFrame* aFixedContainingBlock,
    nsContainerFrame* aAbsoluteContainingBlock,
    nsContainerFrame* aFloatContainingBlock,
    already_AddRefed<nsILayoutHistoryState> aHistoryState)
    : mPresContext(aPresShell->GetPresContext()),
      mPresShell(aPresShell),
      mFrameConstructor(aPresShell->FrameConstructor()),
      mFloatedList(aFloatContainingBlock),
      mAbsoluteList(aAbsoluteContainingBlock),
      mTopLayerAbsoluteList(mFrameConstructor->GetCanvasFrame()),
      mAncestorFixedList(aFixedContainingBlock),
      mRealFixedList(
          static_cast<nsContainerFrame*>(mFrameConstructor->GetRootFrame())),
      // See PushAbsoluteContaningBlock below
      mFrameState(aHistoryState),
      mCreatingExtraFrames(false),
      mHasRenderedLegend(false) {
  MOZ_COUNT_CTOR(nsFrameConstructorState);
  mFixedList = [&] {
    if (aFixedContainingBlock == aAbsoluteContainingBlock) {
      return &mAbsoluteList;
    }
    if (aAbsoluteContainingBlock == mRealFixedList.mContainingBlock) {
      return &mRealFixedList;
    }
    return &mAncestorFixedList;
  }();
}

nsFrameConstructorState::nsFrameConstructorState(
    PresShell* aPresShell, nsContainerFrame* aFixedContainingBlock,
    nsContainerFrame* aAbsoluteContainingBlock,
    nsContainerFrame* aFloatContainingBlock)
    : nsFrameConstructorState(
          aPresShell, aFixedContainingBlock, aAbsoluteContainingBlock,
          aFloatContainingBlock,
          aPresShell->GetDocument()->GetLayoutHistoryState()) {}

nsFrameConstructorState::~nsFrameConstructorState() {
  MOZ_COUNT_DTOR(nsFrameConstructorState);
  ProcessFrameInsertionsForAllLists();
  for (auto& content : Reversed(mGeneratedContentWithInitializer)) {
    content->RemoveProperty(nsGkAtoms::genConInitializerProperty);
  }
}

void nsFrameConstructorState::ProcessFrameInsertionsForAllLists() {
  ProcessFrameInsertions(mFloatedList, FrameChildListID::Float);
  ProcessFrameInsertions(mAbsoluteList, FrameChildListID::Absolute);
  ProcessFrameInsertions(mTopLayerAbsoluteList, FrameChildListID::Absolute);
  ProcessFrameInsertions(*mFixedList, FrameChildListID::Absolute);
  ProcessFrameInsertions(mRealFixedList, FrameChildListID::Absolute);
}

void nsFrameConstructorState::PushAbsoluteContainingBlock(
    nsContainerFrame* aNewAbsoluteContainingBlock, nsIFrame* aPositionedFrame,
    nsFrameConstructorSaveState& aSaveState) {
  MOZ_ASSERT(!!aNewAbsoluteContainingBlock == !!aPositionedFrame,
             "We should have both or none");
  aSaveState.mList = &mAbsoluteList;
  aSaveState.mChildListID = FrameChildListID::Absolute;
  aSaveState.mState = this;
  aSaveState.mSavedList = std::move(mAbsoluteList);
  aSaveState.mSavedFixedList = mFixedList;
  mAbsoluteList = AbsoluteFrameList(aNewAbsoluteContainingBlock);
  mFixedList = [&] {
    if (!aPositionedFrame || aPositionedFrame->IsFixedPosContainingBlock()) {
      // See if we need to treat abspos and fixedpos the same. This happens if
      // we're a transformed/filtered/etc element, or if we force a null abspos
      // containing block (for mathml for example).
      return &mAbsoluteList;
    }
    if (aPositionedFrame->StyleDisplay()->mTopLayer == StyleTopLayer::Auto) {
      // If our new CB is in the top layer, and isn't a fixed CB itself, we also
      // escape the usual containment.
      return &mRealFixedList;
    }
    if (mFixedList == &mAbsoluteList) {
      // If we were pointing to our old absolute list, keep pointing to it.
      return &aSaveState.mSavedList;
    }
    // Otherwise keep pointing to the current thing (another ancestor's
    // absolute list, or the real fixed list, doesn't matter).
    return mFixedList;
  }();

  if (aNewAbsoluteContainingBlock &&
      !aNewAbsoluteContainingBlock->IsAbsoluteContainer()) {
    aNewAbsoluteContainingBlock->MarkAsAbsoluteContainingBlock();
  }
}

void nsFrameConstructorState::MaybePushFloatContainingBlock(
    nsContainerFrame* aFloatCBCandidate,
    nsFrameConstructorSaveState& aSaveState) {
  // The logic here needs to match the logic in GetFloatContainingBlock().
  if (ShouldSuppressFloatingOfDescendants(aFloatCBCandidate)) {
    // Pushing a null float containing block forbids any frames from being
    // floated until a new float containing block is pushed. See implementation
    // of nsFrameConstructorState::AddChild().
    //
    // XXX we should get rid of null float containing blocks and teach the
    // various frame classes to deal with floats instead.
    PushFloatContainingBlock(nullptr, aSaveState);
  } else if (aFloatCBCandidate->IsFloatContainingBlock()) {
    PushFloatContainingBlock(aFloatCBCandidate, aSaveState);
  }

#ifdef DEBUG
  mFloatCBCandidate = aFloatCBCandidate;
#endif
}

void nsFrameConstructorState::PushFloatContainingBlock(
    nsContainerFrame* aNewFloatContainingBlock,
    nsFrameConstructorSaveState& aSaveState) {
  MOZ_ASSERT(!aNewFloatContainingBlock ||
                 aNewFloatContainingBlock->IsFloatContainingBlock(),
             "Please push a real float containing block!");
  NS_ASSERTION(
      !aNewFloatContainingBlock ||
          !ShouldSuppressFloatingOfDescendants(aNewFloatContainingBlock),
      "We should not push a frame that is supposed to _suppress_ "
      "floats as a float containing block!");
  aSaveState.mList = &mFloatedList;
  aSaveState.mSavedList = std::move(mFloatedList);
  aSaveState.mChildListID = FrameChildListID::Float;
  aSaveState.mState = this;
  mFloatedList = AbsoluteFrameList(aNewFloatContainingBlock);
}

nsContainerFrame* nsFrameConstructorState::GetGeometricParent(
    const nsStyleDisplay& aStyleDisplay,
    nsContainerFrame* aContentParentFrame) const {
  // If there is no container for a fixed, absolute, or floating root
  // frame, we will ignore the positioning.  This hack is originally
  // brought to you by the letter T: tables, since other roots don't
  // even call into this code.  See bug 178855.
  //
  // XXX Disabling positioning in this case is a hack.  If one was so inclined,
  // one could support this either by (1) inserting a dummy block between the
  // table and the canvas or (2) teaching the canvas how to reflow positioned
  // elements. (1) has the usual problems when multiple frames share the same
  // content (notice all the special cases in this file dealing with inner
  // tables and table wrappers which share the same content). (2) requires some
  // work and possible factoring.
  //
  // XXXbz couldn't we just force position to "static" on roots and
  // float to "none"?  That's OK per CSS 2.1, as far as I can tell.

  if (aContentParentFrame && aContentParentFrame->IsInSVGTextSubtree()) {
    return aContentParentFrame;
  }

  if (aStyleDisplay.IsFloatingStyle() && mFloatedList.mContainingBlock) {
    NS_ASSERTION(!aStyleDisplay.IsAbsolutelyPositionedStyle(),
                 "Absolutely positioned _and_ floating?");
    return mFloatedList.mContainingBlock;
  }

  if (aStyleDisplay.mTopLayer != StyleTopLayer::None) {
    MOZ_ASSERT(aStyleDisplay.mTopLayer == StyleTopLayer::Auto,
               "-moz-top-layer should be either none or auto");
    MOZ_ASSERT(aStyleDisplay.IsAbsolutelyPositionedStyle(),
               "Top layer items should always be absolutely positioned");
    if (aStyleDisplay.mPosition == StylePositionProperty::Fixed) {
      MOZ_ASSERT(mRealFixedList.mContainingBlock, "No root frame?");
      return mRealFixedList.mContainingBlock;
    }
    MOZ_ASSERT(aStyleDisplay.mPosition == StylePositionProperty::Absolute);
    MOZ_ASSERT(mTopLayerAbsoluteList.mContainingBlock);
    return mTopLayerAbsoluteList.mContainingBlock;
  }

  if (aStyleDisplay.mPosition == StylePositionProperty::Absolute &&
      mAbsoluteList.mContainingBlock) {
    return mAbsoluteList.mContainingBlock;
  }

  if (aStyleDisplay.mPosition == StylePositionProperty::Fixed &&
      mFixedList->mContainingBlock) {
    return mFixedList->mContainingBlock;
  }

  return aContentParentFrame;
}

void nsFrameConstructorState::ReparentAbsoluteItems(
    nsContainerFrame* aNewParent) {
  // Bug 1491727: This function might not conform to the spec. See
  // https://github.com/w3c/csswg-drafts/issues/1894.

  MOZ_ASSERT(aNewParent->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR),
             "Restrict the usage under column hierarchy.");

  AbsoluteFrameList newAbsoluteItems(aNewParent);

  nsIFrame* current = mAbsoluteList.FirstChild();
  while (current) {
    nsIFrame* placeholder = current->GetPlaceholderFrame();

    if (nsLayoutUtils::IsProperAncestorFrame(aNewParent, placeholder)) {
      nsIFrame* next = current->GetNextSibling();
      mAbsoluteList.RemoveFrame(current);
      newAbsoluteItems.AppendFrame(aNewParent, current);
      current = next;
    } else {
      current = current->GetNextSibling();
    }
  }

  if (newAbsoluteItems.NotEmpty()) {
    // ~nsFrameConstructorSaveState() will move newAbsoluteItems to
    // aNewParent's absolute child list.
    nsFrameConstructorSaveState absoluteSaveState;

    // It doesn't matter whether aNewParent has position style or not. Caller
    // won't call us if we can't have absolute children.
    PushAbsoluteContainingBlock(aNewParent, aNewParent, absoluteSaveState);
    mAbsoluteList = std::move(newAbsoluteItems);
  }
}

void nsFrameConstructorState::ReparentFloats(nsContainerFrame* aNewParent) {
  MOZ_ASSERT(aNewParent->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR),
             "Restrict the usage under column hierarchy.");
  MOZ_ASSERT(
      aNewParent->IsFloatContainingBlock(),
      "Why calling this method if aNewParent is not a float containing block?");

  // Gather floats that should reparent under aNewParent.
  AbsoluteFrameList floats(aNewParent);
  nsIFrame* current = mFloatedList.FirstChild();
  while (current) {
    nsIFrame* placeholder = current->GetPlaceholderFrame();
    nsIFrame* next = current->GetNextSibling();
    if (nsLayoutUtils::IsProperAncestorFrame(aNewParent, placeholder)) {
      mFloatedList.RemoveFrame(current);
      floats.AppendFrame(aNewParent, current);
    }
    current = next;
  }

  if (floats.NotEmpty()) {
    // Make floats move into aNewParent's float child list in
    // ~nsFrameConstructorSaveState() when destructing floatSaveState.
    nsFrameConstructorSaveState floatSaveState;
    PushFloatContainingBlock(aNewParent, floatSaveState);
    mFloatedList = std::move(floats);
  }
}

AbsoluteFrameList* nsFrameConstructorState::GetOutOfFlowFrameList(
    nsIFrame* aNewFrame, bool aCanBePositioned, bool aCanBeFloated,
    nsFrameState* aPlaceholderType) {
  const nsStyleDisplay* disp = aNewFrame->StyleDisplay();
  if (aCanBeFloated && disp->IsFloatingStyle()) {
    *aPlaceholderType = PLACEHOLDER_FOR_FLOAT;
    return &mFloatedList;
  }

  if (aCanBePositioned) {
    if (disp->mTopLayer != StyleTopLayer::None) {
      *aPlaceholderType = PLACEHOLDER_FOR_TOPLAYER;
      if (disp->mPosition == StylePositionProperty::Fixed) {
        *aPlaceholderType |= PLACEHOLDER_FOR_FIXEDPOS;
        return &mRealFixedList;
      }
      *aPlaceholderType |= PLACEHOLDER_FOR_ABSPOS;
      return &mTopLayerAbsoluteList;
    }
    if (disp->mPosition == StylePositionProperty::Absolute) {
      *aPlaceholderType = PLACEHOLDER_FOR_ABSPOS;
      return &mAbsoluteList;
    }
    if (disp->mPosition == StylePositionProperty::Fixed) {
      *aPlaceholderType = PLACEHOLDER_FOR_FIXEDPOS;
      return mFixedList;
    }
  }
  return nullptr;
}

void nsFrameConstructorState::AddChild(
    nsIFrame* aNewFrame, nsFrameList& aFrameList, nsIContent* aContent,
    nsContainerFrame* aParentFrame, bool aCanBePositioned, bool aCanBeFloated,
    bool aInsertAfter, nsIFrame* aInsertAfterFrame) {
  MOZ_ASSERT(!aNewFrame->GetNextSibling(), "Shouldn't happen");

  nsFrameState placeholderType = NS_FRAME_STATE_NONE;
  AbsoluteFrameList* outOfFlowFrameList = GetOutOfFlowFrameList(
      aNewFrame, aCanBePositioned, aCanBeFloated, &placeholderType);

  // The comments in GetGeometricParent regarding root table frames
  // all apply here, unfortunately. Thus, we need to check whether
  // the returned frame items really has containing block.
  nsFrameList* frameList;
  if (outOfFlowFrameList && outOfFlowFrameList->mContainingBlock) {
    MOZ_ASSERT(aNewFrame->GetParent() == outOfFlowFrameList->mContainingBlock,
               "Parent of the frame is not the containing block?");
    frameList = outOfFlowFrameList;
  } else {
    frameList = &aFrameList;
    placeholderType = NS_FRAME_STATE_NONE;
  }

  if (placeholderType != NS_FRAME_STATE_NONE) {
    NS_ASSERTION(frameList != &aFrameList,
                 "Putting frame in-flow _and_ want a placeholder?");
    nsIFrame* placeholderFrame =
        nsCSSFrameConstructor::CreatePlaceholderFrameFor(
            mPresShell, aContent, aNewFrame, aParentFrame, nullptr,
            placeholderType);

    placeholderFrame->AddStateBits(mAdditionalStateBits);
    // Add the placeholder frame to the flow
    aFrameList.AppendFrame(nullptr, placeholderFrame);
  }
#ifdef DEBUG
  else {
    NS_ASSERTION(aNewFrame->GetParent() == aParentFrame,
                 "In-flow frame has wrong parent");
  }
#endif

  if (aInsertAfter) {
    frameList->InsertFrame(nullptr, aInsertAfterFrame, aNewFrame);
  } else {
    frameList->AppendFrame(nullptr, aNewFrame);
  }
}

// Some of this function's callers recurse 1000 levels deep in crashtests. On
// platforms where stack limits are low, we can't afford to incorporate this
// function's `AutoTArray`s into its callers' stack frames, so disable inlining.
MOZ_NEVER_INLINE void nsFrameConstructorState::ProcessFrameInsertions(
    AbsoluteFrameList& aFrameList, FrameChildListID aChildListID) {
  MOZ_ASSERT(&aFrameList == &mFloatedList || &aFrameList == &mAbsoluteList ||
             &aFrameList == &mTopLayerAbsoluteList ||
             &aFrameList == &mAncestorFixedList || &aFrameList == mFixedList ||
             &aFrameList == &mRealFixedList);
  MOZ_ASSERT_IF(&aFrameList == &mFloatedList,
                aChildListID == FrameChildListID::Float);
  MOZ_ASSERT_IF(&aFrameList == &mAbsoluteList || &aFrameList == mFixedList,
                aChildListID == FrameChildListID::Absolute);
  MOZ_ASSERT_IF(&aFrameList == &mTopLayerAbsoluteList,
                aChildListID == FrameChildListID::Absolute);
  MOZ_ASSERT_IF(&aFrameList == mFixedList && &aFrameList != &mAbsoluteList,
                aChildListID == FrameChildListID::Absolute);
  MOZ_ASSERT_IF(&aFrameList == &mAncestorFixedList,
                aChildListID == FrameChildListID::Absolute);
  MOZ_ASSERT_IF(&aFrameList == &mRealFixedList,
                aChildListID == FrameChildListID::Absolute);

  if (aFrameList.IsEmpty()) {
    return;
  }

  nsContainerFrame* containingBlock = aFrameList.mContainingBlock;

  NS_ASSERTION(containingBlock, "Child list without containing block?");

  // Insert the frames hanging out in aItems.  We can use SetInitialChildList()
  // if the containing block hasn't been reflowed yet (so NS_FRAME_FIRST_REFLOW
  // is set) and doesn't have any frames in the aChildListID child list yet.
  const nsFrameList& childList = containingBlock->GetChildList(aChildListID);
  if (childList.IsEmpty() &&
      containingBlock->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // If we're injecting absolutely positioned frames, inject them on the
    // absolute containing block
    if (aChildListID == FrameChildListID::Absolute) {
      containingBlock->GetAbsoluteContainingBlock()->SetInitialChildList(
          containingBlock, aChildListID, std::move(aFrameList));
    } else {
      containingBlock->SetInitialChildList(aChildListID, std::move(aFrameList));
    }
  } else if (childList.IsEmpty() ||
             aChildListID == FrameChildListID::Absolute) {
    // The order is not important for abs-pos/fixed-pos frame list, just
    // append the frame items to the list directly.
    mFrameConstructor->AppendFrames(containingBlock, aChildListID,
                                    std::move(aFrameList));
  } else {
    MOZ_ASSERT(aChildListID == FrameChildListID::Float);
    // Note that whether the frame construction context is doing an append or
    // not is not helpful here, since it could be appending to some frame in
    // the middle of the document, which means we're not necessarily
    // appending to the children of the containing block.
    //
    // We need to make sure the 'append to the end of document' case is fast.
    // So first test the last child of the containing block
    nsIFrame* lastChild = childList.LastChild();
    lastChild = lastChild->FirstContinuation()->GetPlaceholderFrame();

    // CompareTreePosition uses placeholder hierarchy for out of flow frames,
    // so this will make out-of-flows respect the ordering of placeholders,
    // which is great because it takes care of anonymous content.
    nsIFrame* firstNewFrame = aFrameList.FirstChild();
    firstNewFrame = firstNewFrame->GetPlaceholderFrame();

    // Cache the ancestor chain so that we can reuse it if needed.
    AutoTArray<const nsIFrame*, 20> firstNewFrameAncestors;
    const nsIFrame* notCommonAncestor = nsLayoutUtils::FillAncestors(
        firstNewFrame, containingBlock, &firstNewFrameAncestors);

    if (nsLayoutUtils::CompareTreePosition(
            lastChild, firstNewFrame, firstNewFrameAncestors,
            notCommonAncestor ? containingBlock : nullptr) < 0) {
      // lastChild comes before the new children, so just append
      mFrameConstructor->AppendFrames(containingBlock, aChildListID,
                                      std::move(aFrameList));
    } else {
      // Try the other children. First collect them to an array so that a
      // reasonable fast binary search can be used to find the insertion point.
      AutoTArray<std::pair<nsIFrame*, nsPlaceholderFrame*>, 128> children;
      for (nsIFrame* f : childList) {
        children.AppendElement(
            std::make_pair(f, f->FirstContinuation()->GetPlaceholderFrame()));
      }

      nsIFrame* insertionPoint = nullptr;
      int32_t imin = 0;
      int32_t max = children.Length();
      while (max > imin) {
        int32_t imid = imin + ((max - imin) / 2);
        const auto& pair = children[imid];
        int32_t compare = nsLayoutUtils::CompareTreePosition(
            pair.second, firstNewFrame, firstNewFrameAncestors,
            notCommonAncestor ? containingBlock : nullptr);
        if (compare > 0) {
          // f is after the new frame.
          max = imid;
          insertionPoint = imid > 0 ? children[imid - 1].first : nullptr;
        } else if (compare < 0) {
          // f is before the new frame.
          imin = imid + 1;
          insertionPoint = pair.first;
        } else {
          // This is for the old behavior. Should be removed once it is
          // guaranteed that CompareTreePosition can't return 0!
          // See bug 928645.
          NS_WARNING("Something odd happening???");
          insertionPoint = nullptr;
          for (auto [frame, placeholder] : children) {
            if (nsLayoutUtils::CompareTreePosition(
                    placeholder, firstNewFrame, firstNewFrameAncestors,
                    notCommonAncestor ? containingBlock : nullptr) > 0) {
              break;
            }
            insertionPoint = frame;
          }
          break;
        }
      }
      mFrameConstructor->InsertFrames(containingBlock, aChildListID,
                                      insertionPoint, std::move(aFrameList));
    }
  }

  MOZ_ASSERT(aFrameList.IsEmpty(), "How did that happen?");
}

nsFrameConstructorSaveState::~nsFrameConstructorSaveState() {
  // Restore the state
  if (mList) {
    MOZ_ASSERT(mState, "Can't have mList set without having a state!");
    mState->ProcessFrameInsertions(*mList, mChildListID);

    if (mList == &mState->mAbsoluteList) {
      mState->mAbsoluteList = std::move(mSavedList);
      mState->mFixedList = mSavedFixedList;
    } else {
      mState->mFloatedList = std::move(mSavedList);
    }

    MOZ_ASSERT(mSavedList.IsEmpty(),
               "Frames in mSavedList should've moved back into mState!");
    MOZ_ASSERT(!mList->LastChild() || !mList->LastChild()->GetNextSibling(),
               "Something corrupted our list!");
  }
}

/**
 * Moves aFrameList from aOldParent to aNewParent.  This updates the parent
 * pointer of the frames in the list, and reparents their views as needed.
 * nsIFrame::SetParent sets the NS_FRAME_HAS_VIEW bit on aNewParent and its
 * ancestors as needed. Then it sets the list as the initial child list
 * on aNewParent, unless aNewParent either already has kids or has been
 * reflowed; in that case it appends the new frames.  Note that this
 * method differs from ReparentFrames in that it doesn't change the kids'
 * style.
 */
// XXXbz Since this is only used for {ib} splits, could we just copy the view
// bits from aOldParent to aNewParent and then use the
// nsFrameList::ApplySetParent?  That would still leave us doing two passes
// over the list, of course; if we really wanted to we could factor out the
// relevant part of ReparentFrameViewList, I suppose...  Or just get rid of
// views, which would make most of this function go away.
static void MoveChildrenTo(nsIFrame* aOldParent, nsContainerFrame* aNewParent,
                           nsFrameList& aFrameList) {
  aFrameList.ApplySetParent(aNewParent);

  if (aNewParent->PrincipalChildList().IsEmpty() &&
      aNewParent->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    aNewParent->SetInitialChildList(FrameChildListID::Principal,
                                    std::move(aFrameList));
  } else {
    aNewParent->AppendFrames(FrameChildListID::Principal,
                             std::move(aFrameList));
  }
}

static void EnsureAutoPageName(nsFrameConstructorState& aState,
                               const nsContainerFrame* const aFrame) {
  // Check if we need to figure out our used page name.
  // When building the entire document, this should only happen for the
  // root, which will mean the loop will immediately end. Either way, this will
  // only happen once for each time the frame constructor is run.
  if (aState.mAutoPageNameValue) {
    return;
  }

  for (const nsContainerFrame* frame = aFrame; frame;
       frame = frame->GetParent()) {
    if (const nsAtom* maybePageName = frame->GetStylePageName()) {
      aState.mAutoPageNameValue = maybePageName;
      return;
    }
  }
  // Ensure that a root with `page: auto` gets an empty page name
  // https://drafts.csswg.org/css-page-3/#using-named-pages
  aState.mAutoPageNameValue = nsGkAtoms::_empty;
}

nsCSSFrameConstructor::AutoFrameConstructionPageName::
    AutoFrameConstructionPageName(nsFrameConstructorState& aState,
                                  nsIFrame* const aFrame)
    : mState(aState), mNameToRestore(nullptr) {
  if (!aState.mPresContext->IsPaginated()) {
    MOZ_ASSERT(!aState.mAutoPageNameValue,
               "Page name should not have been set");
    return;
  }
#ifdef DEBUG
  MOZ_ASSERT(!aFrame->mWasVisitedByAutoFrameConstructionPageName,
             "Frame should only have been visited once");
  aFrame->mWasVisitedByAutoFrameConstructionPageName = true;
#endif

  EnsureAutoPageName(aState, aFrame->GetParent());
  mNameToRestore = aState.mAutoPageNameValue;

  MOZ_ASSERT(mNameToRestore,
             "Page name should have been found by EnsureAutoPageName");
  if (const nsAtom* maybePageName = aFrame->GetStylePageName()) {
    aState.mAutoPageNameValue = maybePageName;
  }
  aFrame->SetAutoPageValue(aState.mAutoPageNameValue);
}

nsCSSFrameConstructor::AutoFrameConstructionPageName::
    ~AutoFrameConstructionPageName() {
  // This isn't actually useful when not in paginated layout, but it's very
  // likely cheaper to unconditionally write this pointer than to test for
  // paginated layout and then branch on the result.
  mState.mAutoPageNameValue = mNameToRestore;
}

//----------------------------------------------------------------------

nsCSSFrameConstructor::nsCSSFrameConstructor(Document* aDocument,
                                             PresShell* aPresShell)
    : nsFrameManager(aPresShell),
      mDocument(aDocument),
      mFirstFreeFCItem(nullptr),
      mFCItemsInUse(0),
      mCurrentDepth(0),
      mQuotesDirty(false),
      mCountersDirty(false),
      mAlwaysCreateFramesForIgnorableWhitespace(false),
      mRemovingContent(false) {
#ifdef DEBUG
  static bool gFirstTime = true;
  if (gFirstTime) {
    gFirstTime = false;
    char* flags = PR_GetEnv("GECKO_FRAMECTOR_DEBUG_FLAGS");
    if (flags) {
      bool error = false;
      for (;;) {
        char* comma = strchr(flags, ',');
        if (comma) *comma = '\0';

        bool found = false;
        FrameCtorDebugFlags* flag = gFrameCtorDebugFlags;
        FrameCtorDebugFlags* limit = gFrameCtorDebugFlags + NUM_DEBUG_FLAGS;
        while (flag < limit) {
          if (nsCRT::strcasecmp(flag->name, flags) == 0) {
            *(flag->on) = true;
            printf("nsCSSFrameConstructor: setting %s debug flag on\n",
                   flag->name);
            found = true;
            break;
          }
          ++flag;
        }

        if (!found) error = true;

        if (!comma) break;

        *comma = ',';
        flags = comma + 1;
      }

      if (error) {
        printf("Here are the available GECKO_FRAMECTOR_DEBUG_FLAGS:\n");
        FrameCtorDebugFlags* flag = gFrameCtorDebugFlags;
        FrameCtorDebugFlags* limit = gFrameCtorDebugFlags + NUM_DEBUG_FLAGS;
        while (flag < limit) {
          printf("  %s\n", flag->name);
          ++flag;
        }
        printf(
            "Note: GECKO_FRAMECTOR_DEBUG_FLAGS is a comma separated list of "
            "flag\n");
        printf("names (no whitespace)\n");
      }
    }
  }
#endif
}

void nsCSSFrameConstructor::NotifyDestroyingFrame(nsIFrame* aFrame) {
  if (aFrame->StyleDisplay()->IsContainStyle()) {
    mContainStyleScopeManager.DestroyScopesFor(aFrame);
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_GENERATED_CONTENT) &&
      mContainStyleScopeManager.DestroyQuoteNodesFor(aFrame)) {
    QuotesDirty();
  }

  if (aFrame->HasAnyStateBits(NS_FRAME_HAS_CSS_COUNTER_STYLE) &&
      mContainStyleScopeManager.DestroyCounterNodesFor(aFrame)) {
    // Technically we don't need to update anything if we destroyed only
    // USE nodes.  However, this is unlikely to happen in the real world
    // since USE nodes generally go along with INCREMENT nodes.
    CountersDirty();
  }

  RestyleManager()->NotifyDestroyingFrame(aFrame);
}

struct nsGenConInitializer {
  UniquePtr<nsGenConNode> mNode;
  nsGenConList* mList;
  void (nsCSSFrameConstructor::*mDirtyAll)();

  nsGenConInitializer(UniquePtr<nsGenConNode> aNode, nsGenConList* aList,
                      void (nsCSSFrameConstructor::*aDirtyAll)())
      : mNode(std::move(aNode)), mList(aList), mDirtyAll(aDirtyAll) {}
};

already_AddRefed<nsIContent> nsCSSFrameConstructor::CreateGenConTextNode(
    nsFrameConstructorState& aState, const nsAString& aString,
    UniquePtr<nsGenConInitializer> aInitializer) {
  RefPtr<nsTextNode> content = new (mDocument->NodeInfoManager())
      nsTextNode(mDocument->NodeInfoManager());
  content->SetText(aString, false);
  if (aInitializer) {
    aInitializer->mNode->mText = content;
    content->SetProperty(nsGkAtoms::genConInitializerProperty,
                         aInitializer.release(),
                         nsINode::DeleteProperty<nsGenConInitializer>);
    aState.mGeneratedContentWithInitializer.AppendElement(content);
  }
  return content.forget();
}

void nsCSSFrameConstructor::CreateGeneratedContent(
    nsFrameConstructorState& aState, Element& aOriginatingElement,
    ComputedStyle& aPseudoStyle, const StyleContentItem& aItem,
    size_t aContentIndex, const FunctionRef<void(nsIContent*)> aAddChild) {
  using Type = StyleContentItem::Tag;
  // Get the content value
  const Type type = aItem.tag;

  switch (type) {
    case Type::Image: {
      RefPtr c = GeneratedImageContent::Create(*mDocument, aContentIndex);
      aAddChild(c);
      return;
    }

    case Type::String: {
      const auto string = aItem.AsString().AsString();
      if (string.IsEmpty()) {
        return;
      }
      RefPtr text =
          CreateGenConTextNode(aState, NS_ConvertUTF8toUTF16(string), nullptr);
      aAddChild(text);
      return;
    }

    case Type::Attr: {
      const auto& attr = aItem.AsAttr();
      RefPtr<nsAtom> attrName = attr.attribute.AsAtom();
      int32_t attrNameSpace = kNameSpaceID_None;
      RefPtr<nsAtom> ns = attr.namespace_url.AsAtom();
      if (!ns->IsEmpty()) {
        nsresult rv = nsNameSpaceManager::GetInstance()->RegisterNameSpace(
            ns.forget(), attrNameSpace);
        NS_ENSURE_SUCCESS_VOID(rv);
      }

      if (mDocument->IsHTMLDocument() && aOriginatingElement.IsHTMLElement()) {
        ToLowerCaseASCII(attrName);
      }

      RefPtr<nsAtom> fallback = attr.fallback.AsAtom();

      nsCOMPtr<nsIContent> content;
      NS_NewAttributeContent(mDocument->NodeInfoManager(), attrNameSpace,
                             attrName, fallback, getter_AddRefs(content));
      aAddChild(content);
      return;
    }

    case Type::Counter:
    case Type::Counters: {
      RefPtr<nsAtom> name;
      const StyleCounterStyle* style;
      nsString separator;
      if (type == Type::Counter) {
        const auto& counter = aItem.AsCounter();
        name = counter._0.AsAtom();
        style = &counter._1;
      } else {
        const auto& counters = aItem.AsCounters();
        name = counters._0.AsAtom();
        CopyUTF8toUTF16(counters._1.AsString(), separator);
        style = &counters._2;
      }

      auto* counterList = mContainStyleScopeManager.GetOrCreateCounterList(
          aOriginatingElement, name);
      auto node = MakeUnique<nsCounterUseNode>(
          *style, std::move(separator), aContentIndex,
          /* aAllCounters = */ type == Type::Counters);

      auto initializer = MakeUnique<nsGenConInitializer>(
          std::move(node), counterList, &nsCSSFrameConstructor::CountersDirty);
      RefPtr c = CreateGenConTextNode(aState, u""_ns, std::move(initializer));
      aAddChild(c);
      return;
    }
    case Type::OpenQuote:
    case Type::CloseQuote:
    case Type::NoOpenQuote:
    case Type::NoCloseQuote: {
      auto node = MakeUnique<nsQuoteNode>(type, aContentIndex);
      auto* quoteList =
          mContainStyleScopeManager.QuoteListFor(aOriginatingElement);
      auto initializer = MakeUnique<nsGenConInitializer>(
          std::move(node), quoteList, &nsCSSFrameConstructor::QuotesDirty);
      RefPtr c = CreateGenConTextNode(aState, u""_ns, std::move(initializer));
      aAddChild(c);
      return;
    }

    case Type::MozLabelContent: {
      nsAutoString accesskey;
      if (!aOriginatingElement.GetAttr(nsGkAtoms::accesskey, accesskey) ||
          accesskey.IsEmpty() || !LookAndFeel::GetMenuAccessKey()) {
        // Easy path: just return a regular value attribute content.
        nsCOMPtr<nsIContent> content;
        NS_NewAttributeContent(mDocument->NodeInfoManager(), kNameSpaceID_None,
                               nsGkAtoms::value, nsGkAtoms::_empty,
                               getter_AddRefs(content));
        aAddChild(content);
        return;
      }

      nsAutoString value;
      aOriginatingElement.GetAttr(nsGkAtoms::value, value);

      auto AppendAccessKeyLabel = [&] {
        // Always append accesskey text in uppercase, see bug 1806167.
        ToUpperCase(accesskey);
        nsAutoString accessKeyLabel = u"("_ns + accesskey + u")"_ns;
        if (!StringEndsWith(value, accessKeyLabel)) {
          if (InsertSeparatorBeforeAccessKey() && !value.IsEmpty() &&
              !NS_IS_SPACE(value.Last())) {
            value.Append(' ');
          }
          value.Append(accessKeyLabel);
        }
      };
      if (AlwaysAppendAccessKey()) {
        AppendAccessKeyLabel();
        RefPtr c = CreateGenConTextNode(aState, value, nullptr);
        aAddChild(c);
        return;
      }

      const auto accessKeyStart = [&]() -> Maybe<size_t> {
        nsAString::const_iterator start, end;
        value.BeginReading(start);
        value.EndReading(end);

        const auto originalStart = start;
        // not appending access key - do case-sensitive search
        // first
        bool found = true;
        if (!FindInReadable(accesskey, start, end)) {
          start = originalStart;
          // didn't find it - perform a case-insensitive search
          found = CaseInsensitiveFindInReadable(accesskey, start, end);
        }
        if (!found) {
          return Nothing();
        }
        return Some(Distance(originalStart, start));
      }();

      if (accessKeyStart.isNothing()) {
        AppendAccessKeyLabel();
        RefPtr c = CreateGenConTextNode(aState, value, nullptr);
        aAddChild(c);
        return;
      }

      if (*accessKeyStart != 0) {
        RefPtr beginning = CreateGenConTextNode(
            aState, Substring(value, 0, *accessKeyStart), nullptr);
        aAddChild(beginning);
      }

      {
        RefPtr accessKeyText = CreateGenConTextNode(
            aState, Substring(value, *accessKeyStart, accesskey.Length()),
            nullptr);
        RefPtr<nsIContent> underline =
            mDocument->CreateHTMLElement(nsGkAtoms::u);
        underline->AppendChildTo(accessKeyText, /* aNotify = */ false,
                                 IgnoreErrors());
        aAddChild(underline);
      }

      size_t accessKeyEnd = *accessKeyStart + accesskey.Length();
      if (accessKeyEnd != value.Length()) {
        RefPtr valueEnd = CreateGenConTextNode(
            aState, Substring(value, *accessKeyStart + accesskey.Length()),
            nullptr);
        aAddChild(valueEnd);
      }
      break;
    }
    case Type::MozAltContent: {
      // Use the "alt" attribute; if that fails and the node is an HTML
      // <input>, try the value attribute and then fall back to some default
      // localized text we have.
      // XXX what if the 'alt' attribute is added later, how will we
      // detect that and do the right thing here?
      if (aOriginatingElement.HasAttr(nsGkAtoms::alt)) {
        nsCOMPtr<nsIContent> content;
        NS_NewAttributeContent(mDocument->NodeInfoManager(), kNameSpaceID_None,
                               nsGkAtoms::alt, nsGkAtoms::_empty,
                               getter_AddRefs(content));
        aAddChild(content);
        return;
      }

      if (aOriginatingElement.IsHTMLElement(nsGkAtoms::input)) {
        if (aOriginatingElement.HasAttr(nsGkAtoms::value)) {
          nsCOMPtr<nsIContent> content;
          NS_NewAttributeContent(mDocument->NodeInfoManager(),
                                 kNameSpaceID_None, nsGkAtoms::value,
                                 nsGkAtoms::_empty, getter_AddRefs(content));
          aAddChild(content);
          return;
        }

        nsAutoString temp;
        nsContentUtils::GetMaybeLocalizedString(
            PropertiesFile::FORMS_PROPERTIES, "Submit", mDocument, temp);
        RefPtr c = CreateGenConTextNode(aState, temp, nullptr);
        aAddChild(c);
        return;
      }
      break;
    }
  }
}

void nsCSSFrameConstructor::CreateGeneratedContentFromListStyle(
    nsFrameConstructorState& aState, Element& aOriginatingElement,
    const ComputedStyle& aPseudoStyle,
    const FunctionRef<void(nsIContent*)> aAddChild) {
  const nsStyleList* styleList = aPseudoStyle.StyleList();
  if (!styleList->mListStyleImage.IsNone()) {
    RefPtr<nsIContent> child =
        GeneratedImageContent::CreateForListStyleImage(*mDocument);
    aAddChild(child);
    child = CreateGenConTextNode(aState, u" "_ns, nullptr);
    aAddChild(child);
    return;
  }
  CreateGeneratedContentFromListStyleType(aState, aOriginatingElement,
                                          aPseudoStyle, aAddChild);
}

void nsCSSFrameConstructor::CreateGeneratedContentFromListStyleType(
    nsFrameConstructorState& aState, Element& aOriginatingElement,
    const ComputedStyle& aPseudoStyle,
    const FunctionRef<void(nsIContent*)> aAddChild) {
  using Tag = StyleCounterStyle::Tag;
  const auto& styleType = aPseudoStyle.StyleList()->mListStyleType;
  switch (styleType.tag) {
    case Tag::None:
      return;
    case Tag::String: {
      nsDependentAtomString string(styleType.AsString().AsAtom());
      RefPtr<nsIContent> child = CreateGenConTextNode(aState, string, nullptr);
      aAddChild(child);
      return;
    }
    case Tag::Name:
    case Tag::Symbols:
      break;
  }

  auto node = MakeUnique<nsCounterUseNode>(nsCounterUseNode::ForLegacyBullet,
                                           styleType);
  if (styleType.IsName()) {
    nsAtom* name = styleType.AsName().AsAtom();
    if (name == nsGkAtoms::disc || name == nsGkAtoms::circle ||
        name == nsGkAtoms::square || name == nsGkAtoms::disclosure_closed ||
        name == nsGkAtoms::disclosure_open) {
      // We don't need a use node inserted for these.
      CounterStyle* counterStyle = mPresShell->GetPresContext()
                                       ->CounterStyleManager()
                                       ->ResolveCounterStyle(name);
      nsAutoString text;
      node->GetText(WritingMode(&aPseudoStyle), counterStyle, text);
      // Note that we're done with 'node' in this case.  It's not inserted into
      // any list so it's deleted when we return.
      RefPtr<nsIContent> child = CreateGenConTextNode(aState, text, nullptr);
      aAddChild(child);
      return;
    }
  }

  auto* counterList = mContainStyleScopeManager.GetOrCreateCounterList(
      aOriginatingElement, nsGkAtoms::list_item);
  auto initializer = MakeUnique<nsGenConInitializer>(
      std::move(node), counterList, &nsCSSFrameConstructor::CountersDirty);
  RefPtr<nsIContent> child =
      CreateGenConTextNode(aState, EmptyString(), std::move(initializer));
  aAddChild(child);
}

// Frames for these may not be leaves in the proper sense, but we still don't
// want to expose generated content on them. For the purposes of the page they
// should be leaves.
static bool HasUAWidget(const Element& aOriginatingElement) {
  const ShadowRoot* sr = aOriginatingElement.GetShadowRoot();
  return sr && sr->IsUAWidget();
}

/*
 * aParentFrame - the frame that should be the parent of the generated
 *   content.  This is the frame for the corresponding content node,
 *   which must not be a leaf frame.
 *
 * Any items created are added to aItems.
 *
 * We create an XML element (tag _moz_generated_content_before/after/marker)
 * representing the pseudoelement. We create a DOM node for each 'content'
 * item and make those nodes the children of the XML element. Then we create
 * a frame subtree for the XML element as if it were a regular child of
 * aParentFrame/aParentContent, giving the XML element the ::before, ::after
 * or ::marker style.
 */
void nsCSSFrameConstructor::CreateGeneratedContentItem(
    nsFrameConstructorState& aState, nsContainerFrame* aParentFrame,
    Element& aOriginatingElement, ComputedStyle& aStyle,
    PseudoStyleType aPseudoElement, FrameConstructionItemList& aItems,
    ItemFlags aExtraFlags) {
  MOZ_ASSERT(aPseudoElement == PseudoStyleType::Before ||
                 aPseudoElement == PseudoStyleType::After ||
                 aPseudoElement == PseudoStyleType::Marker ||
                 aPseudoElement == PseudoStyleType::Backdrop ||
                 aPseudoElement == PseudoStyleType::Checkmark ||
                 aPseudoElement == PseudoStyleType::PickerIcon,
             "unexpected aPseudoElement");

  if (aPseudoElement != PseudoStyleType::Backdrop &&
      aPseudoElement != PseudoStyleType::PickerIcon &&
      HasUAWidget(aOriginatingElement) &&
      !aOriginatingElement.IsHTMLElement(nsGkAtoms::details)) {
    // ::before / ::after / ::marker shouldn't work on <video> / <input>.
    return;
  }

  ServoStyleSet* styleSet = mPresShell->StyleSet();

  // Probe for the existence of the pseudo-element.
  // |ProbePseudoElementStyle| checks the relevant properties for the pseudo.
  // It only returns a non-null value if the pseudo should exist.
  RefPtr<ComputedStyle> pseudoStyle = styleSet->ProbePseudoElementStyle(
      aOriginatingElement, aPseudoElement, nullptr, &aStyle);
  if (!pseudoStyle) {
    return;
  }

  nsAtom* elemName = nullptr;
  nsAtom* property = nullptr;
  switch (aPseudoElement) {
    case PseudoStyleType::Before:
      elemName = nsGkAtoms::mozgeneratedcontentbefore;
      property = nsGkAtoms::beforePseudoProperty;
      break;
    case PseudoStyleType::After:
      elemName = nsGkAtoms::mozgeneratedcontentafter;
      property = nsGkAtoms::afterPseudoProperty;
      break;
    case PseudoStyleType::Marker:
      // We want to get a marker style even if we match no rules, but we still
      // want to check the result of GeneratedContentPseudoExists.
      elemName = nsGkAtoms::mozgeneratedcontentmarker;
      property = nsGkAtoms::markerPseudoProperty;
      break;
    case PseudoStyleType::Backdrop:
      elemName = nsGkAtoms::mozgeneratedcontentbackdrop;
      property = nsGkAtoms::backdropPseudoProperty;
      break;
    case PseudoStyleType::Checkmark:
      elemName = nsGkAtoms::mozgeneratedcontentcheckmark;
      property = nsGkAtoms::checkmarkPseudoProperty;
      break;
    case PseudoStyleType::PickerIcon:
      elemName = nsGkAtoms::mozgeneratedcontentpickericon;
      property = nsGkAtoms::pickerIconPseudoProperty;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected aPseudoElement");
  }

  RefPtr<NodeInfo> nodeInfo = mDocument->NodeInfoManager()->GetNodeInfo(
      elemName, nullptr, kNameSpaceID_None, nsINode::ELEMENT_NODE);
  RefPtr<Element> container;
  nsresult rv = NS_NewXMLElement(getter_AddRefs(container), nodeInfo.forget());
  if (NS_FAILED(rv)) {
    return;
  }

  // Cleared when the pseudo is unbound from the tree, so no need to store a
  // strong reference, nor a destructor.
  aOriginatingElement.SetProperty(property, container.get());

  container->SetIsNativeAnonymousRoot();
  container->SetPseudoElementType(aPseudoElement);

  BindContext context(aOriginatingElement, BindContext::ForNativeAnonymous);
  rv = container->BindToTree(context, aOriginatingElement);
  if (NS_FAILED(rv)) {
    container->UnbindFromTree();
    return;
  }

  if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
    container->QueueDevtoolsAnonymousEvent(/* aIsRemove = */ false);
  }

  // Servo has already eagerly computed the style for the container, so we can
  // just stick the style on the element and avoid an additional traversal.
  //
  // We don't do this for pseudos that may trigger animations or transitions,
  // since those need to be kicked off by the traversal machinery.
  //
  // Note that when a pseudo-element animates, we flag the originating element,
  // so we check that flag, but we could also a more expensive (but exhaustive)
  // check using EffectSet::GetEffectSet, for example.
  if (!Servo_ComputedValues_SpecifiesAnimationsOrTransitions(pseudoStyle) &&
      !aOriginatingElement.MayHaveAnimations()) {
    Servo_SetExplicitStyle(container, pseudoStyle);
  } else {
    // If animations are involved, we avoid the SetExplicitStyle optimization
    // above. We need to grab style with animations from the pseudo element and
    // replace old one.
    mPresShell->StyleSet()->StyleNewSubtree(container);
    pseudoStyle = ServoStyleSet::ResolveServoStyle(*container);
  }
  if (aPseudoElement != PseudoStyleType::Backdrop) {
    auto AppendChild = [&container, this](nsIContent* aChild) {
      // We don't strictly have to set NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE
      // here; it would get set under AppendChildTo.  But AppendChildTo might
      // think that we're going from not being anonymous to being anonymous and
      // do some extra work; setting the flag here avoids that.
      aChild->SetFlags(NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE);
      container->AppendChildTo(aChild, false, IgnoreErrors());
      if (auto* childElement = Element::FromNode(aChild)) {
        // If we created any children elements, Servo needs to traverse them,
        // but the root is already set up.
        mPresShell->StyleSet()->StyleNewSubtree(childElement);
      }
    };
    auto items = pseudoStyle->StyleContent()->NonAltContentItems();
    size_t index = 0;
    for (const auto& item : items) {
      CreateGeneratedContent(aState, aOriginatingElement, *pseudoStyle, item,
                             index++, AppendChild);
    }
    // If a ::marker has no 'content' then generate it from its 'list-style-*'.
    if (index == 0 && aPseudoElement == PseudoStyleType::Marker) {
      CreateGeneratedContentFromListStyle(aState, aOriginatingElement,
                                          *pseudoStyle, AppendChild);
    }
  }
  auto flags = ItemFlags{ItemFlag::IsGeneratedContent} + aExtraFlags;
  AddFrameConstructionItemsInternal(aState, container, aParentFrame, true,
                                    pseudoStyle, flags, aItems);
}

/****************************************************
 **  BEGIN TABLE SECTION
 ****************************************************/

// The term pseudo frame is being used instead of anonymous frame, since
// anonymous frame has been used elsewhere to refer to frames that have
// generated content

// Return whether the given frame is a table pseudo-frame. Note that
// cell-content and table-outer frames have pseudo-types, but are always
// created, even for non-anonymous cells and tables respectively.  So for those
// we have to examine the cell or table frame to see whether it's a pseudo
// frame. In particular, a lone table caption will have a table wrapper as its
// parent, but will also trigger construction of an empty inner table, which
// will be the one we can examine to see whether the wrapper was a pseudo-frame.
static bool IsTablePseudo(nsIFrame* aFrame) {
  auto pseudoType = aFrame->Style()->GetPseudoType();
  if (pseudoType == PseudoStyleType::NotPseudo) {
    return false;
  }
  return pseudoType == PseudoStyleType::MozTable ||
         pseudoType == PseudoStyleType::MozInlineTable ||
         pseudoType == PseudoStyleType::MozTableColumnGroup ||
         pseudoType == PseudoStyleType::MozTableRowGroup ||
         pseudoType == PseudoStyleType::MozTableRow ||
         pseudoType == PseudoStyleType::MozTableCell ||
         (pseudoType == PseudoStyleType::MozCellContent &&
          aFrame->GetParent()->Style()->GetPseudoType() ==
              PseudoStyleType::MozTableCell) ||
         (pseudoType == PseudoStyleType::MozTableWrapper &&
          static_cast<nsTableWrapperFrame*>(aFrame)
              ->InnerTableFrame()
              ->Style()
              ->IsPseudoOrAnonBox());
}

static bool IsRubyPseudo(nsIFrame* aFrame) {
  return RubyUtils::IsRubyPseudo(aFrame->Style()->GetPseudoType());
}

// Note that this is (subtly) different to ParentIsWrapperAnonBox, since
// ParentIsWrapperAnonBox is really just about restyles, but there are wrapper
// anon boxes that don't need to return true for that...
// FIXME(emilio): This should be less complicated, ParentIsWrapperAnonBox should
// probably be renamed to something else, and this should just use
// IsWrapperAnonBox or similar...
static bool IsWrapperPseudo(nsIFrame* aFrame) {
  auto pseudoType = aFrame->Style()->GetPseudoType();
  if (!PseudoStyle::IsAnonBox(pseudoType)) {
    return false;
  }
  return PseudoStyle::IsWrapperAnonBox(pseudoType) || IsTablePseudo(aFrame);
}

static bool IsInAnonymousTable(nsIFrame* aFrame) {
  for (nsIFrame* f = aFrame; f; f = f->GetParent()) {
    if (!IsWrapperPseudo(f)) {
      return false;
    }
    if (f->IsTableWrapperFrame()) {
      return true;
    }
  }
  MOZ_ASSERT_UNREACHABLE("Expected to be called inside tables");
  return false;
}

/* static */
nsCSSFrameConstructor::ParentType nsCSSFrameConstructor::GetParentType(
    LayoutFrameType aFrameType) {
  if (aFrameType == LayoutFrameType::Table) {
    return eTypeTable;
  }
  if (aFrameType == LayoutFrameType::TableRowGroup) {
    return eTypeRowGroup;
  }
  if (aFrameType == LayoutFrameType::TableRow) {
    return eTypeRow;
  }
  if (aFrameType == LayoutFrameType::TableColGroup) {
    return eTypeColGroup;
  }
  if (aFrameType == LayoutFrameType::RubyBaseContainer) {
    return eTypeRubyBaseContainer;
  }
  if (aFrameType == LayoutFrameType::RubyTextContainer) {
    return eTypeRubyTextContainer;
  }
  if (aFrameType == LayoutFrameType::Ruby) {
    return eTypeRuby;
  }

  return eTypeBlock;
}

// Pull all the captions present in aItems out into aCaptions.
static void PullOutCaptionFrames(nsFrameList& aList, nsFrameList& aCaptions) {
  nsIFrame* child = aList.FirstChild();
  while (child) {
    nsIFrame* nextSibling = child->GetNextSibling();
    if (child->StyleDisplay()->mDisplay == StyleDisplay::TableCaption) {
      aList.RemoveFrame(child);
      aCaptions.AppendFrame(nullptr, child);
    }
    child = nextSibling;
  }
}

// Construct the outer, inner table frames and the children frames for the
// table.
// XXX Page break frames for pseudo table frames are not constructed to avoid
// the risk associated with revising the pseudo frame mechanism. The long term
// solution of having frames handle page-break-before/after will solve the
// problem.
nsIFrame* nsCSSFrameConstructor::ConstructTable(nsFrameConstructorState& aState,
                                                FrameConstructionItem& aItem,
                                                nsContainerFrame* aParentFrame,
                                                const nsStyleDisplay* aDisplay,
                                                nsFrameList& aFrameList) {
  MOZ_ASSERT(aDisplay->mDisplay == StyleDisplay::Table ||
                 aDisplay->mDisplay == StyleDisplay::InlineTable,
             "Unexpected call");

  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;
  const bool isMathMLContent = content->IsMathMLElement();

  // create the pseudo SC for the table wrapper as a child of the inner SC
  RefPtr<ComputedStyle> outerComputedStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozTableWrapper, computedStyle);

  // Create the table wrapper frame which holds the caption and inner table
  // frame
  nsContainerFrame* newFrame;
  if (isMathMLContent) {
    newFrame = NS_NewMathMLmtableOuterFrame(mPresShell, outerComputedStyle);
  } else {
    newFrame = NS_NewTableWrapperFrame(mPresShell, outerComputedStyle);
  }

  nsContainerFrame* geometricParent = aState.GetGeometricParent(
      *outerComputedStyle->StyleDisplay(), aParentFrame);

  // Init the table wrapper frame
  InitAndRestoreFrame(aState, content, geometricParent, newFrame);

  // Create the inner table frame
  nsContainerFrame* innerFrame;
  if (isMathMLContent) {
    innerFrame = NS_NewMathMLmtableFrame(mPresShell, computedStyle);
  } else {
    innerFrame = NS_NewTableFrame(mPresShell, computedStyle);
  }

  InitAndRestoreFrame(aState, content, newFrame, innerFrame);
  innerFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  // Put the newly created frames into the right child list
  SetInitialSingleChild(newFrame, innerFrame);

  aState.AddChild(newFrame, aFrameList, content, aParentFrame);

  if (!mRootElementFrame) {
    mRootElementFrame = newFrame;
  }

  nsFrameList childList;

  // Process children
  nsFrameConstructorSaveState absoluteSaveState;

  // Mark the table frame as an absolute container if needed
  newFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (newFrame->IsAbsPosContainingBlock()) {
    aState.PushAbsoluteContainingBlock(newFrame, newFrame, absoluteSaveState);
  }

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(innerFrame, floatSaveState);

  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    ConstructFramesFromItemList(
        aState, aItem.mChildItems, innerFrame,
        aItem.mFCData->mBits & FCDATA_IS_WRAPPER_ANON_BOX, childList);
  } else {
    ProcessChildren(aState, content, computedStyle, innerFrame, true, childList,
                    false);
  }

  nsFrameList captionList;
  PullOutCaptionFrames(childList, captionList);

  // Set the inner table frame's principal child list.
  innerFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(childList));

  // Append caption frames to the table wrapper frame's principal child list.
  if (captionList.NotEmpty()) {
    captionList.ApplySetParent(newFrame);
    newFrame->AppendFrames(FrameChildListID::Principal, std::move(captionList));
  }

  return newFrame;
}

static void MakeTablePartAbsoluteContainingBlock(
    nsFrameConstructorState& aState, nsFrameConstructorSaveState& aAbsSaveState,
    nsContainerFrame* aFrame) {
  // If we're positioned, then we need to become an absolute containing block
  // for any absolutely positioned children.
  aFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (aFrame->IsAbsPosContainingBlock()) {
    aState.PushAbsoluteContainingBlock(aFrame, aFrame, aAbsSaveState);
  }
}

nsIFrame* nsCSSFrameConstructor::ConstructTableRowOrRowGroup(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  MOZ_ASSERT(aDisplay->mDisplay == StyleDisplay::TableRow ||
                 aDisplay->mDisplay == StyleDisplay::TableRowGroup ||
                 aDisplay->mDisplay == StyleDisplay::TableFooterGroup ||
                 aDisplay->mDisplay == StyleDisplay::TableHeaderGroup,
             "Not a row or row group");
  MOZ_ASSERT(aItem.mComputedStyle->StyleDisplay() == aDisplay,
             "Display style doesn't match style");
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  nsContainerFrame* newFrame;
  if (aDisplay->mDisplay == StyleDisplay::TableRow) {
    if (content->IsMathMLElement()) {
      newFrame = NS_NewMathMLmtrFrame(mPresShell, computedStyle);
    } else {
      newFrame = NS_NewTableRowFrame(mPresShell, computedStyle);
    }
  } else {
    newFrame = NS_NewTableRowGroupFrame(mPresShell, computedStyle);
  }

  InitAndRestoreFrame(aState, content, aParentFrame, newFrame);

  nsFrameConstructorSaveState absoluteSaveState;
  MakeTablePartAbsoluteContainingBlock(aState, absoluteSaveState, newFrame);

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(newFrame, floatSaveState);

  nsFrameList childList;
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    ConstructFramesFromItemList(
        aState, aItem.mChildItems, newFrame,
        aItem.mFCData->mBits & FCDATA_IS_WRAPPER_ANON_BOX, childList);
  } else {
    ProcessChildren(aState, content, computedStyle, newFrame, true, childList,
                    false);
  }

  newFrame->SetInitialChildList(FrameChildListID::Principal,
                                std::move(childList));
  aFrameList.AppendFrame(nullptr, newFrame);
  return newFrame;
}

nsIFrame* nsCSSFrameConstructor::ConstructTableCol(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aStyleDisplay,
    nsFrameList& aFrameList) {
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  nsTableColFrame* colFrame = NS_NewTableColFrame(mPresShell, computedStyle);
  InitAndRestoreFrame(aState, content, aParentFrame, colFrame);

  NS_ASSERTION(colFrame->Style() == computedStyle, "Unexpected style");

  aFrameList.AppendFrame(nullptr, colFrame);

  // construct additional col frames if the col frame has a span > 1
  int32_t span = colFrame->GetSpan();
  for (int32_t spanX = 1; spanX < span; spanX++) {
    nsTableColFrame* newCol = NS_NewTableColFrame(mPresShell, computedStyle);
    InitAndRestoreFrame(aState, content, aParentFrame, newCol,
                        AllowCounters::No);
    aFrameList.LastChild()->SetNextContinuation(newCol);
    newCol->SetPrevContinuation(aFrameList.LastChild());
    aFrameList.AppendFrame(nullptr, newCol);
    newCol->SetColType(eColAnonymousCol);
  }

  return colFrame;
}

nsIFrame* nsCSSFrameConstructor::ConstructTableCell(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  MOZ_ASSERT(aDisplay->mDisplay == StyleDisplay::TableCell, "Unexpected call");

  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;
  const bool isMathMLContent = content->IsMathMLElement();

  nsTableFrame* tableFrame =
      static_cast<nsTableRowFrame*>(aParentFrame)->GetTableFrame();
  nsContainerFrame* cellFrame;
  // <mtable> is border separate in mathml.css and the MathML code doesn't
  // implement border collapse. For those users who style <mtable> with border
  // collapse, give them the default non-MathML table frames that understand
  // border collapse. This won't break us because MathML table frames are all
  // subclasses of the default table code, and so we can freely mix <mtable>
  // with <mtr> or <tr>, <mtd> or <td>. What will happen is just that non-MathML
  // frames won't understand MathML attributes and will therefore miss the
  // special handling that the MathML code does.
  if (isMathMLContent && !tableFrame->IsBorderCollapse()) {
    cellFrame = NS_NewMathMLmtdFrame(mPresShell, computedStyle, tableFrame);
  } else {
    // Warning: If you change this and add a wrapper frame around table cell
    // frames, make sure Bug 368554 doesn't regress!
    // See IsInAutoWidthTableCellForQuirk() in nsImageFrame.cpp.
    cellFrame = NS_NewTableCellFrame(mPresShell, computedStyle, tableFrame);
  }

  // Initialize the table cell frame
  InitAndRestoreFrame(aState, content, aParentFrame, cellFrame);
  cellFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  // Resolve pseudo style and initialize the body cell frame
  RefPtr<ComputedStyle> innerPseudoStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozCellContent, computedStyle);

  nsContainerFrame* cellInnerFrame;
  nsContainerFrame* scrollFrame = nullptr;
  bool isScrollable = false;
  // Create a block frame that will format the cell's content
  if (isMathMLContent) {
    cellInnerFrame = NS_NewMathMLmtdInnerFrame(mPresShell, innerPseudoStyle);
  } else {
    isScrollable = innerPseudoStyle->StyleDisplay()->IsScrollableOverflow() &&
                   !aState.mPresContext->IsPaginated() &&
                   StaticPrefs::layout_tables_scrollable_cells();
    if (isScrollable) {
      innerPseudoStyle = BeginBuildingScrollContainerFrame(
          aState, content, innerPseudoStyle, cellFrame, false, scrollFrame);
    }
    cellInnerFrame = NS_NewBlockFrame(mPresShell, innerPseudoStyle);
  }
  auto* parent = scrollFrame ? scrollFrame : cellFrame;
  InitAndRestoreFrame(aState, content, parent, cellInnerFrame);

  nsFrameConstructorSaveState absoluteSaveState;
  MakeTablePartAbsoluteContainingBlock(aState, absoluteSaveState, cellFrame);

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(cellInnerFrame, floatSaveState);

  nsFrameList childList;
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    AutoFrameConstructionPageName pageNameTracker(aState, cellInnerFrame);
    ConstructFramesFromItemList(
        aState, aItem.mChildItems, cellInnerFrame,
        aItem.mFCData->mBits & FCDATA_IS_WRAPPER_ANON_BOX, childList);
  } else {
    // Process the child content
    ProcessChildren(aState, content, computedStyle, cellInnerFrame, true,
                    childList, !isMathMLContent);
  }

  cellInnerFrame->SetInitialChildList(FrameChildListID::Principal,
                                      std::move(childList));

  if (isScrollable) {
    FinishBuildingScrollContainerFrame(scrollFrame, cellInnerFrame);
  }
  SetInitialSingleChild(cellFrame, scrollFrame ? scrollFrame : cellInnerFrame);
  aFrameList.AppendFrame(nullptr, cellFrame);
  return cellFrame;
}

static inline bool NeedFrameFor(const nsFrameConstructorState& aState,
                                nsContainerFrame* aParentFrame,
                                nsIContent* aChildContent) {
  // XXX the GetContent() != aChildContent check is needed due to bug 135040.
  // Remove it once that's fixed.
  MOZ_ASSERT(
      !aChildContent->GetPrimaryFrame() || aState.mCreatingExtraFrames ||
          aChildContent->GetPrimaryFrame()->GetContent() != aChildContent,
      "Why did we get called?");

  // don't create a whitespace frame if aParentFrame doesn't want it.
  // always create frames for children in generated content. counter(),
  // quotes, and attr() content can easily change dynamically and we don't
  // want to be reconstructing frames. It's not even clear that these
  // should be considered ignorable just because they evaluate to
  // whitespace.

  // We could handle all this in CreateNeededPseudoContainers or some other
  // place after we build our frame construction items, but that would involve
  // creating frame construction items for whitespace kids that ignores
  // white-space, where we know we'll be dropping them all anyway, and involve
  // an extra walk down the frame construction item list.
  auto excludesIgnorableWhitespace = [](nsIFrame* aParentFrame) {
    return aParentFrame->IsMathMLFrame();
  };
  if (!aParentFrame || !excludesIgnorableWhitespace(aParentFrame) ||
      aParentFrame->IsGeneratedContentFrame() || !aChildContent->IsText()) {
    return true;
  }

  aChildContent->SetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE |
                          NS_REFRAME_IF_WHITESPACE);
  return !aChildContent->TextIsOnlyWhitespace();
}

/***********************************************
 * END TABLE SECTION
 ***********************************************/

nsIFrame* nsCSSFrameConstructor::ConstructDocElementFrame(
    Element* aDocElement) {
  MOZ_ASSERT(GetRootFrame(),
             "No viewport?  Someone forgot to call ConstructRootFrame!");
  MOZ_ASSERT(!mDocElementContainingBlock,
             "Shouldn't have a doc element containing block here");

  // Ensure the document element is styled at this point.
  // FIXME(emilio, bug 1852735): This is only needed because of the sync frame
  // construction from PresShell::Initialize.
  if (!aDocElement->HasServoData()) {
    mPresShell->StyleSet()->StyleNewSubtree(aDocElement);
  }
  aDocElement->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);

  // Make sure to call UpdateViewportScrollStylesOverride before
  // SetUpDocElementContainingBlock, since it sets up our scrollbar state
  // properly.
  DebugOnly<nsIContent*> propagatedScrollFrom;
  if (nsPresContext* presContext = mPresShell->GetPresContext()) {
    propagatedScrollFrom = presContext->UpdateViewportScrollStylesOverride();
  }

  SetUpDocElementContainingBlock(aDocElement);

  // This has the side-effect of getting `mFrameTreeState` from our docshell.
  //
  // FIXME(emilio): There may be a more sensible time to do this.
  if (!mFrameTreeState) {
    mPresShell->CaptureHistoryState(getter_AddRefs(mFrameTreeState));
  }

  NS_ASSERTION(mDocElementContainingBlock, "Should have parent by now");
  nsFrameConstructorState state(
      mPresShell,
      GetAbsoluteContainingBlock(mDocElementContainingBlock, FIXED_POS),
      nullptr, nullptr, do_AddRef(mFrameTreeState));

  RefPtr<ComputedStyle> computedStyle =
      ServoStyleSet::ResolveServoStyle(*aDocElement);

  const nsStyleDisplay* display = computedStyle->StyleDisplay();

  // --------- IF SCROLLABLE WRAP IN SCROLLFRAME --------

  NS_ASSERTION(!display->IsScrollableOverflow() ||
                   state.mPresContext->IsPaginated() ||
                   propagatedScrollFrom == aDocElement,
               "Scrollbars should have been propagated to the viewport");

  if (MOZ_UNLIKELY(display->mDisplay == StyleDisplay::None)) {
    return nullptr;
  }

  // This implements "The Principal Writing Mode".
  // https://drafts.csswg.org/css-writing-modes-3/#principal-flow
  //
  // If there's a <body> element in an HTML document, its writing-mode,
  // direction, and text-orientation override the root element's used value.
  //
  // We need to copy <body>'s WritingMode to mDocElementContainingBlock before
  // construct mRootElementFrame so that anonymous internal frames such as
  // <html> with table style can copy their parent frame's mWritingMode in
  // nsIFrame::Init().
  MOZ_ASSERT(!mRootElementFrame,
             "We need to copy <body>'s principal writing-mode before "
             "constructing mRootElementFrame.");

  const WritingMode propagatedWM = [&] {
    const WritingMode rootWM(computedStyle);
    if (computedStyle->StyleDisplay()->IsContainAny()) {
      return rootWM;
    }
    Element* body = mDocument->GetBodyElement();
    if (!body) {
      return rootWM;
    }
    RefPtr<ComputedStyle> bodyStyle = ResolveComputedStyle(body);
    if (bodyStyle->StyleDisplay()->IsContainAny()) {
      return rootWM;
    }
    const WritingMode bodyWM(bodyStyle);
    if (bodyWM != rootWM) {
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "Layout"_ns,
                                      mDocument,
                                      PropertiesFile::LAYOUT_PROPERTIES,
                                      "PrincipalWritingModePropagationWarning");
    }
    return bodyWM;
  }();

  mDocElementContainingBlock->PropagateWritingModeToSelfAndAncestors(
      propagatedWM);

  // Push the absolute containing block now so we can absolutely position the
  // root element
  nsFrameConstructorSaveState canvasCbSaveState;
  mCanvasFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);

  state.PushAbsoluteContainingBlock(mCanvasFrame, mCanvasFrame,
                                    canvasCbSaveState);

  nsFrameConstructorSaveState docElementCbSaveState;
  if (mCanvasFrame != mDocElementContainingBlock) {
    mDocElementContainingBlock->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
    state.PushAbsoluteContainingBlock(mDocElementContainingBlock,
                                      mDocElementContainingBlock,
                                      docElementCbSaveState);
  }

  // The rules from CSS 2.1, section 9.2.4, have already been applied
  // by the style system, so we can assume that display->mDisplay is
  // either NONE, BLOCK, or TABLE.

  // contentFrame is the primary frame for the root element. frameList contains
  // the children of the initial containing block.
  //
  // The first of those frames is usually `contentFrame`, but it can be
  // different, in particular if the root frame is positioned, in which case
  // contentFrame is the out-of-flow frame and frameList.FirstChild() is the
  // placeholder.
  //
  // The rest of the frames in frameList are the anonymous content of the canvas
  // frame.
  nsContainerFrame* contentFrame;
  nsFrameList frameList;
  bool processChildren = false;

  nsFrameConstructorSaveState absoluteSaveState;

  if (aDocElement->IsSVGElement()) {
    if (!aDocElement->IsSVGElement(nsGkAtoms::svg)) {
      return nullptr;
    }
    // We're going to call the right function ourselves, so no need to give a
    // function to this FrameConstructionData.

    // XXXbz on the other hand, if we converted this whole function to
    // FrameConstructionData/Item, then we'd need the right function
    // here... but would probably be able to get away with less code in this
    // function in general.
    static constexpr FrameConstructionData rootSVGData;
    AutoFrameConstructionItem item(this, &rootSVGData, aDocElement,
                                   do_AddRef(computedStyle), true);

    contentFrame = static_cast<nsContainerFrame*>(ConstructOuterSVG(
        state, item, mDocElementContainingBlock, display, frameList));
  } else if (display->mDisplay == StyleDisplay::Flex ||
             display->mDisplay == StyleDisplay::WebkitBox ||
             display->mDisplay == StyleDisplay::Grid) {
    auto func = [&] {
      if (display->mDisplay == StyleDisplay::Grid) {
        return NS_NewGridContainerFrame;
      }
      return NS_NewFlexContainerFrame;
    }();
    contentFrame = func(mPresShell, computedStyle);
    InitAndRestoreFrame(
        state, aDocElement,
        state.GetGeometricParent(*display, mDocElementContainingBlock),
        contentFrame);
    state.AddChild(contentFrame, frameList, aDocElement,
                   mDocElementContainingBlock);
    processChildren = true;

    contentFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
    if (contentFrame->IsAbsPosContainingBlock()) {
      state.PushAbsoluteContainingBlock(contentFrame, contentFrame,
                                        absoluteSaveState);
    }
  } else if (display->mDisplay == StyleDisplay::Table) {
    // We're going to call the right function ourselves, so no need to give a
    // function to this FrameConstructionData.

    // XXXbz on the other hand, if we converted this whole function to
    // FrameConstructionData/Item, then we'd need the right function
    // here... but would probably be able to get away with less code in this
    // function in general.
    static constexpr FrameConstructionData rootTableData;
    AutoFrameConstructionItem item(this, &rootTableData, aDocElement,
                                   do_AddRef(computedStyle), true);

    // if the document is a table then just populate it.
    contentFrame = static_cast<nsContainerFrame*>(ConstructTable(
        state, item, mDocElementContainingBlock, display, frameList));
  } else if (display->DisplayInside() == StyleDisplayInside::Ruby) {
    static constexpr FrameConstructionData data(
        &nsCSSFrameConstructor::ConstructBlockRubyFrame);
    AutoFrameConstructionItem item(this, &data, aDocElement,
                                   do_AddRef(computedStyle), true);
    contentFrame = static_cast<nsContainerFrame*>(ConstructBlockRubyFrame(
        state, item,
        state.GetGeometricParent(*display, mDocElementContainingBlock), display,
        frameList));
  } else {
    MOZ_ASSERT(display->mDisplay == StyleDisplay::Block ||
                   display->mDisplay == StyleDisplay::FlowRoot,
               "Unhandled display type for root element");
    contentFrame = NS_NewBlockFrame(mPresShell, computedStyle);
    ConstructBlock(
        state, aDocElement,
        state.GetGeometricParent(*display, mDocElementContainingBlock),
        mDocElementContainingBlock, computedStyle, &contentFrame, frameList,
        contentFrame->IsAbsPosContainingBlock() ? contentFrame : nullptr);
  }

  MOZ_ASSERT(frameList.FirstChild());
  MOZ_ASSERT(frameList.FirstChild()->GetContent() == aDocElement);
  MOZ_ASSERT(contentFrame);

  MOZ_ASSERT(
      processChildren ? !mRootElementFrame : mRootElementFrame == contentFrame,
      "unexpected mRootElementFrame");
  if (processChildren) {
    mRootElementFrame = contentFrame;
  }

  // Figure out which frame has the main style for the document element,
  // assigning it to mRootElementStyleFrame.
  // Backgrounds should be propagated from that frame to the viewport.
  contentFrame->GetParentComputedStyle(&mRootElementStyleFrame);
  bool isChild = mRootElementStyleFrame &&
                 mRootElementStyleFrame->GetParent() == contentFrame;
  if (!isChild) {
    mRootElementStyleFrame = mRootElementFrame;
  }

  if (processChildren) {
    // Still need to process the child content
    nsFrameList childList;

    NS_ASSERTION(
        !contentFrame->IsBlockFrameOrSubclass() && !contentFrame->IsSVGFrame(),
        "Only XUL frames should reach here");

    nsFrameConstructorSaveState floatSaveState;
    state.MaybePushFloatContainingBlock(contentFrame, floatSaveState);

    ProcessChildren(state, aDocElement, computedStyle, contentFrame, true,
                    childList, false);

    // Set the initial child lists
    contentFrame->SetInitialChildList(FrameChildListID::Principal,
                                      std::move(childList));
  }

  nsIFrame* newFrame = frameList.FirstChild();
  // set the primary frame
  aDocElement->SetPrimaryFrame(contentFrame);
  mDocElementContainingBlock->AppendFrames(FrameChildListID::Principal,
                                           std::move(frameList));

  // NOTE(emilio): This is in the reverse order compared to normal anonymous
  // children. We usually generate anonymous kids first, then non-anonymous,
  // but we generate the doc element frame the other way around. This is fine
  // either way, but generating anonymous children in a different order requires
  // changing nsCanvasFrame (and a whole lot of other potentially unknown code)
  // to look at the last child to find the root frame rather than the first
  // child.
  ConstructAnonymousContentForRoot(state, mCanvasFrame,
                                   mRootElementFrame->GetContent(), frameList);
  mCanvasFrame->AppendFrames(FrameChildListID::Principal, std::move(frameList));

  return newFrame;
}

RestyleManager* nsCSSFrameConstructor::RestyleManager() const {
  return mPresShell->GetPresContext()->RestyleManager();
}

ViewportFrame* nsCSSFrameConstructor::ConstructRootFrame() {
  AUTO_PROFILER_LABEL_HOT("nsCSSFrameConstructor::ConstructRootFrame",
                          LAYOUT_FrameConstruction);
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);

  ServoStyleSet* styleSet = mPresShell->StyleSet();

  // --------- BUILD VIEWPORT -----------
  RefPtr<ComputedStyle> viewportPseudoStyle =
      styleSet->ResolveNonInheritingAnonymousBoxStyle(
          PseudoStyleType::MozViewport);
  ViewportFrame* viewportFrame =
      NS_NewViewportFrame(mPresShell, viewportPseudoStyle);

  // XXXbz do we _have_ to pass a null content pointer to that frame?
  // Would it really kill us to pass in the root element or something?
  // What would that break?
  viewportFrame->Init(nullptr, nullptr, nullptr);

  viewportFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  mPresShell->SetNeedsWindowPropertiesSync();

  // Make it an absolute container for fixed-pos elements
  viewportFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  viewportFrame->MarkAsAbsoluteContainingBlock();

  return viewportFrame;
}

void nsCSSFrameConstructor::SetUpDocElementContainingBlock(
    nsIContent* aDocElement) {
  MOZ_ASSERT(aDocElement, "No element?");
  MOZ_ASSERT(!aDocElement->GetParent(), "Not root content?");
  MOZ_ASSERT(aDocElement->GetUncomposedDoc(), "Not in a document?");
  MOZ_ASSERT(aDocElement->GetUncomposedDoc()->GetRootElement() == aDocElement,
             "Not the root of the document?");

  /*
    how the root frame hierarchy should look

  Galley presentation, with scrolling:

      ViewportFrame [fixed-cb]
        ScrollContainerFrame (if needed)
          nsCanvasFrame [abs-cb]
            root element frame (nsBlockFrame, SVGOuterSVGFrame,
                                nsTableWrapperFrame, nsPlaceholderFrame,
                                nsFlexContainerFrame, nsGridContainerFrame)

  Print presentation, non-XUL

      ViewportFrame
        nsCanvasFrame
          nsPageSequenceFrame
            PrintedSheetFrame
              nsPageFrame
                nsPageContentFrame [fixed-cb]
                  nsCanvasFrame [abs-cb]
                    root element frame (nsBlockFrame, SVGOuterSVGFrame,
                                        nsTableWrapperFrame, nsPlaceholderFrame,
                                        nsFlexContainerFrame,
                                        nsGridContainerFrame)

  Print-preview presentation, non-XUL

      ViewportFrame
        ScrollContainerFrame
          nsCanvasFrame
            nsPageSequenceFrame
              PrintedSheetFrame
                nsPageFrame
                  nsPageContentFrame [fixed-cb]
                    nsCanvasFrame [abs-cb]
                      root element frame (nsBlockFrame, SVGOuterSVGFrame,
                                          nsTableWrapperFrame,
                                          nsPlaceholderFrame,
                                          nsFlexContainerFrame,
                                          nsGridContainerFrame)

  Print/print preview of XUL is not supported.
  [fixed-cb]: the default containing block for fixed-pos content
  [abs-cb]: the default containing block for abs-pos content

  Meaning of nsCSSFrameConstructor fields:
    mRootElementFrame is "root element frame".  This is the primary frame for
      the root element.
    mDocElementContainingBlock is the parent of mRootElementFrame
      (i.e. nsCanvasFrame)
    mPageSequenceFrame is the nsPageSequenceFrame, or null if there isn't
      one
  */

  // --------- CREATE ROOT FRAME -------

  // Create the root frame. The document element's frame is a child of the
  // root frame.
  //
  // The root frame serves two purposes:
  // - reserves space for any margins needed for the document element's frame
  // - renders the document element's background. This ensures the background
  //   covers the entire canvas as specified by the CSS2 spec

  nsPresContext* presContext = mPresShell->GetPresContext();
  const bool isPaginated = presContext->IsRootPaginatedDocument();

  const bool isHTML = aDocElement->IsHTMLElement();
  const bool isXUL = !isHTML && aDocElement->IsXULElement();

  const bool isScrollable = [&] {
    if (isPaginated) {
      return presContext->HasPaginatedScrolling();
    }
    // Never create scrollbars for XUL documents or top level XHTML documents
    // that disable scrolling.
    if (isXUL) {
      return false;
    }
    if (aDocElement->OwnerDoc()->ChromeRulesEnabled() &&
        aDocElement->AsElement()->AttrValueIs(
            kNameSpaceID_None, nsGkAtoms::scrolling, nsGkAtoms::_false,
            eCaseMatters)) {
      return false;
    }
    return true;
  }();

  nsContainerFrame* viewportFrame =
      static_cast<nsContainerFrame*>(GetRootFrame());
  ComputedStyle* viewportPseudoStyle = viewportFrame->Style();

  nsCanvasFrame* rootCanvasFrame =
      NS_NewCanvasFrame(mPresShell, viewportPseudoStyle);
  mCanvasFrame = rootCanvasFrame;
  mDocElementContainingBlock = rootCanvasFrame;

  // --------- IF SCROLLABLE WRAP IN SCROLLFRAME --------

  // If the device supports scrolling (e.g., in galley mode on the screen and
  // for print-preview, but not when printing), then create a scroll frame that
  // will act as the scrolling mechanism for the viewport.
  // XXX Do we even need a viewport when printing to a printer?

  // We no longer need to do overflow propagation here. It's taken care of
  // when we construct frames for the element whose overflow might be
  // propagated
  NS_ASSERTION(!isScrollable || !isXUL,
               "XUL documents should never be scrollable - see above");

  nsContainerFrame* newFrame = rootCanvasFrame;
  RefPtr<ComputedStyle> rootPseudoStyle;
  // we must create a state because if the scrollbars are GFX it needs the
  // state to build the scrollbar frames.
  nsFrameConstructorState state(mPresShell, nullptr, nullptr, nullptr);

  // Start off with the viewport as parent; we'll adjust it as needed.
  nsContainerFrame* parentFrame = viewportFrame;

  ServoStyleSet* styleSet = mPresShell->StyleSet();
  // If paginated, make sure we don't put scrollbars in
  if (!isScrollable) {
    rootPseudoStyle = styleSet->ResolveNonInheritingAnonymousBoxStyle(
        PseudoStyleType::MozCanvas);
  } else {
    // Build the frame. We give it the content we are wrapping which is the
    // document element, the root frame, the parent view port frame, and we
    // should get back the new frame and the scrollable view if one was
    // created.

    // Resolve a style for the scrollframe
    RefPtr<ComputedStyle> computedStyle =
        styleSet->ResolveNonInheritingAnonymousBoxStyle(
            PseudoStyleType::MozViewportScroll);

    newFrame = nullptr;
    rootPseudoStyle = BeginBuildingScrollContainerFrame(
        state, aDocElement, computedStyle, viewportFrame, true, newFrame);
    parentFrame = newFrame;
  }

  rootCanvasFrame->SetComputedStyleWithoutNotification(rootPseudoStyle);
  rootCanvasFrame->Init(aDocElement, parentFrame, nullptr);

  if (isScrollable) {
    FinishBuildingScrollContainerFrame(parentFrame, rootCanvasFrame);
  }

  if (isPaginated) {
    // Create a page sequence frame
    {
      RefPtr<ComputedStyle> pageSequenceStyle =
          styleSet->ResolveInheritingAnonymousBoxStyle(
              PseudoStyleType::MozPageSequence, viewportPseudoStyle);
      mPageSequenceFrame =
          NS_NewPageSequenceFrame(mPresShell, pageSequenceStyle);
      mPageSequenceFrame->Init(aDocElement, rootCanvasFrame, nullptr);
      SetInitialSingleChild(rootCanvasFrame, mPageSequenceFrame);
    }

    // Create the first printed sheet frame, as the sole child (for now) of our
    // page sequence frame (mPageSequenceFrame).
    auto* printedSheetFrame =
        ConstructPrintedSheetFrame(mPresShell, mPageSequenceFrame, nullptr);
    SetInitialSingleChild(mPageSequenceFrame, printedSheetFrame);

    MOZ_ASSERT(!mNextPageContentFramePageName,
               "Next page name should not have been set.");

    // Create the first page, as the sole child (for now) of the printed sheet
    // frame that we just created.
    nsCanvasFrame* canvasFrame;
    nsContainerFrame* pageFrame =
        ConstructPageFrame(mPresShell, printedSheetFrame, nullptr, canvasFrame);
    pageFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);
    SetInitialSingleChild(printedSheetFrame, pageFrame);

    // The eventual parent of the document element frame.
    // XXX should this be set for every new page (in ConstructPageFrame)?
    mDocElementContainingBlock = canvasFrame;
  }

  if (viewportFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    SetInitialSingleChild(viewportFrame, newFrame);
  } else {
    viewportFrame->AppendFrames(FrameChildListID::Principal,
                                nsFrameList(newFrame, newFrame));
  }
}

void nsCSSFrameConstructor::ConstructAnonymousContentForRoot(
    nsFrameConstructorState& aState, nsContainerFrame* aCanvasFrame,
    nsIContent* aDocElement, nsFrameList& aFrameList) {
  NS_ASSERTION(aCanvasFrame->IsCanvasFrame(), "aFrame should be canvas frame!");
  MOZ_ASSERT(mRootElementFrame->GetContent() == aDocElement);

  AutoTArray<nsIAnonymousContentCreator::ContentInfo, 4> anonymousItems;
  GetAnonymousContent(aDocElement, aCanvasFrame, anonymousItems);

  // If we get here, we are rebuilding the anonymous content of the root
  // element. In this case, we also need to deal with the custom content
  // container.
  if (auto* container =
          aState.mPresContext->Document()->GetCustomContentContainer()) {
    // FIXME(emilio, bug 1852735): This is only needed because of the sync frame
    // construction from PresShell::Initialize. See the similar code-path in
    // ConstructDocElementFrame.
    if (!container->HasServoData()) {
      mPresShell->StyleSet()->StyleNewSubtree(container);
    }
    anonymousItems.AppendElement(container);
  }

  if (anonymousItems.IsEmpty()) {
    return;
  }

  AutoFrameConstructionItemList itemsToConstruct(this);
  AutoFrameConstructionPageName pageNameTracker(aState, aCanvasFrame);
  AddFCItemsForAnonymousContent(aState, aCanvasFrame, anonymousItems,
                                itemsToConstruct, pageNameTracker);
  ConstructFramesFromItemList(aState, itemsToConstruct, aCanvasFrame,
                              /* aParentIsWrapperAnonBox = */ false,
                              aFrameList);
}

PrintedSheetFrame* nsCSSFrameConstructor::ConstructPrintedSheetFrame(
    PresShell* aPresShell, nsContainerFrame* aParentFrame,
    nsIFrame* aPrevSheetFrame) {
  RefPtr<ComputedStyle> printedSheetPseudoStyle =
      aPresShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
          PseudoStyleType::MozPrintedSheet);

  auto* printedSheetFrame =
      NS_NewPrintedSheetFrame(aPresShell, printedSheetPseudoStyle);

  printedSheetFrame->Init(nullptr, aParentFrame, aPrevSheetFrame);

  return printedSheetFrame;
}

nsContainerFrame* nsCSSFrameConstructor::ConstructPageFrame(
    PresShell* aPresShell, nsContainerFrame* aParentFrame,
    nsIFrame* aPrevPageFrame, nsCanvasFrame*& aCanvasFrame) {
  ServoStyleSet* styleSet = aPresShell->StyleSet();

  RefPtr<ComputedStyle> pagePseudoStyle =
      styleSet->ResolveNonInheritingAnonymousBoxStyle(PseudoStyleType::MozPage);

  nsContainerFrame* pageFrame = NS_NewPageFrame(aPresShell, pagePseudoStyle);

  // Initialize the page frame and force it to have a view. This makes printing
  // of the pages easier and faster.
  pageFrame->Init(nullptr, aParentFrame, aPrevPageFrame);

  RefPtr<const nsAtom> pageName;
  if (mNextPageContentFramePageName) {
    pageName = mNextPageContentFramePageName.forget();
  } else if (aPrevPageFrame) {
    pageName = aPrevPageFrame->ComputePageValue();
    MOZ_ASSERT(pageName,
               "Page name from prev-in-flow should not have been null");
  }
  RefPtr<ComputedStyle> pageContentPseudoStyle =
      styleSet->ResolvePageContentStyle(pageName,
                                        StylePagePseudoClassFlags::NONE);

  nsContainerFrame* pageContentFrame = NS_NewPageContentFrame(
      aPresShell, pageContentPseudoStyle, pageName.forget());

  nsPageContentFrame* prevPageContentFrame = nullptr;
  if (aPrevPageFrame) {
    MOZ_ASSERT(aPrevPageFrame->IsPageFrame());
    prevPageContentFrame =
        static_cast<nsPageFrame*>(aPrevPageFrame)->PageContentFrame();
  }
  pageContentFrame->Init(nullptr, pageFrame, prevPageContentFrame);
  if (!prevPageContentFrame) {
    // The canvas is an inheriting anon box, so needs to be "owned" by the page
    // content.
    pageContentFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES |
                                   NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
    // Make it an absolute container for fixed-pos elements
    pageContentFrame->MarkAsAbsoluteContainingBlock();
  } else {
    MOZ_ASSERT(
        pageContentFrame->HasAllStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN),
        "This bit should've been carried over from the previous continuation "
        "in nsIFrame::Init().");
    MOZ_ASSERT(pageContentFrame->GetAbsoluteContainingBlock(),
               "nsIFrame::Init() should've constructed AbsoluteContainingBlock "
               "for continuations!");
  }
  SetInitialSingleChild(pageFrame, pageContentFrame);

  RefPtr<ComputedStyle> canvasPseudoStyle =
      styleSet->ResolveNonInheritingAnonymousBoxStyle(
          PseudoStyleType::MozCanvas);
  aCanvasFrame = NS_NewCanvasFrame(aPresShell, canvasPseudoStyle);

  nsIFrame* prevCanvasFrame = nullptr;
  if (prevPageContentFrame) {
    prevCanvasFrame = prevPageContentFrame->PrincipalChildList().FirstChild();
    NS_ASSERTION(prevCanvasFrame, "missing canvas frame");
  }
  aCanvasFrame->Init(nullptr, pageContentFrame, prevCanvasFrame);
  SetInitialSingleChild(pageContentFrame, aCanvasFrame);
  return pageFrame;
}

/* static */
nsIFrame* nsCSSFrameConstructor::CreatePlaceholderFrameFor(
    PresShell* aPresShell, nsIContent* aContent, nsIFrame* aFrame,
    nsContainerFrame* aParentFrame, nsIFrame* aPrevInFlow,
    nsFrameState aTypeBit) {
  RefPtr<ComputedStyle> placeholderStyle =
      aPresShell->StyleSet()->ResolveStyleForPlaceholder();

  // The placeholder frame gets a pseudo style.
  nsPlaceholderFrame* placeholderFrame =
      NS_NewPlaceholderFrame(aPresShell, placeholderStyle, aTypeBit);

  placeholderFrame->Init(aContent, aParentFrame, aPrevInFlow);

  // Associate the placeholder/out-of-flow with each other.
  placeholderFrame->SetOutOfFlowFrame(aFrame);
  aFrame->SetProperty(nsIFrame::PlaceholderFrameProperty(), placeholderFrame);

  aFrame->AddStateBits(NS_FRAME_OUT_OF_FLOW);

  return placeholderFrame;
}

// Clears any lazy bits set in the range [aStartContent, aEndContent).  If
// aEndContent is null, that means to clear bits in all siblings starting with
// aStartContent.  aStartContent must not be null unless aEndContent is also
// null.  We do this so that when new children are inserted under elements whose
// frame is a leaf the new children don't cause us to try to construct frames
// for the existing children again.
static inline void ClearLazyBits(nsIContent* aStartContent,
                                 nsIContent* aEndContent) {
  MOZ_ASSERT(aStartContent || !aEndContent,
             "Must have start child if we have an end child");

  for (nsIContent* cur = aStartContent; cur != aEndContent;
       cur = cur->GetNextSibling()) {
    cur->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);
  }
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindSelectData(const Element& aElement,
                                      ComputedStyle& aStyle) {
  // Construct a frame-based listbox or combobox
  const auto* sel = dom::HTMLSelectElement::FromNode(aElement);
  MOZ_ASSERT(sel);
  switch (aStyle.StyleDisplay()->EffectiveAppearance()) {
    case StyleAppearance::Base:
    case StyleAppearance::BaseSelect:
      return nullptr;
    default:
      break;
  }
  if (sel->IsCombobox()) {
    static constexpr FrameConstructionData sComboboxData{
        ToCreationFunc(NS_NewComboboxControlFrame)};
    return &sComboboxData;
  }
  // FIXME: Can we simplify this to avoid needing ConstructListboxSelectFrame,
  // and reuse ConstructScrollableBlock or so?
  static constexpr FrameConstructionData sListBoxData{
      &nsCSSFrameConstructor::ConstructListBoxSelectFrame};
  return &sListBoxData;
}

nsIFrame* nsCSSFrameConstructor::ConstructListBoxSelectFrame(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aStyleDisplay,
    nsFrameList& aFrameList) {
  nsContainerFrame* listFrame =
      NS_NewListControlFrame(mPresShell, aItem.mComputedStyle);
  InitAndRestoreFrame(aState, aItem.mContent,
                      aState.GetGeometricParent(*aStyleDisplay, aParentFrame),
                      listFrame);
  ConstructScrollableBlockWithScrollContainer(
      aState, aItem, aParentFrame, aStyleDisplay, aFrameList, listFrame);
  return listFrame;
}

nsIFrame* nsCSSFrameConstructor::ConstructTextControl(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aStyleDisplay,
    nsFrameList& aFrameList) {
  nsContainerFrame* tcf =
      NS_NewTextControlFrame(mPresShell, aItem.mComputedStyle);
  InitAndRestoreFrame(aState, aItem.mContent,
                      aState.GetGeometricParent(*aStyleDisplay, aParentFrame),
                      tcf);
  ConstructScrollableBlockWithScrollContainer(aState, aItem, aParentFrame,
                                              aStyleDisplay, aFrameList, tcf);
  return tcf;
}

nsIFrame* nsCSSFrameConstructor::ConstructFieldSetFrame(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aStyleDisplay,
    nsFrameList& aFrameList) {
  AutoRestore<bool> savedHasRenderedLegend(aState.mHasRenderedLegend);
  aState.mHasRenderedLegend = false;
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  nsContainerFrame* fieldsetFrame =
      NS_NewFieldSetFrame(mPresShell, computedStyle);

  // Initialize it
  InitAndRestoreFrame(aState, content,
                      aState.GetGeometricParent(*aStyleDisplay, aParentFrame),
                      fieldsetFrame);

  fieldsetFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  // Resolve style and initialize the frame
  RefPtr<ComputedStyle> fieldsetContentStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozFieldsetContent, computedStyle);

  const nsStyleDisplay* fieldsetContentDisplay =
      fieldsetContentStyle->StyleDisplay();
  const bool isScrollable = fieldsetContentDisplay->IsScrollableOverflow();
  nsContainerFrame* scrollFrame = nullptr;
  if (isScrollable) {
    fieldsetContentStyle =
        BeginBuildingScrollContainerFrame(aState, content, fieldsetContentStyle,
                                          fieldsetFrame, false, scrollFrame);
  }

  // Create the inner ::-moz-fieldset-content frame.
  nsContainerFrame* contentFrameTop;
  nsContainerFrame* contentFrame;
  auto* parent = scrollFrame ? scrollFrame : fieldsetFrame;
  MOZ_ASSERT(fieldsetContentDisplay->DisplayOutside() ==
             StyleDisplayOutside::Block);
  switch (fieldsetContentDisplay->DisplayInside()) {
    case StyleDisplayInside::Flex:
      contentFrame = NS_NewFlexContainerFrame(mPresShell, fieldsetContentStyle);
      InitAndRestoreFrame(aState, content, parent, contentFrame);
      contentFrameTop = contentFrame;
      break;
    case StyleDisplayInside::Grid:
      contentFrame = NS_NewGridContainerFrame(mPresShell, fieldsetContentStyle);
      InitAndRestoreFrame(aState, content, parent, contentFrame);
      contentFrameTop = contentFrame;
      break;
    default: {
      MOZ_ASSERT(fieldsetContentDisplay->mDisplay == StyleDisplay::Block,
                 "bug in StyleAdjuster::adjust_for_fieldset_content?");

      contentFrame = NS_NewBlockFrame(mPresShell, fieldsetContentStyle);
      if (fieldsetContentStyle->StyleColumn()->IsColumnContainerStyle()) {
        contentFrameTop = BeginBuildingColumns(
            aState, content, parent, contentFrame, fieldsetContentStyle);
      } else {
        // No need to create column container. Initialize content frame.
        InitAndRestoreFrame(aState, content, parent, contentFrame);
        contentFrameTop = contentFrame;
      }

      break;
    }
  }

  aState.AddChild(fieldsetFrame, aFrameList, content, aParentFrame);

  // Process children
  nsFrameConstructorSaveState absoluteSaveState;
  nsFrameList childList;

  contentFrameTop->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (fieldsetFrame->IsAbsPosContainingBlock()) {
    aState.PushAbsoluteContainingBlock(contentFrameTop, fieldsetFrame,
                                       absoluteSaveState);
  }

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(contentFrame, floatSaveState);

  ProcessChildren(aState, content, computedStyle, contentFrame, true, childList,
                  true);
  nsFrameList fieldsetKids;
  fieldsetKids.AppendFrame(nullptr,
                           scrollFrame ? scrollFrame : contentFrameTop);

  if (!MayNeedToCreateColumnSpanSiblings(contentFrame, childList)) {
    // Set the inner frame's initial child lists.
    contentFrame->SetInitialChildList(FrameChildListID::Principal,
                                      std::move(childList));
  } else {
    // Extract any initial non-column-span kids, and put them in inner frame's
    // child list.
    nsFrameList initialNonColumnSpanKids =
        childList.Split([](nsIFrame* f) { return f->IsColumnSpan(); });
    contentFrame->SetInitialChildList(FrameChildListID::Principal,
                                      std::move(initialNonColumnSpanKids));

    if (childList.NotEmpty()) {
      nsFrameList columnSpanSiblings = CreateColumnSpanSiblings(
          aState, contentFrame, childList,
          // Column content should never be a absolute/fixed positioned
          // containing block. Pass nullptr as aPositionedFrame.
          nullptr);
      FinishBuildingColumns(aState, contentFrameTop, contentFrame,
                            columnSpanSiblings);
    }
  }

  if (isScrollable) {
    FinishBuildingScrollContainerFrame(scrollFrame, contentFrameTop);
  }

  // We use AppendFrames here because the rendered legend will already
  // be present in the principal child list if it exists.
  fieldsetFrame->AppendFrames(FrameChildListID::NoReflowPrincipal,
                              std::move(fieldsetKids));

  return fieldsetFrame;
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDetailsData(const Element& aElement,
                                       ComputedStyle& aStyle) {
  if (!StaticPrefs::layout_details_force_block_layout()) {
    return nullptr;
  }
  static constexpr FrameConstructionData sBlockData[2] = {
      {&nsCSSFrameConstructor::ConstructNonScrollableBlock},
      {&nsCSSFrameConstructor::ConstructScrollableBlock},
  };
  return &sBlockData[aStyle.StyleDisplay()->IsScrollableOverflow()];
}

nsIFrame* nsCSSFrameConstructor::ConstructBlockRubyFrame(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aStyleDisplay,
    nsFrameList& aFrameList) {
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  nsBlockFrame* blockFrame = NS_NewBlockFrame(mPresShell, computedStyle);
  nsContainerFrame* newFrame = blockFrame;
  nsContainerFrame* geometricParent =
      aState.GetGeometricParent(*aStyleDisplay, aParentFrame);
  AutoFrameConstructionPageName pageNameTracker(aState, blockFrame);
  if ((aItem.mFCData->mBits & FCDATA_MAY_NEED_SCROLLFRAME) &&
      aStyleDisplay->IsScrollableOverflow()) {
    nsContainerFrame* scrollframe = nullptr;
    BuildScrollContainerFrame(aState, content, computedStyle, blockFrame,
                              geometricParent, scrollframe);
    newFrame = scrollframe;
  } else {
    InitAndRestoreFrame(aState, content, geometricParent, blockFrame);
  }

  RefPtr<ComputedStyle> rubyStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozBlockRubyContent, computedStyle);
  nsContainerFrame* rubyFrame = NS_NewRubyFrame(mPresShell, rubyStyle);
  InitAndRestoreFrame(aState, content, blockFrame, rubyFrame);
  SetInitialSingleChild(blockFrame, rubyFrame);
  blockFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  aState.AddChild(newFrame, aFrameList, content, aParentFrame);

  if (!mRootElementFrame) {
    mRootElementFrame = newFrame;
  }

  nsFrameConstructorSaveState absoluteSaveState;
  blockFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (newFrame->IsAbsPosContainingBlock()) {
    aState.PushAbsoluteContainingBlock(blockFrame, blockFrame,
                                       absoluteSaveState);
  }
  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(blockFrame, floatSaveState);

  nsFrameList childList;
  ProcessChildren(aState, content, rubyStyle, rubyFrame, true, childList, false,
                  nullptr);
  rubyFrame->SetInitialChildList(FrameChildListID::Principal,
                                 std::move(childList));

  return newFrame;
}

static nsIFrame* FindAncestorWithGeneratedContentPseudo(nsIFrame* aFrame) {
  for (nsIFrame* f = aFrame->GetParent(); f; f = f->GetParent()) {
    NS_ASSERTION(f->IsGeneratedContentFrame(),
                 "should not have exited generated content");
    auto pseudo = f->Style()->GetPseudoType();
    if (pseudo == PseudoStyleType::Before || pseudo == PseudoStyleType::After ||
        pseudo == PseudoStyleType::Marker ||
        pseudo == PseudoStyleType::Backdrop ||
        pseudo == PseudoStyleType::Checkmark ||
        pseudo == PseudoStyleType::PickerIcon) {
      return f;
    }
  }
  return nullptr;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindTextData(const Text& aTextContent,
                                    nsIFrame* aParentFrame) {
  if (aParentFrame && IsFrameForSVG(aParentFrame)) {
    if (!aParentFrame->IsInSVGTextSubtree()) {
      return nullptr;
    }

    // FIXME(bug 1588477) Don't render stuff in display: contents / Shadow DOM
    // subtrees, because TextCorrespondenceRecorder in the SVG text code doesn't
    // really know how to deal with it. This kinda sucks. :(
    if (aParentFrame->GetContent() != aTextContent.GetParent()) {
      return nullptr;
    }

    static constexpr FrameConstructionData sSVGTextData(
        NS_NewTextFrame, FCDATA_IS_LINE_PARTICIPANT | FCDATA_IS_SVG_TEXT);
    return &sSVGTextData;
  }

  static constexpr FrameConstructionData sTextData(NS_NewTextFrame,
                                                   FCDATA_IS_LINE_PARTICIPANT);
  return &sTextData;
}

void nsCSSFrameConstructor::ConstructTextFrame(
    const FrameConstructionData* aData, nsFrameConstructorState& aState,
    nsIContent* aContent, nsContainerFrame* aParentFrame,
    ComputedStyle* aComputedStyle, nsFrameList& aFrameList) {
  MOZ_ASSERT(aData, "Must have frame construction data");

  nsIFrame* newFrame =
      (*aData->mFunc.mCreationFunc)(mPresShell, aComputedStyle);

  InitAndRestoreFrame(aState, aContent, aParentFrame, newFrame);

  // We never need to create a view for a text frame.

  if (newFrame->IsGeneratedContentFrame()) {
    UniquePtr<nsGenConInitializer> initializer(
        static_cast<nsGenConInitializer*>(
            aContent->TakeProperty(nsGkAtoms::genConInitializerProperty)));
    if (initializer) {
      if (initializer->mNode.release()->InitTextFrame(
              initializer->mList,
              FindAncestorWithGeneratedContentPseudo(newFrame), newFrame)) {
        (this->*(initializer->mDirtyAll))();
      }
    }
  }

  // Add the newly constructed frame to the flow
  aFrameList.AppendFrame(nullptr, newFrame);

  if (!aState.mCreatingExtraFrames || (aContent->IsInNativeAnonymousSubtree() &&
                                       !aContent->GetPrimaryFrame())) {
    aContent->SetPrimaryFrame(newFrame);
  }
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDataByInt(int32_t aInt, const Element& aElement,
                                     ComputedStyle& aComputedStyle,
                                     const FrameConstructionDataByInt* aDataPtr,
                                     uint32_t aDataLength) {
  for (const FrameConstructionDataByInt *curData = aDataPtr,
                                        *endData = aDataPtr + aDataLength;
       curData != endData; ++curData) {
    if (curData->mInt == aInt) {
      const FrameConstructionData* data = &curData->mData;
      if (data->mBits & FCDATA_FUNC_IS_DATA_GETTER) {
        return data->mFunc.mDataGetter(aElement, aComputedStyle);
      }

      return data;
    }
  }

  return nullptr;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDataByTag(const Element& aElement,
                                     ComputedStyle& aStyle,
                                     const FrameConstructionDataByTag* aDataPtr,
                                     uint32_t aDataLength) {
  const nsAtom* tag = aElement.NodeInfo()->NameAtom();
  for (const FrameConstructionDataByTag *curData = aDataPtr,
                                        *endData = aDataPtr + aDataLength;
       curData != endData; ++curData) {
    if (curData->mTag == tag) {
      const FrameConstructionData* data = &curData->mData;
      if (data->mBits & FCDATA_FUNC_IS_DATA_GETTER) {
        return data->mFunc.mDataGetter(aElement, aStyle);
      }

      return data;
    }
  }

  return nullptr;
}

#define SUPPRESS_FCDATA() FrameConstructionData(nullptr, FCDATA_SUPPRESS_FRAME)
#define SIMPLE_INT_CREATE(_int, _func) \
  {int32_t(_int), FrameConstructionData(_func)}
#define SIMPLE_INT_CHAIN(_int, _func) \
  {int32_t(_int), FrameConstructionData(_func)}
#define COMPLEX_INT_CREATE(_int, _func) \
  {int32_t(_int), FrameConstructionData(_func)}

#define SIMPLE_TAG_CREATE(_tag, _func) \
  {nsGkAtoms::_tag, FrameConstructionData(_func)}
#define SIMPLE_TAG_CHAIN(_tag, _func) \
  {nsGkAtoms::_tag, FrameConstructionData(_func)}
#define COMPLEX_TAG_CREATE(_tag, _func) \
  {nsGkAtoms::_tag, FrameConstructionData(_func)}

static nsFieldSetFrame* GetFieldSetFrameFor(nsIFrame* aFrame) {
  auto pseudo = aFrame->Style()->GetPseudoType();
  if (pseudo == PseudoStyleType::MozFieldsetContent ||
      pseudo == PseudoStyleType::MozScrolledContent ||
      pseudo == PseudoStyleType::MozColumnSet ||
      pseudo == PseudoStyleType::MozColumnContent) {
    return GetFieldSetFrameFor(aFrame->GetParent());
  }
  return do_QueryFrame(aFrame);
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindHTMLData(const Element& aElement,
                                    nsIFrame* aParentFrame,
                                    ComputedStyle& aStyle) {
  MOZ_ASSERT(aElement.IsHTMLElement());
  NS_ASSERTION(!aParentFrame ||
                   aParentFrame->Style()->GetPseudoType() !=
                       PseudoStyleType::MozFieldsetContent ||
                   aParentFrame->GetParent()->IsFieldSetFrame(),
               "Unexpected parent for fieldset content anon box");

  switch (aStyle.GetPseudoType()) {
    case PseudoStyleType::ViewTransitionOld:
    case PseudoStyleType::ViewTransitionNew: {
      static constexpr FrameConstructionData sViewTransitionData(
          NS_NewImageFrameForViewTransition);
      return &sViewTransitionData;
    }
    case PseudoStyleType::MozSelectContent: {
      if (aParentFrame && aParentFrame->IsComboboxControlFrame()) {
        static constexpr FrameConstructionData sComboboxLabelData(
            NS_NewComboboxLabelFrame);
        return &sComboboxLabelData;
      }
      break;
    }
    case PseudoStyleType::MozFileContent: {
      if (aParentFrame && aParentFrame->IsFileControlFrame()) {
        static constexpr FrameConstructionData sFileLabelData(
            NS_NewFileControlLabelFrame);
        return &sFileLabelData;
      }
      break;
    }
    case PseudoStyleType::MozReveal:
    case PseudoStyleType::MozNumberSpinBox: {
      if (aParentFrame && aParentFrame->StyleDisplay()->EffectiveAppearance() ==
                              StyleAppearance::Textfield) {
        // Suppress button-box pseudo-elements with appearance: textfield.
        static constexpr FrameConstructionData sSuppressData =
            SUPPRESS_FCDATA();
        return &sSuppressData;
      }
      break;
    }
    default:
      break;
  }

  static constexpr FrameConstructionDataByTag sHTMLData[] = {
      SIMPLE_TAG_CHAIN(img, nsCSSFrameConstructor::FindImgData),
      SIMPLE_TAG_CHAIN(mozgeneratedcontentimage,
                       nsCSSFrameConstructor::FindGeneratedImageData),
      {nsGkAtoms::br,
       {NS_NewBRFrame, FCDATA_IS_LINE_PARTICIPANT | FCDATA_IS_LINE_BREAK}},
      SIMPLE_TAG_CREATE(wbr, NS_NewWBRFrame),
      SIMPLE_TAG_CHAIN(button, nsCSSFrameConstructor::FindHTMLButtonData),
      SIMPLE_TAG_CHAIN(input, nsCSSFrameConstructor::FindInputData),
      SIMPLE_TAG_CREATE(textarea, &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_TAG_CHAIN(select, nsCSSFrameConstructor::FindSelectData),
      SIMPLE_TAG_CHAIN(object, nsCSSFrameConstructor::FindObjectData),
      SIMPLE_TAG_CHAIN(embed, nsCSSFrameConstructor::FindObjectData),
      COMPLEX_TAG_CREATE(fieldset,
                         &nsCSSFrameConstructor::ConstructFieldSetFrame),
      SIMPLE_TAG_CREATE(frameset, NS_NewHTMLFramesetFrame),
      SIMPLE_TAG_CREATE(iframe, NS_NewSubDocumentFrame),
      SIMPLE_TAG_CHAIN(canvas, nsCSSFrameConstructor::FindCanvasData),
      SIMPLE_TAG_CREATE(video, NS_NewHTMLVideoFrame),
      SIMPLE_TAG_CREATE(audio, NS_NewHTMLAudioFrame),
      SIMPLE_TAG_CREATE(progress, NS_NewProgressFrame),
      SIMPLE_TAG_CREATE(meter, NS_NewMeterFrame),
      SIMPLE_TAG_CHAIN(details, nsCSSFrameConstructor::FindDetailsData),
  };

  return FindDataByTag(aElement, aStyle, sHTMLData, std::size(sHTMLData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindGeneratedImageData(const Element& aElement,
                                              ComputedStyle&) {
  if (!aElement.IsInNativeAnonymousSubtree()) {
    return nullptr;
  }

  auto& generatedContent = static_cast<const GeneratedImageContent&>(aElement);
  if (generatedContent.IsForListStyleImageMarker()) {
    static constexpr FrameConstructionData sImgData(
        NS_NewImageFrameForListStyleImage);
    return &sImgData;
  }

  static constexpr FrameConstructionData sImgData(
      NS_NewImageFrameForGeneratedContentIndex);
  return &sImgData;
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindHTMLButtonData(const Element&,
                                          ComputedStyle& aStyle) {
  // Buttons force a (maybe inline) block unless their display is flex or grid.
  // TODO(emilio): It'd be good to remove this restriction more broadly.
  // There are some tests that expect block baselines on e.g. a `display: table`
  // button, but seems like it would be doable.
  const auto* disp = aStyle.StyleDisplay();
  const bool respectDisplay = [&] {
    if (disp->IsInlineFlow()) {
      // For compat, `display: inline` and co need to create an inline-block.
      return false;
    }
    switch (disp->DisplayInside()) {
      case StyleDisplayInside::Flex:
      case StyleDisplayInside::Grid:
      case StyleDisplayInside::FlowRoot:
        return true;
      default:
        return false;
    }
  }();
  if (respectDisplay) {
    return nullptr;
  }
  static constexpr FrameConstructionData sBlockData[2] = {
      {&nsCSSFrameConstructor::ConstructNonScrollableBlock},
      {&nsCSSFrameConstructor::ConstructScrollableBlock},
  };
  return &sBlockData[disp->IsScrollableOverflow()];
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindImgData(const Element& aElement,
                                   ComputedStyle& aStyle) {
  if (nsImageFrame::ImageFrameTypeFor(aElement, aStyle) !=
      nsImageFrame::ImageFrameType::ForElementRequest) {
    // content: url gets handled by the generic code-path.
    return nullptr;
  }

  static constexpr FrameConstructionData sImgData(NS_NewImageFrame);
  return &sImgData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindImgControlData(const Element& aElement,
                                          ComputedStyle& aStyle) {
  if (nsImageFrame::ImageFrameTypeFor(aElement, aStyle) !=
      nsImageFrame::ImageFrameType::ForElementRequest) {
    return nullptr;
  }

  static constexpr FrameConstructionData sImgControlData(
      NS_NewImageControlFrame);
  return &sImgControlData;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindInputData(const Element& aElement,
                                     ComputedStyle& aStyle) {
  static constexpr FrameConstructionDataByInt sInputData[] = {
      SIMPLE_INT_CREATE(FormControlType::InputCheckbox,
                        ToCreationFunc(NS_NewCheckboxRadioFrame)),
      SIMPLE_INT_CREATE(FormControlType::InputRadio,
                        ToCreationFunc(NS_NewCheckboxRadioFrame)),
      SIMPLE_INT_CREATE(FormControlType::InputFile, NS_NewFileControlFrame),
      SIMPLE_INT_CHAIN(FormControlType::InputImage,
                       nsCSSFrameConstructor::FindImgControlData),
      SIMPLE_INT_CREATE(FormControlType::InputEmail,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputText,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputTel,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputUrl,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputRange, NS_NewRangeFrame),
      SIMPLE_INT_CREATE(FormControlType::InputPassword,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputColor, NS_NewColorControlFrame),
      SIMPLE_INT_CREATE(FormControlType::InputSearch,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputNumber,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputTime, NS_NewDateTimeControlFrame),
      SIMPLE_INT_CREATE(FormControlType::InputDate, NS_NewDateTimeControlFrame),
      SIMPLE_INT_CREATE(FormControlType::InputDatetimeLocal,
                        NS_NewDateTimeControlFrame),
      // TODO: this is temporary until a frame is written: bug 888320
      SIMPLE_INT_CREATE(FormControlType::InputMonth,
                        &nsCSSFrameConstructor::ConstructTextControl),
      // TODO: this is temporary until a frame is written: bug 888320
      SIMPLE_INT_CREATE(FormControlType::InputWeek,
                        &nsCSSFrameConstructor::ConstructTextControl),
      SIMPLE_INT_CREATE(FormControlType::InputSubmit,
                        NS_NewInputButtonControlFrame),
      SIMPLE_INT_CREATE(FormControlType::InputReset,
                        NS_NewInputButtonControlFrame),
      SIMPLE_INT_CREATE(FormControlType::InputButton,
                        NS_NewInputButtonControlFrame),
      // Keeping hidden inputs out of here on purpose for so they get frames by
      // display (in practice, none).
  };

  auto controlType = HTMLInputElement::FromNode(aElement)->ControlType();

  // radio and checkbox inputs with appearance:none should be constructed
  // by display type.  (Note that we're not checking that appearance is
  // not (respectively) StyleAppearance::Radio and StyleAppearance::Checkbox.)
  if ((controlType == FormControlType::InputCheckbox ||
       controlType == FormControlType::InputRadio) &&
      !aStyle.StyleDisplay()->HasNativeAppearance()) {
    return nullptr;
  }

  return FindDataByInt(int32_t(controlType), aElement, aStyle, sInputData,
                       std::size(sInputData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindObjectData(const Element& aElement,
                                      ComputedStyle& aStyle) {
  uint32_t type;
  nsCOMPtr<nsIObjectLoadingContent> objContent =
      do_QueryInterface(const_cast<Element*>(&aElement));
  NS_ASSERTION(objContent,
               "embed and object must implement "
               "nsIObjectLoadingContent!");
  objContent->GetDisplayedType(&type);

  static constexpr FrameConstructionDataByInt sObjectData[] = {
      // TODO(emilio): Can we remove the NS_NewEmptyFrame case and just use a
      // subdocument frame here?
      SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_LOADING,
                        NS_NewEmptyFrame),
      SIMPLE_INT_CREATE(nsIObjectLoadingContent::TYPE_DOCUMENT,
                        NS_NewSubDocumentFrame),
      // Nothing for TYPE_FALLBACK so we'll construct frames by display there
  };

  return FindDataByInt((int32_t)type, aElement, aStyle, sObjectData,
                       std::size(sObjectData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindCanvasData(const Element& aElement,
                                      ComputedStyle& aStyle) {
  // We want to check whether script is enabled on the document that
  // could be painting to the canvas.  That's the owner document of
  // the canvas, except when the owner document is a static document,
  // in which case it's the original document it was cloned from.
  Document* doc = aElement.OwnerDoc();
  if (doc->IsStaticDocument()) {
    doc = doc->GetOriginalDocument();
  }
  if (!doc->IsScriptEnabled()) {
    return nullptr;
  }

  static constexpr FrameConstructionData sCanvasData(
      NS_NewHTMLCanvasFrame, 0, PseudoStyleType::MozHtmlCanvasContent);
  return &sCanvasData;
}

static MOZ_NEVER_INLINE void DestroyFramesInList(PresShell* aPs,
                                                 nsFrameList& aList) {
  nsIFrame::DestroyContext context(aPs);
  aList.DestroyFrames(context);
}

void nsCSSFrameConstructor::ConstructFrameFromItemInternal(
    FrameConstructionItem& aItem, nsFrameConstructorState& aState,
    nsContainerFrame* aParentFrame, nsFrameList& aFrameList) {
  const FrameConstructionData* data = aItem.mFCData;
  NS_ASSERTION(data, "Must have frame construction data");

  uint32_t bits = data->mBits;

  NS_ASSERTION(!(bits & FCDATA_FUNC_IS_DATA_GETTER),
               "Should have dealt with this inside the data finder");

  // Some sets of bits are not compatible with each other
#define CHECK_ONLY_ONE_BIT(_bit1, _bit2)           \
  NS_ASSERTION(!(bits & _bit1) || !(bits & _bit2), \
               "Only one of these bits should be set")
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR,
                     FCDATA_FORCE_NULL_ABSPOS_CONTAINER);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_WRAP_KIDS_IN_BLOCKS);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_SKIP_ABSPOS_PUSH);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR,
                     FCDATA_DISALLOW_GENERATED_CONTENT);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR, FCDATA_ALLOW_BLOCK_STYLES);
  CHECK_ONLY_ONE_BIT(FCDATA_FUNC_IS_FULL_CTOR,
                     FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS);
  CHECK_ONLY_ONE_BIT(FCDATA_WRAP_KIDS_IN_BLOCKS,
                     FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS);
#undef CHECK_ONLY_ONE_BIT
  MOZ_ASSERT(
      !(bits & FCDATA_IS_WRAPPER_ANON_BOX) || (bits & FCDATA_USE_CHILD_ITEMS),
      "Wrapper anon boxes should always have FCDATA_USE_CHILD_ITEMS");

  // Don't create a subdocument frame for iframes if we're creating extra frames
  if (aState.mCreatingExtraFrames &&
      aItem.mContent->IsHTMLElement(nsGkAtoms::iframe)) {
    return;
  }

  nsIContent* const content = aItem.mContent;
  nsIFrame* newFrame;
  nsIFrame* primaryFrame;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;
  const nsStyleDisplay* display = computedStyle->StyleDisplay();
  if (bits & FCDATA_FUNC_IS_FULL_CTOR) {
    newFrame = (this->*(data->mFunc.mFullConstructor))(
        aState, aItem, aParentFrame, display, aFrameList);
    MOZ_ASSERT(newFrame, "Full constructor failed");
    primaryFrame = newFrame;
  } else {
    newFrame = (*data->mFunc.mCreationFunc)(mPresShell, computedStyle);

    const bool allowOutOfFlow = !(bits & FCDATA_DISALLOW_OUT_OF_FLOW);
    nsContainerFrame* geometricParent =
        allowOutOfFlow ? aState.GetGeometricParent(*display, aParentFrame)
                       : aParentFrame;

    // In the non-scrollframe case, primaryFrame and newFrame are equal; in the
    // scrollframe case, newFrame is the scrolled frame while primaryFrame is
    // the scrollframe.
    if ((bits & FCDATA_MAY_NEED_SCROLLFRAME) &&
        display->IsScrollableOverflow()) {
      nsContainerFrame* scrollframe = nullptr;
      BuildScrollContainerFrame(aState, content, computedStyle, newFrame,
                                geometricParent, scrollframe);
      primaryFrame = scrollframe;
    } else {
      InitAndRestoreFrame(aState, content, geometricParent, newFrame);
      primaryFrame = newFrame;
    }

    // If we need to create a block formatting context to wrap our
    // kids, do it now.
    nsIFrame* maybeAbsoluteContainingBlockStyleFrame = primaryFrame;
    nsIFrame* maybeAbsoluteContainingBlock = newFrame;
    nsIFrame* possiblyLeafFrame = newFrame;
    nsContainerFrame* outerFrame = nullptr;
    if (bits & FCDATA_CREATE_BLOCK_WRAPPER_FOR_ALL_KIDS) {
      RefPtr<ComputedStyle> outerStyle =
          mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
              data->mAnonBoxPseudo, computedStyle);
#ifdef DEBUG
      nsContainerFrame* containerFrame = do_QueryFrame(newFrame);
      MOZ_ASSERT(containerFrame);
#endif
      nsContainerFrame* container = static_cast<nsContainerFrame*>(newFrame);
      nsContainerFrame* innerFrame = NS_NewBlockFrame(mPresShell, outerStyle);
      InitAndRestoreFrame(aState, content, container, innerFrame);
      outerFrame = innerFrame;

      SetInitialSingleChild(container, outerFrame);

      container->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

      // Now figure out whether newFrame or outerFrame should be the
      // absolute container.
      if (outerFrame->IsAbsPosContainingBlock()) {
        maybeAbsoluteContainingBlock = outerFrame;
        maybeAbsoluteContainingBlockStyleFrame = outerFrame;
        innerFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
      }

      // Our kids should go into the innerFrame.
      newFrame = innerFrame;
    }

    aState.AddChild(primaryFrame, aFrameList, content, aParentFrame,
                    allowOutOfFlow, allowOutOfFlow);

    nsContainerFrame* newFrameAsContainer = do_QueryFrame(newFrame);
    if (newFrameAsContainer) {
      // Process the child content if requested
      nsFrameList childList;
      nsFrameConstructorSaveState absoluteSaveState;

      if (bits & FCDATA_FORCE_NULL_ABSPOS_CONTAINER) {
        aState.PushAbsoluteContainingBlock(nullptr, nullptr, absoluteSaveState);
      } else if (!(bits & FCDATA_SKIP_ABSPOS_PUSH)) {
        maybeAbsoluteContainingBlock->AddStateBits(
            NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
        if (maybeAbsoluteContainingBlockStyleFrame->IsAbsPosContainingBlock()) {
          auto* cf =
              static_cast<nsContainerFrame*>(maybeAbsoluteContainingBlock);
          aState.PushAbsoluteContainingBlock(
              cf, maybeAbsoluteContainingBlockStyleFrame, absoluteSaveState);
        }
      }

      nsFrameConstructorSaveState floatSaveState;
      aState.MaybePushFloatContainingBlock(newFrameAsContainer, floatSaveState);

      if (bits & FCDATA_USE_CHILD_ITEMS) {
        // At this point, we have not set up the auto value for this frame, and
        // no caller will have set it so it is not redundant and therefor will
        // not assert.
        AutoFrameConstructionPageName pageNameTracker(aState,
                                                      newFrameAsContainer);
        ConstructFramesFromItemList(
            aState, aItem.mChildItems, newFrameAsContainer,
            bits & FCDATA_IS_WRAPPER_ANON_BOX, childList);
      } else {
        // Process the child frames.
        ProcessChildren(aState, content, computedStyle, newFrameAsContainer,
                        !(bits & FCDATA_DISALLOW_GENERATED_CONTENT), childList,
                        (bits & FCDATA_ALLOW_BLOCK_STYLES) != 0,
                        possiblyLeafFrame);
      }

      if (bits & FCDATA_WRAP_KIDS_IN_BLOCKS) {
        nsFrameList newList;
        nsFrameList currentBlockList;
        nsIFrame* f;
        while ((f = childList.FirstChild()) != nullptr) {
          bool wrapFrame = IsInlineFrame(f) || IsFramePartOfIBSplit(f);
          if (!wrapFrame) {
            FlushAccumulatedBlock(aState, content, newFrameAsContainer,
                                  currentBlockList, newList);
          }

          childList.RemoveFrame(f);
          if (wrapFrame) {
            currentBlockList.AppendFrame(nullptr, f);
          } else {
            newList.AppendFrame(nullptr, f);
          }
        }
        FlushAccumulatedBlock(aState, content, newFrameAsContainer,
                              currentBlockList, newList);

        if (childList.NotEmpty()) {
          // an error must have occurred, delete unprocessed frames
          DestroyFramesInList(mPresShell, childList);
        }

        childList = std::move(newList);
      }

      // Set the frame's initial child list. Note that MathML depends on this
      // being called even if childList is empty!
      newFrameAsContainer->SetInitialChildList(FrameChildListID::Principal,
                                               std::move(childList));
    }
  }

  NS_ASSERTION(newFrame->IsLineParticipant() ==
                   ((bits & FCDATA_IS_LINE_PARTICIPANT) != 0),
               "Incorrectly set FCDATA_IS_LINE_PARTICIPANT bits");

  // Even if mCreatingExtraFrames is set, we may need to SetPrimaryFrame for
  // generated content that doesn't have one yet.  Note that we have to examine
  // the frame bit, because by this point mIsGeneratedContent has been cleared
  // on aItem.
  if ((!aState.mCreatingExtraFrames ||
       (aItem.mContent->IsRootOfNativeAnonymousSubtree() &&
        !aItem.mContent->GetPrimaryFrame())) &&
      !(bits & FCDATA_SKIP_FRAMESET)) {
    aItem.mContent->SetPrimaryFrame(primaryFrame);
    ActiveLayerTracker::TransferActivityToFrame(aItem.mContent, primaryFrame);
  }
}

static void GatherSubtreeElements(Element* aElement,
                                  nsTArray<Element*>& aElements) {
  aElements.AppendElement(aElement);
  StyleChildrenIterator iter(aElement);
  for (nsIContent* c = iter.GetNextChild(); c; c = iter.GetNextChild()) {
    if (!c->IsElement()) {
      continue;
    }
    GatherSubtreeElements(c->AsElement(), aElements);
  }
}

nsresult nsCSSFrameConstructor::GetAnonymousContent(
    nsIContent* aParent, nsIFrame* aParentFrame,
    nsTArray<nsIAnonymousContentCreator::ContentInfo>& aContent) {
  nsIAnonymousContentCreator* creator = do_QueryFrame(aParentFrame);
  if (!creator) {
    return NS_OK;
  }

  nsresult rv = creator->CreateAnonymousContent(aContent);
  if (NS_FAILED(rv)) {
    // CreateAnonymousContent failed, e.g. because the page has a <use> loop.
    return rv;
  }

  if (aContent.IsEmpty()) {
    return NS_OK;
  }

  const bool devtoolsEventsEnabled =
      mDocument->DevToolsAnonymousAndShadowEventsEnabled();

  MOZ_ASSERT(aParent->IsElement());
  for (const auto& info : aContent) {
    // get our child's content and set its parent to our content
    nsIContent* content = info.mContent;
    content->SetIsNativeAnonymousRoot();

    BindContext context(*aParent->AsElement(), BindContext::ForNativeAnonymous);
    rv = content->BindToTree(context, *aParent);

    if (NS_FAILED(rv)) {
      content->UnbindFromTree();
      return rv;
    }

    if (devtoolsEventsEnabled) {
      content->QueueDevtoolsAnonymousEvent(/* aIsRemove = */ false);
    }
  }

  // Some situations where we don't cache anonymous content styles:
  //
  // * when visibility or pointer-events is anything other than the initial
  //   value; we rely on visibility and pointer-events inheriting into anonymous
  //   content, but don't bother adding this state to the AnonymousContentKey,
  //   since it's not so common. Note that with overlay scrollbars, scrollbars
  //   always start off with pointer-events: none so we don't need to check for
  //   that in that case.
  //
  // * when the medium is anything other than screen; some UA style sheet rules
  //   apply in e.g. print medium, and will give different results from the
  //   cached styles
  Maybe<bool> computedAllowStyleCaching;
  auto ComputeAllowStyleCaching = [&] {
    if (!StaticPrefs::layout_css_cached_scrollbar_styles_enabled()) {
      return false;
    }
    if (aParentFrame->StyleVisibility()->mVisible != StyleVisibility::Visible) {
      return false;
    }
    nsPresContext* pc = mPresShell->GetPresContext();
    if (!pc->UseOverlayScrollbars() &&
        aParentFrame->StyleUI()->ComputedPointerEvents() !=
            StylePointerEvents::Auto) {
      return false;
    }
    if (pc->Medium() != nsGkAtoms::screen) {
      return false;
    }
    return true;
  };

  auto AllowStyleCaching = [&] {
    if (computedAllowStyleCaching.isNothing()) {
      computedAllowStyleCaching.emplace(ComputeAllowStyleCaching());
    }
    return computedAllowStyleCaching.value();
  };

  // Compute styles for the anonymous content tree.
  ServoStyleSet* styleSet = mPresShell->StyleSet();
  for (auto& info : aContent) {
    Element* e = Element::FromNode(info.mContent);
    if (!e) {
      continue;
    }

    if (info.mKey == AnonymousContentKey::None || !AllowStyleCaching()) {
      // Most NAC subtrees do not use caching of computed styles.  Just go
      // ahead and eagerly style the subtree.
      styleSet->StyleNewSubtree(e);
      continue;
    }

    // We have a NAC subtree for which we can use cached styles.
    AutoTArray<RefPtr<ComputedStyle>, 2> cachedStyles;
    AutoTArray<Element*, 2> elements;

    GatherSubtreeElements(e, elements);
    styleSet->GetCachedAnonymousContentStyles(info.mKey, cachedStyles);

    if (cachedStyles.IsEmpty()) {
      // We haven't stored cached styles for this kind of NAC subtree yet.
      // Eagerly compute those styles, then cache them for later.
      styleSet->StyleNewSubtree(e);
      for (Element* e : elements) {
        if (e->HasServoData()) {
          cachedStyles.AppendElement(ServoStyleSet::ResolveServoStyle(*e));
        } else {
          cachedStyles.AppendElement(nullptr);
        }
      }
      styleSet->PutCachedAnonymousContentStyles(info.mKey,
                                                std::move(cachedStyles));
      continue;
    }

    // We previously stored cached styles for this kind of NAC subtree.
    // Iterate over them and set them on the subtree's elements.
    MOZ_ASSERT(cachedStyles.Length() == elements.Length(),
               "should always produce the same size NAC subtree");
    for (size_t i = 0, len = cachedStyles.Length(); i != len; ++i) {
      if (cachedStyles[i]) {
#ifdef DEBUG
        // Assert that our cached style is the same as one we could compute.
        RefPtr<ComputedStyle> cs = styleSet->ResolveStyleLazily(*elements[i]);
        MOZ_ASSERT(
            cachedStyles[i]->EqualForCachedAnonymousContentStyle(*cs),
            "cached anonymous content styles should be identical to those we "
            "would compute normally");
        // All overlay scrollbars start off as inactive, so we can rely on their
        // pointer-events value being always none.
        MOZ_ASSERT(!mPresShell->GetPresContext()->UseOverlayScrollbars() ||
                   cs->StyleUI()->ComputedPointerEvents() ==
                       StylePointerEvents::None);
#endif
        Servo_SetExplicitStyle(elements[i], cachedStyles[i]);
      }
    }
  }

  return NS_OK;
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULTagData(const Element& aElement,
                                      ComputedStyle& aStyle) {
  MOZ_ASSERT(aElement.IsXULElement());
  static constexpr FrameConstructionData kPopupData(NS_NewMenuPopupFrame);

  static constexpr FrameConstructionDataByTag sXULTagData[] = {
      SIMPLE_TAG_CREATE(image, NS_NewXULImageFrame),
      SIMPLE_TAG_CREATE(treechildren, NS_NewTreeBodyFrame),
      SIMPLE_TAG_CHAIN(label,
                       nsCSSFrameConstructor::FindXULLabelOrDescriptionData),
      SIMPLE_TAG_CHAIN(description,
                       nsCSSFrameConstructor::FindXULLabelOrDescriptionData),
      SIMPLE_TAG_CREATE(iframe, NS_NewSubDocumentFrame),
      SIMPLE_TAG_CREATE(editor, NS_NewSubDocumentFrame),
      SIMPLE_TAG_CREATE(browser, NS_NewSubDocumentFrame),
      SIMPLE_TAG_CREATE(splitter, NS_NewSplitterFrame),
      SIMPLE_TAG_CREATE(scrollbar, NS_NewScrollbarFrame),
      SIMPLE_TAG_CREATE(slider, NS_NewSliderFrame),
      SIMPLE_TAG_CREATE(thumb, NS_NewSimpleXULLeafFrame),
      SIMPLE_TAG_CREATE(scrollcorner, NS_NewSimpleXULLeafFrame),
      SIMPLE_TAG_CREATE(resizer, NS_NewSimpleXULLeafFrame),
      SIMPLE_TAG_CREATE(scrollbarbutton, NS_NewScrollbarButtonFrame),
      {nsGkAtoms::panel, kPopupData},
      {nsGkAtoms::menupopup, kPopupData},
      {nsGkAtoms::tooltip, kPopupData},
  };

  return FindDataByTag(aElement, aStyle, sXULTagData, std::size(sXULTagData));
}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindXULLabelOrDescriptionData(const Element& aElement,
                                                     ComputedStyle&) {
  // Follow CSS display value if no value attribute
  if (!aElement.HasAttr(nsGkAtoms::value)) {
    return nullptr;
  }

  // Follow CSS display if there's no crop="center".
  if (!aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::crop,
                            nsGkAtoms::center, eCaseMatters)) {
    return nullptr;
  }

  static constexpr FrameConstructionData sMiddleCroppingData(
      NS_NewMiddleCroppingLabelFrame);
  return &sMiddleCroppingData;
}

already_AddRefed<ComputedStyle>
nsCSSFrameConstructor::BeginBuildingScrollContainerFrame(
    nsFrameConstructorState& aState, nsIContent* aContent,
    ComputedStyle* aContentStyle, nsContainerFrame* aParentFrame, bool aIsRoot,
    nsContainerFrame*& aNewFrame) {
  nsContainerFrame* scrollContainerFrame = aNewFrame;

  if (!scrollContainerFrame) {
    scrollContainerFrame =
        NS_NewScrollContainerFrame(mPresShell, aContentStyle, aIsRoot);
    InitAndRestoreFrame(aState, aContent, aParentFrame, scrollContainerFrame);
  }

  MOZ_ASSERT(scrollContainerFrame);

  // if there are any anonymous children for the scroll frame, create
  // frames for them.
  //
  // We can't take the normal ProcessChildren path, because the NAC needs to
  // be parented to the scrollframe, and everything else needs to be parented
  // to the scrolledframe.
  AutoTArray<nsIAnonymousContentCreator::ContentInfo, 4> scrollNAC;
  DebugOnly<nsresult> rv =
      GetAnonymousContent(aContent, scrollContainerFrame, scrollNAC);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  nsFrameList anonymousList;
  if (!scrollNAC.IsEmpty()) {
    nsFrameConstructorSaveState floatSaveState;
    aState.MaybePushFloatContainingBlock(scrollContainerFrame, floatSaveState);

    AutoFrameConstructionItemList items(this);
    AutoFrameConstructionPageName pageNameTracker(aState, scrollContainerFrame);
    AddFCItemsForAnonymousContent(aState, scrollContainerFrame, scrollNAC,
                                  items, pageNameTracker);
    ConstructFramesFromItemList(aState, items, scrollContainerFrame,
                                /* aParentIsWrapperAnonBox = */ false,
                                anonymousList);
  }

  aNewFrame = scrollContainerFrame;
  scrollContainerFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  // we used the style that was passed in. So resolve another one.
  ServoStyleSet* styleSet = mPresShell->StyleSet();
  RefPtr<ComputedStyle> scrolledChildStyle =
      styleSet->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozScrolledContent, aContentStyle);

  scrollContainerFrame->SetInitialChildList(FrameChildListID::Principal,
                                            std::move(anonymousList));

  return scrolledChildStyle.forget();
}

void nsCSSFrameConstructor::FinishBuildingScrollContainerFrame(
    nsContainerFrame* aScrollContainerFrame, nsIFrame* aScrolledFrame) {
  aScrollContainerFrame->AppendFrames(
      FrameChildListID::Principal, nsFrameList(aScrolledFrame, aScrolledFrame));
}

void nsCSSFrameConstructor::BuildScrollContainerFrame(
    nsFrameConstructorState& aState, nsIContent* aContent,
    ComputedStyle* aContentStyle, nsIFrame* aScrolledFrame,
    nsContainerFrame* aParentFrame, nsContainerFrame*& aNewFrame) {
  RefPtr<ComputedStyle> scrolledContentStyle =
      BeginBuildingScrollContainerFrame(aState, aContent, aContentStyle,
                                        aParentFrame, false, aNewFrame);

  aScrolledFrame->SetComputedStyleWithoutNotification(scrolledContentStyle);
  InitAndRestoreFrame(aState, aContent, aNewFrame, aScrolledFrame);

  FinishBuildingScrollContainerFrame(aNewFrame, aScrolledFrame);
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDisplayData(const nsStyleDisplay& aDisplay,
                                       const Element& aElement) {
  static_assert(eParentTypeCount < (1 << (32 - FCDATA_PARENT_TYPE_OFFSET)),
                "Check eParentTypeCount should not overflow");

  // The style system ensures that floated and positioned frames are
  // block-level.
  NS_ASSERTION(
      !(aDisplay.IsFloatingStyle() || aDisplay.IsAbsolutelyPositionedStyle()) ||
          aDisplay.IsBlockOutsideStyle(),
      "Style system did not apply CSS2.1 section 9.7 fixups");

  // If this is "body", try propagating its scroll style to the viewport
  // Note that we need to do this even if the body is NOT scrollable;
  // it might have dynamically changed from scrollable to not scrollable,
  // and that might need to be propagated.
  // XXXbz is this the right place to do this?  If this code moves,
  // make this function static.
  bool propagatedScrollToViewport = false;
  if (aElement.IsHTMLElement(nsGkAtoms::body)) {
    if (nsPresContext* presContext = mPresShell->GetPresContext()) {
      propagatedScrollToViewport =
          presContext->UpdateViewportScrollStylesOverride() == &aElement;
      MOZ_ASSERT(!propagatedScrollToViewport ||
                     !mPresShell->GetPresContext()->IsPaginated(),
                 "Shouldn't propagate scroll in paginated contexts");
    }
  }

  switch (aDisplay.DisplayInside()) {
    case StyleDisplayInside::Flow:
    case StyleDisplayInside::FlowRoot: {
      if (aDisplay.IsInlineFlow()) {
        static constexpr FrameConstructionData data(
            &nsCSSFrameConstructor::ConstructInline,
            FCDATA_IS_INLINE | FCDATA_IS_LINE_PARTICIPANT);
        return &data;
      }

      // If the frame is a block-level frame and is scrollable, then wrap it in
      // a scroll frame.  Except we don't want to do that for paginated contexts
      // for frames that are block-outside and aren't frames for native
      // anonymous stuff.
      // XXX Ignore tables for the time being (except caption)
      const uint32_t kCaptionCtorFlags =
          FCDATA_IS_TABLE_PART | FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable);
      const bool caption = aDisplay.mDisplay == StyleDisplay::TableCaption;
      const bool needScrollFrame =
          aDisplay.IsScrollableOverflow() && !propagatedScrollToViewport;
      if (needScrollFrame) {
        const bool suppressScrollFrame =
            mPresShell->GetPresContext()->IsPaginated() &&
            aDisplay.IsBlockOutsideStyle() &&
            !aElement.IsInNativeAnonymousSubtree();
        if (!suppressScrollFrame) {
          static constexpr FrameConstructionData sScrollableBlockData[2] = {
              {&nsCSSFrameConstructor::ConstructScrollableBlock},
              {&nsCSSFrameConstructor::ConstructScrollableBlock,
               kCaptionCtorFlags}};
          return &sScrollableBlockData[caption];
        }
      }

      // Handle various non-scrollable blocks.
      static constexpr FrameConstructionData sNonScrollableBlockData[2] = {
          {&nsCSSFrameConstructor::ConstructNonScrollableBlock},
          {&nsCSSFrameConstructor::ConstructNonScrollableBlock,
           kCaptionCtorFlags}};
      return &sNonScrollableBlockData[caption];
    }
    case StyleDisplayInside::Table: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTable);
      return &data;
    }
    // NOTE: In the unlikely event that we add another table-part here that
    // has a desired-parent-type (& hence triggers table fixup), we'll need to
    // also update the flexbox chunk in ComputedStyle::ApplyStyleFixups().
    case StyleDisplayInside::TableRowGroup: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable));
      return &data;
    }
    case StyleDisplayInside::TableColumn: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableCol,
          FCDATA_IS_TABLE_PART |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeColGroup));
      return &data;
    }
    case StyleDisplayInside::TableColumnGroup: {
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewTableColGroupFrame),
          FCDATA_IS_TABLE_PART | FCDATA_DISALLOW_OUT_OF_FLOW |
              FCDATA_SKIP_ABSPOS_PUSH |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable));
      return &data;
    }
    case StyleDisplayInside::TableHeaderGroup: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable));
      return &data;
    }
    case StyleDisplayInside::TableFooterGroup: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable));
      return &data;
    }
    case StyleDisplayInside::TableRow: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRowGroup));
      return &data;
    }
    case StyleDisplayInside::TableCell: {
      static constexpr FrameConstructionData data(
          &nsCSSFrameConstructor::ConstructTableCell,
          FCDATA_IS_TABLE_PART | FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRow));
      return &data;
    }
    case StyleDisplayInside::Flex:
    case StyleDisplayInside::WebkitBox: {
      static constexpr FrameConstructionData nonScrollableData(
          ToCreationFunc(NS_NewFlexContainerFrame));
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewFlexContainerFrame),
          FCDATA_MAY_NEED_SCROLLFRAME);
      return MOZ_UNLIKELY(propagatedScrollToViewport) ? &nonScrollableData
                                                      : &data;
    }
    case StyleDisplayInside::Grid: {
      static constexpr FrameConstructionData nonScrollableData(
          ToCreationFunc(NS_NewGridContainerFrame));
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewGridContainerFrame),
          FCDATA_MAY_NEED_SCROLLFRAME);
      return MOZ_UNLIKELY(propagatedScrollToViewport) ? &nonScrollableData
                                                      : &data;
    }
    case StyleDisplayInside::Ruby: {
      static constexpr FrameConstructionData data[] = {
          {&nsCSSFrameConstructor::ConstructBlockRubyFrame,
           FCDATA_MAY_NEED_SCROLLFRAME},
          {ToCreationFunc(NS_NewRubyFrame), FCDATA_IS_LINE_PARTICIPANT}};
      bool isInline = aDisplay.DisplayOutside() == StyleDisplayOutside::Inline;
      return &data[isInline];
    }
    case StyleDisplayInside::RubyBase: {
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewRubyBaseFrame),
          FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRubyBaseContainer));
      return &data;
    }
    case StyleDisplayInside::RubyBaseContainer: {
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewRubyBaseContainerFrame),
          FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRuby));
      return &data;
    }
    case StyleDisplayInside::RubyText: {
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewRubyTextFrame),
          FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRubyTextContainer));
      return &data;
    }
    case StyleDisplayInside::RubyTextContainer: {
      static constexpr FrameConstructionData data(
          ToCreationFunc(NS_NewRubyTextContainerFrame),
          FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRuby));
      return &data;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("unknown 'display' value");
      return nullptr;
  }
}

nsIFrame* nsCSSFrameConstructor::ConstructScrollableBlock(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  nsContainerFrame* newFrame = nullptr;
  ConstructScrollableBlockWithScrollContainer(aState, aItem, aParentFrame,
                                              aDisplay, aFrameList, newFrame);
  MOZ_ASSERT(newFrame);
  return newFrame;
}

void nsCSSFrameConstructor::ConstructScrollableBlockWithScrollContainer(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList, nsContainerFrame*& aNewFrame) {
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;
  MOZ_ASSERT_IF(aNewFrame, aNewFrame->IsScrollContainerOrSubclass());

  RefPtr<ComputedStyle> scrolledContentStyle =
      BeginBuildingScrollContainerFrame(
          aState, content, computedStyle,
          aState.GetGeometricParent(*aDisplay, aParentFrame), false, aNewFrame);

  // Create our block frame
  // pass a temporary stylecontext, the correct one will be set later
  nsContainerFrame* scrolledFrame = NS_NewBlockFrame(mPresShell, computedStyle);

  // Make sure to AddChild before we call ConstructBlock so that we
  // end up before our descendants in fixed-pos lists as needed.
  aState.AddChild(aNewFrame, aFrameList, content, aParentFrame);

  nsFrameList blockList;
  ConstructBlock(aState, content, aNewFrame, aNewFrame, scrolledContentStyle,
                 &scrolledFrame, blockList,
                 aNewFrame->IsAbsPosContainingBlock() ? aNewFrame : nullptr);

  MOZ_ASSERT(blockList.OnlyChild() == scrolledFrame,
             "Scrollframe's frameList should be exactly the scrolled frame!");
  FinishBuildingScrollContainerFrame(aNewFrame, scrolledFrame);
}

nsIFrame* nsCSSFrameConstructor::ConstructNonScrollableBlock(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  ComputedStyle* const computedStyle = aItem.mComputedStyle;
  nsContainerFrame* newFrame = NS_NewBlockFrame(mPresShell, computedStyle);
  ConstructBlock(aState, aItem.mContent,
                 aState.GetGeometricParent(*aDisplay, aParentFrame),
                 aParentFrame, computedStyle, &newFrame, aFrameList,
                 newFrame->IsAbsPosContainingBlock() ? newFrame : nullptr);
  return newFrame;
}

void nsCSSFrameConstructor::InitAndRestoreFrame(
    const nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame, nsIFrame* aNewFrame,
    AllowCounters aAllowCounters) {
  MOZ_ASSERT(aNewFrame, "Null frame cannot be initialized");

  // Initialize the frame
  aNewFrame->Init(aContent, aParentFrame, nullptr);
  aNewFrame->AddStateBits(aState.mAdditionalStateBits);

  if (aState.mFrameState) {
    // Restore frame state for just the newly created frame.
    RestoreFrameStateFor(aNewFrame, aState.mFrameState);
  }

  if (aAllowCounters == AllowCounters::Yes &&
      mContainStyleScopeManager.AddCounterChanges(aNewFrame)) {
    CountersDirty();
  }
}

already_AddRefed<ComputedStyle> nsCSSFrameConstructor::ResolveComputedStyle(
    nsIContent* aContent) {
  if (auto* element = Element::FromNode(aContent)) {
    return ServoStyleSet::ResolveServoStyle(*element);
  }

  MOZ_ASSERT(aContent->IsText(),
             "shouldn't waste time creating ComputedStyles for "
             "comments and processing instructions");

  Element* parent = aContent->GetFlattenedTreeParentElement();
  MOZ_ASSERT(parent, "Text out of the flattened tree?");

  // FIXME(emilio): The const_cast is unfortunate, but it's not worse than what
  // we did before.
  //
  // We could use ResolveServoStyle, but that would involve extra unnecessary
  // refcount traffic...
  auto* parentStyle =
      const_cast<ComputedStyle*>(Servo_Element_GetMaybeOutOfDateStyle(parent));
  MOZ_ASSERT(parentStyle,
             "How are we inserting text frames in an unstyled element?");
  return mPresShell->StyleSet()->ResolveStyleForText(aContent, parentStyle);
}

// MathML Mod - RBS
void nsCSSFrameConstructor::FlushAccumulatedBlock(
    nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame, nsFrameList& aBlockList,
    nsFrameList& aNewList) {
  if (aBlockList.IsEmpty()) {
    // Nothing to do
    return;
  }

  auto anonPseudo = PseudoStyleType::MozMathmlAnonymousBlock;

  ComputedStyle* parentContext =
      nsIFrame::CorrectStyleParentFrame(aParentFrame, anonPseudo)->Style();
  ServoStyleSet* styleSet = mPresShell->StyleSet();
  RefPtr<ComputedStyle> blockContext =
      styleSet->ResolveInheritingAnonymousBoxStyle(anonPseudo, parentContext);

  // then, create a block frame that will wrap the child frames. Make it a
  // MathML frame so that Get(Absolute/Float)ContainingBlockFor know that this
  // is not a suitable block.
  nsContainerFrame* blockFrame =
      NS_NewMathMLmathBlockFrame(mPresShell, blockContext);

  InitAndRestoreFrame(aState, aContent, aParentFrame, blockFrame);
  ReparentFrames(this, blockFrame, aBlockList, false);
  // We have to walk over aBlockList before we hand it over to blockFrame.
  for (nsIFrame* f : aBlockList) {
    f->SetParentIsWrapperAnonBox();
  }
  // abs-pos and floats are disabled in MathML children so we don't have to
  // worry about messing up those.
  blockFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(aBlockList));
  aNewList.AppendFrame(nullptr, blockFrame);
}

// Only <math> elements can be floated or positioned.  All other MathML
// should be in-flow.
#define MATHML_DATA(_func)                                                    \
  FrameConstructionData {                                                     \
    _func, FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_FORCE_NULL_ABSPOS_CONTAINER | \
               FCDATA_WRAP_KIDS_IN_BLOCKS                                     \
  }

#define SIMPLE_MATHML_CREATE(_tag, _func) {nsGkAtoms::_tag, MATHML_DATA(_func)}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindMathMLData(const Element& aElement,
                                      ComputedStyle& aStyle) {
  MOZ_ASSERT(aElement.IsMathMLElement());

  nsAtom* tag = aElement.NodeInfo()->NameAtom();

  // Handle <math> specially, because it sometimes produces inlines
  if (tag == nsGkAtoms::math) {
    // The IsBlockOutsideStyle() check must match what
    // specified::Display::equivalent_block_display is checking for
    // already-block-outside things. Though the behavior here for the
    // display:table case is pretty weird...
    if (aStyle.StyleDisplay()->IsBlockOutsideStyle()) {
      static constexpr FrameConstructionData sBlockMathData(
          ToCreationFunc(NS_NewMathMLmathBlockFrame),
          FCDATA_FORCE_NULL_ABSPOS_CONTAINER | FCDATA_WRAP_KIDS_IN_BLOCKS);
      return &sBlockMathData;
    }

    static constexpr FrameConstructionData sInlineMathData(
        ToCreationFunc(NS_NewMathMLmathInlineFrame),
        FCDATA_FORCE_NULL_ABSPOS_CONTAINER | FCDATA_IS_LINE_PARTICIPANT |
            FCDATA_WRAP_KIDS_IN_BLOCKS);
    return &sInlineMathData;
  }

  // Special case for elements with a display value other than none
  // specified in mathml.css that are not handled by this function.
  // These shouldn't be rendered as an mrow.
  if (tag == nsGkAtoms::mtable || tag == nsGkAtoms::mtr ||
      tag == nsGkAtoms::mlabeledtr || tag == nsGkAtoms::mtd) {
    return nullptr;
  }

  static constexpr FrameConstructionDataByTag sMathMLData[] = {
      SIMPLE_MATHML_CREATE(annotation, NS_NewMathMLTokenFrame),
      SIMPLE_MATHML_CREATE(annotation_xml, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mi, NS_NewMathMLTokenFrame),
      SIMPLE_MATHML_CREATE(mn, NS_NewMathMLTokenFrame),
      SIMPLE_MATHML_CREATE(ms, NS_NewMathMLTokenFrame),
      SIMPLE_MATHML_CREATE(mtext, NS_NewMathMLTokenFrame),
      SIMPLE_MATHML_CREATE(mo, NS_NewMathMLmoFrame),
      SIMPLE_MATHML_CREATE(mfrac, NS_NewMathMLmfracFrame),
      SIMPLE_MATHML_CREATE(msup, NS_NewMathMLmmultiscriptsFrame),
      SIMPLE_MATHML_CREATE(msub, NS_NewMathMLmmultiscriptsFrame),
      SIMPLE_MATHML_CREATE(msubsup, NS_NewMathMLmmultiscriptsFrame),
      SIMPLE_MATHML_CREATE(munder, NS_NewMathMLmunderoverFrame),
      SIMPLE_MATHML_CREATE(mover, NS_NewMathMLmunderoverFrame),
      SIMPLE_MATHML_CREATE(munderover, NS_NewMathMLmunderoverFrame),
      SIMPLE_MATHML_CREATE(mphantom, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mpadded, NS_NewMathMLmpaddedFrame),
      SIMPLE_MATHML_CREATE(mspace, NS_NewMathMLmspaceFrame),
      SIMPLE_MATHML_CREATE(none, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mprescripts, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mfenced, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mmultiscripts, NS_NewMathMLmmultiscriptsFrame),
      SIMPLE_MATHML_CREATE(mstyle, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(msqrt, NS_NewMathMLmrootFrame),
      SIMPLE_MATHML_CREATE(mroot, NS_NewMathMLmrootFrame),
      SIMPLE_MATHML_CREATE(maction, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(mrow, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(merror, NS_NewMathMLmrowFrame),
      SIMPLE_MATHML_CREATE(menclose, NS_NewMathMLmencloseFrame),
      SIMPLE_MATHML_CREATE(semantics, NS_NewMathMLmrowFrame)};

  if (const auto* data = FindDataByTag(aElement, aStyle, sMathMLData,
                                       std::size(sMathMLData))) {
    return data;
  }
  // Unknown MathML elements render as an mrow, see:
  // https://w3c.github.io/mathml-core/#ref-for-dfn-unknown-mathml-element-2
  static constexpr FrameConstructionData sMrowData =
      MATHML_DATA(NS_NewMathMLmrowFrame);
  return &sMrowData;
}

nsContainerFrame* nsCSSFrameConstructor::ConstructSVGFrameWithAnonymousChild(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, nsFrameList& aFrameList,
    ContainerFrameCreationFunc aConstructor,
    ContainerFrameCreationFunc aInnerConstructor, PseudoStyleType aInnerPseudo,
    bool aCandidateRootFrame) {
  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  // Create the outer frame:
  nsContainerFrame* newFrame = aConstructor(mPresShell, computedStyle);

  InitAndRestoreFrame(aState, content,
                      aCandidateRootFrame
                          ? aState.GetGeometricParent(
                                *computedStyle->StyleDisplay(), aParentFrame)
                          : aParentFrame,
                      newFrame);
  newFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);

  // Create the pseudo SC for the anonymous wrapper child as a child of the SC:
  RefPtr<ComputedStyle> scForAnon =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(aInnerPseudo,
                                                                 computedStyle);

  // Create the anonymous inner wrapper frame
  nsContainerFrame* innerFrame = aInnerConstructor(mPresShell, scForAnon);

  InitAndRestoreFrame(aState, content, newFrame, innerFrame);

  // Put the newly created frames into the right child list
  SetInitialSingleChild(newFrame, innerFrame);

  aState.AddChild(newFrame, aFrameList, content, aParentFrame,
                  aCandidateRootFrame, aCandidateRootFrame);

  if (!mRootElementFrame && aCandidateRootFrame) {
    mRootElementFrame = newFrame;
  }

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(innerFrame, floatSaveState);

  nsFrameList childList;

  // Process children
  if (aItem.mFCData->mBits & FCDATA_USE_CHILD_ITEMS) {
    ConstructFramesFromItemList(
        aState, aItem.mChildItems, innerFrame,
        aItem.mFCData->mBits & FCDATA_IS_WRAPPER_ANON_BOX, childList);
  } else {
    ProcessChildren(aState, content, computedStyle, innerFrame,
                    /* aCanHaveGeneratedContent = */ false, childList, false);
  }

  // Set the inner wrapper frame's initial primary list
  innerFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(childList));

  return newFrame;
}

nsIFrame* nsCSSFrameConstructor::ConstructOuterSVG(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  return ConstructSVGFrameWithAnonymousChild(
      aState, aItem, aParentFrame, aFrameList, NS_NewSVGOuterSVGFrame,
      NS_NewSVGOuterSVGAnonChildFrame, PseudoStyleType::MozSvgOuterSvgAnonChild,
      true);
}

nsIFrame* nsCSSFrameConstructor::ConstructMarker(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  return ConstructSVGFrameWithAnonymousChild(
      aState, aItem, aParentFrame, aFrameList, NS_NewSVGMarkerFrame,
      NS_NewSVGMarkerAnonChildFrame, PseudoStyleType::MozSvgMarkerAnonChild,
      false);
}

// Only outer <svg> elements can be floated or positioned.  All other SVG
// should be in-flow.
#define SIMPLE_SVG_FCDATA(_func)                      \
  FrameConstructionData(ToCreationFunc(_func),        \
                        FCDATA_DISALLOW_OUT_OF_FLOW | \
                            FCDATA_SKIP_ABSPOS_PUSH | \
                            FCDATA_DISALLOW_GENERATED_CONTENT)
#define SIMPLE_SVG_CREATE(_tag, _func) \
  {nsGkAtoms::_tag, SIMPLE_SVG_FCDATA(_func)}

/* static */
const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindSVGData(const Element& aElement,
                                   nsIFrame* aParentFrame,
                                   bool aIsWithinSVGText,
                                   bool aAllowsTextPathChild,
                                   ComputedStyle& aStyle) {
  MOZ_ASSERT(aElement.IsSVGElement());

  static constexpr FrameConstructionData sSuppressData = SUPPRESS_FCDATA();
  static constexpr FrameConstructionData sContainerData =
      SIMPLE_SVG_FCDATA(NS_NewSVGContainerFrame);

  bool parentIsSVG = aIsWithinSVGText;
  nsIContent* parentContent =
      aParentFrame ? aParentFrame->GetContent() : nullptr;

  nsAtom* tag = aElement.NodeInfo()->NameAtom();

  if (aElement.OwnerDoc()->IsSVGGlyphsDocument()) {
    // SVG elements that are explicitly not supported in svg-glyphs documents.
    // (Partially applies the restrictions mentioned at
    // https://learn.microsoft.com/en-us/typography/opentype/spec/svg#svg-capability-requirements-and-restrictions
    if (tag == nsGkAtoms::text || tag == nsGkAtoms::tspan ||
        tag == nsGkAtoms::textPath || tag == nsGkAtoms::a ||
        tag == nsGkAtoms::foreignObject || tag == nsGkAtoms::svgSwitch ||
        tag == nsGkAtoms::view) {
      return &sSuppressData;
    }
  }

  // XXXbz should this really be based on the tag of the parent frame's content?
  // Should it not be based on the type of the parent frame (e.g. whether it's
  // an SVG frame)?
  if (parentContent) {
    // It's not clear whether the SVG spec intends to allow any SVG
    // content within svg:foreignObject at all (SVG 1.1, section
    // 23.2), but if it does, it better be svg:svg.  So given that
    // we're allowing it, treat it as a non-SVG parent.
    parentIsSVG =
        parentContent->IsSVGElement() &&
        parentContent->NodeInfo()->NameAtom() != nsGkAtoms::foreignObject;
  }

  if ((tag != nsGkAtoms::svg && !parentIsSVG) ||
      (tag == nsGkAtoms::desc || tag == nsGkAtoms::title ||
       tag == nsGkAtoms::metadata)) {
    // Sections 5.1 and G.4 of SVG 1.1 say that SVG elements other than
    // svg:svg not contained within svg:svg are incorrect, although they
    // don't seem to specify error handling.  Ignore them, since many of
    // our frame classes can't deal.  It *may* be that the document
    // should at that point be considered in error according to F.2, but
    // it's hard to tell.
    //
    // We don't currently handle any UI for desc/title/metadata
    return &sSuppressData;
  }

  // We don't need frames for animation elements
  if (aElement.IsSVGAnimationElement()) {
    return &sSuppressData;
  }

  if (tag == nsGkAtoms::svg && !parentIsSVG) {
    // We need outer <svg> elements to have an SVGOuterSVGFrame regardless
    // of whether they fail conditional processing attributes, since various
    // SVG frames assume that one exists.  We handle the non-rendering
    // of failing outer <svg> element contents like <switch> statements,
    // and do the PassesConditionalProcessingTests call in
    // SVGOuterSVGFrame::Init.
    static constexpr FrameConstructionData sOuterSVGData(
        &nsCSSFrameConstructor::ConstructOuterSVG);
    return &sOuterSVGData;
  }

  if (tag == nsGkAtoms::marker) {
    static constexpr FrameConstructionData sMarkerSVGData(
        &nsCSSFrameConstructor::ConstructMarker);
    return &sMarkerSVGData;
  }

  if (!aElement.PassesConditionalProcessingTests()) {
    // Elements with failing conditional processing attributes never get
    // rendered.  Note that this is not where we select which frame in a
    // <switch> to render!  That happens in SVGSwitchFrame::PaintSVG.
    if (aIsWithinSVGText) {
      // SVGTextFrame doesn't handle conditional processing attributes,
      // so don't create frames for descendants of <text> with failing
      // attributes.  We need frames not to be created so that text layout
      // is correct.
      return &sSuppressData;
    }
    // If we're not inside <text>, create an SVGContainerFrame (which is a
    // frame that doesn't render) so that paint servers can still be referenced,
    // even if they live inside an element with failing conditional processing
    // attributes.
    return &sContainerData;
  }

  // Ensure that a stop frame is a child of a gradient and that gradients
  // can only have stop children.
  bool parentIsGradient = aParentFrame && static_cast<SVGGradientFrame*>(
                                              do_QueryFrame(aParentFrame));
  bool stop = (tag == nsGkAtoms::stop);
  if ((parentIsGradient && !stop) || (!parentIsGradient && stop)) {
    return &sSuppressData;
  }

  // Prevent bad frame types being children of filters or parents of filter
  // primitives.  If aParentFrame is null, we know that the frame that will
  // be created will be an nsInlineFrame, so it can never be a filter.
  bool parentIsFilter = aParentFrame && aParentFrame->IsSVGFilterFrame();
  if ((parentIsFilter && !aElement.IsSVGFilterPrimitiveElement()) ||
      (!parentIsFilter && aElement.IsSVGFilterPrimitiveElement())) {
    return &sSuppressData;
  }

  // Prevent bad frame types being children of filter primitives or parents of
  // filter primitive children.  If aParentFrame is null, we know that the frame
  // that will be created will be an nsInlineFrame, so it can never be a filter
  // primitive.
  bool parentIsFEContainerFrame =
      aParentFrame && aParentFrame->IsSVGFEContainerFrame();
  if ((parentIsFEContainerFrame &&
       !aElement.IsSVGFilterPrimitiveChildElement()) ||
      (!parentIsFEContainerFrame &&
       aElement.IsSVGFilterPrimitiveChildElement())) {
    return &sSuppressData;
  }

  // Special cases for text/tspan/textPath, because the kind of frame
  // they get depends on the parent frame.  We ignore 'a' elements when
  // determining the parent, however.
  if (aIsWithinSVGText) {
    // If aIsWithinSVGText is true, then we know that the "SVG text uses
    // CSS frames" pref was true when this SVG fragment was first constructed.
    //
    // FIXME(bug 1588477) Don't render stuff in display: contents / Shadow DOM
    // subtrees, because TextCorrespondenceRecorder in the SVG text code doesn't
    // really know how to deal with it. This kinda sucks. :(
    if (aParentFrame && aParentFrame->GetContent() != aElement.GetParent()) {
      return &sSuppressData;
    }

    // We don't use ConstructInline because we want different behavior
    // for generated content.
    static constexpr FrameConstructionData sTSpanData(
        ToCreationFunc(NS_NewInlineFrame),
        FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_SKIP_ABSPOS_PUSH |
            FCDATA_DISALLOW_GENERATED_CONTENT | FCDATA_IS_LINE_PARTICIPANT |
            FCDATA_IS_INLINE | FCDATA_USE_CHILD_ITEMS);
    if (tag == nsGkAtoms::textPath) {
      if (aAllowsTextPathChild) {
        return &sTSpanData;
      }
    } else if (tag == nsGkAtoms::tspan || tag == nsGkAtoms::a) {
      return &sTSpanData;
    }
    return &sSuppressData;
  } else if (tag == nsGkAtoms::tspan || tag == nsGkAtoms::textPath) {
    return &sSuppressData;
  }

  static constexpr FrameConstructionDataByTag sSVGData[] = {
      SIMPLE_SVG_CREATE(svg, NS_NewSVGInnerSVGFrame),
      SIMPLE_SVG_CREATE(g, NS_NewSVGGFrame),
      SIMPLE_SVG_CREATE(svgSwitch, NS_NewSVGSwitchFrame),
      SIMPLE_SVG_CREATE(symbol, NS_NewSVGSymbolFrame),
      SIMPLE_SVG_CREATE(polygon, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(polyline, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(circle, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(ellipse, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(line, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(rect, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(path, NS_NewSVGGeometryFrame),
      SIMPLE_SVG_CREATE(defs, NS_NewSVGContainerFrame),
      {nsGkAtoms::text,
       {NS_NewSVGTextFrame,
        FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_ALLOW_BLOCK_STYLES |
            FCDATA_DISALLOW_GENERATED_CONTENT,
        PseudoStyleType::MozSvgText}},
      {nsGkAtoms::foreignObject,
       {ToCreationFunc(NS_NewSVGForeignObjectFrame),
        FCDATA_DISALLOW_OUT_OF_FLOW, PseudoStyleType::MozSvgForeignContent}},
      SIMPLE_SVG_CREATE(a, NS_NewSVGAFrame),
      SIMPLE_SVG_CREATE(linearGradient, NS_NewSVGLinearGradientFrame),
      SIMPLE_SVG_CREATE(radialGradient, NS_NewSVGRadialGradientFrame),
      SIMPLE_SVG_CREATE(stop, NS_NewSVGStopFrame),
      SIMPLE_SVG_CREATE(use, NS_NewSVGUseFrame),
      SIMPLE_SVG_CREATE(view, NS_NewSVGViewFrame),
      SIMPLE_SVG_CREATE(image, NS_NewSVGImageFrame),
      SIMPLE_SVG_CREATE(clipPath, NS_NewSVGClipPathFrame),
      SIMPLE_SVG_CREATE(filter, NS_NewSVGFilterFrame),
      SIMPLE_SVG_CREATE(pattern, NS_NewSVGPatternFrame),
      SIMPLE_SVG_CREATE(mask, NS_NewSVGMaskFrame),
      SIMPLE_SVG_CREATE(feDistantLight, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(fePointLight, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feSpotLight, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feBlend, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feColorMatrix, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feFuncR, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feFuncG, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feFuncB, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feFuncA, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feComposite, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feComponentTransfer, NS_NewSVGFEContainerFrame),
      SIMPLE_SVG_CREATE(feConvolveMatrix, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feDiffuseLighting, NS_NewSVGFEContainerFrame),
      SIMPLE_SVG_CREATE(feDisplacementMap, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feDropShadow, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feFlood, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feGaussianBlur, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feImage, NS_NewSVGFEImageFrame),
      SIMPLE_SVG_CREATE(feMerge, NS_NewSVGFEContainerFrame),
      SIMPLE_SVG_CREATE(feMergeNode, NS_NewSVGFEUnstyledLeafFrame),
      SIMPLE_SVG_CREATE(feMorphology, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feOffset, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feSpecularLighting, NS_NewSVGFEContainerFrame),
      SIMPLE_SVG_CREATE(feTile, NS_NewSVGFELeafFrame),
      SIMPLE_SVG_CREATE(feTurbulence, NS_NewSVGFELeafFrame)};

  const FrameConstructionData* data =
      FindDataByTag(aElement, aStyle, sSVGData, std::size(sSVGData));

  if (!data) {
    data = &sContainerData;
  }

  return data;
}

void nsCSSFrameConstructor::AppendPageBreakItem(
    nsIContent* aContent, FrameConstructionItemList& aItems) {
  RefPtr<ComputedStyle> pseudoStyle =
      mPresShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
          PseudoStyleType::MozPageBreak);

  MOZ_ASSERT(pseudoStyle->StyleDisplay()->mDisplay == StyleDisplay::Block,
             "Unexpected display");

  static constexpr FrameConstructionData sPageBreakData(NS_NewPageBreakFrame,
                                                        FCDATA_SKIP_FRAMESET);
  aItems.AppendItem(this, &sPageBreakData, aContent, pseudoStyle.forget(),
                    true);
}

bool nsCSSFrameConstructor::ShouldCreateItemsForChild(
    nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame) {
  aContent->UnsetFlags(NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME);
  // XXX the GetContent() != aContent check is needed due to bug 135040.
  // Remove it once that's fixed.
  if (aContent->GetPrimaryFrame() &&
      aContent->GetPrimaryFrame()->GetContent() == aContent &&
      !aState.mCreatingExtraFrames) {
    MOZ_ASSERT(false,
               "asked to create frame construction item for a node that "
               "already has a frame");
    return false;
  }

  // don't create a whitespace frame if aParent doesn't want it
  if (!NeedFrameFor(aState, aParentFrame, aContent)) {
    return false;
  }

  // never create frames for comments or PIs
  if (aContent->IsComment() || aContent->IsProcessingInstruction()) {
    return false;
  }

  return true;
}

void nsCSSFrameConstructor::AddFrameConstructionItems(
    nsFrameConstructorState& aState, nsIContent* aContent,
    bool aSuppressWhiteSpaceOptimizations, const ComputedStyle& aParentStyle,
    const InsertionPoint& aInsertion, FrameConstructionItemList& aItems,
    ItemFlags aFlags) {
  nsContainerFrame* parentFrame = aInsertion.mParentFrame;
  if (!ShouldCreateItemsForChild(aState, aContent, parentFrame)) {
    return;
  }

  RefPtr<ComputedStyle> computedStyle = ResolveComputedStyle(aContent);
  auto flags = aFlags + ItemFlag::AllowPageBreak;
  if (parentFrame) {
    if (parentFrame->IsInSVGTextSubtree()) {
      flags += ItemFlag::IsWithinSVGText;
    }
    if (parentFrame->IsBlockFrame() && parentFrame->GetParent() &&
        parentFrame->GetParent()->IsSVGTextFrame()) {
      flags += ItemFlag::AllowTextPathChild;
    }
  }
  AddFrameConstructionItemsInternal(aState, aContent, parentFrame,
                                    aSuppressWhiteSpaceOptimizations,
                                    computedStyle, flags, aItems);
}

// Whether we should suppress frames for a child under a <select> element.
// Right now we need this for two things:
//  * To implement <option label>'s suppression of descendants (sad!)
//  * To implement hiding of the <select>'s text content (hopefully going away
//    soon enough, see https://github.com/whatwg/html/issues/12717).
static bool ShouldSuppressFrameForSelect(const nsIContent* aParent,
                                         const nsIContent& aChild) {
  if (!aParent) {
    return false;
  }

  if (aChild.IsRootOfNativeAnonymousSubtree()) {
    // Allow native anonymous content no matter what.
    return false;
  }

  if (aParent->IsHTMLElement(nsGkAtoms::option)) {
    // Options with labels have their label text added in ::before by forms.css.
    // Suppress frames for their children.
    // TODO(emilio): This should probably be done with shadow DOM instead (but a
    // ShadowRoot per option seems unfortunate...).
    return aParent->AsElement()->HasNonEmptyAttr(nsGkAtoms::label);
  }

  if (const auto* select = HTMLSelectElement::FromNode(aParent)) {
    // Direct text descendants of listbox <select> are not expected to render.
    return !select->IsCombobox() && aChild.IsText() &&
           aParent == aChild.GetParent();
  }

  return false;
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindDataForContent(nsIContent& aContent,
                                          ComputedStyle& aStyle,
                                          nsIFrame* aParentFrame,
                                          ItemFlags aFlags) {
  MOZ_ASSERT(aStyle.StyleDisplay()->mDisplay != StyleDisplay::None &&
                 aStyle.StyleDisplay()->mDisplay != StyleDisplay::Contents,
             "These two special display values should be handled earlier");

  if (auto* text = Text::FromNode(aContent)) {
    return FindTextData(*text, aParentFrame);
  }

  return FindElementData(*aContent.AsElement(), aStyle, aParentFrame, aFlags);
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindElementData(const Element& aElement,
                                       ComputedStyle& aStyle,
                                       nsIFrame* aParentFrame,
                                       ItemFlags aFlags) {
  // Don't create frames for non-SVG element children of SVG elements.
  if (!aElement.IsSVGElement()) {
    // NOTE: Anon content is allowed, the native code should know what it's
    // doing. In practice we care about ::backdrop and the
    // custom-content-container.
    if (aParentFrame && IsFrameForSVG(aParentFrame) &&
        !aParentFrame->IsSVGForeignObjectFrame() &&
        !aElement.IsRootOfNativeAnonymousSubtree()) {
      return nullptr;
    }
    if (aFlags.contains(ItemFlag::IsWithinSVGText)) {
      return nullptr;
    }
  }

  if (auto* data = FindElementTagData(aElement, aStyle, aParentFrame, aFlags)) {
    return data;
  }

  // Check for 'content: <image-url>' on the element (which makes us ignore
  // 'display' values other than 'none' or 'contents').
  if (nsImageFrame::ShouldCreateImageFrameForContentProperty(aElement,
                                                             aStyle)) {
    static constexpr FrameConstructionData sImgData(
        NS_NewImageFrameForContentProperty);
    return &sImgData;
  }

  const bool shouldBlockify = aFlags.contains(ItemFlag::IsForRenderedLegend) ||
                              aFlags.contains(ItemFlag::IsForOutsideMarker);
  if (shouldBlockify && !aStyle.StyleDisplay()->IsBlockOutsideStyle()) {
    // Make a temp copy of StyleDisplay and blockify its mDisplay value.
    auto display = *aStyle.StyleDisplay();
    bool isRootElement = false;
    uint16_t rawDisplayValue =
        Servo_ComputedValues_BlockifiedDisplay(&aStyle, isRootElement);
    display.mDisplay = StyleDisplay{rawDisplayValue};
    return FindDisplayData(display, aElement);
  }

  const auto& display = *aStyle.StyleDisplay();
  return FindDisplayData(display, aElement);
}

const nsCSSFrameConstructor::FrameConstructionData*
nsCSSFrameConstructor::FindElementTagData(const Element& aElement,
                                          ComputedStyle& aStyle,
                                          nsIFrame* aParentFrame,
                                          ItemFlags aFlags) {
  switch (aElement.GetNameSpaceID()) {
    case kNameSpaceID_XHTML:
      return FindHTMLData(aElement, aParentFrame, aStyle);
    case kNameSpaceID_MathML:
      return FindMathMLData(aElement, aStyle);
    case kNameSpaceID_SVG:
      return FindSVGData(aElement, aParentFrame,
                         aFlags.contains(ItemFlag::IsWithinSVGText),
                         aFlags.contains(ItemFlag::AllowTextPathChild), aStyle);
    case kNameSpaceID_XUL:
      return FindXULTagData(aElement, aStyle);
    default:
      return nullptr;
  }
}

void nsCSSFrameConstructor::AddFrameConstructionItemsInternal(
    nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame, bool aSuppressWhiteSpaceOptimizations,
    ComputedStyle* aComputedStyle, ItemFlags aFlags,
    FrameConstructionItemList& aItems) {
  MOZ_ASSERT(aContent->IsText() || aContent->IsElement(),
             "Shouldn't get anything else here!");
  MOZ_ASSERT(aContent->IsInComposedDoc());
  MOZ_ASSERT(!aContent->GetPrimaryFrame() || aState.mCreatingExtraFrames ||
             aContent->NodeInfo()->NameAtom() == nsGkAtoms::area);

  const bool withinSVGText = aFlags.contains(ItemFlag::IsWithinSVGText);
  const bool isGeneratedContent = aFlags.contains(ItemFlag::IsGeneratedContent);
  MOZ_ASSERT(!isGeneratedContent || aComputedStyle->IsPseudoElement(),
             "Generated content should be a pseudo-element");

  FrameConstructionItem* item = nullptr;
  auto cleanupGeneratedContent = mozilla::MakeScopeExit([&]() {
    if (isGeneratedContent && !item) {
      MOZ_ASSERT(!IsDisplayContents(aContent),
                 "This would need to change if we support display: contents "
                 "in generated content");
      aContent->UnbindFromTree();
    }
  });

  // 'display:none' elements never creates any frames at all.
  const nsStyleDisplay& display = *aComputedStyle->StyleDisplay();
  if (display.mDisplay == StyleDisplay::None) {
    return;
  }

  if (display.mDisplay == StyleDisplay::Contents) {
    // See the mDisplay fixup code in StyleAdjuster::adjust.
    MOZ_ASSERT(!aContent->AsElement()->IsRootOfNativeAnonymousSubtree(),
               "display:contents on anonymous content is unsupported");

    // FIXME(bug 1588477): <svg:text>'s TextNodeCorrespondenceRecorder has
    // trouble with everything that looks like display: contents.
    if (withinSVGText) {
      return;
    }

    CreateGeneratedContentItem(aState, aParentFrame, *aContent->AsElement(),
                               *aComputedStyle, PseudoStyleType::Before,
                               aItems);

    FlattenedChildIterator iter(aContent);
    InsertionPoint insertion(aParentFrame, aContent);
    for (nsIContent* child = iter.GetNextChild(); child;
         child = iter.GetNextChild()) {
      AddFrameConstructionItems(aState, child, aSuppressWhiteSpaceOptimizations,
                                *aComputedStyle, insertion, aItems, aFlags);
    }
    aItems.SetParentHasNoShadowDOM(!iter.ShadowDOMInvolved());

    CreateGeneratedContentItem(aState, aParentFrame, *aContent->AsElement(),
                               *aComputedStyle, PseudoStyleType::After, aItems);
    return;
  }

  nsIContent* parent = aParentFrame ? aParentFrame->GetContent() : nullptr;
  if (ShouldSuppressFrameForSelect(parent, *aContent)) {
    return;
  }

  if (aContent->IsHTMLElement(nsGkAtoms::legend) && aParentFrame) {
    const nsFieldSetFrame* const fs = GetFieldSetFrameFor(aParentFrame);
    if (fs && !fs->GetLegend() && !aState.mHasRenderedLegend &&
        !aComputedStyle->StyleDisplay()->IsFloatingStyle() &&
        !aComputedStyle->StyleDisplay()->IsAbsolutelyPositionedStyle()) {
      aState.mHasRenderedLegend = true;
      aFlags += ItemFlag::IsForRenderedLegend;
    }
  }

  const FrameConstructionData* const data =
      FindDataForContent(*aContent, *aComputedStyle, aParentFrame, aFlags);
  if (!data) {
    return;
  }
  const uint32_t bits = data->mBits;
  if (bits & FCDATA_SUPPRESS_FRAME) {
    return;
  }
  // Create our shadow tree lazily if needed.
  // NOTE(emilio): This is rather hacky, we should ideally remove this and make
  // shadow tree creation faster, see bug 2017005.
  if (auto* input = HTMLInputElement::FromNode(aContent)) {
    if (auto* sr = input->CreateShadowTreeFromLayoutIfNeeded()) {
      StyleNewChildRange(sr->GetFirstChild(), nullptr);
    }
  }

  const bool canHavePageBreak =
      aFlags.contains(ItemFlag::AllowPageBreak) &&
      aState.mPresContext->IsPaginated() &&
      !display.IsAbsolutelyPositionedStyle() &&
      !(aParentFrame && aParentFrame->IsFlexOrGridContainer()) &&
      !(bits & FCDATA_IS_TABLE_PART) && !(bits & FCDATA_IS_SVG_TEXT);
  if (canHavePageBreak && display.BreakBefore()) {
    AppendPageBreakItem(aContent, aItems);
  }

  if (!item) {
    item = aItems.AppendItem(this, data, aContent, do_AddRef(aComputedStyle),
                             aSuppressWhiteSpaceOptimizations);
    if (aFlags.contains(ItemFlag::IsForRenderedLegend)) {
      item->mIsRenderedLegend = true;
    }
  }
  item->mIsText = !aContent->IsElement();
  item->mIsGeneratedContent = isGeneratedContent;
  if (isGeneratedContent) {
    // We need to keep this alive until the frame takes ownership.
    // This corresponds to the Release in ConstructFramesFromItem.
    item->mContent->AddRef();
  }

  if (canHavePageBreak && display.BreakAfter()) {
    AppendPageBreakItem(aContent, aItems);
  }

  if (bits & FCDATA_IS_INLINE) {
    // To correctly set item->mIsAllInline we need to build up our child items
    // right now.
    BuildInlineChildItems(aState, *item,
                          aFlags.contains(ItemFlag::IsWithinSVGText),
                          aFlags.contains(ItemFlag::AllowTextPathChild));
    item->mIsBlock = false;
  } else {
    // Compute a boolean isInline which is guaranteed to be false for blocks
    // (but may also be false for some inlines).
    const bool isInline =
        // Table-internal things are inline-outside if and only if they're kids
        // of inlines, since they'll trigger construction of inline-table
        // pseudos.
        ((bits & FCDATA_IS_TABLE_PART) &&
         (!aParentFrame ||  // No aParentFrame means inline
          aParentFrame->StyleDisplay()->IsInlineFlow())) ||
        // Things that are inline-outside but aren't inline frames are inline
        display.IsInlineOutsideStyle();

    // Set mIsAllInline conservatively.  It just might be that even an inline
    // that has mIsAllInline false doesn't need an {ib} split.  So this is just
    // an optimization to keep from doing too much work in cases when we can
    // show that mIsAllInline is true..
    item->mIsAllInline =
        isInline ||
        // Figure out whether we're guaranteed this item will be out of flow.
        // This is not a precise test, since one of our ancestor inlines might
        // add an absolute containing block (if it's relatively positioned) when
        // there wasn't such a containing block before.  But it's conservative
        // in the sense that anything that will really end up as an in-flow
        // non-inline will test false here.  In other words, if this test is
        // true we're guaranteed to be inline; if it's false we don't know what
        // we'll end up as.
        //
        // If we make this test precise, we can remove some of the code dealing
        // with the imprecision in ConstructInline and adjust the comments on
        // mIsAllInline and mIsBlock in the header.
        (!(bits & FCDATA_DISALLOW_OUT_OF_FLOW) &&
         aState.GetGeometricParent(display, nullptr));

    // Set mIsBlock conservatively.  It's OK to set it false for some real
    // blocks, but not OK to set it true for things that aren't blocks.  Since
    // isOutOfFlow might be false even in cases when the frame will end up
    // out-of-flow, we can't use it here.  But we _can_ say that the frame will
    // for sure end up in-flow if it's not floated or absolutely positioned.
    item->mIsBlock = !isInline && !display.IsAbsolutelyPositionedStyle() &&
                     !display.IsFloatingStyle() && !(bits & FCDATA_IS_SVG_TEXT);
  }

  if (item->mIsAllInline) {
    aItems.InlineItemAdded();
  } else if (item->mIsBlock) {
    aItems.BlockItemAdded();
  }
}

/**
 * Return true if the frame construction item pointed to by aIter will
 * create a frame adjacent to a line boundary in the frame tree, and that
 * line boundary is induced by a content node adjacent to the frame's
 * content node in the content tree. The latter condition is necessary so
 * that ContentAppended/ContentInserted/ContentWillBeRemoved can easily find any
 * text nodes that were suppressed here.
 */
bool nsCSSFrameConstructor::AtLineBoundary(FCItemIterator& aIter) {
  if (aIter.item().mSuppressWhiteSpaceOptimizations) {
    return false;
  }

  if (aIter.AtStart()) {
    if (aIter.List()->HasLineBoundaryAtStart() &&
        !aIter.item().mContent->GetPreviousSibling()) {
      return true;
    }
  } else {
    FCItemIterator prev = aIter;
    prev.Prev();
    if (prev.item().IsLineBoundary() &&
        !prev.item().mSuppressWhiteSpaceOptimizations &&
        aIter.item().mContent->GetPreviousSibling() == prev.item().mContent) {
      return true;
    }
  }

  FCItemIterator next = aIter;
  next.Next();
  if (next.IsDone()) {
    if (aIter.List()->HasLineBoundaryAtEnd() &&
        !aIter.item().mContent->GetNextSibling()) {
      return true;
    }
  } else {
    if (next.item().IsLineBoundary() &&
        !next.item().mSuppressWhiteSpaceOptimizations &&
        aIter.item().mContent->GetNextSibling() == next.item().mContent) {
      return true;
    }
  }

  return false;
}

void nsCSSFrameConstructor::ConstructFramesFromItem(
    nsFrameConstructorState& aState, FCItemIterator& aIter,
    nsContainerFrame* aParentFrame, nsFrameList& aFrameList) {
  FrameConstructionItem& item = aIter.item();
  ComputedStyle* computedStyle = item.mComputedStyle;
  if (item.mIsText) {
    // If this is collapsible whitespace next to a line boundary,
    // don't create a frame. item.IsWhitespace() also sets the
    // NS_CREATE_FRAME_IF_NON_WHITESPACE flag in the text node. (If we
    // end up creating a frame, nsTextFrame::Init will clear the flag.)
    // We don't do this for generated content, because some generated
    // text content is empty text nodes that are about to be initialized.
    // (We check mAdditionalStateBits because only the generated content
    // container's frame construction item is marked with
    // mIsGeneratedContent, and we might not have an aParentFrame.)
    // We don't do it for content that may have Shadow DOM siblings / insertion
    // points, because they make it difficult to correctly create the frame due
    // to dynamic changes.
    // We don't do it for SVG text, since we might need to position and
    // measure the white space glyphs due to x/y/dx/dy attributes.
    if (AtLineBoundary(aIter) &&
        !computedStyle->StyleText()->WhiteSpaceOrNewlineIsSignificant() &&
        aIter.List()->ParentHasNoShadowDOM() &&
        !(aState.mAdditionalStateBits & NS_FRAME_GENERATED_CONTENT) &&
        (item.mFCData->mBits & FCDATA_IS_LINE_PARTICIPANT) &&
        !(item.mFCData->mBits & FCDATA_IS_SVG_TEXT) &&
        !mAlwaysCreateFramesForIgnorableWhitespace &&
        item.IsWhitespace(aState)) {
      return;
    }

    ConstructTextFrame(item.mFCData, aState, item.mContent, aParentFrame,
                       computedStyle, aFrameList);
    return;
  }

  AutoRestore<nsFrameState> savedStateBits(aState.mAdditionalStateBits);
  if (item.mIsGeneratedContent) {
    // Ensure that frames created here are all tagged with
    // NS_FRAME_GENERATED_CONTENT.
    aState.mAdditionalStateBits |= NS_FRAME_GENERATED_CONTENT;
  }

  // XXXbz maybe just inline ConstructFrameFromItemInternal here or something?
  ConstructFrameFromItemInternal(item, aState, aParentFrame, aFrameList);

  if (item.mIsGeneratedContent) {
    // This corresponds to the AddRef in AddFrameConstructionItemsInternal.
    // The frame owns the generated content now.
    item.mContent->Release();

    // Now that we've passed ownership of item.mContent to the frame, unset
    // our generated content flag so we don't release or unbind it ourselves.
    item.mIsGeneratedContent = false;
  }
}

nsContainerFrame* nsCSSFrameConstructor::GetAbsoluteContainingBlock(
    nsIFrame* aFrame, ContainingBlockType aType) {
  // Starting with aFrame, look for a frame that is absolutely positioned or
  // relatively positioned (and transformed, if aType is FIXED)
  for (nsIFrame* frame = aFrame; frame; frame = frame->GetParent()) {
    if (frame->IsMathMLFrame()) {
      // If it's mathml, bail out -- no absolute positioning out from inside
      // mathml frames.  Note that we don't make this part of the loop
      // condition because of the stuff at the end of this method...
      return nullptr;
    }

    // Look for the ICB.
    if (aType == FIXED_POS &&
        (frame->IsViewportFrame() || frame->IsPageContentFrame())) {
      return static_cast<nsContainerFrame*>(frame);
    }

    // If the frame is positioned, we will probably return it as the containing
    // block (see the exceptions below).  Otherwise, we'll start looking at the
    // parent frame, unless we're dealing with a scrollframe.
    // Scrollframes are special since they're not positioned, but their
    // scrolledframe might be.  So, we need to check this special case to return
    // the correct containing block (the scrolledframe) in that case.
    // If we're looking for a fixed-pos containing block and the frame is
    // not transformed, skip it.
    if (!frame->IsAbsPosContainingBlock()) {
      continue;
    }
    if (aType == FIXED_POS && !frame->IsFixedPosContainingBlock()) {
      continue;
    }
    nsIFrame* absPosCBCandidate = frame;
    if (absPosCBCandidate->IsFieldSetFrame()) {
      absPosCBCandidate =
          static_cast<nsFieldSetFrame*>(absPosCBCandidate)->GetInner();
      if (!absPosCBCandidate) {
        continue;
      }
    }
    if (absPosCBCandidate->IsScrollContainerFrame()) {
      ScrollContainerFrame* scrollContainerFrame =
          do_QueryFrame(absPosCBCandidate);
      absPosCBCandidate = scrollContainerFrame->GetScrolledFrame();
      if (!absPosCBCandidate) {
        continue;
      }
    }
    // Only first continuations or first IB-split siblings can be containing
    // blocks.
    absPosCBCandidate =
        nsLayoutUtils::FirstContinuationOrIBSplitSibling(absPosCBCandidate);
    // Is the frame really an absolute container?
    if (!absPosCBCandidate->IsAbsoluteContainer()) {
      continue;
    }

    // For tables, skip the inner frame and consider the table wrapper frame.
    if (absPosCBCandidate->IsTableFrame()) {
      continue;
    }
    // For table wrapper frames, we can just return absPosCBCandidate.
    MOZ_ASSERT((nsContainerFrame*)do_QueryFrame(absPosCBCandidate),
               "abs.pos. containing block must be nsContainerFrame sub-class");
    return static_cast<nsContainerFrame*>(absPosCBCandidate);
  }

  MOZ_ASSERT(aType != FIXED_POS, "no ICB in this frame tree?");

  // It is possible for the search for the containing block to fail, because
  // no absolute container can be found in the parent chain.  In those cases,
  // we fall back to the document element's containing block.
  return mDocElementContainingBlock;
}

nsContainerFrame* nsCSSFrameConstructor::GetFloatContainingBlock(
    nsIFrame* aFrame) {
  // Starting with aFrame, look for a frame that is a float containing block.
  // If we hit a frame which prevents its descendants from floating, bail out.
  // The logic here needs to match the logic in MaybePushFloatContainingBlock().
  for (nsIFrame* containingBlock = aFrame;
       containingBlock && !ShouldSuppressFloatingOfDescendants(containingBlock);
       containingBlock = containingBlock->GetParent()) {
    if (containingBlock->IsFloatContainingBlock()) {
      MOZ_ASSERT((nsContainerFrame*)do_QueryFrame(containingBlock),
                 "float containing block must be nsContainerFrame sub-class");
      return static_cast<nsContainerFrame*>(containingBlock);
    }
  }

  // If we didn't find a containing block, then there just isn't
  // one.... return null
  return nullptr;
}

/**
 * Finds the right parent frame to append content to aParentFrame.
 *
 * Cannot return or receive null.
 */
static nsContainerFrame* ContinuationToAppendTo(
    nsContainerFrame* aParentFrame) {
  MOZ_ASSERT(aParentFrame);

  if (IsFramePartOfIBSplit(aParentFrame)) {
    // If the frame we are manipulating is a ib-split frame (that is, one that's
    // been created as a result of a block-in-inline situation) then we need to
    // append to the last ib-split sibling, not to the frame itself.
    //
    // Always make sure to look at the last continuation of the frame for the
    // {ib} case, even if that continuation is empty.
    //
    // We don't do this for the non-ib-split-frame case, since in the other
    // cases appending to the last nonempty continuation is fine and in fact not
    // doing that can confuse code that doesn't know to pull kids from
    // continuations other than its next one.
    return static_cast<nsContainerFrame*>(
        GetLastIBSplitSibling(aParentFrame)->LastContinuation());
  }

  return nsLayoutUtils::LastContinuationWithChild(aParentFrame);
}

/**
 * This function will get the next sibling for a frame insert operation given
 * the parent and previous sibling.  aPrevSibling may be null.
 */
static nsIFrame* GetInsertNextSibling(nsIFrame* aParentFrame,
                                      nsIFrame* aPrevSibling) {
  if (aPrevSibling) {
    return aPrevSibling->GetNextSibling();
  }

  return aParentFrame->PrincipalChildList().FirstChild();
}

void nsCSSFrameConstructor::AppendFramesToParent(
    nsFrameConstructorState& aState, nsContainerFrame* aParentFrame,
    nsFrameList& aFrameList, nsIFrame* aPrevSibling, bool aIsRecursiveCall) {
  MOZ_ASSERT(
      !IsFramePartOfIBSplit(aParentFrame) || !GetIBSplitSibling(aParentFrame) ||
          !GetIBSplitSibling(aParentFrame)->PrincipalChildList().FirstChild(),
      "aParentFrame has a ib-split sibling with kids?");
  MOZ_ASSERT(!aPrevSibling || aPrevSibling->GetParent() == aParentFrame,
             "Parent and prevsibling don't match");
  MOZ_ASSERT(
      !aParentFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) ||
          !IsFramePartOfIBSplit(aParentFrame),
      "We should have wiped aParentFrame in WipeContainingBlock() "
      "if it's part of an IB split!");

  nsIFrame* nextSibling = ::GetInsertNextSibling(aParentFrame, aPrevSibling);

  NS_ASSERTION(nextSibling || !aParentFrame->GetNextContinuation() ||
                   !aParentFrame->GetNextContinuation()
                        ->PrincipalChildList()
                        .FirstChild() ||
                   aIsRecursiveCall,
               "aParentFrame has later continuations with kids?");
  NS_ASSERTION(
      nextSibling || !IsFramePartOfIBSplit(aParentFrame) ||
          (IsInlineFrame(aParentFrame) && !GetIBSplitSibling(aParentFrame) &&
           !aParentFrame->GetNextContinuation()) ||
          aIsRecursiveCall,
      "aParentFrame is not last?");

  // If we're inserting a list of frames at the end of the trailing inline
  // of an {ib} split, we may need to create additional {ib} siblings to parent
  // them.
  if (!nextSibling && IsFramePartOfIBSplit(aParentFrame)) {
    // When we get here, our frame list might start with a block.  If it does
    // so, and aParentFrame is an inline, and it and all its previous
    // continuations have no siblings, then put the initial blocks from the
    // frame list into the previous block of the {ib} split.  Note that we
    // didn't want to stop at the block part of the split when figuring out
    // initial parent, because that could screw up float parenting; it's easier
    // to do this little fixup here instead.
    if (aFrameList.NotEmpty() && aFrameList.FirstChild()->IsBlockOutside()) {
      // See whether our trailing inline is empty
      nsIFrame* firstContinuation = aParentFrame->FirstContinuation();
      if (firstContinuation->PrincipalChildList().IsEmpty()) {
        // Our trailing inline is empty.  Collect our starting blocks from
        // aFrameList, get the right parent frame for them, and put them in.
        nsFrameList blockKids =
            aFrameList.Split([](nsIFrame* f) { return !f->IsBlockOutside(); });
        NS_ASSERTION(blockKids.NotEmpty(), "No blocks?");

        nsContainerFrame* prevBlock = GetIBSplitPrevSibling(firstContinuation);
        prevBlock =
            static_cast<nsContainerFrame*>(prevBlock->LastContinuation());
        NS_ASSERTION(prevBlock, "Should have previous block here");

        MoveChildrenTo(aParentFrame, prevBlock, blockKids);
      }
    }

    // We want to put some of the frames into this inline frame.
    nsFrameList inlineKids =
        aFrameList.Split([](nsIFrame* f) { return f->IsBlockOutside(); });

    if (!inlineKids.IsEmpty()) {
      AppendFrames(aParentFrame, FrameChildListID::Principal,
                   std::move(inlineKids));
    }

    if (!aFrameList.IsEmpty()) {
      nsFrameList ibSiblings;
      CreateIBSiblings(aState, aParentFrame,
                       aParentFrame->IsAbsPosContainingBlock(), aFrameList,
                       ibSiblings);

      // Make sure to trigger reflow of the inline that used to be our
      // last one and now isn't anymore, since its GetSkipSides() has
      // changed.
      mPresShell->FrameNeedsReflow(aParentFrame,
                                   IntrinsicDirty::FrameAndAncestors,
                                   NS_FRAME_HAS_DIRTY_CHILDREN);

      // Recurse so we create new ib siblings as needed for aParentFrame's
      // parent
      return AppendFramesToParent(aState, aParentFrame->GetParent(), ibSiblings,
                                  aParentFrame, true);
    }
    return;
  }

  // If we're appending a list of frames to the last continuations of a
  // ::-moz-column-content, we may need to create column-span siblings for them.
  if (!nextSibling && IsLastContinuationForColumnContent(aParentFrame)) {
    // Extract any initial non-column-span kids, and append them to
    // ::-moz-column-content's child list.
    nsFrameList initialNonColumnSpanKids =
        aFrameList.Split([](nsIFrame* f) { return f->IsColumnSpan(); });
    AppendFrames(aParentFrame, FrameChildListID::Principal,
                 std::move(initialNonColumnSpanKids));

    if (aFrameList.IsEmpty()) {
      // No more kids to process (there weren't any column-span kids).
      return;
    }

    nsFrameList columnSpanSiblings = CreateColumnSpanSiblings(
        aState, aParentFrame, aFrameList,
        // Column content should never be a absolute/fixed positioned containing
        // block. Pass nullptr as aPositionedFrame.
        nullptr);

    nsContainerFrame* columnSetWrapper = aParentFrame->GetParent();
    while (!columnSetWrapper->IsColumnSetWrapperFrame()) {
      columnSetWrapper = columnSetWrapper->GetParent();
    }
    MOZ_ASSERT(columnSetWrapper,
               "No ColumnSetWrapperFrame ancestor for -moz-column-content?");

    FinishBuildingColumns(aState, columnSetWrapper, aParentFrame,
                          columnSpanSiblings);

    MOZ_ASSERT(columnSpanSiblings.IsEmpty(),
               "The column-span siblings should be moved to the proper place!");
    return;
  }

  // Insert the frames after our aPrevSibling
  InsertFrames(aParentFrame, FrameChildListID::Principal, aPrevSibling,
               std::move(aFrameList));
}

template <nsCSSFrameConstructor::SiblingDirection aDirection>
nsIFrame* nsCSSFrameConstructor::FindSiblingInternal(
    FlattenedChildIterator& aIter) {
  auto nextDomSibling = [](FlattenedChildIterator& aIter) -> nsIContent* {
    return aDirection == SiblingDirection::Forward ? aIter.GetNextChild()
                                                   : aIter.GetPreviousChild();
  };

  auto getInsideMarkerFrame = [](const nsIContent* aContent) -> nsIFrame* {
    auto* marker = nsLayoutUtils::GetMarkerFrame(aContent);
    const bool isInsideMarker =
        marker && marker->GetInFlowParent()->StyleList()->mListStylePosition ==
                      StyleListStylePosition::Inside;
    return isInsideMarker ? marker : nullptr;
  };

  auto getNearPseudo = [&](const nsIContent* aContent) -> nsIFrame* {
    if (aDirection == SiblingDirection::Forward) {
      if (auto* backdrop = nsLayoutUtils::GetBackdropFrame(aContent)) {
        return backdrop;
      }
      if (auto* marker = getInsideMarkerFrame(aContent)) {
        return marker;
      }
      return nsLayoutUtils::GetBeforeFrame(aContent);
    }
    return nsLayoutUtils::GetAfterFrame(aContent);
  };

  auto getFarPseudo = [&](const nsIContent* aContent) -> nsIFrame* {
    if (aDirection == SiblingDirection::Forward) {
      if (auto* pickerIcon = nsLayoutUtils::GetPickerIconFrame(aContent)) {
        return pickerIcon;
      }
      return nsLayoutUtils::GetAfterFrame(aContent);
    }
    if (auto* before = nsLayoutUtils::GetBeforeFrame(aContent)) {
      return before;
    }
    if (auto* marker = getInsideMarkerFrame(aContent)) {
      return marker;
    }
    return nsLayoutUtils::GetBackdropFrame(aContent);
  };

  while (nsIContent* sibling = nextDomSibling(aIter)) {
    // NOTE(emilio): It's important to check GetPrimaryFrame() before
    // IsDisplayContents to get the correct insertion point when multiple
    // siblings go from display: non-none to display: contents.
    if (nsIFrame* primaryFrame = sibling->GetPrimaryFrame()) {
      // XXX the GetContent() == sibling check is needed due to bug 135040.
      // Remove it once that's fixed.
      // The rendered legend check is needed because it is conceptually out of
      // flow. There can only be one rendered legend at a time, and we reframe
      // when that changes, so this is ok. Table captions are a similar case,
      // but there can be multiple of them and they instead get pulled out (and
      // the sibling fixed up) after building the frame list.
      if (primaryFrame->GetContent() == sibling &&
          !primaryFrame->IsRenderedLegend()) [[likely]] {
        return primaryFrame;
      }
    }

    if (IsDisplayContents(sibling)) {
      if (nsIFrame* frame = getNearPseudo(sibling)) {
        return frame;
      }

      const bool startFromBeginning = aDirection == SiblingDirection::Forward;
      FlattenedChildIterator iter(sibling, startFromBeginning);
      nsIFrame* sibling = FindSiblingInternal<aDirection>(iter);
      if (sibling) {
        return sibling;
      }
    }
  }

  MOZ_ASSERT(aIter.ParentNode()->IsContent());
  return getFarPseudo(aIter.ParentNode()->AsContent());
}

nsIFrame* nsCSSFrameConstructor::AdjustSiblingFrame(
    nsIFrame* aSibling, SiblingDirection aDirection) {
  if (!aSibling) {
    return nullptr;
  }

  if (aSibling->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    aSibling = aSibling->GetPlaceholderFrame();
    MOZ_ASSERT(aSibling);
  }

  MOZ_ASSERT(!aSibling->GetPrevContinuation(), "How?");
  if (aDirection == SiblingDirection::Backward) {
    // The frame may be a ib-split frame (a split inline frame that contains a
    // block).  Get the last part of that split.
    if (IsFramePartOfIBSplit(aSibling)) {
      aSibling = GetLastIBSplitSibling(aSibling);
    }

    // The frame may have a continuation. If so, we want the last
    // non-overflow-container continuation as our previous sibling.
    aSibling = aSibling->GetTailContinuation();
  }

  return aSibling;
}

nsIFrame* nsCSSFrameConstructor::FindPreviousSibling(
    const FlattenedChildIterator& aIter) {
  return AdjustSiblingFrame(FindSibling<SiblingDirection::Backward>(aIter),
                            SiblingDirection::Backward);
}

nsIFrame* nsCSSFrameConstructor::FindNextSibling(
    const FlattenedChildIterator& aIter) {
  return AdjustSiblingFrame(FindSibling<SiblingDirection::Forward>(aIter),
                            SiblingDirection::Forward);
}

template <nsCSSFrameConstructor::SiblingDirection aDirection>
nsIFrame* nsCSSFrameConstructor::FindSibling(
    const FlattenedChildIterator& aIter) {
  FlattenedChildIterator siblingIter = aIter;
  nsIFrame* sibling = FindSiblingInternal<aDirection>(siblingIter);
  if (sibling) {
    return sibling;
  }

  // Our siblings (if any) do not have a frame to guide us. The frame for the
  // target content should be inserted whereever a frame for the container would
  // be inserted. This is needed when inserting into display: contents nodes.
  const nsIContent* current = aIter.ParentNode()->AsContent();
  while (IsDisplayContents(current)) {
    const nsIContent* parent = current->GetFlattenedTreeParent();
    MOZ_ASSERT(parent, "No display: contents on the root");

    FlattenedChildIterator iter(parent);
    iter.Seek(current);
    sibling = FindSiblingInternal<aDirection>(iter);
    if (sibling) {
      return sibling;
    }

    current = parent;
  }

  return nullptr;
}

nsIFrame* nsCSSFrameConstructor::GetInsertionPrevSibling(
    InsertionPoint* aInsertion, nsIContent* aChild, bool* aIsAppend) {
  MOZ_ASSERT(aInsertion->mParentFrame, "Must have parent frame to start with");

  *aIsAppend = false;

  // Find the frame that precedes the insertion point.
  FlattenedChildIterator iter(aInsertion->mContainer);
  if (!aChild->IsRootOfNativeAnonymousSubtree()) {
    // The check for IsRootOfNativeAnonymousSubtree() is because editor is
    // severely broken and calls us directly for native anonymous
    // nodes that it creates.
    iter.Seek(aChild);
  } else {
    // Prime the iterator for the call to FindPreviousSibling.
    (void)iter.GetNextChild();
    MOZ_ASSERT(aChild->GetProperty(nsGkAtoms::restylableAnonymousNode),
               "Someone passed native anonymous content directly into frame "
               "construction.  Stop doing that!");
  }

  // Note that FindPreviousSibling is passed the iterator by value, so that
  // the later usage of the iterator starts from the same place.
  nsIFrame* prevSibling = FindPreviousSibling(iter);

  // Now, find the geometric parent so that we can handle continuations
  // properly. Use the prev sibling if we have it; otherwise use the next
  // sibling.
  if (prevSibling) {
    aInsertion->mParentFrame =
        prevSibling->GetParent()->GetContentInsertionFrame();

    if (IsAnonymousItem(aInsertion->mParentFrame)) {
      // Special-case anonymous flex / grid items: Elements are blockified
      // inside those containers, so the anonymous item is unlikely to be the
      // right parent unless we're inserting a non-whitespace text-node.
      //
      // If we guess wrong, we catch this in WipeContainingBlock, but with a
      // performance penalty (and sometimes correctness too, see bug 2014986).
      //
      // TODO(emilio): Are there other situations where this is worth doing
      // (some table anon boxes or something?). Seems hard to do at this stage
      // where we still don't _quite_ know the details of what aChild is (in or
      // out of flow, display inside or outside...). Maybe WipeContainingBlock
      // should be able to fix-up the insertion point at the last minute? But
      // that seems more sketchy since some code does rely on looking at the
      // insertion point to decide what to construct.
      AssertAnonymousFlexOrGridItemParent(aInsertion->mParentFrame);
      if (!prevSibling->GetNextSibling() &&
          (!aChild->IsText() || aChild->TextIsOnlyWhitespace())) {
        prevSibling = aInsertion->mParentFrame->GetTailContinuation();
        aInsertion->mParentFrame = prevSibling->GetParent();
      }
    }

    *aIsAppend =
        !::GetInsertNextSibling(aInsertion->mParentFrame, prevSibling) &&
        !nsLayoutUtils::GetNextContinuationOrIBSplitSibling(
            aInsertion->mParentFrame) &&
        !IsWrapperPseudo(aInsertion->mParentFrame);
  } else if (nsIFrame* nextSibling = FindNextSibling(iter)) {
    // If there is no previous sibling, then find the frame that follows
    aInsertion->mParentFrame =
        nextSibling->GetParent()->GetContentInsertionFrame();
    if (IsAnonymousItem(aInsertion->mParentFrame)) {
      // See the prevSibling special-case above.
      AssertAnonymousFlexOrGridItemParent(aInsertion->mParentFrame);
      if (!nextSibling->GetPrevSibling() &&
          (!aChild->IsText() || aChild->TextIsOnlyWhitespace())) {
        aInsertion->mParentFrame =
            aInsertion->mParentFrame->FirstContinuation()->GetParent();
      }
    }
  } else {
    // No previous or next sibling, so treat this like an appended frame.
    *aIsAppend = true;

    aInsertion->mParentFrame =
        ::ContinuationToAppendTo(aInsertion->mParentFrame);

    prevSibling = aInsertion->mParentFrame->PrincipalChildList().LastChild();
  }

  return prevSibling;
}

nsContainerFrame* nsCSSFrameConstructor::GetContentInsertionFrameFor(
    nsIContent* aContent) {
  nsIFrame* frame;
  while (!(frame = aContent->GetPrimaryFrame())) {
    if (!IsDisplayContents(aContent)) {
      return nullptr;
    }

    aContent = aContent->GetFlattenedTreeParent();
    if (!aContent) {
      return nullptr;
    }
  }

  // If the content of the frame is not the desired content then this is not
  // really a frame for the desired content.
  // XXX This check is needed due to bug 135040. Remove it once that's fixed.
  if (frame->GetContent() != aContent) {
    return nullptr;
  }

  nsContainerFrame* insertionFrame = frame->GetContentInsertionFrame();

  NS_ASSERTION(!insertionFrame || insertionFrame == frame || !frame->IsLeaf(),
               "The insertion frame is the primary frame or the primary frame "
               "isn't a leaf");

  return insertionFrame;
}

static bool IsSpecialFramesetChild(nsIContent* aContent) {
  // IMPORTANT: This must match the conditions in nsHTMLFramesetFrame::Init.
  return aContent->IsAnyOfHTMLElements(nsGkAtoms::frameset, nsGkAtoms::frame);
}

static void InvalidateCanvasIfNeeded(PresShell* aPresShell, nsIContent* aNode);

void nsCSSFrameConstructor::AddTextItemIfNeeded(
    nsFrameConstructorState& aState, const ComputedStyle& aParentStyle,
    const InsertionPoint& aInsertion, nsIContent* aPossibleTextContent,
    FrameConstructionItemList& aItems) {
  MOZ_ASSERT(aPossibleTextContent, "Must have node");
  if (!aPossibleTextContent->IsText() ||
      !aPossibleTextContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE) ||
      aPossibleTextContent->HasFlag(NODE_NEEDS_FRAME)) {
    // Not text, or not suppressed due to being all-whitespace (if it were being
    // suppressed, it would have the NS_CREATE_FRAME_IF_NON_WHITESPACE flag), or
    // going to be reframed anyway.
    return;
  }
  MOZ_ASSERT(!aPossibleTextContent->GetPrimaryFrame(),
             "Text node has a frame and NS_CREATE_FRAME_IF_NON_WHITESPACE");
  AddFrameConstructionItems(aState, aPossibleTextContent, false, aParentStyle,
                            aInsertion, aItems);
}

void nsCSSFrameConstructor::ReframeTextIfNeeded(nsIContent* aContent) {
  if (!aContent->IsText() ||
      !aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE) ||
      aContent->HasFlag(NODE_NEEDS_FRAME)) {
    // Not text, or not suppressed due to being all-whitespace (if it were being
    // suppressed, it would have the NS_CREATE_FRAME_IF_NON_WHITESPACE flag), or
    // going to be reframed anyway.
    return;
  }
  MOZ_ASSERT(!aContent->GetPrimaryFrame(),
             "Text node has a frame and NS_CREATE_FRAME_IF_NON_WHITESPACE");
  ContentInserted(aContent, InsertionKind::Async);
}

#ifdef DEBUG
void nsCSSFrameConstructor::CheckBitsForLazyFrameConstruction(
    nsIContent* aParent) {
  // If we hit a node with no primary frame, or the NODE_NEEDS_FRAME bit set
  // we want to assert, but leaf frames that process their own children and may
  // ignore anonymous children (eg framesets) make this complicated. So we set
  // these two booleans if we encounter these situations and unset them if we
  // hit a node with a leaf frame.
  //
  // It's fine if one of node without primary frame is in a display:none
  // subtree.
  //
  // Also, it's fine if one of the nodes without primary frame is a display:
  // contents node.
  bool noPrimaryFrame = false;
  bool needsFrameBitSet = false;
  nsIContent* content = aParent;
  while (content && !content->HasFlag(NODE_DESCENDANTS_NEED_FRAMES)) {
    if (content->GetPrimaryFrame() && content->GetPrimaryFrame()->IsLeaf()) {
      noPrimaryFrame = needsFrameBitSet = false;
    }
    if (!noPrimaryFrame && !content->GetPrimaryFrame()) {
      noPrimaryFrame = !IsDisplayContents(content);
    }
    if (!needsFrameBitSet && content->HasFlag(NODE_NEEDS_FRAME)) {
      needsFrameBitSet = true;
    }

    content = content->GetFlattenedTreeParent();
  }
  if (content && content->GetPrimaryFrame() &&
      content->GetPrimaryFrame()->IsLeaf()) {
    noPrimaryFrame = needsFrameBitSet = false;
  }
  MOZ_ASSERT(!noPrimaryFrame,
             "Ancestors of nodes with frames to be "
             "constructed lazily should have frames");
  MOZ_ASSERT(!needsFrameBitSet,
             "Ancestors of nodes with frames to be "
             "constructed lazily should not have NEEDS_FRAME bit set");
}
#endif

// Returns true if this operation can be lazy, false if not.
//
// NOTE(emilio): This function assumes that the flattened tree
// parent of all the appended children is the same, which holds because this
// gets called after GetRangeInsertionPoint which notifies individually.
void nsCSSFrameConstructor::ConstructLazily(nsIContent* aStartChild,
                                            nsIContent* aEndChild) {
  MOZ_ASSERT(aStartChild->GetParent());

  // We can construct lazily; just need to set suitable bits in the content
  // tree.
  Element* parent = aStartChild->GetFlattenedTreeParentElement();
  if (!parent) {
    // Not part of the flat tree, nothing to do.
    return;
  }

  if (Servo_Element_IsDisplayNone(parent)) {
    // Nothing to do either.
    //
    // FIXME(emilio): This should be an assert, except for weird <frameset>
    // stuff that does its own frame construction. Such an assert would fire in
    // layout/style/crashtests/1411478.html, for example.
    return;
  }

  // Set NODE_NEEDS_FRAME on the new nodes.
  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    NS_ASSERTION(!child->GetPrimaryFrame() ||
                     child->GetPrimaryFrame()->GetContent() != child,
                 // XXX the child->GetPrimaryFrame()->GetContent() != child
                 // check is needed due to bug 135040. Remove it once that's
                 // fixed.
                 "setting NEEDS_FRAME on a node that already has a frame?");
    child->SetFlags(NODE_NEEDS_FRAME);
  }

  CheckBitsForLazyFrameConstruction(parent);
  parent->NoteDescendantsNeedFramesForServo();
}

void nsCSSFrameConstructor::IssueSingleInsertNofications(
    nsIContent* aStartChild, nsIContent* aEndChild,
    InsertionKind aInsertionKind) {
  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    // XXX the GetContent() != child check is needed due to bug 135040.
    // Remove it once that's fixed.
    MOZ_ASSERT(!child->GetPrimaryFrame() ||
               child->GetPrimaryFrame()->GetContent() != child);

    // Call ContentRangeInserted with this node.
    ContentRangeInserted(child, child->GetNextSibling(), aInsertionKind);
  }
}

nsCSSFrameConstructor::InsertionPoint
nsCSSFrameConstructor::GetRangeInsertionPoint(nsIContent* aStartChild,
                                              nsIContent* aEndChild,
                                              InsertionKind aInsertionKind) {
  MOZ_ASSERT(aStartChild);
  MOZ_ASSERT(aStartChild->GetParentNode());

  // If the children of the container may be distributed to different insertion
  // points, insert them separately and bail out, letting ContentInserted handle
  // the mess.
  if (aStartChild->GetParentNode()->GetShadowRoot()) {
    IssueSingleInsertNofications(aStartChild, aEndChild, aInsertionKind);
    return {};
  }

#ifdef DEBUG
  {
    nsIContent* expectedParent = aStartChild->GetFlattenedTreeParent();
    for (nsIContent* child = aStartChild->GetNextSibling(); child;
         child = child->GetNextSibling()) {
      MOZ_ASSERT(child->GetFlattenedTreeParent() == expectedParent);
    }
  }
#endif

  // Now the flattened tree parent of all the siblings is the same, just use the
  // same insertion point.
  return GetInsertionPoint(aStartChild);
}

bool nsCSSFrameConstructor::MaybeRecreateForFrameset(nsIFrame* aParentFrame,
                                                     nsIContent* aStartChild,
                                                     nsIContent* aEndChild) {
  if (aParentFrame->IsFrameSetFrame()) {
    // Check whether we have any kids we care about.
    for (nsIContent* cur = aStartChild; cur != aEndChild;
         cur = cur->GetNextSibling()) {
      if (IsSpecialFramesetChild(cur)) {
        // Just reframe the parent, since framesets are weird like that.
        RecreateFramesForContent(aParentFrame->GetContent(),
                                 InsertionKind::Async);
        return true;
      }
    }
  }
  return false;
}

void nsCSSFrameConstructor::LazilyStyleNewChildRange(nsIContent* aStartChild,
                                                     nsIContent* aEndChild) {
  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    if (child->IsElement()) {
      child->AsElement()->NoteDirtyForServo();
    }
  }
}

#ifdef DEBUG
static bool IsFlattenedTreeChild(nsIContent* aParent, nsIContent* aChild) {
  FlattenedChildIterator iter(aParent);
  for (nsIContent* node = iter.GetNextChild(); node;
       node = iter.GetNextChild()) {
    if (node == aChild) {
      return true;
    }
  }
  return false;
}
#endif

void nsCSSFrameConstructor::StyleNewChildRange(nsIContent* aStartChild,
                                               nsIContent* aEndChild) {
  ServoStyleSet* styleSet = mPresShell->StyleSet();

  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    if (!child->IsElement()) {
      continue;
    }

    Element* childElement = child->AsElement();

    // We only come in here from non-lazy frame construction, so the children
    // should be unstyled.
    MOZ_ASSERT(!childElement->HasServoData());

#ifdef DEBUG
    {
      // Furthermore, all of them should have the same flattened tree parent
      // (GetRangeInsertionPoint ensures it). And that parent should be styled,
      // otherwise we would've never found an insertion point at all.
      Element* parent = childElement->GetFlattenedTreeParentElement();
      MOZ_ASSERT(parent);
      MOZ_ASSERT(parent->HasServoData());
      MOZ_ASSERT(
          IsFlattenedTreeChild(parent, child),
          "GetFlattenedTreeParent and ChildIterator don't agree, fix this!");
    }
#endif

    styleSet->StyleNewSubtree(childElement);
  }
}

// This is a bit slow, but sometimes we need it.
static bool ParentIsWrapperAnonBox(nsIFrame* aParent) {
  nsIFrame* maybeAnonBox = aParent;
  if (maybeAnonBox->Style()->GetPseudoType() ==
      PseudoStyleType::MozCellContent) {
    // The thing that would maybe be a wrapper anon box is the cell.
    maybeAnonBox = maybeAnonBox->GetParent();
  }
  return maybeAnonBox->Style()->IsWrapperAnonBox();
}

void nsCSSFrameConstructor::ContentAppended(nsIContent* aFirstNewContent,
                                            InsertionKind aInsertionKind) {
  return ContentRangeInserted(aFirstNewContent, nullptr, aInsertionKind);
}

void nsCSSFrameConstructor::ContentInserted(nsIContent* aChild,
                                            InsertionKind aInsertionKind) {
  ContentRangeInserted(aChild, aChild->GetNextSibling(), aInsertionKind);
}

// It's very likely that we don't have any existing captions (because we only
// render one of them, see bug 144517). We also don't fragment table captions.
// So, prefer just walking the existing captions and searching it, rather than
// adding a special GetInsertionPrevSibling() version that skips everything but
// table captions.
static nsIFrame* FindCaptionPrevSibling(nsTableWrapperFrame* aTable,
                                        nsIContent* aCaptionContent) {
  nsIFrame* prevSibling = aTable->InnerTableFrame();
  if (nsIFrame* firstCaption = prevSibling->GetNextSibling()) {
    nsContentUtils::NodeIndexCache cache;
    for (auto* caption = firstCaption; caption;
         caption = caption->GetNextSibling()) {
      if (nsContentUtils::CompareTreePosition<TreeKind::Flat>(
              caption->GetContent(), aCaptionContent, nullptr, &cache) >= 0) {
        break;
      }
      prevSibling = caption;
    }
  }
  return prevSibling;
}

// ContentRangeInserted handles creating frames for a range of nodes that
// might not be at the end of their childlist. ContentRangeInserted isn't a real
// content notification, but rather it handles regular ContentInserted calls
// for a single node as well as the lazy construction of frames for a range of
// nodes when called from CreateNeededFrames. For a range of nodes to be
// suitable to have its frames constructed all at once they must meet the same
// conditions that ContentAppended imposes (GetRangeInsertionPoint checks
// these), plus more.
void nsCSSFrameConstructor::ContentRangeInserted(nsIContent* aStartChild,
                                                 nsIContent* aEndChild,
                                                 InsertionKind aInsertionKind) {
  AUTO_PROFILER_LABEL_HOT("nsCSSFrameConstructor::ContentRangeInserted",
                          LAYOUT_FrameConstruction);
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);

  MOZ_ASSERT(aStartChild, "must always pass a child");

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf(
        "nsCSSFrameConstructor::ContentRangeInserted container=%p "
        "start-child=%p end-child=%p lazy=%d\n",
        aStartChild->GetParent(), aStartChild, aEndChild,
        aInsertionKind == InsertionKind::Async);
    if (gReallyNoisyContentUpdates) {
      if (aStartChild->GetParent()) {
        aStartChild->GetParent()->List(stdout, 0);
      } else {
        aStartChild->List(stdout, 0);
      }
    }
  }

  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    // XXX the GetContent() != child check is needed due to bug 135040.
    // Remove it once that's fixed.
    MOZ_ASSERT(
        !child->GetPrimaryFrame() ||
            child->GetPrimaryFrame()->GetContent() != child,
        "asked to construct a frame for a node that already has a frame");
  }
#endif

  // If we have a null parent, then this must be the document element being
  // inserted, or some other child of the document in the DOM (might be a
  // processing instruction or comment).
  if (!aStartChild->GetParent()) {
    Element* docElement = mDocument->GetRootElement();
    const bool foundRoot = [&] {
      for (nsIContent* cur = aStartChild; cur != aEndChild;
           cur = cur->GetNextSibling()) {
        if (cur == docElement) {
          return true;
        }
      }
      return false;
    }();

    if (!foundRoot) {
      // Not the root element (could be e.g. a comment), just bail out
      return;
    }

    MOZ_ASSERT(!mRootElementFrame, "root element frame already created");
    if (aInsertionKind == InsertionKind::Async) {
      docElement->SetFlags(NODE_NEEDS_FRAME);
      LazilyStyleNewChildRange(docElement, nullptr);
      return;
    }

    // Create frames for the document element and its child elements
    if (ConstructDocElementFrame(docElement)) {
      InvalidateCanvasIfNeeded(mPresShell, docElement);
#ifdef DEBUG
      if (gReallyNoisyContentUpdates) {
        printf(
            "nsCSSFrameConstructor::ContentRangeInserted: resulting frame "
            "model:\n");
        mRootElementFrame->List(stdout);
      }
#endif
    }

#ifdef ACCESSIBILITY
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->ContentRangeInserted(mPresShell, aStartChild, aEndChild);
    }
#endif
    return;
  }

  const bool isSingleInsert = aStartChild->GetNextSibling() == aEndChild;
  InsertionPoint insertion;
  if (isSingleInsert) {
    // See if we have a Shadow DOM insertion point. If so, then that's our real
    // parent frame; if not, then the frame hasn't been built yet and we just
    // bail.
    insertion = GetInsertionPoint(aStartChild);
  } else {
    // Get our insertion point. If we need to issue single ContentInserteds
    // GetRangeInsertionPoint will take care of that for us.
    LAYOUT_PHASE_TEMP_EXIT();
    insertion = GetRangeInsertionPoint(aStartChild, aEndChild, aInsertionKind);
    LAYOUT_PHASE_TEMP_REENTER();
  }

  if (!insertion.mParentFrame) {
    // We're punting on frame construction because there's no container frame.
    // The Servo-backed style system handles this case like the lazy frame
    // construction case, except when we're already constructing frames, in
    // which case we shouldn't need to do anything else.
    if (aInsertionKind == InsertionKind::Async) {
      LazilyStyleNewChildRange(aStartChild, aEndChild);
    }
    return;
  }

  if (aInsertionKind == InsertionKind::Async) {
    ConstructLazily(aStartChild, aEndChild);
    LazilyStyleNewChildRange(aStartChild, aEndChild);
    return;
  }

  bool isAppend;
  nsIFrame* prevSibling =
      GetInsertionPrevSibling(&insertion, aStartChild, &isAppend);

  LayoutFrameType frameType = insertion.mParentFrame->Type();
  LAYOUT_PHASE_TEMP_EXIT();
  if (MaybeRecreateForFrameset(insertion.mParentFrame, aStartChild,
                               aEndChild)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // Don't construct kids of leaves
  if (insertion.mParentFrame->IsLeaf()) {
    // Clear lazy bits so we don't try to construct again.
    ClearLazyBits(aStartChild, aEndChild);
    return;
  }

  LAYOUT_PHASE_TEMP_EXIT();
  if (WipeInsertionParent(insertion.mParentFrame)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  nsFrameConstructorState state(
      mPresShell, GetAbsoluteContainingBlock(insertion.mParentFrame, FIXED_POS),
      GetAbsoluteContainingBlock(insertion.mParentFrame, ABS_POS),
      GetFloatContainingBlock(insertion.mParentFrame),
      do_AddRef(mFrameTreeState));

  // Recover state for the containing block - we need to know if
  // it has :first-letter or :first-line style applied to it. The
  // reason we care is that the internal structure in these cases
  // is not the normal structure and requires custom updating
  // logic.
  nsContainerFrame* containingBlock = state.mFloatedList.mContainingBlock;
  bool haveFirstLetterStyle = false;
  bool haveFirstLineStyle = false;

  // In order to shave off some cycles, we only dig up the
  // containing block haveFirst* flags if the parent frame where
  // the insertion/append is occurring is an inline or block
  // container. For other types of containers this isn't relevant.
  StyleDisplayInside parentDisplayInside =
      insertion.mParentFrame->StyleDisplay()->DisplayInside();

  // Examine the insertion.mParentFrame where the insertion is taking
  // place. If it's a certain kind of container then some special
  // processing is done.
  if (StyleDisplayInside::Flow == parentDisplayInside) {
    // Recover the special style flags for the containing block
    if (containingBlock) {
      haveFirstLetterStyle = HasFirstLetterStyle(containingBlock);
      haveFirstLineStyle = ShouldHaveFirstLineStyle(
          containingBlock->GetContent(), containingBlock->Style());
    }

    if (haveFirstLetterStyle) {
      // If our current insertion.mParentFrame is a Letter frame, use its parent
      // as our new parent hint
      if (insertion.mParentFrame->IsLetterFrame()) {
        // If insertion.mParentFrame is out of flow, then we actually want the
        // parent of the placeholder frame.
        if (insertion.mParentFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
          nsPlaceholderFrame* placeholderFrame =
              insertion.mParentFrame->GetPlaceholderFrame();
          NS_ASSERTION(placeholderFrame, "No placeholder for out-of-flow?");
          insertion.mParentFrame = placeholderFrame->GetParent();
        } else {
          insertion.mParentFrame = insertion.mParentFrame->GetParent();
        }
      }

      // Remove the old letter frames before doing the insertion
      RemoveLetterFrames(mPresShell, state.mFloatedList.mContainingBlock);

      // Removing the letterframes messes around with the frame tree, removing
      // and creating frames.  We need to reget our prevsibling, parent frame,
      // etc.
      prevSibling = GetInsertionPrevSibling(&insertion, aStartChild, &isAppend);
      frameType = insertion.mParentFrame->Type();
    }
  }

  // This handles fallback to 'list-style-type' when a 'list-style-image' fails
  // to load.
  if (aStartChild->IsInNativeAnonymousSubtree() &&
      aStartChild->IsHTMLElement(nsGkAtoms::mozgeneratedcontentimage)) {
    MOZ_ASSERT(isSingleInsert);
    MOZ_ASSERT(insertion.mParentFrame->Style()->GetPseudoType() ==
                   PseudoStyleType::Marker,
               "we can only handle ::marker fallback for now");
    nsIContent* const nextSibling = aStartChild->GetNextSibling();
    MOZ_ASSERT(nextSibling && nextSibling->IsText(),
               "expected a text node after the list-style-image image");
    DestroyContext context(mPresShell);
    RemoveFrame(context, FrameChildListID::Principal,
                nextSibling->GetPrimaryFrame());
    auto* const container = aStartChild->GetParent()->AsElement();
    nsIContent* firstNewChild = nullptr;
    auto InsertChild = [this, container, nextSibling,
                        &firstNewChild](RefPtr<nsIContent>&& aChild) {
      // We don't strictly have to set NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE
      // here; it would get set under AppendChildTo.  But AppendChildTo might
      // think that we're going from not being anonymous to being anonymous and
      // do some extra work; setting the flag here avoids that.
      aChild->SetFlags(NODE_IS_IN_NATIVE_ANONYMOUS_SUBTREE);
      container->InsertChildBefore(aChild, nextSibling, false, IgnoreErrors());
      if (auto* childElement = Element::FromNode(aChild)) {
        // If we created any children elements, Servo needs to traverse them,
        // but the root is already set up.
        mPresShell->StyleSet()->StyleNewSubtree(childElement);
      }
      if (!firstNewChild) {
        firstNewChild = aChild;
      }
    };
    CreateGeneratedContentFromListStyleType(
        state, *insertion.mContainer->AsElement(),
        *insertion.mParentFrame->Style(), InsertChild);
    if (!firstNewChild) {
      // No fallback content - we're done.
      return;
    }
    aStartChild = firstNewChild;
    MOZ_ASSERT(firstNewChild->GetNextSibling() == nextSibling,
               "list-style-type should only create one child");
  }

  AutoFrameConstructionItemList items(this);
  RefPtr<ComputedStyle> parentStyle =
      ResolveComputedStyle(insertion.mContainer);
  ParentType parentType = GetParentType(frameType);
  FlattenedChildIterator iter(insertion.mContainer);
  const bool haveNoShadowDOM =
      !iter.ShadowDOMInvolved() || !iter.GetNextChild();
  if (aStartChild->GetPreviousSibling() && parentType == eTypeBlock &&
      haveNoShadowDOM) {
    // If there's a text node in the normal content list just before the
    // new nodes, and it has no frame, make a frame construction item for
    // it, because it might need a frame now.  No need to do this if our
    // parent type is not block, though, since WipeContainingBlock
    // already handles that situation.
    AddTextItemIfNeeded(state, *parentStyle, insertion,
                        aStartChild->GetPreviousSibling(), items);
  }

  const bool suppressWhiteSpaceOptimizations =
      isSingleInsert && aStartChild->IsRootOfNativeAnonymousSubtree();
  for (nsIContent* child = aStartChild; child != aEndChild;
       child = child->GetNextSibling()) {
    AddFrameConstructionItems(state, child, suppressWhiteSpaceOptimizations,
                              *parentStyle, insertion, items);
  }

  if (aEndChild && parentType == eTypeBlock && haveNoShadowDOM) {
    // If there's a text node in the normal content list just after the
    // new nodes, and it has no frame, make a frame construction item for
    // it, because it might need a frame now.  No need to do this if our
    // parent type is not block, though, since WipeContainingBlock
    // already handles that situation.
    AddTextItemIfNeeded(state, *parentStyle, insertion, aEndChild, items);
  }

  // Perform special check for diddling around with the frames in a special
  // inline frame. If we're appending before :after content, then we're not
  // really appending, so let WipeContainingBlock know that.
  LAYOUT_PHASE_TEMP_EXIT();
  if (WipeContainingBlock(state, containingBlock, insertion.mParentFrame, items,
                          isAppend, prevSibling)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // If the parent is a block frame, and we're not in a special case
  // where frames can be moved around, determine if the list is for the
  // start or end of the block.
  if (insertion.mParentFrame->IsBlockFrameOrSubclass() &&
      !haveFirstLetterStyle && !haveFirstLineStyle &&
      !IsFramePartOfIBSplit(insertion.mParentFrame)) {
    items.SetLineBoundaryAtStart(!prevSibling ||
                                 !prevSibling->IsInlineOutside() ||
                                 prevSibling->IsBrFrame());
    auto* nextSibling =
        ::GetInsertNextSibling(insertion.mParentFrame, prevSibling);
    items.SetLineBoundaryAtEnd(!nextSibling ||
                               !nextSibling->IsInlineOutside() ||
                               nextSibling->IsBrFrame());
  }
  // To suppress whitespace-only text frames, we have to verify that
  // our container's DOM child list matches its flattened tree child list.
  items.SetParentHasNoShadowDOM(haveNoShadowDOM);

  nsFrameConstructorSaveState floatSaveState;
  state.MaybePushFloatContainingBlock(insertion.mParentFrame, floatSaveState);

  if (state.mPresContext->IsPaginated()) {
    // Because this function can be called outside frame construction, we need
    // to set state.mAutoPageNameValue based on what the parent frame's auto
    // value is.
    // Calling this from outside the frame constructor can violate many of the
    // expectations in AutoFrameConstructionPageName, and unlike during frame
    // construction we already have an auto value from parentFrame, so we do
    // not use AutoFrameConstructionPageName here.
    state.mAutoPageNameValue = insertion.mParentFrame->GetAutoPageValue();
#ifdef DEBUG
    insertion.mParentFrame->mWasVisitedByAutoFrameConstructionPageName = true;
#endif
  }

  // If the container is a table and a caption will be appended, it needs to be
  // put in the table wrapper frame's additional child list.
  // We make no attempt here to set flags to indicate whether the list
  // will be at the start or end of a block. It doesn't seem worthwhile.
  nsFrameList frameList, captionList;
  ConstructFramesFromItemList(state, items, insertion.mParentFrame,
                              ParentIsWrapperAnonBox(insertion.mParentFrame),
                              frameList);

  if (frameList.NotEmpty()) {
    for (nsIContent* child = aStartChild; child != aEndChild;
         child = child->GetNextSibling()) {
      InvalidateCanvasIfNeeded(mPresShell, child);
    }

    if (LayoutFrameType::Table == frameType ||
        LayoutFrameType::TableWrapper == frameType) {
      PullOutCaptionFrames(frameList, captionList);
      if (prevSibling && prevSibling->IsTableCaption()) {
        // This can happen, but only if the table is empty (otherwise
        // SafeToInsertPseudoNeedingChildren bails).
        prevSibling = nullptr;
      }
    }
  }

  if (haveFirstLineStyle && insertion.mParentFrame == containingBlock &&
      isAppend) {
    // It's possible that the new frame goes into a first-line
    // frame. Look at it and see...
    AppendFirstLineFrames(state, containingBlock->GetContent(), containingBlock,
                          frameList);
  } else if (insertion.mParentFrame->Style()->IsInFirstLineSubtree()) {
    CheckForFirstLineInsertion(insertion.mParentFrame, frameList);
    CheckForFirstLineInsertion(insertion.mParentFrame, captionList);
  }

  // We might have captions; put them into the principal child list of the table
  // wrapper frame.
  if (captionList.NotEmpty()) {
    NS_ASSERTION(LayoutFrameType::Table == frameType ||
                     LayoutFrameType::TableWrapper == frameType,
                 "parent for caption is not table?");
    // We need to determine where to put the caption items; start with the
    // the parent frame that has already been determined and get the insertion
    // prevsibling of the first caption item.
    nsContainerFrame* outerTable = insertion.mParentFrame->IsTableFrame()
                                       ? insertion.mParentFrame->GetParent()
                                       : insertion.mParentFrame;

    // If the parent is not a table wrapper frame we will try to add frames
    // to a named child list that the parent does not honor and the frames
    // will get lost.
    MOZ_ASSERT(outerTable->IsTableWrapperFrame(),
               "Pseudo frame construction failure; "
               "a caption can be only a child of a table wrapper frame");

    nsIFrame* captionPrevSibling =
        FindCaptionPrevSibling(static_cast<nsTableWrapperFrame*>(outerTable),
                               captionList.FirstChild()->GetContent());
    captionList.ApplySetParent(outerTable);
    if (!captionPrevSibling->GetNextSibling()) {
      AppendFrames(outerTable, FrameChildListID::Principal,
                   std::move(captionList));
    } else {
      InsertFrames(outerTable, FrameChildListID::Principal, captionPrevSibling,
                   std::move(captionList));
    }
  }

  LAYOUT_PHASE_TEMP_EXIT();
  if (MaybeRecreateForColumnSpan(state, insertion.mParentFrame, frameList,
                                 prevSibling)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  if (frameList.NotEmpty()) {
    // Notify the parent frame
    if (isAppend) {
      AppendFramesToParent(state, insertion.mParentFrame, frameList,
                           prevSibling);
    } else {
      InsertFrames(insertion.mParentFrame, FrameChildListID::Principal,
                   prevSibling, std::move(frameList));
    }
  }

  if (haveFirstLetterStyle) {
    // Recover the letter frames for the containing block when
    // it has first-letter style.
    RecoverLetterFrames(state.mFloatedList.mContainingBlock);
  }

#ifdef DEBUG
  if (gReallyNoisyContentUpdates && insertion.mParentFrame) {
    printf(
        "nsCSSFrameConstructor::ContentRangeInserted: resulting frame "
        "model:\n");
    insertion.mParentFrame->List(stdout);
  }
#endif

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->ContentRangeInserted(mPresShell, aStartChild, aEndChild);
  }
#endif
}

static bool IsWhitespaceFrame(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "invalid argument");
  return aFrame->IsTextFrame() && aFrame->GetContent()->TextIsOnlyWhitespace();
}

static bool IsSyntheticColGroup(const nsIFrame* aFrame) {
  return aFrame->IsTableColGroupFrame() &&
         static_cast<const nsTableColGroupFrame*>(aFrame)->IsSynthetic();
}

static bool IsOnlyNonWhitespaceFrameInList(
    const nsFrameList& aFrameList, const nsIFrame* aFrame,
    const nsIFrame* aIgnoreFrame = nullptr) {
  for (const nsIFrame* f : aFrameList) {
    if (f == aIgnoreFrame) {
      continue;
    }
    if (f == aFrame) {
      // If we have continuations, ignore them too.
      aFrame = aFrame->GetNextContinuation();
    } else if (!IsWhitespaceFrame(f) && !IsSyntheticColGroup(f)) {
      // Synthetic colgroups get created unconditionally, so let's not consider
      // them as giving us a non-whitespace frame.
      return false;
    }
  }
  return true;
}

static bool AllChildListsAreEffectivelyEmpty(nsIFrame* aFrame) {
  for (auto& [list, listID] : aFrame->ChildLists()) {
    if (list.IsEmpty()) {
      continue;
    }
    // We have some existing frame, usually that would be considered as making
    // this list nonempty. But let's make an exception for the synthetic
    // colgroup that tables have, since that gets created unconditionally.
    if (listID == FrameChildListID::Principal && aFrame->IsTableFrame()) {
      if (nsIFrame* f = list.OnlyChild(); f && IsSyntheticColGroup(f)) {
        continue;
      }
    }
    return false;
  }
  return true;
}

static bool SafeToInsertPseudoNeedingChildren(nsIFrame* aFrame) {
  return AllChildListsAreEffectivelyEmpty(aFrame);
}

// Returns true if aFrame is the only meaningful child of aParent (which is
// known to be a wrapper-pseudo). This lets us determine whether aParent can be
// removed, as a result of aFrame being removed.
static bool IsOnlyMeaningfulChildOfWrapperPseudo(nsIFrame* aFrame,
                                                 nsIFrame* aParent) {
  MOZ_ASSERT(IsWrapperPseudo(aParent));
  // Handle a few special cases with tables and colgroups / captions.
  if (aParent->IsTableFrame()) {
    auto* wrapper = aParent->GetParent();
    MOZ_ASSERT(wrapper);
    MOZ_ASSERT(wrapper->IsTableWrapperFrame());
    MOZ_ASSERT(!aFrame->IsTableCaption(),
               "Caption parent should be the wrapper");
    // We can't remove the table if there are any captions present (captions are
    // never anonymous themselves), because table wrapper always relies on
    // having a table frame.
    if (!wrapper->PrincipalChildList().OnlyChild()) {
      return false;
    }
  }
  if (aFrame->IsTableCaption()) {
    MOZ_ASSERT(aParent->IsTableWrapperFrame());
    auto* table = static_cast<nsTableWrapperFrame*>(aParent)->InnerTableFrame();
    MOZ_ASSERT(table);
    return IsOnlyNonWhitespaceFrameInList(aParent->PrincipalChildList(), aFrame,
                                          /* aIgnoreFrame = */ table) &&
           // This checks for both colgroups and the principal list of the table
           // frame.
           AllChildListsAreEffectivelyEmpty(table);
  }
  return IsOnlyNonWhitespaceFrameInList(aParent->PrincipalChildList(), aFrame);
}

static bool CanRemoveWrapperPseudoForChildRemoval(nsIFrame* aFrame,
                                                  nsIFrame* aParent) {
  if (!IsOnlyMeaningfulChildOfWrapperPseudo(aFrame, aParent)) {
    return false;
  }
  if (aParent->GetPrevContinuation() || aParent->GetNextContinuation()) {
    // If our parent is fragmented we're not really the only meaningful child.
    return false;
  }
  if (aParent->IsRubyBaseContainerFrame()) {
    // We can't remove the first ruby base container of a ruby frame unless
    // it has no siblings. See CreateNeededPseudoSiblings.
    return aParent->GetPrevSibling() || !aParent->GetNextSibling();
  }
  return true;
}

bool nsCSSFrameConstructor::ContentWillBeRemoved(nsIContent* aChild,
                                                 RemovalKind aKind) {
  MOZ_ASSERT(aChild);
  MOZ_ASSERT(
      !aChild->IsRootOfNativeAnonymousSubtree() || !aChild->GetNextSibling(),
      "Anonymous roots don't have siblings");
  AUTO_PROFILER_LABEL_HOT("nsCSSFrameConstructor::ContentWillBeRemoved",
                          LAYOUT_FrameConstruction);
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);
  nsPresContext* presContext = mPresShell->GetPresContext();
  MOZ_ASSERT(presContext, "Our presShell should have a valid presContext");

  const bool wasRemovingContent = mRemovingContent;
  auto _ = MakeScopeExit([&] { mRemovingContent = wasRemovingContent; });
  mRemovingContent = true;

  // We want to detect when the viewport override element stored in the
  // prescontext is in the subtree being removed.  Except in fullscreen cases
  // (which are handled in Element::UnbindFromTree and do not get stored on the
  // prescontext), the override element is always either the root element or a
  // <body> child of the root element.  So we can only be removing the stored
  // override element if the thing being removed is either the override element
  // itself or the root element (which can be a parent of the override element).
  //
  // The !wasRemovingContent check makes sure that we don't re-enter here from
  // other ContentWillBeRemoved calls, as that'd be useless work, and we don't
  // want to incorrectly pick aChild again as our viewport scroll style element
  // if it's getting removed from the DOM.
  if ((aChild == presContext->GetViewportScrollStylesOverrideElement() ||
       aChild->IsRootElement()) &&
      !wasRemovingContent) {
    // We might be removing the element that we propagated viewport scrollbar
    // styles from.  Recompute those. (This clause covers two of the three
    // possible scrollbar-propagation sources: the <body> [as aChild or a
    // descendant] and the root node. The other possible scrollbar-propagation
    // source is a fullscreen element, and we have code elsewhere to update
    // scrollbars after fullscreen elements are removed -- specifically, it's
    // part of the fullscreen cleanup code called by Element::UnbindFromTree.
    // We don't handle the fullscreen case here, because it doesn't change the
    // scrollbar styles override element stored on the prescontext.)
    const Element* removingElement =
        aKind == RemovalKind::Dom ? aChild->AsElement() : nullptr;
    Element* newOverrideElement =
        presContext->UpdateViewportScrollStylesOverride(removingElement);

    // If aChild is the root, then we don't need to do any reframing of
    // newOverrideElement, because we're about to tear down the whole frame tree
    // anyway.  And we need to make sure we don't do any such reframing, because
    // reframing the <body> can trigger a reframe of the <html> and then reenter
    // here.
    //
    // But if aChild is not the root, and if newOverrideElement is not the root
    // and isn't aChild (which it could be if all we're doing here is reframing
    // the current override element), it needs reframing.  In particular, it
    // used to have a scrollframe (because its overflow was not "visible"), but
    // now it will propagate its overflow to the viewport, so it should not need
    // a scrollframe anymore.
    if (aChild->GetParent() && newOverrideElement &&
        newOverrideElement->GetParent() && newOverrideElement != aChild) {
      LAYOUT_PHASE_TEMP_EXIT();
      RecreateFramesForContent(newOverrideElement, InsertionKind::Async);
      LAYOUT_PHASE_TEMP_REENTER();
    }
  }

#ifdef DEBUG
  if (gNoisyContentUpdates) {
    printf(
        "nsCSSFrameConstructor::ContentWillBeRemoved container=%p child=%p\n",
        aChild->GetParent(), aChild);
    if (gReallyNoisyContentUpdates) {
      aChild->GetParent()->List(stdout, 0);
    }
  }
#endif

  nsIFrame* childFrame = aChild->GetPrimaryFrame();
  if (!childFrame || childFrame->GetContent() != aChild) {
    // XXXbz the GetContent() != aChild check is needed due to bug 135040.
    // Remove it once that's fixed.
    childFrame = nullptr;
  }

  // If we're removing the root, then make sure to remove things starting at
  // the viewport's child instead of the primary frame (which might even be
  // null if the root was display:none, even though the frames above it got
  // created).  Detecting removal of a root is a little exciting; in particular,
  // having no parent is necessary but NOT sufficient.
  //
  // Due to how we process reframes, the content node might not even be in our
  // document by now.  So explicitly check whether the viewport's first kid's
  // content node is aChild.
  //
  // FIXME(emilio): I think the "might not be in our document" bit is impossible
  // now.
  bool isRoot = false;
  if (!aChild->GetParent()) {
    if (nsIFrame* viewport = GetRootFrame()) {
      nsIFrame* firstChild = viewport->PrincipalChildList().FirstChild();
      if (firstChild && firstChild->GetContent() == aChild) {
        isRoot = true;
        childFrame = firstChild;
        NS_ASSERTION(!childFrame->GetNextSibling(), "How did that happen?");
      }
    }
  }

  // We need to be conservative about when to determine whether something has
  // display: contents or not because at this point our actual display may be
  // different.
  //
  // Consider the case of:
  //
  //   <div id="A" style="display: contents"><div id="B"></div></div>
  //
  // If we reconstruct A because its display changed to "none", we still need to
  // cleanup the frame on B, but A's display is now "none", so we can't poke at
  // the style of it.
  //
  // FIXME(emilio, bug 1450366): We can make this faster without adding much
  // complexity for the display: none -> other case, which right now
  // unnecessarily walks the content tree down.
  auto CouldHaveBeenDisplayContents = [aKind](nsIContent* aContent) -> bool {
    return aContent->IsElement() && (aKind != RemovalKind::Dom ||
                                     IsDisplayContents(aContent->AsElement()));
  };

  if (!childFrame) {
    if (CouldHaveBeenDisplayContents(aChild)) {
      // NOTE(emilio): We may iterate through ::before and ::after here and they
      // may be gone after the respective ContentWillBeRemoved call. Right now
      // StyleChildrenIterator handles that properly, so it's not an issue.
      StyleChildrenIterator iter(aChild);
      for (nsIContent* c = iter.GetNextChild(); c; c = iter.GetNextChild()) {
        if (c->GetPrimaryFrame() || CouldHaveBeenDisplayContents(c)) {
          LAYOUT_PHASE_TEMP_EXIT();
          bool didReconstruct = ContentWillBeRemoved(c, aKind);
          LAYOUT_PHASE_TEMP_REENTER();
          if (didReconstruct) {
            return true;
          }
        }
      }
    }
    return false;
  }

  if (aKind != RemovalKind::Dom) {
    // Before removing the frames associated with the content object,
    // ask them to save their state onto our state object.
    CaptureStateForFramesOf(aChild, mFrameTreeState);
  }

  InvalidateCanvasIfNeeded(mPresShell, aChild);

  // See whether we need to remove more than just childFrame
  LAYOUT_PHASE_TEMP_EXIT();
  if (MaybeRecreateContainerForFrameRemoval(childFrame)) {
    LAYOUT_PHASE_TEMP_REENTER();
    return true;
  }
  LAYOUT_PHASE_TEMP_REENTER();

  // Get the childFrame's parent frame
  nsIFrame* parentFrame = childFrame->GetParent();
  LayoutFrameType parentType = parentFrame->Type();

  if (parentType == LayoutFrameType::FrameSet &&
      IsSpecialFramesetChild(aChild)) {
    // Just reframe the parent, since framesets are weird like that.
    LAYOUT_PHASE_TEMP_EXIT();
    RecreateFramesForContent(parentFrame->GetContent(), InsertionKind::Async);
    LAYOUT_PHASE_TEMP_REENTER();
    return true;
  }

  // If we're a child of MathML, then we should reframe the MathML content.
  // If we're non-MathML, then we would be wrapped in a block so we need to
  // check our grandparent in that case.
  nsIFrame* possibleMathMLAncestor = parentType == LayoutFrameType::Block
                                         ? parentFrame->GetParent()
                                         : parentFrame;
  if (possibleMathMLAncestor->IsMathMLFrame()) {
    LAYOUT_PHASE_TEMP_EXIT();
    RecreateFramesForContent(parentFrame->GetContent(), InsertionKind::Async);
    LAYOUT_PHASE_TEMP_REENTER();
    return true;
  }

#ifdef ACCESSIBILITY
  if (aKind != RemovalKind::ForReconstruction) {
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->ContentRemoved(mPresShell, aChild);
    }
  }
#endif

  // Examine the containing-block for the removed content and see if
  // :first-letter style applies.
  nsIFrame* inflowChild = childFrame;
  if (childFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    inflowChild = childFrame->GetPlaceholderFrame();
    NS_ASSERTION(inflowChild, "No placeholder for out-of-flow?");
  }
  nsContainerFrame* containingBlock =
      GetFloatContainingBlock(inflowChild->GetParent());
  bool haveFLS = containingBlock && HasFirstLetterStyle(containingBlock);
  if (haveFLS) {
    // Trap out to special routine that handles adjusting a blocks
    // frame tree when first-letter style is present.
#ifdef NOISY_FIRST_LETTER
    printf("ContentWillBeRemoved: containingBlock=");
    containingBlock->ListTag(stdout);
    printf(" parentFrame=");
    parentFrame->ListTag(stdout);
    printf(" childFrame=");
    childFrame->ListTag(stdout);
    printf("\n");
#endif

    // First update the containing blocks structure by removing the
    // existing letter frames. This makes the subsequent logic
    // simpler.
    RemoveLetterFrames(mPresShell, containingBlock);

    // Recover childFrame and parentFrame
    childFrame = aChild->GetPrimaryFrame();
    if (!childFrame || childFrame->GetContent() != aChild) {
      // XXXbz the GetContent() != aChild check is needed due to bug 135040.
      // Remove it once that's fixed.
      return false;
    }
    parentFrame = childFrame->GetParent();
    parentType = parentFrame->Type();

#ifdef NOISY_FIRST_LETTER
    printf("  ==> revised parentFrame=");
    parentFrame->ListTag(stdout);
    printf(" childFrame=");
    childFrame->ListTag(stdout);
    printf("\n");
#endif
  }

#ifdef DEBUG
  if (gReallyNoisyContentUpdates) {
    printf("nsCSSFrameConstructor::ContentWillBeRemoved: childFrame=");
    childFrame->ListTag(stdout);
    putchar('\n');
    parentFrame->List(stdout);
  }
#endif

  // Notify the parent frame that it should delete the frame
  if (childFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)) {
    childFrame = childFrame->GetPlaceholderFrame();
    NS_ASSERTION(childFrame, "Missing placeholder frame for out of flow.");
    parentFrame = childFrame->GetParent();
  }

  // Take care of wrapper anonymous boxes that we might need to remove while
  // at it. Note that MaybeRecreateContainerForFrameRemoval takes care of
  // harder cases (merging sibling anonymous boxes etc).
  while (IsWrapperPseudo(parentFrame) &&
         CanRemoveWrapperPseudoForChildRemoval(childFrame, parentFrame)) {
    childFrame = parentFrame;
    parentFrame = childFrame->GetParent();
  }

  const bool canSkipWhitespaceFixup = [&] {
    if (aKind == RemovalKind::ForReconstruction) {
      // If we're just reconstructing frames for the element, then the
      // following ContentInserted notification on the element will
      // take care of fixing up any adjacent white-space text nodes.
      return true;
    }
    switch (parentFrame->Type()) {
      case LayoutFrameType::Table:
      case LayoutFrameType::TableRow:
      case LayoutFrameType::TableRowGroup:
      case LayoutFrameType::TableCol:
      case LayoutFrameType::TableColGroup:
      case LayoutFrameType::TableWrapper: {
        // Tables ignore all whitespace within their wrappers, so we can avoid
        // reconstructing adjacent whitespace if the table is not anonymous.
        if (!IsInAnonymousTable(parentFrame)) {
          return true;
        }
        // If the table is anonymous and the child is at the edges, we might
        // need to create whitespace at the edges of the table that wasn't
        // there before, so we can only skip the check if childFrame has frames
        // around. Captions are extra-special here, because they're siblings
        // with the main table frame, so we can't count that as a relevant
        // sibling for our purposes.
        auto* prevSibling = childFrame->GetPrevSibling();
        return prevSibling && !prevSibling->IsTableFrame() &&
               childFrame->GetNextSibling();
      }
      case LayoutFrameType::GridContainer:
      case LayoutFrameType::FlexContainer:
        // Flex and grid containers similarly skip whitespace and wrap
        // non-whitespace in anonymous flex items, so any change to
        // white-space that could matter would have triggered the
        // reconstruction of the container itself due to merging anonymous
        // grid / flex items.
        return true;
      default:
        break;
    }
    return false;
  }();
  DestroyContext context(mPresShell);
  RemoveFrame(context, nsLayoutUtils::GetChildListNameFor(childFrame),
              childFrame);

  // NOTE(emilio): aChild could be dead here already if it is a ::before or
  // ::after pseudo-element (since in that case it was owned by childFrame,
  // which we just destroyed).

  if (isRoot) {
    mRootElementFrame = nullptr;
    mRootElementStyleFrame = nullptr;
    mDocElementContainingBlock = nullptr;
    mCanvasFrame = nullptr;
    mPageSequenceFrame = nullptr;
  }

  if (haveFLS && mRootElementFrame) {
    RecoverLetterFrames(containingBlock);
  }

  if (!canSkipWhitespaceFixup) {
    MOZ_ASSERT(aChild->GetParentNode(),
               "How did we have a sibling without a parent?");
    // Adjacent whitespace-only text nodes might have been suppressed if
    // this node does not have inline ends. Create frames for them now
    // if necessary.
    // Reframe any text node just before the node being removed, if there is
    // one, and if it's not the last child or the first child. If a whitespace
    // textframe was being suppressed and it's now the last child or first
    // child then it can stay suppressed since the parent must be a block
    // and hence it's adjacent to a block end.
    // If aOldNextSibling is null, then the text node before the node being
    // removed is the last node, and we don't need to worry about it.
    nsIContent* prevSibling = aChild->GetPreviousSibling();
    if (prevSibling && prevSibling->GetPreviousSibling()) {
      LAYOUT_PHASE_TEMP_EXIT();
      ReframeTextIfNeeded(prevSibling);
      LAYOUT_PHASE_TEMP_REENTER();
    }
    // Reframe any text node just after the node being removed, if there is
    // one, and if it's not the last child or the first child.
    nsIContent* nextSibling = aChild->GetNextSibling();
    if (nextSibling && prevSibling && nextSibling->GetNextSibling()) {
      LAYOUT_PHASE_TEMP_EXIT();
      ReframeTextIfNeeded(nextSibling);
      LAYOUT_PHASE_TEMP_REENTER();
    }
  }

#ifdef DEBUG
  if (gReallyNoisyContentUpdates && parentFrame) {
    printf(
        "nsCSSFrameConstructor::ContentWillBeRemoved: resulting frame "
        "model:\n");
    parentFrame->List(stdout);
  }
#endif

  return false;
}

/**
 * This method invalidates the canvas when frames are removed or added for a
 * node that might have its background propagated to the canvas, i.e., a
 * document root node or an HTML BODY which is a child of the root node.
 *
 * @param aFrame a frame for a content node about to be removed or a frame that
 *               was just created for a content node that was inserted.
 */
static void InvalidateCanvasIfNeeded(PresShell* aPresShell, nsIContent* aNode) {
  MOZ_ASSERT(aPresShell->GetRootFrame(), "What happened here?");
  MOZ_ASSERT(aPresShell->GetPresContext(), "Say what?");

  //  Note that both in ContentWillBeRemoved and ContentInserted the content
  //  node will still have the right parent pointer, so looking at that is ok.

  nsIContent* parent = aNode->GetParent();
  if (parent) {
    // Has a parent; might not be what we want
    nsIContent* grandParent = parent->GetParent();
    if (grandParent) {
      // Has a grandparent, so not what we want
      return;
    }

    // Check whether it's an HTML body
    if (!aNode->IsHTMLElement(nsGkAtoms::body)) {
      return;
    }
  }

  // At this point the node has no parent or it's an HTML <body> child of the
  // root.  We might not need to invalidate in this case (eg we might be in
  // XHTML or something), but chances are we want to.  Play it safe.
  // Invalidate the viewport.

  nsIFrame* rootFrame = aPresShell->GetRootFrame();
  rootFrame->InvalidateFrameSubtree();
}

bool nsCSSFrameConstructor::EnsureFrameForTextNodeIsCreatedAfterFlush(
    CharacterData* aContent) {
  if (!aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE)) {
    return false;
  }

  if (mAlwaysCreateFramesForIgnorableWhitespace) {
    return false;
  }

  // Text frame may have been suppressed. Disable suppression and signal that a
  // flush should be performed. We do this on a document-wide basis so that
  // pages that repeatedly query metrics for collapsed-whitespace text nodes
  // don't trigger pathological behavior.
  mAlwaysCreateFramesForIgnorableWhitespace = true;
  Element* root = mDocument->GetRootElement();
  if (!root) {
    return false;
  }

  RestyleManager()->PostRestyleEvent(root, RestyleHint{0},
                                     nsChangeHint_ReconstructFrame);
  return true;
}

void nsCSSFrameConstructor::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  AUTO_PROFILER_LABEL_HOT("nsCSSFrameConstructor::CharacterDataChanged",
                          LAYOUT_FrameConstruction);
  AUTO_LAYOUT_PHASE_ENTRY_POINT(mPresShell->GetPresContext(), FrameC);

  if ((aContent->HasFlag(NS_CREATE_FRAME_IF_NON_WHITESPACE) &&
       !aContent->TextIsOnlyWhitespace()) ||
      (aContent->HasFlag(NS_REFRAME_IF_WHITESPACE) &&
       aContent->TextIsOnlyWhitespace())) {
#ifdef DEBUG
    nsIFrame* frame = aContent->GetPrimaryFrame();
    NS_ASSERTION(!frame || !frame->IsGeneratedContentFrame(),
                 "Bit should never be set on generated content");
#endif
    LAYOUT_PHASE_TEMP_EXIT();
    RecreateFramesForContent(aContent, InsertionKind::Async);
    LAYOUT_PHASE_TEMP_REENTER();
    return;
  }

  // It's possible the frame whose content changed isn't inserted into the
  // frame hierarchy yet, or that there is no frame that maps the content
  if (nsIFrame* frame = aContent->GetPrimaryFrame()) {
#if 0
    NS_FRAME_LOG(NS_FRAME_TRACE_CALLS,
       ("nsCSSFrameConstructor::CharacterDataChanged: content=%p[%s] subcontent=%p frame=%p",
        aContent, ContentTag(aContent, 0),
        aSubContent, frame));
#endif

    if (frame->HasAnyStateBits(NS_FRAME_IS_IN_SINGLE_CHAR_MI)) {
      LAYOUT_PHASE_TEMP_EXIT();
      RecreateFramesForContent(aContent, InsertionKind::Async);
      LAYOUT_PHASE_TEMP_REENTER();
      return;
    }

    // Special check for text content that is a child of a letter frame.  If
    // this happens, we should remove the letter frame, do whatever we're
    // planning to do with this notification, then put the letter frame back.
    // Note that this is basically what RecreateFramesForContent ends up doing;
    // the reason we dont' want to call that here is that our text content
    // could be native anonymous, in which case RecreateFramesForContent would
    // completely barf on it.  And recreating the non-anonymous ancestor would
    // just lead us to come back into this notification (e.g. if quotes or
    // counters are involved), leading to a loop.
    nsContainerFrame* block = GetFloatContainingBlock(frame);
    bool haveFirstLetterStyle = false;
    if (block) {
      // See if the block has first-letter style applied to it.
      haveFirstLetterStyle = HasFirstLetterStyle(block);
      if (haveFirstLetterStyle) {
        RemoveLetterFrames(mPresShell, block);
        // Reget |frame|, since we might have killed it.
        // Do we really need to call CharacterDataChanged in this case, though?
        frame = aContent->GetPrimaryFrame();
        NS_ASSERTION(frame, "Should have frame here!");
      }
    }

    // Notify the first frame that maps the content. It will generate a reflow
    // command
    frame->CharacterDataChanged(aInfo);

    if (haveFirstLetterStyle) {
      RecoverLetterFrames(block);
    }
  }
}

void nsCSSFrameConstructor::RecalcQuotesAndCounters() {
  nsAutoScriptBlocker scriptBlocker;

  if (mQuotesDirty) {
    mQuotesDirty = false;
    mContainStyleScopeManager.RecalcAllQuotes();
  }

  if (mCountersDirty) {
    mCountersDirty = false;
    mContainStyleScopeManager.RecalcAllCounters();
  }

  NS_ASSERTION(!mQuotesDirty, "Quotes updates will be lost");
  NS_ASSERTION(!mCountersDirty, "Counter updates will be lost");
}

void nsCSSFrameConstructor::NotifyCounterStylesAreDirty() {
  mContainStyleScopeManager.SetAllCountersDirty();
  CountersDirty();
}

void nsCSSFrameConstructor::WillDestroyFrameTree() {
#if defined(DEBUG_dbaron_off)
  mContainStyleScopeManager.DumpCounters();
#endif

  // Prevent frame tree destruction from being O(N^2)
  mContainStyleScopeManager.Clear();
  nsFrameManager::Destroy();
}

// STATIC

// XXXbz I'd really like this method to go away. Once we have inline-block and
// I can just use that for sized broken images, that can happen, maybe.
//
// NOTE(emilio): This needs to match MozAltContent handling.
void nsCSSFrameConstructor::GetAlternateTextFor(const Element& aElement,
                                                nsAString& aAltText) {
  // The "alt" attribute specifies alternate text that is rendered
  // when the image can not be displayed.
  if (aElement.GetAttr(nsGkAtoms::alt, aAltText)) {
    return;
  }

  if (aElement.IsHTMLElement(nsGkAtoms::input)) {
    // If there's no "alt" attribute, and aElement is an input element, then use
    // the value of the "value" attribute.
    if (aElement.GetAttr(nsGkAtoms::value, aAltText)) {
      return;
    }

    // If there's no "value" attribute either, then use the localized string for
    // "Submit" as the alternate text.
    nsContentUtils::GetMaybeLocalizedString(PropertiesFile::FORMS_PROPERTIES,
                                            "Submit", aElement.OwnerDoc(),
                                            aAltText);
  }
}

nsIFrame* nsCSSFrameConstructor::CreateContinuingOuterTableFrame(
    nsIFrame* aFrame, nsContainerFrame* aParentFrame, nsIContent* aContent,
    ComputedStyle* aComputedStyle) {
  nsTableWrapperFrame* newFrame =
      NS_NewTableWrapperFrame(mPresShell, aComputedStyle);

  newFrame->Init(aContent, aParentFrame, aFrame);

  // Create a continuing inner table frame. Note we don't replicate the
  // captions: a comment used to hint at that, but the code dealing with that
  // never worked and was removed in bug 309322.
  nsFrameList newChildFrames;

  MOZ_ASSERT(aFrame->IsTableWrapperFrame());
  if (nsTableFrame* childFrame =
          static_cast<nsTableWrapperFrame*>(aFrame)->InnerTableFrame()) {
    nsIFrame* continuingTableFrame =
        CreateContinuingFrame(childFrame, newFrame);
    newChildFrames.AppendFrame(nullptr, continuingTableFrame);
  }

  // Set the table wrapper's initial child list
  newFrame->SetInitialChildList(FrameChildListID::Principal,
                                std::move(newChildFrames));

  return newFrame;
}

nsIFrame* nsCSSFrameConstructor::CreateContinuingTableFrame(
    nsIFrame* aFrame, nsContainerFrame* aParentFrame, nsIContent* aContent,
    ComputedStyle* aComputedStyle) {
  nsTableFrame* newFrame = NS_NewTableFrame(mPresShell, aComputedStyle);

  newFrame->Init(aContent, aParentFrame, aFrame);

  // Replicate any header/footer frames
  nsFrameList childFrames;
  for (nsIFrame* childFrame : aFrame->PrincipalChildList()) {
    // See if it's a header/footer, possibly wrapped in a scroll frame.
    nsTableRowGroupFrame* rowGroupFrame =
        static_cast<nsTableRowGroupFrame*>(childFrame);
    // If the row group was continued, then don't replicate it.
    nsIFrame* rgNextInFlow = rowGroupFrame->GetNextInFlow();
    if (rgNextInFlow) {
      rowGroupFrame->SetRepeatable(false);
    } else if (rowGroupFrame->IsRepeatable()) {
      // Replicate the header/footer frame.
      nsTableRowGroupFrame* headerFooterFrame;
      nsFrameList childList;

      nsFrameConstructorState state(
          mPresShell, GetAbsoluteContainingBlock(newFrame, FIXED_POS),
          GetAbsoluteContainingBlock(newFrame, ABS_POS), nullptr);
      state.mCreatingExtraFrames = true;

      ComputedStyle* const headerFooterComputedStyle = rowGroupFrame->Style();
      headerFooterFrame = static_cast<nsTableRowGroupFrame*>(
          NS_NewTableRowGroupFrame(mPresShell, headerFooterComputedStyle));

      nsIContent* headerFooter = rowGroupFrame->GetContent();
      headerFooterFrame->Init(headerFooter, newFrame, nullptr);

      nsFrameConstructorSaveState absoluteSaveState;
      MakeTablePartAbsoluteContainingBlock(state, absoluteSaveState,
                                           headerFooterFrame);

      nsFrameConstructorSaveState floatSaveState;
      state.MaybePushFloatContainingBlock(headerFooterFrame, floatSaveState);

      ProcessChildren(state, headerFooter, rowGroupFrame->Style(),
                      headerFooterFrame, true, childList, false, nullptr);
      NS_ASSERTION(state.mFloatedList.IsEmpty(), "unexpected floated element");
      headerFooterFrame->SetInitialChildList(FrameChildListID::Principal,
                                             std::move(childList));
      headerFooterFrame->SetRepeatable(true);

      // Table specific initialization
      headerFooterFrame->InitRepeatedFrame(rowGroupFrame);

      // XXX Deal with absolute and fixed frames...
      childFrames.AppendFrame(nullptr, headerFooterFrame);
    }
  }

  // Set the table frame's initial child list
  newFrame->SetInitialChildList(FrameChildListID::Principal,
                                std::move(childFrames));

  return newFrame;
}

nsIFrame* nsCSSFrameConstructor::CreateContinuingFrame(
    nsIFrame* aFrame, nsContainerFrame* aParentFrame, bool aIsFluid) {
  ComputedStyle* computedStyle = aFrame->Style();
  nsIFrame* newFrame = nullptr;
  nsIFrame* nextContinuation = aFrame->GetNextContinuation();
  nsIFrame* nextInFlow = aFrame->GetNextInFlow();

  // Use the frame type to determine what type of frame to create
  LayoutFrameType frameType = aFrame->Type();
  nsIContent* content = aFrame->GetContent();

  if (LayoutFrameType::Text == frameType) {
    newFrame = NS_NewContinuingTextFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::Inline == frameType) {
    newFrame = NS_NewInlineFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::Block == frameType) {
    MOZ_ASSERT(!aFrame->IsTableCaption(),
               "no support for fragmenting table captions yet");
    newFrame = NS_NewBlockFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::ColumnSetWrapper == frameType) {
    newFrame = NS_NewColumnSetWrapperFrame(mPresShell, computedStyle,
                                           NS_FRAME_STATE_NONE);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::ColumnSet == frameType) {
    MOZ_ASSERT(!aFrame->IsTableCaption(),
               "no support for fragmenting table captions yet");
    newFrame =
        NS_NewColumnSetFrame(mPresShell, computedStyle, NS_FRAME_STATE_NONE);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::PrintedSheet == frameType) {
    newFrame = ConstructPrintedSheetFrame(mPresShell, aParentFrame, aFrame);
  } else if (LayoutFrameType::Page == frameType) {
    nsCanvasFrame* canvasFrame;  // (unused outparam for ConstructPageFrame)
    newFrame =
        ConstructPageFrame(mPresShell, aParentFrame, aFrame, canvasFrame);
  } else if (LayoutFrameType::TableWrapper == frameType) {
    newFrame = CreateContinuingOuterTableFrame(aFrame, aParentFrame, content,
                                               computedStyle);
  } else if (LayoutFrameType::Table == frameType) {
    newFrame = CreateContinuingTableFrame(aFrame, aParentFrame, content,
                                          computedStyle);
  } else if (LayoutFrameType::TableRowGroup == frameType) {
    newFrame = NS_NewTableRowGroupFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::TableRow == frameType) {
    nsTableRowFrame* rowFrame = NS_NewTableRowFrame(mPresShell, computedStyle);

    rowFrame->Init(content, aParentFrame, aFrame);

    // Create a continuing frame for each table cell frame
    nsFrameList newChildList;
    nsIFrame* cellFrame = aFrame->PrincipalChildList().FirstChild();
    while (cellFrame) {
      // See if it's a table cell frame
      if (cellFrame->IsTableCellFrame()) {
        nsIFrame* continuingCellFrame =
            CreateContinuingFrame(cellFrame, rowFrame);
        newChildList.AppendFrame(nullptr, continuingCellFrame);
      }
      cellFrame = cellFrame->GetNextSibling();
    }

    rowFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(newChildList));
    newFrame = rowFrame;

  } else if (LayoutFrameType::TableCell == frameType) {
    // Warning: If you change this and add a wrapper frame around table cell
    // frames, make sure Bug 368554 doesn't regress!
    // See IsInAutoWidthTableCellForQuirk() in nsImageFrame.cpp.
    nsTableFrame* tableFrame =
        static_cast<nsTableRowFrame*>(aParentFrame)->GetTableFrame();
    nsTableCellFrame* cellFrame =
        NS_NewTableCellFrame(mPresShell, computedStyle, tableFrame);

    cellFrame->Init(content, aParentFrame, aFrame);

    // Create a continuing area frame
    nsIFrame* blockFrame = aFrame->PrincipalChildList().FirstChild();
    nsIFrame* continuingBlockFrame =
        CreateContinuingFrame(blockFrame, cellFrame);

    SetInitialSingleChild(cellFrame, continuingBlockFrame);
    newFrame = cellFrame;
  } else if (LayoutFrameType::Line == frameType) {
    newFrame = NS_NewFirstLineFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::Letter == frameType) {
    newFrame = NS_NewFirstLetterFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::Image == frameType) {
    auto* imageFrame = static_cast<nsImageFrame*>(aFrame);
    newFrame = imageFrame->CreateContinuingFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::ImageControl == frameType) {
    newFrame = NS_NewImageControlFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::FieldSet == frameType) {
    newFrame = NS_NewFieldSetFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::FlexContainer == frameType) {
    newFrame = NS_NewFlexContainerFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::GridContainer == frameType) {
    newFrame = NS_NewGridContainerFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::Ruby == frameType) {
    newFrame = NS_NewRubyFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::RubyBaseContainer == frameType) {
    newFrame = NS_NewRubyBaseContainerFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else if (LayoutFrameType::RubyTextContainer == frameType) {
    newFrame = NS_NewRubyTextContainerFrame(mPresShell, computedStyle);
    newFrame->Init(content, aParentFrame, aFrame);
  } else {
    MOZ_CRASH("unexpected frame type");
  }

  // Init() set newFrame to be a fluid continuation of aFrame.
  // If we want a non-fluid continuation, we need to call SetPrevContinuation()
  // to reset NS_FRAME_IS_FLUID_CONTINUATION.
  if (!aIsFluid) {
    newFrame->SetPrevContinuation(aFrame);
  }

  // If a continuing frame needs to carry frame state bits from its previous
  // continuation or parent, set them in nsIFrame::Init(), or in any derived
  // frame class's Init() if the bits are belong to specific group.

  if (nextInFlow) {
    nextInFlow->SetPrevInFlow(newFrame);
    newFrame->SetNextInFlow(nextInFlow);
  } else if (nextContinuation) {
    nextContinuation->SetPrevContinuation(newFrame);
    newFrame->SetNextContinuation(nextContinuation);
  }

  // aFrame cannot be a dynamic reflow root because it has a continuation now.
  aFrame->RemoveStateBits(NS_FRAME_DYNAMIC_REFLOW_ROOT);

  MOZ_ASSERT(!newFrame->GetNextSibling(), "unexpected sibling");
  return newFrame;
}

void nsCSSFrameConstructor::MaybeSetNextPageContentFramePageName(
    const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "Frame should not be null");
  // No parent means the root frame, which isn't what this funciton is for.
  MOZ_ASSERT(aFrame->GetParent(),
             "Frame should be the first child placed on a new page, not the "
             "root frame.");
  if (mNextPageContentFramePageName) {
    return;
  }
  const nsAtom* const autoValue = aFrame->GetParent()->GetAutoPageValue();
  mNextPageContentFramePageName = aFrame->ComputePageValue(autoValue);
}

nsresult nsCSSFrameConstructor::ReplicateFixedFrames(
    nsPageContentFrame* aParentFrame) {
  // Now deal with fixed-pos things....  They should appear on all pages,
  // so we want to move over the placeholders when processing the child
  // of the pageContentFrame.

  nsIFrame* prevPageContentFrame = aParentFrame->GetPrevInFlow();
  if (!prevPageContentFrame) {
    return NS_OK;
  }
  nsContainerFrame* canvasFrame =
      do_QueryFrame(aParentFrame->PrincipalChildList().FirstChild());
  nsIFrame* prevCanvasFrame =
      prevPageContentFrame->PrincipalChildList().FirstChild();
  if (!canvasFrame || !prevCanvasFrame) {
    // document's root element frame missing
    return NS_ERROR_UNEXPECTED;
  }

  nsFrameList fixedPlaceholders;
  nsIFrame* firstFixed =
      prevPageContentFrame->GetChildList(FrameChildListID::Absolute)
          .FirstChild();
  if (!firstFixed) {
    return NS_OK;
  }

  // Don't allow abs-pos descendants of the fixed content to escape the content.
  // This should not normally be possible (because fixed-pos elements should
  // be absolute containers) but fixed-pos tables currently aren't abs-pos
  // containers.
  nsFrameConstructorState state(mPresShell, aParentFrame, nullptr,
                                mRootElementFrame);
  state.mCreatingExtraFrames = true;

  // We can't use an ancestor filter here, because we're not going to
  // be usefully recurring down the tree.  This means that other
  // places in frame construction can't assume a filter is
  // initialized!

  // Iterate across fixed frames and replicate each whose placeholder is a
  // descendant of aFrame. (We don't want to explicitly copy placeholders that
  // are within fixed frames, because that would cause duplicates on the new
  // page - bug 389619)
  for (nsIFrame* fixed = firstFixed; fixed; fixed = fixed->GetNextSibling()) {
    nsIFrame* prevPlaceholder = fixed->GetPlaceholderFrame();
    if (prevPlaceholder && nsLayoutUtils::IsProperAncestorFrame(
                               prevCanvasFrame, prevPlaceholder)) {
      // We want to use the same style as the primary style frame for
      // our content
      nsIContent* content = fixed->GetContent();
      ComputedStyle* computedStyle =
          nsLayoutUtils::GetStyleFrame(content)->Style();
      AutoFrameConstructionItemList items(this);
      AddFrameConstructionItemsInternal(state, content, canvasFrame, true,
                                        computedStyle,
                                        {ItemFlag::AllowPageBreak}, items);
      ConstructFramesFromItemList(state, items, canvasFrame,
                                  /* aParentIsWrapperAnonBox = */ false,
                                  fixedPlaceholders);
    }
  }

  // Add the placeholders to our primary child list.
  // XXXbz this is a little screwed up, since the fixed frames will have
  // broken auto-positioning. Oh, well.
  NS_ASSERTION(!canvasFrame->PrincipalChildList().FirstChild(),
               "leaking frames; doc root continuation must be empty");
  canvasFrame->SetInitialChildList(FrameChildListID::Principal,
                                   std::move(fixedPlaceholders));
  return NS_OK;
}

nsCSSFrameConstructor::InsertionPoint nsCSSFrameConstructor::GetInsertionPoint(
    nsIContent* aChild) {
  MOZ_ASSERT(aChild);
  nsIContent* insertionElement = aChild->GetFlattenedTreeParent();
  if (!insertionElement) {
    // The element doesn't belong in the flattened tree, and thus we don't want
    // to render it.
    return {};
  }

  return {GetContentInsertionFrameFor(insertionElement), insertionElement};
}

// Capture state for the frame tree rooted at the frame associated with the
// content object, aContent
void nsCSSFrameConstructor::CaptureStateForFramesOf(
    nsIContent* aContent, nsILayoutHistoryState* aHistoryState) {
  if (!aHistoryState) {
    return;
  }
  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (frame == mRootElementFrame) {
    frame = mRootElementFrame
                ? GetAbsoluteContainingBlock(mRootElementFrame, FIXED_POS)
                : GetRootFrame();
  }
  for (; frame;
       frame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(frame)) {
    CaptureFrameState(frame, aHistoryState);
  }
}

static bool IsWhitespaceFrame(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame, "invalid argument");
  return aFrame->IsTextFrame() && aFrame->GetContent()->TextIsOnlyWhitespace();
}

static nsIFrame* FindNextNonWhitespaceSibling(nsIFrame* aFrame) {
  nsIFrame* f = aFrame;
  do {
    f = f->GetNextSibling();
  } while (f && IsWhitespaceFrame(f));
  return f;
}

static nsIFrame* FindPreviousNonWhitespaceSibling(nsIFrame* aFrame) {
  nsIFrame* f = aFrame;
  do {
    f = f->GetPrevSibling();
  } while (f && IsWhitespaceFrame(f));
  return f;
}

static nsIFrame* CheckRubyContainers(nsIFrame* aFrame, nsIFrame* aParent) {
  auto* ancestor = aParent;
  auto* ancestorChild = aFrame;
  while (ancestor && IsWrapperPseudo(ancestor) &&
         CanRemoveWrapperPseudoForChildRemoval(ancestorChild, ancestor)) {
    ancestorChild = ancestor;
    ancestor = ancestorChild->GetParent();
  }
  if (!ancestor) {
    return nullptr;
  }
  const auto ancestorType = ancestor->Type();
  if (ancestorType != LayoutFrameType::Ruby &&
      !RubyUtils::IsRubyContainerBox(ancestorType)) {
    return nullptr;
  }
  return ancestor;
}

bool nsCSSFrameConstructor::MaybeRecreateContainerForFrameRemoval(
    nsIFrame* aFrame) {
#define TRACE(reason)                                                       \
  PROFILER_MARKER("MaybeRecreateContainerForFrameRemoval: " reason, LAYOUT, \
                  {}, Tracing, "Layout")
  MOZ_ASSERT(aFrame, "Must have a frame");
  MOZ_ASSERT(aFrame->GetParent(), "Frame shouldn't be root");
  MOZ_ASSERT(aFrame == aFrame->FirstContinuation(),
             "aFrame not the result of GetPrimaryFrame()?");

  nsIFrame* inFlowFrame = aFrame->HasAnyStateBits(NS_FRAME_OUT_OF_FLOW)
                              ? aFrame->GetPlaceholderFrame()
                              : aFrame;
  MOZ_ASSERT(inFlowFrame, "How did that happen?");
  MOZ_ASSERT(inFlowFrame == inFlowFrame->FirstContinuation(),
             "placeholder for primary frame has previous continuations?");
  nsIFrame* parent = inFlowFrame->GetParent();

  if (inFlowFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
    nsIFrame* grandparent = parent->GetParent();
    MOZ_ASSERT(grandparent);

    bool needsReframe =
        // 1. Removing a column-span may lead to an empty
        // ::-moz-column-span-wrapper.
        inFlowFrame->IsColumnSpan() ||
        // 2. Removing a frame which has any column-span siblings may also
        // lead to an empty ::-moz-column-span-wrapper subtree. The
        // column-span siblings were the frame's children, but later become
        // the frame's siblings after CreateColumnSpanSiblings().
        inFlowFrame->HasColumnSpanSiblings() ||
        // 3. Removing the only child of a ::-moz-column-content, whose
        // ColumnSet grandparent has a previous column-span sibling, requires
        // reframing since we might connect the ColumnSet's next column-span
        // sibling (if there's one). Note that this isn't actually needed if
        // the ColumnSet is at the end of ColumnSetWrapper since we create
        // empty ones at the end anyway, but we're not worried about
        // optimizing that case.
        (parent->Style()->GetPseudoType() ==
             PseudoStyleType::MozColumnContent &&
         // The only child in ::-moz-column-content (might be tall enough to
         // split across columns)
         !inFlowFrame->GetPrevSibling() && !inFlowFrame->GetNextSibling() &&
         // That ::-moz-column-content is the first column.
         !parent->GetPrevInFlow() &&
         // The ColumnSet grandparent has a previous sibling that is a
         // column-span.
         grandparent->GetPrevSibling());

    if (needsReframe) {
      nsContainerFrame* containingBlock =
          GetMultiColumnContainingBlockFor(inFlowFrame);

#ifdef DEBUG
      if (IsFramePartOfIBSplit(inFlowFrame)) {
        nsIFrame* ibContainingBlock = GetIBContainingBlockFor(inFlowFrame);
        MOZ_ASSERT(containingBlock == ibContainingBlock ||
                       nsLayoutUtils::IsProperAncestorFrame(containingBlock,
                                                            ibContainingBlock),
                   "Multi-column containing block should be equal to or be the "
                   "ancestor of the IB containing block!");
      }
#endif

      TRACE("Multi-column");
      RecreateFramesForContent(containingBlock->GetContent(),
                               InsertionKind::Async);
      return true;
    }
  }

  if (IsFramePartOfIBSplit(aFrame)) {
    // The removal functions can't handle removal of an {ib} split directly; we
    // need to rebuild the containing block.
    TRACE("IB split removal");
    ReframeContainingBlock(aFrame);
    return true;
  }

  if (inFlowFrame->IsRenderedLegend()) {
    TRACE("Fieldset / Legend");
    RecreateFramesForContent(parent->GetContent(), InsertionKind::Async);
    return true;
  }

  // Might need to reconstruct things if this frame's nextSibling is a table
  // or ruby pseudo, since removal of this frame might mean that this pseudo
  // needs to get merged with the frame's prevSibling if that's also a table
  // or ruby pseudo.
  nsIFrame* nextSibling =
      FindNextNonWhitespaceSibling(inFlowFrame->LastContinuation());
  NS_ASSERTION(!IsWrapperPseudo(inFlowFrame),
               "Shouldn't happen here (we start removals from primary frames)");
  // Effectively, for the ruby pseudo sibling case, only pseudo <ruby> frame
  // need to be checked here, since all other types of such frames will have
  // a ruby container parent, and be catched by "Check ruby containers" below.
  if (nextSibling && IsWrapperPseudo(nextSibling)) {
    nsIFrame* prevSibling = FindPreviousNonWhitespaceSibling(inFlowFrame);
    if (prevSibling && IsWrapperPseudo(prevSibling)) {
      TRACE("Pseudo sibling");
      // Good enough to recreate frames for aFrame's parent's content; even if
      // aFrame's parent is a pseudo, that'll be the right content node.
      // FIXME(emilio): Consider doing a more subtle check here like, only if
      // prevSibling and nextSibling share frame type. Or even consider just
      // moving the frames around and destroying nextSibling?
      RecreateFramesForContent(parent->GetContent(), InsertionKind::Async);
      return true;
    }
  }

  // Check ruby containers
  if (const auto* ancestor = CheckRubyContainers(inFlowFrame, parent)) {
    // In ruby containers, pseudo frames may be created from
    // whitespaces or even nothing. There are two cases we actually
    // need to handle here, but hard to check exactly:
    // 1. Status of spaces beside the frame may vary, and related
    //    frames may be constructed or destroyed accordingly.
    // 2. The type of the first child of a ruby frame determines
    //    whether a pseudo ruby base container should exist.
    TRACE("Ruby container");
    RecreateFramesForContent(ancestor->GetContent(), InsertionKind::Async);
    return true;
  }

  // Reconstruct if inflowFrame is parent's only child, and parent is, or has,
  // a non-fluid continuation, i.e. it was split by bidi resolution
  if (!inFlowFrame->GetPrevSibling() && !inFlowFrame->GetNextSibling() &&
      ((parent->GetPrevContinuation() && !parent->GetPrevInFlow()) ||
       (parent->GetNextContinuation() && !parent->GetNextInFlow()))) {
    TRACE("Removing last child of non-fluid split parent");
    RecreateFramesForContent(parent->GetContent(), InsertionKind::Async);
    return true;
  }

  // We might still need to reconstruct things if the parent of inFlowFrame is
  // ib-split, since in that case the removal of aFrame might affect the
  // splitting of its parent.
  if (!IsFramePartOfIBSplit(parent)) {
    return false;
  }

  // If inFlowFrame is not the only in-flow child of |parent|, then removing
  // it will change nothing about the {ib} split.
  if (inFlowFrame != parent->PrincipalChildList().FirstChild() ||
      inFlowFrame->LastContinuation()->GetNextSibling()) {
    return false;
  }

  // If the parent is the first or last part of the {ib} split, then
  // removing one of its kids will have no effect on the splitting.
  // Get the first continuation up front so we don't have to do it twice.
  nsIFrame* parentFirstContinuation = parent->FirstContinuation();
  if (!GetIBSplitSibling(parentFirstContinuation) ||
      !GetIBSplitPrevSibling(parentFirstContinuation)) {
    return false;
  }

  TRACE("IB split parent");
  ReframeContainingBlock(parent);
  return true;
#undef TRACE
}

void nsCSSFrameConstructor::UpdateTableCellSpans(nsIContent* aContent) {
  nsTableCellFrame* cellFrame = do_QueryFrame(aContent->GetPrimaryFrame());

  // It's possible that this warning could fire if some other style change
  // simultaneously changes the 'display' of the element and makes it no
  // longer be a table cell.
  NS_WARNING_ASSERTION(cellFrame, "Hint should only be posted on table cells!");

  if (cellFrame) {
    cellFrame->GetTableFrame()->RowOrColSpanChanged(cellFrame);
  }
}

static nsIContent* GetTopmostMathMLElement(nsIContent* aMathMLContent) {
  MOZ_ASSERT(aMathMLContent->IsMathMLElement());
  MOZ_ASSERT(aMathMLContent->GetPrimaryFrame());
  MOZ_ASSERT(aMathMLContent->GetPrimaryFrame()->IsMathMLFrame());
  nsIContent* root = aMathMLContent;

  for (nsIContent* parent = aMathMLContent->GetFlattenedTreeParent(); parent;
       parent = parent->GetFlattenedTreeParent()) {
    nsIFrame* frame = parent->GetPrimaryFrame();
    if (!frame || !frame->IsMathMLFrame()) {
      break;
    }
    root = parent;
  }

  return root;
}

// We don't know how to re-insert an anonymous subtree root, so recreate the
// closest non-generated ancestor instead, except for a few special cases...
static bool ShouldRecreateContainerForNativeAnonymousContentRoot(
    nsIContent* aContent) {
  if (!aContent->IsRootOfNativeAnonymousSubtree()) {
    return false;
  }
  if (ManualNACPtr::IsManualNAC(aContent)) {
    // Editor NAC, would enter an infinite loop, and we sorta get away with it
    // because it's all abspos.
    return false;
  }
  if (auto* el = Element::FromNode(aContent)) {
    if (el->GetPseudoElementType() ==
        PseudoStyleType::MozSnapshotContainingBlock) {
      // Much like above, all abspos and on its own top layer so insertion order
      // wouldn't really matter anyways.
      return false;
    }
    if (auto* classes = el->GetClasses()) {
      if (classes->Contains(nsGkAtoms::mozCustomContentContainer,
                            eCaseMatters)) {
        // Canvas anonymous content (like the custom content container) is also
        // fine, because its only sibling is a tooltip which is also abspos, so
        // relative insertion order doesn't really matter.
        //
        // This is important because the inspector uses it, and we don't want
        // inspecting the page to change behavior heavily (and reframing
        // unfortunately has side-effects sometimes, even though they're bugs).
        return false;
      }
    }
  }

  return true;
}

void nsCSSFrameConstructor::RecreateFramesForContent(
    nsIContent* aContent, InsertionKind aInsertionKind) {
  MOZ_ASSERT(aContent);

  // If there is no document, we don't want to recreate frames for it.  (You
  // shouldn't generally be giving this method content without a document
  // anyway).
  // Rebuilding the frame tree can have bad effects, especially if it's the
  // frame tree for chrome (see bug 157322).
  if (NS_WARN_IF(!aContent->GetComposedDoc())) {
    return;
  }

  // TODO(emilio): We technically can find the right insertion point nowadays
  // using StyleChildrenIterator rather than FlattenedChildIterator. But we'd
  // need to tweak the setup to insert into replaced elements to filter which
  // anonymous roots can be allowed, and which can't.
  //
  // TODO(emilio, 2022): Is this true? If we have a replaced element we wouldn't
  // have generated e.g., a ::before/::after pseudo-element to begin with (which
  // is what this code is about, so maybe we can just remove this piece of code
  // altogether).
  if (ShouldRecreateContainerForNativeAnonymousContentRoot(aContent)) {
    do {
      aContent = aContent->GetParent();
    } while (ShouldRecreateContainerForNativeAnonymousContentRoot(aContent));
    return RecreateFramesForContent(aContent, InsertionKind::Async);
  }

  nsIFrame* frame = aContent->GetPrimaryFrame();
  if (frame && frame->IsMathMLFrame()) {
    // Reframe the topmost MathML element to prevent exponential blowup
    // (see bug 397518).
    aContent = GetTopmostMathMLElement(aContent);
    frame = aContent->GetPrimaryFrame();
  }

  if (frame) {
    nsIFrame* parent = frame->GetParent();
    nsIContent* parentContent = parent ? parent->GetContent() : nullptr;
    // If the parent frame is a leaf then the subsequent insert will fail to
    // create a frame, so we need to recreate the parent content. This happens
    // with native anonymous content from the editor.
    if (parent && parent->IsLeaf() && parentContent &&
        parentContent != aContent) {
      return RecreateFramesForContent(parentContent, InsertionKind::Async);
    }
  }

  if (frame && MaybeRecreateContainerForFrameRemoval(frame)) {
    return;
  }

  MOZ_ASSERT(aContent->GetParentNode());
  const auto removalKind = [&] {
    if (aInsertionKind == InsertionKind::Sync && aContent->IsElement() &&
        Servo_Element_IsDisplayNone(aContent->AsElement())) {
      // If we know we're not going to have frames after reconstructing, it's
      // more efficient to do some of that work (a11y notifications, fixing-up
      // text nodes) earlier.
      return RemovalKind::ForDisplayNoneChange;
    }
    return RemovalKind::ForReconstruction;
  }();
  const bool didReconstruct = ContentWillBeRemoved(aContent, removalKind);
  if (didReconstruct || removalKind == RemovalKind::ForDisplayNoneChange) {
    // If ContentWillBeRemoved triggered reconstruction, then we don't need to
    // do anything else because the frames will already have been built.
    return;
  }
  if (aInsertionKind == InsertionKind::Async && aContent->IsElement()) {
    // FIXME(emilio, bug 1397239): There's nothing removing the frame state
    // for elements that go away before we come back to the frame constructor.
    //
    // Also, it'd be nice to just use the `ContentRangeInserted` path for
    // both elements and non-elements, but we need to make lazy frame
    // construction to apply to all elements first.
    //
    // TODO(emilio): I think lazy frame construction works everywhere now, so
    // maybe we can remove this altogether?
    RestyleManager()->PostRestyleEvent(aContent->AsElement(), RestyleHint{0},
                                       nsChangeHint_ReconstructFrame);
    return;
  }
  // Now, recreate the frames associated with this content object.
  ContentRangeInserted(aContent, aContent->GetNextSibling(), aInsertionKind);
}

bool nsCSSFrameConstructor::DestroyFramesFor(nsIContent* aContent) {
  MOZ_ASSERT(aContent && aContent->GetParentNode());
  return ContentWillBeRemoved(aContent, RemovalKind::ForReconstruction);
}

//////////////////////////////////////////////////////////////////////

// Block frame construction code

already_AddRefed<ComputedStyle> nsCSSFrameConstructor::GetFirstLetterStyle(
    nsIContent* aContent, ComputedStyle* aComputedStyle) {
  if (aContent) {
    return mPresShell->StyleSet()->ResolvePseudoElementStyle(
        *aContent->AsElement(), PseudoStyleType::FirstLetter, nullptr,
        aComputedStyle);
  }
  return nullptr;
}

already_AddRefed<ComputedStyle> nsCSSFrameConstructor::GetFirstLineStyle(
    nsIContent* aContent, ComputedStyle* aComputedStyle) {
  if (aContent) {
    return mPresShell->StyleSet()->ResolvePseudoElementStyle(
        *aContent->AsElement(), PseudoStyleType::FirstLine, nullptr,
        aComputedStyle);
  }
  return nullptr;
}

// Predicate to see if a given content (block element) has
// first-letter style applied to it.
bool nsCSSFrameConstructor::ShouldHaveFirstLetterStyle(
    nsIContent* aContent, ComputedStyle* aComputedStyle) {
  return nsLayoutUtils::HasPseudoStyle(aContent, aComputedStyle,
                                       PseudoStyleType::FirstLetter,
                                       mPresShell->GetPresContext());
}

bool nsCSSFrameConstructor::HasFirstLetterStyle(nsIFrame* aBlockFrame) {
  MOZ_ASSERT(aBlockFrame, "Need a frame");
  NS_ASSERTION(aBlockFrame->IsBlockFrameOrSubclass(), "Not a block frame?");
  return aBlockFrame->HasAnyStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE);
}

bool nsCSSFrameConstructor::ShouldHaveFirstLineStyle(
    nsIContent* aContent, ComputedStyle* aComputedStyle) {
  bool hasFirstLine = nsLayoutUtils::HasPseudoStyle(
      aContent, aComputedStyle, PseudoStyleType::FirstLine,
      mPresShell->GetPresContext());
  return hasFirstLine &&
         !aContent->IsAnyOfHTMLElements(nsGkAtoms::fieldset, nsGkAtoms::input,
                                        nsGkAtoms::textarea);
}

void nsCSSFrameConstructor::ShouldHaveSpecialBlockStyle(
    nsIContent* aContent, ComputedStyle* aComputedStyle,
    bool* aHaveFirstLetterStyle, bool* aHaveFirstLineStyle) {
  *aHaveFirstLetterStyle = ShouldHaveFirstLetterStyle(aContent, aComputedStyle);
  *aHaveFirstLineStyle = ShouldHaveFirstLineStyle(aContent, aComputedStyle);
}

/* static */
const nsCSSFrameConstructor::PseudoParentData
    nsCSSFrameConstructor::sPseudoParentData[eParentTypeCount] = {
        // Cell
        {{&nsCSSFrameConstructor::ConstructTableCell,
          FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET | FCDATA_USE_CHILD_ITEMS |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRow)},
         PseudoStyleType::MozTableCell},
        // Row
        {{&nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET | FCDATA_USE_CHILD_ITEMS |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRowGroup)},
         PseudoStyleType::MozTableRow},
        // Row group
        {{&nsCSSFrameConstructor::ConstructTableRowOrRowGroup,
          FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET | FCDATA_USE_CHILD_ITEMS |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable)},
         PseudoStyleType::MozTableRowGroup},
        // Column group
        {{ToCreationFunc(NS_NewTableColGroupFrame),
          FCDATA_IS_TABLE_PART | FCDATA_SKIP_FRAMESET |
              FCDATA_DISALLOW_OUT_OF_FLOW | FCDATA_USE_CHILD_ITEMS |
              FCDATA_SKIP_ABSPOS_PUSH |
              // Not FCDATA_IS_WRAPPER_ANON_BOX, because we don't need to
              // restyle these: they have non-inheriting styles.
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeTable)},
         PseudoStyleType::MozTableColumnGroup},
        // Table
        {{&nsCSSFrameConstructor::ConstructTable,
          FCDATA_SKIP_FRAMESET | FCDATA_USE_CHILD_ITEMS |
              FCDATA_IS_WRAPPER_ANON_BOX},
         PseudoStyleType::MozTable},
        // Ruby
        {{ToCreationFunc(NS_NewRubyFrame),
          FCDATA_IS_LINE_PARTICIPANT | FCDATA_USE_CHILD_ITEMS |
              FCDATA_IS_WRAPPER_ANON_BOX | FCDATA_SKIP_FRAMESET},
         PseudoStyleType::MozRuby},
        // Ruby Base
        {{ToCreationFunc(NS_NewRubyBaseFrame),
          FCDATA_USE_CHILD_ITEMS | FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRubyBaseContainer) |
              FCDATA_SKIP_FRAMESET},
         PseudoStyleType::MozRubyBase},
        // Ruby Base Container
        {{ToCreationFunc(NS_NewRubyBaseContainerFrame),
          FCDATA_USE_CHILD_ITEMS | FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRuby) |
              FCDATA_SKIP_FRAMESET},
         PseudoStyleType::MozRubyBaseContainer},
        // Ruby Text
        {{ToCreationFunc(NS_NewRubyTextFrame),
          FCDATA_USE_CHILD_ITEMS | FCDATA_IS_LINE_PARTICIPANT |
              FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRubyTextContainer) |
              FCDATA_SKIP_FRAMESET},
         PseudoStyleType::MozRubyText},
        // Ruby Text Container
        {{ToCreationFunc(NS_NewRubyTextContainerFrame),
          FCDATA_USE_CHILD_ITEMS | FCDATA_IS_WRAPPER_ANON_BOX |
              FCDATA_DESIRED_PARENT_TYPE_TO_BITS(eTypeRuby) |
              FCDATA_SKIP_FRAMESET},
         PseudoStyleType::MozRubyTextContainer}};

void nsCSSFrameConstructor::CreateNeededAnonFlexOrGridItems(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
    nsIFrame* aParentFrame) {
  if (aItems.IsEmpty()) {
    return;
  }

  if (!aParentFrame->IsFlexOrGridContainer()) {
    return;
  }

  const bool isLegacyWebKitBox =
      IsFlexContainerForLegacyWebKitBox(aParentFrame);
  FCItemIterator iter(aItems);
  do {
    // Advance iter past children that don't want to be wrapped
    if (iter.SkipItemsThatDontNeedAnonFlexOrGridItem(aState,
                                                     isLegacyWebKitBox)) {
      // Hit the end of the items without finding any remaining children that
      // need to be wrapped. We're finished!
      return;
    }

    // If our next potentially-wrappable child is whitespace, then see if
    // there's anything wrappable immediately after it. If not, we just drop
    // the whitespace and move on. (We're not supposed to create any anonymous
    // flex/grid items that _only_ contain whitespace).
    // (BUT if this is generated content, then we don't give whitespace nodes
    // any special treatment, because they're probably not really whitespace --
    // they're just temporarily empty, waiting for their generated text.)
    // XXXdholbert If this node's generated text will *actually end up being
    // entirely whitespace*, then we technically should still skip over it, per
    // the CSS grid & flexbox specs. I'm not bothering with that at this point,
    // since it's a pretty extreme edge case.
    if (!aParentFrame->IsGeneratedContentFrame() &&
        iter.item().IsWhitespace(aState)) {
      FCItemIterator afterWhitespaceIter(iter);
      bool hitEnd = afterWhitespaceIter.SkipWhitespace(aState);
      bool nextChildNeedsAnonItem =
          !hitEnd && afterWhitespaceIter.item().NeedsAnonFlexOrGridItem(
                         aState, isLegacyWebKitBox);

      if (!nextChildNeedsAnonItem) {
        // There's nothing after the whitespace that we need to wrap, so we
        // just drop this run of whitespace.
        iter.DeleteItemsTo(this, afterWhitespaceIter);
        if (hitEnd) {
          // Nothing left to do -- we're finished!
          return;
        }
        // else, we have a next child and it does not want to be wrapped.  So,
        // we jump back to the beginning of the loop to skip over that child
        // (and anything else non-wrappable after it)
        MOZ_ASSERT(!iter.IsDone() && !iter.item().NeedsAnonFlexOrGridItem(
                                         aState, isLegacyWebKitBox),
                   "hitEnd and/or nextChildNeedsAnonItem lied");
        continue;
      }
    }

    // Now |iter| points to the first child that needs to be wrapped in an
    // anonymous flex/grid item. Now we see how many children after it also want
    // to be wrapped in an anonymous flex/grid item.
    FCItemIterator endIter(iter);  // iterator to find the end of the group
    endIter.SkipItemsThatNeedAnonFlexOrGridItem(aState, isLegacyWebKitBox);

    NS_ASSERTION(iter != endIter,
                 "Should've had at least one wrappable child to seek past");

    // Now, we create the anonymous flex or grid item to contain the children
    // between |iter| and |endIter|.
    nsIContent* parentContent = aParentFrame->GetContent();
    RefPtr<ComputedStyle> wrapperStyle =
        mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
            PseudoStyleType::MozAnonymousItem, aParentFrame->Style());

    static constexpr FrameConstructionData sBlockFCData(
        ToCreationFunc(NS_NewBlockFrame), FCDATA_SKIP_FRAMESET |
                                              FCDATA_USE_CHILD_ITEMS |
                                              FCDATA_IS_WRAPPER_ANON_BOX);

    // Use the content of our parent frame
    auto* newItem = new (this) FrameConstructionItem(
        &sBlockFCData, parentContent, wrapperStyle.forget(), true);

    newItem->mIsAllInline =
        newItem->mComputedStyle->StyleDisplay()->IsInlineOutsideStyle();
    newItem->mIsBlock = !newItem->mIsAllInline;

    MOZ_ASSERT(!newItem->mIsAllInline && newItem->mIsBlock,
               "expecting anonymous flex/grid items to be block-level "
               "(this will make a difference when we encounter "
               "'align-items: baseline')");

    // Anonymous flex and grid items induce line boundaries around their
    // contents.
    newItem->mChildItems.SetLineBoundaryAtStart(true);
    newItem->mChildItems.SetLineBoundaryAtEnd(true);
    // The parent of the items in aItems is also the parent of the items
    // in mChildItems
    newItem->mChildItems.SetParentHasNoShadowDOM(aItems.ParentHasNoShadowDOM());

    // Eat up all items between |iter| and |endIter| and put them in our
    // wrapper. This advances |iter| to point to |endIter|.
    iter.AppendItemsToList(this, endIter, newItem->mChildItems);

    iter.InsertItem(newItem);
  } while (!iter.IsDone());
}

/* static */ nsCSSFrameConstructor::RubyWhitespaceType
nsCSSFrameConstructor::ComputeRubyWhitespaceType(StyleDisplay aPrevDisplay,
                                                 StyleDisplay aNextDisplay) {
  MOZ_ASSERT(aPrevDisplay.IsRuby() && aNextDisplay.IsRuby());
  if (aPrevDisplay == aNextDisplay &&
      (aPrevDisplay == StyleDisplay::RubyBase ||
       aPrevDisplay == StyleDisplay::RubyText)) {
    return eRubyInterLeafWhitespace;
  }
  if (aNextDisplay == StyleDisplay::RubyText ||
      aNextDisplay == StyleDisplay::RubyTextContainer) {
    return eRubyInterLevelWhitespace;
  }
  return eRubyInterSegmentWhitespace;
}

/**
 * This function checks the content from |aStartIter| to |aEndIter|,
 * determines whether it contains only whitespace, and if yes,
 * interprets the type of whitespace. This method does not change
 * any of the iters.
 */
/* static */ nsCSSFrameConstructor::RubyWhitespaceType
nsCSSFrameConstructor::InterpretRubyWhitespace(nsFrameConstructorState& aState,
                                               const FCItemIterator& aStartIter,
                                               const FCItemIterator& aEndIter) {
  if (!aStartIter.item().IsWhitespace(aState)) {
    return eRubyNotWhitespace;
  }

  FCItemIterator spaceEndIter(aStartIter);
  spaceEndIter.SkipWhitespace(aState);
  if (spaceEndIter != aEndIter) {
    return eRubyNotWhitespace;
  }

  // Any leading or trailing whitespace in non-pseudo ruby box
  // should have been trimmed, hence there should not be any
  // whitespace at the start or the end.
  MOZ_ASSERT(!aStartIter.AtStart() && !aEndIter.IsDone());
  FCItemIterator prevIter(aStartIter);
  prevIter.Prev();
  return ComputeRubyWhitespaceType(
      prevIter.item().mComputedStyle->StyleDisplay()->mDisplay,
      aEndIter.item().mComputedStyle->StyleDisplay()->mDisplay);
}

/**
 * This function eats up consecutive items which do not want the current
 * parent into either a ruby base box or a ruby text box.  When it
 * returns, |aIter| points to the first item it doesn't wrap.
 */
void nsCSSFrameConstructor::WrapItemsInPseudoRubyLeafBox(
    FCItemIterator& aIter, ComputedStyle* aParentStyle,
    nsIContent* aParentContent) {
  StyleDisplay parentDisplay = aParentStyle->StyleDisplay()->mDisplay;
  ParentType parentType, wrapperType;
  if (parentDisplay == StyleDisplay::RubyTextContainer) {
    parentType = eTypeRubyTextContainer;
    wrapperType = eTypeRubyText;
  } else {
    MOZ_ASSERT(parentDisplay == StyleDisplay::RubyBaseContainer);
    parentType = eTypeRubyBaseContainer;
    wrapperType = eTypeRubyBase;
  }

  MOZ_ASSERT(aIter.item().DesiredParentType() != parentType,
             "Should point to something needs to be wrapped.");

  FCItemIterator endIter(aIter);
  endIter.SkipItemsNotWantingParentType(parentType);

  WrapItemsInPseudoParent(aParentContent, aParentStyle, wrapperType, aIter,
                          endIter);
}

/**
 * This function eats up consecutive items into a ruby level container.
 * It may create zero or one level container. When it returns, |aIter|
 * points to the first item it doesn't wrap.
 */
void nsCSSFrameConstructor::WrapItemsInPseudoRubyLevelContainer(
    nsFrameConstructorState& aState, FCItemIterator& aIter,
    ComputedStyle* aParentStyle, nsIContent* aParentContent) {
  MOZ_ASSERT(aIter.item().DesiredParentType() != eTypeRuby,
             "Pointing to a level container?");

  FrameConstructionItem& firstItem = aIter.item();
  ParentType wrapperType = firstItem.DesiredParentType();
  if (wrapperType != eTypeRubyTextContainer) {
    // If the first item is not ruby text,
    // it should be in a base container.
    wrapperType = eTypeRubyBaseContainer;
  }

  FCItemIterator endIter(aIter);
  do {
    if (endIter.SkipItemsWantingParentType(wrapperType) ||
        // If the skipping above stops at some item which wants a
        // different ruby parent, then we have finished.
        IsRubyParentType(endIter.item().DesiredParentType())) {
      // No more items need to be wrapped in this level container.
      break;
    }

    FCItemIterator contentEndIter(endIter);
    contentEndIter.SkipItemsNotWantingRubyParent();
    // endIter must be on something doesn't want a ruby parent.
    MOZ_ASSERT(contentEndIter != endIter);

    // InterpretRubyWhitespace depends on the fact that any leading or
    // trailing whitespace described in the spec have been trimmed at
    // this point. With this precondition, it is safe not to check
    // whether contentEndIter has been done.
    RubyWhitespaceType whitespaceType =
        InterpretRubyWhitespace(aState, endIter, contentEndIter);
    if (whitespaceType == eRubyInterLevelWhitespace) {
      // Remove inter-level whitespace.
      bool atStart = (aIter == endIter);
      endIter.DeleteItemsTo(this, contentEndIter);
      if (atStart) {
        aIter = endIter;
      }
    } else if (whitespaceType == eRubyInterSegmentWhitespace) {
      // If this level container starts with inter-segment whitespaces,
      // wrap them. Break at contentEndIter. Otherwise, leave it here.
      // Break at endIter. They will be wrapped when we are here again.
      if (aIter == endIter) {
        MOZ_ASSERT(wrapperType == eTypeRubyBaseContainer,
                   "Inter-segment whitespace should be wrapped in rbc");
        endIter = contentEndIter;
      }
      break;
    } else if (wrapperType == eTypeRubyTextContainer &&
               whitespaceType != eRubyInterLeafWhitespace) {
      // Misparented inline content that's not inter-annotation
      // whitespace doesn't belong in a pseudo ruby text container.
      // Break at endIter.
      break;
    } else {
      endIter = contentEndIter;
    }
  } while (!endIter.IsDone());

  // It is possible that everything our parent wants us to wrap is
  // simply an inter-level whitespace, which has been trimmed, or
  // an inter-segment whitespace, which will be wrapped later.
  // In those cases, don't create anything.
  if (aIter != endIter) {
    WrapItemsInPseudoParent(aParentContent, aParentStyle, wrapperType, aIter,
                            endIter);
  }
}

/**
 * This function trims leading and trailing whitespaces
 * in the given item list.
 */
void nsCSSFrameConstructor::TrimLeadingAndTrailingWhitespaces(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems) {
  FCItemIterator iter(aItems);
  if (!iter.IsDone() && iter.item().IsWhitespace(aState)) {
    FCItemIterator spaceEndIter(iter);
    spaceEndIter.SkipWhitespace(aState);
    iter.DeleteItemsTo(this, spaceEndIter);
  }

  iter.SetToEnd();
  if (!iter.AtStart()) {
    FCItemIterator spaceEndIter(iter);
    do {
      iter.Prev();
      if (iter.AtStart()) {
        // It's fine to not check the first item, because we
        // should have trimmed leading whitespaces above.
        break;
      }
    } while (iter.item().IsWhitespace(aState));
    iter.Next();
    if (iter != spaceEndIter) {
      iter.DeleteItemsTo(this, spaceEndIter);
    }
  }
}

/**
 * This function walks through the child list (aItems) and creates
 * needed pseudo ruby boxes to wrap misparented children.
 */
void nsCSSFrameConstructor::CreateNeededPseudoInternalRubyBoxes(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
    nsIFrame* aParentFrame) {
  const ParentType ourParentType = GetParentType(aParentFrame);
  if (!IsRubyParentType(ourParentType) ||
      aItems.AllWantParentType(ourParentType)) {
    return;
  }

  if (!IsRubyPseudo(aParentFrame) ||
      ourParentType == eTypeRuby /* for 'display:block ruby' */) {
    // Normally, ruby pseudo frames start from and end at some elements,
    // which means they don't have leading and trailing whitespaces at
    // all.  But there are two cases where they do actually have leading
    // or trailing whitespaces:
    // 1. It is an inter-segment whitespace which in an individual ruby
    //    base container.
    // 2. The pseudo frame starts from or ends at consecutive inline
    //    content, which is not pure whitespace, but includes some.
    // In either case, the whitespaces are not the leading or trailing
    // whitespaces defined in the spec, and thus should not be trimmed.
    TrimLeadingAndTrailingWhitespaces(aState, aItems);
  }

  FCItemIterator iter(aItems);
  nsIContent* parentContent = aParentFrame->GetContent();
  ComputedStyle* parentStyle = aParentFrame->Style();
  while (!iter.IsDone()) {
    if (!iter.SkipItemsWantingParentType(ourParentType)) {
      if (ourParentType == eTypeRuby) {
        WrapItemsInPseudoRubyLevelContainer(aState, iter, parentStyle,
                                            parentContent);
      } else {
        WrapItemsInPseudoRubyLeafBox(iter, parentStyle, parentContent);
      }
    }
  }
}

/*
 * This function works as follows: we walk through the child list (aItems) and
 * find items that cannot have aParentFrame as their parent.  We wrap
 * continuous runs of such items into a FrameConstructionItem for a frame that
 * gets them closer to their desired parents.  For example, a run of non-row
 * children of a row-group will get wrapped in a row.  When we later construct
 * the frame for this wrapper (in this case for the row), it'll be the correct
 * parent for the cells in the set of items we wrapped or we'll wrap cells
 * around everything else.  At the end of this method, aItems is guaranteed to
 * contain only items for frames that can be direct kids of aParentFrame.
 */
void nsCSSFrameConstructor::CreateNeededPseudoContainers(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
    nsIFrame* aParentFrame) {
  ParentType ourParentType = GetParentType(aParentFrame);
  if (IsRubyParentType(ourParentType) ||
      aItems.AllWantParentType(ourParentType)) {
    // Nothing to do here
    return;
  }

  FCItemIterator iter(aItems);
  do {
    if (iter.SkipItemsWantingParentType(ourParentType)) {
      // Nothing else to do here; we're finished
      return;
    }

    // Now we're pointing to the first child that wants a different parent
    // type.

    // Now try to figure out what kids we can group together.  We can generally
    // group everything that has a different desired parent type from us.  Two
    // exceptions to this:
    // 1) If our parent type is table, we can't group columns with anything
    //    else other than whitespace.
    // 2) Whitespace that lies between two things we can group which both want
    //    a non-block parent should be dropped, even if we can't group them
    //    with each other and even if the whitespace wants a parent of
    //    ourParentType.  Ends of the list count as things that don't want a
    //    block parent (so that for example we'll drop a whitespace-only list).

    FCItemIterator endIter(iter); /* iterator to find the end of the group */
    ParentType groupingParentType = endIter.item().DesiredParentType();
    if (aItems.AllWantParentType(groupingParentType) &&
        groupingParentType != eTypeBlock) {
      // Just group them all and be done with it.  We need the check for
      // eTypeBlock here to catch the "all the items are whitespace" case
      // described above.
      endIter.SetToEnd();
    } else {
      // Locate the end of the group.

      // Keep track of the type the previous item wanted, in case we have to
      // deal with whitespace.  Start it off with ourParentType, since that's
      // the last thing |iter| would have skipped over.
      ParentType prevParentType = ourParentType;
      do {
        // Walk an iterator past any whitespace that we might be able to drop
        // from the list
        FCItemIterator spaceEndIter(endIter);
        if (prevParentType != eTypeBlock &&
            !aParentFrame->IsGeneratedContentFrame() &&
            spaceEndIter.item().IsWhitespace(aState)) {
          bool trailingSpaces = spaceEndIter.SkipWhitespace(aState);

          // We drop the whitespace in the following cases:
          // 1) If these are not trailing spaces and the next item wants a table
          //    or table-part parent
          // 2) If these are trailing spaces and aParentFrame is a
          //    tabular container according to rule 1.3 of CSS 2.1 Sec 17.2.1.
          //    (Being a tabular container pretty much means ourParentType is
          //    not eTypeBlock besides the eTypeColGroup case, which won't
          //    reach here.)
          if ((!trailingSpaces &&
               IsTableParentType(spaceEndIter.item().DesiredParentType())) ||
              (trailingSpaces && ourParentType != eTypeBlock)) {
            bool updateStart = (iter == endIter);
            endIter.DeleteItemsTo(this, spaceEndIter);
            NS_ASSERTION(trailingSpaces == endIter.IsDone(),
                         "These should match");

            if (updateStart) {
              iter = endIter;
            }

            if (trailingSpaces) {
              break; /* Found group end */
            }

            if (updateStart) {
              // Update groupingParentType, since it might have been eTypeBlock
              // just because of the whitespace.
              groupingParentType = iter.item().DesiredParentType();
            }
          }
        }

        // Now endIter points to a non-whitespace item or a non-droppable
        // whitespace item. In the latter case, if this is the end of the group
        // we'll traverse this whitespace again.  But it'll all just be quick
        // DesiredParentType() checks which will match ourParentType (that's
        // what it means that this is the group end), so it's OK.
        // However, when we are grouping a ruby parent, and endIter points to
        // a non-droppable whitespace, if the next non-whitespace item also
        // wants a ruby parent, the whitespace should also be included into
        // the current ruby container.
        prevParentType = endIter.item().DesiredParentType();
        if (prevParentType == ourParentType &&
            (endIter == spaceEndIter || spaceEndIter.IsDone() ||
             !IsRubyParentType(groupingParentType) ||
             !IsRubyParentType(spaceEndIter.item().DesiredParentType()))) {
          // End the group at endIter.
          break;
        }

        if (ourParentType == eTypeTable &&
            (prevParentType == eTypeColGroup) !=
                (groupingParentType == eTypeColGroup)) {
          // Either we started with columns and now found something else, or
          // vice versa.  In any case, end the grouping.
          break;
        }

        // If we have some whitespace that we were not able to drop and there is
        // an item after the whitespace that is already properly parented, then
        // make sure to include the spaces in our group but stop the group after
        // that.
        if (spaceEndIter != endIter && !spaceEndIter.IsDone() &&
            ourParentType == spaceEndIter.item().DesiredParentType()) {
          endIter = spaceEndIter;
          break;
        }

        // Include the whitespace we didn't drop (if any) in the group.
        endIter = spaceEndIter;
        prevParentType = endIter.item().DesiredParentType();

        endIter.Next();
      } while (!endIter.IsDone());
    }

    if (iter == endIter) {
      // Nothing to wrap here; just skipped some whitespace
      continue;
    }

    if (ourParentType == eTypeColGroup) {
      // Suppress non-col items of colgroups.
      iter.DeleteItemsTo(this, endIter);
      continue;
    }

    // Now group together all the items between iter and endIter.  The right
    // parent type to use depends on ourParentType.
    ParentType wrapperType;
    switch (ourParentType) {
      case eTypeRow:
        // The parent type for a cell is eTypeBlock, since that's what a cell
        // looks like to its kids.
        wrapperType = eTypeBlock;
        break;
      case eTypeRowGroup:
        wrapperType = eTypeRow;
        break;
      case eTypeTable:
        // Either colgroup or rowgroup, depending on what we're grouping.
        wrapperType =
            groupingParentType == eTypeColGroup ? eTypeColGroup : eTypeRowGroup;
        break;
      case eTypeColGroup:
        MOZ_FALLTHROUGH_ASSERT("handled above");
      default:
        NS_ASSERTION(ourParentType == eTypeBlock, "Unrecognized parent type");
        if (IsRubyParentType(groupingParentType)) {
          wrapperType = eTypeRuby;
        } else {
          NS_ASSERTION(IsTableParentType(groupingParentType),
                       "groupingParentType should be either Ruby or table");
          wrapperType = eTypeTable;
        }
    }

    ComputedStyle* parentStyle = aParentFrame->Style();
    WrapItemsInPseudoParent(aParentFrame->GetContent(), parentStyle,
                            wrapperType, iter, endIter);

    // Now |iter| points to the item that was the first one we didn't wrap;
    // loop and see whether we need to skip it or wrap it in something
    // different.
  } while (!iter.IsDone());
}

/**
 * This method wraps frame construction item from |aIter| to
 * |aEndIter|. After it returns, aIter points to the first item
 * after the wrapper.
 */
void nsCSSFrameConstructor::WrapItemsInPseudoParent(
    nsIContent* aParentContent, ComputedStyle* aParentStyle,
    ParentType aWrapperType, FCItemIterator& aIter,
    const FCItemIterator& aEndIter) {
  const PseudoParentData& pseudoData = sPseudoParentData[aWrapperType];
  PseudoStyleType pseudoType = pseudoData.mPseudoType;
  auto& parentDisplay = *aParentStyle->StyleDisplay();
  auto parentDisplayInside = parentDisplay.DisplayInside();

  // XXXmats should we use IsInlineInsideStyle() here instead? seems odd to
  // exclude RubyBaseContainer/RubyTextContainer...
  if (pseudoType == PseudoStyleType::MozTable &&
      (parentDisplay.IsInlineFlow() ||
       parentDisplayInside == StyleDisplayInside::RubyBase ||
       parentDisplayInside == StyleDisplayInside::RubyText)) {
    pseudoType = PseudoStyleType::MozInlineTable;
  }

  RefPtr<ComputedStyle> wrapperStyle;
  if (pseudoData.mFCData.mBits & FCDATA_IS_WRAPPER_ANON_BOX) {
    wrapperStyle = mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
        pseudoType, aParentStyle);
  } else {
    wrapperStyle =
        mPresShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
            pseudoType);
  }

  // Use the content of our parent frame
  auto* newItem = new (this) FrameConstructionItem(
      &pseudoData.mFCData, aParentContent, wrapperStyle.forget(), true);

  const nsStyleDisplay* disp = newItem->mComputedStyle->StyleDisplay();
  // Here we're cheating a tad... technically, table-internal items should be
  // inline if aParentFrame is inline, but they'll get wrapped in an
  // inline-table in the end, so it'll all work out.  In any case, arguably
  // we don't need to maintain this state at this point... but it's better
  // to, I guess.
  newItem->mIsAllInline = disp->IsInlineOutsideStyle();

  bool isRuby = disp->IsRubyDisplayType();
  if (!isRuby) {
    // Table pseudo frames always induce line boundaries around their
    // contents.
    newItem->mChildItems.SetLineBoundaryAtStart(true);
    newItem->mChildItems.SetLineBoundaryAtEnd(true);
  }
  // The parent of the items in aItems is also the parent of the items
  // in mChildItems
  newItem->mChildItems.SetParentHasNoShadowDOM(
      aIter.List()->ParentHasNoShadowDOM());

  // Eat up all items between |aIter| and |aEndIter| and put them in our
  // wrapper Advances |aIter| to point to |aEndIter|.
  aIter.AppendItemsToList(this, aEndIter, newItem->mChildItems);

  aIter.InsertItem(newItem);
}

void nsCSSFrameConstructor::CreateNeededPseudoSiblings(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
    nsIFrame* aParentFrame) {
  if (aItems.IsEmpty() || GetParentType(aParentFrame) != eTypeRuby) {
    return;
  }

  FCItemIterator iter(aItems);
  StyleDisplay firstDisplay =
      iter.item().mComputedStyle->StyleDisplay()->mDisplay;
  if (firstDisplay == StyleDisplay::RubyBaseContainer) {
    return;
  }
  NS_ASSERTION(firstDisplay == StyleDisplay::RubyTextContainer,
               "Child of ruby frame should either a rbc or a rtc");

  const PseudoParentData& pseudoData =
      sPseudoParentData[eTypeRubyBaseContainer];
  RefPtr<ComputedStyle> pseudoStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          pseudoData.mPseudoType, aParentFrame->Style());
  FrameConstructionItem* newItem = new (this) FrameConstructionItem(
      &pseudoData.mFCData,
      // Use the content of the parent frame
      aParentFrame->GetContent(), pseudoStyle.forget(), true);
  newItem->mIsAllInline = true;
  newItem->mChildItems.SetParentHasNoShadowDOM(true);
  iter.InsertItem(newItem);
}

#ifdef DEBUG
/**
 * Returns true iff aFrame should be wrapped in an anonymous flex/grid item,
 * rather than being a direct child of aContainerFrame.
 *
 * NOTE: aContainerFrame must be a flex or grid container - this function is
 * purely for sanity-checking the children of these container types.
 * NOTE: See also NeedsAnonFlexOrGridItem(), for the non-debug version of this
 * logic (which operates a bit earlier, on FCData instead of frames).
 */
static bool FrameWantsToBeInAnonymousItem(const nsIFrame* aContainerFrame,
                                          const nsIFrame* aFrame) {
  MOZ_ASSERT(aContainerFrame->IsFlexOrGridContainer());

  // Any line-participant frames (e.g. text) definitely want to be wrapped in
  // an anonymous flex/grid item.
  if (aFrame->IsLineParticipant()) {
    return true;
  }

  // If the container is a -webkit-{inline-}box container, then placeholders
  // also need to be wrapped, for compatibility.
  if (IsFlexContainerForLegacyWebKitBox(aContainerFrame) &&
      aFrame->IsPlaceholderFrame()) {
    return true;
  }

  return false;
}
#endif

static void VerifyGridFlexContainerChildren(nsIFrame* aParentFrame,
                                            const nsFrameList& aChildren) {
#ifdef DEBUG
  if (!aParentFrame->IsFlexOrGridContainer()) {
    return;
  }

  bool prevChildWasAnonItem = false;
  for (const nsIFrame* child : aChildren) {
    MOZ_ASSERT(!FrameWantsToBeInAnonymousItem(aParentFrame, child),
               "frame wants to be inside an anonymous item, but it isn't");
    if (IsAnonymousItem(child)) {
      AssertAnonymousFlexOrGridItemParent(child, aParentFrame);
      MOZ_ASSERT(!prevChildWasAnonItem, "two anon items in a row");
      nsIFrame* firstWrappedChild = child->PrincipalChildList().FirstChild();
      MOZ_ASSERT(firstWrappedChild, "anonymous item shouldn't be empty");
      prevChildWasAnonItem = true;
    } else {
      prevChildWasAnonItem = false;
    }
  }
#endif
}

static bool FrameHasOnlyPlaceholderPrevSiblings(const nsIFrame* aFrame) {
  // Check for prev siblings, ignoring placeholder frames.
  MOZ_ASSERT(aFrame, "frame must not be null");
  const nsIFrame* prevSibling = aFrame;
  do {
    prevSibling = prevSibling->GetPrevSibling();
  } while (prevSibling && prevSibling->IsPlaceholderFrame());
  return !prevSibling;
}

static bool FrameHasOnlyPlaceholderNextSiblings(const nsIFrame* aFrame) {
  // Check for next siblings, ignoring placeholder frames.
  MOZ_ASSERT(aFrame, "frame must not be null");
  const nsIFrame* nextSibling = aFrame;
  do {
    nextSibling = nextSibling->GetNextSibling();
  } while (nextSibling && nextSibling->IsPlaceholderFrame());
  return !nextSibling;
}

static void SetPageValues(nsIFrame* const aFrame,
                          const nsAtom* const aAutoValue,
                          const nsAtom* const aStartValue,
                          const nsAtom* const aEndValue) {
  MOZ_ASSERT(aAutoValue, "Auto page value should never be null");
  MOZ_ASSERT(aStartValue || aEndValue, "Should not have called with no values");
  nsIFrame::PageValues* pageValues =
      aFrame->GetProperty(nsIFrame::PageValuesProperty());

  if (aStartValue) {
    if (aStartValue == aAutoValue) {
      // If the page value struct already exists, set the start value to null
      // to indicate the auto value.
      if (pageValues) {
        pageValues->mStartPageValue = nullptr;
      }
    } else {
      // The start value is not auto, so we need to store it, creating the
      // page values struct if it does not already exist.
      if (!pageValues) {
        pageValues = new nsIFrame::PageValues();
        aFrame->SetProperty(nsIFrame::PageValuesProperty(), pageValues);
      }
      pageValues->mStartPageValue = aStartValue;
    }
  }
  if (aEndValue) {
    if (aEndValue == aAutoValue) {
      // If the page value struct already exists, set the end value to null
      // to indicate the auto value.
      if (pageValues) {
        pageValues->mEndPageValue = nullptr;
      }
    } else {
      // The end value is not auto, so we need to store it, creating the
      // page values struct if it does not already exist.
      if (!pageValues) {
        pageValues = new nsIFrame::PageValues();
        aFrame->SetProperty(nsIFrame::PageValuesProperty(), pageValues);
      }
      pageValues->mEndPageValue = aEndValue;
    }
  }
}

inline void nsCSSFrameConstructor::ConstructFramesFromItemList(
    nsFrameConstructorState& aState, FrameConstructionItemList& aItems,
    nsContainerFrame* aParentFrame, bool aParentIsWrapperAnonBox,
    nsFrameList& aFrameList) {
#ifdef DEBUG
  // The assertion condition should match the logic in
  // MaybePushFloatContainingBlock().
  MOZ_ASSERT(!(ShouldSuppressFloatingOfDescendants(aParentFrame) ||
               aParentFrame->IsFloatContainingBlock()) ||
                 aState.mFloatCBCandidate == aParentFrame,
             "Our caller or ProcessChildren()'s caller should call "
             "MaybePushFloatContainingBlock() to handle the float containing "
             "block candidate!");
  aState.mFloatCBCandidate = nullptr;
#endif

  // Ensure aParentIsWrapperAnonBox is correct.  We _could_ compute it directly,
  // but it would be a bit slow, which is why we pass it from callers, who have
  // that information offhand in many cases.
  MOZ_ASSERT(ParentIsWrapperAnonBox(aParentFrame) == aParentIsWrapperAnonBox);

  // Note: we explicitly exclude TableColGroupFrame because it doesn't
  // have the FCDATA_IS_WRAPPER_ANON_BOX on pseudos so aParentIsWrapperAnonBox
  // is false for such pseudos (see sPseudoParentData below).
  if (!aParentIsWrapperAnonBox && aState.mHasRenderedLegend &&
      aParentFrame->GetContent()->IsHTMLElement(nsGkAtoms::fieldset) &&
      !aParentFrame->IsTableColGroupFrame()) {
    DebugOnly<bool> found = false;
    for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
      if (iter.item().mIsRenderedLegend) {
        // This makes the rendered legend the first frame in the fieldset child
        // list which makes keyboard traversal follow the visual order.
        nsFieldSetFrame* fieldSetFrame = GetFieldSetFrameFor(aParentFrame);
        nsFrameList renderedLegend;
        ConstructFramesFromItem(aState, iter, fieldSetFrame, renderedLegend);
        MOZ_ASSERT(renderedLegend.OnlyChild(),
                   "a rendered legend should have exactly one frame");
        fieldSetFrame->InsertFrames(FrameChildListID::Principal, nullptr,
                                    nullptr, std::move(renderedLegend));
        FCItemIterator next = iter;
        next.Next();
        iter.DeleteItemsTo(this, next);
        found = true;
        break;
      }
    }
    MOZ_ASSERT(found, "should have found our rendered legend");
  }

  CreateNeededPseudoContainers(aState, aItems, aParentFrame);
  CreateNeededAnonFlexOrGridItems(aState, aItems, aParentFrame);
  CreateNeededPseudoInternalRubyBoxes(aState, aItems, aParentFrame);
  CreateNeededPseudoSiblings(aState, aItems, aParentFrame);

  for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
    MOZ_ASSERT(!iter.item().mIsRenderedLegend,
               "Only one item can be the rendered legend, "
               "and it should've been handled above");
    NS_ASSERTION(iter.item().DesiredParentType() == GetParentType(aParentFrame),
                 "Needed pseudos didn't get created; expect bad things");
    ConstructFramesFromItem(aState, iter, aParentFrame, aFrameList);
  }

  VerifyGridFlexContainerChildren(aParentFrame, aFrameList);

  // Calculate and propagate page-name values for each frame in the frame list.
  // We do not want to compute and propagate page-name values from frames that
  // are children of any subclasses of block frames, but not actually a block
  // frame. The page-name property does not apply to frames which cannot create
  // class A breakpoints (currently no subclass of BlockFrame can). Because the
  // property does not apply, those children also cannot propagate page-name
  // values.
  // This assumption helps avoid unnecessarily handling page-names for frames
  // such as form controls, which also avoids bug 1819468.
  if (aState.mPresContext->IsPaginated() && aParentFrame->IsBlockFrame()) {
    // Set the start/end page values while iterating the frame list, to walk
    // up the frame tree only once after iterating the frame list.
    // This also avoids extra property lookups on these frames.
    MOZ_ASSERT(aState.mAutoPageNameValue == aParentFrame->GetAutoPageValue(),
               "aState.mAutoPageNameValue should have been equivalent to "
               "the auto value stored on our parent frame.");
    // Even though we store null for page values that equal the "auto" resolved
    // value on frames, we always want startPageValue/endPageValue to be the
    // actual atoms reflecting the start/end values. This is because when we
    // propagate the values up the frame tree, we will need to compare them to
    // the auto value for each ancestor. This value might be different than the
    // auto value for this frame.
    const nsAtom* startPageValue = nullptr;
    const nsAtom* endPageValue = nullptr;
    for (nsIFrame* f : aFrameList) {
      if (f->IsPlaceholderFrame()) {
        continue;
      }
      // Resolve auto against the parent frame's used page name, which has been
      // determined and set on aState.mAutoPageNameValue. If this item is not
      // block-level then we use the value that auto resolves to.
      //
      // This is to achieve the propagation behavior described in the spec:
      //
      // "A start page value and end page value is determined for each box as
      //  the value (if any) propagated from its first or last child box
      //  (respectively), else the used value on the box itself."
      //
      // "A child propagates its own start or end page value if and only if the
      //  page property applies to it."
      //
      // The page property only applies to "boxes that create class A break
      // points". When taken together, this means that non block-level children
      // do not propagate start/end page values, and instead we use "the used
      // value on the box itself", the "box itself" being aParentFrame. This
      // value has been determined and saved as aState.mAutoPageNameValue
      //
      // https://www.w3.org/TR/css-page-3/#using-named-pages
      // https://www.w3.org/TR/css-break-3/#btw-blocks
      const StylePageName& pageName = f->StylePage()->mPage;
      const nsAtom* const pageNameAtom =
          (pageName.IsPageName() && f->IsBlockOutside())
              ? pageName.AsPageName().AsAtom()
              : aState.mAutoPageNameValue;
      nsIFrame::PageValues* pageValues =
          f->GetProperty(nsIFrame::PageValuesProperty());
      // If this frame has any children, it will already have had its page
      // values set at this point. However, if no page values have been set,
      // we must ensure that the appropriate PageValuesProperty value has been
      // set.
      // If the page name is equal to the auto value, then PageValuesProperty
      // should remain null to indicate that the start/end values are both
      // equal to the auto value.
      if (pageNameAtom != aState.mAutoPageNameValue && !pageValues) {
        pageValues = new nsIFrame::PageValues{pageNameAtom, pageNameAtom};
        f->SetProperty(nsIFrame::PageValuesProperty(), pageValues);
      }
      // We don't want to use GetStartPageValue() or GetEndPageValue(), as each
      // requires a property lookup which we can avoid here.
      if (!startPageValue) {
        startPageValue = (pageValues && pageValues->mStartPageValue)
                             ? pageValues->mStartPageValue.get()
                             : aState.mAutoPageNameValue;
      }
      endPageValue = (pageValues && pageValues->mEndPageValue)
                         ? pageValues->mEndPageValue.get()
                         : aState.mAutoPageNameValue;
      MOZ_ASSERT(startPageValue && endPageValue,
                 "Should have found start/end page value");
    }
    MOZ_ASSERT(!startPageValue == !endPageValue,
               "Should have set both or neither page values");
    if (startPageValue) {
      // Walk up the frame tree from our parent frame, propagating start and
      // end page values.
      // As we go, if we find that, for a frame, we are not contributing one of
      // the start/end page values, then our subtree will not contribute this
      // value from that frame onward. startPageValue/endPageValue are set to
      // null to indicate this.
      // Stop iterating when we are not contributing either start or end
      // values, when we hit the root frame (no parent), or when we find a
      // frame that is not a block frame.
      for (nsContainerFrame* ancestorFrame = aParentFrame;
           (startPageValue || endPageValue) && ancestorFrame &&
           ancestorFrame->IsBlockFrame();
           ancestorFrame = ancestorFrame->GetParent()) {
        MOZ_ASSERT(!ancestorFrame->GetPrevInFlow(),
                   "Should not have fragmentation yet");
        MOZ_ASSERT(ancestorFrame->mWasVisitedByAutoFrameConstructionPageName,
                   "Frame should have been visited by "
                   "AutoFrameConstructionPageName");
        {
          // Get what the auto value is, based on this frame's parent.
          // For the root frame, `auto` resolves to the empty atom.
          const nsContainerFrame* const parent = ancestorFrame->GetParent();
          const nsAtom* const parentAuto = MOZ_LIKELY(parent)
                                               ? parent->GetAutoPageValue()
                                               : nsGkAtoms::_empty;
          SetPageValues(ancestorFrame, parentAuto, startPageValue,
                        endPageValue);
        }
        // Once we stop contributing start/end values, we know there is a
        // sibling subtree that contributed that value to our shared parent
        // instead of our starting frame's subtree. This means once
        // startPageValue/endPageValue becomes null, indicating that we are no
        // longer contributing that page value, it should stay null and we no
        // longer need to check for siblings in that direction.
        if (startPageValue &&
            !FrameHasOnlyPlaceholderPrevSiblings(ancestorFrame)) {
          startPageValue = nullptr;
        }
        if (endPageValue &&
            !FrameHasOnlyPlaceholderNextSiblings(ancestorFrame)) {
          endPageValue = nullptr;
        }
      }
    }
  }

  if (aParentIsWrapperAnonBox) {
    for (nsIFrame* f : aFrameList) {
      f->SetParentIsWrapperAnonBox();
    }
  }
}

void nsCSSFrameConstructor::AddFCItemsForAnonymousContent(
    nsFrameConstructorState& aState, nsContainerFrame* aFrame,
    const nsTArray<nsIAnonymousContentCreator::ContentInfo>& aAnonymousItems,
    FrameConstructionItemList& aItemsToConstruct,
    const AutoFrameConstructionPageName&) {
  for (const auto& info : aAnonymousItems) {
    nsIContent* content = info.mContent;
    // Gecko-styled nodes should have no pending restyle flags.
    // Assert some things about this content
    MOZ_ASSERT(!(content->GetFlags() &
                 (NODE_DESCENDANTS_NEED_FRAMES | NODE_NEEDS_FRAME)),
               "Should not be marked as needing frames");
    MOZ_ASSERT(!content->GetPrimaryFrame(), "Should have no existing frame");
    MOZ_ASSERT(!content->IsComment() && !content->IsProcessingInstruction(),
               "Why is someone creating garbage anonymous content");

    // Make sure we eagerly performed the servo cascade when the anonymous
    // nodes were created.
    MOZ_ASSERT(!content->IsElement() || content->AsElement()->HasServoData());

    RefPtr<ComputedStyle> computedStyle = ResolveComputedStyle(content);

    AddFrameConstructionItemsInternal(aState, content, aFrame, true,
                                      computedStyle, {ItemFlag::AllowPageBreak},
                                      aItemsToConstruct);
  }
}

void nsCSSFrameConstructor::ProcessChildren(
    nsFrameConstructorState& aState, nsIContent* aContent,
    ComputedStyle* aComputedStyle, nsContainerFrame* aFrame,
    const bool aCanHaveGeneratedContent, nsFrameList& aFrameList,
    const bool aAllowBlockStyles, nsIFrame* aPossiblyLeafFrame) {
  MOZ_ASSERT(aFrame, "Must have parent frame here");
  MOZ_ASSERT(aFrame->GetContentInsertionFrame() == aFrame,
             "Parent frame in ProcessChildren should be its own "
             "content insertion frame");

  const uint32_t kMaxDepth = 2 * MAX_REFLOW_DEPTH;
  static_assert(kMaxDepth <= UINT16_MAX, "mCurrentDepth type is too narrow");
  AutoRestore<uint16_t> savedDepth(mCurrentDepth);
  if (mCurrentDepth != UINT16_MAX) {
    ++mCurrentDepth;
  }

  if (!aPossiblyLeafFrame) {
    aPossiblyLeafFrame = aFrame;
  }

  // XXXbz ideally, this would do all the pushing of various
  // containing blocks as needed, so callers don't have to do it...

  // Check that our parent frame is a block before allowing ::first-letter/line.
  // E.g. <button style="display:grid"> should not allow it.
  const bool allowFirstPseudos =
      aAllowBlockStyles && aFrame->IsBlockFrameOrSubclass();
  bool haveFirstLetterStyle = false, haveFirstLineStyle = false;
  if (allowFirstPseudos) {
    ShouldHaveSpecialBlockStyle(aContent, aComputedStyle, &haveFirstLetterStyle,
                                &haveFirstLineStyle);
  }

  AutoFrameConstructionItemList itemsToConstruct(this);
  AutoFrameConstructionPageName pageNameTracker(aState, aFrame);

  // If we have first-letter or first-line style then frames can get
  // moved around so don't set these flags.
  if (allowFirstPseudos && !haveFirstLetterStyle && !haveFirstLineStyle) {
    itemsToConstruct.SetLineBoundaryAtStart(true);
    itemsToConstruct.SetLineBoundaryAtEnd(true);
  }

  // Create any anonymous frames we need here.
  AutoTArray<nsIAnonymousContentCreator::ContentInfo, 4> anonymousItems;
  GetAnonymousContent(aContent, aPossiblyLeafFrame, anonymousItems);
#ifdef DEBUG
  for (auto& item : anonymousItems) {
    MOZ_ASSERT(item.mContent->IsRootOfNativeAnonymousSubtree(),
               "Content should know it's an anonymous subtree");
  }
#endif
  AddFCItemsForAnonymousContent(aState, aFrame, anonymousItems,
                                itemsToConstruct, pageNameTracker);

  // Generated content should have the same style parent as normal kids.
  //
  // Note that we don't use this style for looking up things like special
  // block styles because in some cases involving table pseudo-frames it has
  // nothing to do with the parent frame's desired behavior.
  auto* styleParentFrame =
      nsIFrame::CorrectStyleParentFrame(aFrame, PseudoStyleType::NotPseudo);
  ComputedStyle* parentStyle = styleParentFrame->Style();
  if (parentStyle->StyleDisplay()->mTopLayer == StyleTopLayer::Auto &&
      !aContent->IsInNativeAnonymousSubtree()) {
    CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                               *parentStyle, PseudoStyleType::Backdrop,
                               itemsToConstruct);
  }

  nsBlockFrame* listItem = nullptr;
  bool isOutsideMarker = false;
  if (!aPossiblyLeafFrame->IsLeaf()) {
    if (aCanHaveGeneratedContent) {
      if (parentStyle->StyleDisplay()->IsListItem() &&
          (listItem = do_QueryFrame(aFrame)) &&
          !styleParentFrame->IsFieldSetFrame()) {
        isOutsideMarker = parentStyle->StyleList()->mListStylePosition ==
                          StyleListStylePosition::Outside;
        ItemFlags extraFlags;
        if (isOutsideMarker) {
          extraFlags += ItemFlag::IsForOutsideMarker;
        }
        CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                                   *parentStyle, PseudoStyleType::Marker,
                                   itemsToConstruct, extraFlags);
      }
      if (aContent->IsHTMLElement(nsGkAtoms::option)) {
        CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                                   *parentStyle, PseudoStyleType::Checkmark,
                                   itemsToConstruct);
      }
      // Probe for generated content before
      CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                                 *parentStyle, PseudoStyleType::Before,
                                 itemsToConstruct);
    }

    const bool addChildItems = MOZ_LIKELY(mCurrentDepth < kMaxDepth);
    if (!addChildItems) {
      NS_WARNING("ProcessChildren max depth exceeded");
    }

    FlattenedChildIterator iter(aContent);
    const InsertionPoint insertion(aFrame, aContent);
    for (nsIContent* child = iter.GetNextChild(); child;
         child = iter.GetNextChild()) {
      MOZ_ASSERT(insertion.mContainer == GetInsertionPoint(child).mContainer,
                 "GetInsertionPoint should agree with us");
      if (addChildItems) {
        AddFrameConstructionItems(aState, child, iter.ShadowDOMInvolved(),
                                  *parentStyle, insertion, itemsToConstruct);
      } else {
        ClearLazyBits(child, child->GetNextSibling());
      }
    }
    itemsToConstruct.SetParentHasNoShadowDOM(!iter.ShadowDOMInvolved());

    if (aCanHaveGeneratedContent) {
      // Probe for generated content after
      CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                                 *parentStyle, PseudoStyleType::After,
                                 itemsToConstruct);
      if (aContent->IsHTMLElement(nsGkAtoms::select)) {
        CreateGeneratedContentItem(aState, aFrame, *aContent->AsElement(),
                                   *parentStyle, PseudoStyleType::PickerIcon,
                                   itemsToConstruct);
      }
    }
  } else {
    ClearLazyBits(aContent->GetFirstChild(), nullptr);
  }

  ConstructFramesFromItemList(aState, itemsToConstruct, aFrame,
                              /* aParentIsWrapperAnonBox = */ false,
                              aFrameList);

  if (listItem) {
    if (auto* markerFrame = nsLayoutUtils::GetMarkerFrame(aContent)) {
      for (auto* childFrame : aFrameList) {
        if (markerFrame == childFrame) {
          if (isOutsideMarker) {
            // SetMarkerFrameForListItem will add childFrame to the marker list.
            aFrameList.RemoveFrame(childFrame);
            auto* grandParent = listItem->GetParent()->GetParent();
            if (listItem->Style()->GetPseudoType() ==
                    PseudoStyleType::MozColumnContent &&
                grandParent && grandParent->IsColumnSetWrapperFrame()) {
              listItem = do_QueryFrame(grandParent);
              MOZ_ASSERT(listItem,
                         "ColumnSetWrapperFrame is expected to be "
                         "a nsBlockFrame subclass");
              childFrame->SetParent(listItem);
            }
          }
          listItem->SetMarkerFrameForListItem(childFrame);
          MOZ_ASSERT(listItem->HasOutsideMarker() == isOutsideMarker);
#ifdef ACCESSIBILITY
          if (nsAccessibilityService* accService = GetAccService()) {
            auto* marker = markerFrame->GetContent();
            accService->ContentRangeInserted(mPresShell, marker, nullptr);
          }
#endif
          break;
        }
      }
    }
  }

  if (haveFirstLetterStyle) {
    WrapFramesInFirstLetterFrame(aFrame, aFrameList);
  }
  if (haveFirstLineStyle) {
    WrapFramesInFirstLineFrame(aState, aContent, aFrame, nullptr, aFrameList);
  }
}

//----------------------------------------------------------------------

// Support for :first-line style

// Special routine to handle placing a list of frames into a block
// frame that has first-line style. The routine ensures that the first
// collection of inline frames end up in a first-line frame.
// NOTE: aState may have containing block information related to a
// different part of the frame tree than where the first line occurs.
// In particular aState may be set up for where ContentInserted or
// ContentAppended is inserting content, which may be some
// non-first-in-flow continuation of the block to which the first-line
// belongs. So this function needs to be careful about how it uses
// aState.
void nsCSSFrameConstructor::WrapFramesInFirstLineFrame(
    nsFrameConstructorState& aState, nsIContent* aBlockContent,
    nsContainerFrame* aBlockFrame, nsFirstLineFrame* aLineFrame,
    nsFrameList& aFrameList) {
  // Extract any initial inline frames from aFrameList so we can put them
  // in the first-line.
  nsFrameList firstLineChildren =
      aFrameList.Split([](nsIFrame* f) { return !f->IsInlineOutside(); });

  if (firstLineChildren.IsEmpty()) {
    // Nothing is supposed to go into the first-line; nothing to do
    return;
  }

  if (!aLineFrame) {
    // Create line frame
    ComputedStyle* parentStyle = nsIFrame::CorrectStyleParentFrame(
                                     aBlockFrame, PseudoStyleType::FirstLine)
                                     ->Style();
    RefPtr<ComputedStyle> firstLineStyle =
        GetFirstLineStyle(aBlockContent, parentStyle);

    aLineFrame = NS_NewFirstLineFrame(mPresShell, firstLineStyle);

    // Initialize the line frame
    InitAndRestoreFrame(aState, aBlockContent, aBlockFrame, aLineFrame);

    // The lineFrame will be the block's first child; the rest of the
    // frame list (after lastInlineFrame) will be the second and
    // subsequent children; insert lineFrame into aFrameList.
    aFrameList.InsertFrame(nullptr, nullptr, aLineFrame);

    NS_ASSERTION(aLineFrame->Style() == firstLineStyle,
                 "Bogus style on line frame");
  }

  // Give the inline frames to the lineFrame <b>after</b> reparenting them
  ReparentFrames(this, aLineFrame, firstLineChildren, true);
  if (aLineFrame->PrincipalChildList().IsEmpty() &&
      aLineFrame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    aLineFrame->SetInitialChildList(FrameChildListID::Principal,
                                    std::move(firstLineChildren));
  } else {
    AppendFrames(aLineFrame, FrameChildListID::Principal,
                 std::move(firstLineChildren));
  }
}

// Special routine to handle appending a new frame to a block frame's
// child list. Takes care of placing the new frame into the right
// place when first-line style is present.
void nsCSSFrameConstructor::AppendFirstLineFrames(
    nsFrameConstructorState& aState, nsIContent* aBlockContent,
    nsContainerFrame* aBlockFrame, nsFrameList& aFrameList) {
  // It's possible that aBlockFrame needs to have a first-line frame
  // created because it doesn't currently have any children.
  const nsFrameList& blockKids = aBlockFrame->PrincipalChildList();
  if (blockKids.IsEmpty()) {
    WrapFramesInFirstLineFrame(aState, aBlockContent, aBlockFrame, nullptr,
                               aFrameList);
    return;
  }

  // Examine the last block child - if it's a first-line frame then
  // appended frames need special treatment.
  nsIFrame* lastBlockKid = blockKids.LastChild();
  if (!lastBlockKid->IsLineFrame()) {
    // No first-line frame at the end of the list, therefore there is
    // an intervening block between any first-line frame the frames
    // we are appending. Therefore, we don't need any special
    // treatment of the appended frames.
    return;
  }

  nsFirstLineFrame* lineFrame = static_cast<nsFirstLineFrame*>(lastBlockKid);
  WrapFramesInFirstLineFrame(aState, aBlockContent, aBlockFrame, lineFrame,
                             aFrameList);
}

void nsCSSFrameConstructor::CheckForFirstLineInsertion(
    nsIFrame* aParentFrame, nsFrameList& aFrameList) {
  MOZ_ASSERT(aParentFrame->Style()->IsInFirstLineSubtree(),
             "Why were we called?");

  if (aFrameList.IsEmpty()) {
    // Happens often enough, with the caption stuff.  No need to do the ancestor
    // walk here.
    return;
  }

  class RestyleManager* restyleManager = RestyleManager();

  // Check whether there's a ::first-line on the path up from aParentFrame.
  // Note that we can't stop until we've run out of ancestors with
  // pseudo-element data, because the first-letter might be somewhere way up the
  // tree; in particular it might be past our containing block.
  nsIFrame* ancestor = aParentFrame;
  while (ancestor) {
    if (!ancestor->Style()->IsInFirstLineSubtree()) {
      // We know we won't find a ::first-line now.
      return;
    }

    if (!ancestor->IsLineFrame()) {
      ancestor = ancestor->GetParent();
      continue;
    }

    if (!ancestor->Style()->IsPseudoElement()) {
      // This is a continuation lineframe, not the first line; no need to do
      // anything to the styles.
      return;
    }

    // Fix up the styles of aFrameList for ::first-line.
    for (nsIFrame* f : aFrameList) {
      restyleManager->ReparentComputedStyleForFirstLine(f);
    }
    return;
  }
}

//----------------------------------------------------------------------

// First-letter support

// Determine how many characters in the text fragment apply to the
// first letter
static int32_t FirstLetterCount(
    const CharacterDataBuffer* aCharacterDataBuffer) {
  int32_t count = 0;
  int32_t firstLetterLength = 0;

  const uint32_t n = aCharacterDataBuffer->GetLength();
  for (uint32_t i = 0; i < n; i++) {
    const char16_t ch = aCharacterDataBuffer->CharAt(i);
    // FIXME: take content language into account when deciding whitespace.
    if (dom::IsSpaceCharacter(ch)) {
      if (firstLetterLength) {
        break;
      }
      count++;
      continue;
    }
    // XXX I18n
    if ((ch == '\'') || (ch == '\"')) {
      if (firstLetterLength) {
        break;
      }
      // keep looping
      firstLetterLength = 1;
    } else {
      count++;
      break;
    }
  }

  return count;
}

static bool NeedFirstLetterContinuation(Text* aText) {
  MOZ_ASSERT(aText, "null ptr");
  int32_t flc = FirstLetterCount(&aText->DataBuffer());
  int32_t tl = aText->TextDataLength();
  return flc < tl;
}

static bool IsFirstLetterContent(Text* aText) {
  return aText->TextDataLength() && !aText->TextIsOnlyWhitespace();
}

/**
 * Create a letter frame, only make it a floating frame.
 */
nsFirstLetterFrame* nsCSSFrameConstructor::CreateFloatingLetterFrame(
    nsFrameConstructorState& aState, Text* aTextContent, nsIFrame* aTextFrame,
    nsContainerFrame* aParentFrame, ComputedStyle* aParentStyle,
    ComputedStyle* aComputedStyle, nsFrameList& aResult) {
  MOZ_ASSERT(aParentStyle);

  nsFirstLetterFrame* letterFrame =
      NS_NewFloatingFirstLetterFrame(mPresShell, aComputedStyle);
  // We don't want to use a text content for a non-text frame (because we want
  // its primary frame to be a text frame).
  nsIContent* letterContent = aParentFrame->GetContent();
  nsContainerFrame* containingBlock =
      aState.GetGeometricParent(*aComputedStyle->StyleDisplay(), aParentFrame);
  InitAndRestoreFrame(aState, letterContent, containingBlock, letterFrame);

  // Init the text frame to refer to the letter frame.
  //
  // Make sure we get a proper style for it (the one passed in is for the letter
  // frame and will have the float property set on it; the text frame shouldn't
  // have that set).
  ServoStyleSet* styleSet = mPresShell->StyleSet();
  RefPtr<ComputedStyle> textSC =
      styleSet->ResolveStyleForText(aTextContent, aComputedStyle);
  aTextFrame->SetComputedStyleWithoutNotification(textSC);
  InitAndRestoreFrame(aState, aTextContent, letterFrame, aTextFrame);

  // And then give the text frame to the letter frame
  SetInitialSingleChild(letterFrame, aTextFrame);

  // See if we will need to continue the text frame (does it contain
  // more than just the first-letter text or not?) If it does, then we
  // create (in advance) a continuation frame for it.
  nsIFrame* nextTextFrame = nullptr;
  if (NeedFirstLetterContinuation(aTextContent)) {
    // Create continuation
    nextTextFrame = CreateContinuingFrame(aTextFrame, aParentFrame);
    RefPtr<ComputedStyle> newSC =
        styleSet->ResolveStyleForText(aTextContent, aParentStyle);
    nextTextFrame->SetComputedStyle(newSC);
  }

  NS_ASSERTION(aResult.IsEmpty(), "aResult should be an empty nsFrameList!");
  // Put the new float before any of the floats in the block we're doing
  // first-letter for, that is, before any floats whose parent is
  // containingBlock.
  nsIFrame* prevSibling = nullptr;
  for (nsIFrame* f : aState.mFloatedList) {
    if (f->GetParent() == containingBlock) {
      break;
    }
    prevSibling = f;
  }

  aState.AddChild(letterFrame, aResult, letterContent, aParentFrame, false,
                  true, true, prevSibling);

  if (nextTextFrame) {
    aResult.AppendFrame(nullptr, nextTextFrame);
  }

  return letterFrame;
}

/**
 * Create a new letter frame for aTextFrame. The letter frame will be
 * a child of aParentFrame.
 */
void nsCSSFrameConstructor::CreateLetterFrame(
    nsContainerFrame* aBlockFrame, nsContainerFrame* aBlockContinuation,
    Text* aTextContent, nsContainerFrame* aParentFrame, nsFrameList& aResult) {
  NS_ASSERTION(aBlockFrame->IsBlockFrameOrSubclass(), "Not a block frame?");

  // Get a ComputedStyle for the first-letter-frame.
  //
  // Keep this in sync with nsBlockFrame::UpdatePseudoElementStyles.
  nsIFrame* parentFrame = nsIFrame::CorrectStyleParentFrame(
      aParentFrame, PseudoStyleType::FirstLetter);

  ComputedStyle* parentComputedStyle = parentFrame->Style();
  ComputedStyle* parentComputedStyleIgnoringFirstLine = parentComputedStyle;
  if (parentFrame->IsLineFrame()) {
    parentComputedStyleIgnoringFirstLine =
        nsIFrame::CorrectStyleParentFrame(aBlockFrame,
                                          PseudoStyleType::FirstLetter)
            ->Style();
  }

  // Use content from containing block so that we can actually
  // find a matching style rule.
  nsIContent* blockContent = aBlockFrame->GetContent();

  // Create first-letter style rule, ignoring first line. If we already have a
  // first-line we'll reparent the style below.
  RefPtr<ComputedStyle> sc =
      GetFirstLetterStyle(blockContent, parentComputedStyleIgnoringFirstLine);

  if (sc) {
    if (parentComputedStyleIgnoringFirstLine != parentComputedStyle) {
      sc = mPresShell->StyleSet()->ReparentComputedStyle(
          sc, parentComputedStyle, parentComputedStyle,
          blockContent->AsElement());
    }

    RefPtr<ComputedStyle> textSC =
        mPresShell->StyleSet()->ResolveStyleForText(aTextContent, sc);

    // Create a new text frame (the original one will be discarded)
    // pass a temporary stylecontext, the correct one will be set
    // later.  Start off by unsetting the primary frame for
    // aTextContent, so it's no longer pointing to the to-be-destroyed
    // frame.
    // XXXbz it would be really nice to destroy the old frame _first_,
    // then create the new one, so we could avoid this hack.
    aTextContent->SetPrimaryFrame(nullptr);
    nsIFrame* textFrame = NS_NewTextFrame(mPresShell, textSC);

    NS_ASSERTION(aBlockContinuation == GetFloatContainingBlock(aParentFrame),
                 "Containing block is confused");
    nsFrameConstructorState state(
        mPresShell, GetAbsoluteContainingBlock(aParentFrame, FIXED_POS),
        GetAbsoluteContainingBlock(aParentFrame, ABS_POS), aBlockContinuation);

    // Create the right type of first-letter frame
    const nsStyleDisplay* display = sc->StyleDisplay();
    nsFirstLetterFrame* letterFrame;
    if (display->IsFloatingStyle() && !aParentFrame->IsInSVGTextSubtree()) {
      // Make a floating first-letter frame
      letterFrame = CreateFloatingLetterFrame(state, aTextContent, textFrame,
                                              aParentFrame, parentComputedStyle,
                                              sc, aResult);
    } else {
      // Make an inflow first-letter frame
      letterFrame = NS_NewFirstLetterFrame(mPresShell, sc);

      // Initialize the first-letter-frame.  We don't want to use a text
      // content for a non-text frame (because we want its primary frame to
      // be a text frame).
      nsIContent* letterContent = aParentFrame->GetContent();
      letterFrame->Init(letterContent, aParentFrame, nullptr);

      InitAndRestoreFrame(state, aTextContent, letterFrame, textFrame);

      SetInitialSingleChild(letterFrame, textFrame);
      aResult.Clear();
      aResult.AppendFrame(nullptr, letterFrame);
      NS_ASSERTION(!aBlockFrame->GetPrevContinuation(),
                   "should have the first continuation here");
      aBlockFrame->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD);
    }
    MOZ_ASSERT(
        !aBlockFrame->GetPrevContinuation(),
        "Setting up a first-letter frame on a non-first block continuation?");
    auto parent =
        static_cast<nsContainerFrame*>(aParentFrame->FirstContinuation());
    if (MOZ_UNLIKELY(parent->IsLineFrame())) {
      parent = static_cast<nsContainerFrame*>(
          parent->GetParent()->FirstContinuation());
    }
    parent->SetHasFirstLetterChild();
    aBlockFrame->SetProperty(nsContainerFrame::FirstLetterProperty(),
                             letterFrame);
    aTextContent->SetPrimaryFrame(textFrame);
  }
}

void nsCSSFrameConstructor::WrapFramesInFirstLetterFrame(
    nsContainerFrame* aBlockFrame, nsFrameList& aBlockFrames) {
  aBlockFrame->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE);

  nsContainerFrame* parentFrame = nullptr;
  nsIFrame* textFrame = nullptr;
  nsIFrame* prevFrame = nullptr;
  nsFrameList letterFrames;
  bool stopLooking = false;
  WrapFramesInFirstLetterFrame(
      aBlockFrame, aBlockFrame, aBlockFrame, aBlockFrames.FirstChild(),
      &parentFrame, &textFrame, &prevFrame, letterFrames, &stopLooking);
  if (!parentFrame) {
    return;
  }
  DestroyContext context(mPresShell);
  if (parentFrame == aBlockFrame) {
    // Take textFrame out of the block's frame list and substitute the
    // letter frame(s) instead.
    aBlockFrames.DestroyFrame(context, textFrame);
    aBlockFrames.InsertFrames(nullptr, prevFrame, std::move(letterFrames));
  } else {
    // Take the old textFrame out of the inline parent's child list
    RemoveFrame(context, FrameChildListID::Principal, textFrame);

    // Insert in the letter frame(s)
    parentFrame->InsertFrames(FrameChildListID::Principal, prevFrame, nullptr,
                              std::move(letterFrames));
  }
}

void nsCSSFrameConstructor::WrapFramesInFirstLetterFrame(
    nsContainerFrame* aBlockFrame, nsContainerFrame* aBlockContinuation,
    nsContainerFrame* aParentFrame, nsIFrame* aParentFrameList,
    nsContainerFrame** aModifiedParent, nsIFrame** aTextFrame,
    nsIFrame** aPrevFrame, nsFrameList& aLetterFrames, bool* aStopLooking) {
  nsIFrame* prevFrame = nullptr;
  nsIFrame* frame = aParentFrameList;

  // This loop attempts to implement "Finding the First Letter":
  // https://drafts.csswg.org/css-pseudo-4/#application-in-css
  // FIXME: we don't handle nested blocks correctly yet though (bug 214004)
  while (frame) {
    nsIFrame* nextFrame = frame->GetNextSibling();

    // Skip all ::markers and placeholders.
    if (frame->Style()->GetPseudoType() == PseudoStyleType::Marker ||
        frame->IsPlaceholderFrame()) {
      prevFrame = frame;
      frame = nextFrame;
      continue;
    }
    LayoutFrameType frameType = frame->Type();
    if (LayoutFrameType::Text == frameType) {
      // Wrap up first-letter content in a letter frame
      Text* textContent = frame->GetContent()->AsText();
      if (IsFirstLetterContent(textContent)) {
        // Create letter frame to wrap up the text
        CreateLetterFrame(aBlockFrame, aBlockContinuation, textContent,
                          aParentFrame, aLetterFrames);

        // Provide adjustment information for parent
        *aModifiedParent = aParentFrame;
        *aTextFrame = frame;
        *aPrevFrame = prevFrame;
        *aStopLooking = true;
        return;
      }
    } else if (IsInlineFrame(frame) && frameType != LayoutFrameType::Br) {
      nsIFrame* kids = frame->PrincipalChildList().FirstChild();
      WrapFramesInFirstLetterFrame(aBlockFrame, aBlockContinuation,
                                   static_cast<nsContainerFrame*>(frame), kids,
                                   aModifiedParent, aTextFrame, aPrevFrame,
                                   aLetterFrames, aStopLooking);
      if (*aStopLooking) {
        return;
      }
    } else {
      // This will stop us looking to create more letter frames. For
      // example, maybe the frame-type is "letterFrame" or
      // "placeholderFrame". This keeps us from creating extra letter
      // frames, and also prevents us from creating letter frames when
      // the first real content child of a block is not text (e.g. an
      // image, hr, etc.)
      *aStopLooking = true;
      break;
    }

    prevFrame = frame;
    frame = nextFrame;
  }
}

static nsIFrame* FindFirstLetterFrame(nsIFrame* aFrame,
                                      FrameChildListID aListID) {
  for (nsIFrame* f : aFrame->GetChildList(aListID)) {
    if (f->IsLetterFrame()) {
      return f;
    }
  }
  return nullptr;
}

static void ClearHasFirstLetterChildFrom(nsContainerFrame* aParentFrame) {
  MOZ_ASSERT(aParentFrame);
  auto* parent =
      static_cast<nsContainerFrame*>(aParentFrame->FirstContinuation());
  if (MOZ_UNLIKELY(parent->IsLineFrame())) {
    MOZ_ASSERT(!parent->HasFirstLetterChild());
    parent = static_cast<nsContainerFrame*>(
        parent->GetParent()->FirstContinuation());
  }
  MOZ_ASSERT(parent->HasFirstLetterChild());
  parent->ClearHasFirstLetterChild();
}

void nsCSSFrameConstructor::RemoveFloatingFirstLetterFrames(
    PresShell* aPresShell, nsIFrame* aBlockFrame) {
  // Look for the first letter frame on the FrameChildListID::Float, then
  // FrameChildListID::PushedFloats.
  nsIFrame* floatFrame =
      ::FindFirstLetterFrame(aBlockFrame, FrameChildListID::Float);
  if (!floatFrame) {
    floatFrame =
        ::FindFirstLetterFrame(aBlockFrame, FrameChildListID::PushedFloats);
    if (!floatFrame) {
      return;
    }
  }

  // Take the text frame away from the letter frame (so it isn't
  // destroyed when we destroy the letter frame).
  nsIFrame* textFrame = floatFrame->PrincipalChildList().FirstChild();
  if (!textFrame) {
    return;
  }

  // Discover the placeholder frame for the letter frame
  nsPlaceholderFrame* placeholderFrame = floatFrame->GetPlaceholderFrame();
  if (!placeholderFrame) {
    // Somethings really wrong
    return;
  }
  nsContainerFrame* parentFrame = placeholderFrame->GetParent();
  if (!parentFrame) {
    // Somethings really wrong
    return;
  }

  ClearHasFirstLetterChildFrom(parentFrame);

  // Create a new text frame with the right style that maps all of the content
  // that was previously part of the letter frame (and probably continued
  // elsewhere).
  ComputedStyle* parentSC = parentFrame->Style();
  nsIContent* textContent = textFrame->GetContent();
  if (!textContent) {
    return;
  }
  RefPtr<ComputedStyle> newSC =
      aPresShell->StyleSet()->ResolveStyleForText(textContent, parentSC);
  nsIFrame* newTextFrame = NS_NewTextFrame(aPresShell, newSC);
  newTextFrame->Init(textContent, parentFrame, nullptr);

  // Destroy the old text frame's continuations (the old text frame
  // will be destroyed when its letter frame is destroyed).
  nsIFrame* frameToDelete = textFrame->LastContinuation();
  DestroyContext context(mPresShell);
  while (frameToDelete != textFrame) {
    nsIFrame* nextFrameToDelete = frameToDelete->GetPrevContinuation();
    RemoveFrame(context, FrameChildListID::Principal, frameToDelete);
    frameToDelete = nextFrameToDelete;
  }

  nsIFrame* prevSibling = placeholderFrame->GetPrevSibling();

  // Now that everything is set...
#ifdef NOISY_FIRST_LETTER
  printf(
      "RemoveFloatingFirstLetterFrames: textContent=%p oldTextFrame=%p "
      "newTextFrame=%p\n",
      textContent.get(), textFrame, newTextFrame);
#endif

  // Remove placeholder frame and the float
  RemoveFrame(context, FrameChildListID::Principal, placeholderFrame);

  // Now that the old frames are gone, we can start pointing to our
  // new primary frame.
  textContent->SetPrimaryFrame(newTextFrame);

  // Wallpaper bug 822910.
  bool offsetsNeedFixing = prevSibling && prevSibling->IsTextFrame();
  if (offsetsNeedFixing) {
    prevSibling->AddStateBits(TEXT_OFFSETS_NEED_FIXING);
  }

  // Insert text frame in its place
  InsertFrames(parentFrame, FrameChildListID::Principal, prevSibling,
               nsFrameList(newTextFrame, newTextFrame));

  if (offsetsNeedFixing) {
    prevSibling->RemoveStateBits(TEXT_OFFSETS_NEED_FIXING);
  }
}

void nsCSSFrameConstructor::RemoveFirstLetterFrames(
    PresShell* aPresShell, nsContainerFrame* aFrame,
    nsContainerFrame* aBlockFrame, bool* aStopLooking) {
  nsIFrame* prevSibling = nullptr;
  nsIFrame* kid = aFrame->PrincipalChildList().FirstChild();

  while (kid) {
    if (kid->IsLetterFrame()) {
      ClearHasFirstLetterChildFrom(aFrame);
      nsIFrame* textFrame = kid->PrincipalChildList().FirstChild();
      if (!textFrame) {
        break;
      }

      // Create a new textframe
      ComputedStyle* parentSC = aFrame->Style();
      if (!parentSC) {
        break;
      }
      nsIContent* textContent = textFrame->GetContent();
      if (!textContent) {
        break;
      }
      RefPtr<ComputedStyle> newSC =
          aPresShell->StyleSet()->ResolveStyleForText(textContent, parentSC);
      textFrame = NS_NewTextFrame(aPresShell, newSC);
      textFrame->Init(textContent, aFrame, nullptr);

      DestroyContext context(mPresShell);

      // Next rip out the kid and replace it with the text frame
      RemoveFrame(context, FrameChildListID::Principal, kid);

      // Now that the old frames are gone, we can start pointing to our
      // new primary frame.
      textContent->SetPrimaryFrame(textFrame);

      // Wallpaper bug 822910.
      bool offsetsNeedFixing = prevSibling && prevSibling->IsTextFrame();
      if (offsetsNeedFixing) {
        prevSibling->AddStateBits(TEXT_OFFSETS_NEED_FIXING);
      }

      // Insert text frame in its place
      InsertFrames(aFrame, FrameChildListID::Principal, prevSibling,
                   nsFrameList(textFrame, textFrame));

      if (offsetsNeedFixing) {
        prevSibling->RemoveStateBits(TEXT_OFFSETS_NEED_FIXING);
      }

      *aStopLooking = true;
      NS_ASSERTION(!aBlockFrame->GetPrevContinuation(),
                   "should have the first continuation here");
      aBlockFrame->RemoveStateBits(NS_BLOCK_HAS_FIRST_LETTER_CHILD);
      break;
    }
    if (IsInlineFrame(kid)) {
      nsContainerFrame* kidAsContainerFrame = do_QueryFrame(kid);
      if (kidAsContainerFrame) {
        // Look inside child inline frame for the letter frame.
        RemoveFirstLetterFrames(aPresShell, kidAsContainerFrame, aBlockFrame,
                                aStopLooking);
        if (*aStopLooking) {
          break;
        }
      }
    }
    prevSibling = kid;
    kid = kid->GetNextSibling();
  }
}

void nsCSSFrameConstructor::RemoveLetterFrames(PresShell* aPresShell,
                                               nsContainerFrame* aBlockFrame) {
  aBlockFrame =
      static_cast<nsContainerFrame*>(aBlockFrame->FirstContinuation());
  aBlockFrame->RemoveProperty(nsContainerFrame::FirstLetterProperty());
  nsContainerFrame* continuation = aBlockFrame;

  bool stopLooking = false;
  do {
    RemoveFloatingFirstLetterFrames(aPresShell, continuation);
    RemoveFirstLetterFrames(aPresShell, continuation, aBlockFrame,
                            &stopLooking);
    if (stopLooking) {
      break;
    }
    continuation =
        static_cast<nsContainerFrame*>(continuation->GetNextContinuation());
  } while (continuation);
}

// Fixup the letter frame situation for the given block
void nsCSSFrameConstructor::RecoverLetterFrames(nsContainerFrame* aBlockFrame) {
  aBlockFrame =
      static_cast<nsContainerFrame*>(aBlockFrame->FirstContinuation());
  nsContainerFrame* continuation = aBlockFrame;

  nsContainerFrame* parentFrame = nullptr;
  nsIFrame* textFrame = nullptr;
  nsIFrame* prevFrame = nullptr;
  nsFrameList letterFrames;
  bool stopLooking = false;
  do {
    // XXX shouldn't this bit be set already (bug 408493), assert instead?
    continuation->AddStateBits(NS_BLOCK_HAS_FIRST_LETTER_STYLE);
    WrapFramesInFirstLetterFrame(
        aBlockFrame, continuation, continuation,
        continuation->PrincipalChildList().FirstChild(), &parentFrame,
        &textFrame, &prevFrame, letterFrames, &stopLooking);
    if (stopLooking) {
      break;
    }
    continuation =
        static_cast<nsContainerFrame*>(continuation->GetNextContinuation());
  } while (continuation);

  if (!parentFrame) {
    return;
  }
  // Take the old textFrame out of the parent's child list
  DestroyContext context(mPresShell);
  RemoveFrame(context, FrameChildListID::Principal, textFrame);

  // Insert in the letter frame(s)
  parentFrame->InsertFrames(FrameChildListID::Principal, prevFrame, nullptr,
                            std::move(letterFrames));
}

//----------------------------------------------------------------------

void nsCSSFrameConstructor::ConstructBlock(
    nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame, nsContainerFrame* aContentParentFrame,
    ComputedStyle* aComputedStyle, nsContainerFrame** aNewFrame,
    nsFrameList& aFrameList, nsIFrame* aPositionedFrameForAbsPosContainer) {
  // clang-format off
  //
  // If a block frame is in a multi-column subtree, its children may need to
  // be chopped into runs of blocks containing column-spans and runs of
  // blocks containing no column-spans. Each run containing column-spans
  // will be wrapped by an anonymous block. See CreateColumnSpanSiblings() for
  // the implementation.
  //
  // If a block frame is a multi-column container, its children will need to
  // be processed as above. Moreover, it creates a ColumnSetWrapperFrame as
  // its outermost frame, and its children which have no
  // -moz-column-span-wrapper pseudo will be wrapped in ColumnSetFrames. See
  // FinishBuildingColumns() for the implementation.
  //
  // The multi-column subtree maintains the following invariants:
  //
  // 1) All the frames have the frame state bit
  //    NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR set, except for top-level
  //    ColumnSetWrapperFrame and those children in the column-span subtrees.
  //
  // 2) The first and last frame under ColumnSetWrapperFrame are always
  //    ColumnSetFrame.
  //
  // 3) ColumnSetFrames are linked together as continuations.
  //
  // 4) Those column-span wrappers are *not* linked together with themselves nor
  //    with the original block frame. The continuation chain consists of the
  //    original block frame and the original block's continuations wrapping
  //    non-column-spans.
  //
  // For example, this HTML
  //  <div id="x" style="column-count: 2;">
  //    <div style="column-span: all">a</div>
  //    <div id="y">
  //      b
  //      <div style="column-span: all">c</div>
  //      <div style="column-span: all">d</div>
  //      e
  //    </div>
  //  </div>
  //  <div style="column-span: all">f</div>
  //
  //  yields the following frame tree.
  //
  // A) ColumnSetWrapper (original style)
  // B)   ColumnSet (-moz-column-set)   <-- always created by BeginBuildingColumns
  // C)     Block (-moz-column-content)
  // D)   Block (-moz-column-span-wrapper, created by x)
  // E)     Block (div)
  // F)       Text ("a")
  // G)   ColumnSet (-moz-column-set)
  // H)     Block (-moz-column-content, created by x)
  // I)       Block (div, y)
  // J)         Text ("b")
  // K)   Block (-moz-column-span-wrapper, created by x)
  // L)     Block (-moz-column-span-wrapper, created by y)
  // M)       Block (div, new BFC)
  // N)         Text ("c")
  // O)       Block (div, new BFC)
  // P)         Text ("d")
  // Q)   ColumnSet (-moz-column-set)
  // R)     Block (-moz-column-content, created by x)
  // S)       Block (div, y)
  // T)         Text ("e")
  // U) Block (div, new BFC)   <-- not in multi-column hierarchy
  // V)   Text ("f")
  //
  // ColumnSet linkage described in 3): B -> G -> Q
  //
  // Block linkage described in 4): C -> H -> R  and  I -> S
  //
  // clang-format on

  nsBlockFrame* blockFrame = do_QueryFrame(*aNewFrame);
  MOZ_ASSERT(blockFrame && blockFrame->IsBlockFrame(), "not a block frame?");

  // Create column hierarchy if necessary.
  const bool needsColumn =
      aComputedStyle->StyleColumn()->IsColumnContainerStyle() &&
      !aParentFrame->IsTextInputFrame();
  if (needsColumn) {
    *aNewFrame = BeginBuildingColumns(aState, aContent, aParentFrame,
                                      blockFrame, aComputedStyle);

    if (aPositionedFrameForAbsPosContainer == blockFrame) {
      aPositionedFrameForAbsPosContainer = *aNewFrame;
    }
  } else {
    // No need to create column hierarchy. Initialize block frame.
    blockFrame->SetComputedStyleWithoutNotification(aComputedStyle);
    InitAndRestoreFrame(aState, aContent, aParentFrame, blockFrame);
  }

  aState.AddChild(*aNewFrame, aFrameList, aContent,
                  aContentParentFrame ? aContentParentFrame : aParentFrame);
  if (!mRootElementFrame) {
    mRootElementFrame = *aNewFrame;
  }

  // We should make the outer frame be the absolute containing block,
  // if one is required. We have to do this because absolute
  // positioning must be computed with respect to the CSS dimensions
  // of the element, which are the dimensions of the outer block. But
  // we can't really do that because only blocks can have absolute
  // children. So use the block and try to compensate with hacks
  // in nsBlockFrame::CalculateContainingBlockSizeForAbsolutes.
  nsFrameConstructorSaveState absoluteSaveState;
  (*aNewFrame)->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (aPositionedFrameForAbsPosContainer) {
    aState.PushAbsoluteContainingBlock(
        *aNewFrame, aPositionedFrameForAbsPosContainer, absoluteSaveState);
  }

  nsFrameConstructorSaveState floatSaveState;
  aState.MaybePushFloatContainingBlock(blockFrame, floatSaveState);

  if (aParentFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) &&
      !ShouldSuppressColumnSpanDescendants(aParentFrame)) {
    blockFrame->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
  }

  // Process the child content
  nsFrameList childList;
  ProcessChildren(aState, aContent, aComputedStyle, blockFrame, true, childList,
                  true);

  if (!MayNeedToCreateColumnSpanSiblings(blockFrame, childList)) {
    // No need to create column-span siblings.
    blockFrame->SetInitialChildList(FrameChildListID::Principal,
                                    std::move(childList));
    return;
  }

  // Extract any initial non-column-span kids, and put them in block frame's
  // child list.
  nsFrameList initialNonColumnSpanKids =
      childList.Split([](nsIFrame* f) { return f->IsColumnSpan(); });
  blockFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(initialNonColumnSpanKids));

  if (childList.IsEmpty()) {
    // No more kids to process (there weren't any column-span kids).
    return;
  }

  nsFrameList columnSpanSiblings = CreateColumnSpanSiblings(
      aState, blockFrame, childList,
      // If we're constructing a column container, pass nullptr as
      // aPositionedFrame to forbid reparenting absolute/fixed positioned frames
      // to column contents or column-span wrappers.
      needsColumn ? nullptr : aPositionedFrameForAbsPosContainer);

  if (needsColumn) {
    // We're constructing a column container; need to finish building it.
    FinishBuildingColumns(aState, *aNewFrame, blockFrame, columnSpanSiblings);
  } else {
    // We're constructing a normal block which has column-span children in a
    // column hierarchy such as "x" in the following example.
    //
    // <div style="column-count: 2">
    //   <div id="x">
    //     <div>normal child</div>
    //     <div style="column-span">spanner</div>
    //   </div>
    // </div>
    aFrameList.AppendFrames(nullptr, std::move(columnSpanSiblings));
  }

  MOZ_ASSERT(columnSpanSiblings.IsEmpty(),
             "The column-span siblings should be moved to the proper place!");
}

nsBlockFrame* nsCSSFrameConstructor::BeginBuildingColumns(
    nsFrameConstructorState& aState, nsIContent* aContent,
    nsContainerFrame* aParentFrame, nsContainerFrame* aColumnContent,
    ComputedStyle* aComputedStyle) {
  MOZ_ASSERT(aColumnContent->IsBlockFrame(),
             "aColumnContent should be a block frame.");
  MOZ_ASSERT(aComputedStyle->StyleColumn()->IsColumnContainerStyle(),
             "No need to build a column hierarchy!");

  // The initial column hierarchy looks like this:
  //
  // ColumnSetWrapper (original style)
  //   ColumnSet (-moz-column-set)
  //     Block (-moz-column-content)
  //
  nsBlockFrame* columnSetWrapper = NS_NewColumnSetWrapperFrame(
      mPresShell, aComputedStyle, nsFrameState(NS_FRAME_OWNS_ANON_BOXES));
  InitAndRestoreFrame(aState, aContent, aParentFrame, columnSetWrapper);
  if (aParentFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) &&
      !ShouldSuppressColumnSpanDescendants(aParentFrame)) {
    columnSetWrapper->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
  }

  AutoFrameConstructionPageName pageNameTracker(aState, columnSetWrapper);
  RefPtr<ComputedStyle> columnSetStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozColumnSet, aComputedStyle);
  nsContainerFrame* columnSet = NS_NewColumnSetFrame(
      mPresShell, columnSetStyle, nsFrameState(NS_FRAME_OWNS_ANON_BOXES));
  InitAndRestoreFrame(aState, aContent, columnSetWrapper, columnSet);
  columnSet->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);

  RefPtr<ComputedStyle> blockStyle =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozColumnContent, columnSetStyle);
  aColumnContent->SetComputedStyleWithoutNotification(blockStyle);
  InitAndRestoreFrame(aState, aContent, columnSet, aColumnContent);
  aColumnContent->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);

  // Set up the parent-child chain.
  SetInitialSingleChild(columnSetWrapper, columnSet);
  SetInitialSingleChild(columnSet, aColumnContent);

  return columnSetWrapper;
}

void nsCSSFrameConstructor::FinishBuildingColumns(
    nsFrameConstructorState& aState, nsContainerFrame* aColumnSetWrapper,
    nsContainerFrame* aColumnContent, nsFrameList& aColumnContentSiblings) {
  nsContainerFrame* prevColumnSet = aColumnContent->GetParent();

  MOZ_ASSERT(prevColumnSet->IsColumnSetFrame() &&
                 prevColumnSet->GetParent() == aColumnSetWrapper,
             "Should have established column hierarchy!");

  // Tag the first ColumnSet to have column-span siblings so that the bit can
  // propagate to all the continuations. We don't want the last ColumnSet to
  // have this bit, so we will unset the bit for it at the end of this function.
  prevColumnSet->SetHasColumnSpanSiblings(true);

  nsFrameList finalList;
  while (aColumnContentSiblings.NotEmpty()) {
    nsIFrame* f = aColumnContentSiblings.RemoveFirstChild();
    if (f->IsColumnSpan()) {
      // Do nothing for column-span wrappers. Just move it to the final
      // items.
      finalList.AppendFrame(aColumnSetWrapper, f);
    } else {
      auto* continuingColumnSet = static_cast<nsContainerFrame*>(
          CreateContinuingFrame(prevColumnSet, aColumnSetWrapper, false));
      MOZ_ASSERT(continuingColumnSet->HasColumnSpanSiblings(),
                 "The bit should propagate to the next continuation!");

      f->SetParent(continuingColumnSet);
      SetInitialSingleChild(continuingColumnSet, f);
      finalList.AppendFrame(aColumnSetWrapper, continuingColumnSet);
      prevColumnSet = continuingColumnSet;
    }
  }

  // Unset the bit because the last ColumnSet has no column-span siblings.
  prevColumnSet->SetHasColumnSpanSiblings(false);

  aColumnSetWrapper->AppendFrames(FrameChildListID::Principal,
                                  std::move(finalList));
}

bool nsCSSFrameConstructor::MayNeedToCreateColumnSpanSiblings(
    nsContainerFrame* aBlockFrame, const nsFrameList& aChildList) {
  if (!aBlockFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
    // The block frame isn't in a multi-column block formatting context.
    return false;
  }

  if (ShouldSuppressColumnSpanDescendants(aBlockFrame)) {
    // No need to create column-span siblings for a frame that suppresses them.
    return false;
  }

  if (aChildList.IsEmpty()) {
    // No child needs to be processed.
    return false;
  }

  // Need to actually look into the child list.
  return true;
}

nsFrameList nsCSSFrameConstructor::CreateColumnSpanSiblings(
    nsFrameConstructorState& aState, nsContainerFrame* aInitialBlock,
    nsFrameList& aChildList, nsIFrame* aPositionedFrame) {
  MOZ_ASSERT(aInitialBlock->IsBlockFrameOrSubclass());
  MOZ_ASSERT(!aPositionedFrame || aPositionedFrame->IsAbsPosContainingBlock());

  nsIContent* const content = aInitialBlock->GetContent();
  nsContainerFrame* const parentFrame = aInitialBlock->GetParent();
  const bool isInitialBlockFloatCB = aInitialBlock->IsFloatContainingBlock();

  nsFrameList siblings;
  nsContainerFrame* lastNonColumnSpanWrapper = aInitialBlock;

  // Tag the first non-column-span wrapper to have column-span siblings so that
  // the bit can propagate to all the continuations. We don't want the last
  // wrapper to have this bit, so we will unset the bit for it at the end of
  // this function.
  lastNonColumnSpanWrapper->SetHasColumnSpanSiblings(true);
  do {
    MOZ_ASSERT(aChildList.NotEmpty(), "Why call this if child list is empty?");
    MOZ_ASSERT(aChildList.FirstChild()->IsColumnSpan(),
               "Must have the child starting with column-span!");

    // Grab the consecutive column-span kids, and reparent them into a
    // block frame.
    RefPtr<ComputedStyle> columnSpanWrapperStyle =
        mPresShell->StyleSet()->ResolveNonInheritingAnonymousBoxStyle(
            PseudoStyleType::MozColumnSpanWrapper);
    nsBlockFrame* columnSpanWrapper =
        NS_NewBlockFrame(mPresShell, columnSpanWrapperStyle);
    InitAndRestoreFrame(aState, content, parentFrame, columnSpanWrapper,
                        AllowCounters::No);
    columnSpanWrapper->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR |
                                    NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);

    nsFrameList columnSpanKids =
        aChildList.Split([](nsIFrame* f) { return !f->IsColumnSpan(); });
    columnSpanKids.ApplySetParent(columnSpanWrapper);
    columnSpanWrapper->SetInitialChildList(FrameChildListID::Principal,
                                           std::move(columnSpanKids));
    if (aPositionedFrame) {
      aState.ReparentAbsoluteItems(columnSpanWrapper);
    }

    siblings.AppendFrame(nullptr, columnSpanWrapper);

    // Grab the consecutive non-column-span kids, and reparent them into a new
    // continuation of the last non-column-span wrapper frame.
    auto* nonColumnSpanWrapper = static_cast<nsContainerFrame*>(
        CreateContinuingFrame(lastNonColumnSpanWrapper, parentFrame, false));
    nonColumnSpanWrapper->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR |
                                       NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
    MOZ_ASSERT(nonColumnSpanWrapper->HasColumnSpanSiblings(),
               "The bit should propagate to the next continuation!");

    if (aChildList.NotEmpty()) {
      nsFrameList nonColumnSpanKids =
          aChildList.Split([](nsIFrame* f) { return f->IsColumnSpan(); });

      nonColumnSpanKids.ApplySetParent(nonColumnSpanWrapper);
      nonColumnSpanWrapper->SetInitialChildList(FrameChildListID::Principal,
                                                std::move(nonColumnSpanKids));
      if (aPositionedFrame) {
        aState.ReparentAbsoluteItems(nonColumnSpanWrapper);
      }
      if (isInitialBlockFloatCB) {
        aState.ReparentFloats(nonColumnSpanWrapper);
      }
    }

    siblings.AppendFrame(nullptr, nonColumnSpanWrapper);

    lastNonColumnSpanWrapper = nonColumnSpanWrapper;
  } while (aChildList.NotEmpty());

  // Unset the bit because the last non-column-span wrapper has no column-span
  // siblings.
  lastNonColumnSpanWrapper->SetHasColumnSpanSiblings(false);

  return siblings;
}

bool nsCSSFrameConstructor::MaybeRecreateForColumnSpan(
    nsFrameConstructorState& aState, nsContainerFrame* aParentFrame,
    nsFrameList& aFrameList, nsIFrame* aPrevSibling) {
  if (!aParentFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
    return false;
  }

  if (aFrameList.IsEmpty()) {
    return false;
  }

  MOZ_ASSERT(!IsFramePartOfIBSplit(aParentFrame),
             "We should have wiped aParentFrame in "
             "WipeContainingBlock if it's part of IB split!");

  nsIFrame* nextSibling = ::GetInsertNextSibling(aParentFrame, aPrevSibling);
  if (!nextSibling && IsLastContinuationForColumnContent(aParentFrame)) {
    // We are appending a list of frames to the last continuation of a
    // ::-moz-column-content. This is the case where we can fix the frame tree
    // instead of reframing the containing block. Return false and let
    // AppendFramesToParent() deal with this.
    return false;
  }

  auto HasColumnSpan = [](const nsFrameList& aList) {
    for (nsIFrame* f : aList) {
      if (f->IsColumnSpan()) {
        return true;
      }
    }
    return false;
  };

  if (HasColumnSpan(aFrameList)) {
    // If any frame in the frame list has "column-span:all" style, i.e. a
    // -moz-column-span-wrapper frame, we need to reframe the multi-column
    // containing block.
    //
    // We can only be here if none of the new inserted nsIContent* nodes (via
    // ContentAppended or ContentRangeInserted) have column-span:all style, yet
    // some of them have column-span:all descendants. Sadly, there's no way to
    // detect this by checking FrameConstructionItems in WipeContainingBlock().
    // Otherwise, we would have already wiped the multi-column containing block.
    PROFILER_MARKER("Reframe multi-column after constructing frame list",
                    LAYOUT, {}, Tracing, "Layout");

    // aFrameList can contain placeholder frames. In order to destroy their
    // associated out-of-flow frames properly, we need to manually flush all the
    // out-of-flow frames in aState to their container frames.
    aState.ProcessFrameInsertionsForAllLists();
    DestroyContext context(mPresShell);
    aFrameList.DestroyFrames(context);
    RecreateFramesForContent(
        GetMultiColumnContainingBlockFor(aParentFrame)->GetContent(),
        InsertionKind::Async);
    return true;
  }

  return false;
}

nsIFrame* nsCSSFrameConstructor::ConstructInline(
    nsFrameConstructorState& aState, FrameConstructionItem& aItem,
    nsContainerFrame* aParentFrame, const nsStyleDisplay* aDisplay,
    nsFrameList& aFrameList) {
  // If an inline frame has non-inline kids, then we chop up the child list
  // into runs of blocks and runs of inlines, create anonymous block frames to
  // contain the runs of blocks, inline frames with our style for the runs of
  // inlines, and put all these frames, in order, into aFrameList.
  //
  // When there are column-span blocks in a run of blocks, instead of creating
  // an anonymous block to wrap them, we create multiple anonymous blocks,
  // wrapping runs of non-column-spans and runs of column-spans.
  //
  // We return the the first one.  The whole setup is called an {ib}
  // split; in what follows "frames in the split" refers to the anonymous blocks
  // and inlines that contain our children.
  //
  // {ib} splits maintain the following invariants:
  // 1) All frames in the split have the NS_FRAME_PART_OF_IBSPLIT bit
  //    set.
  //
  // 2) Each frame in the split has the nsIFrame::IBSplitSibling
  //    property pointing to the next frame in the split, except for the last
  //    one, which does not have it set.
  //
  // 3) Each frame in the split has the nsIFrame::IBSplitPrevSibling
  //    property pointing to the previous frame in the split, except for the
  //    first one, which does not have it set.
  //
  // 4) The first and last frame in the split are always inlines.
  //
  // 5) The frames wrapping runs of non-column-spans are linked together as
  //    continuations. The frames wrapping runs of column-spans are *not*
  //    linked with each other nor with other non-column-span wrappers.
  //
  // 6) The first and last frame in the chains of blocks are always wrapping
  //    non-column-spans. Both of them are created even if they're empty.
  //
  // An invariant that is NOT maintained is that the wrappers are actually
  // linked via GetNextSibling linkage.  A simple example is an inline
  // containing an inline that contains a block.  The three parts of the inner
  // inline end up with three different parents.
  //
  // For example, this HTML:
  // <span>
  //   <div>a</div>
  //   <span>
  //     b
  //     <div>c</div>
  //   </span>
  //   d
  //   <div>e</div>
  //   f
  //  </span>
  // Gives the following frame tree:
  //
  // Inline (outer span)
  // Block (anonymous, outer span)
  //   Block (div)
  //     Text("a")
  // Inline (outer span)
  //   Inline (inner span)
  //     Text("b")
  // Block (anonymous, outer span)
  //   Block (anonymous, inner span)
  //     Block (div)
  //       Text("c")
  // Inline (outer span)
  //   Inline (inner span)
  //   Text("d")
  // Block (anonymous, outer span)
  //   Block (div)
  //     Text("e")
  // Inline (outer span)
  //   Text("f")

  nsIContent* const content = aItem.mContent;
  ComputedStyle* const computedStyle = aItem.mComputedStyle;

  nsInlineFrame* newFrame = NS_NewInlineFrame(mPresShell, computedStyle);

  // Initialize the frame
  InitAndRestoreFrame(aState, content, aParentFrame, newFrame);

  // definition cannot be inside next block because the object's destructor is
  // significant. this is part of the fix for bug 42372
  nsFrameConstructorSaveState absoluteSaveState;

  bool isAbsPosCB = newFrame->IsAbsPosContainingBlock();
  newFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
  if (isAbsPosCB) {
    // Relatively positioned frames becomes a container for child
    // frames that are positioned
    aState.PushAbsoluteContainingBlock(newFrame, newFrame, absoluteSaveState);
  }

  if (aParentFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR) &&
      !ShouldSuppressColumnSpanDescendants(aParentFrame)) {
    newFrame->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
  }

  // Process the child content
  nsFrameList childList;
  ConstructFramesFromItemList(aState, aItem.mChildItems, newFrame,
                              /* aParentIsWrapperAnonBox = */ false, childList);

  nsIFrame* firstBlock = nullptr;
  if (!aItem.mIsAllInline) {
    for (nsIFrame* f : childList) {
      if (f->IsBlockOutside()) {
        firstBlock = f;
        break;
      }
    }
  }

  if (aItem.mIsAllInline || !firstBlock) {
    // This part is easy.  We either already know we have no non-inline kids,
    // or haven't found any when constructing actual frames (the latter can
    // happen only if out-of-flows that we thought had no containing block
    // acquired one when ancestor inline frames and {ib} splits got
    // constructed).  Just put all the kids into the single inline frame and
    // bail.
    newFrame->SetInitialChildList(FrameChildListID::Principal,
                                  std::move(childList));
    aState.AddChild(newFrame, aFrameList, content, aParentFrame);
    return newFrame;
  }

  // This inline frame contains several types of children. Therefore this frame
  // has to be chopped into several pieces, as described above.

  // Grab the first inline's kids
  nsFrameList firstInlineKids = childList.TakeFramesBefore(firstBlock);
  newFrame->SetInitialChildList(FrameChildListID::Principal,
                                std::move(firstInlineKids));

  aFrameList.AppendFrame(nullptr, newFrame);

  newFrame->AddStateBits(NS_FRAME_OWNS_ANON_BOXES);
  CreateIBSiblings(aState, newFrame, isAbsPosCB, childList, aFrameList);

  return newFrame;
}

void nsCSSFrameConstructor::CreateIBSiblings(nsFrameConstructorState& aState,
                                             nsContainerFrame* aInitialInline,
                                             bool aIsAbsPosCB,
                                             nsFrameList& aChildList,
                                             nsFrameList& aSiblings) {
  MOZ_ASSERT(aIsAbsPosCB == aInitialInline->IsAbsPosContainingBlock());

  nsIContent* content = aInitialInline->GetContent();
  ComputedStyle* computedStyle = aInitialInline->Style();
  nsContainerFrame* parentFrame = aInitialInline->GetParent();

  // Resolve the right style for our anonymous blocks.
  //
  // The distinction in styles is needed because of CSS 2.1, section
  // 9.2.1.1, which says:
  //
  //   When such an inline box is affected by relative positioning, any
  //   resulting translation also affects the block-level box contained
  //   in the inline box.
  RefPtr<ComputedStyle> blockSC =
      mPresShell->StyleSet()->ResolveInheritingAnonymousBoxStyle(
          PseudoStyleType::MozBlockInsideInlineWrapper, computedStyle);

  nsContainerFrame* lastNewInline =
      static_cast<nsContainerFrame*>(aInitialInline->FirstContinuation());
  do {
    // On entry to this loop aChildList is not empty and the first frame in it
    // is block-level.
    MOZ_ASSERT(aChildList.NotEmpty(), "Should have child items");
    MOZ_ASSERT(aChildList.FirstChild()->IsBlockOutside(),
               "Must have list starting with block");

    // The initial run of blocks belongs to an anonymous block that we create
    // right now. The anonymous block will be the parent of these block
    // children of the inline.
    nsBlockFrame* blockFrame = NS_NewBlockFrame(mPresShell, blockSC);
    InitAndRestoreFrame(aState, content, parentFrame, blockFrame,
                        AllowCounters::No);
    if (aInitialInline->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
      blockFrame->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
    }

    // Find the first non-block child which defines the end of our block kids
    // and the start of our next inline's kids
    nsFrameList blockKids =
        aChildList.Split([](nsIFrame* f) { return !f->IsBlockOutside(); });

    if (!aInitialInline->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
      MoveChildrenTo(aInitialInline, blockFrame, blockKids);

      SetFrameIsIBSplit(lastNewInline, blockFrame);
      aSiblings.AppendFrame(nullptr, blockFrame);
    } else {
      // Extract any initial non-column-span frames, and put them in
      // blockFrame's child list.
      nsFrameList initialNonColumnSpanKids =
          blockKids.Split([](nsIFrame* f) { return f->IsColumnSpan(); });
      MoveChildrenTo(aInitialInline, blockFrame, initialNonColumnSpanKids);

      SetFrameIsIBSplit(lastNewInline, blockFrame);
      aSiblings.AppendFrame(nullptr, blockFrame);

      if (blockKids.NotEmpty()) {
        // Although SetFrameIsIBSplit() will add NS_FRAME_PART_OF_IBSPLIT for
        // blockFrame later, we manually add the bit earlier here to make all
        // the continuations of blockFrame created in
        // CreateColumnSpanSiblings(), i.e. non-column-span wrappers, have the
        // bit via nsIFrame::Init().
        blockFrame->AddStateBits(NS_FRAME_PART_OF_IBSPLIT);

        nsFrameList columnSpanSiblings =
            CreateColumnSpanSiblings(aState, blockFrame, blockKids,
                                     aIsAbsPosCB ? aInitialInline : nullptr);
        aSiblings.AppendFrames(nullptr, std::move(columnSpanSiblings));
      }
    }

    // Now grab the initial inlines in aChildList and put them into an inline
    // frame.
    nsInlineFrame* inlineFrame = NS_NewInlineFrame(mPresShell, computedStyle);
    InitAndRestoreFrame(aState, content, parentFrame, inlineFrame,
                        AllowCounters::No);
    inlineFrame->AddStateBits(NS_FRAME_CAN_HAVE_ABSPOS_CHILDREN);
    if (aInitialInline->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
      inlineFrame->AddStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR);
    }

    if (aIsAbsPosCB) {
      inlineFrame->MarkAsAbsoluteContainingBlock();
    }

    if (aChildList.NotEmpty()) {
      nsFrameList inlineKids =
          aChildList.Split([](nsIFrame* f) { return f->IsBlockOutside(); });
      MoveChildrenTo(aInitialInline, inlineFrame, inlineKids);
    }

    SetFrameIsIBSplit(blockFrame, inlineFrame);
    aSiblings.AppendFrame(nullptr, inlineFrame);
    lastNewInline = inlineFrame;
  } while (aChildList.NotEmpty());

  SetFrameIsIBSplit(lastNewInline, nullptr);
}

void nsCSSFrameConstructor::BuildInlineChildItems(
    nsFrameConstructorState& aState, FrameConstructionItem& aParentItem,
    bool aItemIsWithinSVGText, bool aItemAllowsTextPathChild) {
  ComputedStyle* const parentStyle = aParentItem.mComputedStyle;
  nsIContent* const parentContent = aParentItem.mContent;

  if (!aItemIsWithinSVGText) {
    if (parentStyle->StyleDisplay()->IsListItem()) {
      CreateGeneratedContentItem(aState, nullptr, *parentContent->AsElement(),
                                 *parentStyle, PseudoStyleType::Marker,
                                 aParentItem.mChildItems);
    }
    // Probe for generated content before
    CreateGeneratedContentItem(aState, nullptr, *parentContent->AsElement(),
                               *parentStyle, PseudoStyleType::Before,
                               aParentItem.mChildItems);
  }

  ItemFlags flags;
  if (aItemIsWithinSVGText) {
    flags += ItemFlag::IsWithinSVGText;
  }
  if (aItemAllowsTextPathChild &&
      aParentItem.mContent->IsSVGElement(nsGkAtoms::a)) {
    flags += ItemFlag::AllowTextPathChild;
  }

  FlattenedChildIterator iter(parentContent);
  for (nsIContent* content = iter.GetNextChild(); content;
       content = iter.GetNextChild()) {
    AddFrameConstructionItems(aState, content, iter.ShadowDOMInvolved(),
                              *parentStyle, InsertionPoint(),
                              aParentItem.mChildItems, flags);
  }

  if (!aItemIsWithinSVGText) {
    // Probe for generated content after
    CreateGeneratedContentItem(aState, nullptr, *parentContent->AsElement(),
                               *parentStyle, PseudoStyleType::After,
                               aParentItem.mChildItems);
  }

  aParentItem.mIsAllInline = aParentItem.mChildItems.AreAllItemsInline();
}

// return whether it's ok to append (in the AppendFrames sense) to
// aParentFrame if our nextSibling is aNextSibling.  aParentFrame must
// be an ib-split inline.
static bool IsSafeToAppendToIBSplitInline(nsIFrame* aParentFrame,
                                          nsIFrame* aNextSibling) {
  MOZ_ASSERT(IsInlineFrame(aParentFrame), "Must have an inline parent here");

  do {
    NS_ASSERTION(IsFramePartOfIBSplit(aParentFrame),
                 "How is this not part of an ib-split?");
    if (aNextSibling || aParentFrame->GetNextContinuation() ||
        GetIBSplitSibling(aParentFrame)) {
      return false;
    }

    aNextSibling = aParentFrame->GetNextSibling();
    aParentFrame = aParentFrame->GetParent();
  } while (IsInlineFrame(aParentFrame));

  return true;
}

bool nsCSSFrameConstructor::WipeInsertionParent(nsContainerFrame* aFrame) {
#define TRACE(reason)                                                  \
  PROFILER_MARKER("WipeInsertionParent: " reason, LAYOUT, {}, Tracing, \
                  "Layout");

  const LayoutFrameType frameType = aFrame->Type();

  // FIXME(emilio): This looks terribly inefficient if you insert elements deep
  // in a MathML subtree.
  if (aFrame->IsMathMLFrame()) {
    TRACE("MathML");
    RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
    return true;
  }

  // A ruby-related frame that's getting new children.
  // The situation for ruby is complex, especially when interacting with
  // spaces. It contains these two special cases apart from tables:
  // 1) There are effectively three types of white spaces in ruby frames
  //    we handle differently: leading/tailing/inter-level space,
  //    inter-base/inter-annotation space, and inter-segment space.
  //    These three types of spaces can be converted to each other when
  //    their sibling changes.
  // 2) The first effective child of a ruby frame must always be a ruby
  //    base container. It should be created or destroyed accordingly.
  if (IsRubyPseudo(aFrame) || frameType == LayoutFrameType::Ruby ||
      RubyUtils::IsRubyContainerBox(frameType)) {
    // We want to optimize it better, and avoid reframing as much as
    // possible. But given the cases above, and the fact that a ruby
    // usually won't be very large, it should be fine to reframe it.
    TRACE("Ruby");
    RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
    return true;
  }

  // Reframe the multi-column container whenever elements insert/append
  // into it because we need to reconstruct column-span split.
  if (aFrame->IsColumnSetWrapperFrame()) {
    TRACE("Multi-column");
    RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
    return true;
  }

  return false;

#undef TRACE
}

bool nsCSSFrameConstructor::WipeContainingBlock(
    nsFrameConstructorState& aState, nsIFrame* aContainingBlock,
    nsIFrame* aFrame, FrameConstructionItemList& aItems, bool aIsAppend,
    nsIFrame* aPrevSibling) {
#define TRACE(reason)                                                  \
  PROFILER_MARKER("WipeContainingBlock: " reason, LAYOUT, {}, Tracing, \
                  "Layout");

  if (aItems.IsEmpty()) {
    return false;
  }

  // Before we go and append the frames, we must check for several
  // special situations.

  if (aFrame->GetContent() == mDocument->GetRootElement()) {
    // Situation #1 is when we insert content that becomes the canonical body
    // element, and its used WritingMode is different from the root element's
    // used WritingMode.
    // We need to reframe the root element so that the root element's frames has
    // the correct writing-mode propagated from body element. (See
    // nsCSSFrameConstructor::ConstructDocElementFrame.)
    //
    // Bug 1594297: When inserting a new <body>, we may need to reframe the old
    // <body> which has a "overflow" value other than simple "visible". But it's
    // tricky, see bug 1593752.
    nsIContent* bodyElement = mDocument->GetBodyElement();
    for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
      const WritingMode bodyWM(iter.item().mComputedStyle);
      if (iter.item().mContent == bodyElement &&
          bodyWM != aFrame->GetWritingMode()) {
        TRACE("Root");
        RecreateFramesForContent(mDocument->GetRootElement(),
                                 InsertionKind::Async);
        return true;
      }
    }
  }

  nsIFrame* nextSibling = ::GetInsertNextSibling(aFrame, aPrevSibling);

  // Situation #2 is a flex / grid  container frame into which we're inserting
  // new inline non-replaced children, adjacent to an existing anonymous flex or
  // grid item.
  if (aFrame->IsFlexOrGridContainer()) {
    FCItemIterator iter(aItems);

    // Check if we're adding to-be-wrapped content right *after* an existing
    // anonymous flex or grid item (which would need to absorb this content).
    const bool isLegacyWebKitBox = IsFlexContainerForLegacyWebKitBox(aFrame);
    if (aPrevSibling && IsAnonymousItem(aPrevSibling) &&
        iter.item().NeedsAnonFlexOrGridItem(aState, isLegacyWebKitBox)) {
      TRACE("Inserting inline after anon flex or grid item");
      RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
      return true;
    }

    // Check if we're adding to-be-wrapped content right *before* an existing
    // anonymous flex or grid item (which would need to absorb this content).
    if (nextSibling && IsAnonymousItem(nextSibling)) {
      // Jump to the last entry in the list
      iter.SetToEnd();
      iter.Prev();
      if (iter.item().NeedsAnonFlexOrGridItem(aState, isLegacyWebKitBox)) {
        TRACE("Inserting inline before anon flex or grid item");
        RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
        return true;
      }
    }
  }

  // Situation #3 is an anonymous flex or grid item that's getting new children
  // who don't want to be wrapped.
  if (IsAnonymousItem(aFrame)) {
    AssertAnonymousFlexOrGridItemParent(aFrame);

    // We need to push a null float containing block to be sure that
    // "NeedsAnonFlexOrGridItem" will know we're not honoring floats for this
    // inserted content. (In particular, this is necessary in order for
    // its "GetGeometricParent" call to return the correct result.)
    // We're not honoring floats on this content because it has the
    // _flex/grid container_ as its parent in the content tree.
    nsFrameConstructorSaveState floatSaveState;
    aState.PushFloatContainingBlock(nullptr, floatSaveState);

    FCItemIterator iter(aItems);
    // Skip over things that _do_ need an anonymous flex item, because
    // they're perfectly happy to go here -- they won't cause a reframe.
    nsIFrame* containerFrame = aFrame->GetParent();
    const bool isLegacyWebKitBox =
        IsFlexContainerForLegacyWebKitBox(containerFrame);
    if (!iter.SkipItemsThatNeedAnonFlexOrGridItem(aState, isLegacyWebKitBox)) {
      // We hit something that _doesn't_ need an anonymous flex item!
      // Rebuild the flex container to bust it out.
      TRACE("Inserting non-inlines inside anon flex or grid item");
      RecreateFramesForContent(containerFrame->GetContent(),
                               InsertionKind::Async);
      return true;
    }

    // If we get here, then everything in |aItems| needs to be wrapped in
    // an anonymous flex or grid item.  That's where it's already going - good!
  }

  // Situation #4 is a case when table pseudo-frames don't work out right
  ParentType parentType = GetParentType(aFrame);
  // If all the kids want a parent of the type that aFrame is, then we're all
  // set to go.  Indeed, there won't be any table pseudo-frames created between
  // aFrame and the kids, so those won't need to be merged with any table
  // pseudo-frames that might already be kids of aFrame.  If aFrame itself is a
  // table pseudo-frame, then all the kids in this list would have wanted a
  // frame of that type wrapping them anyway, so putting them inside it is ok.
  if (!aItems.AllWantParentType(parentType)) {
    // Don't give up yet.  If parentType is not eTypeBlock and the parent is
    // not a generated content frame, then try filtering whitespace out of the
    // list.
    if (parentType != eTypeBlock && !aFrame->IsGeneratedContentFrame()) {
      // For leading whitespace followed by a kid that wants our parent type,
      // there are four cases:
      // 1) We have a previous sibling which is not a table pseudo.  That means
      //    that previous sibling wanted a (non-block) parent of the type we're
      //    looking at.  Then the whitespace comes between two table-internal
      //    elements, so should be collapsed out.
      // 2) We have a previous sibling which is a table pseudo.  It might have
      //    kids who want this whitespace, so we need to reframe.
      // 3) We have no previous sibling and our parent frame is not a table
      //    pseudo.  That means that we'll be at the beginning of our actual
      //    non-block-type parent, and the whitespace is OK to collapse out.
      //    If something is ever inserted before us, it'll find our own parent
      //    as its parent and if it's something that would care about the
      //    whitespace it'll want a block parent, so it'll trigger a reframe at
      //    that point.
      // 4) We have no previous sibling and our parent frame is a table pseudo.
      //    Need to reframe.
      // All that is predicated on finding the correct previous sibling.  We
      // might have to walk backwards along continuations from aFrame to do so.
      //
      // It's always OK to drop whitespace between any two items that want a
      // parent of type parentType.
      //
      // For trailing whitespace preceded by a kid that wants our parent type,
      // there are four cases:
      // 1) We have a next sibling which is not a table pseudo.  That means
      //    that next sibling wanted a (non-block) parent of the type we're
      //    looking at.  Then the whitespace comes between two table-internal
      //    elements, so should be collapsed out.
      // 2) We have a next sibling which is a table pseudo.  It might have
      //    kids who want this whitespace, so we need to reframe.
      // 3) We have no next sibling and our parent frame is not a table
      //    pseudo.  That means that we'll be at the end of our actual
      //    non-block-type parent, and the whitespace is OK to collapse out.
      //    If something is ever inserted after us, it'll find our own parent
      //    as its parent and if it's something that would care about the
      //    whitespace it'll want a block parent, so it'll trigger a reframe at
      //    that point.
      // 4) We have no next sibling and our parent frame is a table pseudo.
      //    Need to reframe.
      // All that is predicated on finding the correct next sibling.  We might
      // have to walk forward along continuations from aFrame to do so.  That
      // said, in the case when nextSibling is null at this point and aIsAppend
      // is true, we know we're in case 3.  Furthermore, in that case we don't
      // even have to worry about the table pseudo situation; we know our
      // parent is not a table pseudo there.
      FCItemIterator iter(aItems);
      FCItemIterator start(iter);
      do {
        if (iter.SkipItemsWantingParentType(parentType)) {
          break;
        }

        // iter points to an item that wants a different parent.  If it's not
        // whitespace, we're done; no more point scanning the list.
        if (!iter.item().IsWhitespace(aState)) {
          break;
        }

        if (iter == start) {
          // Leading whitespace.  How to handle this depends on our
          // previous sibling and aFrame.  See the long comment above.
          nsIFrame* prevSibling = aPrevSibling;
          if (!prevSibling) {
            // Try to find one after all
            nsIFrame* parentPrevCont = aFrame->GetPrevContinuation();
            while (parentPrevCont) {
              prevSibling = parentPrevCont->PrincipalChildList().LastChild();
              if (prevSibling) {
                break;
              }
              parentPrevCont = parentPrevCont->GetPrevContinuation();
            }
          };
          if (prevSibling) {
            if (IsTablePseudo(prevSibling)) {
              // need to reframe
              break;
            }
          } else if (IsTablePseudo(aFrame)) {
            // need to reframe
            break;
          }
        }

        FCItemIterator spaceEndIter(iter);
        // Advance spaceEndIter past any whitespace
        bool trailingSpaces = spaceEndIter.SkipWhitespace(aState);

        bool okToDrop;
        if (trailingSpaces) {
          // Trailing whitespace.  How to handle this depeds on aIsAppend, our
          // next sibling and aFrame.  See the long comment above.
          okToDrop = aIsAppend && !nextSibling;
          if (!okToDrop) {
            if (!nextSibling) {
              // Try to find one after all
              nsIFrame* parentNextCont = aFrame->GetNextContinuation();
              while (parentNextCont) {
                nextSibling = parentNextCont->PrincipalChildList().FirstChild();
                if (nextSibling) {
                  break;
                }
                parentNextCont = parentNextCont->GetNextContinuation();
              }
            }

            okToDrop = (nextSibling && !IsTablePseudo(nextSibling)) ||
                       (!nextSibling && !IsTablePseudo(aFrame));
          } else {
            NS_ASSERTION(!IsTablePseudo(aFrame), "How did that happen?");
          }
        } else {
          okToDrop = (spaceEndIter.item().DesiredParentType() == parentType);
        }

        if (okToDrop) {
          iter.DeleteItemsTo(this, spaceEndIter);
        } else {
          // We're done: we don't want to drop the whitespace, and it has the
          // wrong parent type.
          break;
        }

        // Now loop, since |iter| points to item right after the whitespace we
        // removed.
      } while (!iter.IsDone());
    }

    // We might be able to figure out some sort of optimizations here, but they
    // would have to depend on having a correct aPrevSibling and a correct next
    // sibling.  For example, we can probably avoid reframing if none of
    // aFrame, aPrevSibling, and next sibling are table pseudo-frames.  But it
    // doesn't seem worth it to worry about that for now, especially since we
    // in fact do not have a reliable aPrevSibling, nor any next sibling, in
    // this method.

    // aItems might have changed, so recheck the parent type thing.  In fact,
    // it might be empty, so recheck that too.
    if (aItems.IsEmpty()) {
      return false;
    }

    // If aFrame is empty, the insertion process will be able to take care of
    // creating any needed pseudo-parents.
    if (!aItems.AllWantParentType(parentType) &&
        !SafeToInsertPseudoNeedingChildren(aFrame)) {
      // Reframing aFrame->GetContent() is good enough, since the content of
      // table pseudo-frames is the ancestor content.
      TRACE("Pseudo-frames going wrong");
      RecreateFramesForContent(aFrame->GetContent(), InsertionKind::Async);
      return true;
    }
  }

  // Situation #5 is a frame in multicol subtree that's getting new children.
  if (aFrame->HasAnyStateBits(NS_FRAME_HAS_MULTI_COLUMN_ANCESTOR)) {
    bool anyColumnSpanItems = false;
    for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
      if (iter.item().mComputedStyle->StyleColumn()->IsColumnSpanStyle()) {
        anyColumnSpanItems = true;
        break;
      }
    }

    bool needsReframe =
        // 1. Insert / append any column-span children.
        anyColumnSpanItems ||
        // 2. GetInsertionPrevSibling() modifies insertion parent. If the prev
        // sibling is a column-span, aFrame ends up being the
        // column-span-wrapper.
        aFrame->Style()->GetPseudoType() ==
            PseudoStyleType::MozColumnSpanWrapper ||
        // 3. Append into {ib} split container. There might be room for
        // optimization, but let's reframe for correctness...
        IsFramePartOfIBSplit(aFrame);

    if (needsReframe) {
      TRACE("Multi-column");
      RecreateFramesForContent(
          GetMultiColumnContainingBlockFor(aFrame)->GetContent(),
          InsertionKind::Async);
      return true;
    }

    // If we get here, then we need further check for {ib} split to decide
    // whether to reframe. For example, appending a block into an empty inline
    // that is not part of an {ib} split, but should become an {ib} split.
  }

  // A <fieldset> may need to pick up a new rendered legend from aItems.
  // We currently can't handle this case without recreating frames for
  // the fieldset.
  // XXXmats we should be able to optimize this when the fieldset doesn't
  // currently have a rendered legend.  ContentRangeInserted needs to be fixed
  // to use the inner frame as the content insertion frame in that case.
  if (const auto* fieldset = GetFieldSetFrameFor(aFrame)) {
    // Check if any item is eligible to be a rendered legend.
    for (FCItemIterator iter(aItems); !iter.IsDone(); iter.Next()) {
      const auto& item = iter.item();
      if (!item.mContent->IsHTMLElement(nsGkAtoms::legend)) {
        continue;
      }
      const auto* display = item.mComputedStyle->StyleDisplay();
      if (display->IsFloatingStyle() ||
          display->IsAbsolutelyPositionedStyle()) {
        continue;
      }
      TRACE("Fieldset with rendered legend");
      RecreateFramesForContent(fieldset->GetContent(), InsertionKind::Async);
      return true;
    }
  }

  // Now we have several cases involving {ib} splits.  Put them all in a
  // do/while with breaks to take us to the "go and reconstruct" code.
  do {
    if (IsInlineFrame(aFrame)) {
      if (aItems.AreAllItemsInline()) {
        // We can just put the kids in.
        return false;
      }

      if (!IsFramePartOfIBSplit(aFrame)) {
        // Need to go ahead and reconstruct.
        break;
      }

      // Now we're adding kids including some blocks to an inline part of an
      // {ib} split.  If we plan to call AppendFrames, and don't have a next
      // sibling for the new frames, and our parent is the last continuation of
      // the last part of the {ib} split, and the same is true of all our
      // ancestor inlines (they have no following continuations and they're the
      // last part of their {ib} splits and we'd be adding to the end for all
      // of them), then AppendFrames will handle things for us.  Bail out in
      // that case.
      if (aIsAppend && IsSafeToAppendToIBSplitInline(aFrame, nextSibling)) {
        return false;
      }

      // Need to reconstruct.
      break;
    }

    // Now we know we have a block parent.  If it's not part of an
    // ib-split, we're all set.
    if (!IsFramePartOfIBSplit(aFrame)) {
      return false;
    }

    // We're adding some kids to a block part of an {ib} split.  If all the
    // kids are blocks, we don't need to reconstruct.
    if (aItems.AreAllItemsBlock()) {
      return false;
    }

    // We might have some inline kids for this block.  Just fall out of the
    // loop and reconstruct.
  } while (false);

  // If we don't have a containing block, start with aFrame and look for one.
  if (!aContainingBlock) {
    aContainingBlock = aFrame;
  }

  // To find the right block to reframe, just walk up the tree until we find a
  // frame that is:
  // 1)  Not part of an IB split
  // 2)  Not a pseudo-frame
  // 3)  Not an inline frame
  // We're guaranteed to find one, since ComputedStyle::ApplyStyleFixups
  // enforces that the root is display:none, display:table, or display:block.
  // Note that walking up "too far" is OK in terms of correctness, even if it
  // might be a little inefficient.  This is why we walk out of all
  // pseudo-frames -- telling which ones are or are not OK to walk out of is
  // too hard (and I suspect that we do in fact need to walk out of all of
  // them).
  while (IsFramePartOfIBSplit(aContainingBlock) ||
         aContainingBlock->IsInlineOutside() ||
         aContainingBlock->Style()->IsPseudoOrAnonBox()) {
    aContainingBlock = aContainingBlock->GetParent();
    NS_ASSERTION(aContainingBlock,
                 "Must have non-inline, non-ib-split, non-pseudo frame as "
                 "root (or child of root, for a table root)!");
  }

  // Tell parent of the containing block to reformulate the
  // entire block. This is painful and definitely not optimal
  // but it will *always* get the right answer.

  nsIContent* blockContent = aContainingBlock->GetContent();
  TRACE("IB splits");
  RecreateFramesForContent(blockContent, InsertionKind::Async);
  return true;
#undef TRACE
}

void nsCSSFrameConstructor::ReframeContainingBlock(nsIFrame* aFrame) {
  // XXXbz how exactly would we get here while isReflowing anyway?  Should this
  // whole test be ifdef DEBUG?
  if (mPresShell->IsReflowLocked()) {
    // don't ReframeContainingBlock, this will result in a crash
    // if we remove a tree that's in reflow - see bug 121368 for testcase
    NS_ERROR(
        "Atemptted to nsCSSFrameConstructor::ReframeContainingBlock during a "
        "Reflow!!!");
    return;
  }

  // Get the first "normal" ancestor of the target frame.
  nsIFrame* containingBlock = GetIBContainingBlockFor(aFrame);
  if (containingBlock) {
    // From here we look for the containing block in case the target
    // frame is already a block (which can happen when an inline frame
    // wraps some of its content in an anonymous block; see
    // ConstructInline)

    // NOTE: We used to get the FloatContainingBlock here, but it was often
    // wrong. GetIBContainingBlock works much better and provides the correct
    // container in all cases so GetFloatContainingBlock(aFrame) has been
    // removed

    // And get the containingBlock's content
    if (nsIContent* blockContent = containingBlock->GetContent()) {
#ifdef DEBUG
      if (gNoisyContentUpdates) {
        printf("  ==> blockContent=%p\n", blockContent);
      }
#endif
      RecreateFramesForContent(blockContent, InsertionKind::Async);
      return;
    }
  }

  // If we get here, we're screwed!
  RecreateFramesForContent(mPresShell->GetDocument()->GetRootElement(),
                           InsertionKind::Async);
}

//////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItem methods //
//////////////////////////////////////////////////////////
bool nsCSSFrameConstructor::FrameConstructionItem::IsWhitespace(
    nsFrameConstructorState& aState) const {
  MOZ_ASSERT(aState.mCreatingExtraFrames || !mContent->GetPrimaryFrame(),
             "How did that happen?");
  if (!mIsText) {
    return false;
  }
  mContent->SetFlags(NS_CREATE_FRAME_IF_NON_WHITESPACE |
                     NS_REFRAME_IF_WHITESPACE);
  return mContent->TextIsOnlyWhitespace();
}

//////////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItemList methods //
//////////////////////////////////////////////////////////////
void nsCSSFrameConstructor::FrameConstructionItemList::AdjustCountsForItem(
    FrameConstructionItem* aItem, int32_t aDelta) {
  MOZ_ASSERT(aDelta == 1 || aDelta == -1, "Unexpected delta");
  mItemCount += aDelta;
  if (aItem->mIsAllInline) {
    mInlineCount += aDelta;
  }
  if (aItem->mIsBlock) {
    mBlockCount += aDelta;
  }
  mDesiredParentCounts[aItem->DesiredParentType()] += aDelta;
}

////////////////////////////////////////////////////////////////////////
// nsCSSFrameConstructor::FrameConstructionItemList::Iterator methods //
////////////////////////////////////////////////////////////////////////
inline bool nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    SkipItemsWantingParentType(ParentType aParentType) {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  while (item().DesiredParentType() == aParentType) {
    Next();
    if (IsDone()) {
      return true;
    }
  }
  return false;
}

inline bool nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    SkipItemsNotWantingParentType(ParentType aParentType) {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  while (item().DesiredParentType() != aParentType) {
    Next();
    if (IsDone()) {
      return true;
    }
  }
  return false;
}

// Note: we implement -webkit-{inline-}box using nsFlexContainerFrame, but we
// use different rules for what gets wrapped in an anonymous flex item.
bool nsCSSFrameConstructor::FrameConstructionItem::NeedsAnonFlexOrGridItem(
    const nsFrameConstructorState& aState, bool aIsLegacyWebKitBox) {
  if (mFCData->mBits & FCDATA_IS_LINE_PARTICIPANT) {
    // This will be an inline non-replaced box.
    return true;
  }

  if (aIsLegacyWebKitBox) {
    if (mComputedStyle->StyleDisplay()->IsInlineOutsideStyle()) {
      // In an emulated legacy box, all inline-level content gets wrapped in an
      // anonymous flex item.
      return true;
    }
    if (!(mFCData->mBits & FCDATA_DISALLOW_OUT_OF_FLOW) &&
        aState.GetGeometricParent(*mComputedStyle->StyleDisplay(), nullptr)) {
      // We're abspos or fixedpos (or a XUL popup), which means we'll spawn a
      // placeholder which (because our container is an emulated legacy box)
      // we'll need to wrap in an anonymous flex item.  So, we just treat
      // _this_ frame as if _it_ needs to be wrapped in an anonymous flex item,
      // and then when we spawn the placeholder, it'll end up in the right
      // spot.
      return true;
    }
  }

  return false;
}

inline bool nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    SkipItemsThatNeedAnonFlexOrGridItem(const nsFrameConstructorState& aState,
                                        bool aIsLegacyWebKitBox) {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  while (item().NeedsAnonFlexOrGridItem(aState, aIsLegacyWebKitBox)) {
    Next();
    if (IsDone()) {
      return true;
    }
  }
  return false;
}

inline bool nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    SkipItemsThatDontNeedAnonFlexOrGridItem(
        const nsFrameConstructorState& aState, bool aIsLegacyWebKitBox) {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  while (!(item().NeedsAnonFlexOrGridItem(aState, aIsLegacyWebKitBox))) {
    Next();
    if (IsDone()) {
      return true;
    }
  }
  return false;
}

inline bool nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    SkipItemsNotWantingRubyParent() {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  while (!IsRubyParentType(item().DesiredParentType())) {
    Next();
    if (IsDone()) {
      return true;
    }
  }
  return false;
}

inline bool
nsCSSFrameConstructor::FrameConstructionItemList::Iterator::SkipWhitespace(
    nsFrameConstructorState& aState) {
  MOZ_ASSERT(!IsDone(), "Shouldn't be done yet");
  MOZ_ASSERT(item().IsWhitespace(aState), "Not pointing to whitespace?");
  do {
    Next();
    if (IsDone()) {
      return true;
    }
  } while (item().IsWhitespace(aState));

  return false;
}

void nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    AppendItemToList(FrameConstructionItemList& aTargetList) {
  NS_ASSERTION(&aTargetList != &mList, "Unexpected call");
  MOZ_ASSERT(!IsDone(), "should not be done");

  FrameConstructionItem* item = mCurrent;
  Next();
  item->remove();
  aTargetList.mItems.insertBack(item);

  mList.AdjustCountsForItem(item, -1);
  aTargetList.AdjustCountsForItem(item, 1);
}

void nsCSSFrameConstructor::FrameConstructionItemList::Iterator::
    AppendItemsToList(nsCSSFrameConstructor* aFCtor, const Iterator& aEnd,
                      FrameConstructionItemList& aTargetList) {
  NS_ASSERTION(&aTargetList != &mList, "Unexpected call");
  MOZ_ASSERT(&mList == &aEnd.mList, "End iterator for some other list?");

  // We can't just move our guts to the other list if it already has
  // some information or if we're not moving our entire list.
  if (!AtStart() || !aEnd.IsDone() || !aTargetList.IsEmpty()) {
    do {
      AppendItemToList(aTargetList);
    } while (*this != aEnd);
    return;
  }

  // Move our entire list of items into the empty target list.
  aTargetList.mItems = std::move(mList.mItems);

  // Copy over the various counters
  aTargetList.mInlineCount = mList.mInlineCount;
  aTargetList.mBlockCount = mList.mBlockCount;
  aTargetList.mItemCount = mList.mItemCount;
  memcpy(aTargetList.mDesiredParentCounts, mList.mDesiredParentCounts,
         sizeof(aTargetList.mDesiredParentCounts));

  // reset mList
  mList.Reset(aFCtor);

  // Point ourselves to aEnd, as advertised
  SetToEnd();
  MOZ_ASSERT(*this == aEnd, "How did that happen?");
}

void nsCSSFrameConstructor::FrameConstructionItemList::Iterator::InsertItem(
    FrameConstructionItem* aItem) {
  if (IsDone()) {
    mList.mItems.insertBack(aItem);
  } else {
    // Just insert the item before us.  There's no magic here.
    mCurrent->setPrevious(aItem);
  }
  mList.AdjustCountsForItem(aItem, 1);

  MOZ_ASSERT(aItem->getNext() == mCurrent, "How did that happen?");
}

void nsCSSFrameConstructor::FrameConstructionItemList::Iterator::DeleteItemsTo(
    nsCSSFrameConstructor* aFCtor, const Iterator& aEnd) {
  MOZ_ASSERT(&mList == &aEnd.mList, "End iterator for some other list?");
  MOZ_ASSERT(*this != aEnd, "Shouldn't be at aEnd yet");

  do {
    NS_ASSERTION(!IsDone(), "Ran off end of list?");
    FrameConstructionItem* item = mCurrent;
    Next();
    item->remove();
    mList.AdjustCountsForItem(item, -1);
    item->Delete(aFCtor);
  } while (*this != aEnd);
}

void nsCSSFrameConstructor::QuotesDirty() {
  mQuotesDirty = true;
  mPresShell->SetNeedLayoutFlush();
}

void nsCSSFrameConstructor::CountersDirty() {
  mCountersDirty = true;
  mPresShell->SetNeedLayoutFlush();
}

void* nsCSSFrameConstructor::AllocateFCItem() {
  void* item;
  if (mFirstFreeFCItem) {
    item = mFirstFreeFCItem;
    mFirstFreeFCItem = mFirstFreeFCItem->mNext;
  } else {
    item = mFCItemPool.Allocate(sizeof(FrameConstructionItem));
  }
  ++mFCItemsInUse;
  return item;
}

void nsCSSFrameConstructor::FreeFCItem(FrameConstructionItem* aItem) {
  MOZ_ASSERT(mFCItemsInUse != 0);
  if (--mFCItemsInUse == 0) {
    // The arena is now unused - clear it but retain one chunk.
    mFirstFreeFCItem = nullptr;
    mFCItemPool.Clear();
  } else {
    // Prepend it to the list of free items.
    FreeFCItemLink* item = reinterpret_cast<FreeFCItemLink*>(aItem);
    item->mNext = mFirstFreeFCItem;
    mFirstFreeFCItem = item;
  }
}

void nsCSSFrameConstructor::AddSizeOfIncludingThis(
    nsWindowSizes& aSizes) const {
  if (nsIFrame* rootFrame = GetRootFrame()) {
    rootFrame->AddSizeOfExcludingThisForTree(aSizes);
    if (RetainedDisplayListBuilder* builder =
            rootFrame->GetProperty(RetainedDisplayListBuilder::Cached())) {
      builder->AddSizeOfIncludingThis(aSizes);
    }
  }

  // This must be done after measuring from the frame tree, since frame
  // manager will measure sizes of staled computed values and style
  // structs, which only make sense after we know what are being used.
  nsFrameManager::AddSizeOfIncludingThis(aSizes);

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mFCItemPool
  // - mContainStyleScopeManager
}
