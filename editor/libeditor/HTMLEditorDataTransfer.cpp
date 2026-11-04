/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditor.h"

#include <string.h>

#include "AutoSelectionRestorer.h"
#include "EditAction.h"
#include "EditorBase.h"
#include "EditorDOMPoint.h"
#include "EditorUtils.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditUtils.h"
#include "InternetCiter.h"
#include "PendingStyles.h"
#include "SelectionState.h"
#include "WhiteSpaceVisibilityKeeper.h"
#include "WSRunScanner.h"

#include "ErrorList.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/FileBlobImpl.h"
#include "mozilla/dom/FileReader.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/Attributes.h"
#include "mozilla/Base64.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/Preferences.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/TextComposition.h"
#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsCRTGlue.h"  // for CRLF
#include "nsComponentManagerUtils.h"
#include "nsIScriptError.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsDependentSubstring.h"
#include "nsError.h"
#include "nsFocusManager.h"
#include "nsGkAtoms.h"
#include "nsIClipboard.h"
#include "nsIContent.h"
#include "nsIDocumentEncoder.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIMIMEService.h"
#include "nsINode.h"
#include "nsIParserUtils.h"
#include "nsIPrincipal.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPrimitives.h"
#include "nsISupportsUtils.h"
#include "nsITransferable.h"
#include "nsIVariant.h"
#include "nsLinebreakConverter.h"
#include "nsLiteralString.h"
#include "nsNameSpaceManager.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStringIterator.h"
#include "nsTextNode.h"
#include "nsTreeSanitizer.h"
#include "nsXPCOM.h"
#include "nscore.h"
#include "nsContentUtils.h"
#include "nsQueryObject.h"

class nsAtom;
class nsILoadContext;

namespace mozilla {

using namespace dom;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;

#define kInsertCookie "_moz_Insert Here_moz_"

// some little helpers
static bool FindIntegerAfterString(const char* aLeadingString,
                                   const nsCString& aCStr,
                                   int32_t& foundNumber);
static void RemoveFragComments(nsCString& aStr);

nsresult HTMLEditor::InsertDroppedDataTransferAsAction(
    AutoEditActionDataSetter& aEditActionData, DataTransfer& aDataTransfer,
    const EditorDOMPoint& aDroppedAt, nsIPrincipal* aSourcePrincipal) {
  MOZ_ASSERT(aEditActionData.GetEditAction() == EditAction::eDrop);
  MOZ_ASSERT(GetEditAction() == EditAction::eDrop);
  MOZ_ASSERT(aDroppedAt.IsSet());
  MOZ_ASSERT(aDataTransfer.MozItemCount() > 0);

  if (IsReadonly()) {
    return NS_OK;
  }

  aEditActionData.InitializeDataTransfer(&aDataTransfer);
  RefPtr<StaticRange> targetRange = StaticRange::Create(
      aDroppedAt.GetContainer(), aDroppedAt.Offset(), aDroppedAt.GetContainer(),
      aDroppedAt.Offset(), IgnoreErrors());
  NS_WARNING_ASSERTION(targetRange && targetRange->IsPositioned(),
                       "Why did we fail to create collapsed static range at "
                       "dropped position?");
  if (targetRange && targetRange->IsPositioned()) {
    aEditActionData.AppendTargetRange(*targetRange);
  }
  nsresult rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return rv;
  }

  if (MOZ_UNLIKELY(!aDroppedAt.IsInContentNode())) {
    NS_WARNING("Dropped into non-content node");
    return NS_OK;
  }

  const RefPtr<Element> editingHost = ComputeEditingHost(
      *aDroppedAt.ContainerAs<nsIContent>(), LimitInBodyElement::No);
  if (MOZ_UNLIKELY(!editingHost)) {
    NS_WARNING("Dropped onto non-editable node");
    return NS_OK;
  }

  uint32_t numItems = aDataTransfer.MozItemCount();
  for (uint32_t i = 0; i < numItems; ++i) {
    DebugOnly<nsresult> rvIgnored =
        InsertFromDataTransfer(&aDataTransfer, i, aSourcePrincipal, aDroppedAt,
                               DeleteSelectedContent::No, *editingHost);
    if (NS_WARN_IF(Destroyed())) {
      return NS_OK;
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "HTMLEditor::InsertFromDataTransfer("
                         "DeleteSelectedContent::No) failed, but ignored");
  }
  return NS_OK;
}

nsresult HTMLEditor::LoadHTML(const nsAString& aInputString) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // force IME commit; set up rules sniffing and batching
  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertHTMLSource, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::EnsureNoPaddingBRElementForEmptyEditor() failed");
    return rv;
  }

  // Delete Selection, but only if it isn't collapsed, see bug #106269
  if (!SelectionRef().IsCollapsed()) {
    nsresult rv = DeleteSelectionAsSubAction(eNone, eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eStrip) failed");
      return rv;
    }
  }

  // Get the first range in the selection, for context:
  RefPtr<const dom::Range> range = SelectionRef().GetRangeAt(0);
  if (NS_WARN_IF(!range)) {
    return NS_ERROR_FAILURE;
  }

  // Create fragment for pasted HTML.
  ErrorResult error;
  RefPtr<DocumentFragment> documentFragment =
      range->CreateContextualFragment(aInputString, error);
  if (error.Failed()) {
    NS_WARNING("Range::CreateContextualFragment() failed");
    return error.StealNSResult();
  }

  // Put the fragment into the document at start of selection.
  EditorDOMPoint pointToInsert(range->StartRef());
  // XXX We need to make pointToInsert store offset for keeping traditional
  //     behavior since using only child node to pointing insertion point
  //     changes the behavior when inserted child is moved by mutation
  //     observer.  We need to investigate what we should do here.
  (void)pointToInsert.Offset();
  EditorDOMPoint pointToPutCaret;
  for (nsCOMPtr<nsIContent> contentToInsert = documentFragment->GetFirstChild();
       contentToInsert; contentToInsert = documentFragment->GetFirstChild()) {
    Result<CreateContentResult, nsresult> insertChildContentNodeResult =
        InsertNodeWithTransaction(*contentToInsert, pointToInsert);
    if (MOZ_UNLIKELY(insertChildContentNodeResult.isErr())) {
      NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
      return insertChildContentNodeResult.unwrapErr();
    }
    CreateContentResult unwrappedInsertChildContentNodeResult =
        insertChildContentNodeResult.unwrap();
    unwrappedInsertChildContentNodeResult.MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    // XXX If the inserted node has been moved by mutation observer,
    //     incrementing offset will cause odd result.  Next new node
    //     will be inserted after existing node and the offset will be
    //     overflown from the container node.
    pointToInsert.Set(pointToInsert.GetContainer(), pointToInsert.Offset() + 1);
    if (NS_WARN_IF(!pointToInsert.Offset())) {
      // Append the remaining children to the container if offset is
      // overflown.
      pointToInsert.SetToEndOf(pointToInsert.GetContainer());
    }
  }

  if (pointToPutCaret.IsSet()) {
    nsresult rv = CollapseSelectionTo(pointToPutCaret);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING("EditorBase::CollapseSelectionTo() failed, but ignored");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
  }

  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::InsertHTML(const nsAString& aInString) {
  nsresult rv = InsertHTMLAsAction(aInString);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertHTMLAsAction() failed");
  return rv;
}

nsresult HTMLEditor::InsertHTMLAsAction(const nsAString& aInString,
                                        nsIPrincipal* aPrincipal) {
  // FIXME: This should keep handling inserting HTML if the caller is
  // nsIHTMLEditor::InsertHTML.
  if (IsReadonly()) {
    return NS_OK;
  }
  if (ComputeEditContext()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertHTML,
                                          aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  if (!IsPlaintextMailComposer() && !IsStyleEditable(editingHost)) {
    nsAutoString plaintextString;
    nsresult rv = nsContentUtils::ConvertToPlainText(
        aInString, plaintextString, nsIDocumentEncoder::OutputLFLineBreak,
        0u /* never wrap lines*/);
    if (NS_FAILED(rv)) {
      NS_WARNING("nsContentUtils::ConvertToPlainText() failed");
      return EditorBase::ToGenericNSResult(rv);
    }
    Maybe<AutoPlaceholderBatch> treatAsOneTransaction;
    const auto EnsureAutoPlaceholderBatch = [&]() {
      if (treatAsOneTransaction.isNothing()) {
        treatAsOneTransaction.emplace(*this, ScrollSelectionIntoView::Yes,
                                      __FUNCTION__);
      }
    };
    if (mComposition &&
        mComposition->CanRequsetIMEToCommitOrCancelComposition()) {
      EnsureAutoPlaceholderBatch();
      CommitComposition();
      if (NS_WARN_IF(Destroyed())) {
        return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
      }
      if (NS_WARN_IF(editingHost !=
                     ComputeEditingHost(LimitInBodyElement::No))) {
        return EditorBase::ToGenericNSResult(
            NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
    }
    if (MOZ_LIKELY(!plaintextString.IsEmpty())) {
      EnsureAutoPlaceholderBatch();
      rv = InsertTextAsSubAction(plaintextString, InsertTextFor::NormalText);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::InsertTextAsSubAction() failed");
    } else if (!SelectionRef().IsCollapsed()) {
      EnsureAutoPlaceholderBatch();
      rv = DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eNoStrip);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::DeleteSelectionAsSubAction() failed");
    }
    return EditorBase::ToGenericNSResult(rv);
  }
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertHTMLWithContextAsSubAction(
      aInString, u""_ns, u""_ns, u""_ns, SafeToInsertData::Yes,
      EditorDOMPoint(), DeleteSelectedContent::Yes,
      InlineStylesAtInsertionPoint::Clear, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertHTMLWithContextAsSubAction("
                       "SafeToInsertData::Yes, DeleteSelectedContent::Yes, "
                       "InlineStylesAtInsertionPoint::Clear) failed");
  return EditorBase::ToGenericNSResult(rv);
}

class MOZ_STACK_CLASS HTMLEditor::HTMLWithContextInserter final {
 public:
  MOZ_CAN_RUN_SCRIPT HTMLWithContextInserter(HTMLEditor& aHTMLEditor,
                                             const Element& aEditingHost)
      : mHTMLEditor(aHTMLEditor), mEditingHost(aEditingHost) {}

  HTMLWithContextInserter() = delete;
  HTMLWithContextInserter(const HTMLWithContextInserter&) = delete;
  HTMLWithContextInserter(HTMLWithContextInserter&&) = delete;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditActionResult, nsresult> Run(
      const nsAString& aInputString, const nsAString& aContextStr,
      const nsAString& aInfoStr, SafeToInsertData aSafeToInsertData,
      InlineStylesAtInsertionPoint aInlineStylesAtInsertionPoint);

 private:
  class FragmentFromPasteCreator;
  class FragmentParser;
  /**
   * CollectTopMostChildContentsCompletelyInRange() collects topmost child
   * contents which are completely in the given range.
   * For example, if the range points a node with its container node, the
   * result is only the node (meaning does not include its descendants).
   * If the range starts start of a node and ends end of it, and if the node
   * does not have children, returns no nodes, otherwise, if the node has
   * some children, the result includes its all children (not including their
   * descendants).
   *
   * @param aStartPoint         Start point of the range.
   * @param aEndPoint           End point of the range.
   * @param aOutArrayOfContents [Out] Topmost children which are completely in
   *                            the range.
   */
  static void CollectTopMostChildContentsCompletelyInRange(
      const EditorRawDOMPoint& aStartPoint, const EditorRawDOMPoint& aEndPoint,
      nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents);

  EditorDOMPoint GetNewCaretPointAfterInsertingHTML(
      const EditorDOMPoint& aLastInsertedPoint) const;

  /**
   * Insert nodes in aArrayOfTopMostChildContents or their children to
   * aPointToInsert (if the container is not a proper parent of inserting node,
   * this splits the ancestors).
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<CreateContentResult, nsresult>
  InsertContents(
      const EditorDOMPoint& aPointToInsert,
      nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents,
      const nsINode* aFragmentAsNode);

  /**
   * @param aContextStr as indicated by nsITransferable's kHTMLContext.
   * @param aInfoStr as indicated by nsITransferable's kHTMLInfo.
   */
  nsresult CreateDOMFragmentFromPaste(
      const nsAString& aInputString, const nsAString& aContextStr,
      const nsAString& aInfoStr, nsCOMPtr<nsINode>* aOutFragNode,
      nsCOMPtr<nsINode>* aOutStartNode, nsCOMPtr<nsINode>* aOutEndNode,
      uint32_t* aOutStartOffset, uint32_t* aOutEndOffset,
      SafeToInsertData aSafeToInsertData) const;

  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult MoveCaretOutsideOfLink(
      Element& aLinkElement, const EditorDOMPoint& aPointToPutCaret);

  // MOZ_KNOWN_LIVE because this is set only by the constructor which is
  // marked as MOZ_CAN_RUN_SCRIPT and this is allocated only in the stack.
  MOZ_KNOWN_LIVE HTMLEditor& mHTMLEditor;
  MOZ_KNOWN_LIVE const Element& mEditingHost;
};

class MOZ_STACK_CLASS
HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator final {
 public:
  nsresult Run(const Document& aDocument, const nsAString& aInputString,
               const nsAString& aContextStr, const nsAString& aInfoStr,
               nsCOMPtr<nsINode>* aOutFragNode,
               nsCOMPtr<nsINode>* aOutStartNode, nsCOMPtr<nsINode>* aOutEndNode,
               SafeToInsertData aSafeToInsertData) const;

 private:
  nsresult CreateDocumentFragmentAndGetParentOfPastedHTMLInContext(
      const Document& aDocument, const nsAString& aInputString,
      const nsAString& aContextStr, SafeToInsertData aSafeToInsertData,
      nsCOMPtr<nsINode>& aParentNodeOfPastedHTMLInContext,
      RefPtr<DocumentFragment>& aDocumentFragmentToInsert) const;

  static nsAtom* DetermineContextLocalNameForParsingPastedHTML(
      const nsIContent* aParentContentOfPastedHTMLInContext);

  static bool FindTargetNodeOfContextForPastedHTMLAndRemoveInsertionCookie(
      nsINode& aStart, nsCOMPtr<nsINode>& aResult);

  static bool IsInsertionCookie(const nsIContent& aContent);

  /**
   * @param aDocumentFragmentForContext contains the merged result.
   */
  static nsresult MergeAndPostProcessFragmentsForPastedHTMLAndContext(
      DocumentFragment& aDocumentFragmentForPastedHTML,
      DocumentFragment& aDocumentFragmentForContext,
      nsIContent& aTargetContentOfContextForPastedHTML);

  /**
   * @param aInfoStr as indicated by nsITransferable's kHTMLInfo.
   */
  [[nodiscard]] static nsresult MoveStartAndEndAccordingToHTMLInfo(
      const nsAString& aInfoStr, nsCOMPtr<nsINode>* aOutStartNode,
      nsCOMPtr<nsINode>* aOutEndNode);

  static nsresult PostProcessFragmentForPastedHTMLWithoutContext(
      DocumentFragment& aDocumentFragmentForPastedHTML);

  static nsresult PreProcessContextDocumentFragmentForMerging(
      DocumentFragment& aDocumentFragmentForContext);

  static void RemoveHeadChildAndStealBodyChildsChildren(nsINode& aNode);

  /**
   * This is designed for a helper class to remove disturbing nodes at inserting
   * the HTML fragment into the DOM tree.  This walks the children and if some
   * elements do not have enough children, e.g., list elements not having
   * another visible list elements nor list item elements,
   * will be removed.
   *
   * @param aNode       Should not be a node whose mutation may be observed by
   *                    JS.
   */
  static void RemoveIncompleteDescendantsFromInsertingFragment(nsINode& aNode);

  enum class NodesToRemove {
    eAll,
    eOnlyListItems /*!< List items are always block-level elements, hence such
                     whitespace-only nodes are always invisible. */
  };
  static nsresult
  RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
      nsIContent& aNode, NodesToRemove aNodesToRemove);
};

EditorDOMPoint
HTMLEditor::HTMLWithContextInserter::GetNewCaretPointAfterInsertingHTML(
    const EditorDOMPoint& aLastInsertedPoint) const {
  MOZ_ASSERT(aLastInsertedPoint.IsInContentNode());
  MOZ_ASSERT(
      HTMLEditUtils::IsSimplyEditableNode(*aLastInsertedPoint.GetContainer()));
  MOZ_ASSERT(aLastInsertedPoint.GetChild());
  // The last inserted node may be non-editable. Then, we want to put caret
  // after it.
  nsIContent* const lastInsertedContent = aLastInsertedPoint.GetChild();
  const EditorRawDOMPoint firstEditablePoint = [&]() MOZ_NEVER_INLINE_DEBUG {
    if (HTMLEditUtils::IsSimplyEditableNode(*lastInsertedContent)) {
      return aLastInsertedPoint.To<EditorRawDOMPoint>();
    }
    MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(
        *aLastInsertedPoint.GetContainer()));
    return aLastInsertedPoint.NextPoint<EditorRawDOMPoint>();
  }();
  if (NS_WARN_IF(!firstEditablePoint.IsSet())) {
    return EditorDOMPoint();  // No editable node, why?
  }

  const EditorRawDOMPoint adjustedEditablePoint = [&]() MOZ_NEVER_INLINE_DEBUG {
    if (firstEditablePoint != aLastInsertedPoint) {
      return firstEditablePoint;
    }
    if (lastInsertedContent->IsText()) {
      // End of the last inserted Text.
      return EditorRawDOMPoint::AtEndOf(*lastInsertedContent);
    }
    if (lastInsertedContent->IsHTMLElement(nsGkAtoms::table)) {
      // After the <table>.
      return aLastInsertedPoint.NextPoint<EditorRawDOMPoint>();
    }
    if (!HTMLEditUtils::IsContainerNode(*lastInsertedContent) ||
        HTMLEditUtils::IsReplacedElement(*lastInsertedContent)) {
      // After the atomic content node like <br> or <img>.
      return aLastInsertedPoint.NextPoint<EditorRawDOMPoint>();
    }
    // We want to put caret to the last leaf content in the inserted content.
    nsIContent* const lastLeaf = HTMLEditUtils::GetLastLeafContent(
        *lastInsertedContent,
        {
            LeafNodeOption::TreatNonEditableNodeAsLeafNode,
            LeafNodeOption::IgnoreInvisibleText,
            // FIXME: We cannot visually put caret into empty inline containers
            // like <span></span> so that let's ignore them.
        });
    if (!lastLeaf) {
      // No meaning content in lastInsertedContent, let's put caret to end of
      // it.
      return EditorRawDOMPoint::AtEndOf(*lastInsertedContent);
    }
    Element* const mostDistantInclusiveAncestorTableElement =
        [&]() -> Element* {
      Element* tableElement = nullptr;
      for (Element* maybeTableElement = lastLeaf->GetAsElementOrParentElement();
           maybeTableElement &&
           maybeTableElement != aLastInsertedPoint.GetChild();
           maybeTableElement = maybeTableElement->GetParentElement()) {
        if (maybeTableElement->IsHTMLElement(nsGkAtoms::table)) {
          tableElement = maybeTableElement;
        }
      }
      return tableElement;
    }();
    // If we're in table elements, we should put caret after the most ancestor
    // table element in the container of aLastInsertedPoint.
    if (mostDistantInclusiveAncestorTableElement) {
      MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(
          *mostDistantInclusiveAncestorTableElement));
      MOZ_ASSERT(mostDistantInclusiveAncestorTableElement->GetParentNode());
      MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(
          *mostDistantInclusiveAncestorTableElement->GetParentNode()));
      return EditorRawDOMPoint::After(
          *mostDistantInclusiveAncestorTableElement);
    }
    MOZ_ASSERT(!lastLeaf->IsHTMLElement(nsGkAtoms::table));
    if (lastLeaf->IsText()) {
      // End of the last Text in the last inserted content.
      return EditorRawDOMPoint::AtEndOf(*lastLeaf);
    }
    return !HTMLEditUtils::IsContainerNode(*lastLeaf) ||
                   !HTMLEditUtils::IsSimplyEditableNode(*lastLeaf) ||
                   HTMLEditUtils::IsReplacedElement(*lastLeaf)
               ? EditorRawDOMPoint::After(*lastLeaf)
               : EditorRawDOMPoint::AtEndOf(*lastLeaf);
  }();

  // Make sure we don't end up with selection collapsed after an invisible
  // `<br>` element.
  EditorDOMPoint pointToPutCaret = adjustedEditablePoint.To<EditorDOMPoint>();
  // FIXME: Handle preformatted linefeed too
  const WSScanResult prevVisibleThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // We want to put caret to an editable point so that we need to scan
          // only editable nodes.
          {WSRunScanner::Option::OnlyEditableNodes}, pointToPutCaret);
  if (prevVisibleThing.ReachedBRElementFollowedByBlockBoundary()) {
    const WSScanResult prevVisibleThingOfBRElement =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorRawDOMPoint(prevVisibleThing.BRElementPtr()));
    if (prevVisibleThingOfBRElement.InVisibleOrCollapsibleCharacters()) {
      // XXX Why not end of the text node?
      pointToPutCaret = prevVisibleThingOfBRElement
                            .PointAfterReachedContent<EditorDOMPoint>();
    } else if (prevVisibleThingOfBRElement.ReachedSpecialContent() ||
               prevVisibleThingOfBRElement
                   .ReachedEmptyInlineContainerElement()) {
      pointToPutCaret = prevVisibleThingOfBRElement
                            .PointAfterReachedContentNode<EditorDOMPoint>();
    }
  }

  return pointToPutCaret;
}

