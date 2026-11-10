/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorBase.h"
#include "HTMLEditor.h"
#include "HTMLEditorInlines.h"
#include "HTMLEditorNestedClasses.h"

#include <fmt/format.h>
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
#include "mozilla/Logging.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/TextComposition.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/Range.h"
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
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStyledElement.h"
#include "nsTArray.h"
#include "nsTextNode.h"
#include "nsThreadUtils.h"

class nsISupports;

namespace mozilla {

extern LazyLogModule gTextInputLog;  // Defined in EditorBase.cpp

using namespace dom;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using EmptyCheckOptions = HTMLEditUtils::EmptyCheckOptions;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;
using LeafNodeOptions = HTMLEditUtils::LeafNodeOptions;
using WalkTextOption = HTMLEditUtils::WalkTextOption;
using WalkTreeDirection = HTMLEditUtils::WalkTreeDirection;

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

template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMRange& aRange);
template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMRange& aRange);
template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint);
template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint);
template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMPoint& aStartPoint, const EditorRawDOMPoint& aEndPoint);
template already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorRawDOMPoint& aStartPoint, const EditorRawDOMPoint& aEndPoint);

nsresult HTMLEditor::InitEditorContentAndSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // We should do nothing with the result of GetRoot() if only a part of the
  // document is editable.
  if (!EntireDocumentIsEditable()) {
    return NS_OK;
  }

  // Don't create padding BR element if the <body> is an EditContext editing
  // host. (This can be called even if no element is focused, so we can't
  // rely on the active EditContext here which would be null in those cases.)
  if (Element* body = GetBodyElement()) {
    if (body->HasFlag(ELEMENT_HAS_EDIT_CONTEXT) &&
        !body->GetParentNode()->IsEditable()) {
      return NS_OK;
    }
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
      RawRangeBoundary::StartOfParent(*bodyOrDocumentElement),
      RawRangeBoundary::EndOfParent(*bodyOrDocumentElement));
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
    if (const dom::Range* range = SelectionRef().GetRangeAt(0)) {
      TopLevelEditSubActionDataRef().mSelectedRange->StoreRange(*range);
    }
  }

  // Register with range updater to track this as we perturb the doc
  RangeUpdaterRef().RegisterRangeItem(
      *TopLevelEditSubActionDataRef().mSelectedRange);

  // Remember current inline styles for deletion and normal insertion ops
  const bool cacheInlineStyles = [&]() {
    if (GetEditActionEditContext()) {
      // EditContext doesn't have styles.
      return false;
    }
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
        "HTMLEditor::OnEndHandlingTopLevelEditSubActionInternal() failed");
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

  if (GetTopLevelEditSubAction() ==
      EditSubAction::eCreatePaddingBRElementForEmptyEditor) {
    return NS_OK;
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
            RefPtr<dom::Range> extendedChangedRange =
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
            RefPtr<dom::Range> extendedChangedRange = AutoClonedRangeArray::
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
      const RefPtr<Element> editingHost =
          ComputeEditingHost(LimitInBodyElement::No);
      if (!editingHost) [[unlikely]] {
        return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
      }
      Result<CreateLineBreakResult, nsresult>
          insertPaddingBRElementResultOrError =
              InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded(
                  newCaretPosition, *editingHost);
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

  // For EditContext, the DOM is not directly updated by libeditor, so
  // don't run spellcheck here.
  if (!GetEditActionEditContext()) {
    rv = HandleInlineSpellCheck(
        TopLevelEditSubActionDataRef().mSelectedRange->StartPoint(),
        TopLevelEditSubActionDataRef().mChangedRange);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::HandleInlineSpellCheck() failed");
      return rv;
    }
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
    CheckSelectionInReplacedElement
        aCheckSelectionInReplacedElement /* = ::Yes */) const {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }

  if (GetEditActionEditContext()) {
    // For EditContext, we always accept text input, even if the selection
    // is in a non-editable node.
    return EditActionResult::IgnoredResult();
  }

  // If there is not selection ranges, we should ignore the result.
  if (!SelectionRef().RangeCount()) {
    return EditActionResult::CanceledResult();
  }

  const dom::Range* range = SelectionRef().GetRangeAt(0);
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

  using ReplaceOrVoidElementOption = HTMLEditUtils::ReplaceOrVoidElementOption;

  if (selStartNode == selEndNode) {
    if (aCheckSelectionInReplacedElement ==
            CheckSelectionInReplacedElement::Yes &&
        HTMLEditUtils::GetInclusiveAncestorReplacedOrVoidElement(
            *selStartNode->AsContent(),
            ReplaceOrVoidElementOption::LookForOnlyNonVoidReplacedElement)) {
      return EditActionResult::CanceledResult();
    }
    return EditActionResult::IgnoredResult();
  }

  if (aCheckSelectionInReplacedElement != CheckSelectionInReplacedElement::No &&
      (HTMLEditUtils::GetInclusiveAncestorReplacedOrVoidElement(
           *selStartNode->AsContent(),
           ReplaceOrVoidElementOption::LookForOnlyNonVoidReplacedElement) ||
       HTMLEditUtils::GetInclusiveAncestorReplacedOrVoidElement(
           *selEndNode->AsContent(),
           ReplaceOrVoidElementOption::LookForOnlyNonVoidReplacedElement))) {
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
  const dom::Range* firstRange = SelectionRef().GetRangeAt(0);
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

  const WSScanResult prevThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          atSelectionStart, &aEditingHost);
  if (!prevThing.ReachedLineBreak()) {
    return NS_OK;
  }
  EditorRawLineBreak unnecessaryLineBreak =
      prevThing.CreateEditorLineBreak<EditorRawLineBreak>();
  if (!unnecessaryLineBreak.IsFollowedByBlockBoundary()) {
    return NS_OK;
  }
  if (!unnecessaryLineBreak.ContentRef().GetParent() ||
      !unnecessaryLineBreak.ContentRef().IsInclusiveDescendantOf(&aEditingHost))
      [[unlikely]] {
    return NS_OK;
  }
  if (unnecessaryLineBreak.IsPreformattedLineBreak() &&
      NS_WARN_IF(
          !HTMLEditUtils::IsSimplyEditableNode(
              unnecessaryLineBreak.ContentRef()) &&
          !unnecessaryLineBreak.IsPreformattedLineBreakAtStartOfText())) {
    // If the preceding unnecessary preformatted line break is a part of a
    // non-editable visible Text, we cannot put caret into it. Then, typing
    // something will cause a new line because the unnecessary line break
    // becomes visible.
    return NS_OK;
  }
  EditorRawDOMPoint pointToPutCaret =
      unnecessaryLineBreak.To<EditorRawDOMPoint>();
  for (nsIContent* container :
       pointToPutCaret.GetContainer()->InclusiveAncestorsOfType<nsIContent>()) {
    if (!HTMLEditUtils::IsSimplyEditableNode(*container)) [[unlikely]] {
      if (NS_WARN_IF(container->GetPreviousSibling())) {
        // If the non-editable node is not the first child, we need to put caret
        // too far. Therefore, we should keep current selection. Although typing
        // something will cause a new line because the unnecessary line break
        // becomes visible.
        return NS_OK;
      }
      continue;
    }
    if (container != pointToPutCaret.GetContainer()) {
      MOZ_ASSERT(container->GetFirstChild());
      MOZ_ASSERT(
          !HTMLEditUtils::IsSimplyEditableNode(*container->GetFirstChild()));
      pointToPutCaret = EditorRawDOMPoint(container, 0);
    }
    break;
  }
  MOZ_ASSERT(pointToPutCaret.IsSet());
  MOZ_ASSERT(
      pointToPutCaret.GetContainer()->IsInclusiveDescendantOf(&aEditingHost));

  // If we are here then the selection is right after a padding line break for
  // empty last line that is in the same block as the selection.  We need to
  // move the selection start to be before the padding line break node.
  // FIXME: We should return the position instead of updating the Selection.
  nsresult rv = CollapseSelectionTo(pointToPutCaret);
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

  // Skip adding <br> element for EditContext editors.
  if (GetEditActionEditContext()) {
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
  nsIContent* firstLeafChild =
      HTMLEditUtils::GetFirstLeafContent(*mRootElement, {});
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

  MOZ_LOG(
      gTextInputLog, LogLevel::Info,
      ("%p HTMLEditor::HandleInsertText(aInsertionString=\"%s\", aPurpose=%s)",
       this, NS_ConvertUTF16toUTF8(aInsertionString).get(),
       ToString(aPurpose).c_str()));

  {
    Result<EditActionResult, nsresult> result =
        CanHandleHTMLEditSubAction(CheckSelectionInReplacedElement::No);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  UndefineCaretBidiLevel();

  if (RefPtr editContext = GetEditActionEditContext()) {
    uint32_t start = editContext->SelectionStart();
    uint32_t end = editContext->SelectionEnd();
    RefPtr<nsFrameSelection> frameSelection = GetEditableFrameSelection();
    if (InsertingTextForComposition(aPurpose)) {
      MOZ_ASSERT(mComposition);
      if (mComposition->GetContainerTextNode()) {
        start = mComposition->ClampedStartOffsetInTextNode();
        end = mComposition->ClampedEndOffsetInTextNode();
      }
      mComposition->OnUpdateCompositionInEditor(aInsertionString,
                                                editContext->TextNode(), start);
    }
    editContext->UpdateTextAndFireEvent(start, end, aInsertionString);
    if (NS_WARN_IF(Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (EditContextChangedSinceStartOfEditAction()) {
      // textupdate handler deactivated this EditContext
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }

    // Set caret association to "before" after text insertion.
    // Without this, the caret will not be next to the
    // inserted text when inserting LTR into RTL, for example.
    // However, we don't do this if the web app changed the text next to the
    // caret, since in that case this may not be a simple insertion.
    if (frameSelection &&
        !editContext->WasTextNextToCaretChangedByTextUpdateHandler()) {
      frameSelection->SetHint(CaretAssociationHint::Before);
    }
    return EditActionResult::HandledResult();
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  // For the compatibility with the other browsers, we shouldn't delete extant
  // unnecessary line break at inserting text because it may be used as a marker
  // by the web app. Especially when IME started composition, modifying the
  // editable content which is caused by that the web app restores the
  // unnecessary line break may lead committing the composition.
  const Maybe<EditorLineBreak> preservingUnnecessaryLineBreak =
      [&]() MOZ_NEVER_INLINE_DEBUG -> Maybe<EditorLineBreak> {
    const auto selectionRangeEnd =
        EditorBase::GetFirstSelectionEndPoint<EditorRawDOMPoint>();
    if (!selectionRangeEnd.IsSet()) [[unlikely]] {
      return Nothing{};
    }
    const WSScanResult nextThing =
        HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
            selectionRangeEnd,
            // For the compatibility with the other browsers, we should delete
            // unecessary line break if and only if it's a padding line break
            // for empty block or it makes an empty line before a child block
            // boundary. Therefore, we should treat a padding line break for an
            // empty block is significant here.
            TreatAsUnnecessaryLineBreak{}, *editingHost);
    return nextThing.MaybeIgnoredLineBreak();
  }();

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
    if (NS_WARN_IF(editingHost != ComputeEditingHost(LimitInBodyElement::No))) {
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
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

  EditorDOMPoint pointToInsert = [&]() {
    if (InsertingTextForExtantComposition(aPurpose)) {
      auto compositionStartPoint =
          GetFirstIMESelectionStartPoint<EditorDOMPoint>();
      if (MOZ_LIKELY(compositionStartPoint.IsSet())) {
        return compositionStartPoint;
      }
    }
    return GetFirstSelectionStartPoint<EditorDOMPoint>();
  }();

  MOZ_LOG(gTextInputLog, LogLevel::Info,
          ("%p HTMLEditor::HandleInsertText(), pointToInsert=%s", this,
           ToString(pointToInsert).c_str()));

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
  pointToInsert = HTMLEditUtils::GetPossiblePointToInsert(
      pointToInsert, *nsGkAtoms::textTagName, *editingHost);
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(pointToInsert.IsInContentNode());

  if (InsertingTextForComposition(aPurpose)) {
    if (aInsertionString.IsEmpty()) {
      // Right now the WhiteSpaceVisibilityKeeper code bails on empty strings,
      // but IME needs the InsertTextWithTransaction() call to still happen
      // since empty strings are meaningful there.
      Result<InsertTextResult, nsresult> insertEmptyTextResultOrError =
          InsertTextWithTransaction(aInsertionString, pointToInsert,
                                    InsertTextTo::ExistingTextNodeIfAvailable,
                                    aPurpose);
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
      // Don't delete extant unnecessary line break because it may be inserted
      // by the web app as a private marker.
      if (preservingUnnecessaryLineBreak.isNothing()) [[likely]] {
        nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
            insertEmptyTextResult.EndOfInsertedTextRef(),
            // When user inserting text, the web app may expect that nothing
            // extant content will be deleted. Therefore, we should preserve
            // preformatted linefeed at least.
            PreservePreformattedLineBreak::Yes,
            PaddingForEmptyBlock::Unnecessary, *editingHost);
        if (NS_FAILED(rv)) {
          NS_WARNING(
              "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
          return Err(rv);
        }
      }
      const EditorDOMPoint& endOfInsertedText =
          insertEmptyTextResult.EndOfInsertedTextRef();
      if (endOfInsertedText.IsInTextNode() &&
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

    EditorDOMPoint endOfInsertedText;
    {
      AutoTrackDOMPoint trackPointToInsert(RangeUpdaterRef(), &pointToInsert);
      const auto compositionEndPoint =
          GetLastIMESelectionEndPoint<EditorDOMPoint>();
      Result<InsertTextResult, nsresult> replaceTextResult =
          WhiteSpaceVisibilityKeeper::InsertOrUpdateCompositionString(
              *this, aInsertionString,
              compositionEndPoint.IsSet()
                  ? EditorDOMRange(pointToInsert, compositionEndPoint)
                  : EditorDOMRange(pointToInsert),
              preservingUnnecessaryLineBreak, aPurpose, *editingHost);
      if (MOZ_UNLIKELY(replaceTextResult.isErr())) {
        NS_WARNING("WhiteSpaceVisibilityKeeper::ReplaceText() failed");
        return replaceTextResult.propagateErr();
      }
      InsertTextResult unwrappedReplaceTextResult = replaceTextResult.unwrap();
      endOfInsertedText = unwrappedReplaceTextResult.EndOfInsertedTextRef();
      if (InsertingTextForCommittingComposition(aPurpose)) {
        // If we're committing the composition,
        // WhiteSpaceVisibilityKeeper::InsertOrUpdateCompositionString() may
        // replace the last character of the composition string when it's a
        // white-space.  Then, Selection will be moved before the last
        // character.  So, we need to adjust Selection here.
        nsresult rv = unwrappedReplaceTextResult.SuggestCaretPointTo(
            *this, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                    SuggestCaret::AndIgnoreTrivialError});
        if (NS_FAILED(rv)) {
          NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
          return Err(rv);
        }
        NS_WARNING_ASSERTION(
            rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
            "CaretPoint::SuggestCaretPoint() failed, but ignored");
      } else {
        // CompositionTransaction should've set selection so that we should
        // ignore caret suggestion.
        unwrappedReplaceTextResult.IgnoreCaretPointSuggestion();
      }
    }

    if (!InsertingTextForCommittingComposition(aPurpose)) {
      const auto newCompositionStartPoint =
          GetFirstIMESelectionStartPoint<EditorDOMPoint>();
      const auto newCompositionEndPoint =
          GetLastIMESelectionEndPoint<EditorDOMPoint>();
      if (NS_WARN_IF(!newCompositionStartPoint.IsSet()) ||
          NS_WARN_IF(!newCompositionEndPoint.IsSet())) {
        // Mutation event listener has changed the DOM tree...
        return EditActionResult::HandledResult();
      }
      nsresult rv =
          TopLevelEditSubActionDataRef().mChangedRange->SetStartAndEnd(
              newCompositionStartPoint.ToRawRangeBoundary(),
              newCompositionEndPoint.ToRawRangeBoundary());
      if (NS_FAILED(rv)) {
        NS_WARNING("Range::SetStartAndEnd() failed");
        return Err(rv);
      }
    } else {
      if (NS_WARN_IF(!endOfInsertedText.IsSetAndValidInComposedDoc()) ||
          NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
        return EditActionResult::HandledResult();
      }
      nsresult rv =
          TopLevelEditSubActionDataRef().mChangedRange->SetStartAndEnd(
              pointToInsert.ToRawRangeBoundary(),
              endOfInsertedText.ToRawRangeBoundary());
      if (NS_FAILED(rv)) {
        NS_WARNING("Range::SetStartAndEnd() failed");
        return Err(rv);
      }
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
      if (!aInsertionString.IsEmpty()) [[likely]] {
        // If the inserting string is not empty, we need to delete padding
        // line break after the insertion point first because X (Twitter)
        // expects that character data change will be notified at last.
        const WSScanResult nextThing = HTMLEditUtils::
            ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
                currentPoint,
                TreatAsUnnecessaryLineBreak{
                    LineBreakFor::PaddingForEmptyBlock,
                    LineBreakFor::EmptyLineBeforeBlockBoundary},
                *editingHost);
        if (nextThing.MaybeIgnoredLineBreak().isSome() &&
            // Don't delete extant unnecessary line break because it may be
            // inserted by the web app as a private marker.
            nextThing.MaybeIgnoredLineBreak() !=
                preservingUnnecessaryLineBreak &&
            !(IsPlaintextMailComposer() &&
              nextThing.IgnoredLineBreakIsRequiredInPlaintextMailComposer())) {
          const EditorLineBreak& lineBreak =
              nextThing.MaybeIgnoredLineBreak().ref();
          const RefPtr<Element> ancestorLimiterToDeleteEmptyInlines =
              lineBreak.ContentRef().IsInclusiveDescendantOf(
                  currentPoint.GetContainer())
                  ? currentPoint.GetContainerOrContainerParentElement()
                  : editingHost.get();
          {
            AutoTrackDOMPoint trackCurrentPoint(RangeUpdaterRef(),
                                                &currentPoint);
            Result<EditorDOMPoint, nsresult> deleteLineBreakResultOrError =
                DeleteLineBreakWithTransaction(
                    lineBreak, nsIEditor::eStrip,
                    *ancestorLimiterToDeleteEmptyInlines);
            if (deleteLineBreakResultOrError.isErr()) [[unlikely]] {
              NS_WARNING("HTMLEditor::DeleteLineBreakWithTransaction() failed");
              return deleteLineBreakResultOrError.propagateErr();
            }
          }
          if (NS_WARN_IF(!currentPoint.IsSetAndValidInComposedDoc())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
        }
      }
      if (*lineBreakType == LineBreakType::Linefeed) {
        // Both Chrome and us inserts a preformatted linefeed with its own
        // `Text` node in various cases.  However, when inserting multiline
        // text, we should insert a `Text` because Chrome does so and the
        // comment field in https://discussions.apple.com/ handles the new
        // `Text` to split each line into a paragraph.  At that time, it's
        // not assumed that inserted text is split at every linefeed.
        MOZ_ASSERT(*lineBreakType == LineBreakType::Linefeed);
        Result<InsertTextResult, nsresult> insertTextResult =
            InsertTextWithTransaction(aInsertionString, currentPoint,
                                      InsertTextTo::ExistingTextNodeIfAvailable,
                                      aPurpose);
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
          const int32_t inclusiveNextLinefeedOffset = aInsertionString.FindChar(
              HTMLEditUtils::kNewLine, lineStartOffset);
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
                                    lineStartOffset),
                    aPurpose);
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
            aInsertionString.FindChar(HTMLEditUtils::kNewLine, lineStartOffset);
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
                  *this, lineText, currentPoint, preservingUnnecessaryLineBreak,
                  GetInsertTextTo(inclusiveNextLinefeedOffset, lineStartOffset),
                  *editingHost);
            }
            nsAutoString formattedLineText(lineText);
            formattedLineText.ReplaceSubstring(u"\t"_ns, u"    "_ns);
            return WhiteSpaceVisibilityKeeper::InsertText(
                *this, formattedLineText, currentPoint,
                preservingUnnecessaryLineBreak,
                GetInsertTextTo(inclusiveNextLinefeedOffset, lineStartOffset),
                *editingHost);
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
    if (!aInsertionString.IsEmpty() &&
        aInsertionString.Last() == HTMLEditUtils::kNewLine) {
      Result<CreateLineBreakResult, nsresult> insertPaddingLineBreakResult =
          InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded(currentPoint,
                                                               *editingHost);
      if (insertPaddingLineBreakResult.isErr()) [[unlikely]] {
        NS_WARNING(
            "HTMLEditor::InsertPaddingBRElementToMakeEmptyLineVisibleIfNeeded()"
            " failed");
        return insertPaddingLineBreakResult.propagateErr();
      }
      if (insertPaddingLineBreakResult.inspect().HasCaretPointSuggestion()) {
        currentPoint = insertPaddingLineBreakResult.unwrap().UnwrapCaretPoint();
      }
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
      NS_WARNING("Range::SetStartAndEnd() failed");
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
    NS_WARNING("Range::CollapseTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
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
          {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
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
          {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
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
  const CharacterDataBuffer* const characterDataBuffer =
      textNode ? &textNode->DataBuffer() : nullptr;
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
    MOZ_ASSERT(characterDataBuffer);
    const auto nonWhiteSpaceOffset =
        HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
            *textNode, aPointToInsert.Offset(),
            {HTMLEditUtils::WalkTextOption::TreatNBSPsCollapsible});
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(characterDataBuffer->GetLength()) >=
               aPointToInsert.Offset());
    return nonWhiteSpaceOffset.valueOr(characterDataBuffer->GetLength()) -
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
          !characterDataBuffer || !result.mReplaceStartOffset ||
                  !aNormalizeSurroundingWhiteSpaces
              ? CharPointType::TextEnd
              : (characterDataBuffer->CharAt(result.mReplaceStartOffset - 1u) ==
                         HTMLEditUtils::kNewLine
                     ? CharPointType::PreformattedLineBreak
                     : CharPointType::VisibleChar)),
      CharPointData::InSameTextNode(
          !characterDataBuffer ||
                  result.mReplaceEndOffset >=
                      characterDataBuffer->GetLength() ||
                  !aNormalizeSurroundingWhiteSpaces
              ? CharPointType::TextEnd
              : (characterDataBuffer->CharAt(result.mReplaceEndOffset) ==
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
  const CharacterDataBuffer& characterDataBuffer = textNode.DataBuffer();

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
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(characterDataBuffer.GetLength()) >=
               aPoint.Offset());
    return nonWhiteSpaceOffset.valueOr(characterDataBuffer.GetLength()) -
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
                : (characterDataBuffer.CharAt(result.mReplaceStartOffset -
                                              1u) == HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= characterDataBuffer.GetLength()
                ? CharPointType::TextEnd
                : (characterDataBuffer.CharAt(result.mReplaceEndOffset) ==
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
    const CharacterDataBuffer& characterDataBuffer =
        aPointToSplit.ContainerAs<Text>()->DataBuffer();
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(CharPointType::TextEnd),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= characterDataBuffer.GetLength()
                ? CharPointType::TextEnd
                : (characterDataBuffer.CharAt(result.mReplaceEndOffset) ==
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
    const CharacterDataBuffer& characterDataBuffer =
        aPointToSplit.ContainerAs<Text>()->DataBuffer();
    HTMLEditor::NormalizeAllWhiteSpaceSequences(
        result.mNormalizedString,
        CharPointData::InSameTextNode(
            !result.mReplaceStartOffset
                ? CharPointType::TextEnd
                : (characterDataBuffer.CharAt(result.mReplaceStartOffset -
                                              1u) == HTMLEditUtils::kNewLine
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
  const CharacterDataBuffer& characterDataBuffer = aTextNode.DataBuffer();
  const char16_t precedingChar = aOffset
                                     ? characterDataBuffer.CharAt(aOffset - 1u)
                                     : static_cast<char16_t>(0);
  const char16_t followingChar =
      aOffset + aLength < characterDataBuffer.GetLength()
          ? characterDataBuffer.CharAt(aOffset + aLength)
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
    MOZ_ASSERT(nonWhiteSpaceOffset.valueOr(characterDataBuffer.GetLength()) >=
               aOffset + aLength);
    return nonWhiteSpaceOffset.valueOr(characterDataBuffer.GetLength()) -
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
                : (characterDataBuffer.CharAt(result.mReplaceStartOffset -
                                              1u) == HTMLEditUtils::kNewLine
                       ? CharPointType::PreformattedLineBreak
                       : CharPointType::VisibleChar)),
        CharPointData::InSameTextNode(
            result.mReplaceEndOffset >= characterDataBuffer.GetLength()
                ? CharPointType::TextEnd
                : (characterDataBuffer.CharAt(result.mReplaceEndOffset) ==
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
          {WSRunScanner::Option::OnlyEditableNodes}, aStartToDelete);
  const auto followingCharPoint =
      WSRunScanner::GetInclusiveNextCharPoint<EditorDOMPointInText>(
          {WSRunScanner::Option::OnlyEditableNodes}, aEndToDelete);
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
                normalizedWhiteSpacesInFirstNode, InsertTextFor::NormalText);
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
      MOZ_ASSERT(trackingEndToDelete.IsInTextNode());
      endToDelete.Set(trackingEndToDelete.ContainerAs<Text>(),
                      trackingEndToDelete.Offset());
      // If the remaining range was modified by mutation event listener,
      // we should stop handling the deletion.
      startToDelete =
          EditorDOMPointInText::AtEndOf(*startToDelete.ContainerAs<Text>());
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
        if (MaybeNodeRemovalsObservedByDevTools() &&
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
            normalizedWhiteSpacesInLastNode, InsertTextFor::NormalText);
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
              newCaretPosition,
              {LeafNodeOption::TreatNonEditableNodeAsLeafNode},
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
                       {LeafNodeOption::TreatNonEditableNodeAsLeafNode},
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
    nsresult rv = EnsureNoFollowingUnnecessaryLineBreak(
        newCaretPosition, PreservePreformattedLineBreak::No,
        PaddingForEmptyBlock::Significant, aEditingHost);
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
    trackingNewCaretPosition.Flush(StopTracking::Yes);
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
  const char16_t lastLeftChar = aLeftText.DataBuffer().SafeLastChar();
  char16_t firstRightChar = aRightText.DataBuffer().SafeFirstChar();
  const char16_t secondRightChar = aRightText.DataBuffer().GetLength() >= 2
                                       ? aRightText.DataBuffer().CharAt(1u)
                                       : static_cast<char16_t>(0);
  if (IsCollapsibleCharOrNBSP(firstRightChar)) {
    // If the right Text starts only with a collapsible white-space and it'll
    // follow a non-collapsible char, we should make it an ASCII white-space.
    if (secondRightChar && !IsCollapsibleCharOrNBSP(secondRightChar) &&
        lastLeftChar && !IsCollapsibleChar(lastLeftChar)) {
      if (firstRightChar != HTMLEditUtils::kSpace) {
        Result<InsertTextResult, nsresult> replaceWhiteSpaceResultOrError =
            ReplaceTextWithTransaction(aRightText, 0u, 1u, u" "_ns,
                                       InsertTextFor::NormalText);
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
             aLeftText.DataBuffer().GetLength() >= 2u) {
    // If the last char of the left `Text` is a single white-space but not an
    // ASCII space, let's replace it with an ASCII space.
    const char16_t secondLastChar =
        aLeftText.DataBuffer().CharAt(aLeftText.DataBuffer().GetLength() - 2u);
    if (!IsCollapsibleCharOrNBSP(secondLastChar) &&
        !IsCollapsibleCharOrNBSP(firstRightChar)) {
      Result<InsertTextResult, nsresult> replaceWhiteSpaceResultOrError =
          ReplaceTextWithTransaction(aLeftText,
                                     aLeftText.DataBuffer().GetLength() - 1u,
                                     1u, u" "_ns, InsertTextFor::NormalText);
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
    const EditorDOMPoint& aPointToInsert, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aPointToInsert.IsSet());

  if (!aPointToInsert.IsInContentNode() ||
      NS_WARN_IF(!aPointToInsert.GetContainerOrContainerParentElement()))
      [[unlikely]] {
    return CreateLineBreakResult::NotHandled();
  }

  // FYI: We don't need to put <br> if it reaches an inline editing host because
  // editing host has at least one line height by default even if it's empty and
  // it's tested by WPT to no <br> element is inserted in the cases.

  // If the point is not start of a line, we don't need to put a line break
  // here.
  const WSScanResult previousThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::OnlyEditableNodes}, aPointToInsert,
          &aEditingHost);
  if (!previousThing.ReachedLineBoundary()) {
    return CreateLineBreakResult::NotHandled();
  }

  // If the point is not followed by a block boundary, we don't need to put a
  // line break here.
  const WSScanResult nextThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          {}, aPointToInsert,
          // FIXME: We should not limit the scan range into the editing host.
          // However, WSRunScanner does not check the visibility so that
          // following invisible text like the conent of <script> may change the
          // result. Fortunately, inline editing host is not used so widely.
          // We should treat the inline editing host boundary as a block
          // boundary.
          &aEditingHost);
  if (!nextThing.ReachedBlockBoundary() &&
      !nextThing.ReachedInlineEditingHostBoundary()) {
    return CreateLineBreakResult::NotHandled();
  }

  EditorDOMPoint pointToInsert(aPointToInsert);
  AutoTrackDOMPoint trackPointToInsert(RangeUpdaterRef(), &pointToInsert);

  // Okay, there is no meaningful content in the line. However, there might be
  // visible empty inline containers or some invisible nodes like Comment.
  // For the compatibility with the other browsers, we should put <br> as far as
  // near aPointToInsert.
  if (previousThing.ReachedPreformattedLineBreak() &&
      !EditorUtils::IsWhiteSpacePreformatted(*previousThing.TextPtr())) {
    const EditorDOMPoint pointAfterLineBreak =
        previousThing.PointAtReachedContent<EditorDOMPoint>();
    if (!pointAfterLineBreak.IsEndOfContainer()) [[unlikely]] {
      // If the previous thing is a preformatted line break but it's middle of a
      // Text, we want to delete the invisible trailing white-spaces.
      Result<CaretPoint, nsresult> caretPointOrError =
          WhiteSpaceVisibilityKeeper::DeleteInvisibleASCIIWhiteSpaces(
              *this, pointAfterLineBreak);
      if (caretPointOrError.isErr()) [[unlikely]] {
        NS_WARNING(
            "WhiteSpaceVisibilityKeeper::DeleteInvisibleASCIIWhiteSpaces() "
            "failed");
      }
      caretPointOrError.unwrap().IgnoreCaretPointSuggestion();
      trackPointToInsert.Flush(StopTracking::No);
    }
  }
  if (Element* const containerElement =
          pointToInsert.GetContainerOrContainerParentElement()) {
    if (!HTMLEditor::CanInsertLineBreak(LineBreakType::BRElement,
                                        *containerElement)) [[unlikely]] {
      // FIXME: We're deleting empty blocks at the post-processing after this
      // this called. Therefore, here may be in an empty list element.
      // Therefore, even if we cannot insert a <br>, we should return "not
      // handled" for now. We should make the delete handler delete empty blocks
      // by themselves and stop doing it in the post-processor. Then, return
      // error via PrepareToInsertLineBreak(). See bug 2019187.
      return CreateLineBreakResult::NotHandled();
    }
  } else {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  Result<EditorDOMPoint, nsresult> pointToInsertOrError =
      PrepareToInsertLineBreak(LineBreakType::BRElement, pointToInsert);
  if (pointToInsertOrError.isErr()) [[unlikely]] {
    NS_WARNING(
        "HTMLEditor::PrepareToInsertLineBreak(LineBreakType::BRElement) "
        "failed");
    return pointToInsertOrError.propagateErr();
  }
  trackPointToInsert.Flush(StopTracking::Yes);
  EditorDOMPoint pointToPutCaret = pointToInsert;
  pointToInsert = pointToInsertOrError.unwrap();
  AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
  const BRElementType brElementType = [&]() {
    // The serializer requires a normal <br> in the empty block. See bug
    // 1385905.
    if (nextThing.ReachedCurrentBlockBoundary() &&
        previousThing.ReachedCurrentBlockBoundary()) {
      return BRElementType::Normal;
    }
    return BRElementType::PaddingForEmptyLastLine;
  }();
  Result<CreateElementResult, nsresult> insertLineBreakResultOrError =
      InsertBRElement(WithTransaction::Yes, brElementType, pointToInsert);
  if (insertLineBreakResultOrError.isErr()) [[unlikely]] {
    NS_WARNING(
        fmt::format(
            "HTMLEditor::InsertLineBreak(WithTransaction::Yes, {}) failed",
            brElementType)
            .c_str());
    return insertLineBreakResultOrError.propagateErr();
  }
  trackPointToPutCaret.Flush(StopTracking::Yes);
  return CreateLineBreakResult(insertLineBreakResultOrError.unwrap(),
                               std::move(pointToPutCaret));
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
    HTMLEditor& aHTMLEditor, AutoClonedRangeArray& /*aRanges*/,
    AutoContentNodeArray& aArrayOfContents, const Element& aEditingHost) const {
  // if there is only one node in the array, and it is a list, div, or
  // blockquote, then look inside of it until we find inner list or content.
  if (aArrayOfContents.Length() == 1) {
    if (Element* const deepestDivBlockquoteOrListElement =
            HTMLEditUtils::GetInclusiveDeepestFirstChildWhichHasOneChild(
                aArrayOfContents[0], {LeafNodeOption::IgnoreNonEditableNode},
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
  if (HTMLEditUtils::IsListElement(aHandlingContent)) {
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
  if (HTMLEditUtils::IsListItemElement(aHandlingContent)) {
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
  MOZ_ASSERT(HTMLEditUtils::IsListElement(aHandlingListElement));

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
  MOZ_ASSERT(HTMLEditUtils::IsListItemElement(aHandlingListItemElement));
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
  MOZ_ASSERT(HTMLEditUtils::IsListItemElement(aHandlingListItemElement));

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
  MOZ_ASSERT(HTMLEditUtils::IsListItemElement(aHandlingListItemElement));
  MOZ_ASSERT(
      aHandlingListItemElement.GetParent()->IsHTMLElement(&mListTagName));

  EditorDOMPoint atListItem(&aHandlingListItemElement);
  MOZ_ASSERT(atListItem.IsInContentNode());

  // If we've not met a list element, set current list element to the
  // parent of current list item element.
  if (!aState.mCurrentListElement) {
    aState.mCurrentListElement = atListItem.GetContainerAs<Element>();
    NS_WARNING_ASSERTION(
        HTMLEditUtils::IsListElement(*aState.mCurrentListElement),
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
    const OwningNonNull<nsIContent>& content = arrayOfContents[i];
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      arrayOfContents.RemoveElementAt(i);
    }
  }

  // Only act on lists or list items in the array
  for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
    // here's where we actually figure out what to do
    if (HTMLEditUtils::IsListItemElement(*content)) {
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
    if (HTMLEditUtils::IsListElement(*content)) {
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
      if (nsCOMPtr<nsIContent> brContent = HTMLEditUtils::GetNextLeafContent(
              pointToInsertBlock, {LeafNodeOption::IgnoreNonEditableNode},
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
    if (nsCOMPtr<nsIContent> maybeBRContent =
            HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                pointToInsertBlock,
                {LeafNodeOption::IgnoreNonEditableNode,
                 LeafNodeOption::TreatChildBlockAsLeafNode},
                BlockInlineCheck::UseComputedDisplayOutsideStyle,
                &aEditingHost)) {
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
  MOZ_ASSERT(aPointInListElement.IsInContentNode());
  MOZ_ASSERT(HTMLEditUtils::IsListElement(
      *aPointInListElement.ContainerAs<nsIContent>()));
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  // some logic for putting list items into nested lists...

  // If aContentMovingToSubList is followed by a sub-list element whose tag is
  // same as the parent list element's tag, we can move it to start of the
  // sub-list.
  if (nsIContent* const nextEditableSibling = HTMLEditUtils::GetNextSibling(
          aContentMovingToSubList,
          {LeafNodeOption::IgnoreInvisibleText,
           LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    if (HTMLEditUtils::IsListElement(*nextEditableSibling) &&
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
  if (const nsCOMPtr<nsIContent> previousEditableSibling =
          HTMLEditUtils::GetPreviousSibling(
              aContentMovingToSubList,
              {LeafNodeOption::IgnoreInvisibleText,
               LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
    if (HTMLEditUtils::IsListElement(*previousEditableSibling) &&
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
                             {LeafNodeOption::IgnoreInvisibleText,
                              LeafNodeOption::IgnoreNonEditableNode},
                             BlockInlineCheck::UseComputedDisplayOutsideStyle)
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
        HTMLEditUtils::IsListItemElement(*editableBlockElement)) {
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
  for (size_t i = 0; i < arrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = arrayOfContents[i];

    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
      continue;
    }

    // Ignore all non-editable nodes.  Leave them be.
    // XXX We ignore non-editable nodes here, but not so in the above block.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    if (HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>())) {
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

    const auto IsMovableContentSibling = [&](const nsIContent& aContent) {
      return HTMLEditUtils::IsSimplyEditableNode(aContent) &&
             !HTMLEditUtils::IsBlockElement(
                 aContent, BlockInlineCheck::UseHTMLDefaultStyle);
    };
    MOZ_ASSERT(IsMovableContentSibling(content));
    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < arrayOfContents.Length(); i++) {
        nsIContent* const nextContent = arrayOfContents[i + 1];
        if (lastContent->GetNextSibling() != nextContent ||
            !IsMovableContentSibling(*nextContent)) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();
    // Move the content into the <div> which has start margin.
    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
    // keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *divElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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
  for (size_t i = 0; i < arrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = arrayOfContents[i];

    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
      continue;
    }

    const auto IsNotHandlableContent = [](const nsIContent& aContent) {
      // Ignore all non-editable nodes.  Leave them be.
      // XXX We ignore non-editable nodes here, but not so in the above
      // block.
      return !EditorUtils::IsEditableContent(aContent, EditorType::HTML) ||
             !HTMLEditUtils::IsRemovableNode(aContent);
    };

    const auto IsMovableContentSibling = [&](const nsIContent& aContent) {
      return !IsNotHandlableContent(aContent) &&
             !HTMLEditUtils::IsListItemElement(aContent);
    };

    if (IsNotHandlableContent(content)) {
      continue;
    }

    // If the content has been moved to different place, ignore it.
    if (!content->IsInclusiveDescendantOf(&aEditingHost)) {
      continue;
    }

    if (HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>())) {
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
            HTMLEditUtils::GetClosestInclusiveAncestorListItemElement(
                content, &aEditingHost)) {
      if (indentedListItemElement == listItem) {
        // already indented this list item
        continue;
      }
      // check to see if subListElement is still appropriate.  Which it is if
      // content is still right after it in the same list.
      nsIContent* const previousEditableSibling =
          subListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    *listItem, {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle)
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

    MOZ_ASSERT(IsMovableContentSibling(content));
    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < arrayOfContents.Length(); i++) {
        const OwningNonNull<nsIContent>& nextContent = arrayOfContents[i + 1];
        if (lastContent->GetNextSibling() != nextContent ||
            !IsMovableContentSibling(nextContent)) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();
    // tuck the node into the end of the active blockquote
    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
    // keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *blockquoteElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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
    const dom::Range* firstRange = SelectionRef().GetRangeAt(0);
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
    const dom::Range* firstRange = SelectionRef().GetRangeAt(0);
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
  for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
    // Here's where we actually figure out what to do
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
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
    if (HTMLEditUtils::IsListItemElement(*content)) {
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
          !HTMLEditUtils::IsAnyTableElementExceptColumnElement(*parentContent));
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
      if (startMargin > 0 && !(HTMLEditUtils::IsListElement(
                                   *atContent.ContainerAs<nsIContent>()) &&
                               HTMLEditUtils::IsListElement(*content))) {
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
    if (HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>())) {
      if (!HTMLEditUtils::IsListElement(*content)) {
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
    if (HTMLEditUtils::IsListElement(*content)) {
      // XXX If mutation event listener appends new children forever, this
      //     becomes an infinite loop so that we should set limitation from
      //     first child count.
      for (nsCOMPtr<nsIContent> lastChildContent = content->GetLastChild();
           lastChildContent; lastChildContent = content->GetLastChild()) {
        if (HTMLEditUtils::IsListItemElement(*lastChildContent)) {
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

        if (HTMLEditUtils::IsListElement(*lastChildContent)) {
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
    nsIContent& aEndOfRange, BlockInlineCheck /*aBlockInlineCheck*/) {
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
    Element& childElement = *childContent->AsElement();
    if (HTMLEditUtils::IsListItemElement(childElement) &&
        !childContent->IsHTMLElement(&aNewListItemTag)) {
      // MOZ_KnownLive(childElement) because its lifetime is guaranteed by
      // listElementChildren.
      Result<CreateElementResult, nsresult>
          replaceWithNewListItemElementResult =
              ReplaceContainerAndCloneAttributesWithTransaction(
                  MOZ_KnownLive(childElement), aNewListItemTag);
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
    if (HTMLEditUtils::IsListElement(childElement) &&
        !childElement.IsHTMLElement(&aNewListTag)) {
      // XXX List elements shouldn't have other list elements as their
      //     child.  Why do we handle such invalid tree?
      //     -> Maybe, for bug 525888.
      // MOZ_KnownLive(childElement) because its lifetime is guaranteed by
      // listElementChildren.
      Result<CreateElementResult, nsresult> convertListTypeResult =
          ChangeListElementType(MOZ_KnownLive(childElement), aNewListTag,
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
    return CreateElementResult(aListElement, std::move(pointToPutCaret));
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
  nsresult rv =
      SetInlinePropertiesAroundRanges(ranges, stylesToSet, aEditingHost);
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
          *container->AsContent(), {});
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
    const OwningNonNull<nsIContent>& content = arrayOfContents[0];

    if (HTMLEditUtils::IsAlignAttrSupported(content) &&
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
      if (NS_WARN_IF(!firstRangeStartPoint.IsInContentNode())) {
        return NS_ERROR_FAILURE;
      }
      nsIContent& parent = *firstRangeStartPoint.ContainerAs<nsIContent>();
      createEmptyDivElement =
          !HTMLEditUtils::IsAnyTableElementExceptColumnElement(parent) ||
          HTMLEditUtils::IsTableCellOrCaptionElement(parent);
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
  for (size_t i = 0; i < aArrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = aArrayOfContents[i];

    // Ignore all non-editable nodes.  Leave them be.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    // The node is a table element, an hr, a paragraph, a div or a section
    // header; in HTML 4, it can directly carry the ALIGN attribute and we
    // don't need to nest it, just set the alignment.  In CSS, assign the
    // corresponding CSS styles in SetBlockElementAlign().
    if (HTMLEditUtils::IsAlignAttrSupported(content)) {
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
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
      continue;
    }

    // Skip insignificant formatting text nodes to prevent unnecessary
    // structure splitting!
    if (content->IsText() &&
        ((HTMLEditUtils::IsAnyTableElementExceptColumnElement(
              *atContent.ContainerAs<nsIContent>()) &&
          !HTMLEditUtils::IsTableCellOrCaptionElement(
              *atContent.ContainerAs<nsIContent>())) ||
         HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>()) ||
         HTMLEditUtils::IsEmptyNode(
             *content,
             {EmptyCheckOption::TreatSingleBRElementAsVisible,
              EmptyCheckOption::TreatNonEditableContentAsInvisible}))) {
      continue;
    }

    // If it's a list item, or a list inside a list, forget any "current" div,
    // and instead put divs inside the appropriate block (td, li, etc.)
    if (HTMLEditUtils::IsListItemElement(*content) ||
        HTMLEditUtils::IsListElement(*content)) {
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

      if (HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>())) {
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
    if (!createdDivElement || transitionList[i]) {
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

    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < aArrayOfContents.Length(); i++) {
        const OwningNonNull<nsIContent>& nextContent = aArrayOfContents[i + 1];
        if (lastContent->GetNextSibling() != nextContent ||
            !EditorUtils::IsEditableContent(content, EditorType::HTML) ||
            !HTMLEditUtils::IsAlignAttrSupported(nextContent) ||
            // If we meets an invisible `Text` in table or list, we don't move
            // it to avoid to handle ancestors for them.  However, ignoring the
            // empty `Text` nodes is more expensive than moving them here.
            // Therefore, here does not check whether the following sibling of
            // `content` is an empty `Text`.

            // In some cases, we reach here even if `content` is a list or a
            // list item.  However, anyway we need to run a preparation for such
            // element.  Therefore, we cannot move such type of elements with
            // `content` here.
            HTMLEditUtils::IsListItemElement(*nextContent) ||
            HTMLEditUtils::IsListElement(*nextContent) ||
            // Similarly, if the sibling is in the transitionList, we need to
            // handle it separately.
            transitionList[i + 1]) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();

    // Tuck the node into the end of the active div
    //
    // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *createdDivElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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
        return HTMLEditUtils::IsTableCellElement(*aNode.AsElement()) ||
               HTMLEditUtils::IsListItemElement(*aNode.AsElement());
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

  // XXX Chrome wraps text into a <div> only when the container has different
  // blocks.  At that time, Chrome seems treating non-editable nodes as a line
  // break.  So, their behavior is also odd so that it does not make sense to
  // follow their behavior when there is non-editable content.

  // XXX I don't understand why we should NOT align non-editable children
  //     with modifying EDITABLE `<div>` element.
  const nsCOMPtr<nsIContent> firstEditableContent =
      HTMLEditUtils::GetFirstChild(
          aBlockElement, {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (!firstEditableContent) {
    // This block has no editable content, nothing to align.
    return EditorDOMPoint();
  }

  // If there is only one editable content and it's a `<div>` element,
  // just set `align` attribute of it.
  const nsCOMPtr<nsIContent> lastEditableContent = HTMLEditUtils::GetLastChild(
      aBlockElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (firstEditableContent == lastEditableContent &&
      firstEditableContent->IsHTMLElement(nsGkAtoms::div)) {
    // XXX Chrome uses `style="text-align: foo"` instead of the legacy `align`
    // attribute.  That does not allow to align the child blocks center and they
    // put the style to every blocks in the selection range. So, it requires a
    // complicated change to follow their behavior.
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
  Result<CreateElementResult, nsresult> createNewDivElementResultOrError =
      CreateAndInsertElement(
          WithTransaction::Yes, *nsGkAtoms::div,
          EditorDOMPoint(&aBlockElement, 0u),
          // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
          [&](HTMLEditor& aHTMLEditor, Element& aDivElement,
              const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            MOZ_ASSERT(!aDivElement.IsInComposedDoc());
            // If aDivElement has not been connected yet, we do not need
            // transaction of setting align attribute here.
            nsresult rv = aHTMLEditor.SetAttributeOrEquivalent(
                &aDivElement, nsGkAtoms::align, aAlignType, false);
            if (NS_FAILED(rv)) {
              NS_WARNING(
                  "EditorBase::SetAttributeOrEquivalent(nsGkAtoms::align, "
                  "\"...\", false) failed");
              return rv;
            }
            if (!aBlockElement.HasChildren()) {
              return NS_OK;
            }
            // FIXME: This will move non-editable children between
            // firstEditableContent and lastEditableContent.  So, the design of
            // this method is odd.
            Result<MoveNodeResult, nsresult> moveChildrenResultOrError =
                aHTMLEditor.MoveSiblingsWithTransaction(
                    *firstEditableContent, *lastEditableContent,
                    EditorDOMPoint(&aDivElement, 0));
            if (MOZ_UNLIKELY(moveChildrenResultOrError.isErr())) {
              NS_WARNING_ASSERTION(
                  moveChildrenResultOrError.isOk(),
                  "HTMLEditor::MoveSiblingsWithTransaction() failed");
              return moveChildrenResultOrError.unwrapErr();
            }
            moveChildrenResultOrError.unwrap().IgnoreCaretPointSuggestion();
            return NS_OK;
          });
  if (MOZ_UNLIKELY(createNewDivElementResultOrError.isErr())) {
    NS_WARNING(
        "HTMLEditor::CreateAndInsertElement(WithTransaction::Yes, "
        "nsGkAtoms::div) failed");
    return createNewDivElementResultOrError.propagateErr();
  }
  return createNewDivElementResultOrError.unwrap().UnwrapCaretPoint();
}

Result<EditorRawDOMRange, nsresult>
HTMLEditor::GetRangeExtendedToHardLineEdgesForBlockEditAction(
    const dom::Range* aRange, const Element& aEditingHost) const {
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

  {
    // Is there any intervening visible white-space?  If so we can't push
    // selection past that, it would visibly change meaning of users selection.
    const WSScanResult prevVisibleThingOfEndPoint =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {
                // We should refer only the default style of HTML because we
                // need to wrap any elements with a specific HTML element.  So
                // we should not refer actual style.  For example, we want to
                // reformat parent HTML block element even if selected in a
                // blocked phrase element or non-HTMLelement.
                WSRunScanner::Option::ReferHTMLDefaultStyle,
            },
            endPoint, &aEditingHost);
    if (MOZ_UNLIKELY(prevVisibleThingOfEndPoint.Failed())) {
      NS_WARNING(
          "WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary() failed");
      return Err(NS_ERROR_FAILURE);
    }
    if (prevVisibleThingOfEndPoint.ReachedSomethingNonTextContent()) {
      // eThisBlock and eOtherBlock conveniently distinguish cases
      // of going "down" into a block and "up" out of a block.
      if (prevVisibleThingOfEndPoint.ReachedOtherBlockElement()) {
        // endpoint is just after the close of a block.
        if (nsIContent* child = HTMLEditUtils::GetLastLeafContent(
                *prevVisibleThingOfEndPoint.ElementPtr(),
                {LeafNodeOption::TreatChildBlockAsLeafNode},
                BlockInlineCheck::UseHTMLDefaultStyle)) {
          newRange.SetEnd(EditorRawDOMPoint::After(*child));
        }
        // else block is empty - we can leave selection alone here, i think.
      } else if (prevVisibleThingOfEndPoint.ReachedCurrentBlockBoundary() ||
                 prevVisibleThingOfEndPoint
                     .ReachedInlineEditingHostBoundary()) {
        // endpoint is just after start of this block
        if (nsIContent* const child = HTMLEditUtils::GetPreviousLeafContent(
                endPoint, {LeafNodeOption::IgnoreNonEditableNode},
                BlockInlineCheck::UseHTMLDefaultStyle, &aEditingHost)) {
          newRange.SetEnd(EditorRawDOMPoint::After(*child));
        }
        // else block is empty - we can leave selection alone here, i think.
      } else if (prevVisibleThingOfEndPoint.ReachedBRElement()) {
        // endpoint is just after break.  lets adjust it to before it.
        newRange.SetEnd(prevVisibleThingOfEndPoint
                            .PointAtReachedContent<EditorRawDOMPoint>());
      }
    }
  }
  {
    // Is there any intervening visible white-space?  If so we can't push
    // selection past that, it would visibly change meaning of users selection.
    const WSScanResult nextVisibleThingOfStartPoint =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::ReferHTMLDefaultStyle}, startPoint,
            &aEditingHost);
    if (MOZ_UNLIKELY(nextVisibleThingOfStartPoint.Failed())) {
      NS_WARNING(
          "WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary() failed");
      return Err(NS_ERROR_FAILURE);
    }
    if (nextVisibleThingOfStartPoint.ReachedSomethingNonTextContent()) {
      // eThisBlock and eOtherBlock conveniently distinguish cases
      // of going "down" into a block and "up" out of a block.
      if (nextVisibleThingOfStartPoint.ReachedOtherBlockElement()) {
        // startpoint is just before the start of a block.
        if (nsIContent* child = HTMLEditUtils::GetFirstLeafContent(
                *nextVisibleThingOfStartPoint.ElementPtr(),
                {LeafNodeOption::TreatChildBlockAsLeafNode},
                BlockInlineCheck::UseHTMLDefaultStyle)) {
          newRange.SetStart(EditorRawDOMPoint(child));
        }
        // else block is empty - we can leave selection alone here, i think.
      } else if (nextVisibleThingOfStartPoint.ReachedCurrentBlockBoundary() ||
                 nextVisibleThingOfStartPoint
                     .ReachedInlineEditingHostBoundary()) {
        // startpoint is just before end of this block
        if (nsIContent* const child = HTMLEditUtils::GetNextLeafContent(
                startPoint, {LeafNodeOption::IgnoreNonEditableNode},
                BlockInlineCheck::UseHTMLDefaultStyle, &aEditingHost)) {
          newRange.SetStart(EditorRawDOMPoint(child));
        }
        // else block is empty - we can leave selection alone here, i think.
      } else if (nextVisibleThingOfStartPoint.ReachedBRElement()) {
        // startpoint is just before a break.  lets adjust it to after it.
        // XXX If it's an invisible <br>, does this work? Will the following
        // checks solve that?
        newRange.SetStart(nextVisibleThingOfStartPoint
                              .PointAfterReachedContent<EditorRawDOMPoint>());
      }
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

  // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
  // boundaries.
  Maybe<int32_t> comp =
      nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
          startPoint.ToRawRangeBoundary(),
          newRange.EndRef().ToRawRangeBoundary());

  if (NS_WARN_IF(!comp)) {
    return Err(NS_ERROR_FAILURE);
  }

  if (*comp == 1) {
    return EditorRawDOMRange();  // New end before old start.
  }

  comp = nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
      newRange.StartRef().ToRawRangeBoundary(), endPoint.ToRawRangeBoundary());

  if (NS_WARN_IF(!comp)) {
    return Err(NS_ERROR_FAILURE);
  }

  if (*comp == 1) {
    return EditorRawDOMRange();  // New start after old end.
  }

  return newRange;
}

template <typename EditorDOMRangeType>
already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
    const EditorDOMRangeType& aRange) {
  MOZ_DIAGNOSTIC_ASSERT(aRange.IsPositioned());
  return CreateRangeIncludingAdjuscentWhiteSpaces(aRange.StartRef(),
                                                  aRange.EndRef());
}

template <typename EditorDOMPointType1, typename EditorDOMPointType2>
already_AddRefed<dom::Range>
HTMLEditor::CreateRangeIncludingAdjuscentWhiteSpaces(
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

  RefPtr<dom::Range> range =
      dom::Range::Create(startPoint.ToRawRangeBoundary(),
                         endPoint.ToRawRangeBoundary(), IgnoreErrors());
  NS_WARNING_ASSERTION(range, "Range::Create() failed");
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
  for (size_t i = 0; i < aArrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = aArrayOfContents[i];

    const auto IsNewBlockRequired = [](const nsIContent& aContent) {
      return HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
                 aContent) ||
             HTMLEditUtils::IsListItemElement(aContent);
    };

    if (IsNewBlockRequired(content)) {
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

    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < aArrayOfContents.Length(); i++) {
        const OwningNonNull<nsIContent>& nextContent = aArrayOfContents[i + 1];
        if (lastContent->GetNextSibling() == nextContent ||
            !IsNewBlockRequired(nextContent)) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();

    // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to/ keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *curBlock);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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
        HTMLEditUtils::IsListElement(*content)) {
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
  for (size_t i = 0; i < aArrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = aArrayOfContents[i];

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

    const auto IsSameFormatBlockOrNonEditableBlock =
        [&aNewFormatTagName](const nsIContent& aContent) {
          return aContent.IsHTMLElement(&aNewFormatTagName) ||
                 (!EditorUtils::IsEditableContent(aContent, EditorType::HTML) &&
                  HTMLEditUtils::IsBlockElement(
                      aContent, BlockInlineCheck::UseHTMLDefaultStyle));
        };

    const auto IsMozDivOrFormatBlock =
        [&aFormatBlockMode](const nsIContent& aContent) {
          return HTMLEditUtils::IsMozDivElement(aContent) ||
                 HTMLEditor::IsFormatElement(aFormatBlockMode, aContent);
        };

    const auto IsNewFormatBlockRequired = [](const nsIContent& aContent) {
      return aContent.IsHTMLElement(nsGkAtoms::table) ||
             HTMLEditUtils::IsListElement(aContent) ||
             aContent.IsAnyOfHTMLElements(
                 nsGkAtoms::tbody, nsGkAtoms::tr, nsGkAtoms::td, nsGkAtoms::li,
                 nsGkAtoms::blockquote, nsGkAtoms::div);
    };

    const auto IsMovableInlineContent = [&aNewFormatTagName](
                                            const nsIContent& aContent) {
      return HTMLEditUtils::IsInlineContent(
                 aContent, BlockInlineCheck::UseHTMLDefaultStyle) &&
             // If content is a non editable, drop it if we are going to <pre>.
             !(&aNewFormatTagName == nsGkAtoms::pre &&
               !EditorUtils::IsEditableContent(aContent, EditorType::HTML));
    };

    const auto IsMovableInlineContentSibling = [&](const nsIContent& aContent) {
      return !IsSameFormatBlockOrNonEditableBlock(aContent) &&
             !IsMozDivOrFormatBlock(aContent) &&
             !IsNewFormatBlockRequired(aContent) &&
             !aContent.IsHTMLElement(nsGkAtoms::br) &&
             IsMovableInlineContent(aContent);
    };

    // Is it already the right kind of block, or an uneditable block?
    if (IsSameFormatBlockOrNonEditableBlock(content)) {
      // Forget any previous block used for previous inline nodes
      curBlock = nullptr;
      pendingBRElementToMoveCurBlock = nullptr;
      // Do nothing to this block
      continue;
    }

    // If content is a format element, replace it with a new block of correct
    // type.
    // XXX: pre can't hold everything the others can
    if (IsMozDivOrFormatBlock(content)) {
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

    if (IsNewFormatBlockRequired(content)) {
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

    if (!IsMovableInlineContent(content)) {
      // Do nothing to this block
      continue;
    }
    MOZ_ASSERT(IsMovableInlineContentSibling(content));

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

    // This is a continuation of some inline nodes that belong together in
    // the same block item.  Use curBlock.
    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < aArrayOfContents.Length(); i++) {
        const OwningNonNull<nsIContent>& nextContent = aArrayOfContents[i + 1];
        if (lastContent->GetNextSibling() != nextContent ||
            !IsMovableInlineContentSibling(nextContent)) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();
    // MOZ_KnownLive because 'aArrayOfContents' is guaranteed to keep it
    // alive.  We could try to make that a rvalue ref and create a const array
    // on the stack here, but callers are passing in auto arrays, and we don't
    // want to introduce copies..
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *curBlock);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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
    if (nsCOMPtr<nsIContent> maybeBRContent =
            HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                splitPoint,
                {LeafNodeOption::IgnoreNonEditableNode,
                 LeafNodeOption::TreatChildBlockAsLeafNode},
                BlockInlineCheck::UseComputedDisplayOutsideStyle,
                &aEditingHost)) {
      if (maybeBRContent->IsHTMLElement(nsGkAtoms::br) &&
          splitPoint.GetChild()) {
        // Making use of html structure... if next node after where we are
        // putting our div is not a block, then the br we found is in same
        // block we are, so it's safe to consume it.
        if (nsIContent* const nextEditableSibling =
                HTMLEditUtils::GetNextSibling(
                    *splitPoint.GetChild(),
                    {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
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
  if (HTMLEditUtils::IsListElement(aNodeLeft) || aNodeLeft.IsText()) {
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
  const nsCOMPtr<nsIContent> lastEditableChildOfLeftContent =
      HTMLEditUtils::GetLastChild(
          aNodeLeft, {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (MOZ_UNLIKELY(NS_WARN_IF(!lastEditableChildOfLeftContent))) {
    return NS_ERROR_FAILURE;
  }

  const nsCOMPtr<nsIContent> firstEditableChildOfRightContent =
      HTMLEditUtils::GetFirstChild(
          aNodeRight, {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
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
        HTMLEditUtils::IsMailCiteElement(*element)) {
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
                                 nsGkAtoms::background_color,
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

  const AutoPendingStyleCacheArray cachedPendingStyles(
      TopLevelEditSubActionDataRef().mCachedPendingStyles->Clone());
  for (const PendingStyleCache& styleCacheBeforeEdit :
       Reversed(cachedPendingStyles)) {
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
        Element& element = *aNode.AsElement();
        if (!EditorUtils::IsEditableContent(element, EditorType::HTML) ||
            (!HTMLEditUtils::IsListItemElement(element) &&
             !HTMLEditUtils::IsTableCellOrCaptionElement(element))) {
          return false;
        }
        return HTMLEditUtils::IsEmptyNode(
            element, {EmptyCheckOption::TreatSingleBRElementAsVisible,
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
  const dom::Range* firstRange = SelectionRef().GetRangeAt(0);
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
            HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
                atCaret,
                {LeafNodeOption::IgnoreNonEditableNode,
                 LeafNodeOption::TreatChildBlockAsLeafNode},
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
  if (nsIContent* const previousEditableContentInBlockAtCaret =
          HTMLEditUtils::GetPreviousSibling(
              *atCaret.GetChild(), {LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
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
  if (nsIContent* const nextEditableContentInBlockAtCaret =
          HTMLEditUtils::GetNextSibling(
              *atCaret.GetChild(), {LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
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
          HTMLEditUtils::GetPreviousLeafContent(
              point, {LeafNodeOption::IgnoreNonEditableNode},
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
      if (HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
              *previousEditableContent) &&
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
                   HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
                       *previousEditableContent,
                       {LeafNodeOption::IgnoreNonEditableNode,
                        LeafNodeOption::TreatChildBlockAsLeafNode},
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
  if (nsIContent* const previousEditableContentInBlock =
          HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
              point,
              {LeafNodeOption::IgnoreNonEditableNode,
               LeafNodeOption::TreatChildBlockAsLeafNode},
              BlockInlineCheck::UseComputedDisplayStyle, editingHost)) {
    if (previousEditableContentInBlock->IsHTMLElement(nsGkAtoms::br) ||
        previousEditableContentInBlock->IsText() ||
        HTMLEditUtils::IsImageElement(*previousEditableContentInBlock) ||
        previousEditableContentInBlock->IsHTMLElement(nsGkAtoms::hr)) {
      return NS_OK;
    }
  }

  // If next editable content in same block is `<br>`, text node, `<img>` or
  // `<hr>`, current caret position is fine.
  if (nsIContent* nextEditableContentInBlock =
          HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
              point,
              {LeafNodeOption::IgnoreNonEditableNode,
               LeafNodeOption::TreatChildBlockAsLeafNode},
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
        Element& element = *content->AsElement();
        const bool isMailCite =
            isMailEditor && HTMLEditUtils::IsMailCiteElement(element);
        const bool isCandidate = [&]() {
          if (element.IsHTMLElement(nsGkAtoms::body)) {
            // Don't delete the body
            return false;
          }
          if (isMailCite || element.IsHTMLElement(nsGkAtoms::a) ||
              HTMLEditUtils::IsInlineStyleElement(element) ||
              HTMLEditUtils::IsListElement(element) ||
              element.IsHTMLElement(nsGkAtoms::div)) {
            // Only consider certain nodes to be empty for purposes of removal
            return true;
          }
          if (HTMLEditUtils::IsFormatElementForFormatBlockCommand(element) ||
              HTMLEditUtils::IsListItemElement(element) ||
              element.IsHTMLElement(nsGkAtoms::blockquote)) {
            // These node types are candidates if selection is not in them.  If
            // it is one of these, don't delete if selection inside.  This is so
            // we can create empty headings, etc., for the user to type into.
            if (maybeSelectionRanges.isNothing()) {
              maybeSelectionRanges.emplace(SelectionRef());
            }
            return !maybeSelectionRanges
                        ->IsAtLeastOneContainerOfRangeBoundariesInclusiveDescendantOf(
                            element);
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

  if (!HTMLEditUtils::IsListItemElement(aListItemElement)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(!aListItemElement.GetParentElement()) ||
      NS_WARN_IF(!aListItemElement.GetParentElement()->GetParentNode())) {
    return NS_ERROR_FAILURE;
  }

  // if it's first or last list item, don't need to split the list
  // otherwise we do.
  const bool isFirstListItem = HTMLEditUtils::IsFirstChild(
      aListItemElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  const bool isLastListItem = HTMLEditUtils::IsLastChild(
      aListItemElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);

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
  if (NS_WARN_IF(!pointToInsertListItem.IsInContentNode())) {
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
  if (!HTMLEditUtils::IsListElement(
          *pointToInsertListItem.ContainerAs<nsIContent>()) &&
      HTMLEditUtils::IsListItemElement(aListItemElement)) {
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
  MOZ_ASSERT(HTMLEditUtils::IsListElement(aListElement));

  // XXX If mutation event listener inserts new child into `aListElement`,
  //     this becomes infinite loop so that we should set limit of the
  //     loop count from original child count.
  while (aListElement.GetFirstChild()) {
    const OwningNonNull<nsIContent> child = *aListElement.GetFirstChild();

    if (HTMLEditUtils::IsListItemElement(*child)) {
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

    if (HTMLEditUtils::IsListElement(*child)) {
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

  EditorDOMPoint editorDOMPoint{paddingBRElement};
  EditorLineBreak editorLineBreak{std::move(paddingBRElement)};
  return CreateLineBreakResult{std::move(editorLineBreak),
                               std::move(editorDOMPoint)};
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
              {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
      if (nextVisibleThing.ReachedBlockBoundary() &&
          HTMLEditUtils::IsMailCiteElement(*nextVisibleThing.ElementPtr()) &&
          HTMLEditUtils::IsInlineContent(
              *nextVisibleThing.ElementPtr(),
              BlockInlineCheck::UseHTMLDefaultStyle)) {
        return nextVisibleThing.ReachedCurrentBlockBoundary()
                   ? EditorDOMPoint::AtEndOf(*nextVisibleThing.ElementPtr())
                   : EditorDOMPoint(nextVisibleThing.ElementPtr());
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
    if (HTMLEditUtils::IsAlignAttrSupported(blockOrHRElement)) {
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

  nsIContent* const firstEditableChild = HTMLEditUtils::GetFirstChild(
      aRemovingContainerElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (!firstEditableChild) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *firstEditableChild, BlockInlineCheck::UseComputedDisplayStyle) ||
      firstEditableChild->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  nsIContent* const previousEditableContent = HTMLEditUtils::GetPreviousSibling(
      aRemovingContainerElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
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

  nsIContent* const firstEditableContent = HTMLEditUtils::GetLastChild(
      aRemovingContainerElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (!firstEditableContent) {
    return CreateElementResult::NotHandled();
  }

  if (HTMLEditUtils::IsBlockElement(
          *firstEditableContent, BlockInlineCheck::UseComputedDisplayStyle) ||
      firstEditableContent->IsHTMLElement(nsGkAtoms::br)) {
    return CreateElementResult::NotHandled();
  }

  nsIContent* const nextEditableContent = HTMLEditUtils::GetPreviousSibling(
      aRemovingContainerElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
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
             HTMLEditUtils::IsAlignAttrSupported(aBlockOrHRElement));

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

  const RefPtr<Element> focusElement = GetSelectionContainerElement();
  if (focusElement && HTMLEditUtils::IsImageElement(*focusElement)) {
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
  for (size_t i = 0; i < arrayOfContents.Length(); i++) {
    const OwningNonNull<nsIContent>& content = arrayOfContents[i];

    // Here's where we actually figure out what to do.
    EditorDOMPoint atContent(content);
    if (NS_WARN_IF(!atContent.IsInContentNode())) {
      return NS_ERROR_FAILURE;  // XXX not continue??
    }

    // Ignore all non-editable nodes.  Leave them be.
    if (!EditorUtils::IsEditableContent(content, EditorType::HTML)) {
      continue;
    }

    // If current node is a child of a list element, we need another list
    // element in absolute-positioned `<div>` element to avoid non-selected
    // list items are moved into the `<div>` element.
    if (HTMLEditUtils::IsListElement(*atContent.ContainerAs<nsIContent>())) {
      // If we cannot move current node to created list element, we need a
      // list element in the target `<div>` element for the destination.
      // Therefore, duplicate same list element into the target `<div>`
      // element.
      nsIContent* const previousEditableContent =
          createdListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    content, {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle)
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
            HTMLEditUtils::GetClosestInclusiveAncestorListItemElement(
                content, &aEditingHost)) {
      if (handledListItemElement == listItemElement) {
        // Current node has already been moved into the `<div>` element.
        continue;
      }
      // If we cannot move the list item element into created list element,
      // we need another list element in the target `<div>` element.
      nsIContent* const previousEditableContent =
          createdListElement
              ? HTMLEditUtils::GetPreviousSibling(
                    *listItemElement, {LeafNodeOption::IgnoreNonEditableNode},
                    BlockInlineCheck::UseComputedDisplayOutsideStyle)
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

    const OwningNonNull<nsIContent> lastContent = [&]() {
      nsIContent* lastContent = content;
      for (; i + 1 < arrayOfContents.Length(); i++) {
        const OwningNonNull<nsIContent>& nextContent = arrayOfContents[i + 1];
        if (lastContent->GetNextSibling() == nextContent ||
            HTMLEditUtils::IsListElement(*nextContent) ||
            HTMLEditUtils::IsListItemElement(*nextContent) ||
            !EditorUtils::IsEditableContent(content, EditorType::HTML)) {
          break;
        }
        lastContent = nextContent;
      }
      return OwningNonNull<nsIContent>(*lastContent);
    }();

    // MOZ_KnownLive because 'arrayOfContents' is guaranteed to keep it alive.
    Result<MoveNodeResult, nsresult> moveNodeResult =
        MoveSiblingsToEndWithTransaction(MOZ_KnownLive(content), lastContent,
                                         *targetDivElement);
    if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveSiblingsToEndWithTransaction() failed");
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

}  // namespace mozilla
