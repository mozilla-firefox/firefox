/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of nsFrameSelection
 */

#include "nsFrameSelection.h"

#include <algorithm>

#include "ErrorList.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Logging.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/PseudoStyleType.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_bidi.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/TextEvents.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsBidiPresUtils.h"
#include "nsCCUncollectableMarker.h"
#include "nsCOMPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsDeviceContext.h"
#include "nsFrameTraversal.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsISelectionListener.h"
#include "nsITableCellLayout.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTableCellFrame.h"
#include "nsTableWrapperFrame.h"
#include "nsTextFrame.h"
#include "nsThreadUtils.h"

// notifications
#include "SelectionMovementUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoCopyListener.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Highlight.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/SelectionBinding.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/Text.h"
#include "nsCopySupport.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsIClipboard.h"
#include "nsIFrameInlines.h"
#include "nsISelectionController.h"  //for the enums
#include "nsPIDOMWindow.h"

using namespace mozilla;
using namespace mozilla::dom;

static LazyLogModule sFrameSelectionLog("FrameSelection");

std::ostream& operator<<(std::ostream& aStream,
                         const nsFrameSelection& aFrameSelection) {
  return aStream << "{ mPresShell=" << aFrameSelection.mPresShell
                 << ", mLimiters={ mIndependentSelectionRootElement="
                 << aFrameSelection.mLimiters.mIndependentSelectionRootElement
                 << ", mAncestorLimiter="
                 << aFrameSelection.mLimiters.mAncestorLimiter
                 << "}, IsBatching()=" << std::boolalpha
                 << aFrameSelection.IsBatching()
                 << ", IsInTableSelectionMode()=" << std::boolalpha
                 << aFrameSelection.IsInTableSelectionMode()
                 << ", GetDragState()=" << std::boolalpha
                 << aFrameSelection.GetDragState()
                 << ", HighlightSelectionCount()="
                 << aFrameSelection.HighlightSelectionCount() << " }";
}

namespace mozilla {
extern LazyLogModule sSelectionAPILog;
extern void LogStackForSelectionAPI();

static void LogSelectionAPI(const dom::Selection* aSelection,
                            const char* aFuncName, const char* aArgName,
                            const nsIContent* aContent) {
  MOZ_LOG(sSelectionAPILog, LogLevel::Info,
          ("%p nsFrameSelection::%s(%s=%s)", aSelection, aFuncName, aArgName,
           aContent ? ToString(*aContent).c_str() : "<nullptr>"));
}
}  // namespace mozilla

// #define DEBUG_TABLE 1

/**
 * Add cells to the selection inside of the given cells range.
 *
 * @param  aTable             [in] HTML table element
 * @param  aStartRowIndex     [in] row index where the cells range starts
 * @param  aStartColumnIndex  [in] column index where the cells range starts
 * @param  aEndRowIndex       [in] row index where the cells range ends
 * @param  aEndColumnIndex    [in] column index where the cells range ends
 */
static nsresult AddCellsToSelection(const nsIContent* aTableContent,
                                    int32_t aStartRowIndex,
                                    int32_t aStartColumnIndex,
                                    int32_t aEndRowIndex,
                                    int32_t aEndColumnIndex,
                                    Selection& aNormalSelection);

static nsAtom* GetTag(nsINode* aNode);

static nsINode* GetClosestInclusiveTableCellAncestor(nsINode* aDomNode);
MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult CreateAndAddRange(
    nsINode* aContainer, int32_t aOffset, Selection& aNormalSelection);
static nsresult SelectCellElement(nsIContent* aCellElement,
                                  Selection& aNormalSelection);

// On macOS, we need to update the selection cache when we repaint a selection.
// This runs nsHTMLCopyEncoder to serialize the selection ranges, which is
// really complicated especially when shadow DOM selection. Therefore, that may
// cause some assertion failures. Unfortunately, our macOS machines in the CI
// are always busy and we need one or two days to check the result on trysever.
// Therefore, we should run nsHTMLCopyEncoder part in all desktop platforms if
// it's a debug build. Thus, we can check the result on Linux machines in
// tryserver.
#if defined(XP_MACOSX) || (defined(DEBUG) && !defined(ANDROID))
#  define RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION
#endif

#ifdef RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION
static nsresult MaybeUpdateSelectionCacheOnRepaintSelection(Selection* aSel);
#endif  // RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION

#ifdef PRINT_RANGE
static void printRange(dom::Range* aDomRange);
#  define DEBUG_OUT_RANGE(x) printRange(x)
#else
#  define DEBUG_OUT_RANGE(x)
#endif  // PRINT_RANGE

/******************************************************************************
 * mozilla::PeekOffsetStruct
 ******************************************************************************/

// #define DEBUG_SELECTION // uncomment for printf describing every collapse and
//  extend. #define DEBUG_NAVIGATION

// #define DEBUG_TABLE_SELECTION 1

namespace mozilla {

PeekOffsetStruct::PeekOffsetStruct(
    nsSelectionAmount aAmount, nsDirection aDirection, int32_t aStartOffset,
    nsPoint aDesiredCaretPos, const PeekOffsetOptions aOptions,
    EWordMovementType aWordMovementType /* = eDefaultBehavior */,
    const Element* aAncestorLimiter /* = nullptr */)
    : mAmount(aAmount),
      mDirection(aDirection),
      mStartOffset(aStartOffset),
      mDesiredCaretPos(aDesiredCaretPos),
      mWordMovementType(aWordMovementType),
      mOptions(aOptions),
      mAncestorLimiter(aAncestorLimiter),
      mResultFrame(nullptr),
      mContentOffset(0),
      mAttach(CaretAssociationHint::Before) {}

}  // namespace mozilla

// Array which contains index of each SelectionType in
// Selection::mDOMSelections. For avoiding using if nor switch to retrieve the
// index, this needs to have -1 for SelectionTypes which won't be created its
// Selection instance.
static const int8_t kIndexOfSelections[] = {
    -1,  // SelectionType::eInvalid
    -1,  // SelectionType::eNone
    0,   // SelectionType::eNormal
    1,   // SelectionType::eSpellCheck
    2,   // SelectionType::eIMERawClause
    3,   // SelectionType::eIMESelectedRawClause
    4,   // SelectionType::eIMEConvertedClause
    5,   // SelectionType::eIMESelectedClause
    6,   // SelectionType::eAccessibility
    7,   // SelectionType::eFind
    8,   // SelectionType::eURLSecondary
    9,   // SelectionType::eURLStrikeout
    10,  // SelectionType::eTargetText
    -1,  // SelectionType::eHighlight
};

inline int8_t GetIndexFromSelectionType(SelectionType aSelectionType) {
  // The enum value of eInvalid is -1 and the others are sequential value
  // starting from 0.  Therefore, |SelectionType + 1| is the index of
  // kIndexOfSelections.
  return kIndexOfSelections[static_cast<int8_t>(aSelectionType) + 1];
}

namespace mozilla {

/*
The limiter is used specifically for the text areas and textfields
In that case it is the DIV tag that is anonymously created for the text
areas/fields.  Text nodes and BR nodes fall beneath it.  In the case of a
BR node the limiter will be the parent and the offset will point before or
after the BR node.  In the case of the text node the parent content is
the text node itself and the offset will be the exact character position.
The offset is not important to check for validity.  Simply look at the
passed in content.  If it equals the limiter then the selection point is valid.
If its parent it the limiter then the point is also valid.  In the case of
NO limiter all points are valid since you are in a topmost iframe. (browser
or composer)
*/
bool SelectionLimiters::NodeIsInLimiters(const nsINode* aContainerNode) const {
  if (!aContainerNode) {
    return false;
  }

  // If there is a selection limiter, it must be the anonymous <div> of a text
  // control.  The <div> should have only one Text and/or a <br>.  Therefore,
  // when it's non-nullptr, selection range containers must be the container or
  // the Text in it.
  if (mIndependentSelectionRootElement) {
    MOZ_ASSERT(mIndependentSelectionRootElement->GetPseudoElementType() ==
               PseudoStyleType::MozTextControlEditingRoot);
    MOZ_ASSERT(mIndependentSelectionRootElement->IsHTMLElement(nsGkAtoms::div));
    if (mIndependentSelectionRootElement == aContainerNode) {
      return true;
    }
    if (mIndependentSelectionRootElement == aContainerNode->GetParent()) {
      NS_WARNING_ASSERTION(aContainerNode->IsText(),
                           ToString(*aContainerNode).c_str());
      MOZ_ASSERT(aContainerNode->IsText());
      return true;
    }
    return false;
  }

  // XXX We might need to return `false` if aContainerNode is in a native
  // anonymous subtree, but doing it will make it impossible to select the
  // anonymous subtree text in <details>.
  return !mAncestorLimiter ||
         aContainerNode->IsInclusiveDescendantOf(mAncestorLimiter);
}

struct MOZ_RAII AutoPrepareFocusRange {
  AutoPrepareFocusRange(Selection* aSelection,
                        const bool aMultiRangeSelection) {
    MOZ_ASSERT(aSelection);
    MOZ_ASSERT(aSelection->GetType() == SelectionType::eNormal);

    if (aSelection->mStyledRanges.mRanges.Length() <= 1) {
      return;
    }

    if (aSelection->mFrameSelection->IsUserSelectionReason()) {
      mUserSelect.emplace(aSelection);
    }

    Span ranges = aSelection->mStyledRanges.Ranges();
    if (!aSelection->mUserInitiated || aMultiRangeSelection) {
      // Scripted command or the user is starting a new explicit multi-range
      // selection.
      for (const auto& range : ranges) {
        MOZ_ASSERT(range->IsRange());
        range->AsRange()->SetIsGenerated(false);
      }
      return;
    }

    if (!IsAnchorRelativeOperation(
            aSelection->mFrameSelection->mSelectionChangeReasons)) {
      return;
    }

    // This operation is against the anchor but our current mAnchorFocusRange
    // represents the focus in a multi-range selection.  The anchor from a user
    // perspective is the most distant generated range on the opposite side.
    // Find that range and make it the mAnchorFocusRange.
    dom::Range* const newAnchorFocusRange =
        FindGeneratedRangeMostDistantFromAnchor(*aSelection);

    if (!newAnchorFocusRange) {
      // There are no generated ranges - that's fine.
      return;
    }

    // Setup the new mAnchorFocusRange and mark the old one as generated.
    if (aSelection->mAnchorFocusRange) {
      aSelection->mAnchorFocusRange->SetIsGenerated(true);
    }

    newAnchorFocusRange->SetIsGenerated(false);
    aSelection->mAnchorFocusRange = newAnchorFocusRange;

    RemoveGeneratedRanges(*aSelection);

    if (aSelection->mFrameSelection) {
      aSelection->mFrameSelection->InvalidateDesiredCaretPos();
    }
  }

 private:
  static dom::Range* FindGeneratedRangeMostDistantFromAnchor(
      const Selection& aSelection) {
    const Span ranges = aSelection.mStyledRanges.Ranges();
    // This function is only called for selections with type == eNormal.
    // (see MOZ_ASSERT in constructor).
    // Therefore, all ranges must be dynamic.
    if (aSelection.GetDirection() == eDirNext) {
      for (const auto& range : ranges) {
        if (range->AsRange()->IsGenerated()) {
          return range->AsRange();
        }
      }
    } else {
      for (const auto& range : Reversed(ranges)) {
        if (range->AsRange()->IsGenerated()) {
          return range->AsRange();
        }
      }
    }

    return nullptr;
  }

  static void RemoveGeneratedRanges(Selection& aSelection) {
    RefPtr<nsPresContext> presContext = aSelection.GetPresContext();
    Span ranges = aSelection.mStyledRanges.Ranges();
    size_t i = ranges.Length();
    while (i--) {
      // This function is only called for selections with type == eNormal.
      // (see MOZ_ASSERT in constructor).
      // Therefore, all ranges must be dynamic.
      if (!ranges[i]->IsRange()) {
        continue;
      }
      dom::Range* range = ranges[i]->AsRange();
      if (range->IsGenerated()) {
        range->UnregisterSelection(aSelection);
        aSelection.SelectFrames(presContext, *range, false);
        aSelection.mStyledRanges.mRanges.RemoveElementAt(i);
      }
    }
  }

  /**
   * @aParam aSelectionChangeReasons can be multiple of the reasons defined in
             nsISelectionListener.idl.
   */
  static bool IsAnchorRelativeOperation(const int16_t aSelectionChangeReasons) {
    return aSelectionChangeReasons &
           (nsISelectionListener::DRAG_REASON |
            nsISelectionListener::MOUSEDOWN_REASON |
            nsISelectionListener::MOUSEUP_REASON |
            nsISelectionListener::COLLAPSETOSTART_REASON);
  }

  Maybe<Selection::AutoUserInitiated> mUserSelect;
};

}  // namespace mozilla

////////////BEGIN nsFrameSelection methods

template Result<RefPtr<dom::Range>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    PresShell& aPresShell,
    const mozilla::LimitersAndCaretData& aLimitersAndCaretData,
    const AbstractRange& aRange, nsDirection aRangeDirection,
    nsDirection aExtendDirection, nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle);
template Result<RefPtr<StaticRange>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    PresShell& aPresShell,
    const mozilla::LimitersAndCaretData& aLimitersAndCaretData,
    const AbstractRange& aRange, nsDirection aRangeDirection,
    nsDirection aExtendDirection, nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle);

nsFrameSelection::nsFrameSelection(
    PresShell* aPresShell, const bool aAccessibleCaretEnabled,
    Element* aEditorRootAnonymousDiv /* = nullptr */) {
  for (size_t i = 0; i < std::size(mDomSelections); i++) {
    mDomSelections[i] = new Selection(kPresentSelectionTypes[i], this);
  }

  Selection& sel = NormalSelection();
  if (AutoCopyListener::IsEnabled()) {
    sel.NotifyAutoCopy();
  }

  mPresShell = aPresShell;
  mDragState = false;

  MOZ_ASSERT_IF(aEditorRootAnonymousDiv,
                aEditorRootAnonymousDiv->GetPseudoElementType() ==
                    PseudoStyleType::MozTextControlEditingRoot);
  MOZ_ASSERT_IF(aEditorRootAnonymousDiv,
                aEditorRootAnonymousDiv->IsHTMLElement(nsGkAtoms::div));
  mLimiters.mIndependentSelectionRootElement = aEditorRootAnonymousDiv;

  // This should only ever be initialized on the main thread, so we are OK here.
  MOZ_ASSERT(NS_IsMainThread());

  mAccessibleCaretEnabled = aAccessibleCaretEnabled;
  if (mAccessibleCaretEnabled) {
    sel.MaybeNotifyAccessibleCaretEventHub(aPresShell);
  }

  sel.EnableSelectionChangeEvent();
}

nsFrameSelection::~nsFrameSelection() = default;

NS_IMPL_CYCLE_COLLECTION_CLASS(nsFrameSelection)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsFrameSelection)
  for (size_t i = 0; i < std::size(tmp->mDomSelections); ++i) {
    tmp->mDomSelections[i] = nullptr;
  }
  tmp->mHighlightSelections.Clear();

  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mTableSelection.mClosestInclusiveTableCellAncestor)
  tmp->mTableSelection.mMode = TableSelectionMode::None;
  tmp->mTableSelection.mDragSelectingCells = false;
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTableSelection.mStartSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTableSelection.mEndSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTableSelection.mAppendStartSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTableSelection.mUnselectCellOnMouseUp)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMaintainedRange.mRange)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLimiters.mIndependentSelectionRootElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLimiters.mAncestorLimiter)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsFrameSelection)
  if (tmp->mPresShell && tmp->mPresShell->GetDocument() &&
      nsCCUncollectableMarker::InGeneration(
          cb, tmp->mPresShell->GetDocument()->GetMarkedCCGeneration())) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  for (size_t i = 0; i < std::size(tmp->mDomSelections); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDomSelections[i])
  }

  for (const auto& value : tmp->mHighlightSelections) {
    CycleCollectionNoteChild(cb, value.second().get(),
                             "mHighlightSelections[]");
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mTableSelection.mClosestInclusiveTableCellAncestor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTableSelection.mStartSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTableSelection.mEndSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTableSelection.mAppendStartSelectedCell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTableSelection.mUnselectCellOnMouseUp)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMaintainedRange.mRange)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLimiters.mIndependentSelectionRootElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLimiters.mAncestorLimiter)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

