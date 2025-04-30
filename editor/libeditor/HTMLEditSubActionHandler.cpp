/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"
#include "HTMLEditorInlines.h"
#include "HTMLEditorNestedClasses.h"

#include <utility>

#include "AutoClonedRangeArray.h"
#include "AutoSelectionRestorer.h"
#include "CSSEditUtils.h"
#include "EditAction.h"
#include "EditorDOMPoint.h"
#include "EditorLineBreak.h"
#include "EditorUtils.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditUtils.h"
#include "PendingStyles.h"  // for SpecifiedStyle
#include "WhiteSpaceVisibilityKeeper.h"
#include "WSRunScanner.h"

#include "ErrorList.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/EditorForwards.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/TextComposition.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/RangeBinding.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"
#include "nsAtom.h"
#include "nsCRT.h"
#include "nsCRTGlue.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsLiteralString.h"
#include "nsPrintfCString.h"
#include "nsRange.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStyledElement.h"
#include "nsTArray.h"
#include "nsTextNode.h"
#include "nsThreadUtils.h"

class nsISupports;

namespace mozilla {

using namespace dom;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using EmptyCheckOptions = HTMLEditUtils::EmptyCheckOptions;
using LeafNodeType = HTMLEditUtils::LeafNodeType;
using LeafNodeTypes = HTMLEditUtils::LeafNodeTypes;
using WalkTextOption = HTMLEditUtils::WalkTextOption;
using WalkTreeDirection = HTMLEditUtils::WalkTreeDirection;
using WalkTreeOption = HTMLEditUtils::WalkTreeOption;

/********************************************************
 *  first some helpful functors we will use
 ********************************************************/

static bool IsPendingStyleCachePreservingSubAction(
    EditSubAction aEditSubAction) {
  switch (aEditSubAction) {
    case EditSubAction::eDeleteSelectedContent:
    case EditSubAction::eInsertLineBreak:
    case EditSubAction::eInsertParagraphSeparator:
    case EditSubAction::eCreateOrChangeList:
    case EditSubAction::eIndent:
    case EditSubAction::eOutdent:
    case EditSubAction::eSetOrClearAlignment:
    case EditSubAction::eCreateOrRemoveBlock:
    case EditSubAction::eFormatBlockForHTMLCommand:
    case EditSubAction::eMergeBlockContents:
    case EditSubAction::eRemoveList:
    case EditSubAction::eCreateOrChangeDefinitionListItem:
    case EditSubAction::eInsertElement:
    case EditSubAction::eInsertQuotation:
    case EditSubAction::eInsertQuotedText:
      return true;
    default:
      return false;
  }
}

template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMRange& aRange);
template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMRange& aRange);
template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint);
template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint);
template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMPoint& aStartPoint, const EditorRawDOMPoint& aEndPoint);
template already_AddRefed<nsRange>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMPoint& aStartPoint, const EditorRawDOMPoint& aEndPoint);

nsresult HTMLEditor::InitEditorContentAndSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // We should do nothing with the result of GetRoot() if only a part of the
  // document is editable.
  if (!EntireDocumentIsEditable()) {
    return NS_OK;
  }

  nsresult rv = MaybeCreatePaddingBRElementForEmptyEditor();
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::MaybeCreatePaddingBRElementForEmptyEditor() failed");
    return rv;
  }

  // If the selection hasn't been set up yet, set it up collapsed to the end of
  // our editable content.
  // XXX I think that this shouldn't do it in `HTMLEditor` because it maybe
  //     removed by the web app and if they call `Selection::AddRange()` without
  //     checking the range count, it may cause multiple selection ranges.
  if (!SelectionRef().RangeCount()) {
    nsresult rv = CollapseSelectionToEndOfLastLeafNodeOfDocument();
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::CollapseSelectionToEndOfLastLeafNodeOfDocument() "
          "failed");
      return rv;
    }
  }

  if (IsPlaintextMailComposer()) {
    // XXX Should we do this in HTMLEditor?  It's odd to guarantee that last
    //     empty line is visible only when it's in the plain text mode.
    nsresult rv = EnsurePaddingBRElementInMultilineEditor();
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::EnsurePaddingBRElementInMultilineEditor() failed");
      return rv;
    }
  }

  Element* bodyOrDocumentElement = GetRoot();
  if (NS_WARN_IF(!bodyOrDocumentElement && !GetDocument())) {
    return NS_ERROR_FAILURE;
  }

  if (!bodyOrDocumentElement) {
    return NS_OK;
  }

  // FIXME: This is odd to update the DOM for making users can put caret in
  // empty table cells and list items.  We should make it possible without
  // the hacky <br>.
  rv = InsertBRElementToEmptyListItemsAndTableCellsInRange(
      RawRangeBoundary(bodyOrDocumentElement, 0u),
      RawRangeBoundary(bodyOrDocumentElement,
                       bodyOrDocumentElement->GetChildCount()));
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::InsertBRElementToEmptyListItemsAndTableCellsInRange() "
      "failed, but ignored");
  return NS_OK;
}

void HTMLEditor::OnStartToHandleTopLevelEditSubAction(
    EditSubAction aTopLevelEditSubAction,
    nsIEditor::EDirection aDirectionOfTopLevelEditSubAction, ErrorResult& aRv) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aRv.Failed());

  EditorBase::OnStartToHandleTopLevelEditSubAction(
      aTopLevelEditSubAction, aDirectionOfTopLevelEditSubAction, aRv);

  MOZ_ASSERT(GetTopLevelEditSubAction() == aTopLevelEditSubAction);
  MOZ_ASSERT(GetDirectionOfTopLevelEditSubAction() ==
             aDirectionOfTopLevelEditSubAction);

  if (NS_WARN_IF(Destroyed())) {
    aRv.Throw(NS_ERROR_EDITOR_DESTROYED);
    return;
  }

  if (!mInitSucceeded) {
    return;  // We should do nothing if we're being initialized.
  }

  NS_WARNING_ASSERTION(
      !aRv.Failed(),
      "EditorBase::OnStartToHandleTopLevelEditSubAction() failed");

  // Let's work with the latest layout information after (maybe) dispatching
  // `beforeinput` event.
  RefPtr<Document> document = GetDocument();
  if (NS_WARN_IF(!document)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }
  document->FlushPendingNotifications(FlushType::Frames);
  if (NS_WARN_IF(Destroyed())) {
    aRv.Throw(NS_ERROR_EDITOR_DESTROYED);
    return;
  }

  // Remember where our selection was before edit action took place:
  const auto atCompositionStart =
      GetFirstIMESelectionStartPoint<EditorRawDOMPoint>();
  if (atCompositionStart.IsSet()) {
    // If there is composition string, let's remember current composition
    // range.
    TopLevelEditSubActionDataRef().mSelectedRange->StoreRange(
        atCompositionStart, GetLastIMESelectionEndPoint<EditorRawDOMPoint>());
  } else {
    // Get the selection location
    // XXX This may occur so that I think that we shouldn't throw exception
    //     in this case.
    if (NS_WARN_IF(!SelectionRef().RangeCount())) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return;
    }
    if (const nsRange* range = SelectionRef().GetRangeAt(0)) {
      TopLevelEditSubActionDataRef().mSelectedRange->StoreRange(*range);
    }
  }

  // Register with range updater to track this as we perturb the doc
  RangeUpdaterRef().RegisterRangeItem(
      *TopLevelEditSubActionDataRef().mSelectedRange);

  // Remember current inline styles for deletion and normal insertion ops
  const bool cacheInlineStyles = [&]() {
    switch (aTopLevelEditSubAction) {
      case EditSubAction::eInsertText:
      case EditSubAction::eInsertTextComingFromIME:
      case EditSubAction::eDeleteSelectedContent:
        return true;
      default:
        return IsPendingStyleCachePreservingSubAction(aTopLevelEditSubAction);
    }
  }();
  if (cacheInlineStyles) {
    const RefPtr<Element> editingHost =
        ComputeEditingHost(LimitInBodyElement::No);
    if (NS_WARN_IF(!editingHost)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    nsIContent* const startContainer =
        HTMLEditUtils::GetContentToPreserveInlineStyles(
            TopLevelEditSubActionDataRef()
                .mSelectedRange->StartPoint<EditorRawDOMPoint>(),
            *editingHost);
    if (NS_WARN_IF(!startContainer)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }
    if (const RefPtr<Element> startContainerElement =
            startContainer->GetAsElementOrParentElement()) {
      nsresult rv = CacheInlineStyles(*startContainerElement);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::CacheInlineStyles() failed");
        aRv.Throw(rv);
        return;
      }
    }
  }

  // Stabilize the document against contenteditable count changes
  if (document->GetEditingState() == Document::EditingState::eContentEditable) {
    document->ChangeContentEditableCount(nullptr, +1);
    TopLevelEditSubActionDataRef().mRestoreContentEditableCount = true;
  }

  // Check that selection is in subtree defined by body node
  nsresult rv = EnsureSelectionInBodyOrDocumentElement();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    aRv.Throw(NS_ERROR_EDITOR_DESTROYED);
    return;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::EnsureSelectionInBodyOrDocumentElement() "
                       "failed, but ignored");
}

nsresult HTMLEditor::OnEndHandlingTopLevelEditSubAction() {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  nsresult rv;
  while (true) {
    if (NS_WARN_IF(Destroyed())) {
      rv = NS_ERROR_EDITOR_DESTROYED;
      break;
    }

    if (!mInitSucceeded) {
      rv = NS_OK;  // We should do nothing if we're being initialized.
      break;
    }

    // Do all the tricky stuff
    rv = OnEndHandlingTopLevelEditSubActionInternal();
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::OnEndHandlingTopLevelEditSubActionInternal() failied");
    // Perhaps, we need to do the following jobs even if the editor has been
    // destroyed since they adjust some states of HTML document but don't
    // modify the DOM tree nor Selection.

    // Free up selectionState range item
    if (TopLevelEditSubActionDataRef().mSelectedRange) {
      RangeUpdaterRef().DropRangeItem(
          *TopLevelEditSubActionDataRef().mSelectedRange);
    }

    // Reset the contenteditable count to its previous value
    if (TopLevelEditSubActionDataRef().mRestoreContentEditableCount) {
      Document* document = GetDocument();
      if (NS_WARN_IF(!document)) {
        rv = NS_ERROR_FAILURE;
        break;
      }
      if (document->GetEditingState() ==
          Document::EditingState::eContentEditable) {
        document->ChangeContentEditableCount(nullptr, -1);
      }
    }
    break;
  }
  DebugOnly<nsresult> rvIgnored =
      EditorBase::OnEndHandlingTopLevelEditSubAction();
  NS_WARNING_ASSERTION(
      NS_FAILED(rv) || NS_SUCCEEDED(rvIgnored),
      "EditorBase::OnEndHandlingTopLevelEditSubAction() failed, but ignored");
  MOZ_ASSERT(!GetTopLevelEditSubAction());
  MOZ_ASSERT(GetDirectionOfTopLevelEditSubAction() == eNone);
  return rv;
}

nsresult HTMLEditor::OnEndHandlingTopLevelEditSubActionInternal() {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // If we just maintained the DOM tree for consistent behavior even after
  // web apps modified the DOM, we should not touch the DOM in this
  // post-processor.
  if (GetTopLevelEditSubAction() ==
      EditSubAction::eMaintainWhiteSpaceVisibility) {
    return NS_OK;
  }

  nsresult rv = EnsureSelectionInBodyOrDocumentElement();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::EnsureSelectionInBodyOrDocumentElement() "
                       "failed, but ignored");

  switch (GetTopLevelEditSubAction()) {
    case EditSubAction::eReplaceHeadWithHTMLSource:
    case EditSubAction::eCreatePaddingBRElementForEmptyEditor:
      return NS_OK;
    default:
      break;
  }

  if (TopLevelEditSubActionDataRef().mChangedRange->IsPositioned() &&
      GetTopLevelEditSubAction() != EditSubAction::eUndo &&
      GetTopLevelEditSubAction() != EditSubAction::eRedo) {
    // don't let any txns in here move the selection around behind our back.
    // Note that this won't prevent explicit selection setting from working.
    AutoTransactionsConserveSelection dontChangeMySelection(*this);

    {
      EditorDOMRange changedRange(
          *TopLevelEditSubActionDataRef().mChangedRange);
      if (changedRange.IsPositioned() &&
          changedRange.EnsureNotInNativeAnonymousSubtree()) {
        bool isBlockLevelSubAction = false;
        switch (GetTopLevelEditSubAction()) {
          case EditSubAction::eInsertText:
          case EditSubAction::eInsertTextComingFromIME:
          case EditSubAction::eInsertLineBreak:
          case EditSubAction::eInsertParagraphSeparator:
          case EditSubAction::eDeleteText: {
            // XXX We should investigate whether this is really needed because
            //     it seems that the following code does not handle the
            //     white-spaces.
            RefPtr<nsRange> extendedChangedRange =
                CreateRangeIncludingAdjuscentWhiteSpaces(changedRange);
            if (extendedChangedRange) {
              MOZ_ASSERT(extendedChangedRange->IsPositioned());
              // Use extended range temporarily.
              TopLevelEditSubActionDataRef().mChangedRange =
                  std::move(extendedChangedRange);
            }
            break;
          }
          case EditSubAction::eCreateOrChangeList:
          case EditSubAction::eCreateOrChangeDefinitionListItem:
          case EditSubAction::eRemoveList:
          case EditSubAction::eFormatBlockForHTMLCommand:
          case EditSubAction::eCreateOrRemoveBlock:
          case EditSubAction::eIndent:
          case EditSubAction::eOutdent:
          case EditSubAction::eSetOrClearAlignment:
          case EditSubAction::eSetPositionToAbsolute:
          case EditSubAction::eSetPositionToStatic:
          case EditSubAction::eDecreaseZIndex:
          case EditSubAction::eIncreaseZIndex:
            isBlockLevelSubAction = true;
            [[fallthrough]];
          default: {
            Element* editingHost = ComputeEditingHost();
            if (MOZ_UNLIKELY(!editingHost)) {
              break;
            }
            RefPtr<nsRange> extendedChangedRange = AutoClonedRangeArray::
                CreateRangeWrappingStartAndEndLinesContainingBoundaries(
                    changedRange, GetTopLevelEditSubAction(),
                    isBlockLevelSubAction
                        ? BlockInlineCheck::UseHTMLDefaultStyle
                        : BlockInlineCheck::UseComputedDisplayOutsideStyle,
                    *editingHost);
            if (!extendedChangedRange) {
              break;
            }
            MOZ_ASSERT(extendedChangedRange->IsPositioned());
            // Use extended range temporarily.
            TopLevelEditSubActionDataRef().mChangedRange =
                std::move(extendedChangedRange);
            break;
          }
        }
      }
    }

    // if we did a ranged deletion or handling backspace key, make sure we have
    // a place to put caret.
    // Note we only want to do this if the overall operation was deletion,
    // not if deletion was done along the way for
    // EditSubAction::eInsertHTMLSource, EditSubAction::eInsertText, etc.
    // That's why this is here rather than DeleteSelectionAsSubAction().
    // However, we shouldn't insert <br> elements if we've already removed
    // empty block parents because users may want to disappear the line by
    // the deletion.
    // XXX We should make HandleDeleteSelection() store expected container
    //     for handling this here since we cannot trust current selection is
    //     collapsed at deleted point.
    if (GetTopLevelEditSubAction() == EditSubAction::eDeleteSelectedContent &&
        TopLevelEditSubActionDataRef().mDidDeleteNonCollapsedRange &&
        !TopLevelEditSubActionDataRef().mDidDeleteEmptyParentBlocks) {
      const auto newCaretPosition =
          GetFirstSelectionStartPoint<EditorDOMPoint>();
      if (!newCaretPosition.IsSet()) {
        NS_WARNING("There was no selection range");
        return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
      }
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError =
              InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded(
                  newCaretPosition);
      if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::"
            "InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded() failed");
        return insertPaddingBRElementResultOrError.unwrapErr();
      }
      nsresult rv =
          insertPaddingBRElementResultOrError.unwrap().SuggestCaretPointTo(
              *this, {SuggestCaret::OnlyIfHasSuggestion});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
        return rv;
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CaretPoint::SuggestCaretPointTo() failed, but ignored");
    }

    // add in any needed <br>s, and remove any unneeded ones.
    nsresult rv = InsertBRElementToEmptyListItemsAndTableCellsInRange(
        TopLevelEditSubActionDataRef().mChangedRange->StartRef().AsRaw(),
        TopLevelEditSubActionDataRef().mChangedRange->EndRef().AsRaw());
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::InsertBRElementToEmptyListItemsAndTableCellsInRange()"
        " failed, but ignored");

    // merge any adjacent text nodes
    switch (GetTopLevelEditSubAction()) {
      case EditSubAction::eInsertText:
      case EditSubAction::eInsertTextComingFromIME:
        break;
      default: {
        nsresult rv = CollapseAdjacentTextNodes(
            MOZ_KnownLive(*TopLevelEditSubActionDataRef().mChangedRange));
        if (NS_WARN_IF(Destroyed())) {
          return NS_ERROR_EDITOR_DESTROYED;
        }
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::CollapseAdjacentTextNodes() failed");
          return rv;
        }
        break;
      }
    }

    // Clean up any empty nodes in the changed range unless they are inserted
    // intentionally.
    if (TopLevelEditSubActionDataRef().mNeedsToCleanUpEmptyElements) {
      nsresult rv = RemoveEmptyNodesIn(
          EditorDOMRange(*TopLevelEditSubActionDataRef().mChangedRange));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::RemoveEmptyNodesIn() failed");
        return rv;
      }
    }

    // attempt to transform any unneeded nbsp's into spaces after doing various
    // operations
    const bool needToNormalizeWhiteSpaces = [&]() {
      switch (GetTopLevelEditSubAction()) {
        case EditSubAction::eDeleteSelectedContent:
          if (TopLevelEditSubActionDataRef().mDidNormalizeWhitespaces) {
            return false;
          }
          [[fallthrough]];
        case EditSubAction::eInsertText:
        case EditSubAction::eInsertTextComingFromIME:
        case EditSubAction::eInsertLineBreak:
        case EditSubAction::eInsertParagraphSeparator:
        case EditSubAction::ePasteHTMLContent:
        case EditSubAction::eInsertHTMLSource:
          return !StaticPrefs::
              editor_white_space_normalization_blink_compatible();
        default:
          return false;
      }
    }();
    if (needToNormalizeWhiteSpaces) {
      // Due to the replacement of white-spaces in
      // WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt(), selection
      // ranges may be changed since DOM ranges track the DOM mutation by
      // themselves.  However, we want to keep selection as-is. Therefore, we
      // should restore `Selection` after replacing white-spaces.
      AutoSelectionRestorer restoreSelection(this);
      // TODO: Temporarily, WhiteSpaceVisibilityKeeper replaces ASCII
      //       white-spaces with NPSPs and then, we'll replace them with ASCII
      //       white-spaces here.  We should avoid this overwriting things as
      //       far as possible because replacing characters in text nodes
      //       causes running mutation event listeners which are really
      //       expensive.
      // Adjust end of composition string if there is composition string.
      auto pointToAdjust = GetLastIMESelectionEndPoint<EditorDOMPoint>();
      if (!pointToAdjust.IsInContentNode()) {
        // Otherwise, adjust current selection start point.
        pointToAdjust = GetFirstSelectionStartPoint<EditorDOMPoint>();
        if (NS_WARN_IF(!pointToAdjust.IsInContentNode())) {
          return NS_ERROR_FAILURE;
        }
      }
      if (const RefPtr<Element> editingHost =
              ComputeEditingHost(LimitInBodyElement::No)) {
        if (EditorUtils::IsEditableContent(
                *pointToAdjust.ContainerAs<nsIContent>(), EditorType::HTML)) {
          AutoTrackDOMPoint trackPointToAdjust(RangeUpdaterRef(),
                                               &pointToAdjust);
          nsresult rv =
              WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt(
                  *this, pointToAdjust, *editingHost);
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt() "
                "failed");
            return rv;
          }
        }

        // also do this for original selection endpoints.
        // XXX Hmm, if `NormalizeVisibleWhiteSpacesAt()` runs mutation event
        //     listener and that causes changing `mSelectedRange`, what we
        //     should do?
        if (NS_WARN_IF(!TopLevelEditSubActionDataRef()
                            .mSelectedRange->IsPositioned())) {
          return NS_ERROR_FAILURE;
        }

        EditorDOMPoint atStart =
            TopLevelEditSubActionDataRef().mSelectedRange->StartPoint();
        if (atStart != pointToAdjust && atStart.IsInContentNode() &&
            EditorUtils::IsEditableContent(*atStart.ContainerAs<nsIContent>(),
                                           EditorType::HTML)) {
          AutoTrackDOMPoint trackPointToAdjust(RangeUpdaterRef(),
                                               &pointToAdjust);
          AutoTrackDOMPoint trackStartPoint(RangeUpdaterRef(), &atStart);
          nsresult rv =
              WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt(
                  *this, atStart, *editingHost);
          if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
            return NS_ERROR_EDITOR_DESTROYED;
          }
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt() "
              "failed, but ignored");
        }
        // we only need to handle old selection endpoint if it was different
        // from start
        EditorDOMPoint atEnd =
            TopLevelEditSubActionDataRef().mSelectedRange->EndPoint();
        if (!TopLevelEditSubActionDataRef().mSelectedRange->Collapsed() &&
            atEnd != pointToAdjust && atEnd != atStart &&
            atEnd.IsInContentNode() &&
            EditorUtils::IsEditableContent(*atEnd.ContainerAs<nsIContent>(),
                                           EditorType::HTML)) {
          nsresult rv =
              WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt(
                  *this, atEnd, *editingHost);
          if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
            return NS_ERROR_EDITOR_DESTROYED;
          }
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "WhiteSpaceVisibilityKeeper::NormalizeVisibleWhiteSpacesAt() "
              "failed, but ignored");
        }
      }
    }

    // Adjust selection for insert text, html paste, and delete actions if
    // we haven't removed new empty blocks.  Note that if empty block parents
    // are removed, Selection should've been adjusted by the method which
    // did it.
    if (!TopLevelEditSubActionDataRef().mDidDeleteEmptyParentBlocks &&
        SelectionRef().IsCollapsed()) {
      switch (GetTopLevelEditSubAction()) {
        case EditSubAction::eInsertText:
        case EditSubAction::eInsertTextComingFromIME:
        case EditSubAction::eInsertLineBreak:
        case EditSubAction::eInsertParagraphSeparator:
        case EditSubAction::ePasteHTMLContent:
        case EditSubAction::eInsertHTMLSource:
          // XXX AdjustCaretPositionAndEnsurePaddingBRElement() intentionally
          //     does not create padding `<br>` element for empty editor.
          //     Investigate which is better that whether this should does it
          //     or wait MaybeCreatePaddingBRElementForEmptyEditor().
          rv = AdjustCaretPositionAndEnsurePaddingBRElement(
              GetDirectionOfTopLevelEditSubAction());
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "HTMLEditor::AdjustCaretPositionAndEnsurePaddingBRElement() "
                "failed");
            return rv;
          }
          break;
        default:
          break;
      }
    }

    // check for any styles which were removed inappropriately
    bool reapplyCachedStyle;
    switch (GetTopLevelEditSubAction()) {
      case EditSubAction::eInsertText:
      case EditSubAction::eInsertTextComingFromIME:
      case EditSubAction::eDeleteSelectedContent:
        reapplyCachedStyle = true;
        break;
      default:
        reapplyCachedStyle =
            IsPendingStyleCachePreservingSubAction(GetTopLevelEditSubAction());
        break;
    }

    // If the selection is in empty inline HTML elements, we should delete
    // them unless it's inserted intentionally.
    if (mPlaceholderBatch &&
        TopLevelEditSubActionDataRef().mNeedsToCleanUpEmptyElements &&
        SelectionRef().IsCollapsed() && SelectionRef().GetFocusNode()) {
      RefPtr<Element> mostDistantEmptyInlineAncestor = nullptr;
      for (Element* ancestor :
           SelectionRef().GetFocusNode()->InclusiveAncestorsOfType<Element>()) {
        if (!ancestor->IsHTMLElement() ||
            !HTMLEditUtils::IsRemovableFromParentNode(*ancestor) ||
            !HTMLEditUtils::IsEmptyInlineContainer(
                *ancestor, {EmptyCheckOption::TreatSingleBRElementAsVisible},
                BlockInlineCheck::UseComputedDisplayStyle)) {
          break;
        }
        mostDistantEmptyInlineAncestor = ancestor;
      }
      if (mostDistantEmptyInlineAncestor) {
        nsresult rv =
            DeleteNodeWithTransaction(*mostDistantEmptyInlineAncestor);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "EditorBase::DeleteNodeWithTransaction() failed at deleting "
              "empty inline ancestors");
          return rv;
        }
      }
    }

    // But the cached inline styles should be restored from type-in-state later.
    if (reapplyCachedStyle) {
      DebugOnly<nsresult> rvIgnored =
          mPendingStylesToApplyToNewContent->UpdateSelState(*this);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "PendingStyles::UpdateSelState() failed, but ignored");
      rvIgnored = ReapplyCachedStyles();
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::ReapplyCachedStyles() failed, but ignored");
      TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
    }
  }

  rv = HandleInlineSpellCheck(
      TopLevelEditSubActionDataRef().mSelectedRange->StartPoint(),
      TopLevelEditSubActionDataRef().mChangedRange);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::HandleInlineSpellCheck() failed");
    return rv;
  }

  // detect empty doc
  // XXX Need to investigate when the padding <br> element is removed because
  //     I don't see the <br> element with testing manually.  If it won't be
  //     used, we can get rid of this cost.
  rv = MaybeCreatePaddingBRElementForEmptyEditor();
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "EditorBase::MaybeCreatePaddingBRElementForEmptyEditor() failed");
    return rv;
  }

  // adjust selection HINT if needed
  if (!TopLevelEditSubActionDataRef().mDidExplicitlySetInterLine &&
      SelectionRef().IsCollapsed()) {
    SetSelectionInterlinePosition();
  }

  return NS_OK;
}

Result<EditActionResult, nsresult> HTMLEditor::CanHandleHTMLEditSubAction(
    CheckSelectionInReplacedElement aCheckSelectionInReplacedElement
    /* = CheckSelectionInReplacedElement::Yes */) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  // If there is not selection ranges, we should ignore the result.
  if (!SelectionRef().RangeCount()) {
    return EditActionResult::CanceledResult();
  }

  const nsRange* range = SelectionRef().GetRangeAt(0);
  nsINode* selStartNode = range->GetStartContainer();
  if (NS_WARN_IF(!selStartNode) || NS_WARN_IF(!selStartNode->IsContent())) {
    return Err(NS_ERROR_FAILURE);
  }

  if (!HTMLEditUtils::IsSimplyEditableNode(*selStartNode)) {
    return EditActionResult::CanceledResult();
  }

  nsINode* selEndNode = range->GetEndContainer();
  if (NS_WARN_IF(!selEndNode) || NS_WARN_IF(!selEndNode->IsContent())) {
    return Err(NS_ERROR_FAILURE);
  }

  if (selStartNode == selEndNode) {
    if (aCheckSelectionInReplacedElement ==
            CheckSelectionInReplacedElement::Yes &&
        HTMLEditUtils::IsNonEditableReplacedContent(
            *selStartNode->AsContent())) {
      return EditActionResult::CanceledResult();
    }
    return EditActionResult::IgnoredResult();
  }

  if (HTMLEditUtils::IsNonEditableReplacedContent(*selStartNode->AsContent()) ||
      HTMLEditUtils::IsNonEditableReplacedContent(*selEndNode->AsContent())) {
    return EditActionResult::CanceledResult();
  }

  if (!HTMLEditUtils::IsSimplyEditableNode(*selEndNode)) {
    return EditActionResult::CanceledResult();
  }

  // If anchor node is in an HTML element which has inert attribute, we should
  // do nothing.
  // XXX HTMLEditor typically uses first range instead of anchor/focus range.
  //     Therefore, referring first range here is more reasonable than
  //     anchor/focus range of Selection.
  nsIContent* const selAnchorContent = SelectionRef().GetDirection() == eDirNext
                                           ? nsIContent::FromNode(selStartNode)
                                           : nsIContent::FromNode(selEndNode);
  if (selAnchorContent &&
      HTMLEditUtils::ContentIsInert(*selAnchorContent->AsContent())) {
    return EditActionResult::CanceledResult();
  }

  // XXX What does it mean the common ancestor is editable?  I have no idea.
  //     It should be in same (active) editing host, and even if it's editable,
  //     there may be non-editable contents in the range.
  nsINode* commonAncestor = range->GetClosestCommonInclusiveAncestor();
  if (MOZ_UNLIKELY(!commonAncestor)) {
    NS_WARNING(
        "AbstractRange::GetClosestCommonInclusiveAncestor() returned nullptr");
    return Err(NS_ERROR_FAILURE);
  }
  return HTMLEditUtils::IsSimplyEditableNode(*commonAncestor)
             ? EditActionResult::IgnoredResult()
             : EditActionResult::CanceledResult();
}

MOZ_CAN_RUN_SCRIPT static nsStaticAtom& MarginPropertyAtomForIndent(
    nsIContent& aContent) {
  nsAutoString direction;
  DebugOnly<nsresult> rvIgnored = CSSEditUtils::GetComputedProperty(
      aContent, *nsGkAtoms::direction, direction);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "CSSEditUtils::GetComputedProperty(nsGkAtoms::direction)"
                       " failed, but ignored");
  return direction.EqualsLiteral("rtl") ? *nsGkAtoms::marginRight
                                        : *nsGkAtoms::marginLeft;
}

nsresult HTMLEditor::EnsureCaretNotAfterInvisibleBRElement(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(SelectionRef().IsCollapsed());

  // If we are after a padding `<br>` element for empty last line in the same
  // block, then move selection to be before it
  const nsRange* firstRange = SelectionRef().GetRangeAt(0);
  if (NS_WARN_IF(!firstRange)) {
    return NS_ERROR_FAILURE;
  }

  EditorRawDOMPoint atSelectionStart(firstRange->StartRef());
  if (NS_WARN_IF(!atSelectionStart.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(atSelectionStart.IsSetAndValid());

  if (!atSelectionStart.IsInContentNode()) {
    return NS_OK;
  }

  nsIContent* previousBRElement = HTMLEditUtils::GetPreviousContent(
      atSelectionStart, {}, BlockInlineCheck::UseComputedDisplayStyle,
      &aEditingHost);
  if (!previousBRElement || !previousBRElement->IsHTMLElement(nsGkAtoms::br) ||
      !previousBRElement->GetParent() ||
      !EditorUtils::IsEditableContent(*previousBRElement->GetParent(),
                                      EditorType::HTML) ||
      !HTMLEditUtils::IsInvisibleBRElement(*previousBRElement)) {
    return NS_OK;
  }

  const RefPtr<const Element> blockElementAtSelectionStart =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *atSelectionStart.ContainerAs<nsIContent>(),
          HTMLEditUtils::ClosestBlockElement,
          BlockInlineCheck::UseComputedDisplayStyle);
  const RefPtr<const Element> parentBlockElementOfBRElement =
      HTMLEditUtils::GetAncestorElement(
          *previousBRElement, HTMLEditUtils::ClosestBlockElement,
          BlockInlineCheck::UseComputedDisplayStyle);

  if (!blockElementAtSelectionStart ||
      blockElementAtSelectionStart != parentBlockElementOfBRElement) {
    return NS_OK;
  }

  // If we are here then the selection is right after a padding <br>
  // element for empty last line that is in the same block as the
  // selection.  We need to move the selection start to be before the
  // padding <br> element.
  EditorRawDOMPoint atInvisibleBRElement(previousBRElement);
  nsresult rv = CollapseSelectionTo(atInvisibleBRElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

nsresult HTMLEditor::MaybeCreatePaddingBRElementForEmptyEditor() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (mPaddingBRElementForEmptyEditor) {
    return NS_OK;
  }

  // XXX I think that we should not insert a <br> element if we're for a web
  // content.  Probably, this is required only by chrome editors such as
  // the mail composer of Thunderbird and the composer of SeaMonkey.

  const RefPtr<Element> bodyOrDocumentElement = GetRoot();
  if (!bodyOrDocumentElement) {
    return NS_OK;
  }

  // Skip adding the padding <br> element for empty editor if body
  // is read-only.
  if (!HTMLEditUtils::IsSimplyEditableNode(*bodyOrDocumentElement)) {
    return NS_OK;
  }

  // Now we've got the body element. Iterate over the body element's children,
  // looking for editable content. If no editable content is found, insert the
  // padding <br> element.
  EditorType editorType = GetEditorType();
  bool isRootEditable =
      EditorUtils::IsEditableContent(*bodyOrDocumentElement, editorType);
  for (nsIContent* child = bodyOrDocumentElement->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (EditorUtils::IsPaddingBRElementForEmptyEditor(*child) ||
        !isRootEditable || EditorUtils::IsEditableContent(*child, editorType) ||
        HTMLEditUtils::IsBlockElement(
            *child, BlockInlineCheck::UseComputedDisplayStyle)) {
      return NS_OK;
    }
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eCreatePaddingBRElementForEmptyEditor,
      nsIEditor::eNone, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  Result<CreateElementResult, nsresult> insertPaddingBRElementResultOrError =
      InsertBRElement(WithTransaction::Yes,
                      BRElementType::PaddingForEmptyEditor,
                      EditorDOMPoint(bodyOrDocumentElement, 0u));
  if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
    NS_WARNING(
        "EditorBase::InsertBRElement(WithTransaction::Yes, "
        "BRElementType::PaddingForEmptyEditor) failed");
    return insertPaddingBRElementResultOrError.propagateErr();
  }
  CreateElementResult insertPaddingBRElementResult =
      insertPaddingBRElementResultOrError.unwrap();
  mPaddingBRElementForEmptyEditor =
      HTMLBRElement::FromNode(insertPaddingBRElementResult.GetNewNode());
  nsresult rv = insertPaddingBRElementResult.SuggestCaretPointTo(
      *this, {SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return rv;
  }
  NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                       "CaretPoint::SuggestCaretPointTo() failed, but ignored");
  return NS_OK;
}

nsresult HTMLEditor::EnsureNoPaddingBRElementForEmptyEditor() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!mPaddingBRElementForEmptyEditor) {
    return NS_OK;
  }

  // If we're an HTML editor, a mutation event listener may recreate padding
  // <br> element for empty editor again during the call of
  // DeleteNodeWithTransaction().  So, move it first.
  RefPtr<HTMLBRElement> paddingBRElement(
      std::move(mPaddingBRElementForEmptyEditor));
  nsresult rv = DeleteNodeWithTransaction(*paddingBRElement);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteNodeWithTransaction() failed");
  return rv;
}

nsresult HTMLEditor::ReflectPaddingBRElementForEmptyEditor() {
  if (NS_WARN_IF(!mRootElement)) {
    NS_WARNING("Failed to handle padding BR element due to no root element");
    return NS_ERROR_FAILURE;
  }
  // The idea here is to see if the magic empty node has suddenly reappeared. If
  // it has, set our state so we remember it. There is a tradeoff between doing
  // here and at redo, or doing it everywhere else that might care.  Since undo
  // and redo are relatively rare, it makes sense to take the (small)
  // performance hit here.
  nsIContent* firstLeafChild = HTMLEditUtils::GetFirstLeafContent(
      *mRootElement, {LeafNodeType::OnlyLeafNode});
  if (firstLeafChild &&
      EditorUtils::IsPaddingBRElementForEmptyEditor(*firstLeafChild)) {
    mPaddingBRElementForEmptyEditor =
        static_cast<HTMLBRElement*>(firstLeafChild);
  } else {
    mPaddingBRElementForEmptyEditor = nullptr;
  }
  return NS_OK;
}

nsresult HTMLEditor::PrepareInlineStylesForCaret() {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(SelectionRef().IsCollapsed());

  // XXX This method works with the top level edit sub-action, but this
  //     must be wrong if we are handling nested edit action.

  if (TopLevelEditSubActionDataRef().mDidDeleteSelection) {
    switch (GetTopLevelEditSubAction()) {
      case EditSubAction::eInsertText:
      case EditSubAction::eInsertTextComingFromIME:
      case EditSubAction::eDeleteSelectedContent: {
        nsresult rv = ReapplyCachedStyles();
        if (NS_FAILED(rv)) {
          NS_WARNING("HTMLEditor::ReapplyCachedStyles() failed");
          return rv;
        }
        break;
      }
      default:
        break;
    }
  }
  // For most actions we want to clear the cached styles, but there are
  // exceptions
  if (!IsPendingStyleCachePreservingSubAction(GetTopLevelEditSubAction())) {
    TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
  }
  return NS_OK;
}

Result<EditActionResult, nsresult> HTMLEditor::HandleInsertText(
    const nsAString& aInsertionString, InsertTextFor aPurpose) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  UndefineCaretBidiLevel();

  // If the selection isn't collapsed, delete it.  Don't delete existing inline
  // tags, because we're hopefully going to insert text (bug 787432).
  if (!SelectionRef().IsCollapsed() &&
      !InsertingTextForExtantComposition(aPurpose)) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eNoStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(nsIEditor::eNone, "
          "nsIEditor::eNoStrip) failed");
      return Err(rv);
    }
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(*editingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  auto pointToInsert = [&]() -> EditorDOMPoint {
    if (InsertingTextForExtantComposition(aPurpose)) {
      auto compositionStartPoint =
          GetFirstIMESelectionStartPoint<EditorDOMPoint>();
      if (MOZ_LIKELY(compositionStartPoint.IsSet())) {
        return compositionStartPoint;
      }
    }
    return GetFirstSelectionStartPoint<EditorDOMPoint>();
  }();
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  // for every property that is set, insert a new inline style node
  // XXX I think that if this is second or later composition update, we should
  // not change the style because we won't update composition with keeping
  // inline elements in composing range.
  Result<EditorDOMPoint, nsresult> setStyleResult =
      CreateStyleForInsertText(pointToInsert, *editingHost);
  if (MOZ_UNLIKELY(setStyleResult.isErr())) {
    NS_WARNING("HTMLEditor::CreateStyleForInsertText() failed");
    return setStyleResult.propagateErr();
  }
  if (setStyleResult.inspect().IsSet()) {
    pointToInsert = setStyleResult.unwrap();
  }

  if (NS_WARN_IF(!pointToInsert.IsSetAndValid()) ||
      NS_WARN_IF(!pointToInsert.IsInContentNode())) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(pointToInsert.IsSetAndValid());

  // If the point is not in an element which can contain text nodes, climb up
  // the DOM tree.
  if (!pointToInsert.IsInTextNode()) {
    while (!HTMLEditUtils::CanNodeContain(*pointToInsert.GetContainer(),
                                          *nsGkAtoms::textTagName)) {
      if (NS_WARN_IF(pointToInsert.GetContainer() == editingHost) ||
          NS_WARN_IF(!pointToInsert.GetContainerParentAs<nsIContent>())) {
        NS_WARNING("Selection start point couldn't have text nodes");
        return Err(NS_ERROR_FAILURE);
      }
      pointToInsert.Set(pointToInsert.ContainerAs<nsIContent>());
    }
  }

  if (InsertingTextForComposition(aPurpose)) {
    if (aInsertionString.IsEmpty()) {
      // Right now the WhiteSpaceVisibilityKeeper code bails on empty strings,
      // but IME needs the InsertTextWithTransaction() call to still happen
      // since empty strings are meaningful there.
      Result<InsertTextResult, nsresult> insertEmptyTextResultOrError =
          InsertTextWithTransaction(aInsertionString, pointToInsert,
                                    InsertTextTo::ExistingTextNodeIfAvailable);
      if (MOZ_UNLIKELY(insertEmptyTextResultOrError.isErr())) {
        NS_WARNING("HTMLEditor::InsertTextWithTransaction() failed");
        return insertEmptyTextResultOrError.propagateErr();
      }
      InsertTextResult insertEmptyTextResult =
          insertEmptyTextResultOrError.unwrap();
      // InsertTextWithTransaction() doesn not suggest caret position if it's
      // called for IME composition. However, for the safety, let's ignore the
      // caret position explicitly.
      insertEmptyTextResult.IgnoreCaretPointSuggestion();
      nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
          insertEmptyTextResult.EndOfInsertedTextRef());
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
        return Err(rv);
      }
      const EditorDOMPoint& endOfInsertedText =
          insertEmptyTextResult.EndOfInsertedTextRef();
      if (StaticPrefs::editor_white_space_normalization_blink_compatible() &&
          endOfInsertedText.IsInTextNode() &&
          !endOfInsertedText.IsStartOfContainer()) {
        nsresult rv = WhiteSpaceVisibilityKeeper::
            NormalizeVisibleWhiteSpacesWithoutDeletingInvisibleWhiteSpaces(
                *this, endOfInsertedText.AsInText().PreviousPoint());
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "WhiteSpaceVisibilityKeeper::"
              "NormalizeVisibleWhiteSpacesWithoutDeletingInvisibleWhiteSpaces()"
              " failed");
          return Err(rv);
        }
        if (NS_WARN_IF(
                !endOfInsertedText.IsInContentNodeAndValidInComposedDoc())) {
          return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
        }
      }
      // If we replaced non-empty composition string with an empty string,
      // its preceding character may be a collapsible ASCII white-space.
      // Therefore, we may need to insert a padding <br> after the white-space.
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError = InsertPaddingBRElementIfNeeded(
              insertEmptyTextResult.EndOfInsertedTextRef(), nsIEditor::eNoStrip,
              *editingHost);
      if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertPaddingBRElementIfNeeded(eNoStrip) failed");
        return insertPaddingBRElementResultOrError.propagateErr();
      }
      insertPaddingBRElementResultOrError.unwrap().IgnoreCaretPointSuggestion();
      // Then, collapse caret after the empty text inserted position, i.e.,
      // whether the removed composition string was.
      if (AllowsTransactionsToChangeSelection()) {
        nsresult rv = CollapseSelectionTo(endOfInsertedText);
        if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CaretPoint::SuggestCaretPointTo() failed, but ignored");
      }
      return EditActionResult::HandledResult();
    }

    const auto compositionEndPoint =
        GetLastIMESelectionEndPoint<EditorDOMPoint>();
    Result<InsertTextResult, nsresult> replaceTextResult =
        WhiteSpaceVisibilityKeeper::InsertOrUpdateCompositionString(
            *this, aInsertionString,
            compositionEndPoint.IsSet()
                ? EditorDOMRange(pointToInsert, compositionEndPoint)
                : EditorDOMRange(pointToInsert),
            aPurpose);
    if (MOZ_UNLIKELY(replaceTextResult.isErr())) {
      NS_WARNING("WhiteSpaceVisibilityKeeper::ReplaceText() failed");
      return replaceTextResult.propagateErr();
    }
    InsertTextResult unwrappedReplacedTextResult = replaceTextResult.unwrap();
    nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
        unwrappedReplacedTextResult.EndOfInsertedTextRef());
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
    // CompositionTransaction should've set selection so that we should ignore
    // caret suggestion.
    unwrappedReplacedTextResult.IgnoreCaretPointSuggestion();

    const auto newCompositionStartPoint =
        GetFirstIMESelectionStartPoint<EditorDOMPoint>();
    const auto newCompositionEndPoint =
        GetLastIMESelectionEndPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!newCompositionStartPoint.IsSet()) ||
        NS_WARN_IF(!newCompositionEndPoint.IsSet())) {
      // Mutation event listener has changed the DOM tree...
      return EditActionResult::HandledResult();
    }
    rv = TopLevelEditSubActionDataRef().mChangedRange->SetStartAndEnd(
        newCompositionStartPoint.ToRawRangeBoundary(),
        newCompositionEndPoint.ToRawRangeBoundary());
    if (NS_FAILED(rv)) {
      NS_WARNING("nsRange::SetStartAndEnd() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  MOZ_ASSERT(!InsertingTextForComposition(aPurpose));

  // find where we are
  EditorDOMPoint currentPoint(pointToInsert);

  // is our text going to be PREformatted?
  // We remember this so that we know how to handle tabs.
  const bool isWhiteSpaceCollapsible = !EditorUtils::IsWhiteSpacePreformatted(
      *pointToInsert.ContainerAs<nsIContent>());
  const Maybe<LineBreakType> lineBreakType = GetPreferredLineBreakType(
      *pointToInsert.ContainerAs<nsIContent>(), *editingHost);
  if (NS_WARN_IF(lineBreakType.isNothing())) {
    return Err(NS_ERROR_FAILURE);
  }

  // turn off the edit listener: we know how to
  // build the "doc changed range" ourselves, and it's
  // must faster to do it once here than to track all
  // the changes one at a time.
  AutoRestore<bool> disableListener(
      EditSubActionDataRef().mAdjustChangedRangeFromListener);
  EditSubActionDataRef().mAdjustChangedRangeFromListener = false;

  // don't change my selection in subtransactions
  AutoTransactionsConserveSelection dontChangeMySelection(*this);
  {
    AutoTrackDOMPoint tracker(RangeUpdaterRef(), &pointToInsert);

    const auto GetInsertTextTo = [](int32_t aInclusiveNextLinefeedOffset,
                                    uint32_t aLineStartOffset) {
      if (aInclusiveNextLinefeedOffset > 0) {
        return aLineStartOffset > 0
                   // If we'll insert a <br> and we're inserting 2nd or later
                   // line, we should always create new `Text` since it'll be
                   // between 2 <br> elements.
                   ? InsertTextTo::AlwaysCreateNewTextNode
                   // If we'll insert a <br> and we're inserting first line,
                   // we should append text to preceding text node, but
                   // we don't want to insert it to a a following text node
                   // because of avoiding to split the `Text`.
                   : InsertTextTo::ExistingTextNodeIfAvailableAndNotStart;
      }
      // If we're inserting the last line, the text should be inserted to
      // start of the following `Text` if there is or middle of the `Text`
      // at insertion position if we're inserting only the line.
      return InsertTextTo::ExistingTextNodeIfAvailable;
    };

    // for efficiency, break out the pre case separately.  This is because
    // its a lot cheaper to search the input string for only newlines than
    // it is to search for both tabs and newlines.
    if (!isWhiteSpaceCollapsible || IsPlaintextMailComposer()) {
      if (*lineBreakType == LineBreakType::Linefeed) {
        // Both Chrome and us inserts a preformatted linefeed with its own
        // `Text` node in various cases.  However, when inserting multiline
        // text, we should insert a `Text` because Chrome does so and the
        // comment field in https://discussions.apple.com/ handles the new
        // `Text` to split each line into a paragraph.  At that time, it's
        // not assumed that inserted text is split at every linefeed.
        MOZ_ASSERT(*lineBreakType == LineBreakType::Linefeed);
        Result<InsertTextResult, nsresult> insertTextResult =
            InsertTextWithTransaction(
                aInsertionString, currentPoint,
                InsertTextTo::ExistingTextNodeIfAvailable);
        if (MOZ_UNLIKELY(insertTextResult.isErr())) {
          NS_WARNING("HTMLEditor::InsertTextWithTransaction() failed");
          return insertTextResult.propagateErr();
        }
        // Ignore the caret suggestion because of `dontChangeMySelection`
        // above.
        insertTextResult.inspect().IgnoreCaretPointSuggestion();
        if (insertTextResult.inspect().Handled()) {
          pointToInsert = currentPoint = insertTextResult.unwrap()
                                             .EndOfInsertedTextRef()
                                             .To<EditorDOMPoint>();
        } else {
          pointToInsert = currentPoint;
        }
      } else {
        MOZ_ASSERT(*lineBreakType == LineBreakType::BRElement);
        uint32_t nextOffset = 0;
        while (nextOffset < aInsertionString.Length()) {
          const uint32_t lineStartOffset = nextOffset;
          const int32_t inclusiveNextLinefeedOffset =
              aInsertionString.FindChar(nsCRT::LF, lineStartOffset);
          const uint32_t lineLength =
              inclusiveNextLinefeedOffset != -1
                  ? static_cast<uint32_t>(inclusiveNextLinefeedOffset) -
                        lineStartOffset
                  : aInsertionString.Length() - lineStartOffset;
          if (lineLength) {
            // lineText does not include the preformatted line break.
            const nsDependentSubstring lineText(aInsertionString,
                                                lineStartOffset, lineLength);
            Result<InsertTextResult, nsresult> insertTextResult =
                InsertTextWithTransaction(
                    lineText, currentPoint,
                    GetInsertTextTo(inclusiveNextLinefeedOffset,
                                    lineStartOffset));
            if (MOZ_UNLIKELY(insertTextResult.isErr())) {
              NS_WARNING("HTMLEditor::InsertTextWithTransaction() failed");
              return insertTextResult.propagateErr();
            }
            // Ignore the caret suggestion because of `dontChangeMySelection`
            // above.
            insertTextResult.inspect().IgnoreCaretPointSuggestion();
            if (insertTextResult.inspect().Handled()) {
              pointToInsert = currentPoint = insertTextResult.unwrap()
                                                 .EndOfInsertedTextRef()
                                                 .To<EditorDOMPoint>();
            } else {
              pointToInsert = currentPoint;
            }
            if (inclusiveNextLinefeedOffset < 0) {
              break;  // We reached the last line
            }
          }
          MOZ_ASSERT(inclusiveNextLinefeedOffset >= 0);
          Result<CreateLineBreakResult, nsresult> insertLineBreakResultOrError =
              InsertLineBreak(WithTransaction::Yes, *lineBreakType,
                              currentPoint);
          if (MOZ_UNLIKELY(insertLineBreakResultOrError.isErr())) {
            NS_WARNING(nsPrintfCString("HTMLEditor::InsertLineBreak("
                                       "WithTransaction::Yes, %s) failed",
                                       ToString(*lineBreakType).c_str())
                           .get());
            return insertLineBreakResultOrError.propagateErr();
          }
          CreateLineBreakResult insertLineBreakResult =
              insertLineBreakResultOrError.unwrap();
          // We don't want to update selection here because we've blocked
          // InsertNodeTransaction updating selection with
          // dontChangeMySelection.
          insertLineBreakResult.IgnoreCaretPointSuggestion();
          MOZ_ASSERT(!AllowsTransactionsToChangeSelection());

          nextOffset = inclusiveNextLinefeedOffset + 1;
          pointToInsert =
              insertLineBreakResult.AfterLineBreak<EditorDOMPoint>();
          currentPoint.SetAfter(&insertLineBreakResult.LineBreakContentRef());
          if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc()) ||
              NS_WARN_IF(!currentPoint.IsSetAndValidInComposedDoc())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
        }
      }
    } else {
      uint32_t nextOffset = 0;
      while (nextOffset < aInsertionString.Length()) {
        const uint32_t lineStartOffset = nextOffset;
        const int32_t inclusiveNextLinefeedOffset =
            aInsertionString.FindChar(nsCRT::LF, lineStartOffset);
        const uint32_t lineLength =
            inclusiveNextLinefeedOffset != -1
                ? static_cast<uint32_t>(inclusiveNextLinefeedOffset) -
                      lineStartOffset
                : aInsertionString.Length() - lineStartOffset;

        if (lineLength) {
          auto insertTextResult =
              [&]() MOZ_CAN_RUN_SCRIPT -> Result<InsertTextResult, nsresult> {
            // lineText does not include the preformatted line break.
            const nsDependentSubstring lineText(aInsertionString,
                                                lineStartOffset, lineLength);
            if (!lineText.Contains(u'\t')) {
              return WhiteSpaceVisibilityKeeper::InsertText(
                  *this, lineText, currentPoint,
                  GetInsertTextTo(inclusiveNextLinefeedOffset,
                                  lineStartOffset));
            }
            nsAutoString formattedLineText(lineText);
            formattedLineText.ReplaceSubstring(u"\t"_ns, u"    "_ns);
            return WhiteSpaceVisibilityKeeper::InsertText(
                *this, formattedLineText, currentPoint,
                GetInsertTextTo(inclusiveNextLinefeedOffset, lineStartOffset));
          }();
          if (MOZ_UNLIKELY(insertTextResult.isErr())) {
            NS_WARNING("WhiteSpaceVisibilityKeeper::InsertText() failed");
            return insertTextResult.propagateErr();
          }
          // Ignore the caret suggestion because of `dontChangeMySelection`
          // above.
          insertTextResult.inspect().IgnoreCaretPointSuggestion();
          if (insertTextResult.inspect().Handled()) {
            pointToInsert = currentPoint =
                insertTextResult.unwrap().EndOfInsertedTextRef();
          } else {
            pointToInsert = currentPoint;
          }
          if (inclusiveNextLinefeedOffset < 0) {
            break;  // We reached the last line
          }
        }

        Result<CreateLineBreakResult, nsresult> insertLineBreakResultOrError =
            WhiteSpaceVisibilityKeeper::InsertLineBreak(*lineBreakType, *this,
                                                        currentPoint);
        if (MOZ_UNLIKELY(insertLineBreakResultOrError.isErr())) {
          NS_WARNING(
              nsPrintfCString(
                  "WhiteSpaceVisibilityKeeper::InsertLineBreak(%s) failed",
                  ToString(*lineBreakType).c_str())
                  .get());
          return insertLineBreakResultOrError.propagateErr();
        }
        CreateLineBreakResult insertLineBreakResult =
            insertLineBreakResultOrError.unwrap();
        // TODO: Some methods called for handling non-preformatted text use
        //       ComputeEditingHost().  Therefore, they depend on the latest
        //       selection.  So we cannot skip updating selection here.
        nsresult rv = insertLineBreakResult.SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CreateElementResult::SuggestCaretPointTo() failed, but ignored");
        nextOffset = inclusiveNextLinefeedOffset + 1;
        pointToInsert = insertLineBreakResult.AfterLineBreak<EditorDOMPoint>();
        currentPoint.SetAfter(&insertLineBreakResult.LineBreakContentRef());
        if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc()) ||
            NS_WARN_IF(!currentPoint.IsSetAndValidInComposedDoc())) {
          return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
        }
      }
    }

    // After this block, pointToInsert is updated by AutoTrackDOMPoint.
  }

  if (currentPoint.IsSet()) {
    // If we appended a collapsible white-space to the end of the text node,
    // its following content may be removed by the web app.  Then, we need to
    // keep it visible even if it becomes immediately before a block boundary.
    // For referring the node from our mutation observer, we need to store the
    // text node temporarily.
    if (currentPoint.IsInTextNode() &&
        MOZ_LIKELY(!currentPoint.IsStartOfContainer()) &&
        currentPoint.IsEndOfContainer() &&
        currentPoint.IsPreviousCharCollapsibleASCIISpace()) {
      mLastCollapsibleWhiteSpaceAppendedTextNode =
          currentPoint.ContainerAs<Text>();
    }
    nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(currentPoint);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
    currentPoint.SetInterlinePosition(InterlinePosition::EndOfLine);
    rv = CollapseSelectionTo(currentPoint);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "Selection::Collapse() failed, but ignored");

    // manually update the doc changed range so that AfterEdit will clean up
    // the correct portion of the document.
    rv = TopLevelEditSubActionDataRef().mChangedRange->SetStartAndEnd(
        pointToInsert.ToRawRangeBoundary(), currentPoint.ToRawRangeBoundary());
    if (NS_FAILED(rv)) {
      NS_WARNING("nsRange::SetStartAndEnd() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  DebugOnly<nsresult> rvIgnored =
      SelectionRef().SetInterlinePosition(InterlinePosition::EndOfLine);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "Selection::SetInterlinePosition(InterlinePosition::"
                       "EndOfLine) failed, but ignored");
  rv = TopLevelEditSubActionDataRef().mChangedRange->CollapseTo(pointToInsert);
  if (NS_FAILED(rv)) {
    NS_WARNING("nsRange::CollapseTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::InsertLineBreakAsSubAction() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsSelectionRangeContainerNotContent());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  // XXX This may be called by execCommand() with "insertLineBreak".
  //     In such case, naming the transaction "TypingTxnName" is odd.
  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::TypingTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);

  // calling it text insertion to trigger moz br treatment by rules
  // XXX Why do we use EditSubAction::eInsertText here?  Looks like
  //     EditSubAction::eInsertLineBreak or EditSubAction::eInsertNode
  //     is better.
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  UndefineCaretBidiLevel();

  // If the selection isn't collapsed, delete it.
  if (!SelectionRef().IsCollapsed()) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eStrip) failed");
      return rv;
    }
  }

  auto atStartOfSelection = GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!atStartOfSelection.IsInContentNode())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(atStartOfSelection.IsSetAndValidInComposedDoc());

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  const Maybe<LineBreakType> lineBreakType = GetPreferredLineBreakType(
      *atStartOfSelection.ContainerAs<nsIContent>(), *editingHost);
  if (MOZ_UNLIKELY(!lineBreakType)) {
    return NS_SUCCESS_DOM_NO_OPERATION;  // Cannot insert a line break there.
  }
  if (lineBreakType.value() == LineBreakType::BRElement) {
    Result<CreateLineBreakResult, nsresult> insertLineBreakResultOrError =
        InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                        atStartOfSelection, nsIEditor::eNext);
    if (MOZ_UNLIKELY(insertLineBreakResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
          "LineBreakType::BRElement, eNext) failed");
      return insertLineBreakResultOrError.unwrapErr();
    }
    CreateLineBreakResult insertLineBreakResult =
        insertLineBreakResultOrError.unwrap();
    MOZ_ASSERT(insertLineBreakResult.Handled());
    insertLineBreakResult.IgnoreCaretPointSuggestion();

    auto pointToPutCaret = insertLineBreakResult.UnwrapCaretPoint();
    if (MOZ_UNLIKELY(!pointToPutCaret.IsSet())) {
      NS_WARNING("Inserted <br> was unexpectedly removed");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    const WSScanResult backwardScanFromBeforeBRElementResult =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            WSRunScanner::Scan::EditableNodes,
            insertLineBreakResult.AtLineBreak<EditorDOMPoint>(),
            BlockInlineCheck::UseComputedDisplayStyle);
    if (MOZ_UNLIKELY(backwardScanFromBeforeBRElementResult.Failed())) {
      NS_WARNING(
          "WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary() failed");
      return Err(NS_ERROR_FAILURE);
    }

    const WSScanResult forwardScanFromAfterBRElementResult =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            WSRunScanner::Scan::EditableNodes, pointToPutCaret,
            BlockInlineCheck::UseComputedDisplayStyle);
    if (MOZ_UNLIKELY(forwardScanFromAfterBRElementResult.Failed())) {
      NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundary() failed");
      return Err(NS_ERROR_FAILURE);
    }
    const bool brElementIsAfterBlock =
        backwardScanFromBeforeBRElementResult.ReachedBlockBoundary() ||
        // FIXME: This is wrong considering because the inline editing host may
        // be surrounded by visible inline content.  However, WSRunScanner is
        // not aware of block boundary around it and stopping this change causes
        // starting to fail some WPT.  Therefore, we need to keep doing this for
        // now.
        backwardScanFromBeforeBRElementResult
            .ReachedInlineEditingHostBoundary();
    const bool brElementIsBeforeBlock =
        forwardScanFromAfterBRElementResult.ReachedBlockBoundary() ||
        // FIXME: See above comment.
        forwardScanFromAfterBRElementResult.ReachedInlineEditingHostBoundary();
    const bool isEmptyEditingHost = HTMLEditUtils::IsEmptyNode(
        *editingHost, {EmptyCheckOption::TreatNonEditableContentAsInvisible});
    if (brElementIsBeforeBlock &&
        (isEmptyEditingHost || !brElementIsAfterBlock)) {
      // Empty last line is invisible if it's immediately before either parent
      // or another block's boundary so that we need to put invisible <br>
      // element here for making it visible.
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError =
              WhiteSpaceVisibilityKeeper::InsertLineBreak(
                  LineBreakType::BRElement, *this, pointToPutCaret);
      if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::InsertLineBreak(LineBreakType::"
            "BRElement) failed");
        return insertPaddingBRElementResultOrError.unwrapErr();
      }
      CreateLineBreakResult insertPaddingBRElementResult =
          insertPaddingBRElementResultOrError.unwrap();
      pointToPutCaret =
          insertPaddingBRElementResult.AtLineBreak<EditorDOMPoint>();
      insertPaddingBRElementResult.IgnoreCaretPointSuggestion();
    } else if (forwardScanFromAfterBRElementResult
                   .InVisibleOrCollapsibleCharacters()) {
      pointToPutCaret = forwardScanFromAfterBRElementResult
                            .PointAtReachedContent<EditorDOMPoint>();
    } else if (forwardScanFromAfterBRElementResult.ReachedSpecialContent()) {
      // Next inserting text should be inserted into styled inline elements if
      // they have first visible thing in the new line.
      pointToPutCaret = forwardScanFromAfterBRElementResult
                            .PointAtReachedContent<EditorDOMPoint>();
    }

    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "CreateElementResult::SuggestCaretPointTo() failed");
    return rv;
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(*editingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  atStartOfSelection = GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!atStartOfSelection.IsInContentNode())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(atStartOfSelection.IsSetAndValidInComposedDoc());

  // Do nothing if the node is read-only
  if (!HTMLEditUtils::IsSimplyEditableNode(
          *atStartOfSelection.GetContainer())) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  Result<EditorDOMPoint, nsresult> insertLineFeedResult =
      HandleInsertLinefeed(atStartOfSelection, *editingHost);
  if (MOZ_UNLIKELY(insertLineFeedResult.isErr())) {
    NS_WARNING("HTMLEditor::HandleInsertLinefeed() failed");
    return insertLineFeedResult.unwrapErr();
  }
  rv = CollapseSelectionTo(insertLineFeedResult.inspect());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

Result<EditActionResult, nsresult>
HTMLEditor::InsertParagraphSeparatorAsSubAction(const Element& aEditingHost) {
  if (NS_WARN_IF(!mInitSucceeded)) {
    return Err(NS_ERROR_NOT_INITIALIZED);
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction(
        CheckSelectionInReplacedElement::OnlyWhenNotInSameNode);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  // XXX This may be called by execCommand() with "insertParagraph".
  //     In such case, naming the transaction "TypingTxnName" is odd.
  AutoPlaceholderBatch treatAsOneTransaction(*this, *nsGkAtoms::TypingTxnName,
                                             ScrollSelectionIntoView::Yes,
                                             __FUNCTION__);

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertParagraphSeparator, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  UndefineCaretBidiLevel();

  // If the selection isn't collapsed, delete it.
  if (!SelectionRef().IsCollapsed()) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eStrip) failed");
      return Err(rv);
    }
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  {
    // If the editing host is the body element, the selection may be outside
    // aEditingHost.  In the case, we should use the editing host outside the
    // <body> only here for keeping our traditional behavior for now.
    // This should be fixed in bug 1634351.
    const Element* editingHostMaybeOutsideBody = &aEditingHost;
    if (aEditingHost.IsHTMLElement(nsGkAtoms::body)) {
      editingHostMaybeOutsideBody = ComputeEditingHost(LimitInBodyElement::No);
      if (NS_WARN_IF(!editingHostMaybeOutsideBody)) {
        return Err(NS_ERROR_FAILURE);
      }
    }
    selectionRanges.EnsureOnlyEditableRanges(*editingHostMaybeOutsideBody);
    if (NS_WARN_IF(selectionRanges.Ranges().IsEmpty())) {
      return Err(NS_ERROR_FAILURE);
    }
  }

  auto pointToInsert =
      selectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!pointToInsert.IsInContentNode())) {
    return Err(NS_ERROR_FAILURE);
  }
  while (true) {
    Element* element = pointToInsert.GetContainerOrContainerParentElement();
    if (MOZ_UNLIKELY(!element)) {
      return Err(NS_ERROR_FAILURE);
    }
    // If the element can have a <br> element (it means that the element or its
    // container must be able to have <div> or <p> too), we can handle
    // insertParagraph at the point.
    if (HTMLEditUtils::CanNodeContain(*element, *nsGkAtoms::br)) {
      break;
    }
    // Otherwise, try to insert paragraph at the parent.
    pointToInsert = pointToInsert.ParentPoint();
  }

  if (IsMailEditor()) {
    if (RefPtr<Element> mailCiteElement = GetMostDistantAncestorMailCiteElement(
            *pointToInsert.ContainerAs<nsIContent>())) {
      // Split any mailcites in the way.  Should we abort this if we encounter
      // table cell boundaries?
      Result<CaretPoint, nsresult> caretPointOrError =
          HandleInsertParagraphInMailCiteElement(*mailCiteElement,
                                                 pointToInsert);
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::HandleInsertParagraphInMailCiteElement() failed");
        return caretPointOrError.propagateErr();
      }
      CaretPoint caretPoint = caretPointOrError.unwrap();
      MOZ_ASSERT(caretPoint.HasCaretPointSuggestion());
      MOZ_ASSERT(caretPoint.CaretPointRef().GetInterlinePosition() ==
                 InterlinePosition::StartOfNextLine);
      MOZ_ASSERT(caretPoint.CaretPointRef().GetChild());
      MOZ_ASSERT(
          caretPoint.CaretPointRef().GetChild()->IsHTMLElement(nsGkAtoms::br));
      nsresult rv = caretPoint.SuggestCaretPointTo(*this, {});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      return EditActionResult::HandledResult();
    }
  }

  // If the active editing host is an inline element, or if the active editing
  // host is the block parent itself and we're configured to use <br> as a
  // paragraph separator, just append a <br>.
  // If the editing host parent element is editable, it means that the editing
  // host must be a <body> element and the selection may be outside the body
  // element.  If the selection is outside the editing host, we should not
  // insert new paragraph nor <br> element.
  // XXX Currently, we don't support editing outside <body> element, but Blink
  //     does it.
  if (aEditingHost.GetParentElement() &&
      HTMLEditUtils::IsSimplyEditableNode(*aEditingHost.GetParentElement()) &&
      !nsContentUtils::ContentIsFlattenedTreeDescendantOf(
          pointToInsert.ContainerAs<nsIContent>(), &aEditingHost)) {
    return Err(NS_ERROR_EDITOR_NO_EDITABLE_RANGE);
  }

  auto InsertLineBreakInstead =
      [this](const Element* aEditableBlockElement,
             const EditorDOMPoint& aCandidatePointToSplit,
             ParagraphSeparator aDefaultParagraphSeparator,
             const Element& aEditingHost) {
        // If there is no block parent in the editing host, i.e., the editing
        // host itself is also a non-block element, we should insert a line
        // break.
        if (!aEditableBlockElement) {
          // XXX Chromium checks if the CSS box of the editing host is a block.
          return true;
        }

        // If the editable block element is not splittable, e.g., it's an
        // editing host, and the default paragraph separator is <br> or the
        // element cannot contain a <p> element, we should insert a <br>
        // element.
        if (!HTMLEditUtils::IsSplittableNode(*aEditableBlockElement)) {
          return aDefaultParagraphSeparator == ParagraphSeparator::br ||
                 !HTMLEditUtils::CanElementContainParagraph(
                     *aEditableBlockElement) ||
                 (aCandidatePointToSplit.IsInContentNode() &&
                  GetPreferredLineBreakType(
                      *aCandidatePointToSplit.ContainerAs<nsIContent>(),
                      aEditingHost)
                          .valueOr(LineBreakType::BRElement) ==
                      LineBreakType::Linefeed &&
                  HTMLEditUtils::IsDisplayOutsideInline(aEditingHost));
        }

        // If the nearest block parent is a single-line container declared in
        // the execCommand spec and not the editing host, we should separate the
        // block even if the default paragraph separator is <br> element.
        if (HTMLEditUtils::IsSingleLineContainer(*aEditableBlockElement)) {
          return false;
        }

        // Otherwise, unless there is no block ancestor which can contain <p>
        // element, we shouldn't insert a line break here.
        for (const Element* editableBlockAncestor = aEditableBlockElement;
             editableBlockAncestor;
             editableBlockAncestor = HTMLEditUtils::GetAncestorElement(
                 *editableBlockAncestor,
                 HTMLEditUtils::ClosestEditableBlockElementOrButtonElement,
                 BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
          if (HTMLEditUtils::CanElementContainParagraph(
                  *editableBlockAncestor)) {
            return false;
          }
        }
        return true;
      };

  // Look for the nearest parent block.  However, don't return error even if
  // there is no block parent here because in such case, i.e., editing host
  // is an inline element, we should insert <br> simply.
  RefPtr<Element> editableBlockElement =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *pointToInsert.ContainerAs<nsIContent>(),
          HTMLEditUtils::ClosestEditableBlockElementOrButtonElement,
          BlockInlineCheck::UseComputedDisplayOutsideStyle);

  // If we cannot insert a <p>/<div> element at the selection, we should insert
  // a <br> element or a linefeed instead.
  const ParagraphSeparator separator = GetDefaultParagraphSeparator();
  if (InsertLineBreakInstead(editableBlockElement, pointToInsert, separator,
                             aEditingHost)) {
    const Maybe<LineBreakType> lineBreakType = GetPreferredLineBreakType(
        *pointToInsert.ContainerAs<nsIContent>(), aEditingHost);
    if (MOZ_UNLIKELY(!lineBreakType)) {
      // Cannot insert a line break there.
      return EditActionResult::IgnoredResult();
    }
    if (lineBreakType.value() == LineBreakType::Linefeed) {
      Result<EditorDOMPoint, nsresult> insertLineFeedResult =
          HandleInsertLinefeed(pointToInsert, aEditingHost);
      if (MOZ_UNLIKELY(insertLineFeedResult.isErr())) {
        NS_WARNING("HTMLEditor::HandleInsertLinefeed() failed");
        return insertLineFeedResult.propagateErr();
      }
      nsresult rv = CollapseSelectionTo(insertLineFeedResult.inspect());
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
      return EditActionResult::HandledResult();
    }

    Result<CreateElementResult, nsresult> insertBRElementResult =
        HandleInsertBRElement(pointToInsert, aEditingHost);
    if (MOZ_UNLIKELY(insertBRElementResult.isErr())) {
      NS_WARNING("HTMLEditor::HandleInsertBRElement() failed");
      return insertBRElementResult.propagateErr();
    }
    nsresult rv =
        insertBRElementResult.inspect().SuggestCaretPointTo(*this, {});
    if (NS_FAILED(rv)) {
      NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  // If somebody wants to restrict caret position in a block element below,
  // we should guarantee it.  Otherwise, we can put caret to the candidate
  // point.
  auto CollapseSelection =
      [this](const EditorDOMPoint& aCandidatePointToPutCaret,
             const Element* aBlockElementShouldHaveCaret,
             const SuggestCaretOptions& aOptions)
          MOZ_CAN_RUN_SCRIPT -> nsresult {
    if (!aCandidatePointToPutCaret.IsSet()) {
      if (aOptions.contains(SuggestCaret::OnlyIfHasSuggestion)) {
        return NS_OK;
      }
      return aOptions.contains(SuggestCaret::AndIgnoreTrivialError)
                 ? NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR
                 : NS_ERROR_FAILURE;
    }
    EditorDOMPoint pointToPutCaret(aCandidatePointToPutCaret);
    if (aBlockElementShouldHaveCaret) {
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
              EditorDOMPoint>(*aBlockElementShouldHaveCaret,
                              aCandidatePointToPutCaret);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING(
            "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() "
            "failed, but ignored");
      } else if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
    }
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (NS_FAILED(rv) && MOZ_LIKELY(rv != NS_ERROR_EDITOR_DESTROYED) &&
        aOptions.contains(SuggestCaret::AndIgnoreTrivialError)) {
      rv = NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR;
    }
    return rv;
  };

  RefPtr<Element> blockElementToPutCaret;
  // If the default paragraph separator is not <br> and selection is not in
  // a splittable block element, we should wrap selected contents in a new
  // paragraph, then, split it.
  if (!HTMLEditUtils::IsSplittableNode(*editableBlockElement) &&
      separator != ParagraphSeparator::br) {
    MOZ_ASSERT(separator == ParagraphSeparator::div ||
               separator == ParagraphSeparator::p);
    // FIXME: If there is no splittable block element, the other browsers wrap
    // the right nodes into new paragraph, but keep the left node as-is.
    // We should follow them to make here simpler and better compatibility.
    Result<RefPtr<Element>, nsresult> suggestBlockElementToPutCaretOrError =
        FormatBlockContainerWithTransaction(
            selectionRanges,
            MOZ_KnownLive(HTMLEditor::ToParagraphSeparatorTagName(separator)),
            // For keeping the traditional behavior at insertParagraph command,
            // let's use the XUL paragraph state command targets even if we're
            // handling HTML insertParagraph command.
            FormatBlockMode::XULParagraphStateCommand, aEditingHost);
    if (MOZ_UNLIKELY(suggestBlockElementToPutCaretOrError.isErr())) {
      NS_WARNING("HTMLEditor::FormatBlockContainerWithTransaction() failed");
      return suggestBlockElementToPutCaretOrError.propagateErr();
    }
    if (selectionRanges.HasSavedRanges()) {
      selectionRanges.RestoreFromSavedRanges();
    }
    pointToInsert = selectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!pointToInsert.IsInContentNode())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    MOZ_ASSERT(pointToInsert.IsSetAndValid());
    blockElementToPutCaret = suggestBlockElementToPutCaretOrError.unwrap();

    editableBlockElement = HTMLEditUtils::GetInclusiveAncestorElement(
        *pointToInsert.ContainerAs<nsIContent>(),
        HTMLEditUtils::ClosestEditableBlockElementOrButtonElement,
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (NS_WARN_IF(!editableBlockElement)) {
      return Err(NS_ERROR_UNEXPECTED);
    }
    if (NS_WARN_IF(!HTMLEditUtils::IsSplittableNode(*editableBlockElement))) {
      // Didn't create a new block for some reason, fall back to <br>
      Result<CreateElementResult, nsresult> insertBRElementResult =
          HandleInsertBRElement(pointToInsert, aEditingHost);
      if (MOZ_UNLIKELY(insertBRElementResult.isErr())) {
        NS_WARNING("HTMLEditor::HandleInsertBRElement() failed");
        return insertBRElementResult.propagateErr();
      }
      CreateElementResult unwrappedInsertBRElementResult =
          insertBRElementResult.unwrap();
      EditorDOMPoint pointToPutCaret =
          unwrappedInsertBRElementResult.UnwrapCaretPoint();
      if (MOZ_UNLIKELY(!pointToPutCaret.IsSet())) {
        NS_WARNING(
            "HTMLEditor::HandleInsertBRElement() didn't suggest a point to put "
            "caret");
        return Err(NS_ERROR_FAILURE);
      }
      nsresult rv =
          CollapseSelection(pointToPutCaret, blockElementToPutCaret, {});
      if (NS_FAILED(rv)) {
        NS_WARNING("CollapseSelection() failed");
        return Err(rv);
      }
      return EditActionResult::HandledResult();
    }
    // We want to collapse selection in the editable block element.
    blockElementToPutCaret = editableBlockElement;
  }

  // If block is empty, populate with br.  (For example, imagine a div that
  // contains the word "text".  The user selects "text" and types return.
  // "Text" is deleted leaving an empty block.  We want to put in one br to
  // make block have a line.  Then code further below will put in a second br.)
  RefPtr<Element> insertedPaddingBRElement;
  if (HTMLEditUtils::IsEmptyBlockElement(
          *editableBlockElement,
          {EmptyCheckOption::TreatSingleBRElementAsVisible},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
        InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                        EditorDOMPoint::AtEndOf(*editableBlockElement));
    if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
          "LineBreakType::BRElement) failed");
      return insertBRElementResultOrError.propagateErr();
    }
    CreateLineBreakResult insertBRElementResult =
        insertBRElementResultOrError.unwrap();
    insertBRElementResult.IgnoreCaretPointSuggestion();
    insertedPaddingBRElement = &insertBRElementResult->BRElementRef();

    pointToInsert = selectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!pointToInsert.IsInContentNode())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  RefPtr<Element> maybeNonEditableListItem =
      HTMLEditUtils::GetClosestAncestorListItemElement(*editableBlockElement,
                                                       &aEditingHost);
  if (maybeNonEditableListItem &&
      HTMLEditUtils::IsSplittableNode(*maybeNonEditableListItem)) {
    Result<InsertParagraphResult, nsresult> insertParagraphInListItemResult =
        HandleInsertParagraphInListItemElement(*maybeNonEditableListItem,
                                               pointToInsert, aEditingHost);
    if (MOZ_UNLIKELY(insertParagraphInListItemResult.isErr())) {
      if (NS_WARN_IF(insertParagraphInListItemResult.unwrapErr() ==
                     NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING(
          "HTMLEditor::HandleInsertParagraphInListItemElement() failed, but "
          "ignored");
      return EditActionResult::HandledResult();
    }
    InsertParagraphResult unwrappedInsertParagraphInListItemResult =
        insertParagraphInListItemResult.unwrap();
    MOZ_ASSERT(unwrappedInsertParagraphInListItemResult.Handled());
    MOZ_ASSERT(unwrappedInsertParagraphInListItemResult.GetNewNode());
    const RefPtr<Element> listItemOrParagraphElement =
        unwrappedInsertParagraphInListItemResult.UnwrapNewNode();
    const EditorDOMPoint pointToPutCaret =
        unwrappedInsertParagraphInListItemResult.UnwrapCaretPoint();
    nsresult rv = CollapseSelection(pointToPutCaret, listItemOrParagraphElement,
                                    {SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CollapseSelection() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                         "CollapseSelection() failed, but ignored");
    return EditActionResult::HandledResult();
  }

  if (HTMLEditUtils::IsHeader(*editableBlockElement)) {
    Result<InsertParagraphResult, nsresult>
        insertParagraphInHeadingElementResult =
            HandleInsertParagraphInHeadingElement(*editableBlockElement,
                                                  pointToInsert);
    if (MOZ_UNLIKELY(insertParagraphInHeadingElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::HandleInsertParagraphInHeadingElement() failed, but "
          "ignored");
      return EditActionResult::HandledResult();
    }
    InsertParagraphResult unwrappedInsertParagraphInHeadingElementResult =
        insertParagraphInHeadingElementResult.unwrap();
    if (unwrappedInsertParagraphInHeadingElementResult.Handled()) {
      MOZ_ASSERT(unwrappedInsertParagraphInHeadingElementResult.GetNewNode());
      blockElementToPutCaret =
          unwrappedInsertParagraphInHeadingElementResult.UnwrapNewNode();
    }
    const EditorDOMPoint pointToPutCaret =
        unwrappedInsertParagraphInHeadingElementResult.UnwrapCaretPoint();
    nsresult rv = CollapseSelection(pointToPutCaret, blockElementToPutCaret,
                                    {SuggestCaret::OnlyIfHasSuggestion,
                                     SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CollapseSelection() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                         "CollapseSelection() failed, but ignored");
    return EditActionResult::HandledResult();
  }

  // XXX Ideally, we should take same behavior with both <p> container and
  //     <div> container.  However, we are still using <br> as default
  //     paragraph separator (non-standard) and we've split only <p> container
  //     long time.  Therefore, some web apps may depend on this behavior like
  //     Gmail.  So, let's use traditional odd behavior only when the default
  //     paragraph separator is <br>.  Otherwise, take consistent behavior
  //     between <p> container and <div> container.
  if ((separator == ParagraphSeparator::br &&
       editableBlockElement->IsHTMLElement(nsGkAtoms::p)) ||
      (separator != ParagraphSeparator::br &&
       editableBlockElement->IsAnyOfHTMLElements(nsGkAtoms::p,
                                                 nsGkAtoms::div))) {
    // Paragraphs: special rules to look for <br>s
    Result<SplitNodeResult, nsresult> splitNodeResult =
        HandleInsertParagraphInParagraph(
            *editableBlockElement,
            insertedPaddingBRElement ? EditorDOMPoint(insertedPaddingBRElement)
                                     : pointToInsert,
            aEditingHost);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::HandleInsertParagraphInParagraph() failed");
      return splitNodeResult.propagateErr();
    }
    if (splitNodeResult.inspect().Handled()) {
      SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
      const RefPtr<Element> rightParagraphElement =
          unwrappedSplitNodeResult.DidSplit()
              ? unwrappedSplitNodeResult.GetNextContentAs<Element>()
              : blockElementToPutCaret.get();
      const EditorDOMPoint pointToPutCaret =
          unwrappedSplitNodeResult.UnwrapCaretPoint();
      nsresult rv = CollapseSelection(pointToPutCaret, rightParagraphElement,
                                      {SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CollapseSelection() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                           "CollapseSelection() failed, but ignored");
      return EditActionResult::HandledResult();
    }
    MOZ_ASSERT(!splitNodeResult.inspect().HasCaretPointSuggestion());

    // Fall through, if HandleInsertParagraphInParagraph() didn't handle it.
    MOZ_ASSERT(pointToInsert.IsSetAndValid(),
               "HTMLEditor::HandleInsertParagraphInParagraph() shouldn't touch "
               "the DOM tree if it returns not-handled state");
  }

  // If nobody handles this edit action, let's insert new <br> at the selection.
  Result<CreateElementResult, nsresult> insertBRElementResult =
      HandleInsertBRElement(pointToInsert, aEditingHost);
  if (MOZ_UNLIKELY(insertBRElementResult.isErr())) {
    NS_WARNING("HTMLEditor::HandleInsertBRElement() failed");
    return insertBRElementResult.propagateErr();
  }
  CreateElementResult unwrappedInsertBRElementResult =
      insertBRElementResult.unwrap();
  EditorDOMPoint pointToPutCaret =
      unwrappedInsertBRElementResult.UnwrapCaretPoint();
  rv = CollapseSelection(pointToPutCaret, blockElementToPutCaret, {});
  if (NS_FAILED(rv)) {
    NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

Result<CreateElementResult, nsresult> HTMLEditor::HandleInsertBRElement(
    const EditorDOMPoint& aPointToBreak, const Element& aEditingHost) {
  MOZ_ASSERT(aPointToBreak.IsInContentNode());
  MOZ_ASSERT(IsEditActionDataAvailable());

  const bool editingHostIsEmpty = HTMLEditUtils::IsEmptyNode(
      aEditingHost, {EmptyCheckOption::TreatNonEditableContentAsInvisible});
  const WSRunScanner wsRunScanner(WSRunScanner::Scan::EditableNodes,
                                  aPointToBreak,
                                  BlockInlineCheck::UseComputedDisplayStyle);
  const WSScanResult backwardScanResult =
      wsRunScanner.ScanPreviousVisibleNodeOrBlockBoundaryFrom(aPointToBreak);
  if (MOZ_UNLIKELY(backwardScanResult.Failed())) {
    NS_WARNING(
        "WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundaryFrom() failed");
    return Err(NS_ERROR_FAILURE);
  }
  const bool brElementIsAfterBlock =
      backwardScanResult.ReachedBlockBoundary() ||
      // FIXME: This is wrong considering because the inline editing host may
      // be surrounded by visible inline content.  However, WSRunScanner is
      // not aware of block boundary around it and stopping this change causes
      // starting to fail some WPT.  Therefore, we need to keep doing this for
      // now.
      backwardScanResult.ReachedInlineEditingHostBoundary();
  const WSScanResult forwardScanResult =
      wsRunScanner.ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
          aPointToBreak);
  if (MOZ_UNLIKELY(forwardScanResult.Failed())) {
    NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundaryFrom() failed");
    return Err(NS_ERROR_FAILURE);
  }
  const bool brElementIsBeforeBlock =
      forwardScanResult.ReachedBlockBoundary() ||
      // FIXME: See above comment
      forwardScanResult.ReachedInlineEditingHostBoundary();

  // First, insert a <br> element.
  RefPtr<Element> brElement;
  if (IsPlaintextMailComposer()) {
    Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
        InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                        aPointToBreak);
    if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
          "LineBreakType::BRElement) failed");
      return insertBRElementResultOrError.propagateErr();
    }
    CreateLineBreakResult insertBRElementResult =
        insertBRElementResultOrError.unwrap();
    // We'll return with suggesting new caret position and nobody refers
    // selection after here.  So we don't need to update selection here.
    insertBRElementResult.IgnoreCaretPointSuggestion();
    brElement = &insertBRElementResult->BRElementRef();
  } else {
    EditorDOMPoint pointToBreak(aPointToBreak);
    // If the container of the break is a link, we need to split it and
    // insert new <br> between the split links.
    RefPtr<Element> linkNode =
        HTMLEditor::GetLinkElement(pointToBreak.GetContainer());
    if (linkNode) {
      // FIXME: Normalize surrounding white-spaces before splitting the
      // insertion point here.
      Result<SplitNodeResult, nsresult> splitLinkNodeResult =
          SplitNodeDeepWithTransaction(
              *linkNode, pointToBreak,
              SplitAtEdges::eDoNotCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitLinkNodeResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
            "eDoNotCreateEmptyContainer) failed");
        return splitLinkNodeResult.propagateErr();
      }
      // TODO: Some methods called by
      //       WhiteSpaceVisibilityKeeper::InsertLineBreak() use
      //       ComputeEditingHost() which depends on selection.  Therefore,
      //       we cannot skip updating selection here.
      nsresult rv = splitLinkNodeResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
      if (NS_FAILED(rv)) {
        NS_WARNING("SplitNodeResult::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      pointToBreak =
          splitLinkNodeResult.inspect().AtSplitPoint<EditorDOMPoint>();
    }
    Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
        WhiteSpaceVisibilityKeeper::InsertLineBreak(LineBreakType::BRElement,
                                                    *this, pointToBreak);
    if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::InsertLineBreak(LineBreakType::"
          "BRElement) failed");
      return insertBRElementResultOrError.propagateErr();
    }
    CreateLineBreakResult insertBRElementResult =
        insertBRElementResultOrError.unwrap();
    // We'll return with suggesting new caret position and nobody refers
    // selection after here.  So we don't need to update selection here.
    insertBRElementResult.IgnoreCaretPointSuggestion();
    brElement = &insertBRElementResult->BRElementRef();
  }

  if (MOZ_UNLIKELY(!brElement->GetParentNode())) {
    NS_WARNING("Inserted <br> element was removed by the web app");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  auto afterBRElement = EditorDOMPoint::After(brElement);

  const auto InsertAdditionalInvisibleLineBreak =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<CreateLineBreakResult, nsresult> {
    // Empty last line is invisible if it's immediately before either parent or
    // another block's boundary so that we need to put invisible <br> element
    // here for making it visible.
    Result<CreateLineBreakResult, nsresult>
        insertPaddingBRElementResultOrError =
            WhiteSpaceVisibilityKeeper::InsertLineBreak(
                LineBreakType::BRElement, *this, afterBRElement);
    NS_WARNING_ASSERTION(insertPaddingBRElementResultOrError.isOk(),
                         "WhiteSpaceVisibilityKeeper::InsertLineBreak("
                         "LineBreakType::BRElement) failed");
    // afterBRElement points after the first <br> with referring an old child.
    // Therefore, we need to update it with new child which is the new invisible
    // <br>.
    afterBRElement = insertPaddingBRElementResultOrError.inspect()
                         .AtLineBreak<EditorDOMPoint>();
    return insertPaddingBRElementResultOrError;
  };

  if (brElementIsAfterBlock && brElementIsBeforeBlock) {
    // We just placed a <br> between block boundaries.  This is the one case
    // where we want the selection to be before the br we just placed, as the
    // br will be on a new line, rather than at end of prior line.
    // XXX brElementIsAfterBlock and brElementIsBeforeBlock were set before
    //     modifying the DOM tree.  So, now, the <br> element may not be
    //     between blocks.
    EditorDOMPoint pointToPutCaret;
    if (editingHostIsEmpty) {
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError =
              InsertAdditionalInvisibleLineBreak();
      if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
        return insertPaddingBRElementResultOrError.propagateErr();
      }
      insertPaddingBRElementResultOrError.unwrap().IgnoreCaretPointSuggestion();
      pointToPutCaret = std::move(afterBRElement);
    } else {
      pointToPutCaret =
          EditorDOMPoint(brElement, InterlinePosition::StartOfNextLine);
    }
    return CreateElementResult(std::move(brElement),
                               std::move(pointToPutCaret));
  }

  const WSScanResult forwardScanFromAfterBRElementResult =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          WSRunScanner::Scan::EditableNodes, afterBRElement,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (MOZ_UNLIKELY(forwardScanFromAfterBRElementResult.Failed())) {
    NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundary() failed");
    return Err(NS_ERROR_FAILURE);
  }
  if (forwardScanFromAfterBRElementResult.ReachedBRElement()) {
    // The next thing after the break we inserted is another break.  Move the
    // second break to be the first break's sibling.  This will prevent them
    // from being in different inline nodes, which would break
    // SetInterlinePosition().  It will also assure that if the user clicks
    // away and then clicks back on their new blank line, they will still get
    // the style from the line above.
    if (brElement->GetNextSibling() !=
        forwardScanFromAfterBRElementResult.BRElementPtr()) {
      MOZ_ASSERT(forwardScanFromAfterBRElementResult.BRElementPtr());
      Result<MoveNodeResult, nsresult> moveBRElementResult =
          MoveNodeWithTransaction(
              MOZ_KnownLive(
                  *forwardScanFromAfterBRElementResult.BRElementPtr()),
              afterBRElement);
      if (MOZ_UNLIKELY(moveBRElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
        return moveBRElementResult.propagateErr();
      }
      nsresult rv = moveBRElementResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
      // afterBRElement points after the first <br> with referring an old child.
      // Therefore, we need to update it with new child which is the new
      // invisible <br>.
      afterBRElement.Set(forwardScanFromAfterBRElementResult.BRElementPtr());
    }
  } else if ((forwardScanFromAfterBRElementResult.ReachedBlockBoundary() ||
              // FIXME: This is wrong considering because the inline editing
              // host may be surrounded by visible inline content.  However,
              // WSRunScanner is not aware of block boundary around it and
              // stopping this change causes starting to fail some WPT.
              // Therefore, we need to keep doing this for now.
              forwardScanFromAfterBRElementResult
                  .ReachedInlineEditingHostBoundary()) &&
             !brElementIsAfterBlock) {
    Result<CreateLineBreakResult, nsresult>
        insertPaddingBRElementResultOrError =
            InsertAdditionalInvisibleLineBreak();
    if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
      return insertPaddingBRElementResultOrError.propagateErr();
    }
    insertPaddingBRElementResultOrError.unwrap().IgnoreCaretPointSuggestion();
  }

  // We want the caret to stick to whatever is past the break.  This is because
  // the break is on the same line we were on, but the next content will be on
  // the following line.

  // An exception to this is if the break has a next sibling that is a block
  // node.  Then we stick to the left to avoid an uber caret.
  nsIContent* nextSiblingOfBRElement = brElement->GetNextSibling();
  afterBRElement.SetInterlinePosition(
      nextSiblingOfBRElement && HTMLEditUtils::IsBlockElement(
                                    *nextSiblingOfBRElement,
                                    BlockInlineCheck::UseComputedDisplayStyle)
          ? InterlinePosition::EndOfLine
          : InterlinePosition::StartOfNextLine);
  return CreateElementResult(std::move(brElement), afterBRElement);
}

Result<EditorDOMPoint, nsresult> HTMLEditor::HandleInsertLinefeed(
    const EditorDOMPoint& aPointToBreak, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aPointToBreak.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  const RefPtr<Document> document = GetDocument();
  MOZ_DIAGNOSTIC_ASSERT(document);
  if (NS_WARN_IF(!document)) {
    return Err(NS_ERROR_FAILURE);
  }

  // TODO: The following code is duplicated from `HandleInsertText`.  They
  //       should be merged when we fix bug 92921.

  Result<EditorDOMPoint, nsresult> setStyleResult =
      CreateStyleForInsertText(aPointToBreak, aEditingHost);
  if (MOZ_UNLIKELY(setStyleResult.isErr())) {
    NS_WARNING("HTMLEditor::CreateStyleForInsertText() failed");
    return setStyleResult.propagateErr();
  }

  EditorDOMPoint pointToInsert = setStyleResult.inspect().IsSet()
                                     ? setStyleResult.inspect()
                                     : aPointToBreak;
  if (NS_WARN_IF(!pointToInsert.IsSetAndValid()) ||
      NS_WARN_IF(!pointToInsert.IsInContentNode())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  MOZ_ASSERT(pointToInsert.IsSetAndValid());

  // The node may not be able to have a text node so that we need to check it
  // here.
  if (!pointToInsert.IsInTextNode() &&
      !HTMLEditUtils::CanNodeContain(*pointToInsert.ContainerAs<nsIContent>(),
                                     *nsGkAtoms::textTagName)) {
    NS_WARNING(
        "HTMLEditor::HandleInsertLinefeed() couldn't insert a linefeed because "
        "the insertion position couldn't have text nodes");
    return Err(NS_ERROR_EDITOR_NO_EDITABLE_RANGE);
  }

  AutoRestore<bool> disableListener(
      EditSubActionDataRef().mAdjustChangedRangeFromListener);
  EditSubActionDataRef().mAdjustChangedRangeFromListener = false;

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  EditorDOMPoint pointToPutCaret;
  {
    AutoTrackDOMPoint trackingInsertingPosition(RangeUpdaterRef(),
                                                &pointToInsert);
    Result<CreateLineBreakResult, nsresult> insertLinefeedResultOrError =
        InsertLineBreak(WithTransaction::Yes, LineBreakType::Linefeed,
                        pointToInsert, eNext);
    if (MOZ_UNLIKELY(insertLinefeedResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
          "LineBreakType::Linefeed, eNext) failed");
      return insertLinefeedResultOrError.propagateErr();
    }
    pointToPutCaret = insertLinefeedResultOrError.unwrap().UnwrapCaretPoint();
  }

  // Insert a padding <br> if the inserted linefeed is followed by a block
  // boundary.  Note that it should always be <br> for avoiding padding line
  // breaks appear in `.textContent` value.
  if (pointToPutCaret.IsInContentNode() && pointToPutCaret.IsEndOfContainer()) {
    const WSRunScanner wsScannerAtCaret(
        WSRunScanner::Scan::EditableNodes, pointToPutCaret,
        BlockInlineCheck::UseComputedDisplayStyle);
    if (wsScannerAtCaret.StartsFromPreformattedLineBreak() &&
        (wsScannerAtCaret.EndsByBlockBoundary() ||
         wsScannerAtCaret.EndsByInlineEditingHostBoundary()) &&
        HTMLEditUtils::CanNodeContain(*wsScannerAtCaret.GetEndReasonContent(),
                                      *nsGkAtoms::br)) {
      AutoTrackDOMPoint trackingInsertedPosition(RangeUpdaterRef(),
                                                 &pointToInsert);
      AutoTrackDOMPoint trackingNewCaretPosition(RangeUpdaterRef(),
                                                 &pointToPutCaret);
      Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
          InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                          pointToPutCaret);
      if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
            "LineBreakType::BRElement) failed");
        return insertBRElementResultOrError.propagateErr();
      }
      CreateLineBreakResult insertBRElementResult =
          insertBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertBRElementResult.Handled());
      insertBRElementResult.IgnoreCaretPointSuggestion();
    }
  }

  // manually update the doc changed range so that
  // OnEndHandlingTopLevelEditSubActionInternal will clean up the correct
  // portion of the document.
  MOZ_ASSERT(pointToPutCaret.IsSet());
  if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
    // XXX Here is odd.  We did mChangedRange->SetStartAndEnd(pointToInsert,
    //     pointToPutCaret), but it always fails because of the latter is unset.
    //     Therefore, always returning NS_ERROR_FAILURE from here is the
    //     traditional behavior...
    // TODO: Stop updating the interline position of Selection with fixing here
    //       and returning expected point.
    DebugOnly<nsresult> rvIgnored =
        SelectionRef().SetInterlinePosition(InterlinePosition::EndOfLine);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Selection::SetInterlinePosition(InterlinePosition::"
                         "EndOfLine) failed, but ignored");
    if (NS_FAILED(TopLevelEditSubActionDataRef().mChangedRange->CollapseTo(
            pointToInsert))) {
      NS_WARNING("nsRange::CollapseTo() failed");
      return Err(NS_ERROR_FAILURE);
    }
    NS_WARNING(
        "We always return NS_ERROR_FAILURE here because of a failure of "
        "updating mChangedRange");
    return Err(NS_ERROR_FAILURE);
  }

  if (NS_FAILED(TopLevelEditSubActionDataRef().mChangedRange->SetStartAndEnd(
          pointToInsert.ToRawRangeBoundary(),
          pointToPutCaret.ToRawRangeBoundary()))) {
    NS_WARNING("nsRange::SetStartAndEnd() failed");
    return Err(NS_ERROR_FAILURE);
  }

  pointToPutCaret.SetInterlinePosition(InterlinePosition::EndOfLine);
  return pointToPutCaret;
}

Result<CaretPoint, nsresult> HTMLEditor::HandleInsertParagraphInMailCiteElement(
    Element& aMailCiteElement, const EditorDOMPoint& aPointToSplit) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToSplit.IsSet());
  NS_ASSERTION(!HTMLEditUtils::IsEmptyNode(
                   aMailCiteElement,
                   {EmptyCheckOption::TreatNonEditableContentAsInvisible}),
               "The mail-cite element will be deleted, does it expected result "
               "for you?");

  auto splitCiteElementResult =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    EditorDOMPoint pointToSplit(aPointToSplit);

    // If our selection is just before a break, nudge it to be just after
    // it. This does two things for us.  It saves us the trouble of having
    // to add a break here ourselves to preserve the "blockness" of the
    // inline span mailquote (in the inline case), and : it means the break
    // won't end up making an empty line that happens to be inside a
    // mailquote (in either inline or block case). The latter can confuse a
    // user if they click there and start typing, because being in the
    // mailquote may affect wrapping behavior, or font color, etc.
    const WSScanResult forwardScanFromPointToSplitResult =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            WSRunScanner::Scan::EditableNodes, pointToSplit,
            BlockInlineCheck::UseHTMLDefaultStyle);
    if (forwardScanFromPointToSplitResult.Failed()) {
      return Err(NS_ERROR_FAILURE);
    }
    // If selection start point is before a break and it's inside the
    // mailquote, let's split it after the visible node.
    if (forwardScanFromPointToSplitResult.ReachedBRElement() &&
        forwardScanFromPointToSplitResult.BRElementPtr() != &aMailCiteElement &&
        aMailCiteElement.Contains(
            forwardScanFromPointToSplitResult.BRElementPtr())) {
      pointToSplit = forwardScanFromPointToSplitResult
                         .PointAfterReachedContent<EditorDOMPoint>();
    }

    if (NS_WARN_IF(!pointToSplit.IsInContentNode())) {
      return Err(NS_ERROR_FAILURE);
    }

    if (StaticPrefs::editor_white_space_normalization_blink_compatible()) {
      Result<EditorDOMPoint, nsresult> pointToSplitOrError =
          WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt(
              *this, pointToSplit,
              {WhiteSpaceVisibilityKeeper::NormalizeOption::
                   StopIfPrecedingWhiteSpacesEndsWithNBP,
               WhiteSpaceVisibilityKeeper::NormalizeOption::
                   StopIfFollowingWhiteSpacesStartsWithNBSP});
      if (MOZ_UNLIKELY(pointToSplitOrError.isErr())) {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt() "
            "failed");
        return pointToSplitOrError.propagateErr();
      }
      pointToSplit = pointToSplitOrError.unwrap();
      if (NS_WARN_IF(!pointToSplit.IsInContentNode())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }

    Result<SplitNodeResult, nsresult> splitResult =
        SplitNodeDeepWithTransaction(aMailCiteElement, pointToSplit,
                                     SplitAtEdges::eDoNotCreateEmptyContainer);
    if (MOZ_UNLIKELY(splitResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeDeepWithTransaction(aMailCiteElement, "
          "SplitAtEdges::eDoNotCreateEmptyContainer) failed");
      return splitResult;
    }
    nsresult rv = splitResult.inspect().SuggestCaretPointTo(
        *this, {SuggestCaret::OnlyIfHasSuggestion,
                SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (NS_FAILED(rv)) {
      NS_WARNING("SplitNodeResult::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    return splitResult;
  }();
  if (MOZ_UNLIKELY(splitCiteElementResult.isErr())) {
    NS_WARNING("Failed to split a mail-cite element");
    return splitCiteElementResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitCiteElementResult =
      splitCiteElementResult.unwrap();
  // When adding caret suggestion to SplitNodeResult, here didn't change
  // selection so that just ignore it.
  unwrappedSplitCiteElementResult.IgnoreCaretPointSuggestion();

  // Add an invisible <br> to the end of left cite node if it was a <span> of
  // style="display: block".  This is important, since when serializing the cite
  // to plain text, the span which caused the visual break is discarded.  So the
  // added <br> will guarantee that the serializer will insert a break where the
  // user saw one.
  // FYI: unwrappedSplitCiteElementResult grabs the previous node and the next
  //      node with nsCOMPtr or EditorDOMPoint.  So, it's safe to access
  //      leftCiteElement and rightCiteElement even after changing the DOM tree
  //      and/or selection even though it's raw pointer.
  auto* const leftCiteElement =
      unwrappedSplitCiteElementResult.GetPreviousContentAs<Element>();
  auto* const rightCiteElement =
      unwrappedSplitCiteElementResult.GetNextContentAs<Element>();
  if (leftCiteElement && leftCiteElement->IsHTMLElement(nsGkAtoms::span) &&
      // XXX Oh, this depends on layout information of new element, and it's
      //     created by the hacky flush in DoSplitNode().  So we need to
      //     redesign around this for bug 1710784.
      leftCiteElement->GetPrimaryFrame() &&
      leftCiteElement->GetPrimaryFrame()->IsBlockFrameOrSubclass()) {
    nsIContent* lastChild = leftCiteElement->GetLastChild();
    if (lastChild && !lastChild->IsHTMLElement(nsGkAtoms::br)) {
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              EditorDOMPoint::AtEndOf(*leftCiteElement));
      if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
            "LineBreakType::BRElement) failed");
        return insertPaddingBRElementResultOrError.propagateErr();
      }
      CreateLineBreakResult insertPaddingBRElementResult =
          insertPaddingBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertPaddingBRElementResult.Handled());
      // We don't need to update selection here because we'll do another
      // InsertLineBreak call soon.
      insertPaddingBRElementResult.IgnoreCaretPointSuggestion();
    }
  }

  // In most cases, <br> should be inserted after current cite.  However, if
  // left cite hasn't been created because the split point was start of the
  // cite node, <br> should be inserted before the current cite.
  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(
          WithTransaction::Yes, LineBreakType::BRElement,
          unwrappedSplitCiteElementResult.AtSplitPoint<EditorDOMPoint>());
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return Err(insertBRElementResultOrError.unwrapErr());
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  MOZ_ASSERT(insertBRElementResult.Handled());
  // We'll return with suggesting caret position.  Therefore, we don't need
  // to update selection here.
  insertBRElementResult.IgnoreCaretPointSuggestion();
  // if aMailCiteElement wasn't a block, we might also want another break before
  // it. We need to examine the content both before the br we just added and
  // also just after it.  If we don't have another br or block boundary
  // adjacent, then we will need a 2nd br added to achieve blank line that user
  // expects.
  if (HTMLEditUtils::IsInlineContent(aMailCiteElement,
                                     BlockInlineCheck::UseHTMLDefaultStyle)) {
    nsresult rvOfInsertPaddingBRElement = [&]() MOZ_CAN_RUN_SCRIPT {
      const auto pointToCreateNewBRElement =
          insertBRElementResult.AtLineBreak<EditorDOMPoint>();
      // XXX Cannot we replace this complicated check with just a call of
      //     HTMLEditUtils::IsVisibleBRElement with
      //     resultOfInsertingBRElement.inspect()?
      const WSScanResult backwardScanFromPointToCreateNewBRElementResult =
          WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
              WSRunScanner::Scan::EditableNodes, pointToCreateNewBRElement,
              BlockInlineCheck::UseHTMLDefaultStyle);
      if (MOZ_UNLIKELY(
              backwardScanFromPointToCreateNewBRElementResult.Failed())) {
        NS_WARNING(
            "WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary() "
            "failed");
        return NS_ERROR_FAILURE;
      }
      if (!backwardScanFromPointToCreateNewBRElementResult
               .InVisibleOrCollapsibleCharacters() &&
          !backwardScanFromPointToCreateNewBRElementResult
               .ReachedSpecialContent()) {
        return NS_SUCCESS_DOM_NO_OPERATION;
      }
      const WSScanResult forwardScanFromPointAfterNewBRElementResult =
          WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
              WSRunScanner::Scan::EditableNodes,
              EditorRawDOMPoint::After(pointToCreateNewBRElement),
              BlockInlineCheck::UseHTMLDefaultStyle);
      if (MOZ_UNLIKELY(forwardScanFromPointAfterNewBRElementResult.Failed())) {
        NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundary() failed");
        return NS_ERROR_FAILURE;
      }
      if (!forwardScanFromPointAfterNewBRElementResult
               .InVisibleOrCollapsibleCharacters() &&
          !forwardScanFromPointAfterNewBRElementResult
               .ReachedSpecialContent() &&
          // In case we're at the very end.
          !forwardScanFromPointAfterNewBRElementResult
               .ReachedCurrentBlockBoundary()) {
        return NS_SUCCESS_DOM_NO_OPERATION;
      }
      Result<CreateLineBreakResult, nsresult>
          insertAnotherBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              pointToCreateNewBRElement);
      if (MOZ_UNLIKELY(insertAnotherBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
            "LineBreakType::BRElement) failed");
        return insertAnotherBRElementResultOrError.unwrapErr();
      }
      CreateLineBreakResult insertAnotherBRElementResult =
          insertAnotherBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertAnotherBRElementResult.Handled());
      insertAnotherBRElementResult.IgnoreCaretPointSuggestion();
      return NS_OK;
    }();

    if (NS_FAILED(rvOfInsertPaddingBRElement)) {
      NS_WARNING(
          "Failed to insert additional <br> element before the inline right "
          "mail-cite element");
      return Err(rvOfInsertPaddingBRElement);
    }
  }

  if (leftCiteElement &&
      HTMLEditUtils::IsEmptyNode(
          *leftCiteElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    // MOZ_KnownLive(leftCiteElement) because it's grabbed by
    // unwrappedSplitCiteElementResult.
    nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*leftCiteElement));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  if (rightCiteElement &&
      HTMLEditUtils::IsEmptyNode(
          *rightCiteElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    // MOZ_KnownLive(rightCiteElement) because it's grabbed by
    // unwrappedSplitCiteElementResult.
    nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*rightCiteElement));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  if (NS_WARN_IF(!insertBRElementResult.LineBreakIsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  auto pointToPutCaret = insertBRElementResult.AtLineBreak<EditorDOMPoint>();
  pointToPutCaret.SetInterlinePosition(InterlinePosition::StartOfNextLine);
  return CaretPoint(std::move(pointToPutCaret));
}

HTMLEditor::CharPointData
HTMLEditor::GetPreviousCharPointDataForNormalizingWhiteSpaces(
    const EditorDOMPointInText& aPoint) const {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  if (!aPoint.IsStartOfContainer()) {
    return CharPointData::InSameTextNode(
        HTMLEditor::GetPreviousCharPointType(aPoint));
  }
  const auto previousCharPoint =
      WSRunScanner::GetPreviousCharPoint<EditorRawDOMPointInText>(
          WSRunScanner::Scan::EditableNodes, aPoint,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (!previousCharPoint.IsSet()) {
    return CharPointData::InDifferentTextNode(CharPointType::TextEnd);
  }
  return CharPointData::InDifferentTextNode(
      HTMLEditor::GetCharPointType(previousCharPoint));
}

HTMLEditor::CharPointData
HTMLEditor::GetInclusiveNextCharPointDataForNormalizingWhiteSpaces(
    const EditorDOMPointInText& aPoint) const {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  if (!aPoint.IsEndOfContainer()) {
    return CharPointData::InSameTextNode(HTMLEditor::GetCharPointType(aPoint));
  }
  const auto nextCharPoint =
      WSRunScanner::GetInclusiveNextCharPoint<EditorRawDOMPointInText>(
          WSRunScanner::Scan::EditableNodes, aPoint,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (!nextCharPoint.IsSet()) {
    return CharPointData::InDifferentTextNode(CharPointType::TextEnd);
  }
  return CharPointData::InDifferentTextNode(
      HTMLEditor::GetCharPointType(nextCharPoint));
}

// static
void HTMLEditor::NormalizeAllWhiteSpaceSequences(
    nsString& aResult, const CharPointData& aPreviousCharPointData,
    const CharPointData& aNextCharPointData, Linefeed aLinefeed) {
  MOZ_ASSERT(!aResult.IsEmpty());

  const auto IsCollapsibleChar = [&](char16_t aChar) {
    if (aChar == HTMLEditUtils::kNewLine) {
      return aLinefeed == Linefeed::Preformatted;
    }
    return nsCRT::IsAsciiSpace(aChar);
  };
  const auto IsCollapsibleCharOrNBSP = [&](char16_t aChar) {
    return aChar == HTMLEditUtils::kNBSP || IsCollapsibleChar(aChar);
  };

  const uint32_t length = aResult.Length();
  for (uint32_t offset = 0; offset < length; offset++) {
    const char16_t ch = aResult[offset];
    if (!IsCollapsibleCharOrNBSP(ch)) {
      continue;
    }
    const CharPointData previousCharData = [&]() {
      if (offset) {
        const char16_t prevChar = aResult[offset - 1u];
        return CharPointData::InSameTextNode(
            prevChar == HTMLEditUtils::kNewLine
                ? CharPointType::PreformattedLineBreak
                : CharPointType::VisibleChar);
      }
      return aPreviousCharPointData;
    }();
    const uint32_t endOffset = [&]() {
      for (const uint32_t i : IntegerRange(offset, length)) {
        if (IsCollapsibleCharOrNBSP(aResult[i])) {
          continue;
        }
        return i;
      }
      return length;
    }();
    const CharPointData nextCharData = [&]() {
      if (endOffset < length) {
        const char16_t nextChar = aResult[endOffset];
        return CharPointData::InSameTextNode(
            nextChar == HTMLEditUtils::kNewLine
                ? CharPointType::PreformattedLineBreak
                : CharPointType::VisibleChar);
      }
      return aNextCharPointData;
    }();
    HTMLEditor::ReplaceStringWithNormalizedWhiteSpaceSequence(
        aResult, offset, endOffset - offset, previousCharData, nextCharData);
    offset = endOffset;
  }
}

// static
void HTMLEditor::GenerateWhiteSpaceSequence(
    nsString& aResult, uint32_t aLength,
    const CharPointData& aPreviousCharPointData,
    const CharPointData& aNextCharPointData) {
  MOZ_ASSERT(aResult.IsEmpty());
  MOZ_ASSERT(aLength);

  aResult.SetLength(aLength);
  HTMLEditor::ReplaceStringWithNormalizedWhiteSpaceSequence(
      aResult, 0u, aLength, aPreviousCharPointData, aNextCharPointData);
}

// static
void HTMLEditor::ReplaceStringWithNormalizedWhiteSpaceSequence(
    nsString& aResult, uint32_t aOffset, uint32_t aLength,
    const CharPointData& aPreviousCharPointData,
    const CharPointData& aNextCharPointData) {
  MOZ_ASSERT(!aResult.IsEmpty());
  MOZ_ASSERT(aLength);
  MOZ_ASSERT(aOffset < aResult.Length());
  MOZ_ASSERT(aOffset + aLength <= aResult.Length());

  // For now, this method does not assume that result will be append to
  // white-space sequence in the text node.
  MOZ_ASSERT(aPreviousCharPointData.AcrossTextNodeBoundary() ||
             !aPreviousCharPointData.IsCollapsibleWhiteSpace());
  // For now, this method does not assume that the result will be inserted
  // into white-space sequence nor start of white-space sequence.
  MOZ_ASSERT(aNextCharPointData.AcrossTextNodeBoundary() ||
             !aNextCharPointData.IsCollapsibleWhiteSpace());

  if (aLength == 1) {
    // Even if previous/next char is in different text node, we should put
    // an ASCII white-space between visible characters.
    // XXX This means that this does not allow to put an NBSP in HTML editor
    //     without preformatted style.  However, Chrome has same issue too.
    if (aPreviousCharPointData.Type() == CharPointType::VisibleChar &&
        aNextCharPointData.Type() == CharPointType::VisibleChar) {
      aResult.SetCharAt(HTMLEditUtils::kSpace, aOffset);
      return;
    }
    // If it's start or end of text, put an NBSP.
    if (aPreviousCharPointData.Type() == CharPointType::TextEnd ||
        aNextCharPointData.Type() == CharPointType::TextEnd) {
      aResult.SetCharAt(HTMLEditUtils::kNBSP, aOffset);
      return;
    }
    // If the character is next to a preformatted linefeed, we need to put
    // an NBSP for avoiding collapsed into the linefeed.
    if (aPreviousCharPointData.Type() == CharPointType::PreformattedLineBreak ||
        aNextCharPointData.Type() == CharPointType::PreformattedLineBreak) {
      aResult.SetCharAt(HTMLEditUtils::kNBSP, aOffset);
      return;
    }
    // Now, the white-space will be inserted to a white-space sequence, but not
    // end of text.  We can put an ASCII white-space only when both sides are
    // not ASCII white-spaces.
    aResult.SetCharAt(
        aPreviousCharPointData.Type() == CharPointType::ASCIIWhiteSpace ||
                aNextCharPointData.Type() == CharPointType::ASCIIWhiteSpace
            ? HTMLEditUtils::kNBSP
            : HTMLEditUtils::kSpace,
        aOffset);
    return;
  }

  // Generate pairs of NBSP and ASCII white-space.
  bool appendNBSP = true;  // Basically, starts with an NBSP.
  char16_t* const lastChar = aResult.BeginWriting() + aOffset + aLength - 1;
  for (char16_t* iter = aResult.BeginWriting() + aOffset; iter != lastChar;
       iter++) {
    *iter = appendNBSP ? HTMLEditUtils::kNBSP : HTMLEditUtils::kSpace;
    appendNBSP = !appendNBSP;
  }

  // If the final one is expected to an NBSP, we can put an NBSP simply.
  if (appendNBSP) {
    *lastChar = HTMLEditUtils::kNBSP;
    return;
  }

  // If next char point is end of text node, an ASCII white-space or
  // preformatted linefeed, we need to put an NBSP.
  *lastChar =
      aNextCharPointData.AcrossTextNodeBoundary() ||
              aNextCharPointData.Type() == CharPointType::ASCIIWhiteSpace ||
              aNextCharPointData.Type() == CharPointType::PreformattedLineBreak
          ? HTMLEditUtils::kNBSP
          : HTMLEditUtils::kSpace;
}

HTMLEditor::NormalizedStringToInsertText
HTMLEditor::NormalizeWhiteSpacesToInsertText(
    const EditorDOMPoint& aPointToInsert, const nsAString& aStringToInsert,
    NormalizeSurroundingWhiteSpaces aNormalizeSurroundingWhiteSpaces) const {
  MOZ_ASSERT(aPointToInsert.IsSet());

  // If white-spaces are preformatted, we don't need to normalize white-spaces.
  if (EditorUtils::IsWhiteSpacePreformatted(
          *aPointToInsert.ContainerAs<nsIContent>())) {
    return NormalizedStringToInsertText(aStringToInsert, aPointToInsert);
  }

  Text* const textNode = aPointToInsert.GetContainerAs<Text>();
  const nsTextFragment* const textFragment =
      textNode ? &textNode->TextFragment() : nullptr;
  const bool isNewLineCollapsible = !EditorUtils::IsNewLinePreformatted(
      *aPointToInsert.ContainerAs<nsIContent>());

  // We don't want to make invisible things visible with this normalization.
  // Therefore, we need to know whether there are invisible leading and/or
  // trailing white-spaces in the `Text`.

  // Then, compute visible white-space length before/after the insertion point.
  // Note that these lengths may contain invisible white-spaces.
  const uint32_t precedingWhiteSpaceLength = [&]() {
    if (!textNode || !aNormalizeSurroundingWhiteSpaces ||
        aPointToInsert.IsStartOfContainer()) {
      return 0u;
    }
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
            *textNode, aPointToInsert.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    const uint32_t firstWhiteSpaceOffset =
        nonWhiteSpaceOffset ? *nonWhiteSpaceOffset + 1u : 0u;
    return aPointToInsert.Offset() - firstWhiteSpaceOffset;
  }();
  const uint32_t followingWhiteSpaceLength = [&]() {
    if (!textNode || !aNormalizeSurroundingWhiteSpaces ||
        aPointToInsert.IsEndOfContainer()) {
      return 0u;
    }
    MOZ_ASSERT(textFragment);
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            *textNode, aPointToInsert.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(textFragment->GetLength()) >=
               aPointToInsert.Offset());
    return nonWhiteSpaceOffset.valueOr(textFragment->GetLength()) -
           aPointToInsert.Offset();
  }();

  // Now, we can know invisible white-space length in precedingWhiteSpaceLength
  // and followingWhiteSpaceLength.
  const uint32_t precedingInvisibleWhiteSpaceCount =
      textNode
          ? HTMLEditUtils::GetInvisibleWhiteSpaceCount(
                *textNode, aPointToInsert.Offset() - precedingWhiteSpaceLength,
                precedingWhiteSpaceLength)
          : 0u;
  MOZ_ASSERT(precedingWhiteSpaceLength >= precedingInvisibleWhiteSpaceCount);
  const uint32_t newPrecedingWhiteSpaceLength =
      precedingWhiteSpaceLength - precedingInvisibleWhiteSpaceCount;
  const uint32_t followingInvisibleSpaceCount =
      textNode
          ? HTMLEditUtils::GetInvisibleWhiteSpaceCount(
                *textNode, aPointToInsert.Offset(), followingWhiteSpaceLength)
          : 0u;
  MOZ_ASSERT(followingWhiteSpaceLength >= followingInvisibleSpaceCount);
  const uint32_t newFollowingWhiteSpaceLength =
      followingWhiteSpaceLength - followingInvisibleSpaceCount;

  const nsAutoString stringToInsertWithSurroundingSpaces =
      [&]() -> nsAutoString {
    if (!newPrecedingWhiteSpaceLength && !newFollowingWhiteSpaceLength) {
      return nsAutoString(aStringToInsert);
    }
    nsAutoString str;
    str.SetCapacity(aStringToInsert.Length() + newPrecedingWhiteSpaceLength +
                    newFollowingWhiteSpaceLength);
    for ([[maybe_unused]] auto unused :
         IntegerRange(newPrecedingWhiteSpaceLength)) {
      str.Append(' ');
    }
    str.Append(aStringToInsert);
    for ([[maybe_unused]] auto unused :
         IntegerRange(newFollowingWhiteSpaceLength)) {
      str.Append(' ');
    }
    return str;
  }();

  const uint32_t insertionOffsetInTextNode =
      aPointToInsert.IsInTextNode() ? aPointToInsert.Offset() : 0u;
  NormalizedStringToInsertText result(
      stringToInsertWithSurroundingSpaces, insertionOffsetInTextNode,
      insertionOffsetInTextNode - precedingWhiteSpaceLength,  // replace start
      precedingWhiteSpaceLength + followingWhiteSpaceLength,  // replace length
      newPrecedingWhiteSpaceLength, newFollowingWhiteSpaceLength);

  // Now, normalize the inserting string.
  // Note that if the caller does not want to normalize the following
  // white-spaces, we always need to guarantee that neither the first character
  // nor the last character of the insertion string is not collapsible, i.e., if
  // each one is a collapsible white-space, we need to replace them an NBSP to
  // keep the visibility of the collapsible white-spaces.  Therefore, if
  // aNormalizeSurroundingWhiteSpaces is "No", we need to treat the insertion
  // string is the only characters in the `Text`.
  HTMLEditor::NormalizeAllWhiteSpaceSequences(
      result.mNormalizedString,
      CharPointData::InSameTextNode(
          !textFragment || !result.mReplaceStartOffset ||
                  !aNormalizeSurroundingWhiteSpaces
              ? CharPointType::TextEnd
              : (textFragment->CharAt(result.mReplaceStartOffset - 1u) ==
                         HTMLEditUtils::kNewLine
                     ? CharPointType::PreformattedLineBreak
                     : CharPointType::VisibleChar)),
      CharPointData::InSameTextNode(
          !textFragment ||
                  result.mReplaceEndOffset >= textFragment->GetLength() ||
                  !aNormalizeSurroundingWhiteSpaces
              ? CharPointType::TextEnd
              : (textFragment->CharAt(result.mReplaceEndOffset) ==
                         HTMLEditUtils::kNewLine
                     ? CharPointType::PreformattedLineBreak
                     : CharPointType::VisibleChar)),
      isNewLineCollapsible ? Linefeed::Collapsible : Linefeed::Preformatted);
  return result;
}

HTMLEditor::ReplaceWhiteSpacesData HTMLEditor::GetNormalizedStringAt(
    const EditorDOMPointInText& aPoint) const {
  MOZ_ASSERT(aPoint.IsSet());

  // If white-spaces are preformatted, we don't need to normalize white-spaces.
  if (EditorUtils::IsWhiteSpacePreformatted(*aPoint.ContainerAs<Text>())) {
    return ReplaceWhiteSpacesData();
  }

  const Text& textNode = *aPoint.ContainerAs<Text>();
  const nsTextFragment& textFragment = textNode.TextFragment();

  // We don't want to make invisible things visible with this normalization.
  // Therefore, we need to know whether there are invisible leading and/or
  // trailing white-spaces in the `Text`.

  // Then, compute visible white-space length before/after the point.
  // Note that these lengths may contain invisible white-spaces.
  const uint32_t precedingWhiteSpaceLength = [&]() {
    if (aPoint.IsStartOfContainer()) {
      return 0u;
    }
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
            textNode, aPoint.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    const uint32_t firstWhiteSpaceOffset =
        nonWhiteSpaceOffset ? *nonWhiteSpaceOffset + 1u : 0u;
    return aPoint.Offset() - firstWhiteSpaceOffset;
  }();
  const uint32_t followingWhiteSpaceLength = [&]() {
    if (aPoint.IsEndOfContainer()) {
      return 0u;
    }
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            textNode, aPoint.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(textFragment.GetLength()) >=
               aPoint.Offset());
    return nonWhiteSpaceOffset.valueOr(textFragment.GetLength()) -
           aPoint.Offset();
  }();
  if (!precedingWhiteSpaceLength && !followingWhiteSpaceLength) {
    return ReplaceWhiteSpacesData();
  }

  // Now, we can know invisible white-space length in precedingWhiteSpaceLength
  // and followingWhiteSpaceLength.
  const uint32_t precedingInvisibleWhiteSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(
          textNode, aPoint.Offset() - precedingWhiteSpaceLength,
          precedingWhiteSpaceLength);
  MOZ_ASSERT(precedingWhiteSpaceLength >= precedingInvisibleWhiteSpaceCount);
  const uint32_t newPrecedingWhiteSpaceLength =
      precedingWhiteSpaceLength - precedingInvisibleWhiteSpaceCount;
  const uint32_t followingInvisibleSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(textNode, aPoint.Offset(),
                                                 followingWhiteSpaceLength);
  MOZ_ASSERT(followingWhiteSpaceLength >= followingInvisibleSpaceCount);
  const uint32_t newFollowingWhiteSpaceLength =
      followingWhiteSpaceLength - followingInvisibleSpaceCount;

  nsAutoString stringToInsertWithSurroundingSpaces;
  if (newPrecedingWhiteSpaceLength || newFollowingWhiteSpaceLength) {
    stringToInsertWithSurroundingSpaces.SetLength(newPrecedingWhiteSpaceLength +
                                                  newFollowingWhiteSpaceLength);
    for (auto index : IntegerRange(newPrecedingWhiteSpaceLength +
                                   newFollowingWhiteSpaceLength)) {
      stringToInsertWithSurroundingSpaces.SetCharAt(' ', index);
    }
  }

  ReplaceWhiteSpacesData result(
      std::move(stringToInsertWithSurroundingSpaces),
      aPoint.Offset() - precedingWhiteSpaceLength,            // replace start
      precedingWhiteSpaceLength + followingWhiteSpaceLength,  // replace length
      // aPoint.Offset() after replacing the white-spaces
      aPoint.Offset() - precedingWhiteSpaceLength +
          newPrecedingWhiteSpaceLength);
  if (!result.mNormalizedString.IsEmpty()) {
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(
            !result.mReplaceStartOffset
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceStartOffset - 1u) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= textFragment.GetLength()
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceEndOffset) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        EditorUtils::IsNewLinePreformatted(textNode) ? Linefeed::Collapsible
                                                     : Linefeed::Preformatted);
  }
  return result;
}

HTMLEditor::ReplaceWhiteSpacesData
HTMLEditor::GetFollowingNormalizedStringToSplitAt(
    const EditorDOMPointInText& aPointToSplit) const {
  MOZ_ASSERT(aPointToSplit.IsSet());

  if (EditorUtils::IsWhiteSpacePreformatted(
          *aPointToSplit.ContainerAs<Text>()) ||
      aPointToSplit.IsEndOfContainer()) {
    return ReplaceWhiteSpacesData();
  }
  const bool isNewLineCollapsible =
      !EditorUtils::IsNewLinePreformatted(*aPointToSplit.ContainerAs<Text>());
  const auto IsPreformattedLineBreak = [&](char16_t aChar) {
    return !isNewLineCollapsible && aChar == HTMLEditUtils::kNewLine;
  };
  const auto IsCollapsibleChar = [&](char16_t aChar) {
    return !IsPreformattedLineBreak(aChar) && nsCRT::IsAsciiSpace(aChar);
  };
  const auto IsCollapsibleCharOrNBSP = [&](char16_t aChar) {
    return aChar == HTMLEditUtils::kNBSP || IsCollapsibleChar(aChar);
  };
  const char16_t followingChar = aPointToSplit.Char();
  if (!IsCollapsibleCharOrNBSP(followingChar)) {
    return ReplaceWhiteSpacesData();
  }
  const uint32_t followingWhiteSpaceLength = [&]() {
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            *aPointToSplit.ContainerAs<Text>(), aPointToSplit.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(
                   aPointToSplit.ContainerAs<Text>()->TextDataLength()) >=
               aPointToSplit.Offset());
    return nonWhiteSpaceOffset.valueOr(
               aPointToSplit.ContainerAs<Text>()->TextDataLength()) -
           aPointToSplit.Offset();
  }();
  MOZ_ASSERT(followingWhiteSpaceLength);
  if (NS_WARN_IF(!followingWhiteSpaceLength) ||
      (followingWhiteSpaceLength == 1u &&
       followingChar == HTMLEditUtils::kNBSP)) {
    return ReplaceWhiteSpacesData();
  }

  const uint32_t followingInvisibleSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(
          *aPointToSplit.ContainerAs<Text>(), aPointToSplit.Offset(),
          followingWhiteSpaceLength);
  MOZ_ASSERT(followingWhiteSpaceLength >= followingInvisibleSpaceCount);
  const uint32_t newFollowingWhiteSpaceLength =
      followingWhiteSpaceLength - followingInvisibleSpaceCount;
  nsAutoString followingWhiteSpaces;
  if (newFollowingWhiteSpaceLength) {
    followingWhiteSpaces.SetLength(newFollowingWhiteSpaceLength);
    for (const auto offset : IntegerRange(newFollowingWhiteSpaceLength)) {
      followingWhiteSpaces.SetCharAt(' ', offset);
    }
  }
  ReplaceWhiteSpacesData result(std::move(followingWhiteSpaces),
                                aPointToSplit.Offset(),
                                followingWhiteSpaceLength);
  if (!result.mNormalizedString.IsEmpty()) {
    const nsTextFragment& textFragment =
        aPointToSplit.ContainerAs<Text>()->TextFragment();
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(CharPointType::TextEnd),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= textFragment.GetLength()
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceEndOffset) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        isNewLineCollapsible ? Linefeed::Collapsible : Linefeed::Preformatted);
  }
  return result;
}

HTMLEditor::ReplaceWhiteSpacesData
HTMLEditor::GetPrecedingNormalizedStringToSplitAt(
    const EditorDOMPointInText& aPointToSplit) const {
  MOZ_ASSERT(aPointToSplit.IsSet());

  if (EditorUtils::IsWhiteSpacePreformatted(
          *aPointToSplit.ContainerAs<Text>()) ||
      aPointToSplit.IsStartOfContainer()) {
    return ReplaceWhiteSpacesData();
  }
  const bool isNewLineCollapsible =
      !EditorUtils::IsNewLinePreformatted(*aPointToSplit.ContainerAs<Text>());
  const auto IsPreformattedLineBreak = [&](char16_t aChar) {
    return !isNewLineCollapsible && aChar == HTMLEditUtils::kNewLine;
  };
  const auto IsCollapsibleChar = [&](char16_t aChar) {
    return !IsPreformattedLineBreak(aChar) && nsCRT::IsAsciiSpace(aChar);
  };
  const auto IsCollapsibleCharOrNBSP = [&](char16_t aChar) {
    return aChar == HTMLEditUtils::kNBSP || IsCollapsibleChar(aChar);
  };
  const char16_t precedingChar = aPointToSplit.PreviousChar();
  if (!IsCollapsibleCharOrNBSP(precedingChar)) {
    return ReplaceWhiteSpacesData();
  }
  const uint32_t precedingWhiteSpaceLength = [&]() {
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
            *aPointToSplit.ContainerAs<Text>(), aPointToSplit.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    const uint32_t firstWhiteSpaceOffset =
        nonWhiteSpaceOffset ? *nonWhiteSpaceOffset + 1u : 0u;
    return aPointToSplit.Offset() - firstWhiteSpaceOffset;
  }();
  MOZ_ASSERT(precedingWhiteSpaceLength);
  if (NS_WARN_IF(!precedingWhiteSpaceLength) ||
      (precedingWhiteSpaceLength == 1u &&
       precedingChar == HTMLEditUtils::kNBSP)) {
    return ReplaceWhiteSpacesData();
  }

  const uint32_t precedingInvisibleWhiteSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(
          *aPointToSplit.ContainerAs<Text>(),
          aPointToSplit.Offset() - precedingWhiteSpaceLength,
          precedingWhiteSpaceLength);
  MOZ_ASSERT(precedingWhiteSpaceLength >= precedingInvisibleWhiteSpaceCount);
  const uint32_t newPrecedingWhiteSpaceLength =
      precedingWhiteSpaceLength - precedingInvisibleWhiteSpaceCount;
  nsAutoString precedingWhiteSpaces;
  if (newPrecedingWhiteSpaceLength) {
    precedingWhiteSpaces.SetLength(newPrecedingWhiteSpaceLength);
    for (const auto offset : IntegerRange(newPrecedingWhiteSpaceLength)) {
      precedingWhiteSpaces.SetCharAt(' ', offset);
    }
  }
  ReplaceWhiteSpacesData result(
      std::move(precedingWhiteSpaces),
      aPointToSplit.Offset() - precedingWhiteSpaceLength,
      precedingWhiteSpaceLength);
  if (!result.mNormalizedString.IsEmpty()) {
    const nsTextFragment& textFragment =
        aPointToSplit.ContainerAs<Text>()->TextFragment();
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(
            !result.mReplaceStartOffset
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceStartOffset - 1u) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        CharPointData::InSameTextNode(CharPointType::TextEnd),
        isNewLineCollapsible ? Linefeed::Collapsible : Linefeed::Preformatted);
  }
  return result;
}

HTMLEditor::ReplaceWhiteSpacesData
HTMLEditor::GetSurroundingNormalizedStringToDelete(const Text& aTextNode,
                                                   uint32_t aOffset,
                                                   uint32_t aLength) const {
  MOZ_ASSERT(StaticPrefs::editor_white_space_normalization_blink_compatible());
  MOZ_ASSERT(aOffset <= aTextNode.TextDataLength());
  MOZ_ASSERT(aOffset + aLength <= aTextNode.TextDataLength());

  if (EditorUtils::IsWhiteSpacePreformatted(aTextNode) || !aLength ||
      (!aOffset && aLength >= aTextNode.TextDataLength())) {
    return ReplaceWhiteSpacesData();
  }
  const bool isNewLineCollapsible =
      !EditorUtils::IsNewLinePreformatted(aTextNode);
  const auto IsPreformattedLineBreak = [&](char16_t aChar) {
    return !isNewLineCollapsible && aChar == HTMLEditUtils::kNewLine;
  };
  const auto IsCollapsibleChar = [&](char16_t aChar) {
    return !IsPreformattedLineBreak(aChar) && nsCRT::IsAsciiSpace(aChar);
  };
  const auto IsCollapsibleCharOrNBSP = [&](char16_t aChar) {
    return aChar == HTMLEditUtils::kNBSP || IsCollapsibleChar(aChar);
  };
  const nsTextFragment& textFragment = aTextNode.TextFragment();
  const char16_t precedingChar =
      aOffset ? textFragment.CharAt(aOffset - 1u) : static_cast<char16_t>(0);
  const char16_t followingChar = aOffset + aLength < textFragment.GetLength()
                                     ? textFragment.CharAt(aOffset + aLength)
                                     : static_cast<char16_t>(0);
  // If there is no surrounding white-spaces, we need to do nothing here.
  if (!IsCollapsibleCharOrNBSP(precedingChar) &&
      !IsCollapsibleCharOrNBSP(followingChar)) {
    return ReplaceWhiteSpacesData();
  }
  const uint32_t precedingWhiteSpaceLength = [&]() {
    if (!IsCollapsibleCharOrNBSP(precedingChar)) {
      return 0u;
    }
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
            aTextNode, aOffset,
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    const uint32_t firstWhiteSpaceOffset =
        nonWhiteSpaceOffset ? *nonWhiteSpaceOffset + 1u : 0u;
    return aOffset - firstWhiteSpaceOffset;
  }();
  const uint32_t followingWhiteSpaceLength = [&]() {
    if (!IsCollapsibleCharOrNBSP(followingChar)) {
      return 0u;
    }
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            aTextNode, aOffset + aLength,
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(textFragment.GetLength()) >=
               aOffset + aLength);
    return nonWhiteSpaceOffset.valueOr(textFragment.GetLength()) -
           (aOffset + aLength);
  }();
  if (NS_WARN_IF(!precedingWhiteSpaceLength && !followingWhiteSpaceLength)) {
    return ReplaceWhiteSpacesData();
  }
  const uint32_t precedingInvisibleWhiteSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(
          aTextNode, aOffset - precedingWhiteSpaceLength,
          precedingWhiteSpaceLength);
  MOZ_ASSERT(precedingWhiteSpaceLength >= precedingInvisibleWhiteSpaceCount);
  const uint32_t followingInvisibleSpaceCount =
      HTMLEditUtils::GetInvisibleWhiteSpaceCount(aTextNode, aOffset + aLength,
                                                 followingWhiteSpaceLength);
  MOZ_ASSERT(followingWhiteSpaceLength >= followingInvisibleSpaceCount);

  // Let's try to return early if there is only one white-space around the
  // deleting range to avoid to run the expensive path.
  if (precedingWhiteSpaceLength == 1u && !precedingInvisibleWhiteSpaceCount &&
      !followingWhiteSpaceLength) {
    // If there is only one ASCII space and it'll be followed by a
    // non-collapsible character except preformatted linebreak after deletion,
    // we don't need to normalize the preceding white-space.
    if (precedingChar == HTMLEditUtils::kSpace && followingChar &&
        !IsPreformattedLineBreak(followingChar)) {
      return ReplaceWhiteSpacesData();
    }
    // If there is only one NBSP and it'll be the last character or will be
    // followed by a collapsible white-space, we don't need to normalize the
    // preceding white-space.
    if (precedingChar == HTMLEditUtils::kNBSP &&
        (!followingChar || IsPreformattedLineBreak(followingChar))) {
      return ReplaceWhiteSpacesData();
    }
  }
  if (followingWhiteSpaceLength == 1u && !followingInvisibleSpaceCount &&
      !precedingWhiteSpaceLength) {
    // If there is only one ASCII space and it'll follow by a non-collapsible
    // character after deletion, we don't need to normalize the following
    // white-space.
    if (followingChar == HTMLEditUtils::kSpace && precedingChar &&
        !IsPreformattedLineBreak(precedingChar)) {
      return ReplaceWhiteSpacesData();
    }
    // If there is only one NBSP and it'll be the first character or will
    // follow a preformatted line break, we don't need to normalize the
    // following white-space.
    if (followingChar == HTMLEditUtils::kNBSP &&
        (!precedingChar || IsPreformattedLineBreak(precedingChar))) {
      return ReplaceWhiteSpacesData();
    }
  }

  const uint32_t newPrecedingWhiteSpaceLength =
      precedingWhiteSpaceLength - precedingInvisibleWhiteSpaceCount;
  const uint32_t newFollowingWhiteSpaceLength =
      followingWhiteSpaceLength - followingInvisibleSpaceCount;
  nsAutoString surroundingWhiteSpaces;
  if (newPrecedingWhiteSpaceLength || newFollowingWhiteSpaceLength) {
    surroundingWhiteSpaces.SetLength(newPrecedingWhiteSpaceLength +
                                     newFollowingWhiteSpaceLength);
    for (const auto offset : IntegerRange(newPrecedingWhiteSpaceLength +
                                          newFollowingWhiteSpaceLength)) {
      surroundingWhiteSpaces.SetCharAt(' ', offset);
    }
  }
  ReplaceWhiteSpacesData result(
      std::move(surroundingWhiteSpaces), aOffset - precedingWhiteSpaceLength,
      precedingWhiteSpaceLength + aLength + followingWhiteSpaceLength,
      aOffset - precedingInvisibleWhiteSpaceCount);
  if (!result.mNormalizedString.IsEmpty()) {
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(
            !result.mReplaceStartOffset
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceStartOffset - 1u) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= textFragment.GetLength()
                ? CharPointType::TextEnd
                : (textFragment.CharAt(result.mReplaceEndOffset) ==
                           HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        isNewLineCollapsible ? Linefeed::Collapsible : Linefeed::Preformatted);
  }
  return result;
}

void HTMLEditor::ExtendRangeToDeleteWithNormalizingWhiteSpaces(
    EditorDOMPointInText& aStartToDelete, EditorDOMPointInText& aEndToDelete,
    nsString& aNormalizedWhiteSpacesInStartNode,
    nsString& aNormalizedWhiteSpacesInEndNode) const {
  MOZ_ASSERT(aStartToDelete.IsSetAndValid());
  MOZ_ASSERT(aEndToDelete.IsSetAndValid());
  MOZ_ASSERT(aStartToDelete.EqualsOrIsBefore(aEndToDelete));
  MOZ_ASSERT(aNormalizedWhiteSpacesInStartNode.IsEmpty());
  MOZ_ASSERT(aNormalizedWhiteSpacesInEndNode.IsEmpty());

  // First, check whether there is surrounding white-spaces or not, and if there
  // are, check whether they are collapsible or not.  Note that we shouldn't
  // touch white-spaces in different text nodes for performance, but we need
  // adjacent text node's first or last character information in some cases.
  const auto precedingCharPoint =
      WSRunScanner::GetPreviousCharPoint<EditorDOMPointInText>(
          WSRunScanner::Scan::EditableNodes, aStartToDelete,
          BlockInlineCheck::UseComputedDisplayStyle);
  const auto followingCharPoint =
      WSRunScanner::GetInclusiveNextCharPoint<EditorDOMPointInText>(
          WSRunScanner::Scan::EditableNodes, aEndToDelete,
          BlockInlineCheck::UseComputedDisplayStyle);
  // Blink-compat: Normalize white-spaces in first node only when not removing
  //               its last character or no text nodes follow the first node.
  //               If removing last character of first node and there are
  //               following text nodes, white-spaces in following text node are
  //               normalized instead.
  const bool removingLastCharOfStartNode =
      aStartToDelete.ContainerAs<Text>() != aEndToDelete.ContainerAs<Text>() ||
      (aEndToDelete.IsEndOfContainer() && followingCharPoint.IsSet());
  const bool maybeNormalizePrecedingWhiteSpaces =
      !removingLastCharOfStartNode && precedingCharPoint.IsSet() &&
      !precedingCharPoint.IsEndOfContainer() &&
      precedingCharPoint.ContainerAs<Text>() ==
          aStartToDelete.ContainerAs<Text>() &&
      precedingCharPoint.IsCharCollapsibleASCIISpaceOrNBSP();
  const bool maybeNormalizeFollowingWhiteSpaces =
      followingCharPoint.IsSet() && !followingCharPoint.IsEndOfContainer() &&
      (followingCharPoint.ContainerAs<Text>() ==
           aEndToDelete.ContainerAs<Text>() ||
       removingLastCharOfStartNode) &&
      followingCharPoint.IsCharCollapsibleASCIISpaceOrNBSP();

  if (!maybeNormalizePrecedingWhiteSpaces &&
      !maybeNormalizeFollowingWhiteSpaces) {
    return;  // There are no white-spaces.
  }

  // Next, consider the range to normalize.
  EditorDOMPointInText startToNormalize, endToNormalize;
  if (maybeNormalizePrecedingWhiteSpaces) {
    Maybe<uint32_t> previousCharOffsetOfWhiteSpaces =
        HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
            precedingCharPoint, {WalkTextOption::TreatNBSPsCollapsible});
    startToNormalize.Set(precedingCharPoint.ContainerAs<Text>(),
                         previousCharOffsetOfWhiteSpaces.isSome()
                             ? previousCharOffsetOfWhiteSpaces.value() + 1
                             : 0);
    MOZ_ASSERT(!startToNormalize.IsEndOfContainer());
  }
  if (maybeNormalizeFollowingWhiteSpaces) {
    Maybe<uint32_t> nextCharOffsetOfWhiteSpaces =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            followingCharPoint, {WalkTextOption::TreatNBSPsCollapsible});
    if (nextCharOffsetOfWhiteSpaces.isSome()) {
      endToNormalize.Set(followingCharPoint.ContainerAs<Text>(),
                         nextCharOffsetOfWhiteSpaces.value());
    } else {
      endToNormalize.SetToEndOf(followingCharPoint.ContainerAs<Text>());
    }
    MOZ_ASSERT(!endToNormalize.IsStartOfContainer());
  }

  // Next, retrieve surrounding information of white-space sequence.
  // If we're removing first text node's last character, we need to
  // normalize white-spaces starts from another text node.  In this case,
  // we need to lie for avoiding assertion in GenerateWhiteSpaceSequence().
  CharPointData previousCharPointData =
      removingLastCharOfStartNode
          ? CharPointData::InDifferentTextNode(CharPointType::TextEnd)
          : GetPreviousCharPointDataForNormalizingWhiteSpaces(
                startToNormalize.IsSet() ? startToNormalize : aStartToDelete);
  CharPointData nextCharPointData =
      GetInclusiveNextCharPointDataForNormalizingWhiteSpaces(
          endToNormalize.IsSet() ? endToNormalize : aEndToDelete);

  // Next, compute number of white-spaces in start/end node.
  uint32_t lengthInStartNode = 0, lengthInEndNode = 0;
  if (startToNormalize.IsSet()) {
    MOZ_ASSERT(startToNormalize.ContainerAs<Text>() ==
               aStartToDelete.ContainerAs<Text>());
    lengthInStartNode = aStartToDelete.Offset() - startToNormalize.Offset();
    MOZ_ASSERT(lengthInStartNode);
  }
  if (endToNormalize.IsSet()) {
    lengthInEndNode =
        endToNormalize.ContainerAs<Text>() == aEndToDelete.ContainerAs<Text>()
            ? endToNormalize.Offset() - aEndToDelete.Offset()
            : endToNormalize.Offset();
    MOZ_ASSERT(lengthInEndNode);
    // If we normalize white-spaces in a text node, we can replace all of them
    // with one ReplaceTextTransaction.
    if (endToNormalize.ContainerAs<Text>() ==
        aStartToDelete.ContainerAs<Text>()) {
      lengthInStartNode += lengthInEndNode;
      lengthInEndNode = 0;
    }
  }

  MOZ_ASSERT(lengthInStartNode + lengthInEndNode);

  // Next, generate normalized white-spaces.
  if (!lengthInEndNode) {
    HTMLEditor::GenerateWhiteSpaceSequence(
        aNormalizedWhiteSpacesInStartNode, lengthInStartNode,
        previousCharPointData, nextCharPointData);
  } else if (!lengthInStartNode) {
    HTMLEditor::GenerateWhiteSpaceSequence(
        aNormalizedWhiteSpacesInEndNode, lengthInEndNode, previousCharPointData,
        nextCharPointData);
  } else {
    // For making `GenerateWhiteSpaceSequence()` simpler, we should create
    // whole white-space sequence first, then, copy to the out params.
    nsAutoString whiteSpaces;
    HTMLEditor::GenerateWhiteSpaceSequence(
        whiteSpaces, lengthInStartNode + lengthInEndNode, previousCharPointData,
        nextCharPointData);
    aNormalizedWhiteSpacesInStartNode =
        Substring(whiteSpaces, 0, lengthInStartNode);
    aNormalizedWhiteSpacesInEndNode = Substring(whiteSpaces, lengthInStartNode);
    MOZ_ASSERT(aNormalizedWhiteSpacesInEndNode.Length() == lengthInEndNode);
  }

  // TODO: Shrink the replacing range and string as far as possible because
  //       this may run a lot, i.e., HTMLEditor creates ReplaceTextTransaction
  //       a lot for normalizing white-spaces.  Then, each transaction shouldn't
  //       have all white-spaces every time because once it's normalized, we
  //       don't need to normalize all of the sequence again, but currently
  //       we do.

  // Finally, extend the range.
  if (startToNormalize.IsSet()) {
    aStartToDelete = startToNormalize;
  }
  if (endToNormalize.IsSet()) {
    aEndToDelete = endToNormalize;
  }
}

Result<CaretPoint, nsresult>
HTMLEditor::DeleteTextAndNormalizeSurroundingWhiteSpaces(
    const EditorDOMPointInText& aStartToDelete,
    const EditorDOMPointInText& aEndToDelete,
    TreatEmptyTextNodes aTreatEmptyTextNodes, DeleteDirection aDeleteDirection,
    const Element& aEditingHost) {
  MOZ_ASSERT(aStartToDelete.IsSetAndValid());
  MOZ_ASSERT(aEndToDelete.IsSetAndValid());
  MOZ_ASSERT(aStartToDelete.EqualsOrIsBefore(aEndToDelete));

  // Use nsString for these replacing string because we should avoid to copy
  // the buffer from auto storange to ReplaceTextTransaction.
  nsString normalizedWhiteSpacesInFirstNode, normalizedWhiteSpacesInLastNode;

  // First, check whether we need to normalize white-spaces after deleting
  // the given range.
  EditorDOMPointInText startToDelete(aStartToDelete);
  EditorDOMPointInText endToDelete(aEndToDelete);
  ExtendRangeToDeleteWithNormalizingWhiteSpaces(
      startToDelete, endToDelete, normalizedWhiteSpacesInFirstNode,
      normalizedWhiteSpacesInLastNode);

  // If extended range is still collapsed, i.e., the caller just wants to
  // normalize white-space sequence, but there is no white-spaces which need to
  // be replaced, we need to do nothing here.
  if (startToDelete == endToDelete) {
    return CaretPoint(aStartToDelete.To<EditorDOMPoint>());
  }

  // Note that the container text node of startToDelete may be removed from
  // the tree if it becomes empty.  Therefore, we need to track the point.
  EditorDOMPoint newCaretPosition;
  if (aStartToDelete.ContainerAs<Text>() == aEndToDelete.ContainerAs<Text>()) {
    newCaretPosition = aEndToDelete.To<EditorDOMPoint>();
  } else if (aDeleteDirection == DeleteDirection::Forward) {
    newCaretPosition.SetToEndOf(aStartToDelete.ContainerAs<Text>());
  } else {
    newCaretPosition.Set(aEndToDelete.ContainerAs<Text>(), 0u);
  }

  // Then, modify the text nodes in the range.
  while (true) {
    AutoTrackDOMPoint trackingNewCaretPosition(RangeUpdaterRef(),
                                               &newCaretPosition);
    // Use ReplaceTextTransaction if we need to normalize white-spaces in
    // the first text node.
    if (!normalizedWhiteSpacesInFirstNode.IsEmpty()) {
      EditorDOMPoint trackingEndToDelete(endToDelete.ContainerAs<Text>(),
                                         endToDelete.Offset());
      {
        AutoTrackDOMPoint trackEndToDelete(RangeUpdaterRef(),
                                           &trackingEndToDelete);
        uint32_t lengthToReplaceInFirstTextNode =
            startToDelete.ContainerAs<Text>() ==
                    trackingEndToDelete.ContainerAs<Text>()
                ? trackingEndToDelete.Offset() - startToDelete.Offset()
                : startToDelete.ContainerAs<Text>()->TextLength() -
                      startToDelete.Offset();
        Result<InsertTextResult, nsresult> replaceTextResult =
            ReplaceTextWithTransaction(
                MOZ_KnownLive(*startToDelete.ContainerAs<Text>()),
                startToDelete.Offset(), lengthToReplaceInFirstTextNode,
                normalizedWhiteSpacesInFirstNode);
        if (MOZ_UNLIKELY(replaceTextResult.isErr())) {
          NS_WARNING("HTMLEditor::ReplaceTextWithTransaction() failed");
          return replaceTextResult.propagateErr();
        }
        // We'll return computed caret point, newCaretPosition, below.
        replaceTextResult.unwrap().IgnoreCaretPointSuggestion();
        if (startToDelete.ContainerAs<Text>() ==
            trackingEndToDelete.ContainerAs<Text>()) {
          MOZ_ASSERT(normalizedWhiteSpacesInLastNode.IsEmpty());
          break;  // There is no more text which we need to delete.
        }
      }
      if (MayHaveMutationEventListeners(
              NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED) &&
          (NS_WARN_IF(!trackingEndToDelete.IsSetAndValid()) ||
           NS_WARN_IF(!trackingEndToDelete.IsInTextNode()))) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      MOZ_ASSERT(trackingEndToDelete.IsInTextNode());
      endToDelete.Set(trackingEndToDelete.ContainerAs<Text>(),
                      trackingEndToDelete.Offset());
      // If the remaining range was modified by mutation event listener,
      // we should stop handling the deletion.
      startToDelete =
          EditorDOMPointInText::AtEndOf(*startToDelete.ContainerAs<Text>());
      if (MayHaveMutationEventListeners(
              NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED) &&
          NS_WARN_IF(!startToDelete.IsBefore(endToDelete))) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
    // Delete ASCII whiteSpaces in the range simpley if there are some text
    // nodes which we don't need to replace their text.
    if (normalizedWhiteSpacesInLastNode.IsEmpty() ||
        startToDelete.ContainerAs<Text>() != endToDelete.ContainerAs<Text>()) {
      // If we need to replace text in the last text node, we should
      // delete text before its previous text node.
      EditorDOMPointInText endToDeleteExceptReplaceRange =
          normalizedWhiteSpacesInLastNode.IsEmpty()
              ? endToDelete
              : EditorDOMPointInText(endToDelete.ContainerAs<Text>(), 0);
      if (startToDelete != endToDeleteExceptReplaceRange) {
        Result<CaretPoint, nsresult> caretPointOrError =
            DeleteTextAndTextNodesWithTransaction(startToDelete,
                                                  endToDeleteExceptReplaceRange,
                                                  aTreatEmptyTextNodes);
        if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::DeleteTextAndTextNodesWithTransaction() failed");
          return caretPointOrError.propagateErr();
        }
        nsresult rv = caretPointOrError.unwrap().SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CaretPoint::SuggestCaretPointTo() failed, but ignored");
        if (normalizedWhiteSpacesInLastNode.IsEmpty()) {
          break;  // There is no more text which we need to delete.
        }
        if (MayHaveMutationEventListeners(
                NS_EVENT_BITS_MUTATION_CHARACTERDATAMODIFIED |
                NS_EVENT_BITS_MUTATION_NODEREMOVED |
                NS_EVENT_BITS_MUTATION_NODEREMOVEDFROMDOCUMENT |
                NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED) &&
            (NS_WARN_IF(!endToDeleteExceptReplaceRange.IsSetAndValid()) ||
             NS_WARN_IF(!endToDelete.IsSetAndValid()) ||
             NS_WARN_IF(endToDelete.IsStartOfContainer()))) {
          return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
        }
        // Then, replace the text in the last text node.
        startToDelete = endToDeleteExceptReplaceRange;
      }
    }

    // Replace ASCII whiteSpaces in the range and following character in the
    // last text node.
    MOZ_ASSERT(!normalizedWhiteSpacesInLastNode.IsEmpty());
    MOZ_ASSERT(startToDelete.ContainerAs<Text>() ==
               endToDelete.ContainerAs<Text>());
    Result<InsertTextResult, nsresult> replaceTextResult =
        ReplaceTextWithTransaction(
            MOZ_KnownLive(*startToDelete.ContainerAs<Text>()),
            startToDelete.Offset(),
            endToDelete.Offset() - startToDelete.Offset(),
            normalizedWhiteSpacesInLastNode);
    if (MOZ_UNLIKELY(replaceTextResult.isErr())) {
      NS_WARNING("HTMLEditor::ReplaceTextWithTransaction() failed");
      return replaceTextResult.propagateErr();
    }
    // We'll return computed caret point, newCaretPosition, below.
    replaceTextResult.unwrap().IgnoreCaretPointSuggestion();
    break;
  }

  if (NS_WARN_IF(!newCaretPosition.IsSetAndValid()) ||
      NS_WARN_IF(!newCaretPosition.GetContainer()->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // Look for leaf node to put caret if we remove some empty inline ancestors
  // at new caret position.
  if (!newCaretPosition.IsInTextNode()) {
    if (const Element* editableBlockElementOrInlineEditingHost =
            HTMLEditUtils::GetInclusiveAncestorElement(
                *newCaretPosition.ContainerAs<nsIContent>(),
                HTMLEditUtils::ClosestEditableBlockElementOrInlineEditingHost,
                BlockInlineCheck::UseComputedDisplayStyle)) {
      // Try to put caret next to immediately after previous editable leaf.
      nsIContent* previousContent =
          HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
              newCaretPosition, {LeafNodeType::LeafNodeOrNonEditableNode},
              BlockInlineCheck::UseComputedDisplayStyle,
              editableBlockElementOrInlineEditingHost);
      if (previousContent &&
          HTMLEditUtils::IsSimplyEditableNode(*previousContent) &&
          !HTMLEditUtils::IsBlockElement(
              *previousContent, BlockInlineCheck::UseComputedDisplayStyle)) {
        newCaretPosition =
            previousContent->IsText() ||
                    HTMLEditUtils::IsContainerNode(*previousContent)
                ? EditorDOMPoint::AtEndOf(*previousContent)
                : EditorDOMPoint::After(*previousContent);
      }
      // But if the point is very first of a block element or immediately after
      // a child block, look for next editable leaf instead.
      else if (nsIContent* nextContent =
                   HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                       newCaretPosition,
                       {LeafNodeType::LeafNodeOrNonEditableNode},
                       BlockInlineCheck::UseComputedDisplayStyle,
                       editableBlockElementOrInlineEditingHost)) {
        if (HTMLEditUtils::IsSimplyEditableNode(*nextContent) &&
            !HTMLEditUtils::IsBlockElement(
                *nextContent, BlockInlineCheck::UseComputedDisplayStyle)) {
          newCaretPosition =
              nextContent->IsText() ||
                      HTMLEditUtils::IsContainerNode(*nextContent)
                  ? EditorDOMPoint(nextContent, 0)
                  : EditorDOMPoint(nextContent);
        }
      }
    }
  }

  // For compatibility with Blink, we should move caret to end of previous
  // text node if it's direct previous sibling of the first text node in the
  // range.
  if (newCaretPosition.IsStartOfContainer() &&
      newCaretPosition.IsInTextNode() &&
      newCaretPosition.GetContainer()->GetPreviousSibling() &&
      newCaretPosition.GetContainer()->GetPreviousSibling()->IsEditable() &&
      newCaretPosition.GetContainer()->GetPreviousSibling()->IsText()) {
    newCaretPosition.SetToEndOf(
        newCaretPosition.GetContainer()->GetPreviousSibling()->AsText());
  }
  MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(
      *newCaretPosition.ContainerAs<nsIContent>()));

  {
    AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(),
                                           &newCaretPosition);
    nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(newCaretPosition);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
      return Err(rv);
    }
    if (NS_WARN_IF(!newCaretPosition.IsSet())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }

  if (GetTopLevelEditSubAction() == EditSubAction::eDeleteSelectedContent) {
    AutoTrackDOMPoint trackingNewCaretPosition(RangeUpdaterRef(),
                                               &newCaretPosition);
    Result<CreateLineBreakResult, nsresult> insertPaddingBRElementOrError =
        InsertPaddingBRElementIfNeeded(
            newCaretPosition,
            aEditingHost.IsContentEditablePlainTextOnly() ? nsIEditor::eNoStrip
                                                          : nsIEditor::eStrip,
            aEditingHost);
    if (MOZ_UNLIKELY(insertPaddingBRElementOrError.isErr())) {
      NS_WARNING("HTMLEditor::InsertPaddingBRElementIfNeeded() failed");
      return insertPaddingBRElementOrError.propagateErr();
    }
    trackingNewCaretPosition.FlushAndStopTracking();
    if (!newCaretPosition.IsInTextNode()) {
      insertPaddingBRElementOrError.unwrap().MoveCaretPointTo(
          newCaretPosition, {SuggestCaret::OnlyIfHasSuggestion});
    } else {
      insertPaddingBRElementOrError.unwrap().IgnoreCaretPointSuggestion();
    }
    if (!newCaretPosition.IsSetAndValid()) {
      NS_WARNING("Inserting <br> element caused unexpected DOM tree");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
  }
  return CaretPoint(std::move(newCaretPosition));
}

Result<JoinNodesResult, nsresult>
HTMLEditor::JoinTextNodesWithNormalizeWhiteSpaces(Text& aLeftText,
                                                  Text& aRightText) {
  MOZ_ASSERT(StaticPrefs::editor_white_space_normalization_blink_compatible());

  if (EditorUtils::IsWhiteSpacePreformatted(aLeftText)) {
    Result<JoinNodesResult, nsresult> joinResultOrError =
        JoinNodesWithTransaction(aLeftText, aRightText);
    NS_WARNING_ASSERTION(joinResultOrError.isOk(),
                         "HTMLEditor::JoinNodesWithTransaction() failed");
    return joinResultOrError;
  }
  const bool isNewLinePreformatted =
      EditorUtils::IsNewLinePreformatted(aLeftText);
  const auto IsCollapsibleChar = [&](char16_t aChar) {
    return (aChar == HTMLEditUtils::kNewLine && !isNewLinePreformatted) ||
           nsCRT::IsAsciiSpace(aChar);
  };
  const auto IsCollapsibleCharOrNBSP = [&](char16_t aChar) {
    return aChar == HTMLEditUtils::kNBSP || IsCollapsibleChar(aChar);
  };
  const char16_t lastLeftChar = aLeftText.TextFragment().SafeLastChar();
  char16_t firstRightChar = aRightText.TextFragment().SafeFirstChar();
  const char16_t secondRightChar = aRightText.TextFragment().GetLength() >= 2
                                       ? aRightText.TextFragment().CharAt(1u)
                                       : static_cast<char16_t>(0);
  if (IsCollapsibleCharOrNBSP(firstRightChar)) {
    // If the right Text starts only with a collapsible white-space and it'll
    // follow a non-collapsible char, we should make it an ASCII white-space.
    if (secondRightChar && !IsCollapsibleCharOrNBSP(secondRightChar) &&
        lastLeftChar && !IsCollapsibleChar(lastLeftChar)) {
      if (firstRightChar != HTMLEditUtils::kSpace) {
        Result<InsertTextResult, nsresult> replaceWhiteSpaceResultOrError =
            ReplaceTextWithTransaction(aRightText, 0u, 1u, u" "_ns);
        if (MOZ_UNLIKELY(replaceWhiteSpaceResultOrError.isErr())) {
          NS_WARNING("HTMLEditor::ReplaceTextWithTransaction() failed");
          return replaceWhiteSpaceResultOrError.propagateErr();
        }
        replaceWhiteSpaceResultOrError.unwrap().IgnoreCaretPointSuggestion();
        if (NS_WARN_IF(aLeftText.GetNextSibling() != &aRightText)) {
          return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
        }
        firstRightChar = HTMLEditUtils::kSpace;
      }
    }
    // Otherwise, normalize the white-spaces before join, i.e., it will start
    // with an NBSP.
    else {
      Result<EditorDOMPoint, nsresult> atFirstVisibleThingOrError =
          WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesAfter(
              *this, EditorDOMPoint(&aRightText, 0u), {});
      if (MOZ_UNLIKELY(atFirstVisibleThingOrError.isErr())) {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesAfter() failed");
        return atFirstVisibleThingOrError.propagateErr();
      }
      if (!aRightText.GetParentNode()) {
        return JoinNodesResult(EditorDOMPoint::AtEndOf(aLeftText), aRightText);
      }
    }
  } else if (IsCollapsibleCharOrNBSP(lastLeftChar) &&
             lastLeftChar != HTMLEditUtils::kSpace &&
             aLeftText.TextFragment().GetLength() >= 2u) {
    // If the last char of the left `Text` is a single white-space but not an
    // ASCII space, let's replace it with an ASCII space.
    const char16_t secondLastChar = aLeftText.TextFragment().CharAt(
        aLeftText.TextFragment().GetLength() - 2u);
    if (!IsCollapsibleCharOrNBSP(secondLastChar) &&
        !IsCollapsibleCharOrNBSP(firstRightChar)) {
      Result<InsertTextResult, nsresult> replaceWhiteSpaceResultOrError =
          ReplaceTextWithTransaction(aLeftText,
                                     aLeftText.TextFragment().GetLength() - 1u,
                                     1u, u" "_ns);
      if (MOZ_UNLIKELY(replaceWhiteSpaceResultOrError.isErr())) {
        NS_WARNING("HTMLEditor::ReplaceTextWithTransaction() failed");
        return replaceWhiteSpaceResultOrError.propagateErr();
      }
      replaceWhiteSpaceResultOrError.unwrap().IgnoreCaretPointSuggestion();
      if (NS_WARN_IF(aLeftText.GetNextSibling() != &aRightText)) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
  }
  Result<JoinNodesResult, nsresult> joinResultOrError =
      JoinNodesWithTransaction(aLeftText, aRightText);
  if (MOZ_UNLIKELY(joinResultOrError.isErr())) {
    NS_WARNING("HTMLEditor::JoinNodesWithTransaction() failed");
    return joinResultOrError;
  }
  JoinNodesResult joinResult = joinResultOrError.unwrap();
  const EditorDOMPointInText startOfRightTextData =
      joinResult.AtJoinedPoint<EditorRawDOMPoint>().GetAsInText();
  if (NS_WARN_IF(!startOfRightTextData.IsSet()) ||
      (firstRightChar &&
       (NS_WARN_IF(startOfRightTextData.IsEndOfContainer()) ||
        NS_WARN_IF(firstRightChar != startOfRightTextData.Char())))) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  return std::move(joinResult);
}

// static
bool HTMLEditor::CanInsertLineBreak(LineBreakType aLineBreakType,
                                    const nsIContent& aContent) {
  if (MOZ_UNLIKELY(!HTMLEditUtils::IsSimplyEditableNode(aContent))) {
    return false;
  }
  if (aLineBreakType == LineBreakType::BRElement) {
    return HTMLEditUtils::CanNodeContain(aContent, *nsGkAtoms::br);
  }
  MOZ_ASSERT(aLineBreakType == LineBreakType::Linefeed);
  const Element* const container = aContent.GetAsElementOrParentElement();
  return container &&
         HTMLEditUtils::CanNodeContain(*container, *nsGkAtoms::textTagName) &&
         EditorUtils::IsNewLinePreformatted(*container);
}

Result<CreateLineBreakResult, nsresult>
HTMLEditor::InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded(
    const EditorDOMPoint& aPointToInsert) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSet());

  if (MOZ_UNLIKELY(!aPointToInsert.IsInContentNode())) {
    return CreateLineBreakResult::NotHandled();
  }

  // If we cannot insert a line break here, do nothing.
  if (!HTMLEditor::CanInsertLineBreak(
          LineBreakType::BRElement,
          *aPointToInsert.ContainerAs<nsIContent>())) {
    return CreateLineBreakResult::NotHandled();
  }

  // FYI: We don't need to put <br> if it reaches an inline editing host because
  // editing host has at least one line height by default even if it's empty and
  // it's tested by WPT to no <br> element is inserted in the cases.

  // If the point is not start of a line, we don't need to put a line break
  // here.
  const WSScanResult previousThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          WSRunScanner::Scan::EditableNodes, aPointToInsert,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (!previousThing.ReachedLineBoundary()) {
    return CreateLineBreakResult::NotHandled();
  }

  // If the point is not followed by a block boundary, we don't need to put a
  // line break here.
  const WSScanResult nextThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          WSRunScanner::Scan::EditableNodes, aPointToInsert,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (!nextThing.ReachedBlockBoundary()) {
    return CreateLineBreakResult::NotHandled();
  }

  Result<CreateLineBreakResult, nsresult> insertLineBreakResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      aPointToInsert, nsIEditor::ePrevious);
  NS_WARNING_ASSERTION(insertLineBreakResultOrError.isOk(),
                       "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                       "LineBreakType::BRElement, ePrevious) failed");
  return insertLineBreakResultOrError;
}

Result<EditActionResult, nsresult>
HTMLEditor::MakeOrChangeListAndListItemAsSubAction(
    const nsStaticAtom& aListElementOrListItemElementTagName,
    const nsAString& aBulletType,
    SelectAllOfCurrentList aSelectAllOfCurrentList,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(&aListElementOrListItemElementTagName == nsGkAtoms::ul ||
             &aListElementOrListItemElementTagName == nsGkAtoms::ol ||
             &aListElementOrListItemElementTagName == nsGkAtoms::dl ||
             &aListElementOrListItemElementTagName == nsGkAtoms::dd ||
             &aListElementOrListItemElementTagName == nsGkAtoms::dt);

  if (NS_WARN_IF(!mInitSucceeded)) {
    return Err(NS_ERROR_NOT_INITIALIZED);
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Some selection containers are not content node, but ignored");
    return EditActionResult::IgnoredResult();
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  // XXX EditSubAction::eCreateOrChangeDefinitionListItem and
  //     EditSubAction::eCreateOrChangeList are treated differently in
  //     HTMLEditor::MaybeSplitElementsAtEveryBRElement().  Only when
  //     EditSubAction::eCreateOrChangeList, it splits inline nodes.
  //     Currently, it shouldn't be done when we called for formatting
  //     `<dd>` or `<dt>` by
  //     HTMLEditor::MakeDefinitionListItemWithTransaction().  But this
  //     difference may be a bug.  We should investigate this later.
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this,
      &aListElementOrListItemElementTagName == nsGkAtoms::dd ||
              &aListElementOrListItemElementTagName == nsGkAtoms::dt
          ? EditSubAction::eCreateOrChangeDefinitionListItem
          : EditSubAction::eCreateOrChangeList,
      nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(error.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  const nsStaticAtom* listTagName = nullptr;
  const nsStaticAtom* listItemTagName = nullptr;
  if (&aListElementOrListItemElementTagName == nsGkAtoms::ul ||
      &aListElementOrListItemElementTagName == nsGkAtoms::ol) {
    listTagName = &aListElementOrListItemElementTagName;
    listItemTagName = nsGkAtoms::li;
  } else if (&aListElementOrListItemElementTagName == nsGkAtoms::dl) {
    listTagName = &aListElementOrListItemElementTagName;
    listItemTagName = nsGkAtoms::dd;
  } else if (&aListElementOrListItemElementTagName == nsGkAtoms::dd ||
             &aListElementOrListItemElementTagName == nsGkAtoms::dt) {
    listTagName = nsGkAtoms::dl;
    listItemTagName = &aListElementOrListItemElementTagName;
  } else {
    NS_WARNING(
        "aListElementOrListItemElementTagName was neither list element name "
        "nor "
        "definition listitem element name");
    return Err(NS_ERROR_INVALID_ARG);
  }

  // Expands selection range to include the immediate block parent, and then
  // further expands to include any ancestors whose children are all in the
  // range.
  // XXX Why do we do this only when there is only one selection range?
  if (!SelectionRef().IsCollapsed() && SelectionRef().RangeCount() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            SelectionRef().GetRangeAt(0u), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.propagateErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use Selection::SetStartAndEndInLimit() here.
    error.SuppressException();
    SelectionRef().SetBaseAndExtentInLimiter(
        extendedRange.inspect().StartRef().ToRawRangeBoundary(),
        extendedRange.inspect().EndRef().ToRawRangeBoundary(), error);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING("Selection::SetBaseAndExtentInLimiter() failed");
      return Err(error.StealNSResult());
    }
  }

  AutoListElementCreator listCreator(*listTagName, *listItemTagName,
                                     aBulletType);
  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  Result<EditActionResult, nsresult> result = listCreator.Run(
      *this, selectionRanges, aSelectAllOfCurrentList, aEditingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::ConvertContentAroundRangesToList() failed");
    // XXX Should we try to restore selection ranges in this case?
    return result;
  }

  rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoClonedSelectionRangeArray::ApplyTo() failed");
    return Err(rv);
  }
  return result.inspect().Ignored() ? EditActionResult::CanceledResult()
                                    : EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult> HTMLEditor::AutoListElementCreator::Run(
    HTMLEditor& aHTMLEditor, AutoClonedSelectionRangeArray& aRanges,
    SelectAllOfCurrentList aSelectAllOfCurrentList,
    const Element& aEditingHost) const {
  MOZ_ASSERT(aHTMLEditor.IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(!aHTMLEditor.IsSelectionRangeContainerNotContent());

  if (NS_WARN_IF(!aRanges.SaveAndTrackRanges(aHTMLEditor))) {
    return Err(NS_ERROR_FAILURE);
  }

  AutoContentNodeArray arrayOfContents;
  nsresult rv = SplitAtRangeEdgesAndCollectContentNodesToMoveIntoList(
      aHTMLEditor, aRanges, aSelectAllOfCurrentList, aEditingHost,
      arrayOfContents);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "AutoListElementCreator::"
        "SplitAtRangeEdgesAndCollectContentNodesToMoveIntoList() failed");
    return Err(rv);
  }

  // check if all our nodes are <br>s, or empty inlines
  // if no nodes, we make empty list.  Ditto if the user tried to make a list
  // of some # of breaks.
  if (AutoListElementCreator::
          IsEmptyOrContainsOnlyBRElementsOrEmptyInlineElements(
              arrayOfContents)) {
    Result<RefPtr<Element>, nsresult> newListItemElementOrError =
        ReplaceContentNodesWithEmptyNewList(aHTMLEditor, aRanges,
                                            arrayOfContents, aEditingHost);
    if (MOZ_UNLIKELY(newListItemElementOrError.isErr())) {
      NS_WARNING(
          "AutoListElementCreator::ReplaceContentNodesWithEmptyNewList() "
          "failed");
      return newListItemElementOrError.propagateErr();
    }
    if (MOZ_UNLIKELY(!newListItemElementOrError.inspect())) {
      aRanges.RestoreFromSavedRanges();
      return EditActionResult::CanceledResult();
    }
    aRanges.ClearSavedRanges();
    nsresult rv = aRanges.Collapse(
        EditorRawDOMPoint(newListItemElementOrError.inspect(), 0u));
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return Err(rv);
    }
    return EditActionResult::IgnoredResult();
  }

  Result<RefPtr<Element>, nsresult> listItemOrListToPutCaretOrError =
      WrapContentNodesIntoNewListElements(aHTMLEditor, aRanges, arrayOfContents,
                                          aEditingHost);
  if (MOZ_UNLIKELY(listItemOrListToPutCaretOrError.isErr())) {
    NS_WARNING(
        "AutoListElementCreator::WrapContentNodesIntoNewListElements() failed");
    return listItemOrListToPutCaretOrError.propagateErr();
  }

  MOZ_ASSERT(aRanges.HasSavedRanges());
  aRanges.RestoreFromSavedRanges();

  // If selection will be collapsed but not in listItemOrListToPutCaret, we need
  // to adjust the caret position into it.
  if (listItemOrListToPutCaretOrError.inspect()) {
    DebugOnly<nsresult> rvIgnored =
        EnsureCollapsedRangeIsInListItemOrListElement(
            *listItemOrListToPutCaretOrError.inspect(), aRanges);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "AutoListElementCreator::"
        "EnsureCollapsedRangeIsInListItemOrListElement() failed, but ignored");
  }

  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::AutoListElementCreator::
    SplitAtRangeEdgesAndCollectContentNodesToMoveIntoList(
        HTMLEditor& aHTMLEditor, AutoClonedRangeArray& aRanges,
        SelectAllOfCurrentList aSelectAllOfCurrentList,
        const Element& aEditingHost,
        ContentNodeArray& aOutArrayOfContents) const {
  MOZ_ASSERT(aOutArrayOfContents.IsEmpty());

  if (aSelectAllOfCurrentList == SelectAllOfCurrentList::Yes) {
    if (Element* parentListElementOfRanges =
            aRanges.GetClosestAncestorAnyListElementOfRange()) {
      aOutArrayOfContents.AppendElement(
          OwningNonNull<nsIContent>(*parentListElementOfRanges));
      return NS_OK;
    }
  }

  AutoClonedRangeArray extendedRanges(aRanges);

  // TODO: We don't need AutoTransactionsConserveSelection here in the
  //       normal cases, but removing this may cause the behavior with the
  //       legacy mutation event listeners.  We should try to delete this in
  //       a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(aHTMLEditor);

  extendedRanges.ExtendRangesToWrapLines(EditSubAction::eCreateOrChangeList,
                                         BlockInlineCheck::UseHTMLDefaultStyle,
                                         aEditingHost);
  Result<EditorDOMPoint, nsresult> splitResult =
      extendedRanges.SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
          aHTMLEditor, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
  if (MOZ_UNLIKELY(splitResult.isErr())) {
    NS_WARNING(
        "AutoClonedRangeArray::"
        "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() failed");
    return splitResult.unwrapErr();
  }
  nsresult rv = extendedRanges.CollectEditTargetNodes(
      aHTMLEditor, aOutArrayOfContents, EditSubAction::eCreateOrChangeList,
      AutoClonedRangeArray::CollectNonEditableNodes::No);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
        "eCreateOrChangeList, CollectNonEditableNodes::No) failed");
    return rv;
  }

  Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
      aHTMLEditor.MaybeSplitElementsAtEveryBRElement(
          aOutArrayOfContents, EditSubAction::eCreateOrChangeList);
  if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
        "eCreateOrChangeList) failed");
    return splitAtBRElementsResult.unwrapErr();
  }
  return NS_OK;
}

// static
bool HTMLEditor::AutoListElementCreator::
    IsEmptyOrContainsOnlyBRElementsOrEmptyInlineElements(
        const ContentNodeArray& aArrayOfContents) {
  for (const OwningNonNull<nsIContent>& content : aArrayOfContents) {
    // if content is not a <br> or empty inline, we're done
    // XXX Should we handle line breaks in preformatted text node?
    if (!content->IsHTMLElement(nsGkAtoms::br) &&
        !HTMLEditUtils::IsEmptyInlineContainer(
            content,
            {EmptyCheckOption::TreatSingleBRElementAsVisible,
             EmptyCheckOption::TreatNonEditableContentAsInvisible},
            BlockInlineCheck::UseComputedDisplayStyle)) {
      return false;
    }
  }
  return true;
}

Result<RefPtr<Element>, nsresult>
HTMLEditor::AutoListElementCreator::ReplaceContentNodesWithEmptyNewList(
    HTMLEditor& aHTMLEditor, const AutoClonedRangeArray& aRanges,
    const AutoContentNodeArray& aArrayOfContents,
    const Element& aEditingHost) const {
  // if only breaks, delete them
  for (const OwningNonNull<nsIContent>& content : aArrayOfContents) {
    // MOZ_KnownLive because of bug 1620312
    nsresult rv =
        aHTMLEditor.DeleteNodeWithTransaction(MOZ_KnownLive(*content));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  const auto firstRangeStartPoint =
      aRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!firstRangeStartPoint.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  // Make sure we can put a list here.
  if (!HTMLEditUtils::CanNodeContain(*firstRangeStartPoint.GetContainer(),
                                     mListTagName)) {
    return RefPtr<Element>();
  }

  RefPtr<Element> newListItemElement;
  Result<CreateElementResult, nsresult> createNewListElementResult =
      aHTMLEditor.InsertElementWithSplittingAncestorsWithTransaction(
          mListTagName, firstRangeStartPoint, BRElementNextToSplitPoint::Keep,
          aEditingHost,
          // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
          [&](HTMLEditor& aHTMLEditor, Element& aListElement,
              const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            AutoHandlingState dummyState;
            Result<CreateElementResult, nsresult> createListItemElementResult =
                AppendListItemElement(aHTMLEditor, aListElement, dummyState);
            if (MOZ_UNLIKELY(createListItemElementResult.isErr())) {
              NS_WARNING(
                  "AutoListElementCreator::AppendListItemElement() failed");
              return createListItemElementResult.unwrapErr();
            }
            CreateElementResult unwrappedResult =
                createListItemElementResult.unwrap();
            // There is AutoSelectionRestorer in this method so that it'll
            // be restored or updated with making it abort.  Therefore,
            // we don't need to update selection here.
            // XXX I'd like to check aRanges.HasSavedRanges() here, but it
            //     requires ifdefs to avoid bustage of opt builds caused
            //     by unused warning...
            unwrappedResult.IgnoreCaretPointSuggestion();
            newListItemElement = unwrappedResult.UnwrapNewNode();
            MOZ_ASSERT(newListItemElement);
            return NS_OK;
          });
  if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
    NS_WARNING(
        nsPrintfCString(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "%s) failed",
            nsAtomCString(&mListTagName).get())
            .get());
    return createNewListElementResult.propagateErr();
  }
  MOZ_ASSERT(createNewListElementResult.inspect().GetNewNode());

  // Put selection in new list item and don't restore the Selection.
  createNewListElementResult.inspect().IgnoreCaretPointSuggestion();
  return newListItemElement;
}

Result<RefPtr<Element>, nsresult>
HTMLEditor::AutoListElementCreator::WrapContentNodesIntoNewListElements(
    HTMLEditor& aHTMLEditor, AutoClonedRangeArray& aRanges,
    AutoContentNodeArray& aArrayOfContents, const Element& aEditingHost) const {
  // if there is only one node in the array, and it is a list, div, or
  // blockquote, then look inside of it until we find inner list or content.
  if (aArrayOfContents.Length() == 1) {
    if (Element* deepestDivBlockquoteOrListElement =
            HTMLEditUtils::GetInclusiveDeepestFirstChildWhichHasOneChild(
                aArrayOfContents[0], {WalkTreeOption::IgnoreNonEditableNode},
                BlockInlineCheck::UseHTMLDefaultStyle, nsGkAtoms::div,
                nsGkAtoms::blockquote, nsGkAtoms::ul, nsGkAtoms::ol,
                nsGkAtoms::dl)) {
      if (deepestDivBlockquoteOrListElement->IsAnyOfHTMLElements(
              nsGkAtoms::div, nsGkAtoms::blockquote)) {
        aArrayOfContents.Clear();
        HTMLEditUtils::CollectChildren(*deepestDivBlockquoteOrListElement,
                                       aArrayOfContents, 0, {});
      } else {
        aArrayOfContents.ReplaceElementAt(
            0, OwningNonNull<nsIContent>(*deepestDivBlockquoteOrListElement));
      }
    }
  }

  // Ok, now go through all the nodes and put then in the list,
  // or whatever is appropriate.  Wohoo!
  AutoHandlingState handlingState;
  for (const OwningNonNull<nsIContent>& content : aArrayOfContents) {
    // MOZ_KnownLive because of bug 1620312
    nsresult rv = HandleChildContent(aHTMLEditor, MOZ_KnownLive(content),
                                     handlingState, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoListElementCreator::HandleChildContent() failed");
      return Err(rv);
    }
  }

  return std::move(handlingState.mListOrListItemElementToPutCaret);
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildContent(
    HTMLEditor& aHTMLEditor, nsIContent& aHandlingContent,
    AutoHandlingState& aState, const Element& aEditingHost) const {
  // make sure we don't assemble content that is in different table cells
  // into the same list.  respect table cell boundaries when listifying.
  if (aState.mCurrentListElement &&
      HTMLEditUtils::GetInclusiveAncestorAnyTableElement(
          *aState.mCurrentListElement) !=
          HTMLEditUtils::GetInclusiveAncestorAnyTableElement(
              aHandlingContent)) {
    aState.mCurrentListElement = nullptr;
  }

  // If current node is a `<br>` element, delete it and forget previous
  // list item element.
  // If current node is an empty inline node, just delete it.
  if (EditorUtils::IsEditableContent(aHandlingContent, EditorType::HTML) &&
      (aHandlingContent.IsHTMLElement(nsGkAtoms::br) ||
       HTMLEditUtils::IsEmptyInlineContainer(
           aHandlingContent,
           {EmptyCheckOption::TreatSingleBRElementAsVisible,
            EmptyCheckOption::TreatNonEditableContentAsInvisible},
           BlockInlineCheck::UseHTMLDefaultStyle))) {
    nsresult rv = aHTMLEditor.DeleteNodeWithTransaction(aHandlingContent);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
    if (aHandlingContent.IsHTMLElement(nsGkAtoms::br)) {
      aState.mPreviousListItemElement = nullptr;
    }
    return NS_OK;
  }

  // If we meet a list, we can reuse it or convert it to the expected type list.
  if (HTMLEditUtils::IsAnyListElement(&aHandlingContent)) {
    nsresult rv = HandleChildListElement(
        aHTMLEditor, MOZ_KnownLive(*aHandlingContent.AsElement()), aState);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoListElementCreator::HandleChildListElement() failed");
    return rv;
  }

  // We cannot handle nodes if not in element node.
  if (NS_WARN_IF(!aHandlingContent.GetParentElement())) {
    return NS_ERROR_FAILURE;
  }

  // If we meet a list item, we can just move it to current list element or new
  // list element.
  if (HTMLEditUtils::IsListItem(&aHandlingContent)) {
    nsresult rv = HandleChildListItemElement(
        aHTMLEditor, MOZ_KnownLive(*aHandlingContent.AsElement()), aState);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoListElementCreator::HandleChildListItemElement() failed");
    return rv;
  }

  // If we meet a <div> or a <p>, we want only its children to wrapping into
  // list element.  Therefore, this call will call this recursively.
  if (aHandlingContent.IsAnyOfHTMLElements(nsGkAtoms::div, nsGkAtoms::p)) {
    nsresult rv = HandleChildDivOrParagraphElement(
        aHTMLEditor, MOZ_KnownLive(*aHandlingContent.AsElement()), aState,
        aEditingHost);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoListElementCreator::HandleChildDivOrParagraphElement() failed");
    return rv;
  }

  // If we've not met a list element, create a list element and make it
  // current list element.
  if (!aState.mCurrentListElement) {
    nsresult rv = CreateAndUpdateCurrentListElement(
        aHTMLEditor, EditorDOMPoint(&aHandlingContent),
        EmptyListItem::NotCreate, aState, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoListElementCreator::HandleChildInlineElement() failed");
      return rv;
    }
  }

  // If we meet an inline content, we want to move it to previously used list
  // item element or new list item element.
  if (HTMLEditUtils::IsInlineContent(aHandlingContent,
                                     BlockInlineCheck::UseHTMLDefaultStyle)) {
    nsresult rv =
        HandleChildInlineContent(aHTMLEditor, aHandlingContent, aState);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoListElementCreator::HandleChildInlineElement() failed");
    return rv;
  }

  // Otherwise, we should wrap it into new list item element.
  nsresult rv =
      WrapContentIntoNewListItemElement(aHTMLEditor, aHandlingContent, aState);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "AutoListElementCreator::WrapContentIntoNewListItemElement() failed");
  return rv;
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildListElement(
    HTMLEditor& aHTMLEditor, Element& aHandlingListElement,
    AutoHandlingState& aState) const {
  MOZ_ASSERT(HTMLEditUtils::IsAnyListElement(&aHandlingListElement));

  // If we met a list element and current list element is not a descendant
  // of the list, append current node to end of the current list element.
  // Then, wrap it with list item element and delete the old container.
  if (aState.mCurrentListElement &&
      !EditorUtils::IsDescendantOf(aHandlingListElement,
                                   *aState.mCurrentListElement)) {
    Result<MoveNodeResult, nsresult> moveNodeResult =
        aHTMLEditor.MoveNodeToEndWithTransaction(
            aHandlingListElement, MOZ_KnownLive(*aState.mCurrentListElement));
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.propagateErr();
    }
    moveNodeResult.inspect().IgnoreCaretPointSuggestion();

    Result<CreateElementResult, nsresult> convertListTypeResult =
        aHTMLEditor.ChangeListElementType(aHandlingListElement, mListTagName,
                                          mListItemTagName);
    if (MOZ_UNLIKELY(convertListTypeResult.isErr())) {
      NS_WARNING("HTMLEditor::ChangeListElementType() failed");
      return convertListTypeResult.propagateErr();
    }
    convertListTypeResult.inspect().IgnoreCaretPointSuggestion();

    Result<EditorDOMPoint, nsresult> unwrapNewListElementResult =
        aHTMLEditor.RemoveBlockContainerWithTransaction(
            MOZ_KnownLive(*convertListTypeResult.inspect().GetNewNode()));
    if (MOZ_UNLIKELY(unwrapNewListElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
      return unwrapNewListElementResult.propagateErr();
    }
    aState.mPreviousListItemElement = nullptr;
    return NS_OK;
  }

  // If current list element is in found list element or we've not met a
  // list element, convert current list element to proper type.
  Result<CreateElementResult, nsresult> convertListTypeResult =
      aHTMLEditor.ChangeListElementType(aHandlingListElement, mListTagName,
                                        mListItemTagName);
  if (MOZ_UNLIKELY(convertListTypeResult.isErr())) {
    NS_WARNING("HTMLEditor::ChangeListElementType() failed");
    return convertListTypeResult.propagateErr();
  }
  CreateElementResult unwrappedConvertListTypeResult =
      convertListTypeResult.unwrap();
  unwrappedConvertListTypeResult.IgnoreCaretPointSuggestion();
  MOZ_ASSERT(unwrappedConvertListTypeResult.GetNewNode());
  aState.mCurrentListElement = unwrappedConvertListTypeResult.UnwrapNewNode();
  aState.mPreviousListItemElement = nullptr;
  return NS_OK;
}

nsresult
HTMLEditor::AutoListElementCreator::HandleChildListItemInDifferentTypeList(
    HTMLEditor& aHTMLEditor, Element& aHandlingListItemElement,
    AutoHandlingState& aState) const {
  MOZ_ASSERT(HTMLEditUtils::IsListItem(&aHandlingListItemElement));
  MOZ_ASSERT(
      !aHandlingListItemElement.GetParent()->IsHTMLElement(&mListTagName));

  // If we've not met a list element or current node is not in current list
  // element, insert a list element at current node and set current list element
  // to the new one.
  if (!aState.mCurrentListElement ||
      aHandlingListItemElement.IsInclusiveDescendantOf(
          aState.mCurrentListElement)) {
    EditorDOMPoint atListItem(&aHandlingListItemElement);
    MOZ_ASSERT(atListItem.IsInContentNode());

    Result<SplitNodeResult, nsresult> splitListItemParentResult =
        aHTMLEditor.SplitNodeWithTransaction(atListItem);
    if (MOZ_UNLIKELY(splitListItemParentResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
      return splitListItemParentResult.propagateErr();
    }
    SplitNodeResult unwrappedSplitListItemParentResult =
        splitListItemParentResult.unwrap();
    MOZ_ASSERT(unwrappedSplitListItemParentResult.DidSplit());
    unwrappedSplitListItemParentResult.IgnoreCaretPointSuggestion();

    Result<CreateElementResult, nsresult> createNewListElementResult =
        aHTMLEditor.CreateAndInsertElement(
            WithTransaction::Yes, mListTagName,
            unwrappedSplitListItemParentResult.AtNextContent<EditorDOMPoint>());
    if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) "
          "failed");
      return createNewListElementResult.propagateErr();
    }
    CreateElementResult unwrapCreateNewListElementResult =
        createNewListElementResult.unwrap();
    unwrapCreateNewListElementResult.IgnoreCaretPointSuggestion();
    MOZ_ASSERT(unwrapCreateNewListElementResult.GetNewNode());
    aState.mCurrentListElement =
        unwrapCreateNewListElementResult.UnwrapNewNode();
  }

  // Then, move current node into current list element.
  Result<MoveNodeResult, nsresult> moveNodeResult =
      aHTMLEditor.MoveNodeToEndWithTransaction(
          aHandlingListItemElement, MOZ_KnownLive(*aState.mCurrentListElement));
  if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
    NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
    return moveNodeResult.propagateErr();
  }
  moveNodeResult.inspect().IgnoreCaretPointSuggestion();

  // Convert list item type if current node is different list item type.
  if (aHandlingListItemElement.IsHTMLElement(&mListItemTagName)) {
    return NS_OK;
  }
  Result<CreateElementResult, nsresult> newListItemElementOrError =
      aHTMLEditor.ReplaceContainerAndCloneAttributesWithTransaction(
          aHandlingListItemElement, mListItemTagName);
  if (MOZ_UNLIKELY(newListItemElementOrError.isErr())) {
    NS_WARNING("HTMLEditor::ReplaceContainerWithTransaction() failed");
    return newListItemElementOrError.propagateErr();
  }
  newListItemElementOrError.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildListItemElement(
    HTMLEditor& aHTMLEditor, Element& aHandlingListItemElement,
    AutoHandlingState& aState) const {
  MOZ_ASSERT(aHandlingListItemElement.GetParentNode());
  MOZ_ASSERT(HTMLEditUtils::IsListItem(&aHandlingListItemElement));

  // If current list item element is not in proper list element, we need
  // to convert the list element.
  // XXX This check is not enough,
  if (!aHandlingListItemElement.GetParentNode()->IsHTMLElement(&mListTagName)) {
    nsresult rv = HandleChildListItemInDifferentTypeList(
        aHTMLEditor, aHandlingListItemElement, aState);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoListElementCreator::HandleChildListItemInDifferentTypeList() "
          "failed");
      return rv;
    }
  } else {
    nsresult rv = HandleChildListItemInSameTypeList(
        aHTMLEditor, aHandlingListItemElement, aState);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoListElementCreator::HandleChildListItemInSameTypeList() failed");
      return rv;
    }
  }

  // If bullet type is specified, set list type attribute.
  // XXX Cannot we set type attribute before inserting the list item
  //     element into the DOM tree?
  if (!mBulletType.IsEmpty()) {
    nsresult rv = aHTMLEditor.SetAttributeWithTransaction(
        aHandlingListItemElement, *nsGkAtoms::type, mBulletType);
    if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::SetAttributeWithTransaction(nsGkAtoms::type) failed");
    return rv;
  }

  // Otherwise, remove list type attribute if there is.
  if (!aHandlingListItemElement.HasAttr(nsGkAtoms::type)) {
    return NS_OK;
  }
  nsresult rv = aHTMLEditor.RemoveAttributeWithTransaction(
      aHandlingListItemElement, *nsGkAtoms::type);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::type) failed");
  return rv;
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildListItemInSameTypeList(
    HTMLEditor& aHTMLEditor, Element& aHandlingListItemElement,
    AutoHandlingState& aState) const {
  MOZ_ASSERT(HTMLEditUtils::IsListItem(&aHandlingListItemElement));
  MOZ_ASSERT(
      aHandlingListItemElement.GetParent()->IsHTMLElement(&mListTagName));

  EditorDOMPoint atListItem(&aHandlingListItemElement);
  MOZ_ASSERT(atListItem.IsInContentNode());

  // If we've not met a list element, set current list element to the
  // parent of current list item element.
  if (!aState.mCurrentListElement) {
    aState.mCurrentListElement = atListItem.GetContainerAs<Element>();
    NS_WARNING_ASSERTION(
        HTMLEditUtils::IsAnyListElement(aState.mCurrentListElement),
        "Current list item parent is not a list element");
  }
  // If current list item element is not a child of current list element,
  // move it into current list item.
  else if (atListItem.GetContainer() != aState.mCurrentListElement) {
    Result<MoveNodeResult, nsresult> moveNodeResult =
        aHTMLEditor.MoveNodeToEndWithTransaction(
            aHandlingListItemElement,
            MOZ_KnownLive(*aState.mCurrentListElement));
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.propagateErr();
    }
    moveNodeResult.inspect().IgnoreCaretPointSuggestion();
  }

  // Then, if current list item element is not proper type for current
  // list element, convert list item element to proper element.
  if (aHandlingListItemElement.IsHTMLElement(&mListItemTagName)) {
    return NS_OK;
  }
  // FIXME: Manage attribute cloning
  Result<CreateElementResult, nsresult> newListItemElementOrError =
      aHTMLEditor.ReplaceContainerAndCloneAttributesWithTransaction(
          aHandlingListItemElement, mListItemTagName);
  if (MOZ_UNLIKELY(newListItemElementOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::ReplaceContainerAndCloneAttributesWithTransaction() "
        "failed");
    return newListItemElementOrError.propagateErr();
  }
  newListItemElementOrError.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildDivOrParagraphElement(
    HTMLEditor& aHTMLEditor, Element& aHandlingDivOrParagraphElement,
    AutoHandlingState& aState, const Element& aEditingHost) const {
  MOZ_ASSERT(aHandlingDivOrParagraphElement.IsAnyOfHTMLElements(nsGkAtoms::div,
                                                                nsGkAtoms::p));

  AutoRestore<RefPtr<Element>> previouslyReplacingBlockElement(
      aState.mReplacingBlockElement);
  aState.mReplacingBlockElement = &aHandlingDivOrParagraphElement;
  AutoRestore<bool> previouslyReplacingBlockElementIdCopied(
      aState.mMaybeCopiedReplacingBlockElementId);
  aState.mMaybeCopiedReplacingBlockElementId = false;

  // If the <div> or <p> is empty, we should replace it with a list element
  // and/or a list item element.
  if (HTMLEditUtils::IsEmptyNode(aHandlingDivOrParagraphElement,
                                 {EmptyCheckOption::TreatListItemAsVisible,
                                  EmptyCheckOption::TreatTableCellAsVisible})) {
    if (!aState.mCurrentListElement) {
      nsresult rv = CreateAndUpdateCurrentListElement(
          aHTMLEditor, EditorDOMPoint(&aHandlingDivOrParagraphElement),
          EmptyListItem::Create, aState, aEditingHost);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "AutoListElementCreator::CreateAndUpdateCurrentListElement("
            "EmptyListItem::Create) failed");
        return rv;
      }
    } else {
      Result<CreateElementResult, nsresult> createListItemElementResult =
          AppendListItemElement(
              aHTMLEditor, MOZ_KnownLive(*aState.mCurrentListElement), aState);
      if (MOZ_UNLIKELY(createListItemElementResult.isErr())) {
        NS_WARNING("AutoListElementCreator::AppendListItemElement() failed");
        return createListItemElementResult.unwrapErr();
      }
      CreateElementResult unwrappedResult =
          createListItemElementResult.unwrap();
      unwrappedResult.IgnoreCaretPointSuggestion();
      aState.mListOrListItemElementToPutCaret = unwrappedResult.UnwrapNewNode();
    }
    nsresult rv =
        aHTMLEditor.DeleteNodeWithTransaction(aHandlingDivOrParagraphElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteNodeWithTransaction() failed");
      return rv;
    }

    // We don't want new inline contents inserted into the new list item element
    // because we want to keep the line break at end of
    // aHandlingDivOrParagraphElement.
    aState.mPreviousListItemElement = nullptr;

    return NS_OK;
  }

  // If current node is a <div> element, replace it with its children and handle
  // them as same as topmost children in the range.
  AutoContentNodeArray arrayOfContentsInDiv;
  HTMLEditUtils::CollectChildren(aHandlingDivOrParagraphElement,
                                 arrayOfContentsInDiv, 0,
                                 {CollectChildrenOption::CollectListChildren,
                                  CollectChildrenOption::CollectTableChildren});

  Result<EditorDOMPoint, nsresult> unwrapDivElementResult =
      aHTMLEditor.RemoveContainerWithTransaction(
          aHandlingDivOrParagraphElement);
  if (MOZ_UNLIKELY(unwrapDivElementResult.isErr())) {
    NS_WARNING("HTMLEditor::RemoveContainerWithTransaction() failed");
    return unwrapDivElementResult.unwrapErr();
  }

  for (const OwningNonNull<nsIContent>& content : arrayOfContentsInDiv) {
    // MOZ_KnownLive because of bug 1620312
    nsresult rv = HandleChildContent(aHTMLEditor, MOZ_KnownLive(content),
                                     aState, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoListElementCreator::HandleChildContent() failed");
      return rv;
    }
  }

  // We don't want new inline contents inserted into the new list item element
  // because we want to keep the line break at end of
  // aHandlingDivOrParagraphElement.
  aState.mPreviousListItemElement = nullptr;

  return NS_OK;
}

nsresult HTMLEditor::AutoListElementCreator::CreateAndUpdateCurrentListElement(
    HTMLEditor& aHTMLEditor, const EditorDOMPoint& aPointToInsert,
    EmptyListItem aEmptyListItem, AutoHandlingState& aState,
    const Element& aEditingHost) const {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  aState.mPreviousListItemElement = nullptr;
  RefPtr<Element> newListItemElement;
  auto initializer =
      // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
      [&](HTMLEditor&, Element& aListElement, const EditorDOMPoint&)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            // If the replacing element has `dir` attribute, the new list
            // element should take it to correct its list marker position.
            if (aState.mReplacingBlockElement) {
              nsString dirValue;
              if (aState.mReplacingBlockElement->GetAttr(nsGkAtoms::dir,
                                                         dirValue) &&
                  !dirValue.IsEmpty()) {
                // We don't need to use transaction to set `dir` attribute here
                // because the element will be stored with the `dir` attribute
                // in InsertNodeTransaction.  Therefore, undo should work.
                IgnoredErrorResult ignoredError;
                aListElement.SetAttr(nsGkAtoms::dir, dirValue, ignoredError);
                NS_WARNING_ASSERTION(
                    !ignoredError.Failed(),
                    "Element::SetAttr(nsGkAtoms::dir) failed, but ignored");
              }
            }
            if (aEmptyListItem == EmptyListItem::Create) {
              Result<CreateElementResult, nsresult> createNewListItemResult =
                  AppendListItemElement(aHTMLEditor, aListElement, aState);
              if (MOZ_UNLIKELY(createNewListItemResult.isErr())) {
                NS_WARNING(
                    "HTMLEditor::AppendNewElementToInsertingElement()"
                    " failed");
                return createNewListItemResult.unwrapErr();
              }
              CreateElementResult unwrappedResult =
                  createNewListItemResult.unwrap();
              unwrappedResult.IgnoreCaretPointSuggestion();
              newListItemElement = unwrappedResult.UnwrapNewNode();
            }
            return NS_OK;
          };
  Result<CreateElementResult, nsresult> createNewListElementResult =
      aHTMLEditor.InsertElementWithSplittingAncestorsWithTransaction(
          mListTagName, aPointToInsert, BRElementNextToSplitPoint::Keep,
          aEditingHost, initializer);
  if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
    NS_WARNING(
        nsPrintfCString(
            "HTMLEditor::"
            "InsertElementWithSplittingAncestorsWithTransaction(%s) failed",
            nsAtomCString(&mListTagName).get())
            .get());
    return createNewListElementResult.propagateErr();
  }
  CreateElementResult unwrappedCreateNewListElementResult =
      createNewListElementResult.unwrap();
  unwrappedCreateNewListElementResult.IgnoreCaretPointSuggestion();

  MOZ_ASSERT(unwrappedCreateNewListElementResult.GetNewNode());
  aState.mListOrListItemElementToPutCaret =
      newListItemElement ? newListItemElement.get()
                         : unwrappedCreateNewListElementResult.GetNewNode();
  aState.mCurrentListElement =
      unwrappedCreateNewListElementResult.UnwrapNewNode();
  aState.mPreviousListItemElement = std::move(newListItemElement);
  return NS_OK;
}

// static
nsresult HTMLEditor::AutoListElementCreator::MaybeCloneAttributesToNewListItem(
    HTMLEditor& aHTMLEditor, Element& aListItemElement,
    AutoHandlingState& aState) {
  if (!aState.mReplacingBlockElement) {
    return NS_OK;
  }
  // If we're replacing a block element, the list items should have attributes
  // of the replacing element. However, we don't want to copy `dir` attribute
  // because it does not affect content in list item element and setting
  // opposite direction from the parent list causes the marker invisible.
  // Therefore, we don't want to take it. Finally, we don't need to use
  // transaction to copy the attributes here because the element will be stored
  // with the attributes in InsertNodeTransaction.  Therefore, undo should work.
  nsresult rv = aHTMLEditor.CopyAttributes(
      WithTransaction::No, aListItemElement,
      MOZ_KnownLive(*aState.mReplacingBlockElement),
      aState.mMaybeCopiedReplacingBlockElementId
          ? HTMLEditor::CopyAllAttributesExceptIdAndDir
          : HTMLEditor::CopyAllAttributesExceptDir);
  aState.mMaybeCopiedReplacingBlockElementId = true;
  if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::CopyAttributes(WithTransaction::No) failed");
  return rv;
}

Result<CreateElementResult, nsresult>
HTMLEditor::AutoListElementCreator::AppendListItemElement(
    HTMLEditor& aHTMLEditor, const Element& aListElement,
    AutoHandlingState& aState) const {
  const WithTransaction withTransaction = aListElement.IsInComposedDoc()
                                              ? WithTransaction::Yes
                                              : WithTransaction::No;
  Result<CreateElementResult, nsresult> createNewListItemResult =
      aHTMLEditor.CreateAndInsertElement(
          withTransaction, mListItemTagName,
          EditorDOMPoint::AtEndOf(aListElement),
          !aState.mReplacingBlockElement
              ? HTMLEditor::DoNothingForNewElement
              // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
              : [&aState](HTMLEditor& aHTMLEditor, Element& aListItemElement,
                          const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
                  nsresult rv =
                      AutoListElementCreator::MaybeCloneAttributesToNewListItem(
                          aHTMLEditor, aListItemElement, aState);
                  NS_WARNING_ASSERTION(
                      NS_SUCCEEDED(rv),
                      "AutoListElementCreator::"
                      "MaybeCloneAttributesToNewListItem() failed");
                  return rv;
                });
  NS_WARNING_ASSERTION(createNewListItemResult.isOk(),
                       "HTMLEditor::CreateAndInsertElement() failed");
  return createNewListItemResult;
}

nsresult HTMLEditor::AutoListElementCreator::HandleChildInlineContent(
    HTMLEditor& aHTMLEditor, nsIContent& aHandlingInlineContent,
    AutoHandlingState& aState) const {
  MOZ_ASSERT(HTMLEditUtils::IsInlineContent(
      aHandlingInlineContent, BlockInlineCheck::UseHTMLDefaultStyle));

  // If we're currently handling contents of a list item and current node
  // is not a block element, move current node into the list item.
  if (!aState.mPreviousListItemElement) {
    nsresult rv = WrapContentIntoNewListItemElement(
        aHTMLEditor, aHandlingInlineContent, aState);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "AutoListElementCreator::WrapContentIntoNewListItemElement() failed");
    return rv;
  }

  Result<MoveNodeResult, nsresult> moveInlineElementResult =
      aHTMLEditor.MoveNodeToEndWithTransaction(
          aHandlingInlineContent,
          MOZ_KnownLive(*aState.mPreviousListItemElement));
  if (MOZ_UNLIKELY(moveInlineElementResult.isErr())) {
    NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
    return moveInlineElementResult.propagateErr();
  }
  moveInlineElementResult.inspect().IgnoreCaretPointSuggestion();
  return NS_OK;
}

nsresult HTMLEditor::AutoListElementCreator::WrapContentIntoNewListItemElement(
    HTMLEditor& aHTMLEditor, nsIContent& aHandlingContent,
    AutoHandlingState& aState) const {
  // If current node is not a paragraph, wrap current node with new list
  // item element and move it into current list element.
  Result<CreateElementResult, nsresult> wrapContentInListItemElementResult =
      aHTMLEditor.InsertContainerWithTransaction(
          aHandlingContent, mListItemTagName,
          !aState.mReplacingBlockElement
              ? HTMLEditor::DoNothingForNewElement
              // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
              : [&aState](HTMLEditor& aHTMLEditor, Element& aListItemElement,
                          const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
                  nsresult rv =
                      AutoListElementCreator::MaybeCloneAttributesToNewListItem(
                          aHTMLEditor, aListItemElement, aState);
                  NS_WARNING_ASSERTION(
                      NS_SUCCEEDED(rv),
                      "AutoListElementCreator::"
                      "MaybeCloneAttributesToNewListItem() failed");
                  return rv;
                });
  if (MOZ_UNLIKELY(wrapContentInListItemElementResult.isErr())) {
    NS_WARNING("HTMLEditor::InsertContainerWithTransaction() failed");
    return wrapContentInListItemElementResult.unwrapErr();
  }
  CreateElementResult unwrappedWrapContentInListItemElementResult =
      wrapContentInListItemElementResult.unwrap();
  unwrappedWrapContentInListItemElementResult.IgnoreCaretPointSuggestion();
  MOZ_ASSERT(unwrappedWrapContentInListItemElementResult.GetNewNode());

  // MOZ_KnownLive(unwrappedWrapContentInListItemElementResult.GetNewNode()):
  // The result is grabbed by unwrappedWrapContentInListItemElementResult.
  Result<MoveNodeResult, nsresult> moveListItemElementResult =
      aHTMLEditor.MoveNodeToEndWithTransaction(
          MOZ_KnownLive(
              *unwrappedWrapContentInListItemElementResult.GetNewNode()),
          MOZ_KnownLive(*aState.mCurrentListElement));
  if (MOZ_UNLIKELY(moveListItemElementResult.isErr())) {
    NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
    return moveListItemElementResult.unwrapErr();
  }
  moveListItemElementResult.inspect().IgnoreCaretPointSuggestion();

  // If current node is not a block element, new list item should have
  // following inline nodes too.
  if (HTMLEditUtils::IsInlineContent(aHandlingContent,
                                     BlockInlineCheck::UseHTMLDefaultStyle)) {
    aState.mPreviousListItemElement =
        unwrappedWrapContentInListItemElementResult.UnwrapNewNode();
  } else {
    aState.mPreviousListItemElement = nullptr;
  }

  // XXX Why don't we set `type` attribute here??
  return NS_OK;
}

nsresult HTMLEditor::AutoListElementCreator::
    EnsureCollapsedRangeIsInListItemOrListElement(
        Element& aListItemOrListToPutCaret,
        AutoClonedRangeArray& aRanges) const {
  if (!aRanges.IsCollapsed() || aRanges.Ranges().IsEmpty()) {
    return NS_OK;
  }

  const auto firstRangeStartPoint =
      aRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
  if (MOZ_UNLIKELY(!firstRangeStartPoint.IsSet())) {
    return NS_OK;
  }
  Result<EditorRawDOMPoint, nsresult> pointToPutCaretOrError =
      HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
          EditorRawDOMPoint>(aListItemOrListToPutCaret, firstRangeStartPoint);
  if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
    NS_WARNING("HTMLEditUtils::ComputePointToPutCaretInElementIfOutside()");
    return pointToPutCaretOrError.unwrapErr();
  }
  if (pointToPutCaretOrError.inspect().IsSet()) {
    nsresult rv = aRanges.Collapse(pointToPutCaretOrError.inspect());
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return rv;
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::RemoveListAtSelectionAsSubAction(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult error;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eRemoveList, nsIEditor::eNext, error);
  if (NS_WARN_IF(error.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return error.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !error.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // XXX Why do we do this only when there is only one selection range?
  if (!SelectionRef().IsCollapsed() && SelectionRef().RangeCount() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            SelectionRef().GetRangeAt(0u), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.unwrapErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use Selection::SetStartAndEndInLimit() here.
    error.SuppressException();
    SelectionRef().SetBaseAndExtentInLimiter(
        extendedRange.inspect().StartRef().ToRawRangeBoundary(),
        extendedRange.inspect().EndRef().ToRawRangeBoundary(), error);
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (error.Failed()) {
      NS_WARNING("Selection::SetBaseAndExtentInLimiter() failed");
      return error.StealNSResult();
    }
  }

  AutoSelectionRestorer restoreSelectionLater(this);

  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    // TODO: We don't need AutoTransactionsConserveSelection here in the normal
    //       cases, but removing this may cause the behavior with the legacy
    //       mutation event listeners.  We should try to delete this in a bug.
    AutoTransactionsConserveSelection dontChangeMySelection(*this);

    {
      AutoClonedSelectionRangeArray extendedSelectionRanges(SelectionRef());
      extendedSelectionRanges.ExtendRangesToWrapLines(
          EditSubAction::eCreateOrChangeList,
          BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
      Result<EditorDOMPoint, nsresult> splitResult =
          extendedSelectionRanges
              .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
                  *this, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
      if (MOZ_UNLIKELY(splitResult.isErr())) {
        NS_WARNING(
            "AutoClonedRangeArray::"
            "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() "
            "failed");
        return splitResult.unwrapErr();
      }
      nsresult rv = extendedSelectionRanges.CollectEditTargetNodes(
          *this, arrayOfContents, EditSubAction::eCreateOrChangeList,
          AutoClonedRangeArray::CollectNonEditableNodes::No);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
            "eCreateOrChangeList, CollectNonEditableNodes::No) failed");
        return rv;
      }
    }

    const Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
        MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                           EditSubAction::eCreateOrChangeList);
    if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
          "eCreateOrChangeList) failed");
      return splitAtBRElementsResult.inspectErr();
    }
  }

  // Remove all non-editable nodes.  Leave them be.
  // XXX CollectEditTargetNodes() should return only editable contents when it's
  //     called with CollectNonEditableNodes::No, but checking it here, looks
  //     like just wasting the runtime cost.
  for (int32_t i = arrayOfContents.Length() - 1; i >= 0; i--) {
    OwningNonNull<nsIContent>& content = arrayOfContents[i];
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      arrayOfContents.RemoveElementAt(i);
    }
  }

  // Only act on lists or list items in the array
  for (auto& content : arrayOfContents) {
    // here's where we actually figure out what to do
    if (HTMLEditUtils::IsListItem(content)) {
      // unlist this listitem
      nsresult rv = LiftUpListItemElement(MOZ_KnownLive(*content->AsElement()),
                                          LiftUpFromAllParentListElements::Yes);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::LiftUpListItemElement(LiftUpFromAllParentListElements:"
            ":Yes) failed");
        return rv;
      }
      continue;
    }
    if (HTMLEditUtils::IsAnyListElement(content)) {
      // node is a list, move list items out
      nsresult rv =
          DestroyListStructureRecursively(MOZ_KnownLive(*content->AsElement()));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::DestroyListStructureRecursively() failed");
        return rv;
      }
      continue;
    }
  }
  return NS_OK;
}

Result<RefPtr<Element>, nsresult>
HTMLEditor::FormatBlockContainerWithTransaction(
    AutoClonedSelectionRangeArray& aSelectionRanges,
    const nsStaticAtom& aNewFormatTagName, FormatBlockMode aFormatBlockMode,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // XXX Why do we do this only when there is only one selection range?
  if (!aSelectionRanges.IsCollapsed() &&
      aSelectionRanges.Ranges().Length() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            aSelectionRanges.FirstRangeRef(), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.propagateErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use AutoClonedRangeArray::SetStartAndEnd() here.
    if (NS_FAILED(aSelectionRanges.SetBaseAndExtent(
            extendedRange.inspect().StartRef(),
            extendedRange.inspect().EndRef()))) {
      NS_WARNING("AutoClonedRangeArray::SetBaseAndExtent() failed");
      return Err(NS_ERROR_FAILURE);
    }
  }

  MOZ_ALWAYS_TRUE(aSelectionRanges.SaveAndTrackRanges(*this));

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  aSelectionRanges.ExtendRangesToWrapLines(
      aFormatBlockMode == FormatBlockMode::HTMLFormatBlockCommand
          ? EditSubAction::eFormatBlockForHTMLCommand
          : EditSubAction::eCreateOrRemoveBlock,
      BlockInlineCheck::UseComputedDisplayOutsideStyle, aEditingHost);
  Result<EditorDOMPoint, nsresult> splitResult =
      aSelectionRanges
          .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
              *this, BlockInlineCheck::UseComputedDisplayOutsideStyle,
              aEditingHost);
  if (MOZ_UNLIKELY(splitResult.isErr())) {
    NS_WARNING(
        "AutoClonedRangeArray::"
        "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() failed");
    return splitResult.propagateErr();
  }
  nsresult rv = aSelectionRanges.CollectEditTargetNodes(
      *this, arrayOfContents,
      aFormatBlockMode == FormatBlockMode::HTMLFormatBlockCommand
          ? EditSubAction::eFormatBlockForHTMLCommand
          : EditSubAction::eCreateOrRemoveBlock,
      AutoClonedRangeArray::CollectNonEditableNodes::Yes);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "AutoClonedRangeArray::CollectEditTargetNodes(CollectNonEditableNodes::"
        "No) failed");
    return Err(rv);
  }

  Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
      MaybeSplitElementsAtEveryBRElement(
          arrayOfContents,
          aFormatBlockMode == FormatBlockMode::HTMLFormatBlockCommand
              ? EditSubAction::eFormatBlockForHTMLCommand
              : EditSubAction::eCreateOrRemoveBlock);
  if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
    NS_WARNING("HTMLEditor::MaybeSplitElementsAtEveryBRElement() failed");
    return splitAtBRElementsResult.propagateErr();
  }

  // If there is no visible and editable nodes in the edit targets, make an
  // empty block.
  // XXX Isn't this odd if there are only non-editable visible nodes?
  if (HTMLEditUtils::IsEmptyOneHardLine(
          arrayOfContents, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    if (NS_WARN_IF(aSelectionRanges.Ranges().IsEmpty())) {
      return Err(NS_ERROR_FAILURE);
    }

    auto pointToInsertBlock =
        aSelectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (aFormatBlockMode == FormatBlockMode::XULParagraphStateCommand &&
        (&aNewFormatTagName == nsGkAtoms::normal ||
         &aNewFormatTagName == nsGkAtoms::_empty)) {
      if (!pointToInsertBlock.IsInContentNode()) {
        NS_WARNING(
            "HTMLEditor::FormatBlockContainerWithTransaction() couldn't find "
            "block parent because container of the point is not content");
        return Err(NS_ERROR_FAILURE);
      }
      // We are removing blocks (going to "body text")
      const RefPtr<Element> editableBlockElement =
          HTMLEditUtils::GetInclusiveAncestorElement(
              *pointToInsertBlock.ContainerAs<nsIContent>(),
              HTMLEditUtils::ClosestEditableBlockElement,
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (!editableBlockElement) {
        NS_WARNING(
            "HTMLEditor::FormatBlockContainerWithTransaction() couldn't find "
            "block parent");
        return Err(NS_ERROR_FAILURE);
      }
      if (editableBlockElement->IsAnyOfHTMLElements(
              nsGkAtoms::dd, nsGkAtoms::dl, nsGkAtoms::dt) ||
          !HTMLEditUtils::IsFormatElementForParagraphStateCommand(
              *editableBlockElement)) {
        return RefPtr<Element>();
      }

      // If the first editable node after selection is a br, consume it.
      // Otherwise it gets pushed into a following block after the split,
      // which is visually bad.
      if (nsCOMPtr<nsIContent> brContent = HTMLEditUtils::GetNextContent(
              pointToInsertBlock, {WalkTreeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle,
              &aEditingHost)) {
        if (brContent && brContent->IsHTMLElement(nsGkAtoms::br)) {
          AutoEditorDOMPointChildInvalidator lockOffset(pointToInsertBlock);
          nsresult rv = DeleteNodeWithTransaction(*brContent);
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
            return Err(rv);
          }
        }
      }
      // Do the splits!
      Result<SplitNodeResult, nsresult> splitNodeResult =
          SplitNodeDeepWithTransaction(
              *editableBlockElement, pointToInsertBlock,
              SplitAtEdges::eDoNotCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
        return splitNodeResult.propagateErr();
      }
      SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
      unwrappedSplitNodeResult.IgnoreCaretPointSuggestion();
      // Put a <br> element at the split point
      Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
          InsertLineBreak(
              WithTransaction::Yes, LineBreakType::BRElement,
              unwrappedSplitNodeResult.AtSplitPoint<EditorDOMPoint>());
      if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes "
            "LineBreakType::BRElement) failed");
        return insertBRElementResultOrError.propagateErr();
      }
      CreateLineBreakResult insertBRElementResult =
          insertBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertBRElementResult.Handled());
      aSelectionRanges.ClearSavedRanges();
      nsresult rv =
          aSelectionRanges.Collapse(insertBRElementResult.UnwrapCaretPoint());
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoClonedRangeArray::Collapse() failed");
        return Err(rv);
      }
      return RefPtr<Element>();
    }

    // We are making a block.  Consume a br, if needed.
    if (nsCOMPtr<nsIContent> maybeBRContent = HTMLEditUtils::GetNextContent(
            pointToInsertBlock,
            {WalkTreeOption::IgnoreNonEditableNode,
             WalkTreeOption::StopAtBlockBoundary},
            BlockInlineCheck::UseComputedDisplayOutsideStyle, &aEditingHost)) {
      if (maybeBRContent->IsHTMLElement(nsGkAtoms::br)) {
        AutoEditorDOMPointChildInvalidator lockOffset(pointToInsertBlock);
        nsresult rv = DeleteNodeWithTransaction(*maybeBRContent);
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return Err(rv);
        }
        // We don't need to act on this node any more
        arrayOfContents.RemoveElement(maybeBRContent);
      }
    }
    // Make sure we can put a block here.
    Result<CreateElementResult, nsresult> createNewBlockElementResult =
        InsertElementWithSplittingAncestorsWithTransaction(
            aNewFormatTagName, pointToInsertBlock,
            BRElementNextToSplitPoint::Keep, aEditingHost);
    if (MOZ_UNLIKELY(createNewBlockElementResult.isErr())) {
      NS_WARNING(
          nsPrintfCString(
              "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
              "%s) failed",
              nsAtomCString(&aNewFormatTagName).get())
              .get());
      return createNewBlockElementResult.propagateErr();
    }
    CreateElementResult unwrappedCreateNewBlockElementResult =
        createNewBlockElementResult.unwrap();
    unwrappedCreateNewBlockElementResult.IgnoreCaretPointSuggestion();
    MOZ_ASSERT(unwrappedCreateNewBlockElementResult.GetNewNode());

    // Delete anything that was in the list of nodes
    while (!arrayOfContents.IsEmpty()) {
      OwningNonNull<nsIContent>& content = arrayOfContents[0];
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
      // keep it alive.
      nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*content));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return Err(rv);
      }
      arrayOfContents.RemoveElementAt(0);
    }
    // Put selection in new block
    aSelectionRanges.ClearSavedRanges();
    nsresult rv = aSelectionRanges.Collapse(EditorRawDOMPoint(
        unwrappedCreateNewBlockElementResult.GetNewNode(), 0u));
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::Collapse() failed");
      return Err(rv);
    }
    return unwrappedCreateNewBlockElementResult.UnwrapNewNode();
  }

  if (aFormatBlockMode == FormatBlockMode::XULParagraphStateCommand) {
    // Okay, now go through all the nodes and make the right kind of blocks, or
    // whatever is appropriate.
    // Note: blockquote is handled a little differently.
    if (&aNewFormatTagName == nsGkAtoms::blockquote) {
      Result<CreateElementResult, nsresult>
          wrapContentsInBlockquoteElementsResult =
              WrapContentsInBlockquoteElementsWithTransaction(arrayOfContents,
                                                              aEditingHost);
      if (MOZ_UNLIKELY(wrapContentsInBlockquoteElementsResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::WrapContentsInBlockquoteElementsWithTransaction() "
            "failed");
        return wrapContentsInBlockquoteElementsResult.propagateErr();
      }
      wrapContentsInBlockquoteElementsResult.inspect()
          .IgnoreCaretPointSuggestion();
      return wrapContentsInBlockquoteElementsResult.unwrap().UnwrapNewNode();
    }
    if (&aNewFormatTagName == nsGkAtoms::normal ||
        &aNewFormatTagName == nsGkAtoms::_empty) {
      Result<EditorDOMPoint, nsresult> removeBlockContainerElementsResult =
          RemoveBlockContainerElementsWithTransaction(
              arrayOfContents, FormatBlockMode::XULParagraphStateCommand,
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (MOZ_UNLIKELY(removeBlockContainerElementsResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::RemoveBlockContainerElementsWithTransaction() failed");
        return removeBlockContainerElementsResult.propagateErr();
      }
      return RefPtr<Element>();
    }
  }

  Result<CreateElementResult, nsresult> wrapContentsInBlockElementResult =
      CreateOrChangeFormatContainerElement(arrayOfContents, aNewFormatTagName,
                                           aFormatBlockMode, aEditingHost);
  if (MOZ_UNLIKELY(wrapContentsInBlockElementResult.isErr())) {
    NS_WARNING("HTMLEditor::CreateOrChangeFormatContainerElement() failed");
    return wrapContentsInBlockElementResult.propagateErr();
  }
  wrapContentsInBlockElementResult.inspect().IgnoreCaretPointSuggestion();
  return wrapContentsInBlockElementResult.unwrap().UnwrapNewNode();
}

Result<EditActionResult, nsresult> HTMLEditor::IndentAsSubAction(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eIndent, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Some selection containers are not content node, but ignored");
    return EditActionResult::IgnoredResult();
  }

  Result<EditActionResult, nsresult> result =
      HandleIndentAtSelection(aEditingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::HandleIndentAtSelection() failed");
    return result;
  }
  if (result.inspect().Canceled()) {
    return result;
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Mutation event listener might have changed selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (!SelectionRef().IsCollapsed()) {
    return result;
  }

  // TODO: Investigate when we need to put a `<br>` element after indenting
  //       ranges.  Then, we could stop calling this here, or maybe we need to
  //       do it while moving content nodes.
  const auto caretPosition =
      EditorBase::GetFirstSelectionStartPoint<EditorDOMPoint>();
  Result<CreateLineBreakResult, nsresult> insertPaddingBRElementResultOrError =
      InsertPaddingBRElementIfInEmptyBlock(caretPosition, eNoStrip);
  if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(eNoStrip) failed");
    return insertPaddingBRElementResultOrError.propagateErr();
  }
  nsresult rv =
      insertPaddingBRElementResultOrError.unwrap().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                       "CaretPoint::SuggestCaretPointTo() failed, but ignored");
  return result;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::IndentListChildWithTransaction(
    RefPtr<Element>* aSubListElement, const EditorDOMPoint& aPointInListElement,
    nsIContent& aContentMovingToSubList, const Element& aEditingHost) {
  MOZ_ASSERT(
      HTMLEditUtils::IsAnyListElement(aPointInListElement.GetContainer()),
      "unexpected container");
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // some logic for putting list items into nested lists...

  // If aContentMovingToSubList is followed by a sub-list element whose tag is
  // same as the parent list element's tag, we can move it to start of the
  // sub-list.
  if (nsIContent* nextEditableSibling = HTMLEditUtils::GetNextSibling(
          aContentMovingToSubList, {WalkTreeOption::IgnoreWhiteSpaceOnlyText,
                                    WalkTreeOption::IgnoreNonEditableNode})) {
    if (HTMLEditUtils::IsAnyListElement(nextEditableSibling) &&
        aPointInListElement.GetContainer()->NodeInfo()->NameAtom() ==
            nextEditableSibling->NodeInfo()->NameAtom() &&
        aPointInListElement.GetContainer()->NodeInfo()->NamespaceID() ==
            nextEditableSibling->NodeInfo()->NamespaceID()) {
      Result<MoveNodeResult, nsresult> moveListElementResult =
          MoveNodeWithTransaction(aContentMovingToSubList,
                                  EditorDOMPoint(nextEditableSibling, 0u));
      if (MOZ_UNLIKELY(moveListElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
        return moveListElementResult.propagateErr();
      }
      return moveListElementResult.unwrap().UnwrapCaretPoint();
    }
  }

  // If aContentMovingToSubList follows a sub-list element whose tag is same
  // as the parent list element's tag, we can move it to end of the sub-list.
  if (nsCOMPtr<nsIContent> previousEditableSibling =
          HTMLEditUtils::GetPreviousSibling(
              aContentMovingToSubList,
              {WalkTreeOption::IgnoreWhiteSpaceOnlyText,
               WalkTreeOption::IgnoreNonEditableNode})) {
    if (HTMLEditUtils::IsAnyListElement(previousEditableSibling) &&
        aPointInListElement.GetContainer()->NodeInfo()->NameAtom() ==
            previousEditableSibling->NodeInfo()->NameAtom() &&
        aPointInListElement.GetContainer()->NodeInfo()->NamespaceID() ==
            previousEditableSibling->NodeInfo()->NamespaceID()) {
      Result<MoveNodeResult, nsresult> moveListElementResult =
          MoveNodeToEndWithTransaction(aContentMovingToSubList,
                                       *previousEditableSibling);
      if (MOZ_UNLIKELY(moveListElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveListElementResult.propagateErr();
      }
      return moveListElementResult.unwrap().UnwrapCaretPoint();
    }
  }

  // If aContentMovingToSubList does not follow aSubListElement, we need
  // to create new sub-list element.
  EditorDOMPoint pointToPutCaret;
  nsIContent* previousEditableSibling =
      *aSubListElement ? HTMLEditUtils::GetPreviousSibling(
                             aContentMovingToSubList,
                             {WalkTreeOption::IgnoreWhiteSpaceOnlyText,
                              WalkTreeOption::IgnoreNonEditableNode})
                       : nullptr;
  if (!*aSubListElement || (previousEditableSibling &&
                            previousEditableSibling != *aSubListElement)) {
    nsAtom* containerName =
        aPointInListElement.GetContainer()->NodeInfo()->NameAtom();
    // Create a new nested list of correct type.
    Result<CreateElementResult, nsresult> createNewListElementResult =
        InsertElementWithSplittingAncestorsWithTransaction(
            MOZ_KnownLive(*containerName), aPointInListElement,
            BRElementNextToSplitPoint::Keep, aEditingHost);
    if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
      NS_WARNING(
          nsPrintfCString(
              "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
              "%s) failed",
              nsAtomCString(containerName).get())
              .get());
      return createNewListElementResult.propagateErr();
    }
    CreateElementResult unwrappedCreateNewListElementResult =
        createNewListElementResult.unwrap();
    MOZ_ASSERT(unwrappedCreateNewListElementResult.GetNewNode());
    pointToPutCaret = unwrappedCreateNewListElementResult.UnwrapCaretPoint();
    *aSubListElement = unwrappedCreateNewListElementResult.UnwrapNewNode();
  }

  // Finally, we should move aContentMovingToSubList into aSubListElement.
  const RefPtr<Element> subListElement = *aSubListElement;
  Result<MoveNodeResult, nsresult> moveNodeResult =
      MoveNodeToEndWithTransaction(aContentMovingToSubList, *subListElement);
  if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
    NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
    return moveNodeResult.propagateErr();
  }
  MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
  if (unwrappedMoveNodeResult.HasCaretPointSuggestion()) {
    pointToPutCaret = unwrappedMoveNodeResult.UnwrapCaretPoint();
  }
  return pointToPutCaret;
}

Result<EditActionResult, nsresult> HTMLEditor::HandleIndentAtSelection(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsSelectionRangeContainerNotContent());

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());

  if (MOZ_UNLIKELY(!selectionRanges.IsInContent())) {
    NS_WARNING("Mutation event listener might have changed the selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (IsCSSEnabled()) {
    nsresult rv = HandleCSSIndentAroundRanges(selectionRanges, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::HandleCSSIndentAroundRanges() failed");
      return Err(rv);
    }
  } else {
    nsresult rv = HandleHTMLIndentAroundRanges(selectionRanges, aEditingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::HandleHTMLIndentAroundRanges() failed");
      return Err(rv);
    }
  }
  rv = selectionRanges.ApplyTo(SelectionRef());
  if (MOZ_UNLIKELY(Destroyed())) {
    NS_WARNING(
        "AutoClonedSelectionRangeArray::ApplyTo() caused destroying the "
        "editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoClonedSelectionRangeArray::ApplyTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::HandleCSSIndentAroundRanges(
    AutoClonedSelectionRangeArray& aRanges, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(!aRanges.Ranges().IsEmpty());
  MOZ_ASSERT(aRanges.IsInContent());

  if (aRanges.Ranges().IsEmpty()) {
    NS_WARNING("There is no selection range");
    return NS_ERROR_FAILURE;
  }

  // XXX Why do we do this only when there is only one selection range?
  if (!aRanges.IsCollapsed() && aRanges.Ranges().Length() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            aRanges.FirstRangeRef(), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.unwrapErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use SetStartAndEnd() here.
    nsresult rv = aRanges.SetBaseAndExtent(extendedRange.inspect().StartRef(),
                                           extendedRange.inspect().EndRef());
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::SetBaseAndExtent() failed");
      return rv;
    }
  }

  if (NS_WARN_IF(!aRanges.SaveAndTrackRanges(*this))) {
    return NS_ERROR_FAILURE;
  }

  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;

  // short circuit: detect case of collapsed selection inside an <li>.
  // just sublist that <li>.  This prevents bug 97797.

  if (aRanges.IsCollapsed()) {
    const auto atCaret = aRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
    if (NS_WARN_IF(!atCaret.IsSet())) {
      return NS_ERROR_FAILURE;
    }
    MOZ_ASSERT(atCaret.IsInContentNode());
    Element* const editableBlockElement =
        HTMLEditUtils::GetInclusiveAncestorElement(
            *atCaret.ContainerAs<nsIContent>(),
            HTMLEditUtils::ClosestEditableBlockElement,
            BlockInlineCheck::UseHTMLDefaultStyle);
    if (editableBlockElement &&
        HTMLEditUtils::IsListItem(editableBlockElement)) {
      arrayOfContents.AppendElement(*editableBlockElement);
    }
  }

  EditorDOMPoint pointToPutCaret;
  if (arrayOfContents.IsEmpty()) {
    {
      AutoClonedSelectionRangeArray extendedRanges(aRanges);
      extendedRanges.ExtendRangesToWrapLines(
          EditSubAction::eIndent, BlockInlineCheck::UseHTMLDefaultStyle,
          aEditingHost);
      Result<EditorDOMPoint, nsresult> splitResult =
          extendedRanges
              .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
                  *this, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
      if (MOZ_UNLIKELY(splitResult.isErr())) {
        NS_WARNING(
            "AutoClonedRangeArray::"
            "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() "
            "failed");
        return splitResult.unwrapErr();
      }
      if (splitResult.inspect().IsSet()) {
        pointToPutCaret = splitResult.unwrap();
      }
      nsresult rv = extendedRanges.CollectEditTargetNodes(
          *this, arrayOfContents, EditSubAction::eIndent,
          AutoClonedRangeArray::CollectNonEditableNodes::Yes);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
            "eIndent, CollectNonEditableNodes::Yes) failed");
        return rv;
      }
    }
    Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
        MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                           EditSubAction::eIndent);
    if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
          "eIndent) failed");
      return splitAtBRElementsResult.inspectErr();
    }
    if (splitAtBRElementsResult.inspect().IsSet()) {
      pointToPutCaret = splitAtBRElementsResult.unwrap();
    }
  }

  // If there is no visible and editable nodes in the edit targets, make an
  // empty block.
  // XXX Isn't this odd if there are only non-editable visible nodes?
  if (HTMLEditUtils::IsEmptyOneHardLine(
          arrayOfContents, BlockInlineCheck::UseHTMLDefaultStyle)) {
    const EditorDOMPoint pointToInsertDivElement =
        pointToPutCaret.IsSet()
            ? std::move(pointToPutCaret)
            : aRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!pointToInsertDivElement.IsSet())) {
      return NS_ERROR_FAILURE;
    }

    // make sure we can put a block here
    Result<CreateElementResult, nsresult> createNewDivElementResult =
        InsertElementWithSplittingAncestorsWithTransaction(
            *nsGkAtoms::div, pointToInsertDivElement,
            BRElementNextToSplitPoint::Keep, aEditingHost);
    if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
          "nsGkAtoms::div) failed");
      return createNewDivElementResult.unwrapErr();
    }
    CreateElementResult unwrappedCreateNewDivElementResult =
        createNewDivElementResult.unwrap();
    // We'll collapse ranges below, so we don't need to touch the ranges here.
    unwrappedCreateNewDivElementResult.IgnoreCaretPointSuggestion();
    const RefPtr<Element> newDivElement =
        unwrappedCreateNewDivElementResult.UnwrapNewNode();
    MOZ_ASSERT(newDivElement);
    const Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
        ChangeMarginStart(*newDivElement, ChangeMargin::Increase, aEditingHost);
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      if (NS_WARN_IF(pointToPutCaretOrError.inspectErr() ==
                     NS_ERROR_EDITOR_DESTROYED)) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING(
          "HTMLEditor::ChangeMarginStart(ChangeMargin::Increase) failed, but "
          "ignored");
    }
    // delete anything that was in the list of nodes
    // XXX We don't need to remove the nodes from the array for performance.
    for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
      // MOZ_KnownLive(content) due to bug 1622253
      nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(content));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
    }
    aRanges.ClearSavedRanges();
    nsresult rv = aRanges.Collapse(EditorDOMPoint(newDivElement, 0u));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoClonedRangeArray::Collapse() failed");
    return rv;
  }

  RefPtr<Element> latestNewBlockElement;
  auto RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside =
      [&]() -> nsresult {
    MOZ_ASSERT(aRanges.HasSavedRanges());
    aRanges.RestoreFromSavedRanges();

    if (!latestNewBlockElement || !aRanges.IsCollapsed() ||
        aRanges.Ranges().IsEmpty()) {
      return NS_OK;
    }

    const auto firstRangeStartRawPoint =
        aRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
    if (MOZ_UNLIKELY(!firstRangeStartRawPoint.IsSet())) {
      return NS_OK;
    }
    Result<EditorRawDOMPoint, nsresult> pointInNewBlockElementOrError =
        HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
            EditorRawDOMPoint>(*latestNewBlockElement, firstRangeStartRawPoint);
    if (MOZ_UNLIKELY(pointInNewBlockElementOrError.isErr())) {
      NS_WARNING(
          "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() failed, "
          "but ignored");
      return NS_OK;
    }
    if (!pointInNewBlockElementOrError.inspect().IsSet()) {
      return NS_OK;
    }
    return aRanges.Collapse(pointInNewBlockElementOrError.unwrap());
  };

  // Ok, now go through all the nodes and put them into sub-list element
  // elements and new <div> elements which have start margin.
  RefPtr<Element> subListElement, divElement;
  for (OwningNonNull<nsIContent>& content : arrayOfContents) {
    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsSet())) {
      continue;
    }

    // Ignore all non-editable nodes.  Leave them be.
    // XXX We ignore non-editable nodes here, but not so in the above block.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    if (HTMLEditUtils::IsAnyListElement(atContent.GetContainer())) {
      const RefPtr<Element> oldSubListElement = subListElement;
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
      // keep it alive.
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          IndentListChildWithTransaction(&subListElement, atContent,
                                         MOZ_KnownLive(content), aEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING("HTMLEditor::IndentListChildWithTransaction() failed");
        return pointToPutCaretOrError.unwrapErr();
      }
      if (subListElement != oldSubListElement) {
        // New list element is created, so we should put caret into the new list
        // element.
        latestNewBlockElement = subListElement;
      }
      if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
      continue;
    }

    // Not a list item.

    if (HTMLEditUtils::IsBlockElement(content,
                                      BlockInlineCheck::UseHTMLDefaultStyle)) {
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          ChangeMarginStart(MOZ_KnownLive(*content->AsElement()),
                            ChangeMargin::Increase, aEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        if (MOZ_UNLIKELY(pointToPutCaretOrError.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED)) {
          NS_WARNING(
              "HTMLEditor::ChangeMarginStart(ChangeMargin::Increase) failed");
          return NS_ERROR_EDITOR_DESTROYED;
        }
        NS_WARNING(
            "HTMLEditor::ChangeMarginStart(ChangeMargin::Increase) failed, but "
            "ignored");
      } else if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
      divElement = nullptr;
      continue;
    }

    if (!divElement) {
      // First, check that our element can contain a div.
      if (!HTMLEditUtils::CanNodeContain(*atContent.GetContainer(),
                                         *nsGkAtoms::div)) {
        // XXX This is odd, why do we stop indenting remaining content nodes?
        //     Perhaps, `continue` is better.
        nsresult rv =
            RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside();
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "RestoreSavedRangesAndCollapseInLatestBlockElement"
                             "IfOutside() failed");
        return rv;
      }

      Result<CreateElementResult, nsresult> createNewDivElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              *nsGkAtoms::div, atContent, BRElementNextToSplitPoint::Keep,
              aEditingHost);
      if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "nsGkAtoms::div) failed");
        return createNewDivElementResult.unwrapErr();
      }
      CreateElementResult unwrappedCreateNewDivElementResult =
          createNewDivElementResult.unwrap();
      pointToPutCaret = unwrappedCreateNewDivElementResult.UnwrapCaretPoint();

      MOZ_ASSERT(unwrappedCreateNewDivElementResult.GetNewNode());
      divElement = unwrappedCreateNewDivElementResult.UnwrapNewNode();
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          ChangeMarginStart(*divElement, ChangeMargin::Increase, aEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        if (MOZ_UNLIKELY(pointToPutCaretOrError.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED)) {
          NS_WARNING(
              "HTMLEditor::ChangeMarginStart(ChangeMargin::Increase) failed");
          return NS_ERROR_EDITOR_DESTROYED;
        }
        NS_WARNING(
            "HTMLEditor::ChangeMarginStart(ChangeMargin::Increase) failed, but "
            "ignored");
      } else if (AllowsTransactionsToChangeSelection() &&
                 pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }

      latestNewBlockElement = divElement;
    }

    // Move the content into the <div> which has start margin.
    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
    // keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeToEndWithTransaction(MOZ_KnownLive(content), *divElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.unwrapErr();
    }
    MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
    if (unwrappedMoveNodeResult.HasCaretPointSuggestion()) {
      pointToPutCaret = unwrappedMoveNodeResult.UnwrapCaretPoint();
    }
  }

  nsresult rv = RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside() failed");
  return rv;
}

nsresult HTMLEditor::HandleHTMLIndentAroundRanges(
    AutoClonedSelectionRangeArray& aRanges, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(!aRanges.Ranges().IsEmpty());
  MOZ_ASSERT(aRanges.IsInContent());

  // XXX Why do we do this only when there is only one range?
  if (!aRanges.IsCollapsed() && aRanges.Ranges().Length() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            aRanges.FirstRangeRef(), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.unwrapErr();
    }
    // Note that end point may be prior to start point.  So, we cannot use
    // SetStartAndEnd() here.
    nsresult rv = aRanges.SetBaseAndExtent(extendedRange.inspect().StartRef(),
                                           extendedRange.inspect().EndRef());
    if (NS_FAILED(rv)) {
      NS_WARNING("AutoClonedRangeArray::SetBaseAndExtent() failed");
      return rv;
    }
  }

  if (NS_WARN_IF(!aRanges.SaveAndTrackRanges(*this))) {
    return NS_ERROR_FAILURE;
  }

  EditorDOMPoint pointToPutCaret;

  // convert the selection ranges into "promoted" selection ranges:
  // this basically just expands the range to include the immediate
  // block parent, and then further expands to include any ancestors
  // whose children are all in the range

  // use these ranges to construct a list of nodes to act on.
  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    AutoClonedSelectionRangeArray extendedRanges(aRanges);
    extendedRanges.ExtendRangesToWrapLines(
        EditSubAction::eIndent, BlockInlineCheck::UseHTMLDefaultStyle,
        aEditingHost);
    Result<EditorDOMPoint, nsresult> splitResult =
        extendedRanges
            .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
                *this, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
    if (MOZ_UNLIKELY(splitResult.isErr())) {
      NS_WARNING(
          "AutoClonedRangeArray::"
          "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() "
          "failed");
      return splitResult.unwrapErr();
    }
    if (splitResult.inspect().IsSet()) {
      pointToPutCaret = splitResult.unwrap();
    }
    nsresult rv = extendedRanges.CollectEditTargetNodes(
        *this, arrayOfContents, EditSubAction::eIndent,
        AutoClonedRangeArray::CollectNonEditableNodes::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::eIndent,"
          " CollectNonEditableNodes::Yes) failed");
      return rv;
    }
  }

  // FIXME: Split ancestors when we consider to indent the range.
  Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
      MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                         EditSubAction::eIndent);
  if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::eIndent)"
        " failed");
    return splitAtBRElementsResult.inspectErr();
  }
  if (splitAtBRElementsResult.inspect().IsSet()) {
    pointToPutCaret = splitAtBRElementsResult.unwrap();
  }

  // If there is no visible and editable nodes in the edit targets, make an
  // empty block.
  // XXX Isn't this odd if there are only non-editable visible nodes?
  if (HTMLEditUtils::IsEmptyOneHardLine(
          arrayOfContents, BlockInlineCheck::UseHTMLDefaultStyle)) {
    const EditorDOMPoint pointToInsertBlockquoteElement =
        pointToPutCaret.IsSet()
            ? std::move(pointToPutCaret)
            : EditorBase::GetFirstSelectionStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!pointToInsertBlockquoteElement.IsSet())) {
      return NS_ERROR_FAILURE;
    }

    // If there is no element which can have <blockquote>, abort.
    if (NS_WARN_IF(!HTMLEditUtils::GetInsertionPointInInclusiveAncestor(
                        *nsGkAtoms::blockquote, pointToInsertBlockquoteElement,
                        &aEditingHost)
                        .IsSet())) {
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }

    // Make sure we can put a block here.
    // XXX Unfortunately, this calls
    // MaybeSplitAncestorsForInsertWithTransaction() then,
    // HTMLEditUtils::GetInsertionPointInInclusiveAncestor() is called again.
    Result<CreateElementResult, nsresult> createNewBlockquoteElementResult =
        InsertElementWithSplittingAncestorsWithTransaction(
            *nsGkAtoms::blockquote, pointToInsertBlockquoteElement,
            BRElementNextToSplitPoint::Keep, aEditingHost);
    if (MOZ_UNLIKELY(createNewBlockquoteElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
          "nsGkAtoms::blockquote) failed");
      return createNewBlockquoteElementResult.unwrapErr();
    }
    CreateElementResult unwrappedCreateNewBlockquoteElementResult =
        createNewBlockquoteElementResult.unwrap();
    unwrappedCreateNewBlockquoteElementResult.IgnoreCaretPointSuggestion();
    RefPtr<Element> newBlockquoteElement =
        unwrappedCreateNewBlockquoteElementResult.UnwrapNewNode();
    MOZ_ASSERT(newBlockquoteElement);
    // delete anything that was in the list of nodes
    // XXX We don't need to remove the nodes from the array for performance.
    for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
      // keep it alive.
      nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*content));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
    }
    aRanges.ClearSavedRanges();
    nsresult rv = aRanges.Collapse(EditorRawDOMPoint(newBlockquoteElement, 0u));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionToStartOf() failed");
    return rv;
  }

  RefPtr<Element> latestNewBlockElement;
  auto RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside =
      [&]() -> nsresult {
    MOZ_ASSERT(aRanges.HasSavedRanges());
    aRanges.RestoreFromSavedRanges();

    if (!latestNewBlockElement || !aRanges.IsCollapsed() ||
        aRanges.Ranges().IsEmpty()) {
      return NS_OK;
    }

    const auto firstRangeStartRawPoint =
        aRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
    if (MOZ_UNLIKELY(!firstRangeStartRawPoint.IsSet())) {
      return NS_OK;
    }
    Result<EditorRawDOMPoint, nsresult> pointInNewBlockElementOrError =
        HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
            EditorRawDOMPoint>(*latestNewBlockElement, firstRangeStartRawPoint);
    if (MOZ_UNLIKELY(pointInNewBlockElementOrError.isErr())) {
      NS_WARNING(
          "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() failed, "
          "but ignored");
      return NS_OK;
    }
    if (!pointInNewBlockElementOrError.inspect().IsSet()) {
      return NS_OK;
    }
    return aRanges.Collapse(pointInNewBlockElementOrError.unwrap());
  };

  // Ok, now go through all the nodes and put them in a blockquote,
  // or whatever is appropriate.  Wohoo!
  RefPtr<Element> subListElement, blockquoteElement, indentedListItemElement;
  for (OwningNonNull<nsIContent>& content : arrayOfContents) {
    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsSet())) {
      continue;
    }

    // Ignore all non-editable nodes.  Leave them be.
    // XXX We ignore non-editable nodes here, but not so in the above block.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML) ||
        !HTMLEditUtils::IsRemovableNode(content)) {
      continue;
    }

    // If the content has been moved to different place, ignore it.
    if (MOZ_UNLIKELY(!content->IsInclusiveDescendantOf(&aEditingHost))) {
      continue;
    }

    if (HTMLEditUtils::IsAnyListElement(atContent.GetContainer())) {
      const RefPtr<Element> oldSubListElement = subListElement;
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
      // keep it alive.
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          IndentListChildWithTransaction(&subListElement, atContent,
                                         MOZ_KnownLive(content), aEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING("HTMLEditor::IndentListChildWithTransaction() failed");
        return pointToPutCaretOrError.unwrapErr();
      }
      if (oldSubListElement != subListElement) {
        // New list element is created, so we should put caret into the new list
        // element.
        latestNewBlockElement = subListElement;
      }
      if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
      blockquoteElement = nullptr;
      continue;
    }

    // Not a list item, use blockquote?

    // if we are inside a list item, we don't want to blockquote, we want
    // to sublist the list item.  We may have several nodes listed in the
    // array of nodes to act on, that are in the same list item.  Since
    // we only want to indent that li once, we must keep track of the most
    // recent indented list item, and not indent it if we find another node
    // to act on that is still inside the same li.
    if (RefPtr<Element> listItem =
            HTMLEditUtils::GetClosestAncestorListItemElement(content,
                                                             &aEditingHost)) {
      if (indentedListItemElement == listItem) {
        // already indented this list item
        continue;
      }
      // check to see if subListElement is still appropriate.  Which it is if
      // content is still right after it in the same list.
      nsIContent* previousEditableSibling =
          subListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    *listItem, {WalkTreeOption::IgnoreNonEditableNode})
              : nullptr;
      if (!subListElement || (previousEditableSibling &&
                              previousEditableSibling != subListElement)) {
        EditorDOMPoint atListItem(listItem);
        if (NS_WARN_IF(!listItem)) {
          return NS_ERROR_FAILURE;
        }
        nsAtom* containerName =
            atListItem.GetContainer()->NodeInfo()->NameAtom();
        // Create a new nested list of correct type.
        Result<CreateElementResult, nsresult> createNewListElementResult =
            InsertElementWithSplittingAncestorsWithTransaction(
                MOZ_KnownLive(*containerName), atListItem,
                BRElementNextToSplitPoint::Keep, aEditingHost);
        if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
          NS_WARNING(nsPrintfCString("HTMLEditor::"
                                     "InsertElementWithSplittingAncestorsWithTr"
                                     "ansaction(%s) failed",
                                     nsAtomCString(containerName).get())
                         .get());
          return createNewListElementResult.unwrapErr();
        }
        CreateElementResult unwrappedCreateNewListElementResult =
            createNewListElementResult.unwrap();
        if (unwrappedCreateNewListElementResult.HasCaretPointSuggestion()) {
          pointToPutCaret =
              unwrappedCreateNewListElementResult.UnwrapCaretPoint();
        }
        MOZ_ASSERT(unwrappedCreateNewListElementResult.GetNewNode());
        subListElement = unwrappedCreateNewListElementResult.UnwrapNewNode();
      }

      Result<MoveNodeResult, nsresult> moveListItemElementResult =
          MoveNodeToEndWithTransaction(*listItem, *subListElement);
      if (MOZ_UNLIKELY(moveListItemElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveListItemElementResult.unwrapErr();
      }
      MoveNodeResult unwrappedMoveListItemElementResult =
          moveListItemElementResult.unwrap();
      if (unwrappedMoveListItemElementResult.HasCaretPointSuggestion()) {
        pointToPutCaret = unwrappedMoveListItemElementResult.UnwrapCaretPoint();
      }

      // Remember the list item element which we indented now for ignoring its
      // children to avoid using <blockquote> in it.
      indentedListItemElement = std::move(listItem);

      continue;
    }

    // need to make a blockquote to put things in if we haven't already,
    // or if this node doesn't go in blockquote we used earlier.
    // One reason it might not go in prio blockquote is if we are now
    // in a different table cell.
    if (blockquoteElement &&
        HTMLEditUtils::GetInclusiveAncestorAnyTableElement(
            *blockquoteElement) !=
            HTMLEditUtils::GetInclusiveAncestorAnyTableElement(content)) {
      blockquoteElement = nullptr;
    }

    if (!blockquoteElement) {
      // First, check that our element can contain a blockquote.
      if (!HTMLEditUtils::CanNodeContain(*atContent.GetContainer(),
                                         *nsGkAtoms::blockquote)) {
        // XXX This is odd, why do we stop indenting remaining content nodes?
        //     Perhaps, `continue` is better.
        nsresult rv =
            RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside();
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "RestoreSavedRangesAndCollapseInLatestBlockElement"
                             "IfOutside() failed");
        return rv;
      }

      Result<CreateElementResult, nsresult> createNewBlockquoteElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              *nsGkAtoms::blockquote, atContent,
              BRElementNextToSplitPoint::Keep, aEditingHost);
      if (MOZ_UNLIKELY(createNewBlockquoteElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "nsGkAtoms::blockquote) failed");
        return createNewBlockquoteElementResult.unwrapErr();
      }
      CreateElementResult unwrappedCreateNewBlockquoteElementResult =
          createNewBlockquoteElementResult.unwrap();
      if (unwrappedCreateNewBlockquoteElementResult.HasCaretPointSuggestion()) {
        pointToPutCaret =
            unwrappedCreateNewBlockquoteElementResult.UnwrapCaretPoint();
      }

      MOZ_ASSERT(unwrappedCreateNewBlockquoteElementResult.GetNewNode());
      blockquoteElement =
          unwrappedCreateNewBlockquoteElementResult.UnwrapNewNode();
      latestNewBlockElement = blockquoteElement;
    }

    // tuck the node into the end of the active blockquote
    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
    // keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeToEndWithTransaction(MOZ_KnownLive(content),
                                     *blockquoteElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.unwrapErr();
    }
    MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
    if (unwrappedMoveNodeResult.HasCaretPointSuggestion()) {
      pointToPutCaret = unwrappedMoveNodeResult.UnwrapCaretPoint();
    }
    subListElement = nullptr;
  }

  nsresult rv = RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "RestoreSavedRangesAndCollapseInLatestBlockElementIfOutside() failed");
  return rv;
}

Result<EditActionResult, nsresult> HTMLEditor::OutdentAsSubAction(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eOutdent, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Some selection containers are not content node, but ignored");
    return EditActionResult::IgnoredResult();
  }

  Result<EditActionResult, nsresult> result =
      HandleOutdentAtSelection(aEditingHost);
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING("HTMLEditor::HandleOutdentAtSelection() failed");
    return result;
  }
  if (result.inspect().Canceled()) {
    return result;
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Mutation event listener might have changed the selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (!SelectionRef().IsCollapsed()) {
    return result;
  }

  const auto caretPosition =
      EditorBase::GetFirstSelectionStartPoint<EditorDOMPoint>();
  Result<CreateLineBreakResult, nsresult> insertPaddingBRElementResultOrError =
      InsertPaddingBRElementIfInEmptyBlock(caretPosition, eNoStrip);
  if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(eNoStrip) failed");
    return insertPaddingBRElementResultOrError.propagateErr();
  }
  nsresult rv =
      insertPaddingBRElementResultOrError.unwrap().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                       "CaretPoint::SuggestCaretPointTo() failed, but ignored");
  return result;
}

Result<EditActionResult, nsresult> HTMLEditor::HandleOutdentAtSelection(
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsSelectionRangeContainerNotContent());

  // XXX Why do we do this only when there is only one selection range?
  if (!SelectionRef().IsCollapsed() && SelectionRef().RangeCount() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            SelectionRef().GetRangeAt(0u), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.propagateErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use Selection::SetStartAndEndInLimit() here.
    IgnoredErrorResult error;
    SelectionRef().SetBaseAndExtentInLimiter(
        extendedRange.inspect().StartRef().ToRawRangeBoundary(),
        extendedRange.inspect().EndRef().ToRawRangeBoundary(), error);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING("Selection::SetBaseAndExtentInLimiter() failed");
      return Err(error.StealNSResult());
    }
  }

  // HandleOutdentAtSelectionInternal() creates AutoSelectionRestorer.
  // Therefore, even if it returns NS_OK, the editor might have been destroyed
  // at restoring Selection.
  Result<SplitRangeOffFromNodeResult, nsresult> outdentResult =
      HandleOutdentAtSelectionInternal(aEditingHost);
  MOZ_ASSERT_IF(outdentResult.isOk(),
                !outdentResult.inspect().HasCaretPointSuggestion());
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (MOZ_UNLIKELY(outdentResult.isErr())) {
    NS_WARNING("HTMLEditor::HandleOutdentAtSelectionInternal() failed");
    return outdentResult.propagateErr();
  }
  SplitRangeOffFromNodeResult unwrappedOutdentResult = outdentResult.unwrap();

  // Make sure selection didn't stick to last piece of content in old bq (only
  // a problem for collapsed selections)
  if (!unwrappedOutdentResult.GetLeftContent() &&
      !unwrappedOutdentResult.GetRightContent()) {
    return EditActionResult::HandledResult();
  }

  if (!SelectionRef().IsCollapsed()) {
    return EditActionResult::HandledResult();
  }

  // Push selection past end of left element of last split indented element.
  if (unwrappedOutdentResult.GetLeftContent()) {
    const nsRange* firstRange = SelectionRef().GetRangeAt(0);
    if (NS_WARN_IF(!firstRange)) {
      return EditActionResult::HandledResult();
    }
    const RangeBoundary& atStartOfSelection = firstRange->StartRef();
    if (NS_WARN_IF(!atStartOfSelection.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (atStartOfSelection.GetContainer() ==
            unwrappedOutdentResult.GetLeftContent() ||
        EditorUtils::IsDescendantOf(*atStartOfSelection.GetContainer(),
                                    *unwrappedOutdentResult.GetLeftContent())) {
      // Selection is inside the left node - push it past it.
      EditorRawDOMPoint afterRememberedLeftBQ(
          EditorRawDOMPoint::After(*unwrappedOutdentResult.GetLeftContent()));
      NS_WARNING_ASSERTION(
          afterRememberedLeftBQ.IsSet(),
          "Failed to set after remembered left blockquote element");
      nsresult rv = CollapseSelectionTo(afterRememberedLeftBQ);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }
  }
  // And pull selection before beginning of right element of last split
  // indented element.
  if (unwrappedOutdentResult.GetRightContent()) {
    const nsRange* firstRange = SelectionRef().GetRangeAt(0);
    if (NS_WARN_IF(!firstRange)) {
      return EditActionResult::HandledResult();
    }
    const RangeBoundary& atStartOfSelection = firstRange->StartRef();
    if (NS_WARN_IF(!atStartOfSelection.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    if (atStartOfSelection.GetContainer() ==
            unwrappedOutdentResult.GetRightContent() ||
        EditorUtils::IsDescendantOf(
            *atStartOfSelection.GetContainer(),
            *unwrappedOutdentResult.GetRightContent())) {
      // Selection is inside the right element - push it before it.
      EditorRawDOMPoint atRememberedRightBQ(
          unwrappedOutdentResult.GetRightContent());
      nsresult rv = CollapseSelectionTo(atRememberedRightBQ);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }
  }
  return EditActionResult::HandledResult();
}

Result<SplitRangeOffFromNodeResult, nsresult>
HTMLEditor::HandleOutdentAtSelectionInternal(const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoSelectionRestorer restoreSelectionLater(this);

  bool useCSS = IsCSSEnabled();

  // Convert the selection ranges into "promoted" selection ranges: this
  // basically just expands the range to include the immediate block parent,
  // and then further expands to include any ancestors whose children are all
  // in the range
  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    AutoClonedSelectionRangeArray extendedSelectionRanges(SelectionRef());
    extendedSelectionRanges.ExtendRangesToWrapLines(
        EditSubAction::eOutdent, BlockInlineCheck::UseHTMLDefaultStyle,
        aEditingHost);
    nsresult rv = extendedSelectionRanges.CollectEditTargetNodes(
        *this, arrayOfContents, EditSubAction::eOutdent,
        AutoClonedRangeArray::CollectNonEditableNodes::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
          "eOutdent, CollectNonEditableNodes::Yes) failed");
      return Err(rv);
    }
    Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
        MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                           EditSubAction::eOutdent);
    if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
          "eOutdent) failed");
      return splitAtBRElementsResult.propagateErr();
    }
    if (AllowsTransactionsToChangeSelection() &&
        splitAtBRElementsResult.inspect().IsSet()) {
      nsresult rv = CollapseSelectionTo(splitAtBRElementsResult.inspect());
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
    }
  }

  nsCOMPtr<nsIContent> leftContentOfLastOutdented;
  nsCOMPtr<nsIContent> middleContentOfLastOutdented;
  nsCOMPtr<nsIContent> rightContentOfLastOutdented;
  RefPtr<Element> indentedParentElement;
  nsCOMPtr<nsIContent> firstContentToBeOutdented, lastContentToBeOutdented;
  BlockIndentedWith indentedParentIndentedWith = BlockIndentedWith::HTML;
  for (OwningNonNull<nsIContent>& content : arrayOfContents) {
    // Here's where we actually figure out what to do
    EditorDOMPoint atContent(content);
    if (!atContent.IsSet()) {
      continue;
    }

    // If it's a `<blockquote>`, remove it to outdent its children.
    if (content->IsHTMLElement(nsGkAtoms::blockquote)) {
      // If we've already found an ancestor block element indented, we need to
      // split it and remove the block element first.
      if (indentedParentElement) {
        NS_WARNING_ASSERTION(indentedParentElement == content,
                             "Indented parent element is not the <blockquote>");
        Result<SplitRangeOffFromNodeResult, nsresult> outdentResult =
            OutdentPartOfBlock(*indentedParentElement,
                               *firstContentToBeOutdented,
                               *lastContentToBeOutdented,
                               indentedParentIndentedWith, aEditingHost);
        if (MOZ_UNLIKELY(outdentResult.isErr())) {
          NS_WARNING("HTMLEditor::OutdentPartOfBlock() failed");
          return outdentResult;
        }
        SplitRangeOffFromNodeResult unwrappedOutdentResult =
            outdentResult.unwrap();
        unwrappedOutdentResult.IgnoreCaretPointSuggestion();
        leftContentOfLastOutdented = unwrappedOutdentResult.UnwrapLeftContent();
        middleContentOfLastOutdented =
            unwrappedOutdentResult.UnwrapMiddleContent();
        rightContentOfLastOutdented =
            unwrappedOutdentResult.UnwrapRightContent();
        indentedParentElement = nullptr;
        firstContentToBeOutdented = nullptr;
        lastContentToBeOutdented = nullptr;
        indentedParentIndentedWith = BlockIndentedWith::HTML;
      }
      Result<EditorDOMPoint, nsresult> unwrapBlockquoteElementResult =
          RemoveBlockContainerWithTransaction(
              MOZ_KnownLive(*content->AsElement()));
      if (MOZ_UNLIKELY(unwrapBlockquoteElementResult.isErr())) {
        NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
        return unwrapBlockquoteElementResult.propagateErr();
      }
      const EditorDOMPoint& pointToPutCaret =
          unwrapBlockquoteElementResult.inspect();
      if (AllowsTransactionsToChangeSelection() && pointToPutCaret.IsSet()) {
        nsresult rv = CollapseSelectionTo(pointToPutCaret);
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::CollapseSelectionTo() failed");
          return Err(rv);
        }
      }
      continue;
    }

    // If we're using CSS and the node is a block element, check its start
    // margin whether it's indented with CSS.
    if (useCSS && HTMLEditUtils::IsBlockElement(
                      content, BlockInlineCheck::UseHTMLDefaultStyle)) {
      nsStaticAtom& marginProperty =
          MarginPropertyAtomForIndent(MOZ_KnownLive(content));
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      nsAutoString value;
      DebugOnly<nsresult> rvIgnored =
          CSSEditUtils::GetSpecifiedProperty(content, marginProperty, value);
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "CSSEditUtils::GetSpecifiedProperty() failed, but ignored");
      float startMargin = 0;
      RefPtr<nsAtom> unit;
      CSSEditUtils::ParseLength(value, &startMargin, getter_AddRefs(unit));
      // If indented with CSS, we should decrease the start margin.
      if (startMargin > 0) {
        const Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            ChangeMarginStart(MOZ_KnownLive(*content->AsElement()),
                              ChangeMargin::Decrease, aEditingHost);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          if (NS_WARN_IF(pointToPutCaretOrError.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED)) {
            return Err(NS_ERROR_EDITOR_DESTROYED);
          }
          NS_WARNING(
              "HTMLEditor::ChangeMarginStart(ChangeMargin::Decrease) failed, "
              "but ignored");
        } else if (AllowsTransactionsToChangeSelection() &&
                   pointToPutCaretOrError.inspect().IsSet()) {
          nsresult rv = CollapseSelectionTo(pointToPutCaretOrError.inspect());
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::CollapseSelectionTo() failed");
            return Err(rv);
          }
        }
        continue;
      }
    }

    // If it's a list item, we should treat as that it "indents" its children.
    if (HTMLEditUtils::IsListItem(content)) {
      // If it is a list item, that means we are not outdenting whole list.
      // XXX I don't understand this sentence...  We may meet parent list
      //     element, no?
      if (indentedParentElement) {
        Result<SplitRangeOffFromNodeResult, nsresult> outdentResult =
            OutdentPartOfBlock(*indentedParentElement,
                               *firstContentToBeOutdented,
                               *lastContentToBeOutdented,
                               indentedParentIndentedWith, aEditingHost);
        if (MOZ_UNLIKELY(outdentResult.isErr())) {
          NS_WARNING("HTMLEditor::OutdentPartOfBlock() failed");
          return outdentResult;
        }
        SplitRangeOffFromNodeResult unwrappedOutdentResult =
            outdentResult.unwrap();
        unwrappedOutdentResult.IgnoreCaretPointSuggestion();
        leftContentOfLastOutdented = unwrappedOutdentResult.UnwrapLeftContent();
        middleContentOfLastOutdented =
            unwrappedOutdentResult.UnwrapMiddleContent();
        rightContentOfLastOutdented =
            unwrappedOutdentResult.UnwrapRightContent();
        indentedParentElement = nullptr;
        firstContentToBeOutdented = nullptr;
        lastContentToBeOutdented = nullptr;
        indentedParentIndentedWith = BlockIndentedWith::HTML;
      }
      // XXX `content` could become different element since
      //     `OutdentPartOfBlock()` may run mutation event listeners.
      nsresult rv = LiftUpListItemElement(MOZ_KnownLive(*content->AsElement()),
                                          LiftUpFromAllParentListElements::No);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::LiftUpListItemElement(LiftUpFromAllParentListElements:"
            ":No) failed");
        return Err(rv);
      }
      continue;
    }

    // If we've found an ancestor block element which indents its children
    // and the current node is NOT a descendant of it, we should remove it to
    // outdent its children.  Otherwise, i.e., current node is a descendant of
    // it, we meet new node which should be outdented when the indented parent
    // is removed.
    if (indentedParentElement) {
      if (EditorUtils::IsDescendantOf(*content, *indentedParentElement)) {
        // Extend the range to be outdented at removing the
        // indentedParentElement.
        lastContentToBeOutdented = content;
        continue;
      }
      Result<SplitRangeOffFromNodeResult, nsresult> outdentResult =
          OutdentPartOfBlock(*indentedParentElement, *firstContentToBeOutdented,
                             *lastContentToBeOutdented,
                             indentedParentIndentedWith, aEditingHost);
      if (MOZ_UNLIKELY(outdentResult.isErr())) {
        NS_WARNING("HTMLEditor::OutdentPartOfBlock() failed");
        return outdentResult;
      }
      SplitRangeOffFromNodeResult unwrappedOutdentResult =
          outdentResult.unwrap();
      unwrappedOutdentResult.IgnoreCaretPointSuggestion();
      leftContentOfLastOutdented = unwrappedOutdentResult.UnwrapLeftContent();
      middleContentOfLastOutdented =
          unwrappedOutdentResult.UnwrapMiddleContent();
      rightContentOfLastOutdented = unwrappedOutdentResult.UnwrapRightContent();
      indentedParentElement = nullptr;
      firstContentToBeOutdented = nullptr;
      lastContentToBeOutdented = nullptr;
      // curBlockIndentedWith = HTMLEditor::BlockIndentedWith::HTML;

      // Then, we need to look for next indentedParentElement.
    }

    indentedParentIndentedWith = BlockIndentedWith::HTML;
    for (nsCOMPtr<nsIContent> parentContent = content->GetParent();
         parentContent && !parentContent->IsHTMLElement(nsGkAtoms::body) &&
         parentContent != &aEditingHost &&
         (parentContent->IsHTMLElement(nsGkAtoms::table) ||
          !HTMLEditUtils::IsAnyTableElement(parentContent));
         parentContent = parentContent->GetParent()) {
      if (MOZ_UNLIKELY(!HTMLEditUtils::IsRemovableNode(*parentContent))) {
        continue;
      }
      // If we reach a `<blockquote>` ancestor, it should be split at next
      // time at least for outdenting current node.
      if (parentContent->IsHTMLElement(nsGkAtoms::blockquote)) {
        indentedParentElement = parentContent->AsElement();
        firstContentToBeOutdented = content;
        lastContentToBeOutdented = content;
        break;
      }

      if (!useCSS) {
        continue;
      }

      nsCOMPtr<nsINode> grandParentNode = parentContent->GetParentNode();
      nsStaticAtom& marginProperty =
          MarginPropertyAtomForIndent(MOZ_KnownLive(content));
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (NS_WARN_IF(grandParentNode != parentContent->GetParentNode())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      nsAutoString value;
      DebugOnly<nsresult> rvIgnored = CSSEditUtils::GetSpecifiedProperty(
          *parentContent, marginProperty, value);
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "CSSEditUtils::GetSpecifiedProperty() failed, but ignored");
      // XXX Now, editing host may become different element.  If so, shouldn't
      //     we stop this handling?
      float startMargin;
      RefPtr<nsAtom> unit;
      CSSEditUtils::ParseLength(value, &startMargin, getter_AddRefs(unit));
      // If we reach a block element which indents its children with start
      // margin, we should remove it at next time.
      if (startMargin > 0 &&
          !(HTMLEditUtils::IsAnyListElement(atContent.GetContainer()) &&
            HTMLEditUtils::IsAnyListElement(content))) {
        indentedParentElement = parentContent->AsElement();
        firstContentToBeOutdented = content;
        lastContentToBeOutdented = content;
        indentedParentIndentedWith = BlockIndentedWith::CSS;
        break;
      }
    }

    if (indentedParentElement) {
      continue;
    }

    // If we don't have any block elements which indents current node and
    // both current node and its parent are list element, remove current
    // node to move all its children to the parent list.
    // XXX This is buggy.  When both lists' item types are different,
    //     we create invalid tree.  E.g., `<ul>` may have `<dd>` as its
    //     list item element.
    if (HTMLEditUtils::IsAnyListElement(atContent.GetContainer())) {
      if (!HTMLEditUtils::IsAnyListElement(content)) {
        continue;
      }
      // Just unwrap this sublist
      Result<EditorDOMPoint, nsresult> unwrapSubListElementResult =
          RemoveBlockContainerWithTransaction(
              MOZ_KnownLive(*content->AsElement()));
      if (MOZ_UNLIKELY(unwrapSubListElementResult.isErr())) {
        NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
        return unwrapSubListElementResult.propagateErr();
      }
      const EditorDOMPoint& pointToPutCaret =
          unwrapSubListElementResult.inspect();
      if (!AllowsTransactionsToChangeSelection() || !pointToPutCaret.IsSet()) {
        continue;
      }
      nsresult rv = CollapseSelectionTo(pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
      continue;
    }

    // If current content is a list element but its parent is not a list
    // element, move children to where it is and remove it from the tree.
    if (HTMLEditUtils::IsAnyListElement(content)) {
      // XXX If mutation event listener appends new children forever, this
      //     becomes an infinite loop so that we should set limitation from
      //     first child count.
      for (nsCOMPtr<nsIContent> lastChildContent = content->GetLastChild();
           lastChildContent; lastChildContent = content->GetLastChild()) {
        if (HTMLEditUtils::IsListItem(lastChildContent)) {
          nsresult rv = LiftUpListItemElement(
              MOZ_KnownLive(*lastChildContent->AsElement()),
              LiftUpFromAllParentListElements::No);
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "HTMLEditor::LiftUpListItemElement("
                "LiftUpFromAllParentListElements::No) failed");
            return Err(rv);
          }
          continue;
        }

        if (HTMLEditUtils::IsAnyListElement(lastChildContent)) {
          // We have an embedded list, so move it out from under the parent
          // list. Be sure to put it after the parent list because this
          // loop iterates backwards through the parent's list of children.
          EditorDOMPoint afterCurrentList(EditorDOMPoint::After(atContent));
          NS_WARNING_ASSERTION(
              afterCurrentList.IsSet(),
              "Failed to set it to after current list element");
          Result<MoveNodeResult, nsresult> moveListElementResult =
              MoveNodeWithTransaction(*lastChildContent, afterCurrentList);
          if (MOZ_UNLIKELY(moveListElementResult.isErr())) {
            NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
            return moveListElementResult.propagateErr();
          }
          nsresult rv = moveListElementResult.inspect().SuggestCaretPointTo(
              *this, {SuggestCaret::OnlyIfHasSuggestion,
                      SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                      SuggestCaret::AndIgnoreTrivialError});
          if (NS_FAILED(rv)) {
            NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
            return Err(rv);
          }
          NS_WARNING_ASSERTION(
              rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
              "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
          continue;
        }

        // Delete any non-list items for now
        // XXX Chrome moves it from the list element.  We should follow it.
        nsresult rv = DeleteNodeWithTransaction(*lastChildContent);
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return Err(rv);
        }
      }
      // Delete the now-empty list
      Result<EditorDOMPoint, nsresult> unwrapListElementResult =
          RemoveBlockContainerWithTransaction(
              MOZ_KnownLive(*content->AsElement()));
      if (MOZ_UNLIKELY(unwrapListElementResult.isErr())) {
        NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
        return unwrapListElementResult.propagateErr();
      }
      const EditorDOMPoint& pointToPutCaret = unwrapListElementResult.inspect();
      if (!AllowsTransactionsToChangeSelection() || !pointToPutCaret.IsSet()) {
        continue;
      }
      nsresult rv = CollapseSelectionTo(pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
      continue;
    }

    if (useCSS) {
      if (RefPtr<Element> element = content->GetAsElementOrParentElement()) {
        const Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            ChangeMarginStart(*element, ChangeMargin::Decrease, aEditingHost);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          if (NS_WARN_IF(pointToPutCaretOrError.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED)) {
            return Err(NS_ERROR_EDITOR_DESTROYED);
          }
          NS_WARNING(
              "HTMLEditor::ChangeMarginStart(ChangeMargin::Decrease) failed, "
              "but ignored");
        } else if (AllowsTransactionsToChangeSelection() &&
                   pointToPutCaretOrError.inspect().IsSet()) {
          nsresult rv = CollapseSelectionTo(pointToPutCaretOrError.inspect());
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::CollapseSelectionTo() failed");
            return Err(rv);
          }
        }
      }
      continue;
    }
  }

  if (!indentedParentElement) {
    return SplitRangeOffFromNodeResult(leftContentOfLastOutdented,
                                       middleContentOfLastOutdented,
                                       rightContentOfLastOutdented);
  }

  // We have a <blockquote> we haven't finished handling.
  Result<SplitRangeOffFromNodeResult, nsresult> outdentResult =
      OutdentPartOfBlock(*indentedParentElement, *firstContentToBeOutdented,
                         *lastContentToBeOutdented, indentedParentIndentedWith,
                         aEditingHost);
  if (MOZ_UNLIKELY(outdentResult.isErr())) {
    NS_WARNING("HTMLEditor::OutdentPartOfBlock() failed");
    return outdentResult;
  }
  // We will restore selection soon.  Therefore, callers do not need to restore
  // the selection.
  SplitRangeOffFromNodeResult unwrappedOutdentResult = outdentResult.unwrap();
  unwrappedOutdentResult.ForgetCaretPointSuggestion();
  return unwrappedOutdentResult;
}

Result<SplitRangeOffFromNodeResult, nsresult>
HTMLEditor::RemoveBlockContainerElementWithTransactionBetween(
    Element& aBlockContainerElement, nsIContent& aStartOfRange,
    nsIContent& aEndOfRange, BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  EditorDOMPoint pointToPutCaret;
  Result<SplitRangeOffFromNodeResult, nsresult> splitResult =
      SplitRangeOffFromElement(aBlockContainerElement, aStartOfRange,
                               aEndOfRange);
  if (MOZ_UNLIKELY(splitResult.isErr())) {
    if (splitResult.inspectErr() == NS_ERROR_EDITOR_DESTROYED) {
      NS_WARNING("HTMLEditor::SplitRangeOffFromElement() failed");
      return splitResult;
    }
    NS_WARNING(
        "HTMLEditor::SplitRangeOffFromElement() failed, but might be ignored");
    return SplitRangeOffFromNodeResult(nullptr, nullptr, nullptr);
  }
  SplitRangeOffFromNodeResult unwrappedSplitResult = splitResult.unwrap();
  unwrappedSplitResult.MoveCaretPointTo(pointToPutCaret,
                                        {SuggestCaret::OnlyIfHasSuggestion});

  // Even if either split aBlockContainerElement or did not split it, we should
  // unwrap the right most element which is split from aBlockContainerElement
  // (or aBlockContainerElement itself if it was not split without errors).
  Element* rightmostElement =
      unwrappedSplitResult.GetRightmostContentAs<Element>();
  MOZ_ASSERT(rightmostElement);
  if (NS_WARN_IF(!rightmostElement)) {
    return Err(NS_ERROR_FAILURE);
  }

  {
    // MOZ_KnownLive(rightmostElement) because it's grabbed by
    // unwrappedSplitResult.
    Result<EditorDOMPoint, nsresult> unwrapBlockElementResult =
        RemoveBlockContainerWithTransaction(MOZ_KnownLive(*rightmostElement));
    if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
      return unwrapBlockElementResult.propagateErr();
    }
    if (unwrapBlockElementResult.inspect().IsSet()) {
      pointToPutCaret = unwrapBlockElementResult.unwrap();
    }
  }

  return SplitRangeOffFromNodeResult(
      unwrappedSplitResult.GetLeftContent(), nullptr,
      unwrappedSplitResult.GetRightContent(), std::move(pointToPutCaret));
}

Result<SplitRangeOffFromNodeResult, nsresult>
HTMLEditor::SplitRangeOffFromElement(Element& aElementToSplit,
                                     nsIContent& aStartOfMiddleElement,
                                     nsIContent& aEndOfMiddleElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // aStartOfMiddleElement and aEndOfMiddleElement must be exclusive
  // descendants of aElementToSplit.
  MOZ_ASSERT(
      EditorUtils::IsDescendantOf(aStartOfMiddleElement, aElementToSplit));
  MOZ_ASSERT(EditorUtils::IsDescendantOf(aEndOfMiddleElement, aElementToSplit));

  EditorDOMPoint pointToPutCaret;
  // Split at the start.
  Result<SplitNodeResult, nsresult> splitAtStartResult =
      SplitNodeDeepWithTransaction(aElementToSplit,
                                   EditorDOMPoint(&aStartOfMiddleElement),
                                   SplitAtEdges::eDoNotCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitAtStartResult.isErr())) {
    if (splitAtStartResult.inspectErr() == NS_ERROR_EDITOR_DESTROYED) {
      NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed (at left)");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING(
        "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
        "eDoNotCreateEmptyContainer) at start of middle element failed");
  } else {
    splitAtStartResult.inspect().CopyCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  }

  // Split at after the end
  auto atAfterEnd = EditorDOMPoint::After(aEndOfMiddleElement);
  Element* rightElement =
      splitAtStartResult.isOk() && splitAtStartResult.inspect().DidSplit()
          ? splitAtStartResult.inspect().GetNextContentAs<Element>()
          : &aElementToSplit;
  // MOZ_KnownLive(rightElement) because it's grabbed by splitAtStartResult or
  // aElementToSplit whose lifetime is guaranteed by the caller.
  Result<SplitNodeResult, nsresult> splitAtEndResult =
      SplitNodeDeepWithTransaction(MOZ_KnownLive(*rightElement), atAfterEnd,
                                   SplitAtEdges::eDoNotCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitAtEndResult.isErr())) {
    if (splitAtEndResult.inspectErr() == NS_ERROR_EDITOR_DESTROYED) {
      NS_WARNING(
          "HTMLEditor::SplitNodeDeepWithTransaction() failed (at right)");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING(
        "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
        "eDoNotCreateEmptyContainer) after end of middle element failed");
  } else {
    splitAtEndResult.inspect().CopyCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  }

  if (splitAtStartResult.isOk() && splitAtStartResult.inspect().DidSplit() &&
      splitAtEndResult.isOk() && splitAtEndResult.inspect().DidSplit()) {
    // Note that the middle node can be computed only with the latter split
    // result.
    return SplitRangeOffFromNodeResult(
        splitAtStartResult.inspect().GetPreviousContent(),
        splitAtEndResult.inspect().GetPreviousContent(),
        splitAtEndResult.inspect().GetNextContent(),
        std::move(pointToPutCaret));
  }
  if (splitAtStartResult.isOk() && splitAtStartResult.inspect().DidSplit()) {
    return SplitRangeOffFromNodeResult(
        splitAtStartResult.inspect().GetPreviousContent(),
        splitAtStartResult.inspect().GetNextContent(), nullptr,
        std::move(pointToPutCaret));
  }
  if (splitAtEndResult.isOk() && splitAtEndResult.inspect().DidSplit()) {
    return SplitRangeOffFromNodeResult(
        nullptr, splitAtEndResult.inspect().GetPreviousContent(),
        splitAtEndResult.inspect().GetNextContent(),
        std::move(pointToPutCaret));
  }
  return SplitRangeOffFromNodeResult(nullptr, &aElementToSplit, nullptr,
                                     std::move(pointToPutCaret));
}

Result<SplitRangeOffFromNodeResult, nsresult> HTMLEditor::OutdentPartOfBlock(
    Element& aBlockElement, nsIContent& aStartOfOutdent,
    nsIContent& aEndOfOutdent, BlockIndentedWith aBlockIndentedWith,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Result<SplitRangeOffFromNodeResult, nsresult> splitResult =
      SplitRangeOffFromElement(aBlockElement, aStartOfOutdent, aEndOfOutdent);
  if (MOZ_UNLIKELY(splitResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitRangeOffFromElement() failed");
    return splitResult;
  }

  SplitRangeOffFromNodeResult unwrappedSplitResult = splitResult.unwrap();
  Element* middleElement = unwrappedSplitResult.GetMiddleContentAs<Element>();
  if (MOZ_UNLIKELY(!middleElement)) {
    NS_WARNING(
        "HTMLEditor::SplitRangeOffFromElement() didn't return middle content");
    unwrappedSplitResult.IgnoreCaretPointSuggestion();
    return Err(NS_ERROR_FAILURE);
  }
  if (NS_WARN_IF(!HTMLEditUtils::IsRemovableNode(*middleElement))) {
    unwrappedSplitResult.IgnoreCaretPointSuggestion();
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  nsresult rv = unwrappedSplitResult.SuggestCaretPointTo(
      *this, {SuggestCaret::OnlyIfHasSuggestion,
              SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
              SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("SplitRangeOffFromNodeResult::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "SplitRangeOffFromNodeResult::SuggestCaretPointTo() "
                       "failed, but ignored");

  if (aBlockIndentedWith == BlockIndentedWith::HTML) {
    // MOZ_KnownLive(middleElement) because of grabbed by unwrappedSplitResult.
    Result<EditorDOMPoint, nsresult> unwrapBlockElementResult =
        RemoveBlockContainerWithTransaction(MOZ_KnownLive(*middleElement));
    if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
      return unwrapBlockElementResult.propagateErr();
    }
    const EditorDOMPoint& pointToPutCaret = unwrapBlockElementResult.inspect();
    if (AllowsTransactionsToChangeSelection() && pointToPutCaret.IsSet()) {
      nsresult rv = CollapseSelectionTo(pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::CollapseSelectionTo() failed");
        return Err(rv);
      }
    }
    return SplitRangeOffFromNodeResult(unwrappedSplitResult.GetLeftContent(),
                                       nullptr,
                                       unwrappedSplitResult.GetRightContent());
  }

  // MOZ_KnownLive(middleElement) because of grabbed by unwrappedSplitResult.
  Result<EditorDOMPoint, nsresult> pointToPutCaretOrError = ChangeMarginStart(
      MOZ_KnownLive(*middleElement), ChangeMargin::Decrease, aEditingHost);
  if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
    NS_WARNING("HTMLEditor::ChangeMarginStart(ChangeMargin::Decrease) failed");
    return pointToPutCaretOrError.propagateErr();
  }
  if (AllowsTransactionsToChangeSelection() &&
      pointToPutCaretOrError.inspect().IsSet()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaretOrError.inspect());
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      return Err(rv);
    }
  }
  return unwrappedSplitResult;
}

Result<CreateElementResult, nsresult> HTMLEditor::ChangeListElementType(
    Element& aListElement, nsAtom& aNewListTag, nsAtom& aNewListItemTag) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  EditorDOMPoint pointToPutCaret;

  AutoTArray<OwningNonNull<nsIContent>, 32> listElementChildren;
  HTMLEditUtils::CollectAllChildren(aListElement, listElementChildren);

  for (const OwningNonNull<nsIContent>& childContent : listElementChildren) {
    if (!childContent->IsElement()) {
      continue;
    }
    Element* childElement = childContent->AsElement();
    if (HTMLEditUtils::IsListItem(childElement) &&
        !childContent->IsHTMLElement(&aNewListItemTag)) {
      // MOZ_KnownLive(childElement) because its lifetime is guaranteed by
      // listElementChildren.
      Result<CreateElementResult, nsresult>
          replaceWithNewListItemElementResult =
              ReplaceContainerAndCloneAttributesWithTransaction(
                  MOZ_KnownLive(*childElement), aNewListItemTag);
      if (MOZ_UNLIKELY(replaceWithNewListItemElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::ReplaceContainerAndCloneAttributesWithTransaction() "
            "failed");
        return replaceWithNewListItemElementResult;
      }
      CreateElementResult unwrappedReplaceWithNewListItemElementResult =
          replaceWithNewListItemElementResult.unwrap();
      unwrappedReplaceWithNewListItemElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      continue;
    }
    if (HTMLEditUtils::IsAnyListElement(childElement) &&
        !childElement->IsHTMLElement(&aNewListTag)) {
      // XXX List elements shouldn't have other list elements as their
      //     child.  Why do we handle such invalid tree?
      //     -> Maybe, for bug 525888.
      // MOZ_KnownLive(childElement) because its lifetime is guaranteed by
      // listElementChildren.
      Result<CreateElementResult, nsresult> convertListTypeResult =
          ChangeListElementType(MOZ_KnownLive(*childElement), aNewListTag,
                                aNewListItemTag);
      if (MOZ_UNLIKELY(convertListTypeResult.isErr())) {
        NS_WARNING("HTMLEditor::ChangeListElementType() failed");
        return convertListTypeResult;
      }
      CreateElementResult unwrappedConvertListTypeResult =
          convertListTypeResult.unwrap();
      unwrappedConvertListTypeResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      continue;
    }
  }

  if (aListElement.IsHTMLElement(&aNewListTag)) {
    return CreateElementResult(&aListElement, std::move(pointToPutCaret));
  }

  // XXX If we replace the list element, shouldn't we create it first and then,
  //     move children into it before inserting the new list element into the
  //     DOM tree? Then, we could reduce the cost of dispatching DOM mutation
  //     events.
  Result<CreateElementResult, nsresult> replaceWithNewListElementResult =
      ReplaceContainerWithTransaction(aListElement, aNewListTag);
  if (MOZ_UNLIKELY(replaceWithNewListElementResult.isErr())) {
    NS_WARNING("HTMLEditor::ReplaceContainerWithTransaction() failed");
    return replaceWithNewListElementResult;
  }
  CreateElementResult unwrappedReplaceWithNewListElementResult =
      replaceWithNewListElementResult.unwrap();
  unwrappedReplaceWithNewListElementResult.MoveCaretPointTo(
      pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  return CreateElementResult(
      unwrappedReplaceWithNewListElementResult.UnwrapNewNode(),
      std::move(pointToPutCaret));
}

Result<EditorDOMPoint, nsresult> HTMLEditor::CreateStyleForInsertText(
    const EditorDOMPoint& aPointToInsertText, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsertText.IsSetAndValid());
  MOZ_ASSERT(mPendingStylesToApplyToNewContent);

  const RefPtr<Element> documentRootElement = GetDocument()->GetRootElement();
  if (NS_WARN_IF(!documentRootElement)) {
    return Err(NS_ERROR_FAILURE);
  }

  // process clearing any styles first
  UniquePtr<PendingStyle> pendingStyle =
      mPendingStylesToApplyToNewContent->TakeClearingStyle();

  EditorDOMPoint pointToPutCaret(aPointToInsertText);
  {
    // Transactions may set selection, but we will set selection if necessary.
    AutoTransactionsConserveSelection dontChangeMySelection(*this);

    while (pendingStyle &&
           pointToPutCaret.GetContainer() != documentRootElement) {
      // MOZ_KnownLive because we own pendingStyle which guarantees the lifetime
      // of its members.
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          ClearStyleAt(pointToPutCaret, pendingStyle->ToInlineStyle(),
                       pendingStyle->GetSpecifiedStyle(), aEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING("HTMLEditor::ClearStyleAt() failed");
        return pointToPutCaretOrError;
      }
      pointToPutCaret = pointToPutCaretOrError.unwrap();
      if (NS_WARN_IF(!pointToPutCaret.IsSetAndValid())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      pendingStyle = mPendingStylesToApplyToNewContent->TakeClearingStyle();
    }
  }

  // then process setting any styles
  const int32_t relFontSize =
      mPendingStylesToApplyToNewContent->TakeRelativeFontSize();
  AutoTArray<EditorInlineStyleAndValue, 32> stylesToSet;
  mPendingStylesToApplyToNewContent->TakeAllPreservedStyles(stylesToSet);
  if (stylesToSet.IsEmpty() && !relFontSize) {
    return pointToPutCaret;
  }

  // We're in chrome, e.g., the email composer of Thunderbird, and there is
  // relative font size changes, we need to keep using legacy path until we port
  // IncrementOrDecrementFontSizeAsSubAction() to work with
  // AutoInlineStyleSetter.
  if (relFontSize) {
    // we have at least one style to add; make a new text node to insert style
    // nodes above.
    EditorDOMPoint pointToInsertTextNode(pointToPutCaret);
    if (pointToInsertTextNode.IsInTextNode()) {
      // if we are in a text node, split it
      Result<SplitNodeResult, nsresult> splitTextNodeResult =
          SplitNodeDeepWithTransaction(
              MOZ_KnownLive(*pointToInsertTextNode.ContainerAs<Text>()),
              pointToInsertTextNode,
              SplitAtEdges::eAllowToCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitTextNodeResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
            "eAllowToCreateEmptyContainer) failed");
        return splitTextNodeResult.propagateErr();
      }
      SplitNodeResult unwrappedSplitTextNodeResult =
          splitTextNodeResult.unwrap();
      unwrappedSplitTextNodeResult.MoveCaretPointTo(
          pointToPutCaret, *this,
          {SuggestCaret::OnlyIfHasSuggestion,
           SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
      pointToInsertTextNode =
          unwrappedSplitTextNodeResult.AtSplitPoint<EditorDOMPoint>();
    }
    if (!pointToInsertTextNode.IsInContentNode() ||
        !HTMLEditUtils::IsContainerNode(
            *pointToInsertTextNode.ContainerAs<nsIContent>())) {
      return pointToPutCaret;
    }
    RefPtr<Text> newEmptyTextNode = CreateTextNode(u""_ns);
    if (!newEmptyTextNode) {
      NS_WARNING("EditorBase::CreateTextNode() failed");
      return Err(NS_ERROR_FAILURE);
    }
    Result<CreateTextResult, nsresult> insertNewTextNodeResult =
        InsertNodeWithTransaction<Text>(*newEmptyTextNode,
                                        pointToInsertTextNode);
    if (MOZ_UNLIKELY(insertNewTextNodeResult.isErr())) {
      NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
      return insertNewTextNodeResult.propagateErr();
    }
    insertNewTextNodeResult.inspect().IgnoreCaretPointSuggestion();
    pointToPutCaret.Set(newEmptyTextNode, 0u);

    // FIXME: If the stylesToSet have background-color style, it may
    // be applied shorter because outer <span> element height is not
    // computed with inner element's height.
    HTMLEditor::FontSize incrementOrDecrement =
        relFontSize > 0 ? HTMLEditor::FontSize::incr
                        : HTMLEditor::FontSize::decr;
    for ([[maybe_unused]] uint32_t j : IntegerRange(Abs(relFontSize))) {
      Result<CreateElementResult, nsresult> wrapTextInBigOrSmallElementResult =
          SetFontSizeOnTextNode(*newEmptyTextNode, 0, UINT32_MAX,
                                incrementOrDecrement);
      if (MOZ_UNLIKELY(wrapTextInBigOrSmallElementResult.isErr())) {
        NS_WARNING("HTMLEditor::SetFontSizeOnTextNode() failed");
        return wrapTextInBigOrSmallElementResult.propagateErr();
      }
      // We don't need to update here because we'll suggest caret position
      // which is computed above.
      MOZ_ASSERT(pointToPutCaret.IsSet());
      wrapTextInBigOrSmallElementResult.inspect().IgnoreCaretPointSuggestion();
    }

    for (const EditorInlineStyleAndValue& styleToSet : stylesToSet) {
      AutoInlineStyleSetter inlineStyleSetter(styleToSet);
      // MOZ_KnownLive(...ContainerAs<nsIContent>()) because pointToPutCaret
      // grabs the result.
      Result<CaretPoint, nsresult> setStyleResult =
          inlineStyleSetter.ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(
              *this, MOZ_KnownLive(*pointToPutCaret.ContainerAs<nsIContent>()));
      if (MOZ_UNLIKELY(setStyleResult.isErr())) {
        NS_WARNING("HTMLEditor::SetInlinePropertyOnNode() failed");
        return setStyleResult.propagateErr();
      }
      // We don't need to update here because we'll suggest caret position which
      // is computed above.
      MOZ_ASSERT(pointToPutCaret.IsSet());
      setStyleResult.unwrap().IgnoreCaretPointSuggestion();
    }
    return pointToPutCaret;
  }

  // If we have preserved commands except relative font style changes, we can
  // use inline style setting code which reuse ancestors better.
  AutoClonedRangeArray ranges(pointToPutCaret);
  if (MOZ_UNLIKELY(ranges.Ranges().IsEmpty())) {
    NS_WARNING("AutoClonedRangeArray::AutoClonedRangeArray() failed");
    return Err(NS_ERROR_FAILURE);
  }
  nsresult rv = SetInlinePropertiesAroundRanges(ranges, stylesToSet);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetInlinePropertiesAroundRanges() failed");
    return Err(rv);
  }
  if (NS_WARN_IF(ranges.Ranges().IsEmpty())) {
    return Err(NS_ERROR_FAILURE);
  }
  // Now `ranges` selects new styled contents and the range may not be
  // collapsed.  We should use the deepest editable start point of the range
  // to insert text.
  nsINode* container = ranges.FirstRangeRef()->GetStartContainer();
  if (MOZ_UNLIKELY(!container->IsContent())) {
    container = ranges.FirstRangeRef()->GetChildAtStartOffset();
    if (MOZ_UNLIKELY(!container)) {
      NS_WARNING("How did we get lost insertion point?");
      return Err(NS_ERROR_FAILURE);
    }
  }
  pointToPutCaret =
      HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
          *container->AsContent());
  if (NS_WARN_IF(!pointToPutCaret.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }
  return pointToPutCaret;
}

Result<EditActionResult, nsresult> HTMLEditor::AlignAsSubAction(
    const nsAString& aAlignType, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSetOrClearAlignment, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Some selection containers are not content node, but ignored");
    return EditActionResult::IgnoredResult();
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Mutation event listener might have changed the selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());

  // XXX Why do we do this only when there is only one selection range?
  if (!selectionRanges.IsCollapsed() &&
      selectionRanges.Ranges().Length() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            selectionRanges.FirstRangeRef(), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.propagateErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use etStartAndEnd() here.
    nsresult rv = selectionRanges.SetBaseAndExtent(
        extendedRange.inspect().StartRef(), extendedRange.inspect().EndRef());
    if (NS_FAILED(rv)) {
      NS_WARNING("Selection::SetBaseAndExtentInLimiter() failed");
      return Err(rv);
    }
  }

  rv = AlignContentsAtRanges(selectionRanges, aAlignType, aEditingHost);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::AlignContentsAtSelection() failed");
    return Err(rv);
  }

  if (selectionRanges.IsCollapsed()) {
    // FIXME: If we get rid of the legacy mutation events, we should be able to
    // just insert a line break without empty check.
    Result<CreateLineBreakResult, nsresult>
        insertPaddingBRElementResultOrError =
            InsertPaddingBRElementIfInEmptyBlock(
                selectionRanges.GetFirstRangeStartPoint<EditorDOMPoint>(),
                eNoStrip);
    if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(eNoStrip) failed");
      return insertPaddingBRElementResultOrError.propagateErr();
    }
    EditorDOMPoint pointToPutCaret;
    insertPaddingBRElementResultOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (pointToPutCaret.IsSet()) {
      nsresult rv = selectionRanges.Collapse(pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING("AutoClonedRangeArray::Collapse() failed");
        return Err(rv);
      }
    }
  }

  rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_FAILED(rv)) {
    NS_WARNING("AutoClonedRangeArray::ApplyTo() failed");
    return Err(rv);
  }

  if (MOZ_UNLIKELY(IsSelectionRangeContainerNotContent())) {
    NS_WARNING("Mutation event listener might have changed the selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::AlignContentsAtRanges(
    AutoClonedSelectionRangeArray& aRanges, const nsAString& aAlignType,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(aRanges.IsInContent());

  if (NS_WARN_IF(!aRanges.SaveAndTrackRanges(*this))) {
    return NS_ERROR_FAILURE;
  }

  EditorDOMPoint pointToPutCaret;

  // Convert the selection ranges into "promoted" selection ranges: This
  // basically just expands the range to include the immediate block parent,
  // and then further expands to include any ancestors whose children are all
  // in the range
  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    AutoClonedSelectionRangeArray extendedRanges(aRanges);
    extendedRanges.ExtendRangesToWrapLines(
        EditSubAction::eSetOrClearAlignment,
        BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
    Result<EditorDOMPoint, nsresult> splitResult =
        extendedRanges
            .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
                *this, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
    if (MOZ_UNLIKELY(splitResult.isErr())) {
      NS_WARNING(
          "AutoClonedRangeArray::"
          "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() "
          "failed");
      return splitResult.unwrapErr();
    }
    if (splitResult.inspect().IsSet()) {
      pointToPutCaret = splitResult.unwrap();
    }
    nsresult rv = extendedRanges.CollectEditTargetNodes(
        *this, arrayOfContents, EditSubAction::eSetOrClearAlignment,
        AutoClonedRangeArray::CollectNonEditableNodes::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
          "eSetOrClearAlignment, CollectNonEditableNodes::Yes) failed");
      return rv;
    }
  }

  Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
      MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                         EditSubAction::eSetOrClearAlignment);
  if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
        "eSetOrClearAlignment) failed");
    return splitAtBRElementsResult.inspectErr();
  }
  if (splitAtBRElementsResult.inspect().IsSet()) {
    pointToPutCaret = splitAtBRElementsResult.unwrap();
  }

  // If we don't have any nodes, or we have only a single br, then we are
  // creating an empty alignment div.  We have to do some different things for
  // these.
  bool createEmptyDivElement = arrayOfContents.IsEmpty();
  if (arrayOfContents.Length() == 1) {
    OwningNonNull<nsIContent>& content = arrayOfContents[0];

    if (HTMLEditUtils::SupportsAlignAttr(content) &&
        HTMLEditUtils::IsBlockElement(content,
                                      BlockInlineCheck::UseHTMLDefaultStyle)) {
      // The node is a table element, an hr, a paragraph, a div or a section
      // header; in HTML 4, it can directly carry the ALIGN attribute and we
      // don't need to make a div! If we are in CSS mode, all the work is done
      // in SetBlockElementAlign().
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          SetBlockElementAlign(MOZ_KnownLive(*content->AsElement()), aAlignType,
                               EditTarget::OnlyDescendantsExceptTable);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING("HTMLEditor::SetBlockElementAlign() failed");
        return pointToPutCaretOrError.unwrapErr();
      }
      if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
    }

    if (content->IsHTMLElement(nsGkAtoms::br)) {
      // The special case createEmptyDivElement code (below) that consumes
      // `<br>` elements can cause tables to split if the start node of the
      // selection is not in a table cell or caption, for example parent is a
      // `<tr>`.  Avoid this unnecessary splitting if possible by leaving
      // createEmptyDivElement false so that we fall through to the normal case
      // alignment code.
      //
      // XXX: It seems a little error prone for the createEmptyDivElement
      //      special case code to assume that the start node of the selection
      //      is the parent of the single node in the arrayOfContents, as the
      //      paragraph above points out. Do we rely on the selection start
      //      node because of the fact that arrayOfContents can be empty?  We
      //      should probably revisit this issue. - kin

      const EditorDOMPoint firstRangeStartPoint =
          pointToPutCaret.IsSet()
              ? pointToPutCaret
              : aRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
      if (NS_WARN_IF(!firstRangeStartPoint.IsSet())) {
        return NS_ERROR_FAILURE;
      }
      nsINode* parent = firstRangeStartPoint.GetContainer();
      createEmptyDivElement = !HTMLEditUtils::IsAnyTableElement(parent) ||
                              HTMLEditUtils::IsTableCellOrCaption(*parent);
    }
  }

  if (createEmptyDivElement) {
    if (MOZ_UNLIKELY(!pointToPutCaret.IsSet() && !aRanges.IsInContent())) {
      NS_WARNING("Mutation event listener might have changed the selection");
      return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
    }
    const EditorDOMPoint pointToInsertDivElement =
        pointToPutCaret.IsSet()
            ? pointToPutCaret
            : aRanges.GetFirstRangeStartPoint<EditorDOMPoint>();
    Result<CreateElementResult, nsresult> insertNewDivElementResult =
        InsertDivElementToAlignContents(pointToInsertDivElement, aAlignType,
                                        aEditingHost);
    if (insertNewDivElementResult.isErr()) {
      NS_WARNING("HTMLEditor::InsertDivElementToAlignContents() failed");
      return insertNewDivElementResult.unwrapErr();
    }
    CreateElementResult unwrappedInsertNewDivElementResult =
        insertNewDivElementResult.unwrap();
    aRanges.ClearSavedRanges();
    EditorDOMPoint pointToPutCaret =
        unwrappedInsertNewDivElementResult.UnwrapCaretPoint();
    nsresult rv = aRanges.Collapse(pointToPutCaret);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "AutoClonedRangeArray::Collapse() failed");
    return rv;
  }

  Result<CreateElementResult, nsresult> maybeCreateDivElementResult =
      AlignNodesAndDescendants(arrayOfContents, aAlignType, aEditingHost);
  if (MOZ_UNLIKELY(maybeCreateDivElementResult.isErr())) {
    NS_WARNING("HTMLEditor::AlignNodesAndDescendants() failed");
    return maybeCreateDivElementResult.unwrapErr();
  }
  maybeCreateDivElementResult.inspect().IgnoreCaretPointSuggestion();

  MOZ_ASSERT(aRanges.HasSavedRanges());
  aRanges.RestoreFromSavedRanges();
  // If restored range is collapsed outside the latest cased <div> element,
  // we should move caret into the <div>.
  if (maybeCreateDivElementResult.inspect().GetNewNode() &&
      aRanges.IsCollapsed() && !aRanges.Ranges().IsEmpty()) {
    const auto firstRangeStartRawPoint =
        aRanges.GetFirstRangeStartPoint<EditorRawDOMPoint>();
    if (MOZ_LIKELY(firstRangeStartRawPoint.IsSet())) {
      Result<EditorRawDOMPoint, nsresult> pointInNewDivOrError =
          HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
              EditorRawDOMPoint>(
              *maybeCreateDivElementResult.inspect().GetNewNode(),
              firstRangeStartRawPoint);
      if (MOZ_UNLIKELY(pointInNewDivOrError.isErr())) {
        NS_WARNING(
            "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() failed, "
            "but ignored");
      } else if (pointInNewDivOrError.inspect().IsSet()) {
        nsresult rv = aRanges.Collapse(pointInNewDivOrError.unwrap());
        if (NS_FAILED(rv)) {
          NS_WARNING("AutoClonedRangeArray::Collapse() failed");
          return rv;
        }
      }
    }
  }
  return NS_OK;
}

Result<CreateElementResult, nsresult>
HTMLEditor::InsertDivElementToAlignContents(
    const EditorDOMPoint& aPointToInsert, const nsAString& aAlignType,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());
  MOZ_ASSERT(!IsSelectionRangeContainerNotContent());
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  Result<CreateElementResult, nsresult> createNewDivElementResult =
      InsertElementWithSplittingAncestorsWithTransaction(
          *nsGkAtoms::div, aPointToInsert, BRElementNextToSplitPoint::Delete,
          aEditingHost);
  if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
        "nsGkAtoms::div, BRElementNextToSplitPoint::Delete) failed");
    return createNewDivElementResult;
  }
  CreateElementResult unwrappedCreateNewDivElementResult =
      createNewDivElementResult.unwrap();
  // We'll suggest start of the new <div>, so we don't need the suggested
  // position.
  unwrappedCreateNewDivElementResult.IgnoreCaretPointSuggestion();

  MOZ_ASSERT(unwrappedCreateNewDivElementResult.GetNewNode());
  RefPtr<Element> newDivElement =
      unwrappedCreateNewDivElementResult.UnwrapNewNode();
  // Set up the alignment on the div, using HTML or CSS
  Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
      SetBlockElementAlign(*newDivElement, aAlignType,
                           EditTarget::OnlyDescendantsExceptTable);
  if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::SetBlockElementAlign(EditTarget::"
        "OnlyDescendantsExceptTable) failed");
    return pointToPutCaretOrError.propagateErr();
  }
  // We don't need the new suggested position too.

  // Put in a padding <br> element for empty last line so that it won't get
  // deleted.
  {
    Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
        InsertPaddingBRElementForEmptyLastLineWithTransaction(
            EditorDOMPoint(newDivElement, 0u));
    if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementForEmptyLastLineWithTransaction() "
          "failed");
      return insertPaddingBRElementResult;
    }
    insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
  }

  return CreateElementResult(std::move(newDivElement),
                             EditorDOMPoint(newDivElement, 0u));
}

Result<CreateElementResult, nsresult> HTMLEditor::AlignNodesAndDescendants(
    nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    const nsAString& aAlignType, const Element& aEditingHost) {
  // Detect all the transitions in the array, where a transition means that
  // adjacent nodes in the array don't have the same parent.
  AutoTArray<bool, 64> transitionList;
  HTMLEditor::MakeTransitionList(aArrayOfContents, transitionList);

  RefPtr<Element> latestCreatedDivElement;
  EditorDOMPoint pointToPutCaret;

  // Okay, now go through all the nodes and give them an align attrib or put
  // them in a div, or whatever is appropriate.  Woohoo!

  RefPtr<Element> createdDivElement;
  const bool useCSS = IsCSSEnabled();
  int32_t indexOfTransitionList = -1;
  for (OwningNonNull<nsIContent>& content : aArrayOfContents) {
    ++indexOfTransitionList;

    // Ignore all non-editable nodes.  Leave them be.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    // The node is a table element, an hr, a paragraph, a div or a section
    // header; in HTML 4, it can directly carry the ALIGN attribute and we
    // don't need to nest it, just set the alignment.  In CSS, assign the
    // corresponding CSS styles in SetBlockElementAlign().
    if (HTMLEditUtils::SupportsAlignAttr(content)) {
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          SetBlockElementAlign(MOZ_KnownLive(*content->AsElement()), aAlignType,
                               EditTarget::NodeAndDescendantsExceptTable);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::SetBlockElementAlign(EditTarget::"
            "NodeAndDescendantsExceptTable) failed");
        return pointToPutCaretOrError.propagateErr();
      }
      if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
      // Clear out createdDivElement so that we don't put nodes after this one
      // into it
      createdDivElement = nullptr;
      continue;
    }

    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsSet())) {
      continue;
    }

    // Skip insignificant formatting text nodes to prevent unnecessary
    // structure splitting!
    if (content->IsText() &&
        ((HTMLEditUtils::IsAnyTableElement(atContent.GetContainer()) &&
          !HTMLEditUtils::IsTableCellOrCaption(*atContent.GetContainer())) ||
         HTMLEditUtils::IsAnyListElement(atContent.GetContainer()) ||
         HTMLEditUtils::IsEmptyNode(
             *content,
             {EmptyCheckOption::TreatSingleBRElementAsVisible,
              EmptyCheckOption::TreatNonEditableContentAsInvisible}))) {
      continue;
    }

    // If it's a list item, or a list inside a list, forget any "current" div,
    // and instead put divs inside the appropriate block (td, li, etc.)
    if (HTMLEditUtils::IsListItem(content) ||
        HTMLEditUtils::IsAnyListElement(content)) {
      Element* listOrListItemElement = content->AsElement();
      {
        AutoEditorDOMPointOffsetInvalidator lockChild(atContent);
        // MOZ_KnownLive(*listOrListItemElement): An element of aArrayOfContents
        // which is array of OwningNonNull.
        Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            RemoveAlignFromDescendants(MOZ_KnownLive(*listOrListItemElement),
                                       aAlignType,
                                       EditTarget::OnlyDescendantsExceptTable);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::RemoveAlignFromDescendants(EditTarget::"
              "OnlyDescendantsExceptTable) failed");
          return pointToPutCaretOrError.propagateErr();
        }
        if (pointToPutCaretOrError.inspect().IsSet()) {
          pointToPutCaret = pointToPutCaretOrError.unwrap();
        }
      }
      if (NS_WARN_IF(!atContent.IsSetAndValid())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }

      if (useCSS) {
        nsStyledElement* styledListOrListItemElement =
            nsStyledElement::FromNode(listOrListItemElement);
        if (styledListOrListItemElement &&
            EditorElementStyle::Align().IsCSSSettable(
                *styledListOrListItemElement)) {
          // MOZ_KnownLive(*styledListOrListItemElement): An element of
          // aArrayOfContents which is array of OwningNonNull.
          Result<size_t, nsresult> result =
              CSSEditUtils::SetCSSEquivalentToStyle(
                  WithTransaction::Yes, *this,
                  MOZ_KnownLive(*styledListOrListItemElement),
                  EditorElementStyle::Align(), &aAlignType);
          if (MOZ_UNLIKELY(result.isErr())) {
            if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
              return result.propagateErr();
            }
            NS_WARNING(
                "CSSEditUtils::SetCSSEquivalentToStyle(EditorElementStyle::"
                "Align()) failed, but ignored");
          }
        }
        createdDivElement = nullptr;
        continue;
      }

      if (HTMLEditUtils::IsAnyListElement(atContent.GetContainer())) {
        // If we don't use CSS, add a content to list element: they have to
        // be inside another list, i.e., >= second level of nesting.
        // XXX AlignContentsInAllTableCellsAndListItems() handles only list
        //     item elements and table cells.  Is it intentional?  Why don't
        //     we need to align contents in other type blocks?
        // MOZ_KnownLive(*listOrListItemElement): An element of aArrayOfContents
        // which is array of OwningNonNull.
        Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            AlignContentsInAllTableCellsAndListItems(
                MOZ_KnownLive(*listOrListItemElement), aAlignType);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::AlignContentsInAllTableCellsAndListItems() failed");
          return pointToPutCaretOrError.propagateErr();
        }
        if (pointToPutCaretOrError.inspect().IsSet()) {
          pointToPutCaret = pointToPutCaretOrError.unwrap();
        }
        createdDivElement = nullptr;
        continue;
      }

      // Clear out createdDivElement so that we don't put nodes after this one
      // into it
    }

    // Need to make a div to put things in if we haven't already, or if this
    // node doesn't go in div we used earlier.
    if (!createdDivElement || transitionList[indexOfTransitionList]) {
      // First, check that our element can contain a div.
      if (!HTMLEditUtils::CanNodeContain(*atContent.GetContainer(),
                                         *nsGkAtoms::div)) {
        // XXX Why do we return "OK" here rather than returning error or
        //     doing continue?
        return latestCreatedDivElement
                   ? CreateElementResult(std::move(latestCreatedDivElement),
                                         std::move(pointToPutCaret))
                   : CreateElementResult::NotHandled(
                         std::move(pointToPutCaret));
      }

      Result<CreateElementResult, nsresult> createNewDivElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              *nsGkAtoms::div, atContent, BRElementNextToSplitPoint::Keep,
              aEditingHost);
      if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "nsGkAtoms::div) failed");
        return createNewDivElementResult;
      }
      CreateElementResult unwrappedCreateNewDivElementResult =
          createNewDivElementResult.unwrap();
      if (unwrappedCreateNewDivElementResult.HasCaretPointSuggestion()) {
        pointToPutCaret = unwrappedCreateNewDivElementResult.UnwrapCaretPoint();
      }

      MOZ_ASSERT(unwrappedCreateNewDivElementResult.GetNewNode());
      createdDivElement = unwrappedCreateNewDivElementResult.UnwrapNewNode();
      // Set up the alignment on the div
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          SetBlockElementAlign(*createdDivElement, aAlignType,
                               EditTarget::OnlyDescendantsExceptTable);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        if (NS_WARN_IF(pointToPutCaretOrError.inspectErr() ==
                       NS_ERROR_EDITOR_DESTROYED)) {
          return pointToPutCaretOrError.propagateErr();
        }
        NS_WARNING(
            "HTMLEditor::SetBlockElementAlign(EditTarget::"
            "OnlyDescendantsExceptTable) failed, but ignored");
      } else if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
      latestCreatedDivElement = createdDivElement;
    }

    // Tuck the node into the end of the active div
    //
    // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeToEndWithTransaction(MOZ_KnownLive(content),
                                     *createdDivElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
    if (unwrappedMoveNodeResult.HasCaretPointSuggestion()) {
      pointToPutCaret = unwrappedMoveNodeResult.UnwrapCaretPoint();
    }
  }

  return latestCreatedDivElement
             ? CreateElementResult(std::move(latestCreatedDivElement),
                                   std::move(pointToPutCaret))
             : CreateElementResult::NotHandled(std::move(pointToPutCaret));
}

Result<EditorDOMPoint, nsresult>
HTMLEditor::AlignContentsInAllTableCellsAndListItems(
    Element& aElement, const nsAString& aAlignType) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Gather list of table cells or list items
  AutoTArray<OwningNonNull<Element>, 64> arrayOfTableCellsAndListItems;
  DOMIterator iter(aElement);
  iter.AppendNodesToArray(
      +[](nsINode& aNode, void*) -> bool {
        MOZ_ASSERT(Element::FromNode(&aNode));
        return HTMLEditUtils::IsTableCell(&aNode) ||
               HTMLEditUtils::IsListItem(&aNode);
      },
      arrayOfTableCellsAndListItems);

  // Now that we have the list, align their contents as requested
  EditorDOMPoint pointToPutCaret;
  for (auto& tableCellOrListItemElement : arrayOfTableCellsAndListItems) {
    // MOZ_KnownLive because 'arrayOfTableCellsAndListItems' is guaranteed to
    // keep it alive.
    Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
        AlignBlockContentsWithDivElement(
            MOZ_KnownLive(tableCellOrListItemElement), aAlignType);
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      NS_WARNING("HTMLEditor::AlignBlockContentsWithDivElement() failed");
      return pointToPutCaretOrError;
    }
    if (pointToPutCaretOrError.inspect().IsSet()) {
      pointToPutCaret = pointToPutCaretOrError.unwrap();
    }
  }

  return pointToPutCaret;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::AlignBlockContentsWithDivElement(
    Element& aBlockElement, const nsAString& aAlignType) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // XXX I don't understand why we should NOT align non-editable children
  //     with modifying EDITABLE `<div>` element.
  nsCOMPtr<nsIContent> firstEditableContent = HTMLEditUtils::GetFirstChild(
      aBlockElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (!firstEditableContent) {
    // This block has no editable content, nothing to align.
    return EditorDOMPoint();
  }

  // If there is only one editable content and it's a `<div>` element,
  // just set `align` attribute of it.
  nsCOMPtr<nsIContent> lastEditableContent = HTMLEditUtils::GetLastChild(
      aBlockElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (firstEditableContent == lastEditableContent &&
      firstEditableContent->IsHTMLElement(nsGkAtoms::div)) {
    nsresult rv = SetAttributeOrEquivalent(
        MOZ_KnownLive(firstEditableContent->AsElement()), nsGkAtoms::align,
        aAlignType, false);
    if (NS_WARN_IF(Destroyed())) {
      NS_WARNING(
          "EditorBase::SetAttributeOrEquivalent(nsGkAtoms::align) caused "
          "destroying the editor");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::SetAttributeOrEquivalent(nsGkAtoms::align) failed");
      return Err(rv);
    }
    return EditorDOMPoint();
  }

  // Otherwise, we need to insert a `<div>` element to set `align` attribute.
  // XXX Don't insert the new `<div>` element until we set `align` attribute
  //     for avoiding running mutation event listeners.
  Result<CreateElementResult, nsresult> createNewDivElementResult =
      CreateAndInsertElement(
          WithTransaction::Yes, *nsGkAtoms::div,
          EditorDOMPoint(&aBlockElement, 0u),
          // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
          [&aAlignType](HTMLEditor& aHTMLEditor, Element& aDivElement,
                        const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            MOZ_ASSERT(!aDivElement.IsInComposedDoc());
            // If aDivElement has not been connected yet, we do not need
            // transaction of setting align attribute here.
            nsresult rv = aHTMLEditor.SetAttributeOrEquivalent(
                &aDivElement, nsGkAtoms::align, aAlignType, false);
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "EditorBase::SetAttributeOrEquivalent("
                                 "nsGkAtoms::align, \"...\", false) failed");
            return rv;
          });
  if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes, "
        "nsGkAtoms::div) failed");
    return createNewDivElementResult.propagateErr();
  }
  CreateElementResult unwrappedCreateNewDivElementResult =
      createNewDivElementResult.unwrap();
  EditorDOMPoint pointToPutCaret =
      unwrappedCreateNewDivElementResult.UnwrapCaretPoint();
  RefPtr<Element> newDivElement =
      unwrappedCreateNewDivElementResult.UnwrapNewNode();
  MOZ_ASSERT(newDivElement);
  // XXX This is tricky and does not work with mutation event listeners.
  //     But I'm not sure what we should do if new content is inserted.
  //     Anyway, I don't think that we should move editable contents
  //     over non-editable contents.  Chrome does no do that.
  while (lastEditableContent && (lastEditableContent != newDivElement)) {
    Result<MoveNodeResult, nsresult> moveNodeResult = MoveNodeWithTransaction(
        *lastEditableContent, EditorDOMPoint(newDivElement, 0u));
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
      return moveNodeResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
    if (unwrappedMoveNodeResult.HasCaretPointSuggestion()) {
      pointToPutCaret = unwrappedMoveNodeResult.UnwrapCaretPoint();
    }
    lastEditableContent = HTMLEditUtils::GetLastChild(
        aBlockElement, {WalkTreeOption::IgnoreNonEditableNode});
  }
  return pointToPutCaret;
}

Result<EditorRawDOMRange, nsresult>
HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction(
    const nsRange* aRange, const Element& aEditingHost) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // This tweaks selections to be more "natural".
  // Idea here is to adjust edges of selection ranges so that they do not cross
  // breaks or block boundaries unless something editable beyond that boundary
  // is also selected.  This adjustment makes it much easier for the various
  // block operations to determine what nodes to act on.
  if (NS_WARN_IF(!aRange) || NS_WARN_IF(!aRange->IsPositioned())) {
    return Err(NS_ERROR_FAILURE);
  }

  const EditorRawDOMPoint startPoint(aRange->StartRef());
  if (NS_WARN_IF(!startPoint.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }
  const EditorRawDOMPoint endPoint(aRange->EndRef());
  if (NS_WARN_IF(!endPoint.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }

  // adjusted values default to original values
  EditorRawDOMRange newRange(startPoint, endPoint);

  // Is there any intervening visible white-space?  If so we can't push
  // selection past that, it would visibly change meaning of users selection.
  const WSRunScanner wsScannerAtEnd(
      WSRunScanner::Scan::EditableNodes, endPoint,
      // We should refer only the default style of HTML because we need to wrap
      // any elements with a specific HTML element.  So we should not refer
      // actual style.  For example, we want to reformat parent HTML block
      // element even if selected in a blocked phrase element or
      // non-HTMLelement.
      BlockInlineCheck::UseHTMLDefaultStyle);
  const WSScanResult scanResultAtEnd =
      wsScannerAtEnd.ScanPreviousVisibleNodeOrBlockBoundaryFrom(endPoint);
  if (scanResultAtEnd.Failed()) {
    NS_WARNING(
        "WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundaryFrom() failed");
    return Err(NS_ERROR_FAILURE);
  }
  if (scanResultAtEnd.ReachedSomethingNonTextContent()) {
    // eThisBlock and eOtherBlock conveniently distinguish cases
    // of going "down" into a block and "up" out of a block.
    if (wsScannerAtEnd.StartsFromOtherBlockElement()) {
      // endpoint is just after the close of a block.
      if (nsIContent* child = HTMLEditUtils::GetLastLeafContent(
              *wsScannerAtEnd.StartReasonOtherBlockElementPtr(),
              {LeafNodeType::LeafNodeOrChildBlock},
              BlockInlineCheck::UseHTMLDefaultStyle)) {
        newRange.SetEnd(EditorRawDOMPoint::After(*child));
      }
      // else block is empty - we can leave selection alone here, i think.
    } else if (wsScannerAtEnd.StartsFromCurrentBlockBoundary() ||
               wsScannerAtEnd.StartsFromInlineEditingHostBoundary()) {
      // endpoint is just after start of this block
      if (nsIContent* child = HTMLEditUtils::GetPreviousContent(
              endPoint, {WalkTreeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseHTMLDefaultStyle, &aEditingHost)) {
        newRange.SetEnd(EditorRawDOMPoint::After(*child));
      }
      // else block is empty - we can leave selection alone here, i think.
    } else if (wsScannerAtEnd.StartsFromBRElement()) {
      // endpoint is just after break.  lets adjust it to before it.
      newRange.SetEnd(
          EditorRawDOMPoint(wsScannerAtEnd.StartReasonBRElementPtr()));
    }
  }

  // Is there any intervening visible white-space?  If so we can't push
  // selection past that, it would visibly change meaning of users selection.
  const WSRunScanner wsScannerAtStart(WSRunScanner::Scan::EditableNodes,
                                      startPoint,
                                      BlockInlineCheck::UseHTMLDefaultStyle);
  const WSScanResult scanResultAtStart =
      wsScannerAtStart.ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
          startPoint);
  if (scanResultAtStart.Failed()) {
    NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundaryFrom() failed");
    return Err(NS_ERROR_FAILURE);
  }
  if (scanResultAtStart.ReachedSomethingNonTextContent()) {
    // eThisBlock and eOtherBlock conveniently distinguish cases
    // of going "down" into a block and "up" out of a block.
    if (wsScannerAtStart.EndsByOtherBlockElement()) {
      // startpoint is just before the start of a block.
      if (nsIContent* child = HTMLEditUtils::GetFirstLeafContent(
              *wsScannerAtStart.EndReasonOtherBlockElementPtr(),
              {LeafNodeType::LeafNodeOrChildBlock},
              BlockInlineCheck::UseHTMLDefaultStyle)) {
        newRange.SetStart(EditorRawDOMPoint(child));
      }
      // else block is empty - we can leave selection alone here, i think.
    } else if (wsScannerAtStart.EndsByCurrentBlockBoundary() ||
               wsScannerAtStart.EndsByInlineEditingHostBoundary()) {
      // startpoint is just before end of this block
      if (nsIContent* child = HTMLEditUtils::GetNextContent(
              startPoint, {WalkTreeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseHTMLDefaultStyle, &aEditingHost)) {
        newRange.SetStart(EditorRawDOMPoint(child));
      }
      // else block is empty - we can leave selection alone here, i think.
    } else if (wsScannerAtStart.EndsByBRElement()) {
      // startpoint is just before a break.  lets adjust it to after it.
      newRange.SetStart(
          EditorRawDOMPoint::After(*wsScannerAtStart.EndReasonBRElementPtr()));
    }
  }

  // There is a demented possibility we have to check for.  We might have a very
  // strange selection that is not collapsed and yet does not contain any
  // editable content, and satisfies some of the above conditions that cause
  // tweaking.  In this case we don't want to tweak the selection into a block
  // it was never in, etc.  There are a variety of strategies one might use to
  // try to detect these cases, but I think the most straightforward is to see
  // if the adjusted locations "cross" the old values: i.e., new end before old
  // start, or new start after old end.  If so then just leave things alone.

  Maybe<int32_t> comp = nsContentUtils::ComparePoints(
      startPoint.ToRawRangeBoundary(), newRange.EndRef().ToRawRangeBoundary());

  if (NS_WARN_IF(!comp)) {
    return Err(NS_ERROR_FAILURE);
  }

  if (*comp == 1) {
    return EditorRawDOMRange();  // New end before old start.
  }

  comp = nsContentUtils::ComparePoints(newRange.StartRef().ToRawRangeBoundary(),
                                       endPoint.ToRawRangeBoundary());

  if (NS_WARN_IF(!comp)) {
    return Err(NS_ERROR_FAILURE);
  }

  if (*comp == 1) {
    return EditorRawDOMRange();  // New start after old end.
  }

  return newRange;
}

template <typename EditorDOMRangeType>
already_AddRefed<nsRange> HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMRangeType& aRange) {
  MOZ_DIAGNOSTIC_ASSERT(aRange.IsPositioned());
  return CreateRangeIncludingAdjuscentWhiteSpaces(aRange.StartRef(),
                                                  aRange.EndRef());
}

template <typename EditorDOMPointType1, typename EditorDOMPointType2>
already_AddRefed<nsRange> HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMPointType1& aStartPoint,
    const EditorDOMPointType2& aEndPoint) {
  MOZ_DIAGNOSTIC_ASSERT(!aStartPoint.IsInNativeAnonymousSubtree());
  MOZ_DIAGNOSTIC_ASSERT(!aEndPoint.IsInNativeAnonymousSubtree());

  if (!aStartPoint.IsInContentNode() || !aEndPoint.IsInContentNode()) {
    NS_WARNING_ASSERTION(aStartPoint.IsSet(), "aStartPoint was not set");
    NS_WARNING_ASSERTION(aEndPoint.IsSet(), "aEndPoint was not set");
    return nullptr;
  }

  const Element* const editingHost = ComputeEditingHost();
  if (NS_WARN_IF(!editingHost)) {
    return nullptr;
  }

  EditorDOMPoint startPoint = aStartPoint.template To<EditorDOMPoint>();
  EditorDOMPoint endPoint = aEndPoint.template To<EditorDOMPoint>();
  AutoClonedRangeArray::
      UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement(
          startPoint, endPoint, *editingHost);

  if (NS_WARN_IF(!startPoint.IsInContentNode()) ||
      NS_WARN_IF(!endPoint.IsInContentNode())) {
    NS_WARNING(
        "AutoClonedRangeArray::"
        "UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement() "
        "failed");
    return nullptr;
  }

  // For text actions, we want to look backwards (or forwards, as
  // appropriate) for additional white-space or nbsp's.  We may have to act
  // on these later even though they are outside of the initial selection.
  // Even if they are in another node!
  // XXX Those scanners do not treat siblings of the text nodes.  Perhaps,
  //     we should use `WSRunScanner::GetFirstASCIIWhiteSpacePointCollapsedTo()`
  //     and `WSRunScanner::GetEndOfCollapsibleASCIIWhiteSpaces()` instead.
  if (startPoint.IsInTextNode()) {
    while (!startPoint.IsStartOfContainer()) {
      if (!startPoint.IsPreviousCharASCIISpaceOrNBSP()) {
        break;
      }
      MOZ_ALWAYS_TRUE(startPoint.RewindOffset());
    }
  }
  if (!startPoint.GetChildOrContainerIfDataNode() ||
      !startPoint.GetChildOrContainerIfDataNode()->IsInclusiveDescendantOf(
          editingHost)) {
    return nullptr;
  }
  if (endPoint.IsInTextNode()) {
    while (!endPoint.IsEndOfContainer()) {
      if (!endPoint.IsCharASCIISpaceOrNBSP()) {
        break;
      }
      MOZ_ALWAYS_TRUE(endPoint.AdvanceOffset());
    }
  }
  EditorDOMPoint lastRawPoint(endPoint);
  if (!lastRawPoint.IsStartOfContainer()) {
    lastRawPoint.RewindOffset();
  }
  if (!lastRawPoint.GetChildOrContainerIfDataNode() ||
      !lastRawPoint.GetChildOrContainerIfDataNode()->IsInclusiveDescendantOf(
          editingHost)) {
    return nullptr;
  }

  RefPtr<nsRange> range =
      nsRange::Create(startPoint.ToRawRangeBoundary(),
                      endPoint.ToRawRangeBoundary(), IgnoreErrors());
  NS_WARNING_ASSERTION(range, "nsRange::Create() failed");
  return range.forget();
}

Result<EditorDOMPoint, nsresult> HTMLEditor::MaybeSplitElementsAtEveryBRElement(
    nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    EditSubAction aEditSubAction) {
  // Post-process the list to break up inline containers that contain br's, but
  // only for operations that might care, like making lists or paragraphs
  switch (aEditSubAction) {
    case EditSubAction::eCreateOrRemoveBlock:
    case EditSubAction::eFormatBlockForHTMLCommand:
    case EditSubAction::eMergeBlockContents:
    case EditSubAction::eCreateOrChangeList:
    case EditSubAction::eSetOrClearAlignment:
    case EditSubAction::eSetPositionToAbsolute:
    case EditSubAction::eIndent:
    case EditSubAction::eOutdent: {
      EditorDOMPoint pointToPutCaret;
      for (size_t index : Reversed(IntegerRange(aArrayOfContents.Length()))) {
        OwningNonNull<nsIContent>& content = aArrayOfContents[index];
        if (HTMLEditUtils::IsInlineContent(
                content, BlockInlineCheck::UseHTMLDefaultStyle) &&
            HTMLEditUtils::IsContainerNode(content) && !content->IsText()) {
          AutoTArray<OwningNonNull<nsIContent>, 24> arrayOfInlineContents;
          // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it
          // alive.
          Result<EditorDOMPoint, nsresult> splitResult =
              SplitElementsAtEveryBRElement(MOZ_KnownLive(content),
                                            arrayOfInlineContents);
          if (splitResult.isErr()) {
            NS_WARNING("HTMLEditor::SplitElementsAtEveryBRElement() failed");
            return splitResult;
          }
          if (splitResult.inspect().IsSet()) {
            pointToPutCaret = splitResult.unwrap();
          }
          // Put these nodes in aArrayOfContents, replacing the current node
          aArrayOfContents.RemoveElementAt(index);
          aArrayOfContents.InsertElementsAt(index, arrayOfInlineContents);
        }
      }
      return pointToPutCaret;
    }
    default:
      return EditorDOMPoint();
  }
}

Result<EditorDOMPoint, nsresult>
HTMLEditor::SplitInlineAncestorsAtRangeBoundaries(
    RangeItem& aRangeItem, BlockInlineCheck aBlockInlineCheck,
    const Element& aEditingHost,
    const nsIContent* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  EditorDOMPoint pointToPutCaret;
  if (!aRangeItem.Collapsed() && aRangeItem.mEndContainer &&
      aRangeItem.mEndContainer->IsContent()) {
    nsCOMPtr<nsIContent> mostAncestorInlineContentAtEnd =
        HTMLEditUtils::GetMostDistantAncestorInlineElement(
            *aRangeItem.mEndContainer->AsContent(), aBlockInlineCheck,
            &aEditingHost, aAncestorLimiter);

    if (mostAncestorInlineContentAtEnd) {
      Result<SplitNodeResult, nsresult> splitEndInlineResult =
          SplitNodeDeepWithTransaction(
              *mostAncestorInlineContentAtEnd, aRangeItem.EndPoint(),
              SplitAtEdges::eDoNotCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitEndInlineResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
            "eDoNotCreateEmptyContainer) failed");
        return splitEndInlineResult.propagateErr();
      }
      SplitNodeResult unwrappedSplitEndInlineResult =
          splitEndInlineResult.unwrap();
      unwrappedSplitEndInlineResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      if (pointToPutCaret.IsInContentNode() &&
          MOZ_UNLIKELY(
              &aEditingHost !=
              ComputeEditingHost(*pointToPutCaret.ContainerAs<nsIContent>()))) {
        NS_WARNING(
            "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
            "eDoNotCreateEmptyContainer) caused changing editing host");
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      const auto splitPointAtEnd =
          unwrappedSplitEndInlineResult.AtSplitPoint<EditorRawDOMPoint>();
      if (MOZ_UNLIKELY(!splitPointAtEnd.IsSet())) {
        NS_WARNING(
            "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
            "eDoNotCreateEmptyContainer) didn't return split point");
        return Err(NS_ERROR_FAILURE);
      }
      aRangeItem.mEndContainer = splitPointAtEnd.GetContainer();
      aRangeItem.mEndOffset = splitPointAtEnd.Offset();
    }
  }

  if (!aRangeItem.mStartContainer || !aRangeItem.mStartContainer->IsContent()) {
    return pointToPutCaret;
  }

  nsCOMPtr<nsIContent> mostAncestorInlineContentAtStart =
      HTMLEditUtils::GetMostDistantAncestorInlineElement(
          *aRangeItem.mStartContainer->AsContent(), aBlockInlineCheck,
          &aEditingHost, aAncestorLimiter);

  if (mostAncestorInlineContentAtStart) {
    Result<SplitNodeResult, nsresult> splitStartInlineResult =
        SplitNodeDeepWithTransaction(*mostAncestorInlineContentAtStart,
                                     aRangeItem.StartPoint(),
                                     SplitAtEdges::eDoNotCreateEmptyContainer);
    if (MOZ_UNLIKELY(splitStartInlineResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
          "eDoNotCreateEmptyContainer) failed");
      return splitStartInlineResult.propagateErr();
    }
    SplitNodeResult unwrappedSplitStartInlineResult =
        splitStartInlineResult.unwrap();
    // XXX Why don't we check editing host like above??
    unwrappedSplitStartInlineResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    // XXX If we split only here because of collapsed range, we're modifying
    //     only start point of aRangeItem.  Shouldn't we modify end point here
    //     if it's collapsed?
    const auto splitPointAtStart =
        unwrappedSplitStartInlineResult.AtSplitPoint<EditorRawDOMPoint>();
    if (MOZ_UNLIKELY(!splitPointAtStart.IsSet())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
          "eDoNotCreateEmptyContainer) didn't return split point");
      return Err(NS_ERROR_FAILURE);
    }
    aRangeItem.mStartContainer = splitPointAtStart.GetContainer();
    aRangeItem.mStartOffset = splitPointAtStart.Offset();
  }

  return pointToPutCaret;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::SplitElementsAtEveryBRElement(
    nsIContent& aMostAncestorToBeSplit,
    nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // First build up a list of all the break nodes inside the inline container.
  AutoTArray<OwningNonNull<HTMLBRElement>, 24> arrayOfBRElements;
  DOMIterator iter(aMostAncestorToBeSplit);
  iter.AppendAllNodesToArray(arrayOfBRElements);

  // If there aren't any breaks, just put inNode itself in the array
  if (arrayOfBRElements.IsEmpty()) {
    aOutArrayOfContents.AppendElement(aMostAncestorToBeSplit);
    return EditorDOMPoint();
  }

  // Else we need to bust up aMostAncestorToBeSplit along all the breaks
  nsCOMPtr<nsIContent> nextContent = &aMostAncestorToBeSplit;
  EditorDOMPoint pointToPutCaret;
  for (OwningNonNull<HTMLBRElement>& brElement : arrayOfBRElements) {
    EditorDOMPoint atBRNode(brElement);
    if (NS_WARN_IF(!atBRNode.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }
    Result<SplitNodeResult, nsresult> splitNodeResult =
        SplitNodeDeepWithTransaction(
            *nextContent, atBRNode, SplitAtEdges::eAllowToCreateEmptyContainer);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
      return splitNodeResult.propagateErr();
    }
    SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
    unwrappedSplitNodeResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    // Put previous node at the split point.
    if (nsIContent* previousContent =
            unwrappedSplitNodeResult.GetPreviousContent()) {
      // Might not be a left node.  A break might have been at the very
      // beginning of inline container, in which case
      // SplitNodeDeepWithTransaction() would not actually split anything.
      aOutArrayOfContents.AppendElement(*previousContent);
    }

    // Move break outside of container and also put in node list
    // MOZ_KnownLive because 'arrayOfBRElements' is guaranteed to keep it alive.
    Result<MoveNodeResult, nsresult> moveBRElementResult =
        MoveNodeWithTransaction(
            MOZ_KnownLive(brElement),
            unwrappedSplitNodeResult.AtNextContent<EditorDOMPoint>());
    if (MOZ_UNLIKELY(moveBRElementResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
      return moveBRElementResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveBRElementResult = moveBRElementResult.unwrap();
    unwrappedMoveBRElementResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    aOutArrayOfContents.AppendElement(brElement);

    nextContent = unwrappedSplitNodeResult.GetNextContent();
  }

  // Now tack on remaining next node.
  aOutArrayOfContents.AppendElement(*nextContent);

  return pointToPutCaret;
}

// static
void HTMLEditor::MakeTransitionList(
    const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    nsTArray<bool>& aTransitionArray) {
  nsINode* prevParent = nullptr;
  aTransitionArray.EnsureLengthAtLeast(aArrayOfContents.Length());
  for (uint32_t i = 0; i < aArrayOfContents.Length(); i++) {
    aTransitionArray[i] = aArrayOfContents[i]->GetParentNode() != prevParent;
    prevParent = aArrayOfContents[i]->GetParentNode();
  }
}

Result<InsertParagraphResult, nsresult>
HTMLEditor::HandleInsertParagraphInHeadingElement(
    Element& aHeadingElement, const EditorDOMPoint& aPointToSplit) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  auto splitHeadingResult =
      [this, &aPointToSplit, &aHeadingElement]()
          MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    // Normalize collapsible white-spaces around the split point to keep
    // them visible after the split.  Note that this does not touch
    // selection because of using AutoTransactionsConserveSelection in
    // WhiteSpaceVisibilityKeeper::ReplaceTextAndRemoveEmptyTextNodes().
    Result<EditorDOMPoint, nsresult> preparationResult =
        WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement(
            *this, aPointToSplit, aHeadingElement);
    if (MOZ_UNLIKELY(preparationResult.isErr())) {
      NS_WARNING(
          "WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement() "
          "failed");
      return preparationResult.propagateErr();
    }
    EditorDOMPoint pointToSplit = preparationResult.unwrap();
    MOZ_ASSERT(pointToSplit.IsInContentNode());

    // Split the header
    Result<SplitNodeResult, nsresult> splitResult =
        SplitNodeDeepWithTransaction(
            aHeadingElement, pointToSplit,
            SplitAtEdges::eAllowToCreateEmptyContainer);
    NS_WARNING_ASSERTION(
        splitResult.isOk(),
        "HTMLEditor::SplitNodeDeepWithTransaction(aHeadingElement, "
        "SplitAtEdges::eAllowToCreateEmptyContainer) failed");
    return splitResult;
  }();
  if (MOZ_UNLIKELY(splitHeadingResult.isErr())) {
    NS_WARNING("Failed to splitting aHeadingElement");
    return splitHeadingResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitHeadingResult = splitHeadingResult.unwrap();
  unwrappedSplitHeadingResult.IgnoreCaretPointSuggestion();
  if (MOZ_UNLIKELY(!unwrappedSplitHeadingResult.DidSplit())) {
    NS_WARNING(
        "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
        "eAllowToCreateEmptyContainer) didn't split aHeadingElement");
    return Err(NS_ERROR_FAILURE);
  }

  // If the left heading element is empty, put a padding <br> element for empty
  // last line into it.
  // FYI: leftHeadingElement is grabbed by unwrappedSplitHeadingResult so that
  //      it's safe to access anytime.
  auto* const leftHeadingElement =
      unwrappedSplitHeadingResult.GetPreviousContentAs<Element>();
  MOZ_ASSERT(leftHeadingElement,
             "SplitNodeResult::GetPreviousContent() should return something if "
             "DidSplit() returns true");
  MOZ_DIAGNOSTIC_ASSERT(HTMLEditUtils::IsHeader(*leftHeadingElement));
  if (HTMLEditUtils::IsEmptyNode(
          *leftHeadingElement,
          {EmptyCheckOption::TreatSingleBRElementAsVisible,
           EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
        InsertPaddingBRElementForEmptyLastLineWithTransaction(
            EditorDOMPoint(leftHeadingElement, 0u));
    if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementForEmptyLastLineWithTransaction("
          ") failed");
      return insertPaddingBRElementResult.propagateErr();
    }
    insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
  }

  // Put caret at start of the right head element if it's not empty.
  auto* const rightHeadingElement =
      unwrappedSplitHeadingResult.GetNextContentAs<Element>();
  MOZ_ASSERT(rightHeadingElement,
             "SplitNodeResult::GetNextContent() should return something if "
             "DidSplit() returns true");
  if (!HTMLEditUtils::IsEmptyBlockElement(
          *rightHeadingElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    return InsertParagraphResult(rightHeadingElement,
                                 EditorDOMPoint(rightHeadingElement, 0u));
  }

  // If the right heading element is empty, delete it.
  // TODO: If we know the new heading element becomes empty, we stop spliting
  //       the heading element.
  // MOZ_KnownLive(rightHeadingElement) because it's grabbed by
  // unwrappedSplitHeadingResult.
  nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*rightHeadingElement));
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
    return Err(rv);
  }

  // Layout tells the caret to blink in a weird place if we don't place a
  // break after the header.
  // XXX This block is dead code unless the removed right heading element is
  //     reconnected by a mutation event listener.  This is a regression of
  //     bug 1405751:
  //     https://searchfox.org/mozilla-central/diff/879f3317d1331818718e18776caa47be7f426a22/editor/libeditor/HTMLEditRules.cpp#6389
  //     However, the traditional behavior is different from the other browsers.
  //     Chrome creates new paragraph in this case.  Therefore, we should just
  //     drop this block in a follow up bug.
  if (rightHeadingElement->GetNextSibling()) {
    // XXX Ignoring non-editable <br> element here is odd because non-editable
    //     <br> elements also work as <br> from point of view of layout.
    nsIContent* nextEditableSibling =
        HTMLEditUtils::GetNextSibling(*rightHeadingElement->GetNextSibling(),
                                      {WalkTreeOption::IgnoreNonEditableNode});
    if (nextEditableSibling &&
        nextEditableSibling->IsHTMLElement(nsGkAtoms::br)) {
      auto afterEditableBRElement = EditorDOMPoint::After(*nextEditableSibling);
      if (NS_WARN_IF(!afterEditableBRElement.IsSet())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      // Put caret at the <br> element.
      return InsertParagraphResult::NotHandled(
          std::move(afterEditableBRElement));
    }
  }

  if (MOZ_UNLIKELY(!leftHeadingElement->IsInComposedDoc())) {
    NS_WARNING("The left heading element was unexpectedly removed");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  TopLevelEditSubActionDataRef().mCachedPendingStyles->Clear();
  mPendingStylesToApplyToNewContent->ClearAllStyles();

  // Create a paragraph if the right heading element is not followed by an
  // editable <br> element.
  nsStaticAtom& newParagraphTagName =
      &DefaultParagraphSeparatorTagName() == nsGkAtoms::br
          ? *nsGkAtoms::p
          : DefaultParagraphSeparatorTagName();
  // We want a wrapper element even if we separate with a <br>.
  // MOZ_KnownLive(newParagraphTagName) because it's available until shutdown.
  Result<CreateElementResult, nsresult> createNewParagraphElementResult =
      CreateAndInsertElement(WithTransaction::Yes,
                             MOZ_KnownLive(newParagraphTagName),
                             EditorDOMPoint::After(*leftHeadingElement),
                             HTMLEditor::InsertNewBRElement);
  if (MOZ_UNLIKELY(createNewParagraphElementResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
    return createNewParagraphElementResult.propagateErr();
  }
  CreateElementResult unwrappedCreateNewParagraphElementResult =
      createNewParagraphElementResult.unwrap();
  // Put caret at the <br> element in the following paragraph.
  unwrappedCreateNewParagraphElementResult.IgnoreCaretPointSuggestion();
  MOZ_ASSERT(unwrappedCreateNewParagraphElementResult.GetNewNode());
  EditorDOMPoint pointToPutCaret(
      unwrappedCreateNewParagraphElementResult.GetNewNode(), 0u);
  return InsertParagraphResult(
      unwrappedCreateNewParagraphElementResult.UnwrapNewNode(),
      std::move(pointToPutCaret));
}

Result<SplitNodeResult, nsresult> HTMLEditor::HandleInsertParagraphInParagraph(
    Element& aParentDivOrP, const EditorDOMPoint& aCandidatePointToSplit,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aCandidatePointToSplit.IsSetAndValid());

  // First, get a better split point to avoid to create a new empty link in the
  // right paragraph.
  EditorDOMPoint pointToSplit = [&]() {
    // We shouldn't create new anchor element which has non-empty href unless
    // splitting middle of it because we assume that users don't want to create
    // *same* anchor element across two or more paragraphs in most cases.
    // So, adjust selection start if it's edge of anchor element(s).
    // XXX We don't support white-space collapsing in these cases since it needs
    //     some additional work with WhiteSpaceVisibilityKeeper but it's not
    //     usual case. E.g., |<a href="foo"><b>foo []</b> </a>|
    if (aCandidatePointToSplit.IsStartOfContainer()) {
      EditorDOMPoint candidatePoint(aCandidatePointToSplit);
      for (nsIContent* container =
               aCandidatePointToSplit.GetContainerAs<nsIContent>();
           container && container != &aParentDivOrP;
           container = container->GetParent()) {
        if (HTMLEditUtils::IsLink(container)) {
          // Found link should be only in right node.  So, we shouldn't split
          // it.
          candidatePoint.Set(container);
          // Even if we found an anchor element, don't break because DOM API
          // allows to nest anchor elements.
        }
        // If the container is middle of its parent, stop adjusting split point.
        if (container->GetPreviousSibling()) {
          // XXX Should we check if previous sibling is visible content?
          //     E.g., should we ignore comment node, invisible <br> element?
          break;
        }
      }
      return candidatePoint;
    }

    // We also need to check if selection is at invisible <br> element at end
    // of an <a href="foo"> element because editor inserts a <br> element when
    // user types Enter key after a white-space which is at middle of
    // <a href="foo"> element and when setting selection at end of the element,
    // selection becomes referring the <br> element.  We may need to change this
    // behavior later if it'd be standardized.
    if (aCandidatePointToSplit.IsEndOfContainer() ||
        aCandidatePointToSplit.IsBRElementAtEndOfContainer()) {
      // If there are 2 <br> elements, the first <br> element is visible.  E.g.,
      // |<a href="foo"><b>boo[]<br></b><br></a>|, we should split the <a>
      // element.  Otherwise, E.g., |<a href="foo"><b>boo[]<br></b></a>|,
      // we should not split the <a> element and ignore inline elements in it.
      bool foundBRElement =
          aCandidatePointToSplit.IsBRElementAtEndOfContainer();
      EditorDOMPoint candidatePoint(aCandidatePointToSplit);
      for (nsIContent* container =
               aCandidatePointToSplit.GetContainerAs<nsIContent>();
           container && container != &aParentDivOrP;
           container = container->GetParent()) {
        if (HTMLEditUtils::IsLink(container)) {
          // Found link should be only in left node.  So, we shouldn't split it.
          candidatePoint.SetAfter(container);
          // Even if we found an anchor element, don't break because DOM API
          // allows to nest anchor elements.
        }
        // If the container is middle of its parent, stop adjusting split point.
        if (nsIContent* nextSibling = container->GetNextSibling()) {
          if (foundBRElement) {
            // If we've already found a <br> element, we assume found node is
            // visible <br> or something other node.
            // XXX Should we check if non-text data node like comment?
            break;
          }

          // XXX Should we check if non-text data node like comment?
          if (!nextSibling->IsHTMLElement(nsGkAtoms::br)) {
            break;
          }
          foundBRElement = true;
        }
      }
      return candidatePoint;
    }
    return aCandidatePointToSplit;
  }();

  const bool createNewParagraph = GetReturnInParagraphCreatesNewParagraph();
  RefPtr<HTMLBRElement> brElement;
  if (createNewParagraph && pointToSplit.GetContainer() == &aParentDivOrP) {
    // We are try to split only the current paragraph.  Therefore, we don't need
    // to create new <br> elements around it (if left and/or right paragraph
    // becomes empty, it'll be treated by SplitParagraphWithTransaction().
    brElement = nullptr;
  } else if (pointToSplit.IsInTextNode()) {
    if (pointToSplit.IsStartOfContainer()) {
      // If we're splitting the paragraph at start of a text node and there is
      // no preceding visible <br> element, we need to create a <br> element to
      // keep the inline elements containing this text node.
      // TODO: If the parent of the text node is the splitting paragraph,
      //       obviously we don't need to do this because empty paragraphs will
      //       be treated by SplitParagraphWithTransaction().  In this case, we
      //       just need to update pointToSplit for using the same path as the
      //       previous `if` block.
      brElement =
          HTMLBRElement::FromNodeOrNull(HTMLEditUtils::GetPreviousSibling(
              *pointToSplit.ContainerAs<Text>(),
              {WalkTreeOption::IgnoreNonEditableNode}));
      if (!brElement || HTMLEditUtils::IsInvisibleBRElement(*brElement) ||
          EditorUtils::IsPaddingBRElementForEmptyLastLine(*brElement)) {
        // If insertParagraph does not create a new paragraph, default to
        // insertLineBreak.
        if (!createNewParagraph) {
          return SplitNodeResult::NotHandled(pointToSplit);
        }
        const EditorDOMPoint pointToInsertBR = pointToSplit.ParentPoint();
        MOZ_ASSERT(pointToInsertBR.IsSet());
        if (pointToInsertBR.IsInContentNode() &&
            HTMLEditUtils::CanNodeContain(
                *pointToInsertBR.ContainerAs<nsIContent>(), *nsGkAtoms::br)) {
          Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              pointToInsertBR);
          if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                "LineBreakType::BRElement) failed");
            return insertBRElementResultOrError.propagateErr();
          }
          CreateLineBreakResult insertBRElementResult =
              insertBRElementResultOrError.unwrap();
          // We'll collapse `Selection` to the place suggested by
          // SplitParagraphWithTransaction.
          insertBRElementResult.IgnoreCaretPointSuggestion();
          brElement = &insertBRElementResult->BRElementRef();
        }
      }
    } else if (pointToSplit.IsEndOfContainer()) {
      // If we're splitting the paragraph at end of a text node and there is not
      // following visible <br> element, we need to create a <br> element after
      // the text node to make current style specified by parent inline elements
      // keep in the right paragraph.
      // TODO: Same as above, we don't need to do this if the text node is a
      //       direct child of the paragraph.  For using the simplest path, we
      //       just need to update `pointToSplit` in the case.
      brElement = HTMLBRElement::FromNodeOrNull(HTMLEditUtils::GetNextSibling(
          *pointToSplit.ContainerAs<Text>(),
          {WalkTreeOption::IgnoreNonEditableNode}));
      if (!brElement || HTMLEditUtils::IsInvisibleBRElement(*brElement) ||
          EditorUtils::IsPaddingBRElementForEmptyLastLine(*brElement)) {
        // If insertParagraph does not create a new paragraph, default to
        // insertLineBreak.
        if (!createNewParagraph) {
          return SplitNodeResult::NotHandled(pointToSplit);
        }
        const auto pointToInsertBR =
            EditorDOMPoint::After(*pointToSplit.ContainerAs<Text>());
        MOZ_ASSERT(pointToInsertBR.IsSet());
        if (pointToInsertBR.IsInContentNode() &&
            HTMLEditUtils::CanNodeContain(
                *pointToInsertBR.ContainerAs<nsIContent>(), *nsGkAtoms::br)) {
          Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              pointToInsertBR);
          if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                "LineBreakType::BRElement) failed");
            return insertBRElementResultOrError.propagateErr();
          }
          CreateLineBreakResult insertBRElementResult =
              insertBRElementResultOrError.unwrap();
          // We'll collapse `Selection` to the place suggested by
          // SplitParagraphWithTransaction.
          insertBRElementResult.IgnoreCaretPointSuggestion();
          brElement = &insertBRElementResult->BRElementRef();
        }
      }
    } else {
      // If insertParagraph does not create a new paragraph, default to
      // insertLineBreak.
      if (!createNewParagraph) {
        return SplitNodeResult::NotHandled(pointToSplit);
      }

      // If we're splitting the paragraph at middle of a text node, we should
      // split the text node here and put a <br> element next to the left text
      // node.
      // XXX Why? I think that this should be handled in
      //     SplitParagraphWithTransaction() directly because I don't find
      //     the necessary case of the <br> element.

      // XXX We split a text node here if caret is middle of it to insert
      //     <br> element **before** splitting aParentDivOrP.  Then, if
      //     the <br> element becomes unnecessary, it'll be removed again.
      //     So this does much more complicated things than what we want to
      //     do here.  We should handle this case separately to make the code
      //     much simpler.

      // Normalize collapsible white-spaces around the split point to keep
      // them visible after the split.  Note that this does not touch
      // selection because of using AutoTransactionsConserveSelection in
      // WhiteSpaceVisibilityKeeper::ReplaceTextAndRemoveEmptyTextNodes().
      Result<EditorDOMPoint, nsresult> pointToSplitOrError =
          WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement(
              *this, pointToSplit, aParentDivOrP);
      if (NS_WARN_IF(Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (MOZ_UNLIKELY(pointToSplitOrError.isErr())) {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement() "
            "failed");
        return pointToSplitOrError.propagateErr();
      }
      MOZ_ASSERT(pointToSplitOrError.inspect().IsSetAndValid());
      if (pointToSplitOrError.inspect().IsSet()) {
        pointToSplit = pointToSplitOrError.unwrap();
      }
      Result<SplitNodeResult, nsresult> splitParentDivOrPResult =
          SplitNodeWithTransaction(pointToSplit);
      if (MOZ_UNLIKELY(splitParentDivOrPResult.isErr())) {
        NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
        return splitParentDivOrPResult;
      }
      // We'll collapse `Selection` to the place suggested by
      // SplitParagraphWithTransaction.
      splitParentDivOrPResult.inspect().IgnoreCaretPointSuggestion();

      pointToSplit.SetToEndOf(
          splitParentDivOrPResult.inspect().GetPreviousContent());
      if (NS_WARN_IF(!pointToSplit.IsInContentNode())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }

      // We need to put new <br> after the left node if given node was split
      // above.
      const auto pointToInsertBR =
          EditorDOMPoint::After(*pointToSplit.ContainerAs<nsIContent>());
      MOZ_ASSERT(pointToInsertBR.IsSet());
      if (pointToInsertBR.IsInContentNode() &&
          HTMLEditUtils::CanNodeContain(
              *pointToInsertBR.ContainerAs<nsIContent>(), *nsGkAtoms::br)) {
        AutoTrackDOMPoint trackPointToSplit(RangeUpdaterRef(), &pointToSplit);
        Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
            InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                            pointToInsertBR);
        if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
              "LineBreakType::BRElement) failed");
          return insertBRElementResultOrError.propagateErr();
        }
        CreateLineBreakResult insertBRElementResult =
            insertBRElementResultOrError.unwrap();
        // We'll collapse `Selection` to the place suggested by
        // SplitParagraphWithTransaction.
        insertBRElementResult.IgnoreCaretPointSuggestion();
        brElement = &insertBRElementResult->BRElementRef();
        trackPointToSplit.FlushAndStopTracking();
        if (NS_WARN_IF(!pointToSplit.IsInContentNodeAndValidInComposedDoc())) {
          return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
        }
      }
    }
  } else {
    // If we're splitting in a child element of the paragraph, and there is no
    // <br> element around it, we should insert a <br> element at the split
    // point and keep splitting the paragraph after the new <br> element.
    // XXX Why? We probably need to do this if we're splitting in an inline
    //     element which and whose parents provide some styles, we should put
    //     the <br> element for making a placeholder in the left paragraph for
    //     moving to the caret, but I think that this could be handled in fewer
    //     cases than this.
    brElement = HTMLBRElement::FromNodeOrNull(HTMLEditUtils::GetPreviousContent(
        pointToSplit, {WalkTreeOption::IgnoreNonEditableNode},
        BlockInlineCheck::Unused, &aEditingHost));
    if (!brElement || HTMLEditUtils::IsInvisibleBRElement(*brElement) ||
        EditorUtils::IsPaddingBRElementForEmptyLastLine(*brElement)) {
      // is there a BR after it?
      brElement = HTMLBRElement::FromNodeOrNull(HTMLEditUtils::GetNextContent(
          pointToSplit, {WalkTreeOption::IgnoreNonEditableNode},
          BlockInlineCheck::Unused, &aEditingHost));
      if (!brElement || HTMLEditUtils::IsInvisibleBRElement(*brElement) ||
          EditorUtils::IsPaddingBRElementForEmptyLastLine(*brElement)) {
        // If insertParagraph does not create a new paragraph, default to
        // insertLineBreak.
        if (!createNewParagraph) {
          return SplitNodeResult::NotHandled(pointToSplit);
        }
        if (pointToSplit.IsInContentNode() &&
            HTMLEditUtils::CanNodeContain(
                *pointToSplit.ContainerAs<nsIContent>(), *nsGkAtoms::br)) {
          Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
              InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                              pointToSplit);
          if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
            NS_WARNING(
                "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
                "LineBreakType::BRElement) failed");
            return insertBRElementResultOrError.propagateErr();
          }
          CreateLineBreakResult insertBRElementResult =
              insertBRElementResultOrError.unwrap();
          // We'll collapse `Selection` to the place suggested by
          // SplitParagraphWithTransaction.
          insertBRElementResult.IgnoreCaretPointSuggestion();
          brElement = &insertBRElementResult->BRElementRef();
          // We split the parent after the <br>.
          pointToSplit.SetAfter(brElement);
          if (NS_WARN_IF(!pointToSplit.IsSet())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
        }
      }
    }
  }

  Result<SplitNodeResult, nsresult> splitParagraphResult =
      SplitParagraphWithTransaction(aParentDivOrP, pointToSplit, brElement,
                                    aEditingHost);
  if (MOZ_UNLIKELY(splitParagraphResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitParagraphWithTransaction() failed");
    return splitParagraphResult;
  }
  if (MOZ_UNLIKELY(!splitParagraphResult.inspect().DidSplit())) {
    NS_WARNING(
        "HTMLEditor::SplitParagraphWithTransaction() didn't split the "
        "paragraph");
    splitParagraphResult.inspect().IgnoreCaretPointSuggestion();
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(splitParagraphResult.inspect().Handled());
  return splitParagraphResult;
}

Result<SplitNodeResult, nsresult> HTMLEditor::SplitParagraphWithTransaction(
    Element& aParentDivOrP, const EditorDOMPoint& aStartOfRightNode,
    HTMLBRElement* aMayBecomeVisibleBRElement, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  Result<EditorDOMPoint, nsresult> preparationResult =
      WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement(
          *this, aStartOfRightNode, aParentDivOrP);
  if (MOZ_UNLIKELY(preparationResult.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement() failed");
    return preparationResult.propagateErr();
  }
  EditorDOMPoint pointToSplit = preparationResult.unwrap();
  MOZ_ASSERT(pointToSplit.IsInContentNode());

  // Split the paragraph.
  Result<SplitNodeResult, nsresult> splitDivOrPResult =
      SplitNodeDeepWithTransaction(aParentDivOrP, pointToSplit,
                                   SplitAtEdges::eAllowToCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitDivOrPResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
    return splitDivOrPResult;
  }
  SplitNodeResult unwrappedSplitDivOrPResult = splitDivOrPResult.unwrap();
  if (MOZ_UNLIKELY(!unwrappedSplitDivOrPResult.DidSplit())) {
    NS_WARNING(
        "HTMLEditor::SplitNodeDeepWithTransaction() didn't split any nodes");
    return unwrappedSplitDivOrPResult;
  }

  // We'll compute caret suggestion later.  So the simple result is not needed.
  unwrappedSplitDivOrPResult.IgnoreCaretPointSuggestion();

  auto* const leftDivOrParagraphElement =
      unwrappedSplitDivOrPResult.GetPreviousContentAs<Element>();
  MOZ_ASSERT(leftDivOrParagraphElement,
             "SplitNodeResult::GetPreviousContent() should return something if "
             "DidSplit() returns true");
  auto* const rightDivOrParagraphElement =
      unwrappedSplitDivOrPResult.GetNextContentAs<Element>();
  MOZ_ASSERT(rightDivOrParagraphElement,
             "SplitNodeResult::GetNextContent() should return something if "
             "DidSplit() returns true");

  // Get rid of the break, if it is visible (otherwise it may be needed to
  // prevent an empty p).
  if (aMayBecomeVisibleBRElement &&
      HTMLEditUtils::IsVisibleBRElement(*aMayBecomeVisibleBRElement)) {
    nsresult rv = DeleteNodeWithTransaction(*aMayBecomeVisibleBRElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  // Remove ID attribute on the paragraph from the right node.
  // MOZ_KnownLive(rightDivOrParagraphElement) because it's grabbed by
  // unwrappedSplitDivOrPResult.
  nsresult rv = RemoveAttributeWithTransaction(
      MOZ_KnownLive(*rightDivOrParagraphElement), *nsGkAtoms::id);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::id) failed");
    return Err(rv);
  }

  // We need to ensure to both paragraphs visible even if they are empty.
  // However, padding <br> element for empty last line isn't useful in this
  // case because it'll be ignored by PlaintextSerializer.  Additionally,
  // it'll be exposed as <br> with Element.innerHTML.  Therefore, we can use
  // normal <br> elements for placeholder in this case.  Note that Chromium
  // also behaves so.
  auto InsertBRElementIfEmptyBlockElement =
      [&](Element& aElement) MOZ_CAN_RUN_SCRIPT {
        if (!HTMLEditUtils::IsBlockElement(
                aElement, BlockInlineCheck::UseComputedDisplayStyle)) {
          return NS_OK;
        }

        if (!HTMLEditUtils::IsEmptyNode(
                aElement, {EmptyCheckOption::TreatSingleBRElementAsVisible})) {
          return NS_OK;
        }

        // XXX: Probably, we should use
        //      InsertPaddingBRElementForEmptyLastLineWithTransaction here, and
        //      if there are some empty inline container, we should put the <br>
        //      into the last one.
        Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
            InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                            EditorDOMPoint(&aElement, 0u));
        if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
              "LineBreakType::BRElement) failed");
          return insertBRElementResultOrError.unwrapErr();
        }
        CreateLineBreakResult insertBRElementResult =
            insertBRElementResultOrError.unwrap();
        MOZ_ASSERT(insertBRElementResult.Handled());
        // After this is called twice, we'll compute new caret position.
        // Therefore, we don't need to update selection here.
        insertBRElementResult.IgnoreCaretPointSuggestion();
        return NS_OK;
      };

  // MOZ_KnownLive(leftDivOrParagraphElement) because it's grabbed by
  // splitDivOrResult.
  rv = InsertBRElementIfEmptyBlockElement(
      MOZ_KnownLive(*leftDivOrParagraphElement));
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "InsertBRElementIfEmptyBlockElement(leftDivOrParagraphElement) failed");
    return Err(rv);
  }

  if (HTMLEditUtils::IsEmptyNode(*rightDivOrParagraphElement)) {
    // If the right paragraph is empty, it might have an empty inline element
    // (which may contain other empty inline containers) and optionally a <br>
    // element which may not be in the deepest inline element.
    const RefPtr<Element> deepestInlineContainerElement =
        [](const Element& aBlockElement) {
          Element* result = nullptr;
          for (Element* maybeDeepestInlineContainer =
                   Element::FromNodeOrNull(aBlockElement.GetFirstChild());
               maybeDeepestInlineContainer &&
               HTMLEditUtils::IsInlineContent(
                   *maybeDeepestInlineContainer,
                   BlockInlineCheck::UseComputedDisplayStyle) &&
               HTMLEditUtils::IsContainerNode(*maybeDeepestInlineContainer);
               maybeDeepestInlineContainer =
                   maybeDeepestInlineContainer->GetFirstElementChild()) {
            result = maybeDeepestInlineContainer;
          }
          return result;
        }(*rightDivOrParagraphElement);
    if (deepestInlineContainerElement) {
      const Maybe<EditorLineBreak> lineBreak =
          HTMLEditUtils::GetFirstLineBreak<EditorLineBreak>(
              *rightDivOrParagraphElement);
      if (lineBreak.isSome()) {
        // If there is a <br> element and it is in the deepest inline container,
        // we need to do nothing anymore. Let's suggest caret position as at the
        // <br>.
        if (lineBreak->IsHTMLBRElement() &&
            lineBreak->BRElementRef().GetParentNode() ==
                deepestInlineContainerElement) {
          auto pointAtBRElement = lineBreak->To<EditorDOMPoint>();
          {
            AutoEditorDOMPointChildInvalidator lockOffset(pointAtBRElement);
            nsresult rv =
                UpdateBRElementType(MOZ_KnownLive(lineBreak->BRElementRef()),
                                    BRElementType::PaddingForEmptyLastLine);
            if (NS_FAILED(rv)) {
              NS_WARNING("EditorBase::UpdateBRElementType() failed");
              return Err(rv);
            }
          }
          return SplitNodeResult(std::move(unwrappedSplitDivOrPResult),
                                 pointAtBRElement);
        }
        // Otherwise, we should put a padding line break into the deepest
        // inline container and then, existing line break (if there is)
        // becomes unnecessary.
        Result<EditorDOMPoint, nsresult> lineBreakPointOrError =
            DeleteLineBreakWithTransaction(lineBreak.ref(), nsIEditor::eStrip,
                                           aEditingHost);
        if (MOZ_UNLIKELY(lineBreakPointOrError.isErr())) {
          NS_WARNING("HTMLEditor::DeleteLineBreakWithTransaction() failed");
          return lineBreakPointOrError.propagateErr();
        }
        Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
            InsertPaddingBRElementForEmptyLastLineWithTransaction(
                EditorDOMPoint::AtEndOf(deepestInlineContainerElement));
        if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::"
              "InsertPaddingBRElementForEmptyLastLineWithTransaction() failed");
          return insertPaddingBRElementResult.propagateErr();
        }
        insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
        return SplitNodeResult(
            std::move(unwrappedSplitDivOrPResult),
            EditorDOMPoint(
                insertPaddingBRElementResult.inspect().GetNewNode()));
      }
    }

    // If there is no inline container elements, we just need to make the
    // right paragraph visible.
    nsresult rv = InsertBRElementIfEmptyBlockElement(
        MOZ_KnownLive(*rightDivOrParagraphElement));
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "InsertBRElementIfEmptyBlockElement(rightDivOrParagraphElement) "
          "failed");
      return Err(rv);
    }
  }

  // Let's put caret at start of the first leaf container.
  nsIContent* child = HTMLEditUtils::GetFirstLeafContent(
      *rightDivOrParagraphElement, {LeafNodeType::LeafNodeOrChildBlock},
      BlockInlineCheck::UseComputedDisplayStyle);
  if (MOZ_UNLIKELY(!child)) {
    return SplitNodeResult(std::move(unwrappedSplitDivOrPResult),
                           EditorDOMPoint(rightDivOrParagraphElement, 0u));
  }
  return child->IsText() || HTMLEditUtils::IsContainerNode(*child)
             ? SplitNodeResult(std::move(unwrappedSplitDivOrPResult),
                               EditorDOMPoint(child, 0u))
             : SplitNodeResult(std::move(unwrappedSplitDivOrPResult),
                               EditorDOMPoint(child));
}

Result<InsertParagraphResult, nsresult>
HTMLEditor::HandleInsertParagraphInListItemElement(
    Element& aListItemElement, const EditorDOMPoint& aPointToSplit,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(HTMLEditUtils::IsListItem(&aListItemElement));

  // If aListItemElement is empty, then we want to outdent its content.
  if (&aEditingHost != aListItemElement.GetParentElement() &&
      HTMLEditUtils::IsEmptyBlockElement(
          aListItemElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    RefPtr<Element> leftListElement = aListItemElement.GetParentElement();
    // If the given list item element is not the last list item element of
    // its parent nor not followed by sub list elements, split the parent
    // before it.
    if (!HTMLEditUtils::IsLastChild(aListItemElement,
                                    {WalkTreeOption::IgnoreNonEditableNode})) {
      Result<SplitNodeResult, nsresult> splitListItemParentResult =
          SplitNodeWithTransaction(EditorDOMPoint(&aListItemElement));
      if (MOZ_UNLIKELY(splitListItemParentResult.isErr())) {
        NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
        return splitListItemParentResult.propagateErr();
      }
      SplitNodeResult unwrappedSplitListItemParentResult =
          splitListItemParentResult.unwrap();
      if (MOZ_UNLIKELY(!unwrappedSplitListItemParentResult.DidSplit())) {
        NS_WARNING(
            "HTMLEditor::SplitNodeWithTransaction() didn't split the parent of "
            "aListItemElement");
        MOZ_ASSERT(
            !unwrappedSplitListItemParentResult.HasCaretPointSuggestion());
        return Err(NS_ERROR_FAILURE);
      }
      unwrappedSplitListItemParentResult.IgnoreCaretPointSuggestion();
      leftListElement =
          unwrappedSplitListItemParentResult.GetPreviousContentAs<Element>();
      MOZ_DIAGNOSTIC_ASSERT(leftListElement);
    }

    auto afterLeftListElement = EditorDOMPoint::After(leftListElement);
    if (MOZ_UNLIKELY(!afterLeftListElement.IsSet())) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // If aListItemElement is in an invalid sub-list element, move it into
    // the grand parent list element in order to outdent.
    if (HTMLEditUtils::IsAnyListElement(afterLeftListElement.GetContainer())) {
      Result<MoveNodeResult, nsresult> moveListItemElementResult =
          MoveNodeWithTransaction(aListItemElement, afterLeftListElement);
      if (MOZ_UNLIKELY(moveListItemElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
        return moveListItemElementResult.propagateErr();
      }
      moveListItemElementResult.inspect().IgnoreCaretPointSuggestion();
      return InsertParagraphResult(&aListItemElement,
                                   EditorDOMPoint(&aListItemElement, 0u));
    }

    // Otherwise, replace the empty aListItemElement with a new paragraph.
    nsresult rv = DeleteNodeWithTransaction(aListItemElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    nsStaticAtom& newParagraphTagName =
        &DefaultParagraphSeparatorTagName() == nsGkAtoms::br
            ? *nsGkAtoms::p
            : DefaultParagraphSeparatorTagName();
    // MOZ_KnownLive(newParagraphTagName) because it's available until shutdown.
    Result<CreateElementResult, nsresult> createNewParagraphElementResult =
        CreateAndInsertElement(
            WithTransaction::Yes, MOZ_KnownLive(newParagraphTagName),
            afterLeftListElement, HTMLEditor::InsertNewBRElement);
    if (MOZ_UNLIKELY(createNewParagraphElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
      return createNewParagraphElementResult.propagateErr();
    }
    createNewParagraphElementResult.inspect().IgnoreCaretPointSuggestion();
    MOZ_ASSERT(createNewParagraphElementResult.inspect().GetNewNode());
    EditorDOMPoint pointToPutCaret(
        createNewParagraphElementResult.inspect().GetNewNode(), 0u);
    return InsertParagraphResult(
        createNewParagraphElementResult.inspect().GetNewNode(),
        std::move(pointToPutCaret));
  }

  // If aListItemElement has some content or aListItemElement is empty but it's
  // a child of editing host, we want a new list item at the same list level.
  // First, sort out white-spaces.
  Result<EditorDOMPoint, nsresult> preparationResult =
      WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement(
          *this, aPointToSplit, aListItemElement);
  if (preparationResult.isErr()) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::PrepareToSplitBlockElement() failed");
    return Err(preparationResult.unwrapErr());
  }
  EditorDOMPoint pointToSplit = preparationResult.unwrap();
  MOZ_ASSERT(pointToSplit.IsInContentNode());

  // Now split the list item.
  Result<SplitNodeResult, nsresult> splitListItemResult =
      SplitNodeDeepWithTransaction(aListItemElement, pointToSplit,
                                   SplitAtEdges::eAllowToCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitListItemResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
    return splitListItemResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitListItemElement = splitListItemResult.unwrap();
  unwrappedSplitListItemElement.IgnoreCaretPointSuggestion();
  if (MOZ_UNLIKELY(!aListItemElement.GetParent())) {
    NS_WARNING("Somebody disconnected the target listitem from the parent");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  // If aListItemElement is not replaced, we should not do anything anymore.
  if (MOZ_UNLIKELY(!unwrappedSplitListItemElement.DidSplit()) ||
      NS_WARN_IF(!unwrappedSplitListItemElement.GetNewContentAs<Element>()) ||
      NS_WARN_IF(
          !unwrappedSplitListItemElement.GetOriginalContentAs<Element>())) {
    NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() didn't split");
    return Err(NS_ERROR_FAILURE);
  }

  // FYI: They are grabbed by unwrappedSplitListItemElement so that they are
  // known live
  //      things.
  auto& leftListItemElement =
      *unwrappedSplitListItemElement.GetPreviousContentAs<Element>();
  auto& rightListItemElement =
      *unwrappedSplitListItemElement.GetNextContentAs<Element>();

  // Hack: until I can change the damaged doc range code back to being
  // extra-inclusive, I have to manually detect certain list items that may be
  // left empty.
  if (HTMLEditUtils::IsEmptyNode(
          leftListItemElement,
          {EmptyCheckOption::TreatSingleBRElementAsVisible,
           EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
        InsertPaddingBRElementForEmptyLastLineWithTransaction(
            EditorDOMPoint(&leftListItemElement, 0u));
    if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementForEmptyLastLineWithTransaction("
          ") failed");
      return insertPaddingBRElementResult.propagateErr();
    }
    // We're returning a candidate point to put caret so that we don't need to
    // update now.
    insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
    return InsertParagraphResult(&rightListItemElement,
                                 EditorDOMPoint(&rightListItemElement, 0u));
  }

  if (HTMLEditUtils::IsEmptyNode(
          rightListItemElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    // If aListItemElement is a <dd> or a <dt> and the right list item is empty
    // or a direct child of the editing host, replace it a new list item element
    // whose type is the other one.
    if (aListItemElement.IsAnyOfHTMLElements(nsGkAtoms::dd, nsGkAtoms::dt)) {
      nsStaticAtom& nextDefinitionListItemTagName =
          aListItemElement.IsHTMLElement(nsGkAtoms::dt) ? *nsGkAtoms::dd
                                                        : *nsGkAtoms::dt;
      // MOZ_KnownLive(nextDefinitionListItemTagName) because it's available
      // until shutdown.
      Result<CreateElementResult, nsresult> createNewListItemElementResult =
          CreateAndInsertElement(WithTransaction::Yes,
                                 MOZ_KnownLive(nextDefinitionListItemTagName),
                                 EditorDOMPoint::After(rightListItemElement));
      if (MOZ_UNLIKELY(createNewListItemElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
        return createNewListItemElementResult.propagateErr();
      }
      CreateElementResult unwrappedCreateNewListItemElementResult =
          createNewListItemElementResult.unwrap();
      unwrappedCreateNewListItemElementResult.IgnoreCaretPointSuggestion();
      RefPtr<Element> newListItemElement =
          unwrappedCreateNewListItemElementResult.UnwrapNewNode();
      MOZ_ASSERT(newListItemElement);
      // MOZ_KnownLive(rightListItemElement) because it's grabbed by
      // unwrappedSplitListItemElement.
      nsresult rv =
          DeleteNodeWithTransaction(MOZ_KnownLive(rightListItemElement));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return Err(rv);
      }
      EditorDOMPoint pointToPutCaret(newListItemElement, 0u);
      return InsertParagraphResult(std::move(newListItemElement),
                                   std::move(pointToPutCaret));
    }

    // If aListItemElement is a <li> and the right list item becomes empty or a
    // direct child of the editing host, copy all inline elements affecting to
    // the style at end of the left list item element to the right list item
    // element.
    // MOZ_KnownLive(leftListItemElement) and
    // MOZ_KnownLive(rightListItemElement) because they are grabbed by
    // unwrappedSplitListItemElement.
    Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
        CopyLastEditableChildStylesWithTransaction(
            MOZ_KnownLive(leftListItemElement),
            MOZ_KnownLive(rightListItemElement), aEditingHost);
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::CopyLastEditableChildStylesWithTransaction() failed");
      return pointToPutCaretOrError.propagateErr();
    }
    return InsertParagraphResult(&rightListItemElement,
                                 pointToPutCaretOrError.unwrap());
  }

  // If the right list item element is not empty, we need to consider where to
  // put caret in it. If it has non-container inline elements, <br> or <hr>, at
  // the element is proper position.
  const WSScanResult forwardScanFromStartOfListItemResult =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          WSRunScanner::Scan::EditableNodes,
          EditorRawDOMPoint(&rightListItemElement, 0u),
          BlockInlineCheck::UseComputedDisplayStyle);
  if (MOZ_UNLIKELY(forwardScanFromStartOfListItemResult.Failed())) {
    NS_WARNING("WSRunScanner::ScanNextVisibleNodeOrBlockBoundary() failed");
    return Err(NS_ERROR_FAILURE);
  }
  if (forwardScanFromStartOfListItemResult.ReachedSpecialContent() ||
      forwardScanFromStartOfListItemResult.ReachedBRElement() ||
      forwardScanFromStartOfListItemResult.ReachedHRElement()) {
    auto atFoundElement = forwardScanFromStartOfListItemResult
                              .PointAtReachedContent<EditorDOMPoint>();
    if (NS_WARN_IF(!atFoundElement.IsSetAndValid())) {
      return Err(NS_ERROR_FAILURE);
    }
    return InsertParagraphResult(&rightListItemElement,
                                 std::move(atFoundElement));
  }

  // If we reached a block boundary (end of the list item or a child block),
  // let's put deepest start of the list item or the child block.
  if (forwardScanFromStartOfListItemResult.ReachedBlockBoundary() ||
      // FIXME: This is wrong considering because the inline editing host may
      // be surrounded by visible inline content.  However, WSRunScanner is
      // not aware of block boundary around it and stopping this change causes
      // starting to fail some WPT.  Therefore, we need to keep doing this for
      // now.
      forwardScanFromStartOfListItemResult.ReachedInlineEditingHostBoundary()) {
    return InsertParagraphResult(
        &rightListItemElement,
        HTMLEditUtils::GetDeepestEditableStartPointOf<EditorDOMPoint>(
            forwardScanFromStartOfListItemResult.GetContent()
                ? *forwardScanFromStartOfListItemResult.GetContent()
                : rightListItemElement));
  }

  // Otherwise, return the point at first visible thing.
  // XXX This may be not meaningful position if it reached block element
  //     in aListItemElement.
  return InsertParagraphResult(&rightListItemElement,
                               forwardScanFromStartOfListItemResult
                                   .PointAtReachedContent<EditorDOMPoint>());
}

Result<CreateElementResult, nsresult>
HTMLEditor::WrapContentsInBlockquoteElementsWithTransaction(
    const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // The idea here is to put the nodes into a minimal number of blockquotes.
  // When the user blockquotes something, they expect one blockquote.  That
  // may not be possible (for instance, if they have two table cells selected,
  // you need two blockquotes inside the cells).
  RefPtr<Element> curBlock, blockElementToPutCaret;
  nsCOMPtr<nsINode> prevParent;

  EditorDOMPoint pointToPutCaret;
  for (auto& content : aArrayOfContents) {
    // If the node is a table element or list item, dive inside
    if (HTMLEditUtils::IsAnyTableElementButNotTable(content) ||
        HTMLEditUtils::IsListItem(content)) {
      // Forget any previous block
      curBlock = nullptr;
      // Recursion time
      AutoTArray<OwningNonNull<nsIContent>, 24> childContents;
      HTMLEditUtils::CollectAllChildren(*content, childContents);
      Result<CreateElementResult, nsresult>
          wrapChildrenInAnotherBlockquoteResult =
              WrapContentsInBlockquoteElementsWithTransaction(childContents,
                                                              aEditingHost);
      if (MOZ_UNLIKELY(wrapChildrenInAnotherBlockquoteResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::WrapContentsInBlockquoteElementsWithTransaction() "
            "failed");
        return wrapChildrenInAnotherBlockquoteResult;
      }
      CreateElementResult unwrappedWrapChildrenInAnotherBlockquoteResult =
          wrapChildrenInAnotherBlockquoteResult.unwrap();
      unwrappedWrapChildrenInAnotherBlockquoteResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      if (unwrappedWrapChildrenInAnotherBlockquoteResult.GetNewNode()) {
        blockElementToPutCaret =
            unwrappedWrapChildrenInAnotherBlockquoteResult.UnwrapNewNode();
      }
    }

    // If the node has different parent than previous node, further nodes in a
    // new parent
    if (prevParent) {
      if (prevParent != content->GetParentNode()) {
        // Forget any previous blockquote node we were using
        curBlock = nullptr;
        prevParent = content->GetParentNode();
      }
    } else {
      prevParent = content->GetParentNode();
    }

    // If no curBlock, make one
    if (!curBlock) {
      Result<CreateElementResult, nsresult> createNewBlockquoteElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              *nsGkAtoms::blockquote, EditorDOMPoint(content),
              BRElementNextToSplitPoint::Keep, aEditingHost);
      if (MOZ_UNLIKELY(createNewBlockquoteElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "nsGkAtoms::blockquote) failed");
        return createNewBlockquoteElementResult;
      }
      CreateElementResult unwrappedCreateNewBlockquoteElementResult =
          createNewBlockquoteElementResult.unwrap();
      unwrappedCreateNewBlockquoteElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      MOZ_ASSERT(unwrappedCreateNewBlockquoteElementResult.GetNewNode());
      blockElementToPutCaret =
          unwrappedCreateNewBlockquoteElementResult.GetNewNode();
      curBlock = unwrappedCreateNewBlockquoteElementResult.UnwrapNewNode();
    }

    // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to/ keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeToEndWithTransaction(MOZ_KnownLive(content), *curBlock);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
    unwrappedMoveNodeResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  }
  return blockElementToPutCaret
             ? CreateElementResult(std::move(blockElementToPutCaret),
                                   std::move(pointToPutCaret))
             : CreateElementResult::NotHandled(std::move(pointToPutCaret));
}

Result<EditorDOMPoint, nsresult>
HTMLEditor::RemoveBlockContainerElementsWithTransaction(
    const nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    FormatBlockMode aFormatBlockMode, BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aFormatBlockMode == FormatBlockMode::XULParagraphStateCommand);

  // Intent of this routine is to be used for converting to/from headers,
  // paragraphs, pre, and address.  Those blocks that pretty much just contain
  // inline things...
  RefPtr<Element> blockElement;
  nsCOMPtr<nsIContent> firstContent, lastContent;
  EditorDOMPoint pointToPutCaret;
  for (const auto& content : aArrayOfContents) {
    // If the current node is a format element, remove it.
    if (HTMLEditUtils::IsFormatElementForParagraphStateCommand(content)) {
      // Process any partial progress saved
      if (blockElement) {
        Result<SplitRangeOffFromNodeResult, nsresult> unwrapBlockElementResult =
            RemoveBlockContainerElementWithTransactionBetween(
                *blockElement, *firstContent, *lastContent, aBlockInlineCheck);
        if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::RemoveBlockContainerElementWithTransactionBetween() "
              "failed");
          return unwrapBlockElementResult.propagateErr();
        }
        unwrapBlockElementResult.unwrap().MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        firstContent = lastContent = blockElement = nullptr;
      }
      if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
        continue;
      }
      // Remove current block
      Result<EditorDOMPoint, nsresult> unwrapFormatBlockResult =
          RemoveBlockContainerWithTransaction(
              MOZ_KnownLive(*content->AsElement()));
      if (MOZ_UNLIKELY(unwrapFormatBlockResult.isErr())) {
        NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
        return unwrapFormatBlockResult;
      }
      if (unwrapFormatBlockResult.inspect().IsSet()) {
        pointToPutCaret = unwrapFormatBlockResult.unwrap();
      }
      continue;
    }

    // XXX How about, <th>, <thead>, <tfoot>, <dt>, <dl>?
    if (content->IsAnyOfHTMLElements(
            nsGkAtoms::table, nsGkAtoms::tr, nsGkAtoms::tbody, nsGkAtoms::td,
            nsGkAtoms::li, nsGkAtoms::blockquote, nsGkAtoms::div) ||
        HTMLEditUtils::IsAnyListElement(content)) {
      // Process any partial progress saved
      if (blockElement) {
        Result<SplitRangeOffFromNodeResult, nsresult> unwrapBlockElementResult =
            RemoveBlockContainerElementWithTransactionBetween(
                *blockElement, *firstContent, *lastContent, aBlockInlineCheck);
        if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::RemoveBlockContainerElementWithTransactionBetween() "
              "failed");
          return unwrapBlockElementResult.propagateErr();
        }
        unwrapBlockElementResult.unwrap().MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        firstContent = lastContent = blockElement = nullptr;
      }
      if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
        continue;
      }
      // Recursion time
      AutoTArray<OwningNonNull<nsIContent>, 24> childContents;
      HTMLEditUtils::CollectAllChildren(*content, childContents);
      Result<EditorDOMPoint, nsresult> removeBlockContainerElementsResult =
          RemoveBlockContainerElementsWithTransaction(
              childContents, aFormatBlockMode, aBlockInlineCheck);
      if (MOZ_UNLIKELY(removeBlockContainerElementsResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::RemoveBlockContainerElementsWithTransaction() failed");
        return removeBlockContainerElementsResult;
      }
      if (removeBlockContainerElementsResult.inspect().IsSet()) {
        pointToPutCaret = removeBlockContainerElementsResult.unwrap();
      }
      continue;
    }

    if (HTMLEditUtils::IsInlineContent(content, aBlockInlineCheck)) {
      if (blockElement) {
        // If so, is this node a descendant?
        if (EditorUtils::IsDescendantOf(*content, *blockElement)) {
          // Then we don't need to do anything different for this node
          lastContent = content;
          continue;
        }
        // Otherwise, we have progressed beyond end of blockElement, so let's
        // handle it now.  We need to remove the portion of blockElement that
        // contains [firstContent - lastContent].
        Result<SplitRangeOffFromNodeResult, nsresult> unwrapBlockElementResult =
            RemoveBlockContainerElementWithTransactionBetween(
                *blockElement, *firstContent, *lastContent, aBlockInlineCheck);
        if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::RemoveBlockContainerElementWithTransactionBetween() "
              "failed");
          return unwrapBlockElementResult.propagateErr();
        }
        unwrapBlockElementResult.unwrap().MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        firstContent = lastContent = blockElement = nullptr;
        // Fall out and handle content
      }
      blockElement = HTMLEditUtils::GetAncestorElement(
          content, HTMLEditUtils::ClosestEditableBlockElement,
          aBlockInlineCheck);
      if (!blockElement ||
          !HTMLEditUtils::IsFormatElementForParagraphStateCommand(
              *blockElement) ||
          !HTMLEditUtils::IsRemovableNode(*blockElement)) {
        // Not a block kind that we care about.
        blockElement = nullptr;
      } else {
        firstContent = lastContent = content;
      }
      continue;
    }

    if (blockElement) {
      // Some node that is already sans block style.  Skip over it and process
      // any partial progress saved.
      Result<SplitRangeOffFromNodeResult, nsresult> unwrapBlockElementResult =
          RemoveBlockContainerElementWithTransactionBetween(
              *blockElement, *firstContent, *lastContent, aBlockInlineCheck);
      if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::RemoveBlockContainerElementWithTransactionBetween() "
            "failed");
        return unwrapBlockElementResult.propagateErr();
      }
      unwrapBlockElementResult.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      firstContent = lastContent = blockElement = nullptr;
      continue;
    }
  }
  // Process any partial progress saved
  if (blockElement) {
    Result<SplitRangeOffFromNodeResult, nsresult> unwrapBlockElementResult =
        RemoveBlockContainerElementWithTransactionBetween(
            *blockElement, *firstContent, *lastContent, aBlockInlineCheck);
    if (MOZ_UNLIKELY(unwrapBlockElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::RemoveBlockContainerElementWithTransactionBetween() "
          "failed");
      return unwrapBlockElementResult.propagateErr();
    }
    unwrapBlockElementResult.unwrap().MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    firstContent = lastContent = blockElement = nullptr;
  }
  return pointToPutCaret;
}

Result<CreateElementResult, nsresult>
HTMLEditor::CreateOrChangeFormatContainerElement(
    nsTArray<OwningNonNull<nsIContent>>& aArrayOfContents,
    const nsStaticAtom& aNewFormatTagName, FormatBlockMode aFormatBlockMode,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // Intent of this routine is to be used for converting to/from headers,
  // paragraphs, pre, and address.  Those blocks that pretty much just contain
  // inline things...
  RefPtr<Element> newBlock, curBlock, blockElementToPutCaret;
  // If we found a <br> element which should be moved into curBlock, this keeps
  // storing the <br> element after removing it from the tree.
  RefPtr<Element> pendingBRElementToMoveCurBlock;
  EditorDOMPoint pointToPutCaret;
  for (auto& content : aArrayOfContents) {
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
      // If given node has been removed from the document, let's ignore it
      // since the following code may need its parent replace it with new
      // block.
      curBlock = nullptr;
      newBlock = nullptr;
      pendingBRElementToMoveCurBlock = nullptr;
      continue;
    }

    // Is it already the right kind of block, or an uneditable block?
    if (content->IsHTMLElement(&aNewFormatTagName) ||
        (!EditorUtils::IsEditableContent(content, EditorType::HTML) &&
         HTMLEditUtils::IsBlockElement(
             content, BlockInlineCheck::UseHTMLDefaultStyle))) {
      // Forget any previous block used for previous inline nodes
      curBlock = nullptr;
      pendingBRElementToMoveCurBlock = nullptr;
      // Do nothing to this block
      continue;
    }

    // If content is a format element, replace it with a new block of correct
    // type.
    // XXX: pre can't hold everything the others can
    if (HTMLEditUtils::IsMozDiv(content) ||
        HTMLEditor::IsFormatElement(aFormatBlockMode, content)) {
      // Forget any previous block used for previous inline nodes
      curBlock = nullptr;
      pendingBRElementToMoveCurBlock = nullptr;
      RefPtr<Element> expectedContainerOfNewBlock =
          atContent.IsContainerHTMLElement(nsGkAtoms::dl) &&
                  HTMLEditUtils::IsSplittableNode(
                      *atContent.ContainerAs<Element>())
              ? atContent.GetContainerParentAs<Element>()
              : atContent.GetContainerAs<Element>();
      Result<CreateElementResult, nsresult> replaceWithNewBlockElementResult =
          ReplaceContainerAndCloneAttributesWithTransaction(
              MOZ_KnownLive(*content->AsElement()), aNewFormatTagName);
      if (MOZ_UNLIKELY(replaceWithNewBlockElementResult.isErr())) {
        NS_WARNING(
            "EditorBase::ReplaceContainerAndCloneAttributesWithTransaction() "
            "failed");
        return replaceWithNewBlockElementResult;
      }
      CreateElementResult unwrappedReplaceWithNewBlockElementResult =
          replaceWithNewBlockElementResult.unwrap();
      // If the new block element was moved to different element or removed by
      // the web app via mutation event listener, we should stop handling this
      // action since we cannot handle each of a lot of edge cases.
      if (NS_WARN_IF(unwrappedReplaceWithNewBlockElementResult.GetNewNode()
                         ->GetParentNode() != expectedContainerOfNewBlock)) {
        unwrappedReplaceWithNewBlockElementResult.IgnoreCaretPointSuggestion();
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      unwrappedReplaceWithNewBlockElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      newBlock = unwrappedReplaceWithNewBlockElementResult.UnwrapNewNode();
      continue;
    }

    if (HTMLEditUtils::IsTable(content) ||
        HTMLEditUtils::IsAnyListElement(content) ||
        content->IsAnyOfHTMLElements(nsGkAtoms::tbody, nsGkAtoms::tr,
                                     nsGkAtoms::td, nsGkAtoms::li,
                                     nsGkAtoms::blockquote, nsGkAtoms::div)) {
      // Forget any previous block used for previous inline nodes
      curBlock = nullptr;
      pendingBRElementToMoveCurBlock = nullptr;
      // Recursion time
      AutoTArray<OwningNonNull<nsIContent>, 24> childContents;
      HTMLEditUtils::CollectAllChildren(*content, childContents);
      if (!childContents.IsEmpty()) {
        Result<CreateElementResult, nsresult> wrapChildrenInBlockElementResult =
            CreateOrChangeFormatContainerElement(
                childContents, aNewFormatTagName, aFormatBlockMode,
                aEditingHost);
        if (MOZ_UNLIKELY(wrapChildrenInBlockElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::CreateOrChangeFormatContainerElement() failed");
          return wrapChildrenInBlockElementResult;
        }
        CreateElementResult unwrappedWrapChildrenInBlockElementResult =
            wrapChildrenInBlockElementResult.unwrap();
        unwrappedWrapChildrenInBlockElementResult.MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        if (unwrappedWrapChildrenInBlockElementResult.GetNewNode()) {
          blockElementToPutCaret =
              unwrappedWrapChildrenInBlockElementResult.UnwrapNewNode();
        }
        continue;
      }

      // Make sure we can put a block here
      Result<CreateElementResult, nsresult> createNewBlockElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              aNewFormatTagName, atContent, BRElementNextToSplitPoint::Keep,
              aEditingHost);
      if (MOZ_UNLIKELY(createNewBlockElementResult.isErr())) {
        NS_WARNING(
            nsPrintfCString(
                "HTMLEditor::"
                "InsertElementWithSplittingAncestorsWithTransaction(%s) failed",
                nsAtomCString(&aNewFormatTagName).get())
                .get());
        return createNewBlockElementResult;
      }
      CreateElementResult unwrappedCreateNewBlockElementResult =
          createNewBlockElementResult.unwrap();
      unwrappedCreateNewBlockElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      MOZ_ASSERT(unwrappedCreateNewBlockElementResult.GetNewNode());
      blockElementToPutCaret =
          unwrappedCreateNewBlockElementResult.UnwrapNewNode();
      continue;
    }

    if (content->IsHTMLElement(nsGkAtoms::br)) {
      if (curBlock) {
        if (aFormatBlockMode == FormatBlockMode::XULParagraphStateCommand) {
          // If the node is a break, we honor it by putting further nodes in a
          // new parent.

          // Forget any previous block used for previous inline nodes.
          curBlock = nullptr;
          pendingBRElementToMoveCurBlock = nullptr;
        } else {
          // If the node is a break, we need to move it into end of the curBlock
          // if we'll move following content into curBlock.
          pendingBRElementToMoveCurBlock = content->AsElement();
        }
        // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it
        // alive.
        nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*content));
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return Err(rv);
        }
        continue;
      }

      // The break is the first (or even only) node we encountered.  Create a
      // block for it.
      Result<CreateElementResult, nsresult> createNewBlockElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              aNewFormatTagName, atContent, BRElementNextToSplitPoint::Keep,
              aEditingHost);
      if (MOZ_UNLIKELY(createNewBlockElementResult.isErr())) {
        NS_WARNING(nsPrintfCString("HTMLEditor::"
                                   "InsertElementWithSplittingAncestorsWith"
                                   "Transaction(%s) failed",
                                   nsAtomCString(&aNewFormatTagName).get())
                       .get());
        return createNewBlockElementResult;
      }
      CreateElementResult unwrappedCreateNewBlockElementResult =
          createNewBlockElementResult.unwrap();
      unwrappedCreateNewBlockElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      RefPtr<Element> newBlockElement =
          unwrappedCreateNewBlockElementResult.UnwrapNewNode();
      MOZ_ASSERT(newBlockElement);
      blockElementToPutCaret = newBlockElement;
      // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it
      // alive.
      Result<MoveNodeResult, nsresult> moveNodeResult =
          MoveNodeToEndWithTransaction(MOZ_KnownLive(content),
                                       *newBlockElement);
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      unwrappedMoveNodeResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      curBlock = std::move(newBlockElement);
      continue;
    }

    if (HTMLEditUtils::IsInlineContent(content,
                                       BlockInlineCheck::UseHTMLDefaultStyle)) {
      // If content is inline, pull it into curBlock.  Note: it's assumed that
      // consecutive inline nodes in aNodeArray are actually members of the
      // same block parent.  This happens to be true now as a side effect of
      // how aNodeArray is constructed, but some additional logic should be
      // added here if that should change
      //
      // If content is a non editable, drop it if we are going to <pre>.
      if (&aNewFormatTagName == nsGkAtoms::pre &&
          !EditorUtils::IsEditableContent(content, EditorType::HTML)) {
        // Do nothing to this block
        continue;
      }

      // If no curBlock, make one
      if (!curBlock) {
        Result<CreateElementResult, nsresult> createNewBlockElementResult =
            InsertElementWithSplittingAncestorsWithTransaction(
                aNewFormatTagName, atContent, BRElementNextToSplitPoint::Keep,
                aEditingHost);
        if (MOZ_UNLIKELY(createNewBlockElementResult.isErr())) {
          NS_WARNING(nsPrintfCString("HTMLEditor::"
                                     "InsertElementWithSplittingAncestorsWith"
                                     "Transaction(%s) failed",
                                     nsAtomCString(&aNewFormatTagName).get())
                         .get());
          return createNewBlockElementResult;
        }
        CreateElementResult unwrappedCreateNewBlockElementResult =
            createNewBlockElementResult.unwrap();
        unwrappedCreateNewBlockElementResult.MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        MOZ_ASSERT(unwrappedCreateNewBlockElementResult.GetNewNode());
        blockElementToPutCaret =
            unwrappedCreateNewBlockElementResult.GetNewNode();
        curBlock = unwrappedCreateNewBlockElementResult.UnwrapNewNode();

        // Update container of content.
        atContent.Set(content);
        if (NS_WARN_IF(!atContent.IsSet())) {
          // This is possible due to mutation events, let's not assert
          return Err(NS_ERROR_UNEXPECTED);
        }
      } else if (pendingBRElementToMoveCurBlock) {
        Result<CreateElementResult, nsresult> insertBRElementResult =
            InsertNodeWithTransaction<Element>(
                *pendingBRElementToMoveCurBlock,
                EditorDOMPoint::AtEndOf(*curBlock));
        if (MOZ_UNLIKELY(insertBRElementResult.isErr())) {
          NS_WARNING("EditorBase::InsertNodeWithTransaction<Element>() failed");
          return insertBRElementResult.propagateErr();
        }
        insertBRElementResult.inspect().IgnoreCaretPointSuggestion();
        pendingBRElementToMoveCurBlock = nullptr;
      }

      // XXX If content is a br, replace it with a return if going to <pre>

      // This is a continuation of some inline nodes that belong together in
      // the same block item.  Use curBlock.
      //
      // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it
      // alive.  We could try to make that a rvalue ref and create a const array
      // on the stack here, but callers are passing in auto arrays, and we don't
      // want to introduce copies..
      Result<MoveNodeResult, nsresult> moveNodeResult =
          MoveNodeToEndWithTransaction(MOZ_KnownLive(content), *curBlock);
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      unwrappedMoveNodeResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    }
  }
  return blockElementToPutCaret
             ? CreateElementResult(std::move(blockElementToPutCaret),
                                   std::move(pointToPutCaret))
             : CreateElementResult::NotHandled(std::move(pointToPutCaret));
}

Result<SplitNodeResult, nsresult>
HTMLEditor::MaybeSplitAncestorsForInsertWithTransaction(
    const nsAtom& aTag, const EditorDOMPoint& aStartOfDeepestRightNode,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aEditingHost.IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (NS_WARN_IF(!aStartOfDeepestRightNode.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }
  MOZ_ASSERT(aStartOfDeepestRightNode.IsSetAndValid());

  // The point must be descendant of editing host.
  // XXX Isn't it a valid case if it points a direct child of aEditingHost?
  if (NS_WARN_IF(
          !aStartOfDeepestRightNode.GetContainer()->IsInclusiveDescendantOf(
              &aEditingHost))) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  // Look for a node that can legally contain the tag.
  const EditorDOMPoint pointToInsert =
      HTMLEditUtils::GetInsertionPointInInclusiveAncestor(
          aTag, aStartOfDeepestRightNode, &aEditingHost);
  if (MOZ_UNLIKELY(!pointToInsert.IsSet())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitAncestorsForInsertWithTransaction() reached "
        "editing host");
    return Err(NS_ERROR_FAILURE);
  }
  // If the point itself can contain the tag, we don't need to split any
  // ancestor nodes.  In this case, we should return the given split point
  // as is.
  if (pointToInsert.GetContainer() == aStartOfDeepestRightNode.GetContainer()) {
    return SplitNodeResult::NotHandled(aStartOfDeepestRightNode);
  }

  Result<SplitNodeResult, nsresult> splitNodeResult =
      SplitNodeDeepWithTransaction(MOZ_KnownLive(*pointToInsert.GetChild()),
                                   aStartOfDeepestRightNode,
                                   SplitAtEdges::eAllowToCreateEmptyContainer);
  NS_WARNING_ASSERTION(splitNodeResult.isOk(),
                       "HTMLEditor::SplitNodeDeepWithTransaction(SplitAtEdges::"
                       "eAllowToCreateEmptyContainer) failed");
  return splitNodeResult;
}

Result<CreateElementResult, nsresult>
HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction(
    const nsAtom& aTagName, const EditorDOMPoint& aPointToInsert,
    BRElementNextToSplitPoint aBRElementNextToSplitPoint,
    const Element& aEditingHost,
    const InitializeInsertingElement& aInitializer) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  const nsCOMPtr<nsIContent> childAtPointToInsert = aPointToInsert.GetChild();
  Result<SplitNodeResult, nsresult> splitNodeResult =
      MaybeSplitAncestorsForInsertWithTransaction(aTagName, aPointToInsert,
                                                  aEditingHost);
  if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitAncestorsForInsertWithTransaction() failed");
    return splitNodeResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
  DebugOnly<bool> wasCaretPositionSuggestedAtSplit =
      unwrappedSplitNodeResult.HasCaretPointSuggestion();
  // We'll update selection below, and nobody touches selection until then.
  // Therefore, we don't need to touch selection here.
  unwrappedSplitNodeResult.IgnoreCaretPointSuggestion();

  // If current handling node has been moved from the container by a
  // mutation event listener when we need to do something more for it,
  // we should stop handling this action since we cannot handle each
  // edge case.
  if (childAtPointToInsert &&
      NS_WARN_IF(!childAtPointToInsert->IsInclusiveDescendantOf(
          unwrappedSplitNodeResult.DidSplit()
              ? unwrappedSplitNodeResult.GetNextContent()
              : aPointToInsert.GetContainer()))) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  auto splitPoint = unwrappedSplitNodeResult.AtSplitPoint<EditorDOMPoint>();
  if (aBRElementNextToSplitPoint == BRElementNextToSplitPoint::Delete) {
    // Consume a trailing br, if any.  This is to keep an alignment from
    // creating extra lines, if possible.
    if (nsCOMPtr<nsIContent> maybeBRContent = HTMLEditUtils::GetNextContent(
            splitPoint,
            {WalkTreeOption::IgnoreNonEditableNode,
             WalkTreeOption::StopAtBlockBoundary},
            BlockInlineCheck::UseComputedDisplayOutsideStyle, &aEditingHost)) {
      if (maybeBRContent->IsHTMLElement(nsGkAtoms::br) &&
          splitPoint.GetChild()) {
        // Making use of html structure... if next node after where we are
        // putting our div is not a block, then the br we found is in same
        // block we are, so it's safe to consume it.
        if (nsIContent* nextEditableSibling = HTMLEditUtils::GetNextSibling(
                *splitPoint.GetChild(),
                {WalkTreeOption::IgnoreNonEditableNode})) {
          if (!HTMLEditUtils::IsBlockElement(
                  *nextEditableSibling,
                  BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
            AutoEditorDOMPointChildInvalidator lockOffset(splitPoint);
            nsresult rv = DeleteNodeWithTransaction(*maybeBRContent);
            if (NS_FAILED(rv)) {
              NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
              return Err(rv);
            }
          }
        }
      }
    }
  }

  Result<CreateElementResult, nsresult> createNewElementResult =
      CreateAndInsertElement(WithTransaction::Yes, aTagName, splitPoint,
                             aInitializer);
  if (MOZ_UNLIKELY(createNewElementResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) failed");
    return createNewElementResult;
  }
  MOZ_ASSERT_IF(wasCaretPositionSuggestedAtSplit,
                createNewElementResult.inspect().HasCaretPointSuggestion());
  MOZ_ASSERT(createNewElementResult.inspect().GetNewNode());

  // If the new block element was moved to different element or removed by
  // the web app via mutation event listener, we should stop handling this
  // action since we cannot handle each of a lot of edge cases.
  if (NS_WARN_IF(
          createNewElementResult.inspect().GetNewNode()->GetParentNode() !=
          splitPoint.GetContainer())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  return createNewElementResult;
}

nsresult HTMLEditor::JoinNearestEditableNodesWithTransaction(
    nsIContent& aNodeLeft, nsIContent& aNodeRight,
    EditorDOMPoint* aNewFirstChildOfRightNode) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aNewFirstChildOfRightNode);

  // Caller responsible for left and right node being the same type
  if (NS_WARN_IF(!aNodeLeft.GetParentNode())) {
    return NS_ERROR_FAILURE;
  }
  // If they don't have the same parent, first move the right node to after
  // the left one
  if (aNodeLeft.GetParentNode() != aNodeRight.GetParentNode()) {
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeWithTransaction(aNodeRight, EditorDOMPoint(&aNodeLeft));
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
      return moveNodeResult.unwrapErr();
    }
    nsresult rv = moveNodeResult.inspect().SuggestCaretPointTo(
        *this, {SuggestCaret::OnlyIfHasSuggestion,
                SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
  }

  // Separate join rules for differing blocks
  if (HTMLEditUtils::IsAnyListElement(&aNodeLeft) || aNodeLeft.IsText()) {
    // For lists, merge shallow (wouldn't want to combine list items)
    Result<JoinNodesResult, nsresult> joinNodesResult =
        JoinNodesWithTransaction(aNodeLeft, aNodeRight);
    if (MOZ_UNLIKELY(joinNodesResult.isErr())) {
      NS_WARNING("HTMLEditor::JoinNodesWithTransaction failed");
      return joinNodesResult.unwrapErr();
    }
    *aNewFirstChildOfRightNode =
        joinNodesResult.inspect().AtJoinedPoint<EditorDOMPoint>();
    return NS_OK;
  }

  // Remember the last left child, and first right child
  nsCOMPtr<nsIContent> lastEditableChildOfLeftContent =
      HTMLEditUtils::GetLastChild(aNodeLeft,
                                  {WalkTreeOption::IgnoreNonEditableNode});
  if (MOZ_UNLIKELY(NS_WARN_IF(!lastEditableChildOfLeftContent))) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIContent> firstEditableChildOfRightContent =
      HTMLEditUtils::GetFirstChild(aNodeRight,
                                   {WalkTreeOption::IgnoreNonEditableNode});
  if (NS_WARN_IF(!firstEditableChildOfRightContent)) {
    return NS_ERROR_FAILURE;
  }

  // For list items, divs, etc., merge smart
  Result<JoinNodesResult, nsresult> joinNodesResult =
      JoinNodesWithTransaction(aNodeLeft, aNodeRight);
  if (MOZ_UNLIKELY(joinNodesResult.isErr())) {
    NS_WARNING("HTMLEditor::JoinNodesWithTransaction() failed");
    return joinNodesResult.unwrapErr();
  }

  if ((lastEditableChildOfLeftContent->IsText() ||
       lastEditableChildOfLeftContent->IsElement()) &&
      HTMLEditUtils::CanContentsBeJoined(*lastEditableChildOfLeftContent,
                                         *firstEditableChildOfRightContent)) {
    nsresult rv = JoinNearestEditableNodesWithTransaction(
        *lastEditableChildOfLeftContent, *firstEditableChildOfRightContent,
        aNewFirstChildOfRightNode);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::JoinNearestEditableNodesWithTransaction() failed");
    return rv;
  }
  *aNewFirstChildOfRightNode =
      joinNodesResult.inspect().AtJoinedPoint<EditorDOMPoint>();
  return NS_OK;
}

Element* HTMLEditor::GetMostDistantAncestorMailCiteElement(
    const nsINode& aNode) const {
  Element* mailCiteElement = nullptr;
  const bool isPlaintextEditor = IsPlaintextMailComposer();
  for (Element* element : aNode.InclusiveAncestorsOfType<Element>()) {
    if ((isPlaintextEditor && element->IsHTMLElement(nsGkAtoms::pre)) ||
        HTMLEditUtils::IsMailCite(*element)) {
      mailCiteElement = element;
      continue;
    }
    if (element->IsHTMLElement(nsGkAtoms::body)) {
      break;
    }
  }
  return mailCiteElement;
}

nsresult HTMLEditor::CacheInlineStyles(Element& Element) {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  nsresult rv = GetInlineStyles(
      Element, *TopLevelEditSubActionDataRef().mCachedPendingStyles);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetInlineStyles() failed");
  return rv;
}

nsresult HTMLEditor::GetInlineStyles(
    Element& aElement, AutoPendingStyleCacheArray& aPendingStyleCacheArray) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPendingStyleCacheArray.IsEmpty());

  if (!IsCSSEnabled()) {
    // In the HTML styling mode, we should preserve the order of inline styles
    // specified with HTML elements, then, we can keep same order as original
    // one when we create new elements to apply the styles at new place.
    // XXX Currently, we don't preserve all inline parents, therefore, we cannot
    //     restore all inline elements as-is.  Perhaps, we should store all
    //     inline elements with more details (e.g., all attributes), and store
    //     same elements.  For example, web apps may give style as:
    //     em {
    //       font-style: italic;
    //     }
    //     em em {
    //       font-style: normal;
    //       font-weight: bold;
    //     }
    //     but we cannot restore the style as-is.
    nsString value;
    const bool givenElementIsEditable =
        HTMLEditUtils::IsSimplyEditableNode(aElement);
    auto NeedToAppend = [&](nsStaticAtom& aTagName, nsStaticAtom* aAttribute) {
      if (mPendingStylesToApplyToNewContent->GetStyleState(
              aTagName, aAttribute) != PendingStyleState::NotUpdated) {
        return false;  // The style has already been changed.
      }
      if (aPendingStyleCacheArray.Contains(aTagName, aAttribute)) {
        return false;  // Already preserved
      }
      return true;
    };
    for (Element* const inclusiveAncestor :
         aElement.InclusiveAncestorsOfType<Element>()) {
      if (HTMLEditUtils::IsBlockElement(
              *inclusiveAncestor,
              BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
          (givenElementIsEditable &&
           !HTMLEditUtils::IsSimplyEditableNode(*inclusiveAncestor))) {
        break;
      }
      if (inclusiveAncestor->IsAnyOfHTMLElements(
              nsGkAtoms::b, nsGkAtoms::i, nsGkAtoms::u, nsGkAtoms::s,
              nsGkAtoms::strike, nsGkAtoms::tt, nsGkAtoms::em,
              nsGkAtoms::strong, nsGkAtoms::dfn, nsGkAtoms::code,
              nsGkAtoms::samp, nsGkAtoms::var, nsGkAtoms::cite, nsGkAtoms::abbr,
              nsGkAtoms::acronym, nsGkAtoms::sub, nsGkAtoms::sup)) {
        nsStaticAtom& tagName = const_cast<nsStaticAtom&>(
            *inclusiveAncestor->NodeInfo()->NameAtom()->AsStatic());
        if (NeedToAppend(tagName, nullptr)) {
          aPendingStyleCacheArray.AppendElement(
              PendingStyleCache(tagName, nullptr, EmptyString()));
        }
        continue;
      }
      if (inclusiveAncestor->IsHTMLElement(nsGkAtoms::font)) {
        if (NeedToAppend(*nsGkAtoms::font, nsGkAtoms::face)) {
          inclusiveAncestor->GetAttr(nsGkAtoms::face, value);
          if (!value.IsEmpty()) {
            aPendingStyleCacheArray.AppendElement(
                PendingStyleCache(*nsGkAtoms::font, nsGkAtoms::face, value));
            value.Truncate();
          }
        }
        if (NeedToAppend(*nsGkAtoms::font, nsGkAtoms::size)) {
          inclusiveAncestor->GetAttr(nsGkAtoms::size, value);
          if (!value.IsEmpty()) {
            aPendingStyleCacheArray.AppendElement(
                PendingStyleCache(*nsGkAtoms::font, nsGkAtoms::size, value));
            value.Truncate();
          }
        }
        if (NeedToAppend(*nsGkAtoms::font, nsGkAtoms::color)) {
          inclusiveAncestor->GetAttr(nsGkAtoms::color, value);
          if (!value.IsEmpty()) {
            aPendingStyleCacheArray.AppendElement(
                PendingStyleCache(*nsGkAtoms::font, nsGkAtoms::color, value));
            value.Truncate();
          }
        }
        continue;
      }
    }
    return NS_OK;
  }

  for (nsStaticAtom* property : {nsGkAtoms::b,
                                 nsGkAtoms::i,
                                 nsGkAtoms::u,
                                 nsGkAtoms::s,
                                 nsGkAtoms::strike,
                                 nsGkAtoms::face,
                                 nsGkAtoms::size,
                                 nsGkAtoms::color,
                                 nsGkAtoms::tt,
                                 nsGkAtoms::em,
                                 nsGkAtoms::strong,
                                 nsGkAtoms::dfn,
                                 nsGkAtoms::code,
                                 nsGkAtoms::samp,
                                 nsGkAtoms::var,
                                 nsGkAtoms::cite,
                                 nsGkAtoms::abbr,
                                 nsGkAtoms::acronym,
                                 nsGkAtoms::backgroundColor,
                                 nsGkAtoms::sub,
                                 nsGkAtoms::sup}) {
    const EditorInlineStyle style =
        property == nsGkAtoms::face || property == nsGkAtoms::size ||
                property == nsGkAtoms::color
            ? EditorInlineStyle(*nsGkAtoms::font, property)
            : EditorInlineStyle(*property);
    // If type-in state is set, don't intervene
    const PendingStyleState styleState =
        mPendingStylesToApplyToNewContent->GetStyleState(*style.mHTMLProperty,
                                                         style.mAttribute);
    if (styleState != PendingStyleState::NotUpdated) {
      continue;
    }
    bool isSet = false;
    nsString value;  // Don't use nsAutoString here because it requires memcpy
                     // at creating new PendingStyleCache instance.
    // Don't use CSS for <font size>, we don't support it usefully (bug 780035)
    if (property == nsGkAtoms::size) {
      isSet = HTMLEditUtils::IsInlineStyleSetByElement(aElement, style, nullptr,
                                                       &value);
    } else if (style.IsCSSSettable(aElement)) {
      Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
          CSSEditUtils::IsComputedCSSEquivalentTo(*this, aElement, style,
                                                  value);
      if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
        NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
        return isComputedCSSEquivalentToStyleOrError.unwrapErr();
      }
      isSet = isComputedCSSEquivalentToStyleOrError.unwrap();
    }
    if (isSet) {
      aPendingStyleCacheArray.AppendElement(
          style.ToPendingStyleCache(std::move(value)));
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::ReapplyCachedStyles() {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // The idea here is to examine our cached list of styles and see if any have
  // been removed.  If so, add typeinstate for them, so that they will be
  // reinserted when new content is added.

  if (TopLevelEditSubActionDataRef().mCachedPendingStyles->IsEmpty() ||
      !SelectionRef().RangeCount()) {
    return NS_OK;
  }

  // remember if we are in css mode
  const bool useCSS = IsCSSEnabled();

  const RangeBoundary& atStartOfSelection =
      SelectionRef().GetRangeAt(0)->StartRef();
  const RefPtr<Element> startContainerElement =
      atStartOfSelection.GetContainer() &&
              atStartOfSelection.GetContainer()->IsContent()
          ? atStartOfSelection.GetContainer()->GetAsElementOrParentElement()
          : nullptr;
  if (NS_WARN_IF(!startContainerElement)) {
    return NS_OK;
  }

  AutoPendingStyleCacheArray styleCacheArrayAtInsertionPoint;
  nsresult rv =
      GetInlineStyles(*startContainerElement, styleCacheArrayAtInsertionPoint);
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetInlineStyles() failed, but ignored");
    return NS_OK;
  }

  for (PendingStyleCache& styleCacheBeforeEdit :
       Reversed(*TopLevelEditSubActionDataRef().mCachedPendingStyles)) {
    bool isFirst = false, isAny = false, isAll = false;
    nsAutoString currentValue;
    const EditorInlineStyle inlineStyle = styleCacheBeforeEdit.ToInlineStyle();
    if (useCSS && inlineStyle.IsCSSSettable(*startContainerElement)) {
      // check computed style first in css case
      // MOZ_KnownLive(styleCacheBeforeEdit.*) because they are nsStaticAtom
      // and its instances are alive until shutting down.
      Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
          CSSEditUtils::IsComputedCSSEquivalentTo(*this, *startContainerElement,
                                                  inlineStyle, currentValue);
      if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
        NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
        return isComputedCSSEquivalentToStyleOrError.unwrapErr();
      }
      isAny = isComputedCSSEquivalentToStyleOrError.unwrap();
    }
    if (!isAny) {
      // then check typeinstate and html style
      nsresult rv = GetInlinePropertyBase(
          inlineStyle, &styleCacheBeforeEdit.AttributeValueOrCSSValueRef(),
          &isFirst, &isAny, &isAll, &currentValue);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::GetInlinePropertyBase() failed");
        return rv;
      }
    }
    // This style has disappeared through deletion.  Let's add the styles to
    // mPendingStylesToApplyToNewContent when same style isn't applied to the
    // node already.
    if (isAny &&
        !IsPendingStyleCachePreservingSubAction(GetTopLevelEditSubAction())) {
      continue;
    }
    AutoPendingStyleCacheArray::index_type index =
        styleCacheArrayAtInsertionPoint.IndexOf(
            styleCacheBeforeEdit.TagRef(), styleCacheBeforeEdit.GetAttribute());
    if (index == AutoPendingStyleCacheArray::NoIndex ||
        styleCacheBeforeEdit.AttributeValueOrCSSValueRef() !=
            styleCacheArrayAtInsertionPoint.ElementAt(index)
                .AttributeValueOrCSSValueRef()) {
      mPendingStylesToApplyToNewContent->PreserveStyle(styleCacheBeforeEdit);
    }
  }
  return NS_OK;
}

nsresult HTMLEditor::InsertBRElementToEmptyListItemsAndTableCellsInRange(
    const RawRangeBoundary& aStartRef, const RawRangeBoundary& aEndRef) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoTArray<OwningNonNull<Element>, 64> arrayOfEmptyElements;
  DOMIterator iter;
  if (NS_FAILED(iter.Init(aStartRef, aEndRef))) {
    NS_WARNING("DOMIterator::Init() failed");
    return NS_ERROR_FAILURE;
  }
  iter.AppendNodesToArray(
      +[](nsINode& aNode, void* aSelf) {
        MOZ_ASSERT(Element::FromNode(&aNode));
        MOZ_ASSERT(aSelf);
        Element* element = aNode.AsElement();
        if (!EditorUtils::IsEditableContent(*element, EditorType::HTML) ||
            (!HTMLEditUtils::IsListItem(element) &&
             !HTMLEditUtils::IsTableCellOrCaption(*element))) {
          return false;
        }
        return HTMLEditUtils::IsEmptyNode(
            *element, {EmptyCheckOption::TreatSingleBRElementAsVisible,
                       EmptyCheckOption::TreatNonEditableContentAsInvisible});
      },
      arrayOfEmptyElements, this);

  // Put padding <br> elements for empty <li> and <td>.
  EditorDOMPoint pointToPutCaret;
  for (auto& emptyElement : arrayOfEmptyElements) {
    // Need to put br at END of node.  It may have empty containers in it and
    // still pass the "IsEmptyNode" test, and we want the br's to be after
    // them.  Also, we want the br to be after the selection if the selection
    // is in this node.
    EditorDOMPoint endOfNode(EditorDOMPoint::AtEndOf(emptyElement));
    Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
        InsertPaddingBRElementForEmptyLastLineWithTransaction(endOfNode);
    if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementForEmptyLastLineWithTransaction() "
          "failed");
      return insertPaddingBRElementResult.unwrapErr();
    }
    CreateElementResult unwrappedInsertPaddingBRElementResult =
        insertPaddingBRElementResult.unwrap();
    unwrappedInsertPaddingBRElementResult.MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
  }
  if (pointToPutCaret.IsSet()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionTo() caused destroying the editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
  }
  return NS_OK;
}

void HTMLEditor::SetSelectionInterlinePosition() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(SelectionRef().IsCollapsed());

  // Get the (collapsed) selection location
  const nsRange* firstRange = SelectionRef().GetRangeAt(0);
  if (NS_WARN_IF(!firstRange)) {
    return;
  }

  EditorDOMPoint atCaret(firstRange->StartRef());
  if (NS_WARN_IF(!atCaret.IsSet())) {
    return;
  }
  MOZ_ASSERT(atCaret.IsSetAndValid());

  // First, let's check to see if we are after a `<br>`.  We take care of this
  // special-case first so that we don't accidentally fall through into one of
  // the other conditionals.
  // XXX Although I don't understand "interline position", if caret is
  //     immediately after non-editable contents, but previous editable
  //     content is `<br>`, does this do right thing?
  if (Element* editingHost = ComputeEditingHost()) {
    if (nsIContent* previousEditableContentInBlock =
            HTMLEditUtils::GetPreviousContent(
                atCaret,
                {WalkTreeOption::IgnoreNonEditableNode,
                 WalkTreeOption::StopAtBlockBoundary},
                BlockInlineCheck::UseComputedDisplayStyle, editingHost)) {
      if (previousEditableContentInBlock->IsHTMLElement(nsGkAtoms::br)) {
        DebugOnly<nsresult> rvIgnored = SelectionRef().SetInterlinePosition(
            InterlinePosition::StartOfNextLine);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "Selection::SetInterlinePosition(InterlinePosition::"
            "StartOfNextLine) failed, but ignored");
        return;
      }
    }
  }

  if (!atCaret.GetChild()) {
    return;
  }

  // If caret is immediately after a block, set interline position to "right".
  // XXX Although I don't understand "interline position", if caret is
  //     immediately after non-editable contents, but previous editable
  //     content is a block, does this do right thing?
  if (nsIContent* previousEditableContentInBlockAtCaret =
          HTMLEditUtils::GetPreviousSibling(
              *atCaret.GetChild(), {WalkTreeOption::IgnoreNonEditableNode})) {
    if (HTMLEditUtils::IsBlockElement(
            *previousEditableContentInBlockAtCaret,
            BlockInlineCheck::UseComputedDisplayStyle)) {
      DebugOnly<nsresult> rvIgnored = SelectionRef().SetInterlinePosition(
          InterlinePosition::StartOfNextLine);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Selection::SetInterlinePosition(InterlinePosition::"
                           "StartOfNextLine) failed, but ignored");
      return;
    }
  }

  // If caret is immediately before a block, set interline position to "left".
  // XXX Although I don't understand "interline position", if caret is
  //     immediately before non-editable contents, but next editable
  //     content is a block, does this do right thing?
  if (nsIContent* nextEditableContentInBlockAtCaret =
          HTMLEditUtils::GetNextSibling(
              *atCaret.GetChild(), {WalkTreeOption::IgnoreNonEditableNode})) {
    if (HTMLEditUtils::IsBlockElement(
            *nextEditableContentInBlockAtCaret,
            BlockInlineCheck::UseComputedDisplayStyle)) {
      DebugOnly<nsresult> rvIgnored =
          SelectionRef().SetInterlinePosition(InterlinePosition::EndOfLine);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Selection::SetInterlinePosition(InterlinePosition::"
                           "EndOfLine) failed, but ignored");
    }
  }
}

nsresult HTMLEditor::AdjustCaretPositionAndEnsurePaddingBRElement(
    nsIEditor::EDirection aDirectionAndAmount) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(SelectionRef().IsCollapsed());

  auto point = GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!point.IsInContentNode())) {
    return NS_ERROR_FAILURE;
  }

  // If selection start is not editable, climb up the tree until editable one.
  while (!EditorUtils::IsEditableContent(*point.ContainerAs<nsIContent>(),
                                         EditorType::HTML)) {
    point.Set(point.GetContainer());
    if (NS_WARN_IF(!point.IsInContentNode())) {
      return NS_ERROR_FAILURE;
    }
  }

  // If caret is in empty block element, we need to insert a `<br>` element
  // because the block should have one-line height.
  // XXX Even if only a part of the block is editable, shouldn't we put
  //     caret if the block element is now empty?
  if (Element* const editableBlockElement =
          HTMLEditUtils::GetInclusiveAncestorElement(
              *point.ContainerAs<nsIContent>(),
              HTMLEditUtils::ClosestEditableBlockElement,
              BlockInlineCheck::UseComputedDisplayStyle)) {
    if (editableBlockElement &&
        HTMLEditUtils::IsEmptyNode(
            *editableBlockElement,
            {EmptyCheckOption::TreatSingleBRElementAsVisible}) &&
        HTMLEditUtils::CanNodeContain(*point.GetContainer(), *nsGkAtoms::br)) {
      Element* bodyOrDocumentElement = GetRoot();
      if (NS_WARN_IF(!bodyOrDocumentElement)) {
        return NS_ERROR_FAILURE;
      }
      if (point.GetContainer() == bodyOrDocumentElement) {
        // Our root node is completely empty. Don't add a <br> here.
        // AfterEditInner() will add one for us when it calls
        // EditorBase::MaybeCreatePaddingBRElementForEmptyEditor().
        // XXX This kind of dependency between methods makes us spaghetti.
        //     Let's handle it here later.
        // XXX This looks odd check.  If active editing host is not a
        //     `<body>`, what are we doing?
        return NS_OK;
      }
      Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
          InsertPaddingBRElementForEmptyLastLineWithTransaction(point);
      if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertPaddingBRElementForEmptyLastLineWithTransaction("
            ") failed");
        return insertPaddingBRElementResult.unwrapErr();
      }
      nsresult rv = insertPaddingBRElementResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
        return rv;
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CreateElementResult::SuggestCaretPointTo() failed, but ignored");
      return NS_OK;
    }
  }

  // XXX Perhaps, we should do something if we're in a data node but not
  //     a text node.
  if (point.IsInTextNode()) {
    return NS_OK;
  }

  // Do we need to insert a padding <br> element for empty last line?  We do
  // if we are:
  // 1) prior node is in same block where selection is AND
  // 2) prior node is a br AND
  // 3) that br is not visible
  RefPtr<Element> editingHost = ComputeEditingHost();
  if (!editingHost) {
    return NS_OK;
  }

  if (nsCOMPtr<nsIContent> previousEditableContent =
          HTMLEditUtils::GetPreviousContent(
              point, {WalkTreeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayStyle, editingHost)) {
    // If caret and previous editable content are in same block element
    // (even if it's a non-editable element), we should put a padding <br>
    // element at end of the block.
    const Element* const blockElementContainingCaret =
        HTMLEditUtils::GetInclusiveAncestorElement(
            *point.ContainerAs<nsIContent>(),
            HTMLEditUtils::ClosestBlockElement,
            BlockInlineCheck::UseComputedDisplayStyle);
    const Element* const blockElementContainingPreviousEditableContent =
        HTMLEditUtils::GetAncestorElement(
            *previousEditableContent, HTMLEditUtils::ClosestBlockElement,
            BlockInlineCheck::UseComputedDisplayStyle);
    // If previous editable content of caret is in same block and a `<br>`
    // element, we need to adjust interline position.
    if (blockElementContainingCaret &&
        blockElementContainingCaret ==
            blockElementContainingPreviousEditableContent &&
        point.ContainerAs<nsIContent>()->GetEditingHost() ==
            previousEditableContent->GetEditingHost() &&
        previousEditableContent &&
        previousEditableContent->IsHTMLElement(nsGkAtoms::br)) {
      // If it's an invisible `<br>` element, we need to insert a padding
      // `<br>` element for making empty line have one-line height.
      if (HTMLEditUtils::IsInvisibleBRElement(*previousEditableContent) &&
          !EditorUtils::IsPaddingBRElementForEmptyLastLine(
              *previousEditableContent)) {
        AutoEditorDOMPointChildInvalidator lockOffset(point);
        Result<CreateElementResult, nsresult> insertPaddingBRElementResult =
            InsertPaddingBRElementForEmptyLastLineWithTransaction(point);
        if (MOZ_UNLIKELY(insertPaddingBRElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::"
              "InsertPaddingBRElementForEmptyLastLineWithTransaction() failed");
          return insertPaddingBRElementResult.unwrapErr();
        }
        insertPaddingBRElementResult.inspect().IgnoreCaretPointSuggestion();
        nsresult rv = CollapseSelectionTo(EditorRawDOMPoint(
            insertPaddingBRElementResult.inspect().GetNewNode(),
            InterlinePosition::StartOfNextLine));
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::CollapseSelectionTo() failed");
          return rv;
        }
      }
      // If it's a visible `<br>` element and next editable content is a
      // padding `<br>` element, we need to set interline position.
      else if (nsIContent* nextEditableContentInBlock =
                   HTMLEditUtils::GetNextContent(
                       *previousEditableContent,
                       {WalkTreeOption::IgnoreNonEditableNode,
                        WalkTreeOption::StopAtBlockBoundary},
                       BlockInlineCheck::UseComputedDisplayStyle,
                       editingHost)) {
        if (EditorUtils::IsPaddingBRElementForEmptyLastLine(
                *nextEditableContentInBlock)) {
          // Make it stick to the padding `<br>` element so that it will be
          // on blank line.
          DebugOnly<nsresult> rvIgnored = SelectionRef().SetInterlinePosition(
              InterlinePosition::StartOfNextLine);
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rvIgnored),
              "Selection::SetInterlinePosition(InterlinePosition::"
              "StartOfNextLine) failed, but ignored");
        }
      }
    }
  }

  // If previous editable content in same block is `<br>`, text node, `<img>`
  //  or `<hr>`, current caret position is fine.
  if (nsIContent* previousEditableContentInBlock =
          HTMLEditUtils::GetPreviousContent(
              point,
              {WalkTreeOption::IgnoreNonEditableNode,
               WalkTreeOption::StopAtBlockBoundary},
              BlockInlineCheck::UseComputedDisplayStyle, editingHost)) {
    if (previousEditableContentInBlock->IsHTMLElement(nsGkAtoms::br) ||
        previousEditableContentInBlock->IsText() ||
        HTMLEditUtils::IsImage(previousEditableContentInBlock) ||
        previousEditableContentInBlock->IsHTMLElement(nsGkAtoms::hr)) {
      return NS_OK;
    }
  }

  // If next editable content in same block is `<br>`, text node, `<img>` or
  // `<hr>`, current caret position is fine.
  if (nsIContent* nextEditableContentInBlock = HTMLEditUtils::GetNextContent(
          point,
          {WalkTreeOption::IgnoreNonEditableNode,
           WalkTreeOption::StopAtBlockBoundary},
          BlockInlineCheck::UseComputedDisplayStyle, editingHost)) {
    if (nextEditableContentInBlock->IsText() ||
        nextEditableContentInBlock->IsAnyOfHTMLElements(
            nsGkAtoms::br, nsGkAtoms::img, nsGkAtoms::hr)) {
      return NS_OK;
    }
  }

  // Otherwise, look for a near editable content towards edit action direction.

  // If there is no editable content, keep current caret position.
  // XXX Why do we treat `nsIEditor::ePreviousWord` etc as forward direction?
  nsIContent* nearEditableContent = HTMLEditUtils::GetAdjacentContentToPutCaret(
      point,
      aDirectionAndAmount == nsIEditor::ePrevious ? WalkTreeDirection::Backward
                                                  : WalkTreeDirection::Forward,
      *editingHost);
  if (!nearEditableContent) {
    return NS_OK;
  }

  EditorRawDOMPoint pointToPutCaret =
      HTMLEditUtils::GetGoodCaretPointFor<EditorRawDOMPoint>(
          *nearEditableContent, aDirectionAndAmount);
  if (!pointToPutCaret.IsSet()) {
    NS_WARNING("HTMLEditUtils::GetGoodCaretPointFor() failed");
    return NS_ERROR_FAILURE;
  }
  nsresult rv = CollapseSelectionTo(pointToPutCaret);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

nsresult HTMLEditor::RemoveEmptyNodesIn(const EditorDOMRange& aRange) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aRange.IsPositioned());

  // Some general notes on the algorithm used here: the goal is to examine all
  // the nodes in aRange, and remove the empty ones.  We do this by
  // using a content iterator to traverse all the nodes in the range, and
  // placing the empty nodes into an array.  After finishing the iteration,
  // we delete the empty nodes in the array.  (They cannot be deleted as we
  // find them because that would invalidate the iterator.)
  //
  // Since checking to see if a node is empty can be costly for nodes with
  // many descendants, there are some optimizations made.  I rely on the fact
  // that the iterator is post-order: it will visit children of a node before
  // visiting the parent node.  So if I find that a child node is not empty, I
  // know that its parent is not empty without even checking.  So I put the
  // parent on a "skipList" which is just a voidArray of nodes I can skip the
  // empty check on.  If I encounter a node on the skiplist, i skip the
  // processing for that node and replace its slot in the skiplist with that
  // node's parent.
  //
  // An interesting idea is to go ahead and regard parent nodes that are NOT
  // on the skiplist as being empty (without even doing the IsEmptyNode check)
  // on the theory that if they weren't empty, we would have encountered a
  // non-empty child earlier and thus put this parent node on the skiplist.
  //
  // Unfortunately I can't use that strategy here, because the range may
  // include some children of a node while excluding others.  Thus I could
  // find all the _examined_ children empty, but still not have an empty
  // parent.

  const RawRangeBoundary endOfRange = [&]() {
    // If the range is not collapsed and end of the range is start of a
    // container, it means that the inclusive ancestor empty element may be
    // created by splitting the left nodes.
    if (aRange.Collapsed() || !aRange.IsInContentNodes() ||
        !aRange.EndRef().IsStartOfContainer()) {
      return aRange.EndRef().ToRawRangeBoundary();
    }
    nsINode* const commonAncestor =
        nsContentUtils::GetClosestCommonInclusiveAncestor(
            aRange.StartRef().ContainerAs<nsIContent>(),
            aRange.EndRef().ContainerAs<nsIContent>());
    if (!commonAncestor) {
      return aRange.EndRef().ToRawRangeBoundary();
    }
    nsIContent* maybeRightContent = nullptr;
    for (nsIContent* content : aRange.EndRef()
                                   .ContainerAs<nsIContent>()
                                   ->InclusiveAncestorsOfType<nsIContent>()) {
      if (!HTMLEditUtils::IsSimplyEditableNode(*content) ||
          content == commonAncestor) {
        break;
      }
      if (aRange.StartRef().ContainerAs<nsIContent>() == content) {
        break;
      }
      EmptyCheckOptions options = {
          EmptyCheckOption::TreatListItemAsVisible,
          EmptyCheckOption::TreatTableCellAsVisible,
          EmptyCheckOption::TreatNonEditableContentAsInvisible};
      if (!HTMLEditUtils::IsBlockElement(
              *content, BlockInlineCheck::UseComputedDisplayStyle)) {
        options += EmptyCheckOption::TreatSingleBRElementAsVisible;
      }
      if (!HTMLEditUtils::IsEmptyNode(*content, options)) {
        break;
      }
      maybeRightContent = content;
    }
    if (!maybeRightContent) {
      return aRange.EndRef().ToRawRangeBoundary();
    }
    return EditorRawDOMPoint::After(*maybeRightContent).ToRawRangeBoundary();
  }();

  PostContentIterator postOrderIter;
  nsresult rv =
      postOrderIter.Init(aRange.StartRef().ToRawRangeBoundary(), endOfRange);
  if (NS_FAILED(rv)) {
    NS_WARNING("PostContentIterator::Init() failed");
    return rv;
  }

  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfEmptyContents,
      arrayOfEmptyCites;

  // Collect empty nodes first.
  {
    const bool isMailEditor = IsMailEditor();
    AutoTArray<OwningNonNull<nsIContent>, 64> knownNonEmptyContents;
    Maybe<AutoClonedSelectionRangeArray> maybeSelectionRanges;
    for (; !postOrderIter.IsDone(); postOrderIter.Next()) {
      MOZ_ASSERT(postOrderIter.GetCurrentNode()->IsContent());

      nsIContent* content = postOrderIter.GetCurrentNode()->AsContent();
      nsIContent* parentContent = content->GetParent();

      size_t idx = knownNonEmptyContents.IndexOf(content);
      if (idx != decltype(knownNonEmptyContents)::NoIndex) {
        // This node is on our skip list.  Skip processing for this node, and
        // replace its value in the skip list with the value of its parent
        if (parentContent) {
          knownNonEmptyContents[idx] = parentContent;
        }
        continue;
      }

      const bool isEmptyNode = [&]() {
        if (!content->IsElement()) {
          return false;
        }
        const bool isMailCite =
            isMailEditor && HTMLEditUtils::IsMailCite(*content->AsElement());
        const bool isCandidate = [&]() {
          if (content->IsHTMLElement(nsGkAtoms::body)) {
            // Don't delete the body
            return false;
          }
          if (isMailCite || content->IsHTMLElement(nsGkAtoms::a) ||
              HTMLEditUtils::IsInlineStyle(content) ||
              HTMLEditUtils::IsAnyListElement(content) ||
              content->IsHTMLElement(nsGkAtoms::div)) {
            // Only consider certain nodes to be empty for purposes of removal
            return true;
          }
          if (HTMLEditUtils::IsFormatElementForFormatBlockCommand(*content) ||
              HTMLEditUtils::IsListItem(content) ||
              content->IsHTMLElement(nsGkAtoms::blockquote)) {
            // These node types are candidates if selection is not in them.  If
            // it is one of these, don't delete if selection inside.  This is so
            // we can create empty headings, etc., for the user to type into.
            if (maybeSelectionRanges.isNothing()) {
              maybeSelectionRanges.emplace(SelectionRef());
            }
            return !maybeSelectionRanges
                        ->IsAtLeastOneContainerOfRangeBoundariesInclusiveDescendantOf(
                            *content);
          }
          return false;
        }();

        if (!isCandidate) {
          return false;
        }

        // We delete mailcites even if they have a solo br in them.  Other
        // nodes we require to be empty.
        HTMLEditUtils::EmptyCheckOptions options{
            EmptyCheckOption::TreatListItemAsVisible,
            EmptyCheckOption::TreatTableCellAsVisible};
        if (!isMailCite) {
          options += EmptyCheckOption::TreatSingleBRElementAsVisible;
        } else {
          // XXX Maybe unnecessary to specify this.
          options += EmptyCheckOption::TreatNonEditableContentAsInvisible;
        }
        if (!HTMLEditUtils::IsEmptyNode(*content, options)) {
          return false;
        }

        if (isMailCite) {
          // mailcites go on a separate list from other empty nodes
          arrayOfEmptyCites.AppendElement(*content);
        }
        // Don't delete non-editable nodes in this method because this is a
        // clean up method to remove unnecessary nodes of the result of
        // editing.  So, we shouldn't delete non-editable nodes which were
        // there before editing.  Additionally, if the element is some special
        // elements such as <body>, we shouldn't delete it.
        else if (HTMLEditUtils::IsSimplyEditableNode(*content) &&
                 HTMLEditUtils::IsRemovableNode(*content)) {
          arrayOfEmptyContents.AppendElement(*content);
        }
        return true;
      }();
      if (!isEmptyNode && parentContent) {
        knownNonEmptyContents.AppendElement(*parentContent);
      }
    }  // end of the for-loop iterating with postOrderIter
  }

  // now delete the empty nodes
  for (OwningNonNull<nsIContent>& emptyContent : arrayOfEmptyContents) {
    // MOZ_KnownLive due to bug 1622253
    nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(emptyContent));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }

  // Now delete the empty mailcites.  This is a separate step because we want
  // to pull out any br's and preserve them.
  EditorDOMPoint pointToPutCaret;
  for (OwningNonNull<nsIContent>& emptyCite : arrayOfEmptyCites) {
    if (!HTMLEditUtils::IsEmptyNode(
            emptyCite,
            {EmptyCheckOption::TreatSingleBRElementAsVisible,
             EmptyCheckOption::TreatListItemAsVisible,
             EmptyCheckOption::TreatTableCellAsVisible,
             EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
      // We are deleting a cite that has just a `<br>`.  We want to delete cite,
      // but preserve `<br>`.
      Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
          InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                          EditorDOMPoint(emptyCite));
      if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
            "LineBreakType::BRElement) failed");
        return insertBRElementResultOrError.unwrapErr();
      }
      CreateLineBreakResult insertBRElementResult =
          insertBRElementResultOrError.unwrap();
      MOZ_ASSERT(insertBRElementResult.Handled());
      // XXX Is this intentional selection change?
      insertBRElementResult.MoveCaretPointTo(
          pointToPutCaret, *this,
          {SuggestCaret::OnlyIfHasSuggestion,
           SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    }
    // MOZ_KnownLive because 'arrayOfEmptyCites' is guaranteed to keep it alive.
    nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(emptyCite));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }
  // XXX Is this intentional selection change?
  if (pointToPutCaret.IsSet()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionTo() caused destroying the editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
  }

  return NS_OK;
}

nsresult HTMLEditor::LiftUpListItemElement(
    Element& aListItemElement,
    LiftUpFromAllParentListElements aLiftUpFromAllParentListElements) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (!HTMLEditUtils::IsListItem(&aListItemElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(!aListItemElement.GetParentElement()) ||
      NS_WARN_IF(!aListItemElement.GetParentElement()->GetParentNode())) {
    return NS_ERROR_FAILURE;
  }

  // if it's first or last list item, don't need to split the list
  // otherwise we do.
  const bool isFirstListItem = HTMLEditUtils::IsFirstChild(
      aListItemElement, {WalkTreeOption::IgnoreNonEditableNode});
  const bool isLastListItem = HTMLEditUtils::IsLastChild(
      aListItemElement, {WalkTreeOption::IgnoreNonEditableNode});

  Element* leftListElement = aListItemElement.GetParentElement();
  if (NS_WARN_IF(!leftListElement)) {
    return NS_ERROR_FAILURE;
  }

  // If it's at middle of parent list element, split the parent list element.
  // Then, aListItem becomes the first list item of the right list element.
  if (!isFirstListItem && !isLastListItem) {
    EditorDOMPoint atListItemElement(&aListItemElement);
    if (NS_WARN_IF(!atListItemElement.IsSet())) {
      return NS_ERROR_FAILURE;
    }
    MOZ_ASSERT(atListItemElement.IsSetAndValid());
    Result<SplitNodeResult, nsresult> splitListItemParentResult =
        SplitNodeWithTransaction(atListItemElement);
    if (MOZ_UNLIKELY(splitListItemParentResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
      return splitListItemParentResult.unwrapErr();
    }
    nsresult rv = splitListItemParentResult.inspect().SuggestCaretPointTo(
        *this, {SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (NS_FAILED(rv)) {
      NS_WARNING("SplitNodeResult::SuggestCaretPointTo() failed");
      return rv;
    }

    leftListElement =
        splitListItemParentResult.inspect().GetPreviousContentAs<Element>();
    if (MOZ_UNLIKELY(!leftListElement)) {
      NS_WARNING(
          "HTMLEditor::SplitNodeWithTransaction() didn't return left list "
          "element");
      return NS_ERROR_FAILURE;
    }
  }

  // In most cases, insert the list item into the new left list node..
  EditorDOMPoint pointToInsertListItem(leftListElement);
  if (NS_WARN_IF(!pointToInsertListItem.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  // But when the list item was the first child of the right list, it should
  // be inserted between the both list elements.  This allows user to hit
  // Enter twice at a list item breaks the parent list node.
  if (!isFirstListItem) {
    DebugOnly<bool> advanced = pointToInsertListItem.AdvanceOffset();
    NS_WARNING_ASSERTION(advanced,
                         "Failed to advance offset to right list node");
  }

  EditorDOMPoint pointToPutCaret;
  {
    Result<MoveNodeResult, nsresult> moveListItemElementResult =
        MoveNodeWithTransaction(aListItemElement, pointToInsertListItem);
    if (MOZ_UNLIKELY(moveListItemElementResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
      return moveListItemElementResult.unwrapErr();
    }
    MoveNodeResult unwrappedMoveListItemElementResult =
        moveListItemElementResult.unwrap();
    unwrappedMoveListItemElementResult.MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
  }

  // Unwrap list item contents if they are no longer in a list
  // XXX If the parent list element is a child of another list element
  //     (although invalid tree), the list item element won't be unwrapped.
  //     That makes the parent ancestor element tree valid, but might be
  //     unexpected result.
  // XXX If aListItemElement is <dl> or <dd> and current parent is <ul> or <ol>,
  //     the list items won't be unwrapped.  If aListItemElement is <li> and its
  //     current parent is <dl>, there is same issue.
  if (!HTMLEditUtils::IsAnyListElement(pointToInsertListItem.GetContainer()) &&
      HTMLEditUtils::IsListItem(&aListItemElement)) {
    Result<EditorDOMPoint, nsresult> unwrapOrphanListItemElementResult =
        RemoveBlockContainerWithTransaction(aListItemElement);
    if (MOZ_UNLIKELY(unwrapOrphanListItemElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
      return unwrapOrphanListItemElementResult.unwrapErr();
    }
    if (AllowsTransactionsToChangeSelection() &&
        unwrapOrphanListItemElementResult.inspect().IsSet()) {
      pointToPutCaret = unwrapOrphanListItemElementResult.unwrap();
    }
    if (!pointToPutCaret.IsSet()) {
      return NS_OK;
    }
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionTo() failed");
    return rv;
  }

  if (pointToPutCaret.IsSet()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
  }

  if (aLiftUpFromAllParentListElements == LiftUpFromAllParentListElements::No) {
    return NS_OK;
  }
  // XXX If aListItemElement is moved to unexpected element by mutation event
  //     listener, shouldn't we stop calling this?
  nsresult rv = LiftUpListItemElement(aListItemElement,
                                      LiftUpFromAllParentListElements::Yes);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::LiftUpListItemElement("
                       "LiftUpFromAllParentListElements::Yes) failed");
  return rv;
}

nsresult HTMLEditor::DestroyListStructureRecursively(Element& aListElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(HTMLEditUtils::IsAnyListElement(&aListElement));

  // XXX If mutation event listener inserts new child into `aListElement`,
  //     this becomes infinite loop so that we should set limit of the
  //     loop count from original child count.
  while (aListElement.GetFirstChild()) {
    OwningNonNull<nsIContent> child = *aListElement.GetFirstChild();

    if (HTMLEditUtils::IsListItem(child)) {
      // XXX Using LiftUpListItemElement() is too expensive for this purpose.
      //     Looks like the reason why this method uses it is, only this loop
      //     wants to work with first child of aListElement.  However, what it
      //     actually does is removing <li> as container.  Perhaps, we should
      //     decide destination first, and then, move contents in `child`.
      // XXX If aListElement is is a child of another list element (although
      //     it's invalid tree), this moves the list item to outside of
      //     aListElement's parent.  Is that really intentional behavior?
      nsresult rv = LiftUpListItemElement(
          MOZ_KnownLive(*child->AsElement()),
          HTMLEditor::LiftUpFromAllParentListElements::Yes);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::LiftUpListItemElement(LiftUpFromAllParentListElements:"
            ":Yes) failed");
        return rv;
      }
      continue;
    }

    if (HTMLEditUtils::IsAnyListElement(child)) {
      nsresult rv =
          DestroyListStructureRecursively(MOZ_KnownLive(*child->AsElement()));
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::DestroyListStructureRecursively() failed");
        return rv;
      }
      continue;
    }

    // Delete any non-list items for now
    // XXX This is not HTML5 aware.  HTML5 allows all list elements to have
    //     <script> and <template> and <dl> element to have <div> to group
    //     some <dt> and <dd> elements.  So, this may break valid children.
    nsresult rv = DeleteNodeWithTransaction(*child);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return rv;
    }
  }

  // Delete the now-empty list
  const Result<EditorDOMPoint, nsresult> unwrapListElementResult =
      RemoveBlockContainerWithTransaction(aListElement);
  if (MOZ_UNLIKELY(unwrapListElementResult.isErr())) {
    NS_WARNING("HTMLEditor::RemoveBlockContainerWithTransaction() failed");
    return unwrapListElementResult.inspectErr();
  }
  const EditorDOMPoint& pointToPutCaret = unwrapListElementResult.inspect();
  if (!AllowsTransactionsToChangeSelection() || !pointToPutCaret.IsSet()) {
    return NS_OK;
  }
  nsresult rv = CollapseSelectionTo(pointToPutCaret);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed");
  return rv;
}

nsresult HTMLEditor::EnsureSelectionInBodyOrDocumentElement() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  RefPtr<Element> bodyOrDocumentElement = GetRoot();
  if (NS_WARN_IF(!bodyOrDocumentElement)) {
    return NS_ERROR_FAILURE;
  }

  const auto atCaret = GetFirstSelectionStartPoint<EditorRawDOMPoint>();
  if (NS_WARN_IF(!atCaret.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  // XXX This does wrong things.  Web apps can put any elements as sibling
  //     of `<body>` element.  Therefore, this collapses `Selection` into
  //     the `<body>` element which `HTMLDocument.body` is set to.  So,
  //     this makes users impossible to modify content outside of the
  //     `<body>` element even if caret is in an editing host.

  // Check that selection start container is inside the <body> element.
  // XXXsmaug this code is insane.
  nsINode* temp = atCaret.GetContainer();
  while (temp && !temp->IsHTMLElement(nsGkAtoms::body)) {
    temp = temp->GetParentOrShadowHostNode();
  }

  // If we aren't in the <body> element, force the issue.
  if (!temp) {
    nsresult rv = CollapseSelectionToStartOf(*bodyOrDocumentElement);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionToStartOf() caused destroying the "
          "editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionToStartOf() failed, but ignored");
    return NS_OK;
  }

  const auto selectionEndPoint = GetFirstSelectionEndPoint<EditorRawDOMPoint>();
  if (NS_WARN_IF(!selectionEndPoint.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  // check that selNode is inside body
  // XXXsmaug this code is insane.
  temp = selectionEndPoint.GetContainer();
  while (temp && !temp->IsHTMLElement(nsGkAtoms::body)) {
    temp = temp->GetParentOrShadowHostNode();
  }

  // If we aren't in the <body> element, force the issue.
  if (!temp) {
    nsresult rv = CollapseSelectionToStartOf(*bodyOrDocumentElement);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionToStartOf() caused destroying the "
          "editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionToStartOf() failed, but ignored");
  }

  return NS_OK;
}

Result<CreateLineBreakResult, nsresult>
HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(
    const EditorDOMPoint& aPoint,
    nsIEditor::EStripWrappers aDeleteEmptyInlines) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (MOZ_UNLIKELY(!aPoint.IsInContentNode())) {
    return CreateLineBreakResult::NotHandled();
  }

  const RefPtr<Element> editableBlockElement =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *aPoint.ContainerAs<nsIContent>(),
          HTMLEditUtils::ClosestEditableBlockElement,
          BlockInlineCheck::UseComputedDisplayStyle);

  if (!editableBlockElement ||
      !HTMLEditUtils::IsEmptyNode(
          *editableBlockElement,
          {EmptyCheckOption::TreatSingleBRElementAsVisible,
           EmptyCheckOption::TreatBlockAsVisible})) {
    return CreateLineBreakResult::NotHandled();
  }

  EditorDOMPoint pointToInsertLineBreak;
  if (aDeleteEmptyInlines == nsIEditor::eStrip &&
      aPoint.ContainerAs<nsIContent>() != editableBlockElement) {
    nsCOMPtr<nsIContent> emptyInlineAncestor =
        HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
            *aPoint.ContainerAs<nsIContent>(),
            BlockInlineCheck::UseComputedDisplayStyle);
    if (!emptyInlineAncestor) {
      emptyInlineAncestor = aPoint.ContainerAs<nsIContent>();
    }
    nsresult rv = DeleteNodeWithTransaction(*emptyInlineAncestor);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
    pointToInsertLineBreak = EditorDOMPoint(editableBlockElement, 0u);
  } else {
    pointToInsertLineBreak = aPoint;
  }

  // TODO: Use InsertLineBreak instead even if we're inserting a <br>.
  Result<CreateElementResult, nsresult> insertPaddingLineBreakResultOrError =
      InsertPaddingBRElementForEmptyLastLineWithTransaction(
          pointToInsertLineBreak);
  if (MOZ_UNLIKELY(insertPaddingLineBreakResultOrError.isErr())) {
    NS_WARNING(
        "EditorBase::InsertPaddingBRElementForEmptyLastLineWithTransaction() "
        "failed");
    return insertPaddingLineBreakResultOrError.propagateErr();
  }
  CreateElementResult insertPaddingLineBreakResult =
      insertPaddingLineBreakResultOrError.unwrap();
  RefPtr<HTMLBRElement> paddingBRElement =
      HTMLBRElement::FromNodeOrNull(insertPaddingLineBreakResult.GetNewNode());
  if (NS_WARN_IF(!paddingBRElement)) {
    return Err(NS_ERROR_FAILURE);
  }
  if (NS_WARN_IF(!paddingBRElement->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  insertPaddingLineBreakResult.IgnoreCaretPointSuggestion();
  return CreateLineBreakResult(EditorLineBreak(std::move(paddingBRElement)),
                               EditorDOMPoint(paddingBRElement));
}

Result<CreateLineBreakResult, nsresult>
HTMLEditor::InsertPaddingBRElementIfNeeded(
    const EditorDOMPoint& aPoint, nsIEditor::EStripWrappers aDeleteEmptyInlines,
    const Element& aEditingHost) {
  MOZ_ASSERT(aPoint.IsInContentNode());
  MOZ_ASSERT(HTMLEditUtils::NodeIsEditableOrNotInComposedDoc(
      *aPoint.ContainerAs<nsIContent>()));

  auto pointToInsertPaddingBR = [&]() MOZ_NEVER_INLINE_DEBUG -> EditorDOMPoint {
    // If the point is immediately before a block boundary which is for a
    // mailcite in plaintext mail composer (it is a <span> styled as block), we
    // should not treat it as a block because it's required by the serializer to
    // give the mailcite contents are not appear with outer content in the same
    // lines.
    if (IsPlaintextMailComposer()) {
      const WSScanResult nextVisibleThing =
          WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
              WSRunScanner::Scan::EditableNodes, aPoint,
              BlockInlineCheck::UseComputedDisplayOutsideStyle);
      if (nextVisibleThing.ReachedBlockBoundary() &&
          HTMLEditUtils::IsMailCite(*nextVisibleThing.ElementPtr()) &&
          HTMLEditUtils::IsInlineContent(
              *nextVisibleThing.ElementPtr(),
              BlockInlineCheck::UseHTMLDefaultStyle)) {
        return EditorDOMPoint::AtEndOf(*nextVisibleThing.ElementPtr());
      }
    }
    return HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(aPoint,
                                                                  aEditingHost);
  }();
  if (!pointToInsertPaddingBR.IsSet()) {
    return CreateLineBreakResult::NotHandled();
  }
  if (aDeleteEmptyInlines == nsIEditor::eStrip &&
      pointToInsertPaddingBR.IsContainerElement() &&
      HTMLEditUtils::IsEmptyInlineContainer(
          *pointToInsertPaddingBR.ContainerAs<Element>(),
          {EmptyCheckOption::TreatSingleBRElementAsVisible,
           EmptyCheckOption::TreatBlockAsVisible,
           EmptyCheckOption::TreatListItemAsVisible,
           EmptyCheckOption::TreatTableCellAsVisible},
          BlockInlineCheck::UseComputedDisplayStyle)) {
    RefPtr<Element> emptyInlineAncestor =
        HTMLEditUtils::GetMostDistantAncestorEditableEmptyInlineElement(
            *pointToInsertPaddingBR.ContainerAs<nsIContent>(),
            BlockInlineCheck::UseComputedDisplayStyle);
    if (!emptyInlineAncestor) {
      emptyInlineAncestor = pointToInsertPaddingBR.ContainerAs<Element>();
    }
    AutoTrackDOMPoint trackPointToInsertPaddingBR(RangeUpdaterRef(),
                                                  &pointToInsertPaddingBR);
    nsresult rv = DeleteNodeWithTransaction(*emptyInlineAncestor);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  // Padding <br> elements may appear and disappear a lot even during IME has a
  // composition.  Therefore, IME may be confused with the mutation if we use
  // normal <br> element since it does not match with expectation of IME.  For
  // hiding the mutations from IME, we need to set the new <br> element flag to
  // NS_PADDING_FOR_EMPTY_LAST_LINE.
  Result<CreateElementResult, nsresult> insertPaddingBRResultOrError =
      InsertBRElement(WithTransaction::Yes,
                      BRElementType::PaddingForEmptyLastLine,
                      pointToInsertPaddingBR);
  if (MOZ_UNLIKELY(insertPaddingBRResultOrError.isErr())) {
    NS_WARNING(
        "EditorBase::InsertBRElement(WithTransaction::Yes, "
        "BRElementType::PaddingForEmptyLastLine) failed");
    return insertPaddingBRResultOrError.propagateErr();
  }
  return CreateLineBreakResult(insertPaddingBRResultOrError.unwrap());
}

Result<EditorDOMPoint, nsresult> HTMLEditor::RemoveAlignFromDescendants(
    Element& aElement, const nsAString& aAlignType, EditTarget aEditTarget) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aElement.IsHTMLElement(nsGkAtoms::table));

  const bool useCSS = IsCSSEnabled();

  EditorDOMPoint pointToPutCaret;

  // Let's remove all alignment hints in the children of aNode; it can
  // be an ALIGN attribute (in case we just remove it) or a CENTER
  // element (here we have to remove the container and keep its
  // children). We break on tables and don't look at their children.
  nsCOMPtr<nsIContent> nextSibling;
  for (nsIContent* content =
           aEditTarget == EditTarget::NodeAndDescendantsExceptTable
               ? &aElement
               : aElement.GetFirstChild();
       content; content = nextSibling) {
    // Get the next sibling before removing content from the DOM tree.
    // XXX If next sibling is removed from the parent and/or inserted to
    //     different parent, we will behave unexpectedly.  I think that
    //     we should create child list and handle it with checking whether
    //     it's still a child of expected parent.
    nextSibling = aEditTarget == EditTarget::NodeAndDescendantsExceptTable
                      ? nullptr
                      : content->GetNextSibling();

    if (content->IsHTMLElement(nsGkAtoms::center)) {
      OwningNonNull<Element> centerElement = *content->AsElement();
      {
        Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            RemoveAlignFromDescendants(centerElement, aAlignType,
                                       EditTarget::OnlyDescendantsExceptTable);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::RemoveAlignFromDescendants(EditTarget::"
              "OnlyDescendantsExceptTable) failed");
          return pointToPutCaretOrError;
        }
        if (pointToPutCaretOrError.inspect().IsSet()) {
          pointToPutCaret = pointToPutCaretOrError.unwrap();
        }
      }

      // We may have to insert a `<br>` element before first child of the
      // `<center>` element because it should be first element of a hard line
      // even after removing the `<center>` element.
      {
        Result<CreateElementResult, nsresult>
            maybeInsertBRElementBeforeFirstChildResult =
                EnsureHardLineBeginsWithFirstChildOf(centerElement);
        if (MOZ_UNLIKELY(maybeInsertBRElementBeforeFirstChildResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::EnsureHardLineBeginsWithFirstChildOf() failed");
          return maybeInsertBRElementBeforeFirstChildResult.propagateErr();
        }
        CreateElementResult unwrappedResult =
            maybeInsertBRElementBeforeFirstChildResult.unwrap();
        if (unwrappedResult.HasCaretPointSuggestion()) {
          pointToPutCaret = unwrappedResult.UnwrapCaretPoint();
        }
      }

      // We may have to insert a `<br>` element after last child of the
      // `<center>` element because it should be last element of a hard line
      // even after removing the `<center>` element.
      {
        Result<CreateElementResult, nsresult>
            maybeInsertBRElementAfterLastChildResult =
                EnsureHardLineEndsWithLastChildOf(centerElement);
        if (MOZ_UNLIKELY(maybeInsertBRElementAfterLastChildResult.isErr())) {
          NS_WARNING("HTMLEditor::EnsureHardLineEndsWithLastChildOf() failed");
          return maybeInsertBRElementAfterLastChildResult.propagateErr();
        }
        CreateElementResult unwrappedResult =
            maybeInsertBRElementAfterLastChildResult.unwrap();
        if (unwrappedResult.HasCaretPointSuggestion()) {
          pointToPutCaret = unwrappedResult.UnwrapCaretPoint();
        }
      }

      {
        Result<EditorDOMPoint, nsresult> unwrapCenterElementResult =
            RemoveContainerWithTransaction(centerElement);
        if (MOZ_UNLIKELY(unwrapCenterElementResult.isErr())) {
          NS_WARNING("HTMLEditor::RemoveContainerWithTransaction() failed");
          return unwrapCenterElementResult;
        }
        if (unwrapCenterElementResult.inspect().IsSet()) {
          pointToPutCaret = unwrapCenterElementResult.unwrap();
        }
      }
      continue;
    }

    if (!HTMLEditUtils::IsBlockElement(*content,
                                       BlockInlineCheck::UseHTMLDefaultStyle) &&
        !content->IsHTMLElement(nsGkAtoms::hr)) {
      continue;
    }

    const OwningNonNull<Element> blockOrHRElement = *content->AsElement();
    if (HTMLEditUtils::SupportsAlignAttr(blockOrHRElement)) {
      nsresult rv =
          RemoveAttributeWithTransaction(blockOrHRElement, *nsGkAtoms::align);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::align) "
            "failed");
        return Err(rv);
      }
    }
    if (useCSS) {
      if (blockOrHRElement->IsAnyOfHTMLElements(nsGkAtoms::table,
                                                nsGkAtoms::hr)) {
        nsresult rv = SetAttributeOrEquivalent(
            blockOrHRElement, nsGkAtoms::align, aAlignType, false);
        if (NS_WARN_IF(Destroyed())) {
          return Err(NS_ERROR_EDITOR_DESTROYED);
        }
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "EditorBase::SetAttributeOrEquivalent(nsGkAtoms::align) failed");
          return Err(rv);
        }
      } else {
        nsStyledElement* styledBlockOrHRElement =
            nsStyledElement::FromNode(blockOrHRElement);
        if (NS_WARN_IF(!styledBlockOrHRElement)) {
          return Err(NS_ERROR_FAILURE);
        }
        // MOZ_KnownLive(*styledBlockOrHRElement): It's `blockOrHRElement
        // which is OwningNonNull.
        nsAutoString dummyCssValue;
        Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
            CSSEditUtils::RemoveCSSInlineStyleWithTransaction(
                *this, MOZ_KnownLive(*styledBlockOrHRElement),
                nsGkAtoms::textAlign, dummyCssValue);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "CSSEditUtils::RemoveCSSInlineStyleWithTransaction(nsGkAtoms::"
              "textAlign) failed");
          return pointToPutCaretOrError;
        }
        if (pointToPutCaretOrError.inspect().IsSet()) {
          pointToPutCaret = pointToPutCaretOrError.unwrap();
        }
      }
    }
    if (!blockOrHRElement->IsHTMLElement(nsGkAtoms::table)) {
      // unless this is a table, look at children
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          RemoveAlignFromDescendants(blockOrHRElement, aAlignType,
                                     EditTarget::OnlyDescendantsExceptTable);
      if (pointToPutCaretOrError.isErr()) {
        NS_WARNING(
            "HTMLEditor::RemoveAlignFromDescendants(EditTarget::"
            "OnlyDescendantsExceptTable) failed");
        return pointToPutCaretOrError;
      }
      if (pointToPutCaretOrError.inspect().IsSet()) {
        pointToPutCaret = pointToPutCaretOrError.unwrap();
      }
    }
  }
  return pointToPutCaret;
}

Result<CreateElementResult, nsresult>
HTMLEditor::EnsureHardLineBeginsWithFirstChildOf(
    Element& aRemovingContainerElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsIContent* firstEditableChild = HTMLEditUtils::GetFirstChild(
      aRemovingContainerElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (!firstEditableChild) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *firstEditableChild, BlockInlineCheck::UseComputedDisplayStyle) ||
      firstEditableChild->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  nsIContent* previousEditableContent = HTMLEditUtils::GetPreviousSibling(
      aRemovingContainerElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (!previousEditableContent) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *previousEditableContent,
          BlockInlineCheck::UseComputedDisplayStyle) ||
      previousEditableContent->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      EditorDOMPoint(&aRemovingContainerElement, 0u));
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertBRElementResultOrError.propagateErr();
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  return CreateElementResult(insertBRElementResult->BRElementRef(),
                             insertBRElementResult.UnwrapCaretPoint());
}

Result<CreateElementResult, nsresult>
HTMLEditor::EnsureHardLineEndsWithLastChildOf(
    Element& aRemovingContainerElement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsIContent* firstEditableContent = HTMLEditUtils::GetLastChild(
      aRemovingContainerElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (!firstEditableContent) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *firstEditableContent, BlockInlineCheck::UseComputedDisplayStyle) ||
      firstEditableContent->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  nsIContent* nextEditableContent = HTMLEditUtils::GetPreviousSibling(
      aRemovingContainerElement, {WalkTreeOption::IgnoreNonEditableNode});
  if (!nextEditableContent) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *nextEditableContent, BlockInlineCheck::UseComputedDisplayStyle) ||
      nextEditableContent->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
      InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                      EditorDOMPoint::AtEndOf(aRemovingContainerElement));
  if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
        "LineBreakType::BRElement) failed");
    return insertBRElementResultOrError.propagateErr();
  }
  CreateLineBreakResult insertBRElementResult =
      insertBRElementResultOrError.unwrap();
  return CreateElementResult(insertBRElementResult->BRElementRef(),
                             insertBRElementResult.UnwrapCaretPoint());
}

Result<EditorDOMPoint, nsresult> HTMLEditor::SetBlockElementAlign(
    Element& aBlockOrHRElement, const nsAString& aAlignType,
    EditTarget aEditTarget) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(HTMLEditUtils::IsBlockElement(
                 aBlockOrHRElement, BlockInlineCheck::UseHTMLDefaultStyle) ||
             aBlockOrHRElement.IsHTMLElement(nsGkAtoms::hr));
  MOZ_ASSERT(IsCSSEnabled() ||
             HTMLEditUtils::SupportsAlignAttr(aBlockOrHRElement));

  EditorDOMPoint pointToPutCaret;
  if (!aBlockOrHRElement.IsHTMLElement(nsGkAtoms::table)) {
    Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
        RemoveAlignFromDescendants(aBlockOrHRElement, aAlignType, aEditTarget);
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      NS_WARNING("HTMLEditor::RemoveAlignFromDescendants() failed");
      return pointToPutCaretOrError;
    }
    if (pointToPutCaretOrError.inspect().IsSet()) {
      pointToPutCaret = pointToPutCaretOrError.unwrap();
    }
  }
  nsresult rv = SetAttributeOrEquivalent(&aBlockOrHRElement, nsGkAtoms::align,
                                         aAlignType, false);
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetAttributeOrEquivalent(nsGkAtoms::align) failed");
    return Err(rv);
  }
  return pointToPutCaret;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::ChangeMarginStart(
    Element& aElement, ChangeMargin aChangeMargin,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  nsStaticAtom& marginProperty = MarginPropertyAtomForIndent(aElement);
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  nsAutoString value;
  DebugOnly<nsresult> rvIgnored =
      CSSEditUtils::GetSpecifiedProperty(aElement, marginProperty, value);
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "CSSEditUtils::GetSpecifiedProperty() failed, but ignored");
  float f;
  RefPtr<nsAtom> unit;
  CSSEditUtils::ParseLength(value, &f, getter_AddRefs(unit));
  if (!f) {
    unit = nsGkAtoms::px;
  }
  int8_t multiplier = aChangeMargin == ChangeMargin::Increase ? 1 : -1;
  if (nsGkAtoms::in == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_IN * multiplier;
  } else if (nsGkAtoms::cm == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_CM * multiplier;
  } else if (nsGkAtoms::mm == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_MM * multiplier;
  } else if (nsGkAtoms::pt == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_PT * multiplier;
  } else if (nsGkAtoms::pc == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_PC * multiplier;
  } else if (nsGkAtoms::em == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_EM * multiplier;
  } else if (nsGkAtoms::ex == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_EX * multiplier;
  } else if (nsGkAtoms::px == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_PX * multiplier;
  } else if (nsGkAtoms::percentage == unit) {
    f += NS_EDITOR_INDENT_INCREMENT_PERCENT * multiplier;
  }

  if (0 < f) {
    if (nsStyledElement* styledElement = nsStyledElement::FromNode(&aElement)) {
      nsAutoString newValue;
      newValue.AppendFloat(f);
      newValue.Append(nsDependentAtomString(unit));
      // MOZ_KnownLive(*styledElement): It's aElement and its lifetime must
      // be guaranteed by caller because of MOZ_CAN_RUN_SCRIPT method.
      // MOZ_KnownLive(merginProperty): It's nsStaticAtom.
      nsresult rv = CSSEditUtils::SetCSSPropertyWithTransaction(
          *this, MOZ_KnownLive(*styledElement), MOZ_KnownLive(marginProperty),
          newValue);
      if (rv == NS_ERROR_EDITOR_DESTROYED) {
        NS_WARNING(
            "CSSEditUtils::SetCSSPropertyWithTransaction() destroyed the "
            "editor");
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "CSSEditUtils::SetCSSPropertyWithTransaction() failed, but ignored");
    }
    return EditorDOMPoint();
  }

  if (nsStyledElement* styledElement = nsStyledElement::FromNode(&aElement)) {
    // MOZ_KnownLive(*styledElement): It's aElement and its lifetime must
    // be guaranteed by caller because of MOZ_CAN_RUN_SCRIPT method.
    // MOZ_KnownLive(merginProperty): It's nsStaticAtom.
    nsresult rv = CSSEditUtils::RemoveCSSPropertyWithTransaction(
        *this, MOZ_KnownLive(*styledElement), MOZ_KnownLive(marginProperty),
        value);
    if (rv == NS_ERROR_EDITOR_DESTROYED) {
      NS_WARNING(
          "CSSEditUtils::RemoveCSSPropertyWithTransaction() destroyed the "
          "editor");
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "CSSEditUtils::RemoveCSSPropertyWithTransaction() failed, but ignored");
  }

  // Remove unnecessary divs
  if (!aElement.IsHTMLElement(nsGkAtoms::div) ||
      HTMLEditUtils::ElementHasAttribute(aElement)) {
    return EditorDOMPoint();
  }
  // Don't touch editing host nor node which is outside of it.
  if (&aElement == &aEditingHost ||
      !aElement.IsInclusiveDescendantOf(&aEditingHost)) {
    return EditorDOMPoint();
  }

  Result<EditorDOMPoint, nsresult> unwrapDivElementResult =
      RemoveContainerWithTransaction(aElement);
  NS_WARNING_ASSERTION(unwrapDivElementResult.isOk(),
                       "HTMLEditor::RemoveContainerWithTransaction() failed");
  return unwrapDivElementResult;
}

Result<EditActionResult, nsresult>
HTMLEditor::SetSelectionToAbsoluteAsSubAction(const Element& aEditingHost) {
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSetPositionToAbsolute, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  auto EnsureCaretInElementIfCollapsedOutside =
      [&](Element& aElement) MOZ_CAN_RUN_SCRIPT {
        if (!SelectionRef().IsCollapsed() || !SelectionRef().RangeCount()) {
          return NS_OK;
        }
        const auto firstRangeStartPoint =
            GetFirstSelectionStartPoint<EditorRawDOMPoint>();
        if (MOZ_UNLIKELY(!firstRangeStartPoint.IsSet())) {
          return NS_OK;
        }
        const Result<EditorRawDOMPoint, nsresult> pointToPutCaretOrError =
            HTMLEditUtils::ComputePointToPutCaretInElementIfOutside<
                EditorRawDOMPoint>(aElement, firstRangeStartPoint);
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "HTMLEditUtils::ComputePointToPutCaretInElementIfOutside() "
              "failed, but ignored");
          return NS_OK;
        }
        if (!pointToPutCaretOrError.inspect().IsSet()) {
          return NS_OK;
        }
        nsresult rv = CollapseSelectionTo(pointToPutCaretOrError.inspect());
        if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
          NS_WARNING("EditorBase::CollapseSelectionTo() failed");
          return NS_ERROR_EDITOR_DESTROYED;
        }
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "EditorBase::CollapseSelectionTo() failed, but ignored");
        return NS_OK;
      };

  RefPtr<Element> focusElement = GetSelectionContainerElement();
  if (focusElement && HTMLEditUtils::IsImage(focusElement)) {
    nsresult rv = EnsureCaretInElementIfCollapsedOutside(*focusElement);
    if (NS_FAILED(rv)) {
      NS_WARNING("EnsureCaretInElementIfCollapsedOutside() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  // XXX Why do we do this only when there is only one selection range?
  if (!SelectionRef().IsCollapsed() && SelectionRef().RangeCount() == 1u) {
    Result<EditorRawDOMRange, nsresult> extendedRange =
        GetRangeExtendedToHardLineEdgesForBlockEditAction(
            SelectionRef().GetRangeAt(0u), aEditingHost);
    if (MOZ_UNLIKELY(extendedRange.isErr())) {
      NS_WARNING(
          "HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction() "
          "failed");
      return extendedRange.propagateErr();
    }
    // Note that end point may be prior to start point.  So, we
    // cannot use Selection::SetStartAndEndInLimit() here.
    IgnoredErrorResult error;
    SelectionRef().SetBaseAndExtentInLimiter(
        extendedRange.inspect().StartRef().ToRawRangeBoundary(),
        extendedRange.inspect().EndRef().ToRawRangeBoundary(), error);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (MOZ_UNLIKELY(error.Failed())) {
      NS_WARNING("Selection::SetBaseAndExtentInLimiter() failed");
      return Err(error.StealNSResult());
    }
  }

  RefPtr<Element> divElement;
  rv = MoveSelectedContentsToDivElementToMakeItAbsolutePosition(
      address_of(divElement), aEditingHost);
  // MoveSelectedContentsToDivElementToMakeItAbsolutePosition() may restore
  // selection with AutoSelectionRestorer.  Therefore, the editor might have
  // already been destroyed now.
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::MoveSelectedContentsToDivElementToMakeItAbsolutePosition()"
        " failed");
    return Err(rv);
  }

  if (IsSelectionRangeContainerNotContent()) {
    NS_WARNING("Mutation event listener might have changed the selection");
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  if (SelectionRef().IsCollapsed()) {
    const auto caretPosition =
        EditorBase::GetFirstSelectionStartPoint<EditorDOMPoint>();
    Result<CreateLineBreakResult, nsresult>
        insertPaddingBRElementResultOrError =
            InsertPaddingBRElementIfInEmptyBlock(caretPosition, eNoStrip);
    if (MOZ_UNLIKELY(insertPaddingBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertPaddingBRElementIfInEmptyBlock(eNoStrip) failed");
      return insertPaddingBRElementResultOrError.propagateErr();
    }
    nsresult rv =
        insertPaddingBRElementResultOrError.unwrap().SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
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

  if (!divElement) {
    return EditActionResult::HandledResult();
  }

  rv = SetPositionToAbsoluteOrStatic(*divElement, true);
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetPositionToAbsoluteOrStatic() failed");
    return Err(rv);
  }

  rv = EnsureCaretInElementIfCollapsedOutside(*divElement);
  if (NS_FAILED(rv)) {
    NS_WARNING("EnsureCaretInElementIfCollapsedOutside() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::MoveSelectedContentsToDivElementToMakeItAbsolutePosition(
    RefPtr<Element>* aTargetElement, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aTargetElement);

  AutoSelectionRestorer restoreSelectionLater(this);

  EditorDOMPoint pointToPutCaret;

  // Use these ranges to construct a list of nodes to act on.
  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContents;
  {
    AutoClonedSelectionRangeArray extendedSelectionRanges(SelectionRef());
    extendedSelectionRanges.ExtendRangesToWrapLines(
        EditSubAction::eSetPositionToAbsolute,
        BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
    Result<EditorDOMPoint, nsresult> splitResult =
        extendedSelectionRanges
            .SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
                *this, BlockInlineCheck::UseHTMLDefaultStyle, aEditingHost);
    if (MOZ_UNLIKELY(splitResult.isErr())) {
      NS_WARNING(
          "AutoClonedRangeArray::"
          "SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries() "
          "failed");
      return splitResult.unwrapErr();
    }
    if (splitResult.inspect().IsSet()) {
      pointToPutCaret = splitResult.unwrap();
    }
    nsresult rv = extendedSelectionRanges.CollectEditTargetNodes(
        *this, arrayOfContents, EditSubAction::eSetPositionToAbsolute,
        AutoClonedRangeArray::CollectNonEditableNodes::Yes);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "AutoClonedRangeArray::CollectEditTargetNodes(EditSubAction::"
          "eSetPositionToAbsolute, CollectNonEditableNodes::Yes) failed");
      return rv;
    }
  }

  Result<EditorDOMPoint, nsresult> splitAtBRElementsResult =
      MaybeSplitElementsAtEveryBRElement(arrayOfContents,
                                         EditSubAction::eSetPositionToAbsolute);
  if (MOZ_UNLIKELY(splitAtBRElementsResult.isErr())) {
    NS_WARNING(
        "HTMLEditor::MaybeSplitElementsAtEveryBRElement(EditSubAction::"
        "eSetPositionToAbsolute) failed");
    return splitAtBRElementsResult.inspectErr();
  }
  if (splitAtBRElementsResult.inspect().IsSet()) {
    pointToPutCaret = splitAtBRElementsResult.unwrap();
  }

  if (AllowsTransactionsToChangeSelection() &&
      pointToPutCaret.IsSetAndValid()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed");
      return rv;
    }
  }

  // If there is no visible and editable nodes in the edit targets, make an
  // empty block.
  // XXX Isn't this odd if there are only non-editable visible nodes?
  if (HTMLEditUtils::IsEmptyOneHardLine(
          arrayOfContents, BlockInlineCheck::UseHTMLDefaultStyle)) {
    const auto atCaret =
        EditorBase::GetFirstSelectionStartPoint<EditorDOMPoint>();
    if (NS_WARN_IF(!atCaret.IsSet())) {
      return NS_ERROR_FAILURE;
    }

    // Make sure we can put a block here.
    Result<CreateElementResult, nsresult> createNewDivElementResult =
        InsertElementWithSplittingAncestorsWithTransaction(
            *nsGkAtoms::div, atCaret, BRElementNextToSplitPoint::Keep,
            aEditingHost);
    if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
          "nsGkAtoms::div) failed");
      return createNewDivElementResult.unwrapErr();
    }
    CreateElementResult unwrappedCreateNewDivElementResult =
        createNewDivElementResult.unwrap();
    // We'll update selection after deleting the content nodes and nobody
    // refers selection until then.  Therefore, we don't need to update
    // selection here.
    unwrappedCreateNewDivElementResult.IgnoreCaretPointSuggestion();
    RefPtr<Element> newDivElement =
        unwrappedCreateNewDivElementResult.UnwrapNewNode();
    MOZ_ASSERT(newDivElement);
    // Delete anything that was in the list of nodes
    // XXX We don't need to remove items from the array.
    for (OwningNonNull<nsIContent>& curNode : arrayOfContents) {
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to keep it alive.
      nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(*curNode));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return rv;
      }
    }
    // Don't restore the selection
    restoreSelectionLater.Abort();
    nsresult rv = CollapseSelectionToStartOf(*newDivElement);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::CollapseSelectionToStartOf() failed");
    *aTargetElement = std::move(newDivElement);
    return rv;
  }

  // `<div>` element to be positioned absolutely.  This may have already
  // existed or newly created by this method.
  RefPtr<Element> targetDivElement;
  // Newly created list element for moving selected list item elements into
  // targetDivElement.  I.e., this is created in the `<div>` element.
  RefPtr<Element> createdListElement;
  // If we handle a parent list item element, this is set to it.  In such case,
  // we should handle its children again.
  RefPtr<Element> handledListItemElement;
  for (OwningNonNull<nsIContent>& content : arrayOfContents) {
    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsSet())) {
      return NS_ERROR_FAILURE;  // XXX not continue??
    }

    // Ignore all non-editable nodes.  Leave them be.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    // If current node is a child of a list element, we need another list
    // element in absolute-positioned `<div>` element to avoid non-selected
    // list items are moved into the `<div>` element.
    if (HTMLEditUtils::IsAnyListElement(atContent.GetContainer())) {
      // If we cannot move current node to created list element, we need a
      // list element in the target `<div>` element for the destination.
      // Therefore, duplicate same list element into the target `<div>`
      // element.
      nsIContent* previousEditableContent =
          createdListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    content, {WalkTreeOption::IgnoreNonEditableNode})
              : nullptr;
      if (!createdListElement ||
          (previousEditableContent &&
           previousEditableContent != createdListElement)) {
        nsAtom* ULOrOLOrDLTagName =
            atContent.GetContainer()->NodeInfo()->NameAtom();
        if (targetDivElement) {
          // XXX Do we need to split the container? Since we'll append new
          //     element at end of the <div> element.
          Result<SplitNodeResult, nsresult> splitNodeResult =
              MaybeSplitAncestorsForInsertWithTransaction(
                  MOZ_KnownLive(*ULOrOLOrDLTagName), atContent, aEditingHost);
          if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::MaybeSplitAncestorsForInsertWithTransaction() "
                "failed");
            return splitNodeResult.unwrapErr();
          }
          // We'll update selection after creating a list element below.
          // Therefore, we don't need to touch selection here.
          splitNodeResult.inspect().IgnoreCaretPointSuggestion();
        } else {
          // If we've not had a target <div> element yet, let's insert a <div>
          // element with splitting the ancestors.
          Result<CreateElementResult, nsresult> createNewDivElementResult =
              InsertElementWithSplittingAncestorsWithTransaction(
                  *nsGkAtoms::div, atContent, BRElementNextToSplitPoint::Keep,
                  aEditingHost);
          if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::"
                "InsertElementWithSplittingAncestorsWithTransaction(nsGkAtoms::"
                "div) failed");
            return createNewDivElementResult.unwrapErr();
          }
          // We'll update selection after creating a list element below.
          // Therefor, we don't need to touch selection here.
          createNewDivElementResult.inspect().IgnoreCaretPointSuggestion();
          MOZ_ASSERT(createNewDivElementResult.inspect().GetNewNode());
          targetDivElement = createNewDivElementResult.unwrap().UnwrapNewNode();
        }
        Result<CreateElementResult, nsresult> createNewListElementResult =
            CreateAndInsertElement(WithTransaction::Yes,
                                   MOZ_KnownLive(*ULOrOLOrDLTagName),
                                   EditorDOMPoint::AtEndOf(targetDivElement));
        if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) "
              "failed");
          return createNewListElementResult.unwrapErr();
        }
        nsresult rv = createNewListElementResult.inspect().SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CreateElementResult::SuggestCaretPointTo() failed, but ignored");
        createdListElement =
            createNewListElementResult.unwrap().UnwrapNewNode();
        MOZ_ASSERT(createdListElement);
      }
      // Move current node (maybe, assumed as a list item element) into the
      // new list element in the target `<div>` element to be positioned
      // absolutely.
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to keep it alive.
      Result<MoveNodeResult, nsresult> moveNodeResult =
          MoveNodeToEndWithTransaction(MOZ_KnownLive(content),
                                       *createdListElement);
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      nsresult rv = moveNodeResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
      continue;
    }

    // If contents in a list item element is selected, we should move current
    // node into the target `<div>` element with the list item element itself
    // because we want to keep indent level of the contents.
    if (RefPtr<Element> listItemElement =
            HTMLEditUtils::GetClosestAncestorListItemElement(content,
                                                             &aEditingHost)) {
      if (handledListItemElement == listItemElement) {
        // Current node has already been moved into the `<div>` element.
        continue;
      }
      // If we cannot move the list item element into created list element,
      // we need another list element in the target `<div>` element.
      nsIContent* previousEditableContent =
          createdListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    *listItemElement, {WalkTreeOption::IgnoreNonEditableNode})
              : nullptr;
      if (!createdListElement ||
          (previousEditableContent &&
           previousEditableContent != createdListElement)) {
        EditorDOMPoint atListItem(listItemElement);
        if (NS_WARN_IF(!atListItem.IsSet())) {
          return NS_ERROR_FAILURE;
        }
        // XXX If content is the listItemElement and not in a list element,
        //     we duplicate wrong element into the target `<div>` element.
        nsAtom* containerName =
            atListItem.GetContainer()->NodeInfo()->NameAtom();
        if (targetDivElement) {
          // XXX Do we need to split the container? Since we'll append new
          //     element at end of the <div> element.
          Result<SplitNodeResult, nsresult> splitNodeResult =
              MaybeSplitAncestorsForInsertWithTransaction(
                  MOZ_KnownLive(*containerName), atListItem, aEditingHost);
          if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::MaybeSplitAncestorsForInsertWithTransaction() "
                "failed");
            return splitNodeResult.unwrapErr();
          }
          // We'll update selection after creating a list element below.
          // Therefore, we don't need to touch selection here.
          splitNodeResult.inspect().IgnoreCaretPointSuggestion();
        } else {
          // If we've not had a target <div> element yet, let's insert a <div>
          // element with splitting the ancestors.
          Result<CreateElementResult, nsresult> createNewDivElementResult =
              InsertElementWithSplittingAncestorsWithTransaction(
                  *nsGkAtoms::div, atContent, BRElementNextToSplitPoint::Keep,
                  aEditingHost);
          if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::"
                "InsertElementWithSplittingAncestorsWithTransaction("
                "nsGkAtoms::div) failed");
            return createNewDivElementResult.unwrapErr();
          }
          // We'll update selection after creating a list element below.
          // Therefore, we don't need to touch selection here.
          createNewDivElementResult.inspect().IgnoreCaretPointSuggestion();
          MOZ_ASSERT(createNewDivElementResult.inspect().GetNewNode());
          targetDivElement = createNewDivElementResult.unwrap().UnwrapNewNode();
        }
        // XXX So, createdListElement may be set to a non-list element.
        Result<CreateElementResult, nsresult> createNewListElementResult =
            CreateAndInsertElement(WithTransaction::Yes,
                                   MOZ_KnownLive(*containerName),
                                   EditorDOMPoint::AtEndOf(targetDivElement));
        if (MOZ_UNLIKELY(createNewListElementResult.isErr())) {
          NS_WARNING(
              "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes) "
              "failed");
          return createNewListElementResult.unwrapErr();
        }
        nsresult rv = createNewListElementResult.inspect().SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CreateElementResult::SuggestCaretPointTo() failed, but ignored");
        createdListElement =
            createNewListElementResult.unwrap().UnwrapNewNode();
        MOZ_ASSERT(createdListElement);
      }
      // Move current list item element into the createdListElement (could be
      // non-list element due to the above bug) in a candidate `<div>` element
      // to be positioned absolutely.
      Result<MoveNodeResult, nsresult> moveListItemElementResult =
          MoveNodeToEndWithTransaction(*listItemElement, *createdListElement);
      if (MOZ_UNLIKELY(moveListItemElementResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveListItemElementResult.unwrapErr();
      }
      nsresult rv = moveListItemElementResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
        return Err(rv);
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
      handledListItemElement = std::move(listItemElement);
      continue;
    }

    if (!targetDivElement) {
      // If we meet a `<div>` element, use it as the absolute-position
      // container.
      // XXX This looks odd.  If there are 2 or more `<div>` elements are
      //     selected, first found `<div>` element will have all other
      //     selected nodes.
      if (content->IsHTMLElement(nsGkAtoms::div)) {
        targetDivElement = content->AsElement();
        MOZ_ASSERT(!createdListElement);
        MOZ_ASSERT(!handledListItemElement);
        continue;
      }
      // Otherwise, create new `<div>` element to be positioned absolutely
      // and to contain all selected nodes.
      Result<CreateElementResult, nsresult> createNewDivElementResult =
          InsertElementWithSplittingAncestorsWithTransaction(
              *nsGkAtoms::div, atContent, BRElementNextToSplitPoint::Keep,
              aEditingHost);
      if (MOZ_UNLIKELY(createNewDivElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertElementWithSplittingAncestorsWithTransaction("
            "nsGkAtoms::div) failed");
        return createNewDivElementResult.unwrapErr();
      }
      nsresult rv = createNewDivElementResult.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
      if (NS_FAILED(rv)) {
        NS_WARNING("CreateElementResult::SuggestCaretPointTo() failed");
        return rv;
      }
      MOZ_ASSERT(createNewDivElementResult.inspect().GetNewNode());
      targetDivElement = createNewDivElementResult.unwrap().UnwrapNewNode();
    }

    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveNodeToEndWithTransaction(MOZ_KnownLive(content), *targetDivElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveNodeResult.unwrapErr();
    }
    nsresult rv = moveNodeResult.inspect().SuggestCaretPointTo(
        *this, {SuggestCaret::OnlyIfHasSuggestion,
                SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("MoveNodeResult::SuggestCaretPointTo() failed");
      return rv;
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "MoveNodeResult::SuggestCaretPointTo() failed, but ignored");
    // Forget createdListElement, if any
    createdListElement = nullptr;
  }
  *aTargetElement = std::move(targetDivElement);
  return NS_OK;
}

Result<EditActionResult, nsresult>
HTMLEditor::SetSelectionToStaticAsSubAction() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSetPositionToStatic, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(*editingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  RefPtr<Element> element = GetAbsolutelyPositionedSelectionContainer();
  if (!element) {
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING(
        "HTMLEditor::GetAbsolutelyPositionedSelectionContainer() returned "
        "nullptr");
    return Err(NS_ERROR_FAILURE);
  }

  {
    AutoSelectionRestorer restoreSelectionLater(this);

    nsresult rv = SetPositionToAbsoluteOrStatic(*element, false);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::SetPositionToAbsoluteOrStatic() failed");
      return Err(rv);
    }
  }

  // Restoring Selection might cause destroying the HTML editor.
  if (MOZ_UNLIKELY(Destroyed())) {
    NS_WARNING("Destroying AutoSelectionRestorer caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  return EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult> HTMLEditor::AddZIndexAsSubAction(
    int32_t aChange) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this,
      aChange < 0 ? EditSubAction::eDecreaseZIndex
                  : EditSubAction::eIncreaseZIndex,
      nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return Err(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(*editingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  RefPtr<Element> absolutelyPositionedElement =
      GetAbsolutelyPositionedSelectionContainer();
  if (!absolutelyPositionedElement) {
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING(
        "HTMLEditor::GetAbsolutelyPositionedSelectionContainer() returned "
        "nullptr");
    return Err(NS_ERROR_FAILURE);
  }

  nsStyledElement* absolutelyPositionedStyledElement =
      nsStyledElement::FromNode(absolutelyPositionedElement);
  if (NS_WARN_IF(!absolutelyPositionedStyledElement)) {
    return Err(NS_ERROR_FAILURE);
  }

  {
    AutoSelectionRestorer restoreSelectionLater(this);

    // MOZ_KnownLive(*absolutelyPositionedStyledElement): It's
    // absolutelyPositionedElement whose type is RefPtr.
    Result<int32_t, nsresult> result = AddZIndexWithTransaction(
        MOZ_KnownLive(*absolutelyPositionedStyledElement), aChange);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::AddZIndexWithTransaction() failed");
      return result.propagateErr();
    }
  }

  // Restoring Selection might cause destroying the HTML editor.
  if (MOZ_UNLIKELY(Destroyed())) {
    NS_WARNING("Destroying AutoSelectionRestorer caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  return EditActionResult::HandledResult();
}

nsresult HTMLEditor::OnDocumentModified(
    const nsIContent* aContentWillBeRemoved /* = nullptr */) {
  if (mPendingDocumentModifiedRunner) {
    mPendingDocumentModifiedRunner->MaybeAppendNewInvisibleWhiteSpace(
        aContentWillBeRemoved);
    return NS_OK;  // We've already posted same runnable into the queue.
  }
  mPendingDocumentModifiedRunner = new DocumentModifiedEvent(*this);
  mPendingDocumentModifiedRunner->MaybeAppendNewInvisibleWhiteSpace(
      aContentWillBeRemoved);
  nsContentUtils::AddScriptRunner(do_AddRef(mPendingDocumentModifiedRunner));
  // Be aware, if OnModifyDocument() may be called synchronously, the
  // editor might have been destroyed here.
  return NS_WARN_IF(Destroyed()) ? NS_ERROR_EDITOR_DESTROYED : NS_OK;
}

}  // namespace mozilla