nsresult HTMLEditor::InsertHTMLWithContextAsSubAction(
    const nsAString& aInputString, const nsAString& aContextStr,
    const nsAString& aInfoStr, const nsAString& aFlavor,
    SafeToInsertData aSafeToInsertData, const EditorDOMPoint& aPointToInsert,
    DeleteSelectedContent aDeleteSelectedContent,
    InlineStylesAtInsertionPoint aInlineStylesAtInsertionPoint,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  CommitComposition();

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::ePasteHTMLContent, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");
  ignoredError.SuppressException();

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

  // If we have a destination / target node, we want to insert there rather than
  // in place of the selection.  Ignore aDeleteSelectedContent here if
  // aPointToInsert is not set since deletion will also occur later in
  // HTMLWithContextInserter and will be collapsed around there; this block
  // is intended to cover the various scenarios where we are dropping in an
  // editor (and may want to delete the selection before collapsing the
  // selection in the new destination)
  if (aPointToInsert.IsSet()) {
    nsresult rv =
        PrepareToInsertContent(aPointToInsert, aDeleteSelectedContent);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::PrepareToInsertContent() failed");
      return rv;
    }
    aDeleteSelectedContent = DeleteSelectedContent::No;
  }

  HTMLWithContextInserter htmlWithContextInserter(*this, aEditingHost);

  Result<EditActionResult, nsresult> result = htmlWithContextInserter.Run(
      aInputString, aContextStr, aInfoStr, aSafeToInsertData,
      aInlineStylesAtInsertionPoint);
  if (MOZ_UNLIKELY(result.isErr())) {
    return result.unwrapErr();
  }

  // If nothing is inserted and delete selection is required, we need to
  // delete selection right now.
  if (result.inspect().Ignored() &&
      aDeleteSelectedContent == DeleteSelectedContent::Yes) {
    nsresult rv = DeleteSelectionAsSubAction(eNone, eStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eStrip) failed");
      return rv;
    }
  }
  return NS_OK;
}

Result<EditActionResult, nsresult> HTMLEditor::HTMLWithContextInserter::Run(
    const nsAString& aInputString, const nsAString& aContextStr,
    const nsAString& aInfoStr, SafeToInsertData aSafeToInsertData,
    InlineStylesAtInsertionPoint aInlineStylesAtInsertionPoint) {
  MOZ_ASSERT(mHTMLEditor.IsEditActionDataAvailable());

  // create a dom document fragment that represents the structure to paste
  nsCOMPtr<nsINode> fragmentAsNode, streamStartParent, streamEndParent;
  uint32_t streamStartOffset = 0, streamEndOffset = 0;

  nsresult rv = CreateDOMFragmentFromPaste(
      aInputString, aContextStr, aInfoStr, address_of(fragmentAsNode),
      address_of(streamStartParent), address_of(streamEndParent),
      &streamStartOffset, &streamEndOffset, aSafeToInsertData);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::CreateDOMFragmentFromPaste() "
        "failed");
    return Err(rv);
  }

  // we need to recalculate various things based on potentially new offsets
  // this is work to be completed at a later date (probably by jfrancis)

  AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfTopMostChildContents;
  // If we have stream start point information, lets use it and end point.
  // Otherwise, we should make a range all over the document fragment.
  EditorRawDOMPoint streamStartPoint =
      streamStartParent
          ? EditorRawDOMPoint(streamStartParent,
                              AssertedCast<uint32_t>(streamStartOffset))
          : EditorRawDOMPoint(fragmentAsNode, 0);
  EditorRawDOMPoint streamEndPoint =
      streamStartParent ? EditorRawDOMPoint(streamEndParent, streamEndOffset)
                        : EditorRawDOMPoint::AtEndOf(fragmentAsNode);

  (void)streamStartPoint;
  (void)streamEndPoint;

  HTMLWithContextInserter::CollectTopMostChildContentsCompletelyInRange(
      EditorRawDOMPoint(streamStartParent,
                        AssertedCast<uint32_t>(streamStartOffset)),
      EditorRawDOMPoint(streamEndParent,
                        AssertedCast<uint32_t>(streamEndOffset)),
      arrayOfTopMostChildContents);

  if (arrayOfTopMostChildContents.IsEmpty()) {
    return EditActionResult::IgnoredResult();  // Nothing to insert.
  }

  // Are there any table elements in the list?
  // check for table cell selection mode
  bool cellSelectionMode =
      HTMLEditUtils::IsInTableCellSelectionMode(mHTMLEditor.SelectionRef());

  if (cellSelectionMode) {
    // do we have table content to paste?  If so, we want to delete
    // the selected table cells and replace with new table elements;
    // but if not we want to delete _contents_ of cells and replace
    // with non-table elements.  Use cellSelectionMode bool to
    // indicate results.
    if (!HTMLEditUtils::IsAnyTableElementExceptColumnElement(
            *arrayOfTopMostChildContents[0])) {
      cellSelectionMode = false;
    }
  }

  if (!cellSelectionMode) {
    rv = mHTMLEditor.DeleteSelectionAndPrepareToCreateNode();
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::DeleteSelectionAndPrepareToCreateNode() failed");
      return Err(rv);
    }

    if (aInlineStylesAtInsertionPoint == InlineStylesAtInsertionPoint::Clear) {
      // pasting does not inherit local inline styles
      Result<EditorDOMPoint, nsresult> pointToPutCaretOrError =
          mHTMLEditor.ClearStyleAt(
              EditorDOMPoint(mHTMLEditor.SelectionRef().AnchorRef()),
              EditorInlineStyle::RemoveAllStyles(), SpecifiedStyle::Preserve,
              mEditingHost);
      if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
        NS_WARNING("HTMLEditor::ClearStyleAt() failed");
        return pointToPutCaretOrError.propagateErr();
      }
      if (pointToPutCaretOrError.inspect().IsSetAndValid()) {
        nsresult rv =
            mHTMLEditor.CollapseSelectionTo(pointToPutCaretOrError.unwrap());
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::CollapseSelectionTo() failed");
          return Err(rv);
        }
      }
    }
  } else {
    // Delete whole cells: we will replace with new table content.

    // Braces for artificial block to scope AutoSelectionRestorer.
    // Save current selection since DeleteTableCellWithTransaction() perturbs
    // it.
    {
      AutoSelectionRestorer restoreSelectionLater(&mHTMLEditor);
      rv = mHTMLEditor.DeleteTableCellWithTransaction(1);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::DeleteTableCellWithTransaction(1) failed");
        return Err(rv);
      }
    }
    // collapse selection to beginning of deleted table content
    IgnoredErrorResult ignoredError;
    mHTMLEditor.SelectionRef().CollapseToStart(ignoredError);
    NS_WARNING_ASSERTION(!ignoredError.Failed(),
                         "Selection::Collapse() failed, but ignored");
  }

  {
    Result<EditActionResult, nsresult> result =
        mHTMLEditor.CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result;
    }
    if (result.inspect().Canceled()) {
      return result;
    }
  }

  mHTMLEditor.UndefineCaretBidiLevel();

  rv = mHTMLEditor.EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && mHTMLEditor.SelectionRef().IsCollapsed()) {
    nsresult rv =
        mHTMLEditor.EnsureCaretNotAfterInvisibleBRElement(mEditingHost);
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::EnsureCaretNotAfterInvisibleBRElement() "
                         "failed, but ignored");
    if (NS_SUCCEEDED(rv)) {
      nsresult rv = mHTMLEditor.PrepareInlineStylesForCaret();
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::PrepareInlineStylesForCaret() failed, but ignored");
    }
  }

  // Adjust position based on the first node we are going to insert.
  const auto candidatePointToInsert =
      mHTMLEditor.GetFirstSelectionStartPoint<EditorRawDOMPoint>();
  if (NS_WARN_IF(!candidatePointToInsert.IsSet()) ||
      NS_WARN_IF(
          !candidatePointToInsert.GetContainer()->IsInclusiveDescendantOf(
              &mEditingHost))) {
    return Err(NS_ERROR_FAILURE);
  }
  EditorDOMPoint pointToInsert =
      HTMLEditUtils::GetBetterInsertionPointFor<EditorDOMPoint>(
          arrayOfTopMostChildContents[0],
          mHTMLEditor.GetFirstSelectionStartPoint<EditorRawDOMPoint>());
  if (!pointToInsert.IsSet()) {
    NS_WARNING("HTMLEditor::GetBetterInsertionPointFor() failed");
    return Err(NS_ERROR_FAILURE);
  }
  Result<EditorDOMPoint, nsresult> pointToInsertOrError =
      WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt(
          mHTMLEditor, pointToInsert,
          {WhiteSpaceVisibilityKeeper::NormalizeOption::
               StopIfFollowingWhiteSpacesStartsWithNBSP});
  if (MOZ_UNLIKELY(pointToInsertOrError.isErr())) {
    NS_WARNING(
        "WhiteSpaceVisibilityKeeper::NormalizeWhiteSpacesToSplitAt() failed");
    return pointToInsertOrError.propagateErr();
  }
  pointToInsert = pointToInsertOrError.unwrap();
  if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }

  const bool insertionPointWasInLink =
      !!HTMLEditor::GetLinkElement(pointToInsert.GetContainer());

  if (pointToInsert.IsInTextNode()) {
    Result<SplitNodeResult, nsresult> splitNodeResult =
        mHTMLEditor.SplitNodeDeepWithTransaction(
            MOZ_KnownLive(*pointToInsert.ContainerAs<nsIContent>()),
            pointToInsert, SplitAtEdges::eAllowToCreateEmptyContainer);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
      return splitNodeResult.propagateErr();
    }
    nsresult rv = splitNodeResult.inspect().SuggestCaretPointTo(
        mHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                      SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
    if (NS_FAILED(rv)) {
      NS_WARNING("SplitNodeResult::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    pointToInsert = splitNodeResult.inspect().AtSplitPoint<EditorDOMPoint>();
    if (MOZ_UNLIKELY(!pointToInsert.IsSet())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeDeepWithTransaction() didn't return split "
          "point");
      return Err(NS_ERROR_FAILURE);
    }
  }

  {  // Block only for AutoHTMLFragmentBoundariesFixer to hide it from the
     // following code. Note that it may modify arrayOfTopMostChildContents.
    AutoHTMLFragmentBoundariesFixer fixPiecesOfTablesAndLists(
        arrayOfTopMostChildContents);
  }

  MOZ_ASSERT(pointToInsert.GetContainer()->GetChildAt_Deprecated(
                 pointToInsert.Offset()) == pointToInsert.GetChild());

  Result<CreateContentResult, nsresult> insertNodeResultOrError =
      InsertContents(pointToInsert, arrayOfTopMostChildContents,
                     fragmentAsNode);
  if (MOZ_UNLIKELY(insertNodeResultOrError.isErr())) {
    NS_WARNING("HTMLWithContextInserter::InsertContents() failed.");
    return insertNodeResultOrError.propagateErr();
  }

  // The inserting content may contain empty container elements.  However, it's
  // intended.  Therefore, we should not clean up them in the post-processing.
  mHTMLEditor.TopLevelEditSubActionDataRef().mNeedsToCleanUpEmptyElements =
      false;

  CreateContentResult insertNodeResult = insertNodeResultOrError.unwrap();
  if (MOZ_UNLIKELY(!insertNodeResult.Handled())) {
    // Even if we haven't inserted new content nodes, we "handled" to insert
    // them so that return "handled" state.
    return EditActionResult::HandledResult();
  }

  MOZ_ASSERT(insertNodeResult.HasCaretPointSuggestion());
  EditorDOMPoint pointToPutCaret = insertNodeResult.UnwrapCaretPoint();

  if (MOZ_LIKELY(insertNodeResult.GetNewNode()->IsInComposedDoc())) {
    const auto afterLastInsertedContent =
        EditorRawDOMPoint(insertNodeResult.GetNewNode())
            .NextPointOrAfterContainer<EditorDOMPoint>();
    if (MOZ_LIKELY(afterLastInsertedContent.IsInContentNode())) {
      AutoTrackDOMPoint trackPointToPutCaret(mHTMLEditor.RangeUpdaterRef(),
                                             &pointToPutCaret);
      nsresult rv = mHTMLEditor.EnsureNoFollowingUnnecessaryLineBreak(
          afterLastInsertedContent,
          // When user inserting content, the web app may expect that nothing
          // extant content will be deleted. Therefore, we should preserve
          // preformatted linefeed at least.
          PreservePreformattedLineBreak::Yes, PaddingForEmptyBlock::Significant,
          mEditingHost);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::EnsureNoFollowingUnnecessaryLineBreak() failed");
        return Err(rv);
      }
    }
  }

  if (NS_WARN_IF(!pointToPutCaret.IsSetAndValidInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  rv = mHTMLEditor.CollapseSelectionTo(pointToPutCaret);
  if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
    NS_WARNING(
        "EditorBase::CollapseSelectionTo() caused destroying the editor");
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed, but ignored");

  // If we didn't start from an `<a href>` element, we should not keep
  // caret in the link to make users type something outside the link.
  if (insertionPointWasInLink) {
    return EditActionResult::HandledResult();
  }
  if (Element* const parentElement =
          insertNodeResult.GetNewNode()->GetParentElement()) {
    const RefPtr<Element> linkElement = GetLinkElement(parentElement);
    if (MOZ_LIKELY(!linkElement)) {
      return EditActionResult::HandledResult();
    }

    // Note that the caret point may be outside the found link. So, let's check
    // it before trying to split the link.
    if (linkElement->IsInclusiveDescendantOf(pointToPutCaret.GetContainer()))
        [[likely]] {
      nsresult rv = MoveCaretOutsideOfLink(*linkElement, pointToPutCaret);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::HTMLWithContextInserter::MoveCaretOutsideOfLink() "
            "failed");
        return Err(rv);
      }
    }
  }

  return EditActionResult::HandledResult();
}