nsISelectionController* nsFrameSelection::GetSelectionController() const {
  if (IsIndependentSelection()) {
    auto* const textControlElement = TextControlElement::FromNodeOrNull(
        GetIndependentSelectionRootParentElement());
    return textControlElement ? textControlElement->GetSelectionController()
                              : nullptr;
  }
  return mPresShell;
}

// static
void nsFrameSelection::WillFocusDocument(PresShell& aPresShell,
                                         Document& aDocument) {
  const RefPtr<nsFrameSelection> selection =
      aPresShell.GetLastFocusedFrameSelection();
  if (!selection) [[unlikely]] {
    return;
  }
  const int16_t selectionStatus = selection->GetDisplaySelection();
  // If selection was disabled, re-enable it.
  if (selectionStatus == nsISelectionController::SELECTION_DISABLED ||
      selectionStatus == nsISelectionController::SELECTION_HIDDEN) {
    selection->SetDisplaySelection(nsISelectionController::SELECTION_ON);
    selection->RepaintSelection(SelectionType::eNormal);
  }
  // See EditorBase::FinalizeSelection. This fixes up the case where focus
  // left the editor's selection but returned to something else.
  if (selection != aPresShell.ConstFrameSelection()) {
    const bool selectionMatchesFocus =
        selection->IsIndependentSelection() &&
        selection->GetIndependentSelectionRootParentElement() ==
            aDocument.GetUnretargetedFocusedContent();
    if (NS_WARN_IF(!selectionMatchesFocus)) {
      aPresShell.FrameSelectionWillLoseFocus(*selection);
      aPresShell.SelectionWillTakeFocus();
    }
  }
}

// static
void nsFrameSelection::WillBlurDocument(PresShell& aPresShell,
                                        Document& aDocument) {
  nsFrameSelection* const selection = aPresShell.GetLastFocusedFrameSelection();
  if (!selection) [[unlikely]] {
    return;
  }
  const int16_t selectionStatus = selection->GetDisplaySelection();
  // If selection was on, disable it.
  if (selectionStatus == nsISelectionController::SELECTION_ON ||
      selectionStatus == nsISelectionController::SELECTION_ATTENTION) {
    selection->SetDisplaySelection(nsISelectionController::SELECTION_DISABLED);
    selection->RepaintSelection(SelectionType::eNormal);
  }
}

// static
bool nsFrameSelection::Caret::IsVisualMovement(
    ExtendSelection aExtendSelection, CaretMovementStyle aMovementStyle) {
  int32_t movementFlag = StaticPrefs::bidi_edit_caret_movement_style();
  return aMovementStyle == eVisual ||
         (aMovementStyle == eUsePrefStyle &&
          (movementFlag == 1 ||
           (movementFlag == 2 && aExtendSelection == ExtendSelection::No)));
}

// Get the x (or y, in vertical writing mode) position requested
// by the Key Handling for line-up/down
nsresult nsFrameSelection::DesiredCaretPos::FetchPos(
    nsPoint& aDesiredCaretPos, const PresShell& aPresShell,
    Selection& aNormalSelection) const {
  MOZ_ASSERT(aNormalSelection.GetType() == SelectionType::eNormal);

  if (mIsSet) {
    aDesiredCaretPos = mValue;
    return NS_OK;
  }

  RefPtr<nsCaret> caret = aPresShell.GetActiveCaret();
  if (!caret) {
    return NS_ERROR_NULL_POINTER;
  }

  caret->SetSelection(&aNormalSelection);

  nsRect coord;
  nsIFrame* caretFrame = caret->GetGeometry(&coord);
  if (!caretFrame) {
    return NS_ERROR_FAILURE;
  }
  coord += caretFrame->GetOffsetToRootFrame();
  aDesiredCaretPos = coord.TopLeft();
  return NS_OK;
}

void nsFrameSelection::InvalidateDesiredCaretPos()  // do not listen to
                                                    // mDesiredCaretPos.mValue;
                                                    // you must get another.
{
  mDesiredCaretPos.Invalidate();
}

void nsFrameSelection::DesiredCaretPos::Invalidate() { mIsSet = false; }

void nsFrameSelection::DesiredCaretPos::Set(const nsPoint& aPos) {
  mValue = aPos;
  mIsSet = true;
}

nsresult nsFrameSelection::ConstrainFrameAndPointToAnchorSubtree(
    nsIFrame* aFrame, const nsPoint& aPoint, nsIFrame** aRetFrame,
    nsPoint& aRetPoint) const {
  //
  // The whole point of this method is to return a frame and point that
  // that lie within the same valid subtree as the anchor node's frame,
  // for use with the method GetContentAndOffsetsFromPoint().
  //
  // A valid subtree is defined to be one where all the content nodes in
  // the tree have a valid parent-child relationship.
  //
  // If the anchor frame and aFrame are in the same subtree, aFrame will
  // be returned in aRetFrame. If they are in different subtrees, we
  // return the frame for the root of the subtree.
  //

  if (!aFrame || !aRetFrame) {
    return NS_ERROR_NULL_POINTER;
  }

  *aRetFrame = aFrame;
  aRetPoint = aPoint;

  //
  // Get the frame and content for the selection's anchor point!
  //

  const Selection& sel = NormalSelection();

  nsCOMPtr<nsIContent> anchorContent =
      nsIContent::FromNodeOrNull(sel.GetMayCrossShadowBoundaryAnchorNode());
  if (!anchorContent) {
    return NS_ERROR_FAILURE;
  }

  //
  // Now find the root of the subtree containing the anchor's content.
  //

  NS_ENSURE_STATE(mPresShell);
  RefPtr<PresShell> presShell = mPresShell;
  nsIContent* anchorRoot = anchorContent->GetSelectionRootContent(
      presShell, nsINode::IgnoreOwnIndependentSelection::Yes,
      nsINode::AllowCrossShadowBoundary::Yes);
  NS_ENSURE_TRUE(anchorRoot, NS_ERROR_UNEXPECTED);

  //
  // Now find the root of the subtree containing aFrame's content.
  //

  nsCOMPtr<nsIContent> content = aFrame->GetContent();

  if (content) {
    nsIContent* contentRoot = content->GetSelectionRootContent(
        presShell, nsINode::IgnoreOwnIndependentSelection::Yes,
        nsINode::AllowCrossShadowBoundary::Yes);
    NS_ENSURE_TRUE(contentRoot, NS_ERROR_UNEXPECTED);

    if (anchorRoot == contentRoot) {
      // If the aFrame's content isn't the capturing content, it should be
      // a descendant.  At this time, we can return simply.
      nsIContent* capturedContent = PresShell::GetCapturingContent();
      if (capturedContent != content) {
        return NS_OK;
      }

      // Find the frame under the mouse cursor with the root frame.
      // At this time, don't use the anchor's frame because it may not have
      // fixed positioned frames.
      nsIFrame* rootFrame = presShell->GetRootFrame();
      nsPoint ptInRoot = aPoint + aFrame->GetOffsetTo(rootFrame);
      nsIFrame* cursorFrame =
          nsLayoutUtils::GetFrameForPoint(RelativeTo{rootFrame}, ptInRoot);

      // If the mouse cursor in on a frame which is descendant of same
      // selection root, we can expand the selection to the frame.
      if (cursorFrame && cursorFrame->PresShell() == presShell) {
        nsCOMPtr<nsIContent> cursorContent = cursorFrame->GetContent();
        NS_ENSURE_TRUE(cursorContent, NS_ERROR_FAILURE);
        nsIContent* cursorContentRoot = cursorContent->GetSelectionRootContent(
            presShell, nsINode::IgnoreOwnIndependentSelection::Yes,
            nsINode::AllowCrossShadowBoundary::Yes);
        NS_ENSURE_TRUE(cursorContentRoot, NS_ERROR_UNEXPECTED);
        if (cursorContentRoot == anchorRoot) {
          *aRetFrame = cursorFrame;
          aRetPoint = aPoint + aFrame->GetOffsetTo(cursorFrame);
          return NS_OK;
        }
      }
      // Otherwise, e.g., the cursor isn't on any frames (e.g., the mouse
      // cursor is out of the window), we should use the frame of the anchor
      // root.
    }
  }

  //
  // When we can't find a frame which is under the mouse cursor and has a same
  // selection root as the anchor node's, we should return the selection root
  // frame.
  //

  *aRetFrame = anchorRoot->GetPrimaryFrame();

  if (!*aRetFrame) {
    return NS_ERROR_FAILURE;
  }

  //
  // Now make sure that aRetPoint is converted to the same coordinate
  // system used by aRetFrame.
  //

  aRetPoint = aPoint + aFrame->GetOffsetTo(*aRetFrame);

  return NS_OK;
}

void nsFrameSelection::SetCaretBidiLevelAndMaybeSchedulePaint(
    mozilla::intl::BidiEmbeddingLevel aLevel) {
  // If the current level is undefined, we have just inserted new text.
  // In this case, we don't want to reset the keyboard language
  mCaret.mBidiLevel = aLevel;

  RefPtr<nsCaret> caret;
  if (mPresShell && (caret = mPresShell->GetActiveCaret())) {
    caret->SchedulePaint();
  }
}

mozilla::intl::BidiEmbeddingLevel nsFrameSelection::GetCaretBidiLevel() const {
  return mCaret.mBidiLevel;
}

void nsFrameSelection::UndefineCaretBidiLevel() {
  mCaret.mBidiLevel = mozilla::intl::BidiEmbeddingLevel(mCaret.mBidiLevel |
                                                        BIDI_LEVEL_UNDEFINED);
}

#ifdef PRINT_RANGE
void printRange(Range* aDomRange) {
  if (!aDomRange) {
    printf("NULL Range\n");
  }
  nsINode* startNode = aDomRange->GetStartContainer();
  nsINode* endNode = aDomRange->GetEndContainer();
  int32_t startOffset = aDomRange->StartOffset();
  int32_t endOffset = aDomRange->EndOffset();

  printf("range: 0x%lx\t start: 0x%lx %ld, \t end: 0x%lx,%ld\n",
         (unsigned long)aDomRange, (unsigned long)startNode, (long)startOffset,
         (unsigned long)endNode, (long)endOffset);
}
#endif /* PRINT_RANGE */

static nsAtom* GetTag(nsINode* aNode) {
  nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
  if (!content) {
    MOZ_ASSERT_UNREACHABLE("bad node passed to GetTag()");
    return nullptr;
  }

  return content->NodeInfo()->NameAtom();
}

/**
 * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor.
 */
static nsINode* GetClosestInclusiveTableCellAncestor(nsINode* aDomNode) {
  if (!aDomNode) {
    return nullptr;
  }
  nsINode* current = aDomNode;
  // Start with current node and look for a table cell
  while (current) {
    nsAtom* tag = GetTag(current);
    if (tag == nsGkAtoms::td || tag == nsGkAtoms::th) {
      return current;
    }
    current = current->GetParent();
  }
  return nullptr;
}

static nsDirection GetCaretDirection(const nsIFrame& aFrame,
                                     nsDirection aDirection,
                                     bool aVisualMovement) {
  const mozilla::intl::BidiDirection paragraphDirection =
      nsBidiPresUtils::ParagraphDirection(&aFrame);
  return (aVisualMovement &&
          paragraphDirection == mozilla::intl::BidiDirection::RTL)
             ? nsDirection(1 - aDirection)
             : aDirection;
}

