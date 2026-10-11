/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of nsFrameSelection
 */

#include "nsFrameSelection.h"

#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Logging.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_bidi.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/Unused.h"

#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsString.h"
#include "nsISelectionListener.h"
#include "nsContentCID.h"
#include "nsDeviceContext.h"
#include "nsIContent.h"
#include "nsRange.h"
#include "nsITableCellLayout.h"
#include "nsTArray.h"
#include "nsTableWrapperFrame.h"
#include "nsTableCellFrame.h"
#include "nsIScrollableFrame.h"
#include "nsCCUncollectableMarker.h"
#include "nsTextFragment.h"
#include <algorithm>
#include "nsContentUtils.h"
#include "nsCSSFrameConstructor.h"

#include "nsGkAtoms.h"
#include "nsIFrameTraversal.h"
#include "nsLayoutUtils.h"
#include "nsLayoutCID.h"
#include "nsBidiPresUtils.h"
static NS_DEFINE_CID(kFrameTraversalCID, NS_FRAMETRAVERSAL_CID);
#include "nsTextFrame.h"

#include "nsThreadUtils.h"
#include "mozilla/Preferences.h"

#include "mozilla/PresShell.h"
#include "nsPresContext.h"
#include "nsCaret.h"

#include "mozilla/MouseEvents.h"
#include "mozilla/TextEvents.h"

// notifications
#include "mozilla/dom/Document.h"

#include "nsISelectionController.h"  //for the enums
#include "nsCopySupport.h"
#include "nsIClipboard.h"
#include "nsIFrameInlines.h"

#include "nsError.h"
#include "mozilla/AutoCopyListener.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Highlight.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/Text.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/SelectionBinding.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Telemetry.h"

#include "nsFocusManager.h"
#include "nsPIDOMWindow.h"

using namespace mozilla;
using namespace mozilla::dom;

static LazyLogModule sFrameSelectionLog("FrameSelection");

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

#ifdef XP_MACOSX
static nsresult UpdateSelectionCacheOnRepaintSelection(Selection* aSel);
#endif  // XP_MACOSX

#ifdef PRINT_RANGE
static void printRange(nsRange* aDomRange);
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

PeekOffsetStruct::PeekOffsetStruct(nsSelectionAmount aAmount,
                                   nsDirection aDirection, int32_t aStartOffset,
                                   nsPoint aDesiredCaretPos,
                                   const PeekOffsetOptions aOptions,
                                   EWordMovementType aWordMovementType)
    : mAmount(aAmount),
      mDirection(aDirection),
      mStartOffset(aStartOffset),
      mDesiredCaretPos(aDesiredCaretPos),
      mWordMovementType(aWordMovementType),
      mOptions(aOptions),
      mResultContent(),
      mResultFrame(nullptr),
      mContentOffset(0),
      mAttach(CARET_ASSOCIATE_BEFORE) {}

}  // namespace mozilla

// Array which contains index of each SelecionType in Selection::mDOMSelections.
// For avoiding using if nor switch to retrieve the index, this needs to have
// -1 for SelectionTypes which won't be created its Selection instance.
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
    -1,  // SelectionType::eHighlight
};

inline int8_t GetIndexFromSelectionType(SelectionType aSelectionType) {
  // The enum value of eInvalid is -1 and the others are sequential value
  // starting from 0.  Therefore, |SelectionType + 1| is the index of
  // kIndexOfSelections.
  return kIndexOfSelections[static_cast<int8_t>(aSelectionType) + 1];
}

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
bool nsFrameSelection::IsValidSelectionPoint(nsINode* aNode) const {
  if (!aNode) {
    return false;
  }

  nsIContent* limiter = GetLimiter();
  if (limiter && limiter != aNode && limiter != aNode->GetParent()) {
    // if newfocus == the limiter. that's ok. but if not there and not parent
    // bad
    return false;  // not in the right content. tLimiter said so
  }

  limiter = GetAncestorLimiter();
  return !limiter || aNode->IsInclusiveDescendantOf(limiter);
}

namespace mozilla {
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

    nsTArray<StyledRange>& ranges = aSelection->mStyledRanges.mRanges;
    if (!aSelection->mUserInitiated || aMultiRangeSelection) {
      // Scripted command or the user is starting a new explicit multi-range
      // selection.
      for (StyledRange& entry : ranges) {
        MOZ_ASSERT(entry.mRange->IsDynamicRange());
        entry.mRange->AsDynamicRange()->SetIsGenerated(false);
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
    nsRange* const newAnchorFocusRange =
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
  static nsRange* FindGeneratedRangeMostDistantFromAnchor(
      const Selection& aSelection) {
    const nsTArray<StyledRange>& ranges = aSelection.mStyledRanges.mRanges;
    const size_t len = ranges.Length();
    nsRange* result{nullptr};
    if (aSelection.GetDirection() == eDirNext) {
      for (size_t i = 0; i < len; ++i) {
        // This function is only called for selections with type == eNormal.
        // (see MOZ_ASSERT in constructor).
        // Therefore, all ranges must be dynamic.
        if (ranges[i].mRange->AsDynamicRange()->IsGenerated()) {
          result = ranges[i].mRange->AsDynamicRange();
          break;
        }
      }
    } else {
      size_t i = len;
      while (i--) {
        if (ranges[i].mRange->AsDynamicRange()->IsGenerated()) {
          result = ranges[i].mRange->AsDynamicRange();
          break;
        }
      }
    }

    return result;
  }

  static void RemoveGeneratedRanges(Selection& aSelection) {
    RefPtr<nsPresContext> presContext = aSelection.GetPresContext();
    nsTArray<StyledRange>& ranges = aSelection.mStyledRanges.mRanges;
    size_t i = ranges.Length();
    while (i--) {
      // This function is only called for selections with type == eNormal.
      // (see MOZ_ASSERT in constructor).
      // Therefore, all ranges must be dynamic.
      if (!ranges[i].mRange->IsDynamicRange()) {
        continue;
      }
      nsRange* range = ranges[i].mRange->AsDynamicRange();
      if (range->IsGenerated()) {
        range->UnregisterSelection(aSelection);
        aSelection.SelectFrames(presContext, *range, false);
        ranges.RemoveElementAt(i);
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

template Result<RefPtr<nsRange>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    nsDirection aDirection, const nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle);
template Result<RefPtr<StaticRange>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    nsDirection aDirection, const nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle);

nsFrameSelection::nsFrameSelection(PresShell* aPresShell, nsIContent* aLimiter,
                                   const bool aAccessibleCaretEnabled) {
  for (size_t i = 0; i < ArrayLength(mDomSelections); i++) {
    mDomSelections[i] = new Selection(kPresentSelectionTypes[i], this);
  }

#ifdef XP_MACOSX
  // On macOS, cache the current selection to send to service menu of macOS.
  bool enableAutoCopy = true;
  AutoCopyListener::Init(nsIClipboard::kSelectionCache);
#else   // #ifdef XP_MACOSX
  // Check to see if the auto-copy pref is enabled and make the normal
  // Selection notifies auto-copy listener of its changes.
  bool enableAutoCopy = AutoCopyListener::IsPrefEnabled();
  if (enableAutoCopy) {
    AutoCopyListener::Init(nsIClipboard::kSelectionClipboard);
  }
#endif  // #ifdef XP_MACOSX #else

  if (enableAutoCopy) {
    int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
    if (mDomSelections[index]) {
      mDomSelections[index]->NotifyAutoCopy();
    }
  }

  mPresShell = aPresShell;
  mDragState = false;
  mLimiters.mLimiter = aLimiter;

  // This should only ever be initialized on the main thread, so we are OK here.
  MOZ_ASSERT(NS_IsMainThread());

  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);

  mAccessibleCaretEnabled = aAccessibleCaretEnabled;
  if (mAccessibleCaretEnabled) {
    mDomSelections[index]->MaybeNotifyAccessibleCaretEventHub(aPresShell);
  }

  if (mDomSelections[index]) {
    mDomSelections[index]->EnableSelectionChangeEvent();
  }
}

nsFrameSelection::~nsFrameSelection() = default;

NS_IMPL_CYCLE_COLLECTION_CLASS(nsFrameSelection)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsFrameSelection)
  for (size_t i = 0; i < ArrayLength(tmp->mDomSelections); ++i) {
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
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLimiters.mLimiter)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLimiters.mAncestorLimiter)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsFrameSelection)
  if (tmp->mPresShell && tmp->mPresShell->GetDocument() &&
      nsCCUncollectableMarker::InGeneration(
          cb, tmp->mPresShell->GetDocument()->GetMarkedCCGeneration())) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  for (size_t i = 0; i < ArrayLength(tmp->mDomSelections); ++i) {
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
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLimiters.mLimiter)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLimiters.mAncestorLimiter)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

