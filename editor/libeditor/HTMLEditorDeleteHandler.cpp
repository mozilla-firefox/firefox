/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"
#include "HTMLEditorNestedClasses.h"

#include <fmt/format.h>
#include <utility>

#include "AutoClonedRangeArray.h"
#include "CSSEditUtils.h"
#include "EditAction.h"
#include "EditorDOMAPIWrapper.h"
#include "EditorDOMPoint.h"
#include "EditorLineBreak.h"
#include "EditorUtils.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditorInlines.h"
#include "HTMLEditUtils.h"
#include "WhiteSpaceVisibilityKeeper.h"
#include "WSRunScanner.h"

#include "ErrorList.h"
#include "mozilla/Assertions.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/PresShell.h"
#include "mozilla/SelectionState.h"
#include "mozilla/StaticPrefs_editor.h"  // for StaticPrefs::editor_*
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"  // for Element::IsContentEditablePlainTextOnly
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "nsAtom.h"
#include "nsComputedDOMStyle.h"  // for nsComputedDOMStyle
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsINode.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStyleConsts.h"  // for StyleWhiteSpace
#include "nsTArray.h"
#include "nsTextNode.h"

// NOTE: This file was split from:
//   https://searchfox.org/mozilla-central/rev/c409dd9235c133ab41eba635f906aa16e050c197/editor/libeditor/HTMLEditSubActionHandler.cpp

namespace mozilla {

using namespace dom;
using EditablePointOption = HTMLEditUtils::EditablePointOption;
using EditablePointOptions = HTMLEditUtils::EditablePointOptions;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using InvisibleWhiteSpaces = HTMLEditUtils::InvisibleWhiteSpaces;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;
using TableBoundary = HTMLEditUtils::TableBoundary;
using TreatInvisibleLineBreakAs = HTMLEditUtils::TreatInvisibleLineBreakAs;

static LazyLogModule gOneLineMoverLog("AutoMoveOneLineHandler");

template Result<CaretPoint, nsresult>
HTMLEditor::DeleteTextAndTextNodesWithTransaction(
    const EditorDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint,
    TreatEmptyTextNodes aTreatEmptyTextNodes);
template Result<CaretPoint, nsresult>
HTMLEditor::DeleteTextAndTextNodesWithTransaction(
    const EditorDOMPointInText& aStartPoint,
    const EditorDOMPointInText& aEndPoint,
    TreatEmptyTextNodes aTreatEmptyTextNodes);

/**
 * Return true if aNode does not have meaningful content (e.g., only has
 * non-zero width border) or a line break which is followed by a block boundary.
 * NOTE: This is designed to check deleting meaningless content or not. Thus,
 * this is not declared in HTMLEditUtils nor HTMLEditor to prevent a misuse.
 */
static bool NodeIsInvisibleOrLineBreakFollowedByBlockBoundary(
    const nsINode& aNode) {
  if (MOZ_UNLIKELY(!aNode.IsText() && !aNode.IsElement())) {
    return true;  // nodes not `Text` nor an element are invisible.
  }
  if (const Text* const text = Text::FromNode(aNode)) {
    return !HTMLEditUtils::IsVisibleTextNode(
        *text, TreatInvisibleLineBreakAs::Visible);
  }
  const Element& element = *aNode.AsElement();
  if (const HTMLBRElement* const brElement = HTMLBRElement::FromNode(element)) {
    return HTMLEditUtils::IsBRElementFollowedByBlockBoundary(*brElement);
  }
  if (HTMLEditUtils::IsReplacedElement(element)) {
    return !HTMLEditUtils::IsVisibleElementEvenIfLeafNode(element);
  }
  if (nsIContent* const visibleLeaf = HTMLEditUtils::GetFirstLeafContent(
          element, {LeafNodeOption::IgnoreNonEditableNode,
                    LeafNodeOption::IgnoreAnyEmptyInlineContainers,
                    LeafNodeOption::IgnoreInvisibleText})) {
    Element* followingBlockBoundaryElement = nullptr;
    if (visibleLeaf->IsText()) {
      const Maybe<EditorRawLineBreak> preformattedLineBreak =
          EditorRawLineBreak::CreateIfTextHasOnlyOneAndNoOtherVisibleCharacters(
              *visibleLeaf->AsText());
      if (!preformattedLineBreak ||
          !HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
              preformattedLineBreak->To<EditorRawDOMPoint>(),
              HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, nullptr,
              &followingBlockBoundaryElement)) {
        return false;  // Visible `Text`.
      }
    } else {
      if (!HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
              *visibleLeaf, nullptr, &followingBlockBoundaryElement)) {
        return false;  // An element which may be visible.
      }
    }
    MOZ_ASSERT(followingBlockBoundaryElement);
    // If the following block boundary is a child of the element, the element is
    // visible because the element is or contains a child block. Otherwise, it
    // contains only the invisible line break.
    return !followingBlockBoundaryElement->IsInclusiveDescendantOf(&element);
  }
  // The element is empty so that it's invisible. Note that we can treat a
  // visible inline container element which has no significant content, e.g.,
  // has only a border as invisible at deletion.
  return true;
}

bool HTMLEditor::AutoDeleteRangesHandler::
    CanFallbackToDeleteRangeWithTransaction(
        const dom::Range& aRangeToDelete) const {
  return !IsHandlingRecursively() &&
         (!aRangeToDelete.Collapsed() ||
          EditorBase::HowToHandleCollapsedRangeFor(
              mOriginalDirectionAndAmount) !=
              EditorBase::HowToHandleCollapsedRange::Ignore);
}

bool HTMLEditor::AutoDeleteRangesHandler::
    CanFallbackToDeleteRangesWithTransaction(
        const AutoClonedSelectionRangeArray& aRangesToDelete) const {
  return !IsHandlingRecursively() && !aRangesToDelete.Ranges().IsEmpty() &&
         (!aRangesToDelete.IsCollapsed() ||
          EditorBase::HowToHandleCollapsedRangeFor(
              mOriginalDirectionAndAmount) !=
              EditorBase::HowToHandleCollapsedRange::Ignore);
}

Result<CaretPoint, nsresult>
HTMLEditor::AutoDeleteRangesHandler::FallbackToDeleteRangesWithTransaction(
    HTMLEditor& aHTMLEditor, AutoClonedSelectionRangeArray& aRangesToDelete,
    const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(CanFallbackToDeleteRangesWithTransaction(aRangesToDelete));

  const auto stripWrappers = [&]() -> nsIEditor::EStripWrappers {
    if (mOriginalStripWrappers == nsIEditor::eStrip &&
        aEditingHost.IsContentEditablePlainTextOnly()) {
      return nsIEditor::eNoStrip;
    }
    return mOriginalStripWrappers;
  }();

  {
    AutoTrackDOMRange firstRangeTracker(aHTMLEditor.RangeUpdaterRef(),
                                        &aRangesToDelete.FirstRangeRef());
    for (OwningNonNull<dom::Range>& range :
         Reversed(aRangesToDelete.Ranges())) {
      if (MOZ_UNLIKELY(!range->IsPositioned() || range->Collapsed())) {
        continue;
      }
      Maybe<AutoTrackDOMRange> trackRange;
      if (range != aRangesToDelete.FirstRangeRef()) {
        trackRange.emplace(aHTMLEditor.RangeUpdaterRef(), &range);
      }
      Result<EditorDOMRange, nsresult> rangeToDeleteOrError =
          WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin(
              aHTMLEditor, EditorDOMRange(range));
      if (MOZ_UNLIKELY(rangeToDeleteOrError.isErr())) {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin("
            ") failed");
        return rangeToDeleteOrError.propagateErr();
      }
      trackRange.reset();
      EditorDOMRange rangeToDelete = rangeToDeleteOrError.unwrap();
      if (MOZ_LIKELY(rangeToDelete.IsPositionedAndValidInComposedDoc())) {
        nsresult rv =
            range->SetStartAndEnd(rangeToDelete.StartRef().ToRawRangeBoundary(),
                                  rangeToDelete.EndRef().ToRawRangeBoundary());
        if (NS_FAILED(rv)) {
          NS_WARNING("Range::SetStartAndEnd() failed");
          return Err(rv);
        }
      }
    }
  }
  aRangesToDelete.RemoveCollapsedRanges(
      AutoClonedRangeArray::PreservePrimaryRangeIfAllRangesCollapsed::Yes);
  if (aRangesToDelete.IsCollapsed()) [[unlikely]] {
    return CaretPoint(
        EditorDOMPoint(aRangesToDelete.FirstRangeRef()->StartRef()));
  }

  Result<CaretPoint, nsresult> caretPointOrError =
      aHTMLEditor.DeleteRangesWithTransaction(mOriginalDirectionAndAmount,
                                              stripWrappers, aRangesToDelete);
  NS_WARNING_ASSERTION(caretPointOrError.isOk(),
                       "HTMLEditor::DeleteRangesWithTransaction() failed");
  return caretPointOrError;
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::ComputeRangesToDeleteRangesWithTransaction(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const Element& aEditingHost) const {
  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
  const EditorBase::HowToHandleCollapsedRange howToHandleCollapsedRange =
      EditorBase::HowToHandleCollapsedRangeFor(aDirectionAndAmount);
  if (NS_WARN_IF(aRangesToDelete.IsCollapsed() &&
                 howToHandleCollapsedRange ==
                     EditorBase::HowToHandleCollapsedRange::Ignore)) {
    return NS_ERROR_FAILURE;
  }

  const auto stripWrappers = [&]() -> nsIEditor::EStripWrappers {
    if (mOriginalStripWrappers == nsIEditor::eStrip &&
        aEditingHost.IsContentEditablePlainTextOnly()) {
      return nsIEditor::eNoStrip;
    }
    return mOriginalStripWrappers;
  }();

  aRangesToDelete.ExtendRangeToContainSurroundingInvisibleWhiteSpaces(
      stripWrappers);
  if (MOZ_UNLIKELY(aRangesToDelete.IsCollapsed() &&
                   howToHandleCollapsedRange ==
                       EditorBase::HowToHandleCollapsedRange::Ignore)) {
    return NS_OK;
  }

  for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
    if (range->Collapsed()) {
      continue;
    }
    nsresult rv = ComputeRangeToDeleteRangeWithTransaction(
        aHTMLEditor, aDirectionAndAmount, range, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoDeleteRangesHandler::ComputeRangeToDeleteRangeWithTransaction() "
          "failed");
      return rv;
    }
  }
  return NS_OK;
}

Result<EditActionResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::Run(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers, const EditorDOMPoint& aCaretPoint,
    dom::Range& aRangeToDelete, const Element& aEditingHost) {
  switch (mMode) {
    case Mode::JoinCurrentBlock: {
      Result<EditActionResult, nsresult> result =
          HandleDeleteAtCurrentBlockBoundary(aHTMLEditor, aDirectionAndAmount,
                                             aCaretPoint, aEditingHost);
      NS_WARNING_ASSERTION(result.isOk(),
                           "AutoBlockElementsJoiner::"
                           "HandleDeleteAtCurrentBlockBoundary() failed");
      return result;
    }
    case Mode::JoinOtherBlock: {
      Result<EditActionResult, nsresult> result =
          HandleDeleteAtOtherBlockBoundary(aHTMLEditor, aDirectionAndAmount,
                                           aStripWrappers, aCaretPoint,
                                           aRangeToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoBlockElementsJoiner::HandleDeleteAtOtherBlockBoundary() failed");
      return result;
    }
    case Mode::DeleteBRElement:
    case Mode::DeletePrecedingBRElementOfBlock:
    case Mode::DeletePrecedingPreformattedLineBreak: {
      Result<EditActionResult, nsresult> result = HandleDeleteLineBreak(
          aHTMLEditor, aDirectionAndAmount, aCaretPoint, aEditingHost);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoBlockElementsJoiner::HandleDeleteLineBreak() failed");
      return result;
    }
    case Mode::JoinBlocksInSameParent:
    case Mode::DeleteContentInRange:
    case Mode::DeleteNonCollapsedRange:
    case Mode::DeletePrecedingLinesAndContentInRange:
      MOZ_ASSERT_UNREACHABLE("This mode should be handled in the other Run()");
      return Err(NS_ERROR_UNEXPECTED);
    case Mode::NotInitialized:
      return EditActionResult::IgnoredResult();
  }
  return Err(NS_ERROR_NOT_INITIALIZED);
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDelete(const HTMLEditor& aHTMLEditor,
                         nsIEditor::EDirection aDirectionAndAmount,
                         const EditorDOMPoint& aCaretPoint,
                         dom::Range& aRangeToDelete,
                         const Element& aEditingHost) const {
  switch (mMode) {
    case Mode::JoinCurrentBlock: {
      nsresult rv = ComputeRangeToDeleteAtCurrentBlockBoundary(
          aHTMLEditor, aCaretPoint, aRangeToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoBlockElementsJoiner::ComputeRangeToDeleteAtCurrentBlockBoundary("
          ") failed");
      return rv;
    }
    case Mode::JoinOtherBlock: {
      nsresult rv = ComputeRangeToDeleteAtOtherBlockBoundary(
          aHTMLEditor, aDirectionAndAmount, aCaretPoint, aRangeToDelete,
          aEditingHost);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "AutoBlockElementsJoiner::"
                           "ComputeRangeToDeleteAtOtherBlockBoundary() failed");
      return rv;
    }
    case Mode::DeleteBRElement:
    case Mode::DeletePrecedingBRElementOfBlock:
    case Mode::DeletePrecedingPreformattedLineBreak: {
      nsresult rv = ComputeRangeToDeleteLineBreak(
          aHTMLEditor, aRangeToDelete, aEditingHost,
          ComputeRangeFor::GetTargetRanges);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoBlockElementsJoiner::ComputeRangeToDeleteLineBreak() failed");
      return rv;
    }
    case Mode::JoinBlocksInSameParent:
    case Mode::DeleteContentInRange:
    case Mode::DeleteNonCollapsedRange:
    case Mode::DeletePrecedingLinesAndContentInRange:
      MOZ_ASSERT_UNREACHABLE(
          "This mode should be handled in the other ComputeRangesToDelete()");
      return NS_ERROR_UNEXPECTED;
    case Mode::NotInitialized:
      return NS_OK;
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

Result<EditActionResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::Run(
    HTMLEditor& aHTMLEditor, const LimitersAndCaretData& aLimitersAndCaretData,
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers, dom::Range& aRangeToDelete,
    AutoDeleteRangesHandler::SelectionWasCollapsed aSelectionWasCollapsed,
    const Element& aEditingHost) {
  switch (mMode) {
    case Mode::JoinCurrentBlock:
    case Mode::JoinOtherBlock:
    case Mode::DeleteBRElement:
    case Mode::DeletePrecedingBRElementOfBlock:
    case Mode::DeletePrecedingPreformattedLineBreak:
      MOZ_ASSERT_UNREACHABLE("This mode should be handled in the other Run()");
      return Err(NS_ERROR_UNEXPECTED);
    case Mode::JoinBlocksInSameParent: {
      Result<EditActionResult, nsresult> result = JoinBlockElementsInSameParent(
          aHTMLEditor, aLimitersAndCaretData, aDirectionAndAmount,
          aStripWrappers, aRangeToDelete, aSelectionWasCollapsed, aEditingHost);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoBlockElementsJoiner::JoinBlockElementsInSameParent() failed");
      return result;
    }
    case Mode::DeleteContentInRange: {
      Result<EditActionResult, nsresult> result = DeleteContentInRange(
          aHTMLEditor, aLimitersAndCaretData, aDirectionAndAmount,
          aStripWrappers, aRangeToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoBlockElementsJoiner::DeleteContentInRange() failed");
      return result;
    }
    case Mode::DeleteNonCollapsedRange:
    case Mode::DeletePrecedingLinesAndContentInRange: {
      Result<EditActionResult, nsresult> result = HandleDeleteNonCollapsedRange(
          aHTMLEditor, aDirectionAndAmount, aStripWrappers, aRangeToDelete,
          aSelectionWasCollapsed, aEditingHost);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoBlockElementsJoiner::HandleDeleteNonCollapsedRange() failed");
      return result;
    }
    case Mode::NotInitialized:
      MOZ_ASSERT_UNREACHABLE("Call Run() after calling a preparation method");
      return EditActionResult::IgnoredResult();
  }
  return Err(NS_ERROR_NOT_INITIALIZED);
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDelete(
        const HTMLEditor& aHTMLEditor,
        const AutoClonedSelectionRangeArray& /*aRangesToDelete*/,
        nsIEditor::EDirection aDirectionAndAmount, dom::Range& aRangeToDelete,
        AutoDeleteRangesHandler::SelectionWasCollapsed aSelectionWasCollapsed,
        const Element& aEditingHost) const {
  switch (mMode) {
    case Mode::JoinCurrentBlock:
    case Mode::JoinOtherBlock:
    case Mode::DeleteBRElement:
    case Mode::DeletePrecedingBRElementOfBlock:
    case Mode::DeletePrecedingPreformattedLineBreak:
      MOZ_ASSERT_UNREACHABLE(
          "This mode should be handled in the other ComputeRangesToDelete()");
      return NS_ERROR_UNEXPECTED;
    case Mode::JoinBlocksInSameParent: {
      nsresult rv = ComputeRangeToJoinBlockElementsInSameParent(
          aHTMLEditor, aDirectionAndAmount, aRangeToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoBlockElementsJoiner::"
          "ComputeRangesToJoinBlockElementsInSameParent() failed");
      return rv;
    }
    case Mode::DeleteContentInRange: {
      nsresult rv = ComputeRangeToDeleteContentInRange(
          aHTMLEditor, aDirectionAndAmount, aRangeToDelete, aEditingHost);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "AutoBlockElementsJoiner::"
                           "ComputeRangesToDeleteContentInRanges() failed");
      return rv;
    }
    case Mode::DeleteNonCollapsedRange:
    case Mode::DeletePrecedingLinesAndContentInRange: {
      nsresult rv = ComputeRangeToDeleteNonCollapsedRange(
          aHTMLEditor, aDirectionAndAmount, aRangeToDelete,
          aSelectionWasCollapsed, aEditingHost);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "AutoBlockElementsJoiner::"
                           "ComputeRangesToDeleteNonCollapsedRanges() failed");
      return rv;
    }
    case Mode::NotInitialized:
      MOZ_ASSERT_UNREACHABLE(
          "Call ComputeRangesToDelete() after calling a preparation method");
      return NS_ERROR_NOT_INITIALIZED;
  }
  return NS_ERROR_NOT_INITIALIZED;
}

nsresult HTMLEditor::ComputeTargetRanges(
    nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> editingHost = ComputeEditingHost();
  if (!editingHost) {
    aRangesToDelete.RemoveAllRanges();
    return NS_ERROR_EDITOR_NO_EDITABLE_RANGE;
  }

  // First check for table selection mode.  If so, hand off to table editor.
  SelectedTableCellScanner scanner(aRangesToDelete);
  if (scanner.IsInTableCellSelectionMode()) {
    // If it's in table cell selection mode, we'll delete all childen in
    // the all selected table cell elements,
    if (scanner.ElementsRef().Length() == aRangesToDelete.Ranges().Length()) {
      return NS_OK;
    }
    // but will ignore all ranges which does not select a table cell.
    size_t removedRanges = 0;
    for (size_t i = 1; i < scanner.ElementsRef().Length(); i++) {
      if (HTMLEditUtils::GetTableCellElementIfOnlyOneSelected(
              aRangesToDelete.Ranges()[i - removedRanges]) !=
          scanner.ElementsRef()[i]) {
        // XXX Need to manage anchor-focus range too!
        aRangesToDelete.Ranges().RemoveElementAt(i - removedRanges);
        removedRanges++;
      }
    }
    return NS_OK;
  }

  aRangesToDelete.EnsureOnlyEditableRanges(*editingHost);
  if (aRangesToDelete.Ranges().IsEmpty()) {
    NS_WARNING(
        "There is no range which we can delete entire of or around the caret");
    return NS_ERROR_EDITOR_NO_EDITABLE_RANGE;
  }
  // Delete each range if completely in a replaced element or a void element
  // because collapsing the range outside may cause the surrounding content
  // which is outside the selection range will be deleted.
  if (aRangesToDelete.AdjustRangesNotInReplacedNorVoidElements(
          AutoClonedRangeArray::RangeInReplacedOrVoidElement::Delete,
          *editingHost) &&
      !aRangesToDelete.Ranges().Length()) {
    return NS_ERROR_EDITOR_NO_EDITABLE_RANGE;
  }
  AutoDeleteRangesHandler deleteHandler;
  // Should we delete target ranges which cannot delete actually?
  nsresult rv = deleteHandler.ComputeRangesToDelete(
      *this, aDirectionAndAmount, aRangesToDelete, *editingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "AutoDeleteRangesHandler::ComputeRangesToDelete() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::HandleDeleteSelection(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aStripWrappers == nsIEditor::eStrip ||
             aStripWrappers == nsIEditor::eNoStrip);

  if (RefPtr editContext = GetEditActionEditContext()) {
    MOZ_ASSERT(
        GetTopLevelEditSubAction() == EditSubAction::eDeleteSelectedContent,
        "Should not reach here if deletion is for preparing to insert text.");
    // https://github.com/w3c/edit-context/issues/88
    uint32_t selectionStart =
        std::min(editContext->SelectionStart(),
                 static_cast<uint32_t>(editContext->TextLength()));
    uint32_t selectionEnd =
        std::min(editContext->SelectionEnd(),
                 static_cast<uint32_t>(editContext->TextLength()));
    if (selectionStart != selectionEnd) {
      // Selection is not collapsed, so we just delete it
      editContext->UpdateTextAndFireEvent(selectionStart, selectionEnd, u""_ns);
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (EditContextChangedSinceStartOfEditAction()) {
        // textupdate handler deactivated this EditContext
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      return EditActionResult::HandledResult();
    }
    RefPtr<PresShell> presShell = GetPresShell();
    if (NS_WARN_IF(!presShell)) {
      return Err(NS_ERROR_FAILURE);
    }
    // Do reflow now to ensure text frame is up to date.
    presShell->FlushPendingNotifications(FlushType::Layout);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (EditContextChangedSinceStartOfEditAction()) {
      // EditContext was deactivated by reflow
      return Err(NS_ERROR_FAILURE);
    }
    RefPtr<nsTextNode> text = &editContext->TextNode();
    if (NS_WARN_IF(!text->GetPrimaryFrame())) {
      // If text node hasn't been framed yet, ExtendAnchorFocusRangeFor won't
      // work.
      return Err(NS_ERROR_FAILURE);
    }
    AutoDeleteRangesHandler deleteHandler;
    EditorDOMPoint point;
    point.Set(text, selectionStart);
    LimitersAndCaretData limitersAndCaretData;
    limitersAndCaretData.mAncestorLimiter = text->GetParentElement();
    AutoClonedSelectionRangeArray rangeArray(point, limitersAndCaretData);
    RefPtr<Element> textContainer = &editContext->TextContainer();
    nsresult rv = deleteHandler.ComputeRangesToDelete(
        *this, aDirectionAndAmount, rangeArray, *textContainer);
    NS_ENSURE_SUCCESS(rv, Err(rv));
    if (rangeArray.Ranges().IsEmpty()) {
      // Deletion was cancelled by AutoCaretBidiLevelManager
      return EditActionResult::HandledResult();
    }
    EditorDOMPoint deletionStart =
        rangeArray.GetFirstRangeStartPoint<EditorDOMPoint>();
    EditorDOMPoint deletionEnd =
        rangeArray.GetFirstRangeEndPoint<EditorDOMPoint>();
    MOZ_ASSERT(deletionStart.GetContainer() == text);
    MOZ_ASSERT(deletionEnd.GetContainer() == text);
    RefPtr<nsFrameSelection> frameSelection = GetEditableFrameSelection();
    AutoCaretBidiLevelManager bidiLevelManager(*this, aDirectionAndAmount,
                                               *editContext);
    editContext->UpdateTextAndFireEvent(deletionStart.Offset(),
                                        deletionEnd.Offset(), u""_ns);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (EditContextChangedSinceStartOfEditAction()) {
      // textupdate handler deactivated this EditContext
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // Set caret association according to whether this is a forward or backward
    // deletion, so that the caret moves to the position of the deleted text in
    // BiDi cases.
    // However, we don't want to do this if the textupdate handler
    // changed the text next to the caret, since then this may not be a simple
    // deletion.
    if (frameSelection &&
        !editContext->WasTextNextToCaretChangedByTextUpdateHandler()) {
      bidiLevelManager.MaybeUpdateCaretBidiLevel(*this);
      frameSelection->SetHint(DirectionIsBackspace(aDirectionAndAmount)
                                  ? CaretAssociationHint::Before
                                  : CaretAssociationHint::After);
      // Otherwise DeleteSelectionAsSubAction will overwrite this.
      TopLevelEditSubActionDataRef().mDidExplicitlySetInterLine = true;
    }
    return EditActionResult::HandledResult();
  }

  if (MOZ_UNLIKELY(!SelectionRef().RangeCount())) {
    return Err(NS_ERROR_EDITOR_NO_EDITABLE_RANGE);
  }

  const RefPtr<Element> editingHost = ComputeEditingHost();
  if (MOZ_UNLIKELY(!editingHost)) {
    return Err(NS_ERROR_EDITOR_NO_EDITABLE_RANGE);
  }

  // Remember that we did a selection deletion.  Used by
  // CreateStyleForInsertText()
  TopLevelEditSubActionDataRef().mDidDeleteSelection = true;

  if (MOZ_UNLIKELY(IsEmpty())) {
    return EditActionResult::CanceledResult();
  }

  // First check for table selection mode.  If so, hand off to table editor.
  if (HTMLEditUtils::IsInTableCellSelectionMode(SelectionRef())) {
    nsresult rv = DeleteTableCellContentsWithTransaction();
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteTableCellContentsWithTransaction() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  AutoClonedSelectionRangeArray rangesToDelete(SelectionRef());
  rangesToDelete.EnsureOnlyEditableRanges(*editingHost);
  // AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor() need to use
  // NodeIsInLimiters() to extend the range for deletion.  But if focus event
  // doesn't receive yet, ancestor hasn't been set yet.  So we need to set
  // ancestor limiter to editing host, <body> or something else in such case.
  if (!rangesToDelete.GetAncestorLimiter()) {
    rangesToDelete.SetAncestorLimiter(FindSelectionRoot(*editingHost));
  }
  if (MOZ_UNLIKELY(rangesToDelete.Ranges().IsEmpty())) {
    NS_WARNING(
        "There is no range which we can delete entire the ranges or around the "
        "caret");
    return Err(NS_ERROR_EDITOR_NO_EDITABLE_RANGE);
  }
  // Delete each range if completely in a replaced element or a void element
  // because collapsing the range outside may cause the surrounding content
  // which is outside the selection range will be deleted.
  if (rangesToDelete.AdjustRangesNotInReplacedNorVoidElements(
          AutoClonedRangeArray::RangeInReplacedOrVoidElement::Delete,
          *editingHost) &&
      rangesToDelete.Ranges().IsEmpty()) {
    // Collapse Selection to the first editable range to avoid the toplevel edit
    // subaction handler to be confused at non-selection ranges.
    if (GetTopLevelEditSubAction() != EditSubAction::eDeleteSelectedContent) {
      AutoClonedSelectionRangeArray editableSelectionRanges(SelectionRef());
      editableSelectionRanges.EnsureOnlyEditableRanges(*editingHost);
      if (!editableSelectionRanges.GetAncestorLimiter()) {
        editableSelectionRanges.SetAncestorLimiter(
            FindSelectionRoot(*editingHost));
      }
      editableSelectionRanges.AdjustRangesNotInReplacedNorVoidElements(
          AutoClonedRangeArray::RangeInReplacedOrVoidElement::Collapse,
          *editingHost);
      if (NS_WARN_IF(editableSelectionRanges.Ranges().IsEmpty())) {
        return Err(NS_ERROR_FAILURE);
      }
      nsresult rv = editableSelectionRanges.Collapse(
          editableSelectionRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>());
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (NS_FAILED(rv)) {
        return Err(rv);
      }
    }
    return Err(NS_ERROR_EDITOR_NO_DELETABLE_RANGE);
  }
  AutoDeleteRangesHandler deleteHandler;
  Result<EditActionResult, nsresult> result = deleteHandler.Run(
      *this, aDirectionAndAmount, aStripWrappers, rangesToDelete, *editingHost);
  if (MOZ_UNLIKELY(result.isErr()) || result.inspect().Canceled()) {
    NS_WARNING_ASSERTION(result.isOk(),
                         "AutoDeleteRangesHandler::Run() failed");
    return result;
  }
  return EditActionResult::HandledResult();
}

Result<EditorDOMPoint, nsresult> HTMLEditor::DeleteLineBreakWithTransaction(
    const EditorLineBreak& aLineBreak,
    nsIEditor::EStripWrappers aDeleteEmptyInlines,
    const Element& aEditingHost) {
  MOZ_ASSERT(aLineBreak.IsInComposedDoc());
  MOZ_ASSERT_IF(aLineBreak.IsPreformattedLineBreak(),
                aLineBreak.CharAtOffsetIsLineBreak());

  if (aLineBreak.IsHTMLBRElement() ||
      aLineBreak.TextIsOnlyPreformattedLineBreak()) {
    const OwningNonNull<nsIContent> nodeToDelete = [&]() -> nsIContent& {
      if (aDeleteEmptyInlines == nsIEditor::eNoStrip) {
        return aLineBreak.ContentRef();
      }
      Element* const newEmptyInlineElement =
          HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
              aLineBreak.ContentRef(),
              BlockInlineCheck::UseComputedDisplayOutsideStyle, &aEditingHost);
      return newEmptyInlineElement ? *newEmptyInlineElement
                                   : aLineBreak.ContentRef();
    }();
    const nsCOMPtr<nsINode> parentNode = nodeToDelete->GetParentNode();
    if (NS_WARN_IF(!parentNode)) {
      return Err(NS_ERROR_FAILURE);
    }
    const nsCOMPtr<nsIContent> nextSibling = nodeToDelete->GetNextSibling();
    nsresult rv = DeleteNodeWithTransaction(nodeToDelete);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    if (NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parentNode) ||
        NS_WARN_IF(!parentNode->IsInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return nextSibling ? EditorDOMPoint(nextSibling)
                       : EditorDOMPoint::AtEndOf(*parentNode);
  }

  const OwningNonNull<Text> textNode(aLineBreak.TextRef());
  Result<CaretPoint, nsresult> caretPointOrError =
      DeleteTextWithTransaction(textNode, aLineBreak.Offset(), 1u);
  if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
    NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
    return caretPointOrError.propagateErr();
  }
  if (NS_WARN_IF(!caretPointOrError.inspect().HasCaretPointSuggestion())) {
    return Err(NS_ERROR_FAILURE);
  }
  return caretPointOrError.unwrap().UnwrapCaretPoint();
}

Result<CaretPoint, nsresult> HTMLEditor::DeleteRangesWithTransaction(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers,
    AutoClonedRangeArray& aRangesToDelete) {
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_UNEXPECTED);
  }

  aRangesToDelete.ExtendRangeToContainSurroundingInvisibleWhiteSpaces(
      aStripWrappers);
  if (MOZ_UNLIKELY(aRangesToDelete.IsCollapsed())) {
    return CaretPoint(EditorDOMPoint(aRangesToDelete.FocusRef()));
  }

  Result<CaretPoint, nsresult> result = EditorBase::DeleteRangesWithTransaction(
      aDirectionAndAmount, aStripWrappers, aRangesToDelete);
  if (MOZ_UNLIKELY(result.isErr())) {
    return result;
  }

  const bool isDeleteSelection =
      GetTopLevelEditSubAction() == EditSubAction::eDeleteSelectedContent;
  EditorDOMPoint pointToPutCaret = result.unwrap().UnwrapCaretPoint();
  MOZ_ASSERT_IF(pointToPutCaret.IsSet(), HTMLEditUtils::IsSimplyEditableNode(
                                             *pointToPutCaret.GetContainer()));
  {
    AutoTrackDOMPoint trackCaretPoint(RangeUpdaterRef(), &pointToPutCaret);
    for (const auto& range : aRangesToDelete.Ranges()) {
      // Refer the start boundary of the range because it should be end of the
      // preceding content, but the end boundary may be in an ancestor when an
      // ancestor element of end boundary has already been deleted.
      if (MOZ_UNLIKELY(!range->IsPositioned() ||
                       !range->GetStartContainer()->IsContent())) {
        continue;
      }
      EditorDOMPoint pointToInsertLineBreak(range->StartRef());
      // Don't remove empty inline elements in the plaintext-only mode because
      // nobody can restore the style again.
      if (aStripWrappers == nsIEditor::eStrip) {
        const OwningNonNull<nsIContent> maybeEmptyContent =
            *pointToInsertLineBreak.ContainerAs<nsIContent>();
        if (MOZ_UNLIKELY(
                !HTMLEditUtils::IsRemovableFromParentNode(maybeEmptyContent))) {
          continue;
        }
        // If the `Text` becomes invisible but has collapsible white-spaces, we
        // shouldn't delete it because the deletion deletes only before or after
        // the white-space to keep the white-space visible.
        if (!maybeEmptyContent->IsText() ||
            !maybeEmptyContent->AsText()->TextDataLength()) {
          Result<CaretPoint, nsresult> caretPointOrError =
              DeleteEmptyInclusiveAncestorInlineElements(maybeEmptyContent,
                                                         *editingHost);
          if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::DeleteEmptyInclusiveAncestorInlineElements() "
                "failed");
            return caretPointOrError.propagateErr();
          }
          if (NS_WARN_IF(!range->IsPositioned() ||
                         !range->GetStartContainer()->IsContent())) {
            continue;
          }
          MOZ_ASSERT_IF(
              caretPointOrError.inspect().HasCaretPointSuggestion(),
              HTMLEditUtils::IsSimplyEditableNode(
                  *caretPointOrError.inspect().CaretPointRef().GetContainer()));
          caretPointOrError.unwrap().MoveCaretPointTo(
              pointToInsertLineBreak, {SuggestCaret::OnlyIfHasSuggestion});
          if (NS_WARN_IF(
                  !pointToInsertLineBreak.IsSetAndValidInComposedDoc())) {
            continue;
          }
        }
      }

      if ((IsMailEditor() || IsPlaintextMailComposer()) &&
          MOZ_LIKELY(pointToInsertLineBreak.IsInContentNode())) {
        AutoTrackDOMPoint trackPointToInsertLineBreak(RangeUpdaterRef(),
                                                      &pointToInsertLineBreak);
        nsresult rv = DeleteMostAncestorMailCiteElementIfEmpty(
            MOZ_KnownLive(*pointToInsertLineBreak.ContainerAs<nsIContent>()));
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty() failed");
          return Err(rv);
        }
        trackPointToInsertLineBreak.Flush(StopTracking::Yes);
        if (NS_WARN_IF(!pointToInsertLineBreak.IsSetAndValidInComposedDoc())) {
          continue;
        }
        MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(
            *pointToInsertLineBreak.GetContainer()));
      }

      if (isDeleteSelection) {
        {
          AutoTrackDOMPoint trackPointToInsertLineBreak(
              RangeUpdaterRef(), &pointToInsertLineBreak);
          nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
              pointToInsertLineBreak, PreservePreformattedLineBreak::No,
              PaddingForEmptyBlock::Significant, *editingHost);
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
            return Err(rv);
          }
          trackPointToInsertLineBreak.Flush(StopTracking::Yes);
          if (NS_WARN_IF(!pointToInsertLineBreak
                              .IsInContentNodeAndValidInComposedDoc())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
        }
        Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
            InsertPaddingBRElementIfNeeded(
                pointToInsertLineBreak,
                editingHost->IsContentEditablePlainTextOnly()
                    ? nsIEditor::eNoStrip
                    : nsIEditor::eStrip,
                *editingHost);
        if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
          NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
          return insertPaddingBRElementOrError.propagateErr();
        }
        insertPaddingBRElementOrError.unwrap().IgnoreCaretPointSuggestion();
      }
    }
  }
  return CaretPoint(std::move(pointToPutCaret));
}

nsresult HTMLEditor::AutoDeleteRangesHandler::ComputeRangesToDelete(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());

  // Don't modify Selection while computing the target ranges. Otherwise, only
  // us would dispatch `selectionchange` event when the editing host is for
  // EditContext.
  SelectionChangeGuard guard;
  const auto assertNoSelectionChange =
      MakeScopeExit([&]() { MOZ_ASSERT(!guard.Changed(0)); });

  mOriginalDirectionAndAmount = aDirectionAndAmount;
  mOriginalStripWrappers = nsIEditor::eNoStrip;

  if (aHTMLEditor.mPaddingBRElementForEmptyEditor) {
    nsresult rv = aRangesToDelete.Collapse(
        EditorRawDOMPoint(aHTMLEditor.mPaddingBRElementForEmptyEditor));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoClonedRangeArray::Collapse() failed");
    return rv;
  }

  SelectionWasCollapsed selectionWasCollapsed = aRangesToDelete.IsCollapsed()
                                                    ? SelectionWasCollapsed::Yes
                                                    : SelectionWasCollapsed::No;
  if (selectionWasCollapsed == SelectionWasCollapsed::Yes) {
    const auto startPoint =
        aRangesToDelete.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!startPoint.IsSet())) {
      return NS_ERROR_FAILURE;
    }
    if (startPoint.IsInContentNode()) {
      AutoEmptyBlockAncestorDeleter deleter;
      if (deleter.ScanEmptyBlockInclusiveAncestor(
              aHTMLEditor, *startPoint.ContainerAs<nsIContent>())) {
        nsresult rv = deleter.ComputeTargetRanges(
            aHTMLEditor, aDirectionAndAmount, aEditingHost, aRangesToDelete);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "AutoEmptyBlockAncestorDeleter::ComputeTargetRanges() failed");
        return rv;
      }
    }

    // We shouldn't update caret bidi level right now, but we need to check
    // whether the deletion will be canceled or not.
    AutoCaretBidiLevelManager bidiLevelManager(aHTMLEditor, aDirectionAndAmount,
                                               startPoint);
    if (bidiLevelManager.Failed()) {
      NS_WARNING(
          "EditorBase::AutoCaretBidiLevelManager failed to initialize itself");
      return NS_ERROR_FAILURE;
    }
    if (bidiLevelManager.Canceled()) {
      return NS_SUCCESS_DOM_NO_OPERATION;
    }

    Result<nsIEditor::EDirection, nsresult> extendResult =
        aRangesToDelete.ExtendAnchorFocusRangeFor(aHTMLEditor,
                                                  aDirectionAndAmount);
    if (extendResult.isErr()) [[unlikely]] {
      // Couldn't extend the range to outside the limiter. Don't warn this nor
      // return error since the user may press Backspace or Delete at start or
      // end of the editing host or a table cell/caption element.
      return NS_OK;
    }

    // For compatibility with other browsers, we should set target ranges
    // to start from and/or end after an atomic content rather than start
    // from preceding text node end nor end at following text node start.
    Result<bool, nsresult> shrunkenResult =
        aRangesToDelete.ShrinkRangesIfStartFromOrEndAfterAtomicContent(
            aHTMLEditor, aDirectionAndAmount,
            AutoClonedRangeArray::IfSelectingOnlyOneAtomicContent::Collapse);
    if (shrunkenResult.isErr()) {
      NS_WARNING(
          "AutoClonedRangeArray::"
          "ShrinkRangesIfStartFromOrEndAfterAtomicContent() "
          "failed");
      return shrunkenResult.unwrapErr();
    }

    if (!shrunkenResult.inspect() || !aRangesToDelete.IsCollapsed()) {
      aDirectionAndAmount = extendResult.unwrap();
    }

    if (aDirectionAndAmount == nsIEditor::eNone) {
      MOZ_ASSERT(aRangesToDelete.Ranges().Length() == 1);
      if (!CanFallbackToDeleteRangesWithTransaction(aRangesToDelete)) {
        // XXX In this case, do we need to modify the range again?
        return NS_SUCCESS_DOM_NO_OPERATION;
      }
      nsresult rv = FallbackToComputeRangesToDeleteRangesWithTransaction(
          aHTMLEditor, aRangesToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoDeleteRangesHandler::"
          "FallbackToComputeRangesToDeleteRangesWithTransaction() failed");
      return rv;
    }

    if (aRangesToDelete.IsCollapsed()) {
      const auto caretPoint =
          aRangesToDelete.GetFirstRangeStartPoint<EditorDOMPoint>();
      if (MOZ_UNLIKELY(NS_WARN_IF(!caretPoint.IsInContentNode()))) {
        return NS_ERROR_FAILURE;
      }
      if (!EditorUtils::IsEditableContent(*caretPoint.ContainerAs<nsIContent>(),
                                          EditorType::HTML)) {
        return NS_SUCCESS_DOM_NO_OPERATION;
      }
      const WSRunScanner wsRunScannerAtCaret(
          {WSRunScanner::Option::OnlyEditableNodes}, caretPoint);
      const WSScanResult scanFromCaretPointResult =
          aDirectionAndAmount == nsIEditor::eNext
              ? wsRunScannerAtCaret
                    .ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(caretPoint)
              : wsRunScannerAtCaret.ScanPreviousVisibleNodeOrBlockBoundaryFrom(
                    caretPoint);
      if (scanFromCaretPointResult.Failed()) {
        NS_WARNING(
            "WSRunScanner::Scan(Next|Previous)VisibleNodeOrBlockBoundaryFrom() "
            "failed");
        return NS_ERROR_FAILURE;
      }
      MOZ_ASSERT(scanFromCaretPointResult.GetContent());

      // FIXME: Handle preformatted linefeed too.
      if (scanFromCaretPointResult.ReachedBRElement()) {
        if (scanFromCaretPointResult.BRElementPtr() == &aEditingHost) {
          return NS_OK;
        }
        if (!scanFromCaretPointResult.ContentIsEditable()) {
          return NS_SUCCESS_DOM_NO_OPERATION;
        }
        if (scanFromCaretPointResult
                .ReachedBRElementFollowedByBlockBoundary()) {
          EditorDOMPoint newCaretPosition =
              aDirectionAndAmount == nsIEditor::eNext
                  ? scanFromCaretPointResult
                        .PointAfterReachedContent<EditorDOMPoint>()
                  : scanFromCaretPointResult
                        .PointAtReachedContent<EditorDOMPoint>();
          if (NS_WARN_IF(!newCaretPosition.IsSet())) {
            return NS_ERROR_FAILURE;
          }
          nsresult rv = aRangesToDelete.Collapse(newCaretPosition);
          if (NS_FAILED(rv)) {
            NS_WARNING("AutoClonedSelectionRangeArray::Collapse() failed");
            return NS_ERROR_FAILURE;
          }
          MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
          AutoDeleteRangesHandler anotherHandler(this);
          rv = anotherHandler.ComputeRangesToDelete(
              aHTMLEditor, aDirectionAndAmount, aRangesToDelete, aEditingHost);
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "Recursive AutoDeleteRangesHandler::ComputeRangesToDelete() "
              "failed");

          MOZ_ASSERT(aRangesToDelete.Ranges().Length() == 1);
          // If the range is collapsed, there is no content which should
          // be removed together.  In this case, only the invisible `<br>`
          // element should be selected.
          if (aRangesToDelete.IsCollapsed()) {
            nsresult rv = aRangesToDelete.SelectNode(
                *scanFromCaretPointResult.BRElementPtr());
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "AutoClonedRangeArray::SelectNode() failed");
            return rv;
          }

          // Otherwise, extend the range to contain the invisible `<br>`
          // element.
          if (scanFromCaretPointResult
                  .PointAtReachedContent<EditorRawDOMPoint>()
                  .IsBefore(
                      aRangesToDelete
                          .GetFirstRangeStartPoint<EditorRawDOMPoint>())) {
            nsresult rv = aRangesToDelete.FirstRangeRef()->SetStartAndEnd(
                EditorRawDOMPoint(scanFromCaretPointResult.BRElementPtr())
                    .ToRawRangeBoundary(),
                aRangesToDelete.FirstRangeRef()->EndRef());
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "Range::SetStartAndEnd() failed");
            return rv;
          }
          if (aRangesToDelete.GetFirstRangeEndPoint<EditorRawDOMPoint>()
                  .IsBefore(
                      scanFromCaretPointResult
                          .PointAfterReachedContent<EditorRawDOMPoint>())) {
            nsresult rv = aRangesToDelete.FirstRangeRef()->SetStartAndEnd(
                aRangesToDelete.FirstRangeRef()->StartRef(),
                scanFromCaretPointResult
                    .PointAfterReachedContent<EditorRawDOMPoint>()
                    .ToRawRangeBoundary());
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "Range::SetStartAndEnd() failed");
            return rv;
          }
          NS_WARNING("Was the invisible `<br>` element selected?");
          return NS_OK;
        }
      }

      nsresult rv = ComputeRangesToDeleteAroundCollapsedRanges(
          aHTMLEditor, aDirectionAndAmount, aRangesToDelete,
          wsRunScannerAtCaret, scanFromCaretPointResult, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoDeleteRangesHandler::ComputeRangesToDeleteAroundCollapsedRanges("
          ") failed");
      return rv;
    }
  }

  if (selectionWasCollapsed == SelectionWasCollapsed::No) {
    aRangesToDelete.MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace(
        aHTMLEditor);
  }
  nsresult rv = ComputeRangesToDeleteNonCollapsedRanges(
      aHTMLEditor, aDirectionAndAmount, aRangesToDelete, selectionWasCollapsed,
      aEditingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoDeleteRangesHandler::"
                       "ComputeRangesToDeleteNonCollapsedRanges() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::Run(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aStripWrappers == nsIEditor::eStrip ||
             aStripWrappers == nsIEditor::eNoStrip);
  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());

  mOriginalDirectionAndAmount = aDirectionAndAmount;
  mOriginalStripWrappers = aStripWrappers;

  if (MOZ_UNLIKELY(aHTMLEditor.IsEmpty())) {
    return EditActionResult::CanceledResult();
  }

  // selectionWasCollapsed is used later to determine whether we should join
  // blocks in HandleDeleteNonCollapsedRanges(). We don't really care about
  // collapsed because it will be modified by
  // AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor() later.
  // AutoBlockElementsJoiner::AutoInclusiveAncestorBlockElementsJoiner should
  // happen if the original selection is collapsed and the cursor is at the end
  // of a block element, in which case
  // AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor() would always
  // make the selection not collapsed.
  SelectionWasCollapsed selectionWasCollapsed = aRangesToDelete.IsCollapsed()
                                                    ? SelectionWasCollapsed::Yes
                                                    : SelectionWasCollapsed::No;

  if (selectionWasCollapsed == SelectionWasCollapsed::Yes) {
    const auto startPoint =
        aRangesToDelete.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!startPoint.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }

    // If we are inside an empty block, delete it.
    if (startPoint.IsInContentNode()) {
#ifdef DEBUG
      nsMutationGuard debugMutation;
#endif  // #ifdef DEBUG
      AutoEmptyBlockAncestorDeleter deleter;
      if (deleter.ScanEmptyBlockInclusiveAncestor(
              aHTMLEditor, *startPoint.ContainerAs<nsIContent>())) {
        Result<DeleteRangeResult, nsresult> deleteResultOrError =
            deleter.Run(aHTMLEditor, aDirectionAndAmount, aEditingHost);
        if (MOZ_UNLIKELY(deleteResultOrError.isErr())) {
          NS_WARNING("AutoEmptyBlockAncestorDeleter::Run() failed");
          return deleteResultOrError.propagateErr();
        }
        DeleteRangeResult deleteResult = deleteResultOrError.unwrap();
        if (deleteResult.Handled()) {
          nsresult rv = deleteResult.SuggestCaretPointTo(
              aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion});
          if (NS_FAILED(rv)) {
            NS_WARNING("CaretPoint::SuggestCaretPoint() failed");
            return Err(rv);
          }
          return EditActionResult::HandledResult();
        }
      }
      MOZ_ASSERT(!debugMutation.Mutated(0),
                 "AutoEmptyBlockAncestorDeleter shouldn't modify the DOM tree "
                 "if it returns not handled nor error");
    }

    // Test for distance between caret and text that will be deleted.
    AutoCaretBidiLevelManager bidiLevelManager(aHTMLEditor, aDirectionAndAmount,
                                               startPoint);
    if (MOZ_UNLIKELY(bidiLevelManager.Failed())) {
      NS_WARNING(
          "EditorBase::AutoCaretBidiLevelManager failed to initialize itself");
      return Err(NS_ERROR_FAILURE);
    }
    bidiLevelManager.MaybeUpdateCaretBidiLevel(aHTMLEditor);
    if (bidiLevelManager.Canceled()) {
      return EditActionResult::CanceledResult();
    }

    // Calling `ExtendAnchorFocusRangeFor()` and
    // `ShrinkRangesIfStartFromOrEndAfterAtomicContent()` may move caret to
    // the container of deleting atomic content.  However, it may be different
    // from the original caret's container.  The original caret container may
    // be important to put caret after deletion so that let's cache the
    // original position.
    Maybe<EditorDOMPoint> caretPoint;
    if (aRangesToDelete.IsCollapsed() && !aRangesToDelete.Ranges().IsEmpty()) {
      caretPoint =
          Some(aRangesToDelete.GetFirstRangeStartPoint<EditorDOMPoint>());
      if (NS_WARN_IF(!caretPoint.ref().IsInContentNode())) {
        return Err(NS_ERROR_FAILURE);
      }
    }

    Result<nsIEditor::EDirection, nsresult> extendResult =
        aRangesToDelete.ExtendAnchorFocusRangeFor(aHTMLEditor,
                                                  aDirectionAndAmount);
    if (extendResult.isErr()) [[unlikely]] {
      // Couldn't extend the range to outside the limiter. Don't warn this nor
      // return error since the user may press Backspace or Delete at start or
      // end of the editing host or a table cell/caption element.
      return EditActionResult::HandledResult();
    }
    if (caretPoint.isSome() &&
        MOZ_UNLIKELY(!caretPoint.ref().IsSetAndValid())) {
      NS_WARNING("The caret position became invalid");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // If there is only one range and it selects an atomic content, we should
    // delete it with collapsed range path for making consistent behavior
    // between both cases, the content is selected case and caret is at it or
    // after it case.
    Result<bool, nsresult> shrunkenResult =
        aRangesToDelete.ShrinkRangesIfStartFromOrEndAfterAtomicContent(
            aHTMLEditor, aDirectionAndAmount,
            AutoClonedRangeArray::IfSelectingOnlyOneAtomicContent::Collapse);
    if (MOZ_UNLIKELY(shrunkenResult.isErr())) {
      NS_WARNING(
          "AutoClonedRangeArray::"
          "ShrinkRangesIfStartFromOrEndAfterAtomicContent() "
          "failed");
      return shrunkenResult.propagateErr();
    }

    if (!shrunkenResult.inspect() || !aRangesToDelete.IsCollapsed()) {
      aDirectionAndAmount = extendResult.unwrap();
    }

    if (aDirectionAndAmount == nsIEditor::eNone) {
      MOZ_ASSERT(aRangesToDelete.Ranges().Length() == 1);
      if (!CanFallbackToDeleteRangesWithTransaction(aRangesToDelete)) {
        return EditActionResult::IgnoredResult();
      }
      Result<CaretPoint, nsresult> caretPointOrError =
          FallbackToDeleteRangesWithTransaction(aHTMLEditor, aRangesToDelete,
                                                aEditingHost);
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING(
            "AutoDeleteRangesHandler::FallbackToDeleteRangesWithTransaction() "
            "failed");
        return caretPointOrError.propagateErr();
      }
      nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
          aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                        SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                        SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CaretPoint::SuggestCaretPointTo() failed, but ignored");
      // Don't return "ignored" to avoid to fall it back to delete ranges
      // recursively.
      return EditActionResult::HandledResult();
    }

    if (aRangesToDelete.IsCollapsed()) {
      // Use the original caret position for handling the deletion around
      // collapsed range because the container may be different from the
      // new collapsed position's container.
      if (!EditorUtils::IsEditableContent(
              *caretPoint.ref().ContainerAs<nsIContent>(), EditorType::HTML)) {
        return EditActionResult::CanceledResult();
      }
      const WSRunScanner wsRunScannerAtCaret(
          {WSRunScanner::Option::OnlyEditableNodes}, caretPoint.ref());
      const WSScanResult scanFromCaretPointResult =
          aDirectionAndAmount == nsIEditor::eNext
              ? wsRunScannerAtCaret
                    .ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
                        caretPoint.ref())
              : wsRunScannerAtCaret.ScanPreviousVisibleNodeOrBlockBoundaryFrom(
                    caretPoint.ref());
      if (MOZ_UNLIKELY(scanFromCaretPointResult.Failed())) {
        NS_WARNING(
            "WSRunScanner::Scan(Next|Previous)VisibleNodeOrBlockBoundaryFrom() "
            "failed");
        return Err(NS_ERROR_FAILURE);
      }
      MOZ_ASSERT(scanFromCaretPointResult.GetContent());

      // Short circuit for invisible breaks.  delete them and recurse.
      // FIXME: Handle preformatted linefeed too.
      if (scanFromCaretPointResult.ReachedBRElement()) {
        if (scanFromCaretPointResult.BRElementPtr() == &aEditingHost) {
          return EditActionResult::HandledResult();
        }
        if (!scanFromCaretPointResult.ContentIsEditable()) {
          return EditActionResult::CanceledResult();
        }
        if (scanFromCaretPointResult
                .ReachedBRElementFollowedByBlockBoundary()) {
          // TODO: We should extend the range to delete again before/after
          //       the caret point and use `HandleDeleteNonCollapsedRanges()`
          //       instead after we would create delete range computation
          //       method at switching to the new white-space normalizer.
          Result<CaretPoint, nsresult> caretPointOrError =
              WhiteSpaceVisibilityKeeper::
                  DeleteContentNodeAndJoinTextNodesAroundIt(
                      aHTMLEditor,
                      MOZ_KnownLive(*scanFromCaretPointResult.BRElementPtr()),
                      caretPoint.ref(), aEditingHost);
          if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
            NS_WARNING(
                "WhiteSpaceVisibilityKeeper::"
                "DeleteContentNodeAndJoinTextNodesAroundIt() failed");
            return caretPointOrError.propagateErr();
          }
          if (caretPointOrError.inspect().HasCaretPointSuggestion()) {
            caretPoint = Some(caretPointOrError.unwrap().UnwrapCaretPoint());
          }
          if (NS_WARN_IF(!caretPoint->IsSetAndValid())) {
            return Err(NS_ERROR_FAILURE);
          }
          AutoClonedSelectionRangeArray rangesToDelete(
              caretPoint.ref(), aRangesToDelete.LimitersAndCaretDataRef());
          if (NS_WARN_IF(rangesToDelete.Ranges().IsEmpty())) {
            return Err(NS_ERROR_FAILURE);
          }
          if (aHTMLEditor.MaybeNodeRemovalsObservedByDevTools()) {
            // Let's check whether there is new invisible `<br>` element
            // for avoiding infinite recursive calls.
            const WSRunScanner wsRunScannerAtCaret(
                {WSRunScanner::Option::OnlyEditableNodes}, caretPoint.ref());
            const WSScanResult scanFromCaretPointResult =
                aDirectionAndAmount == nsIEditor::eNext
                    ? wsRunScannerAtCaret
                          .ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
                              caretPoint.ref())
                    : wsRunScannerAtCaret
                          .ScanPreviousVisibleNodeOrBlockBoundaryFrom(
                              caretPoint.ref());
            if (MOZ_UNLIKELY(scanFromCaretPointResult.Failed())) {
              NS_WARNING(
                  "WSRunScanner::Scan(Next|Previous)"
                  "VisibleNodeOrBlockBoundaryFrom() failed");
              return Err(NS_ERROR_FAILURE);
            }
            if (NS_WARN_IF(scanFromCaretPointResult
                               .ReachedBRElementFollowedByBlockBoundary())) {
              return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
            }
          }
          AutoDeleteRangesHandler anotherHandler(this);
          Result<EditActionResult, nsresult> result =
              anotherHandler.Run(aHTMLEditor, aDirectionAndAmount,
                                 aStripWrappers, rangesToDelete, aEditingHost);
          NS_WARNING_ASSERTION(
              result.isOk(), "Recursive AutoDeleteRangesHandler::Run() failed");
          return result;
        }
      }

      Result<EditActionResult, nsresult> result =
          HandleDeleteAroundCollapsedRanges(
              aHTMLEditor, aDirectionAndAmount, aStripWrappers, aRangesToDelete,
              wsRunScannerAtCaret, scanFromCaretPointResult, aEditingHost);
      NS_WARNING_ASSERTION(result.isOk(),
                           "AutoDeleteRangesHandler::"
                           "HandleDeleteAroundCollapsedRanges() failed");
      return result;
    }
  }

  if (selectionWasCollapsed == SelectionWasCollapsed::No) {
    aRangesToDelete.MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace(
        aHTMLEditor);
  }
  Result<EditActionResult, nsresult> result = HandleDeleteNonCollapsedRanges(
      aHTMLEditor, aDirectionAndAmount, aStripWrappers, aRangesToDelete,
      selectionWasCollapsed, aEditingHost);
  NS_WARNING_ASSERTION(
      result.isOk(),
      "AutoDeleteRangesHandler::HandleDeleteNonCollapsedRanges() failed");
  return result;
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::ComputeRangesToDeleteAroundCollapsedRanges(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const WSRunScanner& aWSRunScannerAtCaret,
    const WSScanResult& aScanFromCaretPointResult,
    const Element& aEditingHost) const {
  if (aScanFromCaretPointResult.InCollapsibleWhiteSpaces() ||
      aScanFromCaretPointResult.InNonCollapsibleCharacters() ||
      aScanFromCaretPointResult.ReachedPreformattedLineBreak()) {
    // This means that if aDirectionAndAmount == nsIEditor::eNext, collapse
    // selection at the found character.  Otherwise, collapse selection after
    // the found character.
    nsresult rv = aRangesToDelete.Collapse(
        aScanFromCaretPointResult.Point_Deprecated<EditorRawDOMPoint>());
    if (MOZ_UNLIKELY(NS_FAILED(rv))) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return NS_ERROR_FAILURE;
    }
    rv = ComputeRangesToDeleteTextAroundCollapsedRanges(aDirectionAndAmount,
                                                        aRangesToDelete);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoDeleteRangesHandler::"
        "ComputeRangesToDeleteTextAroundCollapsedRanges() failed");
    return rv;
  }

  if (aScanFromCaretPointResult.ReachedSpecialContent() ||
      aScanFromCaretPointResult.ReachedEmptyInlineContainerElement() ||
      aScanFromCaretPointResult.ReachedBRElement() ||
      aScanFromCaretPointResult.ReachedHRElement() ||
      aScanFromCaretPointResult.ReachedNonEditableOtherBlockElement()) {
    if (aScanFromCaretPointResult.GetContent() == &aEditingHost) {
      return NS_OK;
    }
    nsIContent* atomicContent = GetAtomicContentToDelete(
        aDirectionAndAmount, aWSRunScannerAtCaret, aScanFromCaretPointResult);
    if (!HTMLEditUtils::IsRemovableNode(*atomicContent)) {
      NS_WARNING(
          "AutoDeleteRangesHandler::GetAtomicContentToDelete() cannot find "
          "removable atomic content");
      return NS_ERROR_FAILURE;
    }
    nsresult rv =
        ComputeRangesToDeleteAtomicContent(*atomicContent, aRangesToDelete);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoDeleteRangesHandler::ComputeRangesToDeleteAtomicContent() failed");
    return rv;
  }

  const auto MaybeComputeRangeBetweenCaretAndBlockBoundary =
      [&](const EditorRawDOMPoint& aAtBlockBoundary,
          const OwningNonNull<dom::Range>& aOutRange) MOZ_NEVER_INLINE_DEBUG {
        // If the range is collapsed, we don't need to delete anything.
        if (aAtBlockBoundary == aWSRunScannerAtCaret.ScanStartRef()) {
          return NS_SUCCESS_DOM_NO_OPERATION;
        }
        // If the range contains only invisible whitespaces, we don't need
        // to delete them.
        constexpr WSRunScanner::Options options = {
            WSRunScanner::Option::StopAtAnyEmptyInlineContainers,
            WSRunScanner::Option::StopAtComment};
        const WSScanResult scanResult =
            nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
                ? WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
                      options, aWSRunScannerAtCaret.ScanStartRef(),
                      &aEditingHost)
                : WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                      options, aWSRunScannerAtCaret.ScanStartRef());
        if (scanResult.ReachedBlockBoundary()) {
          return NS_SUCCESS_DOM_NO_OPERATION;
        }
        const EditorRawDOMRange rangeToDelete =
            aAtBlockBoundary.IsBefore(aWSRunScannerAtCaret.ScanStartRef())
                ? EditorRawDOMRange(aAtBlockBoundary,
                                    aWSRunScannerAtCaret.ScanStartRef())
                : EditorRawDOMRange(aWSRunScannerAtCaret.ScanStartRef(),
                                    aAtBlockBoundary);
        AutoClonedSelectionRangeArray rangesToDelete(
            rangeToDelete, aHTMLEditor.SelectionLimitersAndCaretData());
        nsresult rv = ComputeRangesToDeleteNonCollapsedRanges(
            aHTMLEditor, aDirectionAndAmount, rangesToDelete,
            SelectionWasCollapsed::Yes, aEditingHost);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "AutoDeleteRangeHandler::"
              "ComputeRangesToDeleteNonCollapsedRanges() failed");
          return rv;
        }
        if (rv == NS_SUCCESS_DOM_NO_OPERATION) {
          return NS_SUCCESS_DOM_NO_OPERATION;
        }
        MOZ_ASSERT(rangesToDelete.Ranges().Length() == 1);
        const RefPtr<dom::Range> range = rangesToDelete.Ranges()[0];
        rv = aOutRange->SetStartAndEnd(range->StartRef(), range->EndRef());
        if (NS_FAILED(rv)) {
          NS_WARNING("Range::SetStartAndEnd() failed");
          return rv;
        }
        return NS_OK;
      };

  if (aScanFromCaretPointResult.ReachedOtherBlockElement()) {
    if (NS_WARN_IF(!aScanFromCaretPointResult.ContentIsElement())) {
      return NS_ERROR_FAILURE;
    }
    MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
    bool handled = false;
    for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
      MOZ_ASSERT(range->IsPositioned());
      AutoBlockElementsJoiner joiner(*this);
      if (!joiner.PrepareToDeleteAtOtherBlockBoundary(
              aHTMLEditor, aDirectionAndAmount,
              *aScanFromCaretPointResult.ElementPtr(),
              aWSRunScannerAtCaret.ScanStartRef(), aWSRunScannerAtCaret)) {
        // Even if we cannot join current line and the first/last line of the
        // other block, there may be empty nodes which may be either visible or
        // invisible. We need to delete them for the compatibility.
        const EditorRawDOMPoint atOtherBlockBoundary =
            nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
                ? EditorRawDOMPoint::After(
                      *aScanFromCaretPointResult.ElementPtr())
                : EditorRawDOMPoint(aScanFromCaretPointResult.ElementPtr());
        nsresult rv = MaybeComputeRangeBetweenCaretAndBlockBoundary(
            atOtherBlockBoundary, range);
        if (NS_FAILED(rv)) {
          NS_WARNING("MaybeComputeRangeBetweenCaretAndBlockBoundary() failed");
          return rv;
        }
        handled |= rv == NS_OK;
        continue;
      }
      handled = true;
      nsresult rv = joiner.ComputeRangeToDelete(
          aHTMLEditor, aDirectionAndAmount, aWSRunScannerAtCaret.ScanStartRef(),
          range, aEditingHost);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "AutoBlockElementsJoiner::ComputeRangeToDelete() failed (other "
            "block boundary)");
        return rv;
      }
    }
    return handled ? NS_OK : NS_SUCCESS_DOM_NO_OPERATION;
  }

  if (aScanFromCaretPointResult.ReachedCurrentBlockBoundary() ||
      aScanFromCaretPointResult.ReachedInlineEditingHostBoundary()) {
    MOZ_ASSERT(aScanFromCaretPointResult.ContentIsElement());
    MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
    bool handled = false;
    for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
      AutoBlockElementsJoiner joiner(*this);
      if (!joiner.PrepareToDeleteAtCurrentBlockBoundary(
              aHTMLEditor, aDirectionAndAmount,
              *aScanFromCaretPointResult.ElementPtr(),
              aWSRunScannerAtCaret.ScanStartRef(), aEditingHost)) {
        // Even if we cannot join current line and the line immediately
        // before/after the current block, there may be empty nodes which may be
        // either visible or invisible. We need to delete them for the
        // compatibility.
        const EditorRawDOMPoint atCurrentBlockBoundary =
            nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
                ? EditorRawDOMPoint(aScanFromCaretPointResult.ElementPtr(), 0u)
                : EditorRawDOMPoint::AtEndOf(
                      *aScanFromCaretPointResult.ElementPtr());
        nsresult rv = MaybeComputeRangeBetweenCaretAndBlockBoundary(
            atCurrentBlockBoundary, range);
        if (NS_FAILED(rv)) {
          NS_WARNING("MaybeComputeRangeBetweenCaretAndBlockBoundary() failed");
          return rv;
        }
        handled |= rv == NS_OK;
        continue;
      }
      handled = true;
      nsresult rv = joiner.ComputeRangeToDelete(
          aHTMLEditor, aDirectionAndAmount, aWSRunScannerAtCaret.ScanStartRef(),
          range, aEditingHost);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "AutoBlockElementsJoiner::ComputeRangeToDelete() failed (current "
            "block boundary)");
        return rv;
      }
    }
    return handled ? NS_OK : NS_SUCCESS_DOM_NO_OPERATION;
  }

  return NS_OK;
}

Result<EditActionResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::HandleDeleteAroundCollapsedRanges(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const WSRunScanner& aWSRunScannerAtCaret,
    const WSScanResult& aScanFromCaretPointResult,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(aRangesToDelete.IsCollapsed());
  MOZ_ASSERT(aDirectionAndAmount != nsIEditor::eNone);
  MOZ_ASSERT(aWSRunScannerAtCaret.ScanStartRef().IsInContentNode());
  MOZ_ASSERT(EditorUtils::IsEditableContent(
      *aWSRunScannerAtCaret.ScanStartRef().ContainerAs<nsIContent>(),
      EditorType::HTML));

  if (aScanFromCaretPointResult.InCollapsibleWhiteSpaces() ||
      aScanFromCaretPointResult.InNonCollapsibleCharacters() ||
      aScanFromCaretPointResult.ReachedPreformattedLineBreak()) {
    // This means that if aDirectionAndAmount == nsIEditor::eNext, collapse
    // selection at the found character.  Otherwise, collapse selection after
    // the found character.
    nsresult rv = aRangesToDelete.Collapse(
        aScanFromCaretPointResult.Point_Deprecated<EditorRawDOMPoint>());
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return Err(NS_ERROR_FAILURE);
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        HandleDeleteTextAroundCollapsedRanges(aHTMLEditor, aDirectionAndAmount,
                                              aRangesToDelete, aEditingHost);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING(
          "AutoDeleteRangesHandler::HandleDeleteTextAroundCollapsedRanges() "
          "failed");
      return caretPointOrError.propagateErr();
    }
    rv = caretPointOrError.unwrap().SuggestCaretPointTo(
        aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                      SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                      SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                         "CaretPoint::SuggestCaretPoint() failed, but ignored");
    return EditActionResult::HandledResult();
  }

  if (aScanFromCaretPointResult.ReachedSpecialContent() ||
      aScanFromCaretPointResult.ReachedEmptyInlineContainerElement() ||
      aScanFromCaretPointResult.ReachedBRElement() ||
      aScanFromCaretPointResult.ReachedHRElement() ||
      aScanFromCaretPointResult.ReachedNonEditableOtherBlockElement()) {
    if (aScanFromCaretPointResult.GetContent() == &aEditingHost) {
      return EditActionResult::HandledResult();
    }
    nsCOMPtr<nsIContent> atomicContent = GetAtomicContentToDelete(
        aDirectionAndAmount, aWSRunScannerAtCaret, aScanFromCaretPointResult);
    if (MOZ_UNLIKELY(!HTMLEditUtils::IsRemovableNode(*atomicContent))) {
      NS_WARNING(
          "AutoDeleteRangesHandler::GetAtomicContentToDelete() cannot find "
          "removable atomic content");
      return Err(NS_ERROR_FAILURE);
    }
    Result<CaretPoint, nsresult> caretPointOrError = HandleDeleteAtomicContent(
        aHTMLEditor, *atomicContent, aWSRunScannerAtCaret.ScanStartRef(),
        aWSRunScannerAtCaret, aEditingHost);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("AutoDeleteRangesHandler::HandleDeleteAtomicContent() failed");
      return caretPointOrError.propagateErr();
    }
    nsresult rv = caretPointOrError.unwrap().SuggestCaretPointTo(
        aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion});
    if (NS_FAILED(rv)) {
      NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "CaretPoint::SuggestCaretPointTo() failed, but ignored");
    return EditActionResult::HandledResult();
  }

  const auto MaybeDeleteContentBetweenCaretAndBlockBoundary =
      [&](const EditorRawDOMPoint& aAtBlockBoundary)
          MOZ_CAN_RUN_SCRIPT MOZ_NEVER_INLINE_DEBUG
      -> Result<EditActionResult, nsresult> {
    // If the range is collapsed, we don't need to delete anything.
    if (aAtBlockBoundary == aWSRunScannerAtCaret.ScanStartRef()) {
      return EditActionResult::IgnoredResult();
    }
    // If the range contains only invisible whitespaces, we don't need
    // to delete them.
    // FYI: This is expected by /editing/run/delete.html
    constexpr WSRunScanner::Options options = {
        WSRunScanner::Option::StopAtAnyEmptyInlineContainers,
        WSRunScanner::Option::StopAtComment};
    const WSScanResult scanResult =
        nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
            ? WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
                  options, aWSRunScannerAtCaret.ScanStartRef(), &aEditingHost)
            : WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                  options, aWSRunScannerAtCaret.ScanStartRef());
    if (scanResult.ReachedBlockBoundary()) {
      return EditActionResult::IgnoredResult();
    }
    const EditorRawDOMRange rangeToDelete =
        aAtBlockBoundary.IsBefore(aWSRunScannerAtCaret.ScanStartRef())
            ? EditorRawDOMRange(aAtBlockBoundary,
                                aWSRunScannerAtCaret.ScanStartRef())
            : EditorRawDOMRange(aWSRunScannerAtCaret.ScanStartRef(),
                                aAtBlockBoundary);
    AutoClonedSelectionRangeArray rangesToDelete(
        rangeToDelete, aHTMLEditor.SelectionLimitersAndCaretData());
    Result<EditActionResult, nsresult> result = HandleDeleteNonCollapsedRanges(
        aHTMLEditor, aDirectionAndAmount, nsIEditor::eStrip, rangesToDelete,
        SelectionWasCollapsed::Yes, aEditingHost);
    NS_WARNING_ASSERTION(result.isOk(),
                         "HTMLEditor::HandleDeleteNonCollapsedRanges() failed");
    return result;
  };

  if (aScanFromCaretPointResult.ReachedOtherBlockElement()) {
    if (NS_WARN_IF(!aScanFromCaretPointResult.ContentIsElement())) {
      return Err(NS_ERROR_FAILURE);
    }
    MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
    bool allRangesNotHandled = true;
    auto ret = EditActionResult::IgnoredResult();
    for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
      AutoBlockElementsJoiner joiner(*this);
      if (!joiner.PrepareToDeleteAtOtherBlockBoundary(
              aHTMLEditor, aDirectionAndAmount,
              *aScanFromCaretPointResult.ElementPtr(),
              aWSRunScannerAtCaret.ScanStartRef(), aWSRunScannerAtCaret)) {
        // Even if we cannot join current line and the first/last line of the
        // other block, there may be empty nodes which may be either visible or
        // invisible. We need to delete them for the compatibility.
        const EditorRawDOMPoint atOtherBlockBoundary =
            nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
                ? EditorRawDOMPoint::After(
                      *aScanFromCaretPointResult.ElementPtr())
                : EditorRawDOMPoint(aScanFromCaretPointResult.ElementPtr());
        Result<EditActionResult, nsresult> result =
            MaybeDeleteContentBetweenCaretAndBlockBoundary(
                atOtherBlockBoundary);
        if (result.isErr()) [[unlikely]] {
          NS_WARNING("MaybeDeleteContentBetweenCaretAndBlockBoundary() failed");
          return result.propagateErr();
        }
        ret |= result.inspect();
        continue;
      }
      allRangesNotHandled = false;
      Result<EditActionResult, nsresult> result =
          joiner.Run(aHTMLEditor, aDirectionAndAmount, aStripWrappers,
                     aWSRunScannerAtCaret.ScanStartRef(), MOZ_KnownLive(range),
                     aEditingHost);
      if (MOZ_UNLIKELY(result.isErr())) {
        NS_WARNING(
            "AutoBlockElementsJoiner::Run() failed (other block boundary)");
        return result;
      }
      ret |= result.inspect();
    }
    return allRangesNotHandled ? EditActionResult::CanceledResult()
                               : std::move(ret);
  }

  if (aScanFromCaretPointResult.ReachedCurrentBlockBoundary() ||
      aScanFromCaretPointResult.ReachedInlineEditingHostBoundary()) {
    MOZ_ASSERT(aScanFromCaretPointResult.ContentIsElement());
    MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
    bool allRangesNotHandled = true;
    auto ret = EditActionResult::IgnoredResult();
    for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
      AutoBlockElementsJoiner joiner(*this);
      if (!joiner.PrepareToDeleteAtCurrentBlockBoundary(
              aHTMLEditor, aDirectionAndAmount,
              *aScanFromCaretPointResult.ElementPtr(),
              aWSRunScannerAtCaret.ScanStartRef(), aEditingHost)) {
        // Even if we cannot join current line and the line immediately
        // before/after the current block, there may be empty nodes which may be
        // either visible or invisible. We need to delete them for the
        // compatibility.
        // FYI: This is expected by /custom-elements/reactions/Document.html
        const EditorRawDOMPoint atCurrentBlockBoundary =
            nsIEditor::DirectionIsBackspace(aDirectionAndAmount)
                ? EditorRawDOMPoint(aScanFromCaretPointResult.ElementPtr(), 0u)
                : EditorRawDOMPoint::AtEndOf(
                      *aScanFromCaretPointResult.ElementPtr());
        Result<EditActionResult, nsresult> result =
            MaybeDeleteContentBetweenCaretAndBlockBoundary(
                atCurrentBlockBoundary);
        if (result.isErr()) [[unlikely]] {
          NS_WARNING("MaybeDeleteContentBetweenCaretAndBlockBoundary() failed");
          return result.propagateErr();
        }
        ret |= result.inspect();
        continue;
      }
      allRangesNotHandled = false;
      Result<EditActionResult, nsresult> result =
          joiner.Run(aHTMLEditor, aDirectionAndAmount, aStripWrappers,
                     aWSRunScannerAtCaret.ScanStartRef(), MOZ_KnownLive(range),
                     aEditingHost);
      if (MOZ_UNLIKELY(result.isErr())) {
        NS_WARNING(
            "AutoBlockElementsJoiner::Run() failed (current block boundary)");
        return result;
      }
      ret |= result.inspect();
    }
    return allRangesNotHandled ? EditActionResult::CanceledResult()
                               : std::move(ret);
  }

  MOZ_ASSERT_UNREACHABLE("New type of reached content hasn't been handled yet");
  return EditActionResult::IgnoredResult();
}

nsresult HTMLEditor::AutoDeleteRangesHandler::
    ComputeRangesToDeleteTextAroundCollapsedRanges(
        nsIEditor::EDirection aDirectionAndAmount,
        AutoClonedSelectionRangeArray& aRangesToDelete) const {
  MOZ_ASSERT(aDirectionAndAmount == nsIEditor::eNext ||
             aDirectionAndAmount == nsIEditor::ePrevious);

  const auto caretPosition =
      aRangesToDelete.GetFirstRangeStartPoint<EditorDOMPoint>();
  MOZ_ASSERT(caretPosition.IsSetAndValid());
  if (MOZ_UNLIKELY(NS_WARN_IF(!caretPosition.IsInContentNode()))) {
    return NS_ERROR_FAILURE;
  }

  EditorDOMRangeInTexts rangeToDelete;
  if (aDirectionAndAmount == nsIEditor::eNext) {
    Result<EditorDOMRangeInTexts, nsresult> result =
        WSRunScanner::GetRangeInTextNodesToForwardDeleteFrom(
            {WSRunScanner::Option::OnlyEditableNodes}, caretPosition);
    if (result.isErr()) {
      NS_WARNING(
          "WSRunScanner::GetRangeInTextNodesToForwardDeleteFrom() failed");
      return result.unwrapErr();
    }
    rangeToDelete = result.unwrap();
    if (!rangeToDelete.IsPositioned()) {
      return NS_OK;  // no range to delete, but consume it.
    }
  } else {
    Result<EditorDOMRangeInTexts, nsresult> result =
        WSRunScanner::GetRangeInTextNodesToBackspaceFrom(
            {WSRunScanner::Option::OnlyEditableNodes}, caretPosition);
    if (result.isErr()) {
      NS_WARNING("WSRunScanner::GetRangeInTextNodesToBackspaceFrom() failed");
      return result.unwrapErr();
    }
    rangeToDelete = result.unwrap();
    if (!rangeToDelete.IsPositioned()) {
      return NS_OK;  // no range to delete, but consume it.
    }
  }

  // FIXME: If we'll delete unnecessary following <br>, we need to include it
  // into aRangesToDelete.

  nsresult rv = aRangesToDelete.SetStartAndEnd(rangeToDelete.StartRef(),
                                               rangeToDelete.EndRef());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoArrayRanges::SetStartAndEnd() failed");
  return rv;
}

Result<CaretPoint, nsresult>
HTMLEditor::AutoDeleteRangesHandler::HandleDeleteTextAroundCollapsedRanges(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aDirectionAndAmount == nsIEditor::eNext ||
             aDirectionAndAmount == nsIEditor::ePrevious);

  nsresult rv = ComputeRangesToDeleteTextAroundCollapsedRanges(
      aDirectionAndAmount, aRangesToDelete);
  if (NS_FAILED(rv)) {
    return Err(NS_ERROR_FAILURE);
  }
  if (MOZ_UNLIKELY(aRangesToDelete.IsCollapsed())) {
    return CaretPoint(EditorDOMPoint());  // no range to delete
  }

  // FYI: rangeToDelete does not contain newly empty inline ancestors which
  //      are removed by DeleteTextAndNormalizeSurroundingWhiteSpaces().
  //      So, if `getTargetRanges()` needs to include parent empty elements,
  //      we need to extend the range with
  //      HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement().
  EditorRawDOMRange rangeToDelete(aRangesToDelete.FirstRangeRef());
  // XXX This method assumes that there is no empty inline containers nor
  // comment nodes around the `Text` and the other browsers might delete them
  // too.
  if (MOZ_UNLIKELY(!rangeToDelete.IsInTextNodes())) {
    NS_WARNING("The extended range to delete character was not in text nodes");
    return Err(NS_ERROR_FAILURE);
  }

  // If deleting some characters makes the last line before a block boundary
  // empty, we need to put a line break.
  const bool needsToPutPaddingBRForLastEmptyLine = [&]() {
    if (!rangeToDelete.StartRef().IsStartOfContainer() ||
        !rangeToDelete.EndRef().IsEndOfContainer()) {
      return false;
    }
    const WSScanResult previousThing =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {}, rangeToDelete.StartRef());
    if (!previousThing.ReachedLineBreak()) {
      return false;
    }
    // XXX Is this right? Shouldn't we stop putting line break if there is an
    // ignored line break?
    return HTMLEditUtils::
        ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
               rangeToDelete.EndRef(), TreatAsUnnecessaryLineBreak{},
               aEditingHost)
            .ReachedBlockBoundary();
  }();

  Result<CaretPoint, nsresult> caretPointOrError =
      aHTMLEditor.DeleteTextAndNormalizeSurroundingWhiteSpaces(
          rangeToDelete.StartRef().AsInText(),
          rangeToDelete.EndRef().AsInText(),
          !aEditingHost.IsContentEditablePlainTextOnly()
              ? TreatEmptyTextNodes::RemoveAllEmptyInlineAncestors
              : TreatEmptyTextNodes::Remove,
          aDirectionAndAmount == nsIEditor::eNext ? DeleteDirection::Forward
                                                  : DeleteDirection::Backward,
          aEditingHost);
  if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::DeleteTextAndNormalizeSurroundingWhiteSpaces() failed");
    return caretPointOrError;
  }
  // Remember that we did a ranged delete for the benefit of
  // OnEndHandlingTopLevelEditSubActionInternal().
  aHTMLEditor.TopLevelEditSubActionDataRef().mDidDeleteNonCollapsedRange = true;
  if (!needsToPutPaddingBRForLastEmptyLine) {
    return caretPointOrError;
  }
  const EditorDOMPoint pointToPutLineBreak =
      caretPointOrError.unwrap().UnwrapCaretPoint();
  const Maybe<LineBreakType> lineBreakType =
      aHTMLEditor.GetPreferredLineBreakType(
          *pointToPutLineBreak.ContainerAs<nsIContent>(), aEditingHost);
  if (NS_WARN_IF(lineBreakType.isNothing())) {
    return Err(NS_ERROR_FAILURE);
  }
  Result<CreateLineBreakResult, nsresult> lineBreakOrError =
      aHTMLEditor.InsertLineBreak(WithTransaction::Yes, *lineBreakType,
                                  pointToPutLineBreak, nsIEditor::ePrevious);
  if (MOZ_UNLIKELY(lineBreakOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "nsIEditor::ePrevious) failed");
    return lineBreakOrError.propagateErr();
  }
  return CaretPoint(lineBreakOrError.unwrap().UnwrapCaretPoint());
}

// static
nsIContent* HTMLEditor::AutoDeleteRangesHandler::GetAtomicContentToDelete(
    nsIEditor::EDirection /*aDirectionAndAmount*/,
    const WSRunScanner& /*aWSRunScannerAtCaret*/,
    const WSScanResult& aScanFromCaretPointResult) {
  MOZ_ASSERT(aScanFromCaretPointResult.GetContent());

  if (!aScanFromCaretPointResult.ReachedSpecialContent() &&
      !aScanFromCaretPointResult.ReachedEmptyInlineContainerElement()) {
    return aScanFromCaretPointResult.GetContent();
  }

  if (!aScanFromCaretPointResult.GetContent()->IsText() ||
      HTMLEditUtils::IsRemovableNode(*aScanFromCaretPointResult.GetContent())) {
    return aScanFromCaretPointResult.GetContent();
  }

  // aScanFromCaretPointResult is non-removable text node.
  // Since we try removing atomic content, we look for removable node from
  // scanned point that is non-removable text.
  nsIContent* removableRoot = aScanFromCaretPointResult.GetContent();
  while (removableRoot && !HTMLEditUtils::IsRemovableNode(*removableRoot)) {
    removableRoot = removableRoot->GetParent();
  }

  if (removableRoot) {
    return removableRoot;
  }

  // Not found better content. This content may not be removable.
  return aScanFromCaretPointResult.GetContent();
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::ComputeRangesToDeleteAtomicContent(
    const nsIContent& aAtomicContent,
    AutoClonedSelectionRangeArray& aRangesToDelete) const {
  EditorDOMRange rangeToDelete =
      WSRunScanner::GetRangesForDeletingAtomicContent(
          {WSRunScanner::Option::OnlyEditableNodes}, aAtomicContent);
  if (!rangeToDelete.IsPositioned()) {
    NS_WARNING("WSRunScanner::GetRangeForDeleteAContentNode() failed");
    return NS_ERROR_FAILURE;
  }

  // FIXME: If we'll delete unnecessary following <br>, we need to include it
  // into aRangesToDelete.

  nsresult rv = aRangesToDelete.SetStartAndEnd(rangeToDelete.StartRef(),
                                               rangeToDelete.EndRef());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedRangeArray::SetStartAndEnd() failed");
  return rv;
}

Result<CaretPoint, nsresult>
HTMLEditor::AutoDeleteRangesHandler::HandleDeleteAtomicContent(
    HTMLEditor& aHTMLEditor, nsIContent& aAtomicContent,
    const EditorDOMPoint& aCaretPoint,
    const WSRunScanner& /*aWSRunScannerAtCaret*/, const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(
      !HTMLEditUtils::IsBRElementFollowedByBlockBoundary(aAtomicContent));
  MOZ_ASSERT(!aAtomicContent.IsEditingHost());

  EditorDOMPoint pointToPutCaret = aCaretPoint;
  {
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    Result<CaretPoint, nsresult> caretPointOrError =
        WhiteSpaceVisibilityKeeper::DeleteContentNodeAndJoinTextNodesAroundIt(
            aHTMLEditor, aAtomicContent, aCaretPoint, aEditingHost);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::"
          "DeleteContentNodeAndJoinTextNodesAroundIt() failed");
      return caretPointOrError;
    }
    trackPointToPutCaret.Flush(StopTracking::Yes);
    caretPointOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, aHTMLEditor,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  if (MOZ_LIKELY(pointToPutCaret.IsInContentNode())) {
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    nsresult rv = aHTMLEditor.EnsureNoFollowingUnnecessaryLineBreak(
        pointToPutCaret, PreservePreformattedLineBreak::No,
        PaddingForEmptyBlock::Significant, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
  }
  if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if ((aHTMLEditor.IsMailEditor() || aHTMLEditor.IsPlaintextMailComposer()) &&
      MOZ_LIKELY(pointToPutCaret.IsInContentNode())) {
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    nsresult rv = aHTMLEditor.DeleteMostAncestorMailCiteElementIfEmpty(
        MOZ_KnownLive(*pointToPutCaret.ContainerAs<nsIContent>()));
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty() failed");
      return Err(rv);
    }
    trackPointToPutCaret.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!pointToPutCaret.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  if (aHTMLEditor.GetTopLevelEditSubAction() ==
      EditSubAction::eDeleteSelectedContent) {
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
        aHTMLEditor.InsertPaddingBRElementIfNeeded(
            pointToPutCaret,
            aEditingHost.IsContentEditablePlainTextOnly() ? nsIEditor::eNoStrip
                                                          : nsIEditor::eStrip,
            aEditingHost);
    if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
      NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
      return insertPaddingBRElementOrError.propagateErr();
    }
    trackPointToPutCaret.Flush(StopTracking::Yes);
    if (!pointToPutCaret.IsInTextNode()) {
      insertPaddingBRElementOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion});
      if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    } else {
      insertPaddingBRElementOrError.unwrap().IgnoreCaretPointSuggestion();
      if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
  }
  return CaretPoint(std::move(pointToPutCaret));
}

// static
Result<bool, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    ExtendRangeToContainAncestorInlineElementsAtStart(
        dom::Range& aRangeToDelete, const Element& aEditingHost) {
  MOZ_ASSERT(aRangeToDelete.IsPositioned());
  MOZ_ASSERT(aRangeToDelete.GetCommonAncestorContainer(IgnoreErrors()));
  MOZ_ASSERT(aRangeToDelete.GetCommonAncestorContainer(IgnoreErrors())
                 ->IsInclusiveDescendantOf(&aEditingHost));

  EditorRawDOMPoint startPoint(aRangeToDelete.StartRef());
  if (startPoint.IsInTextNode()) {
    if (!startPoint.IsStartOfContainer()) {
      // FIXME: If before the point has only collapsible white-spaces and the
      // text node follows a block boundary, we should treat the range start
      // from start of the text node.
      return true;
    }
    startPoint.Set(startPoint.ContainerAs<Text>());
    if (NS_WARN_IF(!startPoint.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (startPoint.GetContainer() == &aEditingHost) {
      return false;
    }
  } else if (startPoint.IsInDataNode()) {
    startPoint.Set(startPoint.ContainerAs<nsIContent>());
    if (NS_WARN_IF(!startPoint.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (startPoint.GetContainer() == &aEditingHost) {
      return false;
    }
  } else if (startPoint.GetContainer() == &aEditingHost) {
    return false;
  }

  // FYI: This method is designed for deleting inline elements which become
  // empty if aRangeToDelete which crosses a block boundary of right block
  // child.  Therefore, you may need to improve this method if you want to use
  // this in the other cases.

  nsINode* const commonAncestor =
      nsContentUtils::GetClosestCommonInclusiveAncestor(
          startPoint.GetContainer(), aRangeToDelete.GetEndContainer());
  if (NS_WARN_IF(!commonAncestor)) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(commonAncestor->IsInclusiveDescendantOf(&aEditingHost));

  EditorRawDOMPoint newStartPoint(startPoint);
  while (newStartPoint.GetContainer() != &aEditingHost &&
         newStartPoint.GetContainer() != commonAncestor) {
    if (NS_WARN_IF(!newStartPoint.IsInContentNode())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (!HTMLEditUtils::IsInlineContent(
            *newStartPoint.ContainerAs<nsIContent>(),
            BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      break;
    }
    // The container is inline, check whether the point is first visible point
    // or not to consider whether climbing up the tree.
    bool foundVisiblePrevSibling = false;
    for (nsIContent* content = newStartPoint.GetPreviousSiblingOfChild();
         content; content = content->GetPreviousSibling()) {
      if (Text* text = Text::FromNode(content)) {
        if (HTMLEditUtils::IsVisibleTextNode(
                *text, TreatInvisibleLineBreakAs::Invisible)) {
          foundVisiblePrevSibling = true;
          break;
        }
        // The text node is invisible.
      } else if (content->IsComment()) {
        // Ignore the comment node.
      } else if (!HTMLEditUtils::IsInlineContent(
                     *content,
                     BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
                 !HTMLEditUtils::IsEmptyNode(
                     *content,
                     {EmptyCheckOption::TreatSingleBRElementAsVisible})) {
        foundVisiblePrevSibling = true;
        break;
      }
    }
    if (foundVisiblePrevSibling) {
      break;
    }
    // the point can be treated as start of the parent inline now.
    newStartPoint.Set(newStartPoint.ContainerAs<nsIContent>());
    if (NS_WARN_IF(!newStartPoint.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
  }
  if (newStartPoint == startPoint) {
    return false;  // Don't need to modify the range
  }
  IgnoredErrorResult error;
  aRangeToDelete.SetStart(newStartPoint.ToRawRangeBoundary(), error);
  if (MOZ_UNLIKELY(error.Failed())) {
    return Err(NS_ERROR_FAILURE);
  }
  return true;
}

bool HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    PrepareToDeleteAtOtherBlockBoundary(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount, Element& aOtherBlockElement,
        const EditorDOMPoint& aCaretPoint,
        const WSRunScanner& aWSRunScannerAtCaret) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aCaretPoint.IsSetAndValid());

  mMode = Mode::JoinOtherBlock;

  // Make sure it's not a table element.  If so, cancel the operation
  // (translation: users cannot backspace or delete across table cells)
  if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(aOtherBlockElement)) {
    return false;
  }

  mOtherBlockElement = &aOtherBlockElement;
  // First find the adjacent node in the block
  mLeafContentInOtherBlock =
      ComputeLeafContentInOtherBlockElement(aDirectionAndAmount);
  if (aDirectionAndAmount == nsIEditor::ePrevious) {
    mLeftContent = mLeafContentInOtherBlock;
    mRightContent = aCaretPoint.GetContainerAs<nsIContent>();
  } else {
    mLeftContent = aCaretPoint.GetContainerAs<nsIContent>();
    mRightContent = mLeafContentInOtherBlock;
  }

  // Next to a block.  See if we are between the block and a `<br>`.
  // If so, we really want to delete the `<br>`.  Else join content at
  // selection to the block.
  const WSScanResult scanFromCaretResult =
      aDirectionAndAmount == nsIEditor::eNext
          ? aWSRunScannerAtCaret.ScanPreviousVisibleNodeOrBlockBoundaryFrom(
                aCaretPoint)
          : aWSRunScannerAtCaret
                .ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(aCaretPoint);
  // If we found a `<br>` element, we need to delete it instead of joining the
  // contents.
  if (scanFromCaretResult.ReachedBRElement()) {
    mBRElement = scanFromCaretResult.BRElementPtr();
    mMode = Mode::DeleteBRElement;
    return true;
  }

  return mLeftContent && mRightContent;
}
nsIContent* HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeLeafContentInOtherBlockElement(
        nsIEditor::EDirection aDirectionAndAmount) const {
  MOZ_ASSERT(mOtherBlockElement);
  return aDirectionAndAmount == nsIEditor::ePrevious
             ? HTMLEditUtils::GetLastLeafContent(
                   *mOtherBlockElement, {LeafNodeOption::IgnoreNonEditableNode})
             : HTMLEditUtils::GetFirstLeafContent(
                   *mOtherBlockElement,
                   {LeafNodeOption::IgnoreNonEditableNode});
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteLineBreak(const HTMLEditor& aHTMLEditor,
                                  dom::Range& aRangeToDelete,
                                  const Element& aEditingHost,
                                  ComputeRangeFor aComputeRangeFor) const {
  // FIXME: Scan invisible leading white-spaces after the <br>.
  MOZ_ASSERT_IF(mMode == Mode::DeleteBRElement, mBRElement);
  MOZ_ASSERT_IF(mMode == Mode::DeletePrecedingBRElementOfBlock, mBRElement);
  MOZ_ASSERT_IF(mMode == Mode::DeletePrecedingPreformattedLineBreak,
                mPreformattedLineBreak.IsSetAndValid());
  MOZ_ASSERT_IF(mMode == Mode::DeletePrecedingPreformattedLineBreak,
                mPreformattedLineBreak.IsCharPreformattedNewLine());
  MOZ_ASSERT_IF(aComputeRangeFor == ComputeRangeFor::GetTargetRanges,
                aRangeToDelete.IsPositioned());

  // If we're computing for beforeinput.getTargetRanges() and the inputType
  // is not a simple deletion like replacing selected content with new
  // content, the range should end at the original end boundary of the given
  // range.
  const bool preserveEndBoundary =
      (mMode == Mode::DeletePrecedingBRElementOfBlock ||
       mMode == Mode::DeletePrecedingPreformattedLineBreak) &&
      aComputeRangeFor == ComputeRangeFor::GetTargetRanges &&
      !MayEditActionDeleteAroundCollapsedSelection(aHTMLEditor.GetEditAction());

  if (mMode != Mode::DeletePrecedingPreformattedLineBreak) {
    Element* const mostDistantInlineAncestor =
        HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
            *mBRElement, BlockInlineCheck::UseComputedDisplayOutsideStyle,
            &aEditingHost);
    if (preserveEndBoundary) {
      // FIXME: If the range ends at end of an inline element, we may need to
      // extend the range.
      IgnoredErrorResult error;
      aRangeToDelete.SetStart(EditorRawDOMPoint(mostDistantInlineAncestor
                                                    ? mostDistantInlineAncestor
                                                    : mBRElement)
                                  .ToRawRangeBoundary(),
                              error);
      NS_WARNING_ASSERTION(!error.Failed(), "Range::SetStart() failed");
      MOZ_ASSERT_IF(!error.Failed(), !aRangeToDelete.Collapsed());
      return error.StealNSResult();
    }
    IgnoredErrorResult error;
    aRangeToDelete.SelectNode(
        mostDistantInlineAncestor ? *mostDistantInlineAncestor : *mBRElement,
        error);
    NS_WARNING_ASSERTION(!error.Failed(), "Range::SelectNode() failed");
    return error.StealNSResult();
  }

  Element* const mostDistantInlineAncestor =
      mPreformattedLineBreak.ContainerAs<Text>()->TextDataLength() == 1
          ? HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
                *mPreformattedLineBreak.ContainerAs<Text>(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle, &aEditingHost)
          : nullptr;

  if (!mostDistantInlineAncestor) {
    if (preserveEndBoundary) {
      // FIXME: If the range ends at end of an inline element, we may need to
      // extend the range.
      IgnoredErrorResult error;
      aRangeToDelete.SetStart(mPreformattedLineBreak.ToRawRangeBoundary(),
                              error);
      MOZ_ASSERT_IF(!error.Failed(), !aRangeToDelete.Collapsed());
      NS_WARNING_ASSERTION(!error.Failed(), "Range::SetStart() failed");
      return error.StealNSResult();
    }
    nsresult rv = aRangeToDelete.SetStartAndEnd(
        mPreformattedLineBreak.ToRawRangeBoundary(),
        mPreformattedLineBreak.NextPoint().ToRawRangeBoundary());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Range::SetStartAndEnd() failed");
    return rv;
  }

  if (preserveEndBoundary) {
    // FIXME: If the range ends at end of an inline element, we may need to
    // extend the range.
    IgnoredErrorResult error;
    aRangeToDelete.SetStart(
        EditorRawDOMPoint(mostDistantInlineAncestor).ToRawRangeBoundary(),
        error);
    MOZ_ASSERT_IF(!error.Failed(), !aRangeToDelete.Collapsed());
    NS_WARNING_ASSERTION(!error.Failed(), "Range::SetStart() failed");
    return error.StealNSResult();
  }

  IgnoredErrorResult error;
  aRangeToDelete.SelectNode(*mostDistantInlineAncestor, error);
  NS_WARNING_ASSERTION(!error.Failed(), "Range::SelectNode() failed");
  return error.StealNSResult();
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::HandleDeleteLineBreak(
        HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
        const EditorDOMPoint& aCaretPoint, const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(mBRElement || mPreformattedLineBreak.IsSet());

  // If we're deleting selection (not replacing with new content), we should
  // put caret to end of preceding text node if there is.  Then, users can type
  // text in it like the other browsers.
  EditorDOMPoint pointToPutCaret = [&]() {
    // but when we're deleting a preceding line break of current block, we
    // should keep the caret position in the current block.
    if (mMode == Mode::DeletePrecedingBRElementOfBlock ||
        mMode == Mode::DeletePrecedingPreformattedLineBreak) {
      return aCaretPoint;
    }
    if (!MayEditActionDeleteAroundCollapsedSelection(
            aHTMLEditor.GetEditAction())) {
      return EditorDOMPoint();
    }
    const WSRunScanner scanner({WSRunScanner::Option::OnlyEditableNodes},
                               EditorRawDOMPoint(mBRElement));
    const WSScanResult maybePreviousText =
        scanner.ScanPreviousVisibleNodeOrBlockBoundaryFrom(
            EditorRawDOMPoint(mBRElement));
    if (maybePreviousText.ContentIsEditable() &&
        maybePreviousText.InVisibleOrCollapsibleCharacters() &&
        !HTMLEditor::GetLinkElement(maybePreviousText.TextPtr())) {
      return maybePreviousText.PointAfterReachedContent<EditorDOMPoint>();
    }
    const WSScanResult maybeNextText =
        scanner.ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
            EditorRawDOMPoint::After(*mBRElement));
    if (maybeNextText.ContentIsEditable() &&
        maybeNextText.InVisibleOrCollapsibleCharacters()) {
      return maybeNextText.PointAtReachedContent<EditorDOMPoint>();
    }
    return EditorDOMPoint();
  }();

  RefPtr<dom::Range> rangeToDelete =
      dom::Range::Create(const_cast<Element*>(&aEditingHost));
  MOZ_ASSERT(rangeToDelete);
  nsresult rv =
      ComputeRangeToDeleteLineBreak(aHTMLEditor, *rangeToDelete, aEditingHost,
                                    ComputeRangeFor::ToDeleteTheRange);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "AutoBlockElementsJoiner::ComputeRangeToDeleteLineBreak() failed");
    return Err(rv);
  }
  Result<EditActionResult, nsresult> result = HandleDeleteNonCollapsedRange(
      aHTMLEditor, aDirectionAndAmount, nsIEditor::eNoStrip, *rangeToDelete,
      SelectionWasCollapsed::Yes, aEditingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING(
        "AutoBlockElementsJoiner::HandleDeleteNonCollapsedRange() failed");
    return result;
  }

  if (mLeftContent && mRightContent &&
      HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mLeftContent) !=
          HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mRightContent)) {
    return EditActionResult::HandledResult();
  }

  // Put selection at edge of block and we are done.
  if (NS_WARN_IF(mMode == Mode::DeleteBRElement && !mLeafContentInOtherBlock)) {
    // XXX This must be odd case.  The other block can be empty.
    return Err(NS_ERROR_FAILURE);
  }

  if ((mMode == Mode::DeletePrecedingBRElementOfBlock ||
       mMode == Mode::DeletePrecedingPreformattedLineBreak) &&
      pointToPutCaret.IsSetAndValidInComposedDoc()) {
    // If we're deleting only the preceding lines of a block, we should
    // normalize the white-spaces at start of the block for compatibility
    // with the other browsers.
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    Result<EditorDOMPoint, nsresult> atFirstVisibleThingOrError =
        WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesAfter(
            aHTMLEditor, pointToPutCaret, {});
    if (MOZ_UNLIKELY(atFirstVisibleThingOrError.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesAfter() failed");
      return atFirstVisibleThingOrError.propagateErr();
    }
    trackPointToPutCaret.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!pointToPutCaret.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  if (pointToPutCaret.IsSet()) {
    nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (mMode == Mode::DeleteBRElement && NS_SUCCEEDED(rv)) {
      // If we prefer to use style in the previous line, we should forget
      // previous styles since the caret position has all styles which we want
      // to use with new content.
      if (nsIEditor::DirectionIsBackspace(aDirectionAndAmount)) {
        aHTMLEditor.TopLevelEditSubActionDataRef()
            .mCachedPendingStyles->Clear();
      }
      // And we don't want to keep extending a link at ex-end of the previous
      // paragraph.
      if (HTMLEditor::GetLinkElement(pointToPutCaret.GetContainer())) {
        aHTMLEditor.mPendingStylesToApplyToNewContent
            ->ClearLinkAndItsSpecifiedStyle();
      }
    } else {
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }
    return EditActionResult::HandledResult();
  }

  EditorDOMPoint newCaretPosition =
      HTMLEditUtils::GetGoodCaretPointFor<EditorDOMPoint>(
          *mLeafContentInOtherBlock, aDirectionAndAmount);
  if (MOZ_UNLIKELY(!newCaretPosition.IsInContentNode())) {
    NS_WARNING("HTMLEditUtils::GetGoodCaretPointFor() failed");
    return Err(NS_ERROR_FAILURE);
  }
  // If we're deleting only a line break and move caret to left block, we
  // want to normalize the white-spaces at end of the left block for the
  // compatibility with the other browsers.
  const WSScanResult nextThingOfCaretPoint =
      HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
          newCaretPosition, TreatAsUnnecessaryLineBreak{}, aEditingHost);
  if (nextThingOfCaretPoint.ReachedBlockBoundary()) {
    AutoTrackDOMPoint trackNewCaretPosition(aHTMLEditor.RangeUpdaterRef(),
                                            &newCaretPosition);
    const EditorDOMPoint atBlockBoundary =
        nextThingOfCaretPoint
            .PointAtReachedBlockBoundaryOrEditingHostBoundary<EditorDOMPoint>();
    Result<EditorDOMPoint, nsresult> afterLastVisibleThingOrError =
        WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesBefore(
            aHTMLEditor, atBlockBoundary, {});
    if (MOZ_UNLIKELY(afterLastVisibleThingOrError.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesBefore() "
          "failed");
      return afterLastVisibleThingOrError.propagateErr();
    }
    trackNewCaretPosition.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!newCaretPosition.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }
  rv = aHTMLEditor.CollapseSelectionTo(newCaretPosition);
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed, but ignored");
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteAtOtherBlockBoundary(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount,
        const EditorDOMPoint& aCaretPoint, dom::Range& aRangeToDelete,
        const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aCaretPoint.IsSetAndValid());
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mRightContent);

  if (HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mLeftContent) !=
      HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mRightContent)) {
    if (!mDeleteRangesHandlerConst.CanFallbackToDeleteRangeWithTransaction(
            aRangeToDelete)) {
      nsresult rv = aRangeToDelete.CollapseTo(aCaretPoint.ToRawRangeBoundary());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Range::CollapseTo() failed");
      return rv;
    }
    nsresult rv = mDeleteRangesHandlerConst
                      .FallbackToComputeRangeToDeleteRangeWithTransaction(
                          aHTMLEditor, aRangeToDelete, aEditingHost);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoDeleteRangesHandler::"
        "FallbackToComputeRangeToDeleteRangeWithTransaction() failed");
    return rv;
  }

  AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                  *mRightContent);
  Result<bool, nsresult> canJoinThem =
      joiner.Prepare(aHTMLEditor, aEditingHost);
  if (canJoinThem.isErr()) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
    return canJoinThem.unwrapErr();
  }
  if (canJoinThem.inspect() && joiner.CanJoinBlocks() &&
      !joiner.ShouldDeleteLeafContentInstead()) {
    nsresult rv = joiner.ComputeRangeToDelete(aHTMLEditor, aCaretPoint,
                                              aRangeToDelete, aEditingHost);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoInclusiveAncestorBlockElementsJoiner::"
                         "ComputeRangeToDelete() failed");
    return rv;
  }

  // If AutoInclusiveAncestorBlockElementsJoiner didn't handle it and it's not
  // canceled, user may want to modify the start leaf node or the last leaf
  // node of the block.
  if (mLeafContentInOtherBlock == aCaretPoint.GetContainer()) {
    return NS_OK;
  }

  // If it's ignored, it didn't modify the DOM tree.  In this case, user must
  // want to delete nearest leaf node in the other block element.
  // TODO: We need to consider this before calling ComputeRangesToDelete() for
  //       computing the deleting range.
  EditorRawDOMPoint newCaretPoint =
      aDirectionAndAmount == nsIEditor::ePrevious
          ? EditorRawDOMPoint::AtEndOf(*mLeafContentInOtherBlock)
          : EditorRawDOMPoint(mLeafContentInOtherBlock, 0);
  // If new caret position is same as current caret position, we can do
  // nothing anymore.
  if (aRangeToDelete.Collapsed() &&
      aRangeToDelete.EndRef() == newCaretPoint.ToRawRangeBoundary()) {
    return NS_OK;
  }
  AutoClonedSelectionRangeArray rangeArray(
      newCaretPoint, aHTMLEditor.SelectionLimitersAndCaretData());
  if (!rangeArray.GetAncestorLimiter()) {
    rangeArray.SetAncestorLimiter(aHTMLEditor.FindSelectionRoot(aEditingHost));
  }
  AutoDeleteRangesHandler anotherHandler(mDeleteRangesHandlerConst);
  nsresult rv = anotherHandler.ComputeRangesToDelete(
      aHTMLEditor, aDirectionAndAmount, rangeArray, aEditingHost);
  if (NS_SUCCEEDED(rv)) {
    if (MOZ_LIKELY(!rangeArray.Ranges().IsEmpty())) {
      MOZ_ASSERT(rangeArray.Ranges().Length() == 1);
      aRangeToDelete.SetStartAndEnd(rangeArray.FirstRangeRef()->StartRef(),
                                    rangeArray.FirstRangeRef()->EndRef());
    } else {
      NS_WARNING(
          "Recursive AutoDeleteRangesHandler::ComputeRangesToDelete() "
          "returned no range");
      rv = NS_ERROR_FAILURE;
    }
  } else {
    NS_WARNING(
        "Recursive AutoDeleteRangesHandler::ComputeRangesToDelete() failed");
  }
  return NS_SUCCEEDED(rv) ? NS_OK : NS_ERROR_FAILURE;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::HandleDeleteAtOtherBlockBoundary(
        HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
        nsIEditor::EStripWrappers aStripWrappers,
        const EditorDOMPoint& aCaretPoint, dom::Range& aRangeToDelete,
        const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aCaretPoint.IsSetAndValid());
  MOZ_ASSERT(mDeleteRangesHandler);
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mRightContent);

  if (HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mLeftContent) !=
      HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mRightContent)) {
    // If we have not deleted `<br>` element and are not called recursively,
    // we should call `DeleteRangesWithTransaction()` here.
    if (!mDeleteRangesHandler->CanFallbackToDeleteRangeWithTransaction(
            aRangeToDelete)) {
      return EditActionResult::IgnoredResult();
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        mDeleteRangesHandler->FallbackToDeleteRangeWithTransaction(
            aHTMLEditor, aRangeToDelete);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING(
          "AutoDeleteRangesHandler::FallbackToDeleteRangesWithTransaction() "
          "failed");
      return caretPointOrError.propagateErr();
    }
    nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
        aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                      SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                      SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "CaretPoint::SuggestCaretPointTo() failed, but ignored");
    // Don't return "ignored" to avoid to fall it back to delete ranges
    // recursively.
    return EditActionResult::HandledResult();
  }

  // Else we are joining content to block
  AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                  *mRightContent);
  Result<bool, nsresult> canJoinThem =
      joiner.Prepare(aHTMLEditor, aEditingHost);
  if (MOZ_UNLIKELY(canJoinThem.isErr())) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
    return canJoinThem.propagateErr();
  }

  if (!canJoinThem.inspect() || !joiner.CanJoinBlocks()) {
    nsresult rv = aHTMLEditor.CollapseSelectionTo(aCaretPoint);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
    return !canJoinThem.inspect() ? EditActionResult::CanceledResult()
                                  : EditActionResult::IgnoredResult();
  }

  EditorDOMPoint pointToPutCaret(aCaretPoint);
  AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                         &pointToPutCaret);
  Result<DeleteRangeResult, nsresult> moveFirstLineResult =
      joiner.Run(aHTMLEditor, aEditingHost);
  if (MOZ_UNLIKELY(moveFirstLineResult.isErr())) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Run() failed");
    return moveFirstLineResult.propagateErr();
  }
  DeleteRangeResult unwrappedMoveFirstLineResult = moveFirstLineResult.unwrap();
#ifdef DEBUG
  if (joiner.ShouldDeleteLeafContentInstead()) {
    NS_ASSERTION(unwrappedMoveFirstLineResult.Ignored(),
                 "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                 "returning ignored, but returned not ignored");
  } else {
    NS_ASSERTION(!unwrappedMoveFirstLineResult.Ignored(),
                 "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                 "returning handled, but returned ignored");
  }
#endif  // #ifdef DEBUG
  // Even if AutoInclusiveAncestorBlockElementsJoiner claims "ignored",
  // invisible white-spaces may have been normalized.  So,
  // mLeafContentInOtherBlock could've been removed from the DOM.  Therefore, we
  // need to update mLeafContentInOtherBlock.
  if (mLeafContentInOtherBlock &&
      !mLeafContentInOtherBlock->IsInComposedDoc()) {
    mLeafContentInOtherBlock =
        ComputeLeafContentInOtherBlockElement(aDirectionAndAmount);
  }
  // If we're deleting selection (not replacing with new content) and
  // AutoInclusiveAncestorBlockElementsJoiner computed new caret position,
  // we should use it.  Otherwise, we should keep the our traditional behavior.
  if (unwrappedMoveFirstLineResult.Handled() &&
      unwrappedMoveFirstLineResult.HasCaretPointSuggestion() &&
      MayEditActionDeleteAroundCollapsedSelection(
          aHTMLEditor.GetEditAction())) {
    EditorDOMPoint pointToPutCaret =
        unwrappedMoveFirstLineResult.UnwrapCaretPoint();
    nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed, but ignored");
      return EditActionResult::HandledResult();
    }
    // If we prefer to use style in the previous line, we should forget
    // previous styles since the caret position has all styles which we want
    // to use with new content.
    if (nsIEditor::DirectionIsBackspace(aDirectionAndAmount)) {
      aHTMLEditor.TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
    }
    // And we don't want to keep extending a link at ex-end of the previous
    // paragraph.
    if (HTMLEditor::GetLinkElement(pointToPutCaret.GetContainer())) {
      aHTMLEditor.mPendingStylesToApplyToNewContent
          ->ClearLinkAndItsSpecifiedStyle();
    }
    return EditActionResult::HandledResult();
  }
  trackPointToPutCaret.Flush(StopTracking::Yes);
  unwrappedMoveFirstLineResult.IgnoreCaretPointSuggestion();

  // If AutoInclusiveAncestorBlockElementsJoiner didn't handle it and it's not
  // canceled, user may want to modify the start leaf node or the last leaf
  // node of the block.
  if (unwrappedMoveFirstLineResult.Ignored() && mLeafContentInOtherBlock &&
      mLeafContentInOtherBlock != aCaretPoint.GetContainer()) {
    // If it's ignored, it didn't modify the DOM tree.  In this case, user
    // must want to delete nearest leaf node in the other block element.
    // TODO: We need to consider this before calling Run() for computing the
    //       deleting range.
    EditorRawDOMPoint newCaretPoint =
        aDirectionAndAmount == nsIEditor::ePrevious
            ? EditorRawDOMPoint::AtEndOf(*mLeafContentInOtherBlock)
            : EditorRawDOMPoint(mLeafContentInOtherBlock, 0);
    // If new caret position is same as current caret position, we can do
    // nothing anymore.
    if (aRangeToDelete.Collapsed() &&
        aRangeToDelete.EndRef() == newCaretPoint.ToRawRangeBoundary()) {
      return EditActionResult::CanceledResult();
    }
    nsresult rv = aHTMLEditor.CollapseSelectionTo(newCaretPoint);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      return Err(rv);
    }
    AutoClonedSelectionRangeArray rangesToDelete(aHTMLEditor.SelectionRef());
    if (!rangesToDelete.GetAncestorLimiter()) {
      rangesToDelete.SetAncestorLimiter(
          aHTMLEditor.FindSelectionRoot(aEditingHost));
    }
    AutoDeleteRangesHandler anotherHandler(mDeleteRangesHandler);
    Result<EditActionResult, nsresult> fallbackResult =
        anotherHandler.Run(aHTMLEditor, aDirectionAndAmount, aStripWrappers,
                           rangesToDelete, aEditingHost);
    if (MOZ_UNLIKELY(fallbackResult.isErr())) {
      NS_WARNING("Recursive AutoDeleteRangesHandler::Run() failed");
      return fallbackResult;
    }
    return fallbackResult;
  }
  // Otherwise, we must have deleted the selection as user expected.
  nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed, but ignored");
  return unwrappedMoveFirstLineResult.Handled()
             ? EditActionResult::HandledResult()
             : EditActionResult::IgnoredResult();
}

bool HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    PrepareToDeleteAtCurrentBlockBoundary(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount,
        Element& aCurrentBlockElement, const EditorDOMPoint& aCaretPoint,
        const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  // At edge of our block.  Look beside it and see if we can join to an
  // adjacent block
  mMode = Mode::JoinCurrentBlock;

  // Don't break the basic structure of the HTML document.
  if (aCurrentBlockElement.IsAnyOfHTMLElements(nsGkAtoms::html, nsGkAtoms::head,
                                               nsGkAtoms::body)) {
    return false;
  }

  // Make sure it's not a table element.  If so, cancel the operation
  // (translation: users cannot backspace or delete across table cells)
  if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(
          aCurrentBlockElement)) {
    return false;
  }

  auto ScanJoinTarget = [&]() MOZ_NEVER_INLINE_DEBUG -> nsIContent* {
    nsIContent* targetContent =
        aDirectionAndAmount == nsIEditor::ePrevious
            ? HTMLEditUtils::GetPreviousLeafContent(
                  aCurrentBlockElement, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::Auto, &aEditingHost)
            : HTMLEditUtils::GetNextLeafContent(
                  aCurrentBlockElement, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::Auto, &aEditingHost);
    // If found content is an invisible text node, let's scan visible things.
    auto IsIgnorableDataNode = [](nsIContent* aContent) {
      return aContent && HTMLEditUtils::IsRemovableNode(*aContent) &&
             ((aContent->IsText() &&
               aContent->AsText()->TextIsOnlyWhitespace() &&
               !HTMLEditUtils::IsVisibleTextNode(
                   *aContent->AsText(), TreatInvisibleLineBreakAs::Visible)) ||
              (aContent->IsCharacterData() && !aContent->IsText()));
    };
    if (!IsIgnorableDataNode(targetContent)) {
      return targetContent;
    }
    MOZ_ASSERT(mSkippedInvisibleContents.IsEmpty());
    for (nsIContent* adjacentContent =
             aDirectionAndAmount == nsIEditor::ePrevious
                 ? HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
                       *targetContent,
                       {LeafNodeOption::TreatChildBlockAsLeafNode},
                       BlockInlineCheck::UseComputedDisplayOutsideStyle,
                       &aEditingHost)
                 : HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                       *targetContent,
                       {LeafNodeOption::TreatChildBlockAsLeafNode},
                       BlockInlineCheck::UseComputedDisplayOutsideStyle,
                       &aEditingHost);
         adjacentContent;
         adjacentContent =
             aDirectionAndAmount == nsIEditor::ePrevious
                 ? HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
                       *adjacentContent,
                       {LeafNodeOption::TreatChildBlockAsLeafNode},
                       BlockInlineCheck::UseComputedDisplayOutsideStyle,
                       &aEditingHost)
                 : HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                       *adjacentContent,
                       {LeafNodeOption::TreatChildBlockAsLeafNode},
                       BlockInlineCheck::UseComputedDisplayOutsideStyle,
                       &aEditingHost)) {
      // If non-editable element is found, we should not skip it to avoid
      // joining too far nodes.
      if (!HTMLEditUtils::IsSimplyEditableNode(*adjacentContent)) {
        break;
      }
      // If block element is found, we should join last leaf content in it.
      if (HTMLEditUtils::IsBlockElement(
              *adjacentContent,
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
        nsIContent* leafContent =
            aDirectionAndAmount == nsIEditor::ePrevious
                ? HTMLEditUtils::GetLastLeafContent(
                      *adjacentContent, {LeafNodeOption::IgnoreNonEditableNode})
                : HTMLEditUtils::GetFirstLeafContent(
                      *adjacentContent,
                      {LeafNodeOption::IgnoreNonEditableNode});
        mSkippedInvisibleContents.AppendElement(*targetContent);
        return leafContent ? leafContent : adjacentContent;
      }
      // Only when the found node is an invisible text node or a non-text data
      // node, we should keep scanning.
      if (IsIgnorableDataNode(adjacentContent)) {
        mSkippedInvisibleContents.AppendElement(*targetContent);
        targetContent = adjacentContent;
        continue;
      }
      // Otherwise, we find a visible things. We should join with last found
      // invisible text node.
      break;
    }
    return targetContent;
  };

  if (aDirectionAndAmount == nsIEditor::ePrevious) {
    const WSScanResult prevVisibleThing = [&]() {
      // When Backspace at start of a block, we need to delete only a preceding
      // <br> element if there is.
      const Result<Element*, nsresult>
          inclusiveAncestorOfRightChildBlockOrError = AutoBlockElementsJoiner::
              GetMostDistantBlockAncestorIfPointIsStartAtBlock(aCaretPoint,
                                                               aEditingHost);
      if (NS_WARN_IF(inclusiveAncestorOfRightChildBlockOrError.isErr()) ||
          !inclusiveAncestorOfRightChildBlockOrError.inspect()) {
        return WSScanResult::Error();
      }
      const WSScanResult prevVisibleThingBeforeCurrentBlock =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
              {WSRunScanner::Option::OnlyEditableNodes},
              EditorRawDOMPoint(
                  inclusiveAncestorOfRightChildBlockOrError.inspect()));
      if (!prevVisibleThingBeforeCurrentBlock.ReachedBRElement() &&
          !prevVisibleThingBeforeCurrentBlock.ReachedPreformattedLineBreak()) {
        return WSScanResult::Error();
      }
      // There is a preceding line break, but it may be invisible.  Then, users
      // want to delete its preceding content not only the line break.
      // Therefore, let's check whether the line break follows another line
      // break or a block boundary. In these cases, the line break causes an
      // empty line which users may want to delete.
      const auto atPrecedingLineBreak =
          prevVisibleThingBeforeCurrentBlock
              .PointAtReachedContent<EditorRawDOMPoint>();
      MOZ_ASSERT(atPrecedingLineBreak.IsSet());
      const WSScanResult prevVisibleThingBeforeLineBreak =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
              {WSRunScanner::Option::OnlyEditableNodes}, atPrecedingLineBreak);
      if (prevVisibleThingBeforeLineBreak.ReachedBRElement() ||
          prevVisibleThingBeforeLineBreak.ReachedPreformattedLineBreak() ||
          prevVisibleThingBeforeLineBreak.ReachedCurrentBlockBoundary()) {
        // Target the latter line break for things simpler.  It's easier to
        // compute the target range.
        MOZ_ASSERT_IF(
            prevVisibleThingBeforeCurrentBlock.ReachedPreformattedLineBreak() &&
                prevVisibleThingBeforeLineBreak.ReachedPreformattedLineBreak(),
            prevVisibleThingBeforeCurrentBlock
                    .PointAtReachedContent<EditorRawDOMPoint>() !=
                prevVisibleThingBeforeLineBreak
                    .PointAtReachedContent<EditorRawDOMPoint>());
        return prevVisibleThingBeforeCurrentBlock;
      }
      return WSScanResult::Error();
    }();

    // If previous visible thing is a <br>, we should just delete it without
    // unwrapping the first line of the right child block.  Note that the <br>
    // is always treated as invisible by HTMLEditUtils because it's immediately
    // preceding <br> of the block boundary.  However, deleting it is fine
    // because the above checks whether it causes empty line or not.
    if (prevVisibleThing.ReachedBRElement()) {
      mMode = Mode::DeletePrecedingBRElementOfBlock;
      mBRElement = prevVisibleThing.BRElementPtr();
      return true;
    }

    // Same for a preformatted line break.
    if (prevVisibleThing.ReachedPreformattedLineBreak()) {
      mMode = Mode::DeletePrecedingPreformattedLineBreak;
      mPreformattedLineBreak =
          prevVisibleThing.PointAtReachedContent<EditorRawDOMPoint>()
              .AsInText();
      return true;
    }

    mLeftContent = ScanJoinTarget();
    mRightContent = aCaretPoint.GetContainerAs<nsIContent>();
  } else {
    mRightContent = ScanJoinTarget();
    mLeftContent = aCaretPoint.GetContainerAs<nsIContent>();
  }

  // Nothing to join
  if (!mLeftContent || !mRightContent) {
    return false;
  }

  // Don't cross table boundaries.
  return HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mLeftContent) ==
         HTMLEditUtils::GetInclusiveAncestorAnyTableElement(*mRightContent);
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteAtCurrentBlockBoundary(
        const HTMLEditor& aHTMLEditor, const EditorDOMPoint& aCaretPoint,
        dom::Range& aRangeToDelete, const Element& aEditingHost) const {
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mRightContent);

  AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                  *mRightContent);
  Result<bool, nsresult> canJoinThem =
      joiner.Prepare(aHTMLEditor, aEditingHost);
  if (canJoinThem.isErr()) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
    return canJoinThem.unwrapErr();
  }
  if (canJoinThem.inspect()) {
    nsresult rv = joiner.ComputeRangeToDelete(aHTMLEditor, aCaretPoint,
                                              aRangeToDelete, aEditingHost);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoInclusiveAncestorBlockElementsJoiner::"
                         "ComputeRangesToDelete() failed");
    return rv;
  }

  // In this case, nothing will be deleted so that the affected range should
  // be collapsed.
  nsresult rv = aRangeToDelete.CollapseTo(aCaretPoint.ToRawRangeBoundary());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Range::CollapseTo() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::HandleDeleteAtCurrentBlockBoundary(
        HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
        const EditorDOMPoint& aCaretPoint, const Element& aEditingHost) {
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mRightContent);

  AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                  *mRightContent);
  Result<bool, nsresult> canJoinThem =
      joiner.Prepare(aHTMLEditor, aEditingHost);
  if (MOZ_UNLIKELY(canJoinThem.isErr())) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
    return Err(canJoinThem.unwrapErr());
  }

  if (!canJoinThem.inspect() || !joiner.CanJoinBlocks()) {
    nsresult rv = aHTMLEditor.CollapseSelectionTo(aCaretPoint);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
    return !canJoinThem.inspect() ? EditActionResult::CanceledResult()
                                  : EditActionResult::HandledResult();
  }

  EditorDOMPoint pointToPutCaret(aCaretPoint);
  AutoTrackDOMPoint tracker(aHTMLEditor.RangeUpdaterRef(), &pointToPutCaret);
  Result<DeleteRangeResult, nsresult> moveFirstLineResult =
      joiner.Run(aHTMLEditor, aEditingHost);
  if (MOZ_UNLIKELY(moveFirstLineResult.isErr())) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Run() failed");
    return moveFirstLineResult.propagateErr();
  }
  DeleteRangeResult unwrappedMoveFirstLineResult = moveFirstLineResult.unwrap();
  MOZ_ASSERT_IF(
      unwrappedMoveFirstLineResult.HasCaretPointSuggestion(),
      HTMLEditUtils::IsSimplyEditableNode(
          *unwrappedMoveFirstLineResult.CaretPointRef().GetContainer()));
#ifdef DEBUG
  if (joiner.ShouldDeleteLeafContentInstead()) {
    NS_ASSERTION(unwrappedMoveFirstLineResult.Ignored(),
                 "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                 "returning ignored, but returned not ignored");
  } else {
    NS_ASSERTION(!unwrappedMoveFirstLineResult.Ignored(),
                 "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                 "returning handled, but returned ignored");
  }
#endif  // #ifdef DEBUG

  // Cleaning up invisible nodes which are skipped at scanning mLeftContent or
  // mRightContent.
  {
    AutoTrackDOMDeleteRangeResult trackMoveFirstLineResult(
        aHTMLEditor.RangeUpdaterRef(), &unwrappedMoveFirstLineResult);
    for (const OwningNonNull<nsIContent>& content : mSkippedInvisibleContents) {
      nsresult rv =
          aHTMLEditor.DeleteNodeWithTransaction(MOZ_KnownLive(content));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return Err(rv);
      }
    }
    mSkippedInvisibleContents.Clear();
    trackMoveFirstLineResult.Flush(StopTracking::Yes);
    if (unwrappedMoveFirstLineResult.HasCaretPointSuggestion() &&
        NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(
            *unwrappedMoveFirstLineResult.CaretPointRef().GetContainer()))) {
      unwrappedMoveFirstLineResult.ForgetCaretPointSuggestion();
    }
  }

  // If we're deleting selection (not replacing with new content) and
  // AutoInclusiveAncestorBlockElementsJoiner computed new caret position, we
  // should use it.  Otherwise, we should keep the our traditional behavior.
  if (unwrappedMoveFirstLineResult.Handled() &&
      unwrappedMoveFirstLineResult.HasCaretPointSuggestion() &&
      MayEditActionDeleteAroundCollapsedSelection(
          aHTMLEditor.GetEditAction())) {
    EditorDOMPoint pointToPutCaret =
        unwrappedMoveFirstLineResult.UnwrapCaretPoint();
    // Don't remove empty inline elements in the plaintext-only mode because
    // nobody can restore the style again.
    if (pointToPutCaret.IsInContentNodeAndValidInComposedDoc() &&
        !aEditingHost.IsContentEditablePlainTextOnly() &&
        MOZ_LIKELY(HTMLEditUtils::IsRemovableFromParentNode(
            *pointToPutCaret.ContainerAs<nsIContent>()))) {
      AutoTrackDOMPoint trackCaretPoint(aHTMLEditor.RangeUpdaterRef(),
                                        &pointToPutCaret);
      Result<CaretPoint, nsresult> caretPointOrError =
          aHTMLEditor.DeleteEmptyInclusiveAncestorInlineElements(
              MOZ_KnownLive(*pointToPutCaret.ContainerAs<nsIContent>()),
              aEditingHost);
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::DeleteEmptyInclusiveAncestorInlineElements() failed");
        return caretPointOrError.propagateErr();
      }
      trackCaretPoint.Flush(StopTracking::Yes);
      caretPointOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    }
    if ((aHTMLEditor.IsMailEditor() || aHTMLEditor.IsPlaintextMailComposer()) &&
        MOZ_LIKELY(pointToPutCaret.IsInContentNode())) {
      AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                             &pointToPutCaret);
      nsresult rv = aHTMLEditor.DeleteMostAncestorMailCiteElementIfEmpty(
          MOZ_KnownLive(*pointToPutCaret.ContainerAs<nsIContent>()));
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty() failed");
        return Err(rv);
      }
      trackPointToPutCaret.Flush(StopTracking::Yes);
      if (NS_WARN_IF(!pointToPutCaret.IsSetAndValidInComposedDoc())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
    if (aHTMLEditor.GetTopLevelEditSubAction() ==
            EditSubAction::eDeleteSelectedContent &&
        pointToPutCaret.IsSetAndValidInComposedDoc()) {
      AutoTrackDOMPoint trackCaretPoint(aHTMLEditor.RangeUpdaterRef(),
                                        &pointToPutCaret);
      Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
          aHTMLEditor.InsertPaddingBRElementIfNeeded(
              pointToPutCaret,
              aEditingHost.IsContentEditablePlainTextOnly()
                  ? nsIEditor::eNoStrip
                  : nsIEditor::eStrip,
              aEditingHost);
      if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
        NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
        return insertPaddingBRElementOrError.propagateErr();
      }
      insertPaddingBRElementOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    }
    nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed, but ignored");
      return EditActionResult::HandledResult();
    }
    // If we prefer to use style in the previous line, we should forget
    // previous styles since the caret position has all styles which we want
    // to use with new content.
    if (nsIEditor::DirectionIsBackspace(aDirectionAndAmount)) {
      aHTMLEditor.TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
    }
    // And we don't want to keep extending a link at ex-end of the previous
    // paragraph.
    if (HTMLEditor::GetLinkElement(pointToPutCaret.GetContainer())) {
      aHTMLEditor.mPendingStylesToApplyToNewContent
          ->ClearLinkAndItsSpecifiedStyle();
    }
    return EditActionResult::HandledResult();
  }
  unwrappedMoveFirstLineResult.IgnoreCaretPointSuggestion();
  tracker.Flush(StopTracking::Yes);
  nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed, but ignored");
  // This should claim that trying to join the block means that
  // this handles the action because the caller shouldn't do anything
  // anymore in this case.
  return EditActionResult::HandledResult();
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::ComputeRangesToDeleteNonCollapsedRanges(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    AutoDeleteRangesHandler::SelectionWasCollapsed aSelectionWasCollapsed,
    const Element& aEditingHost) const {
  MOZ_ASSERT(!aRangesToDelete.IsCollapsed());

  if (NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->StartRef().IsSet()) ||
      NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->EndRef().IsSet())) {
    return NS_ERROR_FAILURE;
  }

  if (aRangesToDelete.Ranges().Length() == 1) {
    Result<EditorRawDOMRange, nsresult> result = ExtendOrShrinkRangeToDelete(
        aHTMLEditor, aRangesToDelete.LimitersAndCaretDataRef(),
        EditorRawDOMRange(aRangesToDelete.FirstRangeRef()),
        aSelectionWasCollapsed, ComputeRangeFor::GetTargetRanges, aEditingHost);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "AutoDeleteRangesHandler::ExtendOrShrinkRangeToDelete() failed");
      return NS_ERROR_FAILURE;
    }
    EditorRawDOMRange newRange(result.unwrap());
    if (MOZ_UNLIKELY(NS_FAILED(aRangesToDelete.FirstRangeRef()->SetStartAndEnd(
            newRange.StartRef().ToRawRangeBoundary(),
            newRange.EndRef().ToRawRangeBoundary())))) {
      NS_WARNING("Range::SetStartAndEnd() failed");
      return NS_ERROR_FAILURE;
    }
    if (MOZ_UNLIKELY(
            NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->IsPositioned()))) {
      return NS_ERROR_FAILURE;
    }
    if (NS_WARN_IF(aRangesToDelete.FirstRangeRef()->Collapsed())) {
      return NS_OK;  // Hmm, there is nothing to delete...?
    }
  }

  if (!aHTMLEditor.IsPlaintextMailComposer()) {
    EditorDOMRange firstRange(aRangesToDelete.FirstRangeRef());
    EditorDOMRange extendedRange =
        WSRunScanner::GetRangeContainingInvisibleWhiteSpacesAtRangeBoundaries(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorDOMRange(aRangesToDelete.FirstRangeRef()));
    if (firstRange != extendedRange) {
      nsresult rv = aRangesToDelete.FirstRangeRef()->SetStartAndEnd(
          extendedRange.StartRef().ToRawRangeBoundary(),
          extendedRange.EndRef().ToRawRangeBoundary());
      if (NS_FAILED(rv)) {
        NS_WARNING("Range::SetStartAndEnd() failed");
        return NS_ERROR_FAILURE;
      }
    }
  }

  if (aRangesToDelete.FirstRangeRef()->GetStartContainer() ==
      aRangesToDelete.FirstRangeRef()->GetEndContainer()) {
    if (!aRangesToDelete.FirstRangeRef()->Collapsed()) {
      nsresult rv = ComputeRangesToDeleteRangesWithTransaction(
          aHTMLEditor, aDirectionAndAmount, aRangesToDelete, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoDeleteRangesHandler::ComputeRangesToDeleteRangesWithTransaction("
          ") failed");
      return rv;
    }
    // `DeleteUnnecessaryNodes()` may delete parent elements, but it does not
    // affect computing target ranges.  Therefore, we don't need to touch
    // aRangesToDelete in this case.
    return NS_OK;
  }

  Element* startCiteNode = aHTMLEditor.GetMostDistantAncestorMailCiteElement(
      *aRangesToDelete.FirstRangeRef()->GetStartContainer());
  Element* endCiteNode = aHTMLEditor.GetMostDistantAncestorMailCiteElement(
      *aRangesToDelete.FirstRangeRef()->GetEndContainer());

  if (startCiteNode && !endCiteNode) {
    aDirectionAndAmount = nsIEditor::eNext;
  } else if (!startCiteNode && endCiteNode) {
    aDirectionAndAmount = nsIEditor::ePrevious;
  }

  for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
    if (MOZ_UNLIKELY(range->Collapsed())) {
      continue;
    }
    AutoBlockElementsJoiner joiner(*this);
    if (!joiner.PrepareToDeleteNonCollapsedRange(aHTMLEditor, range,
                                                 aEditingHost)) {
      return NS_ERROR_FAILURE;
    }
    nsresult rv = joiner.ComputeRangeToDelete(
        aHTMLEditor, aRangesToDelete, aDirectionAndAmount, range,
        aSelectionWasCollapsed, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoBlockElementsJoiner::ComputeRangeToDelete() failed");
      return rv;
    }
  }
  return NS_OK;
}

Result<EditActionResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::HandleDeleteNonCollapsedRanges(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers,
    AutoClonedSelectionRangeArray& aRangesToDelete,
    SelectionWasCollapsed aSelectionWasCollapsed, const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(!aRangesToDelete.IsCollapsed());

  if (NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->StartRef().IsSet()) ||
      NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->EndRef().IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  MOZ_ASSERT_IF(aRangesToDelete.Ranges().Length() == 1,
                aRangesToDelete.IsFirstRangeEditable(aEditingHost));

  // Else we have a non-collapsed selection.  First adjust the selection.
  // XXX Why do we extend selection only when there is only one range?
  if (aRangesToDelete.Ranges().Length() == 1) {
    Result<EditorRawDOMRange, nsresult> result = ExtendOrShrinkRangeToDelete(
        aHTMLEditor, aRangesToDelete.LimitersAndCaretDataRef(),
        EditorRawDOMRange(aRangesToDelete.FirstRangeRef()),
        aSelectionWasCollapsed, ComputeRangeFor::ToDeleteTheRange,
        aEditingHost);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "AutoDeleteRangesHandler::ExtendOrShrinkRangeToDelete() failed");
      return Err(NS_ERROR_FAILURE);
    }
    EditorRawDOMRange newRange(result.unwrap());
    if (NS_FAILED(aRangesToDelete.FirstRangeRef()->SetStartAndEnd(
            newRange.StartRef().ToRawRangeBoundary(),
            newRange.EndRef().ToRawRangeBoundary()))) {
      NS_WARNING("Range::SetStartAndEnd() failed");
      return Err(NS_ERROR_FAILURE);
    }
    if (NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->IsPositioned())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (NS_WARN_IF(aRangesToDelete.FirstRangeRef()->Collapsed())) {
      // Hmm, there is nothing to delete...?
      // In this case, the callers want collapsed selection.  Therefore, we need
      // to change the `Selection` here.
      nsresult rv = aHTMLEditor.CollapseSelectionTo(
          aRangesToDelete.GetFirstRangeStartPoint<EditorRawDOMPoint>());
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
      return EditActionResult::HandledResult();
    }
    MOZ_ASSERT(aRangesToDelete.IsFirstRangeEditable(aEditingHost));
  }

  // Remember that we did a ranged delete for the benefit of
  // OnEndHandlingTopLevelEditSubActionInternal().
  aHTMLEditor.TopLevelEditSubActionDataRef().mDidDeleteNonCollapsedRange = true;

  // Figure out if the endpoints are in nodes that can be merged.  Adjust
  // surrounding white-space in preparation to delete selection.
  if (!aHTMLEditor.IsPlaintextMailComposer()) {
    {
      AutoTrackDOMRange firstRangeTracker(aHTMLEditor.RangeUpdaterRef(),
                                          &aRangesToDelete.FirstRangeRef());
      for (OwningNonNull<dom::Range>& range :
           Reversed(aRangesToDelete.Ranges())) {
        if (MOZ_UNLIKELY(!range->IsPositioned() || range->Collapsed())) {
          continue;
        }
        Maybe<AutoTrackDOMRange> trackRange;
        if (range != aRangesToDelete.FirstRangeRef()) {
          trackRange.emplace(aHTMLEditor.RangeUpdaterRef(), &range);
        }
        Result<EditorDOMRange, nsresult> rangeToDeleteOrError =
            WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin(
                aHTMLEditor, EditorDOMRange(range));
        if (MOZ_UNLIKELY(rangeToDeleteOrError.isErr())) {
          NS_WARNING(
              "WhiteSpaceVisibilityKeeper::"
              "NormalizeSurroundingWhiteSpacesToJoin() failed");
          return rangeToDeleteOrError.propagateErr();
        }
        trackRange.reset();
        EditorDOMRange rangeToDelete = rangeToDeleteOrError.unwrap();
        if (MOZ_LIKELY(rangeToDelete.IsPositionedAndValidInComposedDoc())) {
          nsresult rv = range->SetStartAndEnd(
              rangeToDelete.StartRef().ToRawRangeBoundary(),
              rangeToDelete.EndRef().ToRawRangeBoundary());
          if (NS_FAILED(rv)) {
            NS_WARNING("Range::SetStartAndEnd() failed");
            return Err(rv);
          }
        }
      }
    }
    aRangesToDelete.RemoveCollapsedRanges(
        AutoClonedRangeArray::PreservePrimaryRangeIfAllRangesCollapsed::No);
    if (aRangesToDelete.IsCollapsed()) [[unlikely]] {
      return EditActionResult::HandledResult();
    }
    if (NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->IsPositioned()) ||
        (aHTMLEditor.MaybeNodeRemovalsObservedByDevTools() &&
         NS_WARN_IF(!aRangesToDelete.IsFirstRangeEditable(aEditingHost)))) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::PrepareToDeleteRange() made the first "
          "range invalid");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  // XXX This is odd.  We do we simply use `DeleteRangesWithTransaction()`
  //     only when **first** range is in same container?
  if (aRangesToDelete.FirstRangeRef()->GetStartContainer() ==
      aRangesToDelete.FirstRangeRef()->GetEndContainer()) {
    // Because of previous DOM tree changes, the range may be collapsed.
    // If we've already removed all contents in the range, we shouldn't
    // delete anything around the caret.
    if (!aRangesToDelete.FirstRangeRef()->Collapsed()) {
      const auto stripWrappers = [&]() -> nsIEditor::EStripWrappers {
        if (mOriginalStripWrappers == nsIEditor::eStrip &&
            aEditingHost.IsContentEditablePlainTextOnly()) {
          return nsIEditor::eNoStrip;
        }
        return mOriginalStripWrappers;
      }();
      AutoTrackDOMRange firstRangeTracker(aHTMLEditor.RangeUpdaterRef(),
                                          &aRangesToDelete.FirstRangeRef());
      Result<CaretPoint, nsresult> caretPointOrError =
          aHTMLEditor.DeleteRangesWithTransaction(
              aDirectionAndAmount, stripWrappers, aRangesToDelete);
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING("HTMLEditor::DeleteRangesWithTransaction() failed");
        return caretPointOrError.propagateErr();
      }
      firstRangeTracker.Flush(StopTracking::Yes);
      nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
          aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                        SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                        SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CaretPoint::SuggestCaretPointTo() failed, but ignored");
      if (NS_WARN_IF(!aRangesToDelete.FirstRangeRef()->IsPositioned()) ||
          (aHTMLEditor.MaybeNodeRemovalsObservedByDevTools() &&
           NS_WARN_IF(!aRangesToDelete.IsFirstRangeEditable(aEditingHost)))) {
        NS_WARNING(
            "HTMLEditor::DeleteRangesWithTransaction() made the first range "
            "invalid");
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
    // However, even if the range is removed, we may need to clean up the
    // containers which become empty.
    EditorDOMRange rangeToCleanUp(aRangesToDelete.FirstRangeRef());
    AutoTrackDOMRange trackRangeToCleanUp(aHTMLEditor.RangeUpdaterRef(),
                                          &rangeToCleanUp);
    nsresult rv =
        DeleteUnnecessaryNodes(aHTMLEditor, rangeToCleanUp, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoDeleteRangesHandler::DeleteUnnecessaryNodes() failed");
      return Err(rv);
    }
    trackRangeToCleanUp.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!rangeToCleanUp.IsPositionedAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    const auto& pointToPutCaret =
        !nsIEditor::DirectionIsBackspace(aDirectionAndAmount) ||
                (aHTMLEditor.TopLevelEditSubActionDataRef()
                     .mDidDeleteEmptyParentBlocks &&
                 (aHTMLEditor.GetEditAction() == EditAction::eDrop ||
                  aHTMLEditor.GetEditAction() == EditAction::eDeleteByDrag))
            ? rangeToCleanUp.StartRef()
            : rangeToCleanUp.EndRef();
    rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  if (NS_WARN_IF(
          !aRangesToDelete.FirstRangeRef()->GetStartContainer()->IsContent()) ||
      NS_WARN_IF(
          !aRangesToDelete.FirstRangeRef()->GetEndContainer()->IsContent())) {
    return Err(NS_ERROR_FAILURE);
  }

  // Figure out mailcite ancestors
  RefPtr<Element> startCiteNode =
      aHTMLEditor.GetMostDistantAncestorMailCiteElement(
          *aRangesToDelete.FirstRangeRef()->GetStartContainer());
  RefPtr<Element> endCiteNode =
      aHTMLEditor.GetMostDistantAncestorMailCiteElement(
          *aRangesToDelete.FirstRangeRef()->GetEndContainer());

  // If we only have a mailcite at one of the two endpoints, set the
  // directionality of the deletion so that the selection will end up
  // outside the mailcite.
  if (startCiteNode && !endCiteNode) {
    aDirectionAndAmount = nsIEditor::eNext;
  } else if (!startCiteNode && endCiteNode) {
    aDirectionAndAmount = nsIEditor::ePrevious;
  }

  MOZ_ASSERT(!aRangesToDelete.Ranges().IsEmpty());
  auto ret = EditActionResult::IgnoredResult();
  for (const OwningNonNull<dom::Range>& range : aRangesToDelete.Ranges()) {
    if (MOZ_UNLIKELY(range->Collapsed())) {
      continue;
    }
    AutoBlockElementsJoiner joiner(*this);
    if (!joiner.PrepareToDeleteNonCollapsedRange(aHTMLEditor, range,
                                                 aEditingHost)) {
      return Err(NS_ERROR_FAILURE);
    }
    Result<EditActionResult, nsresult> result =
        joiner.Run(aHTMLEditor, aRangesToDelete.LimitersAndCaretDataRef(),
                   aDirectionAndAmount, aStripWrappers, MOZ_KnownLive(range),
                   aSelectionWasCollapsed, aEditingHost);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("AutoBlockElementsJoiner::Run() failed");
      return result;
    }
    ret |= result.inspect();
  }
  return ret;
}

bool HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    PrepareToDeleteNonCollapsedRange(const HTMLEditor& aHTMLEditor,
                                     const dom::Range& aRangeToDelete,
                                     const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());

  mLeftContent = HTMLEditUtils::GetInclusiveAncestorElement(
      *aRangeToDelete.GetStartContainer()->AsContent(),
      HTMLEditUtils::ClosestEditableBlockElement,
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  mRightContent = HTMLEditUtils::GetInclusiveAncestorElement(
      *aRangeToDelete.GetEndContainer()->AsContent(),
      HTMLEditUtils::ClosestEditableBlockElement,
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  // Note that mLeftContent and/or mRightContent can be nullptr if editing host
  // is an inline element.  If both editable ancestor block is exactly same
  // one or one reaches an inline editing host, we can just delete the content
  // in ranges.
  if (mLeftContent == mRightContent || !mLeftContent || !mRightContent) {
    MOZ_ASSERT_IF(
        !mLeftContent || !mRightContent,
        aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost() ==
            aRangeToDelete.GetEndContainer()->AsContent()->GetEditingHost());
    mMode = Mode::DeleteContentInRange;
    return true;
  }

  // If left block and right block are adjacent siblings and they are same
  // type of elements, we can merge them after deleting the selected contents.
  // MOOSE: this could conceivably screw up a table.. fix me.
  if (mLeftContent->GetParentNode() == mRightContent->GetParentNode() &&
      HTMLEditUtils::CanContentsBeJoined(*mLeftContent, *mRightContent) &&
      // XXX What's special about these three types of block?
      (mLeftContent->IsHTMLElement(nsGkAtoms::p) ||
       HTMLEditUtils::IsListItemElement(*mLeftContent) ||
       HTMLEditUtils::IsHeadingElement(*mLeftContent))) {
    mMode = Mode::JoinBlocksInSameParent;
    return true;
  }

  // If the range starts immediately after a line end and ends in a
  // child right block, we should not unwrap the right block unless the
  // right block will have no nodes.
  if (mRightContent->IsInclusiveDescendantOf(mLeftContent)) {
    // FYI: Chrome does not remove the right child block even if there will be
    // only single <br> or a comment node in it.  Therefore, we should use this
    // rough check.
    const WSScanResult nextVisibleThingOfEndBoundary =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorRawDOMPoint(aRangeToDelete.EndRef()));
    if (!nextVisibleThingOfEndBoundary.ReachedCurrentBlockBoundary()) {
      MOZ_ASSERT(mLeftContent->IsElement());
      Result<Element*, nsresult> mostDistantBlockOrError =
          AutoBlockElementsJoiner::
              GetMostDistantBlockAncestorIfPointIsStartAtBlock(
                  EditorRawDOMPoint(mRightContent, 0), aEditingHost,
                  mLeftContent->AsElement());
      MOZ_ASSERT(mostDistantBlockOrError.isOk());
      if (MOZ_LIKELY(mostDistantBlockOrError.inspect())) {
        const WSScanResult prevVisibleThingOfStartBoundary =
            WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
                {WSRunScanner::Option::OnlyEditableNodes},
                EditorRawDOMPoint(aRangeToDelete.StartRef()));
        if (prevVisibleThingOfStartBoundary.ReachedBRElement()) {
          // If the range start after a <br> followed by the block boundary,
          // we want to delete the <br> or following <br> element unless it's
          // not a part of empty line like `<div>abc<br>{<div>]def`.
          const WSScanResult nextVisibleThingOfBR =
              WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  EditorRawDOMPoint::After(
                      *prevVisibleThingOfStartBoundary.GetContent()));
          MOZ_ASSERT(!nextVisibleThingOfBR.ReachedCurrentBlockBoundary());
          if (!nextVisibleThingOfBR.ReachedOtherBlockElement() ||
              nextVisibleThingOfBR.GetContent() !=
                  mostDistantBlockOrError.inspect()) {
            // The range selects a non-empty line or a child block at least.
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
          const WSScanResult prevVisibleThingOfBR =
              WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  EditorRawDOMPoint(
                      prevVisibleThingOfStartBoundary.GetContent()));
          if (prevVisibleThingOfBR.ReachedLineBoundary()) {
            // The preceding <br> causes an empty line.
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
        } else if (prevVisibleThingOfStartBoundary
                       .ReachedPreformattedLineBreak()) {
          const WSScanResult nextVisibleThingOfLineBreak =
              WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  prevVisibleThingOfStartBoundary
                      .PointAfterReachedContent<EditorRawDOMPoint>());
          MOZ_ASSERT(
              !nextVisibleThingOfLineBreak.ReachedCurrentBlockBoundary());
          if (!nextVisibleThingOfLineBreak.ReachedOtherBlockElement() ||
              nextVisibleThingOfLineBreak.GetContent() !=
                  mostDistantBlockOrError.inspect()) {
            // The range selects a non-empty line or a child block at least.
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
          const WSScanResult prevVisibleThingOfLineBreak =
              WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  prevVisibleThingOfStartBoundary
                      .PointAtReachedContent<EditorRawDOMPoint>());
          if (prevVisibleThingOfLineBreak.ReachedLineBoundary()) {
            // The preceding line break causes an empty line.
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
        } else if (prevVisibleThingOfStartBoundary
                       .ReachedCurrentBlockBoundary()) {
          MOZ_ASSERT(prevVisibleThingOfStartBoundary.ElementPtr() ==
                     mLeftContent);
          const WSScanResult firstVisibleThingInBlock =
              WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  EditorRawDOMPoint(
                      prevVisibleThingOfStartBoundary.ElementPtr(), 0));
          if (!firstVisibleThingInBlock.ReachedOtherBlockElement() ||
              firstVisibleThingInBlock.ElementPtr() !=
                  mostDistantBlockOrError.inspect()) {
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
        } else if (prevVisibleThingOfStartBoundary.ReachedOtherBlockElement()) {
          const WSScanResult firstVisibleThingAfterBlock =
              WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                  {WSRunScanner::Option::OnlyEditableNodes},
                  EditorRawDOMPoint::After(
                      *prevVisibleThingOfStartBoundary.ElementPtr()));
          if (!firstVisibleThingAfterBlock.ReachedOtherBlockElement() ||
              firstVisibleThingAfterBlock.ElementPtr() !=
                  mostDistantBlockOrError.inspect()) {
            mMode = Mode::DeletePrecedingLinesAndContentInRange;
            return true;
          }
        }
      }
    }
  }

  if (EditorRawDOMPoint::After(*mRightContent)
          .EqualsOrIsBefore(EditorRawDOMPoint(aRangeToDelete.EndRef()))) {
    // If mRightContent is completely in the range to delete, we won't join
    // mLeftContent and mRightContent which mRightContent will be deleted. So,
    // we need just to delete all things in the range.
    mMode = Mode::DeleteContentInRange;
  } else {
    // Otherwise, we need to delete the content in the range and then, join
    // mLeftContent and mRightContent.
    mMode = Mode::DeleteNonCollapsedRange;
  }
  return true;
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteContentInRange(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount, dom::Range& aRangeToDelete,
        const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mMode == Mode::DeleteContentInRange);
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost());
  MOZ_ASSERT(
      aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost() ==
      aRangeToDelete.GetEndContainer()->AsContent()->GetEditingHost());
  MOZ_ASSERT_IF(mLeftContent, mLeftContent->IsElement());
  MOZ_ASSERT_IF(mLeftContent,
                aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
                    mLeftContent));
  MOZ_ASSERT_IF(mRightContent, mRightContent->IsElement());
  MOZ_ASSERT_IF(
      mRightContent,
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));
  MOZ_ASSERT_IF(
      !mLeftContent,
      HTMLEditUtils::IsInlineContent(
          *aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost(),
          BlockInlineCheck::UseComputedDisplayOutsideStyle));

  nsresult rv =
      mDeleteRangesHandlerConst.ComputeRangeToDeleteRangeWithTransaction(
          aHTMLEditor, aDirectionAndAmount, aRangeToDelete, aEditingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoDeleteRangesHandler::"
                       "ComputeRangeToDeleteRangeWithTransaction() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::DeleteContentInRange(
        HTMLEditor& aHTMLEditor,
        const LimitersAndCaretData& aLimitersAndCaretData,
        nsIEditor::EDirection aDirectionAndAmount,
        nsIEditor::EStripWrappers aStripWrappers, dom::Range& aRangeToDelete,
        const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mMode == Mode::DeleteContentInRange);
  MOZ_ASSERT(mDeleteRangesHandler);
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost());
  MOZ_ASSERT(
      aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost() ==
      aRangeToDelete.GetEndContainer()->AsContent()->GetEditingHost());
  MOZ_ASSERT_IF(mLeftContent, mLeftContent->IsElement());
  MOZ_ASSERT_IF(mLeftContent,
                aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
                    mLeftContent));
  MOZ_ASSERT_IF(mRightContent, mRightContent->IsElement());
  MOZ_ASSERT_IF(
      mRightContent,
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));
  MOZ_ASSERT_IF(
      !mLeftContent,
      HTMLEditUtils::IsInlineContent(
          *aRangeToDelete.GetStartContainer()->AsContent()->GetEditingHost(),
          BlockInlineCheck::UseComputedDisplayOutsideStyle));

  const OwningNonNull<dom::Range> rangeToDelete(aRangeToDelete);
  Result<EditorDOMRange, nsresult> rangeToDeleteOrError =
      WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin(
          aHTMLEditor, EditorDOMRange(rangeToDelete));
  if (MOZ_UNLIKELY(rangeToDeleteOrError.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin() "
        "failed");
    return rangeToDeleteOrError.propagateErr();
  }
  nsresult rv = rangeToDeleteOrError.unwrap().SetToRange(rangeToDelete);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorDOMRange::SetToRange() failed");
    return Err(rv);
  }
  if (!rangeToDelete->Collapsed()) {
    AutoClonedSelectionRangeArray rangesToDelete(*rangeToDelete,
                                                 aLimitersAndCaretData);
    AutoTrackDOMRange trackRangeToDelete(aHTMLEditor.RangeUpdaterRef(),
                                         &rangeToDelete);
    Result<CaretPoint, nsresult> caretPointOrError =
        aHTMLEditor.DeleteRangesWithTransaction(aDirectionAndAmount,
                                                aStripWrappers, rangesToDelete);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      if (NS_WARN_IF(caretPointOrError.inspectErr() ==
                     NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING(
          "HTMLEditor::DeleteRangesWithTransaction() failed, but ignored");
    } else {
      nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
          aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                        SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                        SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CaretPoint::SuggestCaretPointTo() failed, but ignored");
    }
  }

  if (NS_WARN_IF(!rangeToDelete->IsPositioned())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  EditorDOMRange rangeToCleanUp(*rangeToDelete);
  {
    AutoTrackDOMRange trackRangeToCleanUp(aHTMLEditor.RangeUpdaterRef(),
                                          &rangeToCleanUp);
    nsresult rv = mDeleteRangesHandler->DeleteUnnecessaryNodes(
        aHTMLEditor, rangeToCleanUp, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoDeleteRangesHandler::DeleteUnnecessaryNodes() failed");
      return Err(rv);
    }
  }
  const auto& pointToPutCaret = [&]() -> const EditorDOMPoint& {
    // If we delete left or right block, we want to collapse selection to the
    // remaining block.
    if (!mLeftContent != !mRightContent) {
      return mLeftContent ? rangeToCleanUp.StartRef() : rangeToCleanUp.EndRef();
    }
    // Otherwise, we should consider the caret position from the deleting
    // direction.
    return !nsIEditor::DirectionIsBackspace(aDirectionAndAmount) ||
                   (aHTMLEditor.TopLevelEditSubActionDataRef()
                        .mDidDeleteEmptyParentBlocks &&
                    (aHTMLEditor.GetEditAction() == EditAction::eDrop ||
                     aHTMLEditor.GetEditAction() == EditAction::eDeleteByDrag))
               ? rangeToCleanUp.StartRef()
               : rangeToCleanUp.EndRef();
  }();
  rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToJoinBlockElementsInSameParent(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount, dom::Range& aRangeToDelete,
        const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mMode == Mode::JoinBlocksInSameParent);
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mLeftContent->IsElement());
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
      mLeftContent));
  MOZ_ASSERT(mRightContent);
  MOZ_ASSERT(mRightContent->IsElement());
  MOZ_ASSERT(
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));
  MOZ_ASSERT(mLeftContent->GetParentNode() == mRightContent->GetParentNode());

  nsresult rv =
      mDeleteRangesHandlerConst.ComputeRangeToDeleteRangeWithTransaction(
          aHTMLEditor, aDirectionAndAmount, aRangeToDelete, aEditingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoDeleteRangesHandler::"
                       "ComputeRangeToDeleteRangeWithTransaction() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::JoinBlockElementsInSameParent(
        HTMLEditor& aHTMLEditor,
        const LimitersAndCaretData& aLimitersAndCaretData,
        nsIEditor::EDirection aDirectionAndAmount,
        nsIEditor::EStripWrappers aStripWrappers, dom::Range& aRangeToDelete,
        SelectionWasCollapsed aSelectionWasCollapsed,
        const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mMode == Mode::JoinBlocksInSameParent);
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mLeftContent->IsElement());
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
      mLeftContent));
  MOZ_ASSERT(mRightContent);
  MOZ_ASSERT(mRightContent->IsElement());
  MOZ_ASSERT(
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));
  MOZ_ASSERT(mLeftContent->GetParentNode() == mRightContent->GetParentNode());

  const bool backspaceInRightBlock =
      aSelectionWasCollapsed == SelectionWasCollapsed::Yes &&
      nsIEditor::DirectionIsBackspace(aDirectionAndAmount);

  const OwningNonNull<dom::Range> rangeToDelete(aRangeToDelete);

  // If mLeftContent ends with an invisible line break, we should delete it
  // before joining the blocks because that will appear as a visible line break
  // if JoinNodesDeepWithTransaction() moves the first line content of
  // mRightContent to end of the line break in mLeftContent.
  if (HTMLEditUtils::IsBlockElement(
          *mLeftContent, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    const WSScanResult lastThingInLeftBlock =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {}, EditorRawDOMPoint::AtEndOf(*mLeftContent));
    if (lastThingInLeftBlock.ReachedLineBreak()) {
      const EditorLineBreak lineBreak =
          lastThingInLeftBlock.CreateEditorLineBreak<EditorLineBreak>();
      // FIXME: If the line break is wrapped in an non-editable inline element,
      // we should delete its parent too or should fail.
      Result<EditorDOMPoint, nsresult> exLineBreakPointOrError =
          aHTMLEditor.DeleteLineBreakWithTransaction(
              lineBreak, nsIEditor::eNoStrip, aEditingHost);
      if (MOZ_UNLIKELY(exLineBreakPointOrError.isErr())) {
        NS_WARNING("HTMLEditor::DeleteLineBreakWithTransaction() failed");
        return exLineBreakPointOrError.propagateErr();
      }
    }
  }

  // Then, normalizer white-spaces at end of mLeftContent and at start of
  // mRightContent to keep their visibility.
  Result<EditorDOMRange, nsresult> rangeToDeleteOrError =
      WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin(
          aHTMLEditor, EditorDOMRange(*rangeToDelete));
  if (MOZ_UNLIKELY(rangeToDeleteOrError.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::NormalizeSurroundingWhiteSpacesToJoin() "
        "failed");
    return rangeToDeleteOrError.propagateErr();
  }
  nsresult rv = rangeToDeleteOrError.unwrap().SetToRange(rangeToDelete);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorDOMRange::SetToRange() failed");
    return Err(rv);
  }
  if (HTMLEditUtils::IsBlockElement(
          *mRightContent, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    MOZ_ASSERT(rangeToDelete->EndRef().GetContainer()->IsInclusiveDescendantOf(
        mRightContent));
    const WSScanResult nextThing =
        HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
            EditorRawDOMPoint(rangeToDelete->EndRef()),
            TreatAsUnnecessaryLineBreak{LineBreakFor::PaddingForEmptyBlock},
            aEditingHost, mRightContent->AsElement());
    if (nextThing.ReachedCurrentBlockBoundary()) {
      EditorRawDOMPoint atFollowingBlockBoundary =
          nextThing.PointAtReachedBlockBoundaryOrEditingHostBoundary<
              EditorRawDOMPoint>();
      if (atFollowingBlockBoundary != rangeToDelete->EndRef()) {
        IgnoredErrorResult error;
        rangeToDelete->SetEnd(atFollowingBlockBoundary.ToRawRangeBoundary(),
                              error);
        if (error.Failed()) [[unlikely]] {
          NS_WARNING("Range::SetEnd() failed");
          return Err(NS_ERROR_FAILURE);
        }
      }
    }
  }

  // Finally, delete the selected content and move first line of mRightContent
  // to end of mLeftContent.
  if (!rangeToDelete->Collapsed()) {
    AutoClonedSelectionRangeArray rangesToDelete(rangeToDelete,
                                                 aLimitersAndCaretData);
    Result<CaretPoint, nsresult> caretPointOrError =
        aHTMLEditor.DeleteRangesWithTransaction(aDirectionAndAmount,
                                                aStripWrappers, rangesToDelete);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("HTMLEditor::DeleteRangesWithTransaction() failed");
      return caretPointOrError.propagateErr();
    }

    nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
        aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                      SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                      SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "CaretPoint::SuggestCaretPointTo() failed, but ignored");
    // If mRightContent is removed by DeleteRangesWithTransaction(),
    // mRightContent was empty so that need to do nothing.
    if (!mRightContent->GetParentNode()) {
      return EditActionResult::HandledResult();
    }
  }

  if (NS_WARN_IF(!mLeftContent->GetParentNode()) ||
      NS_WARN_IF(!mRightContent->GetParentNode()) ||
      NS_WARN_IF(mLeftContent->GetParentNode() !=
                 mRightContent->GetParentNode())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  auto startOfRightContent =
      HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
          *mRightContent, {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                           EditablePointOption::StopAtComment});
  AutoTrackDOMPoint trackStartOfRightContent(aHTMLEditor.RangeUpdaterRef(),
                                             &startOfRightContent);
  Result<EditorDOMPoint, nsresult> atFirstChildOfTheLastRightNodeOrError =
      JoinNodesDeepWithTransaction(aHTMLEditor, MOZ_KnownLive(*mLeftContent),
                                   MOZ_KnownLive(*mRightContent));
  if (MOZ_UNLIKELY(atFirstChildOfTheLastRightNodeOrError.isErr())) {
    NS_WARNING("HTMLEditor::JoinNodesDeepWithTransaction() failed");
    return atFirstChildOfTheLastRightNodeOrError.propagateErr();
  }
  MOZ_ASSERT(atFirstChildOfTheLastRightNodeOrError.inspect().IsSet());
  trackStartOfRightContent.Flush(StopTracking::Yes);
  if (NS_WARN_IF(!startOfRightContent.IsSet()) ||
      NS_WARN_IF(!startOfRightContent.GetContainer()->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // If we're deleting selection (not replacing with new content) and the joined
  // point follows a text node, we should put caret to end of the preceding text
  // node because the other browsers insert following inputs into there.
  if (MayEditActionDeleteAroundCollapsedSelection(
          aHTMLEditor.GetEditAction())) {
    const WSScanResult maybePreviousText =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {}, startOfRightContent, &aEditingHost);
    if (maybePreviousText.ContentIsEditable() &&
        maybePreviousText.InVisibleOrCollapsibleCharacters()) {
      nsresult rv = aHTMLEditor.CollapseSelectionTo(
          maybePreviousText.PointAfterReachedContent<EditorRawDOMPoint>());
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
      // If we prefer to use style in the previous line, we should forget
      // previous styles since the caret position has all styles which we want
      // to use with new content.
      if (backspaceInRightBlock) {
        aHTMLEditor.TopLevelEditSubActionDataRef()
            .mCachedPendingStyles->Clear();
      }
      // And we don't want to keep extending a link at ex-end of the previous
      // paragraph.
      if (HTMLEditor::GetLinkElement(maybePreviousText.TextPtr())) {
        aHTMLEditor.mPendingStylesToApplyToNewContent
            ->ClearLinkAndItsSpecifiedStyle();
      }
      return EditActionResult::HandledResult();
    }
  }

  // Otherwise, we should put caret at start of the right content.
  rv = aHTMLEditor.CollapseSelectionTo(
      atFirstChildOfTheLastRightNodeOrError.inspect());
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

Result<bool, nsresult>
HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteNodesEntirelyInRangeButKeepTableStructure(
        const HTMLEditor& aHTMLEditor, dom::Range& aRange,
        AutoDeleteRangesHandler::
            SelectionWasCollapsed /*aSelectionWasCollapsed*/) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  AutoTArray<OwningNonNull<nsIContent>, 10> arrayOfTopChildren;
  DOMSubtreeIterator iter;
  nsresult rv = iter.Init(aRange);
  if (NS_FAILED(rv)) {
    NS_WARNING("DOMSubtreeIterator::Init() failed");
    return Err(rv);
  }
  iter.AppendAllNodesToArray(arrayOfTopChildren);
  return NeedsToJoinNodesAfterDeleteNodesEntirelyInRange();
}

Result<DeleteRangeResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::DeleteNodesEntirelyInRangeButKeepTableStructure(
        HTMLEditor& aHTMLEditor,
        const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContent,
        PutCaretTo aPutCaretTo) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  DeleteRangeResult deleteContentResult = DeleteRangeResult::IgnoredResult();
  for (const auto& content : aArrayOfContent) {
    // XXX After here, the child contents in the array may have been moved
    //     to somewhere or removed.  We should handle it.
    //
    // MOZ_KnownLive because 'aArrayOfContent' is guaranteed to
    // keep it alive.
    AutoTrackDOMDeleteRangeResult trackDeleteContentResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteContentResult);
    Result<DeleteRangeResult, nsresult> deleteResult =
        DeleteContentButKeepTableStructure(aHTMLEditor, MOZ_KnownLive(content));
    if (MOZ_UNLIKELY(deleteResult.isErr())) {
      if (NS_WARN_IF(deleteResult.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING(
          "AutoBlockElementsJoiner::DeleteContentButKeepTableStructure() "
          "failed, but ignored");
      continue;
    }
    trackDeleteContentResult.Flush(StopTracking::Yes);
    deleteContentResult |= deleteResult.unwrap();
  }
  if (deleteContentResult.Handled()) {
    EditorDOMPoint pointToPutCaret =
        aPutCaretTo == PutCaretTo::StartOfRange
            ? deleteContentResult.DeleteRangeRef().StartRef()
            : deleteContentResult.DeleteRangeRef().EndRef();
    deleteContentResult |= CaretPoint(std::move(pointToPutCaret));
  }
  return std::move(deleteContentResult);
}

bool HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    NeedsToJoinNodesAfterDeleteNodesEntirelyInRange() const {
  switch (mMode) {
    case Mode::DeletePrecedingLinesAndContentInRange:
    case Mode::DeleteBRElement:
    case Mode::DeletePrecedingBRElementOfBlock:
    case Mode::DeletePrecedingPreformattedLineBreak:
      // We want to delete a line but preseve the block structure around the
      // selection.
      return false;
    case Mode::DeleteNonCollapsedRange:
      // We must move the first line of mRightContent. If we'll delete
      // mRightContent instead, we should handle the case in the
      // DeleteContentInRange mode.
      return true;
    case Mode::DeleteContentInRange:
    case Mode::JoinBlocksInSameParent:
    case Mode::JoinCurrentBlock:
    case Mode::JoinOtherBlock:
    case Mode::NotInitialized:
      MOZ_ASSERT_UNREACHABLE("Shouldn't be handled in this path");
      break;
  }
  return false;
}

Result<DeleteRangeResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::DeleteTextAtStartAndEndOfRange(
        HTMLEditor& aHTMLEditor, dom::Range& aRange, PutCaretTo aPutCaretTo) {
  if (MOZ_UNLIKELY(aRange.Collapsed())) {
    return DeleteRangeResult::IgnoredResult();
  }

  const auto DeleteTextNode =
      [&aHTMLEditor](const OwningNonNull<Text>& aTextNode)
          MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    const nsCOMPtr<nsINode> parentNode = aTextNode->GetParentNode();
    if (NS_WARN_IF(!parentNode)) {
      return Err(NS_ERROR_FAILURE);
    }
    const nsCOMPtr<nsIContent> nextSibling = aTextNode->GetNextSibling();
    nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(aTextNode);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    if (NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parentNode) ||
        NS_WARN_IF(!parentNode->IsInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    const auto atRemovedTextNode = nextSibling
                                       ? EditorDOMPoint(nextSibling)
                                       : EditorDOMPoint::AtEndOf(*parentNode);
    return DeleteRangeResult(EditorDOMRange(atRemovedTextNode),
                             atRemovedTextNode);
  };

  EditorDOMRange range(aRange);
  // If the range is in a text node, delete middle of the text or the text node
  // itself.
  if (range.StartRef().IsInTextNode() && range.InSameContainer()) {
    const OwningNonNull<Text> textNode = *range.StartRef().ContainerAs<Text>();
    if (range.StartRef().IsStartOfContainer() &&
        range.EndRef().IsEndOfContainer()) {
      Result<DeleteRangeResult, nsresult> deleteTextNodeResult =
          DeleteTextNode(textNode);
      NS_WARNING_ASSERTION(
          deleteTextNodeResult.isOk(),
          "DeleteTextNode() failed to delete the selected Text node");
      return deleteTextNodeResult;
    }
    MOZ_ASSERT(range.EndRef().Offset() - range.StartRef().Offset() > 0);
    Result<CaretPoint, nsresult> caretPointOrError =
        aHTMLEditor.DeleteTextWithTransaction(
            textNode, range.StartRef().Offset(),
            range.EndRef().Offset() - range.StartRef().Offset());
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
      return caretPointOrError.propagateErr();
    }
    const EditorDOMPoint atRemovedText =
        caretPointOrError.unwrap().UnwrapCaretPoint();
    if (NS_WARN_IF(!atRemovedText.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return DeleteRangeResult(EditorDOMRange(atRemovedText), atRemovedText);
  }

  // If the range starts in a text node and ends in a different node, delete
  // the text after the start boundary.
  auto deleteStartTextResultOrError =
      [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    if (!range.StartRef().IsInTextNode() ||
        range.StartRef().IsEndOfContainer()) {
      return DeleteRangeResult::IgnoredResult();
    }
    AutoTrackDOMRange trackRange(aHTMLEditor.RangeUpdaterRef(), &range);
    const OwningNonNull<Text> textNode = *range.StartRef().ContainerAs<Text>();
    if (range.StartRef().IsStartOfContainer()) {
      Result<DeleteRangeResult, nsresult> deleteTextNodeResult =
          DeleteTextNode(textNode);
      NS_WARNING_ASSERTION(
          deleteTextNodeResult.isOk(),
          "DeleteTextNode() failed to delete the start Text node");
      return deleteTextNodeResult;
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        aHTMLEditor.DeleteTextWithTransaction(
            textNode, range.StartRef().Offset(),
            textNode->TextDataLength() - range.StartRef().Offset());
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
      return caretPointOrError.propagateErr();
    }
    trackRange.Flush(StopTracking::Yes);
    const EditorDOMPoint atRemovedText =
        caretPointOrError.unwrap().UnwrapCaretPoint();
    if (NS_WARN_IF(!atRemovedText.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return DeleteRangeResult(EditorDOMRange(atRemovedText), atRemovedText);
  }();
  if (MOZ_UNLIKELY(deleteStartTextResultOrError.isErr())) {
    return deleteStartTextResultOrError.propagateErr();
  }
  DeleteRangeResult deleteStartTextResult =
      deleteStartTextResultOrError.unwrap();

  // If the range ends in a text node and starts from a different node, delete
  // the text before the end boundary.
  auto deleteEndTextResultOrError =
      [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    if (!range.EndRef().IsInTextNode() || range.EndRef().IsStartOfContainer()) {
      return DeleteRangeResult::IgnoredResult();
    }
    AutoTrackDOMRange trackRange(aHTMLEditor.RangeUpdaterRef(), &range);
    AutoTrackDOMDeleteRangeResult trackDeleteStartTextResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteStartTextResult);
    const OwningNonNull<Text> textNode = *range.EndRef().ContainerAs<Text>();
    if (range.EndRef().IsEndOfContainer()) {
      Result<DeleteRangeResult, nsresult> deleteTextNodeResult =
          DeleteTextNode(textNode);
      NS_WARNING_ASSERTION(
          deleteTextNodeResult.isOk(),
          "DeleteTextNode() failed to delete the end Text node");
      return deleteTextNodeResult;
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        aHTMLEditor.DeleteTextWithTransaction(textNode, 0,
                                              range.EndRef().Offset());
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
      return caretPointOrError.propagateErr();
    }
    trackRange.Flush(StopTracking::Yes);
    const EditorDOMPoint atRemovedText =
        caretPointOrError.unwrap().UnwrapCaretPoint();
    if (NS_WARN_IF(!atRemovedText.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return DeleteRangeResult(EditorDOMRange(atRemovedText), atRemovedText);
  }();
  if (MOZ_UNLIKELY(deleteEndTextResultOrError.isErr())) {
    return deleteEndTextResultOrError.propagateErr();
  }
  DeleteRangeResult deleteEndTextResult = deleteEndTextResultOrError.unwrap();

  if (!deleteStartTextResult.Handled() && !deleteEndTextResult.Handled()) {
    deleteStartTextResult.IgnoreCaretPointSuggestion();
    deleteEndTextResult.IgnoreCaretPointSuggestion();
    return DeleteRangeResult::IgnoredResult();
  }

  EditorDOMPoint pointToPutCaret =
      aPutCaretTo == PutCaretTo::EndOfRange
          ? (deleteEndTextResult.Handled()
                 ? deleteEndTextResult.UnwrapCaretPoint()
                 : EditorDOMPoint())
          : (deleteStartTextResult.Handled()
                 ? deleteStartTextResult.UnwrapCaretPoint()
                 : EditorDOMPoint());
  deleteStartTextResult |= deleteEndTextResult;
  deleteStartTextResult.ForgetCaretPointSuggestion();
  if (pointToPutCaret.IsSet()) {
    deleteStartTextResult |= CaretPoint(std::move(pointToPutCaret));
  }
  return std::move(deleteStartTextResult);
}

// static
template <typename EditorDOMPointType>
Result<Element*, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::GetMostDistantBlockAncestorIfPointIsStartAtBlock(
        const EditorDOMPointType& aPoint, const Element& aEditingHost,
        const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  MOZ_ASSERT(aPoint.IsInComposedDoc());

  if (!aAncestorLimiter) {
    aAncestorLimiter = &aEditingHost;
  }

  const auto ReachedCurrentBlockBoundaryWhichWeCanCross =
      [&aEditingHost, aAncestorLimiter](const WSScanResult& aScanResult) {
        // When the scan result is "reached current block boundary", it may not
        // be so literally.
        return aScanResult.ReachedCurrentBlockBoundary() &&
               HTMLEditUtils::IsRemovableFromParentNode(
                   *aScanResult.ElementPtr()) &&
               aScanResult.ElementPtr() != &aEditingHost &&
               aScanResult.ElementPtr() != aAncestorLimiter &&
               // Don't cross <body>, <head> and <html>
               !aScanResult.ElementPtr()->IsAnyOfHTMLElements(
                   nsGkAtoms::body, nsGkAtoms::head, nsGkAtoms::html) &&
               // Don't cross table elements
               !HTMLEditUtils::IsAnyTableElementExceptColumnElement(
                   *aScanResult.ElementPtr());
      };

  const WSScanResult prevVisibleThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::OnlyEditableNodes}, aPoint, aAncestorLimiter);
  if (!ReachedCurrentBlockBoundaryWhichWeCanCross(prevVisibleThing)) {
    return nullptr;
  }
  MOZ_ASSERT(
      HTMLEditUtils::IsBlockElement(*prevVisibleThing.ElementPtr(),
                                    BlockInlineCheck::UseComputedDisplayStyle));
  for (Element* ancestorBlock = prevVisibleThing.ElementPtr(); ancestorBlock;) {
    const WSScanResult prevVisibleThing =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorRawDOMPoint(ancestorBlock), aAncestorLimiter);
    if (!ReachedCurrentBlockBoundaryWhichWeCanCross(prevVisibleThing)) {
      return ancestorBlock;
    }
    MOZ_ASSERT(HTMLEditUtils::IsBlockElement(
        *prevVisibleThing.ElementPtr(),
        BlockInlineCheck::UseComputedDisplayStyle));
    ancestorBlock = prevVisibleThing.ElementPtr();
  }
  return Err(NS_ERROR_FAILURE);
}

void HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ExtendRangeToDeleteNonCollapsedRange(
        const HTMLEditor& aHTMLEditor, dom::Range& aRangeToDelete,
        const Element& aEditingHost, ComputeRangeFor aComputeRangeFor) const {
  MOZ_ASSERT_IF(aComputeRangeFor == ComputeRangeFor::GetTargetRanges,
                aRangeToDelete.IsPositioned());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mLeftContent->IsElement());
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
      mLeftContent));
  MOZ_ASSERT(mRightContent);
  MOZ_ASSERT(mRightContent->IsElement());
  MOZ_ASSERT(
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));

  const DebugOnly<Result<bool, nsresult>> extendRangeResult =
      AutoDeleteRangesHandler::
          ExtendRangeToContainAncestorInlineElementsAtStart(aRangeToDelete,
                                                            aEditingHost);
  NS_WARNING_ASSERTION(extendRangeResult.value.isOk(),
                       "AutoDeleteRangesHandler::"
                       "ExtendRangeToContainAncestorInlineElementsAtStart() "
                       "failed, but ignored");
  if (mMode != Mode::DeletePrecedingLinesAndContentInRange) {
    return;
  }

  // If we're computing for beforeinput.getTargetRanges() and the inputType
  // is not a simple deletion like replacing selected content with new
  // content, the range should end at the original end boundary of the given
  // range even if we're deleting only preceding lines of the right child
  // block.
  const bool preserveEndBoundary =
      aComputeRangeFor == ComputeRangeFor::GetTargetRanges &&
      !MayEditActionDeleteAroundCollapsedSelection(aHTMLEditor.GetEditAction());
  // We need to delete only the preceding lines of the right block. Therefore,
  // we need to shrink the range to ends before the right block if the range
  // does not contain any meaningful content in the right block.
  const Result<Element*, nsresult> inclusiveAncestorCurrentBlockOrError =
      AutoBlockElementsJoiner::GetMostDistantBlockAncestorIfPointIsStartAtBlock(
          EditorRawDOMPoint(aRangeToDelete.EndRef()), aEditingHost,
          mLeftContent->AsElement());
  MOZ_ASSERT(inclusiveAncestorCurrentBlockOrError.isOk());
  MOZ_ASSERT_IF(inclusiveAncestorCurrentBlockOrError.inspect(),
                mRightContent->IsInclusiveDescendantOf(
                    inclusiveAncestorCurrentBlockOrError.inspect()));
  if (MOZ_UNLIKELY(!inclusiveAncestorCurrentBlockOrError.isOk() ||
                   !inclusiveAncestorCurrentBlockOrError.inspect())) {
    return;
  }

  const WSScanResult prevVisibleThingOfStartBoundary =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::OnlyEditableNodes},
          EditorRawDOMPoint(aRangeToDelete.StartRef()));
  // If the range starts after an invisible <br> of empty line immediately
  // before the most distant inclusive ancestor of the right block like
  // `<br><br>{<div>]abc`, we should delete the last empty line because
  // users won't see any reaction of the builtin editor in this case.
  if (prevVisibleThingOfStartBoundary.ReachedBRElement() ||
      prevVisibleThingOfStartBoundary.ReachedPreformattedLineBreak()) {
    const WSScanResult prevVisibleThingOfPreviousLineBreak =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            prevVisibleThingOfStartBoundary
                .PointAtReachedContent<EditorRawDOMPoint>());
    const WSScanResult nextVisibleThingOfPreviousBR =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            prevVisibleThingOfStartBoundary
                .PointAfterReachedContent<EditorRawDOMPoint>());
    if (prevVisibleThingOfPreviousLineBreak.ReachedLineBreak() &&
        nextVisibleThingOfPreviousBR.ReachedOtherBlockElement() &&
        nextVisibleThingOfPreviousBR.ElementPtr() ==
            inclusiveAncestorCurrentBlockOrError.inspect()) {
      aRangeToDelete.SetStart(prevVisibleThingOfStartBoundary
                                  .PointAtReachedContent<EditorRawDOMPoint>()
                                  .ToRawRangeBoundary(),
                              IgnoreErrors());
    }
  }

  if (preserveEndBoundary) {
    return;
  }

  if (aComputeRangeFor == ComputeRangeFor::GetTargetRanges) {
    // When we set the end boundary to around the right block, the new end
    // boundary should not after inline ancestors of the line break which won't
    // be deleted.
    const WSScanResult lastVisibleThingBeforeRightChildBlock =
        [&]() -> WSScanResult {
      EditorRawDOMPoint scanStartPoint(aRangeToDelete.StartRef());
      WSScanResult lastScanResult = WSScanResult::Error();
      while (true) {
        WSScanResult scanResult =
            WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                {WSRunScanner::Option::OnlyEditableNodes}, scanStartPoint,
                mLeftContent->AsElement());
        if (scanResult.ReachedBlockBoundary() ||
            scanResult.ReachedInlineEditingHostBoundary()) {
          return lastScanResult;
        }
        scanStartPoint =
            scanResult.PointAfterReachedContent<EditorRawDOMPoint>();
        lastScanResult = std::move(scanResult);
      }
    }();
    if (lastVisibleThingBeforeRightChildBlock.GetContent()) {
      const nsIContent* commonAncestor = nsIContent::FromNode(
          nsContentUtils::GetClosestCommonInclusiveAncestor(
              aRangeToDelete.StartRef().GetContainer(),
              lastVisibleThingBeforeRightChildBlock.GetContent()));
      MOZ_ASSERT(commonAncestor);
      if (commonAncestor &&
          !mRightContent->IsInclusiveDescendantOf(commonAncestor)) {
        IgnoredErrorResult error;
        aRangeToDelete.SetEnd(
            EditorRawDOMPoint::AtEndOf(*commonAncestor).ToRawRangeBoundary(),
            error);
        NS_WARNING_ASSERTION(!error.Failed(),
                             "Range::SetEnd() failed, but ignored");
        return;
      }
    }
  }

  IgnoredErrorResult error;
  aRangeToDelete.SetEnd(
      EditorRawDOMPoint(inclusiveAncestorCurrentBlockOrError.inspect())
          .ToRawRangeBoundary(),
      error);
  NS_WARNING_ASSERTION(!error.Failed(), "Range::SetEnd() failed, but ignored");
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    ComputeRangeToDeleteNonCollapsedRange(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection /*aDirectionAndAmount*/,
        dom::Range& aRangeToDelete,
        AutoDeleteRangesHandler::SelectionWasCollapsed aSelectionWasCollapsed,
        const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mLeftContent);
  MOZ_ASSERT(mLeftContent->IsElement());
  MOZ_ASSERT(aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
      mLeftContent));
  MOZ_ASSERT(mRightContent);
  MOZ_ASSERT(mRightContent->IsElement());
  MOZ_ASSERT(
      aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(mRightContent));

  ExtendRangeToDeleteNonCollapsedRange(aHTMLEditor, aRangeToDelete,
                                       aEditingHost,
                                       ComputeRangeFor::GetTargetRanges);

  Result<bool, nsresult> result =
      ComputeRangeToDeleteNodesEntirelyInRangeButKeepTableStructure(
          aHTMLEditor, aRangeToDelete, aSelectionWasCollapsed);
  if (result.isErr()) {
    NS_WARNING(
        "AutoBlockElementsJoiner::"
        "ComputeRangeToDeleteNodesEntirelyInRangeButKeepTableStructure() "
        "failed");
    return result.unwrapErr();
  }
  if (!result.unwrap()) {
    return NS_OK;
  }

  AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                  *mRightContent);
  Result<bool, nsresult> canJoinThem =
      joiner.Prepare(aHTMLEditor, aEditingHost);
  if (canJoinThem.isErr()) {
    NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
    return canJoinThem.unwrapErr();
  }

  if (!canJoinThem.unwrap()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  if (!joiner.CanJoinBlocks()) {
    return NS_OK;
  }

  nsresult rv = joiner.ComputeRangeToDelete(aHTMLEditor, EditorDOMPoint(),
                                            aRangeToDelete, aEditingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "AutoInclusiveAncestorBlockElementsJoiner::ComputeRangeToDelete() "
      "failed");

  // FIXME: If we'll delete unnecessary following <br>, we need to include it
  // into aRangesToDelete.

  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::HandleDeleteNonCollapsedRange(
        HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
        nsIEditor::EStripWrappers /*aStripWrappers*/,
        dom::Range& aRangeToDelete,
        AutoDeleteRangesHandler::SelectionWasCollapsed aSelectionWasCollapsed,
        const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(mDeleteRangesHandler);

  const bool isDeletingLineBreak =
      mMode == Mode::DeleteBRElement ||
      mMode == Mode::DeletePrecedingBRElementOfBlock ||
      mMode == Mode::DeletePrecedingPreformattedLineBreak;
  if (!isDeletingLineBreak) {
    MOZ_ASSERT(aRangeToDelete.GetStartContainer()->IsInclusiveDescendantOf(
        mLeftContent));
    MOZ_ASSERT(aRangeToDelete.GetEndContainer()->IsInclusiveDescendantOf(
        mRightContent));
    ExtendRangeToDeleteNonCollapsedRange(aHTMLEditor, aRangeToDelete,
                                         aEditingHost,
                                         ComputeRangeFor::ToDeleteTheRange);
  }

  const bool backspaceInRightBlock =
      aSelectionWasCollapsed == SelectionWasCollapsed::Yes &&
      nsIEditor::DirectionIsBackspace(aDirectionAndAmount);

  const bool maybeDeleteOnlyFollowingContentOfFollowingBlockBoundary =
      !isDeletingLineBreak &&
      mMode != Mode::DeletePrecedingLinesAndContentInRange &&
      HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
          EditorRawDOMPoint(aRangeToDelete.StartRef()),
          HTMLEditUtils::IgnoreInvisibleLineBreak::Yes);
  const PutCaretTo putCaretTo = [&]() {
    // When we delete only preceding lines of the right child block, we should
    // put caret into start of the right block.
    if (mMode == Mode::DeletePrecedingLinesAndContentInRange) {
      return PutCaretTo::EndOfRange;
    }
    // If we're joining blocks: if deleting forward the selection should be
    // collapsed to the end of the selection, if deleting backward the selection
    // should be collapsed to the beginning of the selection.
    if (NeedsToJoinNodesAfterDeleteNodesEntirelyInRange()) {
      return nsIEditor::DirectionIsDelete(aDirectionAndAmount)
                 ? PutCaretTo::EndOfRange
                 : PutCaretTo::StartOfRange;
    }
    // But if we're not joining then the selection should collapse to the
    // beginning of the selection if we're deleting forward, because the end of
    // the selection will still be in the next block. And same thing for
    // deleting backwards (selection should collapse to the end, because the
    // beginning will still be in the first block). See Bug 507936.
    return nsIEditor::DirectionIsDelete(aDirectionAndAmount)
               ? PutCaretTo::StartOfRange
               : PutCaretTo::EndOfRange;
  }();

  auto deleteContentResultOrError =
      [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    AutoTArray<OwningNonNull<nsIContent>, 10> arrayOfTopChildren;
    {
      DOMSubtreeIterator iter;
      nsresult rv = iter.Init(aRangeToDelete);
      if (NS_FAILED(rv)) {
        NS_WARNING("DOMSubtreeIterator::Init() failed");
        return Err(rv);
      }
      iter.AppendAllNodesToArray(arrayOfTopChildren);
    }

    OwningNonNull<dom::Range> rangeToDelete(aRangeToDelete);
    AutoTrackDOMRange trackRangeToDelete(aHTMLEditor.RangeUpdaterRef(),
                                         &rangeToDelete);

    // First, delete nodes which are entirely selected except table structure
    // elements like <td>, <th>, <caption>.
    Result<DeleteRangeResult, nsresult> deleteResultOrError =
        DeleteNodesEntirelyInRangeButKeepTableStructure(
            aHTMLEditor, arrayOfTopChildren, putCaretTo);
    if (MOZ_UNLIKELY(deleteResultOrError.isErr())) {
      NS_WARNING(
          "AutoBlockElementsJoiner::"
          "DeleteNodesEntirelyInRangeButKeepTableStructure() failed");
      return deleteResultOrError.propagateErr();
    }
    DeleteRangeResult deleteResult = deleteResultOrError.unwrap();
    // We'll compute caret position below, so, we don't need the caret point
    // suggestion of DeleteNodesEntirelyInRangeButKeepTableStructure().
    deleteResult.ForgetCaretPointSuggestion();

    // Check endpoints for possible text deletion.  We can assume that if
    // text node is found, we can delete to end or to beginning as
    // appropriate, since the case where both sel endpoints in same text
    // node was already handled (we wouldn't be here)
    AutoTrackDOMDeleteRangeResult trackDeleteResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteResult);
    Result<DeleteRangeResult, nsresult> deleteSurroundingTextResultOrError =
        DeleteTextAtStartAndEndOfRange(aHTMLEditor, rangeToDelete, putCaretTo);
    if (MOZ_UNLIKELY(deleteSurroundingTextResultOrError.isErr())) {
      NS_WARNING(
          "AutoBlockElementsJoiner::DeleteTextAtStartAndEndOfRange() failed");
      return deleteSurroundingTextResultOrError.propagateErr();
    }
    trackDeleteResult.Flush(StopTracking::Yes);
    trackRangeToDelete.Flush(StopTracking::Yes);

    DeleteRangeResult deleteSurroundingTextResult =
        deleteSurroundingTextResultOrError.unwrap();
    // We'll compute caret position below, so, we don't need the caret point
    // suggestion of DeleteTextAtStartAndEndOfRange().
    deleteSurroundingTextResult.ForgetCaretPointSuggestion();

    // Merge the deleted range.
    deleteResult |= deleteSurroundingTextResult;

    if (mRightContent && mMode == Mode::DeletePrecedingLinesAndContentInRange) {
      if (NS_WARN_IF(!mRightContent->IsInComposedDoc())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      auto pointToPutCaret =
          HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
              *mRightContent, {});
      MOZ_ASSERT(pointToPutCaret.IsSet());
      deleteResult |= CaretPoint(std::move(pointToPutCaret));
    }
    return std::move(deleteResult);
  }();
  if (MOZ_UNLIKELY(deleteContentResultOrError.isErr())) {
    return deleteContentResultOrError.propagateErr();
  }
  DeleteRangeResult deleteContentResult = deleteContentResultOrError.unwrap();
  // HandleDeleteLineBreak() should handle the new caret position by itself.
  if (isDeletingLineBreak) {
    MOZ_ASSERT(!NeedsToJoinNodesAfterDeleteNodesEntirelyInRange());
    deleteContentResult.IgnoreCaretPointSuggestion();
    return EditActionResult::HandledResult();
  }

  auto moveFirstLineResultOrError =
      [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    if (!NeedsToJoinNodesAfterDeleteNodesEntirelyInRange()) {
      return DeleteRangeResult::IgnoredResult();
    }

    MOZ_ASSERT(mLeftContent);
    MOZ_ASSERT(mLeftContent->IsElement());
    MOZ_ASSERT(mRightContent);
    MOZ_ASSERT(mRightContent->IsElement());

    // Finally, join elements containing either mLeftContent or mRightContent.
    // XXX This may join only inline elements despite its name.
    AutoInclusiveAncestorBlockElementsJoiner joiner(*mLeftContent,
                                                    *mRightContent);
    Result<bool, nsresult> canJoinThem =
        joiner.Prepare(aHTMLEditor, aEditingHost);
    if (canJoinThem.isErr()) {
      NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Prepare() failed");
      return canJoinThem.propagateErr();
    }

    if (!canJoinThem.inspect() || !joiner.CanJoinBlocks()) {
      return DeleteRangeResult::IgnoredResult();
    }

    OwningNonNull<dom::Range> rangeToDelete(aRangeToDelete);
    AutoTrackDOMRange trackRangeToDelete(aHTMLEditor.RangeUpdaterRef(),
                                         &rangeToDelete);
    AutoTrackDOMDeleteRangeResult trackDeleteContentResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteContentResult);
    Result<DeleteRangeResult, nsresult> moveFirstLineResultOrError =
        joiner.Run(aHTMLEditor, aEditingHost);
    if (MOZ_UNLIKELY(moveFirstLineResultOrError.isErr())) {
      NS_WARNING("AutoInclusiveAncestorBlockElementsJoiner::Run() failed");
      return moveFirstLineResultOrError.propagateErr();
    }
    trackDeleteContentResult.Flush(StopTracking::Yes);
    trackRangeToDelete.Flush(StopTracking::Yes);
    DeleteRangeResult moveFirstLineResult = moveFirstLineResultOrError.unwrap();
#ifdef DEBUG
    if (joiner.ShouldDeleteLeafContentInstead()) {
      NS_ASSERTION(moveFirstLineResult.Ignored(),
                   "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                   "returning ignored, but returned not ignored");
    } else {
      NS_ASSERTION(!moveFirstLineResult.Ignored(),
                   "Assumed `AutoInclusiveAncestorBlockElementsJoiner::Run()` "
                   "returning handled, but returned ignored");
    }
#endif  // #ifdef DEBUG
    return std::move(moveFirstLineResult);
  }();
  if (MOZ_UNLIKELY(moveFirstLineResultOrError.isErr())) {
    deleteContentResult.IgnoreCaretPointSuggestion();
    return moveFirstLineResultOrError.propagateErr();
  }
  DeleteRangeResult moveFirstLineResult = moveFirstLineResultOrError.unwrap();

  auto candidatePointToPutCaret = [&]()
                                      MOZ_NEVER_INLINE_DEBUG -> EditorDOMPoint {
    if (moveFirstLineResult.HasCaretPointSuggestion()) {
      MOZ_ASSERT(moveFirstLineResult.Handled());
      if (MayEditActionDeleteAroundCollapsedSelection(
              aHTMLEditor.GetEditAction())) {
        deleteContentResult.IgnoreCaretPointSuggestion();
        // If we're deleting selection (not replacing with new content) and
        // AutoInclusiveAncestorBlockElementsJoiner computed new caret position,
        // we should use it.
        return moveFirstLineResult.UnwrapCaretPoint();
      }
      moveFirstLineResult.IgnoreCaretPointSuggestion();
    }
    if (deleteContentResult.HasCaretPointSuggestion()) {
      return deleteContentResult.UnwrapCaretPoint();
    }
    return EditorDOMPoint(putCaretTo == PutCaretTo::StartOfRange
                              ? aRangeToDelete.StartRef()
                              : aRangeToDelete.EndRef());
  }();
  MOZ_ASSERT(candidatePointToPutCaret.IsSetAndValidInComposedDoc());

  auto pointToPutCaretOrError = [&]() MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<EditorDOMPoint, nsresult> {
    // While we're touching the DOM, we need to track the following things, but
    // the number of them are too many from the tracker style to write them for
    // each. Therefore, these trackers cover all over this lambda and when this
    // touches the DOM, these are flushed without stop tracking.
    AutoTrackDOMDeleteRangeResult trackDeleteContentResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteContentResult);
    AutoTrackDOMDeleteRangeResult trackMoveFirstLineResult(
        aHTMLEditor.RangeUpdaterRef(), &moveFirstLineResult);
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &candidatePointToPutCaret);
    const auto FlushTrackersAndKeepTracking = [&]() {
      trackDeleteContentResult.Flush(StopTracking::No);
      trackMoveFirstLineResult.Flush(StopTracking::No);
      trackPointToPutCaret.Flush(StopTracking::No);
    };

    // First, delete unnecessary nodes such as empty `Text` in the range.
    nsresult rv = mDeleteRangesHandler->DeleteUnnecessaryNodes(
        aHTMLEditor, EditorDOMRange(aRangeToDelete), aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoDeleteRangesHandler::DeleteUnnecessaryNodes() failed");
      return Err(rv);
    }
    FlushTrackersAndKeepTracking();
    if (NS_WARN_IF(!candidatePointToPutCaret.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // If we're an email editor, we need to delete current mailcite if we're in
    // an empty one.
    if (aHTMLEditor.IsMailEditor() &&
        MOZ_LIKELY(candidatePointToPutCaret.IsInContentNode())) {
      nsresult rv = aHTMLEditor.DeleteMostAncestorMailCiteElementIfEmpty(
          MOZ_KnownLive(*candidatePointToPutCaret.ContainerAs<nsIContent>()));
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty() "
            "failed");
        return Err(rv);
      }
      FlushTrackersAndKeepTracking();
      if (NS_WARN_IF(!candidatePointToPutCaret.IsSetAndValidInComposedDoc())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }

    const auto EnsureNoFollowingUnnecessaryLineBreak =
        [&](const EditorDOMPoint& aPoint)
            MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT {
              if (!aPoint.IsInContentNode()) {
                return NS_OK;
              }
              nsresult rv = aHTMLEditor.EnsureNoFollowingUnnecessaryLineBreak(
                  aPoint, PreservePreformattedLineBreak::No,
                  PaddingForEmptyBlock::Significant, aEditingHost);
              NS_WARNING_ASSERTION(
                  NS_SUCCEEDED(rv),
                  "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
              FlushTrackersAndKeepTracking();
              return rv;
            };

    const auto InsertPaddingBRElementIfNeeded =
        [&](const EditorDOMPoint& aPoint)
            MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
        -> Result<CaretPoint, nsresult> {
      if (!aPoint.IsInContentNode()) {
        return CaretPoint(EditorDOMPoint());
      }
      const bool insertingAtCaretPoint = aPoint == candidatePointToPutCaret;
      if (insertingAtCaretPoint && aHTMLEditor.GetTopLevelEditSubAction() !=
                                       EditSubAction::eDeleteSelectedContent) {
        return CaretPoint(EditorDOMPoint());
      }
      if (!insertingAtCaretPoint &&
          mMode == Mode::DeletePrecedingLinesAndContentInRange) {
        return CaretPoint(EditorDOMPoint());
      }
      Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
          aHTMLEditor.InsertPaddingBRElementIfNeeded(
              aPoint,
              aEditingHost.IsContentEditablePlainTextOnly()
                  ? nsIEditor::eNoStrip
                  : nsIEditor::eStrip,
              aEditingHost);
      if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
        NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
        return insertPaddingBRElementOrError.propagateErr();
      }
      FlushTrackersAndKeepTracking();
      CreateLineBreakResult insertPaddingBRElement =
          insertPaddingBRElementOrError.unwrap();
      if (!insertPaddingBRElement.Handled() || !insertingAtCaretPoint) {
        insertPaddingBRElement.IgnoreCaretPointSuggestion();
        return CaretPoint(EditorDOMPoint());
      }
      return CaretPoint(insertPaddingBRElement.UnwrapCaretPoint());
    };

    // Finally, if we moved content from the right element to the left element,
    // we need to maintain padding line break at end of moved content.
    if (moveFirstLineResult.Handled() &&
        moveFirstLineResult.DeleteRangeRef().IsPositioned()) {
      nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
          moveFirstLineResult.DeleteRangeRef().EndRef());
      if (NS_FAILED(rv)) {
        NS_WARNING("EnsureNoFollowingUnnecessaryLineBreak() failed");
        return Err(rv);
      }
      // If we moved a child block of the first line (although this is
      // logically wrong...), we should not put a <br> after that.
      Element* const commonAncestor =
          Element::FromNodeOrNull(moveFirstLineResult.DeleteRangeRef()
                                      .GetClosestCommonInclusiveAncestor());
      nsIContent* const previousVisibleLeafOrChildBlock =
          HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
              moveFirstLineResult.DeleteRangeRef().EndRef(),
              {LeafNodeOption::TreatChildBlockAsLeafNode,
               LeafNodeOption::IgnoreInvisibleEmptyInlineContainers,
               LeafNodeOption::IgnoreInvisibleText},
              BlockInlineCheck::UseComputedDisplayOutsideStyle, commonAncestor);
      if (!previousVisibleLeafOrChildBlock) {
        // No visible thing before in the common ancestor.
        return candidatePointToPutCaret;
      }
      if (MOZ_UNLIKELY(
              HTMLEditUtils::IsBlockElement(
                  *previousVisibleLeafOrChildBlock,
                  BlockInlineCheck::UseComputedDisplayOutsideStyle) &&
              moveFirstLineResult.DeleteRangeRef().StartRef().EqualsOrIsBefore(
                  EditorRawDOMPoint::After(
                      *previousVisibleLeafOrChildBlock)))) {
        // We moved a child block at last. We don't put a <br> after that to
        // avoid to make an empty line after it.
        return candidatePointToPutCaret;
      }
      Result<CaretPoint, nsresult> caretPointOrError =
          InsertPaddingBRElementIfNeeded(
              moveFirstLineResult.DeleteRangeRef().EndRef());
      if (NS_WARN_IF(caretPointOrError.isErr())) {
        return caretPointOrError.propagateErr();
      }
      EditorDOMPoint pointToPutCaret = candidatePointToPutCaret;
      caretPointOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      return std::move(pointToPutCaret);
    }

    if (!deleteContentResult.DeleteRangeRef().IsPositioned()) {
      return candidatePointToPutCaret;
    }

    // Or if we only deleted content in the range, we need to maintain padding
    // line breaks at both deleted range boundaries.
    if (!deleteContentResult.DeleteRangeRef().Collapsed()) {
      nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
          deleteContentResult.DeleteRangeRef().EndRef());
      if (NS_FAILED(rv)) {
        NS_WARNING("EnsureNoFollowingUnnecessaryLineBreak() failed");
        return Err(rv);
      }
      // If we deleted blocks following current block, we should not
      // insert padding line break after current block when we're handling
      // Backspace.
      const bool isFollowingBlockDeletedByBackspace =
          [&]() MOZ_NEVER_INLINE_DEBUG {
            if (putCaretTo == PutCaretTo::EndOfRange) {
              return false;
            }
            if (!HTMLEditUtils::RangeIsAcrossStartBlockBoundary(
                    deleteContentResult.DeleteRangeRef(),
                    // XXX UseComputedDisplayStyle?
                    BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
              return false;
            }
            const WSScanResult nextThing =
                WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
                    {WSRunScanner::Option::OnlyEditableNodes},
                    deleteContentResult.DeleteRangeRef().EndRef());
            return nextThing.ReachedLineBoundary();
          }();
      if (!isFollowingBlockDeletedByBackspace) {
        Result<CaretPoint, nsresult> caretPointOrError =
            InsertPaddingBRElementIfNeeded(
                deleteContentResult.DeleteRangeRef().EndRef());
        if (NS_WARN_IF(caretPointOrError.isErr())) {
          return caretPointOrError.propagateErr();
        }
        CaretPoint caretPoint = caretPointOrError.unwrap();
        if (caretPoint.HasCaretPointSuggestion() &&
            caretPoint.CaretPointRef() != candidatePointToPutCaret) {
          caretPoint.MoveCaretPointTo(candidatePointToPutCaret,
                                      {SuggestCaret::OnlyIfHasSuggestion});
          trackPointToPutCaret.RestartToTrack();
        }
      }
    }
    // If we deleted content only after current block, we don't need to
    // maintain line breaks at start of the deleted range because nothing
    // has been changed from the caret point of view.
    if (maybeDeleteOnlyFollowingContentOfFollowingBlockBoundary) {
      return candidatePointToPutCaret;
    }
    rv = EnsureNoFollowingUnnecessaryLineBreak(
        deleteContentResult.DeleteRangeRef().StartRef());
    if (NS_FAILED(rv)) {
      NS_WARNING("EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        InsertPaddingBRElementIfNeeded(
            deleteContentResult.DeleteRangeRef().StartRef());
    if (NS_WARN_IF(caretPointOrError.isErr())) {
      return caretPointOrError.propagateErr();
    }
    EditorDOMPoint pointToPutCaret = candidatePointToPutCaret;
    caretPointOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    return std::move(pointToPutCaret);
  }();
  if (NS_WARN_IF(pointToPutCaretOrError.isErr())) {
    return pointToPutCaretOrError.propagateErr();
  }

  EditorDOMPoint pointToPutCaret = pointToPutCaretOrError.unwrap();
  nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionTo() failed");
    return Err(rv);
  }
  if (mMode == Mode::DeletePrecedingLinesAndContentInRange ||
      moveFirstLineResult.Handled()) {
    // If we prefer to use style in the previous line, we should forget previous
    // styles since the caret position has all styles which we want to use with
    // new content.
    if (backspaceInRightBlock) {
      aHTMLEditor.TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
    }
    // And we don't want to keep extending a link at ex-end of the previous
    // paragraph.
    if (HTMLEditor::GetLinkElement(pointToPutCaret.GetContainer())) {
      aHTMLEditor.mPendingStylesToApplyToNewContent
          ->ClearLinkAndItsSpecifiedStyle();
    }
  }
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::AutoDeleteRangesHandler::DeleteUnnecessaryNodes(
    HTMLEditor& aHTMLEditor, const EditorDOMRange& aRange,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(EditorUtils::IsEditableContent(
      *aRange.StartRef().ContainerAs<nsIContent>(), EditorType::HTML));
  MOZ_ASSERT(EditorUtils::IsEditableContent(
      *aRange.EndRef().ContainerAs<nsIContent>(), EditorType::HTML));

  EditorDOMRange range(aRange);

  // If we're handling DnD, this is called to delete dragging item from the
  // tree.  In this case, we should remove parent blocks if it becomes empty.
  if (aHTMLEditor.GetEditAction() == EditAction::eDrop ||
      aHTMLEditor.GetEditAction() == EditAction::eDeleteByDrag) {
    MOZ_ASSERT(range.Collapsed() ||
               (range.StartRef().GetContainer()->GetNextSibling() ==
                    range.EndRef().GetContainer() &&
                range.StartRef().IsEndOfContainer() &&
                range.EndRef().IsStartOfContainer()));
    AutoTrackDOMRange trackRange(aHTMLEditor.RangeUpdaterRef(), &range);

    nsresult rv = DeleteParentBlocksWithTransactionIfEmpty(
        aHTMLEditor, range.StartRef(), aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::DeleteParentBlocksWithTransactionIfEmpty() failed");
      return rv;
    }
    aHTMLEditor.TopLevelEditSubActionDataRef().mDidDeleteEmptyParentBlocks =
        rv == NS_OK;
    // If we removed parent blocks, Selection should be collapsed at where
    // the most ancestor empty block has been.
    if (aHTMLEditor.TopLevelEditSubActionDataRef()
            .mDidDeleteEmptyParentBlocks) {
      return NS_OK;
    }
  }

  if (NS_WARN_IF(!range.IsInContentNodes()) ||
      NS_WARN_IF(!EditorUtils::IsEditableContent(
          *range.StartRef().ContainerAs<nsIContent>(), EditorType::HTML)) ||
      NS_WARN_IF(!EditorUtils::IsEditableContent(
          *range.EndRef().ContainerAs<nsIContent>(), EditorType::HTML))) {
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }

  // We might have left only collapsed white-space in the start/end nodes
  {
    AutoTrackDOMRange trackRange(aHTMLEditor.RangeUpdaterRef(), &range);

    OwningNonNull<nsIContent> startContainer =
        *range.StartRef().ContainerAs<nsIContent>();
    OwningNonNull<nsIContent> endContainer =
        *range.EndRef().ContainerAs<nsIContent>();
    nsresult rv =
        DeleteNodeIfInvisibleAndEditableTextNode(aHTMLEditor, startContainer);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoDeleteRangesHandler::DeleteNodeIfInvisibleAndEditableTextNode() "
        "failed to remove start node, but ignored");
    // If we've not handled the selection end container, and it's still
    // editable, let's handle it.
    if (!range.InSameContainer() &&
        EditorUtils::IsEditableContent(
            *range.EndRef().ContainerAs<nsIContent>(), EditorType::HTML)) {
      rv = DeleteNodeIfInvisibleAndEditableTextNode(aHTMLEditor, endContainer);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "AutoDeleteRangesHandler::DeleteNodeIfInvisibleAndEditableTextNode() "
          "failed to remove end node, but ignored");
    }
  }

  if (NS_WARN_IF(!range.IsPositioned())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (MOZ_LIKELY(range.EndRef().IsInContentNode())) {
    AutoTrackDOMRange trackRange(aHTMLEditor.RangeUpdaterRef(), &range);
    nsresult rv = aHTMLEditor.EnsureNoFollowingUnnecessaryLineBreak(
        range.EndRef(), PreservePreformattedLineBreak::No,
        PaddingForEmptyBlock::Significant, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
  }
  if (NS_WARN_IF(!range.IsPositioned())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  return NS_OK;
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::DeleteNodeIfInvisibleAndEditableTextNode(
    HTMLEditor& aHTMLEditor, nsIContent& aContent) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  Text* text = aContent.GetAsText();
  if (!text) {
    return NS_OK;
  }

  if (!HTMLEditUtils::IsRemovableFromParentNode(*text) ||
      HTMLEditUtils::IsVisibleTextNode(*text,
                                       TreatInvisibleLineBreakAs::Invisible)) {
    return NS_OK;
  }

  nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(aContent);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteNodeWithTransaction() failed");
  return rv;
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::DeleteParentBlocksWithTransactionIfEmpty(
    HTMLEditor& aHTMLEditor, const EditorDOMPoint& aPoint,
    const Element& aEditingHost) {
  MOZ_ASSERT(aPoint.IsSet());
  MOZ_ASSERT(aHTMLEditor.mPlaceholderBatch);

  // First, check there is visible contents before the point in current block.
  const WSScanResult prevVisibleThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary({}, aPoint,
                                                           &aEditingHost);
  if (!prevVisibleThing.ReachedCurrentBlockBoundary() &&
      !prevVisibleThing.ReachedInlineEditingHostBoundary()) {
    // If there is visible node before the point, we shouldn't remove the
    // parent block.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  MOZ_ASSERT(prevVisibleThing.ElementPtr());
  if (&aEditingHost == prevVisibleThing.ElementPtr() ||
      HTMLEditUtils::IsRemovableFromParentNode(
          *prevVisibleThing.ElementPtr())) {
    // If we reach editing host, there is no parent blocks which can be removed.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  if (HTMLEditUtils::IsTableCellOrCaptionElement(
          *prevVisibleThing.ElementPtr())) {
    // If we reach a <td>, <th> or <caption>, we shouldn't remove it even
    // becomes empty because removing such element changes the structure of
    // the <table>.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  // Next, check there is visible contents after the point in current block.
  const WSScanResult nextVisibleThing =
      HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
          aPoint,
          TreatAsUnnecessaryLineBreak{LineBreakFor::PaddingForEmptyBlock},
          aEditingHost, &aEditingHost);
  if (MOZ_UNLIKELY(nextVisibleThing.Failed())) {
    NS_WARNING(
        "HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak("
        ") failed");
    return NS_ERROR_FAILURE;
  }
  // If there is a significant line break, we shouldn't remove the parent block.
  if (nextVisibleThing.ReachedLineBreak()) {
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }
  if (!nextVisibleThing.ReachedCurrentBlockBoundary() &&
      !nextVisibleThing.ReachedInlineEditingHostBoundary()) {
    // If we couldn't reach the block's end after the invisible line break,
    // that means that there is visible content.
    return NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND;
  }

  // Delete the parent block.
  const nsCOMPtr<nsIContent> nextSibling =
      prevVisibleThing.ElementPtr()->GetNextSibling();
  const nsCOMPtr<nsINode> parentNode =
      prevVisibleThing.ElementPtr()->GetParentNode();
  nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(
      // MOZ_KnownLive because of grabbed by prevVisibleThing.
      MOZ_KnownLive(*prevVisibleThing.ElementPtr()));
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return rv;
  }
  // If we reach editing host, return NS_OK.
  if (parentNode == &aEditingHost) {
    return NS_OK;
  }

  // Otherwise, we need to check whether we're still in empty block or not.

  // If the mutations in the document is observed by DevTools, the next point
  // may be now outside of editing host or editing hos has been changed.
  if (aHTMLEditor.MaybeNodeRemovalsObservedByDevTools()) {
    if (NS_WARN_IF(nextSibling &&
                   !nextSibling->IsInclusiveDescendantOf(&aEditingHost)) ||
        NS_WARN_IF(!parentNode->IsInclusiveDescendantOf(&aEditingHost))) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    Element* newEditingHost = aHTMLEditor.ComputeEditingHost();
    if (NS_WARN_IF(!newEditingHost) ||
        NS_WARN_IF(newEditingHost != &aEditingHost)) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    if (NS_WARN_IF(
            !EditorUtils::IsDescendantOf(*parentNode, *newEditingHost))) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
  }

  const EditorDOMPoint nextPoint = nextSibling
                                       ? EditorDOMPoint(nextSibling)
                                       : EditorDOMPoint::AtEndOf(parentNode);
  rv = DeleteParentBlocksWithTransactionIfEmpty(aHTMLEditor, nextPoint,
                                                aEditingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoDeleteRangesHandler::"
                       "DeleteParentBlocksWithTransactionIfEmpty() failed");
  return rv;
}

nsresult
HTMLEditor::AutoDeleteRangesHandler::ComputeRangeToDeleteRangeWithTransaction(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    dom::Range& aRangeToDelete, const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  const EditorBase::HowToHandleCollapsedRange howToHandleCollapsedRange =
      EditorBase::HowToHandleCollapsedRangeFor(aDirectionAndAmount);
  if (MOZ_UNLIKELY(aRangeToDelete.Collapsed() &&
                   howToHandleCollapsedRange ==
                       EditorBase::HowToHandleCollapsedRange::Ignore)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  // If it's not collapsed, `DeleteRangeTransaction::Create()` will be called
  // with it and `DeleteRangeTransaction` won't modify the range.
  if (!aRangeToDelete.Collapsed()) {
    return NS_OK;
  }

  const auto ExtendRangeToSelectCharacterForward =
      [](dom::Range& aRange,
         const EditorRawDOMPointInText& aCaretPoint) -> void {
    const CharacterDataBuffer& characterDataBuffer =
        aCaretPoint.ContainerAs<Text>()->DataBuffer();
    if (!characterDataBuffer.GetLength()) {
      return;
    }
    if (characterDataBuffer.IsHighSurrogateFollowedByLowSurrogateAt(
            aCaretPoint.Offset())) {
      DebugOnly<nsresult> rvIgnored = aRange.SetStartAndEnd(
          aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset(),
          aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset() + 2);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Range::SetStartAndEnd() failed");
      return;
    }
    DebugOnly<nsresult> rvIgnored = aRange.SetStartAndEnd(
        aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset(),
        aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset() + 1);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Range::SetStartAndEnd() failed");
  };
  const auto ExtendRangeToSelectCharacterBackward =
      [](dom::Range& aRange,
         const EditorRawDOMPointInText& aCaretPoint) -> void {
    if (aCaretPoint.IsStartOfContainer()) {
      return;
    }
    const CharacterDataBuffer& characterDataBuffer =
        aCaretPoint.ContainerAs<Text>()->DataBuffer();
    if (!characterDataBuffer.GetLength()) {
      return;
    }
    if (characterDataBuffer.IsLowSurrogateFollowingHighSurrogateAt(
            aCaretPoint.Offset() - 1)) {
      DebugOnly<nsresult> rvIgnored = aRange.SetStartAndEnd(
          aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset() - 2,
          aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Range::SetStartAndEnd() failed");
      return;
    }
    DebugOnly<nsresult> rvIgnored = aRange.SetStartAndEnd(
        aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset() - 1,
        aCaretPoint.ContainerAs<Text>(), aCaretPoint.Offset());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Range::SetStartAndEnd() failed");
  };

  // In the other cases, `EditorBase::CreateTransactionForCollapsedRange()`
  // will handle the collapsed range.
  EditorRawDOMPoint caretPoint(aRangeToDelete.StartRef());
  if (howToHandleCollapsedRange ==
          EditorBase::HowToHandleCollapsedRange::ExtendBackward &&
      caretPoint.IsStartOfContainer() && caretPoint.IsInContentNode()) {
    nsIContent* previousEditableContent = HTMLEditUtils::GetPreviousLeafContent(
        *caretPoint.ContainerAs<nsIContent>(),
        {LeafNodeOption::IgnoreNonEditableNode}, BlockInlineCheck::Auto,
        &aEditingHost);
    if (!previousEditableContent) {
      return NS_OK;
    }
    if (!previousEditableContent->IsText()) {
      IgnoredErrorResult ignoredError;
      aRangeToDelete.SelectNode(*previousEditableContent, ignoredError);
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "Range::SelectNode() failed");
      return NS_OK;
    }

    ExtendRangeToSelectCharacterBackward(
        aRangeToDelete,
        EditorRawDOMPointInText::AtEndOf(*previousEditableContent->AsText()));
    return NS_OK;
  }

  if (howToHandleCollapsedRange ==
          EditorBase::HowToHandleCollapsedRange::ExtendForward &&
      caretPoint.IsEndOfContainer() && caretPoint.IsInContentNode()) {
    nsIContent* nextEditableContent = HTMLEditUtils::GetNextLeafContent(
        *caretPoint.ContainerAs<nsIContent>(),
        {LeafNodeOption::IgnoreNonEditableNode}, BlockInlineCheck::Auto,
        &aEditingHost);
    if (!nextEditableContent) {
      return NS_OK;
    }

    if (!nextEditableContent->IsText()) {
      IgnoredErrorResult ignoredError;
      aRangeToDelete.SelectNode(*nextEditableContent, ignoredError);
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "Range::SelectNode() failed");
      return NS_OK;
    }

    ExtendRangeToSelectCharacterForward(
        aRangeToDelete,
        EditorRawDOMPointInText(nextEditableContent->AsText(), 0));
    return NS_OK;
  }

  if (caretPoint.IsInTextNode()) {
    if (howToHandleCollapsedRange ==
        EditorBase::HowToHandleCollapsedRange::ExtendBackward) {
      ExtendRangeToSelectCharacterBackward(
          aRangeToDelete,
          EditorRawDOMPointInText(caretPoint.ContainerAs<Text>(),
                                  caretPoint.Offset()));
      return NS_OK;
    }
    ExtendRangeToSelectCharacterForward(
        aRangeToDelete, EditorRawDOMPointInText(caretPoint.ContainerAs<Text>(),
                                                caretPoint.Offset()));
    return NS_OK;
  }

  nsIContent* editableContent =
      howToHandleCollapsedRange ==
              EditorBase::HowToHandleCollapsedRange::ExtendBackward
          ? HTMLEditUtils::GetPreviousLeafContent(
                caretPoint, {LeafNodeOption::IgnoreNonEditableNode},
                BlockInlineCheck::Auto, &aEditingHost)
          : HTMLEditUtils::GetNextLeafContent(
                caretPoint, {LeafNodeOption::IgnoreNonEditableNode},
                BlockInlineCheck::Auto, &aEditingHost);
  if (!editableContent) {
    return NS_OK;
  }
  while (editableContent && editableContent->IsCharacterData() &&
         !editableContent->Length()) {
    editableContent =
        howToHandleCollapsedRange ==
                EditorBase::HowToHandleCollapsedRange::ExtendBackward
            ? HTMLEditUtils::GetPreviousLeafContent(
                  *editableContent, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::Auto, &aEditingHost)
            : HTMLEditUtils::GetNextLeafContent(
                  *editableContent, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::Auto, &aEditingHost);
  }
  if (!editableContent) {
    return NS_OK;
  }

  if (!editableContent->IsText()) {
    IgnoredErrorResult ignoredError;
    aRangeToDelete.SelectNode(*editableContent, ignoredError);
    NS_WARNING_ASSERTION(!ignoredError.Failed(),
                         "Range::SelectNode() failed, but ignored");
    return NS_OK;
  }

  if (howToHandleCollapsedRange ==
      EditorBase::HowToHandleCollapsedRange::ExtendBackward) {
    ExtendRangeToSelectCharacterBackward(
        aRangeToDelete,
        EditorRawDOMPointInText::AtEndOf(*editableContent->AsText()));
    return NS_OK;
  }
  ExtendRangeToSelectCharacterForward(
      aRangeToDelete, EditorRawDOMPointInText(editableContent->AsText(), 0));

  return NS_OK;
}

template <typename EditorDOMPointType>
Result<CaretPoint, nsresult> HTMLEditor::DeleteTextAndTextNodesWithTransaction(
    const EditorDOMPointType& aStartPoint, const EditorDOMPointType& aEndPoint,
    TreatEmptyTextNodes aTreatEmptyTextNodes) {
  if (NS_WARN_IF(!aStartPoint.IsSet()) || NS_WARN_IF(!aEndPoint.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  // MOOSE: this routine needs to be modified to preserve the integrity of the
  // wsFragment info.

  if (aStartPoint == aEndPoint) {
    // Nothing to delete
    return CaretPoint(EditorDOMPoint());
  }

  RefPtr<Element> editingHost = ComputeEditingHost();
  auto DeleteEmptyContentNodeWithTransaction =
      [this, &aTreatEmptyTextNodes, &editingHost](nsIContent& aContent)
          MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION -> nsresult {
    OwningNonNull<nsIContent> nodeToRemove = aContent;
    if (aTreatEmptyTextNodes ==
        TreatEmptyTextNodes::RemoveAllEmptyInlineAncestors) {
      Element* emptyParentElementToRemove =
          HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
              nodeToRemove, BlockInlineCheck::UseComputedDisplayOutsideStyle,
              editingHost);
      if (emptyParentElementToRemove) {
        nodeToRemove = *emptyParentElementToRemove;
      }
    }
    nsresult rv = DeleteNodeWithTransaction(nodeToRemove);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DeleteNodeWithTransaction() failed");
    return rv;
  };

  if (aStartPoint.GetContainer() == aEndPoint.GetContainer() &&
      aStartPoint.IsInTextNode()) {
    if (aTreatEmptyTextNodes !=
            TreatEmptyTextNodes::KeepIfContainerOfRangeBoundaries &&
        aStartPoint.IsStartOfContainer() && aEndPoint.IsEndOfContainer()) {
      nsresult rv = DeleteEmptyContentNodeWithTransaction(
          MOZ_KnownLive(*aStartPoint.template ContainerAs<Text>()));
      if (NS_FAILED(rv)) {
        NS_WARNING("deleteEmptyContentNodeWithTransaction() failed");
        return Err(rv);
      }
      return CaretPoint(EditorDOMPoint());
    }
    RefPtr<Text> textNode = aStartPoint.template ContainerAs<Text>();
    Result<CaretPoint, nsresult> caretPointOrError =
        DeleteTextWithTransaction(*textNode, aStartPoint.Offset(),
                                  aEndPoint.Offset() - aStartPoint.Offset());
    NS_WARNING_ASSERTION(caretPointOrError.isOk(),
                         "HTMLEditor::DeleteTextWithTransaction() failed");
    return caretPointOrError;
  }

  RefPtr<dom::Range> range =
      dom::Range::Create(aStartPoint.ToRawRangeBoundary(),
                         aEndPoint.ToRawRangeBoundary(), IgnoreErrors());
  if (!range) {
    NS_WARNING("Range::Create() failed");
    return Err(NS_ERROR_FAILURE);
  }

  // Collect editable text nodes in the given range.
  AutoTArray<OwningNonNull<Text>, 16> arrayOfTextNodes;
  DOMIterator iter;
  if (NS_FAILED(iter.Init(*range))) {
    return CaretPoint(EditorDOMPoint());  // Nothing to delete in the range.
  }
  iter.AppendNodesToArray(
      +[](nsINode& aNode, void*) {
        MOZ_ASSERT(aNode.IsText());
        return HTMLEditUtils::IsSimplyEditableNode(aNode);
      },
      arrayOfTextNodes);
  EditorDOMPoint pointToPutCaret;
  for (OwningNonNull<Text>& textNode : arrayOfTextNodes) {
    if (textNode == aStartPoint.GetContainer()) {
      if (aStartPoint.IsEndOfContainer()) {
        continue;
      }
      if (aStartPoint.IsStartOfContainer() &&
          aTreatEmptyTextNodes !=
              TreatEmptyTextNodes::KeepIfContainerOfRangeBoundaries) {
        AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(),
                                               &pointToPutCaret);
        nsresult rv = DeleteEmptyContentNodeWithTransaction(
            MOZ_KnownLive(*aStartPoint.template ContainerAs<Text>()));
        if (NS_FAILED(rv)) {
          NS_WARNING("DeleteEmptyContentNodeWithTransaction() failed");
          return Err(rv);
        }
        continue;
      }
      AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(),
                                             &pointToPutCaret);
      Result<CaretPoint, nsresult> caretPointOrError =
          DeleteTextWithTransaction(MOZ_KnownLive(textNode),
                                    aStartPoint.Offset(),
                                    textNode->Length() - aStartPoint.Offset());
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
        return caretPointOrError;
      }
      trackPointToPutCaret.Flush(StopTracking::Yes);
      caretPointOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      continue;
    }

    if (textNode == aEndPoint.GetContainer()) {
      if (aEndPoint.IsStartOfContainer()) {
        break;
      }
      if (aEndPoint.IsEndOfContainer() &&
          aTreatEmptyTextNodes !=
              TreatEmptyTextNodes::KeepIfContainerOfRangeBoundaries) {
        AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(),
                                               &pointToPutCaret);
        nsresult rv = DeleteEmptyContentNodeWithTransaction(
            MOZ_KnownLive(*aEndPoint.template ContainerAs<Text>()));
        if (NS_FAILED(rv)) {
          NS_WARNING("DeleteEmptyContentNodeWithTransaction() failed");
          return Err(rv);
        }
        trackPointToPutCaret.Flush(StopTracking::Yes);
        return CaretPoint(std::move(pointToPutCaret));
      }
      AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(),
                                             &pointToPutCaret);
      Result<CaretPoint, nsresult> caretPointOrError =
          DeleteTextWithTransaction(MOZ_KnownLive(textNode), 0,
                                    aEndPoint.Offset());
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
        return caretPointOrError;
      }
      trackPointToPutCaret.Flush(StopTracking::Yes);
      caretPointOrError.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      return CaretPoint(std::move(pointToPutCaret));
    }

    nsresult rv =
        DeleteEmptyContentNodeWithTransaction(MOZ_KnownLive(textNode));
    if (NS_FAILED(rv)) {
      NS_WARNING("DeleteEmptyContentNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  return CaretPoint(std::move(pointToPutCaret));
}

Result<EditorDOMPoint, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::JoinNodesDeepWithTransaction(
        HTMLEditor& aHTMLEditor, nsIContent& aLeftContent,
        nsIContent& aRightContent) {
  // While the rightmost children and their descendants of the left node match
  // the leftmost children and their descendants of the right node, join them
  // up.

  nsCOMPtr<nsIContent> leftContentToJoin = &aLeftContent;
  nsCOMPtr<nsIContent> rightContentToJoin = &aRightContent;
  nsCOMPtr<nsINode> parentNode = aRightContent.GetParentNode();

  EditorDOMPoint ret;
  while (leftContentToJoin && rightContentToJoin && parentNode &&
         HTMLEditUtils::CanContentsBeJoined(*leftContentToJoin,
                                            *rightContentToJoin)) {
    // Do the join
    Result<JoinNodesResult, nsresult> joinNodesResult =
        aHTMLEditor.JoinNodesWithTransaction(*leftContentToJoin,
                                             *rightContentToJoin);
    if (MOZ_UNLIKELY(joinNodesResult.isErr())) {
      NS_WARNING("HTMLEditor::JoinNodesWithTransaction() failed");
      return joinNodesResult.propagateErr();
    }

    ret = joinNodesResult.inspect().AtJoinedPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!ret.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }

    if (parentNode->IsText()) {
      // We've joined all the way down to text nodes, we're done!
      return ret;
    }

    // Get new left and right nodes, and begin anew
    rightContentToJoin = ret.GetCurrentChildAtOffset();
    if (rightContentToJoin) {
      leftContentToJoin = rightContentToJoin->GetPreviousSibling();
    } else {
      leftContentToJoin = nullptr;
    }

    // Skip over non-editable nodes
    while (leftContentToJoin && !EditorUtils::IsEditableContent(
                                    *leftContentToJoin, EditorType::HTML)) {
      leftContentToJoin = leftContentToJoin->GetPreviousSibling();
    }
    if (!leftContentToJoin) {
      return ret;
    }

    while (rightContentToJoin && !EditorUtils::IsEditableContent(
                                     *rightContentToJoin, EditorType::HTML)) {
      rightContentToJoin = rightContentToJoin->GetNextSibling();
    }
    if (!rightContentToJoin) {
      return ret;
    }
  }

  if (!ret.IsSet()) {
    NS_WARNING("HTMLEditor::JoinNodesDeepWithTransaction() joined no contents");
    return Err(NS_ERROR_FAILURE);
  }
  return ret;
}

Result<bool, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::AutoInclusiveAncestorBlockElementsJoiner::Prepare(
        const HTMLEditor& aHTMLEditor, const Element& aEditingHost) {
  mLeftBlockElement = HTMLEditUtils::GetInclusiveAncestorElement(
      mInclusiveDescendantOfLeftBlockElement,
      HTMLEditUtils::ClosestEditableBlockElementExceptHRElement,
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  mRightBlockElement = HTMLEditUtils::GetInclusiveAncestorElement(
      mInclusiveDescendantOfRightBlockElement,
      HTMLEditUtils::ClosestEditableBlockElementExceptHRElement,
      BlockInlineCheck::UseComputedDisplayOutsideStyle);

  if (NS_WARN_IF(!IsSet())) {
    mCanJoinBlocks = false;
    return Err(NS_ERROR_UNEXPECTED);
  }

  // Don't join the blocks if both of them are basic structure of the HTML
  // document (Note that `<body>` can be joined with its children).
  if (mLeftBlockElement->IsAnyOfHTMLElements(nsGkAtoms::html, nsGkAtoms::head,
                                             nsGkAtoms::body) &&
      mRightBlockElement->IsAnyOfHTMLElements(nsGkAtoms::html, nsGkAtoms::head,
                                              nsGkAtoms::body)) {
    mCanJoinBlocks = false;
    return false;
  }

  if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(*mLeftBlockElement) ||
      HTMLEditUtils::IsAnyTableElementExceptColumnElement(
          *mRightBlockElement)) {
    // Do not try to merge table elements, cancel the deletion.
    mCanJoinBlocks = false;
    return false;
  }

  // Bail if both blocks the same
  if (IsSameBlockElement()) {
    mCanJoinBlocks = true;  // XXX Anyway, Run() will ingore this case.
    mFallbackToDeleteLeafContent = true;
    return true;
  }

  // Joining a list item to its parent is a NOP.
  if (HTMLEditUtils::IsListElement(*mLeftBlockElement) &&
      HTMLEditUtils::IsListItemElement(*mRightBlockElement) &&
      mRightBlockElement->GetParentNode() == mLeftBlockElement) {
    mCanJoinBlocks = false;
    return true;
  }

  // Special rule here: if we are trying to join list items, and they are in
  // different lists, join the lists instead.
  if (HTMLEditUtils::IsListItemElement(*mLeftBlockElement) &&
      HTMLEditUtils::IsListItemElement(*mRightBlockElement)) {
    // XXX leftListElement and/or rightListElement may be not list elements.
    Element* leftListElement = mLeftBlockElement->GetParentElement();
    Element* rightListElement = mRightBlockElement->GetParentElement();
    EditorDOMPoint atChildInBlock;
    if (leftListElement && rightListElement &&
        leftListElement != rightListElement &&
        !EditorUtils::IsDescendantOf(*leftListElement, *mRightBlockElement,
                                     &atChildInBlock) &&
        !EditorUtils::IsDescendantOf(*rightListElement, *mLeftBlockElement,
                                     &atChildInBlock)) {
      // There are some special complications if the lists are descendants of
      // the other lists' items.  Note that it is okay for them to be
      // descendants of the other lists themselves, which is the usual case for
      // sublists in our implementation.
      MOZ_DIAGNOSTIC_ASSERT(!atChildInBlock.IsSet());
      mLeftBlockElement = leftListElement;
      mRightBlockElement = rightListElement;
      mNewListElementTagNameOfRightListElement =
          Some(leftListElement->NodeInfo()->NameAtom());
    }
  }

  if (!EditorUtils::IsDescendantOf(*mLeftBlockElement, *mRightBlockElement,
                                   &mPointContainingTheOtherBlockElement)) {
    (void)EditorUtils::IsDescendantOf(*mRightBlockElement, *mLeftBlockElement,
                                      &mPointContainingTheOtherBlockElement);
  }

  if (mPointContainingTheOtherBlockElement.GetContainer() ==
      mRightBlockElement) {
    mPrecedingInvisibleBRElement =
        WSRunScanner::GetPrecedingBRElementUnlessVisibleContentFound(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorDOMPoint::AtEndOf(mLeftBlockElement));
    // `WhiteSpaceVisibilityKeeper::
    // MergeFirstLineOfRightBlockElementIntoDescendantLeftBlockElement()`
    // returns ignored when:
    // - No preceding invisible `<br>` element and
    // - mNewListElementTagNameOfRightListElement is nothing and
    // - There is no content to move from right block element.
    if (!mPrecedingInvisibleBRElement) {
      if (CanMergeLeftAndRightBlockElements()) {
        // Always marked as handled in this case.
        mFallbackToDeleteLeafContent = false;
      } else {
        // Marked as handled only when it actually moves a content node.
        Result<bool, nsresult> firstLineHasContent =
            AutoMoveOneLineHandler::CanMoveOrDeleteSomethingInLine(
                mPointContainingTheOtherBlockElement
                    .NextPoint<EditorDOMPoint>(),
                aEditingHost);
        mFallbackToDeleteLeafContent =
            firstLineHasContent.isOk() && !firstLineHasContent.inspect();
      }
    } else {
      // Marked as handled when deleting the invisible `<br>` element.
      mFallbackToDeleteLeafContent = false;
    }
  } else if (mPointContainingTheOtherBlockElement.GetContainer() ==
             mLeftBlockElement) {
    mPrecedingInvisibleBRElement =
        WSRunScanner::GetPrecedingBRElementUnlessVisibleContentFound(
            {WSRunScanner::Option::OnlyEditableNodes},
            mPointContainingTheOtherBlockElement);
    // `WhiteSpaceVisibilityKeeper::
    // MergeFirstLineOfRightBlockElementIntoAncestorLeftBlockElement()`
    // returns ignored when:
    // - No preceding invisible `<br>` element and
    // - mNewListElementTagNameOfRightListElement is some and
    // - The right block element has no children
    // or,
    // - No preceding invisible `<br>` element and
    // - mNewListElementTagNameOfRightListElement is nothing and
    // - There is no content to move from right block element.
    if (!mPrecedingInvisibleBRElement) {
      if (CanMergeLeftAndRightBlockElements()) {
        // Marked as handled only when it actualy moves a content node.
        Result<bool, nsresult> rightBlockHasContent =
            aHTMLEditor.CanMoveChildren(*mRightBlockElement,
                                        *mLeftBlockElement);
        mFallbackToDeleteLeafContent =
            rightBlockHasContent.isOk() && !rightBlockHasContent.inspect();
      } else {
        // Marked as handled only when it actually moves a content node.
        Result<bool, nsresult> firstLineHasContent =
            AutoMoveOneLineHandler::CanMoveOrDeleteSomethingInLine(
                EditorDOMPoint(mRightBlockElement, 0u), aEditingHost);
        mFallbackToDeleteLeafContent =
            firstLineHasContent.isOk() && !firstLineHasContent.inspect();
      }
    } else {
      // Marked as handled when deleting the invisible `<br>` element.
      mFallbackToDeleteLeafContent = false;
    }
  } else {
    mPrecedingInvisibleBRElement =
        WSRunScanner::GetPrecedingBRElementUnlessVisibleContentFound(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorDOMPoint::AtEndOf(mLeftBlockElement));
    // `WhiteSpaceVisibilityKeeper::
    // MergeFirstLineOfRightBlockElementIntoLeftBlockElement()` always
    // return "handled".
    mFallbackToDeleteLeafContent = false;
  }

  mCanJoinBlocks = true;
  return true;
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoBlockElementsJoiner::
    AutoInclusiveAncestorBlockElementsJoiner::ComputeRangeToDelete(
        const HTMLEditor& /*aHTMLEditor*/, const EditorDOMPoint& aCaretPoint,
        dom::Range& aRangeToDelete, const Element& aEditingHost) const {
  MOZ_ASSERT(mLeftBlockElement);
  MOZ_ASSERT(mRightBlockElement);

  if (IsSameBlockElement()) {
    if (!aCaretPoint.IsSet()) {
      return NS_OK;  // The ranges are not collapsed, keep them as-is.
    }
    nsresult rv = aRangeToDelete.CollapseTo(aCaretPoint.ToRawRangeBoundary());
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Range::CollapseTo() failed");
    return rv;
  }

  EditorDOMPoint pointContainingTheOtherBlock;
  if (!EditorUtils::IsDescendantOf(*mLeftBlockElement, *mRightBlockElement,
                                   &pointContainingTheOtherBlock)) {
    (void)EditorUtils::IsDescendantOf(*mRightBlockElement, *mLeftBlockElement,
                                      &pointContainingTheOtherBlock);
  }
  EditorDOMRange range =
      WSRunScanner::GetRangeForDeletingBlockElementBoundaries(
          {WSRunScanner::Option::OnlyEditableNodes}, *mLeftBlockElement,
          *mRightBlockElement, pointContainingTheOtherBlock);
  if (!range.IsPositioned()) {
    NS_WARNING(
        "WSRunScanner::GetRangeForDeletingBlockElementBoundaries() failed");
    return NS_ERROR_FAILURE;
  }
  if (!aCaretPoint.IsSet()) {
    // Don't shrink the original range.
    bool noNeedToChangeStart = false;
    const EditorDOMPoint atStart(aRangeToDelete.StartRef());
    if (atStart.IsBefore(range.StartRef())) {
      // If the range starts from end of a container, and computed block
      // boundaries range starts from an invisible `<br>` element,  we
      // may need to shrink the range.
      nsIContent* const nextContent =
          atStart.IsEndOfContainer() && range.StartRef().GetChild() &&
                  HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
                      *range.StartRef().GetChild())
              ? HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                    *atStart.ContainerAs<nsIContent>(),
                    {LeafNodeOption::TreatChildBlockAsLeafNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle,
                    &aEditingHost)
              : nullptr;
      if (!nextContent || nextContent != range.StartRef().GetChild()) {
        noNeedToChangeStart = true;
        range.SetStart(EditorRawDOMPoint(aRangeToDelete.StartRef()));
      }
    }
    if (range.EndRef().IsBefore(EditorRawDOMPoint(aRangeToDelete.EndRef()))) {
      if (noNeedToChangeStart) {
        return NS_OK;  // We don't need to modify the range.
      }
      range.SetEnd(EditorRawDOMPoint(aRangeToDelete.EndRef()));
    }
  }
  nsresult rv =
      aRangeToDelete.SetStartAndEnd(range.StartRef().ToRawRangeBoundary(),
                                    range.EndRef().ToRawRangeBoundary());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedRangeArray::SetStartAndEnd() failed");
  return rv;
}

Result<DeleteRangeResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::AutoInclusiveAncestorBlockElementsJoiner::Run(
        HTMLEditor& aHTMLEditor, const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(mLeftBlockElement);
  MOZ_ASSERT(mRightBlockElement);

  if (NS_WARN_IF(IsSameBlockElement()) || NS_WARN_IF(!mCanJoinBlocks)) {
    return DeleteRangeResult::IgnoredResult();
  }

  const auto ConvertMoveNodeResultToDeleteRangeResult =
      [](const EditorDOMPoint& aStartOfRightContent,
         MoveNodeResult&& aMoveNodeResult, const Element& aEditingHost)
          MOZ_NEVER_INLINE_DEBUG MOZ_CAN_RUN_SCRIPT
      -> Result<DeleteRangeResult, nsresult> {
    aMoveNodeResult.IgnoreCaretPointSuggestion();
    if (NS_WARN_IF(aMoveNodeResult.Ignored())) {
      return DeleteRangeResult::IgnoredResult();
    }
    EditorDOMRange movedLineRange = aMoveNodeResult.UnwrapMovedContentRange();
    EditorDOMPoint maybeDeepStartOfRightContent;
    if (MOZ_LIKELY(movedLineRange.IsPositioned())) {
      if (const Element* const firstMovedElement =
              movedLineRange.StartRef().GetChildAs<Element>()) {
        maybeDeepStartOfRightContent =
            HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
                *firstMovedElement,
                {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                 EditablePointOption::StopAtComment});
      } else {
        maybeDeepStartOfRightContent = movedLineRange.StartRef();
      }
    } else {
      maybeDeepStartOfRightContent = aStartOfRightContent;
    }
    if (NS_WARN_IF(
            !maybeDeepStartOfRightContent.IsSetAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    // We should put caret to end of preceding text node if there is.
    // Then, users can type text into it like the other browsers.
    auto pointToPutCaret = [&]() -> EditorDOMPoint {
      const WSScanResult maybePreviousText =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
              {}, maybeDeepStartOfRightContent, &aEditingHost);
      if (maybePreviousText.ContentIsEditable() &&
          maybePreviousText.InVisibleOrCollapsibleCharacters()) {
        return maybePreviousText.PointAfterReachedContent<EditorDOMPoint>();
      }
      return maybeDeepStartOfRightContent;
    }();
    return DeleteRangeResult(std::move(movedLineRange),
                             std::move(pointToPutCaret));
  };

  // If the left block element is in the right block element, move the hard
  // line including the right block element to end of the left block.
  // However, if we are merging list elements, we don't join them.
  if (mPointContainingTheOtherBlockElement.GetContainer() ==
      mRightBlockElement) {
    EditorDOMPoint startOfRightContent =
        mPointContainingTheOtherBlockElement.NextPoint();
    if (const Element* const element =
            startOfRightContent.GetChildAs<Element>()) {
      startOfRightContent =
          HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
              *element, {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                         EditablePointOption::StopAtComment});
    }
    AutoTrackDOMPoint trackStartOfRightBlock(aHTMLEditor.RangeUpdaterRef(),
                                             &startOfRightContent);
    Result<MoveNodeResult, nsresult> moveFirstLineResult =
        WhiteSpaceVisibilityKeeper::
            MergeFirstLineOfRightBlockElementIntoDescendantLeftBlockElement(
                aHTMLEditor, MOZ_KnownLive(*mLeftBlockElement),
                MOZ_KnownLive(*mRightBlockElement),
                mPointContainingTheOtherBlockElement,
                mNewListElementTagNameOfRightListElement,
                MOZ_KnownLive(mPrecedingInvisibleBRElement), aEditingHost);
    if (MOZ_UNLIKELY(moveFirstLineResult.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::"
          "MergeFirstLineOfRightBlockElementIntoDescendantLeftBlockElement() "
          "failed");
      return moveFirstLineResult.propagateErr();
    }

    trackStartOfRightBlock.Flush(StopTracking::Yes);
    return ConvertMoveNodeResultToDeleteRangeResult(
        startOfRightContent, moveFirstLineResult.unwrap(), aEditingHost);
  }

  // If the right block element is in the left block element:
  // - move list item elements in the right block element to where the left
  //   list element is
  // - or first hard line in the right block element to where:
  //   - the left block element is.
  //   - or the given left content in the left block is.
  if (mPointContainingTheOtherBlockElement.GetContainer() ==
      mLeftBlockElement) {
    EditorDOMPoint startOfRightContent =
        HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
            *mRightBlockElement,
            {EditablePointOption::RecognizeInvisibleWhiteSpaces,
             EditablePointOption::StopAtComment});
    AutoTrackDOMPoint trackStartOfRightBlock(aHTMLEditor.RangeUpdaterRef(),
                                             &startOfRightContent);
    Result<MoveNodeResult, nsresult> moveFirstLineResult =
        WhiteSpaceVisibilityKeeper::
            MergeFirstLineOfRightBlockElementIntoAncestorLeftBlockElement(
                aHTMLEditor, MOZ_KnownLive(*mLeftBlockElement),
                MOZ_KnownLive(*mRightBlockElement),
                mPointContainingTheOtherBlockElement,
                MOZ_KnownLive(*mInclusiveDescendantOfLeftBlockElement),
                mNewListElementTagNameOfRightListElement,
                MOZ_KnownLive(mPrecedingInvisibleBRElement), aEditingHost);
    if (MOZ_UNLIKELY(moveFirstLineResult.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::"
          "MergeFirstLineOfRightBlockElementIntoAncestorLeftBlockElement() "
          "failed");
      return moveFirstLineResult.propagateErr();
    }
    trackStartOfRightBlock.Flush(StopTracking::Yes);
    return ConvertMoveNodeResultToDeleteRangeResult(
        startOfRightContent, moveFirstLineResult.unwrap(), aEditingHost);
  }

  // Normal case.  Blocks are siblings, or at least close enough.  An example
  // of the latter is <p>paragraph</p><ul><li>one<li>two<li>three</ul>.  The
  // first li and the p are not true siblings, but we still want to join them
  // if you backspace from li into p.
  MOZ_ASSERT(!mPointContainingTheOtherBlockElement.IsSet());
  EditorDOMPoint startOfRightContent =
      HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
          *mRightBlockElement,
          {EditablePointOption::RecognizeInvisibleWhiteSpaces,
           EditablePointOption::StopAtComment});
  AutoTrackDOMPoint trackStartOfRightBlock(aHTMLEditor.RangeUpdaterRef(),
                                           &startOfRightContent);
  Result<MoveNodeResult, nsresult> moveFirstLineResult =
      WhiteSpaceVisibilityKeeper::
          MergeFirstLineOfRightBlockElementIntoLeftBlockElement(
              aHTMLEditor, MOZ_KnownLive(*mLeftBlockElement),
              MOZ_KnownLive(*mRightBlockElement),
              mNewListElementTagNameOfRightListElement,
              MOZ_KnownLive(mPrecedingInvisibleBRElement), aEditingHost);
  if (MOZ_UNLIKELY(moveFirstLineResult.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::"
        "MergeFirstLineOfRightBlockElementIntoLeftBlockElement() failed");
    return moveFirstLineResult.propagateErr();
  }
  trackStartOfRightBlock.Flush(StopTracking::Yes);
  return ConvertMoveNodeResultToDeleteRangeResult(
      startOfRightContent, moveFirstLineResult.unwrap(), aEditingHost);
}

// static
Result<bool, nsresult>
HTMLEditor::AutoMoveOneLineHandler::CanMoveOrDeleteSomethingInLine(
    const EditorDOMPoint& aPointInHardLine, const Element& aEditingHost) {
  if (NS_WARN_IF(!aPointInHardLine.IsSet()) ||
      NS_WARN_IF(aPointInHardLine.IsInNativeAnonymousSubtree())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  RefPtr<dom::Range> oneLineRange = AutoClonedRangeArray::
      CreateRangeWrappingStartAndEndLinesContainingBoundaries(
          aPointInHardLine, aPointInHardLine,
          EditSubAction::eMergeBlockContents,
          BlockInlineCheck::UseComputedDisplayOutsideStyle, aEditingHost);
  if (!oneLineRange || oneLineRange->Collapsed() ||
      !oneLineRange->IsPositioned() ||
      !oneLineRange->GetStartContainer()->IsContent() ||
      !oneLineRange->GetEndContainer()->IsContent()) {
    return false;
  }

  // If there is only a padding `<br>` element in a empty block, it's selected
  // by `UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement()`.
  // However, it won't be moved.  Although it'll be deleted,
  // AutoMoveOneLineHandler returns "ignored".  Therefore, we should return
  // `false` in this case.
  if (nsIContent* childContent = oneLineRange->GetChildAtStartOffset()) {
    if (childContent->IsHTMLElement(nsGkAtoms::br) &&
        childContent->GetParent()) {
      if (const Element* blockElement =
              HTMLEditUtils::GetInclusiveAncestorElement(
                  *childContent->GetParent(),
                  HTMLEditUtils::ClosestBlockElement,
                  BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
        if (HTMLEditUtils::IsEmptyNode(
                *blockElement,
                {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
          return false;
        }
      }
    }
  }

  EditorRawDOMPoint startPoint(oneLineRange->StartRef());
  EditorRawDOMPoint endPoint(oneLineRange->EndRef());
  // If the range contains only block start boundaries, there is no content to
  // move.
  if (nsIContent* const startContent = startPoint.GetChild()) {
    if (HTMLEditUtils::IsBlockElement(
            *startContent, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      const WSScanResult prevThing =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary({}, endPoint);
      if (prevThing.ReachedCurrentBlockBoundary() &&
          prevThing.ElementPtr()->IsInclusiveDescendantOf(startContent)) {
        return false;
      }
    }
  }

  nsINode* commonAncestor = oneLineRange->GetClosestCommonInclusiveAncestor();
  // Currently, we move non-editable content nodes too.
  if (!startPoint.IsEndOfContainer()) {
    return true;
  }
  if (!endPoint.IsStartOfContainer()) {
    return true;
  }
  if (startPoint.GetContainer() != commonAncestor) {
    while (true) {
      EditorRawDOMPoint pointInParent(startPoint.GetContainerAs<nsIContent>());
      if (NS_WARN_IF(!pointInParent.IsInContentNode())) {
        return Err(NS_ERROR_FAILURE);
      }
      if (pointInParent.GetContainer() == commonAncestor) {
        startPoint = pointInParent;
        break;
      }
      if (!pointInParent.IsEndOfContainer()) {
        return true;
      }
    }
  }
  if (endPoint.GetContainer() != commonAncestor) {
    while (true) {
      EditorRawDOMPoint pointInParent(endPoint.GetContainerAs<nsIContent>());
      if (NS_WARN_IF(!pointInParent.IsInContentNode())) {
        return Err(NS_ERROR_FAILURE);
      }
      if (pointInParent.GetContainer() == commonAncestor) {
        endPoint = pointInParent;
        break;
      }
      if (!pointInParent.IsStartOfContainer()) {
        return true;
      }
    }
  }
  // If start point and end point in the common ancestor are direct siblings,
  // there is no content to move or delete.
  // E.g., `<b>abc<br>[</b><i>]<br>def</i>`.
  return startPoint.GetNextSiblingOfChild() != endPoint.GetChild();
}

nsresult HTMLEditor::AutoMoveOneLineHandler::Prepare(
    HTMLEditor& aHTMLEditor, const EditorDOMPoint& aPointInHardLine,
    const Element& aEditingHost) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(aPointInHardLine.IsInContentNode());
  MOZ_ASSERT(mPointToInsert.IsSetAndValid());

  MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
          ("Prepare(aHTMLEditor=%p, aPointInHardLine=%s, aEditingHost=%s), "
           "mPointToInsert=%s, mMoveToEndOfContainer=%s",
           &aHTMLEditor, ToString(aPointInHardLine).c_str(),
           ToString(aEditingHost).c_str(), ToString(mPointToInsert).c_str(),
           ForceMoveToEndOfContainer() ? "MoveToEndOfContainer::Yes"
                                       : "MoveToEndOfContainer::No"));

  if (NS_WARN_IF(mPointToInsert.IsInNativeAnonymousSubtree())) {
    MOZ_LOG(
        gOneLineMoverLog, LogLevel::Error,
        ("Failed because mPointToInsert was in a native anonymous subtree"));
    return Err(NS_ERROR_INVALID_ARG);
  }

  mSrcInclusiveAncestorBlock =
      aPointInHardLine.IsInContentNode()
          ? HTMLEditUtils::GetInclusiveAncestorElement(
                *aPointInHardLine.ContainerAs<nsIContent>(),
                HTMLEditUtils::ClosestBlockElement,
                BlockInlineCheck::UseComputedDisplayOutsideStyle)
          : nullptr;
  mDestInclusiveAncestorBlock =
      mPointToInsert.IsInContentNode()
          ? HTMLEditUtils::GetInclusiveAncestorElement(
                *mPointToInsert.ContainerAs<nsIContent>(),
                HTMLEditUtils::ClosestBlockElement,
                BlockInlineCheck::UseComputedDisplayOutsideStyle)
          : nullptr;
  mMovingToParentBlock =
      mDestInclusiveAncestorBlock && mSrcInclusiveAncestorBlock &&
      mDestInclusiveAncestorBlock != mSrcInclusiveAncestorBlock &&
      mSrcInclusiveAncestorBlock->IsInclusiveDescendantOf(
          mDestInclusiveAncestorBlock);
  mTopmostSrcAncestorBlockInDestBlock =
      mMovingToParentBlock
          ? AutoMoveOneLineHandler::
                GetMostDistantInclusiveAncestorBlockInSpecificAncestorElement(
                    *mSrcInclusiveAncestorBlock, *mDestInclusiveAncestorBlock)
          : nullptr;
  MOZ_ASSERT_IF(mMovingToParentBlock, mTopmostSrcAncestorBlockInDestBlock);

  mPreserveWhiteSpaceStyle =
      AutoMoveOneLineHandler::ConsiderWhetherPreserveWhiteSpaceStyle(
          aPointInHardLine.GetContainerAs<nsIContent>(),
          mDestInclusiveAncestorBlock);

  AutoClonedRangeArray rangesToWrapTheLine(aPointInHardLine);
  rangesToWrapTheLine.ExtendRangesToWrapLines(
      EditSubAction::eMergeBlockContents,
      BlockInlineCheck::UseComputedDisplayOutsideStyle,
      mTopmostSrcAncestorBlockInDestBlock ? *mTopmostSrcAncestorBlockInDestBlock
                                          : aEditingHost);
  MOZ_ASSERT(rangesToWrapTheLine.Ranges().Length() <= 1u);
  mLineRange = EditorDOMRange(rangesToWrapTheLine.FirstRangeRef());

  MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
          ("mSrcInclusiveAncestorBlock=%s, mDestInclusiveAncestorBlock=%s, "
           "mMovingToParentBlock=%s, mTopmostSrcAncestorBlockInDestBlock=%s, "
           "mPreserveWhiteSpaceStyle=%s, mLineRange=%s",
           mSrcInclusiveAncestorBlock
               ? ToString(*mSrcInclusiveAncestorBlock).c_str()
               : "nullptr",
           mDestInclusiveAncestorBlock
               ? ToString(*mDestInclusiveAncestorBlock).c_str()
               : "nullptr",
           mMovingToParentBlock ? "true" : "false",
           mTopmostSrcAncestorBlockInDestBlock
               ? ToString(*mTopmostSrcAncestorBlockInDestBlock).c_str()
               : "nullptr",
           ToString(mPreserveWhiteSpaceStyle).c_str(),
           ToString(mLineRange).c_str()));

  return NS_OK;
}

Result<CaretPoint, nsresult>
HTMLEditor::AutoMoveOneLineHandler::SplitToMakeTheLineIsolated(
    HTMLEditor& aHTMLEditor, const nsIContent& aNewContainer,
    const Element& aEditingHost,
    nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents) const {
  AutoClonedRangeArray rangesToWrapTheLine(mLineRange);
  Result<EditorDOMPoint, nsresult> splitResult =
      rangesToWrapTheLine
          .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
              aHTMLEditor, BlockInlineCheck::UseComputedDisplayOutsideStyle,
              aEditingHost, &aNewContainer);
  if (MOZ_UNLIKELY(splitResult.isErr())) {
    NS_WARNING(
        "AutoClonedRangeArray::"
        "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() failed");
    return Err(splitResult.unwrapErr());
  }
  EditorDOMPoint pointToPutCaret;
  if (splitResult.inspect().IsSet()) {
    pointToPutCaret = splitResult.unwrap();
  }
  nsresult rv = rangesToWrapTheLine.CollectEditTargetNodes(
      aHTMLEditor, aOutArrayOfContents, EditSubAction::eMergeBlockContents,
      AutoClonedRangeArray::CollectNonEditableNodes::Yes);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
        "eMergeBlockContents, CollectNonEditableNodes::Yes) failed");
    return Err(rv);
  }
  return CaretPoint(pointToPutCaret);
}

// static
Element* HTMLEditor::AutoMoveOneLineHandler::
    GetMostDistantInclusiveAncestorBlockInSpecificAncestorElement(
        Element& aBlockElement, const Element& aAncestorElement) {
  MOZ_ASSERT(aBlockElement.IsInclusiveDescendantOf(&aAncestorElement));
  MOZ_ASSERT(HTMLEditUtils::IsBlockElement(
      aBlockElement, BlockInlineCheck::UseComputedDisplayOutsideStyle));

  if (&aBlockElement == &aAncestorElement) {
    return nullptr;
  }

  Element* lastBlockAncestor = &aBlockElement;
  for (Element* element : aBlockElement.InclusiveAncestorsOfType<Element>()) {
    if (element == &aAncestorElement) {
      return lastBlockAncestor;
    }
    if (HTMLEditUtils::IsBlockElement(
            *lastBlockAncestor,
            BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      lastBlockAncestor = element;
    }
  }
  return nullptr;
}

// static
HTMLEditor::PreserveWhiteSpaceStyle
HTMLEditor::AutoMoveOneLineHandler::ConsiderWhetherPreserveWhiteSpaceStyle(
    const nsIContent* aContentInLine,
    const Element* aInclusiveAncestorBlockOfInsertionPoint) {
  if (MOZ_UNLIKELY(!aInclusiveAncestorBlockOfInsertionPoint)) {
    return PreserveWhiteSpaceStyle::No;
  }

  // If we move content from or to <pre>, we don't need to preserve the
  // white-space style for compatibility with both our traditional behavior
  // and the other browsers.

  // TODO: If `white-space` is specified by non-UA stylesheet, we should
  // preserve it even if the right block is <pre> for compatibility with the
  // other browsers.
  const auto IsInclusiveDescendantOfPre = [](const nsIContent& aContent) {
    // If the content has different `white-space` style from <pre>, we
    // shouldn't treat it as a descendant of <pre> because web apps or
    // the user intent to treat the white-spaces in aContent not as `pre`.
    if (EditorUtils::GetComputedWhiteSpaceStyles(aContent).valueOr(std::pair(
            StyleWhiteSpaceCollapse::Collapse, StyleTextWrapMode::Wrap)) !=
        std::pair(StyleWhiteSpaceCollapse::Preserve,
                  StyleTextWrapMode::Nowrap)) {
      return false;
    }
    for (const Element* element :
         aContent.InclusiveAncestorsOfType<Element>()) {
      if (element->IsHTMLElement(nsGkAtoms::pre)) {
        return true;
      }
    }
    return false;
  };
  if (IsInclusiveDescendantOfPre(*aInclusiveAncestorBlockOfInsertionPoint) ||
      MOZ_UNLIKELY(!aContentInLine) ||
      IsInclusiveDescendantOfPre(*aContentInLine)) {
    return PreserveWhiteSpaceStyle::No;
  }
  return PreserveWhiteSpaceStyle::Yes;
}

Result<MoveNodeResult, nsresult> HTMLEditor::AutoMoveOneLineHandler::Run(
    HTMLEditor& aHTMLEditor, const Element& aEditingHost) {
  EditorDOMPoint pointToInsert(NextInsertionPointRef());
  MOZ_ASSERT(pointToInsert.IsInContentNode());

  MOZ_LOG(
      gOneLineMoverLog, LogLevel::Info,
      ("Run(aHTMLEditor=%p, aEditingHost=%s), pointToInsert=%s", &aHTMLEditor,
       ToString(aEditingHost).c_str(), ToString(pointToInsert).c_str()));

  EditorDOMPoint pointToPutCaret;
  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    AutoTrackDOMPoint tackPointToInsert(aHTMLEditor.RangeUpdaterRef(),
                                        &pointToInsert);

    Result<CaretPoint, nsresult> splitAtLineEdgesResult =
        SplitToMakeTheLineIsolated(
            aHTMLEditor,
            MOZ_KnownLive(*pointToInsert.ContainerAs<nsIContent>()),
            aEditingHost, arrayOfContents);
    if (MOZ_UNLIKELY(splitAtLineEdgesResult.isErr())) {
      NS_WARNING("AutoMoveOneLineHandler::SplitToMakeTheLineIsolated() failed");
      MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
              ("Run: SplitToMakeTheLineIsolated() failed"));
      return splitAtLineEdgesResult.propagateErr();
    }
    splitAtLineEdgesResult.unwrap().MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    MOZ_LOG(gOneLineMoverLog, LogLevel::Verbose,
            ("Run: pointToPutCaret=%s", ToString(pointToPutCaret).c_str()));

    Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
        aHTMLEditor.MaybeSplitElementsAtEveryBRElement(
            arrayOfContents, EditSubAction::eMergeBlockContents);
    if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
          "eMergeBlockContents) failed");
      MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
              ("Run: MaybeSplitElementsAtEveryBRElement() failed"));
      return splitAtBRElementsResult.propagateErr();
    }
    if (splitAtBRElementsResult.inspect().IsSet()) {
      pointToPutCaret = splitAtBRElementsResult.unwrap();
    }
    MOZ_LOG(gOneLineMoverLog, LogLevel::Verbose,
            ("Run: pointToPutCaret=%s", ToString(pointToPutCaret).c_str()));
  }

  if (!pointToInsert.IsSetAndValid()) {
    MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
            ("Run: Failed because pointToInsert pointed invalid position"));
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (aHTMLEditor.AllowsTransactionsToChangeSelection() &&
      pointToPutCaret.IsSet()) {
    nsresult rv = aHTMLEditor.CollapseSelectionTo(pointToPutCaret);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
              ("Run: Failed because of "
               "aHTMLEditor.CollapseSelectionTo(pointToPutCaret) failure"));
      return Err(rv);
    }
  }

  if (arrayOfContents.IsEmpty()) {
    MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
            ("Run: Did nothing because of no content to be moved"));
    return MoveNodeResult::IgnoredResult(std::move(pointToInsert));
  }

  // Track the range which contains the moved contents.
  if (ForceMoveToEndOfContainer()) {
    pointToInsert = NextInsertionPointRef();
  }
  EditorDOMRange movedContentRange(pointToInsert);
  MoveNodeResult moveContentsInLineResult =
      MoveNodeResult::IgnoredResult(pointToInsert);
  for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
    MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
            ("Run: content=%s, pointToInsert=%s, movedContentRange=%s, "
             "mPointToInsert=%s",
             ToString(content.ref()).c_str(), ToString(pointToInsert).c_str(),
             ToString(movedContentRange).c_str(),
             ToString(mPointToInsert).c_str()));
    {
      AutoEditorDOMRangeChildrenInvalidator lockOffsets(movedContentRange);
      AutoTrackDOMRange trackMovedContentRange(aHTMLEditor.RangeUpdaterRef(),
                                               &movedContentRange);
      // If the content is a block element, move all children of it to the
      // new container, and then, remove the (probably) empty block element.
      if (HTMLEditUtils::IsBlockElement(
              content, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
        MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
                ("Run: Unwrapping children of content because of a block"));
        AutoTrackDOMMoveNodeResult trackMoveContentsInLineResult(
            aHTMLEditor.RangeUpdaterRef(), &moveContentsInLineResult);
        Result<MoveNodeResult, nsresult> moveChildrenResult =
            aHTMLEditor.MoveChildrenWithTransaction(
                MOZ_KnownLive(*content->AsElement()), pointToInsert,
                mPreserveWhiteSpaceStyle, RemoveIfInvisibleNode::Yes);
        if (MOZ_UNLIKELY(moveChildrenResult.isErr())) {
          NS_WARNING("HTMLEditor::MoveChildrenWithTransaction() failed");
          MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
                  ("Run: MoveChildrenWithTransaction() failed"));
          moveContentsInLineResult.IgnoreCaretPointSuggestion();
          return moveChildrenResult;
        }
        trackMoveContentsInLineResult.Flush(StopTracking::Yes);
        moveContentsInLineResult |= moveChildrenResult.inspect();
        {
          AutoTrackDOMMoveNodeResult trackMoveContentsInLineResult(
              aHTMLEditor.RangeUpdaterRef(), &moveContentsInLineResult);
          // MOZ_KnownLive due to bug 1620312
          nsresult rv =
              aHTMLEditor.DeleteNodeWithTransaction(MOZ_KnownLive(content));
          if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
            MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
                    ("Run: Aborted because DeleteNodeWithTransaction() caused "
                     "destroying the editor"));
            moveContentsInLineResult.IgnoreCaretPointSuggestion();
            return Err(NS_ERROR_EDITOR_DESTROYED);
          }
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "EditorBase::DeleteNodeWithTransaction() failed, but ignored");
            MOZ_LOG(
                gOneLineMoverLog, LogLevel::Warning,
                ("Run: Failed to delete content but the error was ignored"));
          }
        }
      } else {
        const bool canDelete = [&]() {
          // We don't need to move Comment, etc, just delete.
          if (!content->IsText() && !content->IsElement()) {
            return true;
          }
          if (const Text* const text = Text::FromNode(content)) {
            // Empty Text will be deleted so that we don't need to move it.
            if (!text->TextDataLength()) {
              return true;
            }
            // If the Text contains only one preformatted line break, we can
            // delete it even if surrounded by collapsible white-spaces because
            // we're moving a line so that it should be the last content of the
            // line.
            if (text->TextDataLength() == 1 &&
                EditorUtils::IsNewLinePreformatted(*text) &&
                text->DataBuffer().FirstChar() == HTMLEditUtils::kNewLine) {
              return true;
            }
            return false;
          }
          const Element& element = *content->AsElement();
          if (HTMLEditUtils::IsReplacedElement(element)) {
            return false;
          }
          // If the element is a <br>, it should be the last content of the
          // line. Therefore, we don't need to move it.
          if (element.IsHTMLElement(nsGkAtoms::br)) {
            return true;
          }
          return HTMLEditUtils::IsEmptyInlineContainer(
              content, {EmptyCheckOption::TreatNonEditableContentAsInvisible},
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
        }();
        // If the moving content is a comment node or an empty inline node, we
        // don't want it to appear in the dist paragraph.
        if (canDelete) {
          nsCOMPtr<nsIContent> emptyContent =
              HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
                  content, BlockInlineCheck::UseComputedDisplayOutsideStyle,
                  &aEditingHost, pointToInsert.ContainerAs<nsIContent>());
          if (!emptyContent) {
            emptyContent = content;
          }
          MOZ_LOG_FMT(
              gOneLineMoverLog, LogLevel::Info,
              "Run: Deleting content because of {}{}",
              content->IsComment()
                  ? "a comment node"
                  : (content->IsText() ? "an empty text node"
                                       : "an empty inline container"),
              content != emptyContent
                  ? fmt::format(" (deleting topmost empty ancestor: {})",
                                ToString(*emptyContent))
                        .c_str()
                  : "");
          AutoTrackDOMMoveNodeResult trackMoveContentsInLineResult(
              aHTMLEditor.RangeUpdaterRef(), &moveContentsInLineResult);
          nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(*emptyContent);
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
            MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
                    ("Run: DeleteNodeWithTransaction() failed"));
            moveContentsInLineResult.IgnoreCaretPointSuggestion();
            return Err(rv);
          }
        } else {
          MOZ_LOG(gOneLineMoverLog, LogLevel::Info, ("Run: Moving content"));
          AutoTrackDOMMoveNodeResult trackMoveContentsInLineResult(
              aHTMLEditor.RangeUpdaterRef(), &moveContentsInLineResult);
          // MOZ_KnownLive due to bug 1620312
          Result<MoveNodeResult, nsresult> moveNodeOrChildrenResult =
              aHTMLEditor.MoveNodeOrChildrenWithTransaction(
                  MOZ_KnownLive(content), pointToInsert,
                  mPreserveWhiteSpaceStyle, RemoveIfInvisibleNode::Yes);
          if (MOZ_UNLIKELY(moveNodeOrChildrenResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::MoveNodeOrChildrenWithTransaction() failed");
            MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
                    ("Run: MoveNodeOrChildrenWithTransaction() failed"));
            moveContentsInLineResult.IgnoreCaretPointSuggestion();
            return moveNodeOrChildrenResult;
          }
          trackMoveContentsInLineResult.Flush(StopTracking::Yes);
          moveContentsInLineResult |= moveNodeOrChildrenResult.inspect();
        }
      }
    }
    MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
            ("Run: movedContentRange=%s, mPointToInsert=%s",
             ToString(movedContentRange).c_str(),
             ToString(mPointToInsert).c_str()));
    moveContentsInLineResult.ForceToMarkAsHandled();
    if (NS_WARN_IF(!movedContentRange.IsPositioned())) {
      MOZ_LOG(gOneLineMoverLog, LogLevel::Error,
              ("Run: Failed because movedContentRange was not positioned"));
      moveContentsInLineResult.IgnoreCaretPointSuggestion();
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    // For backward compatibility, we should move contents to end of the
    // container if the instance is created without specific insertion point.
    if (ForceMoveToEndOfContainer()) {
      pointToInsert = NextInsertionPointRef();
      MOZ_ASSERT(pointToInsert.IsSet());
      MOZ_ASSERT(movedContentRange.StartRef().EqualsOrIsBefore(pointToInsert));
      movedContentRange.SetEnd(pointToInsert);
      MOZ_LOG(gOneLineMoverLog, LogLevel::Debug,
              ("Run: Updated movedContentRange end to next insertion point"));
    }
    // And also if pointToInsert has been made invalid with removing preceding
    // children, we should move the content to the end of the container.
    else if (aHTMLEditor.MaybeNodeRemovalsObservedByDevTools() &&
             MOZ_UNLIKELY(!moveContentsInLineResult.NextInsertionPointRef()
                               .IsSetAndValid())) {
      mPointToInsert.SetToEndOf(mPointToInsert.GetContainer());
      pointToInsert = NextInsertionPointRef();
      movedContentRange.SetEnd(pointToInsert);
      MOZ_LOG(gOneLineMoverLog, LogLevel::Debug,
              ("Run: Updated mPointToInsert to end of container and updated "
               "movedContentRange"));
    } else {
      MOZ_DIAGNOSTIC_ASSERT(
          moveContentsInLineResult.NextInsertionPointRef().IsSet());
      mPointToInsert = moveContentsInLineResult.NextInsertionPointRef();
      pointToInsert = NextInsertionPointRef();
      if (!aHTMLEditor.MaybeNodeRemovalsObservedByDevTools() ||
          movedContentRange.EndRef().IsBefore(pointToInsert)) {
        MOZ_ASSERT(pointToInsert.IsSet());
        MOZ_ASSERT(
            movedContentRange.StartRef().EqualsOrIsBefore(pointToInsert));
        movedContentRange.SetEnd(pointToInsert);
        MOZ_LOG(gOneLineMoverLog, LogLevel::Debug,
                ("Run: Updated mPointToInsert and updated movedContentRange"));
      } else {
        MOZ_LOG(gOneLineMoverLog, LogLevel::Debug,
                ("Run: Updated only mPointToInsert"));
      }
    }
  }

  // Nothing has been moved, we don't need to clean up unnecessary <br> element.
  // And also if we're not moving content into a block, we can quit right now.
  if (moveContentsInLineResult.Ignored() ||
      MOZ_UNLIKELY(!mDestInclusiveAncestorBlock)) {
    MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
            (moveContentsInLineResult.Ignored()
                 ? "Run: Did nothing for any children"
                 : "Run: Finished (not dest block)"));
    return std::move(moveContentsInLineResult);
  }

  // If we couldn't track the range to clean up, we should just stop cleaning up
  // because returning error from here may change the behavior of web apps using
  // mutation event listeners.
  if (MOZ_UNLIKELY(!movedContentRange.IsPositioned() ||
                   movedContentRange.Collapsed())) {
    MOZ_LOG(gOneLineMoverLog, LogLevel::Info,
            (!movedContentRange.IsPositioned()
                 ? "Run: Finished (Couldn't track moved line)"
                 : "Run: Finished (Moved line was empty)"));
    return std::move(moveContentsInLineResult);
  }

  {
    AutoTrackDOMMoveNodeResult trackMoveContentsInLineResult(
        aHTMLEditor.RangeUpdaterRef(), &moveContentsInLineResult);
    nsresult rv = DeleteUnnecessaryTrailingLineBreakInMovedLineEnd(
        aHTMLEditor, movedContentRange, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoMoveOneLineHandler::"
          "DeleteUnnecessaryTrailingLineBreakInMovedLineEnd() failed");
      MOZ_LOG(
          gOneLineMoverLog, LogLevel::Error,
          ("Run: DeleteUnnecessaryTrailingLineBreakInMovedLineEnd() failed"));
      moveContentsInLineResult.IgnoreCaretPointSuggestion();
      return Err(rv);
    }
  }

  MOZ_LOG(gOneLineMoverLog, LogLevel::Info, ("Run: Finished"));
  return std::move(moveContentsInLineResult);
}

nsresult HTMLEditor::AutoMoveOneLineHandler::
    DeleteUnnecessaryTrailingLineBreakInMovedLineEnd(
        HTMLEditor& aHTMLEditor, const EditorDOMRange& aMovedContentRange,
        const Element& aEditingHost) const {
  MOZ_ASSERT(mDestInclusiveAncestorBlock);
  MOZ_ASSERT(aMovedContentRange.IsPositioned());
  MOZ_ASSERT(!aMovedContentRange.Collapsed());

  // If we didn't preserve white-space for backward compatibility and
  // white-space becomes not preformatted, we need to clean it up the last text
  // node if it ends with a preformatted line break.
  if (mPreserveWhiteSpaceStyle == PreserveWhiteSpaceStyle::No) {
    const RefPtr<Text> textNodeEndingWithUnnecessaryLineBreak = [&]() -> Text* {
      Text* lastTextNode = Text::FromNodeOrNull(
          mMovingToParentBlock
              ? HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
                    *mTopmostSrcAncestorBlockInDestBlock,
                    {LeafNodeOption::TreatChildBlockAsLeafNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle,
                    mDestInclusiveAncestorBlock)
              : HTMLEditUtils::GetLastLeafContent(
                    *mDestInclusiveAncestorBlock,
                    {LeafNodeOption::TreatNonEditableNodeAsLeafNode}));
      if (!lastTextNode ||
          !HTMLEditUtils::IsSimplyEditableNode(*lastTextNode)) {
        return nullptr;
      }
      const CharacterDataBuffer& characterDataBuffer =
          lastTextNode->DataBuffer();
      const char16_t lastCh =
          characterDataBuffer.GetLength()
              ? characterDataBuffer.CharAt(characterDataBuffer.GetLength() - 1u)
              : 0;
      return lastCh == HTMLEditUtils::kNewLine &&
                     !EditorUtils::IsNewLinePreformatted(*lastTextNode)
                 ? lastTextNode
                 : nullptr;
    }();
    if (textNodeEndingWithUnnecessaryLineBreak) {
      if (textNodeEndingWithUnnecessaryLineBreak->TextDataLength() == 1u) {
        const RefPtr<Element> inlineElement =
            HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
                *textNodeEndingWithUnnecessaryLineBreak,
                BlockInlineCheck::UseComputedDisplayOutsideStyle,
                &aEditingHost);
        nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(
            inlineElement ? static_cast<nsIContent&>(*inlineElement)
                          : static_cast<nsIContent&>(
                                *textNodeEndingWithUnnecessaryLineBreak));
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return Err(rv);
        }
      } else {
        Result<CaretPoint, nsresult> caretPointOrError =
            aHTMLEditor.DeleteTextWithTransaction(
                *textNodeEndingWithUnnecessaryLineBreak,
                textNodeEndingWithUnnecessaryLineBreak->TextDataLength() - 1u,
                1u);
        if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
          NS_WARNING("HTMLEditor::DeleteTextWithTransaction() failed");
          return caretPointOrError.propagateErr();
        }
        nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
            aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                          SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                          SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CaretPoint::SuggestCaretPointTo() failed, but ignored");
      }
    }
  }

  if (NS_WARN_IF(mMovingToParentBlock &&
                 !mTopmostSrcAncestorBlockInDestBlock->GetParentNode()) ||
      NS_WARN_IF(!mMovingToParentBlock &&
                 !mDestInclusiveAncestorBlock->GetParentNode())) {
    return NS_OK;
  }
  const Maybe<EditorLineBreak> lastLineBreak =
      mMovingToParentBlock
          ? HTMLEditUtils::GetPrecedingUnnecessaryLineBreak<EditorLineBreak>(
                EditorRawDOMPoint(mTopmostSrcAncestorBlockInDestBlock),
                &aEditingHost)
          : HTMLEditUtils::GetPrecedingUnnecessaryLineBreak<EditorLineBreak>(
                EditorRawDOMPoint::AtEndOf(*mDestInclusiveAncestorBlock),
                &aEditingHost);
  if (lastLineBreak.isNothing() ||
      !lastLineBreak->IsDeletableFromComposedDoc()) {
    return NS_OK;
  }
  const auto atUnnecessaryLineBreak = lastLineBreak->To<EditorRawDOMPoint>();
  if (NS_WARN_IF(!atUnnecessaryLineBreak.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  // If the found unnecessary line break is not what we moved above, we
  // shouldn't remove it.  E.g., the web app may have inserted it intentionally.
  MOZ_ASSERT(aMovedContentRange.StartRef().IsSetAndValid());
  MOZ_ASSERT(aMovedContentRange.EndRef().IsSetAndValid());
  if (!aMovedContentRange.Contains(atUnnecessaryLineBreak)) {
    return NS_OK;
  }

  AutoTransactionsConserveSelection dontChangeMySelection(aHTMLEditor);
  Result<EditorDOMPoint, nsresult> lineBreakPointOrError =
      aHTMLEditor.DeleteLineBreakWithTransaction(
          lastLineBreak.ref(),
          aEditingHost.IsContentEditablePlainTextOnly() ? nsIEditor::eNoStrip
                                                        : nsIEditor::eStrip,
          aEditingHost);
  if (MOZ_UNLIKELY(lineBreakPointOrError.isErr())) {
    NS_WARNING("HTMLEditor::DeleteLineBreakWithTransaction() failed");
    return lineBreakPointOrError.propagateErr();
  }
  return NS_OK;
}

Result<bool, nsresult> HTMLEditor::CanMoveNodeOrChildren(
    const nsIContent& aContent, const nsINode& aNewContainer) const {
  if (HTMLEditUtils::CanNodeContain(aNewContainer, aContent)) {
    return true;
  }
  if (aContent.IsElement()) {
    return CanMoveChildren(*aContent.AsElement(), aNewContainer);
  }
  return true;
}

Result<MoveNodeResult, nsresult> HTMLEditor::MoveNodeOrChildrenWithTransaction(
    nsIContent& aContentToMove, const EditorDOMPoint& aPointToInsert,
    PreserveWhiteSpaceStyle aPreserveWhiteSpaceStyle,
    RemoveIfInvisibleNode aRemoveIfInvisibleNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsInContentNode());

  const auto destWhiteSpaceStyles =
      [&]() -> Maybe<std::pair<StyleWhiteSpaceCollapse, StyleTextWrapMode>> {
    if (aPreserveWhiteSpaceStyle == PreserveWhiteSpaceStyle::No ||
        !aPointToInsert.IsInContentNode()) {
      return Nothing();
    }
    auto styles = EditorUtils::GetComputedWhiteSpaceStyles(
        *aPointToInsert.ContainerAs<nsIContent>());
    if (NS_WARN_IF(styles.isSome() &&
                   styles.value().first ==
                       StyleWhiteSpaceCollapse::PreserveSpaces)) {
      return Nothing();
    }
    return styles;
  }();
  const auto srcWhiteSpaceStyles =
      [&]() -> Maybe<std::pair<StyleWhiteSpaceCollapse, StyleTextWrapMode>> {
    if (aPreserveWhiteSpaceStyle == PreserveWhiteSpaceStyle::No) {
      return Nothing();
    }
    auto styles = EditorUtils::GetComputedWhiteSpaceStyles(aContentToMove);
    if (NS_WARN_IF(styles.isSome() &&
                   styles.value().first ==
                       StyleWhiteSpaceCollapse::PreserveSpaces)) {
      return Nothing();
    }
    return styles;
  }();
  // Get the `white-space` shorthand form for the given collapse + mode pair.
  const auto GetWhiteSpaceStyleValue =
      [](std::pair<StyleWhiteSpaceCollapse, StyleTextWrapMode> aStyles) {
        if (aStyles.second == StyleTextWrapMode::Wrap) {
          switch (aStyles.first) {
            case StyleWhiteSpaceCollapse::Collapse:
              return u"normal"_ns;
            case StyleWhiteSpaceCollapse::Preserve:
              return u"pre-wrap"_ns;
            case StyleWhiteSpaceCollapse::PreserveBreaks:
              return u"pre-line"_ns;
            case StyleWhiteSpaceCollapse::PreserveSpaces:
              return u"preserve-spaces"_ns;
            case StyleWhiteSpaceCollapse::BreakSpaces:
              return u"break-spaces"_ns;
          }
        } else {
          switch (aStyles.first) {
            case StyleWhiteSpaceCollapse::Collapse:
              return u"nowrap"_ns;
            case StyleWhiteSpaceCollapse::Preserve:
              return u"pre"_ns;
            case StyleWhiteSpaceCollapse::PreserveBreaks:
              return u"nowrap preserve-breaks"_ns;
            case StyleWhiteSpaceCollapse::PreserveSpaces:
              return u"nowrap preserve-spaces"_ns;
            case StyleWhiteSpaceCollapse::BreakSpaces:
              return u"nowrap break-spaces"_ns;
          }
        }
        MOZ_ASSERT_UNREACHABLE("all values should be handled above!");
        return u"normal"_ns;
      };

  if (aRemoveIfInvisibleNode == RemoveIfInvisibleNode::Yes &&
      NodeIsInvisibleOrLineBreakFollowedByBlockBoundary(aContentToMove)) {
    EditorDOMPoint pointToInsert(aPointToInsert);
    {
      AutoTrackDOMPoint trackPointToInsert(RangeUpdaterRef(), &pointToInsert);
      nsresult rv = DeleteNodeWithTransaction(aContentToMove);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return Err(rv);
      }
    }
    if (NS_WARN_IF(!pointToInsert.IsSetAndValid())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return MoveNodeResult::HandledResult(std::move(pointToInsert));
  }

  // Check if this node can go into the destination node
  if (HTMLEditUtils::CanNodeContain(*aPointToInsert.GetContainer(),
                                    aContentToMove)) {
    EditorDOMPoint pointToInsert(aPointToInsert);
    // Preserve white-space in the new position with using `style` attribute.
    // This is additional path from point of view of our traditional behavior.
    // Therefore, ignore errors especially if we got unexpected DOM tree.
    if (destWhiteSpaceStyles.isSome() && srcWhiteSpaceStyles.isSome() &&
        destWhiteSpaceStyles.value() != srcWhiteSpaceStyles.value()) {
      // Set `white-space` with `style` attribute if it's nsStyledElement.
      if (nsStyledElement* styledElement =
              nsStyledElement::FromNode(&aContentToMove)) {
        DebugOnly<nsresult> rvIgnored =
            CSSEditUtils::SetCSSPropertyWithTransaction(
                *this, MOZ_KnownLive(*styledElement), *nsGkAtoms::white_space,
                GetWhiteSpaceStyleValue(srcWhiteSpaceStyles.value()));
        if (NS_WARN_IF(Destroyed())) {
          return Err(NS_ERROR_EDITOR_DESTROYED);
        }
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "CSSEditUtils::SetCSSPropertyWithTransaction("
                             "nsGkAtoms::white_space) failed, but ignored");
      }
      // Otherwise, if the dest container can have <span> element and <span>
      // element can have the moving content node, we should insert it.
      else if (HTMLEditUtils::CanNodeContain(*aPointToInsert.GetContainer(),
                                             *nsGkAtoms::span) &&
               HTMLEditUtils::CanNodeContain(*nsGkAtoms::span,
                                             aContentToMove)) {
        RefPtr<Element> newSpanElement = CreateHTMLContent(nsGkAtoms::span);
        if (NS_WARN_IF(!newSpanElement)) {
          return Err(NS_ERROR_FAILURE);
        }
        nsAutoString styleAttrValue(u"white-space: "_ns);
        styleAttrValue.Append(
            GetWhiteSpaceStyleValue(srcWhiteSpaceStyles.value()));
        IgnoredErrorResult error;
        newSpanElement->SetAttr(nsGkAtoms::style, styleAttrValue, error);
        NS_WARNING_ASSERTION(!error.Failed(),
                             "Element::SetAttr(nsGkAtoms::span) failed");
        if (MOZ_LIKELY(!error.Failed())) {
          Result<CreateElementResult, nsresult> insertSpanElementResult =
              InsertNodeWithTransaction<Element>(*newSpanElement,
                                                 aPointToInsert);
          if (MOZ_UNLIKELY(insertSpanElementResult.isErr())) {
            if (NS_WARN_IF(insertSpanElementResult.inspectErr() ==
                           NS_ERROR_EDITOR_DESTROYED)) {
              return Err(NS_ERROR_EDITOR_DESTROYED);
            }
            NS_WARNING(
                "HTMLEditor::InsertNodeWithTransaction() failed, but ignored");
          } else {
            // We should move the node into the new <span> to preserve the
            // style.
            pointToInsert.Set(newSpanElement, 0u);
            // We should put caret after aContentToMove after moving it so that
            // we do not need the suggested caret point here.
            insertSpanElementResult.inspect().IgnoreCaretPointSuggestion();
          }
        }
      }
    }
    // If it can, move it there.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeWithTransaction(aContentToMove, pointToInsert);
    NS_WARNING_ASSERTION(moveNodeResult.isOk(),
                         "HTMLEditor::MoveNodeWithTransaction() failed");
    // XXX This is odd to override the handled state here, but stopping this
    //     hits an NS_ASSERTION in WhiteSpaceVisibilityKeeper::
    //     MergeFirstLineOfRightBlockElementIntoAncestorLeftBlockElement.
    if (moveNodeResult.isOk()) {
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      unwrappedMoveNodeResult.ForceToMarkAsHandled();
      return unwrappedMoveNodeResult;
    }
    return moveNodeResult;
  }

  // If it can't, move its children (if any), and then delete it.
  auto moveNodeResult =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<MoveNodeResult, nsresult> {
    if (!aContentToMove.IsElement()) {
      return MoveNodeResult::HandledResult(aPointToInsert);
    }
    Result<MoveNodeResult, nsresult> moveChildrenResult =
        MoveChildrenWithTransaction(MOZ_KnownLive(*aContentToMove.AsElement()),
                                    aPointToInsert, aPreserveWhiteSpaceStyle,
                                    aRemoveIfInvisibleNode);
    NS_WARNING_ASSERTION(moveChildrenResult.isOk(),
                         "HTMLEditor::MoveChildrenWithTransaction() failed");
    return moveChildrenResult;
  }();
  if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
    return moveNodeResult;  // Already warned in the lambda.
  }

  MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
  {
    AutoTrackDOMMoveNodeResult trackMoveNodeResult(RangeUpdaterRef(),
                                                   &unwrappedMoveNodeResult);
    nsresult rv = DeleteNodeWithTransaction(aContentToMove);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      unwrappedMoveNodeResult.IgnoreCaretPointSuggestion();
      return Err(rv);
    }
  }
  if (!MaybeNodeRemovalsObservedByDevTools()) {
    return std::move(unwrappedMoveNodeResult);
  }
  // Mutation event listener may make `offset` value invalid with
  // removing some previous children while we call
  // `DeleteNodeWithTransaction()` so that we should adjust it here.
  if (unwrappedMoveNodeResult.NextInsertionPointRef()
          .IsSetAndValidInComposedDoc()) {
    return std::move(unwrappedMoveNodeResult);
  }
  unwrappedMoveNodeResult |= MoveNodeResult::HandledResult(
      EditorDOMPoint::AtEndOf(*aPointToInsert.GetContainer()));
  return std::move(unwrappedMoveNodeResult);
}

Result<bool, nsresult> HTMLEditor::CanMoveChildren(
    const Element& aElement, const nsINode& aNewContainer) const {
  if (NS_WARN_IF(&aElement == &aNewContainer)) {
    return Err(NS_ERROR_FAILURE);
  }
  for (nsIContent* childContent = aElement.GetFirstChild(); childContent;
       childContent = childContent->GetNextSibling()) {
    Result<bool, nsresult> result =
        CanMoveNodeOrChildren(*childContent, aNewContainer);
    if (result.isErr() || result.inspect()) {
      return result;
    }
  }
  return false;
}

Result<MoveNodeResult, nsresult> HTMLEditor::MoveChildrenWithTransaction(
    Element& aElement, const EditorDOMPoint& aPointToInsert,
    PreserveWhiteSpaceStyle aPreserveWhiteSpaceStyle,
    RemoveIfInvisibleNode aRemoveIfInvisibleNode) {
  MOZ_ASSERT(aPointToInsert.IsSet());

  if (NS_WARN_IF(&aElement == aPointToInsert.GetContainer())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  MoveNodeResult moveChildrenResult =
      MoveNodeResult::IgnoredResult(aPointToInsert);
  while (nsCOMPtr<nsIContent> firstChild = aElement.GetFirstChild()) {
    AutoTrackDOMMoveNodeResult trackMoveChildrenResult(RangeUpdaterRef(),
                                                       &moveChildrenResult);
    Result<MoveNodeResult, nsresult> moveNodeOrChildrenResult =
        MoveNodeOrChildrenWithTransaction(
            *firstChild, moveChildrenResult.NextInsertionPointRef(),
            aPreserveWhiteSpaceStyle, aRemoveIfInvisibleNode);
    if (MOZ_UNLIKELY(moveNodeOrChildrenResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeOrChildrenWithTransaction() failed");
      moveChildrenResult.IgnoreCaretPointSuggestion();
      return moveNodeOrChildrenResult;
    }
    trackMoveChildrenResult.Flush(StopTracking::Yes);
    moveChildrenResult |= moveNodeOrChildrenResult.inspect();
  }
  return moveChildrenResult;
}

nsresult HTMLEditor::MoveAllChildren(nsINode& aContainer,
                                     const EditorRawDOMPoint& aPointToInsert) {
  if (!aContainer.HasChildren()) {
    return NS_OK;
  }
  nsIContent* const firstChild = aContainer.GetFirstChild();
  if (NS_WARN_IF(!firstChild)) {
    return NS_ERROR_FAILURE;
  }
  nsIContent* const lastChild = aContainer.GetLastChild();
  if (NS_WARN_IF(!lastChild)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = MoveChildrenBetween(
      // MOZ_KnownLive is okay for here because MoveChildrenBetween() will grab
      // all children with an array of strong pointer.  Therefore, we don't need
      // to guarantee the lifetime of firstChild and lastChild here unless we'd
      // start to refer after this call.
      MOZ_KnownLive(*firstChild), MOZ_KnownLive(*lastChild), aPointToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::MoveChildrenBetween() failed");
  return rv;
}

nsresult HTMLEditor::MoveChildrenBetween(
    nsIContent& aFirstChild, nsIContent& aLastChild,
    const EditorRawDOMPoint& aPointToInsert) {
  nsCOMPtr<nsINode> oldContainer = aFirstChild.GetParentNode();
  if (NS_WARN_IF(oldContainer != aLastChild.GetParentNode()) ||
      NS_WARN_IF(!aPointToInsert.IsInContentNode()) ||
      NS_WARN_IF(!aPointToInsert.CanContainerHaveChildren())) {
    return NS_ERROR_INVALID_ARG;
  }

  // First, store all children which should be moved to the new container.
  AutoTArray<nsCOMPtr<nsIContent>, 10> children;
  for (nsIContent* child = &aFirstChild; child;
       child = child->GetNextSibling()) {
    children.AppendElement(child);
    if (child == &aLastChild) {
      break;
    }
  }

  if (NS_WARN_IF(children.LastElement() != &aLastChild)) {
    return NS_ERROR_INVALID_ARG;
  }

  const nsCOMPtr<nsIContent> newContainer =
      aPointToInsert.ContainerAs<nsIContent>();
  nsCOMPtr<nsIContent> nextNode = aPointToInsert.GetChild();
  for (size_t i = children.Length(); i > 0; --i) {
    nsCOMPtr<nsIContent>& child = children[i - 1];
    if (child->GetParentNode() != oldContainer) {
      // If the child has been moved to different container, we shouldn't
      // touch it.
      continue;
    }
    if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*child))) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    {
      nsresult rv = AutoNodeAPIWrapper(*this, *oldContainer)
                        .RemoveChild(MOZ_KnownLive(*child));
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoNodeAPIWrapper::RemoveChild() failed");
        return rv;
      }
    }
    if (NS_WARN_IF(nextNode && nextNode->GetParentNode() != newContainer) ||
        NS_WARN_IF(newContainer->IsInComposedDoc() &&
                   !HTMLEditUtils::IsSimplyEditableNode(*newContainer))) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    {
      nsresult rv = AutoNodeAPIWrapper(*this, *newContainer)
                        .InsertBefore(MOZ_KnownLive(*child), nextNode);
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoNodeAPIWrapper::InsertBefore() failed");
        return rv;
      }
    }
    // If the child was inserted or appended properly, the following children
    // should be inserted before it.  Otherwise, keep using current position.
    if (child->GetParentNode() == newContainer) {
      nextNode = child;
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::MovePreviousSiblings(
    nsIContent& aChild, const EditorRawDOMPoint& aPointToInsert) {
  if (NS_WARN_IF(!aChild.GetParentNode())) {
    return NS_ERROR_INVALID_ARG;
  }
  nsIContent* const firstChild = aChild.GetParentNode()->GetFirstChild();
  if (NS_WARN_IF(!firstChild)) {
    return NS_ERROR_FAILURE;
  }
  nsIContent* const lastChild =
      &aChild == firstChild ? firstChild : aChild.GetPreviousSibling();
  if (NS_WARN_IF(!lastChild)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = MoveChildrenBetween(
      // MOZ_KnownLive is okay for here because MoveChildrenBetween() will grab
      // all children with an array of strong pointer.  Therefore, we don't need
      // to guarantee the lifetime of firstChild and lastChild here unless we'd
      // start to refer after this call.
      MOZ_KnownLive(*firstChild), MOZ_KnownLive(*lastChild), aPointToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::MoveChildrenBetween() failed");
  return rv;
}

nsresult HTMLEditor::MoveInclusiveNextSiblings(
    nsIContent& aChild, const EditorRawDOMPoint& aPointToInsert) {
  if (NS_WARN_IF(!aChild.GetParentNode())) {
    return NS_ERROR_INVALID_ARG;
  }
  nsIContent* const lastChild = aChild.GetParentNode()->GetLastChild();
  if (NS_WARN_IF(!lastChild)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = MoveChildrenBetween(
      // MOZ_KnownLive is okay for here because MoveChildrenBetween() will grab
      // all children with an array of strong pointer.  Therefore, we don't need
      // to guarantee the lifetime of lastChild here unless we'd start to refer
      // after this call.
      aChild, MOZ_KnownLive(*lastChild), aPointToInsert);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::MoveChildrenBetween() failed");
  return rv;
}

Result<DeleteRangeResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoBlockElementsJoiner::DeleteContentButKeepTableStructure(
        HTMLEditor& aHTMLEditor, nsIContent& aContent) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  if (!HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
          aContent)) {
    nsCOMPtr<nsINode> parentNode = aContent.GetParentNode();
    if (NS_WARN_IF(!parentNode)) {
      return Err(NS_ERROR_FAILURE);
    }
    nsCOMPtr<nsIContent> nextSibling = aContent.GetNextSibling();
    nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(aContent);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    if (NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parentNode) ||
        NS_WARN_IF(!parentNode->IsInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return DeleteRangeResult(
        EditorDOMRange(nextSibling ? EditorDOMPoint(nextSibling)
                                   : EditorDOMPoint::AtEndOf(*parentNode)),
        EditorDOMPoint());
  }

  // XXX For performance, this should just call
  //     DeleteContentButKeepTableStructure() while there are children in
  //     aContent.  If we need to avoid infinite loop because mutation event
  //     listeners can add unexpected nodes into aContent, we should just loop
  //     only original count of the children.
  AutoTArray<OwningNonNull<nsIContent>, 10> childList;
  for (nsIContent* child = aContent.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    childList.AppendElement(*child);
  }

  for (const auto& child : childList) {
    // MOZ_KnownLive because 'childList' is guaranteed to
    // keep it alive.
    Result<DeleteRangeResult, nsresult> deleteChildResult =
        DeleteContentButKeepTableStructure(aHTMLEditor, MOZ_KnownLive(child));
    if (MOZ_UNLIKELY(deleteChildResult.isErr())) {
      NS_WARNING("HTMLEditor::DeleteContentButKeepTableStructure() failed");
      return deleteChildResult.propagateErr();
    }
    deleteChildResult.unwrap().IgnoreCaretPointSuggestion();
  }
  if (NS_WARN_IF(!aContent.IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // Insert a <br> into new empty table cell or caption because we don't have a
  // change to do it for the middle of the range.  Note that this does not
  // handle first cell/caption and end cell/caption at the deleting range. They
  // should be handled by upper level because we may need to delete unnecessary
  // new empty inline ancestors in the cells/captions.
  if (!HTMLEditUtils::IsTableCellOrCaptionElement(aContent) ||
      aContent.GetChildCount()) {
    return DeleteRangeResult(EditorDOMRange(EditorDOMPoint(&aContent, 0u),
                                            EditorDOMPoint::AtEndOf(aContent)),
                             EditorDOMPoint());
  }
  Result<CreateLineBreakResult, nsresult> insertLineBreakResultOrError =
      aHTMLEditor.InsertLineBreak(WithTransaction::Yes,
                                  LineBreakType::BRElement,
                                  EditorDOMPoint(&aContent, 0));
  if (MOZ_UNLIKELY(insertLineBreakResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertLineBreakResultOrError.propagateErr();
  }
  CreateLineBreakResult insertLineBreakResult =
      insertLineBreakResultOrError.unwrap();
  insertLineBreakResult.IgnoreCaretPointSuggestion();
  return DeleteRangeResult(EditorDOMRange(EditorDOMPoint(&aContent, 0u)),
                           EditorDOMPoint());
}

nsresult HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty(
    nsIContent& aContent) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // The element must be `<blockquote type="cite">` or
  // `<span _moz_quote="true">`.
  RefPtr<Element> mailCiteElement =
      GetMostDistantAncestorMailCiteElement(aContent);
  if (!mailCiteElement) {
    return NS_OK;
  }
  bool seenBR = false;
  if (!HTMLEditUtils::IsEmptyNode(
          *mailCiteElement,
          {EmptyCheckOption::TreatListItemAsVisible,
           EmptyCheckOption::TreatTableCellAsVisible,
           EmptyCheckOption::TreatNonEditableContentAsInvisible},
          &seenBR)) {
    return NS_OK;
  }
  EditorDOMPoint atEmptyMailCiteElement(mailCiteElement);
  {
    AutoEditorDOMPointChildInvalidator lockOffset(atEmptyMailCiteElement);
    nsresult rv = DeleteNodeWithTransaction(*mailCiteElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }

  if (!atEmptyMailCiteElement.IsSet() || !seenBR) {
    NS_WARNING_ASSERTION(
        atEmptyMailCiteElement.IsSet(),
        "Mutation event listener might changed the DOM tree during "
        "EditorBase::DeleteNodeWithTransaction(), but ignored");
    return NS_OK;
  }

  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      atEmptyMailCiteElement);
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertBRElementResultOrError.unwrapErr();
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  MOZ_ASSERT(insertBRElementResult.Handled());
  nsresult rv = insertBRElementResult.SuggestCaretPointTo(
      *this, {SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(rv == NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                       "CaretPoint::SuggestCaretPointTo() failed, but ignored");
  return NS_OK;
}

Element* HTMLEditor::AutoDeleteRangesHandler::AutoEmptyBlockAncestorDeleter::
    ScanEmptyBlockInclusiveAncestor(const HTMLEditor& aHTMLEditor,
                                    nsIContent& aStartContent) {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!mEmptyInclusiveAncestorBlockElement);

  // If we are inside an empty block, delete it.
  // Note: do NOT delete table elements this way.
  // Note: do NOT delete non-editable block element.
  Element* editableBlockElement = HTMLEditUtils::GetInclusiveAncestorElement(
      aStartContent, HTMLEditUtils::ClosestEditableBlockElement,
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (!editableBlockElement) {
    return nullptr;
  }
  // XXX Perhaps, this is slow loop.  If empty blocks are nested, then,
  //     each block checks whether it's empty or not.  However, descendant
  //     blocks are checked again and again by IsEmptyNode().  Perhaps, it
  //     should be able to take "known empty element" for avoiding same checks.
  while (editableBlockElement &&
         HTMLEditUtils::IsRemovableFromParentNode(*editableBlockElement) &&
         !HTMLEditUtils::IsAnyTableElementExceptColumnElement(
             *editableBlockElement) &&
         HTMLEditUtils::IsEmptyNode(*editableBlockElement)) {
    // If the removable empty list item is a child of editing host list element,
    // we should not delete it.
    if (HTMLEditUtils::IsListItemElement(*editableBlockElement)) {
      Element* const parentElement = editableBlockElement->GetParentElement();
      if (parentElement && HTMLEditUtils::IsListElement(*parentElement) &&
          !HTMLEditUtils::IsRemovableFromParentNode(*parentElement) &&
          HTMLEditUtils::IsEmptyNode(*parentElement)) {
        break;
      }
    }
    mEmptyInclusiveAncestorBlockElement = editableBlockElement;
    editableBlockElement = HTMLEditUtils::GetAncestorElement(
        *mEmptyInclusiveAncestorBlockElement,
        HTMLEditUtils::ClosestEditableBlockElement,
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
  }
  if (!mEmptyInclusiveAncestorBlockElement) {
    return nullptr;
  }

  // XXX Because of not checking whether found block element is editable
  //     in the above loop, empty ediable block element may be overwritten
  //     with empty non-editable clock element.  Therefore, we fail to
  //     remove the found empty nodes.
  if (NS_WARN_IF(!mEmptyInclusiveAncestorBlockElement->IsEditable()) ||
      NS_WARN_IF(!mEmptyInclusiveAncestorBlockElement->GetParentElement())) {
    mEmptyInclusiveAncestorBlockElement = nullptr;
  }
  return mEmptyInclusiveAncestorBlockElement;
}

nsresult HTMLEditor::AutoDeleteRangesHandler::AutoEmptyBlockAncestorDeleter::
    ComputeTargetRanges(const HTMLEditor& /*aHTMLEditor*/,
                        nsIEditor::EDirection aDirectionAndAmount,
                        const Element& aEditingHost,
                        AutoClonedSelectionRangeArray& aRangesToDelete) const {
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement);

  // We'll delete `mEmptyInclusiveAncestorBlockElement` node from the tree, but
  // we should return the range from start/end of next/previous editable content
  // to end/start of the element for compatiblity with the other browsers.
  switch (aDirectionAndAmount) {
    case nsIEditor::eNone:
      break;
    case nsIEditor::ePrevious:
    case nsIEditor::ePreviousWord:
    case nsIEditor::eToBeginningOfLine: {
      EditorRawDOMPoint startPoint =
          HTMLEditUtils::GetPreviousEditablePoint<EditorRawDOMPoint>(
              *mEmptyInclusiveAncestorBlockElement, &aEditingHost,
              // We'ill delete invisible white-spaces later.
              InvisibleWhiteSpaces::Ignore,
              // In this case, we won't join table cells so that we should
              // get a range which is in a table cell even if it's in a
              // table.
              TableBoundary::NoCrossAnyTableElement);
      if (!startPoint.IsSet()) {
        NS_WARNING(
            "HTMLEditUtils::GetPreviousEditablePoint() didn't return a valid "
            "point");
        return NS_ERROR_FAILURE;
      }
      nsresult rv = aRangesToDelete.SetStartAndEnd(
          startPoint,
          EditorRawDOMPoint::AtEndOf(mEmptyInclusiveAncestorBlockElement));
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "AutoClonedRangeArray::SetStartAndEnd() failed");
      return rv;
    }
    case nsIEditor::eNext:
    case nsIEditor::eNextWord:
    case nsIEditor::eToEndOfLine: {
      EditorRawDOMPoint endPoint =
          HTMLEditUtils::GetNextEditablePoint<EditorRawDOMPoint>(
              *mEmptyInclusiveAncestorBlockElement, &aEditingHost,
              // We'ill delete invisible white-spaces.
              InvisibleWhiteSpaces::Ignore,
              // In this case, we won't join table cells so that we should
              // get a range which is in a table cell even if it's in a
              // table.
              TableBoundary::NoCrossAnyTableElement);
      if (!endPoint.IsSet()) {
        NS_WARNING(
            "HTMLEditUtils::GetNextEditablePoint() didn't return a valid "
            "point");
        return NS_ERROR_FAILURE;
      }
      nsresult rv = aRangesToDelete.SetStartAndEnd(
          EditorRawDOMPoint(mEmptyInclusiveAncestorBlockElement, 0), endPoint);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "AutoClonedRangeArray::SetStartAndEnd() failed");
      return rv;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Handle the nsIEditor::EDirection value");
      break;
  }
  // No direction, let's select the element to be deleted.
  nsresult rv =
      aRangesToDelete.SelectNode(*mEmptyInclusiveAncestorBlockElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedRangeArray::SelectNode() failed");
  return rv;
}

Result<CreateLineBreakResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::AutoEmptyBlockAncestorDeleter::
    MaybeInsertBRElementBeforeEmptyListItemElement(HTMLEditor& aHTMLEditor) {
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement);
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement->GetParentElement());
  MOZ_ASSERT(
      HTMLEditUtils::IsListItemElement(*mEmptyInclusiveAncestorBlockElement));

  // If the found empty block is a list item element and its grand parent
  // (i.e., parent of list element) is NOT a list element, insert <br>
  // element before the list element which has the empty list item.
  // This odd list structure may occur if `Document.execCommand("indent")`
  // is performed for list items.
  // XXX Chrome does not remove empty list elements when last content in
  //     last list item is deleted.  We should follow it since current
  //     behavior is annoying when you type new list item with selecting
  //     all list items.
  if (!HTMLEditUtils::IsFirstChild(
          *mEmptyInclusiveAncestorBlockElement,
          {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    return CreateLineBreakResult::NotHandled();
  }

  const EditorDOMPoint atParentOfEmptyListItem(
      mEmptyInclusiveAncestorBlockElement->GetParentElement());
  if (NS_WARN_IF(!atParentOfEmptyListItem.IsInContentNode())) {
    return Err(NS_ERROR_FAILURE);
  }
  if (HTMLEditUtils::IsListElement(
          *atParentOfEmptyListItem.ContainerAs<nsIContent>())) {
    return CreateLineBreakResult::NotHandled();
  }
  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      aHTMLEditor.InsertLineBreak(WithTransaction::Yes,
                                  LineBreakType::BRElement,
                                  atParentOfEmptyListItem);
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertBRElementResultOrError.propagateErr();
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  nsresult rv = insertBRElementResult.SuggestCaretPointTo(
      aHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  MOZ_ASSERT(insertBRElementResult.Handled());
  return std::move(insertBRElementResult);
}

Result<CaretPoint, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoEmptyBlockAncestorDeleter::GetNewCaretPosition(
        const HTMLEditor& aHTMLEditor,
        nsIEditor::EDirection aDirectionAndAmount,
        const Element& aEditingHost) const {
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement);
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement->GetParentElement());
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  switch (aDirectionAndAmount) {
    case nsIEditor::eNext:
    case nsIEditor::eNextWord:
    case nsIEditor::eToEndOfLine: {
      // Collapse Selection to next node of after empty block element
      // if there is.  Otherwise, to just after the empty block.
      nsIContent* const nextContentOfEmptyBlock = [&]() -> nsIContent* {
        for (EditorRawDOMPoint scanStartPoint =
                 EditorRawDOMPoint::After(mEmptyInclusiveAncestorBlockElement);
             scanStartPoint.IsInContentNode();) {
          nsIContent* const nextContent = HTMLEditUtils::GetNextLeafContent(
              scanStartPoint, {}, BlockInlineCheck::Auto, &aEditingHost);
          // Let's ignore invisible `Text`.
          if (nextContent && nextContent->IsText() &&
              !HTMLEditUtils::IsVisibleTextNode(
                  *nextContent->AsText(), TreatInvisibleLineBreakAs::Visible)) {
            scanStartPoint = EditorRawDOMPoint::After(*nextContent);
            continue;
          }
          return nextContent;
        }
        return nullptr;
      }();
      if (nextContentOfEmptyBlock) {
        EditorDOMPoint pt = HTMLEditUtils::GetGoodCaretPointFor<EditorDOMPoint>(
            *nextContentOfEmptyBlock, aDirectionAndAmount);
        if (!pt.IsSet()) {
          NS_WARNING("HTMLEditUtils::GetGoodCaretPointFor() failed");
          return Err(NS_ERROR_FAILURE);
        }
        return CaretPoint(std::move(pt));
      }
      EditorDOMPoint afterEmptyBlock =
          EditorDOMPoint::After(mEmptyInclusiveAncestorBlockElement);
      if (NS_WARN_IF(!afterEmptyBlock.IsSet())) {
        return Err(NS_ERROR_FAILURE);
      }
      return CaretPoint(std::move(afterEmptyBlock));
    }
    case nsIEditor::ePrevious:
    case nsIEditor::ePreviousWord:
    case nsIEditor::eToBeginningOfLine: {
      // Collapse Selection to previous editable node of the empty block
      // if there is.
      nsIContent* const previousContentOfEmptyBlock = [&]() -> nsIContent* {
        for (EditorRawDOMPoint scanStartPoint =
                 EditorRawDOMPoint(mEmptyInclusiveAncestorBlockElement);
             scanStartPoint.IsInContentNode();) {
          nsIContent* const previousContent =
              HTMLEditUtils::GetPreviousLeafContent(
                  scanStartPoint, {LeafNodeOption::IgnoreNonEditableNode},
                  BlockInlineCheck::Auto, &aEditingHost);
          // Let's ignore invisible `Text`.
          if (previousContent && previousContent->IsText() &&
              !HTMLEditUtils::IsVisibleTextNode(
                  *previousContent->AsText(),
                  TreatInvisibleLineBreakAs::Visible)) {
            scanStartPoint = EditorRawDOMPoint(previousContent, 0u);
            continue;
          }
          return previousContent;
        }
        return nullptr;
      }();
      if (previousContentOfEmptyBlock) {
        const EditorRawDOMPoint atEndOfPreviousContent =
            HTMLEditUtils::GetGoodCaretPointFor<EditorRawDOMPoint>(
                *previousContentOfEmptyBlock, aDirectionAndAmount);
        if (!atEndOfPreviousContent.IsSet()) {
          NS_WARNING("HTMLEditUtils::GetGoodCaretPointFor() failed");
          return Err(NS_ERROR_FAILURE);
        }
        // If the previous content is between a preceding line break and the
        // block boundary of current empty block, let's move caret to the line
        // break if there is no visible things between them.
        const Maybe<EditorRawLineBreak> precedingLineBreak =
            HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem<
                EditorRawLineBreak>(atEndOfPreviousContent, aEditingHost);
        return precedingLineBreak.isSome()
                   ? CaretPoint(precedingLineBreak->To<EditorDOMPoint>())
                   : CaretPoint(atEndOfPreviousContent.To<EditorDOMPoint>());
      }
      // Otherwise, let's put caret next to the deleting block.
      auto afterEmptyBlock =
          EditorDOMPoint::After(*mEmptyInclusiveAncestorBlockElement);
      if (NS_WARN_IF(!afterEmptyBlock.IsSet())) {
        return Err(NS_ERROR_FAILURE);
      }
      return CaretPoint(std::move(afterEmptyBlock));
    }
    case nsIEditor::eNone: {
      // Collapse selection at the removing block when we are replacing
      // selected content.
      EditorDOMPoint atEmptyBlock(mEmptyInclusiveAncestorBlockElement);
      if (NS_WARN_IF(!atEmptyBlock.IsSet())) {
        return Err(NS_ERROR_FAILURE);
      }
      return CaretPoint(std::move(atEmptyBlock));
    }
    default:
      MOZ_CRASH(
          "AutoEmptyBlockAncestorDeleter doesn't support this action yet");
      return Err(NS_ERROR_FAILURE);
  }
}

Result<DeleteRangeResult, nsresult>
HTMLEditor::AutoDeleteRangesHandler::AutoEmptyBlockAncestorDeleter::Run(
    HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    const Element& aEditingHost) {
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement);
  MOZ_ASSERT(mEmptyInclusiveAncestorBlockElement->GetParentElement());
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  {
    Result<DeleteRangeResult, nsresult> replaceSubListResultOrError =
        MaybeReplaceSubListWithNewListItem(aHTMLEditor);
    if (MOZ_UNLIKELY(replaceSubListResultOrError.isErr())) {
      NS_WARNING(
          "AutoEmptyBlockAncestorDeleter::MaybeReplaceSubListWithNewListItem() "
          "failed");
      return replaceSubListResultOrError.propagateErr();
    }
    if (replaceSubListResultOrError.inspect().Handled()) {
      return replaceSubListResultOrError;
    }
  }

  auto caretPointOrError = [&]() MOZ_CAN_RUN_SCRIPT MOZ_NEVER_INLINE_DEBUG
      -> Result<CaretPoint, nsresult> {
    if (HTMLEditUtils::IsListItemElement(
            *mEmptyInclusiveAncestorBlockElement)) {
      Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
          MaybeInsertBRElementBeforeEmptyListItemElement(aHTMLEditor);
      if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
        NS_WARNING(
            "AutoEmptyBlockAncestorDeleter::"
            "MaybeInsertBRElementBeforeEmptyListItemElement() failed");
        return insertBRElementResultOrError.propagateErr();
      }
      CreateLineBreakResult insertBRElementResult =
          insertBRElementResultOrError.unwrap();
      // If a `<br>` element is inserted, caret should be moved to after it.
      // XXX This comment is wrong, we're suggesting the line break position...
      MOZ_ASSERT_IF(insertBRElementResult.Handled(),
                    insertBRElementResult->IsHTMLBRElement());
      insertBRElementResult.IgnoreCaretPointSuggestion();
      return CaretPoint(
          insertBRElementResult.Handled()
              ? insertBRElementResult.AtLineBreak<EditorDOMPoint>()
              : EditorDOMPoint());
    }
    Result<CaretPoint, nsresult> caretPointOrError =
        GetNewCaretPosition(aHTMLEditor, aDirectionAndAmount, aEditingHost);
    NS_WARNING_ASSERTION(
        caretPointOrError.isOk(),
        "AutoEmptyBlockAncestorDeleter::GetNewCaretPosition() failed");
    MOZ_ASSERT_IF(caretPointOrError.isOk(),
                  caretPointOrError.inspect().HasCaretPointSuggestion());
    return caretPointOrError;
  }();
  if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
    return caretPointOrError.propagateErr();
  }
  EditorDOMPoint pointToPutCaret =
      caretPointOrError.unwrap().UnwrapCaretPoint();
  const bool unwrapAncestorBlocks =
      !HTMLEditUtils::IsListItemElement(*mEmptyInclusiveAncestorBlockElement) &&
      pointToPutCaret.GetContainer() ==
          mEmptyInclusiveAncestorBlockElement->GetParentNode();
  EditorDOMPoint atEmptyInclusiveAncestorBlockElement(
      mEmptyInclusiveAncestorBlockElement);
  {
    AutoTrackDOMPoint trackEmptyBlockPoint(
        aHTMLEditor.RangeUpdaterRef(), &atEmptyInclusiveAncestorBlockElement);
    AutoTrackDOMPoint trackPointToPutCaret(aHTMLEditor.RangeUpdaterRef(),
                                           &pointToPutCaret);
    Result<CaretPoint, nsresult> caretPointOrError =
        WhiteSpaceVisibilityKeeper::DeleteContentNodeAndJoinTextNodesAroundIt(
            aHTMLEditor, MOZ_KnownLive(*mEmptyInclusiveAncestorBlockElement),
            pointToPutCaret, aEditingHost);
    if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::"
          "DeleteContentNodeAndJoinTextNodesAroundIt() failed");
      return caretPointOrError.propagateErr();
    }
    trackPointToPutCaret.Flush(StopTracking::Yes);
    caretPointOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    trackEmptyBlockPoint.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!atEmptyInclusiveAncestorBlockElement
                        .IsInContentNodeAndValidInComposedDoc()) ||
        NS_WARN_IF(pointToPutCaret.IsSet() &&
                   !pointToPutCaret.IsInContentNodeAndValidInComposedDoc())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }
  EditorDOMPoint pointToInsertLineBreak =
      std::move(atEmptyInclusiveAncestorBlockElement);
  DeleteRangeResult deleteNodeResult(pointToInsertLineBreak,
                                     std::move(pointToPutCaret));
  if ((aHTMLEditor.IsMailEditor() || aHTMLEditor.IsPlaintextMailComposer()) &&
      MOZ_LIKELY(pointToInsertLineBreak.IsInContentNode())) {
    AutoTrackDOMDeleteRangeResult trackDeleteNodeResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteNodeResult);
    AutoTrackDOMPoint trackPointToInsertLineBreak(aHTMLEditor.RangeUpdaterRef(),
                                                  &pointToInsertLineBreak);
    nsresult rv = aHTMLEditor.DeleteMostAncestorMailCiteElementIfEmpty(
        MOZ_KnownLive(*pointToInsertLineBreak.ContainerAs<nsIContent>()));
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::DeleteMostAncestorMailCiteElementIfEmpty() failed");
      deleteNodeResult.IgnoreCaretPointSuggestion();
      return Err(rv);
    }
    trackPointToInsertLineBreak.Flush(StopTracking::Yes);
    if (NS_WARN_IF(!pointToInsertLineBreak.IsSetAndValidInComposedDoc())) {
      deleteNodeResult.IgnoreCaretPointSuggestion();
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    trackDeleteNodeResult.Flush(StopTracking::Yes);
    deleteNodeResult |= DeleteRangeResult(
        EditorDOMRange(pointToInsertLineBreak), EditorDOMPoint());
  }
  if (unwrapAncestorBlocks && aHTMLEditor.GetTopLevelEditSubAction() ==
                                  EditSubAction::eDeleteSelectedContent) {
    AutoTrackDOMDeleteRangeResult trackDeleteNodeResult(
        aHTMLEditor.RangeUpdaterRef(), &deleteNodeResult);
    Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
        aHTMLEditor.InsertPaddingBRElementIfNeeded(
            pointToInsertLineBreak,
            aEditingHost.IsContentEditablePlainTextOnly() ? nsIEditor::eNoStrip
                                                          : nsIEditor::eStrip,
            aEditingHost);
    if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
      NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
      deleteNodeResult.IgnoreCaretPointSuggestion();
      return insertPaddingBRElementOrError.propagateErr();
    }
    insertPaddingBRElementOrError.unwrap().IgnoreCaretPointSuggestion();
  }
  MOZ_ASSERT(deleteNodeResult.Handled());
  return std::move(deleteNodeResult);
}

Result<DeleteRangeResult, nsresult> HTMLEditor::AutoDeleteRangesHandler::
    AutoEmptyBlockAncestorDeleter::MaybeReplaceSubListWithNewListItem(
        HTMLEditor& aHTMLEditor) {
  // If we're deleting sublist element and it's the last list item of its parent
  // list, we should replace it with a list element.
  if (!HTMLEditUtils::IsListElement(mEmptyInclusiveAncestorBlockElement)) {
    return DeleteRangeResult::IgnoredResult();
  }
  const RefPtr<Element> parentElement =
      mEmptyInclusiveAncestorBlockElement->GetParentElement();
  if (!HTMLEditUtils::IsListElement(parentElement) ||
      !HTMLEditUtils::IsEmptyNode(
          *parentElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    return DeleteRangeResult::IgnoredResult();
  }

  const nsCOMPtr<nsINode> nextSibling =
      mEmptyInclusiveAncestorBlockElement->GetNextSibling();
  nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(
      MOZ_KnownLive(*mEmptyInclusiveAncestorBlockElement));
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return Err(rv);
  }
  if (NS_WARN_IF(nextSibling &&
                 nextSibling->GetParentNode() != parentElement) ||
      NS_WARN_IF(!parentElement->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  const auto pointAtDeletedNode = nextSibling
                                      ? EditorDOMPoint(nextSibling)
                                      : EditorDOMPoint::AtEndOf(*parentElement);
  auto deleteNodeResult =
      DeleteRangeResult(EditorDOMRange(pointAtDeletedNode), EditorDOMPoint());
  AutoTrackDOMDeleteRangeResult trackDeleteNodeResult(
      aHTMLEditor.RangeUpdaterRef(), &deleteNodeResult);
  Result<CreateElementResult, nsresult> insertListItemResultOrError =
      aHTMLEditor.CreateAndInsertElement(
          WithTransaction::Yes,
          parentElement->IsHTMLElement(nsGkAtoms::dl) ? *nsGkAtoms::dd
                                                      : *nsGkAtoms::li,
          pointAtDeletedNode,
          [](HTMLEditor& aHTMLEditor, Element& aNewElement,
             const EditorDOMPoint& /*aPointToInsert*/) -> nsresult {
            RefPtr<Element> brElement =
                aHTMLEditor.CreateHTMLContent(nsGkAtoms::br);
            if (MOZ_UNLIKELY(!brElement)) {
              NS_WARNING(
                  "EditorBase::CreateHTMLContent(nsGkAtoms::br) failed, but "
                  "ignored");
              return NS_OK;  // Just gives up to insert <br>
            }
            IgnoredErrorResult error;
            aNewElement.AppendChild(*brElement, error);
            NS_WARNING_ASSERTION(!error.Failed(),
                                 "nsINode::AppendChild() failed, but ignored");
            return NS_OK;
          });
  if (MOZ_UNLIKELY(insertListItemResultOrError.isErr())) {
    NS_WARNING("HTMLEditor::CreateAndInsertElement() failed");
    deleteNodeResult.IgnoreCaretPointSuggestion();
    return insertListItemResultOrError.propagateErr();
  }
  trackDeleteNodeResult.Flush(StopTracking::Yes);
  CreateElementResult insertListItemResult =
      insertListItemResultOrError.unwrap();
  insertListItemResult.IgnoreCaretPointSuggestion();
  deleteNodeResult |=
      CaretPoint(EditorDOMPoint(insertListItemResult.GetNewNode(), 0u));
  MOZ_ASSERT(deleteNodeResult.Handled());
  return std::move(deleteNodeResult);
}

template <typename EditorDOMRangeType>
Result<EditorRawDOMRange, nsresult>
HTMLEditor::AutoDeleteRangesHandler::ExtendOrShrinkRangeToDelete(
    const HTMLEditor& aHTMLEditor,
    const LimitersAndCaretData& aLimitersAndCaretData,
    const EditorDOMRangeType& aRangeToDelete,
    SelectionWasCollapsed aSelectionWasCollapsed,
    ComputeRangeFor aComputeRangeFor, const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());
  MOZ_ASSERT(!aRangeToDelete.Collapsed());
  MOZ_ASSERT(aRangeToDelete.IsPositioned());

  const nsIContent* commonAncestor = nsIContent::FromNodeOrNull(
      nsContentUtils::GetClosestCommonInclusiveAncestor(
          aRangeToDelete.StartRef().GetContainer(),
          aRangeToDelete.EndRef().GetContainer()));
  if (MOZ_UNLIKELY(NS_WARN_IF(!commonAncestor))) {
    return Err(NS_ERROR_FAILURE);
  }

  // Editing host may be nested and outer one could have focus.  Let's use
  // the closest editing host instead.
  const RefPtr<Element> closestEditingHost =
      aHTMLEditor.ComputeEditingHost(*commonAncestor, LimitInBodyElement::No);
  if (NS_WARN_IF(!closestEditingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  // Look for the common ancestor's block element in the editing host.  It's
  // fine that we get non-editable block element which is ancestor of inline
  // editing host because the following code checks editing host too.
  const RefPtr<Element> closestBlockAncestorOrInlineEditingHost = [&]() {
    // Note that if non-closest editing host has focus, found block may be
    // non-editable.
    if (Element* const maybeEditableBlockElement =
            HTMLEditUtils::GetInclusiveAncestorElement(
                *commonAncestor, HTMLEditUtils::ClosestBlockElement,
                BlockInlineCheck::UseComputedDisplayStyle,
                closestEditingHost)) {
      return maybeEditableBlockElement;
    }
    return closestEditingHost.get();
  }();

  // Set up for loops and cache our root element
  // If only one list element is selected, and if the list element is empty,
  // we should delete only the list element.  Or if the list element is not
  // empty, we should make the list has only one empty list item element.
  if (const Element* maybeListElement =
          HTMLEditUtils::GetElementIfOnlyOneSelected(aRangeToDelete)) {
    if (HTMLEditUtils::IsListElement(*maybeListElement) &&
        !HTMLEditUtils::IsEmptyAnyListElement(*maybeListElement)) {
      EditorRawDOMRange range =
          HTMLEditUtils::GetRangeSelectingAllContentInAllListItems<
              EditorRawDOMRange>(*maybeListElement);
      if (range.IsPositioned()) {
        if (EditorUtils::IsEditableContent(
                *range.StartRef().ContainerAs<nsIContent>(),
                EditorType::HTML) &&
            EditorUtils::IsEditableContent(
                *range.EndRef().ContainerAs<nsIContent>(), EditorType::HTML)) {
          return range;
        }
      }
      // If the first and/or last list item is not editable, we need to do more
      // complicated things probably, but we just delete the list element with
      // invisible things around it for now since it must be rare case.
    }
    // Otherwise, if the list item is empty, we should delete it with invisible
    // things around it.
  }

  // Find previous visible things before start of selection
  EditorRawDOMRange rangeToDelete(aRangeToDelete);
  if (rangeToDelete.StartRef().GetContainer() !=
      closestBlockAncestorOrInlineEditingHost) {
    for (;;) {
      const WSScanResult backwardScanFromStartResult =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
              {WSRunScanner::Option::OnlyEditableNodes},
              rangeToDelete.StartRef());
      if (!backwardScanFromStartResult.ReachedCurrentBlockBoundary() &&
          !backwardScanFromStartResult.ReachedInlineEditingHostBoundary()) {
        break;
      }
      // We want to keep looking up.  But stop if we are crossing table
      // element boundaries, or if we hit the root.
      if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(
              *backwardScanFromStartResult.GetContent()) ||
          backwardScanFromStartResult.GetContent() ==
              closestBlockAncestorOrInlineEditingHost ||
          backwardScanFromStartResult.GetContent() == closestEditingHost) {
        break;
      }
      // Don't cross list element boundary because we don't want to delete list
      // element at start position unless it's empty.
      if (HTMLEditUtils::IsListElement(
              *backwardScanFromStartResult.GetContent()) &&
          !HTMLEditUtils::IsEmptyAnyListElement(
              *backwardScanFromStartResult.ElementPtr())) {
        break;
      }
      // Don't cross flex-item/grid-item boundary to make new content inserted
      // into it.
      if (backwardScanFromStartResult.ContentIsElement() &&
          HTMLEditUtils::IsFlexOrGridItem(
              *backwardScanFromStartResult.ElementPtr())) {
        break;
      }
      rangeToDelete.SetStart(backwardScanFromStartResult
                                 .PointAtReachedContent<EditorRawDOMPoint>());
    }
    if (!aLimitersAndCaretData.NodeIsInLimiters(
            rangeToDelete.StartRef().GetContainer())) {
      NS_WARNING("Computed start container was out of selection limiter");
      return Err(NS_ERROR_FAILURE);
    }
  }

  // Expand selection endpoint only if we don't pass an invisible `<br>`, or if
  // we really needed to pass that `<br>` (i.e., its block is now totally
  // selected).

  // Find next visible things after end of selection
  EditorDOMPoint atFirstInvisibleBRElement;
  if (rangeToDelete.EndRef().GetContainer() !=
      closestBlockAncestorOrInlineEditingHost) {
    for (;;) {
      const WSScanResult nextThingAfterEndBoundary =
          HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
              rangeToDelete.EndRef(), TreatAsUnnecessaryLineBreak{},
              aEditingHost, closestBlockAncestorOrInlineEditingHost);
      if (nextThingAfterEndBoundary.ReachedLineBreak()) {
        break;
      }
      if (nextThingAfterEndBoundary.MaybeIgnoredLineBreak().isSome() &&
          nextThingAfterEndBoundary.MaybeIgnoredLineBreak()
              ->IsInclusiveDescendantOf(aEditingHost)) {
        if (!atFirstInvisibleBRElement.IsSet()) {
          atFirstInvisibleBRElement =
              rangeToDelete.EndRef().To<EditorDOMPoint>();
        }
        rangeToDelete.SetEnd(nextThingAfterEndBoundary.MaybeIgnoredLineBreak()
                                 ->After<EditorRawDOMPoint>());
      }

      if (nextThingAfterEndBoundary.ReachedOutsideEditingHost()) {
        break;
      }

      if (nextThingAfterEndBoundary.ReachedCurrentBlockBoundary() ||
          nextThingAfterEndBoundary.ReachedInlineEditingHostBoundary()) {
        // We want to keep looking up.  But stop if we are crossing table
        // element boundaries, or if we hit the root.
        if (HTMLEditUtils::IsAnyTableElementExceptColumnElement(
                *nextThingAfterEndBoundary.GetContent()) ||
            nextThingAfterEndBoundary.GetContent() ==
                closestBlockAncestorOrInlineEditingHost) {
          break;
        }
        // Don't cross flex-item/grid-item boundary to make new content inserted
        // into it.
        if (HTMLEditUtils::IsFlexOrGridItem(
                *nextThingAfterEndBoundary.ElementPtr())) {
          break;
        }
        rangeToDelete.SetEnd(
            nextThingAfterEndBoundary
                .PointAfterReachedContent<EditorRawDOMPoint>());
        continue;
      }

      break;
    }

    if (!aLimitersAndCaretData.NodeIsInLimiters(
            rangeToDelete.EndRef().GetContainer())) {
      NS_WARNING("Computed end container was out of selection limiter");
      return Err(NS_ERROR_FAILURE);
    }
  }

  // If range boundaries are in list element, and the positions are very
  // start/end of first/last list item, we may need to shrink the ranges for
  // preventing to remove only all list item elements.
  if (aSelectionWasCollapsed != SelectionWasCollapsed::Yes) {
    EditorRawDOMRange rangeToDeleteListOrLeaveOneEmptyListItem =
        AutoDeleteRangesHandler::
            GetRangeToAvoidDeletingAllListItemsIfSelectingAllOverListElements(
                rangeToDelete, aComputeRangeFor);
    if (rangeToDeleteListOrLeaveOneEmptyListItem.IsPositioned()) {
      rangeToDelete = std::move(rangeToDeleteListOrLeaveOneEmptyListItem);
    }
  }

  if (atFirstInvisibleBRElement.IsInContentNode()) {
    // Find block node containing invisible `<br>` element.
    if (const RefPtr<const Element> editableBlockContainingBRElement =
            HTMLEditUtils::GetInclusiveAncestorElement(
                *atFirstInvisibleBRElement.ContainerAs<nsIContent>(),
                HTMLEditUtils::ClosestEditableBlockElement,
                BlockInlineCheck::UseComputedDisplayStyle)) {
      if (rangeToDelete.Contains(
              EditorRawDOMPoint(editableBlockContainingBRElement))) {
        return rangeToDelete;
      }
      // Otherwise, the new range should end at the invisible `<br>`.
      if (!aLimitersAndCaretData.NodeIsInLimiters(
              atFirstInvisibleBRElement.GetContainer())) {
        NS_WARNING(
            "Computed end container (`<br>` element) was out of selection "
            "limiter");
        return Err(NS_ERROR_FAILURE);
      }
      rangeToDelete.SetEnd(atFirstInvisibleBRElement);
    }
  }

  return rangeToDelete;
}

// static
EditorRawDOMRange HTMLEditor::AutoDeleteRangesHandler::
    GetRangeToAvoidDeletingAllListItemsIfSelectingAllOverListElements(
        const EditorRawDOMRange& aRangeToDelete,
        ComputeRangeFor aComputeRangeFor) {
  MOZ_ASSERT(aRangeToDelete.IsPositionedAndValid());

  auto GetDeepestEditableStartPointOfList = [](Element& aListElement) {
    Element* const firstListItemElement =
        HTMLEditUtils::GetFirstListItemElement(aListElement);
    if (MOZ_UNLIKELY(!firstListItemElement)) {
      return EditorRawDOMPoint();
    }
    if (MOZ_UNLIKELY(!EditorUtils::IsEditableContent(*firstListItemElement,
                                                     EditorType::HTML))) {
      return EditorRawDOMPoint(firstListItemElement);
    }
    return HTMLEditUtils::GetDeepestEditableStartPointOf<EditorRawDOMPoint>(
        *firstListItemElement,
        {EditablePointOption::RecognizeInvisibleWhiteSpaces,
         EditablePointOption::StopAtComment});
  };

  auto GetDeepestEditableEndPointOfList = [](Element& aListElement) {
    Element* const lastListItemElement =
        HTMLEditUtils::GetLastListItemElement(aListElement);
    if (MOZ_UNLIKELY(!lastListItemElement)) {
      return EditorRawDOMPoint();
    }
    if (MOZ_UNLIKELY(!EditorUtils::IsEditableContent(*lastListItemElement,
                                                     EditorType::HTML))) {
      return EditorRawDOMPoint::After(*lastListItemElement);
    }
    return HTMLEditUtils::GetDeepestEditableEndPointOf<EditorRawDOMPoint>(
        *lastListItemElement,
        {EditablePointOption::RecognizeInvisibleWhiteSpaces,
         EditablePointOption::StopAtComment});
  };

  Element* const startListElement =
      aRangeToDelete.StartRef().IsInContentNode()
          ? HTMLEditUtils::GetClosestInclusiveAncestorAnyListElement(
                *aRangeToDelete.StartRef().ContainerAs<nsIContent>())
          : nullptr;
  Element* const endListElement = [&]() MOZ_NEVER_INLINE_DEBUG -> Element* {
    // The range may have been extended to after a list element to delete the
    // list. If so, let's return the list element immediately before the end
    // boundary.
    if (nsIContent* const previousSibling =
            aRangeToDelete.EndRef().GetPreviousSiblingOfChild()) {
      if (HTMLEditUtils::IsListElement(*previousSibling)) {
        return previousSibling->AsElement();
      }
    }
    // Otherwise, the range may end almost end of a list element. If so, let's
    // return the inclusive ancestor list element.
    if (aRangeToDelete.EndRef().IsInContentNode()) {
      Element* const listElement =
          HTMLEditUtils::GetClosestInclusiveAncestorAnyListElement(
              *aRangeToDelete.EndRef().ContainerAs<nsIContent>());
      if (listElement) {
        return listElement;
      }
    }
    return nullptr;
  }();
  if (!startListElement && !endListElement) {
    return EditorRawDOMRange();
  }

  // FIXME: If there are invalid children, we cannot handle first/last list item
  // elements properly.  In that case, we should treat list elements and list
  // item elements as normal block elements.
  if (startListElement &&
      NS_WARN_IF(!HTMLEditUtils::IsValidListElement(
          *startListElement, HTMLEditUtils::TreatSubListElementAs::Valid))) {
    return EditorRawDOMRange();
  }
  if (endListElement && startListElement != endListElement &&
      NS_WARN_IF(!HTMLEditUtils::IsValidListElement(
          *endListElement, HTMLEditUtils::TreatSubListElementAs::Valid))) {
    return EditorRawDOMRange();
  }

  const bool startListElementIsEmpty =
      startListElement &&
      HTMLEditUtils::IsEmptyAnyListElement(*startListElement);
  const bool endListElementIsEmpty =
      startListElement == endListElement
          ? startListElementIsEmpty
          : endListElement &&
                HTMLEditUtils::IsEmptyAnyListElement(*endListElement);
  // If both list elements are empty, we should not shrink the range since
  // we want to delete the list.
  if (startListElementIsEmpty && endListElementIsEmpty) {
    return EditorRawDOMRange();
  }

  // There may be invisible white-spaces and there are elements in the
  // list items.  Therefore, we need to compare the deepest positions
  // and range boundaries.
  EditorRawDOMPoint deepestStartPointOfStartList =
      startListElement ? GetDeepestEditableStartPointOfList(*startListElement)
                       : EditorRawDOMPoint();
  EditorRawDOMPoint deepestEndPointOfEndList =
      endListElement ? GetDeepestEditableEndPointOfList(*endListElement)
                     : EditorRawDOMPoint();
  if (MOZ_UNLIKELY(!deepestStartPointOfStartList.IsSet() &&
                   !deepestEndPointOfEndList.IsSet())) {
    // FIXME: This does not work well if there is non-list-item contents in the
    // list elements.  Perhaps, for fixing this invalid cases, we need to wrap
    // the content into new list item like Chrome.
    return EditorRawDOMRange();
  }

  // We don't want to shrink the range into empty sublist.
  if (deepestStartPointOfStartList.IsSet()) {
    for (nsIContent* const maybeList :
         deepestStartPointOfStartList.GetContainer()
             ->InclusiveAncestorsOfType<nsIContent>()) {
      if (aRangeToDelete.StartRef().GetContainer() == maybeList) {
        break;
      }
      if (HTMLEditUtils::IsListElement(*maybeList) &&
          HTMLEditUtils::IsEmptyAnyListElement(*maybeList->AsElement())) {
        deepestStartPointOfStartList.Set(maybeList);
      }
    }
  }
  if (deepestEndPointOfEndList.IsSet()) {
    for (nsIContent* const maybeList :
         deepestEndPointOfEndList.GetContainer()
             ->InclusiveAncestorsOfType<nsIContent>()) {
      if (aRangeToDelete.EndRef().GetContainer() == maybeList) {
        break;
      }
      if (HTMLEditUtils::IsListElement(*maybeList) &&
          HTMLEditUtils::IsEmptyAnyListElement(*maybeList->AsElement())) {
        deepestEndPointOfEndList.SetAfter(maybeList);
      }
    }
  }

  const EditorRawDOMPoint deepestEndPointOfStartList =
      startListElement ? GetDeepestEditableEndPointOfList(*startListElement)
                       : EditorRawDOMPoint();
  MOZ_ASSERT_IF(deepestStartPointOfStartList.IsSet(),
                deepestEndPointOfStartList.IsSet());
  MOZ_ASSERT_IF(!deepestStartPointOfStartList.IsSet(),
                !deepestEndPointOfStartList.IsSet());

  const bool rangeStartsFromBeginningOfStartList =
      deepestStartPointOfStartList.IsSet() &&
      aRangeToDelete.StartRef().EqualsOrIsBefore(deepestStartPointOfStartList);
  const bool rangeEndsByEndingOfStartListOrLater =
      !deepestEndPointOfStartList.IsSet() ||
      deepestEndPointOfStartList.EqualsOrIsBefore(aRangeToDelete.EndRef());
  const bool rangeEndsByEndingOfEndList =
      deepestEndPointOfEndList.IsSet() &&
      deepestEndPointOfEndList.EqualsOrIsBefore(aRangeToDelete.EndRef());

  EditorRawDOMRange newRangeToDelete;
  // If all over the list element at start boundary is selected, we should
  // shrink the range to start from the first list item to avoid to delete
  // all list items.
  if (!startListElementIsEmpty && rangeStartsFromBeginningOfStartList &&
      rangeEndsByEndingOfStartListOrLater) {
    newRangeToDelete.SetStart(EditorRawDOMPoint(
        deepestStartPointOfStartList.ContainerAs<nsIContent>(), 0u));
  }
  // If all over the list element is selected, and...
  if (!endListElementIsEmpty && rangeEndsByEndingOfEndList) {
    // Let's default to end of the deepest editable point.
    newRangeToDelete.SetEnd(deepestEndPointOfEndList);
    MOZ_ASSERT_IF(newRangeToDelete.StartRef().IsSet(),
                  newRangeToDelete.IsPositionedAndValid());
    // Then, let's extend the range to select all the sublists if the range
    // contails all of them if we're deleting the range.
    if (aComputeRangeFor == ComputeRangeFor::ToDeleteTheRange) {
      for (Element* const maybeList :
           deepestEndPointOfEndList.GetContainer()
               ->InclusiveAncestorsOfType<Element>()) {
        if (!HTMLEditUtils::IsListElement(*maybeList)) {
          continue;
        }
        if (!aRangeToDelete.StartRef().IsBefore(
                EditorRawDOMPoint(maybeList, 0u))) {
          break;
        }
        MOZ_ASSERT(maybeList->IsInclusiveDescendantOf(endListElement));
        // The list is entirely in the range so that let's delete it.
        newRangeToDelete.SetEnd(EditorRawDOMPoint::After(*maybeList));
        MOZ_ASSERT_IF(newRangeToDelete.StartRef().IsSet(),
                      newRangeToDelete.IsPositionedAndValid());
      }
    }
  }

  if (!newRangeToDelete.StartRef().IsSet() &&
      !newRangeToDelete.EndRef().IsSet()) {
    return EditorRawDOMRange();
  }

  if (!newRangeToDelete.StartRef().IsSet()) {
    newRangeToDelete.SetStart(aRangeToDelete.StartRef());
    MOZ_ASSERT(newRangeToDelete.IsPositionedAndValid());
  }
  if (!newRangeToDelete.EndRef().IsSet()) {
    newRangeToDelete.SetEnd(aRangeToDelete.EndRef());
    MOZ_ASSERT(newRangeToDelete.IsPositionedAndValid());
  }

  return newRangeToDelete;
}

}  // namespace mozilla