nsresult nsFrameSelection::MoveCaret(nsDirection aDirection,
                                     ExtendSelection aExtendSelection,
                                     const nsSelectionAmount aAmount,
                                     CaretMovementStyle aMovementStyle) {
  NS_ENSURE_STATE(mPresShell);
  // Flush out layout, since we need it to be up to date to do caret
  // positioning.
  OwningNonNull<PresShell> presShell(*mPresShell);
  presShell->FlushPendingNotifications(FlushType::Layout);

  if (!mPresShell) {
    return NS_OK;
  }

  nsPresContext* context = mPresShell->GetPresContext();
  if (!context) {
    return NS_ERROR_FAILURE;
  }

  const RefPtr<Selection> sel = &NormalSelection();
  if (const dom::Range* anchorFocusRange = sel->GetAnchorFocusRange()) {
    if (NS_WARN_IF(!anchorFocusRange->IsPositioned())) {
      return NS_ERROR_FAILURE;
    }
    // If the selection range to be modified is outside the limiters, we should
    // not touch it.
    if (!mLimiters.RangeInLimiters(*anchorFocusRange)) [[unlikely]] {
      // We don't want the caller to fall the per line move back to a complete
      // move in this case. So, let's return "did nothing".
      return NS_SUCCESS_DOM_NO_OPERATION;
    }
  } else {
    // No range to modify.
    return NS_ERROR_FAILURE;
  }

  auto scrollFlags = ScrollFlags::None;
  if (sel->IsEditorSelection()) {
    // If caret moves in editor, it should cause scrolling even if it's in
    // overflow: hidden;.
    scrollFlags |= ScrollFlags::ScrollOverflowHidden;
  }

  const bool doCollapse = [&] {
    if (sel->IsCollapsed() || aExtendSelection == ExtendSelection::Yes) {
      return false;
    }
    if (aAmount > eSelectLine) {
      return false;
    }
    int32_t caretStyle = StaticPrefs::layout_selection_caret_style();
    return caretStyle == 2 || (caretStyle == 0 && aAmount != eSelectLine);
  }();

  if (doCollapse) {
    if (aDirection == eDirPrevious) {
      SetChangeReasons(nsISelectionListener::COLLAPSETOSTART_REASON);
      mCaret.mHint = CaretAssociationHint::After;
    } else {
      SetChangeReasons(nsISelectionListener::COLLAPSETOEND_REASON);
      mCaret.mHint = CaretAssociationHint::Before;
    }
  } else {
    SetChangeReasons(nsISelectionListener::KEYPRESS_REASON);
  }

  mCaretMoveAmount = aAmount;

  AutoPrepareFocusRange prep(sel, false);

  // we must keep this around and revalidate it when its just UP/DOWN
  nsPoint desiredPos(0, 0);

  if (aAmount == eSelectLine) {
    nsresult result = mDesiredCaretPos.FetchPos(desiredPos, *mPresShell, *sel);
    if (NS_FAILED(result)) {
      return result;
    }
    mDesiredCaretPos.Set(desiredPos);
  }

  bool visualMovement =
      Caret::IsVisualMovement(aExtendSelection, aMovementStyle);
  const PrimaryFrameData frameForFocus =
      sel->GetPrimaryFrameForCaretAtFocusNode(visualMovement);
  if (!frameForFocus) {
    return NS_ERROR_FAILURE;
  }
  if (visualMovement) {
    // FYI: This was done during a call of GetPrimaryFrameForCaretAtFocusNode.
    // Therefore, this may not be intended by the original author.
    SetHint(frameForFocus.mHint);
  }

  Result<bool, nsresult> isIntraLineCaretMove =
      SelectionMovementUtils::IsIntraLineCaretMove(aAmount);
  nsDirection direction{aDirection};
  if (isIntraLineCaretMove.isErr()) {
    return isIntraLineCaretMove.unwrapErr();
  }
  if (isIntraLineCaretMove.inspect()) {
    // Forget old caret position for moving caret to different line since
    // caret position may be changed.
    mDesiredCaretPos.Invalidate();
    direction =
        GetCaretDirection(*frameForFocus.mFrame, aDirection, visualMovement);
  }

  if (doCollapse) {
    const dom::Range* anchorFocusRange = sel->GetAnchorFocusRange();
    if (anchorFocusRange) {
      RefPtr<nsINode> node;
      uint32_t offset;
      if (visualMovement &&
          nsBidiPresUtils::IsReversedDirectionFrame(frameForFocus.mFrame)) {
        direction = nsDirection(1 - direction);
      }
      if (direction == eDirPrevious) {
        node = anchorFocusRange->GetStartContainer();
        offset = anchorFocusRange->StartOffset();
      } else {
        node = anchorFocusRange->GetEndContainer();
        offset = anchorFocusRange->EndOffset();
      }
      sel->CollapseInLimiter(node, offset);
    }
    sel->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION,
                        AxisScrollParams(), AxisScrollParams(), scrollFlags);
    return NS_OK;
  }

  CaretAssociationHint tHint(
      mCaret.mHint);  // temporary variable so we dont set
                      // mCaret.mHint until it is necessary

  Result<PeekOffsetOptions, nsresult> options =
      CreatePeekOffsetOptionsForCaretMove(sel, aExtendSelection,
                                          aMovementStyle);
  if (options.isErr()) {
    return options.propagateErr();
  }
  Result<const dom::Element*, nsresult> ancestorLimiter =
      GetAncestorLimiterForCaretMove(sel);
  if (ancestorLimiter.isErr()) {
    return ancestorLimiter.propagateErr();
  }
  nsIContent* content = nsIContent::FromNodeOrNull(sel->GetFocusNode());

  Result<PeekOffsetStruct, nsresult> result =
      SelectionMovementUtils::PeekOffsetForCaretMove(
          content, sel->FocusOffset(), direction, GetHint(),
          GetCaretBidiLevel(), aAmount, desiredPos, options.unwrap(),
          ancestorLimiter.unwrap());
  nsresult rv;
  if (result.isOk() && result.inspect().mResultContent) {
    const PeekOffsetStruct& pos = result.inspect();
    nsIFrame* theFrame;
    int32_t frameStart, frameEnd;

    if (aAmount <= eSelectWordNoSpace) {
      // For left/right, PeekOffset() sets pos.mResultFrame correctly, but does
      // not set pos.mAttachForward, so determine the hint here based on the
      // result frame and offset: If we're at the end of a text frame, set the
      // hint to ASSOCIATE_BEFORE to indicate that we want the caret displayed
      // at the end of this frame, not at the beginning of the next one.
      theFrame = pos.mResultFrame;
      std::tie(frameStart, frameEnd) = theFrame->GetOffsets();
      if (frameEnd == pos.mContentOffset &&
          !(frameStart == 0 && frameEnd == 0)) {
        tHint = CaretAssociationHint::Before;
      } else {
        tHint = CaretAssociationHint::After;
      }
    } else {
      // For up/down and home/end, pos.mResultFrame might not be set correctly,
      // or not at all. In these cases, get the frame based on the content and
      // hint returned by PeekOffset().
      tHint = pos.mAttach;
      theFrame = SelectionMovementUtils::GetFrameForNodeOffset(
          pos.mResultContent, pos.mContentOffset, tHint);
      if (!theFrame) {
        return NS_ERROR_FAILURE;
      }

      std::tie(frameStart, frameEnd) = theFrame->GetOffsets();
    }

    if (context->BidiEnabled()) {
      switch (aAmount) {
        case eSelectBeginLine:
        case eSelectEndLine: {
          // In Bidi contexts, PeekOffset calculates pos.mContentOffset
          // differently depending on whether the movement is visual or logical.
          // For visual movement, pos.mContentOffset depends on the direction-
          // ality of the first/last frame on the line (theFrame), and the caret
          // directionality must correspond.
          FrameBidiData bidiData = theFrame->GetBidiData();
          SetCaretBidiLevelAndMaybeSchedulePaint(
              visualMovement ? bidiData.embeddingLevel : bidiData.baseLevel);
          break;
        }
        default:
          // If the current position is not a frame boundary, it's enough just
          // to take the Bidi level of the current frame
          if ((pos.mContentOffset != frameStart &&
               pos.mContentOffset != frameEnd) ||
              eSelectLine == aAmount) {
            SetCaretBidiLevelAndMaybeSchedulePaint(
                theFrame->GetEmbeddingLevel());
          } else {
            BidiLevelFromMove(mPresShell, pos.mResultContent,
                              pos.mContentOffset, aAmount, tHint);
          }
      }
    }
    // "pos" is on the stack, so pos.mResultContent has stack lifetime, so using
    // MOZ_KnownLive is ok.
    const FocusMode focusMode = aExtendSelection == ExtendSelection::Yes
                                    ? FocusMode::kExtendSelection
                                    : FocusMode::kCollapseToNewPoint;
    rv = TakeFocus(MOZ_KnownLive(*pos.mResultContent), pos.mContentOffset,
                   pos.mContentOffset, tHint, focusMode);
  } else if (aAmount <= eSelectWordNoSpace && direction == eDirNext &&
             aExtendSelection == ExtendSelection::No) {
    // Collapse selection if PeekOffset failed, we either
    //  1. bumped into the BRFrame, bug 207623
    //  2. had select-all in a text input (DIV range), bug 352759.
    bool isBRFrame = frameForFocus.mFrame->IsBrFrame();
    RefPtr<nsINode> node = sel->GetFocusNode();
    sel->CollapseInLimiter(node, sel->FocusOffset());
    // Note: 'frameForFocus.mFrame' might be dead here.
    if (!isBRFrame) {
      mCaret.mHint = CaretAssociationHint::Before;  // We're now at the end of
                                                    // the frame to the left.
    }
    rv = NS_OK;
  } else {
    rv = result.isErr() ? result.unwrapErr() : NS_OK;
  }
  if (NS_SUCCEEDED(rv)) {
    rv = sel->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION,
                             AxisScrollParams(), AxisScrollParams(),
                             scrollFlags);
  }

  return rv;
}

// static
Result<PeekOffsetOptions, nsresult>
nsFrameSelection::CreatePeekOffsetOptionsForCaretMove(
    const Element* aSelectionLimiter, ForceEditableRegion aForceEditableRegion,
    ExtendSelection aExtendSelection, CaretMovementStyle aMovementStyle) {
  PeekOffsetOptions options;
  // set data using aSelectionLimiter to stop on scroll views.  If we have a
  // limiter then we stop peeking when we hit scrollable views.  If no limiter
  // then just let it go ahead
  if (aSelectionLimiter) {
    options += PeekOffsetOption::StopAtScroller;
  }
  const bool visualMovement =
      Caret::IsVisualMovement(aExtendSelection, aMovementStyle);
  if (visualMovement) {
    options += PeekOffsetOption::Visual;
  }
  if (aExtendSelection == ExtendSelection::Yes) {
    options += PeekOffsetOption::Extend;
  }
  if (static_cast<bool>(aForceEditableRegion)) {
    options += PeekOffsetOption::ForceEditableRegion;
  }
  // A caret move wants paragraph peeking to resolve to a leaf frame at a block
  // boundary so the caret has a precise text position for scrolling-into-view
  // (bug 2041228). Selection paths build their own options and intentionally
  // omit this so the block container remains the boundary.
  options += PeekOffsetOption::ForCaretMove;
  return options;
}

Result<Element*, nsresult> nsFrameSelection::GetAncestorLimiterForCaretMove(
    dom::Selection* aSelection) const {
  if (!mPresShell) {
    return Err(NS_ERROR_NULL_POINTER);
  }

  MOZ_ASSERT(aSelection);
  nsIContent* content = nsIContent::FromNodeOrNull(aSelection->GetFocusNode());
  if (!content) {
    return Err(NS_ERROR_FAILURE);
  }

  MOZ_ASSERT(mPresShell->GetDocument() == content->GetComposedDoc());

  Element* ancestorLimiter = GetAncestorLimiter();
  if (aSelection->IsEditorSelection()) {
    // If the editor has not receive `focus` event, it may have not set ancestor
    // limiter.  Then, we need to compute it here for the caret move.
    if (!ancestorLimiter) {
      // Editing hosts can be nested.  Therefore, computing selection root from
      // selection range may be different from the focused editing host.
      // Therefore, we may need to use a non-closest inclusive ancestor editing
      // host of selection range container.  On the other hand, selection ranges
      // may be outside of focused editing host.  In such case, we should use
      // the closest editing host as the ancestor limiter instead.
      PresShell* const presShell = aSelection->GetPresShell();
      const Document* const doc =
          presShell ? presShell->GetDocument() : nullptr;
      if (const nsPIDOMWindowInner* const win =
              doc ? doc->GetInnerWindow() : nullptr) {
        Element* const focusedElement = win->GetFocusedElement();
        Element* closestEditingHost = nullptr;
        for (Element* element : content->InclusiveAncestorsOfType<Element>()) {
          if (element->IsEditingHost()) {
            if (!closestEditingHost) {
              closestEditingHost = element;
            }
            if (focusedElement == element) {
              ancestorLimiter = focusedElement;
              break;
            }
          }
        }
        if (!ancestorLimiter) {
          ancestorLimiter = closestEditingHost;
        }
      }
      // If it's the root element, we don't need to limit the new caret
      // position.
      if (ancestorLimiter && !ancestorLimiter->GetParent()) {
        ancestorLimiter = nullptr;
      }
    }
  }
  return ancestorLimiter;
}

nsPrevNextBidiLevels nsFrameSelection::GetPrevNextBidiLevels(
    nsIContent* aNode, uint32_t aContentOffset, bool aJumpLines) const {
  return SelectionMovementUtils::GetPrevNextBidiLevels(
      aNode, aContentOffset, mCaret.mHint, aJumpLines,
      GetAncestorLimiterOrIndependentSelectionRootElement());
}

nsresult nsFrameSelection::MaintainSelection(nsSelectionAmount aAmount) {
  const Selection& sel = NormalSelection();

  mMaintainedRange.MaintainAnchorFocusRange(sel, aAmount);

  return NS_OK;
}

void nsFrameSelection::BidiLevelFromMove(PresShell* aPresShell,
                                         nsIContent* aNode,
                                         uint32_t aContentOffset,
                                         nsSelectionAmount aAmount,
                                         CaretAssociationHint aHint) {
  switch (aAmount) {
    // Movement within the line: the new cursor Bidi level is the level of the
    // last character moved over
    case eSelectCharacter:
    case eSelectCluster:
    case eSelectWord:
    case eSelectWordNoSpace:
    case eSelectBeginLine:
    case eSelectEndLine:
    case eSelectNoAmount: {
      nsPrevNextBidiLevels levels =
          SelectionMovementUtils::GetPrevNextBidiLevels(
              aNode, aContentOffset, aHint, false,
              GetAncestorLimiterOrIndependentSelectionRootElement());

      SetCaretBidiLevelAndMaybeSchedulePaint(
          aHint == CaretAssociationHint::Before ? levels.mLevelBefore
                                                : levels.mLevelAfter);
      break;
    }
      /*
    // Up and Down: the new cursor Bidi level is the smaller of the two
    surrounding characters case eSelectLine: case eSelectParagraph:
      GetPrevNextBidiLevels(aContext, aNode, aContentOffset, &firstFrame,
    &secondFrame, &firstLevel, &secondLevel);
      aPresShell->SetCaretBidiLevelAndMaybeSchedulePaint(std::min(firstLevel,
    secondLevel)); break;
      */

    default:
      UndefineCaretBidiLevel();
  }
}

void nsFrameSelection::BidiLevelFromClick(nsIContent* aNode,
                                          uint32_t aContentOffset) {
  nsIFrame* clickInFrame = SelectionMovementUtils::GetFrameForNodeOffset(
      aNode, aContentOffset, mCaret.mHint);
  if (!clickInFrame) {
    return;
  }

  SetCaretBidiLevelAndMaybeSchedulePaint(clickInFrame->GetEmbeddingLevel());
}

void nsFrameSelection::MaintainedRange::AdjustNormalSelection(
    const nsIContent* aContent, const int32_t aOffset,
    Selection& aNormalSelection) const {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  if (!mRange || !aContent) {
    return;
  }

  nsINode* rangeStartNode = mRange->GetStartContainer();
  nsINode* rangeEndNode = mRange->GetEndContainer();
  const uint32_t rangeStartOffset = mRange->StartOffset();
  const uint32_t rangeEndOffset = mRange->EndOffset();

  NS_ASSERTION(aOffset >= 0, "aOffset should not be negative");
  const Maybe<int32_t> relToStart =
      nsContentUtils::ComparePoints_AllowNegativeOffsets<
          TreeKind::ShadowIncludingDOM>(rangeStartNode, rangeStartOffset,
                                        aContent, aOffset);
  if (NS_WARN_IF(!relToStart)) {
    // Potentially handle this properly when Selection across Shadow DOM
    // boundary is implemented
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
    return;
  }

  const Maybe<int32_t> relToEnd =
      nsContentUtils::ComparePoints_AllowNegativeOffsets<
          TreeKind::ShadowIncludingDOM>(rangeEndNode, rangeEndOffset, aContent,
                                        aOffset);
  if (NS_WARN_IF(!relToEnd)) {
    // Potentially handle this properly when Selection across Shadow DOM
    // boundary is implemented
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
    return;
  }

  // If aContent/aOffset is inside (or at the edge of) the maintained
  // selection, or if it is on the "anchor" side of the maintained selection,
  // we need to do something.
  if ((*relToStart <= 0 && *relToEnd >= 0) ||
      (*relToStart > 0 && aNormalSelection.GetDirection() == eDirNext) ||
      (*relToEnd < 0 && aNormalSelection.GetDirection() == eDirPrevious)) {
    // Ensure we hold a strong ref to the range across ReplaceAnchorFocusRange.
    const RefPtr range = mRange;
    // Set the current range to the maintained range.
    aNormalSelection.ReplaceAnchorFocusRange(range);
    // Set the direction of the selection so that the anchor will be on the
    // far side of the maintained selection, relative to aContent/aOffset.
    aNormalSelection.SetDirection(*relToStart > 0 ? eDirPrevious : eDirNext);
  }
}

void nsFrameSelection::MaintainedRange::AdjustContentOffsets(
    nsIFrame::ContentOffsets& aOffsets, StopAtScroller aStopAtScroller) const {
  // Adjust offsets according to maintained amount
  if (mRange && mAmount != eSelectNoAmount) {
    const Maybe<int32_t> relativePosition =
        nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
            mRange->StartRef(),
            RawRangeBoundary(aOffsets.content, aOffsets.offset,
                             RangeBoundarySetBy::Offset));
    if (NS_WARN_IF(!relativePosition)) {
      // Potentially handle this properly when Selection across Shadow DOM
      // boundary is implemented
      // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
      return;
    }

    nsDirection direction = *relativePosition > 0 ? eDirPrevious : eDirNext;
    nsSelectionAmount amount = mAmount;
    if (amount == eSelectBeginLine && direction == eDirNext) {
      amount = eSelectEndLine;
    }

    FrameAndOffset frameAndOffset =
        SelectionMovementUtils::GetFrameForNodeOffset(
            aOffsets.content, aOffsets.offset, CaretAssociationHint::After);

    PeekOffsetOptions peekOffsetOptions{};
    if (aStopAtScroller == StopAtScroller::Yes) {
      peekOffsetOptions += PeekOffsetOption::StopAtScroller;
    }
    if (frameAndOffset && amount == eSelectWord && direction == eDirPrevious) {
      // To avoid selecting the previous word when at start of word,
      // first move one character forward.
      PeekOffsetStruct charPos(
          eSelectCharacter, eDirNext,
          static_cast<int32_t>(frameAndOffset.mOffsetInFrameContent),
          nsPoint(0, 0), peekOffsetOptions);
      if (NS_SUCCEEDED(frameAndOffset->PeekOffset(&charPos))) {
        frameAndOffset = {charPos.mResultFrame,
                          static_cast<uint32_t>(charPos.mContentOffset)};
      }
    }

    PeekOffsetStruct pos(
        amount, direction,
        static_cast<int32_t>(frameAndOffset.mOffsetInFrameContent),
        nsPoint(0, 0), peekOffsetOptions);
    if (frameAndOffset && NS_SUCCEEDED(frameAndOffset->PeekOffset(&pos)) &&
        pos.mResultContent) {
      aOffsets.content = pos.mResultContent;
      aOffsets.offset = pos.mContentOffset;
    }
  }
}