Result<CreateContentResult, nsresult>
HTMLEditor::HTMLWithContextInserter::InsertContents(
    const EditorDOMPoint& aPointToInsert,
    nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents,
    const nsINode* aFragmentAsNode) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValidInComposedDoc());

  // Loop over the node list and paste the nodes:
  const RefPtr<const Element> maybeNonEditableBlockElement =
      aPointToInsert.IsInContentNode()
          ? HTMLEditUtils::GetInclusiveAncestorElement(
                *aPointToInsert.ContainerAs<nsIContent>(),
                HTMLEditUtils::ClosestBlockElement,
                BlockInlineCheck::UseComputedDisplayOutsideStyle)
          : nullptr;

  nsCOMPtr<nsIContent> insertedContextParentContent;
  RefPtr<nsIContent> lastInsertedContent;
  for (const OwningNonNull<nsIContent>& content :
       aArrayOfTopMostChildContents) {
    if (NS_WARN_IF(content == aFragmentAsNode) ||
        NS_WARN_IF(content->IsHTMLElement(nsGkAtoms::body))) {
      return Err(NS_ERROR_FAILURE);
    }

    if (insertedContextParentContent) {
      // If we had to insert something higher up in the paste hierarchy,
      // we want to skip any further paste nodes that descend from that.
      // Else we will paste twice.
      // XXX This check may be really expensive.  Cannot we check whether
      //     the node's `ownerDocument` is the `aFragmentAsNode` or not?
      if (EditorUtils::IsDescendantOf(*content,
                                      *insertedContextParentContent)) {
        continue;
      }
      // Okay, now, we finished moving nodes in insertedContextParentContent.
      // We can forget it now to skip the expensive check.
      insertedContextParentContent = nullptr;
    }

    // In the most cases, we want to move `content` into the DOM as-is. However,
    // in some cases, we don't want to insert content but do want to insert its
    // children into the existing proper container.  Therefore, we will check
    // the `content` type and insertion point's container below.  However, even
    // in such case, we may not be able to move only its children.  Then, we
    // need to fall it back to the default behavior.  Therefore, let's wrap the
    // default behavior into this lambda.
    const auto InsertCurrentContentToNextInsertionPoint =
        [&](const EditorDOMPoint& aPointToInsertContent)
            MOZ_CAN_RUN_SCRIPT MOZ_NEVER_INLINE_DEBUG -> Result<Ok, nsresult> {
      // MOZ_KnownLive(content) because 'aArrayOfTopMostChildContents'
      // guarantees its lifetime.
      Result<CreateContentResult, nsresult> moveContentResult =
          mHTMLEditor.InsertNodeIntoProperAncestorWithTransaction<nsIContent>(
              MOZ_KnownLive(content), aPointToInsertContent,
              SplitAtEdges::eDoNotCreateEmptyContainer);
      if (MOZ_LIKELY(moveContentResult.isOk())) {
        moveContentResult.inspect().IgnoreCaretPointSuggestion();
        if (MOZ_UNLIKELY(!moveContentResult.inspect().Handled())) {
          MOZ_ASSERT(aPointToInsertContent.IsSetAndValidInComposedDoc());
          MOZ_ASSERT_IF(lastInsertedContent,
                        lastInsertedContent->IsInComposedDoc());
          return Ok{};
        }
        lastInsertedContent = content;
        MOZ_ASSERT(lastInsertedContent->IsInComposedDoc());
        return Ok{};
      }
      // If we got unexpected DOM tree, let's abort.
      if (NS_WARN_IF(moveContentResult.inspectErr() ==
                     NS_ERROR_EDITOR_DESTROYED) ||
          NS_WARN_IF(moveContentResult.inspectErr() ==
                     NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE)) {
        return moveContentResult.propagateErr();
      }
      // If we the next insertion point becomes invalid, it means that we
      // got unexpected DOM tree which couldn't be detected by
      // InsertNodeIntoProperAncestorWithTransaction().  Let's abort to
      // avoid to move the node into unexpected position/documents.
      if (NS_WARN_IF(!aPointToInsertContent.IsSetAndValidInComposedDoc())) {
        return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
      }
      // Assume failure means no legal parent in the document hierarchy,
      // try again with the parent of content in the paste hierarchy.
      // FYI: We cannot use `InclusiveAncestorOfType` here because of
      //      calling `InsertNodeIntoProperAncestorWithTransaction()`.
      EditorDOMPoint pointToInsert = aPointToInsertContent;
      for (nsCOMPtr<nsIContent> parent = content->GetParent(); parent;
           parent = parent->GetParent()) {
        if (NS_WARN_IF(parent->IsHTMLElement(nsGkAtoms::body))) {
          break;  // for the inner `for` loop
        }
        Result<CreateContentResult, nsresult> moveParentResult =
            mHTMLEditor.InsertNodeIntoProperAncestorWithTransaction<nsIContent>(
                *parent, pointToInsert,
                SplitAtEdges::eDoNotCreateEmptyContainer);
        if (MOZ_UNLIKELY(moveParentResult.isErr())) {
          // If we got unexpected DOM tree, let's abort.
          if (NS_WARN_IF(moveParentResult.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED) ||
              NS_WARN_IF(moveParentResult.inspectErr() ==
                         NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE)) {
            return moveParentResult.propagateErr();
          }
          // If we the next insertion point becomes invalid, it means that we
          // got unexpected DOM tree which couldn't be detected by
          // InsertNodeIntoProperAncestorWithTransaction().  Let's abort to
          // avoid to move the node into unexpected position/documents.
          if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
          // If the parent cannot be inserted into the DOM tree, the node may be
          // an element to make a specific structure like a table. Then, we can
          // insert one of its ancestors to the inserting position.  So, let's
          // retry with its parent.
          continue;  // the inner `for` loop
        }
        moveParentResult.inspect().IgnoreCaretPointSuggestion();
        if (MOZ_UNLIKELY(!moveParentResult.inspect().Handled())) {
          MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
          MOZ_ASSERT_IF(lastInsertedContent,
                        lastInsertedContent->IsInComposedDoc());
          continue;
        }
        pointToInsert = EditorDOMPoint::After(*parent);
        lastInsertedContent = parent;
        MOZ_ASSERT(lastInsertedContent->IsInComposedDoc());
        insertedContextParentContent = std::move(parent);
        break;  // from the inner `for` loop
      }  // end of the inner `for` loop iterating ancestors of content
      return Ok{};
    };

    // If a `<table>` or `<tr>` element on the clipboard, and pasting it into
    // a `<table>` or `<tr>` element, insert only the appropriate children
    // instead.
    if (HTMLEditUtils::IsTableRowElement(*content)) {
      EditorDOMPoint pointToInsert =
          lastInsertedContent ? EditorDOMPoint::After(*lastInsertedContent)
                              : aPointToInsert;
      if (HTMLEditUtils::IsTableRowElement(
              pointToInsert.GetContainerAs<nsIContent>()) &&
          (content->IsHTMLElement(nsGkAtoms::table) ||
           pointToInsert.IsContainerHTMLElement(nsGkAtoms::table))) {
        MOZ_ASSERT(!content->IsInComposedDoc());
        bool inserted = false;
        for (RefPtr<nsIContent> child = content->GetFirstChild(); child;
             child = content->GetFirstChild()) {
          Result<CreateContentResult, nsresult> moveChildResult =
              mHTMLEditor
                  .InsertNodeIntoProperAncestorWithTransaction<nsIContent>(
                      *child, pointToInsert,
                      SplitAtEdges::eDoNotCreateEmptyContainer);
          if (MOZ_UNLIKELY(moveChildResult.isErr())) {
            // If we got unexpected DOM tree, let's abort.
            if (NS_WARN_IF(moveChildResult.inspectErr() ==
                           NS_ERROR_EDITOR_DESTROYED) ||
                NS_WARN_IF(moveChildResult.inspectErr() ==
                           NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE)) {
              return moveChildResult.propagateErr();
            }
            // If we the next insertion point becomes invalid, it means that
            // we got unexpected DOM tree which couldn't be detected by
            // InsertNodeIntoProperAncestorWithTransaction().  Let's abort to
            // avoid to move the node into unexpected position/documents.
            if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
              return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
            }
            NS_WARNING(
                "HTMLEditor::InsertNodeIntoProperAncestorWithTransaction("
                "SplitAtEdges::eDoNotCreateEmptyContainer) failed, but maybe "
                "ignored");
            break;  // from the inner `for` loop
          }
          moveChildResult.inspect().IgnoreCaretPointSuggestion();
          if (MOZ_UNLIKELY(!moveChildResult.inspect().Handled())) {
            MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
            MOZ_ASSERT_IF(lastInsertedContent,
                          lastInsertedContent->IsInComposedDoc());
            continue;
          }
          inserted = true;
          pointToInsert = EditorDOMPoint::After(*child);
          MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
          lastInsertedContent = std::move(child);
          MOZ_ASSERT(lastInsertedContent->IsInComposedDoc());
        }  // end of the inner `for` loop iterating children of `content`
        if (!inserted) {
          Result<Ok, nsresult> moveContentOrParentResultOrError =
              InsertCurrentContentToNextInsertionPoint(pointToInsert);
          if (MOZ_UNLIKELY(moveContentOrParentResultOrError.isErr())) {
            NS_WARNING("InsertCurrentContentToNextInsertionPoint() failed");
            return moveContentOrParentResultOrError.propagateErr();
          }
        }
        continue;
      }
    }  // if <tr>

    // If a list element on the clipboard, and pasting it into a list or
    // list item element, insert the appropriate children instead.  I.e.,
    // merge the list elements instead of pasting as a sublist.
    if (HTMLEditUtils::IsListElement(*content)) {
      EditorDOMPoint pointToInsert =
          lastInsertedContent ? EditorDOMPoint::After(*lastInsertedContent)
                              : aPointToInsert;
      if (HTMLEditUtils::IsListElement(
              pointToInsert.GetContainerAs<nsIContent>()) ||
          HTMLEditUtils::IsListItemElement(
              pointToInsert.GetContainerAs<nsIContent>())) {
        MOZ_ASSERT(!content->IsInComposedDoc());
        bool inserted = false;
        for (RefPtr<nsIContent> child = content->GetFirstChild(); child;
             child = content->GetFirstChild()) {
          // Ignore invisible nodes like `Comment` or white-space only `Text`
          // and invalid children of the list element.
          // XXX Although we should not construct invalid structure, but
          // shouldn't we preserve invalid children for avoiding dataloss?
          if (!HTMLEditUtils::IsListItemElement(*child) &&
              !HTMLEditUtils::IsListElement(*child)) {
            continue;
          }
          // If we're pasting into empty list item, we should remove it
          // and past current node into the parent list directly.
          // XXX This creates invalid structure if current list item element
          //     is not proper child of the parent element, or current node
          //     is a list element.
          if (HTMLEditUtils::IsListItemElement(
                  pointToInsert.GetContainerAs<nsIContent>()) &&
              HTMLEditUtils::IsRemovableNode(
                  *pointToInsert.ContainerAs<Element>()) &&
              HTMLEditUtils::IsEmptyNode(
                  *pointToInsert.ContainerAs<Element>(),
                  {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
            const OwningNonNull<Element> emptyListItemElement =
                *pointToInsert.ContainerAs<Element>();
            nsCOMPtr<nsINode> parentNode =
                emptyListItemElement->GetParentNode();
            MOZ_ASSERT(parentNode);
            nsCOMPtr<nsIContent> nextSibling =
                emptyListItemElement->GetNextSibling();
            nsresult rv =
                mHTMLEditor.DeleteNodeWithTransaction(*emptyListItemElement);
            if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
              NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
              return Err(NS_ERROR_EDITOR_DESTROYED);
            }
            if (NS_WARN_IF(!parentNode->IsInComposedDoc()) ||
                NS_WARN_IF(nextSibling &&
                           nextSibling->GetParentNode() != parentNode)) {
              return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
            }
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "EditorBase::DeleteNodeWithTransaction() "
                                 "failed, but ignored");
            pointToInsert =
                nextSibling ? EditorDOMPoint(std::move(nextSibling))
                            : EditorDOMPoint::AtEndOf(std::move(parentNode));
            MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
          }
          Result<CreateContentResult, nsresult> moveChildResult =
              mHTMLEditor
                  .InsertNodeIntoProperAncestorWithTransaction<nsIContent>(
                      *child, pointToInsert,
                      SplitAtEdges::eDoNotCreateEmptyContainer);
          if (MOZ_UNLIKELY(moveChildResult.isErr())) {
            // If we got unexpected DOM tree, let's abort.
            if (NS_WARN_IF(moveChildResult.inspectErr() ==
                           NS_ERROR_EDITOR_DESTROYED) ||
                NS_WARN_IF(moveChildResult.inspectErr() ==
                           NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE)) {
              return moveChildResult.propagateErr();
            }
            // If we the next insertion point becomes invalid, it means that
            // we got unexpected DOM tree which couldn't be detected by
            // InsertNodeIntoProperAncestorWithTransaction().  Let's abort to
            // avoid to move the node into unexpected position/documents.
            if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
              return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
            }
            NS_WARNING(
                "HTMLEditor::InsertNodeIntoProperAncestorWithTransaction("
                "SplitAtEdges::eDoNotCreateEmptyContainer) failed, but maybe "
                "ignored");
            break;  // from the inner `for` loop
          }
          moveChildResult.inspect().IgnoreCaretPointSuggestion();
          if (MOZ_UNLIKELY(!moveChildResult.inspect().Handled())) {
            MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
            MOZ_ASSERT_IF(lastInsertedContent,
                          lastInsertedContent->IsInComposedDoc());
            continue;
          }
          inserted = true;
          pointToInsert = EditorDOMPoint::After(*child);
          MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
          lastInsertedContent = std::move(child);
          MOZ_ASSERT(lastInsertedContent->IsInComposedDoc());
        }  // end of the inner `for` loop iterating children of `content`
        if (!inserted) {
          Result<Ok, nsresult> moveContentOrParentResultOrError =
              InsertCurrentContentToNextInsertionPoint(pointToInsert);
          if (MOZ_UNLIKELY(moveContentOrParentResultOrError.isErr())) {
            NS_WARNING("InsertCurrentContentToNextInsertionPoint() failed");
            return moveContentOrParentResultOrError.propagateErr();
          }
        }
        continue;
      }
    }  // if <ul>, <ol> or <dl>

    // If pasting into a `<pre>` element and current node is a `<pre>` element,
    // move only its children.
    if (maybeNonEditableBlockElement &&
        maybeNonEditableBlockElement->IsHTMLElement(nsGkAtoms::pre) &&
        content->IsHTMLElement(nsGkAtoms::pre)) {
      MOZ_ASSERT(!content->IsInComposedDoc());
      EditorDOMPoint pointToInsert =
          lastInsertedContent ? EditorDOMPoint::After(*lastInsertedContent)
                              : aPointToInsert;
      bool inserted = false;
      for (RefPtr<nsIContent> child = content->GetFirstChild(); child;
           child = content->GetFirstChild()) {
        Result<CreateContentResult, nsresult> moveChildResult =
            mHTMLEditor.InsertNodeIntoProperAncestorWithTransaction<nsIContent>(
                *child, pointToInsert,
                SplitAtEdges::eDoNotCreateEmptyContainer);
        if (MOZ_UNLIKELY(moveChildResult.isErr())) {
          // If we got unexpected DOM tree, let's abort.
          if (NS_WARN_IF(moveChildResult.inspectErr() ==
                         NS_ERROR_EDITOR_DESTROYED) ||
              NS_WARN_IF(moveChildResult.inspectErr() ==
                         NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE)) {
            return moveChildResult.propagateErr();
          }
          // If we the next insertion point becomes invalid, it means that we
          // got unexpected DOM tree which couldn't be detected by
          // InsertNodeIntoProperAncestorWithTransaction().  Let's abort to
          // avoid to move the node into unexpected position/documents.
          if (NS_WARN_IF(!pointToInsert.IsSetAndValidInComposedDoc())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
          NS_WARNING(
              "HTMLEditor::InsertNodeIntoProperAncestorWithTransaction("
              "SplitAtEdges::eDoNotCreateEmptyContainer) failed, but maybe "
              "ignored");
          break;  // from the inner `for` loop
        }
        moveChildResult.inspect().IgnoreCaretPointSuggestion();
        if (MOZ_UNLIKELY(!moveChildResult.inspect().Handled())) {
          MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
          MOZ_ASSERT_IF(lastInsertedContent,
                        lastInsertedContent->IsInComposedDoc());
          continue;
        }
        inserted = true;
        pointToInsert = EditorDOMPoint::After(*child);
        MOZ_ASSERT(pointToInsert.IsSetAndValidInComposedDoc());
        lastInsertedContent = std::move(child);
        MOZ_ASSERT(lastInsertedContent->IsInComposedDoc());
      }  // end of the inner `for` loop iterating children of `content`
      if (!inserted) {
        Result<Ok, nsresult> moveContentOrParentResultOrError =
            InsertCurrentContentToNextInsertionPoint(pointToInsert);
        if (MOZ_UNLIKELY(moveContentOrParentResultOrError.isErr())) {
          NS_WARNING("InsertCurrentContentToNextInsertionPoint() failed");
          return moveContentOrParentResultOrError.propagateErr();
        }
      }
      continue;
    }  // if <pre> and inserting into a connected <pre>

    // By default, we should move `content` into the DOM.
    Result<Ok, nsresult> moveContentOrParentResultOrError =
        InsertCurrentContentToNextInsertionPoint(
            lastInsertedContent ? EditorDOMPoint::After(*lastInsertedContent)
                                : aPointToInsert);
    if (MOZ_UNLIKELY(moveContentOrParentResultOrError.isErr())) {
      NS_WARNING("InsertCurrentContentToNextInsertionPoint() failed");
      return moveContentOrParentResultOrError.propagateErr();
    }
  }  // end of the `for` loop iterating aArrayOfTopMostChildContents

  if (!lastInsertedContent) {
    return CreateContentResult::NotHandled();
  }
  EditorDOMPoint pointToPutCaret =
      GetNewCaretPointAfterInsertingHTML(EditorDOMPoint(lastInsertedContent));
  return CreateContentResult(std::move(lastInsertedContent),
                             std::move(pointToPutCaret));
}

nsresult HTMLEditor::HTMLWithContextInserter::MoveCaretOutsideOfLink(
    Element& aLinkElement, const EditorDOMPoint& aPointToPutCaret) {
  MOZ_ASSERT(HTMLEditUtils::IsHyperlinkElement(aLinkElement));

  // The reason why do that instead of just moving caret after it is, the
  // link might have ended in an invisible `<br>` element.  If so, the code
  // above just placed selection inside that.  So we need to split it instead.
  // XXX Sounds like that it's not really expensive comparing with the reason
  //     to use SplitNodeDeepWithTransaction() here.
  Result<SplitNodeResult, nsresult> splitLinkResult =
      mHTMLEditor.SplitNodeDeepWithTransaction(
          aLinkElement, aPointToPutCaret,
          SplitAtEdges::eDoNotCreateEmptyContainer);
  if (splitLinkResult.isErr()) [[unlikely]] {
    NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
    return splitLinkResult.unwrapErr();
  }

  if (nsIContent* previousContentOfSplitPoint =
          splitLinkResult.inspect().GetPreviousContent()) {
    splitLinkResult.inspect().IgnoreCaretPointSuggestion();
    nsresult rv = mHTMLEditor.CollapseSelectionTo(
        EditorRawDOMPoint::After(*previousContentOfSplitPoint));
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
    return NS_OK;
  }

  nsresult rv = splitLinkResult.inspect().SuggestCaretPointTo(
      mHTMLEditor, {SuggestCaret::OnlyIfHasSuggestion,
                    SuggestCaret::OnlyIfTransactionsAllowedToDoIt});
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "SplitNodeResult::SuggestCaretPointTo() failed");
  return rv;
}

// static
Element* HTMLEditor::GetLinkElement(nsINode* aNode) {
  if (NS_WARN_IF(!aNode)) {
    return nullptr;
  }
  nsINode* node = aNode;
  while (node) {
    if (node->IsElement() &&
        HTMLEditUtils::IsHyperlinkElement(*node->AsElement())) {
      return node->AsElement();
    }
    node = node->GetParentNode();
  }
  return nullptr;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
        nsIContent& aNode, NodesToRemove aNodesToRemove) {
  if (aNode.TextIsOnlyWhitespace()) {
    nsCOMPtr<nsINode> parent = aNode.GetParentNode();
    // TODO: presumably, if the parent is a `<pre>` element, the node
    // shouldn't be removed.
    if (parent) {
      if (aNodesToRemove == NodesToRemove::eAll ||
          HTMLEditUtils::IsListElement(nsIContent::FromNode(parent))) {
        ErrorResult error;
        parent->RemoveChild(aNode, error);
        NS_WARNING_ASSERTION(!error.Failed(), "nsINode::RemoveChild() failed");
        return error.StealNSResult();
      }
      return NS_OK;
    }
  }

  if (!aNode.IsHTMLElement(nsGkAtoms::pre)) {
    nsCOMPtr<nsIContent> child = aNode.GetLastChild();
    while (child) {
      nsCOMPtr<nsIContent> previous = child->GetPreviousSibling();
      nsresult rv = FragmentFromPasteCreator::
          RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
              *child, aNodesToRemove);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
            "RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces"
            "() "
            "failed");
        return rv;
      }
      child = std::move(previous);
    }
  }
  return NS_OK;
}

class MOZ_STACK_CLASS HTMLEditor::HTMLTransferablePreparer {
 public:
  HTMLTransferablePreparer(const HTMLEditor& aHTMLEditor,
                           nsITransferable** aTransferable,
                           const Element* aEditingHost);

  nsresult Run();

 private:
  void AddDataFlavorsInBestOrder(nsITransferable& aTransferable) const;

  const HTMLEditor& mHTMLEditor;
  const Element* const mEditingHost;
  nsITransferable** mTransferable;
};

HTMLEditor::HTMLTransferablePreparer::HTMLTransferablePreparer(
    const HTMLEditor& aHTMLEditor, nsITransferable** aTransferable,
    const Element* aEditingHost)
    : mHTMLEditor{aHTMLEditor},
      mEditingHost(aEditingHost),
      mTransferable{aTransferable} {
  MOZ_ASSERT(mTransferable);
  MOZ_ASSERT(!*mTransferable);
}

nsresult HTMLEditor::PrepareHTMLTransferable(
    nsITransferable** aTransferable, const Element* aEditingHost) const {
  HTMLTransferablePreparer htmlTransferablePreparer{*this, aTransferable,
                                                    aEditingHost};
  return htmlTransferablePreparer.Run();
}

nsresult HTMLEditor::HTMLTransferablePreparer::Run() {
  // Create generic Transferable for getting the data
  nsresult rv;
  RefPtr<nsITransferable> transferable =
      do_CreateInstance("@mozilla.org/widget/transferable;1", &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("do_CreateInstance() failed to create nsITransferable instance");
    return rv;
  }

  if (!transferable) {
    NS_WARNING("do_CreateInstance() returned nullptr, but ignored");
    return NS_OK;
  }

  // Get the nsITransferable interface for getting the data from the clipboard
  RefPtr<Document> destdoc = mHTMLEditor.GetDocument();
  nsILoadContext* loadContext = destdoc ? destdoc->GetLoadContext() : nullptr;
  DebugOnly<nsresult> rvIgnored = transferable->Init(loadContext);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "nsITransferable::Init() failed, but ignored");

  // See `HTMLEditor::InsertFromTransferableAtSelection`.
  AddDataFlavorsInBestOrder(*transferable);

  transferable.forget(mTransferable);

  return NS_OK;
}

void HTMLEditor::HTMLTransferablePreparer::AddDataFlavorsInBestOrder(
    nsITransferable& aTransferable) const {
  // Create the desired DataFlavor for the type of data
  // we want to get out of the transferable
  // This should only happen in html editors, not plaintext
  // NOTE: If you change the order or flavors, you need to modify
  // HTMLEditor::InsertFromDataTransfer() too.
  // NOTE: If you add more flavors here you will need to add them to
  // DataTransfer::GetExternalClipboardFormats as well.
  if (mHTMLEditor.IsStyleEditable(mEditingHost)) {
    DebugOnly<nsresult> rvIgnored =
        aTransferable.AddDataFlavor(kNativeHTMLMime);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsITransferable::AddDataFlavor(kNativeHTMLMime) failed, but ignored");
    rvIgnored = aTransferable.AddDataFlavor(kHTMLMime);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsITransferable::AddDataFlavor(kHTMLMime) failed, but ignored");
    rvIgnored = aTransferable.AddDataFlavor(kFileMime);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsITransferable::AddDataFlavor(kFileMime) failed, but ignored");

    switch (Preferences::GetInt("clipboard.paste_image_type", 1)) {
      case 0:  // prefer JPEG over PNG over GIF encoding
        rvIgnored = aTransferable.AddDataFlavor(kJPEGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPEGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kJPGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kPNGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kPNGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kGIFImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kGIFImageMime) "
                             "failed, but ignored");
        break;
      case 1:  // prefer PNG over JPEG over GIF encoding (default)
      default:
        rvIgnored = aTransferable.AddDataFlavor(kPNGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kPNGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kJPEGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPEGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kJPGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kGIFImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kGIFImageMime) "
                             "failed, but ignored");
        break;
      case 2:  // prefer GIF over JPEG over PNG encoding
        rvIgnored = aTransferable.AddDataFlavor(kGIFImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kGIFImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kJPEGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPEGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kJPGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kJPGImageMime) "
                             "failed, but ignored");
        rvIgnored = aTransferable.AddDataFlavor(kPNGImageMime);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "nsITransferable::AddDataFlavor(kPNGImageMime) "
                             "failed, but ignored");
        break;
    }
  }
  // GetAnyTransferData() uses this order, so put URL data before text in HTML
  // editors to preserve pasted links.
  DebugOnly<nsresult> rvIgnored = aTransferable.AddDataFlavor(kURLDataMime);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsITransferable::AddDataFlavor(kURLDataMime) failed, but ignored");
  rvIgnored = aTransferable.AddDataFlavor(kTextMime);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsITransferable::AddDataFlavor(kTextMime) failed, but ignored");
  rvIgnored = aTransferable.AddDataFlavor(kMozTextInternal);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsITransferable::AddDataFlavor(kMozTextInternal) failed, but ignored");
}

bool FindIntegerAfterString(const char* aLeadingString, const nsCString& aCStr,
                            int32_t& foundNumber) {
  // first obtain offsets from cfhtml str
  int32_t numFront = aCStr.Find(aLeadingString);
  if (numFront == -1) {
    return false;
  }
  numFront += strlen(aLeadingString);

  int32_t numBack = aCStr.FindCharInSet(CRLF, numFront);
  if (numBack == -1) {
    return false;
  }

  nsAutoCString numStr(Substring(aCStr, numFront, numBack - numFront));
  nsresult errorCode;
  foundNumber = numStr.ToInteger(&errorCode);
  return true;
}