bool nsFrameSelection::Caret::IsVisualMovement(
    bool aContinueSelection, CaretMovementStyle aMovementStyle) const {
  int32_t movementFlag = StaticPrefs::bidi_edit_caret_movement_style();
  return aMovementStyle == eVisual ||
         (aMovementStyle == eUsePrefStyle &&
          (movementFlag == 1 || (movementFlag == 2 && !aContinueSelection)));
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

  RefPtr<nsCaret> caret = aPresShell.GetCaret();
  if (!caret) {
    return NS_ERROR_NULL_POINTER;
  }

  caret->SetSelection(&aNormalSelection);

  nsRect coord;
  nsIFrame* caretFrame = caret->GetGeometry(&coord);
  if (!caretFrame) {
    return NS_ERROR_FAILURE;
  }
  nsPoint viewOffset(0, 0);
  nsView* view = nullptr;
  caretFrame->GetOffsetFromView(viewOffset, &view);
  if (view) {
    coord += viewOffset;
  }
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

  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  if (!mDomSelections[index]) {
    return NS_ERROR_NULL_POINTER;
  }

  nsCOMPtr<nsIContent> anchorContent =
      do_QueryInterface(mDomSelections[index]->GetAnchorNode());
  if (!anchorContent) {
    return NS_ERROR_FAILURE;
  }

  //
  // Now find the root of the subtree containing the anchor's content.
  //

  NS_ENSURE_STATE(mPresShell);
  RefPtr<PresShell> presShell = mPresShell;
  nsIContent* anchorRoot = anchorContent->GetSelectionRootContent(presShell);
  NS_ENSURE_TRUE(anchorRoot, NS_ERROR_UNEXPECTED);

  //
  // Now find the root of the subtree containing aFrame's content.
  //

  nsCOMPtr<nsIContent> content = aFrame->GetContent();

  if (content) {
    nsIContent* contentRoot = content->GetSelectionRootContent(presShell);
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
        nsIContent* cursorContentRoot =
            cursorContent->GetSelectionRootContent(presShell);
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
  if (mPresShell && (caret = mPresShell->GetCaret())) {
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
void printRange(nsRange* aDomRange) {
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
  if (!aDomNode) return nullptr;
  nsINode* current = aDomNode;
  // Start with current node and look for a table cell
  while (current) {
    nsAtom* tag = GetTag(current);
    if (tag == nsGkAtoms::td || tag == nsGkAtoms::th) return current;
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
                                     bool aContinueSelection,
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

  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  const RefPtr<Selection> sel = mDomSelections[index];
  if (!sel) {
    return NS_ERROR_NULL_POINTER;
  }

  int32_t scrollFlags = Selection::SCROLL_FOR_CARET_MOVE;
  if (sel->IsEditorSelection()) {
    // If caret moves in editor, it should cause scrolling even if it's in
    // overflow: hidden;.
    scrollFlags |= Selection::SCROLL_OVERFLOW_HIDDEN;
  }

  const bool doCollapse = [&] {
    if (sel->IsCollapsed() || aContinueSelection) {
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
      mCaret.mHint = CARET_ASSOCIATE_AFTER;
    } else {
      SetChangeReasons(nsISelectionListener::COLLAPSETOEND_REASON);
      mCaret.mHint = CARET_ASSOCIATE_BEFORE;
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
      mCaret.IsVisualMovement(aContinueSelection, aMovementStyle);
  nsIFrame* frame = sel->GetPrimaryFrameForFocusNode(visualMovement);
  if (!frame) {
    return NS_ERROR_FAILURE;
  }

  Result<bool, nsresult> isIntraLineCaretMove = IsIntraLineCaretMove(aAmount);
  nsDirection direction{aDirection};
  if (isIntraLineCaretMove.isErr()) {
    return isIntraLineCaretMove.unwrapErr();
  }
  if (isIntraLineCaretMove.inspect()) {
    // Forget old caret position for moving caret to different line since
    // caret position may be changed.
    mDesiredCaretPos.Invalidate();
    direction = GetCaretDirection(*frame, aDirection, visualMovement);
  }

  if (doCollapse) {
    const nsRange* anchorFocusRange = sel->GetAnchorFocusRange();
    if (anchorFocusRange) {
      RefPtr<nsINode> node;
      uint32_t offset;
      if (visualMovement && nsBidiPresUtils::IsReversedDirectionFrame(frame)) {
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
                        ScrollAxis(), ScrollAxis(), scrollFlags);
    return NS_OK;
  }

  CaretAssociateHint tHint(mCaret.mHint);  // temporary variable so we dont set
                                           // mCaret.mHint until it is necessary

  Result<PeekOffsetStruct, nsresult> result = PeekOffsetForCaretMove(
      direction, aContinueSelection, aAmount, aMovementStyle, desiredPos);
  nsresult rv;
  if (result.isOk() && result.inspect().mResultContent) {
    const PeekOffsetStruct& pos = result.inspect();
    nsIFrame* theFrame;
    int32_t currentOffset, frameStart, frameEnd;

    if (aAmount <= eSelectWordNoSpace) {
      // For left/right, PeekOffset() sets pos.mResultFrame correctly, but does
      // not set pos.mAttachForward, so determine the hint here based on the
      // result frame and offset: If we're at the end of a text frame, set the
      // hint to ASSOCIATE_BEFORE to indicate that we want the caret displayed
      // at the end of this frame, not at the beginning of the next one.
      theFrame = pos.mResultFrame;
      std::tie(frameStart, frameEnd) = theFrame->GetOffsets();
      currentOffset = pos.mContentOffset;
      if (frameEnd == currentOffset && !(frameStart == 0 && frameEnd == 0))
        tHint = CARET_ASSOCIATE_BEFORE;
      else
        tHint = CARET_ASSOCIATE_AFTER;
    } else {
      // For up/down and home/end, pos.mResultFrame might not be set correctly,
      // or not at all. In these cases, get the frame based on the content and
      // hint returned by PeekOffset().
      tHint = pos.mAttach;
      theFrame = GetFrameForNodeOffset(pos.mResultContent, pos.mContentOffset,
                                       tHint, &currentOffset);
      if (!theFrame) return NS_ERROR_FAILURE;

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
    const FocusMode focusMode = aContinueSelection
                                    ? FocusMode::kExtendSelection
                                    : FocusMode::kCollapseToNewPoint;
    rv = TakeFocus(MOZ_KnownLive(*pos.mResultContent), pos.mContentOffset,
                   pos.mContentOffset, tHint, focusMode);
  } else if (aAmount <= eSelectWordNoSpace && direction == eDirNext &&
             !aContinueSelection) {
    // Collapse selection if PeekOffset failed, we either
    //  1. bumped into the BRFrame, bug 207623
    //  2. had select-all in a text input (DIV range), bug 352759.
    bool isBRFrame = frame->IsBrFrame();
    RefPtr<nsINode> node = sel->GetFocusNode();
    sel->CollapseInLimiter(node, sel->FocusOffset());
    // Note: 'frame' might be dead here.
    if (!isBRFrame) {
      mCaret.mHint = CARET_ASSOCIATE_BEFORE;  // We're now at the end of the
                                              // frame to the left.
    }
    rv = NS_OK;
  } else {
    rv = result.isErr() ? result.unwrapErr() : NS_OK;
  }
  if (NS_SUCCEEDED(rv)) {
    rv = sel->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION,
                             ScrollAxis(), ScrollAxis(), scrollFlags);
  }

  return rv;
}

Result<PeekOffsetStruct, nsresult> nsFrameSelection::PeekOffsetForCaretMove(
    nsDirection aDirection, bool aContinueSelection,
    const nsSelectionAmount aAmount, CaretMovementStyle aMovementStyle,
    const nsPoint& aDesiredCaretPos) const {
  Selection* selection =
      mDomSelections[GetIndexFromSelectionType(SelectionType::eNormal)];
  if (!selection) {
    return Err(NS_ERROR_NULL_POINTER);
  }

  const bool visualMovement =
      mCaret.IsVisualMovement(aContinueSelection, aMovementStyle);

  int32_t offsetused = 0;
  nsIFrame* frame =
      selection->GetPrimaryFrameForFocusNode(visualMovement, &offsetused);
  if (!frame) {
    return Err(NS_ERROR_FAILURE);
  }

  // set data using mLimiters.mLimiter to stop on scroll views.  If we have a
  // limiter then we stop peeking when we hit scrollable views.  If no limiter
  // then just let it go ahead
  PeekOffsetOptions options{PeekOffsetOption::JumpLines,
                            PeekOffsetOption::IsKeyboardSelect};
  if (mLimiters.mLimiter) {
    options += PeekOffsetOption::ScrollViewStop;
  }
  if (visualMovement) {
    options += PeekOffsetOption::Visual;
  }
  if (aContinueSelection) {
    options += PeekOffsetOption::Extend;
  }
  if (selection->IsEditorSelection()) {
    options += PeekOffsetOption::ForceEditableRegion;
  }
  PeekOffsetStruct pos(aAmount, aDirection, offsetused, aDesiredCaretPos,
                       options);
  nsresult rv = frame->PeekOffset(&pos);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }
  return pos;
}

nsPrevNextBidiLevels nsFrameSelection::GetPrevNextBidiLevels(
    nsIContent* aNode, uint32_t aContentOffset, bool aJumpLines) const {
  return GetPrevNextBidiLevels(aNode, aContentOffset, mCaret.mHint, aJumpLines);
}

// static
nsPrevNextBidiLevels nsFrameSelection::GetPrevNextBidiLevels(
    nsIContent* aNode, uint32_t aContentOffset, CaretAssociateHint aHint,
    bool aJumpLines) {
  // Get the level of the frames on each side
  nsIFrame* currentFrame;
  int32_t currentOffset;
  nsDirection direction;

  nsPrevNextBidiLevels levels{};
  levels.SetData(nullptr, nullptr, mozilla::intl::BidiEmbeddingLevel::LTR(),
                 mozilla::intl::BidiEmbeddingLevel::LTR());

  currentFrame = GetFrameForNodeOffset(
      aNode, static_cast<int32_t>(aContentOffset), aHint, &currentOffset);
  if (!currentFrame) {
    return levels;
  }

  auto [frameStart, frameEnd] = currentFrame->GetOffsets();

  if (0 == frameStart && 0 == frameEnd) {
    direction = eDirPrevious;
  } else if (frameStart == currentOffset) {
    direction = eDirPrevious;
  } else if (frameEnd == currentOffset) {
    direction = eDirNext;
  } else {
    // we are neither at the beginning nor at the end of the frame, so we have
    // no worries
    mozilla::intl::BidiEmbeddingLevel currentLevel =
        currentFrame->GetEmbeddingLevel();
    levels.SetData(currentFrame, currentFrame, currentLevel, currentLevel);
    return levels;
  }

  PeekOffsetOptions peekOffsetOptions{PeekOffsetOption::ScrollViewStop};
  if (aJumpLines) {
    peekOffsetOptions += PeekOffsetOption::JumpLines;
  }
  nsIFrame* newFrame =
      currentFrame->GetFrameFromDirection(direction, peekOffsetOptions).mFrame;

  FrameBidiData currentBidi = currentFrame->GetBidiData();
  mozilla::intl::BidiEmbeddingLevel currentLevel = currentBidi.embeddingLevel;
  mozilla::intl::BidiEmbeddingLevel newLevel =
      newFrame ? newFrame->GetEmbeddingLevel() : currentBidi.baseLevel;

  // If not jumping lines, disregard br frames, since they might be positioned
  // incorrectly.
  // XXX This could be removed once bug 339786 is fixed.
  if (!aJumpLines) {
    if (currentFrame->IsBrFrame()) {
      currentFrame = nullptr;
      currentLevel = currentBidi.baseLevel;
    }
    if (newFrame && newFrame->IsBrFrame()) {
      newFrame = nullptr;
      newLevel = currentBidi.baseLevel;
    }
  }

  if (direction == eDirNext)
    levels.SetData(currentFrame, newFrame, currentLevel, newLevel);
  else
    levels.SetData(newFrame, currentFrame, newLevel, currentLevel);

  return levels;
}

nsresult nsFrameSelection::GetFrameFromLevel(
    nsIFrame* aFrameIn, nsDirection aDirection,
    mozilla::intl::BidiEmbeddingLevel aBidiLevel, nsIFrame** aFrameOut) const {
  NS_ENSURE_STATE(mPresShell);
  mozilla::intl::BidiEmbeddingLevel foundLevel =
      mozilla::intl::BidiEmbeddingLevel::LTR();
  nsIFrame* foundFrame = aFrameIn;

  nsCOMPtr<nsIFrameEnumerator> frameTraversal;
  nsresult result;
  nsCOMPtr<nsIFrameTraversal> trav(
      do_CreateInstance(kFrameTraversalCID, &result));
  if (NS_FAILED(result)) return result;

  result =
      trav->NewFrameTraversal(getter_AddRefs(frameTraversal),
                              mPresShell->GetPresContext(), aFrameIn, eLeaf,
                              false,  // aVisual
                              false,  // aLockInScrollView
                              false,  // aFollowOOFs
                              false   // aSkipPopupChecks
      );
  if (NS_FAILED(result)) return result;

  do {
    *aFrameOut = foundFrame;
    foundFrame = frameTraversal->Traverse(aDirection == eDirNext);
    if (!foundFrame) return NS_ERROR_FAILURE;
    foundLevel = foundFrame->GetEmbeddingLevel();

  } while (foundLevel > aBidiLevel);

  return NS_OK;
}

nsresult nsFrameSelection::MaintainSelection(nsSelectionAmount aAmount) {
  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  if (!mDomSelections[index]) {
    return NS_ERROR_NULL_POINTER;
  }

  mMaintainedRange.MaintainAnchorFocusRange(*mDomSelections[index], aAmount);

  return NS_OK;
}

void nsFrameSelection::BidiLevelFromMove(PresShell* aPresShell,
                                         nsIContent* aNode,
                                         uint32_t aContentOffset,
                                         nsSelectionAmount aAmount,
                                         CaretAssociateHint aHint) {
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
          GetPrevNextBidiLevels(aNode, aContentOffset, aHint, false);

      SetCaretBidiLevelAndMaybeSchedulePaint(aHint == CARET_ASSOCIATE_BEFORE
                                                 ? levels.mLevelBefore
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
  nsIFrame* clickInFrame = nullptr;
  int32_t OffsetNotUsed;

  clickInFrame = GetFrameForNodeOffset(aNode, aContentOffset, mCaret.mHint,
                                       &OffsetNotUsed);
  if (!clickInFrame) return;

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
      nsContentUtils::ComparePoints_AllowNegativeOffsets(
          rangeStartNode, rangeStartOffset, aContent, aOffset);
  if (NS_WARN_IF(!relToStart)) {
    // Potentially handle this properly when Selection across Shadow DOM
    // boundary is implemented
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=1607497).
    return;
  }

  const Maybe<int32_t> relToEnd =
      nsContentUtils::ComparePoints_AllowNegativeOffsets(
          rangeEndNode, rangeEndOffset, aContent, aOffset);
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
    nsIFrame::ContentOffsets& aOffsets, const bool aScrollViewStop) const {
  // Adjust offsets according to maintained amount
  if (mRange && mAmount != eSelectNoAmount) {
    nsINode* rangenode = mRange->GetStartContainer();
    int32_t rangeOffset = mRange->StartOffset();
    const Maybe<int32_t> relativePosition = nsContentUtils::ComparePoints(
        rangenode, rangeOffset, aOffsets.content, aOffsets.offset);
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

    int32_t offset;
    nsIFrame* frame = GetFrameForNodeOffset(aOffsets.content, aOffsets.offset,
                                            CARET_ASSOCIATE_AFTER, &offset);

    PeekOffsetOptions peekOffsetOptions{};
    if (aScrollViewStop) {
      peekOffsetOptions += PeekOffsetOption::ScrollViewStop;
    }
    if (frame && amount == eSelectWord && direction == eDirPrevious) {
      // To avoid selecting the previous word when at start of word,
      // first move one character forward.
      PeekOffsetStruct charPos(eSelectCharacter, eDirNext, offset,
                               nsPoint(0, 0), peekOffsetOptions);
      if (NS_SUCCEEDED(frame->PeekOffset(&charPos))) {
        frame = charPos.mResultFrame;
        offset = charPos.mContentOffset;
      }
    }

    PeekOffsetStruct pos(amount, direction, offset, nsPoint(0, 0),
                         peekOffsetOptions);
    if (frame && NS_SUCCEEDED(frame->PeekOffset(&pos)) && pos.mResultContent) {
      aOffsets.content = pos.mResultContent;
      aOffsets.offset = pos.mContentOffset;
    }
  }
}

void nsFrameSelection::MaintainedRange::MaintainAnchorFocusRange(
    const Selection& aNormalSelection, const nsSelectionAmount aAmount) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  mAmount = aAmount;

  const nsRange* anchorFocusRange = aNormalSelection.GetAnchorFocusRange();
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
                                       CaretAssociateHint aHint) {
  if (!aNewFocus) return NS_ERROR_INVALID_ARG;

  if (MOZ_LOG_TEST(sFrameSelectionLog, LogLevel::Debug)) {
    const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
    MOZ_LOG(sFrameSelectionLog, LogLevel::Debug,
            ("%s: selection=%p, new focus=%p, offsets=(%u,%u), focus mode=%i",
             __FUNCTION__,
             mDomSelections[index] ? mDomSelections[index].get() : nullptr,
             aNewFocus, aContentOffset, aContentEndOffset,
             static_cast<int>(aFocusMode)));
  }

  mDesiredCaretPos.Invalidate();

  if (aFocusMode != FocusMode::kExtendSelection) {
    mMaintainedRange.mRange = nullptr;
    if (!IsValidSelectionPoint(aNewFocus)) {
      mLimiters.mAncestorLimiter = nullptr;
    }
  }

  // Don't take focus when dragging off of a table
  if (!mTableSelection.mDragSelectingCells) {
    BidiLevelFromClick(aNewFocus, aContentOffset);
    SetChangeReasons(nsISelectionListener::MOUSEDOWN_REASON +
                     nsISelectionListener::DRAG_REASON);

    const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
    RefPtr<Selection> selection = mDomSelections[index];
    MOZ_ASSERT(selection);

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
  nsIFrame* newFrame = 0;
  nsPoint newPoint;

  result = ConstrainFrameAndPointToAnchorSubtree(aFrame, aPoint, &newFrame,
                                                 newPoint);
  if (NS_FAILED(result)) return;
  if (!newFrame) return;

  nsIFrame::ContentOffsets offsets =
      newFrame->GetContentOffsetsFromPoint(newPoint);
  if (!offsets.content) return;

  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  RefPtr<Selection> selection = mDomSelections[index];
  if (newFrame->IsSelected() && selection) {
    // `MOZ_KnownLive` required because of
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1636889.
    mMaintainedRange.AdjustNormalSelection(MOZ_KnownLive(offsets.content),
                                           offsets.offset, *selection);
  }

  const bool scrollViewStop = mLimiters.mLimiter != nullptr;
  mMaintainedRange.AdjustContentOffsets(offsets, scrollViewStop);

  // TODO: no click has happened, rename `HandleClick`.
  HandleClick(MOZ_KnownLive(offsets.content) /* bug 1636889 */, offsets.offset,
              offsets.offset, FocusMode::kExtendSelection, offsets.associate);
}

nsresult nsFrameSelection::StartAutoScrollTimer(nsIFrame* aFrame,
                                                const nsPoint& aPoint,
                                                uint32_t aDelay) {
  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  if (!mDomSelections[index]) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<Selection> selection = mDomSelections[index];
  return selection->StartAutoScrollTimer(aFrame, aPoint, aDelay);
}

void nsFrameSelection::StopAutoScrollTimer() {
  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  if (!mDomSelections[index]) {
    return;
  }

  mDomSelections[index]->StopAutoScrollTimer();
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
                                     CaretAssociateHint aHint,
                                     const FocusMode aFocusMode) {
  NS_ENSURE_STATE(mPresShell);

  if (!IsValidSelectionPoint(&aNewFocus)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(sFrameSelectionLog, LogLevel::Verbose,
          ("%s: new focus=%p, offsets=(%u, %u), hint=%i, focusMode=%i",
           __FUNCTION__, &aNewFocus, aContentOffset, aContentEndOffset,
           static_cast<int>(aHint), static_cast<int>(aFocusMode)));

  mPresShell->FrameSelectionWillTakeFocus(*this);

  // Clear all table selection data
  mTableSelection.mMode = TableSelectionMode::None;
  mTableSelection.mDragSelectingCells = false;
  mTableSelection.mStartSelectedCell = nullptr;
  mTableSelection.mEndSelectedCell = nullptr;
  mTableSelection.mAppendStartSelectedCell = nullptr;
  mCaret.mHint = aHint;

  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  if (!mDomSelections[index]) return NS_ERROR_NULL_POINTER;

  Maybe<Selection::AutoUserInitiated> userSelect;
  if (IsUserSelectionReason()) {
    userSelect.emplace(mDomSelections[index]);
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

      RefPtr<Selection> selection = mDomSelections[index];

      if (aFocusMode == FocusMode::kMultiRangeSelection) {
        // Remove existing collapsed ranges as there's no point in having
        // non-anchor/focus collapsed ranges.
        selection->RemoveCollapsedRanges();

        ErrorResult error;
        RefPtr<nsRange> newRange = nsRange::Create(
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
        if (parentAndOffset.mParent) {
          const nsresult result = HandleTableSelection(
              parentAndOffset.mParent, parentAndOffset.mOffset,
              TableSelectionMode::Cell, &event);
          if (NS_WARN_IF(NS_FAILED(result))) {
            return result;
          }
        }

        // Find the parent of this new cell and extend selection to it
        parentAndOffset = ParentAndOffset{*inclusiveTableCellAncestor};

        // XXXX We need to REALLY get the current key shift state
        //  (we'd need to add event listener -- let's not bother for now)
        event.mModifiers &= ~MODIFIER_SHIFT;  // aContinueSelection;
        if (parentAndOffset.mParent) {
          mTableSelection.mClosestInclusiveTableCellAncestor =
              inclusiveTableCellAncestor;
          // Continue selection into next cell
          const nsresult result = HandleTableSelection(
              parentAndOffset.mParent, parentAndOffset.mOffset,
              TableSelectionMode::Cell, &event);
          if (NS_WARN_IF(NS_FAILED(result))) {
            return result;
          }
        }
      } else {
        RefPtr<Selection> selection = mDomSelections[index];
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

  // Don't notify selection listeners if batching is on:
  if (IsBatching()) {
    return NS_OK;
  }

  // Be aware, the Selection instance may be destroyed after this call.
  return NotifySelectionListeners(SelectionType::eNormal);
}

UniquePtr<SelectionDetails> nsFrameSelection::LookUpSelection(
    nsIContent* aContent, int32_t aContentOffset, int32_t aContentLength,
    bool aSlowCheck) const {
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

  for (size_t j = 0; j < ArrayLength(mDomSelections); j++) {
    if (mDomSelections[j]) {
      details = mDomSelections[j]->LookUpSelection(
          aContent, static_cast<uint32_t>(aContentOffset),
          static_cast<uint32_t>(aContentLength), std::move(details),
          kPresentSelectionTypes[j], aSlowCheck);
    }
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
        SelectionType::eHighlight, aSlowCheck);
  }

  return details;
}

void nsFrameSelection::SetDragState(bool aState) {
  if (mDragState == aState) return;

  mDragState = aState;

  if (!mDragState) {
    mTableSelection.mDragSelectingCells = false;
    // Notify that reason is mouse up.
    SetChangeReasons(nsISelectionListener::MOUSEUP_REASON);

    // flag is set to false in `NotifySelectionListeners`.
    // since this function call is part of click event, this would immediately
    // reset the flag, rendering it useless.
    AutoRestore<bool> restoreIsDoubleClickSelectionFlag(
        mIsDoubleClickSelection);
    // Be aware, the Selection instance may be destroyed after this call.
    NotifySelectionListeners(SelectionType::eNormal);
  }
}

Selection* nsFrameSelection::GetSelection(SelectionType aSelectionType) const {
  int8_t index = GetIndexFromSelectionType(aSelectionType);
  if (index < 0) return nullptr;

  return mDomSelections[index];
}

void nsFrameSelection::AddHighlightSelection(
    const nsAtom* aHighlightName, const mozilla::dom::Highlight& aHighlight) {
  RefPtr<Selection> selection =
      aHighlight.CreateHighlightSelection(aHighlightName, this);
  if (auto iter =
          std::find_if(mHighlightSelections.begin(), mHighlightSelections.end(),
                       [&aHighlightName](auto const& aElm) {
                         return aElm.first() == aHighlightName;
                       });
      iter != mHighlightSelections.end()) {
    iter->second() = std::move(selection);
  } else {
    mHighlightSelections.AppendElement(
        CompactPair<RefPtr<const nsAtom>, RefPtr<Selection>>(
            aHighlightName, std::move(selection)));
  }
}

void nsFrameSelection::RemoveHighlightSelection(const nsAtom* aHighlightName) {
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
    const nsAtom* aHighlightName, const mozilla::dom::Highlight& aHighlight,
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
    // if the selection does not exist yet, add all of its ranges and exit.
    RefPtr<Selection> selection =
        aHighlight.CreateHighlightSelection(aHighlightName, this);
    mHighlightSelections.AppendElement(
        CompactPair<RefPtr<const nsAtom>, RefPtr<Selection>>(
            aHighlightName, std::move(selection)));
  }
}

void nsFrameSelection::RemoveHighlightSelectionRange(
    const nsAtom* aHighlightName, mozilla::dom::AbstractRange& aRange) {
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
  int8_t index = GetIndexFromSelectionType(aSelectionType);
  if (index < 0) return NS_ERROR_INVALID_ARG;

  if (!mDomSelections[index]) return NS_ERROR_NULL_POINTER;

  ScrollAxis verticalScroll = ScrollAxis();
  int32_t flags = Selection::SCROLL_DO_FLUSH;
  if (aFlags & nsISelectionController::SCROLL_SYNCHRONOUS) {
    flags |= Selection::SCROLL_SYNCHRONOUS;
  } else if (aFlags & nsISelectionController::SCROLL_FIRST_ANCESTOR_ONLY) {
    flags |= Selection::SCROLL_FIRST_ANCESTOR_ONLY;
  }
  if (aFlags & nsISelectionController::SCROLL_OVERFLOW_HIDDEN) {
    flags |= Selection::SCROLL_OVERFLOW_HIDDEN;
  }
  if (aFlags & nsISelectionController::SCROLL_CENTER_VERTICALLY) {
    verticalScroll =
        ScrollAxis(WhereToScroll::Center, WhenToScroll::IfNotFullyVisible);
  }
  if (aFlags & nsISelectionController::SCROLL_FOR_CARET_MOVE) {
    flags |= Selection::SCROLL_FOR_CARET_MOVE;
  }

  // After ScrollSelectionIntoView(), the pending notifications might be
  // flushed and PresShell/PresContext/Frames may be dead. See bug 418470.
  RefPtr<Selection> sel = mDomSelections[index];
  return sel->ScrollIntoView(aRegion, verticalScroll, ScrollAxis(), flags);
}

nsresult nsFrameSelection::RepaintSelection(SelectionType aSelectionType) {
  int8_t index = GetIndexFromSelectionType(aSelectionType);
  if (index < 0) return NS_ERROR_INVALID_ARG;
  if (!mDomSelections[index]) return NS_ERROR_NULL_POINTER;
  NS_ENSURE_STATE(mPresShell);

// On macOS, update the selection cache to the new active selection
// aka the current selection.
#ifdef XP_MACOSX
  // Check that we're in the an active window and, if this is Web content,
  // in the frontmost tab.
  Document* doc = mPresShell->GetDocument();
  if (doc && IsInActiveTab(doc) && aSelectionType == SelectionType::eNormal) {
    UpdateSelectionCacheOnRepaintSelection(mDomSelections[index]);
  }
#endif
  return mDomSelections[index]->Repaint(mPresShell->GetPresContext());
}

static bool IsDisplayContents(const nsIContent* aContent) {
  return aContent->IsElement() && aContent->AsElement()->IsDisplayContents();
}

bool nsFrameSelection::AdjustFrameForLineStart(nsIFrame*& aFrame,
                                               int32_t& aFrameOffset) {
  if (!aFrame->HasSignificantTerminalNewline()) {
    return false;
  }

  auto [start, end] = aFrame->GetOffsets();
  if (aFrameOffset != end) {
    return false;
  }

  nsIFrame* nextSibling = aFrame->GetNextSibling();
  if (!nextSibling) {
    return false;
  }

  aFrame = nextSibling;
  std::tie(start, end) = aFrame->GetOffsets();
  aFrameOffset = start;
  return true;
}

// static
nsIFrame* nsFrameSelection::GetFrameForNodeOffset(nsIContent* aNode,
                                                  int32_t aOffset,
                                                  CaretAssociateHint aHint,
                                                  int32_t* aReturnOffset) {
  if (!aNode || !aReturnOffset) return nullptr;

  if (aOffset < 0) return nullptr;

  if (!aNode->GetPrimaryFrame() && !IsDisplayContents(aNode)) {
    return nullptr;
  }

  nsIFrame* returnFrame = nullptr;
  nsCOMPtr<nsIContent> theNode;

  while (true) {
    *aReturnOffset = aOffset;

    theNode = aNode;

    if (aNode->IsElement()) {
      int32_t childIndex = 0;
      int32_t numChildren = theNode->GetChildCount();

      if (aHint == CARET_ASSOCIATE_BEFORE) {
        if (aOffset > 0) {
          childIndex = aOffset - 1;
        } else {
          childIndex = aOffset;
        }
      } else {
        NS_ASSERTION(aHint == CARET_ASSOCIATE_AFTER, "unknown direction");
        if (aOffset >= numChildren) {
          if (numChildren > 0) {
            childIndex = numChildren - 1;
          } else {
            childIndex = 0;
          }
        } else {
          childIndex = aOffset;
        }
      }

      if (childIndex > 0 || numChildren > 0) {
        nsCOMPtr<nsIContent> childNode =
            theNode->GetChildAt_Deprecated(childIndex);

        if (!childNode) {
          break;
        }

        theNode = childNode;
      }

      // Now that we have the child node, check if it too
      // can contain children. If so, descend into child.
      if (theNode->IsElement() && theNode->GetChildCount() &&
          !theNode->HasIndependentSelection()) {
        aNode = theNode;
        aOffset = aOffset > childIndex ? theNode->GetChildCount() : 0;
        continue;
      } else {
        // Check to see if theNode is a text node. If it is, translate
        // aOffset into an offset into the text node.

        RefPtr<Text> textNode = theNode->GetAsText();
        if (textNode) {
          if (theNode->GetPrimaryFrame()) {
            if (aOffset > childIndex) {
              uint32_t textLength = textNode->Length();

              *aReturnOffset = (int32_t)textLength;
            } else {
              *aReturnOffset = 0;
            }
          } else {
            int32_t numChildren = aNode->GetChildCount();
            int32_t newChildIndex = aHint == CARET_ASSOCIATE_BEFORE
                                        ? childIndex - 1
                                        : childIndex + 1;

            if (newChildIndex >= 0 && newChildIndex < numChildren) {
              nsCOMPtr<nsIContent> newChildNode =
                  aNode->GetChildAt_Deprecated(newChildIndex);
              if (!newChildNode) {
                return nullptr;
              }

              aNode = newChildNode;
              aOffset =
                  aHint == CARET_ASSOCIATE_BEFORE ? aNode->GetChildCount() : 0;
              continue;
            } else {
              // newChildIndex is illegal which means we're at first or last
              // child. Just use original node to get the frame.
              theNode = aNode;
            }
          }
        }
      }
    }

    // If the node is a ShadowRoot, the frame needs to be adjusted,
    // because a ShadowRoot does not get a frame. Its children are rendered
    // as children of the host.
    if (ShadowRoot* shadow = ShadowRoot::FromNode(theNode)) {
      theNode = shadow->GetHost();
    }

    returnFrame = theNode->GetPrimaryFrame();
    if (!returnFrame) {
      if (aHint == CARET_ASSOCIATE_BEFORE) {
        if (aOffset > 0) {
          --aOffset;
          continue;
        } else {
          break;
        }
      } else {
        int32_t end = theNode->GetChildCount();
        if (aOffset < end) {
          ++aOffset;
          continue;
        } else {
          break;
        }
      }
    }

    break;
  }  // end while

  if (!returnFrame) return nullptr;

  // If we ended up here and were asked to position the caret after a visible
  // break, let's return the frame on the next line instead if it exists.
  if (aOffset > 0 && (uint32_t)aOffset >= aNode->Length() &&
      theNode == aNode->GetLastChild()) {
    nsIFrame* newFrame;
    nsLayoutUtils::IsInvisibleBreak(theNode, &newFrame);
    if (newFrame) {
      returnFrame = newFrame;
      *aReturnOffset = 0;
    }
  }

  // find the child frame containing the offset we want
  returnFrame->GetChildFrameContainingOffset(
      *aReturnOffset, aHint == CARET_ASSOCIATE_AFTER, &aOffset, &returnFrame);
  return returnFrame;
}

nsIFrame* nsFrameSelection::GetFrameToPageSelect() const {
  if (NS_WARN_IF(!mPresShell)) {
    return nullptr;
  }

  nsIFrame* rootFrameToSelect;
  if (mLimiters.mLimiter) {
    rootFrameToSelect = mLimiters.mLimiter->GetPrimaryFrame();
    if (NS_WARN_IF(!rootFrameToSelect)) {
      return nullptr;
    }
  } else if (mLimiters.mAncestorLimiter) {
    rootFrameToSelect = mLimiters.mAncestorLimiter->GetPrimaryFrame();
    if (NS_WARN_IF(!rootFrameToSelect)) {
      return nullptr;
    }
  } else {
    rootFrameToSelect = mPresShell->GetRootScrollFrame();
    if (NS_WARN_IF(!rootFrameToSelect)) {
      return nullptr;
    }
  }

  nsCOMPtr<nsIContent> contentToSelect = mPresShell->GetContentForScrolling();
  if (contentToSelect) {
    // If there is selected content, look for nearest and vertical scrollable
    // parent under the root frame.
    for (nsIFrame* frame = contentToSelect->GetPrimaryFrame();
         frame && frame != rootFrameToSelect; frame = frame->GetParent()) {
      nsIScrollableFrame* scrollableFrame = do_QueryFrame(frame);
      if (!scrollableFrame) {
        continue;
      }
      ScrollStyles scrollStyles = scrollableFrame->GetScrollStyles();
      if (scrollStyles.mVertical == StyleOverflow::Hidden) {
        continue;
      }
      layers::ScrollDirections directions =
          scrollableFrame->GetAvailableScrollingDirections();
      if (directions.contains(layers::ScrollDirection::eVertical)) {
        // If there is sub scrollable frame, let's use its page size to select.
        return frame;
      }
    }
  }
  // Otherwise, i.e., there is no scrollable frame or only the root frame is
  // scrollable, let's return the root frame because Shift + PageUp/PageDown
  // should expand the selection in the root content even if it's not
  // scrollable.
  return rootFrameToSelect;
}

nsresult nsFrameSelection::PageMove(bool aForward, bool aExtend,
                                    nsIFrame* aFrame,
                                    SelectionIntoView aSelectionIntoView) {
  MOZ_ASSERT(aFrame);

  // expected behavior for PageMove is to scroll AND move the caret
  // and remain relative position of the caret in view. see Bug 4302.

  // Get the scrollable frame.  If aFrame is not scrollable, this is nullptr.
  nsIScrollableFrame* scrollableFrame = aFrame->GetScrollTargetFrame();
  const AutoWeakFrame scrollableFrameWeak(do_QueryFrame(scrollableFrame));
  // Get the scrolled frame.  If aFrame is not scrollable, this is aFrame
  // itself.
  nsIFrame* scrolledFrame =
      scrollableFrame ? scrollableFrame->GetScrolledFrame() : aFrame;
  if (!scrolledFrame) {
    return NS_OK;
  }

  // find out where the caret is.
  // we should know mDesiredCaretPos.mValue value of nsFrameSelection, but I
  // havent seen that behavior in other windows applications yet.
  RefPtr<Selection> selection = GetSelection(SelectionType::eNormal);
  if (!selection) {
    return NS_OK;
  }

  nsRect caretPos;
  nsIFrame* caretFrame = nsCaret::GetGeometry(selection, &caretPos);
  if (!caretFrame) {
    return NS_OK;
  }

  // If the scrolled frame is outside of current selection limiter,
  // we need to scroll the frame but keep moving selection in the limiter.
  nsIFrame* frameToClick = scrolledFrame;
  if (!IsValidSelectionPoint(scrolledFrame->GetContent())) {
    frameToClick = GetFrameToPageSelect();
    if (NS_WARN_IF(!frameToClick)) {
      return NS_OK;
    }
  }

  if (scrollableFrame) {
    // If there is a scrollable frame, adjust pseudo-click position with page
    // scroll amount.
    // XXX This may scroll more than one page if ScrollSelectionIntoView is
    //     called later because caret may not fully visible.  E.g., if
    //     clicking line will be visible only half height with scrolling
    //     the frame, ScrollSelectionIntoView additionally scrolls to show
    //     the caret entirely.
    if (aForward) {
      caretPos.y += scrollableFrame->GetPageScrollAmount().height;
    } else {
      caretPos.y -= scrollableFrame->GetPageScrollAmount().height;
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
  nsIFrame::ContentOffsets offsets =
      frameToClick->GetContentOffsetsFromPoint(desiredPoint);

  if (!offsets.content) {
    // XXX Do we need to handle ScrollSelectionIntoView in this case?
    return NS_OK;
  }

  // First, place the caret.
  bool selectionChanged;
  {
    // We don't want any script to run until we check whether selection is
    // modified by HandleClick.
    SelectionBatcher ensureNoSelectionChangeNotifications(selection,
                                                          __FUNCTION__);

    RangeBoundary oldAnchor = selection->AnchorRef();
    RangeBoundary oldFocus = selection->FocusRef();

    const FocusMode focusMode =
        aExtend ? FocusMode::kExtendSelection : FocusMode::kCollapseToNewPoint;
    HandleClick(MOZ_KnownLive(offsets.content) /* bug 1636889 */,
                offsets.offset, offsets.offset, focusMode,
                CARET_ASSOCIATE_AFTER);

    selectionChanged = selection->AnchorRef() != oldAnchor ||
                       selection->FocusRef() != oldFocus;
  }

  bool doScrollSelectionIntoView = !(
      aSelectionIntoView == SelectionIntoView::IfChanged && !selectionChanged);

  // Then, scroll the given frame one page.
  if (scrollableFrameWeak.IsAlive()) {
    // If we'll call ScrollSelectionIntoView later and selection wasn't
    // changed and we scroll outside of selection limiter, we shouldn't use
    // smooth scroll here because nsIScrollableFrame uses normal runnable,
    // but ScrollSelectionIntoView uses early runner and it cancels the
    // pending smooth scroll.  Therefore, if we used smooth scroll in such
    // case, ScrollSelectionIntoView would scroll to show caret instead of
    // page scroll of an element outside selection limiter.
    ScrollMode scrollMode = doScrollSelectionIntoView && !selectionChanged &&
                                    scrolledFrame != frameToClick
                                ? ScrollMode::Instant
                                : ScrollMode::Smooth;
    scrollableFrame->ScrollBy(nsIntPoint(0, aForward ? 1 : -1),
                              ScrollUnit::PAGES, scrollMode);
  }

  // Finally, scroll selection into view if requested.
  if (!doScrollSelectionIntoView) {
    return NS_OK;
  }
  return ScrollSelectionIntoView(
      SelectionType::eNormal, nsISelectionController::SELECTION_FOCUS_REGION,
      nsISelectionController::SCROLL_SYNCHRONOUS |
          nsISelectionController::SCROLL_FOR_CARET_MOVE);
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
  if (aDirection < 0 || aDirection > 3 || aAmount < 0 || aAmount > 1) {
    return NS_ERROR_FAILURE;
  }

  nsPresContext* context = mPresShell->GetPresContext();
  if (!context) {
    return NS_ERROR_FAILURE;
  }

  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  RefPtr<Selection> sel = mDomSelections[index];
  if (!sel) {
    return NS_ERROR_NULL_POINTER;
  }

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
  nsIFrame* frame = sel->GetPrimaryFrameForFocusNode(true);
  if (frame) {
    if (!frame->Style()->IsTextCombined()) {
      wm = frame->GetWritingMode();
    } else {
      // Using different direction for horizontal-in-vertical would
      // make it hard to navigate via keyboard. Inherit the moving
      // direction from its parent.
      MOZ_ASSERT(frame->IsTextFrame());
      wm = frame->GetParent()->GetWritingMode();
      MOZ_ASSERT(wm.IsVertical(),
                 "Text combined "
                 "can only appear in vertical text");
    }
  }

  const PhysicalToLogicalMapping& mapping =
      wm.IsVertical()
          ? wm.IsVerticalLR() ? verticalLR[aDirection] : verticalRL[aDirection]
          : horizontal[aDirection];

  nsresult rv =
      MoveCaret(mapping.direction, aExtend, mapping.amounts[aAmount], eVisual);
  if (NS_FAILED(rv)) {
    // If we tried to do a line move, but couldn't move in the given direction,
    // then we'll "promote" this to a line-edge move instead.
    if (mapping.amounts[aAmount] == eSelectLine) {
      rv = MoveCaret(mapping.direction, aExtend, mapping.amounts[aAmount + 1],
                     eVisual);
    }
    // And if it was a next-word move that failed (which can happen when
    // eat_space_to_next_word is true, see bug 1153237), then just move forward
    // to the line-edge.
    else if (mapping.amounts[aAmount] == eSelectWord &&
             mapping.direction == eDirNext) {
      rv = MoveCaret(eDirNext, aExtend, eSelectEndLine, eVisual);
    }
  }

  return rv;
}

nsresult nsFrameSelection::CharacterMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, aExtend, eSelectCluster,
                   eUsePrefStyle);
}

nsresult nsFrameSelection::WordMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, aExtend, eSelectWord,
                   eUsePrefStyle);
}

nsresult nsFrameSelection::LineMove(bool aForward, bool aExtend) {
  return MoveCaret(aForward ? eDirNext : eDirPrevious, aExtend, eSelectLine,
                   eUsePrefStyle);
}

nsresult nsFrameSelection::IntraLineMove(bool aForward, bool aExtend) {
  if (aForward) {
    return MoveCaret(eDirNext, aExtend, eSelectEndLine, eLogical);
  } else {
    return MoveCaret(eDirPrevious, aExtend, eSelectBeginLine, eLogical);
  }
}

template <typename RangeType>
Result<RefPtr<RangeType>, nsresult>
nsFrameSelection::CreateRangeExtendedToSomewhere(
    nsDirection aDirection, const nsSelectionAmount aAmount,
    CaretMovementStyle aMovementStyle) {
  MOZ_ASSERT(aDirection == eDirNext || aDirection == eDirPrevious);
  MOZ_ASSERT(aAmount == eSelectCharacter || aAmount == eSelectCluster ||
             aAmount == eSelectWord || aAmount == eSelectBeginLine ||
             aAmount == eSelectEndLine);
  MOZ_ASSERT(aMovementStyle == eLogical || aMovementStyle == eVisual ||
             aMovementStyle == eUsePrefStyle);

  if (!mPresShell) {
    return Err(NS_ERROR_UNEXPECTED);
  }
  OwningNonNull<PresShell> presShell(*mPresShell);
  presShell->FlushPendingNotifications(FlushType::Layout);
  if (!mPresShell) {
    return Err(NS_ERROR_FAILURE);
  }
  Selection* selection =
      mDomSelections[GetIndexFromSelectionType(SelectionType::eNormal)];
  if (!selection || selection->RangeCount() != 1) {
    return Err(NS_ERROR_FAILURE);
  }
  RefPtr<const nsRange> firstRange = selection->GetRangeAt(0);
  if (!firstRange || !firstRange->IsPositioned()) {
    return Err(NS_ERROR_FAILURE);
  }
  Result<PeekOffsetStruct, nsresult> result = PeekOffsetForCaretMove(
      aDirection, true, aAmount, aMovementStyle, nsPoint(0, 0));
  if (result.isErr()) {
    return Err(NS_ERROR_FAILURE);
  }
  const PeekOffsetStruct& pos = result.inspect();
  RefPtr<RangeType> range;
  if (NS_WARN_IF(!pos.mResultContent)) {
    return range;
  }
  if (aDirection == eDirPrevious) {
    range = RangeType::Create(
        RawRangeBoundary(pos.mResultContent, pos.mContentOffset),
        firstRange->EndRef(), IgnoreErrors());
  } else {
    range = RangeType::Create(
        firstRange->StartRef(),
        RawRangeBoundary(pos.mResultContent, pos.mContentOffset),
        IgnoreErrors());
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

  if (mBatching.mCounter == 0 && mBatching.mChangesDuringBatching) {
    AddChangeReasons(aReasons);
    mCaretMoveAmount = eSelectNoAmount;
    mBatching.mChangesDuringBatching = false;
    // Be aware, the Selection instance may be destroyed after this call.
    NotifySelectionListeners(SelectionType::eNormal);
  }
}

nsresult nsFrameSelection::NotifySelectionListeners(
    SelectionType aSelectionType) {
  int8_t index = GetIndexFromSelectionType(aSelectionType);
  if (index >= 0 && mDomSelections[index]) {
    RefPtr<Selection> selection = mDomSelections[index];
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
  int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  RefPtr<Selection> selection = mDomSelections[index];
  if (!selection) {
    return NS_ERROR_NULL_POINTER;
  }

  ErrorResult err;
  selection->RemoveAllRanges(err);
  return err.StealNSResult();
}

static nsIContent* GetFirstSelectedContent(const nsRange* aRange) {
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
  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  RefPtr<Selection> selection = mDomSelections[index];
  if (!selection) {
    return NS_ERROR_NULL_POINTER;
  }

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
      return SelectBlockOfCells(mStartSelectedCell, aChildContent,
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

      return SelectBlockOfCells(mAppendStartSelectedCell, aChildContent,
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
        RefPtr<nsRange> range = aNormalSelection.GetRangeAt(i);
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
  if (NS_FAILED(result)) return result;
  result = GetCellIndexes(aEndCell, endRowIndex, endColIndex);
  if (NS_FAILED(result)) return result;

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

  nsTableWrapperFrame* tableFrame =
      do_QueryFrame(aTableContent->GetPrimaryFrame());
  if (!tableFrame) return NS_ERROR_FAILURE;

  int32_t minRowIndex = std::min(aStartRowIndex, aEndRowIndex);
  int32_t maxRowIndex = std::max(aStartRowIndex, aEndRowIndex);
  int32_t minColIndex = std::min(aStartColumnIndex, aEndColumnIndex);
  int32_t maxColIndex = std::max(aStartColumnIndex, aEndColumnIndex);

  // Strong reference because we sometimes remove the range
  RefPtr<nsRange> range = GetFirstCellRange(aNormalSelection);
  nsIContent* cellNode = GetFirstSelectedContent(range);
  MOZ_ASSERT(!range || cellNode, "Must have cellNode if had a range");

  int32_t curRowIndex, curColIndex;
  while (cellNode) {
    nsresult result = GetCellIndexes(cellNode, curRowIndex, curColIndex);
    if (NS_FAILED(result)) return result;

#ifdef DEBUG_TABLE_SELECTION
    if (!range) printf("RemoveCellsToSelection -- range is null\n");
#endif

    if (range) {
      if (aRemoveOutsideOfCellRange) {
        if (curRowIndex < minRowIndex || curRowIndex > maxRowIndex ||
            curColIndex < minColIndex || curColIndex > maxColIndex) {
          aNormalSelection.RemoveRangeAndUnselectFramesAndNotifyListeners(
              *range, IgnoreErrors());
          // Since we've removed the range, decrement pointer to next range
          mSelectedCellIndex--;
        }

      } else {
        // Remove cell from selection if it belongs to the given cells range or
        // it is spanned onto the cells range.
        nsTableCellFrame* cellFrame =
            tableFrame->GetCellFrameAt(curRowIndex, curColIndex);

        uint32_t origRowIndex = cellFrame->RowIndex();
        uint32_t origColIndex = cellFrame->ColIndex();
        uint32_t actualRowSpan =
            tableFrame->GetEffectiveRowSpanAt(origRowIndex, origColIndex);
        uint32_t actualColSpan =
            tableFrame->GetEffectiveColSpanAt(curRowIndex, curColIndex);
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

  nsTableWrapperFrame* tableFrame =
      do_QueryFrame(aTableContent->GetPrimaryFrame());
  if (!tableFrame) {  // Check that |table| is a table.
    return NS_ERROR_FAILURE;
  }

  nsresult result = NS_OK;
  uint32_t row = aStartRowIndex;
  while (true) {
    uint32_t col = aStartColumnIndex;
    while (true) {
      nsTableCellFrame* cellFrame = tableFrame->GetCellFrameAt(row, col);

      // Skip cells that are spanned from previous locations or are already
      // selected
      if (cellFrame) {
        uint32_t origRow = cellFrame->RowIndex();
        uint32_t origCol = cellFrame->ColIndex();
        if (origRow == row && origCol == col && !cellFrame->IsSelected()) {
          result = SelectCellElement(cellFrame->GetContent(), aNormalSelection);
          if (NS_FAILED(result)) {
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
  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  const RefPtr<mozilla::dom::Selection> selection = mDomSelections[index];
  if (!selection) {
    return NS_ERROR_NULL_POINTER;
  }

  return mTableSelection.UnselectCells(aTable, aStartRowIndex,
                                       aStartColumnIndex, aEndRowIndex,
                                       aEndColumnIndex, false, *selection);
}

nsresult nsFrameSelection::RestrictCellsToSelection(nsIContent* aTable,
                                                    int32_t aStartRowIndex,
                                                    int32_t aStartColumnIndex,
                                                    int32_t aEndRowIndex,
                                                    int32_t aEndColumnIndex) {
  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  const RefPtr<mozilla::dom::Selection> selection = mDomSelections[index];
  if (!selection) {
    return NS_ERROR_NULL_POINTER;
  }

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

    rv = SelectBlockOfCells(mStartSelectedCell,
                            firstAndLastCell.inspect().mLast, aNormalSelection);

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
nsIContent* nsFrameSelection::GetFirstCellNodeInRange(const nsRange* aRange) {
  if (!aRange) return nullptr;

  nsIContent* childContent = aRange->GetChildAtStartOffset();
  if (!childContent) return nullptr;
  // Don't return node if not a cell
  if (!IsCell(childContent)) return nullptr;

  return childContent;
}

nsRange* nsFrameSelection::TableSelection::GetFirstCellRange(
    const mozilla::dom::Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  nsRange* firstRange = aNormalSelection.GetRangeAt(0);
  if (!GetFirstCellNodeInRange(firstRange)) {
    return nullptr;
  }

  // Setup for next cell
  mSelectedCellIndex = 1;

  return firstRange;
}

nsRange* nsFrameSelection::TableSelection::GetNextCellRange(
    const mozilla::dom::Selection& aNormalSelection) {
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  nsRange* range =
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
  if (!aCell) return NS_ERROR_NULL_POINTER;

  aColIndex = 0;  // initialize out params
  aRowIndex = 0;

  nsITableCellLayout* cellLayoutObject = GetCellLayout(aCell);
  if (!cellLayoutObject) return NS_ERROR_FAILURE;
  return cellLayoutObject->GetCellIndexes(aRowIndex, aColIndex);
}

// static
nsIContent* nsFrameSelection::IsInSameTable(const nsIContent* aContent1,
                                            const nsIContent* aContent2) {
  if (!aContent1 || !aContent2) return nullptr;

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
  const int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
  const RefPtr<Selection> selection = mDomSelections[index];
  if (!selection) {
    return NS_ERROR_NULL_POINTER;
  }

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
  RefPtr<nsRange> range =
      nsRange::Create(aContainer, aOffset, aContainer, aOffset + 1, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  MOZ_ASSERT(range);

  ErrorResult err;
  aNormalSelection.AddRangeAndSelectFramesAndNotifyListeners(*range, err);
  return err.StealNSResult();
}

// End of Table Selection

void nsFrameSelection::SetAncestorLimiter(nsIContent* aLimiter) {
  if (mLimiters.mAncestorLimiter != aLimiter) {
    mLimiters.mAncestorLimiter = aLimiter;
    int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
    if (!mDomSelections[index]) return;

    if (!IsValidSelectionPoint(mDomSelections[index]->GetFocusNode())) {
      ClearNormalSelection();
      if (mLimiters.mAncestorLimiter) {
        SetChangeReasons(nsISelectionListener::NO_REASON);
        nsCOMPtr<nsIContent> limiter(mLimiters.mAncestorLimiter);
        const nsresult rv = TakeFocus(*limiter, 0, 0, CARET_ASSOCIATE_BEFORE,
                                      FocusMode::kCollapseToNewPoint);
        Unused << NS_WARN_IF(NS_FAILED(rv));
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
    int8_t index = GetIndexFromSelectionType(SelectionType::eNormal);
    mDomSelections[index]->StopNotifyingAccessibleCaretEventHub();
  }

  StopAutoScrollTimer();
  for (size_t i = 0; i < ArrayLength(mDomSelections); i++) {
    mDomSelections[i]->Clear(nullptr);
  }
  mPresShell = nullptr;
}

#ifdef XP_MACOSX
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
static nsresult UpdateSelectionCacheOnRepaintSelection(Selection* aSel) {
  PresShell* presShell = aSel->GetPresShell();
  if (!presShell) {
    return NS_OK;
  }
  nsCOMPtr<Document> aDoc = presShell->GetDocument();

  if (aDoc && aSel && !aSel->IsCollapsed()) {
    return nsCopySupport::EncodeDocumentWithContextAndPutToClipboard(
        aSel, aDoc, nsIClipboard::kSelectionCache, false);
  }

  return NS_OK;
}
#endif  // XP_MACOSX

// mozilla::AutoCopyListener

int16_t AutoCopyListener::sClipboardID = -1;

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
  MOZ_ASSERT(IsValidClipboardID(sClipboardID));

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

  if (!aDocument || aSelection.IsCollapsed()) {
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