void nsFrameSelection::MaintainedRange::MaintainAnchorFocusRange(
    const Selection& aNormalSelection, const nsSelectionAmount aAmount) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  mAmount = aAmount;

  const dom::Range* anchorFocusRange = aNormalSelection.GetAnchorFocusRange();
  if (anchorFocusRange && aAmount != eSelectNoAmount) {
    mRange = anchorFocusRange->CloneRange();
    return;
  }

  mRange = nullptr;
}

nsresult nsFrameSelection::HandleClick(nsIContent* aNewFocus,
                                       uint32_t aContentOffset,
                                       uint32_t aContentEndOffset,
                                       const FocusMode aFocusMode,
                                       CaretAssociationHint aHint) {
  if (!aNewFocus) {
    return NS_ERROR_INVALID_ARG;
  }

  if (MOZ_LOG_TEST(sFrameSelectionLog, LogLevel::Debug)) {
    const Selection& sel = NormalSelection();
    MOZ_LOG(sFrameSelectionLog, LogLevel::Debug,
            ("%s: selection=%p, new focus=%p, offsets=(%u,%u), focus mode=%i",
             __FUNCTION__, &sel, aNewFocus, aContentOffset, aContentEndOffset,
             static_cast<int>(aFocusMode)));
  }

  mDesiredCaretPos.Invalidate();

  if (aFocusMode != FocusMode::kExtendSelection) {
    mMaintainedRange.mRange = nullptr;
    if (!NodeIsInLimiters(aNewFocus)) {
      mLimiters.mAncestorLimiter = nullptr;
    }
  }

  // Don't take focus when dragging off of a table
  if (!mTableSelection.mDragSelectingCells) {
    BidiLevelFromClick(aNewFocus, aContentOffset);
    SetChangeReasons(nsISelectionListener::MOUSEDOWN_REASON +
                     nsISelectionListener::DRAG_REASON);

    RefPtr<Selection> selection = &NormalSelection();

    if (aFocusMode == FocusMode::kExtendSelection) {
      mMaintainedRange.AdjustNormalSelection(aNewFocus, aContentOffset,
                                             *selection);
    }

    AutoPrepareFocusRange prep(selection,
                               aFocusMode == FocusMode::kMultiRangeSelection);
    return TakeFocus(*aNewFocus, aContentOffset, aContentEndOffset, aHint,
                     aFocusMode);
  }

  return NS_OK;
}

void nsFrameSelection::HandleDrag(nsIFrame* aFrame, const nsPoint& aPoint) {
  if (!aFrame || !mPresShell) {
    return;
  }

  nsresult result;
  nsIFrame* newFrame = nullptr;
  nsPoint newPoint;

  result = ConstrainFrameAndPointToAnchorSubtree(aFrame, aPoint, &newFrame,
                                                 newPoint);
  if (NS_FAILED(result)) {
    return;
  }
  if (!newFrame) {
    return;
  }

  nsIFrame::ContentOffsets offsets =
      newFrame->GetContentOffsetsFromPoint(newPoint);
  if (!offsets.content) {
    return;
  }

  RefPtr<Selection> selection = &NormalSelection();
  if (newFrame->IsSelected()) {
    // `MOZ_KnownLive` required because of
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1636889.
    mMaintainedRange.AdjustNormalSelection(MOZ_KnownLive(offsets.content),
                                           offsets.offset, *selection);
  }

  mMaintainedRange.AdjustContentOffsets(
      offsets, mLimiters.mIndependentSelectionRootElement
                   ? MaintainedRange::StopAtScroller::Yes
                   : MaintainedRange::StopAtScroller::No);

  // TODO: no click has happened, rename `HandleClick`.
  HandleClick(MOZ_KnownLive(offsets.content) /* bug 1636889 */, offsets.offset,
              offsets.offset, FocusMode::kExtendSelection, offsets.associate);
}

nsresult nsFrameSelection::StartAutoScrollTimer(nsIFrame* aFrame,
                                                const nsPoint& aPoint,
                                                uint32_t aDelay) {
  RefPtr<Selection> selection = &NormalSelection();
  return selection->StartAutoScrollTimer(aFrame, aPoint, aDelay);
}

void nsFrameSelection::StopAutoScrollTimer() {
  Selection& sel = NormalSelection();
  sel.StopAutoScrollTimer();
}

// static
nsINode* nsFrameSelection::TableSelection::IsContentInActivelyEditableTableCell(
    nsPresContext* aContext, nsIContent* aContent) {
  if (!aContext) {
    return nullptr;
  }

  RefPtr<HTMLEditor> htmlEditor = nsContentUtils::GetHTMLEditor(aContext);
  if (!htmlEditor) {
    return nullptr;
  }

  nsINode* inclusiveTableCellAncestor =
      GetClosestInclusiveTableCellAncestor(aContent);
  if (!inclusiveTableCellAncestor) {
    return nullptr;
  }

  const Element* editingHost = htmlEditor->ComputeEditingHost();
  if (!editingHost) {
    return nullptr;
  }

  const bool editableCell =
      inclusiveTableCellAncestor->IsInclusiveDescendantOf(editingHost);
  return editableCell ? inclusiveTableCellAncestor : nullptr;
}

namespace {
struct ParentAndOffset {
  explicit ParentAndOffset(const nsINode& aNode)
      : mParent{aNode.GetParent()},
        mOffset{mParent ? mParent->ComputeIndexOf_Deprecated(&aNode) : 0} {}

  nsINode* mParent;

  // 0, if there's no parent.
  int32_t mOffset;
};

}  // namespace
/**
hard to go from nodes to frames, easy the other way!
 */
nsresult nsFrameSelection::TakeFocus(nsIContent& aNewFocus,
                                     uint32_t aContentOffset,
                                     uint32_t aContentEndOffset,
                                     CaretAssociationHint aHint,
                                     const FocusMode aFocusMode) {
  NS_ENSURE_STATE(mPresShell);

  if (!NodeIsInLimiters(&aNewFocus)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(sFrameSelectionLog, LogLevel::Verbose,
          ("%s: new focus=%p, offsets=(%u, %u), hint=%i, focusMode=%i",
           __FUNCTION__, &aNewFocus, aContentOffset, aContentEndOffset,
           static_cast<int>(aHint), static_cast<int>(aFocusMode)));

  mPresShell->FrameSelectionWillTakeFocus(
      *this, aNewFocus.CanStartSelectionAsWebCompatHack()
                 ? PresShell::CanMoveLastSelectionForToString::Yes
                 : PresShell::CanMoveLastSelectionForToString::No);

  // Clear all table selection data
  mTableSelection.mMode = TableSelectionMode::None;
  mTableSelection.mDragSelectingCells = false;
  mTableSelection.mStartSelectedCell = nullptr;
  mTableSelection.mEndSelectedCell = nullptr;
  mTableSelection.mAppendStartSelectedCell = nullptr;
  mCaret.mHint = aHint;

  RefPtr<Selection> selection = &NormalSelection();

  Maybe<Selection::AutoUserInitiated> userSelect;
  if (IsUserSelectionReason()) {
    userSelect.emplace(selection);
  }

  // traverse through document and unselect crap here
  switch (aFocusMode) {
    case FocusMode::kCollapseToNewPoint:
      [[fallthrough]];
    case FocusMode::kMultiRangeSelection: {
      // single click? setting cursor down
      const Batching saveBatching =
          mBatching;  // hack to use the collapse code.
      mBatching.mCounter = 1;

      if (aFocusMode == FocusMode::kMultiRangeSelection) {
        // Remove existing collapsed ranges as there's no point in having
        // non-anchor/focus collapsed ranges.
        selection->RemoveCollapsedRanges();

        ErrorResult error;
        RefPtr<dom::Range> newRange = dom::Range::Create(
            &aNewFocus, aContentOffset, &aNewFocus, aContentOffset, error);
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
        MOZ_ASSERT(newRange);
        selection->AddRangeAndSelectFramesAndNotifyListeners(*newRange,
                                                             IgnoreErrors());
      } else {
        bool oldDesiredPosSet =
            mDesiredCaretPos.mIsSet;  // need to keep old desired
                                      // position if it was set.
        selection->CollapseInLimiter(&aNewFocus, aContentOffset);
        mDesiredCaretPos.mIsSet =
            oldDesiredPosSet;  // now reset desired pos back.
      }

      mBatching = saveBatching;

      if (aContentEndOffset != aContentOffset) {
        selection->Extend(&aNewFocus, aContentEndOffset);
      }

      // find out if we are inside a table. if so, find out which one and which
      // cell once we do that, the next time we get a takefocus, check the
      // parent tree. if we are no longer inside same table ,cell then switch to
      // table selection mode. BUT only do this in an editor

      NS_ENSURE_STATE(mPresShell);
      RefPtr<nsPresContext> context = mPresShell->GetPresContext();
      mTableSelection.mClosestInclusiveTableCellAncestor = nullptr;
      if (nsINode* inclusiveTableCellAncestor =
              TableSelection::IsContentInActivelyEditableTableCell(
                  context, &aNewFocus)) {
        mTableSelection.mClosestInclusiveTableCellAncestor =
            inclusiveTableCellAncestor;
        MOZ_LOG(sFrameSelectionLog, LogLevel::Debug,
                ("%s: Collapsing into new cell", __FUNCTION__));
      }

      break;
    }
    case FocusMode::kExtendSelection: {
      // Now update the range list:
      nsCOMPtr<nsINode> inclusiveTableCellAncestor =
          GetClosestInclusiveTableCellAncestor(&aNewFocus);
      if (mTableSelection.mClosestInclusiveTableCellAncestor &&
          inclusiveTableCellAncestor &&
          inclusiveTableCellAncestor !=
              mTableSelection
                  .mClosestInclusiveTableCellAncestor)  // switch to cell
                                                        // selection mode
      {
        MOZ_LOG(sFrameSelectionLog, LogLevel::Debug,
                ("%s: moving into new cell", __FUNCTION__));

        WidgetMouseEvent event(false, eVoidEvent, nullptr,
                               WidgetMouseEvent::eReal);

        // Start selecting in the cell we were in before
        ParentAndOffset parentAndOffset{
            *mTableSelection.mClosestInclusiveTableCellAncestor};
        if (const nsCOMPtr<nsINode> previousParent = parentAndOffset.mParent) {
          const nsresult result =
              HandleTableSelection(previousParent, parentAndOffset.mOffset,
                                   TableSelectionMode::Cell, &event);
          if (NS_WARN_IF(NS_FAILED(result))) {
            return result;
          }
        }

        // Find the parent of this new cell and extend selection to it
        parentAndOffset = ParentAndOffset{*inclusiveTableCellAncestor};

        // XXXX We need to REALLY get the current key shift state
        //  (we'd need to add event listener -- let's not bother for now)
        event.mModifiers &= ~MODIFIER_SHIFT;  // aExtendSelection;
        if (const nsCOMPtr<nsINode> newParent = parentAndOffset.mParent) {
          mTableSelection.mClosestInclusiveTableCellAncestor =
              inclusiveTableCellAncestor;
          // Continue selection into next cell
          const nsresult result =
              HandleTableSelection(newParent, parentAndOffset.mOffset,
                                   TableSelectionMode::Cell, &event);
          if (NS_WARN_IF(NS_FAILED(result))) {
            return result;
          }
        }
      } else {
        // XXXX Problem: Shift+click in browser is appending text selection to
        // selected table!!!
        //   is this the place to erase selected cells ?????
        uint32_t offset =
            (selection->GetDirection() == eDirNext &&
             aContentEndOffset > aContentOffset)  // didn't go far enough
                ? aContentEndOffset  // this will only redraw the diff
                : aContentOffset;
        selection->Extend(&aNewFocus, offset);
      }
      break;
    }
  }

  // Be aware, the Selection instance may be destroyed after this call.
  return NotifySelectionListeners(SelectionType::eNormal);
}

UniquePtr<SelectionDetails> nsFrameSelection::LookUpSelection(
    nsIContent* aContent, int32_t aContentOffset, int32_t aContentLength,
    IgnoreNormalSelection aIgnoreNormalSelection) const {
  if (!aContent || !mPresShell) {
    return nullptr;
  }

  // TODO: Layout should use `uint32_t` for handling offset in DOM nodes
  //       (for example: bug 1735262)
  MOZ_ASSERT(aContentOffset >= 0);
  MOZ_ASSERT(aContentLength >= 0);
  if (MOZ_UNLIKELY(aContentOffset < 0) || MOZ_UNLIKELY(aContentLength < 0)) {
    return nullptr;
  }

  UniquePtr<SelectionDetails> details;
  for (size_t j = aIgnoreNormalSelection == IgnoreNormalSelection::Yes ? 1 : 0;
       j < std::size(mDomSelections); j++) {
    MOZ_ASSERT(mDomSelections[j]);
    details = mDomSelections[j]->LookUpSelection(
        aContent, static_cast<uint32_t>(aContentOffset),
        static_cast<uint32_t>(aContentLength), std::move(details),
        kPresentSelectionTypes[j]);
  }

  // This may seem counter intuitive at first. Highlight selections need to be
  // iterated from back to front:
  //
  //  - `mHighlightSelections` is ordered by insertion, i.e. if two or more
  //  highlights overlap, the latest must take precedence.
  //  - however, the `LookupSelection()` algorithm reverses the order by setting
  //    the current `details` as `mNext`.
  for (const auto& iter : Reversed(mHighlightSelections)) {
    details = iter.second()->LookUpSelection(
        aContent, static_cast<uint32_t>(aContentOffset),
        static_cast<uint32_t>(aContentLength), std::move(details),
        SelectionType::eHighlight);
  }

  return details;
}

void nsFrameSelection::SetDragState(bool aState) {
  if (mDragState == aState) {
    return;
  }

  mDragState = aState;

  if (!mDragState) {
    mTableSelection.mDragSelectingCells = false;
    // Notify that reason is mouse up.
    SetChangeReasons(nsISelectionListener::MOUSEUP_REASON);

    // flag is set to NotApplicable in `Selection::NotifySelectionListeners`.
    // since this function call is part of click event, this would immediately
    // reset the flag, rendering it useless.
    AutoRestore<ClickSelectionType> restoreClickSelectionType(
        mClickSelectionType);
    // Be aware, the Selection instance may be destroyed after this call.
    NotifySelectionListeners(SelectionType::eNormal);
  }
}

Selection* nsFrameSelection::GetSelection(SelectionType aSelectionType) const {
  int8_t index = GetIndexFromSelectionType(aSelectionType);
  if (index < 0) {
    return nullptr;
  }
  MOZ_ASSERT(mDomSelections[index]);
  return mDomSelections[index];
}

void nsFrameSelection::PopulateHighlightSelection(
    Selection& aSelection, mozilla::dom::Highlight& aHighlight) {
  MOZ_ASSERT(GetPresShell());
  AutoFrameSelectionBatcher selectionBatcher(__FUNCTION__);
  selectionBatcher.AddFrameSelection(this);
  const Document* doc = GetPresShell()->GetDocument();
  for (const RefPtr<AbstractRange>& range : aHighlight.Ranges()) {
    const Document* rangeDoc = range->GetComposedDocOfContainers();
    if (!rangeDoc || rangeDoc == doc) {
      // since this is run in a context guarded by a selection batcher,
      // no strong reference is needed to keep `range` alive.
      aSelection.AddHighlightRangeAndSelectFramesAndNotifyListeners(
          MOZ_KnownLive(*range));
    }
  }
}

void nsFrameSelection::AddHighlightSelection(
    nsAtom* aHighlightName, mozilla::dom::Highlight& aHighlight) {
  // Create the selection and register it in mHighlightSelections BEFORE
  // adding ranges. Adding ranges triggers paint, which queries
  // `mHighlightSelections`.
  RefPtr<Selection> selection =
      MakeRefPtr<Selection>(SelectionType::eHighlight, this);
  selection->SetHighlightSelectionData({aHighlightName, &aHighlight});
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    iter->second() = selection;
  } else {
    mHighlightSelections.AppendElement(
        CompactPair<RefPtr<nsAtom>, RefPtr<Selection>>(aHighlightName,
                                                       selection));
  }
  // Now add ranges to the registered selection.
  PopulateHighlightSelection(*selection, aHighlight);
}