void RemoveFragComments(nsCString& aStr) {
  // remove the StartFragment/EndFragment comments from the str, if present
  int32_t startCommentIndx = aStr.Find("<!--StartFragment");
  if (startCommentIndx >= 0) {
    int32_t startCommentEnd = aStr.Find("-->", startCommentIndx);
    if (startCommentEnd > startCommentIndx) {
      aStr.Cut(startCommentIndx, (startCommentEnd + 3) - startCommentIndx);
    }
  }
  int32_t endCommentIndx = aStr.Find("<!--EndFragment");
  if (endCommentIndx >= 0) {
    int32_t endCommentEnd = aStr.Find("-->", endCommentIndx);
    if (endCommentEnd > endCommentIndx) {
      aStr.Cut(endCommentIndx, (endCommentEnd + 3) - endCommentIndx);
    }
  }
}

nsresult HTMLEditor::ParseCFHTML(const nsCString& aCfhtml,
                                 char16_t** aStuffToPaste,
                                 char16_t** aCfcontext) {
  // First obtain offsets from cfhtml str.
  int32_t startHTML, endHTML, startFragment, endFragment;
  if (!FindIntegerAfterString("StartHTML:", aCfhtml, startHTML) ||
      startHTML < -1) {
    return NS_ERROR_FAILURE;
  }
  if (!FindIntegerAfterString("EndHTML:", aCfhtml, endHTML) || endHTML < -1) {
    return NS_ERROR_FAILURE;
  }
  if (!FindIntegerAfterString("StartFragment:", aCfhtml, startFragment) ||
      startFragment < 0) {
    return NS_ERROR_FAILURE;
  }
  if (!FindIntegerAfterString("EndFragment:", aCfhtml, endFragment) ||
      endFragment < 0) {
    return NS_ERROR_FAILURE;
  }

  // The StartHTML and EndHTML markers are allowed to be -1 to include
  // everything.
  //   See Reference: MSDN doc entitled "HTML Clipboard Format"
  //   http://msdn.microsoft.com/en-us/library/aa767917(VS.85).aspx#unknown_854
  if (startHTML == -1) {
    startHTML = aCfhtml.Find("<!--StartFragment-->");
    if (startHTML == -1) {
      return NS_OK;
    }
  }
  if (endHTML == -1) {
    const char endFragmentMarker[] = "<!--EndFragment-->";
    endHTML = aCfhtml.Find(endFragmentMarker);
    if (endHTML == -1) {
      return NS_OK;
    }
    endHTML += std::size(endFragmentMarker) - 1;
  }

  // create context string
  nsAutoCString contextUTF8(
      Substring(aCfhtml, startHTML, startFragment - startHTML) +
      "<!--" kInsertCookie "-->"_ns +
      Substring(aCfhtml, endFragment, endHTML - endFragment));

  // validate startFragment
  // make sure it's not in the middle of a HTML tag
  // see bug #228879 for more details
  int32_t curPos = startFragment;
  while (curPos > startHTML) {
    if (aCfhtml[curPos] == '>') {
      // working backwards, the first thing we see is the end of a tag
      // so StartFragment is good, so do nothing.
      break;
    }
    if (aCfhtml[curPos] == '<') {
      // if we are at the start, then we want to see the '<'
      if (curPos != startFragment) {
        // working backwards, the first thing we see is the start of a tag
        // so StartFragment is bad, so we need to update it.
        NS_ERROR(
            "StartFragment byte count in the clipboard looks bad, see bug "
            "#228879");
        startFragment = curPos - 1;
      }
      break;
    }
    curPos--;
  }

  // create fragment string
  nsAutoCString fragmentUTF8(
      Substring(aCfhtml, startFragment, endFragment - startFragment));

  // remove the StartFragment/EndFragment comments from the fragment, if present
  RemoveFragComments(fragmentUTF8);

  // remove the StartFragment/EndFragment comments from the context, if present
  RemoveFragComments(contextUTF8);

  // convert both strings to usc2
  const nsString& fragUcs2Str = NS_ConvertUTF8toUTF16(fragmentUTF8);
  const nsString& cntxtUcs2Str = NS_ConvertUTF8toUTF16(contextUTF8);

  // translate platform linebreaks for fragment
  int32_t oldLengthInChars =
      fragUcs2Str.Length() + 1;  // +1 to include null terminator
  int32_t newLengthInChars = 0;
  *aStuffToPaste = nsLinebreakConverter::ConvertUnicharLineBreaks(
      fragUcs2Str.get(), nsLinebreakConverter::eLinebreakAny,
      nsLinebreakConverter::eLinebreakContent, oldLengthInChars,
      &newLengthInChars);
  if (!*aStuffToPaste) {
    NS_WARNING("nsLinebreakConverter::ConvertUnicharLineBreaks() failed");
    return NS_ERROR_FAILURE;
  }

  // translate platform linebreaks for context
  oldLengthInChars =
      cntxtUcs2Str.Length() + 1;  // +1 to include null terminator
  newLengthInChars = 0;
  *aCfcontext = nsLinebreakConverter::ConvertUnicharLineBreaks(
      cntxtUcs2Str.get(), nsLinebreakConverter::eLinebreakAny,
      nsLinebreakConverter::eLinebreakContent, oldLengthInChars,
      &newLengthInChars);
  // it's ok for context to be empty.  frag might be whole doc and contain all
  // its context.

  // we're done!
  return NS_OK;
}

static nsresult ImgFromData(const nsACString& aType, const nsACString& aData,
                            nsString& aOutput) {
  aOutput.AssignLiteral("<IMG src=\"data:");
  AppendUTF8toUTF16(aType, aOutput);
  aOutput.AppendLiteral(";base64,");
  nsresult rv = Base64EncodeAppend(aData, aOutput);
  if (NS_FAILED(rv)) {
    NS_WARNING("Base64Encode() failed");
    return rv;
  }
  aOutput.AppendLiteral("\" alt=\"\" >");
  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLEditor::BlobReader)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(HTMLEditor::BlobReader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBlob)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHTMLEditor)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPointToInsert)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDataTransfer)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(HTMLEditor::BlobReader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBlob)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHTMLEditor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPointToInsert)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDataTransfer)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

HTMLEditor::BlobReader::BlobReader(BlobImpl* aBlob, HTMLEditor* aHTMLEditor,
                                   SafeToInsertData aSafeToInsertData,
                                   const EditorDOMPoint& aPointToInsert,
                                   DeleteSelectedContent aDeleteSelectedContent,
                                   const Element& aEditingHost)
    : mBlob(aBlob),
      mHTMLEditor(aHTMLEditor),
      mEditingHost(&aEditingHost),
      // "beforeinput" event should've been dispatched before we read blob,
      // but anyway, we need to clone dataTransfer for "input" event.
      mDataTransfer(mHTMLEditor->GetInputEventDataTransfer()),
      mPointToInsert(aPointToInsert),
      mEditAction(aHTMLEditor->GetEditAction()),
      mSafeToInsertData(aSafeToInsertData),
      mDeleteSelectedContent(aDeleteSelectedContent),
      mNeedsToDispatchBeforeInputEvent(
          !mHTMLEditor->HasTriedToDispatchBeforeInputEvent()) {
  MOZ_ASSERT(mBlob);
  MOZ_ASSERT(mHTMLEditor);
  MOZ_ASSERT(mHTMLEditor->IsEditActionDataAvailable());
  MOZ_ASSERT(mDataTransfer);

  // Take only offset here since it's our traditional behavior.
  if (mPointToInsert.IsSet()) {
    AutoEditorDOMPointChildInvalidator storeOnlyWithOffset(mPointToInsert);
  }
}

nsresult HTMLEditor::BlobReader::OnResult(const nsACString& aResult) {
  if (NS_WARN_IF(!mEditingHost)) {
    return NS_ERROR_FAILURE;
  }
  AutoEditActionDataSetter editActionData(*mHTMLEditor, mEditAction);
  editActionData.InitializeDataTransfer(mDataTransfer);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(mNeedsToDispatchBeforeInputEvent)) {
    nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
    if (NS_FAILED(rv)) {
      NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                           "MaybeDispatchBeforeInputEvent(), failed");
      return EditorBase::ToGenericNSResult(rv);
    }
  } else {
    editActionData.MarkAsBeforeInputHasBeenDispatched();
  }

  nsString blobType;
  mBlob->GetType(blobType);

  // TODO: This does not work well.
  // * If the data is not an image file, this inserts <img> element with odd
  //   data URI (bug 1610220).
  // * If the data is valid image file data, an <img> file is inserted with
  //   data URI, but it's not loaded (bug 1610219).
  NS_ConvertUTF16toUTF8 type(blobType);
  nsAutoString stuffToPaste;
  nsresult rv = ImgFromData(type, aResult, stuffToPaste);
  if (NS_FAILED(rv)) {
    NS_WARNING("ImgFormData() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  RefPtr<HTMLEditor> htmlEditor = std::move(mHTMLEditor);
  AutoPlaceholderBatch treatAsOneTransaction(
      *htmlEditor, ScrollSelectionIntoView::Yes, __FUNCTION__);
  EditorDOMPoint pointToInsert = std::move(mPointToInsert);
  const RefPtr<const Element> editingHost = std::move(mEditingHost);
  rv = htmlEditor->InsertHTMLWithContextAsSubAction(
      stuffToPaste, u""_ns, u""_ns, NS_LITERAL_STRING_FROM_CSTRING(kFileMime),
      mSafeToInsertData, pointToInsert, mDeleteSelectedContent,
      InlineStylesAtInsertionPoint::Preserve, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertHTMLWithContextAsSubAction("
                       "InlineStylesAtInsertionPoint::Preserve) failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::BlobReader::OnError(const nsAString& aError) {
  AutoTArray<nsString, 1> error;
  error.AppendElement(aError);
  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Editor"_ns, mHTMLEditor->GetDocument(),
      PropertiesFile::DOM_PROPERTIES, "EditorFileDropFailed", error);
  return NS_OK;
}

class SlurpBlobEventListener final : public nsIDOMEventListener {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(SlurpBlobEventListener)

  explicit SlurpBlobEventListener(HTMLEditor::BlobReader* aListener)
      : mListener(aListener) {}

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD HandleEvent(Event* aEvent) override;

 private:
  ~SlurpBlobEventListener() = default;

  RefPtr<HTMLEditor::BlobReader> mListener;
};

NS_IMPL_CYCLE_COLLECTION(SlurpBlobEventListener, mListener)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SlurpBlobEventListener)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(SlurpBlobEventListener)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SlurpBlobEventListener)

NS_IMETHODIMP SlurpBlobEventListener::HandleEvent(Event* aEvent) {
  EventTarget* target = aEvent->GetTarget();
  if (!target || !mListener) {
    return NS_OK;
  }

  RefPtr<FileReader> reader = do_QueryObject(target);
  if (!reader) {
    return NS_OK;
  }

  EventMessage message = aEvent->WidgetEventPtr()->mMessage;

  RefPtr<HTMLEditor::BlobReader> listener(mListener);
  if (message == eLoad) {
    MOZ_ASSERT(reader->DataFormat() == FileReader::FILE_AS_BINARY);

    // The original data has been converted from Latin1 to UTF-16, this just
    // undoes that conversion.
    DebugOnly<nsresult> rvIgnored =
        listener->OnResult(NS_LossyConvertUTF16toASCII(reader->Result()));
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::BlobReader::OnResult() failed, but ignored");
    return NS_OK;
  }

  if (message == eLoadError) {
    nsAutoString errorMessage;
    reader->GetError()->GetErrorMessage(errorMessage);
    DebugOnly<nsresult> rvIgnored = listener->OnError(errorMessage);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::BlobReader::OnError() failed, but ignored");
    return NS_OK;
  }

  return NS_OK;
}

// static
nsresult HTMLEditor::SlurpBlob(Blob* aBlob, nsIGlobalObject* aGlobal,
                               BlobReader* aBlobReader) {
  MOZ_ASSERT(aBlob);
  MOZ_ASSERT(aGlobal);
  MOZ_ASSERT(aBlobReader);

  RefPtr<WeakWorkerRef> workerRef;
  RefPtr reader = MakeRefPtr<FileReader>(aGlobal, workerRef);

  RefPtr eventListener = MakeRefPtr<SlurpBlobEventListener>(aBlobReader);

  nsresult rv = reader->AddEventListener(u"load"_ns, eventListener, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("FileReader::AddEventListener(load) failed");
    return rv;
  }

  rv = reader->AddEventListener(u"error"_ns, eventListener, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("FileReader::AddEventListener(error) failed");
    return rv;
  }

  ErrorResult error;
  reader->ReadAsBinaryString(*aBlob, error);
  NS_WARNING_ASSERTION(!error.Failed(),
                       "FileReader::ReadAsBinaryString() failed");
  return error.StealNSResult();
}