void nsFrameSelection::RepaintHighlightSelection(nsAtom* aHighlightName) {
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    RefPtr selection = iter->second();
    selection->Repaint(mPresShell->GetPresContext());
  }
}

void nsFrameSelection::RemoveHighlightSelection(nsAtom* aHighlightName) {
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    RefPtr<Selection> selection = iter->second();
    selection->RemoveAllRanges(IgnoreErrors());
    mHighlightSelections.RemoveElementAt(iter);
  }
}

void nsFrameSelection::AddHighlightSelectionRange(
    nsAtom* aHighlightName, mozilla::dom::Highlight& aHighlight,
    mozilla::dom::AbstractRange& aRange) {
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    RefPtr<Selection> selection = iter->second();
    selection->AddHighlightRangeAndSelectFramesAndNotifyListeners(aRange);
  } else {
    AddHighlightSelection(aHighlightName, aHighlight);
  }
}

void nsFrameSelection::RemoveHighlightSelectionRange(
    nsAtom* aHighlightName, mozilla::dom::AbstractRange& aRange) {
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    RefPtr<Selection> selection = iter->second();
    selection->RemoveRangeAndUnselectFramesAndNotifyListeners(aRange,
                                                              IgnoreErrors());
  }
}

nsresult nsFrameSelection::ScrollSelectionIntoView(SelectionType aSelectionType,
                                                   SelectionRegion aRegion,
                                                   int16_t aFlags) const {
  RefPtr<Selection> sel = GetSelection(aSelectionType);
  if (!sel) {
    return NS_ERROR_INVALID_ARG;
  }

  const auto vScroll = [&]() -> WhereToScroll {
    if (aFlags & nsISelectionController::SCROLL_VERTICAL_START) {
      return WhereToScroll::Start;
    }
    if (aFlags & nsISelectionController::SCROLL_VERTICAL_END) {
      return WhereToScroll::End;
    }
    if (aFlags & nsISelectionController::SCROLL_VERTICAL_CENTER) {
      return WhereToScroll::Center;
    }
    return WhereToScroll::Nearest;
  }();

  auto mode = aFlags & nsISelectionController::SCROLL_SYNCHRONOUS
                  ? SelectionScrollMode::SyncFlush
                  : SelectionScrollMode::Async;

  auto scrollFlags = ScrollFlags::None;
  if (aFlags & nsISelectionController::SCROLL_OVERFLOW_HIDDEN) {
    scrollFlags |= ScrollFlags::ScrollOverflowHidden;
  }

  // After ScrollSelectionIntoView(), the pending notifications might be
  // flushed and PresShell/PresContext/Frames may be dead. See bug 418470.
  return sel->ScrollIntoView(aRegion, AxisScrollParams(vScroll),
                             AxisScrollParams(), scrollFlags, mode);
}

nsresult nsFrameSelection::RepaintSelection(SelectionType aSelectionType) {
  RefPtr<Selection> sel = GetSelection(aSelectionType);
  if (!sel) {
    return NS_ERROR_INVALID_ARG;
  }
  if (!mPresShell) {
    return NS_ERROR_UNEXPECTED;
  }

// On macOS, update the selection cache to the new active selection
// aka the current selection.
// NOTE: On Linux and Windows, we don't need to run this because this just runs
// nsHTMLCopyEncorder without updating the selection cache.  However, we run
// this in the debug builds on Linux and Windows. See the comment of this macro
// definition for the detail.
#ifdef RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION
  // Check that we're in the an active window and, if this is Web content,
  // in the frontmost tab.
  // XXX This is called when the selection blurs, see
  // PresShell::FrameSelectionWillLoseFocus(). Cannot we skip doing this in that
  // case?
  Document* doc = mPresShell->GetDocument();
  if (doc && IsInActiveTab(doc) && aSelectionType == SelectionType::eNormal) {
    MaybeUpdateSelectionCacheOnRepaintSelection(sel);
  }
#endif  // #ifdef RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION
  return sel->Repaint(mPresShell->GetPresContext());
}

nsIFrame* nsFrameSelection::GetFrameToPageSelect() const {
  if (NS_WARN_IF(!mPresShell)) {
    return nullptr;
  }

  nsIFrame* rootFrameToSelect = [&]() -> nsIFrame* {
    if (mLimiters.mIndependentSelectionRootElement) {
      return mLimiters.mIndependentSelectionRootElement->GetPrimaryFrame();
    }
    if (mLimiters.mAncestorLimiter) {
      return mLimiters.mAncestorLimiter->GetPrimaryFrame();
    }
    return mPresShell->GetRootScrollContainerFrame();
  }();

  if (NS_WARN_IF(!rootFrameToSelect)) {
    return nullptr;
  }

  // If there is selected content, and it's under our root, look for nearest
  // and vertical scrollable parent under the root frame.
  nsIFrame* innerScrollableFrame = [&]() -> nsIFrame* {
    RefPtr contentToSelect = mPresShell->GetContentForScrolling();
    if (!contentToSelect) {
      return nullptr;
    }
    nsIFrame* frame = contentToSelect->GetPrimaryFrame();
    if (!frame ||
        !nsLayoutUtils::IsProperAncestorFrame(rootFrameToSelect, frame)) {
      return nullptr;
    }
    for (; frame != rootFrameToSelect; frame = frame->GetParent()) {
      ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(frame);
      if (!scrollContainerFrame) {
        continue;
      }
      ScrollStyles scrollStyles = scrollContainerFrame->GetScrollStyles();
      if (scrollStyles.mVertical == StyleOverflow::Hidden) {
        continue;
      }
      layers::ScrollDirections directions =
          scrollContainerFrame->GetAvailableScrollingDirections();
      if (directions.contains(layers::ScrollDirection::eVertical)) {
        // If there is sub scrollable frame, let's use its page size to select.
        return frame;
      }
    }
    // Otherwise, i.e., there is no scrollable frame or only the root frame is
    // scrollable, let's return the root frame because Shift + PageUp/PageDown
    // should expand the selection in the root content even if it's not
    // scrollable.
    return nullptr;
  }();
  return innerScrollableFrame ? innerScrollableFrame : rootFrameToSelect;
}

nsresult nsFrameSelection::PageMove(bool aForward, bool aExtend,
                                    nsIFrame* aFrame,
                                    SelectionIntoView aSelectionIntoView) {
  MOZ_ASSERT(aFrame);

  if (!IsAvailable()) [[unlikely]] {
    return NS_OK;
  }

  // expected behavior for PageMove is to scroll AND move the caret
  // and remain relative position of the caret in view. see Bug 4302.

  // find out where the caret is.
  // we should know mDesiredCaretPos.mValue value of nsFrameSelection, but I
  // haven't seen that behavior in other windows applications yet.
  MOZ_DIAGNOSTIC_ASSERT(GetSelection(mozilla::SelectionType::eNormal));
  const OwningNonNull<Selection> selection = NormalSelection();

  // Get the scroll container frame.  If aFrame is not scrollable, this is
  // nullptr.
  ScrollContainerFrame* scrollContainerFrame = aFrame->GetScrollTargetFrame();
  const AutoWeakFrame scrollContainerFrameWeak(scrollContainerFrame);

  bool scrolledFrameIsInLimiter = true;
  const auto offsets = [&]()
                           MOZ_NEVER_INLINE_DEBUG -> nsIFrame::ContentOffsets {
    // Get the scrolled frame.  If aFrame is not scrollable, this is aFrame
    // itself.
    nsIFrame* scrolledFrame = scrollContainerFrame
                                  ? scrollContainerFrame->GetScrolledFrame()
                                  : aFrame;
    if (!scrolledFrame) [[unlikely]] {
      return {};
    }

    nsRect caretPos;
    nsIFrame* caretFrame = nsCaret::GetGeometry(selection, &caretPos);
    if (!caretFrame) [[unlikely]] {
      return {};
    }

    // If the scrolled frame is outside of current selection limiter,
    // we need to scroll the frame but keep moving selection in the limiter.
    nsIFrame* frameToClick = scrolledFrame;
    if (!NodeIsInLimiters(scrolledFrame->GetContent())) {
      frameToClick = GetFrameToPageSelect();
      scrolledFrameIsInLimiter = scrolledFrame == frameToClick;
      if (NS_WARN_IF(!frameToClick)) {
        return {};
      }
    }

    if (scrollContainerFrame) {
      // If there is a scrollable frame, adjust pseudo-click position with page
      // scroll amount.
      // XXX This may scroll more than one page if ScrollSelectionIntoView is
      //     called later because caret may not fully visible.  E.g., if
      //     clicking line will be visible only half height with scrolling
      //     the frame, ScrollSelectionIntoView additionally scrolls to show
      //     the caret entirely.
      if (aForward) {
        caretPos.y += scrollContainerFrame->GetPageScrollAmount().height;
      } else {
        caretPos.y -= scrollContainerFrame->GetPageScrollAmount().height;
      }
    } else {
      // Otherwise, adjust pseudo-click position with the frame size.
      if (aForward) {
        caretPos.y += frameToClick->GetSize().height;
      } else {
        caretPos.y -= frameToClick->GetSize().height;
      }
    }

    caretPos += caretFrame->GetOffsetTo(frameToClick);

    // get a content at desired location
    nsPoint desiredPoint;
    desiredPoint.x = caretPos.x;
    desiredPoint.y = caretPos.y + caretPos.height / 2;
    return frameToClick->GetContentOffsetsFromPoint(desiredPoint);
  }();
  if (!offsets.content) [[unlikely]] {
    // XXX Do we need to handle ScrollSelectionIntoView in this case?
    return NS_OK;
  }

  // First, place the caret.
  bool selectionChanged;
  {
    // We don't want any script to run until we check whether selection is
    // modified by HandleClick.
    SelectionBatcher ensureNoSelectionChangeNotifications(selection.ref(),
                                                          __FUNCTION__);

    RangeBoundary oldAnchor = selection->AnchorRef();
    RangeBoundary oldFocus = selection->FocusRef();

    const FocusMode focusMode =
        aExtend ? FocusMode::kExtendSelection : FocusMode::kCollapseToNewPoint;
    HandleClick(MOZ_KnownLive(offsets.content) /* bug 1636889 */,
                offsets.offset, offsets.offset, focusMode,
                CaretAssociationHint::After);

    selectionChanged = selection->AnchorRef() != oldAnchor ||
                       selection->FocusRef() != oldFocus;
  }

  bool doScrollSelectionIntoView = !(
      aSelectionIntoView == SelectionIntoView::IfChanged && !selectionChanged);

  // Then, scroll the given frame one page.
  if (scrollContainerFrameWeak.IsAlive()) {
    // If we'll call ScrollSelectionIntoView later and selection wasn't
    // changed and we scroll outside of selection limiter, we shouldn't use
    // smooth scroll here because ScrollContainerFrame uses normal runnable,
    // but ScrollSelectionIntoView uses early runner and it cancels the
    // pending smooth scroll.  Therefore, if we used smooth scroll in such
    // case, ScrollSelectionIntoView would scroll to show caret instead of
    // page scroll of an element outside selection limiter.
    ScrollMode scrollMode = doScrollSelectionIntoView && !selectionChanged &&
                                    !scrolledFrameIsInLimiter
                                ? ScrollMode::Instant
                                : ScrollMode::Smooth;
    MOZ_ASSERT(scrollContainerFrameWeak.GetFrame() == scrollContainerFrame);
    scrollContainerFrame->ScrollBy(nsIntPoint(0, aForward ? 1 : -1),
                                   ScrollUnit::PAGES, scrollMode);
  }

  // Finally, scroll selection into view if requested.
  if (!doScrollSelectionIntoView) {
    return NS_OK;
  }
  return ScrollSelectionIntoView(SelectionType::eNormal,
                                 nsISelectionController::SELECTION_FOCUS_REGION,
                                 nsISelectionController::SCROLL_SYNCHRONOUS);
}

nsresult nsFrameSelection::PhysicalMove(int16_t aDirection, int16_t aAmount,
                                        bool aExtend) {
  NS_ENSURE_STATE(mPresShell);
  // Flush out layout, since we need it to be up to date to do caret
  // positioning.
  OwningNonNull<PresShell> presShell(*mPresShell);
  presShell->FlushPendingNotifications(FlushType::Layout);

  if (!mPresShell) {
    return NS_OK;
  }

  // Check that parameters are safe
  if (aDirection < 0 || aDirection > 3 || aAmount < 0 || aAmount > 2) {
    return NS_ERROR_FAILURE;
  }

  nsPresContext* context = mPresShell->GetPresContext();
  if (!context) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Selection> sel = &NormalSelection();

  // Map the abstract movement amounts (0-1) to direction-specific
  // selection units.
  static const nsSelectionAmount inlineAmount[] = {eSelectCluster, eSelectWord};
  static const nsSelectionAmount blockPrevAmount[] = {eSelectLine,
                                                      eSelectBeginLine};
  static const nsSelectionAmount blockNextAmount[] = {eSelectLine,
                                                      eSelectEndLine};

  struct PhysicalToLogicalMapping {
    nsDirection direction;
    const nsSelectionAmount* amounts;
  };
  static const PhysicalToLogicalMapping verticalLR[4] = {
      {eDirPrevious, blockPrevAmount},  // left
      {eDirNext, blockNextAmount},      // right
      {eDirPrevious, inlineAmount},     // up
      {eDirNext, inlineAmount}          // down
  };
  static const PhysicalToLogicalMapping verticalRL[4] = {
      {eDirNext, blockNextAmount},
      {eDirPrevious, blockPrevAmount},
      {eDirPrevious, inlineAmount},
      {eDirNext, inlineAmount}};
  static const PhysicalToLogicalMapping horizontal[4] = {
      {eDirPrevious, inlineAmount},
      {eDirNext, inlineAmount},
      {eDirPrevious, blockPrevAmount},
      {eDirNext, blockNextAmount}};

  WritingMode wm;
  const PrimaryFrameData frameForFocus =
      sel->GetPrimaryFrameForCaretAtFocusNode(true);
  if (frameForFocus) {
    // FYI: Setting the caret association hint was done during a call of
    // GetPrimaryFrameForCaretAtFocusNode.  Therefore, this may not be intended
    // by the original author.
    sel->GetFrameSelection()->SetHint(frameForFocus.mHint);

    if (!frameForFocus->Style()->IsTextCombined()) {
      wm = frameForFocus->GetWritingMode();
    } else {
      // Using different direction for horizontal-in-vertical would
      // make it hard to navigate via keyboard. Inherit the moving
      // direction from its parent.
      MOZ_ASSERT(frameForFocus->IsTextFrame());
      wm = frameForFocus->GetParent()->GetWritingMode();
      MOZ_ASSERT(wm.IsVertical(),
                 "Text combined "
                 "can only appear in vertical text");
    }
  }

  if (aAmount == 2) {
    // Amount 2 is visual intra-line movement (move to line edge).
    // Determine the logical forward direction from the physical direction
    // and the writing mode's bidi direction, then use IntraLineMove which
    // handles eLogical movement correctly.
    bool isLeftOrUp = (aDirection == nsISelectionController::MOVE_LEFT ||
                       aDirection == nsISelectionController::MOVE_UP);
    // In LTR, left/up = backward (beginning), right/down = forward (end).
    // In RTL, left/up = forward (end), right/down = backward (beginning).
    bool forward = wm.IsBidiRTL() ? isLeftOrUp : !isLeftOrUp;
    return IntraLineMove(forward, aExtend);
  }

  MOZ_ASSERT(aAmount <= 1, "aAmount == 2 should have been handled above");

  const PhysicalToLogicalMapping& mapping =
      wm.IsVertical()
          ? wm.IsVerticalLR() ? verticalLR[aDirection] : verticalRL[aDirection]
          : horizontal[aDirection];

  nsresult rv = MoveCaret(mapping.direction, ExtendSelection(aExtend),
                          mapping.amounts[aAmount], eVisual);
  if (NS_FAILED(rv)) {
    // If we tried to do a line move, but couldn't move in the given direction,
    // then we'll "promote" this to a line-edge move instead.
    if (mapping.amounts[aAmount] == eSelectLine) {
      rv = MoveCaret(mapping.direction, ExtendSelection(aExtend),
                     mapping.amounts[aAmount + 1], eVisual);
    }
    // And if it was a next-word move that failed (which can happen when
    // eat_space_to_next_word is true, see bug 1153237), then just move forward
    // to the line-edge.
    else if (mapping.amounts[aAmount] == eSelectWord &&
             mapping.direction == eDirNext) {
      rv = MoveCaret(eDirNext, ExtendSelection(aExtend), eSelectEndLine,
                     eVisual);
    }
  }

  return rv;
}

nsresult nsFrameSelection::CharacterMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, ExtendSelection(aExtend),
                   eSelectCluster, eUsePrefStyle);
}

nsresult nsFrameSelection::WordMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, ExtendSelection(aExtend),
                   eSelectWord, eUsePrefStyle);
}

nsresult nsFrameSelection::LineMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, ExtendSelection(aExtend),
                   eSelectLine, eUsePrefStyle);
}

nsresult nsFrameSelection::IntraLineMove(bool aForward, bool aExtend) {
  if (aForward) {
    return MoveCaret(eDirNext, ExtendSelection(aExtend), eSelectEndLine,
                     eLogical);
  }
  return MoveCaret(eDirPrevious, ExtendSelection(aExtend), eSelectBeginLine,
                   eLogical);
}

nsresult nsFrameSelection::ParagraphMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, ExtendSelection(aExtend),
                   eSelectParagraph, eLogical);
}

// static
template <typename RangeType>
Result<RefPtr<RangeType>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    PresShell& aPresShell,
    const mozilla::LimitersAndCaretData& aLimitersAndCaretData,
    const AbstractRange& aRange, nsDirection aRangeDirection,
    nsDirection aExtendDirection, nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle) {
  MOZ_ASSERT(aRangeDirection == eDirNext || aRangeDirection == eDirPrevious);
  MOZ_ASSERT(aExtendDirection == eDirNext || aExtendDirection == eDirPrevious);
  MOZ_ASSERT(aAmount == eSelectCharacter || aAmount == eSelectCluster ||
             aAmount == eSelectWord || aAmount == eSelectBeginLine ||
             aAmount == eSelectEndLine);
  MOZ_ASSERT(aMovementStyle == eLogical || aMovementStyle == eVisual ||
             aMovementStyle == eUsePrefStyle);

  aPresShell.FlushPendingNotifications(FlushType::Layout);
  if (aPresShell.IsDestroying()) {
    return Err(NS_ERROR_FAILURE);
  }
  if (!aRange.IsPositioned()) {
    return Err(NS_ERROR_FAILURE);
  }
  const ForceEditableRegion forceEditableRegion = [&]() {
    if (aRange.GetStartContainer()->IsEditable()) {
      return ForceEditableRegion::Yes;
    }
    const auto* const element = Element::FromNode(aRange.GetStartContainer());
    return element && element->State().HasState(ElementState::READWRITE)
               ? ForceEditableRegion::Yes
               : ForceEditableRegion::No;
  }();
  Result<PeekOffsetOptions, nsresult> options =
      CreatePeekOffsetOptionsForCaretMove(
          aLimitersAndCaretData.mIndependentSelectionRootElement,
          forceEditableRegion, ExtendSelection::Yes, aMovementStyle);
  if (MOZ_UNLIKELY(options.isErr())) {
    return options.propagateErr();
  }
  Result<RawRangeBoundary, nsresult> result =
      SelectionMovementUtils::MoveRangeBoundaryToSomewhere(
          aRangeDirection == eDirNext ? aRange.StartRef().AsRaw()
                                      : aRange.EndRef().AsRaw(),
          aExtendDirection, aLimitersAndCaretData.mCaretAssociationHint,
          aLimitersAndCaretData.mCaretBidiLevel, aAmount, options.unwrap(),
          aLimitersAndCaretData.mAncestorLimiter);
  if (result.isErr()) {
    return result.propagateErr();
  }
  RefPtr<RangeType> range;
  RawRangeBoundary rangeBoundary = result.unwrap();
  if (!rangeBoundary.IsSetAndValid()) {
    return range;
  }
  if (aExtendDirection == eDirPrevious) {
    range = RangeType::Create(rangeBoundary, aRange.EndRef(), IgnoreErrors());
  } else {
    range = RangeType::Create(aRange.StartRef(), rangeBoundary, IgnoreErrors());
  }
  return range;
}

//////////END FRAMESELECTION

LazyLogModule gBatchLog("SelectionBatch");

void nsFrameSelection::StartBatchChanges(const char* aRequesterFuncName) {
  MOZ_LOG(gBatchLog, LogLevel::Info,
          ("%p%snsFrameSelection::StartBatchChanges(%s)", this,
           std::string((mBatching.mCounter + 1) * 2, ' ').c_str(),
           aRequesterFuncName));
  mBatching.mCounter++;
}

void nsFrameSelection::EndBatchChanges(const char* aRequesterFuncName,
                                       int16_t aReasons) {
  MOZ_LOG(gBatchLog, LogLevel::Info,
          ("%p%snsFrameSelection::EndBatchChanges  (%s, %s)", this,
           std::string(mBatching.mCounter * 2, ' ').c_str(), aRequesterFuncName,
           SelectionChangeReasonsToCString(aReasons).get()));
  MOZ_ASSERT(mBatching.mCounter > 0, "Bad mBatching.mCounter");
  mBatching.mCounter--;

  if (mBatching.mCounter == 0) {
    AddChangeReasons(aReasons);
    mCaretMoveAmount = eSelectNoAmount;
    // Be aware, the Selection instance may be destroyed after this call,
    // hence make sure that this instance remains until the end of this call.
    RefPtr frameSelection = this;
    for (auto selectionType : kPresentSelectionTypes) {
      // This returns NS_ERROR_FAILURE if being called for a selection that is
      // not present. We don't care about that here, so we silently ignore it
      // and continue.
      (void)NotifySelectionListeners(selectionType, IsBatchingEnd::Yes);
    }
  }
}