nsresult HTMLEditor::InsertObject(const nsACString& aType, nsISupports* aObject,
                                  SafeToInsertData aSafeToInsertData,
                                  const EditorDOMPoint& aPointToInsert,
                                  DeleteSelectedContent aDeleteSelectedContent,
                                  const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Check to see if we the file is actually an image.
  nsAutoCString type(aType);
  if (type.EqualsLiteral(kFileMime)) {
    if (nsCOMPtr<nsIFile> file = do_QueryInterface(aObject)) {
      nsCOMPtr<nsIMIMEService> mime = do_GetService("@mozilla.org/mime;1");
      if (NS_WARN_IF(!mime)) {
        return NS_ERROR_FAILURE;
      }

      nsresult rv = mime->GetTypeFromFile(file, type);
      if (NS_FAILED(rv)) {
        NS_WARNING("nsIMIMEService::GetTypeFromFile() failed");
        return rv;
      }
    }
  }

  nsCOMPtr<nsISupports> object = aObject;
  if (type.EqualsLiteral(kJPEGImageMime) || type.EqualsLiteral(kJPGImageMime) ||
      type.EqualsLiteral(kPNGImageMime) || type.EqualsLiteral(kGIFImageMime)) {
    if (nsCOMPtr<nsIFile> file = do_QueryInterface(object)) {
      object = new FileBlobImpl(file);
      // Fallthrough to BlobImpl code below.
    } else if (RefPtr<Blob> blob = do_QueryObject(object)) {
      object = blob->Impl();
      // Fallthrough.
    } else if (nsCOMPtr<nsIInputStream> imageStream =
                   do_QueryInterface(object)) {
      nsCString imageData;
      nsresult rv = NS_ConsumeStream(imageStream, UINT32_MAX, imageData);
      if (NS_FAILED(rv)) {
        NS_WARNING("NS_ConsumeStream() failed");
        return rv;
      }

      rv = imageStream->Close();
      if (NS_FAILED(rv)) {
        NS_WARNING("nsIInputStream::Close() failed");
        return rv;
      }

      nsAutoString stuffToPaste;
      rv = ImgFromData(type, imageData, stuffToPaste);
      if (NS_FAILED(rv)) {
        NS_WARNING("ImgFromData() failed");
        return rv;
      }

      AutoPlaceholderBatch treatAsOneTransaction(
          *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
      rv = InsertHTMLWithContextAsSubAction(
          stuffToPaste, u""_ns, u""_ns,
          NS_LITERAL_STRING_FROM_CSTRING(kFileMime), aSafeToInsertData,
          aPointToInsert, aDeleteSelectedContent,
          InlineStylesAtInsertionPoint::Preserve, aEditingHost);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::InsertHTMLWithContextAsSubAction("
          "InlineStylesAtInsertionPoint::Preserve) failed, but ignored");
      return NS_OK;
    } else {
      NS_WARNING("HTMLEditor::InsertObject: Unexpected type for image mime");
      return NS_OK;
    }
  }

  // We always try to insert BlobImpl even without a known image mime.
  nsCOMPtr<BlobImpl> blob = do_QueryInterface(object);
  if (!blob) {
    return NS_OK;
  }

  RefPtr<BlobReader> br =
      new BlobReader(blob, this, aSafeToInsertData, aPointToInsert,
                     aDeleteSelectedContent, aEditingHost);

  nsCOMPtr<nsPIDOMWindowInner> inner = GetInnerWindow();
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(inner);
  if (!global) {
    NS_WARNING("Could not get global");
    return NS_ERROR_FAILURE;
  }

  RefPtr<Blob> domBlob = Blob::Create(global, blob);
  if (!domBlob) {
    NS_WARNING("Blob::Create() failed");
    return NS_ERROR_FAILURE;
  }

  nsresult rv = SlurpBlob(domBlob, global, br);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::::SlurpBlob() failed");
  return rv;
}

static bool GetString(nsISupports* aData, nsAString& aText) {
  if (nsCOMPtr<nsISupportsString> str = do_QueryInterface(aData)) {
    DebugOnly<nsresult> rvIgnored = str->GetData(aText);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsISupportsString::GetData() failed, but ignored");
    return !aText.IsEmpty();
  }

  return false;
}

static bool GetCString(nsISupports* aData, nsACString& aText) {
  if (nsCOMPtr<nsISupportsCString> str = do_QueryInterface(aData)) {
    DebugOnly<nsresult> rvIgnored = str->GetData(aText);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsISupportsString::GetData() failed, but ignored");
    return !aText.IsEmpty();
  }

  return false;
}

nsresult HTMLEditor::InsertFromTransferableAtSelection(
    nsITransferable* aTransferable, const nsAString& aContextStr,
    const nsAString& aInfoStr, HavePrivateHTMLFlavor aHavePrivateHTMLFlavor,
    const Element& aEditingHost) {
  nsAutoCString bestFlavor;
  nsCOMPtr<nsISupports> genericDataObj;

  // See `HTMLTransferablePreparer::AddDataFlavorsInBestOrder`.
  nsresult rv = aTransferable->GetAnyTransferData(
      bestFlavor, getter_AddRefs(genericDataObj));
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "nsITransferable::GetAnyTransferData() failed, but ignored");
  if (NS_SUCCEEDED(rv)) {
    AutoTransactionsConserveSelection dontChangeMySelection(*this);
    nsAutoString flavor;
    CopyASCIItoUTF16(bestFlavor, flavor);
    const SafeToInsertData safeToInsertData = IsSafeToInsertData(nullptr);

    const bool isPlaintextEditor = !IsStyleEditable(&aEditingHost);

    if (bestFlavor.EqualsLiteral(kFileMime) ||
        bestFlavor.EqualsLiteral(kJPEGImageMime) ||
        bestFlavor.EqualsLiteral(kJPGImageMime) ||
        bestFlavor.EqualsLiteral(kPNGImageMime) ||
        bestFlavor.EqualsLiteral(kGIFImageMime)) {
      nsresult rv = InsertObject(bestFlavor, genericDataObj, safeToInsertData,
                                 EditorDOMPoint(), DeleteSelectedContent::Yes,
                                 aEditingHost);
      if (NS_FAILED(rv)) {
        NS_WARNING("HTMLEditor::InsertObject() failed");
        return rv;
      }
    } else if (bestFlavor.EqualsLiteral(kNativeHTMLMime)) {
      // note cf_html uses utf8
      nsAutoCString cfhtml;
      if (GetCString(genericDataObj, cfhtml)) {
        // cfselection left emtpy for now.
        nsString cfcontext, cffragment, cfselection;
        nsresult rv = ParseCFHTML(cfhtml, getter_Copies(cffragment),
                                  getter_Copies(cfcontext));
        if (NS_SUCCEEDED(rv) && !cffragment.IsEmpty()) {
          // If we have our private HTML flavor, we will only use the fragment
          // from the CF_HTML. The rest comes from the clipboard.
          if (aHavePrivateHTMLFlavor == HavePrivateHTMLFlavor::Yes) {
            AutoPlaceholderBatch treatAsOneTransaction(
                *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
            rv = InsertHTMLWithContextAsSubAction(
                cffragment, aContextStr, aInfoStr, flavor, safeToInsertData,
                EditorDOMPoint(), DeleteSelectedContent::Yes,
                InlineStylesAtInsertionPoint::Clear, aEditingHost);
            if (NS_FAILED(rv)) {
              NS_WARNING(
                  "HTMLEditor::InsertHTMLWithContextAsSubAction("
                  "DeleteSelectedContent::Yes, "
                  "InlineStylesAtInsertionPoint::Clear) failed");
              return rv;
            }
          } else {
            AutoPlaceholderBatch treatAsOneTransaction(
                *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
            rv = InsertHTMLWithContextAsSubAction(
                cffragment, cfcontext, cfselection, flavor, safeToInsertData,
                EditorDOMPoint(), DeleteSelectedContent::Yes,
                InlineStylesAtInsertionPoint::Clear, aEditingHost);
            if (NS_FAILED(rv)) {
              NS_WARNING(
                  "HTMLEditor::InsertHTMLWithContextAsSubAction("
                  "DeleteSelectedContent::Yes, "
                  "InlineStylesAtInsertionPoint::Clear) failed");
              return rv;
            }
          }
        } else {
          // In some platforms (like Linux), the clipboard might return data
          // requested for unknown flavors (for example:
          // application/x-moz-nativehtml).  In this case, treat the data
          // to be pasted as mere HTML to get the best chance of pasting it
          // correctly.
          bestFlavor.AssignLiteral(kHTMLMime);
          // Fall through the next case
        }
      }
    }
    if (bestFlavor.EqualsLiteral(kHTMLMime) ||
        bestFlavor.EqualsLiteral(kTextMime) ||
        bestFlavor.EqualsLiteral(kMozTextInternal) ||
        bestFlavor.EqualsLiteral(kURLDataMime)) {
      nsAutoString stuffToPaste;
      if (!GetString(genericDataObj, stuffToPaste)) {
        nsAutoCString text;
        if (GetCString(genericDataObj, text)) {
          CopyUTF8toUTF16(text, stuffToPaste);
        }
      }

      if (!stuffToPaste.IsEmpty()) {
        if (bestFlavor.EqualsLiteral(kHTMLMime)) {
          AutoPlaceholderBatch treatAsOneTransaction(
              *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
          nsresult rv = InsertHTMLWithContextAsSubAction(
              stuffToPaste, aContextStr, aInfoStr, flavor, safeToInsertData,
              EditorDOMPoint(), DeleteSelectedContent::Yes,
              InlineStylesAtInsertionPoint::Clear, aEditingHost);
          if (NS_FAILED(rv)) {
            NS_WARNING(
                "HTMLEditor::InsertHTMLWithContextAsSubAction("
                "DeleteSelectedContent::Yes, "
                "InlineStylesAtInsertionPoint::Clear) failed");
            return rv;
          }
        } else if (bestFlavor.EqualsLiteral(kURLDataMime) &&
                   !isPlaintextEditor) {
          AutoPlaceholderBatch treatAsOneTransaction(
              *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
          EditorDOMPoint pointToInsert(SelectionRef().AnchorRef());
          nsresult rv = InsertURLAsLinkInternal(stuffToPaste, pointToInsert,
                                                DeleteSelectedContent::Yes);
          if (NS_FAILED(rv)) {
            NS_WARNING("InsertURLAsLink() failed");
            return rv;
          }
        } else {
          AutoPlaceholderBatch treatAsOneTransaction(
              *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
          nsresult rv =
              InsertTextAsSubAction(stuffToPaste, InsertTextFor::NormalText);
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::InsertTextAsSubAction() failed");
            return rv;
          }
        }
      }
    }
  }

  // Try to scroll the selection into view if the paste succeeded
  DebugOnly<nsresult> rvIgnored = ScrollSelectionFocusIntoView();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::ScrollSelectionFocusIntoView() failed, but ignored");
  return NS_OK;
}

static void GetStringFromDataTransfer(const DataTransfer* aDataTransfer,
                                      const nsAString& aType, uint32_t aIndex,
                                      nsString& aOutputString) {
  nsCOMPtr<nsIVariant> variant;
  DebugOnly<nsresult> rvIgnored = aDataTransfer->GetDataAtNoSecurityCheck(
      aType, aIndex, getter_AddRefs(variant));
  if (!variant) {
    MOZ_ASSERT(aOutputString.IsEmpty());
    return;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "DataTransfer::GetDataAtNoSecurityCheck() failed, but ignored");
  variant->GetAsAString(aOutputString);
  nsContentUtils::PlatformToDOMLineBreaks(aOutputString);
}

nsresult HTMLEditor::InsertFromDataTransfer(
    const DataTransfer* aDataTransfer, uint32_t aIndex,
    nsIPrincipal* aSourcePrincipal, const EditorDOMPoint& aDroppedAt,
    DeleteSelectedContent aDeleteSelectedContent, const Element& aEditingHost) {
  MOZ_ASSERT(GetEditAction() == EditAction::eDrop ||
             GetEditAction() == EditAction::ePaste);
  MOZ_ASSERT(mPlaceholderBatch,
             "HTMLEditor::InsertFromDataTransfer() should be called by "
             "HandleDropEvent() or paste action and there should've already "
             "been placeholder transaction");
  MOZ_ASSERT_IF(GetEditAction() == EditAction::eDrop, aDroppedAt.IsSet());

  ErrorResult error;
  RefPtr<DOMStringList> types = aDataTransfer->MozTypesAt(aIndex, error);
  if (error.Failed()) {
    NS_WARNING("DataTransfer::MozTypesAt() failed");
    return error.StealNSResult();
  }

  enum class FlavorType {
    NativeHTML,
    HTML,
    File,
    Image,
    URLData,
    Text,
    MozTextInternal,
    Unsupported,
  };
  AutoTArray<FlavorType, 8> flavors;
  for (const uint32_t i : IntegerRange(types->Length())) {
    nsAutoString type;
    types->Item(i, type);
    if (type.IsEmpty()) {
      flavors.AppendElement(FlavorType::Unsupported);
      continue;
    }
    if (StringBeginsWith(type, u"image/"_ns)) {
      if (type.EqualsLiteral(kJPEGImageMime) ||
          type.EqualsLiteral(kJPGImageMime) ||
          type.EqualsLiteral(kPNGImageMime) ||
          type.EqualsLiteral(kGIFImageMime)) {
        flavors.AppendElement(FlavorType::Image);
      } else {
        flavors.AppendElement(FlavorType::Unsupported);
      }
      continue;
    }
    if (StringBeginsWith(type, u"text/"_ns)) {
      if (type.EqualsLiteral(kHTMLMime)) {
        flavors.AppendElement(FlavorType::HTML);
      } else if (type.EqualsLiteral(kTextMime)) {
        flavors.AppendElement(FlavorType::Text);
      } else if (type.EqualsLiteral(kMozTextInternal)) {
        flavors.AppendElement(FlavorType::MozTextInternal);
      } else if (type.EqualsLiteral(kURLDataMime)) {
        flavors.AppendElement(FlavorType::URLData);
      } else {
        flavors.AppendElement(FlavorType::Unsupported);
      }
      continue;
    }
    if (type.EqualsLiteral(kFileMime)) {
      flavors.AppendElement(FlavorType::File);
    } else if (type.EqualsLiteral(kNativeHTMLMime)) {
      flavors.AppendElement(FlavorType::NativeHTML);
    } else {
      flavors.AppendElement(FlavorType::Unsupported);
    }
  }
  AutoTArray<uint32_t, 8> preferredIndices;
  {
    // The DataTransfer may be not created by AddDataFlavorsInBestOrder.
    // Therefore, we may need to reorder the flavors better.
    // NOTE: If you touch here, you need to maintain
    // HTMLTransferablePreparer::AddDataFlavorsInBestOrder() too.
    const auto AppendIndexOf = [&](FlavorType aFlavorType) {
      const auto index = flavors.IndexOf(aFlavorType);
      if (index != decltype(flavors)::NoIndex) {
        preferredIndices.AppendElement(index);
        return true;
      }
      return false;
    };
    if (IsStyleEditable(&aEditingHost)) {
      AppendIndexOf(FlavorType::NativeHTML);
      AppendIndexOf(FlavorType::HTML);
      AppendIndexOf(FlavorType::File);
      for (const uint32_t i : IntegerRange(flavors.Length())) {
        if (flavors[i] == FlavorType::Image) {
          preferredIndices.AppendElement(i);
        }
      }
    }
    for (const uint32_t i : IntegerRange(flavors.Length())) {
      if (flavors[i] == FlavorType::URLData || flavors[i] == FlavorType::Text ||
          flavors[i] == FlavorType::MozTextInternal) {
        preferredIndices.AppendElement(i);
        // We won't fallback to the others once we reach these types.
        break;
      }
    }
  }

  const SafeToInsertData safeToInsertData =
      IsSafeToInsertData(aSourcePrincipal);
  for (const uint32_t i : preferredIndices) {
    switch (flavors[i]) {
      case FlavorType::File:
      case FlavorType::Image: {
        nsAutoString type;
        types->Item(i, type);
        nsCOMPtr<nsIVariant> variant;
        DebugOnly<nsresult> rvIgnored = aDataTransfer->GetDataAtNoSecurityCheck(
            type, aIndex, getter_AddRefs(variant));
        if (!variant) {
          continue;
        }
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "DataTransfer::GetDataAtNoSecurityCheck() failed, but ignored");
        nsCOMPtr<nsISupports> object;
        rvIgnored = variant->GetAsISupports(getter_AddRefs(object));
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsIVariant::GetAsISupports() failed, but ignored");
        nsresult rv =
            InsertObject(NS_ConvertUTF16toUTF8(type), object, safeToInsertData,
                         aDroppedAt, aDeleteSelectedContent, aEditingHost);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "HTMLEditor::InsertObject() failed");
        return rv;
      }
      case FlavorType::NativeHTML: {
        // Windows only clipboard parsing.
        const nsDependentString kUTF16NativeHTMLMime(
            u"application/x-moz-nativehtml");
        MOZ_ASSERT(kUTF16NativeHTMLMime.EqualsLiteral(kNativeHTMLMime));
        nsAutoString text;
        GetStringFromDataTransfer(aDataTransfer, kUTF16NativeHTMLMime, aIndex,
                                  text);
        NS_ConvertUTF16toUTF8 cfhtml(text);

        nsString cfcontext, cffragment,
            cfselection;  // cfselection left emtpy for now

        nsresult rv = ParseCFHTML(cfhtml, getter_Copies(cffragment),
                                  getter_Copies(cfcontext));
        if (NS_FAILED(rv) || cffragment.IsEmpty()) {
          continue;
        }
        if (types->Contains(NS_LITERAL_STRING_FROM_CSTRING(kHTMLContext))) {
          // If we have our private HTML flavor, we will only use the fragment
          // from the CF_HTML. The rest comes from the clipboard.
          nsAutoString contextString, infoString;
          GetStringFromDataTransfer(
              aDataTransfer, NS_LITERAL_STRING_FROM_CSTRING(kHTMLContext),
              aIndex, contextString);
          GetStringFromDataTransfer(aDataTransfer,
                                    NS_LITERAL_STRING_FROM_CSTRING(kHTMLInfo),
                                    aIndex, infoString);
          AutoPlaceholderBatch treatAsOneTransaction(
              *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
          nsresult rv = InsertHTMLWithContextAsSubAction(
              cffragment, contextString, infoString, kUTF16NativeHTMLMime,
              safeToInsertData, aDroppedAt, aDeleteSelectedContent,
              InlineStylesAtInsertionPoint::Clear, aEditingHost);
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                               "HTMLEditor::InsertHTMLWithContextAsSubAction("
                               "InlineStylesAtInsertionPoint::Clear) failed");
          return rv;
        }
        AutoPlaceholderBatch treatAsOneTransaction(
            *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
        rv = InsertHTMLWithContextAsSubAction(
            cffragment, cfcontext, cfselection, kUTF16NativeHTMLMime,
            safeToInsertData, aDroppedAt, aDeleteSelectedContent,
            InlineStylesAtInsertionPoint::Clear, aEditingHost);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "HTMLEditor::InsertHTMLWithContextAsSubAction("
                             "InlineStylesAtInsertionPoint::Clear) failed");
        return rv;
      }
      case FlavorType::HTML: {
        const nsDependentString kUTF16HTMLMime(u"text/html");
        MOZ_ASSERT(kUTF16HTMLMime.EqualsLiteral(kHTMLMime));
        nsAutoString text, contextString, infoString;
        GetStringFromDataTransfer(aDataTransfer, kUTF16HTMLMime, aIndex, text);
        GetStringFromDataTransfer(aDataTransfer,
                                  NS_LITERAL_STRING_FROM_CSTRING(kHTMLContext),
                                  aIndex, contextString);
        GetStringFromDataTransfer(aDataTransfer,
                                  NS_LITERAL_STRING_FROM_CSTRING(kHTMLInfo),
                                  aIndex, infoString);
        AutoPlaceholderBatch treatAsOneTransaction(
            *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
        nsresult rv = InsertHTMLWithContextAsSubAction(
            text, contextString, infoString, kUTF16HTMLMime, safeToInsertData,
            aDroppedAt, aDeleteSelectedContent,
            InlineStylesAtInsertionPoint::Clear, aEditingHost);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "HTMLEditor::InsertHTMLWithContextAsSubAction("
                             "InlineStylesAtInsertionPoint::Clear) failed");
        return rv;
      }
      case FlavorType::URLData:
        if (IsStyleEditable(&aEditingHost)) {
          const nsDependentString kUTF16URLDataMime(u"text/x-moz-url-data");
          MOZ_ASSERT(kUTF16URLDataMime.EqualsLiteral(kURLDataMime));
          // Handle URL data before text so HTML editors insert a link here.
          nsAutoString url;
          GetStringFromDataTransfer(aDataTransfer, kUTF16URLDataMime, aIndex,
                                    url);
          AutoPlaceholderBatch treatAsOneTransaction(
              *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
          nsresult rv =
              InsertURLAsLinkInternal(url, aDroppedAt, aDeleteSelectedContent);
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "InsertURLAsLink() failed");
          return rv;
        }
        [[fallthrough]];
      case FlavorType::Text:
      case FlavorType::MozTextInternal: {
        nsAutoString type;
        types->Item(i, type);
        nsAutoString text;
        GetStringFromDataTransfer(aDataTransfer, type, aIndex, text);
        AutoPlaceholderBatch treatAsOneTransaction(
            *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
        nsresult rv = InsertTextAt(text, aDroppedAt, aDeleteSelectedContent);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::InsertTextAt() failed");
        return rv;
      }
      case FlavorType::Unsupported:
        continue;
    }
  }

  return NS_OK;
}

nsresult HTMLEditor::InsertURLAsLinkInternal(
    const nsAString& aURL, const EditorDOMPoint& aPointToInsert,
    DeleteSelectedContent aDeleteSelectedContent) {
  if (aURL.IsEmpty()) {
    return NS_OK;
  }
  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = PrepareToInsertContent(aPointToInsert, aDeleteSelectedContent);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::PrepareToInsertContent() failed");
    return rv;
  }
  // PrepareToInsertContent() has already updated the selection state
  // according to aDeleteSelectedContent, so DeleteSelectionAndCreateElement()
  // just creates the <a> element at the prepared insertion point.
  Result<RefPtr<Element>, nsresult> linkOrError =
      DeleteSelectionAndCreateElement(
          *nsGkAtoms::a, [&aURL](HTMLEditor& aEditor, Element& aNewElement,
                                 const EditorDOMPoint&) {
            nsresult rv =
                aNewElement.SetAttr(kNameSpaceID_None, nsGkAtoms::href, aURL,
                                    false, IsKnownNewAttr::Yes);
            if (NS_FAILED(rv)) {
              NS_WARNING("Element::SetAttr(href) failed");
              return rv;
            }
            RefPtr<nsTextNode> textNode = aEditor.CreateTextNode(aURL);
            if (NS_WARN_IF(!textNode)) {
              return NS_ERROR_FAILURE;
            }
            aNewElement.AppendChildTo(textNode, false, IgnoreErrors());
            return NS_OK;
          });
  if (MOZ_UNLIKELY(linkOrError.isErr())) {
    NS_WARNING("HTMLEditor::DeleteSelectionAndCreateElement() failed");
    return linkOrError.unwrapErr();
  }
  return NS_OK;
}

// static
HTMLEditor::HavePrivateHTMLFlavor
HTMLEditor::DataTransferOrClipboardHasPrivateHTMLFlavor(
    DataTransfer* aDataTransfer, nsIClipboard* aClipboard) {
  nsresult rv;
  if (aDataTransfer) {
    return aDataTransfer->HasPrivateHTMLFlavor() ? HavePrivateHTMLFlavor::Yes
                                                 : HavePrivateHTMLFlavor::No;
  }
  // otherwise, fall back to clipboard
  if (NS_WARN_IF(!aClipboard)) {
    return HavePrivateHTMLFlavor::No;
  }

  // check the clipboard for our special kHTMLContext flavor.  If that is there,
  // we know we have our own internal html format on clipboard.
  bool hasPrivateHTMLFlavor = false;
  AutoTArray<nsCString, 1> flavArray = {nsDependentCString(kHTMLContext)};
  rv = aClipboard->HasDataMatchingFlavors(
      flavArray, nsIClipboard::kGlobalClipboard, &hasPrivateHTMLFlavor);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsIClipboard::HasDataMatchingFlavors(nsIClipboard::"
                       "kGlobalClipboard) failed");
  return NS_SUCCEEDED(rv) && hasPrivateHTMLFlavor ? HavePrivateHTMLFlavor::Yes
                                                  : HavePrivateHTMLFlavor::No;
}

nsresult HTMLEditor::HandlePaste(AutoEditActionDataSetter& aEditActionData,
                                 nsIClipboard::ClipboardType aClipboardType,
                                 DataTransfer* aDataTransfer) {
  aEditActionData.InitializeDataTransferWithClipboard(
      SettingDataTransfer::eWithFormat, aDataTransfer, aClipboardType);
  nsresult rv = aEditActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent() failed");
    return rv;
  }
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }
  rv = PasteInternal(aClipboardType, aDataTransfer, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::PasteInternal() failed");
  return rv;
}

nsresult HTMLEditor::PasteInternal(nsIClipboard::ClipboardType aClipboardType,
                                   DataTransfer* aDataTransfer,
                                   const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (MOZ_UNLIKELY(!IsModifiable())) {
    return NS_OK;
  }

  // Get Clipboard Service
  nsresult rv = NS_OK;
  nsCOMPtr<nsIClipboard> clipboard =
      do_GetService("@mozilla.org/widget/clipboard;1", &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get nsIClipboard service");
    return rv;
  }

  // Get the nsITransferable interface for getting the data from the clipboard
  nsCOMPtr<nsITransferable> transferable;
  rv = PrepareHTMLTransferable(getter_AddRefs(transferable), &aEditingHost);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::PrepareHTMLTransferable() failed");
    return rv;
  }
  if (!transferable) {
    NS_WARNING("HTMLEditor::PrepareHTMLTransferable() returned nullptr");
    return NS_ERROR_FAILURE;
  }
  // Get the Data from the clipboard
  rv = GetDataFromDataTransferOrClipboard(aDataTransfer, transferable,
                                          aClipboardType);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::GetDataFromDataTransferOrClipboard() failed");
    return rv;
  }

  // also get additional html copy hints, if present
  nsAutoString contextStr, infoStr;

  // If we have our internal html flavor on the clipboard, there is special
  // context to use instead of cfhtml context.
  const HavePrivateHTMLFlavor clipboardHasPrivateHTMLFlavor =
      DataTransferOrClipboardHasPrivateHTMLFlavor(aDataTransfer, clipboard);
  if (clipboardHasPrivateHTMLFlavor == HavePrivateHTMLFlavor::Yes) {
    nsCOMPtr<nsITransferable> contextTransferable =
        do_CreateInstance("@mozilla.org/widget/transferable;1");
    if (!contextTransferable) {
      NS_WARNING(
          "do_CreateInstance() failed to create nsITransferable instance");
      return NS_ERROR_FAILURE;
    }
    DebugOnly<nsresult> rvIgnored = contextTransferable->Init(nullptr);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "nsITransferable::Init() failed, but ignored");
    contextTransferable->SetIsPrivateData(transferable->GetIsPrivateData());
    rvIgnored = contextTransferable->AddDataFlavor(kHTMLContext);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsITransferable::AddDataFlavor(kHTMLContext) failed, but ignored");
    GetDataFromDataTransferOrClipboard(aDataTransfer, contextTransferable,
                                       aClipboardType);
    nsCOMPtr<nsISupports> contextDataObj;
    rv = contextTransferable->GetTransferData(kHTMLContext,
                                              getter_AddRefs(contextDataObj));
    if (NS_SUCCEEDED(rv) && contextDataObj) {
      if (nsCOMPtr<nsISupportsString> str = do_QueryInterface(contextDataObj)) {
        DebugOnly<nsresult> rvIgnored = str->GetData(contextStr);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsISupportsString::GetData() failed, but ignored");
      }
    }

    nsCOMPtr<nsITransferable> infoTransferable =
        do_CreateInstance("@mozilla.org/widget/transferable;1");
    if (!infoTransferable) {
      NS_WARNING(
          "do_CreateInstance() failed to create nsITransferable instance");
      return NS_ERROR_FAILURE;
    }
    rvIgnored = infoTransferable->Init(nullptr);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "nsITransferable::Init() failed, but ignored");
    contextTransferable->SetIsPrivateData(transferable->GetIsPrivateData());
    rvIgnored = infoTransferable->AddDataFlavor(kHTMLInfo);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "nsITransferable::AddDataFlavor(kHTMLInfo) failed, but ignored");

    GetDataFromDataTransferOrClipboard(aDataTransfer, infoTransferable,
                                       aClipboardType);
    nsCOMPtr<nsISupports> infoDataObj;
    rv = infoTransferable->GetTransferData(kHTMLInfo,
                                           getter_AddRefs(infoDataObj));
    if (NS_SUCCEEDED(rv) && infoDataObj) {
      if (nsCOMPtr<nsISupportsString> str = do_QueryInterface(infoDataObj)) {
        DebugOnly<nsresult> rvIgnored = str->GetData(infoStr);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "nsISupportsString::GetData() failed, but ignored");
      }
    }
  }

  rv = InsertFromTransferableAtSelection(transferable, contextStr, infoStr,
                                         clipboardHasPrivateHTMLFlavor,
                                         aEditingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::InsertFromTransferableAtSelection() failed");
  return rv;
}