nsresult nsFrameSelection::NotifySelectionListeners(
    SelectionType aSelectionType, IsBatchingEnd aEndBatching) {
  if (RefPtr<Selection> selection = GetSelection(aSelectionType)) {
    if (aEndBatching == IsBatchingEnd::Yes &&
        !selection->ChangesDuringBatching()) {
      return NS_OK;
    }
    selection->NotifySelectionListeners();
    mCaretMoveAmount = eSelectNoAmount;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

// Start of Table Selection methods

static bool IsCell(nsIContent* aContent) {
  return aContent->IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th);
}

// static
nsITableCellLayout* nsFrameSelection::GetCellLayout(
    const nsIContent* aCellContent) {
  nsITableCellLayout* cellLayoutObject =
      do_QueryFrame(aCellContent->GetPrimaryFrame());
  return cellLayoutObject;
}

nsresult nsFrameSelection::ClearNormalSelection() {
  RefPtr<Selection> selection = &NormalSelection();
  ErrorResult err;
  selection->RemoveAllRanges(err);
  return err.StealNSResult();
}

static nsIContent* GetFirstSelectedContent(const dom::Range* aRange) {
  if (!aRange) {
    return nullptr;
  }

  MOZ_ASSERT(aRange->GetStartContainer(), "Must have start parent!");
  MOZ_ASSERT(aRange->GetStartContainer()->IsElement(), "Unexpected parent");

  return aRange->GetChildAtStartOffset();
}

// Table selection support.
// TODO: Separate table methods into a separate nsITableSelection interface
nsresult nsFrameSelection::HandleTableSelection(nsINode* aParentContent,
                                                int32_t aContentOffset,
                                                TableSelectionMode aTarget,
                                                WidgetMouseEvent* aMouseEvent) {
  RefPtr<Selection> selection = &NormalSelection();
  return mTableSelection.HandleSelection(aParentContent, aContentOffset,
                                         aTarget, aMouseEvent, mDragState,
                                         *selection);
}

nsresult nsFrameSelection::TableSelection::HandleSelection(
    nsINode* aParentContent, int32_t aContentOffset, TableSelectionMode aTarget,
    WidgetMouseEvent* aMouseEvent, bool aDragState,
    Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  NS_ENSURE_TRUE(aParentContent, NS_ERROR_NULL_POINTER);
  NS_ENSURE_TRUE(aMouseEvent, NS_ERROR_NULL_POINTER);

  if (aDragState && mDragSelectingCells &&
      aTarget == TableSelectionMode::Table) {
    // We were selecting cells and user drags mouse in table border or inbetween
    // cells,
    //  just do nothing
    return NS_OK;
  }

  RefPtr<nsIContent> childContent =
      aParentContent->GetChildAt_Deprecated(aContentOffset);

  // When doing table selection, always set the direction to next so
  // we can be sure that anchorNode's offset always points to the
  // selected cell
  aNormalSelection.SetDirection(eDirNext);

  // Stack-class to wrap all table selection changes in
  //  BeginBatchChanges() / EndBatchChanges()
  SelectionBatcher selectionBatcher(&aNormalSelection, __FUNCTION__);

  if (aDragState && mDragSelectingCells) {
    return HandleDragSelecting(aTarget, childContent, aMouseEvent,
                               aNormalSelection);
  }

  return HandleMouseUpOrDown(aTarget, aDragState, childContent, aParentContent,
                             aContentOffset, aMouseEvent, aNormalSelection);
}

class nsFrameSelection::TableSelection::RowAndColumnRelation {
 public:
  static Result<RowAndColumnRelation, nsresult> Create(
      const nsIContent* aFirst, const nsIContent* aSecond) {
    RowAndColumnRelation result;

    nsresult errorResult =
        GetCellIndexes(aFirst, result.mFirst.mRow, result.mFirst.mColumn);
    if (NS_FAILED(errorResult)) {
      return Err(errorResult);
    }

    errorResult =
        GetCellIndexes(aSecond, result.mSecond.mRow, result.mSecond.mColumn);
    if (NS_FAILED(errorResult)) {
      return Err(errorResult);
    }

    return result;
  }

  bool IsSameColumn() const { return mFirst.mColumn == mSecond.mColumn; }

  bool IsSameRow() const { return mFirst.mRow == mSecond.mRow; }

 private:
  RowAndColumnRelation() = default;

  struct RowAndColumn {
    int32_t mRow = 0;
    int32_t mColumn = 0;
  };

  RowAndColumn mFirst;
  RowAndColumn mSecond;
};

nsresult nsFrameSelection::TableSelection::HandleDragSelecting(
    TableSelectionMode aTarget, nsIContent* aChildContent,
    const WidgetMouseEvent* aMouseEvent, Selection& aNormalSelection) {
  // We are drag-selecting
  if (aTarget != TableSelectionMode::Table) {
    // If dragging in the same cell as last event, do nothing
    if (mEndSelectedCell == aChildContent) {
      return NS_OK;
    }

#ifdef DEBUG_TABLE_SELECTION
    printf(
        " mStartSelectedCell = %p, "
        "mEndSelectedCell = %p, aChildContent = %p "
        "\n",
        mStartSelectedCell.get(), mEndSelectedCell.get(), aChildContent);
#endif
    // aTarget can be any "cell mode",
    //  so we can easily drag-select rows and columns
    // Once we are in row or column mode,
    //  we can drift into any cell to stay in that mode
    //  even if aTarget = TableSelectionMode::Cell

    if (mMode == TableSelectionMode::Row ||
        mMode == TableSelectionMode::Column) {
      if (mEndSelectedCell) {
        Result<RowAndColumnRelation, nsresult> rowAndColumnRelation =
            RowAndColumnRelation::Create(mEndSelectedCell, aChildContent);

        if (rowAndColumnRelation.isErr()) {
          return rowAndColumnRelation.unwrapErr();
        }

        if ((mMode == TableSelectionMode::Row &&
             rowAndColumnRelation.inspect().IsSameRow()) ||
            (mMode == TableSelectionMode::Column &&
             rowAndColumnRelation.inspect().IsSameColumn())) {
          return NS_OK;
        }
      }
#ifdef DEBUG_TABLE_SELECTION
      printf(" Dragged into a new column or row\n");
#endif
      // Continue dragging row or column selection

      return SelectRowOrColumn(aChildContent, aNormalSelection);
    }
    if (mMode == TableSelectionMode::Cell) {
#ifdef DEBUG_TABLE_SELECTION
      printf("HandleTableSelection: Dragged into a new cell\n");
#endif
      // Trick for quick selection of rows and columns
      // Hold down shift, then start selecting in one direction
      // If next cell dragged into is in same row, select entire row,
      //   if next cell is in same column, select entire column
      if (mStartSelectedCell && aMouseEvent->IsShift()) {
        Result<RowAndColumnRelation, nsresult> rowAndColumnRelation =
            RowAndColumnRelation::Create(mStartSelectedCell, aChildContent);
        if (rowAndColumnRelation.isErr()) {
          return rowAndColumnRelation.unwrapErr();
        }

        if (rowAndColumnRelation.inspect().IsSameRow() ||
            rowAndColumnRelation.inspect().IsSameColumn()) {
          // Force new selection block
          mStartSelectedCell = nullptr;
          aNormalSelection.RemoveAllRanges(IgnoreErrors());

          if (rowAndColumnRelation.inspect().IsSameRow()) {
            mMode = TableSelectionMode::Row;
          } else {
            mMode = TableSelectionMode::Column;
          }

          return SelectRowOrColumn(aChildContent, aNormalSelection);
        }
      }

      // Reselect block of cells to new end location
      const nsCOMPtr<nsIContent> startSelectedCell = mStartSelectedCell;
      return SelectBlockOfCells(startSelectedCell, aChildContent,
                                aNormalSelection);
    }
  }
  // Do nothing if dragging in table, but outside a cell
  return NS_OK;
}

nsresult nsFrameSelection::TableSelection::HandleMouseUpOrDown(
    TableSelectionMode aTarget, bool aDragState, nsIContent* aChildContent,
    nsINode* aParentContent, int32_t aContentOffset,
    const WidgetMouseEvent* aMouseEvent, Selection& aNormalSelection) {
  nsresult result = NS_OK;
  // Not dragging  -- mouse event is down or up
  if (aDragState) {
#ifdef DEBUG_TABLE_SELECTION
    printf("HandleTableSelection: Mouse down event\n");
#endif
    // Clear cell we stored in mouse-down
    mUnselectCellOnMouseUp = nullptr;

    if (aTarget == TableSelectionMode::Cell) {
      bool isSelected = false;

      // Check if we have other selected cells
      nsIContent* previousCellNode =
          GetFirstSelectedContent(GetFirstCellRange(aNormalSelection));
      if (previousCellNode) {
        // We have at least 1 other selected cell

        // Check if new cell is already selected
        nsIFrame* cellFrame = aChildContent->GetPrimaryFrame();
        if (!cellFrame) {
          return NS_ERROR_NULL_POINTER;
        }
        isSelected = cellFrame->IsSelected();
      } else {
        // No cells selected -- remove non-cell selection
        aNormalSelection.RemoveAllRanges(IgnoreErrors());
      }
      mDragSelectingCells = true;  // Signal to start drag-cell-selection
      mMode = aTarget;
      // Set start for new drag-selection block (not appended)
      mStartSelectedCell = aChildContent;
      // The initial block end is same as the start
      mEndSelectedCell = aChildContent;

      if (isSelected) {
        // Remember this cell to (possibly) unselect it on mouseup
        mUnselectCellOnMouseUp = aChildContent;
#ifdef DEBUG_TABLE_SELECTION
        printf(
            "HandleTableSelection: Saving "
            "mUnselectCellOnMouseUp\n");
#endif
      } else {
        // Select an unselected cell
        // but first remove existing selection if not in same table
        if (previousCellNode &&
            !IsInSameTable(previousCellNode, aChildContent)) {
          aNormalSelection.RemoveAllRanges(IgnoreErrors());
          // Reset selection mode that is cleared in RemoveAllRanges
          mMode = aTarget;
        }

        return ::SelectCellElement(aChildContent, aNormalSelection);
      }

      return NS_OK;
    }
    if (aTarget == TableSelectionMode::Table) {
      // TODO: We currently select entire table when clicked between cells,
      //  should we restrict to only around border?
      //  *** How do we get location data for cell and click?
      mDragSelectingCells = false;
      mStartSelectedCell = nullptr;
      mEndSelectedCell = nullptr;

      // Remove existing selection and select the table
      aNormalSelection.RemoveAllRanges(IgnoreErrors());
      return CreateAndAddRange(aParentContent, aContentOffset,
                               aNormalSelection);
    }
    if (aTarget == TableSelectionMode::Row ||
        aTarget == TableSelectionMode::Column) {
#ifdef DEBUG_TABLE_SELECTION
      printf("aTarget == %d\n", aTarget);
#endif

      // Start drag-selecting mode so multiple rows/cols can be selected
      // Note: Currently, nsIFrame::GetDataForTableSelection
      //       will never call us for row or column selection on mouse down
      mDragSelectingCells = true;

      // Force new selection block
      mStartSelectedCell = nullptr;
      aNormalSelection.RemoveAllRanges(IgnoreErrors());
      // Always do this AFTER RemoveAllRanges
      mMode = aTarget;

      return SelectRowOrColumn(aChildContent, aNormalSelection);
    }
  } else {
#ifdef DEBUG_TABLE_SELECTION
    printf(
        "HandleTableSelection: Mouse UP event. "
        "mDragSelectingCells=%d, "
        "mStartSelectedCell=%p\n",
        mDragSelectingCells, mStartSelectedCell.get());
#endif
    // First check if we are extending a block selection
    const uint32_t rangeCount = aNormalSelection.RangeCount();

    if (rangeCount > 0 && aMouseEvent->IsShift() && mAppendStartSelectedCell &&
        mAppendStartSelectedCell != aChildContent) {
      // Shift key is down: append a block selection
      mDragSelectingCells = false;

      const OwningNonNull<nsIContent> appendStartSelectedCell =
          *mAppendStartSelectedCell;
      return SelectBlockOfCells(appendStartSelectedCell, aChildContent,
                                aNormalSelection);
    }

    if (mDragSelectingCells) {
      mAppendStartSelectedCell = mStartSelectedCell;
    }

    mDragSelectingCells = false;
    mStartSelectedCell = nullptr;
    mEndSelectedCell = nullptr;

    // Any other mouseup actions require that Ctrl or Cmd key is pressed
    //  else stop table selection mode
    bool doMouseUpAction = false;
#ifdef XP_MACOSX
    doMouseUpAction = aMouseEvent->IsMeta();
#else
    doMouseUpAction = aMouseEvent->IsControl();
#endif
    if (!doMouseUpAction) {
#ifdef DEBUG_TABLE_SELECTION
      printf(
          "HandleTableSelection: Ending cell selection on mouseup: "
          "mAppendStartSelectedCell=%p\n",
          mAppendStartSelectedCell.get());
#endif
      return NS_OK;
    }
    // Unselect a cell only if it wasn't
    //  just selected on mousedown
    if (aChildContent == mUnselectCellOnMouseUp) {
      // Scan ranges to find the cell to unselect (the selection range to
      // remove)
      // XXXbz it's really weird that this lives outside the loop, so once we
      // find one we keep looking at it even if we find no more cells...
      nsINode* previousCellParent = nullptr;
#ifdef DEBUG_TABLE_SELECTION
      printf(
          "HandleTableSelection: Unselecting "
          "mUnselectCellOnMouseUp; "
          "rangeCount=%d\n",
          rangeCount);
#endif
      for (const uint32_t i : IntegerRange(rangeCount)) {
        MOZ_ASSERT(aNormalSelection.RangeCount() == rangeCount);
        // Strong reference, because sometimes we want to remove
        // this range, and then we might be the only owner.
        RefPtr<dom::Range> range = aNormalSelection.GetRangeAt(i);
        if (MOZ_UNLIKELY(!range)) {
          return NS_ERROR_NULL_POINTER;
        }

        nsINode* container = range->GetStartContainer();
        if (!container) {
          return NS_ERROR_NULL_POINTER;
        }

        int32_t offset = range->StartOffset();
        // Be sure previous selection is a table cell
        nsIContent* child = range->GetChildAtStartOffset();
        if (child && IsCell(child)) {
          previousCellParent = container;
        }

        // We're done if we didn't find parent of a previously-selected cell
        if (!previousCellParent) {
          break;
        }

        if (previousCellParent == aParentContent && offset == aContentOffset) {
          // Cell is already selected
          if (rangeCount == 1) {
#ifdef DEBUG_TABLE_SELECTION
            printf("HandleTableSelection: Unselecting single selected cell\n");
#endif
            // This was the only cell selected.
            // Collapse to "normal" selection inside the cell
            mStartSelectedCell = nullptr;
            mEndSelectedCell = nullptr;
            mAppendStartSelectedCell = nullptr;
            // TODO: We need a "Collapse to just before deepest child" routine
            // Even better, should we collapse to just after the LAST deepest
            // child
            //  (i.e., at the end of the cell's contents)?
            return aNormalSelection.CollapseInLimiter(aChildContent, 0);
          }
#ifdef DEBUG_TABLE_SELECTION
          printf(
              "HandleTableSelection: Removing cell from multi-cell "
              "selection\n");
#endif
          // Unselecting the start of previous block
          // XXX What do we use now!
          if (aChildContent == mAppendStartSelectedCell) {
            mAppendStartSelectedCell = nullptr;
          }

          // Deselect cell by removing its range from selection
          ErrorResult err;
          aNormalSelection.RemoveRangeAndUnselectFramesAndNotifyListeners(
              *range, err);
          return err.StealNSResult();
        }
      }
      mUnselectCellOnMouseUp = nullptr;
    }
  }
  return result;
}

nsresult nsFrameSelection::TableSelection::SelectBlockOfCells(
    nsIContent* aStartCell, nsIContent* aEndCell, Selection& aNormalSelection) {
  NS_ENSURE_TRUE(aStartCell, NS_ERROR_NULL_POINTER);
  NS_ENSURE_TRUE(aEndCell, NS_ERROR_NULL_POINTER);
  mEndSelectedCell = aEndCell;

  nsresult result = NS_OK;

  // If new end cell is in a different table, do nothing
  const RefPtr<const nsIContent> table = IsInSameTable(aStartCell, aEndCell);
  if (!table) {
    return NS_OK;
  }

  // Get starting and ending cells' location in the cellmap
  int32_t startRowIndex, startColIndex, endRowIndex, endColIndex;
  result = GetCellIndexes(aStartCell, startRowIndex, startColIndex);
  if (NS_FAILED(result)) {
    return result;
  }
  result = GetCellIndexes(aEndCell, endRowIndex, endColIndex);
  if (NS_FAILED(result)) {
    return result;
  }

  if (mDragSelectingCells) {
    // Drag selecting: remove selected cells outside of new block limits
    // TODO: `UnselectCells`'s return value shouldn't be ignored.
    UnselectCells(table, startRowIndex, startColIndex, endRowIndex, endColIndex,
                  true, aNormalSelection);
  }

  // Note that we select block in the direction of user's mouse dragging,
  //  which means start cell may be after the end cell in either row or column
  return AddCellsToSelection(table, startRowIndex, startColIndex, endRowIndex,
                             endColIndex, aNormalSelection);
}

nsresult nsFrameSelection::TableSelection::UnselectCells(
    const nsIContent* aTableContent, int32_t aStartRowIndex,
    int32_t aStartColumnIndex, int32_t aEndRowIndex, int32_t aEndColumnIndex,
    bool aRemoveOutsideOfCellRange, mozilla::dom::Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  // This is needed as an nsTableWrapperFrame as we use this to know we can
  // static cast tableFrame to nsTableWrapperFrame.
  nsTableWrapperFrame* tableFrameCast =
      do_QueryFrame(aTableContent->GetPrimaryFrame());
  // Declared as weak frame, as the frame may be freed during a call to a
  // listener.
  AutoWeakFrame tableFrame(tableFrameCast);
  if (!tableFrame) {
    return NS_ERROR_FAILURE;
  }

  int32_t minRowIndex = std::min(aStartRowIndex, aEndRowIndex);
  int32_t maxRowIndex = std::max(aStartRowIndex, aEndRowIndex);
  int32_t minColIndex = std::min(aStartColumnIndex, aEndColumnIndex);
  int32_t maxColIndex = std::max(aStartColumnIndex, aEndColumnIndex);

  // Strong reference because we sometimes remove the range
  RefPtr<dom::Range> range = GetFirstCellRange(aNormalSelection);
  nsIContent* cellNode = GetFirstSelectedContent(range);
  MOZ_ASSERT(!range || cellNode, "Must have cellNode if had a range");

  int32_t curRowIndex, curColIndex;
  while (cellNode) {
    nsresult result = GetCellIndexes(cellNode, curRowIndex, curColIndex);
    if (NS_FAILED(result)) {
      return result;
    }

#ifdef DEBUG_TABLE_SELECTION
    if (!range) printf("RemoveCellsToSelection -- range is null\n");
#endif

    if (range) {
      if (aRemoveOutsideOfCellRange) {
        if (curRowIndex < minRowIndex || curRowIndex > maxRowIndex ||
            curColIndex < minColIndex || curColIndex > maxColIndex) {
          aNormalSelection.RemoveRangeAndUnselectFramesAndNotifyListeners(
              *range, IgnoreErrors());
          if (!tableFrame.IsAlive()) {
            return NS_OK;
          }
          // Since we've removed the range, decrement pointer to next range
          mSelectedCellIndex--;
        }

      } else {
        // Remove cell from selection if it belongs to the given cells range or
        // it is spanned onto the cells range.

        MOZ_ASSERT(tableFrame.IsAlive());
        // In general, static casting to another table type is not a good idea,
        // but it is valid here due to the earlier do_QueryFrame() that
        // established that tableFrame is an nsTableWrapperFrame*.
        auto tempTableFrame =
            static_cast<nsTableWrapperFrame*>(tableFrame.GetFrame());
        nsTableCellFrame* cellFrame =
            tempTableFrame->GetCellFrameAt(curRowIndex, curColIndex);

        uint32_t origRowIndex = cellFrame->RowIndex();
        uint32_t origColIndex = cellFrame->ColIndex();
        uint32_t actualRowSpan =
            tempTableFrame->GetEffectiveRowSpanAt(origRowIndex, origColIndex);
        uint32_t actualColSpan =
            tempTableFrame->GetEffectiveColSpanAt(curRowIndex, curColIndex);
        if (origRowIndex <= static_cast<uint32_t>(maxRowIndex) &&
            maxRowIndex >= 0 &&
            origRowIndex + actualRowSpan - 1 >=
                static_cast<uint32_t>(minRowIndex) &&
            origColIndex <= static_cast<uint32_t>(maxColIndex) &&
            maxColIndex >= 0 &&
            origColIndex + actualColSpan - 1 >=
                static_cast<uint32_t>(minColIndex)) {
          aNormalSelection.RemoveRangeAndUnselectFramesAndNotifyListeners(
              *range, IgnoreErrors());
          if (!tableFrame.IsAlive()) {
            return NS_OK;
          }
          // Since we've removed the range, decrement pointer to next range
          mSelectedCellIndex--;
        }
      }
    }

    range = GetNextCellRange(aNormalSelection);
    cellNode = GetFirstSelectedContent(range);
    MOZ_ASSERT(!range || cellNode, "Must have cellNode if had a range");
  }

  return NS_OK;
}

nsresult SelectCellElement(nsIContent* aCellElement,
                           Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  nsIContent* parent = aCellElement->GetParent();

  // Get child offset
  const int32_t offset = parent->ComputeIndexOf_Deprecated(aCellElement);

  return CreateAndAddRange(parent, offset, aNormalSelection);
}

static nsresult AddCellsToSelection(const nsIContent* aTableContent,
                                    int32_t aStartRowIndex,
                                    int32_t aStartColumnIndex,
                                    int32_t aEndRowIndex,
                                    int32_t aEndColumnIndex,
                                    Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  // This is needed as an nsTableWrapperFrame as we use this to know we can
  // static cast tableFrame to nsTableWrapperFrame.
  nsTableWrapperFrame* tableFrameCast =
      do_QueryFrame(aTableContent->GetPrimaryFrame());
  // Declared as weak frame, as the frame may be freed during a call to a
  // listener.
  AutoWeakFrame tableFrame(tableFrameCast);
  if (!tableFrame) {  // Check that |table| is a table.
    return NS_ERROR_FAILURE;
  }

  nsresult result = NS_OK;
  uint32_t row = aStartRowIndex;
  while (true) {
    uint32_t col = aStartColumnIndex;
    while (true) {
      MOZ_ASSERT(tableFrame.IsAlive());
      // In general, static casting to another table type is not a good idea,
      // but it is valid here due to the earlier do_QueryFrame() that
      // established that tableFrame is an nsTableWrapperFrame*.
      auto cellFrame = static_cast<nsTableWrapperFrame*>(tableFrame.GetFrame())
                           ->GetCellFrameAt(row, col);

      // Skip cells that are spanned from previous locations or are already
      // selected
      if (cellFrame) {
        uint32_t origRow = cellFrame->RowIndex();
        uint32_t origCol = cellFrame->ColIndex();
        if (origRow == row && origCol == col && !cellFrame->IsSelected()) {
          result = SelectCellElement(cellFrame->GetContent(), aNormalSelection);
          if (NS_FAILED(result) || !tableFrame.IsAlive()) {
            return result;
          }
        }
      }
      // Done when we reach end column
      if (col == static_cast<uint32_t>(aEndColumnIndex)) {
        break;
      }

      if (aStartColumnIndex < aEndColumnIndex) {
        col++;
      } else {
        col--;
      }
    }
    if (row == static_cast<uint32_t>(aEndRowIndex)) {
      break;
    }

    if (aStartRowIndex < aEndRowIndex) {
      row++;
    } else {
      row--;
    }
  }
  return result;
}

nsresult nsFrameSelection::RemoveCellsFromSelection(nsIContent* aTable,
                                                    int32_t aStartRowIndex,
                                                    int32_t aStartColumnIndex,
                                                    int32_t aEndRowIndex,
                                                    int32_t aEndColumnIndex) {
  const RefPtr<Selection> selection = &NormalSelection();
  return mTableSelection.UnselectCells(aTable, aStartRowIndex,
                                       aStartColumnIndex, aEndRowIndex,
                                       aEndColumnIndex, false, *selection);
}

nsresult nsFrameSelection::RestrictCellsToSelection(nsIContent* aTable,
                                                    int32_t aStartRowIndex,
                                                    int32_t aStartColumnIndex,
                                                    int32_t aEndRowIndex,
                                                    int32_t aEndColumnIndex) {
  const RefPtr<Selection> selection = &NormalSelection();
  return mTableSelection.UnselectCells(aTable, aStartRowIndex,
                                       aStartColumnIndex, aEndRowIndex,
                                       aEndColumnIndex, true, *selection);
}

Result<nsFrameSelection::TableSelection::FirstAndLastCell, nsresult>
nsFrameSelection::TableSelection::FindFirstAndLastCellOfRowOrColumn(
    const nsIContent& aCellContent) const {
  const nsIContent* table = GetParentTable(&aCellContent);
  if (!table) {
    return Err(NS_ERROR_NULL_POINTER);
  }

  // Get table and cell layout interfaces to access
  // cell data based on cellmap location
  // Frames are not ref counted, so don't use an nsCOMPtr
  nsTableWrapperFrame* tableFrame = do_QueryFrame(table->GetPrimaryFrame());
  if (!tableFrame) {
    return Err(NS_ERROR_FAILURE);
  }
  nsITableCellLayout* cellLayout = GetCellLayout(&aCellContent);
  if (!cellLayout) {
    return Err(NS_ERROR_FAILURE);
  }

  // Get location of target cell:
  int32_t rowIndex, colIndex;
  nsresult result = cellLayout->GetCellIndexes(rowIndex, colIndex);
  if (NS_FAILED(result)) {
    return Err(result);
  }

  // Be sure we start at proper beginning
  // (This allows us to select row or col given ANY cell!)
  if (mMode == TableSelectionMode::Row) {
    colIndex = 0;
  }
  if (mMode == TableSelectionMode::Column) {
    rowIndex = 0;
  }

  FirstAndLastCell firstAndLastCell;
  while (true) {
    // Loop through all cells in column or row to find first and last
    nsCOMPtr<nsIContent> curCellContent =
        tableFrame->GetCellAt(rowIndex, colIndex);
    if (!curCellContent) {
      break;
    }

    if (!firstAndLastCell.mFirst) {
      firstAndLastCell.mFirst = curCellContent;
    }

    firstAndLastCell.mLast = std::move(curCellContent);

    // Move to next cell in cellmap, skipping spanned locations
    if (mMode == TableSelectionMode::Row) {
      colIndex += tableFrame->GetEffectiveRowSpanAt(rowIndex, colIndex);
    } else {
      rowIndex += tableFrame->GetEffectiveRowSpanAt(rowIndex, colIndex);
    }
  }
  return firstAndLastCell;
}

nsresult nsFrameSelection::TableSelection::SelectRowOrColumn(
    nsIContent* aCellContent, Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  if (!aCellContent) {
    return NS_ERROR_NULL_POINTER;
  }

  Result<FirstAndLastCell, nsresult> firstAndLastCell =
      FindFirstAndLastCellOfRowOrColumn(*aCellContent);
  if (firstAndLastCell.isErr()) {
    return firstAndLastCell.unwrapErr();
  }

  // Use SelectBlockOfCells:
  // This will replace existing selection,
  //  but allow unselecting by dragging out of selected region
  if (firstAndLastCell.inspect().mFirst && firstAndLastCell.inspect().mLast) {
    nsresult rv{NS_OK};

    if (!mStartSelectedCell) {
      // We are starting a new block, so select the first cell
      rv = ::SelectCellElement(firstAndLastCell.inspect().mFirst,
                               aNormalSelection);
      if (NS_FAILED(rv)) {
        return rv;
      }
      mStartSelectedCell = firstAndLastCell.inspect().mFirst;
    }

    const nsCOMPtr<nsIContent> startSelectedCell = mStartSelectedCell;
    rv = SelectBlockOfCells(startSelectedCell,
                            MOZ_KnownLive(firstAndLastCell.inspect().mLast),
                            aNormalSelection);

    // This gets set to the cell at end of row/col,
    //   but we need it to be the cell under cursor
    mEndSelectedCell = aCellContent;
    return rv;
  }

#if 0
// This is a more efficient strategy that appends row to current selection,
//  but doesn't allow dragging OFF of an existing selection to unselect!
  do {
    // Loop through all cells in column or row
    result = tableLayout->GetCellDataAt(rowIndex, colIndex,
                                        getter_AddRefs(cellElement),
                                        curRowIndex, curColIndex,
                                        rowSpan, colSpan,
                                        actualRowSpan, actualColSpan,
                                        isSelected);
    if (NS_FAILED(result)) return result;
    // We're done when cell is not found
    if (!cellElement) break;


    // Check spans else we infinitely loop
    NS_ASSERTION(actualColSpan, "actualColSpan is 0!");
    NS_ASSERTION(actualRowSpan, "actualRowSpan is 0!");

    // Skip cells that are already selected or span from outside our region
    if (!isSelected && rowIndex == curRowIndex && colIndex == curColIndex)
    {
      result = SelectCellElement(cellElement);
      if (NS_FAILED(result)) return result;
    }
    // Move to next row or column in cellmap, skipping spanned locations
    if (mMode == TableSelectionMode::Row)
      colIndex += actualColSpan;
    else
      rowIndex += actualRowSpan;
  }
  while (cellElement);
#endif

  return NS_OK;
}

// static
nsIContent* nsFrameSelection::GetFirstCellNodeInRange(
    const dom::Range* aRange) {
  if (!aRange) {
    return nullptr;
  }

  nsIContent* childContent = aRange->GetChildAtStartOffset();
  if (!childContent) {
    return nullptr;
  }
  // Don't return node if not a cell
  if (!IsCell(childContent)) {
    return nullptr;
  }

  return childContent;
}

dom::Range* nsFrameSelection::TableSelection::GetFirstCellRange(
    const mozilla::dom::Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  dom::Range* firstRange = aNormalSelection.GetRangeAt(0);
  if (!GetFirstCellNodeInRange(firstRange)) {
    return nullptr;
  }

  // Setup for next cell
  mSelectedCellIndex = 1;

  return firstRange;
}

dom::Range* nsFrameSelection::TableSelection::GetNextCellRange(
    const mozilla::dom::Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  dom::Range* range =
      aNormalSelection.GetRangeAt(AssertedCast<uint32_t>(mSelectedCellIndex));

  // Get first node in next range of selection - test if it's a cell
  if (!GetFirstCellNodeInRange(range)) {
    return nullptr;
  }

  // Setup for next cell
  mSelectedCellIndex++;

  return range;
}

// static
nsresult nsFrameSelection::GetCellIndexes(const nsIContent* aCell,
                                          int32_t& aRowIndex,
                                          int32_t& aColIndex) {
  if (!aCell) {
    return NS_ERROR_NULL_POINTER;
  }

  aColIndex = 0;  // initialize out params
  aRowIndex = 0;

  nsITableCellLayout* cellLayoutObject = GetCellLayout(aCell);
  if (!cellLayoutObject) {
    return NS_ERROR_FAILURE;
  }
  return cellLayoutObject->GetCellIndexes(aRowIndex, aColIndex);
}

// static
nsIContent* nsFrameSelection::IsInSameTable(const nsIContent* aContent1,
                                            const nsIContent* aContent2) {
  if (!aContent1 || !aContent2) {
    return nullptr;
  }

  nsIContent* tableNode1 = GetParentTable(aContent1);
  nsIContent* tableNode2 = GetParentTable(aContent2);

  // Must be in the same table.  Note that we want to return false for
  // the test if both tables are null.
  return (tableNode1 == tableNode2) ? tableNode1 : nullptr;
}

// static
nsIContent* nsFrameSelection::GetParentTable(const nsIContent* aCell) {
  if (!aCell) {
    return nullptr;
  }

  for (nsIContent* parent = aCell->GetParent(); parent;
       parent = parent->GetParent()) {
    if (parent->IsHTMLElement(nsGkAtoms::table)) {
      return parent;
    }
  }

  return nullptr;
}

nsresult nsFrameSelection::SelectCellElement(nsIContent* aCellElement) {
  const RefPtr<Selection> selection = &NormalSelection();
  return ::SelectCellElement(aCellElement, *selection);
}

nsresult CreateAndAddRange(nsINode* aContainer, int32_t aOffset,
                           Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  if (!aContainer) {
    return NS_ERROR_NULL_POINTER;
  }

  // Set range around child at given offset
  ErrorResult error;
  RefPtr<dom::Range> range =
      dom::Range::Create(aContainer, aOffset, aContainer, aOffset + 1, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  MOZ_ASSERT(range);

  ErrorResult err;
  aNormalSelection.AddRangeAndSelectFramesAndNotifyListeners(*range, err);
  return err.StealNSResult();
}

// End of Table Selection

void nsFrameSelection::SetAncestorLimiter(Element* aLimiter) {
  if (mLimiters.mAncestorLimiter != aLimiter) {
    mLimiters.mAncestorLimiter = aLimiter;
    const Selection& sel = NormalSelection();
    LogSelectionAPI(&sel, __FUNCTION__, "aLimiter", aLimiter);

    const bool hasOutOfBoundsRanges = [&]() {
      if (!mLimiters.HasLimiters()) {
        return false;
      }
      for (const uint32_t i : IntegerRange(sel.RangeCount())) {
        const auto* range = sel.GetRangeAt(i);
        MOZ_ASSERT(range);
        if (!RangeInLimiters(*range)) {
          NS_WARNING(fmt::format("{} (index: {}) is not in the limiters {}",
                                 RefPtr{range}, i, mLimiters)
                         .c_str());
          return true;
        }
      }
      return false;
    }();

    if (hasOutOfBoundsRanges) {
      ClearNormalSelection();
      if (mLimiters.mAncestorLimiter) {
        SetChangeReasons(nsISelectionListener::NO_REASON);
        nsCOMPtr<nsIContent> limiter(mLimiters.mAncestorLimiter);
        const nsresult rv =
            TakeFocus(*limiter, 0, 0, CaretAssociationHint::Before,
                      FocusMode::kCollapseToNewPoint);
        (void)NS_WARN_IF(NS_FAILED(rv));
        // TODO: in case of failure, propagate it to the callers.
      }
    }
  }
}

void nsFrameSelection::SetDelayedCaretData(WidgetMouseEvent* aMouseEvent) {
  if (aMouseEvent) {
    mDelayedMouseEvent.mIsValid = true;
    mDelayedMouseEvent.mIsShift = aMouseEvent->IsShift();
    mDelayedMouseEvent.mClickCount = aMouseEvent->mClickCount;
  } else {
    mDelayedMouseEvent.mIsValid = false;
  }
}

void nsFrameSelection::DisconnectFromPresShell() {
  if (mAccessibleCaretEnabled) {
    Selection& sel = NormalSelection();
    sel.StopNotifyingAccessibleCaretEventHub();
  }

  StopAutoScrollTimer();
  for (size_t i = 0; i < std::size(mDomSelections); i++) {
    MOZ_ASSERT(mDomSelections[i]);
    mDomSelections[i]->Clear(nullptr);
  }

  if (auto* presshell = mPresShell) {
    if (const nsFrameSelection* sel = presshell->GetLastSelectionForToString();
        sel == this) {
      presshell->UpdateLastSelectionForToString(nullptr);
    }
    mPresShell = nullptr;
  }
}

#ifdef RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION
/**
 * See Bug 1288453.
 *
 * Update the selection cache on repaint to handle when a pre-existing
 * selection becomes active aka the current selection.
 *
 * 1. Change the current selection by click n dragging another selection.
 *   - Make a selection on content page. Make a selection in a text editor.
 *   - You can click n drag the content selection to make it active again.
 * 2. Change the current selection when switching to a tab with a selection.
 *   - Make selection in tab.
 *   - Switching tabs will make its respective selection active.
 *
 * Therefore, we only update the selection cache on a repaint
 * if the current selection being repainted is not an empty selection.
 *
 * If the current selection is empty. The current selection cache
 * would be cleared by AutoCopyListener::OnSelectionChange().
 */
static nsresult MaybeUpdateSelectionCacheOnRepaintSelection(Selection* aSel) {
  PresShell* presShell = aSel->GetPresShell();
  if (!presShell) {
    return NS_OK;
  }
  nsCOMPtr<Document> aDoc = presShell->GetDocument();

  if (aDoc && aSel && !aSel->IsCollapsed()) {
    return nsCopySupport::EncodeDocumentWithContextAndPutToClipboard(
        aSel, aDoc, nsIClipboard::kSelectionCache, false,
#  ifdef XP_MACOSX
        // Update the selection cache on macOS
        nsCopySupport::UpdateClipboard::Yes
#  else
        // Do not update the clipboard on the other platforms, just run the
        // serializer to detect assertion failures.
        nsCopySupport::UpdateClipboard::No
#  endif
    );
  }

  return NS_OK;
}
#endif  // RUN_MAYBE_UPDATE_SELECTION_CACHE_REPAINT_SELECTION

// mozilla::AutoCopyListener

/*
 * What we do now:
 * On every selection change, we copy to the clipboard anew, creating a
 * HTML buffer, a transferable, an nsISupportsString and
 * a huge mess every time.  This is basically what
 * nsCopySupport::EncodeDocumentWithContextAndPutToClipboard() does to move the
 * selection into the clipboard for Edit->Copy.
 *
 * What we should do, to make our end of the deal faster:
 * Create a singleton transferable with our own magic converter.  When selection
 * changes (use a quick cache to detect ``real'' changes), we put the new
 * Selection in the transferable.  Our magic converter will take care of
 * transferable->whatever-other-format when the time comes to actually
 * hand over the clipboard contents.
 *
 * Other issues:
 * - which X clipboard should we populate?
 * - should we use a different one than Edit->Copy, so that inadvertant
 *   selections (or simple clicks, which currently cause a selection
 *   notification, regardless of if they're in the document which currently has
 *   selection!) don't lose the contents of the ``application''?  Or should we
 *   just put some intelligence in the ``is this a real selection?'' code to
 *   protect our selection against clicks in other documents that don't create
 *   selections?
 * - maybe we should just never clear the X clipboard?  That would make this
 *   problem just go away, which is very tempting.
 *
 * On macOS,
 * nsIClipboard::kSelectionCache is the flag for current selection cache.
 * Set the current selection cache on the parent process in
 * widget cocoa nsClipboard whenever selection changes.
 */

// static
void AutoCopyListener::OnSelectionChange(Document* aDocument,
                                         Selection& aSelection,
                                         int16_t aReason) {
  MOZ_ASSERT(IsEnabled());

  // For now, we should prevent any updates caused by a call of Selection API.
  // We should allow this in some cases later, though. See the valid usage in
  // bug 1567160.
  if (aReason & nsISelectionListener::JS_REASON) {
    return;
  }

  if (sClipboardID == nsIClipboard::kSelectionCache) {
    // Do nothing if this isn't in the active window and,
    // in the case of Web content, in the frontmost tab.
    if (!aDocument || !IsInActiveTab(aDocument)) {
      return;
    }
  }

  static const int16_t kResasonsToHandle =
      nsISelectionListener::MOUSEUP_REASON |
      nsISelectionListener::SELECTALL_REASON |
      nsISelectionListener::KEYPRESS_REASON;
  if (!(aReason & kResasonsToHandle)) {
    return;  // Don't care if we are still dragging.
  }

  if (!aDocument ||
      aSelection.AreNormalAndCrossShadowBoundaryRangesCollapsed()) {
#ifdef DEBUG_CLIPBOARD
    fprintf(stderr, "CLIPBOARD: no selection/collapsed selection\n");
#endif
    if (sClipboardID != nsIClipboard::kSelectionCache) {
      // XXX Should we clear X clipboard?
      return;
    }

    // If on macOS, clear the current selection transferable cached
    // on the parent process (nsClipboard) when the selection is empty.
    DebugOnly<nsresult> rv = nsCopySupport::ClearSelectionCache();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "nsCopySupport::ClearSelectionCache() failed");
    return;
  }

  DebugOnly<nsresult> rv =
      nsCopySupport::EncodeDocumentWithContextAndPutToClipboard(
          &aSelection, aDocument, sClipboardID, false);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "nsCopySupport::EncodeDocumentWithContextAndPutToClipboard() failed");
}