nsresult HTMLEditor::HandlePasteTransferable(
    AutoEditActionDataSetter& aEditActionData, nsITransferable& aTransferable,
    DataTransfer* aDataTransfer) {
  if (aDataTransfer) {
    // aDataTransfer was already created from aTransferable by the caller.
    // Reuse it, because creating another one would try to read the input
    // streams in aTransferable a second time.
    aEditActionData.InitializeDataTransfer(aDataTransfer);
  } else {
    // InitializeDataTransfer may fetch input stream in aTransferable, so it
    // may be invalid after calling this.
    aEditActionData.InitializeDataTransfer(&aTransferable);
  }

  nsresult rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return rv;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<DataTransfer> dataTransfer = GetInputEventDataTransfer();
  if (dataTransfer->HasFile() && dataTransfer->MozItemCount() > 0) {
    // Now aTransferable has moved to DataTransfer. Use DataTransfer.
    AutoPlaceholderBatch treatAsOneTransaction(
        *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

    rv = InsertFromDataTransfer(dataTransfer, 0, nullptr, EditorDOMPoint(),
                                DeleteSelectedContent::Yes, *editingHost);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::InsertFromDataTransfer("
                         "DeleteSelectedContent::Yes) failed");
    return rv;
  }

  nsAutoString contextStr, infoStr;
  rv = InsertFromTransferableAtSelection(&aTransferable, contextStr, infoStr,
                                         HavePrivateHTMLFlavor::No,
                                         *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertFromTransferableAtSelection("
                       "HavePrivateHTMLFlavor::No) failed");
  return rv;
}

nsresult HTMLEditor::HandlePasteNoFormatting(
    AutoEditActionDataSetter& aEditActionData,
    nsIClipboard::ClipboardType aClipboardType, DataTransfer* aDataTransfer) {
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  aEditActionData.InitializeDataTransferWithClipboard(
      SettingDataTransfer::eWithoutFormat, aDataTransfer, aClipboardType);
  // Dispatch "beforeinput" event after "paste" event.  And perhaps, before
  // committing composition because if pasting is canceled, we don't need to
  // commit the active composition.
  nsresult rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  if (NS_WARN_IF(Destroyed())) {
    return EditorBase::ToGenericNSResult(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CommitComposition() failed, but ignored");
  if (MOZ_UNLIKELY(!IsModifiable())) {
    return NS_OK;
  }

  Result<nsCOMPtr<nsITransferable>, nsresult> maybeTransferable =
      EditorUtils::CreateTransferableForPlainText(*GetDocument());
  if (maybeTransferable.isErr()) {
    NS_WARNING("EditorUtils::CreateTransferableForPlainText() failed");
    return EditorBase::ToGenericNSResult(maybeTransferable.unwrapErr());
  }
  nsCOMPtr<nsITransferable> transferable(maybeTransferable.unwrap());
  if (!transferable) {
    NS_WARNING(
        "EditorUtils::CreateTransferableForPlainText() returned nullptr, but "
        "ignored");
    return NS_OK;
  }
  rv = GetDataFromDataTransferOrClipboard(aDataTransfer, transferable,
                                          aClipboardType);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::GetDataFromDataTransferOrClipboard() failed");
    return rv;
  }

  rv = InsertFromTransferableAtSelection(
      transferable, u""_ns, u""_ns, HavePrivateHTMLFlavor::No, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertFromTransferableAtSelection("
                       "HavePrivateHTMLFlavor::No) failed");
  return rv;
}

// The following arrays contain the MIME types that we can paste. The arrays
// are used by CanPaste() and CanPasteTransferable() below.

static const char* textEditorFlavors[] = {kTextMime, kURLDataMime};
static const char* textHtmlEditorFlavors[] = {
    kURLDataMime,  kTextMime,     kHTMLMime,    kJPEGImageMime,
    kJPGImageMime, kPNGImageMime, kGIFImageMime};

bool HTMLEditor::CanPaste(nsIClipboard::ClipboardType aClipboardType) const {
  if (AreClipboardCommandsUnconditionallyEnabled()) {
    return true;
  }

  // can't paste if readonly
  if (!IsModifiable()) {
    return false;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost) {
    return false;
  }

  nsresult rv;
  nsCOMPtr<nsIClipboard> clipboard(
      do_GetService("@mozilla.org/widget/clipboard;1", &rv));
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get nsIClipboard service");
    return false;
  }

  // Use the flavors depending on the current editor mask
  if (!IsStyleEditable(editingHost)) {
    AutoTArray<nsCString, std::size(textEditorFlavors)> flavors;
    flavors.AppendElements<const char*>(Span<const char*>(textEditorFlavors));
    bool haveFlavors;
    nsresult rv = clipboard->HasDataMatchingFlavors(flavors, aClipboardType,
                                                    &haveFlavors);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "nsIClipboard::HasDataMatchingFlavors() failed");
    return NS_SUCCEEDED(rv) && haveFlavors;
  }

  AutoTArray<nsCString, std::size(textHtmlEditorFlavors)> flavors;
  flavors.AppendElements<const char*>(Span<const char*>(textHtmlEditorFlavors));
  bool haveFlavors;
  rv = clipboard->HasDataMatchingFlavors(flavors, aClipboardType, &haveFlavors);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsIClipboard::HasDataMatchingFlavors() failed");
  return NS_SUCCEEDED(rv) && haveFlavors;
}

bool HTMLEditor::CanPasteTransferable(nsITransferable* aTransferable) {
  // can't paste if readonly
  if (!IsModifiable()) {
    return false;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost) {
    return false;
  }

  // If |aTransferable| is null, assume that a paste will succeed.
  if (!aTransferable) {
    return true;
  }

  // Peek in |aTransferable| to see if it contains a supported MIME type.

  // Use the flavors depending on the current editor mask
  const char** flavors;
  size_t length;
  if (IsStyleEditable(editingHost)) {
    flavors = textHtmlEditorFlavors;
    length = std::size(textHtmlEditorFlavors);
  } else {
    flavors = textEditorFlavors;
    length = std::size(textEditorFlavors);
  }

  for (size_t i = 0; i < length; i++, flavors++) {
    nsCOMPtr<nsISupports> data;
    nsresult rv =
        aTransferable->GetTransferData(*flavors, getter_AddRefs(data));
    if (NS_SUCCEEDED(rv) && data) {
      return true;
    }
  }

  return false;
}

nsresult HTMLEditor::HandlePasteAsQuotation(
    AutoEditActionDataSetter& aEditActionData,
    nsIClipboard::ClipboardType aClipboardType, DataTransfer* aDataTransfer) {
  MOZ_ASSERT(aClipboardType == nsIClipboard::kGlobalClipboard ||
             aClipboardType == nsIClipboard::kSelectionClipboard);
  aEditActionData.InitializeDataTransferWithClipboard(
      SettingDataTransfer::eWithFormat, aDataTransfer, aClipboardType);
  if (NS_WARN_IF(!aEditActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return rv;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  if (!IsStyleEditable(editingHost)) {
    nsresult rv =
        PasteAsPlaintextQuotation(aClipboardType, aDataTransfer, *editingHost);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::PasteAsPlaintextQuotation() failed");
    return rv;
  }

  // If it's not in plain text edit mode, paste text into new
  // <blockquote type="cite"> element after removing selection.

  {
    // XXX Why don't we test these first?
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  UndefineCaretBidiLevel();

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertQuotation, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  rv = EnsureNoPaddingBRElementForEmptyEditor();
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

  // Remove Selection and create `<blockquote type="cite">` now.
  // XXX Why don't we insert the `<blockquote>` into the DOM tree after
  //     pasting the content in clipboard into it?
  Result<RefPtr<Element>, nsresult> blockquoteElementOrError =
      DeleteSelectionAndCreateElement(
          *nsGkAtoms::blockquote,
          // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
          [](HTMLEditor&, Element& aBlockquoteElement, const EditorDOMPoint&)
              MOZ_CAN_RUN_SCRIPT_BOUNDARY {
                DebugOnly<nsresult> rvIgnored = aBlockquoteElement.SetAttr(
                    kNameSpaceID_None, nsGkAtoms::type, u"cite"_ns,
                    aBlockquoteElement.IsInComposedDoc(), IsKnownNewAttr::Yes);
                NS_WARNING_ASSERTION(
                    NS_SUCCEEDED(rvIgnored),
                    nsPrintfCString(
                        "Element::SetAttr(nsGkAtoms::type, \"cite\", %s) "
                        "failed, but ignored",
                        aBlockquoteElement.IsInComposedDoc() ? "true" : "false")
                        .get());
                return NS_OK;
              });
  if (MOZ_UNLIKELY(blockquoteElementOrError.isErr()) ||
      NS_WARN_IF(Destroyed())) {
    NS_WARNING(
        "HTMLEditor::DeleteSelectionAndCreateElement(nsGkAtoms::blockquote) "
        "failed");
    return Destroyed() ? NS_ERROR_EDITOR_DESTROYED
                       : blockquoteElementOrError.unwrapErr();
  }
  MOZ_ASSERT(blockquoteElementOrError.inspect());

  // Collapse Selection in the new `<blockquote>` element.
  rv = CollapseSelectionToStartOf(
      MOZ_KnownLive(*blockquoteElementOrError.inspect()));
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionToStartOf() failed");
    return rv;
  }

  rv = PasteInternal(aClipboardType, aDataTransfer, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::PasteInternal() failed");
  return rv;
}

nsresult HTMLEditor::PasteAsPlaintextQuotation(
    nsIClipboard::ClipboardType aSelectionType, DataTransfer* aDataTransfer,
    const Element& aEditingHost) {
  nsresult rv;
  // Create generic Transferable for getting the data
  nsCOMPtr<nsITransferable> transferable =
      do_CreateInstance("@mozilla.org/widget/transferable;1", &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("do_CreateInstance() failed to create nsITransferable instance");
    return rv;
  }
  if (!transferable) {
    NS_WARNING("do_CreateInstance() returned nullptr");
    return NS_ERROR_FAILURE;
  }

  RefPtr<Document> destdoc = GetDocument();
  auto* windowContext = GetDocument()->GetWindowContext();
  if (!windowContext) {
    NS_WARNING("Editor didn't have document window context");
    return NS_ERROR_FAILURE;
  }

  nsILoadContext* loadContext = destdoc ? destdoc->GetLoadContext() : nullptr;
  DebugOnly<nsresult> rvIgnored = transferable->Init(loadContext);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "nsITransferable::Init() failed, but ignored");

  // We only handle plaintext pastes here
  rvIgnored = transferable->AddDataFlavor(kTextMime);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "nsITransferable::AddDataFlavor(kTextMime) failed, but ignored");

  // Get the Data from the clipboard
  GetDataFromDataTransferOrClipboard(aDataTransfer, transferable,
                                     aSelectionType);

  // Now we ask the transferable for the data
  // it still owns the data, we just have a pointer to it.
  // If it can't support a "text" output of the data the call will fail
  nsCOMPtr<nsISupports> genericDataObj;
  nsAutoCString flavor;
  rv = transferable->GetAnyTransferData(flavor, getter_AddRefs(genericDataObj));
  if (NS_FAILED(rv)) {
    NS_WARNING("nsITransferable::GetAnyTransferData() failed");
    return rv;
  }

  if (!flavor.EqualsLiteral(kTextMime)) {
    return NS_OK;
  }

  nsAutoString stuffToPaste;
  if (!GetString(genericDataObj, stuffToPaste)) {
    return NS_OK;
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertAsPlaintextQuotation(stuffToPaste, AddCites::Yes, aEditingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertAsPlaintextQuotation() failed");
  return rv;
}

nsresult HTMLEditor::InsertWithQuotationsAsSubAction(
    const nsAString& aQuotedText) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
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

  UndefineCaretBidiLevel();

  // Let the citer quote it for us:
  nsString quotedStuff;
  InternetCiter::GetCiteString(aQuotedText, quotedStuff);

  // It's best to put a blank line after the quoted text so that mails
  // written without thinking won't be so ugly.
  if (!aQuotedText.IsEmpty() &&
      (aQuotedText.Last() != HTMLEditUtils::kNewLine)) {
    quotedStuff.Append(HTMLEditUtils::kNewLine);
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertText, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "OnStartToHandleTopLevelEditSubAction() failed, but ignored");

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

  rv = InsertTextAsSubAction(quotedStuff, InsertTextFor::NormalText);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAsSubAction() failed");
  return rv;
}

NS_IMETHODIMP HTMLEditor::InsertTextWithQuotations(
    const nsAString& aStringToInsert) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eInsertText);
  MOZ_ASSERT(!aStringToInsert.IsVoid());
  editActionData.SetData(aStringToInsert);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (aStringToInsert.IsEmpty()) {
    return NS_OK;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  // The whole operation should be undoable in one transaction:
  // XXX Why isn't enough to use only AutoPlaceholderBatch here?
  AutoTransactionBatch bundleAllTransactions(*this, __FUNCTION__);
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  rv = InsertTextWithQuotationsInternal(aStringToInsert, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertTextWithQuotationsInternal() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::InsertTextWithQuotationsInternal(
    const nsAString& aStringToInsert, const Element& aEditingHost) {
  MOZ_ASSERT(!aStringToInsert.IsEmpty());
  // We're going to loop over the string, collecting up a "hunk"
  // that's all the same type (quoted or not),
  // Whenever the quotedness changes (or we reach the string's end)
  // we will insert the hunk all at once, quoted or non.
  static const char16_t cite('>');
  bool curHunkIsQuoted = (aStringToInsert.First() == cite);

  nsAString::const_iterator hunkStart, strEnd;
  aStringToInsert.BeginReading(hunkStart);
  aStringToInsert.EndReading(strEnd);

  // In the loop below, we only look for DOM newlines (\n),
  // because we don't have a FindChars method that can look
  // for both \r and \n.  \r is illegal in the dom anyway,
  // but in debug builds, let's take the time to verify that
  // there aren't any there:
#ifdef DEBUG
  nsAString::const_iterator dbgStart(hunkStart);
  if (FindCharInReadable(HTMLEditUtils::kCarriageReturn, dbgStart, strEnd)) {
    NS_ASSERTION(
        false,
        "Return characters in DOM! InsertTextWithQuotations may be wrong");
  }
#endif /* DEBUG */

  // Loop over lines:
  nsresult rv = NS_OK;
  nsAString::const_iterator lineStart(hunkStart);
  // We will break from inside when we run out of newlines.
  for (;;) {
    // Search for the end of this line (dom newlines, see above):
    bool found = FindCharInReadable(HTMLEditUtils::kNewLine, lineStart, strEnd);
    bool quoted = false;
    if (found) {
      // if there's another newline, lineStart now points there.
      // Loop over any consecutive newline chars:
      nsAString::const_iterator firstNewline(lineStart);
      while (*lineStart == HTMLEditUtils::kNewLine) {
        ++lineStart;
      }
      quoted = (*lineStart == cite);
      if (quoted == curHunkIsQuoted) {
        continue;
      }
      // else we're changing state, so we need to insert
      // from curHunk to lineStart then loop around.

      // But if the current hunk is quoted, then we want to make sure
      // that any extra newlines on the end do not get included in
      // the quoted section: blank lines flaking a quoted section
      // should be considered unquoted, so that if the user clicks
      // there and starts typing, the new text will be outside of
      // the quoted block.
      if (curHunkIsQuoted) {
        lineStart = firstNewline;

        // 'firstNewline' points to the first '\n'. We want to
        // ensure that this first newline goes into the hunk
        // since quoted hunks can be displayed as blocks
        // (and the newline should become invisible in this case).
        // So the next line needs to start at the next character.
        lineStart++;
      }
    }

    // If no newline found, lineStart is now strEnd and we can finish up,
    // inserting from curHunk to lineStart then returning.
    const nsAString& curHunk = Substring(hunkStart, lineStart);
    if (curHunkIsQuoted) {
      rv = InsertAsPlaintextQuotation(curHunk, AddCites::No, aEditingHost);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "HTMLEditor::InsertAsPlaintextQuotation() failed, "
                           "but might be ignored");
    } else {
      rv = InsertTextAsSubAction(curHunk, InsertTextFor::NormalText);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::InsertTextAsSubAction() failed, but might be ignored");
    }
    if (!found) {
      break;
    }
    curHunkIsQuoted = quoted;
    hunkStart = lineStart;
  }

  // XXX This returns the last result of InsertAsPlaintextQuotation() or
  //     InsertTextAsSubAction() in the loop.  This must be a bug.
  return rv;
}

nsresult HTMLEditor::InsertAsQuotation(const nsAString& aQuotedText,
                                       nsINode** aNodeInserted) {
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  if (!IsStyleEditable(editingHost)) {
    AutoEditActionDataSetter editActionData(*this, EditAction::eInsertText);
    MOZ_ASSERT(!aQuotedText.IsVoid());
    editActionData.SetData(aQuotedText);
    nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
    if (NS_FAILED(rv)) {
      NS_WARNING_ASSERTION(
          rv == NS_ERROR_EDITOR_ACTION_CANCELED,
          "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
      return EditorBase::ToGenericNSResult(rv);
    }
    AutoPlaceholderBatch treatAsOneTransaction(
        *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
    rv = InsertAsPlaintextQuotation(aQuotedText, AddCites::Yes, *editingHost,
                                    aNodeInserted);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::InsertAsPlaintextQuotation() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertBlockquoteElement);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  nsAutoString citation;
  rv = InsertAsCitedQuotationInternal(aQuotedText, citation, false,
                                      *editingHost, aNodeInserted);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertAsCitedQuotationInternal() failed");
  return EditorBase::ToGenericNSResult(rv);
}

// Insert plaintext as a quotation, with cite marks (e.g. "> ").
// This differs from its corresponding method in TextEditor
// in that here, quoted material is enclosed in a <pre> tag
// in order to preserve the original line wrapping.
nsresult HTMLEditor::InsertAsPlaintextQuotation(const nsAString& aQuotedText,
                                                AddCites aAddCites,
                                                const Element& aEditingHost,
                                                nsINode** aNodeInserted) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (aNodeInserted) {
    *aNodeInserted = nullptr;
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

  UndefineCaretBidiLevel();

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertQuotation, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
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

  RefPtr<Element> containerSpanElement;
  if (IsPlaintextMailComposer() || IsStyleEditable(&aEditingHost)) {
    // Wrap the inserted quote in a <span> so we can distinguish it. If we're
    // inserting into the <body>, we use a <span> which is displayed as a block
    // and sized to the screen using 98 viewport width units.
    // We could use 100vw, but 98vw avoids a horizontal scroll bar where
    // possible. All this is done to wrap overlong lines to the screen and not
    // to the container element, the width-restricted body.
    // XXX I think that we don't need to do this in the web.  This should be
    // done only for Thunderbird.
    Result<RefPtr<Element>, nsresult> spanElementOrError =
        DeleteSelectionAndCreateElement(
            *nsGkAtoms::span, [](HTMLEditor&, Element& aSpanElement,
                                 const EditorDOMPoint& aPointToInsert) {
              // Add an attribute on the pre node so we'll know it's a
              // quotation.
              DebugOnly<nsresult> rvIgnored = aSpanElement.SetAttr(
                  kNameSpaceID_None, nsGkAtoms::mozquote, u"true"_ns,
                  aSpanElement.IsInComposedDoc(), IsKnownNewAttr::Yes);
              NS_WARNING_ASSERTION(
                  NS_SUCCEEDED(rvIgnored),
                  nsPrintfCString(
                      "Element::SetAttr(nsGkAtoms::mozquote, \"true\", %s) "
                      "failed",
                      aSpanElement.IsInComposedDoc() ? "true" : "false")
                      .get());
              // Allow wrapping on spans so long lines get wrapped to the
              // screen.
              if (aPointToInsert.IsContainerHTMLElement(nsGkAtoms::body)) {
                DebugOnly<nsresult> rvIgnored = aSpanElement.SetAttr(
                    kNameSpaceID_None, nsGkAtoms::style,
                    nsLiteralString(u"white-space: pre-wrap; display: block; "
                                    u"width: 98vw;"),
                    false, IsKnownNewAttr::Yes);
                NS_WARNING_ASSERTION(
                    NS_SUCCEEDED(rvIgnored),
                    "Element::SetAttr(nsGkAtoms::style, \"pre-wrap, block\", "
                    "false) failed, but ignored");
              } else {
                DebugOnly<nsresult> rvIgnored = aSpanElement.SetAttr(
                    kNameSpaceID_None, nsGkAtoms::style,
                    u"white-space: pre-wrap;"_ns, false, IsKnownNewAttr::Yes);
                NS_WARNING_ASSERTION(
                    NS_SUCCEEDED(rvIgnored),
                    "Element::SetAttr(nsGkAtoms::style, "
                    "\"pre-wrap\", false) failed, but ignored");
              }
              return NS_OK;
            });
    if (MOZ_UNLIKELY(spanElementOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::DeleteSelectionAndCreateElement(nsGkAtoms::span) "
          "failed");
      return NS_OK;
    }
    // If this succeeded, then set selection inside the pre
    // so the inserted text will end up there.
    // If it failed, we don't care what the return value was,
    // but we'll fall through and try to insert the text anyway.
    MOZ_ASSERT(spanElementOrError.inspect());
    nsresult rv = CollapseSelectionToStartOf(
        MOZ_KnownLive(*spanElementOrError.inspect()));
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionToStartOf() caused destroying the "
          "editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionToStartOf() failed, but ignored");
    containerSpanElement = spanElementOrError.unwrap();
  }

  // TODO: We should insert text at specific point rather than at selection.
  //       Then, we can do this before inserting the <span> element.
  if (aAddCites == AddCites::Yes) {
    nsresult rv = InsertWithQuotationsAsSubAction(aQuotedText);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::InsertWithQuotationsAsSubAction() failed");
      return rv;
    }
  } else {
    nsresult rv = InsertTextAsSubAction(aQuotedText, InsertTextFor::NormalText);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::InsertTextAsSubAction() failed");
      return rv;
    }
  }

  // Set the selection to after the <span> if and only if we wrap the text into
  // it.
  if (containerSpanElement) {
    // If we inserted the quotation into the new span, it may have a padding
    // line break at last. However, we don't want it because the user does not
    // intent to modify the empty line in the quotation block.
    // FIXME: We need a way to make HandleInsertText() not to insert a padding
    // line break for the last empty line to save the footprint.
    if (const RefPtr<HTMLBRElement> brElement = HTMLBRElement::FromNodeOrNull(
            containerSpanElement->GetLastChild())) {
      if (brElement->IsPaddingForEmptyLastLine()) {
        nsresult rv = DeleteNodeWithTransaction(*brElement);
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return rv;
        }
      }
    }
    EditorRawDOMPoint afterNewSpanElement(
        EditorRawDOMPoint::After(*containerSpanElement));
    NS_WARNING_ASSERTION(
        afterNewSpanElement.IsSet(),
        "Failed to set after the new <span> element, but ignored");
    if (afterNewSpanElement.IsSet()) {
      nsresult rv = CollapseSelectionTo(afterNewSpanElement);
      if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
        NS_WARNING(
            "EditorBase::CollapseSelectionTo() caused destroying the editor");
        return NS_ERROR_EDITOR_DESTROYED;
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }

    // Note that if !aAddCites, aNodeInserted isn't set.
    // That's okay because the routines that use aAddCites
    // don't need to know the inserted node.
    if (aNodeInserted) {
      containerSpanElement.forget(aNodeInserted);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP HTMLEditor::Rewrap(bool aRespectNewlines) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eRewrap);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  // Rewrap makes no sense if there's no wrap column; default to 72.
  int32_t wrapWidth = WrapWidth();
  if (wrapWidth <= 0) {
    wrapWidth = 72;
  }

  nsAutoString current;
  const bool isCollapsed = SelectionRef().IsCollapsed();
  uint32_t flags = nsIDocumentEncoder::OutputFormatted |
                   nsIDocumentEncoder::OutputLFLineBreak;
  if (!isCollapsed) {
    flags |= nsIDocumentEncoder::OutputSelectionOnly;
  }
  rv = ComputeValueInternal(u"text/plain"_ns, flags, current);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::ComputeValueInternal(text/plain) failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (current.IsEmpty()) {
    return NS_OK;
  }

  nsString wrapped;
  uint32_t firstLineOffset = 0;  // XXX need to reset this if there is a
                                 //     selection
  InternetCiter::Rewrap(current, wrapWidth, firstLineOffset, aRespectNewlines,
                        wrapped);

  if (wrapped.IsEmpty()) {
    return NS_OK;
  }

  if (isCollapsed) {
    DebugOnly<nsresult> rvIgnored = SelectAllInternal();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "HTMLEditor::SelectAllInternal() failed");
  }

  // The whole operation in InsertTextWithQuotationsInternal() should be
  // undoable in one transaction.
  // XXX Why isn't enough to use only AutoPlaceholderBatch here?
  AutoTransactionBatch bundleAllTransactions(*this, __FUNCTION__);
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertTextWithQuotationsInternal(wrapped, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertTextWithQuotationsInternal() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::InsertAsCitedQuotation(const nsAString& aQuotedText,
                                                 const nsAString& aCitation,
                                                 bool aInsertHTML,
                                                 nsINode** aNodeInserted) {
  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }

  // Don't let anyone insert HTML when we're in plaintext mode.
  if (!IsStyleEditable(editingHost)) {
    NS_ASSERTION(
        !aInsertHTML,
        "InsertAsCitedQuotation: trying to insert html into plaintext editor");

    AutoEditActionDataSetter editActionData(*this, EditAction::eInsertText);
    MOZ_ASSERT(!aQuotedText.IsVoid());
    editActionData.SetData(aQuotedText);
    nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
    if (NS_FAILED(rv)) {
      NS_WARNING_ASSERTION(
          rv == NS_ERROR_EDITOR_ACTION_CANCELED,
          "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
      return EditorBase::ToGenericNSResult(rv);
    }

    AutoPlaceholderBatch treatAsOneTransaction(
        *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
    rv = InsertAsPlaintextQuotation(aQuotedText, AddCites::Yes, *editingHost,
                                    aNodeInserted);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::InsertAsPlaintextQuotation() failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoEditActionDataSetter editActionData(*this,
                                          EditAction::eInsertBlockquoteElement);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  rv = InsertAsCitedQuotationInternal(aQuotedText, aCitation, aInsertHTML,
                                      *editingHost, aNodeInserted);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertAsCitedQuotationInternal() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::InsertAsCitedQuotationInternal(
    const nsAString& aQuotedText, const nsAString& aCitation, bool aInsertHTML,
    const Element& aEditingHost, nsINode** aNodeInserted) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsPlaintextMailComposer());

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

  UndefineCaretBidiLevel();

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertQuotation, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  nsresult rv = EnsureNoPaddingBRElementForEmptyEditor();
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::EnsureNoPaddingBRElementForEmptyEditor() "
                       "failed, but ignored");

  if (NS_SUCCEEDED(rv) && SelectionRef().IsCollapsed()) {
    nsresult rv = EnsureCaretNotAfterInvisibleBRElement(aEditingHost);
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

  Result<RefPtr<Element>, nsresult> blockquoteElementOrError =
      DeleteSelectionAndCreateElement(
          *nsGkAtoms::blockquote,
          // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
          [&aCitation](HTMLEditor&, Element& aBlockquoteElement,
                       const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
            // Try to set type=cite.  Ignore it if this fails.
            DebugOnly<nsresult> rvIgnored = aBlockquoteElement.SetAttr(
                kNameSpaceID_None, nsGkAtoms::type, u"cite"_ns,
                aBlockquoteElement.IsInComposedDoc(), IsKnownNewAttr::Yes);
            NS_WARNING_ASSERTION(
                NS_SUCCEEDED(rvIgnored),
                nsPrintfCString(
                    "Element::SetAttr(nsGkAtoms::type, \"cite\", %s) failed, "
                    "but ignored",
                    aBlockquoteElement.IsInComposedDoc() ? "true" : "false")
                    .get());
            if (!aCitation.IsEmpty()) {
              DebugOnly<nsresult> rvIgnored = aBlockquoteElement.SetAttr(
                  kNameSpaceID_None, nsGkAtoms::cite, aCitation,
                  aBlockquoteElement.IsInComposedDoc(), IsKnownNewAttr::Yes);
              NS_WARNING_ASSERTION(
                  NS_SUCCEEDED(rvIgnored),
                  nsPrintfCString(
                      "Element::SetAttr(nsGkAtoms::cite, \"...\", %s) failed, "
                      "but ignored",
                      aBlockquoteElement.IsInComposedDoc() ? "true" : "false")
                      .get());
            }
            return NS_OK;
          });
  if (MOZ_UNLIKELY(blockquoteElementOrError.isErr() ||
                   NS_WARN_IF(Destroyed()))) {
    NS_WARNING(
        "HTMLEditor::DeleteSelectionAndCreateElement(nsGkAtoms::blockquote) "
        "failed");
    return Destroyed() ? NS_ERROR_EDITOR_DESTROYED
                       : blockquoteElementOrError.unwrapErr();
  }
  MOZ_ASSERT(blockquoteElementOrError.inspect());

  // Set the selection inside the blockquote so aQuotedText will go there:
  rv = CollapseSelectionTo(
      EditorRawDOMPoint(blockquoteElementOrError.inspect(), 0u));
  if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
    NS_WARNING(
        "EditorBase::CollapseSelectionTo() caused destroying the editor");
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::CollapseSelectionTo() failed, but ignored");

  // TODO: We should insert text at specific point rather than at selection.
  //       Then, we can do this before inserting the <blockquote> element.
  if (aInsertHTML) {
    rv = LoadHTML(aQuotedText);
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::LoadHTML() failed");
      return rv;
    }
  } else {
    rv = InsertTextAsSubAction(
        aQuotedText, InsertTextFor::NormalText);  // XXX ignore charset
    if (NS_WARN_IF(Destroyed())) {
      return NS_ERROR_EDITOR_DESTROYED;
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::LoadHTML() failed");
      return rv;
    }
  }

  // Set the selection to just after the inserted node:
  EditorRawDOMPoint afterNewBlockquoteElement(
      EditorRawDOMPoint::After(blockquoteElementOrError.inspect()));
  NS_WARNING_ASSERTION(
      afterNewBlockquoteElement.IsSet(),
      "Failed to set after new <blockquote> element, but ignored");
  if (afterNewBlockquoteElement.IsSet()) {
    nsresult rv = CollapseSelectionTo(afterNewBlockquoteElement);
    if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
      NS_WARNING(
          "EditorBase::CollapseSelectionTo() caused destroying the editor");
      return NS_ERROR_EDITOR_DESTROYED;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "EditorBase::CollapseSelectionTo() failed, but ignored");
  }

  if (aNodeInserted) {
    blockquoteElementOrError.unwrap().forget(aNodeInserted);
  }

  return NS_OK;
}

void HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    RemoveHeadChildAndStealBodyChildsChildren(nsINode& aNode) {
  nsCOMPtr<nsIContent> body, head;
  // find the body and head nodes if any.
  // look only at immediate children of aNode.
  for (nsCOMPtr<nsIContent> child = aNode.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsHTMLElement(nsGkAtoms::body)) {
      body = child;
    } else if (child->IsHTMLElement(nsGkAtoms::head)) {
      head = child;
    }
  }
  if (head) {
    ErrorResult ignored;
    aNode.RemoveChild(*head, ignored);
  }
  if (body) {
    nsCOMPtr<nsIContent> child = body->GetFirstChild();
    while (child) {
      ErrorResult ignored;
      aNode.InsertBefore(*child, body, ignored);
      child = body->GetFirstChild();
    }

    ErrorResult ignored;
    aNode.RemoveChild(*body, ignored);
  }
}

// static
void HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    RemoveIncompleteDescendantsFromInsertingFragment(nsINode& aNode) {
  nsIContent* child = aNode.GetFirstChild();
  while (child) {
    bool isEmptyNodeShouldNotInserted = false;
    if (HTMLEditUtils::IsListElement(*child)) {
      // Current limitation of HTMLEditor:
      //   Cannot put caret in a list element which does not have list item
      //   element even as a descendant.  I.e., HTMLEditor does not support
      //   editing in such empty list element, and does not support to delete
      //   it from outside.  Therefore, HTMLWithContextInserter should not
      //   insert empty list element.
      isEmptyNodeShouldNotInserted = HTMLEditUtils::IsEmptyNode(
          *child,
          {
              // Although we don't check relation between list item element
              // and parent list element, but it should not be a problem in the
              // wild because appearing such invalid list element is an edge
              // case and anyway HTMLEditor supports editing in them.
              EmptyCheckOption::TreatListItemAsVisible,
              // A non-editable list item element may make the list element
              // visible.  Although HTMLEditor does not support to edit list
              // elements which have only non-editable list item elements, but
              // it should be deleted from outside.  Therefore, don't treat
              // non-editable things as invisible.
              // TODO: Currently, HTMLEditor does not support deleting such list
              //       element with Backspace.  We should fix this issue.
          });
    }
    // TODO: Perhaps, we should delete <table>s if they have no <td>/<th>
    //       element, or something other elements which must have specific
    //       children but they don't.
    if (isEmptyNodeShouldNotInserted) {
      nsIContent* nextChild = child->GetNextSibling();
      OwningNonNull<nsIContent> removingChild(*child);
      removingChild->Remove();
      child = nextChild;
      continue;
    }
    if (child->HasChildNodes()) {
      RemoveIncompleteDescendantsFromInsertingFragment(*child);
    }
    child = child->GetNextSibling();
  }
}

// static
bool HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    IsInsertionCookie(const nsIContent& aContent) {
  // Is this child the magical cookie?
  if (const auto* comment = Comment::FromNode(&aContent)) {
    nsAutoString data;
    comment->GetData(data);

    return data.EqualsLiteral(kInsertCookie);
  }

  return false;
}

/**
 * This function finds the target node that we will be pasting into. aStart is
 * the context that we're given and aResult will be the target. Initially,
 * *aResult must be nullptr.
 *
 * The target for a paste is found by either finding the node that contains
 * the magical comment node containing kInsertCookie or, failing that, the
 * firstChild of the firstChild (until we reach a leaf).
 */
bool HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    FindTargetNodeOfContextForPastedHTMLAndRemoveInsertionCookie(
        nsINode& aStart, nsCOMPtr<nsINode>& aResult) {
  nsIContent* firstChild = aStart.GetFirstChild();
  if (!firstChild) {
    // If the current result is nullptr, then aStart is a leaf, and is the
    // fallback result.
    if (!aResult) {
      aResult = &aStart;
    }
    return false;
  }

  for (nsCOMPtr<nsIContent> child = firstChild; child;
       child = child->GetNextSibling()) {
    if (FragmentFromPasteCreator::IsInsertionCookie(*child)) {
      // Yes it is! Return an error so we bubble out and short-circuit the
      // search.
      aResult = &aStart;

      child->Remove();

      return true;
    }

    if (FindTargetNodeOfContextForPastedHTMLAndRemoveInsertionCookie(*child,
                                                                     aResult)) {
      return true;
    }
  }

  return false;
}

class MOZ_STACK_CLASS
HTMLEditor::HTMLWithContextInserter::FragmentParser final {
 public:
  FragmentParser(const Document& aDocument, SafeToInsertData aSafeToInsertData);

  [[nodiscard]] nsresult ParseContext(const nsAString& aContextString,
                                      DocumentFragment** aFragment);

  [[nodiscard]] nsresult ParsePastedHTML(const nsAString& aInputString,
                                         nsAtom* aContextLocalNameAtom,
                                         DocumentFragment** aFragment);

 private:
  static nsresult ParseFragment(const nsAString& aStr,
                                nsAtom* aContextLocalName,
                                const Document* aTargetDoc,
                                dom::DocumentFragment** aFragment,
                                SafeToInsertData aSafeToInsertData);

  const Document& mDocument;
  const SafeToInsertData mSafeToInsertData;
};

HTMLEditor::HTMLWithContextInserter::FragmentParser::FragmentParser(
    const Document& aDocument, SafeToInsertData aSafeToInsertData)
    : mDocument{aDocument}, mSafeToInsertData{aSafeToInsertData} {}

nsresult HTMLEditor::HTMLWithContextInserter::FragmentParser::ParseContext(
    const nsAString& aContextStr, DocumentFragment** aFragment) {
  return FragmentParser::ParseFragment(aContextStr, nullptr, &mDocument,
                                       aFragment, mSafeToInsertData);
}

nsresult HTMLEditor::HTMLWithContextInserter::FragmentParser::ParsePastedHTML(
    const nsAString& aInputString, nsAtom* aContextLocalNameAtom,
    DocumentFragment** aFragment) {
  return FragmentParser::ParseFragment(aInputString, aContextLocalNameAtom,
                                       &mDocument, aFragment,
                                       mSafeToInsertData);
}

nsresult HTMLEditor::HTMLWithContextInserter::CreateDOMFragmentFromPaste(
    const nsAString& aInputString, const nsAString& aContextStr,
    const nsAString& aInfoStr, nsCOMPtr<nsINode>* aOutFragNode,
    nsCOMPtr<nsINode>* aOutStartNode, nsCOMPtr<nsINode>* aOutEndNode,
    uint32_t* aOutStartOffset, uint32_t* aOutEndOffset,
    SafeToInsertData aSafeToInsertData) const {
  if (NS_WARN_IF(!aOutFragNode) || NS_WARN_IF(!aOutStartNode) ||
      NS_WARN_IF(!aOutEndNode) || NS_WARN_IF(!aOutStartOffset) ||
      NS_WARN_IF(!aOutEndOffset)) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<const Document> document = mHTMLEditor.GetDocument();
  if (NS_WARN_IF(!document)) {
    return NS_ERROR_FAILURE;
  }

  FragmentFromPasteCreator fragmentFromPasteCreator;

  const nsresult rv = fragmentFromPasteCreator.Run(
      *document, aInputString, aContextStr, aInfoStr, aOutFragNode,
      aOutStartNode, aOutEndNode, aSafeToInsertData);

  *aOutStartOffset = 0;
  *aOutEndOffset = (*aOutEndNode)->Length();

  return rv;
}

// static
nsAtom* HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    DetermineContextLocalNameForParsingPastedHTML(
        const nsIContent* aParentContentOfPastedHTMLInContext) {
  if (!aParentContentOfPastedHTMLInContext) {
    return nsGkAtoms::body;
  }

  nsAtom* contextLocalNameAtom =
      aParentContentOfPastedHTMLInContext->NodeInfo()->NameAtom();

  return (aParentContentOfPastedHTMLInContext->IsHTMLElement(nsGkAtoms::html))
             ? nsGkAtoms::body
             : contextLocalNameAtom;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    MergeAndPostProcessFragmentsForPastedHTMLAndContext(
        DocumentFragment& aDocumentFragmentForPastedHTML,
        DocumentFragment& aDocumentFragmentForContext,
        nsIContent& aTargetContentOfContextForPastedHTML) {
  FragmentFromPasteCreator::RemoveHeadChildAndStealBodyChildsChildren(
      aDocumentFragmentForPastedHTML);

  FragmentFromPasteCreator::RemoveIncompleteDescendantsFromInsertingFragment(
      aDocumentFragmentForPastedHTML);

  // unite the two trees
  IgnoredErrorResult ignoredError;
  aTargetContentOfContextForPastedHTML.AppendChild(
      aDocumentFragmentForPastedHTML, ignoredError);
  NS_WARNING_ASSERTION(!ignoredError.Failed(),
                       "nsINode::AppendChild() failed, but ignored");
  const nsresult rv = FragmentFromPasteCreator::
      RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
          aDocumentFragmentForContext, NodesToRemove::eOnlyListItems);

  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
        "RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces()"
        " failed");
    return rv;
  }

  return rv;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    PostProcessFragmentForPastedHTMLWithoutContext(
        DocumentFragment& aDocumentFragmentForPastedHTML) {
  FragmentFromPasteCreator::RemoveHeadChildAndStealBodyChildsChildren(
      aDocumentFragmentForPastedHTML);

  FragmentFromPasteCreator::RemoveIncompleteDescendantsFromInsertingFragment(
      aDocumentFragmentForPastedHTML);

  const nsresult rv = FragmentFromPasteCreator::
      RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
          aDocumentFragmentForPastedHTML, NodesToRemove::eOnlyListItems);

  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
        "RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces() "
        "failed");
    return rv;
  }

  return rv;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    PreProcessContextDocumentFragmentForMerging(
        DocumentFragment& aDocumentFragmentForContext) {
  // The context is expected to contain text nodes only in block level
  // elements. Hence, if they contain only whitespace, they're invisible.
  const nsresult rv = FragmentFromPasteCreator::
      RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces(
          aDocumentFragmentForContext, NodesToRemove::eAll);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
        "RemoveNonPreWhiteSpaceOnlyTextNodesForIgnoringInvisibleWhiteSpaces() "
        "failed");
    return rv;
  }

  FragmentFromPasteCreator::RemoveHeadChildAndStealBodyChildsChildren(
      aDocumentFragmentForContext);

  return rv;
}

nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    CreateDocumentFragmentAndGetParentOfPastedHTMLInContext(
        const Document& aDocument, const nsAString& aInputString,
        const nsAString& aContextStr, SafeToInsertData aSafeToInsertData,
        nsCOMPtr<nsINode>& aParentNodeOfPastedHTMLInContext,
        RefPtr<DocumentFragment>& aDocumentFragmentToInsert) const {
  // if we have context info, create a fragment for that
  RefPtr<DocumentFragment> documentFragmentForContext;

  FragmentParser fragmentParser{aDocument, aSafeToInsertData};
  if (!aContextStr.IsEmpty()) {
    nsresult rv = fragmentParser.ParseContext(
        aContextStr, getter_AddRefs(documentFragmentForContext));
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentParser::ParseContext() "
          "failed");
      return rv;
    }
    if (!documentFragmentForContext) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentParser::ParseContext() "
          "returned nullptr");
      return NS_ERROR_FAILURE;
    }

    rv = FragmentFromPasteCreator::PreProcessContextDocumentFragmentForMerging(
        *documentFragmentForContext);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
          "PreProcessContextDocumentFragmentForMerging() failed.");
      return rv;
    }

    FragmentFromPasteCreator::
        FindTargetNodeOfContextForPastedHTMLAndRemoveInsertionCookie(
            *documentFragmentForContext, aParentNodeOfPastedHTMLInContext);
    MOZ_ASSERT(aParentNodeOfPastedHTMLInContext);
  }

  nsCOMPtr<nsIContent> parentContentOfPastedHTMLInContext =
      nsIContent::FromNodeOrNull(aParentNodeOfPastedHTMLInContext);
  MOZ_ASSERT_IF(aParentNodeOfPastedHTMLInContext,
                parentContentOfPastedHTMLInContext);

  nsAtom* contextLocalNameAtom =
      FragmentFromPasteCreator::DetermineContextLocalNameForParsingPastedHTML(
          parentContentOfPastedHTMLInContext);
  RefPtr<DocumentFragment> documentFragmentForPastedHTML;
  nsresult rv = fragmentParser.ParsePastedHTML(
      aInputString, contextLocalNameAtom,
      getter_AddRefs(documentFragmentForPastedHTML));
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentParser::ParsePastedHTML()"
        " failed");
    return rv;
  }
  if (!documentFragmentForPastedHTML) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentParser::ParsePastedHTML()"
        " returned nullptr");
    return NS_ERROR_FAILURE;
  }

  if (aParentNodeOfPastedHTMLInContext) {
    const nsresult rv = FragmentFromPasteCreator::
        MergeAndPostProcessFragmentsForPastedHTMLAndContext(
            *documentFragmentForPastedHTML, *documentFragmentForContext,
            *parentContentOfPastedHTMLInContext);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
          "MergeAndPostProcessFragmentsForPastedHTMLAndContext() failed.");
      return rv;
    }
    aDocumentFragmentToInsert = std::move(documentFragmentForContext);
  } else {
    const nsresult rv = PostProcessFragmentForPastedHTMLWithoutContext(
        *documentFragmentForPastedHTML);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
          "PostProcessFragmentForPastedHTMLWithoutContext() failed.");
      return rv;
    }

    aDocumentFragmentToInsert = std::move(documentFragmentForPastedHTML);
  }

  return rv;
}

nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::Run(
    const Document& aDocument, const nsAString& aInputString,
    const nsAString& aContextStr, const nsAString& aInfoStr,
    nsCOMPtr<nsINode>* aOutFragNode, nsCOMPtr<nsINode>* aOutStartNode,
    nsCOMPtr<nsINode>* aOutEndNode, SafeToInsertData aSafeToInsertData) const {
  MOZ_ASSERT(aOutFragNode);
  MOZ_ASSERT(aOutStartNode);
  MOZ_ASSERT(aOutEndNode);

  nsCOMPtr<nsINode> parentNodeOfPastedHTMLInContext;
  RefPtr<DocumentFragment> documentFragmentToInsert;
  nsresult rv = CreateDocumentFragmentAndGetParentOfPastedHTMLInContext(
      aDocument, aInputString, aContextStr, aSafeToInsertData,
      parentNodeOfPastedHTMLInContext, documentFragmentToInsert);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
        "CreateDocumentFragmentAndGetParentOfPastedHTMLInContext() failed.");
    return rv;
  }

  // If there was no context, then treat all of the data we did get as the
  // pasted data.
  if (parentNodeOfPastedHTMLInContext) {
    *aOutEndNode = *aOutStartNode = parentNodeOfPastedHTMLInContext;
  } else {
    *aOutEndNode = *aOutStartNode = documentFragmentToInsert;
  }

  *aOutFragNode = std::move(documentFragmentToInsert);

  if (!aInfoStr.IsEmpty()) {
    const nsresult rv =
        FragmentFromPasteCreator::MoveStartAndEndAccordingToHTMLInfo(
            aInfoStr, aOutStartNode, aOutEndNode);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::"
          "MoveStartAndEndAccordingToHTMLInfo() failed");
      return rv;
    }
  }

  return NS_OK;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentFromPasteCreator::
    MoveStartAndEndAccordingToHTMLInfo(const nsAString& aInfoStr,
                                       nsCOMPtr<nsINode>* aOutStartNode,
                                       nsCOMPtr<nsINode>* aOutEndNode) {
  int32_t sep = aInfoStr.FindChar((char16_t)',');
  nsAutoString numstr1(Substring(aInfoStr, 0, sep));
  nsAutoString numstr2(
      Substring(aInfoStr, sep + 1, aInfoStr.Length() - (sep + 1)));

  // Move the start and end children.
  nsresult rvIgnored;
  int32_t num = numstr1.ToInteger(&rvIgnored);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "nsAString::ToInteger() failed, but ignored");
  while (num--) {
    nsINode* tmp = (*aOutStartNode)->GetFirstChild();
    if (!tmp) {
      NS_WARNING("aOutStartNode did not have children");
      return NS_ERROR_FAILURE;
    }
    *aOutStartNode = tmp;
  }

  num = numstr2.ToInteger(&rvIgnored);
  while (num--) {
    nsINode* tmp = (*aOutEndNode)->GetLastChild();
    if (!tmp) {
      NS_WARNING("aOutEndNode did not have children");
      return NS_ERROR_FAILURE;
    }
    *aOutEndNode = tmp;
  }

  return NS_OK;
}

// static
nsresult HTMLEditor::HTMLWithContextInserter::FragmentParser::ParseFragment(
    const nsAString& aFragStr, nsAtom* aContextLocalName,
    const Document* aTargetDocument, DocumentFragment** aFragment,
    SafeToInsertData aSafeToInsertData) {
  nsAutoScriptBlockerSuppressNodeRemoved autoBlocker;

  nsCOMPtr<Document> doc =
      nsContentUtils::CreateInertHTMLDocument(aTargetDocument);
  if (!doc) {
    return NS_ERROR_FAILURE;
  }
  RefPtr<DocumentFragment> fragment =
      new (doc->NodeInfoManager()) DocumentFragment(doc->NodeInfoManager());
  nsresult rv = nsContentUtils::ParseFragmentHTML(
      aFragStr, fragment,
      aContextLocalName ? aContextLocalName : nsGkAtoms::body,
      kNameSpaceID_XHTML, false, true,
      nsContentUtils::kParseFragmentPrivilegedDefaultSanitization,
      mozilla::Nothing());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsContentUtils::ParseFragmentHTML() failed");
  if (aSafeToInsertData == SafeToInsertData::No) {
    nsTreeSanitizer sanitizer(aContextLocalName
                                  ? nsIParserUtils::SanitizerAllowStyle
                                  : nsIParserUtils::SanitizerAllowComments);
    sanitizer.Sanitize(fragment);
  }
  fragment.forget(aFragment);
  return rv;
}

// static
void HTMLEditor::HTMLWithContextInserter::
    CollectTopMostChildContentsCompletelyInRange(
        const EditorRawDOMPoint& aStartPoint,
        const EditorRawDOMPoint& aEndPoint,
        nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents) {
  MOZ_ASSERT(aStartPoint.IsSetAndValid());
  MOZ_ASSERT(aEndPoint.IsSetAndValid());

  RefPtr<dom::Range> range =
      dom::Range::Create(aStartPoint.ToRawRangeBoundary(),
                         aEndPoint.ToRawRangeBoundary(), IgnoreErrors());
  if (!range) {
    NS_WARNING("Range::Create() failed");
    return;
  }
  DOMSubtreeIterator iter;
  if (NS_FAILED(iter.Init(*range))) {
    NS_WARNING("DOMSubtreeIterator::Init() failed, but ignored");
    return;
  }

  iter.AppendAllNodesToArray(aOutArrayOfContents);
}

/******************************************************************************
 * HTMLEditor::AutoHTMLFragmentBoundariesFixer
 ******************************************************************************/

HTMLEditor::AutoHTMLFragmentBoundariesFixer::AutoHTMLFragmentBoundariesFixer(
    nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents) {
  EnsureBeginsOrEndsWithValidContent(StartOrEnd::start,
                                     aArrayOfTopMostChildContents);
  EnsureBeginsOrEndsWithValidContent(StartOrEnd::end,
                                     aArrayOfTopMostChildContents);
}

// static
void HTMLEditor::AutoHTMLFragmentBoundariesFixer::
    CollectTableAndAnyListElementsOfInclusiveAncestorsAt(
        nsIContent& aContent,
        nsTArray<OwningNonNull<Element>>& aOutArrayOfListAndTableElements) {
  for (Element* element = aContent.GetAsElementOrParentElement(); element;
       element = element->GetParentElement()) {
    if (HTMLEditUtils::IsListElement(*element) ||
        element->IsHTMLElement(nsGkAtoms::table)) {
      aOutArrayOfListAndTableElements.AppendElement(*element);
    }
  }
}

// static
Element* HTMLEditor::AutoHTMLFragmentBoundariesFixer::
    GetMostDistantAncestorListOrTableElement(
        const nsTArray<OwningNonNull<nsIContent>>& aArrayOfTopMostChildContents,
        const nsTArray<OwningNonNull<Element>>&
            aInclusiveAncestorsTableOrListElements) {
  Element* lastFoundAncestorListOrTableElement = nullptr;
  for (const OwningNonNull<nsIContent>& content :
       aArrayOfTopMostChildContents) {
    if (HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
            content)) {
      Element* tableElement =
          HTMLEditUtils::GetClosestAncestorTableElement(*content);
      if (!tableElement) {
        continue;
      }
      // If we find a `<table>` element which is an ancestor of a table
      // related element and is not an acestor of first nor last of
      // aArrayOfNodes, return the last found list or `<table>` element.
      // XXX Is that really expected that this returns a list element in this
      //     case?
      if (!aInclusiveAncestorsTableOrListElements.Contains(tableElement)) {
        return lastFoundAncestorListOrTableElement;
      }
      // If we find a `<table>` element which is topmost list or `<table>`
      // element at first or last of aArrayOfNodes, return it.
      if (aInclusiveAncestorsTableOrListElements.LastElement().get() ==
          tableElement) {
        return tableElement;
      }
      // Otherwise, store the `<table>` element which is an ancestor but
      // not topmost ancestor of first or last of aArrayOfNodes.
      lastFoundAncestorListOrTableElement = tableElement;
      continue;
    }

    if (!HTMLEditUtils::IsListItemElement(*content)) {
      continue;
    }
    Element* listElement =
        HTMLEditUtils::GetClosestAncestorAnyListElement(*content);
    if (!listElement) {
      continue;
    }
    // If we find a list element which is ancestor of a list item element and
    // is not an acestor of first nor last of aArrayOfNodes, return the last
    // found list or `<table>` element.
    // XXX Is that really expected that this returns a `<table>` element in
    //     this case?
    if (!aInclusiveAncestorsTableOrListElements.Contains(listElement)) {
      return lastFoundAncestorListOrTableElement;
    }
    // If we find a list element which is topmost list or `<table>` element at
    // first or last of aArrayOfNodes, return it.
    if (aInclusiveAncestorsTableOrListElements.LastElement().get() ==
        listElement) {
      return listElement;
    }
    // Otherwise, store the list element which is an ancestor but not topmost
    // ancestor of first or last of aArrayOfNodes.
    lastFoundAncestorListOrTableElement = listElement;
  }

  // If we find only non-topmost list or `<table>` element, returns the last
  // found one (meaning bottommost one).  Otherwise, nullptr.
  return lastFoundAncestorListOrTableElement;
}

Element*
HTMLEditor::AutoHTMLFragmentBoundariesFixer::FindReplaceableTableElement(
    Element& aTableElement, nsIContent& aContentMaybeInTableElement) const {
  MOZ_ASSERT(aTableElement.IsHTMLElement(nsGkAtoms::table));
  // Perhaps, this is designed for climbing up the DOM tree from
  // aContentMaybeInTableElement to aTableElement and making sure that
  // aContentMaybeInTableElement itself or its ancestor is a `<td>`, `<th>`,
  // `<tr>`, `<thead>`, `<tbody>`, `<tfoot>` or `<caption>`.
  // But this looks really buggy because this loop may skip aTableElement
  // as the following NS_ASSERTION.  We should write automated tests and
  // check right behavior.
  for (Element* element =
           aContentMaybeInTableElement.GetAsElementOrParentElement();
       element; element = element->GetParentElement()) {
    if (!HTMLEditUtils::IsAnyTableElementExceptColumnElement(*element) ||
        element->IsHTMLElement(nsGkAtoms::table)) {
      // XXX Perhaps, the original developer of this method assumed that
      //     aTableElement won't be skipped because if it's assumed, we can
      //     stop climbing up the tree in that case.
      NS_ASSERTION(element != &aTableElement,
                   "The table element which is looking for is ignored");
      continue;
    }
    Element* tableElement = nullptr;
    for (Element* maybeTableElement = element->GetParentElement();
         maybeTableElement;
         maybeTableElement = maybeTableElement->GetParentElement()) {
      if (maybeTableElement->IsHTMLElement(nsGkAtoms::table)) {
        tableElement = maybeTableElement;
        break;
      }
    }
    if (tableElement == &aTableElement) {
      return element;
    }
    // XXX If we find another `<table>` element, why don't we keep searching
    //     from its parent?
  }
  return nullptr;
}

bool HTMLEditor::AutoHTMLFragmentBoundariesFixer::IsReplaceableListElement(
    Element& aListElement, nsIContent& aContentMaybeInListElement) const {
  MOZ_ASSERT(HTMLEditUtils::IsListElement(aListElement));
  // Perhaps, this is designed for climbing up the DOM tree from
  // aContentMaybeInListElement to aListElement and making sure that
  // aContentMaybeInListElement itself or its ancestor is an list item.
  // But this looks really buggy because this loop may skip aListElement
  // as the following NS_ASSERTION.  We should write automated tests and
  // check right behavior.
  for (Element* element =
           aContentMaybeInListElement.GetAsElementOrParentElement();
       element; element = element->GetParentElement()) {
    if (!HTMLEditUtils::IsListItemElement(*element)) {
      // XXX Perhaps, the original developer of this method assumed that
      //     aListElement won't be skipped because if it's assumed, we can
      //     stop climbing up the tree in that case.
      NS_ASSERTION(element != &aListElement,
                   "The list element which is looking for is ignored");
      continue;
    }
    Element* listElement =
        HTMLEditUtils::GetClosestAncestorAnyListElement(*element);
    if (listElement == &aListElement) {
      return true;
    }
    // XXX If we find another list element, why don't we keep searching
    //     from its parent?
  }
  return false;
}

void HTMLEditor::AutoHTMLFragmentBoundariesFixer::
    EnsureBeginsOrEndsWithValidContent(StartOrEnd aStartOrEnd,
                                       nsTArray<OwningNonNull<nsIContent>>&
                                           aArrayOfTopMostChildContents) const {
  MOZ_ASSERT(!aArrayOfTopMostChildContents.IsEmpty());

  // Collect list elements and table related elements at first or last node
  // in aArrayOfTopMostChildContents.
  AutoTArray<OwningNonNull<Element>, 4> inclusiveAncestorsListOrTableElements;
  CollectTableAndAnyListElementsOfInclusiveAncestorsAt(
      aStartOrEnd == StartOrEnd::end
          ? aArrayOfTopMostChildContents.LastElement()
          : aArrayOfTopMostChildContents[0],
      inclusiveAncestorsListOrTableElements);
  if (inclusiveAncestorsListOrTableElements.IsEmpty()) {
    return;
  }

  // Get most ancestor list or `<table>` element in
  // inclusiveAncestorsListOrTableElements which contains earlier
  // node in aArrayOfTopMostChildContents as far as possible.
  // XXX With inclusiveAncestorsListOrTableElements, this returns a
  //     list or `<table>` element which contains first or last node of
  //     aArrayOfTopMostChildContents.  However, this seems slow when
  //     aStartOrEnd is StartOrEnd::end and only the last node is in
  //     different list or `<table>`.  But I'm not sure whether it's
  //     possible case or not.  We need to add tests to
  //     test_content_iterator_subtree.html for checking how
  //     SubtreeContentIterator works.
  Element* listOrTableElement = GetMostDistantAncestorListOrTableElement(
      aArrayOfTopMostChildContents, inclusiveAncestorsListOrTableElements);
  if (!listOrTableElement) {
    return;
  }

  // If we have pieces of tables or lists to be inserted, let's force the
  // insertion to deal with table elements right away, so that it doesn't
  // orphan some table or list contents outside the table or list.

  OwningNonNull<nsIContent>& firstOrLastChildContent =
      aStartOrEnd == StartOrEnd::end
          ? aArrayOfTopMostChildContents.LastElement()
          : aArrayOfTopMostChildContents[0];

  // Find substructure of list or table that must be included in paste.
  Element* replaceElement;
  if (HTMLEditUtils::IsListElement(*listOrTableElement)) {
    if (!IsReplaceableListElement(*listOrTableElement,
                                  firstOrLastChildContent)) {
      return;
    }
    replaceElement = listOrTableElement;
  } else {
    MOZ_ASSERT(listOrTableElement->IsHTMLElement(nsGkAtoms::table));
    replaceElement = FindReplaceableTableElement(*listOrTableElement,
                                                 firstOrLastChildContent);
    if (!replaceElement) {
      return;
    }
  }

  // If we can replace the given list element or found a table related element
  // in the `<table>` element, insert it into aArrayOfTopMostChildContents which
  // is tompost children to be inserted instead of descendants of them in
  // aArrayOfTopMostChildContents.
  for (size_t i = 0; i < aArrayOfTopMostChildContents.Length();) {
    OwningNonNull<nsIContent>& content = aArrayOfTopMostChildContents[i];
    if (content == replaceElement) {
      // If the element is n aArrayOfTopMostChildContents, its descendants must
      // not be in the array.  Therefore, we don't need to optimize this case.
      // XXX Perhaps, we can break this loop right now.
      aArrayOfTopMostChildContents.RemoveElementAt(i);
      continue;
    }
    if (!EditorUtils::IsDescendantOf(content, *replaceElement)) {
      i++;
      continue;
    }
    // For saving number of calls of EditorUtils::IsDescendantOf(), we should
    // remove its siblings in the array.
    nsIContent* parent = content->GetParent();
    aArrayOfTopMostChildContents.RemoveElementAt(i);
    while (i < aArrayOfTopMostChildContents.Length() &&
           aArrayOfTopMostChildContents[i]->GetParent() == parent) {
      aArrayOfTopMostChildContents.RemoveElementAt(i);
    }
  }

  // Now replace the removed nodes with the structural parent
  if (aStartOrEnd == StartOrEnd::end) {
    aArrayOfTopMostChildContents.AppendElement(*replaceElement);
  } else {
    aArrayOfTopMostChildContents.InsertElementAt(0, *replaceElement);
  }
}

}  // namespace mozilla
